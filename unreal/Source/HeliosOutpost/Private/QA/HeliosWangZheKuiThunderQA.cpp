#include "QA/HeliosWangZheKuiThunderQA.h"
#include "QA/HeliosAsyncFrameCapture.h"
#include "Weapons/HeliosWangZheKuiThunder.h"
#include "Weapons/HeliosSSXVoice.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Weapons/HeliosChanZeus.h"
#include "Pets/HeliosChanZeusPet.h"
#include "Engine/DamageEvents.h"
#include "Engine/GameInstance.h"
#include "Components/AudioComponent.h"
#include "Pets/HeliosWangZheKuiThunderPet.h"
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
#include "Engine/StaticMesh.h"
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
bool Click(const FString& Text,bool Card=false)
{
    auto Scroll=Card?Find(Text,TEXT("SScrollBox")):nullptr;if(Card&&!Scroll)return false;auto B=Card?Find(Scroll.ToSharedRef(),Text,TEXT("SButton")):Find(Text,TEXT("SButton"));if(!B||!B->IsEnabled())return false;auto& Slate=FSlateApplication::Get();FWidgetPath Path;if(!Slate.GeneratePathToWidgetUnchecked(B.ToSharedRef(),Path))return false;
    auto G=B->GetCachedGeometry();FVector2D At=G.LocalToAbsolute(G.GetLocalSize()*.5f);TSet<FKey> Down;Down.Add(EKeys::LeftMouseButton);TSet<FKey> Up;
    FPointerEvent Press(FSlateApplication::CursorPointerIndex,At,At,Down,EKeys::LeftMouseButton,0,FModifierKeysState());FPointerEvent Release(FSlateApplication::CursorPointerIndex,At,At,Up,EKeys::LeftMouseButton,0,FModifierKeysState());
    Slate.ReleaseAllPointerCapture();B->OnMouseEnter(G,Press);auto P=B->OnMouseButtonDown(G,Press);Slate.ProcessReply(Path,P,&Path,&Press);auto R=B->OnMouseButtonUp(G,Release);Slate.ProcessReply(Path,R,&Path,&Release);return P.IsEventHandled()&&R.IsEventHandled();
}
int32 OwnedEffects(UWorld* World,AActor* Owner)
{int32 N=0;for(TActorIterator<AHeliosSSXEffect> It(World);It;++It)if(IsValid(*It)&&It->GetOwner()==Owner)++N;return N;}
bool NoInactiveKuiEffects(AHeliosArenaCharacter* P)
{for(TActorIterator<AHeliosSSXEffect> It(P->GetWorld());It;++It)if(auto* W=Cast<AHeliosWangZheKuiThunderWeapon>(It->GetOwner());W&&W!=P->GetEquippedWeapon())return false;return true;}
bool WangZheKuiMaterials(USkeletalMeshComponent* Mesh)
{
    if(!Mesh||Mesh->GetNumMaterials()==0)return false;
    for(int32 I=0;I<Mesh->GetNumMaterials();++I)
    {const auto* M=Mesh->GetMaterial(I);if(!M||!(M->GetPathName().Contains(TEXT("FamilyP_Kui_WangZhe"))||M->GetPathName().Contains(TEXT("FamilyB_Kui_WangZhe_PBR_3P_Face"))))return false;}
    return true;
}
}
AHeliosWangZheKuiThunderQA::AHeliosWangZheKuiThunderQA(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void AHeliosWangZheKuiThunderQA::Check(const TCHAR* Name,bool Pass)
{auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("name"),Name);R->SetBoolField(TEXT("passed"),Pass);Checks.Add(MakeShared<FJsonValueObject>(R));if(!Pass)++Failures;UE_LOG(LogTemp,Display,TEXT("KUI_QA %s %s"),Name,Pass?TEXT("PASS"):TEXT("FAIL"));}
void AHeliosWangZheKuiThunderQA::Capture(const TCHAR* Name){FScreenshotRequest::RequestScreenshot(Out/(FString(Name)+TEXT(".png")),true,false);}
void AHeliosWangZheKuiThunderQA::Key(FKey K,bool Down)
{if(auto* PC=UGameplayStatics::GetPlayerController(this,0))PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),K,Down?IE_Pressed:IE_Released,FPlatformTime::Cycles64()));}
AHeliosEnemyActor* AHeliosWangZheKuiThunderQA::SpawnTarget(float HP)
{
    if(IsValid(Target))Target->Destroy();FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Player->GetController()->SetControlRotation(FRotator(0,90,0));
    Target=GetWorld()->SpawnActor<AHeliosEnemyE01>(Player->GetActorLocation()+FVector(0,650,0),FRotator(0,-90,0),P);
    if(Target){Target->CurrentHP=HP;Target->SetActorTickEnabled(false);Target->StopShooting();Target->GetCharacterMovement()->StopMovementImmediately();Target->GetCharacterMovement()->DisableMovement();if(auto* AI=Cast<AAIController>(Target->GetController())){AI->StopMovement();if(AI->GetBrainComponent())AI->GetBrainComponent()->StopLogic(TEXT("bounded Chan QA target"));}}
    return Target;
}
void AHeliosWangZheKuiThunderQA::SampleReloadEffects(AHeliosWangZheKuiThunderWeapon* W)
{
    if(!W)return;
    ++ReloadSamples;
    for(TActorIterator<AHeliosSSXEffect> It(GetWorld());It;++It)
    {
        auto* E=*It;if(E->GetOwner()!=W)continue;
        const FName Socket=E->GetRootComponent()->GetAttachSocketName();
        if(E!=Probe&&Socket==TEXT("FireEffectSocket")&&E->VisibleTrackCount()>0)
        {if(ReloadFirstVisible<0)ReloadFirstVisible=Age;ReloadLastVisible=Age;}
        if(Socket!=TEXT("EffectSocket_DanJia")&&Socket!=TEXT("EffectSocket_JiChui"))continue;
        if(E!=Probe)bReloadAttachmentsValid&=E->GetRootComponent()->GetAttachParent()==W->Thunder&&E->PrimaryActorTick.TickGroup==TG_PostUpdateWork;
        TInlineComponentArray<UStaticMeshComponent*> Geometry(E);
        for(auto* C:Geometry)
        {
            if(!C->GetStaticMesh())continue;
            const FString Name=C->GetStaticMesh()->GetName();
            // These source mesh tracks have an identity transform for every
            // sample. Unlike billboards, their orientation belongs to the bone.
            const bool Identity=Name.EndsWith(TEXT("DanJia_001"))||Name.EndsWith(TEXT("JiChui_001"))||Name.EndsWith(TEXT("JiChui_002"))||Name.EndsWith(TEXT("JiChui_004"))||Name.EndsWith(TEXT("JiChui_006"))||Name.EndsWith(TEXT("JiChui_007"))||Name.EndsWith(TEXT("JiChui_008"));
            if(!Identity)continue;
            const float Position=FVector::Distance(C->GetComponentLocation(),E->GetActorLocation());
            if(E==Probe){LegacyPositionError=FMath::Max(LegacyPositionError,Position);continue;}
            ++ReloadGeometrySamples;ReloadPositionError=FMath::Max(ReloadPositionError,Position);
            ReloadRotationError=FMath::Max(ReloadRotationError,FMath::RadiansToDegrees(C->GetComponentQuat().AngularDistance(E->GetActorQuat())));
        }
    }
    UE_LOG(LogTemp,Display,TEXT("KUI_RELOAD_SAMPLE case=%d age=%.6f seconds=%.3f fixed_error_cm=%.6f fixed_error_deg=%.6f legacy_error_cm=%.6f first_visible=%.6f last_visible=%.6f player=%s"),ReloadCase,Age,ReloadTestSeconds,ReloadPositionError,ReloadRotationError,LegacyPositionError,ReloadFirstVisible,ReloadLastVisible,*Player->GetActorLocation().ToCompactString());
}
void AHeliosWangZheKuiThunderQA::Finish()
{
    if(bFinished)return;bFinished=true;HeliosAsyncFrameCapture::Finish(Out);Key(EKeys::LeftMouseButton,false);Key(EKeys::W,false);
    if(bRecording)UAudioMixerBlueprintLibrary::StopRecordingOutput(this,EAudioRecordingExportType::WavFile,TEXT("runtime_audio"),Out);
    FApp::SetUnfocusedVolumeMultiplier(PreviousUnfocusedVolume);
    auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("passed"),Failures==0);R->SetArrayField(TEXT("checks"),Checks);R->SetStringField(TEXT("map"),GetWorld()->GetMapName());R->SetStringField(TEXT("scope"),TEXT("Real D3D12/Slate/input. Fixture portion: invulnerable player, passive targets, paused director. Then up to 60 seconds production director/enemies/health with automated aim/input. No full 30-wave run claim."));
    if(Pet){R->SetNumberField(TEXT("pet_shots"),Pet->ShotsFired);R->SetNumberField(TEXT("pet_reloads"),Pet->Reloads);R->SetNumberField(TEXT("pet_kills"),Pet->TrueKills);R->SetNumberField(TEXT("pet_scale"),Pet->GetMesh()->GetRelativeScale3D().Z);R->SetStringField(TEXT("pet_muzzle"),Pet->MuzzleLocation().ToString());}
    R->SetNumberField(TEXT("production_enemy_observations"),NormalEnemiesSeen);R->SetNumberField(TEXT("production_seconds"),Stage==22?Age:0);R->SetNumberField(TEXT("player_z_after_production"),Player?Player->GetActorLocation().Z:0);
    R->SetNumberField(TEXT("player_fixture_defeats"),Kills);R->SetNumberField(TEXT("unique_player_fixture_voice_indices"),VoiceIndices.Num());
    R->SetArrayField(TEXT("reload_effect_cases"),ReloadCases);
    if(FParse::Param(FCommandLine::Get(),TEXT("KuiQAReloadEffects")))R->SetStringField(TEXT("scope"),TEXT("Real D3D12/Slate/input regression fixture plus six reload-effect cases: normal/slug, 2.3/1.15/4.6 seconds, stationary/moving with rapid camera turns. Temporary in-memory weapon definitions; source materials/recipes unchanged. Hidden original world-anchored drum effect is a negative control. No production-wave or packaged-build claim."));
    if(FParse::Param(FCommandLine::Get(),TEXT("KuiQASpatialOnly")))R->SetStringField(TEXT("scope"),TEXT("Dedicated spatial voice probe: real menu/input, passive enemy, close camera within 3 metres of companion, actual companion lethal damage and recorded spatial voice. No player voice or production combat claims from this probe."));
    TArray<TSharedPtr<FJsonValue>> Times;for(double F:Frames)Times.Add(MakeShared<FJsonValueNumber>(F));R->SetArrayField(TEXT("frame_seconds"),Times);
    FString Text;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Out/TEXT("report.json")));Next=FPlatformTime::Seconds()+3;Stage=99;
}
void AHeliosWangZheKuiThunderQA::Tick(float Dt)
{
    Super::Tick(Dt);const double Wall=FPlatformTime::Seconds();if(Stage==99){if(Wall>=Next)FPlatformMisc::RequestExit(false);return;}
    if(Started==0){PreviousUnfocusedVolume=FApp::GetUnfocusedVolumeMultiplier();FApp::SetUnfocusedVolumeMultiplier(1);Started=Wall;FParse::Value(FCommandLine::Get(),TEXT("KuiQAOut="),Out);if(Out.IsEmpty())Out=FPaths::ProjectDir()/TEXT("../qa/cfm_wangzhekui_thunder/runtime");Out=FPaths::ConvertRelativePathToFull(Out);IFileManager::Get().MakeDirectory(*Out,true);}
    if(Wall-Started>300){Check(TEXT("bounded_timeout"),false);Finish();return;}
    auto* PC=UGameplayStatics::GetPlayerController(this,0);auto* H=PC?Cast<AHeliosAdventureHUD>(PC->GetHUD()):nullptr;auto* D=AHeliosAdventureDirector::Find(GetWorld());Player=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));if(!H||!D||!Player||Wall-Started<5)return;
    auto Advance=[&](int32 S){Stage=S;Age=0;Next=Wall+.4;Captures.Reset();};Age+=Dt;if(Stage>5)Frames.Add(Dt);
    const bool Dense=(Stage==7&&Age<1)||(Stage==81&&Age<.4f)||(Stage==11&&Age<2)||Stage==44;
    if(Stage>=6&&Stage<99&&Wall-LastMotion>=(Dense?.01:.1)){LastMotion=Wall;HeliosAsyncFrameCapture::Request(Out/FString::Printf(TEXT("motion_%05d.jpg"),MotionFrame++));}
    auto TimedCapture=[&](int32 I,float T,const TCHAR* N){if(Age>=T&&!Captures.Contains(I)){Captures.Add(I);Capture(N);}};
    if((Stage==11||Stage==12)&&IsValid(Camera)&&IsValid(Pet))
    {
        FVector Offset(550,-380,100);
        if(Stage==12&&FParse::Param(FCommandLine::Get(),TEXT("KuiQASpatialOnly")))Offset=FVector(200,-100,65);
        if(Stage==11&&Age>=3.5f&&Age<4.5f)Offset=Pet->GetActorForwardVector()*500+FVector(0,0,80);
        else if(Stage==11&&Age>=4.5f&&Age<5.5f)Offset=Pet->GetActorRightVector()*500+FVector(0,0,80);
        else if(Stage==11&&Age>=5.5f&&Age<6.5f)Offset=-Pet->GetActorForwardVector()*500+FVector(0,0,80);
        Camera->SetActorLocation(Pet->GetActorLocation()+Offset);Camera->SetActorRotation((Pet->GetActorLocation()+FVector(0,0,-5)-Camera->GetActorLocation()).Rotation());
    }
    // Release the fixture trigger before the generic transition delay. Holding
    // it through that delay fires twice and exhausts a magazine before 11 kills.
    if((Stage==81||Stage==43)&&Age>.1f)Key(EKeys::LeftMouseButton,false);
    // Let newly spawned passive target collision and the camera settle before
    // the first shot; this QA actor observes effects after bone/physics updates.
    if(Stage==9&&Age>=.2f&&!Captures.Contains(8)){Captures.Add(8);Key(EKeys::LeftMouseButton,true);}
    if(Stage==9&&Age>=.3f)Key(EKeys::LeftMouseButton,false);
    if(Stage==81)TimedCapture(1,.02f,TEXT("06b_slug_fire"));
    if(Stage==80&&Age>.4f&&!Captures.Contains(2)){Captures.Add(2);auto* K=Cast<AHeliosWangZheKuiThunderWeapon>(Player->GetEquippedWeapon());Check(TEXT("mode_switch_pending"),K&&K->IsSwitchingMode()&&!K->IsSlugMode());}
    if(Wall<Next)return;
    auto* W=Cast<AHeliosWangZheKuiThunderWeapon>(Player->GetEquippedWeapon());
    switch(Stage)
    {
    case 0:
#if WITH_DEV_AUTOMATION_TESTS
        if(HeliosMenuQANavigate)HeliosMenuQANavigate(true,false);
#endif
        Advance(1);return;
    case 1:Scroll(TEXT("王者葵·保卫者 / 王者之霆"));Advance(2);return;
    case 2:Check(TEXT("actual_weapon_button_clicked"),Click(TEXT("王者葵·保卫者 / 王者之霆"),true));Check(TEXT("selected_new_weapon"),H->GetPendingWeaponId()==TEXT("cfm_wangzhekui_thunder"));Capture(TEXT("01_weapon_menu"));Advance(3);return;
    case 3:
        H->SelectPetCount(1);H->SelectPetSlot(0);
#if WITH_DEV_AUTOMATION_TESTS
        if(HeliosMenuQANavigate)HeliosMenuQANavigate(true,true);
#endif
        Check(TEXT("actual_partner_tab_clicked"),Click(TEXT("AI 伙伴")));Advance(4);return;
    case 4:Scroll(TEXT("王者葵·保卫者 / 王者之霆"));Advance(5);return;
    case 5:Check(TEXT("actual_pet_button_clicked"),Click(TEXT("王者葵·保卫者 / 王者之霆"),true));Check(TEXT("independent_pet_selection"),H->GetSelectedPet()==EHeliosPetKind::WangZheKuiThunder&&H->GetPendingWeaponId()==TEXT("cfm_wangzhekui_thunder"));Capture(TEXT("02_pet_menu"));Advance(51);return;
    case 51: {auto* Def=UHeliosWeaponRegistry::FindWeapon(TEXT("cfm_wangzhekui_thunder"));UE_LOG(LogTemp,Display,TEXT("KUI_START state=%d map=%s weapon=%s class=%s hasmodel=%d"),int32(H->GetMenuState()),*H->GetPendingMapId().ToString(),*H->GetPendingWeaponId().ToString(),*GetNameSafe(Def?Def->WeaponClass.Get():nullptr),Def&&Def->HasModel());}H->StartSelectedRun();UE_LOG(LogTemp,Display,TEXT("KUI_START afterstate=%d equipped=%s"),int32(H->GetMenuState()),*GetNameSafe(Player->GetEquippedWeapon()));D->SetActorTickEnabled(false);D->CleanupOwnedActors();Player->SetCanBeDamaged(false);Player->SetActorLocation(FVector(0,-2800,102),false,nullptr,ETeleportType::TeleportPhysics);PC->SetControlRotation(FRotator(0,90,0));Advance(6);return;
    case 6:
        if(Age<3)return;Pet=Cast<AHeliosWangZheKuiThunderPet>(GetWorld()->GetSubsystem<UHeliosPetWorld>()->Pet);Check(TEXT("selected_pet_spawned"),Pet&&Pet->bModelReady);Check(TEXT("selected_weapon_spawned"),W!=nullptr);if(!W||!Pet){Finish();return;}
        Check(TEXT("source_arms_and_gun_loaded"),W->Thunder->GetSkeletalMeshAsset()&&W->GetFirstPersonMesh()->GetSkeletalMeshAsset());
        Check(TEXT("verified_wangzhekui_arms_and_body_materials"),WangZheKuiMaterials(W->GetFirstPersonMesh())&&WangZheKuiMaterials(Pet->GetMesh()));
        Check(TEXT("no_hunter07_component"),W->FindComponentByClass<UHeliosHunterDamageFeedback>()==nullptr);Check(TEXT("eleven_original_kill_lines"),W->Voice->GetKillPoolSize()==11&&Pet->Voice->GetKillPoolSize()==11);Pet->SetActorTickEnabled(false);SpawnTarget(100000);UAudioMixerBlueprintLibrary::StartRecordingOutput(this,240);bRecording=true;Capture(TEXT("03_bright_idle"));
        if(FParse::Param(FCommandLine::Get(),TEXT("KuiQAReloadEffects")))for(TActorIterator<AHeliosSSXEffect> It(GetWorld());It;++It)if(It->GetOwner()==W)AddTickPrerequisiteActor(*It);
        if(FParse::Param(FCommandLine::Get(),TEXT("KuiQASpatialOnly"))){if(IsValid(Target))Target->Destroy();Pet->SetActorTickEnabled(true);PetStart=Pet->GetActorLocation();Player->SetActorLocation(Player->GetActorLocation()+FVector(0,2000,0),false,nullptr,ETeleportType::TeleportPhysics);Advance(10);return;}
        Key(EKeys::LeftMouseButton,true);Advance(7);return;
    case 7:
        TimedCapture(1,.65f,TEXT("04_bright_fire"));if(Age<6.1)return;Key(EKeys::LeftMouseButton,false);Check(TEXT("continuous_fire_magazine"),W&&W->ShotsFired==W->ShotSettings.Magazine&&W->GetBulletCount()==0);Check(TEXT("nonlethal_damage_no_defeat_voice"),W&&W->TrueKills==0&&W->Voice->KillCount==0&&W->HitPellets>0);Key(EKeys::R,true);Key(EKeys::R,false);Advance(8);return;
    case 8:
        TimedCapture(1,.45f,TEXT("05_reload_out"));TimedCapture(2,1.0f,TEXT("06_reload_in"));if(Age<2.4)return;Check(TEXT("reload_completed"),W&&W->GetBulletCount()==W->ShotSettings.Magazine&&!Player->IsReloading());
        SlugStartAmmo=W->GetBulletCount();Key(EKeys::RightMouseButton,true);Key(EKeys::RightMouseButton,false);Advance(80);return;
    case 80:
        TimedCapture(1,.6f,TEXT("06a_enter_slug"));if(Age<1.6)return;
        Check(TEXT("slug_switch_completed_shared_ammo"),W&&W->IsSlugMode()&&!W->IsSwitchingMode()&&W->GetBulletCount()==SlugStartAmmo&&W->ShotSettings.Pellets==1&&W->ShotSettings.Range==6000);
        SlugStartShots=W->ShotsFired;Key(EKeys::LeftMouseButton,true);Advance(81);return;
    case 81:
        TimedCapture(1,.05f,TEXT("06b_slug_fire"));if(Age<.6f)return;
        Check(TEXT("slug_consumes_one_shared_round"),W&&W->ShotsFired==SlugStartShots+1&&W->GetBulletCount()==SlugStartAmmo-1);
        Key(EKeys::RightMouseButton,true);Key(EKeys::RightMouseButton,false);Key(EKeys::R,true);Key(EKeys::R,false);Check(TEXT("reload_rejected_during_mode_switch"),!Player->IsReloading());Advance(82);return;
    case 82:
        TimedCapture(1,.6f,TEXT("06c_return_to_pellets"));if(Age<1.6)return;
        Check(TEXT("normal_mode_restored_without_ammo_reset"),W&&!W->IsSlugMode()&&W->ShotSettings.Pellets==10&&W->GetBulletCount()==SlugStartAmmo-1);
        Key(EKeys::R,true);Key(EKeys::R,false);Advance(83);return;
    case 83:
        if(Age<2.6f)return;Check(TEXT("shared_magazine_reload_after_switch"),W&&W->GetBulletCount()==25&&!Player->IsReloading());SpawnTarget(1);Advance(9);return;
    case 9:
        if(Age<3.35f)return;if(W)VoiceIndices.Add(W->Voice->GetLastPlayedKillIndex());++Kills;
        {const int32 RequestedKills=FParse::Param(FCommandLine::Get(),TEXT("KuiQAAllVoices"))?11:4;
        if(Kills<RequestedKills){SpawnTarget(1);Advance(9);return;}Check(TEXT("one_defeat_per_real_enemy"),W&&W->TrueKills==RequestedKills);Check(TEXT("random_nonrepeating_defeat_lines"),RequestedKills==11?VoiceIndices.Num()==11:VoiceIndices.Num()>=3);}
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
                FCollisionQueryParams Q(SCENE_QUERY_STAT(KuiQAClearFixture),false,Pet);Q.AddIgnoredActor(Player);Q.AddIgnoredActor(Target);FHitResult Hit;
                if(GetWorld()->LineTraceSingleByChannel(Hit,Pet->GetActorLocation()+FVector(0,0,35),At,ECC_Visibility,Q))continue;
                Target->SetActorLocation(At,false,nullptr,ETeleportType::TeleportPhysics);Placed=true;
            }
            Check(TEXT("partner_fixture_has_navigable_clear_sight"),Placed);
        }
        Camera=GetWorld()->SpawnActor<ACameraActor>();if(Pet){Camera->SetActorLocation(Pet->GetActorLocation()+FVector(550,-380,100));Camera->SetActorRotation((Pet->GetActorLocation()+FVector(0,0,-5)-Camera->GetActorLocation()).Rotation());Camera->GetCameraComponent()->FieldOfView=55;PC->SetViewTarget(Camera);}Advance(11);return;
    case 11:
        TimedCapture(1,3,TEXT("08_pet_holding_fire"));TimedCapture(2,8,TEXT("09_pet_reload"));
        TimedCapture(3,4,TEXT("08a_pet_front"));TimedCapture(4,5,TEXT("08b_pet_side"));TimedCapture(5,6,TEXT("08c_pet_back"));
        if(Age<11)return;Check(TEXT("pet_fires_and_reloads"),Pet&&Pet->ShotsFired>=13&&Pet->Reloads>=1);Check(TEXT("gun_attached_to_source_weapon_bone"),Pet&&Pet->Thunder->GetAttachSocketName()==TEXT("b_RightWeapon"));
        if(Pet){const float Height=Pet->GetMesh()->GetSkeletalMeshAsset()->GetBounds().BoxExtent.Z*2*Pet->GetMesh()->GetRelativeScale3D().Z;Check(TEXT("height_matches_ssx_203cm"),FMath::Abs(Height-203.3593f)<.5f);UE_LOG(LogTemp,Display,TEXT("KUI_QA_PET height=%.4f bounds_minz=%.4f capsule_bottom=%.4f right_weapon=%s muzzle=%s"),Height,Pet->GetMesh()->Bounds.Origin.Z-Pet->GetMesh()->Bounds.BoxExtent.Z,Pet->GetActorLocation().Z-Pet->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),*Pet->GetMesh()->GetSocketLocation(TEXT("b_RightWeapon")).ToCompactString(),*Pet->MuzzleLocation().ToCompactString());}
        if(IsValid(Target)&&!FParse::Param(FCommandLine::Get(),TEXT("KuiQASpatialOnly")))Target->CurrentHP=1;BeforeShots=Pet?Pet->ShotsFired:0;Advance(12);return;
    case 12:
        if(FParse::Param(FCommandLine::Get(),TEXT("KuiQASpatialOnly")))
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
        TimedCapture(1,.65f,TEXT("12_show"));TimedCapture(2,3.1f,TEXT("12a_show_press"));if(Age<4.8)return;Check(TEXT("same_weapon_still_active_after_show"),W&&Player->GetWeaponId()==TEXT("cfm_wangzhekui_thunder"));Check(TEXT("switch_to_old_rifle"),Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("rifle"))));Advance(16);return;
    case 16:
        if(Age<.7f)return;Check(TEXT("no_effects_after_switch"),NoInactiveKuiEffects(Player));Check(TEXT("old_rifle_actual_equipped"),Player->GetWeaponId()==TEXT("rifle"));Check(TEXT("switch_back_to_chan"),Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("cfm_wangzhekui_thunder"))));Advance(17);return;
    case 17:
        if(Age<1.2f)return;SpawnTarget(100000);Key(EKeys::LeftMouseButton,true);Advance(18);return;
    case 18:
        TimedCapture(1,.05f,TEXT("13_after_switch_fire"));if(Age<.6f)return;Key(EKeys::LeftMouseButton,false);Check(TEXT("fires_after_equipment_switch"),W&&W->ShotsFired>=1);Key(EKeys::R,true);Key(EKeys::R,false);Advance(19);return;
    case 19:
        if(Age<.5f)return;Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("rifle")));Check(TEXT("switch_during_reload"),!Player->IsReloading()&&Player->GetWeaponId()==TEXT("rifle"));Advance(20);return;
    case 20:
        if(Age<.5f)return;Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("cfm_wangzhekui_thunder")));Advance(21);return;
    case 21:
        if(Age<1.2f)return;Check(TEXT("new_weapon_recovers_after_cancelled_reload"),W&&W->GetBulletCount()==W->ShotSettings.Magazine&&!Player->IsReloading());Check(TEXT("reequip_defaults_to_normal"),W&&!W->IsSlugMode());
        if(IsValid(Target))Target->Destroy();Pet->SetActorTickEnabled(true);Pet->TakeDamage(100000,FDamageEvent(),nullptr,nullptr);
        Check(TEXT("actual_damage_knocks_down_partner"),Pet->IsSleeping()&&Pet->Health==0);Check(TEXT("down_clears_gun_effects"),OwnedEffects(GetWorld(),Pet)==0);
        Camera->SetActorLocation(Pet->GetActorLocation()+FVector(250,-200,100));Camera->SetActorRotation((Pet->GetActorLocation()-Camera->GetActorLocation()).Rotation());PC->SetViewTarget(Camera);Advance(23);return;
    case 23:
        TimedCapture(1,1.5f,TEXT("16_partner_down"));TimedCapture(2,31.5f,TEXT("17_partner_recovered"));if(Age<32)return;
        Check(TEXT("default_30_second_recovery"),Pet&&!Pet->IsSleeping()&&Pet->Health==Pet->Settings.MaxHealth&&Pet->State!=EHeliosPetState::Waking);
        PC->SetViewTarget(Player);Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("cfm_chan_zeus")));Advance(24);return;
    case 24:
        if(Age<1.5f)return;Check(TEXT("chan_zeus_still_equips"),Cast<AHeliosChanZeusWeapon>(Player->GetEquippedWeapon())!=nullptr);SpawnTarget(100000);Key(EKeys::LeftMouseButton,true);Advance(25);return;
    case 25:
        Key(EKeys::LeftMouseButton,false);{auto* C=Cast<AHeliosChanZeusWeapon>(Player->GetEquippedWeapon());Check(TEXT("chan_zeus_still_fires_and_hits"),C&&C->ShotsFired>0&&C->HitPellets>0);}
        if(IsValid(Target))Target->Destroy();Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("cfm_wangzhekui_thunder")));
        {auto* S=GetGameInstance()->GetSubsystem<UHeliosPetSession>();auto* PW=GetWorld()->GetSubsystem<UHeliosPetWorld>();PW->Clear(false);S->SetCount(4);S->SelectSlot(0,EHeliosPetKind::WangZheKuiThunder);S->SelectSlot(1,EHeliosPetKind::ChanZeus);S->SelectSlot(2,EHeliosPetKind::WangZheKuiThunder);S->SelectSlot(3,EHeliosPetKind::SwordDog);S->ResetSnapshots();}Advance(26);return;
    case 26:
        if(Age<3)return;{auto* PW=GetWorld()->GetSubsystem<UHeliosPetWorld>();Check(TEXT("mixed_roster_spawns_four"),PW->Pets.Num()==4);if(PW->Pets.Num()!=4){Finish();return;}Pet=Cast<AHeliosWangZheKuiThunderPet>(PW->Pets[0]);Check(TEXT("mixed_roster_keeps_order_and_duplicate_kui"),Pet&&Cast<AHeliosChanZeusPet>(PW->Pets[1])&&Cast<AHeliosWangZheKuiThunderPet>(PW->Pets[2]));}
        SpawnTarget(100000);Camera->SetActorLocation(Player->GetActorLocation()+FVector(650,-800,220));Camera->SetActorRotation((Player->GetActorLocation()-Camera->GetActorLocation()).Rotation());PC->SetViewTarget(Camera);Advance(27);return;
    case 27:
        TimedCapture(1,5,TEXT("18_mixed_partners"));if(Age<12)return;
        {auto* PW=GetWorld()->GetSubsystem<UHeliosPetWorld>();bool Healthy=true;int32 KuiShots=0,ChanShots=0;for(auto& A:PW->Pets){Healthy&=A&&A->Health==A->Settings.MaxHealth;if(auto* K=Cast<AHeliosWangZheKuiThunderPet>(A))KuiShots+=K->ShotsFired;if(auto* C=Cast<AHeliosChanZeusPet>(A))ChanShots+=C->ShotsFired;}Check(TEXT("mixed_roster_no_friendly_damage"),Healthy);Check(TEXT("mixed_kui_and_chan_both_fire"),KuiShots>0&&ChanShots>0);}
        PC->SetViewTarget(Player);if(IsValid(Target))Target->Destroy();
        Advance(28);return;
    case 28:
        if(Age<1.2f)return;W->StartSecondaryFire();Check(TEXT("mode_started_before_equipment_cancel"),W->IsSwitchingMode());Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("rifle")));Advance(29);return;
    case 29:
        Check(TEXT("cancelled_mode_leaves_no_effects"),NoInactiveKuiEffects(Player));Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("cfm_wangzhekui_thunder")));Advance(30);return;
    case 30:
        if(Age<1.3f)return;Check(TEXT("cancelled_mode_requip_normal"),W&&!W->IsSlugMode()&&W->GetBulletCount()==25);Player->SetCanBeDamaged(true);Player->TakeDamage(10,FDamageEvent(),nullptr,nullptr);Check(TEXT("player_real_hurt_uses_original_voice"),W&&W->Voice->IsVoicePlaying());Player->SetCanBeDamaged(false);
        Probe=HeliosWangZheKuiThunder::Effect(W,TEXT("FX_1P_Gunfire_AA12_LeiTing_Gold"),W->MuzzleLocation(),PC->GetControlRotation(),true);if(Probe){Probe->Tick(.025f);Probe->SetActorTickEnabled(false);}Advance(31);return;
    case 31:
        TimedCapture(1,.4f,TEXT("19_normal_fx_source_25ms_probe"));if(Age<.7f)return;Check(TEXT("normal_source_probe_visible_tracks"),Probe&&Probe->VisibleTrackCount()>0);if(Probe)Probe->Destroy();Probe=HeliosWangZheKuiThunder::Effect(W,TEXT("FX_1P_Gunfire_AA12_LeiTing_Gold_Bio"),W->MuzzleLocation(),PC->GetControlRotation(),true);if(Probe){Probe->Tick(.025f);Probe->SetActorTickEnabled(false);}Advance(32);return;
    case 32:
        TimedCapture(1,.4f,TEXT("20_slug_fx_source_25ms_probe"));if(Age<.7f)return;Check(TEXT("slug_source_probe_visible_tracks"),Probe&&Probe->VisibleTrackCount()>0);if(Probe)Probe->Destroy();
        if(FParse::Param(FCommandLine::Get(),TEXT("KuiQAReloadEffects"))){Probe=nullptr;auto* PW=GetWorld()->GetSubsystem<UHeliosPetWorld>();for(auto& A:PW->Pets)if(A)A->SetActorTickEnabled(false);ReloadCase=0;Advance(40);return;}
        if(FParse::Param(FCommandLine::Get(),TEXT("KuiQAQuick"))){Finish();return;}Player->SetCanBeDamaged(true);D->SetActorTickEnabled(true);Advance(22);return;
    case 40:
    {
        Key(EKeys::W,false);Key(EKeys::LeftMouseButton,false);
        Player->GetCharacterMovement()->StopMovementImmediately();Player->SetActorLocation(FVector(0,-2800,102),false,nullptr,ETeleportType::TeleportPhysics);PC->SetControlRotation(FRotator(0,90,0));
        ReloadTestSeconds=ReloadCase<2?2.3f:ReloadCase<4?1.15f:4.6f;
        bReloadMoving=ReloadCase==1||ReloadCase>=4;
        auto* Definition=DuplicateObject<UHeliosWeaponDefinition>(UHeliosWeaponRegistry::FindWeapon(TEXT("cfm_wangzhekui_thunder")),this);Definition->ReloadSeconds=ReloadTestSeconds;
        Check(*FString::Printf(TEXT("reload_case_%d_temporary_weapon_equipped"),ReloadCase),Player->EquipWeaponDefinition(Definition));
        Advance(41);return;
    }
    case 41:
        if(Age<1.3f)return;
        if(ReloadCase%2){Key(EKeys::RightMouseButton,true);Key(EKeys::RightMouseButton,false);Advance(42);return;}
        Key(EKeys::LeftMouseButton,true);Advance(43);return;
    case 42:
        if(Age<1.55f)return;Check(*FString::Printf(TEXT("reload_case_%d_slug_mode"),ReloadCase),W&&W->IsSlugMode());Key(EKeys::LeftMouseButton,true);Advance(43);return;
    case 43:
    {
        Key(EKeys::LeftMouseButton,false);if(!W){Check(TEXT("reload_case_weapon_available"),false);Finish();return;}
        Check(*FString::Printf(TEXT("reload_case_%d_has_spent_round"),ReloadCase),W->GetBulletCount()<W->ShotSettings.Magazine);
        // Hidden negative control reproduces the original frozen world anchor.
        Probe=HeliosWangZheKuiThunder::AttachedEffect(W,W->Thunder,TEXT("FX_1P_AA12_LeiTing_Gold_EffectSocket_DanJia_Idle_FP"),true,.01f,TEXT("EffectSocket_DanJia"));if(Probe)Probe->SetActorHiddenInGame(true);
        for(TActorIterator<AHeliosSSXEffect> It(GetWorld());It;++It)if(It->GetOwner()==W)AddTickPrerequisiteActor(*It);
        ReloadSamples=ReloadGeometrySamples=0;ReloadFirstVisible=ReloadLastVisible=-1;ReloadPositionError=ReloadRotationError=LegacyPositionError=ReloadFrameTolerance=0; bReloadAttachmentsValid=true;ReloadStartLocation=Player->GetActorLocation();
        Key(EKeys::R,true);Key(EKeys::R,false);
        Advance(44);Next=Wall;if(bReloadMoving)Key(EKeys::W,true);return;
    }
    case 44:
    {
        ReloadFrameTolerance=FMath::Max(ReloadFrameTolerance,2*Dt+.01f);SampleReloadEffects(W);
        if(Age>=.05f&&!Captures.Contains(4)){Captures.Add(4);Check(*FString::Printf(TEXT("reload_case_%d_real_input_started"),ReloadCase),Player->IsReloading());}
        if(bReloadMoving)PC->SetControlRotation(FRotator(12*FMath::Sin(Age*5),90+55*FMath::Sin(Age*7),0));
        TimedCapture(1,ReloadTestSeconds*.45f/2.3f,*FString::Printf(TEXT("21_reload_%d_out"),ReloadCase));
        TimedCapture(2,ReloadTestSeconds/2.3f,*FString::Printf(TEXT("22_reload_%d_in"),ReloadCase));
        TimedCapture(3,ReloadTestSeconds*1.55f/2.3f,*FString::Printf(TEXT("23_reload_%d_bolt"),ReloadCase));
        if(Age<ReloadTestSeconds+.15f)return;
        Key(EKeys::W,false);Player->GetCharacterMovement()->StopMovementImmediately();
        const float Moved=FVector::Dist2D(ReloadStartLocation,Player->GetActorLocation());
        Check(*FString::Printf(TEXT("reload_case_%d_completed"),ReloadCase),W&&!Player->IsReloading()&&W->GetBulletCount()==W->ShotSettings.Magazine);
        Check(*FString::Printf(TEXT("reload_case_%d_bone_geometry_follows"),ReloadCase),ReloadSamples>10&&ReloadGeometrySamples>10&&ReloadPositionError<.1f&&ReloadRotationError<.1f&&bReloadAttachmentsValid);
        Check(*FString::Printf(TEXT("reload_case_%d_old_anchor_reproduces_drift"),ReloadCase),LegacyPositionError>5);
        const float Onset=.18f*ReloadTestSeconds/2.3f,End=2.1426568918f*ReloadTestSeconds/2.3f;
        Check(*FString::Printf(TEXT("reload_case_%d_source_particle_timing"),ReloadCase),ReloadFirstVisible>=0&&FMath::Abs(ReloadFirstVisible-Onset)<=ReloadFrameTolerance&&FMath::Abs(ReloadLastVisible-End)<=ReloadFrameTolerance);
        Check(*FString::Printf(TEXT("reload_case_%d_movement"),ReloadCase),bReloadMoving?Moved>20:Moved<5);
        int32 Loops=0,VisibleTransientTracks=0;
        for(TActorIterator<AHeliosSSXEffect> It(GetWorld());It;++It)if(It->GetOwner()==W&&*It!=Probe){if(It->IsLooping())++Loops;else VisibleTransientTracks+=It->VisibleTrackCount();}
        Check(*FString::Printf(TEXT("reload_case_%d_only_two_persistent_effects_remain"),ReloadCase),Loops==2&&VisibleTransientTracks==0);
        auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("case"),ReloadCase);R->SetNumberField(TEXT("seconds"),ReloadTestSeconds);R->SetBoolField(TEXT("slug"),ReloadCase%2!=0);R->SetBoolField(TEXT("moving_and_turning"),bReloadMoving);
        R->SetNumberField(TEXT("samples"),ReloadSamples);R->SetNumberField(TEXT("geometry_samples"),ReloadGeometrySamples);R->SetNumberField(TEXT("position_error_cm"),ReloadPositionError);R->SetNumberField(TEXT("rotation_error_degrees"),ReloadRotationError);R->SetNumberField(TEXT("legacy_position_error_cm"),LegacyPositionError);R->SetNumberField(TEXT("movement_cm"),Moved);
        R->SetNumberField(TEXT("first_visible_seconds"),ReloadFirstVisible);R->SetNumberField(TEXT("last_visible_seconds"),ReloadLastVisible);R->SetNumberField(TEXT("timing_tolerance_seconds"),ReloadFrameTolerance);ReloadCases.Add(MakeShared<FJsonValueObject>(R));
        if(Probe)Probe->Destroy();Probe=nullptr;PC->SetControlRotation(FRotator(0,90,0));Capture(*FString::Printf(TEXT("24_reload_%d_finished"),ReloadCase));
        if(++ReloadCase<6){Advance(40);return;}
        Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("rifle")));Advance(45);return;
    }
    case 45:
        Check(TEXT("reload_suite_switch_leaves_no_kui_effects"),NoInactiveKuiEffects(Player));
        Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("cfm_wangzhekui_thunder")));Advance(46);return;
    case 46:
        if(Age<1.3f)return;Check(TEXT("reload_suite_default_definition_restored"),W&&!W->IsSlugMode()&&W->GetBulletCount()==25);Finish();return;
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
