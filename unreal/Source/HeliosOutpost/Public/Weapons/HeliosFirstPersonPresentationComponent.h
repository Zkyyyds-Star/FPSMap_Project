#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HeliosFirstPersonPresentationComponent.generated.h"

class AShooterWeapon;
class AShooterProjectile;
class AHeliosArenaCharacter;
class ACharacter;
class UHeliosWeaponDefinition;
class UHeliosWeaponPresentationProfile;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UAnimSequence;
class UNiagaraSystem;
class USoundBase;

/** Owner-only hands and moving weapon parts; never owns ammunition or hit logic. */
UCLASS(ClassGroup=(Helios), meta=(BlueprintSpawnableComponent))
class HELIOSOUTPOST_API UHeliosFirstPersonPresentationComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UHeliosFirstPersonPresentationComponent();
    bool Equip(AHeliosArenaCharacter* Character, AShooterWeapon* InWeapon, const UHeliosWeaponDefinition* Definition);
    bool EquipCompanion(ACharacter* Character, USceneComponent* ViewAnchor, AShooterWeapon* InWeapon, const UHeliosWeaponDefinition* Definition);
    void BeginReload(float Duration, bool bEmpty);
    void FinishReload();
    void Cancel(bool bHide = false);
    void ReplayEquip();
    void BeginThrow();
    void CommitThrow();
    void CancelThrow();
    void Unequip();
    void SetMeleePose(UAnimSequence* Animation, float Progress);
    FVector GetMeleeTip() const;
    void ApplyMeleeImpact(float Strength) { Kick=FMath::Max(Kick,Strength); }
    bool IsReady() const { return bReady; }
    bool IsReloadPlaying() const { return bReloading; }
    float GetReloadProgress() const;
    int32 GetShotPresentationCount() const { return ShotPresentationCount; }
    USkeletalMeshComponent* GetArmsMesh() const { return Arms; }
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    bool EquipHolder(ACharacter* Character, USceneComponent* ViewAnchor, AShooterWeapon* InWeapon, const UHeliosWeaponDefinition* Definition, bool bCompanion);
    void OnShot();
    void OnProjectile(AShooterProjectile* Projectile);
    void Pose(UAnimSequence* ArmsAnimation, UAnimSequence* GunAnimation, float Alpha, bool bLoop);
    void PlayCue(USoundBase* Sound, float Volume = 0.65f);
    void EjectCasing();
    UPROPERTY(Transient) TObjectPtr<ACharacter> Player;
    UPROPERTY(Transient) TObjectPtr<USceneComponent> HolderView;
    UPROPERTY(Transient) TObjectPtr<AShooterWeapon> Weapon;
    UPROPERTY(Transient) TObjectPtr<UHeliosWeaponPresentationProfile> Profile;
    UPROPERTY(Transient) TObjectPtr<USceneComponent> PoseRoot;
    UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> Arms;
    UPROPERTY(Transient) TObjectPtr<UPointLightComponent> MuzzleLight;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Casings;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> Idle;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> Reload;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> Fire;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> Raise;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> GunReload;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> GunFire;
    UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> MuzzleFX;
    UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> ImpactFX;
    UPROPERTY(Transient) TObjectPtr<USoundBase> ShotSound;
    UPROPERTY(Transient) TObjectPtr<USoundBase> OutSound;
    UPROPERTY(Transient) TObjectPtr<USoundBase> InSound;
    UPROPERTY(Transient) TObjectPtr<USoundBase> ActionSound;
    FDelegateHandle ShotHandle;
    FDelegateHandle ProjectileHandle;
    FName WeaponId;
    float Clock = 0;
    float ReloadAge = 0;
    float ReloadDuration = 1;
    float ShotAge = 100;
    float EquipAge = 0;
    float Kick = 0;
    float SprintBlend = 0;
    FRotator PreviousView = FRotator::ZeroRotator;
    FVector2D Sway = FVector2D::ZeroVector;
    bool bMeleePose = false;
    bool bCompanionSource = false;
    bool bReady = false;
    bool bReloading = false;
    bool bEmptyReload = false;
    bool bThrowing = false;
    bool bThrowReleased = false;
    uint8 ReloadCues = 0;
    int32 ShotPresentationCount = 0;
    int32 NextCasing = 0;
    struct FCasingMotion { FVector Position; FVector Velocity; FRotator Rotation; FVector Spin; float Age = 10; };
    TArray<FCasingMotion> CasingMotion;
};
