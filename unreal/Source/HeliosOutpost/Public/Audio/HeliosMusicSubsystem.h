#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HeliosMusicSubsystem.generated.h"

class UAudioComponent;
class USoundWave;

/** One non-spatial playlist per game instance, including while paused and travelling. */
UCLASS(Config=Game)
class HELIOSOUTPOST_API UHeliosMusicSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    void EnsureStarted();

    int32 GetTrackIndex() const { return CurrentTrack; }
    int32 GetPlayCount() const { return PlayCount; }
    float GetPlaybackSeconds() const { return PlaybackSeconds; }
    UAudioComponent* GetMusicComponent() const { return MusicComponent; }

private:
    bool Tick(float DeltaSeconds);
    bool PlayCurrentTrack();
    UFUNCTION() void OnTrackFinished();
    UFUNCTION() void OnPlaybackPercent(const USoundWave* Wave, float Percent);

    UPROPERTY(Config) TArray<TSoftObjectPtr<USoundWave>> Tracks;
    UPROPERTY(Config) float MusicVolume = 0.2f;
    UPROPERTY(Transient) TArray<TObjectPtr<USoundWave>> LoadedTracks;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> MusicComponent;

    FTSTicker::FDelegateHandle TickHandle;
    int32 CurrentTrack = 0;
    int32 PlayCount = 0;
    float PlaybackSeconds = 0.f;
    double RetryAt = 0.;
    bool bRequested = false;
    bool bPendingPlay = false;
    bool bShuttingDown = false;
    bool bReportedNoAudio = false;
};
