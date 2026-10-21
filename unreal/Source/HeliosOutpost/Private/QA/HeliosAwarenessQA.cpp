#include "QA/HeliosAwarenessQA.h"

#include "Player/HeliosArenaCharacter.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "Weapons/HeliosBasketballProjectile.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Boss/HeliosEnemyBoss.h"
#include "BrainComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Hearing.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Variant_Shooter/AI/ShooterAIController.h"

AHeliosAwarenessQA::AHeliosAwarenessQA()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
}

void AHeliosAwarenessQA::BeginPlay()
{
    Super::BeginPlay();
    if (AHeliosAdventureDirector* Director = AHeliosAdventureDirector::Find(GetWorld()))
    {
        Director->StartRun(TEXT("rifle"));
        Director->SetActorTickEnabled(false);
    }
    UGameplayStatics::SetGamePaused(GetWorld(), false);
}

void AHeliosAwarenessQA::Check(const FString& Name, bool bPassed, const FString& Detail)
{
    auto Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("name"), Name);
    Item->SetBoolField(TEXT("passed"), bPassed);
    Item->SetStringField(TEXT("detail"), Detail);
    Checks.Add(MakeShared<FJsonValueObject>(Item));
    if (!bPassed) ++Failures;
    UE_LOG(LogTemp, Display, TEXT("HELIOS_AWARENESS_QA %s %s %s"),
        bPassed ? TEXT("PASS") : TEXT("FAIL"), *Name, *Detail);
}

void AHeliosAwarenessQA::PlacePlayer(const FVector& Offset)
{
    Player->GetCharacterMovement()->DisableMovement();
    Player->SetActorLocation(Origin + Offset, false, nullptr, ETeleportType::TeleportPhysics);
}

void AHeliosAwarenessQA::SpawnEnemy(int32 Type)
{
    if (Enemy.IsValid()) Enemy->Destroy();
    UClass* Classes[] = { AHeliosEnemyE01::StaticClass(), AHeliosEnemyE02::StaticClass(),
        AHeliosEnemyE03::StaticClass(), AHeliosEnemyE04::StaticClass(), AHeliosEnemyBoss::StaticClass() };
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Enemy = GetWorld()->SpawnActor<AHeliosEnemyActor>(Classes[Type - 1], Origin, FRotator::ZeroRotator, Params);
    AI = Enemy.IsValid() ? Cast<AShooterAIController>(Enemy->GetController()) : nullptr;
    if (Enemy.IsValid())
    {
        Enemy->GetCharacterMovement()->DisableMovement();
        Enemy->GetCharacterMovement()->RotationRate = FRotator::ZeroRotator;
    }
}

void AHeliosAwarenessQA::MakeWall(bool bEnabled)
{
    if (Wall.IsValid()) Wall->Destroy();
    Wall.Reset();
    if (!bEnabled) return;
    Wall = GetWorld()->SpawnActor<AActor>();
    UBoxComponent* Box = NewObject<UBoxComponent>(Wall.Get());
    Wall->AddInstanceComponent(Box);
    Wall->SetRootComponent(Box);
    // A thin, tall wall tests collision at high projectile speed and blocks both sight samples.
    Box->SetBoxExtent(FVector(5, 3000, 1000));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent();
    Wall->SetActorLocation(Origin + FVector(500, 0, 0));
}

