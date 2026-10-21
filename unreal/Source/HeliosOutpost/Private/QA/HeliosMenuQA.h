#pragma once
#include "CoreMinimal.h"
class AHeliosAdventureHUD;
void StartHeliosMenuQA(TWeakObjectPtr<AHeliosAdventureHUD> HUD);
#if WITH_DEV_AUTOMATION_TESTS
extern TFunction<void(bool,bool)> HeliosMenuQANavigate;
#endif
