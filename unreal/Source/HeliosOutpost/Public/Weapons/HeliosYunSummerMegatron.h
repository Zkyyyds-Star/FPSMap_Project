#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Weapons/HeliosCombatWeapons.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "HeliosYunSummerMegatron.generated.h"

class UAnimSequence;
class UAudioComponent;
class UHeliosSSXVoiceComponent;
class AHeliosSSXEffect;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMesh;
class AHeliosMegatronGrenade;

UENUM(BlueprintType)
enum class EHeliosYunSummerForm : uint8 { GR, BL, SP };

struct HELIOSOUTPOST_API FHeliosMegatronTuning
{
    float Damage=720,MinimumDamage=240,Radius=450,Speed=7000,Range=12000;
    float Interval=.5666667f,Reload=2.f;
    int32 Magazine=20;
};

namespace HeliosYunSummer
{
    HELIOSOUTPOST_API const TCHAR* FormCode(EHeliosYunSummerForm Form);
    HELIOSOUTPOST_API const TCHAR* FormLabel(EHeliosYunSummerForm Form);
    HELIOSOUTPOST_API FString PreviewPath(EHeliosYunSummerForm Form);
    HELIOSOUTPOST_API FString IconPath(EHeliosYunSummerForm Form);
    HELIOSOUTPOST_API float Setting(bool Pet,const TCHAR* Key,float Default,float Min,float Max);
    HELIOSOUTPOST_API FHeliosMegatronTuning Tuning(bool Pet);
    HELIOSOUTPOST_API UAnimSequence* Animation(const FString& Prefix,FName Clip);
    HELIOSOUTPOST_API USoundBase* Sound(int32 Id);
    HELIOSOUTPOST_API void InitializeVoice(UHeliosSSXVoiceComponent* Voice,EHeliosYunSummerForm Form,bool Spatial);
    HELIOSOUTPOST_API AHeliosSSXEffect* Effect(AActor* Owner,const TCHAR* Name,const FVector& At,const FRotator& Rotation,bool FP,float Rate=1.f);
    HELIOSOUTPOST_API AHeliosSSXEffect* AttachedEffect(AActor* Owner,USceneComponent* Parent,const TCHAR* Name,bool FP,FName Socket=NAME_None,float Rate=1.f);
    HELIOSOUTPOST_API AHeliosMegatronGrenade* Launch(AActor* Source,APawn* Instigator,const FVector& View,const FVector& Direction,const FVector& Muzzle,const FHeliosMegatronTuning& Tuning,bool FirstPerson);
    HELIOSOUTPOST_API bool IsFriendlyExplosion(const FDamageEvent& Event,AController* Instigator,AActor* Causer);
}

UCLASS()
class HELIOSOUTPOST_API UHeliosYunSummerSession : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    EHeliosYunSummerForm GetPlayerForm() const { return PlayerForm; }
    void SetPlayerForm(EHeliosYunSummerForm Form);
    EHeliosYunSummerForm GetPetForm(int32 Slot) const;
    void SetPetForm(int32 Slot,EHeliosYunSummerForm Form);
    bool LoadSelection(const FString& Path);
    bool SaveSelection(const FString& Path) const;
private:
    void SaveSelection() const;
    EHeliosYunSummerForm PlayerForm=EHeliosYunSummerForm::SP;
    TArray<EHeliosYunSummerForm> PetForms={EHeliosYunSummerForm::SP,EHeliosYunSummerForm::SP,EHeliosYunSummerForm::SP,EHeliosYunSummerForm::SP};
};

UCLASS(NotBlueprintable)
class HELIOSOUTPOST_API AHeliosMegatronGrenade : public AActor
{
    GENERATED_BODY()
public:
    AHeliosMegatronGrenade();
    void Initialize(AActor* Source,APawn* InInstigator,const FVector& Direction,const FHeliosMegatronTuning& Tuning,bool FirstPerson);
    void Explode(const FVector& At,const FVector& Normal);
    virtual void Tick(float Dt) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    bool HasExploded() const { return bExploded; }
    int32 Hits=0,Kills=0;
private:
    FHeliosMegatronTuning Settings;
    bool IsBioLaunchContextCurrent() const;
    int32 BioLaunchRound=INDEX_NONE,BioLaunchParticipant=INDEX_NONE,BioLaunchTeam=INDEX_NONE;
    FVector Velocity=FVector::ZeroVector;
    float Travel=0,Age=0;
    bool bExploded=false;
    TWeakObjectPtr<AActor> DamageSource;
    TWeakObjectPtr<APawn> DamageInstigator;
    UPROPERTY(Transient) TObjectPtr<AHeliosSSXEffect> FlightEffect;
};

