#pragma once
#include "CoreMinimal.h"
#include "Pets/HeliosPetDog.h"
#include "Weapons/HeliosSunShangXiang.h"
#include "HeliosSunShangXiangPet.generated.h"

UCLASS()
class HELIOSOUTPOST_API AHeliosSunShangXiangPet : public AHeliosPetCompanion
{
    GENERATED_BODY()
    friend class FHeliosSSXTest;
    friend class FHeliosSSXGaitTest;
    friend class AHeliosSSXDemo;
    friend class AHeliosUrbanQA;
public:
    AHeliosSunShangXiangPet();
    virtual FString DisplayName() const override { return TEXT("孙尚香"); }
    virtual void Tick(float Dt) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual float TakeDamage(float Amount,const FDamageEvent& Event,AController* InstigatorController,AActor* Causer) override;
protected:
    virtual void LoadModel() override;
    virtual void SetPose(FName Name,bool Loop=true) override;
    virtual void UpdateAttack(float Dt) override;
    virtual void CancelAttack() override;
    virtual float RecoveryDuration(FName Pose,float Fallback) const override;
    virtual void TrackCombatTarget(float Dt) override;
    virtual void UpdateLocomotion(float Dt) override;
private:
    friend class AHeliosMultiPetQA;
    void PoseAt(FName Name,float Time,bool Loop);
    void Contact(int32 Phase);
    void ShowEffect(int32 Phase);
    HeliosSSX::FTimeline Timeline;
    HeliosSSX::FHitAudioGate HitAudioGate;
    UPROPERTY(Transient) TObjectPtr<USoundBase> HitSound;
    float PoseAge=0,MoveAge=0,PlaybackSpeed=1;
    float SkillRemaining=0;
    FName MovePose=TEXT("Run_Fwd");
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosSSXEffect>> SourceEffects;
    UPROPERTY(Transient) TObjectPtr<class UHeliosSSXEffectLibrary> ResidentEffectLibrary;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> ActionAudio;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UHeliosSSXVoiceComponent> Voice;
};
