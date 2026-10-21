// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/AI/ShooterAIController.h"
#include "ShooterNPC.h"
#include "Components/StateTreeAIComponent.h"
#include "Perception/AISenseConfig_Damage.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AIPerceptionComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "AI/Navigation/PathFollowingAgentInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "StateTreeReference.h"
#include "StateTree.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerController.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Damage.h"
#include "Perception/AISense_Sight.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "EngineUtils.h"

namespace
{
bool IsHeliosEnemyQADiagnosticsEnabled()
{
	FString Mode;
	return FParse::Value(FCommandLine::Get(), TEXT("HeliosEnemyQA="), Mode)
		|| FParse::Param(FCommandLine::Get(), TEXT("HeliosEnemyQA"));
}

const FStateTreeReference* GetConfiguredStateTreeReference(const UStateTreeAIComponent* Component)
{
	if (!Component)
	{
		return nullptr;
	}

	const FStructProperty* Property = FindFProperty<FStructProperty>(Component->GetClass(), TEXT("StateTreeRef"));
	return Property ? Property->ContainerPtrToValuePtr<FStateTreeReference>(Component) : nullptr;
}

FString DescribeHeliosActor(const AActor* Actor)
{
	return Actor ? FString::Printf(TEXT("%s class=%s tags=%s"), *Actor->GetPathName(), *Actor->GetClass()->GetPathName(), *FString::JoinBy(Actor->Tags, TEXT(","), [](FName Tag) { return Tag.ToString(); })) : TEXT("None");
}

#if WITH_EDITOR
void PreflightEditorGameStateTree(UStateTree* StateTree, const AShooterAIController* Controller, const TCHAR* Role, const bool bLog)
{
	if (!StateTree)
	{
		if (bLog)
		{
			UE_LOG(LogTemp, Error, TEXT("HELIOS_AI_QA phase=editor_game_preflight role=%s controller=%s tree=None"),
				Role, *GetPathNameSafe(Controller));
		}
		return;
	}

	const bool bReadyBefore = StateTree->IsReadyToRun();
	if (!bReadyBefore)
	{
		StateTree->CompileIfChanged();
	}
	if (bLog)
	{
		UE_LOG(LogTemp, Display,
			TEXT("HELIOS_AI_QA phase=editor_game_preflight role=%s controller=%s tree=%s ready_before=%s ready_after=%s"),
			Role, *GetPathNameSafe(Controller), *StateTree->GetPathName(),
			bReadyBefore ? TEXT("true") : TEXT("false"),
			StateTree->IsReadyToRun() ? TEXT("true") : TEXT("false"));
	}
}
#endif
}

AShooterAIController::AShooterAIController()
{
	bQADiagnosticsEnabled = IsHeliosEnemyQADiagnosticsEnabled();
	// create the StateTree component
	StateTreeAI = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeAI"));
	StateTreeAI->SetStartLogicAutomatically(false);

	// create the AI perception component. It will be configured in BP
	AIPerception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerception"));

	// subscribe to the AI perception delegates
	AIPerception->OnTargetPerceptionUpdated.AddDynamic(this, &AShooterAIController::OnPerceptionUpdated);
	AIPerception->OnTargetPerceptionForgotten.AddDynamic(this, &AShooterAIController::OnPerceptionForgotten);
}

void AShooterAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	const AShooterNPC* AwarenessNPC = Cast<AShooterNPC>(InPawn);
	bEnhancedAwareness = AwarenessNPC && AwarenessNPC->GetAwarenessSettings();

#if WITH_EDITOR
	// UStateTree::PostLoad deliberately clears compiled data whenever editor data is
	// present. In an Editor executable launched with -game, the asset can load before
	// StateTreeEditorModule installs its asset-loaded compilation delegate. Compile at
	// the first actual consumer after all Default-phase editor modules have started.
	// The official shooter graph has one linked asset. Preflight it dependency-first;
	// otherwise the root can start successfully and fail only when it selects combat.
	UStateTree* ShootAtTargetTree = LoadObject<UStateTree>(nullptr,
		TEXT("/Game/Variant_Shooter/Blueprints/AI/ST_Shooter_ShootAtTarget.ST_Shooter_ShootAtTarget"));
	PreflightEditorGameStateTree(ShootAtTargetTree, this, TEXT("linked_shoot_at_target"), bQADiagnosticsEnabled);

	if (const FStateTreeReference* Reference = GetConfiguredStateTreeReference(StateTreeAI))
	{
		if (UStateTree* ConfiguredTree = const_cast<UStateTree*>(Reference->GetStateTree()))
		{
			PreflightEditorGameStateTree(ConfiguredTree, this, TEXT("root"), bQADiagnosticsEnabled);
		}
	}
