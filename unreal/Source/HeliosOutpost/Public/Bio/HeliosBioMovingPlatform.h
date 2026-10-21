#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Bio/HeliosBioPlatformSchedule.h"
#include "HeliosBioMovingPlatform.generated.h"
class UStaticMesh;
class UStaticMeshComponent;

UCLASS()
class HELIOSOUTPOST_API AHeliosBioMovingPlatform : public AActor
{
    GENERATED_BODY()
public:
    AHeliosBioMovingPlatform();
    virtual void Tick(float DeltaSeconds) override;
    // Caller converts original world points and speed into UE centimetres, preserving source timing.
    bool ConfigureSourceRoute(UStaticMesh* SourceMeshAsset, const TArray<FVector>& WorldPointsCm,
        float SpeedCmPerSecond, float PauseSeconds, int32 OriginalLoopType,
        EHeliosBioPlatformReplay ReplayPolicy);
    void SetRouteRunning(bool bRunning);
    void ResetSourceRoute(bool bRun=false);
    UStaticMeshComponent* GetPlatformMesh() const { return PlatformMesh; }
    int32 GetOriginalLoopType() const { return SourceLoopType; }
    bool IsRouteConfigured() const { return bConfigured; }
    const TArray<FVector>& GetRoutePoints() const { return RoutePoints; }
    bool IsRouteRunning() const { return bRouteRunning; }
    float GetRouteCycleSeconds() const;
    // Read-only prediction, including current direction and endpoint pauses.
    bool PredictRouteJourney(int32 BoardIndex,int32 ExitIndex,float EarliestBoardSeconds,
        float& BoardSeconds,float& ExitSeconds) const;
private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> PlatformMesh;
    TArray<FVector> RoutePoints;
    float TravelSpeed = 0.f;
    float EndpointPause = 0.f;
    float WaitRemaining = 0.f;
    int32 TargetIndex = 1;
    int32 Direction = 1;
    int32 SourceLoopType = INDEX_NONE;
    EHeliosBioPlatformReplay Replay = EHeliosBioPlatformReplay::Once;
    bool bConfigured = false;
    bool bRouteRunning = false;
    HeliosBioPlatformSchedule::FSnapshot GetScheduleSnapshot() const;
    void AdvanceTarget();
};
