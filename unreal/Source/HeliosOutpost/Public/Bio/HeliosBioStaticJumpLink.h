#pragma once
#include "CoreMinimal.h"
#include "Navigation/NavLinkProxy.h"
#include "AITypes.h"
#include "HeliosBioStaticJumpLink.generated.h"
class ACharacter;
class UPathFollowingComponent;

struct HELIOSOUTPOST_API FHeliosBioStaticJumpDiagnostics
{
    bool bTraversing=false,bJumpIssued=false,bAirborneObserved=false;
    int32 Attempts=0,Landings=0,Failures=0,Step=INDEX_NONE,RouteSegment=0,GroundedWalkSamples=0,CompletedSegments=0;
    FVector Start=FVector::ZeroVector,End=FVector::ZeroVector,Feet=FVector::ZeroVector;
    FVector SegmentStart=FVector::ZeroVector,SegmentEnd=FVector::ZeroVector;
    float Age=0,NativeJumpZ=0,MaximumRise=0,PredictedFlightSeconds=0;
    FString Reason=TEXT("idle");
};

/** Original static surfaces linked by an actual native CharacterMovement jump.
 * No substitute geometry, teleport, LaunchCharacter or partial-path success. */
UCLASS()
class HELIOSOUTPOST_API AHeliosBioStaticJumpLink : public ANavLinkProxy
{
    GENERATED_BODY()
public:
    AHeliosBioStaticJumpLink();
    bool Configure(const FVector& StartWorldFeet,const FVector& EndWorldFeet,bool Bidirectional=true);
    // A derived connection may follow original narrow surfaces between real nav
    // polygons. Every internal segment still requires physical floor/capsule proof.
    bool ConfigurePhysicalRoute(const TArray<FVector>& WorldFeet,const TArray<bool>& WalkSegments,bool Bidirectional=true);
    virtual void Tick(float Dt) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    FVector GetStartWorldFeet() const { return WorldStart; }
    FVector GetEndWorldFeet() const { return WorldEnd; }
    const TArray<FVector>& GetPhysicalRouteWorldFeet() const { return PhysicalRoutePoints; }
    const TArray<bool>& GetPhysicalWalkSegments() const { return PhysicalWalkSegments; }
    static constexpr float PhysicalWalkInputScale=.4f;
    static constexpr float JumpAlignmentInputScale=.35f;
    static constexpr float PhysicalWalkEndTolerance=8.f;
    static constexpr float JumpAlignmentTolerance=16.f;
    // Optional QA player path follower can use the same native input motor.
    bool BeginTraversal(ACharacter* Character,const FVector& Destination);
    static bool IsTraversing(const ACharacter* Character);
    static void Forget(ACharacter* Character,bool ResetEvidence=false);
    static FHeliosBioStaticJumpDiagnostics GetDiagnostics(const ACharacter* Character);
    static FString DebugState(const ACharacter* Character);
    // Shared native air-control input; never changes velocity or movement tuning.
    // A moving landing surface is intercepted using its measured current velocity.
    static FVector NativeAirInputToLanding(const ACharacter* Character,const FVector& LandingFeet,const FVector& LandingVelocity,float Dt);
private:
    struct FAgentState
    {
        FVector Start=FVector::ZeroVector,End=FVector::ZeroVector;
        TArray<FVector> RoutePoints;
        TArray<bool> WalkSegments;
        float Age=0,SegmentAge=0,FlightAge=0,FlightSeconds=0,HorizontalStartSeconds=0,HorizontalArrivalSeconds=0;
        int32 RoundSerial=INDEX_NONE,RouteSegment=0;
        bool bJumpIssued=false,bAirborneObserved=false,bHorizontalStarted=false,bWalkStarted=false;
        TWeakObjectPtr<UPathFollowingComponent> PathFollowing;
        FAIRequestID RequestId=FAIRequestID::InvalidRequest;
    };
    TMap<TWeakObjectPtr<ACharacter>,FAgentState> Agents;
    TMap<TWeakObjectPtr<ACharacter>,double> RetryAfter;
    FVector WorldStart=FVector::ZeroVector,WorldEnd=FVector::ZeroVector;
    TArray<FVector> PhysicalRoutePoints;
    TArray<bool> PhysicalWalkSegments;
    bool bConfigured=false,bBidirectional=true;
    UFUNCTION() void HandleSmartLinkReached(AActor* Agent,const FVector& Destination);
    bool PlanNativeJump(ACharacter* Character,FAgentState& State,FString& Reason) const;
    bool PlanPhysicalWalk(ACharacter* Character,const FAgentState& State,FString& Reason) const;
    void CompleteSegment(ACharacter* Character,const FString& Reason);
    void FinishTraversal(ACharacter* Character,bool Landed,const FString& Reason);
};
