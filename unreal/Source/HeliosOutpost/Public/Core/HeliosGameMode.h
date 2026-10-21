#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "HeliosGameMode.generated.h"

UCLASS()
class HELIOSOUTPOST_API AHeliosGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AHeliosGameMode();
    virtual void StartPlay() override;

private:
    void RebuildRuntimeNavigation();
};
