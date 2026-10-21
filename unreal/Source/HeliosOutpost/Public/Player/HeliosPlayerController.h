#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "HeliosPlayerController.generated.h"

UCLASS()
class HELIOSOUTPOST_API AHeliosPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    AHeliosPlayerController();

    UFUNCTION(Exec)
    void HeliosState();

    /** Console form: HeliosMove Forward Right Duration Sprint(0|1). */
    UFUNCTION(Exec)
    void HeliosMove(float Forward = 1.0f, float Right = 0.0f, float Duration = 1.0f, bool bSprint = false);

    UFUNCTION(Exec)
    void HeliosLook(float DeltaYaw = 0.0f, float DeltaPitch = 0.0f);

    UFUNCTION(Exec)
    void HeliosStop();

    UFUNCTION(Exec)
    void HeliosRunControlQA();

    UFUNCTION(Exec)
    void HeliosAdventureState();

    UFUNCTION(Exec)
    void HeliosAdventureRestart();

private:
    void LogResult(const TCHAR* Command, const struct FHeliosControlResult& Result) const;
};
