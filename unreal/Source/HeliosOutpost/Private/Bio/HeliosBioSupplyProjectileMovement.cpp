#include "Bio/HeliosBioSupplyProjectileMovement.h"

namespace
{
constexpr float MaximumLifetimeSeconds=30.f;
constexpr int32 CoarseIntervals=34; // -85..85 degrees in five-degree intervals.
constexpr int32 RefinementIterations=24;

bool FiniteVector(const FVector& V)
{return FMath::IsFinite(V.X)&&FMath::IsFinite(V.Y)&&FMath::IsFinite(V.Z);}

struct FTrial
{
    FVector Velocity=FVector::ZeroVector,Closest=FVector::ZeroVector;
    float Miss=MAX_flt,Time=0,Pitch=0,PlaneError=0;
    int32 Steps=0;
    bool bReachedTargetPlane=false;
};

FTrial Simulate(const FVector& From,const FVector& To,const FVector& Horizontal,float Pitch,
    float InitialSpeed,float MaximumSpeed,float Acceleration,float GravityZ,float Lifetime)
{
    FTrial Result;Result.Pitch=Pitch;Result.Closest=From;
    const float Radians=FMath::DegreesToRadians(Pitch);
    Result.Velocity=(Horizontal*FMath::Cos(Radians)+FVector::UpVector*FMath::Sin(Radians))*InitialSpeed;
    FVector Velocity=Result.Velocity,Position=From;
    const double TargetRange=FVector::DotProduct(To-From,Horizontal);
    Result.Miss=FVector::Dist(From,To);
    const int32 MaximumSteps=FMath::CeilToInt(Lifetime/HeliosBioSupplyBallistics::MaximumStepSeconds);
    float Time=0;
    for(int32 Step=0;Step<MaximumSteps;++Step)
    {
        const float Dt=FMath::Min(HeliosBioSupplyBallistics::MaximumStepSeconds,Lifetime-Time);
        if(Dt<=SMALL_NUMBER)break;
        FVector NextVelocity,Delta;
        HeliosBioSupplyBallistics::AdvanceNativeStep(Velocity,Dt,MaximumSpeed,Acceleration,GravityZ,NextVelocity,Delta);
        const FVector Next=Position+Delta;
        const double Fraction=Delta.SizeSquared()>UE_DOUBLE_SMALL_NUMBER?FMath::Clamp(FVector::DotProduct(To-Position,Delta)/Delta.SizeSquared(),0.,1.):0.;
        const FVector Closest=Position+Delta*Fraction;
        const float Miss=FVector::Dist(Closest,To);
        if(Miss<Result.Miss){Result.Miss=Miss;Result.Closest=Closest;Result.Time=Time+Dt*Fraction;}
        ++Result.Steps;
        const double OldRange=FVector::DotProduct(Position-From,Horizontal),NewRange=FVector::DotProduct(Next-From,Horizontal);
        if(TargetRange>SMALL_NUMBER&&OldRange<=TargetRange&&NewRange>=TargetRange&&NewRange>OldRange)
        {
            const double Alpha=FMath::Clamp((TargetRange-OldRange)/(NewRange-OldRange),0.,1.);
            const FVector AtPlane=FMath::Lerp(Position,Next,Alpha);
            Result.bReachedTargetPlane=true;Result.PlaneError=AtPlane.Z-To.Z;
            // The best approach can precede this plane; keep its distance for
            // scoring, but the signed plane height brackets a real launch root.
            break;
        }
        Position=Next;Velocity=NextVelocity;Time+=Dt;
        if(!FiniteVector(Position)||!FiniteVector(Velocity))break;
    }
    return Result;
}
}

void HeliosBioSupplyBallistics::AdvanceNativeStep(const FVector& Velocity,float Dt,float MaximumSpeed,
    float Acceleration,float GravityZ,FVector& NextVelocity,FVector& MoveDelta)
{
    // UE5.8 Engine/Private/Components/ProjectileMovementComponent.cpp459-508:
    // v1=LimitVelocity(v0+a*dt); delta=(v0+v1)*dt/2. No external forces,
    // homing, plane constraint or drag is enabled on these native projectiles.
    const FVector Force=FVector(0,0,GravityZ)+Velocity.GetSafeNormal()*Acceleration;
    NextVelocity=Velocity+Force*Dt;
    if(MaximumSpeed>0)NextVelocity=NextVelocity.GetClampedToMaxSize(MaximumSpeed);
    MoveDelta=(Velocity+NextVelocity)*(.5f*Dt);
}

