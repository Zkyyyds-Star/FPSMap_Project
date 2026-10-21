#pragma once
#include "CoreMinimal.h"
#include "Pets/HeliosPetDog.h"
#include "Weapons/HeliosBuLianShiP90.h"
#include "HeliosBuLianShiP90Pet.generated.h"
UCLASS()
class HELIOSOUTPOST_API AHeliosBuLianShiP90Pet : public AHeliosPetCompanion
{
    GENERATED_BODY()
public:
    AHeliosBuLianShiP90Pet();
    virtual FString DisplayName()const override{return TEXT("步练师·灵魂舞者 / P90双脊龙伙伴");}
    virtual void Tick(float Dt)override;
    virtual void EndPlay(const EEndPlayReason::Type Reason)override;
    virtual float TakeDamage(float Amount,const FDamageEvent& Event,AController* InstigatorController,AActor* Causer)override;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UStaticMeshComponent> P90;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UHeliosSSXVoiceComponent> Voice;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UHeliosP90SubstituteFireAudio> FireAudio;
    int32 Ammo=12,ShotsFired=0,Reloads=0,TrueKills=0;
    FHeliosP90Tuning ShotSettings;
    FVector MuzzleLocation()const;
    bool IsReloading()const{return ReloadAge>=0;}
protected:
    virtual void LoadModel()override;
    virtual void SetPose(FName Name,bool Loop=true)override;
    virtual void UpdateLocomotion(float Dt)override;
    virtual void TrackCombatTarget(float Dt)override;
    virtual void UpdateAttack(float Dt)override;
    virtual void CancelAttack()override;
    virtual float RecoveryDuration(FName Pose,float Fallback)const override{return Pose==TEXT("Down")?1.133333f:Pose==TEXT("Wake")?.3f:Fallback;}
private:
    void PoseAt(FName Clip,float Time,bool Loop);
    void PlayActionSound(int32 Id);
    void FireRound();
    bool HasMuzzleClearLine(const AHeliosEnemyActor* Enemy) const;
    float PoseAge=0,MoveAge=0,FireAge=100,ReloadAge=-1,ShotWait=0;
    uint32 ReloadEvents=0;
    FName MovePose=TEXT("Run_Fwd");
    FRandomStream SpreadRandom{133922};
    UPROPERTY(Transient) TMap<int32,TObjectPtr<USoundBase>> Sounds;
    UPROPERTY(Transient) TArray<TObjectPtr<class UAudioComponent>> SoundChannels;
    UPROPERTY(Transient) TObjectPtr<class AHeliosBuLianShiP90Effect> IdleEffect;
};
