#include "Boss/HeliosEnemyBoss.h"
#include "Combat/CombatDamageSettings.h"
#include "Combat/HeliosCombatTempoSettings.h"
#include "Boss/HeliosBossAnimInstance.h"
#include "Boss/HeliosBossEffectsComponent.h"
#include "AIController.h"
#include "ShooterAIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

namespace
{
bool IsBossDamageTarget(const APawn* Pawn)
{
    return IsValid(Pawn) && !Pawn->ActorHasTag(TEXT("Dead"))
        && (Pawn->IsPlayerControlled() || Pawn->ActorHasTag(TEXT("Player"))
            || Pawn->ActorHasTag(TEXT("HeliosPet"))
            || Pawn->ActorHasTag(TEXT("HeliosSummon")));
}
}

AHeliosEnemyBoss::AHeliosEnemyBoss()
{
    BossAwarenessSettings.SightDistance = 4500.f;
    BossAwarenessSettings.RetainDistance = 4500.f;
    BossAwarenessSettings.SightHalfAngle = 130.f;
    BossAwarenessSettings.CloseAwarenessDistance = 800.f;
    BossAwarenessSettings.CheckInterval = .05f;
    BossAwarenessSettings.MemoryDuration = 8.f;
    BossAwarenessSettings.bPersistentCombatTracking = true;
    EnemyType = EHeliosEnemyType::E05_Boss;
    EnemyTypeId = TEXT("E05");
    DeferredDestructionTime = 8.0f;
    Tags.AddUnique(TEXT("HeliosBoss"));
    BossEffects = CreateDefaultSubobject<UHeliosBossEffectsComponent>(TEXT("BossEffects"));
}

void AHeliosEnemyBoss::LoadAnimationAssets()
{
    const auto Load = [](const TCHAR* Name) -> UAnimSequence*
    {
        const FString Asset = FString::Printf(TEXT("A_E05WhiteSuit_%s"), Name);
        return LoadObject<UAnimSequence>(nullptr, *FString::Printf(
            TEXT("/Game/Helios/Enemies/E05WhiteSuit/Animations/%s.%s"), *Asset, *Asset));
    };
    IdleAnimation = Load(TEXT("Idle")); WalkAnimation = Load(TEXT("Walk"));
    RunAnimation = Load(TEXT("Run")); TurnAnimation = Load(TEXT("Turn"));
    EntranceAnimation = Load(TEXT("Entrance")); ChargeAnimation = Load(TEXT("Charge"));
    CloseShoulderAnimation = Load(TEXT("CloseShoulder"));
    HitAnimation = Load(TEXT("Hit")); DeathAnimation = Load(TEXT("Death"));
    AttackAnimation = ChargeAnimation;
    GetMesh()->SetAnimInstanceClass(UHeliosBossAnimInstance::StaticClass());
}

void AHeliosEnemyBoss::BeginPlay()
{
    Super::BeginPlay();
    ShoulderCooldownSeconds = UHeliosCombatTempoSettings::Resolve(this, EHeliosTempoFile::Attack,
        TEXT("Boss"), TEXT("冲锋冷却"), ShoulderCooldownSeconds);
    CloseCooldownSeconds = UHeliosCombatTempoSettings::Resolve(this, EHeliosTempoFile::Attack,
        TEXT("Boss"), TEXT("近身攻击冷却"), CloseCooldownSeconds);
    ShoulderSpeed = UHeliosCombatTempoSettings::Resolve(this, EHeliosTempoFile::Movement,
        TEXT("Boss"), TEXT("冲锋速度"), ShoulderSpeed);
    CloseSpeed = UHeliosCombatTempoSettings::Resolve(this, EHeliosTempoFile::Movement,
        TEXT("Boss"), TEXT("近身突进速度"), CloseSpeed);
    GetMesh()->AddTickPrerequisiteActor(this);
    GetCharacterMovement()->bOrientRotationToMovement = false;
    GetCharacterMovement()->bUseControllerDesiredRotation = false;
    if (bSkipUrbanEntrance)
    {
        bEntering = false;
        EntranceRemaining = 0.f;
        PlayStateAnimation(EHeliosEnemyCombatState::Idle, IdleAnimation, true);
    }
    else
    {
        EntranceRemaining = EntranceAnimation ? EntranceAnimation->GetPlayLength() : 0.6f;
        PlayStateAnimation(EHeliosEnemyCombatState::Attack, EntranceAnimation, false);
    }
}

