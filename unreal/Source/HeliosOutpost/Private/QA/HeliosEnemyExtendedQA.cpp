#include "QA/HeliosEnemyExtendedQA.h"

#include "AIController.h"
#include "AITypes.h"
#include "Perception/AIPerceptionComponent.h"
#include "BrainComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Player/HeliosArenaCharacter.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Enemies/HeliosEnemyTestDirector.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavigationPath.h"
#include "Navigation/PathFollowingComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "Variant_Shooter/AI/ShooterAIController.h"

namespace
{
constexpr double CaptureSecondsPerType = 12.0;
constexpr double CaptureReturnFireSeconds = 7.0;
constexpr double CaptureTotalSeconds = CaptureSecondsPerType * 4.0;
constexpr double PerformanceSightStabilizationSeconds = 1.0;
constexpr float NavigationHorizontalAcceptanceCm = 75.0f;
constexpr float NavigationHeightToleranceScale = 1.05f;
// UPathFollowingComponent::HasReachedCurrentTarget always adds five percent of
// the agent radius at path-segment boundaries, even for an exact-location move.
constexpr float NavigationSegmentAgentRadiusFraction = 0.05f;
constexpr double NavigationTimeoutSpeedScale = 1.35;
constexpr double NavigationTimeoutGraceSeconds = 3.0;
constexpr double NavigationMinimumTimeoutSeconds = 20.0;
constexpr double NavigationMaximumTimeoutSeconds = 90.0;

TSharedPtr<FJsonValue> VectorJson(const FVector& Value)
{
    TArray<TSharedPtr<FJsonValue>> Values;
    Values.Add(MakeShared<FJsonValueNumber>(Value.X));
    Values.Add(MakeShared<FJsonValueNumber>(Value.Y));
    Values.Add(MakeShared<FJsonValueNumber>(Value.Z));
    return MakeShared<FJsonValueArray>(Values);
}

double PercentileSeconds(TArray<double> Samples, double Fraction)
{
    if (Samples.IsEmpty())
    {
        return 0.0;
    }
    Samples.Sort();
    const int32 Index = FMath::Clamp(FMath::CeilToInt(Samples.Num() * Fraction) - 1, 0, Samples.Num() - 1);
    return Samples[Index];
}

int32 ConsoleInt(const TCHAR* Name)
{
    const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
    return Variable ? Variable->GetInt() : INDEX_NONE;
}

float ConsoleFloat(const TCHAR* Name)
{
    const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
    return Variable ? Variable->GetFloat() : -1.0f;
}
}

AHeliosEnemyExtendedQA::AHeliosEnemyExtendedQA()
{
    PrimaryActorTick.bCanEverTick = true;
}

void AHeliosEnemyExtendedQA::BeginPlay()
{
    Super::BeginPlay();
    RootDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("..")));
    FParse::Value(FCommandLine::Get(), TEXT("HeliosEnemyExtendedQA="), Mode);
    Mode = Mode.ToLower();
    ReportPath = FPaths::Combine(RootDir, TEXT("qa/enemies"),
        FString::Printf(TEXT("extended_%s.json"), *Mode));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath), true);
    Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("mode"), Mode);
    Report->SetStringField(TEXT("status"), TEXT("running"));
    Report->SetStringField(TEXT("started_utc"), FDateTime::UtcNow().ToIso8601());
    Report->SetStringField(TEXT("measurement_policy"),
        TEXT("All frame intervals use FPlatformTime wall clock. Enemy AI, animation notifies, movement and combat remain live."));
    StartWallSeconds = FPlatformTime::Seconds();
}

void AHeliosEnemyExtendedQA::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ReleaseFire();
    RestoreNavigationObserver();
    if (Player.IsValid())
    {
        Player->SetCanBeDamaged(true);
    }
    if (NavigationController.IsValid() && NavigationController->GetBrainComponent())
    {
        NavigationController->GetBrainComponent()->ResumeLogic(TEXT("Extended navigation QA ended"));
    }
    if (NavigationMoveFinishedHandle.IsValid() && NavigationController.IsValid())
    {
        if (UPathFollowingComponent* PathFollowing = NavigationController->GetPathFollowingComponent())
        {
            PathFollowing->OnRequestFinished.Remove(NavigationMoveFinishedHandle);
        }
        NavigationMoveFinishedHandle.Reset();
    }
    Super::EndPlay(EndPlayReason);
}

bool AHeliosEnemyExtendedQA::ResolveActors()
{
    if (!Director.IsValid())
    {
        for (TActorIterator<AHeliosEnemyTestDirector> It(GetWorld()); It; ++It)
        {
            Director = *It;
            break;
        }
    }
    if (!Player.IsValid())
    {
        Player = Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
    }
    if (!PlayerController.IsValid())
    {
        PlayerController = UGameplayStatics::GetPlayerController(this, 0);
    }
    return Director.IsValid() && Player.IsValid() && PlayerController.IsValid();
}

void AHeliosEnemyExtendedQA::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bFinished)
    {
        return;
    }
    if (!bStarted)
    {
        ResolveWaitSeconds += DeltaSeconds;
        if (!ResolveActors())
        {
            if (ResolveWaitSeconds >= 10.0f)
            {
                Report->SetStringField(TEXT("error"), TEXT("Director, arena player, or player controller unavailable"));
                SaveAndFinish(TEXT("failed"));
            }
            return;
        }
        if (Mode == TEXT("recording") && !IFileManager::Get().FileExists(
            *FPaths::Combine(RootDir, TEXT("qa/enemies/recording_ready.flag"))))
        {
            Player->SetCanBeDamaged(false);
            if (FPlatformTime::Seconds() - StartWallSeconds > 120.0)
            {
                Report->SetStringField(TEXT("error"), TEXT("Window recorder did not signal readiness within 120 seconds"));
                SaveAndFinish(TEXT("failed"));
            }
            return;
        }
        bStarted = true;
        if (Mode == TEXT("performance4")) StartPerformance(4);
        else if (Mode == TEXT("performance8")) StartPerformance(8);
        else if (Mode == TEXT("capture") || Mode == TEXT("recording")) StartCapture();
        else if (Mode == TEXT("navigation")) StartNavigation();
        else
        {
            Report->SetStringField(TEXT("error"), TEXT("Unsupported mode"));
            SaveAndFinish(TEXT("failed"));
        }
        return;
    }

    if (Mode.StartsWith(TEXT("performance"))) TickPerformance();
    else if (Mode == TEXT("capture") || Mode == TEXT("recording")) TickCapture();
    else if (Mode == TEXT("navigation")) TickNavigation(DeltaSeconds);
}

FVector AHeliosEnemyExtendedQA::Grounded(const FVector& Desired, AActor* Ignore) const
{
    float HalfHeight = 90.0f;
    if (const ACharacter* Character = Cast<ACharacter>(Ignore))
    {
        HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    }
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(HeliosExtendedGround), false, Ignore);
    // A 1 cm footprint avoids zero-width ray misses at shared convex edges.
    // Runtime seam probes found a 0.00002 cm boundary offset at the bridge;
    // even a 0.1 cm sphere hits the correct +4 m surface, as do all pawn capsules.
    // Search near the authored elevation so upper floors do not snap to roofs.
    if (GetWorld()->SweepSingleByChannel(Hit, Desired + FVector(0, 0, 250),
        Desired - FVector(0, 0, 500), FQuat::Identity, ECC_Visibility,
        FCollisionShape::MakeSphere(1.0f), Params))
    {
        return FVector(Desired.X, Desired.Y, Hit.ImpactPoint.Z + HalfHeight + 2.0f);
    }
    return Desired + FVector(0, 0, HalfHeight + 2.0f);
}

void AHeliosEnemyExtendedQA::AimAt(AHeliosEnemyActor* Target)
{
    if (PlayerController.IsValid() && Player.IsValid() && IsValid(Target))
    {
        PlayerController->SetControlRotation(
            (Target->GetActorLocation() - Player->FirstPersonCamera->GetComponentLocation()).Rotation());
    }
}

void AHeliosEnemyExtendedQA::SendFire(EInputEvent Event)
{
    if (PlayerController.IsValid())
    {
        PlayerController->InputKey(FInputKeyEventArgs::CreateSimulated(
            EKeys::LeftMouseButton, Event, Event == IE_Released ? 0.0f : 1.0f));
    }
}

void AHeliosEnemyExtendedQA::ReleaseFire()
{
    if (bFireHeld)
    {
        SendFire(IE_Released);
        bFireHeld = false;
    }
}

void AHeliosEnemyExtendedQA::PlacePerformanceCombatants()
{
    const FVector PlayerBase(-6200.0f, -4400.0f, 0.0f);
    Player->SetActorLocationAndRotation(Grounded(PlayerBase, Player.Get()), FRotator(0, 0, 0),
        false, nullptr, ETeleportType::TeleportPhysics);
    Player->GetCharacterMovement()->StopMovementImmediately();
    const TArray<AHeliosEnemyActor*> Enemies = Director->GetSpawnedEnemies();
    PerformanceInitialLocations.Reset();
    TArray<TSharedPtr<FJsonValue>> PlacementJson;
    for (int32 Index = 0; Index < Enemies.Num(); ++Index)
    {
        AHeliosEnemyActor* Enemy = Enemies[Index];
        if (!IsValid(Enemy)) continue;
        const float Alpha = Enemies.Num() > 1
            ? static_cast<float>(Index) / static_cast<float>(Enemies.Num() - 1) : 0.5f;
        // The four-enemy layout is retained because it passed the full 180 s
        // measurement. Eight enemies use one equally distant arc in the open
        // south apron: no pawn sits behind another, and the arc stays on the
        // same unobstructed side that all four actors acquired in Perf4.
        const bool bEightEnemyLayout = Enemies.Num() > 4;
        const float Angle = bEightEnemyLayout
            ? FMath::Lerp(-42.0f, 0.0f, Alpha)
            : FMath::Lerp(-28.0f, 28.0f, Alpha);
        const float Distance = bEightEnemyLayout
            ? 1050.0f
            : 800.0f + static_cast<float>(Index % 4) * 220.0f;
        const FVector Offset = FRotator(0, Angle, 0).RotateVector(FVector(Distance, 0, 0));
        const FVector RequestedLocation = PlayerBase + Offset;
        const FVector PlacedLocation = Grounded(RequestedLocation, Enemy);
        Enemy->SetActorLocationAndRotation(PlacedLocation,
            (PlayerBase - (PlayerBase + Offset)).Rotation(), false, nullptr, ETeleportType::TeleportPhysics);
        if (bEightEnemyLayout)
        {
            Enemy->GetCharacterMovement()->DisableMovement();
        }
        else
        {
            Enemy->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        }
        Enemy->GetCharacterMovement()->StopMovementImmediately();
        AAIController* PlacementAI = Cast<AAIController>(Enemy->GetController());
        if (PlacementAI)
        {
            PlacementAI->StopMovement();
            PlacementAI->SetControlRotation((Player->GetActorLocation() - Enemy->GetActorLocation()).Rotation());
            if (PlacementAI->GetPerceptionComponent())
            {
                PlacementAI->GetPerceptionComponent()->RequestStimuliListenerUpdate();
            }
        }
        PerformanceInitialLocations.Add(Enemy->GetRuntimeActorId(), Enemy->GetActorLocation());
        TSharedPtr<FJsonObject> Placement = MakeShared<FJsonObject>();
        Placement->SetStringField(TEXT("runtime_id"), Enemy->GetRuntimeActorId());
        Placement->SetStringField(TEXT("type"), Enemy->GetEnemyTypeId());
        Placement->SetNumberField(TEXT("arc_angle_degrees"), Angle);
        Placement->SetNumberField(TEXT("planned_distance_cm"), Distance);
        Placement->SetArrayField(TEXT("requested_location"), VectorJson(RequestedLocation)->AsArray());
        Placement->SetArrayField(TEXT("initial_location"), VectorJson(Enemy->GetActorLocation())->AsArray());
        Placement->SetNumberField(TEXT("initial_distance_to_player_cm"),
            FVector::Dist(Enemy->GetActorLocation(), Player->GetActorLocation()));
        Placement->SetBoolField(TEXT("initial_line_of_sight_to_player"),
            PlacementAI && PlacementAI->LineOfSightTo(Player.Get()));
        PlacementJson.Add(MakeShared<FJsonValueObject>(Placement));
    }
    Report->SetStringField(TEXT("performance_combat_layout"), Enemies.Num() > 4
        ? TEXT("eight_actor_equal_radius_open_south_arc")
        : TEXT("established_four_actor_fan"));
    Report->SetArrayField(TEXT("performance_initial_placement"), PlacementJson);
}

