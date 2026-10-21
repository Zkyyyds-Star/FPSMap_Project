#include "Player/HeliosPlayerController.h"
#include "Control/HeliosControlSubsystem.h"
#include "HeliosOutpost.h"

AHeliosPlayerController::AHeliosPlayerController()
{
    bShowMouseCursor = false;
}

void AHeliosPlayerController::HeliosState()
{
    UHeliosControlSubsystem* Control = GetWorld() ? GetWorld()->GetSubsystem<UHeliosControlSubsystem>() : nullptr;
    if (!Control)
    {
        UE_LOG(LogHeliosOutpost, Error, TEXT("HELIOS_CONTROL command=get_state ok=false error=world_unavailable"));
        return;
    }

    FHeliosActorState State;
    const FHeliosControlResult Result = Control->GetState(UHeliosControlSubsystem::GetMainPlayerActorId(), State);
    LogResult(TEXT("get_state"), Result);
    if (Result.bSuccess)
    {
        UE_LOG(LogHeliosOutpost, Display,
            TEXT("HELIOS_STATE actor=%s location_cm=(%.2f,%.2f,%.2f) rotation_deg=(%.2f,%.2f,%.2f) velocity_cm_s=(%.2f,%.2f,%.2f) grounded=%s automation=%s remaining_s=%.3f zone=%s stop_reason=%s"),
            *State.ActorId,
            State.LocationCm.X, State.LocationCm.Y, State.LocationCm.Z,
            State.RotationDegrees.Pitch, State.RotationDegrees.Yaw, State.RotationDegrees.Roll,
            State.VelocityCmPerSecond.X, State.VelocityCmPerSecond.Y, State.VelocityCmPerSecond.Z,
            State.bGrounded ? TEXT("true") : TEXT("false"),
            State.bAutomationActive ? TEXT("true") : TEXT("false"),
            State.AutomationRemainingSeconds,
            *State.CurrentZoneId,
            *State.LastStopReason);
    }
}

void AHeliosPlayerController::HeliosMove(float Forward, float Right, float Duration, bool bSprint)
{
    UHeliosControlSubsystem* Control = GetWorld() ? GetWorld()->GetSubsystem<UHeliosControlSubsystem>() : nullptr;
    if (!Control)
    {
        UE_LOG(LogHeliosOutpost, Error, TEXT("HELIOS_CONTROL command=move ok=false error=world_unavailable"));
        return;
    }
    LogResult(TEXT("move"), Control->Move(UHeliosControlSubsystem::GetMainPlayerActorId(), FVector2D(Right, Forward), Duration, bSprint));
}

void AHeliosPlayerController::HeliosLook(float DeltaYaw, float DeltaPitch)
{
    UHeliosControlSubsystem* Control = GetWorld() ? GetWorld()->GetSubsystem<UHeliosControlSubsystem>() : nullptr;
    if (!Control)
    {
        UE_LOG(LogHeliosOutpost, Error, TEXT("HELIOS_CONTROL command=look ok=false error=world_unavailable"));
        return;
    }
    LogResult(TEXT("look"), Control->Look(UHeliosControlSubsystem::GetMainPlayerActorId(), DeltaYaw, DeltaPitch));
}

void AHeliosPlayerController::HeliosStop()
{
    UHeliosControlSubsystem* Control = GetWorld() ? GetWorld()->GetSubsystem<UHeliosControlSubsystem>() : nullptr;
    if (!Control)
    {
        UE_LOG(LogHeliosOutpost, Error, TEXT("HELIOS_CONTROL command=stop ok=false error=world_unavailable"));
        return;
    }
    LogResult(TEXT("stop"), Control->Stop(UHeliosControlSubsystem::GetMainPlayerActorId()));
}

void AHeliosPlayerController::HeliosRunControlQA()
{
    UHeliosControlSubsystem* Control = GetWorld() ? GetWorld()->GetSubsystem<UHeliosControlSubsystem>() : nullptr;
    if (!Control)
    {
        UE_LOG(LogHeliosOutpost, Error, TEXT("HELIOS_QA control_smoke result=FAIL reason=world_unavailable"));
        return;
    }

    FHeliosActorState State;
    const FHeliosControlResult StateResult = Control->GetState(TEXT("player_01"), State);
    const FHeliosControlResult InvalidActor = Control->Stop(TEXT("missing_actor"));
    const FHeliosControlResult InvalidDuration = Control->Move(TEXT("player_01"), FVector2D(0.0, 1.0), 3.01f, false);
    const bool bPassed = StateResult.bSuccess
        && !InvalidActor.bSuccess && InvalidActor.ErrorCode == TEXT("invalid_actor")
        && !InvalidDuration.bSuccess && InvalidDuration.ErrorCode == TEXT("invalid_duration");
    UE_LOG(LogHeliosOutpost, Display, TEXT("HELIOS_QA control_smoke result=%s"), bPassed ? TEXT("PASS") : TEXT("FAIL"));
}

void AHeliosPlayerController::LogResult(const TCHAR* Command, const FHeliosControlResult& Result) const
{
    UE_LOG(LogHeliosOutpost, Display, TEXT("HELIOS_CONTROL command=%s ok=%s error=%s message=\"%s\""),
        Command,
        Result.bSuccess ? TEXT("true") : TEXT("false"),
        *Result.ErrorCode,
        *Result.Message);
}

void AHeliosPlayerController::HeliosAdventureState()
{
    if (UHeliosControlSubsystem* C = GetWorld()->GetSubsystem<UHeliosControlSubsystem>())
        UE_LOG(LogHeliosOutpost, Display, TEXT("HELIOS_ADVENTURE_STATE %s"), *C->DispatchLocalCommand(TEXT("get_adventure_state"), TEXT("player_01"), TEXT("{}")));
}
void AHeliosPlayerController::HeliosAdventureRestart()
{
    if (UHeliosControlSubsystem* C = GetWorld()->GetSubsystem<UHeliosControlSubsystem>())
        UE_LOG(LogHeliosOutpost, Display, TEXT("HELIOS_ADVENTURE_RESTART %s"), *C->DispatchLocalCommand(TEXT("restart_adventure"), TEXT("player_01"), TEXT("{}")));
}
