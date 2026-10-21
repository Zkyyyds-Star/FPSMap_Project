// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterWeapon.h"
#include "Kismet/KismetMathLibrary.h"
#include "Engine/World.h"
#include "ShooterProjectile.h"
#include "ShooterWeaponHolder.h"
#include "Components/SceneComponent.h"
#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
bool IsShooterWeaponQADiagnosticsEnabled()
{
	FString Mode;
	return FParse::Value(FCommandLine::Get(), TEXT("HeliosEnemyQA="), Mode)
		|| FParse::Param(FCommandLine::Get(), TEXT("HeliosEnemyQA"));
}

FString DescribeWeaponHitActor(const AActor* Actor)
{
	if (!Actor)
	{
		return TEXT("None");
	}
	FString Result = FString::Printf(TEXT("%s class=%s location=%s"),
		*Actor->GetPathName(), *Actor->GetClass()->GetPathName(), *Actor->GetActorLocation().ToCompactString());
	if (const ACharacter* Character = Cast<ACharacter>(Actor))
	{
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			Result += FString::Printf(TEXT(" capsule_location=%s capsule_radius=%.2f capsule_half_height=%.2f collision=%d visibility_response=%d world_dynamic_response=%d"),
				*Capsule->GetComponentLocation().ToCompactString(), Capsule->GetScaledCapsuleRadius(),
				Capsule->GetScaledCapsuleHalfHeight(), static_cast<int32>(Capsule->GetCollisionEnabled()),
				static_cast<int32>(Capsule->GetCollisionResponseToChannel(ECC_Visibility)),
				static_cast<int32>(Capsule->GetCollisionResponseToChannel(ECC_WorldDynamic)));
		}
	}
	return Result;
}
}

AShooterWeapon::AShooterWeapon()
{
	PrimaryActorTick.bCanEverTick = true;

	// create the root
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// create the first person mesh
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));
	FirstPersonMesh->SetupAttachment(RootComponent);

	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	FirstPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
	FirstPersonMesh->bOnlyOwnerSee = true;

	// create the third person mesh
	ThirdPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Third Person Mesh"));
	ThirdPersonMesh->SetupAttachment(RootComponent);

	ThirdPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	ThirdPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::WorldSpaceRepresentation);
	ThirdPersonMesh->bOwnerNoSee = true;
}

void AShooterWeapon::BeginPlay()
{
	Super::BeginPlay();

	// subscribe to the owner's destroyed delegate
	if (!GetOwner())
	{
		Destroy();
		return;
	}
	GetOwner()->OnDestroyed.AddDynamic(this, &AShooterWeapon::OnOwnerDestroyed);

	// cast the weapon owner
	WeaponOwner = Cast<IShooterWeaponHolder>(GetOwner());
	PawnOwner = Cast<APawn>(GetOwner());

	// fill the first ammo clip
	CurrentBullets = MagazineSize;

	// attach the meshes to the owner
	if (!WeaponOwner)
	{
		Destroy();
		return;
	}
	WeaponOwner->AttachWeaponMeshes(this);
}

void AShooterWeapon::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear the refire timer
	GetWorld()->GetTimerManager().ClearTimer(RefireTimer);
}

void AShooterWeapon::OnOwnerDestroyed(AActor* DestroyedActor)
{
	// ensure this weapon is destroyed when the owner is destroyed
	Destroy();
}

void AShooterWeapon::ActivateWeapon(const FName& OwnerTag)
{
	// save the owner tag for perception noise detection
	NoiseOwnerTag = OwnerTag;

	// unhide this weapon
	SetActorHiddenInGame(false);

	// notify the owner
	WeaponOwner->OnWeaponActivated(this);
}

void AShooterWeapon::DeactivateWeapon()
{
	// ensure we're no longer firing this weapon while deactivated
	StopFiring();

	// hide the weapon
	SetActorHiddenInGame(true);

	// notify the owner
	WeaponOwner->OnWeaponDeactivated(this);
}

