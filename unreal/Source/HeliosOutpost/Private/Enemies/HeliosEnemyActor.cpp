#include "Enemies/HeliosEnemyActor.h"
#include "Combat/HeliosKillFeedbackSubsystem.h"
#include "Combat/CombatDamageSettings.h"
#include "Combat/HeliosCombatTempoSettings.h"
#include "Player/HeliosCharacter.h"
#include "Weapons/HeliosSoulBanner.h"
#include "Pets/HeliosPetDog.h"

#include "Weapons/HeliosBasketballProjectile.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Variant_Shooter/AI/ShooterAIController.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"

namespace
{
    constexpr float FriendlyHitReactionSeconds = 0.2f;
    constexpr double FriendlyHitReactionIntervalSeconds = 0.8;

    FVector CombatTargetGround(const AActor* Target)
    {
        const auto* P=Cast<AHeliosCharacter>(Target);
        return P&&P->IsMechMode()?P->GetGameplayReference():Target->GetActorLocation();
    }
    FVector CombatTargetAim(const AActor* Target,const FVector& From)
    {
        const auto* P=Cast<AHeliosCharacter>(Target);
        if(P&&P->IsMechMode())return P->GetGroundReference()+FVector(0,0,150)+(From-P->GetGroundReference()).GetSafeNormal2D()*210;
        return Target->GetActorLocation();
    }

    bool IsFriendlyAIDamageCauser(const AActor* Causer)
    {
        const auto IsFriendlyAI = [](const AActor* Actor)
        {
            return IsValid(Actor) && (Actor->ActorHasTag(TEXT("HeliosSummon")) ||
                Actor->ActorHasTag(TEXT("HeliosPet")));
        };
        return IsFriendlyAI(Causer) || (IsValid(Causer) && IsFriendlyAI(Causer->GetInstigator()));
    }

    ECreatureHealthType HealthTypeForEnemy(EHeliosEnemyType Type)
    {
        switch (Type)
        {
        case EHeliosEnemyType::E02_Charger: return ECreatureHealthType::Charger;
        case EHeliosEnemyType::E03_Skirmisher: return ECreatureHealthType::Skirmisher;
        case EHeliosEnemyType::E04_Basketball: return ECreatureHealthType::Basketball;
        case EHeliosEnemyType::E05_Boss: return ECreatureHealthType::Boss;
        default: return ECreatureHealthType::Rifle;
        }
    }
}

ECreatureHealthType AHeliosEnemyActor::GetHealthSettingsType() const
{
    return HealthTypeForEnemy(EnemyType);
}

FHeliosEnemyDefinition FHeliosEnemyDefinition::ForType(EHeliosEnemyType InType)
{
    FHeliosEnemyDefinition D;
    D.Type = InType;
    D.MaxHP = FCreatureHealthTable::Default(HealthTypeForEnemy(InType));
    switch (InType)
    {
    case EHeliosEnemyType::E02_Charger:
        D.TypeId = TEXT("E02"); D.CapsuleRadius = 42.0f; D.CapsuleHalfHeight = 68.0f;
        D.WalkSpeed = 240.0f; D.RunSpeed = 560.0f;
        D.WalkAnimationGroundSpeed = 95.0f; D.RunAnimationGroundSpeed = 230.0f;
        D.Damage = 24.0f; D.Range = 165.0f; D.Interval = 1.05f;
        D.AttackAnimationName = TEXT("Peck"); D.AttackAnimationDuration = 1.0f;
        D.DeathAnimationDuration = 1.5f;
        break;
    case EHeliosEnemyType::E03_Skirmisher:
        D.TypeId = TEXT("E03"); D.CapsuleRadius = 34.0f; D.CapsuleHalfHeight = 88.0f;
        D.WalkSpeed = 330.0f; D.RunSpeed = 610.0f;
        D.WalkAnimationGroundSpeed = 115.0f; D.RunAnimationGroundSpeed = 235.0f;
        D.Damage = 4.0f; D.Range = 1500.0f; D.Interval = 0.72f;
        D.BurstShotCount = 2; D.BurstShotInterval = 0.07f;
        break;
    case EHeliosEnemyType::E04_Basketball:
        D.TypeId = TEXT("E04"); D.CapsuleRadius = 40.0f; D.CapsuleHalfHeight = 93.0f;
        D.WalkSpeed = 230.0f; D.RunSpeed = 390.0f;
        D.WalkAnimationGroundSpeed = 95.0f; D.RunAnimationGroundSpeed = 315.0f;
        D.Damage = 22.0f; D.Range = 1450.0f; D.Interval = 1.65f;
        D.AttackAnimationName = TEXT("Throw"); D.AttackAnimationDuration = 1.4f;
        break;
    case EHeliosEnemyType::E05_Boss:
        D.TypeId = TEXT("E05"); D.CapsuleRadius = 44.0f; D.CapsuleHalfHeight = 125.0f;
        D.WalkSpeed = 360.0f; D.RunSpeed = 840.0f;
        D.WalkAnimationGroundSpeed = 120.0f; D.RunAnimationGroundSpeed = 280.0f;
        D.Damage = 18.0f; D.Range = 1400.0f; D.Interval = 3.45f;
        D.AttackAnimationName = TEXT("Charge"); D.AttackAnimationDuration = 2.25f;
        D.HitAnimationDuration = 0.18f; D.DeathAnimationDuration = 2.2f;
        break;
    case EHeliosEnemyType::E01_Rifle:
    default:
        D.TypeId = TEXT("E01"); D.CapsuleRadius = 34.0f; D.CapsuleHalfHeight = 80.0f;
        D.WalkSpeed = 270.0f; D.RunSpeed = 460.0f;
        D.WalkAnimationGroundSpeed = 120.0f; D.RunAnimationGroundSpeed = 275.0f;
        D.Damage = 5.0f; D.Range = 1900.0f; D.Interval = 1.0f;
        D.BurstShotCount = 3; D.BurstShotInterval = 0.11f;
        break;
    }
    D.MeshAssetPath = FString::Printf(TEXT("/Game/Helios/Enemies/%s/SK_%s.SK_%s"),
        *D.TypeId, *D.TypeId, *D.TypeId);
    if (InType == EHeliosEnemyType::E05_Boss)
        D.MeshAssetPath = TEXT("/Game/Helios/Enemies/E05WhiteSuit/SK_E05WhiteSuit.SK_E05WhiteSuit");
    return D;
}

