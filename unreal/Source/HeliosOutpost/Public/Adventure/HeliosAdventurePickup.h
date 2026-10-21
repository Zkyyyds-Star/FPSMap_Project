#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosAdventurePickup.generated.h"

class AHeliosAdventureDirector;
class UPointLightComponent;
class USphereComponent;
class UStaticMeshComponent;

/** A visible, overlap-driven survival pickup. The director owns scoring and healing. */
UCLASS(BlueprintType)
class HELIOSOUTPOST_API AHeliosAdventurePickup : public AActor
{
    GENERATED_BODY()

public:
    AHeliosAdventurePickup();
    virtual void Tick(float DeltaSeconds) override;

    void Configure(AHeliosAdventureDirector* InDirector, bool bInMedical, const FString& InMarkerId);

    UFUNCTION(BlueprintPure, Category="Helios|Adventure")
    bool IsMedical() const { return bMedical; }

    UFUNCTION(BlueprintPure, Category="Helios|Adventure")
    FString GetSourceMarkerId() const { return SourceMarkerId; }

protected:
    virtual void BeginPlay() override;

    UFUNCTION()
    void HandleOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
        UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep,
        const FHitResult& SweepResult);

private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Collision;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Visual;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Glow;
    UPROPERTY(Transient) TObjectPtr<AHeliosAdventureDirector> Director;

    bool bMedical = false;
    bool bConsumed = false;
    FString SourceMarkerId;
    float BaseHeight = 0.0f;
    float VisualTime = 0.0f;
};
