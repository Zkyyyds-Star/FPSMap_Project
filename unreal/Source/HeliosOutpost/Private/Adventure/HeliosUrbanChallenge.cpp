#include "Adventure/HeliosAdventureDirector.h"
#include "Adventure/HeliosAdventurePickup.h"
#include "Engine/World.h"
#include "HeliosOutpost.h"
#include "Boss/HeliosUrbanBoss.h"
#include "Boss/HeliosUrbanHazard.h"
#include "NavigationSystem.h"
#include "Player/HeliosArenaCharacter.h"
#include "Player/HeliosQingYaAbilityComponent.h"
#include "World/HeliosMarker.h"
#include "HAL/PlatformTime.h"
#include "Enemies/HeliosEnemyActor.h"
#include "EngineUtils.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Kismet/GameplayStatics.h"

bool AHeliosAdventureDirector::TrySpawnUrbanFinalBoss()
{
    if (!Player || PendingBossNumbers.IsEmpty()) return false;
    auto* Navigation=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!Navigation) return false;
    // Open central ground fits the five-metre giant, clear of bridges.
    for (const FVector Candidate : {FVector(-2200,0,0),FVector(2200,0,0),FVector(0,0,0)})
    {
        if (FVector::Dist2D(Player->GetActorLocation(),Candidate)<500) continue;
        FNavLocation Ground;
        if (!Navigation->ProjectPointToNavigation(Candidate+FVector(0,0,30),Ground,FVector(150,150,180))) continue;
        const FTransform Transform(Ground.Location+FVector(0,0,127));
        auto* Boss=GetWorld()->SpawnActorDeferred<AHeliosUrbanBoss>(AHeliosUrbanBoss::StaticClass(),Transform,this,nullptr,
            ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding);
        if (!Boss) continue;
        Boss->SetBossNumber(30);
        Boss->SetRuntimeIdentity(FString::Printf(TEXT("urban_final_giant_r%03d"),RunSerial),TEXT("urban_central_court"));
        Boss->FinishSpawning(Transform);
        if (Boss->IsActorBeingDestroyed()) continue;
        Bosses.Add(Boss); PendingBossNumbers.RemoveAt(0); ++BossSpawnSerial;
        return true;
    }
    return false;
}

bool AHeliosAdventureDirector::IsUrbanChallenge() const
{
    return GetWorld() && GetWorld()->GetMapName().EndsWith(TEXT("UrbanChallenge"));
}

FString AHeliosAdventureDirector::GetUrbanWaveText() const
{
    if (!UrbanConfigError.IsEmpty()) return UrbanConfigError;
    if (UrbanRecoveryWarningRemaining>0)
        return FString::Printf(TEXT("Wave %02d / 30\nReinforcements arrive in %.1fs\nLeave the blue arrival marker"),UrbanWaveIndex+1,UrbanRecoveryWarningRemaining);
    if (bUrbanVictory) return UrbanSkippedWaves > 0
        ? FString::Printf(TEXT("都市断层流程完成 · 跳过 %d 波\n跳过不计击杀与奖励"), UrbanSkippedWaves)
        : TEXT("30波完成 · 都市断层挑战通关");
    if (!UrbanSchedule.Waves.IsValidIndex(UrbanWaveIndex)) return TEXT("都市断层 · 开发预览");
    const auto& Wave = UrbanSchedule.Waves[UrbanWaveIndex];
    const int32 Remaining = FMath::Max(0, Wave.Total - UrbanRegularIssued - UrbanBossIssued) + GetAliveCount() + GetBossCount();
    return FString::Printf(TEXT("第 %02d / 30 波 · 本波 %d 只\n剩余 %d · 活跃 %d / %d\n%s"),
        UrbanWaveIndex + 1, Wave.Total, Remaining, GetAliveCount() + GetBossCount(), Wave.AliveCap,
        UrbanRestRemaining > 0.f ? *FString::Printf(TEXT("准备 %.0fs"), UrbanRestRemaining) : TEXT("清除本波所有敌人"));
}

