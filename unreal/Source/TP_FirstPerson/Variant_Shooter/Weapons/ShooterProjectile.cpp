// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterProjectile.h"
#include "Combat/CombatDamageSettings.h"
#include "ShooterWeapon.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "ShooterNPC.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
bool IsShooterProjectileQADiagnosticsEnabled()
{
	FString Mode;
	return FParse::Value(FCommandLine::Get(), TEXT("HeliosEnemyQA="), Mode)
		|| FParse::Param(FCommandLine::Get(), TEXT("HeliosEnemyQA"));
}
}

AShooterProjectile::AShooterProjectile()
{
	PrimaryActorTick.bCanEverTick = true;

	// create the collision component and assign it as the root
	RootComponent = CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("Collision Component"));

	CollisionComponent->SetSphereRadius(16.0f);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionComponent->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionComponent->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_No;
	CollisionComponent->bReturnMaterialOnMove = true;

	// create the projectile movement component. No need to attach it because it's not a Scene Component
	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Projectile Movement"));

	ProjectileMovement->InitialSpeed = 3000.0f;
	ProjectileMovement->MaxSpeed = 3000.0f;
	ProjectileMovement->bShouldBounce = true;

	// set the default damage type
	HitDamageType = UDamageType::StaticClass();
}

void AShooterProjectile::ConfigureDamageForWeapon(const AShooterWeapon* Weapon)
{
    if (!bUseTemplateDamageSettings) return;
    using T = ECombatDamageType;
    T Type = bExplodeOnHit ? T::TemplateGrenade : T::TemplateBullet;
    if (Cast<AShooterNPC>(GetInstigator())) Type = bExplodeOnHit ? T::NPCGrenade : T::NPCBullet;
    else if (Weapon)
    {
        // Blueprint subclasses inherit their registered weapon's damage category.
        for (const UClass* Class = Weapon->GetClass(); Class; Class = Class->GetSuperClass())
        {
            const FName Name = Class->GetFName();
            if (Name == TEXT("BP_ShooterWeapon_Rifle_C")) { Type=T::Rifle; break; }
            if (Name == TEXT("BP_ShooterWeapon_Pistol_C")) { Type=T::Pistol; break; }
            if (Name == TEXT("BP_ShooterWeapon_GrenadeLauncher_C")) { Type=T::Grenade; break; }
        }
    }
    HitDamage = UCombatDamageSettings::Resolve(this, Type);
    bDamageConfigured = true;
}

void AShooterProjectile::BeginPlay()
{
	Super::BeginPlay();
    if (bUseTemplateDamageSettings && !bDamageConfigured) ConfigureDamageForWeapon(nullptr);

	// Blueprint projectile assets may add a visible mesh with physics collision.
	// Movement and hit detection belong exclusively to CollisionComponent; allowing
	// a visual primitive to collide leaves its mesh behind after a deferred hit and
	// makes following full-auto rounds collide with the previous projectile.
	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(this);
	for (UPrimitiveComponent* Primitive : PrimitiveComponents)
	{
		if (Primitive && Primitive != CollisionComponent)
		{
			Primitive->SetSimulatePhysics(false);
			Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Primitive->SetGenerateOverlapEvents(false);
			Primitive->SetCanEverAffectNavigation(false);
		}
	}

	// ignore the pawn that shot this projectile
	CollisionComponent->IgnoreActorWhenMoving(GetInstigator(), true);
	if (GetInstigator() && GetInstigator()->IsPlayerControlled())
	{
		TArray<AActor*> Pets;
		UGameplayStatics::GetAllActorsWithTag(this, TEXT("HeliosPet"), Pets);
		for (AActor* Pet : Pets)
			if (!Pet->ActorHasTag(TEXT("BioParticipant"))) CollisionComponent->IgnoreActorWhenMoving(Pet, true);
	}
	ResolveInitialPenetration();
}

