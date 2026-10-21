#if WITH_DEV_AUTOMATION_TESTS
#include "Weapons/HeliosMysteriousHand.h"
#include "Weapons/HeliosHandAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Weapons/HeliosHandImpact.h"
#include "Weapons/HeliosWeaponVFX.h"
#include "EngineUtils.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Player/HeliosArenaCharacter.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameModeBase.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Kismet/GameplayStatics.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosMysteriousHandTest,"Helios.MysteriousHand.CombatAndLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHeliosMysteriousHandTest::RunTest(const FString&)
{
    FTestWorldWrapper F;if(!F.CreateTestWorld(EWorldType::Game))return false;UWorld* W=F.GetTestWorld();W->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();if(!F.BeginPlayInTestWorld())return false;
    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* P=W->SpawnActor<AHeliosArenaCharacter>(FVector(0,0,100),FRotator::ZeroRotator,Spawn);P->GetCharacterMovement()->DisableMovement();
    auto* D=UHeliosWeaponRegistry::FindWeapon(TEXT("mysterious_hand"));if(!TestNotNull(TEXT("Registered"),D)||!TestTrue(TEXT("Equip"),P->EquipWeaponDefinition(D)))return false;
    auto* H=Cast<AHeliosMysteriousHand>(P->GetEquippedWeapon());if(!TestNotNull(TEXT("Dedicated hand"),H))return false;
    TestFalse(TEXT("No ammo"),H->UsesAmmunition());TestTrue(TEXT("Skeletal model recognized"),D->HasModel());
    TestEqual(TEXT("10 metre radius"),H->Settings->MissileRadius,1000.f);
    TestEqual(TEXT("60 metre summon height"),H->Settings->SummonHeight,6000.f);
    TestEqual(TEXT("1.2 second descent"),H->Settings->MissileFlightSeconds,1.2f);
    auto Enemy=[&](FVector At){auto* E=W->SpawnActor<AHeliosEnemyE01>(At,FRotator::ZeroRotator,Spawn);E->GetCharacterMovement()->DisableMovement();E->StopShooting();E->SetActorTickEnabled(false);if(E->GetController())E->GetController()->Destroy();return E;};
    auto Tick=[&](float Seconds,float FPS){for(int I=0;I<FMath::CeilToInt(Seconds*FPS);++I){++GFrameCounter;W->Tick(LEVELTICK_All,1.f/FPS);}};
    FVector S,V;H->View(S,V);auto* E=Enemy(S+V*1000-FVector(0,0,25));auto* Other=Enemy(S+V*1200+FVector(0,90,-25));
    const float Before=E->GetHealth(),BeforeOther=Other->GetHealth();H->StartFiring();H->StopFiring();Tick(.7f,30);
    TestEqual(TEXT("One accepted click one pulse at 30 fps"),H->LaserCount,1);TestTrue(TEXT("Laser hits closest to reticle"),FMath::IsNearlyEqual(Before-E->GetHealth(),60,.01f));TestEqual(TEXT("No chain hit"),Other->GetHealth(),BeforeOther);
    H->StartFiring();H->StopFiring();H->CancelReload();Tick(.7f,120);TestEqual(TEXT("Cancellation before commit"),H->LaserCount,1);
    E->Destroy();Other->Destroy();
    for(float FPS:{30.f,60.f,120.f}){const int Count=H->LaserCount;H->StartFiring();Tick(1.4f,FPS);TestEqual(TEXT("Holding does not auto-repeat"),H->LaserCount,Count+1);H->StopFiring();}
    // Cover and end-of-recovery input buffering are observable gameplay outcomes.
    auto Box=[&](FVector At,FVector Scale){auto* A=W->SpawnActor<AStaticMeshActor>(At,FRotator::ZeroRotator);auto* C=A->GetStaticMeshComponent();C->SetMobility(EComponentMobility::Movable);C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));C->SetWorldScale3D(Scale);C->SetCollisionProfileName(TEXT("BlockAll"));return A;};
    auto* Hidden=Enemy(S+V*1000-FVector(0,0,25));auto* Wall=Box(S+V*500,FVector(.5,8,8));
    const float HiddenHealth=Hidden->GetHealth();H->StartFiring();H->StopFiring();Tick(.7f,60);TestEqual(TEXT("Laser cannot shoot through cover"),Hidden->GetHealth(),HiddenHealth);Wall->Destroy();Hidden->Destroy();
    const int BeforeBuffered=H->LaserCount;H->StartFiring();H->StopFiring();Tick(.51f,60);H->StartFiring();H->StopFiring();Tick(.85f,60);
    TestEqual(TEXT("One input buffered in recovery executes once"),H->LaserCount,BeforeBuffered+2);
    H->StartSecondaryFire();H->StopSecondaryFire();TestEqual(TEXT("No surface rejects missile"),int(H->Gesture),0);TestEqual(TEXT("Invalid aim does not consume cooldown"),H->GetMissileCooldownRemaining(),0.f);
    // Static floor validates fixed-point aim and independent projectile ownership.
    auto* Floor=W->SpawnActor<AStaticMeshActor>(FVector(1500,0,-10),FRotator::ZeroRotator);auto* M=Floor->GetStaticMeshComponent();M->SetMobility(EComponentMobility::Movable);M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));M->SetWorldScale3D(FVector(60,60,.1f));M->SetCollisionProfileName(TEXT("BlockAll"));
    P->FirstPersonCamera->SetWorldRotation(FRotator(-15,0,0));
    H->StartSecondaryFire();H->StopSecondaryFire();const FVector Chosen=H->StrikePoint;TestEqual(TEXT("Secondary accepted"),int(H->Gesture),2);
    TestTrue(TEXT("Origin stays at altitude"),FMath::IsNearlyEqual(H->StrikeSpawn.Z-Chosen.Z,6014.f,1.f));
    P->FirstPersonCamera->SetWorldRotation(FRotator(-15,60,0));Tick(.5f,30);TestEqual(TEXT("One missile"),H->MissileCount,1);TestTrue(TEXT("Aim change preserves point"),H->StrikePoint.Equals(Chosen));TestEqual(TEXT("No cooldown after spawn"),H->GetMissileCooldownRemaining(),0.f);
    H->DeactivateWeapon();Tick(.6f,60);TestEqual(TEXT("No extra spawn after deactivation"),H->MissileCount,1);
    H->ActivateWeapon(TEXT("Player"));H->StartSecondaryFire();TestEqual(TEXT("Immediately available after equip"),int(H->Gesture),2);H->StopSecondaryFire();H->CancelReload();
    for(float FPS:{30.f,60.f,120.f})
    {
        const int Count=H->MissileCount;
        H->StartSecondaryFire();Tick(2.1f,FPS);H->StopSecondaryFire();
        TestEqual(TEXT("Holding secondary launches once"),H->MissileCount,Count+1);
        H->StartSecondaryFire();H->StopSecondaryFire();Tick(.92f,FPS);
        H->StartSecondaryFire();H->StopSecondaryFire();const FVector BufferedPoint=H->BufferedStrikePoint;
        P->FirstPersonCamera->SetWorldRotation(FRotator(-15,10,0));Tick(1.3f,FPS);
        TestEqual(TEXT("No CD and one buffered secondary press"),H->MissileCount,Count+3);
        TestTrue(TEXT("Buffered missile retains point at click time"),H->StrikePoint.Equals(BufferedPoint));
    }
    // Blast is distance-scaled and cover-aware; an already launched missile survives unequip.
    auto* NearEnemy=Enemy(FVector(3500,0,120));auto* FarEnemy=Enemy(FVector(3700,0,120));auto* Covered=Enemy(FVector(3400,200,120));
    const float N0=NearEnemy->GetHealth(),F0=FarEnemy->GetHealth(),C0=Covered->GetHealth();
    auto* Shield=Box(FVector(3400,100,120),FVector(1,.1,3));
    auto* TestDefinition=NewObject<UHeliosMysteriousHandDefinition>();TestDefinition->MissileMaxDamage=20;TestDefinition->MissileMinDamage=4;
    FActorSpawnParameters MP;MP.Owner=P;auto* Missile=W->SpawnActor<AHeliosHandMissile>(FVector(3400,0,920),FRotator::ZeroRotator,MP);Missile->Launch(FVector(3400,0,120),FVector::UpVector,TestDefinition);
    H->DeactivateWeapon();Tick(1.3f,120);
    TestTrue(TEXT("Missile continues after unequip"),NearEnemy->GetHealth()<N0);TestTrue(TEXT("Explosion damage falls with distance"),N0-NearEnemy->GetHealth()>F0-FarEnemy->GetHealth());TestEqual(TEXT("Blast blocked by solid cover"),Covered->GetHealth(),C0);
    // Place boundary targets on separate rays, so one capsule cannot shield another.
    const FVector Center(7000,0,150);
    auto* Inside=Enemy(Center+FVector(990,0,0));auto* Edge=Enemy(Center+FVector(0,1000,0));auto* Outside=Enemy(Center+FVector(-1010,0,0));
    const float I0=Inside->GetHealth(),E0=Edge->GetHealth(),O0=Outside->GetHealth();
    auto* Boundary=W->SpawnActor<AHeliosHandMissile>(Center+FVector(0,0,6000),FRotator::ZeroRotator,MP);Boundary->Launch(Center,FVector::UpVector,TestDefinition);Boundary->Explode(Center,FVector::UpVector);
    TestTrue(TEXT("9.9m receives falloff damage"),FMath::IsNearlyEqual(I0-Inside->GetHealth(),4.16f,.03f));
    TestTrue(TEXT("10m receives edge damage"),FMath::IsNearlyEqual(E0-Edge->GetHealth(),4.f,.03f));
    TestEqual(TEXT("10.1m receives no damage"),Outside->GetHealth(),O0);
    // The missile must hit the roof from above, not teleport beneath it.
    auto* Roof=Box(FVector(10000,0,720),FVector(12,12,.4));
    auto* Below=Enemy(FVector(10000,0,120));auto* Above=Enemy(FVector(10200,0,840));
    const float B0=Below->GetHealth(),A0=Above->GetHealth();
    auto* RoofMissile=W->SpawnActor<AHeliosHandMissile>(FVector(10000,0,6000),FRotator::ZeroRotator,MP);RoofMissile->Launch(FVector(10000,0,0),FVector::UpVector,TestDefinition);
    Tick(.8f,30);TestTrue(TEXT("Visible high-altitude descent before impact"),IsValid(RoofMissile)&&RoofMissile->GetActorLocation().Z>2000);
    Tick(.5f,30);TestEqual(TEXT("Roof shields targets underneath"),Below->GetHealth(),B0);TestTrue(TEXT("Roof-top explosion damages exposed targets"),Above->GetHealth()<A0);
    bool RoofImpact=false;for(TActorIterator<AHeliosHandImpact> It(W);It;++It)if(FMath::Abs(It->GetActorLocation().X-10000)<20&&FMath::Abs(It->GetActorLocation().Z-740)<3)RoofImpact=true;
    TestTrue(TEXT("Presentation uses actual roof impact location"),RoofImpact);
    Tick(9.f,60);TestEqual(TEXT("Tracked effects released after smoke finishes"),UHeliosWeaponVFX::GetTrackedCount(W),0);
    return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FHeliosMysteriousHandAimAssistTest,"Helios.MysteriousHand.AimAssist",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FHeliosMysteriousHandAimAssistTest::GetTests(TArray<FString>& Names,TArray<FString>& Commands) const
{
    for(int32 FPS:{30,60,120})
    {
        Names.Add(FString::Printf(TEXT("%dFPS"),FPS));
        Commands.Add(FString::FromInt(FPS));
    }
}
bool FHeliosMysteriousHandAimAssistTest::RunTest(const FString& Parameters)
{
    FTestWorldWrapper F;if(!F.CreateTestWorld(EWorldType::Game))return false;
    UWorld* W=F.GetTestWorld();W->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();if(!F.BeginPlayInTestWorld())return false;
    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* P=W->SpawnActor<AHeliosArenaCharacter>(FVector(0,0,100),FRotator::ZeroRotator,Spawn);
    P->GetCharacterMovement()->DisableMovement();
    if(!TestTrue(TEXT("Equip hand"),P->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("mysterious_hand")))))return false;
    auto* H=Cast<AHeliosMysteriousHand>(P->GetEquippedWeapon());if(!TestNotNull(TEXT("Hand"),H))return false;
    H->Settings=DuplicateObject<UHeliosMysteriousHandDefinition>(H->Settings,H);
    TestTrue(TEXT("Assist enabled by default"),H->Settings->bMissileAimAssist);
    TestEqual(TEXT("Independent missile cone"),H->Settings->MissileAimHalfAngle,8.f);
    const float FPS=FCString::Atof(*Parameters);
    if(!TestTrue(TEXT("Valid test frame rate"),FPS==30.f||FPS==60.f||FPS==120.f))return false;
    auto Tick=[&](float Seconds){for(int I=0;I<FMath::CeilToInt(Seconds*FPS);++I){++GFrameCounter;W->Tick(LEVELTICK_All,1.f/FPS);}};
    auto Box=[&](FVector At,FVector Scale)
    {
        auto* A=W->SpawnActor<AStaticMeshActor>(At,FRotator::ZeroRotator);auto* C=A->GetStaticMeshComponent();
        C->SetMobility(EComponentMobility::Movable);C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        C->SetWorldScale3D(Scale);C->SetCollisionProfileName(TEXT("BlockAll"));return A;
    };
    auto* Floor=Box(FVector(10000,0,-10),FVector(450,100,.2));
    FVector S,D;H->View(S,D);
    auto At=[&](float Range,float Yaw){return S+FRotator(0,Yaw,0).Vector()*Range;};
    auto Enemy=[&](FVector Location)
    {
        auto* E=W->SpawnActor<AHeliosEnemyE01>(Location,FRotator::ZeroRotator,Spawn);
        E->GetCharacterMovement()->DisableMovement();E->StopShooting();E->SetActorTickEnabled(false);
        if(E->GetController())E->GetController()->Destroy();return E;
    };
    FVector Point,Normal,Origin;AHeliosEnemyActor* Selected=nullptr;
    auto Resolve=[&](){Selected=nullptr;return H->ResolveStrike(Point,Normal,Origin,&Selected);};
    auto* E=Enemy(At(2000,7.99f));
    TestTrue(TEXT("Sky ray can acquire near-reticle enemy"),Resolve());TestTrue(TEXT("Inside cone selected"),Selected==E);
    TestTrue(TEXT("Strike projects to ground under capsule"),FMath::IsNearlyZero(Point.Z,.1)&&FMath::IsNearlyEqual(Point.X,E->GetActorLocation().X,.1));
    TestTrue(TEXT("Assisted origin keeps summon altitude"),FMath::IsNearlyEqual(Origin.Z-Point.Z,6014,.1));
    E->SetActorLocation(At(2000,8.01f));TestFalse(TEXT("Outside cone with no aim surface rejects"),Resolve());
    E->SetActorLocation(At(19999,2));TestTrue(TEXT("Inside 200 metres selected"),Resolve()&&Selected==E);
    E->SetActorLocation(At(20001,2));TestFalse(TEXT("Outside 200 metres rejects"),Resolve());
    E->SetActorLocation(At(2000,4));H->Settings->AimHalfAngle=.1f;
    TestTrue(TEXT("Laser cone does not affect missiles"),Resolve()&&Selected==E);
    H->Settings->bMissileAimAssist=false;TestFalse(TEXT("Disabled assist restores empty sky rejection"),Resolve());
    E->SetActorLocation(At(2000,0));TestTrue(TEXT("Disabled assist preserves direct collision"),Resolve()&&Selected==nullptr&&Point.Z>0);
    H->Settings->bMissileAimAssist=true;
    E->SetActorLocation(At(1800,4));auto* Other=Enemy(At(1200,-4));
    TestTrue(TEXT("Equal angle chooses nearer enemy"),Resolve()&&Selected==Other);
    E->SetActorLocation(At(2000,3));TestTrue(TEXT("Smaller angle wins over distance"),Resolve()&&Selected==E);
    // The nearer capsule grazes the reticle; the farther centre is closer to it but outside that capsule's shadow.
    Other->SetActorLocation(S+FVector(500,30,0));E->SetActorLocation(At(2000,-2));
    TestTrue(TEXT("Direct hit wins over smaller centre angle"),Resolve()&&Selected==Other);
    Other->Destroy();E->SetActorLocation(At(2000,4));
    auto* Wall=Box(S+FVector(1000,0,0),FVector(.5,8,8));
    TestTrue(TEXT("Covered enemy is skipped and wall remains target"),Resolve()&&Selected==nullptr&&Point.X<1100);
    Wall->Destroy();
    UGameplayStatics::ApplyDamage(E,100000,nullptr,P,UDamageType::StaticClass());
    TestTrue(TEXT("Target died"),E->IsEnemyDead());Resolve();TestNull(TEXT("Dead enemy never assisted"),Selected);E->Destroy();
    E=Enemy(At(2000,3));Other=Enemy(At(2000,-5));
    Resolve();const FVector PreferredOrigin=Origin;
    auto* Obstruction=Box(PreferredOrigin,FVector(1,1,1));
    TestTrue(TEXT("Blocked summon tries next candidate"),Resolve()&&Selected==Other);Obstruction->Destroy();
    Floor->Destroy();
    auto* OtherFloor=Box(FVector(Other->GetActorLocation().X,Other->GetActorLocation().Y,-10),FVector(1,1,.2));
    TestTrue(TEXT("Missing ground tries next candidate"),Resolve()&&Selected==Other);
    OtherFloor->Destroy();TestFalse(TEXT("No ground or aim surface rejects"),Resolve());
    Floor=Box(FVector(10000,0,-10),FVector(450,100,.2));
    // Ground probe may not extend beyond three metres below the capsule.
    const FVector SavedLocation=E->GetActorLocation();E->SetActorLocation(SavedLocation+FVector(0,0,600));Other->Destroy();
    P->FirstPersonCamera->SetWorldRotation((E->GetActorLocation()-S).Rotation());Resolve();TestNull(TEXT("Ground beyond probe is not assisted"),Selected);
    P->FirstPersonCamera->SetWorldRotation(FRotator::ZeroRotator);E->SetActorLocation(SavedLocation);
    Tick(.06f);TestTrue(TEXT("Idle preview acquires enemy"),H->PreviewEnemy.Get()==E);
    const FVector PreviewBefore=H->PreviewPosition;
    E->SetActorLocation(At(2200,4));Tick(.06f);TestTrue(TEXT("Preview refreshes with enemy"),!H->PreviewPosition.Equals(PreviewBefore));
    E->SetActorLocation(At(2300,5));H->StartSecondaryFire();H->StopSecondaryFire();
    const FVector Chosen=H->StrikePoint;
    TestTrue(TEXT("Click resolves fresh target position"),FMath::IsNearlyEqual(Chosen.X,E->GetActorLocation().X,.1));
    TestFalse(TEXT("Live candidate cleared during casting"),H->PreviewEnemy.IsValid());
    E->SetActorLocation(At(2400,6));P->FirstPersonCamera->SetWorldRotation(FRotator(0,60,0));Tick(.5f);
    TestTrue(TEXT("Movement and view change keep clicked point"),H->StrikePoint.Equals(Chosen));
    TestEqual(TEXT("Assisted click launches one missile"),H->MissileCount,1);
    Tick(.41f);P->FirstPersonCamera->SetWorldRotation(FRotator::ZeroRotator);
    H->StartSecondaryFire();H->StopSecondaryFire();const FVector BufferedPoint=H->BufferedStrikePoint;
    TestEqual(TEXT("Assisted secondary buffered"),int(H->Buffered),2);
    UGameplayStatics::ApplyDamage(E,100000,nullptr,P,UDamageType::StaticClass());E->Destroy();
    P->FirstPersonCamera->SetWorldRotation(FRotator(0,90,0));Tick(.65f);
    TestTrue(TEXT("Buffered point survives target death and view change"),H->StrikePoint.Equals(BufferedPoint));
    TestEqual(TEXT("Buffered assisted click launches once"),H->MissileCount,2);
    H->CancelReload();TestFalse(TEXT("Cancellation clears preview"),H->PreviewEnemy.IsValid());
    P->FirstPersonCamera->SetWorldRotation(FRotator::ZeroRotator);E=Enemy(At(2000,4));Tick(.06f);
    TestTrue(TEXT("Preview resumes when ready"),H->PreviewEnemy.Get()==E);
    H->DeactivateWeapon();TestFalse(TEXT("Unequip clears preview"),H->PreviewEnemy.IsValid());Tick(.06f);
    TestFalse(TEXT("Unequipped preview stays cleared"),H->PreviewEnemy.IsValid());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosMysteriousHandAnimationTest,"Helios.MysteriousHand.GestureContinuity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHeliosMysteriousHandAnimationTest::RunTest(const FString&)
{
    FTestWorldWrapper F;if(!F.CreateTestWorld(EWorldType::Game))return false;
    UWorld* W=F.GetTestWorld();W->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();if(!F.BeginPlayInTestWorld())return false;
    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* P=W->SpawnActor<AHeliosArenaCharacter>(FVector(0,0,100),FRotator::ZeroRotator,Spawn);
    P->GetCharacterMovement()->DisableMovement();
    if(!P->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("mysterious_hand"))))return false;
    auto* H=CastChecked<AHeliosMysteriousHand>(P->GetEquippedWeapon());
    auto* Anim=Cast<UHeliosHandAnimInstance>(H->GetFirstPersonMesh()->GetAnimInstance());
    if(!TestNotNull(TEXT("Dedicated inertial hand instance"),Anim))return false;
    TestTrue(TEXT("Index effect socket preserved"),H->GetFirstPersonMesh()->DoesSocketExist(TEXT("IndexTip")));
    TestTrue(TEXT("Thumb effect socket preserved"),H->GetFirstPersonMesh()->DoesSocketExist(TEXT("ThumbTip")));
    TestTrue(TEXT("Point lasts .6 seconds"),FMath::IsNearlyEqual(H->PointAnim->GetPlayLength(),.6f,.002f));
    TestTrue(TEXT("Boo lasts 1 second"),FMath::IsNearlyEqual(H->BooAnim->GetPlayLength(),1.f,.002f));
    auto* Floor=W->SpawnActor<AStaticMeshActor>(FVector(1500,0,-10),FRotator::ZeroRotator);
    auto* C=Floor->GetStaticMeshComponent();C->SetMobility(EComponentMobility::Movable);
    C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));C->SetWorldScale3D(FVector(60,60,.1));C->SetCollisionProfileName(TEXT("BlockAll"));
    P->FirstPersonCamera->SetWorldRotation(FRotator(-15,0,0));
    for(float FPS:{30.f,60.f,120.f})
    {
        auto Tick=[&](float Seconds){for(int I=0;I<FMath::RoundToInt(Seconds*FPS);++I){++GFrameCounter;W->Tick(LEVELTICK_All,1.f/FPS);}};
        H->CancelGesture();Tick(.1f);
        const int LaserBefore=H->LaserCount,MissileBefore=H->MissileCount;
        H->StartFiring();H->StopFiring();Tick(.1f);
        TestEqual(TEXT("Laser never commits during anticipation"),H->LaserCount,LaserBefore);
        Tick(.2f);TestEqual(TEXT("Laser commits once"),H->LaserCount,LaserBefore+1);
        Tick(.2f);H->StartSecondaryFire();H->StopSecondaryFire();
        TestEqual(TEXT("Alternate input buffered"),int(H->Buffered),2);
        const uint32 Serial=Anim->TransitionSerial;Tick(.2f);
        TestEqual(TEXT("Buffered point-to-boo has only one transition"),Anim->TransitionSerial,Serial+1);
        TestTrue(TEXT("No intermediate idle pose"),Anim->Sequence==H->BooAnim);
        TestEqual(TEXT("Boo anticipation does not commit early"),H->MissileCount,MissileBefore);
        Tick(.6f);TestEqual(TEXT("Boo commits once"),H->MissileCount,MissileBefore+1);
        TestTrue(TEXT("Component transform stays fixed throughout gesture"),H->GetFirstPersonMesh()->GetRelativeLocation().Equals(H->Settings->AttachmentLocation,.001f));
        H->CancelGesture();H->StartFiring();H->StopFiring();Tick(.1f);H->DeactivateWeapon();Tick(.3f);
        TestEqual(TEXT("Unequip cancels the pending pulse"),H->LaserCount,LaserBefore+1);
        H->ActivateWeapon(TEXT("Player"));TestTrue(TEXT("Re-equip restores idle"),Anim->Sequence==H->IdleAnim);
    }
    return true;
}
#endif
