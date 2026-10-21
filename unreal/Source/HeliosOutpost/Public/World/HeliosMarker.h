#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosMarker.generated.h"

class USceneComponent;
class USphereComponent;

UCLASS(BlueprintType)
class HELIOSOUTPOST_API AHeliosMarker : public AActor
{
    GENERATED_BODY()

public:
    AHeliosMarker();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Helios|Marker")
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Helios|Marker")
    TObjectPtr<USphereComponent> MarkerVisual;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helios|Marker")
    FString MarkerId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helios|Marker")
    FString MarkerType;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helios|Marker")
    FString ZoneId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helios|Marker")
    int32 FloorIndex = 0;
};