AHeliosEnemyActor::AHeliosEnemyActor()
{
    PrimaryActorTick.bCanEverTick = true;
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
    bUseRagdollOnDeath = false;
    DeferredDestructionTime = 5.0f;
    Tags.AddUnique(TEXT("HeliosEnemy"));
    GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

    MuzzleFlashLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("MuzzleFlashLight"));
    MuzzleFlashLight->SetupAttachment(GetMesh());
    MuzzleFlashLight->SetRelativeLocation(FVector(28.0f, 0.0f, 0.0f));
    MuzzleFlashLight->SetLightColor(FLinearColor(1.0f, 0.48f, 0.12f));
    MuzzleFlashLight->SetAttenuationRadius(220.0f);
    MuzzleFlashLight->SetCastShadows(false);
    MuzzleFlashLight->SetIntensity(0.0f);

    static const TCHAR* ControllerPath =
        TEXT("/Game/Variant_Shooter/Blueprints/AI/BP_ShooterAIController.BP_ShooterAIController_C");
    if (UClass* ControllerBP = StaticLoadClass(AAIController::StaticClass(), nullptr, ControllerPath))
    {
        AIControllerClass = ControllerBP;
    }

    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f);
    bUseControllerRotationYaw = false;
    // The custom NPCs have no first-person arms. Give the template LOS camera a valid parent.
    GetFirstPersonCameraComponent()->SetupAttachment(GetCapsuleComponent());
    GetFirstPersonCameraComponent()->SetRelativeRotation(FRotator::ZeroRotator);
}

void AHeliosEnemyActor::BeginPlay()
{
    ApplyTypeDefaults();

    // Rifle-bearing types reuse the official template weapon asset for their visible prop.
    if (EnemyType == EHeliosEnemyType::E01_Rifle || EnemyType == EHeliosEnemyType::E03_Skirmisher)
    {
        const TCHAR* WeaponPath = EnemyType == EHeliosEnemyType::E03_Skirmisher
            ? TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_Pistol.BP_ShooterWeapon_Pistol_C")
            : TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_Rifle.BP_ShooterWeapon_Rifle_C");
        WeaponClass = LoadClass<AShooterWeapon>(nullptr, WeaponPath);
    }
    else
    {
        WeaponClass = nullptr;
    }

    Super::BeginPlay();
    LoadAnimationAssets();
    PreviousYaw = GetActorRotation().Yaw;
    PlayStateAnimation(EHeliosEnemyCombatState::Idle, IdleAnimation, true);
    GetWorldTimerManager().SetTimer(BlinkTimer, this, &AHeliosEnemyActor::TriggerBlink,
        FMath::FRandRange(2.0f, 4.5f), false);
}

void AHeliosEnemyActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    CancelRangedBurst();
    GetWorldTimerManager().ClearTimer(AttackCycleTimer);
    GetWorldTimerManager().ClearTimer(HitReactionTimer);
    GetWorldTimerManager().ClearTimer(BlinkTimer);
    GetWorldTimerManager().ClearTimer(BlinkEndTimer);
    if (IsValid(Weapon))
    {
        Weapon->StopFiring();
        Weapon->Destroy();
        Weapon = nullptr;
    }
    Super::EndPlay(EndPlayReason);
}

void AHeliosEnemyActor::ApplyTypeDefaults()
{
    Definition = FHeliosEnemyDefinition::ForType(EnemyType);
    const TCHAR* TempoSection = TEXT("步枪兵");
    switch (EnemyType)
    {
    case EHeliosEnemyType::E02_Charger: TempoSection = TEXT("冲锋兵"); break;
    case EHeliosEnemyType::E03_Skirmisher: TempoSection = TEXT("游击兵"); break;
    case EHeliosEnemyType::E04_Basketball: TempoSection = TEXT("篮球兵"); break;
    case EHeliosEnemyType::E05_Boss: TempoSection = TEXT("Boss"); break;
    default: break;
    }
    Definition.WalkSpeed = UHeliosCombatTempoSettings::Resolve(this, EHeliosTempoFile::Movement,
        TempoSection, TEXT("步行"), Definition.WalkSpeed);
    Definition.RunSpeed = UHeliosCombatTempoSettings::Resolve(this, EHeliosTempoFile::Movement,
        TempoSection, TEXT("追击"), Definition.RunSpeed);
    if (EnemyType != EHeliosEnemyType::E05_Boss)
    {
        Definition.Interval = UHeliosCombatTempoSettings::Resolve(this, EHeliosTempoFile::Attack,
            TempoSection, TEXT("攻击间隔"), Definition.Interval);
        if (EnemyType == EHeliosEnemyType::E01_Rifle || EnemyType == EHeliosEnemyType::E03_Skirmisher)
            Definition.BurstShotInterval = UHeliosCombatTempoSettings::Resolve(this,
                EHeliosTempoFile::Attack, TempoSection, TEXT("连发间隔"), Definition.BurstShotInterval);
    }
    Definition.MaxHP = UCreatureHealthSettings::Resolve(this, GetHealthSettingsType());
    EnemyTypeId = Definition.TypeId;
    CurrentHP = Definition.MaxHP;
    ECombatDamageType DamageType = ECombatDamageType::EnemyRifle;
    switch (Definition.Type)
    {
    case EHeliosEnemyType::E02_Charger: DamageType=ECombatDamageType::EnemyCharger; break;
    case EHeliosEnemyType::E03_Skirmisher: DamageType=ECombatDamageType::EnemySkirmisher; break;
    case EHeliosEnemyType::E04_Basketball: DamageType=ECombatDamageType::EnemyBasketball; break;
    case EHeliosEnemyType::E05_Boss: DamageType=ECombatDamageType::BossLegacy; break;
    default: break;
    }
    AttackDamage = Definition.Damage = UCombatDamageSettings::Resolve(this,DamageType);
    AttackRange = Definition.Range;
    AttackInterval = Definition.Interval;
    GetCapsuleComponent()->SetCapsuleSize(Definition.CapsuleRadius, Definition.CapsuleHalfHeight);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -Definition.CapsuleHalfHeight));
    GetMesh()->SetRelativeRotation(FRotator(0.f,-90.f,0.f));
    GetCharacterMovement()->MaxWalkSpeed = Definition.RunSpeed;
    // Keep the ordinary enemies on their established mesh; the taller Boss has a separate one.
    GetCharacterMovement()->SetUpdateNavAgentWithOwnersCollisions(false);
    GetCharacterMovement()->NavAgentProps.AgentRadius = EnemyType == EHeliosEnemyType::E05_Boss ? 44.0f : 38.0f;
    GetCharacterMovement()->NavAgentProps.AgentHeight = EnemyType == EHeliosEnemyType::E05_Boss ? 250.0f : 192.0f;
    GetCharacterMovement()->NavAgentProps.AgentStepHeight = 40.0f;
    GetFirstPersonCameraComponent()->SetRelativeLocation(FVector(0,0,Definition.CapsuleHalfHeight * .65f));

    if (USkeletalMesh* EnemyMesh = LoadObject<USkeletalMesh>(nullptr, *Definition.MeshAssetPath))
    {
        GetMesh()->SetSkeletalMeshAsset(EnemyMesh);
        if (EnemyType == EHeliosEnemyType::E05_Boss) GetMesh()->SetRelativeScale3D(FVector(250.f / 188.f));
        MuzzleFlashLight->AttachToComponent(GetMesh(),FAttachmentTransformRules::KeepRelativeTransform,TEXT("HandGrip_R"));
    }
}

