#pragma once

#include "CoreMinimal.h"
class UHeliosGuanXiaoYuAbilityComponent;
class UHeliosQingYaAbilityComponent;
#include "Player/HeliosCharacter.h"
#include "Player/HeliosLoadout.h"
#include "ShooterWeaponHolder.h"
#include "HeliosArenaCharacter.generated.h"

class AShooterWeapon;
class UAnimMontage;
class UHeliosWeaponDefinition;
class UHeliosFirstPersonPresentationComponent;
class UHeliosCharacterPresentationComponent;

UCLASS(BlueprintType)
class HELIOSOUTPOST_API AHeliosArenaCharacter : public AHeliosCharacter, public IShooterWeaponHolder
{
    GENERATED_BODY()

public:
    void ToggleGuanXiaoYuUmbrella();
    UHeliosGuanXiaoYuAbilityComponent* GetGuanXiaoYuAbility()const{return GuanXiaoYuAbility;}
    UPROPERTY(VisibleAnywhere) TObjectPtr<UHeliosGuanXiaoYuAbilityComponent> GuanXiaoYuAbility;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UHeliosQingYaAbilityComponent> QingYaAbility;
    UHeliosQingYaAbilityComponent* GetQingYaAbility()const{return QingYaAbility;}
    void StartQingYaGunGesture();
    bool IsQingYaBusy() const;
    AHeliosArenaCharacter();

    virtual void BeginPlay() override;
    virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
        AController* EventInstigator, AActor* DamageCauser) override;

    virtual void AttachWeaponMeshes(AShooterWeapon* WeaponToAttach) override;
    virtual void PlayFiringMontage(UAnimMontage* Montage) override;
    virtual void AddWeaponRecoil(float Recoil) override;
    virtual void UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize) override;
    virtual FVector GetWeaponTargetLocation() override;
    virtual void AddWeaponClass(const TSubclassOf<AShooterWeapon>& InWeaponClass) override;
    virtual void OnWeaponActivated(AShooterWeapon* InWeapon) override;
    virtual void OnWeaponDeactivated(AShooterWeapon* InWeapon) override;
    virtual void OnSemiWeaponRefire() override;

    UFUNCTION(BlueprintPure, Category="Helios|Combat")
    float GetHealth() const { return CurrentHealth; }

    UFUNCTION(BlueprintPure, Category="Helios|Combat")
    float GetMaxHealth() const { return MaxHealth; }

    UFUNCTION(BlueprintPure, Category="Helios|Combat")
    int32 GetAmmo() const { return CurrentAmmo; }

    UFUNCTION(BlueprintPure, Category="Helios|Combat")
    bool IsReloading() const;

    UFUNCTION(BlueprintPure, Category="Helios|Combat")
    bool IsDead() const { return bDead; }

    UFUNCTION(BlueprintCallable, Category="Helios|Combat")
    void ResetForTest();

    UFUNCTION(BlueprintCallable, Category="Helios|Combat")
    bool EquipWeaponDefinition(UHeliosWeaponDefinition* Definition);

    UFUNCTION(BlueprintCallable, Category="Helios|Loadout")
    bool EquipLoadout(const FHeliosLoadout& Loadout);

    UFUNCTION(BlueprintPure, Category="Helios|Loadout")
    FHeliosLoadout GetLoadout() const;

    UHeliosCharacterPresentationComponent* GetCharacterPresentation() const { return CharacterPresentation; }
    void ToggleCharacterForm();
    void StartCharacterInteraction();
    void SetEquipmentMovementMultiplier(float Multiplier);

    UFUNCTION(BlueprintPure, Category="Helios|Combat")
    FName GetWeaponId() const { return CurrentWeaponId; }

    UFUNCTION(BlueprintPure, Category="Helios|Combat")
    FText GetWeaponDisplayName() const { return CurrentWeaponDisplayName; }

    UFUNCTION(BlueprintPure, Category="Helios|Combat")
    int32 GetMagazineSize() const { return MagazineSize; }

    UHeliosFirstPersonPresentationComponent* GetPresentation() const { return Presentation; }

    float Heal(float Amount);
    AShooterWeapon* GetEquippedWeapon() const { return Weapon; }
    void StartSecondaryAttack();
    void StartSpecialAttack();
    void StopSecondaryAttack();
    void ToggleCalamityBackup();
    void ToggleZhongXiaoKuiRanged();
    void StartZhongXiaoKuiWave();
    void CancelCombatAndControl();

    /** Input-equivalent combat actions used by the opt-in Codex bridge. */
    void StartFireFromAutomation();
    void StopFireFromAutomation();
    void StartReloadFromAutomation();
    void JumpFromAutomation();
    void CancelAutomationCombat();

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Helios|Presentation")
    TObjectPtr<UHeliosFirstPersonPresentationComponent> Presentation;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Helios|Presentation")
    TObjectPtr<UHeliosCharacterPresentationComponent> CharacterPresentation;

    virtual bool OnPlayerDefeated() { return false; }
    virtual void OnCombatCue(FName Cue) {}
    UPROPERTY(EditDefaultsOnly, Category="Helios|Combat")
    TSubclassOf<AShooterWeapon> WeaponClass;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Helios|Combat")
    TObjectPtr<AShooterWeapon> Weapon;

    UPROPERTY(Transient)
    TObjectPtr<UHeliosWeaponDefinition> EquippedWeaponDefinition;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Helios|Combat")
    float MaxHealth = 100.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Helios|Combat")
    int32 MagazineSize = 30;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Helios|Combat", meta=(Units="s"))
    float ReloadSeconds = 1.1f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Helios|Combat", meta=(Units="s"))
    float DeathResetSeconds = 2.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Helios|Combat", meta=(Units="s"))
    float SpawnProtectionSeconds = 3.0f;

private:
    void StartFire();
    void StopFire();
    void StartReload();
    void FinishReload();
    void HandleDeath();
    void FinishDeathReset();
    void SpawnConfiguredWeapon();
    void SyncActiveLoadout();
    void RefreshCharacterMovementMultiplier();

    float CurrentHealth = 100.0f;
    int32 CurrentAmmo = 30;
    bool bTriggerHeld = false;
    bool bReloading = false;
    bool bDead = false;
    double SpawnProtectionEndTime = 0.0;
    FTimerHandle ReloadTimer;
    FTimerHandle DeathResetTimer;
    FHeliosLoadout CurrentLoadout;
    float WeaponMovementMultiplier = 1.f;
    FName CurrentWeaponId = TEXT("cfm_chan_zeus");
    FText CurrentWeaponDisplayName = NSLOCTEXT("HeliosWeapons", "DefaultZeusName", "宙斯");
    FVector WeaponAttachmentLocation = FVector(35.0f, 14.0f, -18.0f);
    FRotator WeaponAttachmentRotation = FRotator(0.0f, -90.0f, 0.0f);
    FVector WeaponAttachmentScale = FVector(0.8f);
};
