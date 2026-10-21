#include "Town/HeliosTownSupply.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

AHeliosTownSupply::AHeliosTownSupply()
{
    PrimaryActorTick.bCanEverTick = false;
    Mesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MedicalBag")); SetRootComponent(Mesh);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCanEverAffectNavigation(false);
}
void AHeliosTownSupply::Configure(const FString& Id)
{
    Tags.AddUnique(FName(*Id));
    if (auto* Asset=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/IkunTown/Meshes/IT_Medbag.IT_Medbag")))
    {
        Mesh->SetStaticMesh(Asset); Mesh->SetRelativeScale3D(FVector::OneVector);
        // Data positions are ground contacts; imported kit meshes have centered pivots.
        SetActorLocation(GetActorLocation()+FVector(0,0,-Asset->GetBoundingBox().Min.Z));
    }
}