#endif

	LogQADiagnostics(TEXT("possess_before_start"));

	// ensure we're possessing an NPC
	if (AShooterNPC* NPC = Cast<AShooterNPC>(InPawn))
	{
		// add the team tag to the pawn
		NPC->Tags.Add(TeamTag);

		// subscribe to the pawn's OnDeath delegate
		NPC->OnPawnDeath.AddDynamic(this, &AShooterAIController::OnPawnDeath);

		// Native combat pawns own movement and attacks; perception remains active below.
		if (!NPC->OwnsNativeCombatMovement()) StateTreeAI->StartLogic();
		LogQADiagnostics(TEXT("possess_after_start"));
		if (bEnhancedAwareness)
		{
			GetWorldTimerManager().SetTimer(AwarenessTimer, this,
				&AShooterAIController::RefreshAwareness,
				FMath::Max(0.02f, NPC->GetAwarenessSettings()->CheckInterval), true);
			RefreshAwareness();
		}

		if (bQADiagnosticsEnabled)
		{
			GetWorldTimerManager().SetTimer(QADiagnosticsTimer, this,
				&AShooterAIController::LogQAPeriodicDiagnostics, 1.0f, true, 0.2f);
		}
	}
	else if (bQADiagnosticsEnabled)
	{
		UE_LOG(LogTemp, Error, TEXT("HELIOS_AI_QA phase=possess_rejected controller=%s pawn=%s reason=not_shooter_npc"),
			*GetPathName(), *DescribeHeliosActor(InPawn));
	}
}

void AShooterAIController::OnUnPossess()
{
	GetWorldTimerManager().ClearTimer(AwarenessTimer);
	GetWorldTimerManager().ClearTimer(QADiagnosticsTimer);
	if (bEnhancedAwareness) ResetAwareness();
	OnShooterAwarenessChanged.Unbind();
	bEnhancedAwareness = false;
	Super::OnUnPossess();
}

void AShooterAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(AwarenessTimer);
	GetWorldTimerManager().ClearTimer(QADiagnosticsTimer);
	OnShooterAwarenessChanged.Unbind();
	Super::EndPlay(EndPlayReason);
}

bool AShooterAIController::IsLivingPlayer(const AActor* Candidate) const
{
	const APawn* PlayerPawn = Cast<APawn>(Candidate);
	return IsValid(PlayerPawn) && !PlayerPawn->IsActorBeingDestroyed() && !PlayerPawn->ActorHasTag(TEXT("Dead"))
		&& ((PlayerPawn->IsPlayerControlled() && PlayerPawn->ActorHasTag(TEXT("Player")))
			|| PlayerPawn->ActorHasTag(TEXT("HeliosPet")) || PlayerPawn->ActorHasTag(TEXT("HeliosSummon")));
}

bool AShooterAIController::CanSeeAwarenessTarget(const AActor* Candidate, bool bRetainTarget) const
{
	const AShooterNPC* NPC = Cast<AShooterNPC>(GetPawn());
	const FShooterAwarenessSettings* Settings = NPC ? NPC->GetAwarenessSettings() : nullptr;
	if (!Settings || NPC->IsDead() || !IsLivingPlayer(Candidate)) return false;
	const FVector Offset = Candidate->GetActorLocation() - NPC->GetActorLocation();
	const float Distance = bRetainTarget ? FMath::Max(Settings->SightDistance, Settings->RetainDistance) : Settings->SightDistance;
	if (Offset.SizeSquared() > FMath::Square(Distance)) return false;
	if (Offset.SizeSquared() > FMath::Square(Settings->CloseAwarenessDistance)
		&& FVector::DotProduct(Offset.GetSafeNormal2D(), NPC->GetActorForwardVector().GetSafeNormal2D())
			< FMath::Cos(FMath::DegreesToRadians(Settings->SightHalfAngle))) return false;

	const FVector Eye = NPC->GetFirstPersonCameraComponent()->GetComponentLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShooterAwareness), false, NPC);
	if (const AActor* Weapon = Cast<AActor>(NPC->GetEquippedWeapon())) Params.AddIgnoredActor(Weapon);
	const ACharacter* TargetCharacter = Cast<ACharacter>(Candidate);
	const float UpperOffset = TargetCharacter ? TargetCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 0.65f : 40.0f;
	for (const FVector Point : {Candidate->GetActorLocation(), Candidate->GetActorLocation() + FVector(0, 0, UpperOffset)})
	{
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Eye, Point, ECC_Visibility, Params)
			|| Hit.GetActor() == Candidate) return true;
	}
	return false;
}