void AHeliosAdventureDirector::TickUrbanWaves(float DeltaSeconds)
{
    if (!UrbanConfigError.IsEmpty() || !UrbanSchedule.Waves.IsValidIndex(UrbanWaveIndex)) return;
    // Derive fulfilled quotas from counted deaths + living actors, not corpse lifetime.
    // Reclaimed or externally removed live actors thus return their spawn slot.
    UrbanBossIssued = BossKills - UrbanWaveStartBossKills + GetBossCount();
    UrbanRegularIssued = Kills - UrbanWaveStartKills - (BossKills - UrbanWaveStartBossKills) + GetAliveCount();
    if (UrbanRestRemaining > 0.f)
    {
        UrbanRestRemaining = FMath::Max(0.f, UrbanRestRemaining - DeltaSeconds);
        return;
    }
    if (TickUrbanSpawnRecovery(DeltaSeconds)) return;
    const auto Wave = UrbanSchedule.Waves[UrbanWaveIndex];
    UrbanSpawnRemaining -= DeltaSeconds;
    if (UrbanSpawnRemaining <= 0.f)
    {
        // A failed safe spawn consumes no quota. Cap includes the reused midboss.
        if (GetAliveCount() + GetBossCount() < Wave.AliveCap)
        {
            if (UrbanBossIssued < Wave.Midbosses && UrbanRegularIssued >= (Wave.Total - Wave.Midbosses) / 2)
            {
                if (PendingBossNumbers.IsEmpty()) PendingBossNumbers.Add(UrbanWaveIndex + 1);
                if (TrySpawnPendingBoss()) ++UrbanBossIssued;
            }
            else if (UrbanRegularIssued < Wave.Total - Wave.Midbosses && TrySpawnEnemy()) ++UrbanRegularIssued;
        }
        UrbanSpawnRemaining = Wave.SpawnSeconds;
    }
    if (UrbanRegularIssued == Wave.Total - Wave.Midbosses && UrbanBossIssued == Wave.Midbosses &&
        GetAliveCount() == 0 && GetBossCount() == 0)
    {
        UE_LOG(LogHeliosOutpost, Display, TEXT("URBAN_WAVES cleared wave=%d total=%d time=%.2f"),
            UrbanWaveIndex + 1, Wave.Total, SurvivalSeconds);
        AdvanceUrbanWave(false);
    }
}

bool AHeliosAdventureDirector::CanSkipUrbanWave() const
{
    return IsUrbanChallenge() && !bSelectingWeapon && !bRunOver && !bUrbanVictory &&
        UrbanConfigError.IsEmpty() && UrbanSchedule.Waves.IsValidIndex(UrbanWaveIndex) &&
        UrbanRestRemaining <= 0.f && IsValid(Player) && !Player->IsDead() &&
        !UGameplayStatics::IsGamePaused(this);
}

void AHeliosAdventureDirector::AdvanceUrbanWave(bool bSkipped)
{
    const auto Wave = UrbanSchedule.Waves[UrbanWaveIndex];
    if (bSkipped) ++UrbanSkippedWaves;
    ClearUrbanSpawnRecovery();
    PendingBossNumbers.Reset();
    UrbanRegularIssued = UrbanBossIssued = 0;
    UrbanWaveStartKills = Kills; UrbanWaveStartBossKills = BossKills;
    UrbanSpawnRemaining = 0.f;
    if (UrbanWaveIndex == UrbanSchedule.Waves.Num() - 1)
    {
        bUrbanVictory = true;
        EndRun();
        if(UrbanSkippedWaves==0 && Player && !Player->IsDead())
            if(auto* Qing=Player->GetQingYaAbility())Qing->PlayVictoryMusic();
        return;
    }
    ++UrbanWaveIndex;
    UrbanRestRemaining = Wave.RestSeconds;
    SpawnPickup(true, TEXT("supply"), true);
    SpawnPickup(false, FString(), true);
}