void AHeliosEnemyActor::AttachWeaponMeshes(AShooterWeapon* WeaponToAttach)
{
    if (!IsValid(WeaponToAttach)) return;
    WeaponToAttach->AttachToActor(this,FAttachmentTransformRules::SnapToTargetNotIncludingScale);
    WeaponToAttach->GetFirstPersonMesh()->AttachToComponent(GetCapsuleComponent(),
        FAttachmentTransformRules::SnapToTargetNotIncludingScale);
    WeaponToAttach->GetFirstPersonMesh()->SetHiddenInGame(true);
    WeaponToAttach->GetThirdPersonMesh()->AttachToComponent(GetMesh(),
        FAttachmentTransformRules::SnapToTargetNotIncludingScale,TEXT("HandGrip_R"));
}

UAnimSequence* AHeliosEnemyActor::LoadEnemyAnimation(const TCHAR* Name, bool bPreferRetargeted) const
{
    const FString AssetName = FString::Printf(TEXT("A_%s_%s"), *EnemyTypeId, Name);
    if (bPreferRetargeted)
    {
        const FString Retargeted = FString::Printf(
            TEXT("/Game/Helios/Enemies/%s/Animations/Retargeted/%s.%s"),
            *EnemyTypeId, *AssetName, *AssetName);
        if (UAnimSequence* Result = LoadObject<UAnimSequence>(nullptr, *Retargeted))
        {
            return Result;
        }
    }
    const FString Standard = FString::Printf(TEXT("/Game/Helios/Enemies/%s/Animations/%s.%s"),
        *EnemyTypeId, *AssetName, *AssetName);
    return LoadObject<UAnimSequence>(nullptr, *Standard);
}

void AHeliosEnemyActor::LoadAnimationAssets()
{
    const bool bRetargetLocomotion = EnemyType == EHeliosEnemyType::E01_Rifle ||
        EnemyType == EHeliosEnemyType::E03_Skirmisher;
    IdleAnimation = LoadEnemyAnimation(
        EnemyType == EHeliosEnemyType::E04_Basketball ? TEXT("SpinBall") : TEXT("Idle"),
        bRetargetLocomotion);
    WalkAnimation = LoadEnemyAnimation(TEXT("Walk"), bRetargetLocomotion || EnemyType == EHeliosEnemyType::E04_Basketball);
    // E02 Charge remains an authored anticipation clip; locomotion uses the measured Run cycle.
    RunAnimation = LoadEnemyAnimation(TEXT("Run"),
        bRetargetLocomotion || EnemyType == EHeliosEnemyType::E04_Basketball);
    TurnAnimation = LoadEnemyAnimation(TEXT("Turn"), false);
    AttackAnimation = LoadEnemyAnimation(*Definition.AttackAnimationName, false);
    HitAnimation = LoadEnemyAnimation(TEXT("Hit"), false);
    DeathAnimation = LoadEnemyAnimation(TEXT("Death"), false);
    GetMesh()->SetAnimationMode(EAnimationMode::AnimationSingleNode);
}

void AHeliosEnemyActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    TacticalMoveCooldown = FMath::Max(0.0f, TacticalMoveCooldown - DeltaSeconds);
    if (!bIsDead && UsesTacticalCombat() && bIsShooting)
    {
        if (!IsLivingCombatTarget(CurrentAimTarget)) StopShooting();
        else UpdateTacticalMovement();
    }
    if (!bIsDead && !bHitReactionPlaying &&
        (!bIsShooting || (UsesTacticalCombat() && !bAttackCycleActive)))
    {
        RefreshLocomotionAnimation(DeltaSeconds);
    }
    if (!bIsDead && !bHitReactionPlaying)
    {
        // E02 can remain in Run while the combat StateTree is actively closing distance.
        UpdateLocomotionPlayRate(DeltaSeconds);
    }
    PreviousYaw = GetActorRotation().Yaw;
}

void AHeliosEnemyActor::UpdateTacticalMovement()
{
    AAIController* AI = Cast<AAIController>(GetController());
    if (!UsesTacticalCombat() || !AI || !IsLivingCombatTarget(CurrentAimTarget) || bHitReactionPlaying) return;
    const double Now = GetWorld()->GetTimeSeconds();
    AI->SetFocus(CurrentAimTarget);
    GetCharacterMovement()->bOrientRotationToMovement = false;
    GetCharacterMovement()->bUseControllerDesiredRotation = true;

    // Never replace an authored windup or a burst with locomotion. Ranged units
    // can use their recovery to step away once the actual shots have finished.
    if (bAttackCycleActive)
    {
        if (EnemyType == EHeliosEnemyType::E02_Charger && TacticalMoveCooldown <= 0.f)
        {
            // The peck socket moves during its clip. Continue closing the
            // capsule gap instead of freezing at the windup socket's reach.
            RequestTacticalMove(CombatTargetGround(CurrentAimTarget),AttackRange*.30f);
            TacticalMoveCooldown = .45f;
        }
        if (EnemyType != EHeliosEnemyType::E02_Charger && EnemyType != EHeliosEnemyType::E03_Skirmisher
            && bAttackCommittedThisCycle && !bBurstActive && !bSkirmishRepositioning && TacticalMoveCooldown <= 0.f)
            BeginTacticalStep(false);
        return;
    }
    if (bSkirmishRepositioning)
    {
        if (Now < TacticalStepEndsAt && AI->GetMoveStatus() == EPathFollowingStatus::Moving) return;
        StopTacticalMovement();
    }

    FVector Impact;
    const bool bCloseEnough = EnemyType != EHeliosEnemyType::E02_Charger ||
        FVector::DistSquared2D(GetActorLocation(),CurrentAimTarget->GetActorLocation()) <= FMath::Square(AttackRange*.9f);
    const bool bClearShot = bCloseEnough && HasClearAttackLine(CurrentAimTarget, Impact);
    if (bClearShot)
    {
        StopTacticalMovement();
        if (Now >= NextAttackAllowedAt && CanStartTacticalAttack(CurrentAimTarget)) BeginAttackCycle();
        return;
    }
    if (TacticalMoveCooldown > 0.f) return;

    if (EnemyType != EHeliosEnemyType::E02_Charger && BeginTacticalStep(false)) return;
    // MoveToActor adds both capsule radii to acceptance. MoveToLocation with
    // overlap disabled keeps the stop distance inside the *actual* attack range.
    const FVector Goal = CombatTargetGround(CurrentAimTarget);
    const float Acceptance = EnemyType == EHeliosEnemyType::E02_Charger ? AttackRange * .30f : 35.f;
    RequestTacticalMove(Goal, Acceptance);
    TacticalMoveCooldown = .45f;
}

