#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Control/HeliosControlTypes.h"
#include "HeliosControlSubsystem.generated.h"

class AHeliosCharacter;
class AHeliosArenaCharacter;

/**
 * Local, in-process control surface for QA and future adapters. This subsystem
 * deliberately opens no socket and registers no external listener.
 */
UCLASS()
class HELIOSOUTPOST_API UHeliosControlSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

    UFUNCTION(BlueprintCallable, Category = "Helios|Control")
    FHeliosControlResult GetState(const FString& ActorId, FHeliosActorState& OutState) const;

    /** LocalAxes X=right, Y=forward; each axis is clamped to [-1,1]. Duration must be (0,3]. */
    UFUNCTION(BlueprintCallable, Category = "Helios|Control")
    FHeliosControlResult Move(const FString& ActorId, FVector2D LocalAxes, float DurationSeconds, bool bSprint);

    /** Applies a bounded relative camera turn. Yaw is clamped to +/-180, pitch to +/-89 degrees. */
    UFUNCTION(BlueprintCallable, Category = "Helios|Control")
    FHeliosControlResult Look(const FString& ActorId, float DeltaYawDegrees, float DeltaPitchDegrees);

    UFUNCTION(BlueprintCallable, Category = "Helios|Control")
    FHeliosControlResult Stop(const FString& ActorId);

    FHeliosControlResult Fire(const FString& ActorId, float DurationSeconds);
    FHeliosControlResult Reload(const FString& ActorId);
    FHeliosControlResult Jump(const FString& ActorId);
    bool IsAutonomyActive() const;

    /**
     * Local JSON dispatcher for future in-process adapters. This function does
     * not create a server or listener. The returned FString is a JSON object.
     */
    UFUNCTION(BlueprintCallable, Category = "Helios|Control")
    FString DispatchLocalCommand(const FString& Command, const FString& ActorId, const FString& ParametersJson);

    UFUNCTION(BlueprintPure, Category = "Helios|Control")
    static FString GetMainPlayerActorId() { return TEXT("player_01"); }

    UFUNCTION(BlueprintPure, Category = "Helios|Control")
    bool IsAutomationActive() const { return bAutomationActive; }

    void NotifyManualInput(AHeliosCharacter* Character);

    static bool ValidateDuration(float DurationSeconds, FString& OutErrorCode);
    static FVector2D ClampMoveAxes(FVector2D LocalAxes);

private:
    AHeliosCharacter* FindCharacter(const FString& ActorId) const;
    void StopAutomation(const FString& Reason, bool bStopVelocity);
    void CancelActionTimers();
    static FHeliosControlResult Success(const FString& Message);
    static FHeliosControlResult Failure(const FString& Code, const FString& Message);
    static FString SerializeDispatchResult(const FHeliosControlResult& Result, const FHeliosActorState* State = nullptr);

    TWeakObjectPtr<AHeliosCharacter> AutomatedCharacter;
    FVector2D AutomatedAxes = FVector2D::ZeroVector;
    float RemainingSeconds = 0.0f;
    bool bAutomatedSprint = false;
    bool bAutomationActive = false;
    FString LastStopReason = TEXT("none");
    FTimerHandle FireStopTimer;
    FTimerHandle JumpStopTimer;
    TWeakObjectPtr<AHeliosArenaCharacter> FireCharacter;
};
