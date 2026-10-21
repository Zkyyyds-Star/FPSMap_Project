#include "Adventure/HeliosAdventurePickup.h"

#include "Adventure/HeliosAdventureDirector.h"
#include "Player/HeliosArenaCharacter.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

AHeliosAdventurePickup::AHeliosAdventurePickup()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.02f;
    Tags.Add(TEXT("HeliosAdventurePickup"));

    Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
    RootComponent = Collision;
    Collision->InitSphereRadius(58.0f);
    Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
    Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    Collision->SetGenerateOverlapEvents(true);
    Collision->OnComponentBeginOverlap.AddDynamic(this, &AHeliosAdventurePickup::HandleOverlap);

    Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
    Visual->SetupAttachment(Collision);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Visual->SetRelativeScale3D(FVector(0.28f));
    if (UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr,
        TEXT("/Engine/BasicShapes/Sphere.Sphere")))
    {
        Visual->SetStaticMesh(Sphere);
    }

    Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
    Glow->SetupAttachment(Collision);
    Glow->SetAttenuationRadius(300.0f);
    Glow->SetIntensity(1800.0f);
    Glow->SetCastShadows(false);
}

void AHeliosAdventurePickup::BeginPlay()
{
    Super::BeginPlay();
    BaseHeight = GetActorLocation().Z;
}

void AHeliosAdventurePickup::Configure(AHeliosAdventureDirector* InDirector, bool bInMedical,
    const FString& InMarkerId)
{
    Director = InDirector;
    bMedical = bInMedical;
    SourceMarkerId = InMarkerId;
    Tags.AddUnique(bMedical ? TEXT("HeliosMedicalPickup") : TEXT("HeliosCollectiblePickup"));
    if (Glow)
    {
        Glow->SetLightColor(bMedical ? FLinearColor(0.15f, 1.0f, 0.30f)
                                     : FLinearColor(1.0f, 0.62f, 0.05f));
    }
    if (Visual)
    {
        const bool bUrbanMedical=bMedical && GetWorld() && GetWorld()->GetMapName().EndsWith(TEXT("UrbanChallenge"));
        if (bMedical)
        {
            if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr,bUrbanMedical ? TEXT("/Game/UrbanChallenge/Meshes/UC_MedicalCase.UC_MedicalCase") : TEXT("/Engine/BasicShapes/Cube.Cube")))
                Visual->SetStaticMesh(Cube);
        }
        Visual->SetRelativeScale3D(bUrbanMedical ? FVector(1.f) : bMedical ? FVector(0.24f, 0.24f, 0.34f)
                                           : FVector(0.26f));
        if (!bUrbanMedical) if (UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr,
            TEXT("/Game/Helios/Materials/M_HeliosSurface.M_HeliosSurface")))
        {
            UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, this);
            const FLinearColor Color = bMedical
                ? FLinearColor(0.04f, 0.8f, 0.16f) : FLinearColor(1.0f, 0.32f, 0.015f);
            Material->SetVectorParameterValue(TEXT("BaseColor"), Color);
            Material->SetVectorParameterValue(TEXT("EmissiveColor"), Color);
            Material->SetScalarParameterValue(TEXT("EmissiveStrength"), 4.0f);
            Material->SetScalarParameterValue(TEXT("Metallic"), 0.25f);
            Material->SetScalarParameterValue(TEXT("Roughness"), 0.28f);
            Visual->SetMaterial(0, Material);
        }
    }
}

void AHeliosAdventurePickup::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    VisualTime += DeltaSeconds;
    if (Visual)
    {
        Visual->AddLocalRotation(FRotator(0.0f, 75.0f * DeltaSeconds, 0.0f));
        Visual->SetRelativeLocation(FVector(0.0f, 0.0f, 12.0f + FMath::Sin(VisualTime * 2.4f) * 10.0f));
    }
}

void AHeliosAdventurePickup::HandleOverlap(UPrimitiveComponent*, AActor* OtherActor,
    UPrimitiveComponent*, int32, bool, const FHitResult&)
{
    if (bConsumed || !Cast<AHeliosArenaCharacter>(OtherActor) || !IsValid(Director))
    {
        return;
    }
    if (Director->TryCollect(bMedical))
    {
        bConsumed = true;
        Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Destroy();
    }
}
