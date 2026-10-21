#include "Bio/HeliosBioPlatformTraversal.h"
#include "Bio/HeliosBioMovingPlatform.h"
#include "Bio/HeliosBioStaticJumpLink.h"
#include "Bio/HeliosBioHotelGameMode.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavLinkCustomComponent.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

namespace HeliosBioPlatformTraversal
{
namespace
{
    // Keep the original diagnostic values; waiting is an additional state.
    enum class EStep : uint8 { Approach,Boarding,Riding,Departing,Cooldown,Waiting };
    struct FPathTravelTime
    {
        float GroundWalk=0,StaticJump=0,WallWalk=0,Alignment=0;
        int32 StaticLinks=0;
        bool bComplete=false;
        FString Segments;
        float Total() const { return GroundWalk+StaticJump+WallWalk+Alignment; }
    };
    struct FPlatformRouteTime
    {
        float BoardingFlight=0,Wait=0,Carry=0,DepartureFlight=0,Total=-1;
    };
    struct FBotState
    {
        TWeakObjectPtr<AHeliosBioHotelGameMode> Mode;
        TWeakObjectPtr<AHeliosBioMovingPlatform> Platform;
        TWeakObjectPtr<AActor> Target;
        int32 Round = INDEX_NONE;
        EStep Step = EStep::Approach;
        float Age = 0.f,RequestAge = 1.f;
        FVector ApproachPoint = FVector::ZeroVector;
        FVector DeparturePoint = FVector::ZeroVector;
        FVector FlightLandingLocal = FVector::ZeroVector;
        TWeakObjectPtr<UPrimitiveComponent> FlightLandingComponent;
        bool bFlightAirborneObserved=false,bDepartureLandingVerified=false;
        bool bGlobalRoute = false, bHasDeparturePoint = false;
        FString LastReason = TEXT("new_request");
        float DiagnosticAge = 1.f;
        int32 LastMoveResult = INDEX_NONE;
        TWeakObjectPtr<UCharacterMovementComponent> BallisticMovement;
        float SavedFallingLateralFriction = 0.f,SavedBrakingDecelerationFalling = 0.f;
        bool bFlightSettingsSaved = false;
        bool bTactical=false,bAvoidanceSaved=false,bSavedAvoidance=false;
        FTraversalRequest Request;
        FPathTravelTime GroundTime,ApproachTime,DepartureTime;
        FPlatformRouteTime PlatformTime;
        int32 EntryIndex=INDEX_NONE,ExitIndex=INDEX_NONE;
        float GroundSeconds=-1.f,RouteSeconds=-1.f,WaitDeadline=0.f,EvaluateAge=1.f;
        float ProgressAge=0.f,StalledSeconds=0.f,JumpCheckAge=1.f;
        FVector ProgressPosition=FVector::ZeroVector;
        FVector YieldPoint=FVector::ZeroVector;
        bool bYielding=false;
    };
    TMap<TWeakObjectPtr<ACharacter>,FBotState> States;
    struct FTraversalTotals { int32 Round=INDEX_NONE,Boardings=0,Departures=0; };
    TMap<TWeakObjectPtr<ACharacter>,FTraversalTotals> Totals;
    FStandaloneTuning Settings;

    void RestoreAvoidance(ACharacter* Bot,FBotState& State)
    {
        if(Bot&&State.bAvoidanceSaved)Bot->GetCharacterMovement()->SetAvoidanceEnabled(State.bSavedAvoidance);
        State.bAvoidanceSaved=false;
    }

    void RestoreFlightSettings(FBotState& State)
    {
        if(!State.bFlightSettingsSaved)return;
        if(auto* Movement=State.BallisticMovement.Get())
        {
            Movement->FallingLateralFriction=State.SavedFallingLateralFriction;
            Movement->BrakingDecelerationFalling=State.SavedBrakingDecelerationFalling;
            UE_LOG(LogTemp,Display,TEXT("BIO_PLATFORM_FLIGHT_SETTINGS_RESTORED bot=%s friction=%.3f braking=%.3f"),*GetNameSafe(Movement->GetOwner()),State.SavedFallingLateralFriction,State.SavedBrakingDecelerationFalling);
        }
        State.bFlightSettingsSaved=false;State.BallisticMovement.Reset();
    }
    void PrepareFlightSettings(ACharacter* Bot,FBotState& State)
    {
        auto* Movement=Bot->GetCharacterMovement();
        if(!State.bFlightSettingsSaved)
        {
            State.BallisticMovement=Movement;
            State.SavedFallingLateralFriction=Movement->FallingLateralFriction;
            State.SavedBrakingDecelerationFalling=Movement->BrakingDecelerationFalling;
            State.bFlightSettingsSaved=true;
        }
        // Only a collision-approved ballistic flight suspends horizontal braking.
        Movement->FallingLateralFriction=0.f;Movement->BrakingDecelerationFalling=0.f;
        UE_LOG(LogTemp,Display,TEXT("BIO_PLATFORM_FLIGHT_SETTINGS_SAVED bot=%s friction=%.3f braking=%.3f"),*Bot->GetName(),State.SavedFallingLateralFriction,State.SavedBrakingDecelerationFalling);
    }

    void StartFlight(ACharacter* Bot,FBotState& State,const FVector& WorldLaunch,const FVector& LandingFeet,UPrimitiveComponent* LandingComponent)
    {
        auto* Movement=Bot->GetCharacterMovement();
        State.FlightLandingComponent=LandingComponent;
        State.FlightLandingLocal=LandingComponent?LandingComponent->GetComponentTransform().InverseTransformPosition(LandingFeet):LandingFeet;
        State.bFlightAirborneObserved=false;State.bDepartureLandingVerified=false;
        // Entering Falling natively adds the departing base velocity after the
        // pending launch. Compensate that addition so actual world velocity
        // equals the capsule-swept plan, without changing physics settings.
        const FVector Imparted=Movement->IsMovingOnGround()?Movement->GetImpartedMovementBaseVelocity():FVector::ZeroVector;
        Bot->ConsumeMovementInputVector();PrepareFlightSettings(Bot,State);
        Bot->LaunchCharacter(WorldLaunch-Imparted,true,true);
        UE_LOG(LogTemp,Display,TEXT("BIO_PLATFORM_WORLD_LAUNCH bot=%s planned_world=%s imparted_base=%s requested=%s landing=%s component=%s"),*Bot->GetName(),*WorldLaunch.ToString(),*Imparted.ToString(),*(WorldLaunch-Imparted).ToString(),*LandingFeet.ToString(),*GetNameSafe(LandingComponent));
    }
    void Diagnostic(ACharacter* Bot,FBotState& State,const FString& Reason)
    {
        State.LastReason=Reason;
        if(State.DiagnosticAge<.75f)return;
        State.DiagnosticAge=0.f;
        UE_LOG(LogTemp,Display,TEXT("BIO_PLATFORM_TRAVERSAL_DIAGNOSTIC bot=%s step=%d approach=%s distance2d=%.3f position=%s reason=%s"),*Bot->GetName(),int32(State.Step),*State.ApproachPoint.ToString(),FVector::Dist2D(Bot->GetActorLocation(),State.ApproachPoint),*Bot->GetActorLocation().ToString(),*Reason);
    }

