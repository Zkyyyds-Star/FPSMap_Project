#include "Player/HeliosArenaCharacter.h"
#include "Player/HeliosGuanXiaoYuAbilityComponent.h"
#include "Player/HeliosQingYaAbilityComponent.h"
#include "Player/HeliosQingYaSelection.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "Player/HeliosCharacterDefinition.h"
#include "Player/HeliosCharacterActionProfile.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Weapons/HeliosWangZheKuiThunder.h"
#include "Weapons/HeliosChanZeus.h"
#include "Weapons/HeliosBlackDragonSoul.h"
#include "Weapons/HeliosYunSummerMegatron.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "Combat/HeliosKillFeedbackSubsystem.h"
#include "Weapons/HeliosFighterWeapon.h"
#include "Weapons/HeliosWeaponDamageSettings.h"
#include "Weapons/HeliosWeaponTempoSettings.h"
#include "Combat/HeliosCombatTempoSettings.h"
#include "Health/CreatureHealthSettings.h"
#include "Pets/HeliosPetDog.h"

#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Weapons/HeliosCombatWeapons.h"
#include "Weapons/HeliosCalamityStaff.h"
#include "Weapons/HeliosZhongXiaoKui.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Weapons/HeliosFirstPersonPresentationComponent.h"
#include "ShooterWeapon.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
bool CanPresentLoadout(const FHeliosLoadout& Loadout)
{
    if (Loadout.PrimaryWeaponId == TEXT("mech") || Loadout.PrimaryWeaponId == TEXT("fighter")) return true;
    const auto* Character=FHeliosCharacterRegistry::FindCharacter(Loadout.CharacterId);
    const auto* Form=Character?Character->FindForm(Loadout.CharacterVariantId):nullptr;
    const auto* Profile=Form?Form->ActionProfile.LoadSynchronous():nullptr;
    const auto* Pair = Profile ? Profile->FindWeapon(Loadout.PrimaryWeaponId) : nullptr;
    return Pair && Pair->bGeneratedAndValidated && Pair->FirstPersonMesh.LoadSynchronous();
}
}

AHeliosArenaCharacter::AHeliosArenaCharacter()
{
    Presentation = CreateDefaultSubobject<UHeliosFirstPersonPresentationComponent>(TEXT("FirstPersonPresentation"));
    CharacterPresentation = CreateDefaultSubobject<UHeliosCharacterPresentationComponent>(TEXT("CharacterPresentation"));
    GuanXiaoYuAbility=CreateDefaultSubobject<UHeliosGuanXiaoYuAbilityComponent>(TEXT("GuanXiaoYuAbility"));
    QingYaAbility=CreateDefaultSubobject<UHeliosQingYaAbilityComponent>(TEXT("QingYaAbility"));
    ActorId = TEXT("player_01");
    Tags.AddUnique(FName(TEXT("Player")));
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);

    WeaponClass = AHeliosChanZeusWeapon::StaticClass();
}

void AHeliosArenaCharacter::BeginPlay()
{
    Super::BeginPlay();
    DeathResetSeconds = UHeliosCombatTempoSettings::Resolve(this, EHeliosTempoFile::Recovery,
        TEXT("玩家复活秒"), TEXT("非小镇死亡重置"), DeathResetSeconds);
    SpawnProtectionSeconds = UHeliosCombatTempoSettings::Resolve(this, EHeliosTempoFile::Recovery,
        TEXT("玩家复活秒"), TEXT("复活保护"), SpawnProtectionSeconds, 0.f);
    MaxHealth = UCreatureHealthSettings::Resolve(this, ECreatureHealthType::Player);
    CurrentHealth = MaxHealth;
    CurrentAmmo = MagazineSize;
    SpawnProtectionEndTime = GetWorld()->GetTimeSeconds() + SpawnProtectionSeconds;
    HeliosQingYaSelection::Restore(CurrentLoadout);
    EquipLoadout(CurrentLoadout);
}

void AHeliosArenaCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
    Super::CalcCamera(DeltaTime,OutResult);
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosNoMotionBlurQA"))){OutResult.PostProcessSettings.bOverride_MotionBlurAmount=true;OutResult.PostProcessSettings.MotionBlurAmount=0;OutResult.PostProcessBlendWeight=1;}
    if (CharacterPresentation && CharacterPresentation->IsReady() && CurrentWeaponId != TEXT("mech") && CurrentWeaponId != TEXT("fighter"))
    {
        OutResult.bUseFirstPersonParameters=true;
        OutResult.FirstPersonFOV=OutResult.FOV;
        OutResult.FirstPersonScale=GuanXiaoYuAbility->IsUsingUmbrella()?.2f:CharacterPresentation->GetFirstPersonViewScale();
        OutResult.PerspectiveNearClipPlane=CharacterPresentation->GetFirstPersonNearClip();
    }
    else if(GetWeaponId()==TEXT("cfm_sunshangxiang")||GetWeaponId()==TEXT("cfm_chan_zeus")||GetWeaponId()==TEXT("cfm_bulianshi_p90")||GetWeaponId()==TEXT("cfm_yun_summer_megatron"))
    {
        // Uniform camera-space depth compression preserves projected dimensions.
        // Keep authored arms at their origin and out of nearby world surfaces.
        OutResult.bUseFirstPersonParameters=true;
        OutResult.FirstPersonFOV=OutResult.FOV;
        OutResult.FirstPersonScale=.2f;
        OutResult.PerspectiveNearClipPlane=.1f;
    }
    // Diagnostic A/B only: compare ZXK against the already accepted SSX view
    // settings without changing either character in normal gameplay.
    if(GetWeaponId()==TEXT("cfm_zhongxiaokui")&&FParse::Param(FCommandLine::Get(),TEXT("HeliosInputControlQA")))
    {
        float Scale=1.f,NearPlane=0.f;
        if(FParse::Value(FCommandLine::Get(),TEXT("HeliosZXKViewScaleQA="),Scale))
        {OutResult.bUseFirstPersonParameters=true;OutResult.FirstPersonFOV=OutResult.FOV;OutResult.FirstPersonScale=FMath::Clamp(Scale,.01f,1.f);}
        if(FParse::Value(FCommandLine::Get(),TEXT("HeliosZXKNearPlaneQA="),NearPlane))OutResult.PerspectiveNearClipPlane=FMath::Clamp(NearPlane,.01f,10.f);
    }
}

void AHeliosArenaCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    QingYaAbility->Cancel();
    GuanXiaoYuAbility->Cancel();
    if (CharacterPresentation) CharacterPresentation->SetVisible(false);
    if(auto* Staff=UHeliosCalamityState::Get(this,false))Staff->Clear();
    if (Presentation) Presentation->Unequip();
    StopFire();
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(ReloadTimer);
        World->GetTimerManager().ClearTimer(DeathResetTimer);
    }
    if (Weapon)
    {
        Weapon->Destroy();
        Weapon = nullptr;
    }
    Super::EndPlay(EndPlayReason);
}

void AHeliosArenaCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    PlayerInputComponent->BindAction(TEXT("HeliosFire"), IE_Pressed, this, &AHeliosArenaCharacter::StartFire);
    PlayerInputComponent->BindAction(TEXT("HeliosFire"), IE_Released, this, &AHeliosArenaCharacter::StopFire);
    PlayerInputComponent->BindAction(TEXT("HeliosSpecial"), IE_Pressed, this, &AHeliosArenaCharacter::StartSpecialAttack);
    PlayerInputComponent->BindAction(TEXT("HeliosSecondary"), IE_Pressed, this, &AHeliosArenaCharacter::StartSecondaryAttack);
    PlayerInputComponent->BindAction(TEXT("HeliosSecondary"), IE_Released, this, &AHeliosArenaCharacter::StopSecondaryAttack);
    PlayerInputComponent->BindAction(TEXT("HeliosReload"), IE_Pressed, this, &AHeliosArenaCharacter::StartReload);
    PlayerInputComponent->BindKey(EKeys::Q, IE_Pressed, this, &AHeliosArenaCharacter::ToggleCalamityBackup);
    PlayerInputComponent->BindKey(EKeys::V, IE_Pressed, this, &AHeliosArenaCharacter::ToggleZhongXiaoKuiRanged);
    PlayerInputComponent->BindKey(EKeys::T, IE_Pressed, this, &AHeliosArenaCharacter::ToggleGuanXiaoYuUmbrella);
    PlayerInputComponent->BindKey(EKeys::H, IE_Pressed, this, &AHeliosArenaCharacter::ToggleCharacterForm);
    PlayerInputComponent->BindKey(EKeys::J, IE_Pressed, this, &AHeliosArenaCharacter::StartCharacterInteraction);
    PlayerInputComponent->BindKey(EKeys::K, IE_Pressed, this, &AHeliosArenaCharacter::StartQingYaGunGesture);
}

