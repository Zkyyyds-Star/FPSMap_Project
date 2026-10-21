#include "QA/HeliosSSXDemo.h"
#include "Weapons/HeliosHunterDamageFeedback.h"
#include "QA/HeliosAsyncFrameCapture.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Combat/HeliosCombatTempoSettings.h"
#include "Weapons/HeliosSunShangXiang.h"
#include "Weapons/HeliosZhongXiaoKui.h"
#include "Weapons/HeliosZXKProjectile.h"
#include "SkeletalRenderPublic.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Pets/HeliosZhongXiaoKuiPet.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Pets/HeliosSunShangXiangPet.h"
#include "Player/HeliosArenaCharacter.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "UI/HeliosAdventureHUD.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "ShaderCompiler.h"
#include "Weapons/HeliosHandAnimInstance.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequence.h"
#include "World/HeliosMapTravelSubsystem.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Weapons/HeliosSSXVoice.h"
#include "Engine/DamageEvents.h"
#include "AIController.h"
#include "TimerManager.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"

namespace {
int32 SSXTravelHop=0,SSXTravelFailures=0;FString SSXTravelChecks;
FString SSXQADirectory()
{
    FString Run;FParse::Value(FCommandLine::Get(),TEXT("HeliosSSXQARun="),Run);
    const FString Base=FPaths::ProjectDir()/TEXT("../qa/cfm_sunshangxiang");
    return FPaths::ConvertRelativePathToFull(Run.IsEmpty()?Base:Base/TEXT("fixes")/FPaths::MakeValidFileName(Run));
}
}

