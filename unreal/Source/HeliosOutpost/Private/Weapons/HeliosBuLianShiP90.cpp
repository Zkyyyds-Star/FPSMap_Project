#include "Weapons/HeliosBuLianShiP90.h"
#include "Weapons/HeliosWeaponHolderContext.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Audio/HeliosAudio.h"
#include "Weapons/HeliosHandAnimInstance.h"
#include "Weapons/HeliosSSXVoice.h"
#include "Weapons/HeliosBuLianShiP90Effect.h"
#include "Weapons/HeliosP90SubstituteFireAudio.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Pets/HeliosPetDog.h"
#include "Player/HeliosArenaCharacter.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sound/SoundBase.h"
#include "HeliosCharacterFormVoiceSources.inl"

namespace HeliosBuLianShiP90
{
float Setting(bool Pet,const TCHAR* Key,float Default,float Min,float Max)
{
    FString Contents;const FString Path=FPaths::ProjectContentDir()/TEXT("Settings/战斗数值/步练师灵魂舞者P90双脊龙.ini");
    FConfigFile File;FString Value;
    if(FFileHelper::LoadFileToString(Contents,*Path))File.ProcessInputFileContents(Contents,Path);
    double Number=Default;
    if(File.GetString(Pet?TEXT("伙伴"):TEXT("玩家"),Key,Value))
    {double Parsed=0;if(LexTryParseString(Parsed,*Value)&&FMath::IsFinite(Parsed))Number=Parsed;}
    return FMath::Clamp(float(Number),Min,Max);
}
FHeliosP90Tuning Tuning(bool Pet)
{
    FHeliosP90Tuning S;S.Damage=Setting(Pet,TEXT("单发伤害"),Pet?14:24,0,100000);
    S.MinimumDamage=FMath::Min(S.Damage,Setting(Pet,TEXT("远端单发伤害"),Pet?5:9,0,100000));
    S.Range=Setting(Pet,TEXT("射程"),3500,100,20000);S.Falloff=Setting(Pet,TEXT("衰减起点"),1000,0,S.Range);
    S.Spread=Setting(Pet,TEXT("散布半角"),1,0,30);S.Interval=Setting(Pet,TEXT("射击间隔"),Pet?.15f:.08f,.03,10);
    S.Reload=Setting(Pet,TEXT("换弹时间"),2.f,.3,20);S.Magazine=FMath::RoundToInt(Setting(Pet,TEXT("弹匣容量"),50,1,100));
    S.Pellets=1;S.Recoil=Setting(Pet,TEXT("单发后坐"),.12f,0,10);return S;
}
UAnimSequence* Animation(const TCHAR* Prefix,FName Clip)
{const FString Name=FString(Prefix)+Clip.ToString();return LoadObject<UAnimSequence>(nullptr,*(TEXT("/Game/Helios/CFMBuLianShiP90/Animations/")+Name+TEXT(".")+Name));}
USoundBase* Sound(int32 Id)
{const FString Name=FString::Printf(TEXT("SFX_P90_%d"),Id);return LoadObject<USoundBase>(nullptr,*(TEXT("/Game/Helios/CFMBuLianShiP90/Audio/")+Name+TEXT(".")+Name));}
void InitializeVoice(UHeliosSSXVoiceComponent* Voice,bool Spatial)
{
    TArray<USoundBase*> Pool;
    for(const TCHAR* N:{TEXT("VO_BLS_28159"),TEXT("VO_BLS_28160_102522319"),TEXT("VO_BLS_28160_447817978"),TEXT("VO_BLS_28162_611822088"),TEXT("VO_BLS_28162_939189977"),TEXT("VO_BLS_28162_1054834849"),TEXT("VO_BLS_28162_1063521662"),TEXT("VO_BLS_28165_503219050"),TEXT("VO_BLS_28165_911309006"),TEXT("VO_BLS_28166_30646098"),TEXT("VO_BLS_28166_213516550"),TEXT("VO_BLS_28167_202989498"),TEXT("VO_BLS_28167_668306230"),TEXT("VO_BLS_28168_126626305"),TEXT("VO_BLS_28168_276843450"),TEXT("VO_BLS_28169_544122791"),TEXT("VO_BLS_28169_642041614"),TEXT("VO_BLS_28170_343569698"),TEXT("VO_BLS_28170_352018925"),TEXT("VO_BLS_28171_350066219"),TEXT("VO_BLS_28171_725456833"),TEXT("VO_BLS_28161_271448425"),TEXT("VO_BLS_28161_499898996")})
        if(auto* S=LoadObject<USoundBase>(nullptr,*(FString(TEXT("/Game/Helios/CFMBuLianShiP90/Audio/"))+N)))Pool.Add(S);
    TMap<FName,USoundBase*> Cues;
    Cues.Add(TEXT("Hurt"),LoadObject<USoundBase>(nullptr,TEXT("/Game/Helios/CFMBuLianShiP90/Audio/VO_BLS_28140_1119922")));
    Cues.Add(TEXT("Death"),LoadObject<USoundBase>(nullptr,TEXT("/Game/Helios/CFMBuLianShiP90/Audio/VO_BLS_28139_96003202")));
    Voice->InitializeSources(Spatial,Cues,Pool);
    UE_LOG(LogTemp,Display,TEXT("BLS_VOICE identity=BuLianShiSP sources=%d spatial=%d"),Pool.Num(),Spatial);
}
void CombatVoice(UHeliosSSXVoiceComponent* Voice,FName Cue)
{if(Voice)Voice->Play(Cue);}
void InitializeFormVoice(UHeliosSSXVoiceComponent* Voice,FName Variant,bool Spatial)
{HeliosCharacterFormVoice::Initialize(TEXT("cfm_bulianshi"),Variant.IsNone()?FName("SP"):Variant,Voice,Spatial);}
AHeliosBuLianShiP90Effect* Effect(AActor* Owner,const TCHAR* Name,const FVector& At,const FRotator& Rotation,bool FP)
{return AHeliosBuLianShiP90Effect::Spawn(Owner,Name,At,Rotation,FP);}
AHeliosBuLianShiP90Effect* AttachedEffect(AActor* Owner,USceneComponent* Parent,const TCHAR* Name,bool FP,float Scale,FName Socket)
{
    auto* E=Effect(Owner,Name,Parent->GetComponentLocation(),Parent->GetComponentRotation(),FP);
    if(E){E->AttachToComponent(Parent,FAttachmentTransformRules::SnapToTargetNotIncludingScale,Socket);E->SetActorRelativeRotation(FRotator(0,90,0));E->SetActorRelativeScale3D(FVector(Scale));E->ResetBirthAnchors();E->AddTickPrerequisiteComponent(Parent);E->SetTickGroup(TG_PostUpdateWork);}
    return E;
}
int32 FirePellets(AActor* Source,APawn* Instigator,AActor* Friendly,const FVector& View,const FVector& Direction,const FVector& Muzzle,const FHeliosP90Tuning& S,FRandomStream& Random,int32& Hits)
{
    int32 Kills=0;Hits=0;if(!Source||!Source->GetWorld())return 0;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(BuLianShiP90Pellets),true,Source);Q.AddIgnoredActor(Instigator);Q.AddIgnoredActor(Friendly);
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
        if(Source->ActorHasTag(TEXT("Friendly"))&&FParse::Param(FCommandLine::Get(),TEXT("HeliosMultiPetQA")))UE_LOG(LogTemp,Display,TEXT("P90_QA_TRACE source=%s view=%s muzzle=%s camera=%s final=%s blocked=%d"),*Source->GetName(),*View.ToCompactString(),*Muzzle.ToCompactString(),*GetNameSafe(Camera.GetActor()),*GetNameSafe(Hit.GetActor()),Blocked);
        auto* Enemy=Cast<AHeliosEnemyActor>(Hit.GetActor());
        if(!Enemy||Enemy->IsEnemyDead()||Enemy->ActorHasTag(TEXT("Friendly")))continue;
        const float Distance=FVector::Distance(View,Hit.ImpactPoint);const float Alpha=FMath::Clamp((Distance-S.Falloff)/FMath::Max(1.f,S.Range-S.Falloff),0.f,1.f);
        const float Before=Enemy->CurrentHP;
        const float Applied=UGameplayStatics::ApplyPointDamage(Enemy,FMath::Lerp(S.Damage,S.MinimumDamage,Alpha),Dir,Hit,Instigator?Instigator->GetController():nullptr,Source,UDamageType::StaticClass());
        if(Applied>0&&Enemy->CurrentHP<Before)
        {++Hits;Contacts.FindOrAdd(Enemy)=Hit.ImpactPoint;if(Enemy->IsEnemyDead()&&!Killed.Contains(Enemy)){Killed.Add(Enemy);++Kills;}}
    }
    // One source impact/kill effect per enemy per shot, irrespective of pellet count.
    for(auto* Enemy:Killed)
    {
        // Original burst samples already cover body/head heights above the
        // character origin. UE's actor origin is the capsule centre, so use its
        // foot plane rather than adding a second half-height to every layer.
        const FVector Feet=Enemy->GetActorLocation()-FVector(0,0,Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        Effect(Source,TEXT("FX_P90_ShuangJiLong_Kill"),Feet,FRotator(0,Direction.Rotation().Yaw,0),false);
    }
    return Kills;
}
}
UHeliosBuLianShiP90Definition::UHeliosBuLianShiP90Definition()
{
    WeaponId=TEXT("cfm_bulianshi_p90");DisplayName=NSLOCTEXT("HeliosWeapons","BuLianShiP90","步练师·灵魂舞者 / P90双脊龙");
    Description=NSLOCTEXT("HeliosWeapons","BuLianShiP90Description","步练师灵魂舞者原手臂与P90双脊龙。左键连续射击，R 换弹，G 展示。击败敌人随机播放原角色台词；可同时选择持双脊龙的步练师伙伴。");
    WeaponClass=AHeliosBuLianShiP90Weapon::StaticClass();const auto S=HeliosBuLianShiP90::Tuning(false);
    MagazineSize=S.Magazine;ReloadSeconds=S.Reload;Damage=S.Damage;MinimumDamage=S.MinimumDamage;Range=S.Range;FalloffStart=S.Falloff;PelletCount=S.Pellets;SpreadHalfAngle=S.Spread;RefireSeconds=S.Interval;Recoil=S.Recoil;
    AttachmentLocation=FVector::ZeroVector;AttachmentRotation=FRotator(0,-90,0);AttachmentScale=FVector::OneVector;
    SkeletalWeaponMesh=TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Helios/CFMBuLianShiP90/SK_BuLianShiArms_FP.SK_BuLianShiArms_FP")));
    PreviewTexture=TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Helios/CFMBuLianShiP90/Textures/T_BuLianShi_TP_Preview.T_BuLianShi_TP_Preview")));
}
AHeliosBuLianShiP90Weapon::AHeliosBuLianShiP90Weapon()
{
    bFullAuto=true;MagazineSize=50;SetExternalPresentation(true,TEXT("MuzzleFlashSocket"));
    P90=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Original P90 first person"));P90->SetupAttachment(GetFirstPersonMesh());
    P90->SetCollisionEnabled(ECollisionEnabled::NoCollision);P90->SetOnlyOwnerSee(true);P90->SetCastShadow(false);P90->SetCanEverAffectNavigation(false);P90->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
    GunParts.Add(P90);
    for(const TCHAR* Part:{TEXT("P90B"),TEXT("P90Shell"),TEXT("P90Scales"),TEXT("P90Eyes")})
    {
        auto* M=CreateDefaultSubobject<USkeletalMeshComponent>(FName(Part));M->SetupAttachment(GetFirstPersonMesh());
        M->SetCollisionEnabled(ECollisionEnabled::NoCollision);M->SetOnlyOwnerSee(true);M->SetCastShadow(false);M->SetCanEverAffectNavigation(false);M->SetBoundsScale(4);M->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
        M->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;M->bEnableUpdateRateOptimizations=false;GunParts.Add(M);
    }
    Voice=CreateDefaultSubobject<UHeliosSSXVoiceComponent>(TEXT("Original BuLianShi SP voice"));
    FireAudio=CreateDefaultSubobject<UHeliosP90SubstituteFireAudio>(TEXT("Approved ordinary P90 first person fire"));
    GetFirstPersonMesh()->SetCastShadow(false);GetFirstPersonMesh()->SetBoundsScale(4);P90->SetBoundsScale(4);
    for(auto* M:{GetFirstPersonMesh(),P90.Get()}){M->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;M->bEnableUpdateRateOptimizations=false;}
}
void AHeliosBuLianShiP90Weapon::ApplyDefinition(const UHeliosWeaponDefinition* D)
{
    Super::ApplyDefinition(D);ShotSettings=HeliosBuLianShiP90::Tuning(HeliosWeaponHolderContext::IsCompanion(GetOwner()));RefireRate=ShotSettings.Interval;MagazineSize=ShotSettings.Magazine;
    FireAudio->Initialize(HeliosWeaponHolderContext::IsCompanion(GetOwner()),ShotSettings.Interval);
    GetFirstPersonMesh()->SetSkeletalMesh(D->SkeletalWeaponMesh.LoadSynchronous());P90->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Helios/CFMBuLianShiP90/SK_P90A_FP")));
    GetFirstPersonMesh()->SetRelativeLocationAndRotation(D->AttachmentLocation,D->AttachmentRotation);P90->SetRelativeTransform(FTransform::Identity);
    for(auto* M:{GetFirstPersonMesh(),P90.Get()}){M->SetAnimInstanceClass(UHeliosHandAnimInstance::StaticClass());M->AddTickPrerequisiteActor(this);if(auto* A=Cast<UHeliosHandAnimInstance>(M->GetAnimInstance())){A->bPreserveTransitionVelocity=false;A->BlendDuration=.025f;}}
    for(FName N:{FName("Idle"),FName("Move"),FName("Equip"),FName("ChangeClip"),FName("FireSingle"),FName("Fire"),FName("Fire0"),FName("Show")})
    {ArmsClips.Add(N,HeliosBuLianShiP90::Animation(TEXT("A_BLS_FP_"),N));for(const TCHAR* Part:{TEXT("P90A"),TEXT("P90B"),TEXT("P90Shell"),TEXT("P90Scales"),TEXT("P90Eyes")})GunClips.Add(FName(FString(Part)+N.ToString()),HeliosBuLianShiP90::Animation(*(FString(TEXT("A_"))+Part+TEXT("_FP_")),N));}
    for(int32 I=1;I<GunParts.Num();++I)
    {
        auto* M=GunParts[I].Get();const FString N=TEXT("SK_")+M->GetName()+TEXT("_FP");M->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,*(FString(TEXT("/Game/Helios/CFMBuLianShiP90/"))+N)));M->SetRelativeTransform(FTransform::Identity);
        M->SetAnimInstanceClass(UHeliosHandAnimInstance::StaticClass());M->AddTickPrerequisiteActor(this);
        if(auto* A=Cast<UHeliosHandAnimInstance>(M->GetAnimInstance())){A->bPreserveTransitionVelocity=false;A->BlendDuration=.025f;}
    }
    for(int32 Id:{20670,20671,20672,20673,20674,20675})ActionSounds.Add(Id,HeliosBuLianShiP90::Sound(Id));
    HeliosBuLianShiP90::InitializeVoice(Voice,HeliosWeaponHolderContext::IsCompanion(GetOwner()));FirstPersonModel->SetVisibility(false);ThirdPersonModel->SetVisibility(false);Pose();
}
void AHeliosBuLianShiP90Weapon::ActivateWeapon(const FName& Tag)
{Super::ActivateWeapon(Tag);ShotSettings=HeliosBuLianShiP90::Tuning(HeliosWeaponHolderContext::IsCompanion(GetOwner()));RefireRate=ShotSettings.Interval;MagazineSize=ShotSettings.Magazine;bActive=true;GetFirstPersonMesh()->SetRelativeLocationAndRotation(ActiveDefinition->AttachmentLocation,ActiveDefinition->AttachmentRotation);P90->SetRelativeTransform(FTransform::Identity);for(const TCHAR* Bone:{TEXT("Tou"),TEXT("Mao_L02"),TEXT("Mao_L03"),TEXT("Mao_R02"),TEXT("Mao_R03"),TEXT("Back01"),TEXT("Back02"),TEXT("Back03"),TEXT("Back04"),TEXT("Back05"),TEXT("Back06"),TEXT("Back07"),TEXT("Back09"),TEXT("Back10")})Effects.Add(HeliosBuLianShiP90::AttachedEffect(this,P90,*(FString(TEXT("FX_1P_P90_ShuangJiLong_"))+Bone+TEXT("_Idle_FP")),true,.01f,FName(Bone)));
Effects.Add(HeliosBuLianShiP90::AttachedEffect(this,GunParts[1],TEXT("FX_1P_P90_ShuangJiLong_Bone_RightHand_Idle_FP"),true,.01f,TEXT("bone_RightHand")));BeginAction(TEXT("Equip"),.800001f);}
void AHeliosBuLianShiP90Weapon::ClearEffects(){for(const auto& E:Effects)if(IsValid(E))E->Destroy();Effects.Reset();}
void AHeliosBuLianShiP90Weapon::DeactivateWeapon(){bActive=false;CancelReload();ClearEffects();Voice->Stop();Super::DeactivateWeapon();}
void AHeliosBuLianShiP90Weapon::EndPlay(const EEndPlayReason::Type Reason){ClearEffects();CancelSounds();Voice->Stop();Super::EndPlay(Reason);}
void AHeliosBuLianShiP90Weapon::StartFiring(){if(bActive&&!bReload&&!HeliosWeaponHolderContext::IsDead(GetOwner())&&Action!=TEXT("Show"))Super::StartFiring();}
void AHeliosBuLianShiP90Weapon::StartSpecialFire(){if(bActive&&!bReload&&!bIsFiring)BeginAction(TEXT("Show"),10.566668f);}
void AHeliosBuLianShiP90Weapon::BeginReload(float Seconds){Super::BeginReload(Seconds);bReload=true;CancelSounds();BeginAction(TEXT("ChangeClip"),Seconds);}
void AHeliosBuLianShiP90Weapon::CancelReload(){bReload=false;Action=NAME_None;CancelSounds();Super::CancelReload();Pose();}
bool AHeliosBuLianShiP90Weapon::HandleOwnerCombatCue(FName Cue)
{if(Cue==TEXT("FatalHurt"))return true;if(Cue!=TEXT("Hurt")&&Cue!=TEXT("Death"))return false;if(Cue==TEXT("Death"))StopFiring();if(!HeliosWeaponHolderContext::PlayCharacterCue(GetOwner(),Cue))HeliosBuLianShiP90::CombatVoice(Voice,Cue);return true;}
void AHeliosBuLianShiP90Weapon::CancelSounds(){for(const auto& C:SoundChannels)if(IsValid(C))C->Stop();SoundChannels.Reset();}
void AHeliosBuLianShiP90Weapon::PlayActionSound(int32 Id,float Gain)
{if(auto* S=ActionSounds.FindRef(Id).Get())if(auto* C=(HeliosWeaponHolderContext::IsCompanion(GetOwner())?HeliosAudio::SpawnSoundAttached(S,GetOwner()->GetRootComponent(),NAME_None,FVector::ZeroVector,EAttachLocation::KeepRelativeOffset,true,Gain):HeliosAudio::SpawnSound2D(this,S,Gain)))SoundChannels.Add(C);}
void AHeliosBuLianShiP90Weapon::BeginAction(FName Clip,float Duration)
{
    Action=Clip;ActionAge=0;ActionDuration=Duration;EventMask=0;Pose();
    if(Clip==TEXT("Equip"))Effects.Add(HeliosBuLianShiP90::AttachedEffect(this,GunParts[1],TEXT("FX_1P_P90_ShuangJiLong_Bone_RightHand_Equip"),true,.01f,TEXT("bone_RightHand")));
    if(Clip==TEXT("ChangeClip"))
    {
        Effects.Add(HeliosBuLianShiP90::AttachedEffect(this,P90,TEXT("FX_1P_P90_ShuangJiLong_ChangeClip"),true,.01f));
        Effects.Add(HeliosBuLianShiP90::AttachedEffect(this,P90,TEXT("FX_1P_P90_ShuangJiLong_Back10_ChangeClip"),true,.01f,TEXT("Back10")));
        Effects.Add(HeliosBuLianShiP90::AttachedEffect(this,GunParts[1],TEXT("FX_1P_P90_ShuangJiLong_Bone_RightHand_ChangeClip"),true,.01f,TEXT("bone_RightHand")));
    }
    if(Clip==TEXT("Show"))
    {
        Effects.Add(HeliosBuLianShiP90::AttachedEffect(this,P90,TEXT("FX_1P_P90_ShuangJiLong_Show"),true,.01f));
        Effects.Add(HeliosBuLianShiP90::AttachedEffect(this,P90,TEXT("FX_1P_P90_ShuangJiLong_Tou_Show"),true,.01f,TEXT("Tou")));
    }
}
void AHeliosBuLianShiP90Weapon::Pose()
{
    const FName Clip=Action.IsNone()?(PawnOwner&&PawnOwner->GetVelocity().Size2D()>20?FName("Move"):FName("Idle")):Action;
    float Time=Action.IsNone()?IdleAge:ActionAge;
    if(Action==TEXT("ChangeClip"))Time*=2.f/FMath::Max(.01f,ActionDuration);
    if(auto* A=Cast<UHeliosHandAnimInstance>(GetFirstPersonMesh()->GetAnimInstance()))A->SetTimelinePose(ArmsClips.FindRef(Clip),Time,Action.IsNone());
    for(int32 I=0;I<GunParts.Num();++I)if(auto* A=Cast<UHeliosHandAnimInstance>(GunParts[I]->GetAnimInstance()))A->SetTimelinePose(GunClips.FindRef(FName((I==0?FString(TEXT("P90A")):GunParts[I]->GetName())+Clip.ToString())),Time,Action.IsNone());
}
void AHeliosBuLianShiP90Weapon::Tick(float Dt)
{
    if(HeliosWeaponHolderContext::IsDead(GetOwner())){StopFiring();CancelReload();ClearEffects();return;}
    Super::Tick(Dt);if(!bActive||GetWorld()->IsPaused())return;IdleAge+=Dt;ActionAge+=Dt;
    auto Event=[&](uint32 Bit,float At,int32 Id){if(!(EventMask&Bit)&&ActionAge>=At){EventMask|=Bit;PlayActionSound(Id,.8f);}};
    if(Action==TEXT("ChangeClip")){const float Rate=ActionDuration/2.f;Event(1,.022322832f*Rate,20671);Event(2,1.145444274f*Rate,20670);}
    if(Action==TEXT("Equip"))Event(1,.02511476f,20672);
    if(Action==TEXT("Show")){Event(1,0,20673);Event(2,4.0122504f,20674);Event(4,7.181652f,20675);}
    if(!Action.IsNone()&&ActionAge>=ActionDuration){if(Action==TEXT("ChangeClip"))++Reloads;Action=NAME_None;}
    Effects.RemoveAll([](const auto& E){return !IsValid(E);});SoundChannels.RemoveAll([](const auto& C){return !IsValid(C)||!C->IsPlaying();});Pose();
}
FVector AHeliosBuLianShiP90Weapon::MuzzleLocation()const
{
    if(HeliosWeaponHolderContext::IsCompanion(GetOwner()))if(auto* C=GetOwner()->FindComponentByClass<UHeliosCharacterPresentationComponent>();C&&C->IsReady())return C->GetAnchorTransform(TEXT("Muzzle")).GetLocation();
    return P90->GetSocketLocation(TEXT("MuzzleFlashSocket"));
}
void AHeliosBuLianShiP90Weapon::FireProjectile(const FVector& Target)
{
    if(!bActive||bReload||!PawnOwner||!WeaponOwner||CurrentBullets<=0||HeliosWeaponHolderContext::IsDead(GetOwner()))return;
    FVector View;FRotator Aim;PawnOwner->GetActorEyesViewPoint(View,Aim);
    int32 Hits=0;const int32 Kills=HeliosBuLianShiP90::FirePellets(this,PawnOwner,HeliosWeaponHolderContext::FriendlyPlayer(GetOwner()),View,Aim.Vector(),MuzzleLocation(),ShotSettings,SpreadRandom,Hits);
    TrueKills+=Kills;HitPellets+=Hits;++ShotsFired;for(int32 I=0;I<Kills;++I)if(!HeliosWeaponHolderContext::PlayCharacterCue(GetOwner(),TEXT("Kill")))Voice->Play(TEXT("Kill"));
    FireAudio->PlayShot(MuzzleLocation());
    BeginAction(CurrentBullets==1?FName("Fire0"):FName("Fire"),CurrentBullets==1?.3f:.066667f);
    if(auto* Flash=HeliosBuLianShiP90::Effect(this,HeliosWeaponHolderContext::IsCompanion(GetOwner())?TEXT("FX_3P_Gunfire_P90_ShuangJiLong"):TEXT("FX_1P_Gunfire_P90_ShuangJiLong"),MuzzleLocation(),Aim,!HeliosWeaponHolderContext::IsCompanion(GetOwner())))
    {
        if(!HeliosWeaponHolderContext::IsCompanion(GetOwner()))
        {Flash->AttachToComponent(P90,FAttachmentTransformRules::KeepWorldTransform,TEXT("MuzzleFlashSocket"));Flash->FollowLocalViewTranslation();}
        Effects.Add(Flash);
    }
    --CurrentBullets;bShotCommitted=true;WeaponOwner->UpdateWeaponHUD(CurrentBullets,MagazineSize);WeaponOwner->AddWeaponRecoil(FiringRecoil);
    UE_LOG(LogTemp,Display,TEXT("BLS_P90_SHOT source=player shot=%d hits=%d kills=%d ammo=%d muzzle=%s"),ShotsFired,Hits,Kills,CurrentBullets,*MuzzleLocation().ToCompactString());
}
