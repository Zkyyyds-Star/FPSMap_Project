#pragma once
#include "CoreMinimal.h"
#include "Pets/HeliosPetDog.h"
#include "HeliosZhongXiaoKuiPet.generated.h"
class UHeliosSSXVoiceComponent;
class UAudioComponent;
class AHeliosSSXEffect;
UCLASS()
class HELIOSOUTPOST_API AHeliosZhongXiaoKuiPet : public AHeliosPetCompanion
{
    GENERATED_BODY()
    friend class FHeliosZXKTest;
    friend class AHeliosSSXDemo;
    friend class AHeliosUrbanQA;
public:
    AHeliosZhongXiaoKuiPet();
    virtual FString DisplayName() const override { return TEXT("钟小葵"); }
    virtual float TakeDamage(float Amount,const FDamageEvent& Event,AController* InstigatorController,AActor* Causer) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
protected:
    virtual void LoadModel() override;
    virtual void SetPose(FName Name,bool Loop=true) override;
    virtual void UpdateAttack(float Dt) override;
    virtual void CancelAttack() override;
    virtual void UpdateLocomotion(float Dt) override;
    virtual void TrackCombatTarget(float Dt) override;
private:
    friend class AHeliosMultiPetQA;
    void PoseAt(FName Name,float Time,bool Loop);
    void PoseAttack();
    void AudioBetween(float Previous,float Time);
    float MoveAge=0,IdleAge=0,SourceDuration=0;
    FName Action,Recover;
    FName MovePose=TEXT("Run_Fwd");
    int32 Contacts=0;
    bool bContact=false;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UHeliosSSXVoiceComponent> Voice;
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> ActionChannels;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosSSXEffect>> SourceEffects;
    UPROPERTY(Transient) TObjectPtr<class UHeliosSSXEffectLibrary> ResidentEffectLibrary;
};