void AShooterAIController::ClearAwarenessState()
{
	const bool bHadState = TargetEnemy != nullptr || bInvestigating;
	TargetEnemy = nullptr;
	RememberedPlayer.Reset();
	CombatTrackingTarget.Reset();
	bInvestigating = false;
	AwarenessMemoryExpiresAt = 0.0;
	InvestigationLocation = FVector::ZeroVector;
	if (bHadState)
	{
		if (AShooterNPC* NPC = Cast<AShooterNPC>(GetPawn())) NPC->StopShooting();
		ClearFocus(EAIFocusPriority::Gameplay);
		StopMovement();
		OnShooterAwarenessChanged.ExecuteIfBound();
	}
}

void AShooterAIController::ResetAwareness()
{
	RecentCombatThreats.Reset();
	ClearAwarenessState();
}

void AShooterAIController::InvestigateAwarenessLocation(AActor* Source, const FVector& Location)
{
	const AShooterNPC* NPC = Cast<AShooterNPC>(GetPawn());
	if (!NPC || !NPC->GetAwarenessSettings() || !IsLivingPlayer(Source)) return;
	TargetEnemy = nullptr;
	RememberedPlayer = Source;
	InvestigationLocation = Location;
	bInvestigating = true;
	AwarenessMemoryExpiresAt = GetWorld()->GetTimeSeconds() + NPC->GetAwarenessSettings()->MemoryDuration;
	if (AShooterNPC* MutableNPC = Cast<AShooterNPC>(GetPawn())) MutableNPC->StopShooting();
	ClearFocus(EAIFocusPriority::Gameplay);
	SetFocalPoint(Location);
	OnShooterAwarenessChanged.ExecuteIfBound();
}

void AShooterAIController::RefreshAwareness()
{
	AShooterNPC* NPC = Cast<AShooterNPC>(GetPawn());
	if (!bEnhancedAwareness || !NPC) return;
	if (NPC->IsDead()) { ClearAwarenessState(); return; }
	if (NPC->UsesTacticalCombat()) { RefreshTacticalAwareness(); return; }
	if (NPC->GetAwarenessSettings()->bPersistentCombatTracking && CombatTrackingTarget.IsValid())
	{
		if (IsLivingPlayer(CombatTrackingTarget.Get()))
		{
			TargetEnemy = CombatTrackingTarget.Get();
			InvestigationLocation = TargetEnemy->GetActorLocation();
			bInvestigating = false;
			return;
		}
		ClearAwarenessState();
	}
	if ((TargetEnemy && !IsLivingPlayer(TargetEnemy))
		|| (bInvestigating && (!IsLivingPlayer(RememberedPlayer.Get())
			|| GetWorld()->GetTimeSeconds() >= AwarenessMemoryExpiresAt))) ClearAwarenessState();

	AActor* VisiblePlayer = CanSeeAwarenessTarget(TargetEnemy, true) ? TargetEnemy.Get() : nullptr;
	if (!VisiblePlayer)
	{
		float BestDistance = TNumericLimits<float>::Max();
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			AActor* Candidate = It->IsValid() ? It->Get()->GetPawn() : nullptr;
			if (!IsLivingPlayer(Candidate)) continue;
			const float Distance = FVector::DistSquared(Candidate->GetActorLocation(), NPC->GetActorLocation());
			if (Distance < BestDistance && CanSeeAwarenessTarget(Candidate, Candidate == TargetEnemy))
			{
				VisiblePlayer = Candidate;
				BestDistance = Distance;
			}
		}
	}
	if (VisiblePlayer)
	{
		const bool bChanged = TargetEnemy != VisiblePlayer || bInvestigating;
		TargetEnemy = VisiblePlayer;
		if (NPC->GetAwarenessSettings()->bPersistentCombatTracking) CombatTrackingTarget = VisiblePlayer;
		RememberedPlayer = VisiblePlayer;
		InvestigationLocation = VisiblePlayer->GetActorLocation();
		bInvestigating = false;
		if (bChanged) OnShooterAwarenessChanged.ExecuteIfBound();
	}
	else if (IsLivingPlayer(TargetEnemy))
	{
		// Only this last visible position may drive investigation after LOS is lost.
		InvestigateAwarenessLocation(TargetEnemy, InvestigationLocation);
	}
}