void AHeliosArenaCharacter::ToggleCalamityBackup()
{
    if(GuanXiaoYuAbility->IsUsingUmbrella())return;
    if(IsFighterMode())return;
    if (bDead || !GetWorld() || GetWorld()->IsPaused()) return;
    if(auto* W=Cast<AHeliosZhongXiaoKuiWeapon>(Weapon)){if(CharacterPresentation)CharacterPresentation->CancelCharacterGesture();NotifyManualInput();W->ToggleForm();return;}
    if(auto* W=Cast<AHeliosWangZheKuiThunderWeapon>(Weapon)){if(CharacterPresentation)CharacterPresentation->CancelCharacterGesture();NotifyManualInput();W->StartSecondaryFire();return;}
}

void AHeliosArenaCharacter::ToggleGuanXiaoYuUmbrella()
{if(CurrentLoadout.CharacterId==TEXT("cfm_guanxiaoyu")){NotifyManualInput();GuanXiaoYuAbility->ToggleUmbrella();}else if(CurrentLoadout.CharacterId==TEXT("cfm_qingya")){NotifyManualInput();QingYaAbility->StartKick();}}

void AHeliosArenaCharacter::ToggleCharacterForm()
{
    if (bDead || IsReloading() || !GetWorld() || GetWorld()->IsPaused()) return;
    auto Loadout=GetLoadout();const auto* Character=FHeliosCharacterRegistry::FindCharacter(Loadout.CharacterId);
    if(!Character||Character->Forms.Num()<2)return;
    const auto* Current=Character->FindForm(Loadout.CharacterVariantId);
    const int32 Index=Character->Forms.IndexOfByPredicate([Current](const FHeliosCharacterFormInfo& Form){return Current&&Form.VariantId==Current->VariantId;});
    Loadout.CharacterVariantId=Character->Forms[(Index+1)%Character->Forms.Num()].VariantId;
    if(EquipLoadout(Loadout))NotifyManualInput();
}

void AHeliosArenaCharacter::StartCharacterInteraction()
{
    if(IsQingYaBusy())return;
    if (bDead || IsReloading() || !GetWorld() || GetWorld()->IsPaused()) return;
    if(CurrentLoadout.CharacterId==TEXT("cfm_qingya")&&!IsMechMode()&&!IsFighterMode()&&CharacterPresentation&&CharacterPresentation->BeginCharacterGesture(TEXT("Love")))
    {NotifyManualInput();StopFire();return;}
    if(CurrentLoadout.CharacterId==TEXT("cfm_yun_summer")&&CharacterPresentation&&
        CharacterPresentation->BeginCharacterGesture(TEXT("Cheers")))
    {NotifyManualInput();StopFire();}
}
void AHeliosArenaCharacter::StartQingYaGunGesture()
{
    if(CurrentLoadout.CharacterId!=TEXT("cfm_qingya")||bDead||IsMechMode()||IsFighterMode()||IsReloading()||IsQingYaBusy()||!GetWorld()||GetWorld()->IsPaused())return;
    if(CharacterPresentation&&CharacterPresentation->BeginCharacterGesture(TEXT("GunFire"))){NotifyManualInput();StopFire();}
}
bool AHeliosArenaCharacter::IsQingYaBusy() const
{return QingYaAbility&&QingYaAbility->IsSelected()&&(QingYaAbility->IsAttacking()||QingYaAbility->SwitchRemaining()>0);}

void AHeliosArenaCharacter::ToggleZhongXiaoKuiRanged()
{
    if(GuanXiaoYuAbility->IsUsingUmbrella())return;
    if(bDead||!GetWorld()||GetWorld()->IsPaused())return;
    if(auto* W=Cast<AHeliosZhongXiaoKuiWeapon>(Weapon)){if(CharacterPresentation)CharacterPresentation->CancelCharacterGesture();NotifyManualInput();W->ToggleRangedForm();}
    else if(auto* Thunder=Cast<AHeliosWangZheKuiThunderWeapon>(Weapon)){if(CharacterPresentation)CharacterPresentation->CancelCharacterGesture();NotifyManualInput();Thunder->StartSecondaryFire();}
}
void AHeliosArenaCharacter::StartZhongXiaoKuiWave()
{
    if(GuanXiaoYuAbility->IsUsingUmbrella())return;
    if(bDead||!GetWorld()||GetWorld()->IsPaused())return;
    if(auto* W=Cast<AHeliosZhongXiaoKuiWeapon>(Weapon)){if(CharacterPresentation)CharacterPresentation->CancelCharacterGesture();NotifyManualInput();W->StartSwordWave();}
}

