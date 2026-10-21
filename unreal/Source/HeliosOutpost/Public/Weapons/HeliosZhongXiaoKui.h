#pragma once
#include "CoreMinimal.h"
#include "Weapons/HeliosCombatWeapons.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Components/PoseableMeshComponent.h"
#include "HeliosZhongXiaoKui.generated.h"
class UAnimSequence;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UHeliosSSXVoiceComponent;
class UAudioComponent;
class AHeliosSSXEffect;
class USoundBase;
class AHeliosZXKProjectile;
class AHeliosEnemyActor;
/** Copy the rendered native pose after the main mesh finishes its animation tick. */
UCLASS()
class HELIOSOUTPOST_API UHeliosZXKWaveDepthComponent : public UPoseableMeshComponent
{
    GENERATED_BODY()
public:
    UHeliosZXKWaveDepthComponent();
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
};
namespace HeliosZXK
{
    HELIOSOUTPOST_API UAnimSequence* Animation(bool FP,FName Name);
    HELIOSOUTPOST_API void Sample(USkeletalMeshComponent* Mesh,UAnimSequence* Clip,float Time,bool Loop);
    HELIOSOUTPOST_API USoundBase* OriginalSound(int32 AudioId);
    HELIOSOUTPOST_API void InitializeVoice(UHeliosSSXVoiceComponent* Voice,bool Spatial);
    HELIOSOUTPOST_API int32 PlayEvents(AActor* Owner,UHeliosSSXVoiceComponent* Voice,bool FP,FName Clip,float PreviousTime,float Time,TArray<TObjectPtr<UAudioComponent>>& Channels);
    HELIOSOUTPOST_API AHeliosSSXEffect* Effect(AActor* Owner,const FString& Name,const FVector& At,const FRotator& Rotation,bool FP);
}
/** Original hunter rig, animation and audio; combat rules adapt to this project. */
UCLASS()
class HELIOSOUTPOST_API UHeliosZhongXiaoKuiDefinition : public UHeliosWeaponDefinition
{
    GENERATED_BODY()
public:
    UHeliosZhongXiaoKuiDefinition();
};
UCLASS()
class HELIOSOUTPOST_API AHeliosZhongXiaoKuiWeapon : public AHeliosCombatWeapon
{
    GENERATED_BODY()
    friend class FHeliosZXKTest;
    friend class AHeliosSSXDemo;
public:
    AHeliosZhongXiaoKuiWeapon();
    virtual void ApplyDefinition(const UHeliosWeaponDefinition* Definition) override;
    virtual void ActivateWeapon(const FName& Tag) override;
    virtual void DeactivateWeapon() override;
    virtual void Tick(float Dt) override;
    virtual void StartFiring() override;
    virtual void StopFiring() override { bHeld=false; }
    virtual void StartSecondaryFire() override;
    virtual void StartSpecialFire() override;
    void ToggleForm();
    void ToggleRangedForm();
    void RequestSourceReload();
    void StartSwordWave();
    int32 GetForm() const { return Form; }
    int32 GetPendingForm() const { return PendingForm; }
    bool IsChangingForm() const { return Age>=0 && ActionKind==2; }
    bool ShouldDisplayDart() const { return bActive && (Form==2 || (IsChangingForm() && PendingForm==2)); }
    UStaticMeshComponent* GetDartMesh() const { return Dart; }
    int32 GetDartAmmo() const { return DartAmmo; }
    float GetSkillCooldownEnd() const { return SkillCooldownEnd; }
    FName GetCurrentSourceAction() const { return Age>=0 ? Action : NAME_None; }
    FString Status() const;
    void ProjectileContact(bool Killed);
    virtual void StopSecondaryFire() override { bWaveHeld=false; }
    virtual void CancelReload() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual bool HandleOwnerCombatCue(FName Cue) override;
    virtual bool UsesAmmunition() const override { return false; }
private:
    void StartAttack(bool Heavy);
    void Cancel();
    void Pose();
    void AudioBetween(float Previous,float Time);
    void StartSourceAction(FName Clip,FName Recovery,int32 Kind);
    void StartTransition(int32 To);
    void ShowSourceEffect(const FString& Name,FName Bone=NAME_None);
    void ThrowProjectile(bool SwordWave);
    void StartFormAttack(bool Heavy);
    float Duration() const;
    bool bActive=false,bHeld=false,bHeavy=false,bCommitted=false,bWaveHeld=false;
    float Age=-1,IdleAge=0,CompanionNextAttackAt=0;
    float WaveViewTailEnd=0;
    int32 Combo=0,HitCount=0,AudioEventCount=0,WaveShots=0;
    uint64 DamageAttackSerial=0;
    FName Action,Recover;
    int32 Form=0,PendingForm=0,ActionKind=0,DartAmmo=20;
    bool bQueuedRanged=false;
    float SkillCooldownEnd=0,WaveCooldownEnd=0;
    UPROPERTY(Transient) TMap<FName,TObjectPtr<UAnimSequence>> Clips;
    UPROPERTY(Transient) TObjectPtr<class UHeliosSSXEffectLibrary> ResidentEffectLibrary;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UHeliosSSXVoiceComponent> Voice;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UHeliosHunterDamageFeedback> DamageFeedback;
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> ActionChannels;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosSSXEffect>> Effects;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Dart;
    UPROPERTY(Transient) TArray<TObjectPtr<class UMaterialInterface>> NormalViewMaterials;
    UPROPERTY(Transient) TArray<TObjectPtr<class UMaterialInterface>> WaveViewMaterials;
    bool bWaveLateView=false;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UPoseableMeshComponent> WaveSelfDepth;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosZXKProjectile>> Projectiles;
};
