// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TP_FirstPersonCharacter.h"
#include "ShooterWeaponHolder.h"
#include "Health/CreatureHealthSettings.h"
#include "ShooterNPC.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPawnDeathDelegate);

class AShooterWeapon;

/** Opt-in awareness policy; template NPCs keep their existing perception. */
USTRUCT(BlueprintType)
struct TP_FIRSTPERSON_API FShooterAwarenessSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Awareness", meta=(ClampMin="1"))
	float SightDistance = 3000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Awareness", meta=(ClampMin="1"))
	float RetainDistance = 3500.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Awareness", meta=(ClampMin="0", ClampMax="180"))
	float SightHalfAngle = 100.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Awareness", meta=(ClampMin="0"))
	float CloseAwarenessDistance = 400.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Awareness", meta=(ClampMin="0.02"))
	float CheckInterval = 0.1f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Awareness", meta=(ClampMin="0"))
	float MemoryDuration = 3.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Awareness")
	bool bPersistentCombatTracking = false;
};

/**
 *  A simple AI-controlled shooter game NPC
 *  Executes its behavior through a StateTree managed by its AI Controller
 *  Holds and manages a weapon
 */
UCLASS(abstract)
class TP_FIRSTPERSON_API AShooterNPC : public ATP_FirstPersonCharacter, public IShooterWeaponHolder
{
	GENERATED_BODY()

public:

    virtual ECreatureHealthType GetHealthSettingsType() const { return ECreatureHealthType::ShooterNPC; }

	/** Current HP for this character. It dies if it reaches zero through damage */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Damage")
	float CurrentHP = 100.0f;

	virtual const FShooterAwarenessSettings* GetAwarenessSettings() const { return nullptr; }
	virtual bool OwnsNativeCombatMovement() const { return false; }
	/** Ordinary minions keep StateTree engagement, but own combat positioning. */
	virtual bool UsesTacticalCombat() const { return false; }

protected:

	/** Name of the collision profile to use during ragdoll death */
	UPROPERTY(EditAnywhere, Category="Damage")
	FName RagdollCollisionProfile = FName("Ragdoll");

	/** Time to wait after death before destroying this actor */
	UPROPERTY(EditAnywhere, Category="Damage")
	float DeferredDestructionTime = 5.0f;

	/** Native subclasses with authored death clips can opt out of ragdoll. */
	UPROPERTY(EditAnywhere, Category="Damage")
	bool bUseRagdollOnDeath = true;

	/** Team byte for this character */
	UPROPERTY(EditAnywhere, Category="Team")
	uint8 TeamByte = 1;

	/** Actor tag to grant this character when it dies */
	UPROPERTY(EditAnywhere, Category="Team")
	FName DeathTag = FName("Dead");

	/** Pointer to the equipped weapon */
	TObjectPtr<AShooterWeapon> Weapon;

	/** Type of weapon to spawn for this character */
	UPROPERTY(EditAnywhere, Category="Weapon")
	TSubclassOf<AShooterWeapon> WeaponClass;

	/** Name of the first person mesh weapon socket */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category ="Weapons")
	FName FirstPersonWeaponSocket = FName("HandGrip_R");

	/** Name of the third person mesh weapon socket */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category ="Weapons")
	FName ThirdPersonWeaponSocket = FName("HandGrip_R");

	/** Max range for aiming calculations */
	UPROPERTY(EditAnywhere, Category="Aim")
	float AimRange = 10000.0f;

	/** Cone variance to apply while aiming */
	UPROPERTY(EditAnywhere, Category="Aim")
	float AimVarianceHalfAngle = 10.0f;

	/** Minimum vertical offset from the target center to apply when aiming */
	UPROPERTY(EditAnywhere, Category="Aim")
	float MinAimOffsetZ = -35.0f;

	/** Maximum vertical offset from the target center to apply when aiming */
	UPROPERTY(EditAnywhere, Category="Aim")
	float MaxAimOffsetZ = -60.0f;

	/** Actor currently being targeted */
	TObjectPtr<AActor> CurrentAimTarget;

	/** If true, this character is currently shooting its weapon */
	bool bIsShooting = false;

	/** If true, this character has already died */
	bool bIsDead = false;

	/** Deferred destruction on death timer */
	FTimerHandle DeathTimer;

public:

	/** Delegate called when this NPC dies */
	FPawnDeathDelegate OnPawnDeath;

protected:

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Gameplay cleanup */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:

	/** Handle incoming damage */
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

public:

	//~Begin IShooterWeaponHolder interface

	/** Attaches a weapon's meshes to the owner */
	virtual void AttachWeaponMeshes(AShooterWeapon* Weapon) override;

	/** Plays the firing montage for the weapon */
	virtual void PlayFiringMontage(UAnimMontage* Montage) override;

	/** Applies weapon recoil to the owner */
	virtual void AddWeaponRecoil(float Recoil) override;

	/** Updates the weapon's HUD with the current ammo count */
	virtual void UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize) override;

	/** Calculates and returns the aim location for the weapon */
	virtual FVector GetWeaponTargetLocation() override;

	/** Gives a weapon of this class to the owner */
	virtual void AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass) override;

	/** Activates the passed weapon */
	virtual void OnWeaponActivated(AShooterWeapon* Weapon) override;

	/** Deactivates the passed weapon */
	virtual void OnWeaponDeactivated(AShooterWeapon* Weapon) override;

	/** Notifies the owner that the weapon cooldown has expired and it's ready to shoot again */
	virtual void OnSemiWeaponRefire() override;

	//~End IShooterWeaponHolder interface

protected:

	/** Called when HP is depleted and the character should die */
	virtual void Die();

	/** Called after death to destroy the actor */
	void DeferredDestruction();

public:

	/** Signals this character to start shooting at the passed actor */
	virtual void StartShooting(AActor* ActorToShoot);

	/** Signals this character to stop shooting */
	virtual void StopShooting();

	/** Read-only combat state for project-owned directors and QA. */
	bool IsDead() const { return bIsDead; }
	AShooterWeapon* GetEquippedWeapon() const { return Weapon; }
};
