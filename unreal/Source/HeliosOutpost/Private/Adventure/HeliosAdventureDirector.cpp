#include "Adventure/HeliosAdventureDirector.h"
#include "Player/HeliosQingYaAbilityComponent.h"
#include "Player/HeliosQingYaSelection.h"
#include "Pets/HeliosPetDog.h"
#include "Variant_Shooter/AI/ShooterAIController.h"
#include "Combat/HeliosKillFeedbackSubsystem.h"
#include "Weapons/HeliosFighterWeapon.h"
#include "Weapons/HeliosLightstickWeapon.h"
#include "Weapons/HeliosColaWeapon.h"
#include "Town/HeliosTownSubsystem.h"
#include "MCP/HeliosCodexMCPSubsystem.h"
#include "Engine/GameInstance.h"
#include "Weapons/HeliosWeaponVFX.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"

#include "Adventure/HeliosAdventurePickup.h"
#include "Adventure/HeliosAdventureCharacter.h"
#include "Player/HeliosArenaCharacter.h"
#include "Weapons/HeliosBasketballProjectile.h"
#include "Boss/HeliosEnemyBoss.h"
#include "Control/HeliosControlSubsystem.h"
#include "World/HeliosMarker.h"
#include "HeliosOutpost.h"
#include "World/HeliosZone.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Weapons/HeliosCombatWeapons.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "NavigationPath.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"
#include "Variant_Shooter/Weapons/ShooterProjectile.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

namespace
{
constexpr float EnemyAttemptInterval = 8.0f;
constexpr float MinimumPlayerDistance = 2000.0f;
constexpr float MinimumRunSpawnDistance = 1200.0f;
constexpr float ReclaimDistance = 6000.0f;
constexpr float ReclaimHiddenSeconds = 12.0f;

FString BestFilePath()
{
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosLoadoutQA"))||FParse::Param(FCommandLine::Get(),TEXT("HeliosLoadoutFlowQA")))return FPaths::ProjectSavedDir()/TEXT("Automation/HeliosLoadout/adventure-best.json");
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosKillFeedbackQA")))return FPaths::ProjectSavedDir()/TEXT("SaveGames/HeliosKillFeedbackQABest.json");
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosCalamityDemo")))return FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("SaveGames/HeliosCalamityDemoBest.json"));
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosQingxiaoQA")))return FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("SaveGames/HeliosQingxiaoAdventureBest.json"));
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"),
        FParse::Param(FCommandLine::Get(),TEXT("HeliosLightstickDemo")) ? TEXT("HeliosLightstickDemoBest.json") : FParse::Param(FCommandLine::Get(),TEXT("HeliosColaQA")) ? TEXT("HeliosColaAdventureBest.json") : FParse::Param(FCommandLine::Get(),TEXT("HeliosChopsticksQA")) ? TEXT("HeliosChopsticksAdventureBest.json") : TEXT("HeliosAdventureBest.json"));
}
}

AHeliosAdventureDirector::AHeliosAdventureDirector()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.1f;
    Tags.Add(TEXT("HeliosAdventureDirector"));
}

void AHeliosAdventureDirector::BeginPlay()
{
    Super::BeginPlay();
    // A world-specific collection keeps authored emissive RGB visible in the
    // physical daylight map. Default zero preserves all other map rendering.
    if(auto* Collection=LoadObject<UMaterialParameterCollection>(nullptr,TEXT("/Game/UrbanChallenge/Materials/MPC_UrbanWeaponExposure.MPC_UrbanWeaponExposure")))
        if(auto* Instance=GetWorld()->GetParameterCollectionInstance(Collection))
            Instance->SetScalarParameterValue(TEXT("CompensateWeaponExposure"),IsUrbanChallenge()?1.f:0.f);
    if(FParse::Param(FCommandLine::Get(),TEXT("CalamityDefault")))SelectedWeaponId=TEXT("calamity");
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosYunSummerMegatron")))SelectedWeaponId=TEXT("cfm_yun_summer_megatron");
    SelectedLoadout=FHeliosLoadout::FromLegacyWeapon(SelectedWeaponId);
    if(!FParse::Param(FCommandLine::Get(),TEXT("CalamityDefault"))&&!FParse::Param(FCommandLine::Get(),TEXT("HeliosYunSummerMegatron")))
        if(HeliosQingYaSelection::Restore(SelectedLoadout))SelectedWeaponId=SelectedLoadout.PrimaryWeaponId;
    if (UHeliosTownSubsystem* Town = GetTownSubsystem())
    {
        Town->AttachDirector(this);
        EnterWeaponSelection();
        return;
    }
    if (!IsUrbanChallenge()) LoadBest();
    FString QAMode;
    if (FParse::Value(FCommandLine::Get(), TEXT("HeliosAdventureQA="), QAMode)) RestartRun();
    else EnterWeaponSelection();
}

void AHeliosAdventureDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UHeliosTownSubsystem* Town = GetTownSubsystem()) Town->SaveProgress();
    if (GetWorld() && UGameplayStatics::IsGamePaused(GetWorld()))
    {
        UGameplayStatics::SetGamePaused(GetWorld(), false);
    }
    CleanupOwnedActors();
    Super::EndPlay(EndPlayReason);
}

void AHeliosAdventureDirector::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (UHeliosTownSubsystem* Town = GetTownSubsystem())
    {
        Town->TickTown(DeltaSeconds);
        return;
    }
    if (bSelectingWeapon || bRunOver || UGameplayStatics::IsGamePaused(this))
    {
        return;
    }
    if (!IsValid(Player))
    {
        Player = Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
        if (!Player) return;
    }

    SurvivalSeconds += DeltaSeconds;
    UpdateEnemies(DeltaSeconds);
    UpdateBosses();
    UpdateZone();
    UpdatePickups();

    if (IsUrbanChallenge())
    {
        TickUrbanWaves(DeltaSeconds);
        return;
    }

    while (SurvivalSeconds >= NextEnemyAttemptSeconds)
    {
        TrySpawnEnemy();
        NextEnemyAttemptSeconds += EnemyAttemptInterval;
    }
    while (SurvivalSeconds >= NextBossEventSeconds)
    {
        PendingBossNumbers.Add(++BossEventSerial);
        NextBossEventSeconds += BossIntervalSeconds;
        UE_LOG(LogHeliosOutpost, Display, TEXT("HELIOS_BOSS event=%d registered at %.1fs"),
            BossEventSerial, SurvivalSeconds);
    }
    // One pending event receives one safe-spawn attempt per second.  This keeps
    // long-blocked bosses from materializing as a same-frame pile-up.
    if (!PendingBossNumbers.IsEmpty() && SurvivalSeconds >= NextBossSpawnAttemptSeconds)
    {
        TrySpawnPendingBoss();
        NextBossSpawnAttemptSeconds = SurvivalSeconds + 1.0f;
    }
}

