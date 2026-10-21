#pragma once
#include "CoreMinimal.h"
#include "Weapons/HeliosCombatWeapons.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Components/ActorComponent.h"
#include "HeliosMechWeapon.generated.h"

class UCanvas;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UAudioComponent;
class UPointLightComponent;
class AHeliosArenaCharacter;

UENUM()
enum class EHeliosMechState : uint8 { Idle, Sweep, Stomp, Charging, Firing, Cooling };

UCLASS(BlueprintType)
class HELIOSOUTPOST_API UHeliosMechDefinition : public UHeliosWeaponDefinition
{
    GENERATED_BODY()
public:
    UHeliosMechDefinition();
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float StompDamage=1000;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float BeamDamage=180;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float SweepSeconds=1.1f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float StompSeconds=1.4f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float ChargeSeconds=.9f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float MaximumBeamSeconds=5;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float CooldownSeconds=4;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float StompRadius=700;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float BeamRadius=100;
};

/** World-space articulated armor and bounded pools of beam/impact presentation. */
UCLASS()
class HELIOSOUTPOST_API UHeliosMechPresentationComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    void Initialize();
    void Update(float Dt);
    void Burst(const FVector& At,const FVector& Normal,float Radius,bool Heavy);
    void SetBeam(const FVector& Start,const FVector& End,bool Hit,float Charge,bool Firing);
    void Clear();
    int32 GetFootstepCount() const { return FootstepCount; }
    FVector Hands[2],Feet[2],Muzzle;
private:
    UStaticMeshComponent* Part(const TCHAR* Mesh,const TCHAR* Material=nullptr,bool Shadow=false);
    void Pose(UStaticMeshComponent* C,const FVector& P,const FRotator& R,const FVector& S=FVector::OneVector);
    void Limb(UStaticMeshComponent* C,const FVector& A,const FVector& B,float Length);
    void Mote(const FVector& P,const FVector& V,float Life,float Size,bool Dust);
    void Footfall(const FVector& Point,const FVector& Normal,bool Sprinting);
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> StepWaves;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> StepMaterials;
    UPROPERTY(Transient) TArray<TObjectPtr<UPointLightComponent>> StepLights;
    float StepLightAges[2]={1,1};
    TArray<float> StepAges,StepRadii;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Armor;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> ViewArmor;
    UPROPERTY(Transient) TArray<TObjectPtr<UPointLightComponent>> ViewLights;
    FVector PlantedFeet[2],SwingStarts[2],SwingTargets[2],FootNormals[2],AttackFeet[2];
    float SwingProgress[2]={1,1},ContactAge[2]={1,1};
    float TravelSinceStep=550,SmoothedSpeed=0,BodyYaw=0;
    int32 LeadFoot=0;
    bool bWasStomping=false;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Beams;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Rings;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Motes;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Impacts;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Plasma;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> PlasmaMaterials;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> BeamMaterials;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> MoteMaterials;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> ImpactMaterials;
    UPROPERTY(Transient) TObjectPtr<UPointLightComponent> ReactorLight;
    UPROPERTY(Transient) TObjectPtr<UPointLightComponent> HitLight;
    TArray<FVector> MotePositions,MoteVelocities;
    TArray<float> MoteAges,MoteLives,MoteSizes,ImpactAges,ImpactRadii;
    TArray<bool> DustFlags;
    int32 NextMote=0,NextImpact=0;
    float Clock=0,StepPhase=0,EmissionAge=0,BeamTail=0,StepShakeAge=1,StepShakeStrength=0;
    FVector PreviousGround=FVector::ZeroVector;
    bool bGroundTracked=false;
    int32 NextStep=0,FootstepCount=0;
    FVector BeamStart,BeamEnd,ImpactNormal=FVector::UpVector;
    bool bBeamHit=false,bBeamOn=false;
};

UCLASS()
class HELIOSOUTPOST_API AHeliosMechWeapon : public AHeliosCombatWeapon
{
    GENERATED_BODY()
public:
    AHeliosMechWeapon();
    virtual void ApplyDefinition(const UHeliosWeaponDefinition* Definition) override;
    virtual void ActivateWeapon(const FName& OwnerTag) override;
    virtual void DeactivateWeapon() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void StartFiring() override;
    virtual void StopFiring() override { bPrimaryHeld=false; }
    virtual void StartSecondaryFire() override;
    virtual void StopSecondaryFire() override;
    virtual bool UsesAmmunition() const override { return false; }
    virtual void CancelReload() override { CancelAttack(); }
    void SuspendForArrival();
    bool TryResumeAfterArrival();
    void CancelAttack();
    FString Status() const;
    void DrawMechHUD(UCanvas* Canvas);
    bool WantsStomp(FVector* Point=nullptr) const;
    EHeliosMechState GetState() const { return State; }
    float GetHeat() const { return Heat; }
    void PlaySound(const TCHAR* Name,float Volume=1) const;
    AHeliosArenaCharacter* Player() const;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UHeliosMechPresentationComponent> MechArt;
private:
    friend class UHeliosMechPresentationComponent;
    friend class AHeliosMechDemo;
    bool CanAttack() const;
    void BeginMelee();
    void BeginCharge();
    void FinishLaser(bool Overheated);
    void TraceBeam();
    void DamageBeam();
    void DamageStomp();
    bool Unobstructed(const FVector& A,const FVector& B,const AActor* Target) const;
    UPROPERTY(Transient) TObjectPtr<UHeliosMechDefinition> Settings;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> LaserAudio;
    EHeliosMechState State=EHeliosMechState::Idle;
    float Age=0,Heat=0,PulseAge=0,DeployAge=0,CoolRemaining=0;
    float InvalidStompUntil=0;
    int32 SwingSide=0;
    bool bActive=false,bPrimaryHeld=false,bSecondaryHeld=false,bStompCommitted=false,bHavePreviousHand=false,bBeamHasHit=false;
    FVector StompPoint,PreviousHand,BeamOrigin,BeamTarget,BeamNormal=FVector::UpVector;
    TSet<TWeakObjectPtr<AActor>> HitEnemies;
};
