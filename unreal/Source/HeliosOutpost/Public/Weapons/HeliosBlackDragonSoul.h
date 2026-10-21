#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Weapons/HeliosCombatWeapons.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "HeliosBlackDragonSoul.generated.h"

class UAnimSequence;
class UAudioComponent;
class AHeliosSSXEffect;
class UHeliosSSXEffectLibrary;

USTRUCT(BlueprintType)
struct FHeliosDragonActionSound
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere) FName Action;
    UPROPERTY(EditAnywhere) float Seconds = 0;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<USoundBase> Sound;
};

/** Lives on the holder, so replacing the weapon actor cannot refill/cancel its chamber. */
UCLASS()
class HELIOSOUTPOST_API UHeliosBlackDragonSoulState : public UActorComponent
{
    GENERATED_BODY()
public:
    UHeliosBlackDragonSoulState();
    static UHeliosBlackDragonSoulState* Get(AActor* Holder, bool Create = true);
    virtual void TickComponent(float Dt, ELevelTick Type, FActorComponentTickFunction* Tick) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void Refresh();
    void Reset();
    void TrackWorldActor(AActor* Actor);
    void StartReload(float Seconds, float Delay = 0);
    float Remaining() const;
    int32 Ammo = 1;
    double ReloadStart = -1, ReloadEnd = -1;
    int32 Explosions = 0, EnemyHits = 0, Knockbacks = 0;
    FVector LastExplosion = FVector::ZeroVector;
    TArray<TWeakObjectPtr<AActor>> WorldActors;
};

UCLASS()
class HELIOSOUTPOST_API UHeliosBlackDragonSoulDefinition : public UHeliosWeaponDefinition
{
    GENERATED_BODY()
public:
    UHeliosBlackDragonSoulDefinition();
    UPROPERTY(EditAnywhere) TMap<FName,TSoftObjectPtr<UAnimSequence>> Actions;
    UPROPERTY(EditAnywhere) TArray<FHeliosDragonActionSound> ActionSounds;
    UPROPERTY(EditAnywhere) float KnockbackSpeed = 500;
    UPROPERTY(EditAnywhere) FString FireEffectName;
    UPROPERTY(EditAnywhere) float ExplosionInnerRadius = 150;
    UPROPERTY(EditAnywhere) float ExplosionOuterRadius = 800;
    UPROPERTY(EditAnywhere) FString ProjectileEffectName = TEXT("FX_HLH_Projectile1P");
    UPROPERTY(EditAnywhere) FString ExplosionEffectName = TEXT("FX_HLH_Explosion");
};

/** Owns a shot after the weapon actor is replaced; swept movement cannot tunnel. */
UCLASS()
class HELIOSOUTPOST_API AHeliosBlackDragonSoulProjectile : public AActor
{
    GENERATED_BODY()
public:
    AHeliosBlackDragonSoulProjectile();
    void InitializeShot(const UHeliosBlackDragonSoulDefinition* Definition, const FVector& Direction, AActor* SourceWeapon);
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void Explode(const FVector& Center, const FVector& Normal);
    float Travelled = 0;
    bool bExploded = false;
    UPROPERTY(Transient) TObjectPtr<AHeliosSSXEffect> FlightEffect;
private:
    float Speed = 6000, Range = 3000, Damage = 1200, MinimumDamage = 600;
    float Inner = 150, Outer = 800, KnockbackSpeed = 500;
    FVector Heading = FVector::ForwardVector;
    FString ProjectileEffect, ExplosionEffect;
    TWeakObjectPtr<AActor> Source;
    double LastMovementTime = 0;
};

UCLASS()
class HELIOSOUTPOST_API AHeliosBlackDragonSoulWeapon : public AHeliosCombatWeapon
{
    GENERATED_BODY()
public:
    AHeliosBlackDragonSoulWeapon();
    virtual void ApplyDefinition(const UHeliosWeaponDefinition* Definition) override;
    virtual void ActivateWeapon(const FName& Tag) override;
    virtual void DeactivateWeapon() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void StartFiring() override;
    virtual void Tick(float Dt) override;
    virtual void BeginReload(float Seconds) override;
    virtual void ReloadMagazine() override;
    virtual void CancelReload() override;
    float ReloadRemaining() const;
    FVector MuzzleLocation() const;
    int32 ShotsFired = 0;
    UPROPERTY(Transient) TObjectPtr<AHeliosBlackDragonSoulProjectile> LastProjectile;
protected:
    virtual void FireProjectile(const FVector& Target) override;
private:
    void BeginAction(FName Clip, float Age = 0);
    void Pose();
    void StopSounds();
    UPROPERTY(Transient) TObjectPtr<UHeliosBlackDragonSoulState> Chamber;
    UPROPERTY(Transient) TMap<FName,TObjectPtr<UAnimSequence>> Clips;
    UPROPERTY(Transient) TObjectPtr<UHeliosSSXEffectLibrary> EffectLibrary;
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> Sounds;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosSSXEffect>> Effects;
    FName Action;
    float ActionAge = 0, IdleAge = 0;
    TSet<int32> PlayedEvents;
    bool bActive = false;
};
