#include "QA/HeliosEnemyQARunner.h"

#include "AIController.h"
#include "BrainComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Player/HeliosArenaCharacter.h"
#include "Weapons/HeliosBasketballProjectile.h"
#include "Control/HeliosControlSubsystem.h"
#include "Control/HeliosControlTypes.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Enemies/HeliosEnemyTestDirector.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Perception/AIPerceptionComponent.h"
#include "Serialization/JsonSerializer.h"
#include "ShooterProjectile.h"
#include "ShooterAIController.h"
#include "ShooterWeapon.h"
#include "UnrealClient.h"
#include "UObject/UnrealType.h"

AHeliosEnemyQARunner::AHeliosEnemyQARunner()
{
    PrimaryActorTick.bCanEverTick = true;
}

void AHeliosEnemyQARunner::BeginPlay()
{
    Super::BeginPlay();
    RootDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("..")));
    Mode = TEXT("all");
    FParse::Value(FCommandLine::Get(), TEXT("HeliosEnemyQA="), Mode);
    Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("mode"), Mode);
    Report->SetStringField(TEXT("map"), TEXT("/Game/Helios/Maps/HeliosEnemyTestbed"));
    Report->SetStringField(TEXT("status"), TEXT("running"));
    Report->SetStringField(TEXT("started_utc"), FDateTime::UtcNow().ToIso8601());
    Report->SetStringField(TEXT("evidence_policy"),
        TEXT("No direct damage injection: combat requires authored HeliosAttack notify, committed attack, real player input, and observed health/death state."));
    IFileManager::Get().MakeDirectory(*FPaths::Combine(RootDir, TEXT("qa/enemies/video_frames")), true);
}

void AHeliosEnemyQARunner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ReleaseFire();
    RestorePointBlankAim();
    DestroyBlockingWall();
    Super::EndPlay(EndPlayReason);
}

void AHeliosEnemyQARunner::Check(const FString& Name, bool bPassed, const FString& Detail)
{
    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("name"), Name);
    Item->SetBoolField(TEXT("passed"), bPassed);
    Item->SetStringField(TEXT("detail"), Detail);
    Checks.Add(MakeShared<FJsonValueObject>(Item));
    if (!bPassed)
    {
        ++FailureCount;
    }
    UE_LOG(LogTemp, Display, TEXT("HELIOS_ENEMY_QA check=%s result=%s %s"),
        *Name, bPassed ? TEXT("PASS") : TEXT("FAIL"), *Detail);
}

void AHeliosEnemyQARunner::Pending(const FString& Name, const FString& Detail)
{
    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("name"), Name);
    Item->SetStringField(TEXT("status"), TEXT("pending"));
    Item->SetStringField(TEXT("detail"), Detail);
    PendingChecks.Add(MakeShared<FJsonValueObject>(Item));
}

void AHeliosEnemyQARunner::SetStage(int32 NewStage)
{
    Stage = NewStage;
    StageTimer = 0.0f;
    bStageActionDone = false;
}

bool AHeliosEnemyQARunner::ResolveActors()
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

bool AHeliosEnemyQARunner::ResolveSingleEnemy()
{
    Enemy.Reset();
    if (!Director.IsValid())
    {
        return false;
    }
    const TArray<AHeliosEnemyActor*> Enemies = Director->GetSpawnedEnemies();
    if (Enemies.Num() == 1 && IsValid(Enemies[0]))
    {
        Enemy = Enemies[0];
        return true;
    }
    return false;
}

FVector AHeliosEnemyQARunner::GroundedLocation(const FVector& Desired, float HalfHeight, AActor* Ignore) const
{
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(HeliosEnemyQAGround), false, Ignore);
    const FVector Start(Desired.X, Desired.Y, Desired.Z + 2000.0f);
    const FVector End(Desired.X, Desired.Y, Desired.Z - 2000.0f);
    if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
    {
        return FVector(Desired.X, Desired.Y, Hit.ImpactPoint.Z + HalfHeight + 2.0f);
    }
    return Desired;
}

bool AHeliosEnemyQARunner::PlaceCombatants()
{
    if (!Player.IsValid() || !Enemy.IsValid())
    {
        return false;
    }
    const float PlayerHalf = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const float EnemyHalf = Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FVector PlayerGrounded = GroundedLocation(TestPlayerLocation, PlayerHalf, Player.Get());
    const FVector EnemyGrounded = GroundedLocation(TestEnemyLocation, EnemyHalf, Enemy.Get());
    Player->SetActorLocationAndRotation(PlayerGrounded, FRotator(0.0f, 0.0f, 0.0f), false, nullptr,
        ETeleportType::TeleportPhysics);
    Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Player->GetCharacterMovement()->StopMovementImmediately();
    Enemy->SetActorLocationAndRotation(EnemyGrounded, FRotator(0.0f, 180.0f, 0.0f), false, nullptr,
        ETeleportType::TeleportPhysics);
    Enemy->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Enemy->GetCharacterMovement()->StopMovementImmediately();
	if (AAIController* EnemyAI = Cast<AAIController>(Enemy->GetController()))
	{
		EnemyAI->StopMovement();
		EnemyAI->SetControlRotation((PlayerGrounded - EnemyGrounded).Rotation());
		if (UAIPerceptionComponent* Perception = EnemyAI->GetPerceptionComponent())
		{
			Perception->RequestStimuliListenerUpdate();
		}
	}
    AimAtEnemy();
    return true;
}

