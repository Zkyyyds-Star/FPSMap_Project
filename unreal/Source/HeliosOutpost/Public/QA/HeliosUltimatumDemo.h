#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosUltimatumDemo.generated.h"
UCLASS()
class HELIOSOUTPOST_API AHeliosUltimatumDemo : public AActor
{
    GENERATED_BODY()
public:
    AHeliosUltimatumDemo();
    virtual void Tick(float Dt) override;
private:
    void Check(FString Name,bool Passed);
    void Capture(FString Name);
    void Finish();
    void CameraView(FVector Offset,FVector Focus);
    void CollisionChecks();
    void GaitTick(float Dt);
    float GaitWorstGrip=0,GaitWorstStep=0,GaitLastPhase=0;
    FVector GaitLastFoot=FVector::ZeroVector;
    int32 GaitSamples=0;
    float GaitSupportDrift=0;
    FVector GaitSupportOrigin=FVector::ZeroVector;
    bool bGaitSupporting=false;
    FString Results;
    int32 Stage=0,Failures=0,Frame=0,RecoveryCase=0;
    float Age=0,RecordClock=0,RecordStep=0,Before=0;
    double Started=0,PauseAt=0;
    bool bNearCaptured=false,bFarCaptured=false,bCombatAnchored=false;
    bool bCombatBrainChecked=false;
    TArray<float> FrameTimes;
    FVector Anchor,RecoveryOrigin;
    UPROPERTY() TObjectPtr<AActor> RecoveryFloor;
    UPROPERTY() TObjectPtr<AActor> RecoveryWall;
    UPROPERTY() TObjectPtr<class AHeliosUltimatumPet> Pet;
    UPROPERTY() TObjectPtr<class AHeliosEnemyActor> Enemy;
    UPROPERTY() TObjectPtr<class ACameraActor> Camera;
    UPROPERTY() TObjectPtr<AActor> Wall;
    TWeakObjectPtr<class AHeliosPetCompanion> PreviousPet;
};
