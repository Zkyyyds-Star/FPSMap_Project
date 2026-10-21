#include "World/HeliosZone.h"
#include "Components/BoxComponent.h"

AHeliosZone::AHeliosZone()
{
    PrimaryActorTick.bCanEverTick = false;
    Bounds = CreateDefaultSubobject<UBoxComponent>(TEXT("Bounds"));
    RootComponent = Bounds;
    Bounds->SetBoxExtent(FVector(100.0f));
    Bounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Bounds->SetGenerateOverlapEvents(false);
    Bounds->SetHiddenInGame(true);
    Bounds->ShapeColor = FColor(48, 190, 255);
}

bool AHeliosZone::ContainsWorldLocation(const FVector& WorldLocation) const
{
    if (!Bounds)
    {
        return false;
    }

    const FVector Local = Bounds->GetComponentTransform().InverseTransformPosition(WorldLocation);
    const FVector Extent = Bounds->GetUnscaledBoxExtent();
    return FMath::Abs(Local.X) <= Extent.X
        && FMath::Abs(Local.Y) <= Extent.Y
        && FMath::Abs(Local.Z) <= Extent.Z;
}

double AHeliosZone::GetBoundsVolume() const
{
    if (!Bounds)
    {
        return TNumericLimits<double>::Max();
    }
    const FVector ScaledExtent = Bounds->GetScaledBoxExtent();
    return 8.0 * ScaledExtent.X * ScaledExtent.Y * ScaledExtent.Z;
}