void AHeliosEnemyExtendedQA::StartPerformance(int32 EnemyCount)
{
    RequestedEnemyCount = EnemyCount;
    Director->SpawnCount = EnemyCount;
    Director->SelectEnemyMode(5);
    Player->SetCanBeDamaged(false);
    PlacePerformanceCombatants();
    ModeStartWallSeconds = FPlatformTime::Seconds();
    PreviousFrameWallSeconds = ModeStartWallSeconds;
    bPerformanceSightStabilizing = EnemyCount > 4;
    PerformanceSightStabilizationStartWallSeconds = ModeStartWallSeconds;
    PerformanceSightStabilizationEndWallSeconds =
        ModeStartWallSeconds + PerformanceSightStabilizationSeconds;
    Report->SetNumberField(TEXT("requested_enemy_count"), EnemyCount);
    Report->SetNumberField(TEXT("pre_warmup_sight_stabilization_target_seconds"),
        bPerformanceSightStabilizing ? PerformanceSightStabilizationSeconds : 0.0);
    Report->SetStringField(TEXT("sight_stabilization_policy"),
        bPerformanceSightStabilizing
            ? TEXT("Hold all eight pawns stationary and facing Player while time-sliced AI Sight processes every listener; require real perception and StateTree target acquisition before the 15s warmup.")
            : TEXT("Disabled for the established four-enemy layout."));
    Report->SetNumberField(TEXT("warmup_target_seconds"), 15.0);
    Report->SetNumberField(TEXT("measurement_target_seconds"), 180.0);
    Report->SetObjectField(TEXT("environment"), MakeEnvironmentJson());
}

void AHeliosEnemyExtendedQA::TickPerformance()
{
    const double Now = FPlatformTime::Seconds();
    if (bPerformanceSightStabilizing)
    {
        const TArray<AHeliosEnemyActor*> Enemies = Director->GetSpawnedEnemies();
        for (AHeliosEnemyActor* Enemy : Enemies)
        {
            if (!IsValid(Enemy)) continue;
            const FRotator Facing = (Player->GetActorLocation() - Enemy->GetActorLocation()).Rotation();
            Enemy->SetActorRotation(FRotator(0.0f, Facing.Yaw, 0.0f), ETeleportType::TeleportPhysics);
            Enemy->GetCharacterMovement()->DisableMovement();
            Enemy->GetCharacterMovement()->StopMovementImmediately();
            if (AAIController* AI = Cast<AAIController>(Enemy->GetController()))
            {
                AI->SetControlRotation(Facing);
            }
        }
        if (Now < PerformanceSightStabilizationEndWallSeconds)
        {
            return;
        }

        int32 StabilizedTargetCount = 0;
        TArray<TSharedPtr<FJsonValue>> StabilizationJson;
        for (AHeliosEnemyActor* Enemy : Enemies)
        {
            if (!IsValid(Enemy)) continue;
            AAIController* AI = Cast<AAIController>(Enemy->GetController());
            const AShooterAIController* ShooterAI = Cast<AShooterAIController>(AI);
            AActor* CurrentTarget = ShooterAI ? ShooterAI->GetCurrentTarget() : nullptr;
            TArray<AActor*> PerceivedActors;
            if (AI && AI->GetPerceptionComponent())
            {
                AI->GetPerceptionComponent()->GetCurrentlyPerceivedActors(nullptr, PerceivedActors);
            }
            const bool bPerceivesPlayer = PerceivedActors.Contains(Player.Get());
            const bool bTargetsPlayer = IsValid(CurrentTarget) && CurrentTarget == Player.Get();
            if (bPerceivesPlayer && bTargetsPlayer)
            {
                ++StabilizedTargetCount;
            }

            TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("runtime_id"), Enemy->GetRuntimeActorId());
            Item->SetStringField(TEXT("type"), Enemy->GetEnemyTypeId());
            Item->SetArrayField(TEXT("location"), VectorJson(Enemy->GetActorLocation())->AsArray());
            Item->SetNumberField(TEXT("actor_yaw_degrees"), Enemy->GetActorRotation().Yaw);
            Item->SetNumberField(TEXT("controller_yaw_degrees"),
                AI ? AI->GetControlRotation().Yaw : 0.0f);
            Item->SetBoolField(TEXT("perceives_player"), bPerceivesPlayer);
            Item->SetNumberField(TEXT("currently_perceived_actor_count"), PerceivedActors.Num());
            Item->SetStringField(TEXT("current_target"),
                IsValid(CurrentTarget) ? CurrentTarget->GetPathName() : TEXT("None"));
            Item->SetBoolField(TEXT("current_target_is_player"), bTargetsPlayer);
            Item->SetBoolField(TEXT("state_tree_brain_running"),
                AI && AI->GetBrainComponent() && AI->GetBrainComponent()->IsRunning());
            StabilizationJson.Add(MakeShared<FJsonValueObject>(Item));

            Enemy->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            if (AI && AI->GetPerceptionComponent())
            {
                AI->GetPerceptionComponent()->RequestStimuliListenerUpdate();
            }
        }
        bPerformanceSightStabilizing = false;
        Report->SetNumberField(TEXT("pre_warmup_sight_stabilization_actual_seconds"),
            Now - PerformanceSightStabilizationStartWallSeconds);
        Report->SetNumberField(TEXT("players_acquired_after_sight_stabilization"),
            StabilizedTargetCount);
        Report->SetArrayField(TEXT("sight_stabilization"), StabilizationJson);
        if (StabilizedTargetCount != RequestedEnemyCount)
        {
            Report->SetStringField(TEXT("error"), FString::Printf(
                TEXT("Sight stabilization acquired Player on %d of %d enemies"),
                StabilizedTargetCount, RequestedEnemyCount));
            SaveAndFinish(TEXT("failed"));
            return;
        }

        // The requested 15-second warmup starts only after every sight listener
        // has completed a real perception update and normal movement resumes.
        ModeStartWallSeconds = Now;
        PreviousFrameWallSeconds = Now;
        return;
    }
    if (SampleStartWallSeconds <= 0.0)
    {
        if (Now - ModeStartWallSeconds >= 15.0)
        {
            SampleStartWallSeconds = Now;
            PreviousFrameWallSeconds = Now;
            CollectEnemyCounters(PerformanceStartAlive, PerformanceStartAttackers,
                PerformanceStartNotifies, PerformanceStartCommitted);
            PerformanceEnemyBaselines.Reset();
            for (const AHeliosEnemyActor* Enemy : Director->GetSpawnedEnemies())
            {
                if (!IsValid(Enemy)) continue;
                FEnemyMeasurementBaseline& Baseline = PerformanceEnemyBaselines.FindOrAdd(
                    Enemy->GetRuntimeActorId());
                Baseline.AttackNotifies = Enemy->GetAttackNotifyCount();
                Baseline.AttacksCommitted = Enemy->GetAttacksCommitted();
            }
        }
        return;
    }
    const double FrameSeconds = Now - PreviousFrameWallSeconds;
    PreviousFrameWallSeconds = Now;
    if (FrameSeconds > 0.0)
    {
        PerformanceFrameSeconds.Add(FrameSeconds);
    }
    if (Now - SampleStartWallSeconds >= 180.0)
    {
        FinishPerformance();
    }
}

void AHeliosEnemyExtendedQA::CollectEnemyCounters(int32& OutAlive, int32& OutAttackers,
    int32& OutNotifies, int32& OutCommitted) const
{
    OutAlive = OutAttackers = OutNotifies = OutCommitted = 0;
    if (!Director.IsValid()) return;
    for (const AHeliosEnemyActor* Enemy : Director->GetSpawnedEnemies())
    {
        if (!IsValid(Enemy)) continue;
        if (!Enemy->IsEnemyDead()) ++OutAlive;
        if (Enemy->GetAttackNotifyCount() > 0) ++OutAttackers;
        OutNotifies += Enemy->GetAttackNotifyCount();
        OutCommitted += Enemy->GetAttacksCommitted();
    }
}