void AShooterWeapon::StartFiring()
{
	// raise the firing flag
	bIsFiring = true;
	if (CurrentBullets <= 0)
	{
		StopFiring();
		return;
	}

	// check how much time has passed since we last shot
	// this may be under the refire rate if the weapon shoots slow enough and the player is spamming the trigger
	const float TimeSinceLastShot = GetWorld()->GetTimeSeconds() - TimeOfLastShot;

	if (TimeSinceLastShot >= RefireRate)
	{
		// fire the weapon right away
		Fire();

	} else {
		// A quick release/repress during cooldown must still fire once the cooldown
		// expires. StopFiring clears this timer if the trigger is released again.
		const float RemainingCooldown = FMath::Max(KINDA_SMALL_NUMBER, RefireRate - TimeSinceLastShot);
		GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::Fire,
			RemainingCooldown, false);
	}
}

void AShooterWeapon::StopFiring()
{
	// lower the firing flag
	bIsFiring = false;

	// clear the refire timer
	GetWorld()->GetTimerManager().ClearTimer(RefireTimer);
}

void AShooterWeapon::Fire()
{
	// ensure the player still wants to fire. They may have let go of the trigger
	if (!bIsFiring || !WeaponOwner || CurrentBullets <= 0)
	{
		if (CurrentBullets <= 0)
		{
			StopFiring();
		}
		return;
	}
	
	// fire a projectile at the target
	const int32 BulletsBeforeShot = CurrentBullets;
    bShotCommitted = false;
	FireProjectile(WeaponOwner->GetWeaponTargetLocation());
	if (bShotCommitted && CurrentBullets < BulletsBeforeShot) OnShotPresented.Broadcast();

	// update the time of our last shot
	TimeOfLastShot = GetWorld()->GetTimeSeconds();

	// make noise so the AI perception system can hear us
	MakeNoise(ShotLoudness, PawnOwner, PawnOwner->GetActorLocation(), ShotNoiseRange, NoiseOwnerTag);

	// are we full auto?
	if (bFullAuto)
	{
		// schedule the next shot
		GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::Fire, RefireRate, false);
	} else {

		// for semi-auto weapons, schedule the cooldown notification
		GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::FireCooldownExpired, RefireRate, false);

	}
}

void AShooterWeapon::FireCooldownExpired()
{
	// notify the owner
	WeaponOwner->OnSemiWeaponRefire();
}

void AShooterWeapon::FireProjectile(const FVector& TargetLocation)
{
	const bool bQADiagnosticShot = PawnOwner && PawnOwner->ActorHasTag(TEXT("Player"))
		&& IsShooterWeaponQADiagnosticsEnabled() && QADiagnosticShotIndex < 30;
	const int32 ShotIndex = bQADiagnosticShot ? ++QADiagnosticShotIndex : -2;
	// get the projectile transform
	FTransform ProjectileTransform = CalculateProjectileSpawnTransform(TargetLocation);
	if (bQADiagnosticShot)
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		PawnOwner->GetActorEyesViewPoint(ViewLocation, ViewRotation);
		FHitResult CameraHit;
		FCollisionQueryParams CameraParams(SCENE_QUERY_STAT(ShooterQACameraTrace), true, PawnOwner);
		CameraParams.AddIgnoredActor(this);
		GetWorld()->LineTraceSingleByChannel(CameraHit, ViewLocation,
			ViewLocation + ViewRotation.Vector() * 10000.0f, ECC_Visibility, CameraParams);
		UE_LOG(LogTemp, Display,
			TEXT("HELIOS_WEAPON_QA phase=fire shot=%d weapon=%s owner=%s owner_location=%s eye=%s view_rotation=%s control_rotation=%s target_location=%s muzzle=%s spawn=%s spawn_rotation=%s camera_hit=%s camera_impact=%s"),
			ShotIndex, *GetPathName(), *GetPathNameSafe(PawnOwner), *PawnOwner->GetActorLocation().ToCompactString(),
			*ViewLocation.ToCompactString(), *ViewRotation.ToCompactString(),
			PawnOwner->GetController() ? *PawnOwner->GetController()->GetControlRotation().ToCompactString() : TEXT("None"),
			*TargetLocation.ToCompactString(), *FirstPersonMesh->GetSocketLocation(MuzzleSocketName).ToCompactString(),
			*ProjectileTransform.GetLocation().ToCompactString(), *ProjectileTransform.Rotator().ToCompactString(),
			*DescribeWeaponHitActor(CameraHit.GetActor()), *CameraHit.ImpactPoint.ToCompactString());
	}
	
	// spawn the projectile
	// Damage must be configured before BeginPlay resolves an initial overlap.
    AShooterProjectile* Projectile = GetWorld()->SpawnActorDeferred<AShooterProjectile>(ProjectileClass,
        ProjectileTransform, GetOwner(), PawnOwner, ESpawnActorCollisionHandlingMethod::AlwaysSpawn,
        ESpawnActorScaleMethod::OverrideRootScale);

	// set the noise tag on the projectile
	if (Projectile)
	{
        bShotCommitted = true;
        Projectile->ConfigureDamageForWeapon(this);
		Projectile->SetNoiseTag(NoiseOwnerTag);
		Projectile->SetQADiagnosticShotIndex(ShotIndex);
        if (HasExternalPresentation())
        {
            OnProjectilePresented.Broadcast(Projectile);
        }
        Projectile->FinishSpawning(ProjectileTransform);
	}

	// play the firing montage
	WeaponOwner->PlayFiringMontage(FiringMontage);

	// add recoil
	WeaponOwner->AddWeaponRecoil(FiringRecoil);

	// consume bullets
	--CurrentBullets;

	// update the weapon HUD
	WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
}

