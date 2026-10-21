#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "HeliosBridgeProbeSubsystem.generated.h"

class FJsonObject;

/** Opt-in runtime collision probe for the upper bridge seam. */
UCLASS()
class HELIOSOUTPOST_API UHeliosBridgeProbeSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
    void RunProbe();
    TSharedPtr<FJsonObject> Trace(const FVector& Start, const FVector& End, bool bTraceComplex,
        const FString& Name = FString()) const;
    TSharedPtr<FJsonObject> SphereTrace(float RadiusCm, bool bTraceComplex) const;

    FTimerHandle ProbeTimer;
};
