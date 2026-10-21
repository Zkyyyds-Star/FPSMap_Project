#include "QA/HeliosQARunner.h"
#include "Player/HeliosCharacter.h"
#include "Control/HeliosControlSubsystem.h"
#include "World/HeliosMarker.h"
#include "HeliosOutpost.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "InputKeyEventArgs.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "NavigationData.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include <limits>

AHeliosQARunner::AHeliosQARunner()
{
    PrimaryActorTick.bCanEverTick = true;
}

FVector AHeliosQARunner::JsonVector(const TSharedPtr<FJsonObject>& Object, const FString& Field) const
{
    const TArray<TSharedPtr<FJsonValue>>& Values = Object->GetArrayField(Field);
    return FVector(Values[0]->AsNumber(), Values[1]->AsNumber(), Values[2]->AsNumber()) * 100.0;
}

void AHeliosQARunner::BeginPlay()
{
    Super::BeginPlay();
    RootDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("..")));
    Mode = TEXT("all");
    FParse::Value(FCommandLine::Get(), TEXT("HeliosQA="), Mode);
    Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("mode"), Mode);
    Report->SetStringField(TEXT("started_utc"), FDateTime::UtcNow().ToIso8601());
    Report->SetStringField(TEXT("movement_method"), TEXT("Native control subsystem -> CharacterMovement; teleport only for deterministic control setup and at each route start"));

    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(RootDir, TEXT("exports/layout.json")))
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Layout))
    {
        Check(TEXT("load_layout"), false, TEXT("Cannot read manifest"));
        Finish();
        return;
    }

    LastFrameTime = FPlatformTime::Seconds();
    UE_LOG(LogHeliosOutpost, Display, TEXT("HELIOS_QA warmup mode=%s"), *Mode);
}

void AHeliosQARunner::Check(const FString& Name, bool Passed, const FString& Detail)
{
    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("name"), Name);
    Item->SetBoolField(TEXT("passed"), Passed);
    Item->SetStringField(TEXT("detail"), Detail);
    Checks.Add(MakeShared<FJsonValueObject>(Item));
    if (!Passed)
    {
        ++FailureCount;
    }
    UE_LOG(LogHeliosOutpost, Display, TEXT("HELIOS_QA check=%s result=%s %s"), *Name, Passed ? TEXT("PASS") : TEXT("FAIL"), *Detail);
}

void AHeliosQARunner::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bFinished)
    {
        return;
    }

    Timer += DeltaSeconds;
    const double Now = FPlatformTime::Seconds();
    const double WallDelta = Now - LastFrameTime;
    LastFrameTime = Now;
    if (Stage == 10 && WallDelta > 0.0)
    {
        FrameTimes.Add(WallDelta);
        RouteFrameTimes.Add(WallDelta);
    }

    if (Stage == 0 && Timer >= 10.0f)
    {
        Player = Cast<AHeliosCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
        if (!Player.IsValid())
        {
            Check(TEXT("possessed_player"), false);
            Finish();
            return;
        }
        if (Mode == TEXT("captures"))
        {
            StartCaptures();
        }
        else
        {
            StartControls();
        }
        return;
    }

    if (!Player.IsValid())
    {
        return;
    }

    UHeliosControlSubsystem* Control = GetWorld()->GetSubsystem<UHeliosControlSubsystem>();
    if (Stage == 1 && Timer >= 2.25f)
    {
        const float ForwardDistance = FVector::DotProduct(Player->GetActorLocation() - StartPosition, ControlForward);
        Check(TEXT("move_uses_character_movement"), ForwardDistance > 100.0f,
            FString::Printf(TEXT("forward_distance_cm=%.1f"), ForwardDistance));
        Check(TEXT("collision_blocks_controlled_move"), CollisionTestWall.IsValid() && ForwardDistance < 375.0f,
            FString::Printf(TEXT("forward_distance_cm=%.1f wall_distance_cm=400.0"), ForwardDistance));
        Check(TEXT("duration_expires"), Control && !Control->IsAutomationActive());

        const FHeliosControlResult StopResult = Control
            ? Control->Stop(TEXT("player_01"))
            : FHeliosControlResult();
        Check(TEXT("stop_accepts"), StopResult.bSuccess);
        DestroyCollisionTestWall();
        StopPosition = Player->GetActorLocation();
        Stage = 2;
        Timer = 0.0f;
    }
    else if (Stage == 2 && Timer >= 0.4f)
    {
        Check(TEXT("stop_has_no_residual_motion"), FVector::Dist2D(StopPosition, Player->GetActorLocation()) < 2.0f,
            FString::Printf(TEXT("drift_cm=%.3f"), FVector::Dist2D(StopPosition, Player->GetActorLocation())));

        const float Yaw = Player->GetControlRotation().Yaw;
        Check(TEXT("look_accepts"), Control && Control->Look(TEXT("player_01"), 45.0f, 0.0f).bSuccess);
        Check(TEXT("look_changes_camera"),
            FMath::Abs(FRotator::NormalizeAxis(Player->GetControlRotation().Yaw - Yaw) - 45.0f) < 0.1f);

        if (Mode == TEXT("control") || Mode == TEXT("all"))
        {
            StartInputBindingQA();
        }
        else
        {
            if (Control)
            {
                Control->Move(TEXT("player_01"), FVector2D(0.0, 1.0), 2.0f, false);
                Control->NotifyManualInput(Player.Get());
                Check(TEXT("manual_handover_cancels_automation"), !Control->IsAutomationActive(),
                    TEXT("Direct subsystem notification-path unit check; control mode separately exercises held PlayerController InputKey events"));
                Control->Stop(TEXT("player_01"));
            }
            BeginNavigationWait();
        }
    }
    else if (Stage == 30)
    {
        TickInputBindingQA();
    }
    else if (Stage == 4)
    {
        FString NavigationDetail;
        const bool bNavigationReady = IsNavigationReady(NavigationDetail);
        if (!bNavigationReady && Timer < 30.0f)
        {
            return;
        }

        Check(TEXT("navigation_build_ready"), bNavigationReady,
            bNavigationReady
                ? FString::Printf(TEXT("wait_seconds=%.2f %s"), Timer, *NavigationDetail)
                : FString::Printf(TEXT("Timed out after 30 seconds: %s"), *NavigationDetail));
        WriteGroundDiagnosis(bNavigationReady);
        CheckNavigation();
        ContinueAfterNavigation();
    }
    else if (Stage == 10)
    {
        TickRoute(DeltaSeconds);
    }
    else if (Stage == 11)
    {
        if (!bScreenshotCompletionObserved)
        {
            if (!FScreenshotRequest::IsScreenshotRequested())
            {
                bScreenshotCompletionObserved = true;
                Timer = 0.0f;
            }
            else if (Timer >= 10.0f)
            {
                Check(TEXT("route_screenshot_processed_") + Routes[RouteIndex]->GetStringField(TEXT("id")),
                    false, TEXT("Screenshot request timed out after 10 seconds"));
                FScreenshotRequest::Reset();
                bScreenshotCompletionObserved = true;
                Timer = 0.0f;
            }
        }
        else if (Timer >= 1.0f)
        {
            ContinueAfterRouteScreenshot();
        }
    }
    else if (Stage == 20 && Timer >= 2.0f)
    {
        if (CaptureIndex >= Cameras.Num())
        {
            Finish();
            return;
        }
        const FString Name = Cameras[CaptureIndex]->GetStringField(TEXT("id"));
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(RootDir, TEXT("qa/screenshots"), Name + TEXT(".png")), false, false);
        UE_LOG(LogHeliosOutpost, Display, TEXT("HELIOS_QA screenshot=%s"), *Name);
        bScreenshotCompletionObserved = false;
        Stage = 21;
        Timer = 0.0f;
    }
    else if (Stage == 21)
    {
        if (!bScreenshotCompletionObserved)
        {
            if (!FScreenshotRequest::IsScreenshotRequested())
            {
                bScreenshotCompletionObserved = true;
                Timer = 0.0f;
            }
            else if (Timer >= 10.0f)
            {
                Check(TEXT("camera_screenshot_processed_") + Cameras[CaptureIndex]->GetStringField(TEXT("id")),
                    false, TEXT("Screenshot request timed out after 10 seconds"));
                FScreenshotRequest::Reset();
                bScreenshotCompletionObserved = true;
                Timer = 0.0f;
            }
            return;
        }
        if (Timer < 1.0f)
        {
            return;
        }

        ++CaptureIndex;
        if (CaptureIndex >= Cameras.Num())
        {
            Finish();
            return;
        }

        const TSharedPtr<FJsonObject> Camera = Cameras[CaptureIndex];
        const FVector Position = JsonVector(Camera, TEXT("position"));
        CaptureCamera->SetActorLocationAndRotation(Position, (JsonVector(Camera, TEXT("target")) - Position).Rotation());
        Stage = 20;
        Timer = 0.0f;
    }
}