void AHeliosEnemyQARunner::AimAtEnemy()
{
    if (PlayerController.IsValid() && Player.IsValid() && Enemy.IsValid())
    {
        const FVector Eye = Player->FirstPersonCamera->GetComponentLocation();
        PlayerController->SetControlRotation((Enemy->GetActorLocation() - Eye).Rotation());
    }
}

void AHeliosEnemyQARunner::SendKey(const FKey& Key, EInputEvent Event, float Amount)
{
    if (PlayerController.IsValid())
    {
        PlayerController->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, Amount));
    }
}

void AHeliosEnemyQARunner::ReleaseFire()
{
    if (bFireHeld)
    {
        SendKey(EKeys::LeftMouseButton, IE_Released, 0.0f);
        bFireHeld = false;
    }
}

void AHeliosEnemyQARunner::RestorePointBlankAim()
{
    if (PointBlankWeapon.IsValid())
        if (FFloatProperty* Variance = FindFProperty<FFloatProperty>(AShooterWeapon::StaticClass(), TEXT("AimVariance")))
            Variance->SetPropertyValue_InContainer(PointBlankWeapon.Get(), SavedPointBlankVariance);
    PointBlankWeapon.Reset();
}

bool AHeliosEnemyQARunner::SpawnBlockingWall()
{
    DestroyBlockingWall();
    if (!Player.IsValid() || !Enemy.IsValid())
    {
        return false;
    }
    AActor* Wall = GetWorld()->SpawnActor<AActor>();
    if (!Wall)
    {
        return false;
    }
    UBoxComponent* Box = NewObject<UBoxComponent>(Wall, TEXT("HeliosQACombatWall"));
    Wall->AddInstanceComponent(Box);
    Wall->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(45.0f, 220.0f, 180.0f));
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent();
    const FVector Midpoint = (Player->GetActorLocation() + Enemy->GetActorLocation()) * 0.5f;
    Wall->SetActorLocation(FVector(Midpoint.X, Midpoint.Y, Midpoint.Z + 40.0f));
    Wall->Tags.Add(FName(TEXT("HeliosQATemporary")));
    BlockingWall = Wall;
    return true;
}

void AHeliosEnemyQARunner::DestroyBlockingWall()
{
    if (BlockingWall.IsValid())
    {
        BlockingWall->Destroy();
    }
    BlockingWall.Reset();
}

bool AHeliosEnemyQARunner::IsWallBlockingVisibility() const
{
    if (!BlockingWall.IsValid() || !Enemy.IsValid() || !Player.IsValid())
    {
        return false;
    }
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(HeliosEnemyQAWall), true, Enemy.Get());
    const FVector Start = Enemy->GetActorLocation() + FVector(0.0f, 0.0f, Enemy->BaseEyeHeight);
    const FVector End = Player->GetActorLocation();
    return GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params)
        && Hit.GetActor() == BlockingWall.Get();
}

void AHeliosEnemyQARunner::RequestTypeScreenshot()
{
    const FString Directory = FPaths::Combine(RootDir, TEXT("qa/enemies"),
        FString::Printf(TEXT("E%02d"), TypeIndex));
    IFileManager::Get().MakeDirectory(*Directory, true);
    PendingScreenshotPath = FPaths::Combine(Directory, TEXT("runtime_combat.png"));
    FScreenshotRequest::RequestScreenshot(PendingScreenshotPath, false, false);
    bScreenshotOutstanding = true;
}

void AHeliosEnemyQARunner::StartMode()
{
    if (!ResolveActors())
    {
        Check(TEXT("typed_runtime_actors_available"), false,
            TEXT("AHeliosEnemyTestDirector, AHeliosArenaCharacter, or PlayerController missing"));
        Finish();
        return;
    }
    Check(TEXT("typed_runtime_actors_available"), true);
    if (Mode == TEXT("capture"))
    {
        SetStage(300);
    }
    else if (Mode == TEXT("performance"))
    {
        Director->SpawnCount = 4;
        Director->SelectEnemyMode(5);
        SetStage(200);
    }
    else if (Mode == TEXT("reset"))
    {
        BeginResetChecks();
    }
    else
    {
        TypeIndex = Mode == TEXT("type4") ? 4 : (Mode == TEXT("type2") ? 2 : 1);
        SetStage(1);
    }
}

void AHeliosEnemyQARunner::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bFinished)
    {
        return;
    }
    Timer += DeltaSeconds;
    StageTimer += DeltaSeconds;
    FrameTimes.Add(DeltaSeconds);
    if (Stage == 0)
    {
        if (Timer >= 3.0f)
        {
            StartMode();
        }
        return;
    }
    if (Stage < 100)
    {
        TickCombat(DeltaSeconds);
    }
    else if (Stage < 200)
    {
        TickReset(DeltaSeconds);
    }
    else if (Stage < 300)
    {
        TickPerformance(DeltaSeconds);
    }
    else
    {
        TickCapture(DeltaSeconds);
    }
}

