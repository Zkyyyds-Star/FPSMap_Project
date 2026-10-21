#include "Bio/HeliosBioNPC.h"
#include "Bio/HeliosBioHotelGameMode.h"
#include "Bio/HeliosBioMovingPlatform.h"
#include "Bio/HeliosBioSupply.h"
#include "Bio/HeliosBioSupplyWeaponDefinition.h"
#include "Bio/HeliosBioRoundInventoryComponent.h"
#include "Bio/HeliosBioFourSlashComponent.h"
#include "Bio/HeliosBioStaticJumpLink.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
bool FloatingPath(AHeliosBioNPC* Bot,const FVector& Wanted,FVector& Out,float* Length=nullptr,float HeightTolerance=100.f)
{
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(Bot->GetWorld());FNavLocation At;
    if(!Nav||!Nav->ProjectPointToNavigation(Wanted,At,FVector(100,100,120),&Bot->GetNavAgentPropertiesRef())||FMath::Abs(At.Location.Z-Wanted.Z)>HeightTolerance)return false;
    auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(Bot->GetWorld(),Bot->GetNavAgentLocation(),At.Location,Bot);
    if(!Path||!Path->IsValid()||Path->IsPartial())return false;
    Out=At.Location;if(Length)*Length=Path->GetPathLength();return true;
}
bool FiringLine(AHeliosBioNPC* Bot,const FVector& Feet,AActor* Target)
{
    if(!Target)return true;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(BioFloatingPositionSight),false,Bot);
    TArray<AActor*> Attached;Bot->GetAttachedActors(Attached,true,true);for(auto* A:Attached)Q.AddIgnoredActor(A);
    FHitResult Hit;
    return !Bot->GetWorld()->LineTraceSingleByChannel(Hit,Feet+FVector(0,0,Bot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+40.f),Target->GetActorLocation()+FVector(0,0,25),ECC_Visibility,Q)||Hit.GetActor()==Target;
}
float Occupancy(AHeliosBioNPC* Bot,AHeliosBioHotelGameMode* Mode,const FVector& At,float Radius)
{
    float Cost=0.f;
    for(auto Other:Mode->Bots)if(IsValid(Other)&&Other!=Bot)
    {
        const auto* P=Mode->Model.FindParticipant(Other->ParticipantId);if(!P||!P->bAlive||Mode->AreOpponents(Bot->ParticipantId,Other->ParticipantId))continue;
        const FVector OtherGoal=Other->IsMoveIntentActive()?Other->GetMovementGoal():Other->GetNavAgentLocation();
        if(FMath::Abs(OtherGoal.Z-At.Z)<140.f)Cost+=FMath::Max(0.f,1.f-FVector::Dist2D(OtherGoal,At)/Radius);
    }
    return Cost;
}
int32 EscapeDirections(AHeliosBioNPC* Bot,const FVector& Feet)
{
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(Bot->GetWorld());if(!Nav)return 0;
    int32 Count=0;
    for(int32 I=0;I<4;++I)
    {
        const float Angle=PI*.5f*I;FNavLocation At;
        if(!Nav->ProjectPointToNavigation(Feet+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*220.f,At,FVector(50,50,60),&Bot->GetNavAgentPropertiesRef()))continue;
        // A projected point beyond a gap is not a walkable escape.
        FHitResult Block;
        if(!Nav->NavigationRaycast(Bot->GetWorld(),Feet,At.Location,Block.Location,nullptr,Bot->GetController()))++Count;
    }
    return Count;
}
}

