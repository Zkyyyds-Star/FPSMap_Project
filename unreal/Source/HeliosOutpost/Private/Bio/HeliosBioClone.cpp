#include "Bio/HeliosBioClone.h"
#include "Bio/HeliosBioHotelGameMode.h"
#include "Bio/HeliosBioRoleDefinition.h"
#include "AIController.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

AHeliosBioClone::AHeliosBioClone()
{
    PrimaryActorTick.bCanEverTick = true;
    AIControllerClass = AAIController::StaticClass();
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
    EnemyType = EHeliosEnemyType::None;
    EnemyTypeId = TEXT("BioWhiteBoneClone");
    Tags.AddUnique(TEXT("Enemy"));
    Tags.AddUnique(TEXT("BioClone"));
    Tags.Remove(TEXT("Friendly"));
    GetCapsuleComponent()->InitCapsuleSize(34.f, 96.f);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    GetCharacterMovement()->MaxWalkSpeed = CloneMoveSpeed;
    GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
}

void AHeliosBioClone::ConfigureClone(int32 InMotherId, float InCloneMaxHP, float Damage, float Lifetime)
{
    if (bConfigured || InMotherId < 0 || !FMath::IsFinite(InCloneMaxHP) || InCloneMaxHP <= 0.f ||
        !FMath::IsFinite(Damage) || Damage < 0.f || !FMath::IsFinite(Lifetime) || Lifetime <= 0.f)
    {
        UE_LOG(LogTemp, Error, TEXT("BIOCLONE_CONFIGURATION_FAILED mother=%d hp=%f damage=%f lifetime=%f"), InMotherId, InCloneMaxHP, Damage, Lifetime);
        Destroy();
        return;
    }
    MotherId = InMotherId;
    CloneMaxHealth = InCloneMaxHP;
    CurrentHP = CloneMaxHealth;
    AttackDamage = Damage;
    LifetimeRemaining = Lifetime;
    bConfigured = true;
    bIsDead = false;
    if (auto* Mode = AHeliosBioHotelGameMode::Find(GetWorld())) ConfiguredRoundSerial = Mode->RoundSerial;
    GetCharacterMovement()->MaxWalkSpeed = FMath::Max(0.f, CloneMoveSpeed);
}

bool AHeliosBioClone::LoadSourceArt()
{
    SourceRole = LoadObject<UHeliosBioRoleDefinition>(nullptr,
        TEXT("/Game/Helios/CFMBioPyramid/Characters/DA_BioKuiLei.DA_BioKuiLei"));
    USkeletalMesh* SourceMeshAsset = SourceRole ? SourceRole->ThirdPersonMesh.LoadSynchronous() : nullptr;
    if (!SourceMeshAsset)
    {
        UE_LOG(LogTemp, Error, TEXT("BIOCLONE_SOURCE_MESH_MISSING DA_BioKuiLei; refusing substitute geometry"));
        return false;
    }
    GetMesh()->SetSkeletalMesh(SourceMeshAsset);
    GetMesh()->SetRelativeTransform(SourceRole->ThirdPersonTransform);
    GetMesh()->SetComponentTickEnabled(true);
    GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    GetMesh()->SetVisibility(true);
    CloneIdle = SourceRole->TPIdle.LoadSynchronous();
    CloneRun = SourceRole->TPRun.LoadSynchronous();
    CloneAttack = SourceRole->TPAttack.LoadSynchronous();
    CloneDeath = SourceRole->TPDeath.LoadSynchronous();
    if (!CloneIdle || !CloneRun || !CloneAttack || !CloneDeath)
        UE_LOG(LogTemp, Warning, TEXT("BIOCLONE_SOURCE_CLIP_MISSING idle=%d run=%d attack=%d death=%d source=%s"),
            CloneIdle != nullptr, CloneRun != nullptr, CloneAttack != nullptr, CloneDeath != nullptr, *SourceRole->SourceIdentity);
    PlayCloneClip(CloneIdle, true);
    return true;
}

