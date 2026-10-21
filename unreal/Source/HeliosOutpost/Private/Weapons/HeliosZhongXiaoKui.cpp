#include "Weapons/HeliosZhongXiaoKui.h"
#include "Weapons/HeliosWeaponHolderContext.h"
#include "GameFramework/Character.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Weapons/HeliosHunterDamageFeedback.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Weapons/HeliosSunShangXiang.h"
#include "Weapons/HeliosSSXVoice.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Weapons/HeliosHandAnimInstance.h"
#include "Weapons/HeliosZXKProjectile.h"
#include "Engine/StaticMesh.h"
#include "Components/AudioComponent.h"
#include "Player/HeliosArenaCharacter.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Combat/HeliosCombatTempoSettings.h"
UHeliosZXKWaveDepthComponent::UHeliosZXKWaveDepthComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bStartWithTickEnabled=false;
    PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
}
void UHeliosZXKWaveDepthComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(Dt,TickType,TickFunction);
    if(bRenderCustomDepth)if(auto* Source=Cast<USkeletalMeshComponent>(GetAttachParent()))
    {
        CopyPoseFromSkeletalComponent(Source);RefreshBoneTransforms();
        if(FParse::Param(FCommandLine::Get(),TEXT("HeliosAsyncMotionCapture")))
            UE_LOG(LogTemp,Display,TEXT("ZXK_DEPTH_SYNC frame=%llu hand_error_cm=%.6f component_error_cm=%.6f source_revision=%u"),GFrameCounter,FVector::Distance(GetSocketLocation(TEXT("b_RightHand")),Source->GetSocketLocation(TEXT("b_RightHand"))),FVector::Distance(GetComponentLocation(),Source->GetComponentLocation()),Source->GetBoneTransformRevisionNumber());
    }
}
namespace HeliosZXK
{
UAnimSequence* Animation(bool FP,FName Name)
{
    const FString N=FString::Printf(TEXT("A_ZXK_%s_%s"),FP?TEXT("FP"):TEXT("TP"),*Name.ToString());
    return LoadObject<UAnimSequence>(nullptr,*(TEXT("/Game/Helios/CFMZhongXiaoKui/Animations/")+N+TEXT(".")+N));
}
void Sample(USkeletalMeshComponent* Mesh,UAnimSequence* Clip,float Time,bool Loop)
{
    if(!Mesh||!Clip)return;
    if(auto* Hand=Cast<UHeliosHandAnimInstance>(Mesh->GetAnimInstance())){Hand->SetTimelinePose(Clip,Time,Loop);Mesh->TickAnimation(0,false);Mesh->RefreshBoneTransforms();return;}
    auto* Node=Mesh->GetSingleNodeInstance();
    if(!Node||Node->GetCurrentAsset()!=Clip){Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);Mesh->SetAnimation(Clip);Node=Mesh->GetSingleNodeInstance();}
    if(!Node)return;Node->SetPlaying(false);Node->SetLooping(Loop);
    const float Length=FMath::Max(Clip->GetPlayLength(),KINDA_SMALL_NUMBER);
    Node->SetPosition(Loop?FMath::Fmod(FMath::Max(Time,0.f),Length):FMath::Clamp(Time,0.f,Length),false);
    Mesh->TickAnimation(0,false);Mesh->RefreshBoneTransforms();
}
}
UHeliosZhongXiaoKuiDefinition::UHeliosZhongXiaoKuiDefinition()
{
    WeaponId=TEXT("cfm_zhongxiaokui");DisplayName=FText::FromString(TEXT("钟小葵·生化猎手"));
    Description=FText::FromString(TEXT("唐刀、强化唐刀与飞镖。Q 变身，V 切换飞镖，G 技能，B形态右键剑气，R 装填；可与战斗伙伴同时使用。"));
    WeaponClass=AHeliosZhongXiaoKuiWeapon::StaticClass();MagazineSize=1;Damage=100;MinimumDamage=100;Range=260;RefireSeconds=1;
    SkeletalWeaponMesh=TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Helios/CFMZhongXiaoKui/SK_ZhongXiaoKui_FP.SK_ZhongXiaoKui_FP")));
}
AHeliosZhongXiaoKuiWeapon::AHeliosZhongXiaoKuiWeapon()
{
    PrimaryActorTick.bCanEverTick=true;bFullAuto=false;MagazineSize=1;
    Voice=CreateDefaultSubobject<UHeliosSSXVoiceComponent>(TEXT("Original HunterOni voice"));
    DamageFeedback=CreateDefaultSubobject<UHeliosHunterDamageFeedback>(TEXT("Player damage feedback07"));DamageFeedback->bZXK=true;
    Dart=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Original FP flying dart"));Dart->SetupAttachment(GetFirstPersonMesh(),TEXT("feidao"));Dart->SetCollisionEnabled(ECollisionEnabled::NoCollision);Dart->SetCastShadow(false);Dart->SetOnlyOwnerSee(true);Dart->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);Dart->SetVisibility(false);
    FirstPersonModel->SetVisibility(false);ThirdPersonModel->SetVisibility(false);
    auto* Mesh=GetFirstPersonMesh();Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetCastShadow(false);Mesh->SetOnlyOwnerSee(true);
    Mesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);Mesh->SetBoundsScale(4);
    Mesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    WaveSelfDepth=CreateDefaultSubobject<UHeliosZXKWaveDepthComponent>(TEXT("Native wave self depth only"));
    WaveSelfDepth->SetupAttachment(Mesh);WaveSelfDepth->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WaveSelfDepth->SetCastShadow(false);WaveSelfDepth->SetOnlyOwnerSee(true);
    WaveSelfDepth->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
    WaveSelfDepth->SetRenderInMainPass(false);WaveSelfDepth->SetRenderInDepthPass(false);
    WaveSelfDepth->SetBoundsScale(4);WaveSelfDepth->bEnableUpdateRateOptimizations=false;
    WaveSelfDepth->AddTickPrerequisiteComponent(Mesh);
}
void AHeliosZhongXiaoKuiWeapon::ApplyDefinition(const UHeliosWeaponDefinition* D)
{
    const bool Pet=HeliosWeaponHolderContext::IsCompanion(GetOwner());
    if(bWaveLateView){for(int32 I=0;I<NormalViewMaterials.Num();++I)GetFirstPersonMesh()->SetMaterial(I,NormalViewMaterials[I]);bWaveLateView=false;}
    Super::ApplyDefinition(D);FirstPersonModel->SetVisibility(false);ThirdPersonModel->SetVisibility(false);
    GetFirstPersonMesh()->SetSkeletalMesh(Pet?LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Helios/CFMZhongXiaoKui/SK_ZhongXiaoKui_TP.SK_ZhongXiaoKui_TP")):D->SkeletalWeaponMesh.LoadSynchronous());GetFirstPersonMesh()->SetAnimInstanceClass(UHeliosHandAnimInstance::StaticClass());
    for(int32 I=0;I<GetFirstPersonMesh()->GetNumMaterials();++I)
    {
        if(Pet||FParse::Param(FCommandLine::Get(),TEXT("HeliosOriginalFPMaterialsQA")))continue;
        auto* Original=GetFirstPersonMesh()->GetMaterial(I);if(!Original||Original->GetName().EndsWith(TEXT("_ClearFP")))continue;
        const FString Name=Original->GetName()+TEXT("_ClearFP");
        if(auto* Clear=LoadObject<UMaterialInterface>(nullptr,*(TEXT("/Game/Helios/CFMZhongXiaoKui/Materials/Clarity/")+Name+TEXT(".")+Name)))
        {
            GetFirstPersonMesh()->SetMaterial(I,Clear);
        }
    }
    if(auto* Hand=Cast<UHeliosHandAnimInstance>(GetFirstPersonMesh()->GetAnimInstance()))Hand->bPreserveTransitionVelocity=false;
    GetFirstPersonMesh()->AddTickPrerequisiteActor(this);
    GetFirstPersonMesh()->bEnableUpdateRateOptimizations=false;
    NormalViewMaterials.Reset();WaveViewMaterials.Reset();bWaveLateView=false;
    for(int32 I=0;I<GetFirstPersonMesh()->GetNumMaterials();++I)
    {
        auto* Base=GetFirstPersonMesh()->GetMaterial(I);NormalViewMaterials.Add(Base);
        const FString Name=Base->GetName()+TEXT("_WaveLate");
        auto* Late=LoadObject<UMaterialInterface>(nullptr,*(TEXT("/Game/Helios/CFMZhongXiaoKui/Materials/Clarity/")+Name+TEXT(".")+Name));
        WaveViewMaterials.Add(Late?Late:Base);
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosZXKNoBoneBlurQA")))
    {GetFirstPersonMesh()->bPerBoneMotionBlur=false;GetFirstPersonMesh()->MarkRenderStateDirty();}
    WaveSelfDepth->SetSkeletalMesh(D->SkeletalWeaponMesh.LoadSynchronous());
    for(int32 I=0;I<NormalViewMaterials.Num();++I)WaveSelfDepth->SetMaterial(I,NormalViewMaterials[I]);
    ResidentEffectLibrary=LoadObject<UHeliosSSXEffectLibrary>(nullptr,TEXT("/Game/Helios/CFMZhongXiaoKui/Effects/DA_ZXK_Effects.DA_ZXK_Effects"));
    if(ResidentEffectLibrary)ResidentEffectLibrary->GetParsedRecipe();
    GetFirstPersonMesh()->SetRelativeLocation(FVector::ZeroVector);GetFirstPersonMesh()->SetRelativeRotation(FRotator(0,-90,0));
    for(const TCHAR* N:{TEXT("Idle"),TEXT("Move"),TEXT("Equip"),TEXT("FireSlightFirst"),TEXT("FireSlightSecond"),TEXT("FireSlightThird"),TEXT("First_Recover"),TEXT("Second_Recover"),TEXT("Third_Recover"),TEXT("FireHeavy")})Clips.Add(N,HeliosZXK::Animation(true,N));
    HeliosZXK::InitializeVoice(Voice,Pet);Pose();
    for(const TCHAR* N:{TEXT("Skill01"),TEXT("Show"),TEXT("Recover_Gai"),TEXT("B_Idle"),TEXT("B_Move"),TEXT("B_FireSlightFirst"),TEXT("B_FireSlightSecond"),TEXT("B_FireSlightThird"),TEXT("B_FireSlightFourth"),TEXT("B_FireSlightFifth"),TEXT("B_First_Recover"),TEXT("B_Second_Recover"),TEXT("B_Third_Recover"),TEXT("B_FireSlightFourth_Recove"),TEXT("B_FireSlightFifth_Recove"),TEXT("B_Heavry"),TEXT("B_Skill01"),TEXT("B_Skill02"),TEXT("C_Idle"),TEXT("C_Move"),TEXT("C_Equip"),TEXT("C_Fire01"),TEXT("C_Fire01_Recove"),TEXT("C_Fire02"),TEXT("C_Fire02_Recove"),TEXT("C_ChangeClip"),TEXT("C_Change_BToC"),TEXT("C_Change_CToB"),TEXT("Change_AToB"),TEXT("Change_BToA")})Clips.Add(N,HeliosZXK::Animation(true,N));
    if(Pet)for(const TCHAR* N:{TEXT("Idle"),TEXT("Run_Fwd"),TEXT("Run_Bwd"),TEXT("Run_Lt"),TEXT("Run_Rt"),TEXT("Hit"),TEXT("Dead"),TEXT("Att01"),TEXT("Att02"),TEXT("Att03"),TEXT("Att01_Recover"),TEXT("Att02_Recover"),TEXT("Att03_Recover")})Clips.Add(N,HeliosZXK::Animation(false,N));
    Dart->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Helios/CFMZhongXiaoKui/SM_ZXK_Feidao_FP.SM_ZXK_Feidao_FP")));
    // Blender's armature unit conversion gives attachment sockets scale 100;
    // The raw static dart is in centimeters; retain the source clip's animated scale.
    Dart->SetRelativeScale3D(FVector(.01f));
}
void AHeliosZhongXiaoKuiWeapon::ActivateWeapon(const FName& Tag){Super::ActivateWeapon(Tag);GetFirstPersonMesh()->SetRelativeLocation(FVector::ZeroVector);GetFirstPersonMesh()->SetRelativeRotation(FRotator(0,-90,0));bActive=true;Form=0;DartAmmo=20;Cancel();Pose();}
void AHeliosZhongXiaoKuiWeapon::DeactivateWeapon(){bActive=false;DamageFeedback->Clear();Cancel();Dart->SetVisibility(false);Super::DeactivateWeapon();}
void AHeliosZhongXiaoKuiWeapon::Cancel(){WaveSelfDepth->SetRenderCustomDepth(false);WaveSelfDepth->SetComponentTickEnabled(false);if(bWaveLateView){for(int32 I=0;I<NormalViewMaterials.Num();++I)GetFirstPersonMesh()->SetMaterial(I,NormalViewMaterials[I]);bWaveLateView=false;}WaveViewTailEnd=0;Age=-1;bHeld=false;bWaveHeld=false;bCommitted=false;bQueuedRanged=false;PendingForm=Form;Combo=0;IdleAge=0;Voice->Stop();for(UAudioComponent* Channel:ActionChannels)if(IsValid(Channel))Channel->Stop();ActionChannels.Reset();for(AHeliosSSXEffect* Effect:Effects)if(IsValid(Effect))Effect->Destroy();Effects.Reset();for(AHeliosZXKProjectile* Projectile:Projectiles)if(IsValid(Projectile))Projectile->Destroy();Projectiles.Reset();}
void AHeliosZhongXiaoKuiWeapon::EndPlay(const EEndPlayReason::Type Reason){Cancel();Super::EndPlay(Reason);}
void AHeliosZhongXiaoKuiWeapon::CancelReload(){Cancel();Super::CancelReload();}
bool AHeliosZhongXiaoKuiWeapon::HandleOwnerCombatCue(FName Cue)
{
    if(Cue==TEXT("FatalHurt"))return true;
    if(Cue!=TEXT("Hurt")&&Cue!=TEXT("Death"))return false;
    if(Cue==TEXT("Death"))Cancel();
    if(!HeliosWeaponHolderContext::PlayCharacterCue(GetOwner(),Cue))Voice->PlaySource(Cue,HeliosZXK::OriginalSound(Cue==TEXT("Death")?32401:32403));return true;
}
void AHeliosZhongXiaoKuiWeapon::StartFiring(){bWaveHeld=false;bHeld=true;if(Age<0)StartAttack(false);}
void AHeliosZhongXiaoKuiWeapon::StartSecondaryFire(){if(Form==1){bWaveHeld=true;StartSwordWave();return;}if(Age<0)StartAttack(true);}
float AHeliosZhongXiaoKuiWeapon::Duration() const
{
    UAnimSequence* A=Clips.FindRef(Action);UAnimSequence* R=Clips.FindRef(Recover);return (A?A->GetPlayLength():0)+(R?R->GetPlayLength():0);
}
void AHeliosZhongXiaoKuiWeapon::StartAttack(bool Heavy)
{
    if(Age>=0)return;if(Form>0){StartFormAttack(Heavy);return;}ActionKind=0;
    auto* P=Cast<APawn>(GetOwner());const bool Pet=HeliosWeaponHolderContext::IsCompanion(P);if(!bActive||!P||HeliosWeaponHolderContext::IsDead(P)||GetWorld()->IsPaused()||(Pet&&GetWorld()->GetTimeSeconds()+KINDA_SMALL_NUMBER<CompanionNextAttackAt))return;bHeavy=Heavy&&!Pet;bCommitted=false;Age=0;++DamageAttackSerial;
    const TCHAR* Attacks[]={TEXT("FireSlightFirst"),TEXT("FireSlightSecond"),TEXT("FireSlightThird")};
    const TCHAR* Recovers[]={TEXT("First_Recover"),TEXT("Second_Recover"),TEXT("Third_Recover")};
    const int32 I=Combo++%3;
    if(Pet){const TCHAR* Names[]={TEXT("Att01"),TEXT("Att02"),TEXT("Att03")};Action=Names[I];Recover=FName(FString(Names[I])+TEXT("_Recover"));}
    else{Action=Heavy?FName(TEXT("FireHeavy")):FName(Attacks[I]);Recover=Heavy?NAME_None:FName(Recovers[I]);}
    if(!Clips.FindRef(Action)){Cancel();return;}
    if(Pet)CompanionNextAttackAt=GetWorld()->GetTimeSeconds()+Duration()/UHeliosCombatTempoSettings::Hunter(this,true,true,TEXT("动作倍率"),1,.1,5)+UHeliosCombatTempoSettings::Hunter(this,true,true,TEXT("攻击额外间隔"),0,0,30);
    AudioBetween(-KINDA_SMALL_NUMBER,0);Pose();
    if(Pet)
    {
        const FString Name=TEXT("FX_ShengHua_ZhongXiaoKui_b_Root_3P_A_")+Action.ToString();
        const FQuat SourceRootBasis(FVector::RightVector,-.5f*PI);
        if(auto* C=Cast<ACharacter>(P))if(auto* E=HeliosZXK::Effect(this,Name,C->GetMesh()->GetComponentLocation(),(P->GetActorQuat()*SourceRootBasis).Rotator(),false))
        {E->SetActorScale3D(C->GetMesh()->GetComponentScale());E->AttachToComponent(C->GetMesh(),FAttachmentTransformRules::KeepWorldTransform);Effects.Add(E);}return;
    }
    {
        FVector Eye;FRotator View;HeliosWeaponHolderContext::View(P,Eye,View);
        const FString Name=TEXT("FX_ShengHua_ZhongXiaoKui_Root_1P_A_")+(Heavy?FString(TEXT("Heavry")):Action.ToString());
        if(auto* Effect=HeliosZXK::Effect(this,Name,GetFirstPersonMesh()->GetComponentLocation(),View,true))
        {Effect->AttachToComponent(GetFirstPersonMesh(),FAttachmentTransformRules::KeepWorldTransform);
            // A local blade trail must share the hand mesh's translation. Its
            // world-space particles otherwise cross the camera while walking.
            if(!FParse::Param(FCommandLine::Get(),TEXT("HeliosZXKOriginalMeleeTranslationQA")))Effect->FollowLocalViewTranslation();
            Effects.Add(Effect);}
    }
}
void AHeliosZhongXiaoKuiWeapon::AudioBetween(float Previous,float Time)
{
    if(UAnimSequence* Clip=Clips.FindRef(Action))
    {
        const float Length=Clip->GetPlayLength();
        AudioEventCount+=HeliosZXK::PlayEvents(this,Voice,!HeliosWeaponHolderContext::IsCompanion(GetOwner()),Action,Previous,FMath::Min(Time,Length),ActionChannels);
        if(Time>=Length&&Clips.FindRef(Recover))AudioEventCount+=HeliosZXK::PlayEvents(this,Voice,!HeliosWeaponHolderContext::IsCompanion(GetOwner()),Recover,Previous<Length?-KINDA_SMALL_NUMBER:Previous-Length,Time-Length,ActionChannels);
    }
    ActionChannels.RemoveAll([](UAudioComponent* Channel){return !IsValid(Channel)||!Channel->IsPlaying();});
}
void AHeliosZhongXiaoKuiWeapon::Pose()
{
    const bool Moving=GetOwner()&&GetOwner()->GetVelocity().Size2D()>30.f;const FString Idle=Form==0?FString(Moving?TEXT("Move"):TEXT("Idle")):FString::Printf(TEXT("%s_%s"),Form==1?TEXT("B"):TEXT("C"),Moving?TEXT("Move"):TEXT("Idle"));
    const bool Pet=HeliosWeaponHolderContext::IsCompanion(GetOwner());
    UAnimSequence* Clip=Clips.FindRef(Age<0?(Pet?FName(Moving?TEXT("Run_Fwd"):TEXT("Idle")):FName(Idle)):Action);float Time=Age<0?IdleAge:Age;
    if(Age>=0&&Clip&&Age>Clip->GetPlayLength()&&Clips.FindRef(Recover)){Time-=Clip->GetPlayLength();Clip=Clips.FindRef(Recover);}
    if(auto* Hand=Cast<UHeliosHandAnimInstance>(GetFirstPersonMesh()->GetAnimInstance()))
    {
        // Retain displayed motion through the wave/idle boundary. Other forms
        // and attacks keep their existing transition policy and source speed.
        const bool Wave=ActionKind==4&&Form==1;
        // Use the same displayed-pose transition for idle and every movement direction.
        // Velocity extrapolation overshoots the accelerated native wave wind-up when standing.
        Hand->BlendDuration=UHeliosHandAnimInstance::BlendSeconds;
        Hand->bPreserveTransitionVelocity=false;
        Hand->TimelineRate=Wave&&Age>=0?UHeliosCombatTempoSettings::Hunter(this,true,HeliosWeaponHolderContext::IsCompanion(GetOwner()),TEXT("剑气动作倍率"),3.f,.1f,10.f)*UHeliosCombatTempoSettings::Hunter(this,true,HeliosWeaponHolderContext::IsCompanion(GetOwner()),TEXT("动作倍率"),1.f,.1f,5.f):1.f;
    }
    HeliosZXK::Sample(GetFirstPersonMesh(),Clip,Time,Age<0);
    if(Pet){Dart->SetVisibility(false);WaveSelfDepth->SetRenderCustomDepth(false);return;}
    const bool NativeHistory=FParse::Param(FCommandLine::Get(),TEXT("HeliosZXKLegacyWaveRenderingQA"));
    const float Now=GetWorld()->GetTimeSeconds();
    if(Form==1&&ActionKind==4&&Age>=0)WaveViewTailEnd=Now+.12f;
    const bool CurrentWaveView=!NativeHistory&&Form==1&&((ActionKind==4&&Age>=0)||(Age<0&&Now<WaveViewTailEnd));
    if(CurrentWaveView)
    {
        auto* Source=GetFirstPersonMesh();
        auto* Character=GetOwner()?GetOwner()->FindComponentByClass<UHeliosCharacterPresentationComponent>():nullptr;
        if(Character&&Character->IsReady()&&Character->GetArmsMesh())
        {
            Source=Character->GetArmsMesh();
            WaveSelfDepth->AddTickPrerequisiteComponent(Character);
        }
        // Custom depth follows the selected hands, including their paired rig.
        // The original weapon driver still owns the attack and wave clocks.
        if(WaveSelfDepth->GetAttachParent()!=Source)
        {
            if(auto* Previous=WaveSelfDepth->GetAttachParent())WaveSelfDepth->RemoveTickPrerequisiteComponent(Previous);
            WaveSelfDepth->AttachToComponent(Source,FAttachmentTransformRules::SnapToTargetIncludingScale);
            WaveSelfDepth->AddTickPrerequisiteComponent(Source);
        }
        if(WaveSelfDepth->GetSkinnedAsset()!=Source->GetSkinnedAsset())
        {
            WaveSelfDepth->SetSkeletalMesh(Source->GetSkeletalMeshAsset());
            WaveSelfDepth->EmptyOverrideMaterials();
            for(int32 I=0;I<Source->GetNumMaterials();++I)WaveSelfDepth->SetMaterial(I,Source->GetMaterial(I));
        }
        WaveSelfDepth->CopyPoseFromSkeletalComponent(Source);WaveSelfDepth->RefreshBoneTransforms();
    }
    // The same native mesh and sampled pose render once. Only the accelerated
    // wave uses solid alpha-one surfaces in the supported after-motion-blur pass.
    // Restore the original opaque surfaces for every other action.
    if(CurrentWaveView!=bWaveLateView)
    {
        WaveSelfDepth->SetRenderCustomDepth(bActive&&CurrentWaveView);
        WaveSelfDepth->SetComponentTickEnabled(bActive&&CurrentWaveView);
        bWaveLateView=CurrentWaveView;const auto& Materials=CurrentWaveView?WaveViewMaterials:NormalViewMaterials;
        for(int32 I=0;I<Materials.Num();++I)GetFirstPersonMesh()->SetMaterial(I,Materials[I]);
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosZXKForceVelocityQA")))GetFirstPersonMesh()->ForceMotionVector();
    Dart->UpdateComponentToWorld();
    Dart->SetVisibility(bActive&&(Form==2||(Age>=0&&ActionKind==2&&PendingForm==2)));
}
void AHeliosZhongXiaoKuiWeapon::Tick(float Dt)
{
    Super::Tick(Dt);if(!bActive||GetWorld()->IsPaused())return;
    auto* P=Cast<APawn>(GetOwner());if(!P||HeliosWeaponHolderContext::IsDead(P)){Cancel();return;}
    const bool Pet=HeliosWeaponHolderContext::IsCompanion(P);
    IdleAge+=Dt;
    Effects.RemoveAll([](AHeliosSSXEffect* Effect){return !IsValid(Effect);});
    Projectiles.RemoveAll([](AHeliosZXKProjectile* Projectile){return !IsValid(Projectile);});
    const float WaveRate=ActionKind==4&&Age>=0?UHeliosCombatTempoSettings::Hunter(this,true,HeliosWeaponHolderContext::IsCompanion(GetOwner()),TEXT("剑气动作倍率"),3.f,.1f,10.f):1.f;
    float Remaining=FMath::Max(Dt,0.f)*WaveRate*UHeliosCombatTempoSettings::Hunter(this,true,HeliosWeaponHolderContext::IsCompanion(GetOwner()),TEXT("动作倍率"),1,.1,5);
    for(int32 Guard=0;Age>=0&&Remaining>UE_SMALL_NUMBER&&Guard<128;++Guard)
    {
        const float Previous=Age,Length=Duration();
        const float Step=FMath::Min(Remaining,FMath::Max(Length-Age,0.f));
        Age+=Step;Remaining-=Step;AudioBetween(Previous,Age);UAnimSequence* Clip=Clips.FindRef(Action);
        if(Clip&&!bCommitted&&ActionKind!=2&&ActionKind!=3&&Age>=Clip->GetPlayLength()*.45f)
        {
            bCommitted=true;FVector Eye;FRotator R;HeliosWeaponHolderContext::View(P,Eye,R);
            if(Form==2||ActionKind==4)ThrowProjectile(ActionKind==4);
            else {const int32 Damaged=HeliosSSX::Strike(this,P,HeliosWeaponHolderContext::FriendlyPlayer(GetOwner()),Eye,R.Vector(),UHeliosCombatTempoSettings::Hunter(this,true,HeliosWeaponHolderContext::IsCompanion(GetOwner()),ActionKind==1?TEXT("技能攻击距离"):TEXT("攻击距离"),ActionKind==1?450:260,10,10000),UHeliosCombatTempoSettings::Hunter(this,true,HeliosWeaponHolderContext::IsCompanion(GetOwner()),ActionKind==1?TEXT("技能碰撞半径"):TEXT("近战碰撞半径"),ActionKind==1?180:85,1,2000),UHeliosCombatTempoSettings::Hunter(this,true,HeliosWeaponHolderContext::IsCompanion(GetOwner()),ActionKind==1?TEXT("技能伤害"):bHeavy?TEXT("重击伤害"):TEXT("轻击伤害"),ActiveDefinition->Damage*(ActionKind==1||bHeavy?2.f:1.f),0,100000),[this](AHeliosEnemyActor*,const FHitResult&,bool Killed){if(Killed&&(HeliosWeaponHolderContext::IsCompanion(GetOwner())||FMath::RandRange(1,10)==1)&&!HeliosWeaponHolderContext::PlayCharacterCue(GetOwner(),TEXT("Kill")))Voice->Play(TEXT("Kill"));});HitCount+=Damaged;if(Damaged>0&&!Pet)DamageFeedback->Emit(DamageAttackSerial<<8);}
        }
        if(Age>=Length-UE_SMALL_NUMBER)
        {
            Age=-1;if(ActionKind==3)DartAmmo=20;
            if(ActionKind==2){Form=PendingForm;Combo=0;IdleAge=0;if(bQueuedRanged&&Form==1){bQueuedRanged=false;StartTransition(2);}}
            if(bHeld&&Age<0)StartAttack(false);
        }
        else break;
    }
    if(Age<0&&bWaveHeld&&Form==1)StartSwordWave();
    Pose();
}
