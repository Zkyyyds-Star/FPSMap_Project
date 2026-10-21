#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Player/HeliosLoadout.h"
#include "HeliosMapTravelSubsystem.generated.h"

/** Keeps the selected equipment across map travel; the destination HUD starts only after readiness. */
UCLASS()
class HELIOSOUTPOST_API UHeliosMapTravelSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    bool RequestTravel(FName MapId, FName WeaponId);
    bool RequestTravelWithLoadout(FName MapId, const FHeliosLoadout& Loadout);
    FHeliosLoadout PendingLoadout;
    FName PendingWeaponId = TEXT("cfm_chan_zeus");
    FName DestinationMap;
    bool bPendingStart = false;
    bool bResetTownOnArrival = false;
};
