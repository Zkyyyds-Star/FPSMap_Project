#pragma once
#include "CoreMinimal.h"
class UHeliosGuanXiaoYuAbilityComponent;
#include "Pets/HeliosPetDog.h"
#include "ShooterWeaponHolder.h"
#include "HeliosHumanoidPet.generated.h"
class AShooterWeapon;
class UHeliosWeaponDefinition;
class UHeliosCharacterPresentationComponent;
class UHeliosFirstPersonPresentationComponent;
class USceneComponent;
/** A character owns appearance and recovery; its separately spawned primary owns combat. */
UCLASS()
class HELIOSOUTPOST_API AHeliosHumanoidPet : public AHeliosPetCompanion, public IShooterWeaponHolder
{
    GENERATED_BODY()
public:
    UHeliosGuanXiaoYuAbilityComponent* GetGuanXiaoYuAbility()const{return GuanXiaoYuAbility;}
    UPROPERTY(VisibleAnywhere) TObjectPtr<UHeliosGuanXiaoYuAbilityComponent> GuanXiaoYuAbility;
    AHeliosHumanoidPet();
    virtual FString DisplayName() const override;
    virtual void Tick(float Dt) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual float TakeDamage(float Amount,const FDamageEvent& Event,AController* Controller,AActor* Causer) override;
    virtual void GetActorEyesViewPoint(FVector& Location,FRotator& Rotation) const override;
    virtual FVector GetPawnViewLocation() const override;
    UHeliosFirstPersonPresentationComponent* GetPresentation() const{return Presentation;}
    UHeliosCharacterPresentationComponent* GetCharacterPresentation() const{return CharacterPresentation;}
    AShooterWeapon* GetEquippedWeapon() const{return EquippedPrimary;}
    const FHeliosLoadout& GetLoadout() const{return Loadout;}
    bool ApplyLoadout(const FHeliosLoadout& InLoadout);
    int32 GetAmmo() const{return Ammo;}
    bool IsReloading() const{return ReloadRemaining>=0;}
    int32 Ammo=0,ShotsFired=0,Reloads=0;
    virtual void AttachWeaponMeshes(AShooterWeapon* Weapon) override;
    virtual void PlayFiringMontage(UAnimMontage* Montage) override {}
    virtual void AddWeaponRecoil(float Recoil) override {}
    virtual void UpdateWeaponHUD(int32 CurrentAmmo,int32 Magazine) override;
    virtual FVector GetWeaponTargetLocation() override;
    virtual void AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass) override {}
    virtual void OnWeaponActivated(AShooterWeapon* Weapon) override {}
    virtual void OnWeaponDeactivated(AShooterWeapon* Weapon) override {}
    virtual void OnSemiWeaponRefire() override {}
protected:
    virtual void LoadModel() override;
    virtual void SetPose(FName Name,bool bLoop=true) override;
    virtual void UpdateLocomotion(float Dt) override;
    virtual void TrackCombatTarget(float Dt) override;
    virtual void UpdateAttack(float Dt) override;
    virtual void CancelAttack() override;
    virtual float RecoveryDuration(FName Pose,float Fallback) const override;
private:
    void ShotPresented();
    void StartPrimaryReload();
    void HideSourceMeshes();
    void ConfigureCharacterSettings();
    void AlignBody();
    void ConfigurePrimary();
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> WeaponView;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UHeliosCharacterPresentationComponent> CharacterPresentation;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UHeliosFirstPersonPresentationComponent> Presentation;
    UPROPERTY(Transient) TObjectPtr<AShooterWeapon> EquippedPrimary;
    UPROPERTY(Transient) TObjectPtr<UHeliosWeaponDefinition> PrimaryDefinition;
    float PoseAge=0,MoveAge=0,ShotAge=100,ReloadRemaining=-1,ReloadDuration=0,PressWait=0,WeaponRange=1000;
    FName MovePose=TEXT("Run_Fwd");
    bool bMeleePrimary=false;
    FDelegateHandle ShotHandle;
};