void AShooterAIController::RefreshTacticalAwareness()
{
	const AShooterNPC* NPC = CastChecked<AShooterNPC>(GetPawn());
	const FShooterAwarenessSettings& Settings = *NPC->GetAwarenessSettings();
	const double Now = GetWorld()->GetTimeSeconds();
	for (auto It = RecentCombatThreats.CreateIterator(); It; ++It)
		if (!IsLivingPlayer(It.Key().Get()) || It.Value() <= Now) It.RemoveCurrent();
	if ((TargetEnemy && !IsLivingPlayer(TargetEnemy)) ||
		(bInvestigating && (!IsLivingPlayer(RememberedPlayer.Get()) || Now >= AwarenessMemoryExpiresAt)))
		ClearAwarenessState();

	auto Priority = [&](AActor* Candidate)
	{
		if (!RecentCombatThreats.Contains(Candidate)) return 2;
		return FVector::DistSquared2D(NPC->GetActorLocation(), Candidate->GetActorLocation())
			<= FMath::Square(Settings.CloseAwarenessDistance) ? 0 : 1;
	};
	AActor* Best = CanSeeAwarenessTarget(TargetEnemy, true) ? TargetEnemy.Get() : nullptr;
	int32 BestPriority = Best ? Priority(Best) : MAX_int32;
	float BestDistance = Best ? FVector::DistSquared(NPC->GetActorLocation(), Best->GetActorLocation()) : MAX_flt;
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		AActor* Candidate = *It;
		if (Candidate == Best || !IsLivingPlayer(Candidate)) continue;
		const int32 CandidatePriority = Priority(Candidate);
		const float Distance = FVector::DistSquared(NPC->GetActorLocation(), Candidate->GetActorLocation());
		// Keep a visible current target within its tier; distance breaks ties only on acquisition.
		if (CandidatePriority > BestPriority || (CandidatePriority == BestPriority &&
			(Best == TargetEnemy || Distance >= BestDistance))) continue;
		if (!CanSeeAwarenessTarget(Candidate, Candidate == TargetEnemy)) continue;
		Best = Candidate; BestPriority = CandidatePriority; BestDistance = Distance;
	}
	if (Best)
	{
		const bool bChanged = TargetEnemy != Best || bInvestigating;
		TargetEnemy = Best; RememberedPlayer = Best;
		InvestigationLocation = Best->GetActorLocation(); bInvestigating = false;
		if (bChanged) OnShooterAwarenessChanged.ExecuteIfBound();
	}
	else if (IsLivingPlayer(TargetEnemy)) InvestigateAwarenessLocation(TargetEnemy, InvestigationLocation);
}

void AShooterAIController::ReportAwarenessThreat(AActor* Source, const FVector& Location, bool bDamage)
{
	if (!bEnhancedAwareness || !IsLivingPlayer(Source)) return;
	const AShooterNPC* ThreatNPC = Cast<AShooterNPC>(GetPawn());
	if (ThreatNPC && ThreatNPC->UsesTacticalCombat())
	{
		if (ThreatNPC->IsDead()) return;
		if (bDamage) RecentCombatThreats.Add(Source, GetWorld()->GetTimeSeconds() + ThreatNPC->GetAwarenessSettings()->MemoryDuration);
		RefreshAwareness();
		// Damage/hearing behind cover supplies a remembered position, never wall vision.
		if (!TargetEnemy && (!bInvestigating || RememberedPlayer.Get() != Source))
			InvestigateAwarenessLocation(Source, Location);
		return;
	}
	RefreshAwareness();
	const AShooterNPC* NPC = Cast<AShooterNPC>(GetPawn());
	if (!NPC || NPC->IsDead()) return;
	// A friendly companion that actually lands a hit can draw retaliation.
	if (bDamage && (Source->ActorHasTag(TEXT("HeliosPet")) || Source->ActorHasTag(TEXT("HeliosSummon"))))
	{
		TargetEnemy = Source;
		RememberedPlayer = Source;
		if (NPC->GetAwarenessSettings()->bPersistentCombatTracking) CombatTrackingTarget = Source;
		InvestigationLocation = Location;
		bInvestigating = false;
		OnShooterAwarenessChanged.ExecuteIfBound();
		return;
	}
	if (bDamage && NPC->GetAwarenessSettings()->bPersistentCombatTracking && !CombatTrackingTarget.IsValid())
	{
		CombatTrackingTarget = Source;
		TargetEnemy = Source;
		RememberedPlayer = Source;
		InvestigationLocation = Source->GetActorLocation();
		bInvestigating = false;
		OnShooterAwarenessChanged.ExecuteIfBound();
		return;
	}
	if (!TargetEnemy) InvestigateAwarenessLocation(Source, Location);
}

