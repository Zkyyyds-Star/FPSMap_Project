#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Camera/CameraShakeBase.h"
#include "HeliosHandImpact.generated.h"

class UPointLightComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UDecalComponent;
class UNiagaraComponent;

/** Presentation only: damage belongs to the missile and is resolved exactly once. */
UCLASS()
class HELIOSOUTPOST_API AHeliosHandImpact : public AActor
{
    GENERATED_BODY()
public:
    AHeliosHandImpact();
    void Initialize(const FVector& SurfaceNormal, float BlastRadius);
    virtual void Tick(float DeltaSeconds) override;
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    float Age=0, Radius=1000;
    UPROPERTY() TObjectPtr<UPointLightComponent> Flash;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Wave;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Heat;
    UPROPERTY() TObjectPtr<UDecalComponent> Scorch;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> WaveMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> HeatMaterial;
    UPROPERTY() TArray<TObjectPtr<UNiagaraComponent>> Effects;
};

UCLASS()
class HELIOSOUTPOST_API UHeliosHandImpactPattern : public UCameraShakePattern
{
    GENERATED_BODY()
protected:
    virtual void GetShakePatternInfoImpl(FCameraShakeInfo& Info) const override;
    virtual void StartShakePatternImpl(const FCameraShakePatternStartParams& Params) override { Age=0; }
    virtual void UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params,FCameraShakePatternUpdateResult& Result) override;
    virtual bool IsFinishedImpl() const override { return Age>=.22f; }
    virtual void StopShakePatternImpl(const FCameraShakePatternStopParams& Params) override { Age=.22f; }
private:
    float Age=0;
};

UCLASS()
class HELIOSOUTPOST_API UHeliosHandImpactShake : public UCameraShakeBase
{
    GENERATED_BODY()
public:
    UHeliosHandImpactShake(const FObjectInitializer& ObjectInitializer);
};
