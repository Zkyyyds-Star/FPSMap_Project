#include "Bio/HeliosBioTurret.h"
#include "Bio/HeliosBioHotelGameMode.h"
#include "Bio/HeliosBioRoleDefinition.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

namespace
{
// Standalone balance defaults; no original numeric config has been verified.
float PositiveTuning(float Value, float Fallback)
{
    return FMath::IsFinite(Value) && Value > 0.f ? Value : Fallback;
}
}

AHeliosBioTurret::AHeliosBioTurret()
{
    PrimaryActorTick.bCanEverTick = true;
    SourceMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SourceTurretMesh"));
    SetRootComponent(SourceMesh);
    SourceMesh->SetMobility(EComponentMobility::Movable);
    SourceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SourceMesh->SetCollisionObjectType(ECC_WorldDynamic);
    SourceMesh->SetCollisionResponseToAllChannels(ECR_Block);
    SourceMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    SourceMesh->SetCanEverAffectNavigation(false);
    Tags.AddUnique(TEXT("BioTurret"));
}

void AHeliosBioTurret::Configure(int32 InCasterId, int32 InExpectedRound,
    UHeliosBioRoleDefinition* Definition)
{
    if (bConfigured || bResolved || !Definition || InCasterId < 0 || InExpectedRound < 0)
    {
        UE_LOG(LogTemp, Error, TEXT("BIO_TURRET_CONFIGURATION_FAILED caster=%d round=%d"), InCasterId, InExpectedRound);
        RemoveWithoutExplosion();
        return;
    }
    auto* Mode = AHeliosBioHotelGameMode::Find(GetWorld());
    const auto* Caster = Mode ? Mode->Model.FindParticipant(InCasterId) : nullptr;
    AActor* OwnerActor = Mode ? Mode->ParticipantActor(InCasterId) : nullptr;
    UStaticMesh* Mesh = Definition->SkillAuxiliaryMesh.LoadSynchronous();
    if (!Mode || Mode->RoundSerial != InExpectedRound ||
        Mode->Model.GetPhase() != EHeliosBioPhase::Active || !Caster || !Caster->bAlive ||
        !IsValid(OwnerActor) || OwnerActor->IsActorBeingDestroyed() || !Mesh)
    {
        UE_LOG(LogTemp, Error, TEXT("BIO_TURRET_SOURCE_OR_OWNER_UNAVAILABLE caster=%d round=%d mesh=%d source=%s"),
            InCasterId, InExpectedRound, Mesh != nullptr, *Definition->SourceIdentity);
        RemoveWithoutExplosion();
        return;
    }
    CasterId = InCasterId;
    ExpectedRound = InExpectedRound;
    SourceDefinition = Definition;
    CasterActor = OwnerActor;
    bCasterHumanSide = Caster->IsHumanSide();
    SetOwner(OwnerActor);
    SourceMesh->SetStaticMesh(Mesh);
    SourceMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    SourceMesh->SetSimulatePhysics(false);
    MaximumHealth = PositiveTuning(Definition->SkillObjectHealth, 1000.f);
    CurrentHealth = MaximumHealth;
    LifetimeRemaining = PositiveTuning(Definition->SkillObjectLifetime, 20.f);
    FireRange = PositiveTuning(Definition->SkillRange, 1000.f);
    FireDamage = PositiveTuning(Definition->SkillDamage, 160.f);
    FireInterval = FMath::Max(.05f, PositiveTuning(Definition->SkillFireInterval, .2f));
    ExplosionRadius = PositiveTuning(Definition->SkillExplosionRadius, 300.f);
    ExplosionDamage = PositiveTuning(Definition->SkillExplosionDamage, 1000.f);
    FireCooldown = 0.f;
    bConfigured = true;
    UE_LOG(LogTemp, Display, TEXT("BIO_TURRET_CONFIGURED caster=%d round=%d hp=%.1f lifetime=%.1f range=%.1f damage=%.1f source=%s"),
        CasterId, ExpectedRound, CurrentHealth, LifetimeRemaining, FireRange, FireDamage, *Definition->SourceIdentity);
}

