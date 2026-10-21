#include "QA/HeliosBioHotelQA.h"
#include "Bio/HeliosBioHotelGameMode.h"
#include "Bio/HeliosBioNPC.h"
#include "Bio/HeliosBioMovingPlatform.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformMisc.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"

void AHeliosBioHotelQA::FinishFloatingAI(const FString& Blocker)
{
    if(TacticsBlocker)TacticsBlocker->Destroy();
    Stage=99;auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("passed"),Failures==0&&Blocker.IsEmpty());
    Report->SetStringField(TEXT("blocker"),Blocker);Report->SetNumberField(TEXT("failures"),Failures);
    Report->SetStringField(TEXT("scope"),TEXT("Controlled production-action/traversal fixtures. NPC actors are explicitly placed and actor decision ticks suspended; native CharacterMovement/collision remain active. Platform schedule is advanced explicitly for preflight/queue probes. These checks do not establish natural-round frequency or independent complete platform routes."));
    Report->SetArrayField(TEXT("checks"),Checks);Report->SetArrayField(TEXT("samples"),PlatformRouteEvidence);
    Report->SetArrayField(TEXT("platforms"),PlatformDiagnostics);
    FString Text;auto Writer=TJsonWriterFactory<>::Create(&Text);FJsonSerializer::Serialize(Report,Writer);
    FFileHelper::SaveStringToFile(Text,*(Out/TEXT("floating_ai_report.json")));FPlatformMisc::RequestExit(false);
}
void AHeliosBioHotelQA::TickFloatingAI(float Dt,double Now)
{
    using namespace HeliosBioPlatformTraversal;
    if(Now-Started>150){Check(TEXT("floating_AI_fixture_bounded"),false);FinishFloatingAI(TEXT("Fixture timeout"));return;}
    auto Place=[&](AHeliosBioNPC* Bot,const FVector& Feet)
    {
        ForgetBot(Bot);Bot->ConsumeMovementInputVector();Bot->GetCharacterMovement()->StopMovementImmediately();
        Bot->SetActorLocation(Feet+FVector(0,0,Bot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+6.f),false,nullptr,ETeleportType::TeleportPhysics);
        Bot->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
        if(auto* AI=Cast<AAIController>(Bot->GetController()))AI->StopMovement();
    };
    auto Based=[&](AHeliosBioNPC* Bot){return Bot&&Bot->GetMovementBase()==PhysicsPlatform->GetPlatformMesh()&&Bot->GetCharacterMovement()->IsMovingOnGround();};
    auto Advance=[&](AHeliosBioNPC* Bot,float Seconds){return TickTraversal(Bot,Cast<AAIController>(Bot->GetController()),Mode,TacticsRequest,Seconds);};
    auto Step=[&](int32 Number){Stage=Number;TacticsAt=Now;};
    if(Stage==0)
    {
        if(Now<Next)return;Mode=AHeliosBioHotelGameMode::Find(GetWorld());
        if(!Mode||!Mode->bFloatingProfile||Mode->Bots.Num()<3||Mode->MovingPlatforms.IsEmpty()){FinishFloatingAI(TEXT("Floating profile, NPCs or source platform unavailable"));return;}
        Mode->Model.Config.PreparationSeconds=.1f;Mode->Model.Config.RoundSeconds=1200.f;Mode->Model.Config.bKeepLocalPlayerHuman=true;
        Mode->RestartBioRound();Mode->Model.Tick(.2f);
        for(auto Bot:Mode->Bots){Bot->bQAFreeze=true;if(auto* AI=Cast<AAIController>(Bot->GetController()))AI->StopMovement();}
        TArray<AHeliosBioNPC*> Humans;
        for(auto Bot:Mode->Bots)if(const auto* P=Mode->Model.FindParticipant(Bot->ParticipantId);P&&P->Team==EHeliosBioTeam::Human)Humans.Add(Bot);
        if(Humans.Num()<3){FinishFloatingAI(TEXT("Three human fixture actors required"));return;}
        AIBoardBot=Humans[0];TacticsOpponent=Humans[1];TacticsSecond=Humans[2];PhysicsPlatform=Mode->MovingPlatforms[0];
        for(auto* Bot:{AIBoardBot.Get(),TacticsOpponent.Get(),TacticsSecond.Get()}){Bot->SetActorTickEnabled(false);Bot->ResetFloatingAction();}
        PhysicsPlatform->SetRouteRunning(false);
        RecordPlatformDiagnostics(TEXT("floating_ai_fixture_top"));
        const auto* Mesh=PhysicsPlatform->GetPlatformMesh();const FBox Bounds=Mesh->Bounds.GetBox();TArray<FVector> Floors;
        FCollisionQueryParams Q(SCENE_QUERY_STAT(BioFloatingQAActualTop),false);
        for(const auto& P:Mode->Model.GetParticipants())Q.AddIgnoredActor(Mode->ParticipantActor(P.Id));
        for(int32 X=0;X<7;++X)for(int32 Y=0;Y<7;++Y)
        {
            const FVector Probe(FMath::Lerp(Bounds.Min.X+45.f,Bounds.Max.X-45.f,X/6.f),FMath::Lerp(Bounds.Min.Y+45.f,Bounds.Max.Y-45.f,Y/6.f),Bounds.Max.Z);
            FHitResult Hit;if(GetWorld()->LineTraceSingleByChannel(Hit,Probe+FVector(0,0,80),Probe-FVector(0,0,Bounds.GetSize().Z+80),ECC_Visibility,Q)&&Hit.GetComponent()==Mesh&&Hit.ImpactNormal.Z>.8f)Floors.Add(Hit.ImpactPoint);
        }
        float Best=0.f;for(const auto& A:Floors)for(const auto& B:Floors)if(FVector::Dist2D(A,B)>Best){Best=FVector::Dist2D(A,B);TacticsA=A;TacticsB=B;}
        if(Best<120.f){FinishFloatingAI(FString::Printf(TEXT("Actual source platform supported separation=%.2f floors=%d bounds=%s"),Best,Floors.Num(),*Bounds.ToString()));return;}
        TacticsMiddle=TacticsA;float Middle=MAX_flt;
        for(const auto& P:Floors){const float D=FVector::Dist2D(P,(TacticsA+TacticsB)*.5f);if(D<Middle){Middle=D;TacticsMiddle=P;}}
        Place(AIBoardBot,TacticsA);Place(TacticsOpponent.Get(),TacticsB);
        if(auto* AI=Cast<AAIController>(AIBoardBot->GetController()))AI->SetFocus(TacticsOpponent.Get());
        auto* CameraActor=GetWorld()->SpawnActor<AActor>();auto* Camera=NewObject<UCameraComponent>(CameraActor);
        CameraActor->SetRootComponent(Camera);Camera->RegisterComponent();CameraActor->SetActorLocation(TacticsMiddle+FVector(-300,500,280));
        CameraActor->SetActorRotation((TacticsMiddle+FVector(0,0,70)-CameraActor->GetActorLocation()).Rotation());
        if(auto* PC=UGameplayStatics::GetPlayerController(GetWorld(),0))PC->SetViewTarget(CameraActor);
        TacticsRequest.bValid=true;TacticsRequest.Intent=TEXT("Chase");TacticsRequest.CombatTarget=TacticsOpponent;TacticsRequest.CombatDistance=75.f;TacticsRequest.DestinationFeet=TacticsB;
        Check(TEXT("fixture_uses_original_supported_platform"),true);Step(1);return;
    }
    if(!AIBoardBot||!TacticsOpponent.IsValid()){FinishFloatingAI(TEXT("Fixture actors lost"));return;}
    if(Now>=RouteSampleAt)
    {
        RouteSampleAt=Now+.2;auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("stage"),Stage);Row->SetStringField(TEXT("position"),AIBoardBot->GetActorLocation().ToString());
        Row->SetStringField(TEXT("velocity"),AIBoardBot->GetVelocity().ToString());Row->SetStringField(TEXT("traversal"),DebugState(AIBoardBot));Row->SetStringField(TEXT("action"),AIBoardBot->GetFloatingActionDiagnostic());
        Row->SetBoolField(TEXT("actual_platform_base"),Based(AIBoardBot));PlatformRouteEvidence.Add(MakeShared<FJsonValueObject>(Row));
    }
    switch(Stage)
    {
    case 1:
        if(!Based(AIBoardBot)||!Based(TacticsOpponent.Get())){if(Now-TacticsAt>3){FinishFloatingAI(TEXT("Local combat actors failed actual gravity landing"));}return;}
        TacticsDistance=FVector::Dist2D(AIBoardBot->GetActorLocation(),TacticsOpponent->GetActorLocation());Step(2);break;
    case 2:
        Advance(AIBoardBot,Dt);
        if(Now-TacticsAt<2.f)return;
        Check(TEXT("same_platform_pursuit_closes_distance"),TacticsDistance-FVector::Dist2D(AIBoardBot->GetActorLocation(),TacticsOpponent->GetActorLocation())>30.f);
        Check(TEXT("same_platform_pursuit_preserves_support"),Based(AIBoardBot));Capture(TEXT("floating_ai_platform_pursuit"));
        Step(12);break;
    case 12:
        if(Now-TacticsAt<.1)return;
        Place(AIBoardBot,TacticsMiddle);TacticsRequest.Intent=TEXT("Retreat");TacticsRequest.bKeepDistance=true;TacticsRequest.bUrgent=true;TacticsRequest.CombatDistance=850.f;TacticsRequest.DestinationFeet=TacticsA;Step(3);break;
    case 3:
        if(!Based(AIBoardBot)){if(Now-TacticsAt>3)FinishFloatingAI(TEXT("Retreat actor did not land"));return;}
        TacticsDistance=FVector::Dist2D(AIBoardBot->GetActorLocation(),TacticsOpponent->GetActorLocation());Step(4);break;
    case 4:
        Advance(AIBoardBot,Dt);if(Now-TacticsAt<2.f)return;
        Check(TEXT("same_platform_retreat_increases_distance"),FVector::Dist2D(AIBoardBot->GetActorLocation(),TacticsOpponent->GetActorLocation())-TacticsDistance>25.f);
        Check(TEXT("same_platform_retreat_stays_on_supported_top"),Based(AIBoardBot));Capture(TEXT("floating_ai_platform_retreat"));
        Step(13);break;
    case 13:
        if(Now-TacticsAt<.1)return;
        Place(TacticsOpponent.Get(),Mode->SpawnLocation(TacticsOpponent->ParticipantId)-FVector(0,0,TacticsOpponent->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
        TacticsRequest.bUrgent=false;TacticsRequest.bKeepDistance=false;TacticsRequest.Intent=TEXT("Chase");TacticsRequest.DestinationFeet=TacticsOpponent->GetNavAgentLocation();PhysicsPlatform->SetRouteRunning(true);Step(5);break;
    case 5:
        Advance(AIBoardBot,Dt);if(Now-TacticsAt<1.2f)return;
        Check(TEXT("opponent_leaves_platform_refreshes_departure"),!DebugState(AIBoardBot).Contains(TEXT("platform_local_pursuit")));
        ForgetBot(AIBoardBot);Check(TEXT("rider_avoidance_restored"),AIBoardBot->GetCharacterMovement()->bUseRVOAvoidance);
        Place(AIBoardBot,Mode->SpawnLocation(AIBoardBot->ParticipantId)-FVector(0,0,AIBoardBot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));Step(6);break;
    case 6:
        if(!AIBoardBot->GetCharacterMovement()->IsMovingOnGround())return;
        {
            auto* P=const_cast<FHeliosBioParticipant*>(Mode->Model.FindParticipant(AIBoardBot->ParticipantId));P->Team=EHeliosBioTeam::Human;
            AIBoardBot->ResetFloatingAction();AIBoardBot->ThinkFloatingMovement(nullptr,false);
            const FVector Held=AIBoardBot->FloatingAction.DestinationFeet;const FName Intent=AIBoardBot->FloatingAction.Intent;
            AIBoardBot->ThinkFloatingMovement(nullptr,false);
            Check(TEXT("action_commitment_holds_across_decisions"),AIBoardBot->FloatingAction.DestinationFeet==Held&&AIBoardBot->FloatingAction.Intent==Intent);
            Place(TacticsOpponent.Get(),AIBoardBot->GetNavAgentLocation()+FVector(200,0,0));
            AIBoardBot->FloatingAction.bValid=true;AIBoardBot->FloatingAction.Intent=TEXT("Supply");AIBoardBot->FloatingActionUntil=GetWorld()->GetTimeSeconds()+10.f;
            AIBoardBot->ThinkFloatingMovement(TacticsOpponent.Get(),true);
            Check(TEXT("danger_preempts_locked_supply_action"),AIBoardBot->FloatingAction.Intent==TEXT("Retreat")&&AIBoardBot->FloatingAction.bUrgent);
            AIBoardBot->RecoveryRemaining=1.6f;const FVector RecoveryGoal=AIBoardBot->MovementGoal;const FName RecoveryIntent=AIBoardBot->MovementIntent;
            AIBoardBot->ThinkFloatingMovement(TacticsOpponent.Get(),true);
            Check(TEXT("recovery_owns_movement_until_complete"),AIBoardBot->MovementGoal==RecoveryGoal&&AIBoardBot->MovementIntent==RecoveryIntent);AIBoardBot->RecoveryRemaining=0.f;
            // Return the opponent to the actual moving top using its current translated floor.
            const FVector Shift=PhysicsPlatform->GetActorLocation()-PhysicsPlatform->GetRoutePoints()[0];Place(TacticsOpponent.Get(),TacticsMiddle+Shift);
            AIBoardBot->ResetFloatingAction();Place(AIBoardBot,Mode->SpawnLocation(AIBoardBot->ParticipantId)-FVector(0,0,AIBoardBot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));Step(7);
        }
        break;
    case 7:
        if(!Based(TacticsOpponent.Get())||!AIBoardBot->GetCharacterMovement()->IsMovingOnGround()){if(Now-TacticsAt>4)FinishFloatingAI(TEXT("Boarding fixture actors failed actual landing"));return;}
        TacticsRequest={};TacticsRequest.bValid=true;TacticsRequest.Intent=TEXT("Chase");TacticsRequest.CombatTarget=TacticsOpponent;TacticsRequest.DestinationFeet=TacticsOpponent->GetNavAgentLocation();
        for(auto Candidate:Mode->Bots)if(Candidate!=TacticsOpponent.Get()&&Candidate->GetCharacterMovement()->IsMovingOnGround()&&
            Mode->Model.FindParticipant(Candidate->ParticipantId)->Team==EHeliosBioTeam::Human)
        {
            ForgetBot(Candidate);TickTraversal(Candidate,Cast<AAIController>(Candidate->GetController()),Mode,TacticsRequest,0.f);
            UE_LOG(LogTemp,Display,TEXT("BIO_FLOATING_QA_ROUTE_CANDIDATE id=%d %s"),Candidate->ParticipantId,*DebugState(Candidate));
            if(GetDiagnostics(Candidate).Step==0)
            {
                AIBoardBot=Candidate;Candidate->SetActorTickEnabled(false);
                if(TacticsSecond.Get()==Candidate)for(auto Other:Mode->Bots)if(Other!=Candidate&&Other!=TacticsOpponent.Get()&&Mode->Model.FindParticipant(Other->ParticipantId)->Team==EHeliosBioTeam::Human)
                {TacticsSecond=Other;Other->SetActorTickEnabled(false);break;}
                break;
            }
        }
        {
            const auto D=GetDiagnostics(AIBoardBot);if(D.Step!=0){FinishFloatingAI(TEXT("No connected production boarding route from fixture spawn"));return;}
            const FVector Entry=D.MovementGoal;Place(AIBoardBot,Entry);Step(8);
        }
        break;
    case 8:
        if(!AIBoardBot->GetCharacterMovement()->IsMovingOnGround())return;
        Advance(AIBoardBot,0.f);Advance(AIBoardBot,0.f);
        {
            auto D=GetDiagnostics(AIBoardBot);Check(TEXT("real_dock_wait_has_bounded_ETA"),D.Step==5&&D.WaitRemaining>0.f&&D.WaitRemaining<=PhysicsPlatform->GetRouteCycleSeconds()+3.1f);
            TacticsRequest.bUrgent=true;const bool Owned=Advance(AIBoardBot,0.f);
            Check(TEXT("dock_threat_cancels_on_next_evaluation"),!Owned&&GetDiagnostics(AIBoardBot).Step==4);
            TacticsRequest.bUrgent=false;ForgetBot(AIBoardBot);Advance(AIBoardBot,0.f);Advance(AIBoardBot,0.f);
            TacticsRequest.bValid=false;Check(TEXT("obsolete_goal_cancels_dock_wait"),!Advance(AIBoardBot,0.f)&&GetDiagnostics(AIBoardBot).Step==4);
            TacticsRequest.bValid=true;ForgetBot(AIBoardBot);Advance(AIBoardBot,0.f);Advance(AIBoardBot,0.f);
            PhysicsPlatform->SetRouteRunning(false);Check(TEXT("stopped_platform_cancels_wait"),!Advance(AIBoardBot,0.f)&&GetDiagnostics(AIBoardBot).Step==4);PhysicsPlatform->SetRouteRunning(true);
            ForgetBot(AIBoardBot);Advance(AIBoardBot,0.f);Advance(AIBoardBot,0.f);D=GetDiagnostics(AIBoardBot);
            float BoardAt,ExitAt;
            if(!PhysicsPlatform->PredictRouteJourney(D.EntryIndex,INDEX_NONE,0.f,BoardAt,ExitAt)){FinishFloatingAI(TEXT("Fixture dock prediction failed"));return;}
            // Explicit controlled schedule advance, not natural waiting evidence.
            PhysicsPlatform->Tick(BoardAt);Step(9);
        }
        break;
    case 9:
        if(Now-TacticsAt<.2)return;
        {
            const auto* Mesh=PhysicsPlatform->GetPlatformMesh();TacticsBlocker=GetWorld()->SpawnActor<AActor>();
            auto* Box=NewObject<UBoxComponent>(TacticsBlocker);TacticsBlocker->SetRootComponent(Box);Box->SetBoxExtent(FVector(Mesh->Bounds.BoxExtent.X+10,Mesh->Bounds.BoxExtent.Y+10,180));
            Box->SetCanEverAffectNavigation(false);Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Box->SetCollisionResponseToAllChannels(ECR_Ignore);Box->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);Box->RegisterComponent();
            TacticsBlocker->SetActorLocation(Mesh->Bounds.Origin+FVector(0,0,Mesh->Bounds.BoxExtent.Z+180));
            TacticsBlocker->AttachToActor(PhysicsPlatform,FAttachmentTransformRules::KeepWorldTransform);
            Advance(AIBoardBot,.25f);Check(TEXT("blocked_ballistic_preflight_does_not_launch"),!IsInFlight(AIBoardBot)&&DebugState(AIBoardBot).Contains(TEXT("blocked")));
            TacticsBlocker->Destroy();TacticsBlocker=nullptr;
            TacticsSecond->SetActorLocation(AIBoardBot->GetActorLocation()+FVector(0,90,0),false,nullptr,ETeleportType::TeleportPhysics);
            TacticsSecond->GetCharacterMovement()->SetMovementMode(MOVE_Falling);bAIBoardLaunchObserved=false;bAIBoardCarried=false;Step(10);
        }
        break;
    case 10:
        Advance(AIBoardBot,Dt);Advance(TacticsSecond.Get(),Dt);
        bAIBoardLaunchObserved|=IsInFlight(AIBoardBot)||IsInFlight(TacticsSecond.Get());
        bAIBoardCarried|=IsInFlight(AIBoardBot)&&IsInFlight(TacticsSecond.Get());
        if(Now-TacticsAt<2.f)return;
        Check(TEXT("boarding_queue_permits_a_real_launch"),bAIBoardLaunchObserved);
        Check(TEXT("boarding_queue_never_launches_two_at_once"),!bAIBoardCarried);
        {
            const auto Before=GetDiagnostics(AIBoardBot);Advance(AIBoardBot,0.f);Advance(AIBoardBot,0.f);
            Check(TEXT("airborne_repeated_ticks_do_not_add_boardings"),GetDiagnostics(AIBoardBot).Boardings==Before.Boardings);
            auto* P=const_cast<FHeliosBioParticipant*>(Mode->Model.FindParticipant(AIBoardBot->ParticipantId));P->bAlive=false;Advance(AIBoardBot,0.f);
            Check(TEXT("death_clears_platform_state"),GetDiagnostics(AIBoardBot).Step==INDEX_NONE);P->bAlive=true;
            AIBoardBot->FloatingAction.bValid=true;AIBoardBot->LastTeam=EHeliosBioTeam::Human;P->Team=EHeliosBioTeam::Zombie;
            AIBoardBot->BeginHumanInfectionTransition();Check(TEXT("infection_clears_action_and_platform"),!AIBoardBot->FloatingAction.bValid&&GetDiagnostics(AIBoardBot).Step==INDEX_NONE);
            AIBoardBot->ClearHumanInfectionTransition();AIBoardBot->ResetForBioRound();
            Check(TEXT("round_reset_clears_action_and_restores_avoidance"),!AIBoardBot->FloatingAction.bValid&&GetDiagnostics(AIBoardBot).Step==INDEX_NONE&&AIBoardBot->GetCharacterMovement()->bUseRVOAvoidance);
            Step(11);
        }
        break;
    case 11:
        if(Now-TacticsAt>.5)FinishFloatingAI();break;
    }
}