void AHeliosEnemyExtendedQA::FinishPerformance()
{
    const double Now = FPlatformTime::Seconds();
    double Sum = 0.0;
    for (double Sample : PerformanceFrameSeconds) Sum += Sample;
    int32 Alive, Attackers, Notifies, Committed;
    CollectEnemyCounters(Alive, Attackers, Notifies, Committed);
    TSharedPtr<FJsonObject> Metrics = MakeShared<FJsonObject>();
    Metrics->SetNumberField(TEXT("actual_warmup_seconds"), SampleStartWallSeconds - ModeStartWallSeconds);
    Metrics->SetNumberField(TEXT("actual_measured_seconds"), Now - SampleStartWallSeconds);
    Metrics->SetNumberField(TEXT("wallclock_frame_samples"), PerformanceFrameSeconds.Num());
    Metrics->SetNumberField(TEXT("average_fps"), Sum > 0.0 ? PerformanceFrameSeconds.Num() / Sum : 0.0);
    Metrics->SetNumberField(TEXT("p95_frame_ms"), PercentileSeconds(PerformanceFrameSeconds, 0.95) * 1000.0);
    Metrics->SetNumberField(TEXT("p99_frame_ms"), PercentileSeconds(PerformanceFrameSeconds, 0.99) * 1000.0);
    Metrics->SetNumberField(TEXT("spawned_enemies"), Director->GetSpawnedEnemies().Num());
    Metrics->SetNumberField(TEXT("alive_enemies"), Alive);
    Metrics->SetNumberField(TEXT("alive_enemies_at_measurement_start"), PerformanceStartAlive);
    Metrics->SetNumberField(TEXT("enemies_with_attack_notify"), Attackers);
    Metrics->SetNumberField(TEXT("attack_notifies"), Notifies);
    Metrics->SetNumberField(TEXT("attacks_committed"), Committed);
    Metrics->SetNumberField(TEXT("attack_notifies_during_measurement"),
        FMath::Max(0, Notifies - PerformanceStartNotifies));
    Metrics->SetNumberField(TEXT("attacks_committed_during_measurement"),
        FMath::Max(0, Committed - PerformanceStartCommitted));
    TArray<TSharedPtr<FJsonValue>> EnemiesJson;
    int32 EnemiesActiveDuringMeasurement = 0;
    bool bEveryEnemyActiveDuringMeasurement = PerformanceEnemyBaselines.Num() == RequestedEnemyCount;
    for (const AHeliosEnemyActor* Enemy : Director->GetSpawnedEnemies())
    {
        if (!IsValid(Enemy)) continue;
        const FString RuntimeId = Enemy->GetRuntimeActorId();
        const FEnemyMeasurementBaseline* Baseline = PerformanceEnemyBaselines.Find(RuntimeId);
        const int32 NotifyStart = Baseline ? Baseline->AttackNotifies : 0;
        const int32 CommitStart = Baseline ? Baseline->AttacksCommitted : 0;
        const int32 NotifyDelta = Baseline
            ? FMath::Max(0, Enemy->GetAttackNotifyCount() - NotifyStart) : 0;
        const int32 CommitDelta = Baseline
            ? FMath::Max(0, Enemy->GetAttacksCommitted() - CommitStart) : 0;
        const bool bAlive = !Enemy->IsEnemyDead();
        const bool bActiveDuringMeasurement = Baseline && bAlive && NotifyDelta > 0 && CommitDelta > 0;
        if (bActiveDuringMeasurement) ++EnemiesActiveDuringMeasurement;
        bEveryEnemyActiveDuringMeasurement &= bActiveDuringMeasurement;

        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("runtime_id"), RuntimeId);
        Item->SetStringField(TEXT("type"), Enemy->GetEnemyTypeId());
        Item->SetNumberField(TEXT("hp"), Enemy->GetHealth());
        Item->SetBoolField(TEXT("alive"), bAlive);
        Item->SetBoolField(TEXT("measurement_baseline_found"), Baseline != nullptr);
        Item->SetNumberField(TEXT("attack_notifies_at_measurement_start"), NotifyStart);
        Item->SetNumberField(TEXT("attacks_committed_at_measurement_start"), CommitStart);
        Item->SetNumberField(TEXT("attack_notifies"), Enemy->GetAttackNotifyCount());
        Item->SetNumberField(TEXT("attacks_committed"), Enemy->GetAttacksCommitted());
        Item->SetNumberField(TEXT("attack_notifies_during_measurement"), NotifyDelta);
        Item->SetNumberField(TEXT("attacks_committed_during_measurement"), CommitDelta);
        Item->SetBoolField(TEXT("active_in_measured_combat"), bActiveDuringMeasurement);
        const FVector EndLocation = Enemy->GetActorLocation();
        const FVector* InitialLocation = PerformanceInitialLocations.Find(RuntimeId);
        Item->SetBoolField(TEXT("initial_location_found"), InitialLocation != nullptr);
        Item->SetArrayField(TEXT("initial_location"),
            VectorJson(InitialLocation ? *InitialLocation : FVector::ZeroVector)->AsArray());
        Item->SetArrayField(TEXT("end_location"), VectorJson(EndLocation)->AsArray());
        Item->SetArrayField(TEXT("end_velocity"), VectorJson(Enemy->GetVelocity())->AsArray());
        Item->SetNumberField(TEXT("distance_moved_from_initial_cm"), InitialLocation
            ? FVector::Dist(EndLocation, *InitialLocation) : -1.0f);
        Item->SetNumberField(TEXT("end_distance_to_player_cm"), Player.IsValid()
            ? FVector::Dist(EndLocation, Player->GetActorLocation()) : -1.0f);
        Item->SetNumberField(TEXT("combat_state"), static_cast<int32>(Enemy->GetCombatState()));

        AAIController* AI = Cast<AAIController>(Enemy->GetController());
        const UBrainComponent* Brain = AI ? AI->GetBrainComponent() : nullptr;
        const AShooterAIController* ShooterAI = Cast<AShooterAIController>(AI);
        AActor* CurrentTarget = ShooterAI ? ShooterAI->GetCurrentTarget() : nullptr;
        TArray<AActor*> PerceivedActors;
        if (AI && AI->GetPerceptionComponent())
        {
            AI->GetPerceptionComponent()->GetCurrentlyPerceivedActors(nullptr, PerceivedActors);
        }
        TArray<FString> PerceivedActorPaths;
        for (const AActor* PerceivedActor : PerceivedActors)
        {
            PerceivedActorPaths.Add(IsValid(PerceivedActor) ? PerceivedActor->GetPathName() : TEXT("Invalid"));
        }
        const UPathFollowingComponent* AIPathFollowing = AI ? AI->GetPathFollowingComponent() : nullptr;
        Item->SetBoolField(TEXT("ai_controller_valid"), AI != nullptr);
        Item->SetBoolField(TEXT("ai_brain_running"), Brain && Brain->IsRunning());
        Item->SetBoolField(TEXT("ai_brain_paused"), Brain && Brain->IsPaused());
        Item->SetStringField(TEXT("path_following_status"),
            AIPathFollowing ? AIPathFollowing->GetStatusDesc() : TEXT("missing"));
        Item->SetBoolField(TEXT("line_of_sight_to_player_at_end"),
            AI && Player.IsValid() && AI->LineOfSightTo(Player.Get()));
        Item->SetStringField(TEXT("current_target"),
            IsValid(CurrentTarget) ? CurrentTarget->GetPathName() : TEXT("None"));
        Item->SetBoolField(TEXT("current_target_is_player"),
            IsValid(CurrentTarget) && CurrentTarget == Player.Get());
        Item->SetNumberField(TEXT("currently_perceived_actor_count"), PerceivedActors.Num());
        Item->SetStringField(TEXT("currently_perceived_actors"),
            FString::Join(PerceivedActorPaths, TEXT(";")));
        EnemiesJson.Add(MakeShared<FJsonValueObject>(Item));
    }
    Metrics->SetArrayField(TEXT("enemies"), EnemiesJson);
    Metrics->SetNumberField(TEXT("measurement_enemy_baseline_count"), PerformanceEnemyBaselines.Num());
    Metrics->SetNumberField(TEXT("enemies_active_during_measurement"), EnemiesActiveDuringMeasurement);
    FIntPoint ViewportSize = FIntPoint::ZeroValue;
    if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
    {
        ViewportSize = GEngine->GameViewport->Viewport->GetSizeXY();
    }
    const FString RHI = FApp::GetGraphicsRHI();
    const bool bMedium = ConsoleInt(TEXT("sg.ViewDistanceQuality")) == 1 &&
        ConsoleInt(TEXT("sg.ShadowQuality")) == 1 && ConsoleInt(TEXT("sg.TextureQuality")) == 1 &&
        ConsoleInt(TEXT("sg.EffectsQuality")) == 1;
    const bool bD3D11 = RHI.Contains(TEXT("D3D11"), ESearchCase::IgnoreCase) ||
        RHI.Contains(TEXT("DirectX 11"), ESearchCase::IgnoreCase);
    const float ScreenPercentage = ConsoleFloat(TEXT("r.ScreenPercentage"));
    const bool bScreenPercentage100 = FMath::IsNearlyEqual(ScreenPercentage, 100.0f, 0.01f);
    const bool bRequestedEnvironment = ViewportSize == FIntPoint(1920, 1080) && bMedium && bD3D11
        && bScreenPercentage100;
    const bool bAICombatActive = Alive == RequestedEnemyCount
        && Director->GetSpawnedEnemies().Num() == RequestedEnemyCount
        && bEveryEnemyActiveDuringMeasurement;
    Metrics->SetNumberField(TEXT("r_screen_percentage"), ScreenPercentage);
    Metrics->SetBoolField(TEXT("screen_percentage_100"), bScreenPercentage100);
    Metrics->SetBoolField(TEXT("configuration_matches_1080p_medium_dx11"), bRequestedEnvironment);
    Metrics->SetBoolField(TEXT("all_enemies_active_in_measured_combat"), bAICombatActive);
    Report->SetObjectField(TEXT("performance"), Metrics);
    SaveAndFinish(Director->GetSpawnedEnemies().Num() == RequestedEnemyCount && bRequestedEnvironment && bAICombatActive
        ? TEXT("completed") : TEXT("failed"));
}

TSharedPtr<FJsonObject> AHeliosEnemyExtendedQA::MakeEnvironmentJson() const
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    FIntPoint Size = FIntPoint::ZeroValue;
    if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
    {
        Size = GEngine->GameViewport->Viewport->GetSizeXY();
    }
    Result->SetNumberField(TEXT("viewport_width"), Size.X);
    Result->SetNumberField(TEXT("viewport_height"), Size.Y);
    Result->SetStringField(TEXT("graphics_rhi"), FApp::GetGraphicsRHI());
    Result->SetNumberField(TEXT("sg_view_distance"), ConsoleInt(TEXT("sg.ViewDistanceQuality")));
    Result->SetNumberField(TEXT("sg_anti_aliasing"), ConsoleInt(TEXT("sg.AntiAliasingQuality")));
    Result->SetNumberField(TEXT("sg_shadow"), ConsoleInt(TEXT("sg.ShadowQuality")));
    Result->SetNumberField(TEXT("sg_global_illumination"), ConsoleInt(TEXT("sg.GlobalIlluminationQuality")));
    Result->SetNumberField(TEXT("sg_reflection"), ConsoleInt(TEXT("sg.ReflectionQuality")));
    Result->SetNumberField(TEXT("sg_post_process"), ConsoleInt(TEXT("sg.PostProcessQuality")));
    Result->SetNumberField(TEXT("sg_texture"), ConsoleInt(TEXT("sg.TextureQuality")));
    Result->SetNumberField(TEXT("sg_effects"), ConsoleInt(TEXT("sg.EffectsQuality")));
    Result->SetNumberField(TEXT("sg_foliage"), ConsoleInt(TEXT("sg.FoliageQuality")));
    Result->SetNumberField(TEXT("sg_shading"), ConsoleInt(TEXT("sg.ShadingQuality")));
    Result->SetNumberField(TEXT("r_screen_percentage"), ConsoleFloat(TEXT("r.ScreenPercentage")));
    Result->SetStringField(TEXT("command_line"), FCommandLine::Get());
    return Result;
}