bool AHeliosEnemyActor::IsLivingCombatTarget(const AActor* Target) const
{
    return IsValid(Target) && !Target->IsActorBeingDestroyed() && Target != this
        && !Target->ActorHasTag(TEXT("Dead")) && !Cast<AShooterNPC>(Target);
}

bool AHeliosEnemyActor::CanStartTacticalAttack(AActor* Target) const
{
    FVector Impact;
    if (!IsLivingCombatTarget(Target) || !HasClearAttackLine(Target, Impact)) return false;
    // A ground charger's yaw cannot correct the height difference between two
    // overlapping capsules. Test its horizontal facing, with the real 3D range
    // and obstruction still enforced above, rather than rejecting a taller foe.
    const FVector Offset = CombatTargetAim(Target,GetActorLocation())-GetActorLocation();
    if (EnemyType == EHeliosEnemyType::E02_Charger && Offset.SizeSquared2D() <= KINDA_SMALL_NUMBER) return true;
    const FVector Direction = EnemyType == EHeliosEnemyType::E02_Charger ? Offset.GetSafeNormal2D() : Offset.GetSafeNormal();
    return FVector::DotProduct(GetActorForwardVector(), Direction)
        > FMath::Cos(FMath::DegreesToRadians(35.f));
}

void AHeliosEnemyActor::StopTacticalMovement()
{
    if (bTacticalMoveRequested)
        if (AAIController* AI = Cast<AAIController>(GetController())) AI->StopMovement();
    bTacticalMoveRequested = false;
    bSkirmishRepositioning = false;
}

bool AHeliosEnemyActor::RequestTacticalMove(const FVector& Goal, float Acceptance)
{
    auto* AI = Cast<AAIController>(GetController());
    if (!AI) return false;
    if (bTacticalMoveRequested && AI->GetMoveStatus() == EPathFollowingStatus::Moving
        && FVector::DistSquared2D(Goal, LastTacticalGoal) <= FMath::Square(GetCapsuleComponent()->GetScaledCapsuleRadius()))
        return true;
    LastTacticalGoal = Goal;
    bTacticalMoveRequested = AI->MoveToLocation(Goal, Acceptance, false, true, true, false, nullptr, false)
        == EPathFollowingRequestResult::RequestSuccessful;
    return bTacticalMoveRequested;
}

bool AHeliosEnemyActor::FindTacticalStep(AActor* Target, bool bRetreat, FVector& OutGoal) const
{
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!Nav || !IsLivingCombatTarget(Target)) return false;
    const FVector Away = (GetActorLocation()-Target->GetActorLocation()).GetSafeNormal2D();
    const FVector Side = FVector::CrossProduct(FVector::UpVector,Away) * (((AttackCount/2)%2) ? -1.f : 1.f);
    const FVector Base = bRetreat ? Away : Side;
    const float Radius = GetCapsuleComponent()->GetScaledCapsuleRadius();
    const float Height = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FVector Muzzle = GetMesh()->DoesSocketExist(TEXT("HandGrip_R"))
        ? GetMesh()->GetSocketLocation(TEXT("HandGrip_R")) : GetActorLocation()+FVector(0,0,BaseEyeHeight);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(HeliosTacticalStep),false,this);
    if (IsValid(Weapon)) Params.AddIgnoredActor(Weapon);
    // Reuse E03's established step length; evaluate both sides before giving up.
    for (float Angle : {0.f, 45.f, -45.f, 90.f, -90.f, 180.f})
    {
        FNavLocation Point;
        if (!Nav->ProjectPointToNavigation(GetActorLocation()+Base.RotateAngleAxis(Angle,FVector::UpVector)*260.f,
            Point,FVector(Radius,Radius,Height*2))) continue;
        const FVector Center = Point.Location+FVector(0,0,Height);
        if (FVector::DistSquared2D(Center,GetActorLocation()) < FMath::Square(Radius)) continue;
        if (bRetreat && FVector::DistSquared2D(Center,Target->GetActorLocation())
            <= FVector::DistSquared2D(GetActorLocation(),Target->GetActorLocation())) continue;
        if (GetWorld()->OverlapBlockingTestByChannel(Center,FQuat::Identity,ECC_Pawn,
            FCollisionShape::MakeCapsule(Radius,Height),Params)) continue;
        FVector Impact;
        if (!HasClearAttackLineFrom(Target,Muzzle+Center-GetActorLocation(),Impact)) continue;
        auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),GetActorLocation(),Point.Location,
            const_cast<AHeliosEnemyActor*>(this));
        if (!Path || !Path->IsValid() || Path->IsPartial()) continue;
        OutGoal = Point.Location; return true;
    }
    return false;
}

bool AHeliosEnemyActor::BeginTacticalStep(bool bAlwaysReposition)
{
    if (!IsLivingCombatTarget(CurrentAimTarget)) return false;
    FVector Impact, Goal;
    const bool bClear = HasClearAttackLine(CurrentAimTarget, Impact);
    const bool bClose = FVector::DistSquared2D(GetActorLocation(),CurrentAimTarget->GetActorLocation())
        < FMath::Square(FMath::Max(AwarenessSettings.CloseAwarenessDistance,AttackRange*.45f));
    TacticalMoveCooldown = .45f;
    if (!bAlwaysReposition && bClear && !bClose) return false;
    if (!FindTacticalStep(CurrentAimTarget,bClose,Goal) || !RequestTacticalMove(Goal,35.f)) return false;
    bSkirmishRepositioning = true;
    TacticalStepEndsAt = GetWorld()->GetTimeSeconds()+.45;
    if (!bAttackCycleActive) PlayStateAnimation(EHeliosEnemyCombatState::Run,RunAnimation,true);
    return true;
}

