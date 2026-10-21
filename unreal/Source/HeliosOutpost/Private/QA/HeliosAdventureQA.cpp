#include "QA/HeliosAdventureQA.h"

#include "Adventure/HeliosAdventureDirector.h"
#include "Adventure/HeliosAdventurePickup.h"
#include "Player/HeliosArenaCharacter.h"
#include "Enemies/HeliosEnemyActor.h"
#include "HeliosOutpost.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

AHeliosAdventureQA::AHeliosAdventureQA()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    Tags.Add(TEXT("HeliosAdventureQA"));
}

void AHeliosAdventureQA::BeginPlay()
{
    Super::BeginPlay();
    FParse::Value(FCommandLine::Get(), TEXT("HeliosAdventureQA="), Mode);
    if (Mode.IsEmpty()) Mode = TEXT("all");
    StartWallSeconds = FPlatformTime::Seconds();
    StageWallSeconds = StartWallSeconds;
    Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("mode"), Mode);
    Report->SetStringField(TEXT("scope"), TEXT("compact_playability_acceptance"));
    Report->SetStringField(TEXT("generated_utc"), FDateTime::UtcNow().ToIso8601());
    Report->SetBoolField(TEXT("test_instrumentation_used"), true);
    Report->SetBoolField(TEXT("endurance_or_performance_long_test_run"), false);
}

void AHeliosAdventureQA::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bFinished) return;
    const double Now = FPlatformTime::Seconds();
    const double StageAge = Now - StageWallSeconds;

    if (!Director.IsValid()) Director = AHeliosAdventureDirector::Find(GetWorld());
    if (!Player.IsValid()) Player = Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));

    switch (Stage)
    {
    case 0:
        if (!Director.IsValid() || !Player.IsValid())
        {
            if (Now - StartWallSeconds > 8.0)
            {
                Check(TEXT("runtime_ready"), false, TEXT("director_or_player_missing"));
                Finish();
            }
            return;
        }
        Check(TEXT("runtime_ready"), true);
        Stage = 1; StageWallSeconds = Now;
        break;
    case 1:
        // Natural director timing is preserved: preparation lasts 10 seconds and attempts occur every 8 seconds.
        if (Director->GetAliveCount() > 0)
        {
            Check(TEXT("natural_enemy_spawn"), true,
                FString::Printf(TEXT("alive=%d seconds=%.1f"), Director->GetAliveCount(), Director->GetSurvivalSeconds()));
            AHeliosEnemyActor* Target = nullptr;
            for (AHeliosEnemyActor* Enemy : Director->Enemies)
                if (IsValid(Enemy) && !Enemy->IsEnemyDead()) { Target = Enemy; break; }
            if (Target)
                UGameplayStatics::ApplyDamage(Target, 10000.0f, Player->GetController(), Player.Get(), UDamageType::StaticClass());
            Stage = 2; StageWallSeconds = Now;
        }
        else if (StageAge > 30.0)
        {
            Check(TEXT("natural_enemy_spawn"), false, TEXT("no valid spawn after preparation and three attempts"));
            Stage = 2; StageWallSeconds = Now;
        }
        break;
    case 2:
        if (Director->GetKills() > 0 || StageAge > 2.0)
        {
            Check(TEXT("enemy_kill_scores"), Director->GetKills() > 0 && Director->GetScore() >= 10,
                FString::Printf(TEXT("kills=%d score=%d"), Director->GetKills(), Director->GetScore()));
            if (AHeliosAdventurePickup* Pickup = FindPickup(false))
            {
                bCollectibleLocated = true;
                ScoreBeforePickup = Director->GetScore();
                PendingPickup = Pickup;
                Player->SetActorLocation(Pickup->GetActorLocation(), false, nullptr, ETeleportType::TeleportPhysics);
            }
            Stage = 3; StageWallSeconds = Now;
        }
        break;
    case 3:
        if (StageAge >= 0.5)
        {
            Check(TEXT("collectible_pickup"), bCollectibleLocated && !PendingPickup.IsValid()
                && Director->GetScore() >= ScoreBeforePickup + 25,
                FString::Printf(TEXT("located=%s score_before=%d score_after=%d consumed=%s"),
                    bCollectibleLocated ? TEXT("true") : TEXT("false"), ScoreBeforePickup,
                    Director->GetScore(), PendingPickup.IsValid() ? TEXT("false") : TEXT("true")));
            UGameplayStatics::ApplyDamage(Player.Get(), 20.0f, nullptr, this, UDamageType::StaticClass());
            HealthBeforeMedical = Player->GetHealth();
            if (AHeliosAdventurePickup* Pickup = FindPickup(true))
            {
                bMedicalLocated = true;
                PendingPickup = Pickup;
                Player->SetActorLocation(Pickup->GetActorLocation(), false, nullptr, ETeleportType::TeleportPhysics);
            }
            Stage = 4; StageWallSeconds = Now;
        }
        break;
    case 4:
        if (StageAge >= 0.5)
        {
            Check(TEXT("medical_pickup"), bMedicalLocated && !PendingPickup.IsValid()
                && Player->GetHealth() > HealthBeforeMedical,
                FString::Printf(TEXT("located=%s health_before=%.1f health_after=%.1f consumed=%s"),
                    bMedicalLocated ? TEXT("true") : TEXT("false"), HealthBeforeMedical,
                    Player->GetHealth(), PendingPickup.IsValid() ? TEXT("false") : TEXT("true")));
            Director->SetRunPaused(true);
            const bool bPaused = UGameplayStatics::IsGamePaused(this);
            Director->SetRunPaused(false);
            Check(TEXT("pause_resume"), bPaused && !UGameplayStatics::IsGamePaused(this));
            UGameplayStatics::ApplyDamage(Player.Get(), 10000.0f, nullptr, this, UDamageType::StaticClass());
            Stage = 5; StageWallSeconds = Now;
        }
        break;
    case 5:
        if (Director->IsRunOver() || StageAge > 2.0)
        {
            Check(TEXT("death_ends_run"), Director->IsRunOver() && Player->IsDead());
            Director->RestartRun(); ++ResetCount;
            Stage = 6; StageWallSeconds = Now;
        }
        break;
    case 6:
        if (StageAge >= 0.5)
        {
            Director->RestartRun(); ++ResetCount;
            Stage = 7; StageWallSeconds = Now;
        }
        break;
    case 7:
        if (StageAge >= 0.5)
        {
            Director->RestartRun(); ++ResetCount;
            Check(TEXT("three_restarts"), ResetCount == 3 && !Director->IsRunOver() && !Player->IsDead(),
                FString::Printf(TEXT("resets=%d"), ResetCount));
            Stage = 8; StageWallSeconds = Now;
        }
        break;
    case 8:
        if (Director->GetAliveCount() > 0 || StageAge > 30.0)
        {
            Check(TEXT("restart_spawns_again"), Director->GetAliveCount() > 0,
                FString::Printf(TEXT("alive=%d seconds=%.1f"), Director->GetAliveCount(), Director->GetSurvivalSeconds()));
            Finish();
        }
        break;
    default:
        break;
    }
}

