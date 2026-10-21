#include "Audio/HeliosMusicSubsystem.h"
#include "Audio/HeliosAudio.h"
#include "QA/HeliosMusicQA.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "HeliosOutpost.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWave.h"

void UHeliosMusicSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    MusicVolume = FMath::Clamp(MusicVolume, 0.f, 1.f);
    TickHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UHeliosMusicSubsystem::Tick), 0.05f);
}

void UHeliosMusicSubsystem::EnsureStarted()
{
    if (bRequested || bShuttingDown || !GetWorld() || GetWorld()->IsNetMode(NM_DedicatedServer)) return;
    bRequested = true;
    StartHeliosMusicQA(this);
    // Require the complete playlist: missing songs must not silently become a single-song loop.
    for (const TSoftObjectPtr<USoundWave>& Track : Tracks)
    {
        USoundWave* Wave = Track.LoadSynchronous();
        if (!Wave || Wave->GetDuration() <= 0.f || Wave->bLooping)
        {
            UE_LOG(LogHeliosOutpost, Warning, TEXT("HELIOS_MUSIC missing_or_invalid_track=%s; playlist disabled"), *Track.ToString());
            LoadedTracks.Empty();
            return;
        }
        LoadedTracks.Add(Wave);
    }
    if (LoadedTracks.Num() != 2)
    {
        UE_LOG(LogHeliosOutpost, Warning, TEXT("HELIOS_MUSIC requires both songs; found=%d"), LoadedTracks.Num());
        LoadedTracks.Empty();
        return;
    }
    bPendingPlay = true;
}

bool UHeliosMusicSubsystem::Tick(float DeltaSeconds)
{
    // Core ticker continues while the world is paused. Defer completion until a valid
    // destination world exists, rather than spawning a player inside an audio callback.
    UWorld* World = GetWorld();
    if (!bShuttingDown && bPendingPlay && World && World->HasBegunPlay()
        && !World->bIsTearingDown && FPlatformTime::Seconds() >= RetryAt)
    {
        if (PlayCurrentTrack()) bPendingPlay = false;
        else RetryAt = FPlatformTime::Seconds() + 2.;
    }
    return true;
}

bool UHeliosMusicSubsystem::PlayCurrentTrack()
{
    if (!LoadedTracks.IsValidIndex(CurrentTrack)) return false;
    USoundWave* Wave = LoadedTracks[CurrentTrack];
    if (!IsValid(MusicComponent))
    {
        // Persistent CreateSound2D has no world/actor owner, survives audio-device flush,
        // and is kept alive by this subsystem's UPROPERTY across world garbage collection.
        MusicComponent = HeliosAudio::CreateSound2D(this, Wave, 1.f, 1.f, 0.f, nullptr, true, false);
        if (!MusicComponent)
        {
            if (!bReportedNoAudio)
                UE_LOG(LogHeliosOutpost, Warning, TEXT("HELIOS_MUSIC audio device unavailable; waiting"));
            bReportedNoAudio = true;
            return false;
        }
        MusicComponent->OnAudioFinished.AddDynamic(this, &UHeliosMusicSubsystem::OnTrackFinished);
        MusicComponent->OnAudioPlaybackPercent.AddDynamic(this, &UHeliosMusicSubsystem::OnPlaybackPercent);
        MusicComponent->bIsMusic = true;
    }
    MusicComponent->SetSound(Wave);
    MusicComponent->SetVolumeMultiplier(1.f);
    PlaybackSeconds = 0.f;
    MusicComponent->Play();
    ++PlayCount;
    UE_LOG(LogHeliosOutpost, Display, TEXT("HELIOS_MUSIC play=%d track=%d name=%s duration=%.3f volume=%.2f component=%s"),
        PlayCount, CurrentTrack, *Wave->GetName(), Wave->GetDuration(), MusicVolume, *MusicComponent->GetPathName());
    return true;
}

void UHeliosMusicSubsystem::OnTrackFinished()
{
    if (bShuttingDown || bPendingPlay || LoadedTracks.IsEmpty()) return;
    CurrentTrack = (CurrentTrack + 1) % LoadedTracks.Num();
    bPendingPlay = true;
}

void UHeliosMusicSubsystem::OnPlaybackPercent(const USoundWave* Wave, float Percent)
{
    if (LoadedTracks.IsValidIndex(CurrentTrack) && Wave == LoadedTracks[CurrentTrack])
        PlaybackSeconds = FMath::Clamp(Percent, 0.f, 1.f) * Wave->GetDuration();
}

void UHeliosMusicSubsystem::Deinitialize()
{
    bShuttingDown = true;
    bPendingPlay = false;
    FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
    if (IsValid(MusicComponent))
    {
        MusicComponent->OnAudioFinished.RemoveAll(this);
        MusicComponent->OnAudioPlaybackPercent.RemoveAll(this);
        MusicComponent->Stop();
        MusicComponent->DestroyComponent();
        MusicComponent = nullptr;
    }
    LoadedTracks.Empty();
    UE_LOG(LogHeliosOutpost, Display, TEXT("HELIOS_MUSIC shutdown"));
    Super::Deinitialize();
}