void AHeliosAwarenessQA::BeginCase()
{
    CaseElapsed = 0;
    DetectionLatency = -1;
    CaseDuration = 0.22f;
    if (CaseIndex < 28)
    {
        MakeWall(false);
        const int32 Local = CaseIndex % 7;
        const FVector Offsets[] = { FVector(1000,0,0), FRotator(0,99,0).Vector()*1000,
            FRotator(0,101,0).Vector()*1000, FVector(-350,0,0), FVector(-1000,0,0),
            FVector(3100,0,0), FVector(1000,0,0) };
        PlacePlayer(Offsets[Local]);
        if (Local == 0) SpawnEnemy(CaseIndex / 7 + 1);
        else if (AI.IsValid()) AI->ResetAwareness();
        if (Local == 6) MakeWall(true);
        bExpectedVisible = Local == 0 || Local == 1 || Local == 3;
    }
    else if (CaseIndex == 28)
    {
        MakeWall(false);
        PlacePlayer(FVector(1000,0,0));
        SpawnEnemy(1);
    }
    else if (CaseIndex == 29) { MemoryLocation = AI->GetInvestigationLocation(); MakeWall(true); }
    else if (CaseIndex == 30) { PlacePlayer(FVector(1200,500,0)); CaseDuration = 0.6f; }
    else if (CaseIndex == 31) MakeWall(false);
    else if (CaseIndex == 32) { MakeWall(true); CaseDuration = 3.3f; }
    else if (CaseIndex == 33)
    {
        MakeWall(false); AI->ResetAwareness(); PlacePlayer(FVector(-1000,0,0));
        UAISense_Hearing::ReportNoiseEvent(GetWorld(), Player->GetActorLocation(), 1.0f, Player.Get(), 0, TEXT("Player"));
    }
    else if (CaseIndex == 34)
    {
        AI->ResetAwareness(); PlacePlayer(FVector(-1000,0,0));
        AI->GetPerceptionComponent()->OnTargetPerceptionUpdated.Broadcast(Player.Get(), FAIStimulus());
        AI->GetPerceptionComponent()->OnTargetPerceptionUpdated.Broadcast(nullptr, FAIStimulus());
    }
    else if (CaseIndex == 35)
    {
        UGameplayStatics::ApplyDamage(Enemy.Get(), 1.0f, Player->GetController(), Player.Get(), UDamageType::StaticClass());
    }
    else if (CaseIndex == 36) Player->Tags.AddUnique(TEXT("Dead"));
    else if (CaseIndex == 37) { Player->Tags.Remove(TEXT("Dead")); PlacePlayer(FVector(1000,0,0)); }
    else if (CaseIndex == 38) PlacePlayer(FVector(3400,0,0));
    else if (CaseIndex == 39) PlacePlayer(FVector(3600,0,0));
    else if (CaseIndex == 41)
    {
        NavigationStart = Enemy->GetActorLocation();
        MemoryLocation = AI->GetInvestigationLocation();
        MakeWall(true);
        Wall->FindComponentByClass<UBoxComponent>()->SetBoxExtent(FVector(5,120,200));
        Wall->SetActorLocation((Enemy->GetActorLocation() + Player->GetActorLocation()) * 0.5f);
        Wall->SetActorRotation((Player->GetActorLocation() - Enemy->GetActorLocation()).Rotation());
        CaseDuration = 0.3f;
    }
    else if (CaseIndex == 42)
    {
        PlacePlayer(FVector(0,0,10000));
        CaseDuration = 1.3f;
    }
    else if (CaseIndex >= 43 && CaseIndex <= 46)
    {
        MakeWall(false);
        Origin = FVector(0,0,50000);
        const FVector Offsets[] = {FVector(4400,0,0), FRotator(0,125,0).Vector()*1500,
            FVector(-750,0,0), FVector(1000,0,0)};
        PlacePlayer(Offsets[CaseIndex-43]);
        if (CaseIndex == 46) MakeWall(true);
        SpawnEnemy(5);
    }
    else if (CaseIndex == 47) { MakeWall(false); PlacePlayer(FVector(1000,0,0)); }
    else if (CaseIndex == 48)
    {
        MakeWall(true); PlacePlayer(FVector(700,100,0)); CaseDuration = 3.5f;
    }
    else if (CaseIndex == 49) Enemy->StopShooting();
    else if (CaseIndex == 50) PlacePlayer(FVector(-8000,0,0));
    else if (CaseIndex == 51) Player->Tags.AddUnique(TEXT("Dead"));
    else if (CaseIndex == 52)
    {
        Player->Tags.Remove(TEXT("Dead")); PlacePlayer(FVector(1000,0,0)); AI->ResetAwareness();
    }
    else if (CaseIndex == 53)
    {
        UAISense_Hearing::ReportNoiseEvent(GetWorld(), Player->GetActorLocation(), 1.f, Player.Get(), 0, TEXT("Player"));
    }
    else if (CaseIndex == 54) CaseDuration = 8.3f;
    else if (CaseIndex == 55)
    {
        MakeWall(false); PlacePlayer(FVector(-1500,0,0));
        UGameplayStatics::ApplyDamage(Enemy.Get(), 1.f, Player->GetController(), Player.Get(), UDamageType::StaticClass());
    }
    else if (CaseIndex == 56)
    {
        UGameplayStatics::ApplyDamage(Enemy.Get(), 10000.f, Player->GetController(), Player.Get(), UDamageType::StaticClass());
    }
    else if (CaseIndex == 58)
    {
        NavigationStart = Enemy->GetActorLocation();
        MakeWall(true);
        Wall->FindComponentByClass<UBoxComponent>()->SetBoxExtent(FVector(5,120,200));
        Wall->SetActorLocation((Enemy->GetActorLocation()+Player->GetActorLocation())*.5f);
        Wall->SetActorRotation((Player->GetActorLocation()-Enemy->GetActorLocation()).Rotation());
        CaseDuration = .3f;
    }
    else if (CaseIndex == 59) { PlacePlayer(FVector(0,800,0)); CaseDuration = 3.f; }
    if (CaseIndex == 56) return;
    if (!AI.IsValid()) { Check(TEXT("runtime_controller_available"), false); Finish(); }
}