int32 AHeliosEnemyBoss::GetBossPhase() const { return CurrentHP <= Definition.MaxHP * 0.5f ? 2 : 1; }
float AHeliosEnemyBoss::WindupDuration() const
{
    return BossAttack == EHeliosBossAttackType::CloseShoulder ? .4f : .75f;
}
float AHeliosEnemyBoss::ActiveDuration() const
{
    return BossAttack == EHeliosBossAttackType::CloseShoulder ? .2f : .6f;
}
float AHeliosEnemyBoss::GetBossAttackDuration() const
{
    if (BossAttack == EHeliosBossAttackType::None) return 0.f;
    return WindupDuration() + ActiveDuration() + (BossAttack == EHeliosBossAttackType::CloseShoulder ? .65f : .9f);
}
float AHeliosEnemyBoss::GetBossWindupProgress() const
{
    return BossAttack == EHeliosBossAttackType::None ? 0.0f : FMath::Clamp(AttackElapsed / WindupDuration(), 0.0f, 1.0f);
}
bool AHeliosEnemyBoss::IsBossAttackReleasing() const
{
    return BossAttack != EHeliosBossAttackType::None && bReleased
        && AttackElapsed < WindupDuration() + ActiveDuration() && !bChargeStopped;
}

void AHeliosEnemyBoss::StartShooting(AActor* ActorToShoot)
{
    if (bIsDead || !IsValid(ActorToShoot) || ActorToShoot == this) return;
    CurrentAimTarget = ActorToShoot;
    bIsShooting = true;
    BeginAttackCycle();
}
void AHeliosEnemyBoss::StopShooting()
{
    // A StateTree LOS transition must not cancel an already telegraphed wave.
    bIsShooting = false;
    CurrentAimTarget = nullptr;
    GetWorldTimerManager().ClearTimer(AttackCycleTimer);
    if (AAIController* AI = Cast<AAIController>(GetController())) AI->ClearFocus(EAIFocusPriority::Gameplay);
}
void AHeliosEnemyBoss::HandleAttackNotify() { ++AttackNotifyCount; }

