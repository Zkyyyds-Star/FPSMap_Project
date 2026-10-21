#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosInstanceGroup.generated.h"

class UHierarchicalInstancedStaticMeshComponent;

UCLASS(BlueprintType)
class HELIOSOUTPOST_API AHeliosInstanceGroup : public AActor
{
    GENERATED_BODY()

public:
    AHeliosInstanceGroup();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Helios|Instances")
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Instances;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helios|Instances")
    FString GroupId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helios|Instances")
    FString ModuleId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helios|Instances")
    FString MaterialId;
};