AHeliosBioHotelGameMode* AHeliosBioTurret::FindActiveMatch() const
{
    if (!bConfigured || !IsValid(SourceDefinition) || !CasterActor.IsValid() ||
        CasterActor->IsActorBeingDestroyed() || GetOwner() != CasterActor.Get() || IsActorBeingDestroyed()) return nullptr;
    auto* Mode = AHeliosBioHotelGameMode::Find(GetWorld());
    const auto* Caster = Mode ? Mode->Model.FindParticipant(CasterId) : nullptr;
    if (!Mode || Mode->RoundSerial != ExpectedRound || Mode->Model.GetPhase() != EHeliosBioPhase::Active ||
        !Caster || !Caster->bAlive || Caster->IsHumanSide() != bCasterHumanSide ||
        Mode->ParticipantActor(CasterId) != CasterActor.Get()) return nullptr;
    return Mode;
}

bool AHeliosBioTurret::IsActiveTurret() const
{
    return bConfigured && !bResolved && CurrentHealth > 0.f && LifetimeRemaining > 0.f && FindActiveMatch() != nullptr;
}

FVector AHeliosBioTurret::FiringOrigin() const
{
    if (SourceMesh->DoesSocketExist(TEXT("Muzzle"))) return SourceMesh->GetSocketLocation(TEXT("Muzzle"));
    // Use the real mesh bounds for LOS when the source static mesh has no muzzle socket.
    return SourceMesh->Bounds.Origin + FVector(0.f, 0.f, SourceMesh->Bounds.BoxExtent.Z * .35f);
}

bool AHeliosBioTurret::HasLineOfSight(const FVector& Origin, const AActor* Victim) const
{
    if (!GetWorld() || !IsValid(Victim) || Victim->IsActorBeingDestroyed()) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BioTurretLOS), true, this);
    Query.AddIgnoredActor(CasterActor.Get());
    if (CasterActor.IsValid())
    {
        TArray<AActor*> Attachments;
        CasterActor->GetAttachedActors(Attachments, true, true);
        Query.AddIgnoredActors(Attachments);
    }
    FHitResult Hit;
    if (!GetWorld()->LineTraceSingleByChannel(Hit, Origin, Victim->GetActorLocation(), ECC_Visibility, Query)) return true;
    return Hit.GetActor() == Victim || (Hit.GetActor() && Hit.GetActor()->IsOwnedBy(Victim));
}

void AHeliosBioTurret::FireAtOpponent(AHeliosBioHotelGameMode* Mode)
{
    const FVector Origin = FiringOrigin();
    int32 TargetId = INDEX_NONE;
    float BestDistance = FMath::Square(FireRange);
    for (const auto& Participant : Mode->Model.GetParticipants())
    {
        if (!Participant.bAlive || !Mode->AreOpponents(CasterId, Participant.Id)) continue;
        AActor* Candidate = Mode->ParticipantActor(Participant.Id);
        if (!IsValid(Candidate) || Candidate->IsActorBeingDestroyed()) continue;
        const float Distance = FVector::DistSquared(Origin, Candidate->GetActorLocation());
        if (Distance > BestDistance || !HasLineOfSight(Origin, Candidate)) continue;
        BestDistance = Distance;
        TargetId = Participant.Id;
    }
    if (TargetId != INDEX_NONE && Mode->DamageParticipant(TargetId, CasterId, FireDamage, false))
    {
        ++Shots;
        UE_LOG(LogTemp, Verbose, TEXT("BIO_TURRET_SHOT caster=%d target=%d shots=%d round=%d"),
            CasterId, TargetId, Shots, ExpectedRound);
    }
}

