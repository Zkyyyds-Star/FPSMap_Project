#include "Audio/HeliosAudioSettingsSubsystem.h"
#include "AudioDevice.h"
#include "Audio/HeliosMusicSubsystem.h"
#include "Audio/HeliosAudioRoutingSubsystem.h"
#include "Pets/HeliosPetDog.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Components/AudioComponent.h"
#include "Components/ActorComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "UObject/UObjectIterator.h"

namespace { constexpr int32 BusCount=int32(EHeliosAudioBus::Count); }
UHeliosAudioSettingsSubsystem* UHeliosAudioSettingsSubsystem::Get(const UObject* Context)
{auto* W=Context&&GEngine?GEngine->GetWorldFromContextObject(Context,EGetWorldErrorMode::ReturnNull):nullptr;return W&&W->GetGameInstance()?W->GetGameInstance()->GetSubsystem<UHeliosAudioSettingsSubsystem>():nullptr;}
const TCHAR* UHeliosAudioSettingsSubsystem::Label(EHeliosAudioBus B)
{static const TCHAR* Names[]={TEXT("总音量"),TEXT("背景音乐"),TEXT("玩家武器"),TEXT("击杀反馈"),TEXT("玩家受击"),TEXT("伙伴"),TEXT("角色语音"),TEXT("环境与敌人"),TEXT("界面音效")};return int32(B)<BusCount?Names[int32(B)]:TEXT("");}
void UHeliosAudioSettingsSubsystem::Defaults()
{Volumes.Init(1,BusCount);Switches.Init(true,BusCount);Volumes[int32(EHeliosAudioBus::Music)]=DefaultMusic;}
void UHeliosAudioSettingsSubsystem::Initialize(FSubsystemCollectionBase& C)
{
    Super::Initialize(C);
    GConfig->GetFloat(TEXT("/Script/HeliosOutpost.HeliosMusicSubsystem"),TEXT("MusicVolume"),DefaultMusic,GGameIni);DefaultMusic=FMath::Clamp(DefaultMusic,0.f,1.f);
    if(Volumes.Num()!=BusCount||Switches.Num()!=BusCount)Defaults();
    for(float& V:Volumes)V=FMath::IsFinite(V)?FMath::Clamp(V,0.f,1.f):1.f;
    Mix=NewObject<USoundMix>(this,TEXT("HeliosUserAudioMix"));Mix->Duration=-1;Mix->FadeInTime=0;Mix->FadeOutTime=0;
    for(int32 I=0;I<BusCount;++I){auto* S=NewObject<USoundClass>(this,*FString::Printf(TEXT("HeliosBus_%d"),I));S->Properties.Volume=1;Classes.Add(S);}
    Classes[int32(EHeliosAudioBus::Music)]->Properties.bIsMusic=true;
    Ticker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&UHeliosAudioSettingsSubsystem::Tick),.05f);
}
float UHeliosAudioSettingsSubsystem::Volume(EHeliosAudioBus B)const{return Volumes.IsValidIndex(int32(B))?Volumes[int32(B)]:1.f;}
bool UHeliosAudioSettingsSubsystem::Enabled(EHeliosAudioBus B)const{return Switches.IsValidIndex(int32(B))&&Switches[int32(B)];}
float UHeliosAudioSettingsSubsystem::EffectiveGain(EHeliosAudioBus B)const
{const float M=Enabled(EHeliosAudioBus::Master)?Volume(EHeliosAudioBus::Master):0;return B==EHeliosAudioBus::Master?M:M*(Enabled(B)?Volume(B):0);}
void UHeliosAudioSettingsSubsystem::SetVolume(EHeliosAudioBus B,float V)
{if(!Volumes.IsValidIndex(int32(B))||!FMath::IsFinite(V))return;Volumes[int32(B)]=FMath::Clamp(V,0.f,1.f);Dirty=true;SaveAfter=FPlatformTime::Seconds()+.25;Apply();}
void UHeliosAudioSettingsSubsystem::SetEnabled(EHeliosAudioBus B,bool V)
{if(!Switches.IsValidIndex(int32(B)))return;Switches[int32(B)]=V;Dirty=true;SaveAfter=FPlatformTime::Seconds()+.25;Apply();}
void UHeliosAudioSettingsSubsystem::ResetDefaults(){Defaults();Dirty=true;Apply();FlushSettings();}
void UHeliosAudioSettingsSubsystem::FlushSettings(){if(Dirty){SaveConfig(CPF_Config,*GGameIni);GConfig->Flush(false,GGameIni);Dirty=false;UE_LOG(LogTemp,Display,TEXT("HELIOS_AUDIO_SETTINGS saved=%s"),*GGameIni);}}
USoundClass* UHeliosAudioSettingsSubsystem::BusClass(EHeliosAudioBus B)const{return Classes.IsValidIndex(int32(B))?Classes[int32(B)].Get():nullptr;}
void UHeliosAudioSettingsSubsystem::Apply()
{
    auto* W=GetWorld();if(!W||!W->IsGameWorld()||!W->GetAudioDeviceRaw())return;
    if(AppliedWorld.Get()!=W){for(auto& C:Classes)W->GetAudioDeviceRaw()->RegisterSoundClass(C);UGameplayStatics::PushSoundMixModifier(W,Mix);AppliedWorld=W;UHeliosAudioRoutingSubsystem::Publish(W,Classes);}
    for(int32 I=1;I<BusCount;++I)UGameplayStatics::SetSoundMixClassOverride(W,Mix,Classes[I],EffectiveGain(EHeliosAudioBus(I)),1,.03f,false);
}
EHeliosAudioBus UHeliosAudioSettingsSubsystem::Classify(const UObject* Context,const USoundBase* Sound)const
{
    const FString Type=Context?Context->GetClass()->GetName():TEXT("");
    auto* A=Cast<AActor>(const_cast<UObject*>(Context));if(auto* C=Cast<UActorComponent>(const_cast<UObject*>(Context)))A=C->GetOwner();
    for(int32 I=0;A&&I<5;++I,A=A->GetOwner())if(Cast<AHeliosPetCompanion>(A))return EHeliosAudioBus::Companion;
    if(Type.Contains(TEXT("MusicSubsystem")))return EHeliosAudioBus::Music;
    if(Type.Contains(TEXT("HunterDamageFeedback")))return EHeliosAudioBus::Weapon; // existing weapon contact feedback 07
    if(Type.Contains(TEXT("KillFeedback")))return EHeliosAudioBus::KillFeedback;
    if(Type.Contains(TEXT("SSXVoice")))return EHeliosAudioBus::CharacterVoice;
    if(Type.Contains(TEXT("AdventureHUD")))return EHeliosAudioBus::KillFeedback; // existing boss entrance/react feedback
    A=Cast<AActor>(const_cast<UObject*>(Context));if(auto* C=Cast<UActorComponent>(const_cast<UObject*>(Context)))A=C->GetOwner();
    for(int32 I=0;A&&I<5;++I,A=A->GetOwner())if(Cast<AHeliosEnemyActor>(A)||A->ActorHasTag(TEXT("Enemy")))return EHeliosAudioBus::Environment;
    const FString P=Sound?Sound->GetPathName():TEXT("");
    if(P.Contains(TEXT("SFX_PlayerDamage07")))return EHeliosAudioBus::Weapon;
    if(P.Contains(TEXT("/Music/")))return EHeliosAudioBus::Music;
    if(P.Contains(TEXT("/KillFeedback/"))||P.Contains(TEXT("HunterDamage")))return EHeliosAudioBus::KillFeedback;
    if(P.Contains(TEXT("/VO_")))return EHeliosAudioBus::CharacterVoice;
    if(P.Contains(TEXT("SFX_Hurt"))||P.Contains(TEXT("SFX_Death")))return EHeliosAudioBus::PlayerHurt;
    if(P.Contains(TEXT("Pickup"))||P.Contains(TEXT("Footstep"))||P.Contains(TEXT("Step_"))||P.Contains(TEXT("Ambien"))||Type.Contains(TEXT("BossEffects")))return EHeliosAudioBus::Environment;
    if(P.Contains(TEXT("UI_"))||P.Contains(TEXT("/UI/"))||P.StartsWith(TEXT("/Engine/")))return EHeliosAudioBus::Interface;
    if(P.Contains(TEXT("/Weapons/"))||P.Contains(TEXT("/Realism/"))||P.Contains(TEXT("/CFM"))||P.Contains(TEXT("SFX_Fire"))||P.Contains(TEXT("SFX_Reload"))||Type.Contains(TEXT("Weapon"))||Type.Contains(TEXT("Presentation")))return EHeliosAudioBus::Weapon;
    return EHeliosAudioBus::Environment;
}
void UHeliosAudioSettingsSubsystem::RouteTo(UAudioComponent* C,EHeliosAudioBus B)
{if(C){C->SoundClassOverride=BusClass(B);if(C->Sound)C->Sound->VirtualizationMode=EVirtualizationMode::PlayWhenSilent;if(AppliedWorld.Get()!=GetWorld())Apply();}}
void UHeliosAudioSettingsSubsystem::Route(UAudioComponent* C,const UObject* Context,USoundBase* S)
{RouteTo(C,Classify(Context,S));}
bool UHeliosAudioSettingsSubsystem::Tick(float Dt)
{
    if(AppliedWorld.Get()!=GetWorld())Apply();
    const double Now=FPlatformTime::Seconds();if(Dirty&&Now>=SaveAfter)FlushSettings();
    if(Now>=ScanAfter){ScanAfter=Now+.2;UHeliosAudioRoutingSubsystem::Publish(GetWorld(),Classes);}
    return true;
}
void UHeliosAudioSettingsSubsystem::Deinitialize()
{FlushSettings();FTSTicker::GetCoreTicker().RemoveTicker(Ticker);UHeliosAudioRoutingSubsystem::Forget(Classes);if(auto* W=AppliedWorld.Get())UGameplayStatics::PopSoundMixModifier(W,Mix);Super::Deinitialize();}
