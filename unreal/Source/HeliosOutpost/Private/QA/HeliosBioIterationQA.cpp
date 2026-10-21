#include "QA/HeliosBioIterationQA.h"
#include "Bio/HeliosBioHotelGameMode.h"
#include "Bio/HeliosBioPlayer.h"
#include "Bio/HeliosBioNPC.h"
#include "Bio/HeliosBioSupply.h"
#include "Bio/HeliosBioSupplyWeaponDefinition.h"
#include "Bio/HeliosBioRoundInventoryComponent.h"
#include "Bio/HeliosBioMovingPlatform.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimationAsset.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "SkeletalRenderPublic.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialShared.h"
#include "Materials/MaterialRenderProxy.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PhysicsVolume.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
    TSharedRef<FJsonObject> WaterShaderEvidence(UMaterialInterface* Material,ERHIFeatureLevel::Type FeatureLevel)
    {
        auto Row=MakeShared<FJsonObject>();auto* Resource=Material&&Material->GetMaterial()?Material->GetMaterial()->GetMaterialResource(GMaxRHIShaderPlatform):nullptr;
        Row->SetStringField(TEXT("kind"),TEXT("actual_water_shader_and_render_proxy"));Row->SetNumberField(TEXT("actual_world_feature_level"),int32(FeatureLevel));Row->SetNumberField(TEXT("actual_shader_platform"),int32(GMaxRHIShaderPlatform));
        Row->SetBoolField(TEXT("game_thread_shader_map_exists"),Resource&&Resource->GetGameThreadShaderMap());Row->SetBoolField(TEXT("game_thread_shader_map_complete"),Resource&&Resource->IsGameThreadShaderMapComplete());Row->SetBoolField(TEXT("game_thread_shader_map_valid_for_rendering"),Resource&&Resource->HasValidGameThreadShaderMap());
        bool Finished=Resource&&Resource->IsGameThreadShaderMapComplete();
#if WITH_EDITOR
        Finished=Resource&&Resource->IsCompilationFinished();
#endif
        Row->SetBoolField(TEXT("actual_shader_compilation_finished"),Finished);TArray<TSharedPtr<FJsonValue>> Errors;
#if WITH_EDITOR
        if(Resource)for(const FString& Error:Resource->GetCompileErrors())Errors.Add(MakeShared<FJsonValueString>(Error));
#endif
        if(Resource)
        {
            Row->SetStringField(TEXT("resource_name"),Resource->GetFriendlyName());Row->SetNumberField(TEXT("resource_feature_level"),int32(Resource->GetFeatureLevel()));Row->SetNumberField(TEXT("resource_quality_level"),int32(Resource->GetQualityLevel()));Row->SetNumberField(TEXT("resource_shader_platform"),int32(Resource->GetShaderPlatform()));
            if(const auto* Map=Resource->GetGameThreadShaderMap()){Row->SetBoolField(TEXT("shader_map_compilation_finalized"),Map->IsCompilationFinalized());Row->SetBoolField(TEXT("shader_map_compiled_successfully"),Map->CompiledSuccessfully());if(Map->IsCompilationFinalized()){int32 Shaders=0,Pipelines=0;Map->CountNumShaders(Shaders,Pipelines);Row->SetNumberField(TEXT("shader_map_actual_shader_count"),Shaders);Row->SetNumberField(TEXT("shader_map_actual_pipeline_count"),Pipelines);}}
        }
        Row->SetArrayField(TEXT("actual_compile_errors"),Errors);
        struct FRenderEvidence { bool Original=false,Complete=false;FString Name; };
        auto Actual=MakeShared<FRenderEvidence,ESPMode::ThreadSafe>();const FMaterialRenderProxy* Proxy=Material?Material->GetRenderProxy():nullptr;
        if(Proxy)
        {
            ENQUEUE_RENDER_COMMAND(BioWaterActualRenderProxy)([Proxy,FeatureLevel,Actual](FRHICommandListImmediate&)
            {
                const FMaterialRenderProxy* Used=Proxy;const FMaterial& Selected=Proxy->GetMaterialWithFallback(FeatureLevel,Used);
                Actual->Original=Used==Proxy;Actual->Complete=Selected.IsRenderingThreadShaderMapComplete();Actual->Name=Selected.GetAssetName();
            });FlushRenderingCommands();
        }
        Row->SetBoolField(TEXT("render_thread_uses_original_proxy"),Actual->Original);Row->SetBoolField(TEXT("render_thread_shader_map_complete"),Actual->Complete);Row->SetStringField(TEXT("actual_render_material_name"),Actual->Name);return Row;
    }
    FString MeshPath(USkeletalMeshComponent* Mesh){return Mesh?GetPathNameSafe(Mesh->GetSkeletalMeshAsset()):FString();}
    // Camera location, clocks and round timers are deliberately excluded: these
    // fields describe the atomic gameplay transaction, not a frozen world frame.
    bool SameTransaction(const TSharedRef<FJsonObject>& A,const TSharedRef<FJsonObject>& B)
    {
        for(const TCHAR* Key:{TEXT("weapon"),TEXT("weapon_class"),TEXT("FP_mesh"),TEXT("TP_mesh"),TEXT("slot0_weapon_id"),TEXT("inventory")})
            if(A->GetStringField(Key)!=B->GetStringField(Key))return false;
        for(const TCHAR* Key:{TEXT("ammo"),TEXT("reserve"),TEXT("shots_counter"),TEXT("HP"),TEXT("max_HP"),TEXT("supplies_collected"),TEXT("blue_supplies_collected"),TEXT("zombie_supplies_collected"),TEXT("evolution_level"),TEXT("active_slot"),TEXT("total_supplies"),TEXT("total_damage_events"),TEXT("total_infections"),TEXT("occupied_slots")})
            if(A->GetNumberField(Key)!=B->GetNumberField(Key))return false;
        return A->GetBoolField(TEXT("box_available"))==B->GetBoolField(TEXT("box_available"));
    }
}
AHeliosBioIterationQA::AHeliosBioIterationQA()
{PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void AHeliosBioIterationQA::BeginPlay()
{
    Super::BeginPlay();Started=FPlatformTime::Seconds();StageAt=Started;
    FParse::Value(FCommandLine::Get(),TEXT("BioIterationQAOut="),Out);FParse::Value(FCommandLine::Get(),TEXT("BioExpectedMap="),ExpectedMap);
    bWaterMode=FParse::Param(FCommandLine::Get(),TEXT("BioIterationWaterQA"));bFootMode=FParse::Param(FCommandLine::Get(),TEXT("BioIterationFootQA"));bWaterLandingMode=FParse::Param(FCommandLine::Get(),TEXT("BioWaterLandingQA"));
    if(!Out.IsEmpty())IFileManager::Get().MakeDirectory(*Out,true);
}
void AHeliosBioIterationQA::Check(const FString& Name,bool Passed)
{
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("name"),Name);Row->SetBoolField(TEXT("passed"),Passed);Checks.Add(MakeShared<FJsonValueObject>(Row));
    if(!Passed)++Failures;UE_LOG(LogTemp,Display,TEXT("BIO_ITERATION_QA %s %s"),*Name,Passed?TEXT("PASS"):TEXT("FAIL"));
}
void AHeliosBioIterationQA::FreezeIsolation()
{
    if(!Mode)return;
    for(auto Bot:Mode->Bots)if(IsValid(Bot))Bot->bQAFreeze=true;
    for(auto Supply:Mode->Supplies)if(IsValid(Supply))Supply->SetActorTickEnabled(false);
}
FVector AHeliosBioIterationQA::Feet() const
{return Mode->Player->GetActorLocation()-FVector(0,0,Mode->Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());}
TSharedRef<FJsonObject> AHeliosBioIterationQA::Snapshot(const TCHAR* Event) const
{
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("event"),Event);Row->SetBoolField(TEXT("isolated_fixture"),true);Row->SetNumberField(TEXT("wall_elapsed"),FPlatformTime::Seconds()-Started);
    Row->SetStringField(TEXT("map"),GetWorld()->GetOutermost()->GetName());Row->SetBoolField(TEXT("paused"),GetWorld()->IsPaused());
    auto* Player=Mode->Player.Get();auto* Inventory=Player->GetRoundInventory();auto* Weapon=Player->GetEquippedWeapon();
    const auto* Participant=Mode->Model.FindParticipant(0);
    Row->SetStringField(TEXT("weapon"),GetPathNameSafe(Weapon));Row->SetStringField(TEXT("weapon_class"),GetPathNameSafe(Weapon?Weapon->GetClass():nullptr));
    auto* Presentation=Player->FindComponentByClass<UHeliosCharacterPresentationComponent>();
    Row->SetStringField(TEXT("FP_mesh"),MeshPath(Presentation?Presentation->GetArmsMesh():nullptr));Row->SetStringField(TEXT("TP_mesh"),MeshPath(Weapon?Weapon->GetThirdPersonMesh():nullptr));
    Row->SetStringField(TEXT("slot0_weapon_id"),Inventory->GetWeaponId(0).ToString());Row->SetStringField(TEXT("inventory"),Inventory->Describe());Row->SetNumberField(TEXT("active_slot"),Inventory->GetActiveSlot());Row->SetBoolField(TEXT("inventory_enabled"),Inventory->IsEnabled());
    int32 Occupied=0;TArray<TSharedPtr<FJsonValue>> Slots;
    for(int32 I=0;I<5;++I){auto S=MakeShared<FJsonObject>();S->SetNumberField(TEXT("index"),I);S->SetBoolField(TEXT("occupied"),Inventory->HasSlot(I));S->SetStringField(TEXT("id"),Inventory->GetWeaponId(I).ToString());S->SetStringField(TEXT("weapon"),GetPathNameSafe(Inventory->GetWeapon(I)));S->SetNumberField(TEXT("reserve"),Inventory->GetReserve(I));Slots.Add(MakeShared<FJsonValueObject>(S));if(Inventory->HasSlot(I))++Occupied;}
    Row->SetArrayField(TEXT("slots"),Slots);Row->SetNumberField(TEXT("occupied_slots"),Occupied);
    Row->SetNumberField(TEXT("ammo"),Player->GetAmmo());Row->SetNumberField(TEXT("reserve"),Player->GetReserveAmmo());Row->SetNumberField(TEXT("shots_counter"),Player->ShotsFired);
    Row->SetNumberField(TEXT("HP"),Participant?Participant->Health:-1);Row->SetNumberField(TEXT("max_HP"),Participant?Participant->MaxHealth:-1);Row->SetNumberField(TEXT("team"),Participant?static_cast<int32>(Participant->Team):-1);Row->SetBoolField(TEXT("alive"),Participant&&Participant->bAlive);
    Row->SetNumberField(TEXT("supplies_collected"),Participant?Participant->SuppliesCollected:-1);Row->SetNumberField(TEXT("blue_supplies_collected"),Participant?Participant->BlueSuppliesCollected:-1);Row->SetNumberField(TEXT("zombie_supplies_collected"),Participant?Participant->ZombieSuppliesCollected:-1);Row->SetNumberField(TEXT("evolution_level"),Participant?Participant->EvolutionLevel:-1);
    Row->SetNumberField(TEXT("total_supplies"),Mode->TotalSupplies);Row->SetNumberField(TEXT("total_damage_events"),Mode->TotalDamageEvents);Row->SetNumberField(TEXT("total_infections"),Mode->TotalInfections);Row->SetNumberField(TEXT("round_serial"),Mode->RoundSerial);Row->SetNumberField(TEXT("phase"),static_cast<int32>(Mode->Model.GetPhase()));
    Row->SetBoolField(TEXT("box_available"),FixtureSupply&&FixtureSupply->IsAvailable());Row->SetBoolField(TEXT("box_can_reach"),FixtureSupply&&FixtureSupply->CanReachPickup(Player));
    Row->SetStringField(TEXT("feet"),Feet().ToString());Row->SetNumberField(TEXT("capsule_radius"),Player->GetCapsuleComponent()->GetScaledCapsuleRadius());Row->SetNumberField(TEXT("capsule_half_height"),Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());Row->SetBoolField(TEXT("crouched"),Player->bIsCrouched);
    Row->SetStringField(TEXT("actual_actor_rotation"),Player->GetActorRotation().ToString());Row->SetStringField(TEXT("actual_actor_quaternion"),Player->GetActorQuat().ToString());Row->SetStringField(TEXT("actual_actor_scale"),Player->GetActorScale3D().ToString());Row->SetStringField(TEXT("actual_capsule_up"),Player->GetCapsuleComponent()->GetUpVector().ToString());Row->SetStringField(TEXT("actual_production_spawn_rotation"),Mode->SpawnRotation(0).ToString());Row->SetNumberField(TEXT("configured_combat_eye"),Mode->Model.Config.EyeHeight);Row->SetNumberField(TEXT("configured_capsule_half_height"),Mode->Model.Config.MovementCapsuleHalfHeight);Row->SetStringField(TEXT("actual_camera_parent"),GetPathNameSafe(Player->FirstPersonCamera->GetAttachParent()));Row->SetStringField(TEXT("actual_camera_relative_location"),Player->FirstPersonCamera->GetRelativeLocation().ToString());Row->SetStringField(TEXT("actual_camera_relative_rotation"),Player->FirstPersonCamera->GetRelativeRotation().ToString());Row->SetStringField(TEXT("actual_camera_relative_scale"),Player->FirstPersonCamera->GetRelativeScale3D().ToString());
    if(auto* PC=Cast<APlayerController>(Player->GetController()))
    {
        int32 W=0,H=0;PC->GetViewportSize(W,H);Row->SetNumberField(TEXT("viewport_width"),W);Row->SetNumberField(TEXT("viewport_height"),H);
        if(PC->PlayerCameraManager)
        {
            const auto& View=PC->PlayerCameraManager->GetCameraCacheView();const float Aspect=W>0&&H>0&&!View.bConstrainAspectRatio?float(W)/H:View.AspectRatio;
            const auto Vertical=[Aspect](float HFOV){return FMath::RadiansToDegrees(2.f*FMath::Atan(FMath::Tan(FMath::DegreesToRadians(HFOV)*.5f)/Aspect));};
            Row->SetNumberField(TEXT("cached_world_horizontal_FOV"),View.FOV);Row->SetNumberField(TEXT("cached_FP_horizontal_FOV"),View.FirstPersonFOV);Row->SetNumberField(TEXT("cached_world_vertical_FOV"),Vertical(View.FOV));Row->SetNumberField(TEXT("cached_FP_vertical_FOV"),Vertical(View.FirstPersonFOV));Row->SetNumberField(TEXT("cached_FP_scale"),View.FirstPersonScale);Row->SetNumberField(TEXT("cached_aspect"),View.AspectRatio);Row->SetBoolField(TEXT("cached_constrain_aspect"),View.bConstrainAspectRatio);Row->SetNumberField(TEXT("cached_eye_above_feet"),View.Location.Z-Feet().Z);Row->SetNumberField(TEXT("cached_camera_world_time"),PC->PlayerCameraManager->GetCameraCacheTime());Row->SetBoolField(TEXT("cached_uses_FP_parameters"),View.bUseFirstPersonParameters);
        }
    }
    Row->SetNumberField(TEXT("component_eye_above_feet"),Player->FirstPersonCamera->GetComponentLocation().Z-Feet().Z);
    FHitResult Floor;FCollisionQueryParams Query(SCENE_QUERY_STAT(BioIterationQACapsuleFloor),false,Player);TArray<AActor*> Attached;Player->GetAttachedActors(Attached,true,true);Query.AddIgnoredActors(Attached);
    const bool Hit=GetWorld()->LineTraceSingleByChannel(Floor,Feet()+FVector(0,0,25),Feet()-FVector(0,0,100),ECC_Visibility,Query);
    Row->SetBoolField(TEXT("actual_original_floor_hit"),Hit);if(Hit){Row->SetNumberField(TEXT("capsule_floor_gap"),Feet().Z-Floor.ImpactPoint.Z);Row->SetStringField(TEXT("original_floor_component"),GetPathNameSafe(Floor.GetComponent()));}
    return Row;
}
bool AHeliosBioIterationQA::CheckView(const TCHAR* Event,float ExpectedEye,int32 ExpectedWidth,int32 ExpectedHeight)
{
    auto Row=Snapshot(Event);Evidence.Add(MakeShared<FJsonValueObject>(Row));
    const bool HasCache=Row->HasField(TEXT("cached_world_horizontal_FOV"));Check(FString(Event)+TEXT("_actual_camera_cache_exists"),HasCache);if(!HasCache)return false;
    const int32 W=Row->GetNumberField(TEXT("viewport_width")),H=Row->GetNumberField(TEXT("viewport_height"));Check(FString(Event)+TEXT("_actual_viewport"),W>0&&H>0&&(!ExpectedWidth||(W==ExpectedWidth&&H==ExpectedHeight)));
    Check(FString(Event)+TEXT("_actual_camera_eye"),FMath::Abs(Row->GetNumberField(TEXT("component_eye_above_feet"))-ExpectedEye)<.75&&FMath::Abs(Row->GetNumberField(TEXT("cached_eye_above_feet"))-ExpectedEye)<.75);
    Check(FString(Event)+TEXT("_actual_supported_capsule"),Row->GetBoolField(TEXT("actual_original_floor_hit"))&&FMath::Abs(Row->GetNumberField(TEXT("capsule_floor_gap")))<3&&Mode->Player->GetCharacterMovement()->IsMovingOnGround());
    if(bFloating)
    {
        Check(FString(Event)+TEXT("_world_vertical_70_FP_vertical_33"),FMath::Abs(Row->GetNumberField(TEXT("cached_world_vertical_FOV"))-70)<.15&&FMath::Abs(Row->GetNumberField(TEXT("cached_FP_vertical_FOV"))-33)<.15);
        Check(FString(Event)+TEXT("_dynamic_aspect_unconstrained"),!Row->GetBoolField(TEXT("cached_constrain_aspect"))&&FMath::Abs(Row->GetNumberField(TEXT("cached_aspect"))-float(W)/FMath::Max(1,H))<.005);
        Check(FString(Event)+TEXT("_FP_scale_one"),FMath::Abs(Row->GetNumberField(TEXT("cached_FP_scale"))-1)<.001);
    }
    else Check(FString(Event)+TEXT("_legacy_HFOV90_FP90_scale_point2"),FMath::Abs(Row->GetNumberField(TEXT("cached_world_horizontal_FOV"))-90)<.15&&FMath::Abs(Row->GetNumberField(TEXT("cached_FP_horizontal_FOV"))-90)<.15&&FMath::Abs(Row->GetNumberField(TEXT("cached_FP_scale"))-.2)<.001);
    return true;
}
bool AHeliosBioIterationQA::PrepareSupplyFixture()
{
    auto* Player=Mode->Player.Get();FCollisionQueryParams Query(SCENE_QUERY_STAT(BioIterationSupplyFixtureFloor),false,Player);FCollisionObjectQueryParams Static(ECC_WorldStatic);
    TArray<AActor*> Attached;Player->GetAttachedActors(Attached,true,true);Query.AddIgnoredActors(Attached);
    for(int32 I=0;I<8;++I)
    {
        const FVector Probe=Feet()+FVector(FMath::Cos(I*PI/4)*50,FMath::Sin(I*PI/4)*50,0);FHitResult Floor;
        if(!GetWorld()->LineTraceSingleByObjectType(Floor,Probe+FVector(0,0,30),Probe-FVector(0,0,50),Static,Query)||Floor.ImpactNormal.Z<Player->GetCharacterMovement()->GetWalkableFloorZ())continue;
        FixtureSupply=GetWorld()->SpawnActor<AHeliosBioSupply>(AHeliosBioSupply::StaticClass(),Floor.ImpactPoint+FVector(0,0,5),FRotator::ZeroRotator);
        if(!FixtureSupply)return false;FixtureSupply->SetActorTickEnabled(false);FixtureSupply->SetKind(EHeliosBioSupplyKind::Normal);
        if(FixtureSupply->bHasOriginalMesh&&FixtureSupply->CanReachPickup(Player))
        {auto E=Snapshot(TEXT("explicit_original_mesh_supply_fixture_at_supported_floor"));E->SetStringField(TEXT("fixture_box_location"),FixtureSupply->GetActorLocation().ToString());E->SetStringField(TEXT("fixture_box_original_floor"),Floor.ImpactPoint.ToString());E->SetBoolField(TEXT("automatic_pickup_tick_disabled_for_atomic_tests"),true);Evidence.Add(MakeShared<FJsonValueObject>(E));return true;}
        FixtureSupply->Destroy();FixtureSupply=nullptr;
    }
    return false;
}
bool AHeliosBioIterationQA::CheckRejectedPickup(const TCHAR* Case,UHeliosBioSupplyWeaponDefinition* Definition,bool TestPickup)
{
    auto* Inventory=Mode->Player->GetRoundInventory();FixtureSupply->SetWeaponContent(Definition);FixtureSupply->SetAvailable(true);
    auto Before=Snapshot(TEXT("rejection_before"));FString Error;
    const bool Can=Inventory->CanGrant(Definition,&Error),Granted=Inventory->TryGrant(Definition);
    const bool Picked=TestPickup&&Mode->PickupSupply(0,FixtureSupply);
    auto After=Snapshot(TEXT("rejection_after"));const bool Same=SameTransaction(Before,After);
    Check(FString(Case)+TEXT("_rejects_grant"),!Can&&!Granted);if(TestPickup)Check(FString(Case)+TEXT("_rejects_actual_pickup"),!Picked);Check(FString(Case)+TEXT("_box_HP_counts_ammo_loadout_unchanged"),Same);
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("event"),TEXT("production_inventory_rejected_transaction"));Row->SetStringField(TEXT("case"),Case);Row->SetStringField(TEXT("source_error"),Error);Row->SetBoolField(TEXT("actual_pickup_called"),TestPickup);Row->SetBoolField(TEXT("actual_pickup_result"),Picked);Row->SetBoolField(TEXT("grant_result"),Granted);Row->SetBoolField(TEXT("unchanged"),Same);Row->SetObjectField(TEXT("before"),Before);Row->SetObjectField(TEXT("after"),After);Evidence.Add(MakeShared<FJsonValueObject>(Row));return !Can&&!Granted&&!Picked&&Same;
}
void AHeliosBioIterationQA::TestInventory()
{
    const int32 BeforeFailures=Failures;auto* Player=Mode->Player.Get();auto* Inventory=Player->GetRoundInventory();
    UnreadyDefinition=NewObject<UHeliosBioSupplyWeaponDefinition>(this);UnreadyDefinition->OriginalItemId=10804013;UnreadyDefinition->WeaponId=TEXT("cfm_bio_supply_10804013");
    WrongDefinition=NewObject<UHeliosBioSupplyWeaponDefinition>(this);WrongDefinition->OriginalItemId=99999999;WrongDefinition->WeaponId=TEXT("cfm_bio_supply_99999999");
    Check(TEXT("unready_definition_stays_not_ready"),!UnreadyDefinition->IsReady());Check(TEXT("wrong_original_ID_not_in_supply_slot_map"),UHeliosBioSupplyWeaponDefinition::SlotForItem(WrongDefinition->OriginalItemId)==INDEX_NONE&&!WrongDefinition->IsReady());
    auto Initial=Snapshot(TEXT("original_inventory_before_transactions"));Evidence.Add(MakeShared<FJsonValueObject>(Initial));
    Check(TEXT("original_selected_loadout_unchanged"),Mode->SelectedLoadout==OriginalLoadout&&Player->GetEquippedWeapon()==OriginalWeapon.Get());
    Check(TEXT("inventory_map_scope"),Inventory->IsEnabled()==bFloating);
    if(!bFloating)
    {
        Check(TEXT("ordinary_map_five_slot_inventory_disabled"),!Inventory->HasSlot(0)&&!Inventory->Select(0,true)&&!Inventory->TryGrant(UnreadyDefinition)&&!Inventory->TryGrant(WrongDefinition));
        auto After=Snapshot(TEXT("ordinary_map_disabled_inventory_after_rejections"));Check(TEXT("ordinary_map_old_equipment_ammo_unchanged"),SameTransaction(Initial,After));Evidence.Add(MakeShared<FJsonValueObject>(After));bInventoryVerified=Failures==BeforeFailures;return;
    }
    Check(TEXT("real_original_weapon_in_baseline_slot"),Inventory->HasSlot(0)&&Inventory->GetWeapon(0)==OriginalWeapon.Get()&&Inventory->GetWeaponId(0)==OriginalLoadout.PrimaryWeaponId&&Initial->GetNumberField(TEXT("occupied_slots"))==1);
    if(!PrepareSupplyFixture()){Check(TEXT("supported_original_supply_fixture_available"),false);return;}
    // Explicit ammo fixture, not a physical shot. SetMagazineBulletCount invokes
    // the owner's HUD counter; that counter is never claimed as real fire here.
    const int32 Reduced=FMath::Max(0,Player->GetAmmo()-1);Player->GetEquippedWeapon()->SetMagazineBulletCount(Reduced);
    auto AmmoFixture=Snapshot(TEXT("explicit_ammo_state_fixture_no_real_shot_claim"));AmmoFixture->SetStringField(TEXT("fixture_API"),TEXT("AShooterWeapon::SetMagazineBulletCount; capacity unchanged; HUD counter side effect is not shot evidence"));Evidence.Add(MakeShared<FJsonValueObject>(AmmoFixture));
    CheckRejectedPickup(TEXT("unready_source_gameplay_definition"),UnreadyDefinition,true);
    CheckRejectedPickup(TEXT("wrong_original_item_ID"),WrongDefinition,true);
    {
        Check(TEXT("pause_fixture_entered"),UGameplayStatics::SetGamePaused(GetWorld(),true)&&GetWorld()->IsPaused());
        CheckRejectedPickup(TEXT("paused"),UnreadyDefinition,true);Check(TEXT("paused_baseline_selection_rejected"),!Inventory->Select(0,true)&&!Inventory->Restore());
        UGameplayStatics::SetGamePaused(GetWorld(),false);
    }
    auto* Participant=const_cast<FHeliosBioParticipant*>(Mode->Model.FindParticipant(0));const FHeliosBioParticipant Saved=*Participant;
    Participant->bAlive=false;Participant->Health=0;
    CheckRejectedPickup(TEXT("explicit_dead_rules_state"),UnreadyDefinition,true);Check(TEXT("dead_baseline_selection_rejected"),!Inventory->Select(0,true)&&!Inventory->Restore());*Participant=Saved;
    Participant->Team=EHeliosBioTeam::Zombie;
    // Normal zombie boxes legitimately advance evolution. Test inventory gating,
    // not an invented blanket ban on production zombie supply consumption.
    CheckRejectedPickup(TEXT("explicit_nonhuman_rules_state_inventory_only"),UnreadyDefinition,false);Check(TEXT("nonhuman_baseline_selection_rejected"),!Inventory->Select(0,true)&&!Inventory->Restore());*Participant=Saved;
    Participant->FrozenSeconds=2;
    CheckRejectedPickup(TEXT("explicit_frozen_rules_state"),UnreadyDefinition,true);Check(TEXT("frozen_baseline_selection_rejected"),!Inventory->Select(0,true));*Participant=Saved;
    auto Before=Snapshot(TEXT("before_baseline_select_suspend_restore"));bool Restored=true;
    for(int32 I=0;I<3;++I){Restored&=Inventory->Select(0,true);Inventory->Suspend();Restored&=Inventory->Restore();}
    for(int32 I=1;I<5;++I)Restored&=!Inventory->Select(I);
    auto After=Snapshot(TEXT("after_baseline_select_suspend_restore"));
    Check(TEXT("baseline_select_suspend_restore_production_success"),Restored);Check(TEXT("baseline_select_suspend_restore_no_ammo_refill_or_loadout_change"),SameTransaction(Before,After)&&Player->GetAmmo()==Reduced&&Inventory->GetWeapon(0)==OriginalWeapon.Get());Evidence.Add(MakeShared<FJsonValueObject>(Before));Evidence.Add(MakeShared<FJsonValueObject>(After));
    bInventoryVerified=Failures==BeforeFailures;
}
void AHeliosBioIterationQA::Tick(float Dt)
{
    Super::Tick(Dt);if(bFinished)return;if(bWaterLandingMode){TickWaterLanding(Dt);return;}if(bWaterMode||bFootMode){TickVisualFixture(Dt);return;}const double Now=FPlatformTime::Seconds();
    if(Now-Started>60){Finish(FString::Printf(TEXT("Bounded isolated QA timeout at stage %d"),Stage));return;}
    if(Stage==0)
    {
        if(Out.IsEmpty()||ExpectedMap.IsEmpty()){Finish(TEXT("BioIterationQAOut/BioExpectedMap missing"));return;}
        Mode=AHeliosBioHotelGameMode::Find(GetWorld());if(!Mode||!Mode->Player||!Mode->Player->GetEquippedWeapon())return;
        Check(TEXT("expected_actual_map"),GetWorld()->GetOutermost()->GetName()==ExpectedMap);
        bFloating=ExpectedMap.EndsWith(TEXT("/CFMBioPyramidFloating"));Check(TEXT("map_profile_scope"),Mode->bFloatingProfile==bFloating);
        OriginalLoadout=Mode->SelectedLoadout;OriginalWeapon=Mode->Player->GetEquippedWeapon();OriginalMagazineAmmo=Mode->Player->GetAmmo();
        Mode->Model.Config.PreparationSeconds=.2f;Mode->Model.Config.RoundSeconds=1200;Mode->Model.Config.InitialMotherCount=1;Mode->Model.Config.bKeepLocalPlayerHuman=true;Mode->Model.Config.SupplyIntervalSeconds=10000;
        Mode->RestartBioRound();FreezeIsolation();Next=Now+2;StageAt=Now;Stage=1;return;
    }
    FreezeIsolation();if(Now<Next)return;
    auto* Player=Mode->Player.Get();auto* PC=Cast<APlayerController>(Player->GetController());if(!PC||!PC->PlayerCameraManager){Finish(TEXT("Actual player camera manager unavailable"));return;}
    if(Stage==1)
    {
        if(Mode->Model.GetPhase()!=EHeliosBioPhase::Active){if(Now-StageAt>6)Finish(TEXT("Isolated real round did not become active"));return;}
        auto Initial=Snapshot(TEXT("initial_standing"));OriginalFP=Initial->GetStringField(TEXT("FP_mesh"));OriginalTP=Initial->GetStringField(TEXT("TP_mesh"));
        PC->GetViewportSize(InitialWidth,InitialHeight);if(InitialWidth<=0||InitialHeight<=0){Finish(TEXT("Real viewport required for dynamic aspect acceptance"));return;}
        Check(TEXT("independent_combat_eye_remains_160"),FMath::IsNearlyEqual(Mode->Model.Config.EyeHeight,160.f));
        Check(TEXT("dedicated_player_profile_only_on_floating"),Mode->Model.Config.PlayerCamera.bEnabled==bFloating);
        StandingFeet=Feet();CheckView(TEXT("standing"),bFloating?90:160);
        if(bFloating){Player->Crouch();Stage=2;Next=Now+.3;StageAt=Now;return;}
        Stage=3;
    }
    if(Stage==2)
    {
        if(!Player->bIsCrouched&&Now-StageAt<2)return;
        Check(TEXT("production_crouch_state"),Player->bIsCrouched);CheckView(TEXT("crouched"),60);
        Check(TEXT("crouch_preserves_original_feet"),FVector::Dist(Feet(),StandingFeet)<.25f);Check(TEXT("crouched_capsule_half_40"),FMath::IsNearlyEqual(Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),40.f,.05f));
        Player->UnCrouch();Stage=3;Next=Now+.3;StageAt=Now;return;
    }
    if(Stage==3)
    {
        if(bFloating){if(Player->bIsCrouched&&Now-StageAt<2)return;Check(TEXT("production_uncrouch_state"),!Player->bIsCrouched);CheckView(TEXT("uncrouched"),90);Check(TEXT("uncrouch_preserves_original_feet"),FVector::Dist(Feet(),StandingFeet)<.25f);}
        GEngine->Exec(GetWorld(),TEXT("r.SetRes 1024x768w"));Stage=4;StageAt=Now;Next=Now+.4;return;
    }
    if(Stage==4)
    {
        int32 W=0,H=0;PC->GetViewportSize(W,H);if(W!=1024||H!=768){if(Now-StageAt>5)Finish(TEXT("Real viewport did not change to 4:3; no simulated aspect substitution"));return;}
        CheckView(TEXT("actual_4_3_viewport"),bFloating?90:160,1024,768);
        const FString Command=FString::Printf(TEXT("r.SetRes %dx%dw"),InitialWidth,InitialHeight);GEngine->Exec(GetWorld(),*Command);Stage=5;StageAt=Now;Next=Now+.4;return;
    }
    if(Stage==5)
    {
        int32 W=0,H=0;PC->GetViewportSize(W,H);if(W!=InitialWidth||H!=InitialHeight){if(Now-StageAt>5)Finish(TEXT("Real viewport restoration failed"));return;}
        CheckView(TEXT("restored_original_viewport"),bFloating?90:160,InitialWidth,InitialHeight);bCameraVerified=Failures==0;
        TestInventory();RestartFailuresBefore=Failures;
        // Rules-only phase advancement is an explicit fixture. The actual restart
        // request and all inventory reset/equipment/camera behavior are production.
        Mode->Model.Tick(Mode->Model.GetPhaseTimeRemaining()+1.f);Check(TEXT("explicit_roundover_phase_fixture"),Mode->Model.GetPhase()==EHeliosBioPhase::RoundOver);
        if(bFloating)Check(TEXT("roundover_baseline_selection_rejected"),!Player->GetRoundInventory()->Select(0,true));
        RestartSerial=Mode->RoundSerial;Player->RequestBioRestart();FreezeIsolation();Stage=6;Next=Now+2;return;
    }
    if(Stage==6)
    {
        auto After=Snapshot(TEXT("actual_production_request_restart_result"));Evidence.Add(MakeShared<FJsonValueObject>(After));
        Check(TEXT("production_request_restart_advanced_serial_once"),Mode->RoundSerial==RestartSerial+1);
        Check(TEXT("restart_retains_original_selected_loadout_and_weapon_instance"),Mode->SelectedLoadout==OriginalLoadout&&Player->GetEquippedWeapon()==OriginalWeapon.Get());
        Check(TEXT("restart_retains_original_presentation_assets"),After->GetStringField(TEXT("FP_mesh"))==OriginalFP&&After->GetStringField(TEXT("TP_mesh"))==OriginalTP);
        if(bFloating){auto* Inv=Player->GetRoundInventory();Check(TEXT("restart_original_baseline_only"),Inv->HasSlot(0)&&Inv->GetWeapon(0)==OriginalWeapon.Get()&&Inv->GetWeaponId(0)==OriginalLoadout.PrimaryWeaponId&&After->GetNumberField(TEXT("occupied_slots"))==1);}
        Check(TEXT("restart_refills_only_by_explicit_round_reset_contract"),Player->GetAmmo()==OriginalMagazineAmmo&&Player->GetReserveAmmo()==600);
        CheckView(TEXT("after_production_restart"),bFloating?90:160);bRestartVerified=Failures==RestartFailuresBefore;Finish();
    }
}
void AHeliosBioIterationQA::Finish(const FString& Why)
{
    if(bFinished)return;bFinished=true;Blocker=Why;if(GetWorld()->IsPaused())UGameplayStatics::SetGamePaused(GetWorld(),false);
    auto Report=MakeShared<FJsonObject>();const bool Passed=Why.IsEmpty()&&Failures==0&&bCameraVerified&&bInventoryVerified&&bRestartVerified;
    Report->SetBoolField(TEXT("passed"),Passed);Report->SetStringField(TEXT("status"),Passed?TEXT("isolated_camera_inventory_regression_observed"):TEXT("incomplete_or_blocked"));Report->SetStringField(TEXT("blocker"),Why);Report->SetNumberField(TEXT("failures"),Failures);Report->SetStringField(TEXT("map"),GetWorld()->GetOutermost()->GetName());Report->SetStringField(TEXT("expected_map"),ExpectedMap);Report->SetBoolField(TEXT("floating_profile"),bFloating);Report->SetBoolField(TEXT("camera_verified"),bCameraVerified);Report->SetBoolField(TEXT("inventory_transaction_regression_verified"),bInventoryVerified);Report->SetBoolField(TEXT("production_restart_verified"),bRestartVerified);
    Report->SetBoolField(TEXT("fixture_mode"),true);Report->SetBoolField(TEXT("new_supply_weapons_four_gun_acceptance_verified"),false);Report->SetBoolField(TEXT("physical_firearm_shots_verified"),false);Report->SetBoolField(TEXT("natural_round_verified"),false);Report->SetBoolField(TEXT("readiness_gates_mutated"),false);Report->SetBoolField(TEXT("saved_loadout_written"),false);Report->SetBoolField(TEXT("headless"),FParse::Param(FCommandLine::Get(),TEXT("NullRHI")));Report->SetBoolField(TEXT("rendered_visual_acceptance"),false);
    TArray<TSharedPtr<FJsonValue>> SupplyReadiness;for(int32 Slot=1;Slot<5;++Slot){auto Row=MakeShared<FJsonObject>();auto* Def=UHeliosBioSupplyWeaponDefinition::LoadForSlot(Slot);Row->SetNumberField(TEXT("slot"),Slot);Row->SetNumberField(TEXT("original_item_ID"),UHeliosBioSupplyWeaponDefinition::ItemForSlot(Slot));Row->SetBoolField(TEXT("asset_exists"),Def!=nullptr);Row->SetBoolField(TEXT("current_production_ready"),Def&&Def->IsReady());Row->SetBoolField(TEXT("tested_acquisition_fire_or_source_presentation"),false);SupplyReadiness.Add(MakeShared<FJsonValueObject>(Row));}Report->SetArrayField(TEXT("new_supply_weapon_readiness_read_only"),SupplyReadiness);
    Report->SetStringField(TEXT("scope"),TEXT("Independent isolated runtime regression in the real map. Reads production cached camera and physically supported capsule, calls production crouch and real r.SetRes viewport changes. Rules preparation/round length, frozen NPCs, disabled automatic supply ticks, same-frame alive/team/frozen state and reduced magazine ammunition are explicitly controlled fixtures. Unready/wrong definitions are transient rejection probes with all readiness flags left false. Source normal supply mesh is placed on measured original support solely for atomic real PickupSupply rejection checks. Baseline Select/Suspend/Restore must preserve ammo, health, counters, box and original equipment. Rules Tick advances only the final phase fixture; actual RequestBioRestart owns serial, loadout, inventory and legitimate round ammunition reset. Ordinary map keeps disabled round inventory and its legacy camera. No four-gun acquisition/firing, natural round, physical device input, source animation or visual-fidelity acceptance is inferred."));
    Report->SetArrayField(TEXT("checks"),Checks);Report->SetArrayField(TEXT("evidence"),Evidence);
    if(!Out.IsEmpty()){FString Text;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Out/TEXT("iteration_report.json")));}
    if(IsValid(FixtureSupply))FixtureSupply->Destroy();UE_LOG(LogTemp,Display,TEXT("BIO_ITERATION_QA_FINISHED passed=%d failures=%d blocker=%s"),Passed,Failures,*Why);FPlatformMisc::RequestExit(false);
}
void AHeliosBioIterationQA::SampleActualSoles()
{
    TSet<FString> Forms;int32 Measured=0,Aligned=0,Eligible=0;
    for(auto Bot:Mode->Bots)
    {
        const auto* Participant=Mode->Model.FindParticipant(Bot->ParticipantId);if(!Participant||!Participant->bAlive||Participant->Team!=EHeliosBioTeam::Human)continue;++Eligible;
        const auto& Loadout=Bot->GetHumanLoadout();const FString Form=Loadout.CharacterId.ToString()+TEXT("/")+Loadout.CharacterVariantId.ToString();Forms.Add(Form);
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("actual_CPU_skinned_shoe_sole"));Row->SetNumberField(TEXT("sample"),SoleSamples);Row->SetStringField(TEXT("form"),Form);Row->SetNumberField(TEXT("participant"),Bot->ParticipantId);Row->SetStringField(TEXT("actor_rotation"),Bot->GetActorRotation().ToString());Row->SetStringField(TEXT("capsule_up"),Bot->GetCapsuleComponent()->GetUpVector().ToString());Row->SetNumberField(TEXT("speed_cm_s"),Bot->GetVelocity().Size());
        auto* Presentation=Bot->FindComponentByClass<UHeliosCharacterPresentationComponent>();auto* Mesh=Presentation?Presentation->GetArmsMesh():nullptr;
        auto Add=[&](){SoleEvidence.Add(MakeShared<FJsonValueObject>(Row));};
        if(!Mesh||!Mesh->GetSkeletalMeshAsset()){Row->SetStringField(TEXT("blocker"),TEXT("Actual human presentation mesh unavailable"));Add();continue;}
        Row->SetStringField(TEXT("actual_body"),Mesh->GetSkeletalMeshAsset()->GetPathName());Row->SetStringField(TEXT("actual_mesh_transform"),Mesh->GetComponentTransform().ToHumanReadableString());Row->SetBoolField(TEXT("actual_visible"),Mesh->IsVisible()&&!Mesh->bHiddenInGame&&!Bot->IsHidden());Row->SetNumberField(TEXT("pose_commit_frame"),double(Presentation->GetPoseCommitFrame()));Row->SetNumberField(TEXT("actual_frame"),double(GFrameCounter));
        if(auto* Node=Mesh->GetSingleNodeInstance()){Row->SetStringField(TEXT("actual_source_clip"),GetPathNameSafe(Node->GetCurrentAsset()));Row->SetNumberField(TEXT("actual_source_seconds"),Node->GetCurrentTime());}
        const auto* Render=Mesh->GetSkeletalMeshRenderData();auto* Weights=Mesh->GetSkinWeightBuffer(0);
        if(!Render||!Render->LODRenderData.IsValidIndex(0)||!Weights||!Weights->GetDataVertexBuffer()->GetWeightData()){Row->SetStringField(TEXT("blocker"),TEXT("CPU skin weights unavailable; capsule/bone centers are not shoe soles"));Add();continue;}
        TArray<FFinalSkinVertex> Vertices;Mesh->GetCPUSkinnedVertices(Vertices,0);const auto& LOD=Render->LODRenderData[0];
        FVector Sole[3];float Lowest[3]={MAX_flt,MAX_flt,MAX_flt};int32 Counts[3]={0,0,0};
        for(const auto& Section:LOD.RenderSections)for(uint32 Index=Section.BaseVertexIndex;Index<Section.BaseVertexIndex+Section.NumVertices&&Index<uint32(Vertices.Num());++Index)
        {
            bool Foot=false,Left=false,Right=false;
            for(uint32 Influence=0;Influence<Weights->GetMaxBoneInfluences();++Influence)
            {
                const uint32 Local=Weights->GetBoneIndex(Index,Influence);if(!Weights->GetBoneWeight(Index,Influence)||!Section.BoneMap.IsValidIndex(Local))continue;
                const FString Name=Mesh->GetBoneName(Section.BoneMap[Local]).ToString().ToLower();if(!Name.Contains(TEXT("foot"))&&!Name.Contains(TEXT("toe"))&&!Name.Contains(TEXT("ankle")))continue;
                Foot=true;Left|=Name.Contains(TEXT("left"))||Name.Contains(TEXT("_l"))||Name.Contains(TEXT(" l "));Right|=Name.Contains(TEXT("right"))||Name.Contains(TEXT("_r"))||Name.Contains(TEXT(" r "));
            }
            if(!Foot)continue;const FVector Position=Mesh->GetComponentTransform().TransformPosition(FVector(Vertices[Index].Position));
            for(int32 Side=0;Side<3;++Side)if(Side==0||(Side==1&&Left)||(Side==2&&Right)){++Counts[Side];if(Position.Z<Lowest[Side]){Lowest[Side]=Position.Z;Sole[Side]=Position;}}
        }
        const FVector CapsuleFeet=Bot->GetActorLocation()-FVector(0,0,Bot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        FCollisionQueryParams Query(SCENE_QUERY_STAT(BioIterationActualSoleGround),false,Bot);for(auto Other:Mode->Bots)Query.AddIgnoredActor(Other);Query.AddIgnoredActor(Mode->Player);TArray<AActor*> Attached;Bot->GetAttachedActors(Attached,true,true);Query.AddIgnoredActors(Attached);
        FHitResult Center;const bool CenterHit=GetWorld()->LineTraceSingleByChannel(Center,CapsuleFeet+FVector(0,0,30),CapsuleFeet-FVector(0,0,200),ECC_Visibility,Query);
        Row->SetBoolField(TEXT("actual_grounded"),Bot->GetCharacterMovement()->IsMovingOnGround());Row->SetNumberField(TEXT("capsule_half_cm"),Bot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());Row->SetNumberField(TEXT("capsule_radius_cm"),Bot->GetCapsuleComponent()->GetScaledCapsuleRadius());Row->SetBoolField(TEXT("capsule_center_floor_hit"),CenterHit);
        if(CenterHit)Row->SetNumberField(TEXT("capsule_bottom_ground_gap_cm"),CapsuleFeet.Z-Center.ImpactPoint.Z);
        TArray<TSharedPtr<FJsonValue>> Sides;bool AllFootHit=false;float MinGap=MAX_flt;
        for(int32 Side=0;Side<3;++Side)
        {
            auto S=MakeShared<FJsonObject>();S->SetStringField(TEXT("side"),Side==0?TEXT("all_source_foot_weights"):Side==1?TEXT("source_left_foot_weights"):TEXT("source_right_foot_weights"));S->SetNumberField(TEXT("actual_skinned_vertices"),Counts[Side]);FHitResult Floor;
            const bool Hit=Counts[Side]>0&&GetWorld()->LineTraceSingleByChannel(Floor,FVector(Sole[Side].X,Sole[Side].Y,FMath::Max(Sole[Side].Z,CapsuleFeet.Z)+30),FVector(Sole[Side].X,Sole[Side].Y,FMath::Min(Sole[Side].Z,CapsuleFeet.Z)-200),ECC_Visibility,Query);
            S->SetBoolField(TEXT("actual_shoe_underfloor_hit"),Hit);if(Hit){const float Gap=Lowest[Side]-Floor.ImpactPoint.Z;S->SetStringField(TEXT("actual_skinned_sole_world"),Sole[Side].ToString());S->SetNumberField(TEXT("actual_skin_sole_ground_gap_cm"),Gap);S->SetStringField(TEXT("actual_original_floor"),GetPathNameSafe(Floor.GetComponent()));S->SetStringField(TEXT("actual_floor_normal"),Floor.ImpactNormal.ToString());if(CenterHit)S->SetNumberField(TEXT("shoe_floor_vs_capsule_center_floor_cm"),Floor.ImpactPoint.Z-Center.ImpactPoint.Z);if(Side==0){AllFootHit=true;MinGap=Gap;}}
            Sides.Add(MakeShared<FJsonValueObject>(S));
        }
        const bool Standing=Bot->GetCharacterMovement()->IsMovingOnGround()&&Bot->GetVelocity().Size()<5.f&&Mesh->IsVisible()&&!Mesh->bHiddenInGame&&!Bot->IsHidden()&&Presentation->GetPoseCommitCount()>0;
        Row->SetArrayField(TEXT("actual_shoe_sides"),Sides);Row->SetBoolField(TEXT("standing_sample"),Standing);Row->SetBoolField(TEXT("actual_minimum_shoe_sole_within_3cm"),Standing&&AllFootHit&&FMath::Abs(MinGap)<=3.f);
        if(AllFootHit&&Standing){++Measured;if(FMath::Abs(MinGap)<=3.f)++Aligned;}
        Add();
    }
    auto Summary=MakeShared<FJsonObject>();Summary->SetStringField(TEXT("kind"),TEXT("actual_sole_sample_summary"));Summary->SetNumberField(TEXT("sample"),SoleSamples++);Summary->SetNumberField(TEXT("eligible_human_NPCs"),Eligible);Summary->SetNumberField(TEXT("actual_distinct_source_forms"),Forms.Num());Summary->SetNumberField(TEXT("actual_standing_skin_sole_measurements"),Measured);Summary->SetNumberField(TEXT("standing_soles_within_3cm"),Aligned);SoleEvidence.Add(MakeShared<FJsonValueObject>(Summary));
    bSolesMeasured=Eligible>0&&Measured==Eligible;bSolesAligned=bSolesMeasured&&Aligned==Measured;
}
bool AHeliosBioIterationQA::LoadWaterWitness()
{
    FString Text;TSharedPtr<FJsonObject> Document;
    const FString Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../exports/cfm_biopyramid_floating/water/water_visual_fixture.json"));
    if(!FFileHelper::LoadFileToString(Text,*Path)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Document)||!Document.IsValid()||Document->GetStringField(TEXT("schema"))!=TEXT("OriginalWaterVisualWitness.v1"))return false;
    for(const auto& Value:Document->GetArrayField(TEXT("positions_ue_cm"))){const auto& P=Value->AsArray();if(P.Num()!=3)return false;WaterVertices.Add(FVector(P[0]->AsNumber(),P[1]->AsNumber(),P[2]->AsNumber()));}
    for(const auto& Value:Document->GetArrayField(TEXT("red")))WaterRed.Add(Value->AsNumber());
    for(const auto& Value:Document->GetArrayField(TEXT("triangles"))){const auto& P=Value->AsArray();if(P.Num()!=3)return false;WaterTriangles.Add(FIntVector(P[0]->AsNumber(),P[1]->AsNumber(),P[2]->AsNumber()));}
    if(WaterVertices.Num()!=56||WaterRed.Num()!=56||WaterTriangles.Num()!=86)return false;
    const auto& Collider=Document->GetObjectField(TEXT("original_water_collision"));
    if(Collider->GetStringField(TEXT("source_key"))!=TEXT("buildplayer-2_pvp_bio_shjzt:3211")||Collider->GetStringField(TEXT("expected_combined_collision_mesh"))!=TEXT("SM_BioPyramidFloatingCollision"))return false;
    const auto& Low=Collider->GetArrayField(TEXT("UE_bounds_min"));const auto& High=Collider->GetArrayField(TEXT("UE_bounds_max"));if(Low.Num()!=3||High.Num()!=3)return false;
    WaterCollisionMin=FVector(Low[0]->AsNumber(),Low[1]->AsNumber(),Low[2]->AsNumber());WaterCollisionMax=FVector(High[0]->AsNumber(),High[1]->AsNumber(),High[2]->AsNumber());

    for(TActorIterator<AActor> It(GetWorld());It;++It)
    {
        TInlineComponentArray<UStaticMeshComponent*> Components;It->GetComponents(Components);
        for(auto* Mesh:Components)
        {
        if(!Mesh||!Mesh->GetStaticMesh()||!Mesh->GetStaticMesh()->GetName().Equals(TEXT("SM_BioPyramidFloating")))continue;
        for(int32 I=0;I<Mesh->GetNumMaterials();++I)if(auto* Material=Mesh->GetMaterial(I);Material&&Material->GetName()==TEXT("M_BPF_Water_2_37__NoLM")){WaterComponent=Mesh;WaterMaterialSlot=I;OriginalWaterMaterial=Material;}
        }
    }
    if(!WaterComponent.IsValid()||!OriginalWaterMaterial)return false;
    for(auto Platform:Mode->MovingPlatforms)if(IsValid(Platform)&&Platform->IsRouteConfigured()){VisualPlatform=Platform;bPlatformWasRunning=Platform->IsRouteRunning();break;}
    return VisualPlatform!=nullptr;
}
bool AHeliosBioIterationQA::OriginalWaterRayVisible(const FVector& Eye,const FVector& Target) const
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BioIterationOriginalWaterVisibility),true,Mode->Player);for(auto Bot:Mode->Bots)Query.AddIgnoredActor(Bot);TArray<AActor*> Attached;Mode->Player->GetAttachedActors(Attached,true,true);Query.AddIgnoredActors(Attached);
    FHitResult Hit;const bool Blocked=GetWorld()->LineTraceSingleByChannel(Hit,Eye,Target,ECC_Visibility,Query);if(!Blocked){++WaterClearRays;return true;}
    const auto* Mesh=Cast<UStaticMeshComponent>(Hit.GetComponent());const float Top=WaterCollisionMax.Z;
    // The authored nontrigger Water box is 0.10cm above the translucent mesh.
    // This is a terminal self surface, not an opaque wall. A different component,
    // height, extent or earlier hit remains rejected; no actor is ignored.
    const bool Terminal=Mesh&&Mesh->GetStaticMesh()&&Mesh->GetStaticMesh()->GetName()==TEXT("SM_BioPyramidFloatingCollision")&&
        FMath::Abs(Hit.ImpactPoint.Z-Top)<=.05f&&FMath::Abs(Hit.ImpactNormal.Z)>=.999f&&
        Hit.ImpactPoint.X>=WaterCollisionMin.X-.05f&&Hit.ImpactPoint.X<=WaterCollisionMax.X+.05f&&Hit.ImpactPoint.Y>=WaterCollisionMin.Y-.05f&&Hit.ImpactPoint.Y<=WaterCollisionMax.Y+.05f&&
        Target.X>=WaterCollisionMin.X-.05f&&Target.X<=WaterCollisionMax.X+.05f&&Target.Y>=WaterCollisionMin.Y-.05f&&Target.Y<=WaterCollisionMax.Y+.05f&&
        Top>Target.Z&&Top-Target.Z<=.2f&&Hit.TraceStart.Z>Top;
    if(Terminal)++WaterTerminalRays;else ++WaterOpaqueRays;
    if(WaterRayLogged<16)
    {
        ++WaterRayLogged;auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("actual_water_visibility_first_hit"));Row->SetStringField(TEXT("eye"),Eye.ToString());Row->SetStringField(TEXT("original_water_point"),Target.ToString());Row->SetStringField(TEXT("actual_hit"),Hit.ImpactPoint.ToString());Row->SetStringField(TEXT("actual_normal"),Hit.ImpactNormal.ToString());Row->SetStringField(TEXT("actual_component"),GetPathNameSafe(Hit.GetComponent()));Row->SetBoolField(TEXT("exact_original_Water3211_terminal_self_surface"),Terminal);Row->SetNumberField(TEXT("source_water_box_top_Z"),Top);VisualDiagnostics.Add(MakeShared<FJsonValueObject>(Row));
        UE_LOG(LogTemp,Display,TEXT("BIO_WATER_VISIBILITY_FIRST_HIT terminal_water3211=%d eye=%s target=%s hit=%s normal=%s component=%s source_top=%.6f"),Terminal,*Eye.ToString(),*Target.ToString(),*Hit.ImpactPoint.ToString(),*Hit.ImpactNormal.ToString(),*GetPathNameSafe(Hit.GetComponent()),Top);
    }
    return Terminal;
}
int32 AHeliosBioIterationQA::VisibleWaterCount(const FVector& Eye,FVector& BestTarget) const
{
    int32 Count=0;float Best=-1;
    for(const auto& T:WaterTriangles)
    {
        if(!WaterVertices.IsValidIndex(T.X)||!WaterVertices.IsValidIndex(T.Y)||!WaterVertices.IsValidIndex(T.Z))continue;
        const float Red=(WaterRed[T.X]+WaterRed[T.Y]+WaterRed[T.Z])/3;if(Red<.35f)continue;
        const FVector Target=(WaterVertices[T.X]+WaterVertices[T.Y]+WaterVertices[T.Z])/3;
        if(!OriginalWaterRayVisible(Eye,Target))continue;
        ++Count;const float Score=Red*FVector::CrossProduct(WaterVertices[T.Y]-WaterVertices[T.X],WaterVertices[T.Z]-WaterVertices[T.X]).Size()/FMath::Max(100.f,FVector::DistSquared(Eye,Target));
        if(Score>Best){Best=Score;BestTarget=Target;}
    }
    return Count;
}

