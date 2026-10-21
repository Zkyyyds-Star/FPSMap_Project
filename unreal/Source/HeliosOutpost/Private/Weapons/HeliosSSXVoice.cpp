#include "Weapons/HeliosSSXVoice.h"
#include "Audio/HeliosAudio.h"
#include "Pets/HeliosPetDog.h"
#include "Sound/SoundConcurrency.h"
#include "Combat/HeliosKillFeedbackSubsystem.h"
#include "Components/AudioComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"

UHeliosSSXVoiceComponent::UHeliosSSXVoiceComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bStartWithTickEnabled=false;
}

void UHeliosSSXVoiceComponent::Initialize(bool Spatial)
{
    Stop();Sounds.Reset();KillSounds.Reset();KillBag.Reset();OriginalEventIndices.Reset();OriginalEventSounds.Reset();LastKillIndex=INDEX_NONE;KillRequestSerial=0;
    bSpatial=Spatial;
    for(FName Cue:{FName(TEXT("Kill")),FName(TEXT("Hurt")),FName(TEXT("Death"))})
        Sounds.Add(Cue,LoadObject<USoundBase>(nullptr,*(TEXT("/Game/Helios/CFMSunShangXiang/Audio/VO_SSX_")+Cue.ToString())));
    if(auto* Sound=Sounds.FindRef(TEXT("Kill")).Get())KillSounds.Add(Sound);
    for(int32 Level=2;Level<=8;++Level)
    {
        const FString Path=FString::Printf(TEXT("/Game/Helios/CFMSunShangXiang/Audio/VO_SSX_KillMulti%d"),Level);
        if(auto* Sound=LoadObject<USoundBase>(nullptr,*Path))KillSounds.Add(Sound);
        else UE_LOG(LogTemp,Error,TEXT("SSX missing original kill voice: %s"),*Path);
    }
    SetupAttenuation(Spatial);
}
void UHeliosSSXVoiceComponent::InitializeSources(bool Spatial,const TMap<FName,USoundBase*>& Cues,const TArray<USoundBase*>& KillPool)
{
    Stop();Sounds.Reset();KillSounds.Reset();KillBag.Reset();OriginalEventIndices.Reset();OriginalEventSounds.Reset();LastKillIndex=INDEX_NONE;KillRequestSerial=0;
    for(const auto& Cue:Cues)Sounds.Add(Cue.Key,Cue.Value);
    for(auto* Sound:KillPool)if(Sound)KillSounds.Add(Sound);
    SetupAttenuation(Spatial);
}
void UHeliosSSXVoiceComponent::InitializeEventSources(bool Spatial,const TMap<FName,TArray<USoundBase*>>& Events)
{
    InitializeSources(Spatial,{},{});
    for(const auto& Event:Events)for(auto* Sound:Event.Value)if(Sound)
        OriginalEventIndices.FindOrAdd(Event.Key).Add(OriginalEventSounds.Add(Sound));
}
bool UHeliosSSXVoiceComponent::PlayOriginalEvent(FName Event,FName PriorityCue)
{
    const auto* Indices=OriginalEventIndices.Find(Event);
    if(!Indices||Indices->IsEmpty()||!GetWorld()||GetWorld()->IsPaused())return false;
    auto* Sound=OriginalEventSounds[(*Indices)[FMath::RandRange(0,Indices->Num()-1)]].Get();
    if(PriorityCue!=TEXT("Kill"))return PlaySource(PriorityCue,Sound);
    if(IsVoicePlaying()&&Priority>2)return false;
    if(PendingKillRequests.Num()==2)
    {PendingKillRequests.RemoveAt(0,1,EAllowShrinking::No);PendingOriginalKills.RemoveAt(0,1,EAllowShrinking::No);}
    PendingKillRequests.Add(++KillRequestSerial);PendingOriginalKills.Add(Sound);SetComponentTickEnabled(true);
    if(!IsVoicePlaying()||Priority<2)DrainKillQueue();return true;
}
bool UHeliosSSXVoiceComponent::PlaySource(FName Cue,USoundBase* Sound)
{if(!Sound)return false;Sounds.Add(Cue,Sound);return Play(Cue);}
void UHeliosSSXVoiceComponent::SetupAttenuation(bool Spatial)
{
    bSpatial=Spatial;Attenuation=nullptr;
    if(Spatial)
    {
        Attenuation=NewObject<USoundAttenuation>(this);
        auto& S=Attenuation->Attenuation;
        S.bSpatialize=true;S.bAttenuate=true;S.DistanceAlgorithm=EAttenuationDistanceModel::Linear;
        S.AttenuationShapeExtents=FVector(150,0,0);S.FalloffDistance=450;
    }
}

USoundBase* UHeliosSSXVoiceComponent::NextKillSound()
{
    if(KillSounds.IsEmpty())return nullptr;
    if(KillBag.IsEmpty())
    {
        for(int32 Index=0;Index<KillSounds.Num();++Index)KillBag.Add(Index);
        for(int32 Index=KillBag.Num()-1;Index>0;--Index)KillBag.Swap(Index,FMath::RandRange(0,Index));
        // Pop from the end, keeping the boundary between shuffle rounds distinct.
        if(KillBag.Num()>1&&KillBag.Last()==LastKillIndex)KillBag.Swap(KillBag.Num()-1,FMath::RandRange(0,KillBag.Num()-2));
    }
    LastKillIndex=KillBag.Pop(EAllowShrinking::No);
    return KillSounds[LastKillIndex];
}

bool UHeliosSSXVoiceComponent::IsVoicePlaying() const
{return IsValid(Channel)&&Channel->IsPlaying();}

