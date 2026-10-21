#include "QA/HeliosMultiPetQA.h"
#include "QA/HeliosMenuQA.h"
#include "Pets/HeliosPetDog.h"
#include "Pets/HeliosChanZeusPet.h"
#include "Pets/HeliosBuLianShiP90Pet.h"
#include "Pets/HeliosUltimatumPet.h"
#include "Pets/HeliosZhongXiaoKuiPet.h"
#include "Pets/HeliosSunShangXiangPet.h"
#include "Sound/SoundConcurrency.h"
#include "Weapons/HeliosSSXVoice.h"
#include "Weapons/HeliosP90SubstituteFireAudio.h"
#include "UI/HeliosAdventureHUD.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "Player/HeliosArenaCharacter.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerController.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Town/HeliosTownSubsystem.h"
#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SScrollBox.h"

namespace
{
bool Label(const TSharedRef<SWidget>& W,const FString& T)
{if(W->GetTypeAsString()==TEXT("STextBlock")&&StaticCastSharedRef<STextBlock>(W)->GetText().ToString()==T)return true;auto* C=W->GetChildren();for(int32 I=0;C&&I<C->Num();++I)if(Label(C->GetChildAt(I),T))return true;return false;}
TSharedPtr<SWidget> Find(const TSharedRef<SWidget>& W,const FString& T,const FString& Type)
{if(!W->GetVisibility().IsVisible())return nullptr;if(W->GetTypeAsString()==Type&&Label(W,T))return W;auto* C=W->GetChildren();for(int32 I=0;C&&I<C->Num();++I)if(auto R=Find(C->GetChildAt(I),T,Type))return R;return nullptr;}
TSharedPtr<SWidget> Find(const FString& T,const FString& Type)
{if(FSlateApplication::IsInitialized())for(const auto& W:FSlateApplication::Get().GetInteractiveTopLevelWindows())if(auto R=Find(W->GetContent(),T,Type))return R;return nullptr;}
bool Click(const FString& T)
{
    auto B=Find(T,TEXT("SButton"));if(!B||!B->IsEnabled())return false;auto& Slate=FSlateApplication::Get();FWidgetPath Path;if(!Slate.GeneratePathToWidgetUnchecked(B.ToSharedRef(),Path))return false;
    auto G=B->GetCachedGeometry();auto At=G.LocalToAbsolute(G.GetLocalSize()*.5f);TSet<FKey> Down;Down.Add(EKeys::LeftMouseButton);TSet<FKey> Up;
    FPointerEvent Press(FSlateApplication::CursorPointerIndex,At,At,Down,EKeys::LeftMouseButton,0,FModifierKeysState()),Release(FSlateApplication::CursorPointerIndex,At,At,Up,EKeys::LeftMouseButton,0,FModifierKeysState());
    Slate.ReleaseAllPointerCapture();B->OnMouseEnter(G,Press);auto P=B->OnMouseButtonDown(G,Press);Slate.ProcessReply(Path,P,&Path,&Press);auto R=B->OnMouseButtonUp(G,Release);Slate.ProcessReply(Path,R,&Path,&Release);return P.IsEventHandled()&&R.IsEventHandled();
}
void Scroll(const FString& T){auto B=Find(T,TEXT("SButton")),S=Find(T,TEXT("SScrollBox"));if(B&&S)StaticCastSharedPtr<SScrollBox>(S)->ScrollDescendantIntoView(B,false,EDescendantScrollDestination::Center);}
int32 TravelStep=0;
TArray<EHeliosPetKind> TravelKinds;
TArray<float> TravelHealth;
}
AHeliosMultiPetQA::AHeliosMultiPetQA(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;}
void AHeliosMultiPetQA::Check(const FString& Name,bool Pass)
{auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("name"),Name);J->SetBoolField(TEXT("passed"),Pass);Checks.Add(MakeShared<FJsonValueObject>(J));if(!Pass)++Failures;UE_LOG(LogTemp,Display,TEXT("MULTI_QA %s %s"),*Name,Pass?TEXT("PASS"):TEXT("FAIL"));}
void AHeliosMultiPetQA::Shot(const FString& Name)
{FScreenshotRequest::RequestScreenshot(Out/(Name+TEXT(".png")),true,false);}
void AHeliosMultiPetQA::Finish(bool Exit)
{
    if(bFinished)return;bFinished=Exit;
    if(bRecording){UAudioMixerBlueprintLibrary::StopRecordingOutput(this,EAudioRecordingExportType::WavFile,TEXT("runtime_audio"),Out);bRecording=false;}
    auto J=MakeShared<FJsonObject>();J->SetBoolField(TEXT("passed"),Failures==0);J->SetArrayField(TEXT("checks"),Checks);J->SetArrayField(TEXT("samples"),Samples);J->SetStringField(TEXT("map"),GetWorld()->GetMapName());
    J->SetStringField(TEXT("scope"),TEXT("Fresh native -game run; actual Slate buttons, production spawning/AI/damage/recovery. Passive target and disabled director form a bounded fixture. Game target built separately; no packaged play claim."));
    if(!Frames.IsEmpty()){Frames.Sort();J->SetNumberField(TEXT("frame_p95_ms"),Frames[FMath::Min(Frames.Num()-1,int32(Frames.Num()*.95))]*1000);J->SetNumberField(TEXT("frame_samples"),Frames.Num());}
    FString Text;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Out/TEXT("report.json")));
    if(Exit)FPlatformMisc::RequestExit(false);
}
void AHeliosMultiPetQA::AuditCapsules(const FString& Name)
{
    auto* P=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));auto* PW=GetWorld()->GetSubsystem<UHeliosPetWorld>();if(!P||!PW)return;
    bool Safe=true,Separated=true;float MinGap=MAX_flt;TArray<ACharacter*> Actors{P};for(const auto& Pet:PW->Pets)if(IsValid(Pet))Actors.Add(Pet);
    for(auto* A:Actors)
    {
        auto* C=A->GetCapsuleComponent();FCollisionQueryParams Q(SCENE_QUERY_STAT(MultiQACapsule),false,A);
        for(auto* Other:Actors)Q.AddIgnoredActor(Other);
        const bool Block=GetWorld()->OverlapBlockingTestByChannel(A->GetActorLocation(),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(C->GetScaledCapsuleRadius(),C->GetScaledCapsuleHalfHeight()-.5f),Q);
        FHitResult Floor;const FVector Feet=A->GetActorLocation()-FVector(0,0,C->GetScaledCapsuleHalfHeight());
        const bool Ground=GetWorld()->LineTraceSingleByChannel(Floor,Feet+FVector(0,0,15),Feet-FVector(0,0,80),ECC_WorldStatic,Q);
        Safe&=!Block&&Ground&&Floor.ImpactNormal.Z>.7f&&Feet.Z>=Floor.ImpactPoint.Z-2;
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("case"),Name);Row->SetStringField(TEXT("actor"),A->GetName());Row->SetStringField(TEXT("location"),A->GetActorLocation().ToString());Row->SetBoolField(TEXT("blocked"),Block);Row->SetBoolField(TEXT("ground"),Ground);Row->SetNumberField(TEXT("feet_z"),Feet.Z);Row->SetNumberField(TEXT("floor_z"),Floor.ImpactPoint.Z);Samples.Add(MakeShared<FJsonValueObject>(Row));
        for(auto* Other:Actors)if(A!=Other)
        {
            const float Gap=FVector::Dist2D(A->GetActorLocation(),Other->GetActorLocation())-C->GetScaledCapsuleRadius()-Other->GetCapsuleComponent()->GetScaledCapsuleRadius();
            MinGap=FMath::Min(MinGap,Gap);
            // Touching capsules are valid while moving; reject actual penetration,
            // with the same half-centimetre tolerance as the level overlap audit.
            Separated&=Gap>=-.5f;
        }
    }
    if(Actors.Num()>1){auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("case"),Name);Row->SetNumberField(TEXT("minimum_capsule_gap_cm"),MinGap);Samples.Add(MakeShared<FJsonValueObject>(Row));}
    Check(Name+TEXT("_level_capsules_and_floor"),Safe);Check(Name+TEXT("_peer_and_player_spacing"),Separated);
}
void AHeliosMultiPetQA::Tick(float Dt)
{
    Super::Tick(Dt);if(bFinished)return;const double Now=FPlatformTime::Seconds();
    if(!Started){Started=Now;FParse::Value(FCommandLine::Get(),TEXT("MultiQAOut="),Out);Out=Out/GetWorld()->GetMapName();IFileManager::Get().MakeDirectory(*Out,true);Next=Now+10;}
    if(Now-Started>360){Check(TEXT("bounded_wall_timeout"),false);Finish();return;}
    if(PreviousFrame&&Stage>0&&!GetWorld()->IsPaused())Frames.Add(Now-PreviousFrame);PreviousFrame=Now;
    auto* PC=UGameplayStatics::GetPlayerController(this,0);auto* P=PC?Cast<AHeliosArenaCharacter>(PC->GetPawn()):nullptr;auto* H=PC?Cast<AHeliosAdventureHUD>(PC->GetHUD()):nullptr;
    auto* D=AHeliosAdventureDirector::Find(GetWorld());auto* PW=GetWorld()->GetSubsystem<UHeliosPetWorld>();auto* S=GetGameInstance()->GetSubsystem<UHeliosPetSession>();if(!H||!P||!D||!PW||!S)return;
    if(Stage==31)P->AddMovementInput(FVector(-1,0,0),1);
    if(Stage==32)P->AddMovementInput(FVector(0,1,0),1);
    if(Stage==41)P->AddMovementInput(FVector(0,1,0),1);
    if(Stage==42)P->AddMovementInput(FVector(1,0,0),1);
    if(TravelStep>0)
    {
        if(!D->GetTownSubsystem())D->SetActorTickEnabled(false);
        for(TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It){It->SetActorTickEnabled(false);It->StopShooting();if(auto* AI=Cast<AAIController>(It->GetController())){AI->SetActorTickEnabled(false);AI->StopMovement();if(AI->GetBrainComponent())AI->GetBrainComponent()->StopLogic(TEXT("bounded travel fixture"));}}
    }
    if(Now<Next)return;
    auto Advance=[&](int32 To,float Delay=1.f){Stage=To;Next=Now+Delay;};
    if(TravelStep>0&&Stage==0){Advance(90);return;}
    switch(Stage)
    {
    case 0:
        AuditCapsules(TEXT("initial"));
#if WITH_DEV_AUTOMATION_TESTS
        if(HeliosMenuQANavigate)HeliosMenuQANavigate(true,true);
#endif
        if(FParse::Param(FCommandLine::Get(),TEXT("HeliosRosterLoadQA")))
        {
            const TArray<EHeliosPetKind> Expected={EHeliosPetKind::SwordDog,EHeliosPetKind::ChanZeus,EHeliosPetKind::BuLianShiP90,EHeliosPetKind::BuLianShiP90};
            Check(TEXT("fresh_process_loads_saved_count_and_duplicates"),S->GetCount()==4&&S->SelectedKinds==Expected);Shot(TEXT("menu_reopened_saved_roster"));Advance(80,.5f);return;
        }
        H->SelectWeapon(TEXT("rifle"));
        if(FParse::Param(FCommandLine::Get(),TEXT("HeliosGeometryOnlyQA"))){H->SelectPetCount(0);H->StartSelectedRun();D->SetActorTickEnabled(false);Advance(30,3);return;}
        if(FParse::Param(FCommandLine::Get(),TEXT("HeliosP90RosterOnlyQA"))){KindCase=5;Advance(3);return;}
        Advance(1);return;
    case 80:
        H->StartSelectedRun();D->SetActorTickEnabled(false);Advance(81,4);return;
    case 81:
        Check(TEXT("fresh_process_spawns_all_saved_slots"),PW->Pets.Num()==4&&([&]{for(int32 I=0;I<4;++I)if(!IsValid(PW->Pets[I])||PW->Pets[I]->Kind!=S->GetKind(I)||PW->Pets[I]->Health!=PW->Pets[I]->Settings.MaxHealth)return false;return true;})());
        AuditCapsules(TEXT("reopened_saved_roster"));Shot(TEXT("reopened_saved_gameplay"));Advance(82,.5f);return;
    case 82:Finish();return;
    case 1:
        Check(FString::Printf(TEXT("actual_count_%d_clicked"),CountCase),Click(FString::Printf(TEXT("%d 位"),CountCase)));
        Check(TEXT("ui_count_matches"),H->GetSelectedPetCount()==CountCase);Shot(FString::Printf(TEXT("menu_count_%d"),CountCase));
        Advance(101,.5f);return;
    case 101:
        H->StartSelectedRun();D->SetActorTickEnabled(false);P->SetCanBeDamaged(false);Advance(2,3);return;
    case 2:
        Check(FString::Printf(TEXT("spawn_count_%d"),CountCase),PW->Pets.Num()==CountCase&&([&]{for(const auto& A:PW->Pets)if(!IsValid(A))return false;return true;})());
        AuditCapsules(FString::Printf(TEXT("count_%d"),CountCase));
        H->RequestRestartConfirmation();H->CancelRestartConfirmation();Check(TEXT("cancel_restart_keeps_roster"),PW->Pets.Num()==CountCase);H->ContinueRun();
        H->RequestRestartConfirmation();H->ConfirmRestart();
        if(++CountCase<=4){Advance(1);return;}KindCase=0;Advance(3);return;
    case 3:
        Check(TEXT("four_count_button"),Click(TEXT("4 位")));H->SelectPetSlot(0);
        Scroll(FHeliosPetDefinition::All()[KindCase].Name);Advance(4);return;
    case 4:
        for(int32 I=0;I<4;++I){Check(FString::Printf(TEXT("kind_%d_slot_%d_button"),KindCase,I),Click(FString::Printf(TEXT("伙伴 %d"),I+1)));Check(FString::Printf(TEXT("kind_%d_slot_%d_choice"),KindCase,I),Click(FHeliosPetDefinition::All()[KindCase].Name));}
        Check(TEXT("weapon_remains_independent"),H->GetPendingWeaponId()==TEXT("rifle"));Shot(FString::Printf(TEXT("menu_four_kind_%d"),KindCase));Advance(104,.5f);return;
    case 104:
        H->StartSelectedRun();D->SetActorTickEnabled(false);Advance(5,3);return;
    case 5:
    {
        bool Unique=PW->Pets.Num()==4;TSet<AActor*> Actors;for(auto& A:PW->Pets)if(IsValid(A)){Actors.Add(A);Unique&=A->Kind==FHeliosPetDefinition::All()[KindCase].Kind&&A->bModelReady&&A->GetController()!=nullptr;}else Unique=false;
        Check(FString::Printf(TEXT("four_independent_models_kind_%d"),KindCase),Unique&&Actors.Num()==4);if(!Unique){Finish();return;}
        AuditCapsules(FString::Printf(TEXT("repeat_kind_%d"),KindCase));
        Check(TEXT("player_manual_movement_does_not_enable_rvo"),!P->GetCharacterMovement()->bUseRVOAvoidance);
        FollowStart=P->GetActorLocation();PetStarts.Reset();for(const auto& A:PW->Pets)PetStarts.Add(A->GetActorLocation());
        P->AddMovementInput(P->GetActorForwardVector(),1);P->SetActorLocation(P->GetActorLocation()+P->GetActorForwardVector()*700,true);Advance(6,3);return;
    }
    case 6:
    {
        bool Follow=true;for(int32 I=0;I<PW->Pets.Num();++I)Follow&=FVector::Dist2D(PW->Pets[I]->GetActorLocation(),PetStarts[I])>10;
        Check(FString::Printf(TEXT("all_four_follow_kind_%d"),KindCase),Follow);
        const FVector Place=P->GetActorLocation()+P->GetActorForwardVector()*600;FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
        Target=GetWorld()->SpawnActor<AHeliosEnemyE01>(Place,FRotator::ZeroRotator,Params);
        if(!Target){Check(TEXT("passive_target_spawn"),false);Finish();return;}Target->CurrentHP=1000000;Target->SetActorTickEnabled(false);Target->StopShooting();Target->GetCharacterMovement()->DisableMovement();if(auto* AI=Cast<AAIController>(Target->GetController())){AI->StopMovement();if(AI->GetBrainComponent())AI->GetBrainComponent()->StopLogic(TEXT("multi QA passive target"));}
        BeforeHits.Reset();for(auto& A:PW->Pets)BeforeHits.Add(Cast<AHeliosZhongXiaoKuiPet>(A)?Cast<AHeliosZhongXiaoKuiPet>(A)->Contacts:A->HitsDelivered);
        if(KindCase==2)Cast<AHeliosSunShangXiangPet>(PW->Pets[0])->SkillRemaining=30;
        if(!bRecording&&KindCase==5){FApp::SetUnfocusedVolumeMultiplier(1);UAudioMixerBlueprintLibrary::StartRecordingOutput(this,40);bRecording=true;}
        Advance(7,10);return;
    }
    case 7:
    {
        if(KindCase==2){bool Independent=Cast<AHeliosSunShangXiangPet>(PW->Pets[0])->SkillRemaining>15;for(int32 I=1;I<4;++I)Independent&=Cast<AHeliosSunShangXiangPet>(PW->Pets[I])->SkillRemaining>0;Check(TEXT("ssx_each_skill_cooldown_independent"),Independent);}
        bool Attack=true;for(int32 I=0;I<4;++I){const int32 Hits=Cast<AHeliosZhongXiaoKuiPet>(PW->Pets[I])?Cast<AHeliosZhongXiaoKuiPet>(PW->Pets[I])->Contacts:PW->Pets[I]->HitsDelivered;Attack&=Hits>BeforeHits[I];auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("kind"),KindCase);Row->SetNumberField(TEXT("slot"),I);Row->SetNumberField(TEXT("hits"),Hits-BeforeHits[I]);if(auto* B=Cast<AHeliosBuLianShiP90Pet>(PW->Pets[I])){Row->SetNumberField(TEXT("shots"),B->ShotsFired);Row->SetNumberField(TEXT("sound_triggers"),B->FireAudio->TriggeredShots);Check(FString::Printf(TEXT("p90_slot_%d_real_shot_audio"),I),B->ShotsFired==B->FireAudio->TriggeredShots&&B->ShotsFired>0);}Samples.Add(MakeShared<FJsonValueObject>(Row));}
        Check(FString::Printf(TEXT("all_four_attack_kind_%d"),KindCase),Attack);Shot(FString::Printf(TEXT("combat_four_kind_%d"),KindCase));
        const float PeerHealth=PW->Pets[1]->Health;PW->Pets[0]->Settings.SleepSeconds=.6f;PW->Pets[0]->TakeDamage(1000000,FDamageEvent(),nullptr,Target);
        Check(TEXT("first_down_only"),PW->Pets[0]->IsSleeping()&&!PW->Pets[1]->IsSleeping()&&PW->Pets[1]->Health==PeerHealth);Advance(8,3);return;
    }
    case 8:
        Check(FString::Printf(TEXT("own_recovery_kind_%d"),KindCase),!PW->Pets[0]->IsSleeping()&&PW->Pets[0]->Health==PW->Pets[0]->Settings.MaxHealth);
        if(IsValid(Target)){Target->Destroy();Target=nullptr;}
        if(KindCase==5)
        {
            for(auto& A:PW->Pets)if(auto* B=Cast<AHeliosBuLianShiP90Pet>(A))
            {
                Check(TEXT("original_23_sp_voice_pool"),B->Voice->GetKillPoolSize()==23);B->SuspendCombat();B->GetCharacterMovement()->DisableMovement();
                const FVector Offset=FVector((B->RosterSlot%2?1:-1)*200,(B->RosterSlot/2?1:-1)*170,7);
                B->SetActorLocation(P->GetActorLocation()+Offset,false,nullptr,ETeleportType::TeleportPhysics);B->Voice->Stop();
            }
            for(auto& A:PW->Pets)Cast<AHeliosBuLianShiP90Pet>(A)->Voice->Play(TEXT("Kill"));
            Advance(9,.15f);return;
        }
        H->RequestRestartConfirmation();H->ConfirmRestart();
        if(++KindCase<6){Advance(3);return;}Advance(20);return;
    case 9:
    {
        int32 Playing=0;for(auto& A:PW->Pets)if(auto* B=Cast<AHeliosBuLianShiP90Pet>(A))Playing+=B->Voice->IsVoicePlaying()?1:0;
        Check(TEXT("four_simultaneous_voice_requests_bounded_to_two"),Playing>0&&Playing<=2&&PW->GetVoiceConcurrency()->Concurrency.MaxCount==2);
        auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("simultaneous_partner_voices"),Playing);Row->SetNumberField(TEXT("four_partner_gain"),PW->Pets[0]->RosterAudioGain());Samples.Add(MakeShared<FJsonValueObject>(Row));Advance(10,4);return;
    }
    case 10:
        if(FParse::Param(FCommandLine::Get(),TEXT("HeliosP90RosterOnlyQA"))){Finish();return;}
        for(auto& A:PW->Pets)if(auto* B=Cast<AHeliosBuLianShiP90Pet>(A))B->Voice->Stop();
        H->RequestRestartConfirmation();H->ConfirmRestart();Advance(20);return;
    case 20:
    {
        const TArray<EHeliosPetKind> Mixed={EHeliosPetKind::SwordDog,EHeliosPetKind::ChanZeus,EHeliosPetKind::BuLianShiP90,EHeliosPetKind::BuLianShiP90};
        H->SelectPetCount(4);for(int32 I=0;I<4;++I){H->SelectPetSlot(I);H->SelectPet(Mixed[I]);}
        H->SelectPetCount(1);H->SelectPetCount(4);Check(TEXT("reduce_and_reincrease_remembers_hidden_slots"),S->SelectedKinds==Mixed);
        H->SelectWeapon(TEXT("shotgun"));H->SelectMap(TEXT("UrbanChallenge"));Check(TEXT("map_weapon_keep_roster"),S->SelectedKinds==Mixed);
        Check(TEXT("save_multiset"),S->SaveSelection(Out/TEXT("roster.json")));H->SelectPetCount(0);Check(TEXT("load_duplicate_multiset"),S->LoadSelection(Out/TEXT("roster.json"))&&S->GetCount()==4&&S->SelectedKinds==Mixed);
        FFileHelper::SaveStringToFile(TEXT("{\"selected_kind\":4}"),*(Out/TEXT("legacy.json")));Check(TEXT("legacy_single_numeric_config"),S->LoadSelection(Out/TEXT("legacy.json"))&&S->GetCount()==1&&S->GetKind(0)==EHeliosPetKind::ChanZeus);
        S->LoadSelection(Out/TEXT("roster.json"));H->SelectPetSlot(0);Shot(TEXT("menu_mixed_four"));Advance(120,.5f);return;
    }
    case 120:
        H->StartSelectedRun();D->SetActorTickEnabled(false);Advance(21,3);return;
    case 21:
    {
        AuditCapsules(TEXT("mixed_four"));TravelKinds=S->SelectedKinds;TravelHealth.Reset();
        for(int32 I=0;I<PW->Pets.Num();++I){PW->Pets[I]->TakeDamage(float(13+I*9),FDamageEvent(),nullptr,nullptr);TravelHealth.Add(PW->Pets[I]->Health);}
        Advance(121,8);return;
    }
    case 121:
    {
        P->SetCanBeDamaged(true);P->TakeDamage(1000000,FDamageEvent(),nullptr,nullptr);Check(TEXT("real_player_dead"),P->IsDead());
        H->RequestRestartConfirmation();H->ConfirmRestart();H->StartSelectedRun();D->SetActorTickEnabled(false);Advance(22,3);return;
    }
    case 22:
        Check(TEXT("restart_recreates_all_healthy"),PW->Pets.Num()==4&&([&]{for(auto& A:PW->Pets)if(!A||A->Health!=A->Settings.MaxHealth)return false;return true;})());AuditCapsules(TEXT("restart_spawn"));
        Advance(30);return;
    case 30:
    {
        S->SetCount(0);PW->Clear(false);P->SetCanBeDamaged(false);
        SpawnBlocker=GetWorld()->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(SpawnBlocker);SpawnBlocker->SetRootComponent(Box);Box->SetBoxExtent(FVector(45,45,100));Box->SetCollisionProfileName(TEXT("BlockAll"));Box->RegisterComponent();SpawnBlocker->SetActorLocation(FVector(0,-3700,100));
        P->ResetToSafeSpawn();Check(TEXT("occupied_start_uses_local_safe_capsule"),FVector::Dist2D(P->GetActorLocation(),SpawnBlocker->GetActorLocation())>100);AuditCapsules(TEXT("occupied_start_fallback"));SpawnBlocker->Destroy();SpawnBlocker=nullptr;
        for(int32 I=0;I<4;++I){P->ResetToSafeSpawn();AuditCapsules(FString::Printf(TEXT("repeat_respawn_%d"),I));}
        P->SetActorLocation(FVector(-5000,1000,99),false,nullptr,ETeleportType::TeleportPhysics);P->GetCharacterMovement()->StopMovementImmediately();WalkStart=P->GetActorLocation();Advance(31,1.3f);return;
    }
    case 31:
        Check(TEXT("walk_into_truck_stops_at_hull"),P->GetActorLocation().X<WalkStart.X-100&&P->GetActorLocation().X>-5400+96+32);AuditCapsules(TEXT("truck_side_contact"));Shot(TEXT("truck_close_gameplay"));WalkStart=P->GetActorLocation();Advance(32,1.4f);return;
    case 32:
    {
        P->GetCharacterMovement()->StopMovementImmediately();Check(TEXT("walk_along_truck_not_stuck"),P->GetActorLocation().Y>WalkStart.Y+220);AuditCapsules(TEXT("truck_side_walk"));
        auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),FVector(-5100,650,0),FVector(-5100,1350,0),P);Check(TEXT("truck_adjacent_navigation_connected"),Path&&Path->IsValid()&&!Path->IsPartial());
        P->ResetToSafeSpawn();Advance(40);return;
    }
    case 40:
    {
        FCollisionQueryParams Q(SCENE_QUERY_STAT(ClinicPhysicalQA),false,P);PW->IgnoreAll(Q);FHitResult Hit;
        const auto* C=P->GetCapsuleComponent();const auto Shape=FCollisionShape::MakeCapsule(C->GetScaledCapsuleRadius(),C->GetScaledCapsuleHalfHeight());
        auto Sweep=[&](const FVector& A,const FVector& B){Hit=FHitResult();return GetWorld()->SweepSingleByChannel(Hit,A,B,FQuat::Identity,ECC_Pawn,Shape,Q);};
        Check(TEXT("clinic_side_wall_blocks_capsule"),Sweep(FVector(850,-3900,99),FVector(1200,-3900,99))&&Cast<UStaticMeshComponent>(Hit.GetComponent())&&Cast<UStaticMeshComponent>(Hit.GetComponent())->GetStaticMesh()->GetName()==TEXT("UC_FieldClinic"));
        Check(TEXT("clinic_front_solid_wall_blocks_capsule"),Sweep(FVector(600,-3500,99),FVector(600,-3050,99)));
        Check(TEXT("clinic_door_open_for_full_capsule"),!Sweep(FVector(0,-3500,99),FVector(0,-3050,99)));
        Check(TEXT("clinic_roof_blocks_upward_capsule"),Sweep(FVector(0,-3900,200),FVector(0,-3900,400)));
        auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),FVector(0,-3700,0),FVector(0,-3000,0),P);Check(TEXT("clinic_door_navigation_connected"),Path&&Path->IsValid()&&!Path->IsPartial());
        P->ResetToSafeSpawn();WalkStart=P->GetActorLocation();Advance(41,1.7f);return;
    }
    case 41:
        Check(TEXT("real_walk_out_clinic_door"),P->GetActorLocation().Y>-3200);AuditCapsules(TEXT("clinic_door_walk"));Shot(TEXT("clinic_exit_gameplay"));P->SetActorLocation(FVector(850,-3900,99),false,nullptr,ETeleportType::TeleportPhysics);P->GetCharacterMovement()->StopMovementImmediately();Advance(42,1.f);return;
    case 42:
        P->GetCharacterMovement()->StopMovementImmediately();Check(TEXT("real_walk_stops_at_clinic_wall"),P->GetActorLocation().X>880&&P->GetActorLocation().X<970);AuditCapsules(TEXT("clinic_wall_contact"));P->ResetToSafeSpawn();S->SetCount(4);Advance(33,3);return;
    case 33:
        Check(TEXT("four_after_truck_respawn"),PW->Pets.Num()==4);AuditCapsules(TEXT("four_respawn_final"));
        if(FParse::Param(FCommandLine::Get(),TEXT("HeliosGeometryOnlyQA"))){Shot(TEXT("four_clinic_spawn"));Advance(82,.5f);return;}
        for(auto& A:PW->Pets)if(IsValid(A))A->TakeDamage(17,FDamageEvent(),nullptr,nullptr);Advance(23);return;
    case 23:
        TravelHealth.Reset();for(auto& A:PW->Pets)TravelHealth.Add(A->Health);
        Finish(false);
        H->RequestRestartConfirmation();H->ConfirmRestart();Check(TEXT("menu_snapshots_each_health_before_travel"),([&]{for(int32 I=0;I<4;++I)if(!S->Snapshots[I].bValid||S->Snapshots[I].Health!=TravelHealth[I])return false;return true;})());H->SelectMap(TEXT("HeliosAdventure"));TravelStep=1;H->StartSelectedRun();Advance(99,25);return;
    case 90:
        if(D->IsSelectingWeapon()&&Now-Started<90){Next=Now+.5;return;}
        Check(TEXT("travel_keeps_four_selected_and_order"),S->GetCount()==4&&S->SelectedKinds==TravelKinds);if(!D->GetTownSubsystem())D->SetActorTickEnabled(false);P->SetCanBeDamaged(false);Advance(91,5);return;
    case 91:
        if(PW->Pets.Num()!=4&&Now-Started<90){Next=Now+.5;return;}
        for(int32 I=0;I<PW->Pets.Num();++I)if(IsValid(PW->Pets[I])){auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("slot"),I);Row->SetNumberField(TEXT("health"),PW->Pets[I]->Health);Row->SetNumberField(TEXT("expected_health"),TravelHealth[I]);Samples.Add(MakeShared<FJsonValueObject>(Row));}
        Check(TEXT("travel_spawns_four"),PW->Pets.Num()==4);Check(TEXT("travel_keeps_each_health"),PW->Pets.Num()==4&&([&]{for(int32 I=0;I<4;++I)if(!PW->Pets[I]||PW->Pets[I]->Health!=TravelHealth[I])return false;return true;})());AuditCapsules(TEXT("travel"));Shot(TEXT("travel_roster"));
        Advance(92,.5f);return;
    case 92:
        if(TravelStep<3){Finish(false);H->RequestRestartConfirmation();H->ConfirmRestart();H->SelectMap(TravelStep==1?TEXT("IkunTown"):TEXT("IkunGhostVillage"));++TravelStep;H->StartSelectedRun();Advance(99,30);return;}Finish();return;
    case 99:Check(TEXT("travel_completed_before_timeout"),false);Finish();return;
    }
}