void AHeliosEnemyActor::TriggerBlink()
{
    if (bIsDead)
    {
        return;
    }
    GetMesh()->SetMorphTarget(TEXT("Blink"), 1.0f);
    GetWorldTimerManager().SetTimer(BlinkEndTimer, this, &AHeliosEnemyActor::ClearBlink, 0.12f, false);
    GetWorldTimerManager().SetTimer(BlinkTimer, this, &AHeliosEnemyActor::TriggerBlink,
        FMath::FRandRange(2.0f, 4.5f), false);
}

void AHeliosEnemyActor::ClearBlink()
{
    GetMesh()->SetMorphTarget(TEXT("Blink"), 0.0f);
}

void AHeliosEnemyActor::RefreshLocomotionAnimation(float DeltaSeconds)
{
    const float Speed = GetVelocity().Size2D();
    if (Speed > Definition.WalkSpeed + 35.0f)
    {
        PlayStateAnimation(EHeliosEnemyCombatState::Run, RunAnimation, true);
    }
    else if (Speed > 8.0f)
    {
        PlayStateAnimation(EHeliosEnemyCombatState::Walk, WalkAnimation, true);
    }
    else if (DeltaSeconds > KINDA_SMALL_NUMBER &&
        FMath::Abs(FMath::FindDeltaAngleDegrees(PreviousYaw, GetActorRotation().Yaw)) / DeltaSeconds > 35.0f)
    {
        PlayStateAnimation(EHeliosEnemyCombatState::Turn, TurnAnimation, true);
    }
    else
    {
        PlayStateAnimation(EHeliosEnemyCombatState::Idle, IdleAnimation, true);
    }
}

void AHeliosEnemyActor::UpdateLocomotionPlayRate(float DeltaSeconds)
{
    float ReferenceSpeed = 0.0f;
    UAnimSequence* LocomotionSequence = nullptr;
    if (CombatState == EHeliosEnemyCombatState::Walk)
    {
        ReferenceSpeed = Definition.WalkAnimationGroundSpeed;
        LocomotionSequence = WalkAnimation;
    }
    else if (CombatState == EHeliosEnemyCombatState::Run)
    {
        ReferenceSpeed = Definition.RunAnimationGroundSpeed;
        LocomotionSequence = RunAnimation;
    }
    else
    {
        SmoothedLocomotionPlayRate = 1.0f;
        return;
    }

    if (!LocomotionSequence || ReferenceSpeed <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    const float HorizontalSpeed = GetVelocity().Size2D();
    constexpr float MinLocomotionPlayRate = 0.35f;
    constexpr float MaxLocomotionPlayRate = 2.75f;
    constexpr float PlayRateInterpSpeed = 8.0f;
    const float TargetPlayRate = FMath::Clamp(
        HorizontalSpeed / ReferenceSpeed, MinLocomotionPlayRate, MaxLocomotionPlayRate);
    SmoothedLocomotionPlayRate = FMath::FInterpTo(
        SmoothedLocomotionPlayRate, TargetPlayRate, DeltaSeconds, PlayRateInterpSpeed);
    GetMesh()->SetPlayRate(SmoothedLocomotionPlayRate);
}

void AHeliosEnemyActor::PlayStateAnimation(EHeliosEnemyCombatState NewState, UAnimSequence* Sequence, bool bLoop)
{
    if (CombatState == NewState && (!Sequence || GetMesh()->IsPlaying()))
    {
        return;
    }
    CombatState = NewState;
    if (NewState != EHeliosEnemyCombatState::Walk && NewState != EHeliosEnemyCombatState::Run)
    {
        SmoothedLocomotionPlayRate = 1.0f;
    }
    if (!Sequence)
    {
        return;
    }
    GetMesh()->SetPlayRate(1.0f);
    GetMesh()->PlayAnimation(Sequence, bLoop);
}

float AHeliosEnemyActor::TakeDamage(float Damage, const FDamageEvent& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser)
{
    const APawn* InstigatorPawn = EventInstigator ? EventInstigator->GetPawn() : nullptr;
    if (Cast<AShooterNPC>(DamageCauser) || Cast<AShooterNPC>(InstigatorPawn))
    {
        return 0.0f;
    }

    const bool bWasAlive = !bIsDead;
    const float Applied = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);
    if (bWasAlive && bIsDead && Applied > 0.f)
        if (auto* Feedback = GetWorld()->GetSubsystem<UHeliosKillFeedbackSubsystem>())
            Feedback->ReportKill(this, EventInstigator, DamageCauser);
    if (Applied > 0.f)
        if (auto* Pets = GetWorld()->GetSubsystem<UHeliosPetWorld>())
            Pets->ObserveDamage(this, Applied, nullptr, EventInstigator, DamageCauser);
    if(Applied>0.f&&EventInstigator)
        if(auto* Army=UHeliosSoulArmy::Get(EventInstigator->GetPawn(),false))Army->ObservePlayerDamage(this,EventInstigator,DamageCauser);
    if (Applied > 0.0f && !bIsDead)
    {
        if (GetAwarenessSettings())
        {
            APawn* Source = nullptr;
            // Summons often use their owner's controller to attribute damage.
            // Retaliation must remember the actual attacker, not its player owner.
            if (UsesTacticalCombat()) Source = Cast<APawn>(DamageCauser);
            if (!Source && EventInstigator) Source = EventInstigator->GetPawn();
            if (!Source && IsValid(DamageCauser)) Source = DamageCauser->GetInstigator();
            if (IsValid(Source))
                if (AShooterAIController* AI = Cast<AShooterAIController>(GetController()))
                    AI->ReportAwarenessThreat(Source, Source->GetActorLocation(), true);
        }
        // Keep a short visual response for every enemy.  Bosses deliberately do
        // not enter Hit or cancel their current ability when damaged.
        GetMesh()->SetMorphTarget(TEXT("HitFace"), 1.0f);
        if (UsesTacticalCombat())
        {
            if (bHitReactionPlaying) return Applied;
            if (IsFriendlyAIDamageCauser(DamageCauser))
            {
                const double Now = GetWorld()->GetTimeSeconds();
                if (!bAttackCycleActive && !bBurstActive && Now >= NextFriendlyHitReactionAllowedAt)
                {
                    GetWorldTimerManager().SetTimer(HitReactionTimer, this,
                        &AHeliosEnemyActor::FinishHitReaction, FriendlyHitReactionSeconds, false);
                    if (EnemyType != EHeliosEnemyType::E02_Charger) StopTacticalMovement();
                    bHitReactionPlaying = true;
                    NextFriendlyHitReactionAllowedAt = Now + FriendlyHitReactionIntervalSeconds;
                    PlayStateAnimation(EHeliosEnemyCombatState::Hit, HitAnimation, false);
                }
                else if (!GetWorldTimerManager().IsTimerActive(HitReactionTimer))
                {
                    // Keep facial feedback without restarting an attack or extending a reaction.
                    GetWorldTimerManager().SetTimer(HitReactionTimer, this,
                        &AHeliosEnemyActor::FinishHitReaction, FriendlyHitReactionSeconds, false);
                }
                return Applied;
            }
            // Player hits keep their authored reaction. An existing reaction is not extended.
            GetWorldTimerManager().SetTimer(HitReactionTimer, this,
                &AHeliosEnemyActor::FinishHitReaction, Definition.HitAnimationDuration, false);
            if (bAttackCycleActive || bBurstActive) return Applied;
            // A charger's existing chase also continued through hit feedback in
            // the original implementation. Preserve that movement, not stun-lock it.
            if (EnemyType != EHeliosEnemyType::E02_Charger) StopTacticalMovement();
            bHitReactionPlaying = true;
            PlayStateAnimation(EHeliosEnemyCombatState::Hit, HitAnimation, false);
            return Applied;
        }
        GetWorldTimerManager().ClearTimer(HitReactionTimer);
        GetWorldTimerManager().SetTimer(HitReactionTimer, this,
            &AHeliosEnemyActor::FinishHitReaction, Definition.HitAnimationDuration, false);
        if (!ShouldInterruptOnHitReaction())
        {
            return Applied;
        }
        CancelRangedBurst();
        if (bSkirmishRepositioning)
        {
            bSkirmishRepositioning = false;
            if (AAIController* AI = Cast<AAIController>(GetController())) AI->StopMovement();
        }
        bHitReactionPlaying = true;
        bAttackCommittedThisCycle = true;
        GetWorldTimerManager().ClearTimer(AttackCycleTimer);
        PlayStateAnimation(EHeliosEnemyCombatState::Hit, HitAnimation, false);
    }
    return Applied;
}

