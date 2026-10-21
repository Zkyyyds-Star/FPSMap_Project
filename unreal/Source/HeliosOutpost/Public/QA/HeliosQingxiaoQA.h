#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosQingxiaoQA.generated.h"
class AHeliosArenaCharacter;
class AHeliosEnemyActor;
class AStaticMeshActor;
UCLASS()
class HELIOSOUTPOST_API AHeliosQingxiaoQA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosQingxiaoQA();
    virtual void Tick(float Dt) override;
private:
    void Check(const FString& Name,bool Pass);
    void Capture(const FString& Name);
    void Finish();
    void ClearTargets();
    AHeliosEnemyActor* Enemy(const FVector& Offset,bool Boss=false);
    void BeginCase();
    void EndCase();
    UPROPERTY() TObjectPtr<AHeliosArenaCharacter> Player;
    UPROPERTY() TArray<TObjectPtr<AHeliosEnemyActor>> Enemies;
    UPROPERTY() TObjectPtr<AStaticMeshActor> Obstacle;
    FVector Start;
    int32 Stage=0,Failures=0,Case=0,Round=0,CaptureFrame=0,Frame=0,BeforeHits=0,BeforeWaves=0,BeforeWalls=0;
    float Age=0,Before=0,PausedAge=0;
    float ExpectedSlash=0,ExpectedSword=0,ExpectedHealth=0;
    bool bSettings=false,bLifecycleChecked=false,bLiveChecked=false;
    bool bCapture=false,bSmoke=false,bCaseAction=false,bImpactShot=false;
    double Started=0,PreviousFrame=0,PauseStart=0;
    TArray<double> IdleFrames,CombatFrames;
    FString Results;
};
