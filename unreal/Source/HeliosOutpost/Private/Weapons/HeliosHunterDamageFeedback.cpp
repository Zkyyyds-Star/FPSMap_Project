#include "Weapons/HeliosHunterDamageFeedback.h"
#include "Audio/HeliosAudio.h"
#include "Weapons/HeliosSunShangXiang.h"
#include "Combat/HeliosCombatTempoSettings.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Player/HeliosArenaCharacter.h"
#include "Engine/World.h"
#include "Sound/SoundBase.h"
UHeliosHunterDamageFeedback::UHeliosHunterDamageFeedback(){PrimaryComponentTick.bCanEverTick=true;}
void UHeliosHunterDamageFeedback::BeginPlay(){Super::BeginPlay();Sound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Helios/SharedHunterAudio/SFX_PlayerDamage07.SFX_PlayerDamage07"));}
int32 UHeliosHunterDamageFeedback::ActiveCount()const{int32 N=0;for(const auto& C:Channels)if(IsValid(C)&&C->IsPlaying())++N;return N;}
void UHeliosHunterDamageFeedback::Clear(){for(const auto& C:Channels)if(IsValid(C)){C->Stop();C->DestroyComponent();}Channels.Reset();}
void UHeliosHunterDamageFeedback::EndPlay(const EEndPlayReason::Type R){Clear();Super::EndPlay(R);}
void UHeliosHunterDamageFeedback::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Fn)
{
 Super::TickComponent(Dt,Type,Fn);
 const auto* Player=GetOwner()?Cast<AHeliosArenaCharacter>(GetOwner()->GetOwner()):nullptr;
 if(!Player||Player->IsDead()||Player->GetEquippedWeapon()!=GetOwner()){Clear();return;}
 for(int32 I=Channels.Num()-1;I>=0;--I)if(!IsValid(Channels[I])||!Channels[I]->IsPlaying()){if(IsValid(Channels[I]))Channels[I]->DestroyComponent();Channels.RemoveAt(I);}
}
bool UHeliosHunterDamageFeedback::Emit(uint64 Event)
{
 const auto* Player=GetOwner()?Cast<AHeliosArenaCharacter>(GetOwner()->GetOwner()):nullptr;
 if(!Player||!Player->IsLocallyControlled()||Player->IsDead()||Player->GetEquippedWeapon()!=GetOwner()||!Sound)return false;
 if(RecentEvents.Contains(Event)){++Duplicates;return false;}
 RecentEvents.Add(Event);if(RecentEvents.Num()>128)RecentEvents.RemoveAt(0);
 const float Cooldown=UHeliosCombatTempoSettings::Hunter(this,bZXK,false,TEXT("伤害反馈最短间隔"),0,0,1);
 const double Now=GetWorld()->GetTimeSeconds();if(Now-LastPlayed+UE_SMALL_NUMBER<Cooldown)return false;
 const float Volume=UHeliosCombatTempoSettings::Hunter(this,bZXK,false,TEXT("伤害反馈音量"),1,0,2);
 const int32 Limit=FMath::RoundToInt(UHeliosCombatTempoSettings::Hunter(this,bZXK,false,TEXT("伤害反馈并发数"),1,1,8));
 const float Release=UHeliosCombatTempoSettings::Hunter(this,bZXK,false,TEXT("伤害反馈覆盖淡出"),0,0,.1f);
 while(Channels.Num()>=Limit){auto* Old=Channels[0].Get();if(IsValid(Old)){if(Release>0){Old->bAutoDestroy=true;Old->FadeOut(Release,0);}else{Old->Stop();Old->DestroyComponent();}}Channels.RemoveAt(0);++Replaced;}
 if(auto* C=HeliosAudio::SpawnSound2D(this,Sound,Volume,1.f,0.f,nullptr,false,false))
 {Channels.Add(C);LastPlayed=Now;++Played;UE_LOG(LogTemp,Display,TEXT("HUNTER_DAMAGE07 source=%s event=%llu played=%d active=%d volume=%.3f pitch=1"),*GetOwner()->GetName(),Event,Played,ActiveCount(),Volume);return true;}
 return false;
}
