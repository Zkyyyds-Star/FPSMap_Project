#include "Weapons/HeliosBasketballProjectile.h"

#include "Player/HeliosArenaCharacter.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Variant_Shooter/AI/ShooterNPC.h"

namespace
{
bool IsHeliosEnemyQADiagnosticsEnabled()
{
    FString Mode;
    return FParse::Value(FCommandLine::Get(), TEXT("HeliosEnemyQA="), Mode)
        || FParse::Param(FCommandLine::Get(), TEXT("HeliosEnemyQA"));
}

int32 GHeliosBasketballQAShotCounter = 0;

FString DescribeBasketballActor(const AActor* Actor)
{
    return IsValid(Actor)
        ? FString::Printf(TEXT("%s class=%s location=%s"), *Actor->GetPathName(),
            *Actor->GetClass()->GetPathName(), *Actor->GetActorLocation().ToCompactString())
        : TEXT("None");
}
}

AHeliosBasketballProjectile::AHeliosBasketballProjectile()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.05f;
    InitialLifeSpan = 6.0f;
    Tags.Add(TEXT("HeliosEnemyProjectile"));

    Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
    RootComponent = Collision;
    Collision->InitSphereRadius(22.0f);
    Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Collision->SetCollisionObjectType(ECC_WorldDynamic);
    Collision->SetCollisionResponseToAllChannels(ECR_Block);
    Collision->SetNotifyRigidBodyCollision(true);
    Collision->OnComponentHit.AddDynamic(this, &AHeliosBasketballProjectile::OnBallHit);

    BallMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BallMesh"));
    BallMesh->SetupAttachment(Collision);
    BallMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BallMesh->SetRelativeScale3D(FVector(1.f));
    if (UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Helios/Enemies/Shared/SM_HeliosBasketball.SM_HeliosBasketball")))
    {
        BallMesh->SetStaticMesh(Sphere);
    }

    Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
    Movement->UpdatedComponent = Collision;
    Movement->InitialSpeed = 0.0f;
    Movement->MaxSpeed = 4400.0f;
    Movement->ProjectileGravityScale = 0.8f;
    Movement->bRotationFollowsVelocity = true;
    Movement->bShouldBounce = false;
    Movement->bSweepCollision = true;
    Movement->bForceSubStepping = true;
    Movement->MaxSimulationTimeStep = 1.0f / 120.0f;
    Movement->MaxSimulationIterations = 16;
}

void AHeliosBasketballProjectile::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (IsValid(IntendedTarget))
    {
        QAClosestTargetDistance = FMath::Min(QAClosestTargetDistance,
            FVector::Distance(GetActorLocation(), IntendedTarget->GetActorLocation()));
    }

    if (bQADiagnostics && GetWorld() && GetWorld()->GetTimeSeconds() >= QANextSampleTime)
    {
        QANextSampleTime = GetWorld()->GetTimeSeconds() + 0.20f;
        UE_LOG(LogTemp, Display,
            TEXT("HELIOS_BASKETBALL_QA phase=flight shot=%d age=%.3f projectile=%s velocity=%s target=%s center_distance=%.2f closest_center_distance=%.2f impact_committed=%s"),
            QAShotId, GetGameTimeSinceCreation(), *GetActorLocation().ToCompactString(),
            Movement ? *Movement->Velocity.ToCompactString() : TEXT("None"),
            *DescribeBasketballActor(IntendedTarget),
            IsValid(IntendedTarget) ? FVector::Distance(GetActorLocation(), IntendedTarget->GetActorLocation()) : -1.0f,
            QAClosestTargetDistance, bImpactCommitted ? TEXT("true") : TEXT("false"));
    }

    const AHeliosEnemyActor* EnemyOwner = Cast<AHeliosEnemyActor>(GetOwner());
    if (!IsValid(EnemyOwner) || EnemyOwner->IsEnemyDead())
    {
        if (bQADiagnostics)
        {
            UE_LOG(LogTemp, Display,
                TEXT("HELIOS_BASKETBALL_QA phase=owner_invalid shot=%d owner=%s impact_committed=%s"),
                QAShotId, *DescribeBasketballActor(EnemyOwner), bImpactCommitted ? TEXT("true") : TEXT("false"));
        }
        bImpactCommitted = true;
        Destroy();
    }
}

