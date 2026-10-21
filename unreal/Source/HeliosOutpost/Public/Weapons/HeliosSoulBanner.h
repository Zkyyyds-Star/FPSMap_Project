#pragma once
#include "CoreMinimal.h"
#include "Weapons/HeliosCombatWeapons.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Character.h"
#include "HeliosSoulBanner.generated.h"

class UAnimSequence;
class AHeliosEnemyActor;
class AHeliosArenaCharacter;
class AHeliosSoulFlag;
class AHeliosSoulSkeleton;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class AHeliosSoulEffect;
class UNiagaraComponent;

USTRUCT(BlueprintType)
struct FHeliosSoulAttackPresentation
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FName> MeleeAttackVariants={FName(TEXT("Attack")),FName(TEXT("Attack2"))};
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName MageAttackVariant=TEXT("Attack");
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.05",ClampMax="0.5")) float RibbonLifetimeSeconds=.22f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float TrailStartSeconds=.24f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float TrailEndSeconds=.56f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float TrailWidth=38.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector BladeTip=FVector(0,0,93);
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector BladeBase=FVector(0,0,17);
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector StaffTip=FVector(0,0,84);
};

UCLASS(BlueprintType)
class HELIOSOUTPOST_API UHeliosSoulBannerDefinition : public UHeliosWeaponDefinition
{
    GENERATED_BODY()
public:
    UHeliosSoulBannerDefinition();
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<float> ComboDamage={100,120,160};
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<float> ComboSeconds={.55f,.55f,.75f};
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<TSoftObjectPtr<UAnimSequence>> Swings;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimSequence> PlantAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float PlantSeconds=.9f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float CooldownSeconds=3.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float FlagSeconds=45.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 MaxFlags=10;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float CombatRadius=2000.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FHeliosSoulAttackPresentation AttackPresentation;
};

/** Per-holder lifetime: weapon replacement must not reset summons or cooldown. */
UCLASS()
class HELIOSOUTPOST_API UHeliosSoulArmy : public UActorComponent
{
    GENERATED_BODY()
public:
    UHeliosSoulArmy();
    static UHeliosSoulArmy* Get(AActor* Player,bool Create=true);
    bool FindPlacement(FVector& Ground,TArray<FVector>& Slots) const;
    bool Plant(const UHeliosSoulBannerDefinition* Settings);
    void Clear();
    void TrackTrail(AHeliosSoulEffect* Effect);
    void TrackBurst(UNiagaraComponent* Effect);
    float Cooldown() const;
    int32 FlagCount() const;
    int32 SoldierCount() const;
    FText Status() const;
    TWeakObjectPtr<AHeliosEnemyActor> PreferredTarget;
    TWeakObjectPtr<AHeliosEnemyActor> RecentDamageTarget;
    double RecentDamageTime=-100.;
    void ObservePlayerDamage(AHeliosEnemyActor* Enemy,AController* InstigatorController,AActor* Causer=nullptr);
    UPROPERTY() TArray<TObjectPtr<AHeliosSoulFlag>> Flags;
    float ReadyAt=0;
    float MessageUntil=0;
    FString Message;
    virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Function) override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
private:
    TArray<TWeakObjectPtr<AHeliosSoulEffect>> OwnedTrails;
    TArray<TWeakObjectPtr<UNiagaraComponent>> OwnedBursts;
};

UCLASS()
class HELIOSOUTPOST_API AHeliosSoulBanner : public AHeliosCombatWeapon
{
    GENERATED_BODY()
    friend class FHeliosDamageActorsTest;
public:
    AHeliosSoulBanner();
    virtual void ApplyDefinition(const UHeliosWeaponDefinition* Definition) override;
    virtual void StartFiring() override;
    virtual void StopFiring() override { bHeld=false; }
    virtual void StartSecondaryFire() override;
    virtual void StopSecondaryFire() override {}
    virtual bool UsesAmmunition() const override { return false; }
    virtual void BeginReload(float) override {}
    virtual void CancelReload() override;
    virtual void DeactivateWeapon() override;
    virtual void Tick(float Dt) override;
    UPROPERTY() TObjectPtr<UHeliosSoulBannerDefinition> Settings;
    int32 State=0,Combo=0,AttackSerial=0;
private:
    void Swing();
    void Sweep(float From,float To);
    bool CanAct() const;
    UPROPERTY() TArray<TObjectPtr<UAnimSequence>> Animations;
    UPROPERTY() TObjectPtr<UAnimSequence> PlantPose;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> GhostMaterials;
    UPROPERTY() TObjectPtr<AHeliosSoulEffect> ActiveTrail;
    TSet<TWeakObjectPtr<AActor>> HitActors;
    float Age=0,IdleAge=10,TrailAge=0;
    bool bHeld=false,bGhost=false,bArtMaterialsReady=false;
    float ClothImpulse=0;
};

