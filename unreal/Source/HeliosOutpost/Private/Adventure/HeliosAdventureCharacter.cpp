#include "Adventure/HeliosAdventureCharacter.h"
#include "Audio/HeliosAudio.h"

#include "Adventure/HeliosAdventureDirector.h"
#include "UI/HeliosAdventureHUD.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Engine/World.h"

void AHeliosAdventureCharacter::BeginPlay()
{
    Super::BeginPlay();
    LoadAdventureSounds();
}

void AHeliosAdventureCharacter::LoadAdventureSounds()
{
    static const TPair<FName, const TCHAR*> SoundPaths[] = {
        { TEXT("Fire"),   TEXT("/Game/Adventure/Audio/SFX_Fire.SFX_Fire") },
        { TEXT("Reload"), TEXT("/Game/Adventure/Audio/SFX_Reload.SFX_Reload") },
        { TEXT("Hurt"),   TEXT("/Game/Adventure/Audio/SFX_Hurt.SFX_Hurt") },
        { TEXT("Pickup"), TEXT("/Game/Adventure/Audio/SFX_Pickup.SFX_Pickup") },
        { TEXT("Death"),  TEXT("/Game/Adventure/Audio/SFX_Death.SFX_Death") }
    };

    for (const TPair<FName, const TCHAR*>& Entry : SoundPaths)
    {
        if (USoundBase* Sound = LoadObject<USoundBase>(nullptr, Entry.Value))
        {
            AdventureSounds.Add(Entry.Key, Sound);
        }
    }
}

void AHeliosAdventureCharacter::PlayAdventureCue(FName Cue)
{
    if (const TObjectPtr<USoundBase>* Sound = AdventureSounds.Find(Cue))
    {
        HeliosAudio::PlaySound2D(this, Sound->Get(), 0.72f);
    }
}

void AHeliosAdventureCharacter::OnCombatCue(FName Cue)
{
    if ((Cue == TEXT("Fire") || Cue == TEXT("Reload")) && GetWeaponId() != TEXT("rifle")) return;
    PlayAdventureCue(Cue);
}

float AHeliosAdventureCharacter::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser)
{
    const AHeliosAdventureDirector* Director = AHeliosAdventureDirector::Find(GetWorld());
    if ((Director && (Director->IsSelectingWeapon() || Director->IsRunOver()))
        || (GetWorld() && GetWorld()->IsPaused())) return 0.0f;
    return Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
}

bool AHeliosAdventureCharacter::OnPlayerDefeated()
{
    if (AHeliosAdventureDirector* Director = AHeliosAdventureDirector::Find(GetWorld()))
    {
        Director->EndRun();
    }
    return true;
}

void AHeliosAdventureCharacter::QuitPlay()
{
    if (const APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (AHeliosAdventureHUD* HUD = Cast<AHeliosAdventureHUD>(PC->GetHUD()))
        {
            HUD->TogglePauseMenu();
            return;
        }
    }
    Super::QuitPlay();
}
