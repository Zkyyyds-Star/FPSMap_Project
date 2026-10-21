#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosBioRoleQA.generated.h"
class AHeliosBioHotelGameMode;
class UHeliosBioRoleDefinition;
class USkeletalMeshComponent;
class ACharacter;
class FJsonValue;
class AHeliosBioSkillProjectile;
class AHeliosBioTurret;
class AShooterWeapon;
struct FHeliosBioRolePoseProbe
{
    FString Clip;
    float Seconds=0;
    TArray<FTransform> Pose;
    int32 Samples=0,ClockAdvances=0,PoseAdvances=0;
};

/** Isolated source-role fixtures; never counted as a natural round or physical input test. */
UCLASS()
class HELIOSOUTPOST_API AHeliosBioRoleQA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosBioRoleQA();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY(Transient) TObjectPtr<AHeliosBioHotelGameMode> Mode;
    UPROPERTY(Transient) TObjectPtr<UHeliosBioRoleDefinition> Definition;
    FString Out,TestRole;
    TArray<TPair<FString,bool>> Results;
    TArray<TSharedPtr<FJsonValue>> Observations;
    TMap<FString,FHeliosBioRolePoseProbe> PoseProbes;
    int32 Stage=0,Failures=0,VictimId=INDEX_NONE,HunterId=INDEX_NONE;
    double Next=0,Started=0,LastSample=-1;
    float HealthBefore=0,CooldownBefore=0;
    float LowestVictimHealth=0,MaxFrozenSeconds=0;
    FVector CastPosition=FVector::ZeroVector,FixtureOrigin=FVector::ZeroVector,FixtureForward=FVector::ForwardVector;
    FVector PlayerMoveStart=FVector::ZeroVector,HunterMoveStart=FVector::ZeroVector;
    bool bAbyss=false;
    bool bManualSpectator=false,bManualSpectatorPrepared=false,bManualSpectatorFinished=false,bManualSpectatorRoundChanged=false;
    int32 ManualSpectatorRound=0;
    double ManualSpectatorStartedWall=0,ManualSpectatorRestartWall=0;
    FString ManualSpectatorStopReason;
    bool bHumanBaselineFrozen=false,bHadProjectile=false,bProjectileFX=false,bVictimAirborne=false;
    double BaselineAt=0,ProbeStarted=0;
    float SecondaryBefore=0,MaskBefore=0,SpeedBefore=0,EnergyBefore=0,ProjectileTravel=0;
    int32 KillsBefore=0,ShotsBefore=0,HistoricalInfections=0,AirMotionSamples=0;
    int32 AfterNPCValidation=12,NPCPreviousMelee=0,NPCLightHits=0,NPCHeavyHits=0;
    float NPCPreviousVictimHealth=0;
    float WukongBaseLightDamage=0,WukongPlayerBaseSpeed=0,WukongNPCBaseSpeed=0,WukongNPCBoostedReferenceDamage=0;
    int32 WukongPlayerAreaHitsBefore=0,WukongNPCAreaHitsBefore=0;
    int32 SerumShotsBefore=0,SerumEffectsBefore=0,SerumBlocksBefore=0,SerumVictimReserve=0,SerumCancelledBefore=0;
    FName SerumVictimWeaponId;
    TWeakObjectPtr<AShooterWeapon> SerumVictimWeapon;
    bool bWukongPlayerSpeedApplied=false,bWukongNPCSpeedApplied=false,bWukongNPCSpeedRestored=false;
    double NPCPrimaryRecoveryStarted=0;
    FVector VictimBefore=FVector::ZeroVector,LastVictimPosition=FVector::ZeroVector,ProjectileStart=FVector::ZeroVector,LastProjectilePosition=FVector::ZeroVector;
    TWeakObjectPtr<AHeliosBioSkillProjectile> Projectile;
    TWeakObjectPtr<AHeliosBioTurret> Turret;
    UPROPERTY(Transient) TObjectPtr<AActor> Obstacle;
    void Check(const TCHAR* Name,bool Passed);
    void Finish(const FString& Blocker=FString());
    void Capture(const TCHAR* Name);
    USkeletalMeshComponent* FirstPersonMesh() const;
    void SamplePose(const FString& Key,USkeletalMeshComponent* Mesh);
    void CheckPose(const FString& Key,bool RequirePose);
    bool CheckGround(const FString& Key,ACharacter* Character,USkeletalMeshComponent* Mesh);
    bool PlaceActor(ACharacter* Character,const FVector& Wanted) const;
    bool FindFixtureLane();
    void PlaceCombatTarget(float Distance=200.f);
    bool PickupFixtureSupply(int32 Id,int32 Kind);
    bool ValidateSourceBindings(FString& Blocker);
    void SampleHumanBaseline(const FString& Label);
    bool SelectLivingVictim();
    bool PrepareVictimHealth(float Desired);
    void WatchProjectile();
    void SampleProjectile();
    void MakeObstacle(float Distance);
    void RemoveObstacle();
    void FinishCombatFixture(double Now);
    void TickZhongXiaoKui(double Now);
    void TickMechanical(double Now);
    void SampleMechanicalNPCWindow(bool Requested=false,bool Accepted=false);
    void BeginNPCValidation(double Now,int32 ContinueStage);
    void TickNPCValidation(double Now);
    void TickWukongBuff(double Now);
    void TickSerum(double Now);
};
