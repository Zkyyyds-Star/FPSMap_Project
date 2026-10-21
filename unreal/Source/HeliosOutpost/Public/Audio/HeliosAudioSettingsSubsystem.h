#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "HeliosAudioSettingsSubsystem.generated.h"

class UAudioComponent; class USoundBase; class USoundClass; class USoundMix;
enum class EHeliosAudioBus : uint8 { Master, Music, Weapon, KillFeedback, PlayerHurt, Companion, CharacterVoice, Environment, Interface, Count };

/** Runtime sound-class routing; settings share the existing per-user Game.ini. */
UCLASS(Config=Game)
class HELIOSOUTPOST_API UHeliosAudioSettingsSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    static UHeliosAudioSettingsSubsystem* Get(const UObject* Context);
    static const TCHAR* Label(EHeliosAudioBus Bus);
    float Volume(EHeliosAudioBus Bus) const;
    bool Enabled(EHeliosAudioBus Bus) const;
    float EffectiveGain(EHeliosAudioBus Bus) const;
    void SetVolume(EHeliosAudioBus Bus,float Value);
    void SetEnabled(EHeliosAudioBus Bus,bool Value);
    void ResetDefaults();
    void FlushSettings();
    void Route(UAudioComponent* Component,const UObject* Context,USoundBase* Sound);
    void RouteTo(UAudioComponent* Component,EHeliosAudioBus Bus);
    EHeliosAudioBus Classify(const UObject* Context,const USoundBase* Sound) const;
    USoundClass* BusClass(EHeliosAudioBus Bus) const;
private:
    bool Tick(float Delta);
    void Apply();
    void Defaults();
    UPROPERTY(Config) TArray<float> Volumes;
    UPROPERTY(Config) TArray<bool> Switches;
    UPROPERTY(Transient) TArray<TObjectPtr<USoundClass>> Classes;
    UPROPERTY(Transient) TObjectPtr<USoundMix> Mix;
    TSet<TWeakObjectPtr<USoundBase>> ScannedSounds;
    TWeakObjectPtr<UWorld> AppliedWorld;
    FTSTicker::FDelegateHandle Ticker;
    double SaveAfter=0, ScanAfter=0;
    float DefaultMusic=.2f;
    bool Dirty=false;
};
