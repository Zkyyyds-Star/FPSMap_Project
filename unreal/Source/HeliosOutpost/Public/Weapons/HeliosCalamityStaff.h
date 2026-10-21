#pragma once

#include "CoreMinimal.h"
#include "Weapons/HeliosCombatWeapons.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "HeliosCalamityStaff.generated.h"

class AHeliosEnemyActor;
class AHeliosCalamityEffect;
class AHeliosCalamityTower;
class UMaterialInstanceDynamic;
class UAnimSequence;
class UPointLightComponent;
class UAudioComponent;
class UInstancedStaticMeshComponent;
class UNiagaraComponent;
class USoundBase;
class UHeliosCalamityState;

UENUM(BlueprintType)
enum class EHeliosCalamitySpell : uint8 { Earthquake, Fireball, Tornado, Poison };

UENUM(BlueprintType)
enum class EHeliosCalamityPhase : uint8 { Flight, Telegraph, Active, Dissipating };

USTRUCT(BlueprintType)
struct FHeliosCalamitySpellSettings
{
    GENERATED_BODY()
    // Retained for existing fire damage falloff and tornado pull tuning.
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float Radius=600;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float Damage=60;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float Duration=4;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float MinimumDamage=0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.1",ClampMax="4.0")) float FlightSpeedMultiplier=1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float TelegraphSeconds=0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float DissipateSeconds=.5f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USoundBase> ChargeSound;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USoundBase> ReleaseSound;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USoundBase> PreludeSound;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USoundBase> ImpactSound;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USoundBase> LoopSound;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USoundBase> TailSound;
};

/** Presentation-only profile. Resource keys are the authored Impact asset names. */
UCLASS(BlueprintType)
class HELIOSOUTPOST_API UHeliosCalamityImpactDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    UHeliosCalamityImpactDefinition();
    // Additive defaults keep existing profiles compatible and make the range materials cookable.
    UPROPERTY() TMap<FName,TObjectPtr<UObject>> RangeResources;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float FireVisualSeconds=3.2f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float FireRiseSeconds=.45f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float FireHeight=400;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float PoisonCrownHeight=150;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float PoisonSpreadSeconds=.5f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float PoisonFadeSeconds=.8f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float EmissiveGain=1;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float ParticleScale=1;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) TMap<FName,TObjectPtr<UObject>> Resources;
    void Prewarm();
};

UCLASS(BlueprintType)
class HELIOSOUTPOST_API UHeliosCalamityDefinition : public UHeliosWeaponDefinition
{
    GENERATED_BODY()
public:
    UHeliosCalamityDefinition();
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UHeliosCalamityImpactDefinition> ImpactPresentation;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<class UHeliosCalamityWindDefinition> WindPresentation;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float CastWindupSeconds=.4f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FHeliosCalamitySpellSettings> Spells;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float TowerSeconds=20;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float TowerCooldown=10;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float TowerRange=3500;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float TowerInterval=1.6f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float TowerDamageScale=.7f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float PlacementRange=1200;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimSequence> DeployPose;
};

/** Attached, bounded ion geometry and pooled sparks shared by the held staff and tower. */
UCLASS()
class HELIOSOUTPOST_API UHeliosCalamityChannel : public USceneComponent
{
    GENERATED_BODY()
public:
    void Initialize(UHeliosCalamityState* InState,USceneComponent* Parent,FVector Grip,FVector Tip);
    void Start(EHeliosCalamitySpell InSpell);
    void Advance(float Dt);
    void Release();
    void Cancel();
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    bool IsReady() const;
    bool bCharging=false;
    float Age=100,Power=0;
    FVector GripPoint,TipPoint;
    EHeliosCalamitySpell Spell=EHeliosCalamitySpell::Earthquake;
    UPROPERTY() TObjectPtr<UHeliosCalamityState> State;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Ions;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Streaks;
    UPROPERTY() TObjectPtr<UPointLightComponent> Light;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Material;
    UPROPERTY() TObjectPtr<UNiagaraComponent> Sparks;
    UPROPERTY() TObjectPtr<UAudioComponent> ChargeAudio;
};

/** Owns summons and cooldowns across the project's destroy-on-switch weapon lifecycle. */
UCLASS()
class HELIOSOUTPOST_API UHeliosCalamityState : public UActorComponent
{
    GENERATED_BODY()
public:
    UHeliosCalamityState();
    static constexpr float SpellSoundGain=1.5f;
    // Shared gameplay/presentation radius, independent of serialized falloff and pull tuning.
    static constexpr float GetDamageRadius() { return 1000.f; }
    static UHeliosCalamityState* Get(AActor* Player,bool Create=true);
    bool Deploy();
    bool FindPlacement(FVector& Point) const;
    bool CastSpell(EHeliosCalamitySpell Spell,const FVector& Origin,const FVector& Target,float Scale=1);
    void Clear();
    bool Deployed() const;
    float Cooldown() const;
    FText Status() const;
    bool CanHit(const FVector& From,AHeliosEnemyActor* Enemy) const;
    void DamageArea(const FVector& Center,EHeliosCalamitySpell Spell,float Scale,float Time=1);
    UAudioComponent* Sound(USoundBase* Asset,USceneComponent* Attach,const FVector& Location,float Volume=.7f);
    void Shake(const FVector& Center,float Strength);
    virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Function) override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    UPROPERTY() TObjectPtr<UHeliosCalamityDefinition> Settings;
    UPROPERTY() TObjectPtr<AHeliosCalamityTower> Tower;
    UPROPERTY() TArray<TObjectPtr<AHeliosCalamityEffect>> Effects;
    UPROPERTY() TArray<TObjectPtr<UAudioComponent>> PlayingSounds;
    EHeliosCalamitySpell LastSpell=EHeliosCalamitySpell::Earthquake;
    float ReadyAt=0,CastReadyAt=0,MessageUntil=0;
    FString Message;
    int32 CastCount=0,HitCount=0,TowerShots=0;
