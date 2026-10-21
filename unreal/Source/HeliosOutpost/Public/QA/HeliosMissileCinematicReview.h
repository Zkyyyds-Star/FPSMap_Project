#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "HeliosMissileCinematicReview.generated.h"
class AHeliosArenaCharacter;
class ACameraActor;

/** Isolated art review: high descent, close impacts, repeat fire and a roof interception. */
UCLASS()
class HELIOSOUTPOST_API AHeliosMissileCinematicReview : public AGameModeBase
{
    GENERATED_BODY()
public:
    AHeliosMissileCinematicReview();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
private:
    float Age=0,NextCapture=0,NextStrike=2;
    int32 Step=0,Frame=0;
    bool bSetup=false,bStress=false,bRecord=false,bFinished=false;
    FString Output;
    TArray<float> FrameTimes;
    UPROPERTY() TObjectPtr<AHeliosArenaCharacter> Player;
    UPROPERTY() TObjectPtr<ACameraActor> WideCamera;
};

UCLASS()
class HELIOSOUTPOST_API AHeliosMissileCinematicHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};