AHeliosAdventureDirector* AHeliosAdventureDirector::Find(UWorld* World)
{
    if (!World) return nullptr;
    for (TActorIterator<AHeliosAdventureDirector> It(World); It; ++It)
    {
        return *It;
    }
    return nullptr;
}

int32 AHeliosAdventureDirector::GetAliveCount() const
{
    int32 Result = 0;
    for (const AHeliosEnemyActor* Enemy : Enemies)
    {
        if (IsValid(Enemy) && !Enemy->IsEnemyDead()) ++Result;
    }
    return Result;
}

UHeliosTownSubsystem* AHeliosAdventureDirector::GetTownSubsystem() const
{
    UHeliosTownSubsystem* Town = GetWorld() ? GetWorld()->GetSubsystem<UHeliosTownSubsystem>() : nullptr;
    return Town && Town->IsTownWorld() ? Town : nullptr;
}

bool AHeliosAdventureDirector::IsTownMode() const { return GetTownSubsystem() != nullptr; }

int32 AHeliosAdventureDirector::GetBossCount() const
{
    int32 Result = 0;
    for (const AHeliosEnemyBoss* Boss : Bosses)
        if (IsValid(Boss) && !Boss->IsEnemyDead()) ++Result;
    return Result;
}

float AHeliosAdventureDirector::GetSecondsUntilNextBoss() const
{
    if (const auto* Town=GetTownSubsystem()) return Town->GetSecondsUntilNextBoss();
    return PendingBossNumbers.IsEmpty()
        ? FMath::Max(0.0f, NextBossEventSeconds - SurvivalSeconds)
        : 0.0f;
}

int32 AHeliosAdventureDirector::GetPendingBossCount() const
{
    if (const auto* Town=GetTownSubsystem()) return Town->GetPendingBossCount();
    return PendingBossNumbers.Num();
}

TArray<AHeliosEnemyBoss*> AHeliosAdventureDirector::GetBossActors() const
{
    TArray<AHeliosEnemyBoss*> Result;
    for (AHeliosEnemyBoss* Boss : Bosses)
        if (IsValid(Boss) && !Boss->IsEnemyDead()) Result.Add(Boss);
    return Result;
}

int32 AHeliosAdventureDirector::GetEnemyCap() const
{
    if (const auto* Town=GetTownSubsystem()) return Town->GetSurvivalEnemyCap();
    if (IsUrbanChallenge() && UrbanSchedule.Waves.IsValidIndex(UrbanWaveIndex))
        return UrbanSchedule.Waves[UrbanWaveIndex].AliveCap;
    return FMath::Clamp(4 + FMath::FloorToInt(SurvivalSeconds / 120.0f), 4, 8);
}

TArray<UHeliosWeaponDefinition*> AHeliosAdventureDirector::GetAvailableWeapons() const
{
    return UHeliosWeaponRegistry::GetAvailableWeapons();
}

FString AHeliosAdventureDirector::GetRunPhase() const
{
    if (bSelectingWeapon) return TEXT("weapon_selection");
    if (IsUrbanChallenge())
        return bUrbanVictory ? TEXT("victory") : bRunOver ? TEXT("ended") :
            !UrbanConfigError.IsEmpty() ? TEXT("config_error") : UrbanRestRemaining > 0.f ? TEXT("intermission") : TEXT("active");
    if (const UHeliosTownSubsystem* Town = GetTownSubsystem()) return Town->IsWorldReady() ? (SurvivalSeconds<10.f?TEXT("preparing"):TEXT("active")) : TEXT("streaming");
    if (bRunOver) return TEXT("ended");
    return SurvivalSeconds < 10.0f ? TEXT("preparing") : TEXT("active");
}

void AHeliosAdventureDirector::EnterWeaponSelection()
{
    CaptureActiveLoadout();
    if(GetWorld())if(auto* Pets=GetWorld()->GetSubsystem<UHeliosPetWorld>())Pets->Clear();
    if (!GetWorld()) return;
    if (auto* Feedback=GetWorld()->GetSubsystem<UHeliosKillFeedbackSubsystem>()) Feedback->ResetRun();
    Player = Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
    if (Player){Player->CancelCombatAndControl();if(auto* F=Cast<AHeliosFighterWeapon>(Player->GetEquippedWeapon()))F->SuspendForArrival();}
    CleanupOwnedActors();
    bSelectingWeapon = true;
    bRunOver = false;
    if (IsTownMode()) UGameplayStatics::SetGamePaused(GetWorld(), false);
    else SetRunPaused(true);
}

bool AHeliosAdventureDirector::StartRun(FName WeaponId)
{
    FHeliosLoadout Loadout=SelectedLoadout;Loadout.PrimaryWeaponId=WeaponId;
    return StartRunWithLoadout(Loadout);
}

bool AHeliosAdventureDirector::CaptureActiveLoadout()
{
    // Old demo fixtures deliberately assign SelectedWeaponId before redeployment.
    if (bSelectingWeapon || SelectedLoadout.PrimaryWeaponId!=SelectedWeaponId ||
        !IsValid(Player) || !IsValid(Player->GetEquippedWeapon())) return false;
    const FHeliosLoadout Runtime=Player->GetLoadout();
    FString Error;
    if (!FHeliosLoadout::Validate(Runtime,Error) || Runtime.CharacterId==TEXT("sword_dog")) return false;
    SelectedLoadout=Runtime;
    SelectedWeaponId=Runtime.PrimaryWeaponId;
    return true;
}

bool AHeliosAdventureDirector::StartRunWithLoadout(const FHeliosLoadout& Loadout)
{
    FString Error;
    const UHeliosWeaponDefinition* Definition=UHeliosWeaponRegistry::FindWeapon(Loadout.PrimaryWeaponId);
    if (!bSelectingWeapon || !FHeliosLoadout::Validate(Loadout,Error) || Loadout.CharacterId==TEXT("sword_dog") || !Definition || !Definition->HasModel()) return false;
    SelectedLoadout=FHeliosLoadout::ResolveLegacyVariant(Loadout,GetGameInstance()); SelectedWeaponId=SelectedLoadout.PrimaryWeaponId;
    RestartRun();
    return !bSelectingWeapon;
}

