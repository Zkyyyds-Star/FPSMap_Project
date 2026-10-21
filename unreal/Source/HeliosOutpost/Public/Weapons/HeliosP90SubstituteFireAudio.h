#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HeliosP90SubstituteFireAudio.generated.h"

// Explicitly approved ordinary-P90 substitution, separate from original action/voice sounds.
UCLASS()
class HELIOSOUTPOST_API UHeliosP90SubstituteFireAudio : public UActorComponent
{
    GENERATED_BODY()
public:
    UHeliosP90SubstituteFireAudio();
    void Initialize(bool Spatial,float ShotInterval);
    void PlayShot(const FVector& Muzzle);
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)override;
    int32 PlayingCount()const;
    TArray<TWeakObjectPtr<class UAudioComponent>> PlayingChannels()const{return Channels;}
    int32 TriggeredShots=0,StartedVoices=0,PeakVoices=0,VoiceLimit=0;
    float Gain=0;
    bool bSpatial=false;
    UPROPERTY(Transient) TObjectPtr<class USoundWave> Sound;
    UPROPERTY(Transient) TObjectPtr<class USoundAttenuation> Attenuation;
private:
    UPROPERTY(Transient) TObjectPtr<class USoundConcurrency> Concurrency;
    TArray<TWeakObjectPtr<class UAudioComponent>> Channels;
};
