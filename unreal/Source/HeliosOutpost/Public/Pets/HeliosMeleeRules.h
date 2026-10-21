#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

// Shared by the sword dog, boxing companion and flag-bound melee summons.
namespace HeliosMelee
{
inline bool InStrike(const AActor* Attacker, const AActor* Enemy, const FVector& Anchor, float Radius, float Range)
{
    if (!IsValid(Attacker) || !IsValid(Enemy)) return false;
    const FVector Offset = Enemy->GetActorLocation() - Attacker->GetActorLocation();
    return FVector::DistSquared2D(Anchor, Enemy->GetActorLocation()) <= FMath::Square(Radius)
        && Offset.SizeSquared2D() <= FMath::Square(Range) && FMath::Abs(Offset.Z) < 180.f
        && FVector::DotProduct(Attacker->GetActorForwardVector(), Offset.GetSafeNormal2D()) >= .5f;
}
inline double TargetScore(bool Recent, bool Threat, double DistanceSquared)
{ return (Recent ? 0. : Threat ? 1.e9 : 2.e9) + DistanceSquared; }
}
