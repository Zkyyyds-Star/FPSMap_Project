#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosMinionCombatQA.generated.h"

class AHeliosEnemyActor;
class AHeliosArenaCharacter;
class FJsonValue;

/** Opt-in, fixed-seed real-combat fixture. Never spawned by normal play. */
UCLASS()
class HELIOSOUTPOST_API AHeliosMinionCombatQA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosMinionCombatQA();
    virtual void Tick(float DeltaSeconds) override;
private:
    void PrepareArena();
    void BeginCase();
    void EndCase();
    void ClearCase();
    void Finish(const FString& Error = FString());
    AActor* Box(const FVector& Center, const FVector& Extent);
    float FriendlyHealth() const;
    TWeakObjectPtr<AHeliosArenaCharacter> Player;
    TWeakObjectPtr<AHeliosEnemyActor> Enemy;
    TArray<TWeakObjectPtr<AActor>> Actors;
    TArray<TWeakObjectPtr<AActor>> Friendlies;
    TArray<TSharedPtr<FJsonValue>> Results;
    FVector Origin = FVector(0, 0, 10000);
    float Elapsed = 0, CaseTime = 0, FirstAttack = -1, FirstAcquire = -1;
    float InitialFriendlyHP = 0, LastFriendlyHP = 0, DamageDealt = 0, EnemyHP = 0, HitSeconds = 0;
    int32 CaseIndex = -1, Committed = 0, Attempts = 0, Acquisitions = 0;
    TWeakObjectPtr<AActor> LastTarget;
    bool bPrepared = false, bFinished = false;
};