void AHeliosAdventureDirector::RestartRun()
{
    if (!GetWorld()) return;
    CaptureActiveLoadout();
    if (SelectedLoadout.PrimaryWeaponId!=SelectedWeaponId) SelectedLoadout.PrimaryWeaponId=SelectedWeaponId;
    SelectedLoadout=FHeliosLoadout::ResolveLegacyVariant(SelectedLoadout,GetGameInstance());
    if (auto* Feedback=GetWorld()->GetSubsystem<UHeliosKillFeedbackSubsystem>()) Feedback->ResetRun();
    if (UHeliosTownSubsystem* Town = GetTownSubsystem())
    {
        UGameplayStatics::SetGamePaused(GetWorld(), false);
        for(TActorIterator<AHeliosColaProjectile> It(GetWorld());It;++It) It->Destroy();
        for(TActorIterator<AHeliosColaSplash> It(GetWorld());It;++It) It->Destroy();
        UHeliosWeaponVFX::ClearWorld(this);
        Player = Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
        if (!Player) return;
        if(auto* Qing=Player->GetQingYaAbility())Qing->ResetRun();
        if (!Player->EquipLoadout(SelectedLoadout)) return;
        ++RunSerial;
        Town->StartTownWithLoadout(SelectedLoadout);
        return;
    }
    UGameplayStatics::SetGamePaused(GetWorld(), false);
    CleanupOwnedActors();

    Player = Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
    if (Player)
    {
        Player->CancelCombatAndControl();
        if(auto* Qing=Player->GetQingYaAbility())Qing->ResetRun();
        if (!Player->EquipLoadout(SelectedLoadout))
        {
            EnterWeaponSelection();
            return;
        }
        Player->ResetForTest();
        if(auto* F=Cast<AHeliosFighterWeapon>(Player->GetEquippedWeapon());F&&!F->bActive){EnterWeaponSelection();return;}
        RunSpawnLocation = Player->GetActorLocation();
        if (AController* PC = Player->GetController()) PC->SetControlRotation(Player->GetActorRotation());
    }
    bSelectingWeapon = false;
    if (UHeliosControlSubsystem* Control = GetWorld()->GetSubsystem<UHeliosControlSubsystem>())
    {
        Control->Stop(UHeliosControlSubsystem::GetMainPlayerActorId());
    }

    ++RunSerial;
    SpawnSerial = 0;
    Score = 0;
    Kills = 0;
    BossKills = 0;
    BossEventSerial = 0;
    BossSpawnSerial = 0;
    PendingBossNumbers.Reset();
    SurvivalSeconds = 0.0f;
    NextEnemyAttemptSeconds = 10.0f;
    NextCollectibleSeconds = 90.0f;
    NextMedicalSeconds = 60.0f;
    BossIntervalSeconds = 60.0f;
    float RequestedBossInterval = 0.0f;
    if (FParse::Value(FCommandLine::Get(), TEXT("HeliosBossInterval="), RequestedBossInterval))
        BossIntervalSeconds = FMath::Max(1.0f, RequestedBossInterval);
    NextBossEventSeconds = BossIntervalSeconds;
    NextBossSpawnAttemptSeconds = 0.0f;
    bRunOver = false;
    CurrentZoneName = TEXT("未知区域");
    ExploredZones.Reset();
    CountedDeaths.Reset();
    HiddenFarSeconds.Reset();

    int32 RequestedSeed = 0;
    if (FParse::Value(FCommandLine::Get(), TEXT("HeliosAdventureSeed="), RequestedSeed))
    {
        Seed = RequestedSeed;
    }
    else
    {
        Seed = static_cast<int32>((FDateTime::UtcNow().GetTicks() ^ (RunSerial * 7919LL)) & 0x7fffffff);
    }
    Random.Initialize(Seed);
    CacheWorldActors();
    if (IsUrbanChallenge())
    {
        UrbanWaveIndex = UrbanRegularIssued = UrbanBossIssued = 0;
        UrbanSkippedWaves = 0;
        UrbanWaveStartKills = UrbanWaveStartBossKills = 0;
        UrbanRecoverySpawns=0; UrbanLastSpawnDiagnostics.Reset();
        UrbanRestRemaining = 10.f; UrbanSpawnRemaining = 0.f; bUrbanVictory = false;
        if (!UrbanSchedule.Load(UrbanConfigError))
            UE_LOG(LogHeliosOutpost, Error, TEXT("URBAN_WAVES %s"), *UrbanConfigError);
    }
    for (AHeliosZone* Zone : Zones)
    {
        if (!IsValid(Zone) || Zone->ZoneId.Equals(TEXT("exterior"), ESearchCase::IgnoreCase)) continue;
        SpawnPickup(false, Zone->ZoneId, true);
        SpawnPickup(true, Zone->ZoneId, true);
    }
    UE_LOG(LogHeliosOutpost, Display,
        TEXT("HELIOS_ADVENTURE restart run=%d seed=%d boss_interval=%.1f (use -HeliosBossInterval=60)"),
        RunSerial, Seed, BossIntervalSeconds);
}

void AHeliosAdventureDirector::EndRun()
{
    CaptureActiveLoadout();
    if (GetWorld()) if (auto* Feedback=GetWorld()->GetSubsystem<UHeliosKillFeedbackSubsystem>()) Feedback->ResetRun();
    if (UHeliosTownSubsystem* Town = GetTownSubsystem()) { Town->RespawnAtCheckpoint(); return; }
    if (bSelectingWeapon || bRunOver) return;
    bRunOver = true;
    const bool bNewBest = Score > BestScore || SurvivalSeconds > BestSeconds;
    BestScore = FMath::Max(BestScore, Score);
    BestSeconds = FMath::Max(BestSeconds, SurvivalSeconds);
    if (bNewBest && !IsUrbanChallenge()) SaveBest();
    SetRunPaused(true);
    UE_LOG(LogHeliosOutpost, Display,
        TEXT("HELIOS_ADVENTURE end run=%d score=%d seconds=%.1f kills=%d explored=%d"),
        RunSerial, Score, SurvivalSeconds, Kills, ExploredZones.Num());
}

