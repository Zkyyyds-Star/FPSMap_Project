#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HeliosCharacterActionProfile.generated.h"
class USkeletalMesh;
class UAnimSequence;
class USoundBase;

/** Original character-only actions use their own motion rig, independent of a weapon. */
USTRUCT(BlueprintType)
struct HELIOSOUTPOST_API FHeliosCharacterGesture
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName GestureId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USkeletalMesh> FirstPersonMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimSequence> FirstPersonClip;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimSequence> ThirdPersonClip;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USoundBase> Sound;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float SoundTime = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float ThirdPersonSoundTime = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FTransform FirstPersonTransform = FTransform(FRotator(0,-90,0));
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float FirstPersonViewScale = .2f;
};

/** Every mesh is genuinely rebound to its weapon motion rig; no foreign-skeleton playback. */
USTRUCT(BlueprintType)
struct HELIOSOUTPOST_API FHeliosCharacterWeaponPresentation
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName WeaponId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USkeletalMesh> FirstPersonMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USkeletalMesh> FirstPersonWeaponMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USkeletalMesh> ThirdPersonMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TMap<FName,TSoftObjectPtr<UAnimSequence>> FirstPersonClips;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TMap<FName,TSoftObjectPtr<UAnimSequence>> ThirdPersonClips;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TMap<FName,FName> Anchors;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FTransform WeaponAttachment;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USkeletalMesh> ThirdPersonWeaponMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bGeneratedAndValidated = false;
};
UCLASS(BlueprintType)
class HELIOSOUTPOST_API UHeliosCharacterActionProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName CharacterId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName CharacterVariantId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FHeliosCharacterWeaponPresentation> Weapons;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FHeliosCharacterGesture> Gestures;
    const FHeliosCharacterGesture* FindGesture(FName Id) const
    {for(const auto& G:Gestures)if(G.GestureId==Id)return &G;return nullptr;}
    const FHeliosCharacterWeaponPresentation* FindWeapon(FName Id) const
    {for(const auto& W:Weapons)if(W.WeaponId==Id)return &W;return nullptr;}
};
