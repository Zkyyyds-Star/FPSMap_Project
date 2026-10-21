#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosSoulBannerQA.generated.h"
class AHeliosAdventureCharacter;
class AHeliosEnemyActor;
UCLASS()
class HELIOSOUTPOST_API AHeliosSoulBannerQA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosSoulBannerQA();
    virtual void Tick(float Dt) override;
private:
    void Check(const FString& Name,bool Pass);
    void Capture(const FString& Name);
    void Finish();
    UPROPERTY() TObjectPtr<AHeliosAdventureCharacter> Player;
    UPROPERTY() TArray<TObjectPtr<AHeliosEnemyActor>> Enemies;
    int32 Stage=0,Failures=0,Frames=0,CaptureFrame=0,IntroFrames=0;
    float Age=0,Before=0,PauseCooldown=0;
    double Started=0,MeasureStarted=0;
    TArray<float> CombatFrameMilliseconds;
    bool bCloseupsDone=false;
    bool bTownFixturePlaced=false;
    FString Results;
    TMap<FString,int32> CaptureStills;
    FVector StartPosition;
};
