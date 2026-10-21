#include "QA/HeliosSoulBannerQA.h"
#include "Weapons/HeliosSoulBanner.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Weapons/HeliosFirstPersonPresentationComponent.h"
#include "Adventure/HeliosAdventureCharacter.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "UI/HeliosAdventureHUD.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "AI/ShooterAIController.h"
#include "EngineUtils.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/DamageType.h"
#include "Engine/DamageEvents.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "ShaderCompiler.h"
#include "Town/HeliosTownSubsystem.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"

AHeliosSoulBannerQA::AHeliosSoulBannerQA(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void AHeliosSoulBannerQA::Check(const FString& N,bool P){if(!P)++Failures;if(!Results.IsEmpty())Results+=TEXT(",\n");Results+=FString::Printf(TEXT("{\"name\":\"%s\",\"passed\":%s}"),*N,P?TEXT("true"):TEXT("false"));UE_LOG(LogTemp,Display,TEXT("SOUL_CHECK %s %s"),*N,P?TEXT("PASS"):TEXT("FAIL"));}
void AHeliosSoulBannerQA::Capture(const FString& N)
{
    // A named still must not replace the screenshot already queued for this video frame.
    if(FParse::Param(FCommandLine::Get(),TEXT("SoulCapture"))&&!N.StartsWith(TEXT("frames/"))&&CaptureFrame>0){CaptureStills.Add(N,CaptureFrame-1);return;}
    auto Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/soulbanner")/(N+TEXT(".png")));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);FScreenshotRequest::RequestScreenshot(Path,true,false);
}
void AHeliosSoulBannerQA::Finish()
{
    FString Stills=TEXT("{");bool First=true;
    for(const auto& Pair:CaptureStills){if(!First)Stills+=TEXT(",");First=false;Stills+=FString::Printf(TEXT("\"%s\":%d"),*Pair.Key,Pair.Value);}
    Stills+=TEXT("}");FFileHelper::SaveStringToFile(Stills,*(FPaths::ProjectDir()/TEXT("../qa/soulbanner/capture_stills.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    CombatFrameMilliseconds.Sort();
    double Sum=0;for(float Ms:CombatFrameMilliseconds)Sum+=Ms;
    const int32 Count=CombatFrameMilliseconds.Num();
    const double Mean=Count?Sum/Count:0;
    const double P95=Count?CombatFrameMilliseconds[FMath::Min(Count-1,FMath::CeilToInt(Count*.95f)-1)]:0;
    FString Report=FString::Printf(TEXT("{\"status\":\"%s\",\"failures\":%d,\"checks\":[%s],\"capture_frames\":%d,\"performance\":{\"samples\":%d,\"mean_ms\":%.4f,\"p95_ms\":%.4f}}"),Failures?TEXT("failed"):TEXT("passed"),Failures,*Results,CaptureFrame,Count,Mean,P95);
    FFileHelper::SaveStringToFile(Report,*(FPaths::ProjectDir()/TEXT("../qa/soulbanner/runtime_report.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);Stage=99;FPlatformMisc::RequestExit(false);
}
void AHeliosSoulBannerQA::Tick(float Dt)
{
    Super::Tick(Dt);if(Stage==99)return;if(!Started)Started=FPlatformTime::Seconds();if(FPlatformTime::Seconds()-Started>420){Check(TEXT("timeout"),false);Finish();return;}
    if(!Player)Player=Cast<AHeliosAdventureCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));auto* Director=AHeliosAdventureDirector::Find(GetWorld());if(!Player||!Director)return;
    auto* PC=Cast<APlayerController>(Player->GetController());auto* HUD=PC?Cast<AHeliosAdventureHUD>(PC->GetHUD()):nullptr;if(!HUD)return;
    Age+=Dt;auto* W=Cast<AHeliosSoulBanner>(Player->GetEquippedWeapon());auto* Army=UHeliosSoulArmy::Get(Player);
    const bool ArtCapture=FParse::Param(FCommandLine::Get(),TEXT("SoulArtCapture"));
    if(((Stage>=3&&Stage<=6)||Stage==45||(Stage>=80&&Stage<=84))&&FParse::Param(FCommandLine::Get(),TEXT("SoulCapture"))&&++IntroFrames%(ArtCapture?1:3)==0)Capture(FString::Printf(TEXT("frames/frame_%05d"),CaptureFrame++));
    if(Stage>=82&&Stage<=84)
    {
        if(Army->Flags.IsEmpty()){Check(TEXT("showcase_has_flag"),false);Finish();return;}
        auto* Flag=Army->Flags[0].Get();
        if(Stage==84){const FVector Focus=Flag->GetActorLocation()+FVector(0,0,135);Player->SetActorLocation(Focus+FVector(-230,-230,-100));PC->SetControlRotation((Focus-Player->FirstPersonCamera->GetComponentLocation()).Rotation());}
        else
        {
            auto* Soldier=Flag->Soldiers[Stage==82?0:2].Get();
            const FVector Focus=Soldier->GetActorLocation()+Soldier->GetActorForwardVector()*55;
            Player->SetActorLocation(Soldier->GetActorLocation()+FVector(-120,-330,-40));
            PC->SetControlRotation((Focus-Player->FirstPersonCamera->GetComponentLocation()).Rotation());
        }
        if(Age>(Stage==84?3.f:4.f))
        {
            Capture(Stage==82?TEXT("06_warrior_close"):Stage==83?TEXT("07_mage_close"):TEXT("08_banner_close"));
            if(Stage==84){Stage=6;bCloseupsDone=true;Player->SetActorLocation(StartPosition);PC->SetControlRotation(FRotator::ZeroRotator);}
            else ++Stage;Age=0;
        }
        return;
    }
    if(Stage==80&&Age>1.5f)
    {
        Player->SetActorLocation(StartPosition);PC->SetControlRotation(FRotator::ZeroRotator);
        for(TActorIterator<ADirectionalLight> L(GetWorld());L;++L)if(auto* C=Cast<UDirectionalLightComponent>(L->GetLightComponent()))C->SetIntensity(.15f);
        Player->FirstPersonCamera->PostProcessSettings.bOverride_AutoExposureBias=true;
        Player->FirstPersonCamera->PostProcessSettings.AutoExposureBias=-1.8f;
        Player->FirstPersonCamera->PostProcessBlendWeight=1;Stage=81;Age=0;return;
    }
    if(Stage==81&&Age>2.f){Player->StopFireFromAutomation();Capture(TEXT("05_dark_flag"));Finish();return;}
    if(Stage==0){HUD->SelectWeapon(TEXT("soulbanner"));Stage=1;Age=0;return;}
    if(Stage==1&&Age>1.5){Capture(TEXT("01_menu"));Stage=2;Age=0;return;}
    if(Stage==2&&Age>.3)
    {HUD->StartSelectedRun();Stage=20;Age=0;return;}
    if(Stage==20)
    {
        if(UHeliosTownSubsystem::IsTownMap(GetWorld()))if(auto* Town=GetWorld()->GetSubsystem<UHeliosTownSubsystem>())
        {
            if(!Town->IsWorldReady())return;
            // Keep day/dark comparisons on the same open street, independent of random arrival.
            if(!bTownFixturePlaced){bTownFixturePlaced=true;const bool Moved=Town->TeleportToCheckpoint(TEXT("business"));Check(TEXT("fixture_town_open_street"),Moved);if(!Moved)Finish();return;}
        }
        if(Director->IsSelectingWeapon())
        {
            const bool StartedRun=Director->StartRun(TEXT("soulbanner"));Check(TEXT("fixture_starts_ready_run"),StartedRun);
            if(!StartedRun)Finish();return;
        }
        // Town arrival restores the saved loadout after the menu selection.
        // Equip the fixture only after that asynchronous arrival has finished.
        if(Player->GetWeaponId()!=TEXT("soulbanner"))Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("soulbanner")));
        W=Cast<AHeliosSoulBanner>(Player->GetEquippedWeapon());
        if(!W){Check(TEXT("fixture_has_soul_weapon"),false);Finish();return;}
        Director->SetActorTickEnabled(false);Player->SetCanBeDamaged(false);StartPosition=Player->GetActorLocation();PC->SetControlRotation(FRotator::ZeroRotator);
        Check(TEXT("selection_equips_soulbanner"),Player->GetWeaponId()==TEXT("soulbanner"));Check(TEXT("first_person_assets_ready"),Player->GetPresentation()&&Player->GetPresentation()->IsReady());
        if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
        if(FParse::Param(FCommandLine::Get(),TEXT("SoulDarkLighting")))
        {
            for(TActorIterator<ADirectionalLight> L(GetWorld());L;++L)if(auto* C=Cast<UDirectionalLightComponent>(L->GetLightComponent()))C->SetIntensity(.15f);
            Player->FirstPersonCamera->PostProcessSettings.bOverride_AutoExposureBias=true;
            Player->FirstPersonCamera->PostProcessSettings.AutoExposureBias=-1.8f;
            Player->FirstPersonCamera->PostProcessBlendWeight=1;
        }
        for(TActorIterator<AHeliosEnemyActor> E(GetWorld());E;++E)E->Destroy();
        for(FVector Offset:{FVector(320,0,0),FVector(800,450,0),FVector(850,-450,0)})
        {
            FActorSpawnParameters S;S.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;auto* E=GetWorld()->SpawnActor<AHeliosEnemyActor>(StartPosition+Offset,FRotator(0,180,0),S);
            E->CurrentHP=10000;E->SetActorTickEnabled(false);E->GetCharacterMovement()->DisableMovement();if(auto* AI=Cast<AAIController>(E->GetController()))if(AI->GetBrainComponent())AI->GetBrainComponent()->StopLogic(TEXT("Soul QA target"));Enemies.Add(E);
        }
        Stage=3;Age=0;return;
    }
    if(Stage==3&&Age>(ArtCapture?2.f:1.f)){Capture(TEXT("02_equipped"));Before=Enemies[0]->CurrentHP;Player->StartFireFromAutomation();Stage=4;Age=0;return;}
    if(Stage==4&&Age>(ArtCapture?3.8f:1.6f))
    {
        Player->StopFireFromAutomation();Check(TEXT("three_combo_damage"),Enemies[0]->CurrentHP<Before);Check(TEXT("infinite_ammo"),W&&!W->UsesAmmunition());W->CancelReload();
        Enemies[0]->SetActorLocation(StartPosition+FVector(1000,0,0));PC->SetControlRotation(FRotator::ZeroRotator);Stage=45;Age=0;return;
    }
    if(Stage==45&&Age>.3){Player->StartSecondaryAttack();Check(TEXT("right_click_begins_plant"),W->State==2);Stage=5;Age=0;return;}
    if(Stage==5&&Age>1.25)
    {
        Check(TEXT("right_click_creates_one_flag_three_skeletons"),Army->FlagCount()==1&&Army->SoldierCount()==3);Check(TEXT("cooldown_starts_on_success"),Army->Cooldown()>2);
        Player->StartSecondaryAttack();Check(TEXT("cooldown_blocks_repeat"),W->State==0&&Army->FlagCount()==1);
        if(FParse::Param(FCommandLine::Get(),TEXT("SoulFlagOnly"))){Player->SetActorLocation(StartPosition+FVector(-160,0,0));PC->SetControlRotation(FRotator(-12,0,0));}
        Capture(TEXT("03_summon"));Stage=6;Age=0;return;
    }
    if(Stage==6&&Age>2)
    {
        if(FParse::Param(FCommandLine::Get(),TEXT("SoulCloseups"))&&!bCloseupsDone){Stage=82;Age=0;return;}
        if(FParse::Param(FCommandLine::Get(),TEXT("SoulFlagOnly")))
        {
            for(auto Flag:Army->Flags)if(IsValid(Flag))Flag->Dismiss();
            Check(TEXT("flag_dismisses_cleanly"),Army->FlagCount()==0);
            Stage=80;Age=0;return;
        }
        Check(TEXT("skeleton_has_mesh_and_animation"),Army->FlagCount()>0&&Army->Flags[0]->Soldiers[0]->GetMesh()->GetSkeletalMeshAsset()&&Army->Flags[0]->Soldiers[0]->Animations.Num()>=6);
        if(Army->FlagCount()>0)
        {
            auto* Warrior=Army->Flags[0]->Soldiers[0].Get();auto* Mage=Army->Flags[0]->Soldiers[2].Get();
            Check(TEXT("warrior_has_alternate_attack"),Warrior->Animations.FindRef(TEXT("Attack2"))!=nullptr);
            Check(TEXT("mage_has_charge_visual"),Mage->Charge&&Mage->Charge->GetStaticMesh());
            Warrior->AttackAge=.30f;Warrior->UpdateAttackVisuals();Warrior->ClearAttackVisuals();
            Check(TEXT("attack_cancel_cleans_ribbon"),!IsValid(Warrior->AttackTrail));Warrior->AttackAge=-1;
        }
        if(Army->FlagCount()==0){Finish();return;}
        // Isolated combat fixtures restore the live summon scenario afterwards.
        {
            auto* Soldier=Army->Flags[0]->Soldiers[0].Get();auto* Flag=Army->Flags[0].Get();
            auto* Mage=Flag->Soldiers[2].Get();auto* Enemy=Enemies[0].Get();
            const FVector SavedSoldierPosition=Soldier->GetActorLocation(),FlagOrigin=Flag->GetActorLocation();
            // Use exact centimetres for the inclusive 500 cm boundary fixture.
            const FVector Origin(FMath::RoundToDouble(SavedSoldierPosition.X),FMath::RoundToDouble(SavedSoldierPosition.Y),FMath::RoundToDouble(SavedSoldierPosition.Z));
            Soldier->SetActorLocation(Origin);
            const FRotator Rotation=Soldier->GetActorRotation();TArray<FVector> EnemyPositions;
            for(auto E:Enemies)EnemyPositions.Add(E->GetActorLocation());
            Soldier->SetActorRotation(FRotator::ZeroRotator);Soldier->Target=Enemy;
            for(float Distance:{499.f,500.f,501.f})
            {
                Enemy->SetActorLocation(Origin+FVector(Distance,0,0));
                Check(FString::Printf(TEXT("skeleton_range_%.0fcm"),Distance),Soldier->CanMeleeHit(Enemy)==(Distance<=500));
            }
            for(float Height:{179.f,180.f})
            {
                Enemy->SetActorLocation(Origin+FVector(300,0,Height));
                Check(FString::Printf(TEXT("skeleton_height_%.0fcm"),Height),Soldier->CanMeleeHit(Enemy)==(Height<180));
            }
            Enemy->SetActorLocation(Origin+FVector(-300,0,0));Check(TEXT("skeleton_rejects_behind"),!Soldier->CanMeleeHit(Enemy));
            Enemy->SetActorLocation(Origin+FVector(300,0,0));
            auto* Blocker=GetWorld()->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Blocker);Blocker->SetRootComponent(Box);
            Box->SetCanEverAffectNavigation(false);Box->SetBoxExtent(FVector(10,100,200));Box->SetCollisionProfileName(TEXT("BlockAll"));Box->SetWorldLocation(Origin+FVector(150,0,0));Box->RegisterComponent();
            Check(TEXT("skeleton_wall_blocks_damage"),!Soldier->CanMeleeHit(Enemy));Blocker->Destroy();
            Soldier->Age=10;Soldier->AttackAge=0;Soldier->bCommitted=false;Soldier->Cooldown=1.2f;Soldier->Target=Enemy;
            const float BeforeHit=Enemy->CurrentHP;
            for(int I=0;I<10;++I)Soldier->Tick(.1f);
            Check(TEXT("skeleton_one_hit_per_swing"),Enemy->CurrentHP==BeforeHit-25);
            Soldier->AttackAge=.1f;Soldier->Target=Enemy;Soldier->Decision=-1;
            Soldier->Tick(.01f);Check(TEXT("skeleton_locks_target_during_swing"),Soldier->Target.Get()==Enemy);
            Enemy->SetActorLocation(FlagOrigin+FVector(5100,0,0));Soldier->Tick(.01f);
            Check(TEXT("skeleton_cancels_target_outside_flag_50m"),Soldier->AttackAge<0&&Soldier->Target.Get()!=Enemy);
            // Melee radius is independent of the mage's original 20m flag radius.
            Flag->SetActorLocation(Origin-FVector(4500,0,0));Enemy->SetActorLocation(Origin+FVector(400,0,0));
            Check(TEXT("skeleton_flag_radius_expanded_to_50m"),Soldier->CanMeleeHit(Enemy));
            Check(TEXT("mage_keeps_original_flag_radius"),Mage->Role==2&&Flag->Radius==2000.f);
            Flag->SetActorLocation(FlagOrigin);
            for(int I=0;I<Enemies.Num();++I)Enemies[I]->SetActorLocation(EnemyPositions[I]);
            Soldier->CancelMeleeAttack();Soldier->SetActorLocation(SavedSoldierPosition);Soldier->SetActorRotation(Rotation);
            Army->ObservePlayerDamage(Enemy,PC);Check(TEXT("player_damage_is_recorded"),Army->RecentDamageTarget.Get()==Enemy);
            UGameplayStatics::ApplyDamage(Enemies[1],1.f,PC,Soldier,UDamageType::StaticClass());
            Check(TEXT("summon_damage_does_not_replace_player_priority"),Army->RecentDamageTarget.Get()==Enemy);
            Soldier->Decide();Check(TEXT("skeleton_prefers_recent_player_damage"),Soldier->Target.Get()==Enemy);
            Army->RecentDamageTime=GetWorld()->GetTimeSeconds()-6.;Army->PreferredTarget=Enemies.Last();
            Soldier->Decide();
            AHeliosEnemyActor* Expected=nullptr;double Best=1.e30;
            for(auto E:Enemies)
            {
                auto* EnemyAI=Cast<AShooterAIController>(E->GetController());
                const bool Threat=EnemyAI&&(EnemyAI->GetCurrentTarget()==Player||EnemyAI->GetCurrentTarget()==Soldier);
                const double Score=(Threat?1.e9:2.e9)+FVector::DistSquared2D(Origin,E->GetActorLocation());
                auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Origin,E->GetActorLocation(),Soldier);
                if(Score<Best&&Path&&Path->IsValid()&&!Path->IsPartial()){Expected=E;Best=Score;}
            }
            Check(TEXT("expired_damage_uses_threat_then_nearest_not_crosshair"),Expected&&Soldier->Target.Get()==Expected);
            for(auto E:Enemies)E->SetActorLocation(FlagOrigin+FVector(6000,0,0));Soldier->CancelMeleeAttack();Soldier->Decide();
            Check(TEXT("skeleton_no_target_returns_to_flag"),!Soldier->Target.IsValid());
            for(int I=0;I<Enemies.Num();++I)Enemies[I]->SetActorLocation(EnemyPositions[I]);
            Soldier->CancelMeleeAttack();Soldier->Cooldown=0;Army->RecentDamageTarget.Reset();
        }
        auto S=Army->Flags[0]->Soldiers[0];const float HP=S->Health;FDamageEvent Event;S->TakeDamage(50,Event,PC,Player);Check(TEXT("player_cannot_damage_summons"),S->Health==HP);
        S->TakeDamage(1000,Event,nullptr,Enemies[0]);Check(TEXT("summon_can_die"),!S->Alive());Check(TEXT("no_auto_replenishment"),Army->SoldierCount()==2);
        Army->Clear();
        FVector Ground;TArray<FVector> Slots;
        const FVector Saved=Player->GetActorLocation();Player->SetActorLocation(Saved+FVector(0,0,1400));
        Check(TEXT("no_ground_rejects_placement_without_cooldown"),!Army->FindPlacement(Ground,Slots)&&Army->Cooldown()==0);Player->SetActorLocation(Saved);
        PC->SetControlRotation(FRotator::ZeroRotator);W->CancelReload();W->Combo=0;Enemies[0]->SetActorLocation(Player->GetActorLocation()+FVector(300,0,0));
        float TestHP=Enemies[0]->CurrentHP;W->StartFiring();W->Tick(.38f);W->Tick(.03f);W->StopFiring();
        Check(TEXT("one_damage_application_per_swing"),FMath::IsNearlyEqual(TestHP-Enemies[0]->CurrentHP,100.f));W->CancelReload();
        auto* Wall=GetWorld()->SpawnActor<AActor>();auto* WallBox=NewObject<UBoxComponent>(Wall);Wall->SetRootComponent(WallBox);WallBox->SetCanEverAffectNavigation(false);WallBox->SetBoxExtent(FVector(15,600,600));WallBox->SetCollisionProfileName(TEXT("BlockAll"));WallBox->SetWorldLocation(Player->GetActorLocation()+FVector(145,0,0));WallBox->RegisterComponent();
        TestHP=Enemies[0]->CurrentHP;W->Combo=0;W->StartFiring();W->Tick(.4f);W->StopFiring();Check(TEXT("wall_blocks_melee_damage"),Enemies[0]->CurrentHP==TestHP);W->CancelReload();
        Check(TEXT("wall_rejects_placement"),!Army->FindPlacement(Ground,Slots));Wall->Destroy();Enemies[0]->SetActorLocation(StartPosition+FVector(1000,0,0));
        for(int32 I=0;I<100&&Army->FlagCount()<10;++I){Player->SetActorLocation(StartPosition+FVector((I%10-4)*160,(I/10-4)*160,0));PC->SetControlRotation(FRotator(0,0,0));Army->ReadyAt=0;Army->Plant(W->Settings);}
        Check(TEXT("ten_flags_thirty_skeletons"),Army->FlagCount()==10&&Army->SoldierCount()==30);
        if(Army->FlagCount())
        {
            auto* F=Army->Flags[0].Get();Check(TEXT("two_melee_one_mage"),F->Soldiers.Num()==3&&F->Soldiers[0]->Role==0&&F->Soldiers[1]->Role==1&&F->Soldiers[2]->Role==2);
            if(auto* AI=Cast<AShooterAIController>(Enemies[0]->GetController())){AI->ReportAwarenessThreat(F->Soldiers[0],F->Soldiers[0]->GetActorLocation(),true);Check(TEXT("enemy_accepts_summon_as_threat"),AI->GetCurrentTarget()==F->Soldiers[0]);}
        }
        TWeakObjectPtr<AHeliosSoulFlag> Old=Army->Flags.Num()?Army->Flags[0]:nullptr;for(int32 I=0;I<50&&Old.IsValid()&&!Old->IsActorBeingDestroyed();++I){Player->SetActorLocation(StartPosition+FVector((I%10-4)*180,500+(I/10)*180,0));PC->SetControlRotation(FRotator(0,0,0));Army->ReadyAt=0;Army->Plant(W->Settings);}
        Check(TEXT("eleventh_replaces_oldest"),Army->FlagCount()==10&&(!Old.IsValid()||Old->IsActorBeingDestroyed()));
        const float CD=Army->Cooldown();Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("rifle")));Check(TEXT("switch_preserves_army_and_cooldown"),Army->FlagCount()==10&&Army->Cooldown()>=CD-.01f);
        Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("soulbanner")));Player->SetActorLocation(StartPosition+FVector(-650,-750,0));PC->SetControlRotation(FRotator(-5,40,0));Stage=7;Age=0;MeasureStarted=FPlatformTime::Seconds();return;
    }
    if(Stage==7)
    {
        ++Frames;
        if(Age>2.f&&!FParse::Param(FCommandLine::Get(),TEXT("SoulCapture"))){CombatFrameMilliseconds.Add(Dt*1000.f);}
        if(FParse::Param(FCommandLine::Get(),TEXT("SoulCapture"))&&Frames%(ArtCapture?1:3)==0){Capture(FString::Printf(TEXT("frames/frame_%05d"),CaptureFrame++));}
        if(Frames==240)Capture(TEXT("04_ten_flags"));
        if(Age>8)
        {
            int32 Hits=0;for(auto F:Army->Flags)if(IsValid(F))for(auto S:F->Soldiers)if(IsValid(S))Hits+=S->HitsDelivered;
            if(Hits==0&&Army->Flags.Num())for(auto S:Army->Flags[0]->Soldiers)if(IsValid(S))for(auto E:Enemies)if(IsValid(E)){auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),S->GetActorLocation(),E->GetActorLocation(),S);UE_LOG(LogTemp,Warning,TEXT("SOUL_AI_DIAG role=%d from=%s to=%s path=%d partial=%d target=%s"),S->Role,*S->GetActorLocation().ToString(),*E->GetActorLocation().ToString(),Path&&Path->IsValid(),Path&&Path->IsPartial(),*GetNameSafe(S->Target.Get()));}
            Check(TEXT("summons_attack_enemies"),Hits>0);Check(TEXT("bounded_army_during_combat"),Army->FlagCount()<=10&&Army->SoldierCount()<=30);
            UE_LOG(LogTemp,Display,TEXT("SOUL_PERF frames=%d wall_seconds=%.2f game_seconds=%.2f"),Frames,FPlatformTime::Seconds()-MeasureStarted,Age);
            if(Army->FlagCount())Before=Army->Flags[0]->Remaining;Army->ReadyAt=GetWorld()->GetTimeSeconds()+2;PauseCooldown=Army->Cooldown();UGameplayStatics::SetGamePaused(this,true);Stage=71;MeasureStarted=FPlatformTime::Seconds();
        }return;
    }
    if(Stage==71&&FPlatformTime::Seconds()-MeasureStarted>.3)
    {
        Check(TEXT("pause_freezes_lifetime_and_cooldown"),Army->FlagCount()>0&&Army->Flags[0]->Remaining==Before&&FMath::IsNearlyEqual(Army->Cooldown(),PauseCooldown));UGameplayStatics::SetGamePaused(this,false);
        if(Army->FlagCount()){int32 Count=Army->FlagCount();Army->Flags[0]->Remaining=.001f;Army->Flags[0]->Tick(.01f);Check(TEXT("expiry_removes_flag_and_soldiers"),Army->FlagCount()==Count-1);}
        Player->SetCanBeDamaged(true);FDamageEvent Event;Player->TakeDamage(10000,Event,nullptr,Enemies[0]);Army->TickComponent(.2f,LEVELTICK_All,nullptr);Check(TEXT("player_death_cleans_army"),Player->IsDead()&&Army->FlagCount()==0);
        Player->ResetForTest();Check(TEXT("restart_cleans_army"),Army->FlagCount()==0&&Army->SoldierCount()==0&&Army->Cooldown()==0);Stage=8;Age=0;
    }
    if(Stage==8&&Age>.5)Finish();
}
