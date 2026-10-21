#include "Bio/HeliosBioStaticJumpLink.h"
#include "Bio/HeliosBioHotelGameMode.h"
#include "NavLinkCustomComponent.h"
#include "NavAreas/NavArea_Default.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Engine/World.h"

namespace
{
    TMap<TWeakObjectPtr<ACharacter>,TWeakObjectPtr<AHeliosBioStaticJumpLink>> JumpOwners;
    TMap<TWeakObjectPtr<ACharacter>,FHeliosBioStaticJumpDiagnostics> JumpEvidence;
    constexpr float LandingTolerance=40.f;
    constexpr float AlignmentTolerance=AHeliosBioStaticJumpLink::JumpAlignmentTolerance;
    float NativeAirAcceleration(const UCharacterMovementComponent* Movement,const FVector& Velocity)
    {
        float Control=Movement->AirControl;
        if(Movement->AirControlBoostMultiplier>0&&Velocity.SizeSquared2D()<FMath::Square(Movement->AirControlBoostVelocityThreshold))Control=FMath::Min(1.f,Control*Movement->AirControlBoostMultiplier);
        return FMath::Max(0.f,Movement->MaxAcceleration*Control);
    }
    FVector NativeSteeringInput(const UCharacterMovementComponent* Movement,const FVector& Feet,const FVector& Velocity,const FVector& End,float Remaining,float Dt,bool StopAtTarget=false)
    {
        const float AirAcceleration=NativeAirAcceleration(Movement,Velocity);if(AirAcceleration<=0)return FVector::ZeroVector;
        const FVector Displacement=(End-Feet)*FVector(1,1,0);const float Distance=Displacement.Size();
        float DesiredSpeed=FMath::Min(Movement->MaxWalkSpeed,Distance/FMath::Max(.016f,Remaining));
        // A short descending landing can have just 2cm clearance past its
        // source ledge. Brake before the endpoint using native acceleration,
        // rather than arriving at full speed and oscillating across that ledge.
        if(StopAtTarget)DesiredSpeed=FMath::Min(DesiredSpeed,FMath::Sqrt(2.f*AirAcceleration*FMath::Max(0.f,Distance-.25f)));
        const FVector Desired=Displacement.GetSafeNormal()*DesiredSpeed;
        const FVector Change=(Desired-Velocity*FVector(1,1,0))/FMath::Max(.001f,Dt);
        return (Change/AirAcceleration).GetClampedToMaxSize(1.f);
    }
    float NativeTimeToLanding(const UCharacterMovementComponent* Movement,const FVector& Feet,float VerticalVelocity,const FVector& End)
    {
        const float Gravity=-Movement->GetGravityZ(),Discriminant=FMath::Square(VerticalVelocity)+2.f*Gravity*(Feet.Z-End.Z);
        return Gravity>0&&Discriminant>=0?FMath::Max(.016f,(VerticalVelocity+FMath::Sqrt(Discriminant))/Gravity):.016f;
    }
}
AHeliosBioStaticJumpLink::AHeliosBioStaticJumpLink()
{
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PrePhysics;
    PointLinks.Reset();SegmentLinks.Reset();bSmartLinkIsRelevant=true;
    GetSmartLinkComp()->SetNavigationRelevancy(true);
    GetSmartLinkComp()->SetEnabledArea(UNavArea_Default::StaticClass());
    OnSmartLinkReached.AddDynamic(this,&AHeliosBioStaticJumpLink::HandleSmartLinkReached);
    Tags.Add(TEXT("BioOriginalStaticJumpLink"));
}
bool AHeliosBioStaticJumpLink::Configure(const FVector& StartWorldFeet,const FVector& EndWorldFeet,bool Bidirectional)
{
    if(StartWorldFeet.ContainsNaN()||EndWorldFeet.ContainsNaN()||FVector::Dist(StartWorldFeet,EndWorldFeet)<5)return false;
    WorldStart=StartWorldFeet;WorldEnd=EndWorldFeet;bBidirectional=Bidirectional;
    PhysicalRoutePoints={WorldStart,WorldEnd};PhysicalWalkSegments={false};
    GetSmartLinkComp()->SetLinkData(GetActorTransform().InverseTransformPosition(WorldStart),GetActorTransform().InverseTransformPosition(WorldEnd),Bidirectional?ENavLinkDirection::BothWays:ENavLinkDirection::LeftToRight);
    GetSmartLinkComp()->SetNavigationRelevancy(true);SetSmartLinkEnabled(true);bConfigured=true;
    UE_LOG(LogTemp,Display,TEXT("BIO_STATIC_JUMP_CONFIG link=%s start=%s end=%s direction=%s native_jump_only=true"),*GetName(),*WorldStart.ToString(),*WorldEnd.ToString(),Bidirectional?TEXT("both"):TEXT("start_to_end_only"));return true;
}
bool AHeliosBioStaticJumpLink::ConfigurePhysicalRoute(const TArray<FVector>& WorldFeet,const TArray<bool>& WalkSegments,bool Bidirectional)
{
    if(WorldFeet.Num()<2||WalkSegments.Num()!=WorldFeet.Num()-1)return false;
    for(const FVector& Point:WorldFeet)if(Point.ContainsNaN())return false;
    for(int32 Index=1;Index<WorldFeet.Num();++Index)if(FVector::Dist(WorldFeet[Index-1],WorldFeet[Index])<5)return false;
    if(!Configure(WorldFeet[0],WorldFeet.Last(),Bidirectional))return false;
    PhysicalRoutePoints=WorldFeet;PhysicalWalkSegments=WalkSegments;
    UE_LOG(LogTemp,Display,TEXT("BIO_STATIC_PHYSICAL_ROUTE_CONFIG link=%s segments=%d geometry_changed=false capsule_sweeps_and_native_ground_contact_required=true"),*GetName(),WalkSegments.Num());return true;
}
bool AHeliosBioStaticJumpLink::IsTraversing(const ACharacter* Character)
{
    const auto* Owner=JumpOwners.Find(const_cast<ACharacter*>(Character));return Owner&&Owner->IsValid();
}
FHeliosBioStaticJumpDiagnostics AHeliosBioStaticJumpLink::GetDiagnostics(const ACharacter* Character)
{
    auto Result=JumpEvidence.FindRef(const_cast<ACharacter*>(Character));Result.bTraversing=IsTraversing(Character);if(Character)Result.Feet=Character->GetNavAgentLocation();return Result;
}
FString AHeliosBioStaticJumpLink::DebugState(const ACharacter* Character)
{
    const auto D=GetDiagnostics(Character);return FString::Printf(TEXT("active=%d step=%d attempts=%d landed=%d failed=%d jump=%d airborne=%d age=%.2f feet=%s start=%s end=%s segment=%d segment_start=%s segment_end=%s grounded_walk_samples=%d completed_segments=%d native_jump_z=%.1f max_rise=%.1f predicted_time=%.3f reason=%s"),D.bTraversing,D.Step,D.Attempts,D.Landings,D.Failures,D.bJumpIssued,D.bAirborneObserved,D.Age,*D.Feet.ToString(),*D.Start.ToString(),*D.End.ToString(),D.RouteSegment,*D.SegmentStart.ToString(),*D.SegmentEnd.ToString(),D.GroundedWalkSamples,D.CompletedSegments,D.NativeJumpZ,D.MaximumRise,D.PredictedFlightSeconds,*D.Reason);
}
FVector AHeliosBioStaticJumpLink::NativeAirInputToLanding(const ACharacter* Character,const FVector& LandingFeet,const FVector& LandingVelocity,float Dt)
{
    if(!Character||!FMath::IsFinite(Dt)||Dt<=0||LandingFeet.ContainsNaN()||LandingVelocity.ContainsNaN())return FVector::ZeroVector;
    const auto* Movement=Character->GetCharacterMovement();const FVector Feet=Character->GetNavAgentLocation();
    const float Gravity=-Movement->GetGravityZ(),RelativeUp=Movement->Velocity.Z-LandingVelocity.Z;
    const float Disc=RelativeUp*RelativeUp+2.f*Gravity*(Feet.Z-LandingFeet.Z);
    if(Gravity<=0||Disc<0)return FVector::ZeroVector;
    const float Remaining=FMath::Max(.016f,(RelativeUp+FMath::Sqrt(Disc))/Gravity);
    return NativeSteeringInput(Movement,Feet,Movement->Velocity,LandingFeet+LandingVelocity*Remaining,Remaining,Dt);
}
void AHeliosBioStaticJumpLink::Forget(ACharacter* Character,bool ResetEvidence)
{
    if(auto* Owner=JumpOwners.Find(Character);Owner&&Owner->IsValid())Owner->Get()->FinishTraversal(Character,false,TEXT("actor_state_reset_or_cancelled"));
    JumpOwners.Remove(Character);if(ResetEvidence)JumpEvidence.Remove(Character);
}
void AHeliosBioStaticJumpLink::HandleSmartLinkReached(AActor* Agent,const FVector& Destination)
{
    auto* Character=Cast<ACharacter>(Agent);
    if(!BeginTraversal(Character,Destination))
    {
        if(auto* AI=Character?Cast<AAIController>(Character->GetController()):nullptr)
            if(auto* Path=AI->GetPathFollowingComponent())Path->AbortMove(*this,FPathFollowingResultFlags::UserAbort|FPathFollowingResultFlags::InvalidPath);
    }
}
bool AHeliosBioStaticJumpLink::BeginTraversal(ACharacter* Character,const FVector& Destination)
{
    if(!bConfigured||!IsValid(Character)||Character->GetWorld()!=GetWorld()||Character->ActorHasTag(TEXT("Dead"))||IsTraversing(Character))return false;
    if(GetWorld()->GetTimeSeconds()<RetryAfter.FindRef(Character))return false;
    const bool TowardEnd=FVector::DistSquared(Destination,WorldEnd)<=FVector::DistSquared(Destination,WorldStart);
    if(!bBidirectional&&!TowardEnd)return false;
    FAgentState State;
    if(TowardEnd){State.RoutePoints=PhysicalRoutePoints;State.WalkSegments=PhysicalWalkSegments;}
    else
    {
        for(int32 Index=PhysicalRoutePoints.Num()-1;Index>=0;--Index)State.RoutePoints.Add(PhysicalRoutePoints[Index]);
        for(int32 Index=PhysicalWalkSegments.Num()-1;Index>=0;--Index)State.WalkSegments.Add(PhysicalWalkSegments[Index]);
    }
    State.Start=State.RoutePoints[0];State.End=State.RoutePoints[1];
    if(auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld()))State.RoundSerial=Mode->RoundSerial;
    if(auto* AI=Cast<AAIController>(Character->GetController()))
    {
        State.PathFollowing=AI->GetPathFollowingComponent();if(auto* Path=State.PathFollowing.Get()){State.RequestId=Path->GetCurrentRequestId();Path->PauseMove(State.RequestId,EPathFollowingVelocityMode::Keep);}
    }
    Character->GetCharacterMovement()->StopActiveMovement();
    Agents.Add(Character,State);JumpOwners.Add(Character,this);
    auto& D=JumpEvidence.FindOrAdd(Character);++D.Attempts;D.bTraversing=true;D.bJumpIssued=false;D.bAirborneObserved=false;D.Step=0;D.Age=0;D.Start=State.Start;D.End=State.RoutePoints.Last();D.SegmentStart=State.Start;D.SegmentEnd=State.End;D.RouteSegment=0;D.GroundedWalkSamples=0;D.CompletedSegments=0;D.NativeJumpZ=Character->GetCharacterMovement()->JumpZVelocity;
    const float Gravity=-Character->GetCharacterMovement()->GetGravityZ();D.MaximumRise=Gravity>0?FMath::Square(D.NativeJumpZ)/(2*Gravity):0;D.PredictedFlightSeconds=0;D.Reason=TEXT("aligning_to_original_link_start");
    UE_LOG(LogTemp,Display,TEXT("BIO_STATIC_JUMP_ATTEMPT link=%s actor=%s %s"),*GetName(),*Character->GetName(),*DebugState(Character));return true;
}
bool AHeliosBioStaticJumpLink::PlanNativeJump(ACharacter* Character,FAgentState& State,FString& Reason) const
{
    auto* Movement=Character->GetCharacterMovement();const float Gravity=-Movement->GetGravityZ(),JumpZ=Movement->JumpZVelocity;
    const float Height=State.End.Z-Character->GetNavAgentLocation().Z,Disc=JumpZ*JumpZ-2*Gravity*Height;
    if(Gravity<=0||JumpZ<=0||Disc<0){Reason=FString::Printf(TEXT("native_jump_height_unreachable delta_z=%.3f jump_z=%.3f gravity=%.3f max_rise=%.3f"),Height,JumpZ,Gravity,Gravity>0?JumpZ*JumpZ/(2*Gravity):0);return false;}
    State.FlightSeconds=(JumpZ+FMath::Sqrt(Disc))/Gravity;
    // Rise above the measured destination surface before crossing its face.
    // The native controller starts from rest, not the old constant XY velocity
    // which sent the capsule into the 74cm box before it had risen far enough.
    State.HorizontalStartSeconds=Height>0?(JumpZ-FMath::Sqrt(Disc))/Gravity:0.f;
    const FVector Delta=(State.End-Character->GetNavAgentLocation())*FVector(1,1,0);const FVector AverageVelocity=Delta/FMath::Max(.01f,State.FlightSeconds);
    State.HorizontalArrivalSeconds=State.FlightSeconds;
    if(Height<0)
    {
        // A low landing must clear the departure ledge while the capsule is
        // high, rather than reaching its XY only after descending into its edge.
        // The same bounded native-input deadline is used by prediction and Tick.
        const float Acceleration=FMath::Max(1.f,Movement->MaxAcceleration*Movement->AirControl);
        const float Speed=FMath::Max(1.f,Movement->MaxWalkSpeed),Distance=Delta.Size();
        const float RampDistance=Speed*Speed/Acceleration;
        const float RestToRest=Distance<=RampDistance?2.f*FMath::Sqrt(Distance/Acceleration):2.f*Speed/Acceleration+(Distance-RampDistance)/Speed;
        State.HorizontalArrivalSeconds=FMath::Min(State.FlightSeconds,FMath::Max(JumpZ/Gravity,RestToRest*1.15f));
    }
    if(State.FlightSeconds<.1f||State.FlightSeconds>2.5f||AverageVelocity.Size()>Movement->MaxWalkSpeed){Reason=FString::Printf(TEXT("native_jump_horizontal_unreachable distance=%.3f flight=%.3f required=%.3f native_speed=%.3f"),Delta.Size(),State.FlightSeconds,AverageVelocity.Size(),Movement->MaxWalkSpeed);return false;}
    if(Delta.Size()/FMath::Max(.001f,State.FlightSeconds-State.HorizontalStartSeconds)>Movement->MaxWalkSpeed)
    {Reason=TEXT("native_jump_horizontal_unreachable_after_vertical_clearance");return false;}
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BioStaticJumpNativeCapsule),false,Character);TArray<AActor*> Attached;Character->GetAttachedActors(Attached,true,true);Query.AddIgnoredActors(Attached);
    FCollisionObjectQueryParams Static(ECC_WorldStatic);FHitResult Floor;
    if(!GetWorld()->LineTraceSingleByObjectType(Floor,State.End+FVector(0,0,20),State.End-FVector(0,0,35),Static,Query)||!Floor.GetComponent()||Floor.ImpactNormal.Z<Movement->GetWalkableFloorZ()||FMath::Abs(Floor.ImpactPoint.Z-State.End.Z)>12){Reason=TEXT("link_destination_has_no_original_walkable_static_floor");return false;}
    const auto* Capsule=Character->GetCapsuleComponent();const FCollisionShape Shape=FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight());
    const FVector StartFeet=Character->GetNavAgentLocation();
    float SourcePlaneReturnSeconds=0,RequiredLedgeProgress=0,ActualLedgeProgress=0;bool LedgeClearanceObserved=false;
    if(Height<0)
    {
        FHitResult SourceFloor;
        if(GetWorld()->LineTraceSingleByObjectType(SourceFloor,StartFeet+FVector(0,0,12),StartFeet-FVector(0,0,16),Static,Query)&&SourceFloor.ImpactNormal.Z>.999f)
        {
            const FVector Along=Delta.GetSafeNormal();const float Distance=Delta.Size();float LastTop=0;const float ProbeStep=1.f;
            // Only the connected, physically measured departure top is used.
            // The first missing source-plane hit bounds its edge to one cm.
            for(float AlongCm=ProbeStep;AlongCm<=Distance;AlongCm+=ProbeStep)
            {
                const FVector Point=StartFeet+Along*AlongCm;FHitResult Top;
                if(!GetWorld()->LineTraceSingleByObjectType(Top,FVector(Point.X,Point.Y,SourceFloor.ImpactPoint.Z+1),FVector(Point.X,Point.Y,SourceFloor.ImpactPoint.Z-1),Static,Query)||Top.ImpactNormal.Z<.999f||FMath::Abs(Top.ImpactPoint.Z-SourceFloor.ImpactPoint.Z)>.5f)break;
                LastTop=AlongCm;
            }
            RequiredLedgeProgress=LastTop+ProbeStep+Capsule->GetScaledCapsuleRadius();
            SourcePlaneReturnSeconds=2.f*JumpZ/Gravity;
        }
    }
    FVector SimulatedFeet=StartFeet,Velocity=FVector::ZeroVector,Previous=Character->GetActorLocation();float Time=0;bool HorizontalStarted=Height<=0;
    while(Time<State.FlightSeconds-.0001f)
    {
        const float Step=FMath::Min(1.f/60.f,State.FlightSeconds-Time),T=Time+Step;
        HorizontalStarted|=SimulatedFeet.Z>=State.End.Z;
        const FVector Input=HorizontalStarted?NativeSteeringInput(Movement,SimulatedFeet,Velocity,State.End,(Height<0?State.HorizontalArrivalSeconds-Time:NativeTimeToLanding(Movement,SimulatedFeet,JumpZ-Gravity*Time,State.End)),Step,Height<0):FVector::ZeroVector;
        const FVector NewVelocity=(Velocity+Input*NativeAirAcceleration(Movement,Velocity)*Step).GetClampedToMaxSize(Movement->MaxWalkSpeed);
        SimulatedFeet+=(Velocity+NewVelocity)*(.5f*Step);SimulatedFeet.Z=StartFeet.Z+JumpZ*T-.5f*Gravity*T*T;Velocity=NewVelocity;
        if(SourcePlaneReturnSeconds>0&&!LedgeClearanceObserved&&T>=SourcePlaneReturnSeconds)
        {
            LedgeClearanceObserved=true;ActualLedgeProgress=FVector::DotProduct((SimulatedFeet-StartFeet)*FVector(1,1,0),Delta.GetSafeNormal());
            if(ActualLedgeProgress<RequiredLedgeProgress){Reason=FString::Printf(TEXT("native_descending_jump_has_not_cleared_source_top time=%.3f horizontal_arrival=%.3f measured_progress=%.3f required_edge_plus_radius=%.3f"),T,State.HorizontalArrivalSeconds,ActualLedgeProgress,RequiredLedgeProgress);return false;}
        }
        const FVector Next=SimulatedFeet+FVector(0,0,Capsule->GetScaledCapsuleHalfHeight());FHitResult Hit;
        if(GetWorld()->SweepSingleByChannel(Hit,Previous,Next,FQuat::Identity,ECC_Pawn,Shape,Query))
        {
            const bool Landing=T>=State.FlightSeconds*.75f&&FVector::Dist2D(SimulatedFeet,State.End)<=LandingTolerance&&Hit.GetComponent()==Floor.GetComponent()&&Hit.ImpactNormal.Z>=Movement->GetWalkableFloorZ()&&FMath::Abs(Hit.ImpactPoint.Z-Floor.ImpactPoint.Z)<=12.f;
            if(!Landing){Reason=FString::Printf(TEXT("native_capsule_arc_blocked time=%.3f vertical_clearance_until=%.3f actor=%s component=%s impact=%s normal=%s"),T,State.HorizontalStartSeconds,*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()),*Hit.ImpactPoint.ToString(),*Hit.ImpactNormal.ToString());return false;}
        }
        Previous=Next;Time=T;
    }
    const float SimulatedMiss=FVector::Dist2D(SimulatedFeet,State.End);
    if(SimulatedMiss>LandingTolerance){Reason=FString::Printf(TEXT("native_air_control_cannot_reach_measured_landing predicted_miss=%.3f"),SimulatedMiss);return false;}
    Reason=FString::Printf(TEXT("native_jump_arc_clear flight=%.3f vertical_clearance_until=%.3f horizontal_arrival=%.3f predicted_miss=%.3f native_air_control=%.3f source_plane_return=%.3f measured_ledge_progress=%.3f required_ledge_progress=%.3f"),State.FlightSeconds,State.HorizontalStartSeconds,State.HorizontalArrivalSeconds,SimulatedMiss,Movement->AirControl,SourcePlaneReturnSeconds,ActualLedgeProgress,RequiredLedgeProgress);return true;
}
bool AHeliosBioStaticJumpLink::PlanPhysicalWalk(ACharacter* Character,const FAgentState& State,FString& Reason) const
{
    auto* Movement=Character->GetCharacterMovement();auto* Capsule=Character->GetCapsuleComponent();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BioStaticRoutePhysicalWalk),false,Character);TArray<AActor*> Attached;Character->GetAttachedActors(Attached,true,true);Query.AddIgnoredActors(Attached);
    FCollisionObjectQueryParams Static(ECC_WorldStatic);const FCollisionShape Shape=FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight());
    const FVector Start=Character->GetNavAgentLocation();const int32 Samples=FMath::Max(2,FMath::CeilToInt(FVector::Dist2D(Start,State.End)/5.f));FVector Previous=Character->GetActorLocation();
    for(int32 Index=0;Index<=Samples;++Index)
    {
        const FVector Point=FMath::Lerp(Start,State.End,float(Index)/Samples);FHitResult Floor;
        if(!GetWorld()->LineTraceSingleByObjectType(Floor,Point+FVector(0,0,12),Point-FVector(0,0,12),Static,Query)||!Floor.GetComponent()||Floor.GetComponent()->Mobility!=EComponentMobility::Static||Floor.ImpactNormal.Z<Movement->GetWalkableFloorZ()||FMath::Abs(Floor.ImpactPoint.Z-(Point.Z-2.f))>8.f)
        {Reason=FString::Printf(TEXT("physical_walk_source_floor_gap_or_unwalkable sample=%d point=%s"),Index,*Point.ToString());return false;}
        const FVector Next=FVector(Point.X,Point.Y,Floor.ImpactPoint.Z+2.f+Capsule->GetScaledCapsuleHalfHeight());FHitResult Hit;
        if(GetWorld()->SweepSingleByChannel(Hit,Previous,Next,FQuat::Identity,ECC_Pawn,Shape,Query))
        {Reason=FString::Printf(TEXT("physical_walk_full_capsule_blocked sample=%d actor=%s impact=%s normal=%s"),Index,*GetNameSafe(Hit.GetActor()),*Hit.ImpactPoint.ToString(),*Hit.ImpactNormal.ToString());return false;}
        Previous=Next;
    }
    Reason=FString::Printf(TEXT("physical_walk_preflight_clear samples=%d native_perching_and_ground_contact_still_required"),Samples+1);return true;
}
void AHeliosBioStaticJumpLink::CompleteSegment(ACharacter* Character,const FString& Reason)
{
    auto* State=Agents.Find(Character);if(!State)return;auto& D=JumpEvidence.FindOrAdd(Character);++D.CompletedSegments;
    UE_LOG(LogTemp,Display,TEXT("BIO_STATIC_PHYSICAL_SEGMENT_COMPLETED link=%s actor=%s segment=%d walk=%d actual_feet=%s target=%s reason=%s"),*GetName(),*GetNameSafe(Character),State->RouteSegment,State->WalkSegments[State->RouteSegment],*Character->GetNavAgentLocation().ToString(),*State->End.ToString(),*Reason);
    if(++State->RouteSegment>=State->WalkSegments.Num()){FinishTraversal(Character,true,Reason);return;}
    Character->StopJumping();Character->ConsumeMovementInputVector();Character->GetCharacterMovement()->StopMovementImmediately();
    State->Start=State->RoutePoints[State->RouteSegment];State->End=State->RoutePoints[State->RouteSegment+1];State->SegmentAge=0;State->FlightAge=0;State->FlightSeconds=0;State->HorizontalStartSeconds=0;State->HorizontalArrivalSeconds=0;State->bJumpIssued=false;State->bAirborneObserved=false;State->bHorizontalStarted=false;State->bWalkStarted=false;
    D.RouteSegment=State->RouteSegment;D.SegmentStart=State->Start;D.SegmentEnd=State->End;D.Step=0;D.Reason=TEXT("aligning_to_next_measured_physical_segment");
}
void AHeliosBioStaticJumpLink::FinishTraversal(ACharacter* Character,bool Landed,const FString& Reason)
{
    auto* Found=Agents.Find(Character);if(!Found)return;const FAgentState State=*Found;Agents.Remove(Character);JumpOwners.Remove(Character);
    auto& D=JumpEvidence.FindOrAdd(Character);D.bTraversing=false;D.Step=Landed?2:3;D.Reason=Reason;if(Landed)++D.Landings;else ++D.Failures;
    if(!Landed&&Character)RetryAfter.Add(Character,GetWorld()->GetTimeSeconds()+3.f);
    if(IsValid(Character))
    {
        Character->StopJumping();Character->ConsumeMovementInputVector();
        // Only a physically landed character can have its ground velocity reset.
        if(Character->GetCharacterMovement()->IsMovingOnGround())Character->GetCharacterMovement()->StopMovementImmediately();
        if(auto* Path=State.PathFollowing.Get())
        {
            if(Landed){ResumePathFollowing(Character);Path->ResumeMove(State.RequestId);}
            else Path->AbortMove(*this,FPathFollowingResultFlags::UserAbort|FPathFollowingResultFlags::InvalidPath,State.RequestId,EPathFollowingVelocityMode::Keep);
        }
    }
    UE_LOG(LogTemp,Display,TEXT("BIO_STATIC_JUMP_%s link=%s actor=%s %s"),Landed?TEXT("LANDED"):TEXT("FAILED"),*GetName(),*GetNameSafe(Character),*DebugState(Character));
}
void AHeliosBioStaticJumpLink::Tick(float Dt)
{
    Super::Tick(Dt);if(GetWorld()->IsPaused())return;
    TArray<TWeakObjectPtr<ACharacter>> Keys;Agents.GenerateKeyArray(Keys);
    for(const auto& Key:Keys)
    {
        auto* Character=Key.Get();auto* State=Agents.Find(Key);if(!State)continue;
        if(!Character||Character->ActorHasTag(TEXT("Dead"))){FinishTraversal(Character,false,TEXT("actor_missing_or_dead"));Agents.Remove(Key);JumpOwners.Remove(Key);continue;}
        if(auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld()))
        {
            const auto* P=Mode->Model.FindParticipant(Mode->ParticipantId(Character));
            if((State->RoundSerial!=INDEX_NONE&&State->RoundSerial!=Mode->RoundSerial)||(P&&(!P->bAlive||P->FrozenSeconds>0||Mode->Model.GetPhase()==EHeliosBioPhase::RoundOver))){FinishTraversal(Character,false,TEXT("participant_dead_frozen_round_over_or_changed"));continue;}
        }
        auto* Movement=Character->GetCharacterMovement();State->Age+=Dt;State->SegmentAge+=Dt;auto& D=JumpEvidence.FindOrAdd(Character);D.Age=State->Age;
        if(State->SegmentAge>5.f){FinishTraversal(Character,false,TEXT("native_physical_segment_or_alignment_timeout"));continue;}
        if(State->WalkSegments[State->RouteSegment])
        {
            if(!Movement->IsMovingOnGround()){FinishTraversal(Character,false,TEXT("physical_wall_walk_lost_native_ground_contact"));continue;}
            if(!State->bWalkStarted)
            {
                if(FVector::Dist2D(Character->GetNavAgentLocation(),State->Start)>AlignmentTolerance){Character->AddMovementInput((State->Start-Character->GetActorLocation()).GetSafeNormal2D(),.25f);continue;}
                FString Reason;if(!PlanPhysicalWalk(Character,*State,Reason)){FinishTraversal(Character,false,Reason);continue;}
                State->bWalkStarted=true;D.Reason=Reason;D.Step=4;
            }
            FHitResult Floor;FCollisionQueryParams Query(SCENE_QUERY_STAT(BioStaticRouteGroundContact),false,Character);FCollisionObjectQueryParams Static(ECC_WorldStatic);
            const FVector Feet=Character->GetNavAgentLocation();
            if(!GetWorld()->LineTraceSingleByObjectType(Floor,Feet+FVector(0,0,8),Feet-FVector(0,0,12),Static,Query)||!Floor.GetComponent()||Floor.GetComponent()->Mobility!=EComponentMobility::Static||Floor.ImpactNormal.Z<Movement->GetWalkableFloorZ()||FMath::Abs(Feet.Z-State->End.Z)>12.f)
            {FinishTraversal(Character,false,TEXT("physical_wall_walk_has_no_measured_layer_ground_contact"));continue;}
            ++D.GroundedWalkSamples;
            if(FVector::Dist2D(Feet,State->End)<=PhysicalWalkEndTolerance){CompleteSegment(Character,TEXT("native_grounded_wall_walk_reached_measured_endpoint"));continue;}
            Character->AddMovementInput((State->End-Feet).GetSafeNormal2D(),PhysicalWalkInputScale);continue;
        }
        if(!State->bJumpIssued)
        {
            if(!Movement->IsMovingOnGround())continue;
            if(FVector::Dist2D(Character->GetNavAgentLocation(),State->Start)>AlignmentTolerance)
            {Character->AddMovementInput((State->Start-Character->GetActorLocation()).GetSafeNormal2D(),JumpAlignmentInputScale);continue;}
            FString Reason;if(!PlanNativeJump(Character,*State,Reason)){FinishTraversal(Character,false,Reason);continue;}
            D.PredictedFlightSeconds=State->FlightSeconds;D.Reason=Reason;D.Step=1;D.bJumpIssued=true;State->bJumpIssued=true;
            Movement->StopMovementImmediately();Character->ConsumeMovementInputVector();Character->Jump();
            UE_LOG(LogTemp,Display,TEXT("BIO_STATIC_JUMP_NATIVE_JUMP link=%s actor=%s %s"),*GetName(),*Character->GetName(),*DebugState(Character));continue;
        }
        State->FlightAge+=Dt;if(Movement->IsFalling()&&!State->bAirborneObserved)Character->StopJumping();State->bAirborneObserved|=Movement->IsFalling();D.bAirborneObserved|=State->bAirborneObserved;
        if(Movement->IsFalling())
        {
            // Native acceleration/air control and collision resolve the motion.
            // Input aims for the actual endpoint; there is no direct velocity write.
            State->bHorizontalStarted|=State->HorizontalStartSeconds<=0||Character->GetNavAgentLocation().Z>=State->End.Z;
            if(State->bHorizontalStarted)
            {
                const FVector Input=State->End.Z<State->Start.Z?NativeSteeringInput(Movement,Character->GetNavAgentLocation(),Movement->Velocity,State->End,State->HorizontalArrivalSeconds-State->FlightAge,Dt,true):NativeAirInputToLanding(Character,State->End,FVector::ZeroVector,Dt);
                Character->AddMovementInput(Input.GetSafeNormal2D(),FMath::Min(1.f,Input.Size2D()));
            }
        }
        else if(State->bAirborneObserved&&Movement->IsMovingOnGround())
        {
            const float Distance=FVector::Dist(Character->GetNavAgentLocation(),State->End);
            const FString Reason=FString::Printf(TEXT("actual_grounded_landing_distance=%.3f tolerance=%.3f"),Distance,LandingTolerance);
            if(Distance<=LandingTolerance)CompleteSegment(Character,Reason);else FinishTraversal(Character,false,Reason);
        }
        else if(State->FlightAge>.4f&&!State->bAirborneObserved)FinishTraversal(Character,false,TEXT("native_jump_did_not_enter_falling"));
    }
    for(auto It=JumpEvidence.CreateIterator();It;++It)if(!It.Key().IsValid())It.RemoveCurrent();
    for(auto It=JumpOwners.CreateIterator();It;++It)if(!It.Key().IsValid()||!It.Value().IsValid())It.RemoveCurrent();
    for(auto It=RetryAfter.CreateIterator();It;++It)if(!It.Key().IsValid()||GetWorld()->GetTimeSeconds()>=It.Value())It.RemoveCurrent();
}
void AHeliosBioStaticJumpLink::EndPlay(const EEndPlayReason::Type Reason)
{
    TArray<TWeakObjectPtr<ACharacter>> Keys;Agents.GenerateKeyArray(Keys);for(const auto& Key:Keys)if(auto* Character=Key.Get())FinishTraversal(Character,false,TEXT("link_ended"));
    Super::EndPlay(Reason);
}
