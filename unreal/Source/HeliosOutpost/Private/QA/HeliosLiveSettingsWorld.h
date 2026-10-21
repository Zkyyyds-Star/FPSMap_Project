#pragma once
#include "CoreMinimal.h"
#include "Engine/World.h"
#include "EngineUtils.h"

// QA-only adapter: run the existing actor assertions in the loaded map/session.
// Only actors created by these synchronous checks are cleaned up.
struct FHeliosLiveSettingsCleanup
{
    UWorld* World;
    TSet<AActor*> Existing;
    explicit FHeliosLiveSettingsCleanup(UWorld* In):World(In)
    {if(World)for(TActorIterator<AActor> It(World);It;++It)Existing.Add(*It);}
    ~FHeliosLiveSettingsCleanup()
    {if(World)for(TActorIterator<AActor> It(World);It;++It)if(!Existing.Contains(*It))It->Destroy();}
};
#if WITH_DEV_AUTOMATION_TESTS
bool HeliosCheckLiveDamage(UWorld* World);
bool HeliosCheckLiveHealth(UWorld* World);
#endif
