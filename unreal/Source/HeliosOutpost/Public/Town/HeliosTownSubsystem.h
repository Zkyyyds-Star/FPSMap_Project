#pragma once

#include "CoreMinimal.h"
#include "Player/HeliosLoadout.h"
#include "Subsystems/WorldSubsystem.h"
#include "NavigationSystem.h"
#include "HeliosTownSubsystem.generated.h"

class AHeliosAdventureDirector;
class AHeliosAdventureCharacter;
class AHeliosEnemyActor;
class AHeliosTownSupply;
class AHeliosMechWeapon;

/** Selected by the town's WorldSettings, never applied to the old adventure. */
UCLASS(Config=Engine)
class HELIOSOUTPOST_API UHeliosTownNavigationSystem : public UNavigationSystemV1
{
    GENERATED_BODY()
public:
    UHeliosTownNavigationSystem(const FObjectInitializer& ObjectInitializer);
    virtual void PostInitProperties() override;
};

struct FHeliosTownZone
{
    FString Id, Name;
    FVector Center = FVector::ZeroVector, Extent = FVector::ZeroVector, Checkpoint = FVector::ZeroVector;
};
struct FHeliosTownMeme
{
    FString Id, ZoneId, Name, Kind;
    FVector Position = FVector::ZeroVector;
    FVector Target = FVector::ZeroVector;
    bool bHasTarget = false;
    // Optional authored interior ownership. Missing data retains legacy map behavior.
    FString RoomId;
    int32 FloorId = 0;
    FVector SpaceCenter = FVector::ZeroVector, SpaceExtent = FVector::ZeroVector;
    float DiscoveryRadius = 900.f;
    bool bIndoorDiscovery = false;
};
struct FHeliosTownEncounter
{
    FString Id, ZoneId;
    FVector Center = FVector::ZeroVector;
    float Radius = 4500.f;
    bool bBoss = false, bTriggered = false;
    int32 Spawned = 0, Defeated = 0;
    int32 SpawnAttempts = 0;
    FString LastSpawnFailure;
    TArray<FVector> Spawns;
    TArray<int32> EnemyTypes;
    TArray<TWeakObjectPtr<AHeliosEnemyActor>> Actors;
};

/** Pure-data town progress; no references to streamed scenery survive a cell unload. */
UCLASS()
class HELIOSOUTPOST_API UHeliosTownSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    static bool IsTownMap(const UWorld* World);
    bool IsTownWorld() const { return bTown; }
    bool IsTown() const { return bTown; }
    bool IsWorldReady() const { return bConfigLoaded && bStreamingReady && bNavReady && !bRestorePending; }
    void BeginStreamingArrival(const FVector& Destination, bool bHoldMovementAfterReady = false);
    bool ReleaseArrivalMovement();
    void AttachDirector(AHeliosAdventureDirector* InDirector);
    void TickTown(float DeltaSeconds);
    void StartTown(FName WeaponId);
    void StartTownWithLoadout(const FHeliosLoadout& Loadout);
    void RespawnAtCheckpoint();
    float GetConfiguredRespawnDelay() const;
    bool SaveProgress();
    bool ResetProgress();
    bool ResetTown() { return ResetProgress(); }
    bool Interact(AHeliosAdventureCharacter* Character);
    bool InteractNearest();
    bool TeleportToCheckpoint(const FString& ZoneId);
    bool UnlockMeme(const FString& MemeId);
    bool TriggerEncounter(const FString& EncounterId);
    void RecordBasketballScore(const FString& MemeId);
    FString GetStateJson() const;
    FString GetCurrentZoneId() const { return CurrentZoneId; }
    FString GetCurrentZoneName() const;
    FString GetCheckpointId() const { return CheckpointId; }
    FVector GetCheckpointLocation() const;
    int32 GetExploredDistrictCount() const { return Explored.Num(); }
    int32 GetExploredCount() const { return Explored.Num(); }
    int32 GetDiscoveredMemeCount() const { return Discovered.Num(); }
    int32 GetMemeCount() const { return Discovered.Num(); }
    int32 GetCompletedEncounterCount() const { return Completed.Num(); }
    int32 GetDefeatedBossCount() const;
    FName GetSavedWeaponId() const { return WeaponId; }
    FHeliosLoadout GetSavedLoadout() const { return SavedLoadout; }
    int32 GetSurvivalEnemyCap() const;
    int32 GetPendingBossCount() const { return bBossPending ? 1 : 0; }
    float GetSecondsUntilNextBoss() const;
    FString GetTimerStopReason() const;
private:
    bool LoadConfig();
    void LoadProgress();
    void UpdateReadiness();
    void UpdateExploration();
    bool CanDiscoverIndoor(const FHeliosTownMeme& Meme) const;
    void UpdateSurvival(float DeltaSeconds);
    void ResetSurvival(bool bResetClock);
    void BuildSurvivalCandidates(FHeliosTownEncounter& Encounter);
    void UpdateSupplies();
    void ResetUnfinishedEncounters();
    bool TrySpawn(FHeliosTownEncounter& Encounter);
    bool FindSafeSpawn(const FVector& Candidate, bool bBoss, int32 Type, FVector& Result, FString* Failure = nullptr) const;
    const FHeliosTownMeme* NearestMeme() const;
    void FinishRespawn();
    void SelectRandomSpawnZone();
    UPROPERTY(Transient) TObjectPtr<AHeliosAdventureDirector> Director;
    TArray<FHeliosTownZone> Zones;
    TArray<FHeliosTownMeme> Memes;
    TArray<FHeliosTownMeme> Interiors;
    TArray<FHeliosTownMeme> Supplies;
    TSet<FString> ConsumedSupplies;
    TMap<FString,TWeakObjectPtr<AHeliosTownSupply>> SupplyActors;
    TArray<FHeliosTownEncounter> Encounters;
    TSet<FString> Explored, Discovered, Completed;
    FString CurrentZoneId, CheckpointId, LastMessage, ConfigError;
    FHeliosLoadout SavedLoadout;
    FName WeaponId = TEXT("cfm_chan_zeus");
    bool bTown = false, bConfigLoaded = false, bStreamingReady = false, bNavReady = false;
    bool bInvoked = false, bRespawning = false, bRestorePending = false;
    float SinceAttempt = 0.f, SinceSave = 0.f;
    double RespawnTime = 0.0;
    double LastInteractionTime = -100.;
    int32 BasketballScores = 0;
    double MissingNavigationSince = -1.;
    double LastNavigationRepairTime = -1000.;
    int32 NavigationRepairCount = 0;
    FVector ArrivalDestination = FVector::ZeroVector;
    double ArrivalReadySince = -1.;
    FString ArrivalGroundSignature;
    float ArrivalGroundZ = 0.f;
    bool bHoldArrivalMovement = false;
    FString ArrivalBlockReason;
    TWeakObjectPtr<AHeliosMechWeapon> ArrivalMech;
    FHeliosTownEncounter SurvivalNormal, SurvivalBoss;
    float NextNormalAt = 10.f, NextBossAt = 60.f, NextBossAttemptAt = 60.f;
    bool bBossPending = false, bSessionStarted = false;
    int32 NormalSpawnedTotal = 0, BossSpawnedTotal = 0;
};
