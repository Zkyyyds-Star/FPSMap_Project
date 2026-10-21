#pragma once

#include "CoreMinimal.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "HeliosBioSupplyProjectileMovement.generated.h"

/** Offline physics policy. Original item/config identity is owned by the definition. */
namespace HeliosBioSupplyBallistics
{
    inline constexpr float MaximumStepSeconds=1.f/120.f;
    inline constexpr float SolveToleranceCm=.5f;

    struct FLaunchAssessment
    {
        bool bReachable=false;
        FVector CandidateVelocity=FVector::ZeroVector;
        FVector ClosestPoint=FVector::ZeroVector;
        float ClosestDistanceCm=MAX_flt;
        float FlightSeconds=0;
        float PitchDegrees=0;
        // 1 for an exact solution; otherwise a bounded closest-approach score.
        // A positive score alone never authorizes firing an unreachable shot.
        float ReachabilityScore=0;
        int32 SimulatedSteps=0;
    };

    /** Same velocity cap and velocity-Verlet move delta used by UE5.8 ProjectileMovement. */
    HELIOSOUTPOST_API void AdvanceNativeStep(const FVector& Velocity,float DeltaSeconds,
        float MaximumSpeed,float ForwardAcceleration,float GravityZ,FVector& NextVelocity,FVector& MoveDelta);

    /** Collision-free aim assessment. Production Movement still performs real swept collision. */
    HELIOSOUTPOST_API FLaunchAssessment AssessNativeLaunch(const FVector& From,const FVector& To,
        float InitialSpeed,float MaximumSpeed,float ForwardAcceleration,float GravityZ,float Lifetime);

    /** Returns the lowest reachable arc, never a best-effort fake hit or teleported trajectory. */
    HELIOSOUTPOST_API bool SolveNativeLaunch(const FVector& From,const FVector& To,float InitialSpeed,
        float MaximumSpeed,float ForwardAcceleration,float GravityZ,float Lifetime,FVector& OutVelocity,
        FLaunchAssessment* Assessment=nullptr);
}

/** Real collision/gravity stay in the engine component; acceleration is integrated per substep. */
UCLASS()
class HELIOSOUTPOST_API UHeliosBioSupplyProjectileMovement : public UProjectileMovementComponent
{
    GENERATED_BODY()
public:
    UHeliosBioSupplyProjectileMovement();
    UPROPERTY(EditAnywhere,Category="Bio|OfflinePhysics",meta=(Units="cm/s^2"))
    float ForwardAcceleration=0;
    virtual FVector ComputeAcceleration(const FVector& InVelocity,float DeltaTime) const override;
};
