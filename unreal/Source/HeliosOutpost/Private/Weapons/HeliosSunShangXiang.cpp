#include "Weapons/HeliosSunShangXiang.h"
#include "Weapons/HeliosWeaponHolderContext.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "GameFramework/Character.h"
#include "Audio/HeliosAudio.h"
#include "Weapons/HeliosHunterDamageFeedback.h"
#include "Materials/MaterialInterface.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Weapons/HeliosSSXVoice.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Weapons/HeliosHandAnimInstance.h"
#include "Player/HeliosArenaCharacter.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Pets/HeliosPetDog.h"
#include "Combat/CombatDamageSettings.h"
#include "Combat/HeliosCombatTempoSettings.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

namespace HeliosSSX
{
USoundBase* OriginalHitSound()
{
    // Reserved for a source-verified zombie impact. The installed SSX bank only
    // establishes Attack/Stab/SiLianZhan; it does not establish a flesh-hit mapping.
    return LoadObject<USoundBase>(nullptr,TEXT("/Game/Helios/CFMSunShangXiang/Audio/SFX_SSX_Hit_Original.SFX_SSX_Hit_Original"),nullptr,LOAD_NoWarn|LOAD_Quiet);
}
UHeliosSSXSession* Session(const UObject* Context)
{ const auto* W=Context?Context->GetWorld():nullptr;return W&&W->GetGameInstance()?W->GetGameInstance()->GetSubsystem<UHeliosSSXSession>():nullptr; }
UAnimSequence* Animation(bool FP,FName Name)
{ const FString N=FString::Printf(TEXT("A_SSX_%s_%s"),FP?TEXT("FP"):TEXT("TP"),*Name.ToString());return LoadObject<UAnimSequence>(nullptr,*((FP?TEXT("/Game/Helios/CFMSunShangXiang/FP_Rebuild_20261004/Animations/"):TEXT("/Game/Helios/CFMSunShangXiang/Animations/"))+N+TEXT(".")+N)); }
float FTimeline::Duration(bool FP) const
{
    switch(Attack){case EAttack::Light:return FP?.4f:.9333333f;case EAttack::Heavy:return FP?2.0333333f:2.f;
    case EAttack::Skill:return 2.2333333f;case EAttack::Equip:return 1.2f;default:return 0;}
}
void FTimeline::Pose(bool FP,FName& Name,float& Seconds) const
{
    Seconds=Age;Name=TEXT("Idle");
    if(Attack==EAttack::Equip){Name=TEXT("Equip");return;}
    if(Attack==EAttack::Heavy){Name=FP?TEXT("Heavy"):TEXT("FireHeavy");return;}
    if(Attack==EAttack::Light)
    {
        // Later reference 93.6-95.4s: accelerate the original windup, then
        // retain both authored sweeps. Only the clock changes; no joint offsets.
        if(FP)
        {
            constexpr float Windup=1.f/60.f, Stroke=.2666667f;
            Name=Age<Stroke?TEXT("FireFirst"):TEXT("FireRecover");
            Seconds=Age<Stroke?(Age<Windup?Age*3.f:.05f+(Age-Windup)*1.4f):(Age-Stroke)*1.5f;
            return;
        }
        const float First=.2666667f,Second=.6666667f;
        if(Age<First)Name=FP?TEXT("FireFirst"):TEXT("FireSlightFirst");
        else if(Age<First+Second){Name=FP?TEXT("FireSecond"):TEXT("FireSlightSec");Seconds-=First;}
        else {Name=FP?TEXT("FireRecover"):TEXT("Idle");Seconds-=First+Second;}
    }
    if(Attack==EAttack::Skill)
    {
        if(Age<.6f)Name=TEXT("Skill01");
        else if(Age<.7666667f){Name=TEXT("Skill02");Seconds-=.6f;}
        else if(Age<1.0333333f){Name=TEXT("Skill03");Seconds-=.7666667f;}
        else {Name=TEXT("Skill04");Seconds-=1.0333333f;}
    }
}
void FTimeline::Present(bool FP,TFunctionRef<void(int32)> Visual)
{
    const TArray<float> Times=Attack==EAttack::Light?(FP?TArray<float>{0}:TArray<float>{0,.2666667f}):Attack==EAttack::Heavy?TArray<float>{0}:Attack==EAttack::Skill?TArray<float>{0,.6f,.7666667f,1.0333333f}:TArray<float>{};
    for(int32 I=0;I<Times.Num();++I)if(!(Presented&(1u<<I))&&Age+KINDA_SMALL_NUMBER>=Times[I]){Presented|=1u<<I;Visual(I);}
}
void FTimeline::Advance(float Dt,bool FP,TFunctionRef<void(int32)> Contact)
{
    if(Attack==EAttack::None)return;
    Age+=FMath::Max(0.f,Dt);
    // Contact windows are calibrated on the original clips; no damage is tied to render frames or both blades independently.
    const TArray<float> Times=Attack==EAttack::Light?(FP?TArray<float>{1.f/60.f+(.13f-.05f)/1.4f,1.f/60.f+(.3f-.05f)/1.4f}:TArray<float>{.13f,.52f}):
        Attack==EAttack::Heavy?(FP?TArray<float>{.9f,1.0333333f}:TArray<float>{.48f,1.08f}):Attack==EAttack::Skill?TArray<float>{.34f,.66f,.88f,1.2f}:TArray<float>{};
    for(int32 I=0;I<Times.Num();++I)if(!(Committed&(1u<<I))&&Age+KINDA_SMALL_NUMBER>=Times[I])
    {Committed|=1u<<I;Contact(I);}
}
float Damage(const UObject* C,EAttack A,bool Pet)
{
    const ECombatDamageType T=A==EAttack::Light?(Pet?ECombatDamageType::SSXPetLight:ECombatDamageType::SSXLight):
        A==EAttack::Heavy?(Pet?ECombatDamageType::SSXPetHeavy:ECombatDamageType::SSXHeavy):(Pet?ECombatDamageType::SSXPetSkill:ECombatDamageType::SSXSkill);
    return UHeliosCombatTempoSettings::Hunter(C,false,Pet,A==EAttack::Light?TEXT("轻击整组伤害"):A==EAttack::Heavy?TEXT("重击整组伤害"):TEXT("技能每段伤害"),UCombatDamageSettings::Resolve(C,T),0,100000)/(A==EAttack::Skill?1.f:2.f);
}
float Tempo(const UObject* C,bool Pet)
{ return UHeliosCombatTempoSettings::Hunter(C,false,Pet,TEXT("动作倍率"),UHeliosCombatTempoSettings::Resolve(C,EHeliosTempoFile::Attack,TEXT("终极猎手孙尚香"),Pet?TEXT("伙伴动作倍率"):TEXT("玩家动作倍率"),1.f),.1f,5.f); }
float Cooldown(const UObject* C,bool Pet)
{ return UHeliosCombatTempoSettings::Hunter(C,false,Pet,TEXT("技能冷却"),UHeliosCombatTempoSettings::Resolve(C,EHeliosTempoFile::Recovery,TEXT("终极猎手孙尚香"),Pet?TEXT("伙伴技能秒"):TEXT("玩家技能秒"),10.f),.05f,600.f); }
int32 Strike(AActor* Source,APawn* Instigator,AActor* Friendly,const FVector& Start,const FVector& Direction,float Reach,float Radius,float Amount,TFunction<void(AHeliosEnemyActor*,const FHitResult&,bool)> Feedback)
{
    if(!Source||Amount<=0)return 0;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(SSXStrike),false,Source);Q.AddIgnoredActor(Instigator);Q.AddIgnoredActor(Friendly);
    for(TActorIterator<AHeliosPetCompanion> P(Source->GetWorld());P;++P)Q.AddIgnoredActor(*P);
    TArray<FHitResult> Hits;FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_Pawn);
    Source->GetWorld()->SweepMultiByObjectType(Hits,Start,Start+Direction*Reach,FQuat::Identity,Objects,FCollisionShape::MakeSphere(Radius),Q);
    TSet<AActor*> Seen;int32 Count=0;
    for(const auto& H:Hits)
    {
        auto* E=Cast<AHeliosEnemyActor>(H.GetActor());if(!E||!E->CanBeDamaged()||E->IsEnemyDead()||Seen.Contains(E))continue;Seen.Add(E);
        const FVector To=E->GetActorLocation()-Start;if(FVector::DotProduct(To,Direction)<0)continue;
        FHitResult Wall;if(Source->GetWorld()->LineTraceSingleByChannel(Wall,Start,E->GetActorLocation(),ECC_Visibility,Q)&&Wall.GetActor()!=E)continue;
        const float PreviousHP=E->CurrentHP;
        if(UGameplayStatics::ApplyDamage(E,Amount,Instigator?Instigator->GetController():nullptr,Source,UDamageType::StaticClass())>0&&E->CurrentHP<PreviousHP)
        {++Count;if(Feedback)Feedback(E,H,E->IsEnemyDead());}
    }
    return Count;
}
}
void UHeliosSSXClock::Tick(float Dt)
{
    if(!GetWorld()->HasBegunPlay()||GetWorld()->IsPaused())return;
    if(auto* S=HeliosSSX::Session(this)){S->PlayerRemaining=FMath::Max(0.f,S->PlayerRemaining-Dt);S->PetRemaining=FMath::Max(0.f,S->PetRemaining-Dt);}
}
UHeliosSunShangXiangDefinition::UHeliosSunShangXiangDefinition()
{
    WeaponId=TEXT("cfm_sunshangxiang");DisplayName=FText::FromString(TEXT("终极猎手·孙尚香"));
    Description=FText::FromString(TEXT("原版手臂与双刃。左键轻击 · 右键重击 · G 四连斩（近身判定，10秒冷却）。无需弹药，可与孙尚香伙伴同时使用。"));
    WeaponClass=AHeliosSunShangXiangWeapon::StaticClass();MagazineSize=1;ReloadSeconds=0;Damage=100;MinimumDamage=100;Range=260;RefireSeconds=1.1666667f;Recoil=0;
    // Keep authored camera origin: moving the mesh forward exposes open arm ends.
    // The owning camera supplies first-person depth scaling and a scoped near plane.
    AttachmentLocation=FVector::ZeroVector;AttachmentRotation=FRotator(0,-90,0);AttachmentScale=FVector(1);
    RefireSeconds=.4f;
    SkeletalWeaponMesh=TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Helios/CFMSunShangXiang/FP_Rebuild_20261004/SK_SunShangXiang_FP.SK_SunShangXiang_FP")));
    PreviewTexture=TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Helios/CFMSunShangXiang/Textures/T_SSX_FP_Preview.T_SSX_FP_Preview")));
}
AHeliosSunShangXiangWeapon::AHeliosSunShangXiangWeapon()
{
    PrimaryActorTick.bCanEverTick=true;bFullAuto=false;MagazineSize=1;
    Voice=CreateDefaultSubobject<UHeliosSSXVoiceComponent>(TEXT("Original SunShangXiang voice"));
    DamageFeedback=CreateDefaultSubobject<UHeliosHunterDamageFeedback>(TEXT("Player damage feedback07"));DamageFeedback->bZXK=false;
    FirstPersonModel->SetVisibility(false);ThirdPersonModel->SetVisibility(false);
    GetFirstPersonMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);GetFirstPersonMesh()->SetCastShadow(false);GetFirstPersonMesh()->SetOnlyOwnerSee(true);
    GetFirstPersonMesh()->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
    // No physics asset: retain the animated swords in the view even when the reference arms are below the camera.
    GetFirstPersonMesh()->SetBoundsScale(4.f);
    GetFirstPersonMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
}
void AHeliosSunShangXiangWeapon::ApplyDefinition(const UHeliosWeaponDefinition* D)
{
    const bool Pet=HeliosWeaponHolderContext::IsCompanion(GetOwner());
    Super::ApplyDefinition(D);GetFirstPersonMesh()->SetSkeletalMesh(Pet?LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Helios/CFMSunShangXiang/SK_SunShangXiang_TP.SK_SunShangXiang_TP")):D->SkeletalWeaponMesh.LoadSynchronous());
    for(int32 I=0;I<GetFirstPersonMesh()->GetNumMaterials();++I)
    {
        if(Pet||FParse::Param(FCommandLine::Get(),TEXT("HeliosOriginalFPMaterialsQA")))continue;
        auto* Original=GetFirstPersonMesh()->GetMaterial(I);if(!Original||Original->GetName().EndsWith(TEXT("_ClearFP")))continue;
        const FString Name=Original->GetName()+TEXT("_ClearFP");
        if(auto* Clear=LoadObject<UMaterialInterface>(nullptr,*(TEXT("/Game/Helios/CFMSunShangXiang/Materials/Clarity/")+Name+TEXT(".")+Name)))GetFirstPersonMesh()->SetMaterial(I,Clear);
    }
    GetFirstPersonMesh()->SetAnimInstanceClass(UHeliosHandAnimInstance::StaticClass());GetFirstPersonMesh()->AddTickPrerequisiteActor(this);
    if(auto* Hand=Cast<UHeliosHandAnimInstance>(GetFirstPersonMesh()->GetAnimInstance()))Hand->bPreserveTransitionVelocity=false;
    GetFirstPersonMesh()->bEnableUpdateRateOptimizations=false;GetFirstPersonMesh()->SetComponentTickEnabled(true);
    ResidentEffectLibrary=LoadObject<UHeliosSSXEffectLibrary>(nullptr,TEXT("/Game/Helios/CFMSunShangXiang/Effects/DA_SSX_Effects.DA_SSX_Effects"));
    if(ResidentEffectLibrary)ResidentEffectLibrary->GetParsedRecipe();
    BladeEffectLibrary=LoadObject<UHeliosSSXEffectLibrary>(nullptr,TEXT("/Game/Helios/CFMSunShangXiang/Effects/BladeFlames/DA_SSX_BladeFlames.DA_SSX_BladeFlames"));
    if(BladeEffectLibrary)BladeEffectLibrary->GetParsedRecipe();
    for(const TCHAR* N:{TEXT("Idle"),TEXT("Move"),TEXT("Equip"),TEXT("FireFirst"),TEXT("FireSecond"),TEXT("FireRecover"),TEXT("Heavy"),TEXT("Skill01"),TEXT("Skill02"),TEXT("Skill03"),TEXT("Skill04")})ResidentAnimations.Add(HeliosSSX::Animation(true,N));
    for(const TCHAR* N:{TEXT("Light"),TEXT("Heavy"),TEXT("Skill")})ResidentSounds.Add(LoadObject<USoundBase>(nullptr,*(FString(TEXT("/Game/Helios/CFMSunShangXiang/Audio/SFX_SSX_"))+N)));
    HitSound=HeliosSSX::OriginalHitSound();
    if(!HitSound)UE_LOG(LogTemp,Warning,TEXT("SSX original zombie impact audio is pending source verification; impact playback is disabled."));
    GetFirstPersonMesh()->SetRelativeLocation(D->AttachmentLocation);GetFirstPersonMesh()->SetRelativeRotation(D->AttachmentRotation);
    if(Pet)for(const TCHAR* N:{TEXT("Idle"),TEXT("Run_Fwd"),TEXT("FireSlightFirst"),TEXT("FireSlightSec"),TEXT("FireHeavy"),TEXT("Skill01"),TEXT("Skill02"),TEXT("Skill03"),TEXT("Skill04")})ResidentAnimations.Add(HeliosSSX::Animation(false,N));
    Voice->Initialize(Pet);PlaybackSpeed=HeliosSSX::Tempo(this,Pet);Pose();FirstPersonModel->SetVisibility(false);ThirdPersonModel->SetVisibility(false);
}
void AHeliosSunShangXiangWeapon::ActivateWeapon(const FName& Tag)
{Super::ActivateWeapon(Tag);GetFirstPersonMesh()->SetRelativeLocation(ActiveDefinition->AttachmentLocation);GetFirstPersonMesh()->SetRelativeRotation(ActiveDefinition->AttachmentRotation);bActive=true;Cancel();PlaybackSpeed=HeliosSSX::Tempo(this,HeliosWeaponHolderContext::IsCompanion(GetOwner()));if(!HeliosWeaponHolderContext::IsCompanion(GetOwner()))Timeline.Begin(HeliosSSX::EAttack::Equip);FirstPersonModel->SetVisibility(false);Pose();}
void AHeliosSunShangXiangWeapon::DeactivateWeapon(){bActive=false;DamageFeedback->Clear();ClearBladeEffects();Cancel();Super::DeactivateWeapon();}
void AHeliosSunShangXiangWeapon::EndPlay(const EEndPlayReason::Type R){ClearBladeEffects();Cancel();Super::EndPlay(R);}
void AHeliosSunShangXiangWeapon::CancelReload(){Cancel();Super::CancelReload();}
void AHeliosSunShangXiangWeapon::Cancel()
{
    Timeline.Clear();HitAudioGate.Reset();bPrimaryDown=bSecondaryDown=bQueuedPrimary=false;
    Voice->Stop();
    for(const auto& E:Effects)if(IsValid(E))E->Destroy();Effects.Reset();if(ActionAudio){ActionAudio->Stop();ActionAudio=nullptr;}Pose();
}
void AHeliosSunShangXiangWeapon::StartFiring()
{
    if(!bPrimaryDown)
    {
        bPrimaryDown=true;
        if(Timeline.Attack==HeliosSSX::EAttack::Light)bQueuedPrimary=true;
        else BeginAttack(HeliosSSX::EAttack::Light);
    }
}
void AHeliosSunShangXiangWeapon::StartSecondaryFire(){if(!bSecondaryDown){bSecondaryDown=true;BeginAttack(HeliosSSX::EAttack::Heavy);}}
void AHeliosSunShangXiangWeapon::StartSpecialFire(){BeginAttack(HeliosSSX::EAttack::Skill);}
bool AHeliosSunShangXiangWeapon::HandleOwnerCombatCue(FName Cue)
{
    if(Cue==TEXT("FatalHurt"))return true;
    if(Cue!=TEXT("Hurt")&&Cue!=TEXT("Death"))return false;
    if(Cue==TEXT("Death"))Cancel();
    if(!HeliosWeaponHolderContext::PlayCharacterCue(GetOwner(),Cue))Voice->Play(Cue);return true;
}
float AHeliosSunShangXiangWeapon::CooldownRemaining()const{if(HeliosWeaponHolderContext::IsCompanion(GetOwner()))return CompanionSkillRemaining;auto* S=HeliosSSX::Session(this);return S?S->PlayerRemaining:0;}
bool AHeliosSunShangXiangWeapon::BeginAttack(HeliosSSX::EAttack Type)
{
    auto* P=Cast<APawn>(GetOwner());const bool Pet=HeliosWeaponHolderContext::IsCompanion(P);if(!bActive||!P||HeliosWeaponHolderContext::IsDead(P)||GetWorld()->IsPaused()||Timeline.Attack!=HeliosSSX::EAttack::None)return false;
    if(Pet&&GetWorld()->GetTimeSeconds()+KINDA_SMALL_NUMBER<CompanionNextAttackAt)return false;
    if(Type==HeliosSSX::EAttack::Skill){if(Pet){if(CompanionSkillRemaining>0)return false;CompanionSkillRemaining=HeliosSSX::Cooldown(this,true);}else{auto* S=HeliosSSX::Session(this);if(!S||S->PlayerRemaining>0)return false;S->PlayerRemaining=HeliosSSX::Cooldown(this,false);}}
    ++DamageAttackSerial;Timeline.Begin(Type);if(Pet)CompanionNextAttackAt=GetWorld()->GetTimeSeconds()+Timeline.Duration(false)/PlaybackSpeed+UHeliosCombatTempoSettings::Hunter(this,false,true,TEXT("攻击额外间隔"),0,0,30);HitAudioGate.Reset();PlaySourceSound(Type==HeliosSSX::EAttack::Light?TEXT("Light"):Type==HeliosSSX::EAttack::Heavy?TEXT("Heavy"):TEXT("Skill"));Pose();return true;
}
void AHeliosSunShangXiangWeapon::PlaySourceSound(const TCHAR* N)
{
    if(ActionAudio)ActionAudio->Stop();
    const int32 I=FCString::Strcmp(N,TEXT("Light"))==0?0:FCString::Strcmp(N,TEXT("Heavy"))==0?1:2;
    if(ResidentSounds.IsValidIndex(I))ActionAudio=HeliosAudio::SpawnSoundAttached(ResidentSounds[I],GetFirstPersonMesh(),NAME_None,FVector::ZeroVector,EAttachLocation::KeepRelativeOffset,true,.8f,1,0,nullptr,nullptr,true);
}
void AHeliosSunShangXiangWeapon::Tick(float Dt)
{
    Super::Tick(Dt);if(GetWorld()->IsPaused())return;
    const bool FP=!HeliosWeaponHolderContext::IsCompanion(GetOwner());
    CompanionSkillRemaining=FMath::Max(0.f,CompanionSkillRemaining-Dt);
    if(!bActive)return;
    if(HeliosWeaponHolderContext::IsDead(GetOwner())){ClearBladeEffects();Cancel();return;}
    IdleAge+=Dt;
    float Remaining=FMath::Max(0.f,Dt*PlaybackSpeed);
    // Consume the frame across action boundaries. Otherwise every repeated attack
    // loses its overshoot and sustained cadence depends on the render frame rate.
    for(int32 Guard=0;Guard<128;++Guard)
    {
        if(Timeline.Attack==HeliosSSX::EAttack::None)
        {
            if(!(bPrimaryDown||bQueuedPrimary)||!BeginAttack(HeliosSSX::EAttack::Light))break;
            bQueuedPrimary=false;
        }
        const float End=Timeline.Duration(FP);
        const float Step=FMath::Min(Remaining,FMath::Max(0.f,End-Timeline.Age));
        Timeline.Advance(Step,FP,[this](int32 I){Contact(I);});
        Timeline.Present(FP,[this](int32 I){ShowEffect(I);});
        Remaining-=Step;
        if(Timeline.Age+KINDA_SMALL_NUMBER<End)break;
        Timeline.Clear();
        if(Remaining<=KINDA_SMALL_NUMBER&&!(bPrimaryDown||bQueuedPrimary))break;
        // Also initialize the next held action at an exact boundary with zero overshoot.
    }
    Pose();
    UpdateBladeEffects();
    Effects.RemoveAll([](const auto& E){return !IsValid(E);});
}
void AHeliosSunShangXiangWeapon::Pose()
{
    auto* Mesh=GetFirstPersonMesh();if(!Mesh->GetSkeletalMeshAsset())return;
    const bool FP=!HeliosWeaponHolderContext::IsCompanion(GetOwner());
    FName N;float T;Timeline.Pose(FP,N,T);const bool Idle=Timeline.Attack==HeliosSSX::EAttack::None;
    if(Idle){T=IdleAge;if(GetOwner()&&GetOwner()->GetVelocity().Size2D()>20)N=FP?TEXT("Move"):TEXT("Run_Fwd");}
    auto* Clip=HeliosSSX::Animation(FP,N);if(!Clip)return;
    // Use the repaired weapon-clock proxy (positive-delta update advances its
    // transition). Source curves remain intact; only incompatible clip boundaries
    // retain the displayed pose in a velocity-free transition. Light entry/recovery
    // use short, independent blends; all other actions retain the 80ms baseline.
    if(auto* Hand=Cast<UHeliosHandAnimInstance>(Mesh->GetAnimInstance()))
    {
        Hand->BlendDuration=Timeline.Attack==HeliosSSX::EAttack::Light?(N==TEXT("FireFirst")?.018f:.055f):UHeliosHandAnimInstance::BlendSeconds;
        Hand->SetTimelinePose(Clip,T,Idle);Mesh->TickAnimation(0,false);Mesh->RefreshBoneTransforms();return;
    }
    auto* Node=Mesh->GetSingleNodeInstance();
    if(!Node||Node->GetCurrentAsset()!=Clip){Mesh->SetAnimation(Clip);Node=Mesh->GetSingleNodeInstance();}
    if(!Node)return;Node->SetPlaying(false);Node->SetLooping(Idle);
    const float Length=FMath::Max(Clip->GetPlayLength(),KINDA_SMALL_NUMBER);
    Mesh->SetPosition(Idle?FMath::Fmod(FMath::Max(T,0.f),Length):FMath::Clamp(T,0.f,Length),false);
    Mesh->TickAnimation(0,false);Mesh->RefreshBoneTransforms();
}
void AHeliosSunShangXiangWeapon::Contact(int32 Phase)
{
    auto* P=Cast<APawn>(GetOwner());if(!P||HeliosWeaponHolderContext::IsDead(P))return;FVector Start;FRotator R;HeliosWeaponHolderContext::View(P,Start,R);
    const bool Skill=Timeline.Attack==HeliosSSX::EAttack::Skill,Pet=HeliosWeaponHolderContext::IsCompanion(P);
    // The local assets establish four source clips, but contain no verified ranged-hit rule.
    // Keep the unverified ranged extension disabled instead of inventing a projectile or range.
    const int32 Damaged=HeliosSSX::Strike(this,P,HeliosWeaponHolderContext::FriendlyPlayer(GetOwner()),Start,R.Vector(),UHeliosCombatTempoSettings::Hunter(this,false,Pet,TEXT("攻击距离"),260,10,10000),UHeliosCombatTempoSettings::Hunter(this,false,Pet,Skill?TEXT("技能碰撞半径"):TEXT("近战碰撞半径"),Skill?110:85,1,2000),HeliosSSX::Damage(this,Timeline.Attack,Pet),[this,R](AHeliosEnemyActor* E,const FHitResult& H,bool Killed)
    {
        if(Killed&&!HeliosWeaponHolderContext::PlayCharacterCue(GetOwner(),TEXT("Kill")))Voice->Play(TEXT("Kill"));
        const FVector At=H.ImpactPoint.IsNearlyZero()?E->GetActorLocation():FVector(H.ImpactPoint);
        if(auto* V=AHeliosSSXEffect::Spawn(this,TEXT("FX_SunShangXiang_Damage"),At,R,false))Effects.Add(V);
    });
    HitCount+=Damaged;
    if(HitAudioGate.Accept(Phase,Damaged)){if(!Pet)DamageFeedback->Emit((DamageAttackSerial<<8)|uint64(Phase));if(HitSound){if(Pet)HeliosAudio::PlaySoundAtLocation(this,HitSound,Start,.55f);else HeliosAudio::PlaySound2D(this,HitSound,.8f);}}
}
void AHeliosSunShangXiangWeapon::ShowEffect(int32 Phase)
{
    auto* P=Cast<APawn>(GetOwner());if(!P||HeliosWeaponHolderContext::IsDead(P))return;FVector Start;FRotator R;HeliosWeaponHolderContext::View(P,Start,R);
    const bool Skill=Timeline.Attack==HeliosSSX::EAttack::Skill,Pet=HeliosWeaponHolderContext::IsCompanion(P);
    if(Pet)
    {
        const FString FX=Skill?FString::Printf(TEXT("FX_3P_SunShangXiang_Skill%d"),Phase+1):FString::Printf(TEXT("FX_3P_SunShangXiang_Trail%02d"),Timeline.Attack==HeliosSSX::EAttack::Heavy?3:Phase+1);
        if(auto* E=AHeliosSSXEffect::Spawn(this,FX,P->GetActorLocation()+FVector(0,0,30),R,false,PlaybackSpeed))
        {if(auto* C=Cast<ACharacter>(P))E->SetActorScale3D(FVector(C->GetMesh()->GetComponentScale().X/1.7f));E->AttachToComponent(P->GetRootComponent(),FAttachmentTransformRules::KeepWorldTransform);Effects.Add(E);}return;
    }
    const FString FX=Skill?FString::Printf(TEXT("FX_1P_SunShangXiang_Skill%d"),Phase+1):FString::Printf(TEXT("FX_1P_SunShangXiang_Trail%02d"),Timeline.Attack==HeliosSSX::EAttack::Heavy?3:Phase+1);
    // Trail01 already carries both original blade meshes for the FireFirst pair.
    const float EffectSpeed=PlaybackSpeed*(Timeline.Attack==HeliosSSX::EAttack::Light?1.5f:1.f);
    if(auto* E=AHeliosSSXEffect::Spawn(this,FX,GetFirstPersonMesh()->GetComponentLocation(),R,true,EffectSpeed)){E->AttachToComponent(GetFirstPersonMesh(),FAttachmentTransformRules::KeepWorldTransform);Effects.Add(E);}
}
void AHeliosSunShangXiangWeapon::ClearBladeEffects()
{
    for(const auto& E:BladeEffects)if(IsValid(E))E->Destroy();BladeEffects.Reset();
}
void AHeliosSunShangXiangWeapon::UpdateBladeEffects()
{
    if(HeliosWeaponHolderContext::IsCompanion(GetOwner()))return;
    BladeEffects.RemoveAll([](const auto& E){return !IsValid(E);});
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosSSXNoBladeFlamesQA")))return;
    if(!BladeEffectLibrary||!bActive||BladeEffects.Num()==2)return;
    ClearBladeEffects();
    for(const TCHAR* Side:{TEXT("R"),TEXT("L")})
    {
        const FName Bone=FCString::Strcmp(Side,TEXT("R"))==0?FName(TEXT("Bone01")):FName(TEXT("Bone02"));
        auto* E=AHeliosSSXEffect::Spawn(this,FString(TEXT("FX_SSX_BladeFlame_"))+Side,GetFirstPersonMesh()->GetSocketLocation(Bone),GetFirstPersonMesh()->GetSocketRotation(Bone),true,1.f,TEXT("/Game/Helios/CFMSunShangXiang/Effects/BladeFlames/DA_SSX_BladeFlames.DA_SSX_BladeFlames"));
        if(!E)continue;
        E->AttachToComponent(GetFirstPersonMesh(),FAttachmentTransformRules::SnapToTargetNotIncludingScale,Bone);
        // Same source-effect basis as the root-bound trails: native FP model uses +Y forward.
        E->SetActorRelativeRotation(FRotator(0,90,0));E->ResetBirthAnchors();
        E->AddTickPrerequisiteComponent(GetFirstPersonMesh());BladeEffects.Add(E);
        UE_LOG(LogTemp,Display,TEXT("SSX_BLADE_FLAME bone=%s socket_scale=%s effect_scale=%s"),*Bone.ToString(),*GetFirstPersonMesh()->GetSocketTransform(Bone).GetScale3D().ToString(),*E->GetActorScale3D().ToString());
    }
}
FString AHeliosSunShangXiangWeapon::Status()const
{const float C=CooldownRemaining();return C>0?FString::Printf(TEXT("双刃 · G 技能冷却 %.1f秒"),C):TEXT("双刃 · 左键轻击 / 右键重击 / G 技能就绪");}