void AHeliosAdventureDirector::CancelUrbanWaveActors()
{
    ClearUrbanSpawnRecovery();
    PendingBossNumbers.Reset();
    // Follow the owner chain before destroying pawns. This includes weapons,
    // in-flight enemy rounds, basketballs and boss effects, but leaves player
    // projectiles and companion actors intact. Destruction never applies damage.
    TSet<AActor*> WaveActors;
    for (AHeliosEnemyActor* Enemy : Enemies) if (IsValid(Enemy)) WaveActors.Add(Enemy);
    for (AHeliosEnemyBoss* Boss : Bosses) if (IsValid(Boss)) WaveActors.Add(Boss);
    TArray<AActor*> Owned;
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        AActor* Actor = *It;
        if (WaveActors.Contains(Actor)) continue;
        bool bOwned = WaveActors.Contains(Actor->GetInstigator());
        TSet<AActor*> Visited;
        for (AActor* WaveOwner = Actor->GetOwner(); WaveOwner && !Visited.Contains(WaveOwner); WaveOwner = WaveOwner->GetOwner())
        {
            Visited.Add(WaveOwner);
            if (WaveActors.Contains(WaveOwner)) { bOwned = true; break; }
        }
        if (bOwned || Actor->IsA<AHeliosUrbanHazard>()) Owned.Add(Actor);
    }
    for (AActor* Actor : Owned) if (IsValid(Actor)) Actor->Destroy();
    for (AHeliosEnemyActor* Enemy : Enemies) DestroyEnemy(Enemy);
    for (AHeliosEnemyBoss* Boss : Bosses) DestroyEnemy(Boss);
    Enemies.Reset(); Bosses.Reset(); CountedDeaths.Reset(); CountedBossDeaths.Reset();
    HiddenFarSeconds.Reset(); NextBossSpawnAttemptSeconds = 0.f;
}

bool AHeliosAdventureDirector::TrySkipUrbanWave(int32 ExpectedRun, int32 ExpectedWave)
{
    if (!CanSkipUrbanWave() || ExpectedRun != RunSerial || ExpectedWave != GetUrbanWaveNumber()) return false;
    // Settle only deaths that already happened through combat. Living actors
    // below are removed, never killed, so no score, drops or feedback is created.
    UpdateEnemies(0.f); UpdateBosses();
    const int32 Wave = GetUrbanWaveNumber();
    CancelUrbanWaveActors();
    AdvanceUrbanWave(true);
    UE_LOG(LogHeliosOutpost, Display, TEXT("URBAN_SKIP run=%d skipped_wave=%d next_wave=%d kills=%d boss_kills=%d score=%d victory=%d"),
        RunSerial, Wave, GetUrbanWaveNumber(), Kills, BossKills, Score, bUrbanVictory);
    return true;
}

void AHeliosAdventureDirector::ClearUrbanSpawnRecovery()
{
    if (IsValid(UrbanRecoveryBeacon)) UrbanRecoveryBeacon->Destroy();
    UrbanRecoveryBeacon=nullptr; UrbanRecoveryMarker=nullptr;
    UrbanRecoveryWarningRemaining=0; UrbanEmptySpawnSeconds=0; UrbanRecoveryWarningStartedWall=0;
}

