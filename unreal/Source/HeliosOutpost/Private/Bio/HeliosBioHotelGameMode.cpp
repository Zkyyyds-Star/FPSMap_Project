#include "Bio/HeliosBioHotelGameMode.h"
#include "Bio/HeliosBioPlayer.h"
#include "Bio/HeliosBioNPC.h"
#include "Bio/HeliosBioSupply.h"
#include "Bio/HeliosBioSupplyWeaponDefinition.h"
#include "Bio/HeliosBioRoundInventoryComponent.h"
#include "Bio/HeliosBioClone.h"
#include "Bio/HeliosBioMovingPlatform.h"
#include "Bio/HeliosBioSkillProjectile.h"
#include "Bio/HeliosBioSerumProjectile.h"
#include "Bio/HeliosBioTurret.h"
#include "Bio/HeliosBioStaticJumpLink.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Weapons/HeliosSSXEffect.h"
#include "GameFramework/Character.h"
#include "Animation/AnimSequence.h"
#include "TimerManager.h"
#include "Bio/HeliosBioHUD.h"
#include "Bio/HeliosBioArt.h"
#include "World/HeliosMapTravelSubsystem.h"
#include "Player/HeliosPlayerController.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "UObject/UnrealType.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Sound/SoundBase.h"
#include "ShooterWeapon.h"

