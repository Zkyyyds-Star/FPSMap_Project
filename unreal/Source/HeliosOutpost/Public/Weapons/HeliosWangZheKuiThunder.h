#pragma once
#include "CoreMinimal.h"
#include "Weapons/HeliosCombatWeapons.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "HeliosWangZheKuiThunder.generated.h"

class UHeliosSSXVoiceComponent;
class UAnimSequence;
class USkeletalMeshComponent;
class UAudioComponent;
class AHeliosSSXEffect;
class USceneComponent;
struct HELIOSOUTPOST_API FHeliosThunderTuning
{
    float Damage=16,MinimumDamage=4,Range=3000,Falloff=500,Spread=3,Interval=.22f,Reload=2.3f;
    int32 Pellets=10,Magazine=25;
};
namespace HeliosWangZheKuiThunder
{
    HELIOSOUTPOST_API FHeliosThunderTuning Tuning(bool Pet,bool Slug=false);
    HELIOSOUTPOST_API float Setting(bool Pet,const TCHAR* Key,float Default,float Min,float Max,bool Slug=false);
    HELIOSOUTPOST_API UAnimSequence* Animation(const TCHAR* Prefix,FName Clip);
    HELIOSOUTPOST_API USoundBase* Sound(int32 Id);
    HELIOSOUTPOST_API void InitializeVoice(UHeliosSSXVoiceComponent* Voice,bool Spatial);
    HELIOSOUTPOST_API void InitializeFormVoice(UHeliosSSXVoiceComponent* Voice,FName Variant,bool Spatial);
    HELIOSOUTPOST_API void CombatVoice(UHeliosSSXVoiceComponent* Voice,FName Cue);
    HELIOSOUTPOST_API AHeliosSSXEffect* Effect(AActor* Owner,const TCHAR* Name,const FVector& At,const FRotator& Rotation,bool FP,float PlaybackRate=1.f);
    HELIOSOUTPOST_API AHeliosSSXEffect* AttachedEffect(AActor* Owner,USceneComponent* Parent,const TCHAR* Name,bool FP,float Scale=1,FName Socket=NAME_None,float PlaybackRate=1.f);
    // Counts only the alive -> dead transition caused by these traces, once per enemy.
    HELIOSOUTPOST_API int32 FirePellets(AActor* Source,APawn* Instigator,AActor* Friendly,
        const FVector& View,const FVector& Direction,const FVector& Muzzle,const FHeliosThunderTuning& Settings,FRandomStream& Random,int32& Hits);
}
UCLASS()
class HELIOSOUTPOST_API UHeliosWangZheKuiThunderDefinition : public UHeliosWeaponDefinition
{
    GENERATED_BODY()
public:
    UHeliosWangZheKuiThunderDefinition();
};
UCLASS()
class HELIOSOUTPOST_API AHeliosWangZheKuiThunderWeapon : public AHeliosCombatWeapon
{
    GENERATED_BODY()
public:
    AHeliosWangZheKuiThunderWeapon();
    virtual void ApplyDefinition(const UHeliosWeaponDefinition* Definition) override;
    virtual void ActivateWeapon(const FName& Tag) override;
    virtual void DeactivateWeapon() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float Dt) override;
    virtual void StartFiring() override;
    virtual void StopFiring() override;
    virtual void StartSpecialFire() override;
    virtual void StartSecondaryFire() override;
    bool IsSlugMode()const{return bSlug;}
    bool IsSwitchingMode()const{return Action==TEXT("Skill")||Action==TEXT("Skill_Cancellation");}
    FString Status()const;
    virtual void BeginReload(float Seconds) override;
    virtual void CancelReload() override;
    virtual bool HandleOwnerCombatCue(FName Cue) override;
    FVector MuzzleLocation() const;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> Thunder;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UHeliosSSXVoiceComponent> Voice;
    int32 ShotsFired=0,TrueKills=0,HitPellets=0,Reloads=0;
    FHeliosThunderTuning ShotSettings;
protected:
    virtual void FireProjectile(const FVector& Target) override;
private:
    void BeginAction(FName Clip,float Duration);
    void Pose();
    void PlayActionSound(int32 Id,float Gain=1);
    void CancelSounds();
    void ClearEffects();
    void ClearActionEffects();
    FName Action;
    float ActionAge=0,ActionDuration=0,IdleAge=0;
    uint32 EventMask=0;
    bool bActive=false,bReload=false,bSlug=false,bPendingSlug=false,bTriggerHeld=false;
    FRandomStream SpreadRandom{133921};
    UPROPERTY(Transient) TMap<FName,TObjectPtr<UAnimSequence>> ArmsClips;
    UPROPERTY(Transient) TMap<FName,TObjectPtr<UAnimSequence>> GunClips;
    UPROPERTY(Transient) TMap<int32,TObjectPtr<USoundBase>> ActionSounds;
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> SoundChannels;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosSSXEffect>> Effects;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosSSXEffect>> ActionEffects;
};
