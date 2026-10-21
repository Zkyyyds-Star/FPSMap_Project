#if WITH_DEV_AUTOMATION_TESTS

#include "Control/HeliosControlSubsystem.h"
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FHeliosControlValidationTest,
    "Helios.Control.Validation",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FHeliosControlValidationTest::RunTest(const FString& Parameters)
{
    FString ErrorCode;
    TestFalse(TEXT("Zero duration is rejected"), UHeliosControlSubsystem::ValidateDuration(0.0f, ErrorCode));
    TestEqual(TEXT("Duration error is stable"), ErrorCode, FString(TEXT("invalid_duration")));
    TestFalse(TEXT("Duration above command bound is rejected"), UHeliosControlSubsystem::ValidateDuration(3.001f, ErrorCode));
    TestFalse(TEXT("Non-finite duration is rejected"), UHeliosControlSubsystem::ValidateDuration(std::numeric_limits<float>::infinity(), ErrorCode));
    TestTrue(TEXT("Maximum duration is accepted"), UHeliosControlSubsystem::ValidateDuration(3.0f, ErrorCode));

    const FVector2D Clamped = UHeliosControlSubsystem::ClampMoveAxes(FVector2D(4.0, -3.0));
    TestTrue(TEXT("Move input is bounded to unit length"), Clamped.Size() <= 1.00001);
    TestTrue(TEXT("Move input keeps direction"), Clamped.X > 0.0 && Clamped.Y < 0.0);
    return true;
}

#endif
