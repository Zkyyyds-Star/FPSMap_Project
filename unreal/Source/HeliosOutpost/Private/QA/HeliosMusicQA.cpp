#include "QA/HeliosMusicQA.h"
#include "Audio/HeliosMusicSubsystem.h"
#include "Audio/HeliosAudioSettingsSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "ActiveSound.h"
#include "AudioDevice.h"
#include "AudioThread.h"
#include "Components/AudioComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "UI/HeliosAdventureHUD.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundBase.h"

namespace
{
struct FMusicAcceptance
{
    TWeakObjectPtr<UHeliosMusicSubsystem> Music;
    TWeakObjectPtr<UAudioComponent> OriginalComponent;
    TArray<TSharedPtr<FJsonValue>> Checks;
    FString Output;
    double Started = FPlatformTime::Seconds(), Next = Started + 5.;
    int32 Stage = 0, Failures = 0, LastPlayCount = 0;
    float PreviousPosition = 0.f;
    bool bFullCycle = false;
    bool bGhostVillage = false;

    void Check(const TCHAR* Name, bool bPass)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("name"), Name);
        Row->SetBoolField(TEXT("passed"), bPass);
        Checks.Add(MakeShared<FJsonValueObject>(Row));
        if (!bPass) ++Failures;
        if (auto* M = Music.Get())
        {
            Row->SetNumberField(TEXT("position_seconds"), M->GetPlaybackSeconds());
            Row->SetNumberField(TEXT("previous_position_seconds"), PreviousPosition);
            Row->SetNumberField(TEXT("play_count"), M->GetPlayCount());
            if (auto* C = M->GetMusicComponent())
            {
                auto Device = M->GetWorld()->GetAudioDevice();
                const uint64 Id = C->GetAudioComponentID();
                FAudioThread::RunCommandOnAudioThread([Device, Id]() mutable
                {
                    if (Device.IsValid()) if (auto* Active = Device->FindActiveSound(Id))
                    {
                        UE_LOG(LogTemp, Display, TEXT("MUSIC_QA_AUDIO ui=%d paused=%d playback=%.3f waves=%d"),
                            Active->bIsUISound, Active->bIsPaused, Active->PlaybackTime, Active->GetWaveInstances().Num());
                        for (auto& Pair : Active->GetWaveInstances())
                        {
                            auto* Wave = Pair.Value;
                            auto* Source = Device->GetSoundSource(Wave);
                            UE_LOG(LogTemp, Display, TEXT("MUSIC_QA_SOURCE ui=%d paused=%d source=%d percent=%.4f source_paused=%d"),
                                Wave->bIsUISound, Wave->bIsPaused, Source != nullptr,
                                Source ? Source->GetPlaybackPercent() : -1.f, Source ? Source->IsPaused() : false);
                        }
                    }
                });
            }
        }
        UE_LOG(LogTemp, Display, TEXT("MUSIC_QA %s %s"), Name, bPass ? TEXT("PASS") : TEXT("FAIL"));
    }

    bool Finish()
    {
        auto Report = MakeShared<FJsonObject>();
        Report->SetStringField(TEXT("status"), Failures ? TEXT("failed") : TEXT("passed"));
        Report->SetBoolField(TEXT("full_natural_cycle"), bFullCycle);
        Report->SetNumberField(TEXT("failures"), Failures);
        Report->SetNumberField(TEXT("elapsed_seconds"), FPlatformTime::Seconds() - Started);
        Report->SetArrayField(TEXT("checks"), Checks);
        FString Json;
        FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output), true);
        FFileHelper::SaveStringToFile(Json, *Output, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
        return false;
    }

    bool Tick(float Delta)
    {
        auto* M = Music.Get();
        if (!M) return false;
        const double Now = FPlatformTime::Seconds();
        if (Now - Started > (bFullCycle ? 1000. : 240.)) { Check(TEXT("timeout"), false); return Finish(); }
        if (Now < Next) return true;
        auto* World = M->GetWorld();
        auto* C = M->GetMusicComponent();
        if (!World || !World->HasBegunPlay() || World->bIsTearingDown) return true;
        if (bGhostVillage && C && Now >= Next)
            Check(TEXT("travel_and_playlist_volume_twenty_percent"), FMath::IsNearlyEqual(C->VolumeMultiplier * UHeliosAudioSettingsSubsystem::Get(M)->EffectiveGain(EHeliosAudioBus::Music), .2f));
        if (!C || !C->IsPlaying())
        {
            if (Stage == 0 && Now - Started > 25.) { Check(TEXT("two_actual_songs_loaded_and_playing"), false); return Finish(); }
            return true;
        }
        auto* PC = World->GetFirstPlayerController();
        auto* H = PC ? Cast<AHeliosAdventureHUD>(PC->GetHUD()) : nullptr;
        auto* D = AHeliosAdventureDirector::Find(World);
        Next = Now + 2.;
        switch (Stage)
        {
        case 0:
            if (!H || !D) return true;
            OriginalComponent = C;
            Check(TEXT("menu_starts_first_song"), M->GetTrackIndex() == 0 && M->GetPlayCount() == 1 && H->GetMenuState() == EHeliosAdventureMenu::WeaponSelect);
            Check(TEXT("persistent_2d_music_at_twenty_percent"), C->bIsUISound && C->bIgnoreForFlushing && !C->bAllowSpatialization && FMath::IsNearlyEqual(C->VolumeMultiplier * UHeliosAudioSettingsSubsystem::Get(M)->EffectiveGain(EHeliosAudioBus::Music), .2f));
            PreviousPosition = M->GetPlaybackSeconds();
            M->EnsureStarted(); M->EnsureStarted();
            break;
        case 1:
            Check(TEXT("menu_progress_and_duplicate_start_guard"), M->GetPlayCount() == 1 && C == OriginalComponent.Get() && M->GetPlaybackSeconds() > PreviousPosition + .5f);
            if (!H || !D) return true;
            H->SelectWeapon(TEXT("rifle")); H->StartSelectedRun();
            if (PC->GetPawn()) PC->GetPawn()->SetCanBeDamaged(false);
            break;
        case 2:
            if (!H) return true;
            Check(TEXT("gameplay_keeps_same_player"), C == OriginalComponent.Get() && M->GetPlayCount() == 1 && H->GetMenuState() == EHeliosAdventureMenu::None);
            H->TogglePauseMenu(); PreviousPosition = M->GetPlaybackSeconds();
            break;
        case 3:
            if (!H || !D) return true;
            Check(TEXT("pause_menu_does_not_pause_music"), H->GetMenuState() == EHeliosAdventureMenu::Pause && UGameplayStatics::IsGamePaused(World) && M->GetPlaybackSeconds() > PreviousPosition + .5f);
            H->ContinueRun(); D->EndRun(); PreviousPosition = M->GetPlaybackSeconds();
            break;
        case 4:
            if (!H || !D) return true;
            Check(TEXT("death_keeps_music_progress"), D->IsRunOver() && H->GetMenuState() == EHeliosAdventureMenu::Death && M->GetPlaybackSeconds() > PreviousPosition + .5f);
            H->RequestRestartConfirmation(); H->ConfirmRestart();
            H->SelectWeapon(TEXT("rifle")); H->StartSelectedRun();
            PreviousPosition = M->GetPlaybackSeconds();
            break;
        case 5:
            Check(TEXT("restart_does_not_restart_song"), C == OriginalComponent.Get() && M->GetPlayCount() == 1 && M->GetPlaybackSeconds() > PreviousPosition + .5f);
            PreviousPosition = M->GetPlaybackSeconds();
            UGameplayStatics::SetGamePaused(World, false);
            UGameplayStatics::OpenLevel(World, TEXT("/Game/Helios/Maps/HeliosOutpost"));
            Next = Now + 5.;
            break;
        case 6:
            if (!World->GetMapName().Contains(TEXT("HeliosOutpost"))) return true;
            Check(TEXT("map_travel_survives_gc_with_same_audio"), C == OriginalComponent.Get() && M->GetPlayCount() == 1 && M->GetPlaybackSeconds() > PreviousPosition);
            PreviousPosition = M->GetPlaybackSeconds();
            UGameplayStatics::OpenLevel(World, bGhostVillage ? TEXT("/Game/Helios/Maps/IkunGhostVillage") : TEXT("/Game/Helios/Maps/HeliosAdventure"));
            Next = Now + 5.;
            break;
        case 7:
            if (!World->GetMapName().Contains(bGhostVillage ? TEXT("IkunGhostVillage") : TEXT("HeliosAdventure"))) return true;
            Check(TEXT("return_to_menu_keeps_song_position"), C == OriginalComponent.Get() && M->GetPlayCount() == 1 && M->GetPlaybackSeconds() > PreviousPosition);
            LastPlayCount = M->GetPlayCount();
            // Quick mode exercises the real decoder and completion callback near EOF.
            // Full mode leaves both complete recordings to end naturally.
            if (!bFullCycle) C->Play(FMath::Max(0.f, C->Sound->GetDuration() - 3.f));
            break;
        case 8:
            if (M->GetPlayCount() == LastPlayCount) return true;
            Check(TEXT("first_song_advances_to_second"), M->GetTrackIndex() == 1 && M->GetPlayCount() == 2 && C == OriginalComponent.Get());
            LastPlayCount = M->GetPlayCount();
            if (!bFullCycle) C->Play(FMath::Max(0.f, C->Sound->GetDuration() - 3.f));
            break;
        case 9:
            if (M->GetPlayCount() == LastPlayCount) return true;
            Check(TEXT("second_song_wraps_to_first"), M->GetTrackIndex() == 0 && M->GetPlayCount() == 3 && C == OriginalComponent.Get());
            if (bGhostVillage)
            {
                PreviousPosition = M->GetPlaybackSeconds();
                UGameplayStatics::OpenLevel(World, TEXT("/Game/Helios/Maps/HeliosAdventure"));
                Next = Now + 5.;
                break;
            }
            return Finish();
        case 10:
            if (!World->GetMapName().Contains(TEXT("HeliosAdventure"))) return true;
            Check(TEXT("leave_village_keeps_same_audio"), C == OriginalComponent.Get() && M->GetPlayCount() == 3 && M->GetPlaybackSeconds() > PreviousPosition);
            PreviousPosition = M->GetPlaybackSeconds();
            UGameplayStatics::OpenLevel(World, TEXT("/Game/Helios/Maps/IkunGhostVillage"));
            Next = Now + 5.;
            break;
        case 11:
            if (!World->GetMapName().Contains(TEXT("IkunGhostVillage"))) return true;
            Check(TEXT("reenter_village_keeps_same_audio"), C == OriginalComponent.Get() && M->GetPlayCount() == 3 && M->GetPlaybackSeconds() > PreviousPosition);
            return Finish();
        }
        ++Stage;
        return true;
    }
};
}
#endif

void StartHeliosMusicQA(UHeliosMusicSubsystem* Music)
{
#if WITH_DEV_AUTOMATION_TESTS
    if (!FParse::Param(FCommandLine::Get(), TEXT("HeliosMusicQA"))) return;
    auto Runner = MakeShared<FMusicAcceptance>();
    Runner->Music = Music;
    Runner->bFullCycle = FParse::Param(FCommandLine::Get(), TEXT("HeliosMusicFullCycle"));
    Runner->bGhostVillage = FParse::Param(FCommandLine::Get(), TEXT("HeliosMusicGhostVillage"));
    Runner->Output = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("MusicQA/report.json"));
    FParse::Value(FCommandLine::Get(), TEXT("HeliosMusicQAOutput="), Runner->Output);
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Runner](float Delta) { return Runner->Tick(Delta); }), .1f);
#endif
}
