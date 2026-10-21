#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HeliosWeaponPresentationProfile.generated.h"

class USkeletalMesh;
class UAnimSequence;
class UNiagaraSystem;
class USoundBase;
class UStaticMesh;
class UMaterialInterface;

/** First-person art only. Combat and reload timing remain in the weapon definition. */
UCLASS(BlueprintType)
class HELIOSOUTPOST_API UHeliosWeaponPresentationProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    /** Compact rig carries the weapon in its right-hand animation; do not move the shoulders again. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bBakedReloadPose = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.01")) float FireAnimationSeconds = .18f;
    /** Thrown weapons render their own visible mesh and impact; do not hide them like bullets. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bPreserveProjectileVisual = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bMuzzleLightEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USkeletalMesh> ArmsMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USkeletalMesh> WeaponMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimSequence> ArmsIdle;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimSequence> ArmsReload;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimSequence> ArmsFire;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimSequence> ArmsEquip;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimSequence> WeaponReload;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimSequence> WeaponFire;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector ViewLocation = FVector(28, 11, -20);
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FRotator ViewRotation = FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName GripSocket = TEXT("Grip_R");
    /** Authored tilted idle poses must not be cancelled by the equip-time socket correction. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bAttachAtReferencePose = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName MuzzleSocket = TEXT("Muzzle");
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName EjectSocket = TEXT("Eject");
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UNiagaraSystem> MuzzleEffect;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UNiagaraSystem> ImpactEffect;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UNiagaraSystem> TrailEffect;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UNiagaraSystem> ExplosionEffect;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UMaterialInterface> BeamCoreMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UMaterialInterface> BeamHaloMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UStaticMesh> CasingMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UMaterialInterface> CasingMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USoundBase> FireSound;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USoundBase> MagazineOutSound;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USoundBase> MagazineInSound;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USoundBase> ActionSound;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float VisualKick = 1.0f;
};
