#include "QA/HeliosBioSupplyPresentationQA.h"
#include "Bio/HeliosBioSupplyWeapon.h"
#include "Bio/HeliosBioSupplyWeaponDefinition.h"
#include "Bio/HeliosBioSupplyProjectileMovement.h"
#include "Bio/HeliosBioRoundInventoryComponent.h"
#include "Bio/HeliosBioHotelGameMode.h"
#include "Bio/HeliosBioNPC.h"
#include "Bio/HeliosBioPlayer.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimationAsset.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialRenderProxy.h"
#include "MaterialShared.h"
#include "RenderingThread.h"
#include "ActiveSound.h"
#include "AudioDevice.h"
#include "Audio.h"
#include "Sound/SoundBase.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "SkeletalRenderPublic.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeLock.h"
#include "UObject/UnrealType.h"

namespace
{
constexpr int32 MaximumRecords=8192;
bool Enabled()
{
    return FParse::Param(FCommandLine::Get(),TEXT("BioSupplyWeaponQA"))||
        FParse::Param(FCommandLine::Get(),TEXT("BioSupplyObserve"));
}
struct FObservationRecord {uint64 Serial=0;uint32 World=0;FString JSON;};
struct FObservationStore {FCriticalSection Lock;uint64 Serial=0;TArray<FObservationRecord> Rows;};
FObservationStore& Store()
{
    // Process lifetime avoids static teardown racing the last audio command.
    static auto* Value=new FObservationStore;return *Value;
}
FString JSON(const TSharedRef<FJsonObject>& Value)
{FString Text;FJsonSerializer::Serialize(Value,TJsonWriterFactory<>::Create(&Text));return Text;}
void Record(uint32 World,const TSharedRef<FJsonObject>& Value)
{
    auto& State=Store();FScopeLock Lock(&State.Lock);const uint64 Serial=++State.Serial;
    Value->SetNumberField(TEXT("serial"),double(Serial));Value->SetNumberField(TEXT("world_id"),World);
    Value->SetNumberField(TEXT("wall_seconds"),FPlatformTime::Seconds());
    State.Rows.Add({Serial,World,JSON(Value)});
    if(State.Rows.Num()>MaximumRecords)State.Rows.RemoveAt(0,State.Rows.Num()-MaximumRecords,EAllowShrinking::No);
}
TSharedRef<FJsonObject> Animation(USkeletalMeshComponent* Mesh)
{
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("component"),GetPathNameSafe(Mesh));
    if(Mesh)
    {
        Row->SetStringField(TEXT("mesh"),GetPathNameSafe(Mesh->GetSkeletalMeshAsset()));
        if(auto* Instance=Mesh->GetSingleNodeInstance())
        {
            Row->SetStringField(TEXT("actual_clip"),GetPathNameSafe(Instance->GetCurrentAsset()));
            Row->SetNumberField(TEXT("actual_source_seconds"),Instance->GetCurrentTime());
            Row->SetNumberField(TEXT("actual_play_rate"),Instance->GetPlayRate());
            Row->SetBoolField(TEXT("actual_playing"),Instance->IsPlaying());
        }
    }
    return Row;
}
const UHeliosBioSupplyWeaponDefinition* Definition(const AShooterWeapon* Gun)
{
    const auto* Field=Gun?FindFProperty<FObjectPropertyBase>(Gun->GetClass(),TEXT("ActiveDefinition")):nullptr;
    return Field?Cast<UHeliosBioSupplyWeaponDefinition>(Field->GetObjectPropertyValue_InContainer(Gun)):nullptr;
}
void QueueMaterialProbe(UWorld* World,UPrimitiveComponent* Component,UMaterialInterface* Material,int32 Slot)
{
    if(!Enabled()||!World||!Component||!Material)return;
    const uint32 WorldID=World->GetUniqueID();const auto Feature=World->GetFeatureLevel();
    FMaterialRenderProxy* Proxy=Material->GetRenderProxy();
    const FString ComponentPath=Component->GetPathName(),MaterialPath=Material->GetPathName();
    const double RequestedAt=FPlatformTime::Seconds();
    ENQUEUE_RENDER_COMMAND(BioSupplyReadOriginalMaterialProxy)([WorldID,Feature,Proxy,ComponentPath,MaterialPath,Slot,RequestedAt](FRHICommandListImmediate&)
    {
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("actual_material_render_proxy"));
        Row->SetStringField(TEXT("component"),ComponentPath);Row->SetStringField(TEXT("material"),MaterialPath);Row->SetNumberField(TEXT("slot"),Slot);Row->SetNumberField(TEXT("requested_wall_seconds"),RequestedAt);
        Row->SetBoolField(TEXT("render_proxy_exists"),Proxy!=nullptr);
        const FMaterialRenderProxy* Selected=Proxy;const FMaterial* Original=Proxy?Proxy->GetMaterialNoFallback(Feature):nullptr;
        const FMaterial* Resolved=Original;int32 Fallbacks=0;
        // Read the same fallback chain as GetMaterialWithFallback, without its
        // editor SubmitCompileJobs side effect or any rendering mutation.
        while(Selected&&(!Resolved||!Resolved->IsRenderingThreadShaderMapComplete())&&Fallbacks<16)
        {Selected=Selected->GetFallback(Feature);Resolved=Selected?Selected->GetMaterialNoFallback(Feature):nullptr;++Fallbacks;}
        Row->SetBoolField(TEXT("original_shader_map_complete"),Original&&Original->IsRenderingThreadShaderMapComplete());
        Row->SetBoolField(TEXT("fallback_required"),Fallbacks>0);
        Row->SetStringField(TEXT("original_render_asset_name"),Original?Original->GetAssetName():TEXT(""));
        Row->SetStringField(TEXT("resolved_render_asset_name"),Resolved?Resolved->GetAssetName():TEXT(""));
        Row->SetBoolField(TEXT("resolved_default_material"),Resolved&&Resolved->IsDefaultMaterial());
        Row->SetStringField(TEXT("scope"),TEXT("Live render-thread material proxy/fallback state; independent on-screen timing or pixels must establish visibility."));Record(WorldID,Row);
    });
}
TSharedRef<FJsonObject> Primitive(UWorld* World,UPrimitiveComponent* Component)
{
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("component"),Component->GetPathName());
    Row->SetBoolField(TEXT("registered"),Component->IsRegistered());Row->SetBoolField(TEXT("render_state_created"),Component->IsRenderStateCreated());
    Row->SetBoolField(TEXT("scene_proxy_exists"),Component->GetSceneProxy()!=nullptr);
    Row->SetBoolField(TEXT("visible"),Component->IsVisible()&&!Component->bHiddenInGame&&!Component->GetOwner()->IsHidden());
    Row->SetNumberField(TEXT("last_render_seconds"),Component->GetLastRenderTime());Row->SetNumberField(TEXT("last_on_screen_seconds"),Component->GetLastRenderTimeOnScreen());
    Row->SetBoolField(TEXT("recently_rendered"),Component->WasRecentlyRendered(.2f));Row->SetStringField(TEXT("world_location"),Component->GetComponentLocation().ToString());
    if(auto* Static=Cast<UStaticMeshComponent>(Component))Row->SetStringField(TEXT("actual_static_mesh"),GetPathNameSafe(Static->GetStaticMesh()));
    if(auto* Ribbon=Cast<UProceduralMeshComponent>(Component))Row->SetNumberField(TEXT("actual_procedural_sections"),Ribbon->GetNumSections());
    TArray<TSharedPtr<FJsonValue>> Materials;
    for(int32 I=0;I<Component->GetNumMaterials();++I)
    {
        auto* Material=Component->GetMaterial(I);auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("actual_material"),GetPathNameSafe(Material));
        if(Material){Item->SetStringField(TEXT("actual_base_material"),GetPathNameSafe(Material->GetMaterial()));if(auto* Instance=Cast<UMaterialInstance>(Material))Item->SetStringField(TEXT("actual_parent"),GetPathNameSafe(Instance->Parent));QueueMaterialProbe(World,Component,Material,I);}
        Materials.Add(MakeShared<FJsonValueObject>(Item));
    }
    Row->SetArrayField(TEXT("actual_materials"),Materials);return Row;
}
}