namespace
{
void BioSound(UObject* Context,const TCHAR* Name)
{const FString P=FString::Printf(TEXT("/Game/Helios/CFMBioHotel/Audio/%s.%s"),Name,Name);if(auto* S=LoadObject<USoundBase>(nullptr,*P))UGameplayStatics::PlaySound2D(Context,S,.75f);}
EHeliosBioAbility AbilityFor(const UHeliosBioRoleDefinition* Art,const FHeliosBioParticipant& P)
{
    if(Art&&Art->Ability!=EHeliosBioAbility::None)return Art->Ability;
    if(Art&&Art->SourceIdentity.Contains(TEXT("SunShangXiang"),ESearchCase::IgnoreCase))return EHeliosBioAbility::FourSlash;
    if(Art&&Art->SourceIdentity.Contains(TEXT("SunWuKong"),ESearchCase::IgnoreCase))return EHeliosBioAbility::AreaStrike;
    if(Art&&Art->SourceIdentity.Contains(TEXT("BaiGuJing"),ESearchCase::IgnoreCase))return EHeliosBioAbility::CloneSummon;
    return P.ZombieForm==EHeliosBioZombieForm::Terminator?EHeliosBioAbility::Shield:EHeliosBioAbility::Sprint;
}
bool AbilityImplemented(EHeliosBioAbility Ability)
{
    return Ability!=EHeliosBioAbility::None;
}
bool TurretPlacement(ACharacter* Source,UHeliosBioRoleDefinition* Art,FTransform& Placement)
{
    auto* Mesh=Art?Art->SkillAuxiliaryMesh.LoadSynchronous():nullptr;
    if(!Source||!Mesh)return false;
    const FRotator Aim=Source->GetBaseAimRotation();
    const FVector At=Source->GetActorLocation()+Aim.Vector().GetSafeNormal2D()*150.f;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(BioTurretPlacement),false,Source);TArray<AActor*> Attached;Source->GetAttachedActors(Attached,true,true);Q.AddIgnoredActors(Attached);
    FHitResult Floor;FCollisionObjectQueryParams Static(ECC_WorldStatic);
    if(!Source->GetWorld()->LineTraceSingleByObjectType(Floor,At+FVector(0,0,180),At-FVector(0,0,350),Static,Q)||Floor.ImpactNormal.Z<.85f)return false;
    const FBoxSphereBounds Bounds=Mesh->GetBounds();const FVector Location=Floor.ImpactPoint+FVector(0,0,2.f-Bounds.Origin.Z+Bounds.BoxExtent.Z);
    const FQuat Rotation=FRotator(0,Aim.Yaw,0).Quaternion();const FVector Center=Location+Rotation.RotateVector(Bounds.Origin);
    // Slightly shrink the bottom so the support floor itself does not invalidate placement.
    if(Source->GetWorld()->OverlapBlockingTestByChannel(Center+FVector(0,0,2),Rotation,ECC_Pawn,FCollisionShape::MakeBox(FVector(Bounds.BoxExtent.X+4,Bounds.BoxExtent.Y+4,FMath::Max(1.f,Bounds.BoxExtent.Z-2))),Q))return false;
    Placement=FTransform(Rotation,Location);return true;
}
bool RoleArtReady(UHeliosBioRoleDefinition* Art,EHeliosBioAbility Ability)
{
    if(!Art||!Art->bIdentityVerified)return false;
    auto* FP=Art->FirstPersonMesh.LoadSynchronous();auto* TP=Art->ThirdPersonMesh.LoadSynchronous();
    if(!FP||!TP)return false;
    const auto ValidClip=[](const TSoftObjectPtr<UAnimSequence>& Ref,USkeletalMesh* Mesh)
    {auto* Clip=Ref.LoadSynchronous();return Clip&&Mesh&&Clip->GetSkeleton()==Mesh->GetSkeleton()&&FMath::IsFinite(Clip->GetPlayLength())&&Clip->GetPlayLength()>0;};
    if(!ValidClip(Art->FPIdle,FP)||!ValidClip(Art->FPAttack,FP)||!ValidClip(Art->FPHeavy,FP)||
        !ValidClip(Art->TPIdle,TP)||!ValidClip(Art->TPAttack,TP)||!ValidClip(Art->TPRun,TP))return false;
    if(Ability==EHeliosBioAbility::FourSlash)
    {
        if(Art->FPSkillChain.Num()!=4||Art->TPSkillChain.Num()!=4)return false;
        for(int32 I=0;I<4;++I)if(!ValidClip(Art->FPSkillChain[I],FP)||!ValidClip(Art->TPSkillChain[I],TP))return false;
    }
    else if(!ValidClip(Art->FPSkill,Art->SkillFirstPersonMesh.IsNull()?FP:Art->SkillFirstPersonMesh.LoadSynchronous())||!ValidClip(Art->TPSkill,TP))return false;
    if((Ability==EHeliosBioAbility::FreezeGrenade||Ability==EHeliosBioAbility::AbyssEnergy)&&!Art->SkillProjectileMesh.LoadSynchronous())return false;
    if((Ability==EHeliosBioAbility::Shield||Ability==EHeliosBioAbility::SacredShield)&&Art->Ability!=EHeliosBioAbility::None&&Art->SkillShieldReduction<=0)return false;
    if(Ability==EHeliosBioAbility::MechanicalFire)
    {
        auto* PrimaryFP=Art->PrimaryFirstPersonMesh.LoadSynchronous();auto* PrimaryTP=Art->PrimaryThirdPersonWeaponMesh.LoadSynchronous();
        if(!Art->SkillAuxiliaryMesh.LoadSynchronous()||(!Art->SecondaryProjectileMesh.LoadSynchronous()&&Art->SecondaryProjectileEffect.IsEmpty())||!PrimaryFP||!PrimaryTP)return false;
        for(auto Clip:{Art->FPPrimaryIdle,Art->FPPrimaryFire})if(!Clip.LoadSynchronous()||Clip.LoadSynchronous()->GetSkeleton()!=PrimaryFP->GetSkeleton())return false;
        for(auto Clip:{Art->TPPrimaryIdle,Art->TPPrimaryFire})if(!Clip.LoadSynchronous()||Clip.LoadSynchronous()->GetSkeleton()!=PrimaryTP->GetSkeleton())return false;
    }
    if(Ability==EHeliosBioAbility::ZhongXiaoKuiBlade&&(Art->SkillEffectLibrary.IsEmpty()||Art->SwordWaveEffect.IsEmpty()))return false;
    return AbilityImplemented(Ability);
}
}
AHeliosBioHotelGameMode::AHeliosBioHotelGameMode()
{PrimaryActorTick.bCanEverTick=true;DefaultPawnClass=AHeliosBioPlayer::StaticClass();PlayerControllerClass=AHeliosPlayerController::StaticClass();HUDClass=AHeliosBioHUD::StaticClass();Model.Config.ParticipantCount=30;Model.Config.bKeepLocalPlayerHuman=false;}
AHeliosBioHotelGameMode* AHeliosBioHotelGameMode::Find(UWorld* W)
{return W?Cast<AHeliosBioHotelGameMode>(W->GetAuthGameMode()):nullptr;}
void AHeliosBioHotelGameMode::StartPlay()
{
    AGameModeBase::StartPlay();
    LoadSourcePoints();
    LoadRuntimeProfile();
    LoadOriginalStaticJumpLinks();
    if(auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
    {
        // Finish registration before changing build geometry. Changing all
        // agents' registered configs makes the separate boss/default meshes
        // collide in the navigation registry and deletes the calibrated data.
        for(TActorIterator<ANavMeshBoundsVolume> It(GetWorld());It;++It)Nav->OnNavigationBoundsUpdated(*It);Nav->Build();
        if(bFloatingProfile)if(auto* Mesh=Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)))
        {
            Mesh->AgentRadius=Model.Config.MovementCapsuleRadius;Mesh->AgentHeight=Model.Config.MovementCapsuleHalfHeight*2;
            Mesh->AgentMaxSlope=Model.Config.MovementSlopeDegrees;
            // Preserve the original 67 cm ramp and 73 cm box tops. At the engine's
            // 19 cm default cell size ceil(20/19) erodes 38 cm from each edge.
            for(auto Resolution:{ENavigationDataResolution::Low,ENavigationDataResolution::Default,ENavigationDataResolution::High})
            {Mesh->SetAgentMaxStepHeight(Resolution,Model.Config.MovementStepHeight);Mesh->SetCellSize(Resolution,8.f);Mesh->SetCellHeight(Resolution,2.f);}
            // RuntimeGeneration has no public setter. Use its reflected property
            // for this world-only rebuild, then restore the saved static policy.
            auto* RuntimeProperty=FindFProperty<FProperty>(ANavigationData::StaticClass(),TEXT("RuntimeGeneration"));
            if(RuntimeProperty)
            {
                auto* RuntimeMode=RuntimeProperty->ContainerPtrToValuePtr<ERuntimeGenerationType>(Mesh);const auto PreviousMode=*RuntimeMode;
                *RuntimeMode=ERuntimeGenerationType::Dynamic;Nav->Build();Mesh->EnsureBuildCompletion();*RuntimeMode=PreviousMode;
            }
            else UE_LOG(LogTemp,Error,TEXT("BIO_SOURCE_NAV_REBUILD_UNAVAILABLE RuntimeGeneration property missing"));
            UE_LOG(LogTemp,Display,TEXT("BIO_SOURCE_NAV_DIMENSIONS mesh=%s radius=%.1f height=%.1f step=%.1f slope=%.1f cell=%.1f voxel_height=%.1f registry_radius=%.1f"),*Mesh->GetName(),Mesh->AgentRadius,Mesh->AgentHeight,Mesh->GetAgentMaxStepHeight(ENavigationDataResolution::Default),Mesh->AgentMaxSlope,Mesh->GetCellSize(ENavigationDataResolution::Default),Mesh->GetCellHeight(ENavigationDataResolution::Default),Mesh->GetConfig().AgentRadius);
        }
    }
    if(auto* MotherArt=LoadObject<UHeliosBioRoleDefinition>(nullptr,TEXT("/Game/Helios/CFMBioHotel/Characters/DA_BioTerminator.DA_BioTerminator")))Model.Config.bMothersAreTerminators=MotherArt->bIdentityVerified&&!MotherArt->ThirdPersonMesh.IsNull();
    RefreshHunterPool();
    LoadOriginalMovingPlatforms();
    SelectedLoadout=FHeliosLoadout::FromLegacyWeapon(TEXT("cfm_chan_zeus"));
    if(auto* T=GetGameInstance()->GetSubsystem<UHeliosMapTravelSubsystem>())
    {if(T->bPendingStart){SelectedLoadout=T->PendingLoadout;T->bPendingStart=false;}}
    // The source infantry map uses the existing native firearm loadout.
    if(SelectedLoadout.PrimaryWeaponId==TEXT("mech")||SelectedLoadout.PrimaryWeaponId==TEXT("fighter"))SelectedLoadout=FHeliosLoadout::FromLegacyWeapon(TEXT("cfm_chan_zeus"));
    Player=Cast<AHeliosBioPlayer>(UGameplayStatics::GetPlayerPawn(this,0));
    if(Player&&!Player->EquipBioLoadout(SelectedLoadout))UE_LOG(LogTemp,Error,TEXT("BIOHOTEL_LOADOUT_FAILED %s"),*SelectedLoadout.PrimaryWeaponId.ToString());
    RestartBioRound();
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosBioHotelQA")))
        if(UClass* C=LoadClass<AActor>(nullptr,TEXT("/Script/HeliosOutpost.HeliosBioHotelQA")))GetWorld()->SpawnActor<AActor>(C);
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosBioRoleQA")))
        if(UClass* C=LoadClass<AActor>(nullptr,TEXT("/Script/HeliosOutpost.HeliosBioRoleQA")))GetWorld()->SpawnActor<AActor>(C);
    if(FParse::Param(FCommandLine::Get(),TEXT("BioHumanActionsQA")))
        if(UClass* C=LoadClass<AActor>(nullptr,TEXT("/Script/HeliosOutpost.HeliosBioHumanActionsQA")))GetWorld()->SpawnActor<AActor>(C);
    if(FParse::Param(FCommandLine::Get(),TEXT("BioNavAudit")))
        if(UClass* C=LoadClass<AActor>(nullptr,TEXT("/Script/HeliosOutpost.HeliosBioNavAudit")))GetWorld()->SpawnActor<AActor>(C);
    if(FParse::Param(FCommandLine::Get(),TEXT("BioNativeInputQA")))
        if(UClass* C=LoadClass<AActor>(nullptr,TEXT("/Script/HeliosOutpost.HeliosBioNativeInputQA")))GetWorld()->SpawnActor<AActor>(C);
    if(FParse::Param(FCommandLine::Get(),TEXT("BioIterationQA")))
        if(UClass* C=LoadClass<AActor>(nullptr,TEXT("/Script/HeliosOutpost.HeliosBioIterationQA")))GetWorld()->SpawnActor<AActor>(C);
    if(FParse::Param(FCommandLine::Get(),TEXT("BioSupplyWeaponQA")))
        if(UClass* C=LoadClass<AActor>(nullptr,TEXT("/Script/HeliosOutpost.HeliosBioSupplyWeaponQA")))GetWorld()->SpawnActor<AActor>(C);
    UE_LOG(LogTemp,Display,TEXT("BIOHOTEL_READY spawns=%d supplies=%d participants=%d separate_offline_rules=true"),SpawnPoints.Num(),SupplyPoints.Num(),Model.GetParticipants().Num());
}
void AHeliosBioHotelGameMode::LoadSourcePoints()
{
    for(TActorIterator<AActor> It(GetWorld());It;++It)
    {if(It->ActorHasTag(TEXT("BioSpawn"))){SpawnPoints.Add(It->GetActorLocation());SpawnRotations.Add(It->GetActorRotation());}if(It->ActorHasTag(TEXT("BioSupply")))SupplyPoints.Add(It->GetActorLocation());}
    if(SpawnPoints.IsEmpty())for(TActorIterator<APlayerStart> It(GetWorld());It;++It){SpawnPoints.Add(It->GetActorLocation());SpawnRotations.Add(It->GetActorRotation());}
    if(SpawnPoints.IsEmpty())UE_LOG(LogTemp,Error,TEXT("BIOHOTEL_SOURCE_SPAWNS_MISSING"));
    if(SupplyPoints.IsEmpty())UE_LOG(LogTemp,Error,TEXT("BIOHOTEL_SOURCE_SUPPLIES_MISSING"));
}
void AHeliosBioHotelGameMode::LoadOriginalStaticJumpLinks()
{
    if(!bFloatingProfile)return;
    FString Text;TSharedPtr<FJsonObject> Data;
    const FString File=FPaths::ProjectContentDir()/TEXT("Helios/CFMBioPyramidFloating/Map/static_jump_routes_runtime.json");
    if(!FFileHelper::LoadFileToString(Text,*File)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Data)||!Data.IsValid())
    {UE_LOG(LogTemp,Error,TEXT("BIO_STATIC_JUMP_PROFILE_MISSING %s"),*File);return;}
    const TArray<TSharedPtr<FJsonValue>>* Links=nullptr;if(!Data->TryGetArrayField(TEXT("links"),Links))return;
    const auto Vector=[](const TSharedPtr<FJsonObject>& Row,const TCHAR* Key,FVector& Result)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;if(!Row.IsValid()||!Row->TryGetArrayField(Key,Values)||Values->Num()!=3)return false;
        double X,Y,Z;if(!(*Values)[0]->TryGetNumber(X)||!(*Values)[1]->TryGetNumber(Y)||!(*Values)[2]->TryGetNumber(Z))return false;
        Result=FVector(X,Y,Z);return !Result.ContainsNaN();
    };
    int32 Count=0;
    for(const auto& Value:*Links)
    {
        auto Row=Value->AsObject();FVector Start,End;if(!Vector(Row,TEXT("start_surface_cm"),Start)||!Vector(Row,TEXT("end_surface_cm"),End))continue;
        // Endpoints are measured original solid surfaces, with the normal UE floor gap.
        Start.Z+=2;End.Z+=2;FActorSpawnParameters Params;Params.Owner=this;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        bool Bidirectional=true;Row->TryGetBoolField(TEXT("bidirectional"),Bidirectional);
        auto* Link=GetWorld()->SpawnActor<AHeliosBioStaticJumpLink>(AHeliosBioStaticJumpLink::StaticClass(),Start,FRotator::ZeroRotator,Params);
        bool Configured=false;
        const TArray<TSharedPtr<FJsonValue>>* Segments=nullptr;
        if(Link&&Row->TryGetArrayField(TEXT("physical_segments"),Segments))
        {
            TArray<FVector> Points;TArray<bool> WalkSegments;Points.Add(Start);bool Valid=true;
            for(const auto& SegmentValue:*Segments)
            {
                auto Segment=SegmentValue->AsObject();FVector Point;FString Kind;
                if(!Vector(Segment,TEXT("end_surface_cm"),Point)||!Segment->TryGetStringField(TEXT("kind"),Kind)||(Kind!=TEXT("walk")&&Kind!=TEXT("jump"))){Valid=false;break;}
                Point.Z+=2;Points.Add(Point);WalkSegments.Add(Kind==TEXT("walk"));
            }
            if(Valid&&!Points.IsEmpty()&&FVector::Dist(Points.Last(),End)<.1f)Configured=Link->ConfigurePhysicalRoute(Points,WalkSegments,Bidirectional);
        }
        else if(Link)Configured=Link->Configure(Start,End,Bidirectional);
        if(Link&&!Configured){Link->Destroy();Link=nullptr;}
        if(Link)++Count;
    }
    UE_LOG(LogTemp,Display,TEXT("BIO_STATIC_JUMP_PROFILE links=%d geometry_changed=false source_authored_nav_links=false native_character_jump=true"),Count);
}
void AHeliosBioHotelGameMode::LoadRuntimeProfile()
{
    FParse::Value(FCommandLine::Get(),TEXT("BioRoundSeed="),BaseRoundSeed);
    bFloatingProfile=GetWorld()->GetOutermost()->GetName().EndsWith(TEXT("/CFMBioPyramidFloating"));
    if(!bFloatingProfile)return;
    // A missing camera JSON must not silently restore another map's old viewpoint.
    Model.Config.PlayerCamera.bEnabled=true;
    FString Text;TSharedPtr<FJsonObject> Data;
    const FString File=FPaths::ProjectContentDir()/TEXT("Helios/CFMBioPyramidFloating/Map/rules_runtime.json");
    if(!FFileHelper::LoadFileToString(Text,*File)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Data)||!Data.IsValid())
    {UE_LOG(LogTemp,Error,TEXT("BIO_FLOATING_RULE_PROFILE_MISSING %s"),*File);return;}
    const auto Number=[&](const TCHAR* Key,double Default){double Value;return Data->TryGetNumberField(Key,Value)&&FMath::IsFinite(Value)?Value:Default;};
    auto& C=Model.Config;
    C.ParticipantCount=FMath::Clamp((int32)Number(TEXT("participants"),30),2,64);
    C.PreparationSeconds=FMath::Clamp((float)Number(TEXT("preparation_seconds"),20),0.f,120.f);
    C.RoundSeconds=FMath::Clamp((float)Number(TEXT("round_seconds"),180),10.f,1800.f);
    C.InitialMotherCount=FMath::Clamp((int32)Number(TEXT("initial_mothers"),2),1,C.ParticipantCount-1);
    C.HunterThreshold=FMath::Clamp((int32)Number(TEXT("hunter_threshold"),2),1,C.ParticipantCount-1);
    C.SupplyIntervalSeconds=FMath::Clamp((float)Number(TEXT("supply_interval_seconds"),30),1.f,300.f);
    C.NormalSupplyCount=FMath::Clamp((int32)Number(TEXT("normal_supply_count"),3),0,SupplyPoints.Num());
    C.BlueSupplyCount=FMath::Clamp((int32)Number(TEXT("blue_supply_count"),2),0,SupplyPoints.Num());
    C.RedSupplyCount=FMath::Clamp((int32)Number(TEXT("red_supply_count"),1),0,SupplyPoints.Num());
    C.HumanDamageMultiplier=FMath::Clamp((float)Number(TEXT("human_damage_multiplier"),6),.1f,100.f);
    C.ZombieMeleeDamage=FMath::Clamp((float)Number(TEXT("zombie_melee_damage"),200),1.f,10000.f);
    C.MovementCapsuleRadius=FMath::Clamp((float)Number(TEXT("movement_capsule_radius_cm"),20),10.f,100.f);
    C.MovementCapsuleHalfHeight=FMath::Clamp((float)Number(TEXT("movement_capsule_half_height_cm"),70),C.MovementCapsuleRadius,150.f);
    C.MovementStepHeight=FMath::Clamp((float)Number(TEXT("movement_step_height_cm"),20),1.f,80.f);
    C.MovementSlopeDegrees=FMath::Clamp((float)Number(TEXT("movement_slope_degrees"),45),1.f,60.f);
    C.MovementJumpZ=FMath::Clamp((float)Number(TEXT("movement_jump_z_cm_s"),500),100.f,900.f);
    C.EyeHeight=FMath::Clamp((float)Number(TEXT("eye_height_cm"),160),80.f,200.f);
    const TSharedPtr<FJsonObject>* PlayerView=nullptr;
    if(Data->TryGetObjectField(TEXT("player_camera"),PlayerView)&&PlayerView&&PlayerView->IsValid())
    {
        // This optional profile is read only for the floating map. EyeHeight above
        // remains the independent NPC combat/legacy-player value.
        auto& View=C.PlayerCamera;bool Enabled=View.bEnabled;(*PlayerView)->TryGetBoolField(TEXT("enabled"),Enabled);View.bEnabled=Enabled;
        const auto ViewNumber=[PlayerView](const TCHAR* Key,double Default){double Value;return (*PlayerView)->TryGetNumberField(Key,Value)&&FMath::IsFinite(Value)?Value:Default;};
        View.StandingEyeHeight=FMath::Clamp((float)ViewNumber(TEXT("standing_eye_height_cm"),90),20.f,240.f);
        View.CrouchedEyeHeight=FMath::Clamp((float)ViewNumber(TEXT("crouched_eye_height_cm"),60),10.f,View.StandingEyeHeight);
        View.CrouchedCapsuleHalfHeight=FMath::Clamp((float)ViewNumber(TEXT("crouched_capsule_half_height_cm"),40),C.MovementCapsuleRadius,C.MovementCapsuleHalfHeight);
        View.HorizontalOffset=FVector2D(FMath::Clamp((float)ViewNumber(TEXT("relative_x_cm"),0),-100.f,100.f),FMath::Clamp((float)ViewNumber(TEXT("relative_y_cm"),0),-100.f,100.f));
        View.WorldVerticalFOV=FMath::Clamp((float)ViewNumber(TEXT("world_vertical_fov_degrees"),70),10.f,150.f);
        View.FirstPersonVerticalFOV=FMath::Clamp((float)ViewNumber(TEXT("first_person_vertical_fov_degrees"),33),10.f,150.f);
        View.FirstPersonScale=FMath::Clamp((float)ViewNumber(TEXT("first_person_scale"),1),.001f,1.f);
        UE_LOG(LogTemp,Display,TEXT("BIO_FLOATING_PLAYER_CAMERA enabled=%d stand_eye=%.2f crouch_eye=%.2f crouch_half=%.2f offset_xy=%.2f,%.2f world_vfov=%.3f first_person_vfov=%.3f first_person_scale=%.3f NPC_eye_unchanged=%.2f source_scope=authored_socket_and_scene_FOV_singleplayer_bridge_runtime_overrides_unknown"),View.bEnabled,View.StandingEyeHeight,View.CrouchedEyeHeight,View.CrouchedCapsuleHalfHeight,View.HorizontalOffset.X,View.HorizontalOffset.Y,View.WorldVerticalFOV,View.FirstPersonVerticalFOV,View.FirstPersonScale,C.EyeHeight);
    }
    bool TerminatorMothers=false;
    if(Data->TryGetBoolField(TEXT("mothers_are_terminators"),TerminatorMothers)&&TerminatorMothers)
    {
        FHeliosBioParticipant Probe;Probe.Team=EHeliosBioTeam::Zombie;Probe.ZombieForm=EHeliosBioZombieForm::Terminator;
        auto* MotherArt=HeliosBioArt::Load(Probe);
        C.bMothersAreTerminators=MotherArt&&MotherArt->bIdentityVerified&&MotherArt->FirstPersonMesh.LoadSynchronous()&&MotherArt->ThirdPersonMesh.LoadSynchronous()&&MotherArt->SourceIdentity.Contains(TEXT("Terminator"),ESearchCase::IgnoreCase);
    }
    const TArray<TSharedPtr<FJsonValue>>* Roles=nullptr;
    if(Data->TryGetArrayField(TEXT("allowed_hunters"),Roles))for(const auto& Value:*Roles){FString RoleName;if(Value->TryGetString(RoleName))ProfileHunterRoles.AddUnique(FName(RoleName));}
    FString Abyss;if(Data->TryGetStringField(TEXT("abyss_role"),Abyss))
    {
        auto* Art=HeliosBioArt::Named(FName(Abyss));
        C.bAbyssSourceAvailable=Art&&Art->bIdentityVerified&&Art->bGameplayVerified&&Art->Ability==EHeliosBioAbility::AbyssEnergy&&Art->FirstPersonMesh.LoadSynchronous()&&Art->ThirdPersonMesh.LoadSynchronous()&&Art->FPAttack.LoadSynchronous()&&Art->TPAttack.LoadSynchronous();
        C.AbyssRoleId=C.bAbyssSourceAvailable?FName(Abyss):NAME_None;
    }
    UE_LOG(LogTemp,Display,TEXT("BIO_FLOATING_RULE_PROFILE participants=%d boxes=%d/%d/%d abyss_ready=%d base_seed=%d offline_balance=true"),C.ParticipantCount,C.NormalSupplyCount,C.BlueSupplyCount,C.RedSupplyCount,C.bAbyssSourceAvailable,BaseRoundSeed);
}
FVector AHeliosBioHotelGameMode::SpawnLocation(int32 Id)const
{
    if(SpawnPoints.IsEmpty())return Player?Player->GetActorLocation():FVector::ZeroVector;
    const int32 Index=(Id*7+RoundSerial*3)%SpawnPoints.Num();FVector P=SpawnPoints[Index];
    FHitResult Hit;FCollisionObjectQueryParams Objects(ECC_WorldStatic);FCollisionQueryParams Q(SCENE_QUERY_STAT(BioSourceFloor),false);
    if(GetWorld()->LineTraceSingleByObjectType(Hit,P+FVector(0,0,150),P-FVector(0,0,1500),Objects,Q))P.Z=Hit.ImpactPoint.Z+Model.Config.MovementCapsuleHalfHeight+2;
    return P;
}
FRotator AHeliosBioHotelGameMode::SpawnRotation(int32 Id)const
{return SpawnRotations.IsEmpty()?FRotator::ZeroRotator:SpawnRotations[(Id*7+RoundSerial*3)%SpawnRotations.Num()];}
void AHeliosBioHotelGameMode::RestartBioRound()
{
    UGameplayStatics::SetGamePaused(this,false);
    bHunterChooserOpen=false;
    for(auto Platform:MovingPlatforms)if(IsValid(Platform))Platform->ResetSourceRoute(true);
    CurrentRoundSeed=BaseRoundSeed+RoundSerial*103;
    SupplyWeaponBag.Reset();SupplyWeaponBagCursor=SupplyWeaponBagCycle=0;
    Model.StartRound(0,CurrentRoundSeed);++RoundSerial;RoundOverAge=0;LastSupplyWave=-1;LastCountdown=-1;PreviousPhase=EHeliosBioPhase::Preparation;
    for(auto C:Clones)if(IsValid(C))C->Destroy();Clones.Reset();
    for(auto Turret:Turrets)if(IsValid(Turret))Turret->Destroy();Turrets.Reset();NextRoleWeaponShot.Reset();
    for(auto S:Supplies)if(S)S->Destroy();Supplies.Reset();
    if(Player)Player->ResetForBioRound();
    if(Bots.IsEmpty())
    {
        for(int32 Id=1;Id<Model.GetParticipants().Num();++Id)
        {
            const FTransform T(SpawnRotation(Id),SpawnLocation(Id));
            auto* Bot=GetWorld()->SpawnActorDeferred<AHeliosBioNPC>(AHeliosBioNPC::StaticClass(),T,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if(Bot){Bot->InitializeBioParticipant(Id,CurrentRoundSeed,SelectedLoadout);UGameplayStatics::FinishSpawningActor(Bot,T);Bots.Add(Bot);}
        }
    }
    else for(auto Bot:Bots)if(Bot){Bot->InitializeBioParticipant(Bot->ParticipantId,CurrentRoundSeed,SelectedLoadout);Bot->ResetForBioRound();Bot->SetActorRotation(SpawnRotation(Bot->ParticipantId));}
    if(auto* PC=UGameplayStatics::GetPlayerController(this,0)){PC->bShowMouseCursor=false;PC->SetInputMode(FInputModeGameOnly());}
    ShowNotice(TEXT("选择高点防守；生化幽灵即将出现"),5);
}
AActor* AHeliosBioHotelGameMode::ParticipantActor(int32 Id)const
{if(Id==0)return Player;for(auto Bot:Bots)if(Bot&&Bot->ParticipantId==Id)return Bot;return nullptr;}
int32 AHeliosBioHotelGameMode::ParticipantId(const AActor* A)const
{
    for(int32 Depth=0;A&&Depth<4;++Depth,A=A->GetOwner())
    {if(A==Player)return 0;if(const auto* Bot=Cast<AHeliosBioNPC>(A))return Bot->ParticipantId;}
    return INDEX_NONE;
}
bool AHeliosBioHotelGameMode::AreOpponents(int32 A,int32 B)const
{const auto* P=Model.FindParticipant(A);const auto* Q=Model.FindParticipant(B);return P&&Q&&P->IsHumanSide()!=Q->IsHumanSide();}
bool AHeliosBioHotelGameMode::DamageParticipant(int32 V,int32 A,float Damage,bool HunterMelee)
{
    const auto* Before=Model.FindParticipant(V);const auto* Attacker=Model.FindParticipant(A);
    if(!Before||!Attacker||!AreOpponents(V,A))return false;
    const auto TeamBefore=Before->Team;
    // Six-fold bio damage is offline tuning. Native weapon shots and authored effects remain intact.
    if(Attacker->Team==EHeliosBioTeam::Human)Damage*=Model.Config.HumanDamageMultiplier;
    if(!Model.ApplyDamage(V,A,Damage,HunterMelee))return false;
    ++TotalDamageEvents;
    const auto* After=Model.FindParticipant(V);
    if(After&&TeamBefore!=EHeliosBioTeam::Zombie&&After->Team==EHeliosBioTeam::Zombie)
    {if(TeamBefore==EHeliosBioTeam::Human)if(auto* Bot=Cast<AHeliosBioNPC>(ParticipantActor(V)))Bot->BeginHumanInfectionTransition();++TotalInfections;ShowNotice(FString::Printf(TEXT("%s 被感染"),V==0?TEXT("你"):*FString::Printf(TEXT("队员 %02d"),V)));BioSound(this,TEXT("SFX_ZombieAppear"));}
    return true;
}
bool AHeliosBioHotelGameMode::TransformParticipant(int32 Id)
{if(!Model.Transform(Id))return false;ShowNotice(Id==0?TEXT("已变身幽灵猎手；拾取蓝色箱子进化"):TEXT("最后的幸存者变身幽灵猎手"));return true;}
bool AHeliosBioHotelGameMode::PickupSupply(int32 Id,AHeliosBioSupply* S)
{
    if(!IsValid(S)||!S->CanReachPickup(ParticipantActor(Id))||GetWorld()->IsPaused()||bHunterChooserOpen)return false;
    const auto* Before=Model.FindParticipant(Id);if(!Before||Before->FrozenSeconds>0)return false;
    // First validate the exact production rule on a copy. A failed resource or
    // equip transaction must not mutate health, evolution, scores or the box.
    auto NextRules=Model;if(!NextRules.PickupSupply(Id,S->GetKind()))return false;
    auto* Content=S->GetWeaponContent();
    if(bFloatingProfile&&S->GetKind()==EHeliosBioSupplyKind::Normal&&Before->Team==EHeliosBioTeam::Human&&Content)
    {
        auto* Actor=ParticipantActor(Id);
        auto* Inventory=Actor?Actor->FindComponentByClass<UHeliosBioRoundInventoryComponent>():nullptr;
        if(!Inventory||!Inventory->TryGrant(Content))return false;
    }
    Model=MoveTemp(NextRules);
    S->SetAvailable(false);++TotalSupplies;
    if(S->GetKind()==EHeliosBioSupplyKind::Normal&&Model.FindParticipant(Id)->IsHumanSide())
    {if(Id==0&&Player)Player->AddSupplyAmmo();else if(auto* Bot=Cast<AHeliosBioNPC>(ParticipantActor(Id)))Bot->AddSupplyAmmo();}
    BioSound(this,TEXT("SFX_AmmoSupply"));
    if(const auto* P=Model.FindParticipant(Id))if(P->bHunterChoicePending)
    {if(Id==0)OpenHunterChooser();else if(!Model.Config.AllowedHunterRoles.IsEmpty())SelectHunter(Id,Model.Config.AllowedHunterRoles[Id%Model.Config.AllowedHunterRoles.Num()]);}
    if(Id==0)ShowNotice(Content&&Model.FindParticipant(Id)->Team==EHeliosBioTeam::Human?FString::Printf(TEXT("已获得 %s · 数字键/滚轮切换 · 原武器已保留"),*Content->DisplayName.ToString()):S->GetKind()==EHeliosBioSupplyKind::RedTerminatorEvolution?TEXT("红箱进化成功：深渊终结者"):S->GetKind()==EHeliosBioSupplyKind::BlueHunterUpgrade?TEXT("已获得高级猎手选择机会"):Model.FindParticipant(Id)->IsHumanSide()?TEXT("已拾取普通补给：备弹恢复"):TEXT("已拾取生化补给：进化进度增加"));return true;
}
int32 AHeliosBioHotelGameMode::DrawSupplyWeaponSlot()
{
    if(!bFloatingProfile)return INDEX_NONE;
    if(SupplyWeaponBagCursor>=SupplyWeaponBag.Num())
    {
        SupplyWeaponBag.Reset();SupplyWeaponBagCursor=0;
        for(int32 Slot=1;Slot<5;++Slot)if(auto* Definition=UHeliosBioSupplyWeaponDefinition::LoadForSlot(Slot);Definition&&Definition->IsReady())SupplyWeaponBag.Add(Slot);
        FRandomStream ContentRandom(CurrentRoundSeed+7919+SupplyWeaponBagCycle++*104729);
        for(int32 I=SupplyWeaponBag.Num()-1;I>0;--I)SupplyWeaponBag.Swap(I,ContentRandom.RandRange(0,I));
    }
    return SupplyWeaponBag.IsValidIndex(SupplyWeaponBagCursor)?SupplyWeaponBag[SupplyWeaponBagCursor++]:INDEX_NONE;
}
void AHeliosBioHotelGameMode::RefreshSupplyWave()
{
    for(auto S:Supplies)if(S)S->Destroy();Supplies.Reset();
    if(SupplyPoints.IsEmpty())return;
    FRandomStream R(CurrentRoundSeed+RoundSerial*67+Model.GetSupplyWave()*97);TArray<int32> Chosen;
    const int32 Count=FMath::Min(Model.Config.NormalSupplyCount,SupplyPoints.Num());
    for(int32 I=0;I<Count;++I)
    {
        int32 Index;do{Index=R.RandRange(0,SupplyPoints.Num()-1);}while(Chosen.Contains(Index));Chosen.Add(Index);
        const FTransform T(FRotator::ZeroRotator,SupplyPoints[Index]);
        auto* S=GetWorld()->SpawnActorDeferred<AHeliosBioSupply>(AHeliosBioSupply::StaticClass(),T,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(S){S->SetKind(EHeliosBioSupplyKind::Normal);const int32 Slot=DrawSupplyWeaponSlot();if(Slot!=INDEX_NONE)S->SetWeaponContent(UHeliosBioSupplyWeaponDefinition::LoadForSlot(Slot));UGameplayStatics::FinishSpawningActor(S,T);Supplies.Add(S);}
    }
    // Offline placement uses verified original markers; source Camp color mapping and cadence remain unverified.
    const int32 BlueCount=FMath::Min(Model.Config.BlueSupplyCount,SupplyPoints.Num()-Chosen.Num());
    for(int32 I=0;I<BlueCount;++I)
    {
        int32 Index;do{Index=R.RandRange(0,SupplyPoints.Num()-1);}while(Chosen.Contains(Index));Chosen.Add(Index);
        const FTransform T(FRotator::ZeroRotator,SupplyPoints[Index]);
        auto* S=GetWorld()->SpawnActorDeferred<AHeliosBioSupply>(AHeliosBioSupply::StaticClass(),T,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(S){S->SetKind(EHeliosBioSupplyKind::BlueHunterUpgrade);UGameplayStatics::FinishSpawningActor(S,T);Supplies.Add(S);}
    }
    const int32 RedCount=FMath::Min(Model.Config.RedSupplyCount,SupplyPoints.Num()-Chosen.Num());
    for(int32 I=0;I<RedCount;++I)
    {
        int32 Index;do{Index=R.RandRange(0,SupplyPoints.Num()-1);}while(Chosen.Contains(Index));Chosen.Add(Index);
        const FTransform T(FRotator::ZeroRotator,SupplyPoints[Index]);
        auto* S=GetWorld()->SpawnActorDeferred<AHeliosBioSupply>(AHeliosBioSupply::StaticClass(),T,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(S){S->SetKind(EHeliosBioSupplyKind::RedTerminatorEvolution);UGameplayStatics::FinishSpawningActor(S,T);Supplies.Add(S);}
    }
    ShowNotice(RedCount>0?TEXT("普通补给、蓝箱和红箱已刷新"):TEXT("普通补给和蓝色进化箱已刷新"));BioSound(this,TEXT("SFX_AmmoSupply"));
}
void AHeliosBioHotelGameMode::ShowNotice(const FString& T,float S){Notice=T;NoticeSeconds=S;}
void AHeliosBioHotelGameMode::Tick(float Dt)
{
    Super::Tick(Dt);if(GetWorld()->IsPaused())return;
    Clones.RemoveAll([](const TObjectPtr<AHeliosBioClone>& C){return !IsValid(C);});
    Turrets.RemoveAll([](const TObjectPtr<AHeliosBioTurret>& T){return !IsValid(T);});
    Model.Tick(Dt);if(bHunterChooserOpen){const auto* Local=Model.FindParticipant(0);if(!Local||!Local->bAlive||!Local->bHunterChoicePending)CloseHunterChooser();}NoticeSeconds=FMath::Max(0.f,NoticeSeconds-Dt);
    if(Model.GetPhase()==EHeliosBioPhase::Preparation)
    {const int32 Seconds=FMath::CeilToInt(Model.GetPhaseTimeRemaining());if(Seconds!=LastCountdown){LastCountdown=Seconds;if(Seconds<=5)BioSound(this,TEXT("SFX_Countdown"));}}
    if(Model.GetPhase()==EHeliosBioPhase::Active)
    {
        if(PreviousPhase!=EHeliosBioPhase::Active)ShowNotice(TEXT("生化幽灵已出现：击退幽灵，等待猎手变身"),5);
        if(LastSupplyWave!=Model.GetSupplyWave()){LastSupplyWave=Model.GetSupplyWave();RefreshSupplyWave();}
    }
    if(Model.GetPhase()==EHeliosBioPhase::RoundOver)
    {
        if(PreviousPhase!=EHeliosBioPhase::RoundOver)
        {if(Model.GetWinner()==EHeliosBioWinner::Humans)++HumanWins;else ++ZombieWins;ShowNotice(Model.GetWinner()==EHeliosBioWinner::Humans?TEXT("人类胜利 · F5 再来一局"):TEXT("生化幽灵胜利 · F5 再来一局"),15);if(Player)Player->StopBioFire();if(auto* PC=UGameplayStatics::GetPlayerController(this,0)){PC->bShowMouseCursor=true;PC->SetInputMode(FInputModeGameAndUI());}}
        RoundOverAge+=Dt;
        for(auto Bot:Bots)if(Bot&&Bot->GetEquippedWeapon())Bot->GetEquippedWeapon()->StopFiring();
    }
    PreviousPhase=Model.GetPhase();
}
void AHeliosBioHotelGameMode::PauseBio()
{
    const bool Paused=!GetWorld()->IsPaused();UGameplayStatics::SetGamePaused(this,Paused);
    if(auto* PC=UGameplayStatics::GetPlayerController(this,0)){PC->bShowMouseCursor=Paused;if(Paused)PC->SetInputMode(FInputModeGameAndUI());else PC->SetInputMode(FInputModeGameOnly());}
}
void AHeliosBioHotelGameMode::ReturnToMapMenu()
{UGameplayStatics::SetGamePaused(this,false);UGameplayStatics::OpenLevel(this,TEXT("/Game/Helios/Maps/HeliosAdventure"));}

bool AHeliosBioHotelGameMode::UseParticipantSkill(int32 Id)
{
    const auto* P=Model.FindParticipant(Id);AActor* Source=ParticipantActor(Id);
    auto* Art=P?HeliosBioArt::Load(*P):nullptr;
    if(!P||!Source||!P->bAlive||P->FrozenSeconds>0||P->SkillCooldownRemaining>0||Model.GetPhase()!=EHeliosBioPhase::Active||!IsParticipantSkillImplemented(Id))return false;
    const EHeliosBioAbility Ability=AbilityFor(Art,*P);
    FTransform Placement;
    if(Ability==EHeliosBioAbility::MechanicalFire&&!TurretPlacement(Cast<ACharacter>(Source),Art,Placement))
    {if(Id==0)ShowNotice(TEXT("此处无法放置炮台，请选择平整空地"));return false;}
    if(Ability==EHeliosBioAbility::FourSlash)
    {
        const bool Began=Id==0&&Player?Player->BeginBioFourSlash():Cast<AHeliosBioNPC>(Source)?Cast<AHeliosBioNPC>(Source)->BeginBioFourSlash():false;
        if(!Began)return false;
    }
    const bool Authored=Art->Ability!=EHeliosBioAbility::None;
    const float Duration=Authored?Art->SkillDuration:Model.Config.SkillDurationSeconds;
    const float Cooldown=Authored?Art->SkillCooldown:Model.Config.SkillCooldownSeconds;
    const float Speed=Authored?Art->SkillSpeedMultiplier:Ability==EHeliosBioAbility::Sprint?1.3f:Ability==EHeliosBioAbility::Shield?1.3f:1.f;
    const float Damage=Authored?Art->SkillDamageMultiplier:Ability==EHeliosBioAbility::Sprint&&P->IsHunter()?Model.Config.HunterSkillDamageMultiplier:1.f;
    const float Reduction=Authored?Art->SkillShieldReduction:Ability==EHeliosBioAbility::Shield?.9f:0.f;
    if(!Model.ActivateSkill(Id,Duration,Cooldown,Speed,Damage,Reduction))return false;
    if(!Art)return true;
    if(auto* Sound=Art->SkillSound.LoadSynchronous())UGameplayStatics::PlaySoundAtLocation(this,Sound,Source->GetActorLocation(),.8f);
    const TCHAR* Library=TEXT("/Game/Helios/CFMBioUltimateEffects/Effects/DA_ULTFX_Effects.DA_ULTFX_Effects");
    const auto* SourceCharacter=Cast<ACharacter>(Source);
    const FVector Feet=Source->GetActorLocation()-FVector(0,0,SourceCharacter?SourceCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight():Model.Config.MovementCapsuleHalfHeight);
    if(Ability==EHeliosBioAbility::AreaStrike)
    {
        AHeliosSSXEffect::Spawn(Source,TEXT("FX_3P_SunWuKong_Skill_Ground"),Feet,Source->GetActorRotation(),false,1.f,Library);
        const float Delay=Art->FPSkill.IsNull()?.2f:Art->FPSkill.LoadSynchronous()?Art->FPSkill.LoadSynchronous()->GetPlayLength():.2f;
        const int32 ExpectedRound=RoundSerial;FTimerHandle Handle;
        GetWorldTimerManager().SetTimer(Handle,FTimerDelegate::CreateWeakLambda(this,[this,Id,ExpectedRound]{PerformUltimateStrike(Id,ExpectedRound);}),FMath::Max(.01f,Delay),false);
    }
    else if(Ability==EHeliosBioAbility::CloneSummon)
    {
        AHeliosSSXEffect::Spawn(Source,TEXT("FX_3P_BaiGuJing_Buff"),Feet,Source->GetActorRotation(),false,1.f,Library);
        // Exact source help: two puppets, 30% mother's HP and 50% attack. Lifetime is standalone tuning.
        for(int32 Side:{-1,1})
        {
            FVector At=Source->GetActorLocation()+Source->GetActorRightVector()*Side*160.f+Source->GetActorForwardVector()*80.f;
            FHitResult Floor;FCollisionObjectQueryParams Static(ECC_WorldStatic);FCollisionQueryParams Query(SCENE_QUERY_STAT(BioCloneFloor),false,Source);
            if(GetWorld()->LineTraceSingleByObjectType(Floor,At+FVector(0,0,250),At-FVector(0,0,800),Static,Query))At.Z=Floor.ImpactPoint.Z+98;
            const FTransform Transform(Source->GetActorRotation(),At);
            auto* Clone=GetWorld()->SpawnActorDeferred<AHeliosBioClone>(AHeliosBioClone::StaticClass(),Transform,Source,Cast<APawn>(Source),ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if(Clone){Clone->ConfigureClone(Id,P->MaxHealth*.3f,.5f,CloneLifetime);UGameplayStatics::FinishSpawningActor(Clone,Transform);if(IsValid(Clone)&&Clone->IsActiveClone()){Clones.Add(Clone);++TotalCloneSpawns;}}
        }
    }
    else if(Ability==EHeliosBioAbility::MechanicalFire)
    {
        const int32 ExpectedRound=RoundSerial;FTimerHandle Handle;
        GetWorldTimerManager().SetTimer(Handle,FTimerDelegate::CreateWeakLambda(this,[this,Id,ExpectedRound,Art]{PerformRoleSkill(Id,ExpectedRound,Art);}),.7f,false);
    }
    else PerformRoleSkill(Id,RoundSerial,Art);
    return true;
}
bool AHeliosBioHotelGameMode::IsParticipantSkillImplemented(int32 Id)const
{
    const auto* P=Model.FindParticipant(Id);auto* Art=P?HeliosBioArt::Load(*P):nullptr;
    if(!P||!Art||!Art->bIdentityVerified||!AbilityImplemented(AbilityFor(Art,*P)))return false;
    const auto Ability=AbilityFor(Art,*P);
    if(Ability==EHeliosBioAbility::MechanicalFire)return RoleArtReady(Art,Ability);
    if(Ability==EHeliosBioAbility::FreezeGrenade||Ability==EHeliosBioAbility::AbyssEnergy)return Art->SkillProjectileMesh.LoadSynchronous()!=nullptr;
    if(Ability==EHeliosBioAbility::ZhongXiaoKuiBlade)return !Art->SkillEffectLibrary.IsEmpty()&&!Art->SwordWaveEffect.IsEmpty();
    return true;
}
FString AHeliosBioHotelGameMode::ParticipantSkillDescription(int32 Id)const
{
    const auto* P=Model.FindParticipant(Id);auto* Art=P?HeliosBioArt::Load(*P):nullptr;
    if(!P||!Art)return TEXT("角色技能暂不可用");
    if(!Art->SkillDescription.IsEmpty())return Art->SkillDescription;
    switch(AbilityFor(Art,*P))
    {
    case EHeliosBioAbility::FourSlash:return TEXT("四连斩");
    case EHeliosBioAbility::AreaStrike:return TEXT("如意一击");
    case EHeliosBioAbility::CloneSummon:return TEXT("召唤傀儡");
    case EHeliosBioAbility::Shield:return TEXT("护盾与加速");
    case EHeliosBioAbility::Blink:return TEXT("闪现");
    case EHeliosBioAbility::FreezeGrenade:return TEXT("冰冻手雷");
    case EHeliosBioAbility::SacredShield:return TEXT("护盾");
    case EHeliosBioAbility::AbyssEnergy:return TEXT("深渊能量球");
    default:return TEXT("加速");
    }
}
void AHeliosBioHotelGameMode::PerformRoleSkill(int32 Id,int32 ExpectedRound,UHeliosBioRoleDefinition* Art)
{
    const auto* P=Model.FindParticipant(Id);auto* Source=Cast<ACharacter>(ParticipantActor(Id));
    if(!P||!Source||!Art||RoundSerial!=ExpectedRound||!P->bAlive||Model.GetPhase()!=EHeliosBioPhase::Active)return;
    const auto Ability=AbilityFor(Art,*P);
    if(Ability==EHeliosBioAbility::Blink)
    {
        const FVector From=Source->GetActorLocation();const FVector Forward=Source->GetBaseAimRotation().Vector().GetSafeNormal2D();
        const float Radius=Source->GetCapsuleComponent()->GetScaledCapsuleRadius(),Half=Source->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        FCollisionQueryParams Query(SCENE_QUERY_STAT(BioBlink),false,Source);TArray<AActor*> Attached;Source->GetAttachedActors(Attached,true,true);for(auto A:Attached)Query.AddIgnoredActor(A);
        FHitResult Wall;const FVector Desired=From+Forward*FMath::Max(0.f,Art->SkillRange);
        const bool Blocked=GetWorld()->SweepSingleByChannel(Wall,From,Desired,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Radius,Half),Query);
        FVector Destination=Blocked?From+Forward*FMath::Max(0.f,Wall.Distance-5.f):Desired;
        FHitResult Floor;FCollisionObjectQueryParams Static(ECC_WorldStatic);
        if(GetWorld()->LineTraceSingleByObjectType(Floor,Destination+FVector(0,0,Half),Destination-FVector(0,0,Half*2),Static,Query)&&Floor.ImpactNormal.Z>.65f)
        {Destination.Z=Floor.ImpactPoint.Z+Half+2;Source->SetActorLocation(Destination,true);Source->GetCharacterMovement()->StopMovementImmediately();}
    }
    else if(Ability==EHeliosBioAbility::FreezeGrenade||Ability==EHeliosBioAbility::AbyssEnergy)
    {
        FVector Eyes;FRotator Aim;Source->GetActorEyesViewPoint(Eyes,Aim);
        FActorSpawnParameters Params;Params.Owner=Source;Params.Instigator=Source;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        if(auto* Projectile=GetWorld()->SpawnActor<AHeliosBioSkillProjectile>(Eyes+Aim.Vector()*60,Aim,Params))Projectile->Configure(Id,ExpectedRound,Art,Aim.Vector());
    }
    else if(Ability==EHeliosBioAbility::MechanicalFire)
    {
        FTransform Placement;
        if(TurretPlacement(Source,Art,Placement))
        {
            FActorSpawnParameters Params;Params.Owner=Source;Params.Instigator=Source;
            if(auto* Turret=GetWorld()->SpawnActor<AHeliosBioTurret>(Placement.GetLocation(),Placement.Rotator(),Params))
            {Turret->Configure(Id,ExpectedRound,Art);if(Turret->IsActiveTurret())Turrets.Add(Turret);}
        }
    }
    else if(Ability==EHeliosBioAbility::ZhongXiaoKuiBlade)
    {
        const FVector Origin=Source->GetActorLocation();const FVector Forward=Source->GetBaseAimRotation().Vector().GetSafeNormal2D();
        for(const auto& Other:Model.GetParticipants())if(Other.bAlive&&AreOpponents(Id,Other.Id))if(auto* Victim=Cast<ACharacter>(ParticipantActor(Other.Id)))
        {
            const FVector Delta=Victim->GetActorLocation()-Origin;
            if(Delta.Size()>Art->SkillRange||FVector::DotProduct(Delta.GetSafeNormal2D(),Forward)<.55f)continue;
            FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(BioPunishLOS),false,Source);
            if(GetWorld()->LineTraceSingleByChannel(Hit,Origin+FVector(0,0,40),Victim->GetActorLocation()+FVector(0,0,25),ECC_Visibility,Q)&&Hit.GetActor()!=Victim)continue;
            if(Model.FreezeParticipant(Other.Id,Id,Art->SkillDuration))Victim->LaunchCharacter(Delta.GetSafeNormal2D()*UltimateSkillKnockback+FVector(0,0,180),true,true);
        }
    }
}
bool AHeliosBioHotelGameMode::UseParticipantSecondarySkill(int32 Id)
{
    const auto* P=Model.FindParticipant(Id);auto* Art=P?HeliosBioArt::Load(*P):nullptr;auto* Source=Cast<ACharacter>(ParticipantActor(Id));
    if(!P||!Art||!Art->bIdentityVerified||!Source||!P->bAlive||P->FrozenSeconds>0||!P->IsHunter())return false;
    if(P->Team==EHeliosBioTeam::UltimateHunter&&P->HunterRoleId==TEXT("BioRedeemerHunter"))
    {
        FVector ActualMuzzle;FVector Eyes;FRotator Aim;Source->GetActorEyesViewPoint(Eyes,Aim);
        return GetParticipantSerumMuzzle(Id,ActualMuzzle)&&FireParticipantSerum(Id,ActualMuzzle,Aim.Vector());
    }
    const auto Ability=AbilityFor(Art,*P);
    if(Ability==EHeliosBioAbility::ZhongXiaoKuiBlade)
        return Model.ActivateSecondarySkill(Id,Art->SecondaryDuration,Art->SecondaryCooldown,Art->SecondarySpeedMultiplier,Art->SecondaryDamageMultiplier,true);
    if(Ability!=EHeliosBioAbility::MechanicalFire||(!Art->SecondaryProjectileMesh.LoadSynchronous()&&Art->SecondaryProjectileEffect.IsEmpty()))return false;
    FVector Eyes;FRotator Aim;Source->GetActorEyesViewPoint(Eyes,Aim);FActorSpawnParameters Params;Params.Owner=Source;Params.Instigator=Source;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Projectile=GetWorld()->SpawnActor<AHeliosBioSkillProjectile>(Eyes+Aim.Vector()*60,Aim,Params);if(!Projectile)return false;
    if(!Model.ActivateSecondarySkill(Id,0,Art->SecondaryCooldown)){Projectile->Destroy();return false;}
    Projectile->Configure(Id,RoundSerial,Art,Aim.Vector(),true);return true;
}
bool AHeliosBioHotelGameMode::GetParticipantSerumMuzzle(int32 Id,FVector& OutWorldPosition) const
{
    const auto* P=Model.FindParticipant(Id);auto* Art=P?HeliosBioArt::Load(*P):nullptr;auto* Source=Cast<ACharacter>(ParticipantActor(Id));
    if(!P||!Art||!Source||!Art->bIdentityVerified||!Art->bSerumSourceVerified||P->Team!=EHeliosBioTeam::UltimateHunter||P->HunterRoleId!=TEXT("BioRedeemerHunter"))return false;
    if(Id==0)
    {
        auto* Expected=Art->SecondaryFirstPersonMesh.LoadSynchronous();if(!Expected)return false;
        TInlineComponentArray<USkeletalMeshComponent*> Meshes(Source);
        for(auto* Mesh:Meshes)if(Mesh&&Mesh->IsVisible()&&Mesh->GetSkeletalMeshAsset()==Expected&&Mesh->DoesSocketExist(Art->SecondaryFirstPersonMuzzleSocket))
        {OutWorldPosition=Mesh->GetSocketLocation(Art->SecondaryFirstPersonMuzzleSocket);return !OutWorldPosition.ContainsNaN();}
    }
    else
    {
        auto* Expected=Art->SecondaryThirdPersonWeaponMesh.LoadSynchronous();if(!Expected)return false;
        TInlineComponentArray<UStaticMeshComponent*> Meshes(Source);
        for(auto* Mesh:Meshes)if(Mesh&&Mesh->IsVisible()&&Mesh->GetStaticMesh()==Expected)
        {OutWorldPosition=Mesh->GetComponentTransform().TransformPosition(Art->SecondaryThirdPersonMuzzleLocal);return !OutWorldPosition.ContainsNaN();}
    }
    return false;
}
bool AHeliosBioHotelGameMode::FireParticipantSerum(int32 Id,const FVector& ActualMuzzle,const FVector& Direction)
{
    if(GetWorld()->IsPaused())return false;
    const auto* P=Model.FindParticipant(Id);auto* Art=P?HeliosBioArt::Load(*P):nullptr;auto* Source=Cast<ACharacter>(ParticipantActor(Id));
    if(!P||!Art||!Source||!P->bAlive||P->bPermanentlyDead||P->FrozenSeconds>0||P->Team!=EHeliosBioTeam::UltimateHunter||P->HunterRoleId!=TEXT("BioRedeemerHunter")||Model.GetPhase()!=EHeliosBioPhase::Active||P->SecondaryCooldownRemaining>0||(Id==0&&bHunterChooserOpen))return false;
    auto* FP=Art->SecondaryFirstPersonMesh.LoadSynchronous();auto* TP=Art->ThirdPersonMesh.LoadSynchronous();
    const auto ValidClip=[](const TSoftObjectPtr<UAnimSequence>& Reference,USkeletalMesh* Mesh){auto* Clip=Reference.LoadSynchronous();return Clip&&Mesh&&Clip->GetSkeleton()==Mesh->GetSkeleton()&&FMath::IsFinite(Clip->GetPlayLength())&&Clip->GetPlayLength()>0;};
    FVector ProvenMuzzle;
    if(!Art->bIdentityVerified||!Art->bSerumSourceVerified||!FP||!TP||!Art->SecondaryThirdPersonWeaponMesh.LoadSynchronous()||!ValidClip(Art->FPSecondaryIdle,FP)||!ValidClip(Art->FPSecondaryEquip,FP)||!ValidClip(Art->FPSecondarySkill,FP)||!ValidClip(Art->TPSecondaryIdle,TP)||!ValidClip(Art->TPSecondarySkill,TP)||!Art->SecondaryFireSound.LoadSynchronous()||!GetParticipantSerumMuzzle(Id,ProvenMuzzle)||!ActualMuzzle.Equals(ProvenMuzzle,1.f)||Direction.ContainsNaN()||Direction.IsNearlyZero()||FVector::DistSquared(ActualMuzzle,Source->GetActorLocation())>FMath::Square(300.f))return false;
    if(!FMath::IsFinite(Art->SecondaryRange)||Art->SecondaryRange<=0||!FMath::IsFinite(Art->SerumProjectileSpeed)||Art->SerumProjectileSpeed<=0||!FMath::IsFinite(Art->SerumProjectileRadius)||Art->SerumProjectileRadius<=0)return false;
    // The same sphere used for flight checks the real eye-to-gun segment. A gun
    // protruding through geometry cannot start its dart beyond that obstruction.
    FVector Eyes;FRotator Aim;Source->GetActorEyesViewPoint(Eyes,Aim);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BioSerumMuzzleClearance),true,Source);TArray<AActor*> Attached;Source->GetAttachedActors(Attached,true,true);Query.AddIgnoredActors(Attached);
    FHitResult InitialHit;const bool InitialBlocked=GetWorld()->SweepSingleByChannel(InitialHit,Eyes,ActualMuzzle,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(Art->SerumProjectileRadius),Query);
    const FVector LaunchAt=InitialBlocked?InitialHit.Location:ActualMuzzle;
    const FTransform Launch(Direction.Rotation(),LaunchAt);
    auto* Projectile=GetWorld()->SpawnActorDeferred<AHeliosBioSerumProjectile>(AHeliosBioSerumProjectile::StaticClass(),Launch,Source,Source,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);if(!Projectile)return false;
    if(!Model.ActivateSecondarySkill(Id,0.f,Art->SecondaryCooldown)){Projectile->Destroy();return false;}
    UGameplayStatics::FinishSpawningActor(Projectile,Launch);Projectile->Configure(Id,RoundSerial,Art,Direction);
    ++TotalSerumShots;LastSerumDiagnostic=FString::Printf(TEXT("shot caster=%d round=%d actual_muzzle=%s launch=%s eye=%s direction=%s muzzle_blocked=%d cooldown=%.3f range=%.1f"),Id,RoundSerial,*ActualMuzzle.ToString(),*LaunchAt.ToString(),*Eyes.ToString(),*Direction.GetSafeNormal().ToString(),InitialBlocked,Art->SecondaryCooldown,Art->SecondaryRange);
    UE_LOG(LogTemp,Display,TEXT("BIO_SERUM_SHOT %s"),*LastSerumDiagnostic);
    UGameplayStatics::PlaySoundAtLocation(this,Art->SecondaryFireSound.LoadSynchronous(),ActualMuzzle);
    if(InitialBlocked)Projectile->ResolveInitialObstruction(InitialHit);
    return true;
}
bool AHeliosBioHotelGameMode::FireParticipantRoleWeapon(int32 Id,const FVector& Start,const FVector& Direction)
{
    const auto* P=Model.FindParticipant(Id);auto* Art=P?HeliosBioArt::Load(*P):nullptr;auto* Source=Cast<ACharacter>(ParticipantActor(Id));
    if(!P||!Art||!Source||!P->bAlive||Art->Ability!=EHeliosBioAbility::MechanicalFire||!RoleArtReady(Art,Art->Ability))return false;
    const double Now=GetWorld()->GetTimeSeconds();if(Now<NextRoleWeaponShot.FindRef(Id))return false;
    if(!Model.ConsumeRoleWeaponEnergy(Id,Art->PrimaryEnergyCost,Art->PrimaryRechargeDelay,Art->PrimaryEnergyRecharge,Art->PrimaryEnergy))return false;
    NextRoleWeaponShot.Add(Id,Now+FMath::Max(.03f,Art->PrimaryFireInterval));
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BioNativeXM214),true,Source);
    TArray<AActor*> Attached;Source->GetAttachedActors(Attached,true,true);Query.AddIgnoredActors(Attached);
    FHitResult Hit;if(GetWorld()->LineTraceSingleByChannel(Hit,Start,Start+Direction.GetSafeNormal()*Art->PrimaryRange,ECC_Visibility,Query))
    {
        if(auto* Clone=Cast<AHeliosBioClone>(Hit.GetActor()))Clone->ApplyBioDamage(Id,Art->PrimaryDamage);
        else if(auto* Turret=Cast<AHeliosBioTurret>(Hit.GetActor()))Turret->ApplyBioDamage(Id,Art->PrimaryDamage);
        else {const int32 Victim=ParticipantId(Hit.GetActor());if(Victim!=INDEX_NONE)DamageParticipant(Victim,Id,Art->PrimaryDamage,false);}
    }
    return true;
}
bool AHeliosBioHotelGameMode::FireParticipantSwordWave(int32 Id,const FVector& Start,const FVector& Direction,bool Heavy)
{
    const auto* P=Model.FindParticipant(Id);auto* Art=P?HeliosBioArt::Load(*P):nullptr;auto* Source=Cast<ACharacter>(ParticipantActor(Id));
    if(!P||!Art||!Source||Art->Ability!=EHeliosBioAbility::ZhongXiaoKuiBlade||Art->SwordWaveEffect.IsEmpty()||!P->bGhostMaskActive||(!P->bFirstAttackSwordWave&&(!Heavy||!P->bHeavySwordWaveReady)))return false;
    FActorSpawnParameters Params;Params.Owner=Source;Params.Instigator=Source;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    if(auto* Wave=GetWorld()->SpawnActor<AHeliosBioSkillProjectile>(Start+Direction.GetSafeNormal()*60,Direction.Rotation(),Params)){if(!Model.ConsumeSwordWave(Id,Heavy)){Wave->Destroy();return false;}Wave->Configure(Id,RoundSerial,Art,Direction,false,true);return true;}return false;
}
void AHeliosBioHotelGameMode::PerformUltimateStrike(int32 Id,int32 ExpectedRound)
{
    const auto* P=Model.FindParticipant(Id);AActor* Source=ParticipantActor(Id);
    if(RoundSerial!=ExpectedRound||!P||!P->bAlive||P->Team!=EHeliosBioTeam::UltimateHunter||Model.GetPhase()!=EHeliosBioPhase::Active||!Source)return;
    auto* SkillArt=HeliosBioArt::Load(*P);
    const float Radius=SkillArt&&SkillArt->Ability!=EHeliosBioAbility::None?SkillArt->SkillRange:UltimateSkillRadius;
    const float Damage=SkillArt&&SkillArt->Ability!=EHeliosBioAbility::None?SkillArt->SkillDamage:UltimateSkillDamage;
    const FVector Origin=Source->GetActorLocation();
    auto Hit=[&](AActor* Victim,int32 VictimId,AHeliosBioClone* Clone)
    {
        if(!Victim||FVector::DistSquared(Origin,Victim->GetActorLocation())>FMath::Square(Radius))return;
        FHitResult Obstruction;FCollisionQueryParams Query(SCENE_QUERY_STAT(BioUltimateLOS),false,Source);
        if(GetWorld()->LineTraceSingleByChannel(Obstruction,Origin+FVector(0,0,30),Victim->GetActorLocation()+FVector(0,0,30),ECC_Visibility,Query)&&Obstruction.GetActor()!=Victim)return;
        const bool Applied=Clone?Clone->ApplyBioDamage(Id,Damage)>0:DamageParticipant(VictimId,Id,Damage,true);
        if(!Applied)return;++TotalAreaSkillHits;
        if(auto* Character=Cast<ACharacter>(Victim))Character->LaunchCharacter((Victim->GetActorLocation()-Origin).GetSafeNormal2D()*UltimateSkillKnockback+FVector(0,0,180),true,true);
    };
    for(const auto& Other:Model.GetParticipants())if(Other.bAlive&&AreOpponents(Id,Other.Id))Hit(ParticipantActor(Other.Id),Other.Id,nullptr);
    for(auto Clone:Clones)if(IsValid(Clone)&&Clone->IsActiveClone()&&AreOpponents(Id,Clone->GetMotherId()))Hit(Clone,INDEX_NONE,Clone);
    UE_LOG(LogTemp,Display,TEXT("BIOPYRAMID_WUKONG_STRIKE actor=%d cumulative_hits=%d radius_standalone=%f"),Id,TotalAreaSkillHits,Radius);
}

