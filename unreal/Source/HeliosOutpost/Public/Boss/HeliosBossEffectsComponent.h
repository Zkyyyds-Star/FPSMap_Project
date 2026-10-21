#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Camera/CameraShakeBase.h"
#include "Boss/HeliosBossAttackTypes.h"
#include "HeliosBossEffectsComponent.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UAudioComponent;
class USoundBase;
class USoundAttenuation;

UCLASS()
class HELIOSOUTPOST_API UHeliosBossImpactPattern : public UCameraShakePattern
{
    GENERATED_BODY()
    virtual void GetShakePatternInfoImpl(FCameraShakeInfo& Info) const override;
    virtual void StartShakePatternImpl(const FCameraShakePatternStartParams& Params) override;
    virtual void UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& Result) override;
    virtual bool IsFinishedImpl() const override { return Elapsed >= .24f; }
    float Elapsed = 0.f;
};

UCLASS()
class HELIOSOUTPOST_API UHeliosBossImpactShake : public UCameraShakeBase
{
    GENERATED_BODY()
public:
    UHeliosBossImpactShake();
};

USTRUCT()
struct FHeliosBossDust
{
    GENERATED_BODY()
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Mesh;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Material;
    FVector Velocity = FVector::ZeroVector;
    float Age = 0.f;
    float Lifetime = 0.f;
    float Size = 1.f;
};

/** Visual-only ability presentation. Gameplay owns all damage and cast timing. */
UCLASS(ClassGroup=(Helios), meta=(BlueprintSpawnableComponent))
class HELIOSOUTPOST_API UHeliosBossEffectsComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UHeliosBossEffectsComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    void BeginWindup(EHeliosBossAttackType Type, float Duration);
    void ReleaseAttack(EHeliosBossAttackType Type, FVector Origin, FVector Direction);
    void Impact(FVector Location, FVector Normal, bool bPlayerHit);
    void WaveHit(FVector Location);
    void StopAll();
    void EndDrive();
private:
    UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> WindupFX;
    UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> TrailFX;
    UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> ImpactFX;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> LiveFX;
    float TrailClock = 0.f;
    void SpawnWhiteGold(UNiagaraSystem* System, FVector Location, FVector Direction, bool bAttach=false);
    UStaticMeshComponent* MakeVisual(UStaticMesh* Mesh, UMaterialInterface* Material);
    void SpawnDust(FVector Position, FVector Direction, int32 Count, float Strength);
    void PlaySpatial(USoundBase* Sound, FVector Position, float Volume);
    void Shake(float Scale, FVector Position);
    FVector GroundPoint() const;
    UPROPERTY(Transient) TObjectPtr<UStaticMesh> RingMesh;
    UPROPERTY(Transient) TObjectPtr<UStaticMesh> ArcMesh;
    UPROPERTY(Transient) TObjectPtr<UStaticMesh> DustMesh;
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> AirMaterial;
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> DustMaterial;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Waves;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> WaveMaterials;
    UPROPERTY(Transient) TArray<FHeliosBossDust> Dust;
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> Audio;
    UPROPERTY(Transient) TObjectPtr<USoundBase> WindupSound;
    UPROPERTY(Transient) TObjectPtr<USoundBase> ConeSound;
    UPROPERTY(Transient) TObjectPtr<USoundBase> RadialSound;
    UPROPERTY(Transient) TObjectPtr<USoundBase> ImpactSound;
    UPROPERTY(Transient) TObjectPtr<USoundAttenuation> SpatialAttenuation;
    EHeliosBossAttackType EffectType = EHeliosBossAttackType::None;
    FVector EffectOrigin = FVector::ZeroVector;
    FVector EffectDirection = FVector::ForwardVector;
    float Age = 0.f;
    float WindupDuration = 0.f;
    bool bWindup = false;
    bool bReleased = false;
};
