#pragma once
#include "CoreMinimal.h"
#include "Weapons/HeliosCombatWeapons.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Components/ActorComponent.h"
#include "Camera/CameraShakeBase.h"
#include "HeliosQingxiao.generated.h"

class AHeliosEnemyActor;
class AHeliosQingxiaoSword;
class UAnimSequence;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class USoundAttenuation;

UCLASS(BlueprintType)
class HELIOSOUTPOST_API UHeliosQingxiaoDefinition : public UHeliosWeaponDefinition
{
    GENERATED_BODY()
public:
    UHeliosQingxiaoDefinition();
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float SlashSeconds=.32f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float SummonSeconds=.45f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float LaunchInterval=.09f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float SearchRadius=10000.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float SwordDamage=150.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float SwordSpeed=7200.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float DissolveSeconds=.35f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float MaxWaveSeconds=4.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation") float LightTrailSeconds=.12f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation") float FlightTrailSeconds=.22f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation") float FlightTrailWidth=11.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation") float LightHitStopSeconds=.03f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation") float LightShakeStrength=.65f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation") float WallBurstScale=1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<TSoftObjectPtr<UAnimSequence>> Slashes;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimSequence> SummonPose;
};

/** Survives weapon replacement; one authoritative five-sword wave per player. */
UCLASS()
class HELIOSOUTPOST_API UHeliosQingxiaoFormation : public UActorComponent
{
    GENERATED_BODY()
public:
    UHeliosQingxiaoFormation();
    static UHeliosQingxiaoFormation* Get(AActor* Player, bool Create=true);
    bool Begin(const UHeliosQingxiaoDefinition* Definition);
    void Clear(bool ClearImpacts=true);
    bool Busy() const { return bActive; }
    int32 Remaining() const;
    FText Status() const;
    bool Eligible(const AHeliosEnemyActor* Enemy) const;
    AHeliosEnemyActor* SelectTarget(const AHeliosQingxiaoSword* Requester) const;
    virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Function) override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    UPROPERTY() TObjectPtr<UHeliosQingxiaoDefinition> Settings;
    UPROPERTY() TArray<TObjectPtr<AHeliosQingxiaoSword>> Swords;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Seal;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SealMaterial;
    FVector Origin=FVector::ZeroVector;
    FVector ViewOrigin=FVector::ZeroVector;
    FRotator ViewRotation=FRotator::ZeroRotator;
    float Age=0;
    int32 WaveSerial=0,SpawnedTotal=0,HitsTotal=0,RendTotal=0,WallBurstsTotal=0;
    bool bCompressed=false;
private:
    bool bActive=false;
};

enum class EQingxiaoBurst : uint8 { LightHit, Wall, Launch };
enum class EQingxiaoSwordEnd : uint8 { Natural, EnemyHit, WorldBlocked };

/** Cosmetic only: surface-oriented ring, sharp impact core and outward sword shards. */
UCLASS()
class HELIOSOUTPOST_API AHeliosQingxiaoBurst : public AActor
{
    GENERATED_BODY()
public:
    AHeliosQingxiaoBurst();
    void Initialize(const FVector& Point,const FVector& Normal,const FVector& CutDirection,EQingxiaoBurst Kind,float Strength=1.f,int32 Variation=0);
    virtual void Tick(float Dt) override;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Mesh;
    TWeakObjectPtr<AActor> Follow;
    float Age=0;
private:
    FVector Right,Up,Facing,Anchor,FollowStart;
    EQingxiaoBurst Style=EQingxiaoBurst::Wall;
    float Scale=1,Opacity=1;
    int32 Seed=0;
};

UCLASS()
class HELIOSOUTPOST_API UHeliosQingxiaoShakePattern : public UCameraShakePattern
{
    GENERATED_BODY()
public:
    float Direction=1;
private:
    virtual void GetShakePatternInfoImpl(FCameraShakeInfo& Info) const override;
    virtual void StartShakePatternImpl(const FCameraShakePatternStartParams& Params) override;
    virtual void UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params,FCameraShakePatternUpdateResult& Result) override;
    virtual bool IsFinishedImpl() const override { return Elapsed>=.12f; }
    float Elapsed=0;
};