AHeliosSSXDemo::AHeliosSSXDemo(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void AHeliosSSXDemo::Capture(const TCHAR* N){const FString Dir=SSXQADirectory()/TEXT("runtime");IFileManager::Get().MakeDirectory(*Dir,true);if(FParse::Param(FCommandLine::Get(),TEXT("HeliosAsyncMotionCapture")))HeliosAsyncFrameCapture::Request(Dir/(FString(N)+TEXT(".jpg")));else FScreenshotRequest::RequestScreenshot(Dir/(FString(N)+TEXT(".png")),true,false);}
void AHeliosSSXDemo::Check(const TCHAR* N,bool Pass){if(!Pass)++Failures;if(!Checks.IsEmpty())Checks+=TEXT(",");Checks+=FString::Printf(TEXT("{\"name\":\"%s\",\"passed\":%s}"),N,Pass?TEXT("true"):TEXT("false"));UE_LOG(LogTemp,Display,TEXT("SSX_CHECK %s %s"),N,Pass?TEXT("PASS"):TEXT("FAIL"));}
void AHeliosSSXDemo::Finish(){HeliosAsyncFrameCapture::Finish(SSXQADirectory());FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"status\":\"%s\",\"failures\":%d,\"checks\":[%s]}"),Failures?TEXT("failed"):TEXT("passed"),Failures,*Checks),*(SSXQADirectory()/TEXT("runtime_report.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);Stage=99;FPlatformMisc::RequestExit(false);}
void AHeliosSSXDemo::TickVoiceQA(AHeliosSunShangXiangWeapon* W,AHeliosSunShangXiangPet* Pet)
{
    if(!W||!Pet){Check(TEXT("voice_qa_actors_present"),false);Finish();return;}
    auto* V=W->Voice.Get();
    const double Now=FPlatformTime::Seconds();
    if(Stage==60)
    {
        W->Cancel();Pet->Voice->Stop();
        W->Voice->Initialize(false);Pet->Voice->Initialize(true);
        Check(TEXT("player_eight_kill_voices_loaded"),V->GetKillPoolSize()==8);
        Check(TEXT("companion_eight_kill_voices_loaded"),Pet->Voice->GetKillPoolSize()==8);
        VoiceSample=0;Captured=false;Stage=61;
    }
    else if(Stage==61)
    {
        const int32 ActorIndex=VoiceSample/8;
        auto* Voice=ActorIndex==0?V:Pet->Voice.Get();
        if(!Captured)
        {
            VoiceBaseline=Voice->KillCount;VoiceStartedAt=Now;
            const bool StartedVoice=Voice->Play(TEXT("Kill"));
            Check(*FString::Printf(TEXT("voice_%d_starts"),VoiceSample),StartedVoice&&Voice->IsVoicePlaying());
            if(!StartedVoice||Voice->GetLastPlayedKillIndex()==INDEX_NONE){Finish();return;}
            const int32 Index=Voice->GetLastPlayedKillIndex();VoiceCoverage[ActorIndex].Add(Index);
            VoiceExpectedDuration=Voice->KillSounds[Index]->GetDuration();Captured=true;
        }
        else if(!Voice->IsVoicePlaying())
        {
            Check(*FString::Printf(TEXT("voice_%d_plays_to_completion"),VoiceSample),Voice->KillCount==VoiceBaseline+1&&Now-VoiceStartedAt>=VoiceExpectedDuration-.15);
            ++VoiceSample;Captured=false;
            if(VoiceSample==16)
            {
                Check(TEXT("player_shuffle_covers_eight_voices"),VoiceCoverage[0].Num()==8);
                Check(TEXT("companion_shuffle_covers_eight_voices"),VoiceCoverage[1].Num()==8);
                Stage=62;
            }
        }
    }
    else if(Stage==62)
    {
        VoiceBaseline=V->KillCount;const int32 FirstRequest=V->KillRequestSerial+1;
        V->Play(TEXT("Kill"));auto* OriginalChannel=V->Channel.Get();
        V->Play(TEXT("Kill"));V->Play(TEXT("Kill"));V->Play(TEXT("Kill"));
        Check(TEXT("rapid_kills_keep_latest_two_pending"),V->PendingKillRequests==TArray<int32>({FirstRequest+2,FirstRequest+3}));
        Check(TEXT("rapid_kills_do_not_interrupt_or_overlap"),V->KillCount==VoiceBaseline+1&&V->Channel==OriginalChannel&&V->IsVoicePlaying());
        Check(TEXT("hurt_does_not_interrupt_kill_queue"),!V->Play(TEXT("Hurt"))&&V->GetPendingKillCount()==2);
        Stage=63;
    }
    else if(Stage==63&&!V->IsVoicePlaying()&&V->GetPendingKillCount()==0)
    {
        Check(TEXT("rapid_kills_drain_after_natural_audio_finish"),V->KillCount==VoiceBaseline+3);
        V->Play(TEXT("Kill"));V->Play(TEXT("Kill"));const int32 DeathBefore=V->DeathCount;
        V->Play(TEXT("Death"));VoiceBaseline=V->KillCount;
        Check(TEXT("death_preempts_kill_and_clears_queue"),V->DeathCount==DeathBefore+1&&V->GetPendingKillCount()==0&&V->Priority==3);
        Check(TEXT("kill_cannot_interrupt_death"),!V->Play(TEXT("Kill")));
        Stage=64;
    }
    else if(Stage==64&&!V->IsVoicePlaying())
    {
        Check(TEXT("death_does_not_restart_stale_kills"),V->KillCount==VoiceBaseline);
        VoiceBaseline=V->KillCount;V->Play(TEXT("Kill"));V->Play(TEXT("Kill"));
        Check(TEXT("voice_qa_pause_started"),UGameplayStatics::SetGamePaused(this,true));VoiceStartedAt=Now;Stage=65;
    }
    else if(Stage==65&&Now-VoiceStartedAt>3)
    {
        Check(TEXT("pause_never_starts_pending_voice"),V->KillCount==VoiceBaseline+1&&V->GetPendingKillCount()==1);
        Check(TEXT("paused_kill_request_rejected"),!V->Play(TEXT("Kill")));
        UGameplayStatics::SetGamePaused(this,false);Stage=66;
    }
    else if(Stage==66&&!V->IsVoicePlaying()&&V->GetPendingKillCount()==0)
    {
        Check(TEXT("resume_drains_pending_voice"),V->KillCount==VoiceBaseline+2);
        V->Play(TEXT("Kill"));V->Play(TEXT("Kill"));V->Stop();
        Check(TEXT("explicit_stop_clears_audio_and_queue"),!V->IsVoicePlaying()&&V->GetPendingKillCount()==0);
        V->Play(TEXT("Kill"));V->Play(TEXT("Kill"));W->DeactivateWeapon();
        Check(TEXT("unequip_clears_audio_and_queue"),!V->IsVoicePlaying()&&V->GetPendingKillCount()==0);
        FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Target=GetWorld()->SpawnActor<AHeliosEnemyE01>(Pet->GetActorLocation()+Pet->GetActorForwardVector()*200,FRotator::ZeroRotator,Spawn);
        Target->CurrentHP=1;Target->StopShooting();Target->SetActorTickEnabled(false);GetWorld()->GetTimerManager().ClearAllTimersForObject(Target);
        if(Target->GetController())Target->GetController()->Destroy();
        Pet->Target=Target;Pet->State=EHeliosPetState::Fighting;Pet->Voice->Stop();VoiceBaseline=Pet->Voice->KillCount;
        Pet->Timeline.Begin(HeliosSSX::EAttack::Light);Pet->Contact(0);Pet->Contact(1);Pet->SetActorTickEnabled(true);
        Check(TEXT("real_companion_kill_triggers_once"),Target->IsEnemyDead()&&Pet->Voice->KillCount==VoiceBaseline+1);
        VoiceStartedAt=Now;Captured=false;Stage=67;
    }
    else if(Stage==67)
    {
        if(!Captured&&Now-VoiceStartedAt>.2)
        {
            Check(TEXT("dead_target_cleanup_does_not_cut_companion_voice"),Pet->Voice->IsVoicePlaying());Captured=true;
        }
        if(Captured&&!Pet->Voice->IsVoicePlaying())
        {
            Check(TEXT("real_companion_kill_plays_to_completion"),Pet->Voice->KillCount==VoiceBaseline+1&&Now-VoiceStartedAt>=.7);
            Target->Destroy();Target=nullptr;Pet->SetActorTickEnabled(false);
            Pet->Voice->Play(TEXT("Kill"));Pet->Voice->Play(TEXT("Kill"));
            const int32 DeathBefore=Pet->Voice->DeathCount;FDamageEvent Event;Pet->TakeDamage(10000,Event,nullptr,W);
            Check(TEXT("companion_down_clears_kill_queue"),Pet->IsSleeping()&&Pet->Voice->GetPendingKillCount()==0&&Pet->Voice->DeathCount==DeathBefore+1);
            auto* Voice=Pet->Voice.Get();Pet->Destroy();
            Check(TEXT("companion_destroy_cancels_audio"),!Voice->IsVoicePlaying()&&Voice->GetPendingKillCount()==0);Finish();
        }
    }
}

void AHeliosSSXDemo::TickZXKFeedback(AHeliosZhongXiaoKuiWeapon* W,AHeliosZhongXiaoKuiPet* Pet,float Dt)
{
    if(!W||!Pet){Check(TEXT("feedback_actors"),false);Finish();return;}
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosMotionFeedback"))&&((Stage>=301&&Stage<=304)||Stage==309))
    {
        const FVector Hand=W->GetFirstPersonMesh()->GetSocketLocation(TEXT("b_RightHand"));
        const double Now=FPlatformTime::Seconds();
        const auto* Anim=Cast<UHeliosHandAnimInstance>(W->GetFirstPersonMesh()->GetAnimInstance());
        UE_LOG(LogTemp,Display,TEXT("MOTION_FRAME stage=%d age=%.6f dt=%.6f wall=%.9f action=%s clock=%.6f sequence=%s sample=%.6f serial=%u hand=%s"),Stage,Age,Dt,Now,*W->Action.ToString(),W->Age,*GetNameSafe(Anim?Anim->Sequence:nullptr),Anim?Anim->SampleSeconds:-1.f,Anim?Anim->TransitionSerial:0,*Hand.ToString());
        for(TActorIterator<AActor> It(GetWorld());It;++It)if(It->GetClass()->GetName().Contains(TEXT("ZXKProjectile")))
        {
            TArray<AActor*> Attached;It->GetAttachedActors(Attached);
            for(AActor* FX:Attached){TInlineComponentArray<UStaticMeshComponent*> Meshes(FX);for(auto* M:Meshes)if(M->IsVisible())
                UE_LOG(LogTemp,Display,TEXT("DART_FRAME projectile=%s actor=%s fx=%s mesh=%s location=%s"),*It->GetName(),*It->GetActorLocation().ToString(),*FX->GetName(),*M->GetName(),*M->GetComponentLocation().ToString());}
        }
        if(Stage==304&&W->Form==2&&W->Age<0)W->StartFiring();
        if(Stage==303&&W->Form==1&&W->Age<0)W->StartFiring();
    }
    if(Stage==309)
    {
        if(W->Age<0&&Age<3.f){W->StartSecondaryFire();W->StopSecondaryFire();}
        if(Age>=3.f&&Age<6.f)W->StartSecondaryFire();
        if(Age-FeedbackLastSample>=1.f/30.f){FeedbackLastSample=Age;Capture(*FString::Printf(TEXT("feedback_fp_309_%03d"),FeedbackFrame++));}
        if(Age>6.f)
        {
            Check(TEXT("Q_B_secondary_repeated_clicks_and_hold_spawn_multiple_waves"),W->Form==1&&W->WaveShots-int32(Before)>=3);
            W->StopSecondaryFire();W->Cancel();Check(TEXT("wave_release_and_cancel_clear_repeat_input"),!W->bWaveHeld&&W->Projectiles.IsEmpty());
            Stage=304;Age=0;FeedbackFrame=0;FeedbackLastSample=-1;W->ToggleRangedForm();
        }
        return;
    }
    if(Stage==300)
    {
        W->Cancel();W->StartFiring();if(!FParse::Param(FCommandLine::Get(),TEXT("HeliosMotionFeedback")))W->StopFiring();Stage=301;Age=0;FeedbackLastSample=-1;FeedbackFrame=0;
    }
    else if(Stage==301||Stage==302||Stage==303||Stage==304)
    {
        const bool Motion=FParse::Param(FCommandLine::Get(),TEXT("HeliosMotionFeedback"));
        const float Window=Motion?(Stage==304||Stage==303?6.f:Stage==301?4.f:2.f):2.f;
        if(!FParse::Param(FCommandLine::Get(),TEXT("HeliosMotionNoCapture"))&&Age<Window&&Age-FeedbackLastSample>=(Motion?1.f/30.f:.08f))
        {
            FeedbackLastSample=Age;Capture(*FString::Printf(TEXT("feedback_fp_%d_%03d"),Stage,FeedbackFrame++));
            const FVector Hand=W->GetFirstPersonMesh()->GetSocketLocation(TEXT("b_RightHand"))-Player->GetActorLocation();
            if(!FeedbackSamples.IsEmpty())FeedbackSamples+=TEXT(",");
            FeedbackSamples+=FString::Printf(TEXT("{\"stage\":%d,\"t\":%.4f,\"hand\":[%.4f,%.4f,%.4f],\"action\":\"%s\"}"),Stage,Age,Hand.X,Hand.Y,Hand.Z,*W->Action.ToString());
        }
        if(Age>Window+.1f)
        {
            FeedbackLastSample=-1;Age=0;FeedbackFrame=0;++Stage;
            if(Stage==302){W->Cancel();W->StartSecondaryFire();}
            else if(Stage==303)W->ToggleForm();
            else if(Stage==304){W->Cancel();if(Motion){Stage=309;Before=W->WaveShots;W->StartSecondaryFire();W->StopSecondaryFire();}else W->ToggleRangedForm();}
            else
            {
                W->Cancel();W->SetActorTickEnabled(false);W->GetFirstPersonMesh()->SetVisibility(false);W->Dart->SetVisibility(false);Player->GetCharacterMovement()->DisableMovement();
                if(Motion){int32 Orphans=0;for(TActorIterator<AHeliosSSXEffect> E(GetWorld());E;++E)if(E->GetOwner()&&E->GetOwner()->IsA<AHeliosZXKProjectile>())++Orphans;Check(TEXT("repeated_darts_cancel_leaves_no_flight_effects"),W->Projectiles.IsEmpty()&&Orphans==0);}
                Pet->CancelAttack();Pet->Target.Reset();Pet->DecisionRemaining=100;Pet->SetActorTickEnabled(true);
                Pet->GetCharacterMovement()->SetMovementMode(MOVE_Walking);Pet->GetCharacterMovement()->MaxWalkSpeed=471.1924f;
                Pet->SetActorLocation(Player->GetActorLocation()+FVector(350,100,0));Pet->SetActorRotation(FRotator::ZeroRotator);
                Pet->GetCharacterMovement()->bOrientRotationToMovement=true;FeedbackStart=Pet->GetActorLocation();
                FeedbackFoot=Pet->GetMesh()->GetSocketLocation(TEXT("b_LeftAnkle"));
                if(auto* AI=Cast<AAIController>(Pet->GetController()))AI->MoveToLocation(FeedbackStart+FVector(2200,0,0),30);
            }
        }
    }
    else if(Stage==305||Stage==306)
    {
        const FVector Loc=Pet->GetActorLocation(),Foot=Pet->GetMesh()->GetSocketLocation(TEXT("b_LeftAnkle"));
        Player->SetActorLocation(Loc+FVector(0,-420,50));FVector Eye;FRotator View;Player->GetActorEyesViewPoint(Eye,View);
        if(auto* PC=Cast<APlayerController>(Player->GetController()))PC->SetControlRotation((Loc+FVector(0,0,0)-Eye).Rotation());
        if(Age<2.4f&&Age-FeedbackLastSample>=.08f)
        {
            FeedbackLastSample=Age;Capture(*FString::Printf(TEXT("feedback_walk_%d_%03d"),Stage,FeedbackFrame++));
            if(!FeedbackSamples.IsEmpty())FeedbackSamples+=TEXT(",");
            FeedbackSamples+=FString::Printf(TEXT("{\"stage\":%d,\"t\":%.4f,\"position\":[%.4f,%.4f,%.4f],\"speed\":%.4f,\"phase\":%.4f,\"foot\":[%.4f,%.4f,%.4f],\"pose\":\"%s\"}"),Stage,Age,Loc.X,Loc.Y,Loc.Z,Pet->GetVelocity().Size2D(),Pet->MoveAge,Foot.X,Foot.Y,Foot.Z,*Pet->CurrentPose.ToString());
        }
        if(Age>2.5f)
        {
            Check(*FString::Printf(TEXT("feedback_live_move_%d"),Stage),FVector::Dist2D(FeedbackStart,Loc)>400&&Pet->CurrentPose==TEXT("Move")&&FVector::Dist(FeedbackFoot,Foot)>100);
            Age=0;FeedbackLastSample=-1;FeedbackFrame=0;
            if(Stage==305)
            {
                Stage=306;FeedbackStart=Loc;Pet->GetCharacterMovement()->MaxWalkSpeed=650;
                if(auto* AI=Cast<AAIController>(Pet->GetController()))AI->MoveToLocation(Loc+FVector(0,2000,0),30);
            }
            else{Stage=307;if(auto* AI=Cast<AAIController>(Pet->GetController()))AI->StopMovement();Pet->GetCharacterMovement()->StopMovementImmediately();}
        }
    }
    else if(Stage==307&&Age>.3f)
    {
        Check(TEXT("feedback_stop_pose_idle"),Pet->CurrentPose==TEXT("Idle"));Capture(TEXT("feedback_walk_stopped"));
        FFileHelper::SaveStringToFile(TEXT("[")+FeedbackSamples+TEXT("]"),*(SSXQADirectory()/TEXT("feedback_samples.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);Stage=308;Age=0;
    }
    else if(Stage==308&&Age>.2f)Finish();
}

void AHeliosSSXDemo::TickZXKQA(APlayerController* PC,AHeliosAdventureHUD* HUD)
{
    auto* W=Cast<AHeliosZhongXiaoKuiWeapon>(Player->GetEquippedWeapon());
    auto* PW=GetWorld()->GetSubsystem<UHeliosPetWorld>();auto* Pet=PW?Cast<AHeliosZhongXiaoKuiPet>(PW->Pet):nullptr;
    if(Stage>=330&&Stage<=359){TickZXKMovingWaveQA(W);return;}
    if(Stage>=300){TickZXKFeedback(W,Pet,GetWorld()->GetDeltaSeconds());return;}
    if(Stage==0&&Age>2)
    {
        OriginalHP=Player->GetMaxHealth();OriginalSpeed=Player->GetCharacterMovement()->MaxWalkSpeed;
        if(HUD){HUD->SelectWeapon(TEXT("cfm_zhongxiaokui"));HUD->SelectPet(EHeliosPetKind::ZhongXiaoKui);}Stage=200;Age=0;
    }
    else if(Stage==200&&Age>2){Capture(TEXT("zxk_00_menu"));Stage=201;Age=0;}
    else if(Stage==201&&Age>1){if(HUD)HUD->StartSelectedRun();if(auto* D=AHeliosAdventureDirector::Find(GetWorld());D&&!D->IsSelectingWeapon())Stage=202;Age=0;}
    else if(Stage==202&&Age>3)
    {
        Check(TEXT("zxk_weapon_and_partner_selected_independently"),W&&Pet);if(!W||!Pet){Finish();return;}
        Check(TEXT("zxk_original_fp_and_companion_meshes_loaded"),W->GetFirstPersonMesh()->GetSkeletalMeshAsset()&&Pet->bModelReady);
        Check(TEXT("zxk_player_health_and_speed_preserved"),OriginalHP==Player->GetMaxHealth()&&OriginalSpeed==Player->GetCharacterMovement()->MaxWalkSpeed);
        if(auto* D=AHeliosAdventureDirector::Find(GetWorld()))D->SetActorTickEnabled(false);
        for(TActorIterator<AHeliosEnemyActor> E(GetWorld());E;++E)E->Destroy();Player->SetCanBeDamaged(false);PC->SetControlRotation(FRotator::ZeroRotator);
        if(FParse::Param(FCommandLine::Get(),TEXT("HeliosZXKPlazaQA")))Player->SetActorLocation(FVector(-1500,-1300,100));
        Pet->SetActorTickEnabled(false);if(auto* AI=Cast<AAIController>(Pet->GetController()))AI->StopMovement();Pet->GetCharacterMovement()->DisableMovement();
        Pet->SetActorLocation(Player->GetActorLocation()+FVector(300,160,-6));Pet->SetActorRotation(FRotator(0,180,0));Pet->SetPose(TEXT("Idle"));
        if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();Stage=203;Age=0;
    }
    else if(Stage==203&&Age>2)
    {
        Capture(TEXT("zxk_01_fp_and_partner_idle"));Check(TEXT("zxk_fp_visible_native_pose"),W->GetFirstPersonMesh()->IsVisible()&&!W->GetFirstPersonMesh()->bHiddenInGame);
        if(FParse::Param(FCommandLine::Get(),TEXT("HeliosZXKMovingWaveQA"))){W->Cancel();W->ToggleForm();Stage=330;Age=0;return;}
        if(FParse::Param(FCommandLine::Get(),TEXT("HeliosZXKFeedback"))){Stage=300;Age=0;return;}
        if(FParse::Param(FCommandLine::Get(),TEXT("HeliosZXKQuickForms"))){W->Cancel();W->SetActorTickEnabled(false);W->ToggleForm();W->Tick(3);Stage=213;Age=0;return;}
        FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Target=GetWorld()->SpawnActor<AHeliosEnemyE01>(Player->GetActorLocation()+FVector(200,0,0),FRotator(0,180,0),Spawn);Target->CurrentHP=10000;Target->StopShooting();Target->GetCharacterMovement()->DisableMovement();Target->SetActorTickEnabled(false);GetWorld()->GetTimerManager().ClearAllTimersForObject(Target);if(Target->GetController())Target->GetController()->Destroy();
        Before=Target->CurrentHP;FollowStart=W->GetFirstPersonMesh()->GetSocketLocation(TEXT("b_RightHand"));Player->StartFireFromAutomation();Player->StopFireFromAutomation();Stage=204;Age=0;Captured=false;
    }
    else if(Stage==204)
    {
        if(!Captured&&Age>.18f){Capture(TEXT("zxk_02_fp_light"));Check(TEXT("zxk_light_wrist_moves"),FVector::Dist(FollowStart,W->GetFirstPersonMesh()->GetSocketLocation(TEXT("b_RightHand")))>1);Captured=true;}
        if(Age>1.6f){Check(TEXT("zxk_fp_light_damages_enemy_once"),FMath::IsNearlyEqual(Before-Target->CurrentHP,100));Before=Target->CurrentHP;Player->StartSecondaryAttack();Player->StopSecondaryAttack();Stage=205;Age=0;Captured=false;}
    }
    else if(Stage==205)
    {
        if(!Captured&&Age>.4f){Capture(TEXT("zxk_03_fp_heavy"));Captured=true;}
        if(Age>2){Check(TEXT("zxk_fp_heavy_damages_enemy_once"),FMath::IsNearlyEqual(Before-Target->CurrentHP,200));W->Cancel();Target->SetActorLocation(Player->GetActorLocation()+FVector(2000,0,0));Before=W->HitCount;Player->StartFireFromAutomation();Player->StopFireFromAutomation();Stage=206;Age=0;}
    }
    else if(Stage==206&&Age>1.6f)
    {
        Check(TEXT("zxk_empty_swing_has_no_contact"),W->HitCount==int32(Before));W->Cancel();Target->SetActorLocation(Player->GetActorLocation()+FVector(220,0,0));
        Pet->GetCharacterMovement()->SetMovementMode(MOVE_Walking);Pet->SetActorTickEnabled(true);Before=Pet->Contacts;Stage=207;Age=0;Captured=false;
    }
    else if(Stage==207)
    {
        if(!Captured&&Age>1){Capture(TEXT("zxk_04_partner_combat"));Captured=true;}
        if(Age>4){Check(TEXT("zxk_actual_ai_partner_attacks_enemy"),Pet->Contacts>int32(Before));Target->Destroy();Target=nullptr;Pet->Target=nullptr;Pet->CancelAttack();FollowStart=Pet->GetActorLocation();Player->SetActorLocation(Player->GetActorLocation()+FVector(600,0,0));FVector Goal;const bool Found=Pet->FindFollowPosition(Goal);Check(TEXT("zxk_follow_goal_on_navmesh"),Found);FollowDistance=Found?FVector::Dist2D(FollowStart,Goal):0;Stage=208;Age=0;}
    }
    else if(Stage==208&&Age>5)
    {
        FVector Goal;const bool Found=Pet->FindFollowPosition(Goal);
        UE_LOG(LogTemp,Display,TEXT("ZXK_FOLLOW moved=%.3f initial_goal=%.3f remaining_goal=%.3f"),FVector::Dist2D(FollowStart,Pet->GetActorLocation()),FollowDistance,Found?FVector::Dist2D(Pet->GetActorLocation(),Goal):-1.f);
        Check(TEXT("zxk_actual_ai_partner_follows_owner"),Found&&FVector::Dist2D(FollowStart,Pet->GetActorLocation())>100&&FVector::Dist2D(Pet->GetActorLocation(),Goal)<FollowDistance-100);
        Check(TEXT("zxk_weapon_remains_equipped_alongside_partner"),Player->GetWeaponId()==TEXT("cfm_zhongxiaokui")&&Pet->Kind==EHeliosPetKind::ZhongXiaoKui);
        Capture(TEXT("zxk_05_partner_follow"));Stage=209;Age=0;
    }
    else if(Stage==209&&Age>.3f){EffectSample=0;W->Cancel();W->SetActorTickEnabled(false);Stage=210;Age=0;}
    else if(Stage==210&&Age>.3f)
    {
        W->Combo=EffectSample;W->StartAttack(false);W->Tick(.12f);int32 Visible=0;
        for(AHeliosSSXEffect* Effect:W->Effects)if(IsValid(Effect))
        {
            Effect->SetActorTickEnabled(false);Effect->Tick(.12f);
            TInlineComponentArray<UStaticMeshComponent*> Meshes(Effect);
            for(auto* Mesh:Meshes)Visible+=Mesh->IsVisible()&&!Mesh->bHiddenInGame;
        }
        Check(*FString::Printf(TEXT("zxk_light_%d_source_particles_visible"),EffectSample+1),Visible>0);
        UE_LOG(LogTemp,Display,TEXT("ZXK_SOURCE_FX phase=%d actors=%d visible_tracks=%d"),EffectSample+1,W->Effects.Num(),Visible);
        Capture(*FString::Printf(TEXT("zxk_06_light_%d_source_fx"),EffectSample+1));Stage=211;Age=0;
    }
    else if(Stage==211&&Age>.3f)
    {
        for(AHeliosSSXEffect* Effect:W->Effects)if(IsValid(Effect))Effect->SetActorHiddenInGame(true);
        Capture(*FString::Printf(TEXT("zxk_07_light_%d_without_fx"),EffectSample+1));Stage=212;Age=0;
    }
    else if(Stage==212&&Age>.3f)
    {W->Cancel();if(++EffectSample<3){Stage=210;Age=0;}else{W->ToggleForm();W->Tick(3);Stage=213;Age=0;}}
    else if(Stage==213&&Age>.5f)
    {
        UE_LOG(LogTemp,Display,TEXT("ZXK_B_POSE action=%s idle=%.4f velocity=%s right=%s left=%s jian=%s"),*W->Action.ToString(),W->IdleAge,*Player->GetVelocity().ToString(),*W->GetFirstPersonMesh()->GetSocketTransform(TEXT("b_RightHand"),RTS_Component).ToString(),*W->GetFirstPersonMesh()->GetSocketTransform(TEXT("b_LeftHand"),RTS_Component).ToString(),*W->GetFirstPersonMesh()->GetSocketTransform(TEXT("Jian"),RTS_Component).ToString());
        TArray<FFinalSkinVertex> Vertices;W->GetFirstPersonMesh()->GetCPUSkinnedVertices(Vertices,0);FBox Box(ForceInit);for(const auto& V:Vertices)Box+=FVector(V.Position);
        UE_LOG(LogTemp,Display,TEXT("ZXK_B_SKIN vertices=%d box=%s mesh=%s"),Vertices.Num(),*Box.ToString(),*W->GetFirstPersonMesh()->GetComponentTransform().ToString());
        FVector Eye;FRotator View;PC->GetPlayerViewPoint(Eye,View);UE_LOG(LogTemp,Display,TEXT("ZXK_B_CAMERA eye=%s view=%s parent=%s"),*Eye.ToString(),*View.ToString(),*W->GetFirstPersonMesh()->GetAttachParent()->GetComponentTransform().ToString());
        const auto& Sections=W->GetFirstPersonMesh()->GetSkeletalMeshAsset()->GetResourceForRendering()->LODRenderData[0].RenderSections;
        for(const auto& Section:Sections){FBox SectionBox(ForceInit);float Closest=MAX_flt;const FVector Hand=W->GetFirstPersonMesh()->GetSocketLocation(TEXT("b_LeftHand"));for(uint32 I=Section.BaseVertexIndex;I<Section.BaseVertexIndex+Section.NumVertices&&I<uint32(Vertices.Num());++I){const FVector At(Vertices[I].Position);SectionBox+=At;Closest=FMath::Min(Closest,FVector::Dist(Hand,W->GetFirstPersonMesh()->GetComponentTransform().TransformPosition(At)));}UE_LOG(LogTemp,Display,TEXT("ZXK_B_SECTION material=%d box=%s closest_hand=%.4f"),Section.MaterialIndex,*SectionBox.ToString(),Closest);}
        Check(TEXT("zxk_original_B_transition_completed"),W->Form==1&&W->Age<0);Capture(TEXT("zxk_08_B_idle"));
        Stage=214;Age=0;
    }
    else if(Stage==214&&Age>.5f)
    {
        W->StartFiring();W->StopFiring();W->Tick(.12f);Stage=218;Age=0;
    }
    else if(Stage==218&&Age>.3f){Capture(TEXT("zxk_09_B_light"));Stage=219;Age=0;}
    else if(Stage==219&&Age>.3f){W->Cancel();W->ToggleRangedForm();W->Tick(.2f);for(AHeliosSSXEffect* Effect:W->Effects)if(IsValid(Effect)){Effect->SetActorTickEnabled(false);Effect->Tick(.2f);}Check(TEXT("zxk_original_B_to_C_vertex_effect_loaded"),!W->Effects.IsEmpty());Stage=227;Age=0;}
    else if(Stage==227&&Age>.3f){Capture(TEXT("zxk_15_B_to_C_original_vertex_fx"));Stage=228;Age=0;}
    else if(Stage==228&&Age>.3f){W->Tick(3);Stage=215;Age=0;}
    else if(Stage==215&&Age>.5f)
    {
        Check(TEXT("zxk_original_C_transition_completed"),W->Form==2&&W->Age<0);W->Cancel();W->Pose();Capture(TEXT("zxk_10_C_dart_idle"));
        Stage=220;Age=0;
    }
    else if(Stage==220&&Age>.3f){W->StartFiring();W->StopFiring();W->Tick(.12f);Stage=216;Age=0;}
    else if(Stage==216&&Age>.5f)
    {
        Capture(TEXT("zxk_11_C_dart_throw"));Stage=221;Age=0;
    }
    else if(Stage==221&&Age>.3f){W->Tick(3);Check(TEXT("zxk_C_throw_spawns_projectile_and_consumes_one"),W->DartAmmo==19&&!W->Projectiles.IsEmpty());for(AHeliosZXKProjectile* Projectile:W->Projectiles)if(IsValid(Projectile)){Projectile->Tick(.015f);Projectile->SetActorTickEnabled(false);TArray<AActor*> Attached;Projectile->GetAttachedActors(Attached);Check(TEXT("zxk_C_original_projectile_trail_attached"),!Attached.IsEmpty());for(auto* Actor:Attached)if(auto* Effect=Cast<AHeliosSSXEffect>(Actor)){Effect->SetActorTickEnabled(false);Effect->Tick(.05f);}}Stage=229;Age=0;}
    else if(Stage==229&&Age>.3f){Capture(TEXT("zxk_16_C_original_projectile_trail"));Stage=230;Age=0;}
    else if(Stage==230&&Age>.3f){W->RequestSourceReload();W->Tick(.2f);Stage=217;Age=0;}
    else if(Stage==217&&Age>.5f)
    {
        Capture(TEXT("zxk_12_C_original_reload"));Stage=222;Age=0;
    }
    else if(Stage==222&&Age>.3f)
    {
        W->Tick(3);Check(TEXT("zxk_C_reload_restores_project_clip"),W->DartAmmo==20);W->Cancel();W->ToggleRangedForm();W->Tick(3);W->StartSpecialFire();W->Tick(.2f);
        for(AHeliosSSXEffect* Effect:W->Effects)if(IsValid(Effect)){Effect->SetActorTickEnabled(false);Effect->Tick(.2f);}
        Check(TEXT("zxk_original_B_skill01_effect_loaded"),!W->Effects.IsEmpty());Stage=223;Age=0;
    }
    else if(Stage==223&&Age>.3f){Capture(TEXT("zxk_13_B_original_skill01"));Stage=224;Age=0;}
    else if(Stage==224&&Age>.3f)
    {
        W->Cancel();W->StartSecondaryFire();W->StopSecondaryFire();const float Sample=W->Clips.FindRef(W->Action)->GetPlayLength()*.55f;W->Tick(Sample/UHeliosCombatTempoSettings::Hunter(W,true,false,TEXT("剑气动作倍率"),3.f,.1f,10.f));
        for(AHeliosSSXEffect* Effect:W->Effects)if(IsValid(Effect)){Effect->SetActorTickEnabled(false);Effect->Tick(Sample);}
        bool VisibleWave=false;
        for(AHeliosZXKProjectile* Projectile:W->Projectiles)if(IsValid(Projectile)){Projectile->SetActorTickEnabled(false);TArray<AActor*> Attached;Projectile->GetAttachedActors(Attached);for(auto* Actor:Attached)if(auto* Effect=Cast<AHeliosSSXEffect>(Actor)){Effect->SetActorTickEnabled(false);Effect->Tick(.15f);TInlineComponentArray<UStaticMeshComponent*> Meshes(Effect);for(auto* M:Meshes)VisibleWave|=M->IsVisible();}}
        Check(TEXT("zxk_right_click_B_sword_wave_has_visible_projectile_effect"),VisibleWave&&!W->Projectiles.IsEmpty()&&W->Action==TEXT("B_Skill02"));Stage=225;Age=0;
    }
    else if(Stage==225&&Age>.3f){Capture(TEXT("zxk_14_B_original_sword_wave"));Stage=226;Age=0;}
    else if(Stage==226&&Age>.3f){W->Cancel();W->SetActorTickEnabled(true);Finish();}
}

void AHeliosSSXDemo::Tick(float Dt)
{
    Super::Tick(Dt);if(Stage==99)return;const double Now=FPlatformTime::Seconds();if(!Started){Started=Now;GEngine->Exec(GetWorld(),TEXT("DisableAllScreenMessages"));GEngine->Exec(GetWorld(),FParse::Param(FCommandLine::Get(),TEXT("HeliosInputControlQA"))?TEXT("r.SetRes 1920x1080w"):TEXT("r.SetRes 1600x900w"));if(SSXTravelHop){Stage=20;Checks=SSXTravelChecks;Failures=SSXTravelFailures;}}
    // Rendered movement runs at 120 simulated FPS can exceed three wall-clock
    // minutes while saving every capture. Keep a finite wall watchdog without
    // changing simulation clocks, animation boundaries or assertion tolerances.
    const double Timeout=FParse::Param(FCommandLine::Get(),TEXT("HeliosZXKMovingWaveQA"))?420.:180.;
    if(Now-Started>Timeout){Check(TEXT("timeout"),false);Finish();return;}
    Age+=Dt;if(!Player)Player=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));if(!Player)return;
    auto* PC=Cast<APlayerController>(Player->GetController());auto* HUD=PC?Cast<AHeliosAdventureHUD>(PC->GetHUD()):nullptr;
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosInputControlQA"))){TickInputControlQA(PC,HUD);return;}
    if(Stage>=400&&Stage<=420){TickDamage07QA();return;}
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosZXKQA"))){TickZXKQA(PC,HUD);return;}
    auto* W=Cast<AHeliosSunShangXiangWeapon>(Player->GetEquippedWeapon());auto* PW=GetWorld()->GetSubsystem<UHeliosPetWorld>();auto* Pet=PW?Cast<AHeliosSunShangXiangPet>(PW->Pet):nullptr;
    if(Stage>=87&&Stage<=89)
    {
        UE_LOG(LogTemp,Display,TEXT("SSX_MOTION stage=%d age=%.6f phase=%.6f dt=%.6f wall=%.9f"),Stage,Age,W?W->Timeline.Age:0.f,Dt,Now);
        if(Stage==87&&W){int32 Visible=0;for(const auto& E:W->BladeEffects)if(IsValid(E))Visible+=E->VisibleTrackCount();Before=FMath::Max(Before,float(Visible));}

        if(!FParse::Param(FCommandLine::Get(),TEXT("HeliosMotionNoCapture"))&&Age-FeedbackLastSample>=(FParse::Param(FCommandLine::Get(),TEXT("HeliosOpacityOnlyQA"))?1.f/120.f:1.f/30.f)){FeedbackLastSample=Age;Capture(*FString::Printf(TEXT("ssx_continuous_%d_%03d"),Stage,FeedbackFrame++));}
        if(Stage==87&&Age>6.f)
        {
            Check(TEXT("blade_idle_wrap_keeps_two_loops"),W&&W->BladeEffects.Num()==2);
            Check(TEXT("blade_visible_tracks_bounded_across_loop"),Before>=17&&Before<=26);
            if(W)for(int32 I=0;I<W->BladeEffects.Num();++I){auto* E=W->BladeEffects[I].Get();Check(*FString::Printf(TEXT("blade_loop_%d_attached_source_bone"),I),IsValid(E)&&E->IsLooping()&&E->GetRootComponent()->GetAttachParent()==W->GetFirstPersonMesh()&&E->GetRootComponent()->GetAttachSocketName()==FName(I==0?TEXT("Bone01"):TEXT("Bone02")));}
            Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("rifle")));Stage=88;Age=0;FeedbackFrame=0;FeedbackLastSample=-1;
        }
        else if(Stage==88&&Age>1.f)
        {
            int32 Loops=0;for(TActorIterator<AHeliosSSXEffect> E(GetWorld());E;++E)if(E->IsLooping())++Loops;
            Check(TEXT("weapon_switch_clears_blade_loops"),Loops==0);
            Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("cfm_sunshangxiang")));Stage=89;Age=0;FeedbackFrame=0;FeedbackLastSample=-1;
        }
        else if(Stage==89&&Age>2.f){Check(TEXT("weapon_return_recreates_two_blade_loops"),W&&W->BladeEffects.Num()==2);Check(TEXT("blade_lifecycle_keeps_partner"),IsValid(Pet));Finish();}
        return;
    }
    if(Stage>=80&&Stage<=86)
    {
        if(Stage==84&&Age<1.f)
        {
            const float Pulse=FMath::FloorToFloat(Age/.1f);
            if(Pulse>Before){Before=Pulse;W->StartFiring();W->StopFiring();}
        }
        if(Stage==86)
        {
            if(Age>=.5f&&Before==0){W->StopFiring();Before=1;}
            if(Age>=1.f&&Before==1){W->StartFiring();Before=2;}
            if(Age>=1.5f&&Before==2){W->StopFiring();Before=3;}
        }
        if(Stage==85)PC->SetControlRotation(FRotator(15.f*FMath::Sin(Age*2.f),0,0));
    }
    if(Stage>=80&&Stage<=86)
    {
        const FVector L=W->GetFirstPersonMesh()->GetSocketLocation(TEXT("b_LeftHand")),R=W->GetFirstPersonMesh()->GetSocketLocation(TEXT("b_RightHand"));
        UE_LOG(LogTemp,Display,TEXT("SSX_MOTION stage=%d age=%.6f phase=%.6f dt=%.6f wall=%.9f left=%s right=%s"),Stage,Age,W->Timeline.Age,Dt,Now,*L.ToString(),*R.ToString());
        if(Stage==80&&!FParse::Param(FCommandLine::Get(),TEXT("HeliosViewClarityQA"))&&W->Timeline.Age>=.065f&&W->Timeline.Age<.4f)
        {
            auto* Mesh=W->GetFirstPersonMesh();TArray<FFinalSkinVertex> Vertices;Mesh->GetCPUSkinnedVertices(Vertices,0);
            const auto& LOD=Mesh->GetSkeletalMeshAsset()->GetResourceForRendering()->LODRenderData[0];const auto* Weights=LOD.GetSkinWeightVertexBuffer();
            const auto& Skeleton=Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
            FVector Eye;FRotator View;PC->GetPlayerViewPoint(Eye,View);const FVector Origin=Mesh->GetComponentTransform().InverseTransformPosition(Eye);
            for(FName Bone:{FName(TEXT("Bone01")),FName(TEXT("Bone02"))})
            {
                const int32 Index=Skeleton.FindBoneIndex(Bone);TArray<FVector> Points;
                for(const auto& Section:LOD.RenderSections)for(uint32 I=Section.BaseVertexIndex;I<Section.BaseVertexIndex+Section.NumVertices&&I<uint32(Vertices.Num());++I)
                {const int32 Local=Weights->GetBoneIndex(I,0);if(Section.BoneMap.IsValidIndex(Local)&&Section.BoneMap[Local]==Index)Points.Add(FVector(Vertices[I].Position));}
                if(Points.Num()<100)continue;FVector Mean=FVector::ZeroVector;for(const FVector& P:Points)Mean+=P;Mean/=Points.Num();
                FVector Axis(1,1,1);for(int32 Iteration=0;Iteration<12;++Iteration){FVector Next=FVector::ZeroVector;for(const FVector& P:Points){const FVector D=P-Mean;Next+=D*FVector::DotProduct(D,Axis);}Axis=Next.GetSafeNormal();}
                const FVector A=Mean-Origin-Axis*15,B=Mean-Origin+Axis*15;if(A.Y<5||B.Y<5)continue;
                const float X=B.X/B.Y-A.X/A.Y,Z=B.Z/B.Y-A.Z/A.Y;
                if(Bone==FName(TEXT("Bone01"))&&W->Timeline.Age>=.2f)
                    UE_LOG(LogTemp,Display,TEXT("SSX_NATIVE_RIGHT_SPAN phase=%.6f span=%.6f"),W->Timeline.Age,FMath::Abs(X));
                if(FMath::Abs(X)<.15f)continue;
                const float Tilt=FMath::RadiansToDegrees(FMath::Atan(FMath::Abs(Z/X)));
                const FVector C=Mean-Origin;
                UE_LOG(LogTemp,Display,TEXT("SSX_PROJECTED_BLADE phase=%.6f bone=%s vertices=%d tilt_deg=%.6f center_screen_x=%.6f center_screen_y=%.6f span=%.6f"),W->Timeline.Age,*Bone.ToString(),Points.Num(),Tilt,C.X/C.Y,C.Z/C.Y,FMath::Abs(X));
                Check(*FString::Printf(TEXT("native_blade_finite_%d_%s_%.3f"),FeedbackFrame,*Bone.ToString(),W->Timeline.Age),FMath::IsFinite(Tilt)&&!C.ContainsNaN());
            }
        }
        FVector CameraEye;FRotator CameraView;PC->GetPlayerViewPoint(CameraEye,CameraView);
        for(FName B:{FName(TEXT("b_RightArm")),FName(TEXT("b_RightForeArm")),FName(TEXT("b_RightHand"))})
        {const FVector V=CameraView.UnrotateVector(W->GetFirstPersonMesh()->GetSocketLocation(B)-CameraEye);UE_LOG(LogTemp,Display,TEXT("SSX_RIGHT_TRAJECTORY phase=%.6f bone=%s camera=%s"),W->Timeline.Age,*B.ToString(),*V.ToString());
            if(B==FName(TEXT("b_RightHand"))&&W->Timeline.Age>=.2f&&W->Timeline.Age<.4f)
                Check(*FString::Printf(TEXT("native_right_wrist_finite_%.3f"),W->Timeline.Age),!V.ContainsNaN());
        }
        if(!FParse::Param(FCommandLine::Get(),TEXT("HeliosMotionNoCapture"))&&Age-FeedbackLastSample>=(FParse::Param(FCommandLine::Get(),TEXT("HeliosOpacityOnlyQA"))?1.f/120.f:1.f/30.f)){FeedbackLastSample=Age;Capture(*FString::Printf(TEXT("ssx_continuous_%d_%03d"),Stage,FeedbackFrame++));}
        if(Age>3.f)
        {
            if(Stage==83||Stage==84||Stage==86)
                Check(*FString::Printf(TEXT("released_light_%d_returns_idle"),Stage),W->Timeline.Attack==HeliosSSX::EAttack::None);
            W->StopFiring();W->StopSecondaryFire();W->Cancel();
            Check(*FString::Printf(TEXT("native_action_%d_completed"),Stage),W->Timeline.Attack==HeliosSSX::EAttack::None);
            if(Stage==80&&FParse::Param(FCommandLine::Get(),TEXT("HeliosOpacityOnlyQA"))){Finish();return;}
            if((Stage==86&&!FParse::Param(FCommandLine::Get(),TEXT("HeliosViewClarityQA")))||(Stage==82&&!FParse::Param(FCommandLine::Get(),TEXT("HeliosSSXLateInputQA")))){Finish();return;}
            ++Stage;Age=0;FeedbackFrame=0;FeedbackLastSample=-1;Before=0;
            if(Stage==81)W->StartSecondaryFire();
            else if(Stage==82)W->StartSpecialFire();
            else
            {
                PC->SetControlRotation(FRotator::ZeroRotator);
                if(Stage==85)
                {
                    FVector Eye;FRotator View;PC->GetPlayerViewPoint(Eye,View);
                    EffectBackdrop=GetWorld()->SpawnActor<AStaticMeshActor>();
                    auto* M=EffectBackdrop->GetStaticMeshComponent();M->SetMobility(EComponentMobility::Movable);
                    M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
                    M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                    EffectBackdrop->SetActorLocation(Eye+View.Vector()*28.f);
                    EffectBackdrop->SetActorScale3D(FVector(.02f,3.f,3.f));
                    Check(TEXT("near_wall_front_plane_27cm"),EffectBackdrop!=nullptr);
                }
                if(Stage==86&&EffectBackdrop){EffectBackdrop->Destroy();EffectBackdrop=nullptr;}
                if(Stage!=87)W->StartFiring();
                if(Stage==83||Stage==84)W->StopFiring();
            }
        }return;
    }
    if(Stage==50||Stage==51){TickEffectQA(W,Pet);return;}
    if(Stage>=60&&Stage<=69){TickVoiceQA(W,Pet);return;}
    if(Stage==70){TickLightQA(W,Pet);return;}
    if(Stage==71){if(Age>.3f)Finish();return;}
    if(Stage==0&&Age>2)
    {OriginalHP=Player->GetMaxHealth();OriginalSpeed=Player->GetCharacterMovement()->MaxWalkSpeed;if(HUD){HUD->SelectWeapon(TEXT("cfm_sunshangxiang"));HUD->SelectPet(EHeliosPetKind::SunShangXiang);}Stage=1;Age=0;}
    else if(Stage==1&&Age>2){Capture(TEXT("01_menu"));Stage=2;Age=0;}
    else if(Stage==2&&Age>1){if(HUD)HUD->StartSelectedRun();if(auto* D=AHeliosAdventureDirector::Find(GetWorld());D&&!D->IsSelectingWeapon())Stage=3;Age=0;}
    else if(Stage==3&&Age>3)
    {
        Check(TEXT("menu_weapon_and_companion_independent"),W&&Pet);if(!W||!Pet){Finish();return;}
        Check(TEXT("player_health_unchanged"),Player->GetMaxHealth()==OriginalHP);Check(TEXT("player_speed_unchanged"),Player->GetCharacterMovement()->MaxWalkSpeed==OriginalSpeed);
        Check(TEXT("original_meshes_loaded"),W->GetFirstPersonMesh()->GetSkeletalMeshAsset()&&Pet->bModelReady);
        if(auto* Director=AHeliosAdventureDirector::Find(GetWorld()))Director->SetActorTickEnabled(false);
        for(TActorIterator<AHeliosEnemyActor> E(GetWorld());E;++E)E->Destroy();Player->SetCanBeDamaged(false);
        PC->SetControlRotation(FRotator(0,0,0));Pet->SetActorTickEnabled(false);if(auto* AI=Cast<AAIController>(Pet->GetController()))AI->StopMovement();Pet->GetCharacterMovement()->StopMovementImmediately();Pet->GetCharacterMovement()->DisableMovement();
        Pet->SetActorLocation(Player->GetActorLocation()+FVector(300,160,-6));Pet->SetActorRotation(FRotator(0,180,0));Pet->SetPose(TEXT("Idle"));
        if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();Stage=4;Age=0;
    }
    else if(Stage==4&&Age>2)
    {
        Check(TEXT("initial_map_hands_evaluated"),W->GetFirstPersonMesh()->GetSocketTransform(TEXT("b_RightHand"),RTS_Component).GetLocation().Z>-25.f);
        Capture(TEXT("02_dual_selection_idle"));
        for(auto* M:{W->GetFirstPersonMesh(),Pet->GetMesh()})
        {
            auto* A=Cast<UHeliosHandAnimInstance>(M->GetAnimInstance());
            UE_LOG(LogTemp,Display,TEXT("SSX_VISUAL %s transform=%s bounds=%s anim=%s seconds=%f visible=%d hidden=%d"),*M->GetPathName(),*M->GetComponentTransform().ToString(),*M->Bounds.ToString(),*GetNameSafe(A?A->Sequence:nullptr),A?A->SampleSeconds:-1.f,M->IsVisible(),M->bHiddenInGame);
            for(FName B:{FName(TEXT("Bip01")),FName(TEXT("b_RightHand")),FName(TEXT("b_LeftHand"))})UE_LOG(LogTemp,Display,TEXT("SSX_BONE %s %s"),*B.ToString(),*M->GetSocketTransform(B,RTS_Component).ToString());
        }
        Stage=FParse::Param(FCommandLine::Get(),TEXT("HeliosMotionFeedback"))?80:FParse::Param(FCommandLine::Get(),TEXT("HeliosSSXLightQA"))?70:FParse::Param(FCommandLine::Get(),TEXT("HeliosSSXVoiceQA"))?60:FParse::Param(FCommandLine::Get(),TEXT("HeliosSSXEffectQA"))?50:40;Age=0;
        if(Stage==80){W->Cancel();W->StartFiring();FeedbackLastSample=-1;FeedbackFrame=0;}
    }
    else if(Stage==40&&Age>.3f)
    {
        if(FParse::Param(FCommandLine::Get(),TEXT("HeliosDamage07QA"))){Stage=400;Age=0;return;}
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Target=GetWorld()->SpawnActor<AHeliosEnemyE01>(Player->GetActorLocation()+FVector(200,0,0),FRotator(0,180,0),P);Target->CurrentHP=10000;Target->StopShooting();Target->GetCharacterMovement()->DisableMovement();Target->SetActorTickEnabled(false);GetWorld()->GetTimerManager().ClearAllTimersForObject(Target);if(Target->GetController())Target->GetController()->Destroy();
        Before=Target->CurrentHP;Player->StartFireFromAutomation();Player->StopFireFromAutomation();Stage=5;Age=0;Captured=false;
    }
    else if(Stage==5)
    {
        if(!Captured&&Age>.2f){Capture(TEXT("03_light"));Captured=true;}
        if(Age>1.5f){Check(TEXT("light_whole_attack_100"),FMath::IsNearlyEqual(Before-Target->CurrentHP,100));Before=Target->CurrentHP;Player->StartSecondaryAttack();Player->StopSecondaryAttack();Stage=6;Age=0;Captured=false;}
    }
    else if(Stage==6)
    {
        if(!Captured&&Age>.58f){Capture(TEXT("04_heavy"));Captured=true;}
        if(Age>2.3f){Check(TEXT("heavy_whole_attack_250"),FMath::IsNearlyEqual(Before-Target->CurrentHP,250));Before=Target->CurrentHP;Player->StartSpecialAttack();Stage=7;Age=0;Captured=false;}
    }
    else if(Stage==7)
    {
        if(!Captured&&Age>.17f){Capture(TEXT("05_skill"));Captured=true;}
        if(Age>2.5f){Check(TEXT("skill_four_phases_600"),FMath::IsNearlyEqual(Before-Target->CurrentHP,600));const float Remaining=W->CooldownRemaining();Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("rifle")));Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("cfm_sunshangxiang")));W=Cast<AHeliosSunShangXiangWeapon>(Player->GetEquippedWeapon());Check(TEXT("switch_preserves_skill_cooldown"),W&&W->CooldownRemaining()>=Remaining-.05f);Before=Target->CurrentHP;Stage=8;Age=0;}
    }
    else if(Stage==8&&Age>1.5f)
    {
        Check(TEXT("switch_cancels_lingering_damage"),Target->CurrentHP==Before);Capture(TEXT("06_cooldown"));
        FDamageEvent Event;Player->SetCanBeDamaged(true);const int32 HurtBefore=W->Voice->HurtCount;
        Player->TakeDamage(1,Event,nullptr,Target);Player->TakeDamage(1,Event,nullptr,Target);
        Check(TEXT("player_hurt_voice_throttled"),W->Voice->HurtCount==HurtBefore+1);Player->Heal(2);Player->SetCanBeDamaged(false);
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        FVector Eye;FRotator R;Player->GetActorEyesViewPoint(Eye,R);
        auto* Victim=GetWorld()->SpawnActor<AHeliosEnemyE01>(Eye+R.Vector()*200,FRotator::ZeroRotator,P);Victim->CurrentHP=1;
        const int32 KillBefore=W->Voice->KillCount;W->Cancel();W->Timeline.Begin(HeliosSSX::EAttack::Light);W->Contact(0);W->Contact(1);
        Check(TEXT("player_kill_voice_once_per_enemy"),Victim->IsEnemyDead()&&W->Voice->KillCount==KillBefore+1);Victim->Destroy();W->Cancel();
        Pet->SetActorTickEnabled(true);Pet->GetCharacterMovement()->SetMovementMode(MOVE_Walking);Pet->Target=Target;Before=Target->CurrentHP;Stage=9;Age=0;
    }
    else if(Stage==9&&Age>5)
    {Check(TEXT("companion_assists_combat"),Target->CurrentHP<Before);Check(TEXT("companion_600_health"),Pet->Settings.MaxHealth==600);Capture(TEXT("07_companion_combat"));Stage=90;Age=0;}
    else if(Stage==90&&Age>.3f)
    {
        FDamageEvent Event;const int32 HurtBefore=Pet->Voice->HurtCount;Pet->TakeDamage(1,Event,nullptr,Target);Pet->TakeDamage(1,Event,nullptr,Target);
        Check(TEXT("companion_hurt_voice_throttled"),Pet->Voice->HurtCount==HurtBefore+1);
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Victim=GetWorld()->SpawnActor<AHeliosEnemyE01>(Pet->GetActorLocation()+Pet->GetActorForwardVector()*200,FRotator::ZeroRotator,P);Victim->CurrentHP=1;
        // The existing combat target may stand between the pet and this independent voice fixture.
        const bool PreviousCollision=Target->GetActorEnableCollision();Target->SetActorEnableCollision(false);
        const int32 KillBefore=Pet->Voice->KillCount;Pet->Voice->Stop();Pet->CancelAttack();Pet->Timeline.Begin(HeliosSSX::EAttack::Light);Pet->Contact(0);Pet->Contact(1);
        UE_LOG(LogTemp,Display,TEXT("SSX_PET_KILL fixture_hp=%f voices_before=%d voices_after=%d"),Victim->CurrentHP,KillBefore,Pet->Voice->KillCount);Target->SetActorEnableCollision(PreviousCollision);
        Check(TEXT("companion_kill_voice_once_per_enemy"),Victim->IsEnemyDead()&&Pet->Voice->KillCount==KillBefore+1);Victim->Destroy();
        const int32 DeathBefore=Pet->Voice->DeathCount;Pet->TakeDamage(10000,Event,nullptr,Target);
        Check(TEXT("companion_down_voice"),Pet->IsSleeping()&&Pet->Voice->DeathCount==DeathBefore+1);Pet->SleepRemaining=.2f;
        Stage=10;Age=0;
    }
    else if(Stage==10&&Age>2)
    {
        Check(TEXT("companion_recovers_from_down"),!Pet->IsSleeping()&&Pet->Health==Pet->Settings.MaxHealth);
        Target->Destroy();Target=nullptr;Pet->Target=nullptr;Pet->Restore(Pet->Health,0);
        FollowStart=Pet->GetActorLocation();Player->SetActorLocation(Player->GetActorLocation()+FVector(600,0,0));FVector Goal;const bool Found=Pet->FindFollowPosition(Goal);Check(TEXT("companion_follow_goal_on_navmesh"),Found);FollowDistance=Found?FVector::Dist2D(FollowStart,Goal):0;Stage=11;Age=0;
    }
    else if(Stage==11&&Age>6)
    {
        FVector Goal;const bool Found=Pet->FindFollowPosition(Goal);UE_LOG(LogTemp,Display,TEXT("SSX_FOLLOW moved=%f initial_goal=%f remaining_goal=%f"),FVector::Dist2D(FollowStart,Pet->GetActorLocation()),FollowDistance,Found?FVector::Dist2D(Pet->GetActorLocation(),Goal):-1.f);
        Check(TEXT("companion_navigates_to_follow_position"),Found&&FVector::Dist2D(FollowStart,Pet->GetActorLocation())>100&&FVector::Dist2D(Pet->GetActorLocation(),Goal)<FollowDistance-100);
        if(FParse::Param(FCommandLine::Get(),TEXT("HeliosSSXDirect"))){Finish();return;}
        Pet->Restore(275,0);HeliosSSX::Session(this)->PlayerRemaining=100;HeliosSSX::Session(this)->PetRemaining=80;
        SSXTravelHop=1;const bool Travel=GetGameInstance()->GetSubsystem<UHeliosMapTravelSubsystem>()->RequestTravel(TEXT("ikun_town"),TEXT("cfm_sunshangxiang"));
        Check(TEXT("travel_to_town_requested"),Travel);SSXTravelChecks=Checks;SSXTravelFailures=Failures;Stage=99;if(!Travel)Finish();
    }
    else if(Stage==20&&W&&Pet)
    {
        Player->SetCanBeDamaged(false);if(auto* Director=AHeliosAdventureDirector::Find(GetWorld()))Director->SetActorTickEnabled(false);
        for(TActorIterator<AHeliosEnemyActor> E(GetWorld());E;++E)E->Destroy();
        Check(*FString::Printf(TEXT("map_%d_weapon_and_companion_restored"),SSXTravelHop),Player->GetWeaponId()==TEXT("cfm_sunshangxiang")&&Pet->Kind==EHeliosPetKind::SunShangXiang);
        Check(*FString::Printf(TEXT("map_%d_cooldowns_preserved"),SSXTravelHop),W->CooldownRemaining()>50&&HeliosSSX::Session(this)->PetRemaining>30);
        Check(*FString::Printf(TEXT("map_%d_companion_state_preserved"),SSXTravelHop),SSXTravelHop==2?Pet->IsSleeping()&&Pet->SleepRemaining>10:Pet->Health==275);
        int32 CurrentBladeLoops=0;bool NoStaleEffects=true;
        for(TActorIterator<AHeliosSSXEffect> E(GetWorld());E;++E)
        {
            const auto* Root=E->GetRootComponent();const FName Socket=Root?Root->GetAttachSocketName():NAME_None;
            const bool Current=E->IsLooping()&&E->GetOwner()==W&&W->BladeEffects.Contains(*E)&&Root&&Root->GetAttachParent()==W->GetFirstPersonMesh()&&(Socket==TEXT("Bone01")||Socket==TEXT("Bone02"));
            if(Current)++CurrentBladeLoops;else NoStaleEffects=false;
        }
        Check(*FString::Printf(TEXT("map_%d_no_stale_effects"),SSXTravelHop),NoStaleEffects&&CurrentBladeLoops==2);
        Stage=21;Age=0;
    }
    else if(Stage==21&&Age>2)
    {
        auto* M=W->GetFirstPersonMesh();auto* Hand=Cast<UHeliosHandAnimInstance>(M->GetAnimInstance());auto* Node=M->GetSingleNodeInstance();
        auto* ActualClip=Hand?Hand->Sequence.Get():Cast<UAnimSequence>(Node?Node->GetCurrentAsset():nullptr);
        const float ActualSeconds=Hand?Hand->SampleSeconds:Node?Node->GetCurrentTime():-1.f;
        UE_LOG(LogTemp,Display,TEXT("SSX_MAP_VIEW hop=%d camera=%s fov=%f mesh_relative=%s anim=%s seconds=%f active=%d tick=%d attack=%d age=%f idle_age=%f velocity=%s"),SSXTravelHop,*Player->FirstPersonCamera->GetComponentTransform().ToString(),Player->FirstPersonCamera->FieldOfView,*M->GetRelativeTransform().ToString(),*GetNameSafe(ActualClip),ActualSeconds,W->bActive,W->IsActorTickEnabled(),int32(W->Timeline.Attack),W->Timeline.Age,W->IdleAge,*Player->GetVelocity().ToString());
        UE_LOG(LogTemp,Display,TEXT("SSX_MAP_BONES right=%s left=%s component_tick=%d pause_anim=%d"),*M->GetSocketTransform(TEXT("b_RightHand"),RTS_Component).ToString(),*M->GetSocketTransform(TEXT("b_LeftHand"),RTS_Component).ToString(),M->IsComponentTickEnabled(),M->bPauseAnims);
        Check(*FString::Printf(TEXT("map_%d_idle_hands_evaluated"),SSXTravelHop),ActualClip==HeliosSSX::Animation(true,TEXT("Idle"))&&M->GetSocketTransform(TEXT("b_RightHand"),RTS_Component).GetLocation().Z>-25.f&&M->GetSocketTransform(TEXT("b_LeftHand"),RTS_Component).GetLocation().Z>-25.f);
        Check(*FString::Printf(TEXT("map_%d_camera_and_attachment_consistent"),SSXTravelHop),FMath::IsNearlyEqual(Player->FirstPersonCamera->FieldOfView,90.f)&&M->GetRelativeLocation().Equals(FVector::ZeroVector,.01f)&&M->GetRelativeScale3D().Equals(FVector::OneVector,.01f));
        Capture(*FString::Printf(TEXT("%02d_map_%d"),7+SSXTravelHop,SSXTravelHop));Stage=22;Age=0;
    }
    else if(Stage==22&&Age>.3f)
    {
        if(SSXTravelHop==2&&(Pet->IsSleeping()||Pet->State==EHeliosPetState::Waking)){Pet->SleepRemaining=FMath::Min(Pet->SleepRemaining,.1f);return;}
        if(SSXTravelHop<3)
        {
            if(SSXTravelHop==1)Pet->Restore(0,25);else Pet->Restore(275,0);
            ++SSXTravelHop;
            const bool Travel=GetGameInstance()->GetSubsystem<UHeliosMapTravelSubsystem>()->RequestTravel(SSXTravelHop==2?TEXT("ikun_ghost_village"):TEXT("HeliosAdventure"),TEXT("cfm_sunshangxiang"));
            Check(*FString::Printf(TEXT("travel_%d_requested"),SSXTravelHop),Travel);SSXTravelChecks=Checks;SSXTravelFailures=Failures;Stage=99;if(!Travel)Finish();
        }
        else
        {
            Player->SetCanBeDamaged(true);Stage=23;Age=0;
        }
    }
    else if(Stage==23)
    {
        if(Age<5)return;
        W->Cancel();W->Timeline.Begin(HeliosSSX::EAttack::Heavy);W->ShowEffect(0);
        Check(TEXT("death_test_has_active_attack_and_effect"),!W->Effects.IsEmpty());
        UGameplayStatics::ApplyDamage(Player,100000,nullptr,nullptr,nullptr);
        if(Player->IsDead()){Check(TEXT("death_cancels_weapon"),W->Timeline.Attack==HeliosSSX::EAttack::None&&W->Effects.IsEmpty());Finish();}
    }
}

