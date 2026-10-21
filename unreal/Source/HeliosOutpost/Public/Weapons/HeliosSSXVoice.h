#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HeliosSSXVoice.generated.h"

UCLASS()
class HELIOSOUTPOST_API UHeliosSSXVoiceComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UHeliosSSXVoiceComponent();
    void Initialize(bool Spatial);
    void InitializeSources(bool Spatial,const TMap<FName,class USoundBase*>& Cues,const TArray<class USoundBase*>& KillPool);
    void InitializeEventSources(bool Spatial,const TMap<FName,TArray<class USoundBase*>>& Events);
    bool PlayOriginalEvent(FName Event,FName PriorityCue);
    bool PlaySource(FName Cue,class USoundBase* Sound);
    bool Play(FName Cue);
    void Stop();
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    int32 GetKillPoolSize() const { return KillSounds.Num(); }
    int32 GetPendingKillCount() const { return PendingKillRequests.Num(); }
    int32 GetLastPlayedKillIndex() const { return LastKillIndex; }
    bool IsVoicePlaying() const;
    int32 KillCount=0,HurtCount=0,DeathCount=0;
private:
    friend class AHeliosQingYaQA;
    friend class FHeliosSSXVoiceTest;
    friend class AHeliosSSXDemo;
    class USoundBase* NextKillSound();
    bool StartSound(class USoundBase* Sound,FName Cue);
    void StopChannel();
    void DrainKillQueue();
    void AudioFinished(class UAudioComponent* FinishedChannel);
    void SetupAttenuation(bool Spatial);
    bool bSpatial=false;
    float LastHurt=-100000.f;
    int32 Priority=0;
    int32 LastKillIndex=INDEX_NONE,KillRequestSerial=0;
    TArray<int32> KillBag,PendingKillRequests;
    TMap<FName,TArray<int32>> OriginalEventIndices;
    UPROPERTY(Transient) TArray<TObjectPtr<class USoundBase>> OriginalEventSounds;
    UPROPERTY(Transient) TArray<TObjectPtr<class USoundBase>> PendingOriginalKills;
    UPROPERTY(Transient) TArray<TObjectPtr<class USoundBase>> KillSounds;
    UPROPERTY(Transient) TMap<FName,TObjectPtr<class USoundBase>> Sounds;
    UPROPERTY(Transient) TObjectPtr<class UAudioComponent> Channel;
    UPROPERTY(Transient) TObjectPtr<class USoundAttenuation> Attenuation;
};
