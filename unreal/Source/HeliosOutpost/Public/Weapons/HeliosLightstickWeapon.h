#pragma once
#include "CoreMinimal.h"
#include "Weapons/HeliosCombatWeapons.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "HeliosLightstickWeapon.generated.h"

class UAnimSequence;
class UMaterialInstanceDynamic;
class UAudioComponent;
class UPointLightComponent;
class AHeliosLightstickWave;

UCLASS(BlueprintType)
class HELIOSOUTPOST_API UHeliosLightstickDefinition : public UHeliosWeaponDefinition
{
    GENERATED_BODY()
public:
    UHeliosLightstickDefinition();
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<float> ComboDamage = {105,120,180};
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<float> ComboSeconds = {.21f,.23f,.325f};
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float ComboResetSeconds = .8f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float ChargeSeconds = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float HeavyMinimumDamage = 180.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float HeavyMaximumDamage = 360.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1")) float WaveSpeed = 10000.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1")) float WaveRange = 10000.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1",ClampMax="180")) float WaveAngleDegrees = 180.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1")) float WaveBladeThickness = 90.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="32",ClampMax="256")) int32 WaveSegments = 128;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UStaticMesh> StreakMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UMaterialInterface> WaveMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UMaterialInterface> WaveCoreMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UMaterialInterface> SlashMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UStaticMesh> SlashMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float WaveHeightOffset = -45.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float WaveBodyOpacity = .25f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float WaveEdgeOpacity = .12f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float WaveGhostOpacity = .06f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USoundBase> WaveImpactSound;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float HeavyRecoverySeconds = .8f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float SweepRadius = 26.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<TSoftObjectPtr<UAnimSequence>> SwingAnimations;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimSequence> ChargeAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimSequence> HeavyAnimation;
};

/** Infinite-use melee weapon. Every damage query belongs to an attack window. */
UCLASS()
class HELIOSOUTPOST_API AHeliosLightstickWeapon : public AHeliosCombatWeapon
{
    GENERATED_BODY()
public:
    AHeliosLightstickWeapon();
    virtual void ApplyDefinition(const UHeliosWeaponDefinition* Definition) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void StartFiring() override;
    virtual void StopFiring() override;
    virtual void StartSecondaryFire() override;
    virtual void StopSecondaryFire() override;
    virtual bool UsesAmmunition() const override { return false; }
    virtual void BeginReload(float) override {}
    virtual void CancelReload() override { CancelAttack(); }
    virtual void DeactivateWeapon() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    void CancelAttack();
    void SuspendInput();
    float GetCharge() const;
    int32 GetAttackSerial() const { return AttackSerial; }
    int32 GetWavesReleased() const { return WavesReleased; }
    int32 GetOwnedWaveCount() const;
    bool IsCharging() const { return State==1; }
    bool IsAttacking() const { return State==2 || State==3; }
    void PlaySlashImpact(const FHitResult& Hit,const FVector& Direction,bool Wall,float Charge=0);
protected:
    virtual void BeginPlay() override;
private:
    friend class AHeliosLightstickDemo;
    friend class FHeliosDamageActorsTest;
    bool CanAttack() const;
    void BeginSwing(bool Heavy);
    void ReleaseWave();
    void ClearWaves();
    void EvaluateAttack(float PreviousAge,float NewAge);
    void TraceBlade(const FVector& Start,const FVector& End,float Damage);
    void UpdateArt(float Dt);
    void EmitSegment(const FVector& A,const FVector& B,float Width,const FLinearColor& Color,float Life);
    void Impact(const FHitResult& Hit);
    void Sound(const TCHAR* Name,float Volume=0.7f);
    void EnsureSlashPool();
    void UpdateSlashEffects(float Dt);
    void ClearSlashEffects();
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> SlashMarks;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> SlashMaterials;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> SlashFragments;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> FragmentMaterials;
    TArray<float> SlashAges,SlashLives,FragmentAges;
    TArray<FVector> SlashNormals,FragmentPositions,FragmentVelocities;
    TArray<TWeakObjectPtr<AActor>> SlashSurfaces;
    int32 NextEnemySlash=0,NextWallSlash=0,NextFragment=0;
    double LastSlashAudio=-1;
    UPROPERTY(Transient) TObjectPtr<UHeliosLightstickDefinition> Settings;
    UPROPERTY(Transient) TArray<TObjectPtr<UAnimSequence>> Swings;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> ChargePose;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> HeavyPose;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> LampMaterials;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Streaks;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> StreakMaterials;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Pulse;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PulseMaterial;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Lamp;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> HitLight;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> ChargeAudio;
    TArray<float> StreakAges,StreakLives;
    TSet<TWeakObjectPtr<AActor>> HitActors;
    TArray<TWeakObjectPtr<AHeliosLightstickWave>> OwnedWaves;
    FRotator ReleaseAim=FRotator::ZeroRotator;
    FVector LastHead=FVector::ZeroVector;
    int32 State=0,Combo=0,AttackSerial=0,NextStreak=0;
    float Age=0,IdleAge=10,Clock=0,ReleasedCharge=0,AttackDuration=.42f,PulseAge=10,HitAge=10;
    bool bHeld=false,bHaveHead=false,bSuspended=false;
    bool bWaveReleased=false;
    int32 WavesReleased=0;
};