void AHeliosAwarenessQA::EndCase()
{
    const bool bVisible = AI.IsValid() && AI->GetCurrentTarget() == Player.Get();
    if (CaseIndex < 28)
    {
        static const TCHAR* Names[] = { TEXT("spawn_front"), TEXT("edge_99_degrees"), TEXT("outside_101_degrees"),
            TEXT("near_behind"), TEXT("far_behind"), TEXT("outside_30m"), TEXT("wall_occlusion") };
        Check(FString::Printf(TEXT("E%02d_%s"), CaseIndex / 7 + 1, Names[CaseIndex % 7]),
            bVisible == bExpectedVisible && (!bExpectedVisible || (DetectionLatency >= 0 && DetectionLatency <= 0.201f)),
            FString::Printf(TEXT("visible=%d reaction_seconds=%.3f"), bVisible, DetectionLatency));
        const float Intervals[] = {1.0f, 1.05f, 0.72f, 1.65f};
        if (CaseIndex % 7 == 0)
            Check(FString::Printf(TEXT("E%02d_attack_interval_unchanged"), CaseIndex / 7 + 1),
                FMath::IsNearlyEqual(Enemy->AttackInterval, Intervals[CaseIndex / 7]));
    }
    else if (CaseIndex == 28) Check(TEXT("memory_setup_visible"), bVisible);
    else if (CaseIndex == 29) Check(TEXT("lost_sight_enters_investigation"), !bVisible && AI->IsInvestigating());
    else if (CaseIndex == 30) Check(TEXT("hidden_motion_does_not_update_memory"),
        !bVisible && AI->IsInvestigating() && AI->GetInvestigationLocation().Equals(MemoryLocation, 1));
    else if (CaseIndex == 31) Check(TEXT("reacquire_after_cover"), bVisible && DetectionLatency <= 0.201f);
    else if (CaseIndex == 32) Check(TEXT("memory_expires_after_three_seconds"), !bVisible && !AI->IsInvestigating());
    else if (CaseIndex == 33) Check(TEXT("hearing_investigates_without_visible_target"),
        !bVisible && AI->IsInvestigating() && AI->GetInvestigationLocation().Equals(Player->GetActorLocation(), 1));
    else if (CaseIndex == 34) Check(TEXT("failed_or_null_stimulus_never_acquires_player"), !bVisible && !AI->IsInvestigating());
    else if (CaseIndex == 35) Check(TEXT("damage_from_behind_alerts"), !bVisible && AI->IsInvestigating());
    else if (CaseIndex == 36) Check(TEXT("dead_player_clears_memory_and_target"), !bVisible && !AI->IsInvestigating());
    else if (CaseIndex == 37) Check(TEXT("living_player_reacquired"), bVisible);
    else if (CaseIndex == 38) Check(TEXT("retained_target_at_34m"), bVisible);
    else if (CaseIndex == 39)
    {
        Check(TEXT("target_released_beyond_35m"), !bVisible);
        Check(TEXT("boss_persistent_tracking_enabled"), GetDefault<AHeliosEnemyBoss>()->GetAwarenessSettings()->bPersistentCombatTracking);
    }
    else if (CaseIndex == 40) Check(TEXT("map_navigation_setup_visible"), bVisible);
    else if (CaseIndex == 41) Check(TEXT("map_cover_enters_search"), !bVisible && AI->IsInvestigating());
    else if (CaseIndex == 42)
    {
        Check(TEXT("map_search_moves_toward_last_known_position"), AI->IsInvestigating()
            && AI->GetInvestigationLocation().Equals(MemoryLocation, 1)
            && FVector::Dist2D(Enemy->GetActorLocation(), NavigationStart) > 20,
            FString::Printf(TEXT("travel=%.1f target_error=%.1f"), FVector::Dist2D(Enemy->GetActorLocation(), NavigationStart),
                FVector::Dist(AI->GetInvestigationLocation(), MemoryLocation)));
    }
    else if (CaseIndex >= 43 && CaseIndex <= 45)
        Check(FString::Printf(TEXT("boss_enhanced_detection_%d"), CaseIndex), bVisible
            && DetectionLatency >= 0 && DetectionLatency <= .101f,
            FString::Printf(TEXT("reaction=%.3f"), DetectionLatency));
    else if (CaseIndex == 46) Check(TEXT("boss_no_initial_wall_detection"), !bVisible && !AI->IsCombatTracking());
    else if (CaseIndex == 47) Check(TEXT("boss_visual_engagement"), bVisible && AI->IsCombatTracking());
    else if (CaseIndex == 48)
    {
        Check(TEXT("boss_tracks_hidden_motion_beyond_minion_memory"), bVisible && AI->IsCombatTracking()
            && AI->GetInvestigationLocation().Equals(Player->GetActorLocation(), 1));
        Check(TEXT("boss_does_not_attack_through_cover"), Cast<AHeliosEnemyBoss>(Enemy.Get())->GetBossCastSerial() == 0);
    }
    else if (CaseIndex == 49) Check(TEXT("boss_stop_shooting_preserves_tracking"), AI->GetTrackingTarget() == Player.Get());
    else if (CaseIndex == 50) Check(TEXT("boss_retains_distant_rear_target"), bVisible && AI->GetTrackingTarget() == Player.Get());
    else if (CaseIndex == 51) Check(TEXT("boss_dead_target_released"), !bVisible && !AI->IsCombatTracking());
    else if (CaseIndex == 52) Check(TEXT("boss_reset_does_not_reacquire_through_wall"), !bVisible && !AI->IsCombatTracking());
    else if (CaseIndex == 53) Check(TEXT("boss_hearing_investigates_without_lock"), !bVisible && AI->IsInvestigating() && !AI->IsCombatTracking());
    else if (CaseIndex == 54) Check(TEXT("boss_hearing_memory_expires"), !bVisible && !AI->IsInvestigating());
    else if (CaseIndex == 55) Check(TEXT("boss_rear_damage_engages"), bVisible && AI->GetTrackingTarget() == Player.Get());
    else if (CaseIndex == 56)
    {
        Check(TEXT("boss_death_clears_tracking"), !AI.IsValid() || !AI->IsCombatTracking());
        BeginNavigation(true); return;
    }
    else if (CaseIndex == 57) Check(TEXT("boss_map_navigation_engaged"), AI->IsCombatTracking());
    else if (CaseIndex == 58) Check(TEXT("boss_map_cover_keeps_tracking"), AI->IsCombatTracking());
    else if (CaseIndex == 59)
    {
        Check(TEXT("boss_map_pursuit_moves_after_cover"), AI->IsCombatTracking()
            && AI->GetInvestigationLocation().Equals(Player->GetActorLocation(), 1)
            && FVector::Dist2D(Enemy->GetActorLocation(), NavigationStart) > 20,
            FString::Printf(TEXT("travel=%.1f"), FVector::Dist2D(Enemy->GetActorLocation(), NavigationStart)));
        Finish(); return;
    }
    ++CaseIndex;
    if (CaseIndex <= 39 || CaseIndex >= 41) BeginCase();
    else { ShotIndex = 0; BeginShot(); }
}

