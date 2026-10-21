#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Adventure/HeliosUrbanWaveSchedule.h"
#include "Player/HeliosLoadout.h"
#include "HeliosAdventureDirector.generated.h"

class AHeliosAdventurePickup;
class AHeliosUrbanHazard;
class AHeliosArenaCharacter;
class AHeliosMarker;
class AHeliosZone;
class AHeliosAdventureQA;
class AHeliosEnemyBoss;
class UHeliosWeaponDefinition;
class UHeliosTownSubsystem;

/** Runtime owner for the replayable Helios survival run. */
UCLASS(BlueprintType)
class HELIOSOUTPOST_API AHeliosAdventureDirector : public AActor
{
    friend class AHeliosQingYaQA;
    GENERATED_BODY()

public:
    AHeliosAdventureDirector();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaSeconds) override;

    UFUNCTION(BlueprintPure, Category="Helios|Adventure") int32 GetScore() const { return Score; }
    UFUNCTION(BlueprintPure, Category="Helios|Adventure") int32 GetKills() const { return Kills; }
    UFUNCTION(BlueprintPure, Category="Helios|Adventure") int32 GetExploredCount() const { return ExploredZones.Num(); }
    UFUNCTION(BlueprintPure, Category="Helios|Adventure") int32 GetAliveCount() const;
    UFUNCTION(BlueprintPure, Category="Helios|Adventure") int32 GetEnemyCap() const;
    UFUNCTION(BlueprintPure, Category="Helios|Adventure") int32 GetSeed() const { return Seed; }
    int32 GetRunSerial() const { return RunSerial; }
    UFUNCTION(BlueprintPure, Category="Helios|Adventure") int32 GetBestScore() const { return BestScore; }
    UFUNCTION(BlueprintPure, Category="Helios|Adventure") float GetSurvivalSeconds() const { return SurvivalSeconds; }
    UFUNCTION(BlueprintPure, Category="Helios|Adventure") float GetBestSeconds() const { return BestSeconds; }
    UFUNCTION(BlueprintPure, Category="Helios|Adventure") FString GetZoneName() const { return CurrentZoneName; }
    UFUNCTION(BlueprintPure, Category="Helios|Adventure") bool IsRunOver() const { return bRunOver; }
    UFUNCTION(BlueprintPure, Category="Helios|Adventure") int32 GetBossCount() const;
    UFUNCTION(BlueprintPure, Category="Helios|Adventure") int32 GetBossKills() const { return BossKills; }
    UFUNCTION(BlueprintPure, Category="Helios|Adventure") float GetSecondsUntilNextBoss() const;
    UFUNCTION(BlueprintPure, Category="Helios|Adventure") int32 GetBossSpawnSerial() const { return BossSpawnSerial; }
    UFUNCTION(BlueprintPure, Category="Helios|Adventure") TArray<AHeliosEnemyBoss*> GetBossActors() const;
    UFUNCTION(BlueprintPure, Category="Helios|Adventure") int32 GetPendingBossCount() const;

    UFUNCTION(BlueprintCallable, Category="Helios|Adventure") void RestartRun();
    UFUNCTION(BlueprintCallable, Category="Helios|Adventure") void EnterWeaponSelection();
    UFUNCTION(BlueprintCallable, Category="Helios|Adventure") bool StartRun(FName WeaponId);
    UFUNCTION(BlueprintCallable, Category="Helios|Adventure") bool StartRunWithLoadout(const FHeliosLoadout& Loadout);
    UFUNCTION(BlueprintPure, Category="Helios|Adventure") FHeliosLoadout GetSelectedLoadout() const { return SelectedLoadout; }
    /** Preserve the equipped pair at runtime without overwriting a pending menu selection. */
    bool CaptureActiveLoadout();
    UFUNCTION(BlueprintPure, Category="Helios|Adventure") TArray<UHeliosWeaponDefinition*> GetAvailableWeapons() const;
    UFUNCTION(BlueprintPure, Category="Helios|Adventure") bool IsSelectingWeapon() const { return bSelectingWeapon; }
    UFUNCTION(BlueprintPure, Category="Helios|Adventure") FName GetSelectedWeaponId() const { return SelectedWeaponId; }
    FString GetRunPhase() const;
    bool IsUrbanChallenge() const;
    FString GetUrbanWaveText() const;
    bool IsUrbanVictory() const { return bUrbanVictory; }
    int32 GetUrbanWaveNumber() const { return UrbanWaveIndex + 1; }
    bool CanSkipUrbanWave() const;
    bool TrySkipUrbanWave(int32 ExpectedRun, int32 ExpectedWave);
    UFUNCTION(BlueprintCallable, Category="Helios|Adventure") void EndRun();
    UFUNCTION(BlueprintCallable, Category="Helios|Adventure") void SetRunPaused(bool bPaused);
    UFUNCTION(BlueprintCallable, Category="Helios|Adventure") bool TryCollect(bool bMedical);
    UFUNCTION(BlueprintPure, Category="Helios|Adventure") FString GetStateJson() const;

    static AHeliosAdventureDirector* Find(UWorld* World);
    bool IsTownMode() const;
    UHeliosTownSubsystem* GetTownSubsystem() const;

