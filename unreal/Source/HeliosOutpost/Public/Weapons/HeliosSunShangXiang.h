#pragma once

#include "CoreMinimal.h"
#include "Weapons/HeliosCombatWeapons.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "HeliosSunShangXiang.generated.h"

class UAnimSequence;
class UAudioComponent;
class AHeliosSSXEffect;
class AHeliosEnemyActor;
class UHeliosSSXVoiceComponent;

/** Game-time cooldowns outlive individual weapon actors and map travel. */
UCLASS()
class HELIOSOUTPOST_API UHeliosSSXSession : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    float PlayerRemaining = 0;
    float PetRemaining = 0;
};

UCLASS()
class HELIOSOUTPOST_API UHeliosSSXClock : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Tick(float Dt) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UHeliosSSXClock, STATGROUP_Tickables); }
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override { return Type==EWorldType::Game || Type==EWorldType::PIE; }
};

namespace HeliosSSX
{
    /** One impact cue per successful contact phase, even when the sweep hits several components/targets. */
    struct HELIOSOUTPOST_API FHitAudioGate
    {
        uint32 Submitted = 0;
        int32 Contacts = 0;
        void Reset() { Submitted=0; }
        bool Accept(int32 Phase,int32 DamagedTargets)
        {
            if(Phase<0||Phase>=4||DamagedTargets<=0||(Submitted&(1u<<Phase)))return false;
            Submitted|=1u<<Phase;++Contacts;return true;
        }
    };
    /** Optional verified original impact asset. Swing/voice assets are never a fallback. */
    HELIOSOUTPOST_API USoundBase* OriginalHitSound();
    enum class EAttack : uint8 { None, Light, Heavy, Skill, Equip };
    struct HELIOSOUTPOST_API FTimeline
    {
        EAttack Attack = EAttack::None;
        float Age = 0;
        uint32 Committed = 0, Presented = 0;
        void Begin(EAttack Type) { Attack=Type;Age=0;Committed=Presented=0; }
        void Clear() { Begin(EAttack::None); }
        void Present(bool FP,TFunctionRef<void(int32)> Visual);
        float Duration(bool FP) const;
        void Pose(bool FP, FName& Name, float& Seconds) const;
        void Advance(float Dt, bool FP, TFunctionRef<void(int32)> Contact);
    };
    HELIOSOUTPOST_API UHeliosSSXSession* Session(const UObject* Context);
    HELIOSOUTPOST_API UAnimSequence* Animation(bool FP, FName Name);
    HELIOSOUTPOST_API float Damage(const UObject* Context, EAttack Attack, bool Pet);
    HELIOSOUTPOST_API float Tempo(const UObject* Context, bool Pet);
    HELIOSOUTPOST_API float Cooldown(const UObject* Context, bool Pet);
    HELIOSOUTPOST_API int32 Strike(AActor* Source, APawn* Instigator, AActor* Friendly,
        const FVector& Start, const FVector& Direction, float Reach, float Radius, float Damage,
        TFunction<void(AHeliosEnemyActor*,const FHitResult&,bool)> Feedback = {});
}

UCLASS()
class HELIOSOUTPOST_API UHeliosSunShangXiangDefinition : public UHeliosWeaponDefinition
{
    GENERATED_BODY()
public:
    UHeliosSunShangXiangDefinition();
};

UCLASS()
class HELIOSOUTPOST_API AHeliosSunShangXiangWeapon : public AHeliosCombatWeapon
{
    GENERATED_BODY()
    friend class FHeliosSSXTest;
    friend class AHeliosSSXDemo;
public:
    AHeliosSunShangXiangWeapon();
    virtual void ApplyDefinition(const UHeliosWeaponDefinition* Definition) override;
    virtual void ActivateWeapon(const FName& Tag) override;
    virtual void DeactivateWeapon() override;
    virtual void Tick(float Dt) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void StartFiring() override;
    virtual void StopFiring() override { bPrimaryDown=false; }
    virtual void StartSecondaryFire() override;
    virtual void StopSecondaryFire() override { bSecondaryDown=false; }
    virtual void StartSpecialFire() override;
    virtual bool HandleOwnerCombatCue(FName Cue) override;
    virtual bool UsesAmmunition() const override { return false; }
    virtual void CancelReload() override;
    FString Status() const;
    float CooldownRemaining() const;
private:
    bool BeginAttack(HeliosSSX::EAttack Type);
    void Cancel();
    void Pose();
    void Contact(int32 Phase);
    void ShowEffect(int32 Phase);
    void UpdateBladeEffects();
    void ClearBladeEffects();
    void PlaySourceSound(const TCHAR* Name);
    HeliosSSX::FTimeline Timeline;
    bool bActive=false,bPrimaryDown=false,bSecondaryDown=false,bQueuedPrimary=false;
    float IdleAge=0,PlaybackSpeed=1,CompanionSkillRemaining=0,CompanionNextAttackAt=0;
    int32 HitCount=0;
    uint64 DamageAttackSerial=0;
    HeliosSSX::FHitAudioGate HitAudioGate;
    UPROPERTY(Transient) TObjectPtr<USoundBase> HitSound;
    UPROPERTY(Transient) TArray<TObjectPtr<UAnimSequence>> ResidentAnimations;
    UPROPERTY(Transient) TObjectPtr<class UHeliosSSXEffectLibrary> ResidentEffectLibrary;
    UPROPERTY(Transient) TArray<TObjectPtr<USoundBase>> ResidentSounds;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosSSXEffect>> Effects;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosSSXEffect>> BladeEffects;
    UPROPERTY(Transient) TObjectPtr<class UHeliosSSXEffectLibrary> BladeEffectLibrary;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> ActionAudio;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UHeliosSSXVoiceComponent> Voice;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UHeliosHunterDamageFeedback> DamageFeedback;
};
