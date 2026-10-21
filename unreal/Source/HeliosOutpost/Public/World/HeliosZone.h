#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosZone.generated.h"

class UBoxComponent;

UCLASS(BlueprintType)
class HELIOSOUTPOST_API AHeliosZone : public AActor
{
    GENERATED_BODY()

public:
    AHeliosZone();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Helios|Zone")
    TObjectPtr<UBoxComponent> Bounds;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helios|Zone")
    FString ZoneId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helios|Zone")
    FString DisplayName;

    UFUNCTION(BlueprintPure, Category = "Helios|Zone")
    bool ContainsWorldLocation(const FVector& WorldLocation) const;

    double GetBoundsVolume() const;
};
