#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HeliosCharacterDefinition.generated.h"

class USkeletalMesh;
class UTexture2D;
class UHeliosCharacterActionProfile;
class UHeliosGuanXiaoYuUmbrellaProfile;

USTRUCT(BlueprintType)
struct HELIOSOUTPOST_API FHeliosCharacterFormInfo
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName VariantId = NAME_None;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UTexture2D> PreviewTexture;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UHeliosCharacterActionProfile> ActionProfile;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UHeliosGuanXiaoYuUmbrellaProfile> UmbrellaProfile;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bPresentationReady = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USkeletalMesh> FirstPersonArmsMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USkeletalMesh> ThirdPersonMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FTransform FirstPersonTransform = FTransform(FRotator(0,-90,0));
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float ThirdPersonScale = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float MovementMultiplier = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName VoiceSourceId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString VoicePrefix;
};

UCLASS(BlueprintType)
class HELIOSOUTPOST_API UHeliosCharacterDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName CharacterId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName DefaultVariantId = NAME_None;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FText Description;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UTexture2D> PreviewTexture;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USkeletalMesh> FirstPersonArmsMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USkeletalMesh> ThirdPersonMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FHeliosCharacterFormInfo> Forms;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UHeliosCharacterActionProfile> ActionProfile;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName VoiceSourceId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName AnimationSourceId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString FirstPersonAnimationPrefix;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bPresentationReady = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bPlayerSelectable = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bCompanionSelectable = true;
    const FHeliosCharacterFormInfo* FindForm(FName VariantId) const;
};

struct HELIOSOUTPOST_API FHeliosCharacterRegistry
{
    static TArray<UHeliosCharacterDefinition*> GetAvailableCharacters(bool bIncludeDeferred = true);
    static UHeliosCharacterDefinition* FindCharacter(FName CharacterId);
    static FName CanonicalCharacterId(FName CharacterId);
};