private:
    friend class UHeliosTownSubsystem;
    friend class AHeliosAdventureQA;
    friend class AHeliosGuanXiaoYuQA;
    friend class AHeliosUrbanQA;
    friend class AHeliosChanZeusQA;
    friend class AHeliosBlackDragonSoulQA;
    friend class AHeliosWangZheKuiThunderQA;
    friend class AHeliosYunSummerMegatronQA;
    friend class AHeliosBuLianShiP90QA;
#if WITH_DEV_AUTOMATION_TESTS
    friend class UHeliosCodexMCPSubsystem;
#endif

    void CacheWorldActors();
    void UpdateEnemies(float DeltaSeconds);
    void UpdateBosses();
    void UpdateZone();
    void UpdatePickups();
    bool TrySpawnEnemy(bool bAllowVisible = false, AHeliosMarker* OnlyMarker = nullptr,
        EHeliosEnemyType ForcedType = EHeliosEnemyType::E01_Rifle);
    bool TrySpawnPendingBoss(bool bAllowVisible = false, AHeliosMarker* OnlyMarker = nullptr);
    bool TrySpawnUrbanFinalBoss();
    bool SpawnPickup(bool bMedical, const FString& PreferredZone = FString(), bool bAllowVisible = false);
    bool IsCandidateVisible(const FVector& Location, const AActor* Candidate = nullptr) const;
    bool IsSpawnCandidateValid(const AHeliosMarker& Marker, EHeliosEnemyType Type,
        FVector& OutLocation, bool bAllowVisible = false, FString* Failure = nullptr) const;
    bool IsBossSpawnCandidateValid(const AHeliosMarker& Marker, FVector& OutLocation,
        bool bAllowVisible = false) const;
    TSubclassOf<AHeliosEnemyActor> ClassForType(EHeliosEnemyType Type) const;
    void DestroyEnemy(AHeliosEnemyActor* Enemy);
    void CleanupOwnedActors();
    void LoadBest();
    void SaveBest() const;
    void PlayPlayerCue(FName Cue) const;
    void TickUrbanWaves(float DeltaSeconds);
    bool TickUrbanSpawnRecovery(float DeltaSeconds);
    void ClearUrbanSpawnRecovery();
    void AdvanceUrbanWave(bool bSkipped);
    void CancelUrbanWaveActors();
    EHeliosEnemyType ChooseUrbanRegularType();
    float UrbanEmptySpawnSeconds = 0.f;
    float UrbanRecoveryWarningRemaining = 0.f;
    double UrbanRecoveryWarningStartedWall = 0;
    double UrbanLastRecoveryWarningWallSeconds = 0;
    bool bUrbanRecoveryBoss = false;
    int32 UrbanRecoverySpawns = 0;
    FString UrbanLastSpawnDiagnostics;
    UPROPERTY(Transient) TObjectPtr<AHeliosMarker> UrbanRecoveryMarker;
    UPROPERTY(Transient) TObjectPtr<AHeliosUrbanHazard> UrbanRecoveryBeacon;
    FHeliosUrbanWaveSchedule UrbanSchedule;
    int32 UrbanWaveIndex = 0;
    int32 UrbanRegularIssued = 0;
    int32 UrbanBossIssued = 0;
    int32 UrbanWaveStartKills = 0;
    int32 UrbanWaveStartBossKills = 0;
    float UrbanRestRemaining = 10.f;
    float UrbanSpawnRemaining = 0.f;
    bool bUrbanVictory = false;
    int32 UrbanSkippedWaves = 0;
    FString UrbanConfigError;

    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosEnemyActor>> Enemies;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosEnemyBoss>> Bosses;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosAdventurePickup>> Pickups;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosMarker>> EnemyMarkers;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosMarker>> SupplyMarkers;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosMarker>> PickupMarkers;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosZone>> Zones;
    UPROPERTY(Transient) TObjectPtr<AHeliosArenaCharacter> Player;

    TSet<TWeakObjectPtr<AHeliosEnemyActor>> CountedDeaths;
    TSet<TWeakObjectPtr<AHeliosEnemyBoss>> CountedBossDeaths;
    TMap<TWeakObjectPtr<AHeliosEnemyActor>, float> HiddenFarSeconds;
    TSet<FString> ExploredZones;
    FRandomStream Random;
    FVector RunSpawnLocation = FVector::ZeroVector;
    float SurvivalSeconds = 0.0f;
    float NextEnemyAttemptSeconds = 10.0f;
    float NextCollectibleSeconds = 90.0f;
    float NextMedicalSeconds = 60.0f;
    float BossIntervalSeconds = 60.0f;
    float NextBossEventSeconds = 60.0f;
    float NextBossSpawnAttemptSeconds = 0.0f;
    int32 Score = 0;
    int32 Kills = 0;
    int32 BossKills = 0;
    int32 Seed = 0;
    int32 BestScore = 0;
    float BestSeconds = 0.0f;
    int32 RunSerial = 0;
    int32 SpawnSerial = 0;
    int32 BossEventSerial = 0;
    /** Public serial: incremented only after a boss actually finishes spawning. */
    int32 BossSpawnSerial = 0;
    TArray<int32> PendingBossNumbers;
    bool bRunOver = false;
    bool bSelectingWeapon = true;
    FHeliosLoadout SelectedLoadout;
    FName SelectedWeaponId = TEXT("cfm_chan_zeus");
    FString CurrentZoneName = TEXT("未知区域");
};