// Optional, deterministic render fixtures. Normal play and the combat demo are unchanged.
void AHeliosSSXDemo::TickLightQA(AHeliosSunShangXiangWeapon* W,AHeliosSunShangXiangPet* Pet)
{
    if(!W||!Pet){Check(TEXT("light_fixture_models_exist"),false);Finish();return;}
    constexpr float FP[]={0,.04f,.065f,.1f,.15f,.2f,.24235294f,.32705882f,.41176471f,.5f,.54166665f,.59666665f};
    constexpr float TP[]={0,.05f,.13f,.2f,.2666667f,.35f,.45f,.52f,.65f,.8f,.9f,.9333333f};
    const bool First=EffectSample<12;const int32 Index=EffectSample%12;const float Sample=First?FP[Index]:TP[Index];
    if(!Captured)
    {
        W->SetActorTickEnabled(false);W->Cancel();Pet->CancelAttack();
        W->GetFirstPersonMesh()->SetVisibility(First);W->GetFirstPersonMesh()->SetComponentTickEnabled(true);
        Pet->GetMesh()->SetComponentTickEnabled(true);
        if(First){W->Timeline.Begin(HeliosSSX::EAttack::Light);W->Timeline.Age=Sample;W->Pose();}
        else
        {
            Pet->SetActorLocation(Player->GetActorLocation()+FVector(280,0,-6));Pet->SetActorRotation(FRotator(0,180,0));
            Pet->Timeline.Begin(HeliosSSX::EAttack::Light);Pet->Timeline.Age=Sample;
            FName Clip;float Time;Pet->Timeline.Pose(false,Clip,Time);Pet->PoseAt(Clip,Time,false);
        }
        auto* M=First?W->GetFirstPersonMesh():Pet->GetMesh();
        M->TickAnimation(UHeliosHandAnimInstance::BlendSeconds+.01f,false);M->RefreshBoneTransforms();
        float ScaleError=0;bool Finite=true;const auto& Bones=M->GetBoneSpaceTransforms();
        for(int32 I=0;I<Bones.Num();++I)
        {
            Finite&=!Bones[I].ContainsNaN()&&Bones[I].GetRotation().IsNormalized();
            const FString Name=M->GetBoneName(I).ToString();
            if(Name.StartsWith(TEXT("b_"))||Name.StartsWith(TEXT("Bone")))ScaleError=FMath::Max(ScaleError,(Bones[I].GetScale3D()-FVector::OneVector).GetAbsMax());
        }
        Captured=true;Age=0;return;
    }
    if(Age>.08f)
    {
        auto* M=First?W->GetFirstPersonMesh():Pet->GetMesh();
        float ScaleError=0;bool Finite=true;const auto& Bones=M->GetBoneSpaceTransforms();
        for(int32 I=0;I<Bones.Num();++I)
        {
            Finite&=!Bones[I].ContainsNaN()&&Bones[I].GetRotation().IsNormalized();const FString Name=M->GetBoneName(I).ToString();
            if(Name.StartsWith(TEXT("b_"))||Name.StartsWith(TEXT("Bone")))ScaleError=FMath::Max(ScaleError,(Bones[I].GetScale3D()-FVector::OneVector).GetAbsMax());
        }
        const FVector Left=M->GetSocketTransform(TEXT("b_LeftHand"),RTS_Component).GetLocation();
        if(Index==0)EffectQAOrigin=Left;
        if(Index==2)Check(First?TEXT("fp_light_wrist_moves"):TEXT("tp_light_wrist_moves"),FVector::Dist(Left,EffectQAOrigin)>1.f);
        Check(*FString::Printf(TEXT("%s_light_%02d_finite"),First?TEXT("fp"):TEXT("tp"),Index),Finite);
        Check(*FString::Printf(TEXT("%s_light_%02d_stable_proportions"),First?TEXT("fp"):TEXT("tp"),Index),ScaleError<.001f);
        UE_LOG(LogTemp,Display,TEXT("SSX_LIGHT_FRAME %s time=%.6f scale_error=%.8f left=%s right=%s"),First?TEXT("FP"):TEXT("TP"),Sample,ScaleError,*M->GetSocketTransform(TEXT("b_LeftHand"),RTS_Component).ToString(),*M->GetSocketTransform(TEXT("b_RightHand"),RTS_Component).ToString());
        Capture(*FString::Printf(TEXT("light_%s_%02d"),First?TEXT("fp"):TEXT("tp"),Index));
        if(++EffectSample>=24){Stage=71;Age=0;return;}Captured=false;
    }
}