bool AHeliosAdventureDirector::TickUrbanSpawnRecovery(float DeltaSeconds)
{
    const auto& Wave=UrbanSchedule.Waves[UrbanWaveIndex];
    const int32 MissingRegular=Wave.Total-Wave.Midbosses-UrbanRegularIssued;
    const int32 MissingBoss=Wave.Midbosses-UrbanBossIssued;
    if (GetAliveCount()+GetBossCount()>0 || MissingRegular+MissingBoss<=0)
    { ClearUrbanSpawnRecovery(); return false; }
    if (IsValid(UrbanRecoveryMarker))
    {
        UrbanRecoveryWarningRemaining-=DeltaSeconds;
        const double WallWarning=FPlatformTime::Seconds()-UrbanRecoveryWarningStartedWall;
        if (UrbanRecoveryWarningRemaining>0 || WallWarning<3.0) return true;
        UrbanLastRecoveryWarningWallSeconds=WallWarning;
        // Revalidate the SAME advertised entrance. Only visibility may be relaxed.
        const bool Spawned=bUrbanRecoveryBoss ? TrySpawnPendingBoss(true,UrbanRecoveryMarker)
            : TrySpawnEnemy(true,UrbanRecoveryMarker,EHeliosEnemyType::E01_Rifle);
        if (Spawned)
        {
            ++UrbanRecoverySpawns;
            if(bUrbanRecoveryBoss)++UrbanBossIssued; else ++UrbanRegularIssued;
        }
        UE_LOG(LogHeliosOutpost,Display,TEXT("URBAN_SPAWN recovery=%s wave=%d marker=%s live=%d kills=%d"),
            Spawned?TEXT("spawned"):TEXT("canceled_revalidate"),UrbanWaveIndex+1,
            *UrbanRecoveryMarker->MarkerId,GetAliveCount()+GetBossCount(),Kills);
        ClearUrbanSpawnRecovery(); UrbanSpawnRemaining=Wave.SpawnSeconds;
        return true;
    }
    UrbanEmptySpawnSeconds+=DeltaSeconds;
    if(UrbanEmptySpawnSeconds<5.f)return false;
    UrbanEmptySpawnSeconds=0;
    bUrbanRecoveryBoss=MissingBoss>0 && UrbanRegularIssued>=(Wave.Total-Wave.Midbosses)/2;
    UE_LOG(LogHeliosOutpost,Warning,TEXT("URBAN_SPAWN empty_remaining wave=%d regular=%d boss=%d cap=%d marker_count=%d reasons=%s"),
        UrbanWaveIndex+1,MissingRegular,MissingBoss,Wave.AliveCap,EnemyMarkers.Num(),*UrbanLastSpawnDiagnostics);
    // The independent final Boss has dedicated central court anchors, not E05 entrances.
    if(bUrbanRecoveryBoss && UrbanWaveIndex==29)return false;
    if(bUrbanRecoveryBoss && PendingBossNumbers.IsEmpty())PendingBossNumbers.Add(UrbanWaveIndex+1);
    for(AHeliosMarker* Marker:EnemyMarkers)
    {
        if(!IsValid(Marker))continue;
        FVector Position;
        const bool Valid=bUrbanRecoveryBoss ? IsBossSpawnCandidateValid(*Marker,Position,true)
            : IsSpawnCandidateValid(*Marker,EHeliosEnemyType::E01_Rifle,Position,true);
        if(!Valid)continue;
        const float HalfHeight=FHeliosEnemyDefinition::ForType(bUrbanRecoveryBoss?EHeliosEnemyType::E05_Boss:EHeliosEnemyType::E01_Rifle).CapsuleHalfHeight;
        const FTransform Transform(Position-FVector(0,0,HalfHeight-3.f));
        auto* Beacon=GetWorld()->SpawnActorDeferred<AHeliosUrbanHazard>(AHeliosUrbanHazard::StaticClass(),Transform,this,nullptr);
        if(!Beacon)continue;
        Beacon->Configure(this,false,200,3.5f,0); Beacon->SetArrivalMarker(); Beacon->FinishSpawning(Transform);
        UrbanRecoveryBeacon=Beacon; UrbanRecoveryMarker=Marker; UrbanRecoveryWarningRemaining=3.f; UrbanRecoveryWarningStartedWall=FPlatformTime::Seconds();
        UE_LOG(LogHeliosOutpost,Display,TEXT("URBAN_SPAWN recovery=queued wave=%d marker=%s warning=3.0 damage=0"),UrbanWaveIndex+1,*Marker->MarkerId);
        return true;
    }
    return false;
}

EHeliosEnemyType AHeliosAdventureDirector::ChooseUrbanRegularType()
{
    const auto& Wave=UrbanSchedule.Waves[UrbanWaveIndex];
    int32 Alive[4]={0,0,0,0};
    for(const AHeliosEnemyActor* Enemy:Enemies)
        if(IsValid(Enemy) && !Enemy->IsEnemyDead())
        {
            const int32 Index=static_cast<int32>(Enemy->EnemyType)-1;
            if(Index>=0 && Index<4)++Alive[Index];
        }
    int32 Weights[4];int32 Sum=0;
    for(int32 Type=0;Type<4;++Type)
    {
        Weights[Type]=Wave.TypeWeights[Type];
        if((Type==0 && Alive[Type]>=Wave.RifleCap) || (Type==2 && Alive[Type]>=Wave.SkirmisherCap) || (Type==3 && Alive[Type]>=2))Weights[Type]=0;
        Sum+=Weights[Type];
    }
    // Validated positive charger weight leaves an eligible type under every ranged cap.
    if(Sum<=0)return EHeliosEnemyType::E02_Charger;
    int32 Roll=Random.RandRange(1,Sum);
    for(int32 Type=0;Type<4;++Type)
        if((Roll-=Weights[Type])<=0)return static_cast<EHeliosEnemyType>(Type+1);
    return EHeliosEnemyType::E02_Charger;
}
