#include "Pets/HeliosYunSummerMegatronPet.h"
#include "Audio/HeliosAudio.h"
#include "Weapons/HeliosHandAnimInstance.h"
#include "Weapons/HeliosSSXVoice.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Player/HeliosArenaCharacter.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Animation/AnimSequence.h"
#include "Camera/PlayerCameraManager.h"
#include "AIController.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

AHeliosYunSummerMegatronPet::AHeliosYunSummerMegatronPet()
{
    Kind=EHeliosPetKind::YunSummerMegatron;GetCapsuleComponent()->InitCapsuleSize(34,96);
    Megatron=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Original Megatron TP"));Megatron->SetupAttachment(GetMesh(),TEXT("b_RightWeapon"));Megatron->SetCollisionEnabled(ECollisionEnabled::NoCollision);Megatron->SetCanEverAffectNavigation(false);
    Voice=CreateDefaultSubobject<UHeliosSSXVoiceComponent>(TEXT("Original YunSummer spatial voice"));
    GetMesh()->SetBoundsScale(2);GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
}
FString AHeliosYunSummerMegatronPet::DisplayName() const
{return FString::Printf(TEXT("云悠悠·沐夏微醺 / %s / 威震天"),HeliosYunSummer::FormLabel(Form));}
void AHeliosYunSummerMegatronPet::LoadModel()
{
    auto S=[](const TCHAR* Key,float D,float Min,float Max){return HeliosYunSummer::Setting(true,Key,D,Min,Max);};
    Settings.MaxHealth=S(TEXT("生命"),300,1,1000000);Health=Settings.MaxHealth;Settings.SleepSeconds=S(TEXT("倒地恢复时间"),30,.1,600);Settings.ShieldReduction=0;
    Settings.FollowDistance=S(TEXT("跟随距离"),1400,100,10000);Settings.CatchUpDistance=FMath::Max(Settings.FollowDistance,S(TEXT("追上距离"),3000,100,20000));Settings.CombatRadius=S(TEXT("寻敌半径"),4500,100,20000);
    Settings.FollowMinSpeed=S(TEXT("跟随最低速度"),420,10,5000);Settings.FollowMaxSpeed=S(TEXT("跟随最高速度"),700,Settings.FollowMinSpeed,5000);Settings.ChaseSpeed=S(TEXT("战斗追敌速度"),520,10,5000);Settings.CatchUpSpeed=S(TEXT("远处追赶速度"),850,10,5000);
    ShotSettings=HeliosYunSummer::Tuning(true);Ammo=ShotSettings.Magazine;
    if(auto* GI=GetGameInstance())if(auto* Session=GI->GetSubsystem<UHeliosYunSummerSession>())Form=Session->GetPetForm(RosterSlot);
    Megatron->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Helios/CFMYunSummerMegatron/SM_MEGATRON_TP")));ApplyForm();
    IdleEffect=HeliosYunSummer::AttachedEffect(this,Megatron,TEXT("FX_3P_MG36_WeiZhenTian_Idle_TP"),false);
    if(IsValid(IdleEffect))IdleEffect->FollowTravelingEmitter();
}
void AHeliosYunSummerMegatronPet::ApplyForm()
{
    const FString Code=HeliosYunSummer::FormCode(Form);
    auto* Model=LoadObject<USkeletalMesh>(nullptr,*(TEXT("/Game/Helios/CFMYunSummerMegatron/SK_YUN_")+Code+TEXT("_TP")));
    GetMesh()->SetSkeletalMesh(Model);bModelReady=Model!=nullptr;
    if(Model)
    {
        const float RawHeight=2*Model->GetBounds().BoxExtent.Z;
        const float Scale=HeliosYunSummer::Setting(true,TEXT("可见身高"),190,100,300)/FMath::Max(1.f,RawHeight);
        GetMesh()->SetRelativeScale3D(FVector(Scale));GetMesh()->SetRelativeLocation(FVector(0,0,-GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()-(Model->GetBounds().Origin.Z-Model->GetBounds().BoxExtent.Z)*Scale));
    }
    GetMesh()->SetAnimInstanceClass(UHeliosHandAnimInstance::StaticClass());GetMesh()->AddTickPrerequisiteActor(this);GetMesh()->bEnableUpdateRateOptimizations=false;
    if(auto* A=Cast<UHeliosHandAnimInstance>(GetMesh()->GetAnimInstance())){A->bPreserveTransitionVelocity=false;A->BlendDuration=.08f;}
    Megatron->AttachToComponent(GetMesh(),FAttachmentTransformRules::SnapToTargetNotIncludingScale,TEXT("b_RightWeapon"));Megatron->SetRelativeTransform(FTransform::Identity);
    Poses.Reset();for(FName N:{FName("Idle"),FName("Run_Fwd"),FName("Run_Bwd"),FName("Run_LT"),FName("Run_RT"),FName("ChangeClip"),FName("Fire"),FName("Hit"),FName("Dead_Front")})Poses.Add(N,HeliosYunSummer::Animation(TEXT("A_YUN_")+Code+TEXT("_TP_"),N));
    Settings.MovementMultiplier=Form==EHeliosYunSummerForm::SP?HeliosYunSummer::Setting(true,TEXT("灵魂武者移速倍率"),1.2f,1,3):1;
    HeliosYunSummer::InitializeVoice(Voice,Form,true);CurrentPose=NAME_None;SetPose(IsSleeping()?FName("Sleep"):FName("Idle"));
}
void AHeliosYunSummerMegatronPet::SetForm(EHeliosYunSummerForm InForm)
{
    if(uint8(InForm)>uint8(EHeliosYunSummerForm::SP))return;
    if(IsReloading()){PendingForm=InForm;bFormPending=true;return;}
    if(Form==InForm)return;Form=InForm;ApplyForm();
    if(auto* GI=GetGameInstance())if(auto* Session=GI->GetSubsystem<UHeliosYunSummerSession>())Session->SetPetForm(RosterSlot,Form);
}
void AHeliosYunSummerMegatronPet::PoseAt(FName Clip,float Time,bool Loop)
{if(auto* A=Cast<UHeliosHandAnimInstance>(GetMesh()->GetAnimInstance())){A->SetTimelinePose(Poses.FindRef(Clip),Time,Loop);GetMesh()->TickAnimation(0,false);GetMesh()->RefreshBoneTransforms();}}
void AHeliosYunSummerMegatronPet::SetPose(FName Name,bool Loop)
{
    if(CurrentPose!=Name){CurrentPose=Name;PoseAge=0;}
    const bool Dead=Name==TEXT("Down")||Name==TEXT("Sleep");
    const auto Death=Poses.FindRef(TEXT("Dead_Front"));const float End=Death?Death->GetPlayLength():1.1f;
    PoseAt(Dead?FName("Dead_Front"):Name==TEXT("Move")?MovePose:Name==TEXT("Hit")?Name:FName("Idle"),Dead?(Name==TEXT("Sleep")?End:PoseAge):Name==TEXT("Move")?MoveAge:PoseAge,Dead?false:Loop);
}
float AHeliosYunSummerMegatronPet::RecoveryDuration(FName Pose,float Fallback) const
{const auto Death=Poses.FindRef(TEXT("Dead_Front"));return Pose==TEXT("Down")?(Death?Death->GetPlayLength():1.1f):Pose==TEXT("Wake")?.3f:Fallback;}
void AHeliosYunSummerMegatronPet::UpdateRecoveryGround(float Dt)
{if(IsSleeping()&&DownRemaining>0)SetPose(TEXT("Down"),false);}
void AHeliosYunSummerMegatronPet::UpdateLocomotion(float Dt)
{
    const float Speed=GetVelocity().Size2D();if(Speed<20){MoveAge=0;return;}MoveAge+=Dt*FMath::Clamp(Speed/(520.f*Settings.MovementMultiplier),.05f,2.f);
    const FVector Local=GetActorRotation().UnrotateVector(GetVelocity());const float Angle=FMath::RadiansToDegrees(FMath::Atan2(Local.Y,Local.X));
    MovePose=FMath::Abs(Angle)>135?TEXT("Run_Bwd"):Angle>45?TEXT("Run_RT"):Angle<-45?TEXT("Run_LT"):TEXT("Run_Fwd");
}
bool AHeliosYunSummerMegatronPet::HasMuzzleClearLine(const AHeliosEnemyActor* Enemy) const
{
    if(!Enemy||!GetWorld())return false;FCollisionQueryParams Q(SCENE_QUERY_STAT(YunPetMuzzleSight),true,this);Q.AddIgnoredActor(CompanionOwner.Get());
    if(auto* Roster=GetWorld()->GetSubsystem<UHeliosPetWorld>())Roster->IgnoreAll(Q);
    FHitResult H;return !GetWorld()->SweepSingleByChannel(H,MuzzleLocation(),Enemy->GetActorLocation(),FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(5),Q)||H.GetActor()==Enemy;
}
bool AHeliosYunSummerMegatronPet::HasMuzzleClearance() const
{
    if(!GetWorld())return false;FCollisionQueryParams Q(SCENE_QUERY_STAT(YunPetMuzzleClearance),true,this);Q.AddIgnoredActor(CompanionOwner.Get());
    if(auto* Roster=GetWorld()->GetSubsystem<UHeliosPetWorld>())Roster->IgnoreAll(Q);
    FHitResult H;return !GetWorld()->SweepSingleByChannel(H,GetActorLocation()+FVector(0,0,35),MuzzleLocation(),FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(5),Q)||Cast<AHeliosEnemyActor>(H.GetActor())!=nullptr;
}
void AHeliosYunSummerMegatronPet::TrackCombatTarget(float Dt)
{
    auto* E=Target.Get();if(!E)return;
    const bool CanFire=HasClearLine(E)&&HasMuzzleClearLine(E)&&HasMuzzleClearance()&&FVector::Dist2D(GetActorLocation(),E->GetActorLocation())<=2500;
    GetCharacterMovement()->bOrientRotationToMovement=!CanFire;
    if(CanFire){if(auto* AI=Cast<AAIController>(GetController()))AI->StopMovement();GetCharacterMovement()->StopMovementImmediately();bMoveRequested=false;SetActorRotation(FMath::RInterpTo(GetActorRotation(),(E->GetActorLocation()-GetActorLocation()).GetSafeNormal2D().Rotation(),Dt,12));}
    else MoveTowards(E->GetActorLocation(),300,Dt);
}
FVector AHeliosYunSummerMegatronPet::MuzzleLocation() const
{return Megatron->DoesSocketExist(TEXT("MuzzleFlashSocket"))?Megatron->GetSocketLocation(TEXT("MuzzleFlashSocket")):Megatron->GetComponentTransform().TransformPosition(FVector(.4540651f,-.07867545f,-.04433134f));}
void AHeliosYunSummerMegatronPet::PlayActionSound(int32 Id)
{
    if(!Sounds.Contains(Id))Sounds.Add(Id,HeliosYunSummer::Sound(Id));
    if(auto* S=Sounds.FindRef(Id).Get())
    {
        auto* Att=NewObject<USoundAttenuation>(this);Att->Attenuation.bSpatialize=true;Att->Attenuation.AttenuationShapeExtents=FVector(200,0,0);Att->Attenuation.FalloffDistance=2800;
        if(auto* C=HeliosAudio::SpawnSoundAttached(S,Megatron,NAME_None,FVector::ZeroVector,EAttachLocation::KeepRelativeOffset,true,.6f*RosterAudioGain(),1,0,Att))SoundChannels.Add(C);
    }
}
void AHeliosYunSummerMegatronPet::RecordImpact(int32 Hits,int32 Kills)
{HitsDelivered+=Hits;TrueKills+=Kills;if(!IsSleeping())for(int32 I=0;I<Kills;++I)Voice->Play(TEXT("Kill"));}
void AHeliosYunSummerMegatronPet::RecordKillEffect(const FVector& At)
{
    if(IsSleeping())return;
    auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0);
    const FRotator Facing=Camera?(Camera->GetCameraLocation()-At).Rotation():GetActorRotation();
    if(auto* E=HeliosYunSummer::Effect(this,TEXT("FX_Transformers_Decepticon_Kill"),At,Facing+FRotator(0,90,0),false))
    {
        KillEffects.Add(E);++KillEffectsSpawned;
        UE_LOG(LogTemp,Display,TEXT("YUN_KILL_EFFECT source=pet slot=%d count=%d target=%s"),RosterSlot,KillEffectsSpawned,*At.ToCompactString());
    }
}
void AHeliosYunSummerMegatronPet::FireRound()
{
    auto* E=Target.Get();if(!E||E->IsEnemyDead()||Ammo<=0||!HasClearLine(E)||!HasMuzzleClearLine(E)||!HasMuzzleClearance())return;
    const FVector View=GetActorLocation()+FVector(0,0,35),To=E->GetActorLocation()-View;
    if(!HeliosYunSummer::Launch(this,CompanionOwner.Get(),View,To.GetSafeNormal(),MuzzleLocation(),ShotSettings,false))return;
    ++ShotsFired;--Ammo;FireAge=0;ShotWait=ShotSettings.Interval;PoseAt(TEXT("Fire"),0,false);PlayActionSound(22456);
    ShotEffects.Add(HeliosYunSummer::Effect(this,TEXT("FX_3P_Gunfire_MG36_WeiZhenTian_PVE"),MuzzleLocation(),To.Rotation(),false));
    UE_LOG(LogTemp,Display,TEXT("YUN_MEGATRON_SHOT source=pet slot=%d shot=%d form=%s ammo=%d muzzle=%s"),RosterSlot,ShotsFired,HeliosYunSummer::FormCode(Form),Ammo,*MuzzleLocation().ToCompactString());
}
void AHeliosYunSummerMegatronPet::UpdateAttack(float Dt)
{
    ShotWait=FMath::Max(0.f,ShotWait-Dt);FireAge+=Dt;
    if(ReloadAge>=0)
    {
        ReloadAge+=Dt;AttackAge=ReloadAge;PoseAt(TEXT("ChangeClip"),ReloadAge*2.933333f/FMath::Max(.01f,ShotSettings.Reload),false);
        auto Event=[&](uint32 Bit,float At,int32 Id){if(!(ReloadEvents&Bit)&&ReloadAge>=At*ShotSettings.Reload/2.933333f){ReloadEvents|=Bit;PlayActionSound(Id);}};
        Event(1,.042f,22464);Event(2,.94f,22463);
        if(ReloadAge>=ShotSettings.Reload){Ammo=ShotSettings.Magazine;ReloadAge=-1;AttackAge=-1;++Reloads;CurrentPose=NAME_None;if(bFormPending){bFormPending=false;SetForm(PendingForm);}}
        return;
    }
    if(Ammo<=0){ReloadAge=0;ReloadEvents=0;AttackAge=0;return;}
    if(FireAge<.5666667f){PoseAt(TEXT("Fire"),FireAge,false);return;}
    if(HitPoseRemaining>0){SetPose(TEXT("Hit"),false);return;}
    auto* E=Target.Get();const bool Facing=E&&FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,(E->GetActorLocation()-GetActorLocation()).Rotation().Yaw))<6;
    if(E&&!E->IsEnemyDead()&&Facing&&!GetCharacterMovement()->IsFalling()&&GetVelocity().Size2D()<20&&ShotWait<=0&&FVector::Dist2D(GetActorLocation(),E->GetActorLocation())<=ShotSettings.Range&&HasClearLine(E)&&HasMuzzleClearLine(E))FireRound();
    else SetPose(GetVelocity().Size2D()>20?FName("Move"):FName("Idle"));
}
void AHeliosYunSummerMegatronPet::Tick(float Dt)
{
    if(GetWorld()&&!GetWorld()->IsPaused())if(auto* GI=GetGameInstance())if(auto* Session=GI->GetSubsystem<UHeliosYunSummerSession>())
    {const auto Desired=Session->GetPetForm(RosterSlot);if(Desired!=Form)SetForm(Desired);}
    PoseAge+=Dt;Super::Tick(Dt);if(!Target.IsValid())GetCharacterMovement()->bOrientRotationToMovement=true;
    if(IsValid(IdleEffect))IdleEffect->SetActorHiddenInGame(IsSleeping());
    if(IsSleeping()){for(auto& E:KillEffects)if(IsValid(E))E->Destroy();KillEffects.Reset();}
    KillEffects.RemoveAll([](const auto& E){return !IsValid(E);});
    ShotEffects.RemoveAll([](const auto& E){return !IsValid(E);});SoundChannels.RemoveAll([](const auto& C){return !IsValid(C)||!C->IsPlaying();});
}
void AHeliosYunSummerMegatronPet::CancelAttack()
{ReloadAge=-1;FireAge=100;bFormPending=false;for(auto& E:ShotEffects)if(IsValid(E))E->Destroy();ShotEffects.Reset();for(auto& C:SoundChannels)if(IsValid(C))C->Stop();SoundChannels.Reset();Super::CancelAttack();}
float AHeliosYunSummerMegatronPet::TakeDamage(float Amount,const FDamageEvent& Event,AController* C,AActor* Source)
{if(HeliosYunSummer::IsFriendlyExplosion(Event,C,Source))return 0;const float Applied=Super::TakeDamage(Amount,Event,C,Source);if(Applied>0){if(IsSleeping())Voice->Stop();Voice->Play(IsSleeping()?FName("Death"):FName("Hurt"));}return Applied;}
void AHeliosYunSummerMegatronPet::EndPlay(const EEndPlayReason::Type Reason)
{CancelAttack();for(auto& E:KillEffects)if(IsValid(E))E->Destroy();KillEffects.Reset();if(IsValid(IdleEffect))IdleEffect->Destroy();Voice->Stop();Super::EndPlay(Reason);}
