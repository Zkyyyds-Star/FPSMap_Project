#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Subsystems/WorldSubsystem.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Player/HeliosLoadout.h"
#include "HeliosPetDog.generated.h"

class AHeliosArenaCharacter;
class AHeliosEnemyActor;
class UAnimSequence;

UENUM(BlueprintType)
enum class EHeliosPetKind : uint8 { SwordDog=0, UltimatumGR=1 UMETA(Hidden), SunShangXiang=2, ZhongXiaoKui=3, ChanZeus=4, BuLianShiP90=5, WangZheKuiThunder=6, YunSummerMegatron=7 };

namespace HeliosPetLoadouts
{
    HELIOSOUTPOST_API FHeliosLoadout FromLegacyKind(EHeliosPetKind Kind);
    HELIOSOUTPOST_API EHeliosPetKind KindForLoadout(const FHeliosLoadout& Loadout);
    HELIOSOUTPOST_API bool ValidatePresentation(const FHeliosLoadout& Loadout,FString& Error);
}

UENUM(BlueprintType)
enum class EHeliosPetState : uint8 { Following, CatchingUp, Fighting, Returning, Sleeping, Waking };

struct HELIOSOUTPOST_API FHeliosPetDefinition
{
    EHeliosPetKind Kind;
    const TCHAR* Name;
    TSubclassOf<class AHeliosPetCompanion> Class;
    static const TArray<FHeliosPetDefinition>& All();
    static const FHeliosPetDefinition* Find(EHeliosPetKind Kind);
};

struct FHeliosPetSnapshot
{
    bool bValid=false;
    EHeliosPetKind Kind=EHeliosPetKind::SwordDog;
    float Health=0, SleepRemaining=0;
};

/** Values are in UE centimetres and game-time seconds. */
USTRUCT(BlueprintType)
struct FHeliosPetSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float FollowDistance = 2500.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float CatchUpDistance = 5000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float CombatRadius = 5000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float FollowMinSpeed=520;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float FollowMaxSpeed=900;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float ChaseSpeed=650;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float CatchUpSpeed=1000;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MaxHealth = 150.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Damage = 25.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float AttackInterval = 1.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float SleepSeconds = 30.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float ShieldReduction = .6f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1")) float MovementMultiplier = 2.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="50")) float MeleeRange = 500.f;
};

/** In-memory continuity only; a fresh application starts with a healthy pet. */
UCLASS()
class HELIOSOUTPOST_API UHeliosPetSession : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    EHeliosPetKind SelectedKind = EHeliosPetKind::SwordDog;
    EHeliosPetKind SnapshotKind = EHeliosPetKind::SwordDog;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    void Select(EHeliosPetKind Kind);
    void SetCount(int32 Count);
    void SelectSlot(int32 Slot, EHeliosPetKind Kind);
    EHeliosPetKind GetKind(int32 Slot) const;
    FHeliosLoadout GetLoadout(int32 Slot) const;
    bool SelectLoadout(int32 Slot, const FHeliosLoadout& Loadout);
    bool SelectCharacterSlot(int32 Slot, FName CharacterId, FName VariantId=NAME_None);
    bool SelectWeaponSlot(int32 Slot, FName WeaponId);
    int32 GetCount() const { return SelectedCount; }
    void ResetSnapshots();
    void NormalizeLegacySelection();
    bool LoadSelection(const FString& Path);
    bool SaveSelection(const FString& Path) const;
    void SaveSelection() const;
    TArray<EHeliosPetKind> SelectedKinds={EHeliosPetKind::SwordDog,EHeliosPetKind::SwordDog,EHeliosPetKind::SwordDog,EHeliosPetKind::SwordDog};
    TArray<FHeliosLoadout> SelectedLoadouts;
    FString SelectionSavePathOverride;
    FString LastSelectionError;
    TArray<FHeliosPetSnapshot> Snapshots;
    bool bHasSnapshot = false;
    float Health = 150.f;
    float SleepRemaining = 0.f;
private:
    int32 SelectedCount=1;
};

