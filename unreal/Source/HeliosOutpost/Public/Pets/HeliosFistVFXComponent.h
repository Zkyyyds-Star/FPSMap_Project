#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Camera/CameraModifier.h"
#include "HeliosFistVFXComponent.generated.h"
UCLASS()
class HELIOSOUTPOST_API UHeliosFistCameraModifier : public UCameraModifier
{
    GENERATED_BODY()
public:
    float Remaining=0,Strength=0;
    virtual bool ModifyCamera(float Dt,FMinimalViewInfo& View) override;
};
UCLASS()
class HELIOSOUTPOST_API UHeliosFistVFXComponent : public USceneComponent
{
    GENERATED_BODY()
public:
    UHeliosFistVFXComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Function) override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    void BeginCombo();
    void SetAttackTime(float Time) { AttackTime=Time; }
    void Charge(FVector Left,FVector Right,float Age,float Dt);
    void Trail(FVector Start,FVector End,bool Heavy);
    void Swing(int32 PunchIndex,FVector Position,FVector Direction);
    void Impact(int32 PunchIndex,FVector Fist,FVector Position,FVector Direction);
    void Clear();
    int32 Hits=0;
    int32 GetLiveCount() const { return Particles.Num(); }
private:
    enum class EShape : uint8 { Spark, Ribbon, Glow };
    struct FParticle
    {
        FVector Position=FVector::ZeroVector,End=FVector::ZeroVector,Velocity=FVector::ZeroVector,Normal=FVector::ForwardVector;
        float Age=0,Life=.18f,Size=8;
        EShape Shape=EShape::Spark;
        bool Fresh=true;
        FLinearColor Color=FLinearColor::White;
    };
    void Add(const FParticle& Particle);

    void Sound(class USoundBase* Cue,FVector Position,float Volume);
    TArray<FParticle> Particles;
    float AttackTime=-1.f;
    FVector LastFist[2];
    bool bHasLastFist=false;
    float ChargeStep=0;

    bool bChargeSound=false;
    TWeakObjectPtr<UHeliosFistCameraModifier> CameraKick;
    UPROPERTY() TObjectPtr<class UProceduralMeshComponent> Surface;
    UPROPERTY() TObjectPtr<class USoundBase> Whoosh;
    UPROPERTY() TObjectPtr<class USoundBase> Punch;
    UPROPERTY() TObjectPtr<class USoundBase> HeavyPunch;
    UPROPERTY() TObjectPtr<class USoundBase> ChargeSound;

};