void AShooterProjectile::ResolveInitialPenetration()
{
	if (bHit || !GetWorld() || !CollisionComponent)
	{
		return;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ShooterProjectileInitialPenetration), false, this);
	// Initial overlaps must honor the same exclusions as swept movement (e.g. paired throws).
	for (AActor* Ignored : CollisionComponent->GetMoveIgnoreActors()) QueryParams.AddIgnoredActor(Ignored);
	QueryParams.bReturnPhysicalMaterial = true;
	if (!bDamageOwner)
	{
		QueryParams.AddIgnoredActor(GetInstigator());
		QueryParams.AddIgnoredActor(GetOwner());
	}

	const float Radius = CollisionComponent->GetScaledSphereRadius();
	const FVector Start = GetActorLocation();
	const FVector End = Start + GetActorForwardVector() * FMath::Max(1.0f, Radius * 0.25f);
	FHitResult InitialHit;
	if (GetWorld()->SweepSingleByChannel(InitialHit, Start, End, GetActorQuat(),
		ECC_Visibility, FCollisionShape::MakeSphere(Radius), QueryParams)
		&& InitialHit.bBlockingHit)
	{
		NotifyHit(CollisionComponent, InitialHit.GetActor(), InitialHit.GetComponent(), true,
			InitialHit.Location, InitialHit.Normal, FVector::ZeroVector, InitialHit);
	}
}

void AShooterProjectile::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear the destruction timer
	GetWorld()->GetTimerManager().ClearTimer(DestructionTimer);
}

void AShooterProjectile::NotifyHit(class UPrimitiveComponent* MyComp, AActor* Other, class UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	const bool bQADiagnostic = QADiagnosticShotIndex == -1
		? GetOwner() && GetOwner()->ActorHasTag(TEXT("Player")) && IsShooterProjectileQADiagnosticsEnabled()
		: QADiagnosticShotIndex > 0 && QADiagnosticShotIndex <= 30 && IsShooterProjectileQADiagnosticsEnabled();
	const float HealthBefore = Cast<AShooterNPC>(Other) ? CastChecked<AShooterNPC>(Other)->CurrentHP : -1.0f;
	if (bQADiagnostic)
	{
		const ACharacter* HitCharacter = Cast<ACharacter>(Other);
		const UCapsuleComponent* Capsule = HitCharacter ? HitCharacter->GetCapsuleComponent() : nullptr;
		UE_LOG(LogTemp, Display,
			TEXT("HELIOS_PROJECTILE_QA phase=notify_enter shot=%d projectile=%s projectile_location=%s owner=%s instigator=%s already_hit=%s other=%s other_class=%s other_location=%s other_component=%s impact=%s hit_location=%s normal=%s start_penetrating=%s capsule_location=%s capsule_radius=%.2f capsule_half_height=%.2f capsule_collision=%d capsule_visibility_response=%d capsule_world_dynamic_response=%d health_before=%.1f"),
			QADiagnosticShotIndex, *GetPathName(), *GetActorLocation().ToCompactString(), *GetPathNameSafe(GetOwner()),
			*GetPathNameSafe(GetInstigator()), bHit ? TEXT("true") : TEXT("false"), *GetPathNameSafe(Other),
			Other ? *Other->GetClass()->GetPathName() : TEXT("None"),
			Other ? *Other->GetActorLocation().ToCompactString() : TEXT("None"), *GetPathNameSafe(OtherComp),
			*Hit.ImpactPoint.ToCompactString(), *HitLocation.ToCompactString(), *HitNormal.ToCompactString(),
			Hit.bStartPenetrating ? TEXT("true") : TEXT("false"),
			Capsule ? *Capsule->GetComponentLocation().ToCompactString() : TEXT("None"),
			Capsule ? Capsule->GetScaledCapsuleRadius() : -1.0f,
			Capsule ? Capsule->GetScaledCapsuleHalfHeight() : -1.0f,
			Capsule ? static_cast<int32>(Capsule->GetCollisionEnabled()) : -1,
			Capsule ? static_cast<int32>(Capsule->GetCollisionResponseToChannel(ECC_Visibility)) : -1,
			Capsule ? static_cast<int32>(Capsule->GetCollisionResponseToChannel(ECC_WorldDynamic)) : -1,
			HealthBefore);
	}
	// ignore if we've already hit something else
	if (bHit)
	{
		return;
	}

	bHit = true;

	// Freeze the complete projectile actor while deferred visual effects finish.
	// Blueprint child primitives must not remain as invisible blockers at the old
	// muzzle position after the root collision sphere has stopped.
	SetActorEnableCollision(false);
	ProjectileMovement->StopMovementImmediately();
	ProjectileMovement->Deactivate();

	// make AI perception noise
	MakeNoise(NoiseLoudness, GetInstigator(), GetActorLocation(), NoiseRange, NoiseTag);

	if (bExplodeOnHit)
	{
		
		// apply explosion damage centered on the projectile
		ExplosionCheck(GetActorLocation());

	} else {

		// single hit projectile. Process the collided actor
		ProcessHit(Other, OtherComp, Hit.ImpactPoint, -Hit.ImpactNormal);

	}

	// pass control to BP for any extra effects
	if (bExternalPresentation) OnPresentationImpact.Broadcast(Hit);
	else BP_OnProjectileHit(Hit);

	if (bQADiagnostic)
	{
		const float HealthAfter = Cast<AShooterNPC>(Other) ? CastChecked<AShooterNPC>(Other)->CurrentHP : -1.0f;
		UE_LOG(LogTemp, Display,
			TEXT("HELIOS_PROJECTILE_QA phase=notify_processed shot=%d projectile=%s other=%s health_before=%.1f health_after=%.1f damage_delta=%.1f"),
			QADiagnosticShotIndex, *GetPathName(), *GetPathNameSafe(Other), HealthBefore, HealthAfter,
			HealthBefore >= 0.0f && HealthAfter >= 0.0f ? HealthBefore - HealthAfter : -1.0f);
	}

	// check if we should schedule deferred destruction of the projectile
	if (DeferredDestructionTime > 0.0f)
	{
		GetWorld()->GetTimerManager().SetTimer(DestructionTimer, this, &AShooterProjectile::OnDeferredDestruction, DeferredDestructionTime, false);

	} else {

		// destroy the projectile right away
		Destroy();
	}
}

