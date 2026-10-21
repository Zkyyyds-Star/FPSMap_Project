#include "QA/HeliosFighterDemo.h"
#include "Weapons/HeliosFighterWeapon.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Player/HeliosArenaCharacter.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "UI/HeliosAdventureHUD.h"
#include "Town/HeliosTownSubsystem.h"
#include "World/HeliosMapTravelSubsystem.h"
#include "Engine/GameInstance.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "TimerManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "InputKeyEventArgs.h"

AHeliosFighterDemo::AHeliosFighterDemo(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void AHeliosFighterDemo::Check(const TCHAR* N,bool Pass)
{if(!Pass)++Failures;if(!Checks.IsEmpty())Checks+=TEXT(",");Checks+=FString::Printf(TEXT("{\"name\":\"%s\",\"passed\":%s}"),N,Pass?TEXT("true"):TEXT("false"));UE_LOG(LogTemp,Display,TEXT("VX_CHECK %s %s"),N,Pass?TEXT("PASS"):TEXT("FAIL"));}
void AHeliosFighterDemo::Capture(const FString& N)
{
    if(FParse::Param(FCommandLine::Get(),TEXT("VXRecord"))&&!N.StartsWith(TEXT("frames/")))return;
    FString Variant;FParse::Value(FCommandLine::Get(),TEXT("VXVariant="),Variant);if(Variant.IsEmpty())Variant=TEXT("1080p");
    if(FParse::Param(FCommandLine::Get(),TEXT("VXTravel")))Variant/=GetWorld()->GetMapName();
    const FString Dir=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/fighter")/Variant);
    IFileManager::Get().MakeDirectory(*Dir,true);
    if(N.StartsWith(TEXT("frames/")))FFileHelper::SaveStringToFile(FString::Printf(TEXT("%s,%.6f\n"),*N,GetWorld()->GetTimeSeconds()),*(Dir/TEXT("timing.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
    FScreenshotRequest::RequestScreenshot(Dir/(N+TEXT(".png")),true,false);
}
void AHeliosFighterDemo::Finish()
{
    FString Variant;FParse::Value(FCommandLine::Get(),TEXT("VXVariant="),Variant);if(Variant.IsEmpty())Variant=TEXT("1080p");
    if(FParse::Param(FCommandLine::Get(),TEXT("VXTravel")))Variant/=GetWorld()->GetMapName();
    const FString Dir=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/fighter")/Variant);IFileManager::Get().MakeDirectory(*Dir,true);
    auto P95=[](TArray<float> A){if(A.IsEmpty())return 0.f;A.Sort();return A[FMath::Clamp(FMath::FloorToInt(A.Num()*.95f),0,A.Num()-1)]*1000;};
    const FString Report=FString::Printf(TEXT("{\"failures\":%d,\"checks\":[%s],\"frames\":%d,\"peak_effects\":%d,\"idle_p95_ms\":%.3f,\"combat_p95_ms\":%.3f}"),Failures,*Checks,Frame,PeakEffects,P95(IdleFrames),P95(CombatFrames));FFileHelper::SaveStringToFile(Report,*(Dir/TEXT("runtime_report.json")));
    UE_LOG(LogTemp,Display,TEXT("VX_QA_COMPLETE failures=%d"),Failures);
    if(Failures==0&&FParse::Param(FCommandLine::Get(),TEXT("VXTravel")))
    {
        const FString Map=GetWorld()->GetMapName();const FName Next=Map.EndsWith(TEXT("HeliosAdventure"))?FName(TEXT("ikun_town")):Map.EndsWith(TEXT("IkunTown"))?FName(TEXT("ikun_ghost_village")):NAME_None;
        if(!Next.IsNone())
        {
            // Leave live weapons/effects behind deliberately; destination must be clean.
            if(auto* W=Cast<AHeliosFighterWeapon>(Player->GetEquippedWeapon())){W->StartFiring();W->StartSecondaryFire();}
            Stage=20;Age=0;
            if(GetGameInstance()->GetSubsystem<UHeliosMapTravelSubsystem>()->RequestTravel(Next,TEXT("fighter")))return;
            Check(TEXT("map_travel_request"),false);
        }
    }
    FPlatformMisc::RequestExit(false);
}
void AHeliosFighterDemo::Tick(float Dt)
{
    Super::Tick(Dt);Age+=Dt;Total+=Dt;if(Total>120){Check(TEXT("timeout"),false);Finish();return;}
    if(!Player)Player=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));if(!Player)return;
    auto* PC=Cast<APlayerController>(Player->GetController());auto* HUD=PC?Cast<AHeliosAdventureHUD>(PC->GetHUD()):nullptr;
    auto* W=Cast<AHeliosFighterWeapon>(Player->GetEquippedWeapon());
    const bool FXDemo=FParse::Param(FCommandLine::Get(),TEXT("VXFXDemo"));
    if(W&&W->Art)PeakEffects=FMath::Max(PeakEffects,W->Art->ActiveEffectCount());
    if(Stage==33)IdleFrames.Add(Dt);if(Stage==34)CombatFrames.Add(Dt);
    if(FParse::Param(FCommandLine::Get(),TEXT("VXRecord"))&&((Stage>=3&&Stage<=8)||(Stage>=30&&Stage<=32)))
    {RecordClock+=Dt;if(RecordClock>=1.f/30){RecordClock=0;Capture(FString::Printf(TEXT("frames/vx_%05d"),Frame++));}}
    if(Stage==0&&Age>2)
    {
        if(auto* T=GetWorld()->GetSubsystem<UHeliosTownSubsystem>();T&&T->IsTownWorld()&&!T->IsWorldReady())return;
        if(HUD)HUD->SelectWeapon(TEXT("fighter"));Capture(TEXT("01_menu"));Stage=1;Age=0;
    }
    else if(Stage==1&&Age>1){if(HUD)HUD->StartSelectedRun();Stage=2;Age=0;}
    else if(Stage==2&&Age>2)
    {
        if(!W||!W->bActive){if(Age>25){Check(TEXT("menu_deployment"),false);Finish();}return;}
        if(FParse::Param(FCommandLine::Get(),TEXT("VXQuickStart"))){Destroy();return;}
        Check(TEXT("menu_deployment"),Player->IsFighterMode()&&Player->GetCharacterMovement()->MovementMode==MOVE_Flying);
        Check(TEXT("real_cockpit_asset"),W->Art->Cockpit&&W->Art->Cockpit->GetStaticMesh());
        int Weapons=0,Projectiles=0;for(TActorIterator<AHeliosFighterWeapon> I(GetWorld());I;++I)if(!I->IsActorBeingDestroyed())++Weapons;
        for(TActorIterator<AHeliosFighterMissile> I(GetWorld());I;++I)if(!I->IsActorBeingDestroyed())++Projectiles;
        Check(TEXT("one_fighter_no_stale_projectiles"),Weapons==1&&Projectiles==0);
        if(auto* D=AHeliosAdventureDirector::Find(GetWorld()))D->SetActorTickEnabled(false);
        for(TActorIterator<AHeliosEnemyActor> E(GetWorld());E;++E)E->Destroy();
        Player->SetCanBeDamaged(false);Start=Player->GetActorLocation();PC->SetControlRotation(FRotator(0,0,0));
        GEngine->bEnableOnScreenDebugMessages=false;W->Flight->ForwardRightUp.Z=1;Stage=3;Age=0;
    }
    else if(Stage==3&&Age>2.5f)
    {
        Check(TEXT("vertical_takeoff"),Player->GetActorLocation().Z>Start.Z+1700);W->Flight->ClearInput();Stage=4;Age=0;
    }
    else if(Stage==4&&Age>1.5f)
    {
        Check(TEXT("hover_brakes"),Player->GetVelocity().Size()<2);Capture(TEXT("02_cockpit"));
        if(FParse::Param(FCommandLine::Get(),TEXT("VXViewOnly"))||FParse::Param(FCommandLine::Get(),TEXT("VXTravel"))){Stage=19;Age=0;return;}
        PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),EKeys::W,IE_Pressed,1.f,false,FPlatformTime::Cycles64()));Stage=5;Age=0;
    }
    else if(Stage==5&&Age>1.8f)
    {
        Check(TEXT("forward_speed"),FMath::IsNearlyEqual(float(Player->GetVelocity().Size()),W->Settings->ForwardSpeed,70));PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),EKeys::W,IE_Released,0.f,false,FPlatformTime::Cycles64()));W->Flight->ClearInput();Stage=6;Age=0;
    }
    else if(Stage==6&&Age>1.3f)
    {
        const FVector Eye=Player->FirstPersonCamera->GetComponentLocation();FVector At=Eye+FVector(2500,0,0);
        FCollisionQueryParams Q(SCENE_QUERY_STAT(VXDemoFloor),false,Player);Q.AddIgnoredActor(W);bool Found=false;
        for(float Distance:{3500.f,2200.f,4500.f})
        {
            for(int I=0;I<16&&!Found;++I)
            {
                const float A=I*UE_TWO_PI/16;FVector Probe=Eye+FVector(FMath::Cos(A)*Distance,FMath::Sin(A)*Distance,0);FHitResult H;
                if(!GetWorld()->LineTraceSingleByChannel(H,Probe+FVector(0,0,8000),Probe-FVector(0,0,12000),ECC_Visibility,Q)||H.ImpactNormal.Z<.55f)continue;
                Probe=H.ImpactPoint+FVector(0,0,100);const FRotator Facing=(Probe-Eye).Rotation();
                bool Visible=W->ClearSight(Eye,Probe);
                for(int Side:{-1,1}){const FVector Barrel=Player->GetActorLocation()+Facing.RotateVector(FVector(225,Side*163,-22));Visible&=W->ClearSight(Eye,Barrel)&&W->ClearSight(Barrel,Probe);}
                if(Visible){At=Probe;Found=true;}
            }if(Found)break;
        }
        FActorSpawnParameters S;S.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Target=GetWorld()->SpawnActor<AHeliosEnemyE01>(At,FRotator(0,180,0),S);Target->CurrentHP=10000;Target->GetCharacterMovement()->DisableMovement();Target->StopShooting();Target->SetActorTickEnabled(false);GetWorld()->GetTimerManager().ClearAllTimersForObject(Target);
        if(auto* AI=Cast<AAIController>(Target->GetController())){AI->StopMovement();if(AI->GetBrainComponent())AI->GetBrainComponent()->StopLogic(TEXT("VX QA"));}
        PC->SetControlRotation((At-Player->FirstPersonCamera->GetComponentLocation()).Rotation());BeforeHealth=Target->CurrentHP;Stage=7;Age=0;
    }
    else if(Stage==7&&Age>1)
    {
        Check(TEXT("visible_enemy_locks"),W->LockCandidate.Get()==Target&&W->LockProgress>=W->Settings->LockSeconds);
        W->StartFiring();W->StartSecondaryFire();W->StopSecondaryFire();BeforeCount=W->MissileCount;W->StartSecondaryFire();W->StopSecondaryFire();Check(TEXT("missile_cooldown"),W->MissileCount==BeforeCount);Stage=8;Age=0;
    }
    else if(Stage==8)
    {
        if(!bShot&&Age>.6f){Capture(TEXT("03_attack"));bShot=true;}
        if(FXDemo&&Age>2.7f&&Age<3.2f&&W->MissileRemaining<=0){W->StartSecondaryFire();W->StopSecondaryFire();}
        if(Age>5.3f){Check(TEXT("combat_damages_enemy"),Target->CurrentHP<BeforeHealth-100);Check(TEXT("laser_overheat"),W->CoolRemaining>0);W->StopFiring();Stage=9;Age=0;}
    }
    else if(Stage==9&&Age>2.4f)
    {
        Check(TEXT("overheat_recovers"),W->CoolRemaining==0&&W->Heat<.05f);Capture(TEXT("04_flight"));
        if(FXDemo)
        {
            PC->SetControlRotation(FRotator(18,25,0));Target->SetActorLocation(Player->FirstPersonCamera->GetComponentLocation()+PC->GetControlRotation().Vector()*8000);BeforeHealth=Target->CurrentHP;
            W->CancelInput();Stage=30;Age=0;return;
        }
        W->StartFiring();BeforeCount=W->LaserCount;Player->CancelCombatAndControl();UGameplayStatics::SetGamePaused(this,true);Stage=10;Age=0;
    }
    else if(Stage==30&&Age>1)
    {Check(TEXT("air_target_locked"),W->LockCandidate.Get()==Target&&W->LockProgress>=W->Settings->LockSeconds);W->StartSecondaryFire();W->StopSecondaryFire();Stage=31;Age=0;}
    else if(Stage==31)
    {
        const float Times[]={.08f,.22f,.5f,1.07f,1.5f};const TCHAR* Names[]={TEXT("05_ejection"),TEXT("06_ignition"),TEXT("07_smoke_trail"),TEXT("08_air_explosion"),TEXT("09_air_smoke")};
        if(FXCapture<5&&Age>Times[FXCapture]){Capture(Names[FXCapture]);++FXCapture;}
        if(Age>3.2f){Check(TEXT("fast_guided_missile_hits_air_target"),Target->CurrentHP<BeforeHealth-150);Target->Destroy();PC->SetControlRotation(FRotator(12,-25,0));W->StartSecondaryFire();W->StopSecondaryFire();Stage=32;Age=0;}
    }
    else if(Stage==32&&Age>3)
    {Check(TEXT("unlocked_missile_launch"),!W->LockCandidate.IsValid());W->ClearProjectiles();W->Art->Clear();Stage=33;Age=0;}
    else if(Stage==33&&Age>3)
    {Stage=34;Age=0;W->StartFiring();}
    else if(Stage==34)
    {
        if(W->MissileRemaining<=0){W->StartSecondaryFire();W->StopSecondaryFire();}
        const float Duration=FParse::Param(FCommandLine::Get(),TEXT("VXStress"))?60.f:5.f;
        if(Age>Duration){Check(TEXT("vfx_pool_bounded"),PeakEffects<=192);W->CancelInput();W->ClearProjectiles();W->Art->Clear();Check(TEXT("vfx_explicit_cleanup"),W->Art->ActiveEffectCount()==0);W->StartFiring();BeforeCount=W->LaserCount;Player->CancelCombatAndControl();UGameplayStatics::SetGamePaused(this,true);Stage=10;Age=0;}
    }
    else if(Stage==10&&Age>.5f)
    {Check(TEXT("pause_stops_fire"),W->LaserCount==BeforeCount);UGameplayStatics::SetGamePaused(this,false);Stage=11;Age=0;}
    else if(Stage==11&&Age>.3f)
    {
        Check(TEXT("resume_does_not_stick_trigger"),W->LaserCount==BeforeCount);Player->ResetForTest();Check(TEXT("restart_redeploys"),W->bActive&&Player->IsFighterMode());
        int Missiles=0;for(TActorIterator<AHeliosFighterMissile> M(GetWorld());M;++M)if(!M->IsActorBeingDestroyed())++Missiles;Check(TEXT("restart_clears_missiles"),Missiles==0);Check(TEXT("restart_clears_vfx"),W->Art->ActiveEffectCount()==0);
        Stage=12;Age=0;
    }
    else if(Stage==12&&Age>4)
    {
        Player->SetCanBeDamaged(true);UGameplayStatics::ApplyDamage(Player,100000,PC,this,nullptr);Check(TEXT("death_restores_state"),Player->IsDead()&&!Player->IsFighterMode()&&!W->bActive);
        UGameplayStatics::SetGamePaused(this,false);Player->ResetForTest();Check(TEXT("death_reset_redeploys"),!Player->IsDead()&&Player->IsFighterMode());Stage=13;Age=0;
    }
    else if(Stage==13&&Age>.8f)
    {
        if(auto* D=AHeliosAdventureDirector::Find(GetWorld()))D->EnterWeaponSelection();Check(TEXT("menu_cleans_fighter"),!W->bActive&&!Player->IsFighterMode());
        UGameplayStatics::SetGamePaused(this,false);
        const bool Equipped=AHeliosAdventureDirector::Find(GetWorld())
            ?AHeliosAdventureDirector::Find(GetWorld())->StartRunWithLoadout(FHeliosLoadout::MakeDefault())
            :Player->EquipLoadout(FHeliosLoadout::MakeDefault());
        Check(TEXT("equips_default_primary"),Equipped&&Player->GetWeaponId()==TEXT("cfm_chan_zeus")&&!Player->IsFighterMode());
        BeforeCount=Player->GetAmmo();Player->StartFireFromAutomation();Stage=14;Age=0;
    }
    else if(Stage==14&&Age>.6f)
    {Player->StopFireFromAutomation();Check(TEXT("primary_regression"),Player->GetWeaponId()==TEXT("cfm_chan_zeus")&&Player->GetAmmo()<BeforeCount);Stage=19;Age=0;}
    else if(Stage==19&&Age>1)Finish();
}
