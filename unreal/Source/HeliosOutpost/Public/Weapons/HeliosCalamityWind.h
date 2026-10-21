#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Components/SceneComponent.h"
#include "HeliosCalamityWind.generated.h"

class AHeliosCalamityEffect;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UNiagaraComponent;

/** Only artwork and presentation tuning. Gameplay remains in the calamity field. */
UCLASS(BlueprintType)
class HELIOSOUTPOST_API UHeliosCalamityWindDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TMap<FName,TObjectPtr<UObject>> Resources;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="100")) float Height=720;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0",ClampMax="1")) float Density=.72f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0",ClampMax="3")) float EmissiveGain=1.25f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FLinearColor Tint=FLinearColor(.2f,.52f,.85f);
    bool IsReady() const;
    void Prewarm();
};

/** Fixed component/instance budget; advanced by its owning field's gameplay clock. */
UCLASS()
class HELIOSOUTPOST_API UHeliosCalamityWindVisual : public USceneComponent
{
    GENERATED_BODY()
public:
    void Initialize(AHeliosCalamityEffect* InField,UHeliosCalamityWindDefinition* InProfile);
    void PlayActivation();
    void Advance(float Formation,float Fade,float ActiveAge);
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    float GetGroundRadius() const;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Ground;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Blades;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Debris;
private:
    UStaticMeshComponent* Layer(const TCHAR* Mesh,const TCHAR* Material,int32 Sort=0);
    UPROPERTY() TObjectPtr<AHeliosCalamityEffect> Field;
    UPROPERTY() TObjectPtr<UHeliosCalamityWindDefinition> Profile;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Layers;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Dust;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> InnerBody;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Cuts;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Wave;
    UPROPERTY() TObjectPtr<UNiagaraComponent> Motes;
    bool bStoppedMotes=false;
    float PhaseOffset=0;
};