void AHeliosQARunner::StartControls()
{
    UHeliosControlSubsystem* Control = GetWorld()->GetSubsystem<UHeliosControlSubsystem>();
    Check(TEXT("control_subsystem"), Control != nullptr);
    if (!Control || !ResetPlayerForControls())
    {
        Finish();
        return;
    }

    FHeliosActorState State;
    Check(TEXT("get_state"), Control->GetState(TEXT("player_01"), State).bSuccess);

    auto ParseDispatchResult = [](const FString& Json, TSharedPtr<FJsonObject>& OutObject)
    {
        return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), OutObject) && OutObject.IsValid();
    };
    TSharedPtr<FJsonObject> DispatchObject;
    const FString DispatchState = Control->DispatchLocalCommand(TEXT("get_state"), TEXT("player_01"), TEXT("{}"));
    const bool bDispatchStateParsed = ParseDispatchResult(DispatchState, DispatchObject);
    Check(TEXT("dispatch_get_state"), bDispatchStateParsed
        && DispatchObject->GetBoolField(TEXT("ok"))
        && DispatchObject->HasTypedField<EJson::Object>(TEXT("state")));

    DispatchObject.Reset();
    const FString DispatchUnknown = Control->DispatchLocalCommand(TEXT("unsupported_qa_command"), TEXT("player_01"), TEXT("{}"));
    const bool bDispatchUnknownParsed = ParseDispatchResult(DispatchUnknown, DispatchObject);
    Check(TEXT("dispatch_unknown_command"), bDispatchUnknownParsed
        && !DispatchObject->GetBoolField(TEXT("ok"))
        && DispatchObject->GetStringField(TEXT("error_code")) == TEXT("invalid_command"));

    DispatchObject.Reset();
    const FString DispatchInvalidJson = Control->DispatchLocalCommand(TEXT("move"), TEXT("player_01"), TEXT("{"));
    const bool bDispatchInvalidJsonParsed = ParseDispatchResult(DispatchInvalidJson, DispatchObject);
    Check(TEXT("dispatch_invalid_json"), bDispatchInvalidJsonParsed
        && !DispatchObject->GetBoolField(TEXT("ok"))
        && DispatchObject->GetStringField(TEXT("error_code")) == TEXT("invalid_json"));

    Check(TEXT("invalid_actor"), Control->Stop(TEXT("missing_actor")).ErrorCode == TEXT("invalid_actor"));
    Check(TEXT("duration_limit"), !Control->Move(TEXT("player_01"), FVector2D(0.0, 1.0), 3.01f, false).bSuccess);
    Check(TEXT("reject_nan"), !Control->Move(TEXT("player_01"), FVector2D(std::numeric_limits<double>::quiet_NaN(), 1.0), 1.0f, false).bSuccess);

    if (!SpawnCollisionTestWall())
    {
        Check(TEXT("spawn_collision_test_wall"), false);
        Finish();
        return;
    }
    Check(TEXT("spawn_collision_test_wall"), true);

    StartPosition = Player->GetActorLocation();
    ControlForward = Player->GetActorForwardVector().GetSafeNormal2D();
    Check(TEXT("move_accepts"), Control->Move(TEXT("player_01"), FVector2D(0.0, 1.0), 2.0f, false).bSuccess);
    Stage = 1;
    Timer = 0.0f;
}

bool AHeliosQARunner::ResetPlayerForControls()
{
    AHeliosMarker* Preferred = nullptr;
    for (TActorIterator<AHeliosMarker> It(GetWorld()); It; ++It)
    {
        if (It->MarkerType != TEXT("player"))
        {
            continue;
        }
        if (It->MarkerId == TEXT("player_01"))
        {
            Preferred = *It;
            break;
        }
        if (!Preferred || It->MarkerId < Preferred->MarkerId)
        {
            Preferred = *It;
        }
    }

    Check(TEXT("control_start_marker"), Preferred != nullptr, Preferred ? Preferred->MarkerId : TEXT("No player marker"));
    if (!Preferred)
    {
        return false;
    }

    const FVector Start = Preferred->GetActorLocation() + FVector(0.0, 0.0, 100.0);
    const FRotator NorthFacing(0.0f, 90.0f, 0.0f);
    Player->SetActorLocationAndRotation(Start, NorthFacing, false, nullptr, ETeleportType::TeleportPhysics);
    Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Player->GetCharacterMovement()->StopMovementImmediately();
    Player->ConsumeMovementInputVector();
    if (AController* Controller = Player->GetController())
    {
        Controller->SetControlRotation(NorthFacing);
    }
    return true;
}

