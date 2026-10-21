#include "QA/HeliosLoadoutQA.h"
#include "Player/HeliosArenaCharacter.h"
#include "Player/HeliosCharacterDefinition.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Player/HeliosCharacterActionProfile.h"
#include "Weapons/HeliosFirstPersonPresentationComponent.h"
#include "Control/HeliosAutonomyComponent.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Weapons/HeliosSunShangXiang.h"
#include "Weapons/HeliosChanZeus.h"
#include "Weapons/HeliosBuLianShiP90.h"
#include "Weapons/HeliosWangZheKuiThunder.h"
#include "Combat/HeliosCombatTempoSettings.h"
#include "Weapons/HeliosZhongXiaoKui.h"
#include "Weapons/HeliosHandAnimInstance.h"
#include "Weapons/HeliosCalamityStaff.h"
#include "Weapons/HeliosQingxiao.h"
#include "Weapons/HeliosSoulBanner.h"
#include "Weapons/HeliosMysteriousHand.h"
#include "Weapons/HeliosLightstickWave.h"
#include "Weapons/HeliosZXKProjectile.h"
#include "Weapons/HeliosYunSummerMegatron.h"
#include "Weapons/HeliosSSXVoice.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Camera/CameraComponent.h"
#include "ShooterProjectile.h"
#include "Pets/HeliosHumanoidPet.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/DamageType.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "TextureResource.h"
#include "ContentStreaming.h"
#include "AudioDevice.h"
#include "UObject/UnrealType.h"
#include "Components/CapsuleComponent.h"
#include "Engine/GameViewportClient.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ShooterWeapon.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

namespace
{
bool OwnedBy(const AActor* Source,const AActor* Holder)
{
    if(!Source||!Holder)return false;
    // Bound the walk even if an invalid external owner graph contains a cycle.
    for(int32 Depth=0;Source&&Depth<64;++Depth,Source=Source->GetOwner())if(Source==Holder)return true;
    return false;
}
}

void AHeliosLoadoutQAPet::Tick(float DeltaSeconds)
{
    // Only the fixture's bounded recovery/idle wait suppresses reacquisition.
    // Movement, recovery and weapon/presentation clocks keep their normal ticks.
    if(bQAWaitForIdle){Target.Reset();DecisionRemaining=10.f;}
    Super::Tick(DeltaSeconds);
}

float AHeliosLoadoutQATarget::TakeDamage(float Damage, const FDamageEvent& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser)
{
    // AShooterNPC updates HP without broadcasting AActor's damage delegate.
    // Observe the original result; do not run a second damage calculation.
    const float Applied = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);
    if (Applied > 0.f)
    {
        const UDamageType* DamageType = DamageEvent.DamageTypeClass
            ? DamageEvent.DamageTypeClass->GetDefaultObject<UDamageType>() : GetDefault<UDamageType>();
        OnTakeAnyDamage.Broadcast(this, Applied, DamageType, EventInstigator, DamageCauser);
    }
    return Applied;
}

