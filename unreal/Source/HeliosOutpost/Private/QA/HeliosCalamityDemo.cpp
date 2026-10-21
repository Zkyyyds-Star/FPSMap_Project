#include "QA/HeliosCalamityDemo.h"
#include "Weapons/HeliosCalamityStaff.h"
#include "Weapons/HeliosCalamityWind.h"
#include "Combat/CombatDamageSettings.h"
#include "DynamicRHI.h"
#include "RenderTimer.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Weapons/HeliosFirstPersonPresentationComponent.h"
#include "Player/HeliosArenaCharacter.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "UI/HeliosAdventureHUD.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Boss/HeliosEnemyBoss.h"
#include "Pets/HeliosPetDog.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "ShaderCompiler.h"
#include "TimerManager.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Audio/HeliosMusicSubsystem.h"
#include "Components/AudioComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "DrawDebugHelpers.h"
#include "Engine/GameInstance.h"
#include "Async/Async.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Modules/ModuleManager.h"

namespace { FString DemoDir(){if(FParse::Param(FCommandLine::Get(),TEXT("CSWindBenchmark")))return FParse::Param(FCommandLine::Get(),TEXT("CSWindLegacy"))?TEXT("../qa/wind_benchmark_legacy"):TEXT("../qa/wind_benchmark_new");if(FParse::Param(FCommandLine::Get(),TEXT("CSWind")))return TEXT("../qa/calamity_wind");if(FParse::Param(FCommandLine::Get(),TEXT("CSRange")))return TEXT("../qa/calamity_range");if(FParse::Param(FCommandLine::Get(),TEXT("CSImpacts")))return TEXT("../qa/calamity_impacts");return FParse::Param(FCommandLine::Get(),TEXT("CSProjectiles"))?TEXT("../qa/calamity_projectiles"):TEXT("../qa/calamity_v2");} }
AHeliosCalamityDemo::AHeliosCalamityDemo(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;PendingWrites=MakeShared<FThreadSafeCounter,ESPMode::ThreadSafe>();FailedWrites=MakeShared<FThreadSafeCounter,ESPMode::ThreadSafe>();}
void AHeliosCalamityDemo::Check(const FString& N,bool Pass){if(!Pass)++Failures;if(!Results.IsEmpty())Results+=TEXT(",");Results+=FString::Printf(TEXT("{\"name\":\"%s\",\"passed\":%s}"),*N,Pass?TEXT("true"):TEXT("false"));UE_LOG(LogTemp,Display,TEXT("CS_CHECK %s %s"),*N,Pass?TEXT("PASS"):TEXT("FAIL"));}
void AHeliosCalamityDemo::Capture(const FString& N)
{
    if(FScreenshotRequest::IsScreenshotRequested()||PendingWrites->GetValue()>=3)return;
    PendingFrame=N.StartsWith(TEXT("frames/"))?FCString::Atoi(*N.Right(5)):-1;
    PendingCapturePath=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/DemoDir()/(N+(PendingFrame>=0?TEXT(".jpg"):TEXT(".png"))));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(PendingCapturePath),true);FScreenshotRequest::RequestScreenshot(PendingCapturePath,true,false);
}
void AHeliosCalamityDemo::OnFrameCaptured(int32 Width,int32 Height,const TArray<FColor>& Pixels)
{
    if(PendingCapturePath.IsEmpty())return;
    const FString Path=PendingCapturePath;PendingCapturePath.Empty();
    if(PendingFrame>=0)CaptureTimeline+=FString::Printf(TEXT("%d,%.6f\n"),PendingFrame,FPlatformTime::Seconds()-RecordingAt);
    auto Counter=PendingWrites,Errors=FailedWrites;Counter->Increment();TArray<FColor> Copy=Pixels;
    const auto Format=PendingFrame>=0?EImageFormat::JPEG:EImageFormat::PNG;
    Async(EAsyncExecution::ThreadPool,[Width,Height,Path,Format,Copy=MoveTemp(Copy),Counter,Errors]()
    {
        auto& Module=FModuleManager::GetModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));auto Wrapper=Module.CreateImageWrapper(Format);
        bool Ok=Wrapper.IsValid()&&Wrapper->SetRaw(Copy.GetData(),Copy.Num()*sizeof(FColor),Width,Height,ERGBFormat::BGRA,8);
        if(Ok){const auto& Bytes=Wrapper->GetCompressed(Format==EImageFormat::JPEG?88:0);Ok=FFileHelper::SaveArrayToFile(Bytes,*Path);}if(!Ok)Errors->Increment();Counter->Decrement();
    });
}
void AHeliosCalamityDemo::StopRecording()
{
    if(bAudioRecording){bAudioRecording=false;UAudioMixerBlueprintLibrary::StopRecordingOutput(this,EAudioRecordingExportType::WavFile,TEXT("game_mix"),FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/DemoDir()));}
    FFileHelper::SaveStringToFile(CaptureTimeline,*(FPaths::ProjectDir()/(DemoDir()/TEXT("frame_timing.csv"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
void AHeliosCalamityDemo::EndPlay(EEndPlayReason::Type Reason){UGameViewportClient::OnScreenshotCaptured().RemoveAll(this);Super::EndPlay(Reason);}
AHeliosEnemyActor* AHeliosCalamityDemo::Target(FVector Offset,bool Boss)
{
    FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* E=GetWorld()->SpawnActor<AHeliosEnemyActor>(Boss?AHeliosEnemyBoss::StaticClass():AHeliosEnemyActor::StaticClass(),Start+Offset,FRotator(0,180,0),P);
    E->CurrentHP=100000;E->SetActorTickEnabled(false);E->GetCharacterMovement()->DisableMovement();E->GetCharacterMovement()->SetComponentTickEnabled(false);E->StopShooting();GetWorld()->GetTimerManager().ClearAllTimersForObject(E);
    if(auto* AI=Cast<AAIController>(E->GetController())){AI->StopMovement();if(AI->GetBrainComponent())AI->GetBrainComponent()->StopLogic(TEXT("Calamity showcase"));AI->SetActorTickEnabled(false);}Targets.Add(E);return E;
}
void AHeliosCalamityDemo::Finish()
{
    if(!bFinishing){bFinishing=true;FinishRequestedAt=FPlatformTime::Seconds();StopRecording();Stage=98;return;}
    const int64 AudioBytes=IFileManager::Get().FileSize(*(FPaths::ProjectDir()/(DemoDir()/TEXT("game_mix.wav"))));
    if((PendingWrites->GetValue()>0||(bCapture&&AudioBytes<=44))&&FPlatformTime::Seconds()-FinishRequestedAt<15)return;
    if(bCapture)Check(TEXT("recorded_game_audio_and_frames_saved"),AudioBytes>44&&PendingWrites->GetValue()==0&&FailedWrites->GetValue()==0);
    double Sum=0;for(double T:FrameTimes)Sum+=T;FrameTimes.Sort();
    const FString Report=FString::Printf(TEXT("{\"status\":\"%s\",\"failures\":%d,\"capture_frames\":%d,\"frame_mean_ms\":%.3f,\"frame_p95_ms\":%.3f,\"capture_enabled\":%s,\"checks\":[%s]}"),Failures?TEXT("failed"):TEXT("passed"),Failures,Frame,FrameTimes.Num()?Sum/FrameTimes.Num():0.,FrameTimes.Num()?FrameTimes[FMath::Min(FrameTimes.Num()-1,FMath::FloorToInt(FrameTimes.Num()*.95))]:0.,bCapture?TEXT("true"):TEXT("false"),*Results);
    const FString File=DemoDir()/(FParse::Param(FCommandLine::Get(),TEXT("CSVerifyPoison"))?TEXT("poison_recheck.json"):TEXT("runtime_report.json"));
    FFileHelper::SaveStringToFile(Report,*(FPaths::ProjectDir()/File),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);Stage=999;FPlatformMisc::RequestExit(false);
}
void AHeliosCalamityDemo::Tick(float Dt)
{
    Super::Tick(Dt);if(Stage==999)return;if(Stage==98){Finish();return;}const double Now=FPlatformTime::Seconds();
    if(!Started){Started=Now;bCapture=FParse::Param(FCommandLine::Get(),TEXT("CSCapture"));FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));UGameViewportClient::OnScreenshotCaptured().AddUObject(this,&AHeliosCalamityDemo::OnFrameCaptured);}
    if(Now-Started>420){Check(TEXT("runtime_timeout"),false);Finish();return;}
    const double Ms=LastFrame?(Now-LastFrame)*1000:0;LastFrame=Now;
    if(!Player)Player=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));if(!Player)return;
    auto* PC=Cast<APlayerController>(Player->GetController());auto* HUD=PC?Cast<AHeliosAdventureHUD>(PC->GetHUD()):nullptr;
    auto* S=UHeliosCalamityState::Get(Player);auto* W=Cast<AHeliosCalamityStaff>(Player->GetEquippedWeapon());Age+=Dt;
    if(Stage==30){TickWindGallery(Dt,Ms);return;}
    if(Stage==20){TickRangeGallery(Dt);return;}
    if(bCapture&&Stage>=3&&Stage<=6){if(!bAudioRecording&&RecordingAt==0){RecordingAt=Now;bAudioRecording=true;UAudioMixerBlueprintLibrary::StartRecordingOutput(this,90);}CaptureAge+=Dt;if(CaptureAge>=.05f&&PendingWrites->GetValue()<3&&!FScreenshotRequest::IsScreenshotRequested()){CaptureAge=0;Capture(FString::Printf(TEXT("frames/frame_%05d"),Frame++));}}
    if(Stage==9&&Age>1)FrameTimes.Add(Ms);
    if(Stage>=7&&bAudioRecording)StopRecording();
    if(Stage==0){if(HUD)HUD->SelectWeapon(TEXT("calamity"));Stage=1;Age=0;return;}
    if(Stage==1&&Age>.6f){Capture(TEXT("01_menu"));if(HUD)HUD->StartSelectedRun();else Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("calamity")));Stage=2;Age=0;return;}
    if(Stage==2&&Age>1)
    {
        if(auto* D=AHeliosAdventureDirector::Find(GetWorld()))D->SetActorTickEnabled(false);
        for(TActorIterator<AHeliosEnemyActor> E(GetWorld());E;++E)E->Destroy();
        for(TActorIterator<AHeliosPetCompanion> P(GetWorld());P;++P)P->Destroy();
        Player->SetCanBeDamaged(false);Player->GetCharacterMovement()->StopMovementImmediately();Player->GetCharacterMovement()->DisableMovement();
        Start=Player->GetActorLocation()+FVector(0,0,10000);Player->SetActorLocation(Start);PC->SetControlRotation(FRotator(-5,0,0));
        auto* Floor=GetWorld()->SpawnActor<AStaticMeshActor>();auto* Mesh=Floor->GetStaticMeshComponent();Mesh->SetMobility(EComponentMobility::Movable);Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Mesh->SetCollisionProfileName(TEXT("BlockAll"));Floor->SetActorLocation(Start+FVector(1000,0,-115));Floor->SetActorScale3D(FVector(70,70,.5f));
        Check(TEXT("menu_selectable_and_equipped"),W&&Player->GetWeaponId()==TEXT("calamity"));if(!W){Finish();return;}
        Check(TEXT("first_person_mesh_and_hands_loaded"),Player->GetPresentation()&&Player->GetPresentation()->IsReady());Check(TEXT("unlimited_no_reload"),!W->UsesAmmunition());
        if(auto* Music=GetGameInstance()->GetSubsystem<UHeliosMusicSubsystem>())if(auto* Audio=Music->GetMusicComponent())Audio->SetVolumeMultiplier(0);
        for(int32 I=0;I<4;++I){S->CastSpell(static_cast<EHeliosCalamitySpell>(I),Start+FVector(600,0,100),Start+FVector(600,0,-80));}S->Clear();
        W->ForcedSpell=0;W->StartFiring();W->StopFiring();if(W->Channel)W->Channel->Cancel();S->CastReadyAt=0;
        if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
        if(FParse::Param(FCommandLine::Get(),TEXT("CSWind")))
        {
            RunRangeChecks();RunWindChecks();
            if(FParse::Param(FCommandLine::Get(),TEXT("CSWindLegacy")))S->Settings->WindPresentation=nullptr;
            for(int32 I=0;I<4;++I)Target(FVector(1250+(I%2)*250,(I-1.5f)*220,0));
            Stage=30;Age=0;bAction=false;return;
        }
        if(FParse::Param(FCommandLine::Get(),TEXT("CSRange"))){RunRangeChecks();RunProjectileChecks();RunImpactChecks();}
        for(int32 I=0;I<4;++I)Target(FVector(1000+(I%2)*160,(I-1.5f)*220,0));
        if(FParse::Param(FCommandLine::Get(),TEXT("CSProjectiles"))){RunProjectileChecks();if(FParse::Param(FCommandLine::Get(),TEXT("CSImpacts")))RunImpactChecks();SpellIndex=1;for(int32 I=0;I<Targets.Num();++I)Targets[I]->SetActorLocation(Start+FVector(2200+(I%2)*160,(I-1.5f)*220,0));}
        Stage=FParse::Param(FCommandLine::Get(),TEXT("CSRange"))?20:FParse::Param(FCommandLine::Get(),TEXT("CSVerifyPoison"))?7:3;Age=0;return;
    }
    if(Stage==3&&Age>2){Capture(TEXT("02_staff"));Stage=4;Age=0;bAction=false;return;}
    if(Stage==4)
    {
        if(!bAction){HealthBefore=Targets[1]->GetHealth();W->ForcedSpell=SpellIndex;S->CastReadyAt=0;PC->SetControlRotation((Targets[1]->GetActorLocation()-Player->GetActorLocation()).Rotation()+FRotator(-4,0,0));Player->StartFireFromAutomation();Player->StopFireFromAutomation();Age=0;PhaseChecks=0;bAction=true;Check(FString::Printf(TEXT("spell_%d_starts_with_charge"),SpellIndex),W->Channel&&W->Channel->bCharging&&S->Effects.IsEmpty());}
        if(Age>.65f&&Age<1.f&&!(PhaseChecks&1)&&(SpellIndex==0||SpellIndex==2)){Check(FString::Printf(TEXT("spell_%d_telegraph_has_no_damage"),SpellIndex),Targets[1]->GetHealth()==HealthBefore&&S->Effects.Num()>0&&S->Effects[0]->Phase==EHeliosCalamityPhase::Telegraph);PhaseChecks|=1;}
        if(Age>2&&Age<2+Dt)Capture(FString::Printf(TEXT("spell_%d"),SpellIndex));
        if(Age>8.5f){Check(FString::Printf(TEXT("spell_%d_area_damage"),SpellIndex),Targets[1]->GetHealth()<HealthBefore);if(SpellIndex==0)Check(TEXT("earthquake_exactly_three_pulses"),FMath::Abs(HealthBefore-Targets[1]->GetHealth()-300)<.1f);if(SpellIndex==2)Check(TEXT("tornado_twenty_existing_100_damage_pulses"),FMath::Abs(HealthBefore-Targets[1]->GetHealth()-2000)<.1f);S->Clear();SpellIndex+=FParse::Param(FCommandLine::Get(),TEXT("CSProjectiles"))?2:1;Age=0;bAction=false;if(SpellIndex>=4){if(FParse::Param(FCommandLine::Get(),TEXT("CSImpacts"))&&ImpactPass==0){ImpactPass=1;SpellIndex=1;for(int32 I=0;I<Targets.Num();++I)Targets[I]->SetActorLocation(Start+FVector(950+(I%2)*130,(I-1.5f)*180,0));}else {W->ForcedSpell=-1;Stage=5;}}}
        return;
    }
    if(Stage==5)
    {
        if(!bAction)
        {
            for(int32 I=0;I<Targets.Num();++I)Targets[I]->SetActorLocation(Start+FVector(1100+(I%2)*160,(I-1.5f)*220,0));
            PC->SetControlRotation(FRotator(-15,12,0));Player->StartSecondaryAttack();Check(TEXT("valid_ground_deploys_tower"),S->Deployed());
            auto* Original=S->Tower.Get();Player->StartSecondaryAttack();Check(TEXT("no_duplicate_tower"),S->Tower==Original);const int32 Casts=S->CastCount;Player->StartFireFromAutomation();Player->StopFireFromAutomation();Check(TEXT("deployed_staff_cannot_cast_in_hand"),S->CastCount==Casts);
            BeforeShots=S->TowerShots;bAction=true;
        }
        if(Age>2){Capture(TEXT("07_tower"));Player->ToggleCalamityBackup();Check(TEXT("switch_preserves_tower"),S->Deployed());Stage=6;Age=0;bAction=false;}return;
    }
    if(Stage==6)
    {
        if(Age>3&&!bAction){Check(TEXT("tower_fires_while_using_other_weapon"),S->TowerShots>BeforeShots);Player->ToggleCalamityBackup();bAction=true;}
        if(FParse::Param(FCommandLine::Get(),TEXT("CSProjectiles"))&&Age>6){S->Clear();Stage=FParse::Param(FCommandLine::Get(),TEXT("CSImpacts"))?7:9;Age=0;BeforeShots=0;bAction=false;return;}
        if(Age>19.5f){Check(TEXT("automatic_return_after_twenty_seconds"),!S->Deployed());Check(TEXT("return_has_no_tower_cooldown"),S->Cooldown()==0);Capture(TEXT("08_returned"));Stage=7;Age=0;bAction=false;}return;
    }
    if(Stage==7)
    {
        if(!bAction)
        {
            S->Clear();for(auto E:Targets)E->Destroy();Targets.Reset();auto* E=Target(FVector(500,0,0));
            const FVector Center=Start+FVector(500,0,-80);HealthBefore=E->GetHealth();
            S->CastSpell(EHeliosCalamitySpell::Poison,Center,Center);S->Effects.Last()->bField=true;S->Effects.Last()->SetActorLocation(Center);
            S->CastSpell(EHeliosCalamitySpell::Poison,Center,Center);S->Effects.Last()->bField=true;S->Effects.Last()->SetActorLocation(Center);bAction=true;
            Target(FVector(-900,0,0));const FVector Control=Start+FVector(-900,0,-80);S->CastSpell(EHeliosCalamitySpell::Poison,Control,Control);S->Effects.Last()->bField=true;S->Effects.Last()->SetActorLocation(Control);
        }
        if(Age>1){const float Damage=HealthBefore-Targets[0]->GetHealth(),ControlDamage=HealthBefore-Targets[1]->GetHealth();UE_LOG(LogTemp,Display,TEXT("CS_POISON overlapping=%.2f single=%.2f"),Damage,ControlDamage);Check(TEXT("overlapping_poison_does_not_stack"),Damage>0&&FMath::Abs(Damage-ControlDamage)<.1f);S->Clear();Targets[1]->Destroy();Targets.RemoveAt(1);if(FParse::Param(FCommandLine::Get(),TEXT("CSImpacts"))){Stage=9;Age=0;BeforeShots=0;bAction=false;return;}if(FParse::Param(FCommandLine::Get(),TEXT("CSVerifyPoison"))){Target(FVector(500,200,50),true);Stage=9;Age=0;bAction=false;return;}Stage=8;Age=0;bAction=false;}return;
    }
    if(Stage==8)
    {
        const FVector Center=Start+FVector(0,0,-80);
        if(!bAction)
        {
            Targets[0]->SetActorLocation(Start+FVector(450,0,0));HealthBefore=Targets[0]->GetHealth();
            Wall=GetWorld()->SpawnActor<AStaticMeshActor>();auto* M=Wall->GetStaticMeshComponent();M->SetMobility(EComponentMobility::Movable);M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));M->SetCollisionProfileName(TEXT("BlockAll"));Wall->SetActorLocation(Start+FVector(200,0,50));Wall->SetActorScale3D(FVector(.3f,10,5));
            S->DamageArea(Center,EHeliosCalamitySpell::Earthquake,1);Check(TEXT("wall_blocks_area_damage"),Targets[0]->GetHealth()==HealthBefore);Wall->Destroy();
            auto* Boss=Target(FVector(500,200,50),true);S->CastSpell(EHeliosCalamitySpell::Tornado,Center+FVector(0,0,200),Center);bAction=true;
        }
        if(Age>2){Check(TEXT("boss_not_pulled_by_tornado"),FVector::Dist2D(Targets[1]->GetActorLocation(),Start+FVector(500,200,0))<1);Player->ResetForTest();Check(TEXT("reset_cleans_persistent_effects"),!S->Deployed()&&S->Effects.IsEmpty());Stage=9;Age=0;bAction=false;}return;
    }
    if(Stage==9)
    {
        if(!bAction){Player->SetActorLocation(Start);Player->GetCharacterMovement()->DisableMovement();PC->SetControlRotation(FRotator(-15,12,0));S->Deploy();const FVector Center=Start+FVector(700,0,-80);for(int32 I=0;I<3;++I)S->CastSpell(FParse::Param(FCommandLine::Get(),TEXT("CSProjectiles"))?EHeliosCalamitySpell::Poison:EHeliosCalamitySpell::Tornado,Center+FVector(0,I*120,200),Center+FVector(0,I*120,0));S->CastSpell(FParse::Param(FCommandLine::Get(),TEXT("CSProjectiles"))?EHeliosCalamitySpell::Fireball:EHeliosCalamitySpell::Earthquake,Center+FVector(0,0,200),Center);bAction=true;}
        if(FParse::Param(FCommandLine::Get(),TEXT("CSProjectiles"))&&Age>1&&Age<5&&FMath::FloorToInt(Age/1.1f)>BeforeShots){BeforeShots=FMath::FloorToInt(Age/1.1f);S->CastSpell(EHeliosCalamitySpell::Fireball,Start+FVector(100,0,80),Start+FVector(1000,0,-80));}
        if(Age>6){S->Clear();if(FParse::Param(FCommandLine::Get(),TEXT("CSProjectiles"))&&!FParse::Param(FCommandLine::Get(),TEXT("CSImpacts"))){Finish();return;}Stage=10;Age=0;PhaseChecks=0;bAction=false;}return;
    }
    if(Stage==10)
    {
        if(!bAction){S->Clear();BeforeShots=S->CastCount;W->ForcedSpell=0;W->StartFiring();W->StopFiring();Check(TEXT("mouse_release_keeps_charge"),W->Channel&&W->Channel->bCharging);bAction=true;Age=0;}
        if(PhaseChecks==0&&Age>.15f){PausedChargeAge=W->Channel->Age;PauseStartedAt=Now;UGameplayStatics::SetGamePaused(this,true);PhaseChecks=1;return;}
        if(PhaseChecks==1&&Now-PauseStartedAt>.25){Check(TEXT("pause_freezes_charge"),FMath::IsNearlyEqual(W->Channel->Age,PausedChargeAge));UGameplayStatics::SetGamePaused(this,false);Player->ToggleCalamityBackup();PhaseChecks=2;Age=0;return;}
        if(PhaseChecks==2&&Age>.6f){Check(TEXT("switch_cancels_unreleased_cast"),S->CastCount==BeforeShots);Check(TEXT("cancel_preserves_cast_cooldown"),S->CastReadyAt>GetWorld()->GetTimeSeconds());Player->ToggleCalamityBackup();PhaseChecks=3;Age=0;return;}
        if(PhaseChecks==3&&Age>.4f){S->CastReadyAt=0;W->StartFiring();W->StopFiring();Player->SetCanBeDamaged(true);UGameplayStatics::ApplyDamage(Player,1000000,nullptr,nullptr,UDamageType::StaticClass());Check(TEXT("death_cleans_tower_effects_charge_and_audio"),Player->IsDead()&&!S->Deployed()&&S->Effects.IsEmpty()&&S->PlayingSounds.IsEmpty()&&!W->Channel->bCharging);Finish();}return;
    }
}

