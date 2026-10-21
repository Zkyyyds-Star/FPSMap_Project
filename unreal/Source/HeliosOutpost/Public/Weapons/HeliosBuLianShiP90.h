#pragma once
#include "CoreMinimal.h"
#include "Weapons/HeliosCombatWeapons.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "HeliosBuLianShiP90.generated.h"

class UHeliosSSXVoiceComponent;
class UAnimSequence;
class USkeletalMeshComponent;
class UAudioComponent;
class AHeliosBuLianShiP90Effect;
class USceneComponent;
struct HELIOSOUTPOST_API FHeliosP90Tuning
{
    float Damage=24,MinimumDamage=9,Range=3500,Falloff=1000,Spread=1,Interval=.08f,Reload=2.f,Recoil=.12f;
    int32 Pellets=1,Magazine=50;
};
namespace HeliosBuLianShiP90
{
    HELIOSOUTPOST_API FHeliosP90Tuning Tuning(bool Pet);
    HELIOSOUTPOST_API float Setting(bool Pet,const TCHAR* Key,float Default,float Min,float Max);
    HELIOSOUTPOST_API UAnimSequence* Animation(const TCHAR* Prefix,FName Clip);
    HELIOSOUTPOST_API USoundBase* Sound(int32 Id);
    HELIOSOUTPOST_API void InitializeVoice(UHeliosSSXVoiceComponent* Voice,bool Spatial);
    HELIOSOUTPOST_API void InitializeFormVoice(UHeliosSSXVoiceComponent* Voice,FName Variant,bool Spatial);
    HELIOSOUTPOST_API void CombatVoice(UHeliosSSXVoiceComponent* Voice,FName Cue);
    HELIOSOUTPOST_API AHeliosBuLianShiP90Effect* Effect(AActor* Owner,const TCHAR* Name,const FVector& At,const FRotator& Rotation,bool FP);
    HELIOSOUTPOST_API AHeliosBuLianShiP90Effect* AttachedEffect(AActor* Owner,USceneComponent* Parent,const TCHAR* Name,bool FP,float Scale=1,FName Socket=NAME_None);
    // Counts only the alive -> dead transition caused by these traces, once per enemy.
    HELIOSOUTPOST_API int32 FirePellets(AActor* Source,APawn* Instigator,AActor* Friendly,
        const FVector& View,const FVector& Direction,const FVector& Muzzle,const FHeliosP90Tuning& Settings,FRandomStream& Random,int32& Hits);
}
UCLASS()
class HELIOSOUTPOST_API UHeliosBuLianShiP90Definition : public UHeliosWeaponDefinition
{
    GENERATED_BODY()
public:
    UHeliosBuLianShiP90Definition();
};
UCLASS()
class HELIOSOUTPOST_API AHeliosBuLianShiP90Weapon : public AHeliosCombatWeapon
{
    GENERATED_BODY()
public:
    AHeliosBuLianShiP90Weapon();
    virtual void ApplyDefinition(const UHeliosWeaponDefinition* Definition) override;
    virtual void ActivateWeapon(const FName& Tag) override;
    virtual void DeactivateWeapon() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float Dt) override;
    virtual void StartFiring() override;
    virtual void StartSpecialFire() override;
    virtual void BeginReload(float Seconds) override;
    virtual void CancelReload() override;
    virtual bool HandleOwnerCombatCue(FName Cue) override;
    FVector MuzzleLocation() const;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> P90;
    UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<USkeletalMeshComponent>> GunParts;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UHeliosSSXVoiceComponent> Voice;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UHeliosP90SubstituteFireAudio> FireAudio;
    int32 ShotsFired=0,TrueKills=0,HitPellets=0,Reloads=0;
    FHeliosP90Tuning ShotSettings;
protected:
    virtual void FireProjectile(const FVector& Target) override;
private:
    void BeginAction(FName Clip,float Duration);
    void Pose();
    void PlayActionSound(int32 Id,float Gain=1);
    void CancelSounds();
    void ClearEffects();
    FName Action;
    float ActionAge=0,ActionDuration=0,IdleAge=0;
    uint32 EventMask=0;
    bool bActive=false,bReload=false;
    FRandomStream SpreadRandom{133921};
    UPROPERTY(Transient) TMap<FName,TObjectPtr<UAnimSequence>> ArmsClips;
    UPROPERTY(Transient) TMap<FName,TObjectPtr<UAnimSequence>> GunClips;
    UPROPERTY(Transient) TMap<int32,TObjectPtr<USoundBase>> ActionSounds;
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> SoundChannels;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosBuLianShiP90Effect>> Effects;
};