void AHeliosEnemyQARunner::TickCombat(float DeltaSeconds)
{
    if (Stage >= 2 && Stage <= 8)
    {
        AimAtEnemy();
    }

    if (Stage == 1)
    {
        Director->SpawnCount = 1;
        Director->SelectEnemyMode(TypeIndex);
        Enemy.Reset();
        SetStage(2);
        return;
    }
    if (Stage == 2)
    {
        if (ResolveSingleEnemy() && PlaceCombatants())
        {
            Check(FString::Printf(TEXT("combat_type_%02d_spawned"), TypeIndex),
                CountEnemies() == 1,
                FString::Printf(TEXT("enemy_count=%d controller=%s"), CountEnemies(),
                    Enemy->GetController() ? TEXT("present") : TEXT("missing")));
            SetStage(3);
        }
        else if (StageTimer > 3.0f)
        {
            Check(FString::Printf(TEXT("combat_type_%02d_spawned"), TypeIndex), false,
                TEXT("single typed enemy unavailable after 3 seconds"));
            SetStage(8);
            RequestTypeScreenshot();
        }
        return;
    }
    if (Stage == 3)
    {
        if (StageTimer >= 3.25f)
        {
            StartingPlayerHealth = Player->GetHealth();
            StartingNotifyCount = Enemy->GetAttackNotifyCount();
            StartingCommittedCount = Enemy->GetAttacksCommitted();
            SetStage(4);
        }
        return;
    }
    if (Stage == 4)
    {
        const bool bNotify = Enemy->GetAttackNotifyCount() > StartingNotifyCount;
        const bool bCommitted = Enemy->GetAttacksCommitted() > StartingCommittedCount;
        const bool bDamaged = Player->GetHealth() < StartingPlayerHealth;
        if (bNotify && bCommitted && bDamaged)
        {
            const FString Detail = FString::Printf(TEXT("hp=%.1f->%.1f notify=%d->%d committed=%d->%d"),
                StartingPlayerHealth, Player->GetHealth(), StartingNotifyCount, Enemy->GetAttackNotifyCount(),
                StartingCommittedCount, Enemy->GetAttacksCommitted());
            Check(FString::Printf(TEXT("combat_type_%02d_attack_notify"), TypeIndex), true, Detail);
            Check(FString::Printf(TEXT("combat_type_%02d_attack_committed"), TypeIndex), true, Detail);
            Check(FString::Printf(TEXT("combat_type_%02d_player_damage"), TypeIndex), true, Detail);
            SetStage(5);
        }
        else if (StageTimer >= 15.0f)
        {
            const FString Detail = FString::Printf(TEXT("hp=%.1f->%.1f notify=%d->%d committed=%d->%d attack_started=%d"),
                StartingPlayerHealth, Player->GetHealth(), StartingNotifyCount, Enemy->GetAttackNotifyCount(),
                StartingCommittedCount, Enemy->GetAttacksCommitted(), Enemy->GetAttackCount());
            Check(FString::Printf(TEXT("combat_type_%02d_attack_notify"), TypeIndex), bNotify, Detail);
            Check(FString::Printf(TEXT("combat_type_%02d_attack_committed"), TypeIndex), bCommitted, Detail);
            Check(FString::Printf(TEXT("combat_type_%02d_player_damage"), TypeIndex), bDamaged, Detail);
            SetStage(5);
        }
        return;
    }
    if (Stage == 5)
    {
        if (!bStageActionDone)
        {
            bStageActionDone = true;
            Enemy->GetCharacterMovement()->DisableMovement();
            WallPlayerHealth = Player->GetHealth();
            WallCommittedCount = Enemy->GetAttacksCommitted();
            Check(FString::Printf(TEXT("combat_type_%02d_wall_created"), TypeIndex), SpawnBlockingWall());
        }
        if (StageTimer >= 2.0f)
        {
            const bool bVisibilityBlocked = IsWallBlockingVisibility();
            const bool bNoDamage = FMath::IsNearlyEqual(Player->GetHealth(), WallPlayerHealth);
            const bool bNoCommit = Enemy->GetAttacksCommitted() == WallCommittedCount;
            Check(FString::Printf(TEXT("combat_type_%02d_wall_visibility"), TypeIndex), bVisibilityBlocked);
            Check(FString::Printf(TEXT("combat_type_%02d_wall_blocks_damage"), TypeIndex), bNoDamage && bNoCommit,
                FString::Printf(TEXT("hp=%.1f->%.1f committed=%d->%d"), WallPlayerHealth, Player->GetHealth(),
                    WallCommittedCount, Enemy->GetAttacksCommitted()));

            if (TypeIndex == 2)
            {
                // Keep the real wall and combatants from the attack check in place.
                // The next stage fires exactly one real input-driven projectile into it.
                SetStage(50);
                return;
            }

            DestroyBlockingWall();
            Player->ResetForTest();
            const float PlayerHalf = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            Player->SetActorLocation(GroundedLocation(TestPlayerLocation, PlayerHalf, Player.Get()), false, nullptr,
                ETeleportType::TeleportPhysics);
            AimAtEnemy();
            EnemyControllerAtDeath = Cast<AAIController>(Enemy->GetController());
            SendKey(EKeys::LeftMouseButton, IE_Pressed, 1.0f);
            bFireHeld = true;
            SetStage(6);
        }
        return;
    }
    if (Stage == 50)
    {
        if (!bStageActionDone)
        {
            bStageActionDone = true;
            AimAtEnemy();
            E02WallEnemyHealthBeforeShot = Enemy->GetHealth();
            E02WallPlayerHealthBeforeShot = Player->GetHealth();
            E02WallAmmoBeforeShot = Player->GetAmmo();
            SendKey(EKeys::LeftMouseButton, IE_Pressed, 1.0f);
            bFireHeld = true;
        }
        if (bFireHeld && StageTimer >= 0.02f)
        {
            ReleaseFire();
        }
        if (StageTimer >= 0.45f)
        {
            const int32 AmmoAfter = Player->GetAmmo();
            const float EnemyHealthAfter = Enemy->GetHealth();
            const float PlayerHealthAfter = Player->GetHealth();
            const bool bExactlyOneShot = E02WallAmmoBeforeShot == 30 && AmmoAfter == 29;
            const bool bEnemyProtectedByWall = FMath::IsNearlyEqual(EnemyHealthAfter, E02WallEnemyHealthBeforeShot);
            const bool bNoSelfDamage = FMath::IsNearlyEqual(PlayerHealthAfter, E02WallPlayerHealthBeforeShot);
            Check(TEXT("combat_type_02_wall_single_shot_ammo_30_to_29"), bExactlyOneShot,
                FString::Printf(TEXT("ammo=%d->%d"), E02WallAmmoBeforeShot, AmmoAfter));
            Check(TEXT("combat_type_02_player_shot_blocked_by_wall"),
                bEnemyProtectedByWall && IsWallBlockingVisibility(),
                FString::Printf(TEXT("enemy_hp=%.1f->%.1f wall_visibility=%s"),
                    E02WallEnemyHealthBeforeShot, EnemyHealthAfter,
                    IsWallBlockingVisibility() ? TEXT("blocked") : TEXT("clear")));
            Check(TEXT("combat_type_02_wall_shot_no_self_damage"), bNoSelfDamage,
                FString::Printf(TEXT("player_hp=%.1f->%.1f"), E02WallPlayerHealthBeforeShot, PlayerHealthAfter));

            DestroyBlockingWall();
            Player->ResetForTest();
            const float PlayerHalf = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            const float PointBlankDistance = Player->GetCapsuleComponent()->GetScaledCapsuleRadius()
                + Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius() + 8.0f;
            FVector AwayFromEnemy = Player->GetActorLocation() - Enemy->GetActorLocation();
            AwayFromEnemy.Z = 0.0f;
            if (!AwayFromEnemy.Normalize())
            {
                AwayFromEnemy = FVector(-1.0f, 0.0f, 0.0f);
            }
            const FVector DesiredCloseLocation = Enemy->GetActorLocation() + AwayFromEnemy * PointBlankDistance;
            Player->SetActorLocation(GroundedLocation(DesiredCloseLocation, PlayerHalf, Player.Get()), false, nullptr,
                ETeleportType::TeleportPhysics);
            AimAtEnemy();
            SetStage(51);
        }
        return;
    }
    if (Stage == 51)
    {
        if (!bStageActionDone)
        {
            bStageActionDone = true;
            // This checks collision and exactly-once damage, not random accuracy.
            // At this distance the template's world-space aim jitter can point a
            // bullet away from the capsule. Restore authored spread after the check.
            for (TActorIterator<AShooterWeapon> It(GetWorld()); It; ++It)
            {
                if (It->GetOwner() != Player.Get()) continue;
                if (FFloatProperty* Variance = FindFProperty<FFloatProperty>(AShooterWeapon::StaticClass(), TEXT("AimVariance")))
                {
                    PointBlankWeapon = *It;
                    SavedPointBlankVariance = Variance->GetPropertyValue_InContainer(*It);
                    Variance->SetPropertyValue_InContainer(*It, 0.0f);
                }
                break;
            }
            Check(TEXT("combat_type_02_point_blank_aim_controlled"), PointBlankWeapon.IsValid());
            AimAtEnemy();
            E02CloseEnemyHealthBeforeShot = Enemy->GetHealth();
            E02CloseAmmoBeforeShot = Player->GetAmmo();
            SendKey(EKeys::LeftMouseButton, IE_Pressed, 1.0f);
            bFireHeld = true;
        }
        if (bFireHeld && StageTimer >= 0.02f)
        {
            ReleaseFire();
        }
        if (StageTimer >= 0.45f)
        {
            const int32 AmmoAfter = Player->GetAmmo();
            const float EnemyHealthAfter = Enemy->GetHealth();
            const float EnemyDamage = E02CloseEnemyHealthBeforeShot - EnemyHealthAfter;
            const bool bExactlyOneShot = E02CloseAmmoBeforeShot == 30 && AmmoAfter == 29;
            const bool bExactlyOneHit = FMath::IsNearlyEqual(EnemyDamage, 25.0f, 0.1f);
            Check(TEXT("combat_type_02_point_blank_single_shot_once"), bExactlyOneShot && bExactlyOneHit,
                FString::Printf(TEXT("ammo=%d->%d enemy_hp=%.1f->%.1f damage=%.1f"),
                    E02CloseAmmoBeforeShot, AmmoAfter, E02CloseEnemyHealthBeforeShot, EnemyHealthAfter, EnemyDamage));
            RestorePointBlankAim();

            Player->ResetForTest();
            const float PlayerHalf = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            Player->SetActorLocation(GroundedLocation(TestPlayerLocation, PlayerHalf, Player.Get()), false, nullptr,
                ETeleportType::TeleportPhysics);
            AimAtEnemy();
            EnemyControllerAtDeath = Cast<AAIController>(Enemy->GetController());
            E02MinimumEnemyHealth = Enemy->GetHealth();
            E02MinimumPlayerAmmo = Player->GetAmmo();
            SendKey(EKeys::LeftMouseButton, IE_Pressed, 1.0f);
            bFireHeld = true;
            SetStage(6);
        }
        return;
    }
    if (Stage == 6)
    {
        if (TypeIndex == 2)
        {
            E02MinimumEnemyHealth = FMath::Min(E02MinimumEnemyHealth, Enemy->GetHealth());
            E02MinimumPlayerAmmo = FMath::Min(E02MinimumPlayerAmmo, Player->GetAmmo());
        }
        if (Enemy->IsEnemyDead())
        {
            ReleaseFire();
            DeathCommittedCount = Enemy->GetAttacksCommitted();
            Check(FString::Printf(TEXT("combat_type_%02d_player_defeats_enemy"), TypeIndex), true,
                TypeIndex == 2
                    ? FString::Printf(TEXT("enemy_hp=%.1f observed_min_hp=%.1f observed_min_ammo=%d"),
                        Enemy->GetHealth(), E02MinimumEnemyHealth, E02MinimumPlayerAmmo)
                    : FString::Printf(TEXT("enemy_hp=%.1f"), Enemy->GetHealth()));
            SetStage(7);
        }
        else if (StageTimer >= 15.0f)
        {
            ReleaseFire();
            Check(FString::Printf(TEXT("combat_type_%02d_player_defeats_enemy"), TypeIndex), false,
                TypeIndex == 2
                    ? FString::Printf(TEXT("timeout enemy_hp=%.1f ammo=%d observed_min_hp=%.1f observed_min_ammo=%d"),
                        Enemy->GetHealth(), Player->GetAmmo(), E02MinimumEnemyHealth, E02MinimumPlayerAmmo)
                    : FString::Printf(TEXT("timeout enemy_hp=%.1f ammo=%d"), Enemy->GetHealth(), Player->GetAmmo()));
            DeathCommittedCount = Enemy->GetAttacksCommitted();
            SetStage(7);
        }
        return;
    }
    if (Stage == 7 && StageTimer >= 2.0f)
    {
        const bool bNoFurtherAttack = Enemy.IsValid() && Enemy->GetAttacksCommitted() == DeathCommittedCount;
        const bool bControllerGone = !EnemyControllerAtDeath.IsValid() && (!Enemy.IsValid() || !Enemy->GetController());
        const bool bCorpseNonBlocking = !Enemy.IsValid() ||
            Enemy->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision;
        Check(FString::Printf(TEXT("combat_type_%02d_death_stops_attack"), TypeIndex), bNoFurtherAttack,
            FString::Printf(TEXT("committed=%d->%d"), DeathCommittedCount,
                Enemy.IsValid() ? Enemy->GetAttacksCommitted() : DeathCommittedCount));
        Check(FString::Printf(TEXT("combat_type_%02d_controller_destroyed"), TypeIndex), bControllerGone);
        Check(FString::Printf(TEXT("combat_type_%02d_corpse_nonblocking"), TypeIndex), bCorpseNonBlocking);
        RequestTypeScreenshot();
        SetStage(8);
        return;
    }
    if (Stage == 8)
    {
        if (bScreenshotOutstanding && !FScreenshotRequest::IsScreenshotRequested())
        {
            bScreenshotOutstanding = false;
            Check(FString::Printf(TEXT("combat_type_%02d_screenshot"), TypeIndex),
                IFileManager::Get().FileExists(*PendingScreenshotPath), PendingScreenshotPath);
            if (Mode == TEXT("type2") || Mode == TEXT("type4"))
            {
                Finish();
                return;
            }
            ++TypeIndex;
            if (TypeIndex <= 4)
            {
                SetStage(1);
            }
            else
            {
                BeginAmmoChecks();
            }
        }
        else if (StageTimer > 5.0f)
        {
            Check(FString::Printf(TEXT("combat_type_%02d_screenshot"), TypeIndex), false,
                TEXT("screenshot completion timeout"));
            bScreenshotOutstanding = false;
            if (Mode == TEXT("type2") || Mode == TEXT("type4"))
            {
                Finish();
                return;
            }
            ++TypeIndex;
            if (TypeIndex <= 4) SetStage(1); else BeginAmmoChecks();
        }
        return;
    }

    if (Stage == 20)
    {
        if (ResolveSingleEnemy())
        {
            if (AAIController* AI = Cast<AAIController>(Enemy->GetController()))
            {
                AI->StopMovement();
                if (AI->GetBrainComponent()) AI->GetBrainComponent()->PauseLogic(TEXT("Helios ammo QA"));
            }
            Enemy->StopShooting();
            Player->ResetForTest();
            PlayerController->SetControlRotation(FRotator(65.0f, 90.0f, 0.0f));
            SendKey(EKeys::LeftMouseButton, IE_Pressed, 1.0f);
            bFireHeld = true;
            SetStage(21);
        }
        else if (StageTimer > 3.0f)
        {
            Check(TEXT("ammo_test_setup"), false, TEXT("enemy/director reset did not stabilize"));
            Finish();
        }
        return;
    }
    if (Stage == 21)
    {
        if (Player->GetAmmo() == 0)
        {
            ReleaseFire();
            Check(TEXT("ammo_30_shots_depletes_magazine"), true, TEXT("ammo reached zero through real held fire"));
            SetStage(22);
        }
        else if (StageTimer > 8.0f)
        {
            ReleaseFire();
            Check(TEXT("ammo_30_shots_depletes_magazine"), false,
                FString::Printf(TEXT("timeout ammo=%d"), Player->GetAmmo()));
            SetStage(22);
        }
        return;
    }
    if (Stage == 22 && StageTimer >= 1.0f)
    {
        Check(TEXT("ammo_does_not_auto_refill"), Player->GetAmmo() == 0,
            FString::Printf(TEXT("ammo_after_one_second=%d"), Player->GetAmmo()));
        SendKey(EKeys::R, IE_Pressed, 1.0f);
        SendKey(EKeys::R, IE_Released, 0.0f);
        bSawReloadDelay = false; // Queued input is processed on the next player tick.
        SetStage(23);
        return;
    }
    if (Stage == 23)
    {
        if (StageTimer >= 0.35f && StageTimer < 0.6f)
        {
            bSawReloadDelay = bSawReloadDelay || (Player->IsReloading() && Player->GetAmmo() == 0);
        }
        if (StageTimer >= 1.5f)
        {
            Check(TEXT("reload_has_real_delay"), bSawReloadDelay);
            Check(TEXT("reload_restores_30"), !Player->IsReloading() && Player->GetAmmo() == 30,
                FString::Printf(TEXT("ammo=%d reloading=%s"), Player->GetAmmo(),
                    Player->IsReloading() ? TEXT("true") : TEXT("false")));
            SetStage(24);
        }
        return;
    }
    if (Stage == 24 && !bStageActionDone)
    {
        bStageActionDone = true;
        UHeliosControlSubsystem* Control = GetWorld()->GetSubsystem<UHeliosControlSubsystem>();
        const FHeliosControlResult MoveResult = Control
            ? Control->Move(TEXT("player_01"), FVector2D(0.0f, 1.0f), 2.0f, false)
            : FHeliosControlResult();
        FHeliosActorState Before;
        bWasAutomatedBeforeFire = Control && MoveResult.bSuccess &&
            Control->GetState(TEXT("player_01"), Before).bSuccess && Before.bAutomationActive;
        SendKey(EKeys::LeftMouseButton, IE_Pressed, 1.0f);
        bFireHeld = true;
        SetStage(25);
        return;
    }
    if (Stage == 25 && StageTimer >= 0.1f)
    {
        UHeliosControlSubsystem* Control = GetWorld()->GetSubsystem<UHeliosControlSubsystem>();
        FHeliosActorState After;
        const bool bCancelled = Control && Control->GetState(TEXT("player_01"), After).bSuccess &&
            !After.bAutomationActive && After.LastStopReason == TEXT("manual_input");
        ReleaseFire();
        Check(TEXT("manual_fire_cancels_local_move"), bWasAutomatedBeforeFire && bCancelled,
            FString::Printf(TEXT("was_automated=%s cancelled=%s stop_reason=%s"),
                bWasAutomatedBeforeFire ? TEXT("true") : TEXT("false"), bCancelled ? TEXT("true") : TEXT("false"),
                *After.LastStopReason));
        BeginDeathResetCheck();
        return;
    }
    if (Stage == 30)
    {
        if (ResolveSingleEnemy() && PlaceCombatants())
        {
            bSawPlayerDeath = false;
            SetStage(31);
        }
        else if (StageTimer > 3.0f)
        {
            Check(TEXT("player_death_reset_setup"), false, TEXT("single enemy unavailable"));
            if (Mode == TEXT("all")) BeginResetChecks(); else Finish();
        }
        return;
    }
    if (Stage == 31 && StageTimer >= 3.25f)
    {
        SetStage(32);
        return;
    }
    if (Stage == 32)
    {
        AimAtEnemy();
        if (Player->IsDead())
        {
            bSawPlayerDeath = true;
            SetStage(33);
        }
        else if (StageTimer >= 30.0f)
        {
            Check(TEXT("enemy_can_kill_player"), false,
                FString::Printf(TEXT("timeout hp=%.1f notify=%d committed=%d"), Player->GetHealth(),
                    Enemy->GetAttackNotifyCount(), Enemy->GetAttacksCommitted()));
            if (Mode == TEXT("all")) BeginResetChecks(); else Finish();
        }
        return;
    }
    if (Stage == 33)
    {
        if (!Player->IsDead() || StageTimer >= 3.5f)
        {
            Check(TEXT("enemy_can_kill_player"), bSawPlayerDeath);
            const bool bReset = !Player->IsDead() && FMath::IsNearlyEqual(Player->GetHealth(), 100.0f)
                && Player->GetAmmo() == 30
                && Player->GetCharacterMovement()->MovementMode != MOVE_None
                && Player->GetCapsuleComponent()->GetCollisionEnabled() != ECollisionEnabled::NoCollision;
            Check(TEXT("player_death_resets_after_2s"), bReset,
                FString::Printf(TEXT("elapsed=%.2f hp=%.1f ammo=%d dead=%s movement=%d collision=%d"),
                    StageTimer, Player->GetHealth(), Player->GetAmmo(), Player->IsDead() ? TEXT("true") : TEXT("false"),
                    static_cast<int32>(Player->GetCharacterMovement()->MovementMode),
                    static_cast<int32>(Player->GetCapsuleComponent()->GetCollisionEnabled())));
            InputTestStartLocation = Player->GetActorLocation();
            SendKey(EKeys::W, IE_Pressed, 1.0f);
            SetStage(34);
        }
        return;
    }
    if (Stage == 34 && StageTimer >= 0.35f)
    {
        SendKey(EKeys::W, IE_Released, 0.0f);
        const float Travel = FVector::Dist2D(InputTestStartLocation, Player->GetActorLocation());
        Check(TEXT("player_input_restored_after_death"), Travel > 2.0f,
            FString::Printf(TEXT("travel_cm=%.2f"), Travel));
        if (Mode == TEXT("all")) BeginResetChecks(); else Finish();
    }
}

