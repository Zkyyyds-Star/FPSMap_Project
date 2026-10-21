#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosUrbanQA.generated.h"
class AHeliosUrbanHazard;
class AHeliosUrbanBoss;
class ACameraActor;

/** Explicit command-line flow instrumentation; never spawned in normal play. */
UCLASS()
class HELIOSOUTPOST_API AHeliosUrbanQA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosUrbanQA();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
private:
    void TickCombat();
    void TickSnapshot();
    void TickBossDemo();
    void TickSpawnAudit();
    void TickRoute();
    void TickSpawnRecovery();
    void TickClinic();
    void TickResumedFixes(float DeltaSeconds);
    void FinishResumedFixes();
    void RepairCheck(const FString& Name, bool Passed);
    FString RepairMode;
    FString RepairRunName;
    TArray<TSharedPtr<class FJsonValue>> RepairChecks;
    TArray<TSharedPtr<class FJsonValue>> RepairSamples;
    TSet<int32> RepairCapturedFrames;
    int32 RepairCase = 0;
    float RepairAge = 0.f;
    UPROPERTY() TArray<TObjectPtr<class AHeliosPetCompanion>> RepairPets;
    TArray<FVector> RepairPetOrigins;
    void Finish(bool bPassed, const FString& Detail);
    double Started = 0;
    double StageStarted = 0;
    int32 Stage = 0;
    int32 PeakAlive = 0;
    int32 ClearedWaves = 0;
    int32 LastWave = 0;
    int32 VerifiedKills = 0;
    float HazardHealth = 0;
    bool bWarningSafe = false;
    UPROPERTY() TObjectPtr<AActor> HazardCaster;
    UPROPERTY() TObjectPtr<AHeliosUrbanHazard> SnapshotHazard;
    UPROPERTY() TObjectPtr<AHeliosUrbanBoss> DemoBoss;
    UPROPERTY() TObjectPtr<ACameraActor> DemoCamera;
    bool bBossDemo = false;
    bool bSpawnAudit = false;
    bool bRoute = false;
    int32 RouteReached = 0;
    float RoutePlayerHeight = 0;
    float RoutePetHeight = 0;
    TMap<FString,float> RouteCompanionHeights;
    bool bSpawnRecovery = false;
    bool bClinic = false;
    bool bRecoveryStarvation = false;
    bool bRecoveryNoEarlySpawn = true;
    bool bClinicEnemyEntered = false;
    TArray<FString> SpawnAuditRows;
    TSet<FString> BossDemoCaptures;
    double LastFrameWall = 0;
    TArray<double> RenderFrameTimes;
    TSet<int32> CapturedWaves;
    bool bRendering = false;
    bool bCombat = false;
    bool bFullRun = false;
    int32 NormalGoalWaves = 0;
    float NormalCombatLimitSeconds = 3600;
    double LastSpecialInputWall = 0;
    float NavigationRefresh = 0;
    float SteeringStuckSeconds = 0;
    FVector LastSteeringPosition = FVector::ZeroVector;
    FVector SteeringRecoveryGoal = FVector::ZeroVector;
    bool bSteeringRecovery = false;
    float ObservedProjectileSpeed = 10000;
    double LastProgress = 0;
    TArray<FVector> NavigationPoints;
    FString CombatWeapon = TEXT("rifle");
    bool bSnapshot = false;
    bool bCombatStarted = false;
    int32 CombatShots = 0;
    int32 EnemyAttacks = 0;
    int32 PetHits = 0;
    float PetTravel = 0;
    float PlayerHealth = 0;
    FVector PetOrigin = FVector::ZeroVector;
    bool bPetObserved = false;
    bool bFinished = false;
};