UCLASS()
class HELIOSOUTPOST_API UHeliosYunSummerMegatronDefinition : public UHeliosWeaponDefinition
{
    GENERATED_BODY()
public:
    UHeliosYunSummerMegatronDefinition();
};

UCLASS()
class HELIOSOUTPOST_API AHeliosYunSummerMegatronWeapon : public AHeliosCombatWeapon
{
    GENERATED_BODY()
public:
    AHeliosYunSummerMegatronWeapon();
    virtual void ApplyDefinition(const UHeliosWeaponDefinition* Definition) override;
    virtual void ActivateWeapon(const FName& Tag) override;
    virtual void DeactivateWeapon() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float Dt) override;
    virtual void StartFiring() override;
    virtual void StopFiring() override;
    virtual void StartSpecialFire() override;
    void StartCheers();
    virtual void BeginReload(float Seconds) override;
    virtual void ReloadMagazine() override;
    virtual void CancelReload() override;
    virtual bool HandleOwnerCombatCue(FName Cue) override;
    EHeliosYunSummerForm GetForm() const { return Form; }
    void SetForm(EHeliosYunSummerForm InForm);
    void ToggleForm();
    FString Status() const;
    FVector MuzzleLocation() const;
    bool TryGetNativeThirdPersonMuzzle(FVector& Location) const;
    // Original TP static locator, bridged through the derived weapon's real skin bone.
    UPROPERTY(EditDefaultsOnly, Category="Native TP Muzzle") TSoftObjectPtr<UStaticMesh> NativeTPMuzzleSource;
    UPROPERTY(EditDefaultsOnly, Category="Native TP Muzzle") FName NativeTPMuzzleSocket=TEXT("MuzzleFlashSocket");
    UPROPERTY(EditDefaultsOnly, Category="Native TP Muzzle") FName NativeTPMuzzleBone=TEXT("b_RightWeapon");
    bool IsShowing() const { return Action==TEXT("Show")||Action==TEXT("Cheers"); }
    bool IsKillEffectActive() const;
    bool IsReloading() const { return bReload; }
    void RecordImpact(int32 Hits,int32 Kills);
    void RecordKillEffect(const FVector& At);
    UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> Megatron;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> Cup;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UHeliosSSXVoiceComponent> Voice;
    int32 ShotsFired=0,TrueKills=0,HitsDelivered=0,Reloads=0,KillEffectsSpawned=0;
    FHeliosMegatronTuning ShotSettings;
protected:
    virtual void FireProjectile(const FVector& Target) override;
private:
    void ApplyForm();
    void ApplyMovementBonus(bool Enabled);
    void BeginAction(FName Clip,float Duration);
    void Pose();
    void PlayActionSound(int32 Id);
    void CancelSounds();
    void ClearEffects(bool All);
    EHeliosYunSummerForm Form=EHeliosYunSummerForm::SP,PendingForm=EHeliosYunSummerForm::SP;
    bool bActive=false,bReload=false,bTriggerHeld=false,bFormPending=false;
    bool bNativeTPMuzzleWarned=false;
    FName Action;
    float ActionAge=0,ActionDuration=0,IdleAge=0;
    double ReloadStartedAt=-1;
    uint32 EventMask=0;
    UPROPERTY(Transient) TMap<FName,TObjectPtr<UAnimSequence>> ArmsClips;
    UPROPERTY(Transient) TMap<FName,TObjectPtr<UAnimSequence>> GunClips;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> CupCheers;
    UPROPERTY(Transient) TMap<int32,TObjectPtr<USoundBase>> ActionSounds;
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> SoundChannels;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosSSXEffect>> Effects;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosSSXEffect>> ActionEffects;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosSSXEffect>> KillEffects;
};