float AHeliosArenaCharacter::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser)
{
    if(Cast<AHeliosYunSummerMegatronWeapon>(Weapon)&&HeliosYunSummer::IsFriendlyExplosion(DamageEvent,EventInstigator,DamageCauser))return 0.f;
    if (!CanBeDamaged() || bDead || DamageAmount <= 0.0f || GetWorld()->GetTimeSeconds() < SpawnProtectionEndTime)
    {
        return 0.0f;
    }

    if(GuanXiaoYuAbility->TryAbsorbDamage(DamageAmount,DamageEvent,EventInstigator,DamageCauser))return 0;
    const float AppliedDamage = FMath::Min(CurrentHealth, DamageAmount);
    if(AppliedDamage>0&&CurrentLoadout.CharacterId==TEXT("cfm_qingya"))
    {QingYaAbility->Cancel();if(CharacterPresentation)CharacterPresentation->CancelCharacterGesture();}
    CurrentHealth -= AppliedDamage;
    if(GetWorld()->GetMapName().EndsWith(TEXT("UrbanChallenge")) && FParse::Param(FCommandLine::Get(),TEXT("HeliosUrbanQA")))
        UE_LOG(LogTemp,Display,TEXT("URBAN_PLAYER_DAMAGE actual=%.2f health=%.2f cause=%s instigator=%s"),AppliedDamage,CurrentHealth,*GetNameSafe(DamageCauser?DamageCauser->GetClass():nullptr),*GetNameSafe(EventInstigator?EventInstigator->GetPawn():nullptr));
    if (!Weapon || !Weapon->HandleOwnerCombatCue(CurrentHealth <= 0.f ? TEXT("FatalHurt") : TEXT("Hurt")))
    {
        if(CurrentLoadout.CharacterId==TEXT("cfm_qingya")&&CharacterPresentation)
        {if(CurrentHealth>0)CharacterPresentation->PlayCharacterCue(TEXT("Hurt"));}
        else OnCombatCue(TEXT("Hurt"));
    }
    if (CurrentHealth <= 0.0f)
    {
        HandleDeath();
    }
    return AppliedDamage;
}

void AHeliosArenaCharacter::AttachWeaponMeshes(AShooterWeapon* WeaponToAttach)
{
    if (!WeaponToAttach)
    {
        return;
    }

    WeaponToAttach->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
    WeaponToAttach->GetFirstPersonMesh()->AttachToComponent(FirstPersonCamera,
        FAttachmentTransformRules::KeepRelativeTransform);
    WeaponToAttach->GetFirstPersonMesh()->SetRelativeLocation(WeaponAttachmentLocation);
    WeaponToAttach->GetFirstPersonMesh()->SetRelativeRotation(WeaponAttachmentRotation);
    WeaponToAttach->GetFirstPersonMesh()->SetRelativeScale3D(WeaponAttachmentScale);
    WeaponToAttach->GetFirstPersonMesh()->SetVisibility(true, true);
    WeaponToAttach->GetThirdPersonMesh()->SetVisibility(false, true);
}

void AHeliosArenaCharacter::PlayFiringMontage(UAnimMontage* Montage)
{
    if (Presentation && Presentation->IsReady()) return;
    if (Montage && GetMesh() && GetMesh()->GetAnimInstance())
    {
        GetMesh()->GetAnimInstance()->Montage_Play(Montage);
    }
}

void AHeliosArenaCharacter::AddWeaponRecoil(float Recoil)
{
    AddControllerPitchInput(-FMath::Abs(Recoil));
}

void AHeliosArenaCharacter::UpdateWeaponHUD(int32 InCurrentAmmo, int32 InMagazineSize)
{
    if (InCurrentAmmo < CurrentAmmo && !bDead) OnCombatCue(TEXT("Fire"));
    CurrentAmmo = InCurrentAmmo;
    MagazineSize = InMagazineSize;
}

FVector AHeliosArenaCharacter::GetWeaponTargetLocation()
{
    const FVector Start = FirstPersonCamera->GetComponentLocation();
    const FVector End = Start + FirstPersonCamera->GetForwardVector() * 10000.0f;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(HeliosArenaAim), true, this);
    Params.AddIgnoredActor(Weapon.Get());
    if (auto* Pets=GetWorld()->GetSubsystem<UHeliosPetWorld>()) Pets->IgnoreAll(Params);
    return GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params)
        ? Hit.ImpactPoint : End;
}

void AHeliosArenaCharacter::AddWeaponClass(const TSubclassOf<AShooterWeapon>& InWeaponClass)
{
    if (!InWeaponClass) return;
    for (auto* Definition : UHeliosWeaponRegistry::GetAvailableWeapons())
        if (Definition && Definition->WeaponClass == InWeaponClass)
        {
            EquipWeaponDefinition(Definition);
            return;
        }
}

void AHeliosArenaCharacter::OnWeaponActivated(AShooterWeapon* InWeapon)
{
    Weapon = InWeapon;
}

void AHeliosArenaCharacter::OnWeaponDeactivated(AShooterWeapon* InWeapon)
{
}

