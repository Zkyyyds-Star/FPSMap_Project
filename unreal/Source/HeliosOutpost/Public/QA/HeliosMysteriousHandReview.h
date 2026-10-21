#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "HeliosMysteriousHandReview.generated.h"
class AHeliosArenaCharacter;
class AHeliosEnemyActor;

/** Isolated, reproducible renderer review. Used only by the dedicated review map/game override. */
UCLASS()
class HELIOSOUTPOST_API AHeliosMysteriousHandReview : public AGameModeBase
{
    GENERATED_BODY()
public:
    AHeliosMysteriousHandReview();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
private:
    float Age=0,NextCapture=0,Warmup=0;
    int32 Step=0,Frame=0;
    bool bSetup=false;
    UPROPERTY() TObjectPtr<AHeliosArenaCharacter> Player;
    UPROPERTY() TArray<TObjectPtr<AHeliosEnemyActor>> Enemies;
};
UCLASS()
class HELIOSOUTPOST_API AHeliosMysteriousHandReviewHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};