void AHeliosEnemyExtendedQA::StartCapture()
{
    Director->SpawnCount = 1;
    Player->SetCanBeDamaged(true);
    const FString FrameDirectory = FPaths::Combine(RootDir, TEXT("qa/enemies/video_frames"));
    IFileManager::Get().MakeDirectory(*FrameDirectory, true);
    TArray<FString> ExistingFrames;
    IFileManager::Get().FindFiles(ExistingFrames, *FPaths::Combine(FrameDirectory, TEXT("frame_*.png")), true, false);
    for (const FString& ExistingFrame : ExistingFrames)
    {
        if (Mode != TEXT("recording"))
            IFileManager::Get().Delete(*FPaths::Combine(FrameDirectory, ExistingFrame), false, true);
    }
    ModeStartWallSeconds = FPlatformTime::Seconds();
    CaptureNextFrameWallSeconds = ModeStartWallSeconds;
    CaptureCompletedFramesByType.Init(0, 5);
    CaptureDamageFrameObservedByType.Init(false, 5);
    CaptureReturnFireFrameObservedByType.Init(false, 5);
    CaptureDeathFrameObservedByType.Init(false, 5);
    BeginCaptureType(1);
    Report->SetObjectField(TEXT("environment"), MakeEnvironmentJson());
}

void AHeliosEnemyExtendedQA::BeginCaptureType(int32 TypeIndex)
{
    ReleaseFire();
    CaptureType = TypeIndex;
    Director->SpawnCount = 1;
    Director->SelectEnemyMode(TypeIndex);
    Player->ResetForTest();
    Player->SetCanBeDamaged(true);
    PlaceCaptureCombatants();

    CaptureTypeId = FString::Printf(TEXT("E%02d"), TypeIndex);
    CapturePlayerStartHealth = Player->GetHealth();
    CapturePlayerMinHealth = CapturePlayerStartHealth;
    CapturePlayerMinAmmo = Player->GetAmmo();
    CaptureEnemyStartHealth = 0.0f;
    CaptureEnemyMinHealth = 0.0f;
    CaptureAttackNotifiesStart = 0;
    CaptureAttacksCommittedStart = 0;
    CaptureMaxAttackNotifies = 0;
    CaptureMaxAttacksCommitted = 0;
    bCaptureSawEnemy = false;
    bCaptureEnemyDeathObserved = false;
    bCapturePlayerDeathObserved = false;
    UpdateCaptureObservations();
}

void AHeliosEnemyExtendedQA::PlaceCaptureCombatants()
{
    const FVector PlayerBase(-6200.0f, -4400.0f, 0.0f);
    Player->SetActorLocationAndRotation(Grounded(PlayerBase, Player.Get()), FRotator(0, 0, 0),
        false, nullptr, ETeleportType::TeleportPhysics);
    Player->GetCharacterMovement()->StopMovementImmediately();
    const TArray<AHeliosEnemyActor*> Enemies = Director->GetSpawnedEnemies();
    if (!Enemies.IsEmpty() && IsValid(Enemies[0]))
    {
        AHeliosEnemyActor* Enemy = Enemies[0];
        const FVector EnemyBase = PlayerBase + FVector(900.0f, 0.0f, 0.0f);
        Enemy->SetActorLocationAndRotation(Grounded(EnemyBase, Enemy), FRotator(0, 180, 0),
            false, nullptr, ETeleportType::TeleportPhysics);
        Enemy->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        Enemy->GetCharacterMovement()->StopMovementImmediately();
        if (AAIController* AI = Cast<AAIController>(Enemy->GetController()))
        {
            AI->StopMovement();
            AI->SetControlRotation((Player->GetActorLocation() - Enemy->GetActorLocation()).Rotation());
            if (AI->GetPerceptionComponent()) AI->GetPerceptionComponent()->RequestStimuliListenerUpdate();
        }
        AimAt(Enemy);
    }
}

void AHeliosEnemyExtendedQA::FinishCaptureType()
{
    UpdateCaptureObservations();
    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetNumberField(TEXT("type_index"), CaptureType);
    Item->SetStringField(TEXT("type"), CaptureTypeId);
    Item->SetNumberField(TEXT("observation_seconds"), CaptureSecondsPerType);
    Item->SetNumberField(TEXT("return_fire_started_seconds"), CaptureReturnFireSeconds);
    const TArray<AHeliosEnemyActor*> Enemies = Director->GetSpawnedEnemies();
    if (!Enemies.IsEmpty() && IsValid(Enemies[0]))
    {
        const AHeliosEnemyActor* Enemy = Enemies[0];
        Item->SetNumberField(TEXT("enemy_hp"), Enemy->GetHealth());
        Item->SetNumberField(TEXT("attack_notifies"), Enemy->GetAttackNotifyCount());
        Item->SetNumberField(TEXT("attacks_committed"), Enemy->GetAttacksCommitted());
    }
    else
    {
        Item->SetStringField(TEXT("enemy_state"), TEXT("destroyed_or_missing"));
    }
    Item->SetNumberField(TEXT("player_hp"), Player->GetHealth());
    Item->SetNumberField(TEXT("player_ammo"), Player->GetAmmo());
    Item->SetBoolField(TEXT("player_dead"), Player->IsDead());

    const int32 NotifyDelta = FMath::Max(0, CaptureMaxAttackNotifies - CaptureAttackNotifiesStart);
    const int32 CommittedDelta = FMath::Max(0, CaptureMaxAttacksCommitted - CaptureAttacksCommittedStart);
    const bool bEnemyAttackLogicObserved = NotifyDelta > 0 && CommittedDelta > 0;
    const bool bPlayerDamageObserved = CapturePlayerMinHealth < CapturePlayerStartHealth - KINDA_SMALL_NUMBER;
    const bool bEnemyDamageObserved = bCaptureSawEnemy && CaptureEnemyMinHealth < CaptureEnemyStartHealth - KINDA_SMALL_NUMBER;
    const bool bReturnFireObserved = CapturePlayerMinAmmo < 30 && bEnemyDamageObserved;
    const bool bEnemyAttacked = bEnemyAttackLogicObserved && bPlayerDamageObserved;
    const bool bDamageVisible = CaptureDamageFrameObservedByType.IsValidIndex(CaptureType)
        && CaptureDamageFrameObservedByType[CaptureType];
    const bool bReturnFireVisible = CaptureReturnFireFrameObservedByType.IsValidIndex(CaptureType)
        && CaptureReturnFireFrameObservedByType[CaptureType];
    const bool bDeathVisible = CaptureDeathFrameObservedByType.IsValidIndex(CaptureType)
        && CaptureDeathFrameObservedByType[CaptureType];

    Item->SetNumberField(TEXT("player_start_hp"), CapturePlayerStartHealth);
    Item->SetNumberField(TEXT("player_min_hp"), CapturePlayerMinHealth);
    Item->SetNumberField(TEXT("player_min_ammo"), CapturePlayerMinAmmo);
    Item->SetNumberField(TEXT("enemy_start_hp"), CaptureEnemyStartHealth);
    Item->SetNumberField(TEXT("enemy_min_hp"), CaptureEnemyMinHealth);
    Item->SetNumberField(TEXT("attack_notify_delta"), NotifyDelta);
    Item->SetNumberField(TEXT("attacks_committed_delta"), CommittedDelta);
    Item->SetBoolField(TEXT("enemy_attack_logic_observed"), bEnemyAttackLogicObserved);
    Item->SetBoolField(TEXT("player_damage_observed"), bPlayerDamageObserved);
    Item->SetBoolField(TEXT("enemy_attack_observed"), bEnemyAttacked);
    Item->SetBoolField(TEXT("enemy_damage_observed"), bEnemyDamageObserved);
    Item->SetBoolField(TEXT("return_fire_observed"), bReturnFireObserved);
    Item->SetBoolField(TEXT("enemy_death_observed"), bCaptureEnemyDeathObserved);
    Item->SetBoolField(TEXT("player_death_observed"), bCapturePlayerDeathObserved);
    Item->SetNumberField(TEXT("completed_frames_for_type"),
        CaptureCompletedFramesByType.IsValidIndex(CaptureType) ? CaptureCompletedFramesByType[CaptureType] : 0);
    Item->SetBoolField(TEXT("player_damage_visible_in_completed_frame"), bDamageVisible);
    Item->SetBoolField(TEXT("return_fire_visible_in_completed_frame"), bReturnFireVisible);
    Item->SetBoolField(TEXT("enemy_death_visible_in_completed_frame"), bDeathVisible);
    if (!bCaptureSawEnemy || !bEnemyAttacked || !bReturnFireObserved || !bCaptureEnemyDeathObserved
        || (Mode != TEXT("recording") && (!bDamageVisible || !bReturnFireVisible || !bDeathVisible)))
    {
        ++CaptureCombatFailures;
    }
    CaptureTypesJson.Add(MakeShared<FJsonValueObject>(Item));
}

void AHeliosEnemyExtendedQA::UpdateCaptureObservations()
{
    CapturePlayerMinHealth = FMath::Min(CapturePlayerMinHealth, Player->GetHealth());
    CapturePlayerMinAmmo = FMath::Min(CapturePlayerMinAmmo, Player->GetAmmo());
    bCapturePlayerDeathObserved |= Player->IsDead();

    const TArray<AHeliosEnemyActor*> Enemies = Director->GetSpawnedEnemies();
    if (!Enemies.IsEmpty() && IsValid(Enemies[0]))
    {
        const AHeliosEnemyActor* Enemy = Enemies[0];
        if (!bCaptureSawEnemy)
        {
            bCaptureSawEnemy = true;
            CaptureTypeId = Enemy->GetEnemyTypeId();
            CaptureEnemyStartHealth = Enemy->GetHealth();
            CaptureEnemyMinHealth = CaptureEnemyStartHealth;
            CaptureAttackNotifiesStart = Enemy->GetAttackNotifyCount();
            CaptureAttacksCommittedStart = Enemy->GetAttacksCommitted();
        }
        CaptureEnemyMinHealth = FMath::Min(CaptureEnemyMinHealth, Enemy->GetHealth());
        CaptureMaxAttackNotifies = FMath::Max(CaptureMaxAttackNotifies, Enemy->GetAttackNotifyCount());
        CaptureMaxAttacksCommitted = FMath::Max(CaptureMaxAttacksCommitted, Enemy->GetAttacksCommitted());
        bCaptureEnemyDeathObserved |= Enemy->IsEnemyDead();
    }
}

