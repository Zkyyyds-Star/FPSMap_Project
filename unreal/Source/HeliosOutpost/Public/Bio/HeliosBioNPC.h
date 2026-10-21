#pragma once
#include "CoreMinimal.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Bio/HeliosBioRules.h"
#include "Bio/HeliosBioPlatformTraversal.h"
#include "Player/HeliosLoadout.h"
#include "HeliosBioNPC.generated.h"
class UHeliosCharacterPresentationComponent;
class UAnimSequence;
class UHeliosBioFourSlashComponent;
class UHeliosBioRoleDefinition;
class USkeletalMeshComponent;
class USkeletalMesh;
class UStaticMeshComponent;
class UHeliosBioRoundInventoryComponent;

/** Captured from the committed native pose immediately before the infection handoff. */
struct FHeliosBioHumanDownCommit
{
    uint32 Sequence=0;
    TWeakObjectPtr<UAnimSequence> Clip;
    TWeakObjectPtr<USkeletalMesh> Body;
    float SampleSeconds=0.f,Duration=0.f;
    uint64 PoseCommitFrame=0;
    uint32 PoseCommitCount=0;
};

/** Existing CF firearms can hit this enemy subtype; match rules decide its team. */
UCLASS()
class HELIOSOUTPOST_API AHeliosBioNPC : public AHeliosEnemyActor
{
    GENERATED_BODY()
public:
    AHeliosBioNPC();
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual float TakeDamage(float Damage,const FDamageEvent& Event,AController* Instigator,AActor* Causer) override;
    virtual void GetActorEyesViewPoint(FVector& Location,FRotator& Rotation) const override;
    virtual bool OwnsNativeCombatMovement() const override { return true; }
    virtual bool UsesTacticalCombat() const override { return false; }
    virtual const FShooterAwarenessSettings* GetAwarenessSettings() const override { return nullptr; }
    virtual void AttachWeaponMeshes(AShooterWeapon* InWeapon) override;
    virtual void OnWeaponActivated(AShooterWeapon* InWeapon) override;
    virtual void OnWeaponDeactivated(AShooterWeapon* InWeapon) override;
    virtual void UpdateWeaponHUD(int32 Ammo,int32 Magazine) override;
    virtual FVector GetWeaponTargetLocation() override;
    virtual void OnSemiWeaponRefire() override;
    int32 ParticipantId = INDEX_NONE;
    int32 ShotsFired = 0,MeleeAttacks = 0;
    bool bQAFreeze = false;
    // Test fixtures can hold decisions while production action clocks continue.
    bool bQAHoldDecisions=false;
    float GetBioAttackCooldown() const{return MeleeCooldown;}
    bool BeginBioFourSlash();
    UHeliosBioFourSlashComponent* GetFourSlash() const { return FourSlash; }
    void ResetForBioRound();
    void AddSupplyAmmo();
    void InitializeBioParticipant(int32 Id,int32 RoundSeed,const FHeliosLoadout& PreferredLoadout);
    const FHeliosLoadout& GetHumanLoadout() const { return HumanLoadout; }
    UHeliosBioRoundInventoryComponent* GetRoundInventory() const {return RoundInventory;}
    int32 GetReserveAmmo() const { return ReserveAmmo; }
    float GetReloadRemaining() const { return ReloadRemaining; }
    FName GetMovementIntent() const { return MovementIntent; }
    FVector GetMovementGoal() const { return MovementGoal; }
    int32 GetLastMoveResult() const { return LastMoveResult; }
    bool IsMoveIntentActive() const { return bMoveIntentActive; }
    float GetBlockedSeconds() const { return BlockedSeconds; }
    int32 GetMovementRecoveries() const { return MovementRecoveries; }
    AActor* GetDesiredCombatTarget() const { return Target.Get(); }
    FString GetMovementDiagnostic() const;
    FString GetFloatingActionDiagnostic() const;
    bool IsRoleRangedReady() const { return bRoleRangedReady; }
    float GetBioMeleeCooldown() const { return MeleeCooldown; }
    float GetBioActionAge() const { return AttackAge; }
    int32 GetBioSkillStage() const { return BioSkillStage; }
    UAnimSequence* GetCurrentBioAnimation() const { return CurrentBioClip; }
    USkeletalMeshComponent* GetRoleWeaponMesh() const { return RoleWeaponVisual; }
    bool UseBioSkill(bool Secondary=false);
    bool FireSerumAt(AActor* AimTarget);
    bool FireRoleWeaponAt(AActor* AimTarget);
    /** Called only after a real human infection; initial mother selection skips Down. */
    bool BeginHumanInfectionTransition();
    bool IsHumanInfectionTransition() const { return bHumanInfectionTransition; }
    bool IsHumanInfectionTransitionBlocked() const { return bHumanInfectionTransitionBlocked; }
    float GetHumanInfectionTransitionAge() const { return HumanInfectionAge; }
    float GetHumanInfectionTransitionDuration() const { return HumanInfectionDuration; }
    UAnimSequence* GetHumanInfectionTransitionClip() const { return HumanInfectionDown; }
    const FHeliosBioHumanDownCommit& GetHumanDownCommit() const { return HumanDownCommit; }
private:
    friend class AHeliosBioHotelQA;
    friend class UHeliosBioRoundInventoryComponent;
    bool bPreparingInventoryWeapon=false;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UHeliosBioRoundInventoryComponent> RoundInventory;
    UPROPERTY(Transient) FHeliosLoadout HumanLoadout;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UHeliosBioFourSlashComponent> FourSlash;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UHeliosCharacterPresentationComponent> HumanPresentation;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USkeletalMeshComponent> RoleWeaponVisual;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> SerumWeaponVisual;
    UPROPERTY(Transient) TObjectPtr<UHeliosBioRoleDefinition> ActiveRoleArt;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> RolePrimaryIdle;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> RolePrimaryFire;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> BioIdle;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> BioRun;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> BioAttack;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> BioDeath;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> BioHeavy;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> BioSkill;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> BioSkillLoop;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> BioSkillEnd;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> BioHit;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> BioRunBackward;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> BioRunLeft;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> BioRunRight;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> CurrentBioClip;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> HumanInfectionDown;
    TWeakObjectPtr<AActor> Target;
    TWeakObjectPtr<AActor> AvoidTarget;
    FVector MovementGoal=FVector::ZeroVector,DefenseGoal=FVector::ZeroVector,LastProgressPosition=FVector::ZeroVector;
    FName MovementIntent=TEXT("Spawn");
    FString LastMoveReason;
    int32 ParticipantRoundSeed=1337,LastMoveResult=-1,MovementRecoveries=0;
    float MoveRequestAge=100.f,BlockedSeconds=0.f,ProgressSampleAge=0.f,RecoveryRemaining=0.f,AvoidTargetRemaining=0.f,CombatRepositionRemaining=0.f;
    bool bLoadoutInitialized=false,bDefenseGoalValid=false,bMoveIntentActive=false,bNavigationReconnect=false;
    float DecisionRemaining = 0,MeleeCooldown = 0,ReloadRemaining = 0,AttackAge = 100,PoseAge = 0,DeathAge = 0;
    int32 LastAmmo = 0,ReserveAmmo = 600,MagazineSize = 30;
    float ReloadDuration = 2.f;
    int32 BioSkillStage = 0;
    FName LastRoleId=NAME_None;
    EHeliosBioTeam LastTeam = EHeliosBioTeam::Human;
    bool bPlatformTraversalActive=false;
    HeliosBioPlatformTraversal::FTraversalRequest FloatingAction;
    TWeakObjectPtr<AActor> FloatingActionActor;
    FVector FailedFloatingGoal=FVector::ZeroVector;
    float FloatingActionUntil=0.f,FloatingPlanAt=0.f,FailedFloatingUntil=0.f;
    FString FloatingActionReason;
    bool bRoleRangedReady=false,bRolePrimaryFiring=false,bRolePrimaryBodyPose=false,bRolePrimaryWeaponPose=false,bRolePrimaryBodyActive=false,bWasFrozen=false;
    float FrozenSavedAcceleration=0.f;
    bool bLastAlive = true,bReloadCommit = false,bPlayingTransientPose = false;
    bool bHumanInfectionTransition=false,bHumanInfectionTransitionBlocked=false,bHumanInfectionPosePending=false;
    float HumanInfectionAge=0.f,HumanInfectionDuration=0.f,HumanInfectionSavedAcceleration=0.f;
    uint32 HumanInfectionQueuedCommitCount=0;
    uint64 HumanInfectionQueuedFrame=0;
    FHeliosBioHumanDownCommit HumanDownCommit;
    void Think();
    void ApplyTeamPresentation(EHeliosBioTeam Team,bool Force=false);
    void MeleeAttack();
    void ChooseHumanLoadout(const FHeliosLoadout& Preferred);
    void RestoreHumanMeshTransform();
    FTransform RoleMeshTransformForCapsule(const FTransform& SourceTransform) const;
    bool FindReachableGoal(const FVector& Wanted,FVector& Goal) const;
    bool ChooseDefenseGoal();
    bool ChooseFloatingDefenseGoal();
    void ThinkFloatingMovement(AActor* CombatTarget,bool Clear);
    void ResetFloatingAction();
    bool RequestBioMove(const FVector& Goal,float Acceptance,FName Intent,bool AllowPartial=false);
    void HoldBioPosition(FName Intent);
    void TickMovementProgress(float Dt);
    void RecoverBlockedMovement();
    bool TryReconnectToNavigation();
    void PlayRoleSkillPresentation(bool Secondary);
    bool TickRoleWeapon();
    void SetRolePrimaryPresentation(bool Enabled);
    void TickHumanInfectionTransition(float Dt);
    void ClearHumanInfectionTransition();
    void BlockHumanInfectionTransition(const TCHAR* Reason);
};
