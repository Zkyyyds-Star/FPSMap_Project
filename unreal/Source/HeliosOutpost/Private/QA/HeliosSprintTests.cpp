#if WITH_DEV_AUTOMATION_TESTS
#include "Player/HeliosCharacter.h"
#include "Weapons/HeliosFighterWeapon.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosSprintToggleTest, "Helios.Control.SprintToggle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHeliosSprintToggleTest::RunTest(const FString&)
{
    FTestWorldWrapper Wrapper;
    if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
    UWorld* World = Wrapper.GetTestWorld();
    World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
    if (!Wrapper.BeginPlayInTestWorld()) return false;
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Floor = World->SpawnActor<AStaticMeshActor>(FVector(0,0,-50), FRotator::ZeroRotator, Spawn);
    auto* Mesh = Floor->GetStaticMeshComponent();
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Mesh->SetWorldScale3D(FVector(500,500,1));
    Mesh->SetCollisionProfileName(TEXT("BlockAll"));
    auto* Player = World->SpawnActor<AHeliosCharacter>(FVector(0,0,100), FRotator::ZeroRotator, Spawn);
    auto* Input = NewObject<UInputComponent>(Player);
    Player->SetupPlayerInputComponent(Input);
    auto Send = [&](EInputEvent Event)
    {
        for (int32 I = 0; I < Input->GetNumActionBindings(); ++I)
        {
            auto& Binding = Input->GetActionBinding(I);
            if (Binding.GetActionName() == TEXT("Sprint") && Binding.KeyEvent == Event)
                Binding.ActionDelegate.Execute(EKeys::LeftShift);
        }
    };
    auto CheckGround = [&]()
    {
        auto* Move = Player->GetCharacterMovement();
        TestEqual(TEXT("Starts walking"), Move->MaxWalkSpeed, Player->WalkSpeed);
        Send(IE_Pressed);
        TestEqual(TEXT("First press sprints"), Move->MaxWalkSpeed, Player->SprintSpeed);
        Send(IE_Repeat);
        Send(IE_Released);
        TestEqual(TEXT("Hold and release preserve sprint"), Move->MaxWalkSpeed, Player->SprintSpeed);
        Player->AddMovementInput(FVector::ForwardVector, 1, true);
        Player->ConsumeMovementInputVector();
        Move->StopMovementImmediately();
        Player->SetSprintingFromAutomation(false);
        TestEqual(TEXT("Stopping and automation cleanup preserve selection"), Move->MaxWalkSpeed, Player->SprintSpeed);
        Send(IE_Pressed);
        Send(IE_Released);
        TestEqual(TEXT("Second press walks"), Move->MaxWalkSpeed, Player->WalkSpeed);
        Send(IE_Pressed);
        Player->ClearMovementInput();
        TestEqual(TEXT("Clear resets sprint"), Move->MaxWalkSpeed, Player->WalkSpeed);
        Send(IE_Pressed);
        TestEqual(TEXT("Press after clear enables sprint"), Move->MaxWalkSpeed, Player->SprintSpeed);
        Player->ResetToSafeSpawn();
        TestEqual(TEXT("Reset returns to walking"), Move->MaxWalkSpeed, Player->WalkSpeed);
    };
    CheckGround();
    if (TestTrue(TEXT("Enter mech"), Player->EnterMechMode())) CheckGround();
    Player->ExitMechMode();
    auto* Flight = NewObject<UHeliosFighterFlightComponent>(Player);
    Player->FighterFlight = Flight;
    TestFalse(TEXT("Boost starts off"), Flight->bBoost);
    Send(IE_Pressed);
    TestTrue(TEXT("First press boosts"), Flight->bBoost);
    Send(IE_Repeat);
    Send(IE_Released);
    TestTrue(TEXT("Hold and release preserve boost"), Flight->bBoost);
    Send(IE_Pressed);
    TestFalse(TEXT("Second press stops boost"), Flight->bBoost);
    Send(IE_Released);
    Send(IE_Pressed);
    Player->ClearMovementInput();
    TestFalse(TEXT("Clear resets boost"), Flight->bBoost);
    Send(IE_Pressed);
    TestTrue(TEXT("Press after clear enables boost"), Flight->bBoost);
    Player->FighterFlight = nullptr;
    return true;
}
#endif