void AHeliosBioTurret::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bResolved || !bConfigured) return;
    auto* Mode = FindActiveMatch();
    if (!Mode) { RemoveWithoutExplosion(); return; }
    if (GetWorld()->IsPaused() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f) return;
    LifetimeRemaining = FMath::Max(0.f, LifetimeRemaining - DeltaSeconds);
    if (LifetimeRemaining <= 0.f) { ResolveExplosion(); return; }
    FireCooldown = FMath::Max(0.f, FireCooldown - DeltaSeconds);
    if (FireCooldown <= 0.f)
    {
        FireCooldown = FireInterval;
        FireAtOpponent(Mode);
    }
}

float AHeliosBioTurret::TakeDamage(float Damage, const FDamageEvent& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser)
{
    auto* Mode = FindActiveMatch();
    if (!Mode || DamageCauser == this) return 0.f;
    int32 AttackerId = Mode->ParticipantId(EventInstigator ? EventInstigator->GetPawn() : nullptr);
    if (AttackerId == INDEX_NONE) AttackerId = Mode->ParticipantId(DamageCauser);
    return ApplyBioDamage(AttackerId, Damage);
}

float AHeliosBioTurret::ApplyBioDamage(int32 AttackerId, float Damage)
{
    auto* Mode = FindActiveMatch();
    if (!Mode || !IsActiveTurret() || !FMath::IsFinite(Damage) || Damage <= 0.f) return 0.f;
    const auto* Attacker = Mode->Model.FindParticipant(AttackerId);
    if (!Attacker || !Attacker->bAlive || Attacker->FrozenSeconds > 0.f || !Mode->AreOpponents(CasterId, AttackerId)) return 0.f;
    if (Attacker->Team == EHeliosBioTeam::Human) Damage *= Mode->Model.Config.HumanDamageMultiplier;
    if (Attacker->SkillRemaining > 0.f) Damage *= FMath::Max(0.f, Attacker->SkillDamageMultiplier);
    if (!FMath::IsFinite(Damage) || Damage <= 0.f) return 0.f;
    const float Applied = FMath::Min(CurrentHealth, Damage);
    CurrentHealth = FMath::Max(0.f, CurrentHealth - Applied);
    if (CurrentHealth <= 0.f) ResolveExplosion();
    return Applied;
}

void AHeliosBioTurret::ResolveExplosion()
{
    if (bResolved) return;
    auto* Mode = FindActiveMatch();
    if (!Mode) { RemoveWithoutExplosion(); return; }
    bResolved = true;
    CurrentHealth = 0.f;
    const FVector Origin = FiringOrigin();
    SourceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    for (const auto& Participant : Mode->Model.GetParticipants())
    {
        // A hit can settle the round; never carry damage into the next phase.
        if (Mode->RoundSerial != ExpectedRound || Mode->Model.GetPhase() != EHeliosBioPhase::Active) break;
        if (!Participant.bAlive || !Mode->AreOpponents(CasterId, Participant.Id)) continue;
        AActor* Victim = Mode->ParticipantActor(Participant.Id);
        if (!IsValid(Victim) || FVector::DistSquared(Origin, Victim->GetActorLocation()) > FMath::Square(ExplosionRadius) ||
            !HasLineOfSight(Origin, Victim)) continue;
        if (Mode->DamageParticipant(Participant.Id, CasterId, ExplosionDamage, false)) ++ExplosionHits;
    }
    UE_LOG(LogTemp, Display, TEXT("BIO_TURRET_EXPLODED caster=%d round=%d shots=%d explosion_hits=%d"),
        CasterId, ExpectedRound, Shots, ExplosionHits);
    Destroy();
}

void AHeliosBioTurret::RemoveWithoutExplosion()
{
    if (bResolved) return;
    bResolved = true;
    CurrentHealth = 0.f;
    SourceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    UE_LOG(LogTemp, Display, TEXT("BIO_TURRET_REMOVED caster=%d round=%d shots=%d explosion_hits=%d"),
        CasterId, ExpectedRound, Shots, ExplosionHits);
    Destroy();
}