bool UHeliosSSXVoiceComponent::Play(FName Cue)
{
    if(!GetWorld()||GetWorld()->IsPaused())return false;
    if(Cue==TEXT("Kill"))
    {
        if(KillSounds.IsEmpty()||(IsVoicePlaying()&&Priority>2))return false;
        // Queue requests, not random selections; dropped requests consume no shuffle entries.
        if(PendingKillRequests.Num()==2)PendingKillRequests.RemoveAt(0,1,EAllowShrinking::No);
        PendingKillRequests.Add(++KillRequestSerial);
        SetComponentTickEnabled(true);
        if(!IsVoicePlaying()||Priority<2)DrainKillQueue();
        return true;
    }
    USoundBase* Sound=Sounds.FindRef(Cue);if(!Sound)return false;
    const float Now=GetWorld()->GetTimeSeconds();
    const int32 Incoming=Cue==TEXT("Death")?3:Cue==TEXT("Kill")?2:1;
    if(Cue==TEXT("Hurt")&&Now-LastHurt<1.f)return false;
    if(IsVoicePlaying()&&Incoming<=Priority)return false;
    if(Cue==TEXT("Hurt")&&!PendingKillRequests.IsEmpty())return false;
    if(Cue==TEXT("Death"))Stop();
    return StartSound(Sound,Cue);
}

bool UHeliosSSXVoiceComponent::StartSound(USoundBase* Sound,FName Cue)
{
    if(!Sound||!GetWorld()||GetWorld()->IsPaused()||!IsValid(GetOwner()))return false;
    StopChannel();
    const auto* PetOwner=Cast<AHeliosPetCompanion>(GetOwner());
    const float VoiceGain=PetOwner?PetOwner->RosterAudioGain():1.f;
    auto* Roster=GetWorld()->GetSubsystem<UHeliosPetWorld>();
    USoundConcurrency* Limit=PetOwner&&PetOwner->RosterSize>1&&Roster?Roster->GetVoiceConcurrency():nullptr;
    Channel=bSpatial?HeliosAudio::SpawnSoundAttached(Sound,GetOwner()->GetRootComponent(),NAME_None,FVector::ZeroVector,EAttachLocation::KeepRelativeOffset,true,.75f*VoiceGain,1.f,0.f,Attenuation,Limit,true)
                    :HeliosAudio::SpawnSound2D(this,Sound,.8f,1.f,0.f,nullptr,false,true);
    if(!Channel)return false;
    Priority=Cue==TEXT("Death")?3:Cue==TEXT("Kill")?2:1;
    if(Priority==2) SetComponentTickEnabled(true);
    Channel->OnAudioFinishedNative.AddUObject(this,&UHeliosSSXVoiceComponent::AudioFinished);
    if(Cue==TEXT("Hurt")){LastHurt=GetWorld()->GetTimeSeconds();++HurtCount;}
    else if(Cue==TEXT("Kill"))++KillCount;
    else if(Cue==TEXT("Death"))++DeathCount;
    return true;
}

void UHeliosSSXVoiceComponent::DrainKillQueue()
{
    if(!GetWorld()||GetWorld()->IsPaused()||PendingKillRequests.IsEmpty()||(IsVoicePlaying()&&Priority>=2))return;
    const int32 Request=PendingKillRequests[0];
    PendingKillRequests.RemoveAt(0,1,EAllowShrinking::No);
    const int32 PreviousIndex=LastKillIndex;
    const bool Original=!PendingOriginalKills.IsEmpty();
    USoundBase* Sound=Original?PendingOriginalKills[0].Get():NextKillSound();
    if(Original)PendingOriginalKills.RemoveAt(0,1,EAllowShrinking::No);
    if(StartSound(Sound,TEXT("Kill")))
    {
        UE_LOG(LogTemp,Display,TEXT("SSX_KILL_VOICE owner=%s request=%d asset=%s pending=%d"),*GetNameSafe(GetOwner()),Request,*GetNameSafe(Sound),PendingKillRequests.Num());
    }
    else
    {
        if(!Original&&LastKillIndex!=INDEX_NONE)KillBag.Add(LastKillIndex);
        LastKillIndex=PreviousIndex;
        UE_LOG(LogTemp,Warning,TEXT("SSX kill voice could not start for %s"),*GetNameSafe(GetOwner()));
    }
    SetComponentTickEnabled(IsVoicePlaying() || !PendingKillRequests.IsEmpty());
}

void UHeliosSSXVoiceComponent::AudioFinished(UAudioComponent* FinishedChannel)
{
    if(Channel!=FinishedChannel)return;
    FinishedChannel->OnAudioFinishedNative.RemoveAll(this);
    Channel=nullptr;Priority=0;
    // Start on the next unpaused game tick, outside the audio completion callback.
    SetComponentTickEnabled(!PendingKillRequests.IsEmpty());
}

void UHeliosSSXVoiceComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);DrainKillQueue();
    if(Priority==2 && IsValid(Channel))
        if(auto* Feedback=GetWorld()->GetSubsystem<UHeliosKillFeedbackSubsystem>())
            Channel->SetVolumeMultiplier((bSpatial?.75f:.8f)*Feedback->OtherVoiceGain()*(Cast<AHeliosPetCompanion>(GetOwner())?Cast<AHeliosPetCompanion>(GetOwner())->RosterAudioGain():1.f));
}

void UHeliosSSXVoiceComponent::StopChannel()
{
    if(IsValid(Channel)){Channel->OnAudioFinishedNative.RemoveAll(this);Channel->Stop();}
    Channel=nullptr;Priority=0;
}
void UHeliosSSXVoiceComponent::Stop()
{PendingKillRequests.Reset();PendingOriginalKills.Reset();SetComponentTickEnabled(false);StopChannel();}
void UHeliosSSXVoiceComponent::EndPlay(const EEndPlayReason::Type Reason){Stop();Super::EndPlay(Reason);}