void AHeliosQARunner::StartInputBindingQA()
{
    InputQAController = Cast<APlayerController>(Player->GetController());
    Check(TEXT("input_player_controller"), InputQAController.IsValid(),
        TEXT("Required for the in-engine PlayerController::InputKey binding test"));
    if (!InputQAController.IsValid())
    {
        BeginNavigationWait();
        return;
    }

    InputQAStage = 0;
    InputDirectionIndex = 0;
    bInputPressHandled = false;
    bInputReleaseHandled = false;
    bInputWasDownWhileHeld = false;
    bJumpLeftGround = false;
    bJumpReleased = false;
    Stage = 30;
    Timer = 0.0f;
    ResetPlayerForInputTest();
}

bool AHeliosQARunner::ResetPlayerForInputTest()
{
    if (!Player.IsValid() || !InputQAController.IsValid() || !Player->GetCharacterMovement())
    {
        return false;
    }

    ReleaseInjectedKeys();
    if (UHeliosControlSubsystem* Control = GetWorld()->GetSubsystem<UHeliosControlSubsystem>())
    {
        Control->Stop(TEXT("player_01"));
    }

    const FRotator NorthFacing(0.0f, 90.0f, 0.0f);
    Player->ClearMovementInput();
    Player->SetActorLocationAndRotation(StartPosition, NorthFacing, false, nullptr, ETeleportType::TeleportPhysics);
    Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Player->GetCharacterMovement()->StopMovementImmediately();
    Player->ConsumeMovementInputVector();
    InputQAController->SetControlRotation(NorthFacing);
    return true;
}

bool AHeliosQARunner::SendInputKey(const FKey& Key, EInputEvent Event)
{
    if (!InputQAController.IsValid())
    {
        return false;
    }

    const float Amount = Event == IE_Released ? 0.0f : 1.0f;
    const bool bHandled = InputQAController->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, Amount));
    if (Event == IE_Pressed)
    {
        PressedInputKeys.AddUnique(Key);
    }
    else if (Event == IE_Released)
    {
        PressedInputKeys.Remove(Key);
    }
    return bHandled;
}

void AHeliosQARunner::ReleaseInjectedKeys()
{
    if (!InputQAController.IsValid())
    {
        PressedInputKeys.Reset();
        return;
    }

    const TArray<FKey> KeysToRelease = PressedInputKeys;
    for (const FKey& Key : KeysToRelease)
    {
        SendInputKey(Key, IE_Released);
    }
    PressedInputKeys.Reset();
    InputQAController->FlushPressedKeys();
}

void AHeliosQARunner::StartDirectionalInputTest()
{
    static const FKey DirectionKeys[] = { EKeys::W, EKeys::S, EKeys::A, EKeys::D };
    static const TCHAR* DirectionLabels[] = { TEXT("W"), TEXT("S"), TEXT("A"), TEXT("D") };

    if (!ResetPlayerForInputTest())
    {
        Check(TEXT("input_reset"), false, TEXT("Player or movement component became unavailable"));
        FinishInputBindingQA();
        return;
    }

    InputKeyLabel = DirectionLabels[InputDirectionIndex];
    InputTestOrigin = Player->GetActorLocation();
    const FVector Forward = Player->GetActorForwardVector().GetSafeNormal2D();
    const FVector Right = Player->GetActorRightVector().GetSafeNormal2D();
    switch (InputDirectionIndex)
    {
    case 0: InputExpectedDirection = Forward; break;
    case 1: InputExpectedDirection = -Forward; break;
    case 2: InputExpectedDirection = -Right; break;
    default: InputExpectedDirection = Right; break;
    }
    bInputPressHandled = SendInputKey(DirectionKeys[InputDirectionIndex], IE_Pressed);
    InputQAStage = 1;
    Timer = 0.0f;
}