void AShooterAIController::OnPawnDeath()
{
	LogQADiagnostics(TEXT("pawn_death"));
	GetWorldTimerManager().ClearTimer(QADiagnosticsTimer);
	GetWorldTimerManager().ClearTimer(AwarenessTimer);
	// stop movement
	GetPathFollowingComponent()->AbortMove(*this, FPathFollowingResultFlags::UserAbort);

	// stop StateTree logic
	StateTreeAI->StopLogic(FString(""));

	// unpossess the pawn
	UnPossess();

	// destroy this controller
	Destroy();
}

void AShooterAIController::SetCurrentTarget(AActor* Target)
{
	TargetEnemy = Target;
	if (bQADiagnosticsEnabled)
	{
		UE_LOG(LogTemp, Display, TEXT("HELIOS_AI_QA phase=set_target controller=%s target=%s"),
			*GetPathName(), *DescribeHeliosActor(Target));
	}
}

void AShooterAIController::ClearCurrentTarget()
{
	if (bQADiagnosticsEnabled)
	{
		UE_LOG(LogTemp, Display, TEXT("HELIOS_AI_QA phase=clear_target controller=%s previous=%s"),
			*GetPathName(), *DescribeHeliosActor(TargetEnemy));
	}
	TargetEnemy = nullptr;
}

void AShooterAIController::OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (bEnhancedAwareness)
	{
		// A failed sight stimulus means loss, never a fresh target acquisition.
		RefreshAwareness();
		if (!IsValid(Actor) || !Stimulus.WasSuccessfullySensed()) return;
		if (Stimulus.Type == UAISense::GetSenseID<UAISense_Hearing>()
			|| Stimulus.Type == UAISense::GetSenseID<UAISense_Damage>())
		{
			AActor* Source = Actor;
			for (int32 Depth = 0; IsValid(Source) && !IsLivingPlayer(Source) && Depth < 4; ++Depth)
			{
				Source = Source->GetInstigator() ? static_cast<AActor*>(Source->GetInstigator()) : Source->GetOwner();
			}
			ReportAwarenessThreat(Source, Stimulus.StimulusLocation,
				Stimulus.Type == UAISense::GetSenseID<UAISense_Damage>());
		}
		return;
	}
	// FAIStimulus debug descriptions are unavailable in Shipping and Test builds.
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (bQADiagnosticsEnabled)
	{
		UE_LOG(LogTemp, Display,
			TEXT("HELIOS_AI_QA phase=perception_updated controller=%s actor=%s sensed=%s stimulus=%s sense_delegate_bound=%s"),
			*GetPathName(), *DescribeHeliosActor(Actor),
			Stimulus.WasSuccessfullySensed() ? TEXT("true") : TEXT("false"),
			*Stimulus.GetDebugDescription(),
			OnShooterPerceptionUpdated.IsBound() ? TEXT("true") : TEXT("false"));
	}
#endif
	// pass the data to the StateTree delegate hook
	OnShooterPerceptionUpdated.ExecuteIfBound(Actor, Stimulus);
}

void AShooterAIController::OnPerceptionForgotten(AActor* Actor)
{
	if (bEnhancedAwareness)
	{
		RefreshAwareness();
		return;
	}
	if (bQADiagnosticsEnabled)
	{
		UE_LOG(LogTemp, Display,
			TEXT("HELIOS_AI_QA phase=perception_forgotten controller=%s actor=%s forget_delegate_bound=%s"),
			*GetPathName(), *DescribeHeliosActor(Actor),
			OnShooterPerceptionForgotten.IsBound() ? TEXT("true") : TEXT("false"));
	}
	// pass the data to the StateTree delegate hook
	OnShooterPerceptionForgotten.ExecuteIfBound(Actor);
}

