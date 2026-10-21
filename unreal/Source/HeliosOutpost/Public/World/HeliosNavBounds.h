#pragma once

#include "CoreMinimal.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "HeliosNavBounds.generated.h"

class UBoxComponent;

UCLASS(BlueprintType)
class HELIOSOUTPOST_API AHeliosNavBounds : public ANavMeshBoundsVolume
{
    GENERATED_BODY()

public:
    AHeliosNavBounds();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Helios|Navigation")
    TObjectPtr<UBoxComponent> NavigationBounds;

    /** Notifies the navigation system after importer changes to NavigationBounds. */
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Helios|Navigation")
    void RebuildNavigation();
};
