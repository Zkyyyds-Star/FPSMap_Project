#pragma once

#include "CoreMinimal.h"
#include "ShooterProjectile.h"
#include "ShooterWeapon.h"
#include "HeliosCombatWeapons.generated.h"

class UAudioComponent;
class UPointLightComponent;
class UProjectileMovementComponent;
class UStaticMeshComponent;
class USoundBase;
class UNiagaraSystem;
class UNiagaraComponent;
class UHeliosWeaponDefinition;

namespace HeliosCombatWeaponMath
{
    HELIOSOUTPOST_API void InitializeRocketWorldVelocity(
        UProjectileMovementComponent& Movement, const FVector& WorldDirection, float Speed);
    HELIOSOUTPOST_API FVector ResolveRocketSpawnLocation(
        const FVector& DesiredLocation, const FHitResult& MuzzleObstruction);
    HELIOSOUTPOST_API FVector CalculatePelletEndpoint(
        const FVector& ViewStart, const FVector& WorldDirection, float Range);
    HELIOSOUTPOST_API FVector ExtendTracePastSurface(
        const FVector& TraceStart, const FVector& SurfacePoint, float Extension = 1.0f);
}

UCLASS(Abstract)
class HELIOSOUTPOST_API AHeliosCombatWeapon : public AShooterWeapon
{
    GENERATED_BODY()
    friend class FHeliosDamageActorsTest;

public:
    AHeliosCombatWeapon();
    virtual void Tick(float DeltaSeconds) override;
    virtual void BeginReload(float DurationSeconds) override;
    virtual void ReloadMagazine() override;
    virtual void CancelReload() override;
    virtual void PlayDryFire() override;
    virtual void ApplyDefinition(const UHeliosWeaponDefinition* Definition);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Fire() override;
    void PlayWeaponSound(USoundBase* Sound, float Volume = 1.0f) const;
    FVector GetVisualMuzzleLocation() const;
    FHitResult TraceFromMuzzle(const FVector& ViewStart, const FVector& DesiredEnd,
        FName TraceTag) const;

    UPROPERTY(VisibleAnywhere, Category="Helios|Weapon") TObjectPtr<UStaticMeshComponent> FirstPersonModel;
    UPROPERTY(VisibleAnywhere, Category="Helios|Weapon") TObjectPtr<UStaticMeshComponent> ThirdPersonModel;
    UPROPERTY(Transient) TObjectPtr<USoundBase> FireSound;
    UPROPERTY(Transient) TObjectPtr<USoundBase> ReloadSound;
    UPROPERTY(Transient) TObjectPtr<USoundBase> DrySound;
    UPROPERTY(Transient) TObjectPtr<USoundBase> ResidentActionSound;
    FString MeshAssetPath;
    FString FireSoundPath;
    FString ReloadSoundPath;
    FVector ModelRestLocation = FVector::ZeroVector;
    FRotator ModelRestRotation = FRotator::ZeroRotator;
    FVector ModelMuzzleLocation = FVector(100.0f, 0.0f, 10.0f);
    float ReloadAnimationDuration = 0.0f;
    float ReloadAnimationElapsed = 0.0f;
    bool bReloadAnimating = false;
    UPROPERTY(Transient) TObjectPtr<UHeliosWeaponDefinition> ActiveDefinition;
    UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> ArtImpact;
    UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> ArtTrail;
    UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> ArtExplosion;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraSystem>> ResidentEffects;
};

UCLASS()
class HELIOSOUTPOST_API AHeliosRocketProjectile : public AShooterProjectile
{
    GENERATED_BODY()

public:
    AHeliosRocketProjectile();
    virtual void Tick(float DeltaSeconds) override;
    void ConfigureRocket(float InMaxDamage, float InMinDamage, float InRadius,
        float InSpeed, USoundBase* InImpactSound);
    void ConfigureEffects(UNiagaraSystem* InTrail, UNiagaraSystem* InExplosion);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp,
        bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse,
        const FHitResult& Hit) override;

private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> ProjectileVisual;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> FlightLight;
    UPROPERTY(Transient) TObjectPtr<USoundBase> ImpactSound;
    UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> TrailSystem;
    UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> ExplosionSystem;
    UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> TrailComponent;
    float MinimumExplosionDamage = 60.0f;
    float TrailElapsed = 0.0f;
};

UCLASS()
class HELIOSOUTPOST_API AHeliosRocketWeapon : public AHeliosCombatWeapon
{
    GENERATED_BODY()
public:
    AHeliosRocketWeapon();
    virtual void ApplyDefinition(const UHeliosWeaponDefinition* Definition) override;
protected:
    virtual void FireProjectile(const FVector& TargetLocation) override;
};

UCLASS()
class HELIOSOUTPOST_API AHeliosLaserWeapon : public AHeliosCombatWeapon
{
    GENERATED_BODY()

public:
    AHeliosLaserWeapon();
    virtual void Tick(float DeltaSeconds) override;
    virtual void ApplyDefinition(const UHeliosWeaponDefinition* Definition) override;
    virtual void StartFiring() override;
    virtual void StopFiring() override;
    virtual void DeactivateWeapon() override;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void LaserPulse();
    void UpdateBeam(const FVector& Start, const FVector& End, bool bVisible, bool bHasHit = false);
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BeamVisual;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BeamHalo;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> ImpactLight;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> LoopAudio;
    UPROPERTY(Transient) TObjectPtr<USoundBase> LoopSound;
    FTimerHandle LaserPulseTimer;
    float LaserDamage = 8.0f;
    float LaserRange = 6000.0f;
    float LaserPulseSeconds = 0.1f;
    int32 LaserEnergyPerPulse = 2;
};

UCLASS()
class HELIOSOUTPOST_API AHeliosShotgunWeapon : public AHeliosCombatWeapon
{
    GENERATED_BODY()
    friend class FHeliosDamageActorsTest;

public:
    AHeliosShotgunWeapon();
    virtual void ApplyDefinition(const UHeliosWeaponDefinition* Definition) override;

protected:
    virtual void FireProjectile(const FVector& TargetLocation) override;
private:
    float PelletDamage = 14.0f;
    float PelletMinDamage = 3.0f;
    float ShotgunRange = 3000.0f;
    float FalloffStart = 800.0f;
    int32 PelletCount = 10;
    float SpreadHalfAngle = 5.0f;
};

/** Short-lived non-debug explosion presentation. */
UCLASS(NotBlueprintable)
class HELIOSOUTPOST_API AHeliosExplosionEffect : public AActor
{
    GENERATED_BODY()
public:
    AHeliosExplosionEffect();
    void SetEffect(UNiagaraSystem* System, float BlastRadius, const FVector& ImpactNormal);
    virtual void Tick(float DeltaSeconds) override;
private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Sphere;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Light;
    float Age = 0.0f;
};
