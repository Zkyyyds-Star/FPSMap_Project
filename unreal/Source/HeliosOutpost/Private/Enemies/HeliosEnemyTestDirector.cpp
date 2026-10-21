#include "Enemies/HeliosEnemyTestDirector.h"

#include "Weapons/HeliosBasketballProjectile.h"
#include "Player/HeliosArenaCharacter.h"
#include "Player/HeliosQingYaAbilityComponent.h"
#include "Combat/HeliosKillFeedbackSubsystem.h"
#include "World/HeliosMarker.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "Variant_Shooter/Weapons/ShooterProjectile.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

AHeliosEnemyTestDirector::AHeliosEnemyTestDirector()
{
    PrimaryActorTick.bCanEverTick = false;
    Tags.Add(TEXT("HeliosEnemyTestDirector"));
}

void AHeliosEnemyTestDirector::BeginPlay()
{
    Super::BeginPlay();
    if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
    {
        EnableInput(PlayerController);
        if (InputComponent)
        {
            InputComponent->BindAction(TEXT("HeliosEnemy1"), IE_Pressed, this, &AHeliosEnemyTestDirector::SelectMode1);
            InputComponent->BindAction(TEXT("HeliosEnemy2"), IE_Pressed, this, &AHeliosEnemyTestDirector::SelectMode2);
            InputComponent->BindAction(TEXT("HeliosEnemy3"), IE_Pressed, this, &AHeliosEnemyTestDirector::SelectMode3);
            InputComponent->BindAction(TEXT("HeliosEnemy4"), IE_Pressed, this, &AHeliosEnemyTestDirector::SelectMode4);
            InputComponent->BindAction(TEXT("HeliosEnemyMixed"), IE_Pressed, this, &AHeliosEnemyTestDirector::SelectMode5);
            InputComponent->BindAction(TEXT("HeliosEnemyReset"), IE_Pressed, this, &AHeliosEnemyTestDirector::ResetTest);
        }
    }
    // Give GameMode's navigation rebuild one frame to register before AI possession begins.
    GetWorldTimerManager().SetTimer(InitialSpawnTimer, this,
        &AHeliosEnemyTestDirector::ResetTest, 0.05f, false);
}

void AHeliosEnemyTestDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorldTimerManager().ClearTimer(InitialSpawnTimer);
    CleanupTestActors();
    if(auto* Feedback=GetWorld()->GetSubsystem<UHeliosKillFeedbackSubsystem>())Feedback->ResetRun();
    Super::EndPlay(EndPlayReason);
}

void AHeliosEnemyTestDirector::SelectEnemyMode(int32 Mode)
{
    SelectedMode = FMath::Clamp(Mode, 1, 5);
    ResetTest();
}

void AHeliosEnemyTestDirector::ResetTest()
{
    GetWorldTimerManager().ClearTimer(InitialSpawnTimer);
    CleanupTestActors();
    for (TActorIterator<AHeliosArenaCharacter> It(GetWorld()); It; ++It)
    {
        It->GetQingYaAbility()->ResetRun();
        It->ResetForTest();
    }
    ++ResetSerial;
    SpawnSelectedEnemies();
}

void AHeliosEnemyTestDirector::SelectMode1() { SelectEnemyMode(1); }
void AHeliosEnemyTestDirector::SelectMode2() { SelectEnemyMode(2); }
void AHeliosEnemyTestDirector::SelectMode3() { SelectEnemyMode(3); }
void AHeliosEnemyTestDirector::SelectMode4() { SelectEnemyMode(4); }
void AHeliosEnemyTestDirector::SelectMode5() { SelectEnemyMode(5); }

TSubclassOf<AHeliosEnemyActor> AHeliosEnemyTestDirector::ClassForType(EHeliosEnemyType Type) const
{
    switch (Type)
    {
    case EHeliosEnemyType::E02_Charger: return AHeliosEnemyE02::StaticClass();
    case EHeliosEnemyType::E03_Skirmisher: return AHeliosEnemyE03::StaticClass();
    case EHeliosEnemyType::E04_Basketball: return AHeliosEnemyE04::StaticClass();
    case EHeliosEnemyType::E01_Rifle:
    default: return AHeliosEnemyE01::StaticClass();
    }
}

