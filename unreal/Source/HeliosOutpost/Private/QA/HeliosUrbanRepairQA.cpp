#include "QA/HeliosUrbanQA.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "Boss/HeliosUrbanBoss.h"
#include "Boss/HeliosUrbanHazard.h"
#include "Weapons/HeliosBasketballProjectile.h"
#include "Weapons/HeliosZhongXiaoKui.h"
#include "Weapons/HeliosCalamityStaff.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Player/HeliosArenaCharacter.h"
#include "UI/HeliosAdventureHUD.h"
#include "Pets/HeliosPetDog.h"
#include "Pets/HeliosUltimatumPet.h"
#include "Pets/HeliosSunShangXiangPet.h"
#include "Pets/HeliosZhongXiaoKuiPet.h"
#include "Engine/SkeletalMesh.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Components/PrimitiveComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Variant_Shooter/Weapons/ShooterProjectile.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "InputKeyEventArgs.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
bool HasSkipLabel(const TSharedRef<SWidget>& Widget)
{
    if(Widget->GetTypeAsString()==TEXT("STextBlock") && StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString()==TEXT("跳过本回合"))return true;
    FChildren* Children=Widget->GetChildren();
    for(int32 Index=0;Children && Index<Children->Num();++Index)if(HasSkipLabel(Children->GetChildAt(Index)))return true;
    return false;
}
TSharedPtr<SWidget> FindSkipButton(const TSharedRef<SWidget>& Widget)
{
    if(Widget->GetTypeAsString()==TEXT("SButton") && HasSkipLabel(Widget))return Widget;
    FChildren* Children=Widget->GetChildren();
    for(int32 Index=0;Children && Index<Children->Num();++Index)
        if(auto Found=FindSkipButton(Children->GetChildAt(Index)))return Found;
    return nullptr;
}
bool ClickActualSkipButton()
{
    if(!FSlateApplication::IsInitialized())return false;
    auto& Slate=FSlateApplication::Get();TSharedPtr<SWidget> Button;
    for(const auto& Window:Slate.GetInteractiveTopLevelWindows())if((Button=FindSkipButton(Window->GetContent())))break;
    if(!Button || !Button->IsEnabled())return false;
    FWidgetPath Path;if(!Slate.GeneratePathToWidgetUnchecked(Button.ToSharedRef(),Path))return false;
    if(!Path.IsValid() || Path.Widgets.Num()==0)return false;
    const FGeometry Geometry=Button->GetCachedGeometry();
    const FVector2D Position=Geometry.LocalToAbsolute(Geometry.GetLocalSize()*.5f);
    TSet<FKey> Down;Down.Add(EKeys::LeftMouseButton);TSet<FKey> Up;
    const FPointerEvent Press(FSlateApplication::CursorPointerIndex,Position,Position,Down,EKeys::LeftMouseButton,0,FModifierKeysState());
    const FPointerEvent Release(FSlateApplication::CursorPointerIndex,Position,Position,Up,EKeys::LeftMouseButton,0,FModifierKeysState());
    Slate.ReleaseAllPointerCapture();
    Button->OnMouseEnter(Geometry,Press);
    // Exercise the actual on-screen SButton and its Slate capture reply. An
    // offscreen window has no native focus path for RoutePointerDownEvent.
    const FReply Pressed=Button->OnMouseButtonDown(Geometry,Press);
    Slate.ProcessReply(Path,Pressed,&Path,&Press);
    const FReply Released=Button->OnMouseButtonUp(Geometry,Release);
    Slate.ProcessReply(Path,Released,&Path,&Release);
    return Pressed.IsEventHandled()&&Released.IsEventHandled();
}
}

void AHeliosUrbanQA::RepairCheck(const FString& Name,bool Passed)
{
    auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("name"),Name); Row->SetBoolField(TEXT("passed"),Passed);
    RepairChecks.Add(MakeShared<FJsonValueObject>(Row));
    UE_LOG(LogTemp,Display,TEXT("URBAN_REPAIR_CHECK %s %s"),*Name,Passed?TEXT("PASS"):TEXT("FAIL"));
}

