#pragma once

#include "CoreMinimal.h"
#include "Weapons/HeliosCombatWeapons.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "HeliosColaWeapon.generated.h"

class USphereComponent;
class UAudioComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

/** Cosmetic only; the same variant follows a can from the hand into the world. */
USTRUCT(BlueprintType)
struct FHeliosDrinkVariant
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Id;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UMaterialInterface> LabelMaterial;
};

/** Editable gameplay settings. Presentation never decides when ammo or damage commits. */
UCLASS(BlueprintType)
class HELIOSOUTPOST_API UHeliosColaWeaponDefinition : public UHeliosWeaponDefinition
{
    GENERATED_BODY()
public:
    UHeliosColaWeaponDefinition();
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Drinks") TArray<FHeliosDrinkVariant> DrinkVariants;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.01")) float ReleaseSeconds = .35f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.01")) float FuseSeconds = 2.2f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0")) float InnerRadius = 300.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1")) float OuterRadius = 1500.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float UpwardSpeed = 250.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0",ClampMax="1")) float Bounciness = .45f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0",ClampMax="1")) float BounceFriction = .25f;
};

UCLASS()
class HELIOSOUTPOST_API AHeliosColaProjectile : public AShooterProjectile
{
    GENERATED_BODY()
    friend class FHeliosDamageActorsTest;
public:
    AHeliosColaProjectile();
    void Configure(const FVector& Direction, const UHeliosColaWeaponDefinition* Definition, int32 DrinkIndex = 0);
    FName GetDrinkId() const { return DrinkId; }
    UStaticMeshComponent* GetDrinkVisual() const { return Visual; }
    virtual void Tick(float Dt) override;
    float GetFuseAge() const { return Age; }
    int32 GetBounceCount() const { return BounceCount; }
    bool HasExploded() const { return bHit; }
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    virtual void NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp,
        bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;
private:
    void Explode();
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Visual;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UAudioComponent> Hiss;
    UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> Foam;
    UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> Spray;
    UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> FoamAir;
    UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> SprayAir;
    UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> Bubbles;
    UPROPERTY(Transient) TObjectPtr<USoundBase> BounceSound;
    UPROPERTY(Transient) TObjectPtr<USoundBase> ExplosionSound;
    FTimerHandle FuseTimer;
    float Age=0, Fuse=2.2f, Inner=300.f, Outer=1500.f, MinDamage=80.f, BubbleAge=0, LastBounce=-1;
    int32 BounceCount=0;
    FName DrinkId;
    bool bSurfaceImpact=false;
    FVector ImpactSurfaceNormal=FVector::UpVector,ImpactSurfacePoint=FVector::ZeroVector;
};

UCLASS()
class HELIOSOUTPOST_API AHeliosColaWeapon : public AHeliosCombatWeapon
{
    GENERATED_BODY()
public:
    AHeliosColaWeapon();
    virtual void ApplyDefinition(const UHeliosWeaponDefinition* Definition) override;
    virtual void StartFiring() override;
    virtual void StopFiring() override;
    virtual void BeginReload(float Duration) override;
    virtual void ReloadMagazine() override;
    virtual void CancelReload() override;
    virtual void DeactivateWeapon() override;
    virtual void Tick(float Dt) override;
    bool IsThrowPending() const { return bPending; }
    FName GetDrinkId() const;
    int32 GetDrinkDrawCount() const { return DrinkDrawCount; }
    void RefreshDrinkPresentation();
    bool SelectDrinkForQA(int32 Index);
protected:
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
private:
    void Release();
    void CancelThrow();
    void DrawDrink();
    UPROPERTY(Transient) TObjectPtr<UHeliosColaWeaponDefinition> ColaDefinition;
    FTimerHandle ReleaseTimer;
    double LastThrow=-100;
    bool bInputHeld=false, bPending=false;
    float RecoverAge=10;
    int32 CurrentDrink=INDEX_NONE, DrinkDrawCount=0;
    FRandomStream DrinkRandom;
};

/** Four-second cosmetic ground splash, with no ring, collision or damage. */
UCLASS(NotBlueprintable)
class HELIOSOUTPOST_API AHeliosColaSplash : public AActor
{
    GENERATED_BODY()
public:
    AHeliosColaSplash();
    /** Radius of the damage sphere's intersection with the receiving ground plane, in cm. */
    void ConfigureBlastRadius(float Radius) { BlastRadius=FMath::Max(0.f,Radius); }
    float GetBlastRadius() const { return BlastRadius; }
    virtual void Tick(float Dt) override;
protected:
    virtual void BeginPlay() override;
private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Splash;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SplashMaterial;
    float BlastRadius=0;
    float Age=0;
};
