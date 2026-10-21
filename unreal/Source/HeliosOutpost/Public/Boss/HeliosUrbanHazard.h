#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosUrbanHazard.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UPointLightComponent;

/** New-map skill building block. Visual warning and damage share one ground shape. */
UCLASS()
class HELIOSOUTPOST_API AHeliosUrbanHazard : public AActor
{
    GENERATED_BODY()
public:
    AHeliosUrbanHazard();
    virtual void Tick(float DeltaSeconds) override;
    void Configure(AActor* Caster, bool bBasketball, float Radius, float WarningSeconds, float Damage);
    void SetArrivalMarker() { bArrivalMarker=true; SetLifeSpan(0); }
    bool HasImpacted() const { return bImpacted; }
    float GetSecondsSinceImpact() const { return Age-TelegraphSeconds-(bAirstrike?.65f:0.f); }
    float GetElapsedSeconds() const { return Age; }
    float GetDangerRadius() const { return DangerRadius; }
    bool IsAirstrike() const { return bAirstrike; }
    bool IsDamaging() const { return ImpactDamage>0; }
protected:
    virtual void BeginPlay() override;
private:
    void Impact();
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Warning;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Ball;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Burst;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Fissure;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Trail;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Debris;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Dust;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Sparks;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Shockwave;
    UPROPERTY() TObjectPtr<UPointLightComponent> Flash;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> WarningMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BurstMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FissureMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> DustMaterial;
    float Age=0;
    float DangerRadius=450;
    float TelegraphSeconds=2;
    float ImpactDamage=45;
    bool bAirstrike=true;
    bool bArrivalMarker=false;
    bool bImpacted=false;
};