void AHeliosSSXDemo::TickEffectQA(AHeliosSunShangXiangWeapon* W,AHeliosSunShangXiangPet* Pet)
{
    if(!W||!Pet){Check(TEXT("effect_qa_actors_present"),false);Finish();return;}
    const bool Sweep=FParse::Param(FCommandLine::Get(),TEXT("HeliosSSXEffectFrames"));
    constexpr int32 SweepFrames=136; // 60 Hz source frames, including the final fade.
    if(Stage==51)
    {
        if(Age<(EffectSample>=14?.02f:.2f))return;
        if(++EffectSample>=14+(Sweep?SweepFrames*2:0)){W->Cancel();Pet->CancelAttack();Finish();return;}
        Stage=50;Captured=false;Age=0;
    }
    auto* PC=Cast<APlayerController>(Player->GetController());
    if(!EffectBackdrop)
    {
        EffectQAOrigin=Player->GetActorLocation();Player->GetCharacterMovement()->DisableMovement();
        W->SetActorTickEnabled(false);W->GetFirstPersonMesh()->SetComponentTickEnabled(false);Pet->GetMesh()->SetVisibility(false);
        EffectBackdrop=GetWorld()->SpawnActor<AStaticMeshActor>();auto* M=EffectBackdrop->GetStaticMeshComponent();
        M->SetMobility(EComponentMobility::Movable);M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        M->SetCollisionEnabled(ECollisionEnabled::NoCollision);M->SetCastShadow(false);
        EffectBackdropMaterial=UMaterialInstanceDynamic::Create(LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")),this);
        M->SetMaterial(0,EffectBackdropMaterial);GEngine->Exec(GetWorld(),TEXT("r.EyeAdaptationQuality 0"));
    }
    if(!Captured)
    {
        W->Cancel();Pet->CancelAttack();Player->SetActorLocation(EffectQAOrigin);PC->SetControlRotation(FRotator::ZeroRotator);
        W->GetFirstPersonMesh()->SetVisibility(true);Pet->GetMesh()->SetVisibility(false);
        const bool Dark=(EffectSample>=4&&EffectSample<8)||EffectSample>=14+SweepFrames;
        const float Brightness=Dark?.005f:.85f;EffectBackdropMaterial->SetVectorParameterValue(TEXT("Color"),FLinearColor(Brightness,Brightness,Brightness));
        FVector Eye;FRotator R;PC->GetPlayerViewPoint(Eye,R);EffectBackdrop->SetActorLocation(Eye+FVector(1000,0,0));EffectBackdrop->SetActorScale3D(FVector(.1f,100,100));
        const float Starts[]={0,.6f,.7666667f,1.0333333f};const float Samples[]={.17f,.66f,.88f,1.2f};
        auto SkillSample=[&](float Sample)
        {
            W->Timeline.Begin(HeliosSSX::EAttack::Skill);W->Timeline.Age=Sample;W->Pose();
            for(int32 Phase=0;Phase<4;++Phase)if(Sample>=Starts[Phase])
            {
                const int32 BeforeEffects=W->Effects.Num();W->ShowEffect(Phase);
                if(W->Effects.Num()>BeforeEffects){auto* E=W->Effects.Last().Get();E->SetActorTickEnabled(false);E->Tick((Sample-Starts[Phase])/W->PlaybackSpeed);}
                else Check(TEXT("skill_phase_effect_spawned"),false);
            }
        };
        if(EffectSample<8)SkillSample(Samples[EffectSample%4]);
        else if(EffectSample==8)
        {
            SkillSample(.3f);Player->SetActorLocation(EffectQAOrigin+FVector(0,100,0));PC->SetControlRotation(FRotator(12,25,0));
            PC->GetPlayerViewPoint(Eye,R);EffectBackdrop->SetActorLocation(Eye+R.Vector()*1000);EffectBackdrop->SetActorRotation(R);
            for(const auto& E:W->Effects)E->Tick(0);
            Check(TEXT("effect_survives_movement_and_view_rotation"),!W->Effects.IsEmpty());
        }
        else if(EffectSample==9)
        {
            SkillSample(1.2f);for(const auto& E:W->Effects)E->Tick(10);
            Check(TEXT("all_skill_effects_expire"),!W->Effects.ContainsByPredicate([](const auto& E){return IsValid(E);}));W->Cancel();
        }
        else if(EffectSample==10)
        {
            SkillSample(.88f);W->DeactivateWeapon();Check(TEXT("unequip_removes_all_skill_effects"),W->Effects.IsEmpty());W->ActivateWeapon(NAME_None);W->Cancel();
        }
        else if(EffectSample==11||EffectSample==12)
        {
            const bool Heavy=EffectSample==12;const float Sample=Heavy?.58f:.2f;
            W->Timeline.Begin(Heavy?HeliosSSX::EAttack::Heavy:HeliosSSX::EAttack::Light);W->Timeline.Age=Sample;W->Pose();W->ShowEffect(0);
            for(const auto& E:W->Effects){E->SetActorTickEnabled(false);E->Tick(Sample/W->PlaybackSpeed);}
            Check(Heavy?TEXT("heavy_shared_trail_loaded"):TEXT("light_shared_trail_loaded"),!W->Effects.IsEmpty());
        }
        else if(EffectSample>=14)SkillSample(float((EffectSample-14)%SweepFrames)/60.f);
        else
        {
            W->GetFirstPersonMesh()->SetVisibility(false);Pet->GetMesh()->SetVisibility(true);
            Pet->SetActorLocation(EffectQAOrigin+FVector(250,0,0));Pet->SetActorRotation(FRotator(0,180,0));Pet->Timeline.Begin(HeliosSSX::EAttack::Skill);Pet->Timeline.Age=1.2f;
            Pet->PoseAt(TEXT("Skill04"),1.2f-Starts[3],false);Pet->ShowEffect(3);
            for(const auto& E:Pet->SourceEffects){E->SetActorTickEnabled(false);E->Tick((1.2f-Starts[3])/Pet->PlaybackSpeed);}
            Check(TEXT("companion_shared_skill_effect_loaded"),!Pet->SourceEffects.IsEmpty());
        }
        // The fixture freezes source time. Settle the hand animation's inertia at
        // that same time so the screenshot shows the chosen phase, not the idle pose.
        for(auto* M:{W->GetFirstPersonMesh(),Pet->GetMesh()})
        {M->TickAnimation(UHeliosHandAnimInstance::BlendSeconds+.01f,false);M->RefreshBoneTransforms();}
        if(EffectSample!=8)EffectBackdrop->SetActorRotation(FRotator::ZeroRotator);
        Captured=true;Age=0;return;
    }
    if(Age>(EffectSample>=14?.03f:.5f)&&Stage==50)
    {
        const TCHAR* Names[]={TEXT("skill1_bright"),TEXT("skill2_bright"),TEXT("skill3_bright"),TEXT("skill4_bright"),
            TEXT("skill1_dark"),TEXT("skill2_dark"),TEXT("skill3_dark"),TEXT("skill4_dark"),TEXT("moving_and_rotating"),TEXT("expired"),TEXT("unequipped"),TEXT("light"),TEXT("heavy"),TEXT("companion_skill4")};
        const FString FrameName=FString::Printf(TEXT("skill_frame_%03d_%s"),(EffectSample-14)%SweepFrames,EffectSample>=14+SweepFrames?TEXT("dark"):TEXT("bright"));
        Capture(EffectSample>=14?*FrameName:Names[EffectSample]);Stage=51;Age=0;
    }
}


void AHeliosSSXDemo::TickDamage07QA()
{
    auto* W=Cast<AHeliosSunShangXiangWeapon>(Player->GetEquippedWeapon());
    auto* Z=Cast<AHeliosZhongXiaoKuiWeapon>(Player->GetEquippedWeapon());
    auto* F=Player->GetEquippedWeapon()?Player->GetEquippedWeapon()->FindComponentByClass<UHeliosHunterDamageFeedback>():nullptr;
    auto* PW=GetWorld()->GetSubsystem<UHeliosPetWorld>();auto* Pet=PW?Cast<AHeliosSunShangXiangPet>(PW->Pet):nullptr;
    auto Next=[&](int32 To){Stage=To;Age=0;Damage07Before=F?F->Played:0;};
    if(Stage==400)
    {
        if(!W||!Pet){Check(TEXT("damage07_player_and_partner_fixture"),false);Finish();return;}
        W->Cancel();Pet->CancelAttack();Pet->SetActorTickEnabled(false);
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        FVector Eye;FRotator R;Player->GetActorEyesViewPoint(Eye,R);
        Target=GetWorld()->SpawnActor<AHeliosEnemyE01>(Eye+R.Vector()*200,FRotator::ZeroRotator,P);
        Target->CurrentHP=100000;Target->SetActorTickEnabled(false);Target->GetCharacterMovement()->DisableMovement();GetWorld()->GetTimerManager().ClearAllTimersForObject(Target);if(Target->GetController())Target->GetController()->Destroy();
        UAudioMixerBlueprintLibrary::StartRecordingOutput(this,60.f);
        Next(401);Player->StartFireFromAutomation();Player->StopFireFromAutomation();
    }
    else if(Stage==401&&Age>.8f){Check(TEXT("damage07_ssx_nonlethal_light_two_contacts"),F->Played-Damage07Before==2&&!Target->IsEnemyDead());Next(402);HeavyTargetHPBefore=Target->CurrentHP;FeedbackFrame=0;FeedbackLastSample=-1;Player->StartSecondaryAttack();Player->StopSecondaryAttack();}
    else if(Stage==402&&Age<=2.5f)
    {
        const float Clock=W->Timeline.Age;
        if(W->Timeline.Attack==HeliosSSX::EAttack::Heavy)
        {
            const int32 Expected=Clock+KINDA_SMALL_NUMBER<.9f?0:Clock+KINDA_SMALL_NUMBER<1.0333333f?1:2;
            Check(*FString::Printf(TEXT("heavy_contact_frame_%03d"),FeedbackFrame),F->Played-Damage07Before==Expected);
            UE_LOG(LogTemp,Display,TEXT("HEAVY_CONTACT clock=%.6f hp=%.1f damage=%.1f contacts=%d expected=%d"),Clock,Target->CurrentHP,HeavyTargetHPBefore-Target->CurrentHP,F->Played-Damage07Before,Expected);
        }
        if(Age-FeedbackLastSample>=1.f/30.f){FeedbackLastSample=Age;Capture(*FString::Printf(TEXT("heavy_contact_%03d"),FeedbackFrame));}
        ++FeedbackFrame;
    }
    else if(Stage==402&&Age>2.5f){Check(TEXT("damage07_ssx_heavy_two_source_contacts"),F->Played-Damage07Before==2);Next(403);Player->StartSpecialAttack();}
    else if(Stage==403&&Age>2.5f)
    {
        Check(TEXT("damage07_ssx_skill_four_events"),F->Played-Damage07Before==4);
        W->Cancel();auto Contact=[&](){++W->DamageAttackSerial;W->Timeline.Begin(HeliosSSX::EAttack::Light);W->HitAudioGate.Reset();W->Contact(0);};
        int32 N=F->Played;Target->SetCanBeDamaged(false);Contact();Check(TEXT("damage07_invulnerable_no_sound"),F->Played==N);Target->SetCanBeDamaged(true);
        const FVector At=Target->GetActorLocation();Target->SetActorLocation(At+FVector(5000,0,0));Contact();Check(TEXT("damage07_empty_swing_no_sound"),F->Played==N);Target->SetActorLocation(At);
        FVector Eye;FRotator R;Player->GetActorEyesViewPoint(Eye,R);EffectBackdrop=GetWorld()->SpawnActor<AStaticMeshActor>();auto* M=EffectBackdrop->GetStaticMeshComponent();M->SetMobility(EComponentMobility::Movable);M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));M->SetCollisionEnabled(ECollisionEnabled::QueryOnly);M->SetCollisionResponseToAllChannels(ECR_Block);EffectBackdrop->SetActorLocation(Eye+R.Vector()*100);EffectBackdrop->SetActorScale3D(FVector(.2f,2,2));Contact();Check(TEXT("damage07_wall_blocks_sound"),F->Played==N);EffectBackdrop->Destroy();EffectBackdrop=nullptr;
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;auto* Extra=GetWorld()->SpawnActor<AHeliosEnemyE01>(At+FVector(0,30,0),FRotator::ZeroRotator,P);Extra->CurrentHP=100000;Extra->SetActorTickEnabled(false);Extra->GetCharacterMovement()->DisableMovement();if(Extra->GetController())Extra->GetController()->Destroy();
        Contact();Check(TEXT("damage07_group_one_sound_for_one_contact"),F->Played==N+1&&Extra->CurrentHP<100000);N=F->Played;const uint64 ID=W->DamageAttackSerial<<8;F->Emit(ID);Check(TEXT("damage07_duplicate_event_does_not_replay"),F->Played==N&&F->Duplicates>0);Extra->Destroy();
        Pet->SetActorLocation(Player->GetActorLocation());Pet->SetActorRotation(FRotator::ZeroRotator);Pet->CancelAttack();Pet->Timeline.Begin(HeliosSSX::EAttack::Light);Pet->Contact(0);Contact();Check(TEXT("damage07_simultaneous_partner_player_only"),F->Played==N+1&&Pet->FindComponentByClass<UHeliosHunterDamageFeedback>()==nullptr);
        W->Cancel();Next(404);Player->StartFireFromAutomation();
    }
    else if(Stage==404&&Age>2.1f)
    {
        Player->StopFireFromAutomation();W->Cancel();Check(TEXT("damage07_rapid_held_contacts_not_throttled"),F->Played-Damage07Before>=10);Check(TEXT("damage07_original_single_voice_limit"),F->ActiveCount()<=1&&F->Replaced>0);
        const int32 N=F->Played;Target->CurrentHP=1;++W->DamageAttackSerial;W->Timeline.Begin(HeliosSSX::EAttack::Light);W->HitAudioGate.Reset();W->Contact(0);Check(TEXT("damage07_lethal_contact_also_plays"),F->Played==N+1&&Target->IsEnemyDead());
        Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("rifle")));Check(TEXT("damage07_switch_clears_old_voice"),F->ActiveCount()==0);Target->Destroy();Target=nullptr;Next(405);
    }
    else if(Stage==405&&Age>.2f){Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("cfm_zhongxiaokui")));Next(406);}
    else if(Stage==406&&Age>1.f)
    {
        if(!Z){Check(TEXT("damage07_zxk_equipped"),false);Finish();return;}Z->Cancel();
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;FVector Eye;FRotator R;Player->GetActorEyesViewPoint(Eye,R);Target=GetWorld()->SpawnActor<AHeliosEnemyE01>(Eye+R.Vector()*200,FRotator::ZeroRotator,P);Target->CurrentHP=100000;Target->SetActorTickEnabled(false);Target->GetCharacterMovement()->DisableMovement();if(Target->GetController())Target->GetController()->Destroy();
        Next(407);Player->StartFireFromAutomation();Player->StopFireFromAutomation();
    }
    else if(Stage==407&&Age>2.f){Check(TEXT("damage07_zxk_light_nonlethal"),F->Played-Damage07Before==1);Next(408);Player->StartSecondaryAttack();Player->StopSecondaryAttack();}
    else if(Stage==408&&Age>2.f){Check(TEXT("damage07_zxk_heavy"),F->Played-Damage07Before==1);Next(409);Player->StartSpecialAttack();}
    else if(Stage==409&&Age>2.f){Check(TEXT("damage07_zxk_skill"),F->Played-Damage07Before==1);Z->Cancel();Next(410);Z->ToggleForm();}
    else if(Stage==410&&Age>2.f){Check(TEXT("damage07_zxk_B_form_ready"),Z->Form==1);Next(411);Player->StartSecondaryAttack();}
    else if(Stage==411&&Age>3.f){Player->StopSecondaryAttack();Check(TEXT("damage07_zxk_each_wave_hits_once"),F->Played-Damage07Before>=6&&F->ActiveCount()<=1);Z->Cancel();Next(412);Z->ToggleRangedForm();}
    else if(Stage==412&&Age>2.f){Check(TEXT("damage07_zxk_C_form_ready"),Z->Form==2);Next(413);Player->StartFireFromAutomation();}
    else if(Stage==413&&Age>3.f)
    {
        Player->StopFireFromAutomation();Z->Cancel();Check(TEXT("damage07_zxk_darts_play_on_damage"),F->Played-Damage07Before>=2);
        const int32 N=F->Played;Target->SetCanBeDamaged(false);Z->ThrowProjectile(false);Next(414);Damage07Before=N;
    }
    else if(Stage==414&&Age>.5f)
    {
        Check(TEXT("damage07_zxk_invulnerable_projectile_no_sound"),F->Played==Damage07Before);Target->SetCanBeDamaged(true);Z->Cancel();
        const int32 N=F->Played;Pet->CancelAttack();Pet->Timeline.Begin(HeliosSSX::EAttack::Light);Pet->Contact(0);Check(TEXT("damage07_partner_owner_player_still_no_sound"),F->Played==N);
        FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;auto* Other=GetWorld()->SpawnActor<AHeliosZhongXiaoKuiPet>(Player->GetActorLocation()+FVector(900,900,0),FRotator::ZeroRotator,Spawn);Check(TEXT("damage07_zxk_partner_has_no_binding"),Other&&Other->FindComponentByClass<UHeliosHunterDamageFeedback>()==nullptr);if(Other)Other->Destroy();
        UAudioMixerBlueprintLibrary::StopRecordingOutput(this,EAudioRecordingExportType::WavFile,TEXT("player_damage07_runtime"),FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/cfm_sunshangxiang/fixes/view_clarity_flames_20261004/damage_audio")));
        Next(415);
    }
    else if(Stage==415&&Age>1.f){Finish();}
}