bool AHeliosEnemyBoss::HasBossLineOfSight(AActor* Target, const FVector& Origin) const
{
    if (!IsValid(Target) || !GetWorld()) return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(HeliosBossLOS), true, this);
    FHitResult Hit;
    return !GetWorld()->LineTraceSingleByChannel(Hit, Origin, Target->GetActorLocation(), ECC_Visibility, Params)
        || Hit.GetActor() == Target;
}
bool AHeliosEnemyBoss::HasClearContactPath(AActor* Target, const FVector& Origin) const
{
    if (!IsValid(Target) || !GetWorld()) return false;
    // Damage follows the movement collision channel. Visibility-only props must not
    // suppress a real shoulder/body contact, while solid cover still blocks it.
    FCollisionQueryParams Params(SCENE_QUERY_STAT(HeliosBossContactPath), true, this);
    FHitResult Hit;
    return !GetWorld()->LineTraceSingleByChannel(Hit, Origin, Target->GetActorLocation(), ECC_Pawn, Params)
        || Hit.GetActor() == Target;
}
void AHeliosEnemyBoss::BeginAttackCycle()
{
    if (bIsDead || bEntering || BossAttack != EHeliosBossAttackType::None || ActionWindow > 0.0f
        || !bIsShooting || !IsValid(CurrentAimTarget)) return;
    const float Distance = FVector::Dist2D(GetActorLocation(), CurrentAimTarget->GetActorLocation());
    if (!HasBossLineOfSight(CurrentAimTarget, GetActorLocation() + FVector(0, 0, 30))) return;
    if (Distance < 250.f && CloseCooldown <= 0.f)
        StartAbility(EHeliosBossAttackType::CloseShoulder);
    else if (Distance >= 250.f && Distance <= 900.f && ShoulderCooldown <= 0.f)
        StartAbility(EHeliosBossAttackType::ShoulderCharge);
}
void AHeliosEnemyBoss::StartAbility(EHeliosBossAttackType Type)
{
    BossAttack = Type;
    AttackElapsed = 0.0f;
    WaveRadius = 0.0f;
    ChargeDistance = 0.0f;
    bReleased = false;
    bChargeStopped = false;
    HitTargets.Reset(); PreviousPlayerLocations.Reset();
    ++CastSerial; ++AttackCount;
    AttackOrigin = GetActorLocation();
    AttackDirection = (CurrentAimTarget->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
    if (AttackDirection.IsNearlyZero()) AttackDirection = GetActorForwardVector();
    if (AAIController* AI = Cast<AAIController>(GetController()))
    {
        AI->StopMovement(); AI->ClearFocus(EAIFocusPriority::Gameplay);
    }
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->bOrientRotationToMovement = false;
    GetCharacterMovement()->bUseControllerDesiredRotation = false;
    SetActorRotation(AttackDirection.Rotation());
    const float PhaseScale = GetBossPhase() == 2 ? .8f : 1.f;
    if (Type == EHeliosBossAttackType::ShoulderCharge) ShoulderCooldown = ShoulderCooldownSeconds * PhaseScale;
    else CloseCooldown = CloseCooldownSeconds * PhaseScale;
    PlayStateAnimation(EHeliosEnemyCombatState::Attack,
        Type == EHeliosBossAttackType::CloseShoulder ? CloseShoulderAnimation : ChargeAnimation, false);
    BossEffects->BeginWindup(Type, WindupDuration());
}
void AHeliosEnemyBoss::ReleaseAbility()
{
    bReleased = true;
    // Facing is frozen for the final 0.2 seconds of windup and throughout contact.
    AttackDirection = GetActorForwardVector().GetSafeNormal2D();
    SetActorRotation(AttackDirection.Rotation());
    AttackOrigin = GetActorLocation() + FVector(0, 0, 25.0f);
    PreviousShoulder = ShoulderLocation();
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        if (APlayerController* PC = It->Get())
            if (APawn* Pawn = PC->GetPawn()) PreviousPlayerLocations.Add(Pawn, Pawn->GetActorLocation());
    BossEffects->ReleaseAttack(BossAttack,
        ShoulderLocation(), AttackDirection);
}

void AHeliosEnemyBoss::Tick(float DeltaSeconds)
{
    // The shared enemy class uses a single-node player; this boss owns an independent blended graph.
    AShooterNPC::Tick(DeltaSeconds);
    if (DeltaSeconds <= 0.0f) return;
    if (bIsDead) { TickAnimation(DeltaSeconds); return; }
    if (AShooterAIController* AI = Cast<AShooterAIController>(GetController()))
    {
        AActor* Target = AI->GetTrackingTarget();
        if (PursuitTarget.Get() != Target) { MoveRefresh = 0.f; bHasMoveGoal = false; }
        PursuitTarget = Target;
        // Losing attack visibility never releases the controller's combat lock.
        CurrentAimTarget = IsValid(Target) && !Target->ActorHasTag(TEXT("Dead"))
            && HasBossLineOfSight(Target, GetActorLocation() + FVector(0, 0, 30)) ? Target : nullptr;
        bIsShooting = IsValid(CurrentAimTarget);
    }
    ShoulderCooldown = FMath::Max(0.0f, ShoulderCooldown - DeltaSeconds);
    CloseCooldown = FMath::Max(0.0f, CloseCooldown - DeltaSeconds);
    RadialCooldown = FMath::Max(0.0f, RadialCooldown - DeltaSeconds);
    ActionWindow = FMath::Max(0.0f, ActionWindow - DeltaSeconds);
    if (bEntering)
    {
        if (AAIController* AI = Cast<AAIController>(GetController())) AI->StopMovement();
        GetCharacterMovement()->StopMovementImmediately();
        EntranceRemaining -= DeltaSeconds;
        if (EntranceRemaining <= 0.0f) { bEntering = false; ActionWindow = .15f; }
    }
    else if (BossAttack != EHeliosBossAttackType::None) TickAbility(DeltaSeconds);
    else
    {
        BeginAttackCycle();
        if (BossAttack == EHeliosBossAttackType::None) TickMovement(DeltaSeconds);
    }
    TickAnimation(DeltaSeconds);
    PreviousYaw = GetActorRotation().Yaw;
}
void AHeliosEnemyBoss::TickAbility(float DeltaSeconds)
{
    if (AAIController* AI = Cast<AAIController>(GetController())) AI->StopMovement();
    GetCharacterMovement()->StopMovementImmediately();
    const float PreviousTime = AttackElapsed;
    AttackElapsed += DeltaSeconds;
    if (!bReleased)
    {
        if (IsValid(CurrentAimTarget) && AttackElapsed < WindupDuration() - .2f)
        {
            const FRotator Facing = (CurrentAimTarget->GetActorLocation() - GetActorLocation()).Rotation();
            SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), FRotator(0, Facing.Yaw, 0), DeltaSeconds, 150.0f));
            AttackDirection = GetActorForwardVector();
        }
        if (AttackElapsed >= WindupDuration()) ReleaseAbility();
    }
    if (bReleased)
    {
        const float ActiveStep = FMath::Max(0.0f, FMath::Min(AttackElapsed, WindupDuration() + ActiveDuration())
            - FMath::Max(PreviousTime, WindupDuration()));
        if (ActiveStep > 0.0f)
        {
            TickShoulder(ActiveStep);
        }
    }
    if (AttackElapsed >= GetBossAttackDuration()) FinishAttackCycle();
}

