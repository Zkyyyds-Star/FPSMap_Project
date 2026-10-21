#include "Weapons/HeliosDetachedMegatronWeapon.h"
#include "Weapons/HeliosSSXVoice.h"
#include "Player/HeliosArenaCharacter.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Pets/HeliosHumanoidPet.h"
#include "Components/SkeletalMeshComponent.h"

void AHeliosDetachedMegatronWeapon::KeepCharacterOwnership()
{
    if(Voice && !bDetachedVoice)
    {
        Voice->Stop();
        Voice->InitializeSources(Cast<AHeliosHumanoidPet>(GetOwner())!=nullptr,TMap<FName,USoundBase*>(),TArray<USoundBase*>());
        bDetachedVoice=true;
    }
    if(Cup)Cup->SetVisibility(false);
}
void AHeliosDetachedMegatronWeapon::ApplyDefinition(const UHeliosWeaponDefinition* D)
{
    Super::ApplyDefinition(D);
    if(Cast<AHeliosHumanoidPet>(GetOwner()))
    {
        ShotSettings=HeliosYunSummer::Tuning(true);RefireRate=ShotSettings.Interval;
        ConfigureMagazine(ShotSettings.Magazine);
    }
    KeepCharacterOwnership();
}
void AHeliosDetachedMegatronWeapon::ActivateWeapon(const FName& Tag)
{
    Super::ActivateWeapon(Tag);
    bDetachedVoice=false;
    if(Cast<AHeliosHumanoidPet>(GetOwner())){ShotSettings=HeliosYunSummer::Tuning(true);RefireRate=ShotSettings.Interval;}
    KeepCharacterOwnership();
}
void AHeliosDetachedMegatronWeapon::Tick(float Dt)
{
    Super::Tick(Dt);
    KeepCharacterOwnership();
    if(GetOwner())
        if(auto* P=GetOwner()->FindComponentByClass<UHeliosCharacterPresentationComponent>())
            for(int32 I=ObservedKills;I<TrueKills;++I)P->PlayCharacterCue(TEXT("Kill"));
    ObservedKills=TrueKills;
}
void AHeliosDetachedMegatronWeapon::FireProjectile(const FVector& Target)
{
    Super::FireProjectile(Target);
    KeepCharacterOwnership();
}
bool AHeliosDetachedMegatronWeapon::HandleOwnerCombatCue(FName Cue)
{
    KeepCharacterOwnership();
    // The frozen gun still owns death cancellation and its effect lifecycle;
    // selected-character presentation exclusively owns human voice playback.
    const bool Handled=Super::HandleOwnerCombatCue(Cue);
    if(Cue!=TEXT("Hurt")&&Cue!=TEXT("Death"))return Handled;
    if(GetOwner())if(auto* P=GetOwner()->FindComponentByClass<UHeliosCharacterPresentationComponent>())
    {P->PlayCharacterCue(Cue);return true;}
    return Handled;
}