UCLASS()
class HELIOSOUTPOST_API UHeliosQingxiaoShake : public UCameraShakeBase
{
    GENERATED_BODY()
public:
    UHeliosQingxiaoShake();
};

/** Short-lived tears follow the struck enemy and never apply additional damage. */
UCLASS()
class HELIOSOUTPOST_API AHeliosQingxiaoImpact : public AActor
{
    GENERATED_BODY()
public:
    AHeliosQingxiaoImpact();
    void Initialize(AHeliosEnemyActor* Enemy,const FVector& Eye,int32 Variation);
    virtual void Tick(float Dt) override;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Mesh;
private:
    TWeakObjectPtr<AHeliosEnemyActor> Victim;
    FVector Offset,Right,Up;
    float Age=0;
    int32 Seed=0;
};

/** Fixed-size cosmetic history, also used for melee blade arcs. */
UCLASS()
class HELIOSOUTPOST_API AHeliosQingxiaoRibbon : public AActor
{
    GENERATED_BODY()
public:
    AHeliosQingxiaoRibbon();
    void Add(const FVector& Inner,const FVector& Outer);
    virtual void Tick(float Dt) override;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Mesh;
    float FadeSeconds=.18f;
    bool bFlight=false;
    float MotionStrength=1.f;
private:
    TArray<FVector> Inners,Outers;
    TArray<float> Times;
    float Clock=0;
};

UCLASS()
class HELIOSOUTPOST_API AHeliosQingxiaoSword : public AActor
{
    GENERATED_BODY()
public:
    AHeliosQingxiaoSword();
    void Initialize(UHeliosQingxiaoFormation* Formation,int32 Slot,const FVector& Position);
    void Dissolve(EQingxiaoSwordEnd Reason=EQingxiaoSwordEnd::Natural);
    void BlockAt(const FHitResult& Hit);
    virtual void Tick(float Dt) override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    bool IsDissolving() const { return DissolveAge>=0; }
    UPROPERTY() TObjectPtr<UHeliosQingxiaoFormation> Formation;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Blade;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Halo;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> AuraMaterial;
    UPROPERTY() TObjectPtr<AHeliosQingxiaoRibbon> Ribbon;
    TWeakObjectPtr<AHeliosEnemyActor> Target;
    int32 Index=0,HitsDelivered=0;
    EQingxiaoSwordEnd EndReason=EQingxiaoSwordEnd::Natural;
private:
    FVector Birth=FVector::ZeroVector,Velocity=FVector::ZeroVector;
    float DissolveAge=-1,FlightAge=0;
    bool bLaunched=false;
};

UCLASS()
class HELIOSOUTPOST_API AHeliosQingxiao : public AHeliosCombatWeapon
{
    GENERATED_BODY()
    friend class FHeliosDamageActorsTest;
public:
    AHeliosQingxiao();
    virtual void ApplyDefinition(const UHeliosWeaponDefinition* Definition) override;
    virtual void StartFiring() override;
    virtual void StopFiring() override { bHeld=false; }
    virtual void StartSecondaryFire() override;
    virtual void StopSecondaryFire() override { bSecondaryDown=false; }
    virtual bool UsesAmmunition() const override { return false; }
    virtual void BeginReload(float) override {}
    virtual void CancelReload() override;
    virtual void DeactivateWeapon() override;
    virtual void Tick(float Dt) override;
    UPROPERTY() TObjectPtr<UHeliosQingxiaoDefinition> Settings;
    int32 SlashSerial=0,State=0;
    int32 LightFeedbackTotal=0;
    float SlashPhase=0;
private:
    bool CanAct() const;
    void Slash();
    void Summon();
    float Sweep(float From,float To);
    UPROPERTY() TArray<TObjectPtr<UAnimSequence>> Slashes;
    UPROPERTY() TObjectPtr<UAnimSequence> SummonAnimation;
    UPROPERTY() TObjectPtr<AHeliosQingxiaoRibbon> Arc;
    TSet<TWeakObjectPtr<AActor>> HitActors;
    float Age=0;
    float HitStopRemaining=0,PhaseLag=0;
    bool bImpactFeedback=false,bSwingSound=false;
    bool bHeld=false,bQueuedSlash=false,bQueuedSummon=false,bSecondaryDown=false;
};
