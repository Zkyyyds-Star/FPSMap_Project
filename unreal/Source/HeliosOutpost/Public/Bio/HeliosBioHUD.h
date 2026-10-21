#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "HeliosBioHUD.generated.h"
class SWidget;
class UFont;
UCLASS()
class HELIOSOUTPOST_API AHeliosBioHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void DrawHUD() override;
private:
    TSharedPtr<SWidget> Root;
    UPROPERTY(Transient) TObjectPtr<UFont> SupplyLabelFont;
};