void AHeliosEnemyExtendedQA::TickCapture()
{
    const double Now = FPlatformTime::Seconds();
    const double Elapsed = Now - ModeStartWallSeconds;

    if (!bCaptureEnding)
    {
        UpdateCaptureObservations();
        const int32 DesiredType = FMath::Clamp(static_cast<int32>(Elapsed / CaptureSecondsPerType) + 1, 1, 4);
        if (DesiredType != CaptureType)
        {
            FinishCaptureType();
            BeginCaptureType(DesiredType);
        }
        const TArray<AHeliosEnemyActor*> Enemies = Director->GetSpawnedEnemies();
        AHeliosEnemyActor* Enemy = !Enemies.IsEmpty() ? Enemies[0] : nullptr;
        if (IsValid(Enemy)) AimAt(Enemy);

        const double WithinType = Elapsed - static_cast<double>(CaptureType - 1) * CaptureSecondsPerType;
        if (WithinType >= CaptureReturnFireSeconds && !bFireHeld)
        {
            SendFire(IE_Pressed);
            bFireHeld = true;
        }
        else if (WithinType < CaptureReturnFireSeconds)
        {
            ReleaseFire();
        }
    }

    if (!CaptureOutstandingPath.IsEmpty())
    {
        if (!FScreenshotRequest::IsScreenshotRequested() &&
            IFileManager::Get().FileSize(*CaptureOutstandingPath) > 0)
        {
            ++CaptureFramesCompleted;
            const double CompletedSeconds = Now - ModeStartWallSeconds;
            CaptureCompletionTimes.Add(CompletedSeconds);
            TSharedPtr<FJsonObject> CompletedFrame = MakeShared<FJsonObject>();
            CompletedFrame->SetNumberField(TEXT("frame_index"), CaptureOutstandingFrameIndex);
            CompletedFrame->SetStringField(TEXT("file"), FString::Printf(TEXT("frame_%05d.png"), CaptureOutstandingFrameIndex));
            CompletedFrame->SetNumberField(TEXT("type_index"), CaptureOutstandingType);
            CompletedFrame->SetNumberField(TEXT("requested_seconds"), CaptureOutstandingRequestedSeconds);
            CompletedFrame->SetNumberField(TEXT("completed_seconds"), CompletedSeconds);
            CompletedFrame->SetNumberField(TEXT("player_hp"), CaptureOutstandingPlayerHealth);
            CompletedFrame->SetNumberField(TEXT("player_ammo"), CaptureOutstandingPlayerAmmo);
            CompletedFrame->SetNumberField(TEXT("enemy_hp"), CaptureOutstandingEnemyHealth);
            CompletedFrame->SetNumberField(TEXT("attack_notifies"), CaptureOutstandingAttackNotifies);
            CompletedFrame->SetNumberField(TEXT("attacks_committed"), CaptureOutstandingAttacksCommitted);
            CompletedFrame->SetBoolField(TEXT("player_damage_observed"), bCaptureOutstandingPlayerDamaged);
            CompletedFrame->SetBoolField(TEXT("return_fire_observed"), bCaptureOutstandingReturnFire);
            CompletedFrame->SetBoolField(TEXT("enemy_dead"), bCaptureOutstandingEnemyDead);
            CaptureCompletedFramesJson.Add(MakeShared<FJsonValueObject>(CompletedFrame));
            if (CaptureCompletedFramesByType.IsValidIndex(CaptureOutstandingType))
            {
                ++CaptureCompletedFramesByType[CaptureOutstandingType];
                CaptureDamageFrameObservedByType[CaptureOutstandingType] |= bCaptureOutstandingPlayerDamaged;
                CaptureReturnFireFrameObservedByType[CaptureOutstandingType] |= bCaptureOutstandingReturnFire;
                CaptureDeathFrameObservedByType[CaptureOutstandingType] |= bCaptureOutstandingEnemyDead;
            }
            CaptureOutstandingPath.Reset();
            CaptureOutstandingFrameIndex = INDEX_NONE;
        }
        else if (Now - CaptureOutstandingSince > 5.0)
        {
            ++CaptureFailures;
            CaptureOutstandingPath.Reset();
            CaptureOutstandingFrameIndex = INDEX_NONE;
        }
    }
    if (Mode != TEXT("recording") && !bCaptureEnding && CaptureOutstandingPath.IsEmpty() && Now >= CaptureNextFrameWallSeconds)
    {
        RequestCaptureFrame();
        CaptureNextFrameWallSeconds = Now + 0.1;
    }

    if (!bCaptureEnding && Elapsed >= CaptureTotalSeconds)
    {
        bCaptureEnding = true;
        ReleaseFire();
    }
    if (bCaptureEnding && CaptureOutstandingPath.IsEmpty())
    {
        // Finalize the last type only after its final requested image has
        // completed. A kill on the last frame must be backed by the saved PNG.
        FinishCaptureType();
        FinishCapture();
    }
}

void AHeliosEnemyExtendedQA::RequestCaptureFrame()
{
    UpdateCaptureObservations();
    CaptureOutstandingFrameIndex = CaptureFrameIndex++;
    CaptureOutstandingType = CaptureType;
    CaptureOutstandingRequestedSeconds = FPlatformTime::Seconds() - ModeStartWallSeconds;
    CaptureOutstandingPlayerHealth = Player->GetHealth();
    CaptureOutstandingPlayerAmmo = Player->GetAmmo();
    CaptureOutstandingEnemyHealth = 0.0f;
    CaptureOutstandingAttackNotifies = 0;
    CaptureOutstandingAttacksCommitted = 0;
    bCaptureOutstandingPlayerDamaged = Player->GetHealth() < CapturePlayerStartHealth - KINDA_SMALL_NUMBER;
    bCaptureOutstandingReturnFire = false;
    bCaptureOutstandingEnemyDead = false;
    const TArray<AHeliosEnemyActor*> Enemies = Director->GetSpawnedEnemies();
    if (!Enemies.IsEmpty() && IsValid(Enemies[0]))
    {
        const AHeliosEnemyActor* Enemy = Enemies[0];
        CaptureOutstandingEnemyHealth = Enemy->GetHealth();
        CaptureOutstandingAttackNotifies = Enemy->GetAttackNotifyCount();
        CaptureOutstandingAttacksCommitted = Enemy->GetAttacksCommitted();
        bCaptureOutstandingEnemyDead = Enemy->IsEnemyDead();
        bCaptureOutstandingReturnFire = bFireHeld && Player->GetAmmo() < 30
            && Enemy->GetHealth() < CaptureEnemyStartHealth - KINDA_SMALL_NUMBER;
    }
    CaptureOutstandingPath = FPaths::Combine(RootDir, TEXT("qa/enemies/video_frames"),
        FString::Printf(TEXT("frame_%05d.png"), CaptureOutstandingFrameIndex));
    CaptureOutstandingSince = FPlatformTime::Seconds();
    ++CaptureFramesRequested;
    FScreenshotRequest::RequestScreenshot(CaptureOutstandingPath, false, false);
}

void AHeliosEnemyExtendedQA::FinishCapture()
{
    const double ActualDuration = FPlatformTime::Seconds() - ModeStartWallSeconds;
    TArray<double> Intervals;
    for (int32 Index = 1; Index < CaptureCompletionTimes.Num(); ++Index)
    {
        Intervals.Add(CaptureCompletionTimes[Index] - CaptureCompletionTimes[Index - 1]);
    }
    double Sum = 0.0;
    for (double Interval : Intervals) Sum += Interval;
    TSharedPtr<FJsonObject> Capture = MakeShared<FJsonObject>();
    Capture->SetNumberField(TEXT("target_seconds"), CaptureTotalSeconds);
    Capture->SetNumberField(TEXT("seconds_per_type"), CaptureSecondsPerType);
    Capture->SetNumberField(TEXT("return_fire_started_seconds_per_type"), CaptureReturnFireSeconds);
    Capture->SetNumberField(TEXT("actual_seconds"), ActualDuration);
    Capture->SetNumberField(TEXT("frames_requested"), CaptureFramesRequested);
    Capture->SetNumberField(TEXT("frames_completed"), CaptureFramesCompleted);
    Capture->SetNumberField(TEXT("screenshot_failures"), CaptureFailures);
    Capture->SetNumberField(TEXT("combat_observation_failures"), CaptureCombatFailures);
    Capture->SetNumberField(TEXT("average_completed_frame_interval_seconds"),
        Intervals.Num() > 0 ? Sum / Intervals.Num() : 0.0);
    Capture->SetNumberField(TEXT("p95_completed_frame_interval_seconds"), PercentileSeconds(Intervals, 0.95));
    Capture->SetNumberField(TEXT("p99_completed_frame_interval_seconds"), PercentileSeconds(Intervals, 0.99));
    if (!Intervals.IsEmpty())
    {
        TArray<double> SortedIntervals = Intervals;
        SortedIntervals.Sort();
        Capture->SetNumberField(TEXT("minimum_completed_frame_interval_seconds"), SortedIntervals[0]);
        Capture->SetNumberField(TEXT("maximum_completed_frame_interval_seconds"), SortedIntervals.Last());
        Capture->SetNumberField(TEXT("effective_capture_fps"), Sum > 0.0 ? Intervals.Num() / Sum : 0.0);
    }
    Capture->SetArrayField(TEXT("types"), CaptureTypesJson);
    Capture->SetArrayField(TEXT("completed_frames"), CaptureCompletedFramesJson);
    TArray<TSharedPtr<FJsonValue>> CompletionTimesJson;
    CompletionTimesJson.Reserve(CaptureCompletionTimes.Num());
    for (const double Time : CaptureCompletionTimes)
    {
        CompletionTimesJson.Add(MakeShared<FJsonValueNumber>(Time));
    }
    Capture->SetArrayField(TEXT("frame_completion_times_seconds"), CompletionTimesJson);
    Capture->SetStringField(TEXT("frame_directory"), TEXT("qa/enemies/video_frames"));
    Capture->SetStringField(TEXT("visual_evidence_source"), Mode == TEXT("recording")
        ? TEXT("External exact-window video; runtime verifies combat only. Video requires separate visual review.")
        : TEXT("Completed Unreal viewport screenshots"));
    Report->SetObjectField(TEXT("capture"), Capture);
    SaveAndFinish(CaptureFailures == 0 && CaptureCombatFailures == 0 && (Mode == TEXT("recording") || CaptureFramesCompleted > 0)
        ? TEXT("completed") : TEXT("failed"));
}

