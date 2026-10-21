#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosBioTurret.generated.h"

class AHeliosBioHotelGameMode;
class UHeliosBioRoleDefinition;
class UStaticMeshComponent;

/** Mechanical hunter's source turret; health is separate from the participant roster. */
UCLASS()
class HELIOSOUTPOST_API AHeliosBioTurret : public AActor
{
    GENERATED_BODY()
public:
    AHeliosBioTurret();
    void Configure(int32 InCasterId, int32 InExpectedRound, UHeliosBioRoleDefinition* Definition);
    bool IsActiveTurret() const;
    int32 GetCasterId() const { return CasterId; }
    float GetHealth() const { return CurrentHealth; }
    float GetMaxHealth() const { return MaximumHealth; }
    float GetLifetimeRemaining() const { return LifetimeRemaining; }
    int32 GetShots() const { return Shots; }
    int32 GetExplosionHits() const { return ExplosionHits; }
    // Accepts unscaled native weapon damage and rejects friendly/unattributed damage.
    float ApplyBioDamage(int32 AttackerId, float Damage);
    virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent,
        AController* EventInstigator, AActor* DamageCauser) override;
    virtual void Tick(float DeltaSeconds) override;

private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> SourceMesh;
    UPROPERTY(Transient) TObjectPtr<UHeliosBioRoleDefinition> SourceDefinition;
    TWeakObjectPtr<AActor> CasterActor;
    int32 CasterId = INDEX_NONE;
    int32 ExpectedRound = INDEX_NONE;
    int32 Shots = 0;
    int32 ExplosionHits = 0;
    float MaximumHealth = 1000.f;
    float CurrentHealth = 0.f;
    float LifetimeRemaining = 0.f;
    float FireCooldown = 0.f;
    float FireInterval = .2f;
    float FireRange = 1000.f;
    float FireDamage = 160.f;
    float ExplosionRadius = 300.f;
    float ExplosionDamage = 1000.f;
    bool bConfigured = false;
    bool bResolved = false;
    bool bCasterHumanSide = true;
    AHeliosBioHotelGameMode* FindActiveMatch() const;
    FVector FiringOrigin() const;
    bool HasLineOfSight(const FVector& Origin, const AActor* Victim) const;
    void FireAtOpponent(AHeliosBioHotelGameMode* Mode);
    void ResolveExplosion();
    void RemoveWithoutExplosion();
};