UCLASS()
class HELIOSOUTPOST_API AHeliosSoulFlag : public AActor
{
    GENERATED_BODY()
public:
    AHeliosSoulFlag();
    void Initialize(UHeliosSoulArmy* Army,const UHeliosSoulBannerDefinition* Settings,const TArray<FVector>& Slots);
    virtual void Tick(float Dt) override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    void Dismiss();
    UPROPERTY() TObjectPtr<UHeliosSoulArmy> Army;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Banner;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Circle;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Aura;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> AuraMaterial;
    FHeliosSoulAttackPresentation AttackPresentation;
    UPROPERTY() TArray<TObjectPtr<AHeliosSoulSkeleton>> Soldiers;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
    float Remaining=45,Radius=2000,Age=0;
};

UCLASS()
class HELIOSOUTPOST_API AHeliosSoulSkeleton : public ACharacter
{
    GENERATED_BODY()
    friend class FHeliosDamageActorsTest;
public:
    AHeliosSoulSkeleton();
    void Initialize(AHeliosSoulFlag* Flag,int32 Role);
    virtual void Tick(float Dt) override;
    virtual float TakeDamage(float Amount,const FDamageEvent& Event,AController* InstigatorController,AActor* Causer) override;
    void Die();
    bool Alive() const { return Health>0 && !IsActorBeingDestroyed(); }
    UPROPERTY() TObjectPtr<AHeliosSoulFlag> Flag;
    UPROPERTY() TMap<FName,TObjectPtr<UAnimSequence>> Animations;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Blade;
    TWeakObjectPtr<AHeliosEnemyActor> Target;
    float Health=150,Age=0,Decision=0,Cooldown=0,AttackAge=-1,AttackInterval=1.2f;
    int32 Role=0,HitsDelivered=0;
private:
    void Pose(FName Name,bool Loop=true);
    void UpdateAttackVisuals();
    void ClearAttackVisuals();
    UPROPERTY() TObjectPtr<AHeliosSoulEffect> AttackTrail;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Charge;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ChargeMaterial;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> SoulMaterials;
    int32 AttackVariant=0;
    int32 VisualLOD=0;
    bool Visible(AActor* Other) const;
    friend class AHeliosSoulBannerQA;
    void Decide();
    void TrackMeleeTarget(float Dt);
    bool CanMeleeHit(AHeliosEnemyActor* Enemy) const;
    void CancelMeleeAttack();
    bool bMoveRequested=false;
    FVector LastGoal=FVector::ZeroVector;
    void Hit();
    FName CurrentPose;
    bool bCommitted=false;
};

/** One bounded, time-fading ribbon per swing; bursts are pooled Niagara systems. */
UCLASS()
class HELIOSOUTPOST_API AHeliosSoulEffect : public AActor
{
    GENERATED_BODY()
public:
    AHeliosSoulEffect();
    static UNiagaraComponent* Burst(AActor* Source,const FVector& Position,int32 Kind,float Scale=1,const FVector& Direction=FVector::UpVector,float Strength=1);
    static AHeliosSoulEffect* Trail(AActor* Owner,float Width=20);
    static void Strike(AActor* Owner,const FVector& From,const FVector& To,bool Vertical=false);
    void Sample(const FVector& Tip,const FVector& Inward);
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    float Age=0,Width=20,RibbonLifetime=.22f;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Ribbon;
private:
    bool bStrike=false;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> StrikeMaterial;
    struct FTrailPoint { FVector Tip,Inward; float Time; };
    TArray<FTrailPoint> Points;
};

UCLASS()
class HELIOSOUTPOST_API AHeliosSoulBolt : public AActor
{
    GENERATED_BODY()
public:
    AHeliosSoulBolt();
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    FVector Velocity;
    TWeakObjectPtr<AHeliosSoulSkeleton> Caster;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Visual;
    UPROPERTY() TObjectPtr<AHeliosSoulEffect> Wake;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Helix;
    float Age=0;
};
