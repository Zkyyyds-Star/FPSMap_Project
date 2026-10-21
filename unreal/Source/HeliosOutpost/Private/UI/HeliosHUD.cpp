#include "UI/HeliosHUD.h"
#include "Player/HeliosCharacter.h"
#include "World/HeliosZone.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"

void AHeliosHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas || !PlayerOwner)
    {
        return;
    }

    const AHeliosCharacter* Character = Cast<AHeliosCharacter>(PlayerOwner->GetPawn());
    FString Region = TEXT("UNASSIGNED");
    if (Character)
    {
        double SmallestZoneVolume = TNumericLimits<double>::Max();
        for (TActorIterator<AHeliosZone> It(GetWorld()); It; ++It)
        {
            const double Volume = It->GetBoundsVolume();
            if (It->ContainsWorldLocation(Character->GetActorLocation()) && Volume < SmallestZoneVolume)
            {
                Region = It->DisplayName.IsEmpty() ? It->ZoneId : It->DisplayName;
                SmallestZoneVolume = Volume;
            }
        }
    }

    UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
    DrawText(TEXT("HELIOS OUTPOST"), FLinearColor(0.25f, 0.85f, 1.0f), 28.0f, 22.0f, Font, 1.35f, false);
    DrawText(FString::Printf(TEXT("REGION  %s"), *Region), FLinearColor::White, 28.0f, 52.0f, Font, 1.0f, false);
    DrawText(TEXT("WASD MOVE   MOUSE LOOK   SHIFT SPRINT   SPACE JUMP   ESC EXIT"), FLinearColor(0.78f, 0.82f, 0.86f), 28.0f, Canvas->ClipY - 46.0f, Font, 1.0f, false);
}
