#include "Pets/HeliosZhongXiaoKuiPet.h"
#include "Combat/HeliosCombatTempoSettings.h"
#include "Weapons/HeliosZhongXiaoKui.h"
#include "Weapons/HeliosSunShangXiang.h"
#include "Weapons/HeliosSSXVoice.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Components/AudioComponent.h"
#include "Weapons/HeliosHandAnimInstance.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Player/HeliosArenaCharacter.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
AHeliosZhongXiaoKuiPet::AHeliosZhongXiaoKuiPet()
{
    Kind=EHeliosPetKind::ZhongXiaoKui;Settings.MaxHealth=600;Settings.MeleeRange=260;Settings.AttackInterval=1;Settings.MovementMultiplier=1.f;
    Voice=CreateDefaultSubobject<UHeliosSSXVoiceComponent>(TEXT("Original HunterOni voice"));
    // Same body height and capsule as the confirmed Butterfly/Thunder reference.
    GetCapsuleComponent()->InitCapsuleSize(34,102);GetMesh()->SetRelativeLocation(FVector(0,0,-102));GetMesh()->SetRelativeRotation(FRotator(0,-90,0));GetMesh()->SetRelativeScale3D(FVector(2.079117f));
    GetMesh()->SetBoundsScale(2);GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    GetCharacterMovement()->MaxWalkSpeed=520;
}
void AHeliosZhongXiaoKuiPet::LoadModel()
{
    ResidentEffectLibrary=LoadObject<UHeliosSSXEffectLibrary>(nullptr,TEXT("/Game/Helios/CFMZhongXiaoKui/Effects/DA_ZXK_Effects.DA_ZXK_Effects"));
    if(ResidentEffectLibrary)ResidentEffectLibrary->GetParsedRecipe();
    HeliosZXK::InitializeVoice(Voice,true);
    GetMesh()->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Helios/CFMZhongXiaoKui/SK_ZhongXiaoKui_TP.SK_ZhongXiaoKui_TP")));
    if(auto* Model=GetMesh()->GetSkeletalMeshAsset())
        GetMesh()->SetRelativeLocation(FVector(0,0,-GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()-(Model->GetBounds().Origin.Z-Model->GetBounds().BoxExtent.Z)*GetMesh()->GetRelativeScale3D().Z));
    GetMesh()->SetAnimInstanceClass(UHeliosHandAnimInstance::StaticClass());bModelReady=GetMesh()->GetSkeletalMeshAsset()!=nullptr;
    if(auto* Anim=Cast<UHeliosHandAnimInstance>(GetMesh()->GetAnimInstance()))Anim->bPreserveTransitionVelocity=false;
    GetMesh()->bEnableUpdateRateOptimizations=false;GetMesh()->AddTickPrerequisiteActor(this);
    for(const TCHAR* N:{TEXT("Idle"),TEXT("Run_Fwd"),TEXT("Run_Bwd"),TEXT("Run_Lt"),TEXT("Run_Rt"),TEXT("Hit"),TEXT("Dead"),TEXT("Att01"),TEXT("Att01_Recover"),TEXT("Att02"),TEXT("Att02_Recover"),TEXT("Att03"),TEXT("Att03_Recover")})Poses.Add(N,HeliosZXK::Animation(false,N));
}
void AHeliosZhongXiaoKuiPet::PoseAt(FName Name,float Time,bool Loop)
{
    if(auto* Anim=Cast<UHeliosHandAnimInstance>(GetMesh()->GetAnimInstance()))
    {Anim->SetTimelinePose(Poses.FindRef(Name),Time,Loop);GetMesh()->TickAnimation(0,false);GetMesh()->RefreshBoneTransforms();}
}
void AHeliosZhongXiaoKuiPet::SetPose(FName Name,bool Loop)
{
    if(CurrentPose!=Name){CurrentPose=Name;IdleAge=0;}
    const bool Dead=Name==TEXT("Down")||Name==TEXT("Sleep");const FName Clip=Dead?FName(TEXT("Dead")):Name==TEXT("Move")?MovePose:Name==TEXT("Hit")?Name:FName(TEXT("Idle"));
    UAnimSequence* A=Poses.FindRef(Clip);PoseAt(Clip,Name==TEXT("Sleep")&&A?A->GetPlayLength():Name==TEXT("Move")?MoveAge:IdleAge,Dead?false:Loop);
}
void AHeliosZhongXiaoKuiPet::UpdateLocomotion(float Dt)
{
    IdleAge+=Dt;const float Speed=GetVelocity().Size2D();if(Speed<=20){MoveAge=0;return;}
    const FVector Local=GetActorRotation().UnrotateVector(GetVelocity());const float Angle=FMath::RadiansToDegrees(FMath::Atan2(Local.Y,Local.X));
    const float Centre=MovePose==TEXT("Run_Bwd")?180.f:MovePose==TEXT("Run_Rt")?90.f:MovePose==TEXT("Run_Lt")?-90.f:0.f;
    if(FMath::Abs(FMath::FindDeltaAngleDegrees(Centre,Angle))>55.f)MovePose=FMath::Abs(Angle)>135.f?TEXT("Run_Bwd"):Angle>45.f?TEXT("Run_Rt"):Angle<-45.f?TEXT("Run_Lt"):TEXT("Run_Fwd");
    // Measured original planted-ankle speeds at mesh scale 1.7; lateral clips
    // have shorter strides than forward running (gait_speed_source_audit.json).
    const float StrideSpeed=MovePose==TEXT("Run_Bwd")?435.7988f:MovePose==TEXT("Run_Lt")?362.9476f:MovePose==TEXT("Run_Rt")?310.7046f:471.1924f;
    MoveAge+=Dt*Speed/(StrideSpeed*GetMesh()->GetRelativeScale3D().X/1.7f);
}
void AHeliosZhongXiaoKuiPet::TrackCombatTarget(float Dt)
{
    if(AttackAge>=0){GetCharacterMovement()->bOrientRotationToMovement=false;if(auto* AI=Cast<AAIController>(GetController()))AI->StopMovement();GetCharacterMovement()->StopMovementImmediately();bMoveRequested=false;return;}
    GetCharacterMovement()->bOrientRotationToMovement=true;Super::TrackCombatTarget(Dt);
}
void AHeliosZhongXiaoKuiPet::CancelAttack(){bContact=false;for(UAudioComponent* Channel:ActionChannels)if(IsValid(Channel))Channel->Stop();ActionChannels.Reset();for(AHeliosSSXEffect* Effect:SourceEffects)if(IsValid(Effect))Effect->Destroy();SourceEffects.Reset();Super::CancelAttack();}
void AHeliosZhongXiaoKuiPet::EndPlay(const EEndPlayReason::Type Reason){CancelAttack();Voice->Stop();Super::EndPlay(Reason);}
float AHeliosZhongXiaoKuiPet::TakeDamage(float Amount,const FDamageEvent& Event,AController* InstigatorController,AActor* Causer)
{
    const float Applied=Super::TakeDamage(Amount,Event,InstigatorController,Causer);
    if(Applied>0){const bool Dead=IsSleeping();Voice->PlaySource(Dead?TEXT("Death"):TEXT("Hurt"),HeliosZXK::OriginalSound(Dead?32402:32404));}return Applied;
}
void AHeliosZhongXiaoKuiPet::AudioBetween(float Previous,float Time)
{
    if(UAnimSequence* Clip=Poses.FindRef(Action))
    {
        const float Length=Clip->GetPlayLength();
        HeliosZXK::PlayEvents(this,Voice,false,Action,Previous,FMath::Min(Time,Length),ActionChannels);
        if(Time>=Length&&Poses.FindRef(Recover))HeliosZXK::PlayEvents(this,Voice,false,Recover,Previous<Length?-KINDA_SMALL_NUMBER:Previous-Length,Time-Length,ActionChannels);
    }
    ActionChannels.RemoveAll([](UAudioComponent* Channel){return !IsValid(Channel)||!Channel->IsPlaying();});
}
void AHeliosZhongXiaoKuiPet::PoseAttack()
{
    FName Clip=Action;UAnimSequence* A=Poses.FindRef(Clip);float Time=AttackAge;
    if(A&&AttackAge>A->GetPlayLength()&&Poses.FindRef(Recover)){Time-=A->GetPlayLength();Clip=Recover;}
    PoseAt(Clip,Time,false);
}
void AHeliosZhongXiaoKuiPet::UpdateAttack(float Dt)
{
    SourceEffects.RemoveAll([](AHeliosSSXEffect* Effect){return !IsValid(Effect);});
    if(AttackAge>=0)
    {
        const float Previous=AttackAge;AttackAge+=Dt*UHeliosCombatTempoSettings::Hunter(this,true,true,TEXT("动作倍率"),1,.1,5);AudioBetween(Previous,AttackAge);UAnimSequence* A=Poses.FindRef(Action);
        if(A&&!bContact&&AttackAge>=A->GetPlayLength()*.45f)
        {
            bContact=true;auto* E=Target.Get();const FVector Start=GetActorLocation()+FVector(0,0,40);
            if(E)Contacts+=HeliosSSX::Strike(this,this,CompanionOwner.Get(),Start,(E->GetActorLocation()-Start).GetSafeNormal(),Settings.MeleeRange,UHeliosCombatTempoSettings::Hunter(this,true,true,TEXT("近战碰撞半径"),85,1,2000),UHeliosCombatTempoSettings::Hunter(this,true,true,TEXT("轻击伤害"),100,0,100000),[this](AHeliosEnemyActor*,const FHitResult&,bool Killed){if(Killed)Voice->Play(TEXT("Kill"));});
        }
        PoseAttack();if(AttackAge>=SourceDuration){AttackAge=-1;CurrentPose=NAME_None;DecisionRemaining=0;}return;
    }
    if(HitPoseRemaining>0){SetPose(TEXT("Hit"),false);return;}
    auto* E=Target.Get();if(!E||E->IsEnemyDead()||Cooldown>0||!HasClearLine(E)||FVector::DistSquared2D(GetActorLocation(),E->GetActorLocation())>FMath::Square(Settings.MeleeRange)){SetPose(GetVelocity().Size2D()>20?TEXT("Move"):TEXT("Idle"));return;}
    const TCHAR* Names[]={TEXT("Att01"),TEXT("Att02"),TEXT("Att03")};const int32 I=AttackSerial++%3;
    Action=Names[I];Recover=FName(FString(Names[I])+TEXT("_Recover"));UAnimSequence* A=Poses.FindRef(Action);UAnimSequence* R=Poses.FindRef(Recover);if(!A)return;
    SourceDuration=A->GetPlayLength()+(R?R->GetPlayLength():0);AttackAge=0;bContact=false;Cooldown=SourceDuration/UHeliosCombatTempoSettings::Hunter(this,true,true,TEXT("动作倍率"),1,.1,5)+UHeliosCombatTempoSettings::Hunter(this,true,true,TEXT("攻击额外间隔"),0,0,30);
    if(auto* AI=Cast<AAIController>(GetController()))AI->StopMovement();GetCharacterMovement()->StopMovementImmediately();bMoveRequested=false;AudioBetween(-KINDA_SMALL_NUMBER,0);PoseAttack();
    const FString Name=TEXT("FX_ShengHua_ZhongXiaoKui_b_Root_3P_A_")+Action.ToString();
    // The source b_Root bind rotates Unity X by -90 degrees. In the effect
    // coordinate basis (z,x,y) this is a -90-degree rotation around UE Y.
    const FQuat SourceRootBasis(FVector::RightVector,-.5f*PI);
    if(auto* Effect=HeliosZXK::Effect(this,Name,GetMesh()->GetSocketLocation(TEXT("b_Root")),(GetActorQuat()*SourceRootBasis).Rotator(),false))
    {Effect->SetActorScale3D(GetMesh()->GetComponentScale());Effect->AttachToComponent(GetMesh(),FAttachmentTransformRules::KeepWorldTransform,TEXT("b_Root"));SourceEffects.Add(Effect);}
}
