#pragma once
#include "CoreMinimal.h"
#include "Weapons/HeliosCombatWeapons.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Components/ActorComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HeliosFighterWeapon.generated.h"

class AHeliosArenaCharacter;
class UCanvas;
class UStaticMeshComponent;
class UAudioComponent;
class UPointLightComponent;
class UNiagaraSystem;
class UNiagaraComponent;
class UMaterialInstanceDynamic;

UCLASS(BlueprintType)
class HELIOSOUTPOST_API UHeliosFighterDefinition : public UHeliosWeaponDefinition
{
    GENERATED_BODY()
public:
    UHeliosFighterDefinition();
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flight") float ForwardSpeed=1800;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flight") float BoostSpeed=3200;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flight") float StrafeSpeed=1200;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flight") float VerticalSpeed=1000;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Flight") float Ceiling=8000;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Laser") float HeatSeconds=5;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Laser") float CoolSeconds=2;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Laser") float DissipateSeconds=3;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Missile") float MissileCooldown=2.5f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Missile") float MissileDamage=300;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Missile") float MissileMinimumDamage=60;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Missile") float MissileRadius=500;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Missile") float MissileSpeed=9000;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Missile") float MissileLife=6;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Missile") float TurnDegrees=100;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Missile") float LockRange=22000;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Missile") float LockAngle=8;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Missile") float LockSeconds=.5f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation") float LaserCoreRadius=1.8f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation") float LaserHaloRadius=7;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation") float LaserVisualSeconds=.10f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation") float TrailSpacing=150;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation") float EffectCullDistance=35000;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation") float CockpitFeedback=.35f;
    void LoadConfiguration(const UObject* Context);
};

/** Only the fighter changes PhysFlying; ordinary character movement stays in the engine. */
UCLASS()
class HELIOSOUTPOST_API UHeliosFighterMovement : public UCharacterMovementComponent
{
    GENERATED_BODY()
protected:
    virtual void PhysFlying(float DeltaTime,int32 Iterations) override;
};

UCLASS()
class HELIOSOUTPOST_API UHeliosFighterFlightComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    bool Deploy();
    void Restore();
    void Simulate(float Dt,UCharacterMovementComponent* Movement);
    void ClearInput();
    void SetVerticalKey(bool Ascend,bool Held);
    bool FindDeployment(FVector& OutCenter,float& OutGround) const;
    FVector DesiredVelocity() const;
    FVector ForwardRightUp=FVector::ZeroVector;
    bool bBoost=false,bActive=false,bBoundary=false;
    float GroundZ=0,Altitude=0;
    FBox FlightBounds=FBox(EForceInit::ForceInit);
    FRotator HullRotation=FRotator::ZeroRotator;
    static FVector HullExtent() { return FVector(280,215,115); }
private:
    float SavedRadius=34,SavedHalfHeight=96,SavedFOV=90;
    float SavedPitchMin=-89.9f,SavedPitchMax=89.9f;
    bool bSavedNav=true;
    FVector SavedCamera;
    TEnumAsByte<EMovementMode> SavedMode=MOVE_Walking;
    bool bUpHeld=false,bDownHeld=false;
};