private:
    float PoisonAccumulator=0;
    float ShakePower=0;
    bool bAppliedShake=false;
};

UCLASS()
class HELIOSOUTPOST_API AHeliosCalamityStaff : public AHeliosCombatWeapon
{
    GENERATED_BODY()
public:
    AHeliosCalamityStaff();
    virtual void ApplyDefinition(const UHeliosWeaponDefinition* Definition) override;
    virtual void StartFiring() override;
    virtual void StopFiring() override { bHeld=false; }
    virtual void StartSecondaryFire() override;
    virtual bool UsesAmmunition() const override { return false; }
    virtual void DeactivateWeapon() override;
    virtual void Tick(float Dt) override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    UPROPERTY() TObjectPtr<UHeliosCalamityChannel> Channel;
    UPROPERTY() TObjectPtr<UHeliosCalamityDefinition> Settings;
    int32 ForcedSpell=-1; // Used only by the opt-in, in-engine demonstration.
private:
    void CastOnce();
    void ReleaseSpell();
    bool bHeld=false,bWasDeployed=false;
    float AnimationAge=100;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> GlowMaterials;
};

/** A shared projectile / persistent field, with bounded meshes and pooled Niagara bursts. */
UCLASS()
class HELIOSOUTPOST_API AHeliosCalamityEffect : public AActor
{
    GENERATED_BODY()
public:
    AHeliosCalamityEffect();
    void Initialize(UHeliosCalamityState* State,EHeliosCalamitySpell Type,const FVector& Target,float Scale);
    virtual void Tick(float Dt) override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    UPROPERTY() TObjectPtr<UHeliosCalamityState> State;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
    UPROPERTY() TObjectPtr<UPointLightComponent> Light;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Debris;
    UPROPERTY() TObjectPtr<UAudioComponent> LoopAudio;
    UPROPERTY() TObjectPtr<class UHeliosCalamityWindVisual> WindVisual;
    EHeliosCalamityPhase Phase=EHeliosCalamityPhase::Flight;
    EHeliosCalamitySpell Spell=EHeliosCalamitySpell::Earthquake;
    FVector Velocity=FVector::ZeroVector;
    float FlightSpeed=1;
    float Age=0,FieldAge=0,Scale=1,Radius=600,Duration=4;
    bool bField=false;
    float Telegraph=0,Tail=.5f,LoopGain=0;
    void SetLoopAudible(bool Audible,float Dt);
    int32 Pulses=0;
    FVector ImpactNormal=FVector::UpVector;
    bool bImpactGround=true;
    void Impact(const FVector& Point,const FVector& Normal=FVector::UpVector,const FVector& Incoming=FVector::ForwardVector,bool Ground=true);
private:
    void BuildImpactPresentation();
    void TickImpactPresentation(float Dt);
    void PlayImpactLayers();
    UPROPERTY() TObjectPtr<class UDecalComponent> ImpactDecal;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> DecalMaterial;
    UPROPERTY() TArray<TObjectPtr<UNiagaraComponent>> ImpactParticles;
    float ImpactFXAt=0;
    void BuildField();
    void ActivateField();
    void StartLoop();
    void BuildFlightVisuals();
    void UpdateFlightVisuals(float Dt);
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> FlightTrail;
    UPROPERTY() TObjectPtr<UNiagaraComponent> FlightParticles;
    TArray<FVector> TrailPoints;
    float PulseAt=0,FXAt=0;
    bool bTailPlayed=false;
};

UCLASS()
class HELIOSOUTPOST_API AHeliosCalamityTower : public AActor
{
    GENERATED_BODY()
public:
    AHeliosCalamityTower();
    void Initialize(UHeliosCalamityState* State);
    virtual void Tick(float Dt) override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    UPROPERTY() TObjectPtr<UHeliosCalamityChannel> Channel;
    UPROPERTY() TWeakObjectPtr<AHeliosEnemyActor> PendingTarget;
    UPROPERTY() TObjectPtr<UHeliosCalamityState> State;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
    UPROPERTY() TObjectPtr<UPointLightComponent> Light;
    float Age=0,NextShot=1;
    FVector LaunchPoint=FVector::ZeroVector;
private:
    FVector Ground=FVector::ZeroVector;
};
