#include "Player/HeliosQingYaAbilityComponent.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Player/HeliosArenaCharacter.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Pets/HeliosPetDog.h"
#include "Audio/HeliosAudio.h"
#include "Audio/HeliosAudioSettingsSubsystem.h"
#include "Audio/HeliosMusicSubsystem.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"

UHeliosQingYaAbilityComponent::UHeliosQingYaAbilityComponent()
{PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickGroup=TG_PostUpdateWork;}
bool UHeliosQingYaAbilityComponent::IsSelected() const
{const auto* P=Cast<AHeliosArenaCharacter>(GetOwner());return P&&P->GetLoadout().CharacterId==TEXT("cfm_qingya");}
UHeliosQingYaSkillProfile* UHeliosQingYaAbilityComponent::LoadProfile() const
{
    if(!Profile)Profile=LoadObject<UHeliosQingYaSkillProfile>(nullptr,TEXT("/Game/Helios/CharacterForms/cfm_qingya/DA_QingYaSkills.DA_QingYaSkills"));
    return Profile;
}
void UHeliosQingYaAbilityComponent::EnsureTalent()
{
    if(bTalentDrawn||!IsSelected())return;
    Talent=static_cast<EHeliosQingYaTalent>(FMath::RandRange(0,2));bTalentDrawn=true;++TalentDraws;
    UE_LOG(LogTemp,Display,TEXT("QINGYA_TALENT draw=%d talent=%d"),TalentDraws,int32(Talent));
}
void UHeliosQingYaAbilityComponent::ResetRun()
{Cancel();StopVictoryMusic();bTalentDrawn=false;bVictoryPlayed=false;SwitchClock=0;}
float UHeliosQingYaAbilityComponent::MovementMultiplier() const
{
    const auto* P=Cast<AHeliosArenaCharacter>(GetOwner());const auto* Data=IsSelected()?LoadProfile():nullptr;
    return Data&&P&&!P->IsDead()&&!P->IsMechMode()&&!P->IsFighterMode()&&P->GetLoadout().CharacterVariantId==TEXT("SP")?FMath::Clamp(Data->SpiritMovementMultiplier,1.f,3.f):1.f;
}
float UHeliosQingYaAbilityComponent::KillWindowSeconds() const
{const auto* Data=IsSelected()?LoadProfile():nullptr;return Data&&bTalentDrawn&&Talent==EHeliosQingYaTalent::KillChain?FMath::Max(6.f,Data->KillChainSeconds):6.f;}
FString UHeliosQingYaAbilityComponent::StatusText() const
{const TCHAR* Name=Talent==EHeliosQingYaTalent::RifleSwitch?TEXT("步枪切枪提速"):Talent==EHeliosQingYaTalent::ShotgunSwitch?TEXT("霰弹枪切枪提速"):TEXT("连杀延时");return FString(TEXT("J 比心 · K 手枪手势 · T 踢腿 · 本局天赋："))+(bTalentDrawn?Name:TEXT("待抽取"));}
float UHeliosQingYaAbilityComponent::SwitchDurationMultiplier(FName WeaponId) const
{
    const auto* Data=IsSelected()?LoadProfile():nullptr;const auto* W=UHeliosWeaponRegistry::FindWeapon(WeaponId);
    if(!Data||!W||!bTalentDrawn)return 1.f;
    const auto Category=W->GetCombatCategory();
    const bool Applies=(Talent==EHeliosQingYaTalent::RifleSwitch&&Category==EHeliosWeaponCategory::Rifle)||(Talent==EHeliosQingYaTalent::ShotgunSwitch&&Category==EHeliosWeaponCategory::Shotgun);
    return Applies?FMath::Clamp(Data->SwitchDurationMultiplier,.1f,1.f):1.f;
}
void UHeliosQingYaAbilityComponent::NotifyWeaponEquipped()
{
    Cancel();EnsureTalent();SwitchClock=0;
    if(!IsSelected())StopVictoryMusic();
    auto* P=Cast<AHeliosArenaCharacter>(GetOwner());const auto* Data=IsSelected()?LoadProfile():nullptr;
    if(!P||!Data)return;
    SwitchClock=FMath::Max(.01f,Data->BaseSwitchSeconds)*SwitchDurationMultiplier(P->GetWeaponId());
}
bool UHeliosQingYaAbilityComponent::StartKick()
{
    auto* P=Cast<AHeliosArenaCharacter>(GetOwner());
    if(!IsSelected()||!P||P->IsDead()||P->IsReloading()||P->IsMechMode()||P->IsFighterMode()||!GetWorld()||GetWorld()->IsPaused()||bKicking||SwitchClock>0||!LoadProfile())return false;
    auto* Art=P->GetCharacterPresentation();
    if(!Art||!Art->BeginCharacterGesture(TEXT("Kick")))return false;
    P->StopFireFromAutomation();bKicking=true;bStruck=false;KickAge=0;++Kicks;
    UE_LOG(LogTemp,Display,TEXT("QINGYA_KICK_BEGIN variant=%s"),*P->GetLoadout().CharacterVariantId.ToString());return true;
}
void UHeliosQingYaAbilityComponent::Strike()
{
    auto* P=Cast<AHeliosArenaCharacter>(GetOwner());const auto* Data=LoadProfile();if(!P||!Data||!P->FirstPersonCamera)return;
    const FVector Start=P->FirstPersonCamera->GetComponentLocation(),End=Start+P->FirstPersonCamera->GetForwardVector()*FMath::Max(1.f,Data->KickRange);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(QingYaKick),false,P);if(P->GetEquippedWeapon())Params.AddIgnoredActor(P->GetEquippedWeapon());
    if(auto* Pets=GetWorld()->GetSubsystem<UHeliosPetWorld>())Pets->IgnoreAll(Params);
    FHitResult Hit;
    if(GetWorld()->SweepSingleByChannel(Hit,Start,End,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(FMath::Max(1.f,Data->KickSweepRadius)),Params))
        if(auto* Enemy=Cast<AHeliosEnemyActor>(Hit.GetActor());Enemy&&!Enemy->IsEnemyDead())
        {UGameplayStatics::ApplyPointDamage(Enemy,FMath::Max(0.f,Data->KickDamage),(End-Start).GetSafeNormal(),Hit,P->GetController(),P,nullptr);++Hits;}
}
void UHeliosQingYaAbilityComponent::Cancel()
{
    if(bKicking)if(auto* P=Cast<AHeliosArenaCharacter>(GetOwner()))if(auto* Art=P->GetCharacterPresentation())Art->CancelCharacterGesture();
    bKicking=false;bStruck=false;KickAge=0;
}
void UHeliosQingYaAbilityComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Fn)
{
    Super::TickComponent(Dt,Type,Fn);if(!GetWorld()||GetWorld()->IsPaused())return;
    auto* P=Cast<AHeliosArenaCharacter>(GetOwner());if(!IsSelected()||!P||P->IsDead()){Cancel();StopVictoryMusic();SwitchClock=0;return;}
    EnsureTalent();SwitchClock=FMath::Max(0.f,SwitchClock-Dt);
    if(!bKicking)return;
    auto* Art=P->GetCharacterPresentation();if(!Art||!Art->IsCharacterGesturePlaying()){Cancel();return;}
    KickAge+=Dt;if(!bStruck&&KickAge>=LoadProfile()->KickContactSeconds){bStruck=true;Strike();}
}
void UHeliosQingYaAbilityComponent::PlayVictoryMusic()
{
    if(!IsSelected()||bVictoryPlayed)return;const auto* Data=LoadProfile();if(!Data)return;
    if(auto* Sound=Data->VictoryMusic.LoadSynchronous())
    {
        auto* Audio=HeliosAudio::CreateSound2D(GetOwner(),Sound,.75f);
        if(!Audio)return;Audio->bIsUISound=true;
        if(auto* Settings=UHeliosAudioSettingsSubsystem::Get(this))Settings->RouteTo(Audio,EHeliosAudioBus::Music);
        Audio->OnAudioFinishedNative.AddWeakLambda(this,[this](UAudioComponent*){if(BackgroundMusic.IsValid())BackgroundMusic->SetVolumeMultiplier(1.f);BackgroundMusic.Reset();if(GetWorld())GetWorld()->GetTimerManager().ClearTimer(VictoryRestoreTimer);VictoryAudio=nullptr;});
        Audio->Play();bVictoryPlayed=true;++VictoryPlays;
        VictoryAudio=Audio;
        if(auto* GI=GetWorld()->GetGameInstance())if(auto* Music=GI->GetSubsystem<UHeliosMusicSubsystem>())if(auto* Background=Music->GetMusicComponent())
        {BackgroundMusic=Background;Background->SetVolumeMultiplier(.1f);GetWorld()->GetTimerManager().SetTimer(VictoryRestoreTimer,[this](){if(BackgroundMusic.IsValid())BackgroundMusic->SetVolumeMultiplier(1.f);BackgroundMusic.Reset();},FMath::Max(.1f,Sound->GetDuration()),false);}
        UE_LOG(LogTemp,Display,TEXT("QINGYA_VICTORY_MUSIC plays=%d"),VictoryPlays);
    }
}
void UHeliosQingYaAbilityComponent::EndPlay(const EEndPlayReason::Type Reason)
{Cancel();StopVictoryMusic();Super::EndPlay(Reason);}
void UHeliosQingYaAbilityComponent::StopVictoryMusic()
{if(IsValid(VictoryAudio))VictoryAudio->Stop();VictoryAudio=nullptr;if(BackgroundMusic.IsValid())BackgroundMusic->SetVolumeMultiplier(1.f);BackgroundMusic.Reset();if(GetWorld())GetWorld()->GetTimerManager().ClearTimer(VictoryRestoreTimer);}
