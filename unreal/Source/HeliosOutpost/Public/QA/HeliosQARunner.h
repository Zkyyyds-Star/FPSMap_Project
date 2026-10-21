#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosQARunner.generated.h"

class AHeliosCharacter;
class APlayerController;
class ACameraActor;
class UBoxComponent;
class FJsonObject;

/** Opt-in real-world functional QA. Never spawned in ordinary play. */
UCLASS()
class HELIOSOUTPOST_API AHeliosQARunner : public AActor
{
    GENERATED_BODY()
public:
    AHeliosQARunner();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
private:
    void Check(const FString& Name, bool Passed, const FString& Detail = TEXT(""));
    void StartControls();
    bool ResetPlayerForControls();
    void StartInputBindingQA();
    void TickInputBindingQA();
    bool ResetPlayerForInputTest();
    bool SendInputKey(const FKey& Key, EInputEvent Event);
    void StartDirectionalInputTest();
    void ReleaseInjectedKeys();
    void FinishInputBindingQA();
    bool SpawnCollisionTestWall();
    void DestroyCollisionTestWall();
    void BeginNavigationWait();
    bool IsNavigationReady(FString& OutDetail) const;
    void ContinueAfterNavigation();
    void StartRoutes();
    void StartRoute();
    void TickRoute(float Dt);
    void FinishRoute(bool Passed, const FString& Reason);
    void ContinueAfterRouteScreenshot();
    void WriteGroundDiagnosis(bool bNavigationReady);
    void CheckNavigation();
    void StartCaptures();
    void Finish();
    void RestorePlayerView();
    void AddMarkerIssue(TArray<TSharedPtr<class FJsonValue>>& Target, const class AHeliosMarker& Marker, const FString& Reason) const;
    void AddFrameMetrics(const TArray<double>& Samples, const TSharedPtr<FJsonObject>& Target) const;
    FVector JsonVector(const TSharedPtr<FJsonObject>& Object, const FString& Field) const;
    FString Mode;
    FString RootDir;
    TSharedPtr<FJsonObject> Layout;
    TSharedPtr<FJsonObject> Report;
    TArray<TSharedPtr<class FJsonValue>> Checks;
    TArray<TSharedPtr<class FJsonValue>> RouteResults;
    TArray<TSharedPtr<FJsonObject>> Routes;
    TArray<TSharedPtr<FJsonObject>> Cameras;
    TArray<FVector> Waypoints;
    TArray<double> FrameTimes;
    TArray<double> RouteFrameTimes;
    TWeakObjectPtr<AHeliosCharacter> Player;
    TWeakObjectPtr<APlayerController> InputQAController;
    TWeakObjectPtr<ACameraActor> CaptureCamera;
    TWeakObjectPtr<AActor> CollisionTestWall;
    TWeakObjectPtr<AActor> OriginalViewTarget;
    FVector StartPosition = FVector::ZeroVector;
    FVector StopPosition = FVector::ZeroVector;
    FVector LastProgressPosition = FVector::ZeroVector;
    FVector ControlForward = FVector::ForwardVector;
    FVector InputTestOrigin = FVector::ZeroVector;
    FVector InputExpectedDirection = FVector::ForwardVector;
    TArray<FKey> PressedInputKeys;
    FString InputKeyLabel;
    int32 Stage = 0;
    int32 InputQAStage = 0;
    int32 InputDirectionIndex = 0;
    int32 RouteIndex = 0;
    int32 WaypointIndex = 0;
    int32 CaptureIndex = 0;
    int32 FailureCount = 0;
    float Timer = 0;
    float StuckTimer = 0;
    float RouteTime = 0;
    float CommandTimer = 0;
    double LastFrameTime = 0;
    bool bOriginalHUDVisible = true;
    bool bCaptureStateSaved = false;
    bool bScreenshotCompletionObserved = false;
    bool bInputPressHandled = false;
    bool bInputReleaseHandled = false;
    bool bInputWasDownWhileHeld = false;
    bool bJumpLeftGround = false;
    bool bJumpReleased = false;
    bool bFinished = false;
};
