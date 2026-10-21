#include "Combat/HeliosKillFeedbackSubsystem.h"
#include "Audio/HeliosAudio.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "Boss/HeliosEnemyBoss.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Enemies/HeliosEnemyTestDirector.h"
#include "EngineUtils.h"
#include "Player/HeliosArenaCharacter.h"
#include "Player/HeliosQingYaAbilityComponent.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Components/AudioComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

void FHeliosKillChain::Record(double Now, bool bBoss, double WindowSeconds)
{
    Streak=(Now-LastKill<=FMath::Max(0.0,WindowSeconds))?Streak+1:1;
    LastKill=Now;
    EHeliosKillBadge Kind=bBoss?EHeliosKillBadge::Boss:
        Total==0?EHeliosKillBadge::First:EHeliosKillBadge(FMath::Clamp(Streak-1,0,5));
    ++Total;
    if(!bPending) { PendingAt=Now; Pending=Kind; bPending=true; }
    else if(Priority(Kind)>Priority(Pending)) Pending=Kind;
}

bool FHeliosKillChain::Flush(double Now)
{
    if(!bPending || Now-PendingAt<0.08-1.e-7) return false;
    Display=Pending; DisplayStreak=Streak; StartedAt=Now; ++Serial; bPending=false;
    return true;
}

const TCHAR* UHeliosKillFeedbackSubsystem::AssetKey(EHeliosKillBadge Kind)
{
    static const TCHAR* Keys[]={TEXT("single"),TEXT("double"),TEXT("triple"),TEXT("quad"),TEXT("penta"),TEXT("hexa"),TEXT("first"),TEXT("boss")};
    return Keys[FMath::Clamp(int32(Kind),0,7)];
}
FString UHeliosKillFeedbackSubsystem::Label(EHeliosKillBadge Kind)
{
    static const TCHAR* Labels[]={TEXT("练习开始"),TEXT("你干嘛！"),TEXT("唱功拉满"),TEXT("舞力全开"),TEXT("RAP 全场"),TEXT("坤力全开"),TEXT("全民制作人"),TEXT("两年半·出师")};
    return Labels[FMath::Clamp(int32(Kind),0,7)];
}
bool UHeliosKillFeedbackSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{ return Type==EWorldType::Game || Type==EWorldType::PIE; }
void UHeliosKillFeedbackSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    for(int32 Index=0;Index<int32(EHeliosKillBadge::Count);++Index)
    {
        const TCHAR* Key=AssetKey(EHeliosKillBadge(Index));
        Textures.Add(LoadObject<UTexture2D>(nullptr,*FString::Printf(TEXT("/Game/Helios/UI/KillFeedback/T_IkunKill_%s.T_IkunKill_%s"),Key,Key)));
        Sounds.Add(LoadObject<USoundBase>(nullptr,*FString::Printf(TEXT("/Game/Helios/UI/KillFeedback/SFX_IkunKill_%s.SFX_IkunKill_%s"),Key,Key)));
    }
}
void UHeliosKillFeedbackSubsystem::Deinitialize()
{ ResetRun(); Textures.Reset(); Sounds.Reset(); Super::Deinitialize(); }
TStatId UHeliosKillFeedbackSubsystem::GetStatId() const
{ RETURN_QUICK_DECLARE_CYCLE_STAT(HeliosKillFeedback,STATGROUP_Tickables); }