bool AHeliosEnemyExtendedQA::LoadRoutes()
{
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(RootDir, TEXT("exports/layout.json")))) return false;
    TSharedPtr<FJsonObject> Layout;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Layout) || !Layout.IsValid()) return false;
    const TArray<TSharedPtr<FJsonValue>>* RoutesArray = nullptr;
    if (!Layout->TryGetArrayField(TEXT("routes"), RoutesArray) || !RoutesArray) return false;
    for (const TSharedPtr<FJsonValue>& RouteValue : *RoutesArray)
    {
        const TSharedPtr<FJsonObject> RouteObject = RouteValue->AsObject();
        if (!RouteObject.IsValid()) continue;
        FRouteDefinition Route;
        Route.Id = RouteObject->GetStringField(TEXT("id"));
        const TArray<TSharedPtr<FJsonValue>>* Waypoints = nullptr;
        if (!RouteObject->TryGetArrayField(TEXT("waypoints"), Waypoints) || !Waypoints) continue;
        for (const TSharedPtr<FJsonValue>& WaypointValue : *Waypoints)
        {
            const TArray<TSharedPtr<FJsonValue>>& XYZ = WaypointValue->AsArray();
            if (XYZ.Num() >= 3)
            {
                Route.Waypoints.Add(FVector(XYZ[0]->AsNumber() * 100.0,
                    XYZ[1]->AsNumber() * 100.0, XYZ[2]->AsNumber() * 100.0));
            }
        }
        if (Route.Waypoints.Num() >= 2) Routes.Add(MoveTemp(Route));
    }
    return Routes.Num() == 3;
}

void AHeliosEnemyExtendedQA::StartNavigation()
{
    Player->SetCanBeDamaged(false);
    Player->GetCharacterMovement()->StopMovementImmediately();
    bNavigationPlayerCollisionWasEnabled = Player->GetActorEnableCollision();
    bNavigationPlayerCollisionOverrideActive = true;
    Player->SetActorEnableCollision(false);
    Report->SetStringField(TEXT("navigation_arrival_policy"),
        TEXT("Arrival uses the same coordinate space as UE 5.8 PathFollowing: movement-component feet to the projected navigation goal, horizontal <=75cm and vertical <=1.05*capsule half-height. "
            "The MoveTo request keeps the established 75cm acceptance and excludes agent overlap. Actor-center to authored-center and actor-center to nav-goal errors are reported separately for route auditing."));
    Report->SetStringField(TEXT("navigation_timeout_policy"),
        TEXT("clamp(planned nav path length / MaxWalkSpeed * 1.35 + 3s, 20s, 90s)."));
    Report->SetStringField(TEXT("navigation_collision_metric"),
        TEXT("wall_contact_samples: raw non-floor OnComponentHit callbacks, cumulative per route case; not unique wall collisions."));
    if (!LoadRoutes())
    {
        Report->SetStringField(TEXT("error"), TEXT("exports/layout.json did not provide exactly three usable routes"));
        SaveAndFinish(TEXT("failed"));
        return;
    }
    int32 RequestedNavigationType = 0;
    if (FParse::Value(FCommandLine::Get(), TEXT("HeliosNavigationType="), RequestedNavigationType))
    {
        if (RequestedNavigationType < 1 || RequestedNavigationType > 4)
        {
            Report->SetStringField(TEXT("error"), TEXT("HeliosNavigationType must be an integer from 1 through 4"));
            SaveAndFinish(TEXT("failed"));
            return;
        }
        NavigationTypeIndex = RequestedNavigationType;
        NavigationLastTypeIndex = RequestedNavigationType;
        Report->SetNumberField(TEXT("navigation_type_filter"), RequestedNavigationType);
        ReportPath = FPaths::Combine(RootDir, TEXT("qa/enemies"),
            FString::Printf(TEXT("extended_navigation_type_%02d.json"), RequestedNavigationType));
    }
    else
    {
        NavigationTypeIndex = 1;
        NavigationLastTypeIndex = 4;
        Report->SetStringField(TEXT("navigation_type_filter"), TEXT("all"));
    }
    ModeStartWallSeconds = FPlatformTime::Seconds();
    StartNavigationCase();
}

void AHeliosEnemyExtendedQA::StartNavigationCase()
{
    Director->SpawnCount = 1;
    Director->SelectEnemyMode(NavigationTypeIndex);
    NavigationEnemy.Reset();
    NavigationController.Reset();
    NavigationWaypointsJson.Reset();
    NavigationActualPathJson.Reset();
    NavigationCollisionCount = 0;
    NavigationFirstCollisionActor.Reset();
    NavigationFirstCollisionOtherComponent.Reset();
    NavigationFirstCollisionOwnComponent.Reset();
    NavigationFirstCollisionImpactPoint = FVector::ZeroVector;
    bNavigationFirstCollisionRecorded = false;
    NavigationCaseFailedWaypoints = 0;
    NavigationWaypointIndex = 0;
    NavigationWaypointStart = FPlatformTime::Seconds();
    bNavigationCasePreparing = true;
    NavigationIssuedRequestId = MAX_uint32;
    NavigationPathPointCountAtIssue = 0;
    bNavigationPathWasPartialAtIssue = false;
    bNavigationPathEndAtIssueValid = false;
    bNavigationMoveCompletionObserved = false;
    NavigationMoveCompletionResult.Reset();
    NavigationPathEndAtIssue = FVector::ZeroVector;
    NavigationMoveFinishedHandle.Reset();
}

void AHeliosEnemyExtendedQA::TickNavigation(float DeltaSeconds)
{
    if (bNavigationCasePreparing)
    {
        const TArray<AHeliosEnemyActor*> Enemies = Director->GetSpawnedEnemies();
        if (!Enemies.IsEmpty() && IsValid(Enemies[0]) && Cast<AAIController>(Enemies[0]->GetController()))
        {
            NavigationEnemy = Enemies[0];
            NavigationController = Cast<AAIController>(Enemies[0]->GetController());
            if (UPathFollowingComponent* PathFollowing = NavigationController->GetPathFollowingComponent())
            {
                NavigationMoveFinishedHandle = PathFollowing->OnRequestFinished.AddUObject(
                    this, &AHeliosEnemyExtendedQA::OnNavigationMoveFinished);
            }
            if (NavigationController->GetBrainComponent())
            {
                NavigationController->GetBrainComponent()->PauseLogic(TEXT("Extended navigation QA owns route target"));
            }
            NavigationEnemy->GetCapsuleComponent()->OnComponentHit.AddDynamic(
                this, &AHeliosEnemyExtendedQA::OnNavigationCollision);
            NavigationEnemy->GetCapsuleComponent()->SetNotifyRigidBodyCollision(true);
            const FVector Start = Grounded(Routes[NavigationRouteIndex].Waypoints[0], NavigationEnemy.Get());
            NavigationEnemy->SetActorLocation(Start, false, nullptr, ETeleportType::TeleportPhysics);
            NavigationEnemy->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            NavigationEnemy->GetCharacterMovement()->StopMovementImmediately();
            TSharedPtr<FJsonObject> Initial = MakeShared<FJsonObject>();
            Initial->SetNumberField(TEXT("index"), 0);
            Initial->SetArrayField(TEXT("target"), VectorJson(Start)->AsArray());
            Initial->SetBoolField(TEXT("reached"), true);
            Initial->SetStringField(TEXT("reason"), TEXT("route_start"));
            NavigationWaypointsJson.Add(MakeShared<FJsonValueObject>(Initial));
            NavigationActualPathJson.Add(VectorJson(Start));
            NavigationWaypointIndex = 1;
            bNavigationCasePreparing = false;
            IssueNavigationWaypoint();
        }
        else if (FPlatformTime::Seconds() - NavigationWaypointStart > 10.0)
        {
            ++NavigationCaseFailedWaypoints;
            ++NavigationTotalFailedWaypoints;
            CompleteNavigationCase();
        }
        return;
    }
    if (!NavigationEnemy.IsValid() || !NavigationController.IsValid())
    {
        ++NavigationCaseFailedWaypoints;
        ++NavigationTotalFailedWaypoints;
        CompleteNavigationCase();
        return;
    }

    const double Now = FPlatformTime::Seconds();
    if (Now - NavigationLastSample >= 0.25)
    {
        NavigationLastSample = Now;
        NavigationActualPathJson.Add(VectorJson(NavigationEnemy->GetActorLocation()));
    }
    const FVector FeetLocation = NavigationEnemy->GetCharacterMovement()->GetActorFeetLocation();
    const float HorizontalDistance = FVector::Dist2D(FeetLocation, NavigationMoveGoal);
    const float VerticalDistance = FMath::Abs(FeetLocation.Z - NavigationMoveGoal.Z);
    const float CapsuleHalfHeight = NavigationEnemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const float HeightTolerance = CapsuleHalfHeight * NavigationHeightToleranceScale;
    if (HorizontalDistance <= NavigationHorizontalAcceptanceCm && VerticalDistance <= HeightTolerance)
    {
        CompleteNavigationWaypoint(true, TEXT("arrived"));
        return;
    }

    const UPathFollowingComponent* PathFollowing = NavigationController->GetPathFollowingComponent();
    if (PathFollowing && PathFollowing->GetStatus() == EPathFollowingStatus::Idle &&
        Now - NavigationWaypointStart >= 0.25)
    {
        CompleteNavigationWaypoint(false, TEXT("path_following_idle_outside_feet_tolerance"));
        return;
    }

    if (NavigationEnemy->GetVelocity().Size2D() < 5.0f)
        NavigationStuckSeconds += DeltaSeconds;
    else
        NavigationStuckSeconds = 0.0f;
    if (NavigationStuckSeconds >= 4.0f)
    {
        CompleteNavigationWaypoint(false, TEXT("stuck_4_seconds"));
    }
    else if (Now - NavigationWaypointStart >= NavigationWaypointTimeoutSeconds)
    {
        CompleteNavigationWaypoint(false, TEXT("waypoint_timeout_dynamic"));
    }
}

