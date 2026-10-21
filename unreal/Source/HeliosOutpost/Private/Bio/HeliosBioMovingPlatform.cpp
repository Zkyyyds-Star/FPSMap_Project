#include "Bio/HeliosBioMovingPlatform.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

AHeliosBioMovingPlatform::AHeliosBioMovingPlatform()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PrePhysics;
    PlatformMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SourceMovingPlatform"));
    SetRootComponent(PlatformMesh);
    PlatformMesh->SetMobility(EComponentMobility::Movable);
    PlatformMesh->SetSimulatePhysics(false);
    PlatformMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    PlatformMesh->SetCollisionObjectType(ECC_WorldDynamic);
    PlatformMesh->SetCollisionResponseToAllChannels(ECR_Block);
    PlatformMesh->SetWalkableSlopeOverride(FWalkableSlopeOverride(WalkableSlope_Default, 0.f));
    PlatformMesh->CanCharacterStepUpOn = ECB_Yes;
    PlatformMesh->SetCanEverAffectNavigation(false);
    Tags.Add(TEXT("BioSourceMovingPlatform"));
}

bool AHeliosBioMovingPlatform::ConfigureSourceRoute(UStaticMesh* SourceMeshAsset,
    const TArray<FVector>& WorldPointsCm, float SpeedCmPerSecond, float PauseSeconds,
    int32 OriginalLoopType, EHeliosBioPlatformReplay ReplayPolicy)
{
    if (!SourceMeshAsset || WorldPointsCm.Num() < 2 || !FMath::IsFinite(SpeedCmPerSecond) ||
        SpeedCmPerSecond <= 0.f || !FMath::IsFinite(PauseSeconds) || PauseSeconds < 0.f)
    {
        UE_LOG(LogTemp, Error, TEXT("BIO_PLATFORM_SOURCE_CONFIG_INVALID"));
        return false;
    }
    for (const FVector& Point : WorldPointsCm) if (Point.ContainsNaN()) return false;
    PlatformMesh->SetStaticMesh(SourceMeshAsset);
    RoutePoints = WorldPointsCm;
    TravelSpeed = SpeedCmPerSecond;
    EndpointPause = PauseSeconds;
    SourceLoopType = OriginalLoopType;
    Replay = ReplayPolicy;
    TargetIndex = 1;
    Direction = 1;
    WaitRemaining = 0.f;
    SetActorLocation(RoutePoints[0], false, nullptr, ETeleportType::TeleportPhysics);
    PlatformMesh->ComponentVelocity = FVector::ZeroVector;
    bConfigured = true;
    bRouteRunning = true;
    return true;
}

void AHeliosBioMovingPlatform::ResetSourceRoute(bool bRun)
{
    if(!bConfigured||RoutePoints.IsEmpty())return;
    TargetIndex=1;Direction=1;WaitRemaining=0.f;
    SetActorLocation(RoutePoints[0],false,nullptr,ETeleportType::TeleportPhysics);
    PlatformMesh->ComponentVelocity=FVector::ZeroVector;bRouteRunning=bRun;
}

void AHeliosBioMovingPlatform::SetRouteRunning(bool bRunning)
{
    bRouteRunning = bConfigured && bRunning;
    if (!bRouteRunning) PlatformMesh->ComponentVelocity = FVector::ZeroVector;
}

HeliosBioPlatformSchedule::FSnapshot AHeliosBioMovingPlatform::GetScheduleSnapshot() const
{
    HeliosBioPlatformSchedule::FSnapshot S;
    S.Points=RoutePoints;S.Position=GetActorLocation();S.Speed=TravelSpeed;S.Pause=EndpointPause;
    S.Wait=WaitRemaining;S.TargetIndex=TargetIndex;S.Direction=Direction;S.Replay=Replay;S.bRunning=bConfigured&&bRouteRunning;
    return S;
}
float AHeliosBioMovingPlatform::GetRouteCycleSeconds() const
{return HeliosBioPlatformSchedule::CycleSeconds(GetScheduleSnapshot());}
bool AHeliosBioMovingPlatform::PredictRouteJourney(int32 BoardIndex,int32 ExitIndex,float Earliest,float& Board,float& Exit) const
{return HeliosBioPlatformSchedule::Predict(GetScheduleSnapshot(),BoardIndex,ExitIndex,Earliest,Board,Exit);}

void AHeliosBioMovingPlatform::AdvanceTarget()
{
    const int32 Next = TargetIndex + Direction;
    if (RoutePoints.IsValidIndex(Next)) { TargetIndex = Next; return; }
    WaitRemaining = EndpointPause;
    switch (Replay)
    {
    case EHeliosBioPlatformReplay::PingPong:
        Direction = -Direction;
        TargetIndex += Direction;
        break;
    case EHeliosBioPlatformReplay::Wrap:
        // Traverse the closing segment physically, never teleport a rider to the first point.
        TargetIndex = 0;
        Direction = 1;
        break;
    default:
        bRouteRunning = false;
        break;
    }
}

void AHeliosBioMovingPlatform::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bConfigured || !bRouteRunning || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f)
    {
        PlatformMesh->ComponentVelocity = FVector::ZeroVector;
        return;
    }
    const FVector StartPosition = GetActorLocation();
    float Remaining = DeltaSeconds;
    // Bounded work for unusually large ticks; normal ticks still traverse exact distance/time.
    for (int32 Step = 0; Step < 256 && Remaining > KINDA_SMALL_NUMBER && bRouteRunning; ++Step)
    {
        if (WaitRemaining > 0.f)
        {
            const float Consumed = FMath::Min(Remaining, WaitRemaining);
            WaitRemaining -= Consumed;
            Remaining -= Consumed;
            continue;
        }
        const FVector Offset = RoutePoints[TargetIndex] - GetActorLocation();
        const float Distance = Offset.Size();
        if (Distance <= KINDA_SMALL_NUMBER) { AdvanceTarget(); continue; }
        const float TravelTime = Distance / TravelSpeed;
        const float Consumed = FMath::Min(Remaining, TravelTime);
        const FVector NewPosition = GetActorLocation() + Offset * (Consumed / TravelTime);
        // Kinematic base movement: CharacterMovement carries a standing character via its base.
        // Sweep against that rider would stop the platform; source mesh collision handles contact.
        SetActorLocation(NewPosition, false, nullptr, ETeleportType::None);
        Remaining -= Consumed;
        if (Consumed >= TravelTime) AdvanceTarget();
    }
    PlatformMesh->ComponentVelocity = (GetActorLocation() - StartPosition) / DeltaSeconds;
}