void AHeliosCalamityDemo::RunWindChecks()
{
    auto* S=UHeliosCalamityState::Get(Player);auto* P=S->Settings->WindPresentation.Get();
    Check(TEXT("wind_profile_complete"),P&&P->IsReady());if(!P||!P->IsReady())return;
    const FVector Center=Start+FVector(1200,0,-80);
    auto* Enemy=Target(FVector(1500,0,0));auto* Boss=Target(FVector(1500,230,0),true);
    const FVector BossBefore=Boss->GetActorLocation(),EnemyBefore=Enemy->GetActorLocation();
    FActorSpawnParameters Args;Args.Owner=Player;
    auto* E=GetWorld()->SpawnActor<AHeliosCalamityEffect>(Center,FRotator::ZeroRotator,Args);
    E->Initialize(S,EHeliosCalamitySpell::Tornado,Center,1);E->SetActorTickEnabled(false);S->Effects.Add(E);
    E->Tick(E->Telegraph-.01f);Check(TEXT("wind_prelude_no_damage"),Enemy->GetHealth()==100000);
    E->Tick(.02f);Check(TEXT("wind_three_blades_and_32_debris"),E->WindVisual&&E->WindVisual->Blades.Num()==3&&E->WindVisual->Debris->GetInstanceCount()==32);
    for(int32 I=0;I<20;++I)E->Tick(.2f);
    const float Damage=UCombatDamageSettings::Resolve(this,ECombatDamageType::Tornado);
    Check(TEXT("wind_twenty_original_damage_pulses"),FMath::IsNearlyEqual(100000-Enemy->GetHealth(),Damage*20,.1f));
    Check(TEXT("wind_boss_not_pulled"),FVector::Distance(BossBefore,Boss->GetActorLocation())<.1f);
    Check(TEXT("wind_normal_enemy_still_pulled"),FVector::Distance(EnemyBefore,Enemy->GetActorLocation())>100);
    Check(TEXT("wind_tail_keeps_height_and_dissolves"),E->Phase==EHeliosCalamityPhase::Dissipating&&E->WindVisual->Body->GetRelativeScale3D().Z==1);
    const float HP=Enemy->GetHealth();E->Tick(E->Tail+.05f);
    Check(TEXT("wind_tail_no_damage_and_cleans_actor"),E->IsActorBeingDestroyed()&&Enemy->GetHealth()==HP);
    S->Clear();for(auto T:Targets)if(IsValid(T))T->Destroy();Targets.Reset();
    for(int32 I=0;I<16;++I)S->CastSpell(EHeliosCalamitySpell::Tornado,Start+FVector(0,0,100),Center);
    Check(TEXT("wind_sixteen_effect_limit"),S->Effects.Num()==16&&!S->CastSpell(EHeliosCalamitySpell::Tornado,Start,Center));
    S->Clear();Check(TEXT("wind_reset_cleans_effects_and_audio"),S->Effects.IsEmpty()&&S->PlayingSounds.IsEmpty());
}

