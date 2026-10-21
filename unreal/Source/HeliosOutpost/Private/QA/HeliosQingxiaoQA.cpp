#include "QA/HeliosQingxiaoQA.h"
#include "Weapons/HeliosQingxiao.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Weapons/HeliosFirstPersonPresentationComponent.h"
#include "Weapons/HeliosWeaponVFX.h"
#include "Player/HeliosArenaCharacter.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "UI/HeliosAdventureHUD.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Enemies/HeliosEnemyTestDirector.h"
#include "Boss/HeliosEnemyBoss.h"
#include "Pets/HeliosPetDog.h"
#include "Town/HeliosTownSubsystem.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/DamageEvents.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "ShaderCompiler.h"
#include "TimerManager.h"
#include "QA/HeliosLiveSettingsWorld.h"
#include "Combat/CombatDamageSettings.h"
#include "Health/CreatureHealthSettings.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "World/HeliosMapTravelSubsystem.h"
#include "Engine/GameInstance.h"
static int32 SettingsRouteStep=0;

AHeliosQingxiaoQA::AHeliosQingxiaoQA()
{PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void AHeliosQingxiaoQA::Check(const FString& Name,bool Pass)
{if(!Pass)++Failures;if(!Results.IsEmpty())Results+=TEXT(",\n");Results+=FString::Printf(TEXT("{\"name\":\"%s\",\"passed\":%s}"),*Name,Pass?TEXT("true"):TEXT("false"));UE_LOG(LogTemp,Display,TEXT("QX_CHECK %s %s"),*Name,Pass?TEXT("PASS"):TEXT("FAIL"));}
void AHeliosQingxiaoQA::Capture(const FString& Name)
{if(!bCapture)return;const auto Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/qingxiao")/(Name+TEXT(".png")));IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);FScreenshotRequest::RequestScreenshot(Path,true,false);UE_LOG(LogTemp,Display,TEXT("QX_FRAME %s t=%.6f"),*Name,GetWorld()->GetTimeSeconds());}
void AHeliosQingxiaoQA::Finish()
{
    auto Avg=[](const TArray<double>& A){double S=0;for(double V:A)S+=V;return A.Num()?S/A.Num():0.;};
    auto P95=[](TArray<double> A){A.Sort();return A.Num()?A[FMath::Min(A.Num()-1,FMath::FloorToInt(A.Num()*.95))]:0.;};
    const FString Report=FString::Printf(TEXT("{\"status\":\"%s\",\"failures\":%d,\"map\":\"%s\",\"capture\":%s,\"capture_frames\":%d,\"stress_rounds\":%d,\"frame_time_ms\":{\"idle_mean\":%.3f,\"idle_p95\":%.3f,\"combat_mean\":%.3f,\"combat_p95\":%.3f,\"idle_samples\":%d,\"combat_samples\":%d},\"checks\":[%s]}"),Failures?TEXT("failed"):TEXT("passed"),Failures,*GetWorld()->GetMapName(),bCapture?TEXT("true"):TEXT("false"),CaptureFrame,Round,Avg(IdleFrames),P95(IdleFrames),Avg(CombatFrames),P95(CombatFrames),IdleFrames.Num(),CombatFrames.Num(),*Results);
    FString File=bCapture?TEXT("capture_report.json"):bSmoke?TEXT("smoke_")+GetWorld()->GetMapName()+TEXT(".json"):TEXT("runtime_report.json");
    if(FParse::Param(FCommandLine::Get(),TEXT("QXLowFPS")))File=FPaths::GetBaseFilename(File)+TEXT("_12fps.json");
    FFileHelper::SaveStringToFile(Report,*(FPaths::ProjectDir()/TEXT("../qa/qingxiao")/File),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    if(bSettings)
    {
        const FString StepFile=FPaths::ProjectSavedDir()/FString::Printf(TEXT("settings_route_%d.json"),SettingsRouteStep);
        FFileHelper::SaveStringToFile(Report,*StepFile,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        if(!Failures && FParse::Param(FCommandLine::Get(),TEXT("QXSettingsTravel")) && SettingsRouteStep<4)
        {
            // The external runner saves the next fixture while this map stays alive.
            // Continue only after all three INIs and the next oracle have been saved.
            Stage=90;
            return;
        }
    }
    Stage=999;FPlatformMisc::RequestExit(false);
}
void AHeliosQingxiaoQA::ClearTargets()
{for(auto E:Enemies)if(IsValid(E))E->Destroy();Enemies.Reset();if(IsValid(Obstacle))Obstacle->Destroy();Obstacle=nullptr;}
AHeliosEnemyActor* AHeliosQingxiaoQA::Enemy(const FVector& Offset,bool Boss)
{
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* E=GetWorld()->SpawnActor<AHeliosEnemyActor>(Boss?AHeliosEnemyBoss::StaticClass():AHeliosEnemyActor::StaticClass(),Start+Offset,FRotator(0,180,0),Params);
    E->CurrentHP=1000000;E->SetActorTickEnabled(false);E->GetCharacterMovement()->DisableMovement();E->StopShooting();GetWorld()->GetTimerManager().ClearAllTimersForObject(E);
    if(auto* AI=Cast<AAIController>(E->GetController())){AI->StopMovement();if(AI->GetBrainComponent())AI->GetBrainComponent()->StopLogic(TEXT("Qingxiao QA"));AI->SetActorTickEnabled(false);}
    Enemies.Add(E);return E;
}
void AHeliosQingxiaoQA::BeginCase()
{
    ClearTargets();auto* F=UHeliosQingxiaoFormation::Get(Player);F->Clear();auto* W=Cast<AHeliosQingxiao>(Player->GetEquippedWeapon());W->CancelReload();
    BeforeHits=F->HitsTotal;BeforeWaves=F->WaveSerial;BeforeWalls=F->WallBurstsTotal;bCaseAction=false;Age=0;
    if(Case==1)Enemy(FVector(1700,0,0));
    if(Case==2){Enemy(FVector(1800,-350,0));Enemy(FVector(1800,350,0));}
    if(Case==3||Case==4)for(int32 I=0;I<(Case==3?5:7);++I)Enemy(FVector(2200+(I%2)*400,(I-2)*370,0));
    if(Case==5){Enemy(FVector(9999,0,0));Enemy(FVector(0,10000,0));Enemy(FVector(10001,0,0));}
    if(Case==6){Enemy(FVector(-1800,0,0));Enemy(FVector(1400,0,1500));}
    if(Case==7||Case==12||(Case>=16&&Case<=21))
    {
        Enemy(FVector(1700,0,0));Obstacle=GetWorld()->SpawnActor<AStaticMeshActor>();auto* M=Obstacle->GetStaticMeshComponent();M->SetMobility(EComponentMobility::Movable);M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));M->SetCollisionProfileName(TEXT("BlockAll"));
        if(Case!=12){Obstacle->SetActorLocation(Start+FVector(Case==16?90:Case==18?2200:800,0,100));Obstacle->SetActorScale3D(FVector(Case==17?.01f:.5f,12,10));}
        else{Obstacle->SetActorLocation(Start+FVector(0,0,250));Obstacle->SetActorScale3D(FVector(16,16,.2f));}
        if(Case==19)Obstacle->SetActorRotation(FRotator(0,35,0));
        if(Case==20)Obstacle->SetActorLocation(Start+FVector(4000,0,100));
        if(Case==21)Obstacle->SetActorLocation(Player->FirstPersonCamera->GetComponentLocation());
    }
    if(Case==8||Case==9){Enemy(FVector(2000,-300,0));Enemy(FVector(2100,300,0));}
    if(Case==10)Enemy(FVector(5000,0,0));
    if(Case==11){Enemy(FVector(9500,0,0));W->Settings->SwordSpeed=500;}
    if(Case==13)Enemy(FVector(1600,0,0),true);
    if(Case==14||Case==15)Enemy(FVector(2400,0,0));
    if(Case>=22&&Case<=25)
    {
        Enemy(FVector(Case==25?6500:1700,0,0));
        Enemy(FVector(Case==25?7000:3500,4500,0));
        if(Case==23)Enemy(FVector(3500,-4500,0));
        Obstacle=GetWorld()->SpawnActor<AStaticMeshActor>();auto* M=Obstacle->GetStaticMeshComponent();
        M->SetMobility(EComponentMobility::Movable);M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));M->SetCollisionProfileName(TEXT("BlockAll"));
        Obstacle->SetActorScale3D(FVector(.01f,6,10));
        Obstacle->SetActorLocation(Start+FVector(Case>=24?9000:1000,0,100));
    }
    if(Case==26)
    {
        auto* E=Enemy(FVector(1700,0,0));Enemy(FVector(3500,4500,0));
        Obstacle=GetWorld()->SpawnActor<AStaticMeshActor>();auto* M=Obstacle->GetStaticMeshComponent();
        M->SetMobility(EComponentMobility::Movable);M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));M->SetCollisionProfileName(TEXT("BlockAll"));
        // Wall intersects the centerward trace, but lies behind the first capsule contact.
        Obstacle->SetActorLocation(E->GetActorLocation()+FVector(10,0,0));Obstacle->SetActorScale3D(FVector(.01f,6,10));
    }
    Check(FString::Printf(TEXT("case_%02d_begin"),Case),F->Begin(W->Settings));
    Check(FString::Printf(TEXT("case_%02d_exactly_five"),Case),F->Remaining()==5);
    TMap<AHeliosEnemyActor*,int32> Assignment;for(auto S:F->Swords)if(S->Target.IsValid())++Assignment.FindOrAdd(S->Target.Get());
    if(Case==0)Check(TEXT("no_enemies_no_targets"),Assignment.Num()==0);
    if(Case==1)Check(TEXT("single_enemy_five_assignments"),Assignment.Num()==1&&Assignment.FindRef(Enemies[0])==5);
    if(Case==2)Check(TEXT("two_enemies_balanced_three_two"),Assignment.Num()==2&&FMath::Abs(Assignment.FindRef(Enemies[0])-Assignment.FindRef(Enemies[1]))==1);
    if(Case==3||Case==4)Check(TEXT("five_distinct_nearest_targets"),Assignment.Num()==5);
    if(Case==5)Check(TEXT("sphere_boundary_includes_100m_excludes_over"),F->Eligible(Enemies[0])&&F->Eligible(Enemies[1])&&!F->Eligible(Enemies[2]));
    if(Case==6)Check(TEXT("behind_and_elevated_targets"),Assignment.Num()==2);
    if(Case==7||(Case>=16&&Case<=21))Check(TEXT("wall_does_not_prevent_lock"),Assignment.Num()==1&&Assignment.FindRef(Enemies[0])==5);
    if(Case==22)Check(TEXT("clear_far_target_beats_near_blocked_target"),Assignment.Num()==1&&Assignment.FindRef(Enemies[1])==5);
    if(Case==23)Check(TEXT("clear_targets_balanced_blocked_target_excluded"),Assignment.FindRef(Enemies[0])==0&&Assignment.Num()==2&&FMath::Abs(Assignment.FindRef(Enemies[1])-Assignment.FindRef(Enemies[2]))==1);
    if(Case==24||Case==25)for(auto Sword:F->Swords)Sword->Target=Enemies[0];
    if(Case==26)Check(TEXT("wall_behind_capsule_contact_does_not_block_acquisition"),Assignment.Num()==2&&Assignment.FindRef(Enemies[0])==3&&Assignment.FindRef(Enemies[1])==2);
    if(Case==12)Check(TEXT("ceiling_compresses_formation"),F->bCompressed);
    Check(TEXT("active_wave_rejects_duplicate"),!F->Begin(W->Settings)&&F->Remaining()==5);
}
void AHeliosQingxiaoQA::EndCase()
{
    auto* F=UHeliosQingxiaoFormation::Get(Player);const int32 Hits=F->HitsTotal-BeforeHits;
    const bool Blocked=Case==7||Case==16||Case==17||(Case>=19&&Case<=21);
    Check(FString::Printf(TEXT("case_%02d_single_hit_per_sword"),Case),Hits==(Case==0||Case==11||Blocked?0:5));
    Check(FString::Printf(TEXT("case_%02d_all_dissolved"),Case),!F->Busy()&&F->Remaining()==0);
    if(Blocked)
    {
        Check(TEXT("wall_blocks_all_sword_damage"),Enemies[0]->CurrentHP==1000000.f);
        Check(TEXT("each_blocked_sword_bursts_exactly_once"),F->WallBurstsTotal-BeforeWalls==5);
    }
    if(Case==0||Case==11||Case==18)Check(TEXT("natural_end_and_enemy_hit_do_not_spawn_wall_bursts"),F->WallBurstsTotal==BeforeWalls);
    if(Case==18)Check(TEXT("enemy_before_wall_receives_damage"),Enemies[0]->CurrentHP==1000000.f-5*ExpectedSword);
    if(Case>=22&&Case<=25)
    {
        Check(TEXT("occluded_enemy_untouched"),Enemies[0]->CurrentHP==1000000.f);
        Check(TEXT("clear_priority_avoids_wall_bursts"),F->WallBurstsTotal==BeforeWalls);
        Check(Case==24?TEXT("launch_rechecks_occlusion"):Case==25?TEXT("flight_retargets_when_occluded"):TEXT("clear_priority_delivers_five_hits"),Hits==5);
    }
    if(Case==1)Check(TEXT("five_swords_configured_damage"),FMath::IsNearlyEqual(Enemies[0]->CurrentHP,1000000.f-5*ExpectedSword));
    if(Case==4)Check(TEXT("sixth_seventh_not_hit"),Enemies[5]->CurrentHP==1000000.f&&Enemies[6]->CurrentHP==1000000.f);
    if(Case==5)Check(TEXT("out_of_range_untouched"),Enemies[2]->CurrentHP==1000000.f);
    if(Case==9)Check(TEXT("escaped_target_untouched"),Enemies[0]->CurrentHP==1000000.f);
    if(Case==10)Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("qingxiao")));
    if(Case==11)Cast<AHeliosQingxiao>(Player->GetEquippedWeapon())->Settings->SwordSpeed=7200;
    if(Case==13)Check(TEXT("boss_receives_sword_damage"),FMath::IsNearlyEqual(1000000.f-Enemies[0]->CurrentHP,5*ExpectedSword,.1f));
    if(Case==15)Player->SetActorLocation(Start);
}
void AHeliosQingxiaoQA::Tick(float Dt)
{
    Super::Tick(Dt);if(Stage==999)return;
    const double Now=FPlatformTime::Seconds();if(!Started){Started=Now;bCapture=FParse::Param(FCommandLine::Get(),TEXT("QXCapture"));bSmoke=FParse::Param(FCommandLine::Get(),TEXT("QXSmoke"));
        bSettings=FParse::Param(FCommandLine::Get(),TEXT("QXSettings"));
        if(bSettings)UE_LOG(LogTemp,Display,TEXT("QX_SETTINGS_STEP %d map=%s"),SettingsRouteStep,*GetWorld()->GetMapName());
        bSmoke|=bSettings;
        ExpectedSlash=UCombatDamageSettings::Resolve(this,ECombatDamageType::QingxiaoSlash);
        ExpectedSword=UCombatDamageSettings::Resolve(this,ECombatDamageType::QingxiaoSword);
        ExpectedHealth=UCreatureHealthSettings::Resolve(this,ECreatureHealthType::Player);
        FString OraclePath;
        if(FParse::Value(FCommandLine::Get(),TEXT("HeliosExpectedCombat="),OraclePath))
        {
            FString Text; TSharedPtr<FJsonObject> Oracle;
            if(!FFileHelper::LoadFileToString(Text,*OraclePath)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Oracle)||!Oracle)
            {Check(TEXT("independent_oracle_loaded"),false);Finish();return;}
            ExpectedSlash=Oracle->GetNumberField(TEXT("武器伤害.ini|青霄|普通挥剑"));
            ExpectedSword=Oracle->GetNumberField(TEXT("武器伤害.ini|青霄|每柄飞剑"));
            ExpectedHealth=Oracle->GetNumberField(TEXT("生物血量.ini|生物血量|玩家"));
        }
        else if(bSettings){Check(TEXT("independent_oracle_required"),false);Finish();return;}
}
    if(Now-Started>600){Check(TEXT("timeout"),false);Finish();return;}
    const double FrameMs=PreviousFrame?(Now-PreviousFrame)*1000:0;PreviousFrame=Now;
    if(!Player)Player=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));if(!Player)return;
    auto* PC=Cast<APlayerController>(Player->GetController());auto* HUD=PC?Cast<AHeliosAdventureHUD>(PC->GetHUD()):nullptr;
    auto* F=UHeliosQingxiaoFormation::Get(Player);auto* W=Cast<AHeliosQingxiao>(Player->GetEquippedWeapon());Age+=Dt;
    if(bCapture&&((Stage>=3&&Stage<=8)||(Stage>=50&&Stage<=53))&&++Frame%2==0)Capture(FString::Printf(TEXT("frames/frame_%05d"),CaptureFrame++));
    if(Stage==0){if(!HUD)return;if(HUD)HUD->SelectWeapon(TEXT("qingxiao"));Stage=1;Age=0;return;}
    if(Stage==1&&Age>.8f&&!bCaseAction){Capture(TEXT("01_menu"));bCaseAction=true;return;}
    if(Stage==1&&Age>1.05f)
    {
        if(UHeliosTownSubsystem::IsTownMap(GetWorld()))if(auto* T=GetWorld()->GetSubsystem<UHeliosTownSubsystem>())if(!T->IsWorldReady())return;
        if(HUD)HUD->StartSelectedRun();else Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("qingxiao")));
        Stage=2;Age=0;bCaseAction=false;return;
    }
    if(Stage==2)
    {
        if(UHeliosTownSubsystem::IsTownMap(GetWorld()))if(auto* T=GetWorld()->GetSubsystem<UHeliosTownSubsystem>())if(!T->IsWorldReady())return;
        if(auto* D=AHeliosAdventureDirector::Find(GetWorld()))D->SetActorTickEnabled(false);
        for(TActorIterator<AHeliosEnemyTestDirector> D(GetWorld());D;++D)D->SetActorTickEnabled(false);
        Player->SetCanBeDamaged(false);Player->GetCharacterMovement()->StopMovementImmediately();Player->GetCharacterMovement()->DisableMovement();// Keep synthetic range cases clear of map geometry; obstacles are added explicitly below.
        if(!bCapture&&!bSmoke)Player->SetActorLocation(Player->GetActorLocation()+FVector(0,0,30000));
        if((bSmoke||bCapture)&&UHeliosTownSubsystem::IsTownMap(GetWorld()))
        {
            // Town picks a random district at startup. Find a genuinely open firing lane
            // for the hit-count scenario, leaving world collision enabled throughout.
            const FVector Original=Player->GetActorLocation();
            const FVector EyeOffset=Player->FirstPersonCamera->GetComponentLocation()-Original;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(QXQAClearLane),false,Player);
            for(TActorIterator<APawn> Pawn(GetWorld());Pawn;++Pawn)Query.AddIgnoredActor(*Pawn);
            bool Found=false;
            for(int32 Ring=0;Ring<=10&&!Found;++Ring)for(int32 X=-Ring;X<=Ring&&!Found;++X)for(int32 Y=-Ring;Y<=Ring&&!Found;++Y)
            {
                if(FMath::Max(FMath::Abs(X),FMath::Abs(Y))!=Ring)continue;
                const FVector Candidate=Original+FVector(X*400,Y*400,0),Eye=Candidate+EyeOffset;
                bool Clear=true;
                for(int32 I=0;I<5&&Clear;++I)
                {
                    const FVector Birth=Eye+FVector(340,(I-2)*66,56-12*FMath::Abs(I-2));
                    const FVector Goal=Candidate+FVector((bCapture?1100:2200)+(I%2)*(bCapture?160:300),(I-2)*(bCapture?235:370),20);
                    FHitResult Hit;
                    Clear=!GetWorld()->SweepSingleByChannel(Hit,Eye,Birth,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(65),Query)
                        &&!GetWorld()->SweepSingleByChannel(Hit,Birth,Goal,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(85),Query);
                }
                if(Clear){Player->SetActorLocation(Candidate);Found=true;UE_LOG(LogTemp,Display,TEXT("QX_QA_CLEAR_LANE %s"),*Candidate.ToString());}
            }
            Check(TEXT("town_open_firing_lane_found"),Found);
            if(!Found){Finish();return;}
        }
        Start=Player->GetActorLocation();PC->SetControlRotation(FRotator::ZeroRotator);
        for(TActorIterator<AHeliosEnemyActor> E(GetWorld());E;++E)E->Destroy();
        for(TActorIterator<AHeliosPetCompanion> Pet(GetWorld());Pet;++Pet)Pet->Destroy();
        Check(TEXT("menu_equips_qingxiao"),Player->GetWeaponId()==TEXT("qingxiao")&&W);Check(TEXT("hands_and_weapon_ready"),Player->GetPresentation()&&Player->GetPresentation()->IsReady());
        if(!W){Finish();return;}Check(TEXT("no_ammo_or_reload"),!W->UsesAmmunition());
        Check(TEXT("light_reach_500cm"),FMath::IsNearlyEqual(W->Settings->Range,500.f));
        Check(TEXT("light_timing_and_damage_unchanged"),FMath::IsNearlyEqual(W->Settings->SlashSeconds,.32f)&&FMath::IsNearlyEqual(W->Settings->Damage,ExpectedSlash));
        Check(TEXT("secondary_search_100m"),FMath::IsNearlyEqual(W->Settings->SearchRadius,10000.f));
        Check(TEXT("session_slash_matches_oracle"),UCombatDamageSettings::Resolve(this,ECombatDamageType::QingxiaoSlash)==ExpectedSlash);
        Check(TEXT("session_sword_matches_oracle"),UCombatDamageSettings::Resolve(this,ECombatDamageType::QingxiaoSword)==ExpectedSword);
        Check(TEXT("equipped_sword_matches_oracle"),W->Settings->SwordDamage==ExpectedSword);
        Check(TEXT("live_player_max_health"),Player->GetMaxHealth()==ExpectedHealth);
        if(bSettings && !bLiveChecked)
        {
#if WITH_DEV_AUTOMATION_TESTS
            Check(TEXT("all_live_weapon_and_enemy_damage"),HeliosCheckLiveDamage(GetWorld()));
            Check(TEXT("all_live_creature_health"),HeliosCheckLiveHealth(GetWorld()));
#endif
            bLiveChecked=true;
        }
        if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
        for(int32 I=0;I<5;++I)Enemy(FVector((bCapture?1100:2200)+(I%2)*(bCapture?160:300),(I-2)*(bCapture?235:370),0));
        Stage=3;Age=0;return;
    }
    if(Stage==3)
    {
        if(Age>.5)IdleFrames.Add(FrameMs);
        if(Age>1.8&&!bCaseAction){Capture(TEXT("02_equipped"));bCaseAction=true;}
        if(Age>2){ClearTargets();Enemy(FVector(190,0,0));Before=Enemies[0]->CurrentHP;Player->StartFireFromAutomation();Player->StopFireFromAutomation();Stage=4;Age=0;bCaseAction=false;}return;
    }
    if(Stage==4&&Age>.6)
    {
        UE_LOG(LogTemp,Display,TEXT("QX_SETTINGS_SLASH actual=%.9g expected=%.9g"),Before-Enemies[0]->CurrentHP,ExpectedSlash);
        Check(TEXT("tap_one_slash_configured_damage"),FMath::IsNearlyEqual(Before-Enemies[0]->CurrentHP,ExpectedSlash,.02f));
        Check(TEXT("tap_does_not_auto_repeat"),(bSettings||W->SlashSerial==1));
        Check(TEXT("tap_exactly_one_contact_feedback"),(bSettings||W->LightFeedbackTotal==1));
        Player->StartFireFromAutomation();Stage=5;Age=0;Before=Enemies[0]->CurrentHP;return;
    }
    if(Stage==5&&Age>1.8)
    {
        Player->StopFireFromAutomation();Check(TEXT("held_light_combo"),W->SlashSerial>=6&&(ExpectedSlash==0?Enemies[0]->CurrentHP==Before:Enemies[0]->CurrentHP<Before));W->CancelReload();ClearTargets();
        for(int32 I=0;I<5;++I)Enemy(FVector((bCapture?1100:2200)+(I%2)*(bCapture?160:300),(I-2)*(bCapture?235:370),0));
        BeforeHits=F->HitsTotal;BeforeWaves=F->WaveSerial;BeforeWalls=F->WallBurstsTotal;Player->StartSecondaryAttack();
        Check(TEXT("formation_damage_matches_oracle"),F->Settings&&F->Settings->SwordDamage==ExpectedSword);
        Stage=6;Age=0;return;
    }
    if(Stage==6)
    {
        CombatFrames.Add(FrameMs);
        if(bCapture&&!bImpactShot&&F->HitsTotal-BeforeHits>=4){Capture(TEXT("04_rending_hit"));bImpactShot=true;}
        if(Age>.25&&!bCaseAction){Capture(TEXT("03_five_swords"));Check(TEXT("five_visible_swords"),F->Remaining()==5);bCaseAction=true;}
        if(Age>2.0)
        {
            UE_LOG(LogTemp,Display,TEXT("QX_QA_WAVE hits=%d walls=%d remaining=%d"),F->HitsTotal-BeforeHits,F->WallBurstsTotal-BeforeWalls,F->Remaining());
            Check(TEXT("five_targets_five_hits"),F->HitsTotal-BeforeHits==5);
            float Total=0; for(int32 I=0;I<Enemies.Num();++I)
            {
                const float Actual=1000000.f-Enemies[I]->CurrentHP;Total+=Actual;
                UE_LOG(LogTemp,Display,TEXT("QX_SETTINGS_SWORD target=%d actual=%.9g expected=%.9g"),I,Actual,ExpectedSword);
                // Retargeting can change which capsule each sword hits. The runner
                // checks every QX_SETTINGS_HIT against the independent oracle.
            }
            Check(TEXT("five_swords_total"),FMath::IsNearlyEqual(Total,5*ExpectedSword,.1f));
            if(bSettings)
            {
                Player->StopSecondaryAttack();F->Clear();ClearTargets();
                if(!bLifecycleChecked)
                {
                    Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("rifle")));
                    Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("qingxiao")));
                    auto* Reequipped=Cast<AHeliosQingxiao>(Player->GetEquippedWeapon());
                    Check(TEXT("reequip_damage"),Reequipped&&Reequipped->Settings->Damage==ExpectedSlash&&Reequipped->Settings->SwordDamage==ExpectedSword);
                    Player->SetCanBeDamaged(true);
                    FDamageEvent Damage;Player->TakeDamage(Player->GetMaxHealth()*2,Damage,nullptr,nullptr);
                    Check(TEXT("live_player_died"),Player->IsDead());
                    if(auto* D=AHeliosAdventureDirector::Find(GetWorld()))D->SetActorTickEnabled(true);
                    if(UHeliosTownSubsystem::IsTownMap(GetWorld()))GetWorld()->GetSubsystem<UHeliosTownSubsystem>()->RespawnAtCheckpoint();
                    else if(auto* D=AHeliosAdventureDirector::Find(GetWorld()))D->RestartRun();
                    Stage=80;Age=0;return;
                }
                Finish();return;
            }
            Check(TEXT("held_secondary_no_repeat"),F->WaveSerial-BeforeWaves==1);
            Check(TEXT("every_sword_hit_spawns_one_rend"),F->RendTotal==F->HitsTotal);
            Player->StopSecondaryAttack();Player->StartSecondaryAttack();Player->StartFireFromAutomation();Stage=7;Age=0;Before=W->SlashSerial;bCaseAction=false;
        }return;
    }
    if(Stage==90)
    {
        if(!IFileManager::Get().FileExists(*(FPaths::ProjectSavedDir()/FString::Printf(TEXT("settings_continue_%d"),SettingsRouteStep))))return;
        Check(TEXT("disk_edit_keeps_current_map_damage"),UCombatDamageSettings::Resolve(this,ECombatDamageType::QingxiaoSlash)==ExpectedSlash
            &&UCombatDamageSettings::Resolve(this,ECombatDamageType::QingxiaoSword)==ExpectedSword);
        Check(TEXT("disk_edit_keeps_current_map_health"),UCreatureHealthSettings::Resolve(this,ECreatureHealthType::Player)==ExpectedHealth);
        if(Failures){Finish();return;}
        UE_LOG(LogTemp,Display,TEXT("QX_OLD_MAP_SNAPSHOT_PASS step=%d"),SettingsRouteStep);
        ++SettingsRouteStep;Stage=999;
        // Adventure -> Town -> reload Town -> Adventure -> Town.
        const FName Next=SettingsRouteStep==3?TEXT("HeliosAdventure"):TEXT("IkunTown");
        Check(TEXT("request_map_travel"),GetGameInstance()->GetSubsystem<UHeliosMapTravelSubsystem>()->RequestTravel(Next,TEXT("qingxiao")));
        if(Failures)Finish();
        return;
    }
    if(Stage==80)
    {
        if(Player->IsDead())return;
        if(UHeliosTownSubsystem::IsTownMap(GetWorld())&&!GetWorld()->GetSubsystem<UHeliosTownSubsystem>()->IsWorldReady())return;
        Check(TEXT("live_respawn_full_configured_health"),Player->GetHealth()==ExpectedHealth&&Player->GetMaxHealth()==ExpectedHealth);
        bLifecycleChecked=true;Stage=2;Age=0;bCaseAction=false;return;
    }
    if(Stage==7)
    {
        CombatFrames.Add(FrameMs);
        if(Age>1.8){Check(TEXT("melee_resumes_after_summon"),W->SlashSerial>Before);Player->StopFireFromAutomation();Player->StopSecondaryAttack();W->CancelReload();Stage=8;Age=0;}return;
    }
    if(Stage==8&&Age>.8)
    {
        int32 Impacts=0;for(TActorIterator<AHeliosQingxiaoImpact> I(GetWorld());I;++I)++Impacts;
        Check(TEXT("rending_impacts_expire_without_residue"),Impacts==0);
        if(bCapture||bSmoke)
        {
            Check(TEXT("wave_finished_without_residue"),!F->Busy());
            ClearTargets();Enemy(FVector(1700,0,0));BeforeWalls=F->WallBurstsTotal;
            Obstacle=GetWorld()->SpawnActor<AStaticMeshActor>();auto* M=Obstacle->GetStaticMeshComponent();
            M->SetMobility(EComponentMobility::Movable);M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));M->SetCollisionProfileName(TEXT("BlockAll"));
            Obstacle->SetActorLocation(Start+FVector(800,0,100));Obstacle->SetActorScale3D(FVector(.2f,12,10));
            F->Begin(W->Settings);Stage=50;Age=0;bCaseAction=false;return;
        }
        Case=0;Stage=40;Age=0;return;
    }
    if(Stage==50)
    {
        if(!bCaseAction&&F->WallBurstsTotal>BeforeWalls){Capture(TEXT("05_wall_burst"));bCaseAction=true;}
        if(Age>1.6f)
        {
            Check(TEXT("visible_wall_burst_five_events"),F->WallBurstsTotal-BeforeWalls==5);
            Check(TEXT("visible_wall_burst_no_damage"),Enemies[0]->CurrentHP==1000000.f);
            BeforeWalls=F->WallBurstsTotal;Obstacle->SetActorLocation(Start+FVector(90,0,100));
            Obstacle->GetStaticMeshComponent()->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Helios/Weapons/Qingxiao/M_QX_QADarkWall")));
            F->Begin(W->Settings);Stage=51;Age=0;bCaseAction=false;
        }
        return;
    }
    if(Stage==51)
    {
        if(Age>.05f&&!bCaseAction){Capture(TEXT("06_near_wall_burst"));bCaseAction=true;}
        if(Age>1.2f)
        {
            Check(TEXT("near_wall_summon_five_bursts"),F->WallBurstsTotal-BeforeWalls==5);
            Check(TEXT("near_wall_no_damage"),Enemies[0]->CurrentHP==1000000.f);
            ClearTargets();Before=W->LightFeedbackTotal;Player->StartFireFromAutomation();Stage=52;Age=0;bCaseAction=false;
        }
        return;
    }
    if(Stage==52)
    {
        if(Age>.19f&&!bCaseAction){Capture(TEXT("07_light_swing"));bCaseAction=true;}
        if(Age>1.3f){Player->StopFireFromAutomation();W->CancelReload();Check(TEXT("air_slash_has_no_contact_feedback"),W->LightFeedbackTotal==Before);Stage=53;Age=0;}
        return;
    }
    if(Stage==53&&Age>.8f)
    {
        int32 Effects=0;for(TActorIterator<AHeliosQingxiaoBurst> I(GetWorld());I;++I)++Effects;
        for(TActorIterator<AHeliosQingxiaoRibbon> I(GetWorld());I;++I)++Effects;
        Check(TEXT("all_new_effects_expire"),Effects==0);Finish();return;
    }
    // Test the actual animated sweep, including enemy capsule and 20cm sweep thickness.
    if(Stage==40)
    {
        W->CancelReload();ClearTargets();
        const float Distances[]={450.f,500.f,570.f,450.f};
        const FVector Eye=Player->FirstPersonCamera->GetComponentLocation();
        auto* Target=Enemy(Eye-Start+FVector(Distances[Case/2],0,0));
        if(Case>=6)
        {
            Obstacle=GetWorld()->SpawnActor<AStaticMeshActor>();auto* M=Obstacle->GetStaticMeshComponent();
            M->SetMobility(EComponentMobility::Movable);M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));M->SetCollisionProfileName(TEXT("BlockAll"));
            Obstacle->SetActorLocation(Eye+FVector(220,0,0));Obstacle->SetActorScale3D(FVector(.15f,12,12));
        }
        Before=Target->CurrentHP;
        // Each distance is checked with both left and right attacks.
        W->SlashSerial=Case%2;
        Player->StartFireFromAutomation();Player->StopFireFromAutomation();
        Stage=41;Age=0;return;
    }
    if(Stage==41&&Age>.6f)
    {
        const float Expected=Case<4?ExpectedSlash:0.f;
        Check(FString::Printf(TEXT("light_range_case_%d_%s"),Case,Case%2?TEXT("right"):TEXT("left")),FMath::IsNearlyEqual(Before-Enemies[0]->CurrentHP,Expected));
        int32 Ribbons=0;for(TActorIterator<AHeliosQingxiaoRibbon> I(GetWorld());I;++I)++Ribbons;
        Check(TEXT("light_trail_expires"),Ribbons==0);
        if(++Case<8){Stage=40;Age=0;return;}
        ClearTargets();Player->StartFireFromAutomation();Stage=42;Age=0;return;
    }
    if(Stage==42&&Age>.13f)
    {
        Player->StopFireFromAutomation();Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("rifle")));
        Stage=43;Age=0;return;
    }
    if(Stage==43&&Age>.4f)
    {
        int32 Ribbons=0;for(TActorIterator<AHeliosQingxiaoRibbon> I(GetWorld());I;++I)++Ribbons;
        Check(TEXT("switch_during_slash_clears_trail"),Player->GetWeaponId()==TEXT("rifle")&&Ribbons==0);
        Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("qingxiao")));
        W=Cast<AHeliosQingxiao>(Player->GetEquippedWeapon());
        Check(TEXT("reequip_keeps_5m_reach"),W&&FMath::IsNearlyEqual(W->Settings->Range,500.f));
        Case=0;BeginCase();Stage=20;return;
    }
    if(Stage==20)
    {
        if(Case==14&&Enemies.Num())Enemies[0]->SetActorLocation(Start+FVector(2400+Age*200,FMath::Sin(Age*3)*450,Age*80));
        if(!bCaseAction&&Age>(Case==25?.85f:.20f))
        {
            bCaseAction=true;
            if(Case==24||Case==25)Obstacle->SetActorLocation(Start+FVector(Case==25?4800:1000,0,100));
            if(Case==8)Enemies[0]->Destroy();
            if(Case==9)Enemies[0]->SetActorLocation(Start+FVector(12000,0,0));
            if(Case==10){Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("rifle")));Check(TEXT("switch_preserves_wave"),F->Busy()&&F->Remaining()==5);}
            if(Case==15){Player->SetActorLocation(Start+FVector(15000,0,0));Check(TEXT("range_anchored_at_cast_origin"),F->Eligible(Enemies[0]));}
            if(Case==20)Obstacle->SetActorLocation(F->Swords[2]->GetActorLocation());
        }
        if(!F->Busy()&&Age>.9){EndCase();if(++Case<=26)BeginCase();else{Stage=30;Age=0;ClearTargets();Enemy(FVector(1800,0,0));F->Begin(W->Settings);}}
        return;
    }
    if(Stage==30&&Age>.1)
    {PausedAge=F->Age;UGameplayStatics::SetGamePaused(this,true);PauseStart=Now;Stage=31;return;}
    if(Stage==31&&Now-PauseStart>.3)
    {
        Check(TEXT("pause_freezes_wave"),FMath::IsNearlyEqual(F->Age,PausedAge));UGameplayStatics::SetGamePaused(this,false);F->Clear();
        // Near melee is also blocked by a real wall.
        ClearTargets();Enemy(FVector(190,0,0));Obstacle=GetWorld()->SpawnActor<AStaticMeshActor>();auto* M=Obstacle->GetStaticMeshComponent();M->SetMobility(EComponentMobility::Movable);M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));M->SetCollisionProfileName(TEXT("BlockAll"));Obstacle->SetActorLocation(Start+FVector(90,0,0));Obstacle->SetActorScale3D(FVector(.15f,8,8));
        Before=Enemies[0]->CurrentHP;Player->StartFireFromAutomation();Player->StopFireFromAutomation();Stage=32;Age=0;return;
    }
    if(Stage==32&&Age>.5)
    {
        Check(TEXT("melee_cannot_hit_through_wall"),Enemies[0]->CurrentHP==Before);ClearTargets();Enemy(FVector(1800,0,0));
        Player->StartFireFromAutomation();Player->StopFireFromAutomation();Player->StartSecondaryAttack();BeforeWaves=F->WaveSerial;Stage=33;Age=0;return;
    }
    if(Stage==33&&Age>.7)
    {
        Check(TEXT("secondary_queued_after_slash"),F->WaveSerial==BeforeWaves+1);Player->StopSecondaryAttack();
        Player->ResetForTest();Check(TEXT("restart_clears_wave"),!F->Busy()&&F->Remaining()==0);Player->SetActorLocation(Start);Player->GetCharacterMovement()->DisableMovement();
        Stage=34;Age=0;return;
    }
    if(Stage==34&&Age>3.2f)
    {
        F->Begin(W->Settings);Player->SetCanBeDamaged(true);FDamageEvent Event;Player->TakeDamage(10000,Event,nullptr,Enemies[0]);
        Check(TEXT("death_clears_wave_immediately"),Player->IsDead()&&!F->Busy()&&F->Remaining()==0);
        Player->ResetForTest();Player->SetCanBeDamaged(false);Player->SetActorLocation(Start);Player->GetCharacterMovement()->DisableMovement();
        if(HUD){HUD->ConfirmRestart();HUD->StartSelectedRun();}if(auto* D=AHeliosAdventureDirector::Find(GetWorld()))D->SetActorTickEnabled(false);
        UGameplayStatics::SetGamePaused(this,false);Player->SetActorLocation(Start);Player->GetCharacterMovement()->DisableMovement();
        ClearTargets();Enemy(FVector(2400,0,0));BeforeHits=F->HitsTotal;Stage=100;Age=0;return;
    }
    if(Stage==100)
    {
        if(!F->Busy())
        {
            if(Round==100){Stage=101;Age=0;return;}
            W=Cast<AHeliosQingxiao>(Player->GetEquippedWeapon());const bool Began=W&&F->Begin(W->Settings);if(!Began){Check(TEXT("stress_can_start_round"),false);Finish();return;}++Round;
            Check(FString::Printf(TEXT("stress_%03d_five_swords"),Round),F->Remaining()==5);
        }
        int32 Count=0;for(TActorIterator<AHeliosQingxiaoSword> S(GetWorld());S;++S)if(!S->IsActorBeingDestroyed())++Count;
        if(Count>5){Check(TEXT("stress_actor_count_bounded"),false);Finish();}return;
    }
    if(Stage==101&&Age>.7)
    {
        int32 Impacts=0;for(TActorIterator<AHeliosQingxiaoImpact> I(GetWorld());I;++I)++Impacts;
        for(TActorIterator<AHeliosQingxiaoBurst> I(GetWorld());I;++I)++Impacts;
        Check(TEXT("100_waves_rends_match_hits_and_expire"),F->RendTotal==F->HitsTotal&&Impacts==0);
        int32 Swords=0,Ribbons=0;for(TActorIterator<AHeliosQingxiaoSword> S(GetWorld());S;++S)++Swords;for(TActorIterator<AHeliosQingxiaoRibbon> R(GetWorld());R;++R)++Ribbons;
        Check(TEXT("100_waves_exactly_500_hits"),F->HitsTotal-BeforeHits==500);
        Check(TEXT("100_waves_no_sword_or_trail_residue"),Swords==0&&Ribbons==0&&!F->Seal&&F->Remaining()==0);Check(TEXT("100_waves_effect_pool_returns_to_baseline"),UHeliosWeaponVFX::GetTrackedCount(this)==0);Finish();
    }
}
