#include "Pets/HeliosUltimatumPet.h"
#include "Combat/CombatDamageSettings.h"
#include "Combat/HeliosCombatTempoSettings.h"
#include "Pets/HeliosRifleAnimInstance.h"
#include "Pets/HeliosRifleVFXComponent.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Player/HeliosArenaCharacter.h"
#include "Enemies/HeliosEnemyActor.h"
#include "AIController.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"

AHeliosUltimatumPet::AHeliosUltimatumPet()
{
    Kind=EHeliosPetKind::UltimatumGR;GetCapsuleComponent()->InitCapsuleSize(34,102);
    GetMesh()->SetRelativeLocation(FVector(0,0,-102));GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Settings.ShieldReduction=0;Settings.MovementMultiplier=1;Settings.MeleeRange=0;
    Rifle=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Thunder rifle"));Rifle->SetupAttachment(GetRootComponent());
    Magazine=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Thunder magazine"));Magazine->SetupAttachment(GetRootComponent());
    for(auto* M:{Rifle.Get(),Magazine.Get()}){M->SetCollisionEnabled(ECollisionEnabled::NoCollision);M->SetCanEverAffectNavigation(false);}
    RifleFX=CreateDefaultSubobject<UHeliosRifleVFXComponent>(TEXT("Rifle effects"));RifleFX->SetupAttachment(GetRootComponent());
}
void AHeliosUltimatumPet::LoadModel()
{
    const auto Move = [this](const TCHAR* Key, float Fallback)
    {
        return UHeliosCombatTempoSettings::Resolve(this, EHeliosTempoFile::Movement,
            TEXT("蝴蝶雷神"), Key, Fallback);
    };
    WalkSpeed = Move(TEXT("步行"), WalkSpeed);
    RunSpeed = Move(TEXT("奔跑"), RunSpeed);
    CatchUpSpeed = Move(TEXT("追赶"), CatchUpSpeed);
    ShortStepSpeed = Move(TEXT("短步"), ShortStepSpeed);
    CombatStepSpeed = Move(TEXT("战斗侧移"), CombatStepSpeed);
    CombatPursuitSpeed = Move(TEXT("战斗追击"), CombatPursuitSpeed);
    RifleSettings.ShotInterval = UHeliosCombatTempoSettings::Resolve(this,
        EHeliosTempoFile::Attack, TEXT("宠物与召唤物"), TEXT("蝴蝶雷神每发间隔"),
        RifleSettings.ShotInterval);
    RifleSettings.BurstPause = UHeliosCombatTempoSettings::Resolve(this,
        EHeliosTempoFile::Attack, TEXT("宠物与召唤物"), TEXT("蝴蝶雷神连发停顿"),
        RifleSettings.BurstPause);
    RifleSettings.ReloadSeconds = UHeliosCombatTempoSettings::Resolve(this,
        EHeliosTempoFile::Recovery, TEXT("宠物恢复秒"), TEXT("蝴蝶雷神装填"),
        RifleSettings.ReloadSeconds);
    RifleSettings.Damage=UCombatDamageSettings::Resolve(this,ECombatDamageType::Ultimatum);
    GetMesh()->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Helios/Pets/UltimatumGR/SK_UltimatumGR.SK_UltimatumGR")));
    bModelReady=GetMesh()->GetSkeletalMeshAsset()!=nullptr;
    for(const FName Name:{FName("Idle"),FName("Walk"),FName("BackWalk"),FName("StrafeLeft"),FName("StrafeRight"),FName("Run"),FName("Sprint"),FName("TurnLeft"),FName("TurnRight"),FName("Guard"),FName("Raise"),FName("Reload"),FName("Hit"),FName("Down"),FName("Sleep"),FName("Wake")})
    {
        const FString P=FString::Printf(TEXT("/Game/Helios/Pets/UltimatumGR/A_Ultimatum_%s.A_Ultimatum_%s"),*Name.ToString(),*Name.ToString());
        if(auto* A=LoadObject<UAnimSequence>(nullptr,*P))Poses.Add(Name,A);else bModelReady=false;
    }
    Rifle->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Helios/Pets/UltimatumGR/SM_ThunderRifle.SM_ThunderRifle")));
    Magazine->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Helios/Pets/UltimatumGR/SM_ThunderMagazine.SM_ThunderMagazine")));
    bModelReady=bModelReady&&Rifle->GetStaticMesh()&&Magazine->GetStaticMesh()&&GetMesh()->DoesSocketExist(TEXT("rifle_muzzle"))&&GetMesh()->DoesSocketExist(TEXT("M-bone-Spine1"));
    GetMesh()->SetAnimInstanceClass(UHeliosRifleAnimInstance::StaticClass());Ammo=RifleSettings.MagazineSize;
    UE_LOG(LogTemp,Display,TEXT("ULTIMATUM_RIFLE_READY %d"),bModelReady);
}
void AHeliosUltimatumPet::SetPose(FName Name,bool Loop)
{
    const FVector Local=GetActorTransform().InverseTransformVectorNoScale(GetVelocity());
    const float Speed=GetVelocity().Size2D();
    bool Moving=Name==TEXT("Move");
    UAnimSequence* SidePose=nullptr;float SideWeight=0,Rate=1;
    if(Moving)
    {
        if(Local.X < -20)MovingPose=TEXT("BackWalk");
        else if(FMath::Abs(Local.Y)>FMath::Abs(Local.X)*2)MovingPose=Local.Y<0?TEXT("StrafeLeft"):TEXT("StrafeRight");
        else if(Speed>=750)MovingPose=TEXT("Sprint");
        else if(Speed>=420)MovingPose=TEXT("Run");
        else MovingPose=TEXT("Walk");
        Name=MovingPose;
        const bool Forward=Name==TEXT("Walk")||Name==TEXT("BackWalk");
        const float Reference=Name==TEXT("Sprint")?750.f:Name==TEXT("Run")?420.f:Name==TEXT("Walk")?160.f:130.f;
        Rate=Speed/Reference;
        if(Local.X>0&&FMath::Abs(Local.Y)<FMath::Abs(Local.X)&&Speed>160&&Speed<750)
        {
            // Blend stride distance and a common phase, rather than suddenly
            // slowing the feet when the long-stride run becomes active.
            const bool Fast=Speed>=420;
            Name=Fast?TEXT("Run"):TEXT("Walk");
            SidePose=Poses.FindRef(Fast?TEXT("Sprint"):TEXT("Run"));
            SideWeight=FMath::SmoothStep(Fast?420.f:160.f,Fast?750.f:420.f,Speed);
            const float Length=Poses.FindRef(Name)?Poses.FindRef(Name)->GetPlayLength():1.f;
            const float OtherLength=SidePose?SidePose->GetPlayLength():1.f;
            const float CycleDistance=FMath::Lerp((Fast?420.f:160.f)*Length,(Fast?750.f:420.f)*OtherLength,SideWeight);
            Rate=Speed*Length/FMath::Max(1.f,CycleDistance);
        }
        else if(Forward&&FMath::Abs(Local.Y)>5)
        {
            SidePose=Poses.FindRef(Local.Y<0?TEXT("StrafeLeft"):TEXT("StrafeRight"));
            const float Length=Poses.FindRef(Name)?Poses.FindRef(Name)->GetPlayLength():1.f;
            const float SideLength=SidePose?SidePose->GetPlayLength():.6f;
            const float ForwardRate=FMath::Abs(Local.X)/(Reference*Length),SideRate=FMath::Abs(Local.Y)/(130.f*SideLength);
            SideWeight=SideRate/FMath::Max(.01f,ForwardRate+SideRate);Rate=(ForwardRate+SideRate)*Length;
        }
    }
    else if((Name==TEXT("Idle")||Name==TEXT("Guard"))&&FMath::Abs(TurnRate)>25&&Speed<20)
    {Name=TurnRate<0?TEXT("TurnLeft"):TEXT("TurnRight");Moving=true;Rate=FMath::Clamp(FMath::Abs(TurnRate)/90.f,.05f,2.f);}
    CurrentPose=Name;
    if(!Target.IsValid())GetCharacterMovement()->bOrientRotationToMovement=true;
    if(auto* A=Cast<UHeliosRifleAnimInstance>(GetMesh()->GetAnimInstance()))
    {
        A->BodyPose=Poses.FindRef(Name);A->bLoopPose=Loop;
        const bool Turning=Name==TEXT("TurnLeft")||Name==TEXT("TurnRight");
        A->PoseRate=Moving?FMath::Clamp(Rate,.05f,Turning?2.f:1.65f):1;
        A->bLocomotion=Moving;A->SidePose=SidePose;A->SideWeight=SideWeight;
        A->MoveSpeed=Speed;A->MoveDirection=FMath::RadiansToDegrees(FMath::Atan2(Local.Y,Local.X));
        A->ExplicitTime=Name==TEXT("Reload")?FMath::Max(0.f,ReloadAge)*RecoveryDuration(TEXT("Reload"),1.9f)/RifleSettings.ReloadSeconds:-1;
        A->bAim=Name==TEXT("Guard");A->AimPitch=Pitch;A->Recoil=Recoil;
    }
}
bool AHeliosUltimatumPet::HasClearLine(AActor* Actor) const
{
    if(!IsValid(Actor))return false;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(RifleSight),true,this);Q.AddIgnoredActor(CompanionOwner.Get());Q.AddIgnoredActor(Actor);
    if(auto* Roster=GetWorld()->GetSubsystem<UHeliosPetWorld>())Roster->IgnoreAll(Q);
    FHitResult H;const FVector Start=GetMesh()->GetSocketLocation(TEXT("rifle_muzzle"));
    return !GetWorld()->LineTraceSingleByChannel(H,Start,AimPoint(Actor),ECC_Visibility,Q);
}
void AHeliosUltimatumPet::UpdateLocomotion(float Dt)
{
    if(Dt<=0)return;
    TurnRate=FMath::FindDeltaAngleDegrees(PreviousFacing,GetActorRotation().Yaw)/Dt;
    PreviousFacing=GetActorRotation().Yaw;
    auto* Movement=GetCharacterMovement();
    Movement->MaxAcceleration=700.f;Movement->BrakingDecelerationWalking=850.f;
    Movement->RotationRate=FRotator(0,180,0);
    if(Target.IsValid())
    {
        const FVector Local=GetActorTransform().InverseTransformVectorNoScale(GetVelocity());
        const bool ShortStep=StepRemaining>0||Local.X<-20||FMath::Abs(Local.Y)>FMath::Abs(Local.X);
        Movement->MaxWalkSpeed=ShortStep?ShortStepSpeed:RunSpeed;
    }
    else
    {
        const float Remaining=FMath::Max(0.f,FVector::Dist2D(LastGoal,GetActorLocation())-450.f);
        const float OwnerSpeed=CompanionOwner.IsValid()?CompanionOwner->GetVelocity().Size2D():0;
        const float Cruise=State==EHeliosPetState::CatchingUp?CatchUpSpeed:FMath::Clamp(OwnerSpeed+80.f,WalkSpeed,CatchUpSpeed);
        // Walk the final six metres. Distance braking prevents racing into a hard stop.
        const float Approach=FMath::Lerp(WalkSpeed,Cruise,FMath::Clamp((Remaining-150.f)/600.f,0.f,1.f));
        Movement->MaxWalkSpeed=FMath::Min(Approach,FMath::Sqrt(2.f*850.f*FMath::Max(5.f,Remaining)));
        if(GetVelocity().Size2D()>20)
        {
            const float Facing=FVector::DotProduct(GetVelocity().GetSafeNormal2D(),GetActorForwardVector());
            Movement->MaxWalkSpeed=FMath::Min(Movement->MaxWalkSpeed,FMath::Lerp(ShortStepSpeed,CatchUpSpeed,FMath::SmoothStep(.3f,.95f,Facing)));
        }
    }
}
FVector AHeliosUltimatumPet::AimPoint(AActor* Actor) const
{
    FVector Point=Actor->GetActorLocation()+FVector(0,0,15);
    // At contact range aim at the reachable part of the torso, not below the barrel.
    if(FVector::DistSquared2D(GetActorLocation(),Point)<FMath::Square(350.f))
        if(const auto* Character=Cast<ACharacter>(Actor))
        {
            const float Half=Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()*.5f;
            Point.Z=FMath::Clamp(GetMesh()->GetSocketLocation(TEXT("rifle_muzzle")).Z,Actor->GetActorLocation().Z-Half,Actor->GetActorLocation().Z+Half);
        }
    return Point;
}
float AHeliosUltimatumPet::AimYaw() const
{
    if(!Target.IsValid())return GetActorRotation().Yaw;
    const FVector To=AimPoint(Target.Get())-GetActorLocation();
    const float Side=GetActorTransform().InverseTransformPosition(GetMesh()->GetSocketLocation(TEXT("rifle_muzzle"))).Y;
    return To.Rotation().Yaw-FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Side/FMath::Max(1.f,To.Size2D()),-.8f,.8f)));
}
double AHeliosUltimatumPet::CombatTargetScore(AHeliosEnemyActor* Enemy,bool Recent,bool Threat) const
{
    const bool Retaliate=Attacker.Get()==Enemy&&GetWorld()->GetTimeSeconds()<RetaliateUntil;
    const double Rank=Recent?0.:Retaliate?-.5e9:Threat?1.e9:2.e9;
    const bool Visible=HasClearLine(Enemy);
    return Rank+(Visible?0.:5.e7)+FVector::DistSquared2D(GetActorLocation(),Enemy->GetActorLocation())
        -(Target.Get()==Enemy&&Visible?1.e6:0.);
}
float AHeliosUltimatumPet::TakeDamage(float Amount,const FDamageEvent& Event,AController* InstigatorController,AActor* Causer)
{
    const float Applied=Super::TakeDamage(Amount,Event,InstigatorController,Causer);
    if(Applied<=0||IsSleeping()||State==EHeliosPetState::Waking)return Applied;
    auto* Enemy=Cast<AHeliosEnemyActor>(InstigatorController?InstigatorController->GetPawn():nullptr);
    if(!Enemy)Enemy=Cast<AHeliosEnemyActor>(Causer);
    if(!Enemy&&Causer)Enemy=Cast<AHeliosEnemyActor>(Causer->GetInstigator());
    if(Enemy&&!Enemy->IsEnemyDead()&&CompanionOwner.IsValid()
        &&FVector::Dist2D(CompanionOwner->GetActorLocation(),Enemy->GetActorLocation())<=Settings.CombatRadius
        &&FVector::Dist2D(CompanionOwner->GetActorLocation(),GetActorLocation())<=Settings.CombatRadius)
    {
        Attacker=Enemy;RetaliateUntil=GetWorld()->GetTimeSeconds()+4.;DecisionRemaining=0;
        if(Target.Get()!=Enemy){BurstRemaining=PendingShots=0;AttackAge=-1;AimAge=0;}
        Target=Enemy;State=EHeliosPetState::Fighting;
        // Keep reload progress and cadence; repeated hits must not restart either.
        if(StepRemaining<=0)PlantRemaining=FMath::Max(PlantRemaining,1.25f);
    }
    return Applied;
}
bool AHeliosUltimatumPet::FindCombatStep(bool Retreat,FVector& Out) const
{
    if(!Target.IsValid())return false;
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());if(!Nav)return false;
    const FVector Away=(GetActorLocation()-Target->GetActorLocation()).GetSafeNormal2D();
    FCollisionQueryParams Q(SCENE_QUERY_STAT(RiflePosition),false,this);Q.AddIgnoredActor(CompanionOwner.Get());Q.AddIgnoredActor(Target.Get());
    for(float Distance:{Retreat?280.f:400.f,Retreat?150.f:750.f})
    for(float Angle:{0.f,55.f,-55.f,90.f,-90.f,135.f,-135.f})
    {
        FNavLocation Point;
        if(!Nav->ProjectPointToNavigation(GetActorLocation()+Away.RotateAngleAxis(Angle,FVector::UpVector)*Distance,Point,FVector(90,90,180)))continue;
        const FVector Center=Point.Location+FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        if(FVector::Dist2D(Center,GetActorLocation())<80||FMath::Abs(Center.Z-GetActorLocation().Z)>100)continue;
        if(CompanionOwner.IsValid()&&FVector::Dist2D(Center,CompanionOwner->GetActorLocation())>Settings.CombatRadius-100)continue;
        if(Retreat&&FVector::Dist2D(Center,Target->GetActorLocation())<FVector::Dist2D(GetActorLocation(),Target->GetActorLocation())+60)continue;
        FHitResult H;
        if(GetWorld()->LineTraceSingleByChannel(H,Center+FVector(0,0,45),AimPoint(Target.Get()),ECC_Visibility,Q))continue;
        if(GetWorld()->OverlapBlockingTestByChannel(Center,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,100),Q))continue;
        auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),GetActorLocation(),Point.Location,const_cast<AHeliosUltimatumPet*>(this));
        if(!Path||!Path->IsValid()||Path->IsPartial()||Path->GetPathLength()>Distance*2.5f)continue;
        Out=Point.Location;return true;
    }
    return false;
}
void AHeliosUltimatumPet::TrackCombatTarget(float Dt)
{
    if(!Target.IsValid())return;
    const float Distance=FVector::Dist2D(Target->GetActorLocation(),GetActorLocation());
    auto Stop=[&](){if(auto* AI=Cast<AAIController>(GetController()))AI->StopMovement();GetCharacterMovement()->StopMovementImmediately();bMoveRequested=false;};
    if(FootworkTarget!=Target){FootworkTarget=Target;StepRemaining=0;PlantRemaining=1.25f;RepathRemaining=0;}
    PlantRemaining=FMath::Max(0.f,PlantRemaining-Dt);RepathRemaining=FMath::Max(0.f,RepathRemaining-Dt);
    const bool Visible=HasClearLine(Target.Get());
    if(GetCharacterMovement()->IsFalling())
    {
        if(auto* AI=Cast<AAIController>(GetController()))AI->StopMovement();bMoveRequested=false;
        StepRemaining=0;PlantRemaining=1.25f;
    }
    else if(IsReloading()||(BurstRemaining>0&&Visible&&Distance<=RifleSettings.Range)){StepRemaining=0;Stop();}
    else if(StepRemaining>0)
    {
        StepRemaining=FMath::Max(0.f,StepRemaining-Dt);
        if(StepRemaining<=0||FVector::Dist2D(StepGoal,GetActorLocation())<65){StepRemaining=0;PlantRemaining=1.25f;Stop();}
        else{GetCharacterMovement()->MaxWalkSpeed=CombatStepSpeed;MoveTowards(StepGoal,40,Dt);}
    }
    else if(!Visible)
    {
        if(RepathRemaining<=0){RepathRemaining=.6f;if(FindCombatStep(false,StepGoal))StepRemaining=1.f;}
        GetCharacterMovement()->MaxWalkSpeed=CombatPursuitSpeed;
        // No large acceptance radius behind cover: keep rounding the obstacle.
        MoveTowards(StepRemaining>0?StepGoal:Target->GetActorLocation(),50,Dt);
    }
    else if(Distance>2200){GetCharacterMovement()->MaxWalkSpeed=CombatPursuitSpeed;MoveTowards(Target->GetActorLocation(),1800,Dt);}
    else if(Distance<650&&PlantRemaining<=0&&RepathRemaining<=0)
    {
        RepathRemaining=.6f;
        if(FindCombatStep(true,StepGoal)){StepRemaining=.65f;GetCharacterMovement()->MaxWalkSpeed=CombatStepSpeed;MoveTowards(StepGoal,40,Dt);}
        else{PlantRemaining=1.25f;Stop();}
    }
    else Stop();
    GetCharacterMovement()->bOrientRotationToMovement=false;
    // On long repositioning runs face the route; use directional footwork near the target.
    const bool RouteRun=GetVelocity().Size2D()>180&&StepRemaining<=0&&!IsReloading();
    const float Facing=RouteRun?GetVelocity().Rotation().Yaw:AimYaw();
    SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(),FRotator(0,Facing,0),Dt,180));
}
bool AHeliosUltimatumPet::CanEngageTarget(AActor* Actor) const
{ return Actor&&((FVector::Dist(GetActorLocation(),Actor->GetActorLocation())<=RifleSettings.Range&&HasClearLine(Actor))||Super::CanEngageTarget(Actor)); }
bool AHeliosUltimatumPet::ReadyToShoot() const
{
    if(!Target.IsValid()||Target->IsEnemyDead()||!CompanionOwner.IsValid()||IsSleeping()||State==EHeliosPetState::Waking||IsReloading()||GetCharacterMovement()->IsFalling()||GetVelocity().Size2D()>20)return false;
    const FVector To=Target->GetActorLocation()-GetActorLocation();
    return To.Size()<=RifleSettings.Range&&FVector::Dist2D(CompanionOwner->GetActorLocation(),Target->GetActorLocation())<=Settings.CombatRadius
        &&FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,AimYaw()))<6.f&&HasClearLine(Target.Get());
}
void AHeliosUltimatumPet::UpdateAttack(float Dt)
{
    if(!bModelReady||IsSleeping()||State==EHeliosPetState::Waking)return;
    Recoil=FMath::FInterpTo(Recoil,0.f,Dt,24);
    if(IsReloading())
    {
        ReloadAge+=Dt;SetPose(TEXT("Reload"),false);
        if(ReloadAge>=RifleSettings.ReloadSeconds){Ammo=RifleSettings.MagazineSize;ReloadAge=-1;++ReloadsCompleted;AimAge=0;ShotWait=0;SetPose(TEXT("Raise"),false);}
        return;
    }
    if(Ammo<=0)
    {
        BurstRemaining=PendingShots=0;AttackAge=-1;ReloadAge=0;RifleFX->Reload();SetPose(TEXT("Reload"),false);return;
    }
    if(!ReadyToShoot())
    {
        BurstRemaining=PendingShots=0;AttackAge=-1;AimAge=0;ShotWait=FMath::Max(0.f,ShotWait-Dt);
        SetPose(HitPoseRemaining>0?TEXT("Hit"):GetVelocity().Size2D()>20?TEXT("Move"):Target.IsValid()?TEXT("Guard"):TEXT("Idle"));return;
    }
    const FVector Muzzle=GetMesh()->GetSocketLocation(TEXT("rifle_muzzle"));
    const float Desired=(AimPoint(Target.Get())-Muzzle).Rotation().Pitch;
    Pitch=FMath::FInterpConstantTo(Pitch,FMath::Clamp(Desired,-45.f,45.f),Dt,150);
    AimAge+=Dt;
    if(AimAge<.30f){SetPose(TEXT("Raise"),false);return;}
    SetPose(TEXT("Guard"));if(FMath::Abs(Desired-Pitch)>3)return;
    ShotWait-=Dt;
    // Preserve the residual time so 15/30/60/120 Hz produce the same cadence.
    while(ShotWait<=0&&PendingShots<Ammo)
    {
        if(BurstRemaining<=0)BurstRemaining=RifleSettings.BurstSize;
        ++PendingShots;--BurstRemaining;AttackAge=0;
        ShotWait+=BurstRemaining>0?FMath::Max(.01f,RifleSettings.ShotInterval):FMath::Max(.01f,RifleSettings.BurstPause);
        if(BurstRemaining==0)AttackAge=-1;
    }
}
FTransform AHeliosUltimatumPet::AnimatedGunTransform() const
{
    const FVector P=GetMesh()->GetSocketLocation(TEXT("rifle_grip"));
    const FVector F=GetMesh()->GetSocketLocation(TEXT("rifle_forward"))-P;
    const FVector U=GetMesh()->GetSocketLocation(TEXT("rifle_up"))-P;
    return FTransform(FRotationMatrix::MakeFromXZ(F,U).ToQuat(),P);
}
void AHeliosUltimatumPet::FireRound()
{
    if(Ammo<=0||!ReadyToShoot())return;
    const FVector Start=GetMesh()->GetSocketLocation(TEXT("rifle_muzzle"));
    const FVector Forward=Rifle->GetForwardVector();
    const FVector Aim=(AimPoint(Target.Get())-Start).GetSafeNormal();
    FCollisionQueryParams Q(SCENE_QUERY_STAT(CompanionRound),true,this);Q.AddIgnoredActor(CompanionOwner.Get());
    if(auto* Roster=GetWorld()->GetSubsystem<UHeliosPetWorld>())Roster->IgnoreAll(Q);
    // If the barrel touches a charging enemy, that first contact receives the round.
    // The same test keeps a muzzle inside a wall from firing through that wall.
    FHitResult Near;const bool BarrelBlocked=GetWorld()->LineTraceSingleByChannel(Near,GetActorLocation()+FVector(0,0,40),Start,ECC_Visibility,Q);
    const auto* TouchingEnemy=BarrelBlocked?Cast<AHeliosEnemyActor>(Near.GetActor()):nullptr;
    if(FVector::DotProduct(Aim,Forward)<.995f&&(!TouchingEnemy||TouchingEnemy->IsEnemyDead()))return;
    const FVector Dir=SpreadRandom.VRandCone(Forward,FMath::DegreesToRadians(RifleSettings.SpreadDegrees));
    FHitResult Hit;const FVector End=Start+Dir*RifleSettings.Range;
    const bool Blocked=BarrelBlocked||GetWorld()->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility,Q);
    if(BarrelBlocked)Hit=Near;
    --Ammo;++ShotsFired;Recoil=FMath::Min(1.8f,Recoil+.7f);
    if(Blocked)if(auto* E=Cast<AHeliosEnemyActor>(Hit.GetActor()))if(!E->IsEnemyDead())
        if(UGameplayStatics::ApplyDamage(E,RifleSettings.Damage,GetController(),this,UDamageType::StaticClass())>0)++HitsDelivered;
    RifleFX->Shot(Start,Blocked?Hit.ImpactPoint:End,Blocked?Hit.ImpactNormal:-Dir,Blocked);
}
void AHeliosUltimatumPet::PlaceDroppedGun()
{
    FCollisionQueryParams Q(SCENE_QUERY_STAT(DroppedRifle),false,this);Q.AddIgnoredActor(CompanionOwner.Get());
    const FVector Origin=GetActorLocation();FHitResult H;bool Found=false;
    for(float Side:{46.f,-46.f,60.f,-60.f,30.f})
    {
        FVector P=GetActorTransform().TransformPosition(FVector(20,Side,0));
        if(!GetWorld()->LineTraceSingleByChannel(H,P+FVector(0,0,60),P-FVector(0,0,250),ECC_WorldStatic,Q))continue;
        const FVector Up=H.ImpactNormal;const FVector Forward=FVector::VectorPlaneProject(GetActorForwardVector(),Up).GetSafeNormal();
        const FQuat Rotation=FRotationMatrix::MakeFromXZ(Forward,Up).ToQuat()*FQuat(FVector::ForwardVector,PI/2);
        const FVector Grip=H.ImpactPoint+Up*8.f;
        if(GetWorld()->OverlapBlockingTestByChannel(Grip+Forward*15,Rotation,ECC_WorldStatic,FCollisionShape::MakeBox(FVector(46,6,11)),Q))continue;
        DropEnd=FTransform(Rotation,Grip);Found=true;break;
    }
    if(!Found){const FVector P=Origin-FVector(0,0,96);DropEnd=FTransform(GetActorQuat()*FQuat(FVector::ForwardVector,PI/2),P);}
    DropOwnerLocation=Origin;
}
void AHeliosUltimatumPet::PostPose(float Dt)
{
    if(!bModelReady)return;
    const FTransform Animated=AnimatedGunTransform();FTransform Gun=Animated;
    if(IsSleeping()||State==EHeliosPetState::Waking)
    {
        if(!bGunDropped){DropStart=Rifle->GetComponentTransform();PlaceDroppedGun();bGunDropped=true;bPickingUp=false;}
        if(FVector::DistSquared(DropOwnerLocation,GetActorLocation())>40000)PlaceDroppedGun();
        const float Fall=1-FMath::Clamp(DownRemaining/RecoveryDuration(TEXT("Down"),1),0.f,1.f);
        if(IsSleeping())
        {
            const float T=FMath::SmoothStep(.18f,.78f,Fall);Gun.Blend(DropStart,DropEnd,T);
            if(Fall>=.78f)Gun=DropEnd;
        }
        else
        {
            const float T=1-FMath::Clamp(WakeRemaining/RecoveryDuration(TEXT("Wake"),3),0.f,1.f);
            Gun=DropEnd;
            if(T>=.70f)
            {
                if(!bPickingUp){PickupOffset=DropEnd.GetRelativeTransform(Animated);bPickingUp=true;}
                FTransform Offset;Offset.Blend(PickupOffset,FTransform::Identity,FMath::SmoothStep(.70f,1.f,T));Gun=Offset*Animated;
            }
        }
    }
    Rifle->SetWorldTransform(Gun);
    FTransform Mag=Gun;
    Mag.SetLocation(IsReloading()?GetMesh()->GetSocketLocation(TEXT("rifle_mag")):Gun.TransformPosition(FVector(10.5,0,-1.5)));
    Magazine->SetWorldTransform(Mag);
    if(!IsSleeping()&&State!=EHeliosPetState::Waking)for(int I=0;I<PendingShots;++I)FireRound();
    PendingShots=0;
}
void AHeliosUltimatumPet::CancelAttack()
{
    Super::CancelAttack();BurstRemaining=PendingShots=0;ReloadAge=-1;AimAge=0;ShotWait=0;Recoil=0;
    StepRemaining=0;PlantRemaining=1.25f;RepathRemaining=0;FootworkTarget.Reset();Attacker.Reset();RetaliateUntil=-1;
    if(RifleFX)RifleFX->Clear();
}
float AHeliosUltimatumPet::RecoveryDuration(FName Pose,float Fallback) const
{
    const UAnimSequence* Animation=Poses.FindRef(Pose);return Animation?Animation->GetPlayLength():Fallback;
}
void AHeliosUltimatumPet::UpdateRecoveryGround(float Dt)
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(UltimatumFloor),false,this);
    Query.AddIgnoredActor(CompanionOwner.Get());
    FHitResult Ground;
    if(!GetWorld()->LineTraceSingleByChannel(Ground,GetActorLocation()+FVector(0,0,30),GetActorLocation()-FVector(0,0,220),ECC_WorldStatic,Query))return;
    const float DownLength=RecoveryDuration(TEXT("Down"),1.f),WakeLength=RecoveryDuration(TEXT("Wake"),2.2f);
    const float Blend=State==EHeliosPetState::Waking?FMath::Clamp(WakeRemaining/(WakeLength*.3f),0.f,1.f):1.f-FMath::Clamp(DownRemaining/DownLength,0.f,1.f);
    const FVector Normal=GetActorTransform().InverseTransformVectorNoScale(Ground.ImpactNormal).GetSafeNormal();
    const FQuat Tilt=FQuat::FindBetweenNormals(FVector::UpVector,Normal);
    GetMesh()->SetRelativeRotation(FQuat::Slerp(FQuat::Identity,Tilt,Blend)*FRotator(0,-90,0).Quaternion());
    const float FloorOffset=Ground.ImpactPoint.Z-(GetActorLocation().Z-GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    GetMesh()->SetRelativeLocation(FVector(0,0,-GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()+FloorOffset*Blend));
}
void AHeliosUltimatumPet::RecoveryFinished()
{
    GetMesh()->SetRelativeRotation(FRotator(0,-90,0));
    GetMesh()->SetRelativeLocation(FVector(0,0,-GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()));
    bGunDropped=false;bPickingUp=false;SetPose(TEXT("Guard"));
}