void AHeliosEnemyExtendedQA::IssueNavigationWaypoint()
{
    if (NavigationWaypointIndex >= Routes[NavigationRouteIndex].Waypoints.Num())
    {
        CompleteNavigationCase();
        return;
    }
    NavigationWaypointStart = FPlatformTime::Seconds();
    NavigationLastSample = NavigationWaypointStart;
    NavigationStuckSeconds = 0.0f;
    NavigationLastLocation = NavigationEnemy->GetActorLocation();
    NavigationWaypointTarget = Grounded(
        Routes[NavigationRouteIndex].Waypoints[NavigationWaypointIndex], NavigationEnemy.Get());
    NavigationMoveGoal = NavigationWaypointTarget;
    NavigationPlannedPathLengthCm = FVector::Dist2D(NavigationEnemy->GetActorLocation(), NavigationWaypointTarget);
    bNavigationPathLengthFromFollowedPath = false;
    NavigationWaypointTimeoutSeconds = NavigationMinimumTimeoutSeconds;
    NavigationIssuedRequestId = MAX_uint32;
    NavigationPathPointCountAtIssue = 0;
    bNavigationPathWasPartialAtIssue = false;
    bNavigationPathEndAtIssueValid = false;
    NavigationPathEndAtIssue = FVector::ZeroVector;
    bNavigationMoveCompletionObserved = false;
    NavigationMoveCompletionResult.Reset();
    NavigationRequestAcceptanceCm = NavigationHorizontalAcceptanceCm;
    // Keep the established route request byte-for-byte equivalent at the API
    // boundary. In particular, MoveToLocation aborts an active prior request;
    // the lower-level MoveTo overload does not guarantee that behavior.
    const EPathFollowingRequestResult::Type Result = NavigationController->MoveToLocation(
        NavigationWaypointTarget, NavigationRequestAcceptanceCm, false, true, true, true, nullptr, false);
    UPathFollowingComponent* IssuedPathFollowing = NavigationController->GetPathFollowingComponent();
    NavigationIssuedRequestId = IssuedPathFollowing
        ? IssuedPathFollowing->GetCurrentRequestId().GetID() : MAX_uint32;
    if (Result == EPathFollowingRequestResult::Failed)
    {
        CompleteNavigationWaypoint(false, TEXT("move_request_failed"));
        return;
    }

    const float MovementSpeed = FMath::Max(
        NavigationEnemy->GetCharacterMovement()->MaxWalkSpeed, 1.0f);
    if (Result == EPathFollowingRequestResult::AlreadyAtGoal)
    {
        NavigationWaypointTimeoutSeconds = FMath::Clamp(
            NavigationPlannedPathLengthCm / MovementSpeed * NavigationTimeoutSpeedScale + NavigationTimeoutGraceSeconds,
            NavigationMinimumTimeoutSeconds, NavigationMaximumTimeoutSeconds);
        CompleteNavigationWaypoint(true, TEXT("already_at_goal"));
        return;
    }

    const FNavPathSharedPtr Path = IssuedPathFollowing
        ? IssuedPathFollowing->GetPath() : FNavPathSharedPtr();
    if (Path.IsValid() && Path->IsValid())
    {
        NavigationPlannedPathLengthCm = Path->GetLength();
        bNavigationPathLengthFromFollowedPath = true;
        const TArray<FNavPathPoint>& PathPoints = Path->GetPathPoints();
        NavigationPathPointCountAtIssue = PathPoints.Num();
        bNavigationPathWasPartialAtIssue = Path->IsPartial();
        if (!PathPoints.IsEmpty())
        {
            NavigationMoveGoal = PathPoints.Last().Location;
            NavigationPathEndAtIssue = PathPoints.Last().Location;
            bNavigationPathEndAtIssueValid = true;
        }
    }

    NavigationWaypointTimeoutSeconds = FMath::Clamp(
        NavigationPlannedPathLengthCm / MovementSpeed * NavigationTimeoutSpeedScale + NavigationTimeoutGraceSeconds,
        NavigationMinimumTimeoutSeconds, NavigationMaximumTimeoutSeconds);
}

void AHeliosEnemyExtendedQA::CompleteNavigationWaypoint(bool bReached, const FString& Reason)
{
    const FVector CurrentLocation = NavigationEnemy->GetActorLocation();
    const float CenterToNavHorizontalDistance = FVector::Dist2D(CurrentLocation, NavigationMoveGoal);
    const float CenterToNavVerticalDistance = FMath::Abs(CurrentLocation.Z - NavigationMoveGoal.Z);
    const float AuthoredHorizontalDistance = FVector::Dist2D(CurrentLocation, NavigationWaypointTarget);
    const float AuthoredVerticalDistance = FMath::Abs(CurrentLocation.Z - NavigationWaypointTarget.Z);
    const UPathFollowingComponent* PathFollowing = NavigationController.IsValid()
        ? NavigationController->GetPathFollowingComponent() : nullptr;
    const FNavPathSharedPtr LivePath = PathFollowing ? PathFollowing->GetPath() : FNavPathSharedPtr();
    const FVector LiveTarget = PathFollowing ? PathFollowing->GetCurrentTargetLocation() : FVector::ZeroVector;
    const FVector LiveDestination = LivePath.IsValid() ? LivePath->GetDestinationLocation() : FVector::ZeroVector;
    const FVector FeetLocation = NavigationEnemy->GetCharacterMovement()->GetActorFeetLocation();
    const float FeetHorizontalDistance = FVector::Dist2D(FeetLocation, NavigationMoveGoal);
    const float FeetVerticalDistance = FMath::Abs(FeetLocation.Z - NavigationMoveGoal.Z);
    const float FeetToLiveTargetHorizontalDistance = PathFollowing
        ? FVector::Dist2D(FeetLocation, LiveTarget) : -1.0f;
    const float FeetToLiveTargetVerticalDistance = PathFollowing
        ? FMath::Abs(FeetLocation.Z - LiveTarget.Z) : -1.0f;
    const float PathEndToAuthoredHorizontalDistance = bNavigationPathEndAtIssueValid
        ? FVector::Dist2D(NavigationPathEndAtIssue, NavigationWaypointTarget) : -1.0f;
    const float PathEndToAuthoredVerticalDistance = bNavigationPathEndAtIssueValid
        ? FMath::Abs(NavigationPathEndAtIssue.Z - NavigationWaypointTarget.Z) : -1.0f;
    const FString PathStatus = PathFollowing ? PathFollowing->GetStatusDesc() : TEXT("missing");
    UE_LOG(LogTemp, Display,
        TEXT("HELIOS_NAV_QA type=E%02d route=%s waypoint=%d reached=%s reason=%s actual=%s feet=%s authored_target=%s nav_goal=%s arrival_feet_horizontal_cm=%.3f arrival_feet_vertical_cm=%.3f center_nav_horizontal_cm=%.3f center_nav_vertical_cm=%.3f authored_center_horizontal_cm=%.3f authored_center_vertical_cm=%.3f feet_live_target_horizontal_cm=%.3f feet_live_target_vertical_cm=%.3f path_end_authored_horizontal_cm=%.3f path_end_authored_vertical_cm=%.3f acceptance_cm=%.3f request_acceptance_cm=%.3f path_status=%s request_id=%u completion_observed=%s completion=%s path_points_at_issue=%d partial_at_issue=%s path_end_at_issue=%s live_target=%s live_destination=%s live_path_valid=%s live_segment=%u->%u"),
        NavigationTypeIndex, *Routes[NavigationRouteIndex].Id, NavigationWaypointIndex,
        bReached ? TEXT("true") : TEXT("false"), *Reason, *CurrentLocation.ToCompactString(),
        *FeetLocation.ToCompactString(), *NavigationWaypointTarget.ToCompactString(),
        *NavigationMoveGoal.ToCompactString(), FeetHorizontalDistance, FeetVerticalDistance,
        CenterToNavHorizontalDistance, CenterToNavVerticalDistance,
        AuthoredHorizontalDistance, AuthoredVerticalDistance,
        FeetToLiveTargetHorizontalDistance, FeetToLiveTargetVerticalDistance,
        PathEndToAuthoredHorizontalDistance, PathEndToAuthoredVerticalDistance,
        NavigationHorizontalAcceptanceCm,
        NavigationRequestAcceptanceCm, *PathStatus, NavigationIssuedRequestId,
        bNavigationMoveCompletionObserved ? TEXT("true") : TEXT("false"), *NavigationMoveCompletionResult,
        NavigationPathPointCountAtIssue, bNavigationPathWasPartialAtIssue ? TEXT("true") : TEXT("false"),
        *NavigationPathEndAtIssue.ToCompactString(), *LiveTarget.ToCompactString(),
        *LiveDestination.ToCompactString(), LivePath.IsValid() && LivePath->IsValid() ? TEXT("true") : TEXT("false"),
        PathFollowing ? PathFollowing->GetCurrentPathIndex() : MAX_uint32,
        PathFollowing ? PathFollowing->GetNextPathIndex() : MAX_uint32);
    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetNumberField(TEXT("index"), NavigationWaypointIndex);
    Item->SetArrayField(TEXT("target"), VectorJson(NavigationWaypointTarget)->AsArray());
    Item->SetArrayField(TEXT("navigation_goal"), VectorJson(NavigationMoveGoal)->AsArray());
    Item->SetArrayField(TEXT("actual"), VectorJson(CurrentLocation)->AsArray());
    Item->SetBoolField(TEXT("reached"), bReached);
    Item->SetStringField(TEXT("reason"), Reason);
    Item->SetNumberField(TEXT("elapsed_seconds"), FPlatformTime::Seconds() - NavigationWaypointStart);
    Item->SetStringField(TEXT("arrival_coordinate_basis"),
        TEXT("movement_component_feet_to_projected_navigation_goal"));
    Item->SetNumberField(TEXT("horizontal_distance_cm"), FeetHorizontalDistance);
    Item->SetNumberField(TEXT("vertical_distance_cm"), FeetVerticalDistance);
    Item->SetNumberField(TEXT("center_horizontal_distance_to_navigation_goal_cm"),
        CenterToNavHorizontalDistance);
    Item->SetNumberField(TEXT("center_vertical_distance_to_navigation_goal_cm"),
        CenterToNavVerticalDistance);
    Item->SetNumberField(TEXT("authored_horizontal_distance_cm"), AuthoredHorizontalDistance);
    Item->SetNumberField(TEXT("authored_vertical_distance_cm"), AuthoredVerticalDistance);
    Item->SetNumberField(TEXT("feet_horizontal_distance_to_navigation_goal_cm"), FeetHorizontalDistance);
    Item->SetNumberField(TEXT("feet_vertical_distance_to_navigation_goal_cm"), FeetVerticalDistance);
    Item->SetNumberField(TEXT("feet_horizontal_distance_to_live_target_cm"), FeetToLiveTargetHorizontalDistance);
    Item->SetNumberField(TEXT("feet_vertical_distance_to_live_target_cm"), FeetToLiveTargetVerticalDistance);
    Item->SetNumberField(TEXT("path_end_horizontal_distance_to_authored_target_cm"),
        PathEndToAuthoredHorizontalDistance);
    Item->SetNumberField(TEXT("path_end_vertical_distance_to_authored_target_cm"),
        PathEndToAuthoredVerticalDistance);
    Item->SetNumberField(TEXT("horizontal_acceptance_cm"), NavigationHorizontalAcceptanceCm);
    Item->SetNumberField(TEXT("move_request_acceptance_cm"), NavigationRequestAcceptanceCm);
    Item->SetNumberField(TEXT("engine_segment_agent_radius_allowance_cm"),
        NavigationEnemy->GetCapsuleComponent()->GetScaledCapsuleRadius() * NavigationSegmentAgentRadiusFraction);
    Item->SetNumberField(TEXT("height_tolerance_cm"),
        NavigationEnemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * NavigationHeightToleranceScale);
    Item->SetNumberField(TEXT("planned_path_length_cm"), NavigationPlannedPathLengthCm);
    Item->SetStringField(TEXT("planned_path_length_source"),
        bNavigationPathLengthFromFollowedPath ? TEXT("path_following_component") : TEXT("direct_2d_fallback"));
    Item->SetNumberField(TEXT("timeout_budget_seconds"), NavigationWaypointTimeoutSeconds);
    Item->SetNumberField(TEXT("wall_collisions_so_far"), NavigationCollisionCount);
    Item->SetNumberField(TEXT("wall_contact_samples_so_far"), NavigationCollisionCount);
    Item->SetStringField(TEXT("path_following_status"), PathStatus);
    Item->SetNumberField(TEXT("move_request_id"), NavigationIssuedRequestId);
    Item->SetBoolField(TEXT("move_completion_observed"), bNavigationMoveCompletionObserved);
    Item->SetStringField(TEXT("move_completion_result"), NavigationMoveCompletionResult);
    Item->SetNumberField(TEXT("path_points_at_issue"), NavigationPathPointCountAtIssue);
    Item->SetBoolField(TEXT("path_was_partial_at_issue"), bNavigationPathWasPartialAtIssue);
    Item->SetBoolField(TEXT("path_end_at_issue_valid"), bNavigationPathEndAtIssueValid);
    Item->SetArrayField(TEXT("path_end_at_issue"), VectorJson(NavigationPathEndAtIssue)->AsArray());
    Item->SetArrayField(TEXT("feet_location"), VectorJson(FeetLocation)->AsArray());
    Item->SetArrayField(TEXT("live_path_target"), VectorJson(LiveTarget)->AsArray());
    Item->SetArrayField(TEXT("live_path_destination"), VectorJson(LiveDestination)->AsArray());
    Item->SetBoolField(TEXT("live_path_valid"), LivePath.IsValid() && LivePath->IsValid());
    Item->SetNumberField(TEXT("live_segment_start_index"),
        PathFollowing ? PathFollowing->GetCurrentPathIndex() : -1);
    Item->SetNumberField(TEXT("live_segment_end_index"),
        PathFollowing ? PathFollowing->GetNextPathIndex() : -1);
    NavigationWaypointsJson.Add(MakeShared<FJsonValueObject>(Item));
    if (!bReached)
    {
        ++NavigationCaseFailedWaypoints;
        ++NavigationTotalFailedWaypoints;
    }
    ++NavigationWaypointIndex;
    IssueNavigationWaypoint();
}

