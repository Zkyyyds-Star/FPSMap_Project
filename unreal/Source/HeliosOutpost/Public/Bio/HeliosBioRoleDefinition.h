#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HeliosBioRoleDefinition.generated.h"
class USkeletalMesh;
class UAnimSequence;
class USoundBase;
class UStaticMesh;

UENUM(BlueprintType)
enum class EHeliosBioAbility : uint8
{
    None, Sprint, Shield, FourSlash, AreaStrike, Blink, FreezeGrenade,
    MechanicalFire, SacredShield, ZhongXiaoKuiBlade, AbyssEnergy, CloneSummon
};

/** Derived art with explicit source identity; rules do not imply art verification. */
UCLASS(BlueprintType)
class HELIOSOUTPOST_API UHeliosBioRoleDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere) FString SourceIdentity;
    UPROPERTY(EditAnywhere) FString DisplayName;
    UPROPERTY(EditAnywhere) bool bIdentityVerified = false;
    // Source identity and implemented mechanics are independent readiness gates.
    UPROPERTY(EditAnywhere) bool bGameplayVerified = false;
    UPROPERTY(EditAnywhere) EHeliosBioAbility Ability = EHeliosBioAbility::None;
    UPROPERTY(EditAnywhere) FString SkillDescription;
    UPROPERTY(EditAnywhere) float SkillDuration = 4.f;
    UPROPERTY(EditAnywhere) float SkillCooldown = 15.f;
    UPROPERTY(EditAnywhere) float SkillRange = 600.f;
    UPROPERTY(EditAnywhere) float SkillDamage = 1800.f;
    UPROPERTY(EditAnywhere) float SkillSpeedMultiplier = 1.f;
    UPROPERTY(EditAnywhere) float SkillDamageMultiplier = 1.f;
    UPROPERTY(EditAnywhere) float SkillShieldReduction = 0.f;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UStaticMesh> SkillProjectileMesh;
    UPROPERTY(EditAnywhere) FString SkillEffectLibrary;
    UPROPERTY(EditAnywhere) FString FirstPersonSkillEffect;
    UPROPERTY(EditAnywhere) FString ThirdPersonSkillEffect;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UStaticMesh> SkillAuxiliaryMesh;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<USkeletalMesh> SkillFirstPersonMesh;
    UPROPERTY(EditAnywhere) FTransform SkillFirstPersonTransform;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UStaticMesh> SecondaryProjectileMesh;
    UPROPERTY(EditAnywhere) float SkillObjectLifetime = 20.f;
    UPROPERTY(EditAnywhere) float SkillObjectHealth = 1000.f;
    UPROPERTY(EditAnywhere) float SkillFireInterval = .2f;
    UPROPERTY(EditAnywhere) float SkillExplosionRadius = 300.f;
    UPROPERTY(EditAnywhere) float SkillExplosionDamage = 1000.f;
    UPROPERTY(EditAnywhere) float SecondaryCooldown = 15.f;
    UPROPERTY(EditAnywhere) float SecondaryDuration = 8.f;
    UPROPERTY(EditAnywhere) float SecondaryDamage = 1800.f;
    UPROPERTY(EditAnywhere) float SecondaryRange = 650.f;
    UPROPERTY(EditAnywhere) float SecondarySpeedMultiplier = 1.4f;
    UPROPERTY(EditAnywhere) float SecondaryDamageMultiplier = 1.75f;
    UPROPERTY(EditAnywhere) FString SwordWaveEffect;
    UPROPERTY(EditAnywhere) FString SecondaryEffect;
    UPROPERTY(EditAnywhere) FString SecondaryProjectileEffect;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> FPSecondarySkill;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> TPSecondarySkill;
    // Rescue serum is a separate original gun, not the F freeze grenade or sword.
    // Asset preparation alone never grants the role's independent gameplay gate.
    UPROPERTY(EditAnywhere) bool bSerumSourceVerified = false;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<USkeletalMesh> SecondaryFirstPersonMesh;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UStaticMesh> SecondaryThirdPersonWeaponMesh;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> FPSecondaryIdle;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> FPSecondaryEquip;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> TPSecondaryIdle;
    UPROPERTY(EditAnywhere) FTransform SecondaryWeaponTransform;
    UPROPERTY(EditAnywhere) FTransform SecondaryThirdPersonTransform;
    UPROPERTY(EditAnywhere) FName SecondaryFirstPersonMuzzleSocket = TEXT("MuzzleFlashSocket");
    UPROPERTY(EditAnywhere) FVector SecondaryThirdPersonMuzzleLocal = FVector::ZeroVector;
    UPROPERTY(EditAnywhere) float SerumProjectileSpeed = 6000.f;
    UPROPERTY(EditAnywhere) float SerumProjectileRadius = 3.f;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<USoundBase> SecondaryFireSound;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<USoundBase> SecondaryEquipSound;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<USoundBase> SerumCureSound;
    UPROPERTY(EditAnywhere) FString SerumSourceScope;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<USkeletalMesh> PrimaryFirstPersonMesh;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<USkeletalMesh> PrimaryThirdPersonWeaponMesh;
    UPROPERTY(EditAnywhere) bool bPrimaryThirdPersonIsBody = false;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> FPPrimaryIdle;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> FPPrimaryEquip;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> FPPrimaryFire;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> TPPrimaryIdle;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> TPPrimaryFire;
    UPROPERTY(EditAnywhere) FTransform PrimaryWeaponTransform;
    UPROPERTY(EditAnywhere) FTransform PrimaryThirdPersonTransform;
    UPROPERTY(EditAnywhere) float PrimaryFireInterval = .066668f;
    UPROPERTY(EditAnywhere) float PrimaryDamage = 160.f;
    UPROPERTY(EditAnywhere) float PrimaryRange = 6000.f;
    UPROPERTY(EditAnywhere) float PrimaryEnergy = 100.f;
    UPROPERTY(EditAnywhere) float PrimaryEnergyCost = 1.f;
    UPROPERTY(EditAnywhere) float PrimaryEnergyRecharge = 20.f;
    UPROPERTY(EditAnywhere) float PrimaryRechargeDelay = 1.f;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<USkeletalMesh> ThirdPersonMesh;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<USkeletalMesh> FirstPersonMesh;
    UPROPERTY(EditAnywhere) FTransform ThirdPersonTransform = FTransform(FRotator(0,-90,0),FVector(0,0,-96));
    UPROPERTY(EditAnywhere) FTransform FirstPersonTransform = FTransform(FRotator(0,-90,0));
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> TPIdle;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> TPRun;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> TPAttack;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> TPDeath;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> FPIdle;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> FPAttack;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> FPHeavy;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> FPRecover;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> FPEquip;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> FPMove;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> FPSkill;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> TPSkill;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> TPHeavy;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> TPRunBackward;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> TPRunLeft;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> TPRunRight;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> TPHit;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> FPSkillLoop;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> FPSkillEnd;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> TPSkillLoop;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UAnimSequence> TPSkillEnd;
    UPROPERTY(EditAnywhere) TArray<TSoftObjectPtr<UAnimSequence>> FPSkillChain;
    UPROPERTY(EditAnywhere) TArray<TSoftObjectPtr<UAnimSequence>> TPSkillChain;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<USoundBase> HeavySound;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<USoundBase> SkillSound;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<USoundBase> AttackSound;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<USoundBase> TransformSound;
};