UCLASS(BlueprintType)
class HELIOSOUTPOST_API AHeliosPetCompanion : public ACharacter
{
    GENERATED_BODY()
    friend class FHeliosDamageActorsTest;
    friend class AHeliosUrbanQA;
public:
    AHeliosPetCompanion();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Pet") EHeliosPetKind Kind=EHeliosPetKind::SwordDog;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Pet") FHeliosLoadout Loadout;
    virtual FString DisplayName() const { return TEXT("刀盾狗"); }
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual float TakeDamage(float Amount, const FDamageEvent& Event, AController* InstigatorController, AActor* Causer) override;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pet") FHeliosPetSettings Settings;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Pet") EHeliosPetState State = EHeliosPetState::Following;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Pet") float Health = 150.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Pet") float SleepRemaining = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Pet") int32 HitsDelivered = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Pet") int32 Blocks = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Pet") bool bModelReady = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Pet") TObjectPtr<class UHeliosPetVFXComponent> Effects;
    TWeakObjectPtr<AHeliosArenaCharacter> CompanionOwner;
    TWeakObjectPtr<AHeliosEnemyActor> Target;
    int32 RosterSlot=0;
    int32 RosterSize=1;
    float RosterAudioGain() const { return RosterSize>1 ? 1.f/FMath::Sqrt(float(RosterSize)) : 1.f; }
    void SuspendCombat();
    void Restore(float InHealth, float InSleepRemaining);
    bool FindFollowPosition(FVector& Out) const;
    bool RejoinOwner();
    void RefreshDecision();
    FText StatusText() const;
    bool IsSleeping() const { return State == EHeliosPetState::Sleeping; }
    bool IsGuarding() const { return State == EHeliosPetState::Fighting && AttackAge < 0.f; }
    virtual bool HasClearLine(AActor* Actor) const;
    UFUNCTION() void ObserveDamage(AActor* Damaged, float Damage, const UDamageType* Type, AController* InstigatorController, AActor* Causer);
protected:
    friend class AHeliosPetDemo;
    friend class AHeliosUltimatumDemo;
    virtual void LoadModel();
    virtual void UpdateAttack(float Dt);
    virtual void CancelAttack();
    virtual float RecoveryDuration(FName Pose, float Fallback) const { return Fallback; }
    virtual void UpdateRecoveryGround(float Dt) {}
    virtual bool HoldMovementUntilStanding() const { return false; }
    virtual void RecoveryFinished() {}
    virtual void SetPose(FName Name, bool bLoop = true);
    void MoveTowards(const FVector& Destination, float Acceptance = 100.f, float FailedMoveSeconds = .5f);
    virtual void TrackCombatTarget(float DeltaSeconds);
    virtual void UpdateLocomotion(float DeltaSeconds) {}
    bool CanReach(const FVector& Destination) const;
    virtual bool CanEngageTarget(AActor* Actor) const;
    virtual double CombatTargetScore(AHeliosEnemyActor* Enemy, bool Recent, bool Threat) const;
    void AttackHit();
    bool CanMeleeHit(AHeliosEnemyActor* Enemy) const;
    void Sleep();
    void ForgetPetThreats();
    bool OutsideView() const;
    float DecisionRemaining = 0.f, AttackAge = -1.f, Cooldown = 0.f, WakeRemaining = 0.f;
    float StuckSeconds = 0.f;
    float DownRemaining = 0.f, HitPoseRemaining = 0.f;
    int32 AttackSerial = 0;
    bool bHitCommitted = false, bMoveRequested = false;
    FVector PreviousProgressPosition = FVector::ZeroVector;
    FVector FollowHeading = FVector::ForwardVector;
    FVector LastGoal = FVector::ZeroVector;
    FName CurrentPose;
    TWeakObjectPtr<AHeliosEnemyActor> RecentPlayerTarget;
    double RecentPlayerHitTime = -100.;
    TMap<TWeakObjectPtr<AHeliosEnemyActor>, double> EngagedUntil;
    UPROPERTY(Transient) TMap<FName, TObjectPtr<UAnimSequence>> Poses;
};

UCLASS(BlueprintType)
class HELIOSOUTPOST_API AHeliosPetDog : public AHeliosPetCompanion
{
    GENERATED_BODY()
};

/** Auto-attaches to supported playable worlds, without changing maps. The adventure HUD owns presentation. */
UCLASS()
class HELIOSOUTPOST_API UHeliosPetWorld : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Tick(float DeltaSeconds) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UHeliosPetWorld, STATGROUP_Tickables); }
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
    virtual void Deinitialize() override;
    UPROPERTY(Transient) TObjectPtr<AHeliosPetCompanion> Pet;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosPetCompanion>> Pets;
    void Clear(bool bSaveSnapshot=true);
    void IgnoreAll(FCollisionQueryParams& Query) const;
    class USoundConcurrency* GetVoiceConcurrency();
    void ObserveDamage(AActor* Damaged, float Damage, const UDamageType* Type, AController* Controller, AActor* Causer);
private:
    UPROPERTY(Transient) TObjectPtr<class USoundConcurrency> VoiceConcurrency;
    void Snapshot();
    void UpdatePlayerAvoidance(AHeliosArenaCharacter* Player);
    void ResetPlayerAvoidance();
    TWeakObjectPtr<class UCharacterMovementComponent> PassivePlayerMovement;
    int32 PassivePlayerUID=INDEX_NONE, OriginalPlayerUID=0;
    float OriginalPlayerAvoidanceWeight=.5f;
    int32 LastRunSerial = -1;
    float CheckRemaining = 0.f;
};
