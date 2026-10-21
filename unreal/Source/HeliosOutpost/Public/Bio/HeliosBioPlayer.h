#pragma once
#include "CoreMinimal.h"
#include "Player/HeliosCharacter.h"
#include "ShooterWeaponHolder.h"
#include "Player/HeliosLoadout.h"
#include "Bio/HeliosBioRules.h"
#include "Weapons/HeliosFighterWeapon.h"
#include "HeliosBioPlayer.generated.h"
class AShooterWeapon;
class UHeliosCharacterPresentationComponent;
class UHeliosFirstPersonPresentationComponent;
class UHeliosWeaponDefinition;
class USkeletalMeshComponent;
class UAnimSequence;
class UHeliosBioFourSlashComponent;
class UHeliosBioRoundInventoryComponent;

/** Floating bio crouch keeps the configured instance standing capsule. */
UCLASS()
class HELIOSOUTPOST_API UHeliosBioPlayerMovement : public UHeliosFighterMovement
{
    GENERATED_BODY()
public:
    virtual void Crouch(bool bClientSimulation=false) override;
    virtual void UnCrouch(bool bClientSimulation=false) override;
};

UCLASS()
class HELIOSOUTPOST_API AHeliosBioPlayer : public AHeliosCharacter, public IShooterWeaponHolder
{
    GENERATED_BODY()
public:
    AHeliosBioPlayer(const FObjectInitializer& ObjectInitializer=FObjectInitializer::Get());
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void CalcCamera(float Dt,FMinimalViewInfo& Result) override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    virtual float TakeDamage(float Damage,const FDamageEvent& Event,AController* Instigator,AActor* Causer) override;
    int32 ParticipantId = 0;
    int32 ShotsFired = 0, MeleeAttacks = 0;
    int32 GetAmmo() const;
    int32 GetReserveAmmo() const { return ReserveAmmo; }
    float GetHealth() const;
    bool EquipBioLoadout(const FHeliosLoadout& Loadout);
    UHeliosBioRoundInventoryComponent* GetRoundInventory() const{return RoundInventory;}
    void StartBioFire();
    void StopBioFire();
    void ReloadBio();
    void TryTransform();
    void UseBioSkill();
    void StartHeavy();
    void StopBioSecondary();
    void StartBioSpecial();
    void SelectBioMelee();
    void SelectBioPrimary();
    bool BeginBioFourSlash();
    UHeliosBioFourSlashComponent* GetFourSlash() const { return FourSlash; }
    void ResetForBioRound();
    void RequestBioRestart(); // Public user action shared by F5 binding and runtime acceptance.
    void AddSupplyAmmo();
    AShooterWeapon* GetEquippedWeapon() const { return Weapon; }
    bool IsReloading() const { return ReloadRemaining>0; }
    float GetBioReloadRemaining() const { return ReloadRemaining; }
    float GetBioAttackCooldown() const { return MeleeCooldown; }
    int32 GetSpectatedParticipantId() const { return SpectatedParticipantId; }
    void SpectatePrevious();
    void SpectateNext();
    virtual void AttachWeaponMeshes(AShooterWeapon* InWeapon) override;
    virtual void PlayFiringMontage(UAnimMontage* Montage) override;
    virtual void AddWeaponRecoil(float Recoil) override;
    virtual void UpdateWeaponHUD(int32 Ammo,int32 Magazine) override;
    virtual FVector GetWeaponTargetLocation() override;
    virtual void AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass) override;
    virtual void OnWeaponActivated(AShooterWeapon* InWeapon) override;
    virtual void OnWeaponDeactivated(AShooterWeapon* InWeapon) override;
    virtual void OnSemiWeaponRefire() override;
protected:
    virtual void QuitPlay() override;
private:
    friend class UHeliosBioRoundInventoryComponent;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UHeliosBioRoundInventoryComponent> RoundInventory;
    bool bPreparingInventoryWeapon=false;
    void SelectRoundSlot1();
    void SelectRoundSlot2();
    void SelectRoundSlot3();
    void SelectRoundSlot4();
    void SelectRoundSlot5();
    void NextRoundWeapon();
    void PreviousRoundWeapon();
    friend class UHeliosBioPlayerMovement;
    bool UsesConfiguredBioCrouch() const;
    bool ApplyConfiguredBioCrouch(bool Crouched);
    void ResetBioCrouchState();
    bool ApplyViewProfile();
    float GetPlayerViewAspectRatio() const;
    double CrouchBlockLogAt=-100;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UHeliosBioFourSlashComponent> FourSlash;
    UPROPERTY(Transient) TObjectPtr<AShooterWeapon> Weapon;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UHeliosCharacterPresentationComponent> HumanPresentation;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UHeliosFirstPersonPresentationComponent> WeaponPresentation;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> BioArms;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> BioIdle;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> BioAttack;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> BioHeavy;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> BioRecover;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> BioMove;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> BioSkill;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> BioSkillLoop;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> BioSkillEnd;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> CurrentBioLocomotion;
    FHeliosLoadout Loadout;
    int32 ReserveAmmo = 600,MagazineSize = 30,LastAmmo = 30;
    float ReloadRemaining = 0,ReloadDuration = 2.f,MeleeCooldown = 0,AttackAge = 100.f;
    bool bHoldingFire = false,bLastAlive = true,bReloadCommit = false,bRecovering = false,bPlayRecovery = false;
    bool bBioPrimary = false;
    bool bSkillMeshOverride = false;
    bool bWasFrozen = false;
    float FrozenSavedAcceleration = 0.f;
    int32 BioSkillStage = 0;
    FName LastRoleId=NAME_None;
    int32 SpectatedParticipantId=INDEX_NONE;
    EHeliosBioTeam LastTeam = EHeliosBioTeam::Human;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> CurrentBioClip;
    void Melee(bool Heavy);
    void FireBioPrimary();
    void ApplyTeamPresentation(EHeliosBioTeam Team,bool Force=false);
    void CrouchBio();
    void CycleSpectator(int32 Direction);
};
