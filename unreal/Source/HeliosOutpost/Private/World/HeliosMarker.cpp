#include "World/HeliosMarker.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"

AHeliosMarker::AHeliosMarker()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;

    MarkerVisual = CreateDefaultSubobject<USphereComponent>(TEXT("MarkerVisual"));
    MarkerVisual->SetupAttachment(SceneRoot);
    MarkerVisual->InitSphereRadius(35.0f);
    MarkerVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    MarkerVisual->SetGenerateOverlapEvents(false);
    MarkerVisual->SetHiddenInGame(true);
    MarkerVisual->ShapeColor = FColor(255, 180, 32);
}
