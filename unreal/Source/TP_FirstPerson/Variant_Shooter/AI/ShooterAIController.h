// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Engine/TimerHandle.h"
#include "ShooterAIController.generated.h"

class UStateTreeAIComponent;
class UAIPerceptionComponent;
struct FAIStimulus;

DECLARE_DELEGATE_TwoParams(FShooterPerceptionUpdatedDelegate, AActor*, const FAIStimulus&);
DECLARE_DELEGATE_OneParam(FShooterPerceptionForgottenDelegate, AActor*);
DECLARE_DELEGATE(FShooterAwarenessChangedDelegate);

/**
 *  Simple AI Controller for a first person shooter enemy
 */
UCLASS(abstract)
class TP_FIRSTPERSON_API AShooterAIController : public AAIController
{
	GENERATED_BODY()
	
	/** Runs the behavior StateTree for this NPC */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UStateTreeAIComponent* StateTreeAI;

	/** Detects other actors through sight, hearing and other senses */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UAIPerceptionComponent* AIPerception;

protected:

	/** Team tag for pawn friend or foe identification */
	UPROPERTY(EditAnywhere, Category="Shooter")
	FName TeamTag = FName("Enemy");

	/** Enemy currently being targeted */
	TObjectPtr<AActor> TargetEnemy;

	/** Periodic diagnostics are enabled only by the enemy runtime QA command line. */
	FTimerHandle QADiagnosticsTimer;
	bool bQADiagnosticsEnabled = false;
	FTimerHandle AwarenessTimer;
	TWeakObjectPtr<AActor> RememberedPlayer;
	TWeakObjectPtr<AActor> CombatTrackingTarget;
	FVector InvestigationLocation = FVector::ZeroVector;
	double AwarenessMemoryExpiresAt = 0.0;
	bool bEnhancedAwareness = false;
	bool bInvestigating = false;
	TMap<TWeakObjectPtr<AActor>, double> RecentCombatThreats;

public:

	/** Called when an AI perception has been updated. StateTree task delegate hook */
	FShooterPerceptionUpdatedDelegate OnShooterPerceptionUpdated;

	/** Called when an AI perception has been forgotten. StateTree task delegate hook */
	FShooterPerceptionForgottenDelegate OnShooterPerceptionForgotten;
	FShooterAwarenessChangedDelegate OnShooterAwarenessChanged;

public:

	/** Constructor */
	AShooterAIController();

protected:

	/** Pawn initialization */
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Emits StateTree, perception and target state without changing AI decisions. */
	void LogQADiagnostics(const TCHAR* Phase) const;
	void LogQAPeriodicDiagnostics();

protected:

	/** Called when the possessed pawn dies */
	UFUNCTION()
	void OnPawnDeath();

public:

	/** Sets the targeted enemy */
	void SetCurrentTarget(AActor* Target);

	/** Clears the targeted enemy */
	void ClearCurrentTarget();

	/** Returns the targeted enemy */
	AActor* GetCurrentTarget() const { return TargetEnemy; };

	bool UsesEnhancedAwareness() const { return bEnhancedAwareness; }
	bool IsInvestigating() const { return bInvestigating; }
	FVector GetInvestigationLocation() const { return InvestigationLocation; }
	void RefreshAwareness();
	void ReportAwarenessThreat(AActor* Source, const FVector& Location, bool bDamage = false);
	AActor* GetTrackingTarget() const { return CombatTrackingTarget.Get(); }
	bool IsCombatTracking() const { return CombatTrackingTarget.IsValid(); }
	bool CanSeeAwarenessTarget(const AActor* Candidate, bool bRetainTarget = false) const;
	void ResetAwareness();

private:
	bool IsLivingPlayer(const AActor* Candidate) const;
	void InvestigateAwarenessLocation(AActor* Source, const FVector& Location);
	void ClearAwarenessState();
	void RefreshTacticalAwareness();

protected:

	/** Called when the AI perception component updates a perception on a given actor */
	UFUNCTION()
	void OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	/** Called when the AI perception component forgets a given actor */
	UFUNCTION()
	void OnPerceptionForgotten(AActor* Actor);
};
