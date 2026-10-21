#pragma once

#include "CoreMinimal.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "HeliosEnemyActor.generated.h"

class UAnimSequence;
class UPointLightComponent;
class AHeliosBasketballProjectile;

UENUM(BlueprintType)
enum class EHeliosEnemyType : uint8
{
    None = 0 UMETA(Hidden),
    E01_Rifle = 1,
    E02_Charger = 2,
    E03_Skirmisher = 3,
    E04_Basketball = 4,
    /** Reserved for the adventure director only; never selected by normal mob rolls. */
    E05_Boss = 5
};

UENUM(BlueprintType)
enum class EHeliosEnemyCombatState : uint8
{
    Idle,
    Walk,
    Run,
    Turn,
    Attack,
    Hit,
    Dead
};

USTRUCT(BlueprintType)
struct HELIOSOUTPOST_API FHeliosEnemyDefinition
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) EHeliosEnemyType Type = EHeliosEnemyType::E01_Rifle;
    UPROPERTY(BlueprintReadOnly) FString TypeId = TEXT("E01");
    UPROPERTY(BlueprintReadOnly) FString MeshAssetPath;
    UPROPERTY(BlueprintReadOnly) float CapsuleRadius = 34.0f;
    UPROPERTY(BlueprintReadOnly) float CapsuleHalfHeight = 80.0f;
    UPROPERTY(BlueprintReadOnly) float MaxHP = 100.0f;
    UPROPERTY(BlueprintReadOnly) float WalkSpeed = 260.0f;
    UPROPERTY(BlueprintReadOnly) float RunSpeed = 430.0f;
    /** Measured in-place animation ground speeds used to prevent foot sliding. */
    UPROPERTY(BlueprintReadOnly) float WalkAnimationGroundSpeed = 155.0f;
    UPROPERTY(BlueprintReadOnly) float RunAnimationGroundSpeed = 350.0f;
    UPROPERTY(BlueprintReadOnly) float Damage = 12.0f;
    UPROPERTY(BlueprintReadOnly) float Range = 1800.0f;
    UPROPERTY(BlueprintReadOnly) float Interval = 1.2f;
    UPROPERTY(BlueprintReadOnly) int32 BurstShotCount = 1;
    UPROPERTY(BlueprintReadOnly) float BurstShotInterval = 0.1f;
    UPROPERTY(BlueprintReadOnly) FString AttackAnimationName = TEXT("Attack");
    UPROPERTY(BlueprintReadOnly) float AttackAnimationDuration = 1.0f;
    UPROPERTY(BlueprintReadOnly) float HitAnimationDuration = 0.6f;
    UPROPERTY(BlueprintReadOnly) float DeathAnimationDuration = 1.8f;

    static FHeliosEnemyDefinition ForType(EHeliosEnemyType InType);
};

/** Shared native enemy used by the official Shooter StateTree and AI controller. */
UCLASS(BlueprintType)
class HELIOSOUTPOST_API AHeliosEnemyActor : public AShooterNPC
{
    GENERATED_BODY()
    friend class FHeliosDamageActorsTest;
    friend class FHeliosMinionCombatRulesTest;

public:
    AHeliosEnemyActor();