void AHeliosEnemyQARunner::BeginAmmoChecks()
{
    ReleaseFire();
    Director->SpawnCount = 1;
    Director->SelectEnemyMode(1);
    Enemy.Reset();
    SetStage(20);
}

void AHeliosEnemyQARunner::BeginDeathResetCheck()
{
    ReleaseFire();
    Director->SpawnCount = 1;
    Director->SelectEnemyMode(1);
    Enemy.Reset();
    SetStage(30);
}

void AHeliosEnemyQARunner::BeginResetChecks()
{
    ReleaseFire();
    DestroyBlockingWall();
    Director->SpawnCount = 4;
    Director->SelectEnemyMode(5);
    Enemy.Reset();
    ResetIndex = 0;
    FirstResetMismatch.Reset();
    bResetCountsStayedExact = true;
    SetStage(100);
}

void AHeliosEnemyQARunner::TickReset(float DeltaSeconds)
{
    if (Stage == 100 && StageTimer >= 1.0f)
    {
        BaselineEnemyCount = CountEnemies();
        BaselineControllerCount = CountEnemyControllers();
        BaselineWeaponCount = CountWeapons();
        BaselinePlayerCount = CountPlayers();
        BaselineHUDCount = CountHUDs();
        Check(TEXT("reset_baseline_4_enemies"), BaselineEnemyCount == 4,
            FString::Printf(TEXT("count=%d"), BaselineEnemyCount));
        Check(TEXT("reset_baseline_4_ai_controllers"), BaselineControllerCount == 4,
            FString::Printf(TEXT("count=%d"), BaselineControllerCount));
        Check(TEXT("reset_baseline_3_weapons"), BaselineWeaponCount == 3,
            FString::Printf(TEXT("count=%d"), BaselineWeaponCount));
        Check(TEXT("reset_baseline_player_and_hud"), BaselinePlayerCount == 1 && BaselineHUDCount == 1,
            FString::Printf(TEXT("players=%d huds=%d"), BaselinePlayerCount, BaselineHUDCount));
        SetStage(101);
        return;
    }
    if (Stage == 101)
    {
        if (!bStageActionDone)
        {
            bStageActionDone = true;
            SendKey(EKeys::LeftMouseButton, IE_Pressed, 1.0f);
            bFireHeld = true;
        }
        if (StageTimer >= 0.05f)
        {
            ReleaseFire();
            Director->ResetTest();
            ++ResetIndex;
            SetStage(102);
        }
        return;
    }
    if (Stage == 102 && StageTimer >= 0.6f)
    {
        const int32 Enemies = CountEnemies();
        const int32 Controllers = CountEnemyControllers();
        const int32 Weapons = CountWeapons();
        const int32 Projectiles = CountProjectiles();
        const int32 Players = CountPlayers();
        const int32 HUDs = CountHUDs();
        const bool bExact = Enemies == 4 && Controllers == 4 && Weapons == 3 && Projectiles == 0
            && Players == BaselinePlayerCount && HUDs == BaselineHUDCount;
        if (!bExact && FirstResetMismatch.IsEmpty())
        {
            FirstResetMismatch = FString::Printf(
                TEXT("cycle=%d enemies=%d controllers=%d weapons=%d projectiles=%d players=%d huds=%d"),
                ResetIndex, Enemies, Controllers, Weapons, Projectiles, Players, HUDs);
        }
        bResetCountsStayedExact = bResetCountsStayedExact && bExact;
        if (ResetIndex < 20)
        {
            SetStage(101);
        }
        else
        {
            const FString Detail = bResetCountsStayedExact
                ? TEXT("20/20 cycles exact: enemies=4 controllers=4 weapons=3 projectiles=0; player/HUD stable")
                : FirstResetMismatch;
            Check(TEXT("reset_20_cycles_exact_actor_counts"), bResetCountsStayedExact, Detail);
            Check(TEXT("reset_20_cycles_projectiles_cleaned"), CountProjectiles() == 0,
                FString::Printf(TEXT("projectiles=%d"), CountProjectiles()));
            Finish();
        }
    }
}

