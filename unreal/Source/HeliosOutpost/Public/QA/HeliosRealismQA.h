#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosRealismQA.generated.h"
class AHeliosAdventureDirector;
class AHeliosAdventureCharacter;
class FJsonObject;
class FJsonValue;
UCLASS()
class HELIOSOUTPOST_API AHeliosRealismQA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosRealismQA();
    virtual void Tick(float DeltaSeconds) override;
private:
    void StartWeapon();
    void Check(const FString& Name,bool bPass,const FString& Detail=FString());
    void FinishWeapon();
    void Finish();
    UPROPERTY() TObjectPtr<AHeliosAdventureDirector> Director;
    UPROPERTY() TObjectPtr<AHeliosAdventureCharacter> Player;
    TArray<TSharedPtr<FJsonValue>> Checks;
    TArray<TSharedPtr<FJsonValue>> Results;
    TArray<TSharedPtr<FJsonValue>> CapturedFrames;
    TArray<double> FrameTimes;
    double PreviousWall=0;
    float Age=0;
    float ReloadStartAge=-1;
    float ReloadExpectedSeconds=0;
    float ReloadFrameTolerance=0.05f;
    uint32 Events=0;
    int32 Index=-1;
    int32 Failures=0;
    int32 MovieFrame=0;
    int32 AmmoBeforeReload=0;
    FRotator Aim=FRotator::ZeroRotator;
    bool bLegacy=false;
    bool bDemo=false;
    bool bFinished=false;
    bool bReloadEarlyChecked=false;
    bool bReloadCompletionChecked=false;
    int32 TestStartIndex=0;
    int32 TestEndIndex=3;
};
