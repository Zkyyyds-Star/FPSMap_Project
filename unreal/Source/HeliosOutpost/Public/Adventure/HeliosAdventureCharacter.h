#pragma once

#include "CoreMinimal.h"
#include "Player/HeliosArenaCharacter.h"
#include "HeliosAdventureCharacter.generated.h"

class USoundBase;

UCLASS(BlueprintType)
class HELIOSOUTPOST_API AHeliosAdventureCharacter : public AHeliosArenaCharacter
{
    GENERATED_BODY()

public:
    virtual void BeginPlay() override;
    virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
        AController* EventInstigator, AActor* DamageCauser) override;

    /** Plays one of the generated adventure cues: Fire, Reload, Hurt, Pickup or Death. */
    UFUNCTION(BlueprintCallable, Category="Helios|Adventure|Audio")
    void PlayAdventureCue(FName Cue);

protected:
    virtual bool OnPlayerDefeated() override;
    virtual void OnCombatCue(FName Cue) override;
    virtual void QuitPlay() override;

private:
    void LoadAdventureSounds();

    UPROPERTY(Transient)
    TMap<FName, TObjectPtr<USoundBase>> AdventureSounds;
};
