#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosPetDemo.generated.h"
UCLASS()
class HELIOSOUTPOST_API AHeliosPetDemo : public AActor
{
    GENERATED_BODY()
public:
    AHeliosPetDemo();
    virtual void Tick(float Dt) override;
private:
    void Check(const FString& Name,bool Passed);
    void Capture(const FString& Name);
    void Finish();
    void CheckMelee(class AHeliosPetCompanion* Pet,int32 Phase);
    void CheckAcquisition(class AHeliosPetCompanion* Pet);
    void SetCameraView(class AHeliosPetCompanion* Pet,FVector Offset,FVector Focus,float Fov);
    UPROPERTY() TObjectPtr<AActor> MeleeWall;
    float MeleeHP=0;
    int32 MeleeBursts=0;
    UPROPERTY() TObjectPtr<class AHeliosEnemyActor> Enemy;
    UPROPERTY() TObjectPtr<class ACameraActor> Camera;
    TWeakObjectPtr<class AHeliosPetCompanion> OldPet;
    int32 Stage=0,Failures=0;
    float Age=0,Before=0;
    double Started=0,PausedAt=0,SleptAt=0;
    FString Results;
    bool bIdleCaptured=false;
    bool bFarCombatCaptured=false,bFarSleepCaptured=false;
    int32 RecordedFrames=0;
    FVector MovingEnemyDirection=FVector::ZeroVector;
    int32 MovingStartHits=0;
    bool bMovedDuringSwing=false;
    float RecordAge=0,RecordStep=0;
    TArray<double> FrameTimes;
};