TArray<FHeliosBioHunterOption> AHeliosBioHotelGameMode::HunterOptions() const
{
    TArray<FHeliosBioHunterOption> Result;
    struct Row{const TCHAR* Id;const TCHAR* Name;const TCHAR* Hint;};
    const Row Rows[]={{TEXT("BioUltimateHunter"),TEXT("终极猎手（经典）"),TEXT("双持绯刃 · 四连斩")},
        {TEXT("BioSunShangXiang"),TEXT("孙尚香"),TEXT("双刀 · 四连斩")},
        {TEXT("BioSunWukong"),TEXT("悟空猎手"),TEXT("金箍棒 · 如意一击")},
        {TEXT("BioTimeSpaceHunter"),TEXT("时空猎手"),TEXT("双刃 · 闪现")},
        {TEXT("BioRedeemerHunter"),TEXT("救赎猎手"),TEXT("救赎技能")},
        {TEXT("BioMechanicalHunter"),TEXT("机械猎手"),TEXT("重机枪 · 机械装备")},
        {TEXT("BioSacredFistHunter"),TEXT("圣拳猎手"),TEXT("拳套 · 护盾")},
        {TEXT("BioZhongXiaoKuiHunter"),TEXT("钟小魁"),TEXT("斩鬼刀 · 罚恶 · 傩刃解")}};
    for(const auto& R:Rows)
    {
        auto* Art=HeliosBioArt::Named(FName(R.Id));
        const EHeliosBioAbility Ability=Art&&Art->Ability!=EHeliosBioAbility::None?Art->Ability:FString(R.Id)==TEXT("BioSunShangXiang")?EHeliosBioAbility::FourSlash:FString(R.Id)==TEXT("BioSunWukong")?EHeliosBioAbility::AreaStrike:EHeliosBioAbility::None;
        const bool Ready=RoleArtReady(Art,Ability);
        const bool SelectedByProfile=ProfileHunterRoles.IsEmpty()||ProfileHunterRoles.Contains(FName(R.Id));
        const bool LegacyVerified=FString(R.Id)==TEXT("BioSunShangXiang")||FString(R.Id)==TEXT("BioSunWukong");
        const bool Gameplay=Ready&&(LegacyVerified||Art->bGameplayVerified)&&AbilityImplemented(Art->Ability);
        const bool LegacyAbility=Ready&&LegacyVerified&&AbilityImplemented(FString(R.Id)==TEXT("BioSunShangXiang")?EHeliosBioAbility::FourSlash:EHeliosBioAbility::AreaStrike);
        Result.Add({FName(R.Id),FString(R.Name),FString(R.Hint),SelectedByProfile&&Ready&&(Gameplay||LegacyAbility)});
    }
    return Result;
}
void AHeliosBioHotelGameMode::RefreshHunterPool()
{Model.Config.AllowedHunterRoles.Reset();for(const auto& R:HunterOptions())if(R.bAvailable)Model.Config.AllowedHunterRoles.Add(R.Id);}
bool AHeliosBioHotelGameMode::SelectHunter(int32 Id,FName SelectedRole)
{
    auto* Art=HeliosBioArt::Named(SelectedRole);if(!Art||!Art->bIdentityVerified||!Model.ConfirmHunterChoice(Id,SelectedRole))return false;
    if(Id==0){CloseHunterChooser();ShowNotice(TEXT("已变身 · ")+Art->DisplayName,5);}else ShowNotice(TEXT("高级猎手已出现"));return true;
}
void AHeliosBioHotelGameMode::OpenHunterChooser()
{
    const auto* P=Model.FindParticipant(0);if(!P||!P->bAlive||!P->bHunterChoicePending)return;
    bHunterChooserOpen=true;if(auto* PC=UGameplayStatics::GetPlayerController(this,0)){PC->bShowMouseCursor=true;PC->SetInputMode(FInputModeGameAndUI());}
}
void AHeliosBioHotelGameMode::CloseHunterChooser()
{
    bHunterChooserOpen=false;Model.CancelHunterChoice(0);
    if(auto* PC=UGameplayStatics::GetPlayerController(this,0)){PC->bShowMouseCursor=false;PC->SetInputMode(FInputModeGameOnly());}
}
void AHeliosBioHotelGameMode::LoadOriginalMovingPlatforms()
{
    if(!GetWorld()->GetOutermost()->GetName().EndsWith(TEXT("/CFMBioPyramidFloating")))return;
    FString Text;const FString File=FPaths::ProjectContentDir()/TEXT("Helios/CFMBioPyramidFloating/Map/platform_runtime.json");
    TSharedPtr<FJsonObject> Data;if(!FFileHelper::LoadFileToString(Text,*File)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Data)||!Data.IsValid())
    {UE_LOG(LogTemp,Error,TEXT("BIO_PLATFORM_ORIGINAL_RUNTIME_CONFIG_MISSING %s"),*File);return;}
    auto* Mesh=LoadObject<UStaticMesh>(nullptr,*Data->GetStringField(TEXT("mesh")));TArray<FVector> Points;
    for(const auto& Value:Data->GetArrayField(TEXT("world_points_cm"))){const auto& P=Value->AsArray();if(P.Num()==3)Points.Add(FVector(P[0]->AsNumber(),P[1]->AsNumber(),P[2]->AsNumber()));}
    auto* Platform=GetWorld()->SpawnActor<AHeliosBioMovingPlatform>();
    if(Platform&&Platform->ConfigureSourceRoute(Mesh,Points,Data->GetNumberField(TEXT("speed_cm_per_second")),Data->GetNumberField(TEXT("endpoint_pause_seconds")),Data->GetIntegerField(TEXT("original_loop_type")),EHeliosBioPlatformReplay::PingPong))MovingPlatforms.Add(Platform);
    else if(Platform)Platform->Destroy();
}
