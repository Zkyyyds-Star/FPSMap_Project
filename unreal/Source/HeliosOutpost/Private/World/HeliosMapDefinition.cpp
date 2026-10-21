#include "World/HeliosMapDefinition.h"
#include "Engine/World.h"

const TArray<FHeliosMapDefinition>& FHeliosMapRegistry::All()
{
    static const TArray<FHeliosMapDefinition> Definitions={
        {TEXT("ikun_town"),FText::FromString(TEXT("IKUN 小镇")),TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Helios/Maps/IkunTown.IkunTown"))),8,TEXT("survival"),TEXT("local_loaded"),12000.f,TEXT("IkunTown/Data/town.json"),TEXT("IkunTown"),TEXT("town")},
        {TEXT("ikun_ghost_village"),FText::FromString(TEXT("ikun诡村")),TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Helios/Maps/IkunGhostVillage.IkunGhostVillage"))),8,TEXT("survival"),TEXT("local_loaded"),12000.f,TEXT("IkunGhostVillage/Data/village.json"),TEXT("IkunGhostVillage"),TEXT("ghost_village")},
        {TEXT("HeliosAdventure"),FText::FromString(TEXT("前哨站探险")),TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Helios/Maps/HeliosAdventure.HeliosAdventure"))),7,TEXT("survival"),TEXT("whole_map"),0.f},
        {TEXT("urban_challenge"),FText::FromString(TEXT("都市断层 · 开发预览")),TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Helios/Maps/UrbanChallenge.UrbanChallenge"))),3,TEXT("challenge_30"),TEXT("whole_map"),0.f,FString(),TEXT("UrbanChallenge"),TEXT("urban_challenge")},
        {TEXT("cfm_biopyramid"),FText::FromString(TEXT("新生化金字塔（30人版本）")),TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Helios/Maps/CFMBioPyramid.CFMBioPyramid"))),1,TEXT("bio_infection"),TEXT("whole_map"),0.f,FString(),TEXT("CFMBioPyramid"),TEXT("cfm_biopyramid")},
        {TEXT("cfm_biopyramid_floating"),FText::FromString(TEXT("新生化金字塔 · 浮空平台")),TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Helios/Maps/CFMBioPyramidFloating.CFMBioPyramidFloating"))),1,TEXT("bio_infection"),TEXT("whole_map"),0.f,FString(),TEXT("CFMBioPyramidFloating"),TEXT("cfm_biopyramid_floating")}
    };
    return Definitions;
}
const FHeliosMapDefinition* FHeliosMapRegistry::Find(FName Id)
{
    for(const auto& Definition:All())if(Definition.Id==Id || Definition.AssetMapName()==Id)return &Definition;
    return nullptr;
}
const FHeliosMapDefinition* FHeliosMapRegistry::ForWorld(const UWorld* World)
{
    if(!World)return nullptr;
    const FString MapName=World->GetMapName();
    for(const auto& Definition:All())if(MapName.EndsWith(Definition.AssetMapName().ToString()))return &Definition;
    return nullptr;
}