int32 AHeliosEnemyQARunner::CountEnemies() const
{
    int32 Count = 0;
    for (TActorIterator<AHeliosEnemyActor> It(GetWorld()); It; ++It) if (IsValid(*It)) ++Count;
    return Count;
}

int32 AHeliosEnemyQARunner::CountEnemyControllers() const
{
    int32 Count = 0;
    for (TActorIterator<AShooterAIController> It(GetWorld()); It; ++It) if (IsValid(*It)) ++Count;
    return Count;
}

int32 AHeliosEnemyQARunner::CountWeapons() const
{
    int32 Count = 0;
    for (TActorIterator<AShooterWeapon> It(GetWorld()); It; ++It) if (IsValid(*It)) ++Count;
    return Count;
}

int32 AHeliosEnemyQARunner::CountProjectiles() const
{
    int32 Count = 0;
    for (TActorIterator<AShooterProjectile> It(GetWorld()); It; ++It) if (IsValid(*It)) ++Count;
    for (TActorIterator<AHeliosBasketballProjectile> It(GetWorld()); It; ++It) if (IsValid(*It)) ++Count;
    return Count;
}

int32 AHeliosEnemyQARunner::CountPlayers() const
{
    int32 Count = 0;
    for (TActorIterator<AHeliosArenaCharacter> It(GetWorld()); It; ++It) if (IsValid(*It)) ++Count;
    return Count;
}

