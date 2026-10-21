#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosSSXDemo.generated.h"
UCLASS()
class AHeliosSSXDemo : public AActor
{
    GENERATED_BODY()
public:
    AHeliosSSXDemo();
    virtual void Tick(float Dt) override;
private:
    void Capture(const TCHAR* Name);
    void Check(const TCHAR* Name,bool Pass);
    void Finish();
    void TickEffectQA(class AHeliosSunShangXiangWeapon* Weapon,class AHeliosSunShangXiangPet* Pet);
    void TickVoiceQA(class AHeliosSunShangXiangWeapon* Weapon,class AHeliosSunShangXiangPet* Pet);
    void TickDamage07QA();
    void TickInputControlQA(class APlayerController* PC,class AHeliosAdventureHUD* HUD);
    void TickZXKMovingWaveQA(class AHeliosZhongXiaoKuiWeapon* W);
    int32 Damage07Before=0;
    float HeavyTargetHPBefore=0;
    FVector MovingPreviousHand=FVector::ZeroVector;
    FString MovingPreviousSequence;
    uint32 MovingPreviousSerial=0;
    void TickLightQA(class AHeliosSunShangXiangWeapon* Weapon,class AHeliosSunShangXiangPet* Pet);
    void TickZXKQA(class APlayerController* PC,class AHeliosAdventureHUD* HUD);
    void TickZXKFeedback(class AHeliosZhongXiaoKuiWeapon* W,class AHeliosZhongXiaoKuiPet* Pet,float Dt);
    FString FeedbackSamples;
    int32 FeedbackFrame=0;
    float FeedbackLastSample=-1;
    FVector FeedbackStart=FVector::ZeroVector,FeedbackFoot=FVector::ZeroVector;
    TSet<int32> VoiceCoverage[2];
    int32 VoiceSample=0,VoiceBaseline=0;
    double VoiceStartedAt=0;
    float VoiceExpectedDuration=0;
    int32 EffectSample=0;
    FVector EffectQAOrigin=FVector::ZeroVector;
    UPROPERTY() TObjectPtr<class AStaticMeshActor> EffectBackdrop;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> EffectBackdropMaterial;
    int32 Stage=0,Failures=0;
    uint64 InputAttackSerialBefore=0;
    int32 InputWaveShotsBefore=0;
    float InputSwitchAge=0;
    TSet<FName> InputSeenActions;
    float Age=0,Before=0,OriginalHP=0,OriginalSpeed=0;
    double Started=0;
    bool Captured=false;
    FString Checks;
    FVector FollowStart=FVector::ZeroVector;
    float FollowDistance=0;
    UPROPERTY() TObjectPtr<class AHeliosArenaCharacter> Player;
    UPROPERTY() TObjectPtr<class AHeliosEnemyActor> Target;
};
