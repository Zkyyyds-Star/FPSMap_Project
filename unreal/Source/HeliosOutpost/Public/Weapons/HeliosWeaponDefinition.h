#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HeliosWeaponDefinition.generated.h"

class AShooterWeapon;
class UStaticMesh;
class USkeletalMesh;
class UTexture2D;
class USoundBase;
class UNiagaraSystem;
class UHeliosWeaponPresentationProfile;

UENUM(BlueprintType)
enum class EHeliosWeaponCategory : uint8 { Other, Rifle, Shotgun };

/** Editable description of one selectable player weapon. */
UCLASS(BlueprintType)
class HELIOSOUTPOST_API UHeliosWeaponDefinition : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon")
    EHeliosWeaponCategory CombatCategory = EHeliosWeaponCategory::Other;
    EHeliosWeaponCategory GetCombatCategory() const
    {
        if(CombatCategory!=EHeliosWeaponCategory::Other)return CombatCategory;
        if(WeaponId==TEXT("shotgun")||WeaponId==TEXT("cfm_heilonghun"))return EHeliosWeaponCategory::Shotgun;
        if(WeaponId==TEXT("rifle")||WeaponId==TEXT("cfm_chan_zeus")||WeaponId==TEXT("cfm_wangzhekui_thunder"))return EHeliosWeaponCategory::Rifle;
        return EHeliosWeaponCategory::Other;
    }
    /** Optional owner-only art; weapon balance remains on this definition. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Presentation")
    TSoftObjectPtr<UHeliosWeaponPresentationProfile> PresentationProfile;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon")
    FName WeaponId = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon")
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon", meta=(MultiLine=true))
    FText Description;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon")
    TSoftObjectPtr<UTexture2D> PreviewTexture;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon")
    TSubclassOf<AShooterWeapon> WeaponClass;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon", meta=(ClampMin="1"))
    int32 MagazineSize = 30;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon", meta=(ClampMin="0.0", Units="s"))
    float ReloadSeconds = 1.1f;

    /** Optional presentation mesh used to distinguish a truly missing imported model. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon")
    TSoftObjectPtr<UStaticMesh> WeaponMesh;

    /** Optional skeletal-only weapon, such as an independently animated hand. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon")
    TSoftObjectPtr<USkeletalMesh> SkeletalWeaponMesh;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Combat", meta=(ClampMin="0.0"))
    float Damage = 20.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Combat", meta=(ClampMin="0.0"))
    float MinimumDamage = 20.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Combat", meta=(ClampMin="0.0", Units="cm"))
    float Range = 10000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Combat", meta=(ClampMin="0.0", Units="cm"))
    float FalloffStart = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Combat", meta=(ClampMin="1"))
    int32 PelletCount = 1;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Combat", meta=(ClampMin="0.0", ClampMax="45.0", Units="Degrees"))
    float SpreadHalfAngle = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Combat", meta=(ClampMin="0.0", Units="cm/s"))
    float ProjectileSpeed = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Combat", meta=(ClampMin="0.01", Units="s"))
    float RefireSeconds = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Combat", meta=(ClampMin="1"))
    int32 EnergyPerPulse = 1;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Combat", meta=(ClampMin="0.01", Units="s"))
    float PulseSeconds = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Combat", meta=(ClampMin="0.0"))
    float Recoil = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Combat", meta=(ClampMin="0.0", Units="cm"))
    float MuzzleOffset = 10.0f;

    /** Local-space model muzzle supplied by exports/weapons/manifest.json. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Presentation", meta=(Units="cm"))
    FVector ModelMuzzleLocation = FVector(100.0f, 0.0f, 10.0f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Presentation")
    FVector AttachmentLocation = FVector(35.0f, 14.0f, -18.0f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Presentation")
    FRotator AttachmentRotation = FRotator::ZeroRotator;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Presentation")
    FVector AttachmentScale = FVector::OneVector;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Audio")
    TSoftObjectPtr<USoundBase> FireSound;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Audio")
    TSoftObjectPtr<USoundBase> ReloadSound;

    /** Rocket impact or laser loop cue, depending on weapon type. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Audio")
    TSoftObjectPtr<USoundBase> ActionSound;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Audio")
    TSoftObjectPtr<USoundBase> DrySound;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Effects")
    TSoftObjectPtr<UNiagaraSystem> MuzzleEffect;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Effects")
    TSoftObjectPtr<UNiagaraSystem> TrailEffect;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Effects")
    TSoftObjectPtr<UNiagaraSystem> ImpactEffect;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Effects")
    TSoftObjectPtr<UNiagaraSystem> EnemyImpactEffect;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Effects")
    TSoftObjectPtr<UNiagaraSystem> ExplosionEffect;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon|Effects")
    TSoftObjectPtr<UNiagaraSystem> TracerEffect;

    UFUNCTION(BlueprintPure, Category="Helios|Weapon")
    bool HasModel() const;
};