void AHeliosBioClone::BeginPlay()
{
    // Intentionally bypass ShooterNPC/Enemy initialization, weapons, StateTree and persistence.
    ACharacter::BeginPlay();
    bSourceArtReady = LoadSourceArt();
    if (!bSourceArtReady) { Destroy(); return; }
    if (!Controller) SpawnDefaultController();
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void AHeliosBioClone::EndPlay(const EEndPlayReason::Type Reason)
{
    if (auto* AI = Cast<AAIController>(Controller)) AI->StopMovement();
    Target.Reset();
    // No normal enemy death delegate, drop, save or director behavior.
    ACharacter::EndPlay(Reason);
}

void AHeliosBioClone::PlayCloneClip(UAnimSequence* Clip, bool bLoop)
{
    if (!Clip || (bLoop && CurrentClip == Clip)) return;
    CurrentClip = Clip;
    GetMesh()->PlayAnimation(Clip, bLoop);
}

void AHeliosBioClone::FindHumanTarget()
{
    Target.Reset();
    auto* Mode = AHeliosBioHotelGameMode::Find(GetWorld());
    if (!Mode) return;
    float BestDistance = TNumericLimits<float>::Max();
    for (const auto& Participant : Mode->Model.GetParticipants())
    {
        if (!Participant.bAlive || !Participant.IsHumanSide()) continue;
        AActor* Candidate = Mode->ParticipantActor(Participant.Id);
        if (!IsValid(Candidate) || Candidate->IsActorBeingDestroyed()) continue;
        const float Distance = FVector::DistSquared(GetActorLocation(), Candidate->GetActorLocation());
        if (Distance < BestDistance) { BestDistance = Distance; Target = Candidate; }
    }
    if (auto* AI = Cast<AAIController>(Controller))
    {
        if (Target.IsValid()) AI->MoveToActor(Target.Get(), FMath::Max(20.f, CloneMeleeRange * .5f));
        else AI->StopMovement();
    }
}

bool AHeliosBioClone::HasMeleeLineOfSight(const AActor* Victim) const
{
    if (!IsValid(Victim) || !GetWorld()) return false;
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BioCloneMelee), true, this);
    const FVector Start = GetActorLocation() + FVector(0.f, 0.f, 45.f);
    const FVector End = Victim->GetActorLocation() + FVector(0.f, 0.f, 25.f);
    if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Query)) return true;
    return Hit.GetActor() == Victim || (Hit.GetActor() && Hit.GetActor()->GetOwner() == Victim);
}

void AHeliosBioClone::Tick(float DeltaSeconds)
{
    ACharacter::Tick(DeltaSeconds);
    if (!GetWorld() || GetWorld()->IsPaused() || !bConfigured || !bSourceArtReady) return;
    LifetimeRemaining = FMath::Max(0.f, LifetimeRemaining - DeltaSeconds);
    auto* Mode = AHeliosBioHotelGameMode::Find(GetWorld());
    const auto* Mother = Mode ? Mode->Model.FindParticipant(MotherId) : nullptr;
    if (LifetimeRemaining <= 0.f || !Mode || Mode->RoundSerial != ConfiguredRoundSerial ||
        Mode->Model.GetPhase() != EHeliosBioPhase::Active || !Mother || Mother->Team != EHeliosBioTeam::Zombie)
    { Destroy(); return; }
    if (bIsDead)
    {
        DeathPoseRemaining = FMath::Max(0.f, DeathPoseRemaining - DeltaSeconds);
        if (DeathPoseRemaining <= 0.f) Destroy();
        return;
    }
    AttackCooldown = FMath::Max(0.f, AttackCooldown - DeltaSeconds);
    AttackPoseRemaining = FMath::Max(0.f, AttackPoseRemaining - DeltaSeconds);
    DecisionRemaining -= DeltaSeconds;
    if (DecisionRemaining <= 0.f)
    {
        DecisionRemaining = FMath::Max(.05f, CloneDecisionInterval);
        FindHumanTarget();
    }
    if (Target.IsValid())
    {
        const int32 VictimId = Mode->ParticipantId(Target.Get());
        const auto* Victim = Mode->Model.FindParticipant(VictimId);
        if (!Victim || !Victim->bAlive || !Victim->IsHumanSide()) { Target.Reset(); DecisionRemaining = 0.f; }
        else if (FVector::DistSquared(GetActorLocation(), Target->GetActorLocation()) <= FMath::Square(FMath::Max(0.f, CloneMeleeRange)) &&
            AttackCooldown <= 0.f && HasMeleeLineOfSight(Target.Get()))
        {
            if (auto* AI = Cast<AAIController>(Controller)) AI->StopMovement();
            FRotator Facing = (Target->GetActorLocation() - GetActorLocation()).Rotation();
            Facing.Pitch = Facing.Roll = 0.f;
            SetActorRotation(Facing);
            AttackCooldown = FMath::Max(.05f, CloneAttackInterval);
            AttackPoseRemaining = CloneAttack ? CloneAttack->GetPlayLength() : 0.f;
            CombatState = EHeliosEnemyCombatState::Attack;
            PlayCloneClip(CloneAttack, false);
            // Infection attribution to the mother is an explicit local gameplay adaptation.
            if (Mode->DamageParticipant(VictimId, MotherId, AttackDamage, false)) ++AttackCount;
        }
    }
    if (AttackPoseRemaining <= 0.f)
    {
        const bool bMoving = GetVelocity().SizeSquared2D() > 100.f;
        CombatState = bMoving ? EHeliosEnemyCombatState::Run : EHeliosEnemyCombatState::Idle;
        PlayCloneClip(bMoving ? CloneRun.Get() : CloneIdle.Get(), true);
    }
}