void AHeliosAwarenessQA::BeginShot()
{
    if (Ball.IsValid()) Ball->Destroy();
    MakeWall(false);
    if (ShotIndex == 0)
    {
        SpawnEnemy(4);
        AI->GetBrainComponent()->StopLogic(TEXT("isolated projectile collision checks"));
        Enemy->StopShooting();
        Player->SetCanBeDamaged(true);
    }
    Player->Heal(100);
    const float Distances[] = {500, 1000, 1400};
    const float Distance = ShotIndex < 6 ? Distances[ShotIndex / 2] : 1000;
    PlacePlayer(FVector(Distance + 100, 0, 0));
    if (ShotIndex == 6) MakeWall(true);
    ShotHealth = Player->GetHealth();
    ShotTime = 0;
    BallArrival = -1;
    FActorSpawnParameters Params;
    Params.Owner = Enemy.Get();
    Params.Instigator = Enemy.Get();
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Ball = GetWorld()->SpawnActor<AHeliosBasketballProjectile>(Origin + FVector(100,0,0), FRotator::ZeroRotator, Params);
    if (!Ball.IsValid()) { Check(TEXT("ball_spawned"), false); Finish(); return; }
    Ball->FlightSpeedMultiplier = ShotIndex < 6 && ShotIndex % 2 == 0 ? 1.0f : 2.0f;
    Ball->LaunchAt(Player.Get(), 22);
    ExpectedFlight = FMath::Clamp(Distance / 900.0f, 0.45f, 1.35f) / Ball->FlightSpeedMultiplier;
    Check(FString::Printf(TEXT("ball_%d_planned_flight_time"), ShotIndex),
        FMath::IsNearlyEqual(Ball->GetPlannedFlightTime(), ExpectedFlight, 0.0001f),
        FString::Printf(TEXT("distance=%.0f seconds=%.4f multiplier=%.1f"), Distance, Ball->GetPlannedFlightTime(), Ball->FlightSpeedMultiplier));
    if (ShotIndex == 8)
        UGameplayStatics::ApplyDamage(Enemy.Get(), 10000, Player->GetController(), Player.Get(), UDamageType::StaticClass());
}

