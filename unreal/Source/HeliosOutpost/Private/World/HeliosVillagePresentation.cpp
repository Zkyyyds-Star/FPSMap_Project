#include "World/HeliosVillagePresentation.h"
#include "World/HeliosMapDefinition.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"

void UHeliosVillagePresentation::OnWorldBeginPlay(UWorld& World)
{
    Super::OnWorldBeginPlay(World);
    const auto* Map=FHeliosMapRegistry::ForWorld(&World);
    if(!World.IsGameWorld() || !Map || Map->PresentationProfile!=TEXT("ghost_village"))return;
    const TPair<const TCHAR*,const TCHAR*> Settings[]={
        {TEXT("sg.ViewDistanceQuality"),TEXT("2")},{TEXT("sg.AntiAliasingQuality"),TEXT("3")},
        {TEXT("sg.ShadowQuality"),TEXT("2")},{TEXT("sg.GlobalIlluminationQuality"),TEXT("2")},
        {TEXT("sg.ReflectionQuality"),TEXT("2")},{TEXT("sg.TextureQuality"),TEXT("3")},
        {TEXT("sg.EffectsQuality"),TEXT("2")},{TEXT("sg.PostProcessQuality"),TEXT("2")},
        {TEXT("r.AntiAliasingMethod"),TEXT("4")},{TEXT("r.ScreenPercentage"),TEXT("85")},
        {TEXT("r.Lumen.HardwareRayTracing"),TEXT("0")},
        {TEXT("r.Streaming.PoolSize"),TEXT("2200")},{TEXT("r.VolumetricFog.GridPixelSize"),TEXT("16")}
    };
    for(const auto& Setting:Settings)
        if(auto* Variable=IConsoleManager::Get().FindConsoleVariable(Setting.Key))
        {
            PreviousValues.Add(Setting.Key,Variable->GetString());
            Variable->Set(Setting.Value,ECVF_SetByCode);
        }
}
void UHeliosVillagePresentation::Deinitialize()
{
    for(const auto& Setting:PreviousValues)
        if(auto* Variable=IConsoleManager::Get().FindConsoleVariable(*Setting.Key))
            Variable->Set(*Setting.Value,ECVF_SetByCode);
    PreviousValues.Empty();
    Super::Deinitialize();
}
