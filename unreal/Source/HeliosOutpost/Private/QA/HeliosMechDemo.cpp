#include "QA/HeliosMechDemo.h"
#include "Weapons/HeliosMechWeapon.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Player/HeliosArenaCharacter.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "UI/HeliosAdventureHUD.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "EngineUtils.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "ShaderCompiler.h"
#include "TimerManager.h"

AHeliosMechDemo::AHeliosMechDemo(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void AHeliosMechDemo::Check(const TCHAR* N,bool Pass){if(!Pass)++Failures;if(!Checks.IsEmpty())Checks+=TEXT(",");Checks+=FString::Printf(TEXT("{\"name\":\"%s\",\"passed\":%s}"),N,Pass?TEXT("true"):TEXT("false"));UE_LOG(LogTemp,Display,TEXT("MECH_CHECK %s %s"),N,Pass?TEXT("PASS"):TEXT("FAIL"));}
void AHeliosMechDemo::Capture(const TCHAR* N){const FString Dir=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/mech"));IFileManager::Get().MakeDirectory(*Dir,true);FScreenshotRequest::RequestScreenshot(Dir/(FString(N)+TEXT(".png")),true,false);}
float AHeliosMechDemo::Health() const{float Sum=0;for(const auto& E:Targets)if(IsValid(E))Sum+=E->CurrentHP;return Sum;}
void AHeliosMechDemo::Finish(){FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"status\":\"%s\",\"failures\":%d,\"checks\":[%s]}"),Failures?TEXT("failed"):TEXT("passed"),Failures,*Checks),*(FPaths::ProjectDir()/TEXT("../qa/mech/runtime_report.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);Stage=99;FPlatformMisc::RequestExit(false);}
void AHeliosMechDemo::Tick(float Dt)
{
    Super::Tick(Dt);if(Stage==99)return;const double Now=FPlatformTime::Seconds();if(!Started){Started=Now;GEngine->Exec(GetWorld(),TEXT("DisableAllScreenMessages"));}
    if(Now-Started>180){Check(TEXT("timeout"),false);Finish();return;}
    Age+=Dt;if(!Player)Player=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));if(!Player)return;
    auto* PC=Cast<APlayerController>(Player->GetController());auto* HUD=PC?Cast<AHeliosAdventureHUD>(PC->GetHUD()):nullptr;
    auto* W=Cast<AHeliosMechWeapon>(Player->GetEquippedWeapon());
    if(Stage==0&&Age>1.5f){if(HUD)HUD->SelectWeapon(TEXT("mech"));Stage=1;Age=0;}
    else if(Stage==1)
    {
        if(!bCaptured&&Age>.5f){Capture(TEXT("01_menu"));bCaptured=true;}
        if(Age>1.4f){if(HUD)HUD->StartSelectedRun();else Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("mech")));Stage=2;Age=0;}
    }
    else if(Stage==2&&Age>1.4f)
    {
        Check(TEXT("menu_selectable_and_equipped"),W&&Player->IsMechMode());if(!W){Finish();return;}
        if(auto* D=AHeliosAdventureDirector::Find(GetWorld()))D->SetActorTickEnabled(false);
        for(TActorIterator<AHeliosEnemyActor> E(GetWorld());E;++E)E->Destroy();Player->SetCanBeDamaged(false);
        Ground=Player->GetGroundReference();PC->SetControlRotation(FRotator(-22,0,0));GEngine->bEnableOnScreenDebugMessages=false;
        if(auto* M=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Helios/Weapons/Mech/SM_MT_Beam")))UE_LOG(LogTemp,Display,TEXT("MECH_BEAM_BOUNDS %s"),*M->GetBounds().Origin.ToString());
        Check(TEXT("eye_height_18m"),FMath::IsNearlyEqual(Player->FirstPersonCamera->GetComponentLocation().Z-Ground.Z,1800.f,5.f));
        Check(TEXT("no_ammunition"),!W->UsesAmmunition());
        for(int I=0;I<4;++I)
        {
            FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            const FVector Spot=Ground+FVector(I<3?850:3500,I<3?(I-1)*170:0,100);
            auto* E=GetWorld()->SpawnActor<AHeliosEnemyActor>(Spot,FRotator(0,180,0),Params);E->CurrentHP=100000;E->StopShooting();E->GetCharacterMovement()->DisableMovement();E->SetActorTickEnabled(false);GetWorld()->GetTimerManager().ClearAllTimersForObject(E);
            if(auto* AI=Cast<AAIController>(E->GetController())){AI->StopMovement();if(AI->GetBrainComponent())AI->GetBrainComponent()->StopLogic(TEXT("Titan smoke"));AI->SetActorTickEnabled(false);}Targets.Add(E);
        }
        if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();Stage=3;Age=0;
        UE_LOG(LogTemp,Display,TEXT("MECH_GROUND %s"),*Ground.ToString());
    }
    else if(Stage==3&&Age>2){Capture(TEXT("02_titan_view"));Before=Health();Stage=30;Age=0;}
    else if(Stage==30)
    {
        Player->AddMovementInput(FVector::ForwardVector,1.f);
        if(Age>.65f)
        {
            Check(TEXT("high_view_walking"),FVector::Dist2D(Player->GetGroundReference(),Ground)>100&&FMath::IsNearlyEqual(Player->FirstPersonCamera->GetComponentLocation().Z-Player->GetGroundReference().Z,1800.f,25.f));
            Check(TEXT("ordinary_steps_do_not_damage"),FMath::IsNearlyEqual(Before,Health()));
            Player->ClearMovementInput();Player->GetCharacterMovement()->StopMovementImmediately();
            Player->SetActorLocation(Ground+FVector(0,0,1000),false,nullptr,ETeleportType::TeleportPhysics);
            PC->SetControlRotation(FRotator(-70,0,0));Stage=31;Age=0;
        }
    }
    else if(Stage==31&&Age>.2f)
    {
        FVector StompPoint;const bool ValidGround=W->WantsStomp(&StompPoint);
        Check(TEXT("first_stomp_has_valid_ground"),ValidGround);
        if(ValidGround)for(int32 I=0;I<3&&Targets.IsValidIndex(I);++I)
            if(IsValid(Targets[I]))Targets[I]->SetActorLocation(StompPoint+FVector((I-1)*180,0,100),false,nullptr,ETeleportType::TeleportPhysics);
        Before=Health();W->StartFiring();W->StopFiring();Stage=4;Age=0;bCaptured=false;
    }
    else if(Stage==4)
    {
        if(!bCaptured&&Age>1.05f){Capture(TEXT("03_first_stomp"));bCaptured=true;}
        if(Age>1.6f){Check(TEXT("first_stomp_damages_enemies"),Health()<Before);PC->SetControlRotation(FRotator(-70,0,0));Stage=5;Age=0;}
    }
    else if(Stage==5&&Age>.3f){Check(TEXT("look_down_selects_stomp"),W->WantsStomp());Before=Health();W->StartFiring();W->StopFiring();Stage=6;Age=0;bCaptured=false;}
    else if(Stage==6)
    {
        if(!bCaptured&&Age>1.05f){if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();Capture(TEXT("04_stomp"));bCaptured=true;}
        if(Age>1.6f){Check(TEXT("stomp_damages_enemies"),Health()<Before);PC->SetControlRotation((Targets.Last()->GetActorLocation()-Player->FirstPersonCamera->GetComponentLocation()).Rotation());Before=Health();W->StartSecondaryFire();Stage=7;Age=0;bCaptured=false;}
    }
    else if(Stage==7)
    {
        if(!bCaptured&&Age>3.0f){if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();Capture(TEXT("05_annihilation"));Check(TEXT("beam_damages_enemies"),Health()<Before);bCaptured=true;}
        if(Age>6.2f){Check(TEXT("beam_overheat_stops"),W->State==EHeliosMechState::Cooling);Before=Health();Stage=8;Age=0;}
    }
    else if(Stage==8&&Age>.5f)
    {
        Check(TEXT("no_damage_while_cooling"),FMath::IsNearlyEqual(Health(),Before));Player->CancelCombatAndControl();Check(TEXT("cancel_stops_input"),!W->bPrimaryHeld&&!W->bSecondaryHeld);
        // One isolated cover check uses the actual beam damage path.
        const FVector Probe=Ground+FVector(10000,0,10000),Saved=Targets.Last()->GetActorLocation();
        auto* Wall=GetWorld()->SpawnActor<AStaticMeshActor>();auto* Mesh=Wall->GetStaticMeshComponent();Mesh->SetMobility(EComponentMobility::Movable);Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Mesh->SetCollisionProfileName(TEXT("BlockAll"));Wall->SetActorLocation(Probe);Wall->SetActorScale3D(FVector(.5f,15,15));
        Targets.Last()->SetActorLocation(Probe+FVector(500,0,0));const float CoveredHP=Targets.Last()->CurrentHP;W->BeamOrigin=Probe-FVector(500,0,0);W->BeamTarget=Probe+FVector(1000,0,0);W->DamageBeam();
        Check(TEXT("wall_blocks_beam_damage"),FMath::IsNearlyEqual(CoveredHP,Targets.Last()->CurrentHP));Wall->Destroy();Targets.Last()->SetActorLocation(Saved);
        const bool Primary=Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("cfm_chan_zeus")));
        Check(TEXT("switch_restores_normal_capsule_camera"),Primary&&!Player->IsMechMode()&&FMath::IsNearlyEqual(Player->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),96.f)&&Player->FirstPersonCamera->GetRelativeLocation().Z<100);
        Before=Health();Stage=9;Age=0;
    }
    else if(Stage==9&&Age>.5f)
    {
        Check(TEXT("no_lingering_beam_after_unequip"),FMath::IsNearlyEqual(Before,Health()));
        const int32 PrimaryAmmo=Player->GetAmmo();Player->StartFireFromAutomation();Player->StopFireFromAutomation();
        Check(TEXT("ordinary_weapon_fires_after_mech"),Player->GetAmmo()<PrimaryAmmo);
        Check(TEXT("reequip_mech"),Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("mech"))));Player->ResetForTest();Check(TEXT("reset_redeploys_mech"),Player->IsMechMode());Stage=10;Age=0;
    }
    else if(Stage==10&&Age>1)
    {
        W->StartSecondaryFire();Stage=11;Age=0;
    }
    else if(Stage==11&&Age>.2f)
    {
        Player->CancelCombatAndControl();UGameplayStatics::SetGamePaused(this,true);Check(TEXT("pause_cancels_charge_audio"),W->State!=EHeliosMechState::Firing&&W->State!=EHeliosMechState::Charging&&!W->LaserAudio);Stage=12;Age=0;
    }
    else if(Stage==12&&Age>.3f)
    {
        UGameplayStatics::SetGamePaused(this,false);Check(TEXT("resume_keeps_height"),Player->IsMechMode());Stage=13;Age=0;
    }
    else if(Stage==13&&Age>3.5f)
    {
        Player->SetCanBeDamaged(true);UGameplayStatics::ApplyDamage(Player,100000,PC,this,nullptr);
        Check(TEXT("death_restores_camera_and_stops_weapon"),Player->IsDead()&&!Player->IsMechMode()&&!W->LaserAudio&&Player->FirstPersonCamera->GetRelativeLocation().Z<100);
        UGameplayStatics::SetGamePaused(this,false);Player->ResetForTest();Check(TEXT("death_reset_redeploys"),Player->IsMechMode()&&!Player->IsDead());Finish();
    }
}