void AHeliosAdventureDirector::SetRunPaused(bool bPaused)
{
    if (!GetWorld()) return;
    if(bPaused)if(auto* Pets=GetWorld()->GetSubsystem<UHeliosPetWorld>())for(auto& Pet:Pets->Pets)if(IsValid(Pet))Pet->SuspendCombat();
    if (bPaused) if (auto* Feedback=GetWorld()->GetSubsystem<UHeliosKillFeedbackSubsystem>()) Feedback->SuspendAudio();
    if (!bPaused && (bSelectingWeapon || bRunOver)) return;
    if (bPaused && GetWorld()->GetGameInstance())
        if (auto* Bridge = GetWorld()->GetGameInstance()->GetSubsystem<UHeliosCodexMCPSubsystem>())
            Bridge->RevokeControl(Player && Player->IsDead() ? TEXT("dead") : TEXT("paused"));
    if (Player)
    {
        if (auto* L=Cast<AHeliosLightstickWeapon>(Player->GetEquippedWeapon()); L && bPaused) L->SuspendInput();
        else Player->CancelCombatAndControl();
    }
    UGameplayStatics::SetGamePaused(GetWorld(), bPaused);
}

bool AHeliosAdventureDirector::TryCollect(bool bMedical)
{
    if (bSelectingWeapon || bRunOver || !IsValid(Player) || UGameplayStatics::IsGamePaused(this)) return false;
    if (bMedical)
    {
        const float ActualHeal=Player->Heal(30.0f);
        if (ActualHeal <= 0.0f) return false;
        if(IsUrbanChallenge() && FParse::Param(FCommandLine::Get(),TEXT("HeliosUrbanQA")))
            UE_LOG(LogHeliosOutpost,Display,TEXT("URBAN_MEDICAL actual=%.2f health=%.2f player=%s"),ActualHeal,Player->GetHealth(),*Player->GetActorLocation().ToString());
        NextMedicalSeconds = FMath::Max(NextMedicalSeconds, SurvivalSeconds + 60.0f);
        PlayPlayerCue(TEXT("Pickup"));
    }
    else
    {
        Score += 25;
        NextCollectibleSeconds = FMath::Max(NextCollectibleSeconds, SurvivalSeconds + 90.0f);
        PlayPlayerCue(TEXT("Pickup"));
    }
    return true;
}

FString AHeliosAdventureDirector::GetStateJson() const
{
    if (const UHeliosTownSubsystem* Town = GetTownSubsystem()) return Town->GetStateJson();
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetBoolField(TEXT("ok"), true);
    Json->SetNumberField(TEXT("score"), Score);
    Json->SetNumberField(TEXT("kills"), Kills);
    Json->SetNumberField(TEXT("boss_count"), GetBossCount());
    Json->SetNumberField(TEXT("boss_kills"), BossKills);
    Json->SetNumberField(TEXT("pending_boss_count"), GetPendingBossCount());
    Json->SetNumberField(TEXT("next_boss_seconds"), GetSecondsUntilNextBoss());
    Json->SetNumberField(TEXT("explored"), ExploredZones.Num());
    Json->SetNumberField(TEXT("alive"), GetAliveCount());
    Json->SetNumberField(TEXT("enemy_cap"), GetEnemyCap());
    if (IsUrbanChallenge())
    {
        Json->SetNumberField(TEXT("wave"), UrbanWaveIndex + 1);
        Json->SetNumberField(TEXT("wave_count"), 30);
        Json->SetBoolField(TEXT("victory"), bUrbanVictory);
        Json->SetNumberField(TEXT("skipped_waves"), UrbanSkippedWaves);
        Json->SetBoolField(TEXT("can_skip_wave"), CanSkipUrbanWave());
        Json->SetBoolField(TEXT("development_preview"), true);
        Json->SetStringField(TEXT("wave_status"), GetUrbanWaveText());
        Json->SetStringField(TEXT("config_error"), UrbanConfigError);
    }
    Json->SetNumberField(TEXT("seed"), Seed);
    Json->SetNumberField(TEXT("survival_seconds"), SurvivalSeconds);
    Json->SetNumberField(TEXT("best_score"), BestScore);
    Json->SetNumberField(TEXT("best_seconds"), BestSeconds);
    Json->SetStringField(TEXT("zone"), CurrentZoneName);
    Json->SetBoolField(TEXT("run_over"), bRunOver);
    Json->SetBoolField(TEXT("paused"), GetWorld() && UGameplayStatics::IsGamePaused(GetWorld()));
    Json->SetStringField(TEXT("phase"), GetRunPhase());
    HeliosLoadout::WriteJson(Json,SelectedLoadout);
    Json->SetNumberField(TEXT("ammo"), Player ? Player->GetAmmo() : 0);
    Json->SetNumberField(TEXT("magazine_size"), Player ? Player->GetMagazineSize() : 0);
    TArray<TSharedPtr<FJsonValue>> BossJson;
    for (const AHeliosEnemyBoss* Boss : Bosses)
    {
        if (!IsValid(Boss) || Boss->IsEnemyDead()) continue;
        TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetNumberField(TEXT("number"), Boss->GetBossNumber());
        Entry->SetNumberField(TEXT("health"), Boss->GetHealth());
        Entry->SetNumberField(TEXT("phase"), Boss->GetBossPhase());
        Entry->SetNumberField(TEXT("attack_type"), static_cast<int32>(Boss->GetBossAttackType()));
        Entry->SetNumberField(TEXT("windup"), Boss->GetBossWindupProgress());
        Entry->SetBoolField(TEXT("releasing"), Boss->IsBossAttackReleasing());
        Entry->SetNumberField(TEXT("cast_serial"), Boss->GetBossCastSerial());
        Entry->SetNumberField(TEXT("wave_radius"), Boss->GetBossWaveRadius());
        const FVector Location = Boss->GetActorLocation();
        TSharedRef<FJsonObject> Position = MakeShared<FJsonObject>();
        Position->SetNumberField(TEXT("x"), Location.X);
        Position->SetNumberField(TEXT("y"), Location.Y);
        Position->SetNumberField(TEXT("z"), Location.Z);
        Entry->SetObjectField(TEXT("position"), Position);
        BossJson.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Json->SetArrayField(TEXT("bosses"), BossJson);
    FString Output;
    FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Output));
    return Output;
}

