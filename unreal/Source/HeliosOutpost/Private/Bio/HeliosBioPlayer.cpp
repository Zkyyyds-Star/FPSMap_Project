#include "Bio/HeliosBioPlayer.h"
#include "Bio/HeliosBioHotelGameMode.h"
#include "Bio/HeliosBioStaticJumpLink.h"
#include "Bio/HeliosBioClone.h"
#include "Bio/HeliosBioTurret.h"
#include "Bio/HeliosBioArt.h"
#include "Bio/HeliosBioFourSlashComponent.h"
#include "Bio/HeliosBioRoundInventoryComponent.h"
#include "Bio/HeliosBioSupplyWeapon.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Weapons/HeliosFirstPersonPresentationComponent.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Weapons/HeliosCombatWeapons.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraTypes.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/Package.h"

void UHeliosBioPlayerMovement::Crouch(bool bClientSimulation)
{
    if(auto* Player=Cast<AHeliosBioPlayer>(GetOwner());Player&&Player->UsesConfiguredBioCrouch()){Player->ApplyConfiguredBioCrouch(true);return;}
    Super::Crouch(bClientSimulation);
}
void UHeliosBioPlayerMovement::UnCrouch(bool bClientSimulation)
{
    if(auto* Player=Cast<AHeliosBioPlayer>(GetOwner());Player&&Player->UsesConfiguredBioCrouch()){Player->ApplyConfiguredBioCrouch(false);return;}
    Super::UnCrouch(bClientSimulation);
}
AHeliosBioPlayer::AHeliosBioPlayer(const FObjectInitializer& ObjectInitializer)
    :Super(ObjectInitializer.SetDefaultSubobjectClass<UHeliosBioPlayerMovement>(ACharacter::CharacterMovementComponentName))
{
    RoundInventory=CreateDefaultSubobject<UHeliosBioRoundInventoryComponent>(TEXT("BioRoundInventory"));
    FourSlash=CreateDefaultSubobject<UHeliosBioFourSlashComponent>(TEXT("BioFourSlash"));
    HumanPresentation=CreateDefaultSubobject<UHeliosCharacterPresentationComponent>(TEXT("BioHumanPresentation"));
    WeaponPresentation=CreateDefaultSubobject<UHeliosFirstPersonPresentationComponent>(TEXT("BioWeaponPresentation"));
    BioArms=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("BioArms"));
    BioArms->SetupAttachment(FirstPersonCamera);
    BioArms->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BioArms->SetCanEverAffectNavigation(false);
    BioArms->SetCastShadow(false);
    BioArms->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
    BioArms->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Tags.Add(TEXT("Player"));Tags.Add(TEXT("Friendly"));
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch=true;
}
void AHeliosBioPlayer::BeginPlay(){Super::BeginPlay();BioArms->SetVisibility(false);ApplyViewProfile();}
void AHeliosBioPlayer::EndPlay(const EEndPlayReason::Type Reason)
{FourSlash->Cancel();StopBioFire();RoundInventory->ResetRound(true);if(Weapon)Weapon->Destroy();Super::EndPlay(Reason);}
int32 AHeliosBioPlayer::GetAmmo() const {return Weapon?Weapon->GetBulletCount():0;}
float AHeliosBioPlayer::GetHealth()const
{auto* M=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=M?M->Model.FindParticipant(0):nullptr;return P?P->Health:0;}
bool AHeliosBioPlayer::EquipBioLoadout(const FHeliosLoadout& In)
{
    const auto* D=UHeliosWeaponRegistry::FindWeapon(In.PrimaryWeaponId);
    if(!D||!D->WeaponClass||In.PrimaryWeaponId==TEXT("mech")||In.PrimaryWeaponId==TEXT("fighter"))return false;
    auto* Next=GetWorld()->SpawnActorDeferred<AShooterWeapon>(D->WeaponClass,GetActorTransform(),this,this,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Next)return false;
    bPreparingInventoryWeapon=true;
    if(auto* W=Cast<AHeliosCombatWeapon>(Next))W->ApplyDefinition(D);
    UGameplayStatics::FinishSpawningActor(Next,GetActorTransform());
    if(IsValid(Next))Next->ConfigureMagazine(D->MagazineSize);
    bPreparingInventoryWeapon=false;
    if(!IsValid(Next))return false;
    StopBioFire();
    if(!HumanPresentation->ApplyLoadout(FirstPersonCamera,In,Next))
    {if(IsValid(Weapon))HumanPresentation->ApplyLoadout(FirstPersonCamera,Loadout,Weapon);Next->Destroy();return false;}
    RoundInventory->ResetRound(true);WeaponPresentation->Unequip();
    if(Weapon){Weapon->Destroy();Weapon=nullptr;}
    Loadout=In;MagazineSize=D->MagazineSize;ReloadDuration=D->ReloadSeconds;ReloadRemaining=0;
    Weapon=Next;LastAmmo=MagazineSize;Weapon->ActivateWeapon(TEXT("Player"));
    RoundInventory->RegisterBaseline(Weapon,Loadout,ReserveAmmo,MagazineSize,ReloadDuration);
    LastTeam=EHeliosBioTeam::Human;return true;
}
void AHeliosBioPlayer::AttachWeaponMeshes(AShooterWeapon* W)
{
    if(!W)return;
    W->AttachToActor(this,FAttachmentTransformRules::KeepWorldTransform);
    if(bPreparingInventoryWeapon){W->GetFirstPersonMesh()->SetVisibility(false,true);W->GetThirdPersonMesh()->SetVisibility(false,true);return;}
    const auto* D=UHeliosWeaponRegistry::FindWeapon(Loadout.PrimaryWeaponId);
    W->GetFirstPersonMesh()->AttachToComponent(FirstPersonCamera,FAttachmentTransformRules::KeepRelativeTransform);
    if(D)W->GetFirstPersonMesh()->SetRelativeTransform(FTransform(D->AttachmentRotation,D->AttachmentLocation,D->AttachmentScale));
    W->GetFirstPersonMesh()->SetVisibility(true,true);W->GetThirdPersonMesh()->SetVisibility(false,true);
}
void AHeliosBioPlayer::OnWeaponActivated(AShooterWeapon* W){Weapon=W;AttachWeaponMeshes(W);}
void AHeliosBioPlayer::OnWeaponDeactivated(AShooterWeapon* W){if(Weapon==W)StopBioFire();}
void AHeliosBioPlayer::AddWeaponClass(const TSubclassOf<AShooterWeapon>& C){}
void AHeliosBioPlayer::PlayFiringMontage(UAnimMontage* M){}
void AHeliosBioPlayer::AddWeaponRecoil(float R){if(Controller)AddControllerPitchInput(-FMath::Min(R,.8f));}
void AHeliosBioPlayer::UpdateWeaponHUD(int32 Ammo,int32 Magazine){if(bPreparingInventoryWeapon)return;if(!bReloadCommit&&Ammo<LastAmmo)++ShotsFired;LastAmmo=Ammo;MagazineSize=Magazine;}
FVector AHeliosBioPlayer::GetWeaponTargetLocation(){return FirstPersonCamera->GetComponentLocation()+GetControlRotation().Vector()*10000;}
void AHeliosBioPlayer::OnSemiWeaponRefire(){if(bHoldingFire)StartBioFire();}
void AHeliosBioPlayer::SetupPlayerInputComponent(UInputComponent* I)
{
    Super::SetupPlayerInputComponent(I);
    I->BindAction(TEXT("HeliosFire"),IE_Pressed,this,&AHeliosBioPlayer::StartBioFire);
    I->BindAction(TEXT("HeliosFire"),IE_Released,this,&AHeliosBioPlayer::StopBioFire);
    I->BindAction(TEXT("HeliosReload"),IE_Pressed,this,&AHeliosBioPlayer::ReloadBio);
    I->BindAction(TEXT("HeliosSecondary"),IE_Pressed,this,&AHeliosBioPlayer::StartHeavy);
    I->BindAction(TEXT("HeliosSecondary"),IE_Released,this,&AHeliosBioPlayer::StopBioSecondary);
    I->BindKey(EKeys::G,IE_Pressed,this,&AHeliosBioPlayer::StartBioSpecial);
    I->BindKey(EKeys::One,IE_Pressed,this,&AHeliosBioPlayer::SelectRoundSlot1);
    I->BindKey(EKeys::Two,IE_Pressed,this,&AHeliosBioPlayer::SelectRoundSlot2);
    I->BindKey(EKeys::Three,IE_Pressed,this,&AHeliosBioPlayer::SelectRoundSlot3);
    I->BindKey(EKeys::Four,IE_Pressed,this,&AHeliosBioPlayer::SelectRoundSlot4);
    I->BindKey(EKeys::Five,IE_Pressed,this,&AHeliosBioPlayer::SelectRoundSlot5);
    I->BindKey(EKeys::MouseScrollUp,IE_Pressed,this,&AHeliosBioPlayer::PreviousRoundWeapon);
    I->BindKey(EKeys::MouseScrollDown,IE_Pressed,this,&AHeliosBioPlayer::NextRoundWeapon);
    I->BindKey(EKeys::E,IE_Pressed,this,&AHeliosBioPlayer::TryTransform);
    I->BindKey(EKeys::F,IE_Pressed,this,&AHeliosBioPlayer::UseBioSkill);
    I->BindKey(EKeys::F5,IE_Pressed,this,&AHeliosBioPlayer::RequestBioRestart);
    I->BindKey(EKeys::C,IE_Pressed,this,&AHeliosBioPlayer::CrouchBio);
    I->BindKey(EKeys::Left,IE_Pressed,this,&AHeliosBioPlayer::SpectatePrevious);
    I->BindKey(EKeys::Right,IE_Pressed,this,&AHeliosBioPlayer::SpectateNext);
}
void AHeliosBioPlayer::QuitPlay(){if(auto* M=AHeliosBioHotelGameMode::Find(GetWorld()))M->PauseBio();}
void AHeliosBioPlayer::SelectRoundSlot1(){if(!RoundInventory->Select(0))SelectBioMelee();}
void AHeliosBioPlayer::SelectRoundSlot2(){if(!RoundInventory->Select(1))SelectBioPrimary();}
void AHeliosBioPlayer::SelectRoundSlot3(){RoundInventory->Select(2);}
void AHeliosBioPlayer::SelectRoundSlot4(){RoundInventory->Select(3);}
void AHeliosBioPlayer::SelectRoundSlot5(){RoundInventory->Select(4);}
void AHeliosBioPlayer::NextRoundWeapon(){RoundInventory->Cycle(1);}
void AHeliosBioPlayer::PreviousRoundWeapon(){RoundInventory->Cycle(-1);}
void AHeliosBioPlayer::TryTransform(){if(auto* M=AHeliosBioHotelGameMode::Find(GetWorld())){if(const auto* P=M->Model.FindParticipant(0);P&&P->bHunterChoicePending)M->OpenHunterChooser();else M->TransformParticipant(0);}}
void AHeliosBioPlayer::UseBioSkill()
{
    if(FourSlash->IsBusy())return;
    if(bBioPrimary)SelectBioMelee();
    if(auto* M=AHeliosBioHotelGameMode::Find(GetWorld()))if(M->UseParticipantSkill(0))
    {
        M->ShowNotice(TEXT("技能已启动"));const auto* P=M->Model.FindParticipant(0);
        if(FourSlash->IsBusy()){M->ShowNotice(TEXT("四连斩"));return;}
        if(P&&P->Team!=EHeliosBioTeam::Human)
        {
            auto* Art=HeliosBioArt::Load(*P);
            if(auto* SkillMesh=Art?Art->SkillFirstPersonMesh.LoadSynchronous():nullptr)
            {BioArms->SetSkeletalMesh(SkillMesh);BioArms->SetRelativeTransform(Art->SkillFirstPersonTransform);BioSkill=Art->FPSkill.LoadSynchronous();bSkillMeshOverride=true;}
            StopBioFire();
            if(BioSkill){BioSkillStage=1;CurrentBioClip=BioSkill;CurrentBioLocomotion=nullptr;AttackAge=0;bRecovering=false;bPlayRecovery=false;MeleeCooldown=FMath::Max(MeleeCooldown,BioSkill->GetPlayLength());BioArms->PlayAnimation(BioSkill,false);}
            const bool Wukong=Art&&Art->SourceIdentity.Contains(TEXT("SunWuKong"),ESearchCase::IgnoreCase);
            const bool BaiGu=Art&&Art->SourceIdentity.Contains(TEXT("BaiGuJing"),ESearchCase::IgnoreCase);
            const TCHAR* Library=Wukong||BaiGu?TEXT("/Game/Helios/CFMBioUltimateEffects/Effects/DA_ULTFX_Effects.DA_ULTFX_Effects"):TEXT("/Game/Helios/CFMBioEffects/Effects/DA_BIOFX_Effects.DA_BIOFX_Effects");
            if(Wukong)AHeliosSSXEffect::Spawn(this,TEXT("FX_1P_SunWuKong_Skill"),FirstPersonCamera->GetComponentLocation(),GetControlRotation(),true,1.f,Library);
            else if(Art&&!Art->FirstPersonSkillEffect.IsEmpty()&&!Art->SkillEffectLibrary.IsEmpty())AHeliosSSXEffect::Spawn(this,*Art->FirstPersonSkillEffect,FirstPersonCamera->GetComponentLocation(),GetControlRotation(),true,1.f,*Art->SkillEffectLibrary);
            if(Art&&!Art->ThirdPersonSkillEffect.IsEmpty()&&!Art->SkillEffectLibrary.IsEmpty())if(auto* FX=AHeliosSSXEffect::Spawn(this,*Art->ThirdPersonSkillEffect,GetActorLocation()-FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),GetActorRotation(),false,1.f,*Art->SkillEffectLibrary))FX->SetLifeSpan(Art->SkillDuration);
        }
    }
}
void AHeliosBioPlayer::StartHeavy()
{auto* M=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=M?M->Model.FindParticipant(0):nullptr;if(!P||!P->bAlive||P->FrozenSeconds>0||M->bHunterChooserOpen||M->Model.GetPhase()!=EHeliosBioPhase::Active)return;if(P->Team!=EHeliosBioTeam::Human){if(bBioPrimary)SelectBioMelee();Melee(true);}else if(Weapon)Weapon->StartSecondaryFire();}
void AHeliosBioPlayer::StopBioSecondary(){if(Weapon)Weapon->StopSecondaryFire();}
void AHeliosBioPlayer::StartBioSpecial()
{
    auto* M=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=M?M->Model.FindParticipant(0):nullptr;
    if(!P||!P->bAlive||P->FrozenSeconds>0||M->bHunterChooserOpen||M->Model.GetPhase()!=EHeliosBioPhase::Active||FourSlash->IsBusy())return;
    if(P->Team==EHeliosBioTeam::Human){if(Weapon)Weapon->StartSpecialFire();return;}
    if(bBioPrimary)SelectBioMelee();
    if(P->HunterRoleId==TEXT("BioRedeemerHunter"))
    {
        auto* SerumArt=HeliosBioArt::Load(*P);
        if(!SerumArt||!SerumArt->bSerumSourceVerified||P->SecondaryCooldownRemaining>0||MeleeCooldown>0)return;
        auto* Gun=SerumArt->SecondaryFirstPersonMesh.LoadSynchronous();auto* Idle=SerumArt->FPSecondaryIdle.LoadSynchronous();auto* Fire=SerumArt->FPSecondarySkill.LoadSynchronous();
        if(!Gun||!Idle||!Fire||Idle->GetSkeleton()!=Gun->GetSkeleton()||Fire->GetSkeleton()!=Gun->GetSkeleton())return;
        StopBioFire();BioArms->SetSkeletalMesh(Gun);BioArms->SetRelativeTransform(SerumArt->SecondaryWeaponTransform);
        BioArms->SetVisibility(true);BioArms->PlayAnimation(Idle,true);BioArms->TickAnimation(0,false);BioArms->RefreshBoneTransforms();BioArms->UpdateComponentToWorld();
        FVector Muzzle;
        if(!M->GetParticipantSerumMuzzle(0,Muzzle)||!M->FireParticipantSerum(0,Muzzle,GetControlRotation().Vector())){ApplyTeamPresentation(P->Team,true);return;}
        bSkillMeshOverride=true;bPlayRecovery=false;bRecovering=false;CurrentBioLocomotion=nullptr;CurrentBioClip=Fire;BioSkillStage=4;AttackAge=0;MeleeCooldown=Fire->GetPlayLength();
        BioArms->PlayAnimation(Fire,false);M->ShowNotice(TEXT("救赎血清已发射"));return;
    }
    if(!M->UseParticipantSecondarySkill(0))return;
    auto* Art=HeliosBioArt::Load(*P);StopBioFire();
    if(auto* Clip=Art?Art->FPSecondarySkill.LoadSynchronous():nullptr){CurrentBioClip=Clip;CurrentBioLocomotion=nullptr;BioSkillStage=0;AttackAge=0;bPlayRecovery=false;MeleeCooldown=Clip->GetPlayLength();BioArms->PlayAnimation(Clip,false);}
    if(Art&&!Art->SecondaryEffect.IsEmpty()&&!Art->SkillEffectLibrary.IsEmpty())
        if(auto* FX=AHeliosSSXEffect::Spawn(this,*Art->SecondaryEffect,FirstPersonCamera->GetComponentLocation(),GetControlRotation(),true,1.f,*Art->SkillEffectLibrary))FX->FollowLocalViewTranslation();
    M->ShowNotice(P->bGhostMaskActive?TEXT("傩·刃解已启动 · 首次攻击释放剑气"):TEXT("奥术手榴弹已投出"));
}
void AHeliosBioPlayer::SelectBioMelee()
{
    if(!bBioPrimary)return;
    auto* M=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=M?M->Model.FindParticipant(0):nullptr;
    if(P&&P->bAlive)ApplyTeamPresentation(P->Team,true);
}
void AHeliosBioPlayer::SelectBioPrimary()
{
    auto* M=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=M?M->Model.FindParticipant(0):nullptr;auto* Art=P?HeliosBioArt::Load(*P):nullptr;
    if(!P||!P->bAlive||P->FrozenSeconds>0||bBioPrimary||!Art||Art->Ability!=EHeliosBioAbility::MechanicalFire||FourSlash->IsBusy())return;
    auto* PrimaryMesh=Art->PrimaryFirstPersonMesh.LoadSynchronous();auto* Idle=Art->FPPrimaryIdle.LoadSynchronous();auto* Fire=Art->FPPrimaryFire.LoadSynchronous();
    if(!PrimaryMesh||!Idle||!Fire||Idle->GetSkeleton()!=PrimaryMesh->GetSkeleton()||Fire->GetSkeleton()!=PrimaryMesh->GetSkeleton())return;
    StopBioFire();bBioPrimary=true;BioSkillStage=0;CurrentBioLocomotion=nullptr;bRecovering=bPlayRecovery=false;
    BioArms->SetSkeletalMesh(PrimaryMesh);BioArms->SetRelativeTransform(Art->PrimaryWeaponTransform);BioIdle=Idle;BioMove=nullptr;BioRecover=nullptr;
    CurrentBioClip=Art->FPPrimaryEquip.LoadSynchronous();AttackAge=0;
    if(CurrentBioClip)BioArms->PlayAnimation(CurrentBioClip,false);else{CurrentBioLocomotion=Idle;BioArms->PlayAnimation(Idle,true);}
    M->ShowNotice(TEXT("XM214 · 能量自动恢复 · 1 切换机械臂"));
}
void AHeliosBioPlayer::FireBioPrimary()
{
    auto* M=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=M?M->Model.FindParticipant(0):nullptr;auto* Art=P?HeliosBioArt::Load(*P):nullptr;
    if(!bBioPrimary||!M||!Art||M->bHunterChooserOpen||!M->FireParticipantRoleWeapon(0,FirstPersonCamera->GetComponentLocation(),GetControlRotation().Vector()))return;
    ++ShotsFired;AttackAge=0;CurrentBioClip=Art->FPPrimaryFire.LoadSynchronous();CurrentBioLocomotion=nullptr;
    if(CurrentBioClip)BioArms->PlayAnimation(CurrentBioClip,false);
}
void AHeliosBioPlayer::StartBioFire()
{
    auto* M=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=M?M->Model.FindParticipant(0):nullptr;
    if(!P||!P->bAlive||P->FrozenSeconds>0||M->Model.GetPhase()!=EHeliosBioPhase::Active||GetWorld()->IsPaused()||M->bHunterChooserOpen||FourSlash->IsBusy())return;
    bHoldingFire=true;
    if(P->Team==EHeliosBioTeam::Human){if(Weapon&&ReloadRemaining<=0&&GetAmmo()>0)Weapon->StartFiring();else if(GetAmmo()==0)ReloadBio();}
    else if(bBioPrimary)FireBioPrimary();else Melee(false);
}
void AHeliosBioPlayer::StopBioFire(){bHoldingFire=false;if(Weapon){Weapon->StopFiring();Weapon->StopSecondaryFire();}}
void AHeliosBioPlayer::ReloadBio()
{
    auto* M=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=M?M->Model.FindParticipant(0):nullptr;
    if(!P||!P->bAlive||P->Team!=EHeliosBioTeam::Human||P->FrozenSeconds>0||GetWorld()->IsPaused()||M->bHunterChooserOpen||M->Model.GetPhase()==EHeliosBioPhase::RoundOver||!Weapon)return;
    if(auto* Native=Cast<AHeliosBioSupplyWeapon>(Weapon);Native&&Native->CancelPendingThrow(TEXT("ReloadInput"))){StopBioFire();return;}
    if(ReloadRemaining>0||ReserveAmmo<=0||GetAmmo()>=MagazineSize)return;
    Weapon->StopFiring();ReloadRemaining=FMath::Max(.1f,ReloadDuration);Weapon->BeginReload(ReloadRemaining);
}
void AHeliosBioPlayer::AddSupplyAmmo(){if(RoundInventory->IsEnabled()&&RoundInventory->HasSlot(0))RoundInventory->AddBaselineAmmo();else ReserveAmmo=FMath::Min(1800,ReserveAmmo+300);}
bool AHeliosBioPlayer::BeginBioFourSlash()
{
    auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=Mode?Mode->Model.FindParticipant(0):nullptr;
    auto* RoleArt=P?HeliosBioArt::Load(*P):nullptr;
    if(MeleeCooldown>0||!FourSlash->BeginSourceSequence(0,RoleArt,BioArms,true))return false;
    ++MeleeAttacks;BioSkillStage=0;CurrentBioClip=nullptr;AttackAge=0;
    StopBioFire();CurrentBioLocomotion=nullptr;bPlayRecovery=false;bRecovering=false;
    return true;
}
void AHeliosBioPlayer::ResetForBioRound()
{
    if(RoundInventory->IsEnabled()&&RoundInventory->HasSlot(0))RoundInventory->ResetRound();
    ResetBioCrouchState();
    AHeliosBioStaticJumpLink::Forget(this,true);
    FourSlash->Cancel();StopBioFire();ReloadRemaining=0;MeleeCooldown=0;ReserveAmmo=600;bLastAlive=true;SpectatedParticipantId=INDEX_NONE;
    if(bWasFrozen){GetCharacterMovement()->MaxAcceleration=FrozenSavedAcceleration;bWasFrozen=false;}
    if(auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld()))
    {
        const auto& Config=Mode->Model.Config;GetCapsuleComponent()->SetCapsuleSize(Config.MovementCapsuleRadius,Config.MovementCapsuleHalfHeight);
        auto* Movement=GetCharacterMovement();Movement->NavAgentProps.AgentRadius=Config.MovementCapsuleRadius;Movement->NavAgentProps.AgentHeight=Config.MovementCapsuleHalfHeight*2;
        Movement->MaxStepHeight=Config.MovementStepHeight;Movement->SetWalkableFloorAngle(Config.MovementSlopeDegrees);
        if(Config.MovementJumpZ>0)Movement->JumpZVelocity=Config.MovementJumpZ;
        if(!ApplyViewProfile())
        {
            FirstPersonCamera->SetRelativeLocation(FVector(-10,0,Config.EyeHeight-Config.MovementCapsuleHalfHeight));
            BaseEyeHeight=Config.EyeHeight-Config.MovementCapsuleHalfHeight;
        }
    }
    if(Weapon){Weapon->CancelReload();Weapon->ConfigureMagazine(MagazineSize);}
    Tags.Remove(TEXT("Dead"));GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    ApplyTeamPresentation(EHeliosBioTeam::Human,true);
    if(auto* M=AHeliosBioHotelGameMode::Find(GetWorld())){SetActorLocation(M->SpawnLocation(0),false,nullptr,ETeleportType::TeleportPhysics);SetActorRotation(M->SpawnRotation(0));if(Controller)Controller->SetControlRotation(M->SpawnRotation(0));}
}
void AHeliosBioPlayer::RequestBioRestart()
{if(auto* M=AHeliosBioHotelGameMode::Find(GetWorld())){if(M->Model.GetPhase()==EHeliosBioPhase::RoundOver)M->RestartBioRound();else M->ShowNotice(TEXT("回合结束后按 F5 重开"));}}
void AHeliosBioPlayer::CrouchBio(){if(bIsCrouched)UnCrouch();else Crouch();}
float AHeliosBioPlayer::GetPlayerViewAspectRatio() const
{
    int32 Width=0,Height=0;if(auto* PC=Cast<APlayerController>(GetController()))PC->GetViewportSize(Width,Height);
    // A single local player uses the entire unconstrained viewport. Before it exists,
    // the explicit map preview reference is 16:9, not another map's camera aspect.
    return Width>0&&Height>0?float(Width)/float(Height):16.f/9.f;
}
bool AHeliosBioPlayer::ApplyViewProfile()
{
    auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());
    if(!Mode||!Mode->bFloatingProfile||!Mode->Model.Config.PlayerCamera.bEnabled||!FirstPersonCamera)return false;
    const auto& Config=Mode->Model.Config;const auto& View=Config.PlayerCamera;auto* Capsule=GetCapsuleComponent();
    const float CrouchedHalf=FMath::Clamp(View.CrouchedCapsuleHalfHeight,Config.MovementCapsuleRadius,Config.MovementCapsuleHalfHeight);
    GetCharacterMovement()->SetCrouchedHalfHeight(CrouchedHalf);
    const float CurrentHalf=Capsule->GetUnscaledCapsuleHalfHeight();
    const float EyeHeight=bIsCrouched?View.CrouchedEyeHeight+CurrentHalf-CrouchedHalf:View.StandingEyeHeight;
    BaseEyeHeight=EyeHeight-CurrentHalf;
    FirstPersonCamera->SetRelativeLocation(FVector(View.HorizontalOffset.X,View.HorizontalOffset.Y,BaseEyeHeight));
    const float Aspect=GetPlayerViewAspectRatio();
    const auto HorizontalFOV=[Aspect](float Vertical){return FMath::RadiansToDegrees(2.f*FMath::Atan(FMath::Tan(FMath::DegreesToRadians(Vertical)*.5f)*Aspect));};
    FirstPersonCamera->SetAspectRatio(Aspect);FirstPersonCamera->bConstrainAspectRatio=false;
    // Our input is authored vertical FOV. Convert once for UE's horizontal fields,
    // and prevent the local player's axis policy from applying a second conversion.
    FirstPersonCamera->bOverrideAspectRatioAxisConstraint=true;
    FirstPersonCamera->SetAspectRatioAxisConstraint(AspectRatio_MaintainXFOV);
    FirstPersonCamera->SetFieldOfView(HorizontalFOV(View.WorldVerticalFOV));
    FirstPersonCamera->SetEnableFirstPersonFieldOfView(true);
    FirstPersonCamera->SetFirstPersonFieldOfView(HorizontalFOV(View.FirstPersonVerticalFOV));
    FirstPersonCamera->SetEnableFirstPersonScale(true);FirstPersonCamera->SetFirstPersonScale(View.FirstPersonScale);
    return true;
}
bool AHeliosBioPlayer::UsesConfiguredBioCrouch() const
{
    return GetWorld()&&GetWorld()->GetOutermost()->GetName().EndsWith(TEXT("/CFMBioPyramidFloating"))&&AHeliosBioHotelGameMode::Find(GetWorld());
}
void AHeliosBioPlayer::ResetBioCrouchState()
{
    if(bIsCrouched)if(auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld()))
    {
        GetMesh()->AddRelativeLocation(FVector(0,0,GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()-Mode->Model.Config.MovementCapsuleHalfHeight));
        BaseTranslationOffset=GetMesh()->GetRelativeLocation();
    }
    SetIsCrouched(false);GetCharacterMovement()->bWantsToCrouch=false;CrouchBlockLogAt=-100;
}
bool AHeliosBioPlayer::ApplyConfiguredBioCrouch(bool Crouched)
{
    auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());if(!Mode)return false;
    const auto* State=Mode->Model.FindParticipant(0);auto* Movement=GetCharacterMovement();auto* Capsule=GetCapsuleComponent();
    if(!State||!State->bAlive){ResetBioCrouchState();return false;}
    if(Crouched&&!Movement->CanCrouchInCurrentState())return false;
    const auto& Config=Mode->Model.Config;const float Radius=Config.MovementCapsuleRadius,StandingHalf=Config.MovementCapsuleHalfHeight;
    const float TargetHalf=Crouched?FMath::Clamp(Movement->GetCrouchedHalfHeight(),Radius,StandingHalf):StandingHalf;
    const float OldHalf=Capsule->GetUnscaledCapsuleHalfHeight(),Scale=Capsule->GetShapeScale();
    const FVector FeetBefore=GetActorLocation()-FVector(0,0,Capsule->GetScaledCapsuleHalfHeight());
    const FVector TargetCenter=FeetBefore+FVector(0,0,TargetHalf*Scale);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BioConfiguredCrouchClearance),false,this);TArray<AActor*> Attached;GetAttachedActors(Attached,true,true);Query.AddIgnoredActors(Attached);
    const FCollisionShape TargetShape=FCollisionShape::MakeCapsule(Radius*Scale,TargetHalf*Scale);
    if(!Crouched&&GetWorld()->OverlapBlockingTestByChannel(TargetCenter,GetActorQuat(),Capsule->GetCollisionObjectType(),TargetShape,Query))
    {
        if(GetWorld()->GetTimeSeconds()-CrouchBlockLogAt>.5){CrouchBlockLogAt=GetWorld()->GetTimeSeconds();UE_LOG(LogTemp,Display,TEXT("BIO_CONFIGURED_UNCROUCH_BLOCKED target_center=%s radius=%.1f half=%.1f feet=%s"),*TargetCenter.ToString(),Radius,TargetHalf,*FeetBefore.ToString());}
        return false;
    }
    const FVector MeshBefore=GetMesh()->GetRelativeLocation(),ActorBefore=GetActorLocation();const float RadiusBefore=Capsule->GetUnscaledCapsuleRadius();FHitResult Hit;
    if(TargetHalf<OldHalf)Capsule->SetCapsuleSize(Radius,TargetHalf,false);
    // For standing, sweep the existing SMALL capsule to the validated center
    // before expanding it, so the intermediate capsule never enters the floor.
    Capsule->MoveComponent(TargetCenter-GetActorLocation(),GetActorQuat(),true,&Hit);
    if(FVector::DistSquared(GetActorLocation(),TargetCenter)>.01f)
    {
        FHitResult RollbackHit;Capsule->MoveComponent(ActorBefore-GetActorLocation(),GetActorQuat(),true,&RollbackHit);Capsule->SetCapsuleSize(RadiusBefore,OldHalf,true);
        UE_LOG(LogTemp,Display,TEXT("BIO_CONFIGURED_CROUCH_MOVE_BLOCKED crouched=%d actor=%s component=%s"),Crouched,*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()));return false;
    }
    Capsule->SetCapsuleSize(Radius,TargetHalf,true);SetIsCrouched(Crouched);Movement->bWantsToCrouch=Crouched;Movement->bForceNextFloorCheck=true;
    if(Crouched)OnStartCrouch(OldHalf-TargetHalf,(OldHalf-TargetHalf)*Scale);else OnEndCrouch(TargetHalf-OldHalf,(TargetHalf-OldHalf)*Scale);
    GetMesh()->SetRelativeLocation(MeshBefore+FVector(0,0,OldHalf-TargetHalf));BaseTranslationOffset=GetMesh()->GetRelativeLocation();
    Movement->GetNavAgentPropertiesRef().AgentRadius=Capsule->GetScaledCapsuleRadius();Movement->GetNavAgentPropertiesRef().AgentHeight=Capsule->GetScaledCapsuleHalfHeight()*2;
    if(!ApplyViewProfile()){BaseEyeHeight=Config.EyeHeight-StandingHalf;FirstPersonCamera->SetRelativeLocation(FVector(-10,0,BaseEyeHeight));}
    const FVector FeetAfter=GetActorLocation()-FVector(0,0,Capsule->GetScaledCapsuleHalfHeight());
    UE_LOG(LogTemp,Display,TEXT("BIO_CONFIGURED_CROUCH crouched=%d radius=%.1f half=%.1f native_crouched_half=%.1f feet_delta=%.4f world_eye_above_feet=%.1f"),Crouched,Capsule->GetUnscaledCapsuleRadius(),Capsule->GetUnscaledCapsuleHalfHeight(),Movement->GetCrouchedHalfHeight(),FVector::Dist(FeetBefore,FeetAfter),FirstPersonCamera->GetComponentLocation().Z-FeetAfter.Z);
    return true;
}
void AHeliosBioPlayer::Melee(bool Heavy)
{
    auto* M=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=M?M->Model.FindParticipant(0):nullptr;
    if(!P||!P->bAlive||P->FrozenSeconds>0||M->bHunterChooserOpen||M->Model.GetPhase()!=EHeliosBioPhase::Active||MeleeCooldown>0||FourSlash->IsBusy())return;
    ++MeleeAttacks;AttackAge=0;BioSkillStage=0;bRecovering=false;bPlayRecovery=true;CurrentBioLocomotion=nullptr;
    CurrentBioClip=Heavy&&BioHeavy?BioHeavy:BioAttack;
    MeleeCooldown=CurrentBioClip?FMath::Max(.35f,CurrentBioClip->GetPlayLength()):Heavy?1.2f:.65f;
    if(BioRecover)MeleeCooldown+=BioRecover->GetPlayLength();
    if(CurrentBioClip)BioArms->PlayAnimation(CurrentBioClip,false);
    auto* AttackArt=HeliosBioArt::Load(*P);
    const bool ModernWukong=AttackArt&&AttackArt->SourceIdentity.Contains(TEXT("SunWuKong"),ESearchCase::IgnoreCase);
    const bool ModernBaiGu=AttackArt&&AttackArt->SourceIdentity.Contains(TEXT("BaiGuJing"),ESearchCase::IgnoreCase);
    if(ModernWukong||ModernBaiGu)
    {
        USoundBase* Sound=Heavy?AttackArt->HeavySound.LoadSynchronous():AttackArt->AttackSound.LoadSynchronous();if(Sound)UGameplayStatics::PlaySound2D(this,Sound,.8f);
        const TCHAR* Effect=ModernWukong?(Heavy?TEXT("FX_1P_SunWuKong_FireHeavy"):TEXT("FX_1P_SunWuKong_FireFirst")):(Heavy?TEXT("FX_1P_BaiGuJing_Fu_Attack2"):TEXT("FX_1P_BaiGuJing_Fu_Attack1"));
        if(auto* FX=AHeliosSSXEffect::Spawn(this,Effect,FirstPersonCamera->GetComponentLocation(),GetControlRotation(),true,1.f,TEXT("/Game/Helios/CFMBioUltimateEffects/Effects/DA_ULTFX_Effects.DA_ULTFX_Effects")))FX->FollowLocalViewTranslation();
    }
    else if(AttackArt&&!AttackArt->SourceIdentity.Contains(TEXT("SunShangXiang"),ESearchCase::IgnoreCase)&&!AttackArt->SourceIdentity.Contains(TEXT("FamilyHumanHero"),ESearchCase::IgnoreCase))
    {if(auto* Sound=(Heavy?AttackArt->HeavySound:AttackArt->AttackSound).LoadSynchronous())UGameplayStatics::PlaySound2D(this,Sound,.8f);}
    const bool SSX=AttackArt&&AttackArt->SourceIdentity.Contains(TEXT("SunShangXiang"),ESearchCase::IgnoreCase);
    if(SSX)
    {
        if(auto* Sound=(Heavy?AttackArt->HeavySound:AttackArt->AttackSound).LoadSynchronous())UGameplayStatics::PlaySound2D(this,Sound,.8f);
        if(auto* FX=AHeliosSSXEffect::Spawn(this,Heavy?TEXT("FX_1P_SunShangXiang_Trail03"):TEXT("FX_1P_SunShangXiang_Trail01"),FirstPersonCamera->GetComponentLocation(),GetControlRotation(),true))FX->FollowLocalViewTranslation();
    }
    if(P->IsHunter()&&AttackArt&&AttackArt->SourceIdentity.Contains(TEXT("FamilyHumanHero"),ESearchCase::IgnoreCase))
    {
        const TCHAR* Audio=Heavy?TEXT("SFX_HumanHeroHeavy"):TEXT("SFX_HumanHeroLight");
        const FString Path=FString::Printf(TEXT("/Game/Helios/CFMBioHotel/Audio/%s.%s"),Audio,Audio);
        if(auto* S=LoadObject<USoundBase>(nullptr,*Path))UGameplayStatics::PlaySound2D(this,S,.8f);
        const int32 VoiceIndex=MeleeAttacks%3;
        const FString VoiceName=VoiceIndex==0?TEXT("SFX_HumanHeroAttack"):FString::Printf(TEXT("SFX_HumanHeroAttack_%d"),VoiceIndex);
        const FString VoicePath=FString::Printf(TEXT("/Game/Helios/CFMBioHotel/Audio/%s.%s"),*VoiceName,*VoiceName);
        if(auto* Voice=LoadObject<USoundBase>(nullptr,*VoicePath))UGameplayStatics::PlaySound2D(this,Voice,.65f);
        if(auto* FX=AHeliosSSXEffect::Spawn(this,Heavy?TEXT("FX_1P_FamilyHumanHero_Trail03"):TEXT("FX_1P_FamilyHumanHero_Trail01"),FirstPersonCamera->GetComponentLocation(),GetControlRotation(),true,1.f,TEXT("/Game/Helios/CFMBioEffects/Effects/DA_BIOFX_Effects.DA_BIOFX_Effects")))FX->FollowLocalViewTranslation();
    }
    const FVector View=FirstPersonCamera->GetComponentLocation(),Forward=GetControlRotation().Vector();
    if(M->FireParticipantSwordWave(0,View,Forward,Heavy))return;
    int32 Best=INDEX_NONE;float Dist=Heavy?300.f:240.f;
    for(const auto& Other:M->Model.GetParticipants())
    {
        if(!Other.bAlive||!M->AreOpponents(0,Other.Id))continue;
        auto* A=M->ParticipantActor(Other.Id);if(!A)continue;
        const FVector Delta=A->GetActorLocation()+FVector(0,0,25)-View;
        float Reach=Delta.Size();
        if(auto* Character=Cast<ACharacter>(A)){FVector Closest;if(Character->GetCapsuleComponent()->GetClosestPointOnCollision(View,Closest)>=0)Reach=FVector::Dist(View,Closest);}
        if(Reach>Dist||FVector::DotProduct(Delta.GetSafeNormal(),Forward)<.55f)continue;
        FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(BioPlayerMelee),false,this);Q.AddIgnoredActor(Weapon);
        if(GetWorld()->LineTraceSingleByChannel(Hit,View,A->GetActorLocation(),ECC_Visibility,Q)&&Hit.GetActor()!=A)continue;
        Best=Other.Id;Dist=Reach;
    }
    AHeliosBioClone* BestClone=nullptr;
    for(auto Clone:M->Clones)
    {
        if(!IsValid(Clone)||!Clone->IsActiveClone()||!M->AreOpponents(0,Clone->GetMotherId()))continue;
        const FVector Delta=Clone->GetActorLocation()+FVector(0,0,25)-View;if(Delta.Size()>Dist||FVector::DotProduct(Delta.GetSafeNormal(),Forward)<.55f)continue;
        FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(BioPlayerCloneMelee),false,this);Q.AddIgnoredActor(Weapon);
        if(GetWorld()->LineTraceSingleByChannel(Hit,View,Clone->GetActorLocation(),ECC_Visibility,Q)&&Hit.GetActor()!=Clone)continue;
        BestClone=Clone;Best=INDEX_NONE;Dist=Delta.Size();
    }
    AHeliosBioTurret* BestTurret=nullptr;
    for(auto Turret:M->Turrets)
    {
        if(!IsValid(Turret)||!Turret->IsActiveTurret()||!M->AreOpponents(0,Turret->GetCasterId()))continue;
        const FVector Center=Turret->GetComponentsBoundingBox().GetCenter(),Delta=Center-View;
        const float Reach=FVector::Dist(View,Turret->GetComponentsBoundingBox().GetClosestPointTo(View));
        if(Reach>Dist||FVector::DotProduct(Delta.GetSafeNormal(),Forward)<.55f)continue;
        FHitResult Obstruction;FCollisionQueryParams Query(SCENE_QUERY_STAT(BioPlayerTurretMelee),false,this);Query.AddIgnoredActor(Weapon);
        if(GetWorld()->LineTraceSingleByChannel(Obstruction,View,Center,ECC_Visibility,Query)&&Obstruction.GetActor()!=Turret)continue;
        BestTurret=Turret;BestClone=nullptr;Best=INDEX_NONE;Dist=Reach;
    }
    const float Damage=P->IsHunter()?(Heavy?2200.f:1250.f):M->Model.Config.ZombieMeleeDamage;
    const bool Hit=BestTurret?BestTurret->ApplyBioDamage(0,Damage)>0:BestClone?BestClone->ApplyBioDamage(0,Damage)>0:Best!=INDEX_NONE&&M->DamageParticipant(Best,0,Damage,P->IsHunter());
    UE_LOG(LogTemp,Display,TEXT("BIO_PLAYER_MELEE heavy=%d victim=%d hit=%d damage=%f"),Heavy,Best,Hit,Damage);
}
void AHeliosBioPlayer::ApplyTeamPresentation(EHeliosBioTeam Team,bool Force)
{
    auto* RoleMode=AHeliosBioHotelGameMode::Find(GetWorld());const auto* RoleP=RoleMode?RoleMode->Model.FindParticipant(0):nullptr;
    const FName RoleId=RoleP?(Team==EHeliosBioTeam::Zombie?RoleP->ZombieRoleId:RoleP->HunterRoleId):NAME_None;
    if(!Force&&LastTeam==Team&&LastRoleId==RoleId)return;
    ApplyViewProfile();
    LastRoleId=RoleId;
    bBioPrimary=false;
    bSkillMeshOverride=false;
    FourSlash->Cancel();
    StopBioFire();ReloadRemaining=0;if(Weapon)Weapon->CancelReload();
    LastTeam=Team;BioSkillStage=0;CurrentBioClip=nullptr;CurrentBioLocomotion=nullptr;bRecovering=false;bPlayRecovery=false;MeleeCooldown=0;
    if(Team==EHeliosBioTeam::Human)
    {
        BioArms->SetVisibility(false);
        if(RoundInventory->IsEnabled()&&RoundInventory->HasSlot(0))
        {RoundInventory->Restore();Tags.AddUnique(TEXT("Friendly"));return;}
        if(Weapon){Weapon->DeactivateWeapon();Weapon->SetActorHiddenInGame(false);Weapon->ActivateWeapon(TEXT("Player"));}
        HumanPresentation->ApplyLoadout(FirstPersonCamera,Loadout,Weapon);
        Tags.AddUnique(TEXT("Friendly"));return;
    }
    Tags.Remove(TEXT("Friendly"));
    RoundInventory->Suspend();
    HumanPresentation->SetVisible(false);WeaponPresentation->Unequip();
    if(Weapon){Weapon->DeactivateWeapon();Weapon->SetActorHiddenInGame(true);}
    auto* M=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=M?M->Model.FindParticipant(0):nullptr;
    auto* Art=P?HeliosBioArt::Load(*P):nullptr;
    if(Art)
    {
        BioArms->SetSkeletalMesh(Art->FirstPersonMesh.LoadSynchronous());
        BioArms->SetRelativeTransform(Art->FirstPersonTransform);
        BioIdle=Art->FPIdle.LoadSynchronous();BioAttack=Art->FPAttack.LoadSynchronous();BioHeavy=Art->FPHeavy.LoadSynchronous();
        BioRecover=Art->FPRecover.LoadSynchronous();BioMove=Art->FPMove.LoadSynchronous();BioSkill=Art->FPSkill.LoadSynchronous();BioSkillLoop=Art->FPSkillLoop.LoadSynchronous();BioSkillEnd=Art->FPSkillEnd.LoadSynchronous();
        BioArms->SetVisibility(true);
        if(auto* EquipClip=Art->FPEquip.LoadSynchronous()){CurrentBioClip=EquipClip;AttackAge=0;MeleeCooldown=EquipClip->GetPlayLength();BioArms->PlayAnimation(EquipClip,false);}
        else if(BioIdle){CurrentBioLocomotion=BioIdle;BioArms->PlayAnimation(BioIdle,true);}
        if(auto* S=Art->TransformSound.LoadSynchronous())UGameplayStatics::PlaySound2D(this,S,.8f);
        M->ShowNotice(HeliosBioArt::TeamName(Team)+TEXT(" · ")+Art->DisplayName);
        const bool Wukong=Art->SourceIdentity.Contains(TEXT("SunWuKong"),ESearchCase::IgnoreCase),BaiGu=Art->SourceIdentity.Contains(TEXT("BaiGuJing"),ESearchCase::IgnoreCase);
        if(Wukong||BaiGu)AHeliosSSXEffect::Spawn(this,Wukong?TEXT("FX_3P_SunWuKong_Energy"):TEXT("FX_3P_BaiGuJing_Energy"),GetActorLocation()-FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),GetActorRotation(),false,1.f,TEXT("/Game/Helios/CFMBioUltimateEffects/Effects/DA_ULTFX_Effects.DA_ULTFX_Effects"));
        else if(P->IsHunter()&&Art->SourceIdentity.Contains(TEXT("FamilyHumanHero"),ESearchCase::IgnoreCase))if(auto* FX=AHeliosSSXEffect::Spawn(this,TEXT("FX_3P_FamilyHumanHero_Buff"),GetActorLocation()-FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),GetActorRotation(),false,1.f,TEXT("/Game/Helios/CFMBioEffects/Effects/DA_BIOFX_Effects.DA_BIOFX_Effects")))FX->SetLifeSpan(3.f);
    }
    else if(M)M->ShowNotice(HeliosBioArt::TeamName(Team)+TEXT("资源未导入"));
}
void AHeliosBioPlayer::Tick(float Dt)
{
    Super::Tick(Dt);if(GetWorld()->IsPaused())return;
    auto* M=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=M?M->Model.FindParticipant(0):nullptr;if(!P)return;
    ApplyTeamPresentation(P->Team);
    if(P->FrozenSeconds>0&&!bWasFrozen){FrozenSavedAcceleration=GetCharacterMovement()->MaxAcceleration;GetCharacterMovement()->MaxAcceleration=0;bWasFrozen=true;}
    else if(P->FrozenSeconds<=0&&bWasFrozen){GetCharacterMovement()->MaxAcceleration=FrozenSavedAcceleration;bWasFrozen=false;}
    if(!P->bAlive){ResetBioCrouchState();FourSlash->Cancel();StopBioFire();Tags.AddUnique(TEXT("Dead"));GetCharacterMovement()->DisableMovement();GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);BioArms->SetVisibility(false);HumanPresentation->SetVisible(false);if(bLastAlive){WeaponPresentation->Unequip();if(Weapon)Weapon->DeactivateWeapon();}}
    else
    {
        Tags.Remove(TEXT("Dead"));SpectatedParticipantId=INDEX_NONE;if(!bLastAlive){ResetBioCrouchState();GetCapsuleComponent()->SetCapsuleSize(M->Model.Config.MovementCapsuleRadius,M->Model.Config.MovementCapsuleHalfHeight);GetCharacterMovement()->GetNavAgentPropertiesRef().AgentRadius=M->Model.Config.MovementCapsuleRadius;GetCharacterMovement()->GetNavAgentPropertiesRef().AgentHeight=M->Model.Config.MovementCapsuleHalfHeight*2;if(!ApplyViewProfile()){BaseEyeHeight=M->Model.Config.EyeHeight-M->Model.Config.MovementCapsuleHalfHeight;FirstPersonCamera->SetRelativeLocation(FVector(-10,0,BaseEyeHeight));}GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);SetActorLocation(M->SpawnLocation(0),false,nullptr,ETeleportType::TeleportPhysics);GetCharacterMovement()->SetMovementMode(MOVE_Walking);ApplyTeamPresentation(P->Team,true);}
        const float Speed=P->Team==EHeliosBioTeam::Human?450.f:P->IsHunter()?560.f:500.f;
        const float Multiplier=P->FrozenSeconds>0?0.f:P->MovementSkillMultiplier();
        WalkSpeed=Speed*Multiplier;SprintSpeed=Speed*1.15f*Multiplier;GetCharacterMovement()->MaxWalkSpeed=WalkSpeed;
        if(P->FrozenSeconds>0){FourSlash->Cancel();StopBioFire();ClearMovementInput();if(GetCharacterMovement()->IsMovingOnGround())GetCharacterMovement()->StopMovementImmediately();}
    }
    bLastAlive=P->bAlive;
    MeleeCooldown=FMath::Max(0.f,MeleeCooldown-Dt);AttackAge+=Dt;
    if(bHoldingFire&&P->Team!=EHeliosBioTeam::Human&&!FourSlash->IsBusy()){if(bBioPrimary)FireBioPrimary();else if(MeleeCooldown==0)Melee(false);}
    if(!FourSlash->IsBusy()&&BioSkillStage==2&&P->SkillRemaining<=0)
    {BioSkillStage=3;AttackAge=0;CurrentBioClip=BioSkillEnd;if(BioSkillEnd)BioArms->PlayAnimation(BioSkillEnd,false);else BioSkillStage=0;}
    if(!FourSlash->IsBusy()&&P->Team!=EHeliosBioTeam::Human&&P->bAlive&&CurrentBioClip&&BioSkillStage!=2&&AttackAge>CurrentBioClip->GetPlayLength())
    {
        if(BioSkillStage==1&&BioSkillLoop&&P->SkillRemaining>0){BioSkillStage=2;CurrentBioClip=BioSkillLoop;AttackAge=0;BioArms->PlayAnimation(BioSkillLoop,true);}
        else if(bPlayRecovery&&!bRecovering&&BioRecover){bRecovering=true;CurrentBioClip=BioRecover;AttackAge=0;BioArms->PlayAnimation(BioRecover,false);}
        else{CurrentBioClip=nullptr;BioSkillStage=0;bRecovering=false;bPlayRecovery=false;CurrentBioLocomotion=nullptr;}
    }
    if(!FourSlash->IsBusy()&&P->Team!=EHeliosBioTeam::Human&&P->bAlive&&!CurrentBioClip)
    {
        if(bSkillMeshOverride)ApplyTeamPresentation(P->Team,true);
        UAnimSequence* Locomotion=GetVelocity().Size2D()>10&&BioMove?BioMove.Get():BioIdle.Get();
        if(Locomotion&&CurrentBioLocomotion!=Locomotion){CurrentBioLocomotion=Locomotion;BioArms->PlayAnimation(Locomotion,true);}
    }
    if(ReloadRemaining>0)
    {
        ReloadRemaining=FMath::Max(0.f,ReloadRemaining-Dt);
        if(ReloadRemaining==0&&Weapon)
        {
            const int32 Before=GetAmmo();const int32 Loaded=FMath::Min(ReserveAmmo,MagazineSize-Before);
            ReserveAmmo-=Loaded;bReloadCommit=true;Weapon->ReloadMagazine();Weapon->SetMagazineBulletCount(Before+Loaded);bReloadCommit=false;
            if(bHoldingFire)StartBioFire();
        }
    }
}
float AHeliosBioPlayer::TakeDamage(float D,const FDamageEvent& E,AController* I,AActor* C)
{auto* M=AHeliosBioHotelGameMode::Find(GetWorld());const int32 Attacker=M?M->ParticipantId(I?I->GetPawn():C):INDEX_NONE;return M&&M->DamageParticipant(0,Attacker,D,false)?D:0.f;}
void AHeliosBioPlayer::CalcCamera(float Dt,FMinimalViewInfo& R)
{
    const bool DedicatedView=ApplyViewProfile();
    auto* M=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=M?M->Model.FindParticipant(0):nullptr;
    if(P&&!P->bAlive)
    {
        const auto* Target=M->Model.FindParticipant(SpectatedParticipantId);
        if(!Target||!Target->bAlive)CycleSpectator(1);
        if(auto* Actor=M->ParticipantActor(SpectatedParticipantId))
        {
            const FVector Look=Actor->GetActorLocation()+FVector(0,0,60);
            const FVector Desired=Look-Actor->GetActorForwardVector()*280+FVector(0,0,100);
            FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(BioSpectatorCamera),false,Actor);Q.AddIgnoredActor(this);
            R.Location=GetWorld()->LineTraceSingleByChannel(Hit,Look,Desired,ECC_Camera,Q)?Hit.ImpactPoint+Hit.ImpactNormal*10:Desired;
            R.Rotation=(Look-R.Location).Rotation();R.FOV=DedicatedView?FirstPersonCamera->FieldOfView:90.f;R.bUseFirstPersonParameters=false;
            if(DedicatedView){R.DesiredFOV=R.FOV;R.AspectRatio=GetPlayerViewAspectRatio();R.AspectRatioAxisConstraint=AspectRatio_MaintainXFOV;R.bConstrainAspectRatio=false;}
            return;
        }
    }
    Super::CalcCamera(Dt,R);R.bUseFirstPersonParameters=true;R.PerspectiveNearClipPlane=.1f;
    if(DedicatedView)
    {
        R.FOV=FirstPersonCamera->FieldOfView;R.DesiredFOV=R.FOV;R.FirstPersonFOV=FirstPersonCamera->FirstPersonFieldOfView;R.FirstPersonScale=M->Model.Config.PlayerCamera.FirstPersonScale;
        R.AspectRatio=GetPlayerViewAspectRatio();R.AspectRatioAxisConstraint=AspectRatio_MaintainXFOV;R.bConstrainAspectRatio=false;
    }
    else{R.FirstPersonFOV=R.FOV;R.FirstPersonScale=.2f;}
}
void AHeliosBioPlayer::SpectatePrevious(){CycleSpectator(-1);}
void AHeliosBioPlayer::SpectateNext(){CycleSpectator(1);}
void AHeliosBioPlayer::CycleSpectator(int32 Direction)
{
    auto* M=AHeliosBioHotelGameMode::Find(GetWorld());const auto* Local=M?M->Model.FindParticipant(0):nullptr;
    if(!Local||Local->bAlive)return;
    const int32 Count=M->Model.GetParticipants().Num();if(Count<2)return;
    int32 Candidate=SpectatedParticipantId==INDEX_NONE?0:SpectatedParticipantId;
    for(int32 Step=0;Step<Count;++Step)
    {Candidate=(Candidate+Direction+Count)%Count;const auto* P=M->Model.FindParticipant(Candidate);if(Candidate!=0&&P&&P->bAlive&&M->ParticipantActor(Candidate)){SpectatedParticipantId=Candidate;return;}}
    SpectatedParticipantId=INDEX_NONE;
}
