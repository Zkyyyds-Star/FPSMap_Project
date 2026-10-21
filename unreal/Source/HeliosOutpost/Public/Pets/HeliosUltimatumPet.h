#pragma once
#include "CoreMinimal.h"
#include "Pets/HeliosPetDog.h"
#include "HeliosUltimatumPet.generated.h"
USTRUCT(BlueprintType)
struct FHeliosCompanionRifleSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float Damage=12;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float Range=3500;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float ShotInterval=.09f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float BurstPause=.55f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float ReloadSeconds=1.9f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float SpreadDegrees=.35f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 BurstSize=5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 MagazineSize=36;
};
UCLASS(BlueprintType)
class HELIOSOUTPOST_API AHeliosUltimatumPet : public AHeliosPetCompanion
{
    GENERATED_BODY()
    friend class FHeliosDamageActorsTest;
public:
    AHeliosUltimatumPet();
    virtual FString DisplayName() const override {return TEXT("蝴蝶·雷神助战");}
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FHeliosCompanionRifleSettings RifleSettings;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Locomotion") float WalkSpeed=160.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Locomotion") float RunSpeed=420.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Locomotion") float CatchUpSpeed=750.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Locomotion") float ShortStepSpeed=130.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Locomotion") float CombatStepSpeed=240.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Locomotion") float CombatPursuitSpeed=450.f;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UStaticMeshComponent> Rifle;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UStaticMeshComponent> Magazine;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UHeliosRifleVFXComponent> RifleFX;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 Ammo=36;
    int32 ShotsFired=0,ReloadsCompleted=0;
    bool IsReloading() const {return ReloadAge>=0;}
    bool IsGunDropped() const {return bGunDropped;}
    virtual bool HasClearLine(AActor* Actor) const override;
    virtual float TakeDamage(float Amount,const FDamageEvent& Event,AController* InstigatorController,AActor* Causer) override;
    void PostPose(float Dt);
protected:
    friend class AHeliosUltimatumDemo;
    virtual void LoadModel() override;
    virtual void SetPose(FName Name,bool bLoop=true) override;
    virtual void TrackCombatTarget(float Dt) override;
    virtual void UpdateLocomotion(float Dt) override;
    virtual void UpdateAttack(float Dt) override;
    virtual void CancelAttack() override;
    virtual float RecoveryDuration(FName Pose,float Fallback) const override;
    virtual void UpdateRecoveryGround(float Dt) override;
    virtual bool HoldMovementUntilStanding() const override {return true;}
    virtual void RecoveryFinished() override;
    bool ReadyToShoot() const;
    virtual bool CanEngageTarget(AActor* Actor) const override;
    virtual double CombatTargetScore(AHeliosEnemyActor* Enemy,bool Recent,bool Threat) const override;
    FVector AimPoint(AActor* Actor) const;
    float AimYaw() const;
    bool FindCombatStep(bool Retreat,FVector& Out) const;
    void FireRound();
    void PlaceDroppedGun();
    FTransform AnimatedGunTransform() const;
    float ReloadAge=-1,ShotWait=0,AimAge=0,Pitch=0,Recoil=0;
    int32 BurstRemaining=0,PendingShots=0;
    bool bGunDropped=false,bPickingUp=false;
    FTransform DropStart,DropEnd,PickupOffset;
    FVector DropOwnerLocation=FVector::ZeroVector;
    FRandomStream SpreadRandom{71921};
    TWeakObjectPtr<AHeliosEnemyActor> Attacker,FootworkTarget;
    double RetaliateUntil=-1;
    float PlantRemaining=1.25f,StepRemaining=0,RepathRemaining=0;
    FVector StepGoal=FVector::ZeroVector;
    float PreviousFacing=0,TurnRate=0;
    FName MovingPose=TEXT("Walk");
};
