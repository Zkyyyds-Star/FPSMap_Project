#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "HeliosBioNativeInputQA.generated.h"
class FJsonObject;
class APlayerController;

/** Read-only PlayerController input/state observer. Never generates gameplay input. */
UCLASS()
class HELIOSOUTPOST_API AHeliosBioNativeInputQA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosBioNativeInputQA();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    FString Out,ExpectedMap;
    double StartedWall=0,NextSnapshotWall=0;
    float ObservationSeconds=300;
    int32 EventCount=0,SnapshotCount=0;
    bool bFinished=false,bFixtureMode=false,bIOError=false,bInputInitialized=false;
    TMap<FKey,bool> PreviousKeys;
    TSharedRef<FJsonObject> Snapshot(const TCHAR* Kind) const;
    void Append(const TSharedRef<FJsonObject>& Row);
    void Finish(const FString& Reason,bool RequestExit);
};
