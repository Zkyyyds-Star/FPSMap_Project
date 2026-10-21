#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Player/HeliosLoadout.h"
#include "HeliosBioSupplyWeaponQA.generated.h"
class AHeliosBioHotelGameMode;
class ACharacter;
class AShooterWeapon;
class ACameraActor;
class AHeliosBioNPC;
class AHeliosBioSupply;
class AHeliosBioSupplyWeapon;
class UHeliosBioSupplyWeaponDefinition;
class UHeliosBioRoundInventoryComponent;
class FJsonObject;
class FJsonValue;
class UAnimSequence;
class USkeletalMeshComponent;
class UHeliosCharacterPresentationComponent;
struct FHeliosCharacterWeaponPresentation;

struct FHeliosBioSupplyPoseProbe
{
    TWeakObjectPtr<UAnimSequence> Clip;
    float Seconds=0;
    TArray<FVector> Surface;
    int32 Samples=0,ClockAdvances=0,SurfaceAdvances=0;
};

/** Explicit candidate inventory fixture; saved definitions are always read only. */
UCLASS()
class HELIOSOUTPOST_API AHeliosBioSupplyWeaponQA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosBioSupplyWeaponQA();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY(Transient) TObjectPtr<AHeliosBioHotelGameMode> Mode;
    UPROPERTY(Transient) TArray<TObjectPtr<UHeliosBioSupplyWeaponDefinition>> Originals;
    UPROPERTY(Transient) TArray<TObjectPtr<UHeliosBioSupplyWeaponDefinition>> Candidates;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosBioSupply>> FixtureSupplies;
    TMap<FString,FString> OriginalPackageHashes;
    TArray<uint8> OriginalGateBits;
    TArray<TSharedPtr<FJsonValue>> Checks,Observations,SourceIssues;
    FString Out,ExpectedMap,Blocker;
    int32 Stage=0,Slot=1,Failures=0,CompletedNativeItems=0;
    bool bManual=false,bFinished=false,bFixturePrepared=false;
    double StartedWall=0,StageAt=0,NextObservation=0,MaximumWall=300;
    TWeakObjectPtr<AHeliosBioNPC> Victim;
    TWeakObjectPtr<AHeliosBioSupplyWeapon> TestWeapon;
    int32 AmmoBefore=0,ReserveBefore=0,ShotsBefore=0,HitsBefore=0;
    int32 ReloadAmmoBefore=0,ReloadReserveBefore=0,ReloadShotsBefore=0;
    float HealthBefore=0,DamageBefore=0,FrozenBefore=0;
    bool bFiring=false,bReloadStarted=false;
    int32 FormIndex=-1,CompletedNPCItems=0,NPCShotsBefore=0,NPCHitsBefore=0,NPCAmmoBefore=0,NPCReserveBefore=0,NPCEmptyShots=0,NPCEmptyReserve=0;
    int32 InterruptAmmo=0,InterruptReserve=0,InterruptedSlot=0;
    bool bFormCase=false,bInterruptTestDone=false,bPlayerMoveComplete=false,bNPCEmptySeen=false,bNPCReloadSeen=false,bNPCDamageSeen=false,bNPCSelectedNaturally=false;
    bool bNPCInfectionSeen=false,bNPCStoppedAfterInfection=true,bPlayerRestored=false,bNPCModelRestore=false,bInventoryCases=false;
    uint32 DownSequenceBefore=0,DownPoseCountBefore=0;
    uint64 DownFrameBefore=0;
    int32 DownShotsBefore=0,MotherMeleeBefore=0,MotherInfectionsBefore=0;
    float NPCHealthBefore=0,NPCDamageBefore=0,NPCFrozenBefore=0;
    double NextPoseSample=0,ReloadStartedAt=0;
    FVector MoveDirection=FVector::ZeroVector,MoveStart=FVector::ZeroVector;
    FHeliosLoadout FormBaseline;
    FString SelectedForm;
    TWeakObjectPtr<AHeliosBioNPC> FormBot,OtherMother,SerumCaster;
    TMap<FName,FHeliosBioSupplyPoseProbe> FPPose,TPPose;
    TArray<TSharedPtr<FJsonValue>> FormWindows;
    TArray<TWeakObjectPtr<AShooterWeapon>> PreservedWeapons;
    TArray<int32> PreservedAmmo,PreservedReserve;
    UPROPERTY(Transient) TObjectPtr<ACameraActor> VisualCamera;
    TMap<FString,int32> AVShots,AVHits;
    int64 AudioSerial=0;
    bool bAudioObservationFailed=false;
    TSet<FString> ActuallyRenderedFX;
    TArray<FString> RenderCaptures;
    TSet<FString> CapturedViewPhases;
    double AudioWindowStart=0;
    bool CheckNativeAudio(AHeliosBioSupplyWeapon* Gun,bool FirstPerson);
    TWeakObjectPtr<AHeliosBioSupply> TouchSupply;
    int32 TouchScoreBefore=0,TouchReserveBefore=0,AutomaticTouchItems=0;
    TWeakObjectPtr<AHeliosBioSupply> CompetitionSupply,RejectSupply;
    int32 CompetitionPlayerScore=0,CompetitionOtherScore=0,RepeatedReserve=0,RejectParticipant=INDEX_NONE,RejectScore=0,RejectReserve=0;
    int32 CompetitionActiveSlotBefore=0,CompetitionReserveBefore=0,CompetitionBaselineReserveBefore=0;
    bool bDuplicateTransaction=false,bCompetitionAuto=false,bRepeatAuto=false,bWallAuto=false,bFloorAuto=false;
    bool BeginAutomaticReject(bool DifferentFloor,double Now);
    void TickInventoryTransactions(double Now);
    bool BeginTouchPickup(double Now);
    void TickTouchPickup(double Now);
    void CompleteInitialAcquisition(double Now);
    void ObserveNativeAV(AHeliosBioSupplyWeapon* Gun,bool FirstPerson);
    void QueueAudioObservation(bool FirstPerson);
    bool SetNPCInspectionCamera();
    bool SetupFormFixture();
    const FHeliosCharacterWeaponPresentation* SourcePair() const;
    void SampleNativePose(AActor* ParticipantActor,AHeliosBioSupplyWeapon* Gun,bool FirstPerson,FName Semantic);
    bool PosePassed(bool FirstPerson,FName Semantic) const;
    void SampleCurrentActions();
    bool PrepareMove(ACharacter* Character);
    bool MeasureSole(AHeliosBioNPC* Bot,const FString& Phase);
    bool PlaceOpponent(ACharacter* Human,AHeliosBioNPC* Enemy,float Distance,bool NearMelee=false);
    bool BeginNPCItem(double Now);
    void TickNPCItem(double Now,float Dt);
    void CompleteItemWindow(double Now);
    void BeginPlayerRestore(double Now);
    void TickInventoryRestore(double Now);
    bool SamePreservedInventory(UHeliosBioRoundInventoryComponent* Inventory) const;
    void CapturePreservedInventory(UHeliosBioRoundInventoryComponent* Inventory);
    void CheckInventoryTransactions();
    void Check(const FString& Name,bool Passed);
    bool LoadCandidates();
    bool OriginalsUnchanged();
    void FreezeBots();
    void Observe(const FString& Kind);
    TSharedRef<FJsonObject> InventorySnapshot(UHeliosBioRoundInventoryComponent* Inventory) const;
    bool GrantManual();
    AHeliosBioSupply* MakeSupply(UHeliosBioSupplyWeaponDefinition* Definition);
    bool PrepareVictim();
    bool BeginNativeItem(double Now);
    void TickNativeItem(double Now);
    void Finish(const FString& Reason=FString());
};
