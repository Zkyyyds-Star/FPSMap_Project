#pragma once
#include "CoreMinimal.h"

// Explicit replay policy; not a decoded source LoopType enum.
enum class EHeliosBioPlatformReplay : uint8 { Once, PingPong, Wrap };

namespace HeliosBioPlatformSchedule
{
    struct FSnapshot
    {
        TArray<FVector> Points;
        FVector Position=FVector::ZeroVector;
        float Speed=0.f,Pause=0.f,Wait=0.f;
        int32 TargetIndex=1,Direction=1;
        EHeliosBioPlatformReplay Replay=EHeliosBioPlatformReplay::Once;
        bool bRunning=false;
    };
    HELIOSOUTPOST_API float CycleSeconds(const FSnapshot& Route);
    HELIOSOUTPOST_API bool ShouldUsePlatform(float GroundSeconds,float PlatformSeconds);
    HELIOSOUTPOST_API bool Predict(const FSnapshot& Route,int32 BoardIndex,int32 ExitIndex,
        float EarliestBoardSeconds,float& BoardSeconds,float& ExitSeconds);
}