    virtual void Tick(float DeltaSeconds) override;
    virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent,
        AController* EventInstigator, AActor* DamageCauser) override;
    virtual void StartShooting(AActor* ActorToShoot) override;
    virtual void StopShooting() override;
    virtual void AttachWeaponMeshes(AShooterWeapon* WeaponToAttach) override;
    virtual bool UsesTacticalCombat() const override
    {
        return EnemyType >= EHeliosEnemyType::E01_Rifle && EnemyType <= EHeliosEnemyType::E04_Basketball;
    }

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Helios|Awareness")
    FShooterAwarenessSettings AwarenessSettings;

    virtual const FShooterAwarenessSettings* GetAwarenessSettings() const override
    {
        return EnemyType >= EHeliosEnemyType::E01_Rifle && EnemyType <= EHeliosEnemyType::E04_Basketball
            ? &AwarenessSettings : nullptr;
    }

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Helios|Enemy")
    EHeliosEnemyType EnemyType = EHeliosEnemyType::E01_Rifle;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Helios|Enemy")
    FString EnemyTypeId = TEXT("E01");

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Helios|Enemy")
    FString RuntimeActorId;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Helios|Enemy")
    FString SourceMarkerId;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Helios|Combat")
    float AttackDamage = 12.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Helios|Combat")
    float AttackRange = 1800.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Helios|Combat")
    float AttackInterval = 1.2f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Helios|Combat")
    TSubclassOf<AHeliosBasketballProjectile> BasketballProjectileClass;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Helios|Debug")
    int32 AttackCount = 0;

    UFUNCTION(BlueprintPure, Category = "Helios|Debug")
    bool IsEnemyDead() const { return bIsDead; }

    UFUNCTION(BlueprintPure, Category = "Helios|Debug")
    EHeliosEnemyCombatState GetCombatState() const { return CombatState; }

    UFUNCTION(BlueprintPure, Category = "Helios|Debug")
    int32 GetAttackCount() const { return AttackCount; }

    UFUNCTION(BlueprintPure, Category = "Helios|Debug")
    FString GetEnemyTypeId() const { return EnemyTypeId; }

    UFUNCTION(BlueprintPure, Category = "Helios|Debug")
    FString GetRuntimeActorId() const { return RuntimeActorId; }

    UFUNCTION(BlueprintPure, Category = "Helios|Debug")
    float GetHealth() const { return CurrentHP; }

    UFUNCTION(BlueprintPure, Category = "Helios|Debug")
    int32 GetAttackNotifyCount() const { return AttackNotifyCount; }

    UFUNCTION(BlueprintPure, Category = "Helios|Debug")
    int32 GetAttacksCommitted() const { return AttacksCommitted; }

    /** Called by UHeliosAttackNotify at the authored impact frame. */
    UFUNCTION(BlueprintCallable, Category = "Helios|Combat")
    virtual void HandleAttackNotify();

    void SetRuntimeIdentity(const FString& InRuntimeActorId, const FString& InMarkerId);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Die() override;

    virtual void ApplyTypeDefaults();
    virtual ECreatureHealthType GetHealthSettingsType() const override;
    virtual void LoadAnimationAssets();
    virtual void BeginAttackCycle();
    virtual void FinishAttackCycle();
    /** Bosses retain their attack state when hit, preventing rapid-fire stun locks. */
    virtual bool ShouldInterruptOnHitReaction() const { return true; }
    void FinishHitReaction();
    void UpdateTacticalMovement();
    bool IsLivingCombatTarget(const AActor* Target) const;
    bool CanStartTacticalAttack(AActor* Target) const;
    bool FindTacticalStep(AActor* Target, bool bRetreat, FVector& OutGoal) const;
    bool RequestTacticalMove(const FVector& Goal, float Acceptance);
    bool BeginTacticalStep(bool bAlwaysReposition);
    void StopTacticalMovement();
    void TriggerBlink();
    void ClearBlink();
    void RefreshLocomotionAnimation(float DeltaSeconds);
    void UpdateLocomotionPlayRate(float DeltaSeconds);
    virtual void PlayStateAnimation(EHeliosEnemyCombatState NewState, UAnimSequence* Sequence, bool bLoop);
    bool HasClearAttackLine(AActor* Target, FVector& OutImpactPoint) const;
    bool HasClearAttackLineFrom(AActor* Target, const FVector& Start, FVector& OutImpactPoint) const;
    void PerformHitscanOrMeleeAttack(AActor* Target);
    void StartRangedBurst(AActor* Target);
    void FireBurstShot();
    void CancelRangedBurst();
    void TurnOffMuzzleFlash();
    void ThrowBasketball(AActor* Target);
    UAnimSequence* LoadEnemyAnimation(const TCHAR* Name, bool bPreferRetargeted) const;

    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> IdleAnimation;
    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> WalkAnimation;
    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> RunAnimation;
    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> TurnAnimation;
    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> AttackAnimation;
    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> HitAnimation;
    UPROPERTY(Transient)
    TObjectPtr<UAnimSequence> DeathAnimation;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Helios|Effects")
    TObjectPtr<UPointLightComponent> MuzzleFlashLight;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Helios|Debug")
    EHeliosEnemyCombatState CombatState = EHeliosEnemyCombatState::Idle;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Helios|Debug")
    int32 AttackNotifyCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Helios|Debug")
    int32 AttacksCommitted = 0;

    FHeliosEnemyDefinition Definition;

    bool bAttackCommittedThisCycle = false;
    bool bHitReactionPlaying = false;
    bool bSkirmishRepositioning = false;
    float PreviousYaw = 0.0f;
    FTimerHandle AttackCycleTimer;
    FTimerHandle HitReactionTimer;
    FTimerHandle BlinkTimer;
    FTimerHandle BlinkEndTimer;
    FTimerHandle BurstShotTimer;
    FTimerHandle MuzzleFlashTimer;
    TWeakObjectPtr<AActor> BurstTarget;
    int32 BurstShotsRemaining = 0;
    bool bBurstActive = false;
    float TacticalMoveCooldown = 0.0f;
    float SmoothedLocomotionPlayRate = 1.0f;
    // Absolute cadence survives StateTree re-entry, target switches and hit reactions.
    double NextAttackAllowedAt = 0.0;
    // Friendly summons and pets cannot repeatedly restart a minion's hit reaction.
    double NextFriendlyHitReactionAllowedAt = 0.0;
    double TacticalStepEndsAt = 0.0;
    bool bAttackCycleActive = false;
    bool bTacticalMoveRequested = false;
    FVector LastTacticalGoal = FVector::ZeroVector;
    TWeakObjectPtr<AActor> AttackCycleTarget;
};

UCLASS()
class HELIOSOUTPOST_API AHeliosEnemyE01 : public AHeliosEnemyActor
{
    GENERATED_BODY()
public:
    AHeliosEnemyE01();
};

UCLASS()
class HELIOSOUTPOST_API AHeliosEnemyE02 : public AHeliosEnemyActor
{
    GENERATED_BODY()
public:
    AHeliosEnemyE02();
};

UCLASS()
class HELIOSOUTPOST_API AHeliosEnemyE03 : public AHeliosEnemyActor
{
    GENERATED_BODY()
public:
    AHeliosEnemyE03();
};

UCLASS()
class HELIOSOUTPOST_API AHeliosEnemyE04 : public AHeliosEnemyActor
{
    GENERATED_BODY()
public:
    AHeliosEnemyE04();
};
