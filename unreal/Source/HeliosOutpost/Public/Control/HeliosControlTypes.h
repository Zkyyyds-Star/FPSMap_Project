#pragma once

#include "CoreMinimal.h"
#include "HeliosControlTypes.generated.h"

USTRUCT(BlueprintType)
struct HELIOSOUTPOST_API FHeliosControlResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Helios|Control")
    bool bSuccess = false;

    UPROPERTY(BlueprintReadOnly, Category = "Helios|Control")
    FString ErrorCode;

    UPROPERTY(BlueprintReadOnly, Category = "Helios|Control")
    FString Message;
};

USTRUCT(BlueprintType)
struct HELIOSOUTPOST_API FHeliosActorState
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Helios|Control")
    FString ActorId;

    /** Unreal world location in centimetres. */
    UPROPERTY(BlueprintReadOnly, Category = "Helios|Control")
    FVector LocationCm = FVector::ZeroVector;

    /** Pitch/yaw/roll in degrees. */
    UPROPERTY(BlueprintReadOnly, Category = "Helios|Control")
    FRotator RotationDegrees = FRotator::ZeroRotator;

    /** World-space velocity in centimetres per second. */
    UPROPERTY(BlueprintReadOnly, Category = "Helios|Control")
    FVector VelocityCmPerSecond = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Helios|Control")
    bool bGrounded = false;

    UPROPERTY(BlueprintReadOnly, Category = "Helios|Control")
    bool bAutomationActive = false;

    UPROPERTY(BlueprintReadOnly, Category = "Helios|Control")
    float AutomationRemainingSeconds = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Helios|Control")
    FString CurrentZoneId;

    UPROPERTY(BlueprintReadOnly, Category = "Helios|Control")
    FString LastStopReason;
};
