#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosBasketballProjectile.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;
class APawn;

/** Gravity-driven E04 projectile with a single authoritative damage impact. */
UCLASS()
class HELIOSOUTPOST_API AHeliosBasketballProjectile : public AActor
{
    GENERATED_BODY()

public:
    AHeliosBasketballProjectile();
    void LaunchAt(AActor* InTarget, float InDamage);
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Helios|Projectile", meta=(ClampMin="0.1", ClampMax="4.0"))
    float FlightSpeedMultiplier = 2.0f;

    float GetPlannedFlightTime() const { return PlannedFlightTime; }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    UFUNCTION()
    void OnBallHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
        UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);

    UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Collision;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BallMesh;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProjectileMovementComponent> Movement;
    UPROPERTY(Transient) TObjectPtr<AActor> IntendedTarget;
    TWeakObjectPtr<APawn> ReciprocalIgnoredOwner;
    float Damage = 20.0f;
    float PlannedFlightTime = 0.0f;
    bool bImpactCommitted = false;
    bool bDamageApplied = false;
    bool bQADiagnostics = false;
    int32 QAShotId = INDEX_NONE;
    float QANextSampleTime = 0.0f;
    float QAClosestTargetDistance = TNumericLimits<float>::Max();
};