void AHeliosCalamityDemo::TickWindGallery(float Dt,double FrameMs)
{
    const bool Benchmark=FParse::Param(FCommandLine::Get(),TEXT("CSWindBenchmark"));
    const int32 Counts[]={0,1,3,16};
    auto* S=UHeliosCalamityState::Get(Player);auto* PC=Cast<APlayerController>(Player->GetController());
    auto* W=Cast<AHeliosCalamityStaff>(Player->GetEquippedWeapon());
    const FVector Center=Start+FVector(1400,0,-80);
    if(!bAction)
    {
        S->Clear();Age=0;PhaseChecks=0;
        if(Wall){Wall->Destroy();Wall=nullptr;}
        FVector Camera=Start+FVector(-550,-700,60);
        if(!Benchmark)
        {
            if(WindShot==1)Camera=Start+FVector(600,-600,20);
            if(WindShot==2)Camera=Start+FVector(-1800,-1000,450);
            if(WindShot==3)Camera=Start+FVector(-500,-1600,300);
            if(WindShot==4)Camera=Start+FVector(-400,-900,260);
        }
        Player->SetActorLocation(Camera);PC->SetControlRotation((Center+FVector(0,0,250)-Camera-FVector(0,0,64)).Rotation());
        if(!Benchmark&&WindShot>0)
        {
            Wall=GetWorld()->SpawnActor<AStaticMeshActor>();auto* M=Wall->GetStaticMeshComponent();M->SetMobility(EComponentMobility::Movable);M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));M->SetCollisionProfileName(TEXT("BlockAll"));M->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Helios/Weapons/Calamity/Wind/M_CW_Stone")));
            Wall->SetActorLocation(Start+FVector(2500,0,600));Wall->SetActorScale3D(FVector(.4f,48,18));
            if(WindShot==4){Wall->SetActorLocation(Center-FVector(0,0,25));Wall->SetActorScale3D(FVector(22,23,.4f));Wall->SetActorRotation(FRotator(14,0,0));}
        }
        if(bCapture&&RecordingAt==0){RecordingAt=FPlatformTime::Seconds();bAudioRecording=true;UAudioMixerBlueprintLibrary::StartRecordingOutput(this,60);}
        const int32 Count=Benchmark?Counts[WindShot]:(WindShot==3?3:1);
        if(!Benchmark&&WindShot==0&&W)
        {
            PC->SetControlRotation((Center-Camera-FVector(0,0,64)).Rotation());
            W->ForcedSpell=2;S->CastReadyAt=0;W->StartFiring();W->StopFiring();
        }
        else for(int32 I=0;I<Count;++I)
        {
            const FVector Point=Center+FVector(Count>3?(I%4)*110:0,Count==3?(I-1)*570:Count>3?(I/4-1.5f)*110:0,0);
            S->CastSpell(EHeliosCalamitySpell::Tornado,Camera+FVector(0,0,120),Point);
        }
        bAction=true;
    }
    if(Benchmark&&Age>1.3f&&Age<4.5f)
    {
        FrameTimes.Add(FrameMs);
        WindSamples+=FString::Printf(TEXT("%d,%.4f,%.4f,%.4f,%.4f\n"),Counts[WindShot],FrameMs,FPlatformTime::ToMilliseconds(GGameThreadTime),FPlatformTime::ToMilliseconds(GRenderThreadTime),FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
    }
    if(!Benchmark)
    {
        if(WindShot==0&&Age>.8f&&!(PhaseChecks&4)){PC->SetControlRotation((Center+FVector(0,0,250)-Player->GetActorLocation()-FVector(0,0,64)).Rotation());PhaseChecks|=4;}
        if(Age>2.2f&&!(PhaseChecks&1)){Capture(FString::Printf(TEXT("wind_%d_active"),WindShot));PhaseChecks|=1;}
        if(Age>5.1f&&!(PhaseChecks&2)){Capture(FString::Printf(TEXT("wind_%d_tail"),WindShot));PhaseChecks|=2;}
        if(bCapture){CaptureAge+=Dt;if(CaptureAge>=1.f/30&&PendingWrites->GetValue()<3&&!FScreenshotRequest::IsScreenshotRequested()){CaptureAge=0;Capture(FString::Printf(TEXT("frames/frame_%05d"),Frame++));}}
    }
    if(Age>6.3f)
    {
        S->Clear();++WindShot;bAction=false;Age=0;
        if(WindShot>=(Benchmark?4:5))
        {
            FFileHelper::SaveStringToFile(WindSamples,*(FPaths::ProjectDir()/DemoDir()/TEXT("performance.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            if(Wall){Wall->Destroy();Wall=nullptr;}Finish();
        }
    }
}

void AHeliosCalamityDemo::RunProjectileChecks()
{
    auto* S=UHeliosCalamityState::Get(Player);auto* Config=S->Settings.Get();
    const FVector Origin=Start+FVector(0,-1500,600),Aim=Origin+FVector(1700,0,0);
    for(int32 Index:{1,3}){
        const auto Type=static_cast<EHeliosCalamitySpell>(Index);const float Saved=Config->Spells[Index].FlightSpeedMultiplier;
        Check(FString::Printf(TEXT("projectile_%d_asset_multiplier_two"),Index),FMath::IsNearlyEqual(Saved,2.f));
        auto Spawn=[&](float Multiplier){Config->Spells[Index].FlightSpeedMultiplier=Multiplier;FActorSpawnParameters P;P.Owner=Player;auto* E=GetWorld()->SpawnActor<AHeliosCalamityEffect>(Origin,FRotator::ZeroRotator,P);E->Initialize(S,Type,Aim,1);E->SetActorTickEnabled(false);return E;};
        auto* Slow=Spawn(1);auto* Fast=Spawn(2);Config->Spells[Index].FlightSpeedMultiplier=Saved;
        Slow->Tick(.2f);Fast->Tick(.1f);
        Check(FString::Printf(TEXT("projectile_%d_same_path_half_time"),Index),!Slow->bField&&!Fast->bField&&FVector::Dist(Slow->GetActorLocation(),Fast->GetActorLocation())<.1f);
        if(Index==1)Check(TEXT("fire_moves_520cm_per_100ms"),FMath::Abs(FVector::Dist(Fast->GetActorLocation(),Origin)-520)<.1f);
        else {Slow->Tick(.8f);Fast->Tick(.4f);Check(TEXT("poison_same_aim_point_half_time"),FVector::Dist(Slow->GetActorLocation(),Aim)<.2f&&FVector::Dist(Fast->GetActorLocation(),Aim)<.2f);}
        Slow->Destroy();Fast->Destroy();
        auto* TestWall=GetWorld()->SpawnActor<AStaticMeshActor>();auto* Mesh=TestWall->GetStaticMeshComponent();Mesh->SetMobility(EComponentMobility::Movable);Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Mesh->SetCollisionProfileName(TEXT("BlockAll"));TestWall->SetActorLocation(Origin+FVector(150,0,0));TestWall->SetActorScale3D(FVector(.1f,5,8));
        auto* Blocked=Spawn(2);Config->Spells[Index].FlightSpeedMultiplier=Saved;Blocked->Tick(.1f);
        Check(FString::Printf(TEXT("projectile_%d_near_thin_wall_blocks_high_speed"),Index),Blocked->bField&&Blocked->GetActorLocation().X<Origin.X+150);Blocked->Destroy();TestWall->Destroy();
    }
}

void AHeliosCalamityDemo::RunImpactChecks()
{
    auto* S=UHeliosCalamityState::Get(Player);auto* Profile=S->Settings->ImpactPresentation.Get();
    Check(TEXT("impact_profile_and_resources_loaded"),Profile&&Profile->Resources.Num()>=25);if(!Profile)return;Profile->Prewarm();
    FActorSpawnParameters Params;Params.Owner=Player;auto* E=GetWorld()->SpawnActor<AHeliosCalamityEffect>(Start+FVector(2000,-1600,1500),FRotator::ZeroRotator,Params);
    E->Initialize(S,EHeliosCalamitySpell::Fireball,E->GetActorLocation()+FVector(0,0,-2000),1);E->SetActorTickEnabled(false);
    auto* Ramp=GetWorld()->SpawnActor<AStaticMeshActor>();auto* M=Ramp->GetStaticMeshComponent();M->SetMobility(EComponentMobility::Movable);M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));M->SetCollisionProfileName(TEXT("BlockAll"));Ramp->SetActorLocation(Start+FVector(2000,-1600,500));Ramp->SetActorScale3D(FVector(7,7,.4f));Ramp->SetActorRotation(FRotator(18,0,0));
    E->Tick(.3f);Check(TEXT("impact_follows_sloped_surface_normal"),E->bField&&E->bImpactGround&&FVector::DotProduct(E->GetActorUpVector(),Ramp->GetActorUpVector())>.999f);
    E->Tick(1.4f);Check(TEXT("fire_visual_residue_survives_without_extra_damage"),!E->IsActorBeingDestroyed()&&E->Phase==EHeliosCalamityPhase::Dissipating);E->Tick(2.f);Check(TEXT("fire_visual_cleanup_at_3_2_seconds"),E->IsActorBeingDestroyed());Ramp->Destroy();
    E=GetWorld()->SpawnActor<AHeliosCalamityEffect>(Start+FVector(1800,-1600,300),FRotator::ZeroRotator,Params);E->Initialize(S,EHeliosCalamitySpell::Poison,E->GetActorLocation(),1);E->SetActorTickEnabled(false);E->Impact(Start+FVector(1800,-1600,-86));
    E->Tick(6.05f);Check(TEXT("poison_visual_fade_after_six_seconds"),E->Phase==EHeliosCalamityPhase::Dissipating&&!E->IsActorBeingDestroyed());E->Tick(.8f);Check(TEXT("poison_residue_cleanup_at_6_8_seconds"),E->IsActorBeingDestroyed());
}


void AHeliosCalamityDemo::RunRangeChecks()
{
    auto* S=UHeliosCalamityState::Get(Player);
    auto* Profile=S->Settings->ImpactPresentation.Get();
    auto* WindProfile=S->Settings->WindPresentation.Get();
    const FVector Center=Start+FVector(1000,0,150);
    const float LegacyFireRadius=S->Settings->Spells[1].Radius;
    const float LegacyPullRadius=S->Settings->Spells[2].Radius;
    // Exercise both the authored impact profile and the fallback presentation.
    for(int32 Pass=0;Pass<2;++Pass)
    {
        S->Settings->ImpactPresentation=Pass==0?Profile:nullptr;
        S->Settings->WindPresentation=Pass==0?WindProfile:nullptr;
        for(int32 Index=0;Index<4;++Index)
        {
            S->Clear();
            const auto Spell=static_cast<EHeliosCalamitySpell>(Index);
            const FString Prefix=FString::Printf(TEXT("range_%s_%d_"),Pass==0?TEXT("profile"):TEXT("fallback"),Index);
            auto* Inside=Target(Center-Start+FVector(990,0,0));
            auto* Outside=Target(Center-Start+FVector(0,1010,0));
            auto* Inside3D=Target(Center-Start+FVector(-FMath::Sqrt(990.f*990-200.f*200),0,200));
            auto* Outside3D=Target(Center-Start+FVector(0,-FMath::Sqrt(1010.f*1010-200.f*200),200));
            auto* Falloff=Index==1?Target(Center-Start+FVector(400,0,0)):nullptr;
            FActorSpawnParameters P;P.Owner=Player;
            auto* E=GetWorld()->SpawnActor<AHeliosCalamityEffect>(Center,FRotator::ZeroRotator,P);
            E->Initialize(S,Spell,Center+FVector(100,0,0),1);E->SetActorTickEnabled(false);S->Effects.Add(E);
            if(Index==1||Index==3)
            {
                if(Index==1)Check(Prefix+TEXT("flight_radius_unchanged"),FMath::IsNearlyEqual(E->Parts[0]->GetRelativeScale3D().X,.32));
                E->Impact(Center-FVector(0,0,3));
            }
            else E->Tick(E->Telegraph+.001f);
            Check(Prefix+TEXT("shared_1000_cm_radius"),FMath::IsNearlyEqual(E->Radius,1000.f));
            if(Index==2)E->Tick(.2f);
            if(Index==3)S->TickComponent(.2f,LEVELTICK_All,nullptr);
            Check(Prefix+TEXT("990_cm_hit"),Inside->GetHealth()<100000&&Inside3D->GetHealth()<100000);
            Check(Prefix+TEXT("1010_cm_miss_including_vertical_distance"),Outside->GetHealth()==100000&&Outside3D->GetHealth()==100000);
            if(Falloff)
            {
                const auto& C=S->Settings->Spells[1];
                const float Expected=FMath::Lerp(C.Damage,C.MinimumDamage,FMath::Clamp((400.f-120)/(LegacyFireRadius-120),0.f,1.f));
                Check(Prefix+TEXT("original_fire_falloff"),FMath::Abs(100000-Falloff->GetHealth()-Expected)<.02f);
            }
            if(Index==2)
            {
                // A target between the legacy pull radius and damage boundary is damaged but not pulled.
                auto* PullControl=Target(Center-Start+FVector(0,0,0)+FVector((LegacyPullRadius+1000.f)*.5f,0,100));
                const FVector Before=PullControl->GetActorLocation();E->Tick(.2f);
                Check(Prefix+TEXT("original_pull_range"),FVector::Dist(Before,PullControl->GetActorLocation())<.01f);
                const float VisualRadius=E->WindVisual?E->WindVisual->GetGroundRadius():E->Parts[4]->GetRelativeScale3D().X*117;
                Check(Prefix+TEXT("visible_ground_radius_1000"),FMath::IsNearlyEqual(VisualRadius,1000.f,.1f));
                if(E->WindVisual)Check(Prefix+TEXT("wind_height_720_cm"),FMath::IsNearlyEqual(E->WindVisual->Body->GetStaticMesh()->GetBoundingBox().GetSize().Z*E->WindVisual->Body->GetRelativeScale3D().Z,720.f,1.f));
            }
            if(Index==3)
            {
                E->Tick(.6f);
                if(Pass==0)
                {
                    Check(Prefix+TEXT("pool_and_boundary_extent"),FMath::IsNearlyEqual(E->Parts[1]->GetRelativeScale3D().X*100/1.02f,1000.f,.1f)&&FMath::IsNearlyEqual(E->Parts[2]->GetRelativeScale3D().X*100/1.02f,1000.f,.1f));
                    E->Tick(E->Duration-.7f);
                    Check(Prefix+TEXT("boundary_visible_until_damage_ends"),E->Parts[2]->IsVisible()&&E->Materials[2]->K2_GetScalarParameterValue(TEXT("Opacity"))>.6f);
                }
                else Check(Prefix+TEXT("fallback_persistent_boundary"),FMath::IsNearlyEqual(E->Parts[2]->GetRelativeScale3D().X*100,1000.f,.1f));
            }
            E->Tick(20.f);Check(Prefix+TEXT("effect_cleanup"),E->IsActorBeingDestroyed());
            S->Clear();for(auto T:Targets)if(IsValid(T))T->Destroy();Targets.Reset();
        }
    }
    S->Settings->ImpactPresentation=Profile;
    S->Settings->WindPresentation=WindProfile;
    Check(TEXT("range_legacy_gameplay_settings_preserved"),S->Settings->Spells[1].Radius==LegacyFireRadius&&S->Settings->Spells[2].Radius==LegacyPullRadius);
}

void AHeliosCalamityDemo::TickRangeGallery(float Dt)
{
    auto* S=UHeliosCalamityState::Get(Player);auto* PC=Cast<APlayerController>(Player->GetController());
    const FVector Center=Start+FVector(1000,0,-86);
    if(!RangeEffect)
    {
        S->Clear();Player->SetActorLocation(Start+FVector(-1100,-1800,1600));
        PC->SetControlRotation((Center-Player->GetActorLocation()-FVector(0,0,64)).Rotation());
        FActorSpawnParameters P;P.Owner=Player;
        RangeEffect=GetWorld()->SpawnActor<AHeliosCalamityEffect>(Center,FRotator::ZeroRotator,P);
        RangeEffect->Initialize(S,static_cast<EHeliosCalamitySpell>(RangeSpell),Center+FVector(100,0,0),1);
        RangeEffect->SetActorTickEnabled(false);S->Effects.Add(RangeEffect);
        if(RangeSpell==1||RangeSpell==3)RangeEffect->Impact(Center-FVector(0,0,3));
        RangeCapture=0;Age=0;
    }
    // White survey circle is QA-only. Green/yellow markers sit at 9.9/10.1 m.
    DrawDebugCircle(GetWorld(),Center+FVector(0,0,8),1000,128,FColor::White,false,-1,0,2,FVector::ForwardVector,FVector::RightVector,false);
    DrawDebugPoint(GetWorld(),Center+FVector(990,0,12),12,FColor::Green,false,-1);
    DrawDebugPoint(GetWorld(),Center+FVector(1010,0,12),12,FColor::Yellow,false,-1);
    const float T=RangeEffect->Telegraph;
    const float Times[]={.1f,T+.12f,T+(RangeSpell==1?.46f:.8f),T+RangeEffect->Duration+.15f};
    const TCHAR* Names[]={TEXT("formation"),TEXT("onset"),TEXT("expanded"),TEXT("tail")};
    // Freeze at each requested age so a slow frame cannot skip the impact wave.
    if(RangeCapture<4&&Age>.5f&&PendingCapturePath.IsEmpty()&&!FScreenshotRequest::IsScreenshotRequested())
    {
        RangeEffect->Tick(FMath::Max(0.f,Times[RangeCapture]-RangeEffect->FieldAge));
        Capture(FString::Printf(TEXT("range_%d_%s"),RangeSpell,Names[RangeCapture]));++RangeCapture;Age=0;
    }
    if(RangeCapture==4&&Age>.5f&&PendingWrites->GetValue()==0)
    {
        S->Clear();RangeEffect=nullptr;++RangeSpell;
        if(RangeSpell>=4){Player->SetActorLocation(Start);PC->SetControlRotation(FRotator(-5,0,0));Stage=3;Age=0;}
    }
}