void AHeliosBasketballProjectile::BeginPlay()
{
    Super::BeginPlay();
    bQADiagnostics = IsHeliosEnemyQADiagnosticsEnabled();
    if (AActor* ProjectileOwner = GetOwner())
    {
        Collision->IgnoreActorWhenMoving(ProjectileOwner, true);
        if (APawn* OwnerPawn = Cast<APawn>(ProjectileOwner))
        {
            // Move-ignore lists are directional.  The projectile sweep already
            // ignores its owner, but a moving Character capsule must also ignore
            // the newly released ball or it can register as the ball's first hit.
            OwnerPawn->MoveIgnoreActorAdd(this);
            ReciprocalIgnoredOwner = OwnerPawn;
        }
    }
}

void AHeliosBasketballProjectile::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (APawn* OwnerPawn = ReciprocalIgnoredOwner.Get())
    {
        OwnerPawn->MoveIgnoreActorRemove(this);
    }
    ReciprocalIgnoredOwner.Reset();
    if (bQADiagnostics)
    {
        UE_LOG(LogTemp, Display,
            TEXT("HELIOS_BASKETBALL_QA phase=end shot=%d reason=%d age=%.3f projectile=%s velocity=%s target=%s impact_committed=%s damage_applied=%s closest_center_distance=%.2f"),
            QAShotId, static_cast<int32>(EndPlayReason), GetGameTimeSinceCreation(),
            *GetActorLocation().ToCompactString(), Movement ? *Movement->Velocity.ToCompactString() : TEXT("None"),
            *DescribeBasketballActor(IntendedTarget), bImpactCommitted ? TEXT("true") : TEXT("false"),
            bDamageApplied ? TEXT("true") : TEXT("false"), QAClosestTargetDistance);
    }
    Super::EndPlay(EndPlayReason);
}

void AHeliosBasketballProjectile::LaunchAt(AActor* InTarget, float InDamage)
{
    QAShotId = ++GHeliosBasketballQAShotCounter;
    IntendedTarget = InTarget;
    Damage = FMath::Max(0.0f, InDamage);
    const AHeliosEnemyActor* EnemyOwner = Cast<AHeliosEnemyActor>(GetOwner());
    if (!IsValid(InTarget) || !IsValid(EnemyOwner) || EnemyOwner->IsEnemyDead())
    {
        Destroy();
        return;
    }

    const FVector Start = GetActorLocation();
    const FVector End = InTarget->GetActorLocation();
    const float SpeedMultiplier = FMath::Clamp(FlightSpeedMultiplier, 0.1f, 4.0f);
    const float FlightTime = FMath::Clamp(FVector::Dist2D(Start, End) / 900.0f, 0.45f, 1.35f) / SpeedMultiplier;
    PlannedFlightTime = FlightTime;
    Movement->MaxSpeed = 2200.0f * SpeedMultiplier;
    const float GravityZ = GetWorld()->GetGravityZ() * Movement->ProjectileGravityScale;
    FVector Velocity = (End - Start) / FlightTime;
    Velocity.Z -= 0.5f * GravityZ * FlightTime;
    Movement->Velocity = Velocity;

    QAClosestTargetDistance = FVector::Distance(Start, End);
    if (bQADiagnostics)
    {
        const USkeletalMeshComponent* OwnerMesh = EnemyOwner->GetMesh();
        const bool bHasBallSocket = OwnerMesh && OwnerMesh->DoesSocketExist(TEXT("BallSocket"));
        const FTransform BallSocketTransform = bHasBallSocket
            ? OwnerMesh->GetSocketTransform(TEXT("BallSocket"), RTS_World)
            : FTransform::Identity;
        const ACharacter* TargetCharacter = Cast<ACharacter>(InTarget);
        const UCapsuleComponent* TargetCapsule = TargetCharacter ? TargetCharacter->GetCapsuleComponent() : nullptr;
        const FBox TargetBounds = InTarget->GetComponentsBoundingBox(true);
        TArray<AActor*> InitialOverlaps;
        Collision->GetOverlappingActors(InitialOverlaps);
        FString OverlapActors;
        for (const AActor* Overlap : InitialOverlaps)
        {
            if (!OverlapActors.IsEmpty())
            {
                OverlapActors += TEXT(";");
            }
            OverlapActors += DescribeBasketballActor(Overlap);
        }
        UE_LOG(LogTemp, Display,
            TEXT("HELIOS_BASKETBALL_QA phase=launch shot=%d projectile=%s owner=%s owner_yaw=%.2f socket_exists=%s socket_location=%s socket_rotation=%s socket_scale=%s target=%s target_bounds_center=%s target_bounds_extent=%s capsule_radius=%.2f capsule_half_height=%.2f distance2d=%.2f flight_time=%.3f gravity_z=%.2f velocity=%s predicted_end=%s initial_overlaps=[%s]"),
            QAShotId, *GetActorLocation().ToCompactString(), *DescribeBasketballActor(EnemyOwner),
            EnemyOwner->GetActorRotation().Yaw, bHasBallSocket ? TEXT("true") : TEXT("false"),
            *BallSocketTransform.GetLocation().ToCompactString(),
            *BallSocketTransform.Rotator().ToCompactString(),
            *BallSocketTransform.GetScale3D().ToCompactString(), *DescribeBasketballActor(InTarget),
            *TargetBounds.GetCenter().ToCompactString(), *TargetBounds.GetExtent().ToCompactString(),
            TargetCapsule ? TargetCapsule->GetScaledCapsuleRadius() : -1.0f,
            TargetCapsule ? TargetCapsule->GetScaledCapsuleHalfHeight() : -1.0f,
            FVector::Dist2D(Start, End), FlightTime, GravityZ, *Velocity.ToCompactString(),
            *(Start + Velocity * FlightTime + FVector(0.0f, 0.0f, 0.5f * GravityZ * FlightTime * FlightTime)).ToCompactString(),
            *OverlapActors);
    }
}