// Explicit rendering fixtures only. Player camera placement and a temporary
// platform hold are recorded; none of this is traversal/natural-round evidence.
bool AHeliosBioIterationQA::PlaceWaterCamera(int32 View)
{
    auto* Player=Mode->Player.Get();auto* PC=Cast<APlayerController>(Player->GetController());if(!PC)return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BioWaterCameraFixture),false,Player);TArray<AActor*> Attached;Player->GetAttachedActors(Attached,true,true);Query.AddIgnoredActors(Attached);
    const auto* Capsule=Player->GetCapsuleComponent();const float Half=Capsule->GetScaledCapsuleHalfHeight(),Radius=Capsule->GetScaledCapsuleRadius();
    const FCollisionShape Shape=FCollisionShape::MakeCapsule(Radius,Half);const FCollisionObjectQueryParams Static(ECC_WorldStatic);
    FVector BestFeet,BestTarget;float BestScore=-1;FString Source;UPrimitiveComponent* Support=nullptr;
    TMap<FString,int32> Rejections;int32 CandidateCount=0,FloorPassed=0,CapsulePassed=0,MaxVisible=0;
    const int32 ClearBefore=WaterClearRays,TerminalBefore=WaterTerminalRays,OpaqueBefore=WaterOpaqueRays;
    auto Probe=[&](const FVector& P,const FString& Label,bool PlatformFloor)
    {
        FHitResult Floor;const bool Hit=PlatformFloor?GetWorld()->LineTraceSingleByChannel(Floor,P+FVector(0,0,150),P-FVector(0,0,180),ECC_Visibility,Query):GetWorld()->LineTraceSingleByObjectType(Floor,P+FVector(0,0,180),P-FVector(0,0,1500),Static,Query);
        ++CandidateCount;
        if(!Hit||!Floor.GetComponent()){++Rejections.FindOrAdd(TEXT("no_original_floor"));return;}
        if(Floor.ImpactNormal.Z<Player->GetCharacterMovement()->GetWalkableFloorZ()){++Rejections.FindOrAdd(TEXT("floor_unwalkable"));return;}
        if(PlatformFloor&&Floor.GetComponent()!=VisualPlatform->GetPlatformMesh()){++Rejections.FindOrAdd(TEXT("not_native_platform_floor"));return;}
        if(!PlatformFloor&&FMath::Abs(Floor.ImpactPoint.Z-WaterCollisionMax.Z)<=.05f&&
            Floor.ImpactPoint.X>=WaterCollisionMin.X-.05f&&Floor.ImpactPoint.X<=WaterCollisionMax.X+.05f&&
            Floor.ImpactPoint.Y>=WaterCollisionMin.Y-.05f&&Floor.ImpactPoint.Y<=WaterCollisionMax.Y+.05f)
        {++Rejections.FindOrAdd(TEXT("original_water_surface_is_not_a_standing_camera_shore"));return;}
        ++FloorPassed;
        const FVector FeetAt=Floor.ImpactPoint+FVector(0,0,2),Center=FeetAt+FVector(0,0,Half+2);
        if(GetWorld()->OverlapBlockingTestByChannel(Center,FQuat::Identity,ECC_Pawn,Shape,Query)){++Rejections.FindOrAdd(TEXT("actual_capsule_overlap"));return;}
        ++CapsulePassed;
        // A thin vertical ray alone does not establish that this character's
        // real floor sweep can land here. Use the production movement query at
        // the proposed capsule center, without moving or altering the pawn.
        FFindFloorResult NativeFloor;Player->GetCharacterMovement()->FindFloor(Center,NativeFloor,false);
        if(!NativeFloor.IsWalkableFloor()||NativeFloor.HitResult.GetComponent()!=Floor.GetComponent()||FMath::Abs(NativeFloor.HitResult.ImpactPoint.Z-Floor.ImpactPoint.Z)>3.f)
        {++Rejections.FindOrAdd(TEXT("native_capsule_floor_does_not_match_source_ray"));return;}
        FVector Target;const int32 Count=VisibleWaterCount(FeetAt+FVector(-10,0,90),Target);MaxVisible=FMath::Max(MaxVisible,Count);if(Count<3){++Rejections.FindOrAdd(TEXT("fewer_than_3_visible_source_triangles"));return;}
        const float Distance=FVector::Dist2D(FeetAt,Target);const float Score=Count+1000.f/FMath::Max(100.f,Distance);
        if(View==1&&Distance>700.f){++Rejections.FindOrAdd(TEXT("not_shore_within_700cm"));return;}
        if(Score>BestScore){BestScore=Score;BestFeet=FeetAt;BestTarget=Target;Source=Label;Support=Floor.GetComponent();}
    };
    if(View==0)for(int32 I=0;I<Mode->SpawnPoints.Num();++I)Probe(Mode->SpawnPoints[I],FString::Printf(TEXT("original_source_spawn_%d"),I),false);
    if(View==1)
    {
        for(const auto& T:WaterTriangles)
        {
            const FVector Center=(WaterVertices[T.X]+WaterVertices[T.Y]+WaterVertices[T.Z])/3;
            for(float Distance:{100.f,250.f,450.f})for(int32 I=0;I<8;++I)
                Probe(Center+FVector(FMath::Cos(I*PI/4)*Distance,FMath::Sin(I*PI/4)*Distance,0),TEXT("measured_original_static_shore"),false);
        }
    }
    if(View==2)
    {
        // The original platform reaches the western water on its real route.
        // Do not move it to a constructed screenshot location.
        const FVector Position=VisualPlatform->GetActorLocation();if(Position.X>-1250.f)return false;
        Probe(Position+FVector(0,0,45),TEXT("native_source_platform_actual_route_position"),true);
    }
    auto Summary=MakeShared<FJsonObject>();Summary->SetStringField(TEXT("kind"),TEXT("actual_water_camera_candidates"));Summary->SetNumberField(TEXT("view"),View);Summary->SetNumberField(TEXT("candidates"),CandidateCount);Summary->SetNumberField(TEXT("walkable_original_floor_candidates"),FloorPassed);Summary->SetNumberField(TEXT("actual_clear_capsule_candidates"),CapsulePassed);Summary->SetNumberField(TEXT("maximum_visible_original_water_triangles"),MaxVisible);Summary->SetNumberField(TEXT("clear_rays"),WaterClearRays-ClearBefore);Summary->SetNumberField(TEXT("exact_Water3211_terminal_rays"),WaterTerminalRays-TerminalBefore);Summary->SetNumberField(TEXT("other_opaque_first_hits"),WaterOpaqueRays-OpaqueBefore);auto Reasons=MakeShared<FJsonObject>();for(const auto& Pair:Rejections)Reasons->SetNumberField(Pair.Key,Pair.Value);Summary->SetObjectField(TEXT("actual_rejection_counts"),Reasons);Summary->SetBoolField(TEXT("selected"),BestScore>=0);VisualDiagnostics.Add(MakeShared<FJsonValueObject>(Summary));
    UE_LOG(LogTemp,Display,TEXT("BIO_WATER_CAMERA_CANDIDATES view=%d total=%d floor=%d capsule_clear=%d max_visible=%d ray_clear=%d terminal_water3211=%d opaque=%d selected=%d"),View,CandidateCount,FloorPassed,CapsulePassed,MaxVisible,WaterClearRays-ClearBefore,WaterTerminalRays-TerminalBefore,WaterOpaqueRays-OpaqueBefore,BestScore>=0);
    if(BestScore<0)return false;
    Summary->SetStringField(TEXT("selected_source"),Source);Summary->SetStringField(TEXT("selected_feet"),BestFeet.ToString());Summary->SetStringField(TEXT("selected_support"),GetPathNameSafe(Support));Summary->SetStringField(TEXT("selected_water_target"),BestTarget.ToString());
    if(View==2)VisualPlatform->SetRouteRunning(false);
    Player->UnCrouch();Player->GetCharacterMovement()->StopMovementImmediately();Player->ConsumeMovementInputVector();
    Player->SetActorLocation(BestFeet+FVector(0,0,Half+2),false,nullptr,ETeleportType::TeleportPhysics);Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    const FRotator Facing=(BestTarget-(BestFeet+FVector(-10,0,90))).Rotation();PC->SetControlRotation(Facing);Player->SetActorRotation(FRotator(0,Facing.Yaw,0));VisualTarget=BestTarget;
    CurrentWaterView=MakeShared<FJsonObject>();CurrentWaterView->SetStringField(TEXT("name"),View==0?TEXT("original_spawn_water_view"):View==1?TEXT("original_static_shore_water_view"):TEXT("native_platform_low_angle_water_view"));CurrentWaterView->SetStringField(TEXT("fixture_source"),Source);CurrentWaterView->SetStringField(TEXT("fixture_requested_feet"),BestFeet.ToString());CurrentWaterView->SetStringField(TEXT("fixture_original_support"),GetPathNameSafe(Support));CurrentWaterView->SetBoolField(TEXT("fixture_camera_repositioned"),true);CurrentWaterView->SetBoolField(TEXT("native_platform_held_for_static_camera"),View==2);CurrentWaterView->SetStringField(TEXT("water_target"),BestTarget.ToString());CurrentWaterView->SetArrayField(TEXT("captures"),{});
    CurrentWaterView->SetObjectField(TEXT("actual_immediately_after_placement"),WaterSupportEvidence(TEXT("after_explicit_camera_fixture_placement")));
    UE_LOG(LogTemp,Display,TEXT("BIO_WATER_CAMERA_SELECTED view=%d source=%s feet=%s support=%s target=%s"),View,*Source,*BestFeet.ToString(),*GetPathNameSafe(Support),*BestTarget.ToString());LastWaterSupportLog=0;
    return true;
}
TSharedRef<FJsonObject> AHeliosBioIterationQA::WaterSupportEvidence(const TCHAR* Event) const
{
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("actual_water_camera_native_support"));Row->SetStringField(TEXT("event"),Event);Row->SetNumberField(TEXT("view"),VisualViewIndex);Row->SetNumberField(TEXT("wall_elapsed"),FPlatformTime::Seconds()-Started);Row->SetNumberField(TEXT("world_seconds"),GetWorld()->GetTimeSeconds());Row->SetNumberField(TEXT("world_delta_seconds"),GetWorld()->GetDeltaSeconds());Row->SetBoolField(TEXT("paused"),GetWorld()->IsPaused());
    auto* Player=Mode->Player.Get();auto* Movement=Player->GetCharacterMovement();auto* Capsule=Player->GetCapsuleComponent();const FVector At=Player->GetActorLocation(),Bottom=At-FVector(0,0,Capsule->GetScaledCapsuleHalfHeight());
    Row->SetStringField(TEXT("actual_location"),At.ToString());Row->SetStringField(TEXT("actual_feet"),Bottom.ToString());Row->SetStringField(TEXT("actual_velocity"),Player->GetVelocity().ToString());Row->SetStringField(TEXT("actual_rotation"),Player->GetActorRotation().ToString());Row->SetStringField(TEXT("actual_capsule_up"),Capsule->GetUpVector().ToString());Row->SetNumberField(TEXT("actual_capsule_half"),Capsule->GetScaledCapsuleHalfHeight());Row->SetNumberField(TEXT("actual_capsule_radius"),Capsule->GetScaledCapsuleRadius());Row->SetNumberField(TEXT("actual_movement_mode"),int32(Movement->MovementMode));Row->SetNumberField(TEXT("actual_custom_movement_mode"),Movement->CustomMovementMode);Row->SetStringField(TEXT("actual_movement_base"),GetPathNameSafe(Player->GetMovementBase()));Row->SetBoolField(TEXT("actual_grounded"),Movement->IsMovingOnGround());Row->SetBoolField(TEXT("movement_tick_enabled"),Movement->IsComponentTickEnabled());Row->SetBoolField(TEXT("movement_active"),Movement->IsActive());Row->SetStringField(TEXT("movement_updated_component"),GetPathNameSafe(Movement->UpdatedComponent));Row->SetNumberField(TEXT("actual_gravity_Z"),Movement->GetGravityZ());Row->SetBoolField(TEXT("actual_capsule_collision_enabled"),Capsule->IsQueryCollisionEnabled());Row->SetBoolField(TEXT("actual_actor_collision_enabled"),Player->GetActorEnableCollision());Row->SetBoolField(TEXT("actual_capsule_simulating_physics"),Capsule->IsSimulatingPhysics());Row->SetNumberField(TEXT("actual_capsule_WorldStatic_response"),int32(Capsule->GetCollisionResponseToChannel(ECC_WorldStatic)));Row->SetNumberField(TEXT("actual_actor_time_dilation"),Player->CustomTimeDilation);Row->SetStringField(TEXT("actual_controller"),GetPathNameSafe(Player->GetController()));
    if(const auto* Participant=Mode->Model.FindParticipant(0)){Row->SetBoolField(TEXT("actual_alive"),Participant->bAlive);Row->SetNumberField(TEXT("actual_team"),int32(Participant->Team));Row->SetNumberField(TEXT("actual_frozen_seconds"),Participant->FrozenSeconds);}
    if(auto* Volume=Player->GetPhysicsVolume()){Row->SetStringField(TEXT("actual_physics_volume"),Volume->GetPathName());Row->SetBoolField(TEXT("actual_water_volume"),Volume->bWaterVolume);}
    auto AddFloor=[&](const TCHAR* Name,const FFindFloorResult& Floor)
    {auto F=MakeShared<FJsonObject>();F->SetBoolField(TEXT("blocking"),Floor.bBlockingHit);F->SetBoolField(TEXT("walkable"),Floor.IsWalkableFloor());F->SetBoolField(TEXT("line_trace"),Floor.bLineTrace);F->SetNumberField(TEXT("floor_distance"),Floor.FloorDist);F->SetNumberField(TEXT("line_distance"),Floor.LineDist);F->SetStringField(TEXT("component"),GetPathNameSafe(Floor.HitResult.GetComponent()));F->SetStringField(TEXT("impact"),Floor.HitResult.ImpactPoint.ToString());F->SetStringField(TEXT("normal"),Floor.HitResult.ImpactNormal.ToString());F->SetBoolField(TEXT("start_penetrating"),Floor.HitResult.bStartPenetrating);Row->SetObjectField(Name,F);};
    AddFloor(TEXT("current_movement_floor"),Movement->CurrentFloor);FFindFloorResult Actual;Movement->FindFloor(At,Actual,false);AddFloor(TEXT("actual_native_FindFloor"),Actual);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BioWaterActualSupport),false,Player);TArray<AActor*> Attached;Player->GetAttachedActors(Attached,true,true);Query.AddIgnoredActors(Attached);FHitResult Ray;const bool Hit=GetWorld()->LineTraceSingleByChannel(Ray,Bottom+FVector(0,0,25),Bottom-FVector(0,0,150),ECC_Visibility,Query);Row->SetBoolField(TEXT("actual_support_ray_hit"),Hit);if(Hit){Row->SetStringField(TEXT("actual_support_ray_component"),GetPathNameSafe(Ray.GetComponent()));Row->SetStringField(TEXT("actual_support_ray_impact"),Ray.ImpactPoint.ToString());Row->SetStringField(TEXT("actual_support_ray_normal"),Ray.ImpactNormal.ToString());Row->SetNumberField(TEXT("actual_feet_ray_gap"),Bottom.Z-Ray.ImpactPoint.Z);}
    return Row;
}
void AHeliosBioIterationQA::CaptureWaterFrame(const TCHAR* Phase)
{
    auto* Player=Mode->Player.Get();auto* PC=Cast<APlayerController>(Player->GetController());const auto& Camera=PC->PlayerCameraManager->GetCameraCacheView();
    auto Capture=MakeShared<FJsonObject>();const FString Filename=FString::Printf(TEXT("water_view_%d_%s.png"),VisualViewIndex,Phase);PendingVisualFile=Out/Filename;
    Capture->SetStringField(TEXT("phase"),Phase);Capture->SetStringField(TEXT("filename"),Filename);Capture->SetNumberField(TEXT("world_seconds"),GetWorld()->GetTimeSeconds());Capture->SetNumberField(TEXT("wall_elapsed"),FPlatformTime::Seconds()-Started);Capture->SetBoolField(TEXT("paused"),GetWorld()->IsPaused());Capture->SetStringField(TEXT("actual_camera_location"),Camera.Location.ToString());Capture->SetStringField(TEXT("actual_camera_rotation"),Camera.Rotation.ToString());Capture->SetNumberField(TEXT("actual_camera_horizontal_FOV"),Camera.FOV);Capture->SetNumberField(TEXT("actual_FP_horizontal_FOV"),Camera.FirstPersonFOV);Capture->SetNumberField(TEXT("actual_eye_above_feet_cm"),Camera.Location.Z-Feet().Z);Capture->SetBoolField(TEXT("actual_grounded"),Player->GetCharacterMovement()->IsMovingOnGround());Capture->SetStringField(TEXT("actual_movement_base"),GetPathNameSafe(Player->GetMovementBase()));Capture->SetStringField(TEXT("actual_material"),GetPathNameSafe(WaterComponent->GetMaterial(WaterMaterialSlot)));Capture->SetBoolField(TEXT("transient_Fresnel_zero_counterfactual"),WaterComponent->GetMaterial(WaterMaterialSlot)==WaterMaterialOverride);
    FHitResult Contact;FCollisionQueryParams Ground(SCENE_QUERY_STAT(BioWaterViewGround),false,Player);const bool ContactHit=GetWorld()->LineTraceSingleByChannel(Contact,Feet()+FVector(0,0,20),Feet()-FVector(0,0,50),ECC_Visibility,Ground);Capture->SetBoolField(TEXT("actual_original_floor_hit"),ContactHit);if(ContactHit){Capture->SetStringField(TEXT("actual_floor_component"),GetPathNameSafe(Contact.GetComponent()));Capture->SetNumberField(TEXT("actual_capsule_floor_gap_cm"),Feet().Z-Contact.ImpactPoint.Z);}
    int32 OccludedProbes=0;TArray<TSharedPtr<FJsonValue>> Probes;
    for(const auto& T:WaterTriangles)
    {
        if((WaterRed[T.X]+WaterRed[T.Y]+WaterRed[T.Z])/3<.35f)continue;
        // Multiple strictly interior barycentric samples improve the genuine
        // water ROI without including shoreline silhouettes or vertex.R=0.
        for(const FVector& B:{FVector(1./3,1./3,1./3),FVector(.6,.2,.2),FVector(.2,.6,.2),FVector(.2,.2,.6)})
        {
            const FVector Target=WaterVertices[T.X]*B.X+WaterVertices[T.Y]*B.Y+WaterVertices[T.Z]*B.Z;FVector2D Screen;
            if(!OriginalWaterRayVisible(Camera.Location,Target)){++OccludedProbes;continue;}if(!PC->ProjectWorldLocationToScreen(Target,Screen))continue;
            auto P=MakeShared<FJsonObject>();P->SetStringField(TEXT("original_world_point"),Target.ToString());P->SetArrayField(TEXT("screen"),{MakeShared<FJsonValueNumber>(Screen.X),MakeShared<FJsonValueNumber>(Screen.Y)});P->SetNumberField(TEXT("pixel_radius"),3);P->SetBoolField(TEXT("original_collision_visibility_clear"),true);Probes.Add(MakeShared<FJsonValueObject>(P));
        }
    }
    Capture->SetNumberField(TEXT("original_collision_occluded_water_probes"),OccludedProbes);Capture->SetArrayField(TEXT("visible_water_probes"),Probes);auto Captures=CurrentWaterView->GetArrayField(TEXT("captures"));Captures.Add(MakeShared<FJsonValueObject>(Capture));CurrentWaterView->SetArrayField(TEXT("captures"),Captures);
    FScreenshotRequest::RequestScreenshot(PendingVisualFile,false,false);VisualNext=FPlatformTime::Seconds()+.3;
}
void AHeliosBioIterationQA::RestoreVisualFixture()
{
    if(WaterComponent.IsValid()&&OriginalWaterMaterial)WaterComponent->SetMaterial(WaterMaterialSlot,OriginalWaterMaterial);
    if(VisualPlatform)VisualPlatform->SetRouteRunning(bPlatformWasRunning);
    if(GetWorld()&&GetWorld()->IsPaused())UGameplayStatics::SetGamePaused(GetWorld(),false);
}
void AHeliosBioIterationQA::EndPlay(const EEndPlayReason::Type Reason)
{RestoreVisualFixture();Super::EndPlay(Reason);}
void AHeliosBioIterationQA::FinishVisualFixture(const FString& Why)
{
    if(bFinished)return;RestoreVisualFixture();bFinished=true;
    auto Report=MakeShared<FJsonObject>();const bool Complete=Why.IsEmpty()&&(!bWaterMode||WaterViews.Num()==3)&&bSolesMeasured&&bSolesAligned;
    Report->SetStringField(TEXT("schema"),TEXT("OriginalWaterVisualRuntime.v1"));Report->SetBoolField(TEXT("passed"),Complete);Report->SetStringField(TEXT("blocker"),Why);Report->SetBoolField(TEXT("fixture_mode"),true);Report->SetBoolField(TEXT("natural_round_verified"),false);Report->SetBoolField(TEXT("physical_traversal_verified"),false);Report->SetBoolField(TEXT("rendered_visual_acceptance"),false);Report->SetBoolField(TEXT("GPU_screenshots_recorded"),bWaterMode&&WaterViews.Num()==3);Report->SetBoolField(TEXT("actual_CPU_skin_soles_measured"),bSolesMeasured);Report->SetBoolField(TEXT("measured_standing_soles_within_3cm"),bSolesAligned);Report->SetBoolField(TEXT("original_material_restored"),!OriginalWaterMaterial||(WaterComponent.IsValid()&&WaterComponent->GetMaterial(WaterMaterialSlot)==OriginalWaterMaterial));Report->SetStringField(TEXT("map"),GetWorld()->GetOutermost()->GetName());Report->SetStringField(TEXT("expected_map"),ExpectedMap);Report->SetArrayField(TEXT("views"),WaterViews);Report->SetArrayField(TEXT("sole_evidence"),SoleEvidence);Report->SetArrayField(TEXT("visual_diagnostics"),VisualDiagnostics);
    if(CurrentWaterView.IsValid()){CurrentWaterView->SetObjectField(TEXT("actual_state_at_finish"),WaterSupportEvidence(TEXT("finish_incomplete_view")));Report->SetObjectField(TEXT("unfinished_view"),CurrentWaterView);}
    Report->SetStringField(TEXT("scope"),TEXT("Explicit isolated rendered fixture. Camera is placed at a source spawn, measured original static shore and actual original platform after real route travel; final platform view temporarily holds the route. Native gravity must establish support. Original material flow advances four real game seconds, then a paused repeat estimates temporal noise and a transient Fresnel-zero MID probes shader response before restoration. Screen ROI records source interior water points with actual world collision visibility. No map/material asset is saved. CPU skin foot-weight vertices are measured in actual TP poses against original collision; capsule bottoms and bone centers cannot substitute for shoe soles. Numeric frame differences are separate from human visual-fidelity review and from natural gameplay/traversal acceptance."));
    if(OriginalWaterMaterial)
    {
        Report->SetStringField(TEXT("original_material"),OriginalWaterMaterial->GetPathName());auto* Material=OriginalWaterMaterial->GetMaterial();auto* Resource=Material?Material->GetMaterialResource(GMaxRHIShaderPlatform):nullptr;Report->SetBoolField(TEXT("actual_shader_map_exists"),Resource&&Resource->GetGameThreadShaderMap());const auto Shader=WaterShaderEvidence(OriginalWaterMaterial,GetWorld()->GetFeatureLevel());Report->SetBoolField(TEXT("actual_shader_compilation_finished"),Shader->GetBoolField(TEXT("actual_shader_compilation_finished")));Report->SetObjectField(TEXT("actual_shader_diagnostic"),Shader);Report->SetBoolField(TEXT("actual_shader_map_complete"),Resource&&Resource->IsGameThreadShaderMapComplete());
        TArray<UTexture*> Textures;OriginalWaterMaterial->GetUsedTextures(Textures);TArray<TSharedPtr<FJsonValue>> Used;for(auto* Texture:Textures)Used.Add(MakeShared<FJsonValueString>(GetPathNameSafe(Texture)));Report->SetArrayField(TEXT("actual_used_textures"),Used);
        auto Params=MakeShared<FJsonObject>();for(const TCHAR* Name:{TEXT("WaterSpeed"),TEXT("DetailSpeed"),TEXT("FlowStrength"),TEXT("FresnelStrength"),TEXT("FresnelPower"),TEXT("Opacity")}){float Value=0;const bool Found=OriginalWaterMaterial->GetScalarParameterValue(FHashedMaterialParameterInfo(FName(Name)),Value);if(Found)Params->SetNumberField(Name,Value);else Params->SetStringField(Name,TEXT("not_found"));}Report->SetObjectField(TEXT("actual_source_water_parameters"),Params);
    }
    FString Text;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Out/(bWaterMode?TEXT("water_visual_report.json"):TEXT("skin_sole_report.json"))));UE_LOG(LogTemp,Display,TEXT("BIO_ITERATION_VISUAL_FINISHED complete=%d soles_aligned=%d blocker=%s"),Complete,bSolesAligned,*Why);FPlatformMisc::RequestExit(false);
}
void AHeliosBioIterationQA::TickVisualFixture(float Dt)
{
    const double Now=FPlatformTime::Seconds();
    if(VisualStage!=LastVisualStage||VisualViewIndex!=LastVisualView||VisualPhase!=LastVisualPhase||Now-LastVisualLog>=2)
    {UE_LOG(LogTemp,Display,TEXT("BIO_WATER_VISUAL_STAGE elapsed=%.2f frame=%llu view=%d stage=%d phase=%d initialized=%d soles=%d platform=%s"),Now-Started,static_cast<unsigned long long>(GFrameCounter),VisualViewIndex,VisualStage,VisualPhase,bWaterInitialized,SoleSamples,VisualPlatform?*VisualPlatform->GetActorLocation().ToString():TEXT("unavailable"));LastVisualStage=VisualStage;LastVisualView=VisualViewIndex;LastVisualPhase=VisualPhase;LastVisualLog=Now;}
    if(Now-Started>150){FinishVisualFixture(FString::Printf(TEXT("Bounded visual fixture timeout view=%d stage=%d phase=%d"),VisualViewIndex,VisualStage,VisualPhase));return;}
    if(!bWaterInitialized)
    {
        Mode=AHeliosBioHotelGameMode::Find(GetWorld());if(!Mode||!Mode->Player||!Mode->Player->GetEquippedWeapon())return;
        if(Out.IsEmpty()||ExpectedMap.IsEmpty()||GetWorld()->GetOutermost()->GetName()!=ExpectedMap){FinishVisualFixture(TEXT("Expected actual map/output missing or mismatched"));return;}
        if(FParse::Param(FCommandLine::Get(),TEXT("NullRHI"))&&bWaterMode){FinishVisualFixture(TEXT("Actual GPU required; NullRHI cannot witness water"));return;}
        if(bWaterMode&&!LoadWaterWitness()){FinishVisualFixture(TEXT("Exact original water geometry/material/native platform unavailable"));return;}
        Mode->Model.Config.PreparationSeconds=.2f;Mode->Model.Config.RoundSeconds=1200;Mode->Model.Config.SupplyIntervalSeconds=10000;Mode->Model.Config.bKeepLocalPlayerHuman=true;Mode->RestartBioRound();FreezeIsolation();bWaterInitialized=true;VisualNext=Now+2;return;
    }
    FreezeIsolation();if(Now<VisualNext)return;
    if(Mode->Model.GetPhase()!=EHeliosBioPhase::Active&&!GetWorld()->IsPaused())return;
    if(SoleSamples<3){SampleActualSoles();VisualNext=Now+.5;if(!bWaterMode&&SoleSamples>=3)FinishVisualFixture(bSolesMeasured?FString():TEXT("No complete standing CPU-skinned sole measurement"));return;}
    if(!bWaterMode){FinishVisualFixture();return;}
    if(VisualStage==0)
    {
        if(!PlaceWaterCamera(VisualViewIndex)){if(VisualViewIndex<2)FinishVisualFixture(TEXT("No physically clear original camera candidate with 3 visible water triangles; see recorded rejection counts and first hits"));else VisualNext=Now+.2;return;}
        VisualStage=1;VisualAt=Now;VisualNext=Now+.5;return;
    }
    if(VisualStage==1)
    {
        auto* Player=Mode->Player.Get();FVector RequestedFeet;RequestedFeet.InitFromString(CurrentWaterView->GetStringField(TEXT("fixture_requested_feet")));
        const auto& NativeFloor=Player->GetCharacterMovement()->CurrentFloor;
        const bool Supported=Player->GetCharacterMovement()->IsMovingOnGround()&&NativeFloor.IsWalkableFloor()&&GetPathNameSafe(NativeFloor.HitResult.GetComponent())==CurrentWaterView->GetStringField(TEXT("fixture_original_support"))&&FVector::Dist2D(Feet(),RequestedFeet)<=3.f&&FMath::Abs(Feet().Z-RequestedFeet.Z)<=3.f&&(VisualViewIndex!=2||Player->GetMovementBase()==VisualPlatform->GetPlatformMesh());
        if(Now-LastWaterSupportLog>=1){const auto State=WaterSupportEvidence(TEXT("waiting_for_native_landing"));VisualDiagnostics.Add(MakeShared<FJsonValueObject>(State));CurrentWaterView->SetObjectField(TEXT("last_native_support_state"),State);UE_LOG(LogTemp,Display,TEXT("BIO_WATER_NATIVE_SUPPORT view=%d elapsed=%.3f location=%s velocity=%s mode=%d grounded=%d base=%s native_floor=%d support=%s"),VisualViewIndex,Now-VisualAt,*Player->GetActorLocation().ToString(),*Player->GetVelocity().ToString(),int32(Player->GetCharacterMovement()->MovementMode),Player->GetCharacterMovement()->IsMovingOnGround(),*GetPathNameSafe(Player->GetMovementBase()),Player->GetCharacterMovement()->CurrentFloor.IsWalkableFloor(),*GetPathNameSafe(Player->GetCharacterMovement()->CurrentFloor.HitResult.GetComponent()));LastWaterSupportLog=Now;}
        if(!Supported){if(Now-VisualAt>5)FinishVisualFixture(TEXT("Fixture camera failed actual original floor/native base landing"));return;}
        if(VisualViewIndex==2)Player->Crouch();VisualStage=2;VisualNext=Now+.5;return;
    }
    if(VisualStage==2)
    {
        const auto Shader=WaterShaderEvidence(OriginalWaterMaterial,GetWorld()->GetFeatureLevel());VisualDiagnostics.Add(MakeShared<FJsonValueObject>(Shader));
        if(!Shader->GetBoolField(TEXT("render_thread_uses_original_proxy"))||!Shader->GetBoolField(TEXT("render_thread_shader_map_complete")))
        {if(Now-VisualAt>15){FinishVisualFixture(TEXT("Actual renderer still uses fallback/incomplete water shader; see shader diagnostics"));return;}VisualNext=Now+1;return;}
        FlowTimeStart=GetWorld()->GetTimeSeconds();CaptureWaterFrame(TEXT("flow_t0"));VisualStage=3;VisualPhase=0;return;
    }
    if(VisualStage==3)
    {
        if(IFileManager::Get().FileSize(*PendingVisualFile)<=0){if(Now-VisualNext>5)FinishVisualFixture(TEXT("GPU screenshot request produced no PNG"));return;}
        if(VisualPhase==0)
        {
            if(GetWorld()->GetTimeSeconds()-FlowTimeStart<4)return;
            if(!UGameplayStatics::SetGamePaused(GetWorld(),true)){FinishVisualFixture(TEXT("Paused temporal control unavailable"));return;}
            VisualStage=4;VisualNext=Now+.5;return;
        }
        if(VisualPhase==1){CaptureWaterFrame(TEXT("paused_control_repeat"));VisualPhase=2;return;}
        if(VisualPhase==2)
        {
            WaterMaterialOverride=UMaterialInstanceDynamic::Create(OriginalWaterMaterial,this);if(!WaterMaterialOverride){FinishVisualFixture(TEXT("Transient Fresnel probe material unavailable"));return;}
            WaterMaterialOverride->SetScalarParameterValue(TEXT("FresnelStrength"),0);WaterComponent->SetMaterial(WaterMaterialSlot,WaterMaterialOverride);VisualStage=5;VisualNext=Now+.5;return;
        }
        if(VisualPhase==3)
        {
            WaterComponent->SetMaterial(WaterMaterialSlot,OriginalWaterMaterial);WaterMaterialOverride=nullptr;UGameplayStatics::SetGamePaused(GetWorld(),false);WaterViews.Add(MakeShared<FJsonValueObject>(CurrentWaterView));CurrentWaterView.Reset();
            if(++VisualViewIndex>=3){FinishVisualFixture();return;}VisualStage=0;VisualNext=Now+.3;return;
        }
    }
    if(VisualStage==4){CaptureWaterFrame(TEXT("flow_t1_paused"));VisualPhase=1;VisualStage=3;return;}
    if(VisualStage==5){CaptureWaterFrame(TEXT("paused_fresnel_zero"));VisualPhase=3;VisualStage=3;return;}
}
void AHeliosBioIterationQA::FinishWaterLanding(const FString& Why)
{
    if(bFinished)return;bFinished=true;if(Mode&&Mode->Player){Mode->Player->StopJumping();Mode->Player->ConsumeMovementInputVector();}
    auto Report=MakeShared<FJsonObject>();Report->SetStringField(TEXT("schema"),TEXT("OriginalWater3211NativeLanding.v1"));Report->SetBoolField(TEXT("passed"),Why.IsEmpty()&&WaterLandingIndex==3);Report->SetStringField(TEXT("blocker"),Why);Report->SetBoolField(TEXT("fixture_mode"),true);Report->SetBoolField(TEXT("natural_round_verified"),false);Report->SetBoolField(TEXT("physical_traversal_verified"),false);Report->SetBoolField(TEXT("rendered_visual_acceptance"),false);Report->SetNumberField(TEXT("completed_native_fall_jump_land_samples"),WaterLandingIndex);Report->SetStringField(TEXT("source_collider_key"),TEXT("buildplayer-2_pvp_bio_shjzt:3211"));Report->SetStringField(TEXT("map"),GetWorld()->GetOutermost()->GetName());Report->SetStringField(TEXT("expected_map"),ExpectedMap);Report->SetNumberField(TEXT("source_water_top_Z"),WaterCollisionMax.Z);Report->SetArrayField(TEXT("actual_native_samples"),WaterLandingEvidence);
    if(Mode&&Mode->Player)Report->SetObjectField(TEXT("actual_final_state"),WaterSupportEvidence(TEXT("native_water_landing_finish")));
    Report->SetStringField(TEXT("scope"),TEXT("Explicit isolated physics regression on three collision-clear points of original Water3211. Each fixture places feet 30cm above the actual source top in Falling, requires actual gravity/grounded original base and native floor contact, calls public Character Jump and waits for real upward flight and same-layer landing. No SetWalking, LaunchCharacter, velocity assignment, replacement support, platform control or source geometry mutation. Not natural-round or platform-traversal acceptance."));
    FString Text;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Out/TEXT("water_landing_report.json")));UE_LOG(LogTemp,Display,TEXT("BIO_WATER_NATIVE_LANDING_FINISHED samples=%d success=%d blocker=%s"),WaterLandingIndex,Why.IsEmpty()&&WaterLandingIndex==3,*Why);FPlatformMisc::RequestExit(false);
}
void AHeliosBioIterationQA::TickWaterLanding(float Dt)
{
    const double Now=FPlatformTime::Seconds();
    if(!bWaterInitialized)
    {
        Mode=AHeliosBioHotelGameMode::Find(GetWorld());if(!Mode||!Mode->Player||!Mode->Player->GetEquippedWeapon())return;
        WaterLandingStarted=Now;
        if(Out.IsEmpty()||GetWorld()->GetOutermost()->GetName()!=ExpectedMap||!LoadWaterWitness()){FinishWaterLanding(TEXT("Exact source Water3211 witness or requested map/output unavailable"));return;}
        Mode->Model.Config.PreparationSeconds=.2f;Mode->Model.Config.RoundSeconds=1200;Mode->Model.Config.SupplyIntervalSeconds=10000;Mode->Model.Config.bKeepLocalPlayerHuman=true;Mode->RestartBioRound();FreezeIsolation();bWaterInitialized=true;VisualNext=Now+.7;return;
    }
    if(Now-WaterLandingStarted>15){FinishWaterLanding(FString::Printf(TEXT("Bounded native water landing timeout sample=%d stage=%d"),WaterLandingIndex,VisualStage));return;}
    FreezeIsolation();if(Now<VisualNext||Mode->Model.GetPhase()!=EHeliosBioPhase::Active)return;
    auto* Player=Mode->Player.Get();auto* Movement=Player->GetCharacterMovement();const auto* Capsule=Player->GetCapsuleComponent();const float Half=Capsule->GetScaledCapsuleHalfHeight(),Radius=Capsule->GetScaledCapsuleRadius();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BioOriginalWaterNativeLanding),false,Player);Query.bReturnFaceIndex=true;TArray<AActor*> Attached;Player->GetAttachedActors(Attached,true,true);Query.AddIgnoredActors(Attached);FCollisionObjectQueryParams Static(ECC_WorldStatic);
    auto SourceTop=[&](const FVector& At,FHitResult& Hit)
    {
        if(!GetWorld()->LineTraceSingleByObjectType(Hit,FVector(At.X,At.Y,WaterCollisionMax.Z+100),FVector(At.X,At.Y,WaterCollisionMin.Z-20),Static,Query))return false;
        const auto* Mesh=Cast<UStaticMeshComponent>(Hit.GetComponent());return Mesh&&Mesh->GetStaticMesh()&&Mesh->GetStaticMesh()->GetName()==TEXT("SM_BioPyramidFloatingCollision")&&FMath::Abs(Hit.ImpactPoint.Z-WaterCollisionMax.Z)<=.05f&&Hit.ImpactNormal.Z>.999f&&Hit.ImpactPoint.X>=WaterCollisionMin.X&&Hit.ImpactPoint.X<=WaterCollisionMax.X&&Hit.ImpactPoint.Y>=WaterCollisionMin.Y&&Hit.ImpactPoint.Y<=WaterCollisionMax.Y;
    };
    if(WaterLandingPoints.IsEmpty())
    {
        TArray<FVector> Candidates=Mode->SpawnPoints;
        for(float X:{.2f,.4f,.6f,.8f})for(float Y:{.2f,.4f,.6f,.8f})Candidates.Add(FVector(FMath::Lerp(WaterCollisionMin.X,WaterCollisionMax.X,X),FMath::Lerp(WaterCollisionMin.Y,WaterCollisionMax.Y,Y),WaterCollisionMax.Z));
        for(const FVector& At:Candidates)
        {
            FHitResult Hit;if(!SourceTop(At,Hit))continue;bool Separate=true;for(const auto& P:WaterLandingPoints)Separate&=FVector::Dist2D(P,Hit.ImpactPoint)>100.f;if(!Separate)continue;
            if(GetWorld()->OverlapBlockingTestByChannel(Hit.ImpactPoint+FVector(0,0,Half+30),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Radius,Half),Query)||GetWorld()->OverlapBlockingTestByChannel(Hit.ImpactPoint+FVector(0,0,Half+2+130),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Radius,Half),Query))continue;
            WaterLandingPoints.Add(Hit.ImpactPoint);if(WaterLandingPoints.Num()==3)break;
        }
        if(WaterLandingPoints.Num()!=3){FinishWaterLanding(TEXT("Three real original Water3211 capsule-clear fall/jump positions were not found"));return;}
    }
    if(VisualStage==0)
    {
        FHitResult Hit;if(!SourceTop(WaterLandingPoints[WaterLandingIndex],Hit)){FinishWaterLanding(TEXT("Original Water3211 top disappeared before actual sample"));return;}
        WaterLandingFeet=Hit.ImpactPoint;WaterLandingSupport=Hit.GetComponent();Player->StopBioFire();Player->UnCrouch();Movement->StopMovementImmediately();Player->ConsumeMovementInputVector();Player->SetActorRotation(FRotator(0,Player->GetActorRotation().Yaw,0));Player->SetActorLocation(WaterLandingFeet+FVector(0,0,Half+30),false,nullptr,ETeleportType::TeleportPhysics);Movement->SetMovementMode(MOVE_Falling);bWaterInitialFalling=bWaterJumpAirborne=false;WaterLandingStartZ=WaterLandingFeet.Z;WaterLandingMaxZ=Feet().Z;
        auto E=WaterSupportEvidence(TEXT("explicit_original_water_fall_fixture"));E->SetNumberField(TEXT("sample"),WaterLandingIndex);E->SetStringField(TEXT("source_original_top"),Hit.ImpactPoint.ToString());E->SetStringField(TEXT("source_original_top_hit_normal"),Hit.ImpactNormal.ToString());E->SetNumberField(TEXT("actual_source_collision_face_index"),Hit.FaceIndex);E->SetBoolField(TEXT("actual_fall_and_jump_capsule_positions_clear"),true);E->SetNumberField(TEXT("actual_JumpZVelocity"),Movement->JumpZVelocity);E->SetNumberField(TEXT("actual_GravityZ"),Movement->GetGravityZ());WaterLandingEvidence.Add(MakeShared<FJsonValueObject>(E));VisualStage=1;VisualAt=Now;return;
    }
    const FVector Bottom=Feet();FHitResult Hit;const bool HasSource=SourceTop(Bottom,Hit);const float Gap=HasSource?Bottom.Z-Hit.ImpactPoint.Z:MAX_flt;const bool Supported=Movement->IsMovingOnGround()&&Movement->CurrentFloor.IsWalkableFloor()&&Movement->CurrentFloor.HitResult.GetComponent()==WaterLandingSupport.Get()&&Player->GetMovementBase()==WaterLandingSupport.Get()&&HasSource&&FMath::Abs(Gap-2.f)<1.f&&FVector::Dist2D(Bottom,WaterLandingFeet)<10.f;
    if(VisualStage==1)
    {
        bWaterInitialFalling|=Movement->IsFalling()&&Player->GetVelocity().Z<-.1f;
        if(Supported&&bWaterInitialFalling)
        {
            auto E=WaterSupportEvidence(TEXT("actual_native_gravity_landed_on_original_water"));E->SetNumberField(TEXT("sample"),WaterLandingIndex);E->SetBoolField(TEXT("actual_initial_falling_observed"),true);WaterLandingEvidence.Add(MakeShared<FJsonValueObject>(E));WaterLandingStartZ=Bottom.Z;WaterLandingMaxZ=Bottom.Z;Player->Jump();VisualStage=2;VisualAt=Now;return;
        }
        if(Now-VisualAt>3){FinishWaterLanding(TEXT("Production native Falling did not land on original Water3211; no SetWalking fallback"));return;}
    }
    else if(VisualStage==2)
    {
        WaterLandingMaxZ=FMath::Max(WaterLandingMaxZ,float(Bottom.Z));bWaterJumpAirborne|=Movement->IsFalling()&&Player->GetVelocity().Z>1.f;
        if(bWaterJumpAirborne)Player->StopJumping();
        if(Supported&&bWaterJumpAirborne&&WaterLandingMaxZ-WaterLandingStartZ>20.f)
        {
            auto E=WaterSupportEvidence(TEXT("public_native_Jump_airborne_and_same_source_water_landing"));E->SetNumberField(TEXT("sample"),WaterLandingIndex);E->SetNumberField(TEXT("actual_jump_start_feet_Z"),WaterLandingStartZ);E->SetNumberField(TEXT("actual_maximum_feet_Z"),WaterLandingMaxZ);E->SetNumberField(TEXT("actual_native_rise_cm"),WaterLandingMaxZ-WaterLandingStartZ);E->SetNumberField(TEXT("actual_JumpZVelocity"),Movement->JumpZVelocity);E->SetNumberField(TEXT("actual_GravityZ"),Movement->GetGravityZ());E->SetNumberField(TEXT("native_unobstructed_ballistic_rise_cm"),FMath::Square(Movement->JumpZVelocity)/(-2.f*Movement->GetGravityZ()));E->SetBoolField(TEXT("public_Jump_and_actual_upward_airborne_observed"),true);E->SetBoolField(TEXT("same_original_collision_native_landing"),true);WaterLandingEvidence.Add(MakeShared<FJsonValueObject>(E));
            UE_LOG(LogTemp,Display,TEXT("BIO_WATER_NATIVE_LANDING_SAMPLE sample=%d feet=%s rise=%.3f gap=%.3f base=%s"),WaterLandingIndex,*Bottom.ToString(),WaterLandingMaxZ-WaterLandingStartZ,Gap,*GetPathNameSafe(Player->GetMovementBase()));if(++WaterLandingIndex==3){FinishWaterLanding();return;}VisualStage=0;VisualNext=Now+.1;
        }
        else if(Now-VisualAt>3){FinishWaterLanding(TEXT("Public native Jump did not produce real upward flight and same original-water landing"));return;}
    }
}
