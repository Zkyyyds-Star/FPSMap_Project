#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosTownBall.generated.h"

class USphereComponent;
class UStaticMeshComponent;

/** Harmless physical prop ball used by town hoops and vending machines. */
UCLASS()
class HELIOSOUTPOST_API AHeliosTownBall : public AActor
{
    GENERATED_BODY()
public:
    AHeliosTownBall();
    virtual void Tick(float DeltaSeconds) override;
    void ThrowAtHoop(const FVector& InHoop, const FString& InMemeId);
    void Roll(const FVector& Direction);
private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Collision;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
    FVector Hoop = FVector::ZeroVector, PreviousPosition = FVector::ZeroVector;
    FString MemeId;
    bool bScoring = false;
};