void AHeliosArenaCharacter::OnSemiWeaponRefire()
{
    if (bTriggerHeld && !bReloading && !bDead && Weapon && !IsQingYaBusy())
    {
        Weapon->StartFiring();
    }
}

#include "Weapons/HeliosSoulBanner.h"
#include "Weapons/HeliosQingxiao.h"
void AHeliosArenaCharacter::ResetForTest()
{
    if(auto* Dragon=UHeliosBlackDragonSoulState::Get(this,false))Dragon->Reset();
    if(auto* F=Cast<AHeliosFighterWeapon>(Weapon))F->SuspendForArrival();
    if(IsMechMode())ExitMechMode();
    if(Weapon)Weapon->CancelReload();
    if(auto* Staff=UHeliosCalamityState::Get(this,false))Staff->Clear();
    if(auto* Formation=UHeliosQingxiaoFormation::Get(this,false))Formation->Clear();
    if(auto* Army=UHeliosSoulArmy::Get(this,false))Army->Clear();
    if (Presentation) Presentation->Cancel(false);
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(ReloadTimer);
        World->GetTimerManager().ClearTimer(DeathResetTimer);
    }
    NotifyManualInput();
    StopFire();
    bReloading = false;
    bDead = false;
    Tags.Remove(FName(TEXT("Dead")));
    CurrentHealth = MaxHealth;
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    ResetToSafeSpawn();
    SpawnProtectionEndTime = GetWorld()->GetTimeSeconds() + SpawnProtectionSeconds;
    if (Weapon)
    {
        Weapon->ReloadMagazine();
        Weapon->ActivateWeapon(TEXT("Player"));
    }
    if (Presentation) Presentation->ReplayEquip();
    if(CharacterPresentation)CharacterPresentation->CancelCharacterGesture();
    RefreshCharacterMovementMultiplier();
}

bool AHeliosArenaCharacter::EquipWeaponDefinition(UHeliosWeaponDefinition* Definition)
{
    if (!Definition || Definition->WeaponId.IsNone() || !Definition->WeaponClass ||
        Definition->MagazineSize <= 0 || Definition->ReloadSeconds < 0.0f ||
        !UHeliosWeaponRegistry::FindWeapon(Definition->WeaponId))
    {
        return false;
    }

    FHeliosLoadout Requested = CurrentLoadout;
    Requested.PrimaryWeaponId = Definition->WeaponId;
    FString LoadoutError;
    if (!FHeliosLoadout::Validate(Requested, LoadoutError) || !CanPresentLoadout(Requested)) return false;

    if(Definition->WeaponId==TEXT("mech"))
    {
        FVector DeployFeet;
        if(!FindMechDeployment(DeployFeet))return false;
    }
    CancelCombatAndControl();
    if (CharacterPresentation) CharacterPresentation->SetVisible(false);
    if (Presentation) Presentation->Unequip();
    if (Weapon)
    {
        Weapon->DeactivateWeapon();
        Weapon->Destroy();
        Weapon = nullptr;
    }

    Definition = DuplicateObject<UHeliosWeaponDefinition>(Definition, this);
    ApplyWeaponDamageSettings(this, Definition);
    ApplyWeaponTempoSettings(this, Definition);
    EquippedWeaponDefinition = Definition;
    CurrentWeaponId = Definition->WeaponId;
    WeaponMovementMultiplier=1.f;
    CurrentLoadout.PrimaryWeaponId = CurrentWeaponId;
    CurrentWeaponDisplayName = Definition->DisplayName;
    WeaponClass = Definition->WeaponClass;
    MagazineSize = Definition->MagazineSize;
    ReloadSeconds = Definition->ReloadSeconds;
    CurrentAmmo = MagazineSize;
    WeaponAttachmentLocation = Definition->AttachmentLocation;
    WeaponAttachmentRotation = Definition->AttachmentRotation;
    WeaponAttachmentScale = Definition->AttachmentScale;
    SpawnConfiguredWeapon();
    if (IsValid(Weapon)) SyncActiveLoadout();
    return IsValid(Weapon);
}

void AHeliosArenaCharacter::SyncActiveLoadout()
{
    HeliosQingYaSelection::Save(CurrentLoadout);
    if(auto* Director=AHeliosAdventureDirector::Find(GetWorld()))
        if(!Director->IsSelectingWeapon() && !Director->IsRunOver())Director->CaptureActiveLoadout();
}

