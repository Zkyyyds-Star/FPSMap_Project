#if WITH_DEV_AUTOMATION_TESTS
#include "Combat/HeliosKillFeedbackSubsystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosKillChainTest,"Helios.KillFeedback.ChainRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHeliosKillChainTest::RunTest(const FString&)
{
    FHeliosKillChain C;
    C.Record(1,false);
    TestFalse(TEXT("Coalescing waits 80ms"),C.Flush(1.079));
    TestTrue(TEXT("Exactly 80ms flushes"),C.Flush(1.08));
    TestTrue(TEXT("First personal kill is first blood"),C.Display==EHeliosKillBadge::First);
    C.Record(7,false); C.Flush(7.1);
    TestEqual(TEXT("Exactly six seconds continues"),C.Streak,2);
    TestTrue(TEXT("Double medal"),C.Display==EHeliosKillBadge::Double);
    C.Record(13.001,false); C.Flush(13.1);
    TestEqual(TEXT("Over six seconds resets"),C.Streak,1);
    TestTrue(TEXT("Timeout does not award first blood again"),C.Display==EHeliosKillBadge::Single);
    for(int32 I=0;I<8;++I) { C.Record(14+I,false); C.Flush(14.1+I); }
    TestEqual(TEXT("Count continues past six"),C.DisplayStreak,9);
    TestTrue(TEXT("Highest art remains six-plus"),C.Display==EHeliosKillBadge::Hexa);
    C.Reset(); C.Record(1,false); C.Record(1.01,false); C.Record(1.02,true); C.Flush(1.09);
    TestTrue(TEXT("Boss outranks first and multi in same batch"),C.Display==EHeliosKillBadge::Boss);
    TestEqual(TEXT("Same-frame group counts every victim"),C.DisplayStreak,3);
    TestEqual(TEXT("One batch one presentation"),C.Serial,1);
    TestTrue(TEXT("Boss lasts two seconds"),C.IsVisible(3.08));
    TestFalse(TEXT("Boss expires"),C.IsVisible(3.10));
    C.Record(1.15,false); C.Flush(1.24);
    TestTrue(TEXT("Batch priority clears after display"),C.Display==EHeliosKillBadge::Quad);
    C.Reset(); TestFalse(TEXT("Reset clears pending and visible state"),C.bPending||C.IsVisible(1.3));
    C.Record(2,false); C.Flush(2.1); C.Record(4,false); C.Flush(4.1);
    // Pause supplies no elapsed game time; wall-clock time is deliberately absent.
    C.Record(4,false); C.Flush(4.2); TestEqual(TEXT("Frozen game time preserves chain"),C.Streak,3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosKillAttributionTest,"Helios.KillFeedback.Attribution",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHeliosKillAttributionTest::RunTest(const FString&)
{
    FTestWorldWrapper Fixture;
    if(!TestTrue(TEXT("Fixture"),Fixture.CreateTestWorld(EWorldType::Game))) return false;
    UWorld* W=Fixture.GetTestWorld();
    auto* Player=W->SpawnActor<APawn>();
    auto* PC=W->SpawnActor<APlayerController>(); PC->Possess(Player);
    auto* Weapon=W->SpawnActor<AActor>(); Weapon->SetOwner(Player);
    auto* Projectile=W->SpawnActor<AActor>(); Projectile->SetOwner(Weapon); Projectile->SetInstigator(Player);
    auto* Pet=W->SpawnActor<APawn>(); Pet->SetOwner(Player); Pet->SetInstigator(Player); Pet->Tags.Add(TEXT("HeliosPet"));
    auto* PetShot=W->SpawnActor<AActor>(); PetShot->SetOwner(Pet); PetShot->SetInstigator(Player);
    auto* Summon=W->SpawnActor<AActor>(); Summon->SetOwner(Player); Summon->Tags.Add(TEXT("HeliosSummon"));
    auto* Environment=W->SpawnActor<AActor>();
    auto Direct=[&](AActor* A){return UHeliosKillFeedbackSubsystem::IsDirectPlayerDamage(Player,PC,A);};
    TestTrue(TEXT("Player weapon"),Direct(Weapon));
    TestTrue(TEXT("Player projectile ownership chain"),Direct(Projectile));
    TestFalse(TEXT("Pet with player controller excluded"),Direct(Pet));
    TestFalse(TEXT("Pet shot with player instigator excluded"),Direct(PetShot));
    TestFalse(TEXT("Summon with player owner excluded"),Direct(Summon));
    TestFalse(TEXT("Controller alone cannot claim environment"),Direct(Environment));
    TestFalse(TEXT("Missing cause excluded"),Direct(nullptr));
    TestTrue(TEXT("Delayed player projectile can have no controller"),UHeliosKillFeedbackSubsystem::IsDirectPlayerDamage(Player,nullptr,Projectile));
    auto* OtherPC=W->SpawnActor<APlayerController>();
    TestFalse(TEXT("Foreign controller excluded"),UHeliosKillFeedbackSubsystem::IsDirectPlayerDamage(Player,OtherPC,Projectile));
    return true;
}
#endif
