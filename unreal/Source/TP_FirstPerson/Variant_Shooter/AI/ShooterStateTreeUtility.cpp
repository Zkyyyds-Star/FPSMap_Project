// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/AI/ShooterStateTreeUtility.h"
#include "StateTreeExecutionContext.h"
#include "ShooterNPC.h"
#include "Camera/CameraComponent.h"
#include "AIController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "ShooterAIController.h"
#include "StateTreeAsyncExecutionContext.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
bool IsHeliosEnemyQA()
{
	FString Mode;
	return FParse::Value(FCommandLine::Get(), TEXT("HeliosEnemyQA="), Mode)
		|| FParse::Param(FCommandLine::Get(), TEXT("HeliosEnemyQA"));
}

FString QAActor(const AActor* Actor)
{
	return Actor ? FString::Printf(TEXT("%s class=%s tags=%s"), *Actor->GetPathName(),
		*Actor->GetClass()->GetPathName(),
		*FString::JoinBy(Actor->Tags, TEXT(","), [](FName Tag) { return Tag.ToString(); })) : TEXT("None");
}

/**
 * The copied 5.8 Shooter linked tree can lose its AActor parameter value while
 * instantiating the linked asset after its schema is widened from the template BP.
 * Perception still writes the authoritative target to AShooterAIController. Keep
 * the official StateTree tasks and conditions, but let all three consumers use the
 * same target source when their serialized input binding is empty.
 */
AActor* ResolveShooterTarget(AActor* BoundTarget, const AShooterNPC* Character, const AAIController* Controller = nullptr)
{
	const AAIController* EffectiveController = Controller;
	if (!EffectiveController && IsValid(Character))
	{
		EffectiveController = Cast<AAIController>(Character->GetController());
	}
	const AShooterAIController* ShooterController = Cast<AShooterAIController>(EffectiveController);
	// Enhanced awareness owns the live target, including an intentionally empty target
	// during investigation. Never resurrect a stale linked-tree input binding.
	if (ShooterController && ShooterController->UsesEnhancedAwareness())
	{
		return ShooterController->GetCurrentTarget();
	}
	if (IsValid(BoundTarget)) return BoundTarget;
	AActor* ControllerTarget = ShooterController ? ShooterController->GetCurrentTarget() : nullptr;
	return IsValid(ControllerTarget) ? ControllerTarget : nullptr;
}
}

bool FStateTreeLineOfSightToTargetCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	AActor* Target = ResolveShooterTarget(InstanceData.Target, InstanceData.Character);

	// ensure the bound context and target are valid
	if (!IsValid(InstanceData.Character) || !IsValid(Target))
	{
		if (IsHeliosEnemyQA())
		{
			UE_LOG(LogTemp, Warning, TEXT("HELIOS_AI_QA phase=los_condition character=%s target=%s result=%s reason=invalid_binding"),
				*QAActor(InstanceData.Character), *QAActor(Target),
				!InstanceData.bMustHaveLineOfSight ? TEXT("true") : TEXT("false"));
		}
		return !InstanceData.bMustHaveLineOfSight;
	}
	// The minion's shooting task is an engagement command: its native movement
	// closes range/finds a firing lane and its notify performs the actual LOS test.
	// Keep the StateTree in this branch while awareness owns a live target, so
	// linked-tree EQS movement cannot compete with tactical MoveTo requests.
	if (InstanceData.Character->UsesTacticalCombat())
	{
		const bool bEngaged = !Target->IsActorBeingDestroyed() && !Target->ActorHasTag(TEXT("Dead"));
		return bEngaged == InstanceData.bMustHaveLineOfSight;
	}
	
	// check if the character is facing towards the target
	const FVector TargetDir = (Target->GetActorLocation() - InstanceData.Character->GetActorLocation()).GetSafeNormal();

	const float FacingDot = FVector::DotProduct(TargetDir, InstanceData.Character->GetActorForwardVector());
	const float MaxDot = FMath::Cos(FMath::DegreesToRadians(InstanceData.LineOfSightConeAngle));

	// is the facing outside of our cone half angle?
	if (FacingDot <= MaxDot)
	{
		if (IsHeliosEnemyQA())
		{
			const USkeletalMeshComponent* Mesh = InstanceData.Character->GetMesh();
			UE_LOG(LogTemp, Display,
				TEXT("HELIOS_AI_QA phase=los_condition character=%s target=%s result=%s reason=facing actor_yaw=%.2f mesh_relative_yaw=%.2f forward=%s target_dir=%s facing_dot=%.4f required_dot=%.4f cone=%.2f"),
				*QAActor(InstanceData.Character), *QAActor(Target),
				!InstanceData.bMustHaveLineOfSight ? TEXT("true") : TEXT("false"),
				InstanceData.Character->GetActorRotation().Yaw,
				Mesh ? Mesh->GetRelativeRotation().Yaw : 0.0f,
				*InstanceData.Character->GetActorForwardVector().ToCompactString(), *TargetDir.ToCompactString(),
				FacingDot, MaxDot, InstanceData.LineOfSightConeAngle);
		}
		return !InstanceData.bMustHaveLineOfSight;
	}

	// get the target's bounding box
	FVector CenterOfMass, Extent;
	Target->GetActorBounds(true, CenterOfMass, Extent, false);

	// divide the vertical extent by the number of line of sight checks we'll do
	const float ExtentZOffset = Extent.Z * 2.0f / InstanceData.NumberOfVerticalLineOfSightChecks;

	// get the character's camera location as the source for the line checks
	const FVector Start = InstanceData.Character->GetFirstPersonCameraComponent()->GetComponentLocation();

	// ignore the character and target. We want to ensure there's an unobstructed trace not counting them
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(InstanceData.Character);
	QueryParams.AddIgnoredActor(Target);

	FHitResult OutHit;

	// run a number of vertically offset line traces to the target location
	for (int32 i = 0; i < InstanceData.NumberOfVerticalLineOfSightChecks - 1; ++i)
	{
		// calculate the endpoint for the trace
		const FVector End = CenterOfMass + FVector(0.0f, 0.0f, Extent.Z - ExtentZOffset * i);

		InstanceData.Character->GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, QueryParams);

		// is the trace unobstructed?
		if (!OutHit.bBlockingHit)
		{
			if (IsHeliosEnemyQA())
			{
				UE_LOG(LogTemp, Display,
					TEXT("HELIOS_AI_QA phase=los_condition character=%s target=%s result=%s reason=clear_trace actor_yaw=%.2f facing_dot=%.4f trace_index=%d"),
					*QAActor(InstanceData.Character), *QAActor(Target),
					InstanceData.bMustHaveLineOfSight ? TEXT("true") : TEXT("false"),
					InstanceData.Character->GetActorRotation().Yaw, FacingDot, i);
			}
			// we only need one unobstructed trace, so terminate early
			return InstanceData.bMustHaveLineOfSight;
		}
	}

	// no line of sight found
	if (IsHeliosEnemyQA())
	{
		UE_LOG(LogTemp, Display,
			TEXT("HELIOS_AI_QA phase=los_condition character=%s target=%s result=%s reason=blocked actor_yaw=%.2f facing_dot=%.4f blocker=%s"),
			*QAActor(InstanceData.Character), *QAActor(Target),
			!InstanceData.bMustHaveLineOfSight ? TEXT("true") : TEXT("false"),
			InstanceData.Character->GetActorRotation().Yaw, FacingDot, *QAActor(OutHit.GetActor()));
	}
	return !InstanceData.bMustHaveLineOfSight;
}

#if WITH_EDITOR
FText FStateTreeLineOfSightToTargetCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting /*= EStateTreeNodeFormatting::Text*/) const
{
	return FText::FromString("<b>Has Line of Sight</b>");
}
#endif

////////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeFaceActorTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// get the instance data
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	AActor* Target = ResolveShooterTarget(InstanceData.ActorToFaceTowards, nullptr, InstanceData.Controller);

	if (!IsValid(InstanceData.Controller) || !IsValid(Target))
	{
		return EStateTreeRunStatus::Failed;
	}

	// SetFocus updates controller rotation. Native Helios enemies normally orient
	// to movement, so a stationary pawn would otherwise never turn and could never
	// satisfy the child StateTree's actor-forward LOS cone.
	if (ACharacter* Character = Cast<ACharacter>(InstanceData.Controller->GetPawn()))
	{
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			InstanceData.bPreviousOrientRotationToMovement = Movement->bOrientRotationToMovement;
			InstanceData.bPreviousUseControllerDesiredRotation = Movement->bUseControllerDesiredRotation;
			InstanceData.bSavedRotationSettings = true;
			Movement->bOrientRotationToMovement = false;
			Movement->bUseControllerDesiredRotation = true;
		}
	}

	InstanceData.Controller->SetFocus(Target);
	if (IsHeliosEnemyQA())
	{
		UE_LOG(LogTemp, Display, TEXT("HELIOS_AI_QA phase=face_actor_enter controller=%s pawn=%s target=%s"),
			*GetPathNameSafe(InstanceData.Controller), *QAActor(InstanceData.Controller->GetPawn()),
			*QAActor(Target));
	}

	return EStateTreeRunStatus::Running;
}

void FStateTreeFaceActorTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// get the instance data
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	// clear the AI Controller's focus
	if (IsValid(InstanceData.Controller))
	{
		InstanceData.Controller->ClearFocus(EAIFocusPriority::Gameplay);
		if (InstanceData.bSavedRotationSettings)
		{
			if (ACharacter* Character = Cast<ACharacter>(InstanceData.Controller->GetPawn()))
			{
				if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
				{
					Movement->bOrientRotationToMovement = InstanceData.bPreviousOrientRotationToMovement;
					Movement->bUseControllerDesiredRotation = InstanceData.bPreviousUseControllerDesiredRotation;
				}
			}
		}
	}
	InstanceData.bSavedRotationSettings = false;
}

#if WITH_EDITOR
FText FStateTreeFaceActorTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting /*= EStateTreeNodeFormatting::Text*/) const
{
	return FText::FromString("<b>Face Towards Actor</b>");
}
#endif // WITH_EDITOR

////////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeFaceLocationTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// get the instance data
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	// set the AI Controller's focus
	InstanceData.Controller->SetFocalPoint(InstanceData.FaceLocation);

	return EStateTreeRunStatus::Running;
}

void FStateTreeFaceLocationTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// get the instance data
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	// clear the AI Controller's focus
	InstanceData.Controller->ClearFocus(EAIFocusPriority::Gameplay);
}

#if WITH_EDITOR
FText FStateTreeFaceLocationTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting /*= EStateTreeNodeFormatting::Text*/) const
{
	return FText::FromString("<b>Face Towards Location</b>");
}
#endif // WITH_EDITOR

////////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeSetRandomFloatTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// get the instance data
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	// calculate the output value
	InstanceData.OutValue = FMath::RandRange(InstanceData.MinValue, InstanceData.MaxValue);

	return EStateTreeRunStatus::Running;
}

#if WITH_EDITOR
FText FStateTreeSetRandomFloatTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting /*= EStateTreeNodeFormatting::Text*/) const
{
	return FText::FromString("<b>Set Random Float</b>");
}
#endif // WITH_EDITOR

////////////////////////////////////////////////////////////////////

EStateTreeRunStatus FStateTreeShootAtTargetTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// get the instance data
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	AActor* Target = ResolveShooterTarget(InstanceData.Target, InstanceData.Character);
	if (IsHeliosEnemyQA())
	{
		UE_LOG(LogTemp, Display, TEXT("HELIOS_AI_QA phase=shoot_task_enter character=%s target=%s binding_target=%s fallback=%s"),
			*QAActor(InstanceData.Character), *QAActor(Target), *QAActor(InstanceData.Target),
			!IsValid(InstanceData.Target) && IsValid(Target) ? TEXT("controller_target") : TEXT("none"));
	}

	// tell the character to shoot the target
	if (!IsValid(InstanceData.Character) || !IsValid(Target))
	{
		return EStateTreeRunStatus::Failed;
	}
	InstanceData.Character->StartShooting(Target);

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeShootAtTargetTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (IsValid(Data.Character) && Data.Character->UsesTacticalCombat())
	{
		AActor* Target = ResolveShooterTarget(Data.Target, Data.Character);
		if (IsValid(Target)) Data.Character->StartShooting(Target);
		else Data.Character->StopShooting();
	}
	return EStateTreeRunStatus::Running;
}

void FStateTreeShootAtTargetTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// get the instance data
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (IsHeliosEnemyQA())
	{
		UE_LOG(LogTemp, Display, TEXT("HELIOS_AI_QA phase=shoot_task_exit character=%s target=%s"),
			*QAActor(InstanceData.Character), *QAActor(InstanceData.Target));
	}

	// tell the character to stop shooting
	InstanceData.Character->StopShooting();
}

#if WITH_EDITOR
FText FStateTreeShootAtTargetTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting /*= EStateTreeNodeFormatting::Text*/) const
{
	return FText::FromString("<b>Shoot at Target</b>");
}
#endif // WITH_EDITOR