void AHeliosEnemyExtendedQA::CompleteNavigationCase()
{
    UE_LOG(LogTemp, Display, TEXT("HELIOS_NAV_QA case_complete type=E%02d route_index=%d failed_waypoints=%d"),
        NavigationTypeIndex, NavigationRouteIndex, NavigationCaseFailedWaypoints);
    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetNumberField(TEXT("type_index"), NavigationTypeIndex);
    Item->SetStringField(TEXT("route_id"), Routes.IsValidIndex(NavigationRouteIndex)
        ? Routes[NavigationRouteIndex].Id : TEXT("spawn_failed"));
    Item->SetBoolField(TEXT("all_waypoints_reached"), NavigationCaseFailedWaypoints == 0);
    Item->SetNumberField(TEXT("failed_waypoints"), NavigationCaseFailedWaypoints);
    Item->SetNumberField(TEXT("wall_collisions"), NavigationCollisionCount);
    Item->SetNumberField(TEXT("wall_contact_samples"), NavigationCollisionCount);
    Item->SetStringField(TEXT("collision_metric"),
        TEXT("raw non-floor OnComponentHit contact samples; not deduplicated wall identities"));
    Item->SetBoolField(TEXT("first_wall_collision_recorded"), bNavigationFirstCollisionRecorded);
    Item->SetStringField(TEXT("first_wall_collision_actor"), NavigationFirstCollisionActor);
    Item->SetStringField(TEXT("first_wall_collision_other_component"),
        NavigationFirstCollisionOtherComponent);
    Item->SetStringField(TEXT("first_wall_collision_own_component"),
        NavigationFirstCollisionOwnComponent);
    Item->SetArrayField(TEXT("first_wall_collision_impact_point"),
        VectorJson(NavigationFirstCollisionImpactPoint)->AsArray());
    Item->SetArrayField(TEXT("waypoints"), NavigationWaypointsJson);
    Item->SetArrayField(TEXT("actual_path"), NavigationActualPathJson);
    NavigationCasesJson.Add(MakeShared<FJsonValueObject>(Item));

    Report->SetArrayField(TEXT("navigation_cases"), NavigationCasesJson);
    Report->SetNumberField(TEXT("failed_waypoints"), NavigationTotalFailedWaypoints);
    Report->SetNumberField(TEXT("actual_seconds"), FPlatformTime::Seconds() - ModeStartWallSeconds);
    Report->SetStringField(TEXT("status"), TEXT("running"));
    Report->SetStringField(TEXT("progress_updated_utc"), FDateTime::UtcNow().ToIso8601());
    FString ProgressOutput;
    FJsonSerializer::Serialize(Report.ToSharedRef(), TJsonWriterFactory<>::Create(&ProgressOutput));
    FFileHelper::SaveStringToFile(ProgressOutput, *ReportPath,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

    if (NavigationMoveFinishedHandle.IsValid() && NavigationController.IsValid())
    {
        if (UPathFollowingComponent* PathFollowing = NavigationController->GetPathFollowingComponent())
        {
            PathFollowing->OnRequestFinished.Remove(NavigationMoveFinishedHandle);
        }
        NavigationMoveFinishedHandle.Reset();
    }

    ++NavigationRouteIndex;
    if (NavigationRouteIndex >= Routes.Num())
    {
        NavigationRouteIndex = 0;
        ++NavigationTypeIndex;
    }
    if (NavigationTypeIndex > NavigationLastTypeIndex)
    {
        Report->SetArrayField(TEXT("navigation_cases"), NavigationCasesJson);
        Report->SetNumberField(TEXT("failed_waypoints"), NavigationTotalFailedWaypoints);
        Report->SetNumberField(TEXT("actual_seconds"), FPlatformTime::Seconds() - ModeStartWallSeconds);
        SaveAndFinish(NavigationTotalFailedWaypoints == 0 ? TEXT("completed") : TEXT("completed_with_failures"));
        return;
    }
    StartNavigationCase();
}

void AHeliosEnemyExtendedQA::OnNavigationMoveFinished(
    FAIRequestID RequestId, const FPathFollowingResult& Result)
{
    if (RequestId.GetID() == NavigationIssuedRequestId)
    {
        bNavigationMoveCompletionObserved = true;
        NavigationMoveCompletionResult = Result.ToString();
    }
}

void AHeliosEnemyExtendedQA::OnNavigationCollision(UPrimitiveComponent* HitComponent, AActor* OtherActor,
    UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
    if (OtherActor && FMath::Abs(Hit.ImpactNormal.Z) < 0.7f)
    {
        if (!bNavigationFirstCollisionRecorded)
        {
            bNavigationFirstCollisionRecorded = true;
            NavigationFirstCollisionActor = OtherActor->GetPathName();
            NavigationFirstCollisionOtherComponent = GetNameSafe(OtherComponent);
            NavigationFirstCollisionOwnComponent = GetNameSafe(HitComponent);
            NavigationFirstCollisionImpactPoint = Hit.ImpactPoint;
            UE_LOG(LogTemp, Display,
                TEXT("HELIOS_NAV_QA first_wall_contact type=E%02d route=%s actor=%s other_component=%s own_component=%s impact=%s normal=%s"),
                NavigationTypeIndex,
                Routes.IsValidIndex(NavigationRouteIndex) ? *Routes[NavigationRouteIndex].Id : TEXT("invalid"),
                *NavigationFirstCollisionActor, *NavigationFirstCollisionOtherComponent,
                *NavigationFirstCollisionOwnComponent, *Hit.ImpactPoint.ToCompactString(),
                *Hit.ImpactNormal.ToCompactString());
        }
        ++NavigationCollisionCount;
    }
}

void AHeliosEnemyExtendedQA::RestoreNavigationObserver()
{
    if (bNavigationPlayerCollisionOverrideActive)
    {
        if (Player.IsValid())
        {
            Player->SetActorEnableCollision(bNavigationPlayerCollisionWasEnabled);
        }
        bNavigationPlayerCollisionOverrideActive = false;
    }
}

void AHeliosEnemyExtendedQA::WriteReport()
{
    Report->SetStringField(TEXT("finished_utc"), FDateTime::UtcNow().ToIso8601());
    FString Output;
    FJsonSerializer::Serialize(Report.ToSharedRef(), TJsonWriterFactory<>::Create(&Output));
    FFileHelper::SaveStringToFile(Output, *ReportPath,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void AHeliosEnemyExtendedQA::SaveAndFinish(const FString& RuntimeStatus)
{
    if (bFinished) return;
    bFinished = true;
    ReleaseFire();
    RestoreNavigationObserver();
    if (Player.IsValid()) Player->SetCanBeDamaged(true);
    Report->SetStringField(TEXT("status"), RuntimeStatus);
    Report->SetNumberField(TEXT("total_wallclock_seconds"), FPlatformTime::Seconds() - StartWallSeconds);
    WriteReport();
    UE_LOG(LogTemp, Display, TEXT("HELIOS_EXTENDED_QA mode=%s status=%s report=%s"),
        *Mode, *RuntimeStatus, *ReportPath);
    if (!FParse::Param(FCommandLine::Get(), TEXT("HeliosEnemyExtendedQANoExit")))
    {
        FPlatformMisc::RequestExit(false);
    }
    Destroy();
}
