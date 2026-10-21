#include "QA/HeliosBioHumanActionsQA.h"
#include "Bio/HeliosBioHotelGameMode.h"
#include "Bio/HeliosBioNPC.h"
#include "Bio/HeliosBioPlayer.h"
#include "Bio/HeliosBioSupply.h"
#include "Bio/HeliosBioStaticJumpLink.h"
#include "Player/HeliosCharacterDefinition.h"
#include "Player/HeliosCharacterActionProfile.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Variant_Shooter/Weapons/ShooterProjectile.h"
#include "Weapons/HeliosChanZeus.h"
#include "Weapons/HeliosWangZheKuiThunder.h"
#include "Weapons/HeliosBuLianShiP90.h"
#include "Weapons/HeliosYunSummerMegatron.h"
#include "Weapons/HeliosHandAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "SkeletalRenderPublic.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
bool IsFoot(const FString& Name)
{return Name.Contains(TEXT("foot"),ESearchCase::IgnoreCase)||Name.Contains(TEXT("toe"),ESearchCase::IgnoreCase)||Name.Contains(TEXT("ankle"),ESearchCase::IgnoreCase);}
FString FormKey(const FHeliosLoadout& Loadout)
{return Loadout.CharacterId.ToString()+TEXT("/")+Loadout.CharacterVariantId.ToString();}
struct FNativeHumanWeaponEvidence
{FString Mechanism;int32 Shots=-1,DamageHits=-1;};
FNativeHumanWeaponEvidence ReadNativeWeapon(AShooterWeapon* Weapon)
{
    if(auto* Native=Cast<AHeliosChanZeusWeapon>(Weapon))return {TEXT("original_Zeus_hitscan_pellets_ApplyPointDamage"),Native->ShotsFired,Native->HitPellets};
    if(auto* Native=Cast<AHeliosWangZheKuiThunderWeapon>(Weapon))return {TEXT("original_Thunder_hitscan_pellets_ApplyPointDamage"),Native->ShotsFired,Native->HitPellets};
    if(auto* Native=Cast<AHeliosBuLianShiP90Weapon>(Weapon))return {TEXT("original_P90_hitscan_pellets_ApplyPointDamage"),Native->ShotsFired,Native->HitPellets};
    if(auto* Native=Cast<AHeliosYunSummerMegatronWeapon>(Weapon))return {TEXT("original_MegatronGrenade_sweep_explosion_ApplyDamage"),Native->ShotsFired,Native->HitsDelivered};
    return {TEXT("unsupported_native_weapon_provenance"),-1,-1};
}
}
AHeliosBioHumanActionsQA::AHeliosBioHumanActionsQA()
{
    PrimaryActorTick.bCanEverTick=true;
    // Human source sampling commits in this group; explicit prerequisites place QA after it.
    PrimaryActorTick.TickGroup=TG_PostUpdateWork;
}
void AHeliosBioHumanActionsQA::Check(const FString& Name,bool Passed)
{
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("name"),Name);Row->SetBoolField(TEXT("passed"),Passed);Checks.Add(MakeShared<FJsonValueObject>(Row));
    if(!Passed)++Failures;UE_LOG(LogTemp,Display,TEXT("BIO_HUMAN_QA %s %s"),*Name,Passed?TEXT("PASS"):TEXT("FAIL"));
}
void AHeliosBioHumanActionsQA::AddBlocker(FHeliosHumanActionCase& Case,const FString& Reason)
{
    if(!Case.Blocker.Contains(Reason)){if(!Case.Blocker.IsEmpty())Case.Blocker+=TEXT("; ");Case.Blocker+=Reason;}
}
void AHeliosBioHumanActionsQA::FreezeAll(bool HideOthers)
{
    for(auto Bot:Mode->Bots)if(Bot)
    {
        Bot->bQAFreeze=true;if(auto* Weapon=Bot->GetEquippedWeapon())Weapon->StopFiring();
        if(auto* AI=Cast<AAIController>(Bot->GetController()))AI->StopMovement();
        if(Bot->GetCharacterMovement()->IsMovingOnGround())Bot->GetCharacterMovement()->StopMovementImmediately();
        if(HideOthers){Bot->SetActorHiddenInGame(true);Bot->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);Bot->GetCharacterMovement()->DisableMovement();Bot->SetActorLocation(FVector(1000000+Bot->ParticipantId*500,1000000,1000000),false);}
    }
    if(Mode->Player)Mode->Player->StopBioFire();
}
bool AHeliosBioHumanActionsQA::ResolveBindings(FHeliosHumanActionCase& Case)
{
    auto* Definition=FHeliosCharacterRegistry::FindCharacter(Case.Loadout.CharacterId);
    const auto* Form=Definition?Definition->FindForm(Case.Loadout.CharacterVariantId):nullptr;
    auto* Profile=Form?Form->ActionProfile.LoadSynchronous():nullptr;
    const auto* Pair=Profile?Profile->FindWeapon(Case.Loadout.PrimaryWeaponId):nullptr;
    if(!Definition||!Form||!Profile||Profile->CharacterId!=Case.Loadout.CharacterId||Profile->CharacterVariantId!=Case.Loadout.CharacterVariantId||!Pair||!Pair->bGeneratedAndValidated)
    {AddBlocker(Case,TEXT("Independent saved form/weapon action profile missing or unvalidated"));return false;}
    SourceObjects.AddUnique(Profile);auto* Body=Pair->ThirdPersonMesh.LoadSynchronous();Case.Body=Body;if(Body)SourceObjects.AddUnique(Body);
    bool Complete=Body&&Body->GetSkeleton();
    for(FName Action:{FName(TEXT("Idle")),FName(TEXT("Move")),FName(TEXT("Fire")),FName(TEXT("ChangeClip")),FName(TEXT("Down"))})
    {
        bool Found=false;
        for(const auto& Binding:Pair->ThirdPersonClips)
        {
            // Saved TP rebinding profiles also declare original source-name aliases such as
            // A_DIAOCHAN_FP_FireSingle and A_KUI_FP_FireSingle. The actual weapon driver uses
            // those aliases; their exact declared asset and matching body skeleton remain required.
            const FString KeyName=Binding.Key.ToString();
            const bool Match=Binding.Key==Action||(Action==TEXT("Fire")&&(KeyName.StartsWith(TEXT("Fire"))||KeyName.Contains(TEXT("_Fire"))));
            if(!Match)continue;auto* Clip=Binding.Value.LoadSynchronous();
            const bool Valid=Clip&&Body&&Clip->GetSkeleton()==Body->GetSkeleton()&&FMath::IsFinite(Clip->GetPlayLength())&&Clip->GetPlayLength()>0;
            auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("source_binding"));Row->SetStringField(TEXT("form"),Case.Form);Row->SetNumberField(TEXT("participant"),Case.Bot->ParticipantId);Row->SetStringField(TEXT("weapon"),Case.Loadout.PrimaryWeaponId.ToString());Row->SetStringField(TEXT("semantic"),Action.ToString());Row->SetStringField(TEXT("declared_action"),Binding.Key.ToString());Row->SetStringField(TEXT("clip"),GetPathNameSafe(Clip));Row->SetStringField(TEXT("body"),GetPathNameSafe(Body));Row->SetBoolField(TEXT("matching_source_skeleton_and_duration"),Valid);Observations.Add(MakeShared<FJsonValueObject>(Row));
            if(Valid){SourceObjects.AddUnique(Clip);Case.Clips.FindOrAdd(Action).AddUnique(Clip);Found=true;}
        }
        if(!Found){Complete=false;AddBlocker(Case,Action.ToString()+TEXT(" native compatible TP clip unavailable"));}
    }
    return Complete;
}
bool AHeliosBioHumanActionsQA::BuildRoster()
{
    Cases.Reset();TSet<FString> Forms;
    for(auto Bot:Mode->Bots)if(Bot)
    {
        auto& Case=Cases.AddDefaulted_GetRef();Case.Bot=Bot;Case.Loadout=Bot->GetHumanLoadout();Case.Form=FormKey(Case.Loadout);Case.PreviousPosition=Bot->GetActorLocation();Forms.Add(Case.Form);
        ResolveBindings(Case);
        if(auto* Presentation=Bot->FindComponentByClass<UHeliosCharacterPresentationComponent>())AddTickPrerequisiteComponent(Presentation);
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("roster"));Row->SetNumberField(TEXT("participant"),Bot->ParticipantId);Row->SetStringField(TEXT("form"),Case.Form);Row->SetStringField(TEXT("weapon"),Case.Loadout.PrimaryWeaponId.ToString());Row->SetStringField(TEXT("profile_blocker"),Case.Blocker);Observations.Add(MakeShared<FJsonValueObject>(Row));
    }
    Check(TEXT("actual_29_NPC_roster"),Cases.Num()==29);Check(TEXT("actual_at_least_17_distinct_forms"),Forms.Num()>=17);
    bInitialRosterReady=Cases.Num()==29&&Forms.Num()>=17;return bInitialRosterReady;
}
void AHeliosBioHumanActionsQA::SamplePose(FHeliosHumanActionCase& Case,FName Action)
{
    auto* Bot=Case.Bot.Get();auto* Presentation=Bot?Bot->FindComponentByClass<UHeliosCharacterPresentationComponent>():nullptr;
    auto* Mesh=Presentation?Presentation->GetArmsMesh():nullptr;auto* Node=Mesh?Mesh->GetSingleNodeInstance():nullptr;
    const auto* Clips=Case.Clips.Find(Action);bool Native=false;
    if(Node&&Clips)for(const auto& Clip:*Clips)Native|=Clip.IsValid()&&Node->GetCurrentAsset()==Clip.Get();
    if(!Native||!Mesh||Mesh->GetSkeletalMeshAsset()!=Case.Body.Get()||!Mesh->IsVisible()||Mesh->bHiddenInGame||Bot->IsHidden()||Presentation->GetPoseCommitFrame()!=GFrameCounter)
    {
        if(Bot&&Action==TEXT("Fire")){auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("fire_pose_excluded"));Row->SetStringField(TEXT("form"),Case.Form);Row->SetStringField(TEXT("actual_clip"),Node?GetPathNameSafe(Node->GetCurrentAsset()):TEXT(""));Row->SetStringField(TEXT("actual_body"),Mesh?GetPathNameSafe(Mesh->GetSkeletalMeshAsset()):TEXT(""));Row->SetBoolField(TEXT("exact_declared_native_action_clip"),Native);Row->SetBoolField(TEXT("current_frame_committed"),Presentation&&Presentation->GetPoseCommitFrame()==GFrameCounter);Observations.Add(MakeShared<FJsonValueObject>(Row));}return;
    }
    auto& Probe=Case.Poses.FindOrAdd(Action);const FString Clip=Node->GetCurrentAsset()->GetPathName();const float Seconds=Node->GetCurrentTime();const auto& Pose=Mesh->GetBoneSpaceTransforms();
    bool PoseChanged=false;if(Probe.Clip==Clip&&Probe.Pose.Num()==Pose.Num())for(int32 I=0;I<Pose.Num();++I)
        PoseChanged|=FVector::DistSquared(Probe.Pose[I].GetLocation(),Pose[I].GetLocation())>.0001f||Probe.Pose[I].GetRotation().AngularDistance(Pose[I].GetRotation())>.0001f;
    const bool ClockAdvanced=Probe.Clip==Clip&&FMath::Abs(Seconds-Probe.Seconds)>.0001f;
    ++Probe.Samples;if(ClockAdvanced)++Probe.ClockAdvances;if(PoseChanged)++Probe.PoseAdvances;
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("actual_native_pose"));Row->SetStringField(TEXT("form"),Case.Form);Row->SetNumberField(TEXT("participant"),Bot->ParticipantId);Row->SetStringField(TEXT("action"),Action.ToString());Row->SetStringField(TEXT("body"),Mesh->GetSkeletalMeshAsset()->GetPathName());Row->SetStringField(TEXT("clip"),Clip);Row->SetNumberField(TEXT("clip_seconds"),Seconds);Row->SetNumberField(TEXT("world_seconds"),GetWorld()->GetTimeSeconds());Row->SetNumberField(TEXT("pose_commit_count"),Presentation->GetPoseCommitCount());Row->SetBoolField(TEXT("current_frame_committed"),true);Row->SetBoolField(TEXT("clip_clock_advanced"),ClockAdvanced);Row->SetBoolField(TEXT("local_bone_pose_advanced"),PoseChanged);Observations.Add(MakeShared<FJsonValueObject>(Row));
    Probe.Clip=Clip;Probe.Seconds=Seconds;Probe.Pose=Pose;
}
bool AHeliosBioHumanActionsQA::PosePassed(const FHeliosHumanActionCase& Case,FName Action) const
{
    const auto* Probe=Case.Poses.Find(Action);return Probe&&Probe->Samples>=2&&Probe->ClockAdvances>0&&Probe->PoseAdvances>0;
}
bool AHeliosBioHumanActionsQA::MeasureStanding(FHeliosHumanActionCase& Case,const FString& Phase)
{
    auto* Bot=Case.Bot.Get();auto* Presentation=Bot?Bot->FindComponentByClass<UHeliosCharacterPresentationComponent>():nullptr;auto* Mesh=Presentation?Presentation->GetArmsMesh():nullptr;
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("rendered_standing"));Row->SetStringField(TEXT("phase"),Phase);Row->SetStringField(TEXT("form"),Case.Form);Row->SetNumberField(TEXT("participant"),Bot?Bot->ParticipantId:-1);
    if(!Bot||!Mesh||!Mesh->GetSkeletalMeshAsset()||Mesh->GetSkeletalMeshAsset()!=Case.Body.Get())
    {Row->SetStringField(TEXT("blocker"),TEXT("Expected actual human body missing"));Observations.Add(MakeShared<FJsonValueObject>(Row));return false;}
    const auto* Render=Mesh->GetSkeletalMeshRenderData();auto* Weights=Mesh->GetSkinWeightBuffer(0);
    if(!Render||!Render->LODRenderData.IsValidIndex(0)||!Weights||!Weights->GetDataVertexBuffer()->GetWeightData())
    {Row->SetStringField(TEXT("blocker"),TEXT("CPU skin weights unavailable; bone centers cannot substitute for shoe sole"));Observations.Add(MakeShared<FJsonValueObject>(Row));return false;}
    TArray<FFinalSkinVertex> Vertices;Mesh->GetCPUSkinnedVertices(Vertices,0);const auto& LOD=Render->LODRenderData[0];FVector Sole=FVector::ZeroVector;float Lowest=MAX_flt;int32 FootVertices=0;
    for(const auto& Section:LOD.RenderSections)for(uint32 I=Section.BaseVertexIndex;I<Section.BaseVertexIndex+Section.NumVertices&&I<uint32(Vertices.Num());++I)
    {
        bool Foot=false;for(uint32 W=0;W<Weights->GetMaxBoneInfluences();++W){const uint32 Local=Weights->GetBoneIndex(I,W);if(Weights->GetBoneWeight(I,W)>0&&Section.BoneMap.IsValidIndex(Local))Foot|=IsFoot(Mesh->GetBoneName(Section.BoneMap[Local]).ToString());}
        if(!Foot)continue;++FootVertices;const FVector World=Mesh->GetComponentTransform().TransformPosition(FVector(Vertices[I].Position));if(World.Z<Lowest){Lowest=World.Z;Sole=World;}
    }
    const float CapsuleFeet=Bot->GetActorLocation().Z-Bot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();FHitResult Floor;FCollisionQueryParams Query(SCENE_QUERY_STAT(BioHumanRenderedSole),false,Bot);for(auto Other:Mode->Bots)Query.AddIgnoredActor(Other);Query.AddIgnoredActor(Mode->Player);TArray<AActor*> Attached;Bot->GetAttachedActors(Attached,true,true);Query.AddIgnoredActors(Attached);
    const bool Hit=FootVertices>0&&GetWorld()->LineTraceSingleByChannel(Floor,FVector(Sole.X,Sole.Y,CapsuleFeet+30),FVector(Sole.X,Sole.Y,CapsuleFeet-150),ECC_Visibility,Query);
    const float Gap=Hit?Lowest-Floor.ImpactPoint.Z:MAX_flt;const bool Passed=Hit&&Bot->GetCharacterMovement()->IsMovingOnGround()&&FMath::Abs(Gap)<=3.f;
    Row->SetNumberField(TEXT("foot_weighted_vertices"),FootVertices);Row->SetBoolField(TEXT("floor_hit"),Hit);Row->SetBoolField(TEXT("grounded"),Bot->GetCharacterMovement()->IsMovingOnGround());Row->SetStringField(TEXT("actor_location"),Bot->GetActorLocation().ToString());Row->SetNumberField(TEXT("actual_capsule_half_height_cm"),Bot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());Row->SetNumberField(TEXT("actual_capsule_radius_cm"),Bot->GetCapsuleComponent()->GetScaledCapsuleRadius());
    if(Hit){Row->SetNumberField(TEXT("rendered_sole_ground_gap_cm"),Gap);Row->SetNumberField(TEXT("capsule_bottom_ground_gap_cm"),CapsuleFeet-Floor.ImpactPoint.Z);Row->SetStringField(TEXT("sole_world"),Sole.ToString());Row->SetStringField(TEXT("floor_component"),GetPathNameSafe(Floor.GetComponent()));Row->SetStringField(TEXT("floor_normal"),Floor.ImpactNormal.ToString());FHitResult Center;const FVector Location=Bot->GetActorLocation();if(GetWorld()->LineTraceSingleByChannel(Center,FVector(Location.X,Location.Y,CapsuleFeet+30),FVector(Location.X,Location.Y,CapsuleFeet-150),ECC_Visibility,Query)){Row->SetNumberField(TEXT("capsule_center_ground_gap_cm"),CapsuleFeet-Center.ImpactPoint.Z);Row->SetNumberField(TEXT("sole_support_vs_capsule_support_cm"),Floor.ImpactPoint.Z-Center.ImpactPoint.Z);}}
    Observations.Add(MakeShared<FJsonValueObject>(Row));Check(Phase+TEXT("_human")+FString::FromInt(Bot->ParticipantId)+TEXT("_actual_sole_within_3cm"),Passed);return Passed;
}
void AHeliosBioHumanActionsQA::SamplePreparation()
{
    for(auto& Case:Cases)if(auto* Bot=Case.Bot.Get())
    {
        const auto* P=Mode->Model.FindParticipant(Bot->ParticipantId);if(!P||P->Team!=EHeliosBioTeam::Human)continue;
        const float Distance=FVector::Dist2D(Case.PreviousPosition,Bot->GetActorLocation());Case.PreviousPosition=Bot->GetActorLocation();
        if(Bot->GetCharacterMovement()->IsMovingOnGround()&&Bot->GetVelocity().Size2D()>10.f){Case.PreparationTravel+=Distance;SamplePose(Case,TEXT("Move"));}
        Case.bMove=Case.PreparationTravel>40.f&&PosePassed(Case,TEXT("Move"));
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("preparation_movement"));Row->SetStringField(TEXT("form"),Case.Form);Row->SetNumberField(TEXT("participant"),Bot->ParticipantId);Row->SetNumberField(TEXT("actual_ground_travel_cm"),Case.PreparationTravel);Row->SetStringField(TEXT("position"),Bot->GetActorLocation().ToString());Row->SetStringField(TEXT("velocity"),Bot->GetVelocity().ToString());Row->SetStringField(TEXT("intent"),Bot->GetMovementIntent().ToString());Row->SetNumberField(TEXT("nav_result"),Bot->GetLastMoveResult());Row->SetStringField(TEXT("production_movement_diagnostics"),Bot->GetMovementDiagnostic());Observations.Add(MakeShared<FJsonValueObject>(Row));
    }
}
bool AHeliosBioHumanActionsQA::SelectRepresentatives()
{
    SelectedCases.Reset();TMap<FString,int32> ByForm;
    for(int32 I=0;I<Cases.Num();++I)
    {
        auto* Bot=Cases[I].Bot.Get();const auto* P=Bot?Mode->Model.FindParticipant(Bot->ParticipantId):nullptr;if(!P||!P->bAlive||P->Team!=EHeliosBioTeam::Human)continue;
        auto* Previous=ByForm.Find(Cases[I].Form);if(!Previous||(!Cases[*Previous].bMove&&Cases[I].bMove))ByForm.Add(Cases[I].Form,I);
    }
    TArray<FString> Names;ByForm.GetKeys(Names);Names.Sort();for(const FString& Name:Names)SelectedCases.Add(ByForm.FindChecked(Name));
    Check(TEXT("active_human_roster_covers_all_17_forms"),SelectedCases.Num()>=17);return SelectedCases.Num()>=17;
}
bool AHeliosBioHumanActionsQA::PlaceActor(AHeliosBioNPC* Bot,const FVector& Wanted)
{
    if(!Bot)return false;FHitResult Floor;FCollisionObjectQueryParams Static(ECC_WorldStatic);FCollisionQueryParams Query(SCENE_QUERY_STAT(BioHumanActionFloor),false,Bot);
    const float ExpectedFeet=Wanted.Z-Bot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    // The caller has selected actual support at this height. A tall trace from above
    // the actor could instead select a different box/roof and invalidate the fixture.
    if(!GetWorld()->LineTraceSingleByObjectType(Floor,FVector(Wanted.X,Wanted.Y,ExpectedFeet+30),FVector(Wanted.X,Wanted.Y,ExpectedFeet-30),Static,Query)||Floor.ImpactNormal.Z<.95f||FMath::Abs(Floor.ImpactPoint.Z-ExpectedFeet)>3.f)return false;
    Bot->SetActorTickEnabled(true);Bot->SetActorHiddenInGame(false);Bot->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Bot->GetCharacterMovement()->SetMovementMode(MOVE_Walking);Bot->GetCharacterMovement()->StopMovementImmediately();Bot->SetActorLocation(FVector(Wanted.X,Wanted.Y,Floor.ImpactPoint.Z+Bot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2),false,nullptr,ETeleportType::TeleportPhysics);Bot->SetActorRotation(LaneForward.Rotation());return true;
}
bool AHeliosBioHumanActionsQA::FindCombatLane(AHeliosBioNPC* Human)
{
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());if(!Nav)return false;
    for(int32 Id=0;Id<Mode->Model.GetParticipants().Num();++Id)for(const FVector Direction:{FVector::ForwardVector,-FVector::ForwardVector,FVector::RightVector,-FVector::RightVector})
    {
        const FVector Start=Mode->SpawnLocation(Id);const float Half=Human->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();const float FloorZ=Start.Z-(Mode->Model.Config.MovementCapsuleHalfHeight+2);
        FCollisionObjectQueryParams Static(ECC_WorldStatic);FCollisionQueryParams Query(SCENE_QUERY_STAT(BioHumanActionLane),false);for(auto Bot:Mode->Bots)Query.AddIgnoredActor(Bot);Query.AddIgnoredActor(Mode->Player);bool Clear=true;
        for(int32 Step=0;Step<=12&&Clear;++Step){const FVector At=Start+Direction*(Step*100.f);FHitResult Floor;Clear=GetWorld()->LineTraceSingleByObjectType(Floor,At+FVector(0,0,150),At-FVector(0,0,250),Static,Query)&&Floor.ImpactNormal.Z>.95f&&FMath::Abs(Floor.ImpactPoint.Z-FloorZ)<10.f;}
        FNavLocation Projected;Clear&=Nav->ProjectPointToNavigation(Start,Projected,FVector(80,80,150),&Human->GetNavAgentPropertiesRef());
        FHitResult Wall;Clear&=!GetWorld()->LineTraceSingleByChannel(Wall,Start+FVector(0,0,55),Start+Direction*1200.f+FVector(0,0,25),ECC_Visibility,Query);
        if(Clear){LaneOrigin=Start;LaneForward=Direction;auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("isolated_actual_map_combat_lane"));Row->SetStringField(TEXT("origin"),Start.ToString());Row->SetStringField(TEXT("direction"),Direction.ToString());Row->SetNumberField(TEXT("opponent_distance_cm"),1200);Row->SetNumberField(TEXT("actual_capsule_half_height_cm"),Half);Row->SetStringField(TEXT("scope"),TEXT("Human and the existing mother opponent relocated onto source static floors; no substitute geometry, synthetic shot, ammo or damage."));Observations.Add(MakeShared<FJsonValueObject>(Row));return true;}
    }
    return false;
}
bool AHeliosBioHumanActionsQA::HasNativeFireLane(AHeliosBioNPC* Human,const FVector& TargetPosition,bool LiveTarget,FJsonObject* Evidence)
{
    FVector Eyes;FRotator Aim;Human->GetActorEyesViewPoint(Eyes,Aim);auto* Weapon=Human->GetEquippedWeapon();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BioHumanNativeFireLane),true,Human);Query.AddIgnoredActor(Weapon);TArray<AActor*> Attached;Human->GetAttachedActors(Attached,true,true);Query.AddIgnoredActors(Attached);
    for(auto Bot:Mode->Bots)if(Bot&&!Mode->AreOpponents(Human->ParticipantId,Bot->ParticipantId))Query.AddIgnoredActor(Bot);
    if(!LiveTarget)Query.AddIgnoredActor(Enemy.Get());
    const FVector TargetPoint=TargetPosition+FVector(0,0,25),Direction=LiveTarget?Aim.Vector():(TargetPoint-Eyes).GetSafeNormal();
    FHitResult EyeHit;const bool EyeBlocked=GetWorld()->LineTraceSingleByChannel(EyeHit,Eyes,TargetPoint,ECC_Visibility,Query);
    const bool EyeClear=!EyeBlocked||(LiveTarget&&EyeHit.GetActor()==Enemy.Get());
    auto* Controller=Cast<AAIController>(Human->GetController());
    const bool FocusCorrect=!LiveTarget||(Controller&&Controller->GetFocusActor()==Enemy.Get()&&FVector::DotProduct(Direction,(TargetPoint-Eyes).GetSafeNormal())>.999f);
    bool MuzzleClear=true,FlightClear=true;FVector Muzzle=Eyes,AimPoint=TargetPoint;FHitResult MuzzleHit,FlightHit;
    if(auto* Native=Cast<AHeliosYunSummerMegatronWeapon>(Weapon))
    {
        // Match the real Megatron launch query: the view-to-muzzle obstruction sweep
        // precedes its five-centimetre flight sphere. Eye sight alone cannot prove it.
        Muzzle=Native->MuzzleLocation();
        if(Evidence){Evidence->SetStringField(TEXT("actual_native_muzzle_component"),GetPathNameSafe(Native->Megatron.Get()));Evidence->SetStringField(TEXT("actual_native_muzzle_component_parent"),Native->Megatron?GetPathNameSafe(Native->Megatron->GetAttachParent()):TEXT("None"));Evidence->SetNumberField(TEXT("actual_native_muzzle_to_human_center_cm"),FVector::Dist(Muzzle,Human->GetActorLocation()));}
        MuzzleClear=!GetWorld()->SweepSingleByChannel(MuzzleHit,Eyes,Muzzle,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(5.f),Query);
        if(LiveTarget){FHitResult NativeView;GetWorld()->LineTraceSingleByChannel(NativeView,Eyes,Eyes+Direction*Native->ShotSettings.Range,ECC_Visibility,Query);AimPoint=NativeView.bBlockingHit?NativeView.ImpactPoint+Direction*4.f:Eyes+Direction*Native->ShotSettings.Range;}
        const bool FlightBlocked=GetWorld()->SweepSingleByChannel(FlightHit,Muzzle,AimPoint,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(5.f),Query);
        FlightClear=!FlightBlocked||(LiveTarget&&FlightHit.GetActor()==Enemy.Get());
    }
    if(Evidence)
    {
        Evidence->SetStringField(TEXT("actual_view_position"),Eyes.ToString());Evidence->SetStringField(TEXT("actual_native_muzzle_position"),Muzzle.ToString());Evidence->SetStringField(TEXT("actual_native_aim_point"),AimPoint.ToString());Evidence->SetStringField(TEXT("actual_controller_focus"),Controller?GetNameSafe(Controller->GetFocusActor()):TEXT("None"));Evidence->SetBoolField(TEXT("actual_focus_and_view_aim_match_enemy"),FocusCorrect);Evidence->SetBoolField(TEXT("actual_eye_sight_clear"),EyeClear);Evidence->SetBoolField(TEXT("actual_view_to_native_muzzle_sphere5_clear"),MuzzleClear);Evidence->SetBoolField(TEXT("actual_native_muzzle_flight_sphere5_clear"),FlightClear);Evidence->SetStringField(TEXT("actual_eye_sight_blocker"),GetNameSafe(EyeHit.GetActor()));Evidence->SetStringField(TEXT("actual_muzzle_sweep_blocker"),GetNameSafe(MuzzleHit.GetActor()));Evidence->SetStringField(TEXT("actual_muzzle_sweep_impact"),MuzzleHit.ImpactPoint.ToString());Evidence->SetStringField(TEXT("actual_flight_sweep_blocker"),GetNameSafe(FlightHit.GetActor()));Evidence->SetStringField(TEXT("actual_flight_sweep_impact"),FlightHit.ImpactPoint.ToString());Evidence->SetStringField(TEXT("actual_flight_sweep_component"),GetPathNameSafe(FlightHit.GetComponent()));
    }
    return EyeClear&&FocusCorrect&&MuzzleClear&&FlightClear;
}
bool AHeliosBioHumanActionsQA::MaintainFireTargetSight(AHeliosBioNPC* Human,double Now)
{
    const bool Clear=HasNativeFireLane(Human,Enemy->GetActorLocation(),true);
    if(Clear){LostFireSightAt=-1;return true;}
    if(LostFireSightAt<0)LostFireSightAt=Now;
    if(Now-LostFireSightAt<1.f)return false;
    // Reposition only the explicitly frozen opponent. The tested human retains production
    // navigation, pose, weapon timers and finite ammunition throughout this firearm fixture.
    const FVector First=(Enemy->GetActorLocation()-Human->GetActorLocation()).GetSafeNormal2D();FCollisionObjectQueryParams Static(ECC_WorldStatic);FCollisionQueryParams Query(SCENE_QUERY_STAT(BioHumanFireFixtureFloor),false,Human);Query.AddIgnoredActor(Enemy.Get());
    const float HumanFeet=Human->GetActorLocation().Z-Human->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    for(int32 Sample=0;Sample<16;++Sample)
    {
        const float Angle=2*PI*Sample/16.f;const FVector Direction(First.X*FMath::Cos(Angle)-First.Y*FMath::Sin(Angle),First.X*FMath::Sin(Angle)+First.Y*FMath::Cos(Angle),0);const FVector Wanted=Human->GetActorLocation()+Direction*1200.f;FHitResult Floor;
        if(!GetWorld()->LineTraceSingleByObjectType(Floor,FVector(Wanted.X,Wanted.Y,HumanFeet+30),FVector(Wanted.X,Wanted.Y,HumanFeet-30),Static,Query)||Floor.ImpactNormal.Z<.95f||FMath::Abs(Floor.ImpactPoint.Z-HumanFeet)>3.f)continue;
        const FVector Candidate(Wanted.X,Wanted.Y,Floor.ImpactPoint.Z+Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.f);
        const bool CapsuleClear=!GetWorld()->OverlapBlockingTestByChannel(Candidate,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius(),Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),Query);
        if(!CapsuleClear||!HasNativeFireLane(Human,Candidate,false))continue;
        auto Before=MakeShared<FJsonObject>();HasNativeFireLane(Human,Enemy->GetActorLocation(),true,&Before.Get());
        const FVector Previous=Enemy->GetActorLocation();if(!PlaceActor(Enemy.Get(),Candidate))continue;Enemy->bQAFreeze=true;++FireTargetRepositions;LostFireSightAt=-1;
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("frozen_target_fixture_reposition_for_real_fire_sight"));Row->SetStringField(TEXT("form"),Cases[SelectedCases[CurrentCase]].Form);Row->SetStringField(TEXT("previous_target_position"),Previous.ToString());Row->SetStringField(TEXT("actual_supported_target_position"),Enemy->GetActorLocation().ToString());Row->SetStringField(TEXT("tested_human_position"),Human->GetActorLocation().ToString());Row->SetObjectField(TEXT("previous_actual_native_geometry"),Before);Row->SetBoolField(TEXT("actual_capsule_overlap_clear"),CapsuleClear);Row->SetNumberField(TEXT("actual_target_capsule_radius_cm"),Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius());Row->SetNumberField(TEXT("actual_same_level_floor_delta_cm"),Floor.ImpactPoint.Z-HumanFeet);Row->SetNumberField(TEXT("unchanged_target_sampling_radius_cm"),1200);Row->SetStringField(TEXT("scope"),TEXT("Only the isolated opponent is relocated to actual same-level static support and clear native view/muzzle flight geometry; production human movement, aim, weapon, finite ammo and damage remain active."));Observations.Add(MakeShared<FJsonValueObject>(Row));return true;
    }
    return false;
}
bool AHeliosBioHumanActionsQA::PlaceMeleeOpponent(AHeliosBioNPC* Human)
{
    const FVector HumanLocation=Human->GetActorLocation();const float HumanFeet=HumanLocation.Z-Human->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    FCollisionObjectQueryParams Static(ECC_WorldStatic);FCollisionQueryParams Query(SCENE_QUERY_STAT(BioHumanMeleeFixture),false,Enemy.Get());
    TArray<AActor*> Attached;Enemy->GetAttachedActors(Attached,true,true);Query.AddIgnoredActors(Attached);
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());if(!Nav)return false;
    for(float Radius:{150.f,80.f})for(int32 Sample=0;Sample<16;++Sample)
    {
        const FVector Direction=LaneForward.RotateAngleAxis(Sample*22.5f,FVector::UpVector),Wanted=HumanLocation+Direction*Radius;FHitResult Floor;
        if(!GetWorld()->LineTraceSingleByObjectType(Floor,FVector(Wanted.X,Wanted.Y,HumanFeet+30),FVector(Wanted.X,Wanted.Y,HumanFeet-30),Static,Query)||Floor.ImpactNormal.Z<.95f||FMath::Abs(Floor.ImpactPoint.Z-HumanFeet)>3.f)continue;
        const FVector Candidate(Wanted.X,Wanted.Y,Floor.ImpactPoint.Z+Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.f);const auto Shape=FCollisionShape::MakeCapsule(Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius(),Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        if(GetWorld()->OverlapBlockingTestByChannel(Candidate,FQuat::Identity,ECC_Pawn,Shape,Query))continue;
        FCollisionQueryParams Corridor=Query;Corridor.AddIgnoredActor(Human);Human->GetAttachedActors(Attached,true,true);Corridor.AddIgnoredActors(Attached);FHitResult Obstruction;
        if(GetWorld()->SweepSingleByChannel(Obstruction,Candidate,HumanLocation,FQuat::Identity,ECC_Pawn,Shape,Corridor))continue;
        bool SameFloor=true;for(int32 Step=1;Step<8&&SameFloor;++Step){const FVector At=FMath::Lerp(Candidate,HumanLocation,Step/8.f);FHitResult Support;SameFloor=GetWorld()->LineTraceSingleByObjectType(Support,FVector(At.X,At.Y,HumanFeet+30),FVector(At.X,At.Y,HumanFeet-30),Static,Query)&&Support.ImpactNormal.Z>.95f&&FMath::Abs(Support.ImpactPoint.Z-HumanFeet)<=3.f;}
        if(!SameFloor)continue;
        FNavLocation Projected;if(!Nav->ProjectPointToNavigation(Candidate,Projected,FVector(25,25,90),&Enemy->GetNavAgentPropertiesRef())||FVector::Dist2D(Projected.Location,Candidate)>25.f)continue;
        auto* Path=Nav->FindPathToLocationSynchronously(GetWorld(),Projected.Location,Human->GetNavAgentLocation(),Enemy->GetController());
        if(!Path||!Path->IsValid()||Path->IsPartial()||Path->PathPoints.Num()<2||Path->GetPathLength()>Radius*2+80.f)continue;
        const FVector Previous=Enemy->GetActorLocation();if(!PlaceActor(Enemy.Get(),Candidate))continue;
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("same_level_real_mother_melee_fixture"));Row->SetStringField(TEXT("form"),Cases[SelectedCases[CurrentCase]].Form);Row->SetStringField(TEXT("human_unchanged_position"),HumanLocation.ToString());Row->SetStringField(TEXT("previous_mother_position"),Previous.ToString());Row->SetStringField(TEXT("actual_mother_position"),Enemy->GetActorLocation().ToString());Row->SetNumberField(TEXT("actual_supported_radius_cm"),Radius);Row->SetNumberField(TEXT("actual_floor_delta_cm"),Floor.ImpactPoint.Z-HumanFeet);Row->SetStringField(TEXT("source_floor_component"),GetPathNameSafe(Floor.GetComponent()));Row->SetBoolField(TEXT("actual_capsule_and_corridor_clear"),true);Row->SetBoolField(TEXT("actual_same_level_corridor_supported"),SameFloor);Row->SetNumberField(TEXT("complete_real_nav_path_length_cm"),Path->GetPathLength());Row->SetStringField(TEXT("scope"),TEXT("Only mother fixture placement; live human retains real production movement and firing, and actual mother Think/MeleeAttack must still infect and score."));Observations.Add(MakeShared<FJsonValueObject>(Row));return true;
    }
    return false;
}
void AHeliosBioHumanActionsQA::BindProjectileEvidence(AShooterWeapon* Weapon,int32 CaseIndex)
{
    Weapon->OnProjectilePresented.AddWeakLambda(this,[this,CaseIndex](AShooterProjectile* Projectile)
    {
        if(!Projectile)return;Projectile->OnPresentationImpact.AddWeakLambda(this,[this,CaseIndex](const FHitResult& Hit)
        {
            if(Stage!=5||!SelectedCases.IsValidIndex(CurrentCase)||SelectedCases[CurrentCase]!=CaseIndex||Hit.GetActor()!=Enemy.Get())return;
            ++ActualProjectileHits;auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("actual_original_firearm_projectile_hit"));Row->SetStringField(TEXT("form"),Cases[CaseIndex].Form);Row->SetStringField(TEXT("victim"),GetNameSafe(Hit.GetActor()));Row->SetStringField(TEXT("impact"),Hit.ImpactPoint.ToString());Observations.Add(MakeShared<FJsonValueObject>(Row));
        });
    });
}
bool AHeliosBioHumanActionsQA::BeginCase(double Now)
{
    if(!SelectedCases.IsValidIndex(CurrentCase))
    {
        Mode->BaseRoundSeed=Seed-Mode->RoundSerial*103;RoundSerialBefore=Mode->RoundSerial;
        Mode->Model.Config.PreparationSeconds=20;Mode->RestartBioRound();FreezeAll();
        if(Mode->Player)Mode->Player->SetActorHiddenInGame(false);
        Stage=8;Next=Now+3;return true;
    }
    auto& Case=Cases[SelectedCases[CurrentCase]];auto* Human=Case.Bot.Get();const auto* P=Human?Mode->Model.FindParticipant(Human->ParticipantId):nullptr;
    if(!Human||!P||!P->bAlive||P->Team!=EHeliosBioTeam::Human){AddBlocker(Case,TEXT("Selected real form is no longer a live human"));return false;}
    FreezeAll(true);
    if(!Enemy.IsValid()||!FindCombatLane(Human)||!PlaceActor(Human,LaneOrigin)||!PlaceActor(Enemy.Get(),LaneOrigin+LaneForward*1200.f))
    {AddBlocker(Case,TEXT("No clear 12m source-floor lane for this actual human and mother"));return false;}
    auto* Weapon=Human->GetEquippedWeapon();if(!Weapon){AddBlocker(Case,TEXT("Production human firearm unavailable"));return false;}
    OriginalHumanWeapon=Weapon;BindProjectileEvidence(Weapon,SelectedCases[CurrentCase]);const auto Native=ReadNativeWeapon(Weapon);NativeWeaponShotsBefore=Native.Shots;NativeWeaponHitsBefore=Native.DamageHits;LostFireSightAt=-1;
    if(Native.Shots<0||Native.DamageHits<0){AddBlocker(Case,TEXT("Native weapon class lacks known real damage provenance counters"));return false;}
    AmmoBefore=Weapon->GetBulletCount();ShotsBefore=Human->ShotsFired;ReserveBefore=Human->GetReserveAmmo();EnemyHealthBefore=Mode->Model.FindParticipant(Enemy->ParticipantId)->Health;ShooterDamageBefore=P->DamageDealt;
    SupplyCountBefore=LastObservedSupplyCount=P->SuppliesCollected;ActualSupplyAmmoAdded=0;ReloadSupplyAmmoAddedBefore=0;bSupplyAmmoProvenance=true;
    bReloadStarted=bReloadWorldClock=bTransitionSeen=bTransitionEnded=bDeathLastFrame=false;bNoHumanShotsAfter=true;ActualProjectileHits=0;Case.bEmpty=false;Case.Poses.Remove(TEXT("Fire"));Case.Poses.Remove(TEXT("ChangeClip"));Case.Poses.Remove(TEXT("Down"));
    Human->bQAFreeze=false;Enemy->bQAFreeze=true;
    const float Timeout=FMath::Clamp((Weapon->GetMagazineSize()+2)*Weapon->GetRefireRate()*2+10.f,15.f,60.f);
    Stage=5;StageAt=Now;Next=Now+Timeout;return true;
}
void AHeliosBioHumanActionsQA::SampleCombat(double Now)
{
    auto& Case=Cases[SelectedCases[CurrentCase]];auto* Human=Case.Bot.Get();auto* Weapon=Human?Human->GetEquippedWeapon():nullptr;const auto* State=Human?Mode->Model.FindParticipant(Human->ParticipantId):nullptr;
    if(!Human||!State)return;
    if(Stage==5&&State->Team==EHeliosBioTeam::Human&&Weapon)
    {
        if(Human->ShotsFired>ShotsBefore)SamplePose(Case,TEXT("Fire"));
        if(Weapon->GetBulletCount()==0&&Human->ShotsFired>ShotsBefore&&!Case.bEmpty){Case.bEmpty=true;EmptyShots=Human->ShotsFired;}
        if(Case.bEmpty&&Human->GetReloadRemaining()>0)
        {
            if(!bReloadStarted){bReloadStarted=true;ReloadStartedAt=Now;ReloadReserveBefore=Human->GetReserveAmmo();ReloadCooldownBefore=Human->GetReloadRemaining();ReloadSupplyCountBefore=State->SuppliesCollected;ReloadSupplyAmmoAddedBefore=ActualSupplyAmmoAdded;}
            bReloadWorldClock|=Human->GetReloadRemaining()<ReloadCooldownBefore;SamplePose(Case,TEXT("ChangeClip"));
        }
        const auto* Target=Mode->Model.FindParticipant(Enemy->ParticipantId);
        const auto Native=ReadNativeWeapon(Weapon);const bool Clear=MaintainFireTargetSight(Human,Now);
        auto* Driver=Weapon->GetFirstPersonMesh();auto* Hand=Driver?Cast<UHeliosHandAnimInstance>(Driver->GetAnimInstance()):nullptr;
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("production_fire_and_reload"));Row->SetStringField(TEXT("form"),Case.Form);Row->SetNumberField(TEXT("participant"),Human->ParticipantId);Row->SetNumberField(TEXT("world_seconds"),Now);Row->SetNumberField(TEXT("shots"),Human->ShotsFired);Row->SetNumberField(TEXT("ammo"),Weapon->GetBulletCount());Row->SetNumberField(TEXT("magazine_capacity"),Weapon->GetMagazineSize());Row->SetNumberField(TEXT("reserve"),Human->GetReserveAmmo());Row->SetNumberField(TEXT("reload_remaining"),Human->GetReloadRemaining());Row->SetNumberField(TEXT("victim_HP"),Target?Target->Health:0);Row->SetNumberField(TEXT("shooter_real_damage_dealt"),State->DamageDealt);Row->SetNumberField(TEXT("observed_AShooterProjectile_hits_only"),ActualProjectileHits);Row->SetStringField(TEXT("actual_native_mechanism"),Native.Mechanism);Row->SetNumberField(TEXT("native_weapon_committed_shots"),Native.Shots);Row->SetNumberField(TEXT("native_weapon_actual_ApplyDamage_hits"),Native.DamageHits);Row->SetStringField(TEXT("native_driver_clip"),Hand?GetPathNameSafe(Hand->Sequence.Get()):TEXT(""));Row->SetNumberField(TEXT("native_driver_clock_seconds"),Hand?Hand->SampleSeconds:0.f);Row->SetStringField(TEXT("actual_production_target"),GetNameSafe(Human->GetDesiredCombatTarget()));Row->SetStringField(TEXT("actual_human_position"),Human->GetActorLocation().ToString());Row->SetStringField(TEXT("actual_human_velocity"),Human->GetVelocity().ToString());Row->SetStringField(TEXT("actual_frozen_enemy_position"),Enemy->GetActorLocation().ToString());Row->SetBoolField(TEXT("actual_fire_sight_clear"),Clear);Row->SetStringField(TEXT("actual_movement_diagnostics"),Human->GetMovementDiagnostic());Observations.Add(MakeShared<FJsonValueObject>(Row));
        HasNativeFireLane(Human,Enemy->GetActorLocation(),true,&Row.Get());Row->SetNumberField(TEXT("actual_rules_normal_supply_count"),State->SuppliesCollected);Row->SetNumberField(TEXT("exact_actual_supply_ammo_income"),ActualSupplyAmmoAdded);Row->SetBoolField(TEXT("supply_income_provenance"),bSupplyAmmoProvenance);
    }
    if(Stage==6||Stage==7)
    {
        if(State->Team==EHeliosBioTeam::Zombie)
        {
            if(!bTransitionSeen)
            {
                bTransitionSeen=Human->IsHumanInfectionTransition();InfectionAt=Now;HumanShotsAtInfection=Human->ShotsFired;
                HumanAmmoAtInfection=OriginalHumanWeapon.IsValid()?OriginalHumanWeapon->GetBulletCount():0;DeathDuration=Human->GetHumanInfectionTransitionDuration();
            }
            if(bTransitionSeen)
            {
                bNoHumanShotsAfter&=Human->ShotsFired==HumanShotsAtInfection;
                if(OriginalHumanWeapon.IsValid())bNoHumanShotsAfter&=OriginalHumanWeapon->GetBulletCount()==HumanAmmoAtInfection;
                SamplePose(Case,TEXT("Down"));auto* Presentation=Human->FindComponentByClass<UHeliosCharacterPresentationComponent>();auto* Mesh=Presentation?Presentation->GetArmsMesh():nullptr;auto* Node=Mesh?Mesh->GetSingleNodeInstance():nullptr;
                auto* Clip=Human->GetHumanInfectionTransitionClip();if(Node&&Clip&&Node->GetCurrentAsset()==Clip&&Human->IsHumanInfectionTransition()&&FMath::IsNearlyEqual(Node->GetCurrentTime(),Clip->GetPlayLength(),.0001f))bDeathLastFrame=true;
                auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("real_infection_death_transition"));Row->SetStringField(TEXT("form"),Case.Form);Row->SetNumberField(TEXT("participant"),Human->ParticipantId);Row->SetBoolField(TEXT("transition_active"),Human->IsHumanInfectionTransition());Row->SetBoolField(TEXT("transition_blocked"),Human->IsHumanInfectionTransitionBlocked());Row->SetNumberField(TEXT("transition_age"),Human->GetHumanInfectionTransitionAge());Row->SetNumberField(TEXT("transition_duration"),Human->GetHumanInfectionTransitionDuration());Row->SetStringField(TEXT("original_Down_clip"),GetPathNameSafe(Clip));Row->SetBoolField(TEXT("source_final_frame_committed"),bDeathLastFrame);Row->SetBoolField(TEXT("human_firearm_stopped_without_QA_freeze"),bNoHumanShotsAfter);Observations.Add(MakeShared<FJsonValueObject>(Row));
            }
        }
    }
    if(Stage==6)
    {
        auto Approach=MakeShared<FJsonObject>();Approach->SetStringField(TEXT("kind"),TEXT("real_mother_AI_melee_approach"));Approach->SetStringField(TEXT("form"),Case.Form);Approach->SetNumberField(TEXT("world_seconds"),Now);Approach->SetStringField(TEXT("actual_human_position"),Human->GetActorLocation().ToString());Approach->SetStringField(TEXT("actual_mother_position"),Enemy->GetActorLocation().ToString());Approach->SetNumberField(TEXT("actual_center_distance_cm"),FVector::Dist(Human->GetActorLocation(),Enemy->GetActorLocation()));Approach->SetNumberField(TEXT("actual_feet_height_difference_cm"),(Human->GetActorLocation().Z-Human->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())-(Enemy->GetActorLocation().Z-Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));Approach->SetNumberField(TEXT("actual_mother_melee_count"),Enemy->MeleeAttacks);Approach->SetStringField(TEXT("actual_mother_target"),GetNameSafe(Enemy->GetDesiredCombatTarget()));Approach->SetStringField(TEXT("actual_mother_movement_diagnostics"),Enemy->GetMovementDiagnostic());Observations.Add(MakeShared<FJsonValueObject>(Approach));
    }
}
void AHeliosBioHumanActionsQA::ObserveSupplyIncome(AHeliosBioNPC* Human,double Now)
{
    const auto* State=Human?Mode->Model.FindParticipant(Human->ParticipantId):nullptr;if(!State||State->Team!=EHeliosBioTeam::Human||State->SuppliesCollected==LastObservedSupplyCount)return;
    auto& Case=Cases[SelectedCases[CurrentCase]];const int32 Pickups=State->SuppliesCollected-LastObservedSupplyCount;
    // The production rules count real pickups independently of reserve growth.
    // This upper bound proves that no pickup can clamp, regardless of reload ordering.
    const bool Unclamped=Pickups>0&&ReserveBefore+ActualSupplyAmmoAdded+Pickups*300<=1800;bSupplyAmmoProvenance&=Unclamped;
    auto Pickup=MakeShared<FJsonObject>();Pickup->SetStringField(TEXT("kind"),TEXT("actual_rules_normal_supply_ammo_income"));Pickup->SetStringField(TEXT("form"),Case.Form);Pickup->SetNumberField(TEXT("world_seconds"),Now);Pickup->SetNumberField(TEXT("rules_count_before"),LastObservedSupplyCount);Pickup->SetNumberField(TEXT("actual_rules_count_after"),State->SuppliesCollected);Pickup->SetNumberField(TEXT("actual_pickup_count_delta"),Pickups);Pickup->SetNumberField(TEXT("actual_reserve_after"),Human->GetReserveAmmo());Pickup->SetNumberField(TEXT("production_ammo_per_normal_pickup"),300);Pickup->SetNumberField(TEXT("production_reserve_cap"),1800);Pickup->SetBoolField(TEXT("exact_unclamped_income_provenance"),Unclamped);Observations.Add(MakeShared<FJsonValueObject>(Pickup));
    if(Unclamped)ActualSupplyAmmoAdded+=Pickups*300;else AddBlocker(Case,TEXT("Actual supply count changed at an ambiguous reserve cap/reload ordering; exact income cannot be inferred"));LastObservedSupplyCount=State->SuppliesCollected;
}
void AHeliosBioHumanActionsQA::EndCase(double Now)
{
    auto& Case=Cases[SelectedCases[CurrentCase]];if(auto* Human=Case.Bot.Get())Human->bQAFreeze=true;if(Enemy.IsValid())Enemy->bQAFreeze=true;
    ++CurrentCase;Stage=4;Next=Now+.2;
}
void AHeliosBioHumanActionsQA::Finish(const FString& Reason)
{
    Stage=99;GlobalBlocker=Reason;if(Mode)FreezeAll();
    TArray<TSharedPtr<FJsonValue>> Rows;int32 Complete=0;TSet<FString> InitialForms;
    for(const auto& Case:Cases)InitialForms.Add(Case.Form);
    for(int32 Index:SelectedCases)
    {
        const auto& Case=Cases[Index];const bool Passed=Case.Blocker.IsEmpty()&&Case.bInitialStanding&&Case.bMove&&Case.bFire&&Case.bEmpty&&Case.bReload&&Case.bDeath&&Case.bInfection&&Case.bStoppedHumanWeapon&&Case.bRestart;if(Passed)++Complete;
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("form"),Case.Form);Row->SetNumberField(TEXT("participant"),Case.Bot.IsValid()?Case.Bot->ParticipantId:-1);Row->SetStringField(TEXT("weapon"),Case.Loadout.PrimaryWeaponId.ToString());Row->SetBoolField(TEXT("passed"),Passed);Row->SetStringField(TEXT("blocker"),Case.Blocker);Row->SetBoolField(TEXT("initial_standing"),Case.bInitialStanding);Row->SetBoolField(TEXT("real_preparation_move"),Case.bMove);Row->SetNumberField(TEXT("preparation_actual_ground_travel_cm"),Case.PreparationTravel);Row->SetBoolField(TEXT("real_fire_pose_ammo_and_damage"),Case.bFire);Row->SetBoolField(TEXT("natural_empty_magazine"),Case.bEmpty);Row->SetBoolField(TEXT("real_reload_pose_clock_and_reserve"),Case.bReload);Row->SetBoolField(TEXT("original_Down_pose_and_final_frame"),Case.bDeath);Row->SetBoolField(TEXT("real_zombie_AI_melee_infection"),Case.bInfection);Row->SetBoolField(TEXT("former_human_firearm_stopped"),Case.bStoppedHumanWeapon);Row->SetBoolField(TEXT("ordinary_restart_same_seed_loadout_and_sole"),Case.bRestart);Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    const bool Passed=Failures==0&&Reason.IsEmpty()&&bInitialRosterReady&&SelectedCases.Num()==InitialForms.Num()&&Complete==InitialForms.Num()&&Complete>=17;
    auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("passed"),Passed);Report->SetStringField(TEXT("status"),Passed?TEXT("all_actual_TP_forms_observed"):TEXT("incomplete_or_blocked"));Report->SetStringField(TEXT("blocker"),Reason);Report->SetNumberField(TEXT("failures"),Failures);Report->SetNumberField(TEXT("actual_NPC_count"),Cases.Num());Report->SetNumberField(TEXT("actual_distinct_forms"),InitialForms.Num());Report->SetNumberField(TEXT("selected_form_count"),SelectedCases.Num());Report->SetNumberField(TEXT("fully_passed_form_count"),Complete);Report->SetBoolField(TEXT("all_human_gameplay_actions_verified"),Passed);Report->SetBoolField(TEXT("first_person_all_forms_verified"),false);Report->SetBoolField(TEXT("natural_round_verified"),false);Report->SetBoolField(TEXT("physical_device_input_verified"),false);Report->SetBoolField(TEXT("gameplay_gate_changed"),false);Report->SetNumberField(TEXT("standing_sole_tolerance_cm"),3);Report->SetStringField(TEXT("map"),GetWorld()->GetOutermost()->GetName());Report->SetStringField(TEXT("expected_map"),ExpectedMap);
    Report->SetStringField(TEXT("scope"),TEXT("TP-only isolated real 29-NPC roster. Source bindings warm up in a separate preparation round, then a fresh same-seed ordinary round has 20s preparation and real production defense movement. One representative per actual form uses production Think/weapon firing, natural full ammunition depletion and reserve-consuming reload; a real mother AI melee infects it and production Down completes before zombie presentation. Other actors are hidden/frozen/relocated, target health and round length are isolated overrides. Restart repeats the seed to check exact independent saved in-memory loadouts and actual skinned shoe soles. No manual source animation, ammo fill, rule Tick, tested shot/damage substitute or readiness-gate write; player loadout is preserved. This does not prove FP actions, raw keyboard/mouse, natural-round occurrence or original numeric balance."));
    Report->SetNumberField(TEXT("frozen_enemy_fire_sight_fixture_repositions"),FireTargetRepositions);
    Report->SetStringField(TEXT("native_damage_provenance"),TEXT("Read-only original gun counters increase only after real ApplyPointDamage/ApplyDamage and victim HP loss; matched to shooter rules damage, victim HP and native/NPC shot-ammo conservation. AShooterProjectile event counts cover only that actor class and are never substituted for native hitscan or MegatronGrenade."));
    Report->SetStringField(TEXT("Down_final_frame_provenance"),TEXT("A fresh production HumanDownCommit sequence captures the actual SingleNode final asset/time, body asset and successful presentation commit before human-to-zombie handoff. Independent observed Down clock and bone motion remain required."));
    Report->SetStringField(TEXT("actual_supply_ammo_accounting"),TEXT("Normal boxes remain enabled. Independently observed real Rules SuppliesCollected increments contribute exactly the production 300 ammo only where the cumulative upper bound proves that the 1800 reserve cap cannot clamp. Native/NPC shots, current magazine, reserve expenditure and actual supply income must balance exactly. Ambiguous cap/reload ordering is blocked."));
    Report->SetStringField(TEXT("opponent_fixture_geometry"),TEXT("Frozen fire opponents may be relocated only to actual same-level source floors at the unchanged 1200cm radius with capsule overlap clearance and the real view-to-muzzle and native five-centimetre flight sweeps. The production mother is placed at a supported same-level 150cm or 80cm candidate with a clear capsule corridor and complete short real nav path. The human is never relocated during those stages and real AI melee/infection counters remain mandatory."));
    Report->SetArrayField(TEXT("forms"),Rows);Report->SetArrayField(TEXT("checks"),Checks);Report->SetArrayField(TEXT("observations"),Observations);
    FString Text;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Out/TEXT("human_actions_report.json")));FPlatformMisc::RequestExit(false);
}
void AHeliosBioHumanActionsQA::Tick(float Dt)
{
    Super::Tick(Dt);const double Now=GetWorld()->GetTimeSeconds();if(Stage==99)return;
    if(Stage==0)
    {
        FParse::Value(FCommandLine::Get(),TEXT("BioHumanActionsQAOut="),Out);FParse::Value(FCommandLine::Get(),TEXT("BioExpectedMap="),ExpectedMap);
        if(Out.IsEmpty()){Finish(TEXT("BioHumanActionsQAOut missing"));return;}
        IFileManager::Get().MakeDirectory(*Out,true);Mode=AHeliosBioHotelGameMode::Find(GetWorld());if(!Mode||!Mode->Player){if(Now>30)Finish(TEXT("Actual game mode/player missing"));return;}
        Started=Now;Check(TEXT("expected_source_map"),GetWorld()->GetOutermost()->GetName()==ExpectedMap);
        Mode->Model.Config.PreparationSeconds=120;Mode->Model.Config.RoundSeconds=1400;Mode->Model.Config.InitialMotherCount=1;Mode->Model.Config.bKeepLocalPlayerHuman=true;Mode->Model.Config.bMothersAreTerminators=false;Mode->Model.Config.MotherHealth=Mode->Model.Config.ZombieHealth=100000000.f;
        bool Found=false;for(int32 CandidateSeed=0;CandidateSeed<20000;++CandidateSeed){FHeliosBioRules Candidate;Candidate.Config=Mode->Model.Config;Candidate.Config.PreparationSeconds=20;Candidate.StartRound(30,CandidateSeed);Candidate.Tick(20.1f);const auto* Last=Candidate.FindParticipant(29);if(Last&&Last->bMother){Seed=CandidateSeed;Found=true;break;}}
        if(!Found){Finish(TEXT("No real mother seed preserving the repeated-form roster"));return;}
        Mode->BaseRoundSeed=Seed-Mode->RoundSerial*103;Mode->RestartBioRound();FreezeAll();Next=Now+3;Stage=1;return;
    }
    if(Now-Started>1300){Finish(FString::Printf(TEXT("Bounded human action fixture stalled at stage %d form %d"),Stage,CurrentCase));return;}
    if(Stage==5&&SelectedCases.IsValidIndex(CurrentCase))ObserveSupplyIncome(Cases[SelectedCases[CurrentCase]].Bot.Get(),Now);
    if(Stage==3&&Now-LastSample>=.04){SamplePreparation();LastSample=Now;}
    if((Stage==5||Stage==6||Stage==7)&&Now-LastSample>=.04){SampleCombat(Now);LastSample=Now;}
    if(Stage==1&&Now>=Next)
    {
        if(!BuildRoster()){Finish(TEXT("Actual roster does not cover all required forms"));return;}
        for(auto& Case:Cases){Case.bInitialStanding=MeasureStanding(Case,TEXT("warm_spawn"));if(!Case.bInitialStanding)AddBlocker(Case,TEXT("Actual initial skinned sole/support failed"));}
        Mode->Model.Config.PreparationSeconds=20;Mode->BaseRoundSeed=Seed-Mode->RoundSerial*103;Mode->RestartBioRound();FreezeAll();Next=Now+3;Stage=2;return;
    }
    if(Stage==2&&Now>=Next)
    {
        if(Mode->Model.GetPhase()!=EHeliosBioPhase::Preparation){Finish(TEXT("Normal 20s preparation expired before the real movement fixture"));return;}
        for(auto& Case:Cases)if(auto* Bot=Case.Bot.Get()){const bool Same=Bot->GetHumanLoadout()==Case.Loadout;Check(Case.Form+TEXT("_same_seed_independent_loadout"),Same);if(!Same)AddBlocker(Case,TEXT("Same-seed loadout changed before preparation actions"));Case.bInitialStanding&=MeasureStanding(Case,TEXT("normal_spawn"));Case.PreviousPosition=Bot->GetActorLocation();Bot->bQAFreeze=false;}
        Stage=3;StageAt=Now;return;
    }
    if(Stage==3&&Mode->Model.GetPhase()==EHeliosBioPhase::Active)
    {
        FreezeAll();Enemy=Cast<AHeliosBioNPC>(Mode->ParticipantActor(29));const auto* Mother=Mode->Model.FindParticipant(29);
        if(!Enemy.IsValid()||!Mother||!Mother->bMother||Mother->Team!=EHeliosBioTeam::Zombie||!SelectRepresentatives()){Finish(TEXT("Actual mother/remaining human forms did not match the isolated real seed"));return;}
        for(int32 Index:SelectedCases){auto& Case=Cases[Index];Check(Case.Form+TEXT("_actual_preparation_move_and_native_pose"),Case.bMove);if(!Case.bMove)AddBlocker(Case,TEXT("20s preparation did not show >40cm actual grounded Move with native clock/bone changes"));}
        if(auto* Target=const_cast<FHeliosBioParticipant*>(Mother)){Target->SkillCooldownRemaining=Target->SecondaryCooldownRemaining=1400;Target->bMother=false;}
        for(auto Supply:Mode->Supplies)if(IsValid(Supply))Supply->SetAvailable(false);
        Mode->Player->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mode->Player->GetCharacterMovement()->DisableMovement();Mode->Player->SetActorHiddenInGame(true);Mode->Player->SetActorLocation(FVector(1000000,1000000,1000000),false);
        CurrentCase=0;Stage=4;Next=Now+.2;return;
    }
    if(Stage==4&&Now>=Next){if(!BeginCase(Now)){Check(Cases[SelectedCases[CurrentCase]].Form+TEXT("_production_case_setup"),false);EndCase(Now);}return;}
    if(Stage==5)
    {
        auto& Case=Cases[SelectedCases[CurrentCase]];auto* Human=Case.Bot.Get();auto* Weapon=Human?Human->GetEquippedWeapon():nullptr;const auto* HumanState=Human?Mode->Model.FindParticipant(Human->ParticipantId):nullptr;
        if(!Human||!Weapon||!HumanState||HumanState->Team!=EHeliosBioTeam::Human){AddBlocker(Case,TEXT("Human or real firearm disappeared during production firing"));Check(Case.Form+TEXT("_real_human_firearm_retained"),false);EndCase(Now);return;}
        if(bReloadStarted&&Human->GetReloadRemaining()<=0&&Weapon->GetBulletCount()>0)
        {
            const int32 Loaded=ReloadReserveBefore+(ActualSupplyAmmoAdded-ReloadSupplyAmmoAddedBefore)-Human->GetReserveAmmo(),Refired=Human->ShotsFired-EmptyShots;
            Case.bReload=PosePassed(Case,TEXT("ChangeClip"))&&bReloadWorldClock&&bSupplyAmmoProvenance&&Loaded==Weapon->GetMagazineSize()&&Loaded>0&&Weapon->GetBulletCount()+Refired==Loaded;
            const auto Native=ReadNativeWeapon(Weapon);const int32 ActualShots=Human->ShotsFired-ShotsBefore;
            const bool NativeAmmoConserved=bSupplyAmmoProvenance&&Native.Shots-NativeWeaponShotsBefore==ActualShots&&AmmoBefore+ActualSupplyAmmoAdded+(ReserveBefore-Human->GetReserveAmmo())-Weapon->GetBulletCount()==ActualShots;
            const auto* Target=Mode->Model.FindParticipant(Enemy->ParticipantId);Case.bFire=ActualShots>0&&AmmoBefore>0&&PosePassed(Case,TEXT("Fire"))&&Target&&Target->Health<EnemyHealthBefore&&HumanState->DamageDealt>ShooterDamageBefore&&Native.DamageHits>NativeWeaponHitsBefore&&NativeAmmoConserved;
            auto Fire=MakeShared<FJsonObject>();Fire->SetStringField(TEXT("kind"),TEXT("strict_fire_acceptance_components"));Fire->SetStringField(TEXT("form"),Case.Form);Fire->SetBoolField(TEXT("native_Fire_clock_and_bone_pose"),PosePassed(Case,TEXT("Fire")));Fire->SetNumberField(TEXT("actual_NPC_shot_delta"),ActualShots);Fire->SetNumberField(TEXT("actual_native_shot_delta"),Native.Shots-NativeWeaponShotsBefore);Fire->SetNumberField(TEXT("actual_native_ApplyDamage_hit_delta"),Native.DamageHits-NativeWeaponHitsBefore);Fire->SetNumberField(TEXT("actual_target_HP_loss"),Target?EnemyHealthBefore-Target->Health:0);Fire->SetNumberField(TEXT("actual_attributed_damage_delta"),HumanState->DamageDealt-ShooterDamageBefore);Fire->SetNumberField(TEXT("actual_rules_supply_count_before"),SupplyCountBefore);Fire->SetNumberField(TEXT("actual_rules_supply_count_after"),HumanState->SuppliesCollected);Fire->SetNumberField(TEXT("exact_actual_supply_ammo_income"),ActualSupplyAmmoAdded);Fire->SetBoolField(TEXT("supply_income_provenance"),bSupplyAmmoProvenance);Fire->SetBoolField(TEXT("exact_native_NPC_and_ammo_conservation"),NativeAmmoConserved);Fire->SetNumberField(TEXT("ammo_before"),AmmoBefore);Fire->SetNumberField(TEXT("actual_ammo_after"),Weapon->GetBulletCount());Fire->SetNumberField(TEXT("reserve_before"),ReserveBefore);Fire->SetNumberField(TEXT("actual_reserve_after"),Human->GetReserveAmmo());Fire->SetNumberField(TEXT("reload_supply_count_before"),ReloadSupplyCountBefore);Fire->SetBoolField(TEXT("real_reload_conservation"),Case.bReload);HasNativeFireLane(Human,Enemy->GetActorLocation(),true,&Fire.Get());Observations.Add(MakeShared<FJsonValueObject>(Fire));
            UE_LOG(LogTemp,Display,TEXT("BIO_HUMAN_FIRE_COMPONENTS form=%s pose=%d NPC_shots=%d native_shots=%d damage_hits=%d HP_loss=%.3f attributed_damage=%.3f rules_supply_delta=%d supply_ammo=%d supply_exact=%d ammo_conserved=%d"),*Case.Form,PosePassed(Case,TEXT("Fire")),ActualShots,Native.Shots-NativeWeaponShotsBefore,Native.DamageHits-NativeWeaponHitsBefore,Target?EnemyHealthBefore-Target->Health:0,HumanState->DamageDealt-ShooterDamageBefore,HumanState->SuppliesCollected-SupplyCountBefore,ActualSupplyAmmoAdded,bSupplyAmmoProvenance,NativeAmmoConserved);
            Check(Case.Form+TEXT("_native_fire_ammo_and_actual_attributed_damage"),Case.bFire);Check(Case.Form+TEXT("_naturally_empty_magazine"),Case.bEmpty);Check(Case.Form+TEXT("_native_reload_real_clock_and_reserve_conservation"),Case.bReload);
            if(!Case.bFire)AddBlocker(Case,TEXT("Real native mechanism damage provenance/ammo conservation or declared native Fire clock/pose not observed"));if(!Case.bReload)AddBlocker(Case,TEXT("Natural ChangeClip clock/pose or ammunition conservation failed"));
            const auto* Attacker=Mode->Model.FindParticipant(Enemy->ParticipantId);EnemyInfectionsBefore=Attacker->Infections;EnemyMeleeBefore=Enemy->MeleeAttacks;OriginalHumanWeapon=Weapon;
            HumanDownSequenceBefore=Human->GetHumanDownCommit().Sequence;
            if(auto* Presentation=Human->FindComponentByClass<UHeliosCharacterPresentationComponent>()){HumanDownPoseCommitCountBefore=Presentation->GetPoseCommitCount();HumanDownPoseCommitFrameBefore=Presentation->GetPoseCommitFrame();}
            if(!PlaceMeleeOpponent(Human)){AddBlocker(Case,TEXT("No same-level source support and clear capsule/path for a real 150cm or 80cm mother melee approach"));Check(Case.Form+TEXT("_real_same_level_mother_fixture_available"),false);EndCase(Now);return;}
            // Leave the human's own gun/AI running: only the production infection transition may stop it.
            Enemy->bQAFreeze=false;Stage=6;StageAt=Now;Next=Now+12;return;
        }
        if(Now>=Next){AddBlocker(Case,TEXT("Production firing did not complete natural empty-magazine reload within its weapon-derived timeout"));Check(Case.Form+TEXT("_natural_full_magazine_and_reload_completed"),false);EndCase(Now);}return;
    }
    if(Stage==6)
    {
        auto& Case=Cases[SelectedCases[CurrentCase]];auto* Human=Case.Bot.Get();const auto* State=Human?Mode->Model.FindParticipant(Human->ParticipantId):nullptr;const auto* Attacker=Mode->Model.FindParticipant(Enemy->ParticipantId);
        if(State&&State->Team==EHeliosBioTeam::Zombie)
        {
            Case.bInfection=Enemy->MeleeAttacks>EnemyMeleeBefore&&Attacker&&Attacker->Infections>EnemyInfectionsBefore;Check(Case.Form+TEXT("_actual_production_melee_infection_and_score"),Case.bInfection);
            Enemy->bQAFreeze=true;Stage=7;Next=Now+FMath::Max(3.f,Human->GetHumanInfectionTransitionDuration()+2.f);return;
        }
        if(Now>=Next){AddBlocker(Case,TEXT("Actual zombie chase/melee did not infect the live source human"));Check(Case.Form+TEXT("_real_melee_infection_occurred"),false);EndCase(Now);}return;
    }
    if(Stage==7)
    {
        auto& Case=Cases[SelectedCases[CurrentCase]];auto* Human=Case.Bot.Get();auto* Presentation=Human?Human->FindComponentByClass<UHeliosCharacterPresentationComponent>():nullptr;
        if(Human&&!Human->IsHumanInfectionTransition()&&bTransitionSeen)
        {
            const auto& Commit=Human->GetHumanDownCommit();const auto* DownClips=Case.Clips.Find(TEXT("Down"));bool Declared=false;
            if(DownClips)for(const auto& Clip:*DownClips)Declared|=Clip.IsValid()&&Clip==Commit.Clip;
            const bool FinalCommit=Commit.Sequence==HumanDownSequenceBefore+1&&Declared&&Commit.Body==Case.Body&&Commit.Clip.IsValid()&&Commit.Body.IsValid()&&Commit.Duration>0&&FMath::IsNearlyEqual(Commit.SampleSeconds,Commit.Duration,.0001f)&&FMath::IsNearlyEqual(Commit.Duration,Commit.Clip->GetPlayLength(),.0001f)&&Commit.PoseCommitCount>HumanDownPoseCommitCountBefore&&Commit.PoseCommitFrame>HumanDownPoseCommitFrameBefore&&Commit.PoseCommitFrame<=GFrameCounter;
            // This is a read-only snapshot from the real successful production handoff, not
            // a recreated pose or timer inference after that handoff replaced the human mesh.
            bDeathLastFrame=FinalCommit;
            auto Final=MakeShared<FJsonObject>();Final->SetStringField(TEXT("kind"),TEXT("real_production_Down_final_commit_snapshot"));Final->SetStringField(TEXT("form"),Case.Form);Final->SetNumberField(TEXT("participant"),Human->ParticipantId);Final->SetNumberField(TEXT("sequence_before_infection"),HumanDownSequenceBefore);Final->SetNumberField(TEXT("actual_completion_sequence"),Commit.Sequence);Final->SetStringField(TEXT("actual_final_clip"),GetPathNameSafe(Commit.Clip.Get()));Final->SetStringField(TEXT("actual_final_body"),GetPathNameSafe(Commit.Body.Get()));Final->SetNumberField(TEXT("actual_final_sample_seconds"),Commit.SampleSeconds);Final->SetNumberField(TEXT("source_duration"),Commit.Duration);Final->SetStringField(TEXT("actual_final_pose_commit_frame"),FString::Printf(TEXT("%llu"),static_cast<unsigned long long>(Commit.PoseCommitFrame)));Final->SetNumberField(TEXT("actual_final_pose_commit_count"),Commit.PoseCommitCount);Final->SetBoolField(TEXT("fresh_matching_source_final_frame_committed"),FinalCommit);Observations.Add(MakeShared<FJsonValueObject>(Final));
            bTransitionEnded=true;Case.bDeath=PosePassed(Case,TEXT("Down"))&&bDeathLastFrame&&DeathDuration>0;
            auto* Body=Presentation?Presentation->GetArmsMesh():nullptr;
            Case.bStoppedHumanWeapon=bNoHumanShotsAfter&&Human->GetReloadRemaining()==0&&(!Human->GetEquippedWeapon()||Human->GetEquippedWeapon()!=OriginalHumanWeapon.Get())&&Body&&Body->GetSkeletalMeshAsset()!=Case.Body.Get();
            Check(Case.Form+TEXT("_real_Down_clock_pose_final_frame_before_zombie"),Case.bDeath);Check(Case.Form+TEXT("_production_infection_stops_former_human_gun"),Case.bStoppedHumanWeapon);
            if(!Case.bDeath)AddBlocker(Case,TEXT("Original Down source clock/pose or actual final-frame commit absent"));if(!Case.bStoppedHumanWeapon)AddBlocker(Case,TEXT("Production infection did not stop and retire the former human firearm"));
            FScreenshotRequest::RequestScreenshot(Out/(TEXT("infected_")+FString::FromInt(CurrentCase)+TEXT(".png")),true,false);EndCase(Now);return;
        }
        if(Human&&Human->IsHumanInfectionTransitionBlocked()){AddBlocker(Case,TEXT("Production source human infection transition is explicitly blocked (binding or pose commit)"));Check(Case.Form+TEXT("_production_Down_transition_available"),false);EndCase(Now);return;}
        if(Now>=Next){AddBlocker(Case,TEXT("Real infection Down transition/final-frame handoff did not complete"));Check(Case.Form+TEXT("_real_Down_transition_completed"),false);EndCase(Now);}return;
    }
    if(Stage==8&&Now>=Next)
    {
        Check(TEXT("ordinary_same_seed_restart_advanced_serial"),Mode->RoundSerial>RoundSerialBefore&&Mode->CurrentRoundSeed==Seed);
        for(auto& Case:Cases)if(auto* Bot=Case.Bot.Get())
        {
            const auto* State=Mode->Model.FindParticipant(Bot->ParticipantId);const bool Restored=State&&State->bAlive&&State->Team==EHeliosBioTeam::Human&&Bot->GetHumanLoadout()==Case.Loadout&&!Bot->IsHumanInfectionTransition()&&!Bot->IsHumanInfectionTransitionBlocked()&&Bot->GetEquippedWeapon()&&Bot->GetCapsuleComponent()->GetCollisionEnabled()==ECollisionEnabled::QueryAndPhysics;
            Check(Case.Form+TEXT("_restart_restores_own_human_loadout"),Restored);Case.bRestart=Restored&&MeasureStanding(Case,TEXT("round_restart"));if(!Case.bRestart)AddBlocker(Case,TEXT("Ordinary restart failed independent loadout/presentation/collision/strict sole restoration"));
        }
        Finish();return;
    }
}
