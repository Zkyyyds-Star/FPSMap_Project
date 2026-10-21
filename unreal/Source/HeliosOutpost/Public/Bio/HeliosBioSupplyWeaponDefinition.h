#pragma once
#include "CoreMinimal.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "HeliosBioSupplyWeaponDefinition.generated.h"
class UAnimSequence;

UENUM(BlueprintType)
enum class EHeliosBioSupplyFireType : uint8 { Hitscan,ExplosiveProjectile,ConcussionProjectile };

USTRUCT(BlueprintType)
struct FHeliosBioSupplyAudioCue
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FName Action;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) bool bFirstPerson=true;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float NativeSeconds=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) TSoftObjectPtr<USoundBase> Sound;
};

/** Original BIO-only item identity, separate from persistent selectable weapons. */
UCLASS(BlueprintType)
class HELIOSOUTPOST_API UHeliosBioSupplyWeaponDefinition : public UHeliosWeaponDefinition
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Source") int32 OriginalItemId=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Source") FString SourceEvidence;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Source") bool bSourceIdentityVerified=false;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Source") bool bPresentationVerified=false;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Source") bool bGameplayVerified=false;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Supply") int32 PickupReserveAmmo=6;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Supply") int32 MaximumReserveAmmo=30;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Supply") bool bThrowable=false;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Supply") TSoftObjectPtr<UStaticMesh> SupplyPreviewMesh;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Source") TSoftObjectPtr<USkeletalMesh> NativeFirstPersonMesh;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Source") TMap<FName,TSoftObjectPtr<UAnimSequence>> NativeFirstPersonClips;
    // Native controller slots without a TP motion keep the original idle pose.
    // This policy records absence, rather than inventing an equip/reload clip.
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Source") TSet<FName> ThirdPersonHoldIdleActions;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Source") FTransform NativeFirstPersonTransform;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Source") FName NativeMuzzleSocket=TEXT("MuzzleFlashSocket");
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Source") FName ThirdPersonMuzzleBone=TEXT("b_RightHand");
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Source") FTransform ThirdPersonMuzzleRelativeToBone;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Source") bool bMuzzleBridgeVerified=false;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Source") TSoftObjectPtr<UStaticMesh> NativeProjectileMesh;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Source") FString EffectLibrary;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Source") FString MuzzleEffectName;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Source") FString ThirdPersonMuzzleEffectName;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Source") FString TrailEffectName;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Source") FString ImpactEffectName;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Source") TSoftObjectPtr<USoundBase> ThirdPersonFireSound;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Source") TMap<FName,TSoftObjectPtr<USoundBase>> SourceActionSounds;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Source") TArray<FHeliosBioSupplyAudioCue> SourceAudioCues;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Source") TSoftObjectPtr<UMaterialInterface> NativeTracerMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Source") FString NativeTracerRecipe;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Combat") EHeliosBioSupplyFireType FireType=EHeliosBioSupplyFireType::Hitscan;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Combat") float ExplosionRadius=450.f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Combat") float ProjectileGravityScale=0.f;
    // Centimetres per second and centimetres per second squared, respectively.
    // Zero max speed retains launch speed; zero acceleration preserves ordinary ballistic flight.
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Combat") float MaximumProjectileSpeed=0.f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Combat") float ProjectileAcceleration=0.f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Combat") float ProjectileLifetime=5.f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Combat") float ConcussionSeconds=3.f;
    // A local release clock is separate from evidence of the original client event.
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Combat") float ThrowReleaseSeconds=1.f/30.f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Source") FString ThrowReleaseScope=TEXT("Explicit offline release time; original client throw event unverified");
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Bio|Combat") FString BalanceScope=TEXT("Explicit offline tuning until verified against the original weapon configuration");
    static int32 SlotForItem(int32 ItemId);
    static int32 ItemForSlot(int32 Slot);
    static UHeliosBioSupplyWeaponDefinition* LoadForSlot(int32 Slot);
    static FString LabelForSlot(int32 Slot);
    bool IsReady() const;
};