void AShooterProjectile::ExplosionCheck(const FVector& ExplosionCenter)
{
	// do a sphere overlap check look for nearby actors to damage
	TArray<FOverlapResult> Overlaps;

	FCollisionShape OverlapShape;
	OverlapShape.SetSphere(ExplosionRadius);

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	if (!bDamageOwner)
	{
		QueryParams.AddIgnoredActor(GetInstigator());
	}

	GetWorld()->OverlapMultiByObjectType(Overlaps, ExplosionCenter, FQuat::Identity, ObjectParams, OverlapShape, QueryParams);

	TArray<AActor*> DamagedActors;

	// process the overlap results
	for (const FOverlapResult& CurrentOverlap : Overlaps)
	{
		// overlaps may return the same actor multiple times per each component overlapped
		// ensure we only damage each actor once by adding it to a damaged list
		if (DamagedActors.Find(CurrentOverlap.GetActor()) == INDEX_NONE)
		{
			DamagedActors.Add(CurrentOverlap.GetActor());

			// apply physics force away from the explosion
			const FVector& ExplosionDir = CurrentOverlap.GetActor()->GetActorLocation() - GetActorLocation();

			// push and/or damage the overlapped actor
			ProcessHit(CurrentOverlap.GetActor(), CurrentOverlap.GetComponent(), GetActorLocation(), ExplosionDir.GetSafeNormal());
		}
			
	}
}

void AShooterProjectile::ProcessHit(AActor* HitActor, UPrimitiveComponent* HitComp, const FVector& HitLocation, const FVector& HitDirection)
{
	// have we hit a character?
	if (ACharacter* HitCharacter = Cast<ACharacter>(HitActor))
	{
		const AActor* ProjectileOwner = GetOwner();
		const bool bBothEnemies = ProjectileOwner && ProjectileOwner->ActorHasTag(TEXT("Enemy")) && HitCharacter->ActorHasTag(TEXT("Enemy"));
		const bool bBothPlayers = ProjectileOwner && ProjectileOwner->ActorHasTag(TEXT("Player")) && HitCharacter->ActorHasTag(TEXT("Player"));
		// ignore the owner of this projectile
		if (!bBothEnemies && !bBothPlayers && (HitCharacter != GetOwner() || bDamageOwner))
		{
			// apply damage to the character
			APawn* InstigatorPawn = GetInstigator();
			UGameplayStatics::ApplyDamage(HitCharacter, HitDamage, InstigatorPawn ? InstigatorPawn->GetController() : nullptr, this, HitDamageType);
		}
	}

	// have we hit a physics object?
	if (HitComp && HitComp->IsSimulatingPhysics())
	{
		// give some physics impulse to the object
		HitComp->AddImpulseAtLocation(HitDirection * PhysicsForce, HitLocation);
	}
}

void AShooterProjectile::OnDeferredDestruction()
{
	// destroy this actor
	Destroy();
}

void AShooterProjectile::SetNoiseTag(const FName& Tag)
{
	NoiseTag = Tag;
}
