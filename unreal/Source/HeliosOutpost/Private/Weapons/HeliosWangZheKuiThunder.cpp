#include "Weapons/HeliosWangZheKuiThunder.h"
#include "Weapons/HeliosWeaponHolderContext.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Audio/HeliosAudio.h"
#include "Weapons/HeliosHandAnimInstance.h"
#include "Weapons/HeliosSSXVoice.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Pets/HeliosPetDog.h"
#include "Player/HeliosArenaCharacter.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Sound/SoundBase.h"
#include "HeliosCharacterFormVoiceSources.inl"

namespace HeliosWangZheKuiThunder
{
float Setting(bool Pet,const TCHAR* Key,float Default,float Min,float Max,bool Slug)
{
    FString Contents;const FString Path=FPaths::ProjectContentDir()/TEXT("Settings/战斗数值/王者葵王者之霆.ini");
    FConfigFile File;FString Value;
    if(FFileHelper::LoadFileToString(Contents,*Path))File.ProcessInputFileContents(Contents,Path);
    double Number=Default;
    if(File.GetString(Slug?TEXT("独头弹"):Pet?TEXT("伙伴"):TEXT("玩家"),Key,Value))
    {double Parsed=0;if(LexTryParseString(Parsed,*Value)&&FMath::IsFinite(Parsed))Number=Parsed;}
    return FMath::Clamp(float(Number),Min,Max);
}
FHeliosThunderTuning Tuning(bool Pet,bool Slug)
{
    FHeliosThunderTuning S;
    auto Read=[&](const TCHAR* Key,float Default,float Min,float Max){return Setting(Pet,Key,Default,Min,Max,Slug);};
    S.Damage=Read(TEXT("每颗弹丸伤害"),Slug?160:Pet?6:16,0,100000);
    S.MinimumDamage=FMath::Min(S.Damage,Read(TEXT("远端每颗伤害"),Slug?80:Pet?1.5f:4,0,100000));
    S.Range=Read(TEXT("射程"),Slug?6000:3000,100,20000);S.Falloff=Read(TEXT("衰减起点"),Slug?1500:500,0,S.Range);
    S.Spread=Read(TEXT("散布半角"),Slug?.5f:3,0,30);S.Interval=Read(TEXT("射击间隔"),Slug?.35f:.22f,.1,10);
    S.Reload=Setting(Pet,TEXT("换弹时间"),2.3f,.3,20);S.Magazine=FMath::RoundToInt(Setting(Pet,TEXT("弹匣容量"),25,1,100));
    S.Pellets=FMath::RoundToInt(Read(TEXT("每枪弹丸数"),Slug?1:10,1,32));return S;
}
UAnimSequence* Animation(const TCHAR* Prefix,FName Clip)
{const FString Name=FString(Prefix)+Clip.ToString();return LoadObject<UAnimSequence>(nullptr,*(TEXT("/Game/Helios/CFMWangZheKuiThunder/Animations/")+Name+TEXT(".")+Name));}
USoundBase* Sound(int32 Id)
{const FString Name=FString::Printf(TEXT("SFX_THUNDER_%d"),Id);return LoadObject<USoundBase>(nullptr,*(TEXT("/Game/Helios/CFMWangZheKuiThunder/Audio/")+Name+TEXT(".")+Name));}
void InitializeVoice(UHeliosSSXVoiceComponent* Voice,bool Spatial)
{
    TArray<USoundBase*> Pool;
    for(int32 Id:{24881,24882,24883,24884,24885,24886,24887,24888,24889,24890,24891})
    {const FString N=FString::Printf(TEXT("VO_KUI_%d"),Id);if(auto* S=LoadObject<USoundBase>(nullptr,*(TEXT("/Game/Helios/CFMWangZheKuiThunder/Audio/")+N)))Pool.Add(S);}
    TMap<FName,USoundBase*> Cues;
    Cues.Add(TEXT("Hurt"),LoadObject<USoundBase>(nullptr,TEXT("/Game/Helios/CFMWangZheKuiThunder/Audio/VO_KUI_24861_138083993")));
    Cues.Add(TEXT("Death"),LoadObject<USoundBase>(nullptr,TEXT("/Game/Helios/CFMWangZheKuiThunder/Audio/VO_KUI_24860_713830632")));
    Voice->InitializeSources(Spatial,Cues,Pool);
    UE_LOG(LogTemp,Display,TEXT("KUI_VOICE identity=KuiWangZheGR sources=%d spatial=%d"),Pool.Num(),Spatial);
}
void CombatVoice(UHeliosSSXVoiceComponent* Voice,FName Cue)
{if(Voice)Voice->Play(Cue);}
void InitializeFormVoice(UHeliosSSXVoiceComponent* Voice,FName Variant,bool Spatial)
{HeliosCharacterFormVoice::Initialize(TEXT("cfm_wangzhekui"),Variant.IsNone()?FName("GR"):Variant,Voice,Spatial);}
AHeliosSSXEffect* Effect(AActor* Owner,const TCHAR* Name,const FVector& At,const FRotator& Rotation,bool FP,float PlaybackRate)
{auto* E=AHeliosSSXEffect::Spawn(Owner,Name,At,Rotation,FP,PlaybackRate,TEXT("/Game/Helios/CFMWangZheKuiThunder/Effects/DA_THUNDER_Effects.DA_THUNDER_Effects"));if(FParse::Param(FCommandLine::Get(),TEXT("HeliosWangZheKuiThunderQA")))UE_LOG(LogTemp,Display,TEXT("KUI_FX owner=%s recipe=%s loaded=%d visible_tracks=%d at=%s speed=%.6f"),*GetNameSafe(Owner),Name,E!=nullptr,E?E->VisibleTrackCount():0,*At.ToCompactString(),PlaybackRate);return E;}
AHeliosSSXEffect* AttachedEffect(AActor* Owner,USceneComponent* Parent,const TCHAR* Name,bool FP,float Scale,FName Socket,float PlaybackRate)
{
    auto* E=Effect(Owner,Name,Parent->GetComponentLocation(),Parent->GetComponentRotation(),FP,PlaybackRate);
    if(E){E->AttachToComponent(Parent,FAttachmentTransformRules::SnapToTargetNotIncludingScale,Socket);E->SetActorRelativeRotation(FRotator(0,90,0));E->SetActorRelativeScale3D(FVector(Scale));E->ResetBirthAnchors();E->AddTickPrerequisiteComponent(Parent);E->SetTickGroup(TG_PostUpdateWork);}
    return E;
}
int32 FirePellets(AActor* Source,APawn* Instigator,AActor* Friendly,const FVector& View,const FVector& Direction,const FVector& Muzzle,const FHeliosThunderTuning& S,FRandomStream& Random,int32& Hits)
{
    int32 Kills=0;Hits=0;if(!Source||!Source->GetWorld())return 0;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(ChanThunderPellets),true,Source);Q.AddIgnoredActor(Instigator);Q.AddIgnoredActor(Friendly);
    for(TActorIterator<AHeliosPetCompanion> It(Source->GetWorld());It;++It)Q.AddIgnoredActor(*It);
    FHitResult Obstruction;const bool Blocked=Source->GetWorld()->LineTraceSingleByChannel(Obstruction,View,Muzzle,ECC_Visibility,Q);
    TSet<AHeliosEnemyActor*> Killed;
    TMap<AHeliosEnemyActor*,FVector> Contacts;
    for(int32 I=0;I<S.Pellets;++I)
    {
        const FVector Dir=Random.VRandCone(Direction.GetSafeNormal(),FMath::DegreesToRadians(S.Spread));
        FHitResult Camera,Hit;const FVector End=View+Dir*S.Range;
        Source->GetWorld()->LineTraceSingleByChannel(Camera,View,End,ECC_Visibility,Q);
        if(Blocked)Hit=Obstruction;
        else Source->GetWorld()->LineTraceSingleByChannel(Hit,Muzzle,Camera.bBlockingHit?Camera.ImpactPoint+Dir*3:End,ECC_Visibility,Q);
        auto* Enemy=Cast<AHeliosEnemyActor>(Hit.GetActor());
        if(!Enemy||Enemy->IsEnemyDead()||Enemy->ActorHasTag(TEXT("Friendly")))continue;
        const float Distance=FVector::Distance(View,Hit.ImpactPoint);const float Alpha=FMath::Clamp((Distance-S.Falloff)/FMath::Max(1.f,S.Range-S.Falloff),0.f,1.f);
        const float Before=Enemy->CurrentHP;
        const float Applied=UGameplayStatics::ApplyPointDamage(Enemy,FMath::Lerp(S.Damage,S.MinimumDamage,Alpha),Dir,Hit,Instigator?Instigator->GetController():nullptr,Source,UDamageType::StaticClass());
        if(Applied>0&&Enemy->CurrentHP<Before)
        {++Hits;Contacts.FindOrAdd(Enemy)=Hit.ImpactPoint;if(Enemy->IsEnemyDead()&&!Killed.Contains(Enemy)){Killed.Add(Enemy);++Kills;}}
    }
    // One source impact/kill effect per enemy per shot, irrespective of pellet count.
    return Kills;
}
}