void AShooterWeapon::ConfigureMagazine(int32 InMagazineSize)
{
	MagazineSize = FMath::Max(1, InMagazineSize);
	CurrentBullets = MagazineSize;
	if (WeaponOwner)
	{
		WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
	}
}

void AShooterWeapon::ReloadMagazine()
{
	CurrentBullets = MagazineSize;
	if (WeaponOwner)
	{
		WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
	}
}

void AShooterWeapon::SetMagazineBulletCount(int32 InBullets)
{
	CurrentBullets = FMath::Clamp(InBullets, 0, MagazineSize);
	if (WeaponOwner) WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
}

FTransform AShooterWeapon::CalculateProjectileSpawnTransform(const FVector& TargetLocation) const
{
	// find the muzzle location
	const FVector MuzzleLoc = FirstPersonMesh->GetSocketLocation(MuzzleSocketName);

	// calculate the spawn location ahead of the muzzle
	FVector SpawnLoc = MuzzleLoc + ((TargetLocation - MuzzleLoc).GetSafeNormal() * MuzzleOffset);

	// At point-blank range a first-person muzzle can extend into the target capsule.
	// Sweep from the authoritative view towards the normal muzzle spawn and move the
	// projectile center to the first obstruction. This also keeps a nearby wall from
	// being skipped by a weapon mesh which visually crosses it.
	if (PawnOwner && PawnOwner->ActorHasTag(TEXT("Player")) && GetWorld())
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		PawnOwner->GetActorEyesViewPoint(ViewLocation, ViewRotation);

		float ProjectileRadius = 16.0f;
		if (ProjectileClass)
		{
			if (const AShooterProjectile* ProjectileDefault = ProjectileClass.GetDefaultObject())
			{
				if (const USphereComponent* Sphere = ProjectileDefault->FindComponentByClass<USphereComponent>())
				{
					ProjectileRadius = Sphere->GetUnscaledSphereRadius();
				}
			}
		}

		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ShooterMuzzleObstruction), false, PawnOwner);
		QueryParams.AddIgnoredActor(this);
		FHitResult Obstruction;
		if (GetWorld()->SweepSingleByChannel(Obstruction, ViewLocation, SpawnLoc, FQuat::Identity,
			ECC_Visibility, FCollisionShape::MakeSphere(ProjectileRadius), QueryParams))
		{
			SpawnLoc = Obstruction.Location;
		}
	}

	// find the aim rotation vector while applying some variance to the target 
	const FRotator AimRot = UKismetMathLibrary::FindLookAtRotation(SpawnLoc, TargetLocation + (UKismetMathLibrary::RandomUnitVector() * AimVariance));

	// return the built transform
	return FTransform(AimRot, SpawnLoc, FVector::OneVector);
}

const TSubclassOf<UAnimInstance>& AShooterWeapon::GetFirstPersonAnimInstanceClass() const
{
	return FirstPersonAnimInstanceClass;
}

const TSubclassOf<UAnimInstance>& AShooterWeapon::GetThirdPersonAnimInstanceClass() const
{
	return ThirdPersonAnimInstanceClass;
}
