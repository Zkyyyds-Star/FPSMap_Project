#if WITH_DEV_AUTOMATION_TESTS
#include "Enemies/HeliosEnemyActor.h"
#include "Boss/HeliosEnemyBoss.h"
#include "Variant_Shooter/AI/ShooterAIController.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "TimerManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosMinionCombatRulesTest,"Helios.Minion.CombatRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHeliosMinionCombatRulesTest::RunTest(const FString&)
{
    FTestWorldWrapper Fixture;
    if(!TestTrue(TEXT("Create world"),Fixture.CreateTestWorld(EWorldType::Game)))return false;
    UWorld* W=Fixture.GetTestWorld();
    W->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();
    if(!TestTrue(TEXT("Begin world"),Fixture.BeginPlayInTestWorld()))return false;
    auto AdvanceWorld=[W](float Seconds)
    {
        const int32 Frames=FMath::CeilToInt(Seconds*60.f);
        for(int32 Frame=0;Frame<Frames;++Frame){++GFrameCounter;W->Tick(LEVELTICK_All,Seconds/Frames);}
    };
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Enemy=W->SpawnActor<AHeliosEnemyE01>(FVector(0,0,200),FRotator::ZeroRotator,Params);
    auto* A=W->SpawnActor<ACharacter>(FVector(1000,0,200),FRotator::ZeroRotator,Params);
    auto* B=W->SpawnActor<ACharacter>(FVector(1200,300,200),FRotator::ZeroRotator,Params);
    if(!TestNotNull(TEXT("Enemy"),Enemy)||!TestNotNull(TEXT("Target A"),A)||!TestNotNull(TEXT("Target B"),B))return false;
    A->Tags.Add(TEXT("HeliosSummon")); B->Tags.Add(TEXT("HeliosPet"));
    for(ACharacter* C : {static_cast<ACharacter*>(Enemy),A,B})
    {
        C->GetCharacterMovement()->DisableMovement();
        C->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    }
    auto* AI=Cast<AShooterAIController>(Enemy->GetController());
    if(!TestNotNull(TEXT("Controller"),AI))return false;
    AI->GetBrainComponent()->StopLogic(TEXT("Rules fixture"));
    Enemy->StopShooting(); AI->ResetAwareness();
    int32 Changes=0;
    AI->OnShooterAwarenessChanged.BindLambda([&Changes](){++Changes;});
    AI->RefreshAwareness();
    TestTrue(TEXT("Proactive summon acquisition"),AI->GetCurrentTarget()==A);
    B->SetActorLocation(FVector(800,300,200));
    AI->RefreshAwareness();
    TestTrue(TEXT("Same tier keeps current target"),AI->GetCurrentTarget()==A);
    AI->ReportAwarenessThreat(B,B->GetActorLocation(),true);
    TestTrue(TEXT("Actual attacker outranks passive target"),AI->GetCurrentTarget()==B);
    const int32 Before=Changes;
    AI->ReportAwarenessThreat(B,B->GetActorLocation(),true);
    AI->ReportAwarenessThreat(B,B->GetActorLocation(),true);
    TestEqual(TEXT("Repeated damage does not restart StateTree"),Changes,Before);
    A->SetActorLocation(FVector(200,0,200));
    AI->ReportAwarenessThreat(A,A->GetActorLocation(),true);
    TestTrue(TEXT("Close attacker outranks distant attacker"),AI->GetCurrentTarget()==A);
    A->Tags.Add(TEXT("Dead")); AI->RefreshAwareness();
    TestTrue(TEXT("Dead summon released for living pet"),AI->GetCurrentTarget()==B);
    B->Tags.Add(TEXT("Dead")); AI->RefreshAwareness();
    TestNull(TEXT("Sleeping/dead pet released"),AI->GetCurrentTarget());
    A->Tags.Remove(TEXT("Dead")); A->SetActorLocation(FVector(1000,0,200));
    AI->ResetAwareness(); AI->RefreshAwareness();
    auto* Wall=W->SpawnActor<AActor>();
    auto* Box=NewObject<UBoxComponent>(Wall); Wall->AddInstanceComponent(Box); Wall->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(20,500,400)); Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent();
    Wall->SetActorLocation(FVector(500,0,200));
    AI->RefreshAwareness();
    TestTrue(TEXT("Cover loses target and starts investigation"),!AI->GetCurrentTarget()&&AI->IsInvestigating());
    const FVector Remembered=AI->GetInvestigationLocation();
    A->SetActorLocation(FVector(1100,100,200)); AI->RefreshAwareness();
    TestTrue(TEXT("Hidden movement does not update memory"),Remembered.Equals(AI->GetInvestigationLocation()));
    FVector Impact;
    TestFalse(TEXT("Wall blocks actual shot"),Enemy->HasClearAttackLine(A,Impact));
    Wall->Destroy(); A->SetActorLocation(FVector(1000,0,200));
    A->SetActorLocation(FVector(Enemy->AttackRange+500.f,0,200));
    TestFalse(TEXT("Existing range remains a hard limit"),Enemy->HasClearAttackLine(A,Impact));
    TestFalse(TEXT("No friendly enemy fire"),Enemy->HasClearAttackLine(Enemy,Impact));
    A->SetActorLocation(FVector(1000,0,200));
    AI->ResetAwareness(); AI->RefreshAwareness();
    auto* OwnerPawn=W->SpawnActor<ACharacter>(FVector(8000,0,200),FRotator::ZeroRotator,Params);
    auto* OwnerController=W->SpawnActor<AAIController>();
    if(!TestNotNull(TEXT("Player owner"),OwnerPawn)||!TestNotNull(TEXT("Player controller"),OwnerController))return false;
    OwnerPawn->Tags.Add(TEXT("Player")); OwnerPawn->GetCharacterMovement()->DisableMovement();
    OwnerController->Possess(OwnerPawn);
    TestTrue(TEXT("Summon damage can use its owner's controller"),OwnerController->GetPawn()==OwnerPawn);
    Enemy->StartShooting(A);
    TestTrue(TEXT("Attack can begin"),Enemy->bAttackCycleActive);
    const double ReadyAt=Enemy->NextAttackAllowedAt;
    const float Health=Enemy->GetHealth();
    FDamageEvent Damage;
    Enemy->TakeDamage(1.f,Damage,OwnerController,A); Enemy->TakeDamage(1.f,Damage,OwnerController,A);
    TestEqual(TEXT("Hits still deduct health"),Enemy->GetHealth(),Health-2.f);
    TestTrue(TEXT("Hits preserve active windup"),Enemy->bAttackCycleActive&&!Enemy->bHitReactionPlaying);
    TestEqual(TEXT("Damage does not change cadence"),Enemy->NextAttackAllowedAt,ReadyAt);
    Enemy->HandleAttackNotify();
    const int32 FirstCommit=Enemy->GetAttacksCommitted();
    TestEqual(TEXT("First notify commits one shot"),FirstCommit,1);
    Enemy->HandleAttackNotify();
    TestEqual(TEXT("Duplicate notify cannot duplicate shot"),Enemy->GetAttacksCommitted(),FirstCommit);
    Enemy->TakeDamage(1.f,Damage,OwnerController,A);
    TestTrue(TEXT("Hit preserves remaining burst"),Enemy->bBurstActive);
    Enemy->StopShooting(); Enemy->StartShooting(A);
    TestFalse(TEXT("StateTree reentry cannot bypass cooldown"),Enemy->bAttackCycleActive);
    TestEqual(TEXT("Reentry retains absolute deadline"),Enemy->NextAttackAllowedAt,ReadyAt);
    Enemy->TakeDamage(1.f,Damage,OwnerController,A);
    TestTrue(TEXT("Summon hit starts short idle reaction"),Enemy->bHitReactionPlaying);
    TestTrue(TEXT("Summon hit reaction lasts 0.2 seconds"),
        FMath::IsNearlyEqual(W->GetTimerManager().GetTimerRate(Enemy->HitReactionTimer),.2f));
    TestTrue(TEXT("Friendly hit cooldown is 0.8 seconds"),
        FMath::IsNearlyEqual(Enemy->NextFriendlyHitReactionAllowedAt,W->GetTimeSeconds()+.8,1.e-4));
    AdvanceWorld(.1f);
    const float HitRemaining=W->GetTimerManager().GetTimerRemaining(Enemy->HitReactionTimer);
    Enemy->TakeDamage(1.f,Damage,OwnerController,A);
    TestTrue(TEXT("Repeated summon hits do not extend reaction"),
        FMath::IsNearlyEqual(HitRemaining,W->GetTimerManager().GetTimerRemaining(Enemy->HitReactionTimer)));
    AdvanceWorld(.15f);
    TestFalse(TEXT("Summon reaction finishes"),Enemy->bHitReactionPlaying);
    B->Tags.Remove(TEXT("Dead"));
    const float GraceHealth=Enemy->GetHealth();
    Enemy->TakeDamage(1.f,Damage,OwnerController,B);
    TestEqual(TEXT("Pet hit during grace still deals damage"),Enemy->GetHealth(),GraceHealth-1.f);
    TestFalse(TEXT("Pet hit during grace does not stagger"),Enemy->bHitReactionPlaying);
    TestTrue(TEXT("Pet hit during grace retains facial feedback timer"),
        W->GetTimerManager().IsTimerActive(Enemy->HitReactionTimer));
    const float GraceRemaining=W->GetTimerManager().GetTimerRemaining(Enemy->HitReactionTimer);
    Enemy->TakeDamage(1.f,Damage,OwnerController,B);
    TestTrue(TEXT("Repeated pet hits do not extend facial feedback"),
        FMath::IsNearlyEqual(GraceRemaining,W->GetTimerManager().GetTimerRemaining(Enemy->HitReactionTimer)));
    Enemy->StopShooting();
    AdvanceWorld(1.1f);
    Enemy->TakeDamage(1.f,Damage,OwnerController,B);
    TestTrue(TEXT("Pet hit can stagger after cooldown"),Enemy->bHitReactionPlaying);
    TestTrue(TEXT("Pet reaction lasts 0.2 seconds"),
        FMath::IsNearlyEqual(W->GetTimerManager().GetTimerRate(Enemy->HitReactionTimer),.2f));
    AdvanceWorld(.21f);
    TestFalse(TEXT("Pet reaction finishes"),Enemy->bHitReactionPlaying);
    Enemy->StartShooting(A);
    TestTrue(TEXT("Enemy can counterattack after pet stagger"),Enemy->bAttackCycleActive);
    const double PetAttackReadyAt=Enemy->NextAttackAllowedAt;
    Enemy->TakeDamage(1.f,Damage,OwnerController,B);
    TestTrue(TEXT("Pet hits preserve active windup"),Enemy->bAttackCycleActive&&!Enemy->bHitReactionPlaying);
    TestEqual(TEXT("Pet hits preserve attack cadence"),Enemy->NextAttackAllowedAt,PetAttackReadyAt);
    Enemy->HandleAttackNotify();
    TestTrue(TEXT("Pet hits do not block attack notify"),Enemy->GetAttacksCommitted()>FirstCommit);
    Enemy->StopShooting();
    B->Tags.Add(TEXT("Dead"));
    A->Destroy(); AI->RefreshAwareness();
    TestNull(TEXT("Destroyed target cannot be reacquired"),AI->GetCurrentTarget());
    Enemy->TakeDamage(1.f,Damage,OwnerController,OwnerPawn);
    TestTrue(TEXT("Player hit retains normal reaction"),Enemy->bHitReactionPlaying);
    TestTrue(TEXT("Player reaction still lasts 0.6 seconds"),
        FMath::IsNearlyEqual(W->GetTimerManager().GetTimerRate(Enemy->HitReactionTimer),.6f));
    AdvanceWorld(.1f);
    const float PlayerRemaining=W->GetTimerManager().GetTimerRemaining(Enemy->HitReactionTimer);
    B->Tags.Remove(TEXT("Dead"));
    Enemy->TakeDamage(1.f,Damage,OwnerController,B);
    TestTrue(TEXT("Pet hit cannot shorten player reaction"),
        FMath::IsNearlyEqual(PlayerRemaining,W->GetTimerManager().GetTimerRemaining(Enemy->HitReactionTimer)));
    B->Tags.Add(TEXT("Dead"));
    Enemy->TakeDamage(Enemy->GetHealth(),Damage,nullptr,nullptr);
    const int32 AtDeath=Enemy->GetAttacksCommitted();
    Enemy->HandleAttackNotify(); Enemy->FireBurstShot();
    TestTrue(TEXT("Death clears cycles and bursts"),Enemy->IsEnemyDead()&&!Enemy->bAttackCycleActive&&!Enemy->bBurstActive);
    TestTrue(TEXT("Death clears hit reaction"),!Enemy->bHitReactionPlaying&&!W->GetTimerManager().IsTimerActive(Enemy->HitReactionTimer));
    TestEqual(TEXT("No damage after death"),Enemy->GetAttacksCommitted(),AtDeath);
    auto* Charger=W->SpawnActor<AHeliosEnemyE02>(FVector(3000,0,200),FRotator::ZeroRotator,Params);
    auto* CloseTarget=W->SpawnActor<ACharacter>(FVector(3020,0,216),FRotator::ZeroRotator,Params);
    CloseTarget->Tags.Add(TEXT("HeliosSummon"));
    CloseTarget->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    TestTrue(TEXT("Charger can attack a taller overlapping capsule"),Charger->CanStartTacticalAttack(CloseTarget));
    TestFalse(TEXT("Boss does not opt into minion targeting/movement"),GetDefault<AHeliosEnemyBoss>()->UsesTacticalCombat());
    TestTrue(TEXT("Boss keeps native combat"),GetDefault<AHeliosEnemyBoss>()->OwnsNativeCombatMovement());
    return true;
}
#endif
