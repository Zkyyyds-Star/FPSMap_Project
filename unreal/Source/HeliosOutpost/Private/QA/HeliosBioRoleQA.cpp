#include "QA/HeliosBioRoleQA.h"
#include "Bio/HeliosBioHotelGameMode.h"
#include "Bio/HeliosBioNPC.h"
#include "Bio/HeliosBioPlayer.h"
#include "Bio/HeliosBioRoleDefinition.h"
#include "Bio/HeliosBioArt.h"
#include "Bio/HeliosBioSupply.h"
#include "Bio/HeliosBioFourSlashComponent.h"
#include "Bio/HeliosBioSkillProjectile.h"
#include "Bio/HeliosBioTurret.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraTypes.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "SkeletalRenderPublic.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "InputCoreTypes.h"

namespace
{
constexpr float FixtureGroundClearance=2.f;
bool IsFootBone(const FString& Name)
{return Name.Contains(TEXT("foot"),ESearchCase::IgnoreCase)||Name.Contains(TEXT("toe"),ESearchCase::IgnoreCase)||Name.Contains(TEXT("ankle"),ESearchCase::IgnoreCase);}
}
AHeliosBioRoleQA::AHeliosBioRoleQA(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;}
void AHeliosBioRoleQA::EndPlay(const EEndPlayReason::Type Reason)
{
    if(bManualSpectator&&Stage!=99&&!Out.IsEmpty())
    {bManualSpectatorFinished=true;ManualSpectatorStopReason=TEXT("Runtime shutdown; independent observer retains actual exit/input events");Finish();}
    Super::EndPlay(Reason);
}
void AHeliosBioRoleQA::Check(const TCHAR* Name,bool Passed)
{Results.Emplace(FString(Name),Passed);if(!Passed)++Failures;UE_LOG(LogTemp,Display,TEXT("BIO_ROLE_QA %s %s"),Name,Passed?TEXT("PASS"):TEXT("FAIL"));}
void AHeliosBioRoleQA::Capture(const TCHAR* Name)
{FScreenshotRequest::RequestScreenshot(Out/(FString(Name)+TEXT(".png")),true,false);}
void AHeliosBioRoleQA::Finish(const FString& Blocker)
{
    RemoveObstacle();Stage=99;auto Report=MakeShared<FJsonObject>();Report->SetStringField(TEXT("role"),TestRole);
    Report->SetBoolField(TEXT("passed"),!bManualSpectator&&Failures==0&&Blocker.IsEmpty());Report->SetStringField(TEXT("status"),!Blocker.IsEmpty()?TEXT("blocked"):Failures?TEXT("failed"):bManualSpectator?TEXT("manual_spectator_fixture_recorded"):TEXT("tested"));Report->SetStringField(TEXT("blocker"),Blocker);
    Report->SetBoolField(TEXT("manual_spectator_fixture"),bManualSpectator);Report->SetBoolField(TEXT("manual_spectator_fixture_prepared"),bManualSpectatorPrepared);Report->SetBoolField(TEXT("manual_spectator_observation_finished"),bManualSpectatorFinished);Report->SetBoolField(TEXT("manual_spectator_observed_round_change"),bManualSpectatorRoundChanged);Report->SetStringField(TEXT("manual_spectator_stop_reason"),ManualSpectatorStopReason);Report->SetNumberField(TEXT("manual_spectator_actual_wall_seconds"),ManualSpectatorStartedWall>0?FPlatformTime::Seconds()-ManualSpectatorStartedWall:0.);Report->SetBoolField(TEXT("automatic_spectator_handler_sequence_executed"),!bManualSpectator&&Stage==99&&Results.ContainsByPredicate([](const TPair<FString,bool>& Result){return Result.Key==TEXT("left_right_spectator_handlers_cycle");}));
    Report->SetStringField(TEXT("scope"),TEXT("Isolated fixture: explicit source-role qualification, relocated opponents and normal public action handlers. Not a natural round, physical input or verified original numeric balance; never changes the gameplay readiness gate."));
    Report->SetBoolField(TEXT("physical_device_input_verified"),false);Report->SetBoolField(TEXT("natural_round_verified"),false);
    Report->SetBoolField(TEXT("all_human_gameplay_actions_verified"),false);
    Report->SetBoolField(TEXT("NPC_skill_loop_end_transitions_verified"),false);
    Report->SetStringField(TEXT("NPC_skill_animation_scope"),TEXT("Primary native TP start clips/four-slash chain and shared real effects are verified; the isolated freeze window does not establish all optional TP skill loop/end transitions."));
    Report->SetStringField(TEXT("human_action_scope"),TEXT("All human form mappings and standing surfaces are measured; complete movement/fire/reload/death behavior for every human form still requires independent natural or dedicated action fixtures."));
    Report->SetBoolField(TEXT("gameplay_gate_changed"),false);Report->SetBoolField(TEXT("gameplay_gate_at_load"),Definition&&Definition->bGameplayVerified);
    Report->SetNumberField(TEXT("failures"),Failures);Report->SetStringField(TEXT("source_identity"),Definition?Definition->SourceIdentity:TEXT(""));
    Report->SetNumberField(TEXT("standing_sole_tolerance_cm"),3);Report->SetArrayField(TEXT("observations"),Observations);
    if(Mode){Report->SetNumberField(TEXT("configured_capsule_radius_cm"),Mode->Model.Config.MovementCapsuleRadius);Report->SetNumberField(TEXT("configured_capsule_half_height_cm"),Mode->Model.Config.MovementCapsuleHalfHeight);Report->SetNumberField(TEXT("configured_eye_height_cm"),Mode->Model.Config.EyeHeight);}
    TArray<TSharedPtr<FJsonValue>> Checks;for(const auto& Result:Results){auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("name"),Result.Key);Row->SetBoolField(TEXT("passed"),Result.Value);Checks.Add(MakeShared<FJsonValueObject>(Row));}Report->SetArrayField(TEXT("checks"),Checks);
    FString Text;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Out/TEXT("role_report.json")));
    FPlatformMisc::RequestExit(false);
}
USkeletalMeshComponent* AHeliosBioRoleQA::FirstPersonMesh() const
{
    if(!Mode||!Mode->Player||!Definition)return nullptr;
    TArray<USkeletalMeshComponent*> Meshes;Mode->Player->GetComponents(Meshes);
    for(auto Mesh:Meshes)
    {
        auto* Asset=Mesh->GetSkeletalMeshAsset();
        if(Asset&&Mesh->IsVisible()&&!Mesh->bHiddenInGame&&(Asset==Definition->FirstPersonMesh.LoadSynchronous()||Asset==Definition->SkillFirstPersonMesh.LoadSynchronous()||Asset==Definition->PrimaryFirstPersonMesh.LoadSynchronous()||Asset==Definition->SecondaryFirstPersonMesh.LoadSynchronous()))return Mesh;
    }
    return nullptr;
}
void AHeliosBioRoleQA::SamplePose(const FString& Key,USkeletalMeshComponent* Mesh)
{
    if(!Mesh||!Mesh->GetSkeletalMeshAsset())return;
    auto* Node=Mesh->GetSingleNodeInstance();if(!Node||!Node->GetCurrentAsset())return;
    auto& Probe=PoseProbes.FindOrAdd(Key);const FString Clip=Node->GetCurrentAsset()->GetPathName();const float Seconds=Node->GetCurrentTime();
    const auto Matches=[&](const TSoftObjectPtr<UAnimSequence>& Ref){return !Ref.IsNull()&&Node->GetCurrentAsset()==Ref.LoadSynchronous();};
    bool Native=true;
    if(Key==TEXT("idle_FP"))Native=Matches(Definition->FPIdle);
    else if(Key==TEXT("idle_TP"))Native=Matches(Definition->TPIdle);
    else if(Key==TEXT("move_FP"))Native=Definition->FPMove.IsNull()?Matches(Definition->FPIdle):Matches(Definition->FPMove);
    else if(Key==TEXT("move_TP"))Native=Matches(Definition->TPRun)||Matches(Definition->TPRunBackward)||Matches(Definition->TPRunLeft)||Matches(Definition->TPRunRight);
    else if(Key==TEXT("light_FP"))Native=Matches(Definition->FPAttack);
    else if(Key==TEXT("wukong_boosted_light_FP")||Key==TEXT("wukong_expired_light_FP"))Native=Matches(Definition->FPAttack);
    else if(Key==TEXT("heavy_FP"))Native=Matches(Definition->FPHeavy);
    else if(Key==TEXT("light_TP"))Native=Matches(Definition->TPAttack);
    else if(Key==TEXT("heavy_TP"))Native=Matches(Definition->TPHeavy);
    else if(Key==TEXT("death_TP"))Native=Matches(Definition->TPDeath);
    else if(Key==TEXT("secondary_FP"))Native=Matches(Definition->FPSecondarySkill);
    else if(Key==TEXT("secondary_TP"))Native=Matches(Definition->TPSecondarySkill);
    else if(Key==TEXT("serum_FP"))Native=Matches(Definition->FPSecondarySkill);
    else if(Key==TEXT("serum_TP"))Native=Matches(Definition->TPSecondarySkill);
    else if(Key==TEXT("primary_FP"))Native=Matches(Definition->FPPrimaryFire);
    else if(Key==TEXT("primary_TP"))Native=Matches(Definition->TPPrimaryFire);
    else if(Key==TEXT("deploy_FP"))Native=Matches(Definition->FPSkill);
    else if(Key==TEXT("skill_FP")||Key==TEXT("skill_TP"))
    {
        const bool FP=Key.EndsWith(TEXT("_FP"));Native=FP?(Matches(Definition->FPSkill)||Matches(Definition->FPSkillLoop)||Matches(Definition->FPSkillEnd)):(Matches(Definition->TPSkill)||Matches(Definition->TPSkillLoop)||Matches(Definition->TPSkillEnd));
        for(const auto& Ref:FP?Definition->FPSkillChain:Definition->TPSkillChain)Native|=Matches(Ref);
    }
    if(!Native)
    {
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("animation_excluded"));Row->SetStringField(TEXT("probe"),Key);Row->SetStringField(TEXT("clip"),Clip);
        Row->SetNumberField(TEXT("world_seconds"),GetWorld()->GetTimeSeconds());Row->SetStringField(TEXT("reason"),TEXT("Not the declared native action clip; cannot count idle/recovery toward movement/skill acceptance"));Observations.Add(MakeShared<FJsonValueObject>(Row));return;
    }
    const auto& Pose=Mesh->GetBoneSpaceTransforms();bool Changed=false;
    if(Probe.Clip==Clip&&Pose.Num()==Probe.Pose.Num())for(int32 I=0;I<Pose.Num();++I)
        Changed|=FVector::DistSquared(Pose[I].GetLocation(),Probe.Pose[I].GetLocation())>.0001f||Pose[I].GetRotation().AngularDistance(Probe.Pose[I].GetRotation())>.0001f;
    const bool Clock=Probe.Clip==Clip&&FMath::Abs(Seconds-Probe.Seconds)>.0001f;
    ++Probe.Samples;if(Clock)++Probe.ClockAdvances;if(Changed)++Probe.PoseAdvances;
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("animation"));Row->SetStringField(TEXT("probe"),Key);Row->SetStringField(TEXT("mesh"),Mesh->GetSkeletalMeshAsset()->GetPathName());
    Row->SetStringField(TEXT("clip"),Clip);Row->SetNumberField(TEXT("clip_seconds"),Seconds);Row->SetNumberField(TEXT("world_seconds"),GetWorld()->GetTimeSeconds());
    Row->SetBoolField(TEXT("clip_clock_advanced"),Clock);Row->SetBoolField(TEXT("local_bone_pose_advanced"),Changed);Observations.Add(MakeShared<FJsonValueObject>(Row));
    Probe.Clip=Clip;Probe.Seconds=Seconds;Probe.Pose=Pose;
}
void AHeliosBioRoleQA::CheckPose(const FString& Key,bool RequirePose)
{
    const auto* Probe=PoseProbes.Find(Key);
    Check(*(Key+TEXT("_clip_clock_advances")),Probe&&Probe->Samples>=2&&Probe->ClockAdvances>0);
    if(RequirePose)Check(*(Key+TEXT("_actual_bone_pose_changes")),Probe&&Probe->PoseAdvances>0);
}
bool AHeliosBioRoleQA::CheckGround(const FString& Key,ACharacter* Character,USkeletalMeshComponent* Mesh)
{
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("standing_ground"));Row->SetStringField(TEXT("probe"),Key);
    if(!Character||!Mesh||!Mesh->GetSkeletalMeshAsset()){Row->SetStringField(TEXT("blocker"),TEXT("Actual TP mesh missing"));Observations.Add(MakeShared<FJsonValueObject>(Row));return false;}
    const auto* Render=Mesh->GetSkeletalMeshRenderData();auto* Weights=Mesh->GetSkinWeightBuffer(0);
    if(!Render||!Render->LODRenderData.IsValidIndex(0)||!Weights||!Weights->GetDataVertexBuffer()->GetWeightData())
    {Row->SetStringField(TEXT("blocker"),TEXT("CPU-accessible source skin weights unavailable; bone centers cannot prove visual sole alignment"));Observations.Add(MakeShared<FJsonValueObject>(Row));return false;}
    TArray<FFinalSkinVertex> Vertices;Mesh->GetCPUSkinnedVertices(Vertices,0);const auto& LOD=Render->LODRenderData[0];
    FVector Sole=FVector::ZeroVector;float Lowest=MAX_flt,BoneMin=MAX_flt;int32 SoleVertices=0,FootBones=0;
    for(int32 I=0;I<Mesh->GetNumBones();++I)if(IsFootBone(Mesh->GetBoneName(I).ToString())){++FootBones;BoneMin=FMath::Min(BoneMin,float(Mesh->GetBoneTransform(I).GetLocation().Z));}
    for(const auto& Section:LOD.RenderSections)for(uint32 I=Section.BaseVertexIndex;I<Section.BaseVertexIndex+Section.NumVertices&&I<uint32(Vertices.Num());++I)
    {
        bool Foot=false;for(uint32 W=0;W<Weights->GetMaxBoneInfluences();++W)
        {const uint32 Local=Weights->GetBoneIndex(I,W);if(Weights->GetBoneWeight(I,W)>0&&Section.BoneMap.IsValidIndex(Local))Foot|=IsFootBone(Mesh->GetBoneName(Section.BoneMap[Local]).ToString());}
        if(!Foot)continue;++SoleVertices;const FVector World=Mesh->GetComponentTransform().TransformPosition(FVector(Vertices[I].Position));if(World.Z<Lowest){Lowest=World.Z;Sole=World;}
    }
    FHitResult Floor;FCollisionQueryParams Query(SCENE_QUERY_STAT(BioRoleRenderedSole),false,Character);for(auto Bot:Mode->Bots)Query.AddIgnoredActor(Bot);Query.AddIgnoredActor(Mode->Player);
    TArray<AActor*> Attached;Character->GetAttachedActors(Attached,true,true);for(auto Actor:Attached)Query.AddIgnoredActor(Actor);
    const float CapsuleFeet=Character->GetActorLocation().Z-Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    Row->SetNumberField(TEXT("actual_capsule_half_height_cm"),Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());Row->SetNumberField(TEXT("actual_capsule_radius_cm"),Character->GetCapsuleComponent()->GetScaledCapsuleRadius());
    const bool Hit=SoleVertices>0&&GetWorld()->LineTraceSingleByChannel(Floor,FVector(Sole.X,Sole.Y,CapsuleFeet+30),FVector(Sole.X,Sole.Y,CapsuleFeet-150),ECC_Visibility,Query);
    Row->SetNumberField(TEXT("foot_bones"),FootBones);Row->SetNumberField(TEXT("foot_weighted_skinned_vertices"),SoleVertices);Row->SetBoolField(TEXT("floor_hit"),Hit);Row->SetBoolField(TEXT("on_ground"),Character->GetCharacterMovement()->IsMovingOnGround());
    if(Hit){const float Gap=Lowest-Floor.ImpactPoint.Z;Row->SetStringField(TEXT("sole_world"),Sole.ToString());Row->SetNumberField(TEXT("rendered_sole_ground_gap_cm"),Gap);Row->SetNumberField(TEXT("foot_bone_center_ground_gap_cm"),BoneMin-Floor.ImpactPoint.Z);Row->SetNumberField(TEXT("capsule_ground_gap_cm"),CapsuleFeet-Floor.ImpactPoint.Z);
        Row->SetNumberField(TEXT("sole_vs_capsule_bottom_cm"),Lowest-CapsuleFeet);Row->SetStringField(TEXT("actor_location"),Character->GetActorLocation().ToString());Row->SetStringField(TEXT("sole_support_normal"),Floor.ImpactNormal.ToString());Row->SetStringField(TEXT("sole_support_component"),GetPathNameSafe(Floor.Component.Get()));
        FHitResult CenterFloor;const FVector Center=Character->GetActorLocation();if(GetWorld()->LineTraceSingleByChannel(CenterFloor,FVector(Center.X,Center.Y,CapsuleFeet+30),FVector(Center.X,Center.Y,CapsuleFeet-150),ECC_Visibility,Query))
        {Row->SetNumberField(TEXT("capsule_center_ground_gap_cm"),CapsuleFeet-CenterFloor.ImpactPoint.Z);Row->SetNumberField(TEXT("sole_support_vs_capsule_support_cm"),Floor.ImpactPoint.Z-CenterFloor.ImpactPoint.Z);Row->SetStringField(TEXT("capsule_support_normal"),CenterFloor.ImpactNormal.ToString());}
        Check(*(Key+TEXT("_actual_sole_within_3cm")),Character->GetCharacterMovement()->IsMovingOnGround()&&FMath::Abs(Gap)<=3.f);}
    Observations.Add(MakeShared<FJsonValueObject>(Row));return Hit;
}
bool AHeliosBioRoleQA::ValidateSourceBindings(FString& Blocker)
{
    auto* FP=Definition?Definition->FirstPersonMesh.LoadSynchronous():nullptr;auto* TP=Definition?Definition->ThirdPersonMesh.LoadSynchronous():nullptr;
    if(!Definition||!Definition->bIdentityVerified||!FP||!TP){Blocker=TEXT("Verified complete source FP/TP identity unavailable");return false;}
    const auto Validate=[&](const TCHAR* Name,const TSoftObjectPtr<UAnimSequence>& Ref,USkeletalMesh* Mesh)
    {auto* Clip=Ref.LoadSynchronous();const bool Valid=Clip&&Clip->GetSkeleton()==Mesh->GetSkeleton()&&FMath::IsFinite(Clip->GetPlayLength())&&Clip->GetPlayLength()>0;
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("binding"));Row->SetStringField(TEXT("semantic"),Name);Row->SetStringField(TEXT("mesh"),Mesh->GetPathName());Row->SetStringField(TEXT("clip"),GetPathNameSafe(Clip));Row->SetBoolField(TEXT("matching_skeleton_and_duration"),Valid);Observations.Add(MakeShared<FJsonValueObject>(Row));
        if(!Valid)Blocker+=FString(Name)+TEXT(" missing or skeleton/duration mismatch; ");return Valid;};
    bool Valid=Validate(TEXT("FPIdle"),Definition->FPIdle,FP)&Validate(TEXT("FPAttack"),Definition->FPAttack,FP)&Validate(TEXT("FPHeavy"),Definition->FPHeavy,FP);
    if(!Definition->FPMove.IsNull())Valid&=Validate(TEXT("FPMove"),Definition->FPMove,FP);
    else {auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("binding"));Row->SetStringField(TEXT("semantic"),TEXT("FPMove"));Row->SetStringField(TEXT("scope"),TEXT("No independent source FP move clip is declared; production uses the original FPIdle while walking. TP locomotion still requires its own run pose."));Observations.Add(MakeShared<FJsonValueObject>(Row));}
    Valid&=Validate(TEXT("TPIdle"),Definition->TPIdle,TP)&Validate(TEXT("TPRun"),Definition->TPRun,TP)&Validate(TEXT("TPAttack"),Definition->TPAttack,TP)&Validate(TEXT("TPHeavy"),Definition->TPHeavy,TP)&Validate(TEXT("TPDeath"),Definition->TPDeath,TP);
    const bool Slash=Definition->Ability==EHeliosBioAbility::FourSlash||Definition->SourceIdentity.Contains(TEXT("SunShangXiang"),ESearchCase::IgnoreCase);
    if(Slash){if(Definition->FPSkillChain.Num()!=4||Definition->TPSkillChain.Num()!=4){Valid=false;Blocker+=TEXT("Four original FP/TP skill stages unavailable; ");}
        else for(int32 I=0;I<4;++I){Valid&=Validate(*FString::Printf(TEXT("FPSkill%d"),I+1),Definition->FPSkillChain[I],FP);Valid&=Validate(*FString::Printf(TEXT("TPSkill%d"),I+1),Definition->TPSkillChain[I],TP);}}
    else {auto* SkillFP=Definition->SkillFirstPersonMesh.LoadSynchronous();Valid&=Validate(TEXT("FPSkill"),Definition->FPSkill,SkillFP?SkillFP:FP)&Validate(TEXT("TPSkill"),Definition->TPSkill,TP);}
    if(Definition->Ability==EHeliosBioAbility::ZhongXiaoKuiBlade||Definition->Ability==EHeliosBioAbility::MechanicalFire)
    {
        Valid&=Validate(TEXT("FPSecondarySkill"),Definition->FPSecondarySkill,FP)&Validate(TEXT("TPSecondarySkill"),Definition->TPSecondarySkill,TP);
        auto* Library=LoadObject<UHeliosSSXEffectLibrary>(nullptr,*Definition->SkillEffectLibrary);const auto Recipe=Library?Library->GetParsedRecipe():nullptr;
        const auto Effect=[&](const TCHAR* Semantic,const FString& Name){const bool Found=Recipe&&Recipe->HasTypedField<EJson::Object>(Name);if(!Found)Blocker+=FString(Semantic)+TEXT(" original effect recipe unavailable; ");return Found;};
        if(Definition->Ability==EHeliosBioAbility::ZhongXiaoKuiBlade)Valid&=Effect(TEXT("SwordWave"),Definition->SwordWaveEffect)&Effect(TEXT("GhostMask"),Definition->SecondaryEffect);
        else
        {
            auto* GunFP=Definition->PrimaryFirstPersonMesh.LoadSynchronous();auto* GunTP=Definition->PrimaryThirdPersonWeaponMesh.LoadSynchronous();
            if(!GunFP||!GunTP){Valid=false;Blocker+=TEXT("XM214 original FP/TP weapon meshes unavailable; ");}
            if(GunFP)Valid&=Validate(TEXT("FPPrimaryIdle"),Definition->FPPrimaryIdle,GunFP)&Validate(TEXT("FPPrimaryFire"),Definition->FPPrimaryFire,GunFP);
            if(GunTP)Valid&=Validate(TEXT("TPPrimaryIdle"),Definition->TPPrimaryIdle,GunTP)&Validate(TEXT("TPPrimaryFire"),Definition->TPPrimaryFire,GunTP);
            if(!Definition->SkillAuxiliaryMesh.LoadSynchronous()){Valid=false;Blocker+=TEXT("M1495/PaoTai original deployable mesh unavailable; ");}
            if(!Definition->SecondaryProjectileMesh.LoadSynchronous()&&!Effect(TEXT("ArcaneProjectile"),Definition->SecondaryProjectileEffect))Valid=false;
        }
    }
    return Valid;
}
bool AHeliosBioRoleQA::PlaceActor(ACharacter* Character,const FVector& Wanted) const
{
    if(!Character)return false;FHitResult Floor;FCollisionObjectQueryParams Static(ECC_WorldStatic);FCollisionQueryParams Query(SCENE_QUERY_STAT(BioRoleFixtureFloor),false,Character);
    if(!GetWorld()->LineTraceSingleByObjectType(Floor,Wanted+FVector(0,0,200),Wanted-FVector(0,0,600),Static,Query)||Floor.ImpactNormal.Z<.8f)return false;
    const FVector At(Wanted.X,Wanted.Y,Floor.ImpactPoint.Z+Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+FixtureGroundClearance);
    Character->GetCharacterMovement()->StopMovementImmediately();Character->SetActorLocation(At,false,nullptr,ETeleportType::TeleportPhysics);Character->SetActorRotation(FixtureForward.Rotation());
    if(Character->GetController())Character->GetController()->SetControlRotation(FixtureForward.Rotation());return true;
}
bool AHeliosBioRoleQA::FindFixtureLane()
{
    const auto& Config=Mode->Model.Config;
    const float SpawnFeetOffset=Config.MovementCapsuleHalfHeight+FixtureGroundClearance;
    for(int32 Id=0;Id<Mode->Model.GetParticipants().Num();++Id)for(const FVector Direction:{FVector::ForwardVector,-FVector::ForwardVector,FVector::RightVector,-FVector::RightVector})
    {
        const FVector Start=Mode->SpawnLocation(Id),Side=FVector(-Direction.Y,Direction.X,0);bool Clear=true;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(BioRoleFixtureLane),false);for(auto Bot:Mode->Bots)Query.AddIgnoredActor(Bot);Query.AddIgnoredActor(Mode->Player);
        TArray<AActor*> Attached;Mode->Player->GetAttachedActors(Attached,true,true);for(auto Actor:Attached)Query.AddIgnoredActor(Actor);
        FCollisionObjectQueryParams Static(ECC_WorldStatic);
        for(int32 Step=0;Step<=8&&Clear;++Step)for(float Offset:{0.f,140.f})
        {FHitResult Floor;const FVector At=Start+Direction*(Step*100.f)+Side*Offset;Clear&=GetWorld()->LineTraceSingleByObjectType(Floor,At+FVector(0,0,150),At-FVector(0,0,250),Static,Query)&&Floor.ImpactNormal.Z>.95f&&FMath::Abs(Floor.ImpactPoint.Z-(Start.Z-SpawnFeetOffset))<10.f;}
        FHitResult Wall;if(Clear)Clear=!GetWorld()->SweepSingleByChannel(Wall,Start,Start+Direction*800,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Config.MovementCapsuleRadius,Config.MovementCapsuleHalfHeight),Query);
        if(Clear){FixtureOrigin=Start;FixtureForward=Direction;
            auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("fixture_lane"));Row->SetStringField(TEXT("spawn_center"),Start.ToString());Row->SetStringField(TEXT("direction"),Direction.ToString());Row->SetNumberField(TEXT("spawn_feet_offset_cm"),SpawnFeetOffset);Row->SetNumberField(TEXT("sweep_capsule_radius_cm"),Config.MovementCapsuleRadius);Row->SetNumberField(TEXT("sweep_capsule_half_height_cm"),Config.MovementCapsuleHalfHeight);Observations.Add(MakeShared<FJsonValueObject>(Row));return true;}
    }
    return false;
}
void AHeliosBioRoleQA::PlaceCombatTarget(float Distance)
{
    const FVector Side(-FixtureForward.Y,FixtureForward.X,0);
    PlaceActor(Mode->Player,FixtureOrigin);PlaceActor(Cast<ACharacter>(Mode->ParticipantActor(VictimId)),FixtureOrigin+FixtureForward*Distance);
    PlaceActor(Cast<ACharacter>(Mode->ParticipantActor(HunterId)),FixtureOrigin+Side*140.f);
    // A retained 160cm viewpoint can be above the calibrated capsule. Aim at the actual target
    // instead of using a horizontal ray whose height depended on the previous 96cm half-height.
    if(Mode->Player->FirstPersonCamera&&Mode->Player->GetController())if(auto* Target=Cast<ACharacter>(Mode->ParticipantActor(VictimId)))
    {
        const FVector Eyes=Mode->Player->FirstPersonCamera->GetComponentLocation(),At=Target->GetCapsuleComponent()->GetComponentLocation();
        Mode->Player->GetController()->SetControlRotation((At-Eyes).Rotation());
        // Projectile public actions read the camera component's eye rotation. Evaluate the normal
        // camera handler after controller aim so an action in this same fixture tick sees that aim.
        FMinimalViewInfo Camera;Mode->Player->CalcCamera(0.f,Camera);
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("fixture_player_aim"));Row->SetStringField(TEXT("actual_camera_position"),Mode->Player->FirstPersonCamera->GetComponentLocation().ToString());Row->SetStringField(TEXT("actual_camera_forward"),Mode->Player->FirstPersonCamera->GetForwardVector().ToString());Row->SetStringField(TEXT("actual_target_capsule_center"),At.ToString());Row->SetNumberField(TEXT("target_capsule_half_height_cm"),Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());Row->SetStringField(TEXT("scope"),TEXT("Public controller and camera handlers aim at the live physical target; no hit/damage substitution or reach change."));Observations.Add(MakeShared<FJsonValueObject>(Row));
    }
}
bool AHeliosBioRoleQA::PickupFixtureSupply(int32 Id,int32 Kind)
{
    auto* Participant=Cast<ACharacter>(Mode->ParticipantActor(Id));if(!Participant)return false;
    const FVector Feet=Participant->GetActorLocation()-FVector(0,0,Participant->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    const FTransform At(FRotator::ZeroRotator,Feet+FVector(0,0,10));
    auto* Supply=GetWorld()->SpawnActorDeferred<AHeliosBioSupply>(AHeliosBioSupply::StaticClass(),At,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Supply)return false;Supply->SetKind(static_cast<EHeliosBioSupplyKind>(Kind));UGameplayStatics::FinishSpawningActor(Supply,At);Supply->SetActorTickEnabled(false);
    const bool Reachable=Supply->CanReachPickup(Participant);
    const bool Applied=Supply->bHasOriginalMesh&&Reachable&&Mode->PickupSupply(Id,Supply);const bool Consumed=!Supply->IsAvailable();
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("real_fixture_supply_qualification"));Row->SetNumberField(TEXT("participant"),Id);Row->SetNumberField(TEXT("supply_kind"),Kind);Row->SetStringField(TEXT("actual_box"),At.GetLocation().ToString());Row->SetStringField(TEXT("actual_participant_feet"),Feet.ToString());Row->SetBoolField(TEXT("production_same_floor_and_LOS_reachable"),Reachable);Row->SetBoolField(TEXT("actual_committed_and_consumed"),Applied&&Consumed);Observations.Add(MakeShared<FJsonValueObject>(Row));
    Supply->Destroy();return Applied&&Consumed;
}
void AHeliosBioRoleQA::SampleHumanBaseline(const FString& Label)
{
    TSet<FString> Forms;int32 Measured=0;
    for(auto Bot:Mode->Bots)if(Bot)
    {
        const auto* P=Mode->Model.FindParticipant(Bot->ParticipantId);if(!P||P->Team!=EHeliosBioTeam::Human)continue;
        const auto& Loadout=Bot->GetHumanLoadout();const FString Form=Loadout.CharacterId.ToString()+TEXT("/")+Loadout.CharacterVariantId.ToString();Forms.Add(Form);
        auto* Presentation=Bot->FindComponentByClass<UHeliosCharacterPresentationComponent>();USkeletalMeshComponent* Body=Presentation&&Presentation->GetArmsMesh()?Presentation->GetArmsMesh():Bot->GetMesh();
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("human_loadout"));Row->SetStringField(TEXT("probe"),Label);Row->SetNumberField(TEXT("participant"),Bot->ParticipantId);Row->SetStringField(TEXT("form"),Form);
        Row->SetStringField(TEXT("weapon"),Loadout.PrimaryWeaponId.ToString());Row->SetStringField(TEXT("visible_mesh"),Body?GetPathNameSafe(Body->GetSkeletalMeshAsset()):TEXT(""));Row->SetBoolField(TEXT("presentation_ready"),Presentation&&Presentation->IsReady());Observations.Add(MakeShared<FJsonValueObject>(Row));
        Check(*FString::Printf(TEXT("%s_human%d_real_presentation"),*Label,Bot->ParticipantId),Presentation&&Presentation->IsReady()&&Body&&Body->IsVisible()&&!Body->bHiddenInGame);
        if(CheckGround(FString::Printf(TEXT("%s_human%d"),*Label,Bot->ParticipantId),Bot,Body))++Measured;
    }
    Check(*(Label+TEXT("_29_human_surfaces_measured")),Measured==29);Check(*(Label+TEXT("_17_distinct_human_forms")),Forms.Num()>=17);
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("human_baseline_summary"));Row->SetStringField(TEXT("probe"),Label);Row->SetNumberField(TEXT("distinct_forms"),Forms.Num());Row->SetNumberField(TEXT("actual_surface_measurements"),Measured);
    Row->SetStringField(TEXT("scope"),TEXT("Standing native source pose fixture and independent loadout mapping; not proof that all 17 forms complete every gameplay action."));Observations.Add(MakeShared<FJsonValueObject>(Row));
}
bool AHeliosBioRoleQA::SelectLivingVictim()
{
    for(const auto& P:Mode->Model.GetParticipants())if(P.bAlive&&P.Team==EHeliosBioTeam::Zombie&&P.Id!=VictimId)
    {
        auto* Bot=Cast<AHeliosBioNPC>(Mode->ParticipantActor(P.Id));if(!Bot)continue;VictimId=P.Id;
        Bot->SetActorTickEnabled(true);Bot->bQAFreeze=true;Bot->SetActorHiddenInGame(false);Bot->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Bot->GetCharacterMovement()->SetMovementMode(MOVE_Walking);return true;
    }
    return false;
}
bool AHeliosBioRoleQA::PrepareVictimHealth(float Desired)
{
    const auto* P=Mode->Model.FindParticipant(0);const auto* V=Mode->Model.FindParticipant(VictimId);if(!P||!V||!V->bAlive||V->Health<=Desired)return false;
    const float Mult=(P->SkillRemaining>0?P->SkillDamageMultiplier:1.f)*(P->bGhostMaskActive&&P->SecondarySkillRemaining>0?P->SecondaryDamageMultiplier:1.f);
    if(Mult<=0)return false;const float Damage=(V->Health-Desired)/Mult;
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("fixture_health_precondition"));Row->SetNumberField(TEXT("participant"),VictimId);Row->SetNumberField(TEXT("desired_hp"),Desired);
    Row->SetStringField(TEXT("scope"),TEXT("Nonlethal real damage prepares the target; it does not count as the tested light/heavy/projectile kill."));Observations.Add(MakeShared<FJsonValueObject>(Row));
    return Mode->DamageParticipant(VictimId,0,Damage,false)&&Mode->Model.FindParticipant(VictimId)->bAlive;
}
void AHeliosBioRoleQA::WatchProjectile()
{
    Projectile.Reset();bHadProjectile=bProjectileFX=false;ProjectileTravel=0;
    for(TActorIterator<AHeliosBioSkillProjectile> It(GetWorld());It;++It)if(It->GetOwner()==Mode->Player){Projectile=*It;LastProjectilePosition=ProjectileStart=It->GetActorLocation();bHadProjectile=true;break;}
    SampleProjectile();
}
void AHeliosBioRoleQA::SampleProjectile()
{
    // A short-range grenade can collide and be destroyed in one rendered
    // frame. Read its still-live UObject's actual final collision center,
    // as the turret expiry fixture does; do not infer travel from speed/time.
    auto* Current=Projectile.Get(true);if(!Current)return;LastProjectilePosition=Current->GetActorLocation();ProjectileTravel=FMath::Max(ProjectileTravel,float(FVector::Dist(ProjectileStart,LastProjectilePosition)));
    TArray<AActor*> Attached;Current->GetAttachedActors(Attached);for(auto Actor:Attached)if(auto* FX=Cast<AHeliosSSXEffect>(Actor))bProjectileFX|=FX->VisibleTrackCount()>0;
    if(auto* Visual=Current->FindComponentByClass<UStaticMeshComponent>())bProjectileFX|=Visual->IsVisible()&&!Visual->bHiddenInGame&&Visual->GetStaticMesh()&&Visual->GetStaticMesh()==Definition->SecondaryProjectileMesh.LoadSynchronous();
    auto* Collision=Cast<UPrimitiveComponent>(Current->GetRootComponent());
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("real_projectile"));Row->SetNumberField(TEXT("stage"),Stage);Row->SetStringField(TEXT("position"),Current->GetActorLocation().ToString());Row->SetNumberField(TEXT("travel_cm"),ProjectileTravel);
    Row->SetBoolField(TEXT("real_blocking_collision"),Collision&&Collision->GetCollisionEnabled()!=ECollisionEnabled::NoCollision&&Collision->GetCollisionResponseToChannel(ECC_Visibility)==ECR_Block);Row->SetBoolField(TEXT("native_effect_tracks_visible"),bProjectileFX);Observations.Add(MakeShared<FJsonValueObject>(Row));
    Row->SetBoolField(TEXT("resolved_actor_final_collision_center_sample"),!Projectile.IsValid());
}
void AHeliosBioRoleQA::MakeObstacle(float Distance)
{
    RemoveObstacle();Obstacle=GetWorld()->SpawnActor<AActor>();if(!Obstacle)return;
    auto* Box=NewObject<UBoxComponent>(Obstacle);Obstacle->SetRootComponent(Box);Box->InitBoxExtent(FVector(12,260,300));Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Box->SetCollisionObjectType(ECC_WorldStatic);Box->SetCollisionResponseToAllChannels(ECR_Block);Box->SetCanEverAffectNavigation(false);Box->RegisterComponent();
    Obstacle->SetActorLocation(FixtureOrigin+FixtureForward*Distance);Obstacle->SetActorRotation(FixtureForward.Rotation());
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("physical_obstacle_fixture"));Row->SetStringField(TEXT("position"),Obstacle->GetActorLocation().ToString());Row->SetStringField(TEXT("scope"),TEXT("Explicit runtime collision slab, 24x520x600cm; tests engine blocking and LOS, not original map geometry fidelity."));Observations.Add(MakeShared<FJsonValueObject>(Row));
}
void AHeliosBioRoleQA::RemoveObstacle(){if(IsValid(Obstacle))Obstacle->Destroy();Obstacle=nullptr;}
void AHeliosBioRoleQA::FinishCombatFixture(double Now)
{
    RemoveObstacle();const auto* V=Mode->Model.FindParticipant(VictimId);if(!V||!V->bAlive)if(!SelectLivingVictim()){Finish(TEXT("No real live enemy remains for death/spectator acceptance"));return;}
    if(auto* Bot=Cast<AHeliosBioNPC>(Mode->ParticipantActor(VictimId)))Bot->bQAFreeze=true;
    PlaceCombatTarget();Check(TEXT("NPC_real_permanent_hunter_death"),Mode->DamageParticipant(HunterId,VictimId,1000000,false));Next=Now+.05;Stage=12;
}
void AHeliosBioRoleQA::BeginNPCValidation(double Now,int32 ContinueStage)
{
    AfterNPCValidation=ContinueStage;Stage=160;Next=Now+Mode->Model.FindParticipant(HunterId)->SkillCooldownRemaining+.2;
}
void AHeliosBioRoleQA::TickNPCValidation(double Now)
{
    auto* Hunter=Cast<AHeliosBioNPC>(Mode->ParticipantActor(HunterId));auto* Target=Cast<AHeliosBioNPC>(Mode->ParticipantActor(VictimId));
    const auto* V=Mode->Model.FindParticipant(VictimId);const auto* P=Mode->Model.FindParticipant(HunterId);
    if(!Hunter||!Target||!V||!P||!P->bAlive||!V->bAlive){Finish(TEXT("Live source NPC/target unavailable for primary and melee validation"));return;}
    if(Stage==161)
    {
        LowestVictimHealth=FMath::Min(LowestVictimHealth,V->Health);MaxFrozenSeconds=FMath::Max(MaxFrozenSeconds,V->FrozenSeconds);
        bVictimAirborne|=Target->GetCharacterMovement()->IsFalling();if(Target->GetCharacterMovement()->IsFalling()&&FVector::Dist(Target->GetActorLocation(),LastVictimPosition)>.1f)++AirMotionSamples;LastVictimPosition=Target->GetActorLocation();
        if(V->FrozenSeconds<=0&&MaxFrozenSeconds>0)Target->bQAFreeze=true;
        if(TestRole==TEXT("BioSunWukong")&&P->SkillRemaining>0&&!bWukongNPCSpeedApplied)
        {
            const float Actual=Hunter->GetCharacterMovement()->MaxWalkSpeed;
            bWukongNPCSpeedApplied=WukongNPCBaseSpeed>0&&FMath::IsNearlyEqual(Actual,WukongNPCBaseSpeed*Definition->SkillSpeedMultiplier,1.f);
            if(bWukongNPCSpeedApplied){Hunter->bQAFreeze=true;if(Hunter->GetCharacterMovement()->IsMovingOnGround())Hunter->GetCharacterMovement()->StopMovementImmediately();}
            auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("Wukong_NPC_buff_actual_movement_component"));Row->SetNumberField(TEXT("base_max_walk_speed"),WukongNPCBaseSpeed);Row->SetNumberField(TEXT("actual_max_walk_speed"),Actual);Row->SetNumberField(TEXT("DA_speed_multiplier"),Definition->SkillSpeedMultiplier);Row->SetNumberField(TEXT("actual_remaining"),P->SkillRemaining);Row->SetStringField(TEXT("actual_velocity"),Hunter->GetVelocity().ToString());Row->SetBoolField(TEXT("actual_production_Think_applied_multiplier"),bWukongNPCSpeedApplied);Observations.Add(MakeShared<FJsonValueObject>(Row));
        }
    }
    if(Stage==163)
    {
        if(TestRole==TEXT("BioSunWukong"))bWukongNPCSpeedRestored|=P->SkillRemaining==0&&P->MovementSkillMultiplier()==1.f&&FMath::IsNearlyEqual(Hunter->GetCharacterMovement()->MaxWalkSpeed,WukongNPCBaseSpeed,1.f);
        // Explicit target placement keeps the actual production AI's melee opportunity available, including a ranged hunter's retreat.
        if(FVector::Dist(Hunter->GetActorLocation(),Target->GetActorLocation())>230.f)PlaceActor(Target,Hunter->GetActorLocation()+FixtureForward*180.f);
        auto* Node=Hunter->GetMesh()->GetSingleNodeInstance();auto* Current=Node?Node->GetCurrentAsset():nullptr;
        if(Hunter->MeleeAttacks>NPCPreviousMelee)
        {
            const float Delta=NPCPreviousVictimHealth-V->Health;const bool Light=Current==Definition->TPAttack.LoadSynchronous(),Heavy=Current==Definition->TPHeavy.LoadSynchronous();
            if(Delta>0&&Light)++NPCLightHits;if(Delta>0&&Heavy)++NPCHeavyHits;
            auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("production_AI_melee"));Row->SetNumberField(TEXT("participant"),HunterId);Row->SetNumberField(TEXT("native_attack_counter"),Hunter->MeleeAttacks);Row->SetNumberField(TEXT("actual_target_hp_delta"),Delta);Row->SetStringField(TEXT("native_clip"),GetPathNameSafe(Current));Row->SetBoolField(TEXT("light_clip"),Light);Row->SetBoolField(TEXT("heavy_clip"),Heavy);Observations.Add(MakeShared<FJsonValueObject>(Row));
        }
        NPCPreviousMelee=Hunter->MeleeAttacks;NPCPreviousVictimHealth=V->Health;
        const auto* LightProbe=PoseProbes.Find(TEXT("light_TP"));const auto* HeavyProbe=PoseProbes.Find(TEXT("heavy_TP"));
        if(NPCLightHits>0&&NPCHeavyHits>0&&LightProbe&&HeavyProbe&&LightProbe->ClockAdvances>0&&LightProbe->PoseAdvances>0&&HeavyProbe->ClockAdvances>0&&HeavyProbe->PoseAdvances>0)Next=Now;
    }
    if(Now-LastSample>=.035)
    {
        if(Stage==161)SamplePose(TEXT("skill_TP"),Hunter->GetMesh());
        if(Stage==163){SamplePose(TEXT("light_TP"),Hunter->GetMesh());SamplePose(TEXT("heavy_TP"),Hunter->GetMesh());}
        LastSample=Now;
    }
    if(Now<Next)return;
    switch(Stage)
    {
    case 160:
    {
        PlaceActor(Hunter,FixtureOrigin);PlaceActor(Target,FixtureOrigin+FixtureForward*(Definition->Ability==EHeliosBioAbility::MechanicalFire?400.f:200.f));
        PlaceActor(Mode->Player,FixtureOrigin+FVector(-FixtureForward.Y,FixtureForward.X,0)*140.f);
        CastPosition=Hunter->GetActorLocation();VictimBefore=Target->GetActorLocation();LastVictimPosition=VictimBefore;HealthBefore=V->Health;LowestVictimHealth=HealthBefore;MaxFrozenSeconds=0;bVictimAirborne=false;AirMotionSamples=0;
        PoseProbes.Remove(TEXT("skill_TP"));Check(TEXT("NPC_F_ready_after_real_world_cooldown"),P->SkillCooldownRemaining==0);
        Check(TEXT("NPC_F_uses_real_shared_action_and_native_presentation"),Hunter->UseBioSkill(false));CooldownBefore=P->SkillCooldownRemaining;Check(TEXT("NPC_F_starts_own_cooldown_and_blocks_repeat"),CooldownBefore>0&&!Hunter->UseBioSkill(false));
        if(TestRole==TEXT("BioSunWukong"))
        {
            // The original F area strike is measured before the separate explicit damage reference.
            WukongNPCAreaHitsBefore=Mode->TotalAreaSkillHits;
            Check(TEXT("NPC_Wukong_buff_parameters_match_actual_DA"),P->SkillRemaining>0&&FMath::IsNearlyEqual(P->SkillRemaining,Definition->SkillDuration,.01f)&&FMath::IsNearlyEqual(P->SkillCooldownRemaining,FMath::Max(Definition->SkillDuration,Definition->SkillCooldown),.01f)&&FMath::IsNearlyEqual(P->SkillSpeedMultiplier,Definition->SkillSpeedMultiplier,.001f)&&FMath::IsNearlyEqual(P->SkillDamageMultiplier,Definition->SkillDamageMultiplier,.001f)&&Definition->SkillSpeedMultiplier>1.f&&Definition->SkillDamageMultiplier>1.f);
            bWukongNPCSpeedApplied=bWukongNPCSpeedRestored=false;Hunter->bQAFreeze=false;
        }
        if(Definition->Ability==EHeliosBioAbility::SacredShield||Definition->Ability==EHeliosBioAbility::Shield)
        {const float HP=P->Health;const bool Applied=Mode->DamageParticipant(HunterId,VictimId,100,false);const float Delta=HP-P->Health;Check(TEXT("NPC_F_shield_reduces_real_incoming_damage"),Applied&&Delta>0&&Delta<100);}
        if(Definition->Ability==EHeliosBioAbility::ZhongXiaoKuiBlade)Target->bQAFreeze=false;
        float Duration=Definition->TPSkill.LoadSynchronous()?Definition->TPSkill.LoadSynchronous()->GetPlayLength():0;
        if(Hunter->GetFourSlash()->IsBusy()){Duration=0;for(const auto& Clip:Definition->TPSkillChain)Duration+=Clip.LoadSynchronous()->GetPlayLength()/Hunter->GetFourSlash()->PlaybackRate;}
        Next=Now+FMath::Max(2.f,Duration+1.f);Stage=161;break;
    }
    case 161:
    {
        CheckPose(TEXT("skill_TP"),true);Check(TEXT("NPC_F_cooldown_advances_on_world_ticks"),P->SkillCooldownRemaining<CooldownBefore);
        const auto Ability=Definition->Ability;
        if(Ability==EHeliosBioAbility::Blink)Check(TEXT("NPC_F_blink_actually_moves_and_lands"),FVector::Dist(CastPosition,Hunter->GetActorLocation())>30&&Hunter->GetCharacterMovement()->IsMovingOnGround());
        else if(Ability==EHeliosBioAbility::FreezeGrenade)Check(TEXT("NPC_F_actual_grenade_freezes_enemy"),MaxFrozenSeconds>0);
        else if(Ability==EHeliosBioAbility::ZhongXiaoKuiBlade)Check(TEXT("NPC_F_actual_punish_freezes_knocks_back_and_lands"),MaxFrozenSeconds>0&&FVector::Dist2D(VictimBefore,Target->GetActorLocation())>40&&bVictimAirborne&&AirMotionSamples>=2&&Target->GetCharacterMovement()->IsMovingOnGround());
        else if(Hunter->GetFourSlash()->GetCompletedStages()>0)Check(TEXT("NPC_F_four_native_slashes_complete_and_damage"),Hunter->GetFourSlash()->GetCompletedStages()==4&&Hunter->GetFourSlash()->GetHitCallbacks()==4&&LowestVictimHealth<HealthBefore);
        else if(Ability==EHeliosBioAbility::AreaStrike||Ability==EHeliosBioAbility::AbyssEnergy)Check(TEXT("NPC_F_shared_strike_or_projectile_really_damages_enemy"),LowestVictimHealth<HealthBefore);
        else if(Ability==EHeliosBioAbility::MechanicalFire)
        {bool Fired=false;for(auto Item:Mode->Turrets)if(Item&&Item->GetCasterId()==HunterId)Fired|=Item->IsActiveTurret()&&Item->GetShots()>0;Check(TEXT("NPC_F_actual_turret_deploys_and_hits_enemy"),Fired&&LowestVictimHealth<HealthBefore);}
        Target->bQAFreeze=true;Target->GetCharacterMovement()->StopMovementImmediately();
        if(TestRole==TEXT("BioSunWukong"))
        {
            Hunter->bQAFreeze=true;Hunter->GetCharacterMovement()->StopMovementImmediately();
            Check(TEXT("NPC_Wukong_original_area_strike_and_knockback"),Mode->TotalAreaSkillHits>WukongNPCAreaHitsBefore&&LowestVictimHealth<HealthBefore&&FVector::Dist2D(VictimBefore,Target->GetActorLocation())>40&&bVictimAirborne&&AirMotionSamples>=2&&Target->GetCharacterMovement()->IsMovingOnGround());
            Check(TEXT("NPC_Wukong_real_Think_speed_uses_buff"),bWukongNPCSpeedApplied);
            const float HP=V->Health;const bool Applied=Mode->DamageParticipant(VictimId,HunterId,100.f,false);WukongNPCBoostedReferenceDamage=HP-V->Health;
            Check(TEXT("NPC_Wukong_shared_real_damage_reference_uses_DA_buff"),P->SkillRemaining>0&&Applied&&FMath::IsNearlyEqual(WukongNPCBoostedReferenceDamage,100.f*Definition->SkillDamageMultiplier,.1f));
            auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("Wukong_NPC_shared_damage_reference"));Row->SetNumberField(TEXT("explicit_input_damage"),100);Row->SetNumberField(TEXT("actual_target_HP_delta"),WukongNPCBoostedReferenceDamage);Row->SetNumberField(TEXT("DA_damage_multiplier"),Definition->SkillDamageMultiplier);Row->SetStringField(TEXT("scope"),TEXT("Separate real shared GameMode damage call after original F strike acceptance; this reference does not count as a native melee or skill hit."));Observations.Add(MakeShared<FJsonValueObject>(Row));
        }
        // Remove the already-observed physical turret so it cannot obstruct the separate melee fixture.
        for(auto Item:Mode->Turrets)if(Item)Item->Destroy();Mode->Turrets.Reset();
        Next=Now+FMath::Max(.2f,P->SkillRemaining+.2f);Stage=162;break;
    }
    case 162:
        if(TestRole==TEXT("BioSunWukong"))
        {
            Check(TEXT("NPC_Wukong_buff_expires_on_world_ticks"),P->SkillRemaining==0&&P->SkillSpeedMultiplier==1.f&&P->SkillDamageMultiplier==1.f);
            const float HP=V->Health;const bool Applied=Mode->DamageParticipant(VictimId,HunterId,100.f,false);const float Delta=HP-V->Health;
            Check(TEXT("NPC_Wukong_same_real_reference_damage_restored_after_expiry"),Applied&&FMath::IsNearlyEqual(Delta,100.f,.1f)&&FMath::IsNearlyEqual(WukongNPCBoostedReferenceDamage,Delta*Definition->SkillDamageMultiplier,.1f));
        }
        PlaceActor(Hunter,FixtureOrigin);PlaceActor(Target,FixtureOrigin+FixtureForward*180.f);PlaceActor(Mode->Player,FixtureOrigin+FVector(-FixtureForward.Y,FixtureForward.X,0)*140.f);
        Mode->Model.ActivateSecondarySkill(HunterId,0.f,30.f);PoseProbes.Remove(TEXT("light_TP"));PoseProbes.Remove(TEXT("heavy_TP"));NPCPreviousMelee=Hunter->MeleeAttacks;NPCPreviousVictimHealth=V->Health;NPCLightHits=NPCHeavyHits=0;
        Hunter->bQAFreeze=false;Next=Now+FMath::Max(8.f,Definition->TPAttack.LoadSynchronous()->GetPlayLength()*2+Definition->TPHeavy.LoadSynchronous()->GetPlayLength()+2.f);Stage=163;break;
    case 163:
        Hunter->bQAFreeze=true;Hunter->GetCharacterMovement()->StopMovementImmediately();
        Check(TEXT("production_AI_light_melee_native_pose_and_real_hit"),NPCLightHits>0);Check(TEXT("production_AI_heavy_melee_occurs_and_really_hits"),NPCHeavyHits>0);CheckPose(TEXT("light_TP"),true);CheckPose(TEXT("heavy_TP"),true);
        if(TestRole==TEXT("BioSunWukong")){Check(TEXT("NPC_Wukong_actual_movement_speed_restores_after_expiry"),bWukongNPCSpeedRestored);Next=Now+P->SkillCooldownRemaining+.2;Stage=164;break;}
        Next=Now;Stage=AfterNPCValidation;
        if(AfterNPCValidation==12)FinishCombatFixture(Now);break;
    case 164:
        Check(TEXT("NPC_Wukong_F_cooldown_ready_by_real_world_ticks"),P->SkillCooldownRemaining==0);
        Check(TEXT("NPC_Wukong_real_F_active_before_actual_death"),Hunter->UseBioSkill(false)&&P->SkillRemaining>0);
        FinishCombatFixture(Now);
        Check(TEXT("NPC_Wukong_actual_death_clears_active_buff"),!P->bAlive&&P->SkillRemaining==0&&P->SkillSpeedMultiplier==1.f&&P->SkillDamageMultiplier==1.f);
        break;
    default:Finish(TEXT("Invalid NPC primary/melee fixture stage"));break;
    }
}
void AHeliosBioRoleQA::TickWukongBuff(double Now)
{
    const auto* P=Mode->Model.FindParticipant(0);const auto* V=Mode->Model.FindParticipant(VictimId);
    if(!P||!V||!P->bAlive||!V->bAlive){Finish(TEXT("Wukong real melee buff comparison lost live participants"));return;}
    if(Now-LastSample>=.035){if(Stage==181)SamplePose(TEXT("wukong_boosted_light_FP"),FirstPersonMesh());if(Stage==183)SamplePose(TEXT("wukong_expired_light_FP"),FirstPersonMesh());LastSample=Now;}
    if(Now<Next)return;
    switch(Stage)
    {
    case 180:
    {
        if(P->SkillRemaining<=0){Finish(TEXT("Wukong source F animation consumed the actual buff window before native melee comparison"));return;}
        if(Mode->Player->GetBioAttackCooldown()>0){Next=Now+.05;break;}
        PlaceCombatTarget();const float HP=V->Health;Mode->Player->StartBioFire();Mode->Player->StopBioFire();const float Delta=HP-V->Health;
        Check(TEXT("player_Wukong_actual_buffed_light_damage_matches_unbuffed_native_hit"),WukongBaseLightDamage>0&&P->SkillRemaining>0&&FMath::IsNearlyEqual(Delta,WukongBaseLightDamage*Definition->SkillDamageMultiplier,.1f));
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("Wukong_native_light_damage_comparison"));Row->SetNumberField(TEXT("real_unbuffed_light_HP_delta"),WukongBaseLightDamage);Row->SetNumberField(TEXT("real_buffed_light_HP_delta"),Delta);Row->SetNumberField(TEXT("DA_damage_multiplier"),Definition->SkillDamageMultiplier);Row->SetNumberField(TEXT("actual_buff_remaining_at_native_hit"),P->SkillRemaining);Observations.Add(MakeShared<FJsonValueObject>(Row));
        Next=Now+FMath::Max(.2f,Definition->FPAttack.LoadSynchronous()->GetPlayLength()*.8f);Stage=181;break;
    }
    case 181:
        CheckPose(TEXT("wukong_boosted_light_FP"),true);Next=Now+FMath::Max(P->SkillRemaining+.2f,Mode->Player->GetBioAttackCooldown()+.2f);Stage=182;break;
    case 182:
    {
        Check(TEXT("player_Wukong_buff_expires_and_real_speed_restores"),P->SkillRemaining==0&&P->SkillDamageMultiplier==1.f&&P->SkillSpeedMultiplier==1.f&&FMath::IsNearlyEqual(Mode->Player->GetCharacterMovement()->MaxWalkSpeed,WukongPlayerBaseSpeed,1.f));
        PlaceCombatTarget();const float HP=V->Health;Mode->Player->StartBioFire();Mode->Player->StopBioFire();const float Delta=HP-V->Health;
        Check(TEXT("player_Wukong_actual_same_light_hit_restores_after_expiry"),FMath::IsNearlyEqual(Delta,WukongBaseLightDamage,.1f));
        Next=Now+FMath::Max(.2f,Definition->FPAttack.LoadSynchronous()->GetPlayLength()*.8f);Stage=183;break;
    }
    case 183:
        CheckPose(TEXT("wukong_expired_light_FP"),true);Next=Now+P->SkillCooldownRemaining+.2;Stage=11;break;
    default:Finish(TEXT("Invalid Wukong native buff fixture stage"));break;
    }
}
void AHeliosBioRoleQA::TickZhongXiaoKui(double Now)
{
    auto* Hunter=Cast<AHeliosBioNPC>(Mode->ParticipantActor(HunterId));const auto* Local=Mode->Model.FindParticipant(0);
    if(Now-LastSample>=.04)
    {
        if(Stage==111)SamplePose(TEXT("secondary_FP"),FirstPersonMesh());
        if(Stage==121)SamplePose(TEXT("secondary_TP"),Hunter?Hunter->GetMesh():nullptr);
        SampleProjectile();LastSample=Now;
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("ghost_mask_status"));Row->SetNumberField(TEXT("stage"),Stage);Row->SetNumberField(TEXT("remaining"),Local->SecondarySkillRemaining);Row->SetNumberField(TEXT("speed_multiplier"),Local->MovementSkillMultiplier());Row->SetNumberField(TEXT("actual_walk_speed"),Mode->Player->GetCharacterMovement()->MaxWalkSpeed);Observations.Add(MakeShared<FJsonValueObject>(Row));
    }
    if(Now<Next)return;
    switch(Stage)
    {
    case 110:
        PlaceCombatTarget(500);Mode->Player->StartBioSpecial();SecondaryBefore=Local->SecondaryCooldownRemaining;SpeedBefore=Local->MovementSkillMultiplier();
        Check(TEXT("G_starts_real_ghost_mask"),Local->bGhostMaskActive&&Local->SecondarySkillRemaining>0);Check(TEXT("G_cooldown_blocks_repeat"),SecondaryBefore>0&&!Mode->UseParticipantSecondarySkill(0));
        Next=Now+Definition->FPSecondarySkill.LoadSynchronous()->GetPlayLength()+.15;Stage=111;break;
    case 111:
        CheckPose(TEXT("secondary_FP"),true);Check(TEXT("ghost_mask_speed_gradually_decays"),Local->MovementSkillMultiplier()<SpeedBefore&&Local->MovementSkillMultiplier()>1.f);
        Check(TEXT("ghost_mask_actual_movement_speed_applied"),FMath::IsNearlyEqual(Mode->Player->GetCharacterMovement()->MaxWalkSpeed,560.f*Local->MovementSkillMultiplier(),2.f));
        HealthBefore=Mode->Model.FindParticipant(VictimId)->Health;Mode->Player->StartBioFire();Mode->Player->StopBioFire();WatchProjectile();Check(TEXT("first_attack_spawns_real_native_sword_wave"),bHadProjectile&&!Local->bFirstAttackSwordWave);
        Next=Now+.8;Stage=112;break;
    case 112:
    {
        const float Delta=HealthBefore-Mode->Model.FindParticipant(VictimId)->Health;
        Check(TEXT("first_sword_wave_has_real_travel_and_source_FX"),bHadProjectile&&ProjectileTravel>10&&bProjectileFX);
        Check(TEXT("ghost_mask_boosts_real_projectile_damage"),FMath::IsNearlyEqual(Delta,Definition->SecondaryDamage*Definition->SecondaryDamageMultiplier,2.f));
        const FVector Eyes=Mode->Player->FirstPersonCamera->GetComponentLocation();Check(TEXT("first_sword_wave_cannot_be_duplicated"),!Mode->FireParticipantSwordWave(0,Eyes,FixtureForward,false));
        Next=Now+Mode->Player->GetBioAttackCooldown()+.1;Stage=113;break;
    }
    case 113:
        PlaceCombatTarget();if(!PrepareVictimHealth(300)){Finish(TEXT("Cannot prepare a live target for actual melee kill"));return;}
        MaskBefore=Local->SecondarySkillRemaining;SpeedBefore=Local->MovementSkillMultiplier();KillsBefore=Local->Kills;Mode->Player->StartBioFire();Mode->Player->StopBioFire();Next=Now+.15;Stage=114;break;
    case 114:
        Check(TEXT("actual_melee_kill_permanently_eliminates_target"),!Mode->Model.FindParticipant(VictimId)->bAlive&&Mode->Model.FindParticipant(VictimId)->bPermanentlyDead&&Local->Kills==KillsBefore+1);
        Check(TEXT("actual_melee_kill_extends_mask_and_refreshes_heavy_wave"),Local->SecondarySkillRemaining>MaskBefore&&Local->bHeavySwordWaveReady);
        Check(TEXT("melee_kill_does_not_restart_initial_acceleration"),Local->MovementSkillMultiplier()<=SpeedBefore);
        if(!SelectLivingVictim()){Finish(TEXT("No second live enemy for heavy sword wave"));return;}PlaceCombatTarget(500);Next=Now+Mode->Player->GetBioAttackCooldown()+.15;Stage=115;break;
    case 115:
        HealthBefore=Mode->Model.FindParticipant(VictimId)->Health;Mode->Player->StartHeavy();WatchProjectile();Check(TEXT("refreshed_heavy_attack_spawns_actual_sword_wave"),bHadProjectile&&!Local->bHeavySwordWaveReady);Next=Now+.8;Stage=116;break;
    case 116:
        Check(TEXT("refreshed_heavy_wave_hits_via_projectile_collision"),ProjectileTravel>10&&bProjectileFX&&Mode->Model.FindParticipant(VictimId)->Health<HealthBefore);
        Next=Now+Mode->Player->GetBioAttackCooldown()+.1;Stage=117;break;
    case 117:
        PlaceCombatTarget();if(!PrepareVictimHealth(300)){Finish(TEXT("Cannot prepare second real melee kill for wall test"));return;}Mode->Player->StartBioFire();Mode->Player->StopBioFire();
        Check(TEXT("second_melee_kill_refreshes_wall_test_heavy_wave"),Local->bHeavySwordWaveReady);
        if(!SelectLivingVictim()){Finish(TEXT("No remaining live enemy for physical wall test"));return;}PlaceCombatTarget(650);MakeObstacle(300);Next=Now+Mode->Player->GetBioAttackCooldown()+.1;Stage=118;break;
    case 118:
        HealthBefore=Mode->Model.FindParticipant(VictimId)->Health;Mode->Player->StartHeavy();WatchProjectile();Check(TEXT("wall_test_uses_a_real_projectile"),bHadProjectile);Next=Now+1.1;Stage=119;break;
    case 119:
        Check(TEXT("physical_wall_stops_sword_wave_and_damage"),bHadProjectile&&!Projectile.IsValid()&&ProjectileTravel<300&&Mode->Model.FindParticipant(VictimId)->Health==HealthBefore);RemoveObstacle();
        Next=Now+FMath::Max(Local->SecondaryCooldownRemaining,Mode->Model.FindParticipant(HunterId)->SecondaryCooldownRemaining)+.2;Stage=120;break;
    case 120:
        Check(TEXT("G_cooldown_expires_on_world_ticks"),Local->SecondaryCooldownRemaining==0);Check(TEXT("mask_expiry_clears_queued_waves_and_speed"),!Local->bGhostMaskActive&&!Local->bFirstAttackSwordWave&&!Local->bHeavySwordWaveReady&&Local->MovementSkillMultiplier()==1.f);
        PlaceCombatTarget(500);Check(TEXT("NPC_G_uses_real_shared_action_handler"),Hunter&&Hunter->UseBioSkill(true));Next=Now+Definition->TPSecondarySkill.LoadSynchronous()->GetPlayLength()*.85f;Stage=121;break;
    case 121:
        CheckPose(TEXT("secondary_TP"),true);Check(TEXT("NPC_G_applies_mask_and_cooldown"),Mode->Model.FindParticipant(HunterId)->bGhostMaskActive&&Mode->Model.FindParticipant(HunterId)->SecondaryCooldownRemaining>0);
        FinishCombatFixture(Now);break;
    default:Finish(TEXT("Invalid Zhong Xiao Kui fixture stage"));break;
    }
}
void AHeliosBioRoleQA::SampleMechanicalNPCWindow(bool Requested,bool Accepted)
{
    auto* Hunter=Cast<AHeliosBioNPC>(Mode->ParticipantActor(HunterId));auto* Target=Mode->ParticipantActor(VictimId);const auto* P=Mode->Model.FindParticipant(HunterId);const auto* V=Mode->Model.FindParticipant(VictimId);if(!Hunter||!Target||!P||!V)return;
    FVector Eyes;FRotator Aim;Hunter->GetActorEyesViewPoint(Eyes,Aim);FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(BioMechanicalNPCFixtureLOS),false,Hunter);
    const bool Clear=!GetWorld()->LineTraceSingleByChannel(Hit,Eyes,Target->GetActorLocation()+FVector(0,0,25),ECC_Visibility,Query)||Hit.GetActor()==Target;
    auto* Gun=Definition->bPrimaryThirdPersonIsBody?Hunter->GetMesh():Hunter->GetRoleWeaponMesh();auto* Node=Gun?Gun->GetSingleNodeInstance():nullptr;auto* Clip=Hunter->GetCurrentBioAnimation();
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("mechanical_NPC_original_weapon_window"));Row->SetNumberField(TEXT("stage"),Stage);Row->SetNumberField(TEXT("world_seconds"),GetWorld()->GetTimeSeconds());Row->SetNumberField(TEXT("natural_recovery_elapsed_seconds"),GetWorld()->GetTimeSeconds()-NPCPrimaryRecoveryStarted);Row->SetBoolField(TEXT("public_fire_handler_requested"),Requested);Row->SetBoolField(TEXT("actual_public_fire_handler_return"),Accepted);Row->SetNumberField(TEXT("actual_NPC_shots"),Hunter->ShotsFired);Row->SetNumberField(TEXT("shots_before_recovery"),ShotsBefore);Row->SetNumberField(TEXT("actual_target_HP"),V->Health);Row->SetNumberField(TEXT("target_HP_before_recovery"),HealthBefore);Row->SetStringField(TEXT("actual_target"),GetNameSafe(Hunter->GetDesiredCombatTarget()));Row->SetStringField(TEXT("actual_hunter_position"),Hunter->GetActorLocation().ToString());Row->SetStringField(TEXT("actual_hunter_velocity"),Hunter->GetVelocity().ToString());Row->SetStringField(TEXT("actual_target_position"),Target->GetActorLocation().ToString());Row->SetStringField(TEXT("actual_view_position"),Eyes.ToString());Row->SetBoolField(TEXT("real_weapon_handler_LOS_clear"),Clear);Row->SetStringField(TEXT("actual_LOS_blocker"),GetNameSafe(Hit.GetActor()));Row->SetStringField(TEXT("actual_LOS_component"),GetPathNameSafe(Hit.GetComponent()));Row->SetStringField(TEXT("actual_LOS_impact"),Hit.ImpactPoint.ToString());Row->SetBoolField(TEXT("original_ranged_rig_ready"),Hunter->IsRoleRangedReady());Row->SetBoolField(TEXT("actual_FourSlash_busy"),Hunter->GetFourSlash()->IsBusy());Row->SetNumberField(TEXT("actual_private_melee_cooldown"),Hunter->GetBioMeleeCooldown());Row->SetNumberField(TEXT("actual_private_action_age"),Hunter->GetBioActionAge());Row->SetNumberField(TEXT("actual_private_skill_stage"),Hunter->GetBioSkillStage());Row->SetStringField(TEXT("actual_private_current_clip"),GetPathNameSafe(Clip));Row->SetNumberField(TEXT("actual_current_clip_duration"),Clip?Clip->GetPlayLength():0);Row->SetNumberField(TEXT("actual_skill_cooldown"),P->SkillCooldownRemaining);Row->SetNumberField(TEXT("actual_secondary_cooldown"),P->SecondaryCooldownRemaining);Row->SetNumberField(TEXT("actual_frozen_seconds"),P->FrozenSeconds);Row->SetNumberField(TEXT("actual_weapon_energy"),P->WeaponEnergy);Row->SetNumberField(TEXT("actual_energy_recharge_delay"),P->WeaponRechargeDelay);Row->SetStringField(TEXT("actual_visible_gun_mesh"),Gun?GetPathNameSafe(Gun->GetSkeletalMeshAsset()):TEXT("None"));Row->SetStringField(TEXT("actual_original_gun_clip"),Node?GetPathNameSafe(Node->GetCurrentAsset()):TEXT("None"));Row->SetNumberField(TEXT("actual_original_gun_clip_seconds"),Node?Node->GetCurrentTime():0);Row->SetStringField(TEXT("actual_movement_diagnostics"),Hunter->GetMovementDiagnostic());Observations.Add(MakeShared<FJsonValueObject>(Row));
}
void AHeliosBioRoleQA::TickMechanical(double Now)
{
    auto* Hunter=Cast<AHeliosBioNPC>(Mode->ParticipantActor(HunterId));const auto* Local=Mode->Model.FindParticipant(0);const auto* V=Mode->Model.FindParticipant(VictimId);
    if(Now-LastSample>=.04)
    {
        if(Stage==201||Stage==202)SamplePose(TEXT("primary_FP"),FirstPersonMesh());
        if(Stage==205)SamplePose(TEXT("primary_TP"),Definition->bPrimaryThirdPersonIsBody?Hunter->GetMesh():Hunter->GetRoleWeaponMesh());
        if(Stage==205||Stage==224)SampleMechanicalNPCWindow();
        if(Stage==214)SamplePose(TEXT("secondary_FP"),FirstPersonMesh());
        if(Stage==218)SamplePose(TEXT("secondary_TP"),Hunter->GetMesh());
        if(Stage==220||Stage==221)SamplePose(TEXT("deploy_FP"),FirstPersonMesh());
        SampleProjectile();LastSample=Now;auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("mechanical_status"));Row->SetNumberField(TEXT("stage"),Stage);Row->SetNumberField(TEXT("energy"),Local->WeaponEnergy);Row->SetNumberField(TEXT("recharge_delay"),Local->WeaponRechargeDelay);Row->SetNumberField(TEXT("target_hp"),V?V->Health:0);Row->SetNumberField(TEXT("shots"),Mode->Player->ShotsFired);Observations.Add(MakeShared<FJsonValueObject>(Row));
    }
    // Stop at the first actual exhaustion, before a holding trigger can resume after recharge.
    if(Stage==201&&Local->WeaponEnergy<Definition->PrimaryEnergyCost){Mode->Player->StopBioFire();ShotsBefore=Mode->Player->ShotsFired;Next=Now+Definition->PrimaryFireInterval+.03;Stage=202;}
    if(Now<Next)return;
    switch(Stage)
    {
    case 200:
        for(auto Item:Mode->Turrets)if(Item)Item->Destroy();Mode->Turrets.Reset();PlaceCombatTarget(650);Mode->Player->SelectBioPrimary();
        Check(TEXT("XM214_actual_source_FP_gun_mesh"),FirstPersonMesh()&&FirstPersonMesh()->GetSkeletalMeshAsset()==Definition->PrimaryFirstPersonMesh.LoadSynchronous());
        Next=Now+(Definition->FPPrimaryEquip.LoadSynchronous()?Definition->FPPrimaryEquip.LoadSynchronous()->GetPlayLength():.2f)+.2;Stage=201;ProbeStarted=-1;break;
    case 201:
        if(ProbeStarted<0){HealthBefore=V->Health;EnergyBefore=Local->WeaponEnergy;KillsBefore=Mode->Player->ShotsFired;Mode->Player->StartBioFire();ProbeStarted=Now;Next=Now+15;break;}
        Mode->Player->StopBioFire();Finish(TEXT("XM214 did not reach actual energy exhaustion within 15s of held native firing"));return;
    case 202:
        Check(TEXT("XM214_real_firing_hits_enemy_and_spends_energy"),V->Health<HealthBefore&&Local->WeaponEnergy<EnergyBefore&&Mode->Player->ShotsFired>KillsBefore);CheckPose(TEXT("primary_FP"),true);
        Check(TEXT("exhausted_XM214_blocks_real_weapon_handler"),!Mode->FireParticipantRoleWeapon(0,Mode->Player->FirstPersonCamera->GetComponentLocation(),FixtureForward));
        Check(TEXT("exhaustion_does_not_fire_another_native_shot"),Mode->Player->ShotsFired==ShotsBefore);EnergyBefore=Local->WeaponEnergy;
        Next=Now+FMath::Max(.01f,Local->WeaponRechargeDelay*.5f);Stage=203;break;
    case 203:
        Check(TEXT("energy_recharge_respects_remaining_delay"),FMath::IsNearlyEqual(Local->WeaponEnergy,EnergyBefore,.001f));Next=Now+Local->WeaponRechargeDelay+.3;Stage=204;break;
    case 204:
        Check(TEXT("energy_recovers_on_real_world_ticks"),Local->WeaponEnergy>EnergyBefore&&Local->WeaponEnergy<=Local->WeaponMaxEnergy);
        Mode->Player->SelectBioMelee();PlaceCombatTarget(650);Check(TEXT("NPC_XM214_original_rig_is_ready"),Hunter&&Hunter->IsRoleRangedReady());
        Mode->Model.ActivateSkill(HunterId,.01f,30.f,1.f,1.f,0.f);Mode->Model.ActivateSecondarySkill(HunterId,0.f,30.f);
        Hunter->bQAFreeze=false;ShotsBefore=Hunter->ShotsFired;HealthBefore=V->Health;NPCPrimaryRecoveryStarted=Now;PoseProbes.Remove(TEXT("primary_TP"));Next=Now;Stage=224;break;
    case 224:
    {
        // bQAFreeze paused the previous real heavy swing's private cooldown. Let
        // production Tick/Think recover it without freezing movement or forcing a hit.
        const bool Accepted=Hunter->FireRoleWeaponAt(Mode->ParticipantActor(VictimId));SampleMechanicalNPCWindow(true,Accepted);
        auto* Gun=Definition->bPrimaryThirdPersonIsBody?Hunter->GetMesh():Hunter->GetRoleWeaponMesh();auto* Node=Gun?Gun->GetSingleNodeInstance():nullptr;
        const bool NativeGunActive=Gun&&Gun->GetSkeletalMeshAsset()==Definition->PrimaryThirdPersonWeaponMesh.LoadSynchronous()&&Node&&Node->GetCurrentAsset()==Definition->TPPrimaryFire.LoadSynchronous();
        // Production Tick may have committed its shot before the explicit public
        // request in this frame; its increased native counter and exact gun clip
        // establish that real shot even when our repeat correctly hits the fire CD.
        if(Hunter->ShotsFired>ShotsBefore&&NativeGunActive)
        {Check(TEXT("NPC_XM214_natural_action_recovery_and_actual_firing"),true);SamplePose(TEXT("primary_TP"),Gun);Next=Now+.8;Stage=205;break;}
        if(Now-NPCPrimaryRecoveryStarted>=8.f)
        {Check(TEXT("NPC_XM214_natural_action_recovery_and_actual_firing"),false);Next=Now;Stage=205;break;}
        Next=Now+.04;break;
    }
    case 205:
    {
        Hunter->bQAFreeze=true;Hunter->GetCharacterMovement()->StopMovementImmediately();
        // The source fire clip lasts 0.1s. Production may legitimately return to
        // its melee body while moving during this 0.8s observation window.
        // Require mesh, native fire clip, committed shot and real damage together
        // in an actual recorded sample rather than pinning the final recovery pose.
        bool NativeShotWindow=false;
        for(const auto& Value:Observations)
        {
            const auto Row=Value->AsObject();if(!Row||Row->GetStringField(TEXT("kind"))!=TEXT("mechanical_NPC_original_weapon_window"))continue;
            NativeShotWindow|=Row->GetStringField(TEXT("actual_visible_gun_mesh"))==GetPathNameSafe(Definition->PrimaryThirdPersonWeaponMesh.LoadSynchronous())&&
                Row->GetStringField(TEXT("actual_original_gun_clip"))==GetPathNameSafe(Definition->TPPrimaryFire.LoadSynchronous())&&
                Row->GetNumberField(TEXT("actual_NPC_shots"))>ShotsBefore&&Row->GetNumberField(TEXT("actual_target_HP"))<HealthBefore;
        }
        Check(TEXT("NPC_XM214_real_source_TP_mesh_and_native_shots"),NativeShotWindow&&Hunter->ShotsFired>ShotsBefore&&V->Health<HealthBefore);CheckPose(TEXT("primary_TP"),true);
        PlaceCombatTarget(400);PoseProbes.Remove(TEXT("deploy_FP"));Mode->Player->UseBioSkill();Turret.Reset();HealthBefore=V->Health;
        // Production releases the turret at the original 0.7s deployment moment. Sample the short native clip from this cast frame.
        SamplePose(TEXT("deploy_FP"),FirstPersonMesh());Next=Now+.9;Stage=220;break;
    }
    case 220:
    {
        CheckPose(TEXT("deploy_FP"),true);for(auto Item:Mode->Turrets)if(Item&&Item->GetCasterId()==0)Turret=Item;
        Check(TEXT("F_deploys_actual_source_mesh_turret"),Turret.IsValid()&&Turret->IsActiveTurret()&&Turret->FindComponentByClass<UStaticMeshComponent>()&&Turret->FindComponentByClass<UStaticMeshComponent>()->GetStaticMesh()==Definition->SkillAuxiliaryMesh.LoadSynchronous());
        if(!Turret.IsValid()){Finish(TEXT("Actual source turret deployment failed"));return;}HealthBefore=V->Health;Next=Now+1;Stage=206;break;
    }
    case 206:
        Check(TEXT("turret_autonomous_native_fire_hits_real_enemy"),Turret.IsValid()&&Turret->GetShots()>0&&V->Health<HealthBefore);
        MakeObstacle(300);HealthBefore=V->Health;ShotsBefore=Turret.IsValid()?Turret->GetShots():-1;Next=Now+FMath::Max(.5f,Definition->SkillFireInterval*2);Stage=207;break;
    case 207:
        Check(TEXT("physical_wall_blocks_turret_LOS_and_damage"),Turret.IsValid()&&Turret->GetShots()==ShotsBefore&&V->Health==HealthBefore);RemoveObstacle();
        if(!Turret.IsValid()){Finish(TEXT("Turret expired before destruction test"));return;}
        Check(TEXT("turret_rejects_friendly_damage"),Turret->ApplyBioDamage(HunterId,100)==0);HealthBefore=V->Health;
        {
            auto* ActualTurret=Turret.Get();const float Applied=ActualTurret->ApplyBioDamage(VictimId,ActualTurret->GetHealth()+1);
            Check(TEXT("real_enemy_damage_destroys_turret"),Applied>0);
            // Capture the synchronous real resolution before a later GC can
            // invalidate the destroyed actor's weak pointer.
            auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("mechanical_actual_turret_destruction_resolution"));Row->SetNumberField(TEXT("world_seconds"),GetWorld()->GetTimeSeconds());
            Row->SetNumberField(TEXT("actual_damage_applied_to_turret"),Applied);Row->SetBoolField(TEXT("actual_turret_being_destroyed"),ActualTurret->IsActorBeingDestroyed());Row->SetNumberField(TEXT("actual_explosion_hits"),ActualTurret->GetExplosionHits());
            Row->SetNumberField(TEXT("actual_target_HP_before_destruction"),HealthBefore);Row->SetNumberField(TEXT("actual_target_HP_after_destruction"),V->Health);Observations.Add(MakeShared<FJsonValueObject>(Row));
        }
        Next=Now+.1;Stage=208;break;
    case 208:
    {
        bool ActualExplosion=false;
        for(const auto& Value:Observations)
        {
            const auto Row=Value->AsObject();if(!Row||Row->GetStringField(TEXT("kind"))!=TEXT("mechanical_actual_turret_destruction_resolution"))continue;
            ActualExplosion=Row->GetBoolField(TEXT("actual_turret_being_destroyed"))&&Row->GetNumberField(TEXT("actual_explosion_hits"))>0&&
                Row->GetNumberField(TEXT("actual_target_HP_after_destruction"))<Row->GetNumberField(TEXT("actual_target_HP_before_destruction"));
        }
        Check(TEXT("destruction_explosion_hits_actual_enemy"),!Turret.IsValid()&&ActualExplosion&&V->Health<HealthBefore);
        Next=Now+Local->SkillCooldownRemaining+.2;Stage=209;break;
    }
    case 209:
        PlaceCombatTarget(400);Mode->Player->UseBioSkill();Turret.Reset();SamplePose(TEXT("deploy_FP"),FirstPersonMesh());Next=Now+.9;Stage=221;break;
    case 221:
        for(auto Item:Mode->Turrets)if(Item&&Item->GetCasterId()==0)Turret=Item;
        if(!Turret.IsValid()){Finish(TEXT("Second actual F cast did not deploy turret after world cooldown"));return;}
        Next=Now+Turret->GetLifetimeRemaining()+.15;Stage=210;break;
    case 210:
    {
        auto* Expired=Turret.Get(true);Check(TEXT("turret_lifetime_expires_with_real_explosion"),!Turret.IsValid()&&Expired&&Expired->GetExplosionHits()>0);
        PlaceCombatTarget(200);if(!PrepareVictimHealth(200)){Finish(TEXT("Cannot prepare a live evolved zombie for actual Arcane projectile kill"));return;}
        HistoricalInfections=V->Infections;Check(TEXT("Arcane_target_has_evolution_to_downgrade"),V->EvolutionLevel>0);Mode->Player->StartBioSpecial();WatchProjectile();Check(TEXT("G_cast_spawns_actual_original_Arcane_projectile"),bHadProjectile);Next=Now+1.8;Stage=214;break;
    }
    case 214:
        CheckPose(TEXT("secondary_FP"),true);Check(TEXT("Arcane_kill_is_real_temporary_death"),!V->bAlive&&!V->bPermanentlyDead&&V->RespawnSeconds>0);
        Check(TEXT("Arcane_devolution_keeps_historical_score"),V->EvolutionLevel==0&&V->EvolutionInfections==0&&V->Infections==HistoricalInfections);
        Check(TEXT("Arcane_has_real_source_visual_and_travel"),bHadProjectile&&ProjectileTravel>10&&bProjectileFX);Check(TEXT("Arcane_G_cooldown_blocks_repeat"),!Mode->UseParticipantSecondarySkill(0));
        Next=Now+V->RespawnSeconds+.2;Stage=215;break;
    case 215:
        Check(TEXT("Arcane_victim_respawns_at_devolved_HP"),V->bAlive&&V->Health==V->MaxHealth&&V->EvolutionLevel==0);Check(TEXT("confirmed_hunter_melee_eliminates_target_permanently"),Mode->DamageParticipant(VictimId,0,V->Health+1,true));
        Next=Now+Local->SecondaryCooldownRemaining+.2;Stage=216;break;
    case 216:
    {
        PlaceActor(Mode->Player,FixtureOrigin);auto* Corpse=Cast<ACharacter>(Mode->ParticipantActor(VictimId));
        if(!PlaceActor(Corpse,FixtureOrigin+FixtureForward*200.f)){Finish(TEXT("Cannot position the real permanently eliminated target within the Arcane impact fixture"));return;}
        const FVector Feet=Corpse->GetActorLocation()-FVector(0,0,Corpse->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        Mode->Player->GetController()->SetControlRotation((Feet-Mode->Player->FirstPersonCamera->GetComponentLocation()).Rotation());
        Next=Now+.1;Stage=222;break;
    }
    case 222:
        Mode->Player->StartBioSpecial();WatchProjectile();Check(TEXT("Arcane_denial_uses_a_real_G_projectile"),bHadProjectile);Next=Now+1.8;Stage=223;break;
    case 223:
    {
        auto* Resolved=Projectile.Get(true);if(Resolved)LastProjectilePosition=Resolved->GetActorLocation();
        const float Distance=FVector::Dist(LastProjectilePosition,Mode->ParticipantActor(VictimId)->GetActorLocation());
        Check(TEXT("Arcane_denial_projectile_resolves_inside_actual_target_radius"),bHadProjectile&&!Projectile.IsValid()&&ProjectileTravel>10&&Distance<=Definition->SecondaryRange&&Mode->Model.GetPhase()==EHeliosBioPhase::Active);
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("Arcane_permanent_target_impact"));Row->SetStringField(TEXT("projectile_end"),LastProjectilePosition.ToString());Row->SetStringField(TEXT("permanent_target"),Mode->ParticipantActor(VictimId)->GetActorLocation().ToString());Row->SetNumberField(TEXT("distance_cm"),Distance);Row->SetNumberField(TEXT("actual_effect_radius_cm"),Definition->SecondaryRange);Row->SetStringField(TEXT("position_source"),Resolved?TEXT("resolved_collision_projectile_actor"):TEXT("last_live_collision_projectile_sample"));Observations.Add(MakeShared<FJsonValueObject>(Row));
        Next=Now+Mode->Model.Config.RespawnSeconds+.2;Stage=217;break;
    }
    case 217:
        Check(TEXT("Arcane_cannot_restore_permanently_eliminated_target"),!V->bAlive&&V->bPermanentlyDead&&V->RespawnSeconds==0);
        if(!SelectLivingVictim()){Finish(TEXT("No active target remains for real NPC Arcane action"));return;}PlaceCombatTarget(500);
        Next=Now+Mode->Model.FindParticipant(HunterId)->SecondaryCooldownRemaining+.15;Stage=219;break;
    case 219:
        Check(TEXT("NPC_G_uses_real_Arcane_handler"),Hunter->UseBioSkill(true));Next=Now+Definition->TPSecondarySkill.LoadSynchronous()->GetPlayLength()*.85f;Stage=218;break;
    case 218:
        CheckPose(TEXT("secondary_TP"),true);Check(TEXT("NPC_G_has_independent_world_cooldown"),Mode->Model.FindParticipant(HunterId)->SecondaryCooldownRemaining>0);FinishCombatFixture(Now);break;
    default:Finish(TEXT("Invalid mechanical fixture stage"));break;
    }
}
void AHeliosBioRoleQA::Tick(float Dt)
{
    Super::Tick(Dt);const double Now=GetWorld()->GetTimeSeconds();if(Stage==99)return;
    if(Stage==0)
    {
        FParse::Value(FCommandLine::Get(),TEXT("BioRoleQAOut="),Out);FParse::Value(FCommandLine::Get(),TEXT("BioTestRole="),TestRole);
        bManualSpectator=FParse::Param(FCommandLine::Get(),TEXT("BioRoleManualSpectatorQA"));
        if(Out.IsEmpty()||TestRole.IsEmpty()){Finish(TEXT("Role fixture requires BioTestRole and BioRoleQAOut"));return;}
        IFileManager::Get().MakeDirectory(*Out,true);Mode=AHeliosBioHotelGameMode::Find(GetWorld());
        if(!Mode||!Mode->Player){if(Now>30)Finish(TEXT("Real game mode/player missing"));return;}
        if(!bHumanBaselineFrozen){for(auto Bot:Mode->Bots)if(Bot){Bot->bQAFreeze=true;Bot->GetCharacterMovement()->StopMovementImmediately();if(auto* Presentation=Bot->FindComponentByClass<UHeliosCharacterPresentationComponent>())Presentation->PresentAction(TEXT("Idle"),0,true);}bHumanBaselineFrozen=true;BaselineAt=Now;return;}
        if(Now<3||Now-BaselineAt<.2)return;SampleHumanBaseline(TEXT("initial_spawn"));
        Definition=HeliosBioArt::Named(FName(TestRole));FString Blocker;if(!ValidateSourceBindings(Blocker)){Finish(Blocker);return;}
        bAbyss=TestRole==TEXT("BioAbyssTerminator");Mode->Model.Config.PreparationSeconds=.2f;Mode->Model.Config.RoundSeconds=600;
        Mode->Model.Config.bKeepLocalPlayerHuman=!bAbyss;Mode->Model.Config.ZombieHealth=Mode->Model.Config.MotherHealth=1000000.f;
        if(bAbyss)
        {
            Mode->Model.Config.bMothersAreTerminators=true;Mode->Model.Config.InitialMotherCount=2;Mode->Model.Config.bUseCurrentVersionRoles=false;
            Mode->Model.Config.bAbyssSourceAvailable=true;Mode->Model.Config.AbyssRoleId=FName(TestRole);Mode->Model.Config.AbyssHealth=100000.f;
            bool Found=false;for(int32 Seed=0;Seed<20000&&!Found;++Seed){FHeliosBioRules Candidate;Candidate.Config=Mode->Model.Config;Candidate.StartRound(30,Seed);Candidate.Tick(.3f);
                if(Candidate.FindParticipant(0)->bMother&&Candidate.FindParticipant(1)->bMother){Mode->BaseRoundSeed=Seed-Mode->RoundSerial*103;Found=true;}}
            if(!Found){Finish(TEXT("Unable to derive actual player/NPC mother seed for isolated red-box fixture"));return;}
        }
        Mode->RestartBioRound();for(auto Bot:Mode->Bots)if(Bot)Bot->bQAFreeze=true;
        Next=Now+1;Started=Now;Stage=1;return;
    }
    if(Stage==16&&bManualSpectator)
    {
        const double Wall=FPlatformTime::Seconds();
        const bool RawF9=GetWorld()->GetFirstPlayerController()&&GetWorld()->GetFirstPlayerController()->WasInputKeyJustPressed(EKeys::F9);
        if(Mode->RoundSerial!=ManualSpectatorRound)
        {
            bManualSpectatorRoundChanged=true;
            if(ManualSpectatorRestartWall<=0)ManualSpectatorRestartWall=Wall;
        }
        if(RawF9||Wall-ManualSpectatorStartedWall>=180.||(ManualSpectatorRestartWall>0&&Wall-ManualSpectatorRestartWall>=.35))
        {
            bManualSpectatorFinished=true;ManualSpectatorStopReason=RawF9?TEXT("Actual raw F9 finish edge"):bManualSpectatorRoundChanged?TEXT("Observed production round serial change; no further fixture damage/actions executed"):TEXT("Bounded180-second manual spectator wall window ended");
            auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("manual_spectator_observation_finished"));Row->SetStringField(TEXT("reason"),ManualSpectatorStopReason);Row->SetNumberField(TEXT("round_before"),ManualSpectatorRound);Row->SetNumberField(TEXT("actual_round_after"),Mode->RoundSerial);Row->SetNumberField(TEXT("actual_spectated_participant"),Mode->Player->GetSpectatedParticipantId());Row->SetBoolField(TEXT("actual_player_alive"),Mode->Model.FindParticipant(0)->bAlive);Row->SetBoolField(TEXT("raw_input_verified_by_this_actor"),false);Observations.Add(MakeShared<FJsonValueObject>(Row));Finish();
        }
        return;
    }
    if(Now-Started>300){Finish(FString::Printf(TEXT("Role fixture stalled at stage %d"),Stage));return;}
    if(Stage>=110&&Stage<150){TickZhongXiaoKui(Now);return;}
    if(Stage>=160&&Stage<180){TickNPCValidation(Now);return;}
    if(Stage>=180&&Stage<190){TickWukongBuff(Now);return;}
    if(Stage>=200&&Stage<250){TickMechanical(Now);return;}
    if(Stage>=250&&Stage<280){TickSerum(Now);return;}
    auto* Hunter=Cast<AHeliosBioNPC>(Mode->ParticipantActor(HunterId));const auto* Victim=Mode->Model.FindParticipant(VictimId);
    if(Stage>=2&&Stage<=12&&Victim){LowestVictimHealth=FMath::Min(LowestVictimHealth,Victim->Health);MaxFrozenSeconds=FMath::Max(MaxFrozenSeconds,Victim->FrozenSeconds);}
    if(Stage==10&&(Definition->Ability==EHeliosBioAbility::ZhongXiaoKuiBlade||TestRole==TEXT("BioSunWukong")))if(auto* Target=Cast<ACharacter>(Mode->ParticipantActor(VictimId)))
    {bVictimAirborne|=Target->GetCharacterMovement()->IsFalling();if(Target->GetCharacterMovement()->IsFalling()&&FVector::Dist(Target->GetActorLocation(),LastVictimPosition)>.1f)++AirMotionSamples;LastVictimPosition=Target->GetActorLocation();}
    if(Stage==4)Mode->Player->AddMovementInput(FixtureForward,1.f);
    if(Stage==10&&TestRole==TEXT("BioSunWukong"))
    {
        const auto* P=Mode->Model.FindParticipant(0);const float Actual=Mode->Player->GetCharacterMovement()->MaxWalkSpeed;
        if(P->SkillRemaining>0)bWukongPlayerSpeedApplied|=WukongPlayerBaseSpeed>0&&FMath::IsNearlyEqual(Actual,WukongPlayerBaseSpeed*Definition->SkillSpeedMultiplier,1.f);
        if(Now-LastSample>=.04){auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("Wukong_player_buff_actual_movement_component"));Row->SetNumberField(TEXT("base_max_walk_speed"),WukongPlayerBaseSpeed);Row->SetNumberField(TEXT("actual_max_walk_speed"),Actual);Row->SetNumberField(TEXT("DA_speed_multiplier"),Definition->SkillSpeedMultiplier);Row->SetNumberField(TEXT("actual_buff_remaining"),P->SkillRemaining);Row->SetBoolField(TEXT("actual_player_tick_applied_multiplier"),bWukongPlayerSpeedApplied);Observations.Add(MakeShared<FJsonValueObject>(Row));}
    }
    if(Now-LastSample>=.04&&Stage>=3&&Stage<=15)
    {
        const FString Action=Stage==3?TEXT("idle"):Stage==4?TEXT("move"):Stage==6?TEXT("light"):Stage==8?TEXT("heavy"):Stage==10?TEXT("skill"):Stage==13?TEXT("death"):FString();
        if(!Action.IsEmpty()){SamplePose(Action+TEXT("_FP"),FirstPersonMesh());if(Hunter)SamplePose(Action+TEXT("_TP"),Hunter->GetMesh());}LastSample=Now;
    }
    if(Now<Next)return;
    switch(Stage)
    {
    case 1:
    {
        const int32 BeforeQualification=Failures;
        if(Mode->Model.GetPhase()!=EHeliosBioPhase::Active){Next=Now+.1;break;}
        if(bAbyss){HunterId=1;for(const auto& P:Mode->Model.GetParticipants())if(P.Team==EHeliosBioTeam::Human&&VictimId==INDEX_NONE)VictimId=P.Id;
            for(int32 Id:{0,HunterId}){const auto* P=Mode->Model.FindParticipant(Id);if(!P||!P->bMother||P->ZombieForm!=EHeliosBioZombieForm::Terminator){Finish(TEXT("Red-box fixture did not obtain actual Terminator mother qualification"));return;}
                const int32 Required=Mode->Model.Config.MaximumZombieEvolution*Mode->Model.Config.ZombieSuppliesPerEvolution;
                for(int32 N=0;N<Required;++N)if(!PickupFixtureSupply(Id,int32(EHeliosBioSupplyKind::Normal))){Finish(TEXT("Actual normal-supply evolution path failed"));return;}
                Check(*FString::Printf(TEXT("id%d_real_red_box_evolution"),Id),PickupFixtureSupply(Id,int32(EHeliosBioSupplyKind::RedTerminatorEvolution)));}}
        else
        {
            for(const auto& P:Mode->Model.GetParticipants())if(P.bMother&&VictimId==INDEX_NONE)VictimId=P.Id;else if(P.Id!=0&&P.Team==EHeliosBioTeam::Human&&HunterId==INDEX_NONE)HunterId=P.Id;
            if(VictimId==INDEX_NONE||HunterId==INDEX_NONE){Finish(TEXT("Fixture roster could not select hunter and zombie"));return;}
            for(const auto& P:Mode->Model.GetParticipants())if(P.Id!=0&&P.Id!=HunterId&&P.Team==EHeliosBioTeam::Human)Mode->Model.Infect(P.Id,VictimId);
            Mode->Model.Config.AllowedHunterRoles.Reset();Mode->Model.Config.AllowedHunterRoles.Add(FName(TestRole));
            for(int32 Id:{0,HunterId}){Check(*FString::Printf(TEXT("id%d_ghost_qualification"),Id),Mode->TransformParticipant(Id));Check(*FString::Printf(TEXT("id%d_real_blue_box_qualification"),Id),PickupFixtureSupply(Id,int32(EHeliosBioSupplyKind::BlueHunterUpgrade)));
                // The real NPC pickup handler confirms its choice automatically; the local player confirms explicitly.
                if(Id==0)Check(TEXT("player_explicit_role_selection"),Mode->SelectHunter(Id,FName(TestRole)));
                const auto* Selected=Mode->Model.FindParticipant(Id);Check(*FString::Printf(TEXT("id%d_confirmed_role_identity"),Id),Selected&&Selected->Team==EHeliosBioTeam::UltimateHunter&&Selected->HunterRoleId==FName(TestRole));}
        }
        if(Failures>BeforeQualification||VictimId==INDEX_NONE){Finish(TEXT("Role qualification failed; no substitute actor/role was used"));return;}
        for(auto Bot:Mode->Bots)if(Bot&&Bot->ParticipantId!=VictimId&&Bot->ParticipantId!=HunterId){Bot->SetActorTickEnabled(false);Bot->SetActorHiddenInGame(true);Bot->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);Bot->GetCharacterMovement()->DisableMovement();Bot->SetActorLocation(FVector(1000000+Bot->ParticipantId*500,1000000,1000000),false);}
        for(auto Supply:Mode->Supplies)if(Supply)Supply->SetAvailable(false);
        if(!FindFixtureLane()){Finish(TEXT("No source-map spawn has a clear 8m, level two-character fixture lane"));return;}
        Next=Now+2;Stage=2;break;
    }
    case 2:
        if(!Hunter){Finish(TEXT("Fixture NPC missing"));return;}
        // Some native equip clips (ZhongXiaoKui B_Show is 4.53s) are longer
        // than the old fixed two-second settle. Observe idle only after the
        // real player's equip cooldown has elapsed; never count Show as Idle.
        if(Mode->Player->GetBioAttackCooldown()>0){Next=Now+.1;break;}
        PlaceCombatTarget();
        Check(TEXT("real_third_person_mesh"),Hunter->GetMesh()->GetSkeletalMeshAsset()==Definition->ThirdPersonMesh.LoadSynchronous());Check(TEXT("real_first_person_mesh"),FirstPersonMesh()!=nullptr);
        Capture(TEXT("role_01_idle"));Next=Now+1;Stage=3;break;
    case 3:
        CheckPose(TEXT("idle_FP"),false);CheckPose(TEXT("idle_TP"),false);
        if(!CheckGround(TEXT("role_standing"),Hunter,Hunter?Hunter->GetMesh():nullptr)){Finish(TEXT("Actual rendered shoe sole/floor measurement unavailable"));return;}
        PlaceCombatTarget(800);PlayerMoveStart=Mode->Player->GetActorLocation();HunterMoveStart=Hunter->GetActorLocation();
        // Prevent an autonomous cast replacing the actual locomotion probe.
        Mode->Model.ActivateSkill(HunterId,.01f,30.f,1.f,1.f,0.f);
        if(Definition->Ability==EHeliosBioAbility::MechanicalFire||Definition->Ability==EHeliosBioAbility::ZhongXiaoKuiBlade)Mode->Model.ActivateSecondarySkill(HunterId,0.f,30.f);
        if(Definition->Ability==EHeliosBioAbility::MechanicalFire)MakeObstacle(400);Hunter->bQAFreeze=false;Next=Now+1;Stage=4;break;
    case 4:
    {
        if(TestRole==TEXT("BioSunWukong"))WukongNPCBaseSpeed=Hunter->GetCharacterMovement()->MaxWalkSpeed;
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("native_NPC_movement_window"));Row->SetStringField(TEXT("start"),HunterMoveStart.ToString());Row->SetStringField(TEXT("end"),Hunter->GetActorLocation().ToString());Row->SetStringField(TEXT("goal"),Hunter->GetMovementGoal().ToString());Row->SetStringField(TEXT("intent"),Hunter->GetMovementIntent().ToString());Row->SetNumberField(TEXT("last_native_move_result"),Hunter->GetLastMoveResult());Row->SetNumberField(TEXT("actual_speed"),Hunter->GetVelocity().Size2D());Row->SetStringField(TEXT("diagnostics"),Hunter->GetMovementDiagnostic());Observations.Add(MakeShared<FJsonValueObject>(Row));
        RemoveObstacle();Mode->Player->ClearMovementInput();Mode->Player->GetCharacterMovement()->StopMovementImmediately();Hunter->bQAFreeze=true;Hunter->GetCharacterMovement()->StopMovementImmediately();
        Check(TEXT("player_actual_ground_movement"),FVector::Dist2D(PlayerMoveStart,Mode->Player->GetActorLocation())>40);
        Check(TEXT("NPC_actual_ground_movement"),FVector::Dist2D(HunterMoveStart,Hunter->GetActorLocation())>40);CheckPose(TEXT("move_FP"),!Definition->FPMove.IsNull());CheckPose(TEXT("move_TP"),true);
        Capture(TEXT("role_02_motion"));PlaceCombatTarget();Next=Now+.4;Stage=5;break;
    }
    case 5:
        if(Mode->Player->GetBioAttackCooldown()>0){Next=Now+.1;break;}
        HealthBefore=Mode->Model.FindParticipant(VictimId)->Health;Mode->Player->StartBioFire();Mode->Player->StopBioFire();
        Check(TEXT("real_light_attack_native_clip"),FirstPersonMesh()&&FirstPersonMesh()->GetSingleNodeInstance()&&FirstPersonMesh()->GetSingleNodeInstance()->GetCurrentAsset()==Definition->FPAttack.LoadSynchronous());Next=Now+FMath::Max(.2f,Definition->FPAttack.LoadSynchronous()->GetPlayLength()*.8f);Stage=6;break;
    case 6:
        if(TestRole==TEXT("BioSunWukong"))WukongBaseLightDamage=HealthBefore-Mode->Model.FindParticipant(VictimId)->Health;
        Check(TEXT("real_light_attack_damage"),Mode->Model.FindParticipant(VictimId)->Health<HealthBefore);CheckPose(TEXT("light_FP"),true);Next=Now+Mode->Player->GetBioAttackCooldown()+.2;Stage=7;break;
    case 7:
        PlaceCombatTarget();HealthBefore=Mode->Model.FindParticipant(VictimId)->Health;Mode->Player->StartHeavy();Capture(TEXT("role_03_heavy"));
        Check(TEXT("real_heavy_attack_native_clip"),FirstPersonMesh()&&FirstPersonMesh()->GetSingleNodeInstance()&&FirstPersonMesh()->GetSingleNodeInstance()->GetCurrentAsset()==Definition->FPHeavy.LoadSynchronous());Next=Now+FMath::Max(.2f,Definition->FPHeavy.LoadSynchronous()->GetPlayLength()*.8f);Stage=8;break;
    case 8:
        Check(TEXT("real_heavy_attack_damage"),Mode->Model.FindParticipant(VictimId)->Health<HealthBefore);CheckPose(TEXT("heavy_FP"),true);
        if(TestRole==TEXT("BioSunWukong"))WukongPlayerBaseSpeed=Mode->Player->GetCharacterMovement()->MaxWalkSpeed;
        if(!Mode->IsParticipantSkillImplemented(0)){Finish(TEXT("Source art imported; complete role-specific mechanism or original projectile/weapon is unavailable"));return;}
        Next=Now+Mode->Player->GetBioAttackCooldown()+.2;Stage=9;break;
    case 9:
    {
        PlaceCombatTarget(Definition->Ability==EHeliosBioAbility::Blink?750.f:Definition->Ability==EHeliosBioAbility::MechanicalFire?400.f:200.f);CastPosition=Mode->Player->GetActorLocation();HealthBefore=Mode->Model.FindParticipant(VictimId)->Health;LowestVictimHealth=HealthBefore;MaxFrozenSeconds=0;
        VictimBefore=Mode->ParticipantActor(VictimId)->GetActorLocation();LastVictimPosition=VictimBefore;bVictimAirborne=false;AirMotionSamples=0;
        if(TestRole==TEXT("BioSunWukong")){WukongPlayerAreaHitsBefore=Mode->TotalAreaSkillHits;bWukongPlayerSpeedApplied=false;}
        Mode->Player->UseBioSkill();CooldownBefore=Mode->Model.FindParticipant(0)->SkillCooldownRemaining;Check(TEXT("skill_enters_cooldown"),CooldownBefore>0);Check(TEXT("cooldown_blocks_repeat_cast"),!Mode->UseParticipantSkill(0));
        if(TestRole==TEXT("BioSunWukong"))
        {
            const auto* P=Mode->Model.FindParticipant(0);
            Check(TEXT("player_Wukong_F_real_buff_parameters_match_actual_DA"),P->SkillRemaining>0&&FMath::IsNearlyEqual(P->SkillRemaining,Definition->SkillDuration,.01f)&&FMath::IsNearlyEqual(P->SkillCooldownRemaining,FMath::Max(Definition->SkillDuration,Definition->SkillCooldown),.01f)&&FMath::IsNearlyEqual(P->SkillSpeedMultiplier,Definition->SkillSpeedMultiplier,.001f)&&FMath::IsNearlyEqual(P->SkillDamageMultiplier,Definition->SkillDamageMultiplier,.001f)&&Definition->SkillSpeedMultiplier>1.f&&Definition->SkillDamageMultiplier>1.f);
            auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("Wukong_offline_DA_buff_definition"));Row->SetStringField(TEXT("DA"),Definition->GetPathName());Row->SetNumberField(TEXT("DA_duration_seconds"),Definition->SkillDuration);Row->SetNumberField(TEXT("DA_cooldown_seconds"),Definition->SkillCooldown);Row->SetNumberField(TEXT("DA_speed_multiplier"),Definition->SkillSpeedMultiplier);Row->SetNumberField(TEXT("DA_damage_multiplier"),Definition->SkillDamageMultiplier);Row->SetStringField(TEXT("scope"),TEXT("Explicit standalone F tuning read from the saved role DA; this does not establish original client numeric balance."));Observations.Add(MakeShared<FJsonValueObject>(Row));
        }
        if(Definition->Ability==EHeliosBioAbility::ZhongXiaoKuiBlade)if(auto* Target=Cast<AHeliosBioNPC>(Mode->ParticipantActor(VictimId)))Target->bQAFreeze=false;
        if(Definition->Ability==EHeliosBioAbility::SacredShield||Definition->Ability==EHeliosBioAbility::Shield)
        {const float HP=Mode->Model.FindParticipant(0)->Health;const bool Applied=Mode->DamageParticipant(0,VictimId,100,false);const float Delta=HP-Mode->Model.FindParticipant(0)->Health;Check(TEXT("shield_reduces_real_damage_while_active"),Applied&&Delta>0&&Delta<100);}
        Capture(TEXT("role_04_skill"));float Duration=Definition->FPSkill.LoadSynchronous()?Definition->FPSkill.LoadSynchronous()->GetPlayLength():0;
        if(Mode->Player->GetFourSlash()->IsBusy()){Duration=0;for(const auto& Clip:Definition->FPSkillChain)Duration+=Clip.LoadSynchronous()->GetPlayLength()/Mode->Player->GetFourSlash()->PlaybackRate;}
        Next=Now+FMath::Max(2.f,Duration+1.f);Stage=10;break;
    }
    case 10:
    {
        const auto Ability=Definition->Ability;
        if(Ability==EHeliosBioAbility::Blink)Check(TEXT("blink_actual_collision_safe_displacement"),FVector::Dist(CastPosition,Mode->Player->GetActorLocation())>30&&Mode->Player->GetCharacterMovement()->IsMovingOnGround());
        else if(Ability==EHeliosBioAbility::SacredShield||Ability==EHeliosBioAbility::Shield){}
        else if(Ability==EHeliosBioAbility::FreezeGrenade)Check(TEXT("real_projectile_freezes_enemy"),MaxFrozenSeconds>0);
        else if(Ability==EHeliosBioAbility::FourSlash||Mode->Player->GetFourSlash()->GetCompletedStages()>0)
        {Check(TEXT("four_slash_all_real_callbacks_complete"),Mode->Player->GetFourSlash()->GetCompletedStages()==4&&Mode->Player->GetFourSlash()->GetHitCallbacks()==4);Check(TEXT("four_slash_actual_enemy_damage"),LowestVictimHealth<HealthBefore);}
        else if(Ability==EHeliosBioAbility::AreaStrike||Ability==EHeliosBioAbility::AbyssEnergy)Check(TEXT("skill_applies_real_enemy_damage"),LowestVictimHealth<HealthBefore);
        else if(Ability==EHeliosBioAbility::ZhongXiaoKuiBlade)
        {auto* Target=Cast<AHeliosBioNPC>(Mode->ParticipantActor(VictimId));Check(TEXT("punish_real_freeze"),MaxFrozenSeconds>0);Check(TEXT("punish_actual_knockback"),Target&&FVector::Dist2D(VictimBefore,Target->GetActorLocation())>40);Check(TEXT("frozen_knockback_preserves_gravity_and_lands"),Target&&bVictimAirborne&&AirMotionSamples>=2&&Target->GetCharacterMovement()->IsMovingOnGround());if(Target)Target->bQAFreeze=true;}
        else if(Ability==EHeliosBioAbility::MechanicalFire)
        {for(auto Item:Mode->Turrets)if(Item&&Item->GetCasterId()==0)Turret=Item;Check(TEXT("mechanical_F_real_source_turret"),Turret.IsValid()&&Turret->IsActiveTurret());}
        else {Finish(TEXT("Role fixture has no complete effect-specific acceptance for this primary/secondary skill yet"));return;}
        CheckPose(TEXT("skill_FP"),true);Check(TEXT("cooldown_counts_down_in_real_world"),Mode->Model.FindParticipant(0)->SkillCooldownRemaining<CooldownBefore);
        if(TestRole==TEXT("BioSunWukong"))
        {
            auto* Target=Cast<ACharacter>(Mode->ParticipantActor(VictimId));
            Check(TEXT("player_Wukong_original_area_strike_and_native_knockback"),Mode->TotalAreaSkillHits>WukongPlayerAreaHitsBefore&&LowestVictimHealth<HealthBefore&&Target&&FVector::Dist2D(VictimBefore,Target->GetActorLocation())>40&&bVictimAirborne&&AirMotionSamples>=2&&Target->GetCharacterMovement()->IsMovingOnGround());
            Check(TEXT("player_Wukong_actual_movement_component_uses_DA_buff"),bWukongPlayerSpeedApplied);
            Next=Now;Stage=180;break;
        }
        // Normal world ticks must reach readiness; direct oversized Model.Tick cannot establish runtime acceptance.
        Next=Now+FMath::Max(.1f,Mode->Model.FindParticipant(0)->SkillCooldownRemaining+.1f);Stage=11;break;
    }
    case 11:
        Check(TEXT("cooldown_becomes_ready_on_world_ticks"),Mode->Model.FindParticipant(0)->SkillCooldownRemaining==0);PlaceCombatTarget();
        if(Definition->Ability==EHeliosBioAbility::ZhongXiaoKuiBlade){BeginNPCValidation(Now,110);break;}
        if(Definition->Ability==EHeliosBioAbility::MechanicalFire){BeginNPCValidation(Now,200);break;}
        if(TestRole==TEXT("BioRedeemerHunter")){BeginNPCValidation(Now,250);break;}
        if(!bAbyss)BeginNPCValidation(Now,12);
        else Finish(TEXT("Abyss effects reached, but full energy/buff/collision, red-box denial and respawn still require dedicated acceptance"));break;
    case 12:
        Check(TEXT("NPC_death_native_clip"),Hunter&&Hunter->GetMesh()->GetSingleNodeInstance()&&Hunter->GetMesh()->GetSingleNodeInstance()->GetCurrentAsset()==Definition->TPDeath.LoadSynchronous());
        Capture(TEXT("role_05_death"));Next=Now+FMath::Max(.1f,Definition->TPDeath.LoadSynchronous()->GetPlayLength()*.7f);Stage=13;break;
    case 13:
        CheckPose(TEXT("death_TP"),true);Check(TEXT("NPC_dead_collision_disabled"),Hunter&&Hunter->GetCapsuleComponent()->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
        Check(TEXT("NPC_dead_is_permanently_eliminated"),Mode->Model.FindParticipant(HunterId)->bPermanentlyDead);
        // Restore visible, physical live targets before evaluating the spectator handlers.
        for(auto Bot:Mode->Bots)if(Bot&&Bot->ParticipantId!=HunterId){Bot->SetActorTickEnabled(true);Bot->bQAFreeze=true;Bot->SetActorLocation(Mode->SpawnLocation(Bot->ParticipantId),false,nullptr,ETeleportType::TeleportPhysics);Bot->SetActorHiddenInGame(false);Bot->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Bot->GetCharacterMovement()->SetMovementMode(MOVE_Walking);}
        if(TestRole==TEXT("BioSunWukong")){Mode->Player->UseBioSkill();Check(TEXT("player_Wukong_real_F_active_before_actual_death"),Mode->Model.FindParticipant(0)->SkillRemaining>0);}
        Check(TEXT("player_real_lethal_damage"),Mode->DamageParticipant(0,VictimId,1000000,false));
        if(TestRole==TEXT("BioSunWukong")){const auto* P=Mode->Model.FindParticipant(0);Check(TEXT("player_Wukong_actual_death_clears_active_buff"),!P->bAlive&&P->SkillRemaining==0&&P->SkillSpeedMultiplier==1.f&&P->SkillDamageMultiplier==1.f);}
        Next=Now+.2;Stage=14;break;
    case 14:
    {
        if(bManualSpectator)
        {
            Check(TEXT("dead_player_collision_disabled"),Mode->Player->GetCapsuleComponent()->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
            bManualSpectatorPrepared=!Mode->Model.FindParticipant(0)->bAlive;ManualSpectatorRound=Mode->RoundSerial;ManualSpectatorStartedWall=FPlatformTime::Seconds();
            Mode->Model.Config.PreparationSeconds=20.f;
            auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("actual_dead_player_manual_spectator_fixture_ready"));Row->SetNumberField(TEXT("actual_round"),Mode->RoundSerial);Row->SetNumberField(TEXT("actual_spectated_participant"),Mode->Player->GetSpectatedParticipantId());Row->SetBoolField(TEXT("actual_player_dead"),!Mode->Model.FindParticipant(0)->bAlive);Row->SetBoolField(TEXT("automatic_SpectateNext_Previous_Restart_skipped"),true);Row->SetStringField(TEXT("scope"),TEXT("Explicit prepared death fixture. Only the external operator supplies actual Left/Right/F5/F9; independent NativeInputQA records production state. This branch neither injects controls nor grants automatic spectator/input acceptance."));Observations.Add(MakeShared<FJsonValueObject>(Row));
            Mode->ShowNotice(TEXT("死亡观战手动验收：←/→切换目标，F5重开，F9保存退出"),15.f);Stage=16;return;
        }
        Mode->Player->SpectateNext();FMinimalViewInfo Camera;Mode->Player->CalcCamera(Dt,Camera);const int32 First=Mode->Player->GetSpectatedParticipantId();const auto* Watched=Mode->Model.FindParticipant(First);
        Check(TEXT("dead_player_collision_disabled"),Mode->Player->GetCapsuleComponent()->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
        Check(TEXT("dead_player_spectates_living_actor"),First>0&&Watched&&Watched->bAlive&&!Camera.bUseFirstPersonParameters);
        Mode->Player->SpectateNext();const int32 Second=Mode->Player->GetSpectatedParticipantId();Mode->Player->SpectatePrevious();
        Check(TEXT("left_right_spectator_handlers_cycle"),Second>0&&Second!=First&&Mode->Player->GetSpectatedParticipantId()==First);
        Check(TEXT("last_hunter_death_finishes_round"),Mode->Model.GetPhase()==EHeliosBioPhase::RoundOver);
        Mode->Model.Config.PreparationSeconds=20.f;Mode->Player->RequestBioRestart();for(auto Bot:Mode->Bots)if(Bot){Bot->SetActorTickEnabled(true);Bot->bQAFreeze=true;}
        Next=Now+3;Stage=15;break;
    }
    case 15:
        Check(TEXT("F5_handler_restores_player_and_clears_role"),Mode->Model.FindParticipant(0)->bAlive&&Mode->Model.FindParticipant(0)->HunterRoleId.IsNone()&&Mode->Player->GetSpectatedParticipantId()==INDEX_NONE);
        Check(TEXT("restart_restores_player_collision"),Mode->Player->GetCapsuleComponent()->GetCollisionEnabled()==ECollisionEnabled::QueryAndPhysics);
        Check(TEXT("restart_restores_NPC_collision"),Hunter&&Hunter->GetCapsuleComponent()->GetCollisionEnabled()==ECollisionEnabled::QueryAndPhysics);SampleHumanBaseline(TEXT("round_restart"));Finish();break;
    default:Finish(TEXT("Invalid fixture stage"));break;
    }
}