AHeliosAdventurePickup* AHeliosAdventureQA::FindPickup(bool bMedical) const
{
    for (TActorIterator<AHeliosAdventurePickup> It(GetWorld()); It; ++It)
        if (It->IsMedical() == bMedical) return *It;
    return nullptr;
}

void AHeliosAdventureQA::Check(const FString& Name, bool bPassed, const FString& Detail)
{
    TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("name"), Name);
    Item->SetBoolField(TEXT("passed"), bPassed);
    Item->SetStringField(TEXT("detail"), Detail);
    Checks.Add(MakeShared<FJsonValueObject>(Item));
    if (!bPassed) ++FailureCount;
    UE_LOG(LogHeliosOutpost, Display, TEXT("HELIOS_ADVENTURE_QA check=%s passed=%s detail=%s"),
        *Name, bPassed ? TEXT("true") : TEXT("false"), *Detail);
}

void AHeliosAdventureQA::Finish()
{
    if (bFinished) return;
    bFinished = true;
    const double WallSeconds = FPlatformTime::Seconds() - StartWallSeconds;
    Report->SetArrayField(TEXT("checks"), Checks);
    Report->SetNumberField(TEXT("failures"), FailureCount);
    Report->SetNumberField(TEXT("wall_seconds"), WallSeconds);
    Report->SetNumberField(TEXT("restart_count"), ResetCount);
    Report->SetStringField(TEXT("status"), FailureCount == 0 ? TEXT("passed") : TEXT("failed"));
    if (Director.IsValid()) Report->SetStringField(TEXT("final_state"), Director->GetStateJson());

    FString Output;
    FJsonSerializer::Serialize(Report.ToSharedRef(), TJsonWriterFactory<>::Create(&Output));
    const FString Directory = FPaths::ConvertRelativePathToFull(
        FPaths::Combine(FPaths::ProjectDir(), TEXT("../qa/adventure")));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString OutputFile = FPaths::Combine(Directory,
        FString::Printf(TEXT("runtime_adventure_qa_%s.json"), *Mode));
    const bool bWritten = FFileHelper::SaveStringToFile(Output, *OutputFile,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogHeliosOutpost, Display, TEXT("HELIOS_ADVENTURE_QA finished status=%s report=%s written=%s"),
        FailureCount == 0 ? TEXT("PASS") : TEXT("FAIL"), *OutputFile, bWritten ? TEXT("true") : TEXT("false"));

    if (!FParse::Param(FCommandLine::Get(), TEXT("HeliosAdventureQANoExit")))
        FPlatformMisc::RequestExit(false);
    Destroy();
}