void AHeliosSSXDemo::TickZXKMovingWaveQA(AHeliosZhongXiaoKuiWeapon* W)
{
    if(!W){Check(TEXT("moving_wave_weapon_exists"),false);Finish();return;}
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosZXKHandWaveQA")))
    {
        if(Stage==330){if(Age>2.f){Check(TEXT("hand_wave_B_ready"),W->Form==1&&W->Age<0);Stage=350;Age=0;Before=0;FeedbackFrame=0;FeedbackLastSample=-1;FollowStart=Player->GetActorLocation();
                if(FParse::Param(FCommandLine::Get(),TEXT("HeliosZXKTemporalQA"))){
                    if(auto* PW=GetWorld()->GetSubsystem<UHeliosPetWorld>();PW&&PW->Pet)PW->Pet->SetActorHiddenInGame(true);
                    for(TActorIterator<AActor> A(GetWorld());A;++A)if(*A!=W&&*A!=Player){TInlineComponentArray<UPrimitiveComponent*> Components;A->GetComponents(Components);for(auto* C:Components)C->SetVisibility(false);}

                    EffectBackdrop=GetWorld()->SpawnActor<AStaticMeshActor>();auto* M=EffectBackdrop->GetStaticMeshComponent();M->SetMobility(EComponentMobility::Movable);M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));M->SetCollisionEnabled(ECollisionEnabled::NoCollision);M->SetCastShadow(false);
                    EffectBackdropMaterial=UMaterialInstanceDynamic::Create(LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")),this);EffectBackdropMaterial->SetVectorParameterValue(TEXT("Color"),FLinearColor(.025f,.025f,.025f,1.f));M->SetMaterial(0,EffectBackdropMaterial);EffectBackdrop->SetActorScale3D(FVector(.01,2000,2000));GEngine->Exec(GetWorld(),TEXT("r.EyeAdaptationQuality 0"));
                }}return;}
        const int32 Mode=Stage-350;
        auto WaveMaterials=[&](float Expected){
            auto* Mesh=W->GetFirstPersonMesh();bool Valid=Mesh&&Mesh->GetNumMaterials()>0;
            const auto& Materials=Expected>0?W->WaveViewMaterials:W->NormalViewMaterials;
            for(int32 I=0;I<Mesh->GetNumMaterials();++I){auto* M=Mesh->GetMaterial(I);Valid&=M&&Materials.IsValidIndex(I)&&M==Materials[I]&&(Expected>0?(M->GetBlendMode()==BLEND_Translucent&&M->GetMaterial()->TranslucencyPass==MTP_AfterMotionBlur):M->GetBlendMode()==BLEND_Opaque);}
            return Valid;
        };
        if(Before==1.f&&Age>=1.f){Before=1.5f;Check(*FString::Printf(TEXT("hand_wave_local_history_response_%d"),Mode),W->GetFirstPersonMesh()->bPerBoneMotionBlur&&W->bWaveLateView&&WaveMaterials(1.f)&&W->WaveSelfDepth->bRenderCustomDepth&&!W->WaveSelfDepth->bRenderInMainPass&&!W->WaveSelfDepth->bRenderInDepthPass);}
        const FVector Directions[]={FVector::ZeroVector,FVector(1,0,0),FVector(1,1,0).GetSafeNormal(),FVector(0,1,0),FVector(-1,0,0)};
        Player->AddMovementInput(Directions[Mode],1.f);
        if(EffectBackdrop&&FParse::Param(FCommandLine::Get(),TEXT("HeliosZXKTemporalQA"))){const FTransform Camera=Player->FirstPersonCamera->GetComponentTransform();EffectBackdrop->SetActorLocationAndRotation(Camera.GetLocation()+Camera.GetRotation().GetForwardVector()*20000.f,Camera.Rotator());}

        if(Age>=.8f&&Before==0){W->StartSecondaryFire();Before=1;}
        if(Age>=2.3f&&Before>=1.f&&Before<2.f){W->StopSecondaryFire();Before=2;}
        const auto* Anim=Cast<UHeliosHandAnimInstance>(W->GetFirstPersonMesh()->GetAnimInstance());
        const FTransform Hand=W->GetFirstPersonMesh()->GetSocketTransform(TEXT("b_RightHand"),RTS_Component);
        const FTransform Sword=W->GetFirstPersonMesh()->GetSocketTransform(TEXT("Jian"),RTS_Component);
        if(FParse::Param(FCommandLine::Get(),TEXT("HeliosZXKTemporalQA"))) {
            FVector CachedEye;FRotator CachedView;if(auto* PC=Cast<APlayerController>(Player->GetController()))PC->GetPlayerViewPoint(CachedEye,CachedView);
            const FVector CurrentEye=Player->FirstPersonCamera->GetComponentLocation();auto* Mesh=W->GetFirstPersonMesh();
            for(AHeliosZXKProjectile* Projectile:W->Projectiles)if(IsValid(Projectile)&&Projectile->Age<.2f){
                const FVector Relative=Player->FirstPersonCamera->GetComponentTransform().InverseTransformPosition(Projectile->GetActorLocation());
                UE_LOG(LogTemp,Display,TEXT("TEMPORAL_PROJECTILE mode=%d t=%.6f clock=%.6f id=%u projectile_age=%.6f velocity=%s camera_relative=%s fx=%s"),Mode,Age,W->Age,Projectile->GetUniqueID(),Projectile->Age,*Projectile->Velocity.ToString(),*Relative.ToString(),*GetNameSafe(Projectile->FlightEffect));
            }

            UE_LOG(LogTemp,Display,TEXT("TEMPORAL_WAVE mode=%d age=%.6f cached_eye=%s current_eye=%s eye_error=%.6f bone_current=%u bone_previous=%u bone_frame=%u perbone=%d mesh=%s"),Mode,Age,*CachedEye.ToString(),*CurrentEye.ToString(),FVector::Dist(CachedEye,CurrentEye),Mesh->GetBoneTransformRevisionNumber(),Mesh->GetPreviousBoneTransformRevisionNumber(),Mesh->GetCurrentBoneTransformFrame(),int32(Mesh->bPerBoneMotionBlur),*Mesh->GetComponentTransform().ToString());
        }
        int32 LiveEffects=0;for(AHeliosSSXEffect* E:W->Effects)if(IsValid(E))++LiveEffects;
        UE_LOG(LogTemp,Display,TEXT("HAND_WAVE mode=%d age=%.6f speed=%.3f clock=%.6f wave=%d sequence=%s sample=%.6f preserve=%d hand=%s sword=%s effects=%d"),Mode,Age,Player->GetVelocity().Size2D(),W->Age,W->WaveShots,*GetNameSafe(Anim?Anim->Sequence:nullptr),Anim?Anim->SampleSeconds:-1.f,Anim?int32(Anim->bPreserveTransitionVelocity):-1,*Hand.ToString(),*Sword.ToString(),LiveEffects);
        UE_LOG(LogTemp,Display,TEXT("MOTION_FRAME stage=%d age=%.6f dt=%.6f wall=%.9f action=%s"),Stage,Age,GetWorld()->GetDeltaSeconds(),FPlatformTime::Seconds(),*W->Action.ToString());
        if(!FParse::Param(FCommandLine::Get(),TEXT("HeliosMotionNoCapture"))&&Age-FeedbackLastSample+KINDA_SMALL_NUMBER>=1.f/60.f){FeedbackLastSample=Age;Capture(*FString::Printf(TEXT("feedback_fp_%d_%03d"),Stage,FeedbackFrame++));}
        if(Age>3.2f){Check(*FString::Printf(TEXT("hand_wave_real_movement_%d"),Mode),Mode==0?Player->GetVelocity().Size2D()<1.f:Player->GetVelocity().Size2D()>100.f);Check(*FString::Printf(TEXT("hand_wave_release_idle_%d"),Mode),W->Age<0&&!W->bWaveHeld);Check(*FString::Printf(TEXT("hand_wave_release_native_history_%d"),Mode),W->GetFirstPersonMesh()->bPerBoneMotionBlur&&WaveMaterials(0.f));W->Cancel();Player->GetCharacterMovement()->StopMovementImmediately();Player->SetActorLocation(FollowStart);if(Mode==(FParse::Param(FCommandLine::Get(),TEXT("HeliosZXKTemporalQA"))?1:4)){Finish();return;}++Stage;Age=0;Before=0;FeedbackFrame=0;FeedbackLastSample=-1;}
        return;
    }
    const bool Forward=Stage==331||Stage==332||Stage==333||(Stage==334&&(Age<1.f||Age>=2.f));
    if(Forward){Player->AddMovementInput(FVector::ForwardVector,1);if(Stage==333)Player->AddMovementInput(FVector::RightVector,.7f);}
    if(Stage==333&&W->Age<0){W->StartSecondaryFire();W->StopSecondaryFire();}
    if(Stage==334){if(Age>=1.f&&Before==0){W->StopSecondaryFire();Before=1;}if(Age>=2.f&&Before==1){W->StartSecondaryFire();Before=2;}}
    const auto* Anim=Cast<UHeliosHandAnimInstance>(W->GetFirstPersonMesh()->GetAnimInstance());
    const FVector H=W->GetFirstPersonMesh()->GetSocketTransform(TEXT("b_RightHand"),RTS_Component).GetLocation();
    if(Stage==332&&W->Age<=KINDA_SMALL_NUMBER&&Anim&&Anim->Sequence&&MovingPreviousSequence.EndsWith(TEXT("B_Move"))&&Anim->Sequence->GetName().EndsWith(TEXT("B_Skill02"))&&Anim->TransitionSerial!=MovingPreviousSerial)
        Check(*FString::Printf(TEXT("moving_wave_continuous_entry_%d"),W->WaveShots),FVector::Dist(H,MovingPreviousHand)<=2.f);
    MovingPreviousHand=H;MovingPreviousSequence=GetNameSafe(Anim?Anim->Sequence:nullptr);MovingPreviousSerial=Anim?Anim->TransitionSerial:0;
    UE_LOG(LogTemp,Display,TEXT("MOVING_WAVE stage=%d age=%.6f speed=%.3f clock=%.6f wave=%d sequence=%s sample=%.6f serial=%u hand=%s"),Stage,Age,Player->GetVelocity().Size2D(),W->Age,W->WaveShots,*GetNameSafe(Anim?Anim->Sequence:nullptr),Anim?Anim->SampleSeconds:-1.f,Anim?Anim->TransitionSerial:0,*H.ToString());
    UE_LOG(LogTemp,Display,TEXT("MOTION_FRAME stage=%d age=%.6f dt=%.6f wall=%.9f action=%s"),Stage,Age,GetWorld()->GetDeltaSeconds(),FPlatformTime::Seconds(),*W->Action.ToString());
    if(!FParse::Param(FCommandLine::Get(),TEXT("HeliosMotionNoCapture"))&&Age-FeedbackLastSample>=1.f/30.f){FeedbackLastSample=Age;Capture(*FString::Printf(TEXT("feedback_fp_%d_%03d"),Stage,FeedbackFrame++));}
    auto Next=[&](int32 S){Stage=S;Age=0;FeedbackLastSample=-1;FeedbackFrame=0;Before=0;};
    if(Stage==330&&Age>2.f){Check(TEXT("moving_wave_B_ready"),W->Form==1&&W->Age<0);FollowStart=Player->GetActorLocation();Next(331);}
    else if(Stage==331&&Age>1.f){Check(TEXT("moving_wave_real_forward_velocity"),Player->GetVelocity().Size2D()>100&&FVector::Dist2D(FollowStart,Player->GetActorLocation())>100);Next(332);W->StartSecondaryFire();Check(TEXT("moving_wave_manual_zero_clock_entry"),FVector::Dist(H,W->GetFirstPersonMesh()->GetSocketTransform(TEXT("b_RightHand"),RTS_Component).GetLocation())<=2.f);}
    else if(Stage==332&&Age>3.f){Check(TEXT("moving_wave_held_forward_multiple_waves"),W->WaveShots>=6);W->StopSecondaryFire();Next(333);}
    else if(Stage==333&&Age>3.f){Check(TEXT("moving_wave_diagonal_real_velocity"),Player->GetVelocity().Size2D()>100);Next(334);W->StartSecondaryFire();}
    else if(Stage==334&&Age>3.f){W->StopSecondaryFire();Next(335);}
    else if(Stage==335&&Age>1.f){Check(TEXT("moving_wave_release_returns_B_idle"),W->Age<0&&!W->bWaveHeld);Next(336);W->ToggleRangedForm();}
    else if(Stage==336&&Age>2.f){Check(TEXT("moving_wave_C_transition_completes"),W->Form==2&&W->Age<0);Next(337);W->StartFiring();}
    else if(Stage==337&&Age>2.f){W->StopFiring();W->Cancel();Next(338);W->ToggleRangedForm();}
    else if(Stage==338&&Age>2.f){Check(TEXT("moving_wave_return_B_transition_completes"),W->Form==1&&W->Age<0);Next(339);W->StartSecondaryFire();W->StopSecondaryFire();}
    else if(Stage==339&&Age>1.f){Check(TEXT("moving_wave_single_tap_returns_idle"),W->Age<0&&!W->bWaveHeld);W->Cancel();Finish();}
}