bool UHeliosBioSupplyAudioQA::ShouldCreateSubsystem(UObject* Outer) const
{return Enabled()&&Super::ShouldCreateSubsystem(Outer);}
void UHeliosBioSupplyAudioQA::ObserveSound(const TCHAR* Kind,const FActiveSound& Sound)
{
    auto* Asset=Sound.GetSound();if(!Asset)return;const FString Path=Asset->GetPathName();
    if(!Path.Contains(TEXT("/CFMBioPyramidFloating/SupplyWeapons/"))||!Path.Contains(TEXT("/Audio/")))return;
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),Kind);Row->SetStringField(TEXT("actual_sound"),Path);
    Row->SetNumberField(TEXT("play_order"),Sound.GetPlayOrder());Row->SetNumberField(TEXT("audio_component_id"),double(Sound.GetAudioComponentID()));
    Row->SetNumberField(TEXT("owner_id"),Sound.GetOwnerID());Row->SetStringField(TEXT("engine_owner"),Sound.GetOwnerName());
    Row->SetNumberField(TEXT("playback_seconds"),Sound.PlaybackTime);Row->SetBoolField(TEXT("paused"),Sound.bIsPaused);
    Row->SetNumberField(TEXT("wave_instances"),Sound.GetWaveInstances().Num());int32 Sources=0;TArray<TSharedPtr<FJsonValue>> Waves;
    auto Device=GetAudioDeviceHandle();
    for(const auto& Pair:Sound.GetWaveInstances())if(auto* Wave=Pair.Value)
    {
        auto* Source=Device.IsValid()?Device->GetSoundSource(Wave):nullptr;auto W=MakeShared<FJsonObject>();W->SetBoolField(TEXT("real_source_exists"),Source!=nullptr);
        if(Source){++Sources;W->SetNumberField(TEXT("playback_fraction"),Source->GetPlaybackPercent());W->SetBoolField(TEXT("source_paused"),Source->IsPaused());}
        Waves.Add(MakeShared<FJsonValueObject>(W));
    }
    Row->SetNumberField(TEXT("real_sources"),Sources);Row->SetArrayField(TEXT("actual_waves"),Waves);Record(Sound.GetWorldID(),Row);
}
void UHeliosBioSupplyAudioQA::NotifyActiveSoundCreated(FActiveSound& Sound){ObserveSound(TEXT("engine_active_created"),Sound);}
void UHeliosBioSupplyAudioQA::NotifyActiveSoundDeleting(const FActiveSound& Sound){ObserveSound(TEXT("engine_active_deleted"),Sound);}
void UHeliosBioSupplyAudioQA::NotifyVirtualizedSoundCreated(FActiveSound& Sound){ObserveSound(TEXT("engine_virtual_created"),Sound);}
void UHeliosBioSupplyAudioQA::NotifyVirtualizedSoundDeleting(const FActiveSound& Sound){ObserveSound(TEXT("engine_virtual_deleted"),Sound);}
void UHeliosBioSupplyAudioQA::Update()
{
    const double Wall=FPlatformTime::Seconds();if(Wall<NextSampleWall)return;NextSampleWall=Wall+.05;
    auto Device=GetAudioDeviceHandle();if(Device.IsValid())for(const auto* Active:Device->GetActiveSounds())if(Active)ObserveSound(TEXT("engine_active_sample"),*Active);
}
void UHeliosBioSupplyPresentationQA::RecordSoundRequest(AActor* Source,USoundBase* Sound,FName Action,float ExpectedNativeSeconds,float ActualNativeSeconds,bool FirstPerson,const TCHAR* Kind)
{
    if(!Enabled()||!Source||!Source->GetWorld()||!Sound)return;
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("native_PlaySound_request"));Row->SetStringField(TEXT("request_kind"),Kind);
    Row->SetStringField(TEXT("source_actor"),Source->GetPathName());Row->SetStringField(TEXT("source_owner"),GetPathNameSafe(Source->GetOwner()));
    Row->SetStringField(TEXT("actual_sound"),Sound->GetPathName());Row->SetStringField(TEXT("action"),Action.ToString());Row->SetBoolField(TEXT("first_person"),FirstPerson);
    Row->SetNumberField(TEXT("expected_native_seconds"),ExpectedNativeSeconds);Row->SetNumberField(TEXT("actual_cue_clock_seconds"),ActualNativeSeconds);Row->SetNumberField(TEXT("world_seconds"),Source->GetWorld()->GetTimeSeconds());
    if(auto* Gun=Cast<AHeliosBioSupplyWeapon>(Source))Row->SetObjectField(TEXT("actual_native_driver"),Animation(Gun->GetFirstPersonMesh()));
    Row->SetStringField(TEXT("scope"),TEXT("Observed immediately before real PlaySoundAtLocation. Engine acceptance/source events are independent; no audible output claim."));Record(Source->GetWorld()->GetUniqueID(),Row);
}
FString UHeliosBioSupplyPresentationQA::ReadAudioEvents(const UObject* WorldContextObject,int64 AfterSerial)
{
    UWorld* World=WorldContextObject?WorldContextObject->GetWorld():nullptr;auto Result=MakeShared<FJsonObject>();Result->SetBoolField(TEXT("enabled"),Enabled());
    Result->SetBoolField(TEXT("actual_audio_device_present"),World&&World->GetAudioDevice().IsValid());TArray<TSharedPtr<FJsonValue>> Events;
    auto& State=Store();FScopeLock Lock(&State.Lock);Result->SetNumberField(TEXT("lastSerial"),double(State.Serial));
    Result->SetNumberField(TEXT("firstRetainedSerial"),State.Rows.IsEmpty()?0:double(State.Rows[0].Serial));
    Result->SetBoolField(TEXT("history_truncated_before_cursor"),!State.Rows.IsEmpty()&&uint64(FMath::Max<int64>(0,AfterSerial))+1<State.Rows[0].Serial);
    for(const auto& Row:State.Rows)if(Row.Serial>uint64(FMath::Max<int64>(0,AfterSerial))&&(!World||Row.World==World->GetUniqueID()))
    {TSharedPtr<FJsonObject> Item;if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Row.JSON),Item)&&Item)Events.Add(MakeShared<FJsonValueObject>(Item));}
    Result->SetArrayField(TEXT("events"),Events);return JSON(Result);
}
namespace
{
TSharedRef<FJsonObject> NativeTPSkinLocator(UHeliosCharacterPresentationComponent* Presentation,const UHeliosBioSupplyWeaponDefinition* Def,const FVector& ActualMuzzle)
{
    auto Row=MakeShared<FJsonObject>();Row->SetBoolField(TEXT("checked"),false);
    auto* Body=Presentation?Presentation->GetArmsMesh():nullptr;auto* Gun=Presentation?Presentation->GetWeaponMesh():nullptr;
    if(!Def||!Body||!Gun||Presentation->GetPresentedWeaponId()!=Def->WeaponId||!Body->GetSkeletalMeshAsset()||!Gun->GetSkeletalMeshAsset())return Row;
    const auto& Ref=Gun->GetSkeletalMeshAsset()->GetRefSkeleton();const int32 Bone=Ref.FindBoneIndex(Def->ThirdPersonMuzzleBone);
    if(Bone==INDEX_NONE||!Body->DoesSocketExist(Def->ThirdPersonMuzzleBone))return Row;
    FTransform GunReference=FTransform::Identity;
    for(int32 Index=Bone;Index!=INDEX_NONE;Index=Ref.GetParentIndex(Index))GunReference=GunReference*Ref.GetRefBonePose()[Index];
    const FVector ReferenceScale=GunReference.GetScale3D();if(ReferenceScale.ContainsNaN()||ReferenceScale.GetMin()<=SMALL_NUMBER)return Row;
    const auto* Render=Gun->GetSkeletalMeshRenderData();const auto* Weights=Gun->GetSkinWeightBuffer(0);
    if(!Render||!Render->LODRenderData.IsValidIndex(0)||!Weights||!Weights->GetDataVertexBuffer()->GetWeightData())return Row;
    const auto& LOD=Render->LODRenderData[0];const auto& Positions=LOD.StaticVertexBuffers.PositionVertexBuffer;
    // The authored locator can be outside the barrel surface. Preserve its
    // measured reference offset to a real native gun vertex, then carry that
    // same offset through the actual bone pose and compare with the CPU skin.
    const FVector ReferenceMuzzle=GunReference.TransformPosition(Def->ThirdPersonMuzzleRelativeToBone.GetTranslation()/ReferenceScale);
    float Closest=MAX_flt;int32 ClosestIndex=INDEX_NONE;
    for(uint32 Index=0;Index<Positions.GetNumVertices();++Index)
    {
        const float Distance=FVector::DistSquared(ReferenceMuzzle,FVector(Positions.VertexPosition(Index)));
        if(Distance<Closest){Closest=Distance;ClosestIndex=int32(Index);}
    }
    if(ClosestIndex==INDEX_NONE)return Row;
    bool RigidAnchor=false;
    for(const auto& Section:LOD.RenderSections)if(uint32(ClosestIndex)>=Section.BaseVertexIndex&&uint32(ClosestIndex)<Section.BaseVertexIndex+Section.NumVertices)
    {
        RigidAnchor=true;bool Weighted=false;
        for(uint32 Influence=0;Influence<Weights->GetMaxBoneInfluences();++Influence)
        {
            if(!Weights->GetBoneWeight(ClosestIndex,Influence))continue;Weighted=true;
            const int32 Local=Weights->GetBoneIndex(ClosestIndex,Influence);
            RigidAnchor&=Section.BoneMap.IsValidIndex(Local)&&Section.BoneMap[Local]==Bone;
        }
        RigidAnchor&=Weighted;break;
    }
    TArray<FFinalSkinVertex> Skin;Gun->GetCPUSkinnedVertices(Skin,0);if(!Skin.IsValidIndex(ClosestIndex))return Row;
    const FVector ReferenceVertex(Positions.VertexPosition(ClosestIndex));
    const FVector ActualVertex=Gun->GetComponentTransform().TransformPosition(FVector(Skin[ClosestIndex].Position));
    const FTransform AnimatedAnchor=Body->GetSocketTransform(Def->ThirdPersonMuzzleBone);
    const FVector ActualReferenceOffset=AnimatedAnchor.TransformVector(GunReference.InverseTransformVector(ReferenceMuzzle-ReferenceVertex));
    const FVector SourceSkinMuzzle=ActualVertex+ActualReferenceOffset;
    const FVector OldUncompensated=AnimatedAnchor.TransformPosition(Def->ThirdPersonMuzzleRelativeToBone.GetTranslation());
    const float Error=FVector::Dist(ActualMuzzle,SourceSkinMuzzle);
    Row->SetBoolField(TEXT("checked"),true);Row->SetStringField(TEXT("actual_presented_weapon"),Presentation->GetPresentedWeaponId().ToString());Row->SetStringField(TEXT("actual_matching_gun"),Gun->GetSkeletalMeshAsset()->GetPathName());Row->SetStringField(TEXT("actual_gun_leader_pose"),GetPathNameSafe(Gun->LeaderPoseComponent.Get()));Row->SetNumberField(TEXT("native_reference_vertex_index"),ClosestIndex);Row->SetBoolField(TEXT("actual_reference_vertex_rigid_on_muzzle_anchor"),RigidAnchor);
    Row->SetStringField(TEXT("anchor_reference_scale"),ReferenceScale.ToString());Row->SetStringField(TEXT("actual_animated_anchor_world_scale"),AnimatedAnchor.GetScale3D().ToString());Row->SetStringField(TEXT("actual_animated_reference_scale_ratio_including_owner"),(AnimatedAnchor.GetScale3D()/ReferenceScale).ToString());
    Row->SetStringField(TEXT("actual_skinned_native_reference_vertex_world"),ActualVertex.ToString());Row->SetStringField(TEXT("source_locator_carried_from_actual_skin_world"),SourceSkinMuzzle.ToString());Row->SetNumberField(TEXT("source_reference_locator_to_native_vertex_cm"),FMath::Sqrt(Closest));Row->SetNumberField(TEXT("source_offset_after_actual_pose_cm"),ActualReferenceOffset.Size());Row->SetNumberField(TEXT("actual_muzzle_to_current_skinned_native_vertex_cm"),FVector::Dist(ActualMuzzle,ActualVertex));Row->SetNumberField(TEXT("actual_muzzle_vs_source_skin_point_error_cm"),Error);Row->SetNumberField(TEXT("old_uncompensated_locator_vs_source_skin_point_error_cm"),FVector::Dist(OldUncompensated,SourceSkinMuzzle));Row->SetBoolField(TEXT("actual_skin_locator_bridge_within_0_05cm"),RigidAnchor&&Gun->LeaderPoseComponent.Get()==Body&&Error<=.05f);
    Row->SetStringField(TEXT("scope"),TEXT("Read-only actual matching TP gun CPU skin versus its original locator's measured offset to the nearest persisted native gun vertex. Locator is not assumed to lie on a vertex. Saved reference-unit scale alone is removed; actual pose mirroring and owner/component scale remain. The old unnormalized result is reported as a counterexample, never used to launch."));return Row;
}
}
FString UHeliosBioSupplyPresentationQA::SnapshotParticipant(AActor* Participant,AActor* Target)
{
    auto Root=MakeShared<FJsonObject>();Root->SetStringField(TEXT("participant"),GetPathNameSafe(Participant));Root->SetStringField(TEXT("target"),GetPathNameSafe(Target));
    UWorld* World=Participant?Participant->GetWorld():nullptr;if(!World)return JSON(Root);
    Root->SetNumberField(TEXT("world_seconds"),World->GetTimeSeconds());Root->SetNumberField(TEXT("wall_seconds"),FPlatformTime::Seconds());
    auto* Inventory=Participant->FindComponentByClass<UHeliosBioRoundInventoryComponent>();
    auto* Mode=AHeliosBioHotelGameMode::Find(World);const int32 Id=Mode?Mode->ParticipantId(Participant):INDEX_NONE;
    const auto* State=Mode?Mode->Model.FindParticipant(Id):nullptr;const auto* Victim=Mode&&Target?Mode->Model.FindParticipant(Mode->ParticipantId(Target)):nullptr;
    Root->SetNumberField(TEXT("actual_selected_slot"),Inventory?Inventory->GetActiveSlot():INDEX_NONE);Root->SetNumberField(TEXT("participant_id"),Id);
    if(State){Root->SetNumberField(TEXT("actual_team"),int32(State->Team));Root->SetBoolField(TEXT("alive"),State->bAlive);Root->SetNumberField(TEXT("frozen_seconds"),State->FrozenSeconds);}
    FVector Eyes=Participant->GetActorLocation();FRotator Rotation;Participant->GetActorEyesViewPoint(Eyes,Rotation);
    FVector TargetAt=Target?Target->GetActorLocation()+FVector(0,0,25):Eyes;
    Root->SetNumberField(TEXT("target_distance_cm"),Target?FVector::Dist(Participant->GetActorLocation(),Target->GetActorLocation()):-1);
    if(Victim)Root->SetNumberField(TEXT("target_frozen_seconds"),Victim->FrozenSeconds);
    int32 Cluster=0;if(Mode&&Target)for(const auto& P:Mode->Model.GetParticipants())if(P.bAlive&&Mode->AreOpponents(Id,P.Id))if(auto* A=Mode->ParticipantActor(P.Id);A&&FVector::DistSquared(A->GetActorLocation(),Target->GetActorLocation())<FMath::Square(550.f))++Cluster;
    Root->SetNumberField(TEXT("actual_opponents_within_550cm_of_target"),Cluster);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BioSupplyQASelectionFacts),false,Participant);
    if(Inventory)for(int32 Slot=0;Slot<5;++Slot)if(auto* Gun=Inventory->GetWeapon(Slot))Query.AddIgnoredActor(Gun);
    FHitResult EyeHit;const bool EyeBlocked=Target&&World->LineTraceSingleByChannel(EyeHit,Eyes,TargetAt,ECC_Visibility,Query)&&EyeHit.GetActor()!=Target;
    Root->SetBoolField(TEXT("eyes_target_blocked"),EyeBlocked);Root->SetStringField(TEXT("eyes_target_hit"),GetPathNameSafe(EyeHit.GetActor()));
    TArray<TSharedPtr<FJsonValue>> Slots;
    if(Inventory)for(int32 Slot=0;Slot<5;++Slot)
    {
        auto Row=MakeShared<FJsonObject>();auto* Gun=Inventory->GetWeapon(Slot);auto* Native=Cast<AHeliosBioSupplyWeapon>(Gun);const auto* Def=Definition(Gun);
        Row->SetNumberField(TEXT("slot"),Slot);Row->SetStringField(TEXT("weapon_id"),Inventory->GetWeaponId(Slot).ToString());Row->SetStringField(TEXT("actual_instance"),GetPathNameSafe(Gun));
        Row->SetNumberField(TEXT("actual_magazine"),Gun?Gun->GetBulletCount():0);Row->SetNumberField(TEXT("actual_reserve"),Inventory->GetReserve(Slot));
        if(Native)
        {
            Row->SetNumberField(TEXT("committed_shots"),Native->GetNativeShotCount());Row->SetNumberField(TEXT("committed_hits"),Native->GetNativeHitCount());
            Row->SetBoolField(TEXT("actual_action_busy"),Native->IsNativeActionBusy());Row->SetBoolField(TEXT("actual_throw_busy"),Native->IsNativeThrowBusy());Row->SetObjectField(TEXT("actual_native_driver"),Animation(Native->GetFirstPersonMesh()));
            FVector Muzzle;const bool HasMuzzle=Native->GetNativeMuzzle(Muzzle);Row->SetBoolField(TEXT("actual_source_muzzle_resolved"),HasMuzzle);
            if(HasMuzzle)
            {
                Row->SetStringField(TEXT("actual_source_muzzle"),Muzzle.ToString());FHitResult Near,Hit;
                if(auto* Pawn=Cast<APawn>(Participant);Pawn&&!Pawn->IsPlayerControlled())Row->SetObjectField(TEXT("actual_TP_native_skin_locator_bridge"),NativeTPSkinLocator(Participant->FindComponentByClass<UHeliosCharacterPresentationComponent>(),Def,Muzzle));
                Row->SetBoolField(TEXT("eyes_to_source_muzzle_blocked"),World->SweepSingleByChannel(Near,Eyes,Muzzle,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(3.f),Query));Row->SetStringField(TEXT("eyes_to_source_muzzle_hit"),GetPathNameSafe(Near.GetActor()));
                Row->SetBoolField(TEXT("source_muzzle_to_target_blocked"),Target&&World->LineTraceSingleByChannel(Hit,Muzzle,TargetAt,ECC_Visibility,Query)&&Hit.GetActor()!=Target);Row->SetStringField(TEXT("source_muzzle_to_target_hit"),GetPathNameSafe(Hit.GetActor()));
                if(Def&&Target)
                {
                    Row->SetNumberField(TEXT("actual_source_item_id"),Def->OriginalItemId);Row->SetBoolField(TEXT("production_can_reach_target"),Native->CanReachNativeTarget(TargetAt));
                    if(Def->FireType!=EHeliosBioSupplyFireType::Hitscan)
                    {
                        const auto Assessment=HeliosBioSupplyBallistics::AssessNativeLaunch(Muzzle,TargetAt,Def->ProjectileSpeed,Def->MaximumProjectileSpeed,Def->ProjectileAcceleration,World->GetGravityZ()*Def->ProjectileGravityScale,Def->ProjectileLifetime);
                        Row->SetBoolField(TEXT("collision_free_numeric_reachable"),Assessment.bReachable);Row->SetNumberField(TEXT("numeric_closest_miss_cm"),Assessment.ClosestDistanceCm);Row->SetNumberField(TEXT("numeric_score"),Assessment.ReachabilityScore);Row->SetNumberField(TEXT("numeric_flight_seconds"),Assessment.FlightSeconds);
                    }
                }
            }
        }
        Slots.Add(MakeShared<FJsonValueObject>(Row));
    }
    Root->SetArrayField(TEXT("actual_inventory_slots"),Slots);
    if(auto* Presentation=Participant->FindComponentByClass<UHeliosCharacterPresentationComponent>())
    {Root->SetObjectField(TEXT("actual_selected_character_skin"),Animation(Presentation->GetArmsMesh()));Root->SetObjectField(TEXT("actual_tp_weapon"),Animation(Presentation->GetWeaponMesh()));}
    if(auto* NPC=Cast<AHeliosBioNPC>(Participant)){Root->SetStringField(TEXT("actual_movement_intent"),NPC->GetMovementIntent().ToString());Root->SetNumberField(TEXT("actual_reload_remaining"),NPC->GetReloadRemaining());Root->SetStringField(TEXT("actual_AI_target"),GetPathNameSafe(NPC->GetDesiredCombatTarget()));}
    TArray<TSharedPtr<FJsonValue>> Effects;
    for(TActorIterator<AActor> It(World);It;++It)
    {
        auto* Actor=*It;if(!Actor->IsOwnedBy(Participant)||(!Actor->IsA<AHeliosSSXEffect>()&&!Actor->IsA<AHeliosBioSupplyTracer>()&&!Actor->IsA<AHeliosBioSupplyProjectile>()))continue;
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("actual_actor"),Actor->GetPathName());Row->SetStringField(TEXT("actual_class"),Actor->GetClass()->GetPathName());Row->SetStringField(TEXT("actual_owner"),GetPathNameSafe(Actor->GetOwner()));
        if(auto* Effect=Cast<AHeliosSSXEffect>(Actor))Row->SetNumberField(TEXT("visible_native_tracks"),Effect->VisibleTrackCount());
        TInlineComponentArray<UPrimitiveComponent*> Components(Actor);TArray<TSharedPtr<FJsonValue>> Parts;
        for(auto* Component:Components)if(Component)Parts.Add(MakeShared<FJsonValueObject>(Primitive(World,Component)));
        Row->SetArrayField(TEXT("actual_primitives"),Parts);Effects.Add(MakeShared<FJsonValueObject>(Row));
    }
    Root->SetArrayField(TEXT("live_owned_native_effects"),Effects);Root->SetStringField(TEXT("scope"),TEXT("Observed live instances and render-thread probes, not an expected AI decision oracle. Render proxies and recent render timestamps do not alone prove visible pixel contribution or audible speaker output."));return JSON(Root);
}
