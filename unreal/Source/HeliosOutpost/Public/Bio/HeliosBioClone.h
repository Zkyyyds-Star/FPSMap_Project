#pragma once

#include "CoreMinimal.h"
#include "Enemies/HeliosEnemyActor.h"
#include "HeliosBioClone.generated.h"

class UHeliosBioRoleDefinition;
class UAnimSequence;

/** Source WhiteBone puppet. Its health is independent of the match's participant roster. */
UCLASS()
class HELIOSOUTPOST_API AHeliosBioClone : public AHeliosEnemyActor
{
    GENERATED_BODY()
public:
    AHeliosBioClone();
    void ConfigureClone(int32 InMotherId, float InCloneMaxHP, float Damage, float Lifetime);
    // Unscaled base damage; match Human x6 / active Hunter skill scaling is applied here.
    float ApplyBioDamage(int32 AttackerId, float Damage);
    int32 GetMotherId() const { return MotherId; }
    bool IsActiveClone() const { return bConfigured && bSourceArtReady && !bIsDead && LifetimeRemaining > 0.f && !IsActorBeingDestroyed(); }
    float GetCloneMaxHealth() const { return CloneMaxHealth; }
    virtual void Tick(float DeltaSeconds) override;
    virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent,
        AController* EventInstigator, AActor* DamageCauser) override;
    virtual bool UsesTacticalCombat() const override { return false; }
    virtual void StartShooting(AActor* ActorToShoot) override {}
    virtual void StopShooting() override {}
    virtual void HandleAttackNotify() override {}

    // Explicit standalone tuning; original clone movement/range/cooldown are unverified.
    UPROPERTY(EditAnywhere, Category="BioClone|Standalone Tuning") float CloneMoveSpeed = 500.f;
    UPROPERTY(EditAnywhere, Category="BioClone|Standalone Tuning") float CloneMeleeRange = 145.f;
    UPROPERTY(EditAnywhere, Category="BioClone|Standalone Tuning") float CloneAttackInterval = 1.1f;
    UPROPERTY(EditAnywhere, Category="BioClone|Standalone Tuning") float CloneDecisionInterval = .25f;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Die() override;
private:
    UPROPERTY(Transient) TObjectPtr<UHeliosBioRoleDefinition> SourceRole;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> CloneIdle;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> CloneRun;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> CloneAttack;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> CloneDeath;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> CurrentClip;
    TWeakObjectPtr<AActor> Target;
    int32 MotherId = INDEX_NONE;
    int32 ConfiguredRoundSerial = INDEX_NONE;
    float CloneMaxHealth = 1.f;
    float LifetimeRemaining = 0.f;
    float DecisionRemaining = 0.f;
    float AttackCooldown = 0.f;
    float AttackPoseRemaining = 0.f;
    float DeathPoseRemaining = 0.f;
    bool bConfigured = false;
    bool bSourceArtReady = false;
    bool LoadSourceArt();
    void FindHumanTarget();
    bool HasMeleeLineOfSight(const AActor* Victim) const;
    void PlayCloneClip(UAnimSequence* Clip, bool bLoop);
};