bool UHeliosKillFeedbackSubsystem::IsDirectPlayerDamage(const APawn* Player,const AController* Instigator,const AActor* Causer)
{
    if(!IsValid(Player) || !IsValid(Causer)) return false;
    // Inspect both ownership branches. A pet can be owned AND instigated by the player.
    TArray<const AActor*> Work; Work.Add(Causer);
    TSet<const AActor*> Visited;
    bool bPlayerSource=false;
    while(!Work.IsEmpty() && Visited.Num()<64)
    {
        const AActor* Actor=Work.Pop(EAllowShrinking::No);
        if(!IsValid(Actor) || Visited.Contains(Actor)) continue;
        Visited.Add(Actor);
        if(Actor->ActorHasTag(TEXT("HeliosPet")) || Actor->ActorHasTag(TEXT("HeliosSummon"))) return false;
        if(Actor==Player) { bPlayerSource=true; continue; }
        if(const APawn* Pawn=Cast<APawn>(Actor); Pawn && Pawn!=Player) return false;
        Work.Add(Actor->GetOwner()); Work.Add(Actor->GetInstigator());
    }
    // A player controller alone is insufficient: environmental damage must not earn medals.
    return bPlayerSource && (!Instigator || Instigator==Player->GetController());
}
bool UHeliosKillFeedbackSubsystem::IsLiveRun() const
{
    const auto* Director=AHeliosAdventureDirector::Find(GetWorld());
    const auto* Player=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerPawn(GetWorld(),0));
    if(!Player||Player->IsDead())return false;
    if(Director)return !Director->IsSelectingWeapon()&&!Director->IsRunOver();
    if(Player->GetQingYaAbility()->IsSelected())
        for(TActorIterator<AHeliosEnemyTestDirector> I(GetWorld());I;++I)return true;
    return false;
}
void UHeliosKillFeedbackSubsystem::ReportKill(AHeliosEnemyActor* Victim,AController* Instigator,AActor* Causer)
{
    if(!GetWorld() || GetWorld()->IsPaused() || !IsValid(Victim) || !Victim->IsEnemyDead() || !IsLiveRun()) return;
    if(ReportedDeaths.Contains(Victim)) return;
    const APawn* Player=UGameplayStatics::GetPlayerPawn(this,0);
    if(!IsDirectPlayerDamage(Player,Instigator,Causer)) return;
    ReportedDeaths.Add(Victim);
    const double Now=GetWorld()->GetTimeSeconds();
    Advance(Now);
    const auto* Character=Cast<AHeliosArenaCharacter>(Player);
    const auto* Qing=Character?Character->GetQingYaAbility():nullptr;
    Chain.Record(Now,Victim->IsA<AHeliosEnemyBoss>(),Qing?Qing->KillWindowSeconds():6.0);
    if(Qing&&Qing->IsSelected())if(auto* Art=Character->GetCharacterPresentation())Art->PlayCharacterCue(TEXT("Kill"));
    UE_LOG(LogTemp,Display,TEXT("IKUN_KILL accepted victim=%s source=%s streak=%d total=%d boss=%d"),
        *Victim->GetName(),*GetNameSafe(Causer),Chain.Streak,Chain.Total,Victim->IsA<AHeliosEnemyBoss>());
}
void UHeliosKillFeedbackSubsystem::Advance(double Now)
{
    if(!Chain.Flush(Now)) return;
    UE_LOG(LogTemp,Display,TEXT("IKUN_BADGE key=%s streak=%d serial=%d sound=%d"),AssetKey(Chain.Display),Chain.DisplayStreak,Chain.Serial,bSoundEnabled);
    if(!bSoundEnabled || !Sounds.IsValidIndex(int32(Chain.Display)) || !Sounds[int32(Chain.Display)]) return;
    const int32 Incoming=FHeliosKillChain::Priority(Chain.Display);
    if(IsAnnouncing() && Incoming<=AudioPriority) return;
    SuspendAudio();
    Audio=HeliosAudio::SpawnSound2D(this,Sounds[int32(Chain.Display)],0.8f,1.f,0.f,nullptr,false,true);
    AudioPriority=Incoming;
}
void UHeliosKillFeedbackSubsystem::Tick(float DeltaTime)
{
    if(!GetWorld() || GetWorld()->IsPaused()) return;
    if(!IsLiveRun()) { if(Chain.Total || Audio) ResetRun(); return; }
    Advance(GetWorld()->GetTimeSeconds());
    for(auto It=ReportedDeaths.CreateIterator();It;++It) if(!It->IsValid()) It.RemoveCurrent();
}
void UHeliosKillFeedbackSubsystem::SuspendAudio()
{ if(IsValid(Audio)) Audio->Stop(); Audio=nullptr; AudioPriority=-1; }
void UHeliosKillFeedbackSubsystem::ResetRun()
{ SuspendAudio(); Chain.Reset(); ReportedDeaths.Reset(); }
void UHeliosKillFeedbackSubsystem::SetSoundEnabled(bool bEnabled)
{ bSoundEnabled=bEnabled; if(!bEnabled) SuspendAudio(); }
bool UHeliosKillFeedbackSubsystem::IsAnnouncing() const
{ return IsValid(Audio) && Audio->IsPlaying(); }
bool UHeliosKillFeedbackSubsystem::IsVisible() const
{ return GetWorld() && Chain.IsVisible(GetWorld()->GetTimeSeconds()); }
UTexture2D* UHeliosKillFeedbackSubsystem::GetTexture() const
{ return Textures.IsValidIndex(int32(Chain.Display))?Textures[int32(Chain.Display)].Get():nullptr; }
bool UHeliosKillFeedbackSubsystem::HasAllAssets() const
{
    if(Textures.Num()!=8 || Sounds.Num()!=8) return false;
    for(int32 I=0;I<8;++I) if(!Textures[I] || !Sounds[I]) return false;
    return true;
}