void AHeliosQARunner::TickInputBindingQA()
{
    if (!Player.IsValid() || !InputQAController.IsValid())
    {
        Check(TEXT("input_test_player_valid"), false);
        FinishInputBindingQA();
        return;
    }

    UCharacterMovementComponent* Movement = Player->GetCharacterMovement();
    UHeliosControlSubsystem* Control = GetWorld()->GetSubsystem<UHeliosControlSubsystem>();
    if (!Movement || !Control)
    {
        Check(TEXT("input_test_dependencies"), false);
        FinishInputBindingQA();
        return;
    }

    if (InputQAStage == 0 && Timer >= 0.15f)
    {
        StartDirectionalInputTest();
    }
    else if (InputQAStage == 1 && Timer >= 0.45f)
    {
        static const FKey DirectionKeys[] = { EKeys::W, EKeys::S, EKeys::A, EKeys::D };
        const bool bKeyDownWhileHeld = InputQAController->IsInputKeyDown(DirectionKeys[InputDirectionIndex]);
        bInputReleaseHandled = SendInputKey(DirectionKeys[InputDirectionIndex], IE_Released);
        const FVector Delta = Player->GetActorLocation() - InputTestOrigin;
        const float AlongExpected = FVector::DotProduct(Delta, InputExpectedDirection);
        const float PlanarDistance = Delta.Size2D();
        const float DirectionDot = PlanarDistance > KINDA_SMALL_NUMBER ? AlongExpected / PlanarDistance : 0.0f;
        Check(TEXT("input_") + InputKeyLabel + TEXT("_binding"), bKeyDownWhileHeld && PlanarDistance > 40.0f,
            FString::Printf(TEXT("PlayerController key_down_while_held=%s movement_cm=%.1f raw_press_return=%s raw_release_return=%s"),
                bKeyDownWhileHeld ? TEXT("true") : TEXT("false"), PlanarDistance,
                bInputPressHandled ? TEXT("true") : TEXT("false"), bInputReleaseHandled ? TEXT("true") : TEXT("false")));
        Check(TEXT("input_") + InputKeyLabel + TEXT("_direction"), AlongExpected > 40.0f && DirectionDot > 0.85f,
            FString::Printf(TEXT("distance_cm=%.1f expected_cm=%.1f direction_dot=%.3f"), PlanarDistance, AlongExpected, DirectionDot));

        ++InputDirectionIndex;
        if (InputDirectionIndex < 4)
        {
            InputQAStage = 0;
            Timer = 0.0f;
        }
        else
        {
            ResetPlayerForInputTest();
            Check(TEXT("walk_speed_binding_baseline"), FMath::IsNearlyEqual(Movement->MaxWalkSpeed, Player->WalkSpeed, 0.1f),
                FString::Printf(TEXT("max_walk_speed_cm_s=%.1f"), Movement->MaxWalkSpeed));
            bInputPressHandled = SendInputKey(EKeys::LeftShift, IE_Pressed);
            InputQAStage = 2;
            Timer = 0.0f;
        }
    }
    else if (InputQAStage == 2 && Timer >= 0.2f)
    {
        const bool bSprintSpeed = FMath::IsNearlyEqual(Movement->MaxWalkSpeed, Player->SprintSpeed, 0.1f);
        InputTestOrigin = Player->GetActorLocation();
        InputExpectedDirection = Player->GetActorForwardVector().GetSafeNormal2D();
        bInputPressHandled = bInputPressHandled && SendInputKey(EKeys::W, IE_Pressed);
        Check(TEXT("shift_sets_sprint_speed"), bSprintSpeed,
            FString::Printf(TEXT("max_walk_speed_cm_s=%.1f"), Movement->MaxWalkSpeed));
        InputQAStage = 3;
        Timer = 0.0f;
    }
    else if (InputQAStage == 3 && Timer >= 0.4f)
    {
        const FVector Delta = Player->GetActorLocation() - InputTestOrigin;
        const float AlongExpected = FVector::DotProduct(Delta, InputExpectedDirection);
        const bool bSprintSpeedHeld = FMath::IsNearlyEqual(Movement->MaxWalkSpeed, Player->SprintSpeed, 0.1f);
        const bool bShiftDownWhileHeld = InputQAController->IsInputKeyDown(EKeys::LeftShift);
        const bool bWDownWhileHeld = InputQAController->IsInputKeyDown(EKeys::W);
        const bool bReleasedW = SendInputKey(EKeys::W, IE_Released);
        const bool bReleasedShift = SendInputKey(EKeys::LeftShift, IE_Released);
        Check(TEXT("shift_and_W_bindings"), bShiftDownWhileHeld && bWDownWhileHeld && bSprintSpeedHeld && AlongExpected > 60.0f,
            FString::Printf(TEXT("key_down_shift=%s key_down_W=%s sprint_speed_observed=%s forward_cm=%.1f raw_combined_return=%s"),
                bShiftDownWhileHeld ? TEXT("true") : TEXT("false"), bWDownWhileHeld ? TEXT("true") : TEXT("false"),
                bSprintSpeedHeld ? TEXT("true") : TEXT("false"), AlongExpected,
                (bInputPressHandled && bReleasedW && bReleasedShift) ? TEXT("true") : TEXT("false")));
        Check(TEXT("sprint_moves_forward"), AlongExpected > 60.0f,
            FString::Printf(TEXT("forward_distance_cm=%.1f"), AlongExpected));
        InputQAStage = 4;
        Timer = 0.0f;
    }
    else if (InputQAStage == 4 && Timer >= 0.2f)
    {
        Check(TEXT("shift_release_keeps_sprint_speed"), FMath::IsNearlyEqual(Movement->MaxWalkSpeed, Player->SprintSpeed, 0.1f),
            FString::Printf(TEXT("max_walk_speed_cm_s=%.1f"), Movement->MaxWalkSpeed));
        SendInputKey(EKeys::LeftShift, IE_Pressed);
        SendInputKey(EKeys::LeftShift, IE_Released);
        InputQAStage = 40;
        Timer = 0.0f;
    }
    else if (InputQAStage == 40 && Timer >= 0.2f)
    {
        Check(TEXT("second_shift_press_restores_walk_speed"), FMath::IsNearlyEqual(Movement->MaxWalkSpeed, Player->WalkSpeed, 0.1f));
        ResetPlayerForInputTest();
        const bool bAutomationStarted = Control->Move(TEXT("player_01"), FVector2D(0.0, 1.0), 2.0f, false).bSuccess
            && Control->IsAutomationActive();
        bInputPressHandled = SendInputKey(EKeys::W, IE_Pressed);
        Check(TEXT("manual_cancel_automation_started"), bAutomationStarted);
        InputQAStage = 5;
        Timer = 0.0f;
    }
    else if (InputQAStage == 5 && Timer >= 0.2f)
    {
        const bool bWDownWhileHeld = InputQAController->IsInputKeyDown(EKeys::W);
        bInputReleaseHandled = SendInputKey(EKeys::W, IE_Released);
        Check(TEXT("manual_keyboard_cancels_automation"), bWDownWhileHeld && !Control->IsAutomationActive(),
            FString::Printf(TEXT("W key_down_while_held=%s automation_active=%s raw_press_return=%s raw_release_return=%s"),
                bWDownWhileHeld ? TEXT("true") : TEXT("false"), Control->IsAutomationActive() ? TEXT("true") : TEXT("false"),
                bInputPressHandled ? TEXT("true") : TEXT("false"), bInputReleaseHandled ? TEXT("true") : TEXT("false")));
        Control->Stop(TEXT("player_01"));
        ResetPlayerForInputTest();
        InputTestOrigin = Player->GetActorLocation();
        bInputPressHandled = SendInputKey(EKeys::SpaceBar, IE_Pressed);
        bInputReleaseHandled = false;
        bJumpLeftGround = false;
        bJumpReleased = false;
        InputQAStage = 6;
        Timer = 0.0f;
    }
    else if (InputQAStage == 6)
    {
        if (Movement->IsFalling() || Player->GetActorLocation().Z > InputTestOrigin.Z + 10.0f)
        {
            bJumpLeftGround = true;
        }
        if (!bJumpReleased && Timer >= 0.2f)
        {
            bInputWasDownWhileHeld = InputQAController->IsInputKeyDown(EKeys::SpaceBar);
            bInputReleaseHandled = SendInputKey(EKeys::SpaceBar, IE_Released);
            bJumpReleased = true;
        }

        if (bJumpLeftGround && bJumpReleased && Movement->IsMovingOnGround() && Timer >= 0.3f)
        {
            Check(TEXT("space_binding"), bInputWasDownWhileHeld && bJumpLeftGround,
                FString::Printf(TEXT("key_down_while_held=%s jump_observed=true raw_press_return=%s raw_release_return=%s"),
                    bInputWasDownWhileHeld ? TEXT("true") : TEXT("false"), bInputPressHandled ? TEXT("true") : TEXT("false"),
                    bInputReleaseHandled ? TEXT("true") : TEXT("false")));
            Check(TEXT("jump_leaves_ground"), true);
            Check(TEXT("jump_returns_to_ground"), true,
                FString::Printf(TEXT("air_cycle_seconds=%.2f"), Timer));
            FinishInputBindingQA();
        }
        else if (Timer >= 5.0f)
        {
            Check(TEXT("space_binding"), bInputWasDownWhileHeld && bJumpLeftGround,
                FString::Printf(TEXT("key_down_while_held=%s raw_press_return=%s raw_release_return=%s"),
                    bInputWasDownWhileHeld ? TEXT("true") : TEXT("false"), bInputPressHandled ? TEXT("true") : TEXT("false"),
                    bInputReleaseHandled ? TEXT("true") : TEXT("false")));
            Check(TEXT("jump_leaves_ground"), bJumpLeftGround);
            Check(TEXT("jump_returns_to_ground"), false, TEXT("Jump did not complete within 5 seconds"));
            FinishInputBindingQA();
        }
    }
}

