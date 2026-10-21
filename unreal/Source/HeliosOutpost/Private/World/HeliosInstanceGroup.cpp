#include "World/HeliosInstanceGroup.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

AHeliosInstanceGroup::AHeliosInstanceGroup()
{
    PrimaryActorTick.bCanEverTick = false;
    Instances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Instances"));
    RootComponent = Instances;
    Instances->SetMobility(EComponentMobility::Static);
}
