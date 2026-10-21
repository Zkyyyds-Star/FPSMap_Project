#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosLoadoutFlowQA.generated.h"

/** Exercises the real HUD, control, death, travel and town persistence paths in isolated saves. */
UCLASS(NotBlueprintable)
class HELIOSOUTPOST_API AHeliosLoadoutFlowQA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosLoadoutFlowQA();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
private:
    void Check(const FString& Name,bool bPass);
    void Finish();
    void WriteReport() const;
    double StageStarted=0;
    double StageGameStarted=0;
    int32 LastStage=INDEX_NONE;
};
