#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosTownQARunner.generated.h"
class FJsonObject;
class FJsonValue;

/** Explicit -HeliosTownQA smoke runner; writes only the isolated QA progress slot. */
UCLASS()
class HELIOSOUTPOST_API AHeliosTownQARunner : public AActor
{
    GENERATED_BODY()
public:
    AHeliosTownQARunner();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaSeconds) override;
private:
    struct FStep { FString Kind, Id; FVector Position=FVector::ZeroVector, Target=FVector::ZeroVector; };
    void NextStep();
    void Record(const FString& Name, bool bPassed, const FString& Detail);
    void Finish(bool bAllowExit = true);
    TArray<FStep> Steps;
    TArray<TSharedPtr<FJsonValue>> Results;
    TArray<float> Frames;
    TSharedPtr<FJsonObject> Config;
    int32 StepIndex = -1;
    int32 Failures = 0;
    double StepStart = 0., StartTime = 0.;
    double LastTickTime = 0.;
    bool bStarted = false, bFinished = false, bActionDone = false;
    bool bMovementReleased = false, bInterrupted = false;
    double MovementReleasedAt = 0.;
    double ActionStartedAt = 0., RoomChecksStartedAt = -1.;
    bool bFloorLossLogged = false;
    FVector LastObservedPosition = FVector::ZeroVector, LastObservedVelocity = FVector::ZeroVector;
    float MinimumStepZ = 0.f;
    int32 LastMovementMode = 0;
    FString RunId;
};