void AHeliosAwarenessQA::EndShot()
{
    const float Damage = ShotHealth - Player->GetHealth();
    if (ShotIndex < 6)
    {
        Check(FString::Printf(TEXT("ball_%d_hits_once"), ShotIndex), FMath::IsNearlyEqual(Damage, 22),
            FString::Printf(TEXT("damage=%.1f observed_arrival=%.3f"), Damage, BallArrival));
        if (ShotIndex % 2 == 0) BaselineArrival = BallArrival;
        else Check(FString::Printf(TEXT("ball_%d_arrives_in_half_the_time"), ShotIndex),
            BaselineArrival > 0 && BallArrival > 0 && FMath::Abs(BallArrival - BaselineArrival * 0.5f) <= 0.075f,
            FString::Printf(TEXT("baseline=%.3f boosted=%.3f"), BaselineArrival, BallArrival));
    }
    else if (ShotIndex == 6) Check(TEXT("fast_ball_blocked_by_thin_wall"), !Ball.IsValid() && Damage == 0);
    else if (ShotIndex == 7) Check(TEXT("fast_ball_can_be_dodged"), Damage == 0);
    else if (ShotIndex == 8) Check(TEXT("owner_death_cancels_fast_ball"), !Ball.IsValid() && Damage == 0);
    ++ShotIndex;
    if (ShotIndex < 9) BeginShot(); else BeginNavigation();
}

