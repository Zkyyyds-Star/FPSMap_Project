#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosAwarenessQA.generated.h"

class AHeliosArenaCharacter;
class AHeliosEnemyActor;
class AHeliosBasketballProjectile;
class AShooterAIController;

/** Opt-in runtime checks using real controllers, StateTree events and projectile collision. */
UCLASS()
class HELIOSOUTPOST_API AHeliosAwarenessQA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosAwarenessQA();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
private:
    void Check(const FString& Name, bool bPassed, const FString& Detail = TEXT(""));
    void BeginCase();
    void EndCase();
    void SpawnEnemy(int32 Type);
    void PlacePlayer(const FVector& Offset);
    void MakeWall(bool bEnabled);
    void BeginShot();
    void EndShot();
    void BeginNavigation(bool bBoss = false);
    void Finish();

    TWeakObjectPtr<AHeliosArenaCharacter> Player;
    TWeakObjectPtr<AHeliosEnemyActor> Enemy;
    TWeakObjectPtr<AShooterAIController> AI;
    TWeakObjectPtr<AActor> Wall;
    TWeakObjectPtr<AHeliosBasketballProjectile> Ball;
    TArray<TSharedPtr<class FJsonValue>> Checks;
    FVector Origin = FVector(0, 0, 50000);
    FVector MemoryLocation = FVector::ZeroVector;
    FVector MapPlayerStart = FVector::ZeroVector;
    FVector NavigationStart = FVector::ZeroVector;
    float Elapsed = 0;
    float CaseElapsed = 0;
    float CaseDuration = 0.22f;
    float DetectionLatency = -1;
    float ShotHealth = 0;
    float ShotTime = 0;
    float BaselineArrival = 0;
    float BallArrival = -1;
    float ExpectedFlight = 0;
    int32 CaseIndex = 0;
    int32 ShotIndex = -1;
    int32 Failures = 0;
    bool bStarted = false;
    bool bFinished = false;
    bool bExpectedVisible = false;
};
