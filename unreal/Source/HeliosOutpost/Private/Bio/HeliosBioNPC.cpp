#include "Bio/HeliosBioNPC.h"
#include "Bio/HeliosBioHotelGameMode.h"
#include "Bio/HeliosBioPlayer.h"
#include "Bio/HeliosBioSupply.h"
#include "Bio/HeliosBioRoundInventoryComponent.h"
#include "Bio/HeliosBioSupplyWeapon.h"
#include "Bio/HeliosBioSupplyWeaponDefinition.h"
#include "Bio/HeliosBioClone.h"
#include "Bio/HeliosBioTurret.h"
#include "Sound/SoundBase.h"
#include "Bio/HeliosBioArt.h"
#include "Bio/HeliosBioFourSlashComponent.h"
#include "Bio/HeliosBioPlatformTraversal.h"
#include "Bio/HeliosBioStaticJumpLink.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Player/HeliosCharacterDefinition.h"
#include "Player/HeliosCharacterActionProfile.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Weapons/HeliosCombatWeapons.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Navigation/PathFollowingComponent.h"
#include "EngineUtils.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "Kismet/GameplayStatics.h"

namespace
{
// Character identity and weapon compatibility are checked against the actual saved
// profiles. A missing form is skipped rather than displayed with another skin.
const TArray<FHeliosLoadout>& BioHumanRoster()
{
    static TArray<FHeliosLoadout> Roster;
    if(!Roster.IsEmpty())return Roster;
    const FName Firearms[]={TEXT("cfm_chan_zeus"),TEXT("cfm_bulianshi_p90"),TEXT("cfm_wangzhekui_thunder"),TEXT("cfm_yun_summer_megatron")};
    for(const auto* Character:FHeliosCharacterRegistry::GetAvailableCharacters(false))
    {
        if(!Character||!Character->bCompanionSelectable)continue;
        for(const auto& Form:Character->Forms)
        {
            if(!Form.bPresentationReady)continue;
            auto* Profile=Form.ActionProfile.LoadSynchronous();
            if(!Profile||Profile->CharacterId!=Character->CharacterId||Profile->CharacterVariantId!=Form.VariantId)continue;
            for(FName WeaponId:Firearms)
            {
                const auto* Weapon=UHeliosWeaponRegistry::FindWeapon(WeaponId);
                const auto* Pair=Profile->FindWeapon(WeaponId);
                if(!Weapon||!Weapon->WeaponClass||!Pair||!Pair->bGeneratedAndValidated||Pair->ThirdPersonMesh.IsNull()||
                    !FPackageName::DoesPackageExist(Pair->ThirdPersonMesh.ToSoftObjectPath().GetLongPackageName()))continue;
                bool Complete=true;
                for(FName Action:{FName(TEXT("Idle")),FName(TEXT("Move")),FName(TEXT("Fire")),FName(TEXT("ChangeClip")),FName(TEXT("Down"))})
                {
                    const auto* Clip=Pair->ThirdPersonClips.Find(Action);
                    if(!Clip||Clip->IsNull()||!FPackageName::DoesPackageExist(Clip->ToSoftObjectPath().GetLongPackageName())){Complete=false;break;}
                }
                if(!Complete)continue;
                auto* HumanMesh=Pair->ThirdPersonMesh.LoadSynchronous();const auto* DownRef=Pair->ThirdPersonClips.Find(TEXT("Down"));
                auto* Down=DownRef?DownRef->LoadSynchronous():nullptr;
                if(!HumanMesh||!Down||Down->GetSkeleton()!=HumanMesh->GetSkeleton()||!FMath::IsFinite(Down->GetPlayLength())||Down->GetPlayLength()<=0)
                {UE_LOG(LogTemp,Warning,TEXT("BIO_NPC_ROSTER_DOWN_REJECTED character=%s form=%s weapon=%s compatible_native_down=false"),*Character->CharacterId.ToString(),*Form.VariantId.ToString(),*WeaponId.ToString());continue;}
                FHeliosLoadout Entry;Entry.CharacterId=Character->CharacterId;Entry.CharacterVariantId=Form.VariantId;Entry.PrimaryWeaponId=WeaponId;
                Roster.Add(Entry);
            }
        }
    }
    TSet<FName> Characters;TSet<FString> Forms;
    for(const auto& Entry:Roster){Characters.Add(Entry.CharacterId);Forms.Add(Entry.CharacterId.ToString()+TEXT("/")+Entry.CharacterVariantId.ToString());}
    UE_LOG(LogTemp,Display,TEXT("BIO_NPC_ROSTER characters=%d forms=%d compatible_firearm_pairs=%d"),Characters.Num(),Forms.Num(),Roster.Num());
    return Roster;
}
FVector BioTargetGround(AActor* Actor)
{if(auto* Character=Cast<ACharacter>(Actor))return Character->GetNavAgentLocation();return Actor?Actor->GetActorLocation():FVector::ZeroVector;}
UAnimSequence* BioHumanDownClip(const FHeliosLoadout& Loadout)
{
    const auto* Character=FHeliosCharacterRegistry::FindCharacter(Loadout.CharacterId);
    const auto* Form=Character?Character->FindForm(Loadout.CharacterVariantId):nullptr;
    if(!Character||!Form)return nullptr;
    FString Path=Form->ActionProfile.ToSoftObjectPath().ToString();
    if(Path.IsEmpty())Path=Character->ActionProfile.ToSoftObjectPath().ToString();
    if(Path.IsEmpty()&&Form->VariantId.IsNone())Path=FString::Printf(TEXT("/Game/Helios/LoadoutPresentation/DA_%s.DA_%s"),*Character->CharacterId.ToString(),*Character->CharacterId.ToString());
    auto* Profile=Path.IsEmpty()?nullptr:LoadObject<UHeliosCharacterActionProfile>(nullptr,*Path);
    if(!Profile||Profile->CharacterId!=Loadout.CharacterId||Profile->CharacterVariantId!=Loadout.CharacterVariantId)return nullptr;
    const auto* Pair=Profile->FindWeapon(Loadout.PrimaryWeaponId);
    const auto* Down=Pair&&Pair->bGeneratedAndValidated?Pair->ThirdPersonClips.Find(TEXT("Down")):nullptr;
    return Down?Down->LoadSynchronous():nullptr;
}
}

