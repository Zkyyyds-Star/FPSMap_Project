#include "Core/HeliosGameMode.h"
#include "Audio/HeliosMusicSubsystem.h"
#include "Engine/GameInstance.h"
#include "Town/HeliosTownSubsystem.h"
#include "Player/HeliosCharacter.h"
#include "UI/HeliosHUD.h"
#include "World/HeliosNavBounds.h"
#include "HeliosOutpost.h"
#include "Player/HeliosPlayerController.h"
#include "QA/HeliosQARunner.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"

namespace
{
const TCHAR* HeliosRuntimeGenerationName(ERuntimeGenerationType Mode)
{
    switch (Mode)
    {
    case ERuntimeGenerationType::Static:
        return TEXT("Static");
    case ERuntimeGenerationType::DynamicModifiersOnly:
        return TEXT("DynamicModifiersOnly");
    case ERuntimeGenerationType::Dynamic:
        return TEXT("Dynamic");
    case ERuntimeGenerationType::LegacyGeneration:
        return TEXT("LegacyGeneration");
    default:
        return TEXT("Unknown");
    }
}

void LogHeliosNavData(const TCHAR* Phase, ANavigationData* NavData)
{
    if (!NavData)
    {
        UE_LOG(LogHeliosOutpost, Warning, TEXT("HELIOS_NAV phase=%s nav_data=missing"), Phase);
        return;
    }

    if (ARecastNavMesh* Recast = Cast<ARecastNavMesh>(NavData))
    {
        const FBox NavMeshBounds = Recast->GetNavMeshBounds();
        UE_LOG(LogHeliosOutpost, Display,
            TEXT("HELIOS_NAV phase=%s nav_data=%s runtime_generation=%s supports_runtime=%s needs_rebuild=%s active_tiles=%d tile_capacity=%d bounds_valid=%s bounds_center_cm=%s bounds_extent_cm=%s"),
            Phase,
            *Recast->GetPathName(),
            HeliosRuntimeGenerationName(Recast->GetRuntimeGenerationMode()),
            Recast->SupportsRuntimeGeneration() ? TEXT("true") : TEXT("false"),
            Recast->NeedsRebuild() ? TEXT("true") : TEXT("false"),
            Recast->GetNumActiveTiles(),
            Recast->GetNavMeshTilesCount(),
            NavMeshBounds.IsValid ? TEXT("true") : TEXT("false"),
            *NavMeshBounds.GetCenter().ToString(),
            *NavMeshBounds.GetExtent().ToString());
    }
    else
    {
        UE_LOG(LogHeliosOutpost, Display,
            TEXT("HELIOS_NAV phase=%s nav_data=%s class=%s runtime_generation=%s supports_runtime=%s"),
            Phase,
            *NavData->GetPathName(),
            *NavData->GetClass()->GetPathName(),
            HeliosRuntimeGenerationName(NavData->GetRuntimeGenerationMode()),
            NavData->SupportsRuntimeGeneration() ? TEXT("true") : TEXT("false"));
    }
}
}

AHeliosGameMode::AHeliosGameMode()
{
    DefaultPawnClass = AHeliosCharacter::StaticClass();
    PlayerControllerClass = AHeliosPlayerController::StaticClass();
    HUDClass = AHeliosHUD::StaticClass();
}

void AHeliosGameMode::StartPlay()
{
    Super::StartPlay();

    if (UGameInstance* Instance = GetGameInstance())
        if (UHeliosMusicSubsystem* Music = Instance->GetSubsystem<UHeliosMusicSubsystem>())
            Music->EnsureStarted();

    RebuildRuntimeNavigation();

    FString QAMode;
    const bool bRunQA = FParse::Param(FCommandLine::Get(), TEXT("HeliosQA"))
        || FParse::Value(FCommandLine::Get(), TEXT("HeliosQA="), QAMode);
    if (bRunQA && GetWorld())
    {
        GetWorld()->SpawnActor<AHeliosQARunner>();
    }
}

void AHeliosGameMode::RebuildRuntimeNavigation()
{
    UWorld* World = GetWorld();
    if (UHeliosTownSubsystem::IsTownMap(World))
    {
        UE_LOG(LogHeliosOutpost, Display, TEXT("IKUN_TOWN navigation uses local invokers (120m/160m); global Build skipped"));
        return;
    }
    UNavigationSystemV1* NavigationSystem = World
        ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)
        : nullptr;
    if (!NavigationSystem)
    {
        UE_LOG(LogHeliosOutpost, Error, TEXT("HELIOS_NAV runtime_rebuild=skipped reason=navigation_system_missing"));
        return;
    }

    int32 BoundsCount = 0;
    for (TActorIterator<AHeliosNavBounds> It(World); It; ++It)
    {
        const FBox Bounds = It->GetComponentsBoundingBox(true);
        UE_LOG(LogHeliosOutpost, Display,
            TEXT("HELIOS_NAV volume=%s valid=%s center_cm=%s extent_cm=%s min_cm=%s max_cm=%s"),
            *It->GetPathName(),
            Bounds.IsValid ? TEXT("true") : TEXT("false"),
            *Bounds.GetCenter().ToString(),
            *Bounds.GetExtent().ToString(),
            *Bounds.Min.ToString(),
            *Bounds.Max.ToString());
        NavigationSystem->OnNavigationBoundsUpdated(*It);
        ++BoundsCount;
    }

    UE_LOG(LogHeliosOutpost, Display, TEXT("HELIOS_NAV runtime_rebuild=start bounds_count=%d"), BoundsCount);
    LogHeliosNavData(TEXT("before_build"),
        NavigationSystem->GetDefaultNavDataInstance(FNavigationSystem::DontCreate));

    NavigationSystem->Build();

    LogHeliosNavData(TEXT("after_build"),
        NavigationSystem->GetDefaultNavDataInstance(FNavigationSystem::DontCreate));
    UE_LOG(LogHeliosOutpost, Display, TEXT("HELIOS_NAV runtime_rebuild=complete bounds_count=%d"), BoundsCount);
}
