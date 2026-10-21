#include "QA/HeliosUltimatumDemo.h"
#include "Pets/HeliosUltimatumPet.h"
#include "Pets/HeliosRifleVFXComponent.h"
#include "Pets/HeliosRifleAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "Adventure/HeliosAdventureCharacter.h"
#include "UI/HeliosAdventureHUD.h"
#include "Town/HeliosTownSubsystem.h"
#include "World/HeliosMapTravelSubsystem.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Boss/HeliosEnemyBoss.h"
#include "Variant_Shooter/AI/ShooterAIController.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/DamageType.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "ShaderCompiler.h"
#include "Materials/MaterialInterface.h"
namespace { int32 UltimatumTravelPhase=0; FString TravelResults; int32 TravelFailures=0; }
AHeliosUltimatumDemo::AHeliosUltimatumDemo(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void AHeliosUltimatumDemo::Check(FString Name,bool Passed)
{
    if(!Passed)++Failures;if(!Results.IsEmpty())Results+=TEXT(",\n");Results+=FString::Printf(TEXT("{\"name\":\"%s\",\"passed\":%s}"),*Name,Passed?TEXT("true"):TEXT("false"));
    UE_LOG(LogTemp,Display,TEXT("ULTIMATUM_CHECK %s %s"),*Name,Passed?TEXT("PASS"):TEXT("FAIL"));
}
void AHeliosUltimatumDemo::Capture(FString Name)
{
    if(FParse::Param(FCommandLine::Get(),TEXT("UltimatumRecord"))&&!Name.StartsWith(TEXT("frames/")))return;
    FString Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/ultimatum_gr")/GetWorld()->GetMapName()/(Name+TEXT(".png")));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);FScreenshotRequest::RequestScreenshot(Path,true,false);
}
void AHeliosUltimatumDemo::Finish()
{
    const bool Travel=FParse::Param(FCommandLine::Get(),TEXT("UltimatumTravel"));
    FString Dir=FPaths::ProjectDir()/TEXT("../qa/ultimatum_gr");if(!Travel)Dir/=GetWorld()->GetMapName();IFileManager::Get().MakeDirectory(*Dir,true);
    FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"status\":\"%s\",\"frames\":%d,\"duration\":%.3f,\"checks\":[%s]}"),Failures?TEXT("failed"):TEXT("passed"),Frame,RecordClock,*Results),*(Dir/(Travel?TEXT("travel_report.json"):TEXT("report.json"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    if(FrameTimes.Num())
    {
        FString Times;for(int I=0;I<FrameTimes.Num();++I){if(I)Times+=TEXT(",");Times+=FString::Printf(TEXT("%.6f"),FrameTimes[I]);}
        FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"times\":[%s]}"),*Times),*(Dir/TEXT("frame_times.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
    Stage=99;FPlatformMisc::RequestExit(false);
}
void AHeliosUltimatumDemo::CameraView(FVector Offset,FVector Focus)
{
    const FVector Look=Pet->GetActorLocation()+Focus;FVector Best=Look+Offset;float Max=-1;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(UltimatumCamera),false,Pet);Q.AddIgnoredActor(Enemy);Q.AddIgnoredActor(Pet->CompanionOwner.Get());
    for(float Angle:{0.f,180.f,45.f,-45.f,90.f,-90.f})
    {FVector Candidate=Look+Offset.RotateAngleAxis(Angle,FVector::UpVector);FHitResult Hit;float Fraction=GetWorld()->LineTraceSingleByChannel(Hit,Look,Candidate,ECC_Visibility,Q)?Hit.Time:1.f;
        if(Fraction>Max){Max=Fraction;Best=FMath::Lerp(Look,Candidate,FMath::Max(.1f,Fraction-.03f));}if(Fraction==1)break;}
    Camera->SetActorLocation(Best);Camera->SetActorRotation((Look-Best).Rotation());Camera->GetCameraComponent()->SetFieldOfView(48);
}
void AHeliosUltimatumDemo::CollisionChecks()
{
    Pet->SetActorTickEnabled(false);Pet->SetActorRotation(FRotator::ZeroRotator);Pet->GetCharacterMovement()->StopMovementImmediately();
    if(auto* AI=Cast<AAIController>(Pet->GetController()))AI->StopMovement();
    const FVector Origin=Pet->GetActorLocation();
    auto Step=[&](float Dt){Pet->UpdateAttack(Dt);Pet->GetMesh()->TickAnimation(Dt,false);Pet->GetMesh()->RefreshBoneTransforms();Pet->PostPose(Dt);};
    auto Reset=[&](){Pet->CancelAttack();Pet->Target=Enemy;Pet->State=EHeliosPetState::Fighting;Pet->Ammo=36;Pet->ShotsFired=0;Pet->HitsDelivered=0;Pet->ReloadsCompleted=0;Pet->Pitch=0;Pet->Recoil=0;Pet->AimAge=.31f;Enemy->CurrentHP=10000;Pet->SetPose(TEXT("Guard"));Pet->GetMesh()->TickAnimation(.2f,false);Pet->GetMesh()->RefreshBoneTransforms();};
    Check(TEXT("imported_upper_spine_exists"),Pet->GetMesh()->DoesSocketExist(TEXT("M-bone-Spine1")));
    Check(TEXT("native_rifle_animation"),Cast<UHeliosRifleAnimInstance>(Pet->GetMesh()->GetAnimInstance())!=nullptr);
    Check(TEXT("rifle_assets_and_35m_range"),Pet->Rifle->GetStaticMesh()&&Pet->Magazine->GetStaticMesh()&&Pet->RifleSettings.Range==3500);
    for(int FPS:{15,30,60,120})
    {
        Enemy->SetActorLocation(Origin+FVector(1000,0,48));Reset();
        for(int I=0;I<FPS*5;++I)Step(1.f/FPS);
        UE_LOG(LogTemp,Display,TEXT("RIFLE_FPS %d shots=%d hits=%d damage=%.1f ammo=%d muzzle=%s forward=%s"),FPS,Pet->ShotsFired,Pet->HitsDelivered,10000-Enemy->CurrentHP,Pet->Ammo,*Pet->GetMesh()->GetSocketLocation(TEXT("rifle_muzzle")).ToString(),*Pet->Rifle->GetForwardVector().ToString());
        Check(FString::Printf(TEXT("rifle_%dfps_30_shots"),FPS),Pet->ShotsFired==30&&Pet->Ammo==6);
        Check(FString::Printf(TEXT("rifle_%dfps_damage_matches_contacts"),FPS),Pet->HitsDelivered>=27&&FMath::IsNearlyEqual(10000-Enemy->CurrentHP,Pet->HitsDelivered*12.f));
        for(int I=0;I<FPS*5;++I)Step(1.f/FPS);
        Check(FString::Printf(TEXT("rifle_%dfps_reload_and_resume"),FPS),Pet->ReloadsCompleted==1&&Pet->ShotsFired>36&&Pet->Ammo<36);
    }
    for(int FPS:{15,30,60,120})
    {
        Enemy->GetCapsuleComponent()->SetCapsuleSize(44,125);Enemy->SetActorLocation(Origin+FVector(150,0,23));Reset();
        Pet->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        for(int I=0;I<FPS*3;++I){Pet->TrackCombatTarget(1.f/FPS);Step(1.f/FPS);}
        Check(FString::Printf(TEXT("rifle_%dfps_contact_range_returns_fire"),FPS),Pet->HitsDelivered>=5);
        Pet->SetActorRotation(FRotator::ZeroRotator);
        Enemy->SetActorLocation(Origin+FVector(82,0,23));Reset();
        for(int I=0;I<FPS*3;++I){Pet->TrackCombatTarget(1.f/FPS);Step(1.f/FPS);}
        Check(FString::Printf(TEXT("rifle_%dfps_barrel_contact_returns_fire"),FPS),Pet->HitsDelivered>=5);
        Pet->SetActorRotation(FRotator::ZeroRotator);
    }
    Enemy->GetCapsuleComponent()->SetCapsuleSize(34,80);
    Enemy->SetActorLocation(Origin+FVector(1000,0,48));Reset();
    Pet->Ammo=0;Pet->UpdateAttack(.01f);Pet->UpdateAttack(.8f);
    Pet->Health=150;UGameplayStatics::ApplyDamage(Pet,1,nullptr,Enemy,UDamageType::StaticClass());
    Check(TEXT("damage_causer_immediately_selected"),Pet->Target==Enemy&&Pet->Attacker.Get()==Enemy);
    Pet->UpdateAttack(1.11f);
    Check(TEXT("damage_during_reload_preserves_progress"),Pet->Ammo==36&&Pet->ReloadsCompleted==1);
    Pet->Health=150;
    Enemy->SetActorLocation(Origin+FVector(3600,0,48));Reset();for(int I=0;I<90;++I)Step(1.f/30);
    Check(TEXT("outside_range_no_shots"),Pet->ShotsFired==0&&Enemy->CurrentHP==10000);
    Enemy->SetActorLocation(Origin+FVector(-1000,0,0));Reset();for(int I=0;I<30;++I)Step(1.f/30);
    Check(TEXT("behind_pet_waits_for_turn"),Pet->ShotsFired==0);
    Enemy->GetCapsuleComponent()->SetCapsuleSize(42,68);Enemy->SetActorLocation(Origin+FVector(1000,0,-34));Reset();for(int I=0;I<90;++I)Step(1.f/30);
    Check(TEXT("low_target_aimed_and_hit"),Pet->HitsDelivered>0);Enemy->GetCapsuleComponent()->SetCapsuleSize(34,80);
    Enemy->SetActorLocation(Origin+FVector(1000,0,48));Reset();for(int I=0;I<12;++I)Step(1.f/30);
    Before=Enemy->CurrentHP;Enemy->SetActorLocation(Origin+FVector(1000,600,48));for(int I=0;I<30;++I)Step(1.f/30);
    Check(TEXT("target_leaves_barrel_path_no_homing"),Enemy->CurrentHP==Before);
    Pet->Ammo=0;Pet->UpdateAttack(.01);Pet->UpdateAttack(.6);Pet->CancelAttack();
    Check(TEXT("interrupted_reload_does_not_refill"),Pet->Ammo==0&&!Pet->IsReloading());
    Check(TEXT("interrupt_clears_rifle_fx"),Pet->RifleFX->GetLiveCount()==0);
    for(int I=0;I<100;++I)Pet->RifleFX->Shot(Origin,Origin+FVector(500,0,0),-FVector::ForwardVector,true);
    Check(TEXT("rifle_effect_budget"),Pet->RifleFX->GetLiveCount()<=240);Pet->RifleFX->Clear();
    Enemy->SetActorLocation(Origin+FVector(1000,0,48));Reset();
    FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Fixture=GetWorld()->SpawnActor<AStaticMeshActor>(Origin+FVector(500,0,0),FRotator::ZeroRotator,P);Wall=Fixture;
    auto* M=Fixture->GetStaticMeshComponent();M->SetMobility(EComponentMobility::Movable);M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Wall->SetActorScale3D(FVector(.1f,5,5));M->SetCollisionProfileName(TEXT("BlockAll"));
}
void AHeliosUltimatumDemo::Tick(float Dt)
{
    Super::Tick(Dt);if(Stage==99)return;if(!Started)Started=FPlatformTime::Seconds();if(FPlatformTime::Seconds()-Started>240){Check(TEXT("timeout"),false);Finish();return;}
    auto* Player=Cast<AHeliosAdventureCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));auto* Director=AHeliosAdventureDirector::Find(GetWorld());if(!Player||!Director)return;
    auto* PC=Cast<APlayerController>(Player->GetController());auto* HUD=PC?Cast<AHeliosAdventureHUD>(PC->GetHUD()):nullptr;if(!HUD)return;
    auto* Pets=GetWorld()->GetSubsystem<UHeliosPetWorld>();const bool Record=FParse::Param(FCommandLine::Get(),TEXT("UltimatumRecord"));
    Age+=Dt;
    if(Stage==0)
    {
        if(auto* Town=Director->GetTownSubsystem())if(!Town->IsWorldReady())return;
        if(Age<3)return;
        HUD->SelectPet(EHeliosPetKind::UltimatumGR);Check(TEXT("menu_selects_butterfly"),HUD->GetSelectedPet()==EHeliosPetKind::UltimatumGR);Capture(TEXT("00_menu"));Stage=1;Age=0;return;
    }
    if(Stage==1)
    {if(Age<.5)return;HUD->SelectWeapon(TEXT("rifle"));HUD->StartSelectedRun();if(Director->IsSelectingWeapon())return;Player->SetCanBeDamaged(false);Stage=2;Age=0;return;}
    if(Stage==2)
    {
        Pet=Pets?Cast<AHeliosUltimatumPet>(Pets->Pet):nullptr;if(!Pet||Age<3)return;
        if(FParse::Param(FCommandLine::Get(),TEXT("UltimatumTravel")))
        {
            if(UltimatumTravelPhase==0){Pet->Health=73;Director->SetActorTickEnabled(false);Stage=20;Age=0;return;}
            if(UltimatumTravelPhase==1)
            {
                Check(TEXT("map_travel_keeps_selected_pet"),Pet->Kind==EHeliosPetKind::UltimatumGR);
                Check(TEXT("map_travel_keeps_health"),FMath::IsNearlyEqual(Pet->Health,73));
                Check(TEXT("arrived_other_map"),GetWorld()->GetMapName().EndsWith(TEXT("IkunTown")));
                Pet->Sleep();Director->SetActorTickEnabled(false);Stage=22;Age=0;return;
            }
            Results=TravelResults;Failures=TravelFailures;
            Check(TEXT("return_map_keeps_sleeping_pet"),Pet->Kind==EHeliosPetKind::UltimatumGR&&Pet->IsSleeping()&&Pet->Health==0);
            Check(TEXT("return_map_keeps_recovery_time"),Pet->SleepRemaining>18&&Pet->SleepRemaining<29);
            Check(TEXT("returned_original_map"),GetWorld()->GetMapName().EndsWith(TEXT("HeliosAdventure")));Finish();return;
        }
        Check(TEXT("model_and_animations_ready"),Pet->bModelReady);Check(TEXT("rifle_tracer_material_available"),LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Helios/Pets/UltimatumGR/M_RifleAir.M_RifleAir"))!=nullptr);Check(TEXT("25m_follow"),Pet->Settings.FollowDistance==2500);Check(TEXT("original_height_capsule"),FMath::IsNearlyEqual(Pet->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),102));
        if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
        int Count=0;for(TActorIterator<AHeliosPetCompanion> It(GetWorld());It;++It)++Count;Check(TEXT("single_companion"),Count==1);
        Director->SetActorTickEnabled(false);for(TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It)It->Destroy();
        Pet->CancelAttack();Pet->Target.Reset();Pet->DecisionRemaining=100;Pet->GetCharacterMovement()->StopMovementImmediately();
        if(auto* AI=Cast<AAIController>(Pet->GetController()))AI->StopMovement();Pet->bMoveRequested=false;
        Anchor=Pet->GetActorLocation();Pet->SetActorRotation(FRotator::ZeroRotator);
        Camera=GetWorld()->SpawnActor<ACameraActor>();PC->SetViewTarget(Camera);CameraView(FVector(370,440,110),FVector(0,0,0));Stage=3;Age=0;
        if(FParse::Param(FCommandLine::Get(),TEXT("UltimatumGait")))
        {
            Anchor=FVector(0,0,10000);Pet->SetActorLocation(Anchor);
            auto* Floor=GetWorld()->SpawnActor<AStaticMeshActor>(Anchor-FVector(0,0,112),FRotator::ZeroRotator);
            Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
            Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
            Floor->GetStaticMeshComponent()->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial")));
            Floor->SetActorScale3D(FVector(300,300,.2));
            Pet->SetActorTickEnabled(false);Pet->GetCharacterMovement()->SetComponentTickEnabled(false);Pet->GetMesh()->SetComponentTickEnabled(false);
            for(const FString Name:{TEXT("Walk"),TEXT("BackWalk"),TEXT("Run"),TEXT("Idle")})
            {
                const FString P=FString::Printf(TEXT("/Game/Helios/QA/GaitBaseline/A_Before_%s.A_Before_%s"),*Name,*Name);
                Pet->Poses.Add(FName(*(TEXT("Before")+Name)),LoadObject<UAnimSequence>(nullptr,*P));
            }
            PC->GetHUD()->bShowHUD=false;Stage=70;
        }
        return;
    }
    if(Stage==70){GaitTick(Dt);return;}
    if(Record&&((Stage>=3&&Stage<=7)||Stage==30||Stage==40||Stage==41||Stage==42||(Stage>=50&&Stage<=54)))
    {
        if(Frame==0&&RecordClock==0)UE_LOG(LogTemp,Display,TEXT("ULTIMATUM_RECORD_START time=%.6f"),GetWorld()->GetTimeSeconds());
        RecordClock+=Dt;RecordStep+=Dt;if(RecordStep>=1.f/30.f){RecordStep=0;FrameTimes.Add(RecordClock);Capture(FString::Printf(TEXT("frames/frame_%05d"),Frame++));}
    }
    if(Stage==3)
    {
        if(Age>.6&&Age<.6+Dt)Capture(TEXT("01_full_body"));if(Age<2)return;
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
        const FVector NavOrigin=Pet->GetActorLocation();bool RouteFound=false;int ProjectedCount=0,PathCount=0;
        if(Nav)for(float RouteDistance:{4000.f,3000.f,2000.f,1200.f})
        for(int RouteAngle=0;RouteAngle<24&&!RouteFound;++RouteAngle)
        {
            const FVector Direction=Player->GetActorForwardVector().RotateAngleAxis(RouteAngle*15.f,FVector::UpVector);
            FNavLocation Point;if(!Nav->ProjectPointToNavigation(NavOrigin+Direction*RouteDistance,Point,FVector(500,500,800)))continue;
            ++ProjectedCount;
            auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),NavOrigin,Point.Location,Pet);
            if(!Path||!Path->IsValid()||Path->IsPartial())continue;
            ++PathCount;
            const FVector Center=Point.Location+FVector(0,0,Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+30);
            if(!Player->TeleportTo(Center,Direction.Rotation(),false,false))continue;
            RouteFound=true;break;
        }
        UE_LOG(LogTemp,Display,TEXT("ULTIMATUM_ROUTE nav=%d projected=%d paths=%d found=%d origin=%s"),Nav!=nullptr,ProjectedCount,PathCount,RouteFound,*NavOrigin.ToString());
        Check(TEXT("follow_fixture_uses_reachable_ground"),RouteFound);
        if(FParse::Param(FCommandLine::Get(),TEXT("UltimatumRouteProbe"))){Finish();return;}
        Pet->DecisionRemaining=0;Stage=30;Age=0;return;
    }
    if(Stage==30)
    {
        CameraView(FVector(370,440,110),FVector(0,0,0));if(Age<4)return;
        Check(TEXT("live_follows_owner"),FVector::Dist2D(Anchor,Pet->GetActorLocation())>100);
        if(auto* AI=Cast<AAIController>(Pet->GetController()))AI->StopMovement();
        Pet->GetCharacterMovement()->StopMovementImmediately();Pet->bMoveRequested=false;Pet->SetActorRotation(FRotator::ZeroRotator);Anchor=Pet->GetActorLocation();
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        if(!Record&&!bCombatBrainChecked)
        {
            auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());bool Found=false;
            FCollisionQueryParams Q(SCENE_QUERY_STAT(RifleAIFixture),false,Pet);Q.AddIgnoredActor(Player);
            for(int Ring=0;Ring<10&&!Found;++Ring)for(int I=0;I<16&&!Found;++I)
            {
                FNavLocation A,B;const FVector Candidate=Anchor+FVector(Ring*400,0,0).RotateAngleAxis(I*22.5f,FVector::UpVector);
                if(!Nav||!Nav->ProjectPointToNavigation(Candidate,A,FVector(150,150,500))||!Nav->ProjectPointToNavigation(A.Location+FVector(1200,0,0),B,FVector(70,70,180)))continue;
                if(FMath::Abs(A.Location.Z-B.Location.Z)>20)continue;
                FHitResult H;if(GetWorld()->SweepSingleByChannel(H,A.Location+FVector(0,0,125),B.Location+FVector(0,0,125),FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(70),Q))continue;
                Anchor=A.Location+FVector(0,0,104);Pet->SetActorLocation(Anchor);Player->SetActorLocation(Anchor+FVector(-400,250,10));Found=true;
            }
            Check(TEXT("boss_duel_uses_navigable_map_ground"),Found);
            Enemy=GetWorld()->SpawnActor<AHeliosEnemyBoss>(Anchor+FVector(450,0,23),FRotator(0,180,0),P);Enemy->CurrentHP=10000;
            if(auto* AI=Cast<AShooterAIController>(Enemy->GetController())){if(AI->GetBrainComponent())AI->GetBrainComponent()->StopLogic(TEXT("Rifle boss regression"));AI->ReportAwarenessThreat(Pet,Pet->GetActorLocation(),true);AI->SetActorTickEnabled(false);}
            Pet->CancelAttack();Pet->Target.Reset();Pet->Health=500;Pet->Ammo=36;Pet->ShotsFired=Pet->HitsDelivered=0;Pet->DecisionRemaining=1000;
            UGameplayStatics::ApplyDamage(Pet,1,nullptr,Enemy,UDamageType::StaticClass());
            Check(TEXT("boss_hit_wakes_combat_immediately"),Pet->Target==Enemy&&Pet->State==EHeliosPetState::Fighting);
            Stage=60;Age=0;return;
        }
        if(!Record)
        {
            const FVector Base=Anchor+FVector(0,0,1800);
            auto* TestFloor=GetWorld()->SpawnActor<AStaticMeshActor>(Base-FVector(0,0,112),FRotator::ZeroRotator,P);
            auto* FM=TestFloor->GetStaticMeshComponent();FM->SetMobility(EComponentMobility::Movable);FM->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));TestFloor->SetActorScale3D(FVector(90,90,.2));FM->SetCollisionProfileName(TEXT("BlockAll"));
            Pet->SetActorLocation(Base);Player->SetActorLocation(Base+FVector(-500,0,20));Anchor=Base;
        }
        else
        {
            auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());bool Found=false;
            FCollisionQueryParams Q(SCENE_QUERY_STAT(RifleDemoGround),false,Pet);Q.AddIgnoredActor(Player);
            for(int Ring=0;Ring<8&&!Found;++Ring)for(int I=0;I<16&&!Found;++I)
            {
                FNavLocation A,B;const FVector Candidate=Anchor+FVector(Ring*450,0,0).RotateAngleAxis(I*22.5f,FVector::UpVector);
                if(!Nav||!Nav->ProjectPointToNavigation(Candidate,A,FVector(180,180,600))||!Nav->ProjectPointToNavigation(A.Location+FVector(1300,0,0),B,FVector(80,100,180)))continue;
                if(FMath::Abs(A.Location.Z-B.Location.Z)>25)continue;
                FHitResult Hit;if(GetWorld()->SweepSingleByChannel(Hit,A.Location+FVector(0,0,130),B.Location+FVector(0,0,130),FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(65),Q))continue;
                Anchor=A.Location+FVector(0,0,102);Pet->SetActorLocation(Anchor);Player->SetActorLocation(Anchor+FVector(-300,200,10));Found=true;
            }
            Check(TEXT("record_uses_clear_original_map_ground"),Found);
        }
        Enemy=GetWorld()->SpawnActor<AHeliosEnemyActor>(Anchor+FVector(1200,0,-22),FRotator(0,180,0),P);Enemy->CurrentHP=10000;Enemy->StopShooting();Enemy->GetCharacterMovement()->DisableMovement();Enemy->SetActorTickEnabled(false);
        if(auto* AI=Cast<AAIController>(Enemy->GetController())){if(AI->GetBrainComponent())AI->GetBrainComponent()->StopLogic(TEXT("Ultimatum demo"));AI->StopMovement();}
        CameraView(FVector(100,-820,125),FVector(140,0,20));
        if(!Record){CollisionChecks();Stage=31;}else{Pet->Target=Enemy;Pet->Cooldown=0;Pet->DecisionRemaining=1000;Stage=4;}
        Age=0;Before=Enemy->CurrentHP;return;
    }
    if(Stage==60)
    {
        CameraView(FVector(150,-950,220),FVector(130,0,0));
        if(Age>4&&Age<4+Dt)Capture(TEXT("boss_counterattack"));
        // Locomotion and live boss timing can start the reload near the probe's
        // deadline. Finish an already-started reload, with a strict upper bound;
        // a companion that never starts reloading still fails at 12 seconds.
        if(Age<12||(Pet->IsReloading()&&Age<12+Pet->RifleSettings.ReloadSeconds+.25f))return;
        UE_LOG(LogTemp,Display,TEXT("RIFLE_BOSS_DUEL shots=%d hits=%d pet_hp=%.1f boss_hp=%.1f move=%.1f"),Pet->ShotsFired,Pet->HitsDelivered,Pet->Health,Enemy->CurrentHP,FVector::Dist2D(Anchor,Pet->GetActorLocation()));
        Check(TEXT("live_boss_repeated_attacks_land"),Pet->Health<499&&Pet->Health>0);
        Check(TEXT("live_boss_pressure_returns_sustained_fire"),Pet->ShotsFired>=15&&Pet->HitsDelivered>=8);
        Check(TEXT("live_boss_pressure_reloads"),Pet->ReloadsCompleted>0);
        Enemy->StopShooting();Enemy->SetActorTickEnabled(false);Enemy->GetCharacterMovement()->DisableMovement();
        if(auto* AI=Cast<AAIController>(Enemy->GetController()))AI->StopMovement();
        Pet->CancelAttack();Pet->SetActorLocation(Anchor);Pet->SetActorRotation(FRotator::ZeroRotator);Pet->Health=150;Pet->Ammo=36;Pet->GetCharacterMovement()->StopMovementImmediately();
        Enemy->SetActorLocation(Anchor+FVector(1100,0,23));Pet->Target=Enemy;Pet->DecisionRemaining=1000;
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Fixture=GetWorld()->SpawnActor<AStaticMeshActor>(Anchor+FVector(500,0,40),FRotator::ZeroRotator,P);Wall=Fixture;
        auto* M=Fixture->GetStaticMeshComponent();M->SetMobility(EComponentMobility::Movable);M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Wall->SetActorScale3D(FVector(.2f,3,4));M->SetCollisionProfileName(TEXT("BlockAll"));
        Before=Enemy->CurrentHP;Check(TEXT("live_cover_initially_blocks_fire"),!Pet->HasClearLine(Enemy));Stage=61;Age=0;return;
    }
    if(Stage==61)
    {
        CameraView(FVector(150,-1100,300),FVector(300,0,0));if(Age<8)return;
        Check(TEXT("live_cover_reposition_restores_fire"),Enemy->CurrentHP<Before&&FVector::Dist2D(Anchor,Pet->GetActorLocation())>80);
        Capture(TEXT("cover_reposition"));Wall->Destroy();Wall=nullptr;Enemy->Destroy();Enemy=nullptr;
        Pet->CancelAttack();Pet->Target.Reset();Pet->Health=150;Pet->GetCharacterMovement()->StopMovementImmediately();
        Pet->DecisionRemaining=1000;bCombatBrainChecked=true;Stage=30;Age=4;return;
    }
    if(Stage==31&&Age>.3f)
    {
        Check(TEXT("wall_fixture_blocks_sight"),!Pet->HasClearLine(Enemy));
        const float HP=Enemy->CurrentHP;for(int I=0;I<65;++I)Pet->UpdateAttack(1.f/30.f);
        Check(TEXT("wall_no_damage"),Enemy->CurrentHP==HP);Wall->Destroy();Wall=nullptr;Stage=32;Age=0;return;
    }
    if(Stage==32&&Age>.3f){Enemy->SetActorLocation(Anchor+FVector(1200,0,-22));Pet->CancelAttack();Pet->Target=Enemy;Pet->Cooldown=0;Pet->DecisionRemaining=1000;Pet->SetActorTickEnabled(true);Stage=4;Age=0;Before=Enemy->CurrentHP;return;}
    if(Stage==4)
    {
        CameraView(Age<4?FVector(0,-650,150):Age<8?FVector(520,-380,140):FVector(-330,-580,160),FVector(120,0,10));
        if(!bCombatAnchored&&Pet->AttackAge>=0)
        {
            Check(TEXT("starts_attack_at_rifle_range"),FVector::Dist2D(Pet->GetActorLocation(),Enemy->GetActorLocation())<=3500.f);
            Anchor=Pet->GetActorLocation();bCombatAnchored=true;
        }
        if(!bNearCaptured&&Age>1&&Pet->ShotsFired>5){Capture(TEXT("02_combo"));bNearCaptured=true;}if(Age<12)return;
        Check(TEXT("live_rifle_damage"),Enemy->CurrentHP<Before);Check(TEXT("plants_feet_while_shooting"),bCombatAnchored&&FVector::Dist2D(Pet->GetActorLocation(),Anchor)<20);
        Anchor=Pet->GetActorLocation();Before=Enemy->CurrentHP;Stage=40;Age=0;return;
    }
    if(Stage==40)
    {
        Enemy->SetActorLocation(Anchor+FVector(1200+Age*30,100*FMath::Sin(Age),-22));
        CameraView(FVector(100,-820,140),FVector(180,0,0));if(Age<4)return;
        Check(TEXT("moving_target_tracked_and_hit"),Enemy->CurrentHP<Before);
        Pet->CancelAttack();Pet->SetActorTickEnabled(false);Pet->GetCharacterMovement()->StopMovementImmediately();Pet->GetCharacterMovement()->DisableMovement();
        if(auto* AI=Cast<AAIController>(Pet->GetController()))AI->StopMovement();
        Pet->SetActorRotation(FRotator::ZeroRotator);Anchor=Pet->GetActorLocation();Enemy->SetActorLocation(Anchor+FVector(3600,0,-22));
        Pet->Target=Enemy;Pet->Cooldown=0;Before=Enemy->CurrentHP;Stage=41;Age=0;return;
    }
    if(Stage==41)
    {
        Pet->UpdateAttack(Dt);CameraView(FVector(180,-950,150),FVector(220,0,0));
        if(Age<2)return;Check(TEXT("live_beyond_35m_no_damage"),Enemy->CurrentHP==Before);
        Pet->CancelAttack();Enemy->SetActorLocation(Anchor+FVector(85,0,-22));Pet->Target=Enemy;Pet->Cooldown=0;Pet->UpdateAttack(.01f);
        Enemy->SetActorLocation(Anchor+FVector(650,600,-22));Before=Enemy->CurrentHP;Stage=42;Age=0;return;
    }
    if(Stage==42)
    {
        Pet->UpdateAttack(Dt);CameraView(FVector(180,-1000,150),FVector(250,0,0));if(Age<2)return;
        Check(TEXT("live_whiff_no_damage"),Enemy->CurrentHP==Before);
        Pet->CancelAttack();Pet->GetCharacterMovement()->SetMovementMode(MOVE_Walking);Pet->SetActorTickEnabled(true);Pet->DecisionRemaining=1000;
        CameraView(FVector(60,-2500,150),FVector(45,0,0));Stage=5;Age=0;return;
    }
    if(Stage==5)
    {
        if(!bFarCaptured&&Pet->AttackAge>.19f&&Pet->AttackAge<.29f){Capture(TEXT("03_combat_25m"));bFarCaptured=true;}if(Age<2.5)return;
        Enemy->Destroy();Enemy=nullptr;Pet->CancelAttack();Pet->Target.Reset();Pet->DecisionRemaining=100;
        CameraView(FVector(370,440,110),FVector(0,0,0));UGameplayStatics::ApplyDamage(Pet,1000,nullptr,this,UDamageType::StaticClass());Check(TEXT("sleep_30_seconds"),Pet->IsSleeping()&&Pet->SleepRemaining>29.9f);Stage=6;Age=0;return;
    }
    if(Stage==6)
    {
        if(Age>1&&Age<1+Dt)Capture(TEXT("04_rest"));if(Age<2)return;
        Check(TEXT("effects_cleared_on_down"),Pet->RifleFX->GetLiveCount()==0);
        Check(TEXT("down_animation_one_second"),FMath::IsNearlyEqual(Pet->RecoveryDuration(TEXT("Down"),0),1.f,.03f));
        Check(TEXT("wake_animation_three_seconds"),FMath::IsNearlyEqual(Pet->RecoveryDuration(TEXT("Wake"),0),3.f,.03f));
        const FVector Head=Pet->GetMesh()->GetSocketLocation(TEXT("M-bone-Head"));
        const FVector Hips=Pet->GetMesh()->GetSocketLocation(TEXT("M-bone-Pelvis"));
        Check(TEXT("lying_body_horizontal"),FMath::Abs(Head.Z-Hips.Z)<30.f&&Head.Z<Pet->GetActorLocation().Z-40.f);Before=Pet->SleepRemaining;UGameplayStatics::SetGamePaused(this,true);PauseAt=FPlatformTime::Seconds();Stage=8;return;
    }
    if(Stage==8)
    {
        if(FPlatformTime::Seconds()-PauseAt<1)return;Check(TEXT("pause_freezes_recovery"),FMath::IsNearlyEqual(Before,Pet->SleepRemaining));UGameplayStatics::SetGamePaused(this,false);
        // Record a short transition, while the separate sleep check validates the configured duration.
        if(Record)Pet->SleepRemaining=.5f;Stage=7;Age=0;return;
    }
    if(Stage==7)
    {
        CameraView(FVector(370,440,110),FVector(0,0,0));
        if(Pet->State==EHeliosPetState::Waking)
        {
            if(Pet->GetVelocity().SizeSquared()>1||Pet->AttackAge>=0)Check(TEXT("wake_must_finish_before_moving"),false);
            return;
        }
        if(Pet->IsSleeping())return;
        if(Age<3)return;Check(TEXT("wake_restores_full_health"),Pet->Health==Pet->Settings.MaxHealth);Capture(TEXT("05_awake"));
        RecoveryOrigin=Pet->GetActorLocation();RecoveryCase=0;Stage=50;Age=0;return;
    }
    if(Stage==50)
    {
        if(auto* AI=Cast<AAIController>(Pet->GetController()))AI->StopMovement();
        Pet->CancelAttack();Pet->Target.Reset();Pet->DecisionRemaining=100;
        Pet->GetCharacterMovement()->StopMovementImmediately();Pet->GetCharacterMovement()->DisableMovement();
        Pet->SetActorLocation(RecoveryOrigin+FVector(0,0,205));Pet->SetActorRotation(FRotator::ZeroRotator);
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Floor=GetWorld()->SpawnActor<AStaticMeshActor>(RecoveryOrigin+FVector(0,0,88),FRotator(RecoveryCase==1?12.f:0,0,0),P);
        RecoveryFloor=Floor;auto* Mesh=Floor->GetStaticMeshComponent();Mesh->SetMobility(EComponentMobility::Movable);
        Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Floor->SetActorScale3D(FVector(6,6,.2f));Mesh->SetCollisionProfileName(TEXT("BlockAll"));
        if(RecoveryCase==2)
        {
            auto* Fixture=GetWorld()->SpawnActor<AStaticMeshActor>(Pet->GetActorLocation()+FVector(0,90,0),FRotator::ZeroRotator,P);RecoveryWall=Fixture;
            auto* WallMesh=Fixture->GetStaticMeshComponent();WallMesh->SetMobility(EComponentMobility::Movable);
            WallMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Fixture->SetActorScale3D(FVector(4,.15f,3));WallMesh->SetCollisionProfileName(TEXT("BlockAll"));
        }
        Pet->State=EHeliosPetState::Following;Pet->SetActorTickEnabled(false);
        if(RecoveryCase==0){Pet->GetCharacterMovement()->Velocity=FVector(200,0,0);Pet->SetPose(TEXT("Run"));Pet->GetCharacterMovement()->Velocity=FVector::ZeroVector;}
        else if(RecoveryCase==1)Pet->SetPose(TEXT("Guard"));
        else {Pet->Ammo=0;Pet->ReloadAge=.55f;Pet->SetPose(TEXT("Reload"),false);}
        Stage=51;Age=0;return;
    }
    if(Stage>=51&&Stage<=53)
    {
        const FVector Offset=RecoveryCase==0?FVector(450,0,100):RecoveryCase==1?FVector(0,-510,100):FVector(380,-420,130);
        CameraView(Offset,FVector(0,0,-35));
        if(Stage==51)
        {
            if(Age<.6f)return;
            Pet->Sleep();Pet->SetActorTickEnabled(true);Stage=52;Age=0;return;
        }
        if(Stage==52)
        {
            if(Age<1.5f)return;
            Check(FString::Printf(TEXT("recovery_case_%d_clears_attack"),RecoveryCase),Pet->AttackAge<0&&Pet->RifleFX->GetLiveCount()==0);
            const FVector Head=Pet->GetMesh()->GetSocketLocation(TEXT("M-bone-Head"));
            Check(FString::Printf(TEXT("recovery_case_%d_lying_low"),RecoveryCase),Head.Z<Pet->GetActorLocation().Z-35);
            if(RecoveryCase==1)Check(TEXT("lying_mesh_follows_slope"),FVector::DotProduct(Pet->GetMesh()->GetUpVector(),RecoveryFloor->GetActorUpVector())>.995f);
            Capture(FString::Printf(TEXT("recovery_%d_supine"),RecoveryCase));
            Check(TEXT("rifle_settled_beside_body"),Pet->IsGunDropped()&&Pet->Rifle->GetComponentLocation().Z<Pet->GetActorLocation().Z-50&&FVector::Dist2D(Pet->Rifle->GetComponentLocation(),Pet->GetActorLocation())>30);Pet->SleepRemaining=.3f;Stage=53;Age=0;return;
        }
        if(Pet->IsSleeping()||Pet->State==EHeliosPetState::Waking)
        {
            if(Pet->GetVelocity().SizeSquared()>1||Pet->AttackAge>=0)Check(TEXT("recovery_case_no_early_actions"),false);
            return;
        }
        Check(FString::Printf(TEXT("recovery_case_%d_finished_standing"),RecoveryCase),Pet->Health==Pet->Settings.MaxHealth&&Pet->GetCharacterMovement()->MovementMode==MOVE_Walking);
        if(RecoveryFloor){RecoveryFloor->Destroy();RecoveryFloor=nullptr;}if(RecoveryWall){RecoveryWall->Destroy();RecoveryWall=nullptr;}
        Pet->SetActorLocation(RecoveryOrigin);Pet->GetCharacterMovement()->StopMovementImmediately();
        if(++RecoveryCase<3){Stage=50;Age=0;return;}
        Check(TEXT("pickup_completed_and_weapon_held"),!Pet->IsGunDropped());
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Enemy=GetWorld()->SpawnActor<AHeliosEnemyActor>(RecoveryOrigin+FVector(1200,0,-22),FRotator(0,180,0),Params);
        Enemy->CurrentHP=10000;Enemy->SetActorTickEnabled(false);Enemy->GetCharacterMovement()->DisableMovement();Enemy->StopShooting();
        if(auto* AI=Cast<AAIController>(Enemy->GetController())){if(AI->GetBrainComponent())AI->GetBrainComponent()->StopLogic(TEXT("Rifle recovery test"));AI->StopMovement();}
        Pet->Target=Enemy;Pet->DecisionRemaining=1000;Before=Enemy->CurrentHP;Stage=54;Age=0;return;
    }
    if(Stage==54)
    {
        CameraView(FVector(500,-1600,260),FVector(520,0,20));if(Age<5)return;
        Check(TEXT("revived_pet_reloads_and_fights_again"),Enemy&&Enemy->CurrentHP<Before);
        auto* Dog=GetWorld()->SpawnActor<AHeliosPetDog>(RecoveryOrigin+FVector(300,0,0),FRotator::ZeroRotator);
        Check(TEXT("dog_assets_preserved"),Dog&&Dog->bModelReady);if(Dog)Dog->Destroy();Finish();return;
    }

    if(Stage==9)
    {
        if(Age<1)return;if(auto* Town=Director->GetTownSubsystem())if(!Town->IsWorldReady())return;
        HUD->SelectPet(EHeliosPetKind::UltimatumGR);HUD->StartSelectedRun();Stage=11;Age=0;return;
    }
    if(Stage==11)
    {
        if(Age<2)return;
        Check(TEXT("same_pet_restart_full_health"),Pets->Pet&&Pets->Pet->Kind==EHeliosPetKind::UltimatumGR&&Pets->Pet->Health==Pets->Pet->Settings.MaxHealth);
        HUD->RequestRestartConfirmation();HUD->ConfirmRestart();Stage=12;Age=0;return;
    }
    if(Stage==12)
    {
        if(Age<1)return;if(auto* Town=Director->GetTownSubsystem())if(!Town->IsWorldReady())return;
        HUD->SelectPet(EHeliosPetKind::SwordDog);Check(TEXT("menu_selects_dog"),HUD->GetSelectedPet()==EHeliosPetKind::SwordDog);HUD->StartSelectedRun();Stage=10;Age=0;return;
    }
    if(Stage==10)
    {
        if(Age<2)return;auto* Dog=Pets->Pet.Get();Check(TEXT("dog_selection_and_assets_preserved"),Dog&&Dog->Kind==EHeliosPetKind::SwordDog&&Dog->bModelReady);
        Check(TEXT("selection_does_not_share_health"),Dog&&Dog->Health==Dog->Settings.MaxHealth);int Count=0;for(TActorIterator<AHeliosPetCompanion> It(GetWorld());It;++It)++Count;Check(TEXT("restart_still_one_pet"),Count==1);
        Finish();
    }
    if(Stage==20&&Age>1)
    {
        UltimatumTravelPhase=1;Stage=21;
        auto* Travel=GetGameInstance()->GetSubsystem<UHeliosMapTravelSubsystem>();
        if(!Travel||!Travel->RequestTravel(TEXT("IkunTown"),TEXT("rifle"))){Check(TEXT("map_travel_started"),false);Finish();}
    }
    if(Stage==22&&Age>1)
    {
        TravelResults=Results;TravelFailures=Failures;UltimatumTravelPhase=2;Stage=21;
        auto* Travel=GetGameInstance()->GetSubsystem<UHeliosMapTravelSubsystem>();
        if(!Travel||!Travel->RequestTravel(TEXT("HeliosAdventure"),TEXT("rifle"))){Check(TEXT("return_travel_started"),false);Finish();}
    }
}
