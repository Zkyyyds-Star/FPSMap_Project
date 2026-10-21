#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "HeliosEnemyExtendedQA.generated.h"

class AAIController;
class AHeliosArenaCharacter;
class AHeliosEnemyActor;
class AHeliosEnemyTestDirector;
class APlayerController;
class UPrimitiveComponent;
struct FAIRequestID;
struct FPathFollowingResult;

/** Dedicated long-form performance, capture, and route QA. Spawned only by opt-in GameMode flags. */
UCLASS()
class HELIOSOUTPOST_API AHeliosEnemyExtendedQA : public AActor
{
    GENERATED_BODY()

public:
    AHeliosEnemyExtendedQA();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaSeconds) override;

private:
    struct FRouteDefinition
    {
        FString Id;
        TArray<FVector> Waypoints;
    };

    struct FEnemyMeasurementBaseline
    {
        int32 AttackNotifies = 0;
        int32 AttacksCommitted = 0;
    };

    bool ResolveActors();
    FVector Grounded(const FVector& Desired, AActor* Ignore) const;
    void AimAt(AHeliosEnemyActor* Target);
    void SendFire(EInputEvent Event);
    void ReleaseFire();
    void PlacePerformanceCombatants();
    void PlaceCaptureCombatants();

    void StartPerformance(int32 EnemyCount);
    void TickPerformance();
    void FinishPerformance();
    void StartCapture();
    void TickCapture();
    void BeginCaptureType(int32 TypeIndex);
    void UpdateCaptureObservations();
    void FinishCaptureType();
    void RequestCaptureFrame();
    void FinishCapture();

    bool LoadRoutes();
    void StartNavigation();
    void TickNavigation(float DeltaSeconds);
    void StartNavigationCase();
    void IssueNavigationWaypoint();
    void CompleteNavigationWaypoint(bool bReached, const FString& Reason);
    void CompleteNavigationCase();
    void OnNavigationMoveFinished(FAIRequestID RequestId, const FPathFollowingResult& Result);
    void RestoreNavigationObserver();

    UFUNCTION()
    void OnNavigationCollision(UPrimitiveComponent* HitComponent, AActor* OtherActor,
        UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);

    void CollectEnemyCounters(int32& OutAlive, int32& OutAttackers,
        int32& OutNotifies, int32& OutCommitted) const;
    TSharedPtr<class FJsonObject> MakeEnvironmentJson() const;
    void SaveAndFinish(const FString& RuntimeStatus);
    void WriteReport();

    FString Mode;
    FString RootDir;
    FString ReportPath;
    TSharedPtr<class FJsonObject> Report;
    TWeakObjectPtr<AHeliosEnemyTestDirector> Director;
    TWeakObjectPtr<AHeliosArenaCharacter> Player;
    TWeakObjectPtr<APlayerController> PlayerController;

    double StartWallSeconds = 0.0;
    double ModeStartWallSeconds = 0.0;
    double SampleStartWallSeconds = 0.0;
    double PreviousFrameWallSeconds = 0.0;
    float ResolveWaitSeconds = 0.0f;
    bool bStarted = false;
    bool bFinished = false;
    bool bFireHeld = false;

    int32 RequestedEnemyCount = 0;
    TArray<double> PerformanceFrameSeconds;
    int32 PerformanceStartAlive = 0;
    int32 PerformanceStartAttackers = 0;
    int32 PerformanceStartNotifies = 0;
    int32 PerformanceStartCommitted = 0;
    TMap<FString, FEnemyMeasurementBaseline> PerformanceEnemyBaselines;
    TMap<FString, FVector> PerformanceInitialLocations;
    bool bPerformanceSightStabilizing = false;
    double PerformanceSightStabilizationStartWallSeconds = 0.0;
    double PerformanceSightStabilizationEndWallSeconds = 0.0;

    int32 CaptureType = 0;
    int32 CaptureFrameIndex = 0;
    int32 CaptureFramesRequested = 0;
    int32 CaptureFramesCompleted = 0;
    double CaptureNextFrameWallSeconds = 0.0;
    double CaptureOutstandingSince = 0.0;
    FString CaptureOutstandingPath;
    int32 CaptureOutstandingFrameIndex = INDEX_NONE;
    int32 CaptureOutstandingType = 0;
    double CaptureOutstandingRequestedSeconds = 0.0;
    bool bCaptureOutstandingPlayerDamaged = false;
    bool bCaptureOutstandingReturnFire = false;
    bool bCaptureOutstandingEnemyDead = false;
    float CaptureOutstandingPlayerHealth = 0.0f;
    int32 CaptureOutstandingPlayerAmmo = 0;
    float CaptureOutstandingEnemyHealth = 0.0f;
    int32 CaptureOutstandingAttackNotifies = 0;
    int32 CaptureOutstandingAttacksCommitted = 0;
    TArray<double> CaptureCompletionTimes;
    TArray<TSharedPtr<class FJsonValue>> CaptureCompletedFramesJson;
    TArray<TSharedPtr<class FJsonValue>> CaptureTypesJson;
    TArray<int32> CaptureCompletedFramesByType;
    TArray<bool> CaptureDamageFrameObservedByType;
    TArray<bool> CaptureReturnFireFrameObservedByType;
    TArray<bool> CaptureDeathFrameObservedByType;
    bool bCaptureEnding = false;
    int32 CaptureFailures = 0;
    int32 CaptureCombatFailures = 0;
    FString CaptureTypeId;
    float CapturePlayerStartHealth = 100.0f;
    float CapturePlayerMinHealth = 100.0f;
    int32 CapturePlayerMinAmmo = 30;
    float CaptureEnemyStartHealth = 0.0f;
    float CaptureEnemyMinHealth = 0.0f;
    int32 CaptureAttackNotifiesStart = 0;
    int32 CaptureAttacksCommittedStart = 0;
    int32 CaptureMaxAttackNotifies = 0;
    int32 CaptureMaxAttacksCommitted = 0;
    bool bCaptureSawEnemy = false;
    bool bCaptureEnemyDeathObserved = false;
    bool bCapturePlayerDeathObserved = false;

    TArray<FRouteDefinition> Routes;
    int32 NavigationTypeIndex = 1;
    int32 NavigationLastTypeIndex = 4;
    int32 NavigationRouteIndex = 0;
    int32 NavigationWaypointIndex = 0;
    int32 NavigationCollisionCount = 0;
    FString NavigationFirstCollisionActor;
    FString NavigationFirstCollisionOtherComponent;
    FString NavigationFirstCollisionOwnComponent;
    FVector NavigationFirstCollisionImpactPoint = FVector::ZeroVector;
    bool bNavigationFirstCollisionRecorded = false;
    bool bNavigationPlayerCollisionOverrideActive = false;
    bool bNavigationPlayerCollisionWasEnabled = true;
    float NavigationStuckSeconds = 0.0f;
    double NavigationWaypointStart = 0.0;
    double NavigationLastSample = 0.0;
    double NavigationWaypointTimeoutSeconds = 20.0;
    double NavigationPlannedPathLengthCm = 0.0;
    float NavigationRequestAcceptanceCm = 75.0f;
    bool bNavigationPathLengthFromFollowedPath = false;
    uint32 NavigationIssuedRequestId = MAX_uint32;
    int32 NavigationPathPointCountAtIssue = 0;
    bool bNavigationPathWasPartialAtIssue = false;
    bool bNavigationPathEndAtIssueValid = false;
    bool bNavigationMoveCompletionObserved = false;
    FString NavigationMoveCompletionResult;
    FVector NavigationPathEndAtIssue = FVector::ZeroVector;
    FDelegateHandle NavigationMoveFinishedHandle;
    FVector NavigationLastLocation = FVector::ZeroVector;
    FVector NavigationWaypointTarget = FVector::ZeroVector;
    FVector NavigationMoveGoal = FVector::ZeroVector;
    TWeakObjectPtr<AHeliosEnemyActor> NavigationEnemy;
    TWeakObjectPtr<AAIController> NavigationController;
    TArray<TSharedPtr<class FJsonValue>> NavigationCasesJson;
    TArray<TSharedPtr<class FJsonValue>> NavigationWaypointsJson;
    TArray<TSharedPtr<class FJsonValue>> NavigationActualPathJson;
    bool bNavigationCasePreparing = false;
    int32 NavigationCaseFailedWaypoints = 0;
    int32 NavigationTotalFailedWaypoints = 0;
};