EStateTreeRunStatus FStateTreeSenseEnemiesTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// get the instance data
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!IsValid(InstanceData.Controller)) return EStateTreeRunStatus::Failed;
	if (InstanceData.Controller->UsesEnhancedAwareness())
	{
		InstanceData.Controller->OnShooterAwarenessChanged.BindLambda(
			[WeakContext = Context.MakeWeakExecutionContext()]()
			{
				const FStateTreeStrongExecutionContext StrongContext = WeakContext.MakeStrongExecutionContext();
				if (FInstanceDataType* Data = StrongContext.GetInstanceDataPtr<FInstanceDataType>())
				{
					if (!IsValid(Data->Controller)) return;
					Data->TargetActor = Data->Controller->GetCurrentTarget();
					Data->InvestigateLocation = Data->Controller->GetInvestigationLocation();
					if (IsValid(Data->TargetActor)) StrongContext.BroadcastDelegate(Data->OnSeeEnemyDelegate);
					else if (Data->Controller->IsInvestigating()) StrongContext.BroadcastDelegate(Data->OnInvestigateLocationDelegate);
					else StrongContext.BroadcastDelegate(Data->OnForgetEnemyDelegate);
				}
			});
		InstanceData.TargetActor = InstanceData.Controller->GetCurrentTarget();
		InstanceData.InvestigateLocation = InstanceData.Controller->GetInvestigationLocation();
		if (InstanceData.TargetActor || InstanceData.Controller->IsInvestigating())
			InstanceData.Controller->OnShooterAwarenessChanged.ExecuteIfBound();
		return EStateTreeRunStatus::Running;
	}
	if (IsHeliosEnemyQA())
	{
		UE_LOG(LogTemp, Display,
			TEXT("HELIOS_AI_QA phase=sense_task_enter controller=%s character=%s sense_tag=%s existing_target=%s"),
			*QAActor(InstanceData.Controller), *QAActor(InstanceData.Character),
			*InstanceData.SenseTag.ToString(), *QAActor(InstanceData.TargetActor));
	}

	// bind the perception updated delegate on the controller
	InstanceData.Controller->OnShooterPerceptionUpdated.BindLambda(
		[WeakContext = Context.MakeWeakExecutionContext()](AActor* SensedActor, const FAIStimulus& Stimulus)
		{
			// get the instance data inside the lambda
			const FStateTreeStrongExecutionContext StrongContext = WeakContext.MakeStrongExecutionContext();
			if (FInstanceDataType* LambdaInstanceData = StrongContext.GetInstanceDataPtr<FInstanceDataType>())
			{
				if (IsHeliosEnemyQA())
				{
					UE_LOG(LogTemp, Display,
						TEXT("HELIOS_AI_QA phase=sense_task_event actor=%s required_tag=%s actor_tag_match=%s stimulus_tag=%s stimulus_tag_match=%s sensed=%s"),
						*QAActor(SensedActor), *LambdaInstanceData->SenseTag.ToString(),
						SensedActor && SensedActor->ActorHasTag(LambdaInstanceData->SenseTag) ? TEXT("true") : TEXT("false"),
						*Stimulus.Tag.ToString(), Stimulus.Tag == LambdaInstanceData->SenseTag ? TEXT("true") : TEXT("false"),
						Stimulus.WasSuccessfullySensed() ? TEXT("true") : TEXT("false"));
				}
				// have we sensed the enemy directly?
				if (SensedActor->ActorHasTag(LambdaInstanceData->SenseTag))
				{
					// set the controller's target
					LambdaInstanceData->Controller->SetCurrentTarget(SensedActor);

					// set the task output
					LambdaInstanceData->TargetActor = SensedActor;

					// broadcast the see enemy delegate
					StrongContext.BroadcastDelegate(LambdaInstanceData->OnSeeEnemyDelegate);
				}
				// have we sensed something owned by the enemy? firing noise, bullet impacts, etc.
				else if (Stimulus.Tag == LambdaInstanceData->SenseTag)
				{
					bool bDirectLOS = false;

					// calculate the direction of the stimulus
					const FVector StimulusDir = (Stimulus.StimulusLocation - LambdaInstanceData->Character->GetActorLocation()).GetSafeNormal();

					// infer the angle from the dot product between the character facing and the stimulus direction
					const float DirDot = FVector::DotProduct(StimulusDir, LambdaInstanceData->Character->GetActorForwardVector());
					const float MaxDot = FMath::Cos(FMath::DegreesToRadians(LambdaInstanceData->DirectLineOfSightCone));

					// is the direction within our perception cone?
					if (DirDot >= MaxDot)
					{
						// run a line trace between the character and the sensed actor
						FCollisionQueryParams QueryParams;
						QueryParams.AddIgnoredActor(LambdaInstanceData->Character);
						QueryParams.AddIgnoredActor(SensedActor);

						FHitResult OutHit;

						// we have direct line of sight if this trace is unobstructed
						bDirectLOS = !LambdaInstanceData->Character->GetWorld()->LineTraceSingleByChannel(OutHit, LambdaInstanceData->Character->GetActorLocation(), SensedActor->GetActorLocation(), ECC_Visibility, QueryParams);

					}

					// check if we have a direct line of sight to the stimulus
					if (bDirectLOS)
					{
						// set the controller's target
						LambdaInstanceData->Controller->SetCurrentTarget(SensedActor);

						// set the task output
						LambdaInstanceData->TargetActor = SensedActor;

						// broadcast the see enemy delegate
						StrongContext.BroadcastDelegate(LambdaInstanceData->OnSeeEnemyDelegate);

					// no direct line of sight to target
					} else {

						// if we already have a target, ignore the partial sense and keep on them
						if (!IsValid(LambdaInstanceData->TargetActor))
						{
							// scale the last stimulus by time elapsed so we phase it out over time
							const float LastStimulusTime = LambdaInstanceData->Character->GetWorld()->GetTimeSeconds() - LambdaInstanceData->LastStimulusTime;
							const float ScaledStimulus = LambdaInstanceData->LastStimulusStrength / FMath::Max(LastStimulusTime, 1.0f);

							// is this stimulus stronger?
							if (Stimulus.Strength > ScaledStimulus)
							{
								// update the stimulus strength
								LambdaInstanceData->LastStimulusStrength = Stimulus.Strength;

								// update the stimulus time
								LambdaInstanceData->LastStimulusTime = LambdaInstanceData->Character->GetWorld()->GetTimeSeconds();

								// set the investigate location
								LambdaInstanceData->InvestigateLocation = Stimulus.StimulusLocation;

								// broadcast the investigate delegate
								StrongContext.BroadcastDelegate(LambdaInstanceData->OnInvestigateLocationDelegate);
							}
						}
					}
				}
			}
			else if (IsHeliosEnemyQA())
			{
				UE_LOG(LogTemp, Error, TEXT("HELIOS_AI_QA phase=sense_task_event context=expired actor=%s"), *QAActor(SensedActor));
			}
		}
	);

	// bind the perception forgotten delegate on the controller
	InstanceData.Controller->OnShooterPerceptionForgotten.BindLambda(
		[WeakContext = Context.MakeWeakExecutionContext()](AActor* SensedActor)
		{
			// get the instance data inside the lambda
			const FStateTreeStrongExecutionContext StrongContext = WeakContext.MakeStrongExecutionContext();
			if (FInstanceDataType* LambdaInstanceData = StrongContext.GetInstanceDataPtr<FInstanceDataType>())
			{
				// reset the stimulus strength
				LambdaInstanceData->LastStimulusStrength = 0.0f;

				bool bForget = false;

				// are we forgetting the current target?
				if (SensedActor == LambdaInstanceData->TargetActor)
				{
					bForget = true;
				}
				else 
				{
					// are we forgetting about a partial sense while we have no target?
					if (!IsValid(LambdaInstanceData->TargetActor))
					{
						bForget = true;
					}
				}

				if (bForget)
				{
					// clear the target
					LambdaInstanceData->TargetActor = nullptr;

					// clear the target on the controller
					LambdaInstanceData->Controller->ClearCurrentTarget();
					LambdaInstanceData->Controller->ClearFocus(EAIFocusPriority::Gameplay);

					// broadcast the forget delegate
					StrongContext.BroadcastDelegate(LambdaInstanceData->OnForgetEnemyDelegate);
				}
			}
		}
	);

	return EStateTreeRunStatus::Running;
}

void FStateTreeSenseEnemiesTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// get the instance data
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!IsValid(InstanceData.Controller)) return;
	if (InstanceData.Controller->UsesEnhancedAwareness())
	{
		InstanceData.Controller->OnShooterAwarenessChanged.Unbind();
		return;
	}
	if (IsHeliosEnemyQA())
	{
		UE_LOG(LogTemp, Display, TEXT("HELIOS_AI_QA phase=sense_task_exit controller=%s character=%s target=%s"),
			*QAActor(InstanceData.Controller), *QAActor(InstanceData.Character), *QAActor(InstanceData.TargetActor));
	}

	// unbind the perception delegates
	InstanceData.Controller->OnShooterPerceptionUpdated.Unbind();
	InstanceData.Controller->OnShooterPerceptionForgotten.Unbind();
}

#if WITH_EDITOR
FText FStateTreeSenseEnemiesTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting /*= EStateTreeNodeFormatting::Text*/) const
{
	return FText::FromString("<b>Sense Enemies</b>");
}
#endif // WITH_EDITOR
