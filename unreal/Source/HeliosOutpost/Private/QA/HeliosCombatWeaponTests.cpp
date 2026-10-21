#if WITH_DEV_AUTOMATION_TESTS

#include "Weapons/HeliosCombatWeapons.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FHeliosCombatDirectionRegressionTest,
    "Helios.Combat.DirectionAndOcclusion",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FHeliosCombatDirectionRegressionTest::RunTest(const FString& Parameters)
{
    const FRotator LaunchRotations[] = {
        FRotator(0.0f, 0.0f, 0.0f),
        FRotator(0.0f, 90.0f, 0.0f),
        FRotator(0.0f, 180.0f, 0.0f),
        FRotator(0.0f, 270.0f, 0.0f),
        FRotator(35.0f, 42.0f, 0.0f),
        FRotator(-28.0f, 215.0f, 0.0f)
    };

    UProjectileMovementComponent* Movement = NewObject<UProjectileMovementComponent>();
    for (const FRotator& Rotation : LaunchRotations)
    {
        const FVector Direction = Rotation.Vector();
        HeliosCombatWeaponMath::InitializeRocketWorldVelocity(*Movement, Direction, 2500.0f);
        TestFalse(TEXT("Rocket initial velocity is explicitly world-space"),
            Movement->bInitialVelocityInLocalSpace);
        TestTrue(FString::Printf(TEXT("Rocket follows yaw/pitch %s"), *Rotation.ToCompactString()),
            Movement->Velocity.Equals(Direction * 2500.0f, 0.01f));
        TestTrue(TEXT("Rocket retains configured speed"),
            FMath::IsNearlyEqual(Movement->Velocity.Size(), 2500.0f, 0.01f));
    }

    FHitResult NearWallHit;
    NearWallHit.bBlockingHit = true;
    NearWallHit.Location = FVector(24.0f, 3.0f, 70.0f);
    const FVector DesiredSpawn(110.0f, 18.0f, 65.0f);
    TestTrue(TEXT("Near-wall rocket spawn stops at the camera-to-muzzle sweep"),
        HeliosCombatWeaponMath::ResolveRocketSpawnLocation(DesiredSpawn, NearWallHit)
            .Equals(NearWallHit.Location));

    const FVector ViewStart(15.0f, -20.0f, 80.0f);
    const FVector ViewDirection = FRotator(18.0f, 90.0f, 0.0f).Vector();
    const FVector PelletEnd = HeliosCombatWeaponMath::CalculatePelletEndpoint(
        ViewStart, ViewDirection, 3000.0f);
    TestTrue(TEXT("Pellet endpoint is camera-origin forward at full range"),
        PelletEnd.Equals(ViewStart + ViewDirection * 3000.0f, 0.01f));

    const FVector Muzzle(35.0f, 15.0f, 65.0f);
    const FVector Surface(100.0f, 120.0f, 90.0f);
    const FVector Extended = HeliosCombatWeaponMath::ExtendTracePastSurface(Muzzle, Surface);
    TestTrue(TEXT("Muzzle trace endpoint crosses the camera-hit surface"),
        FVector::DotProduct(Extended - Surface, Surface - Muzzle) > 0.0f);
    return true;
}

#endif
