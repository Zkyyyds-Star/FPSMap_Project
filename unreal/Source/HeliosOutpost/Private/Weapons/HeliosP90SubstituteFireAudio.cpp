#include "Weapons/HeliosP90SubstituteFireAudio.h"
#include "Audio/HeliosAudio.h"
#include "Pets/HeliosPetDog.h"
#include "Weapons/HeliosBuLianShiP90.h"
#include "Components/AudioComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundConcurrency.h"

UHeliosP90SubstituteFireAudio::UHeliosP90SubstituteFireAudio()
{PrimaryComponentTick.bCanEverTick=true;}
void UHeliosP90SubstituteFireAudio::Initialize(bool Spatial,float ShotInterval)
{
    bSpatial=Spatial;
    const TCHAR* Path=Spatial?TEXT("/Game/Helios/CFMBuLianShiP90/Audio/SFX_P90_Substitute_3P_15855")
                            :TEXT("/Game/Helios/CFMBuLianShiP90/Audio/SFX_P90_Substitute_1P_15854");
    Sound=LoadObject<USoundWave>(nullptr,Path);
    Gain=HeliosBuLianShiP90::Setting(Spatial,TEXT("枪声音量"),Spatial?.5f:.45f,0,1);
    Concurrency=NewObject<USoundConcurrency>(GetWorld());
    // Enough voices for every original one-second tail, even at configured rates.
    VoiceLimit=FMath::Clamp(FMath::CeilToInt((Sound?Sound->GetDuration():1.f)/FMath::Max(.03f,ShotInterval))+3,4,40);
    Concurrency->Concurrency.MaxCount=VoiceLimit;
    Concurrency->Concurrency.ResolutionRule=EMaxConcurrentResolutionRule::PreventNew;
    if(Spatial)
    {
        Attenuation=NewObject<USoundAttenuation>(GetWorld());
        auto& A=Attenuation->Attenuation;A.bSpatialize=true;A.bAttenuate=true;
        A.DistanceAlgorithm=EAttenuationDistanceModel::Linear;
        A.AttenuationShapeExtents=FVector(200,0,0);A.FalloffDistance=3300; // 2 m full volume, 35 m outer radius.
    }
    if(!Sound||Sound->bLooping)UE_LOG(LogTemp,Error,TEXT("BLS_P90_AUDIO invalid ordinary-P90 one-shot source %s"),Path);
    UE_LOG(LogTemp,Display,TEXT("BLS_P90_AUDIO_CONFIG source=%s substitute=ordinary_P90 event=%d media=%d duration=%.3f gain=%.3f limit=%d spatial=%d outer_cm=%d"),
        Spatial?TEXT("pet"):TEXT("player"),Spatial?15855:15854,Spatial?1019025687:368229487,
        Sound?Sound->GetDuration():0,Gain,VoiceLimit,Spatial,Spatial?3500:0);
}
int32 UHeliosP90SubstituteFireAudio::PlayingCount()const
{int32 Count=0;for(const auto& C:Channels)if(C.IsValid()&&C->IsPlaying())++Count;return Count;}
void UHeliosP90SubstituteFireAudio::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Function)
{Super::TickComponent(Dt,Type,Function);Channels.RemoveAll([](const auto& C){return !C.IsValid()||!C->IsPlaying();});}
void UHeliosP90SubstituteFireAudio::PlayShot(const FVector& Muzzle)
{
    ++TriggeredShots;
    // World-owned one-shots finish their legitimate tails after trigger release,
    // reload, switch, death or destruction. There is no audio timer/loop to cancel.
    UAudioComponent* C=nullptr;
    const auto* PetOwner=Cast<AHeliosPetCompanion>(GetOwner());
    const float ShotGain=Gain*(PetOwner?PetOwner->RosterAudioGain():1.f);
    if(Sound&&!Sound->bLooping&&Gain>0)
        C=bSpatial?HeliosAudio::SpawnSoundAtLocation(this,Sound,Muzzle,FRotator::ZeroRotator,ShotGain,1,0,Attenuation,Concurrency,true,true)
                  :HeliosAudio::SpawnSound2D(this,Sound,Gain,1,0,Concurrency,false,true,true);
    if(C){Channels.Add(C);++StartedVoices;PeakVoices=FMath::Max(PeakVoices,PlayingCount());}
    UE_LOG(LogTemp,Display,TEXT("BLS_P90_AUDIO source=%s event=%d trigger=%d started=%d active=%d origin=%s"),
        bSpatial?TEXT("pet"):TEXT("player"),bSpatial?15855:15854,TriggeredShots,StartedVoices,PlayingCount(),*Muzzle.ToCompactString());
}
