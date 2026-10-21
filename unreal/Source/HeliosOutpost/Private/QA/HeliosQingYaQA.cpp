#include "QA/HeliosQingYaQA.h"
#include "Player/HeliosArenaCharacter.h"
#include "Player/HeliosCharacterDefinition.h"
#include "Player/HeliosCharacterActionProfile.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Player/HeliosQingYaAbilityComponent.h"
#include "Player/HeliosQingYaSelection.h"
#include "Enemies/HeliosEnemyTestDirector.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "UI/HeliosAdventureHUD.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Weapons/HeliosFirstPersonPresentationComponent.h"
#include "Weapons/HeliosHandAnimInstance.h"
#include "Weapons/HeliosSSXVoice.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Pets/HeliosHumanoidPet.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Combat/HeliosKillFeedbackSubsystem.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/DamageEvents.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "ContentStreaming.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif
AHeliosQingYaQA::AHeliosQingYaQA(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;}
void AHeliosQingYaQA::BeginPlay()
{Super::BeginPlay();Out=FPaths::ProjectSavedDir()/TEXT("QingYaQA");FParse::Value(FCommandLine::Get(),TEXT("QingYaQAOut="),Out);IFileManager::Get().MakeDirectory(*Out,true);Started=PhaseStarted=FPlatformTime::Seconds();}
void AHeliosQingYaQA::Advance(int32 Next){Phase=Next;PhaseStarted=FPlatformTime::Seconds();}
void AHeliosQingYaQA::Check(const FString& Name,bool Passed)
{auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("case"),Case);Row->SetStringField(TEXT("name"),Name);Row->SetBoolField(TEXT("passed"),Passed);Checks.Add(MakeShared<FJsonValueObject>(Row));Failures+=Passed?0:1;UE_LOG(LogTemp,Display,TEXT("QYA_QA_CHECK %s %s %d"),*Case,*Name,Passed);}
bool AHeliosQingYaQA::Capture(const FString& Name)
{
#if WITH_EDITOR
    FAssetCompilingManager::Get().FinishAllCompilation();if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
#endif
    IStreamingManager::Get().StreamAllResources(5.f);IStreamingManager::Get().BlockTillAllRequestsFinished(5.f,true);
    FScreenshotRequest::RequestScreenshot(Out/(Case+TEXT("_")+Name+TEXT(".png")),true,false);return true;
}
void AHeliosQingYaQA::Tick(float Dt)
{
    Super::Tick(Dt);const double Age=FPlatformTime::Seconds()-PhaseStarted;
    if(FPlatformTime::Seconds()-Started>600){Check(TEXT("timeout"),false);Finish();return;}
    if(FParse::Param(FCommandLine::Get(),TEXT("QingYaTestbedQA"))){TickTestbed();return;}
    if(!Player){PC=GetWorld()->GetFirstPlayerController();Player=PC?Cast<AHeliosArenaCharacter>(PC->GetPawn()):nullptr;HUD=PC?Cast<AHeliosAdventureHUD>(PC->GetHUD()):nullptr;for(TActorIterator<AHeliosAdventureDirector> I(GetWorld());I;++I){Director=*I;break;}if(!Player||!HUD||!Director)return;}
#if WITH_EDITOR
    if(FAssetCompilingManager::Get().GetNumRemainingAssets()>0||(GShaderCompilingManager&&GShaderCompilingManager->GetNumRemainingJobs()>0))return;
#endif
    auto* Qing=Player->GetQingYaAbility();auto* Art=Player->GetCharacterPresentation();
    const FName Variant=CaseIndex/14==0?FName("GR"):CaseIndex/14==1?FName("BL"):FName("SP");
    static const FName Poses[]={TEXT("Idle"),TEXT("Move"),TEXT("Fire"),TEXT("ChangeClip"),TEXT("Equip")};
    switch(Phase)
    {
    case 0:
        if(Age<2)return;
        {const auto* D=FHeliosCharacterRegistry::FindCharacter(TEXT("cfm_qingya"));Check(TEXT("registered_three_forms_default_GR"),D&&D->Forms.Num()==3&&D->DefaultVariantId==TEXT("GR"));}
        HUD->SelectCharacter(TEXT("cfm_qingya"));HUD->SelectWeapon(TEXT("cfm_chan_zeus"));HUD->SelectPetCount(0);Capture(TEXT("menu_GR"));Advance(1);return;
    case 1:
        if(Age<.4)return;HUD->SelectCharacterForm(TEXT("BL"));Capture(TEXT("menu_BL"));Advance(2);return;
    case 2:
        if(Age<.4)return;HUD->SelectCharacterForm(TEXT("SP"));Capture(TEXT("menu_SP"));Advance(3);return;
    case 3:
        if(Age<.4)return;HUD->SelectCharacterForm(TEXT("GR"));HUD->StartSelectedRun();Director->SetActorTickEnabled(false);Director->CleanupOwnedActors();
        for(TActorIterator<AHeliosPetCompanion> I(GetWorld());I;++I)I->Destroy();
        Player->SetActorLocation(FVector(0,-2800,102),false,nullptr,ETeleportType::TeleportPhysics);Player->SetCanBeDamaged(false);Player->GetCharacterMovement()->DisableMovement();PC->SetControlRotation(FRotator(-4,90,0));
        for(auto* W:UHeliosWeaponRegistry::GetAvailableWeapons())if(W&&W->WeaponId!=TEXT("mech")&&W->WeaponId!=TEXT("fighter"))Weapons.Add(W->WeaponId);
        Check(TEXT("all_current_humanoid_weapons"),Weapons.Num()==14);Draws=Qing->GetTalentDraws();Advance(4);return;
    case 4:
    {
        if(Age<.1)return;if(CaseIndex==0){Draws=Qing->GetTalentDraws();Check(TEXT("run_talent_drawn"),Draws>0);}auto L=Player->GetLoadout();L.CharacterId=TEXT("cfm_qingya");L.CharacterVariantId=Variant;L.PrimaryWeaponId=Weapons[CaseIndex%14];Case=Variant.ToString()+TEXT("_")+L.PrimaryWeaponId.ToString();
        Check(TEXT("player_pair_ready"),Player->EquipLoadout(L)&&Art->IsReady());Check(TEXT("no_talent_reroll"),Qing->GetTalentDraws()==Draws);
        if(Pet)Pet->Destroy();const FTransform Transform(FRotator(0,-90,0),FVector(90,-2460,102));
        Pet=GetWorld()->SpawnActorDeferred<AHeliosHumanoidPet>(AHeliosHumanoidPet::StaticClass(),Transform,Player,Player,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Check(TEXT("companion_pair_ready"),Pet->ApplyLoadout(L));Pet->CompanionOwner=Player;UGameplayStatics::FinishSpawningActor(Pet,Transform);Pet->SetActorTickEnabled(false);Pet->GetCharacterMovement()->DisableMovement();
        // Sample through the live presentation component, independently of AI.
        Player->GetEquippedWeapon()->SetActorTickEnabled(false);Player->GetPresentation()->SetComponentTickEnabled(false);Pet->GetCharacterPresentation()->SetComponentTickEnabled(false);PoseIndex=0;Advance(5);return;
    }
    case 5:
    {
        if(Age<.25)return;const FName Pose=Poses[PoseIndex];
        if(Pose==TEXT("ChangeClip")&&!Player->GetEquippedWeapon()->UsesAmmunition()){if(++PoseIndex<UE_ARRAY_COUNT(Poses)){Advance(5);return;}}
        const auto* Def=FHeliosCharacterRegistry::FindCharacter(TEXT("cfm_qingya"));auto* Profile=Def->FindForm(Variant)->ActionProfile.LoadSynchronous();const auto* Pair=Profile->FindWeapon(Player->GetWeaponId());FName SourceKey=NAME_None;UAnimSequence* Clip=nullptr;
        for(const auto& Item:Pair->FirstPersonClips)if(Item.Key.ToString().EndsWith(Pose.ToString())){SourceKey=Item.Key;Clip=Item.Value.LoadSynchronous();break;}
        if(!Clip&&Pose==TEXT("Fire"))for(const auto& Item:Pair->FirstPersonClips)if(Item.Key.ToString().Contains(TEXT("Fire"))){SourceKey=Item.Key;Clip=Item.Value.LoadSynchronous();break;}
        if(!Clip)
        {
            const FName Id=Player->GetWeaponId();const TCHAR* Tail=Pose==TEXT("Move")?TEXT("Idle"):Pose==TEXT("ChangeClip")?TEXT("Reload"):Pose==TEXT("Equip")&&Id==TEXT("mysterious_hand")?TEXT("Idle"):
                Pose==TEXT("Fire")?(Id==TEXT("calamity")?TEXT("Cast"):Id==TEXT("qingxiao")?TEXT("SlashL"):Id==TEXT("mysterious_hand")?TEXT("Point"):TEXT("Swing1")):TEXT("__missing");
            for(const auto& Item:Pair->FirstPersonClips)if(Item.Key.ToString().EndsWith(Tail)){SourceKey=Item.Key;Clip=Item.Value.LoadSynchronous();break;}
        }
        Check(TEXT("FP_")+Pose.ToString(),Clip&&Art->PresentAction(SourceKey,.12f,false));
        auto* Driver=Player->GetPresentation()->IsReady()?Player->GetPresentation()->GetArmsMesh():Player->GetEquippedWeapon()->GetFirstPersonMesh();
        if(Clip&&Driver){Driver->HandleExistingParallelEvaluationTask(true,true);if(auto* H=Cast<UHeliosHandAnimInstance>(Driver->GetAnimInstance())){H->bPreserveTransitionVelocity=false;H->SetTimelinePose(Clip,.12f,false,true);}else{Driver->SetAnimationMode(EAnimationMode::AnimationSingleNode);Driver->SetAnimation(Clip);Driver->SetPosition(.12f,false);Driver->Stop();}Driver->TickAnimation(0,false);Driver->RefreshBoneTransforms();Art->TickComponent(0,LEVELTICK_All,nullptr);}
        auto* Body=Pet->GetCharacterPresentation();const FName ThirdKey=Pose==TEXT("Move")?Pose:Pair->ThirdPersonClips.Contains(SourceKey)?SourceKey:Pose;Check(TEXT("TP_")+Pose.ToString(),Body->PresentAction(ThirdKey,.12f,false));Body->TickComponent(0,LEVELTICK_All,nullptr);Body->GetArmsMesh()->ClearMotionVector();
        Capture(Pose.ToString());if(++PoseIndex<UE_ARRAY_COUNT(Poses)){Advance(5);return;}
        Advance(53);return;
    }
    case 53:
        if(Age<.15)return;Player->GetEquippedWeapon()->SetActorTickEnabled(true);Player->GetPresentation()->SetComponentTickEnabled(true);Pet->GetCharacterPresentation()->SetComponentTickEnabled(true);
        if(CaseIndex%14==0){Advance(6);return;}Advance(12);return;
    case 6:
        if(Age<.6)return;Ammo=Player->GetEquippedWeapon()->GetBulletCount();Check(TEXT("Love_begin"),Art->BeginCharacterGesture(TEXT("Love")));Check(TEXT("companion_Love"),Pet->GetCharacterPresentation()->BeginCharacterGesture(TEXT("Love")));Advance(7);return;
    case 7:
        if(Age<.8)return;Capture(TEXT("Love"));Check(TEXT("Love_original_audio_once"),Art->GetCharacterGestureAudioEventCount()==1);if(auto* M=Art->GetGestureArmsMesh())UE_LOG(LogTemp,Display,TEXT("QYA_GESTURE_BOUNDS transform=%s hand=%s bounds=%s"),*M->GetRelativeTransform().ToString(),*M->GetSocketLocation(TEXT("b_RightHand")).ToString(),*M->Bounds.GetBox().ToString());Advance(70);return;
    case 70:
        if(Age<.2)return;Player->CancelAutomationCombat();Check(TEXT("Love_cancel_restores_ammo"),!Art->IsCharacterGesturePlaying()&&Player->GetEquippedWeapon()->GetBulletCount()==Ammo);Check(TEXT("GunFire_begin"),Art->BeginCharacterGesture(TEXT("GunFire")));Advance(8);return;
    case 8:
        if(Age<.8)return;Capture(TEXT("GunFire"));Advance(80);return;
    case 80:
        if(Age<.2)return;Director->SetRunPaused(true);Check(TEXT("pause_cancels_gesture"),!Art->IsCharacterGesturePlaying());Director->SetRunPaused(false);
        {FActorSpawnParameters S;S.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;Target=GetWorld()->SpawnActor<AHeliosEnemyE01>(Player->FirstPersonCamera->GetComponentLocation()+Player->FirstPersonCamera->GetForwardVector()*170-FVector(0,0,65),FRotator::ZeroRotator,S);Target->StopShooting();Target->CurrentHP=10000;Target->SetActorTickEnabled(false);Target->GetCharacterMovement()->DisableMovement();if(Target->GetController())Target->GetController()->Destroy();}
        Target->SetActorHiddenInGame(true);Advance(81);return;
    case 81:
        if(Age<.3)return;Check(TEXT("kick_begin"),Qing->StartKick());Advance(9);return;
    case 9:
        if(Age<.07)return;
        // Screenshot streaming can outlast the 0.2667s native kick. Hold an
        // authored sample until the renderer has consumed the request.
        Art->SetComponentTickEnabled(false);
        if(auto* M=Art->GetGestureArmsMesh())
        {M->SetPosition(.12f,false);M->TickAnimation(0,false);M->RefreshBoneTransforms();}
        Capture(TEXT("Kick"));Advance(10);return;
    case 10:
        if(Age<.2)return;Art->SetComponentTickEnabled(true);Advance(101);return;
    case 101:
        if(Age<.5)return;Check(TEXT("kick_one_60_damage"),Target&&FMath::IsNearlyEqual(10000-Target->CurrentHP,60.f));Check(TEXT("kick_finished_restores_weapon"),!Qing->IsAttacking()&&!Art->IsCharacterGesturePlaying()&&Player->GetEquippedWeapon()->GetBulletCount()==Ammo);Target->Destroy();Target=nullptr;
        {const auto Before=Player->GetEquippedWeapon();auto L=Player->GetLoadout();L.CharacterVariantId=Variant==TEXT("SP")?FName("GR"):FName("SP");Check(TEXT("H_retains_weapon_ammo"),Player->EquipLoadout(L)&&Player->GetEquippedWeapon()==Before&&Before->GetBulletCount()==Ammo);Check(TEXT("H_retains_talent"),Qing->GetTalentDraws()==Draws);L.CharacterVariantId=Variant;Player->EquipLoadout(L);}
        Advance(12);return;
    case 12:
        if(Age<.2)return;++CompletedPairs;CaseIndex+=FParse::Param(FCommandLine::Get(),TEXT("QingYaGestureQA"))?14:1;if(CaseIndex<42){Advance(4);return;}Advance(13);return;
    case 13:
        {Case=TEXT("regression");Check(TEXT("spirit_speed_multiplier"),FMath::IsNearlyEqual(Qing->MovementMultiplier(),1.2f));
        // Exercise each talent independently of this run's random selection.
        const auto SavedTalent=Qing->Talent;
        Qing->Talent=EHeliosQingYaTalent::RifleSwitch;
        Check(TEXT("rifle_talent_category"),FMath::IsNearlyEqual(Qing->SwitchDurationMultiplier(TEXT("cfm_chan_zeus")),.7f)&&Qing->SwitchDurationMultiplier(TEXT("cfm_heilonghun"))==1.f&&Qing->SwitchDurationMultiplier(TEXT("chopsticks"))==1.f);
        const auto SavedLoadout=Player->GetLoadout();auto SwitchLoadout=SavedLoadout;SwitchLoadout.PrimaryWeaponId=TEXT("cfm_chan_zeus");
        Check(TEXT("rifle_ready_clock"),Player->EquipLoadout(SwitchLoadout)&&FMath::IsNearlyEqual(Qing->SwitchRemaining(),.35f));
        Qing->Talent=EHeliosQingYaTalent::ShotgunSwitch;
        Check(TEXT("shotgun_talent_category"),FMath::IsNearlyEqual(Qing->SwitchDurationMultiplier(TEXT("cfm_heilonghun")),.7f)&&Qing->SwitchDurationMultiplier(TEXT("cfm_chan_zeus"))==1.f);
        SwitchLoadout.PrimaryWeaponId=TEXT("cfm_heilonghun");Check(TEXT("shotgun_ready_clock"),Player->EquipLoadout(SwitchLoadout)&&FMath::IsNearlyEqual(Qing->SwitchRemaining(),.35f));
        Qing->Talent=EHeliosQingYaTalent::KillChain;
        Check(TEXT("kill_chain_talent_window"),Qing->KillWindowSeconds()==8.f&&Qing->SwitchDurationMultiplier(TEXT("cfm_chan_zeus"))==1.f);
        Qing->Talent=SavedTalent;Player->EquipLoadout(SavedLoadout);Check(TEXT("talent_fixture_does_not_redraw"),Qing->GetTalentDraws()==Draws);
        auto* V=Art->Voice.Get();
        Check(TEXT("native_voice_event_pools"),V&&V->OriginalEventIndices.Num()==9&&V->OriginalEventIndices.FindChecked(TEXT("Hurt")).Num()==3&&V->OriginalEventIndices.FindChecked(TEXT("Death")).Num()==3);
        if(V)
        {
            V->Stop();Check(TEXT("original_double_kill_voice"),V->PlayOriginalEvent(TEXT("MultiKill2"),TEXT("Kill"))&&V->Channel&&V->Channel->Sound&&V->Channel->Sound->GetName().Contains(TEXT("13997")));
            V->Stop();Check(TEXT("original_eight_kill_voice"),V->PlayOriginalEvent(TEXT("MultiKill8"),TEXT("Kill"))&&V->Channel&&V->Channel->Sound&&V->Channel->Sound->GetName().Contains(TEXT("14003")));
            V->Stop();Check(TEXT("no_fabricated_single_kill_voice"),!V->PlayOriginalEvent(TEXT("Kill"),TEXT("Kill")));
            const int32 BeforeKills=V->KillCount;
            for(int32 I=0;I<2;++I)
            {
                FActorSpawnParameters S;S.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                auto* Victim=GetWorld()->SpawnActor<AHeliosEnemyE01>(Player->GetActorLocation()+FVector(200,200+I*100,0),FRotator::ZeroRotator,S);
                Victim->CurrentHP=1;Victim->SetActorTickEnabled(false);Victim->StopShooting();
                UGameplayStatics::ApplyDamage(Victim,10,PC,Player->GetEquippedWeapon(),nullptr);
                Check(I==0?TEXT("single_kill_is_silent"):TEXT("actual_double_kill_routes_native_voice"),I==0?V->KillCount==BeforeKills:V->KillCount==BeforeKills+1&&V->Channel&&V->Channel->Sound&&V->Channel->Sound->GetName().Contains(TEXT("13997")));
                Victim->Destroy();
            }
            Art->PlayCharacterCue(TEXT("Kill"));Check(TEXT("duplicate_weapon_kill_cue_ignored"),V->KillCount==BeforeKills+1&&V->GetPendingKillCount()==0);V->Stop();
        }
        for(FName Mode:{FName(TEXT("mech")),FName(TEXT("fighter"))})for(FName Form:{FName(TEXT("GR")),FName(TEXT("BL")),FName(TEXT("SP"))})
        {auto Vehicle=Player->GetLoadout();Vehicle.PrimaryWeaponId=Mode;Vehicle.CharacterVariantId=Form;FString Error;Check(TEXT("vehicle_loadout_")+Mode.ToString()+TEXT("_")+Form.ToString(),FHeliosLoadout::Validate(Vehicle,Error));}
        if(Director->IsUrbanChallenge())
        {
            const int32 Plays=Qing->GetVictoryPlays();Director->UrbanWaveIndex=Director->UrbanSchedule.Waves.Num()-1;Director->UrbanSkippedWaves=1;Director->AdvanceUrbanWave(false);Check(TEXT("skipped_completion_has_no_MVP"),Qing->GetVictoryPlays()==Plays);
            Director->bRunOver=false;Director->bUrbanVictory=false;Director->UrbanSkippedWaves=0;Director->AdvanceUrbanWave(false);Check(TEXT("challenge_victory_MVP"),Qing->GetVictoryPlays()==Plays+1);Qing->PlayVictoryMusic();Check(TEXT("MVP_only_once"),Qing->GetVictoryPlays()==Plays+1);
            Director->bRunOver=false;Director->bUrbanVictory=false;Director->SetRunPaused(false);
        }
        auto L=Player->GetLoadout();L.CharacterId=TEXT("cfm_chan");L.CharacterVariantId=NAME_None;L.PrimaryWeaponId=TEXT("cfm_chan_zeus");Check(TEXT("existing_character_ready"),Player->EquipLoadout(L)&&Art->IsReady());Check(TEXT("Qingya_effects_cleared"),Qing->MovementMultiplier()==1.f&&Qing->KillWindowSeconds()==6.f&&Qing->SwitchRemaining()==0&&!Qing->IsAttacking());Capture(TEXT("chan"));
        FHeliosKillChain Chain;Chain.Record(0,false,8);Chain.Record(7.9,false,8);Check(TEXT("extended_kill_window"),Chain.Streak==2);Chain.Record(16,false,8);Check(TEXT("kill_window_expires"),Chain.Streak==1);
        auto Json=MakeShared<FJsonObject>();L.CharacterId=TEXT("cfm_qingya");L.CharacterVariantId=TEXT("BL");HeliosLoadout::WriteJson(Json,L);FHeliosLoadout Restored;Check(TEXT("loadout_save_restore"),HeliosLoadout::ReadJson(Json,Restored)&&Restored==L);
        HeliosQingYaSelection::Save(L);FHeliosLoadout Disk;Check(TEXT("selection_disk_restore"),HeliosQingYaSelection::Restore(Disk)&&Disk==L);
        HeliosQingYaSelection::Save(Player->GetLoadout());Check(TEXT("other_character_clears_Qingya_default"),!HeliosQingYaSelection::Restore(Disk));}
        Advance(14);return;
    case 14:
    {
        if(Age<.4||GetWorld()->GetTimeSeconds()<20)return;auto L=Player->GetLoadout();L.CharacterId=TEXT("cfm_qingya");L.CharacterVariantId=TEXT("GR");Check(TEXT("return_Qingya_keeps_talent"),Player->EquipLoadout(L)&&Qing->GetTalentDraws()==Draws);
        Check(TEXT("hurt_fixture_gesture"),Art->BeginCharacterGesture(TEXT("Love")));Player->SetCanBeDamaged(true);Check(TEXT("hurt_interrupts_gesture"),Player->TakeDamage(1,FDamageEvent(),nullptr,nullptr)>0&&!Art->IsCharacterGesturePlaying());
        Check(TEXT("death_fixture_gesture"),Art->BeginCharacterGesture(TEXT("Love")));Player->TakeDamage(1000000,FDamageEvent(),nullptr,nullptr);Check(TEXT("death_interrupts_gesture"),Player->IsDead()&&!Art->IsCharacterGesturePlaying()&&!Qing->IsAttacking());
        Player->ResetForTest();Director->bRunOver=false;Director->SetRunPaused(false);Draws=Qing->GetTalentDraws();Director->EnterWeaponSelection();Check(TEXT("new_run_starts"),Director->StartRunWithLoadout(L));Director->SetActorTickEnabled(false);Advance(15);return;
    }
    case 15:
        if(Age<.2)return;Check(TEXT("new_run_draws_exactly_once"),Qing->GetTalentDraws()==Draws+1);Finish();return;
    }
}
void AHeliosQingYaQA::TickTestbed()
{
    const double Age=FPlatformTime::Seconds()-PhaseStarted;
    if(!Player){PC=GetWorld()->GetFirstPlayerController();Player=PC?Cast<AHeliosArenaCharacter>(PC->GetPawn()):nullptr;}
    AHeliosEnemyTestDirector* Bed=nullptr;for(TActorIterator<AHeliosEnemyTestDirector> I(GetWorld());I;++I){Bed=*I;break;}
    if(!Player||!Bed)return;auto* Qing=Player->GetQingYaAbility();auto* Art=Player->GetCharacterPresentation();Case=TEXT("testbed");
    if(Phase==0)
    {
        if(Age<2)return;Player->SetCanBeDamaged(false);for(TActorIterator<AHeliosEnemyActor> I(GetWorld());I;++I)I->Destroy();
        auto L=Player->GetLoadout();L.CharacterId=TEXT("cfm_qingya");L.CharacterVariantId=TEXT("GR");L.PrimaryWeaponId=TEXT("cfm_chan_zeus");Check(TEXT("GR_ready"),Player->EquipLoadout(L)&&Art->IsReady());Advance(1);return;
    }
    if(Phase==1)
    {
        if(Age<.6)return;Draws=Qing->GetTalentDraws();Check(TEXT("talent_drawn"),Draws>0);Ammo=Player->GetEquippedWeapon()->GetBulletCount();auto* Original=Player->GetEquippedWeapon();
        for(FName Form:{FName(TEXT("BL")),FName(TEXT("SP"))}){auto L=Player->GetLoadout();L.CharacterVariantId=Form;Check(Form.ToString()+TEXT("_ready_retains_weapon"),Player->EquipLoadout(L)&&Art->IsReady()&&Player->GetEquippedWeapon()==Original&&Original->GetBulletCount()==Ammo);}
        Check(TEXT("forms_keep_talent"),Qing->GetTalentDraws()==Draws);Check(TEXT("SP_speed"),Qing->MovementMultiplier()==1.2f);Capture(TEXT("SP"));Check(TEXT("Love_begin"),Art->BeginCharacterGesture(TEXT("Love")));Advance(2);return;
    }
    if(Phase==2)
    {
        if(Age<.5)return;Bed->ResetTest();Check(TEXT("F5_cancels_gesture"),!Art->IsCharacterGesturePlaying());Advance(3);return;
    }
    if(Phase==3)
    {
        if(Age<.3)return;Check(TEXT("F5_one_new_talent"),Qing->GetTalentDraws()==Draws+1);Check(TEXT("F5_keeps_Qingya_SP"),Player->GetLoadout().CharacterId==TEXT("cfm_qingya")&&Player->GetLoadout().CharacterVariantId==TEXT("SP"));
        CaseIndex=42;CompletedPairs=3;Finish();
    }
}
void AHeliosQingYaQA::Finish()
{auto J=MakeShared<FJsonObject>();J->SetBoolField(TEXT("passed"),Failures==0&&CaseIndex==42);J->SetNumberField(TEXT("failures"),Failures);J->SetNumberField(TEXT("completed_pairs"),CompletedPairs);J->SetBoolField(TEXT("gestures_only"),FParse::Param(FCommandLine::Get(),TEXT("QingYaGestureQA")));J->SetArrayField(TEXT("checks"),Checks);FString Text;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Out/TEXT("report.json")));UE_LOG(LogTemp,Display,TEXT("QYA_RUNTIME_COMPLETE pairs=%d failures=%d"),CompletedPairs,Failures);SetActorTickEnabled(false);FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);}
