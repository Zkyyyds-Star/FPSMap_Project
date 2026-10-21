#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosKillFeedbackQA.generated.h"
UCLASS()
class AHeliosKillFeedbackQA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosKillFeedbackQA();
    virtual void Tick(float Dt) override;
private:
    void Check(const FString& Name,bool Pass);
    void Finish();
    void CheckIntegration();
    void CaptureFrame();
    class AHeliosEnemyActor* SpawnTarget(bool Boss=false);
    FString OutputDirectory() const;
    int32 Stage=0,Sample=0,Failures=0;
    float Age=0,DeathAge=-1;
    double Started=0,RecordStarted=0,NextFrame=0;
    int32 FrameIndex=0;
    bool bFired=false,bCaptured=false,bRecording=false,bFinished=false;
    FString Checks,FrameTimes;
    UPROPERTY() TObjectPtr<class AHeliosArenaCharacter> Player;
    UPROPERTY() TObjectPtr<class AHeliosEnemyActor> Target;
};