int32 AHeliosEnemyQARunner::CountHUDs() const
{
    int32 Count = 0;
    for (TActorIterator<AHUD> It(GetWorld()); It; ++It) if (IsValid(*It)) ++Count;
    return Count;
}

void AHeliosEnemyQARunner::TickPerformance(float DeltaSeconds)
{
    if (!bPerformanceSampling && StageTimer >= 15.0f)
    {
        bPerformanceSampling = true;
        PerformanceFrames.Reset();
    }
    if (bPerformanceSampling) PerformanceFrames.Add(DeltaSeconds);
    if (StageTimer >= 195.0f)
    {
        if (!bPerformanceEightPending)
        {
            if (PerformanceFrames.Num() > 0)
            {
                TArray<double> Sorted = PerformanceFrames;
                Sorted.Sort();
                double Sum = 0.0;
                for (double Sample : Sorted) Sum += Sample;
                const int32 P95 = FMath::Clamp(FMath::CeilToInt(Sorted.Num() * 0.95) - 1, 0, Sorted.Num() - 1);
                TSharedPtr<FJsonObject> Perf = MakeShared<FJsonObject>();
                Perf->SetNumberField(TEXT("enemy_count"), CountEnemies());
                Perf->SetNumberField(TEXT("measured_seconds"), 180.0);
                Perf->SetNumberField(TEXT("average_fps"), Sum > 0.0 ? Sorted.Num() / Sum : 0.0);
                Perf->SetNumberField(TEXT("p95_frame_ms"), Sorted[P95] * 1000.0);
                Report->SetObjectField(TEXT("performance_4"), Perf);
            }
            Check(TEXT("performance_4_enemy_duration"), true,
                TEXT("15 second warmup plus 180 second measured window completed"));
            bPerformanceEightPending = true;
            bPerformanceSampling = false;
            Pending(TEXT("performance_8_enemy_duration"),
                TEXT("8-enemy measurement is intentionally pending for the dedicated performance pass."));
            Finish();
        }
        else
        {
            Finish();
        }
    }
}