void AHeliosBioNPC::ResetFloatingAction()
{
    FloatingAction={};FloatingActionActor.Reset();FloatingActionUntil=FloatingPlanAt=FailedFloatingUntil=0.f;
    FloatingActionReason.Empty();FailedFloatingGoal=FVector::ZeroVector;
}
FString AHeliosBioNPC::GetFloatingActionDiagnostic() const
{
    return FString::Printf(TEXT("action=%s valid=%d urgent=%d destination=%s actor=%s reason=%s"),
        *FloatingAction.Intent.ToString(),FloatingAction.bValid,FloatingAction.bUrgent,*FloatingAction.DestinationFeet.ToString(),*GetNameSafe(FloatingActionActor.Get()),*FloatingActionReason);
}
bool AHeliosBioNPC::ChooseFloatingDefenseGoal()
{
    auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());if(!Mode)return false;
    const FVector Feet=GetNavAgentLocation();float Best=-MAX_flt;FVector Choice=Feet;
    FRandomStream Random(ParticipantRoundSeed+ParticipantId*7919);
    auto Consider=[&](const FVector& Wanted)
    {
        if(FVector::Dist2D(Feet,Wanted)>2400.f)return;
        FVector At;float Length;if(!FloatingPath(this,Wanted,At,&Length))return;
        const int32 Exits=EscapeDirections(this,At);if(Exits<2)return;
        const float Score=FMath::Clamp(At.Z-Feet.Z,-300.f,500.f)*.6f-Length*.12f+
            Exits*65.f-Occupancy(this,Mode,At,300.f)*350.f+(FiringLine(this,At,Target.Get())?150.f:0.f)+Random.FRandRange(0.f,60.f);
        if(Score>Best){Best=Score;Choice=At;}
    };
    Consider(Feet);
    for(TActorIterator<AActor> It(GetWorld());It;++It)if(It->ActorHasTag(TEXT("BioSpawn")))Consider(It->GetActorLocation());
    for(int32 I=0;I<8;++I){const float Angle=2*PI*I/8.f+Random.FRandRange(-.15f,.15f);Consider(Feet+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*(I%2?550.f:1000.f));}
    bDefenseGoalValid=Best>-MAX_flt;DefenseGoal=Choice;return bDefenseGoalValid;
}
void AHeliosBioNPC::ThinkFloatingMovement(AActor* CombatTarget,bool Clear)
{
    auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=Mode?Mode->Model.FindParticipant(ParticipantId):nullptr;
    auto* AI=Cast<AAIController>(Controller);if(!Mode||!Mode->bFloatingProfile||!P||!AI)return;
    const float Now=GetWorld()->GetTimeSeconds();const FVector Feet=GetNavAgentLocation();
    const float Distance=CombatTarget?FVector::Dist(GetActorLocation(),CombatTarget->GetActorLocation()):MAX_flt;
    const bool Human=P->Team==EHeliosBioTeam::Human;
    const bool Ranged=Human||bRoleRangedReady;
    const bool Retreating=FloatingAction.Intent==TEXT("Retreat");
    const bool Danger=Ranged&&CombatTarget&&Clear&&Distance<(Retreating?1100.f:850.f);
    FloatingAction.CombatTarget=CombatTarget;FloatingAction.bKeepDistance=Human||bRoleRangedReady;
    FloatingAction.CombatDistance=Human?850.f:bRoleRangedReady?900.f:P->IsHunter()?140.f:75.f;
    FloatingAction.bUrgent=Danger;
    if(RecoveryRemaining>0.f||bNavigationReconnect||FourSlash->IsBusy()||AHeliosBioStaticJumpLink::IsTraversing(this)||HeliosBioPlatformTraversal::IsInFlight(this))return;
    bool InvalidActor=false;
    if(FloatingAction.Intent==TEXT("Supply"))
    {auto* Supply=Cast<AHeliosBioSupply>(FloatingActionActor.Get());InvalidActor=!Supply||!Supply->IsAvailable();}
    else if(FloatingAction.Intent==TEXT("Chase")||FloatingAction.Intent==TEXT("Flank"))InvalidActor=FloatingActionActor.Get()!=CombatTarget||!CombatTarget;
    const bool EscapeCompromised=Danger&&Retreating&&FloatingAction.bValid&&
        FVector::Dist2D(FloatingAction.DestinationFeet,CombatTarget->GetActorLocation())<FVector::Dist2D(Feet,CombatTarget->GetActorLocation())+50.f;
    const bool Emergency=Danger&&(!Retreating||!FloatingAction.bValid||EscapeCompromised);
    const bool Arrived=FloatingAction.bValid&&FVector::Dist(Feet,FloatingAction.DestinationFeet)<85.f;
    bool Replan=Emergency||InvalidActor||(!FloatingAction.bValid&&Now>=FloatingPlanAt)||
        (Now>=FloatingActionUntil&&Now>=FloatingPlanAt&&(!Retreating||Arrived));
    if(Retreating&&!Danger&&Now>=FloatingActionUntil)Replan=true;
    if(Replan)
    {
        HeliosBioPlatformTraversal::FTraversalRequest Next;
        Next.CombatTarget=CombatTarget;Next.bKeepDistance=Human||bRoleRangedReady;Next.CombatDistance=FloatingAction.CombatDistance;Next.bUrgent=Danger;
        TWeakObjectPtr<AActor> ActionActor;FString Reason;
        auto Commit=[&](const FVector& Goal,FName Intent,const TCHAR* Why,AActor* Actor=nullptr)
        {
            if(Now<FailedFloatingUntil&&FVector::DistSquared(Goal,FailedFloatingGoal)<FMath::Square(120.f))return false;
            Next.DestinationFeet=Goal;Next.Intent=Intent;Next.bValid=true;ActionActor=Actor;Reason=Why;return true;
        };
        // Threat response always precedes shopping, including while waiting for a platform.
        if(Danger)
        {
            FVector Away=(Feet-CombatTarget->GetActorLocation()).GetSafeNormal2D();if(Away.IsNearlyZero())Away=-GetActorForwardVector();
            float Best=-MAX_flt;FVector Choice=Feet;
            for(int32 I=0;I<12;++I)
            {
                const float Angle=(I%6-2.5f)*32.f;const float Radius=I<6?600.f:300.f;
                FVector At;if(!FloatingPath(this,Feet+Away.RotateAngleAxis(Angle,FVector::UpVector)*Radius,At))continue;
                const float Gain=FVector::Dist2D(At,CombatTarget->GetActorLocation())-FVector::Dist2D(Feet,CombatTarget->GetActorLocation());
                if(Gain<60.f)continue;
                const float Score=Gain-Occupancy(this,Mode,At,240.f)*300.f+EscapeDirections(this,At)*35.f;
                if(Score>Best){Best=Score;Choice=At;}
            }
            if(Best>-MAX_flt)Commit(Choice,TEXT("Retreat"),TEXT("nearby_threat_safe_escape"));
            else if(bDefenseGoalValid&&FVector::Dist(Feet,DefenseGoal)>150.f&&FVector::Dist(DefenseGoal,CombatTarget->GetActorLocation())>Distance+100.f)
                Commit(DefenseGoal,TEXT("Retreat"),TEXT("escape_to_defense"));
        }
        if(!Next.bValid&&!Danger)
        {
            bool WantsWeapon=false;
            if(Human&&RoundInventory->IsEnabled())for(int32 Slot=1;Slot<5;++Slot)
                if(!RoundInventory->HasSlot(Slot))if(auto* D=UHeliosBioSupplyWeaponDefinition::LoadForSlot(Slot);D&&D->IsReady()){WantsWeapon=true;break;}
            const int32 Reserve=RoundInventory->IsEnabled()&&RoundInventory->HasSlot(0)?RoundInventory->GetReserve(0):ReserveAmmo;
            const bool Red=P->Team==EHeliosBioTeam::Zombie&&P->bMother&&P->ZombieForm==EHeliosBioZombieForm::Terminator&&P->EvolutionLevel>=Mode->Model.Config.MaximumZombieEvolution&&Mode->Model.Config.bAbyssSourceAvailable&&!Mode->Model.Config.AbyssRoleId.IsNone();
            const bool Blue=P->Team==EHeliosBioTeam::GhostHunter&&!P->bHunterChoicePending&&Distance>350.f;
            const bool Normal=Human?(Reserve<200||P->Health<P->MaxHealth*.6f||WantsWeapon)&&Distance>1100.f:
                P->Team==EHeliosBioTeam::Zombie&&(P->Health<P->MaxHealth*.6f||P->EvolutionLevel<Mode->Model.Config.MaximumZombieEvolution)&&Distance>450.f;
            if(Red||Blue||Normal)
            {
                const auto Kind=Red?EHeliosBioSupplyKind::RedTerminatorEvolution:Blue?EHeliosBioSupplyKind::BlueHunterUpgrade:EHeliosBioSupplyKind::Normal;
                float Best=3000.f;FVector Goal;AHeliosBioSupply* Supply=nullptr;
                for(auto S:Mode->Supplies)if(IsValid(S)&&S->IsAvailable()&&S->GetKind()==Kind)
                {
                    if(Human)if(auto* Content=S->GetWeaponContent();Content&&RoundInventory->IsEnabled()&&!RoundInventory->CanGrant(Content))continue;
                    FVector At;float Length;
                    if(FloatingPath(this,S->GetActorLocation(),At,&Length)&&Length<Best){Best=Length;Supply=S;Goal=At;}
                }
                if(Supply)Commit(Goal,TEXT("Supply"),TEXT("safe_reachable_supply"),Supply);
            }
        }
        if(!Next.bValid&&!Human&&CombatTarget&&(!bRoleRangedReady||!Clear||Distance>2800.f))
        {
            const auto* Character=Cast<ACharacter>(CombatTarget);const FVector TargetFeet=Character?Character->GetNavAgentLocation():CombatTarget->GetActorLocation();
            // Keep a stable approach sector, changing it only when another ally occupies it.
            FVector Approach=(Feet-TargetFeet).GetSafeNormal2D();if(Approach.IsNearlyZero())Approach=GetActorForwardVector();
            const float Sign=ParticipantId%2?1.f:-1.f;FVector BestPoint;float Best=MAX_flt;
            for(int32 I=0;I<4;++I)
            {
                FVector At;float Length;const float Radius=Distance<280.f?55.f:180.f;
                if(!FloatingPath(this,TargetFeet+Approach.RotateAngleAxis(Sign*(I*35.f),FVector::UpVector)*Radius,At,&Length,25.f))continue;
                if(!FiringLine(this,At,CombatTarget))continue;
                const float Cost=Length+Occupancy(this,Mode,At,170.f)*500.f+I*35.f;
                if(Cost<Best){Best=Cost;BestPoint=At;}
            }
            if(Best<MAX_flt)Commit(BestPoint,TEXT("Chase"),TEXT("complete_path_spread_approach"),CombatTarget);
            else Commit(TargetFeet,TEXT("Chase"),TEXT("platform_connection_required"),CombatTarget);
        }
        if(!Next.bValid&&Ranged&&!Danger)
        {
            // Keep a useful firing position; periodically reconsider crowding and retreat routes.
            if(Clear&&Distance<=2800.f&&EscapeDirections(this,Feet)>=2&&Occupancy(this,Mode,Feet,230.f)<1.f)
                Commit(Feet,TEXT("Guard"),TEXT("supported_firing_position"));
            else
            {
                bDefenseGoalValid=false;
                if(ChooseFloatingDefenseGoal())Commit(DefenseGoal,TEXT("Guard"),TEXT("spread_defense_with_exits"));
                if(CombatTarget&&!Clear&&Next.bValid&&!FiringLine(this,Next.DestinationFeet,CombatTarget))
                {
                    const FVector Toward=(CombatTarget->GetActorLocation()-Feet).GetSafeNormal2D();
                    for(float Angle:{45.f,-45.f,90.f,-90.f,0.f})
                    {FVector At;if(FloatingPath(this,Feet+Toward.RotateAngleAxis(Angle,FVector::UpVector)*450.f,At)&&FiringLine(this,At,CombatTarget)&&EscapeDirections(this,At)>=2){Commit(At,TEXT("Reposition"),TEXT("safe_firing_line"));break;}}
                }
            }
        }
        if(!Next.bValid){Next.Intent=Danger?FName(TEXT("Retreat")):FName(TEXT("Guard"));Reason=Danger?TEXT("no_safe_escape_hold_and_fire"):TEXT("no_reachable_action_retry");}
        FloatingAction=Next;FloatingActionActor=ActionActor;FloatingActionReason=Reason;
        FloatingActionUntil=Now+1.5f;FloatingPlanAt=Now+(Human&&!Danger?3.f+ParticipantId%5*.3f:1.5f);
        UE_LOG(LogTemp,Display,TEXT("BIO_FLOATING_ACTION id=%d %s"),ParticipantId,*GetFloatingActionDiagnostic());
    }
    // This entry also handles a passenger's local combat when no shore escape exists.
    bPlatformTraversalActive=HeliosBioPlatformTraversal::TickTraversal(this,AI,Mode,FloatingAction,0.f);
    if(bPlatformTraversalActive){MovementIntent=TEXT("PlatformTraversal");bMoveIntentActive=false;return;}
    if(!FloatingAction.bValid){HoldBioPosition(FloatingAction.Intent);return;}
    const float Acceptance=FloatingAction.Intent==TEXT("Chase")?(P->IsHunter()?65.f:35.f):60.f;
    FVector ValidatedGoal;
    if(!FloatingPath(this,FloatingAction.DestinationFeet,ValidatedGoal,nullptr,25.f)||!RequestBioMove(FloatingAction.DestinationFeet,Acceptance,FloatingAction.Intent,false))
    {
        MovementGoal=FloatingAction.DestinationFeet;MovementIntent=FloatingAction.Intent;
        FailedFloatingGoal=FloatingAction.DestinationFeet;FailedFloatingUntil=Now+4.f;FloatingAction.bValid=false;FloatingPlanAt=Now+1.f;
        FloatingActionReason=TEXT("failed_complete_route_avoided_for_4_seconds");
        RecoverBlockedMovement();
    }
}
