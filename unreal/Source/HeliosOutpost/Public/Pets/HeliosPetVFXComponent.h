#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "HeliosPetVFXComponent.generated.h"
class UProceduralMeshComponent;
class UTextRenderComponent;
class USoundBase;
class UNiagaraComponent;
class UNiagaraSystem;
UCLASS(ClassGroup=(Helios),meta=(BlueprintSpawnableComponent))
class HELIOSOUTPOST_API UHeliosPetVFXComponent : public USceneComponent
{
    GENERATED_BODY()
public:
    UHeliosPetVFXComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* Function) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void BeginSwing(float Reach,bool bDiagonal);
    void Hit(const FVector& Position);
    void Block(const FVector& Position);
    void SetSleeping(bool Sleeping);
    void Clear();
    int32 GetHitBursts() const { return HitBursts; }
    int32 GetLiveParticleCount() const { return Sparks.Num(); }
    bool HasSleepingLetters() const { return bSleeping; }
private:
    struct FSpark { FVector Position,Velocity;float Age=0,Life=.3f,Size=12;FLinearColor Color;bool Ring=false; };
    UFUNCTION() void OnPooledEffectFinished(UNiagaraComponent* Component);
    void PlayNearby(USoundBase* Sound,const FVector& Position,float Volume);
    void Burst(const FVector& Position,bool Shield);
    void UpdateArc();
    void UpdateSparks();
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> ArcMesh;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> SparkMesh;
    UPROPERTY() TArray<TObjectPtr<UTextRenderComponent>> Letters;
    UPROPERTY() TObjectPtr<USoundBase> SwingSound;
    UPROPERTY() TObjectPtr<USoundBase> HitSound;
    UPROPERTY() TObjectPtr<USoundBase> BlockSound;
    UPROPERTY() TObjectPtr<UNiagaraSystem> ImpactSystem;
    UPROPERTY() TArray<TObjectPtr<UNiagaraComponent>> PooledEffects;
    TArray<FSpark> Sparks;
    float SwingAge=-1,Reach=500,SleepAge=0;
    FVector SwingOrigin; FRotator SwingRotation;
    bool bDiagonal=false,bSleeping=false;
    int32 HitBursts=0;
};
