#include "Weapons/HeliosWeaponHolderContext.h"
#include "Weapons/HeliosFirstPersonPresentationComponent.h"
#include "Player/HeliosArenaCharacter.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Pets/HeliosPetDog.h"
#include "Pets/HeliosHumanoidPet.h"
#include "Bio/HeliosBioPlayer.h"
#include "Bio/HeliosBioNPC.h"
#include "GameFramework/Pawn.h"
#include "Camera/CameraComponent.h"
bool HeliosWeaponHolderContext::IsCompanion(const AActor* Holder)
{return Holder&&Holder->ActorHasTag(TEXT("HeliosPet"));}
bool HeliosWeaponHolderContext::IsDead(const AActor* Holder)
{
    if(!IsValid(Holder))return true;
    if(const auto* P=Cast<AHeliosArenaCharacter>(Holder))return P->IsDead();
    if(const auto* P=Cast<AHeliosPetCompanion>(Holder))return P->IsSleeping()||P->State==EHeliosPetState::Waking||P->Health<=0||(P->CompanionOwner.IsValid()&&P->CompanionOwner->IsDead());
    return Holder->ActorHasTag(TEXT("Dead"));
}
bool HeliosWeaponHolderContext::View(const AActor* Holder,FVector& Location,FRotator& Rotation)
{
    if(!IsValid(Holder))return false;
    if(const auto* P=Cast<AHeliosArenaCharacter>(Holder)){Location=P->FirstPersonCamera->GetComponentLocation();Rotation=P->GetControlRotation();return true;}
    Holder->GetActorEyesViewPoint(Location,Rotation);return true;
}
UHeliosFirstPersonPresentationComponent* HeliosWeaponHolderContext::Presentation(AActor* Holder)
{return Holder?Holder->FindComponentByClass<UHeliosFirstPersonPresentationComponent>():nullptr;}
FVector HeliosWeaponHolderContext::ViewLocation(const AActor* Holder)
{FVector Location=FVector::ZeroVector;FRotator Rotation;View(Holder,Location,Rotation);return Location;}
FRotator HeliosWeaponHolderContext::ViewRotation(const AActor* Holder)
{FVector Location;FRotator Rotation=FRotator::ZeroRotator;View(Holder,Location,Rotation);return Rotation;}
AShooterWeapon* HeliosWeaponHolderContext::EquippedWeapon(AActor* Holder)
{
    if(auto* P=Cast<AHeliosArenaCharacter>(Holder))return P->GetEquippedWeapon();
    if(auto* P=Cast<AHeliosHumanoidPet>(Holder))return P->GetEquippedWeapon();
    if(auto* P=Cast<AHeliosBioPlayer>(Holder))return P->GetEquippedWeapon();
    if(auto* P=Cast<AHeliosBioNPC>(Holder))return P->GetEquippedWeapon();
    return nullptr;
}

bool HeliosWeaponHolderContext::PlayCharacterCue(AActor* Holder,FName Cue)
{
    if(auto* Presentation=Holder?Holder->FindComponentByClass<UHeliosCharacterPresentationComponent>():nullptr)
    {Presentation->PlayCharacterCue(Cue);return true;}
    return false;
}

AActor* HeliosWeaponHolderContext::FriendlyPlayer(AActor* Holder)
{if(auto* Companion=Cast<AHeliosPetCompanion>(Holder))return Companion->CompanionOwner.Get();return nullptr;}