void AHeliosQARunner::FinishInputBindingQA()
{
    ReleaseInjectedKeys();
    if (UHeliosControlSubsystem* Control = GetWorld()->GetSubsystem<UHeliosControlSubsystem>())
    {
        Control->Stop(TEXT("player_01"));
    }
    BeginNavigationWait();
}

bool AHeliosQARunner::SpawnCollisionTestWall()
{
    DestroyCollisionTestWall();
    const FVector Forward = Player->GetActorForwardVector().GetSafeNormal2D();
    const FVector WallLocation = Player->GetActorLocation() + Forward * 400.0f;
    const FRotator WallRotation(0.0f, Forward.Rotation().Yaw, 0.0f);

    FActorSpawnParameters SpawnParameters;
    SpawnParameters.ObjectFlags |= RF_Transient;
    AActor* Wall = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), WallLocation, WallRotation, SpawnParameters);
    if (!Wall)
    {
        return false;
    }

    UBoxComponent* Box = NewObject<UBoxComponent>(Wall, TEXT("HeliosQACollisionWall"));
    if (!Box)
    {
        Wall->Destroy();
        return false;
    }

    Wall->SetRootComponent(Box);
    Wall->AddInstanceComponent(Box);
    Box->SetBoxExtent(FVector(25.0f, 200.0f, 200.0f));
    Box->SetCollisionProfileName(TEXT("BlockAll"));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Box->SetGenerateOverlapEvents(false);
    Box->SetCanEverAffectNavigation(false);
    Box->SetHiddenInGame(true);
    Box->RegisterComponent();
    Box->SetWorldLocationAndRotation(WallLocation, WallRotation);
    CollisionTestWall = Wall;
    return true;
}

void AHeliosQARunner::DestroyCollisionTestWall()
{
    if (CollisionTestWall.IsValid())
    {
        CollisionTestWall->Destroy();
    }
    CollisionTestWall.Reset();
}

void AHeliosQARunner::BeginNavigationWait()
{
    Stage = 4;
    Timer = 0.0f;
}

bool AHeliosQARunner::IsNavigationReady(FString& OutDetail) const
{
    UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!Navigation)
    {
        OutDetail = TEXT("navigation_system_missing");
        return false;
    }

    if (UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld()))
    {
        OutDetail = TEXT("navigation_build_or_lock_active");
        return false;
    }

    ANavigationData* DefaultNavData = Navigation->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
    if (!DefaultNavData)
    {
        OutDetail = TEXT("default_nav_data_missing");
        return false;
    }

    if (!Player.IsValid() || !Player->GetCapsuleComponent())
    {
        OutDetail = TEXT("player_capsule_missing");
        return false;
    }

    const float CapsuleHalfHeight = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FVector SpawnSurface = StartPosition - FVector(0.0, 0.0, CapsuleHalfHeight);
    FNavLocation ProjectedSpawn;
    const bool bSpawnProjects = Navigation->ProjectPointToNavigation(
        SpawnSurface + FVector(0.0, 0.0, 30.0),
        ProjectedSpawn,
        FVector(100.0, 100.0, 170.0),
        DefaultNavData);
    if (!bSpawnProjects)
    {
        OutDetail = FString::Printf(TEXT("spawn_projection_pending nav_data=%s spawn_surface_cm=%s"),
            *DefaultNavData->GetPathName(), *SpawnSurface.ToString());
        return false;
    }

    OutDetail = FString::Printf(TEXT("nav_data=%s projected_spawn_cm=%s"),
        *DefaultNavData->GetPathName(), *ProjectedSpawn.Location.ToString());
    return true;
}

void AHeliosQARunner::ContinueAfterNavigation()
{
    if (Mode == TEXT("control"))
    {
        Finish();
    }
    else
    {
        StartRoutes();
    }
}

void AHeliosQARunner::AddMarkerIssue(TArray<TSharedPtr<FJsonValue>>& Target, const AHeliosMarker& Marker, const FString& Reason) const
{
    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("marker_id"), Marker.MarkerId);
    Item->SetStringField(TEXT("marker_type"), Marker.MarkerType);
    Item->SetStringField(TEXT("reason"), Reason);
    const FVector Location = Marker.GetActorLocation();
    TArray<TSharedPtr<FJsonValue>> Coordinates;
    Coordinates.Add(MakeShared<FJsonValueNumber>(Location.X));
    Coordinates.Add(MakeShared<FJsonValueNumber>(Location.Y));
    Coordinates.Add(MakeShared<FJsonValueNumber>(Location.Z));
    Item->SetArrayField(TEXT("location_cm"), Coordinates);
    Target.Add(MakeShared<FJsonValueObject>(Item));
}