void AHeliosArenaCharacter::SetEquipmentMovementMultiplier(float Multiplier)
{
    // Legacy Megatron reports the old character bonus. The selected character now owns it.
    WeaponMovementMultiplier=CurrentWeaponId==TEXT("cfm_yun_summer_megatron")?1.f:
        FMath::IsFinite(Multiplier)?FMath::Clamp(Multiplier,.1f,3.f):1.f;
    RefreshCharacterMovementMultiplier();
}
void AHeliosArenaCharacter::RefreshCharacterMovementMultiplier()
{
    if(bDead||CurrentWeaponId==TEXT("mech")||CurrentWeaponId==TEXT("fighter"))
    {Super::SetEquipmentMovementMultiplier(1.f);return;}
    const float CharacterMultiplier=CurrentLoadout.CharacterId==TEXT("cfm_yun_summer")&&CurrentLoadout.CharacterVariantId==TEXT("SP")?
        HeliosYunSummer::Setting(false,TEXT("灵魂武者移速倍率"),1.2f,1,3):1.f;
    Super::SetEquipmentMovementMultiplier(CharacterMultiplier*WeaponMovementMultiplier*QingYaAbility->MovementMultiplier());
}

FHeliosLoadout AHeliosArenaCharacter::GetLoadout() const
{
    FHeliosLoadout Result = CurrentLoadout;
    Result.PrimaryWeaponId = CurrentWeaponId;
    return Result;
}

bool AHeliosArenaCharacter::IsReloading() const
{
    if(const auto* Dragon=Cast<AHeliosBlackDragonSoulWeapon>(Weapon);Dragon&&Dragon->ReloadRemaining()>0)return true;
    const auto* ZhongXiaoKui=Cast<AHeliosZhongXiaoKuiWeapon>(Weapon);
    return bReloading||(ZhongXiaoKui&&ZhongXiaoKui->GetCurrentSourceAction()==TEXT("C_ChangeClip"));
}

bool AHeliosArenaCharacter::EquipLoadout(const FHeliosLoadout& Input)
{
    FString InputError;if(!FHeliosLoadout::Validate(Input,InputError))return false;
    const auto Loadout=FHeliosLoadout::ResolveLegacyVariant(Input,GetGameInstance());
    FString Error;
    if (!FHeliosLoadout::Validate(Loadout, Error) || Loadout.CharacterId == TEXT("sword_dog") || !CanPresentLoadout(Loadout)) return false;
    auto* Definition = UHeliosWeaponRegistry::FindWeapon(Loadout.PrimaryWeaponId);
    if (!Definition) return false;
    const FHeliosLoadout Previous = CurrentLoadout;
    GuanXiaoYuAbility->Cancel();
    QingYaAbility->Cancel();
    CurrentLoadout = Loadout;
    if(!QingYaAbility->IsSelected())QingYaAbility->NotifyWeaponEquipped();
    if (IsValid(Weapon) && CurrentWeaponId == Loadout.PrimaryWeaponId)
    {
        if (CurrentWeaponId == TEXT("mech") || CurrentWeaponId == TEXT("fighter")) { RefreshCharacterMovementMultiplier();SyncActiveLoadout();return true; }
        if (CharacterPresentation && CharacterPresentation->ConfigureFirstPerson(Loadout.CharacterId, Loadout.CharacterVariantId,
            Loadout.PrimaryWeaponId, FirstPersonCamera, Weapon)) { RefreshCharacterMovementMultiplier();SyncActiveLoadout();return true; }
        CurrentLoadout = Previous;
        if(CharacterPresentation)CharacterPresentation->ConfigureFirstPerson(Previous.CharacterId,Previous.CharacterVariantId,
            Previous.PrimaryWeaponId,FirstPersonCamera,Weapon);
        RefreshCharacterMovementMultiplier();
        return false;
    }
    if (EquipWeaponDefinition(Definition)) {QingYaAbility->NotifyWeaponEquipped();return true;}
    CurrentLoadout = Previous;
    RefreshCharacterMovementMultiplier();
    return false;
}

void AHeliosArenaCharacter::StartFire()
{
    if(IsQingYaBusy())return;
    if(GuanXiaoYuAbility->IsUsingUmbrella()){NotifyManualInput();GuanXiaoYuAbility->StartLightAttack();return;}
    NotifyManualInput();
    if (bDead || bReloading || !Weapon || (GetWorld() && GetWorld()->IsPaused()))
    {
        return;
    }
    if (Weapon->UsesAmmunition() && CurrentAmmo <= 0)
    {
        StartReload();
        return;
    }
    bTriggerHeld = true;
    if(CharacterPresentation)CharacterPresentation->CancelCharacterGesture();
    Weapon->StartFiring();
}

void AHeliosArenaCharacter::StopFire()
{
    GuanXiaoYuAbility->StopLightAttack();
    bTriggerHeld = false;
    if (Weapon)
    {
        Weapon->StopFiring();
    }
}