void AHeliosAdventureDirector::CacheWorldActors()
{
    EnemyMarkers.Reset();
    SupplyMarkers.Reset();
    PickupMarkers.Reset();
    Zones.Reset();
    for (TActorIterator<AHeliosMarker> It(GetWorld()); It; ++It)
    {
        if (It->MarkerType.Equals(TEXT("enemy"), ESearchCase::IgnoreCase)) EnemyMarkers.Add(*It);
        if (It->MarkerType.Equals(TEXT("supply"), ESearchCase::IgnoreCase)) SupplyMarkers.Add(*It);
        if (It->MarkerType.Equals(TEXT("supply"), ESearchCase::IgnoreCase)
            || It->MarkerType.Equals(TEXT("patrol"), ESearchCase::IgnoreCase)
            || It->MarkerType.Equals(TEXT("cover"), ESearchCase::IgnoreCase)) PickupMarkers.Add(*It);
    }
    for (TActorIterator<AHeliosZone> It(GetWorld()); It; ++It) Zones.Add(*It);
    EnemyMarkers.Sort([](const AHeliosMarker& A, const AHeliosMarker& B) { return A.MarkerId < B.MarkerId; });
    SupplyMarkers.Sort([](const AHeliosMarker& A, const AHeliosMarker& B) { return A.MarkerId < B.MarkerId; });
    PickupMarkers.Sort([](const AHeliosMarker& A, const AHeliosMarker& B) { return A.MarkerId < B.MarkerId; });
}

void AHeliosAdventureDirector::UpdateEnemies(float DeltaSeconds)
{
    const bool UrbanAlarm=IsUrbanChallenge() &&
        FMath::FloorToInt(SurvivalSeconds/2.f)!=FMath::FloorToInt((SurvivalSeconds-DeltaSeconds)/2.f);
    for (auto It = CountedDeaths.CreateIterator(); It; ++It)
        if (!It->IsValid()) It.RemoveCurrent();
    for (auto It = HiddenFarSeconds.CreateIterator(); It; ++It)
        if (!It.Key().IsValid()) It.RemoveCurrent();
    for (int32 Index = Enemies.Num() - 1; Index >= 0; --Index)
    {
        AHeliosEnemyActor* Enemy = Enemies[Index];
        if (!IsValid(Enemy))
        {
            Enemies.RemoveAtSwap(Index);
            continue;
        }
        if (Enemy->IsEnemyDead())
        {
            if (!CountedDeaths.Contains(Enemy))
            {
                CountedDeaths.Add(Enemy);
                ++Kills;
                Score += 10;
                Enemy->SetLifeSpan(8.0f);
            }
            continue;
        }
        if(UrbanAlarm && Player)
            if(auto* AI=Cast<AShooterAIController>(Enemy->GetController()))
                if(!IsValid(AI->GetCurrentTarget()) && !AI->IsInvestigating())
                    AI->ReportAwarenessThreat(Player,Player->GetActorLocation(),false);
        if (Player && FVector::Dist2D(Player->GetActorLocation(), Enemy->GetActorLocation()) > ReclaimDistance
            && !IsCandidateVisible(Enemy->GetActorLocation(), Enemy))
        {
            float& HiddenSeconds = HiddenFarSeconds.FindOrAdd(Enemy);
            HiddenSeconds += DeltaSeconds;
            if (HiddenSeconds >= ReclaimHiddenSeconds)
            {
                DestroyEnemy(Enemy);
                Enemies.RemoveAtSwap(Index);
            }
        }
        else
        {
            HiddenFarSeconds.Remove(Enemy);
        }
    }
}

void AHeliosAdventureDirector::UpdateBosses()
{
    for (auto It = CountedBossDeaths.CreateIterator(); It; ++It)
        if (!It->IsValid()) It.RemoveCurrent();
    for (int32 Index = Bosses.Num() - 1; Index >= 0; --Index)
    {
        AHeliosEnemyBoss* Boss = Bosses[Index];
        if (!IsValid(Boss))
        {
            Bosses.RemoveAtSwap(Index);
            continue;
        }
        if (Boss->IsEnemyDead() && !CountedBossDeaths.Contains(Boss))
        {
            CountedBossDeaths.Add(Boss);
            ++Kills;
            ++BossKills;
            Score += 250;
            Boss->SetLifeSpan(8.0f);
        }
    }
}

void AHeliosAdventureDirector::UpdateZone()
{
    if (!Player) return;
    AHeliosZone* BestZone = nullptr;
    double SmallestVolume = TNumericLimits<double>::Max();
    for (AHeliosZone* Zone : Zones)
    {
        const float PlayerFeetZ = Player->GetActorLocation().Z
            - (Player->GetCapsuleComponent() ? Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.0f);
        if (IsValid(Zone)
            && (!Zone->ZoneId.Equals(TEXT("underground"), ESearchCase::IgnoreCase) || PlayerFeetZ < -150.0f)
            && Zone->ContainsWorldLocation(Player->GetGameplayReference())
            && Zone->GetBoundsVolume() < SmallestVolume)
        {
            BestZone = Zone;
            SmallestVolume = Zone->GetBoundsVolume();
        }
    }
    if (!BestZone) return;
    CurrentZoneName = BestZone->DisplayName.IsEmpty() ? BestZone->ZoneId : BestZone->DisplayName;
    if (BestZone->ZoneId.Equals(TEXT("exterior"), ESearchCase::IgnoreCase)) return;
    if (!ExploredZones.Contains(BestZone->ZoneId))
    {
        ExploredZones.Add(BestZone->ZoneId);
        Score += 50;
        PlayPlayerCue(TEXT("Explore"));
    }
}

void AHeliosAdventureDirector::UpdatePickups()
{
    Pickups.RemoveAll([](const TObjectPtr<AHeliosAdventurePickup>& Pickup) { return !IsValid(Pickup); });
    auto CountType = [this](bool bMedical)
    {
        int32 Count = 0;
        for (const AHeliosAdventurePickup* Pickup : Pickups)
            if (IsValid(Pickup) && Pickup->IsMedical() == bMedical) ++Count;
        return Count;
    };
    if (SurvivalSeconds >= NextMedicalSeconds && CountType(true) < 7)
    {
        if (SpawnPickup(true)) NextMedicalSeconds = SurvivalSeconds + 60.0f;
        else NextMedicalSeconds = SurvivalSeconds + 8.0f;
    }
    if (SurvivalSeconds >= NextCollectibleSeconds && CountType(false) < 7)
    {
        if (SpawnPickup(false)) NextCollectibleSeconds = SurvivalSeconds + 90.0f;
        else NextCollectibleSeconds = SurvivalSeconds + 8.0f;
    }
}

