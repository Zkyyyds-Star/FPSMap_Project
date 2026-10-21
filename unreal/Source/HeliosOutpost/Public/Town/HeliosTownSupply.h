#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosTownSupply.generated.h"
class UStaticMeshComponent;

/** Grounded, life-sized medical bag. Collection is owned by the town's stable data ID. */
UCLASS()
class HELIOSOUTPOST_API AHeliosTownSupply : public AActor
{
    GENERATED_BODY()
public:
    AHeliosTownSupply();
    void Configure(const FString& Id);
private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
};
