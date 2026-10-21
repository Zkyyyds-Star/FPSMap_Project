#pragma once
#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"
class UWorld;

/** Runtime map configuration shared by travel, menus and automation metadata. */
struct HELIOSOUTPOST_API FHeliosMapDefinition
{
    FName Id;
    FText DisplayName;
    TSoftObjectPtr<UWorld> Level;
    int32 DistrictCount;
    FName GameplayRules;
    FName NavigationScope;
    float NavigationRadiusCm;
    FString TownConfigPath;
    FString SavePrefix;
    FName PresentationProfile;
    bool UsesTownSystems() const { return !TownConfigPath.IsEmpty(); }
    FName AssetMapName() const { return FName(*Level.ToSoftObjectPath().GetAssetName()); }
};

class HELIOSOUTPOST_API FHeliosMapRegistry
{
public:
    static const TArray<FHeliosMapDefinition>& All();
    static const FHeliosMapDefinition* Find(FName Id);
    static const FHeliosMapDefinition* ForWorld(const UWorld* World);
};