void AShooterAIController::LogQAPeriodicDiagnostics()
{
	LogQADiagnostics(TEXT("periodic"));
}

void AShooterAIController::LogQADiagnostics(const TCHAR* Phase) const
{
	if (!bQADiagnosticsEnabled)
	{
		return;
	}

	const FStateTreeReference* Reference = GetConfiguredStateTreeReference(StateTreeAI);
	const UStateTree* StateTree = Reference ? Reference->GetStateTree() : nullptr;
	TArray<AActor*> PerceivedActors;
	if (AIPerception)
	{
		AIPerception->GetCurrentlyPerceivedActors(nullptr, PerceivedActors);
	}

	FString PerceivedSummary = FString::JoinBy(PerceivedActors, TEXT(";"),
		[](const AActor* Actor) { return DescribeHeliosActor(Actor); });
	const UAISenseConfig_Sight* Sight = AIPerception ? AIPerception->GetSenseConfig<UAISenseConfig_Sight>() : nullptr;
	const UAISenseConfig_Hearing* Hearing = AIPerception ? AIPerception->GetSenseConfig<UAISenseConfig_Hearing>() : nullptr;
	const UAISenseConfig_Damage* Damage = AIPerception ? AIPerception->GetSenseConfig<UAISenseConfig_Damage>() : nullptr;

	UE_LOG(LogTemp, Display,
		TEXT("HELIOS_AI_QA phase=%s controller=%s controller_class=%s pawn=%s begun_play=%s tree_component=%s tree=%s ref_valid=%s brain_running=%s paused=%s run_status=%d target=%s sense_delegate_bound=%s forget_delegate_bound=%s perceived_count=%d perceived=[%s] sight=%s sight_radius=%.1f lose_radius=%.1f sight_affiliation=E%dN%dF%d hearing=%s hearing_range=%.1f damage=%s path_status=%d"),
		Phase, *GetPathName(), *GetClass()->GetPathName(), *DescribeHeliosActor(GetPawn()),
		HasActorBegunPlay() ? TEXT("true") : TEXT("false"),
		StateTreeAI ? *StateTreeAI->GetClass()->GetPathName() : TEXT("None"),
		StateTree ? *StateTree->GetPathName() : TEXT("None"),
		Reference && Reference->IsValid() ? TEXT("true") : TEXT("false"),
		StateTreeAI && StateTreeAI->IsRunning() ? TEXT("true") : TEXT("false"),
		StateTreeAI && StateTreeAI->IsPaused() ? TEXT("true") : TEXT("false"),
		StateTreeAI ? static_cast<int32>(StateTreeAI->GetStateTreeRunStatus()) : -1,
		*DescribeHeliosActor(TargetEnemy),
		OnShooterPerceptionUpdated.IsBound() ? TEXT("true") : TEXT("false"),
		OnShooterPerceptionForgotten.IsBound() ? TEXT("true") : TEXT("false"),
		PerceivedActors.Num(), *PerceivedSummary,
		Sight ? TEXT("true") : TEXT("false"), Sight ? Sight->SightRadius : -1.0f,
		Sight ? Sight->LoseSightRadius : -1.0f,
		Sight ? static_cast<int32>(Sight->DetectionByAffiliation.bDetectEnemies) : -1,
		Sight ? static_cast<int32>(Sight->DetectionByAffiliation.bDetectNeutrals) : -1,
		Sight ? static_cast<int32>(Sight->DetectionByAffiliation.bDetectFriendlies) : -1,
		Hearing ? TEXT("true") : TEXT("false"), Hearing ? Hearing->HearingRange : -1.0f,
		Damage ? TEXT("true") : TEXT("false"),
		GetPathFollowingComponent() ? static_cast<int32>(GetPathFollowingComponent()->GetStatus()) : -1);

#if WITH_GAMEPLAY_DEBUGGER
	if (StateTreeAI)
	{
		const TArray<FName> ActiveStates = StateTreeAI->GetActiveStateNames();
		UE_LOG(LogTemp, Display, TEXT("HELIOS_AI_QA phase=%s controller=%s active_states=[%s] tree_debug=%s"),
			Phase, *GetPathName(),
			*FString::JoinBy(ActiveStates, TEXT(","), [](FName Name) { return Name.ToString(); }),
			*StateTreeAI->GetDebugInfoString());
	}
#endif
}
