#pragma once

#include "CoreMinimal.h"
#include "Weapons/HeliosCombatWeapons.h"
#include "HeliosChopsticksWeapon.generated.h"

class UMaterialInstanceDynamic;
class USphereComponent;

/** A visible, swept, single-hit throwing chopstick. Damage stays on the projectile. */
UCLASS()
class HELIOSOUTPOST_API AHeliosChopstickProjectile : public AShooterProjectile
{
    GENERATED_BODY()
public:
    AHeliosChopstickProjectile();
    void Configure(const FVector& Direction, float Damage, float Speed, float Range,
        UNiagaraSystem* Trail, UNiagaraSystem* Impact, USoundBase* Sound);
    void IgnorePartner(AHeliosChopstickProjectile* Partner);
    bool HasImpacted() const { return bHit; }
    virtual void Tick(float DeltaSeconds) override;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    virtual void NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp,
        bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;
private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Visual;
    UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UStaticMeshComponent>> Trails;
    UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UStaticMeshComponent>> Echoes;
    UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> TrailEffect;
    UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> ImpactEffect;
    UPROPERTY(Transient) TObjectPtr<USoundBase> ImpactSound;
    FVector LaunchLocation = FVector::ZeroVector;
    FVector FlightDirection = FVector::ForwardVector;
    float MaxRange = 5000.f;
    float FlightAge = 0.f;
    float SparkAge = 0.f;
    float HitAge = 0.f;
};

UCLASS()
class HELIOSOUTPOST_API AHeliosChopsticksWeapon : public AHeliosCombatWeapon
{
    GENERATED_BODY()
public:
    AHeliosChopsticksWeapon();
    virtual void Tick(float DeltaSeconds) override;
    virtual void DeactivateWeapon() override;
protected:
    virtual void BeginPlay() override;
    virtual void FireProjectile(const FVector& TargetLocation) override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
private:
    void ClearProjectiles();
    TArray<TWeakObjectPtr<AHeliosChopstickProjectile>> Projectiles;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> ReleaseArc;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ArcMaterial;
    float ReleaseAge = 10.f;
};

/** Bounded expanding gold ring; purely cosmetic, never radial damage. */
UCLASS(NotBlueprintable)
class HELIOSOUTPOST_API AHeliosChopstickImpact : public AActor
{
    GENERATED_BODY()
public:
    AHeliosChopstickImpact();
    virtual void Tick(float DeltaSeconds) override;
protected:
    virtual void BeginPlay() override;
private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Ring;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Material;
    float Age = 0.f;
};
