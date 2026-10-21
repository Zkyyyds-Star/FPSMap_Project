#if WITH_DEV_AUTOMATION_TESTS
#include "Bio/HeliosBioPlatformSchedule.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosBioPlatformScheduleTest,
    "Helios.BioHotel.Rules.FloatingPlatformSchedule",EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::EngineFilter)
bool FHeliosBioPlatformScheduleTest::RunTest(const FString& Parameters)
{
    using namespace HeliosBioPlatformSchedule;
    TestTrue(TEXT("At least twenty percent and over two seconds saved"),ShouldUsePlatform(20.f,16.f));
    TestFalse(TEXT("Fast ground route stays preferable"),ShouldUsePlatform(10.f,8.f));
    TestFalse(TEXT("Small percentage saving is rejected"),ShouldUsePlatform(30.f,26.f));
    TestTrue(TEXT("Platform-only connection remains eligible"),ShouldUsePlatform(-1.f,50.f));
    TestFalse(TEXT("No predicted platform route is rejected"),ShouldUsePlatform(-1.f,-1.f));
    FSnapshot S;S.Points={FVector(0,0,0),FVector(1000,0,0),FVector(2200,0,0)};
    S.Speed=100.f;S.Pause=5.f;S.bRunning=true;
    float Board,Exit;
    TestEqual(TEXT("Empty route has no cycle"),CycleSeconds(FSnapshot{}),0.f);
    S.Replay=EHeliosBioPlatformReplay::PingPong;
    TestEqual(TEXT("Full cycle is 54 seconds"),CycleSeconds(S),54.f);
    TestTrue(TEXT("Outbound trip can board at origin now"),Predict(S,0,2,0.f,Board,Exit));
    TestEqual(TEXT("Origin boarding now"),Board,0.f);TestEqual(TEXT("Outbound arrival"),Exit,22.f);
    TestTrue(TEXT("Missing origin requires return and pause"),Predict(S,0,2,1.f,Board,Exit));
    TestEqual(TEXT("Origin returns at 49 seconds"),Board,49.f);TestEqual(TEXT("Next outbound exit includes origin pause"),Exit,76.f);
    S.Position=S.Points[2];S.TargetIndex=1;S.Direction=-1;S.Wait=4.f;
    TestTrue(TEXT("Can board during remaining endpoint pause"),Predict(S,2,0,3.f,Board,Exit));
    TestEqual(TEXT("Board after approach while paused"),Board,3.f);TestEqual(TEXT("Return arrival includes remaining pause"),Exit,26.f);
    TestTrue(TEXT("Middle dock uses current return direction"),Predict(S,1,0,0.f,Board,Exit));
    TestEqual(TEXT("Middle return arrival"),Board,16.f);TestEqual(TEXT("Ground exit arrival"),Exit,26.f);
    const auto Before=S;
    Predict(S,1,2,0.f,Board,Exit);
    TestTrue(TEXT("Prediction does not advance platform"),S.Position==Before.Position&&S.Wait==Before.Wait&&S.TargetIndex==Before.TargetIndex&&S.Direction==Before.Direction);
    S.bRunning=false;TestFalse(TEXT("Stopped routes cannot be boarded"),Predict(S,0,1,0.f,Board,Exit));
    S.bRunning=true;S.Position=FVector::ZeroVector;S.TargetIndex=1;S.Direction=1;S.Wait=0.f;S.Replay=EHeliosBioPlatformReplay::Once;
    TestFalse(TEXT("Once route cannot return to missed origin"),Predict(S,0,1,1.f,Board,Exit));
    S.Replay=EHeliosBioPlatformReplay::Wrap;
    TestEqual(TEXT("Wrap physically traverses closing leg"),CycleSeconds(S),49.f);
    TestTrue(TEXT("Wrap origin after closing segment"),Predict(S,0,1,1.f,Board,Exit));
    TestEqual(TEXT("Wrap origin return"),Board,49.f);TestEqual(TEXT("Wrap next stop"),Exit,59.f);
    TestFalse(TEXT("Invalid dock is rejected"),Predict(S,8,0,0.f,Board,Exit));
    S.Speed=0.f;TestFalse(TEXT("Zero speed is rejected"),Predict(S,0,1,0.f,Board,Exit));
    return true;
}
#endif
