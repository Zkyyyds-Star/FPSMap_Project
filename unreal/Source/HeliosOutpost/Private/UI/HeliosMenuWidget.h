#pragma once

#include "CoreMinimal.h"

class AHeliosAdventureHUD;
class SWidget;

TSharedRef<SWidget> MakeHeliosMenu(TWeakObjectPtr<AHeliosAdventureHUD> HUD);

TSharedRef<SWidget> MakeHeliosTownOverview(TWeakObjectPtr<AHeliosAdventureHUD> HUD);
