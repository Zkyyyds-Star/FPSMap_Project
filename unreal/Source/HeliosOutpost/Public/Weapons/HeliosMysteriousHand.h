#pragma once
#include "CoreMinimal.h"
#include "Weapons/HeliosCombatWeapons.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "HeliosMysteriousHand.generated.h"

class UAnimSequence;
class UMaterialInterface;
class UNiagaraComponent;
class UCanvas;
class UAudioComponent;
class AHeliosEnemyActor;

UCLASS(BlueprintType)
class HELIOSOUTPOST_API UHeliosMysteriousHandDefinition : public UHeliosWeaponDefinition
{
    GENERATED_BODY()
public:
    UHeliosMysteriousHandDefinition();
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimSequence> IdleAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimSequence> PointAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimSequence> BooAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float AimHalfAngle = 8.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Missile|Aim Assist") bool bMissileAimAssist = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Missile|Aim Assist", meta=(Units="deg", ClampMin="0", ClampMax="45", EditCondition="bMissileAimAssist")) float MissileAimHalfAngle = 8.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(Units="cm", ClampMin="1")) float MissileRange = 20000.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float MissileMaxDamage = 200.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float MissileMinDamage = 40.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(Units="cm", ClampMin="1")) float MissileRadius = 1000.f;
    // Serialized for compatibility only. Secondary fire is limited by its gesture, not a cooldown.
    UPROPERTY(EditAnywhere, meta=(DeprecatedProperty, DeprecationMessage="Missiles no longer have a cooldown")) float MissileCooldown = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(Units="s", ClampMin="0.1")) float MissileFlightSeconds = 1.2f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(Units="cm", ClampMin="120")) float SummonHeight = 6000.f;
};

/** Both gestures are single accepted presses. World time commits exactly once, independently of animation ticks. */
UCLASS()
class HELIOSOUTPOST_API AHeliosMysteriousHand : public AHeliosCombatWeapon
{
    GENERATED_BODY()
    friend class FHeliosDamageActorsTest;
    friend class FHeliosMysteriousHandTest;
    friend class FHeliosMysteriousHandAimAssistTest;
    friend class FHeliosMysteriousHandAnimationTest;
public:
    AHeliosMysteriousHand();
    virtual void ApplyDefinition(const UHeliosWeaponDefinition* Definition) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void StartFiring() override;
    virtual void StopFiring() override { bPrimaryDown = false; }
    virtual void StartSecondaryFire() override;
    virtual void StopSecondaryFire() override { bSecondaryDown = false; }
    virtual void ActivateWeapon(const FName& OwnerTag) override;
    virtual void DeactivateWeapon() override;
    virtual void CancelReload() override;
    virtual bool UsesAmmunition() const override { return false; }
    UFUNCTION(BlueprintPure) float GetMissileCooldownRemaining() const;
    UFUNCTION(BlueprintPure) FString GetHandStatus() const;
    void DrawHandHUD(UCanvas* Canvas) const;
    int32 GetLaserCount() const { return LaserCount; }
    int32 GetMissileCount() const { return MissileCount; }
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    bool BeginGesture(uint8 Requested, bool bUseBufferedStrike=false);
    void CancelGesture();
    void UpdateHandPose(bool bRestart=false);
    void CommitLaser();
    bool CommitMissile();
    bool ResolveStrike(FVector& Point, FVector& Normal, FVector& Spawn, AHeliosEnemyActor** SelectedEnemy=nullptr) const;
    bool View(FVector& Start, FVector& Direction) const;
    bool bActive = false, bPrimaryDown = false, bSecondaryDown = false, bCommitted = false;
    uint8 Gesture = 0, Buffered = 0;
    double GestureStart = 0, InvalidUntil = 0;
    float IdleTime = 0;
    double NextPreviewTime = 0;
    TWeakObjectPtr<AHeliosEnemyActor> PreviewEnemy;
    FVector PreviewPosition = FVector::ZeroVector;
    FVector StrikePoint, StrikeNormal, StrikeSpawn;
    FVector BufferedStrikePoint, BufferedStrikeNormal, BufferedStrikeSpawn;
    int32 LaserCount = 0, MissileCount = 0;
    UPROPERTY(Transient) TObjectPtr<UHeliosMysteriousHandDefinition> Settings;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> IdleAnim;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> PointAnim;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> BooAnim;
    UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> Marker;
};

/** A fixed destination, never an actor reference. Survives weapon unequip. */
UCLASS()
class HELIOSOUTPOST_API AHeliosHandMissile : public AActor
{
    GENERATED_BODY()
    friend class FHeliosDamageActorsTest;
    friend class FHeliosMysteriousHandTest;
public:
    AHeliosHandMissile();
    void Launch(const FVector& Destination, const FVector& Normal, const UHeliosMysteriousHandDefinition* Settings);
    virtual void Tick(float DeltaSeconds) override;
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    void Explode(const FVector& Location, const FVector& Normal);
    void EmitWake(const FVector& End);
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
    UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> Trail;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> FallAudio;
    FVector Origin, Target, TargetNormal, LastWakePoint;
    float Age=0, WakeAge=0, Duration=1.2f, MaxDamage=200, MinDamage=40, Radius=1000;
    bool bExploded=false;
};