void AHeliosQARunner::WriteGroundDiagnosis(bool bNavigationReady)
{
    static const FVector2D TracePointsMetres[] =
    {
        FVector2D(-28.0, -33.0),
        FVector2D(-28.0, -31.0),
        FVector2D(-28.0, -28.0),
        FVector2D(-28.0, -25.0),
        FVector2D(-28.0, -22.0),
        FVector2D(-18.0, -8.0),
        FVector2D(-17.0, -8.0),
        FVector2D(-16.0, -8.0),
        FVector2D(-14.0, -8.0),
        FVector2D(56.0, 23.0),
        FVector2D(56.0, 29.0)
    };

    TSharedPtr<FJsonObject> Diagnosis = MakeShared<FJsonObject>();
    Diagnosis->SetStringField(TEXT("generated_utc"), FDateTime::UtcNow().ToIso8601());
    Diagnosis->SetBoolField(TEXT("navigation_ready"), bNavigationReady);
    Diagnosis->SetStringField(TEXT("trace_channel"), TEXT("Visibility"));
    Diagnosis->SetBoolField(TEXT("trace_complex"), false);
    Diagnosis->SetStringField(TEXT("input_xy_units"), TEXT("metres"));
    Diagnosis->SetStringField(TEXT("world_output_units"), TEXT("centimetres"));
    Diagnosis->SetNumberField(TEXT("trace_start_z_cm"), 550.0);
    Diagnosis->SetNumberField(TEXT("trace_end_z_cm"), -650.0);

    FCollisionQueryParams QueryParameters(SCENE_QUERY_STAT(HeliosGroundDiagnosis), false);
    QueryParameters.AddIgnoredActor(this);
    QueryParameters.AddIgnoredActor(Player.Get());

    auto VectorJson = [](const FVector& Vector)
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        Values.Add(MakeShared<FJsonValueNumber>(Vector.X));
        Values.Add(MakeShared<FJsonValueNumber>(Vector.Y));
        Values.Add(MakeShared<FJsonValueNumber>(Vector.Z));
        return Values;
    };

    TArray<TSharedPtr<FJsonValue>> Results;
    for (const FVector2D& XYMetres : TracePointsMetres)
    {
        const FVector Start(XYMetres.X * 100.0, XYMetres.Y * 100.0, 550.0);
        const FVector End(XYMetres.X * 100.0, XYMetres.Y * 100.0, -650.0);
        FHitResult Hit;
        const bool bBlockingHit = GetWorld()->LineTraceSingleByChannel(
            Hit, Start, End, ECC_Visibility, QueryParameters) && Hit.bBlockingHit;

        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        TArray<TSharedPtr<FJsonValue>> XY;
        XY.Add(MakeShared<FJsonValueNumber>(XYMetres.X));
        XY.Add(MakeShared<FJsonValueNumber>(XYMetres.Y));
        Item->SetArrayField(TEXT("xy_m"), XY);
        Item->SetArrayField(TEXT("trace_start_cm"), VectorJson(Start));
        Item->SetArrayField(TEXT("trace_end_cm"), VectorJson(End));
        Item->SetBoolField(TEXT("blocking_hit"), bBlockingHit);
        Item->SetNumberField(TEXT("hit_item"), Hit.Item);
        Item->SetNumberField(TEXT("face_index"), Hit.FaceIndex);

        const AActor* HitActor = Hit.GetActor();
        const UPrimitiveComponent* HitComponent = Hit.GetComponent();
        Item->SetStringField(TEXT("hit_actor"), HitActor ? HitActor->GetPathName() : TEXT(""));
        Item->SetStringField(TEXT("hit_actor_class"), HitActor ? HitActor->GetClass()->GetPathName() : TEXT(""));
        Item->SetStringField(TEXT("hit_component"), HitComponent ? HitComponent->GetPathName() : TEXT(""));
        Item->SetStringField(TEXT("hit_component_class"), HitComponent ? HitComponent->GetClass()->GetPathName() : TEXT(""));
        Item->SetArrayField(TEXT("impact_point_cm"), VectorJson(Hit.ImpactPoint));
        Item->SetArrayField(TEXT("impact_normal"), VectorJson(Hit.ImpactNormal));
        Item->SetNumberField(TEXT("trace_time"), Hit.Time);
        Item->SetNumberField(TEXT("distance_cm"), Hit.Distance);
        Results.Add(MakeShared<FJsonValueObject>(Item));
    }
    Diagnosis->SetArrayField(TEXT("points"), Results);

    FString Output;
    FJsonSerializer::Serialize(Diagnosis.ToSharedRef(), TJsonWriterFactory<>::Create(&Output));
    const FString OutputFile = FPaths::Combine(RootDir, TEXT("qa/runtime_ground_diagnosis.json"));
    const bool bSaved = FFileHelper::SaveStringToFile(
        Output, *OutputFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    Report->SetStringField(TEXT("ground_diagnosis_file"), OutputFile);
    Check(TEXT("ground_diagnosis_written"), bSaved, OutputFile);
    UE_LOG(LogHeliosOutpost, Display, TEXT("HELIOS_QA ground_diagnosis=%s saved=%s"),
        *OutputFile, bSaved ? TEXT("true") : TEXT("false"));
}