float AHeliosBioClone::TakeDamage(float Damage, const FDamageEvent& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser)
{
    auto* Mode = AHeliosBioHotelGameMode::Find(GetWorld());
    if (!Mode) return 0.f;
    const AActor* Attacker = EventInstigator ? EventInstigator->GetPawn() : DamageCauser;
    int32 AttackerId = Mode->ParticipantId(Attacker);
    if (AttackerId == INDEX_NONE) AttackerId = Mode->ParticipantId(DamageCauser);
    if (DamageCauser == this || (DamageCauser && DamageCauser->ActorHasTag(TEXT("BioClone")))) return 0.f;
    return ApplyBioDamage(AttackerId, Damage);
}

float AHeliosBioClone::ApplyBioDamage(int32 AttackerId, float Damage)
{
    if (!IsActiveClone() || !FMath::IsFinite(Damage) || Damage <= 0.f) return 0.f;
    auto* Mode = AHeliosBioHotelGameMode::Find(GetWorld());
    if (!Mode || Mode->Model.GetPhase() != EHeliosBioPhase::Active || Mode->RoundSerial != ConfiguredRoundSerial) return 0.f;
    const auto* Attacker = Mode->Model.FindParticipant(AttackerId);
    if (Attacker && (!Attacker->bAlive || !Attacker->IsHumanSide())) return 0.f;
    if (!Attacker && AttackerId != INDEX_NONE) return 0.f;
    if (Attacker && Attacker->Team == EHeliosBioTeam::Human) Damage *= 6.f;
    else if (Attacker && Attacker->IsHunter() && Attacker->SkillRemaining > 0.f)
        Damage *= FMath::Max(1.f, Mode->Model.Config.HunterSkillDamageMultiplier);
    if (!FMath::IsFinite(Damage)) return 0.f;
    const float Applied = FMath::Min(CurrentHP, Damage);
    CurrentHP = FMath::Max(0.f, CurrentHP - Damage);
    if (CurrentHP <= 0.f) Die();
    return Applied;
}

void AHeliosBioClone::Die()
{
    if (bIsDead) return;
    bIsDead = true;
    CurrentHP = 0.f;
    Tags.AddUnique(TEXT("Dead"));
    CombatState = EHeliosEnemyCombatState::Dead;
    Target.Reset();
    if (auto* AI = Cast<AAIController>(Controller)) AI->StopMovement();
    GetCharacterMovement()->DisableMovement();
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DeathPoseRemaining = CloneDeath ? CloneDeath->GetPlayLength() : 0.f;
    if (CloneDeath) PlayCloneClip(CloneDeath, false);
    else Destroy();
}