AHeliosBioNPC::AHeliosBioNPC()
{
    RoundInventory=CreateDefaultSubobject<UHeliosBioRoundInventoryComponent>(TEXT("BioRoundInventory"));
    AIControllerClass=AAIController::StaticClass();
    AutoPossessAI=EAutoPossessAI::PlacedInWorldOrSpawned;
    FourSlash=CreateDefaultSubobject<UHeliosBioFourSlashComponent>(TEXT("BioFourSlash"));
    HumanPresentation=CreateDefaultSubobject<UHeliosCharacterPresentationComponent>(TEXT("BioHumanPresentation"));
    RoleWeaponVisual=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("BioRolePrimaryWeapon"));
    RoleWeaponVisual->SetupAttachment(GetMesh());RoleWeaponVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RoleWeaponVisual->SetCanEverAffectNavigation(false);RoleWeaponVisual->SetVisibility(false);
    RoleWeaponVisual->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    SerumWeaponVisual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("OriginalSerumGun"));SerumWeaponVisual->SetupAttachment(GetMesh(),TEXT("b_RightWeapon"));SerumWeaponVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);SerumWeaponVisual->SetCanEverAffectNavigation(false);SerumWeaponVisual->SetVisibility(false);
    GetCapsuleComponent()->InitCapsuleSize(34,96);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    RestoreHumanMeshTransform();
    GetCharacterMovement()->MaxWalkSpeed=400;
    GetCharacterMovement()->bOrientRotationToMovement=false;
    GetCharacterMovement()->bUseControllerDesiredRotation=true;
    GetCharacterMovement()->RotationRate=FRotator(0,540,0);
    GetCharacterMovement()->bUseRVOAvoidance=true;
    GetCharacterMovement()->AvoidanceConsiderationRadius=400.f;
    Tags.AddUnique(TEXT("BioParticipant"));
    Tags.Remove(TEXT("Enemy"));
    Tags.AddUnique(TEXT("HeliosPet")); // Select the existing source third-person firearm path.
}
void AHeliosBioNPC::BeginPlay()
{
    // The separate match owns health, team, targeting and respawn; no Shooter StateTree.
    ACharacter::BeginPlay();
    if(!Controller)SpawnDefaultController();
    if(!bLoadoutInitialized)if(auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld()))InitializeBioParticipant(ParticipantId,1337+Mode->RoundSerial*103,Mode->SelectedLoadout);
    ApplyTeamPresentation(EHeliosBioTeam::Human,true);
}
void AHeliosBioNPC::InitializeBioParticipant(int32 Id,int32 RoundSeed,const FHeliosLoadout& PreferredLoadout)
{
    if(RoundInventory->IsEnabled()){RoundInventory->ResetRound(true);Weapon=nullptr;}
    ParticipantId=Id;ParticipantRoundSeed=RoundSeed;ChooseHumanLoadout(PreferredLoadout);bLoadoutInitialized=true;
    ResetFloatingAction();
    if(auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld()))
    {
        const auto& C=Mode->Model.Config;auto* Movement=GetCharacterMovement();
        const float Radius=FMath::Max(1.f,C.MovementCapsuleRadius),Half=FMath::Max(Radius,C.MovementCapsuleHalfHeight);
        GetCapsuleComponent()->SetCapsuleSize(Radius,Half);
        Movement->GetNavAgentPropertiesRef().AgentRadius=Radius;Movement->GetNavAgentPropertiesRef().AgentHeight=Half*2;
        Movement->MaxStepHeight=FMath::Max(0.f,C.MovementStepHeight);Movement->SetWalkableFloorAngle(C.MovementSlopeDegrees);
        // Idle guards can be displaced by impacts/RVO at a ledge. Revalidate
        // their native floor instead of retaining a stale grounded result.
        if(Mode->bFloatingProfile)Movement->bAlwaysCheckFloor=true;
        if(C.MovementJumpZ>0)Movement->JumpZVelocity=C.MovementJumpZ;
        // The floating scene reuses the installed player's offline air control;
        // no claim that this is a decoded original client movement parameter.
        if(GetWorld()->GetOutermost()->GetName().EndsWith(TEXT("/CFMBioPyramidFloating"))&&Mode->Player)
            Movement->AirControl=Mode->Player->GetCharacterMovement()->AirControl;
        RestoreHumanMeshTransform();
        UE_LOG(LogTemp,Display,TEXT("BIO_NPC_PHYSICAL_PROFILE id=%d radius=%.1f half=%.1f step=%.1f slope=%.1f jump=%.1f air_control=%.2f eye_height=%.1f"),Id,Radius,Half,Movement->MaxStepHeight,Movement->GetWalkableFloorAngle(),Movement->JumpZVelocity,Movement->AirControl,C.EyeHeight);
    }
    bDefenseGoalValid=false;RecoveryRemaining=0;AvoidTargetRemaining=0;AvoidTarget.Reset();
    CombatRepositionRemaining=1.f+float((Id*13)%23)*.1f;
    UE_LOG(LogTemp,Display,TEXT("BIO_NPC_LOADOUT id=%d seed=%d character=%s form=%s weapon=%s"),Id,RoundSeed,*HumanLoadout.CharacterId.ToString(),*HumanLoadout.CharacterVariantId.ToString(),*HumanLoadout.PrimaryWeaponId.ToString());
}
void AHeliosBioNPC::ChooseHumanLoadout(const FHeliosLoadout& Preferred)
{
    const auto& Roster=BioHumanRoster();
    TArray<FHeliosLoadout> Forms;
    // Visit every available form before repeating one; weapon rotates separately.
    for(const auto& Entry:Roster)
        if(!Forms.ContainsByPredicate([&](const FHeliosLoadout& Other){return Other.CharacterId==Entry.CharacterId&&Other.CharacterVariantId==Entry.CharacterVariantId;}))Forms.Add(Entry);
    if(Forms.IsEmpty())
    {HumanLoadout=FHeliosLoadout::Normalize(Preferred);UE_LOG(LogTemp,Error,TEXT("BIO_NPC_COMPATIBLE_ROSTER_EMPTY id=%d"),ParticipantId);return;}
    const int32 Index=int32((uint32(ParticipantRoundSeed)+uint32(FMath::Max(0,ParticipantId-1)))%uint32(Forms.Num()));
    const auto& Form=Forms[Index];TArray<FHeliosLoadout> Weapons;
    for(const auto& Entry:Roster)if(Entry.CharacterId==Form.CharacterId&&Entry.CharacterVariantId==Form.CharacterVariantId)Weapons.Add(Entry);
    HumanLoadout=Weapons[(FMath::Max(0,ParticipantId-1)/Forms.Num()+Index)%Weapons.Num()];
}
void AHeliosBioNPC::RestoreHumanMeshTransform()
{
    GetMesh()->SetRelativeTransform(FTransform(FRotator(0,-90,0),FVector(0,0,-GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()),FVector::OneVector));
    GetMesh()->SetLeaderPoseComponent(nullptr);
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
FTransform AHeliosBioNPC::RoleMeshTransformForCapsule(const FTransform& SourceTransform) const
{
    FTransform Result=SourceTransform;Result.AddToTranslation(FVector(0,0,96.f-GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()));return Result;
}
FString AHeliosBioNPC::GetMovementDiagnostic() const
{
    const auto* AI=Cast<AAIController>(Controller);
    return FString::Printf(TEXT("intent=%s active=%d goal=%s target=%s result=%d status=%d blocked=%.2f recoveries=%d reason=%s platform=%s"),
        *MovementIntent.ToString(),bMoveIntentActive?1:0,*MovementGoal.ToString(),*GetNameSafe(Target.Get()),LastMoveResult,AI?int32(AI->GetMoveStatus()):-1,
        BlockedSeconds,MovementRecoveries,*LastMoveReason,*(HeliosBioPlatformTraversal::DebugState(this)+TEXT(" static_jump={")+AHeliosBioStaticJumpLink::DebugState(this)+TEXT("} floating={")+GetFloatingActionDiagnostic()+TEXT("}")));
}
bool AHeliosBioNPC::FindReachableGoal(const FVector& Wanted,FVector& Goal) const
{
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());FNavLocation Projected;
    if(!Nav||!Nav->ProjectPointToNavigation(Wanted,Projected,FVector(200,200,350),&GetNavAgentPropertiesRef()))return false;
    auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),GetNavAgentLocation(),Projected.Location,const_cast<AHeliosBioNPC*>(this));
    if(!Path||!Path->IsValid()||Path->IsPartial())return false;
    Goal=Projected.Location;return true;
}
bool AHeliosBioNPC::ChooseDefenseGoal()
{
    auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());if(!Mode)return false;
    if(Mode->bFloatingProfile)return ChooseFloatingDefenseGoal();
    const FVector Origin=GetNavAgentLocation();float Best=-FLT_MAX;FVector Chosen=Origin;
    FRandomStream Random(ParticipantRoundSeed+ParticipantId*7919);
    auto Consider=[&](const FVector& Wanted)
    {
        const float Distance=FVector::Dist2D(Origin,Wanted);if(Distance<180.f||Distance>2400.f)return;
        FVector At;if(!FindReachableGoal(Wanted,At))return;
        float Score=(At.Z-Origin.Z)*1.3f-Distance*.08f+Random.FRandRange(0.f,180.f);
        for(auto Other:Mode->Bots)if(IsValid(Other)&&Other!=this&&Other->bDefenseGoalValid&&FVector::DistSquared2D(At,Other->DefenseGoal)<FMath::Square(220.f))Score-=500.f;
        if(Score>Best){Best=Score;Chosen=At;}
    };
    for(TActorIterator<AActor> It(GetWorld());It;++It)if(It->ActorHasTag(TEXT("BioSpawn")))Consider(It->GetActorLocation());
    for(int32 Sample=0;Sample<8;++Sample)
    {
        const float Angle=Random.FRandRange(0.f,2.f*PI),Radius=Random.FRandRange(450.f,1400.f);
        Consider(Origin+FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,0));
    }
    bDefenseGoalValid=Best>-FLT_MAX;DefenseGoal=Chosen;return bDefenseGoalValid;
}
bool AHeliosBioNPC::RequestBioMove(const FVector& Goal,float Acceptance,FName Intent,bool AllowPartial)
{
    auto* AI=Cast<AAIController>(Controller);if(!AI)return false;
    const bool ChangedIntent=MovementIntent!=Intent;
    const bool NewGoal=ChangedIntent||FVector::DistSquared2D(MovementGoal,Goal)>FMath::Square(100.f);
    if(ChangedIntent){BlockedSeconds=0;LastProgressPosition=GetActorLocation();ProgressSampleAge=0;}
    MovementIntent=Intent;MovementGoal=Goal;bMoveIntentActive=FVector::DistSquared(GetNavAgentLocation(),Goal)>FMath::Square(Acceptance+20.f);
    if(!NewGoal&&MoveRequestAge<.8f&&AI->GetMoveStatus()==EPathFollowingStatus::Moving)return true;
    MoveRequestAge=0;
    LastMoveResult=int32(AI->MoveToLocation(Goal,Acceptance,false,true,false,true,nullptr,AllowPartial));
    LastMoveReason=LastMoveResult==int32(EPathFollowingRequestResult::Failed)?TEXT("path_request_failed"):LastMoveResult==int32(EPathFollowingRequestResult::AlreadyAtGoal)?TEXT("arrived"):TEXT("path_requested");
    if(LastMoveResult==int32(EPathFollowingRequestResult::AlreadyAtGoal))bMoveIntentActive=false;
    return LastMoveResult!=int32(EPathFollowingRequestResult::Failed);
}
void AHeliosBioNPC::HoldBioPosition(FName Intent)
{
    MovementIntent=Intent;bMoveIntentActive=false;BlockedSeconds=0;LastMoveReason=TEXT("intentional_hold");
    if(auto* AI=Cast<AAIController>(Controller);AI&&(Intent!=TEXT("Frozen")||GetCharacterMovement()->IsMovingOnGround()))AI->StopMovement();
}
void AHeliosBioNPC::RecoverBlockedMovement()
{
    if(auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());Mode&&Mode->bFloatingProfile)
    {
        FailedFloatingGoal=MovementGoal;FailedFloatingUntil=GetWorld()->GetTimeSeconds()+4.f;
        FloatingAction.bValid=false;FloatingPlanAt=GetWorld()->GetTimeSeconds()+1.6f;
    }
    ++MovementRecoveries;BlockedSeconds=0;MoveRequestAge=100.f;
    if(auto* AI=Cast<AAIController>(Controller))AI->StopMovement();
    if(TryReconnectToNavigation())
    {UE_LOG(LogTemp,Display,TEXT("BIO_NPC_MOVE_RECOVERY id=%d %s"),ParticipantId,*GetMovementDiagnostic());return;}
    if(auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());Mode&&Mode->bFloatingProfile)
    {
        for(auto Other:Mode->Bots)if(IsValid(Other)&&Other!=this&&Other->ParticipantId<ParticipantId&&
            !Mode->AreOpponents(ParticipantId,Other->ParticipantId)&&FVector::Dist(GetActorLocation(),Other->GetActorLocation())<120.f)
        {
            const FVector Forward=(MovementGoal-GetNavAgentLocation()).GetSafeNormal2D();const FVector Side(-Forward.Y,Forward.X,0);
            for(float Sign:{1.f,-1.f})
            {
                FVector At;if(FindReachableGoal(GetNavAgentLocation()+Side*Sign*160.f,At)&&FVector::Dist2D(At,GetNavAgentLocation())>70.f)
                {RecoveryRemaining=1.2f;RequestBioMove(At,25.f,TEXT("Yield"));LastMoveReason=TEXT("yield_to_nearby_ally");return;}
            }
        }
    }
    FRandomStream Random(ParticipantRoundSeed+ParticipantId*701+MovementRecoveries*31);
    for(int32 Attempt=0;Attempt<8;++Attempt)
    {
        const float Angle=Random.FRandRange(0,2*PI);FVector At;
        if(FindReachableGoal(GetNavAgentLocation()+FVector(FMath::Cos(Angle)*400,FMath::Sin(Angle)*400,0),At)&&FVector::DistSquared2D(At,GetActorLocation())>FMath::Square(140.f))
        {RecoveryRemaining=1.6f;RequestBioMove(At,55.f,TEXT("Unblock"));LastMoveReason=TEXT("reachable_detour");break;}
    }
    if(RecoveryRemaining<=0)
    {
        AvoidTarget=Target;AvoidTargetRemaining=4.f;
        bDefenseGoalValid=false;HoldBioPosition(TEXT("Replan"));DecisionRemaining=0;
    }
    UE_LOG(LogTemp,Display,TEXT("BIO_NPC_MOVE_RECOVERY id=%d %s"),ParticipantId,*GetMovementDiagnostic());
}
bool AHeliosBioNPC::TryReconnectToNavigation()
{
    auto* AI=Cast<AAIController>(Controller);auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if(!AI||!Nav||!GetCharacterMovement()->IsMovingOnGround())return false;
    const FVector Feet=GetNavAgentLocation();const float Half=GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const float Radius=GetCapsuleComponent()->GetScaledCapsuleRadius(),Step=GetCharacterMovement()->MaxStepHeight;
    FNavLocation GoalOnNav;if(!Nav->ProjectPointToNavigation(MovementGoal,GoalOnNav,FVector(200,200,350),&GetNavAgentPropertiesRef()))return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BioReconnectPhysicalWalk),false,this);
    TArray<AActor*> Attached;GetAttachedActors(Attached,true,true);for(auto Actor:Attached)Query.AddIgnoredActor(Actor);
    FCollisionObjectQueryParams Static(ECC_WorldStatic);const FCollisionShape Capsule=FCollisionShape::MakeCapsule(Radius,Half);
    FVector Best=FVector::ZeroVector;float BestDistance=MAX_flt;
    // A broad QA nav projection can succeed beside a tiny authored ledge even
    // while MoveTo cannot project its actual start. Recover by WALKING a short,
    // fully collision-checked connection; no teleport and no ignored map wall.
    for(float SearchRadius:{40.f,80.f,140.f,220.f,300.f})for(int32 Direction=0;Direction<8;++Direction)
    {
        if(SearchRadius-65.f>BestDistance)continue;
        const float Angle=2.f*PI*Direction/8.f;FNavLocation At;
        const FVector Wanted=Feet+FVector(FMath::Cos(Angle)*SearchRadius,FMath::Sin(Angle)*SearchRadius,0);
        if(!Nav->ProjectPointToNavigation(Wanted,At,FVector(45,45,60),&GetNavAgentPropertiesRef()))continue;
        const float Distance=FVector::Dist2D(Feet,At.Location);if(Distance<25.f||Distance>320.f||Distance>=BestDistance)continue;
        auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),At.Location,GoalOnNav.Location,this);
        if(!Path||!Path->IsValid()||Path->IsPartial())continue;
        bool Walkable=true;float PreviousHeight=Feet.Z,Highest=Feet.Z;FVector Landing=At.Location;
        const int32 Samples=FMath::Max(2,FMath::CeilToInt(Distance/30.f));
        for(int32 Sample=1;Sample<=Samples;++Sample)
        {
            FVector Point=FMath::Lerp(Feet,At.Location,float(Sample)/Samples);FHitResult Floor;
            if(!GetWorld()->LineTraceSingleByObjectType(Floor,Point+FVector(0,0,Step+20.f),Point-FVector(0,0,Step+20.f),Static,Query)||
                Floor.ImpactNormal.Z<GetCharacterMovement()->GetWalkableFloorZ()||FMath::Abs(Floor.ImpactPoint.Z-PreviousHeight)>Step)
            {Walkable=false;break;}
            PreviousHeight=Floor.ImpactPoint.Z;Highest=FMath::Max(Highest,PreviousHeight);Landing.Z=PreviousHeight+2.f;
        }
        // The destination must be the measured nav floor, not another raised
        // decoration which would leave the next path request stranded again.
        if(!Walkable||FMath::Abs(Landing.Z-At.Location.Z)>6.f)continue;
        const FVector Raised=FVector(Landing.X,Landing.Y,Highest+Half+2.f),Center=Landing+FVector(0,0,Half);
        FHitResult Obstruction;
        if(GetWorld()->SweepSingleByChannel(Obstruction,GetActorLocation(),Raised,FQuat::Identity,ECC_Pawn,Capsule,Query)||
            GetWorld()->SweepSingleByChannel(Obstruction,Raised,Center,FQuat::Identity,ECC_Pawn,Capsule,Query))continue;
        Best=Landing;BestDistance=Distance;
    }
    if(BestDistance==MAX_flt)return false;
    const int32 Result=int32(AI->MoveToLocation(Best,12.f,false,false,false,false,nullptr,false));
    if(Result!=int32(EPathFollowingRequestResult::RequestSuccessful))return false;
    bNavigationReconnect=true;GetCharacterMovement()->SetAvoidanceEnabled(false);
    MovementIntent=TEXT("ReconnectNav");MovementGoal=Best;bMoveIntentActive=true;LastMoveResult=Result;
    RecoveryRemaining=FMath::Max(1.2f,BestDistance/FMath::Max(100.f,GetCharacterMovement()->MaxWalkSpeed)+.8f);
    LastMoveReason=FString::Printf(TEXT("physical_walk_to_nav distance=%.2f start_feet=%s"),BestDistance,*Feet.ToString());
    return true;
}
void AHeliosBioNPC::TickMovementProgress(float Dt)
{
    MoveRequestAge+=Dt;RecoveryRemaining=FMath::Max(0.f,RecoveryRemaining-Dt);AvoidTargetRemaining=FMath::Max(0.f,AvoidTargetRemaining-Dt);CombatRepositionRemaining-=Dt;
    if(bNavigationReconnect)
    {
        if(FVector::DistSquared2D(GetNavAgentLocation(),MovementGoal)<=FMath::Square(14.f)||RecoveryRemaining<=0)
        {bNavigationReconnect=false;RecoveryRemaining=0;DecisionRemaining=0;MoveRequestAge=100.f;}
        // Keep RVO off after a real navigation-start failure. It does not respect
        // Recast boundaries; the ordinary complete-path detours remain enabled.
    }
    if(!bMoveIntentActive||bPlatformTraversalActive||GetCharacterMovement()->IsFalling()||FourSlash->IsBusy()){BlockedSeconds=0;LastProgressPosition=GetActorLocation();ProgressSampleAge=0;return;}
    ProgressSampleAge+=Dt;if(ProgressSampleAge<.5f)return;
    const float Travel=FVector::Dist2D(GetActorLocation(),LastProgressPosition);
    BlockedSeconds=Travel<12.f?BlockedSeconds+ProgressSampleAge:0.f;LastProgressPosition=GetActorLocation();ProgressSampleAge=0;
    if(BlockedSeconds>=1.5f)RecoverBlockedMovement();
}
void AHeliosBioNPC::EndPlay(const EEndPlayReason::Type Reason)
{RoundInventory->ResetRound(true);ClearHumanInfectionTransition();AHeliosBioStaticJumpLink::Forget(this);HeliosBioPlatformTraversal::ForgetBot(this);FourSlash->Cancel();if(Weapon){Weapon->StopFiring();Weapon->Destroy();Weapon=nullptr;}ACharacter::EndPlay(Reason);}
void AHeliosBioNPC::AttachWeaponMeshes(AShooterWeapon* W)
{
    if(!W)return;W->AttachToActor(this,FAttachmentTransformRules::KeepRelativeTransform);
    W->GetFirstPersonMesh()->SetVisibility(false,true);W->GetThirdPersonMesh()->SetVisibility(false,true);
    if(bPreparingInventoryWeapon)return;
    HumanPresentation->AttachWeapon(W);HumanPresentation->BindWeaponSource(W);
}
void AHeliosBioNPC::OnWeaponActivated(AShooterWeapon* W){Weapon=W;AttachWeaponMeshes(W);}
void AHeliosBioNPC::OnWeaponDeactivated(AShooterWeapon* W){if(W)W->StopFiring();}
void AHeliosBioNPC::UpdateWeaponHUD(int32 Ammo,int32 Magazine)
{if(bPreparingInventoryWeapon)return;if(!bHumanInfectionTransition&&!bReloadCommit&&Ammo<LastAmmo){++ShotsFired;++AttackCount;AttackAge=0;}LastAmmo=Ammo;MagazineSize=Magazine;}
void AHeliosBioNPC::OnSemiWeaponRefire()
{auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=Mode?Mode->Model.FindParticipant(ParticipantId):nullptr;if(P&&!bHumanInfectionTransition&&P->FrozenSeconds<=0&&Mode->Model.GetPhase()==EHeliosBioPhase::Active&&Target.IsValid()&&Weapon&&LastTeam==EHeliosBioTeam::Human&&!bIsDead&&!bQAFreeze&&ReloadRemaining<=0)Weapon->StartFiring();}
FVector AHeliosBioNPC::GetWeaponTargetLocation()
{return Target.IsValid()?Target->GetActorLocation()+FVector(0,0,35):GetActorLocation()+GetActorForwardVector()*1000;}
void AHeliosBioNPC::GetActorEyesViewPoint(FVector& L,FRotator& R)const
{const auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());const float Height=Mode?Mode->Model.Config.EyeHeight:160.f;L=GetActorLocation()+FVector(0,0,Height-GetCapsuleComponent()->GetScaledCapsuleHalfHeight());R=Target.IsValid()?(Target->GetActorLocation()+FVector(0,0,25)-L).Rotation():GetActorRotation();}
bool AHeliosBioNPC::BeginHumanInfectionTransition()
{
    if(bHumanInfectionTransition)return !bHumanInfectionTransitionBlocked;
    auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=Mode?Mode->Model.FindParticipant(ParticipantId):nullptr;
    if(!P||!P->bAlive||P->Team!=EHeliosBioTeam::Zombie||LastTeam!=EHeliosBioTeam::Human)
    {UE_LOG(LogTemp,Error,TEXT("BIO_NPC_INFECTION_INVALID_STATE id=%d rendered_team=%d rules_team=%d alive=%d"),ParticipantId,int32(LastTeam),P?int32(P->Team):-1,P&&P->bAlive);return false;}

    bHumanInfectionTransition=true;bHumanInfectionTransitionBlocked=false;bHumanInfectionPosePending=false;
    HumanInfectionAge=0.f;HumanInfectionDuration=0.f;HumanInfectionDown=nullptr;
    Tags.AddUnique(TEXT("BioHumanInfectionTransition"));Tags.Remove(TEXT("Friendly"));
    auto* Movement=GetCharacterMovement();const float VerticalVelocity=Movement->Velocity.Z;
    AHeliosBioStaticJumpLink::Forget(this);HeliosBioPlatformTraversal::ForgetBot(this);bPlatformTraversalActive=false;
    FourSlash->Cancel();StopJumping();ConsumeMovementInputVector();
    if(bWasFrozen){Movement->MaxAcceleration=FrozenSavedAcceleration;bWasFrozen=false;}
    HumanInfectionSavedAcceleration=Movement->MaxAcceleration;Movement->MaxAcceleration=0.f;
    HoldBioPosition(TEXT("HumanInfectionDown"));
    if(auto* AI=Cast<AAIController>(Controller))AI->ClearFocus(EAIFocusPriority::Gameplay);
    // Abort the former walk/jump intent without suppressing gravity or cancelling
    // the capsule's falling velocity when infection happens above the floor.
    Movement->Velocity.X=0.f;Movement->Velocity.Y=0.f;
    if(Movement->IsFalling())Movement->Velocity.Z=VerticalVelocity;
    bNavigationReconnect=false;RecoveryRemaining=0.f;Target.Reset();AvoidTarget.Reset();ResetFloatingAction();
    bRolePrimaryFiring=false;BioSkillStage=0;bPlayingTransientPose=false;CurrentBioClip=nullptr;
    ReloadRemaining=0.f;bReloadCommit=false;DecisionRemaining=0.f;
    if(Weapon){Weapon->StopFiring();Weapon->CancelReload();Weapon->DeactivateWeapon();}
    // The human TP weapon is owned by HumanPresentation, so the retired source
    // firearm can stop completely while its original equipped appearance stays.
    HumanPresentation->CancelCharacterGesture();HumanPresentation->BindWeaponSource(nullptr);
    HumanPresentation->SetComponentTickEnabled(true);HumanPresentation->SetVisible(true);
    HumanInfectionDown=BioHumanDownClip(HumanLoadout);
    if(!HumanPresentation->IsReady()||HumanPresentation->GetArmsMesh()!=GetMesh()||!HumanInfectionDown)
    {BlockHumanInfectionTransition(TEXT("original_tp_down_missing"));return false;}
    auto* HumanBodyMesh=GetMesh()->GetSkeletalMeshAsset();
    if(!HumanBodyMesh||!HumanInfectionDown->GetSkeleton()||HumanInfectionDown->GetSkeleton()!=HumanBodyMesh->GetSkeleton())
    {BlockHumanInfectionTransition(TEXT("original_tp_down_skeleton_mismatch"));return false;}
    HumanInfectionDuration=HumanPresentation->GetRecoveryDuration(TEXT("Down"),0.f);
    if(!FMath::IsFinite(HumanInfectionDuration)||HumanInfectionDuration<=0.f||!FMath::IsNearlyEqual(HumanInfectionDuration,HumanInfectionDown->GetPlayLength(),.0001f))
    {BlockHumanInfectionTransition(TEXT("original_tp_down_duration_invalid"));return false;}
    HumanInfectionQueuedCommitCount=HumanPresentation->GetPoseCommitCount();HumanInfectionQueuedFrame=GFrameCounter;
    if(!HumanPresentation->PresentAction(TEXT("Down"),0.f,false))
    {BlockHumanInfectionTransition(TEXT("original_tp_down_pose_rejected"));return false;}
    bHumanInfectionPosePending=true;
    UE_LOG(LogTemp,Display,TEXT("BIO_NPC_INFECTION_DOWN_BEGIN id=%d character=%s form=%s weapon=%s clip=%s duration=%.6f rules_team=%d"),ParticipantId,*HumanLoadout.CharacterId.ToString(),*HumanLoadout.CharacterVariantId.ToString(),*HumanLoadout.PrimaryWeaponId.ToString(),*HumanInfectionDown->GetPathName(),HumanInfectionDuration,int32(P->Team));
    return true;
}
void AHeliosBioNPC::BlockHumanInfectionTransition(const TCHAR* Reason)
{
    if(bHumanInfectionTransitionBlocked)return;
    bHumanInfectionTransitionBlocked=true;bHumanInfectionPosePending=false;
    LastMoveReason=Reason;
    UE_LOG(LogTemp,Error,TEXT("BIO_NPC_INFECTION_DOWN_BLOCKED id=%d reason=%s character=%s form=%s weapon=%s clip=%s age=%.6f duration=%.6f"),ParticipantId,Reason,*HumanLoadout.CharacterId.ToString(),*HumanLoadout.CharacterVariantId.ToString(),*HumanLoadout.PrimaryWeaponId.ToString(),*GetPathNameSafe(HumanInfectionDown.Get()),HumanInfectionAge,HumanInfectionDuration);
}
void AHeliosBioNPC::ClearHumanInfectionTransition()
{
    if(bHumanInfectionTransition)GetCharacterMovement()->MaxAcceleration=HumanInfectionSavedAcceleration;
    bHumanInfectionTransition=false;bHumanInfectionTransitionBlocked=false;bHumanInfectionPosePending=false;
    HumanInfectionDown=nullptr;HumanInfectionAge=0.f;HumanInfectionDuration=0.f;
    HumanInfectionQueuedCommitCount=0;HumanInfectionQueuedFrame=0;
    Tags.Remove(TEXT("BioHumanInfectionTransition"));
}
void AHeliosBioNPC::TickHumanInfectionTransition(float Dt)
{
    if(bHumanInfectionTransitionBlocked)return;
    if(bHumanInfectionPosePending)
    {
        auto* Instance=GetMesh()->GetSingleNodeInstance();
        const bool Committed=HumanPresentation->GetPoseCommitCount()!=HumanInfectionQueuedCommitCount&&Instance&&
            Instance->GetCurrentAsset()==HumanInfectionDown.Get()&&FMath::IsNearlyEqual(Instance->GetCurrentTime(),HumanInfectionAge,.001f);
        if(!Committed)
        {
            // Begin may arrive after this frame's presentation tick. Allow that
            // boundary, then require real sampling rather than advancing a fake clock.
            if(GFrameCounter>HumanInfectionQueuedFrame+1)BlockHumanInfectionTransition(TEXT("original_tp_down_pose_not_committed"));
            return;
        }
        bHumanInfectionPosePending=false;
        if(HumanInfectionAge>=HumanInfectionDuration)
        {
            ++HumanDownCommit.Sequence;
            HumanDownCommit.Clip=Cast<UAnimSequence>(Instance->GetCurrentAsset());
            HumanDownCommit.Body=GetMesh()->GetSkeletalMeshAsset();
            HumanDownCommit.SampleSeconds=Instance->GetCurrentTime();
            HumanDownCommit.Duration=HumanInfectionDuration;
            HumanDownCommit.PoseCommitFrame=HumanPresentation->GetPoseCommitFrame();
            HumanDownCommit.PoseCommitCount=HumanPresentation->GetPoseCommitCount();
            UE_LOG(LogTemp,Display,TEXT("BIO_NPC_INFECTION_DOWN_COMPLETE id=%d clip=%s age=%.6f duration=%.6f commits=%u"),ParticipantId,*HumanInfectionDown->GetPathName(),HumanInfectionAge,HumanInfectionDuration,HumanPresentation->GetPoseCommitCount());
            ClearHumanInfectionTransition();ApplyTeamPresentation(EHeliosBioTeam::Zombie,true);DecisionRemaining=0.f;
            return;
        }
    }
    HumanInfectionAge=FMath::Min(HumanInfectionDuration,HumanInfectionAge+FMath::Max(0.f,Dt));
    HumanInfectionQueuedCommitCount=HumanPresentation->GetPoseCommitCount();HumanInfectionQueuedFrame=GFrameCounter;
    if(!HumanPresentation->PresentAction(TEXT("Down"),HumanInfectionAge,false))
    {BlockHumanInfectionTransition(TEXT("original_tp_down_pose_rejected"));return;}
    bHumanInfectionPosePending=true;
}
void AHeliosBioNPC::ApplyTeamPresentation(EHeliosBioTeam Team,bool Force)
{
    if(bHumanInfectionTransition)return;
    auto* RoleMode=AHeliosBioHotelGameMode::Find(GetWorld());const auto* RoleP=RoleMode?RoleMode->Model.FindParticipant(ParticipantId):nullptr;
    const FName RoleId=RoleP?(Team==EHeliosBioTeam::Zombie?RoleP->ZombieRoleId:RoleP->HunterRoleId):NAME_None;
    if(!Force&&LastTeam==Team&&LastRoleId==RoleId)return;
    if(LastTeam!=Team){ResetFloatingAction();HeliosBioPlatformTraversal::ForgetBot(this);bPlatformTraversalActive=false;}
    LastRoleId=RoleId;
    FourSlash->Cancel();
    LastTeam=Team;BioSkillStage=0;AttackAge=100;CurrentBioClip=nullptr;bPlayingTransientPose=false;
    ActiveRoleArt=nullptr;RolePrimaryIdle=nullptr;RolePrimaryFire=nullptr;bRoleRangedReady=false;bRolePrimaryFiring=false;bRolePrimaryBodyPose=false;bRolePrimaryWeaponPose=false;bRolePrimaryBodyActive=false;
    RoleWeaponVisual->SetVisibility(false,true);RoleWeaponVisual->SetLeaderPoseComponent(nullptr);RoleWeaponVisual->SetSkeletalMesh(nullptr);
    SerumWeaponVisual->SetVisibility(false);
    if(Weapon){Weapon->StopFiring();Weapon->CancelReload();}
    ReloadRemaining=0;Target.Reset();
    auto* M=AHeliosBioHotelGameMode::Find(GetWorld());if(!M)return;
    if(Team==EHeliosBioTeam::Human)
    {
        Tags.AddUnique(TEXT("Friendly"));
        if(RoundInventory->IsEnabled()&&RoundInventory->HasSlot(0))
        {
            RestoreHumanMeshTransform();HumanPresentation->SetComponentTickEnabled(true);
            if(!RoundInventory->Restore()){GetMesh()->SetVisibility(false);UE_LOG(LogTemp,Error,TEXT("BIO_NPC_INVENTORY_RESTORE_FAILED id=%d"),ParticipantId);}
            return;
        }
        if(Weapon){Weapon->Destroy();Weapon=nullptr;}
        RestoreHumanMeshTransform();
        BioIdle=nullptr;BioRun=nullptr;BioAttack=nullptr;BioDeath=nullptr;BioHeavy=nullptr;BioSkill=nullptr;BioSkillLoop=nullptr;BioSkillEnd=nullptr;BioHit=nullptr;
        if(!bLoadoutInitialized)InitializeBioParticipant(ParticipantId,1337+M->RoundSerial*103,M->SelectedLoadout);
        if(!HumanPresentation->ApplyLoadout(GetMesh(),HumanLoadout))
        {
            UE_LOG(LogTemp,Error,TEXT("BIO_NPC_HUMAN_PRESENTATION_FAILED id=%d character=%s form=%s weapon=%s"),ParticipantId,*HumanLoadout.CharacterId.ToString(),*HumanLoadout.CharacterVariantId.ToString(),*HumanLoadout.PrimaryWeaponId.ToString());
            bool Recovered=false;
            for(const auto& Candidate:BioHumanRoster())if(HumanPresentation->ApplyLoadout(GetMesh(),Candidate)){HumanLoadout=Candidate;Recovered=true;break;}
            if(!Recovered){GetMesh()->SetVisibility(false);return;}
        }
        const auto* D=UHeliosWeaponRegistry::FindWeapon(HumanLoadout.PrimaryWeaponId);
        if(!D)return;
        // The previous zombie/hunter may have used a differently scaled mesh.
        RestoreHumanMeshTransform();
        HumanPresentation->SetComponentTickEnabled(true);HumanPresentation->SetVisible(true);MagazineSize=D->MagazineSize;ReloadDuration=D->ReloadSeconds;
        FActorSpawnParameters Params;Params.Owner=this;Params.Instigator=this;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Weapon=GetWorld()->SpawnActor<AShooterWeapon>(D->WeaponClass,GetActorTransform(),Params);
        if(Weapon)
        {
            if(auto* W=Cast<AHeliosCombatWeapon>(Weapon))W->ApplyDefinition(D);
            Weapon->ConfigureMagazine(MagazineSize);LastAmmo=MagazineSize;Weapon->ActivateWeapon(TEXT("Friendly"));
            AttachWeaponMeshes(Weapon);
            RoundInventory->RegisterBaseline(Weapon,HumanLoadout,ReserveAmmo,MagazineSize,ReloadDuration);
        }
        return;
    }
    Tags.Remove(TEXT("Friendly"));HumanPresentation->SetComponentTickEnabled(false);HumanPresentation->SetVisible(false);
    if(RoundInventory->IsEnabled()&&RoundInventory->HasSlot(0))RoundInventory->Suspend();
    else if(Weapon){Weapon->Destroy();Weapon=nullptr;}
    const auto* P=M->Model.FindParticipant(ParticipantId);auto* Art=P?HeliosBioArt::Load(*P):nullptr;
    if(Art)
    {
        ActiveRoleArt=Art;
        GetMesh()->SetSkeletalMesh(Art->ThirdPersonMesh.LoadSynchronous());
        GetMesh()->SetRelativeTransform(RoleMeshTransformForCapsule(Art->ThirdPersonTransform));
        GetMesh()->SetComponentTickEnabled(true);
        GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        GetMesh()->SetVisibility(true);
        BioIdle=Art->TPIdle.LoadSynchronous();BioRun=Art->TPRun.LoadSynchronous();BioAttack=Art->TPAttack.LoadSynchronous();BioDeath=Art->TPDeath.LoadSynchronous();
        BioHeavy=Art->TPHeavy.LoadSynchronous();BioSkill=Art->TPSkill.LoadSynchronous();BioSkillLoop=Art->TPSkillLoop.LoadSynchronous();BioSkillEnd=Art->TPSkillEnd.LoadSynchronous();BioHit=Art->TPHit.LoadSynchronous();
        BioRunBackward=Art->TPRunBackward.LoadSynchronous();BioRunLeft=Art->TPRunLeft.LoadSynchronous();BioRunRight=Art->TPRunRight.LoadSynchronous();
        if(Art->Ability==EHeliosBioAbility::MechanicalFire&&Art->bIdentityVerified&&P&&M->Model.Config.AllowedHunterRoles.Contains(P->HunterRoleId)&&M->IsParticipantSkillImplemented(ParticipantId))
        {
            auto* Gun=Art->PrimaryThirdPersonWeaponMesh.LoadSynchronous();RolePrimaryIdle=Art->TPPrimaryIdle.LoadSynchronous();RolePrimaryFire=Art->TPPrimaryFire.LoadSynchronous();
            auto* Body=Art->bPrimaryThirdPersonIsBody?Gun:GetMesh()->GetSkeletalMeshAsset();
            bRolePrimaryBodyPose=Body&&RolePrimaryIdle&&RolePrimaryFire&&RolePrimaryIdle->GetSkeleton()==Body->GetSkeleton()&&RolePrimaryFire->GetSkeleton()==Body->GetSkeleton();
            bRolePrimaryWeaponPose=Gun&&RolePrimaryIdle&&RolePrimaryFire&&RolePrimaryIdle->GetSkeleton()==Gun->GetSkeleton()&&RolePrimaryFire->GetSkeleton()==Gun->GetSkeleton();
            bool FollowsBody=Gun&&Body;
            if(FollowsBody)for(int32 Bone=0;Bone<Gun->GetRefSkeleton().GetNum();++Bone)
                if(GetMesh()->GetBoneIndex(Gun->GetRefSkeleton().GetBoneName(Bone))==INDEX_NONE){FollowsBody=false;break;}
            bRoleRangedReady=Gun&&(bRolePrimaryBodyPose||bRolePrimaryWeaponPose)&&(Art->bPrimaryThirdPersonIsBody||FollowsBody||bRolePrimaryWeaponPose);
            if(bRoleRangedReady)
            {
                // Some real source avatars expose the firearm through their own
                // body rig. Never render a second copy of that entire body.
                if(!Art->bPrimaryThirdPersonIsBody&&Gun!=GetMesh()->GetSkeletalMeshAsset())
                {
                    RoleWeaponVisual->SetSkeletalMesh(Gun);RoleWeaponVisual->SetRelativeTransform(Art->PrimaryWeaponTransform);RoleWeaponVisual->SetVisibility(true);
                    if(FollowsBody&&bRolePrimaryBodyPose)RoleWeaponVisual->SetLeaderPoseComponent(GetMesh());
                    else RoleWeaponVisual->PlayAnimation(RolePrimaryIdle,true);
                }
            }
            else UE_LOG(LogTemp,Error,TEXT("BIO_MECH_TP_GEAR_RIG_UNAVAILABLE id=%d body_pose=%d weapon_pose=%d"),ParticipantId,bRolePrimaryBodyPose,bRolePrimaryWeaponPose);
        }
        if(BioIdle)GetMesh()->PlayAnimation(BioIdle,true);
        const bool Wukong=Art->SourceIdentity.Contains(TEXT("SunWuKong"),ESearchCase::IgnoreCase),BaiGu=Art->SourceIdentity.Contains(TEXT("BaiGuJing"),ESearchCase::IgnoreCase);
        if(Wukong||BaiGu)AHeliosSSXEffect::Spawn(this,Wukong?TEXT("FX_3P_SunWuKong_Energy"):TEXT("FX_3P_BaiGuJing_Energy"),GetActorLocation()-FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),GetActorRotation(),false,1.f,TEXT("/Game/Helios/CFMBioUltimateEffects/Effects/DA_ULTFX_Effects.DA_ULTFX_Effects"));
    }
}
bool AHeliosBioNPC::BeginBioFourSlash()
{
    if(bHumanInfectionTransition)return false;
    auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=Mode?Mode->Model.FindParticipant(ParticipantId):nullptr;
    auto* RoleArt=P?HeliosBioArt::Load(*P):nullptr;
    if(!P||P->FrozenSeconds>0||MeleeCooldown>0||!FourSlash->BeginSourceSequence(ParticipantId,RoleArt,GetMesh(),false))return false;
    ++MeleeAttacks;BioSkillStage=0;CurrentBioClip=nullptr;AttackAge=0;
    bPlayingTransientPose=false;
    return true;
}
void AHeliosBioNPC::ResetForBioRound()
{
    ResetFloatingAction();
    ClearHumanInfectionTransition();
    HumanDownCommit=FHeliosBioHumanDownCommit{};
    AHeliosBioStaticJumpLink::Forget(this,true);
    if(bWasFrozen){GetCharacterMovement()->MaxAcceleration=FrozenSavedAcceleration;bWasFrozen=false;}
    HeliosBioPlatformTraversal::ForgetBot(this);bPlatformTraversalActive=false;FourSlash->Cancel();bIsDead=false;bLastAlive=true;Tags.Remove(TEXT("Dead"));SetActorHiddenInGame(false);
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    MeleeCooldown=0;DecisionRemaining=0;ReserveAmmo=600;ReloadRemaining=0;Target.Reset();bQAFreeze=false;bQAHoldDecisions=false;
    HoldBioPosition(TEXT("Spawn"));GetCharacterMovement()->StopMovementImmediately();GetCharacterMovement()->SetAvoidanceEnabled(true);bNavigationReconnect=false;bDefenseGoalValid=false;BlockedSeconds=0;RecoveryRemaining=0;
    LastProgressPosition=GetActorLocation();MoveRequestAge=100.f;
    if(RoundInventory->IsEnabled()&&RoundInventory->HasSlot(0))RoundInventory->ResetRound();
    ApplyTeamPresentation(EHeliosBioTeam::Human,true);
    if(auto* M=AHeliosBioHotelGameMode::Find(GetWorld()))SetActorLocation(M->SpawnLocation(ParticipantId),false,nullptr,ETeleportType::TeleportPhysics);
}
void AHeliosBioNPC::AddSupplyAmmo(){if(RoundInventory->IsEnabled()&&RoundInventory->HasSlot(0))RoundInventory->AddBaselineAmmo();else ReserveAmmo=FMath::Min(1800,ReserveAmmo+300);}
void AHeliosBioNPC::PlayRoleSkillPresentation(bool Secondary)
{
    if(!ActiveRoleArt||FourSlash->IsBusy())return;
    UAnimSequence* Clip=Secondary?ActiveRoleArt->TPSecondarySkill.LoadSynchronous():BioSkill.Get();
    if(Clip&&GetMesh()->GetSkeletalMeshAsset()&&Clip->GetSkeleton()==GetMesh()->GetSkeletalMeshAsset()->GetSkeleton())
    {BioSkillStage=Secondary?4:1;CurrentBioClip=Clip;AttackAge=0;bPlayingTransientPose=true;GetMesh()->PlayAnimation(Clip,false);}
    const FString& Effect=Secondary?ActiveRoleArt->SecondaryEffect:ActiveRoleArt->ThirdPersonSkillEffect;
    auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=Mode?Mode->Model.FindParticipant(ParticipantId):nullptr;
    if(!Effect.IsEmpty()&&!ActiveRoleArt->SkillEffectLibrary.IsEmpty())
    {
        const FVector Feet=GetActorLocation()-FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        if(auto* FX=AHeliosSSXEffect::Spawn(this,Effect,Feet,GetActorRotation(),false,1.f,ActiveRoleArt->SkillEffectLibrary))
            FX->SetLifeSpan(FMath::Max(.1f,Secondary?(P&&P->SecondarySkillRemaining>0?P->SecondarySkillRemaining:Clip?Clip->GetPlayLength():1.f):P?P->SkillRemaining:1.f));
    }
    else if(!Secondary&&ActiveRoleArt->SourceIdentity.Contains(TEXT("FamilyHumanHero"),ESearchCase::IgnoreCase))
    {
        if(auto* FX=AHeliosSSXEffect::Spawn(this,TEXT("FX_3P_FamilyHumanHero_Buff"),GetActorLocation()-FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),GetActorRotation(),false,1.f,TEXT("/Game/Helios/CFMBioEffects/Effects/DA_BIOFX_Effects.DA_BIOFX_Effects")))FX->SetLifeSpan(FMath::Max(.1f,P?P->SkillRemaining:1.f));
    }
}
void AHeliosBioNPC::SetRolePrimaryPresentation(bool Enabled)
{
    if(!ActiveRoleArt||!bRoleRangedReady)return;
    const bool WholeBody=ActiveRoleArt->bPrimaryThirdPersonIsBody||ActiveRoleArt->PrimaryThirdPersonWeaponMesh.LoadSynchronous()==ActiveRoleArt->ThirdPersonMesh.LoadSynchronous();
    if(WholeBody)
    {
        RoleWeaponVisual->SetVisibility(false);
        if(bRolePrimaryBodyActive==Enabled)return;
        GetMesh()->SetSkeletalMesh((Enabled?ActiveRoleArt->PrimaryThirdPersonWeaponMesh:ActiveRoleArt->ThirdPersonMesh).LoadSynchronous());
        GetMesh()->SetRelativeTransform(RoleMeshTransformForCapsule(Enabled?ActiveRoleArt->PrimaryThirdPersonTransform:ActiveRoleArt->ThirdPersonTransform));bRolePrimaryBodyActive=Enabled;
        CurrentBioClip=nullptr;bPlayingTransientPose=false;BioSkillStage=0;
        if(auto* Clip=Enabled?RolePrimaryIdle.Get():BioIdle.Get())GetMesh()->PlayAnimation(Clip,true);
    }
    else RoleWeaponVisual->SetVisibility(Enabled);
}
bool AHeliosBioNPC::UseBioSkill(bool Secondary)
{
    if(bHumanInfectionTransition)return false;
    auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());if(!Mode||FourSlash->IsBusy())return false;
    if(BioSkillStage>0&&CurrentBioClip&&AttackAge<CurrentBioClip->GetPlayLength())return false;
    const auto* CasterState=Mode->Model.FindParticipant(ParticipantId);
    if(Secondary&&CasterState&&CasterState->HunterRoleId==TEXT("BioRedeemerHunter"))return FireSerumAt(Target.Get());
    SetRolePrimaryPresentation(false);
    const bool Began=Secondary?Mode->UseParticipantSecondarySkill(ParticipantId):Mode->UseParticipantSkill(ParticipantId);
    if(Began)PlayRoleSkillPresentation(Secondary);return Began;
}
bool AHeliosBioNPC::FireSerumAt(AActor* AimTarget)
{
    auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=Mode?Mode->Model.FindParticipant(ParticipantId):nullptr;
    const auto* Victim=Mode?Mode->Model.FindParticipant(Mode->ParticipantId(AimTarget)):nullptr;
    FVector AnchorReferenceScale=FVector::ZeroVector;
    const auto Reject=[&](const TCHAR* Reason)
    {
        if(Mode)Mode->LastSerumDiagnostic=FString::Printf(TEXT("NPC_reject caster=%d target=%d reason=%s team=%d role=%s alive=%d cooldown=%.3f frozen=%.3f slash=%d infection_transition=%d melee_cooldown=%.3f stage=%d attack_age=%.3f clip=%s art=%s"),ParticipantId,Victim?Victim->Id:INDEX_NONE,Reason,P?int32(P->Team):-1,P?*P->HunterRoleId.ToString():TEXT(""),P&&P->bAlive,P?P->SecondaryCooldownRemaining:-1.f,P?P->FrozenSeconds:-1.f,FourSlash->IsBusy(),bHumanInfectionTransition,MeleeCooldown,BioSkillStage,AttackAge,*GetPathNameSafe(CurrentBioClip),*GetPathNameSafe(ActiveRoleArt));
        if(Mode)Mode->LastSerumDiagnostic+=FString::Printf(TEXT(" actor_rotation=%s body_relative=%s body_world=%s hand_world=%s anchor_reference_scale=%s gun_world=%s actual_mesh_clip=%s"),*GetActorRotation().ToString(),*GetMesh()->GetRelativeTransform().ToString(),*GetMesh()->GetComponentTransform().ToString(),*GetMesh()->GetSocketTransform(TEXT("b_RightWeapon")).ToString(),*AnchorReferenceScale.ToString(),*SerumWeaponVisual->GetComponentTransform().ToString(),*GetPathNameSafe(GetMesh()->GetSingleNodeInstance()?GetMesh()->GetSingleNodeInstance()->GetCurrentAsset():nullptr));
        return false;
    };
    if(!P||!Victim||!P->bAlive||!Victim->bAlive||AimTarget==this||P->HunterRoleId!=TEXT("BioRedeemerHunter")||P->Team!=EHeliosBioTeam::UltimateHunter||P->SecondaryCooldownRemaining>0||P->FrozenSeconds>0||FourSlash->IsBusy()||bHumanInfectionTransition||MeleeCooldown>0||!ActiveRoleArt||!ActiveRoleArt->bSerumSourceVerified)return Reject(TEXT("caster_or_target_state"));
    if(Victim->Team!=EHeliosBioTeam::Zombie&&(!Victim->IsHunter()||Victim->Health>=Victim->MaxHealth))return Reject(TEXT("target_needs_no_serum"));
    if(BioSkillStage>0&&CurrentBioClip&&AttackAge<CurrentBioClip->GetPlayLength())return Reject(TEXT("native_action_busy"));
    auto* Gun=ActiveRoleArt->SecondaryThirdPersonWeaponMesh.LoadSynchronous();auto* Fire=ActiveRoleArt->TPSecondarySkill.LoadSynchronous();auto* Idle=ActiveRoleArt->TPSecondaryIdle.LoadSynchronous();
    if(!Gun||!Fire||!Idle||!GetMesh()->GetSkeletalMeshAsset()||!GetMesh()->DoesSocketExist(TEXT("b_RightWeapon"))||Fire->GetSkeleton()!=GetMesh()->GetSkeletalMeshAsset()->GetSkeleton()||Idle->GetSkeleton()!=GetMesh()->GetSkeletalMeshAsset()->GetSkeleton())return Reject(TEXT("native_gun_clip_or_body_binding"));
    const auto& Reference=GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton();
    const int32 AnchorIndex=Reference.FindBoneIndex(TEXT("b_RightWeapon"));
    if(AnchorIndex==INDEX_NONE)return Reject(TEXT("native_gun_reference_anchor_unavailable"));
    FTransform AnchorReference=FTransform::Identity;
    for(int32 Index=AnchorIndex;Index!=INDEX_NONE;Index=Reference.GetParentIndex(Index))AnchorReference=AnchorReference*Reference.GetRefBonePose()[Index];
    AnchorReferenceScale=AnchorReference.GetScale3D();
    if(AnchorReferenceScale.ContainsNaN()||AnchorReferenceScale.GetMin()<=SMALL_NUMBER)return Reject(TEXT("native_gun_reference_units_invalid"));
    // The independent static gun is already in centimetres. Its imported body
    // anchor contains an FBX reference-unit scale (100 in the measured source).
    // Cancel only that reference scale, retaining body/actor normalization and
    // any genuine animated scale relative to the reference pose.
    const FTransform ReferenceUnits(FQuat::Identity,FVector::ZeroVector,FVector::OneVector/AnchorReferenceScale);
    const FTransform NativeGunAttachment=ActiveRoleArt->SecondaryThirdPersonTransform*ReferenceUnits;
    const FVector Direction=(AimTarget->GetActorLocation()-GetActorLocation()).GetSafeNormal();
    if(FVector::DotProduct(GetActorForwardVector(),Direction)<.55f)return Reject(TEXT("target_outside_forward_cone"));
    // Sample the original serum holding pose, as the player path does. The
    // knife's idle hand position is not the source gun's firing origin.
    auto* PreviousNode=GetMesh()->GetSingleNodeInstance();UAnimationAsset* PreviousAsset=PreviousNode?PreviousNode->GetCurrentAsset():nullptr;
    const float PreviousSeconds=PreviousNode?PreviousNode->GetCurrentTime():0.f,PreviousRate=PreviousNode?PreviousNode->GetPlayRate():1.f;
    const bool PreviousLoop=PreviousNode&&PreviousNode->IsLooping();bool KeepSerumPose=false;
    ON_SCOPE_EXIT
    {
        if(!KeepSerumPose&&PreviousAsset)
        {
            GetMesh()->PlayAnimation(PreviousAsset,PreviousLoop);
            if(auto* Node=GetMesh()->GetSingleNodeInstance()){Node->SetPosition(PreviousSeconds,false);Node->SetPlayRate(PreviousRate);}
            GetMesh()->TickAnimation(0,false);GetMesh()->RefreshBoneTransforms();
        }
    };
    GetMesh()->PlayAnimation(Idle,true);GetMesh()->TickAnimation(0,false);GetMesh()->RefreshBoneTransforms();
    SerumWeaponVisual->SetStaticMesh(Gun);SerumWeaponVisual->SetRelativeTransform(NativeGunAttachment);SerumWeaponVisual->SetVisibility(true);SerumWeaponVisual->UpdateComponentToWorld();
    FVector Muzzle;
    if(!Mode->GetParticipantSerumMuzzle(ParticipantId,Muzzle)){SerumWeaponVisual->SetVisibility(false);return Reject(TEXT("actual_source_muzzle_unavailable"));}
    const FVector AimPoint=AimTarget->GetActorLocation()+FVector(0,0,25);
    FHitResult Obstruction;FCollisionQueryParams Query(SCENE_QUERY_STAT(BioNPCSerumAim),false,this);
    if(GetWorld()->LineTraceSingleByChannel(Obstruction,Muzzle,AimPoint,ECC_Visibility,Query)&&Obstruction.GetActor()!=AimTarget)
    {
        SerumWeaponVisual->SetVisibility(false);Reject(TEXT("muzzle_to_target_obstructed"));
        Mode->LastSerumDiagnostic+=FString::Printf(TEXT(" blocker=%s actor=%s body_hand=%s gun=%s muzzle=%s aim=%s hit=%s normal=%s initial_overlap=%d hit_distance=%.3f"),*GetPathNameSafe(Obstruction.GetComponent()),*GetActorLocation().ToString(),*GetMesh()->GetSocketLocation(TEXT("b_RightWeapon")).ToString(),*SerumWeaponVisual->GetComponentLocation().ToString(),*Muzzle.ToString(),*AimPoint.ToString(),*Obstruction.ImpactPoint.ToString(),*Obstruction.ImpactNormal.ToString(),Obstruction.bStartPenetrating,Obstruction.Distance);
        return false;
    }
    if(!Mode->FireParticipantSerum(ParticipantId,Muzzle,(AimPoint-Muzzle).GetSafeNormal())){SerumWeaponVisual->SetVisibility(false);Reject(TEXT("shared_serum_launch_rejected"));Mode->LastSerumDiagnostic+=TEXT(" actual_muzzle=")+Muzzle.ToString();return false;}
    Mode->LastSerumDiagnostic+=FString::Printf(TEXT(" anchor_reference_scale=%s actual_body_world=%s actual_anchor_world=%s actual_gun_world=%s actual_muzzle_to_hand_cm=%.6f"),*AnchorReferenceScale.ToString(),*GetMesh()->GetComponentTransform().ToString(),*GetMesh()->GetSocketTransform(TEXT("b_RightWeapon")).ToString(),*SerumWeaponVisual->GetComponentTransform().ToString(),FVector::Dist(Muzzle,GetMesh()->GetSocketLocation(TEXT("b_RightWeapon"))));
    KeepSerumPose=true;BioSkillStage=4;CurrentBioClip=Fire;AttackAge=0;MeleeCooldown=Fire->GetPlayLength();bPlayingTransientPose=true;GetMesh()->PlayAnimation(Fire,false);return true;
}
bool AHeliosBioNPC::FireRoleWeaponAt(AActor* AimTarget)
{
    if(bHumanInfectionTransition)return false;
    auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());
    if(!Mode||!IsValid(AimTarget)||!Mode->AreOpponents(ParticipantId,Mode->ParticipantId(AimTarget)))return false;
    Target=AimTarget;bRolePrimaryFiring=true;return TickRoleWeapon();
}
bool AHeliosBioNPC::TickRoleWeapon()
{
    if(bHumanInfectionTransition)return false;
    if(!bRoleRangedReady||!bRolePrimaryFiring||!ActiveRoleArt||!Target.IsValid()||FourSlash->IsBusy())return false;
    auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=Mode?Mode->Model.FindParticipant(ParticipantId):nullptr;
    if(!P||!P->bAlive||P->FrozenSeconds>0||bQAFreeze||Mode->Model.GetPhase()!=EHeliosBioPhase::Active)return false;
    if(BioSkillStage>0&&CurrentBioClip&&AttackAge<CurrentBioClip->GetPlayLength())return false;
    if(MeleeCooldown>0)return false;
    FVector Eyes;FRotator Aim;GetActorEyesViewPoint(Eyes,Aim);FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BioNPCXM214LOS),false,this);
    if(GetWorld()->LineTraceSingleByChannel(Hit,Eyes,Target->GetActorLocation()+FVector(0,0,25),ECC_Visibility,Query)&&Hit.GetActor()!=Target.Get())return false;
    SetRolePrimaryPresentation(true);
    if(!Mode->FireParticipantRoleWeapon(ParticipantId,Eyes,Aim.Vector()))return false;
    ++ShotsFired;++AttackCount;AttackAge=0;BioSkillStage=0;
    if(bRolePrimaryBodyPose&&RolePrimaryFire)
    {CurrentBioClip=RolePrimaryFire;bPlayingTransientPose=true;GetMesh()->PlayAnimation(RolePrimaryFire,false);}
    if(bRolePrimaryWeaponPose&&RoleWeaponVisual->IsVisible()&&!RoleWeaponVisual->LeaderPoseComponent.IsValid())RoleWeaponVisual->PlayAnimation(RolePrimaryFire,false);
    return true;
}
float AHeliosBioNPC::TakeDamage(float D,const FDamageEvent& E,AController* I,AActor* C)
{
    auto* M=AHeliosBioHotelGameMode::Find(GetWorld());if(!M)return 0;
    const int32 Attacker=M->ParticipantId(I?I->GetPawn():C);
    if(!M->DamageParticipant(ParticipantId,Attacker,D,false))return 0;
    if(const auto* P=M->Model.FindParticipant(ParticipantId)){CurrentHP=P->Health;bIsDead=!P->bAlive;if(!bHumanInfectionTransition&&P->bAlive&&P->Team!=EHeliosBioTeam::Human&&BioHit&&!FourSlash->IsBusy()){SetRolePrimaryPresentation(false);CurrentBioClip=BioHit;AttackAge=0;BioSkillStage=0;bPlayingTransientPose=true;GetMesh()->PlayAnimation(BioHit,false);}}
    return D;
}
void AHeliosBioNPC::MeleeAttack()
{
    if(bHumanInfectionTransition)return;
    auto* M=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=M?M->Model.FindParticipant(ParticipantId):nullptr;
    if(!P||!P->bAlive||P->FrozenSeconds>0||!Target.IsValid()||MeleeCooldown>0||FourSlash->IsBusy())return;
    if(BioSkillStage>0&&CurrentBioClip&&AttackAge<CurrentBioClip->GetPlayLength())return;
    if(auto* Turret=Cast<AHeliosBioTurret>(Target.Get());Turret&&!Turret->IsActiveTurret())return;
    auto* Art=ActiveRoleArt.Get();
    const bool Heavy=BioHeavy&&(P->bHeavySwordWaveReady||MeleeAttacks%3==2);
    const bool WantsWave=Art&&Art->Ability==EHeliosBioAbility::ZhongXiaoKuiBlade&&P->bGhostMaskActive&&(P->bFirstAttackSwordWave||(Heavy&&P->bHeavySwordWaveReady));
    const float Range=WantsWave?Art->SecondaryRange:P->IsHunter()?265.f:180.f;
    if(FVector::Dist(GetActorLocation(),Target->GetActorLocation())>Range)return;
    FVector View;FRotator Aim;GetActorEyesViewPoint(View,Aim);
    FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(BioNPCMelee),false,this);
    if(GetWorld()->LineTraceSingleByChannel(Hit,View,Target->GetActorLocation()+FVector(0,0,25),ECC_Visibility,Q)&&Hit.GetActor()!=Target.Get())return;
    const bool WaveAttack=WantsWave&&M->FireParticipantSwordWave(ParticipantId,View,Aim.Vector(),Heavy);
    if(WantsWave&&!WaveAttack)return;
    SetRolePrimaryPresentation(false);UAnimSequence* Strike=Heavy?BioHeavy.Get():BioAttack.Get();
    ++MeleeAttacks;++AttackCount;BioSkillStage=0;MeleeCooldown=Strike?FMath::Max(.35f,Strike->GetPlayLength()):1.05f;AttackAge=0;
    if(Strike){GetMesh()->PlayAnimation(Strike,false);CurrentBioClip=Strike;bPlayingTransientPose=true;}
    const float Damage=P->IsHunter()?(Heavy?2200.f:1400.f):M->Model.Config.ZombieMeleeDamage;
    if(!WaveAttack)
    {
        if(auto* Clone=Cast<AHeliosBioClone>(Target.Get()))Clone->ApplyBioDamage(ParticipantId,Damage);
        else if(auto* Turret=Cast<AHeliosBioTurret>(Target.Get()))Turret->ApplyBioDamage(ParticipantId,Damage);
        else M->DamageParticipant(M->ParticipantId(Target.Get()),ParticipantId,Damage,P->IsHunter());
    }
    if(Art){if(auto* Sound=(Heavy?Art->HeavySound:Art->AttackSound).LoadSynchronous())UGameplayStatics::PlaySoundAtLocation(this,Sound,GetActorLocation(),.6f);}
}
void AHeliosBioNPC::Think()
{
    if(bHumanInfectionTransition)return;
    auto* M=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=M?M->Model.FindParticipant(ParticipantId):nullptr;
    auto* AI=Cast<AAIController>(Controller);if(!M||!P||!AI||!P->bAlive)return;
    if(P->FrozenSeconds>0){bRolePrimaryFiring=false;if(Weapon)Weapon->StopFiring();HoldBioPosition(TEXT("Frozen"));return;}
    if(M->Model.GetPhase()==EHeliosBioPhase::RoundOver){bRolePrimaryFiring=false;SetRolePrimaryPresentation(false);if(Weapon)Weapon->StopFiring();HoldBioPosition(TEXT("RoundOver"));return;}
    if(AHeliosBioStaticJumpLink::IsTraversing(this)){bRolePrimaryFiring=false;if(Weapon)Weapon->StopFiring();return;}
    if(M->Model.GetPhase()==EHeliosBioPhase::Preparation)
    {
        bRolePrimaryFiring=false;if(Weapon)Weapon->StopFiring();Target.Reset();AI->ClearFocus(EAIFocusPriority::Gameplay);
        GetCharacterMovement()->MaxWalkSpeed=400.f;GetCharacterMovement()->bOrientRotationToMovement=true;GetCharacterMovement()->bUseControllerDesiredRotation=false;
        if(RecoveryRemaining>0)return;
        if(!bDefenseGoalValid&&!ChooseDefenseGoal()){HoldBioPosition(TEXT("NoDefensePath"));return;}
        if(FVector::DistSquared2D(GetNavAgentLocation(),DefenseGoal)>FMath::Square(100.f))RequestBioMove(DefenseGoal,70.f,TEXT("PrepareDefense"));
        else HoldBioPosition(TEXT("Defend"));
        return;
    }
    if(M->Model.CanTransform(ParticipantId))M->TransformParticipant(ParticipantId);
    P=M->Model.FindParticipant(ParticipantId);
    AActor* Nearest=nullptr;float BestScore=FLT_MAX;
    auto Consider=[&](AActor* Actor)
    {
        if(!IsValid(Actor)||(AvoidTargetRemaining>0&&Actor==AvoidTarget.Get()))return;
        const FVector Delta=Actor->GetActorLocation()-GetActorLocation();
        // Prefer nearby opponents on the same floor, while leaving elevated
        // targets available to the real platform traversal helper.
        const float Score=Delta.SizeSquared()+FMath::Square(FMath::Max(0.f,FMath::Abs(Delta.Z)-120.f))*1.5f;
        if(Score<BestScore){BestScore=Score;Nearest=Actor;}
    };
    for(const auto& Other:M->Model.GetParticipants())
    {if(Other.bAlive&&M->AreOpponents(ParticipantId,Other.Id))Consider(M->ParticipantActor(Other.Id));}
    if(P->IsHumanSide())for(auto Clone:M->Clones)if(IsValid(Clone)&&Clone->IsActiveClone()&&M->AreOpponents(ParticipantId,Clone->GetMotherId()))
    {Consider(Clone);}
    if(P->Team==EHeliosBioTeam::Zombie)for(auto Turret:M->Turrets)if(IsValid(Turret)&&Turret->IsActiveTurret()&&M->AreOpponents(ParticipantId,Turret->GetCasterId()))Consider(Turret);
    Target=Nearest;
    if(P->HunterRoleId==TEXT("BioRedeemerHunter")&&P->SecondaryCooldownRemaining<=0&&ActiveRoleArt&&ActiveRoleArt->bSerumSourceVerified)
    {
        AActor* HurtHunter=nullptr;float BestHealthFraction=1.f;
        for(const auto& Ally:M->Model.GetParticipants())if(Ally.Id!=ParticipantId&&Ally.bAlive&&Ally.IsHunter()&&Ally.Health/Ally.MaxHealth<BestHealthFraction)
            if(auto* Actor=M->ParticipantActor(Ally.Id);Actor&&FVector::DistSquared(Actor->GetActorLocation(),GetActorLocation())<FMath::Square(1400.f)){HurtHunter=Actor;BestHealthFraction=Ally.Health/Ally.MaxHealth;}
        if((HurtHunter&&FireSerumAt(HurtHunter))||(Nearest&&FireSerumAt(Nearest)))return;
    }
    if(!Nearest)
    {
        bRolePrimaryFiring=false;SetRolePrimaryPresentation(false);if(Weapon)Weapon->StopFiring();AI->ClearFocus(EAIFocusPriority::Gameplay);
        if(M->bFloatingProfile){ThinkFloatingMovement(nullptr,false);return;}
        if(!bPlatformTraversalActive&&RecoveryRemaining<=0)
        {
            if(!bDefenseGoalValid)ChooseDefenseGoal();
            if(bDefenseGoalValid&&FVector::DistSquared2D(GetNavAgentLocation(),DefenseGoal)>FMath::Square(120.f))RequestBioMove(DefenseGoal,80.f,TEXT("Regroup"));
            else HoldBioPosition(TEXT("Watch"));
        }
        return;
    }
    const float Distance=FVector::Dist(GetActorLocation(),Nearest->GetActorLocation());
    AI->SetFocus(Nearest);GetCharacterMovement()->bOrientRotationToMovement=false;GetCharacterMovement()->bUseControllerDesiredRotation=true;
    GetCharacterMovement()->MaxWalkSpeed=(P->Team==EHeliosBioTeam::Human?400.f:P->IsHunter()?520.f:455.f)*FMath::Max(.1f,P->MovementSkillMultiplier());
    if(P->Team!=EHeliosBioTeam::Human)
    {
        const bool SafeSkillMovement=!M->bFloatingProfile||!bPlatformTraversalActive;
        if(SafeSkillMovement&&!FourSlash->IsBusy()&&P->SkillCooldownRemaining<=0&&Distance<1400)UseBioSkill(false);
        if(SafeSkillMovement&&!FourSlash->IsBusy()&&P->SecondaryCooldownRemaining<=0&&Distance<1400&&ActiveRoleArt&&
            (ActiveRoleArt->Ability==EHeliosBioAbility::MechanicalFire||ActiveRoleArt->Ability==EHeliosBioAbility::ZhongXiaoKuiBlade))UseBioSkill(true);
        MeleeAttack();
        if(!M->bFloatingProfile)
        {
        if(bPlatformTraversalActive||RecoveryRemaining>0||FourSlash->IsBusy())return;
        AHeliosBioSupply* Supply=nullptr;float BestSupply=FMath::Square(3000.f);FVector SupplyGoal;
        const bool WantsRed=P->Team==EHeliosBioTeam::Zombie&&P->bMother&&P->ZombieForm==EHeliosBioZombieForm::Terminator&&
            P->EvolutionLevel>=M->Model.Config.MaximumZombieEvolution&&M->Model.Config.bAbyssSourceAvailable&&!M->Model.Config.AbyssRoleId.IsNone();
        const bool WantsNormal=P->Team==EHeliosBioTeam::Zombie&&(P->Health<P->MaxHealth*.6f||P->EvolutionLevel<M->Model.Config.MaximumZombieEvolution)&&Distance>450.f;
        const bool WantsBlue=P->Team==EHeliosBioTeam::GhostHunter&&!P->bHunterChoicePending&&Distance>350.f;
        if(WantsRed||WantsNormal||WantsBlue)
        {
            const auto DesiredKind=WantsRed?EHeliosBioSupplyKind::RedTerminatorEvolution:WantsBlue?EHeliosBioSupplyKind::BlueHunterUpgrade:EHeliosBioSupplyKind::Normal;
            for(auto S:M->Supplies)if(IsValid(S)&&S->IsAvailable()&&S->GetKind()==DesiredKind)
            {
                const float D=FVector::DistSquared(GetActorLocation(),S->GetActorLocation());FVector At;
                if(D<BestSupply&&FindReachableGoal(S->GetActorLocation(),At)){BestSupply=D;Supply=S;SupplyGoal=At;}
            }
            if(Supply){RequestBioMove(SupplyGoal,65.f,WantsRed?FName(TEXT("RedEvolution")):WantsBlue?FName(TEXT("BlueUpgrade")):FName(TEXT("ZombieSupply")));return;}
        }
        // Disable overlap acceptance: capsule radii must not make the chase stop
        // outside the actual melee distance.
        if(!bRoleRangedReady){RequestBioMove(BioTargetGround(Nearest),P->IsHunter()?140.f:75.f,TEXT("Chase"),true);return;}
        }
    }
    FVector View;FRotator Aim;GetActorEyesViewPoint(View,Aim);
    FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(BioNPCFire),false,this);Q.AddIgnoredActor(Weapon);
    const bool Clear=!GetWorld()->LineTraceSingleByChannel(Hit,View,Nearest->GetActorLocation()+FVector(0,0,25),ECC_Visibility,Q)||Hit.GetActor()==Nearest;
    const auto* NativeSupply=Cast<AHeliosBioSupplyWeapon>(Weapon);
    const bool ThrowBusy=NativeSupply&&NativeSupply->IsNativeThrowBusy();
    if(P->Team==EHeliosBioTeam::Human&&RoundInventory->IsEnabled()&&ReloadRemaining<=0&&!ThrowBusy)
    {
        int32 Preferred=0;
        const auto ReadySlot=[&](int32 Slot){auto* Gun=RoundInventory->GetWeapon(Slot);const auto* Native=Cast<AHeliosBioSupplyWeapon>(Gun);return Gun&&(Gun->GetBulletCount()>0||RoundInventory->GetReserve(Slot)>0)&&(!Native||Native->CanReachNativeTarget(Nearest->GetActorLocation()+FVector(0,0,25)));};
        const auto* Enemy=M->Model.FindParticipant(M->ParticipantId(Nearest));
        if(Clear&&Distance<900&&Enemy&&Enemy->FrozenSeconds<=0&&ReadySlot(3))Preferred=3;
        else if(Clear&&Distance>1000&&Distance<4500&&ReadySlot(2))Preferred=2;
        else if(Clear&&Distance>650&&Distance<3200&&ReadySlot(1))
        {
            int32 Cluster=0;
            for(const auto& Candidate:M->Model.GetParticipants())if(Candidate.bAlive&&M->AreOpponents(ParticipantId,Candidate.Id))
                if(auto* Actor=M->ParticipantActor(Candidate.Id);Actor&&FVector::DistSquared(Actor->GetActorLocation(),Nearest->GetActorLocation())<FMath::Square(550.f))++Cluster;
            Preferred=Cluster>=2?1:ReadySlot(4)?4:0;
        }
        else if(ReadySlot(4))Preferred=4;
        RoundInventory->Select(Preferred);
    }
    bRolePrimaryFiring=bRoleRangedReady&&Clear&&ActiveRoleArt&&Distance<ActiveRoleArt->PrimaryRange;
    if(bRoleRangedReady&&!bRolePrimaryFiring)SetRolePrimaryPresentation(false);
    if(Weapon&&Clear&&Distance<5500&&ReloadRemaining<=0&&Weapon->GetBulletCount()>0)Weapon->StartFiring();else if(Weapon)Weapon->StopFiring();
    if(Weapon&&Weapon->GetBulletCount()==0&&ReserveAmmo>0&&ReloadRemaining<=0&&!ThrowBusy){ReloadRemaining=FMath::Max(.1f,ReloadDuration);Weapon->BeginReload(ReloadRemaining);}
    if(M->bFloatingProfile){ThinkFloatingMovement(Nearest,Clear);return;}
    if(bPlatformTraversalActive||RecoveryRemaining>0)return;
    bool WantsSupplyWeapon=false;
    if(RoundInventory->IsEnabled())for(int32 Slot=1;Slot<5;++Slot)
        if(!RoundInventory->HasSlot(Slot))if(auto* LootDefinition=UHeliosBioSupplyWeaponDefinition::LoadForSlot(Slot);LootDefinition&&LootDefinition->IsReady()){WantsSupplyWeapon=true;break;}
    const int32 BaselineReserve=RoundInventory->IsEnabled()&&RoundInventory->HasSlot(0)?RoundInventory->GetReserve(0):ReserveAmmo;
    if((BaselineReserve<200||P->Health<P->MaxHealth*.6f||WantsSupplyWeapon)&&Distance>500.f)
    {
        AHeliosBioSupply* Supply=nullptr;float BestSupply=FMath::Square(3000.f);FVector SupplyGoal;
        for(auto S:M->Supplies)if(IsValid(S)&&S->IsAvailable()&&S->GetKind()==EHeliosBioSupplyKind::Normal)
        {
            if(auto* Content=S->GetWeaponContent();Content&&RoundInventory->IsEnabled()&&!RoundInventory->CanGrant(Content))continue;
            const float D=FVector::DistSquared(GetActorLocation(),S->GetActorLocation());FVector At;
            if(D<BestSupply&&FindReachableGoal(S->GetActorLocation(),At)){BestSupply=D;Supply=S;SupplyGoal=At;}
        }
        if(Supply){RequestBioMove(SupplyGoal,65.f,TEXT("HumanSupply"));return;}
    }
    if(Distance<850.f)
    {
        FVector Away=(GetActorLocation()-Nearest->GetActorLocation()).GetSafeNormal2D();if(Away.IsNearlyZero())Away=-GetActorForwardVector();
        FVector RetreatGoal;bool FoundRetreat=false;
        const float SideSign=ParticipantId%2?1.f:-1.f;
        // A single point behind a character can be across a wall or off a narrow
        // source ledge. Replan several real reachable escape directions instead
        // of repeatedly holding forever on that failed point.
        for(float Radius:{700.f,450.f,250.f})
        {
            for(float Angle:{0.f,35.f,-35.f,70.f,-70.f,90.f,-90.f})
            {
                const FVector Wanted=GetNavAgentLocation()+Away.RotateAngleAxis(Angle*SideSign,FVector::UpVector)*Radius;
                FVector At;
                if(!FindReachableGoal(Wanted,At)||FVector::DistSquared2D(GetNavAgentLocation(),At)<FMath::Square(120.f))continue;
                if(FVector::Dist2D(At,Nearest->GetActorLocation())<FVector::Dist2D(GetActorLocation(),Nearest->GetActorLocation())+50.f)continue;
                RetreatGoal=At;FoundRetreat=true;break;
            }
            if(FoundRetreat)break;
        }
        if(FoundRetreat)RequestBioMove(RetreatGoal,65.f,TEXT("Retreat"));
        else
        {
            FVector At;
            if(bDefenseGoalValid&&FindReachableGoal(DefenseGoal,At)&&FVector::DistSquared2D(GetNavAgentLocation(),At)>FMath::Square(120.f))RequestBioMove(At,70.f,TEXT("DefenseRetreat"));
            else {HoldBioPosition(TEXT("BlockedRetreat"));LastMoveReason=TEXT("No complete reachable escape path in sampled directions");}
        }
    }
    else if(!Clear||Distance>2800.f)RequestBioMove(BioTargetGround(Nearest),Clear?1200.f:70.f,TEXT("FindFiringLine"),true);
    else if(MovementIntent==TEXT("Reposition")&&bMoveIntentActive&&FVector::DistSquared2D(GetNavAgentLocation(),MovementGoal)>FMath::Square(85.f))
    {RequestBioMove(MovementGoal,60.f,TEXT("Reposition"));}
    else if(CombatRepositionRemaining<=0)
    {
        CombatRepositionRemaining=3.f+float(ParticipantId%5)*.4f;
        const FVector Forward=(Nearest->GetActorLocation()-GetActorLocation()).GetSafeNormal2D();
        const FVector Side=FVector(-Forward.Y,Forward.X,0)*(ParticipantId%2?1.f:-1.f);FVector At;
        if(FindReachableGoal(GetNavAgentLocation()+Side*320.f,At))RequestBioMove(At,60.f,TEXT("Reposition"));
        else HoldBioPosition(TEXT("FireDefense"));
    }
    else HoldBioPosition(TEXT("FireDefense"));
}
void AHeliosBioNPC::Tick(float Dt)
{
    ACharacter::Tick(Dt);if(GetWorld()->IsPaused())return;
    auto* M=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=M?M->Model.FindParticipant(ParticipantId):nullptr;if(!P)return;
    CurrentHP=P->Health;bIsDead=!P->bAlive;
    if(bHumanInfectionTransition)
    {
        if(!P->bAlive||P->Team!=EHeliosBioTeam::Zombie)
        {ClearHumanInfectionTransition();ApplyTeamPresentation(P->Team,true);}
        else
        {
            // QA locomotion freezes must never freeze this original source pose.
            Tags.Remove(TEXT("Dead"));SetActorHiddenInGame(false);bLastAlive=true;
            TickHumanInfectionTransition(Dt);return;
        }
    }
    ApplyTeamPresentation(P->Team);
    if(!P->bAlive)
    {
        ResetFloatingAction();
        SerumWeaponVisual->SetVisibility(false);
        AHeliosBioStaticJumpLink::Forget(this);
        bRolePrimaryFiring=false;SetRolePrimaryPresentation(false);
        if(bWasFrozen){GetCharacterMovement()->MaxAcceleration=FrozenSavedAcceleration;bWasFrozen=false;}
        HeliosBioPlatformTraversal::ForgetBot(this);FourSlash->Cancel();Tags.AddUnique(TEXT("Dead"));
        HoldBioPosition(TEXT("Dead"));
        if(bLastAlive){DeathAge=0;if(Weapon)Weapon->DeactivateWeapon();if(BioDeath){CurrentBioClip=BioDeath;GetMesh()->PlayAnimation(BioDeath,false);}}
        DeathAge+=Dt;SetActorHiddenInGame(!BioDeath||DeathAge>BioDeath->GetPlayLength());
        GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);GetCharacterMovement()->DisableMovement();
        if(auto* AI=Cast<AAIController>(Controller))AI->StopMovement();bLastAlive=false;return;
    }
    Tags.Remove(TEXT("Dead"));SetActorHiddenInGame(false);
    if(!bLastAlive){GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);GetCharacterMovement()->SetMovementMode(MOVE_Walking);SetActorLocation(M->SpawnLocation(ParticipantId),false,nullptr,ETeleportType::TeleportPhysics);ApplyTeamPresentation(P->Team,true);bDefenseGoalValid=false;RecoveryRemaining=0;LastProgressPosition=GetActorLocation();}
    bLastAlive=true;
    if(bQAFreeze){AHeliosBioStaticJumpLink::Forget(this);HeliosBioPlatformTraversal::ForgetBot(this);bPlatformTraversalActive=false;ResetFloatingAction();bRolePrimaryFiring=false;if(Weapon)Weapon->StopFiring();if(auto* AI=Cast<AAIController>(Controller))AI->StopMovement();return;}
    if(P->FrozenSeconds>0)
    {
        if(!bWasFrozen)ResetFloatingAction();
        AHeliosBioStaticJumpLink::Forget(this);
        bRolePrimaryFiring=false;if(Weapon)Weapon->StopFiring();FourSlash->Cancel();HeliosBioPlatformTraversal::ForgetBot(this);bPlatformTraversalActive=false;
        bNavigationReconnect=false;RecoveryRemaining=0;
        auto* Movement=GetCharacterMovement();
        if(!bWasFrozen){FrozenSavedAcceleration=Movement->MaxAcceleration;bWasFrozen=true;}
        Movement->MaxAcceleration=0.f;
        // A freeze suppresses locomotion while gravity, jumping and an external
        // knockback retain their physical motion until the capsule lands.
        if(Movement->IsMovingOnGround()){Movement->Velocity.X=0.f;Movement->Velocity.Y=0.f;Movement->MaxWalkSpeed=0.f;}
        HoldBioPosition(TEXT("Frozen"));
    }
    else if(bQAHoldDecisions)HoldBioPosition(TEXT("QAHoldDecisions"));
    else
    {
        if(bWasFrozen){GetCharacterMovement()->MaxAcceleration=FrozenSavedAcceleration;bWasFrozen=false;DecisionRemaining=0;}
        const bool StaticJump=AHeliosBioStaticJumpLink::IsTraversing(this);
        const bool WasPlatformTraversal=bPlatformTraversalActive;
        bPlatformTraversalActive=!StaticJump&&!bNavigationReconnect&&(!M->bFloatingProfile||!FourSlash->IsBusy()||HeliosBioPlatformTraversal::IsInFlight(this))&&
            (M->bFloatingProfile?HeliosBioPlatformTraversal::TickTraversal(this,Cast<AAIController>(Controller),M,FloatingAction,Dt):
            HeliosBioPlatformTraversal::TickTraversal(this,Cast<AAIController>(Controller),M,Target.Get(),Dt));
        if(M->bFloatingProfile&&WasPlatformTraversal&&!bPlatformTraversalActive)DecisionRemaining=0.f;
        if(StaticJump){MovementIntent=TEXT("StaticJumpLink");bMoveIntentActive=false;BlockedSeconds=0;}
        if(bPlatformTraversalActive){MovementIntent=TEXT("PlatformTraversal");bMoveIntentActive=false;BlockedSeconds=0;}
        if(!StaticJump)TickMovementProgress(Dt);
    }
    DecisionRemaining-=Dt;MeleeCooldown=FMath::Max(0.f,MeleeCooldown-Dt);AttackAge+=Dt;PoseAge+=Dt;
    if(!bQAHoldDecisions&&DecisionRemaining<=0){DecisionRemaining=.25f+ParticipantId*.004f;Think();}
    TickRoleWeapon();
    if(ReloadRemaining>0){ReloadRemaining=FMath::Max(0.f,ReloadRemaining-Dt);if(ReloadRemaining==0&&Weapon){const int32 Before=Weapon->GetBulletCount();const int32 Loaded=FMath::Min(ReserveAmmo,MagazineSize-Before);ReserveAmmo-=Loaded;bReloadCommit=true;Weapon->ReloadMagazine();Weapon->SetMagazineBulletCount(Before+Loaded);bReloadCommit=false;}}
    if(!FourSlash->IsBusy()&&BioSkillStage==1&&CurrentBioClip&&AttackAge>=CurrentBioClip->GetPlayLength()&&BioSkillLoop&&P->SkillRemaining>0)
    {BioSkillStage=2;CurrentBioClip=BioSkillLoop;GetMesh()->PlayAnimation(BioSkillLoop,true);}
    if(!FourSlash->IsBusy()&&BioSkillStage==2&&P->SkillRemaining<=0)
    {BioSkillStage=3;AttackAge=0;CurrentBioClip=BioSkillEnd;if(BioSkillEnd)GetMesh()->PlayAnimation(BioSkillEnd,false);else bPlayingTransientPose=false;}
    if(P->Team==EHeliosBioTeam::Human)
    {
        // BIO loot drives its exact native FP clip and the profile maps that
        // source action onto the body. Keep only ordinary locomotion here;
        // grenade re-equipping must not request a nonexistent TP reload clip.
        if(Cast<AHeliosBioSupplyWeapon>(Weapon))HumanPresentation->PresentAction(GetVelocity().Size2D()>10?TEXT("Move"):TEXT("Idle"),PoseAge,true);
        else HumanPresentation->PresentAction(ReloadRemaining>0?TEXT("ChangeClip"):AttackAge<.3f?TEXT("Fire"):GetVelocity().Size2D()>10?TEXT("Move"):TEXT("Idle"),ReloadRemaining>0?ReloadDuration-ReloadRemaining:AttackAge<.3f?AttackAge:PoseAge,ReloadRemaining<=0&&AttackAge>=.3f);
    }
    else if(!FourSlash->IsBusy()&&BioSkillStage!=2&&(!bPlayingTransientPose||!CurrentBioClip||AttackAge>CurrentBioClip->GetPlayLength()))
    {
        SerumWeaponVisual->SetVisibility(false);
        bPlayingTransientPose=false;BioSkillStage=0;UAnimSequence* Clip=bRolePrimaryBodyActive?RolePrimaryIdle.Get():BioIdle.Get();
        const FVector LocalVelocity=GetActorTransform().InverseTransformVectorNoScale(GetVelocity());
        if(LocalVelocity.Size2D()>10)
        {
            UAnimSequence* Run=FMath::Abs(LocalVelocity.Y)>FMath::Abs(LocalVelocity.X)?(LocalVelocity.Y>0&&BioRunRight?BioRunRight.Get():BioRunLeft?BioRunLeft.Get():BioRun.Get()):(LocalVelocity.X<0&&BioRunBackward?BioRunBackward.Get():BioRun.Get());
            if(Run&&GetMesh()->GetSkeletalMeshAsset()&&Run->GetSkeleton()==GetMesh()->GetSkeletalMeshAsset()->GetSkeleton())Clip=Run;
        }
        if(Clip&&CurrentBioClip!=Clip){CurrentBioClip=Clip;GetMesh()->PlayAnimation(Clip,true);}
        if(bRolePrimaryFiring&&bRolePrimaryWeaponPose&&RoleWeaponVisual->IsVisible()&&!RoleWeaponVisual->LeaderPoseComponent.IsValid()&&RolePrimaryIdle)RoleWeaponVisual->PlayAnimation(RolePrimaryIdle,true);
    }
    if(GetActorLocation().Z<-2000)SetActorLocation(M->SpawnLocation(ParticipantId),false,nullptr,ETeleportType::TeleportPhysics);
}