FVector AHeliosEnemyBoss::ShoulderLocation() const
{
    for (const FName Bone : { FName(TEXT("ShoulderContact")), FName(TEXT("upperarm_r")), FName(TEXT("clavicle_r")) })
        if (GetMesh()->DoesSocketExist(Bone)) return GetMesh()->GetSocketLocation(Bone);
    return GetActorLocation() + GetActorRightVector() * Definition.CapsuleRadius
        + FVector(0, 0, Definition.CapsuleHalfHeight * .56f);
}
FVector AHeliosEnemyBoss::GetBossMouthLocation() const
{
    if (GetMesh()->DoesSocketExist(TEXT("mouth_socket"))) return GetMesh()->GetSocketLocation(TEXT("mouth_socket"));
    return GetActorLocation() + GetActorForwardVector() * Definition.CapsuleRadius * .65f
        + FVector(0, 0, Definition.CapsuleHalfHeight * .75f);
}
float AHeliosEnemyBoss::DamageContactTarget(AActor* Target, float Damage, bool bPush)
{
    const APawn* Pawn = Cast<APawn>(Target);
    if (!IsBossDamageTarget(Pawn) || HitTargets.Contains(Target)) return 0.f;
    // A protected player or inactive ally still physically stops the cast, but is
    // not reported as damaged and receives no knockback or hit feedback.
    HitTargets.Add(Target);
    const float Applied = UGameplayStatics::ApplyDamage(Target, Damage, GetController(), this, UDamageType::StaticClass());
    if (Applied <= 0.f) return 0.f;
    ++AttacksCommitted;
    if (bPush)
        if (ACharacter* Character = Cast<ACharacter>(Target))
            Character->LaunchCharacter(AttackDirection * (BossAttack == EHeliosBossAttackType::CloseShoulder ? 180.f : 340.f) + FVector(0, 0, 65.0f), false, false);
    if (!bPush)
    {
        BossEffects->WaveHit(Target->GetActorLocation());
        if (ACharacter* Character = Cast<ACharacter>(Target))
            Character->LaunchCharacter((Target->GetActorLocation()-AttackOrigin).GetSafeNormal2D()*100.f, false, false);
    }
    return Applied;
}
void AHeliosEnemyBoss::TickShoulder(float DeltaSeconds)
{
    if (bChargeStopped) return;
    const bool bClose = BossAttack == EHeliosBossAttackType::CloseShoulder;
    const float MaxTravel = bClose ? 180.f : 900.f;
    const float Speed = bClose ? CloseSpeed : ShoulderSpeed;
    const float Damage = UCombatDamageSettings::Resolve(this,bClose ? ECombatDamageType::BossClose : ECombatDamageType::BossCharge);
    float Remaining = DeltaSeconds;
    // Bounded collision substeps keep real shoulder contact stable even during a long frame.
    while (Remaining > KINDA_SMALL_NUMBER && !bChargeStopped)
    {
        const float Dt = FMath::Min(Remaining, 1.0f / 60.0f);
        Remaining -= Dt;
        const float Step = FMath::Min(Speed * Dt, MaxTravel - ChargeDistance);
        const FVector Before = GetActorLocation();
        FHitResult BodyHit;
        SetActorLocation(Before + AttackDirection * Step, true, &BodyHit, ETeleportType::None);
        ChargeDistance += FVector::Dist2D(Before, GetActorLocation());
        const FVector Shoulder = ShoulderLocation();
        TArray<FHitResult> Contacts;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(HeliosBossShoulder), false, this);
        // Sweep the shoulder surface, scaled with the body rather than the former 188cm rig.
        GetWorld()->SweepMultiByChannel(Contacts, PreviousShoulder, Shoulder + AttackDirection * Definition.CapsuleRadius * .85f,
            FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(Definition.CapsuleRadius), Params);
        const auto ApplyContact = [&](const FHitResult& Contact, bool bCheckPath) -> bool
        {
            APawn* Pawn = Cast<APawn>(Contact.GetActor());
            if (!IsBossDamageTarget(Pawn) || HitTargets.Contains(Pawn)
                || (bCheckPath && !HasClearContactPath(Pawn, Before))) return false;
            const float Applied = DamageContactTarget(Pawn, Damage, true);
            BossEffects->Impact(Contact.ImpactPoint, Contact.ImpactNormal, Applied > 0.f);
            return true;
        };
        for (const FHitResult& Contact : Contacts) ApplyContact(Contact, true);
        PreviousShoulder = Shoulder;
        // The taller capsule can stop at a target's torso before the shoulder sphere reaches them.
        // Count that actual body contact once, using the same cast history and damage rule.
        if (BodyHit.bBlockingHit) ApplyContact(BodyHit, false);
        // Summons ignore the Pawn trace channel to allow crowd movement. Query their
        // object type along the actual body movement, without changing that response.
        TArray<FHitResult> SummonContacts;
        const UCapsuleComponent* Capsule = GetCapsuleComponent();
        GetWorld()->SweepMultiByObjectType(SummonContacts, Before, GetActorLocation(),
            Capsule->GetComponentQuat(), FCollisionObjectQueryParams(ECC_Pawn),
            FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Params);
        for (const FHitResult& Contact : SummonContacts)
        {
            APawn* Pawn = Cast<APawn>(Contact.GetActor());
            if (IsBossDamageTarget(Pawn) && Pawn->ActorHasTag(TEXT("HeliosSummon")))
                ApplyContact(Contact, true);
        }
        if (BodyHit.bBlockingHit || HitTargets.Num() > 0 || ChargeDistance >= MaxTravel - .1f)
        {
            bChargeStopped = true;
            AttackElapsed = WindupDuration() + ActiveDuration();
            BossEffects->EndDrive();
            GetCharacterMovement()->StopMovementImmediately();
            if (BodyHit.bBlockingHit && !HitTargets.Contains(BodyHit.GetActor()))
                BossEffects->Impact(BodyHit.ImpactPoint, BodyHit.ImpactNormal, false);
        }
    }
}

