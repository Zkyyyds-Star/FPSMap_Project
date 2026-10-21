#pragma once
#include "CoreMinimal.h"
class ACharacter;
class AAIController;
class AHeliosBioHotelGameMode;
class AActor;

/** Offline traversal policy, not a recovered original client AI algorithm. */
namespace HeliosBioPlatformTraversal
{
    struct HELIOSOUTPOST_API FTraversalRequest
    {
        FVector DestinationFeet = FVector::ZeroVector;
        TWeakObjectPtr<AActor> CombatTarget;
        FName Intent = NAME_None;
        bool bValid = false, bUrgent = false, bKeepDistance = false;
        float CombatDistance = 75.f;
    };
    struct HELIOSOUTPOST_API FStandaloneTuning
    {
        float SearchRadius = 700.f;
        float ApproachAcceptance = 45.f;
        float MaxBoardingDistance = 340.f;
        float LaunchVerticalSpeed = 560.f;
        float MaxHorizontalLaunchSpeed = 800.f;
        float RetrySeconds = 3.f;
        float ApproachTimeout = 8.f;
        float FlightTimeout = 2.5f;
        float MaxDeparturePlatformSpeed = 180.f;
        float GlobalApproachTimeout = 35.f;
        float DockWaitTimeout = 75.f;
        float RouteSearchRadius = 12000.f;
    };
    struct HELIOSOUTPOST_API FTraversalDiagnostics
    {
        bool bOwnsMovement = false, bWaitingAtDock = false;
        FVector MovementGoal = FVector::ZeroVector;
        int32 Step = INDEX_NONE, Boardings = 0, Departures = 0;
        FString Reason;
        float GroundSeconds = -1.f, RouteSeconds = -1.f, WaitRemaining = 0.f;
        float GroundWalkSeconds=0.f,StaticJumpSeconds=0.f,PhysicalWallWalkSeconds=0.f,StaticAlignmentSeconds=0.f;
        float ApproachSeconds=0.f,DepartureSeconds=0.f;
        float PlatformBoardingFlightSeconds=0.f,PlatformWaitSeconds=0.f,PlatformCarrySeconds=0.f,PlatformDepartureFlightSeconds=0.f;
        int32 GroundStaticLinks=0;
        bool bGroundTimingComplete=false;
        FString GroundTimingSegments;
        FName Intent = NAME_None;
        int32 EntryIndex = INDEX_NONE, ExitIndex = INDEX_NONE;
    };
    HELIOSOUTPOST_API FStandaloneTuning& Tuning();
    // True means traversal owns movement this tick. The caller must skip ordinary chase MoveTo.
    HELIOSOUTPOST_API bool TickTraversal(ACharacter* Bot,AAIController* AI,AHeliosBioHotelGameMode* Mode,AActor* Target,float Dt);
    HELIOSOUTPOST_API bool TickTraversal(ACharacter* Bot,AAIController* AI,AHeliosBioHotelGameMode* Mode,const FTraversalRequest& Request,float Dt);
    HELIOSOUTPOST_API bool IsInFlight(const ACharacter* Bot);
    HELIOSOUTPOST_API void ForgetBot(ACharacter* Bot);
    HELIOSOUTPOST_API FString DebugState(const ACharacter* Bot);
    HELIOSOUTPOST_API FTraversalDiagnostics GetDiagnostics(const ACharacter* Bot);
}