void AHeliosEnemyQARunner::TickCapture(float DeltaSeconds)
{
    static float CaptureTimer = 0.0f;
    CaptureTimer += DeltaSeconds;
    if (CaptureTimer >= 0.1f && !bScreenshotOutstanding)
    {
        CaptureTimer = 0.0f;
        const FString File = FPaths::Combine(RootDir, TEXT("qa/enemies/video_frames"),
            FString::Printf(TEXT("frame_%05d.png"), CaptureIndex++));
        FScreenshotRequest::RequestScreenshot(File, false, false);
        bScreenshotOutstanding = true;
    }
    if (bScreenshotOutstanding && !FScreenshotRequest::IsScreenshotRequested())
    {
        bScreenshotOutstanding = false;
    }
    if (StageTimer >= 24.0f)
    {
        Pending(TEXT("capture_combat_choreography"),
            TEXT("Frame capture exists; dedicated pass must supply per-type chase/attack/player-return-fire choreography."));
        Report->SetNumberField(TEXT("capture_frames_requested"), CaptureIndex);
        Finish();
    }
}

void AHeliosEnemyQARunner::SaveReport()
{
    Report->SetArrayField(TEXT("checks"), Checks);
    Report->SetArrayField(TEXT("pending"), PendingChecks);
    Report->SetNumberField(TEXT("failures"), FailureCount);
    Report->SetNumberField(TEXT("frame_samples"), FrameTimes.Num());
    Report->SetStringField(TEXT("finished_utc"), FDateTime::UtcNow().ToIso8601());
    const TCHAR* Status = FailureCount > 0
        ? TEXT("failed")
        : (PendingChecks.Num() > 0 ? TEXT("pending") : TEXT("passed"));
    Report->SetStringField(TEXT("status"), Status);
    FString Output;
    FJsonSerializer::Serialize(Report.ToSharedRef(), TJsonWriterFactory<>::Create(&Output));
    const FString Name = FString::Printf(TEXT("runtime_enemy_qa_%s.json"), *Mode);
    FFileHelper::SaveStringToFile(Output, *FPaths::Combine(RootDir, TEXT("qa/enemies"), Name),
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void AHeliosEnemyQARunner::Finish()
{
    if (bFinished)
    {
        return;
    }
    ReleaseFire();
    DestroyBlockingWall();
    bFinished = true;
    SaveReport();
    UE_LOG(LogTemp, Display, TEXT("HELIOS_ENEMY_QA finished mode=%s failures=%d"), *Mode, FailureCount);
    if (!FParse::Param(FCommandLine::Get(), TEXT("HeliosEnemyQANoExit")))
    {
        FPlatformMisc::RequestExit(false);
    }
    Destroy();
}