HeliosBioSupplyBallistics::FLaunchAssessment HeliosBioSupplyBallistics::AssessNativeLaunch(
    const FVector& From,const FVector& To,float InitialSpeed,float MaximumSpeed,float Acceleration,float GravityZ,float Lifetime)
{
    FLaunchAssessment Assessment;Assessment.ClosestPoint=From;
    if(!FiniteVector(From)||!FiniteVector(To)||!FMath::IsFinite(InitialSpeed)||!FMath::IsFinite(MaximumSpeed)||
        !FMath::IsFinite(Acceleration)||!FMath::IsFinite(GravityZ)||!FMath::IsFinite(Lifetime)||
        InitialSpeed<=0||Acceleration<0||Lifetime<=0||Lifetime>MaximumLifetimeSeconds)return Assessment;
    const FVector Target=To-From;
    const double Range=Target.Size();
    if(Range<SolveToleranceCm)return Assessment;
    const FVector Horizontal=Target.GetSafeNormal2D(SMALL_NUMBER,FVector::ForwardVector);
    // Match production Initialize(): a zero/too-small maximum retains launch speed.
    MaximumSpeed=FMath::Max(InitialSpeed,MaximumSpeed);
    FTrial Best;Best.Closest=From;Best.Miss=Range;
    int32 TotalSteps=0;
    const auto Trial=[&](float Pitch)
    {
        FTrial Value=Simulate(From,To,Horizontal,Pitch,InitialSpeed,MaximumSpeed,Acceleration,GravityZ,Lifetime);
        TotalSteps+=Value.Steps;if(Value.Miss<Best.Miss)Best=Value;return Value;
    };
    const auto Finish=[&](const FTrial& Value,bool Reachable)
    {
        FLaunchAssessment Out;Out.bReachable=Reachable;Out.CandidateVelocity=Value.Velocity;
        Out.ClosestPoint=Value.Closest;Out.ClosestDistanceCm=Value.Miss;Out.FlightSeconds=Value.Time;
        Out.PitchDegrees=Value.Pitch;Out.SimulatedSteps=TotalSteps;
        Out.ReachabilityScore=Reachable?1.f:FMath::Clamp(1.f-Value.Miss/FMath::Max(100.,Range),0.,1.);
        return Out;
    };
    const float DirectPitch=FMath::RadiansToDegrees(FMath::Atan2(Target.Z,Target.Size2D()));
    if(FMath::Abs(GravityZ)<SMALL_NUMBER||Target.SizeSquared2D()<1.)
    {
        const FTrial Direct=Trial(DirectPitch);return Finish(Direct,Direct.Miss<=SolveToleranceCm);
    }
    // Ordered scan chooses the first/lower root rather than a high lob that
    // happens to minimize miss. Each trial is bounded by lifetime and120Hz.
    FTrial Previous;bool HavePrevious=false;
    for(int32 I=0;I<=CoarseIntervals;++I)
    {
        const float Pitch=-85.f+170.f*I/CoarseIntervals;
        const FTrial Current=Trial(Pitch);
        if(Current.Miss<=SolveToleranceCm)return Finish(Current,true);
        if(Current.bReachedTargetPlane&&HavePrevious&&
            ((Previous.PlaneError<0&&Current.PlaneError>=0)||(Previous.PlaneError>0&&Current.PlaneError<=0)))
        {
            FTrial Low=Previous,High=Current;
            for(int32 J=0;J<RefinementIterations;++J)
            {
                const FTrial Middle=Trial((Low.Pitch+High.Pitch)*.5f);
                if(!Middle.bReachedTargetPlane)break;
                if(Middle.Miss<=SolveToleranceCm)return Finish(Middle,true);
                if((Low.PlaneError<0)==(Middle.PlaneError<0))Low=Middle;else High=Middle;
            }
        }
        if(Current.bReachedTargetPlane){Previous=Current;HavePrevious=true;}
        else HavePrevious=false;
    }
    return Finish(Best,false);
}

bool HeliosBioSupplyBallistics::SolveNativeLaunch(const FVector& From,const FVector& To,float InitialSpeed,
    float MaximumSpeed,float Acceleration,float GravityZ,float Lifetime,FVector& OutVelocity,FLaunchAssessment* OutAssessment)
{
    const auto Result=AssessNativeLaunch(From,To,InitialSpeed,MaximumSpeed,Acceleration,GravityZ,Lifetime);
    if(OutAssessment)*OutAssessment=Result;
    OutVelocity=Result.bReachable?Result.CandidateVelocity:FVector::ZeroVector;
    return Result.bReachable;
}

UHeliosBioSupplyProjectileMovement::UHeliosBioSupplyProjectileMovement()
{
    bForceSubStepping=true;MaxSimulationTimeStep=HeliosBioSupplyBallistics::MaximumStepSeconds;
    // Leaves enough real swept substeps for a two-second frame hitch. Engine
    // safety still bounds pathological larger hitches; this is offline policy.
    MaxSimulationIterations=256;
}

FVector UHeliosBioSupplyProjectileMovement::ComputeAcceleration(const FVector& InVelocity,float Dt) const
{
    return Super::ComputeAcceleration(InVelocity,Dt)+InVelocity.GetSafeNormal()*FMath::Max(0.f,ForwardAcceleration);
}