void AHeliosUrbanQA::FinishResumedFixes()
{
    auto Report=MakeShared<FJsonObject>(); bool Passed=!RepairChecks.IsEmpty();
    for(auto Row:RepairChecks)Passed &= Row->AsObject()->GetBoolField(TEXT("passed"));
    Report->SetBoolField(TEXT("passed"),Passed);Report->SetStringField(TEXT("mode"),RepairMode);
    Report->SetStringField(TEXT("map"),GetWorld()->GetMapName());Report->SetStringField(TEXT("weapon"),CombatWeapon);
    Report->SetStringField(TEXT("scope"),TEXT("Isolated QA fixture with game-input events and actual rendering. Injected setup/damage in skip tests; passive high-health visual targets, invulnerable player, optional forced spell setup. Images require separate visual review; no normal combat verdict."));
    Report->SetArrayField(TEXT("checks"),RepairChecks);Report->SetArrayField(TEXT("samples"),RepairSamples);
    FString Text;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));
    const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/urban_challenge/resumed_fixes_20261005/")+RepairRunName);
    IFileManager::Get().MakeDirectory(*Directory,true);FFileHelper::SaveStringToFile(Text,*(Directory/TEXT("report.json")));
    bFinished=true;FPlatformMisc::RequestExit(false);
}