void AHeliosEnemyActor::FinishHitReaction()
{
    if (bIsDead)
    {
        return;
    }
    const bool bWasHitReaction = bHitReactionPlaying;
    bHitReactionPlaying = false;
    GetMesh()->SetMorphTarget(TEXT("HitFace"), 0.0f);
    if (bWasHitReaction && bIsShooting && IsValid(CurrentAimTarget))
    {
        if (UsesTacticalCombat()) UpdateTacticalMovement();
        else BeginAttackCycle();
    }
}

void AHeliosEnemyActor::StartShooting(AActor* ActorToShoot)
{
    if (bIsDead || !IsValid(ActorToShoot) || ActorToShoot == this)
    {
        return;
    }
    if (UsesTacticalCombat())
    {
        if (!IsLivingCombatTarget(ActorToShoot)) return;
        // Finish this clip against its locked target; the next cycle can switch.
        if (bAttackCycleActive && IsLivingCombatTarget(AttackCycleTarget.Get())) return;
        if (CurrentAimTarget != ActorToShoot) StopTacticalMovement();
        CurrentAimTarget = ActorToShoot;
        bIsShooting = true;
        UpdateTacticalMovement();
        return;
    }
    CurrentAimTarget = ActorToShoot;
    GetCharacterMovement()->bOrientRotationToMovement=false;
    GetCharacterMovement()->bUseControllerDesiredRotation=true;
    if (AAIController* AI=Cast<AAIController>(GetController())) AI->SetFocus(ActorToShoot);
    const bool bWasShooting = bIsShooting;
    bIsShooting = true;
    const bool bChargerMustClose = EnemyType == EHeliosEnemyType::E02_Charger &&
        FVector::Dist2D(GetActorLocation(), ActorToShoot->GetActorLocation()) > AttackRange * 0.9f;
    if (!bWasShooting && !bHitReactionPlaying && !bChargerMustClose)
    {
        BeginAttackCycle();
    }
    UpdateTacticalMovement();
}

void AHeliosEnemyActor::StopShooting()
{
    if (UsesTacticalCombat())
    {
        StopTacticalMovement();
        bAttackCycleActive = false;
        AttackCycleTarget.Reset();
        // NextAttackAllowedAt intentionally survives task exit and re-entry.
    }
    CancelRangedBurst();
    if (bSkirmishRepositioning)
    {
        bSkirmishRepositioning = false;
        if (AAIController* MovingAI = Cast<AAIController>(GetController())) MovingAI->StopMovement();
    }
    const AAIController* AI=Cast<AAIController>(GetController());
    const bool bHasActiveFocus=AI && IsValid(AI->GetFocusActor());
    GetCharacterMovement()->bOrientRotationToMovement=!bHasActiveFocus;
    GetCharacterMovement()->bUseControllerDesiredRotation=bHasActiveFocus;
    bIsShooting = false;
    CurrentAimTarget = nullptr;
    bAttackCommittedThisCycle = false;
    GetWorldTimerManager().ClearTimer(AttackCycleTimer);
    GetMesh()->SetMorphTarget(TEXT("AttackFace"), 0.0f);
    if (EnemyType == EHeliosEnemyType::E04_Basketball)
    {
        GetMesh()->UnHideBoneByName(TEXT("prop_ball"));
    }
}