bool AHeliosAdventureDirector::TrySpawnEnemy(bool bAllowVisible, AHeliosMarker* OnlyMarker, EHeliosEnemyType ForcedType)
{
    if (bAllowVisible && !IsUrbanChallenge()) return false;
    if (!Player || GetAliveCount() >= GetEnemyCap() || EnemyMarkers.IsEmpty()) return false;
    int32 BasketballAlive = 0;
    for (const AHeliosEnemyActor* Enemy : Enemies)
        if (IsValid(Enemy) && !Enemy->IsEnemyDead()
            && Enemy->EnemyType == EHeliosEnemyType::E04_Basketball) ++BasketballAlive;

    TArray<int32> MarkerOrder;
    for (int32 Index = 0; Index < EnemyMarkers.Num(); ++Index) MarkerOrder.Add(Index);
    for (int32 Index = MarkerOrder.Num() - 1; Index > 0; --Index)
        MarkerOrder.Swap(Index, Random.RandRange(0, Index));

    struct FSpawnChoice { AHeliosMarker* Marker = nullptr; EHeliosEnemyType Type = EHeliosEnemyType::E01_Rifle; FVector Location; bool bPreferred = false; };
    TArray<FSpawnChoice> Choices;
    TMap<FString,int32> Rejections;
    for (const int32 MarkerIndex : MarkerOrder)
    {
        if (OnlyMarker && EnemyMarkers[MarkerIndex]!=OnlyMarker) continue;
        EHeliosEnemyType Type = IsUrbanChallenge() ? ChooseUrbanRegularType() : static_cast<EHeliosEnemyType>(Random.RandRange(1, 4));
        if (OnlyMarker) Type=ForcedType;
        if (Type == EHeliosEnemyType::E04_Basketball && BasketballAlive >= 2)
            Type = static_cast<EHeliosEnemyType>(Random.RandRange(1, 3));
        FVector SpawnLocation;
        AHeliosMarker* Marker = EnemyMarkers[MarkerIndex];
        FString Failure;
        if (!IsValid(Marker)) { ++Rejections.FindOrAdd(TEXT("invalid_marker")); continue; }
        if (!IsSpawnCandidateValid(*Marker, Type, SpawnLocation, bAllowVisible, &Failure))
        { ++Rejections.FindOrAdd(Failure); continue; }
        FSpawnChoice& Choice = Choices.AddDefaulted_GetRef();
        Choice.Marker = Marker;
        Choice.Type = Type;
        Choice.Location = SpawnLocation;
        const float Distance = FVector::Dist2D(Player->GetActorLocation(), Marker->GetActorLocation());
        Choice.bPreferred = Distance <= 6000.0f;
    }
    if (Choices.IsEmpty())
    {
        if (IsUrbanChallenge())
        {
            TArray<FString> Reasons;
            for (const auto& Pair:Rejections) Reasons.Add(FString::Printf(TEXT("%s=%d"),*Pair.Key,Pair.Value));
            Reasons.Sort(); UrbanLastSpawnDiagnostics=FString::Join(Reasons,TEXT(","));
        }
        UE_LOG(LogHeliosOutpost, Verbose, TEXT("HELIOS_ADVENTURE spawn skipped: no safe hidden reachable marker"));
        return false;
    }
    TArray<int32> Preferred;
    for (int32 Index = 0; Index < Choices.Num(); ++Index) if (Choices[Index].bPreferred) Preferred.Add(Index);
    const int32 ChoiceIndex = Preferred.IsEmpty()
        ? Random.RandRange(0, Choices.Num() - 1)
        : Preferred[Random.RandRange(0, Preferred.Num() - 1)];
    const FSpawnChoice& Choice = Choices[ChoiceIndex];
    AHeliosMarker* Marker = Choice.Marker;
    const EHeliosEnemyType Type = Choice.Type;
    FVector FinalLocation;
    if (!IsSpawnCandidateValid(*Marker, Type, FinalLocation, bAllowVisible)) return false;
    const FTransform Transform(Marker->GetActorRotation(), FinalLocation);
        AHeliosEnemyActor* Enemy = GetWorld()->SpawnActorDeferred<AHeliosEnemyActor>(
            ClassForType(Type), Transform, this, nullptr,
            ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding);
        if (!Enemy) return false;
        if (FFloatProperty* DestructionProperty = FindFProperty<FFloatProperty>(
            AShooterNPC::StaticClass(), TEXT("DeferredDestructionTime")))
        {
            DestructionProperty->SetPropertyValue_InContainer(Enemy, 8.0f);
        }
        ++SpawnSerial;
        const FString Id = FString::Printf(TEXT("adventure_enemy_r%03d_s%03d_%s"),
            RunSerial, SpawnSerial, *FHeliosEnemyDefinition::ForType(Type).TypeId);
        Enemy->SetRuntimeIdentity(Id, Marker->MarkerId);
        Enemy->FinishSpawning(Transform);
        Enemies.Add(Enemy);
        if (IsUrbanChallenge())
            if (auto* AI=Cast<AShooterAIController>(Enemy->GetController()))
                AI->ReportAwarenessThreat(Player,Player->GetActorLocation(),false);
    return true;
}

