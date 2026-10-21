#include "Weapons/HeliosChanZeus.h"
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
#include "Sound/SoundBase.h"

namespace HeliosChanZeus
{
float Setting(bool Pet,const TCHAR* Key,float Default,float Min,float Max)
{
    FString Contents;const FString Path=FPaths::ProjectContentDir()/TEXT("Settings/战斗数值/蝉宙斯.ini");
    FConfigFile File;FString Value;
    if(FFileHelper::LoadFileToString(Contents,*Path))File.ProcessInputFileContents(Contents,Path);
    double Number=Default;
    if(File.GetString(Pet?TEXT("伙伴"):TEXT("玩家"),Key,Value))
    {double Parsed=0;if(LexTryParseString(Parsed,*Value)&&FMath::IsFinite(Parsed))Number=Parsed;}
    return FMath::Clamp(float(Number),Min,Max);
}
FHeliosZeusTuning Tuning(bool Pet)
{
    FHeliosZeusTuning S;S.Damage=Setting(Pet,TEXT("每颗弹丸伤害"),Pet?16:28,0,100000);
    S.MinimumDamage=FMath::Min(S.Damage,Setting(Pet,TEXT("远端每颗伤害"),Pet?4:7,0,100000));
    S.Range=Setting(Pet,TEXT("射程"),3000,100,20000);S.Falloff=Setting(Pet,TEXT("衰减起点"),500,0,S.Range);
    S.Spread=Setting(Pet,TEXT("散布半角"),3,0,30);S.Interval=Setting(Pet,TEXT("射击间隔"),Pet?.55f:.35f,.1,10);
    S.Reload=Setting(Pet,TEXT("换弹时间"),Pet?2.f:1.8f,.3,20);S.Magazine=FMath::RoundToInt(Setting(Pet,TEXT("弹匣容量"),12,1,100));
    S.Pellets=FMath::RoundToInt(Setting(Pet,TEXT("每枪弹丸数"),10,1,32));return S;
}
UAnimSequence* Animation(const TCHAR* Prefix,FName Clip)
{const FString Name=FString(Prefix)+Clip.ToString();return LoadObject<UAnimSequence>(nullptr,*(TEXT("/Game/Helios/CFMChanZeus/Animations/")+Name+TEXT(".")+Name));}
USoundBase* Sound(int32 Id)
{const FString Name=FString::Printf(TEXT("SFX_ZEUS_%d"),Id);return LoadObject<USoundBase>(nullptr,*(TEXT("/Game/Helios/CFMChanZeus/Audio/")+Name+TEXT(".")+Name));}
void InitializeVoice(UHeliosSSXVoiceComponent* Voice,bool Spatial)
{
    TArray<USoundBase*> Pool;
    for(int32 Id:{18682,18683,18684,18685,18688,18689,18690,18691,18692,18693,18694})
    {const FString N=FString::Printf(TEXT("VO_CHAN_%d"),Id);if(auto* S=LoadObject<USoundBase>(nullptr,*(TEXT("/Game/Helios/CFMChanZeus/Audio/")+N)))Pool.Add(S);}
    TMap<FName,USoundBase*> Cues;
    Cues.Add(TEXT("Hurt"),LoadObject<USoundBase>(nullptr,TEXT("/Game/Helios/CFMChanZeus/Audio/VO_CHAN_18663_71652816")));
    Cues.Add(TEXT("Death"),LoadObject<USoundBase>(nullptr,TEXT("/Game/Helios/CFMChanZeus/Audio/VO_CHAN_18662_358463143")));
    Voice->InitializeSources(Spatial,Cues,Pool);
    UE_LOG(LogTemp,Display,TEXT("CHAN_VOICE identity=DiaoChanBL sources=%d spatial=%d"),Pool.Num(),Spatial);
}
void CombatVoice(UHeliosSSXVoiceComponent* Voice,FName Cue)
{if(Voice)Voice->Play(Cue);}
AHeliosSSXEffect* Effect(AActor* Owner,const TCHAR* Name,const FVector& At,const FRotator& Rotation,bool FP)
{return AHeliosSSXEffect::Spawn(Owner,Name,At,Rotation,FP,1,TEXT("/Game/Helios/CFMChanZeus/Effects/DA_ZEUS_Effects.DA_ZEUS_Effects"));}
AHeliosSSXEffect* AttachedEffect(AActor* Owner,USceneComponent* Parent,const TCHAR* Name,bool FP,float Scale,FName Socket)
{
    auto* E=Effect(Owner,Name,Parent->GetComponentLocation(),Parent->GetComponentRotation(),FP);
    if(E){E->AttachToComponent(Parent,FAttachmentTransformRules::SnapToTargetNotIncludingScale,Socket);E->SetActorRelativeRotation(FRotator(0,90,0));E->SetActorRelativeScale3D(FVector(Scale));E->ResetBirthAnchors();E->AddTickPrerequisiteComponent(Parent);E->SetTickGroup(TG_PostUpdateWork);}
    return E;
}
int32 FirePellets(AActor* Source,APawn* Instigator,AActor* Friendly,const FVector& View,const FVector& Direction,const FVector& Muzzle,const FHeliosZeusTuning& S,FRandomStream& Random,int32& Hits)
{
    int32 Kills=0;Hits=0;if(!Source||!Source->GetWorld())return 0;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(ChanZeusPellets),true,Source);Q.AddIgnoredActor(Instigator);Q.AddIgnoredActor(Friendly);
    for(TActorIterator<AHeliosPetCompanion> It(Source->GetWorld());It;++It)Q.AddIgnoredActor(*It);
    FHitResult Obstruction;const bool Blocked=Source->GetWorld()->LineTraceSingleByChannel(Obstruction,View,Muzzle,ECC_Visibility,Q);
    TSet<AHeliosEnemyActor*> Killed;
    TMap<AHeliosEnemyActor*,FVector> Contacts;
    for(int32 I=0;I<S.Pellets;++I)
    {
        const FVector Dir=I==0?Direction.GetSafeNormal():Random.VRandCone(Direction.GetSafeNormal(),FMath::DegreesToRadians(S.Spread));
        FHitResult Camera,Hit;const FVector End=View+Dir*S.Range;
        Source->GetWorld()->LineTraceSingleByChannel(Camera,View,End,ECC_Visibility,Q);
        if(Blocked)Hit=Obstruction;
        else Source->GetWorld()->LineTraceSingleByChannel(Hit,Muzzle,Camera.bBlockingHit?Camera.ImpactPoint+Dir*3:End,ECC_Visibility,Q);
        if(auto* Weapon=Cast<AHeliosChanZeusWeapon>(Source);Weapon&&Weapon->OnNativeImpact.IsBound()&&Hit.bBlockingHit)
        {
            FHeliosZeusNativeImpact Event;Event.SourceWeapon=Weapon;Event.ShotSerial=Weapon->ShotsFired+1;Event.PelletIndex=I;Event.View=View;Event.Muzzle=Muzzle;Event.Direction=Dir;Event.Hit=Hit;Event.bViewToMuzzleObstructed=Blocked;
            Weapon->OnNativeImpact.Broadcast(Event);
        }
        auto* Enemy=Cast<AHeliosEnemyActor>(Hit.GetActor());
        if(!Enemy||Enemy->IsEnemyDead()||Enemy->ActorHasTag(TEXT("Friendly")))continue;
        const float Distance=FVector::Distance(View,Hit.ImpactPoint);const float Alpha=FMath::Clamp((Distance-S.Falloff)/FMath::Max(1.f,S.Range-S.Falloff),0.f,1.f);
        const float Before=Enemy->CurrentHP;
        const float Applied=UGameplayStatics::ApplyPointDamage(Enemy,FMath::Lerp(S.Damage,S.MinimumDamage,Alpha),Dir,Hit,Instigator?Instigator->GetController():nullptr,Source,UDamageType::StaticClass());
        if(Applied>0&&Enemy->CurrentHP<Before)
        {++Hits;Contacts.FindOrAdd(Enemy)=Hit.ImpactPoint;if(Enemy->IsEnemyDead()&&!Killed.Contains(Enemy)){Killed.Add(Enemy);++Kills;}}
    }
    // One source impact/kill effect per enemy per shot, irrespective of pellet count.
    for(const auto& Contact:Contacts)if(!Killed.Contains(Contact.Key))Effect(Source,TEXT("FX_VEPR12_Zeus_Hit"),Contact.Value,FRotator::ZeroRotator,false);
    for(auto* Enemy:Killed)Effect(Source,TEXT("FX_Lightning_Zeus_Kill"),Enemy->GetActorLocation(),FRotator::ZeroRotator,false);
    return Kills;
}
}
UHeliosChanZeusDefinition::UHeliosChanZeusDefinition()
{
    WeaponId=TEXT("cfm_chan_zeus");DisplayName=NSLOCTEXT("HeliosWeapons","ChanZeus","蝉·潜伏者 / 宙斯散弹枪");
    Description=NSLOCTEXT("HeliosWeapons","ChanZeusDescription","蝉潜伏者专属手臂与宙斯散弹枪。左键连续射击，R 换弹，G 展示。击败敌人随机播放原角色台词；可同时选择持宙斯的蝉伙伴。");
    WeaponClass=AHeliosChanZeusWeapon::StaticClass();const auto S=HeliosChanZeus::Tuning(false);
    MagazineSize=S.Magazine;ReloadSeconds=S.Reload;Damage=S.Damage;MinimumDamage=S.MinimumDamage;Range=S.Range;FalloffStart=S.Falloff;PelletCount=S.Pellets;SpreadHalfAngle=S.Spread;RefireSeconds=S.Interval;Recoil=.6f;
    AttachmentLocation=FVector::ZeroVector;AttachmentRotation=FRotator(0,-90,0);AttachmentScale=FVector::OneVector;
    SkeletalWeaponMesh=TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Helios/CFMChanZeus/SK_DiaoChanArms_FP.SK_DiaoChanArms_FP")));
    PreviewTexture=TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Helios/CFMChanZeus/Textures/T_Chan_TP_Preview.T_Chan_TP_Preview")));
}
AHeliosChanZeusWeapon::AHeliosChanZeusWeapon()
{
    bFullAuto=true;MagazineSize=12;SetExternalPresentation(true,TEXT("MuzzleFlashSocket"));
    Zeus=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Original Zeus first person"));Zeus->SetupAttachment(GetFirstPersonMesh());
    Zeus->SetCollisionEnabled(ECollisionEnabled::NoCollision);Zeus->SetOnlyOwnerSee(true);Zeus->SetCastShadow(false);Zeus->SetCanEverAffectNavigation(false);Zeus->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
    Voice=CreateDefaultSubobject<UHeliosSSXVoiceComponent>(TEXT("Original Chan BL voice"));
    GetFirstPersonMesh()->SetCastShadow(false);GetFirstPersonMesh()->SetBoundsScale(4);Zeus->SetBoundsScale(4);
    for(auto* M:{GetFirstPersonMesh(),Zeus.Get()}){M->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;M->bEnableUpdateRateOptimizations=false;}
}
void AHeliosChanZeusWeapon::ApplyDefinition(const UHeliosWeaponDefinition* D)
{
    Super::ApplyDefinition(D);ShotSettings=HeliosChanZeus::Tuning(HeliosWeaponHolderContext::IsCompanion(GetOwner()));RefireRate=ShotSettings.Interval;MagazineSize=ShotSettings.Magazine;
    GetFirstPersonMesh()->SetSkeletalMesh(D->SkeletalWeaponMesh.LoadSynchronous());Zeus->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Helios/CFMChanZeus/SK_Zeus_FP")));
    GetFirstPersonMesh()->SetRelativeLocationAndRotation(D->AttachmentLocation,D->AttachmentRotation);Zeus->SetRelativeTransform(FTransform::Identity);
    for(auto* M:{GetFirstPersonMesh(),Zeus.Get()}){M->SetAnimInstanceClass(UHeliosHandAnimInstance::StaticClass());M->AddTickPrerequisiteActor(this);if(auto* A=Cast<UHeliosHandAnimInstance>(M->GetAnimInstance())){A->bPreserveTransitionVelocity=false;A->BlendDuration=.025f;}}
    for(FName N:{FName("Idle"),FName("Move"),FName("Equip"),FName("ChangeClip"),FName("FireSingle"),FName("Fire"),FName("Fire0"),FName("Show")})
    {ArmsClips.Add(N,HeliosChanZeus::Animation(TEXT("A_DIAOCHAN_FP_"),N));GunClips.Add(N,HeliosChanZeus::Animation(TEXT("A_ZEUS_FP_"),N));}
    for(int32 Id:{12238,12239,18111,18014,18015,18016,18017,18018,18019,18020})ActionSounds.Add(Id,HeliosChanZeus::Sound(Id));
    HeliosChanZeus::InitializeVoice(Voice,HeliosWeaponHolderContext::IsCompanion(GetOwner()));FirstPersonModel->SetVisibility(false);ThirdPersonModel->SetVisibility(false);Pose();
}
void AHeliosChanZeusWeapon::ActivateWeapon(const FName& Tag)
{Super::ActivateWeapon(Tag);ShotSettings=HeliosChanZeus::Tuning(HeliosWeaponHolderContext::IsCompanion(GetOwner()));RefireRate=ShotSettings.Interval;MagazineSize=ShotSettings.Magazine;bActive=true;GetFirstPersonMesh()->SetRelativeLocationAndRotation(ActiveDefinition->AttachmentLocation,ActiveDefinition->AttachmentRotation);Zeus->SetRelativeTransform(FTransform::Identity);Effects.Add(HeliosChanZeus::AttachedEffect(this,Zeus,TEXT("FX_Zeus_Idle_FP"),true,.01f,TEXT("EffectSocket")));BeginAction(TEXT("Equip"),.9f);}
void AHeliosChanZeusWeapon::ClearEffects(){for(const auto& E:Effects)if(IsValid(E))E->Destroy();Effects.Reset();}
void AHeliosChanZeusWeapon::DeactivateWeapon(){bActive=false;CancelReload();ClearEffects();Voice->Stop();Super::DeactivateWeapon();}
void AHeliosChanZeusWeapon::EndPlay(const EEndPlayReason::Type Reason){ClearEffects();CancelSounds();Voice->Stop();Super::EndPlay(Reason);}
void AHeliosChanZeusWeapon::StartFiring(){if(bActive&&!bReload&&!HeliosWeaponHolderContext::IsDead(GetOwner())&&Action!=TEXT("Show"))Super::StartFiring();}
void AHeliosChanZeusWeapon::StartSpecialFire(){if(bActive&&!bReload&&!bIsFiring)BeginAction(TEXT("Show"),3.166667f);}
void AHeliosChanZeusWeapon::BeginReload(float Seconds){Super::BeginReload(Seconds);bReload=true;CancelSounds();BeginAction(TEXT("ChangeClip"),Seconds);}
void AHeliosChanZeusWeapon::CancelReload(){bReload=false;Action=NAME_None;CancelSounds();Super::CancelReload();Pose();}
bool AHeliosChanZeusWeapon::HandleOwnerCombatCue(FName Cue)
{if(Cue==TEXT("FatalHurt"))return true;if(Cue!=TEXT("Hurt")&&Cue!=TEXT("Death"))return false;if(!HeliosWeaponHolderContext::PlayCharacterCue(GetOwner(),Cue))HeliosChanZeus::CombatVoice(Voice,Cue);return true;}
void AHeliosChanZeusWeapon::CancelSounds(){for(const auto& C:SoundChannels)if(IsValid(C))C->Stop();SoundChannels.Reset();}
void AHeliosChanZeusWeapon::PlayActionSound(int32 Id,float Gain)
{if(auto* S=ActionSounds.FindRef(Id).Get())if(auto* C=(HeliosWeaponHolderContext::IsCompanion(GetOwner())?HeliosAudio::SpawnSoundAttached(S,GetOwner()->GetRootComponent(),NAME_None,FVector::ZeroVector,EAttachLocation::KeepRelativeOffset,true,Gain):HeliosAudio::SpawnSound2D(this,S,Gain)))SoundChannels.Add(C);}
void AHeliosChanZeusWeapon::BeginAction(FName Clip,float Duration)
{
    Action=Clip;ActionAge=0;ActionDuration=Duration;EventMask=0;Pose();
    // The resource contains these visual prefabs but no usable visual-event list.
    // Align phase one with action start, phase two with its original audio event.
    if(Clip==TEXT("Equip"))Effects.Add(HeliosChanZeus::AttachedEffect(this,Zeus,TEXT("FX_1P_VEPR12_Zeus_Equip01"),true,.01f,TEXT("EffectSocket")));
    if(Clip==TEXT("ChangeClip"))Effects.Add(HeliosChanZeus::AttachedEffect(this,Zeus,TEXT("FX_1P_VEPR12_Zeus_ChangeClip01"),true,.01f,TEXT("EffectSocket")));
    if(Clip==TEXT("Show"))Effects.Add(HeliosChanZeus::AttachedEffect(this,Zeus,TEXT("FX_1P_VEPR12_Zeus_Show01"),true,.01f,TEXT("EffectSocket")));
}
void AHeliosChanZeusWeapon::Pose()
{
    const FName Clip=Action.IsNone()?(PawnOwner&&PawnOwner->GetVelocity().Size2D()>20?FName("Move"):FName("Idle")):Action;
    float Time=Action.IsNone()?IdleAge:ActionAge;
    if(Action==TEXT("ChangeClip"))Time*=1.8f/FMath::Max(.01f,ActionDuration);
    if(auto* A=Cast<UHeliosHandAnimInstance>(GetFirstPersonMesh()->GetAnimInstance()))A->SetTimelinePose(ArmsClips.FindRef(Clip),Time,Action.IsNone());
    if(auto* A=Cast<UHeliosHandAnimInstance>(Zeus->GetAnimInstance()))A->SetTimelinePose(GunClips.FindRef(Clip),Time,Action.IsNone());
}
void AHeliosChanZeusWeapon::Tick(float Dt)
{
    if(HeliosWeaponHolderContext::IsDead(GetOwner())){StopFiring();CancelReload();ClearEffects();return;}
    Super::Tick(Dt);if(!bActive||GetWorld()->IsPaused())return;IdleAge+=Dt;ActionAge+=Dt;
    auto Event=[&](uint32 Bit,float At,int32 Id){if(!(EventMask&Bit)&&ActionAge>=At){EventMask|=Bit;PlayActionSound(Id,.8f);}};
    if(Action==TEXT("ChangeClip")){const float Rate=ActionDuration/1.8f;Event(1,.0151924f*Rate,18014);Event(2,.898197f*Rate,18020);Event(4,1.166878f*Rate,18019);}
    if(Action==TEXT("Equip"))Event(1,.044576f,18015);
    if(Action==TEXT("Show")){Event(1,0,18016);Event(2,1.048216f,18017);Event(4,2.433406f,18018);}
    auto Visual=[&](uint32 Bit,float At,const TCHAR* Name){if(!(EventMask&Bit)&&ActionAge>=At){EventMask|=Bit;Effects.Add(HeliosChanZeus::AttachedEffect(this,Zeus,Name,true,.01f,TEXT("EffectSocket")));}};
    if(Action==TEXT("Equip"))Visual(8,.044576f,TEXT("FX_1P_VEPR12_Zeus_Equip02"));
    if(Action==TEXT("ChangeClip"))Visual(8,.898197f*ActionDuration/1.8f,TEXT("FX_1P_VEPR12_Zeus_ChangeClip02"));
    if(Action==TEXT("Show"))Visual(8,1.048216f,TEXT("FX_1P_VEPR12_Zeus_Show02"));
    if(!Action.IsNone()&&ActionAge>=ActionDuration){if(Action==TEXT("ChangeClip"))++Reloads;Action=NAME_None;}
    Effects.RemoveAll([](const auto& E){return !IsValid(E);});SoundChannels.RemoveAll([](const auto& C){return !IsValid(C)||!C->IsPlaying();});Pose();
}
FVector AHeliosChanZeusWeapon::MuzzleLocation()const
{if(HeliosWeaponHolderContext::IsCompanion(GetOwner()))if(auto* C=GetOwner()->FindComponentByClass<UHeliosCharacterPresentationComponent>();C&&C->IsReady())return C->GetAnchorTransform(TEXT("Muzzle")).GetLocation();return Zeus->DoesSocketExist(TEXT("MuzzleFlashSocket"))?Zeus->GetSocketLocation(TEXT("MuzzleFlashSocket")):Zeus->GetComponentLocation()+PawnOwner->GetBaseAimRotation().Vector()*70;}
void AHeliosChanZeusWeapon::FireProjectile(const FVector& Target)
{
    if(!bActive||bReload||!PawnOwner||!WeaponOwner||CurrentBullets<=0||HeliosWeaponHolderContext::IsDead(GetOwner()))return;
    FVector View;FRotator Aim;PawnOwner->GetActorEyesViewPoint(View,Aim);
    int32 Hits=0;const int32 Kills=HeliosChanZeus::FirePellets(this,PawnOwner,HeliosWeaponHolderContext::FriendlyPlayer(GetOwner()),View,Aim.Vector(),MuzzleLocation(),ShotSettings,SpreadRandom,Hits);
    TrueKills+=Kills;HitPellets+=Hits;++ShotsFired;for(int32 I=0;I<Kills;++I)if(!HeliosWeaponHolderContext::PlayCharacterCue(GetOwner(),TEXT("Kill")))Voice->Play(TEXT("Kill"));
    BeginAction(CurrentBullets==1?FName("Fire0"):FName("FireSingle"),CurrentBullets==1?.1f:.166667f);PlayActionSound(HeliosWeaponHolderContext::IsCompanion(GetOwner())?12239:12238,.65f);PlayActionSound(18111,.65f);
    Effects.Add(HeliosChanZeus::Effect(this,HeliosWeaponHolderContext::IsCompanion(GetOwner())?TEXT("P_Gunfire_3P_Zeus"):TEXT("P_Gunfire_1P_Zeus"),MuzzleLocation(),Aim,!HeliosWeaponHolderContext::IsCompanion(GetOwner())));Effects.Add(HeliosChanZeus::Effect(this,HeliosWeaponHolderContext::IsCompanion(GetOwner())?TEXT("P_Gunfire_3P_Zeus_Lightning"):TEXT("P_Gunfire_1P_Zeus_Lightning"),MuzzleLocation(),Aim,!HeliosWeaponHolderContext::IsCompanion(GetOwner())));
    --CurrentBullets;bShotCommitted=true;WeaponOwner->UpdateWeaponHUD(CurrentBullets,MagazineSize);WeaponOwner->AddWeaponRecoil(FiringRecoil);
    UE_LOG(LogTemp,Display,TEXT("CHAN_ZEUS_SHOT source=player shot=%d hits=%d kills=%d ammo=%d muzzle=%s"),ShotsFired,Hits,Kills,CurrentBullets,*MuzzleLocation().ToCompactString());
}
