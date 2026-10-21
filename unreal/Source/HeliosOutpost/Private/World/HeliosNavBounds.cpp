#include "World/HeliosNavBounds.h"
#include "Components/BoxComponent.h"
#include "NavigationSystem.h"

AHeliosNavBounds::AHeliosNavBounds()
{
    NavigationBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("NavigationBounds"));
    NavigationBounds->SetupAttachment(RootComponent);
    NavigationBounds->SetBoxExtent(FVector(8000.0f, 6000.0f, 1200.0f));
    NavigationBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    NavigationBounds->SetGenerateOverlapEvents(false);
    NavigationBounds->SetCanEverAffectNavigation(false);
    NavigationBounds->SetHiddenInGame(true);
    NavigationBounds->ShapeColor = FColor(80, 210, 80);
}

void AHeliosNavBounds::RebuildNavigation()
{
    if (UWorld* World = GetWorld())
    {
        if (UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
        {
            NavigationSystem->OnNavigationBoundsUpdated(this);
            NavigationSystem->Build();
        }
    }
}