void AHeliosQARunner::CheckNavigation()
{
    UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    Check(TEXT("navigation_system"), Navigation != nullptr);
    if (!Navigation)
    {
        return;
    }

    TArray<TSharedPtr<FJsonValue>> NavigationIssues;
    TArray<TSharedPtr<FJsonValue>> EnvironmentIssues;
    int32 MarkerCount = 0;
    int32 GroundHitCount = 0;
    int32 CapsuleClearCount = 0;
    int32 NavigationMarkerCount = 0;
    int32 ProjectedCount = 0;
    int32 ReachableCount = 0;

    const float CapsuleRadius = Player->GetCapsuleComponent()->GetScaledCapsuleRadius();
    const float CapsuleHalfHeight = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FName CapsuleProfile = Player->GetCapsuleComponent()->GetCollisionProfileName();
    FCollisionQueryParams QueryParameters(SCENE_QUERY_STAT(HeliosMarkerEnvironment), false);
    QueryParameters.AddIgnoredActor(Player.Get());

    FNavLocation ProjectedOrigin;
    const FVector OriginSurface = StartPosition - FVector(0.0, 0.0, CapsuleHalfHeight);
    const bool bOriginProjected = Navigation->ProjectPointToNavigation(
        OriginSurface + FVector(0.0, 0.0, 30.0), ProjectedOrigin, FVector(100.0, 100.0, 170.0));
    Check(TEXT("navigation_start_project"), bOriginProjected, OriginSurface.ToString());

    for (TActorIterator<AHeliosMarker> It(GetWorld()); It; ++It)
    {
        AHeliosMarker& Marker = **It;
        ++MarkerCount;

        FHitResult GroundHit;
        const FVector MarkerLocation = Marker.GetActorLocation();
        const bool bGroundHit = GetWorld()->LineTraceSingleByChannel(
            GroundHit,
            MarkerLocation + FVector(0.0, 0.0, 100.0),
            MarkerLocation - FVector(0.0, 0.0, 150.0),
            ECC_Visibility,
            QueryParameters)
            && FMath::Abs(GroundHit.ImpactPoint.Z - MarkerLocation.Z) <= 60.0f;
        if (bGroundHit)
        {
            ++GroundHitCount;
        }
        else
        {
            AddMarkerIssue(EnvironmentIssues, Marker, TEXT("ground_trace_failed"));
        }

        const FVector CapsuleLocation = MarkerLocation + FVector(0.0, 0.0, CapsuleHalfHeight + 2.0f);
        const bool bCapsuleBlocked = GetWorld()->OverlapBlockingTestByProfile(
            CapsuleLocation,
            FQuat::Identity,
            CapsuleProfile,
            FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight),
            QueryParameters);
        if (!bCapsuleBlocked)
        {
            ++CapsuleClearCount;
        }
        else
        {
            AddMarkerIssue(EnvironmentIssues, Marker, TEXT("capsule_space_blocked"));
        }

        if (Marker.MarkerType != TEXT("patrol") && Marker.MarkerType != TEXT("enemy"))
        {
            continue;
        }

        ++NavigationMarkerCount;
        FNavLocation ProjectedPoint;
        const bool bProjected = Navigation->ProjectPointToNavigation(
            MarkerLocation + FVector(0.0, 0.0, 30.0), ProjectedPoint, FVector(100.0, 100.0, 170.0));
        if (!bProjected)
        {
            AddMarkerIssue(NavigationIssues, Marker, TEXT("not_on_navigation"));
            continue;
        }

        ++ProjectedCount;
        UNavigationPath* Path = bOriginProjected
            ? UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), ProjectedOrigin.Location, ProjectedPoint.Location)
            : nullptr;
        if (Path && Path->IsValid() && !Path->IsPartial())
        {
            ++ReachableCount;
        }
        else
        {
            AddMarkerIssue(NavigationIssues, Marker, TEXT("unreachable_from_start"));
        }
    }

    Report->SetArrayField(TEXT("navigation_issues"), NavigationIssues);
    Report->SetArrayField(TEXT("marker_environment_issues"), EnvironmentIssues);
    Check(TEXT("marker_ground_trace"), MarkerCount > 0 && GroundHitCount == MarkerCount,
        FString::Printf(TEXT("%d/%d"), GroundHitCount, MarkerCount));
    Check(TEXT("marker_capsule_clear"), MarkerCount > 0 && CapsuleClearCount == MarkerCount,
        FString::Printf(TEXT("%d/%d"), CapsuleClearCount, MarkerCount));
    Check(TEXT("navigation_markers_project"), NavigationMarkerCount > 0 && ProjectedCount == NavigationMarkerCount,
        FString::Printf(TEXT("%d/%d"), ProjectedCount, NavigationMarkerCount));
    Check(TEXT("navigation_markers_reachable"), NavigationMarkerCount > 0 && ReachableCount == NavigationMarkerCount,
        FString::Printf(TEXT("%d/%d"), ReachableCount, NavigationMarkerCount));
}

void AHeliosQARunner::StartRoutes()
{
    Routes.Reset();
    for (const TSharedPtr<FJsonValue>& Value : Layout->GetArrayField(TEXT("routes")))
    {
        TSharedPtr<FJsonObject> Route = Value->AsObject();
        if (Mode == TEXT("all") || Mode == TEXT("routes") || Mode == Route->GetStringField(TEXT("id")))
        {
            Routes.Add(Route);
        }
    }
    if (Routes.Num() == 0)
    {
        Check(TEXT("routes_present"), false, Mode);
        Finish();
        return;
    }
    RouteIndex = 0;
    StartRoute();
}

void AHeliosQARunner::StartRoute()
{
    Waypoints.Reset();
    RouteFrameTimes.Reset();
    WaypointIndex = 0;
    for (const TSharedPtr<FJsonValue>& Value : Routes[RouteIndex]->GetArrayField(TEXT("waypoints")))
    {
        const TArray<TSharedPtr<FJsonValue>>& Values = Value->AsArray();
        Waypoints.Add(FVector(Values[0]->AsNumber(), Values[1]->AsNumber(), Values[2]->AsNumber()) * 100.0);
    }
    if (Waypoints.Num() < 2)
    {
        FinishRoute(false, TEXT("fewer_than_two_waypoints"));
        return;
    }

    UHeliosControlSubsystem* Control = GetWorld()->GetSubsystem<UHeliosControlSubsystem>();
    Control->Stop(TEXT("player_01"));
    Player->SetActorLocation(Waypoints[0] + FVector(0.0, 0.0, 100.0), false, nullptr, ETeleportType::TeleportPhysics);
    Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Player->GetCharacterMovement()->StopMovementImmediately();
    Player->ConsumeMovementInputVector();
    WaypointIndex = 1;
    Stage = 10;
    Timer = 0.0f;
    StuckTimer = 0.0f;
    RouteTime = 0.0f;
    CommandTimer = 0.0f;
    LastProgressPosition = Player->GetActorLocation();
    UE_LOG(LogHeliosOutpost, Display, TEXT("HELIOS_QA route_start=%s"), *Routes[RouteIndex]->GetStringField(TEXT("id")));
}

void AHeliosQARunner::TickRoute(float DeltaSeconds)
{
    RouteTime += DeltaSeconds;
    const FVector Current = Player->GetActorLocation();
    FVector Offset = Waypoints[WaypointIndex] - Current;
    const float HorizontalDistance = FVector2D(Offset.X, Offset.Y).Size();
    const float FeetZ = Current.Z - Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    if (HorizontalDistance < 65.0f && FMath::Abs(FeetZ - Waypoints[WaypointIndex].Z) < 100.0f)
    {
        ++WaypointIndex;
        if (WaypointIndex >= Waypoints.Num())
        {
            FinishRoute(true, TEXT("walked_all_waypoints"));
            return;
        }
        UE_LOG(LogHeliosOutpost, Display, TEXT("HELIOS_QA route=%s waypoint=%d"), *Routes[RouteIndex]->GetStringField(TEXT("id")), WaypointIndex);
        StuckTimer = 0.0f;
        LastProgressPosition = Current;
        Offset = Waypoints[WaypointIndex] - Current;
    }

    if (FVector::Dist2D(Current, LastProgressPosition) > 25.0f)
    {
        StuckTimer = 0.0f;
        LastProgressPosition = Current;
    }
    else
    {
        StuckTimer += DeltaSeconds;
    }

    if (StuckTimer > 5.0f || RouteTime > 360.0f || Current.Z < -1200.0f)
    {
        FinishRoute(false, FString::Printf(TEXT("blocked waypoint=%d location_cm=%s target_cm=%s"),
            WaypointIndex, *Current.ToString(), *Waypoints[WaypointIndex].ToString()));
        return;
    }

    Player->GetController()->SetControlRotation(FRotator(-6.0f, Offset.Rotation().Yaw, 0.0f));
    CommandTimer -= DeltaSeconds;
    if (CommandTimer <= 0.0f)
    {
        GetWorld()->GetSubsystem<UHeliosControlSubsystem>()->Move(TEXT("player_01"), FVector2D(0.0, 1.0), 0.3f, false);
        CommandTimer = 0.12f;
    }
}

