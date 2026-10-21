#pragma once
#include "CoreMinimal.h"
#include "Core/HeliosGameMode.h"
#include "HeliosEnemyTestGameMode.generated.h"

/** Separate combat map mode; the original walk-only mode remains usable. */
UCLASS()
class HELIOSOUTPOST_API AHeliosEnemyTestGameMode : public AHeliosGameMode
{
    GENERATED_BODY()
public:
    AHeliosEnemyTestGameMode();
    virtual void StartPlay() override;
};