void AHeliosEnemyActor::BeginAttackCycle()
{
    if (bIsDead || !bIsShooting || !IsValid(CurrentAimTarget))
    {
        StopShooting();
        return;
    }
    if (UsesTacticalCombat())
    {
        if (bHitReactionPlaying || bAttackCycleActive || bSkirmishRepositioning
            || GetWorld()->GetTimeSeconds()+KINDA_SMALL_NUMBER < NextAttackAllowedAt
            || !CanStartTacticalAttack(CurrentAimTarget)) return;
        StopTacticalMovement();
        if (AAIController* AI = Cast<AAIController>(GetController())) AI->StopMovement();
        bAttackCycleActive = true;
        AttackCycleTarget = CurrentAimTarget;
        NextAttackAllowedAt = GetWorld()->GetTimeSeconds()+AttackInterval;
    }
    CancelRangedBurst();
    bAttackCommittedThisCycle = false;
    bSkirmishRepositioning = false;
    if (EnemyType == EHeliosEnemyType::E03_Skirmisher)
    {
        if (AAIController* AI = Cast<AAIController>(GetController())) AI->StopMovement();
        GetCharacterMovement()->StopMovementImmediately();
        GetCharacterMovement()->bOrientRotationToMovement = false;
        GetCharacterMovement()->bUseControllerDesiredRotation = true;
    }
    ++AttackCount;
    if (EnemyType == EHeliosEnemyType::E04_Basketball)
    {
        GetMesh()->UnHideBoneByName(TEXT("prop_ball"));
    }
    GetMesh()->SetMorphTarget(TEXT("AttackFace"), 1.0f);
    // Force every cycle to restart the single-node clip so its notify fires once again.
    CombatState = EHeliosEnemyCombatState::Idle;
    PlayStateAnimation(EHeliosEnemyCombatState::Attack, AttackAnimation, false);
    if (AttackAnimation && AttackInterval > KINDA_SMALL_NUMBER)
    {
        GetMesh()->SetPlayRate(AttackAnimation->GetPlayLength() / AttackInterval);
    }
    GetWorldTimerManager().SetTimer(AttackCycleTimer, this,
        &AHeliosEnemyActor::FinishAttackCycle, AttackInterval, false);
}

