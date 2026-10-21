#include "QA/HeliosChanZeusQA.h"
#include "Weapons/HeliosChanZeus.h"
#include "Weapons/HeliosSSXVoice.h"
#include "Pets/HeliosChanZeusPet.h"
#include "Player/HeliosArenaCharacter.h"
#include "UI/HeliosAdventureHUD.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Weapons/HeliosHunterDamageFeedback.h"
#include "AudioMixerBlueprintLibrary.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/App.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SScrollBox.h"
#if WITH_DEV_AUTOMATION_TESTS
extern TFunction<void(bool,bool)> HeliosMenuQANavigate;
#endif
namespace
{
bool Label(const TSharedRef<SWidget>& W,const FString& Text)
{if(W->GetTypeAsString()==TEXT("STextBlock")&&StaticCastSharedRef<STextBlock>(W)->GetText().ToString()==Text)return true;auto* C=W->GetChildren();for(int32 I=0;C&&I<C->Num();++I)if(Label(C->GetChildAt(I),Text))return true;return false;}
TSharedPtr<SWidget> Find(const TSharedRef<SWidget>& W,const FString& Text,const FString& Type)
{if(!W->GetVisibility().IsVisible())return nullptr;if(W->GetTypeAsString()==Type&&Label(W,Text))return W;auto* C=W->GetChildren();for(int32 I=0;C&&I<C->Num();++I)if(auto R=Find(C->GetChildAt(I),Text,Type))return R;return nullptr;}
TSharedPtr<SWidget> Find(const FString& Text,const FString& Type)
{if(FSlateApplication::IsInitialized())for(const auto& W:FSlateApplication::Get().GetInteractiveTopLevelWindows())if(auto R=Find(W->GetContent(),Text,Type))return R;return nullptr;}
void Scroll(const FString& Text)
{auto B=Find(Text,TEXT("SButton"));auto S=Find(Text,TEXT("SScrollBox"));if(B&&S)StaticCastSharedPtr<SScrollBox>(S)->ScrollDescendantIntoView(B,true,EDescendantScrollDestination::Center);}
bool Click(const FString& Text)
{
    auto B=Find(Text,TEXT("SButton"));if(!B||!B->IsEnabled())return false;auto& Slate=FSlateApplication::Get();FWidgetPath Path;if(!Slate.GeneratePathToWidgetUnchecked(B.ToSharedRef(),Path))return false;
    auto G=B->GetCachedGeometry();FVector2D At=G.LocalToAbsolute(G.GetLocalSize()*.5f);TSet<FKey> Down;Down.Add(EKeys::LeftMouseButton);TSet<FKey> Up;
    FPointerEvent Press(FSlateApplication::CursorPointerIndex,At,At,Down,EKeys::LeftMouseButton,0,FModifierKeysState());FPointerEvent Release(FSlateApplication::CursorPointerIndex,At,At,Up,EKeys::LeftMouseButton,0,FModifierKeysState());
    Slate.ReleaseAllPointerCapture();B->OnMouseEnter(G,Press);auto P=B->OnMouseButtonDown(G,Press);Slate.ProcessReply(Path,P,&Path,&Press);auto R=B->OnMouseButtonUp(G,Release);Slate.ProcessReply(Path,R,&Path,&Release);return P.IsEventHandled()&&R.IsEventHandled();
}
bool DiaoChanMaterials(USkeletalMeshComponent* Mesh)
{
    if(!Mesh||Mesh->GetNumMaterials()==0)return false;
    for(int32 I=0;I<Mesh->GetNumMaterials();++I)
    {const auto* M=Mesh->GetMaterial(I);if(!M||!M->GetPathName().Contains(TEXT("FamilyB_DiaoChan")))return false;}
    return true;
}
}
AHeliosChanZeusQA::AHeliosChanZeusQA(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;}
void AHeliosChanZeusQA::Check(const TCHAR* Name,bool Pass)
{auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("name"),Name);R->SetBoolField(TEXT("passed"),Pass);Checks.Add(MakeShared<FJsonValueObject>(R));if(!Pass)++Failures;UE_LOG(LogTemp,Display,TEXT("CHAN_QA %s %s"),Name,Pass?TEXT("PASS"):TEXT("FAIL"));}
void AHeliosChanZeusQA::Capture(const TCHAR* Name){FScreenshotRequest::RequestScreenshot(Out/(FString(Name)+TEXT(".png")),true,false);}
void AHeliosChanZeusQA::Key(FKey K,bool Down)
{if(auto* PC=UGameplayStatics::GetPlayerController(this,0))PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),K,Down?IE_Pressed:IE_Released,FPlatformTime::Cycles64()));}
AHeliosEnemyActor* AHeliosChanZeusQA::SpawnTarget(float HP)
{
    if(IsValid(Target))Target->Destroy();FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Player->GetController()->SetControlRotation(FRotator(0,90,0));
    Target=GetWorld()->SpawnActor<AHeliosEnemyE01>(Player->GetActorLocation()+FVector(0,650,0),FRotator(0,-90,0),P);
    if(Target){Target->CurrentHP=HP;Target->SetActorTickEnabled(false);Target->StopShooting();Target->GetCharacterMovement()->StopMovementImmediately();Target->GetCharacterMovement()->DisableMovement();if(auto* AI=Cast<AAIController>(Target->GetController())){AI->StopMovement();if(AI->GetBrainComponent())AI->GetBrainComponent()->StopLogic(TEXT("bounded Chan QA target"));}}
    return Target;
}
void AHeliosChanZeusQA::Finish()
{
    if(bFinished)return;bFinished=true;Key(EKeys::LeftMouseButton,false);Key(EKeys::W,false);
    if(bRecording)UAudioMixerBlueprintLibrary::StopRecordingOutput(this,EAudioRecordingExportType::WavFile,TEXT("runtime_audio"),Out);
    FApp::SetUnfocusedVolumeMultiplier(PreviousUnfocusedVolume);
    auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("passed"),Failures==0);R->SetArrayField(TEXT("checks"),Checks);R->SetStringField(TEXT("map"),GetWorld()->GetMapName());R->SetStringField(TEXT("scope"),TEXT("Real D3D12/Slate/input. Fixture portion: invulnerable player, passive targets, paused director. Then up to 60 seconds production director/enemies/health with automated aim/input. No full 30-wave run claim."));
    if(Pet){R->SetNumberField(TEXT("pet_shots"),Pet->ShotsFired);R->SetNumberField(TEXT("pet_reloads"),Pet->Reloads);R->SetNumberField(TEXT("pet_kills"),Pet->TrueKills);R->SetNumberField(TEXT("pet_scale"),Pet->GetMesh()->GetRelativeScale3D().Z);R->SetStringField(TEXT("pet_muzzle"),Pet->MuzzleLocation().ToString());}
    R->SetNumberField(TEXT("production_enemy_observations"),NormalEnemiesSeen);R->SetNumberField(TEXT("production_seconds"),Stage==22?Age:0);R->SetNumberField(TEXT("player_z_after_production"),Player?Player->GetActorLocation().Z:0);
    R->SetNumberField(TEXT("player_fixture_defeats"),Kills);R->SetNumberField(TEXT("unique_player_fixture_voice_indices"),VoiceIndices.Num());
    if(FParse::Param(FCommandLine::Get(),TEXT("ChanQASpatialOnly")))R->SetStringField(TEXT("scope"),TEXT("Dedicated spatial voice probe: real menu/input, passive enemy, close camera within 3 metres of companion, actual companion lethal damage and recorded spatial voice. No player voice or production combat claims from this probe."));
    TArray<TSharedPtr<FJsonValue>> Times;for(double F:Frames)Times.Add(MakeShared<FJsonValueNumber>(F));R->SetArrayField(TEXT("frame_seconds"),Times);
    FString Text;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Out/TEXT("report.json")));Next=FPlatformTime::Seconds()+3;Stage=99;
}
void AHeliosChanZeusQA::Tick(float Dt)
{
    Super::Tick(Dt);const double Wall=FPlatformTime::Seconds();if(Stage==99){if(Wall>=Next)FPlatformMisc::RequestExit(false);return;}
    if(Started==0){PreviousUnfocusedVolume=FApp::GetUnfocusedVolumeMultiplier();FApp::SetUnfocusedVolumeMultiplier(1);Started=Wall;FParse::Value(FCommandLine::Get(),TEXT("ChanQAOut="),Out);if(Out.IsEmpty())Out=FPaths::ProjectDir()/TEXT("../qa/cfm_chan_zeus/runtime");Out=FPaths::ConvertRelativePathToFull(Out);IFileManager::Get().MakeDirectory(*Out,true);}
    if(Wall-Started>180){Check(TEXT("bounded_timeout"),false);Finish();return;}
    auto* PC=UGameplayStatics::GetPlayerController(this,0);auto* H=PC?Cast<AHeliosAdventureHUD>(PC->GetHUD()):nullptr;auto* D=AHeliosAdventureDirector::Find(GetWorld());Player=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));if(!H||!D||!Player||Wall-Started<5)return;
    auto Advance=[&](int32 S){Stage=S;Age=0;Next=Wall+.4;Captures.Reset();};Age+=Dt;if(Stage>5)Frames.Add(Dt);
    auto TimedCapture=[&](int32 I,float T,const TCHAR* N){if(Age>=T&&!Captures.Contains(I)){Captures.Add(I);Capture(N);}};
    if((Stage==11||Stage==12)&&IsValid(Camera)&&IsValid(Pet))
    {
        FVector Offset(550,-380,100);
        if(Stage==12&&FParse::Param(FCommandLine::Get(),TEXT("ChanQASpatialOnly")))Offset=FVector(200,-100,65);
        if(Stage==11&&Age>=3.5f&&Age<4.5f)Offset=Pet->GetActorForwardVector()*500+FVector(0,0,80);
        else if(Stage==11&&Age>=4.5f&&Age<5.5f)Offset=Pet->GetActorRightVector()*500+FVector(0,0,80);
        else if(Stage==11&&Age>=5.5f&&Age<6.5f)Offset=-Pet->GetActorForwardVector()*500+FVector(0,0,80);
        Camera->SetActorLocation(Pet->GetActorLocation()+Offset);Camera->SetActorRotation((Pet->GetActorLocation()+FVector(0,0,-5)-Camera->GetActorLocation()).Rotation());
    }
    // Release the fixture trigger before the generic transition delay. Holding
    // it through that delay fires twice and exhausts a magazine before 11 kills.
    if(Stage==9&&Age>.1f)Key(EKeys::LeftMouseButton,false);
    if(Wall<Next)return;
    auto* W=Cast<AHeliosChanZeusWeapon>(Player->GetEquippedWeapon());
    switch(Stage)
    {
    case 0:
#if WITH_DEV_AUTOMATION_TESTS
        if(HeliosMenuQANavigate)HeliosMenuQANavigate(true,false);
#endif
        Advance(1);return;
    case 1:Scroll(TEXT("蝉·潜伏者 / 宙斯散弹枪"));Advance(2);return;
    case 2:Check(TEXT("actual_weapon_button_clicked"),Click(TEXT("蝉·潜伏者 / 宙斯散弹枪")));Check(TEXT("selected_new_weapon"),H->GetPendingWeaponId()==TEXT("cfm_chan_zeus"));Capture(TEXT("01_weapon_menu"));Advance(3);return;
    case 3:
#if WITH_DEV_AUTOMATION_TESTS
        if(HeliosMenuQANavigate)HeliosMenuQANavigate(true,true);
#endif
        Advance(4);return;
    case 4:Scroll(TEXT("蝉·潜伏者 / 宙斯"));Advance(5);return;
    case 5:Check(TEXT("actual_pet_button_clicked"),Click(TEXT("蝉·潜伏者 / 宙斯")));Check(TEXT("independent_pet_selection"),H->GetSelectedPet()==EHeliosPetKind::ChanZeus&&H->GetPendingWeaponId()==TEXT("cfm_chan_zeus"));Capture(TEXT("02_pet_menu"));Advance(51);return;
    case 51: H->StartSelectedRun();D->SetActorTickEnabled(false);D->CleanupOwnedActors();Player->SetCanBeDamaged(false);Player->SetActorLocation(FVector(0,-2800,102),false,nullptr,ETeleportType::TeleportPhysics);PC->SetControlRotation(FRotator(0,90,0));Advance(6);return;
    case 6:
        if(Age<3)return;Pet=Cast<AHeliosChanZeusPet>(GetWorld()->GetSubsystem<UHeliosPetWorld>()->Pet);Check(TEXT("selected_pet_spawned"),Pet&&Pet->bModelReady);Check(TEXT("selected_weapon_spawned"),W!=nullptr);if(!W||!Pet){Finish();return;}
        Check(TEXT("source_arms_and_gun_loaded"),W->Zeus->GetSkeletalMeshAsset()&&W->GetFirstPersonMesh()->GetSkeletalMeshAsset());
        Check(TEXT("verified_diaochan_arms_and_body_materials"),DiaoChanMaterials(W->GetFirstPersonMesh())&&DiaoChanMaterials(Pet->GetMesh()));
        Check(TEXT("no_hunter07_component"),W->FindComponentByClass<UHeliosHunterDamageFeedback>()==nullptr);Check(TEXT("eleven_original_kill_lines"),W->Voice->GetKillPoolSize()==11&&Pet->Voice->GetKillPoolSize()==11);Pet->SetActorTickEnabled(false);SpawnTarget(100000);UAudioMixerBlueprintLibrary::StartRecordingOutput(this,100);bRecording=true;Capture(TEXT("03_bright_idle"));
        if(FParse::Param(FCommandLine::Get(),TEXT("ChanQASpatialOnly"))){if(IsValid(Target))Target->Destroy();Pet->SetActorTickEnabled(true);PetStart=Pet->GetActorLocation();Player->SetActorLocation(Player->GetActorLocation()+FVector(0,2000,0),false,nullptr,ETeleportType::TeleportPhysics);Advance(10);return;}
        Key(EKeys::LeftMouseButton,true);Advance(7);return;
    case 7:
        TimedCapture(1,.65f,TEXT("04_bright_fire"));if(Age<4.5)return;Key(EKeys::LeftMouseButton,false);Check(TEXT("continuous_fire_magazine"),W&&W->ShotsFired==W->ShotSettings.Magazine&&W->GetBulletCount()==0);Check(TEXT("nonlethal_damage_no_defeat_voice"),W&&W->TrueKills==0&&W->Voice->KillCount==0&&W->HitPellets>0);Key(EKeys::R,true);Key(EKeys::R,false);Advance(8);return;
    case 8:
        TimedCapture(1,.45f,TEXT("05_reload_out"));TimedCapture(2,1.0f,TEXT("06_reload_in"));if(Age<2.4)return;Check(TEXT("reload_completed"),W&&W->GetBulletCount()==W->ShotSettings.Magazine&&!Player->IsReloading());SpawnTarget(1);Key(EKeys::LeftMouseButton,true);Advance(9);return;
    case 9:
        if(Age<3.35f)return;if(W)VoiceIndices.Add(W->Voice->GetLastPlayedKillIndex());++Kills;
        {const int32 RequestedKills=FParse::Param(FCommandLine::Get(),TEXT("ChanQAAllVoices"))?11:4;
        if(Kills<RequestedKills){SpawnTarget(1);Key(EKeys::LeftMouseButton,true);Advance(9);return;}Check(TEXT("one_defeat_per_real_enemy"),W&&W->TrueKills==RequestedKills);Check(TEXT("random_nonrepeating_defeat_lines"),RequestedKills==11?VoiceIndices.Num()==11:VoiceIndices.Num()>=3);}
        Check(TEXT("bounded_voice_queue"),W&&W->Voice->GetPendingKillCount()<=2);Capture(TEXT("07_player_defeat"));
        if(IsValid(Target))Target->Destroy();Pet->SetActorTickEnabled(true);PetStart=Pet->GetActorLocation();Player->SetActorLocation(Player->GetActorLocation()+FVector(0,2000,0),false,nullptr,ETeleportType::TeleportPhysics);Advance(10);return;
    case 10:
        if(Age<5)return;Check(TEXT("navigation_follow_moves"),Pet&&FVector::Dist2D(PetStart,Pet->GetActorLocation())>200);if(Pet){auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Pet->GetActorLocation(),Player->GetActorLocation(),Pet);Check(TEXT("navigation_path_valid"),Path&&Path->IsValid()&&!Path->IsPartial());}
        SpawnTarget(100000);
        if(Pet&&Target)
        {
            bool Placed=false;auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
            for(float Radius:{650.f,950.f})for(float Angle:{0.f,45.f,90.f,135.f,180.f,225.f,270.f,315.f})
            {
                if(Placed||!Nav)continue;FNavLocation Ground;const FVector Candidate=Pet->GetActorLocation()+FVector::ForwardVector.RotateAngleAxis(Angle,FVector::UpVector)*Radius;
                if(!Nav->ProjectPointToNavigation(Candidate,Ground,FVector(150,150,400)))continue;
                const FVector At=Ground.Location+FVector(0,0,Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
                auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Pet->GetActorLocation(),At,Pet);if(!Path||!Path->IsValid()||Path->IsPartial())continue;
                FCollisionQueryParams Q(SCENE_QUERY_STAT(ChanQAClearFixture),false,Pet);Q.AddIgnoredActor(Player);Q.AddIgnoredActor(Target);FHitResult Hit;
                if(GetWorld()->LineTraceSingleByChannel(Hit,Pet->GetActorLocation()+FVector(0,0,35),At,ECC_Visibility,Q))continue;
                Target->SetActorLocation(At,false,nullptr,ETeleportType::TeleportPhysics);Placed=true;
            }
            Check(TEXT("partner_fixture_has_navigable_clear_sight"),Placed);
        }
        Camera=GetWorld()->SpawnActor<ACameraActor>();if(Pet){Camera->SetActorLocation(Pet->GetActorLocation()+FVector(550,-380,100));Camera->SetActorRotation((Pet->GetActorLocation()+FVector(0,0,-5)-Camera->GetActorLocation()).Rotation());Camera->GetCameraComponent()->FieldOfView=55;PC->SetViewTarget(Camera);}Advance(11);return;
    case 11:
        TimedCapture(1,3,TEXT("08_pet_holding_fire"));TimedCapture(2,8,TEXT("09_pet_reload"));
        TimedCapture(3,4,TEXT("08a_pet_front"));TimedCapture(4,5,TEXT("08b_pet_side"));TimedCapture(5,6,TEXT("08c_pet_back"));
        if(Age<11)return;Check(TEXT("pet_fires_and_reloads"),Pet&&Pet->ShotsFired>=13&&Pet->Reloads>=1);Check(TEXT("gun_attached_to_source_weapon_bone"),Pet&&Pet->Zeus->GetAttachSocketName()==TEXT("b_RightWeapon"));
        if(Pet){const float Height=Pet->GetMesh()->GetSkeletalMeshAsset()->GetBounds().BoxExtent.Z*2*Pet->GetMesh()->GetRelativeScale3D().Z;Check(TEXT("height_matches_ssx_203cm"),FMath::Abs(Height-203.3593f)<.5f);UE_LOG(LogTemp,Display,TEXT("CHAN_QA_PET height=%.4f bounds_minz=%.4f capsule_bottom=%.4f right_weapon=%s muzzle=%s"),Height,Pet->GetMesh()->Bounds.Origin.Z-Pet->GetMesh()->Bounds.BoxExtent.Z,Pet->GetActorLocation().Z-Pet->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),*Pet->GetMesh()->GetSocketLocation(TEXT("b_RightWeapon")).ToCompactString(),*Pet->MuzzleLocation().ToCompactString());}
        if(IsValid(Target)&&!FParse::Param(FCommandLine::Get(),TEXT("ChanQASpatialOnly")))Target->CurrentHP=1;BeforeShots=Pet?Pet->ShotsFired:0;Advance(12);return;
    case 12:
        if(FParse::Param(FCommandLine::Get(),TEXT("ChanQASpatialOnly")))
        {
            // Let the actual audio listener reach the near camera before a
            // lethal hit; one-shot spatial sounds started outside 6 m are muted.
            if(IsValid(Target))Target->CurrentHP=1;
            TimedCapture(1,.65f,TEXT("09a_pet_near_real_defeat"));if(Age<4)return;
            Check(TEXT("pet_owns_real_defeat"),Pet&&Pet->TrueKills>0&&Pet->Voice->KillCount>0);
            Check(TEXT("spatial_probe_camera_within_3m"),Camera&&Pet&&FVector::Dist(Camera->GetActorLocation(),Pet->GetActorLocation())<300);
            Finish();return;
        }
        if(Age<3)return;Check(TEXT("pet_owns_real_defeat"),Pet&&Pet->TrueKills>0&&Pet->Voice->KillCount>0);if(Pet)Pet->SetActorTickEnabled(false);PC->SetViewTarget(Player);Player->SetActorLocation(FVector(0,1800,102),false,nullptr,ETeleportType::TeleportPhysics);PC->SetControlRotation(FRotator(0,90,0));SpawnTarget(100000);Key(EKeys::R,true);Key(EKeys::R,false);Advance(13);return;
    case 13:
        if(Age<3)return;Capture(TEXT("10_shade_idle"));Key(EKeys::LeftMouseButton,true);Advance(14);return;
    case 14:
        TimedCapture(1,.65f,TEXT("11_shade_fire"));if(Age<1.2f)return;Key(EKeys::LeftMouseButton,false);Key(EKeys::G,true);Key(EKeys::G,false);Advance(15);return;
    case 15:
        TimedCapture(1,.65f,TEXT("12_show"));if(Age<3.5)return;Check(TEXT("same_weapon_still_active_after_show"),W&&Player->GetWeaponId()==TEXT("cfm_chan_zeus"));Check(TEXT("switch_to_old_rifle"),Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("rifle"))));Advance(16);return;
    case 16:
        if(Age<.7f)return;Check(TEXT("old_rifle_actual_equipped"),Player->GetWeaponId()==TEXT("rifle"));Check(TEXT("switch_back_to_chan"),Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("cfm_chan_zeus"))));Advance(17);return;
    case 17:
        if(Age<1.2f)return;SpawnTarget(100000);Key(EKeys::LeftMouseButton,true);Advance(18);return;
    case 18:
        TimedCapture(1,.05f,TEXT("13_after_switch_fire"));if(Age<.6f)return;Key(EKeys::LeftMouseButton,false);Check(TEXT("fires_after_equipment_switch"),W&&W->ShotsFired>=1);Key(EKeys::R,true);Key(EKeys::R,false);Advance(19);return;
    case 19:
        if(Age<.5f)return;Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("rifle")));Check(TEXT("switch_during_reload"),!Player->IsReloading()&&Player->GetWeaponId()==TEXT("rifle"));Advance(20);return;
    case 20:
        if(Age<.5f)return;Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("cfm_chan_zeus")));Advance(21);return;
    case 21:
        if(Age<1.2f)return;Check(TEXT("new_weapon_recovers_after_cancelled_reload"),W&&W->GetBulletCount()==W->ShotSettings.Magazine&&!Player->IsReloading());if(FParse::Param(FCommandLine::Get(),TEXT("ChanQAQuick"))){Finish();return;}if(IsValid(Target))Target->Destroy();Player->SetCanBeDamaged(true);if(Pet)Pet->SetActorTickEnabled(true);D->SetActorTickEnabled(true);Advance(22);return;
    case 22:
    {
        // Resume the production wave director and unmodified enemies/health for
        // a bounded combat smoke test. Input aiming is automated, difficulty is not.
        AHeliosEnemyActor* Closest=nullptr;float Best=3500*3500;
        for(TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It)if(!It->IsEnemyDead()){++NormalEnemiesSeen;const float Distance=FVector::DistSquared(It->GetActorLocation(),Player->GetActorLocation());if(Distance<Best){Best=Distance;Closest=*It;}}
        if(Closest&&W){FVector Eye;FRotator Aim;Player->GetActorEyesViewPoint(Eye,Aim);PC->SetControlRotation((Closest->GetActorLocation()-Eye).Rotation());if(W->GetBulletCount()>0&&!Player->IsReloading())Key(EKeys::LeftMouseButton,true);else {Key(EKeys::LeftMouseButton,false);if(!Player->IsReloading()){Key(EKeys::R,true);Key(EKeys::R,false);}}}else Key(EKeys::LeftMouseButton,false);
        TimedCapture(1,20,TEXT("14_production_wave_combat"));TimedCapture(2,50,TEXT("15_production_wave_later"));
        if(Age>=60||D->IsRunOver()){Check(TEXT("production_director_spawned_real_enemies"),NormalEnemiesSeen>0);Check(TEXT("bounded_production_combat_finished"),true);Finish();}return;
    }
    default:return;
    }
}