UHeliosWangZheKuiThunderDefinition::UHeliosWangZheKuiThunderDefinition()
{
    WeaponId=TEXT("cfm_wangzhekui_thunder");DisplayName=FText::FromString(TEXT("王者葵·保卫者 / 王者之霆"));
    Description=FText::FromString(TEXT("王者葵保卫者原手臂与王者之霆。左键连续射击，右键切换普通霰弹 / 独头弹，R 换弹，G 展示。原角色击杀语音；伙伴独立选择。"));
    WeaponClass=AHeliosWangZheKuiThunderWeapon::StaticClass();const auto S=HeliosWangZheKuiThunder::Tuning(false);
    MagazineSize=S.Magazine;ReloadSeconds=S.Reload;Damage=S.Damage;MinimumDamage=S.MinimumDamage;Range=S.Range;FalloffStart=S.Falloff;PelletCount=S.Pellets;SpreadHalfAngle=S.Spread;RefireSeconds=S.Interval;Recoil=.6f;
    AttachmentLocation=FVector(0,0,0);AttachmentRotation=FRotator(0,-90,0);AttachmentScale=FVector(1);
    SkeletalWeaponMesh=TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Helios/CFMWangZheKuiThunder/SK_WangZheKuiArms_FP.SK_WangZheKuiArms_FP")));
    PreviewTexture=TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Helios/CFMWangZheKuiThunder/Textures/T_WangZheKui_TP_Preview.T_WangZheKui_TP_Preview")));
}
AHeliosWangZheKuiThunderWeapon::AHeliosWangZheKuiThunderWeapon()
{
    PrimaryActorTick.bCanEverTick=true;SetExternalPresentation(true,TEXT("MuzzleFlashSocket"));Thunder=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Original Thunder FP"));Thunder->SetupAttachment(GetFirstPersonMesh());
    Thunder->SetCollisionEnabled(ECollisionEnabled::NoCollision);Thunder->SetOnlyOwnerSee(true);Thunder->SetCastShadow(false);Thunder->SetCanEverAffectNavigation(false);
    Thunder->SetAnimInstanceClass(UHeliosHandAnimInstance::StaticClass());GetFirstPersonMesh()->SetAnimInstanceClass(UHeliosHandAnimInstance::StaticClass());
    Voice=CreateDefaultSubobject<UHeliosSSXVoiceComponent>(TEXT("Original WangZheKui GR voice"));bFullAuto=true;
    Thunder->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
    GetFirstPersonMesh()->SetCastShadow(false);GetFirstPersonMesh()->SetBoundsScale(4);Thunder->SetBoundsScale(4);
    GetFirstPersonMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Thunder->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
}
void AHeliosWangZheKuiThunderWeapon::ApplyDefinition(const UHeliosWeaponDefinition* D)
{
    Super::ApplyDefinition(D);ShotSettings=HeliosWangZheKuiThunder::Tuning(HeliosWeaponHolderContext::IsCompanion(GetOwner()));RefireRate=ShotSettings.Interval;MagazineSize=ShotSettings.Magazine;
    GetFirstPersonMesh()->SetSkeletalMesh(D->SkeletalWeaponMesh.LoadSynchronous());Thunder->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Helios/CFMWangZheKuiThunder/SK_Thunder_FP")));
    for(auto* M:{GetFirstPersonMesh(),Thunder.Get()}){M->SetAnimInstanceClass(UHeliosHandAnimInstance::StaticClass());M->AddTickPrerequisiteActor(this);M->bEnableUpdateRateOptimizations=false;if(auto* A=Cast<UHeliosHandAnimInstance>(M->GetAnimInstance())){A->bPreserveTransitionVelocity=false;A->BlendDuration=.025f;}}
    for(FName N:{FName("Idle"),FName("Move"),FName("Equip"),FName("ChangeClip"),FName("FireSingle"),FName("Fire"),FName("Fire0"),FName("Show"),FName("Skill"),FName("Skill_Cancellation"),FName("FireSingle_Slug")})
    {ArmsClips.Add(N,HeliosWangZheKuiThunder::Animation(TEXT("A_KUI_FP_"),N));GunClips.Add(N,HeliosWangZheKuiThunder::Animation(TEXT("A_THUNDER_FP_"),N));}
    for(int32 Id:{15228,13582,13583,13584,13585,13586,13587,13588,13826,13828,15227,26294})ActionSounds.Add(Id,HeliosWangZheKuiThunder::Sound(Id));
    HeliosWangZheKuiThunder::InitializeVoice(Voice,HeliosWeaponHolderContext::IsCompanion(GetOwner()));FirstPersonModel->SetVisibility(false);ThirdPersonModel->SetVisibility(false);Pose();
}
void AHeliosWangZheKuiThunderWeapon::ActivateWeapon(const FName& Tag)
{
    Super::ActivateWeapon(Tag);bActive=true;bSlug=false;bPendingSlug=false;bReload=false;ShotSettings=HeliosWangZheKuiThunder::Tuning(HeliosWeaponHolderContext::IsCompanion(GetOwner()));RefireRate=ShotSettings.Interval;MagazineSize=ShotSettings.Magazine;
    GetFirstPersonMesh()->SetRelativeLocationAndRotation(ActiveDefinition->AttachmentLocation,ActiveDefinition->AttachmentRotation);Thunder->SetRelativeTransform(FTransform::Identity);
    Effects.Add(HeliosWangZheKuiThunder::AttachedEffect(this,Thunder,TEXT("FX_1P_AA12_LeiTing_Gold_EffectSocket_JiChui_Idle_FP"),true,.01f,TEXT("EffectSocket_JiChui")));
    Effects.Add(HeliosWangZheKuiThunder::AttachedEffect(this,Thunder,TEXT("FX_1P_AA12_LeiTing_Gold_EffectSocket_DanJia_Idle_FP"),true,.01f,TEXT("EffectSocket_DanJia")));
    // These persistent surface effects belong to the moving gun/drum, including
    // while the drum leaves the receiver. Keep their authored local trajectories;
    // action smoke and muzzle particles retain their world-space birth anchors.
    for(auto& E:Effects)if(IsValid(E))E->FollowTravelingEmitter();
    BeginAction(TEXT("Equip"),1.f);
}
void AHeliosWangZheKuiThunderWeapon::ClearActionEffects(){for(auto& E:ActionEffects)if(IsValid(E))E->Destroy();ActionEffects.Reset();}
void AHeliosWangZheKuiThunderWeapon::ClearEffects(){ClearActionEffects();for(auto& E:Effects)if(IsValid(E))E->Destroy();Effects.Reset();}
void AHeliosWangZheKuiThunderWeapon::DeactivateWeapon(){StopFiring();bActive=false;CancelReload();ClearEffects();Voice->Stop();Super::DeactivateWeapon();}
void AHeliosWangZheKuiThunderWeapon::EndPlay(const EEndPlayReason::Type R){ClearEffects();CancelSounds();Voice->Stop();Super::EndPlay(R);}
void AHeliosWangZheKuiThunderWeapon::StartFiring(){bTriggerHeld=true;if(bActive&&!bReload&&!HeliosWeaponHolderContext::IsDead(GetOwner())&&(Action.IsNone()||Action==TEXT("FireSingle")||Action==TEXT("FireSingle_Slug")||Action==TEXT("Fire0")))Super::StartFiring();}
void AHeliosWangZheKuiThunderWeapon::StopFiring(){bTriggerHeld=false;Super::StopFiring();}
void AHeliosWangZheKuiThunderWeapon::StartSpecialFire(){if(bActive&&!bReload&&!IsSwitchingMode()){Super::StopFiring();BeginAction(TEXT("Show"),4.366668f);}}
void AHeliosWangZheKuiThunderWeapon::StartSecondaryFire(){if(!bActive||bReload||IsSwitchingMode()||Action==TEXT("Show")||Action==TEXT("Equip"))return;Super::StopFiring();bPendingSlug=!bSlug;BeginAction(bPendingSlug?FName("Skill"):FName("Skill_Cancellation"),1.333334f);}
void AHeliosWangZheKuiThunderWeapon::BeginReload(float Seconds){if(IsSwitchingMode())return;StopFiring();Super::BeginReload(Seconds);bReload=true;BeginAction(TEXT("ChangeClip"),Seconds);}
void AHeliosWangZheKuiThunderWeapon::CancelReload(){bReload=false;Action=NAME_None;CancelSounds();ClearActionEffects();Super::CancelReload();Pose();}
bool AHeliosWangZheKuiThunderWeapon::HandleOwnerCombatCue(FName Cue){if(Cue==TEXT("FatalHurt"))return true;if(Cue!=TEXT("Hurt")&&Cue!=TEXT("Death"))return false;if(!HeliosWeaponHolderContext::PlayCharacterCue(GetOwner(),Cue))HeliosWangZheKuiThunder::CombatVoice(Voice,Cue);if(Cue==TEXT("Death")){StopFiring();CancelReload();ClearEffects();}return true;}
void AHeliosWangZheKuiThunderWeapon::CancelSounds(){for(const auto& C:SoundChannels)if(IsValid(C))C->Stop();SoundChannels.Reset();}
void AHeliosWangZheKuiThunderWeapon::PlayActionSound(int32 Id,float Gain){if(auto* S=ActionSounds.FindRef(Id).Get())if(auto* C=(HeliosWeaponHolderContext::IsCompanion(GetOwner())?HeliosAudio::SpawnSoundAttached(S,GetOwner()->GetRootComponent(),NAME_None,FVector::ZeroVector,EAttachLocation::KeepRelativeOffset,true,Gain):HeliosAudio::SpawnSound2D(this,S,Gain)))SoundChannels.Add(C);}
void AHeliosWangZheKuiThunderWeapon::BeginAction(FName Clip,float Duration)
{
    ClearActionEffects();if(Clip==TEXT("Show")||Clip==TEXT("Skill")||Clip==TEXT("Skill_Cancellation")||Clip==TEXT("ChangeClip"))CancelSounds();
    Action=Clip;ActionAge=0;ActionDuration=Duration;EventMask=0;Pose();
    if(Clip==TEXT("ChangeClip"))ActionEffects.Add(HeliosWangZheKuiThunder::AttachedEffect(this,Thunder,TEXT("FX_1P_AA12_LeiTing_Gold_FireEffect_ChangeClip"),true,.01f,TEXT("FireEffectSocket"),2.3f/FMath::Max(.01f,Duration)));
    if(Clip==TEXT("Skill_Cancellation"))ActionEffects.Add(HeliosWangZheKuiThunder::AttachedEffect(this,Thunder,TEXT("FX_1P_AA12_LeiTing_Gold_FireEffect_Skill_Cancellation"),true,.01f,TEXT("FireEffectSocket")));
}
void AHeliosWangZheKuiThunderWeapon::Pose()
{
    const FName Clip=Action.IsNone()?(PawnOwner&&PawnOwner->GetVelocity().Size2D()>20?FName("Move"):FName("Idle")):Action;
    float Time=Action.IsNone()?IdleAge:ActionAge;if(Action==TEXT("ChangeClip"))Time*=2.3f/FMath::Max(.01f,ActionDuration);
    if(auto* A=Cast<UHeliosHandAnimInstance>(GetFirstPersonMesh()->GetAnimInstance()))A->SetTimelinePose(ArmsClips.FindRef(Clip),Time,Action.IsNone());
    if(auto* A=Cast<UHeliosHandAnimInstance>(Thunder->GetAnimInstance()))A->SetTimelinePose(GunClips.FindRef(Clip),Time,Action.IsNone());
}
void AHeliosWangZheKuiThunderWeapon::Tick(float Dt)
{
    if(HeliosWeaponHolderContext::IsDead(GetOwner())){StopFiring();CancelReload();ClearEffects();return;}
    Super::Tick(Dt);if(!bActive||GetWorld()->IsPaused())return;IdleAge+=Dt;ActionAge+=Dt;
    auto Event=[&](uint32 Bit,float At,int32 Id){if(!(EventMask&Bit)&&ActionAge>=At){EventMask|=Bit;PlayActionSound(Id,.8f);}};
    if(Action==TEXT("ChangeClip")){const float Rate=ActionDuration/2.3f;Event(1,.16691144f*Rate,13584);Event(2,.83614051f*Rate,13583);Event(4,1.51785088f*Rate,13582);}
    if(Action==TEXT("Equip"))Event(1,.04762419f,13585);
    if(Action==TEXT("Show")){Event(1,.21786177f,13586);Event(2,1.18833685f,13587);Event(4,2.33706260f,13588);Event(8,3.65413594f,13586);}
    if(Action==TEXT("Skill"))Event(1,.05193035f,13828);
    if(Action==TEXT("Skill_Cancellation"))Event(1,.10583153f,26294);
    if(!Action.IsNone()&&ActionAge>=ActionDuration)
    {
        if(IsSwitchingMode()){bSlug=bPendingSlug;ShotSettings=HeliosWangZheKuiThunder::Tuning(HeliosWeaponHolderContext::IsCompanion(GetOwner()),bSlug);RefireRate=ShotSettings.Interval;MagazineSize=ShotSettings.Magazine;UE_LOG(LogTemp,Display,TEXT("KUI_MODE slug=%d ammo=%d"),bSlug,CurrentBullets);}
        if(Action==TEXT("ChangeClip")){++Reloads;bReload=false;}Action=NAME_None;ClearActionEffects();
    }
    Effects.RemoveAll([](const auto& E){return !IsValid(E);});SoundChannels.RemoveAll([](const auto& C){return !IsValid(C)||!C->IsPlaying();});Pose();
    if(bTriggerHeld&&!bIsFiring&&!bReload&&Action.IsNone()&&CurrentBullets>0)Super::StartFiring();
}
FString AHeliosWangZheKuiThunderWeapon::Status()const{return FString::Printf(TEXT("%s · %d / %d · %s"),IsSwitchingMode()?TEXT("模式切换中"):bSlug?TEXT("独头弹"):TEXT("普通霰弹"),CurrentBullets,MagazineSize,bReload?TEXT("换弹中"):TEXT("右键切换 · G 展示"));}
FVector AHeliosWangZheKuiThunderWeapon::MuzzleLocation()const{if(HeliosWeaponHolderContext::IsCompanion(GetOwner()))if(auto* C=GetOwner()->FindComponentByClass<UHeliosCharacterPresentationComponent>();C&&C->IsReady())return C->GetAnchorTransform(TEXT("Muzzle")).GetLocation();return Thunder->GetSocketLocation(TEXT("MuzzleFlashSocket"));}
void AHeliosWangZheKuiThunderWeapon::FireProjectile(const FVector& Target)
{
    if(!bActive||bReload||IsSwitchingMode()||Action==TEXT("Show")||Action==TEXT("Equip")||!PawnOwner||!WeaponOwner||CurrentBullets<=0||HeliosWeaponHolderContext::IsDead(GetOwner()))return;
    FVector View;FRotator Aim;PawnOwner->GetActorEyesViewPoint(View,Aim);int32 Hits=0;
    const int32 Kills=HeliosWangZheKuiThunder::FirePellets(this,PawnOwner,HeliosWeaponHolderContext::FriendlyPlayer(GetOwner()),View,Aim.Vector(),MuzzleLocation(),ShotSettings,SpreadRandom,Hits);
    TrueKills+=Kills;HitPellets+=Hits;++ShotsFired;for(int32 I=0;I<Kills;++I)if(!HeliosWeaponHolderContext::PlayCharacterCue(GetOwner(),TEXT("Kill")))Voice->Play(TEXT("Kill"));
    BeginAction(bSlug?FName("FireSingle_Slug"):CurrentBullets==1?FName("Fire0"):FName("FireSingle"),CurrentBullets==1?.2f:.233334f);PlayActionSound(bSlug?13826:HeliosWeaponHolderContext::IsCompanion(GetOwner())?15228:15227,.65f);
    Effects.Add(HeliosWangZheKuiThunder::Effect(this,HeliosWeaponHolderContext::IsCompanion(GetOwner())?TEXT("FX_3P_Gunfire_AA12_LeiTing_Gold"):bSlug?TEXT("FX_1P_Gunfire_AA12_LeiTing_Gold_Bio"):TEXT("FX_1P_Gunfire_AA12_LeiTing_Gold"),MuzzleLocation(),Aim,!HeliosWeaponHolderContext::IsCompanion(GetOwner())));
    --CurrentBullets;bShotCommitted=true;WeaponOwner->UpdateWeaponHUD(CurrentBullets,MagazineSize);WeaponOwner->AddWeaponRecoil(FiringRecoil);
    UE_LOG(LogTemp,Display,TEXT("KUI_THUNDER_SHOT source=player slug=%d shot=%d hits=%d kills=%d ammo=%d muzzle=%s"),bSlug,ShotsFired,Hits,Kills,CurrentBullets,*MuzzleLocation().ToCompactString());
}
