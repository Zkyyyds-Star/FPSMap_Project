#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HeliosWeaponRegistry.generated.h"

class UHeliosWeaponDefinition;

/** Stable weapon catalogue. A saved registry asset is optional; native defaults always work. */
UCLASS(BlueprintType)
class HELIOSOUTPOST_API UHeliosWeaponRegistry : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Helios|Weapon")
    TArray<TSoftObjectPtr<UHeliosWeaponDefinition>> RegisteredDefinitions;

    UFUNCTION(BlueprintPure, Category="Helios|Weapon")
    static TArray<UHeliosWeaponDefinition*> GetAvailableWeapons();

    UFUNCTION(BlueprintPure, Category="Helios|Weapon")
    static UHeliosWeaponDefinition* FindWeapon(FName WeaponId);

    static bool IsRetiredWeapon(FName WeaponId);
};