void AHeliosArenaCharacter::StartReload()
{
    if(IsQingYaBusy())return;
    if(GuanXiaoYuAbility->IsUsingUmbrella())return;
    NotifyManualInput();
    if(const auto* Dragon=Cast<AHeliosBlackDragonSoulWeapon>(Weapon);Dragon&&Dragon->ReloadRemaining()>0)return;
    if(auto* W=Cast<AHeliosWangZheKuiThunderWeapon>(Weapon);W&&W->IsSwitchingMode())return;
    if(auto* W=Cast<AHeliosZhongXiaoKuiWeapon>(Weapon)){if(!bDead){if(CharacterPresentation)CharacterPresentation->CancelCharacterGesture();W->RequestSourceReload();}return;}
    if (bDead || bReloading || !Weapon || !Weapon->UsesAmmunition() || CurrentAmmo >= MagazineSize ||
        (GetWorld() && GetWorld()->IsPaused()))
    {
        return;
    }
    StopFire();
    bReloading = true;
    if(CharacterPresentation)CharacterPresentation->CancelCharacterGesture();
    Weapon->BeginReload(ReloadSeconds);
    if (Presentation) Presentation->BeginReload(ReloadSeconds, CurrentAmmo == 0);
    OnCombatCue(TEXT("Reload"));
    GetWorld()->GetTimerManager().SetTimer(ReloadTimer, this,
        &AHeliosArenaCharacter::FinishReload, ReloadSeconds, false);
}

void AHeliosArenaCharacter::FinishReload()
{
    if (Presentation) Presentation->FinishReload();
    bReloading = false;
    if (!bDead && Weapon)
    {
        Weapon->ReloadMagazine();
    }
}

void AHeliosArenaCharacter::HandleDeath()
{
    GuanXiaoYuAbility->Cancel();
    if(auto* Dragon=UHeliosBlackDragonSoulState::Get(this,false))Dragon->Reset();
    if (auto* Feedback=GetWorld()->GetSubsystem<UHeliosKillFeedbackSubsystem>()) Feedback->ResetRun();
    if(auto* Staff=UHeliosCalamityState::Get(this,false))Staff->Clear();
    if(auto* Formation=UHeliosQingxiaoFormation::Get(this,false))Formation->Clear();
    if (Presentation) Presentation->Cancel(true);
    bDead = true;
    if(CharacterPresentation)CharacterPresentation->CancelCharacterGesture();
    RefreshCharacterMovementMultiplier();
    Tags.AddUnique(FName(TEXT("Dead")));
    bReloading = false;
    NotifyManualInput();
    StopFire();
    GetWorld()->GetTimerManager().ClearTimer(ReloadTimer);
    GetCharacterMovement()->DisableMovement();
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if (Weapon)
    {
        Weapon->CancelReload();
        Weapon->DeactivateWeapon();
    }
    if (!Weapon || !Weapon->HandleOwnerCombatCue(TEXT("Death")))
    {
        if(CurrentLoadout.CharacterId==TEXT("cfm_qingya")&&CharacterPresentation)CharacterPresentation->PlayCharacterCue(TEXT("Death"));
        else OnCombatCue(TEXT("Death"));
    }
    if (OnPlayerDefeated()) return;
    GetWorld()->GetTimerManager().SetTimer(DeathResetTimer, this,
        &AHeliosArenaCharacter::FinishDeathReset, DeathResetSeconds, false);
}

void AHeliosArenaCharacter::FinishDeathReset()
{
    ResetForTest();
}

void AHeliosArenaCharacter::SpawnConfiguredWeapon()
{
    if (Weapon || !WeaponClass || !GetWorld())
    {
        return;
    }

    FActorSpawnParameters Params;
    Params.Owner = this;
    Params.Instigator = this;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Weapon = GetWorld()->SpawnActor<AShooterWeapon>(WeaponClass, GetActorTransform(), Params);
    if (Weapon)
    {
        if (AHeliosCombatWeapon* HeliosWeapon = Cast<AHeliosCombatWeapon>(Weapon))
        {
            HeliosWeapon->ApplyDefinition(EquippedWeaponDefinition);
        }
        Weapon->ConfigureMagazine(MagazineSize);
        Weapon->ActivateWeapon(TEXT("Player"));
        if (Presentation)
        {
            const UHeliosWeaponDefinition* ArtDefinition = EquippedWeaponDefinition ? EquippedWeaponDefinition.Get() : UHeliosWeaponRegistry::FindWeapon(CurrentWeaponId);
            Presentation->Equip(this, Weapon, ArtDefinition);
        }
        if (CharacterPresentation && CurrentWeaponId != TEXT("mech") && CurrentWeaponId != TEXT("fighter"))
        {
            CharacterPresentation->ConfigureFirstPerson(CurrentLoadout.CharacterId, CurrentLoadout.CharacterVariantId,
                CurrentWeaponId, FirstPersonCamera, Weapon);
        }
        RefreshCharacterMovementMultiplier();
    }
}

