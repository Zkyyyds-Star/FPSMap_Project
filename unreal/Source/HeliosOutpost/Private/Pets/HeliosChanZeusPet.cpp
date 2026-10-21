#include "Pets/HeliosChanZeusPet.h"
#include "Audio/HeliosAudio.h"
#include "Weapons/HeliosHandAnimInstance.h"
#include "Weapons/HeliosSSXVoice.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Player/HeliosArenaCharacter.h"
#include "AIController.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/AudioComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"

AHeliosChanZeusPet::AHeliosChanZeusPet()
{
    Kind=EHeliosPetKind::ChanZeus;GetCapsuleComponent()->InitCapsuleSize(34,102);
    Zeus=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Original Zeus third person"));Zeus->SetupAttachment(GetMesh(),TEXT("b_RightWeapon"));Zeus->SetCollisionEnabled(ECollisionEnabled::NoCollision);Zeus->SetCanEverAffectNavigation(false);
    Voice=CreateDefaultSubobject<UHeliosSSXVoiceComponent>(TEXT("Original Chan BL spatial voice"));
    GetMesh()->SetBoundsScale(2);GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
}
void AHeliosChanZeusPet::LoadModel()
{
    auto S=[](const TCHAR* Key,float D,float Min,float Max){return HeliosChanZeus::Setting(true,Key,D,Min,Max);};
    Settings.MaxHealth=S(TEXT("生命"),300,1,1000000);Health=Settings.MaxHealth;Settings.SleepSeconds=S(TEXT("倒地恢复时间"),30,.1,600);Settings.MovementMultiplier=1;
    Settings.FollowDistance=S(TEXT("跟随距离"),1400,100,10000);Settings.CatchUpDistance=FMath::Max(Settings.FollowDistance,S(TEXT("追上距离"),3000,100,20000));Settings.CombatRadius=S(TEXT("寻敌半径"),4500,100,20000);
    Settings.FollowMinSpeed=S(TEXT("跟随最低速度"),420,10,5000);Settings.FollowMaxSpeed=S(TEXT("跟随最高速度"),700,Settings.FollowMinSpeed,5000);Settings.ChaseSpeed=S(TEXT("战斗追敌速度"),520,10,5000);Settings.CatchUpSpeed=S(TEXT("远处追赶速度"),850,10,5000);
    ShotSettings=HeliosChanZeus::Tuning(true);Ammo=ShotSettings.Magazine;
    auto* Model=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Helios/CFMChanZeus/SK_DiaoChan_TP"));GetMesh()->SetSkeletalMesh(Model);bModelReady=Model!=nullptr;
    if(Model)
    {
        const float RawHeight=2*Model->GetBounds().BoxExtent.Z;const float Scale=S(TEXT("可见身高"),203.3593f,100,300)/FMath::Max(1.f,RawHeight);
        GetMesh()->SetRelativeScale3D(FVector(Scale));GetMesh()->SetRelativeLocation(FVector(0,0,-GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()-(Model->GetBounds().Origin.Z-Model->GetBounds().BoxExtent.Z)*Scale));
        UE_LOG(LogTemp,Display,TEXT("CHAN_PET_MODEL source_height=%.6f scale=%.7f visible_height=%.6f floor_offset=%.6f"),RawHeight,Scale,RawHeight*Scale,GetMesh()->GetRelativeLocation().Z);
    }
    GetMesh()->SetAnimInstanceClass(UHeliosHandAnimInstance::StaticClass());GetMesh()->AddTickPrerequisiteActor(this);GetMesh()->bEnableUpdateRateOptimizations=false;
    if(auto* A=Cast<UHeliosHandAnimInstance>(GetMesh()->GetAnimInstance())){A->bPreserveTransitionVelocity=false;A->BlendDuration=.08f;}
    Zeus->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Helios/CFMChanZeus/SM_Zeus_TP")));Zeus->AttachToComponent(GetMesh(),FAttachmentTransformRules::SnapToTargetNotIncludingScale,TEXT("b_RightWeapon"));Zeus->SetRelativeTransform(FTransform::Identity);
    for(FName N:{FName("Idle"),FName("Run_Fwd"),FName("Run_Bwd"),FName("Run_LT"),FName("Run_RT"),FName("ChangeClip"),FName("Fire"),FName("Hit"),FName("Dead_Front")})Poses.Add(N,HeliosChanZeus::Animation(TEXT("A_DIAOCHAN_TP_"),N));
    for(int32 Id:{12239,18111,18021,18022,18023,18024})Sounds.Add(Id,HeliosChanZeus::Sound(Id));HeliosChanZeus::InitializeVoice(Voice,true);
    IdleEffect=HeliosChanZeus::AttachedEffect(this,Zeus,TEXT("FX_Zeus_Idle_TP"),false,.01f);
}
void AHeliosChanZeusPet::PoseAt(FName Clip,float Time,bool Loop)
{if(auto* A=Cast<UHeliosHandAnimInstance>(GetMesh()->GetAnimInstance())){A->SetTimelinePose(Poses.FindRef(Clip),Time,Loop);GetMesh()->TickAnimation(0,false);GetMesh()->RefreshBoneTransforms();}}
void AHeliosChanZeusPet::SetPose(FName Name,bool Loop)
{
    if(CurrentPose!=Name){CurrentPose=Name;PoseAge=0;}
    const bool Dead=Name==TEXT("Down")||Name==TEXT("Sleep");
    PoseAt(Dead?FName("Dead_Front"):Name==TEXT("Move")?MovePose:Name==TEXT("Hit")?Name:FName("Idle"),Dead?(Name==TEXT("Sleep")?1.133333f:PoseAge):Name==TEXT("Move")?MoveAge:PoseAge,Dead?false:Loop);
}
void AHeliosChanZeusPet::UpdateLocomotion(float Dt)
{
    const float Speed=GetVelocity().Size2D();if(Speed<20){MoveAge=0;return;}MoveAge+=Dt*FMath::Clamp(Speed/520.f,.05f,2.f);
    const FVector Local=GetActorRotation().UnrotateVector(GetVelocity());const float Angle=FMath::RadiansToDegrees(FMath::Atan2(Local.Y,Local.X));
    MovePose=FMath::Abs(Angle)>135?TEXT("Run_Bwd"):Angle>45?TEXT("Run_RT"):Angle<-45?TEXT("Run_LT"):TEXT("Run_Fwd");
}
bool AHeliosChanZeusPet::HasMuzzleClearLine(const AHeliosEnemyActor* Enemy) const
{
    if(!Enemy||!GetWorld())return false;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(PetMuzzleTargetSight),true,this);Q.AddIgnoredActor(CompanionOwner.Get());
    if(auto* Roster=GetWorld()->GetSubsystem<UHeliosPetWorld>())Roster->IgnoreAll(Q);
    FHitResult Hit;
    return !GetWorld()->LineTraceSingleByChannel(Hit,MuzzleLocation(),Enemy->GetActorLocation(),ECC_Visibility,Q)||Hit.GetActor()==Enemy;
}
void AHeliosChanZeusPet::TrackCombatTarget(float Dt)
{
    auto* E=Target.Get();if(!E)return;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(PetMuzzleClearance),true,this);Q.AddIgnoredActor(CompanionOwner.Get());
    if(auto* Roster=GetWorld()->GetSubsystem<UHeliosPetWorld>())Roster->IgnoreAll(Q);
    FHitResult Obstacle;const bool MuzzleBlocked=GetWorld()->LineTraceSingleByChannel(Obstacle,GetActorLocation()+FVector(0,0,35),MuzzleLocation(),ECC_Visibility,Q);
    const bool CanFire=HasClearLine(E)&&HasMuzzleClearLine(E)&&(!MuzzleBlocked||Cast<AHeliosEnemyActor>(Obstacle.GetActor()))&&FVector::Dist2D(GetActorLocation(),E->GetActorLocation())<=FMath::Min(1800.f,ShotSettings.Range*.8f);
    GetCharacterMovement()->bOrientRotationToMovement=!CanFire;
    if(CanFire)
    {if(auto* AI=Cast<AAIController>(GetController()))AI->StopMovement();GetCharacterMovement()->StopMovementImmediately();bMoveRequested=false;const FVector To=E->GetActorLocation()-GetActorLocation();SetActorRotation(FMath::RInterpTo(GetActorRotation(),To.GetSafeNormal2D().Rotation(),Dt,12));}
    else MoveTowards(E->GetActorLocation(),300,Dt);
}
FVector AHeliosChanZeusPet::MuzzleLocation()const
{
    // This source static FBX retains metre-valued vertices. The authored weapon
    // bone carries the source 100x unit scale; do not apply it twice to sockets.
    return Zeus->GetComponentTransform().TransformPosition(FVector(.4211158f,.00063156f,-.0414553f));
}
void AHeliosChanZeusPet::PlayActionSound(int32 Id)
{
    if(auto* S=Sounds.FindRef(Id).Get())
    {
        // World gun sounds follow the partner. Character speech uses its own 6 m falloff.
        auto* Att=NewObject<USoundAttenuation>(this);Att->Attenuation.bSpatialize=true;Att->Attenuation.AttenuationShapeExtents=FVector(200,0,0);Att->Attenuation.FalloffDistance=2800;
        if(auto* C=HeliosAudio::SpawnSoundAttached(S,Zeus,NAME_None,FVector::ZeroVector,EAttachLocation::KeepRelativeOffset,true,.6f*RosterAudioGain(),1,0,Att))SoundChannels.Add(C);
    }
}
void AHeliosChanZeusPet::FireRound()
{
    auto* E=Target.Get();if(!E||E->IsEnemyDead())return;PoseAt(TEXT("Fire"),0,false);
    const FVector View=GetActorLocation()+FVector(0,0,35);const FVector To=E->GetActorLocation()-View;int32 Hits=0;
    const int32 Kills=HeliosChanZeus::FirePellets(this,CompanionOwner.Get(),CompanionOwner.Get(),View,To.GetSafeNormal(),MuzzleLocation(),ShotSettings,SpreadRandom,Hits);
    HitsDelivered+=Hits;TrueKills+=Kills;for(int32 I=0;I<Kills;++I)Voice->Play(TEXT("Kill"));
    ++ShotsFired;--Ammo;FireAge=0;ShotWait=ShotSettings.Interval;PlayActionSound(12239);PlayActionSound(18111);
    const auto Rotation=To.Rotation();HeliosChanZeus::Effect(this,TEXT("P_Gunfire_3P_Zeus"),MuzzleLocation(),Rotation,false);HeliosChanZeus::Effect(this,TEXT("P_Gunfire_3P_Zeus_Lightning"),MuzzleLocation(),Rotation,false);
    UE_LOG(LogTemp,Display,TEXT("CHAN_ZEUS_SHOT source=pet shot=%d hits=%d kills=%d ammo=%d muzzle=%s"),ShotsFired,Hits,Kills,Ammo,*MuzzleLocation().ToCompactString());
}
void AHeliosChanZeusPet::UpdateAttack(float Dt)
{
    ShotWait=FMath::Max(0.f,ShotWait-Dt);FireAge+=Dt;
    if(ReloadAge>=0)
    {
        ReloadAge+=Dt;AttackAge=ReloadAge;PoseAt(TEXT("ChangeClip"),ReloadAge*2/FMath::Max(.01f,ShotSettings.Reload),false);
        auto Event=[&](uint32 Bit,float At,int32 Id){if(!(ReloadEvents&Bit)&&ReloadAge>=At*ShotSettings.Reload/2){ReloadEvents|=Bit;PlayActionSound(Id);}};
        Event(1,.25129804f,18021);Event(2,.79127723f,18024);Event(4,1.56386292f,18023);
        if(ReloadAge>=ShotSettings.Reload){Ammo=ShotSettings.Magazine;ReloadAge=-1;AttackAge=-1;++Reloads;CurrentPose=NAME_None;}return;
    }
    if(Ammo<=0){ReloadAge=0;ReloadEvents=0;AttackAge=0;return;}
    if(FireAge<.1f){PoseAt(TEXT("Fire"),FireAge,false);return;}
    if(HitPoseRemaining>0){SetPose(TEXT("Hit"),false);return;}
    auto* E=Target.Get();const bool Facing=E&&FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,(E->GetActorLocation()-GetActorLocation()).Rotation().Yaw))<6;
    if(E&&!E->IsEnemyDead()&&Facing&&!GetCharacterMovement()->IsFalling()&&GetVelocity().Size2D()<20&&ShotWait<=0&&FVector::Dist2D(GetActorLocation(),E->GetActorLocation())<=ShotSettings.Range&&HasClearLine(E)&&HasMuzzleClearLine(E))FireRound();
    else SetPose(GetVelocity().Size2D()>20?FName("Move"):FName("Idle"));
}
void AHeliosChanZeusPet::Tick(float Dt)
{PoseAge+=Dt;Super::Tick(Dt);if(IsValid(IdleEffect))IdleEffect->SetActorHiddenInGame(IsSleeping());if(!Target.IsValid())GetCharacterMovement()->bOrientRotationToMovement=true;SoundChannels.RemoveAll([](const auto& C){return !IsValid(C)||!C->IsPlaying();});}
void AHeliosChanZeusPet::CancelAttack(){ReloadAge=-1;FireAge=100;Super::CancelAttack();}
float AHeliosChanZeusPet::TakeDamage(float Amount,const FDamageEvent& Event,AController* C,AActor* Source)
{const float Applied=Super::TakeDamage(Amount,Event,C,Source);if(Applied>0){if(IsSleeping())Voice->Stop();HeliosChanZeus::CombatVoice(Voice,IsSleeping()?FName("Death"):FName("Hurt"));}return Applied;}
void AHeliosChanZeusPet::EndPlay(const EEndPlayReason::Type Reason)
{if(IsValid(IdleEffect))IdleEffect->Destroy();Voice->Stop();for(const auto& C:SoundChannels)if(IsValid(C))C->Stop();Super::EndPlay(Reason);}
