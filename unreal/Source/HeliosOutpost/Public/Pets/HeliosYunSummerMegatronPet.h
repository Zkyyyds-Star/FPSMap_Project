#pragma once
#include "CoreMinimal.h"
#include "Pets/HeliosPetDog.h"
#include "Weapons/HeliosYunSummerMegatron.h"
#include "HeliosYunSummerMegatronPet.generated.h"

UCLASS()
class HELIOSOUTPOST_API AHeliosYunSummerMegatronPet : public AHeliosPetCompanion
{
    GENERATED_BODY()
public:
    AHeliosYunSummerMegatronPet();
    virtual FString DisplayName() const override;
    virtual void Tick(float Dt) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual float TakeDamage(float Amount,const FDamageEvent& Event,AController* InstigatorController,AActor* Causer) override;
    EHeliosYunSummerForm GetForm() const { return Form; }
    void SetForm(EHeliosYunSummerForm InForm);
    FVector MuzzleLocation() const;
    bool IsReloading() const { return ReloadAge>=0; }
    void RecordImpact(int32 Hits,int32 Kills);
    void RecordKillEffect(const FVector& At);
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UStaticMeshComponent> Megatron;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UHeliosSSXVoiceComponent> Voice;
    int32 Ammo=20,ShotsFired=0,Reloads=0,TrueKills=0,KillEffectsSpawned=0;
    FHeliosMegatronTuning ShotSettings;
protected:
    virtual void LoadModel() override;
    virtual void SetPose(FName Name,bool Loop=true) override;
    virtual void UpdateLocomotion(float Dt) override;
    virtual void TrackCombatTarget(float Dt) override;
    virtual void UpdateAttack(float Dt) override;
    virtual void CancelAttack() override;
    virtual void UpdateRecoveryGround(float Dt) override;
    virtual float RecoveryDuration(FName Pose,float Fallback) const override;
private:
    void ApplyForm();
    void PoseAt(FName Clip,float Time,bool Loop);
    void PlayActionSound(int32 Id);
    void FireRound();
    bool HasMuzzleClearLine(const AHeliosEnemyActor* Enemy) const;
    bool HasMuzzleClearance() const;
    EHeliosYunSummerForm Form=EHeliosYunSummerForm::SP,PendingForm=EHeliosYunSummerForm::SP;
    bool bFormPending=false;
    float PoseAge=0,MoveAge=0,FireAge=100,ReloadAge=-1,ShotWait=0;
    uint32 ReloadEvents=0;
    FName MovePose=TEXT("Run_Fwd");
    UPROPERTY(Transient) TMap<int32,TObjectPtr<USoundBase>> Sounds;
    UPROPERTY(Transient) TArray<TObjectPtr<class UAudioComponent>> SoundChannels;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosSSXEffect>> ShotEffects;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosSSXEffect>> KillEffects;
    UPROPERTY(Transient) TObjectPtr<AHeliosSSXEffect> IdleEffect;
};
