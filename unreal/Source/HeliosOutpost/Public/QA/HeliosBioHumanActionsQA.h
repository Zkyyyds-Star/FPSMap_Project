#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Player/HeliosLoadout.h"
#include "HeliosBioHumanActionsQA.generated.h"
class AHeliosBioHotelGameMode;
class AHeliosBioNPC;
class AShooterWeapon;
class UAnimSequence;
class USkeletalMesh;
class FJsonValue;
class FJsonObject;

struct FHeliosHumanActionPoseEvidence
{
    FString Clip;
    float Seconds=0;
    TArray<FTransform> Pose;
    int32 Samples=0,ClockAdvances=0,PoseAdvances=0;
};
struct FHeliosHumanActionCase
{
    TWeakObjectPtr<AHeliosBioNPC> Bot;
    FHeliosLoadout Loadout;
    FString Form,Blocker;
    TWeakObjectPtr<USkeletalMesh> Body;
    TMap<FName,TArray<TWeakObjectPtr<UAnimSequence>>> Clips;
    TMap<FName,FHeliosHumanActionPoseEvidence> Poses;
    FVector PreviousPosition=FVector::ZeroVector;
    float PreparationTravel=0;
    bool bInitialStanding=false,bMove=false,bFire=false,bEmpty=false,bReload=false;
    bool bDeath=false,bInfection=false,bStoppedHumanWeapon=false,bRestart=false;
};

/** Isolated production AI human-action coverage; no synthetic pose, ammo or damage success. */
UCLASS()
class HELIOSOUTPOST_API AHeliosBioHumanActionsQA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosBioHumanActionsQA();
    virtual void Tick(float DeltaSeconds) override;
private:
    UPROPERTY(Transient) TObjectPtr<AHeliosBioHotelGameMode> Mode;
    UPROPERTY(Transient) TArray<TObjectPtr<UObject>> SourceObjects;
    TArray<FHeliosHumanActionCase> Cases;
    TArray<int32> SelectedCases;
    TArray<TSharedPtr<FJsonValue>> Checks,Observations;
    TWeakObjectPtr<AHeliosBioNPC> Enemy;
    TWeakObjectPtr<AShooterWeapon> OriginalHumanWeapon;
    FString Out,ExpectedMap=TEXT("/Game/Helios/Maps/CFMBioPyramidFloating"),GlobalBlocker;
    int32 Stage=0,Failures=0,CurrentCase=0,Seed=0,RoundSerialBefore=0;
    int32 AmmoBefore=0,ShotsBefore=0,ReserveBefore=0,EmptyShots=0,ReloadReserveBefore=0;
    int32 EnemyMeleeBefore=0,EnemyInfectionsBefore=0,HumanShotsAtInfection=0,HumanAmmoAtInfection=0,ActualProjectileHits=0;
    int32 NativeWeaponShotsBefore=0,NativeWeaponHitsBefore=0,FireTargetRepositions=0;
    int32 SupplyCountBefore=0,LastObservedSupplyCount=0,ReloadSupplyCountBefore=0;
    int32 ActualSupplyAmmoAdded=0,ReloadSupplyAmmoAddedBefore=0;
    uint32 HumanDownSequenceBefore=0,HumanDownPoseCommitCountBefore=0;
    uint64 HumanDownPoseCommitFrameBefore=0;
    float EnemyHealthBefore=0,ShooterDamageBefore=0,ReloadCooldownBefore=0,DeathDuration=0;
    bool bReloadStarted=false,bTransitionSeen=false,bTransitionEnded=false;
    bool bReloadWorldClock=false,bInitialRosterReady=false;
    bool bDeathLastFrame=false,bNoHumanShotsAfter=true;
    bool bSupplyAmmoProvenance=true;
    double Started=0,Next=0,StageAt=0,LastSample=0,ReloadStartedAt=0,InfectionAt=0;
    double LostFireSightAt=-1;
    FVector LaneOrigin=FVector::ZeroVector,LaneForward=FVector::ForwardVector;
    void Check(const FString& Name,bool Passed);
    void AddBlocker(FHeliosHumanActionCase& Case,const FString& Reason);
    void Finish(const FString& Reason=FString());
    bool BuildRoster();
    bool ResolveBindings(FHeliosHumanActionCase& Case);
    void SamplePose(FHeliosHumanActionCase& Case,FName Action);
    bool PosePassed(const FHeliosHumanActionCase& Case,FName Action) const;
    bool MeasureStanding(FHeliosHumanActionCase& Case,const FString& Phase);
    void FreezeAll(bool HideOthers=false);
    bool SelectRepresentatives();
    bool FindCombatLane(AHeliosBioNPC* Human);
    bool MaintainFireTargetSight(AHeliosBioNPC* Human,double Now);
    bool HasNativeFireLane(AHeliosBioNPC* Human,const FVector& TargetPosition,bool LiveTarget,FJsonObject* Evidence=nullptr);
    bool PlaceMeleeOpponent(AHeliosBioNPC* Human);
    bool PlaceActor(AHeliosBioNPC* Bot,const FVector& Wanted);
    bool BeginCase(double Now);
    void EndCase(double Now);
    void SamplePreparation();
    void SampleCombat(double Now);
    void ObserveSupplyIncome(AHeliosBioNPC* Human,double Now);
    void BindProjectileEvidence(AShooterWeapon* Weapon,int32 CaseIndex);
};
