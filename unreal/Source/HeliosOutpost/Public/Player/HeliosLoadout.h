#pragma once

#include "CoreMinimal.h"
#include "HeliosLoadout.generated.h"

class FJsonObject;
class UGameInstance;

/** Character identity is independent of the equipped weapon. Reserved slots stay empty in v1. */
USTRUCT(BlueprintType)
struct HELIOSOUTPOST_API FHeliosLoadout
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName CharacterId = TEXT("cfm_chan");
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName CharacterVariantId = NAME_None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName PrimaryWeaponId = TEXT("cfm_chan_zeus");
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName SecondaryWeaponId = NAME_None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName MeleeWeaponId = NAME_None;

    static FHeliosLoadout MakeDefault();
    static FHeliosLoadout FromLegacyWeapon(FName WeaponId);
    static FHeliosLoadout Normalize(const FHeliosLoadout& Loadout);
    static FHeliosLoadout ResolveLegacyVariant(const FHeliosLoadout& Loadout, UGameInstance* GameInstance, int32 CompanionSlot = INDEX_NONE);
    static bool Validate(const FHeliosLoadout& Loadout, FString& OutError);
    bool operator==(const FHeliosLoadout& Other) const;
    bool operator!=(const FHeliosLoadout& Other) const { return !(*this == Other); }
};

namespace HeliosLoadout
{
    HELIOSOUTPOST_API bool ReadJson(const TSharedPtr<FJsonObject>& Json, FHeliosLoadout& OutLoadout);
    HELIOSOUTPOST_API void WriteJson(const TSharedRef<FJsonObject>& Json, const FHeliosLoadout& Loadout);
}
