#include "Pets/HeliosSunShangXiangPet.h"
#include "Audio/HeliosAudio.h"
#include "Combat/HeliosCombatTempoSettings.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Weapons/HeliosSSXVoice.h"
#include "Weapons/HeliosHandAnimInstance.h"
#include "Player/HeliosArenaCharacter.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Components/AudioComponent.h"
#include "Animation/AnimSequence.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

AHeliosSunShangXiangPet::AHeliosSunShangXiangPet()
{
    Voice=CreateDefaultSubobject<UHeliosSSXVoiceComponent>(TEXT("Original SunShangXiang voice"));
    Kind=EHeliosPetKind::SunShangXiang;Settings.MaxHealth=600;Settings.ShieldReduction=0;Settings.MeleeRange=260;Settings.MovementMultiplier=1;
    // Match the confirmed Butterfly/Thunder companion's 203.3593 cm body.
    // Keep the shared navigation agent (38 x 192), as the reference uses it.
    GetCapsuleComponent()->InitCapsuleSize(34,102);GetMesh()->SetRelativeLocation(FVector(0,0,-102));GetMesh()->SetRelativeRotation(FRotator(0,-90,0));GetMesh()->SetRelativeScale3D(FVector(1.8715905f));
    GetMesh()->SetBoundsScale(2.f);
    GetCharacterMovement()->MaxWalkSpeed=520;GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
}
void AHeliosSunShangXiangPet::LoadModel()
{
    ResidentEffectLibrary=LoadObject<UHeliosSSXEffectLibrary>(nullptr,TEXT("/Game/Helios/CFMSunShangXiang/Effects/DA_SSX_Effects.DA_SSX_Effects"));
    if(ResidentEffectLibrary)ResidentEffectLibrary->GetParsedRecipe();
    GetMesh()->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Helios/CFMSunShangXiang/SK_SunShangXiang_TP.SK_SunShangXiang_TP")));
    if(auto* Model=GetMesh()->GetSkeletalMeshAsset())
        GetMesh()->SetRelativeLocation(FVector(0,0,-GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()-(Model->GetBounds().Origin.Z-Model->GetBounds().BoxExtent.Z)*GetMesh()->GetRelativeScale3D().Z));
    GetMesh()->SetAnimInstanceClass(UHeliosHandAnimInstance::StaticClass());GetMesh()->AddTickPrerequisiteActor(this);bModelReady=GetMesh()->GetSkeletalMeshAsset()!=nullptr;
    if(auto* Anim=Cast<UHeliosHandAnimInstance>(GetMesh()->GetAnimInstance()))Anim->bPreserveTransitionVelocity=false;
    for(const TCHAR* N:{TEXT("Idle"),TEXT("Run_Fwd"),TEXT("Run_Bwd"),TEXT("Run_LT"),TEXT("Run_RT"),TEXT("Hit"),TEXT("Dead"),TEXT("FireSlightFirst"),TEXT("FireSlightSec"),TEXT("FireHeavy"),TEXT("Skill01"),TEXT("Skill02"),TEXT("Skill03"),TEXT("Skill04")})Poses.Add(N,HeliosSSX::Animation(false,N));
    GetMesh()->bEnableUpdateRateOptimizations=false;GetMesh()->SetComponentTickEnabled(true);
    HitSound=HeliosSSX::OriginalHitSound();Voice->Initialize(true);PlaybackSpeed=HeliosSSX::Tempo(this,true);
}
void AHeliosSunShangXiangPet::PoseAt(FName Name,float Time,bool Loop)
{
    if(auto* A=Cast<UHeliosHandAnimInstance>(GetMesh()->GetAnimInstance()))
    {A->SetTimelinePose(Poses.FindRef(Name),Time,Loop);GetMesh()->TickAnimation(0,false);GetMesh()->RefreshBoneTransforms();}
}
void AHeliosSunShangXiangPet::SetPose(FName Name,bool Loop)
{
    if((Name==TEXT("Down")||Name==TEXT("Sleep"))&&CurrentPose!=TEXT("Down")&&CurrentPose!=TEXT("Sleep"))Voice->Stop();
    if(CurrentPose!=Name){CurrentPose=Name;PoseAge=0;}
    const bool Dead=Name==TEXT("Down")||Name==TEXT("Sleep");const FName Actual=Dead?FName(TEXT("Dead")):Name==TEXT("Move")?MovePose:Name==TEXT("Hit")?Name:FName(TEXT("Idle"));
    PoseAt(Actual,Name==TEXT("Sleep")?1.3666667f:Name==TEXT("Move")?MoveAge:PoseAge,Dead?false:Loop);
}
void AHeliosSunShangXiangPet::UpdateLocomotion(float Dt)
{
    if(!Target.IsValid())GetCharacterMovement()->bOrientRotationToMovement=true;
    const float Speed=GetVelocity().Size2D();
    if(Speed<=20){MoveAge=0;return;}
    MoveAge+=Dt*FMath::Clamp(Speed/(520.f*GetMesh()->GetRelativeScale3D().X/1.7f),.05f,2.f);
    const FVector Local=GetActorRotation().UnrotateVector(GetVelocity());
    const float Angle=FMath::RadiansToDegrees(FMath::Atan2(Local.Y,Local.X));
    const float Centre=MovePose==TEXT("Run_Bwd")?180.f:MovePose==TEXT("Run_RT")?90.f:MovePose==TEXT("Run_LT")?-90.f:0.f;
    if(FMath::Abs(FMath::FindDeltaAngleDegrees(Centre,Angle))>55.f)
        MovePose=FMath::Abs(Angle)>135.f?TEXT("Run_Bwd"):Angle>45.f?TEXT("Run_RT"):Angle<-45.f?TEXT("Run_LT"):TEXT("Run_Fwd");
}
float AHeliosSunShangXiangPet::TakeDamage(float Amount,const FDamageEvent& Event,AController* InstigatorController,AActor* Causer)
{
    const float Applied=Super::TakeDamage(Amount,Event,InstigatorController,Causer);
    if(Applied>0){if(AttackAge<0)PoseAge=0;Voice->Play(IsSleeping()?TEXT("Death"):TEXT("Hurt"));}
    return Applied;
}
float AHeliosSunShangXiangPet::RecoveryDuration(FName Name,float Fallback)const
{return Name==TEXT("Down")?1.3666667f:Name==TEXT("Wake")?.3f:Fallback;}
void AHeliosSunShangXiangPet::Tick(float Dt)
{
    PoseAge+=Dt;SkillRemaining=FMath::Max(0.f,SkillRemaining-Dt);if(CompanionOwner.IsValid()&&CompanionOwner->IsDead()){Voice->Stop();CancelAttack();}Super::Tick(Dt);
    if(IsSleeping()||State==EHeliosPetState::Waking)SetPose(CurrentPose,false);
    SourceEffects.RemoveAll([](const auto& E){return !IsValid(E);});
}
void AHeliosSunShangXiangPet::TrackCombatTarget(float Dt)
{
    if(Timeline.Attack!=HeliosSSX::EAttack::None){GetCharacterMovement()->bOrientRotationToMovement=false;if(auto* AI=Cast<AAIController>(GetController()))AI->StopMovement();GetCharacterMovement()->StopMovementImmediately();bMoveRequested=false;return;}
    GetCharacterMovement()->bOrientRotationToMovement=!(Target.IsValid()&&HasClearLine(Target.Get())&&FVector::DistSquared2D(GetActorLocation(),Target->GetActorLocation())<=FMath::Square(Settings.MeleeRange*1.5f));
    Super::TrackCombatTarget(Dt);
}
void AHeliosSunShangXiangPet::CancelAttack()
{Timeline.Clear();HitAudioGate.Reset();for(const auto& E:SourceEffects)if(IsValid(E))E->Destroy();SourceEffects.Reset();if(ActionAudio){ActionAudio->Stop();ActionAudio=nullptr;}Super::CancelAttack();}
void AHeliosSunShangXiangPet::EndPlay(const EEndPlayReason::Type R){Voice->Stop();CancelAttack();Super::EndPlay(R);}
void AHeliosSunShangXiangPet::UpdateAttack(float Dt)
{
    if(Timeline.Attack!=HeliosSSX::EAttack::None)
    {
        Timeline.Advance(Dt*PlaybackSpeed,false,[this](int32 P){Contact(P);});Timeline.Present(false,[this](int32 P){ShowEffect(P);});AttackAge=Timeline.Age;
        FName N;float T;Timeline.Pose(false,N,T);PoseAt(N,T,false);
        if(Timeline.Age>=Timeline.Duration(false)){Timeline.Clear();AttackAge=-1;CurrentPose=NAME_None;DecisionRemaining=0;}
        return;
    }
    if(HitPoseRemaining>0){SetPose(TEXT("Hit"),false);return;}
    auto* E=Target.Get();auto* S=HeliosSSX::Session(this);
    const float Distance=E?FVector::Dist2D(GetActorLocation(),E->GetActorLocation()):MAX_flt;
    if(E&&!E->IsEnemyDead()&&Cooldown<=0&&HasClearLine(E)&&Distance<=Settings.MeleeRange)
    {
        const bool SkillReady=RosterSize>1?SkillRemaining<=0:S&&S->PetRemaining<=0;
        const auto Type=SkillReady?HeliosSSX::EAttack::Skill:(AttackSerial++%3==2?HeliosSSX::EAttack::Heavy:HeliosSSX::EAttack::Light);
        if(Type==HeliosSSX::EAttack::Skill){if(RosterSize>1)SkillRemaining=HeliosSSX::Cooldown(this,true);else S->PetRemaining=HeliosSSX::Cooldown(this,true);}
        Timeline.Begin(Type);HitAudioGate.Reset();AttackAge=0;Cooldown=Timeline.Duration(false)/PlaybackSpeed+UHeliosCombatTempoSettings::Hunter(this,false,true,TEXT("攻击额外间隔"),0,0,30);
        if(auto* AI=Cast<AAIController>(GetController()))AI->StopMovement();GetCharacterMovement()->StopMovementImmediately();bMoveRequested=false;
        const TCHAR* Label=Type==HeliosSSX::EAttack::Skill?TEXT("Skill"):Type==HeliosSSX::EAttack::Heavy?TEXT("Heavy"):TEXT("Light");
        if(ActionAudio)ActionAudio->Stop();ActionAudio=HeliosAudio::SpawnSoundAttached(LoadObject<USoundBase>(nullptr,*(FString(TEXT("/Game/Helios/CFMSunShangXiang/Audio/SFX_SSX_"))+Label)),GetMesh(),NAME_None,FVector::ZeroVector,EAttachLocation::KeepRelativeOffset,true,.55f*RosterAudioGain());
        FName N;float T;Timeline.Pose(false,N,T);PoseAt(N,T,false);
    }
    else SetPose(GetVelocity().Size2D()>20?TEXT("Move"):TEXT("Idle"));
}
void AHeliosSunShangXiangPet::Contact(int32 Phase)
{
    const bool Skill=Timeline.Attack==HeliosSSX::EAttack::Skill;
    const FVector Start=GetActorLocation()+FVector(0,0,30);const FRotator R=GetActorRotation();
    const int32 Damaged=HeliosSSX::Strike(this,this,CompanionOwner.Get(),Start,R.Vector(),Settings.MeleeRange,UHeliosCombatTempoSettings::Hunter(this,false,true,Skill?TEXT("技能碰撞半径"):TEXT("近战碰撞半径"),Skill?110:85,1,2000),HeliosSSX::Damage(this,Timeline.Attack,true),[this,R](AHeliosEnemyActor* E,const FHitResult& H,bool Killed)
    {
        if(Killed)Voice->Play(TEXT("Kill"));
        const FVector At=H.ImpactPoint.IsNearlyZero()?E->GetActorLocation():FVector(H.ImpactPoint);
        if(auto* V=AHeliosSSXEffect::Spawn(this,TEXT("FX_SunShangXiang_Damage"),At,R,false))SourceEffects.Add(V);
    });
    HitsDelivered+=Damaged;
    if(HitAudioGate.Accept(Phase,Damaged)&&HitSound)HeliosAudio::PlaySoundAtLocation(this,HitSound,Start,.55f*RosterAudioGain());
}
void AHeliosSunShangXiangPet::ShowEffect(int32 Phase)
{
    const bool Skill=Timeline.Attack==HeliosSSX::EAttack::Skill;
    const FVector Start=GetActorLocation()+FVector(0,0,30);const FRotator R=GetActorRotation();
    const FString FX=Skill?FString::Printf(TEXT("FX_3P_SunShangXiang_Skill%d"),Phase+1):FString::Printf(TEXT("FX_3P_SunShangXiang_Trail%02d"),Timeline.Attack==HeliosSSX::EAttack::Heavy?3:Phase+1);
    if(auto* V=AHeliosSSXEffect::Spawn(this,FX,Start,R,false,PlaybackSpeed)){V->SetActorScale3D(FVector(GetMesh()->GetRelativeScale3D().X/1.7f));V->AttachToComponent(GetRootComponent(),FAttachmentTransformRules::KeepWorldTransform);SourceEffects.Add(V);}
}
