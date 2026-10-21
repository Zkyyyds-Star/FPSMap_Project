#if WITH_DEV_AUTOMATION_TESTS
#include "Weapons/HeliosImpactPresentationSubsystem.h"
#include "Components/BoxComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosImpactSurfaceTest,"Helios.Combat.ImpactSurfaceRouting",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FHeliosImpactSurfaceTest::RunTest(const FString& Parameters)
{
    FHitResult Hit;
    UBoxComponent* Component=NewObject<UBoxComponent>();
    Hit.Component=Component;
    TestEqual(TEXT("Unknown surface uses neutral debris"),UHeliosImpactPresentationSubsystem::ResolveSurface(Hit),EHeliosImpactSurface::Default);
    Component->ComponentTags.Add(TEXT("HeliosMetal"));
    TestEqual(TEXT("Tagged metal is recognized without a physical material"),UHeliosImpactPresentationSubsystem::ResolveSurface(Hit),EHeliosImpactSurface::Metal);
    UPhysicalMaterial* Physical=NewObject<UPhysicalMaterial>();
    Physical->SurfaceType=SurfaceType1;Hit.PhysMaterial=Physical;
    TestEqual(TEXT("Actual physical material overrides a mesh name or tag"),UHeliosImpactPresentationSubsystem::ResolveSurface(Hit),EHeliosImpactSurface::Concrete);
    Physical->SurfaceType=SurfaceType4;
    TestEqual(TEXT("Soil never inherits metal sparks"),UHeliosImpactPresentationSubsystem::ResolveSurface(Hit),EHeliosImpactSurface::Soil);
    Physical->SurfaceType=SurfaceType7;
    TestEqual(TEXT("Skin routes to the no-particle no-blood category"),UHeliosImpactPresentationSubsystem::ResolveSurface(Hit),EHeliosImpactSurface::Skin);
    return true;
}
#endif
