#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "HeliosEnemyHUD.generated.h"

UCLASS()
class HELIOSOUTPOST_API AHeliosEnemyHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};