void AHeliosUrbanQA::TickResumedFixes(float DeltaSeconds)
{
    auto* D=AHeliosAdventureDirector::Find(GetWorld());
    auto* P=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));
    auto* PC=UGameplayStatics::GetPlayerController(this,0);
    auto* HUD=PC?Cast<AHeliosAdventureHUD>(PC->GetHUD()):nullptr;
    if(!D || !P || !PC || !HUD)return;
    const double Now=FPlatformTime::Seconds();
    if(Now-Started>150){RepairCheck(TEXT("wall_timeout"),false);FinishResumedFixes();return;}
    if(Now-Started<5)return;
    auto Key=[&](FKey K,bool Down){PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),K,Down?IE_Pressed:IE_Released,FPlatformTime::Cycles64()));};
    auto Release=[&](){for(FKey K:{EKeys::LeftMouseButton,EKeys::RightMouseButton,EKeys::G,EKeys::Q,EKeys::Tab})Key(K,false);};
    if(RepairMode==TEXT("pets"))
    {
        RepairAge+=DeltaSeconds;
        const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/urban_challenge/resumed_fixes_20261005/")+RepairRunName);
        auto Capture=[&](const TCHAR* Label){FScreenshotRequest::RequestScreenshot(Directory/FString(Label)+TEXT(".png"),true,false);};
        const bool Baseline=FParse::Param(FCommandLine::Get(),TEXT("UrbanRepairBaseline"));
        if(Stage==0)
        {
            HUD->SelectWeapon(TEXT("rifle"));HUD->StartSelectedRun();D->SetActorTickEnabled(false);D->CleanupOwnedActors();P->SetCanBeDamaged(false);
            P->SetActorLocation(FVector(0,-4500,102),false,nullptr,ETeleportType::TeleportPhysics);
            FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            const TSubclassOf<AHeliosPetCompanion> Classes[]={AHeliosUltimatumPet::StaticClass(),AHeliosSunShangXiangPet::StaticClass(),AHeliosZhongXiaoKuiPet::StaticClass()};
            for(int32 Index=0;Index<3;++Index)
            {
                auto* Pet=GetWorld()->SpawnActor<AHeliosPetCompanion>(Classes[Index],FVector((Index-1)*260,-2800,104),FRotator(0,-90,0),Params);
                RepairPets.Add(Pet);Pet->SetActorTickEnabled(false);Pet->SetCanBeDamaged(false);
                if(Baseline&&Index>0){Pet->GetCapsuleComponent()->SetCapsuleHalfHeight(90);Pet->GetMesh()->SetRelativeLocation(FVector(0,0,-90));Pet->GetMesh()->SetRelativeScale3D(FVector(1.7));Pet->SetActorLocation(FVector((Index-1)*260,-2800,92));}
                Pet->SetPose(TEXT("Idle"),true);RepairCheck(FString::Printf(TEXT("pet_%d_model_ready"),Index),Pet->bModelReady);
            }
            DemoCamera=GetWorld()->SpawnActor<ACameraActor>(FVector(0,-3750,135),FRotator(0,90,0));DemoCamera->GetCameraComponent()->SetFieldOfView(60);PC->SetViewTarget(DemoCamera);
            Stage=1;RepairAge=0;return;
        }
        if(Stage==1&&RepairAge>4)
        {
            for(int32 Index=0;Index<RepairPets.Num();++Index)
            {
                auto* Pet=RepairPets[Index].Get();auto* Mesh=Pet->GetMesh();auto* Model=Mesh->GetSkeletalMeshAsset();const auto Bound=Model->GetBounds();
                const float Height=2*Bound.BoxExtent.Z*Mesh->GetComponentScale().Z;
                FHitResult Ground;FCollisionQueryParams Query(SCENE_QUERY_STAT(PetGroundQA),false,Pet);
                GetWorld()->LineTraceSingleByChannel(Ground,Pet->GetActorLocation(),Pet->GetActorLocation()-FVector(0,0,250),ECC_Visibility,Query);
                const float Bottom=Pet->GetActorLocation().Z-Pet->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
                RepairCheck(FString::Printf(TEXT("pet_%d_capsule_grounded"),Index),Ground.bBlockingHit&&FMath::Abs(Bottom-Ground.ImpactPoint.Z)<5);
                RepairCheck(FString::Printf(TEXT("pet_%d_reference_body_height"),Index),Baseline||FMath::Abs(Height-203.3593f)<.01f);
                auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Pet->GetActorLocation(),Pet->GetActorLocation()+FVector(400,0,0),Pet);
                RepairCheck(FString::Printf(TEXT("pet_%d_navigation_path"),Index),Path&&Path->IsValid()&&!Path->IsPartial());
                auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("pet_index"),Index);Row->SetNumberField(TEXT("mesh_height_cm"),Height);Row->SetNumberField(TEXT("scale"),Mesh->GetComponentScale().Z);Row->SetNumberField(TEXT("capsule_ground_gap_cm"),Bottom-Ground.ImpactPoint.Z);
                Row->SetNumberField(TEXT("mesh_bind_floor_gap_cm"),Mesh->GetComponentLocation().Z+(Bound.Origin.Z-Bound.BoxExtent.Z)*Mesh->GetComponentScale().Z-Ground.ImpactPoint.Z);
                Row->SetNumberField(TEXT("nav_height"),Pet->GetNavAgentPropertiesRef().AgentHeight);Row->SetNumberField(TEXT("capsule_height"),2*Pet->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());RepairSamples.Add(MakeShared<FJsonValueObject>(Row));
                RepairPetOrigins.Add(Pet->GetActorLocation());
            }
            Capture(TEXT("lineup_idle_butterfly_ssx_zxk"));Stage=2;RepairAge=0;return;
        }
        if(Stage==2&&RepairAge>.2)
        {
            for(auto Pet:RepairPets)if(auto* AI=Cast<AAIController>(Pet->GetController()))AI->MoveToLocation(Pet->GetActorLocation()+FVector(400,0,0),5);
            Stage=3;RepairAge=0;return;
        }
        if(Stage==3)
        {
            for(auto Pet:RepairPets)
            {
                Pet->UpdateLocomotion(DeltaSeconds);
                if(auto* SSXPet=Cast<AHeliosSunShangXiangPet>(Pet))SSXPet->PoseAt(TEXT("Run_Fwd"),SSXPet->MoveAge,true);
                else if(auto* ZXKPet=Cast<AHeliosZhongXiaoKuiPet>(Pet))ZXKPet->PoseAt(TEXT("Run_Fwd"),ZXKPet->MoveAge,true);
                else Pet->SetPose(TEXT("Move"),true);
            }
            if(RepairAge>.35&&!RepairCapturedFrames.Contains(0)){Capture(TEXT("lineup_navigation_run"));RepairCapturedFrames.Add(0);}
            if(RepairAge<2)return;
            for(int32 Index=0;Index<RepairPets.Num();++Index){auto* Pet=RepairPets[Index].Get();RepairCheck(FString::Printf(TEXT("pet_%d_actual_navigation_moved"),Index),FVector::Dist2D(Pet->GetActorLocation(),RepairPetOrigins[Index])>100);if(auto* AI=Cast<AAIController>(Pet->GetController()))AI->StopMovement();Pet->GetCharacterMovement()->StopMovementImmediately();Pet->SetActorLocation(RepairPetOrigins[Index]);Pet->SetActorRotation(FRotator(0,-90,0));Pet->SetPose(TEXT("Idle"),true);}
            Stage=4;RepairAge=0;return;
        }
        if(Stage==4)
        {
            if(RepairAge<.2)return;
            FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            for(int32 Index=1;Index<RepairPets.Num();++Index){auto* Pet=RepairPets[Index].Get();auto* Enemy=GetWorld()->SpawnActor<AHeliosEnemyE02>(Pet->GetActorLocation()+FVector(0,-180,0),FRotator(0,90,0),Params);Enemy->CurrentHP=100000000;Enemy->SetActorTickEnabled(false);if(auto* AI=Cast<AAIController>(Enemy->GetController()))AI->StopMovement();Pet->Target=Enemy;}
            Stage=5;RepairAge=0;RepairCapturedFrames.Reset();return;
        }
        if(Stage==5)
        {
            for(int32 Index=1;Index<RepairPets.Num();++Index){auto* Pet=RepairPets[Index].Get();Pet->Cooldown=FMath::Max(0.f,Pet->Cooldown-DeltaSeconds);Pet->UpdateAttack(DeltaSeconds);}
            for(int32 Index=1;Index<RepairPets.Num();++Index)if(RepairPets[Index]->AttackAge>=0)RepairCapturedFrames.Add(10+Index);
            const float Times[]={.15f,.45f,.85f,1.5f};for(int32 Index=0;Index<4;++Index)if(RepairAge>Times[Index]&&!RepairCapturedFrames.Contains(Index)){Capture(*FString::Printf(TEXT("lineup_melee_%d"),Index));RepairCapturedFrames.Add(Index);break;}
            if(RepairAge<2)return;
            auto* ZXKPet=Cast<AHeliosZhongXiaoKuiPet>(RepairPets[2]);RepairCheck(TEXT("zxk_root_mount_exists"),ZXKPet->GetMesh()->DoesSocketExist(TEXT("b_Root")));
            RepairCheck(TEXT("pet_attack_animations_executed"),RepairCapturedFrames.Contains(11)&&RepairCapturedFrames.Contains(12));
            FinishResumedFixes();return;
        }
        return;
    }
    if(RepairMode==TEXT("skip"))
    {
        if(Stage==0)
        {
        RepairCheck(TEXT("selection_cannot_skip"),!D->TrySkipUrbanWave(D->GetRunSerial(),D->GetUrbanWaveNumber()));
        HUD->SelectWeapon(TEXT("rifle"));HUD->StartSelectedRun();
        RepairCheck(TEXT("start_real_run"),!D->IsSelectingWeapon()&&!D->IsRunOver());
        D->SetActorTickEnabled(false);
        RepairCheck(TEXT("preparation_cannot_skip"),!D->CanSkipUrbanWave());
        D->UrbanRestRemaining=0.f;
        if(FParse::Param(FCommandLine::Get(),TEXT("UrbanRepairSlateClick")))FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/urban_challenge/resumed_fixes_20261005/")+RepairRunName)/TEXT("skip_button_enabled.png"),true,false);
        Stage=1;StageStarted=Now;return;
        }
        if(Stage==3)
        {
            if(Now-StageStarted<5)return;
            int32 Hazards=0;for(TActorIterator<AHeliosUrbanHazard> It(GetWorld());It;++It)if(!It->IsActorBeingDestroyed())++Hazards;
            RepairCheck(TEXT("canceled_boss_hazards_stay_gone_after_warning_deadline"),Hazards==0&&P->GetHealth()==HazardHealth);
            RepairCheck(TEXT("no_late_old_wave_spawns_or_rewards"),D->GetAliveCount()==0&&D->GetBossCount()==0&&D->Kills==0&&D->Score==0&&D->PendingBossNumbers.IsEmpty());
            Stage=2;StageStarted=Now;return;
        }
        if(Stage==2)
        {
            // Respect the real respawn-protection timer; do not alter health or
            // cooldown settings to make the death test pass.
            P->SetCanBeDamaged(true);UGameplayStatics::ApplyDamage(P,P->GetHealth()+1000,nullptr,nullptr,nullptr);
            if(!P->IsDead() && Now-StageStarted<20)return;
            RepairCheck(TEXT("death_request_rejected"),P->IsDead()&&!D->TrySkipUrbanWave(D->GetRunSerial(),1));
            D->RestartRun();D->SetActorTickEnabled(false);D->UrbanRestRemaining=0;D->UrbanWaveIndex=29;
            const int32 FinalKills=D->Kills,FinalScore=D->Score;
            RepairCheck(TEXT("last_wave_click_accepted"),D->TrySkipUrbanWave(D->GetRunSerial(),30));
            RepairCheck(TEXT("last_wave_finishes_without_wave31_or_reward"),D->IsUrbanVictory()&&D->IsRunOver()&&D->GetUrbanWaveNumber()==30&&D->Kills==FinalKills&&D->Score==FinalScore);
            RepairCheck(TEXT("victory_repeat_rejected"),!D->TrySkipUrbanWave(D->GetRunSerial(),30));
            RepairCheck(TEXT("skipped_completion_is_labeled"),D->GetUrbanWaveText().Contains(TEXT("跳过")));
            D->SetRunPaused(false);FinishResumedFixes();return;
        }
        if(Stage!=1 || Now-StageStarted<.15)return;
        if(!D->IsUrbanChallenge())
        {RepairCheck(TEXT("old_map_cannot_skip"),!D->CanSkipUrbanWave()&&!HUD->IsUrbanSkipVisible()&&!D->TrySkipUrbanWave(D->GetRunSerial(),D->GetUrbanWaveNumber()));FinishResumedFixes();return;}
        P->SetCanBeDamaged(false);
        FActorSpawnParameters Params;Params.Owner=D;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Enemy=GetWorld()->SpawnActor<AHeliosEnemyE02>(P->GetActorLocation()+FVector(1000,0,0),FRotator::ZeroRotator,Params);
        D->Enemies.Add(Enemy);Enemy->SetActorTickEnabled(false);
        auto* Killed=GetWorld()->SpawnActor<AHeliosEnemyE02>(P->GetActorLocation()+FVector(1500,0,0),FRotator::ZeroRotator,Params);
        FActorSpawnParameters DeadRoundParams;DeadRoundParams.Owner=Killed;DeadRoundParams.Instigator=Killed;
        auto* DeadRound=GetWorld()->SpawnActor<AHeliosBasketballProjectile>(P->GetActorLocation()+FVector(0,0,3200),FRotator::ZeroRotator,DeadRoundParams);
        D->Enemies.Add(Killed);UGameplayStatics::ApplyDamage(Killed,999999.f,PC,P,nullptr);
        RepairCheck(TEXT("fixture_real_death"),Killed->IsEnemyDead());
        D->UpdateEnemies(0.f);const int32 Kills=D->Kills,Score=D->Score;
        auto* Boss=GetWorld()->SpawnActor<AHeliosUrbanBoss>(P->GetActorLocation()+FVector(2500,0,0),FRotator::ZeroRotator,Params);
        D->Bosses.Add(Boss);Boss->SetActorTickEnabled(false);
        Boss->StartBasketballs(P);Boss->StartCharge(P);
        RepairCheck(TEXT("actual_basketball_and_charge_skills_queued"),Boss->GetBasketballCasts()==1&&Boss->GetChargeCasts()==1&&!Boss->Hazards.IsEmpty());
        auto* Hazard=GetWorld()->SpawnActorDeferred<AHeliosUrbanHazard>(AHeliosUrbanHazard::StaticClass(),FTransform(P->GetActorLocation()),Boss,nullptr);
        Hazard->Configure(Boss,true,420.f,3.2f,35.f);Hazard->FinishSpawning(FTransform(P->GetActorLocation()));
        D->UrbanRecoveryBeacon=GetWorld()->SpawnActor<AHeliosUrbanHazard>(P->GetActorLocation()+FVector(0,0,-100),FRotator::ZeroRotator,Params);
        D->UrbanRecoveryWarningRemaining=3.f;D->PendingBossNumbers.Add(1);
        FActorSpawnParameters EnemyRoundParams;EnemyRoundParams.Owner=Enemy;EnemyRoundParams.Instigator=Enemy;
        auto* EnemyRound=GetWorld()->SpawnActor<AHeliosBasketballProjectile>(P->GetActorLocation()+FVector(0,0,3000),FRotator::ZeroRotator,EnemyRoundParams);
        FActorSpawnParameters PlayerRoundParams;PlayerRoundParams.Owner=P;PlayerRoundParams.Instigator=P;
        auto* PlayerRound=GetWorld()->SpawnActor<AHeliosBasketballProjectile>(P->GetActorLocation()+FVector(0,0,3500),FRotator::ZeroRotator,PlayerRoundParams);
        if(!EnemyRound || !PlayerRound){RepairCheck(TEXT("concrete_projectile_fixture"),false);FinishResumedFixes();return;}
        const int32 Run=D->GetRunSerial(),Wave=D->GetUrbanWaveNumber();
        RepairCheck(TEXT("active_button_available"),D->CanSkipUrbanWave()&&HUD->CanSkipUrbanWave());
        D->SetRunPaused(true);RepairCheck(TEXT("paused_request_rejected"),!D->TrySkipUrbanWave(Run,Wave));D->SetRunPaused(false);
        HUD->ShowUrbanPointer();RepairCheck(TEXT("tab_pointer_enabled"),PC->bShowMouseCursor&&PC->IsLookInputIgnored());
        if(FParse::Param(FCommandLine::Get(),TEXT("UrbanRepairSlateClick")))
            RepairCheck(TEXT("actual_sbutton_mouse_events_and_capture_handled"),ClickActualSkipButton());
        else { HUD->BeginUrbanSkipClick();HUD->FinishUrbanSkipClick(); }
        RepairCheck(TEXT("one_click_advances_exactly_one_wave"),D->GetUrbanWaveNumber()==Wave+1&&D->UrbanSkippedWaves==1);
        HUD->FinishUrbanSkipClick();RepairCheck(TEXT("repeat_callback_rejected"),D->GetUrbanWaveNumber()==Wave+1);
        RepairCheck(TEXT("skip_does_not_forge_kills_or_score"),D->Kills==Kills&&D->Score==Score&&D->BossKills==0);
        RepairCheck(TEXT("real_death_counted_once"),Kills==1&&Score==10);
        RepairCheck(TEXT("living_enemies_boss_and_hazards_removed"),Enemy->IsActorBeingDestroyed()&&Boss->IsActorBeingDestroyed()&&Hazard->IsActorBeingDestroyed());
        RepairCheck(TEXT("boss_skill_state_and_owned_hazards_canceled"),Boss->Skill==0&&Boss->Hazards.IsEmpty());
        RepairCheck(TEXT("enemy_round_removed_player_round_preserved"),EnemyRound->IsActorBeingDestroyed()&&!PlayerRound->IsActorBeingDestroyed());
        RepairCheck(TEXT("normally_killed_enemy_leftover_round_removed"),DeadRound&&DeadRound->IsActorBeingDestroyed());
        RepairCheck(TEXT("pending_spawns_and_recovery_canceled"),D->PendingBossNumbers.IsEmpty()&&!IsValid(D->UrbanRecoveryBeacon)&&D->UrbanRecoveryWarningRemaining==0);
        RepairCheck(TEXT("next_wave_quota_starts_at_zero"),D->GetAliveCount()==0&&D->GetBossCount()==0&&D->UrbanRegularIssued==0&&D->UrbanBossIssued==0&&D->UrbanWaveStartKills==1);
        RepairCheck(TEXT("rapid_click_during_intermission_rejected"),!D->TrySkipUrbanWave(Run,Wave+1));
        HUD->HideUrbanPointer();RepairCheck(TEXT("tab_release_restores_fps_input"),!PC->bShowMouseCursor&&!PC->IsLookInputIgnored());
        D->UrbanRestRemaining=0;RepairCheck(TEXT("stale_previous_wave_rejected"),!D->TrySkipUrbanWave(Run,Wave));
        HUD->BeginUrbanSkipClick();D->UrbanRegularIssued=D->UrbanSchedule.Waves[D->UrbanWaveIndex].Total;D->UrbanBossIssued=0;
        // Tick derives quota from deaths, so use the common natural transition directly.
        D->AdvanceUrbanWave(false);D->UrbanRestRemaining=0;
        const int32 AfterNatural=D->GetUrbanWaveNumber();HUD->FinishUrbanSkipClick();
        RepairCheck(TEXT("mouse_release_after_natural_transition_rejected"),D->GetUrbanWaveNumber()==AfterNatural);
        D->RestartRun();D->SetActorTickEnabled(false);D->UrbanRestRemaining=0;
        RepairCheck(TEXT("previous_run_click_rejected"),!D->TrySkipUrbanWave(Run,AfterNatural));
        P->SetCanBeDamaged(true);HazardHealth=P->GetHealth();Stage=3;StageStarted=Now;return;
    }
    if(RepairMode!=TEXT("visual")){RepairCheck(TEXT("known_mode"),false);FinishResumedFixes();return;}
    RepairAge+=DeltaSeconds;
    if(Stage==0)
    {
        HUD->SelectWeapon(FName(*CombatWeapon));HUD->StartSelectedRun();
        RepairCheck(TEXT("selected_weapon_equipped"),P->GetWeaponId()==FName(*CombatWeapon));
        D->SetActorTickEnabled(false);D->CleanupOwnedActors();P->SetCanBeDamaged(false);
        Stage=1;RepairAge=0;return;
    }
    for(TActorIterator<AHeliosPetCompanion> It(GetWorld());It;++It)It->Destroy();
    const bool Urban=D->IsUrbanChallenge();
    const bool SSX=CombatWeapon==TEXT("cfm_sunshangxiang"),ZXK=CombatWeapon==TEXT("cfm_zhongxiaokui");
    const bool Guns=CombatWeapon==TEXT("rifle")||CombatWeapon==TEXT("rocket");
    const bool Spells=CombatWeapon==TEXT("calamity")&&FParse::Param(FCommandLine::Get(),TEXT("UrbanRepairSpellMatrix"));
    // Return to the bright plaza after the first-use Niagara/PSO warm-up. Keep
    // those original cold samples too; firing alone is not visual evidence.
    const int32 TotalCases=Spells?8:ZXK?7:Guns?6:SSX?5:4;
    const int32 Action=Spells?0:Guns?RepairCase%2:RepairCase==4?2:RepairCase>=5?1:RepairCase%2;
    const bool Shade=Spells?RepairCase>=4:RepairCase==2 || RepairCase==3 || (ZXK&&RepairCase==6);
    if(Stage==1)
    {
        Release();RepairCapturedFrames.Reset();
        if(RepairCase>=TotalCases){RepairCheck(TEXT("all_attack_environment_cases_recorded"),RepairSamples.Num()==TotalCases*4);FinishResumedFixes();return;}
        FVector Location=Urban?(Shade?FVector(0,1800,102):FVector(0,-2800,102)):FVector(0,-2300,102);
        P->SetActorLocation(Location,false,nullptr,ETeleportType::TeleportPhysics);P->GetCharacterMovement()->StopMovementImmediately();
        PC->SetControlRotation(FRotator(FParse::Param(FCommandLine::Get(),TEXT("UrbanRepairAimQA"))?-6.f:0.f,90,0));PC->SetViewTarget(P);
        if(CombatWeapon==TEXT("rifle")||CombatWeapon==TEXT("rocket")||CombatWeapon==TEXT("laser")){Key(EKeys::R,true);Key(EKeys::R,false);}
        if(Spells){if(auto* State=UHeliosCalamityState::Get(P))State->Clear();if(auto* Staff=Cast<AHeliosCalamityStaff>(P->GetEquippedWeapon()))Staff->ForcedSpell=RepairCase%4;}
        if(RepairCase==4 && ZXK){Key(EKeys::Q,true);Key(EKeys::Q,false);}
        for(TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It)D->DestroyEnemy(*It);
        FActorSpawnParameters Params;Params.Owner=D;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Target=GetWorld()->SpawnActor<AHeliosEnemyE02>(Location+FVector(0,900,0),FRotator(0,-90,0),Params);
        Target->CurrentHP=100000000;Target->SetActorTickEnabled(false);Target->GetCharacterMovement()->StopMovementImmediately();
        if(auto* AI=Cast<AAIController>(Target->GetController())){AI->StopMovement();if(AI->GetBrainComponent())AI->GetBrainComponent()->StopLogic(TEXT("visual target fixture"));}
        if(auto* Collection=LoadObject<UMaterialParameterCollection>(nullptr,TEXT("/Game/UrbanChallenge/Materials/MPC_UrbanWeaponExposure.MPC_UrbanWeaponExposure")))
            GetWorld()->GetParameterCollectionInstance(Collection)->SetScalarParameterValue(TEXT("CompensateWeaponExposure"),Urban&&!FParse::Param(FCommandLine::Get(),TEXT("UrbanRepairBaseline"))?1.f:0.f);
        Stage=2;RepairAge=0;return;
    }
    if(Stage==2 && RepairAge>4)
    {
        const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/urban_challenge/resumed_fixes_20261005/")+RepairRunName);
        IFileManager::Get().MakeDirectory(*Directory,true);
        FScreenshotRequest::RequestScreenshot(Directory/FString::Printf(TEXT("case_%d_idle.png"),RepairCase),true,false);
        Stage=6;RepairAge=0;return;
    }
    if(Stage==6 && RepairAge>.08f)
    {
        Key(Action==2?EKeys::G:Action==1?EKeys::RightMouseButton:EKeys::LeftMouseButton,true);
        Stage=3;RepairAge=0;return;
    }
    if(Stage==3)
    {
        const bool LongSkill=Action==2&&!ZXK;
        const float Times[]={LongSkill?.2f:.01f,LongSkill?.7f:.32f,LongSkill?1.3f:.65f,LongSkill?2.f:1.1f};
        for(int32 Index=0;Index<4;++Index)if(RepairAge>=Times[Index]&&!RepairCapturedFrames.Contains(Index))
        {
            RepairCapturedFrames.Add(Index);
            const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/urban_challenge/resumed_fixes_20261005/")+RepairRunName);
            FScreenshotRequest::RequestScreenshot(Directory/FString::Printf(TEXT("case_%d_action_%d.png"),RepairCase,Index),true,false);
            auto Sample=MakeShared<FJsonObject>();Sample->SetNumberField(TEXT("case"),RepairCase);Sample->SetNumberField(TEXT("phase"),Index);Sample->SetNumberField(TEXT("actual_age"),RepairAge);
            Sample->SetStringField(TEXT("environment"),Shade?TEXT("bridge_shadow"):TEXT("open_plaza"));Sample->SetStringField(TEXT("input"),Action==2?TEXT("G_skill"):Action==1?TEXT("right_mouse"):TEXT("left_mouse"));
            for(TActorIterator<AHeliosZhongXiaoKuiWeapon> It(GetWorld());It;++It)Sample->SetStringField(TEXT("zxk_status"),It->Status());
            if(auto* Weapon=P->GetEquippedWeapon())Sample->SetNumberField(TEXT("magazine_ammo"),Weapon->GetBulletCount());
            if(Spells){Sample->SetNumberField(TEXT("forced_spell"),RepairCase%4);if(auto* State=UHeliosCalamityState::Get(P))Sample->SetNumberField(TEXT("actual_spell_casts"),State->CastCount);}
            int32 Active=0;for(TActorIterator<AActor> It(GetWorld());It;++It)if(It->GetClass()->GetName().Contains(TEXT("Effect"))||It->GetClass()->GetName().Contains(TEXT("Projectile")))++Active;
            Sample->SetNumberField(TEXT("effect_projectile_actors"),Active);RepairSamples.Add(MakeShared<FJsonValueObject>(Sample));break;
        }
        if(RepairAge>(Action==2?2.35f:1.5f)){Release();Stage=4;RepairAge=0;}
    }
    if(Stage==4 && RepairAge>1.f)
    {
        // Releasing a charged attack can create its main effect.
        const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/urban_challenge/resumed_fixes_20261005/")+RepairRunName);
        FScreenshotRequest::RequestScreenshot(Directory/FString::Printf(TEXT("case_%d_release.png"),RepairCase),true,false);
        ++RepairCase;Stage=1;RepairAge=0;
    }
}
