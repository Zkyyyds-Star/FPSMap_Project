#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosAdventureQA.generated.h"

class AHeliosAdventureDirector;
class AHeliosAdventurePickup;
class AHeliosArenaCharacter;
class FJsonObject;

/** Opt-in, compact gameplay acceptance run. It never exists in ordinary play. */
UCLASS()
class HELIOSOUTPOST_API AHeliosAdventureQA : public AActor
{
    GENERATED_BODY()

public:
    AHeliosAdventureQA();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

private:
    void Check(const FString& Name, bool bPassed, const FString& Detail = FString());
    void Finish();
    AHeliosAdventurePickup* FindPickup(bool bMedical) const;

    TWeakObjectPtr<AHeliosAdventureDirector> Director;
    TWeakObjectPtr<AHeliosArenaCharacter> Player;
    TWeakObjectPtr<AHeliosAdventurePickup> PendingPickup;
    TSharedPtr<FJsonObject> Report;
    TArray<TSharedPtr<class FJsonValue>> Checks;
    FString Mode = TEXT("all");
    double StartWallSeconds = 0.0;
    double StageWallSeconds = 0.0;
    int32 Stage = 0;
    int32 FailureCount = 0;
    int32 ScoreBeforePickup = 0;
    float HealthBeforeMedical = 0.0f;
    int32 ResetCount = 0;
    bool bCollectibleLocated = false;
    bool bMedicalLocated = false;
    bool bFinished = false;
};
