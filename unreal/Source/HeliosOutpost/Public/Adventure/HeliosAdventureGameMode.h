#pragma once

#include "CoreMinimal.h"
#include "Core/HeliosGameMode.h"
#include "HeliosAdventureGameMode.generated.h"

UCLASS()
class HELIOSOUTPOST_API AHeliosAdventureGameMode : public AHeliosGameMode
{
    GENERATED_BODY()

public:
    AHeliosAdventureGameMode();
    virtual void StartPlay() override;
};
