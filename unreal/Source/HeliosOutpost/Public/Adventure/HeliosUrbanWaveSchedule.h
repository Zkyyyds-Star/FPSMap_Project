#pragma once
#include "CoreMinimal.h"

/** Validated local configuration, exclusively used by UrbanChallenge worlds. */
struct FHeliosUrbanWave
{
    int32 Total = 15;
    int32 AliveCap = 6;
    int32 Midbosses = 0;
    int32 TypeWeights[4] = {20,65,5,10};
    int32 RifleCap = 2;
    int32 SkirmisherCap = 1;
    float SpawnSeconds = 1.5f;
    float RestSeconds = 8.f;
};

struct HELIOSOUTPOST_API FHeliosUrbanWaveSchedule
{
    TArray<FHeliosUrbanWave> Waves;
    bool Load(FString& Error);
};
