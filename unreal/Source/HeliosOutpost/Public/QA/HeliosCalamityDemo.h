#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HAL/ThreadSafeCounter.h"
#include "HeliosCalamityDemo.generated.h"
class AHeliosArenaCharacter;
class AHeliosEnemyActor;
class AStaticMeshActor;
UCLASS()
class HELIOSOUTPOST_API AHeliosCalamityDemo : public AActor
{
    GENERATED_BODY()
public:
    AHeliosCalamityDemo();
    virtual void Tick(float Dt) override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
private:
    void Check(const FString& Name,bool Pass);
    void Capture(const FString& Name);
    void OnFrameCaptured(int32 Width,int32 Height,const TArray<FColor>& Pixels);
    void StopRecording();
    AHeliosEnemyActor* Target(FVector Offset,bool Boss=false);
    void Finish();
    void RunProjectileChecks();
    void RunImpactChecks();
    void RunRangeChecks();
    void TickRangeGallery(float Dt);
    void RunWindChecks();
    void TickWindGallery(float Dt,double FrameMs);
    int32 WindShot=0;
    FString WindSamples=TEXT("count,frame_ms,game_ms,render_ms,gpu_ms\n");
    UPROPERTY() TObjectPtr<class AHeliosCalamityEffect> RangeEffect;
    int32 RangeSpell=0,RangeCapture=0;
    int32 ImpactPass=0;
    UPROPERTY() TObjectPtr<AHeliosArenaCharacter> Player;
    UPROPERTY() TArray<TObjectPtr<AHeliosEnemyActor>> Targets;
    UPROPERTY() TObjectPtr<AStaticMeshActor> Wall;
    FVector Start;
    int32 Stage=0,SpellIndex=0,Failures=0,Frame=0,BeforeShots=0;
    float Age=0,CaptureAge=0,HealthBefore=0;
    double Started=0,LastFrame=0;
    bool bAction=false,bCapture=false;
    FString Results;
    TArray<double> FrameTimes;
    TSharedPtr<FThreadSafeCounter,ESPMode::ThreadSafe> PendingWrites;
    TSharedPtr<FThreadSafeCounter,ESPMode::ThreadSafe> FailedWrites;
    FString PendingCapturePath,CaptureTimeline;
    int32 PendingFrame=-1,PhaseChecks=0;
    double RecordingAt=0,FinishRequestedAt=0,PauseStartedAt=0;
    float PausedChargeAge=0;
    bool bAudioRecording=false,bFinishing=false;
};