void AHeliosBasketballProjectile::OnBallHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
    UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
    if (bImpactCommitted)
    {
        return;
    }
    if (OtherActor == GetOwner())
    {
        if (bQADiagnostics)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("HELIOS_BASKETBALL_QA phase=ignored_owner_hit shot=%d age=%.3f projectile=%s owner=%s component=%s impact=%s penetrating=%s"),
                QAShotId, GetGameTimeSinceCreation(), *GetActorLocation().ToCompactString(),
                *DescribeBasketballActor(OtherActor), *GetNameSafe(OtherComponent),
                *Hit.ImpactPoint.ToCompactString(), Hit.bStartPenetrating ? TEXT("true") : TEXT("false"));
        }
        return;
    }
    bImpactCommitted = true;

    const AHeliosEnemyActor* EnemyOwner = Cast<AHeliosEnemyActor>(GetOwner());
    const bool bOwnerCanDamage = IsValid(EnemyOwner) && !EnemyOwner->IsEnemyDead();
    const AHeliosArenaCharacter* ArenaTargetBefore = Cast<AHeliosArenaCharacter>(IntendedTarget);
    const float HealthBefore = ArenaTargetBefore ? ArenaTargetBefore->GetHealth() : -1.0f;
    float DamageDelivered = 0.0f;
    if (bOwnerCanDamage && OtherActor == IntendedTarget && !Cast<AShooterNPC>(OtherActor))
    {
        AController* InstigatorController = GetInstigatorController();
        DamageDelivered = UGameplayStatics::ApplyDamage(
            OtherActor, Damage, InstigatorController, this, UDamageType::StaticClass());
        bDamageApplied = DamageDelivered > 0.0f;
    }
    if (bQADiagnostics)
    {
        const AHeliosArenaCharacter* ArenaTargetAfter = Cast<AHeliosArenaCharacter>(IntendedTarget);
        UE_LOG(LogTemp, Display,
            TEXT("HELIOS_BASKETBALL_QA phase=hit shot=%d age=%.3f projectile=%s velocity=%s other=%s component=%s component_class=%s impact=%s hit_location=%s normal=%s penetrating=%s target=%s owner_can_damage=%s exact_target=%s damage=%.1f damage_return=%.1f damage_applied=%s health_before=%.1f health_after=%.1f closest_center_distance=%.2f"),
            QAShotId, GetGameTimeSinceCreation(), *GetActorLocation().ToCompactString(),
            Movement ? *Movement->Velocity.ToCompactString() : TEXT("None"), *DescribeBasketballActor(OtherActor),
            *GetNameSafe(OtherComponent), OtherComponent ? *OtherComponent->GetClass()->GetPathName() : TEXT("None"),
            *Hit.ImpactPoint.ToCompactString(), *Hit.Location.ToCompactString(), *Hit.ImpactNormal.ToCompactString(),
            Hit.bStartPenetrating ? TEXT("true") : TEXT("false"), *DescribeBasketballActor(IntendedTarget),
            bOwnerCanDamage ? TEXT("true") : TEXT("false"), OtherActor == IntendedTarget ? TEXT("true") : TEXT("false"),
            Damage, DamageDelivered, bDamageApplied ? TEXT("true") : TEXT("false"), HealthBefore,
            ArenaTargetAfter ? ArenaTargetAfter->GetHealth() : -1.0f, QAClosestTargetDistance);
    }
    Destroy();
}
