#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "HeliosHUD.generated.h"

UCLASS()
class HELIOSOUTPOST_API AHeliosHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;
};