void AHeliosQARunner::AddFrameMetrics(const TArray<double>& Samples, const TSharedPtr<FJsonObject>& Target) const
{
    if (Samples.Num() == 0 || !Target)
    {
        return;
    }

    TArray<double> Sorted = Samples;
    double Sum = 0.0;
    for (const double Value : Sorted)
    {
        Sum += Value;
    }
    Sorted.Sort();
    const int32 P95Index = FMath::Clamp(FMath::CeilToInt(Sorted.Num() * 0.95) - 1, 0, Sorted.Num() - 1);
    Target->SetNumberField(TEXT("measured_frames"), Sorted.Num());
    Target->SetNumberField(TEXT("measured_seconds"), Sum);
    Target->SetNumberField(TEXT("average_fps"), Sum > 0.0 ? Sorted.Num() / Sum : 0.0);
    Target->SetNumberField(TEXT("p95_frame_ms"), Sorted[P95Index] * 1000.0);
}

void AHeliosQARunner::FinishRoute(bool Passed, const FString& Reason)
{
    const FString Id = Routes[RouteIndex]->GetStringField(TEXT("id"));
    GetWorld()->GetSubsystem<UHeliosControlSubsystem>()->Stop(TEXT("player_01"));
    Check(TEXT("walk_route_") + Id, Passed, Reason);

    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("id"), Id);
    Item->SetBoolField(TEXT("passed"), Passed);
    Item->SetNumberField(TEXT("seconds"), RouteTime);
    Item->SetNumberField(TEXT("reached_waypoints"), WaypointIndex);
    Item->SetNumberField(TEXT("waypoint_count"), Waypoints.Num());
    Item->SetStringField(TEXT("detail"), Reason);
    AddFrameMetrics(RouteFrameTimes, Item);
    RouteResults.Add(MakeShared<FJsonValueObject>(Item));

    FScreenshotRequest::RequestScreenshot(
        FPaths::Combine(RootDir, TEXT("qa/screenshots"), TEXT("route_") + Id + TEXT(".png")), false, false);
    UE_LOG(LogHeliosOutpost, Display, TEXT("HELIOS_QA route_screenshot=%s"), *Id);
    bScreenshotCompletionObserved = false;
    Stage = 11;
    Timer = 0.0f;
}

void AHeliosQARunner::ContinueAfterRouteScreenshot()
{
    ++RouteIndex;
    if (RouteIndex >= Routes.Num())
    {
        StartCaptures();
    }
    else
    {
        StartRoute();
    }
}

void AHeliosQARunner::StartCaptures()
{
    APlayerController* PlayerController = Cast<APlayerController>(Player->GetController());
    if (!PlayerController)
    {
        Check(TEXT("capture_player_controller"), false);
        Finish();
        return;
    }

    OriginalViewTarget = PlayerController->GetViewTarget();
    if (AHUD* HUD = PlayerController->GetHUD())
    {
        bOriginalHUDVisible = HUD->bShowHUD;
        HUD->bShowHUD = false;
    }
    bCaptureStateSaved = true;

    Cameras.Reset();
    for (const TSharedPtr<FJsonValue>& Value : Layout->GetArrayField(TEXT("cameras")))
    {
        Cameras.Add(Value->AsObject());
    }
    if (Cameras.Num() == 0)
    {
        Finish();
        return;
    }

    CaptureCamera = GetWorld()->SpawnActor<ACameraActor>();
    if (!CaptureCamera.IsValid())
    {
        Check(TEXT("capture_camera_spawn"), false);
        Finish();
        return;
    }
    CaptureCamera->GetCameraComponent()->FieldOfView = 80.0f;
    const TSharedPtr<FJsonObject> Camera = Cameras[0];
    const FVector Position = JsonVector(Camera, TEXT("position"));
    CaptureCamera->SetActorLocationAndRotation(Position, (JsonVector(Camera, TEXT("target")) - Position).Rotation());
    PlayerController->SetViewTarget(CaptureCamera.Get());
    CaptureIndex = 0;
    Stage = 20;
    Timer = 0.0f;
}

void AHeliosQARunner::RestorePlayerView()
{
    if (!bCaptureStateSaved || !Player.IsValid())
    {
        return;
    }
    if (APlayerController* PlayerController = Cast<APlayerController>(Player->GetController()))
    {
        PlayerController->SetViewTarget(OriginalViewTarget.IsValid() ? OriginalViewTarget.Get() : Player.Get());
        if (AHUD* HUD = PlayerController->GetHUD())
        {
            HUD->bShowHUD = bOriginalHUDVisible;
        }
    }
    bCaptureStateSaved = false;
}

void AHeliosQARunner::Finish()
{
    if (bFinished)
    {
        return;
    }
    ReleaseInjectedKeys();
    bFinished = true;
    DestroyCollisionTestWall();

    Report->SetArrayField(TEXT("checks"), Checks);
    Report->SetArrayField(TEXT("routes"), RouteResults);
    Report->SetNumberField(TEXT("failures"), FailureCount);
    Report->SetStringField(TEXT("status"), FailureCount == 0 ? TEXT("passed") : TEXT("failed"));
    AddFrameMetrics(FrameTimes, Report);

    if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
    {
        const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
        Report->SetNumberField(TEXT("render_width"), Size.X);
        Report->SetNumberField(TEXT("render_height"), Size.Y);
    }

    FString Output;
    FJsonSerializer::Serialize(Report.ToSharedRef(), TJsonWriterFactory<>::Create(&Output));
    const FString OutputFile = FPaths::Combine(RootDir, TEXT("qa"), TEXT("runtime_qa_") + Mode + TEXT(".json"));
    FFileHelper::SaveStringToFile(Output, *OutputFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogHeliosOutpost, Display, TEXT("HELIOS_QA finished status=%s report=%s"),
        FailureCount == 0 ? TEXT("PASS") : TEXT("FAIL"), *OutputFile);

    RestorePlayerView();
    if (CaptureCamera.IsValid())
    {
        CaptureCamera->Destroy();
        CaptureCamera.Reset();
    }

    const bool bNoExit = FParse::Param(FCommandLine::Get(), TEXT("HeliosQANoExit"));
    if (!bNoExit)
    {
        FPlatformMisc::RequestExit(false);
    }
    Destroy();
}