void AHeliosEnemyTestDirector::SpawnSelectedEnemies()
{
    if (!GetWorld())
    {
        return;
    }

    TArray<AHeliosMarker*> EnemyMarkers;
    for (TActorIterator<AHeliosMarker> It(GetWorld()); It; ++It)
    {
        if (It->MarkerType.Equals(TEXT("enemy"), ESearchCase::IgnoreCase))
        {
            EnemyMarkers.Add(*It);
        }
    }
    EnemyMarkers.Sort([](const AHeliosMarker& A, const AHeliosMarker& B)
    {
        return A.MarkerId < B.MarkerId;
    });

    const int32 Count = FMath::Min(FMath::Clamp(SpawnCount, 1, 18), EnemyMarkers.Num());
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const EHeliosEnemyType Type = SelectedMode == 5
            ? static_cast<EHeliosEnemyType>((Index % 4) + 1)
            : static_cast<EHeliosEnemyType>(SelectedMode);
        const FHeliosEnemyDefinition Definition = FHeliosEnemyDefinition::ForType(Type);
        AHeliosMarker* Marker = EnemyMarkers[Index];
        FTransform SpawnTransform = Marker->GetActorTransform();
        SpawnTransform.AddToTranslation(FVector(0.0f, 0.0f, Definition.CapsuleHalfHeight + 2.0f));

        AHeliosEnemyActor* Enemy = GetWorld()->SpawnActorDeferred<AHeliosEnemyActor>(
            ClassForType(Type), SpawnTransform, this, nullptr,
            ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
        if (!Enemy)
        {
            continue;
        }
        const FString RuntimeId = FString::Printf(TEXT("enemy_runtime_%03d_%02d_%s"),
            ResetSerial, Index + 1, *Definition.TypeId);
        Enemy->SetRuntimeIdentity(RuntimeId, Marker->MarkerId);
        Enemy->FinishSpawning(SpawnTransform);
        SpawnedEnemies.Add(Enemy);
    }
}

void AHeliosEnemyTestDirector::CleanupTestActors()
{
    if (!GetWorld())
    {
        SpawnedEnemies.Reset();
        return;
    }

    // Projectiles are independent once launched, so remove them before their owners.
    for (TActorIterator<AHeliosBasketballProjectile> It(GetWorld()); It; ++It)
    {
        It->Destroy();
    }
    for (TActorIterator<AShooterProjectile> It(GetWorld()); It; ++It)
    {
        It->Destroy();
    }

    TSet<const AActor*> EnemySet;
    for (AHeliosEnemyActor* Enemy : SpawnedEnemies)
    {
        if (IsValid(Enemy))
        {
            EnemySet.Add(Enemy);
        }
    }
    for (TActorIterator<AShooterWeapon> It(GetWorld()); It; ++It)
    {
        if (EnemySet.Contains(It->GetOwner()))
        {
            It->Destroy();
        }
    }
    for (AHeliosEnemyActor* Enemy : SpawnedEnemies)
    {
        if (!IsValid(Enemy))
        {
            continue;
        }
        if (AController* Controller = Enemy->GetController())
        {
            Enemy->StopShooting();
            if (AAIController* AI = Cast<AAIController>(Controller))
            {
                AI->StopMovement();
                // StateTree context still needs its possessed pawn during exit.
                if (AI->GetBrainComponent()) AI->GetBrainComponent()->StopLogic(TEXT("Helios test reset"));
            }
            Controller->UnPossess();
            Controller->Destroy();
        }
        Enemy->Destroy();
    }
    SpawnedEnemies.Reset();
}

TArray<AHeliosEnemyActor*> AHeliosEnemyTestDirector::GetSpawnedEnemies() const
{
    TArray<AHeliosEnemyActor*> Result;
    for (AHeliosEnemyActor* Enemy : SpawnedEnemies)
    {
        if (IsValid(Enemy))
        {
            Result.Add(Enemy);
        }
    }
    return Result;
}

int32 AHeliosEnemyTestDirector::GetAliveEnemyCount() const
{
    int32 Alive = 0;
    for (const AHeliosEnemyActor* Enemy : SpawnedEnemies)
    {
        if (IsValid(Enemy) && !Enemy->IsEnemyDead())
        {
            ++Alive;
        }
    }
    return Alive;
}

FHeliosEnemyDebugState AHeliosEnemyTestDirector::GetEnemyDebugState() const
{
    FHeliosEnemyDebugState State;
    State.SelectedMode = SelectedMode;
    State.SpawnedCount = GetSpawnedEnemies().Num();
    State.AliveCount = GetAliveEnemyCount();
    for (const AHeliosEnemyActor* Enemy : SpawnedEnemies)
    {
        if (!IsValid(Enemy))
        {
            continue;
        }
        FHeliosEnemyDebugEntry& Entry = State.Enemies.AddDefaulted_GetRef();
        Entry.RuntimeActorId = Enemy->GetRuntimeActorId();
        Entry.SourceMarkerId = Enemy->SourceMarkerId;
        Entry.EnemyTypeId = Enemy->GetEnemyTypeId();
        Entry.HP = Enemy->GetHealth();
        Entry.bDead = Enemy->IsEnemyDead();
        Entry.CombatState = Enemy->GetCombatState();
        Entry.AttackNotifyCount = Enemy->GetAttackNotifyCount();
        Entry.AttacksCommitted = Enemy->GetAttacksCommitted();
    }
    return State;
}
