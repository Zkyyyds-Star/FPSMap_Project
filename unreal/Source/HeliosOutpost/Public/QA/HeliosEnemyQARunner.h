#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "HeliosEnemyQARunner.generated.h"

class AAIController;
class AHeliosArenaCharacter;
class AHeliosEnemyActor;
class AHeliosEnemyTestDirector;
class APlayerController;
class AShooterWeapon;

/** Opt-in, evidence-based runtime QA for the enemy testbed. */
UCLASS()
class HELIOSOUTPOST_API AHeliosEnemyQARunner : public AActor
{
    GENERATED_BODY()
public:
    AHeliosEnemyQARunner();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaSeconds) override;

private:
    void Check(const FString& Name, bool bPassed, const FString& Detail = TEXT(""));
    void Pending(const FString& Name, const FString& Detail);
    void StartMode();
    void SetStage(int32 NewStage);
    void TickCombat(float DeltaSeconds);
    void TickReset(float DeltaSeconds);
    void TickPerformance(float DeltaSeconds);
    void TickCapture(float DeltaSeconds);
    void Finish();
    void SaveReport();

    bool ResolveActors();
    bool ResolveSingleEnemy();
    bool PlaceCombatants();
    FVector GroundedLocation(const FVector& Desired, float HalfHeight, AActor* Ignore) const;
    void AimAtEnemy();
    void SendKey(const FKey& Key, EInputEvent Event, float Amount = 1.0f);
    void ReleaseFire();
    void RestorePointBlankAim();
    bool SpawnBlockingWall();
    void DestroyBlockingWall();
    bool IsWallBlockingVisibility() const;
    void RequestTypeScreenshot();
    void BeginAmmoChecks();
    void BeginDeathResetCheck();
    void BeginResetChecks();

    int32 CountEnemies() const;
    int32 CountEnemyControllers() const;
    int32 CountWeapons() const;
    int32 CountProjectiles() const;
    int32 CountPlayers() const;
    int32 CountHUDs() const;

    FString Mode;
    FString RootDir;
    TSharedPtr<class FJsonObject> Report;
    TArray<TSharedPtr<class FJsonValue>> Checks;
    TArray<TSharedPtr<class FJsonValue>> PendingChecks;
    TArray<double> FrameTimes;
    TArray<double> PerformanceFrames;
    TWeakObjectPtr<AHeliosEnemyTestDirector> Director;
    TWeakObjectPtr<AHeliosArenaCharacter> Player;
    TWeakObjectPtr<AHeliosEnemyActor> Enemy;
    TWeakObjectPtr<AAIController> EnemyControllerAtDeath;
    TWeakObjectPtr<APlayerController> PlayerController;
    TWeakObjectPtr<AActor> BlockingWall;
    TWeakObjectPtr<AShooterWeapon> PointBlankWeapon;
    float SavedPointBlankVariance = 0.0f;
    FVector TestPlayerLocation = FVector(-5400.0f, -4400.0f, 96.0f);
    FVector TestEnemyLocation = FVector(-4700.0f, -4400.0f, 96.0f);
    FVector InputTestStartLocation = FVector::ZeroVector;
    FString PendingScreenshotPath;
    FString FirstResetMismatch;
    float Timer = 0.0f;
    float StageTimer = 0.0f;
    float StartingPlayerHealth = 0.0f;
    float WallPlayerHealth = 0.0f;
    float E02WallEnemyHealthBeforeShot = 0.0f;
    float E02WallPlayerHealthBeforeShot = 0.0f;
    float E02CloseEnemyHealthBeforeShot = 0.0f;
    float E02MinimumEnemyHealth = 0.0f;
    int32 StartingNotifyCount = 0;
    int32 StartingCommittedCount = 0;
    int32 WallCommittedCount = 0;
    int32 DeathCommittedCount = 0;
    int32 E02WallAmmoBeforeShot = 0;
    int32 E02CloseAmmoBeforeShot = 0;
    int32 E02MinimumPlayerAmmo = 30;
    int32 Stage = 0;
    int32 TypeIndex = 1;
    int32 ResetIndex = 0;
    int32 CaptureIndex = 0;
    int32 FailureCount = 0;
    int32 BaselineEnemyCount = 0;
    int32 BaselineControllerCount = 0;
    int32 BaselineWeaponCount = 0;
    int32 BaselinePlayerCount = 0;
    int32 BaselineHUDCount = 0;
    bool bFinished = false;
    bool bStageActionDone = false;
    bool bFireHeld = false;
    bool bSawPlayerDeath = false;
    bool bSawReloadDelay = false;
    bool bWasAutomatedBeforeFire = false;
    bool bResetCountsStayedExact = true;
    bool bPerformanceSampling = false;
    bool bPerformanceEightPending = false;
    bool bScreenshotOutstanding = false;
};
