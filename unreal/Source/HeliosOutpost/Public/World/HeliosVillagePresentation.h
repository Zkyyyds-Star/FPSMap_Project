#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "HeliosVillagePresentation.generated.h"

/** The village's rendering overrides live exactly as long as its game world. */
UCLASS()
class HELIOSOUTPOST_API UHeliosVillagePresentation : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void OnWorldBeginPlay(UWorld& World) override;
    virtual void Deinitialize() override;
private:
    TMap<FString,FString> PreviousValues;
};
