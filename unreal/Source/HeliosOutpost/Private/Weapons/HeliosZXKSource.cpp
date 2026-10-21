#include "Audio/HeliosAudio.h"
#include "Weapons/HeliosZhongXiaoKui.h"
#include "Weapons/HeliosWeaponHolderContext.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Weapons/HeliosSSXVoice.h"
#include "Pets/HeliosPetDog.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
namespace
{
struct FSourceEvent {float Time;int32 Id;};
TWeakObjectPtr<UHeliosSSXEffectLibrary> AudioLibrary;
TMap<int32,TArray<FSoftObjectPath>> Sources;
TMap<FName,TArray<FSourceEvent>> Timelines;
TSet<int32> VoiceIds;
bool LoadSourceAudio()
{
    if(AudioLibrary.IsValid())return true;
    auto* Library=LoadObject<UHeliosSSXEffectLibrary>(nullptr,TEXT("/Game/Helios/CFMZhongXiaoKui/Audio/DA_ZXK_Audio.DA_ZXK_Audio"),nullptr,LOAD_Quiet);
    if(!Library)return false;
    TSharedPtr<FJsonObject> Root;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Library->Recipe),Root)||!Root)return false;
    Sources.Reset();Timelines.Reset();VoiceIds.Reset();
    for(const auto& Pair:Root->GetObjectField(TEXT("sounds"))->Values)
        for(const auto& Value:Pair.Value->AsArray())Sources.FindOrAdd(FCString::Atoi(*Pair.Key)).Add(FSoftObjectPath(Value->AsString()));
    for(const auto& Value:Root->GetArrayField(TEXT("voice_ids")))VoiceIds.Add(int32(Value->AsNumber()));
    for(const auto& Pair:Root->GetObjectField(TEXT("timelines"))->Values)
        for(const auto& Value:Pair.Value->AsArray()){const auto Event=Value->AsObject();Timelines.FindOrAdd(FName(Pair.Key)).Add({float(Event->GetNumberField(TEXT("time"))),int32(Event->GetNumberField(TEXT("id")))});}
    AudioLibrary=Library;return true;
}
}
namespace HeliosZXK
{
USoundBase* OriginalSound(int32 Id)
{
    if(!LoadSourceAudio())return nullptr;const auto* Variants=Sources.Find(Id);if(!Variants||Variants->IsEmpty())return nullptr;
    return Cast<USoundBase>((*Variants)[FMath::RandRange(0,Variants->Num()-1)].TryLoad());
}
void InitializeVoice(UHeliosSSXVoiceComponent* Voice,bool Spatial)
{
    if(!Voice||!LoadSourceAudio())return;TArray<USoundBase*> Kills;
    if(const auto* Variants=Sources.Find(32405))for(const auto& Path:*Variants)if(auto* Sound=Cast<USoundBase>(Path.TryLoad()))Kills.Add(Sound);
    Voice->InitializeSources(Spatial,{{TEXT("Hurt"),OriginalSound(Spatial?32404:32403)},{TEXT("Death"),OriginalSound(Spatial?32402:32401)}},Kills);
}
int32 PlayEvents(AActor* Owner,UHeliosSSXVoiceComponent* Voice,bool FP,FName Clip,float Previous,float Time,TArray<TObjectPtr<UAudioComponent>>& Channels)
{
    if(!Owner||!LoadSourceAudio())return 0;
    const FString Short=Clip.ToString();const bool Full=Short.StartsWith(TEXT("B_"))||Short.StartsWith(TEXT("C_"))||Short.StartsWith(TEXT("Change_"));
    const auto* Events=Timelines.Find(FName(FString(FP?(Full?TEXT("FP_"):TEXT("FP_A_")):(Full?TEXT("TP_"):TEXT("TP_A_")))+Short));if(!Events)return 0;
    int32 Count=0;
    for(const auto& Event:*Events)if(Event.Time>Previous&&Event.Time<=Time)
    {
        auto* Sound=OriginalSound(Event.Id);if(!Sound)continue;
        ++Count;UE_LOG(LogTemp,Display,TEXT("ZXK_SOURCE_AUDIO %s %s id=%d source_time=%.6f asset=%s"),FP?TEXT("FP"):TEXT("TP"),*Clip.ToString(),Event.Id,Event.Time,*Sound->GetPathName());
        if(VoiceIds.Contains(Event.Id)){if(!HeliosWeaponHolderContext::PlayCharacterCue(HeliosWeaponHolderContext::IsCompanion(Owner)?Owner:(Owner->GetOwner()?Owner->GetOwner():Owner),TEXT("Action"))&&Voice)Voice->PlaySource(TEXT("Action"),Sound);}
        else if(auto* Channel=FP?HeliosAudio::SpawnSound2D(Owner,Sound,.8f):HeliosAudio::SpawnSoundAttached(Sound,Owner->GetRootComponent(),NAME_None,FVector::ZeroVector,EAttachLocation::KeepRelativeOffset,true,.55f*(Cast<AHeliosPetCompanion>(Owner)?Cast<AHeliosPetCompanion>(Owner)->RosterAudioGain():1.f)))Channels.Add(Channel);
    }
    return Count;
}
AHeliosSSXEffect* Effect(AActor* Owner,const FString& Name,const FVector& At,const FRotator& Rotation,bool FP)
{
    if(FP&&Name.Contains(TEXT("Root_1P_"))&&!Name.Contains(TEXT("JianQi"))&&FParse::Param(FCommandLine::Get(),TEXT("HeliosInputControlQA"))&&FParse::Param(FCommandLine::Get(),TEXT("HeliosZXKNoMeleeFXQA")))return nullptr;
    return AHeliosSSXEffect::Spawn(Owner,Name,At,Rotation,FP,1.f,TEXT("/Game/Helios/CFMZhongXiaoKui/Effects/DA_ZXK_Effects.DA_ZXK_Effects"));
}
}