AHeliosLoadoutQA::AHeliosLoadoutQA()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.bTickEvenWhenPaused=true;
    PrimaryActorTick.TickGroup=TG_PostUpdateWork;
}
void AHeliosLoadoutQA::BeginPlay()
{
    Super::BeginPlay();
    Out=FPaths::ProjectSavedDir()/TEXT("LoadoutQA");
    FParse::Value(FCommandLine::Get(),TEXT("HeliosLoadoutOut="),Out);
    IFileManager::Get().MakeDirectory(*Out,true);
    if(auto Audio=GetWorld()->GetAudioDevice())Audio->SetTransientPrimaryVolume(0.f);
    FString CharacterFilter,VariantFilter,WeaponFilter,WeaponListFilter;
    FParse::Value(FCommandLine::Get(),TEXT("HeliosLoadoutCharacter="),CharacterFilter);
    FParse::Value(FCommandLine::Get(),TEXT("HeliosLoadoutVariant="),VariantFilter);
    FParse::Value(FCommandLine::Get(),TEXT("HeliosLoadoutWeapon="),WeaponFilter);
    FParse::Value(FCommandLine::Get(),TEXT("HeliosLoadoutWeapons="),WeaponListFilter,false);
    TArray<FString> FilteredWeaponNames;
    WeaponListFilter.ParseIntoArray(FilteredWeaponNames,TEXT(","),true);
    TSet<FName> FilteredWeaponIds;
    for(const FString& Id:FilteredWeaponNames)FilteredWeaponIds.Add(FName(*Id));
    if(!WeaponFilter.IsEmpty())FilteredWeaponIds.Add(FName(*WeaponFilter));
    for (auto* C:FHeliosCharacterRegistry::GetAvailableCharacters(false))
        if(C && C->bPlayerSelectable && C->bCompanionSelectable && C->bPresentationReady && (CharacterFilter.IsEmpty() || C->CharacterId==FName(*CharacterFilter)))
        {
            const int32 Before=CharacterForms.Num();
            if(C->Forms.IsEmpty())
            {if(VariantFilter.IsEmpty()||FName(*VariantFilter).IsNone()){auto L=FHeliosLoadout::MakeDefault();L.CharacterId=C->CharacterId;L.CharacterVariantId=NAME_None;CharacterForms.Add(L);}}
            else for(const auto& Form:C->Forms)
                if(Form.bPresentationReady&&(VariantFilter.IsEmpty()||Form.VariantId==FName(*VariantFilter)))
                {auto L=FHeliosLoadout::MakeDefault();L.CharacterId=C->CharacterId;L.CharacterVariantId=Form.VariantId;CharacterForms.Add(L);}
            if(CharacterForms.Num()>Before)Characters.Add(C);
        }
    for (auto* W:UHeliosWeaponRegistry::GetAvailableWeapons())
        if(W && W->WeaponId!=TEXT("mech") && W->WeaponId!=TEXT("fighter") && (FilteredWeaponIds.IsEmpty() || FilteredWeaponIds.Contains(W->WeaponId))) Weapons.Add(W);
    int32 ExpectedForms=0;TSet<FName> ExpectedCharacters;
    for(const FName Id:{FName("cfm_sunshangxiang"),FName("cfm_zhongxiaokui"),FName("cfm_chan"),FName("cfm_bulianshi"),FName("cfm_wangzhekui"),FName("cfm_yun_summer")})
    {
        if(!CharacterFilter.IsEmpty()&&Id!=FName(*CharacterFilter))continue;
        TArray<FName> Variants;
        if(Id==TEXT("cfm_bulianshi")||Id==TEXT("cfm_yun_summer"))Variants={TEXT("GR"),TEXT("BL"),TEXT("SP")};
        else if(Id==TEXT("cfm_wangzhekui"))Variants={TEXT("GR"),TEXT("BL")};
        else Variants={NAME_None};
        for(FName Variant:Variants)if(VariantFilter.IsEmpty()||Variant==FName(*VariantFilter)){++ExpectedForms;ExpectedCharacters.Add(Id);}
    }
    Check(TEXT("active_characters"),Characters.Num()==ExpectedCharacters.Num()&&ExpectedForms>0);
    Check(TEXT("active_forms"),CharacterForms.Num()==ExpectedForms&&ExpectedForms>0);
    Check(TEXT("retained_handheld_weapons"),FilteredWeaponIds.IsEmpty()?Weapons.Num()==13:Weapons.Num()==FilteredWeaponIds.Num());
    for(const TCHAR* Id:{TEXT("rifle"),TEXT("rocket"),TEXT("laser"),TEXT("shotgun")})
        Check(FString(TEXT("retired_"))+Id,UHeliosWeaponRegistry::FindWeapon(Id)==nullptr);
}
void AHeliosLoadoutQA::Check(const FString& Name,bool bPass)
{
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("case"),CaseName);J->SetStringField(TEXT("name"),Name);J->SetBoolField(TEXT("passed"),bPass);
    J->SetNumberField(TEXT("player_damage"),PlayerDamage);J->SetNumberField(TEXT("companion_damage"),CompanionDamage);J->SetNumberField(TEXT("unattributed_damage"),UnattributedDamage);
    Checks.Add(MakeShared<FJsonValueObject>(J));if(!bPass)++Failures;
    UE_LOG(LogTemp,Display,TEXT("LOADOUT_QA %s %s %s"),*CaseName,*Name,bPass?TEXT("PASS"):TEXT("FAIL"));
}
void AHeliosLoadoutQA::Capture(const TCHAR* Name)
{
    check(PendingCaptureName.IsEmpty());
    PendingCaptureName=Name;
    PendingCaptureFrame=FMath::Max(GFrameCounter+2,ResourcesReadyFrame+2);
    PendingCaptureReadyDeadline=GetWorld()->GetTimeSeconds()+10.f;
    CaptureReleaseFrame=PendingCaptureFrame+2;
    CaptureReleaseTime=GetWorld()->GetTimeSeconds()+.1;
    UE_LOG(LogTemp,Display,TEXT("LOADOUT_QA_CAPTURE_REQUEST case=%s state=%s phase=%d age=%.6f frame=%llu after_frame=%llu"),*CaseName,Name,Phase,Age,GFrameCounter,PendingCaptureFrame);
}
void AHeliosLoadoutQA::DeferAfterCapture(TFunction<void()> Mutation)
{
    check(!DeferredCaptureMutation);
    DeferredCaptureMutation=MoveTemp(Mutation);
}
bool AHeliosLoadoutQA::CaptureThirdPersonHands(const FString& State)
{
    auto* P=Companion?Companion->GetCharacterPresentation():nullptr;
    auto* Body=P?P->GetArmsMesh():nullptr;
    if(!Body||!Body->IsVisible()||!Body->GetSkeletalMeshAsset()||Body->GetBoneIndex(TEXT("b_LeftHand"))==INDEX_NONE||Body->GetBoneIndex(TEXT("b_RightHand"))==INDEX_NONE)return false;
    if(!HandsRenderTarget)
    {
        HandsRenderTarget=NewObject<UTextureRenderTarget2D>(this,TEXT("LoadoutQAHandsTarget"));
        HandsRenderTarget->ClearColor=FLinearColor(.035f,.035f,.035f,1.f);
        HandsRenderTarget->InitCustomFormat(1280,960,PF_B8G8R8A8,false);HandsRenderTarget->UpdateResourceImmediate(true);
    }
    if(!HandsCapture)
    {
        HandsCapture=NewObject<USceneCaptureComponent2D>(this,TEXT("LoadoutQAHandsCamera"));
        HandsCapture->bCaptureEveryFrame=false;HandsCapture->bCaptureOnMovement=false;HandsCapture->bAlwaysPersistRenderingState=true;
        HandsCapture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
        HandsCapture->CaptureSource=ESceneCaptureSource::SCS_FinalColorLDR;HandsCapture->TextureTarget=HandsRenderTarget;
        HandsCapture->FOVAngle=55.f;HandsCapture->ShowFlags.SetMotionBlur(false);HandsCapture->ShowFlags.SetTemporalAA(false);
        HandsCapture->PostProcessSettings.bOverride_MotionBlurAmount=true;HandsCapture->PostProcessSettings.MotionBlurAmount=0;
        HandsCapture->RegisterComponent();
    }
    HandsCapture->ClearShowOnlyComponents();TArray<TSharedPtr<FJsonValue>> Components;
    for(UPrimitiveComponent* Part:{static_cast<UPrimitiveComponent*>(Body),static_cast<UPrimitiveComponent*>(P->GetWeaponMesh()),static_cast<UPrimitiveComponent*>(P->GetAlternateWeaponMesh()),static_cast<UPrimitiveComponent*>(P->GetGestureCupMesh())})
        if(Part&&Part->IsVisible())
        {
            HandsCapture->ShowOnlyComponent(Part);
            auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("component"),Part->GetPathName());Item->SetStringField(TEXT("world_transform"),Part->GetComponentTransform().ToString());
            if(auto* Skin=Cast<USkeletalMeshComponent>(Part))Item->SetStringField(TEXT("mesh"),Skin->GetSkeletalMeshAsset()?Skin->GetSkeletalMeshAsset()->GetPathName():TEXT("null"));
            if(auto* Static=Cast<UStaticMeshComponent>(Part))Item->SetStringField(TEXT("mesh"),Static->GetStaticMesh()?Static->GetStaticMesh()->GetPathName():TEXT("null"));
            Components.Add(MakeShared<FJsonValueObject>(Item));
        }
    const FTransform Left=P->GetAnchorTransform(TEXT("LeftHand")),Right=P->GetAnchorTransform(TEXT("RightHand"));
    if(Left.ContainsNaN()||Right.ContainsNaN()||Body->GetComponentTransform().ContainsNaN())return false;
    const FVector Focus=(Left.GetLocation()+Right.GetLocation())*.5+FVector(0,0,5);
    const double Distance=FMath::Max(115.0,FVector::Dist(Left.GetLocation(),Right.GetLocation())*1.4+65);
    const FVector Position=Focus+Companion->GetActorForwardVector()*Distance+Companion->GetActorRightVector()*Distance*.45+FVector(0,0,Distance*.35);
    HandsCapture->SetWorldLocationAndRotation(Position,(Focus-Position).Rotation());HandsCapture->CaptureScene();
    TArray<FColor> Pixels;auto* Resource=HandsRenderTarget->GameThread_GetRenderTargetResource();
    const bool Read=Resource&&Resource->ReadPixels(Pixels)&&Pixels.Num()==HandsRenderTarget->SizeX*HandsRenderTarget->SizeY;
    TArray64<uint8> PNG;if(Read)FImageUtils::PNGCompressImageArray(HandsRenderTarget->SizeX,HandsRenderTarget->SizeY,MakeArrayView(Pixels),PNG);
    const FString Path=Out/(CaseName+TEXT("_")+State+TEXT("_tp_hands.png"));
    const bool Saved=Read&&!PNG.IsEmpty()&&FFileHelper::SaveArrayToFile(PNG,*Path);
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("case"),CaseName);Row->SetStringField(TEXT("state"),State);Row->SetBoolField(TEXT("saved"),Saved);
    Row->SetNumberField(TEXT("frame"),double(GFrameCounter));Row->SetNumberField(TEXT("world_time"),GetWorld()->GetTimeSeconds());Row->SetNumberField(TEXT("phase_age"),Age);
    Row->SetStringField(TEXT("left_hand_world"),Left.ToString());Row->SetStringField(TEXT("right_hand_world"),Right.ToString());Row->SetStringField(TEXT("body_world_scale"),Body->GetComponentScale().ToString());
    Row->SetStringField(TEXT("holder_character_id"),Companion->GetLoadout().CharacterId.ToString());Row->SetStringField(TEXT("holder_variant_id"),Companion->GetLoadout().CharacterVariantId.ToString());Row->SetStringField(TEXT("holder_primary_weapon_id"),Companion->GetLoadout().PrimaryWeaponId.ToString());
    Row->SetStringField(TEXT("camera_position"),Position.ToString());Row->SetStringField(TEXT("camera_focus"),Focus.ToString());Row->SetNumberField(TEXT("capture_fov"),HandsCapture->FOVAngle);
    Row->SetArrayField(TEXT("visible_actual_components"),Components);Row->SetBoolField(TEXT("excluded_effects_and_target_only_for_visibility"),true);
    if(auto* Node=Body->GetSingleNodeInstance()){Row->SetStringField(TEXT("tp_clip"),GetNameSafe(Node->GetCurrentAsset()));Row->SetNumberField(TEXT("tp_clip_age"),Node->GetCurrentTime());}
    AShooterWeapon* Source=State.StartsWith(TEXT("mode_"))&&ModePrimary.IsValid()?ModePrimary.Get():Companion->GetEquippedWeapon();
    Row->SetStringField(TEXT("source_weapon"),GetNameSafe(Source));
    USkeletalMeshComponent* Driver=Source?Source->GetFirstPersonMesh():nullptr;
    if(auto* Legacy=Source&&Source->GetOwner()?Source->GetOwner()->FindComponentByClass<UHeliosFirstPersonPresentationComponent>():nullptr;Legacy&&Legacy->IsReady())Driver=Legacy->GetArmsMesh();
    Row->SetBoolField(TEXT("source_clock_available"),false);Row->SetNumberField(TEXT("source_clip_age"),0);Row->SetStringField(TEXT("source_clip"),TEXT("null"));
    if(auto* Mesh=Driver)
    {
        if(auto* Hand=Cast<UHeliosHandAnimInstance>(Mesh->GetAnimInstance())){Row->SetStringField(TEXT("source_clip"),GetNameSafe(Hand->Sequence));Row->SetNumberField(TEXT("source_clip_age"),Hand->SampleSeconds);Row->SetBoolField(TEXT("source_clock_available"),Hand->Sequence!=nullptr);}
        else if(auto* Node=Mesh->GetSingleNodeInstance()){Row->SetStringField(TEXT("source_clip"),GetNameSafe(Node->GetCurrentAsset()));Row->SetNumberField(TEXT("source_clip_age"),Node->GetCurrentTime());Row->SetBoolField(TEXT("source_clock_available"),Node->GetCurrentAsset()!=nullptr);}
    }
    Row->SetNumberField(TEXT("tp_gesture_age"),P->GetCharacterGestureAge());Row->SetBoolField(TEXT("tp_gesture_playing"),P->IsCharacterGesturePlaying());
    FString Serialized;FJsonSerializer::Serialize(Row,TJsonWriterFactory<>::Create(&Serialized));
    const bool MetadataSaved=FFileHelper::SaveStringToFile(Serialized,*(Out/(CaseName+TEXT("_")+State+TEXT("_tp_hands.json"))));
    UE_LOG(LogTemp,Display,TEXT("LOADOUT_QA_TP_HANDS case=%s state=%s frame=%llu saved=%d components=%d body_scale=%s path=%s"),*CaseName,*State,GFrameCounter,Saved&&MetadataSaved,Components.Num(),*Body->GetComponentScale().ToString(),*Path);
    return Saved&&MetadataSaved;
}
void AHeliosLoadoutQA::OnTargetDamage(AActor* Damaged,float Damage,const UDamageType*,AController* Controller,AActor* Causer)
{
    if(Damaged!=Target||Damage<=0)return;
    const bool PetHit=IsValid(Companion)&&((Companion->GetController()&&Controller==Companion->GetController())||OwnedBy(Causer,Companion));
    const bool PlayerHit=!PetHit&&IsValid(Player)&&((Player->GetController()&&Controller==Player->GetController())||OwnedBy(Causer,Player));
    if(PetHit)
    {
        CompanionDamage+=Damage;if(Phase==0)++ObservedCompanionDamageHits;
        if((Phase==6||Phase==7)&&IsHunterSourcePose(Phase==7))
        {
            HunterDamage+=Damage;++HunterContacts;
            if(auto* W=Cast<AHeliosSunShangXiangWeapon>(Companion->GetEquippedWeapon()))bHunterContactDuringCooldown|=W->CooldownRemaining()>0;
        }
    }
    else if(PlayerHit)PlayerDamage+=Damage;
    else UnattributedDamage+=Damage;
    UE_LOG(LogTemp,Display,TEXT("LOADOUT_QA_DAMAGE case=%s phase=%d amount=%.3f holder=%s controller=%s causer=%s"),*CaseName,Phase,Damage,PetHit?TEXT("companion"):PlayerHit?TEXT("player"):TEXT("unattributed"),*GetNameSafe(Controller),*GetNameSafe(Causer));
}
void AHeliosLoadoutQA::DiagnoseCompanionReadiness(bool bFinal)
{
    if(!Companion||!GetWorld())return;
    auto* Weapon=Companion->GetEquippedWeapon();
    auto* Presentation=Companion->GetCharacterPresentation();
    auto* Enemy=Companion->Target.Get();
    auto* Movement=Companion->GetCharacterMovement();
    auto* ReadOnlyPet=Cast<AHeliosLoadoutQAPet>(Companion);
    const auto* Definition=UHeliosWeaponRegistry::FindWeapon(Companion->GetLoadout().PrimaryWeaponId);
    const FName Id=Companion->GetLoadout().PrimaryWeaponId;
    float Range=Definition?Definition->Range:0;
    if(Id==TEXT("cfm_chan_zeus"))Range=HeliosChanZeus::Tuning(true).Range;
    else if(Id==TEXT("cfm_bulianshi_p90"))Range=HeliosBuLianShiP90::Tuning(true).Range;
    else if(Id==TEXT("cfm_wangzhekui_thunder"))Range=HeliosWangZheKuiThunder::Tuning(true).Range;
    else if(Id==TEXT("cfm_yun_summer_megatron"))Range=HeliosYunSummer::Tuning(true).Range;
    const bool Hunter=Id==TEXT("cfm_sunshangxiang")||Id==TEXT("cfm_zhongxiaokui");
    if(Hunter)Range=UHeliosCombatTempoSettings::Hunter(Companion,Id==TEXT("cfm_zhongxiaokui"),true,TEXT("攻击距离"),260,10,10000);
    const bool Alive=Enemy&&!Enemy->IsEnemyDead();
    const double Distance=Enemy?FVector::Dist2D(Companion->GetActorLocation(),Enemy->GetActorLocation()):-1;
    const float YawDelta=Enemy?FMath::FindDeltaAngleDegrees(Companion->GetActorRotation().Yaw,(Enemy->GetActorLocation()-Companion->GetActorLocation()).Rotation().Yaw):180;
    const double Speed=Companion->GetVelocity().Size2D();
    const bool Facing=Enemy&&FMath::Abs(YawDelta)<8;
    const bool Falling=Movement&&Movement->IsFalling();
    const bool Clear=Enemy&&Companion->HasClearLine(Enemy);
    const float HitPose=ReadOnlyPet?ReadOnlyPet->QAHitPoseRemaining():-1;
    const bool SourceBusy=Presentation&&Presentation->IsSourceActionPlaying();
    UAnimSequence* SourceClip=nullptr;float SourceSeconds=0;
    if(auto* Mesh=Weapon?Weapon->GetFirstPersonMesh():nullptr)
    {
        if(auto* Hand=Cast<UHeliosHandAnimInstance>(Mesh->GetAnimInstance())){SourceClip=Hand->Sequence;SourceSeconds=Hand->SampleSeconds;}
        else if(auto* Node=Mesh->GetSingleNodeInstance()){SourceClip=Cast<UAnimSequence>(Node->GetCurrentAsset());SourceSeconds=Node->GetCurrentTime();}
    }
    FName Action=NAME_None;
    if(auto* ZXK=Cast<AHeliosZhongXiaoKuiWeapon>(Weapon))Action=ZXK->GetCurrentSourceAction();
    else if(SourceClip&&!SourceClip->GetName().Contains(TEXT("Idle"))&&!SourceClip->GetName().Contains(TEXT("Move"))&&!SourceClip->GetName().Contains(TEXT("Run_"))&&!SourceClip->GetName().Contains(TEXT("Recover")))Action=FName(SourceClip->GetName());
    if(!Action.IsNone()&&Action!=LastObservedCompanionAction)++ObservedCompanionAttackStarts;
    LastObservedCompanionAction=Action;
    const double Now=GetWorld()->GetTimeSeconds();
    if(!bFinal&&Now<NextReadinessLogAt)return;
    NextReadinessLogAt=Now+.25;
    auto Row=MakeShared<FJsonObject>();
    Row->SetStringField(TEXT("case"),CaseName);Row->SetStringField(TEXT("kind"),TEXT("companion_readiness"));Row->SetBoolField(TEXT("final"),bFinal);
    Row->SetNumberField(TEXT("world_time"),Now);Row->SetNumberField(TEXT("phase_age"),Age);Row->SetNumberField(TEXT("frame"),double(GFrameCounter));
    if(Presentation&&Presentation->GetArmsMesh())
    {
        auto* Body=Presentation->GetArmsMesh();auto* Mesh=Body->GetSkeletalMeshAsset();
        Row->SetStringField(TEXT("tp_body_component_scale"),Body->GetComponentScale().ToString());Row->SetStringField(TEXT("tp_body_relative_scale"),Body->GetRelativeScale3D().ToString());
        Row->SetNumberField(TEXT("tp_body_raw_bounds_half_height"),Mesh?Mesh->GetBounds().BoxExtent.Z:0);Row->SetNumberField(TEXT("tp_body_raw_bounds_height"),Mesh?Mesh->GetBounds().BoxExtent.Z*2:0);
    }
    for(const auto& Holder:TArray<TPair<FString,AShooterWeapon*>>{{TEXT("player"),Player?Player->GetEquippedWeapon():nullptr},{TEXT("companion"),Weapon}})
        if(auto* Gun=Holder.Value){TInlineComponentArray<USceneComponent*> Parts(Gun);for(auto* Part:Parts)Row->SetStringField(Holder.Key+TEXT("_source_scale_")+Part->GetName(),Part->GetComponentScale().ToString());}
    Row->SetStringField(TEXT("companion_position"),Companion->GetActorLocation().ToString());Row->SetStringField(TEXT("target_position"),Enemy?Enemy->GetActorLocation().ToString():TEXT("null"));
    Row->SetStringField(TEXT("actual_target"),GetNameSafe(Enemy));Row->SetBoolField(TEXT("actual_target_is_fixture"),Enemy==Target);
    Row->SetNumberField(TEXT("distance_2d_cm"),Distance);Row->SetNumberField(TEXT("range_cm"),Range);Row->SetNumberField(TEXT("yaw_delta_degrees"),YawDelta);Row->SetNumberField(TEXT("speed_2d_cm_s"),Speed);
    Row->SetBoolField(TEXT("target_alive"),Alive);Row->SetBoolField(TEXT("facing_less_than_8_degrees"),Facing);Row->SetBoolField(TEXT("falling"),Falling);
    Row->SetBoolField(TEXT("velocity_less_than_20"),Speed<20);Row->SetBoolField(TEXT("distance_within_range"),Distance>=0&&Distance<=Range);Row->SetBoolField(TEXT("clear_line"),Clear);
    Row->SetNumberField(TEXT("hit_pose_remaining"),HitPose);Row->SetBoolField(TEXT("source_action_playing"),SourceBusy);
    Row->SetBoolField(TEXT("ready_predicate"),Alive&&Facing&&!Falling&&Speed<20&&Distance<=Range&&Clear);
    Row->SetBoolField(TEXT("attack_gate_without_private_press_wait"),Alive&&Facing&&!Falling&&Speed<20&&Distance<=Range&&Clear&&HitPose<=0&&(!Hunter||!SourceBusy));
    Row->SetStringField(TEXT("source_pose"),GetNameSafe(SourceClip));Row->SetStringField(TEXT("source_action"),Action.ToString());Row->SetNumberField(TEXT("source_seconds"),SourceSeconds);
    Row->SetNumberField(TEXT("observed_source_attack_starts"),ObservedCompanionAttackStarts);Row->SetNumberField(TEXT("actual_damage_hit_events"),ObservedCompanionDamageHits);
    Row->SetNumberField(TEXT("companion_damage"),CompanionDamage);Row->SetNumberField(TEXT("reported_shots"),Companion->ShotsFired);
    Row->SetNumberField(TEXT("inherited_attack_serial"),ReadOnlyPet?ReadOnlyPet->QAAttackSerial():-1);Row->SetNumberField(TEXT("inherited_attack_age"),ReadOnlyPet?ReadOnlyPet->QAAttackAge():-1);
    Row->SetNumberField(TEXT("state"),int32(Companion->State));Row->SetBoolField(TEXT("sleeping"),Companion->IsSleeping());Row->SetBoolField(TEXT("reloading"),Companion->IsReloading());
    Diagnostics.Add(MakeShared<FJsonValueObject>(Row));FString Text;FJsonSerializer::Serialize(Row,TJsonWriterFactory<>::Create(&Text));
    UE_LOG(LogTemp,Display,TEXT("LOADOUT_QA_READY %s"),*Text);
}
void AHeliosLoadoutQA::DiagnoseModeTransforms()
{
    if(!GetWorld()||!Player)return;
    FVector Eye;FRotator Rotation;Player->GetActorEyesViewPoint(Eye,Rotation);
    // UE58 DoubleFloat.cpp's SafeCastMatrix precision threshold in centimetres.
    constexpr double OriginMax=2097151.0;
    double MaxComponent=0,MaxBone=0,MaxScale=0,MaxBounds=0;
    FString WorstComponent,WorstBone;int32 Components=0,Bones=0,Invalid=0;
    auto AxisMax=[](const FVector& V){return FMath::Max3(FMath::Abs(V.X),FMath::Abs(V.Y),FMath::Abs(V.Z));};
    auto Inspect=[&](const FTransform& T,const FString& Path,bool Bone)
    {
        const double Offset=AxisMax(T.GetLocation()-Eye);
        if(Bone){if(Offset>MaxBone){MaxBone=Offset;WorstBone=Path;}}
        else if(Offset>MaxComponent){MaxComponent=Offset;WorstComponent=Path;}
        MaxScale=FMath::Max(MaxScale,AxisMax(T.GetScale3D()));
        const bool Valid=T.IsValid()&&FMath::IsFinite(Offset)&&Offset<=OriginMax;
        if(!Valid)
        {
            ++Invalid;
            UE_LOG(LogTemp,Error,TEXT("LOADOUT_MODE_TRANSFORM_INVALID case=%s age=%.6f time=%.6f frame=%llu path=%s bone=%d transform=%s eye=%s relative_axis=%.9g"),*CaseName,Age,GetWorld()->GetTimeSeconds(),GFrameCounter,*Path,Bone,*T.ToString(),*Eye.ToString(),Offset);
        }
    };
    for(TActorIterator<AActor> It(GetWorld());It;++It)
    {
        auto* Actor=*It;if(!OwnedBy(Actor,Player)&&!OwnedBy(Actor,Companion))continue;
        TInlineComponentArray<UPrimitiveComponent*> Primitives;Actor->GetComponents(Primitives);
        for(auto* Primitive:Primitives)
        {
            if(!IsValid(Primitive))continue;++Components;
            Inspect(Primitive->GetComponentTransform(),Primitive->GetPathName(),false);
            MaxBounds=FMath::Max(MaxBounds,double(Primitive->Bounds.SphereRadius));
            const double BoundOffset=AxisMax(Primitive->Bounds.Origin-Eye);
            if(Primitive->Bounds.Origin.ContainsNaN()||!FMath::IsFinite(Primitive->Bounds.SphereRadius)||!FMath::IsFinite(BoundOffset)||BoundOffset>OriginMax)
            {
                ++Invalid;UE_LOG(LogTemp,Error,TEXT("LOADOUT_MODE_BOUNDS_INVALID case=%s age=%.6f frame=%llu path=%s bounds_origin=%s radius=%.9g relative_axis=%.9g"),*CaseName,Age,GFrameCounter,*Primitive->GetPathName(),*Primitive->Bounds.Origin.ToString(),Primitive->Bounds.SphereRadius,BoundOffset);
            }
            if(auto* Skinned=Cast<USkinnedMeshComponent>(Primitive);Skinned&&Skinned->GetSkinnedAsset())
                for(int32 Index=0;Index<Skinned->GetNumBones();++Index){++Bones;Inspect(Skinned->GetBoneTransform(Index),Skinned->GetPathName()+TEXT(":")+Skinned->GetBoneName(Index).ToString(),true);}
        }
    }
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("case"),CaseName);Row->SetStringField(TEXT("kind"),TEXT("C_to_B_transform_range"));
    Row->SetNumberField(TEXT("world_time"),GetWorld()->GetTimeSeconds());Row->SetNumberField(TEXT("phase_age"),Age);Row->SetNumberField(TEXT("frame"),double(GFrameCounter));
    Row->SetNumberField(TEXT("components"),Components);Row->SetNumberField(TEXT("bones"),Bones);Row->SetNumberField(TEXT("invalid"),Invalid);
    Row->SetNumberField(TEXT("max_component_relative_axis_cm"),MaxComponent);Row->SetNumberField(TEXT("max_bone_relative_axis_cm"),MaxBone);Row->SetNumberField(TEXT("max_scale_axis"),MaxScale);Row->SetNumberField(TEXT("max_bounds_radius_cm"),MaxBounds);
    Row->SetStringField(TEXT("worst_component"),WorstComponent);Row->SetStringField(TEXT("worst_bone"),WorstBone);
    Diagnostics.Add(MakeShared<FJsonValueObject>(Row));FString Text;FJsonSerializer::Serialize(Row,TJsonWriterFactory<>::Create(&Text));
    UE_LOG(LogTemp,Display,TEXT("LOADOUT_MODE_TRANSFORM_RANGE %s"),*Text);
    if(Invalid>0&&!bModeTransformFailureRecorded){bModeTransformFailureRecorded=true;Check(TEXT("zxk_C_to_B_transforms_valid"),false);}
}
void AHeliosLoadoutQA::CleanupHolderResidue(AActor* Holder)
{
    if(!IsValid(Holder))return;
    if(auto* S=UHeliosCalamityState::Get(Holder,false))S->Clear();
    if(auto* F=UHeliosQingxiaoFormation::Get(Holder,false))F->Clear();
    if(auto* A=UHeliosSoulArmy::Get(Holder,false))A->Clear();
    TArray<AActor*> Residue;
    for(TActorIterator<AActor> It(GetWorld());It;++It)
    {
        AActor* A=*It;
        if(A==this||A==Holder||A==Player||A==Companion||A==Target||A->IsActorBeingDestroyed()||A->IsA<AShooterWeapon>()||A->IsA<AController>())continue;
        const bool GameplayResidue=A->IsA<AShooterProjectile>()||A->ActorHasTag(TEXT("HeliosSummon"))||A->IsA<AHeliosMegatronGrenade>()||A->IsA<AHeliosZXKProjectile>()||A->IsA<AHeliosHandMissile>()||A->IsA<AHeliosLightstickWave>()||A->IsA<AHeliosCalamityEffect>()||A->IsA<AHeliosSoulBolt>()||A->IsA<AHeliosQingxiaoSword>();
        if(GameplayResidue&&(OwnedBy(A,Holder)||A->GetInstigator()==Holder))Residue.Add(A);
    }
    for(AActor* A:Residue)if(IsValid(A))A->Destroy();
}
FHeliosLoadout AHeliosLoadoutQA::DifferentCharacterLoadout(const FHeliosLoadout& Current) const
{
    FHeliosLoadout Result=Current;
    // Cover both restored 102-cm capsule branches after the 96-cm Yun form.
    // This only changes which ready character the QA fixture selects.
    if(Current.CharacterId==TEXT("cfm_yun_summer")&&(WeaponIndex%2)==1)
    {
        auto* C=FHeliosCharacterRegistry::FindCharacter(TEXT("cfm_zhongxiaokui"));
        if(C&&C->bPresentationReady&&C->bPlayerSelectable&&C->bCompanionSelectable)
        {Result.CharacterId=C->CharacterId;Result.CharacterVariantId=C->DefaultVariantId;return FHeliosLoadout::Normalize(Result);}
    }
    for(auto* C:FHeliosCharacterRegistry::GetAvailableCharacters(false))
        if(C&&C->bPresentationReady&&C->bPlayerSelectable&&C->bCompanionSelectable&&C->CharacterId!=Current.CharacterId)
        {Result.CharacterId=C->CharacterId;Result.CharacterVariantId=C->DefaultVariantId;break;}
    return FHeliosLoadout::Normalize(Result);
}
void AHeliosLoadoutQA::PauseCompanion()
{
    if(!IsValid(Companion))return;
    Companion->SuspendCombat();Companion->SetActorTickEnabled(false);
    CleanupHolderResidue(Companion);
}
bool AHeliosLoadoutQA::IsCompanionActuallyIdle() const
{
    auto* P=Companion?Companion->GetCharacterPresentation():nullptr;
    auto* Node=P&&P->GetArmsMesh()?P->GetArmsMesh()->GetSingleNodeInstance():nullptr;
    auto* Clip=Node?Cast<UAnimSequence>(Node->GetCurrentAsset()):nullptr;
    return Companion&&Companion->Health>0&&!Companion->IsSleeping()&&Companion->State!=EHeliosPetState::Waking
        &&P&&P->GetRecoveryPoseLockRemaining()<=0&&!P->IsSourceActionPlaying()&&!P->IsCharacterGesturePlaying()
        &&Clip&&Clip->GetName().Contains(TEXT("Idle"));
}
bool AHeliosLoadoutQA::IsNativeDriverHidden() const
{
    auto* P=Player?Player->GetCharacterPresentation():nullptr;
    auto* W=Player?Player->GetEquippedWeapon():nullptr;
    auto* Driver=W?W->GetFirstPersonMesh():nullptr;
    if(auto* Legacy=Player?Player->FindComponentByClass<UHeliosFirstPersonPresentationComponent>():nullptr;Legacy&&Legacy->IsReady())Driver=Legacy->GetArmsMesh();
    const bool Hidden=P&&P->IsReady()&&Driver&&Driver!=P->GetArmsMesh()&&!Driver->IsVisible();
    UE_LOG(LogTemp,Display,TEXT("LOADOUT_NATIVE_DRIVER_VISIBILITY case=%s phase=%d frame=%llu driver=%s visible=%d selected=%s valid_hidden=%d"),*CaseName,Phase,GFrameCounter,*GetNameSafe(Driver),Driver?Driver->IsVisible():true,P?*GetNameSafe(P->GetArmsMesh()):TEXT("null"),Hidden);
    return Hidden;
}
void AHeliosLoadoutQA::DiagnoseIdleWait(const TCHAR* Stage,bool Final)
{
    if(!Final&&GetWorld()->GetTimeSeconds()<NextIdleWaitDiagnosticAt)return;
    NextIdleWaitDiagnosticAt=GetWorld()->GetTimeSeconds()+.25;
    auto* P=Companion?Companion->GetCharacterPresentation():nullptr;
    auto* Node=P&&P->GetArmsMesh()?P->GetArmsMesh()->GetSingleNodeInstance():nullptr;
    auto* W=Player?Cast<AHeliosZhongXiaoKuiWeapon>(Player->GetEquippedWeapon()):nullptr;
    auto* Hand=W&&W->GetFirstPersonMesh()?Cast<UHeliosHandAnimInstance>(W->GetFirstPersonMesh()->GetAnimInstance()):nullptr;
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("case"),CaseName);Row->SetStringField(TEXT("kind"),TEXT("fixture_idle_wait"));Row->SetStringField(TEXT("stage"),Stage);
    Row->SetNumberField(TEXT("frame"),GFrameCounter);Row->SetNumberField(TEXT("phase_age"),Age);Row->SetBoolField(TEXT("final"),Final);
    Row->SetBoolField(TEXT("companion_idle"),IsCompanionActuallyIdle());Row->SetNumberField(TEXT("state"),Companion?int32(Companion->State):-1);
    Row->SetStringField(TEXT("tp_asset"),Node?GetNameSafe(Node->GetCurrentAsset()):TEXT("null"));Row->SetNumberField(TEXT("pose_lock"),P?P->GetRecoveryPoseLockRemaining():-1);
    Row->SetBoolField(TEXT("source_action"),P&&P->IsSourceActionPlaying());Row->SetStringField(TEXT("player_source"),Hand?GetNameSafe(Hand->Sequence):TEXT("null"));
    Row->SetStringField(TEXT("player_action"),W?W->GetCurrentSourceAction().ToString():TEXT("None"));Diagnostics.Add(MakeShared<FJsonValueObject>(Row));
    FString Serialized;FJsonSerializer::Serialize(Row,TJsonWriterFactory<>::Create(&Serialized));UE_LOG(LogTemp,Display,TEXT("LOADOUT_FIXTURE_IDLE_WAIT %s"),*Serialized);
}
void AHeliosLoadoutQA::WarmCaptureResources()
{
    auto Keep=[this](UObject* Asset){if(Asset)WarmedCaptureAssets.AddUnique(Asset);return Asset;};
    if(WarmedCaptureAssets.IsEmpty())
    {
        TArray<FHeliosLoadout> Forms;
        for(auto L:CharacterForms)if(L.CharacterId==PairLoadout.CharacterId){L.PrimaryWeaponId=PairLoadout.PrimaryWeaponId;Forms.Add(L);}
        Forms.Add(DifferentCharacterLoadout(PairLoadout));
        for(const auto& L:Forms)
        {
            auto* C=FHeliosCharacterRegistry::FindCharacter(L.CharacterId);if(!C)continue;
            const auto* F=C->FindForm(L.CharacterVariantId);
            auto* Profile=F?F->ActionProfile.LoadSynchronous():C->ActionProfile.LoadSynchronous();Keep(Profile);
            if(F)Keep(F->PreviewTexture.LoadSynchronous());else Keep(C->PreviewTexture.LoadSynchronous());
            if(const auto* Pair=Profile?Profile->FindWeapon(L.PrimaryWeaponId):nullptr)
            {
                Keep(Pair->FirstPersonMesh.LoadSynchronous());Keep(Pair->FirstPersonWeaponMesh.LoadSynchronous());Keep(Pair->ThirdPersonMesh.LoadSynchronous());Keep(Pair->ThirdPersonWeaponMesh.LoadSynchronous());
                for(const auto& Clip:Pair->FirstPersonClips)Keep(Clip.Value.LoadSynchronous());
                for(const auto& Clip:Pair->ThirdPersonClips)Keep(Clip.Value.LoadSynchronous());
            }
            if(L.CharacterId==TEXT("cfm_yun_summer"))
            {
                const FString Root=FString::Printf(TEXT("/Game/Helios/LoadoutPresentation/Forms/cfm_yun_summer/%s/Source/"),*L.CharacterVariantId.ToString());
                const FString Prefix=TEXT("YUN_")+L.CharacterVariantId.ToString();
                for(const FString& Name:{TEXT("SK_")+Prefix+TEXT("_FP"),TEXT("SK_")+Prefix+TEXT("_Cup_FP"),TEXT("SK_")+Prefix+TEXT("_Cup_TP")})Keep(LoadObject<USkeletalMesh>(nullptr,*(Root+Name+TEXT(".")+Name)));
                for(const FString& Name:{TEXT("A_")+Prefix+TEXT("_FP_Cheers"),TEXT("A_")+Prefix+TEXT("_CUP_FP_Cheers")})Keep(LoadObject<UAnimSequence>(nullptr,*(Root+Name+TEXT(".")+Name)));
            }
        }
    }
    auto WarmMaterial=[this](UMaterialInterface* M)
    {
        if(!M)return;
        M->SetForceMipLevelsToBeResident(false,false,120.f,0,true);
        TArray<UTexture*> Textures;M->GetUsedTextures(Textures);
        for(auto* Texture:Textures)if(auto* T=Cast<UTexture2D>(Texture))WarmedCaptureTextures.AddUnique(T);
    };
    for(UObject* Asset:WarmedCaptureAssets)if(auto* Texture=Cast<UTexture2D>(Asset)){Texture->SetForceMipLevelsToBeResident(120.f);WarmedCaptureTextures.AddUnique(Texture);}
    for(UObject* Asset:WarmedCaptureAssets)if(auto* Mesh=Cast<USkeletalMesh>(Asset))for(const auto& Slot:Mesh->GetMaterials())WarmMaterial(Slot.MaterialInterface);
    for(AActor* Holder:{static_cast<AActor*>(Player.Get()),static_cast<AActor*>(Companion.Get())})if(Holder)
    {TInlineComponentArray<UPrimitiveComponent*> Parts(Holder);for(auto* Part:Parts)for(int32 Slot=0;Slot<Part->GetNumMaterials();++Slot)WarmMaterial(Part->GetMaterial(Slot));}
#if WITH_EDITOR
    FAssetCompilingManager::Get().FinishAllCompilation();
    if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
#endif
    IStreamingManager::Get().StreamAllResources(10.f);
    IStreamingManager::Get().BlockTillAllRequestsFinished(10.f,true);
    ResourcesReadyFrame=GFrameCounter;
    UE_LOG(LogTemp,Display,TEXT("LOADOUT_CAPTURE_RESOURCES_WARM case=%s phase=%d assets=%d textures=%d frame=%llu"),*CaseName,Phase,WarmedCaptureAssets.Num(),WarmedCaptureTextures.Num(),GFrameCounter);
}
bool AHeliosLoadoutQA::HasSelectedFormVoice(AActor* Holder,const FHeliosLoadout& L) const
{
    auto* P=Holder?Holder->FindComponentByClass<UHeliosCharacterPresentationComponent>():nullptr;
    auto* Voice=P?P->GetCharacterVoice():nullptr;
    if(!Voice||Voice->GetKillPoolSize()<=0)return false;
    const FString Prefix=L.CharacterId==TEXT("cfm_yun_summer")?
        TEXT("/Game/Helios/CFMYunSummerMegatron/Audio/VO_YUN_")+L.CharacterVariantId.ToString()+TEXT("_"):
        FString::Printf(TEXT("/Game/Helios/CharacterForms/%s/%s/Audio/"),*L.CharacterId.ToString(),*L.CharacterVariantId.ToString());
    bool Valid=true;int32 Cues=0,Kills=0;
    auto* Map=FindFProperty<FMapProperty>(Voice->GetClass(),TEXT("Sounds"));
    auto* Array=FindFProperty<FArrayProperty>(Voice->GetClass(),TEXT("KillSounds"));
    if(!Map||!Array)return false;
    FScriptMapHelper MapValues(Map,Map->ContainerPtrToValuePtr<void>(Voice));
    auto* MapObject=CastField<FObjectPropertyBase>(Map->ValueProp);
    FScriptArrayHelper ArrayValues(Array,Array->ContainerPtrToValuePtr<void>(Voice));
    auto* ArrayObject=CastField<FObjectPropertyBase>(Array->Inner);
    if(!MapObject||!ArrayObject)return false;
    for(int32 I=0;I<MapValues.GetMaxIndex();++I)if(MapValues.IsValidIndex(I))
    {auto* Source=MapObject->GetObjectPropertyValue(MapValues.GetValuePtr(I));Valid&=Source&&Source->GetPathName().StartsWith(Prefix);++Cues;}
    for(int32 I=0;I<ArrayValues.Num();++I)
    {auto* Source=ArrayObject->GetObjectPropertyValue(ArrayValues.GetRawPtr(I));Valid&=Source&&Source->GetPathName().StartsWith(Prefix);++Kills;}
    UE_LOG(LogTemp,Display,TEXT("LOADOUT_FORM_VOICE case=%s holder=%s prefix=%s cues=%d kills=%d valid=%d"),*CaseName,*GetNameSafe(Holder),*Prefix,Cues,Kills,Valid);
    return Valid&&Cues>=2&&Kills>0;
}
double AHeliosLoadoutQA::WeaponCooldownStamp(AShooterWeapon* W) const
{
    if(auto* S=Cast<AHeliosSunShangXiangWeapon>(W))return S->CooldownRemaining()>0?GetWorld()->GetTimeSeconds()+S->CooldownRemaining():0;
    if(auto* Z=Cast<AHeliosZhongXiaoKuiWeapon>(W))return Z->GetSkillCooldownEnd();
    return 0;
}
void AHeliosLoadoutQA::FinishPairTests()
{
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosLoadoutModeQA"))&&PairLoadout.PrimaryWeaponId==TEXT("cfm_zhongxiaokui"))BeginZXKModeTest();
    else{++Completed;NextCombination();}
}
void AHeliosLoadoutQA::BeginCharacterFormTest()
{
    PauseCompanion();Player->CancelCombatAndControl();CleanupHolderResidue(Player);
    const bool Setup=Player->EquipLoadout(PairLoadout)&&Companion&&Companion->ApplyLoadout(PairLoadout);
    Check(TEXT("form_fixture"),Setup);
    FormPrimary=Player->GetEquippedWeapon();FormPetPrimary=Companion?Companion->GetEquippedWeapon():nullptr;
    FormAmmo=Player->GetAmmo();FormPetAmmo=Companion?Companion->GetAmmo():0;FormHealth=Player->GetHealth();FormPetHealth=Companion?Companion->Health:0;
    FormRefire=FormPrimary.IsValid()?FormPrimary->GetRefireRate():0;FormPetRefire=FormPetPrimary.IsValid()?FormPetPrimary->GetRefireRate():0;
    FormCooldown=WeaponCooldownStamp(FormPrimary.Get());FormPetCooldown=WeaponCooldownStamp(FormPetPrimary.Get());
    FormCycle=0;bFormReloadIssued=bFormReloadStarted=false;Phase=20;Age=0;
}
void AHeliosLoadoutQA::TickCharacterFormTest()
{
    const auto* Definition=FHeliosCharacterRegistry::FindCharacter(PairLoadout.CharacterId);
    if(!Definition||!Companion||!FormPrimary.IsValid()||!FormPetPrimary.IsValid())
    {Check(TEXT("form_driver_survives"),false);++FormCompleted;FinishPairTests();return;}
    if(Phase==20)
    {
        if(Age<.25f)return;
        const auto Before=Player->GetLoadout();
        const int32 Index=Definition->Forms.IndexOfByPredicate([Before](const FHeliosCharacterFormInfo& F){return F.VariantId==Before.CharacterVariantId;});
        const FName Expected=Definition->Forms[(Index+1)%Definition->Forms.Num()].VariantId;
        Player->ToggleCharacterForm();
        auto PetNext=Companion->GetLoadout();PetNext.CharacterVariantId=Expected;
        Check(TEXT("form_H_cycles_existing_character"),Player->GetLoadout().CharacterId==PairLoadout.CharacterId&&Player->GetLoadout().CharacterVariantId==Expected&&Expected!=Before.CharacterVariantId);
        Check(TEXT("form_companion_changes_same_actor"),Companion->ApplyLoadout(PetNext)&&Companion->GetLoadout().CharacterVariantId==Expected);
        Check(TEXT("form_player_preserves_primary_ammo"),Player->GetEquippedWeapon()==FormPrimary.Get()&&Player->GetAmmo()==FormAmmo);
        Check(TEXT("form_companion_preserves_primary_ammo"),Companion->GetEquippedWeapon()==FormPetPrimary.Get()&&Companion->GetAmmo()==FormPetAmmo);
        Check(TEXT("form_does_not_heal"),Player->GetHealth()<=FormHealth&&Companion->Health<=FormPetHealth);
        const double PlayerStamp=WeaponCooldownStamp(FormPrimary.Get()),PetStamp=WeaponCooldownStamp(FormPetPrimary.Get());
        Check(TEXT("form_preserves_weapon_cooldowns"),FMath::IsNearlyEqual(FormRefire,FormPrimary->GetRefireRate())&&FMath::IsNearlyEqual(FormPetRefire,FormPetPrimary->GetRefireRate())&&(FormCooldown<=GetWorld()->GetTimeSeconds()||FMath::Abs(PlayerStamp-FormCooldown)<.03)&&(FormPetCooldown<=GetWorld()->GetTimeSeconds()||FMath::Abs(PetStamp-FormPetCooldown)<.03));
        Check(TEXT("form_player_voice_matches_variant"),HasSelectedFormVoice(Player,Player->GetLoadout()));
        Check(TEXT("form_companion_voice_matches_variant"),HasSelectedFormVoice(Companion,Companion->GetLoadout()));
        if(PairLoadout.CharacterId==TEXT("cfm_yun_summer"))
        {
            const float Speed=Expected==TEXT("SP")?HeliosYunSummer::Setting(true,TEXT("灵魂武者移速倍率"),1.2f,1,3):1.f;
            const float PlayerSpeed=Expected==TEXT("SP")?HeliosYunSummer::Setting(false,TEXT("灵魂武者移速倍率"),1.2f,1,3):1.f;
            Check(TEXT("yun_form_player_movement_multiplier"),FMath::IsNearlyEqual(Player->GetEquipmentMovementMultiplier(),PlayerSpeed));
            Check(TEXT("yun_form_companion_original_parameters"),FMath::IsNearlyEqual(Companion->Settings.MovementMultiplier,Speed)&&FMath::IsNearlyEqual(Companion->Settings.MaxHealth,HeliosYunSummer::Setting(true,TEXT("生命"),300,1,1000000))&&FMath::IsNearlyEqual(Companion->Settings.SleepSeconds,HeliosYunSummer::Setting(true,TEXT("倒地恢复时间"),30,.1,600))&&FMath::IsNearlyEqual(Companion->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),96.f));
        }
        ++FormCycle;Age=0;
        // The form is installed while neither holder attacks. Finish its real
        // render resources here; native weapon clocks are never paused.
        WarmCaptureResources();
        const FString State=TEXT("form_")+Expected.ToString();Capture(*State);
        DeferAfterCapture([this,Definition]
        {
            if(FormCycle<Definition->Forms.Num())return;
            Check(TEXT("form_cycle_returns_original_variant"),Player->GetLoadout()==PairLoadout&&Companion->GetLoadout()==PairLoadout);
            Companion->Restore(0,17.5f);
            auto DownNext=PairLoadout;const int32 OriginalIndex=Definition->Forms.IndexOfByPredicate([this](const FHeliosCharacterFormInfo& F){return F.VariantId==PairLoadout.CharacterVariantId;});
            DownNext.CharacterVariantId=Definition->Forms[(OriginalIndex+1)%Definition->Forms.Num()].VariantId;
            const bool Changed=Companion->ApplyLoadout(DownNext);
            Check(TEXT("form_companion_down_preserves_remaining"),Changed&&Companion->IsSleeping()&&Companion->State==EHeliosPetState::Sleeping&&Companion->Health==0&&FMath::IsNearlyEqual(Companion->SleepRemaining,17.5f)&&Companion->GetEquippedWeapon()==FormPetPrimary.Get()&&Companion->GetAmmo()==FormPetAmmo);
            Check(TEXT("form_companion_down_restores_pair"),Companion->ApplyLoadout(PairLoadout)&&Companion->IsSleeping()&&Companion->Health==0&&FMath::IsNearlyEqual(Companion->SleepRemaining,17.5f));
            // Advance this fixture to the ordinary production wake path only
            // after observing the untouched down-state countdown above.
            Companion->SleepRemaining=.01f;Companion->Target.Reset();
            if(auto* Fixture=Cast<AHeliosLoadoutQAPet>(Companion))Fixture->bQAWaitForIdle=true;
            Companion->SetActorTickEnabled(true);NextIdleWaitDiagnosticAt=0;Phase=25;Age=0;
        });return;
    }
    if(Phase==25)
    {
        DiagnoseIdleWait(TEXT("post_wake"),false);
        if(!IsCompanionActuallyIdle()&&Age<6.f)return;
        DiagnoseIdleWait(TEXT("post_wake"),true);
        Check(TEXT("form_companion_down_wakes_for_next_test"),IsCompanionActuallyIdle());
        if(auto* Fixture=Cast<AHeliosLoadoutQAPet>(Companion))Fixture->bQAWaitForIdle=false;
        PauseCompanion();Companion->Health=FMath::Min(FormPetHealth,Companion->Settings.MaxHealth);
        if(FormPrimary->UsesAmmunition()){Phase=21;Age=0;Player->StartFireFromAutomation();}
        else{++FormCompleted;if(PairLoadout.CharacterId==TEXT("cfm_yun_summer"))BeginYunCheersTest();else FinishPairTests();}
        return;
    }
    if(!bFormReloadIssued&&(Player->GetAmmo()<FormPrimary->GetMagazineSize()||Age>=4.f))
    {
        bFormReloadIssued=true;Player->StopFireFromAutomation();Player->StartReloadFromAutomation();bFormReloadStarted=Player->IsReloading();
        const auto Before=Player->GetLoadout();const int32 Ammo=Player->GetAmmo();
        Check(TEXT("form_reload_actually_started"),bFormReloadStarted);
        Player->ToggleCharacterForm();
        Check(TEXT("form_H_blocked_during_reload"),bFormReloadStarted&&Player->GetLoadout()==Before&&Player->IsReloading()&&Player->GetEquippedWeapon()==FormPrimary.Get()&&Player->GetAmmo()==Ammo);
        Age=0;return;
    }
    if(bFormReloadIssued&&((bFormReloadStarted&&!Player->IsReloading())||Age>=25.f))
    {
        Check(TEXT("form_reload_completes_same_primary"),bFormReloadStarted&&!Player->IsReloading()&&Player->GetEquippedWeapon()==FormPrimary.Get()&&Player->GetLoadout()==PairLoadout);
        ++FormCompleted;if(PairLoadout.CharacterId==TEXT("cfm_yun_summer"))BeginYunCheersTest();else FinishPairTests();
    }
}
void AHeliosLoadoutQA::BeginYunCheersTest()
{
    Player->CancelCombatAndControl();PauseCompanion();CleanupHolderResidue(Player);
    FormPrimary=Player->GetEquippedWeapon();FormPetPrimary=Companion?Companion->GetEquippedWeapon():nullptr;
    FormAmmo=Player->GetAmmo();FormPetAmmo=Companion?Companion->GetAmmo():0;FormHealth=Player->GetHealth();FormPetHealth=Companion?Companion->Health:0;
    FormCooldown=WeaponCooldownStamp(FormPrimary.Get());FormPetCooldown=WeaponCooldownStamp(FormPetPrimary.Get());
    auto* FP=Player->GetCharacterPresentation();auto* TP=Companion?Companion->GetCharacterPresentation():nullptr;
    // Observe both authoritative component clocks after their ticks. A cold
    // frame can be 0.4 s, so observing one before its tick is not a clock test.
    if(FP)AddTickPrerequisiteComponent(FP);
    if(TP)AddTickPrerequisiteComponent(TP);
    GestureAudioStart=FP?FP->GetCharacterGestureAudioEventCount():0;GestureEffectStart=FP?FP->GetCharacterGestureEffectEventCount():0;
    PetGestureAudioStart=TP?TP->GetCharacterGestureAudioEventCount():0;PetGestureEffectStart=TP?TP->GetCharacterGestureEffectEventCount():0;
    Player->StartCharacterInteraction();const bool PetStart=TP&&TP->BeginCharacterGesture(TEXT("Cheers"));
    Check(TEXT("yun_cheers_start_FP_TP"),FP&&FP->IsCharacterGesturePlaying()&&PetStart&&TP->IsCharacterGesturePlaying());
    Check(TEXT("yun_cheers_independent_of_primary"),Player->GetEquippedWeapon()==FormPrimary.Get()&&Companion&&Companion->GetEquippedWeapon()==FormPetPrimary.Get()&&Player->GetLoadout()==PairLoadout&&Companion->GetLoadout()==PairLoadout);
    bGestureCaptured=false;Phase=22;Age=0;NextCheersDiagnosticAt=0;
    DiagnoseYunCheers(TEXT("natural_begin"));
}
void AHeliosLoadoutQA::AimAtFixtureTarget()
{
    if(!Player||!Target||!Player->GetController())return;
    FVector Eye;FRotator Before;Player->GetActorEyesViewPoint(Eye,Before);
    const FRotator Aim=(Target->GetActorLocation()+FVector(0,0,25)-Eye).Rotation();
    Player->GetController()->SetControlRotation(Aim);
    // This source gun reads the actual camera transform synchronously when
    // StartFiring is called; its normal camera update happens later in the frame.
    if(Player->FirstPersonCamera)Player->FirstPersonCamera->SetWorldRotation(Aim);
}
void AHeliosLoadoutQA::DiagnoseYunCheers(const TCHAR* Stage)
{
    auto* FP=Player?Player->GetCharacterPresentation():nullptr;
    auto* TP=Companion?Companion->GetCharacterPresentation():nullptr;
    auto Row=MakeShared<FJsonObject>();
    Row->SetStringField(TEXT("case"),CaseName);Row->SetStringField(TEXT("kind"),TEXT("yun_cheers"));Row->SetStringField(TEXT("stage"),Stage);
    Row->SetNumberField(TEXT("phase"),Phase);Row->SetNumberField(TEXT("phase_age"),Age);Row->SetNumberField(TEXT("world_time"),GetWorld()->GetTimeSeconds());Row->SetNumberField(TEXT("frame"),GFrameCounter);
    Row->SetNumberField(TEXT("fp_gesture_age"),FP?FP->GetCharacterGestureAge():-1);Row->SetNumberField(TEXT("tp_gesture_age"),TP?TP->GetCharacterGestureAge():-1);
    Row->SetBoolField(TEXT("fp_playing"),FP&&FP->IsCharacterGesturePlaying());Row->SetBoolField(TEXT("tp_playing"),TP&&TP->IsCharacterGesturePlaying());
    Row->SetNumberField(TEXT("clock_difference"),FP&&TP?FMath::Abs(FP->GetCharacterGestureAge()-TP->GetCharacterGestureAge()):-1);
    FVector Eye=FVector::ZeroVector;FRotator View=FRotator::ZeroRotator;if(Player)Player->GetActorEyesViewPoint(Eye,View);
    const FVector AimAt=Target?Target->GetActorLocation()+FVector(0,0,25):FVector::ZeroVector;
    const FRotator Needed=(AimAt-Eye).Rotation();
    Row->SetStringField(TEXT("player_eye"),Eye.ToString());Row->SetStringField(TEXT("actual_view_rotation"),View.ToString());Row->SetStringField(TEXT("target_aim_point"),AimAt.ToString());
    Row->SetStringField(TEXT("needed_rotation"),Needed.ToString());Row->SetNumberField(TEXT("pitch_error_degrees"),FMath::Abs(FMath::FindDeltaAngleDegrees(View.Pitch,Needed.Pitch)));Row->SetNumberField(TEXT("yaw_error_degrees"),FMath::Abs(FMath::FindDeltaAngleDegrees(View.Yaw,Needed.Yaw)));
    if(Player&&Player->GetController())Row->SetStringField(TEXT("control_rotation"),Player->GetController()->GetControlRotation().ToString());
    Row->SetNumberField(TEXT("player_ammo"),Player?Player->GetAmmo():-1);Row->SetNumberField(TEXT("companion_ammo"),Companion?Companion->GetAmmo():-1);
    Row->SetNumberField(TEXT("player_damage"),PlayerDamage);Row->SetNumberField(TEXT("companion_damage"),CompanionDamage);
    Row->SetNumberField(TEXT("player_damage_at_interrupt"),PlayerPrimaryDamageStart);Row->SetNumberField(TEXT("companion_damage_at_interrupt"),ModeDamageStart);
    Row->SetBoolField(TEXT("same_player_primary"),Player&&Player->GetEquippedWeapon()==FormPrimary.Get());Row->SetBoolField(TEXT("same_companion_primary"),Companion&&Companion->GetEquippedWeapon()==FormPetPrimary.Get());
    Diagnostics.Add(MakeShared<FJsonValueObject>(Row));FString Serialized;FJsonSerializer::Serialize(Row,TJsonWriterFactory<>::Create(&Serialized));
    UE_LOG(LogTemp,Display,TEXT("LOADOUT_YUN_CHEERS_DIAGNOSTIC %s"),*Serialized);
}
void AHeliosLoadoutQA::TickYunCheersTest()
{
    auto* FP=Player->GetCharacterPresentation();auto* TP=Companion?Companion->GetCharacterPresentation():nullptr;
    if(!FP||!TP){Check(TEXT("yun_cheers_driver_survives"),false);++CheersCompleted;FinishPairTests();return;}
    if((Phase==22||Phase==23||Phase==24)&&GetWorld()->GetTimeSeconds()>=NextCheersDiagnosticAt)
    {DiagnoseYunCheers(TEXT("observe"));NextCheersDiagnosticAt=GetWorld()->GetTimeSeconds()+.25;}
    if(Phase==22)
    {
        if(Age>=.35f&&!bGestureCaptured)
        {
            bGestureCaptured=true;
            auto* FPArms=FP->GetGestureArmsMesh();auto* FPCup=FP->GetGestureCupMesh();auto* TPCup=TP->GetGestureCupMesh();
            Check(TEXT("yun_cheers_form_meshes_visible"),FP->IsCharacterGesturePlaying()&&TP->IsCharacterGesturePlaying()&&FPArms&&FPCup&&TPCup&&FPArms->IsVisible()&&FPCup->IsVisible()&&TPCup->IsVisible()&&FPArms->GetSkeletalMeshAsset()&&FPArms->GetSkeletalMeshAsset()->GetName().Contains(PairLoadout.CharacterVariantId.ToString())&&TPCup->GetSkeletalMeshAsset()&&TPCup->GetSkeletalMeshAsset()->GetName().Contains(PairLoadout.CharacterVariantId.ToString()));
            Check(TEXT("yun_cheers_FP_TP_source_clock"),FMath::Abs(FP->GetCharacterGestureAge()-TP->GetCharacterGestureAge())<.08f&&FP->GetCharacterGestureAge()>.2f);
            DiagnoseYunCheers(TEXT("natural_clock_assert"));
            Capture(TEXT("cheers"));return;
        }
        const bool Finished=!FP->IsCharacterGesturePlaying()&&!TP->IsCharacterGesturePlaying();
        if(!Finished&&Age<FMath::Max(FP->GetCharacterGestureDuration(),TP->GetCharacterGestureDuration())+2.f)return;
        Check(TEXT("yun_cheers_natural_finish"),Finished);
        Check(TEXT("yun_cheers_preserves_primary_ammo_health"),Player->GetEquippedWeapon()==FormPrimary.Get()&&Companion->GetEquippedWeapon()==FormPetPrimary.Get()&&Player->GetAmmo()==FormAmmo&&Companion->GetAmmo()==FormPetAmmo&&Player->GetHealth()<=FormHealth&&Companion->Health<=FormPetHealth);
        Check(TEXT("yun_cheers_preserves_skill_cooldowns"),(FormCooldown<=GetWorld()->GetTimeSeconds()||FMath::Abs(WeaponCooldownStamp(FormPrimary.Get())-FormCooldown)<.03)&&(FormPetCooldown<=GetWorld()->GetTimeSeconds()||FMath::Abs(WeaponCooldownStamp(FormPetPrimary.Get())-FormPetCooldown)<.03));
        NaturalGestureAudio=FP->GetCharacterGestureAudioEventCount()-GestureAudioStart;NaturalGestureEffect=FP->GetCharacterGestureEffectEventCount()-GestureEffectStart;
        NaturalPetGestureAudio=TP->GetCharacterGestureAudioEventCount()-PetGestureAudioStart;NaturalPetGestureEffect=TP->GetCharacterGestureEffectEventCount()-PetGestureEffectStart;
        Check(TEXT("yun_cheers_natural_audio_once_FP_TP"),NaturalGestureAudio==1&&NaturalPetGestureAudio==1);
        Check(TEXT("yun_cheers_natural_effect_once_FP_TP"),NaturalGestureEffect==1&&NaturalPetGestureEffect==1);
        Check(TEXT("yun_cheers_weapon_visual_restored"),FP->GetArmsMesh()&&FP->GetArmsMesh()->IsVisible()&&(!FP->GetGestureCupMesh()||!FP->GetGestureCupMesh()->IsVisible())&&(!TP->GetGestureCupMesh()||!TP->GetGestureCupMesh()->IsVisible())&&!FormPrimary->IsHidden());
        Check(TEXT("yun_cheers_natural_native_driver_hidden"),IsNativeDriverHidden());
        // Natural completion is checked on its actual end frame above. The
        // paused photo fixture must then permit the ordinary following tick
        // to restore the weapon idle, just as a live companion does.
        if(auto* Fixture=Cast<AHeliosLoadoutQAPet>(Companion))Fixture->bQAWaitForIdle=true;
        Companion->SetActorTickEnabled(true);NextIdleWaitDiagnosticAt=0;Phase=28;Age=0;return;
    }
    if(Phase==28)
    {
        DiagnoseIdleWait(TEXT("post_cheers_idle"),false);
        if(!IsCompanionActuallyIdle()&&Age<6.f)return;
        DiagnoseIdleWait(TEXT("post_cheers_idle"),true);Check(TEXT("yun_cheers_natural_returns_to_idle"),IsCompanionActuallyIdle());
        PauseCompanion();if(auto* Fixture=Cast<AHeliosLoadoutQAPet>(Companion))Fixture->bQAWaitForIdle=false;
        Capture(TEXT("cheers_finished"));DeferAfterCapture([this,FP,TP]
        {
            GestureAudioStart=FP->GetCharacterGestureAudioEventCount();GestureEffectStart=FP->GetCharacterGestureEffectEventCount();PetGestureAudioStart=TP->GetCharacterGestureAudioEventCount();PetGestureEffectStart=TP->GetCharacterGestureEffectEventCount();
            Player->StartCharacterInteraction();TP->BeginCharacterGesture(TEXT("Cheers"));bGestureCaptured=false;Phase=23;Age=0;
        });return;
    }
    if(Phase==23&&Age>=.35f)
    {
        Check(TEXT("yun_cheers_interrupt_fixture"),FP->IsCharacterGesturePlaying()&&TP->IsCharacterGesturePlaying());
        Check(TEXT("yun_cheers_interrupt_audio_once_FP_TP"),FP->GetCharacterGestureAudioEventCount()-GestureAudioStart==1&&TP->GetCharacterGestureAudioEventCount()-PetGestureAudioStart==1);
        Check(TEXT("yun_cheers_interrupt_effect_once_FP_TP"),FP->GetCharacterGestureEffectEventCount()-GestureEffectStart==1&&TP->GetCharacterGestureEffectEventCount()-PetGestureEffectStart==1);
        FormAmmo=Player->GetAmmo();FormPetAmmo=Companion->GetAmmo();PlayerPrimaryDamageStart=PlayerDamage;ModeDamageStart=CompanionDamage;
        DiagnoseYunCheers(TEXT("interrupt_before_reaim"));AimAtFixtureTarget();DiagnoseYunCheers(TEXT("interrupt_aimed"));
        Player->StartFireFromAutomation();Companion->Target=Target;Companion->SetActorTickEnabled(true);
        Phase=24;Age=0;return;
    }
    if(Phase==24)AimAtFixtureTarget();
    if(Phase==24&&((PlayerDamage>PlayerPrimaryDamageStart&&CompanionDamage>ModeDamageStart&&!TP->IsCharacterGesturePlaying())||Age>=6.f))
    {
        DiagnoseYunCheers(TEXT("interrupt_final"));
        Check(TEXT("yun_cheers_fire_interrupts_same_primary"),!FP->IsCharacterGesturePlaying()&&!TP->IsCharacterGesturePlaying()&&Player->GetEquippedWeapon()==FormPrimary.Get()&&Companion->GetEquippedWeapon()==FormPetPrimary.Get()&&Player->GetLoadout()==PairLoadout);
        Check(TEXT("yun_cheers_companion_primary_interrupts_and_hits"),CompanionDamage>ModeDamageStart);
        Player->StopFireFromAutomation();Check(TEXT("yun_cheers_interrupted_primary_hits"),PlayerDamage>PlayerPrimaryDamageStart);
        Check(TEXT("yun_cheers_interruption_does_not_refill"),Player->GetAmmo()<=FormAmmo&&Companion->GetAmmo()<=FormPetAmmo);
        Check(TEXT("yun_cheers_no_duplicate_audio_effects"),FP->GetCharacterGestureAudioEventCount()-GestureAudioStart==1&&TP->GetCharacterGestureAudioEventCount()-PetGestureAudioStart==1&&FP->GetCharacterGestureEffectEventCount()-GestureEffectStart==1&&TP->GetCharacterGestureEffectEventCount()-PetGestureEffectStart==1);
        Check(TEXT("yun_cheers_interrupted_weapon_visible"),FP->GetArmsMesh()&&FP->GetArmsMesh()->IsVisible()&&!FormPrimary->IsHidden()&&(!FP->GetGestureCupMesh()||!FP->GetGestureCupMesh()->IsVisible()));
        Check(TEXT("yun_cheers_interrupted_native_driver_hidden"),IsNativeDriverHidden());
        Capture(TEXT("cheers_interrupted"));DeferAfterCapture([this]{PauseCompanion();++CheersCompleted;FinishPairTests();});
    }
}
void AHeliosLoadoutQA::BeginPlayerPrimary()
{
    PauseCompanion();PlayerPrimaryDamageStart=PlayerDamage;PlayerPrimaryInitialAmmo=Player->GetAmmo();
    if(Player->GetLoadout().CharacterId==TEXT("cfm_chan")&&Player->GetWeaponId()==TEXT("cfm_chan_zeus"))
    {
        auto* Auto=Player->FindComponentByClass<UHeliosAutonomyComponent>();
        if(!Auto)
        {
            // Match the MCP subsystem's on-demand component initialization.
            Auto=NewObject<UHeliosAutonomyComponent>(Player);
            Player->AddInstanceComponent(Auto);
            Auto->RegisterComponent();
        }
        if(Auto)
        {
            FVector Eye;FRotator View;Player->GetActorEyesViewPoint(Eye,View);
            if(Target&&Player->GetController())Player->GetController()->SetControlRotation((Target->GetActorLocation()+FVector(0,0,25)-Eye).Rotation());
            Auto->Sense();
            auto Goal=MakeShared<FJsonObject>();Goal->SetStringField(TEXT("kind"),TEXT("engage"));Goal->SetStringField(TEXT("actor_id"),TEXT("loadout_qa_target"));
            const auto Reply=Auto->SetGoal(Goal);FString ReplyText;
            if(Reply)FJsonSerializer::Serialize(Reply.ToSharedRef(),TJsonWriterFactory<>::Create(&ReplyText));else ReplyText=TEXT("null");
            UE_LOG(LogTemp,Display,TEXT("LOADOUT_QA_AUTONOMY case=%s visible=%d target_id=%s player_eye=%s target=%s reply=%s"),*CaseName,Auto->IsVisible(Target),Target?*Target->GetRuntimeActorId():TEXT("null"),*Eye.ToCompactString(),Target?*Target->GetActorLocation().ToCompactString():TEXT("null"),*ReplyText);
        }
        Check(TEXT("default_pair_accepts_autonomy"),Auto&&Auto->IsActive());
    }
    else Player->StartFireFromAutomation();
    Phase=1;Age=0;
}
bool AHeliosLoadoutQA::IsHunterSourcePose(bool Heavy) const
{
    auto* W=Companion?Cast<AHeliosSunShangXiangWeapon>(Companion->GetEquippedWeapon()):nullptr;
    auto* Hand=W?Cast<UHeliosHandAnimInstance>(W->GetFirstPersonMesh()->GetAnimInstance()):nullptr;
    if(!Hand||!Hand->Sequence)return false;
    const FString Name=Hand->Sequence->GetName();
    return Heavy?Name.Contains(TEXT("TP_FireHeavy")):Name.Contains(TEXT("TP_FireSlightFirst"))||Name.Contains(TEXT("TP_FireSlightSec"));
}
void AHeliosLoadoutQA::BeginHunterBasicTest(bool Heavy)
{
    PauseCompanion();if(Companion)Companion->Target=Target;
    HunterDamage=0;HunterContacts=0;bHunterStarted=false;bHunterContactDuringCooldown=false;bHunterPoseCaptured=false;
    HeliosSSX::FTimeline Clock;Clock.Begin(Heavy?HeliosSSX::EAttack::Heavy:HeliosSSX::EAttack::Light);
    auto* W=Companion?Cast<AHeliosSunShangXiangWeapon>(Companion->GetEquippedWeapon()):nullptr;
    HunterTimeout=Clock.Duration(false)/FMath::Max(.1f,HeliosSSX::Tempo(W,true))+3.f;
    Phase=Heavy?7:6;Age=0;
}
void AHeliosLoadoutQA::TickHunterBasicTest(bool Heavy)
{
    auto* W=Companion?Cast<AHeliosSunShangXiangWeapon>(Companion->GetEquippedWeapon()):nullptr;
    if(!W){Check(Heavy?TEXT("companion_ssx_heavy_hits"):TEXT("companion_ssx_light_hits"),false);BeginPlayerPrimary();return;}
    if(IsHunterSourcePose(Heavy))bHunterStarted=true;
    if(!bHunterStarted)
    {
        // Pulse only the public primary/secondary API. No skill input is issued
        // while these two original third-person contact timelines are observed.
        W->StopFiring();W->StopSecondaryFire();
        if(Heavy)W->StartSecondaryFire();else W->StartFiring();
        W->StopFiring();W->StopSecondaryFire();
    }
    if(IsHunterSourcePose(Heavy)&&!bHunterPoseCaptured)
    {bHunterPoseCaptured=true;Capture(Heavy?TEXT("companion_heavy"):TEXT("companion_light"));return;}
    const bool Complete=bHunterStarted&&Companion->GetCharacterPresentation()&&!Companion->GetCharacterPresentation()->IsSourceActionPlaying();
    if(!Complete&&Age<HunterTimeout)return;
    Check(Heavy?TEXT("companion_ssx_heavy_hits"):TEXT("companion_ssx_light_hits"),HunterDamage>0&&HunterContacts>=2);
    Check(Heavy?TEXT("companion_ssx_heavy_during_skill_cooldown"):TEXT("companion_ssx_light_during_skill_cooldown"),bHunterContactDuringCooldown);
    if(Heavy)BeginPlayerPrimary();else BeginHunterBasicTest(true);
}
void AHeliosLoadoutQA::BeginCompanionReloadTest()
{
    auto* W=Companion?Companion->GetEquippedWeapon():nullptr;
    if(!W||!W->UsesAmmunition()){BeginPlayerPrimary();return;}
    Companion->SuspendCombat();CleanupHolderResidue(Companion);
    NativeCompanionMagazine=W->GetMagazineSize();CompanionReloadsStart=Companion->Reloads;ReloadPrimary=W;bReloadSwapAttempted=false;bCompanionReloadCaptured=false;CompanionReloadCaptureAt=0;
    // A one-round QA magazine reaches the real empty-magazine reload path
    // quickly; damage, refire rate and all source action clocks stay unchanged.
    W->ConfigureMagazine(1);Companion->Target=Target;Companion->SetActorTickEnabled(true);
    ReloadTimeout=FMath::Max(12.f,2*W->GetRefireRate()+25.f);Phase=8;Age=0;
}
void AHeliosLoadoutQA::FinishCompanionReloadTest()
{
    auto* W=Companion?Companion->GetEquippedWeapon():nullptr;
    Check(TEXT("companion_auto_reload_started"),bReloadSwapAttempted);
    Check(TEXT("companion_auto_reload_completes"),Companion&&W&&Companion->Reloads>CompanionReloadsStart&&!Companion->IsReloading()&&W->GetBulletCount()==1&&Companion->GetAmmo()==1);
    PauseCompanion();
    if(Companion&&W)
    {
        Check(TEXT("companion_reload_restores_original_character"),Companion->ApplyLoadout(PairLoadout));
        W=Companion->GetEquippedWeapon();if(W)W->ConfigureMagazine(NativeCompanionMagazine);
        Check(TEXT("companion_qa_magazine_restored"),W&&W->GetMagazineSize()==NativeCompanionMagazine&&Companion->GetAmmo()==NativeCompanionMagazine);
    }
    BeginPlayerPrimary();
}
bool AHeliosLoadoutQA::HasZXKThirdPersonPose(const FString& Fragment) const
{
    auto* P=Companion?Companion->GetCharacterPresentation():nullptr;
    auto* Node=P&&P->GetArmsMesh()?P->GetArmsMesh()->GetSingleNodeInstance():nullptr;
    return Node&&Node->GetCurrentAsset()&&Node->GetCurrentAsset()->GetName().Contains(Fragment);
}
bool AHeliosLoadoutQA::HasZXKThirdPersonClock() const
{
    auto* W=Cast<AHeliosZhongXiaoKuiWeapon>(ModePrimary.Get());
    auto* Hand=W?Cast<UHeliosHandAnimInstance>(W->GetFirstPersonMesh()->GetAnimInstance()):nullptr;
    auto* P=Companion?Companion->GetCharacterPresentation():nullptr;
    auto* Node=P&&P->GetArmsMesh()?P->GetArmsMesh()->GetSingleNodeInstance():nullptr;
    auto* Clip=Node?Cast<UAnimSequence>(Node->GetCurrentAsset()):nullptr;
    if(!Hand||!Hand->Sequence||!Clip||!Clip->GetName().Contains(Hand->Sequence->GetName()))return false;
    const FString SourceName=Hand->Sequence->GetName();
    const bool Loop=SourceName.Contains(TEXT("Idle"))||SourceName.Contains(TEXT("Move"))||SourceName.Contains(TEXT("Run_"));
    const float PhaseTime=Hand->SampleSeconds/FMath::Max(.001f,Hand->Sequence->GetPlayLength());
    const float Expected=Loop?FMath::Fmod(FMath::Max(0.f,PhaseTime),1.f):FMath::Clamp(PhaseTime,0.f,1.f);
    return FMath::Abs(Expected-Node->GetCurrentTime()/FMath::Max(.001f,Clip->GetPlayLength()))<.002f;
}
bool AHeliosLoadoutQA::HasZXKThirdPersonDartContact() const
{
    auto* W=Cast<AHeliosZhongXiaoKuiWeapon>(ModePrimary.Get());
    auto* P=Companion?Companion->GetCharacterPresentation():nullptr;
    auto* Body=P?P->GetArmsMesh():nullptr;
    auto* Dart=P?P->GetAlternateWeaponMesh():nullptr;
    auto* SourceDart=W?W->GetDartMesh():nullptr;
    if(!Body||!Dart||!Dart->IsVisible()||!SourceDart||!W->ShouldDisplayDart())return false;
    const FTransform Contact=SourceDart->GetComponentTransform().GetRelativeTransform(W->GetFirstPersonMesh()->GetSocketTransform(TEXT("b_RightHand")));
    const FTransform Expected=Contact*Body->GetSocketTransform(TEXT("b_RightHand"));
    const FTransform Actual=Dart->GetComponentTransform();
    return FVector::Distance(Expected.GetLocation(),Actual.GetLocation())<.05f
        &&Expected.GetRotation().AngularDistance(Actual.GetRotation())<FMath::DegreesToRadians(.1f)
        &&Expected.GetScale3D().Equals(Actual.GetScale3D(),.001f);
}
void AHeliosLoadoutQA::HideModeTargetForPhotography()
{
    RestoreModeTargetVisibility();
    if(!IsValid(Target))return;
    // Matrix photography suppresses only the fixture target's rendering. Its actor,
    // collision, damage chain and position remain identical to the combat test.
    // Hide owned weapon actors too, so an orphaned target gun cannot cover hands.
    for(TActorIterator<AActor> It(GetWorld());It;++It)
        if(AActor* Actor=*It;OwnedBy(Actor,Target))
        {if(!ModeTargetVisibility.Contains(Actor))ModeTargetVisibility.Add(Actor,Actor->IsHidden());Actor->SetActorHiddenInGame(true);}
    UE_LOG(LogTemp,Display,TEXT("LOADOUT_MODE_QA_TARGET_HIDDEN case=%s target=%s actors=%d collision=%d"),*CaseName,*GetNameSafe(Target),ModeTargetVisibility.Num(),Target->GetActorEnableCollision());
}
void AHeliosLoadoutQA::RestoreModeTargetVisibility()
{
    for(const auto& Item:ModeTargetVisibility)if(auto* Actor=Item.Key.Get())Actor->SetActorHiddenInGame(Item.Value);
    ModeTargetVisibility.Reset();
}
void AHeliosLoadoutQA::BeginZXKModeTest()
{
    PauseCompanion();Player->CancelCombatAndControl();CleanupHolderResidue(Player);
    const bool Setup=Player->EquipLoadout(PairLoadout)&&Companion&&Companion->ApplyLoadout(PairLoadout);
    ModePrimary=Player->GetEquippedWeapon();
    auto* W=Cast<AHeliosZhongXiaoKuiWeapon>(ModePrimary.Get());
    Check(TEXT("zxk_mode_fixture"),Setup&&W&&W->GetForm()==0&&Companion->GetCharacterPresentation()->IsReady());
    if(!Setup||!W){++ModeCompleted;++Completed;NextCombination();return;}
    // Same-pair ApplyLoadout deliberately preserves an unfinished native
    // reload/recovery. Let both real drivers settle before the idle assertion.
    if(auto* Fixture=Cast<AHeliosLoadoutQAPet>(Companion))Fixture->bQAWaitForIdle=true;
    Companion->SetActorTickEnabled(true);
    SetTickGroup(TG_PostUpdateWork);AddTickPrerequisiteComponent(Companion->GetCharacterPresentation());
    AddTickPrerequisiteComponent(Player->GetCharacterPresentation());
    Phase=27;Age=0;NextIdleWaitDiagnosticAt=0;
}
void AHeliosLoadoutQA::CompleteZXKModeFixture()
{
    auto* W=Cast<AHeliosZhongXiaoKuiWeapon>(ModePrimary.Get());if(!W||!Companion){FinishZXKModeTest();return;}
    PauseCompanion();if(auto* Fixture=Cast<AHeliosLoadoutQAPet>(Companion))Fixture->bQAWaitForIdle=false;
    // Only this explicitly requested QA binds the player's actual native FP
    // driver to a companion body. Production companions keep their original A
    // driver, guard, damage and attack strategy throughout this test.
    Companion->GetCharacterPresentation()->BindWeaponSource(W);
    HideModeTargetForPhotography();
    SetTickGroup(TG_PostUpdateWork);AddTickPrerequisiteComponent(Companion->GetCharacterPresentation());
    AddTickPrerequisiteComponent(Player->GetCharacterPresentation());
    UE_LOG(LogTemp,Display,TEXT("LOADOUT_MODE_QA_NATIVE_FP_DRIVER case=%s source=%s tp_owner=%s"),*CaseName,*W->GetPathName(),*Companion->GetPathName());
    Check(TEXT("zxk_tp_A_idle"),HasZXKThirdPersonPose(TEXT("Idle"))&&W->GetCurrentSourceAction().IsNone());
    Capture(TEXT("mode_A_idle"));DeferAfterCapture([this]{StartZXKModePhase(9);});
}
void AHeliosLoadoutQA::StartZXKModePhase(int32 Stage)
{
    auto* W=Cast<AHeliosZhongXiaoKuiWeapon>(ModePrimary.Get());if(!W){FinishZXKModeTest();return;}
    W->SetActorTickEnabled(true);Player->CancelCombatAndControl();
    Phase=Stage;Age=0;ModeDamageStart=PlayerDamage;ModeDartAmmo=W->GetDartAmmo();if(Stage==16)bModeTransformFailureRecorded=false;
    bModePoseSeen=bModeContactSeen=bModeClockSeen=bModeIssued=false;
    if(Stage==9)Player->ToggleCalamityBackup();
    else if(Stage==10||Stage==14)Player->StartFireFromAutomation();
    else if(Stage==12){Player->StartSecondaryAttack();Player->StopSecondaryAttack();}
    else if(Stage==13||Stage==16)Player->ToggleZhongXiaoKuiRanged();
    else if(Stage==15)Player->StartReloadFromAutomation();
    else if(Stage==17)Player->ToggleCalamityBackup();
    else if(Stage==18||Stage==19)
    {
        // Sample the actual authored move driver in QA without moving either
        // capsule out of the fixture or changing combat/animation tempo.
        W->SetActorTickEnabled(false);
        HeliosZXK::Sample(W->GetFirstPersonMesh(),HeliosZXK::Animation(true,Stage==18?FName("B_Move"):FName("C_Move")),0,true);
    }
}
void AHeliosLoadoutQA::FinishZXKModeTest()
{
    RestoreModeTargetVisibility();
    if(auto* W=ModePrimary.Get())W->SetActorTickEnabled(true);
    Player->CancelCombatAndControl();
    auto* DefaultPrimary=Companion?Cast<AHeliosZhongXiaoKuiWeapon>(Companion->GetEquippedWeapon()):nullptr;
    Check(TEXT("zxk_companion_default_A_retained"),DefaultPrimary&&DefaultPrimary->GetForm()==0&&DefaultPrimary!=ModePrimary.Get());
    if(Companion&&Companion->GetCharacterPresentation())
    {
        Companion->GetCharacterPresentation()->BindWeaponSource(Companion->GetEquippedWeapon());
        RemoveTickPrerequisiteComponent(Companion->GetCharacterPresentation());
    }
    RemoveTickPrerequisiteComponent(Player->GetCharacterPresentation());ModePrimary.Reset();
    ++ModeCompleted;++Completed;NextCombination();
}
void AHeliosLoadoutQA::TickZXKModeTest()
{
    auto* W=Cast<AHeliosZhongXiaoKuiWeapon>(ModePrimary.Get());
    auto* P=Companion?Companion->GetCharacterPresentation():nullptr;
    if(!W||!P){Check(TEXT("zxk_mode_driver_survives"),false);FinishZXKModeTest();return;}
    if(Phase==27)
    {
        auto* Hand=W->GetFirstPersonMesh()?Cast<UHeliosHandAnimInstance>(W->GetFirstPersonMesh()->GetAnimInstance()):nullptr;
        const bool NativeIdle=W->GetForm()==0&&!W->IsChangingForm()&&W->GetCurrentSourceAction().IsNone()&&Hand&&Hand->Sequence==HeliosZXK::Animation(true,TEXT("Idle"));
        const bool Ready=NativeIdle&&IsCompanionActuallyIdle();
        DiagnoseIdleWait(TEXT("mode_A_idle"),false);
        if(!Ready&&Age<6.f)return;
        DiagnoseIdleWait(TEXT("mode_A_idle"),true);Check(TEXT("zxk_mode_fixture_idle_ready"),Ready);CompleteZXKModeFixture();return;
    }
    auto* Body=P->GetArmsMesh();
    const bool HasSheathBone=Body&&Body->GetBoneIndex(TEXT("b_LeftWeapon"))!=INDEX_NONE;
    const bool Preserved=Player->GetEquippedWeapon()==W&&Player->GetLoadout().CharacterId==PairLoadout.CharacterId&&Player->GetWeaponId()==PairLoadout.PrimaryWeaponId;
    if(Phase==9||Phase==13||Phase==16||Phase==17)
    {
        const FString Transition=Phase==9?TEXT("Change_AToB"):Phase==13?TEXT("C_Change_BToC"):Phase==16?TEXT("C_Change_CToB"):TEXT("Change_BToA");
        if(HasZXKThirdPersonPose(Transition))bModeClockSeen|=HasZXKThirdPersonClock();
        if(HasZXKThirdPersonPose(Transition)&&!bModePoseSeen){bModePoseSeen=true;Capture(Phase==9?TEXT("mode_A_to_B"):Phase==13?TEXT("mode_B_to_C"):Phase==16?TEXT("mode_C_to_B"):TEXT("mode_B_to_A"));return;}
        const int32 Expected=Phase==13?2:Phase==17?0:1;
        const bool Ready=W->GetForm()==Expected&&!W->IsChangingForm()&&HasZXKThirdPersonPose(Expected==2?TEXT("C_Idle"):Expected==1?TEXT("B_Idle"):TEXT("A_ZXK_TP_Idle"));
        if(!Ready&&Age<8.f)return;
        Check(TEXT("zxk_mode_")+Transition+TEXT("_preserves_character_and_primary"),Ready&&Preserved);
        Check(TEXT("zxk_tp_")+Transition,bModePoseSeen);
        Check(TEXT("zxk_tp_")+Transition+TEXT("_source_clock"),bModeClockSeen);
        if(Phase==13)
        {
            Check(TEXT("zxk_tp_C_idle_dart"),Ready&&P->GetWeaponMesh()&&!P->GetWeaponMesh()->IsVisible()&&P->GetAlternateWeaponMesh()&&P->GetAlternateWeaponMesh()->IsVisible());
            Check(TEXT("zxk_tp_C_sheath_hidden"),Ready&&HasSheathBone&&Body->IsBoneHiddenByName(TEXT("b_LeftWeapon")));
            Check(TEXT("zxk_tp_C_idle_contact"),HasZXKThirdPersonDartContact());Capture(TEXT("mode_C_idle"));DeferAfterCapture([this]{StartZXKModePhase(19);});
        }
        else if(Phase==9)
        {
            Check(TEXT("zxk_tp_B_idle_blade"),Ready&&P->GetWeaponMesh()&&P->GetWeaponMesh()->IsVisible()&&(!P->GetAlternateWeaponMesh()||!P->GetAlternateWeaponMesh()->IsVisible()));
            Check(TEXT("zxk_tp_B_sheath_hidden"),Ready&&HasSheathBone&&Body->IsBoneHiddenByName(TEXT("b_LeftWeapon")));
            Capture(TEXT("mode_B_idle"));DeferAfterCapture([this]{StartZXKModePhase(10);});
        }
        else if(Phase==16){if(!bModeTransformFailureRecorded)Check(TEXT("zxk_C_to_B_transforms_valid"),true);StartZXKModePhase(18);}
        else
        {
            Check(TEXT("zxk_tp_return_A_idle_blade"),Ready&&P->GetWeaponMesh()&&P->GetWeaponMesh()->IsVisible()&&(!P->GetAlternateWeaponMesh()||!P->GetAlternateWeaponMesh()->IsVisible()));
            Check(TEXT("zxk_tp_return_A_sheath_visible"),Ready&&HasSheathBone&&!Body->IsBoneHiddenByName(TEXT("b_LeftWeapon")));
            Capture(TEXT("mode_A_restored"));DeferAfterCapture([this]{FinishZXKModeTest();});
        }
    }
    else if(Phase==10||Phase==14)
    {
        const bool Ranged=Phase==14;
        const bool AttackPose=HasZXKThirdPersonPose(Ranged?TEXT("C_Fire"):TEXT("B_Fire"));
        if(AttackPose){bModeClockSeen|=HasZXKThirdPersonClock();bModeContactSeen|=HasZXKThirdPersonDartContact();}
        if(AttackPose&&!bModePoseSeen){bModePoseSeen=true;Capture(Ranged?TEXT("mode_C_attack"):TEXT("mode_B_attack"));return;}
        if(Age<3.f)return;
        Check(Ranged?TEXT("zxk_C_primary_causes_damage"):TEXT("zxk_B_primary_causes_damage"),Preserved&&PlayerDamage>ModeDamageStart);
        Check(Ranged?TEXT("zxk_tp_C_attack_pose"):TEXT("zxk_tp_B_attack_pose"),bModePoseSeen);
        Check(Ranged?TEXT("zxk_tp_C_attack_source_clock"):TEXT("zxk_tp_B_attack_source_clock"),bModeClockSeen);
        if(Ranged)
        {
            Check(TEXT("zxk_C_primary_consumes_darts"),W->GetDartAmmo()<ModeDartAmmo);
            Check(TEXT("zxk_tp_C_attack_contact"),bModeContactSeen);StartZXKModePhase(15);
        }
        else StartZXKModePhase(11);
    }
    else if(Phase==11)
    {
        if(!bModeIssued&&GetWorld()->GetTimeSeconds()>=W->GetSkillCooldownEnd())
        {Player->StartSpecialAttack();bModeIssued=W->GetCurrentSourceAction()==TEXT("B_Skill01");}
        if(HasZXKThirdPersonPose(TEXT("B_Skill01")))bModeClockSeen|=HasZXKThirdPersonClock();
        if(HasZXKThirdPersonPose(TEXT("B_Skill01"))&&!bModePoseSeen){bModePoseSeen=true;Capture(TEXT("mode_B_skill"));return;}
        if(!(bModeIssued&&W->GetCurrentSourceAction().IsNone()&&Age>=.1f)&&Age<25.f)return;
        Check(TEXT("zxk_B_skill_causes_damage"),bModeIssued&&Preserved&&PlayerDamage>ModeDamageStart);
        Check(TEXT("zxk_tp_B_skill_pose"),bModePoseSeen);Check(TEXT("zxk_tp_B_skill_source_clock"),bModeClockSeen);StartZXKModePhase(12);
    }
    else if(Phase==12)
    {
        if(HasZXKThirdPersonPose(TEXT("B_Skill02")))bModeClockSeen|=HasZXKThirdPersonClock();
        auto* FP=Player->GetCharacterPresentation()->GetWeaponMesh();
        auto* SourceMaterial=W->GetFirstPersonMesh()->GetMaterial(0);
        bModeContactSeen|=FP&&SourceMaterial&&SourceMaterial->GetName().EndsWith(TEXT("_WaveLate"))&&FP->GetMaterial(0)==SourceMaterial;
        if(HasZXKThirdPersonPose(TEXT("B_Skill02"))&&!bModePoseSeen){bModePoseSeen=true;Capture(TEXT("mode_B_wave"));return;}
        if(Age<1.f)return;
        Check(TEXT("zxk_B_wave_causes_damage"),Preserved&&PlayerDamage>ModeDamageStart);
        Check(TEXT("zxk_tp_B_wave_pose"),bModePoseSeen);Check(TEXT("zxk_tp_B_wave_source_clock"),bModeClockSeen);
        Check(TEXT("zxk_fp_blade_follows_wave_material"),bModeContactSeen);StartZXKModePhase(13);
    }
    else if(Phase==15)
    {
        if(HasZXKThirdPersonPose(TEXT("C_ChangeClip"))){bModeClockSeen|=HasZXKThirdPersonClock();bModeContactSeen|=HasZXKThirdPersonDartContact();}
        if(HasZXKThirdPersonPose(TEXT("C_ChangeClip"))&&!bModePoseSeen)
        {
            bModePoseSeen=true;const auto Before=Player->GetLoadout();const int32 Darts=W->GetDartAmmo();
            Check(TEXT("zxk_C_reload_reports_active"),Player->IsReloading());
            Player->ToggleCharacterForm();
            Check(TEXT("zxk_C_reload_rejects_character_H"),Player->IsReloading()&&Player->GetLoadout()==Before&&Player->GetEquippedWeapon()==W&&W->GetDartAmmo()==Darts);
            Player->StartCharacterInteraction();
            Check(TEXT("zxk_C_reload_rejects_character_J"),Player->IsReloading()&&Player->GetLoadout()==Before&&Player->GetEquippedWeapon()==W&&W->GetDartAmmo()==Darts&&Player->GetCharacterPresentation()&&!Player->GetCharacterPresentation()->IsCharacterGesturePlaying());
            Capture(TEXT("mode_C_reload"));return;
        }
        if(!(W->GetDartAmmo()==20&&W->GetCurrentSourceAction().IsNone()&&Age>=.1f)&&Age<8.f)return;
        Check(TEXT("zxk_C_reload_restores_darts"),Preserved&&ModeDartAmmo<20&&W->GetDartAmmo()==20);
        Check(TEXT("zxk_tp_C_reload_pose"),bModePoseSeen);Check(TEXT("zxk_tp_C_reload_source_clock"),bModeClockSeen);
        Check(TEXT("zxk_tp_C_reload_contact"),bModeContactSeen);StartZXKModePhase(16);
    }
    else if(Phase==18||Phase==19)
    {
        const bool Ranged=Phase==19;
        const bool MovePose=HasZXKThirdPersonPose(Ranged?TEXT("C_Move"):TEXT("B_Move"));
        bModePoseSeen|=MovePose;
        if(MovePose){bModeClockSeen|=HasZXKThirdPersonClock();bModeContactSeen|=HasZXKThirdPersonDartContact();}
        if(Age<1.f)
        {HeliosZXK::Sample(W->GetFirstPersonMesh(),HeliosZXK::Animation(true,Ranged?FName("C_Move"):FName("B_Move")),Age,true);return;}
        Check(Ranged?TEXT("zxk_tp_C_move_pose"):TEXT("zxk_tp_B_move_pose"),Preserved&&bModePoseSeen);
        Check(Ranged?TEXT("zxk_tp_C_move_source_clock"):TEXT("zxk_tp_B_move_source_clock"),bModeClockSeen);
        if(Ranged)Check(TEXT("zxk_tp_C_move_contact"),bModeContactSeen);
        Capture(Ranged?TEXT("mode_C_move"):TEXT("mode_B_move"));DeferAfterCapture([this,Ranged]{StartZXKModePhase(Ranged?14:17);});
    }
}
void AHeliosLoadoutQA::NextCombination()
{
    RestoreModeTargetVisibility();WarmedCaptureAssets.Reset();WarmedCaptureTextures.Reset();
    if(Player&&Player->GetCharacterPresentation())RemoveTickPrerequisiteComponent(Player->GetCharacterPresentation());
    if(Companion&&Companion->GetCharacterPresentation())RemoveTickPrerequisiteComponent(Companion->GetCharacterPresentation());
    if(Player)Player->CancelCombatAndControl();
    CleanupHolderResidue(Player);CleanupHolderResidue(Companion);
    if(IsValid(Target)){Target->OnTakeAnyDamage.RemoveDynamic(this,&AHeliosLoadoutQA::OnTargetDamage);Target->Destroy();}
    if(IsValid(Companion))Companion->Destroy();
    Target=nullptr;Companion=nullptr;
    if(CharacterForms.IsEmpty()||Weapons.IsEmpty()){Finish();return;}
    if(++WeaponIndex>=Weapons.Num()){WeaponIndex=0;++CharacterIndex;}
    if(CharacterIndex>=CharacterForms.Num()){Finish();return;}
    FHeliosLoadout L=CharacterForms[CharacterIndex];L.PrimaryWeaponId=Weapons[WeaponIndex]->WeaponId;
    PairLoadout=L;CompanionDamage=PlayerDamage=UnattributedDamage=0;
    ObservedCompanionAttackStarts=ObservedCompanionDamageHits=0;LastObservedCompanionAction=NAME_None;NextReadinessLogAt=0;bMainSkillIssued=false;
    CaseName=L.CharacterId.ToString()+(L.CharacterVariantId.IsNone()?FString():TEXT("_")+L.CharacterVariantId.ToString())+TEXT("__")+L.PrimaryWeaponId.ToString();
    if(Player->GetController())Player->GetController()->SetControlRotation(FRotator(0,90,0));
    if(Player->GetHealth()<=10.f) Player->ResetForTest();
    Check(TEXT("equip_pair"),Player->EquipLoadout(L));
    Check(TEXT("selection_independent"),Player->GetLoadout()==L);
    auto* Presentation=Player->GetCharacterPresentation();
    Check(TEXT("fp_generated_mesh_ready"),Presentation && Presentation->IsReady() && Presentation->GetArmsMesh() && Presentation->GetArmsMesh()->GetSkeletalMeshAsset());
    FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Target=GetWorld()->SpawnActor<AHeliosLoadoutQATarget>(Player->GetActorLocation()+FVector(0,210,0),FRotator(0,-90,0),P);
    if(Target)
    {
        Target->OnTakeAnyDamage.AddDynamic(this,&AHeliosLoadoutQA::OnTargetDamage);
        Target->CurrentHP=1000000;Target->SetActorTickEnabled(false);Target->StopShooting();
        Target->SetRuntimeIdentity(TEXT("loadout_qa_target"),TEXT("loadout_qa_marker"));
        Target->GetCharacterMovement()->DisableMovement();
        if(auto* AI=Cast<AAIController>(Target->GetController())){AI->StopMovement();if(AI->GetBrainComponent())AI->GetBrainComponent()->StopLogic(TEXT("loadout QA target"));}
    }
    Companion=GetWorld()->SpawnActor<AHeliosLoadoutQAPet>(Player->GetActorLocation()+FVector(170,180,0),FRotator(0,90,0),P);
    if(Companion)
    {
        Companion->CompanionOwner=Player;
        Companion->Target=Target;
        Check(TEXT("companion_equips_pair"),Companion->ApplyLoadout(L));
        Check(TEXT("tp_generated_mesh_ready"),Companion->GetCharacterPresentation() && Companion->GetCharacterPresentation()->IsReady());
        if(!L.CharacterVariantId.IsNone())
        {
            Check(TEXT("selected_form_player_voice"),HasSelectedFormVoice(Player,L));
            Check(TEXT("selected_form_companion_voice"),HasSelectedFormVoice(Companion,L));
        }
        CompanionInitialAmmo=Companion->GetAmmo();
    }
    Check(TEXT("damage_target_spawn"),IsValid(Target));
    Check(TEXT("companion_spawn"),IsValid(Companion));
    if(Player->GetCharacterPresentation())AddTickPrerequisiteComponent(Player->GetCharacterPresentation());
    if(Companion&&Companion->GetCharacterPresentation())AddTickPrerequisiteComponent(Companion->GetCharacterPresentation());
    HideModeTargetForPhotography();
    // Preflight renderer work with combat suspended, then allow two real HUD/
    // render frames before opening the unchanged primary-damage window.
    PauseCompanion();WarmCaptureResources();bInitialRendererWarm=false;Age=0;Phase=26;
}
void AHeliosLoadoutQA::Tick(float Dt)
{
    Super::Tick(Dt);if(bFinished)return;Age+=Dt;
    if(Phase==0)DiagnoseCompanionReadiness(false);
    if(Phase==16)DiagnoseModeTransforms();
    if(!PendingCaptureName.IsEmpty()&&GFrameCounter>=PendingCaptureFrame)
    {
        bool ResourcesReady=true;
#if WITH_EDITOR
        ResourcesReady=FAssetCompilingManager::Get().GetNumRemainingAssets()==0&&(!GShaderCompilingManager||GShaderCompilingManager->GetNumRemainingJobs()==0);
#endif
        for(const auto& Texture:WarmedCaptureTextures)ResourcesReady&=Texture&&Texture->IsFullyStreamedIn();
        // Keep native clocks running while late render permutations finish.
        // A bounded timeout is a real failed readiness check, never a gray
        // placeholder counted as a successful visual capture.
        if(!ResourcesReady&&GetWorld()->GetTimeSeconds()<PendingCaptureReadyDeadline)return;
        Check(TEXT("capture_render_resources_ready"),ResourcesReady);
        if(PendingCaptureName==TEXT("cheers_finished"))Check(TEXT("yun_cheers_natural_native_driver_hidden"),IsNativeDriverHidden());
        if(PendingCaptureName==TEXT("cheers_interrupted"))Check(TEXT("yun_cheers_interrupted_native_driver_hidden"),IsNativeDriverHidden());
        Check(TEXT("tp_hands_capture_saved"),CaptureThirdPersonHands(PendingCaptureName));
        FScreenshotRequest::RequestScreenshot(Out/(CaseName+TEXT("_")+PendingCaptureName+TEXT(".png")),true,false);
        UE_LOG(LogTemp,Display,TEXT("LOADOUT_QA_CAPTURE case=%s state=%s phase=%d age=%.6f frame=%llu resources_ready=%d warm_frame=%llu"),*CaseName,*PendingCaptureName,Phase,Age,GFrameCounter,ResourcesReady,ResourcesReadyFrame);
        PendingCaptureName.Empty();CaptureReleaseFrame=GFrameCounter+2;CaptureReleaseTime=GetWorld()->GetTimeSeconds()+.1;
        return;
    }
    if(GFrameCounter<=CaptureReleaseFrame||GetWorld()->GetTimeSeconds()<CaptureReleaseTime)return;
    if(DeferredCaptureMutation)
    {
        auto Mutation=MoveTemp(DeferredCaptureMutation);DeferredCaptureMutation=nullptr;Mutation();return;
    }
    if(Phase<0)
    {
        if(Age<3)return;
        Player=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));
        if(!Player){Check(TEXT("player_exists"),false);Finish();return;}
        OriginalLoadout=Player->GetLoadout();
        UGameplayStatics::SetGamePaused(this,false);
        if(auto* D=AHeliosAdventureDirector::Find(GetWorld()))
        {
            Check(TEXT("default_run_starts"),D->StartRunWithLoadout(FHeliosLoadout::MakeDefault()));
            D->SetActorTickEnabled(false);
        }
        // Keep this fixture bounded: only the target created for the current pair can attack or be damaged.
        for(TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It) It->Destroy();
        Player->GetController()->SetControlRotation(FRotator(0,90,0));
        NextCombination();return;
    }
    if(Player->IsDead()){Check(TEXT("player_survives_fixture"),false);Player->ResetForTest();}
    if(Phase==26)
    {
        if(GFrameCounter<=ResourcesReadyFrame+2||Age<.15f)return;
        if(!bInitialRendererWarm){WarmCaptureResources();bInitialRendererWarm=true;Age=0;return;}
        if(Companion){Companion->Target=Target;Companion->SetActorTickEnabled(true);CompanionInitialAmmo=Companion->GetAmmo();}
        Phase=0;Age=0;return;
    }
    // Keep the full 2.6-second primary window. A cold asset/pose evaluation may
    // consume that window before a real attack gets a frame; observe to the
    // bounded six-second deadline when no attributed hit has occurred.
    if(Phase==0 && Age>=2.6f && (CompanionDamage>0 || Age>=6.f))
    {
        DiagnoseCompanionReadiness(true);Capture(TEXT("idle"));
        Check(TEXT("companion_primary_causes_damage"),CompanionDamage>0);
        if(Companion&&Companion->GetEquippedWeapon()&&Companion->GetEquippedWeapon()->UsesAmmunition())
            Check(TEXT("companion_primary_consumes_ammo"),Companion->GetAmmo()<CompanionInitialAmmo||Companion->Reloads>0);
        DeferAfterCapture([this]
        {
            if(Companion&&Cast<AHeliosSunShangXiangWeapon>(Companion->GetEquippedWeapon()))BeginHunterBasicTest(false);
            else if(Companion&&Companion->GetEquippedWeapon()&&Companion->GetEquippedWeapon()->UsesAmmunition())BeginCompanionReloadTest();
            else BeginPlayerPrimary();
        });
    }
    else if((Phase>=20&&Phase<=21)||Phase==25)TickCharacterFormTest();
    else if((Phase>=22&&Phase<=24)||Phase==28)TickYunCheersTest();
    else if((Phase>=9&&Phase<=19)||Phase==27)TickZXKModeTest();
    else if(Phase==6||Phase==7)TickHunterBasicTest(Phase==7);
    else if(Phase==8)
    {
        if(Companion&&Companion->IsReloading()&&!bReloadSwapAttempted)
        {
            bReloadSwapAttempted=true;
            const auto Alternate=DifferentCharacterLoadout(Companion->GetLoadout());
            Companion->Health=FMath::Min(Companion->Health,Companion->Settings.MaxHealth*.6f);
            const float Health=Companion->Health;const int32 Ammo=Companion->GetAmmo();
            AShooterWeapon* ActualPrimary=Companion->GetEquippedWeapon();
            Check(TEXT("companion_reload_uses_empty_magazine"),Ammo==0&&ActualPrimary&&ActualPrimary->GetBulletCount()==0);
            Check(TEXT("companion_reload_character_is_different"),Alternate.CharacterId!=Companion->GetLoadout().CharacterId);
            Check(TEXT("companion_change_character_during_reload"),Companion->ApplyLoadout(Alternate));
            Check(TEXT("companion_character_preserves_active_reload"),Companion->IsReloading()&&Companion->GetEquippedWeapon()==ActualPrimary&&ActualPrimary==ReloadPrimary.Get()&&Companion->GetAmmo()==Ammo&&ActualPrimary&&ActualPrimary->GetBulletCount()==Ammo);
            Check(TEXT("companion_reload_character_does_not_heal"),Companion->Health<=Health);
            CompanionReloadCaptureAt=GetWorld()->GetTimeSeconds()+.1;return;
        }
        if(bReloadSwapAttempted&&!bCompanionReloadCaptured&&GetWorld()->GetTimeSeconds()>=CompanionReloadCaptureAt)
        {
            bCompanionReloadCaptured=true;
            Check(TEXT("companion_reload_capture_while_active"),Companion&&Companion->IsReloading());
            Capture(TEXT("companion_reload"));return;
        }
        if((bReloadSwapAttempted&&Companion&&Companion->Reloads>CompanionReloadsStart&&!Companion->IsReloading())||Age>=ReloadTimeout)FinishCompanionReloadTest();
    }
    else if(Phase==1&&Age>=3.f)
    {
        Capture(TEXT("attack"));Check(TEXT("weapon_preserved_while_attacking"),Player->GetWeaponId()==Weapons[WeaponIndex]->WeaponId);
        Check(TEXT("player_primary_causes_damage"),PlayerDamage>PlayerPrimaryDamageStart);
        if(Player->GetEquippedWeapon()&&Player->GetEquippedWeapon()->UsesAmmunition())Check(TEXT("player_primary_consumes_ammo"),Player->GetAmmo()<PlayerPrimaryInitialAmmo);
        if(Player->GetLoadout().CharacterId==TEXT("cfm_chan")&&Player->GetWeaponId()==TEXT("cfm_chan_zeus"))
        {
            Check(TEXT("default_pair_autonomy_causes_damage"),PlayerDamage>PlayerPrimaryDamageStart);
        }
        DeferAfterCapture([this]
        {
            if(auto* Auto=Player->FindComponentByClass<UHeliosAutonomyComponent>())Auto->Cancel(TEXT("loadout QA complete"));
            Player->StopFireFromAutomation();Player->StartReloadFromAutomation();Phase=2;Age=0;
        });
    }
    else if(Phase==2 && Age>=.6f){Capture(TEXT("reload"));Phase=3;Age=0;}
    else if(Phase==3)
    {
        if(!bMainSkillIssued&&Age>=3.f)
        {Player->CancelCombatAndControl();Player->StartSpecialAttack();bMainSkillIssued=true;Age=0;return;}
        if(bMainSkillIssued&&Age>=.2f)
        {Capture(TEXT("skill"));Phase=4;Age=0;bMainSkillIssued=false;}
    }
    else if(Phase==4 && Age>=2.4f)
    {
        Player->CancelCombatAndControl();
        Player->TakeDamage(5.f,FDamageEvent(),nullptr,this);
        const float PlayerHealth=Player->GetHealth();
        const int32 Ammo=Player->GetAmmo();const FName WeaponId=Player->GetWeaponId();
        AShooterWeapon* ActualPlayerPrimary=Player->GetEquippedWeapon();
        FHeliosLoadout Alternate=DifferentCharacterLoadout(Player->GetLoadout());
        Check(TEXT("character_swap_is_different"),Alternate.CharacterId!=Player->GetLoadout().CharacterId);
        Check(TEXT("change_character_only"),Player->EquipLoadout(Alternate));
        Check(TEXT("character_change_preserves_ammo"),Player->GetAmmo()==Ammo && Player->GetWeaponId()==WeaponId && Player->GetEquippedWeapon()==ActualPlayerPrimary);
        Check(TEXT("character_change_does_not_heal"),Player->GetHealth()<=PlayerHealth);
        if(Companion)
        {
            Companion->Health=FMath::Min(Companion->Health,Companion->Settings.MaxHealth*.6f);
            const float Health=Companion->Health;
            const int32 PetAmmo=Companion->GetAmmo();
            AShooterWeapon* ActualPrimary=Companion->GetEquippedWeapon();
            const FHeliosLoadout PetAlternate=DifferentCharacterLoadout(Companion->GetLoadout());
            Check(TEXT("companion_character_swap_is_different"),PetAlternate.CharacterId!=Companion->GetLoadout().CharacterId);
            Check(TEXT("companion_change_character_only"),Companion->ApplyLoadout(PetAlternate));
            Check(TEXT("companion_character_change_updates_capsule"),FMath::IsNearlyEqual(Companion->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),PetAlternate.CharacterId==TEXT("cfm_yun_summer")?96.f:102.f)&&FMath::IsNearlyEqual(Companion->GetCapsuleComponent()->GetUnscaledCapsuleRadius(),34.f));
            Check(TEXT("companion_character_preserves_primary"),Companion->GetEquippedWeapon()==ActualPrimary && Companion->GetAmmo()==PetAmmo);
            Check(TEXT("companion_character_does_not_heal"),Companion->Health<=Health);
        }
        if(!PairLoadout.CharacterVariantId.IsNone())BeginCharacterFormTest();
        else FinishPairTests();
    }
}
void AHeliosLoadoutQA::Finish()
{
    if(bFinished)return;bFinished=true;
    if(Player)Player->CancelCombatAndControl();
    auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("failures"),Failures);J->SetNumberField(TEXT("combinations"),Completed);
    J->SetBoolField(TEXT("yun_deferred"),false);J->SetArrayField(TEXT("checks"),Checks);J->SetArrayField(TEXT("diagnostics"),Diagnostics);
    J->SetNumberField(TEXT("characters"),Characters.Num());J->SetNumberField(TEXT("forms"),CharacterForms.Num());
    J->SetNumberField(TEXT("form_state_combinations"),FormCompleted);J->SetNumberField(TEXT("cheers_combinations"),CheersCompleted);
    J->SetBoolField(TEXT("audio_device_enabled"),!FParse::Param(FCommandLine::Get(),TEXT("nosound"))&&GetWorld()->GetAudioDevice().IsValid());
    TArray<TSharedPtr<FJsonValue>> FormRows;for(const auto& L:CharacterForms){auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("character_id"),L.CharacterId.ToString());Row->SetStringField(TEXT("variant_id"),L.CharacterVariantId.ToString());FormRows.Add(MakeShared<FJsonValueObject>(Row));}J->SetArrayField(TEXT("character_forms"),FormRows);
    const bool RequestedModes=FParse::Param(FCommandLine::Get(),TEXT("HeliosLoadoutModeQA"));
    J->SetBoolField(TEXT("weapon_modes_requested"),RequestedModes);J->SetNumberField(TEXT("weapon_modes_completed"),ModeCompleted);
    FString Text;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Out/TEXT("matrix.json")));
    UE_LOG(LogTemp,Display,TEXT("LOADOUT_QA_COMPLETE combinations=%d failures=%d"),Completed,Failures);
    if(RequestedModes)UE_LOG(LogTemp,Display,TEXT("LOADOUT_MODE_QA_COMPLETE combinations=%d failures=%d"),ModeCompleted,Failures);
    FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}
