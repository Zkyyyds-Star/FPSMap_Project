#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "HeliosRifleVFXComponent.generated.h"
UCLASS()
class HELIOSOUTPOST_API UHeliosRifleVFXComponent : public USceneComponent
{
    GENERATED_BODY()
public:
    UHeliosRifleVFXComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Function) override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    void Shot(FVector Start,FVector End,FVector Normal,bool bHit);
    void Reload();
    void Clear();
    int32 GetLiveCount() const {return Particles.Num();}
private:
    enum class EShape:uint8 {Spark,Ribbon,Glow,Tracer,Shell};
    struct FParticle {FVector Position=FVector::ZeroVector,End=FVector::ZeroVector,Velocity=FVector::ZeroVector;float Age=0,Life=.12f,Size=2;EShape Shape=EShape::Spark;bool Fresh=true;FLinearColor Color=FLinearColor::White;};
    void Add(const FParticle& P);
    void Sound(class USoundBase* Cue,FVector Position,float Volume);
    TArray<FParticle> Particles;
    UPROPERTY() TObjectPtr<class UProceduralMeshComponent> Surface;
    UPROPERTY() TObjectPtr<class USoundBase> FireSound;
    UPROPERTY() TObjectPtr<class USoundBase> ReloadSound;
    UPROPERTY() TObjectPtr<class UAudioComponent> ReloadAudio;
};