void AHeliosAwarenessQA::BeginNavigation(bool bBoss)
{
    if (Ball.IsValid()) Ball->Destroy();
    if (Enemy.IsValid()) Enemy->Destroy();
    Player->SetCanBeDamaged(false);
    UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    FNavLocation PlayerGround;
    if (!Nav || !Nav->ProjectPointToNavigation(MapPlayerStart, PlayerGround, FVector(250,250,500)))
    {
        Check(TEXT("map_navigation_available"), false); Finish(); return;
    }
    Player->SetActorLocation(PlayerGround.Location + FVector(0,0,Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2));
    for (const FVector Offset : {FVector(800,0,0), FVector(0,800,0), FVector(-800,0,0), FVector(0,-800,0)})
    {
        FNavLocation EnemyGround;
        if (!Nav->ProjectPointToNavigation(PlayerGround.Location + Offset, EnemyGround, FVector(200,200,300))) continue;
        UNavigationPath* Path = Nav->FindPathToLocationSynchronously(GetWorld(), EnemyGround.Location, PlayerGround.Location);
        if (!Path || !Path->IsValid() || Path->IsPartial() || Path->GetPathLength() < 400) continue;
        Origin = EnemyGround.Location + FVector(0,0,bBoss ? 127 : 82);
        SpawnEnemy(bBoss ? 5 : 1);
        if (!AI.IsValid()) continue;
        const FRotator Facing = (Player->GetActorLocation() - Enemy->GetActorLocation()).Rotation();
        Enemy->SetActorRotation(Facing); AI->SetControlRotation(Facing);
        if (!AI->CanSeeAwarenessTarget(Player.Get())) continue;
        Enemy->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        Enemy->GetCharacterMovement()->RotationRate = FRotator(0,540,0);
        ShotIndex = -1;
        CaseIndex = bBoss ? 57 : 40;
        CaseElapsed = 0;
        DetectionLatency = -1;
        CaseDuration = bBoss ? 3.f : .3f;
        return;
    }
    Check(TEXT("map_navigation_visible_route_found"), false);
    Finish();
}

void AHeliosAwarenessQA::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bFinished) return;
    Elapsed += DeltaSeconds;
    if (!bStarted)
    {
        UGameplayStatics::SetGamePaused(GetWorld(), false);
        if (Elapsed < 4) return;
        Player = Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
        if (!Player.IsValid()) { Check(TEXT("player_available"), false); Finish(); return; }
        MapPlayerStart = Player->GetActorLocation();
        Player->SetCanBeDamaged(false);
        bStarted = true;
        BeginCase();
        return;
    }
    if (Elapsed > 130) { Check(TEXT("suite_timeout"), false); Finish(); return; }
    if ((CaseIndex < 40 || (CaseIndex >= 43 && CaseIndex <= 56)) && Enemy.IsValid() && !Enemy->IsEnemyDead())
    {
        // Lock only test placement, so rotating StateTree tasks cannot change angle cases.
        Enemy->SetActorRotation(FRotator::ZeroRotator);
        Enemy->GetCharacterMovement()->bUseControllerDesiredRotation = false;
        Enemy->GetCharacterMovement()->bOrientRotationToMovement = false;
    }
    if (ShotIndex < 0)
    {
        CaseElapsed += DeltaSeconds;
        if (AI.IsValid() && AI->GetCurrentTarget() == Player.Get() && DetectionLatency < 0) DetectionLatency = CaseElapsed;
        if (CaseElapsed >= CaseDuration) EndCase();
    }
    else
    {
        ShotTime += DeltaSeconds;
        if (Player->GetHealth() < ShotHealth && BallArrival < 0) BallArrival = ShotTime;
        if (ShotIndex == 7 && ShotTime >= 0.1f) PlacePlayer(FVector(1100,500,0));
        if (ShotTime >= ExpectedFlight + 0.3f) EndShot();
    }
}

void AHeliosAwarenessQA::Finish()
{
    if (bFinished) return;
    bFinished = true;
    if (Ball.IsValid()) Ball->Destroy();
    if (Enemy.IsValid()) Enemy->Destroy();
    MakeWall(false);
    if (Player.IsValid()) { Player->Tags.Remove(TEXT("Dead")); Player->SetCanBeDamaged(true); }
    auto Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("map"), GetWorld()->GetMapName());
    Report->SetStringField(TEXT("status"), Failures == 0 ? TEXT("passed") : TEXT("failed"));
    Report->SetNumberField(TEXT("failures"), Failures);
    Report->SetArrayField(TEXT("checks"), Checks);
    FString Output;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Output));
    FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../qa/awareness_speed/runtime.json")));
    FParse::Value(FCommandLine::Get(), TEXT("HeliosAwarenessReport="), Path);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    FFileHelper::SaveStringToFile(Output, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogTemp, Display, TEXT("HELIOS_AWARENESS_QA finished failures=%d report=%s"), Failures, *Path);
    FPlatformMisc::RequestExit(false);
}
