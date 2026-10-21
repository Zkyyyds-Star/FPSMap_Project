#include "World/HeliosVillageFarProxy.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

AHeliosVillageFarProxy::AHeliosVillageFarProxy()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickInterval=.25f;
    SetActorEnableCollision(false);
}

void AHeliosVillageFarProxy::BeginPlay()
{
    Super::BeginPlay();
    SetActorHiddenInGame(true);
    UpdateDistanceVisibility();
}

void AHeliosVillageFarProxy::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UpdateDistanceVisibility();
}

void AHeliosVillageFarProxy::UpdateDistanceVisibility()
{
    const auto* PC=GetWorld()?GetWorld()->GetFirstPlayerController():nullptr;
    if(!PC)return;
    FVector Eye;FRotator Rotation;PC->GetPlayerViewPoint(Eye,Rotation);
    const float Threshold=VisibleBeyondCm+(IsHidden()?250.f:-250.f);
    const bool bShouldHide=FVector::DistSquared2D(Eye,GetActorLocation())<FMath::Square(Threshold);
    if(IsHidden()!=bShouldHide)SetActorHiddenInGame(bShouldHide);
}
