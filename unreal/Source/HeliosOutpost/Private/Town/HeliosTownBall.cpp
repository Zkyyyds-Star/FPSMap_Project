#include "Town/HeliosTownBall.h"
#include "Town/HeliosTownSubsystem.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

AHeliosTownBall::AHeliosTownBall()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.f;
    InitialLifeSpan = 15.f;
    Collision = CreateDefaultSubobject<USphereComponent>(TEXT("BallPhysics"));
    SetRootComponent(Collision);
    Collision->InitSphereRadius(12.f);
    Collision->SetCollisionProfileName(TEXT("PhysicsActor"));
    Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
    Collision->SetSimulatePhysics(true);
    Collision->SetLinearDamping(0.f);
    Collision->SetAngularDamping(0.2f);
    Collision->BodyInstance.bUseCCD = true;
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Basketball"));
    Mesh->SetupAttachment(Collision);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCanEverAffectNavigation(false);
    Collision->SetCanEverAffectNavigation(false);
    if (auto* BallMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Helios/Enemies/Shared/SM_HeliosBasketball.SM_HeliosBasketball")))
    {
        Mesh->SetStaticMesh(BallMesh);
        Mesh->SetRelativeScale3D(FVector(12.0 / FMath::Max(1.0, BallMesh->GetBounds().BoxExtent.GetMax())));
    }
}

void AHeliosTownBall::ThrowAtHoop(const FVector& InHoop, const FString& InMemeId)
{
    Hoop = InHoop; MemeId = InMemeId; bScoring = true;
    PreviousPosition = GetActorLocation();
    const float FlightTime = FMath::Clamp(static_cast<float>(FVector::Dist2D(PreviousPosition, Hoop) / 650.), 1.f, 1.8f);
    FVector Velocity = (Hoop - PreviousPosition) / FlightTime;
    Velocity.Z -= 0.5f * GetWorld()->GetGravityZ() * FlightTime;
    Collision->SetPhysicsLinearVelocity(Velocity);
    Collision->SetPhysicsAngularVelocityInDegrees(FVector(0, 360, 50));
}

void AHeliosTownBall::Roll(const FVector& Direction)
{
    bScoring = false;
    Collision->AddImpulse(Direction.GetSafeNormal2D() * 240.f + FVector(0,0,100), NAME_None, true);
    Collision->SetPhysicsAngularVelocityInDegrees(FVector(120,0,40));
}

void AHeliosTownBall::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const FVector Position = GetActorLocation();
    if (bScoring && PreviousPosition.Z >= Hoop.Z && Position.Z < Hoop.Z)
    {
        const double Alpha = (PreviousPosition.Z-Hoop.Z) / FMath::Max(0.001, PreviousPosition.Z-Position.Z);
        const FVector Crossing = FMath::Lerp(PreviousPosition, Position, Alpha);
        if (FVector::Dist2D(Crossing, Hoop) <= 17.f)
        {
            bScoring = false;
            if (auto* Town = GetWorld()->GetSubsystem<UHeliosTownSubsystem>()) Town->RecordBasketballScore(MemeId);
        }
    }
    PreviousPosition = Position;
}