float AHeliosArenaCharacter::Heal(float Amount)
{
    if (bDead || !FMath::IsFinite(Amount) || Amount <= 0) return 0;
    const float Applied = FMath::Min(Amount, MaxHealth - CurrentHealth);
    CurrentHealth += Applied;
    return Applied;
}

void AHeliosArenaCharacter::CancelCombatAndControl()
{
    QingYaAbility->Cancel();
    if(CharacterPresentation)CharacterPresentation->CancelCharacterGesture();
    if (Presentation) Presentation->Cancel(false);
    NotifyManualInput();
    StopFire();
    if(Weapon)Weapon->StopSecondaryFire();
    if (Weapon) Weapon->CancelReload();
    ClearMovementInput();
    if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(ReloadTimer);
    bReloading = false;
}

void AHeliosArenaCharacter::StartFireFromAutomation()
{
    if(IsQingYaBusy())return;
    if (bDead || bReloading || !Weapon || (GetWorld() && GetWorld()->IsPaused())) return;
    if (Weapon->UsesAmmunition() && CurrentAmmo <= 0)
    {
        StartReloadFromAutomation();
        return;
    }
    bTriggerHeld = true;
    if(CharacterPresentation)CharacterPresentation->CancelCharacterGesture();
    Weapon->StartFiring();
}

void AHeliosArenaCharacter::StopFireFromAutomation()
{
    StopFire();
}

void AHeliosArenaCharacter::StartReloadFromAutomation()
{
    if(IsQingYaBusy())return;
    if(auto* W=Cast<AHeliosZhongXiaoKuiWeapon>(Weapon)){if(!bDead){if(CharacterPresentation)CharacterPresentation->CancelCharacterGesture();W->RequestSourceReload();}return;}
    if (bDead || bReloading || !Weapon || !Weapon->UsesAmmunition() || CurrentAmmo >= MagazineSize ||
        (GetWorld() && GetWorld()->IsPaused())) return;
    StopFire();
    bReloading = true;
    if(CharacterPresentation)CharacterPresentation->CancelCharacterGesture();
    Weapon->BeginReload(ReloadSeconds);
    if (Presentation) Presentation->BeginReload(ReloadSeconds, CurrentAmmo == 0);
    OnCombatCue(TEXT("Reload"));
    GetWorld()->GetTimerManager().SetTimer(ReloadTimer, this,
        &AHeliosArenaCharacter::FinishReload, ReloadSeconds, false);
}

void AHeliosArenaCharacter::JumpFromAutomation()
{
    if (!bDead && !IsMechMode() && !IsFighterMode() && !(GetWorld() && GetWorld()->IsPaused())) Jump();
}

void AHeliosArenaCharacter::CancelAutomationCombat()
{
    QingYaAbility->Cancel();
    if(CharacterPresentation)CharacterPresentation->CancelCharacterGesture();
    if (Presentation) Presentation->Cancel(false);
    StopFire();
    if(Weapon)Weapon->StopSecondaryFire();
    if (Weapon) Weapon->CancelReload();
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ReloadTimer);
    bReloading = false;
    StopJumping();
}

void AHeliosArenaCharacter::StartSpecialAttack()
{
    if(IsQingYaBusy())return;
    if(GuanXiaoYuAbility->IsUsingUmbrella())return;
    NotifyManualInput();
    if (!bDead && !bReloading && Weapon && !GetWorld()->IsPaused()) {if(CharacterPresentation)CharacterPresentation->CancelCharacterGesture();Weapon->StartSpecialFire();}
}

void AHeliosArenaCharacter::StartSecondaryAttack()
{
    if(IsQingYaBusy())return;
    if(GuanXiaoYuAbility->IsUsingUmbrella()){NotifyManualInput();GuanXiaoYuAbility->StartHeavyAttack();return;}
    NotifyManualInput();
    if (!bDead && !bReloading && Weapon && !GetWorld()->IsPaused()) {if(CharacterPresentation)CharacterPresentation->CancelCharacterGesture();Weapon->StartSecondaryFire();}
}
void AHeliosArenaCharacter::StopSecondaryAttack()
{
    if (!bDead && Weapon && !GetWorld()->IsPaused()) Weapon->StopSecondaryFire();
}
