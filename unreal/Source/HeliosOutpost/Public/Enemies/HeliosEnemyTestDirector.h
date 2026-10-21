#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Enemies/HeliosEnemyActor.h"
#include "HeliosEnemyTestDirector.generated.h"

USTRUCT(BlueprintType)
struct HELIOSOUTPOST_API FHeliosEnemyDebugEntry
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString RuntimeActorId;
    UPROPERTY(BlueprintReadOnly) FString SourceMarkerId;
    UPROPERTY(BlueprintReadOnly) FString EnemyTypeId;
    UPROPERTY(BlueprintReadOnly) float HP = 0.0f;
    UPROPERTY(BlueprintReadOnly) bool bDead = false;
    UPROPERTY(BlueprintReadOnly) EHeliosEnemyCombatState CombatState = EHeliosEnemyCombatState::Idle;
    UPROPERTY(BlueprintReadOnly) int32 AttackNotifyCount = 0;
    UPROPERTY(BlueprintReadOnly) int32 AttacksCommitted = 0;
};

USTRUCT(BlueprintType)
struct HELIOSOUTPOST_API FHeliosEnemyDebugState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) int32 SelectedMode = 5;
    UPROPERTY(BlueprintReadOnly) int32 SpawnedCount = 0;
    UPROPERTY(BlueprintReadOnly) int32 AliveCount = 0;
    UPROPERTY(BlueprintReadOnly) TArray<FHeliosEnemyDebugEntry> Enemies;
};

/** Owns deterministic marker-based enemy spawning and complete test resets. */
UCLASS(BlueprintType)
class HELIOSOUTPOST_API AHeliosEnemyTestDirector : public AActor
{
    GENERATED_BODY()

public:
    AHeliosEnemyTestDirector();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    UFUNCTION(BlueprintCallable, Category = "Helios|Enemy Test")
    void SelectEnemyMode(int32 Mode);

    UFUNCTION(BlueprintCallable, Category = "Helios|Enemy Test")
    void ResetTest();

    UFUNCTION(BlueprintPure, Category = "Helios|Enemy Test")
    FHeliosEnemyDebugState GetEnemyDebugState() const;

    UFUNCTION(BlueprintPure, Category = "Helios|Enemy Test")
    int32 GetSelectedMode() const { return SelectedMode; }

    UFUNCTION(BlueprintPure, Category = "Helios|Enemy Test")
    int32 GetAliveEnemyCount() const;

    UFUNCTION(BlueprintPure, Category = "Helios|Enemy Test")
    TArray<AHeliosEnemyActor*> GetSpawnedEnemies() const;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helios|Enemy Test", meta = (ClampMin = "1", ClampMax = "18"))
    int32 SpawnCount = 4;

protected:
    void SpawnSelectedEnemies();
    void CleanupTestActors();
    TSubclassOf<AHeliosEnemyActor> ClassForType(EHeliosEnemyType Type) const;
    void SelectMode1();
    void SelectMode2();
    void SelectMode3();
    void SelectMode4();
    void SelectMode5();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Helios|Enemy Test")
    int32 SelectedMode = 5;

    UPROPERTY(Transient)
    TArray<TObjectPtr<AHeliosEnemyActor>> SpawnedEnemies;

    FTimerHandle InitialSpawnTimer;
    int32 ResetSerial = 0;
};