UCLASS()
class HELIOSOUTPOST_API UHeliosFighterPresentation : public UActorComponent
{
    GENERATED_BODY()
public:
    void Initialize();
    void Update(float Dt);
    void Clear();
    void Pulse(const FVector& Start,const FVector& End,const FHitResult& Hit,int32 Side);
    void MissileLaunch(const FVector& At,const FVector& Direction);
    void MissileIgnition(const FVector& At,const FVector& Direction);
    void MissileTrail(const FVector& At,const FVector& Direction);
    void Detonation(const FVector& At,const FVector& Normal);
    bool EffectsAllowed() const;
    int32 ActiveEffectCount() const { return Effects.Num(); }
    int32 PeakEffectCount=0;
    void Sound(const TCHAR* Name,float Volume=1);
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Hull;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Cockpit;
private:
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Beams;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Halos;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Thrusters;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> EngineAudio;
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> AttackAudio;
    UPROPERTY(Transient) TObjectPtr<UPointLightComponent> InstrumentLight;
    UPROPERTY(Transient) TArray<TObjectPtr<UPointLightComponent>> FlashLights;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> BeamMaterials;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> Effects;
    UPROPERTY(Transient) TMap<FName,TObjectPtr<UNiagaraSystem>> EffectSystems;
    UPROPERTY(Transient) TMap<FName,TObjectPtr<USoundBase>> Sounds;
    TArray<float> BeamAges;
    TArray<float> EffectTimes;
    FVector SavedCockpitOffset=FVector::ZeroVector;
    float Feedback=0,Flash=0,BlastFlash=0;
    int32 FlashSide=0;
    void Effect(const TCHAR* Name,const FVector& At,const FVector& Direction,float Life,float Scale=1);
    void SoundAt(const TCHAR* Name,const FVector& At,float Volume,float Pitch=1);
    int32 NextBeam=0;
    float Clock=0,Bank=0;
};

UCLASS()
class HELIOSOUTPOST_API AHeliosFighterMissile : public AActor
{
    GENERATED_BODY()
public:
    AHeliosFighterMissile();
    void Launch(UHeliosFighterDefinition* Definition,AActor* Target,const FVector& Direction);
    virtual void Tick(float Dt) override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    void Explode(const FVector& At,const FVector& Normal);
    bool IsIgnited() const { return Age>=.18f; }
private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Flame;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> FlameCore;
    UPROPERTY(Transient) TObjectPtr<UHeliosFighterDefinition> Settings;
    TWeakObjectPtr<AActor> LockedTarget;
    FVector Direction=FVector::ForwardVector;
    float Age=0;
    FVector LastTrail=FVector::ZeroVector;
    bool bIgnitionPlayed=false;
    bool bExploded=false;
};

UCLASS()
class HELIOSOUTPOST_API AHeliosFighterWeapon : public AHeliosCombatWeapon
{
    GENERATED_BODY()
public:
    AHeliosFighterWeapon();
    virtual void ApplyDefinition(const UHeliosWeaponDefinition* D) override;
    virtual void ActivateWeapon(const FName& Tag) override;
    virtual void DeactivateWeapon() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    virtual void Tick(float Dt) override;
    virtual void StartFiring() override;
    virtual void StopFiring() override { bPrimaryHeld=false; }
    virtual void StartSecondaryFire() override;
    virtual void StopSecondaryFire() override { bSecondaryHeld=false; }
    virtual bool UsesAmmunition() const override { return false; }
    virtual void CancelReload() override { CancelInput(); }
    void CancelInput();
    void SuspendForArrival();
    bool TryResumeAfterArrival();
    void DrawFighterHUD(UCanvas* Canvas);
    FString Status() const;
    AHeliosArenaCharacter* Player() const;
    bool CanAttack() const;
    bool ClearSight(const FVector& From,const FVector& To,const AActor* Target=nullptr) const;
    FVector Muzzle(int32 Side) const;
    void FirePulse();
    void UpdateLock(float Dt);
    void ClearProjectiles();
    UPROPERTY(VisibleAnywhere) TObjectPtr<UHeliosFighterFlightComponent> Flight;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UHeliosFighterPresentation> Art;
    UPROPERTY(Transient) TObjectPtr<UHeliosFighterDefinition> Settings;
    TWeakObjectPtr<AActor> LockCandidate;
    float Heat=0,CoolRemaining=0,MissileRemaining=0,LockProgress=0;
    int32 LaserCount=0,MissileCount=0;
    bool bActive=false;
private:
    float ShotClock=0,DeployAge=0;
    bool bPrimaryHeld=false,bSecondaryHeld=false,bLockSound=false;
};
