#include "QA/HeliosBioNativeInputQA.h"
#include "Bio/HeliosBioHotelGameMode.h"
#include "Bio/HeliosBioPlayer.h"
#include "Bio/HeliosBioRoundInventoryComponent.h"
#include "Bio/HeliosBioSupplyWeapon.h"
#include "ShooterWeapon.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraTypes.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "InputCoreTypes.h"

AHeliosBioNativeInputQA::AHeliosBioNativeInputQA()
{
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;
    PrimaryActorTick.TickGroup=TG_PostUpdateWork;
}
void AHeliosBioNativeInputQA::BeginPlay()
{
    Super::BeginPlay();StartedWall=FPlatformTime::Seconds();NextSnapshotWall=StartedWall;
    FParse::Value(FCommandLine::Get(),TEXT("BioNativeInputQAOut="),Out);
    FParse::Value(FCommandLine::Get(),TEXT("BioExpectedMap="),ExpectedMap);
    FParse::Value(FCommandLine::Get(),TEXT("BioNativeInputSeconds="),ObservationSeconds);ObservationSeconds=FMath::Clamp(ObservationSeconds,1.f,300.f);
    bFixtureMode=FParse::Param(FCommandLine::Get(),TEXT("BioManualHunterChoice"))||FParse::Param(FCommandLine::Get(),TEXT("HeliosBioHotelQA"))||FParse::Param(FCommandLine::Get(),TEXT("HeliosBioRoleQA"))||FParse::Param(FCommandLine::Get(),TEXT("BioHumanActionsQA"))||FParse::Param(FCommandLine::Get(),TEXT("BioSupplyWeaponQA"));
    if(Out.IsEmpty()){bIOError=true;UE_LOG(LogTemp,Error,TEXT("BIO_NATIVE_INPUT_OBSERVER missing output path"));Finish(TEXT("missing_output_path"),true);return;}
    Out=FPaths::ConvertRelativePathToFull(Out);IFileManager::Get().MakeDirectory(*Out,true);
    Append(Snapshot(TEXT("observer_started")));
    UE_LOG(LogTemp,Display,TEXT("BIO_NATIVE_INPUT_OBSERVER_READY seconds=%.1f fixture=%d read_only=1 out=%s"),ObservationSeconds,bFixtureMode,*Out);
}
TSharedRef<FJsonObject> AHeliosBioNativeInputQA::Snapshot(const TCHAR* Kind) const
{
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),Kind);Row->SetNumberField(TEXT("wall_elapsed_seconds"),FPlatformTime::Seconds()-StartedWall);Row->SetNumberField(TEXT("world_seconds"),GetWorld()->GetTimeSeconds());Row->SetStringField(TEXT("frame"),FString::Printf(TEXT("%llu"),static_cast<unsigned long long>(GFrameCounter)));Row->SetStringField(TEXT("map"),GetWorld()->GetOutermost()->GetName());Row->SetStringField(TEXT("expected_map"),ExpectedMap);Row->SetBoolField(TEXT("fixture_mode"),bFixtureMode);Row->SetBoolField(TEXT("paused"),GetWorld()->IsPaused());
    auto* PC=UGameplayStatics::GetPlayerController(GetWorld(),0);auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());auto* Player=Mode?Mode->Player.Get():nullptr;
    Row->SetBoolField(TEXT("controller_available"),PC!=nullptr);Row->SetBoolField(TEXT("bio_player_available"),Player!=nullptr);
    if(PC){Row->SetBoolField(TEXT("mouse_cursor_visible"),PC->bShowMouseCursor);Row->SetStringField(TEXT("actual_control_rotation"),PC->GetControlRotation().ToString());float X=0,Y=0;Row->SetBoolField(TEXT("viewport_mouse_position_available"),PC->GetMousePosition(X,Y));Row->SetNumberField(TEXT("viewport_mouse_x"),X);Row->SetNumberField(TEXT("viewport_mouse_y"),Y);if(PC->PlayerCameraManager){Row->SetStringField(TEXT("actual_camera_manager_location"),PC->PlayerCameraManager->GetCameraLocation().ToString());Row->SetStringField(TEXT("actual_camera_manager_rotation"),PC->PlayerCameraManager->GetCameraRotation().ToString());}}
    if(PC)
    {
        int32 Width=0,Height=0;PC->GetViewportSize(Width,Height);Row->SetNumberField(TEXT("actual_viewport_width"),Width);Row->SetNumberField(TEXT("actual_viewport_height"),Height);
        if(PC->PlayerCameraManager)
        {
            // Read the view actually cached by production; calling CalcCamera here
            // would mutate the subject and could not prove its previously shown view.
            const auto& View=PC->PlayerCameraManager->GetCameraCacheView();const float Aspect=!View.bConstrainAspectRatio&&Width>0&&Height>0?float(Width)/float(Height):View.AspectRatio;
            const auto Vertical=[Aspect](float Horizontal){return FMath::RadiansToDegrees(2.f*FMath::Atan(FMath::Tan(FMath::DegreesToRadians(Horizontal)*.5f)/FMath::Max(.001f,Aspect)));};
            Row->SetNumberField(TEXT("actual_cached_world_horizontal_FOV"),View.FOV);Row->SetNumberField(TEXT("actual_cached_first_person_horizontal_FOV"),View.FirstPersonFOV);Row->SetNumberField(TEXT("actual_cached_first_person_scale"),View.FirstPersonScale);Row->SetBoolField(TEXT("actual_cached_use_first_person_parameters"),View.bUseFirstPersonParameters);Row->SetBoolField(TEXT("actual_cached_constrain_aspect"),View.bConstrainAspectRatio);Row->SetNumberField(TEXT("actual_cached_aspect_ratio"),View.AspectRatio);Row->SetNumberField(TEXT("observed_effective_aspect_ratio"),Aspect);Row->SetNumberField(TEXT("world_vertical_FOV_from_cached_horizontal_and_viewport"),Vertical(View.FOV));Row->SetNumberField(TEXT("first_person_vertical_FOV_from_cached_horizontal_and_viewport"),Vertical(View.FirstPersonFOV));Row->SetNumberField(TEXT("actual_cached_near_clip_cm"),View.PerspectiveNearClipPlane);Row->SetNumberField(TEXT("actual_cached_axis_constraint"),View.AspectRatioAxisConstraint.IsSet()?static_cast<int32>(View.AspectRatioAxisConstraint.GetValue()):-1);Row->SetNumberField(TEXT("actual_camera_cache_world_seconds"),PC->PlayerCameraManager->GetCameraCacheTime());
        }
    }
    if(Mode){Row->SetNumberField(TEXT("phase"),static_cast<int32>(Mode->Model.GetPhase()));Row->SetNumberField(TEXT("round_serial"),Mode->RoundSerial);Row->SetNumberField(TEXT("round_seed"),Mode->CurrentRoundSeed);Row->SetNumberField(TEXT("actual_participant_count"),Mode->Model.GetParticipants().Num());Row->SetNumberField(TEXT("configured_preparation_seconds"),Mode->Model.Config.PreparationSeconds);Row->SetNumberField(TEXT("configured_active_seconds"),Mode->Model.Config.RoundSeconds);Row->SetBoolField(TEXT("hunter_choice_menu_open"),Mode->bHunterChooserOpen);if(const auto* P=Mode->Model.FindParticipant(0)){Row->SetNumberField(TEXT("player_team"),static_cast<int32>(P->Team));Row->SetBoolField(TEXT("player_alive"),P->bAlive);Row->SetBoolField(TEXT("player_permanently_eliminated"),P->bPermanentlyDead);Row->SetNumberField(TEXT("player_HP"),P->Health);Row->SetStringField(TEXT("hunter_role"),P->HunterRoleId.ToString());Row->SetBoolField(TEXT("hunter_choice_pending"),P->bHunterChoicePending);Row->SetNumberField(TEXT("F_skill_cooldown_seconds"),P->SkillCooldownRemaining);Row->SetNumberField(TEXT("G_skill_cooldown_seconds"),P->SecondaryCooldownRemaining);Row->SetNumberField(TEXT("F_skill_remaining_seconds"),P->SkillRemaining);Row->SetNumberField(TEXT("G_skill_remaining_seconds"),P->SecondarySkillRemaining);}}
    if(Player)
    {
        auto* Capsule=Player->GetCapsuleComponent();auto* Movement=Player->GetCharacterMovement();const FVector At=Player->GetActorLocation(),Feet=At-FVector(0,0,Capsule->GetScaledCapsuleHalfHeight());FVector Eye;FRotator EyeRotation;Player->GetActorEyesViewPoint(Eye,EyeRotation);
        Row->SetStringField(TEXT("actual_player_location"),At.ToString());Row->SetStringField(TEXT("actual_velocity"),Player->GetVelocity().ToString());Row->SetStringField(TEXT("actual_actor_rotation"),Player->GetActorRotation().ToString());Row->SetNumberField(TEXT("actual_actor_yaw_degrees"),Player->GetActorRotation().Yaw);Row->SetBoolField(TEXT("falling"),Movement->IsFalling());Row->SetBoolField(TEXT("grounded"),Movement->IsMovingOnGround());Row->SetBoolField(TEXT("crouched"),Player->bIsCrouched);Row->SetNumberField(TEXT("actual_movement_mode"),static_cast<int32>(Movement->MovementMode));Row->SetNumberField(TEXT("actual_capsule_radius_cm"),Capsule->GetScaledCapsuleRadius());Row->SetNumberField(TEXT("actual_capsule_half_height_cm"),Capsule->GetScaledCapsuleHalfHeight());Row->SetStringField(TEXT("actual_capsule_feet"),Feet.ToString());Row->SetStringField(TEXT("actual_native_eye_location"),Eye.ToString());Row->SetNumberField(TEXT("actual_native_eye_above_capsule_feet_cm"),Eye.Z-Feet.Z);Row->SetStringField(TEXT("actual_native_eye_rotation"),EyeRotation.ToString());Row->SetNumberField(TEXT("actual_ammo"),Player->GetAmmo());Row->SetNumberField(TEXT("actual_reserve_ammo"),Player->GetReserveAmmo());Row->SetNumberField(TEXT("actual_reload_remaining_seconds"),Player->GetBioReloadRemaining());Row->SetNumberField(TEXT("actual_shots"),Player->ShotsFired);Row->SetNumberField(TEXT("actual_melee_attacks"),Player->MeleeAttacks);Row->SetNumberField(TEXT("spectated_participant"),Player->GetSpectatedParticipantId());
        if(auto* Inventory=Player->GetRoundInventory())
        {
            Row->SetBoolField(TEXT("actual_round_inventory_enabled"),Inventory->IsEnabled());Row->SetNumberField(TEXT("actual_inventory_active_slot"),Inventory->GetActiveSlot());
            Row->SetStringField(TEXT("actual_equipped_weapon"),GetPathNameSafe(Player->GetEquippedWeapon()));Row->SetStringField(TEXT("actual_equipped_weapon_class"),GetPathNameSafe(Player->GetEquippedWeapon()?Player->GetEquippedWeapon()->GetClass():nullptr));
            TArray<TSharedPtr<FJsonValue>> Slots;
            for(int32 I=0;I<5;++I)
            {
                auto Slot=MakeShared<FJsonObject>();auto* Weapon=Inventory->GetWeapon(I);
                Slot->SetNumberField(TEXT("slot"),I);Slot->SetBoolField(TEXT("acquired"),Weapon!=nullptr);Slot->SetStringField(TEXT("weapon_id"),Inventory->GetWeaponId(I).ToString());Slot->SetNumberField(TEXT("actual_magazine"),Weapon?Weapon->GetBulletCount():0);Slot->SetNumberField(TEXT("actual_reserve"),Inventory->GetReserve(I));
                if(auto* Native=Cast<AHeliosBioSupplyWeapon>(Weapon)){Slot->SetNumberField(TEXT("native_committed_shots"),Native->GetNativeShotCount());Slot->SetNumberField(TEXT("native_damage_or_concussion_hits"),Native->GetNativeHitCount());}
                Slots.Add(MakeShared<FJsonValueObject>(Slot));
            }
            Row->SetArrayField(TEXT("actual_round_inventory_slots"),Slots);
        }
        if(Player->FirstPersonCamera){Row->SetStringField(TEXT("actual_first_person_camera_location"),Player->FirstPersonCamera->GetComponentLocation().ToString());Row->SetStringField(TEXT("actual_first_person_camera_relative_location"),Player->FirstPersonCamera->GetRelativeLocation().ToString());Row->SetStringField(TEXT("actual_first_person_camera_rotation"),Player->FirstPersonCamera->GetComponentRotation().ToString());Row->SetNumberField(TEXT("actual_first_person_camera_above_capsule_feet_cm"),Player->FirstPersonCamera->GetComponentLocation().Z-Feet.Z);Row->SetNumberField(TEXT("actual_camera_component_world_horizontal_FOV"),Player->FirstPersonCamera->FieldOfView);Row->SetNumberField(TEXT("actual_camera_component_first_person_horizontal_FOV"),Player->FirstPersonCamera->FirstPersonFieldOfView);Row->SetNumberField(TEXT("actual_camera_component_first_person_scale"),Player->FirstPersonCamera->FirstPersonScale);}
        if(Mode){const auto& Profile=Mode->Model.Config.PlayerCamera;Row->SetBoolField(TEXT("floating_player_camera_profile_enabled"),Mode->bFloatingProfile&&Profile.bEnabled);Row->SetNumberField(TEXT("configured_player_standing_eye_cm"),Profile.StandingEyeHeight);Row->SetNumberField(TEXT("configured_player_crouched_eye_cm"),Profile.CrouchedEyeHeight);Row->SetNumberField(TEXT("configured_player_crouched_half_cm"),Profile.CrouchedCapsuleHalfHeight);Row->SetNumberField(TEXT("actual_crouched_half_cm"),Movement->GetCrouchedHalfHeight());Row->SetNumberField(TEXT("configured_world_vertical_FOV"),Profile.WorldVerticalFOV);Row->SetNumberField(TEXT("configured_first_person_vertical_FOV"),Profile.FirstPersonVerticalFOV);Row->SetNumberField(TEXT("independent_NPC_combat_eye_cm"),Mode->Model.Config.EyeHeight);}
        FHitResult Floor;FCollisionQueryParams Query(SCENE_QUERY_STAT(BioNativeInputFeet),false,Player);TArray<AActor*> Attached;Player->GetAttachedActors(Attached,true,true);Query.AddIgnoredActors(Attached);const bool Hit=GetWorld()->LineTraceSingleByChannel(Floor,Feet+FVector(0,0,30),Feet-FVector(0,0,150),ECC_Visibility,Query);Row->SetBoolField(TEXT("actual_capsule_floor_hit"),Hit);if(Hit){Row->SetNumberField(TEXT("actual_capsule_feet_ground_gap_cm"),Feet.Z-Floor.ImpactPoint.Z);Row->SetStringField(TEXT("actual_floor_component"),GetPathNameSafe(Floor.GetComponent()));}
    }
    return Row;
}
void AHeliosBioNativeInputQA::Append(const TSharedRef<FJsonObject>& Row)
{
    if(Out.IsEmpty())return;FString Text;FJsonSerializer::Serialize(Row,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));Text+=TEXT("\n");
    if(!FFileHelper::SaveStringToFile(Text,*(Out/TEXT("native_input_events.jsonl")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append))bIOError=true;
}
void AHeliosBioNativeInputQA::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);if(bFinished)return;const double Wall=FPlatformTime::Seconds();auto* PC=UGameplayStatics::GetPlayerController(GetWorld(),0);
    bool ExitKey=false;
    if(PC)
    {
        for(const FKey& Key:{EKeys::W,EKeys::A,EKeys::S,EKeys::D,EKeys::SpaceBar,EKeys::LeftControl,EKeys::RightControl,EKeys::C,EKeys::R,EKeys::E,EKeys::F,EKeys::G,EKeys::F5,EKeys::F6,EKeys::F9,EKeys::Escape,EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four,EKeys::Five,EKeys::Left,EKeys::Right,EKeys::LeftShift,EKeys::Tab,EKeys::LeftMouseButton,EKeys::RightMouseButton,EKeys::MiddleMouseButton,EKeys::MouseScrollUp,EKeys::MouseScrollDown})
        {
            const bool Down=PC->IsInputKeyDown(Key);const bool Previous=PreviousKeys.FindRef(Key);
            if((bInputInitialized&&Down!=Previous)||(!bInputInitialized&&Down))
            {auto Row=Snapshot(bInputInitialized?TEXT("controller_key_edge"):TEXT("initial_controller_key_state"));Row->SetStringField(TEXT("key"),Key.ToString());Row->SetBoolField(TEXT("down"),Down);Row->SetBoolField(TEXT("native_just_pressed"),PC->WasInputKeyJustPressed(Key));Row->SetBoolField(TEXT("native_just_released"),PC->WasInputKeyJustReleased(Key));Append(Row);++EventCount;ExitKey|=Key==EKeys::F9&&Down&&bInputInitialized;}
            PreviousKeys.Add(Key,Down);
        }
        bInputInitialized=true;float X=0,Y=0;PC->GetInputMouseDelta(X,Y);if(FMath::Abs(X)>.001f||FMath::Abs(Y)>.001f){auto Row=Snapshot(TEXT("controller_mouse_delta"));Row->SetNumberField(TEXT("raw_controller_mouse_delta_x"),X);Row->SetNumberField(TEXT("raw_controller_mouse_delta_y"),Y);Append(Row);++EventCount;}
        const float Wheel=PC->GetInputAnalogKeyState(EKeys::MouseWheelAxis);if(FMath::Abs(Wheel)>.001f){auto Row=Snapshot(TEXT("controller_mouse_wheel_axis"));Row->SetNumberField(TEXT("raw_controller_mouse_wheel_axis"),Wheel);Append(Row);++EventCount;}
    }
    if(Wall>=NextSnapshotWall){Append(Snapshot(TEXT("state_snapshot")));++SnapshotCount;NextSnapshotWall=Wall+.1;}
    if(ExitKey){Finish(TEXT("observed_F9_exit"),true);return;}
    if(Wall-StartedWall>=ObservationSeconds)Finish(TEXT("bounded_wall_observation_ended"),true);
}
void AHeliosBioNativeInputQA::Finish(const FString& Reason,bool RequestExit)
{
    if(bFinished)return;bFinished=true;auto Final=Snapshot(TEXT("observer_finished"));Final->SetStringField(TEXT("finish_reason"),Reason);Append(Final);
    auto Report=MakeShared<FJsonObject>();Report->SetStringField(TEXT("status"),TEXT("read_only_observation_no_acceptance_claim"));Report->SetStringField(TEXT("finish_reason"),Reason);Report->SetBoolField(TEXT("fixture_mode"),bFixtureMode);Report->SetBoolField(TEXT("input_injected_by_observer"),false);Report->SetBoolField(TEXT("gameplay_mutated_by_observer"),false);Report->SetBoolField(TEXT("gameplay_gate_changed"),false);Report->SetBoolField(TEXT("logger_IO_error"),bIOError);Report->SetNumberField(TEXT("observed_event_count"),EventCount);Report->SetNumberField(TEXT("snapshot_count"),SnapshotCount);Report->SetNumberField(TEXT("wall_observation_seconds"),FPlatformTime::Seconds()-StartedWall);Report->SetObjectField(TEXT("final_snapshot"),Final);Report->SetStringField(TEXT("scope"),TEXT("PC native key states/edges and mouse deltas plus actual player/runtime state, including paused ticks. UI consumption and sub-frame input may not appear in PC state. The observer never generates input, movement, aim, damage, fixture placement or rules ticks. Manual hunter-choice coexistence is explicitly a fixture. Capsule feet are measured, not rendered shoe soles. These events need external review and do not assert natural-round or physical-input acceptance."));
    if(!Out.IsEmpty()){FString Text;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Out/TEXT("native_input_report.json")));}if(RequestExit)FPlatformMisc::RequestExit(false);
}
void AHeliosBioNativeInputQA::EndPlay(const EEndPlayReason::Type Reason)
{Finish(TEXT("world_endplay_" )+FString::FromInt(static_cast<int32>(Reason)),false);Super::EndPlay(Reason);}
