#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "HeliosQingYaAbilityComponent.generated.h"
class USoundBase;
class UAudioComponent;

/** Source animation timing and explicitly labelled project gameplay adaptation. */
UCLASS(BlueprintType)
class HELIOSOUTPOST_API UHeliosQingYaSkillProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float SwitchDurationMultiplier = .7f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float BaseSwitchSeconds = .5f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float KillChainSeconds = 8.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float SpiritMovementMultiplier = 1.2f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float KickContactSeconds = .1f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float KickDamage = 60.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float KickRange = 260.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float KickSweepRadius = 30.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USoundBase> VictoryMusic;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bOriginalGameplayParametersVerified = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString SourceNotes;
};

UENUM(BlueprintType)
enum class EHeliosQingYaTalent : uint8 { RifleSwitch, ShotgunSwitch, KillChain };

/** Pawn-owned talent and action clocks survive form and primary-weapon changes. */
UCLASS(ClassGroup=(Helios),meta=(BlueprintSpawnableComponent))
class HELIOSOUTPOST_API UHeliosQingYaAbilityComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UHeliosQingYaAbilityComponent();
    virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Fn) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    bool IsSelected() const;
    bool StartKick();
    void Cancel();
    void ResetRun();
    void NotifyWeaponEquipped();
    void PlayVictoryMusic();
    float MovementMultiplier() const;
    float KillWindowSeconds() const;
    float SwitchDurationMultiplier(FName WeaponId) const;
    FString StatusText() const;
    int32 GetVictoryPlays() const{return VictoryPlays;}
    bool IsAttacking() const{return bKicking;}
    float SwitchRemaining() const{return IsSelected()?SwitchClock:0.f;}
    EHeliosQingYaTalent GetTalent() const{return Talent;}
    int32 GetTalentDraws() const{return TalentDraws;}
    int32 Kicks=0,Hits=0;
    int32 VictoryPlays=0;
private:
    friend class AHeliosQingYaQA;
    UHeliosQingYaSkillProfile* LoadProfile() const;
    void EnsureTalent();
    void StopVictoryMusic();
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> VictoryAudio;
    TWeakObjectPtr<UAudioComponent> BackgroundMusic;
    FTimerHandle VictoryRestoreTimer;
    void Strike();
    UPROPERTY(Transient) mutable TObjectPtr<UHeliosQingYaSkillProfile> Profile;
    bool bTalentDrawn=false,bKicking=false,bStruck=false,bVictoryPlayed=false;
    EHeliosQingYaTalent Talent=EHeliosQingYaTalent::RifleSwitch;
    int32 TalentDraws=0;
    float KickAge=0.f,SwitchClock=0.f;
};
