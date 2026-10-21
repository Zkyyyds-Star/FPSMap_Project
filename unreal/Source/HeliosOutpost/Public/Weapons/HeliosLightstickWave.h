#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosLightstickWave.generated.h"
class UHeliosLightstickDefinition;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;

/** Fixed-origin expanding sector. Target collision is independent of visual tessellation. */
UCLASS()
class HELIOSOUTPOST_API AHeliosLightstickWave : public AActor
{
    GENERATED_BODY()
public:
    AHeliosLightstickWave();
    void Initialize(UHeliosLightstickDefinition* Definition,float Charge,const FVector& Eye);
    virtual void Tick(float Dt) override;
    float GetDistanceTravelled() const { return Travelled; }
    bool IsFlying() const { return bFlying; }
private:
    friend class AHeliosLightstickDemo;
    void Burst(const FHitResult& Hit,bool Wall);
    void AdvanceDamage(float PreviousRadius,float NewRadius);
    void UpdateWaveMesh(float Radius,bool Create=false);
    bool FindWall(const FVector& End,FHitResult& Hit,AActor* Target=nullptr,bool BladeHeight=false) const;
    void GatherTargets(TArray<AActor*>& Out) const;
    UPROPERTY() TObjectPtr<UHeliosLightstickDefinition> Settings;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> WaveSurface;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
    TSet<TWeakObjectPtr<AActor>> Damaged;
    TSet<TWeakObjectPtr<AActor>> OccludedTargets;
    TMap<TWeakObjectPtr<AActor>,FVector> PreviousCenters;
    TArray<float> BlockedRadii;
    TArray<int32> ArcTriangles;
    TArray<FVector> ArcVertices,ArcNormals;
    TArray<FVector2D> ArcUV;
    TArray<FLinearColor> ArcColors;
    int32 SegmentCount=128;
    FLinearColor Color;
    float Travelled=0,Damage=0,Age=0,FadeAge=0,ReleasedCharge=0;
    bool bFlying=true;
};
