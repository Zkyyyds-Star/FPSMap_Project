#pragma once
#include "CoreMinimal.h"
class AActor;
class UHeliosFirstPersonPresentationComponent;
class AShooterWeapon;
namespace HeliosWeaponHolderContext
{
    HELIOSOUTPOST_API bool IsDead(const AActor* Holder);
    HELIOSOUTPOST_API bool IsCompanion(const AActor* Holder);
    /** Returns true when character identity owns the cue, including a silent cue. */
    HELIOSOUTPOST_API bool PlayCharacterCue(AActor* Holder,FName Cue);
    HELIOSOUTPOST_API bool View(const AActor* Holder,FVector& Location,FRotator& Rotation);
    HELIOSOUTPOST_API FVector ViewLocation(const AActor* Holder);
    HELIOSOUTPOST_API FRotator ViewRotation(const AActor* Holder);
    HELIOSOUTPOST_API AShooterWeapon* EquippedWeapon(AActor* Holder);
    HELIOSOUTPOST_API AActor* FriendlyPlayer(AActor* Holder);
    HELIOSOUTPOST_API UHeliosFirstPersonPresentationComponent* Presentation(AActor* Holder);
}