bool AHeliosAdventureDirector::TrySpawnPendingBoss(bool bAllowVisible, AHeliosMarker* OnlyMarker)
{
    if (bAllowVisible && !IsUrbanChallenge()) return false;
    if (IsUrbanChallenge() && !PendingBossNumbers.IsEmpty() && PendingBossNumbers[0]==30) return TrySpawnUrbanFinalBoss();
    if (!Player || PendingBossNumbers.IsEmpty() || EnemyMarkers.IsEmpty()) return false;
    TArray<int32> MarkerOrder;
    for (int32 Index = 0; Index < EnemyMarkers.Num(); ++Index) MarkerOrder.Add(Index);
    for (int32 Index = MarkerOrder.Num() - 1; Index > 0; --Index)
        MarkerOrder.Swap(Index, Random.RandRange(0, Index));
    for (const int32 MarkerIndex : MarkerOrder)
    {
        AHeliosMarker* Marker = EnemyMarkers[MarkerIndex];
        if (OnlyMarker && Marker!=OnlyMarker) continue;
        FVector SpawnLocation;
        if (!IsValid(Marker) || !IsBossSpawnCandidateValid(*Marker, SpawnLocation, bAllowVisible)) continue;
        const FTransform Transform(Marker->GetActorRotation(), SpawnLocation);
        AHeliosEnemyBoss* Boss = GetWorld()->SpawnActorDeferred<AHeliosEnemyBoss>(
            AHeliosEnemyBoss::StaticClass(), Transform, this, nullptr,
            ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding);
        if (!Boss) continue;
        const int32 Number = PendingBossNumbers[0];
        Boss->SetBossNumber(Number);
        if (IsUrbanChallenge()) Boss->SkipEntranceForUrbanMidboss();
        Boss->SetRuntimeIdentity(FString::Printf(TEXT("adventure_boss_r%03d_b%03d_E05"), RunSerial, Number),
            Marker->MarkerId);
        Boss->FinishSpawning(Transform);
        Bosses.Add(Boss);
        PendingBossNumbers.RemoveAt(0);
        ++BossSpawnSerial;
        UE_LOG(LogHeliosOutpost, Display, TEXT("HELIOS_BOSS spawned number=%d marker=%s"),
            Number, *Marker->MarkerId);
        return true;
    }
    return false;
}

bool AHeliosAdventureDirector::SpawnPickup(bool bMedical, const FString& PreferredZone, bool bAllowVisible)
{
    if (!Player || PickupMarkers.IsEmpty()) return false;
    TSet<FString> Occupied;
    for (const AHeliosAdventurePickup* Pickup : Pickups)
        if (IsValid(Pickup)) Occupied.Add(Pickup->GetSourceMarkerId());

    TArray<AHeliosMarker*> Candidates;
    for (AHeliosMarker* Marker : PickupMarkers)
    {
        if (!IsValid(Marker) || Occupied.Contains(Marker->MarkerId)) continue;
        if (IsUrbanChallenge() && bMedical != Marker->ZoneId.Equals(TEXT("supply"),ESearchCase::IgnoreCase)) continue;
        if (!PreferredZone.IsEmpty() && !Marker->ZoneId.Equals(PreferredZone, ESearchCase::IgnoreCase)) continue;
        UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
        FNavLocation Projected;
        if (!Navigation || !Navigation->ProjectPointToNavigation(
            Marker->GetActorLocation() + FVector(0, 0, 30), Projected, FVector(100, 100, 170))) continue;
        const bool bNearby = FVector::Dist2D(Player->GetActorLocation(), Marker->GetActorLocation()) < 2000.0f;
        if (!bAllowVisible && bNearby && IsCandidateVisible(Marker->GetActorLocation() + FVector(0, 0, 60))) continue;
        Candidates.Add(Marker);
    }
    if (Candidates.IsEmpty()) return false;
    AHeliosMarker* Marker = Candidates[Random.RandRange(0, Candidates.Num() - 1)];
    FActorSpawnParameters Params;
    Params.Owner = this;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AHeliosAdventurePickup* Pickup = GetWorld()->SpawnActor<AHeliosAdventurePickup>(
        AHeliosAdventurePickup::StaticClass(), Marker->GetActorLocation() + FVector(0, 0, 65),
        Marker->GetActorRotation(), Params);
    if (!Pickup) return false;
    Pickup->Configure(this, bMedical, Marker->MarkerId);
    Pickups.Add(Pickup);
    return true;
}

bool AHeliosAdventureDirector::IsCandidateVisible(const FVector& Location, const AActor* Candidate) const
{
    if (!Player || !GetWorld()) return false;
    FVector EyeLocation;
    FRotator EyeRotation;
    Player->GetActorEyesViewPoint(EyeLocation, EyeRotation);
    if(Player->IsMechMode())EyeLocation=Player->GetGroundReference()+FVector(0,0,160);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(HeliosAdventureVisibility), true, Player);
    Params.AddIgnoredActor(this);
    FHitResult Hit;
    const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, EyeLocation, Location,
        ECC_Visibility, Params);
    return !bBlocked || (Candidate && Hit.GetActor() == Candidate);
}

bool AHeliosAdventureDirector::IsSpawnCandidateValid(const AHeliosMarker& Marker,
    EHeliosEnemyType Type, FVector& OutLocation, bool bAllowVisible, FString* Failure) const
{
    const auto Reject=[Failure](const TCHAR* Reason) { if(Failure)*Failure=Reason; return false; };
    if (bAllowVisible && !IsUrbanChallenge()) return Reject(TEXT("visibility_bypass_not_urban"));
    if (!Player || !GetWorld()) return Reject(TEXT("no_player_world"));
    const FVector MarkerLocation = Marker.GetActorLocation();
    if (FVector::Dist2D(Player->GetActorLocation(), MarkerLocation) < MinimumPlayerDistance
        || FVector::Dist2D(RunSpawnLocation, MarkerLocation) < MinimumRunSpawnDistance) return Reject(TEXT("minimum_distance"));

    const FHeliosEnemyDefinition Definition = FHeliosEnemyDefinition::ForType(Type);
    UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!Navigation) return Reject(TEXT("no_navigation"));
    FNavLocation NavLocation;
    AActor* PathContext = Player;
    if (Type == EHeliosEnemyType::E05_Boss)
    {
        FNavAgentProperties BossProps(Definition.CapsuleRadius, Definition.CapsuleHalfHeight * 2.0f);
        BossProps.AgentStepHeight = 40.0f;
        ANavigationData* BossNavigation = Navigation->GetNavDataForProps(BossProps, MarkerLocation);
        if (!BossNavigation || !BossNavigation->GetConfig().IsEquivalent(BossProps)) return Reject(TEXT("boss_nav_agent"));
        if (!Navigation->ProjectPointToNavigation(MarkerLocation + FVector(0, 0, 30), NavLocation,
            FVector(120, 120, 180), BossNavigation)) return Reject(TEXT("marker_nav_projection"));
        PathContext = BossNavigation;
    }
    else if (!Navigation->ProjectPointToNavigation(MarkerLocation + FVector(0, 0, 30), NavLocation,
        FVector(120, 120, 180))) return Reject(TEXT("marker_nav_projection"));
    FVector PathStart=Player->GetNavAgentLocation();
    if (IsUrbanChallenge())
    {
        FNavLocation PlayerNav;
        if (!Navigation->ProjectPointToNavigation(PathStart,PlayerNav,FVector(500,500,250)))
            return Reject(TEXT("player_nav_projection"));
        if (FVector::Dist2D(PathStart,PlayerNav.Location)>500) return Reject(TEXT("player_too_far_off_nav"));
        PathStart=PlayerNav.Location;
    }
    UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
        GetWorld(), PathStart, NavLocation.Location, PathContext);
    if (!Path || !Path->IsValid() || Path->IsPartial()) return Reject(TEXT("unreachable_path"));
    OutLocation = NavLocation.Location + FVector(0, 0, Definition.CapsuleHalfHeight + 2.0f);
    if (!bAllowVisible && IsCandidateVisible(OutLocation)) return Reject(TEXT("visible"));

    FCollisionQueryParams Params(SCENE_QUERY_STAT(HeliosAdventureClearance), false, Player);
    Params.AddIgnoredActor(this);
    if (GetWorld()->OverlapBlockingTestByProfile(OutLocation, FQuat::Identity, TEXT("Pawn"),
        FCollisionShape::MakeCapsule(Definition.CapsuleRadius, Definition.CapsuleHalfHeight), Params))
        return Reject(TEXT("capsule_blocked"));
    return true;
}

