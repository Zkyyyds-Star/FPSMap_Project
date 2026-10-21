#include "Pets/HeliosHumanoidPet.h"
#include "Player/HeliosGuanXiaoYuAbilityComponent.h"
#include "Weapons/HeliosBlackDragonSoul.h"
#include "Player/HeliosCharacterDefinition.h"
#include "Player/HeliosCharacterActionProfile.h"
#include "Animation/AnimSequence.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Player/HeliosArenaCharacter.h"
#include "Weapons/HeliosFirstPersonPresentationComponent.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Weapons/HeliosCombatWeapons.h"
#include "Weapons/HeliosSunShangXiang.h"
#include "Weapons/HeliosChanZeus.h"
#include "Weapons/HeliosBuLianShiP90.h"
#include "Weapons/HeliosWangZheKuiThunder.h"
#include "Weapons/HeliosYunSummerMegatron.h"
#include "Combat/HeliosCombatTempoSettings.h"
#include "Enemies/HeliosEnemyActor.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Kismet/GameplayStatics.h"
#include "ShooterWeapon.h"

AHeliosHumanoidPet::AHeliosHumanoidPet()
{
    GuanXiaoYuAbility=CreateDefaultSubobject<UHeliosGuanXiaoYuAbilityComponent>(TEXT("GuanXiaoYuAbility"));
    Kind=EHeliosPetKind::ChanZeus;Settings.ShieldReduction=0;Settings.MovementMultiplier=1;
    GetCapsuleComponent()->InitCapsuleSize(34,102);GetMesh()->SetBoundsScale(2);
    GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    WeaponView=CreateDefaultSubobject<USceneComponent>(TEXT("Companion weapon source view"));WeaponView->SetupAttachment(GetRootComponent());WeaponView->SetRelativeLocation(FVector(0,0,35));
    CharacterPresentation=CreateDefaultSubobject<UHeliosCharacterPresentationComponent>(TEXT("Selected companion character"));
    Presentation=CreateDefaultSubobject<UHeliosFirstPersonPresentationComponent>(TEXT("Companion original weapon trajectory"));
}
FString AHeliosHumanoidPet::DisplayName()const
{
    const auto* C=FHeliosCharacterRegistry::FindCharacter(Loadout.CharacterId);
    return C?C->DisplayName.ToString():TEXT("人形伙伴");
}
void AHeliosHumanoidPet::ConfigureCharacterSettings()
{
    // Block strength belongs to the selected blade, independently of the skin.
    const bool SSXBlade=Loadout.PrimaryWeaponId==TEXT("cfm_sunshangxiang"),ZXKBlade=Loadout.PrimaryWeaponId==TEXT("cfm_zhongxiaokui");
    Settings.ShieldReduction=(SSXBlade||ZXKBlade)?UHeliosCombatTempoSettings::Hunter(this,ZXKBlade,true,TEXT("格挡减伤比例"),ZXKBlade?.6f:0.f,0,.95f):0.f;
    if(Kind==EHeliosPetKind::SunShangXiang||Kind==EHeliosPetKind::ZhongXiaoKui)
    {
        GetCapsuleComponent()->SetCapsuleSize(34,102);
        const bool ZXK=Kind==EHeliosPetKind::ZhongXiaoKui;
        auto H=[this,ZXK](const TCHAR* K,float D,float Min,float Max){return UHeliosCombatTempoSettings::Hunter(this,ZXK,true,K,D,Min,Max);};
        Settings.MaxHealth=H(TEXT("生命"),600,1,1000000);Settings.SleepSeconds=H(TEXT("倒地恢复时间"),30,.1,600);Settings.MovementMultiplier=H(TEXT("移动倍率"),1,.1,5);
        Settings.FollowDistance=H(TEXT("跟随距离"),2500,100,10000);Settings.CatchUpDistance=FMath::Max(Settings.FollowDistance,H(TEXT("追赶距离"),5000,100,20000));Settings.CombatRadius=H(TEXT("寻敌半径"),5000,100,20000);
        Settings.FollowMinSpeed=H(TEXT("跟随最低速度"),520,10,5000);Settings.FollowMaxSpeed=H(TEXT("跟随最高速度"),900,Settings.FollowMinSpeed,5000);Settings.ChaseSpeed=H(TEXT("战斗追敌速度"),650,10,5000);Settings.CatchUpSpeed=H(TEXT("远距追赶速度"),1000,10,5000);return;
    }
    auto S=[this](const TCHAR* K,float D,float Min,float Max)
    {
        if(Kind==EHeliosPetKind::BuLianShiP90)return HeliosBuLianShiP90::Setting(true,K,D,Min,Max);
        if(Kind==EHeliosPetKind::WangZheKuiThunder)return HeliosWangZheKuiThunder::Setting(true,K,D,Min,Max);
        if(Kind==EHeliosPetKind::YunSummerMegatron)return HeliosYunSummer::Setting(true,K,D,Min,Max);
        return HeliosChanZeus::Setting(true,K,D,Min,Max);
    };
    Settings.MovementMultiplier=Kind==EHeliosPetKind::YunSummerMegatron&&Loadout.CharacterVariantId==TEXT("SP")?S(TEXT("灵魂武者移速倍率"),1.2f,1,3):1;
    GetCapsuleComponent()->SetCapsuleSize(34,Kind==EHeliosPetKind::YunSummerMegatron?96:102);
    Settings.MaxHealth=S(TEXT("生命"),300,1,1000000);Settings.SleepSeconds=S(TEXT("倒地恢复时间"),30,.1,600);
    Settings.FollowDistance=S(TEXT("跟随距离"),1400,100,10000);Settings.CatchUpDistance=FMath::Max(Settings.FollowDistance,S(TEXT("追上距离"),3000,100,20000));Settings.CombatRadius=S(TEXT("寻敌半径"),4500,100,20000);
    Settings.FollowMinSpeed=S(TEXT("跟随最低速度"),420,10,5000);Settings.FollowMaxSpeed=S(TEXT("跟随最高速度"),700,Settings.FollowMinSpeed,5000);Settings.ChaseSpeed=S(TEXT("战斗追敌速度"),520,10,5000);Settings.CatchUpSpeed=S(TEXT("远处追赶速度"),850,10,5000);
}
bool AHeliosHumanoidPet::ApplyLoadout(const FHeliosLoadout& RequestedLoadout)
{
    FString RequestedError;if(!FHeliosLoadout::Validate(RequestedLoadout,RequestedError))return false;
    const FHeliosLoadout InLoadout=FHeliosLoadout::ResolveLegacyVariant(RequestedLoadout,GetGameInstance(),RosterSlot);
    FString Error;const auto* C=FHeliosCharacterRegistry::FindCharacter(InLoadout.CharacterId);
    if(InLoadout.CharacterId==TEXT("sword_dog")||!HeliosPetLoadouts::ValidatePresentation(InLoadout,Error))return false;
    if(Loadout==InLoadout&&IsValid(EquippedPrimary))return true;
    // Resolve the next presentation before touching the current weapon or health.
    const auto* Form=C->FindForm(InLoadout.CharacterVariantId);
    const auto* Profile=Form?Form->ActionProfile.LoadSynchronous():C->ActionProfile.LoadSynchronous();
    const auto* Pair=Profile?Profile->FindWeapon(InLoadout.PrimaryWeaponId):nullptr;
    auto* Body=Pair&&Pair->bGeneratedAndValidated?Pair->ThirdPersonMesh.LoadSynchronous():nullptr;
    const auto* IdlePath=Pair?Pair->ThirdPersonClips.Find(TEXT("Idle")):nullptr;
    auto* Idle=IdlePath?IdlePath->LoadSynchronous():nullptr;
    const auto* NextDefinition=UHeliosWeaponRegistry::FindWeapon(InLoadout.PrimaryWeaponId);
    if(!Body||!Idle||Idle->GetSkeleton()!=Body->GetSkeleton()||!NextDefinition||!NextDefinition->WeaponClass)return false;
    GuanXiaoYuAbility->Cancel();
    const float PreviousHealth=Health,PreviousSleep=SleepRemaining;const bool WasSleeping=IsSleeping();
    if(IsValid(EquippedPrimary)&&Loadout.PrimaryWeaponId==InLoadout.PrimaryWeaponId)
    {
        if(!CharacterPresentation->ApplyLoadout(GetMesh(),InLoadout))return false;
        Loadout=InLoadout;Kind=HeliosPetLoadouts::KindForLoadout(Loadout);ConfigureCharacterSettings();
        Health=FMath::Min(PreviousHealth,Settings.MaxHealth);SleepRemaining=PreviousSleep;
        bModelReady=true;
        AlignBody();
        CharacterPresentation->AttachWeapon(EquippedPrimary);CharacterPresentation->BindWeaponSource(EquippedPrimary);
        return bModelReady;
    }
    if(!HasActorBegunPlay()){Loadout=InLoadout;Kind=HeliosPetLoadouts::KindForLoadout(Loadout);return true;}
    // Stage the new combat actor while the old actor is still usable.
    auto* NextWeapon=GetWorld()->SpawnActorDeferred<AShooterWeapon>(NextDefinition->WeaponClass,FTransform::Identity,this,this,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!NextWeapon)return false;
    if(auto* Combat=Cast<AHeliosCombatWeapon>(NextWeapon))Combat->ApplyDefinition(NextDefinition);
    else NextWeapon->ConfigureMagazine(NextDefinition->MagazineSize);
    UGameplayStatics::FinishSpawningActor(NextWeapon,FTransform::Identity);
    if(!IsValid(NextWeapon))return false;
    if(!CharacterPresentation->ApplyLoadout(GetMesh(),InLoadout)){NextWeapon->Destroy();return false;}
    CancelAttack();Presentation->Unequip();
    if(IsValid(EquippedPrimary)){EquippedPrimary->OnShotPresented.Remove(ShotHandle);EquippedPrimary->DeactivateWeapon();EquippedPrimary->Destroy();}EquippedPrimary=nullptr;
    Loadout=InLoadout;Kind=HeliosPetLoadouts::KindForLoadout(Loadout);
    EquippedPrimary=NextWeapon;PrimaryDefinition=UHeliosWeaponRegistry::FindWeapon(Loadout.PrimaryWeaponId);
    ConfigureCharacterSettings();bModelReady=true;AlignBody();ConfigurePrimary();
    Health=FMath::Min(PreviousHealth,Settings.MaxHealth);SleepRemaining=PreviousSleep;
    return true;
}
void AHeliosHumanoidPet::LoadModel()
{
    Kind=HeliosPetLoadouts::KindForLoadout(Loadout);ConfigureCharacterSettings();Health=Settings.MaxHealth;
    bModelReady=CharacterPresentation->ApplyLoadout(GetMesh(),Loadout);
    if(!bModelReady)return;
    AlignBody();
    PrimaryDefinition=UHeliosWeaponRegistry::FindWeapon(Loadout.PrimaryWeaponId);
    if(!PrimaryDefinition||!PrimaryDefinition->WeaponClass)return;
    EquippedPrimary=GetWorld()->SpawnActorDeferred<AShooterWeapon>(PrimaryDefinition->WeaponClass,FTransform::Identity,this,this,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!EquippedPrimary)return;
    if(auto* Combat=Cast<AHeliosCombatWeapon>(EquippedPrimary))Combat->ApplyDefinition(PrimaryDefinition);
    else EquippedPrimary->ConfigureMagazine(PrimaryDefinition->MagazineSize);
    UGameplayStatics::FinishSpawningActor(EquippedPrimary,FTransform::Identity);
    if(!IsValid(EquippedPrimary))return;
    ConfigurePrimary();
}
void AHeliosHumanoidPet::ConfigurePrimary()
{
    EquippedPrimary->ConfigureMagazine(EquippedPrimary->GetMagazineSize());
    // The frozen Megatron implementation remains intact. Its public tuning hook
    // allows the weapon to be reused with another ready character at pet strength.
    if(auto* Megatron=Cast<AHeliosYunSummerMegatronWeapon>(EquippedPrimary))
    {Megatron->ShotSettings=HeliosYunSummer::Tuning(true);Megatron->ConfigureMagazine(Megatron->ShotSettings.Magazine);Megatron->ConfigureRefireRate(Megatron->ShotSettings.Interval);}
    Presentation->EquipCompanion(this,WeaponView,EquippedPrimary,PrimaryDefinition);
    CharacterPresentation->AttachWeapon(EquippedPrimary);CharacterPresentation->BindWeaponSource(EquippedPrimary);
    ShotHandle=EquippedPrimary->OnShotPresented.AddUObject(this,&AHeliosHumanoidPet::ShotPresented);
    EquippedPrimary->ActivateWeapon(TEXT("HeliosPet"));Ammo=EquippedPrimary->GetBulletCount();
    bMeleePrimary=Loadout.PrimaryWeaponId==TEXT("cfm_sunshangxiang")||Loadout.PrimaryWeaponId==TEXT("cfm_zhongxiaokui")||Loadout.PrimaryWeaponId==TEXT("lightstick")||Loadout.PrimaryWeaponId==TEXT("soulbanner")||Loadout.PrimaryWeaponId==TEXT("qingxiao");
    WeaponRange=PrimaryDefinition->Range;
    if(Loadout.PrimaryWeaponId==TEXT("cfm_chan_zeus"))WeaponRange=HeliosChanZeus::Tuning(true).Range;
    else if(Loadout.PrimaryWeaponId==TEXT("cfm_bulianshi_p90"))WeaponRange=HeliosBuLianShiP90::Tuning(true).Range;
    else if(Loadout.PrimaryWeaponId==TEXT("cfm_wangzhekui_thunder"))WeaponRange=HeliosWangZheKuiThunder::Tuning(true).Range;
    else if(Loadout.PrimaryWeaponId==TEXT("cfm_yun_summer_megatron"))WeaponRange=HeliosYunSummer::Tuning(true).Range;
    if(bMeleePrimary&&Loadout.PrimaryWeaponId.ToString().StartsWith(TEXT("cfm_")))WeaponRange=UHeliosCombatTempoSettings::Hunter(this,Loadout.PrimaryWeaponId==TEXT("cfm_zhongxiaokui"),true,TEXT("攻击距离"),260,10,10000);
    HideSourceMeshes();
}
void AHeliosHumanoidPet::AlignBody()
{
    if(auto* Model=GetMesh()->GetSkeletalMeshAsset())
    {
        const float Height=Kind==EHeliosPetKind::YunSummerMegatron?HeliosYunSummer::Setting(true,TEXT("可见身高"),190,100,300):203.3593f;
        const float Scale=Height/FMath::Max(1.f,2*Model->GetBounds().BoxExtent.Z);
        GetMesh()->SetRelativeScale3D(FVector(Scale));
        GetMesh()->SetRelativeLocation(FVector(0,0,-GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()-(Model->GetBounds().Origin.Z-Model->GetBounds().BoxExtent.Z)*Scale));
    }
}
FVector AHeliosHumanoidPet::GetPawnViewLocation()const{return GetActorLocation()+FVector(0,0,35);}
void AHeliosHumanoidPet::GetActorEyesViewPoint(FVector& Location,FRotator& Rotation)const
{Location=GetPawnViewLocation();Rotation=Target.IsValid()?(Target->GetActorLocation()+FVector(0,0,25)-Location).Rotation():GetActorRotation();}
FVector AHeliosHumanoidPet::GetWeaponTargetLocation()
{return Target.IsValid()?Target->GetActorLocation()+FVector(0,0,25):GetPawnViewLocation()+GetActorForwardVector()*WeaponRange;}
void AHeliosHumanoidPet::AttachWeaponMeshes(AShooterWeapon* Weapon)
{
    Weapon->AttachToComponent(WeaponView,FAttachmentTransformRules::SnapToTargetNotIncludingScale);
    Weapon->GetFirstPersonMesh()->AttachToComponent(WeaponView,FAttachmentTransformRules::SnapToTargetNotIncludingScale);
    Weapon->GetFirstPersonMesh()->SetRelativeTransform(FTransform::Identity);
    Weapon->GetThirdPersonMesh()->AttachToComponent(GetMesh(),FAttachmentTransformRules::SnapToTargetNotIncludingScale,TEXT("b_RightWeapon"));
}
void AHeliosHumanoidPet::UpdateWeaponHUD(int32 CurrentAmmo,int32){Ammo=CurrentAmmo;}
void AHeliosHumanoidPet::HideSourceMeshes()
{
    if(!IsValid(EquippedPrimary))return;
    TInlineComponentArray<UPrimitiveComponent*> Components;EquippedPrimary->GetComponents(Components);
    for(auto* C:Components)
    {
        const bool Source=C==EquippedPrimary->GetFirstPersonMesh()||C==EquippedPrimary->GetThirdPersonMesh()||C->IsAttachedTo(EquippedPrimary->GetFirstPersonMesh())||C->IsAttachedTo(EquippedPrimary->GetThirdPersonMesh());
        if(!Source)continue;C->SetHiddenInGame(true);C->SetCastShadow(false);
        if(auto* S=Cast<USkeletalMeshComponent>(C)){S->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;S->bEnableUpdateRateOptimizations=false;}
    }
    if(auto* Arms=Presentation->GetArmsMesh())Arms->SetHiddenInGame(true);
}
void AHeliosHumanoidPet::ShotPresented(){++ShotsFired;ShotAge=0;}
void AHeliosHumanoidPet::SetPose(FName Name,bool Loop)
{
    if(CurrentPose!=Name){CurrentPose=Name;PoseAge=0;}
    const FName Action=Name==TEXT("Sleep")?FName("Down"):Name==TEXT("Guard")?FName("Idle"):Name==TEXT("Move")?MovePose:Name;
    const float Age=Name==TEXT("Sleep")?RecoveryDuration(TEXT("Down"),1.4f):Name==TEXT("Move")?MoveAge:PoseAge;
    CharacterPresentation->SetPose(Action,Age,Name==TEXT("Sleep")?false:Loop);
}
float AHeliosHumanoidPet::RecoveryDuration(FName Pose,float Fallback)const
{return CharacterPresentation?CharacterPresentation->GetRecoveryDuration(Pose,Fallback):Fallback;}
void AHeliosHumanoidPet::UpdateLocomotion(float Dt)
{
    const float Speed=GetVelocity().Size2D();if(Speed<20){MoveAge=0;return;}
    MoveAge+=Dt*FMath::Clamp(Speed/(520.f*Settings.MovementMultiplier),.05f,2.f);const FVector Local=GetActorRotation().UnrotateVector(GetVelocity());const float Angle=FMath::RadiansToDegrees(FMath::Atan2(Local.Y,Local.X));
    MovePose=FMath::Abs(Angle)>135?TEXT("Run_Bwd"):Angle>45?TEXT("Run_RT"):Angle<-45?TEXT("Run_LT"):TEXT("Run_Fwd");
}
void AHeliosHumanoidPet::TrackCombatTarget(float Dt)
{
    if(!Target.IsValid())return;
    const float Stop=GuanXiaoYuAbility->IsUsingUmbrella()?GuanXiaoYuAbility->Range*.45f:bMeleePrimary?WeaponRange*.45f:FMath::Min(1800.f,WeaponRange*.8f);
    const FVector Offset=Target->GetActorLocation()-GetActorLocation();
    const float Distance=Offset.Size2D();
    const bool Visible=HasClearLine(Target.Get());
    const bool Ready=Visible&&Distance<=Stop;
    const bool AimAtTarget=Visible&&Distance<=WeaponRange;
    // Navigation includes the capsule in its acceptance radius and can finish
    // outside the preferred stopping distance. Keep facing a visible target in
    // attack range after that path ends, even when movement has no yaw to follow.
    // Preserve the preferred stop, navigation acceptance and all attack gates.
    GetCharacterMovement()->bOrientRotationToMovement=!AimAtTarget;
    if(Ready){if(auto* AI=Cast<AAIController>(GetController()))AI->StopMovement();GetCharacterMovement()->StopMovementImmediately();bMoveRequested=false;}
    else MoveTowards(Target->GetActorLocation(),bMeleePrimary?FMath::Max(30.f,Stop):300,Dt);
    if(AimAtTarget&&!Offset.IsNearlyZero())SetActorRotation(FMath::RInterpTo(GetActorRotation(),Offset.GetSafeNormal2D().Rotation(),Dt,12));
    if(GetController()){FVector Eye;FRotator Aim;GetActorEyesViewPoint(Eye,Aim);GetController()->SetControlRotation(Aim);}
}
void AHeliosHumanoidPet::StartPrimaryReload()
{
    if(!EquippedPrimary||!EquippedPrimary->UsesAmmunition()||ReloadRemaining>=0)return;
    CharacterPresentation->CancelCharacterGesture();
    EquippedPrimary->StopFiring();EquippedPrimary->StopSecondaryFire();ReloadDuration=PrimaryDefinition->ReloadSeconds;
    if(Loadout.PrimaryWeaponId==TEXT("cfm_chan_zeus"))ReloadDuration=HeliosChanZeus::Tuning(true).Reload;
    else if(Loadout.PrimaryWeaponId==TEXT("cfm_bulianshi_p90"))ReloadDuration=HeliosBuLianShiP90::Tuning(true).Reload;
    else if(Loadout.PrimaryWeaponId==TEXT("cfm_wangzhekui_thunder"))ReloadDuration=HeliosWangZheKuiThunder::Tuning(true).Reload;
    else if(Loadout.PrimaryWeaponId==TEXT("cfm_yun_summer_megatron"))ReloadDuration=HeliosYunSummer::Tuning(true).Reload;
    if(const auto* Dragon=Cast<AHeliosBlackDragonSoulWeapon>(EquippedPrimary);Dragon&&Dragon->ReloadRemaining()>0)ReloadDuration=Dragon->ReloadRemaining();
    ReloadRemaining=FMath::Max(.01f,ReloadDuration);AttackAge=0;EquippedPrimary->BeginReload(ReloadRemaining);Presentation->BeginReload(ReloadRemaining,true);
}
void AHeliosHumanoidPet::UpdateAttack(float Dt)
{
    if(!IsValid(EquippedPrimary))return;PressWait=FMath::Max(0.f,PressWait-Dt);
    if(ReloadRemaining>=0)
    {ReloadRemaining-=Dt;AttackAge=ReloadDuration-FMath::Max(0.f,ReloadRemaining);if(ReloadRemaining<=0){EquippedPrimary->ReloadMagazine();Presentation->FinishReload();ReloadRemaining=-1;AttackAge=-1;++Reloads;}return;}
    if(Loadout.CharacterId==TEXT("cfm_guanxiaoyu"))
    {
        const auto* Enemy=Target.Get();const float Distance=Enemy?FVector::Dist(GetActorLocation(),Enemy->GetActorLocation()):FLT_MAX;
        const bool Visible=Enemy&&!Enemy->IsEnemyDead()&&HasClearLine(Target.Get());
        if(GuanXiaoYuAbility->IsUsingUmbrella()&&(!Visible||Distance>360))GuanXiaoYuAbility->Cancel();
        else if(!GuanXiaoYuAbility->IsUsingUmbrella()&&Visible&&Distance<=GuanXiaoYuAbility->Range)GuanXiaoYuAbility->ToggleUmbrella();
        if(GuanXiaoYuAbility->IsUsingUmbrella())
        {
            if(Visible&&Distance<=GuanXiaoYuAbility->Range&&FVector::DotProduct(GetActorForwardVector(),(Enemy->GetActorLocation()-GetActorLocation()).GetSafeNormal2D())>.9f)
            {if(GuanXiaoYuAbility->CooldownRemaining()<=0)GuanXiaoYuAbility->StartHeavyAttack();else {GuanXiaoYuAbility->StartLightAttack();GuanXiaoYuAbility->StopLightAttack();}}
            return;
        }
    }
    if(EquippedPrimary->UsesAmmunition()&&EquippedPrimary->GetBulletCount()<=0){StartPrimaryReload();return;}
    const auto* E=Target.Get();const bool Facing=E&&FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,(E->GetActorLocation()-GetActorLocation()).Rotation().Yaw))<8;
    const bool Ready=E&&!E->IsEnemyDead()&&Facing&&!GetCharacterMovement()->IsFalling()&&GetVelocity().Size2D()<20&&FVector::Dist2D(GetActorLocation(),E->GetActorLocation())<=WeaponRange&&HasClearLine(Target.Get());
    if(Ready&&HitPoseRemaining<=0)
    {
        const bool Hunter=Loadout.PrimaryWeaponId==TEXT("cfm_sunshangxiang")||Loadout.PrimaryWeaponId==TEXT("cfm_zhongxiaokui");
        if(PressWait<=0&&(!Hunter||!CharacterPresentation->IsSourceActionPlaying()))
        {
            CharacterPresentation->CancelCharacterGesture();
            EquippedPrimary->StopFiring();EquippedPrimary->StopSecondaryFire();
            if(auto* SSX=Cast<AHeliosSunShangXiangWeapon>(EquippedPrimary))
            {
                if(SSX->CooldownRemaining()<=0)SSX->StartSpecialFire();
                else if(AttackSerial++%3==2)SSX->StartSecondaryFire();
                else SSX->StartFiring();
            }
            else EquippedPrimary->StartFiring();
            if(Hunter){EquippedPrimary->StopFiring();EquippedPrimary->StopSecondaryFire();}
            PressWait=FMath::Max(.05f,EquippedPrimary->GetRefireRate());
        }
    }
    else{EquippedPrimary->StopFiring();EquippedPrimary->StopSecondaryFire();}
    AttackAge=bMeleePrimary&&CharacterPresentation->IsSourceActionPlaying()?FMath::Max(0.f,AttackAge+Dt):-1;
    SetPose(HitPoseRemaining>0?FName("Hit"):GetVelocity().Size2D()>20?FName("Move"):FName("Idle"),HitPoseRemaining<=0);
}
void AHeliosHumanoidPet::CancelAttack()
{
    GuanXiaoYuAbility->Cancel();
    if(IsValid(EquippedPrimary)){EquippedPrimary->StopFiring();EquippedPrimary->StopSecondaryFire();EquippedPrimary->CancelReload();}
    if(Presentation)Presentation->Cancel(true);ReloadRemaining=-1;PressWait=0;Super::CancelAttack();
}
void AHeliosHumanoidPet::Tick(float Dt)
{
    PoseAge+=Dt;ShotAge+=Dt;
    if(IsSleeping()||(CompanionOwner.IsValid()&&CompanionOwner->IsDead()))
    {if(auto* Dragon=UHeliosBlackDragonSoulState::Get(this,false))Dragon->Reset();CancelAttack();}
    Super::Tick(Dt);HideSourceMeshes();
    if(!Target.IsValid())GetCharacterMovement()->bOrientRotationToMovement=true;
    if(IsSleeping()||State==EHeliosPetState::Waking)SetPose(CurrentPose,false);
}
float AHeliosHumanoidPet::TakeDamage(float Amount,const FDamageEvent& Event,AController* C,AActor* Source)
{const float Applied=Super::TakeDamage(Amount,Event,C,Source);if(Applied>0)CharacterPresentation->PlayCharacterCue(IsSleeping()?FName("Death"):FName("Hurt"));return Applied;}
void AHeliosHumanoidPet::EndPlay(const EEndPlayReason::Type Reason)
{
    CancelAttack();if(IsValid(EquippedPrimary)){EquippedPrimary->OnShotPresented.Remove(ShotHandle);EquippedPrimary->DeactivateWeapon();EquippedPrimary->Destroy();}EquippedPrimary=nullptr;Super::EndPlay(Reason);
}
