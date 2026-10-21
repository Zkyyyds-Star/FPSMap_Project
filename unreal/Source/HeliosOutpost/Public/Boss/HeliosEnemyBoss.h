#pragma once
#include "CoreMinimal.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Boss/HeliosBossAttackTypes.h"
#include "HeliosEnemyBoss.generated.h"

class UAnimSequence;
class UHeliosBossEffectsComponent;

/** E05 owns its combat, animation clock and per-cast contact history. */
UCLASS(BlueprintType)
class HELIOSOUTPOST_API AHeliosEnemyBoss : public AHeliosEnemyActor
{
    GENERATED_BODY()
    friend class FHeliosDamageActorsTest;
public:
    AHeliosEnemyBoss();
    virtual void BeginPlay() override;
    virtual const FShooterAwarenessSettings* GetAwarenessSettings() const override { return &BossAwarenessSettings; }
    virtual bool OwnsNativeCombatMovement() const override { return true; }
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Helios|Awareness")
    FShooterAwarenessSettings BossAwarenessSettings;
    virtual void Tick(float DeltaSeconds) override;
    virtual void StartShooting(AActor* ActorToShoot) override;
    virtual void StopShooting() override;
    virtual void HandleAttackNotify() override;
    UFUNCTION(BlueprintPure, Category="Helios|Boss") float GetBossMaxHealth() const { return Definition.MaxHP; }
    UFUNCTION(BlueprintPure, Category="Helios|Boss") int32 GetBossPhase() const;
    UFUNCTION(BlueprintPure, Category="Helios|Boss") int32 GetBossNumber() const { return BossNumber; }
    UFUNCTION(BlueprintPure, Category="Helios|Boss") EHeliosBossAttackType GetBossAttackType() const { return BossAttack; }
    UFUNCTION(BlueprintPure, Category="Helios|Boss") float GetBossWindupProgress() const;
    UFUNCTION(BlueprintPure, Category="Helios|Boss") bool IsBossAttackReleasing() const;
    UFUNCTION(BlueprintPure, Category="Helios|Boss") float GetBossAttackElapsed() const { return AttackElapsed; }
    UFUNCTION(BlueprintPure, Category="Helios|Boss") float GetBossAttackDuration() const;
    UFUNCTION(BlueprintPure, Category="Helios|Boss") float GetBossWaveRadius() const { return WaveRadius; }
    UFUNCTION(BlueprintPure, Category="Helios|Boss") FVector GetBossAttackOrigin() const { return AttackOrigin; }
    UFUNCTION(BlueprintPure, Category="Helios|Boss") FVector GetBossMouthLocation() const;
    UFUNCTION(BlueprintPure, Category="Helios|Boss") FVector GetBossAttackDirection() const { return AttackDirection; }
    UFUNCTION(BlueprintPure, Category="Helios|Boss") int32 GetBossCastSerial() const { return CastSerial; }
    void SetBossNumber(int32 InBossNumber) { BossNumber = InBossNumber; }
    /** Set before FinishSpawning only for reused urban midbosses. Defaults preserve old maps. */
    void SkipEntranceForUrbanMidboss() { bSkipUrbanEntrance = true; }
    float GetCloseAttackCooldownSeconds() const { return CloseCooldownSeconds; }
    float GetShoulderAttackCooldownSeconds() const { return ShoulderCooldownSeconds; }
protected:
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void LoadAnimationAssets() override;
    virtual void PlayStateAnimation(EHeliosEnemyCombatState NewState, UAnimSequence* Sequence, bool bLoop) override;
    virtual void BeginAttackCycle() override;
    virtual void FinishAttackCycle() override;
    virtual void Die() override;
    virtual bool ShouldInterruptOnHitReaction() const override { return false; }
private:
    void StartAbility(EHeliosBossAttackType Type);
    void ReleaseAbility();
    void TickAbility(float DeltaSeconds);
    void TickShoulder(float DeltaSeconds);
    void TickWave(float NewRadius);
    void TickMovement(float DeltaSeconds);
    void TickAnimation(float DeltaSeconds);
    void ClearCombat();
    bool HasBossLineOfSight(AActor* Target, const FVector& Origin) const;
    bool HasClearContactPath(AActor* Target, const FVector& Origin) const;
    FVector ShoulderLocation() const;
    float DamageContactTarget(AActor* Target, float Damage, bool bPush);
    float WindupDuration() const;
    float ActiveDuration() const;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UHeliosBossEffectsComponent> BossEffects;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> ChargeAnimation;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> EntranceAnimation;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> CloseShoulderAnimation;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> UnusedLegacyAnimation;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> CurrentBossAnimation;
    TSet<TWeakObjectPtr<AActor>> HitTargets;
    TMap<TWeakObjectPtr<AActor>, FVector> PreviousPlayerLocations;
    EHeliosBossAttackType BossAttack = EHeliosBossAttackType::None;
    FVector AttackDirection = FVector::ForwardVector;
    FVector AttackOrigin = FVector::ZeroVector;
    FVector PreviousShoulder = FVector::ZeroVector;
    float AttackElapsed = 0.0f;
    float AnimationElapsed = 0.0f;
    float EntranceRemaining = 0.0f;
    float ChargeDistance = 0.0f;
    float WaveRadius = 0.0f;
    float ShoulderCooldown = 0.0f;
    float CloseCooldown = 0.0f;
    float ShoulderCooldownSeconds = 4.5f;
    float CloseCooldownSeconds = 2.0f;
    float ShoulderSpeed = 1500.0f;
    float CloseSpeed = 900.0f;
    float RadialCooldown = 0.0f;
    float ActionWindow = 0.0f;
    float MoveRefresh = 0.0f;
    FVector LastMoveGoal = FVector::ZeroVector;
    FVector ProgressLocation = FVector::ZeroVector;
    float NoProgressTime = 0.0f;
    bool bHasMoveGoal = false;
    TWeakObjectPtr<AActor> PursuitTarget;
    int32 CastSerial = 0;
    int32 BossNumber = 0;
    bool bEntering = true;
    bool bSkipUrbanEntrance = false;
    bool bReleased = false;
    bool bChargeStopped = false;
    bool bAnimationLoop = true;
};