bool AHeliosAdventureDirector::IsBossSpawnCandidateValid(const AHeliosMarker& Marker,
    FVector& OutLocation, bool bAllowVisible) const
{
    // Bosses obey the same distance, visibility, nav-path and capsule-clearance
    // safeguards as ordinary enemies, but live in their separate collection.
    if (!Player || FVector::Dist2D(Player->GetActorLocation(), Marker.GetActorLocation()) > 6000.0f)
        return false;
    return IsSpawnCandidateValid(Marker, EHeliosEnemyType::E05_Boss, OutLocation, bAllowVisible);
}

TSubclassOf<AHeliosEnemyActor> AHeliosAdventureDirector::ClassForType(EHeliosEnemyType Type) const
{
    switch (Type)
    {
    case EHeliosEnemyType::E02_Charger: return AHeliosEnemyE02::StaticClass();
    case EHeliosEnemyType::E03_Skirmisher: return AHeliosEnemyE03::StaticClass();
    case EHeliosEnemyType::E04_Basketball: return AHeliosEnemyE04::StaticClass();
    default: return AHeliosEnemyE01::StaticClass();
    }
}

void AHeliosAdventureDirector::DestroyEnemy(AHeliosEnemyActor* Enemy)
{
    if (!IsValid(Enemy)) return;
    Enemy->StopShooting();
    if (AController* Controller = Enemy->GetController())
    {
        if (AAIController* AI = Cast<AAIController>(Controller))
        {
            AI->StopMovement();
            if (AI->GetBrainComponent()) AI->GetBrainComponent()->StopLogic(TEXT("Adventure cleanup"));
        }
        Controller->UnPossess();
        Controller->Destroy();
    }
    Enemy->Destroy();
}

void AHeliosAdventureDirector::CleanupOwnedActors()
{
    if (IsUrbanChallenge()) ClearUrbanSpawnRecovery();
    if (!GetWorld())
    {
        Enemies.Reset(); Pickups.Reset(); return;
    }
    UHeliosWeaponVFX::ClearWorld(this);
    TSet<const AActor*> EnemySet;
    for (AHeliosEnemyActor* Enemy : Enemies) if (IsValid(Enemy)) EnemySet.Add(Enemy);
    for (AHeliosEnemyBoss* Boss : Bosses) if (IsValid(Boss)) EnemySet.Add(Boss);
    for (TActorIterator<AHeliosBasketballProjectile> It(GetWorld()); It; ++It) It->Destroy();
    for (TActorIterator<AShooterProjectile> It(GetWorld()); It; ++It) It->Destroy();
    for (TActorIterator<AHeliosColaSplash> It(GetWorld()); It; ++It) It->Destroy();
    for (TActorIterator<AHeliosExplosionEffect> It(GetWorld()); It; ++It) It->Destroy();
    for (TActorIterator<AShooterWeapon> It(GetWorld()); It; ++It)
        if (EnemySet.Contains(It->GetOwner())) It->Destroy();
    for (AHeliosEnemyActor* Enemy : Enemies) DestroyEnemy(Enemy);
    for (AHeliosEnemyBoss* Boss : Bosses) DestroyEnemy(Boss);
    for (AHeliosAdventurePickup* Pickup : Pickups) if (IsValid(Pickup)) Pickup->Destroy();
    Enemies.Reset(); Bosses.Reset(); Pickups.Reset(); CountedDeaths.Reset(); CountedBossDeaths.Reset();
    HiddenFarSeconds.Reset(); PendingBossNumbers.Reset(); NextBossSpawnAttemptSeconds = 0.0f;
}

void AHeliosAdventureDirector::LoadBest()
{
    FString Text;
    if (!FPaths::FileExists(BestFilePath())) return;
    if (!FFileHelper::LoadFileToString(Text, *BestFilePath()))
    {
        UE_LOG(LogHeliosOutpost, Warning, TEXT("HELIOS_ADVENTURE unable to read best file"));
        return;
    }
    TSharedPtr<FJsonObject> Json;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json)
    {
        UE_LOG(LogHeliosOutpost, Warning, TEXT("HELIOS_ADVENTURE invalid best file"));
        return;
    }
    BestScore = static_cast<int32>(Json->GetNumberField(TEXT("best_score")));
    BestSeconds = Json->GetNumberField(TEXT("best_seconds"));
}

void AHeliosAdventureDirector::SaveBest() const
{
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(BestFilePath()), true);
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("best_score"), BestScore);
    Json->SetNumberField(TEXT("best_seconds"), BestSeconds);
    FString Text;
    FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text));
    if (!FFileHelper::SaveStringToFile(Text, *BestFilePath(),
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        UE_LOG(LogHeliosOutpost, Warning, TEXT("HELIOS_ADVENTURE unable to save best file: %s"), *BestFilePath());
    }
}

void AHeliosAdventureDirector::PlayPlayerCue(FName Cue) const
{
    if (AHeliosAdventureCharacter* AdventurePlayer = Cast<AHeliosAdventureCharacter>(Player))
        AdventurePlayer->PlayAdventureCue(Cue);
}