void AHeliosEnemyActor::FinishAttackCycle()
{
    CancelRangedBurst();
    GetMesh()->SetMorphTarget(TEXT("AttackFace"), 0.0f);
    if (EnemyType == EHeliosEnemyType::E04_Basketball)
    {
        GetMesh()->UnHideBoneByName(TEXT("prop_ball"));
    }
    if (UsesTacticalCombat())
    {
        bAttackCycleActive = false;
        AttackCycleTarget.Reset();
        if (bIsDead || !bIsShooting) return;
        if (auto* AI = Cast<AShooterAIController>(GetController()))
            if (IsLivingCombatTarget(AI->GetCurrentTarget())) CurrentAimTarget = AI->GetCurrentTarget();
        if (!IsLivingCombatTarget(CurrentAimTarget)) { StopShooting(); return; }
        if (EnemyType == EHeliosEnemyType::E03_Skirmisher && BeginTacticalStep(true))
        {
            // Preserve the existing E03 recovery step even if the path ends early.
            NextAttackAllowedAt = FMath::Max(NextAttackAllowedAt,TacticalStepEndsAt);
            return;
        }
        UpdateTacticalMovement();
        return;
    }
    if (!bIsDead && bIsShooting && IsValid(CurrentAimTarget))
    {
        // Complete the burst before a short animated reposition. Reuse the
        // attack timer so hit, stop and death cancel both phases together.
        if (EnemyType == EHeliosEnemyType::E03_Skirmisher && !bSkirmishRepositioning)
        {
            if (AAIController* AI = Cast<AAIController>(GetController()))
            {
                const FVector ToTarget = (CurrentAimTarget->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
                const float SideSign = ((AttackCount / 2) % 2 == 0) ? 1.0f : -1.0f;
                const FVector Side = FVector::CrossProduct(FVector::UpVector, ToTarget) * SideSign;
                const auto MoveResult = AI->MoveToLocation(GetActorLocation() + Side * 260.0f,
                    35.0f, true, true, true, false, nullptr, true);
                if (MoveResult != EPathFollowingRequestResult::Failed)
                {
                    bSkirmishRepositioning = true;
                    GetCharacterMovement()->bOrientRotationToMovement = true;
                    GetCharacterMovement()->bUseControllerDesiredRotation = false;
                    PlayStateAnimation(EHeliosEnemyCombatState::Run, RunAnimation, true);
                    GetWorldTimerManager().SetTimer(AttackCycleTimer, this,
                        &AHeliosEnemyActor::FinishAttackCycle, 0.45f, false);
                    return;
                }
            }
        }
        BeginAttackCycle();
    }
}

bool AHeliosEnemyActor::HasClearAttackLine(AActor* Target, FVector& OutImpactPoint) const
{
    if (!IsLivingCombatTarget(Target))
    {
        return false;
    }
    const FVector Start = GetMesh()->DoesSocketExist(TEXT("HandGrip_R"))
        ? GetMesh()->GetSocketLocation(TEXT("HandGrip_R"))
        : GetActorLocation() + FVector(0.0f, 0.0f, BaseEyeHeight);
    // A socket on the far side of a wall must not shoot through it.
    if (UsesTacticalCombat())
    {
        FCollisionQueryParams Guard(SCENE_QUERY_STAT(HeliosMuzzleGuard),true,this);
        if (IsValid(Weapon)) Guard.AddIgnoredActor(Weapon);
        FHitResult Hit;
        if (GetWorld()->LineTraceSingleByChannel(Hit,GetActorLocation(),Start,ECC_Visibility,Guard)
            && Hit.GetActor() != Target) return false;
    }
    return HasClearAttackLineFrom(Target,Start,OutImpactPoint);
}

bool AHeliosEnemyActor::HasClearAttackLineFrom(AActor* Target, const FVector& Start, FVector& OutImpactPoint) const
{
    if (!IsLivingCombatTarget(Target)) return false;
    const FVector End = CombatTargetAim(Target,Start);
    if (FVector::DistSquared(Start, End) > FMath::Square(AttackRange))
    {
        return false;
    }
    FCollisionQueryParams Params(SCENE_QUERY_STAT(HeliosEnemyAttack), true, this);
    Params.AddIgnoredActor(this);
    if (UsesTacticalCombat() && IsValid(Weapon)) Params.AddIgnoredActor(Weapon);
    FHitResult Hit;
    if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
    {
        return false;
    }
    OutImpactPoint = Hit.ImpactPoint;
    return Hit.GetActor() == Target;
}

void AHeliosEnemyActor::HandleAttackNotify()
{
    ++AttackNotifyCount;
    if (bIsDead || !bIsShooting || bHitReactionPlaying || bAttackCommittedThisCycle || !IsValid(CurrentAimTarget))
    {
        return;
    }
    if (UsesTacticalCombat() && (!bAttackCycleActive || !IsLivingCombatTarget(AttackCycleTarget.Get()))) return;
    bAttackCommittedThisCycle = true;
    AActor* Target = UsesTacticalCombat() ? AttackCycleTarget.Get() : CurrentAimTarget.Get();
    if (EnemyType == EHeliosEnemyType::E04_Basketball)
    {
        ThrowBasketball(Target);
    }
    else
    {
        PerformHitscanOrMeleeAttack(Target);
    }
}

void AHeliosEnemyActor::PerformHitscanOrMeleeAttack(AActor* Target)
{
    if (EnemyType == EHeliosEnemyType::E01_Rifle || EnemyType == EHeliosEnemyType::E03_Skirmisher)
    {
        StartRangedBurst(Target);
        return;
    }

    FVector ImpactPoint;
    if (!Cast<AShooterNPC>(Target) && HasClearAttackLine(Target, ImpactPoint))
    {
        UGameplayStatics::ApplyDamage(Target, AttackDamage, GetController(), this, UDamageType::StaticClass());
        ++AttacksCommitted;
    }
}

void AHeliosEnemyActor::StartRangedBurst(AActor* Target)
{
    if (bBurstActive || bIsDead || !bIsShooting || !IsLivingCombatTarget(Target))
    {
        return;
    }
    BurstTarget = Target;
    BurstShotsRemaining = FMath::Clamp(Definition.BurstShotCount, 2, 3);
    bBurstActive = true;

    // The authored notify is the muzzle event: fire the first shot now, then finish the short burst by timer.
    FireBurstShot();
    if (bBurstActive)
    {
        GetWorldTimerManager().SetTimer(BurstShotTimer, this,
            &AHeliosEnemyActor::FireBurstShot, Definition.BurstShotInterval, true);
    }
}

void AHeliosEnemyActor::FireBurstShot()
{
    if (!bBurstActive || BurstShotsRemaining <= 0 || bIsDead || !bIsShooting ||
        bHitReactionPlaying || !IsLivingCombatTarget(BurstTarget.Get()))
    {
        CancelRangedBurst();
        return;
    }

    MuzzleFlashLight->SetIntensity(6500.0f);
    GetWorldTimerManager().ClearTimer(MuzzleFlashTimer);
    GetWorldTimerManager().SetTimer(MuzzleFlashTimer, this,
        &AHeliosEnemyActor::TurnOffMuzzleFlash, 0.04f, false);

    FVector ImpactPoint;
    if (!Cast<AShooterNPC>(BurstTarget.Get()) && HasClearAttackLine(BurstTarget.Get(), ImpactPoint))
    {
        UGameplayStatics::ApplyDamage(BurstTarget.Get(), AttackDamage,
            GetController(), this, UDamageType::StaticClass());
        ++AttacksCommitted;
    }

    --BurstShotsRemaining;
    if (BurstShotsRemaining <= 0)
    {
        CancelRangedBurst();
    }
}

void AHeliosEnemyActor::CancelRangedBurst()
{
    GetWorldTimerManager().ClearTimer(BurstShotTimer);
    GetWorldTimerManager().ClearTimer(MuzzleFlashTimer);
    TurnOffMuzzleFlash();
    BurstTarget.Reset();
    BurstShotsRemaining = 0;
    bBurstActive = false;
}

void AHeliosEnemyActor::TurnOffMuzzleFlash()
{
    if (MuzzleFlashLight)
    {
        MuzzleFlashLight->SetIntensity(0.0f);
    }
}

void AHeliosEnemyActor::ThrowBasketball(AActor* Target)
{
    FVector ImpactPoint;
    if (!HasClearAttackLine(Target, ImpactPoint))
    {
        return;
    }
    const FVector SpawnLocation = GetMesh()->DoesSocketExist(TEXT("BallSocket"))
        ? GetMesh()->GetSocketLocation(TEXT("BallSocket"))
        : GetActorLocation() + FVector(0.0f, 0.0f, BaseEyeHeight);
    FActorSpawnParameters Params;
    Params.Owner = this;
    Params.Instigator = this;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    TSubclassOf<AHeliosBasketballProjectile> ClassToSpawn = BasketballProjectileClass;
    if (!ClassToSpawn)
    {
        ClassToSpawn = AHeliosBasketballProjectile::StaticClass();
    }
    if (AHeliosBasketballProjectile* Ball = GetWorld()->SpawnActor<AHeliosBasketballProjectile>(
        ClassToSpawn, SpawnLocation, FRotator::ZeroRotator, Params))
    {
        GetMesh()->HideBoneByName(TEXT("prop_ball"), EPhysBodyOp::PBO_None);
        Ball->LaunchAt(Target, AttackDamage);
        ++AttacksCommitted;
    }
}

void AHeliosEnemyActor::Die()
{
    if (bIsDead)
    {
        return;
    }
    StopShooting();
    bHitReactionPlaying = false;
    GetWorldTimerManager().ClearTimer(HitReactionTimer);
    GetWorldTimerManager().ClearTimer(BlinkTimer);
    GetWorldTimerManager().ClearTimer(BlinkEndTimer);
    Super::Die();
    GetMesh()->SetMorphTarget(TEXT("AttackFace"), 0.0f);
    GetMesh()->SetMorphTarget(TEXT("HitFace"), 0.0f);
    GetMesh()->SetMorphTarget(TEXT("Blink"), 0.0f);
    PlayStateAnimation(EHeliosEnemyCombatState::Dead, DeathAnimation, false);
}

void AHeliosEnemyActor::SetRuntimeIdentity(const FString& InRuntimeActorId, const FString& InMarkerId)
{
    RuntimeActorId = InRuntimeActorId;
    SourceMarkerId = InMarkerId;
}

AHeliosEnemyE01::AHeliosEnemyE01() { EnemyType = EHeliosEnemyType::E01_Rifle; EnemyTypeId = TEXT("E01"); }
AHeliosEnemyE02::AHeliosEnemyE02() { EnemyType = EHeliosEnemyType::E02_Charger; EnemyTypeId = TEXT("E02"); }
AHeliosEnemyE03::AHeliosEnemyE03() { EnemyType = EHeliosEnemyType::E03_Skirmisher; EnemyTypeId = TEXT("E03"); }
AHeliosEnemyE04::AHeliosEnemyE04() { EnemyType = EHeliosEnemyType::E04_Basketball; EnemyTypeId = TEXT("E04"); }
