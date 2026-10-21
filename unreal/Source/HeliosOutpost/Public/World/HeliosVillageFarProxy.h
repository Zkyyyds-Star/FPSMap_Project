#pragma once

#include "CoreMinimal.h"
#include "Engine/StaticMeshActor.h"
#include "HeliosVillageFarProxy.generated.h"

/** Explicit near/far visibility; a silhouette must never cover an enterable facade. */
UCLASS()
class HELIOSOUTPOST_API AHeliosVillageFarProxy : public AStaticMeshActor
{
    GENERATED_BODY()
public:
    AHeliosVillageFarProxy();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Village|Streaming")
    float VisibleBeyondCm = 23000.f;
private:
    void UpdateDistanceVisibility();
};