// Legacy waves remain serialized enum values, but never participate in melee damage.
void AHeliosEnemyBoss::TickWave(float) {}

void AHeliosEnemyBoss::FinishAttackCycle()
{
    BossAttack = EHeliosBossAttackType::None;
    bReleased = false;
    WaveRadius = 0.0f;
    HitTargets.Reset(); PreviousPlayerLocations.Reset();
    ActionWindow = .15f;
    MoveRefresh = 0.0f;
    BossEffects->StopAll();
    PlayStateAnimation(EHeliosEnemyCombatState::Idle, IdleAnimation, true);
}
void AHeliosEnemyBoss::TickMovement(float DeltaSeconds)
{
    MoveRefresh -= DeltaSeconds;
    AShooterAIController* AI = Cast<AShooterAIController>(GetController());
    AActor* Target = PursuitTarget.Get();
    const bool bInvestigate = AI && AI->IsInvestigating();
    if (!AI || (!IsValid(Target) && !bInvestigate))
    {
        if (AI && bHasMoveGoal) AI->StopMovement();
        bHasMoveGoal = false;
        NoProgressTime = 0.f;
        RefreshLocomotionAnimation(DeltaSeconds);
        return;
    }
    const FVector Location = GetActorLocation();
    if (FVector::DistSquared2D(Location, ProgressLocation) > FMath::Square(20.f))
    {
        ProgressLocation = Location;
        NoProgressTime = 0.f;
    }
    else NoProgressTime += DeltaSeconds;
    if (MoveRefresh <= 0.f)
    {
        FVector Goal = Target ? Target->GetActorLocation() : AI->GetInvestigationLocation();
        const float Distance = FVector::Dist2D(Location, Goal);
        GetCharacterMovement()->bOrientRotationToMovement = true;
        GetCharacterMovement()->bUseControllerDesiredRotation = false;
        GetCharacterMovement()->MaxWalkSpeed = Distance > 600.f ? Definition.RunSpeed : Definition.WalkSpeed;
        const bool bClose = Target && Distance <= 250.f && HasBossLineOfSight(Target, Location);
        // Keep a stable close-range step until reached, instead of replacing it every poll.
        if (bClose)
        {
            const FVector Away = (Location - Goal).GetSafeNormal2D();
            const FVector Side = FVector::CrossProduct(FVector::UpVector, Away) * ((CastSerial % 2) ? -1.f : 1.f);
            Goal = bHasMoveGoal && AI->GetMoveStatus() == EPathFollowingStatus::Moving
                ? LastMoveGoal : Location + Away * 170.f + Side * 170.f;
        }
        const bool bStuck = NoProgressTime >= 1.f;
        const bool bGoalChanged = !bHasMoveGoal || FVector::DistSquared2D(Goal, LastMoveGoal) > FMath::Square(75.f);
        const bool bPathEnded = AI->GetMoveStatus() != EPathFollowingStatus::Moving;
        if (bGoalChanged || bStuck || bPathEnded)
        {
            // Partial paths advance to the reachable edge. Retry blocked goals once per second.
            AI->MoveToLocation(Goal, bClose ? 35.f : 95.f, true, true, true, false, nullptr, true);
            LastMoveGoal = Goal;
            bHasMoveGoal = true;
            if (bStuck) { NoProgressTime = 0.f; ProgressLocation = Location; }
            MoveRefresh = AI->GetMoveStatus() == EPathFollowingStatus::Moving ? .15f : 1.f;
        }
        else MoveRefresh = .15f;
    }
    RefreshLocomotionAnimation(DeltaSeconds);
}
void AHeliosEnemyBoss::PlayStateAnimation(EHeliosEnemyCombatState NewState, UAnimSequence* Sequence, bool bLoop)
{
    if (CurrentBossAnimation != Sequence || CombatState != NewState) AnimationElapsed = 0.0f;
    CombatState = NewState;
    CurrentBossAnimation = Sequence ? Sequence : IdleAnimation.Get();
    bAnimationLoop = bLoop;
}
void AHeliosEnemyBoss::TickAnimation(float DeltaSeconds)
{
    float Rate = 1.0f;
    if (CombatState == EHeliosEnemyCombatState::Walk || CombatState == EHeliosEnemyCombatState::Run)
    {
        const float GroundSpeed = CombatState == EHeliosEnemyCombatState::Run
            ? Definition.RunAnimationGroundSpeed : Definition.WalkAnimationGroundSpeed;
        Rate = FMath::Clamp(GetVelocity().Size2D() / FMath::Max(1.0f, GroundSpeed), 0.35f, 2.75f);
    }
    AnimationElapsed += DeltaSeconds * Rate;
    if (BossAttack != EHeliosBossAttackType::None && CurrentBossAnimation)
        AnimationElapsed = AttackElapsed / GetBossAttackDuration() * CurrentBossAnimation->GetPlayLength();
    if (UHeliosBossAnimInstance* Anim = Cast<UHeliosBossAnimInstance>(GetMesh()->GetAnimInstance()))
        Anim->SetTimelinePose(CurrentBossAnimation, AnimationElapsed, bAnimationLoop, DeltaSeconds);
}
void AHeliosEnemyBoss::ClearCombat()
{
    BossAttack = EHeliosBossAttackType::None;
    bReleased = false; bChargeStopped = true; bEntering = false;
    AttackElapsed = 0.0f; WaveRadius = 0.0f;
    PursuitTarget.Reset(); bHasMoveGoal = false; NoProgressTime = 0.f;
    if (AShooterAIController* AI = Cast<AShooterAIController>(GetController())) AI->ResetAwareness();
    HitTargets.Reset(); PreviousPlayerLocations.Reset();
    if (BossEffects) BossEffects->StopAll();
    if (GetWorld()) GetWorldTimerManager().ClearTimer(AttackCycleTimer);
}
void AHeliosEnemyBoss::Die()
{
    ClearCombat();
    Super::Die();
}
void AHeliosEnemyBoss::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ClearCombat();
    Super::EndPlay(EndPlayReason);
}