    float FeetZ(const ACharacter* Bot)
    {return Bot->GetActorLocation().Z-Bot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();}
    FVector StaticTargetFeet(const AActor* Target)
    {if(const auto* Character=Cast<ACharacter>(Target))return Character->GetNavAgentLocation();return Target?Target->GetActorLocation():FVector::ZeroVector;}
    bool TopPoint(ACharacter* Bot,AHeliosBioMovingPlatform* Platform,FVector& Out,FBox& Bounds)
    {
        auto* Mesh=Platform?Platform->GetPlatformMesh():nullptr;
        if(!Mesh||!Mesh->GetStaticMesh()||!Mesh->IsCollisionEnabled())return false;
        FVector Low,High;Mesh->GetLocalBounds(Low,High);Bounds=FBox(ForceInit);
        for(int32 Bits=0;Bits<8;++Bits)Bounds+=Mesh->GetComponentTransform().TransformPosition(FVector(Bits&1?High.X:Low.X,Bits&2?High.Y:Low.Y,Bits&4?High.Z:Low.Z));
        const float Margin=Bot->GetCapsuleComponent()->GetScaledCapsuleRadius()+8.f;
        if(Bounds.GetSize().X<2*Margin||Bounds.GetSize().Y<2*Margin)return false;
        const FVector BotLocation=Bot->GetActorLocation();
        const FVector Probe(FMath::Clamp(BotLocation.X,Bounds.Min.X+Margin,Bounds.Max.X-Margin),FMath::Clamp(BotLocation.Y,Bounds.Min.Y+Margin,Bounds.Max.Y-Margin),Bounds.Max.Z);
        FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(BioPlatformActualUCXTop),false,Bot);
        if(auto* Mode=AHeliosBioHotelGameMode::Find(Bot->GetWorld()))for(const auto& Participant:Mode->Model.GetParticipants())Query.AddIgnoredActor(Mode->ParticipantActor(Participant.Id));
        const FVector RayStart=Probe+FVector(0,0,150),RayEnd=Probe-FVector(0,0,Bounds.GetSize().Z+150);
        if(!Bot->GetWorld()->LineTraceSingleByChannel(Hit,RayStart,RayEnd,ECC_Visibility,Query)||Hit.GetComponent()!=Mesh)
        {
            // Route discovery measures this base's real collision. World obstacles
            // above it are still tested by every boarding capsule preflight.
            if(!Mesh->LineTraceComponent(Hit,RayStart,RayEnd,Query))return false;
        }
        if(Hit.ImpactNormal.Z<Bot->GetCharacterMovement()->GetWalkableFloorZ())return false;
        Out=Hit.ImpactPoint;return true;
    }
    bool StaticNavFloor(ACharacter* Bot,UNavigationSystemV1* Nav,const FVector& Wanted,FVector& Out)
    {
        FNavLocation Projected;
        if(!Nav||!Nav->ProjectPointToNavigation(Wanted,Projected,FVector(70,70,180),&Bot->GetNavAgentPropertiesRef()))return false;
        FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(BioPlatformStaticDockFloor),false,Bot);
        if(auto* Mode=AHeliosBioHotelGameMode::Find(Bot->GetWorld()))for(const auto& Participant:Mode->Model.GetParticipants())Query.AddIgnoredActor(Mode->ParticipantActor(Participant.Id));
        FCollisionObjectQueryParams Static(ECC_WorldStatic);
        if(!Bot->GetWorld()->LineTraceSingleByObjectType(Hit,Projected.Location+FVector(0,0,70),Projected.Location-FVector(0,0,180),Static,Query)||!Hit.GetComponent()||Hit.GetComponent()->Mobility!=EComponentMobility::Static||Hit.ImpactNormal.Z<Bot->GetCharacterMovement()->GetWalkableFloorZ())return false;
        if(FVector::DistSquared2D(Projected.Location,Hit.ImpactPoint)>FMath::Square(60.f)||FMath::Abs(Projected.Location.Z-Hit.ImpactPoint.Z)>70.f)return false;
        Out=Hit.ImpactPoint;return true;
    }
    bool ReachableStaticApproach(ACharacter* Bot,UNavigationSystemV1* Nav,const FVector& Point)
    {
        FNavLocation Start;
        if(!Nav->ProjectPointToNavigation(Bot->GetNavAgentLocation(),Start,FVector(80,80,150),&Bot->GetNavAgentPropertiesRef()))return false;
        auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(Bot->GetWorld(),Start.Location,Point,Bot);
        return Path&&Path->IsValid()&&!Path->IsPartial();
    }
    bool DockPoint(ACharacter* Bot,AActor* Target,UNavigationSystemV1* Nav,const FBox& Bounds,bool Departing,FVector& Out)
    {
        const float Radius=Bot->GetCapsuleComponent()->GetScaledCapsuleRadius();
        const FVector Center=Bounds.GetCenter(),Extent=Bounds.GetExtent();
        float Best=MAX_flt;bool Found=false;
        for(int32 Sample=0;Sample<16;++Sample)
        {
            const float Angle=2*PI*Sample/16.f;const FVector Direction(FMath::Cos(Angle),FMath::Sin(Angle),0);
            const FVector Wanted=Center+FVector(Direction.X*(Extent.X+Radius+90.f),Direction.Y*(Extent.Y+Radius+90.f),0);
            FVector Point;if(!StaticNavFloor(Bot,Nav,Wanted,Point))continue;
            if(Bounds.IsInsideOrOn(FVector(Point.X,Point.Y,Bounds.GetCenter().Z)))continue;
            if(FVector::DistSquared2D(Point,Bot->GetActorLocation())>FMath::Square(Departing?Settings.MaxBoardingDistance:Settings.SearchRadius))continue;
            if(!Departing&&!ReachableStaticApproach(Bot,Nav,Point))continue;
            const float Score=Departing?FVector::DistSquared2D(Point,Target->GetActorLocation()):FVector::DistSquared2D(Point,Bot->GetActorLocation());
            if(Score<Best){Best=Score;Out=Point;Found=true;}
        }
        return Found;
    }
    struct FRouteDockCache
    {
        int32 Round=INDEX_NONE;
        float Radius=0.f;
        TArray<TArray<FVector>> Shores;
    };
    TMap<TWeakObjectPtr<AHeliosBioMovingPlatform>,FRouteDockCache> RouteDocks;

    float NativeWalkTimeFromRest(float Distance,float Speed,float Acceleration)
    {
        if(Distance<=0.f)return 0.f;if(Speed<=0.f||Acceleration<=0.f)return MAX_flt;
        const float AccelerationTime=Speed/Acceleration,AccelerationDistance=.5f*Acceleration*AccelerationTime*AccelerationTime;
        return Distance<=AccelerationDistance?FMath::Sqrt(2.f*Distance/Acceleration):AccelerationTime+(Distance-AccelerationDistance)/Speed;
    }
    bool EstimateCompleteNativePath(ACharacter* Bot,UNavigationPath* Path,FPathTravelTime& Time)
    {
        Time={};const auto Native=Path->GetPath();if(!Native.IsValid()||!Native->IsValid()||Native->IsPartial())return false;
        const auto& Points=Native->GetPathPoints();if(Points.IsEmpty())return false;
        TSet<FNavLinkId> CorridorLinks;
        if(const auto* Corridor=Native->CastPath<FNavMeshPath>())if(const auto* Recast=Cast<ARecastNavMesh>(Corridor->GetNavigationDataUsed()))
            for(NavNodeRef Ref:Corridor->PathCorridor){const FNavLinkId Id=Recast->GetNavLinkUserId(Ref);if(Id.IsValid())CorridorLinks.Add(Id);}
        const auto* Movement=Bot->GetCharacterMovement();const float Speed=Movement->MaxWalkSpeed,Gravity=-Movement->GetGravityZ(),JumpZ=Movement->JumpZVelocity;
        if(Speed<=0.f||Gravity<=0.f||JumpZ<=0.f)return false;
        for(int32 I=0;I+1<Points.Num();++I)
        {
            const auto& Point=Points[I];const FVector Next=Points[I+1].Location;const bool HasId=Point.CustomNavLinkId.IsValid(),OffMesh=FNavMeshNodeFlags(Point.Flags).IsNavLink();
            AHeliosBioStaticJumpLink* Matched=nullptr;bool Forward=true;
            for(TActorIterator<AHeliosBioStaticJumpLink> It(Bot->GetWorld());It;++It)
            {
                if(!It->IsSmartLinkEnabled()||!It->GetSmartLinkComp())continue;
                const FNavLinkId Id=It->GetSmartLinkComp()->GetId();if(HasId&&Point.CustomNavLinkId!=Id)continue;
                if(!HasId&&!OffMesh&&!CorridorLinks.Contains(Id)&&!Native->ContainsCustomLink(Id))continue;
                const FVector A=It->GetStartWorldFeet(),B=It->GetEndWorldFeet();
                const bool Toward=FVector::DistSquared(Point.Location,A)+FVector::DistSquared(Next,B)<=FVector::DistSquared(Point.Location,B)+FVector::DistSquared(Next,A);
                if(FVector::Dist(Point.Location,Toward?A:B)>45.f||FVector::Dist(Next,Toward?B:A)>45.f)continue;
                if(Matched){Time.Segments=TEXT("ambiguous_native_registered_link_endpoints");return false;}Matched=*It;Forward=Toward;
            }
            if(!Matched)
            {
                if(HasId||OffMesh){Time.Segments=TEXT("unsupported_native_offmesh_link_not_timed_as_walk");return false;}
                Time.GroundWalk+=FVector::Dist(Point.Location,Next)/Speed;continue;
            }
            const auto& Route=Matched->GetPhysicalRouteWorldFeet();const auto& Walk=Matched->GetPhysicalWalkSegments();if(Route.Num()<2||Walk.Num()!=Route.Num()-1)return false;
            const FVector LinkStart=Forward?Route[0]:Route.Last();
            const float AlignmentDistance=FMath::Max(0.f,FVector::Dist2D(Point.Location,LinkStart)-AHeliosBioStaticJumpLink::JumpAlignmentTolerance);
            Time.Alignment+=NativeWalkTimeFromRest(AlignmentDistance,FMath::Max(Movement->MinAnalogWalkSpeed,Speed*AHeliosBioStaticJumpLink::JumpAlignmentInputScale),Movement->MaxAcceleration*AHeliosBioStaticJumpLink::JumpAlignmentInputScale);
            ++Time.StaticLinks;
            for(int32 Segment=0;Segment<Walk.Num();++Segment)
            {
                const int32 A=Forward?Segment:Route.Num()-1-Segment,B=Forward?Segment+1:Route.Num()-2-Segment,Flag=Forward?Segment:Walk.Num()-1-Segment;
                float Seconds=0;
                if(Walk[Flag])
                {
                    const float Distance=FMath::Max(0.f,FVector::Dist2D(Route[A],Route[B])-AHeliosBioStaticJumpLink::PhysicalWalkEndTolerance);
                    Seconds=NativeWalkTimeFromRest(Distance,FMath::Max(Movement->MinAnalogWalkSpeed,Speed*AHeliosBioStaticJumpLink::PhysicalWalkInputScale),Movement->MaxAcceleration*AHeliosBioStaticJumpLink::PhysicalWalkInputScale);Time.WallWalk+=Seconds;
                }
                else
                {
                    const float Height=Route[B].Z-Route[A].Z,Disc=JumpZ*JumpZ-2.f*Gravity*Height;if(Disc<0.f){Time.Segments=TEXT("registered_link_exceeds_actual_native_jump_height");return false;}
                    Seconds=(JumpZ+FMath::Sqrt(Disc))/Gravity;Time.StaticJump+=Seconds;
                }
                Time.Segments+=FString::Printf(TEXT("%s%s[%d] %s->%s %s=%.3fs"),Time.Segments.IsEmpty()?TEXT(""):TEXT("; "),*Matched->GetName(),Segment,*Route[A].ToString(),*Route[B].ToString(),Walk[Flag]?TEXT("native_wall_walk"):TEXT("native_jump"),Seconds);
            }
        }
        Time.bComplete=FMath::IsFinite(Time.Total());return Time.bComplete;
    }
    bool CompleteStaticPath(ACharacter* Bot,UNavigationSystemV1* Nav,const FVector& From,const FVector& To,float& Length,float HeightTolerance=140.f,FPathTravelTime* Travel=nullptr)
    {
        FNavLocation Start,End;
        if(!Nav->ProjectPointToNavigation(From,Start,FVector(100,100,180),&Bot->GetNavAgentPropertiesRef())||
           !Nav->ProjectPointToNavigation(To,End,FVector(100,100,180),&Bot->GetNavAgentPropertiesRef()))return false;
        if(FMath::Abs(End.Location.Z-To.Z)>HeightTolerance||
            (HeightTolerance<140.f&&FMath::Abs(Start.Location.Z-From.Z)>HeightTolerance))return false;
        auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(Bot->GetWorld(),Start.Location,End.Location,Bot);
        if(!Path||!Path->IsValid()||Path->IsPartial())return false;
        Length=Path->GetPathLength();if(Travel)EstimateCompleteNativePath(Bot,Path,*Travel);return true;
    }
    const TArray<TArray<FVector>>& AuthoredRouteShores(ACharacter* Bot,UNavigationSystemV1* Nav,AHeliosBioMovingPlatform* Platform,const FBox& CurrentBounds,int32 Round)
    {
        auto& Cache=RouteDocks.FindOrAdd(Platform);
        const float Radius=Bot->GetCapsuleComponent()->GetScaledCapsuleRadius();
        if(Cache.Round==Round&&FMath::IsNearlyEqual(Cache.Radius,Radius)&&!Cache.Shores.IsEmpty())return Cache.Shores;
        Cache.Round=Round;Cache.Radius=Radius;Cache.Shores.Reset();
        const auto& Points=Platform->GetRoutePoints();
        for(const FVector& RoutePoint:Points)
        {
            auto& Shores=Cache.Shores.AddDefaulted_GetRef();
            const FBox Bounds=CurrentBounds.ShiftBy(RoutePoint-Platform->GetActorLocation());
            const FVector Center=Bounds.GetCenter(),Extent=Bounds.GetExtent();
            for(int32 Sample=0;Sample<16;++Sample)
            {
                const float Angle=2*PI*Sample/16.f;const FVector Direction(FMath::Cos(Angle),FMath::Sin(Angle),0);
                const FVector Wanted=Center+FVector(Direction.X*(Extent.X+Radius+90.f),Direction.Y*(Extent.Y+Radius+90.f),0);
                FVector Shore;if(!StaticNavFloor(Bot,Nav,Wanted,Shore))continue;
                if(Bounds.IsInsideOrOn(FVector(Shore.X,Shore.Y,Center.Z)))continue;
                Shores.Add(Shore);
            }
        }
        return Cache.Shores;
    }
    bool ReachableRouteDock(ACharacter* Bot,UNavigationSystemV1* Nav,const TArray<FVector>& Shores,const FVector& Endpoint,bool Departing,FVector& Best,float& Cost)
    {
        TArray<FVector> Sorted=Shores;
        Sorted.Sort([&Endpoint](const FVector& A,const FVector& B){return FVector::DistSquared2D(A,Endpoint)<FVector::DistSquared2D(B,Endpoint);});
        Cost=MAX_flt;bool Found=false;
        // Adjacent shore probes normally share a nav island. Bound synchronous queries.
        for(int32 Index=0;Index<FMath::Min(4,Sorted.Num());++Index)
        {
            float Length=0.f;
            if(!CompleteStaticPath(Bot,Nav,Departing?Sorted[Index]:Endpoint,Departing?Endpoint:Sorted[Index],Length))continue;
            if(Length<Cost){Best=Sorted[Index];Cost=Length;Found=true;}
        }
        return Found;
    }
    bool FindGlobalRoute(ACharacter* Bot,AActor* Target,UNavigationSystemV1* Nav,AHeliosBioHotelGameMode* Mode,AHeliosBioMovingPlatform*& OutPlatform,FVector& Entry,FVector& Exit,bool& HasExit,bool& PreferGround)
    {
        const FVector TargetFeet=StaticTargetFeet(Target);float DirectCost=0.f;const bool Direct=CompleteStaticPath(Bot,Nav,Bot->GetNavAgentLocation(),TargetFeet,DirectCost);
        const auto* TargetCharacter=Cast<ACharacter>(Target);float BestCost=MAX_flt;
        bool TargetOnSourcePlatform=false;for(auto Platform:Mode->MovingPlatforms)TargetOnSourcePlatform|=IsValid(Platform)&&TargetCharacter&&TargetCharacter->GetMovementBase()==Platform->GetPlatformMesh();
        PreferGround=Direct&&!TargetOnSourcePlatform&&FMath::Abs(TargetFeet.Z-Bot->GetNavAgentLocation().Z)<140.f&&DirectCost<=FVector::Dist2D(Bot->GetNavAgentLocation(),TargetFeet)*1.35f+300.f;
        if(PreferGround)return false;
        for(auto Platform:Mode->MovingPlatforms)
        {
            if(!IsValid(Platform)||!Platform->IsRouteConfigured())continue;
            FVector Top;FBox Bounds;if(!TopPoint(Bot,Platform,Top,Bounds))continue;
            const bool TargetAboard=TargetCharacter&&TargetCharacter->GetMovementBase()==Platform->GetPlatformMesh();
            const auto& Points=Platform->GetRoutePoints();
            const auto& Shores=AuthoredRouteShores(Bot,Nav,Platform,Bounds,Mode->RoundSerial);
            for(int32 Start=0;Start<Shores.Num();++Start)
            {
                FVector Board;float ApproachCost;
                if(!ReachableRouteDock(Bot,Nav,Shores[Start],Bot->GetNavAgentLocation(),false,Board,ApproachCost)||ApproachCost>Settings.RouteSearchRadius)continue;
                if(TargetAboard)
                {if(ApproachCost<BestCost){BestCost=ApproachCost;OutPlatform=Platform;Entry=Board;HasExit=false;}continue;}
                for(int32 End=0;End<Shores.Num();++End)
                {
                    if(Start==End)continue;
                    FVector Depart;float DepartureCost;
                    if(!ReachableRouteDock(Bot,Nav,Shores[End],TargetFeet,true,Depart,DepartureCost))continue;
                    float RouteDistance=0.f;for(int32 I=FMath::Min(Start,End);I<FMath::Max(Start,End);++I)RouteDistance+=FVector::Dist(Points[I],Points[I+1]);
                    const float Cost=ApproachCost+RouteDistance+DepartureCost;
                    if(Direct&&Cost+250.f>=DirectCost)continue;
                    if(Cost<BestCost){BestCost=Cost;OutPlatform=Platform;Entry=Board;Exit=Depart;HasExit=true;}
                }
            }
        }
        return OutPlatform!=nullptr;
    }
    bool RefreshConnectedDeparture(ACharacter* Bot,AActor* Target,UNavigationSystemV1* Nav,AHeliosBioMovingPlatform* Platform,const FBox& Bounds,int32 Round,FVector& Out)
    {
        const auto& Shores=AuthoredRouteShores(Bot,Nav,Platform,Bounds,Round);const auto& Points=Platform->GetRoutePoints();
        float Best=MAX_flt;bool Found=false;
        for(int32 Index=0;Index<Shores.Num();++Index)
        {
            FVector Shore;float StaticDistance;
            if(!ReachableRouteDock(Bot,Nav,Shores[Index],StaticTargetFeet(Target),true,Shore,StaticDistance))continue;
            const float Cost=FVector::Dist(Platform->GetActorLocation(),Points[Index])+StaticDistance;
            if(Cost<Best){Best=Cost;Out=Shore;Found=true;}
        }
        return Found;
    }
    struct FTacticalRoute
    {
        AHeliosBioMovingPlatform* Platform=nullptr;
        FVector Entry=FVector::ZeroVector,Exit=FVector::ZeroVector;
        int32 EntryIndex=INDEX_NONE,ExitIndex=INDEX_NONE;
        float GroundSeconds=-1.f,TotalSeconds=MAX_flt,BoardSeconds=0.f;
        int32 Tops=0,Approaches=0,Candidates=0;
        FPathTravelTime GroundTime,ApproachTime,DepartureTime;
        FPlatformRouteTime PlatformTime;
    };
    bool WorthBoarding(float Ground,float Platform)
    {return HeliosBioPlatformSchedule::ShouldUsePlatform(Ground,Platform);}
    float DockCrowding(ACharacter* Bot,const FVector& Point)
    {
        float Penalty=0.f;
        for(const auto& Pair:States)if(Pair.Key.IsValid()&&Pair.Key.Get()!=Bot&&Pair.Value.Step!=EStep::Cooldown)
        {
            if(FVector::DistSquared(Pair.Key->GetNavAgentLocation(),Point)<FMath::Square(90.f))Penalty+=180.f;
            if(FVector::DistSquared(Pair.Value.ApproachPoint,Point)<FMath::Square(70.f))Penalty+=120.f;
        }
        return Penalty;
    }
    bool TacticalDock(ACharacter* Bot,UNavigationSystemV1* Nav,const TArray<FVector>& Shores,const FVector& Endpoint,bool Departing,FVector& Best,float& Length,FPathTravelTime* Time=nullptr)
    {
        TArray<FVector> Sorted=Shores;
        Sorted.Sort([&](const FVector& A,const FVector& B){return FVector::Dist2D(A,Endpoint)+DockCrowding(Bot,A)<FVector::Dist2D(B,Endpoint)+DockCrowding(Bot,B);});
        float BestCost=MAX_flt;bool Found=false;
        for(int32 I=0;I<FMath::Min(3,Sorted.Num());++I)
        {
            float Distance;FPathTravelTime Candidate;
            if(!CompleteStaticPath(Bot,Nav,Departing?Sorted[I]:Endpoint,Departing?Endpoint:Sorted[I],Distance,25.f,&Candidate)||!Candidate.bComplete)continue;
            const float Cost=Candidate.Total()+DockCrowding(Bot,Sorted[I])/FMath::Max(100.f,Bot->GetCharacterMovement()->MaxWalkSpeed);
            if(Cost<BestCost){BestCost=Cost;Length=Distance;Best=Sorted[I];Found=true;if(Time)*Time=Candidate;}
        }
        return Found;
    }
    bool PreferredPlatformFlightTime(ACharacter* Bot,float FromZ,float ToZ,float& Seconds)
    {
        const float Gravity=-Bot->GetCharacterMovement()->GetGravityZ(),Vertical=Settings.LaunchVerticalSpeed,Disc=Vertical*Vertical-2.f*Gravity*(ToZ-FromZ);
        if(Gravity<=0||Vertical<=0||Disc<0)return false;Seconds=(Vertical+FMath::Sqrt(Disc))/Gravity;
        return Seconds>=.15f&&Seconds<=Settings.FlightTimeout;
    }
    bool EstimatePlatformJourney(ACharacter* Bot,AHeliosBioMovingPlatform* Platform,int32 EntryIndex,int32 ExitIndex,const FVector& Entry,const FVector& Exit,float Approach,float DepartureGround,float& BoardAt,float& ExitAt,FPlatformRouteTime& Time)
    {
        Time={};const auto& Points=Platform->GetRoutePoints();if(!Points.IsValidIndex(EntryIndex)||(ExitIndex!=INDEX_NONE&&!Points.IsValidIndex(ExitIndex)))return false;
        FVector Top;FBox Bounds;if(!TopPoint(Bot,Platform,Top,Bounds))return false;
        const float TopOffset=Top.Z-Platform->GetActorLocation().Z;
        if(!PreferredPlatformFlightTime(Bot,Entry.Z,Points[EntryIndex].Z+TopOffset,Time.BoardingFlight))return false;
        if(ExitIndex!=INDEX_NONE&&!PreferredPlatformFlightTime(Bot,Points[ExitIndex].Z+TopOffset,Exit.Z,Time.DepartureFlight))return false;
        // Preserve the original scheduled dock visit. Boarding intercepts the
        // moving platform, so its flight overlaps the first part of the carry;
        // do not invent a missed full cycle by demanding a stationary dock.
        if(!Platform->PredictRouteJourney(EntryIndex,ExitIndex,Approach,BoardAt,ExitAt))return false;
        Time.Wait=FMath::Max(0.f,BoardAt-Approach);Time.Carry=FMath::Max(0.f,ExitAt-BoardAt-Time.BoardingFlight);
        Time.Total=Approach+Time.Wait+Time.BoardingFlight+Time.Carry+Time.DepartureFlight+DepartureGround;
        return FMath::IsFinite(Time.Total)&&Time.Total>=0;
    }
    bool FindTacticalRoute(ACharacter* Bot,UNavigationSystemV1* Nav,AHeliosBioHotelGameMode* Mode,const FTraversalRequest& Request,FTacticalRoute& Best)
    {
        if(!Request.bValid||Request.bUrgent)return false;
        float Ground;
        const auto* Opponent=Cast<ACharacter>(Request.CombatTarget.Get());
        const bool Pursuit=Request.Intent==TEXT("Chase")||Request.Intent==TEXT("Flank");
        bool Aboard=false;
        for(auto P:Mode->MovingPlatforms)Aboard|=IsValid(P)&&Pursuit&&Opponent&&Opponent->GetMovementBase()==P->GetPlatformMesh();
        if(!Aboard&&CompleteStaticPath(Bot,Nav,Bot->GetNavAgentLocation(),Request.DestinationFeet,Ground,25.f,&Best.GroundTime))
        {if(!Best.GroundTime.bComplete)return false;Best.GroundSeconds=Best.GroundTime.Total();}
        // An unobstructed short walking route cannot beat even the boarding/departure overhead.
        if(Best.GroundSeconds>=0.f&&Best.GroundSeconds<=2.5f)return false;
        for(auto Platform:Mode->MovingPlatforms)
        {
            if(!IsValid(Platform)||!Platform->IsRouteRunning())continue;
            FVector Top;FBox Bounds;if(!TopPoint(Bot,Platform,Top,Bounds))continue;
            ++Best.Tops;
            const auto& Shores=AuthoredRouteShores(Bot,Nav,Platform,Bounds,Mode->RoundSerial);
            const bool TargetAboard=Pursuit&&Opponent&&Opponent->GetMovementBase()==Platform->GetPlatformMesh();
            for(int32 Start=0;Start<Shores.Num();++Start)
            {
                FVector Entry;float Approach;FPathTravelTime ApproachTime;
                if(!TacticalDock(Bot,Nav,Shores[Start],Bot->GetNavAgentLocation(),false,Entry,Approach,&ApproachTime)||Approach>Settings.RouteSearchRadius)continue;
                ++Best.Approaches;
                for(int32 End=0;End<(TargetAboard?1:Shores.Num());++End)
                {
                    if(!TargetAboard&&End==Start)continue;
                    FVector Exit=FVector::ZeroVector;float Departure=0.f;FPathTravelTime DepartureTime;
                    if(!TargetAboard&&!TacticalDock(Bot,Nav,Shores[End],Request.DestinationFeet,true,Exit,Departure,&DepartureTime))continue;
                    float BoardAt,ExitAt;FPlatformRouteTime PlatformTime;
                    if(!EstimatePlatformJourney(Bot,Platform,Start,TargetAboard?INDEX_NONE:End,Entry,Exit,ApproachTime.Total(),DepartureTime.Total(),BoardAt,ExitAt,PlatformTime))continue;
                    const float Total=PlatformTime.Total;
                    ++Best.Candidates;
                    if(Total>=Best.TotalSeconds)continue;
                    Best.TotalSeconds=Total;Best.BoardSeconds=BoardAt;Best.ApproachTime=ApproachTime;Best.DepartureTime=DepartureTime;Best.PlatformTime=PlatformTime;
                    if(!WorthBoarding(Best.GroundSeconds,Total))continue;
                    Best.Platform=Platform;Best.Entry=Entry;Best.Exit=Exit;Best.EntryIndex=Start;Best.ExitIndex=TargetAboard?INDEX_NONE:End;
                }
            }
        }
        return Best.Platform!=nullptr;
    }
    bool RefreshTacticalDeparture(ACharacter* Bot,UNavigationSystemV1* Nav,AHeliosBioMovingPlatform* Platform,const FBox& Bounds,FBotState& State)
    {
        const auto& Shores=AuthoredRouteShores(Bot,Nav,Platform,Bounds,State.Round);
        float Best=MAX_flt;bool Found=false;FVector ActualTop;FBox ActualBounds;
        if(!TopPoint(Bot,Platform,ActualTop,ActualBounds))return false;
        for(int32 I=0;I<Shores.Num();++I)
        {
            FVector Shore;float Length,Arrival,Unused;FPathTravelTime DepartureTime;
            if(!TacticalDock(Bot,Nav,Shores[I],State.Request.DestinationFeet,true,Shore,Length,&DepartureTime)||
                !Platform->PredictRouteJourney(I,INDEX_NONE,0.f,Arrival,Unused))continue;
            float Flight=0;const auto& Points=Platform->GetRoutePoints();if(!Points.IsValidIndex(I)||!PreferredPlatformFlightTime(Bot,ActualTop.Z+Points[I].Z-Platform->GetActorLocation().Z,Shore.Z,Flight))continue;
            const float Cost=Arrival+Flight+DepartureTime.Total();
            if(Cost<Best){Best=Cost;State.DeparturePoint=Shore;State.ExitIndex=I;State.DepartureTime=DepartureTime;Found=true;}
        }
        return Found;
    }
    bool BoardPriority(ACharacter* Bot,const FBotState& State,ACharacter** OutLeader=nullptr)
    {
        for(const auto& Pair:States)
        {
            auto* Other=Pair.Key.Get();const auto& S=Pair.Value;
            if(!Other||Other==Bot||S.Platform!=State.Platform)continue;
            if(S.Step==EStep::Boarding||S.Step==EStep::Departing){if(OutLeader)*OutLeader=Other;return false;}
            if(S.Step==EStep::Waiting&&FVector::DistSquared2D(Other->GetActorLocation(),State.ApproachPoint)<FMath::Square(180.f)&&
                (S.Age>State.Age+.01f||(FMath::IsNearlyEqual(S.Age,State.Age,.01f)&&Other->GetUniqueID()<Bot->GetUniqueID())))
            {if(OutLeader)*OutLeader=Other;return false;}
        }
        return true;
    }
    void YieldBoardingLane(ACharacter* Bot,AAIController* AI,UNavigationSystemV1* Nav,ACharacter* Leader,const FVector& Top,FBotState& State)
    {
        if(!Leader){AI->StopMovement();return;}
        if(State.bYielding&&State.RequestAge<.5f)return;
        State.RequestAge=0.f;
        const auto* LeaderState=States.Find(Leader);
        const FVector Origin=LeaderState&&LeaderState->Step==EStep::Boarding?LeaderState->ApproachPoint:Leader->GetNavAgentLocation();
        const FVector Forward=(Top-Origin).GetSafeNormal2D(),Side(-Forward.Y,Forward.X,0);
        const float Radius=Bot->GetCapsuleComponent()->GetScaledCapsuleRadius(),Half=Bot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        const float Spacing=Radius+Leader->GetCapsuleComponent()->GetScaledCapsuleRadius()+25.f;
        FCollisionQueryParams Q(SCENE_QUERY_STAT(BioFloatingQueueYield),false,Bot);
        TArray<AActor*> Attached;Bot->GetAttachedActors(Attached,true,true);for(auto* A:Attached)Q.AddIgnoredActor(A);
        FVector Choice=FVector::ZeroVector;float Best=MAX_flt;
        for(float Back:{0.f,Spacing,Spacing*1.5f})for(float Offset:{-Spacing,Spacing,-Spacing*1.5f,Spacing*1.5f})
        {
            FVector At;if(!StaticNavFloor(Bot,Nav,Origin-Forward*Back+Side*Offset,At)||FMath::Abs(At.Z-Bot->GetNavAgentLocation().Z)>25.f||!ReachableStaticApproach(Bot,Nav,At))continue;
            // Keep the waiting capsule outside the leader's takeoff corridor.
            const FVector Delta=At-Origin;
            if(FMath::Abs(FVector::DotProduct(Delta,Side))<Spacing*.8f&&FVector::DotProduct(Delta,Forward)>-Spacing)continue;
            FHitResult Hit;const FVector Lift(0,0,Half+3.f);
            if(Bot->GetWorld()->SweepSingleByChannel(Hit,Bot->GetNavAgentLocation()+Lift,At+Lift,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Radius,Half-2.f),Q))continue;
            const float Cost=FVector::Dist2D(At,Bot->GetNavAgentLocation())+DockCrowding(Bot,At);
            if(Cost<Best){Best=Cost;Choice=At;}
        }
        if(Best<MAX_flt)
        {
            State.bYielding=true;State.YieldPoint=Choice;AI->MoveToLocation(Choice,12.f,false,true,false,false,nullptr,false);
            Diagnostic(Bot,State,TEXT("yielding_boarding_lane_to_earlier_arrival"));
        }
        else {AI->StopMovement();Diagnostic(Bot,State,TEXT("waiting_boarding_priority_no_clear_yield_path"));}
    }
    bool SpreadLanding(ACharacter* Bot,AHeliosBioMovingPlatform* Platform,const FBox& Bounds,FVector& Top)
    {
        const float Margin=Bot->GetCapsuleComponent()->GetScaledCapsuleRadius()+12.f;
        float Best=MAX_flt;bool Found=false;
        FCollisionQueryParams Q(SCENE_QUERY_STAT(BioFloatingLanding),false,Bot);
        if(auto* Mode=AHeliosBioHotelGameMode::Find(Bot->GetWorld()))for(const auto& P:Mode->Model.GetParticipants())Q.AddIgnoredActor(Mode->ParticipantActor(P.Id));
        for(int32 X=0;X<3;++X)for(int32 Y=0;Y<3;++Y)
        {
            const FVector Point(FMath::Lerp(Bounds.Min.X+Margin,Bounds.Max.X-Margin,X*.5f),FMath::Lerp(Bounds.Min.Y+Margin,Bounds.Max.Y-Margin,Y*.5f),Bounds.Max.Z);
            FHitResult Hit;if(!Bot->GetWorld()->LineTraceSingleByChannel(Hit,Point+FVector(0,0,80),Point-FVector(0,0,Bounds.GetSize().Z+80),ECC_Visibility,Q)||Hit.GetComponent()!=Platform->GetPlatformMesh()||Hit.ImpactNormal.Z<Bot->GetCharacterMovement()->GetWalkableFloorZ())continue;
            float Cost=FVector::Dist2D(Bot->GetActorLocation(),Hit.ImpactPoint);
            if(auto* Mode=AHeliosBioHotelGameMode::Find(Bot->GetWorld()))for(const auto& P:Mode->Model.GetParticipants())
                if(auto* Actor=Mode->ParticipantActor(P.Id);Actor&&Actor!=Bot&&P.bAlive&&FVector::Dist2D(Actor->GetActorLocation(),Hit.ImpactPoint)<Margin*2.f)Cost+=1000.f;
            if(Cost<Best){Best=Cost;Top=Hit.ImpactPoint;Found=true;}
        }
        return Found;
    }
    void MoveOnPlatform(ACharacter* Bot,AAIController* AI,AHeliosBioMovingPlatform* Platform,FBotState& State,float Dt)
    {
        auto* Movement=Bot->GetCharacterMovement();auto* Mesh=Platform->GetPlatformMesh();
        if(!State.bAvoidanceSaved){State.bSavedAvoidance=Movement->bUseRVOAvoidance;State.bAvoidanceSaved=true;Movement->SetAvoidanceEnabled(false);}
        const auto& Transform=Mesh->GetComponentTransform();FVector Low,High;Mesh->GetLocalBounds(Low,High);
        const float Radius=Bot->GetCapsuleComponent()->GetScaledCapsuleRadius(),Half=Bot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        const FVector Feet=Bot->GetNavAgentLocation();FVector Wanted=State.bHasDeparturePoint?State.DeparturePoint:Transform.TransformPosition((Low+High)*.5f);
        auto* Opponent=Cast<ACharacter>(State.Request.CombatTarget.Get());
        const bool SamePlatform=Opponent&&Opponent->GetMovementBase()==Mesh;
        if(SamePlatform)
        {
            FVector Toward=(Opponent->GetNavAgentLocation()-Feet).GetSafeNormal2D();
            const float Distance=FVector::Dist2D(Feet,Opponent->GetNavAgentLocation());
            if(State.Request.bKeepDistance)Wanted=Feet-Toward*200.f;
            else if(Distance>State.Request.CombatDistance)Wanted=Opponent->GetNavAgentLocation();
            else {AI->StopMovement();return;}
        }
        FVector Local=Transform.InverseTransformPosition(Wanted);
        const FVector Scale=Transform.GetScale3D().GetAbs();
        Local.X=FMath::Clamp(Local.X,Low.X+(Radius+12.f)/FMath::Max(.01f,Scale.X),High.X-(Radius+12.f)/FMath::Max(.01f,Scale.X));
        Local.Y=FMath::Clamp(Local.Y,Low.Y+(Radius+12.f)/FMath::Max(.01f,Scale.Y),High.Y-(Radius+12.f)/FMath::Max(.01f,Scale.Y));
        Wanted=Transform.TransformPosition(Local);FVector Direction=(Wanted-Feet).GetSafeNormal2D();
        if(FVector::Dist2D(Wanted,Feet)<18.f){AI->StopMovement();return;}
        FCollisionQueryParams Q(SCENE_QUERY_STAT(BioFloatingLocalWalk),false,Bot);
        TArray<AActor*> Attached;Bot->GetAttachedActors(Attached,true,true);for(auto* Actor:Attached)Q.AddIgnoredActor(Actor);
        const float Step=FMath::Clamp(Movement->MaxWalkSpeed*FMath::Max(Dt,.05f)+Radius,35.f,100.f);
        const FCollisionShape Capsule=FCollisionShape::MakeCapsule(Radius,Half-2.f);
        for(float Angle:{0.f,35.f,-35.f,70.f,-70.f})
        {
            const FVector Try=Direction.RotateAngleAxis(Angle,FVector::UpVector);const FVector Next=Feet+Try*Step;
            bool Supported=true;
            for(const FVector& Offset:{FVector::ZeroVector,FVector(Radius,0,0),FVector(-Radius,0,0),FVector(0,Radius,0),FVector(0,-Radius,0)})
            {
                FHitResult Floor;FCollisionQueryParams FloorQuery=Q;
                if(auto* Mode=AHeliosBioHotelGameMode::Find(Bot->GetWorld()))for(const auto& P:Mode->Model.GetParticipants())FloorQuery.AddIgnoredActor(Mode->ParticipantActor(P.Id));
                if(!Bot->GetWorld()->LineTraceSingleByChannel(Floor,Next+Offset+FVector(0,0,35),Next+Offset-FVector(0,0,45),ECC_Visibility,FloorQuery)||Floor.GetComponent()!=Mesh||Floor.ImpactNormal.Z<Movement->GetWalkableFloorZ()){Supported=false;break;}
            }
            FHitResult Hit;
            if(!Supported||Bot->GetWorld()->SweepSingleByChannel(Hit,Feet+FVector(0,0,Half+3),Next+FVector(0,0,Half+3),FQuat::Identity,ECC_Pawn,Capsule,Q))continue;
            Bot->AddMovementInput(Try);Diagnostic(Bot,State,SamePlatform?(State.Request.bKeepDistance?TEXT("platform_local_retreat"):TEXT("platform_local_pursuit")):TEXT("platform_local_positioning"));return;
        }
        AI->StopMovement();Diagnostic(Bot,State,TEXT("platform_local_hold_no_supported_clear_step"));
    }
    // Solve a descending ballistic interception using measured real base velocity.
    bool PlanJumpCandidate(ACharacter* Bot,const FVector& LandingFeet,const FVector& LandingVelocity,UPrimitiveComponent* LandingComponent,float VerticalSpeed,FVector& Launch,FString& Reason)
    {
        const float Gravity=-Bot->GetCharacterMovement()->GetGravityZ();
        if(Gravity<=KINDA_SMALL_NUMBER){Reason=FString::Printf(TEXT("gravity_invalid gravity=%.3f"),Gravity);return false;}
        if(!FMath::IsFinite(VerticalSpeed)||VerticalSpeed<=0.f){Reason=TEXT("vertical_speed_invalid");return false;}
        const float RelativeUp=VerticalSpeed-LandingVelocity.Z;
        const float Height=LandingFeet.Z-FeetZ(Bot);
        const float Discriminant=RelativeUp*RelativeUp-2.f*Gravity*Height;
        if(Discriminant<0.f){Reason=FString::Printf(TEXT("unreachable_height feet_z=%.3f landing=%s height=%.3f relative_up=%.3f gravity=%.3f discriminant=%.3f"),FeetZ(Bot),*LandingFeet.ToString(),Height,RelativeUp,Gravity,Discriminant);return false;}
        const float Time=(RelativeUp+FMath::Sqrt(Discriminant))/Gravity;
        if(Time<.15f||Time>Settings.FlightTimeout){Reason=FString::Printf(TEXT("flight_time_rejected time=%.3f allowed=.15..%.3f landing=%s"),Time,Settings.FlightTimeout,*LandingFeet.ToString());return false;}
        const FVector Predicted=LandingFeet+LandingVelocity*Time;
        const FVector XY=FVector(Predicted.X-Bot->GetActorLocation().X,Predicted.Y-Bot->GetActorLocation().Y,0)/Time;
        const float MaxSpeed=FMath::Min(Settings.MaxHorizontalLaunchSpeed,FMath::Max(100.f,Bot->GetCharacterMovement()->MaxWalkSpeed*1.5f));
        if(XY.SizeSquared()>FMath::Square(MaxSpeed)){Reason=FString::Printf(TEXT("horizontal_speed_rejected required=%.3f cap=%.3f time=%.3f predicted=%s base_velocity=%s"),XY.Size(),MaxSpeed,Time,*Predicted.ToString(),*LandingVelocity.ToString());return false;}
        Launch=XY+FVector(0,0,VerticalSpeed);
        FCollisionQueryParams Query(SCENE_QUERY_STAT(BioPlatformBallisticClearance),false,Bot);
        TArray<AActor*> Attached;Bot->GetAttachedActors(Attached,true,true);for(auto* Actor:Attached)Query.AddIgnoredActor(Actor);
        const auto* Capsule=Bot->GetCapsuleComponent();const FCollisionShape Shape=FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight());
        FVector Previous=Bot->GetActorLocation();
        for(int32 Segment=1;Segment<=10;++Segment)
        {
            const float T=Time*Segment/10.f;const FVector Next=Bot->GetActorLocation()+Launch*T-FVector(0,0,.5f*Gravity*T*T);
            FHitResult Hit;
            if(Bot->GetWorld()->SweepSingleByChannel(Hit,Previous,Next,FQuat::Identity,ECC_Pawn,Shape,Query))
            {
                const bool LandingContact=Segment>=8&&Hit.GetComponent()==LandingComponent&&Hit.ImpactNormal.Z>=Bot->GetCharacterMovement()->GetWalkableFloorZ();
                if(!LandingContact)
                {
                    Reason=FString::Printf(TEXT("ballistic_sweep_blocked segment=%d time=%.3f start=%s end=%s hit_actor=%s hit_component=%s hit_location=%s impact_point=%s impact_normal=%s hit_fraction=%.3f starts_penetrating=%d landing_component=%s launch=%s predicted=%s base_velocity=%s"),Segment,T,*Previous.ToString(),*Next.ToString(),*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()),*Hit.Location.ToString(),*Hit.ImpactPoint.ToString(),*Hit.ImpactNormal.ToString(),Hit.Time,Hit.bStartPenetrating?1:0,*GetNameSafe(LandingComponent),*Launch.ToString(),*Predicted.ToString(),*LandingVelocity.ToString());
                    return false;
                }
            }
            Previous=Next;
        }
        Reason=FString::Printf(TEXT("jump_clear chosen_vertical=%.3f flight_time=%.3f landing=%s predicted=%s launch=%s base_velocity=%s"),VerticalSpeed,Time,*LandingFeet.ToString(),*Predicted.ToString(),*Launch.ToString(),*LandingVelocity.ToString());
        return true;
    }
    bool PlanJump(ACharacter* Bot,const FVector& LandingFeet,const FVector& LandingVelocity,UPrimitiveComponent* LandingComponent,FVector& Launch,FString& Reason)
    {
        // Explicit offline policy: prefer the configured arc, then try lower positive arcs.
        // Each candidate independently passes the same ballistic and real capsule checks.
        const float Fractions[]={1.f,.82f,.66f,.5f};
        FString Rejections;
        for(int32 Index=0;Index<UE_ARRAY_COUNT(Fractions);++Index)
        {
            const float VerticalSpeed=Settings.LaunchVerticalSpeed*Fractions[Index];
            FVector CandidateLaunch=FVector::ZeroVector;FString CandidateReason;
            if(PlanJumpCandidate(Bot,LandingFeet,LandingVelocity,LandingComponent,VerticalSpeed,CandidateLaunch,CandidateReason))
            {
                Launch=CandidateLaunch;
                Reason=FString::Printf(TEXT("%s candidate=%d rejected_candidates=[%s]"),*CandidateReason,Index,*Rejections);
                return true;
            }
            Rejections+=FString::Printf(TEXT("%svertical=%.3f reason={%s}"),Rejections.IsEmpty()?TEXT(""):TEXT("; "),VerticalSpeed,*CandidateReason);
        }
        Reason=FString::Printf(TEXT("all_jump_candidates_rejected [%s]"),*Rejections);
        return false;
    }
    bool IsBasedOn(ACharacter* Bot,AHeliosBioMovingPlatform* Platform)
    {return Platform&&Bot->GetMovementBase()==Platform->GetPlatformMesh()&&Bot->GetCharacterMovement()->IsMovingOnGround();}
    void SetStep(ACharacter* Bot,FBotState& State,EStep Step)
    {
        auto& Total=Totals.FindOrAdd(Bot);if(Total.Round!=State.Round){Total={};Total.Round=State.Round;}
        if(Step==EStep::Riding&&State.Step!=EStep::Riding)++Total.Boardings;
        if(State.Step==EStep::Departing&&Step==EStep::Cooldown&&State.bDepartureLandingVerified)++Total.Departures;
        if(Step!=EStep::Boarding&&Step!=EStep::Departing)RestoreFlightSettings(State);
        if(Step==EStep::Cooldown)RestoreAvoidance(Bot,State);
        State.Step=Step;State.Age=0.f;State.RequestAge=1.f;
        if(State.bTactical&&Step==EStep::Waiting)
        {
            float BoardAt=0.f,ExitAt=0.f;auto* Platform=State.Platform.Get();
            const bool Predicted=Platform&&Platform->PredictRouteJourney(State.EntryIndex,INDEX_NONE,0.f,BoardAt,ExitAt);
            State.WaitDeadline=Bot->GetWorld()->GetTimeSeconds()+(Predicted?FMath::Min(BoardAt+3.f,Platform->GetRouteCycleSeconds()+3.f):3.f);
        }
        UE_LOG(LogTemp,Display,TEXT("BIO_PLATFORM_TRAVERSAL bot=%s platform=%s round=%d step=%d"),*Bot->GetName(),*GetNameSafe(State.Platform.Get()),State.Round,int32(Step));
    }
    bool TryImmediateEscape(ACharacter* Bot,AAIController* AI,AHeliosBioHotelGameMode* Mode,const FTraversalRequest& Request)
    {
        // A threatened passenger must not wait at a dock. A passing, clear base
        // can still be an immediate escape when no walking retreat was found.
        auto* Threat=Request.CombatTarget.Get();
        if(!Request.bUrgent||Request.bValid||!Request.bKeepDistance||!IsValid(Threat))return false;
        const FVector Away=(Bot->GetActorLocation()-Threat->GetActorLocation()).GetSafeNormal2D();
        for(auto Platform:Mode->MovingPlatforms)
        {
            if(!IsValid(Platform)||!Platform->IsRouteRunning())continue;
            const auto* ThreatCharacter=Cast<ACharacter>(Threat);
            if(ThreatCharacter&&ThreatCharacter->GetMovementBase()==Platform->GetPlatformMesh())continue;
            const FVector Velocity=Platform->GetPlatformMesh()->GetComponentVelocity();
            if(Velocity.Size2D()<10.f||FVector::DotProduct(Velocity.GetSafeNormal2D(),Away)<.25f)continue;
            FVector Top;FBox Bounds;
            if(!TopPoint(Bot,Platform,Top,Bounds)||!SpreadLanding(Bot,Platform,Bounds,Top)||
                FVector::Dist2D(Bot->GetActorLocation(),Top)>Settings.MaxBoardingDistance||
                FVector::Dist2D(Top,Threat->GetActorLocation())<FVector::Dist2D(Bot->GetActorLocation(),Threat->GetActorLocation())+100.f)continue;
            FBotState Escape;Escape.Mode=Mode;Escape.Platform=Platform;Escape.Target=Threat;Escape.Round=Mode->RoundSerial;
            Escape.bTactical=true;Escape.bGlobalRoute=true;Escape.Request=Request;Escape.ApproachPoint=Bot->GetNavAgentLocation();
            if(!BoardPriority(Bot,Escape))continue;
            FVector Launch;FString Reason;
            if(!PlanJump(Bot,Top,Velocity,Platform->GetPlatformMesh(),Launch,Reason))continue;
            auto& Saved=States.Add(Bot,MoveTemp(Escape));AI->StopMovement();SetStep(Bot,Saved,EStep::Boarding);
            Diagnostic(Bot,Saved,TEXT("immediate_supported_escape_platform_moving_away_from_threat"));
            StartFlight(Bot,Saved,Launch,Top,Platform->GetPlatformMesh());return true;
        }
        return false;
    }
}
FStandaloneTuning& Tuning(){return Settings;}
void ForgetBot(ACharacter* Bot)
{
    if(auto* State=States.Find(Bot)){RestoreFlightSettings(*State);RestoreAvoidance(Bot,*State);}
    States.Remove(Bot);
}
FString DebugState(const ACharacter* Bot)
{
    const auto* State=States.Find(const_cast<ACharacter*>(Bot));
    if(!State)return TEXT("idle");
    return FString::Printf(TEXT("step=%d age=%.2f round=%d platform=%s approach=%s distance2d=%.3f feet_z=%.3f move_result=%d flight_settings_saved=%d action=%s route_goal=%s combat_target=%s ground_seconds=%.2f route_seconds=%.2f wait_remaining=%.2f reason=%s"),int32(State->Step),State->Age,State->Round,*GetNameSafe(State->Platform.Get()),*State->ApproachPoint.ToString(),FVector::Dist2D(Bot->GetActorLocation(),State->ApproachPoint),FeetZ(Bot),State->LastMoveResult,State->bFlightSettingsSaved?1:0,*State->Request.Intent.ToString(),*State->Request.DestinationFeet.ToString(),*GetNameSafe(State->Request.CombatTarget.Get()),State->GroundSeconds,State->RouteSeconds,FMath::Max(0.f,State->WaitDeadline-Bot->GetWorld()->GetTimeSeconds()),*State->LastReason)+FString::Printf(TEXT(" ground_walk_seconds=%.3f static_jump_seconds=%.3f physical_wall_walk_seconds=%.3f static_alignment_seconds=%.3f ground_static_links=%d ground_timing_complete=%d approach_seconds=%.3f departure_seconds=%.3f ground_segments={%s}"),State->GroundTime.GroundWalk,State->GroundTime.StaticJump,State->GroundTime.WallWalk,State->GroundTime.Alignment,State->GroundTime.StaticLinks,State->GroundTime.bComplete,State->ApproachTime.Total(),State->DepartureTime.Total(),*State->GroundTime.Segments);
}
FTraversalDiagnostics GetDiagnostics(const ACharacter* Bot)
{
    FTraversalDiagnostics Result;
    if(const auto* State=States.Find(const_cast<ACharacter*>(Bot)))
    {
        Result.Step=int32(State->Step);Result.bOwnsMovement=State->Step!=EStep::Cooldown;
        Result.bWaitingAtDock=State->Step==EStep::Waiting||State->Step==EStep::Riding;
        Result.MovementGoal=State->Step==EStep::Departing?State->DeparturePoint:State->ApproachPoint;
        Result.Reason=State->LastReason;
        Result.Intent=State->Request.Intent;Result.GroundSeconds=State->GroundSeconds;Result.RouteSeconds=State->RouteSeconds;
        Result.GroundWalkSeconds=State->GroundTime.GroundWalk;Result.StaticJumpSeconds=State->GroundTime.StaticJump;Result.PhysicalWallWalkSeconds=State->GroundTime.WallWalk;Result.StaticAlignmentSeconds=State->GroundTime.Alignment;Result.GroundStaticLinks=State->GroundTime.StaticLinks;Result.bGroundTimingComplete=State->GroundTime.bComplete;Result.GroundTimingSegments=State->GroundTime.Segments;Result.ApproachSeconds=State->ApproachTime.Total();Result.DepartureSeconds=State->DepartureTime.Total();Result.PlatformBoardingFlightSeconds=State->PlatformTime.BoardingFlight;Result.PlatformWaitSeconds=State->PlatformTime.Wait;Result.PlatformCarrySeconds=State->PlatformTime.Carry;Result.PlatformDepartureFlightSeconds=State->PlatformTime.DepartureFlight;
        Result.EntryIndex=State->EntryIndex;Result.ExitIndex=State->ExitIndex;
        Result.WaitRemaining=FMath::Max(0.f,State->WaitDeadline-Bot->GetWorld()->GetTimeSeconds());
        if(State->bTactical&&State->Step==EStep::Riding)Result.bWaitingAtDock=false;
        if(State->bYielding&&State->Step==EStep::Waiting){Result.bWaitingAtDock=false;Result.MovementGoal=State->YieldPoint;}
    }
    if(const auto* Total=Totals.Find(const_cast<ACharacter*>(Bot))){Result.Boardings=Total->Boardings;Result.Departures=Total->Departures;}
    return Result;
}
bool TickTraversalInternal(ACharacter* Bot,AAIController* AI,AHeliosBioHotelGameMode* Mode,AActor* Target,float Dt,const FTraversalRequest* Request)
{
    for(auto It=Totals.CreateIterator();It;++It)if(!It.Key().IsValid())It.RemoveCurrent();
    for(auto It=RouteDocks.CreateIterator();It;++It)if(!It.Key().IsValid())It.RemoveCurrent();
    for(auto It=States.CreateIterator();It;++It)
    {
        auto* SavedMode=It.Value().Mode.Get();auto* SavedBot=It.Key().Get();
        const auto* SavedParticipant=SavedMode&&SavedBot?SavedMode->Model.FindParticipant(SavedMode->ParticipantId(SavedBot)):nullptr;
        if(!SavedBot||!SavedMode||SavedMode->RoundSerial!=It.Value().Round||SavedMode->Model.GetPhase()!=EHeliosBioPhase::Active||!SavedParticipant||!SavedParticipant->bAlive)
        {RestoreFlightSettings(It.Value());RestoreAvoidance(SavedBot,It.Value());It.RemoveCurrent();}
    }
    if(!IsValid(Bot)||!IsValid(AI)||AI->GetPawn()!=Bot||Bot->IsPlayerControlled()||!IsValid(Mode)||(!Request&&!IsValid(Target))||Bot->GetWorld()!=Mode->GetWorld()||!FMath::IsFinite(Dt)||Dt<0.f)
    {if(Bot)ForgetBot(Bot);return false;}
    if(Mode->MovingPlatforms.IsEmpty()){ForgetBot(Bot);return false;}
    const int32 ParticipantId=Mode->ParticipantId(Bot);
    if(ParticipantId<=0){ForgetBot(Bot);return false;} // Participant zero is the local player, even while unpossessed.
    const auto* Participant=Mode->Model.FindParticipant(ParticipantId);
    if(!Participant||!Participant->bAlive||Mode->Model.GetPhase()!=EHeliosBioPhase::Active){ForgetBot(Bot);return false;}
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(Bot->GetWorld());if(!Nav){ForgetBot(Bot);return false;}
    auto* Movement=Bot->GetCharacterMovement();auto* State=States.Find(Bot);
    const bool Tactical=Request&&Mode->bFloatingProfile;
    const float Now=Bot->GetWorld()->GetTimeSeconds();
    if(Tactical&&Movement->IsMovingOnGround()&&(!State||State->Step==EStep::Cooldown)&&TryImmediateEscape(Bot,AI,Mode,*Request))return true;
    if(State&&(State->Mode.Get()!=Mode||State->Round!=Mode->RoundSerial)){ForgetBot(Bot);State=nullptr;}
    if(State)
    {
        State->Age+=Dt;State->RequestAge+=Dt;State->DiagnosticAge+=Dt;State->EvaluateAge+=Dt;State->JumpCheckAge+=Dt;
        if(State->Step==EStep::Cooldown)
        {
            const bool Changed=Tactical&&(State->Request.Intent!=Request->Intent||FVector::DistSquared(State->Request.DestinationFeet,Request->DestinationFeet)>FMath::Square(200.f));
            if(Changed){ForgetBot(Bot);return TickTraversalInternal(Bot,AI,Mode,Target,0.f,Request);}
            if(State->Age>=(Tactical?4.f:Settings.RetrySeconds))ForgetBot(Bot);return false;
        }
        auto* Platform=State->Platform.Get();
        if(!Platform||!Platform->IsRouteConfigured()||(!State->bGlobalRoute&&!IsBasedOn(Bot,Platform)&&State->Target.Get()!=Target))
        {AI->StopMovement();SetStep(Bot,*State,EStep::Cooldown);return false;}
        if(State->Target.Get()!=Target)State->RequestAge=1.f;State->Target=Target;
        if(Tactical)
        {
            const bool InFlight=State->Step==EStep::Boarding||State->Step==EStep::Departing;
            const bool Pursuit=Request->Intent==TEXT("Chase")||Request->Intent==TEXT("Flank");
            const bool Changed=State->Request.Intent!=Request->Intent||
                (Pursuit?State->Request.CombatTarget!=Request->CombatTarget:
                FVector::DistSquared(State->Request.DestinationFeet,Request->DestinationFeet)>FMath::Square(200.f));
            State->Request=*Request;
            if(!InFlight&&!IsBasedOn(Bot,Platform)&&(!Request->bValid||Request->bUrgent||!Platform->IsRouteRunning()||Changed))
            {Diagnostic(Bot,*State,TEXT("cancel_dock_threat_goal_changed_or_platform_stopped"));AI->StopMovement();SetStep(Bot,*State,EStep::Cooldown);return false;}
        }
        if(IsBasedOn(Bot,Platform)&&(State->Step==EStep::Boarding||State->Step==EStep::Approach||State->Step==EStep::Waiting))
        {AI->StopMovement();SetStep(Bot,*State,EStep::Riding);}
        FVector Top;FBox Bounds;if(!TopPoint(Bot,Platform,Top,Bounds))
        {AI->StopMovement();SetStep(Bot,*State,EStep::Cooldown);return false;}
        if(State->Step==EStep::Boarding||State->Step==EStep::Departing)
        {
            if(State->Step==EStep::Departing&&State->Age>.3f&&IsBasedOn(Bot,Platform))
            {SetStep(Bot,*State,EStep::Cooldown);return false;}
            State->bFlightAirborneObserved|=Movement->IsFalling();
            auto* LandingComponent=State->FlightLandingComponent.Get();
            const FVector Landing=LandingComponent?LandingComponent->GetComponentTransform().TransformPosition(State->FlightLandingLocal):State->FlightLandingLocal;
            if(State->bFlightAirborneObserved&&Movement->IsMovingOnGround()&&!IsBasedOn(Bot,Platform))
            {
                const FVector Feet=Bot->GetNavAgentLocation();
                const bool CorrectFloor=LandingComponent&&Movement->CurrentFloor.HitResult.GetComponent()==LandingComponent;
                State->bDepartureLandingVerified=State->Step==EStep::Departing&&CorrectFloor&&FVector::Dist2D(Feet,Landing)<=40.f&&FMath::Abs(Feet.Z-Landing.Z)<=12.f;
                Diagnostic(Bot,*State,FString::Printf(TEXT("actual_flight_landing verified_departure=%d expected=%s actual=%s floor=%s expected_floor=%s"),State->bDepartureLandingVerified,*Landing.ToString(),*Feet.ToString(),*GetNameSafe(Movement->CurrentFloor.HitResult.GetComponent()),*GetNameSafe(LandingComponent)));
                SetStep(Bot,*State,EStep::Cooldown);return false;
            }
            if(Movement->IsFalling())
            {
                // One launch, then measured feedback through native AirControl.
                const FVector LandingVelocity=State->Step==EStep::Boarding&&LandingComponent?LandingComponent->GetComponentVelocity():FVector::ZeroVector;
                const FVector Input=AHeliosBioStaticJumpLink::NativeAirInputToLanding(Bot,Landing,LandingVelocity,Dt);
                Bot->ConsumeMovementInputVector();Bot->AddMovementInput(Input.GetSafeNormal2D(),FMath::Min(1.f,Input.Size2D()));
            }
            if(State->Age>Settings.FlightTimeout){Diagnostic(Bot,*State,TEXT("actual_flight_landing_timeout"));SetStep(Bot,*State,EStep::Cooldown);return false;}
            return true; // Never issue another LaunchCharacter during the same flight.
        }
        if(State->Step==EStep::Riding)
        {
            if(!IsBasedOn(Bot,Platform)){SetStep(Bot,*State,EStep::Cooldown);return false;}
            if(Tactical)MoveOnPlatform(Bot,AI,Platform,*State,Dt);
            if(State->Age<.35f||Platform->GetPlatformMesh()->GetComponentVelocity().Size()>Settings.MaxDeparturePlatformSpeed)return true;
            const auto* TargetCharacter=Cast<ACharacter>(Target);
            if(TargetCharacter&&TargetCharacter->GetMovementBase()==Platform->GetPlatformMesh()&&(!Tactical||Request->Intent==TEXT("Chase"))){State->bHasDeparturePoint=false;return true;}
            if(Tactical&&Request->bValid&&Bounds.IsInsideOrOn(FVector(Request->DestinationFeet.X,Request->DestinationFeet.Y,Bounds.GetCenter().Z))&&FMath::Abs(Request->DestinationFeet.Z-Top.Z)<45.f)
            {State->bHasDeparturePoint=false;return true;}
            if(State->bGlobalRoute&&State->RequestAge>=1.f)
            {
                State->RequestAge=0.f;
                if(Tactical&&(!Request->bValid||!Platform->IsRouteRunning()))
                {
                    State->bHasDeparturePoint=DockPoint(Bot,Bot,Nav,Bounds,true,State->DeparturePoint);
                    if(State->bHasDeparturePoint&&Request->bUrgent&&Target&&
                        FVector::Dist2D(State->DeparturePoint,Target->GetActorLocation())<FVector::Dist2D(Bot->GetActorLocation(),Target->GetActorLocation())+50.f)State->bHasDeparturePoint=false;
                }
                else State->bHasDeparturePoint=Tactical?RefreshTacticalDeparture(Bot,Nav,Platform,Bounds,*State):RefreshConnectedDeparture(Bot,Target,Nav,Platform,Bounds,State->Round,State->DeparturePoint);
            }
            if(State->bGlobalRoute&&!State->bHasDeparturePoint){Diagnostic(Bot,*State,TEXT("riding_wait_no_connected_shore_for_current_target"));return true;}
            FVector Shore=State->DeparturePoint;
            if(State->bGlobalRoute&&State->bHasDeparturePoint)
            {
                if(FVector::DistSquared2D(Bot->GetActorLocation(),Shore)>FMath::Square(Settings.MaxBoardingDistance))
                {Diagnostic(Bot,*State,TEXT("riding_to_connected_destination_shore"));return true;}
            }
            else if(!DockPoint(Bot,Target?Target:Bot,Nav,Bounds,true,Shore))return true;
            if(!State->bGlobalRoute&&FVector::DistSquared2D(Shore,Target->GetActorLocation())+FMath::Square(70.f)>=FVector::DistSquared2D(Bot->GetActorLocation(),Target->GetActorLocation()))return true;
            FHitResult Floor;FCollisionQueryParams Query(SCENE_QUERY_STAT(BioPlatformDepartureFloor),false,Bot);
            if(!Bot->GetWorld()->LineTraceSingleByChannel(Floor,Shore+FVector(0,0,60),Shore-FVector(0,0,100),ECC_Visibility,Query))return true;
            FVector Launch;FString Reason;if(!PlanJump(Bot,Shore,FVector::ZeroVector,Floor.GetComponent(),Launch,Reason)){Diagnostic(Bot,*State,Reason);return true;}
            Diagnostic(Bot,*State,Reason);
            State->DeparturePoint=Shore;AI->StopMovement();SetStep(Bot,*State,EStep::Departing);StartFlight(Bot,*State,Launch,Shore,Floor.GetComponent());return true;
        }
        if(Tactical&&State->EvaluateAge>=1.f)
        {
            State->EvaluateAge=0.f;float BoardAt,ExitAt,Ground;
            State->GroundTime={};const bool GroundComplete=CompleteStaticPath(Bot,Nav,Bot->GetNavAgentLocation(),Request->DestinationFeet,Ground,25.f,&State->GroundTime);
            State->GroundSeconds=GroundComplete&&State->GroundTime.bComplete?State->GroundTime.Total():-1.f;
            float RemainingLength=0;State->ApproachTime={};const bool ApproachComplete=CompleteStaticPath(Bot,Nav,Bot->GetNavAgentLocation(),State->ApproachPoint,RemainingLength,25.f,&State->ApproachTime)&&State->ApproachTime.bComplete;
            bool Predicted=ApproachComplete;
            float Departure=0.f;State->DepartureTime={};const bool HasGroundExit=State->ExitIndex==INDEX_NONE||(CompleteStaticPath(Bot,Nav,State->DeparturePoint,Request->DestinationFeet,Departure,25.f,&State->DepartureTime)&&State->DepartureTime.bComplete);
            Predicted=Predicted&&HasGroundExit&&EstimatePlatformJourney(Bot,Platform,State->EntryIndex,State->ExitIndex,State->ApproachPoint,State->DeparturePoint,State->ApproachTime.Total(),State->DepartureTime.Total(),BoardAt,ExitAt,State->PlatformTime);
            State->RouteSeconds=Predicted?State->PlatformTime.Total:-1.f;
            const auto* Opponent=Cast<ACharacter>(Target);
            const bool PursueAboard=Opponent&&Opponent->GetMovementBase()==Platform->GetPlatformMesh()&&Request->Intent==TEXT("Chase");
            if(PursueAboard)State->GroundSeconds=-1.f;
            if(!Predicted||!HasGroundExit||(GroundComplete&&!State->GroundTime.bComplete)||(!PursueAboard&&!WorthBoarding(State->GroundSeconds,State->RouteSeconds)))
            {Diagnostic(Bot,*State,TEXT("cancel_route_no_longer_beneficial_or_connected"));AI->StopMovement();SetStep(Bot,*State,EStep::Cooldown);return false;}
        }
        if(State->Step==EStep::Waiting)
        {
            if(!Tactical)AI->StopMovement();
            if(Tactical?Now>State->WaitDeadline:State->Age>Settings.DockWaitTimeout){Diagnostic(Bot,*State,TEXT("authored_route_dock_wait_timeout"));SetStep(Bot,*State,EStep::Cooldown);return false;}
            if(Tactical)
            {
                ACharacter* Leader=nullptr;
                if(!BoardPriority(Bot,*State,&Leader)){YieldBoardingLane(Bot,AI,Nav,Leader,Top,*State);return true;}
                if(State->bYielding&&FVector::Dist2D(Bot->GetNavAgentLocation(),State->ApproachPoint)>30.f)
                {AI->MoveToLocation(State->ApproachPoint,15.f,false,true,false,false,nullptr,false);Diagnostic(Bot,*State,TEXT("returning_to_takeoff_after_yield"));return true;}
                State->bYielding=false;AI->StopMovement();
            }
            if(!Movement->IsMovingOnGround()||FVector::DistSquared2D(Bot->GetActorLocation(),Top)>FMath::Square(Settings.MaxBoardingDistance))
            {Diagnostic(Bot,*State,TEXT("waiting_for_authored_route_dock"));return true;}
            if(Tactical)
            {
                if(State->JumpCheckAge<.2f)return true;State->JumpCheckAge=0.f;
                if(!SpreadLanding(Bot,Platform,Bounds,Top))return true;
            }
            FVector Launch;FString Reason;
            if(!PlanJump(Bot,Top,Platform->GetPlatformMesh()->GetComponentVelocity(),Platform->GetPlatformMesh(),Launch,Reason)){Diagnostic(Bot,*State,Reason);return true;}
            Diagnostic(Bot,*State,Reason);SetStep(Bot,*State,EStep::Boarding);StartFlight(Bot,*State,Launch,Top,Platform->GetPlatformMesh());return true;
        }
        if(State->Age>(State->bGlobalRoute?Settings.GlobalApproachTimeout:Settings.ApproachTimeout)||
           (!State->bGlobalRoute&&FVector::DistSquared2D(Bot->GetActorLocation(),Top)>FMath::Square(Settings.SearchRadius*1.5f)))
        {Diagnostic(Bot,*State,TEXT("approach_timeout_or_platform_out_of_search_range"));AI->StopMovement();SetStep(Bot,*State,EStep::Cooldown);return false;}
        if(Tactical)
        {
            State->ProgressAge+=Dt;
            if(State->ProgressAge>=.5f)
            {
                State->StalledSeconds=FVector::Dist2D(Bot->GetActorLocation(),State->ProgressPosition)<12.f?State->StalledSeconds+State->ProgressAge:0.f;
                State->ProgressPosition=Bot->GetActorLocation();State->ProgressAge=0.f;
                if(State->StalledSeconds>=1.5f&&FVector::Dist2D(Bot->GetActorLocation(),State->ApproachPoint)>Settings.ApproachAcceptance+25.f)
                {Diagnostic(Bot,*State,TEXT("dock_approach_blocked_yield_and_replan"));AI->StopMovement();SetStep(Bot,*State,EStep::Cooldown);return false;}
            }
        }
        if(State->RequestAge>=.4f)
        {
            FVector Dock=State->ApproachPoint;
            if(!State->bGlobalRoute&&!DockPoint(Bot,Target,Nav,Bounds,false,Dock)){Diagnostic(Bot,*State,TEXT("no_static_nav_dock_with_complete_path"));SetStep(Bot,*State,EStep::Cooldown);return false;}
            State->ApproachPoint=Dock;State->RequestAge=0.f;
            State->LastMoveResult=int32(AI->MoveToLocation(Dock,Settings.ApproachAcceptance,false,true,false,false,nullptr,false));
        }
        if(!Movement->IsMovingOnGround()){Diagnostic(Bot,*State,TEXT("approach_wait_not_grounded"));return true;}
        if(FVector::DistSquared2D(Bot->GetActorLocation(),State->ApproachPoint)>FMath::Square(Settings.ApproachAcceptance+25.f))
        {Diagnostic(Bot,*State,FString::Printf(TEXT("approach_wait_distance threshold=%.3f move_result=%d ai_status=%d"),Settings.ApproachAcceptance+25.f,State->LastMoveResult,int32(AI->GetMoveStatus())));return true;}
        if(State->bGlobalRoute){AI->StopMovement();SetStep(Bot,*State,EStep::Waiting);Diagnostic(Bot,*State,TEXT("reached_connected_shore_waiting_for_platform"));return true;}
        if(FVector::DistSquared2D(Bot->GetActorLocation(),Top)>FMath::Square(Settings.MaxBoardingDistance))
        {Diagnostic(Bot,*State,FString::Printf(TEXT("boarding_distance_rejected distance=%.3f cap=%.3f top=%s"),FVector::Dist2D(Bot->GetActorLocation(),Top),Settings.MaxBoardingDistance,*Top.ToString()));return true;}
        FVector Launch;FString Reason;if(!PlanJump(Bot,Top,Platform->GetPlatformMesh()->GetComponentVelocity(),Platform->GetPlatformMesh(),Launch,Reason)){Diagnostic(Bot,*State,Reason);return true;}
        Diagnostic(Bot,*State,Reason);
        AI->StopMovement();SetStep(Bot,*State,EStep::Boarding);StartFlight(Bot,*State,Launch,Top,Platform->GetPlatformMesh());return true;
    }
    if(!Movement->IsMovingOnGround())return false;
    AHeliosBioMovingPlatform* Best=nullptr;FVector BestDock=FVector::ZeroVector,BestExit=FVector::ZeroVector;float BestScore=MAX_flt;bool HasExit=false,PreferGround=false;
    // Adopt characters already riding before choosing a static route from their origin.
    for(auto Platform:Mode->MovingPlatforms)if(IsValid(Platform)&&IsBasedOn(Bot,Platform))
    {
        FBotState Riding;Riding.Mode=Mode;Riding.Platform=Platform;Riding.Target=Target;Riding.Round=Mode->RoundSerial;Riding.bGlobalRoute=true;
        Riding.bTactical=Tactical;if(Tactical)Riding.Request=*Request;
        auto& Added=States.Add(Bot,MoveTemp(Riding));AI->StopMovement();SetStep(Bot,Added,EStep::Riding);return true;
    }
    if(Tactical)
    {
        FTacticalRoute Route;
        if(FindTacticalRoute(Bot,Nav,Mode,*Request,Route))
        {
            FBotState Added;Added.Mode=Mode;Added.Platform=Route.Platform;Added.Target=Target;Added.Round=Mode->RoundSerial;
            Added.bTactical=true;Added.Request=*Request;Added.bGlobalRoute=true;Added.bHasDeparturePoint=Route.ExitIndex!=INDEX_NONE;
            Added.EntryIndex=Route.EntryIndex;Added.ExitIndex=Route.ExitIndex;Added.ApproachPoint=Route.Entry;Added.DeparturePoint=Route.Exit;
            Added.GroundSeconds=Route.GroundSeconds;Added.RouteSeconds=Route.TotalSeconds;Added.GroundTime=Route.GroundTime;Added.ApproachTime=Route.ApproachTime;Added.DepartureTime=Route.DepartureTime;Added.PlatformTime=Route.PlatformTime;Added.ProgressPosition=Bot->GetActorLocation();
            Added.WaitDeadline=Now+FMath::Min(Route.BoardSeconds+3.f,Route.Platform->GetRouteCycleSeconds()+3.f);
            auto& Saved=States.Add(Bot,MoveTemp(Added));AI->StopMovement();SetStep(Bot,Saved,EStep::Approach);
            Diagnostic(Bot,Saved,TEXT("time_compared_action_platform_route"));return true;
        }
        FBotState Retry;Retry.Mode=Mode;Retry.Target=Target;Retry.Round=Mode->RoundSerial;Retry.Step=EStep::Cooldown;
        Retry.bTactical=true;Retry.Request=*Request;Retry.GroundSeconds=Route.GroundSeconds;Retry.GroundTime=Route.GroundTime;Retry.RouteSeconds=Route.TotalSeconds<MAX_flt?Route.TotalSeconds:-1.f;Retry.ApproachTime=Route.ApproachTime;Retry.DepartureTime=Route.DepartureTime;Retry.PlatformTime=Route.PlatformTime;
        Retry.LastReason=FString::Printf(TEXT("action_ground_faster_or_no_platform_connection tops=%d approaches=%d candidates=%d"),Route.Tops,Route.Approaches,Route.Candidates);States.Add(Bot,MoveTemp(Retry));return false;
    }
    if(FindGlobalRoute(Bot,Target,Nav,Mode,Best,BestDock,BestExit,HasExit,PreferGround))
    {
        FBotState NewState;NewState.Mode=Mode;NewState.Platform=Best;NewState.Target=Target;NewState.Round=Mode->RoundSerial;
        NewState.bGlobalRoute=true;NewState.bHasDeparturePoint=HasExit;NewState.ApproachPoint=BestDock;NewState.DeparturePoint=BestExit;NewState.LastReason=TEXT("complete_static_path_to_authored_route_shore");
        auto& Added=States.Add(Bot,MoveTemp(NewState));AI->StopMovement();SetStep(Bot,Added,EStep::Approach);return true;
    }
    if(!PreferGround)for(auto Platform:Mode->MovingPlatforms)
    {
        if(!IsValid(Platform)||!Platform->IsRouteConfigured())continue;
        if(IsBasedOn(Bot,Platform)){Best=Platform;break;}
        FVector Top;FBox Bounds;if(!TopPoint(Bot,Platform,Top,Bounds))continue;
        const float Distance=FVector::DistSquared2D(Bot->GetActorLocation(),Top);if(Distance>FMath::Square(Settings.SearchRadius))continue;
        const auto* TargetCharacter=Cast<ACharacter>(Target);
        const bool TargetAboard=TargetCharacter&&TargetCharacter->GetMovementBase()==Platform->GetPlatformMesh();
        const FVector ToTarget=Target->GetActorLocation()-Top;
        const bool MovesTowardTarget=FVector::DotProduct(Platform->GetPlatformMesh()->GetComponentVelocity().GetSafeNormal2D(),ToTarget.GetSafeNormal2D())>.35f;
        const bool HigherTarget=StaticTargetFeet(Target).Z-FeetZ(Bot)>120.f;
        if(!TargetAboard&&!HigherTarget&&!MovesTowardTarget)continue;
        FVector Dock;if(!DockPoint(Bot,Target,Nav,Bounds,false,Dock))continue;
        if(Distance<BestScore){Best=Platform;BestDock=Dock;BestScore=Distance;}
    }
    if(!Best)
    {
        // Throttle global path queries even when ordinary ground pursuit is preferable.
        FBotState Retry;Retry.Mode=Mode;Retry.Target=Target;Retry.Round=Mode->RoundSerial;Retry.Step=EStep::Cooldown;Retry.LastReason=TEXT("ordinary_ground_route_or_no_connected_platform_route");States.Add(Bot,MoveTemp(Retry));return false;
    }
    FBotState NewState;NewState.Mode=Mode;NewState.Platform=Best;NewState.Target=Target;NewState.Round=Mode->RoundSerial;NewState.ApproachPoint=BestDock;
    auto& Added=States.Add(Bot,MoveTemp(NewState));AI->StopMovement();SetStep(Bot,Added,IsBasedOn(Bot,Best)?EStep::Riding:EStep::Approach);return true;
}
bool TickTraversal(ACharacter* Bot,AAIController* AI,AHeliosBioHotelGameMode* Mode,AActor* Target,float Dt)
{return TickTraversalInternal(Bot,AI,Mode,Target,Dt,nullptr);}
bool TickTraversal(ACharacter* Bot,AAIController* AI,AHeliosBioHotelGameMode* Mode,const FTraversalRequest& Request,float Dt)
{
    if(!IsValid(Mode)||!Mode->bFloatingProfile)return false;
    FTraversalRequest RouteRequest=Request;
    if((Request.Intent==TEXT("Chase")||Request.Intent==TEXT("Flank"))&&Request.CombatTarget.IsValid())
    {
        // Tactical crowd spreading supplies a local ground approach. The
        // global route must still reach the actual enemy's shore and layer.
        RouteRequest.DestinationFeet=StaticTargetFeet(Request.CombatTarget.Get());
    }
    return TickTraversalInternal(Bot,AI,Mode,Request.CombatTarget.Get(),Dt,&RouteRequest);
}
bool IsInFlight(const ACharacter* Bot)
{
    const auto* State=States.Find(const_cast<ACharacter*>(Bot));
    return State&&(State->Step==EStep::Boarding||State->Step==EStep::Departing);
}
}
