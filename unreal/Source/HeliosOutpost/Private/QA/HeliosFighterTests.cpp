#if WITH_DEV_AUTOMATION_TESTS
#include "Weapons/HeliosFighterWeapon.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Player/HeliosArenaCharacter.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameModeBase.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosFighterTest,"Helios.Fighter.FlightCombatLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHeliosFighterTest::RunTest(const FString&)
{
    FTestWorldWrapper Wrapper;if(!Wrapper.CreateTestWorld(EWorldType::Game))return false;
    UWorld* World=Wrapper.GetTestWorld();World->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();if(!Wrapper.BeginPlayInTestWorld())return false;
    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto Box=[&](FVector At,FVector Scale){auto* A=World->SpawnActor<AStaticMeshActor>(At,FRotator::ZeroRotator,Spawn);auto* C=A->GetStaticMeshComponent();C->SetMobility(EComponentMobility::Movable);C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));C->SetWorldScale3D(Scale);C->SetCollisionProfileName(TEXT("BlockAll"));return A;};
    Box(FVector(0,0,-50),FVector(500,500,1));
    auto* P=World->SpawnActor<AHeliosArenaCharacter>(FVector(0,0,100),FRotator::ZeroRotator,Spawn);
    auto* PC=World->SpawnActor<APlayerController>();PC->Possess(P);
    auto* D=UHeliosWeaponRegistry::FindWeapon(TEXT("fighter"));if(!TestNotNull(TEXT("Weapon registered"),D)||!TestTrue(TEXT("Equip fighter"),P->EquipWeaponDefinition(D)))return false;
    auto* W=Cast<AHeliosFighterWeapon>(P->GetEquippedWeapon());if(!TestNotNull(TEXT("Dedicated weapon"),W))return false;
    TestTrue(TEXT("Safe deployment activates flying"),W->bActive&&P->IsFighterMode());
    TestFalse(TEXT("No ammunition"),W->UsesAmmunition());
    auto* F=W->Flight.Get();auto* M=P->GetCharacterMovement();
    TArray<float> Distances;TArray<int32> Counts;
    for(float FPS:{30.f,60.f,120.f})
    {
        P->SetActorLocation(FVector(0,0,1500));PC->SetControlRotation(FRotator::ZeroRotator);M->StopMovementImmediately();F->ForwardRightUp=FVector(1,0,0);
        const FVector Start=P->GetActorLocation();for(int I=0;I<int(3*FPS);++I)F->Simulate(1/FPS,M);
        Distances.Add(float(P->GetActorLocation().X-Start.X));TestTrue(TEXT("Steady forward speed"),FMath::IsNearlyEqual(float(M->Velocity.Size()),1800.f,2.f));
        F->ClearInput();for(int I=0;I<int(2*FPS);++I)F->Simulate(1/FPS,M);TestTrue(TEXT("Hands off holds hover"),M->Velocity.Size()<.1f);
        W->StopFiring();W->Heat=W->CoolRemaining=0;W->Tick(.7f);int Before=W->LaserCount;W->StartFiring();for(int I=0;I<int(FPS);++I)W->Tick(1/FPS);W->StopFiring();Counts.Add(W->LaserCount-Before);
    }
    TestTrue(TEXT("Flight distance independent of frame rate"),FMath::Abs(Distances[0]-Distances[2])<2);
    TestTrue(TEXT("Laser cadence independent of frame rate"),FMath::Abs(Counts[0]-Counts[2])<=1&&Counts[0]>=10&&Counts[0]<=11);
    PC->SetControlRotation(FRotator(70,0,0));F->ForwardRightUp=FVector(1,1,1);TestTrue(TEXT("Diagonal pitch cannot multiply speed"),F->DesiredVelocity().Size()<=1800.1f);F->ClearInput();
    PC->SetControlRotation(FRotator::ZeroRotator);F->ForwardRightUp.X=1;F->bBoost=true;TestEqual(TEXT("Boost is 32 metres per second"),float(F->DesiredVelocity().Size()),3200.f);F->ClearInput();
    F->SetVerticalKey(true,true);F->SetVerticalKey(false,true);TestEqual(TEXT("Opposite vertical keys cancel"),F->ForwardRightUp.Z,0.0);
    F->SetVerticalKey(true,false);TestEqual(TEXT("Held descent survives ascent release"),F->ForwardRightUp.Z,-1.0);TestEqual(TEXT("Descent is 10 metres per second"),float(F->DesiredVelocity().Size()),1000.f);F->ClearInput();
    PC->SetControlRotation(FRotator::ZeroRotator);P->SetActorLocation(FVector(0,0,1500));M->StopMovementImmediately();
    auto* Wall=Box(FVector(1000,0,1500),FVector(.2f,20,20));F->ForwardRightUp.X=1;
    for(int I=0;I<240;++I)F->Simulate(1.f/60,M);
    TestTrue(TEXT("Nose stops before solid wall"),P->GetActorLocation().X<715);Wall->Destroy();F->ClearInput();
    P->SetActorLocation(FVector(0,0,1500));M->StopMovementImmediately();Wall=Box(FVector(0,1000,1500),FVector(30,.2f,20));F->ForwardRightUp.Y=1;
    for(int I=0;I<180;++I)F->Simulate(1.f/60,M);
    TestTrue(TEXT("Wing stops before side wall"),P->GetActorLocation().Y<780);Wall->Destroy();F->ClearInput();
    P->SetActorLocation(FVector(0,0,1500));M->StopMovementImmediately();Wall=Box(FVector(0,0,2500),FVector(30,30,.2f));F->ForwardRightUp.Z=1;
    for(int I=0;I<180;++I)F->Simulate(1.f/60,M);
    TestTrue(TEXT("Hull stops below roof"),P->GetActorLocation().Z<2380);Wall->Destroy();F->ClearInput();
    P->SetActorLocation(FVector(0,0,F->FlightBounds.Max.Z-50));M->StopMovementImmediately();F->ForwardRightUp.Z=1;
    for(int I=0;I<120;++I)F->Simulate(1.f/60,M);
    TestTrue(TEXT("Ceiling respected"),P->GetActorLocation().Z<=F->FlightBounds.Max.Z+.1f);F->ClearInput();
    P->SetActorLocation(FVector(F->FlightBounds.Max.X-50,0,1500));M->StopMovementImmediately();F->ForwardRightUp.X=1;
    for(int I=0;I<120;++I)F->Simulate(1.f/60,M);
    TestTrue(TEXT("Horizontal boundary respected"),P->GetActorLocation().X<=F->FlightBounds.Max.X+.1f&&F->bBoundary);F->ClearInput();
    P->SetActorLocation(FVector(0,0,1500));M->StopMovementImmediately();PC->SetControlRotation(FRotator::ZeroRotator);P->FirstPersonCamera->SetWorldRotation(FRotator::ZeroRotator);
    auto Enemy=[&](FVector At){auto* E=World->SpawnActor<AHeliosEnemyE01>(At,FRotator::ZeroRotator,Spawn);E->CurrentHP=10000;E->StopShooting();E->GetCharacterMovement()->DisableMovement();E->SetActorTickEnabled(false);if(E->GetController())E->GetController()->Destroy();return E;};
    const FVector Eye=P->FirstPersonCamera->GetComponentLocation();auto* E=Enemy(Eye+FVector(2000,0,0));
    W->Heat=W->CoolRemaining=0;W->Tick(1);W->UpdateLock(1);TestTrue(TEXT("Visible target locks"),W->LockCandidate.Get()==E&&W->LockProgress>=W->Settings->LockSeconds);
    float Health=E->GetHealth();W->FirePulse();TestEqual(TEXT("Laser damages reticle target"),Health-E->GetHealth(),W->Settings->Damage);
    Wall=Box(Eye+FVector(1000,0,0),FVector(.2f,10,10));Health=E->GetHealth();W->FirePulse();W->UpdateLock(1);
    TestEqual(TEXT("Solid cover blocks laser"),E->GetHealth(),Health);TestFalse(TEXT("Solid cover breaks acquisition"),W->LockCandidate.IsValid());Wall->Destroy();
    W->MissileRemaining=0;W->StopSecondaryFire();const int Count=W->MissileCount;W->StartSecondaryFire();W->StartSecondaryFire();W->StopSecondaryFire();W->StartSecondaryFire();W->StopSecondaryFire();TestEqual(TEXT("One missile per accepted click and cooldown"),W->MissileCount,Count+1);
    W->StopFiring();W->Heat=W->CoolRemaining=0;W->StartFiring();for(int I=0;I<310;++I)W->Tick(1.f/60);
    TestTrue(TEXT("Laser overheats"),W->CoolRemaining>0);const int OverheatCount=W->LaserCount;W->Tick(.2f);TestEqual(TEXT("No pulse during overheat"),W->LaserCount,OverheatCount);W->StopFiring();for(int I=0;I<180;++I)W->Tick(1.f/60);TestTrue(TEXT("Thermal lock recovers"),W->CoolRemaining==0&&W->Heat<.01f);
    const FVector Blast=FVector(10000,10000,1500);auto* Near=Enemy(Blast+FVector(120,0,0));auto* Far=Enemy(Blast+FVector(0,420,0));auto* Covered=Enemy(Blast+FVector(-300,0,0));
    Wall=Box(Blast+FVector(-150,0,0),FVector(.2f,3,6));FActorSpawnParameters S;S.Owner=W;S.Instigator=P;
    auto* Missile=World->SpawnActor<AHeliosFighterMissile>(Blast,FRotator::ZeroRotator,S);Missile->Launch(W->Settings,nullptr,FVector::ForwardVector);Missile->Explode(Blast,FVector::UpVector);
    TestTrue(TEXT("Blast falls off with distance"),Near->CurrentHP<Far->CurrentHP&&Far->CurrentHP<10000);TestEqual(TEXT("Cover blocks blast"),Covered->CurrentHP,10000.f);Wall->Destroy();
    const FVector Empty=FVector(-15000,-15000,6000);
    Missile=World->SpawnActor<AHeliosFighterMissile>(Empty,FRotator::ZeroRotator,S);Missile->Launch(W->Settings,nullptr,FVector::ForwardVector);
    Missile->Tick(.1f);TestFalse(TEXT("Missile separates before ignition"),Missile->IsIgnited());Missile->Tick(.1f);TestTrue(TEXT("Missile ignites after separation"),Missile->IsIgnited());
    const FVector Straight=Missile->GetActorLocation();for(int I=0;I<30;++I)Missile->Tick(1.f/60);
    TestTrue(TEXT("Unlocked missile flies 45 metres in half a second"),FMath::IsNearlyEqual(float(Missile->GetActorLocation().X-Straight.X),4500.f,2.f)&&FMath::IsNearlyEqual(float(Missile->GetActorLocation().Y),float(Straight.Y),.1f));Missile->Destroy();
    TestEqual(TEXT("Double speed config"),W->Settings->MissileSpeed,9000.f);
    for(int FPS:{30,60,120})
    {
        Missile=World->SpawnActor<AHeliosFighterMissile>(Empty,FRotator::ZeroRotator,S);Missile->Launch(W->Settings,nullptr,FVector::ForwardVector);Missile->Tick(.2f);
        const FVector From=Missile->GetActorLocation();for(int I=0;I<FPS/2;++I)Missile->Tick(1.f/FPS);
        TestTrue(FString::Printf(TEXT("90 m/s at %d FPS"),FPS),FMath::IsNearlyEqual(float(Missile->GetActorLocation().X-From.X),4500.f,2.f));Missile->Destroy();
        Wall=Box(Empty+FVector(1100,0,0),FVector(.02f,4,4));
        Missile=World->SpawnActor<AHeliosFighterMissile>(Empty,FRotator::ZeroRotator,S);Missile->Launch(W->Settings,nullptr,FVector::ForwardVector);
        for(int I=0;I<FPS&&!Missile->IsActorBeingDestroyed();++I)Missile->Tick(1.f/FPS);
        TestTrue(FString::Printf(TEXT("Fast missile hits 2 cm wall at %d FPS"),FPS),Missile->IsActorBeingDestroyed());Wall->Destroy();
    }
    auto* MovingTarget=Enemy(Empty+FVector(10000,8000,0));Missile=World->SpawnActor<AHeliosFighterMissile>(Empty,FRotator::ZeroRotator,S);Missile->Launch(W->Settings,MovingTarget,FVector::ForwardVector);Missile->Tick(.2f);
    const FVector BeforeTurn=Missile->GetActorForwardVector();Missile->Tick(.1f);const float Degrees=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(float(FVector::DotProduct(BeforeTurn,Missile->GetActorForwardVector())),-1.f,1.f)));
    TestTrue(TEXT("Tracking respects turn rate"),Degrees>0&&Degrees<=W->Settings->TurnDegrees*.1f+.1f);
    UGameplayStatics::ApplyDamage(MovingTarget,20000,PC,W,nullptr);TestTrue(TEXT("Tracking test target died"),MovingTarget->IsEnemyDead());
    const FVector LastHeading=Missile->GetActorForwardVector();for(int I=0;I<30;++I)Missile->Tick(1.f/60);
    TestTrue(TEXT("Dead target keeps last heading without retarget"),Missile->GetActorForwardVector().Equals(LastHeading,.001));
    for(int I=0;I<400&&!Missile->IsActorBeingDestroyed();++I)Missile->Tick(1.f/60);
    TestTrue(TEXT("Missile lifetime capped"),Missile->IsActorBeingDestroyed());
    W->CancelInput();W->SuspendForArrival();TestFalse(TEXT("Teardown clears flying"),P->IsFighterMode());
    TestEqual(TEXT("Teardown releases fighter VFX pool leases"),W->Art->ActiveEffectCount(),0);
    int Active=0;for(TActorIterator<AHeliosFighterMissile> It(World);It;++It)if(!It->IsActorBeingDestroyed())++Active;TestEqual(TEXT("Teardown clears missiles"),Active,0);
    P->ResetForTest();TestTrue(TEXT("Reset redeploys"),P->IsFighterMode());
    P->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("cfm_chan_zeus")));TestFalse(TEXT("Other weapon restores locomotion"),P->IsFighterMode());
    return true;
}
#endif
