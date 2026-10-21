#include "QA/HeliosBuLianShiP90QA.h"
#include "Weapons/HeliosBuLianShiP90.h"
#include "Weapons/HeliosP90SubstituteFireAudio.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundAttenuation.h"
#include "Engine/DamageEvents.h"
#include "Weapons/HeliosSSXVoice.h"
#include "Pets/HeliosBuLianShiP90Pet.h"
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
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#if WITH_EDITOR
#include "Animation/Skeleton.h"
#include "ReferenceSkeleton.h"
#include "UObject/SavePackage.h"
#endif
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
#include "Weapons/HeliosSSXEffect.h"
#include "Weapons/HeliosBuLianShiP90Effect.h"
#include "QA/HeliosAsyncFrameCapture.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif
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
bool BuLianShiMaterials(USkeletalMeshComponent* Mesh)
{
    if(!Mesh||Mesh->GetNumMaterials()==0)return false;
    for(int32 I=0;I<Mesh->GetNumMaterials();++I)
    {const auto* M=Mesh->GetMaterial(I);if(!M||!M->GetPathName().Contains(TEXT("FamilySP_BuLianShi")))return false;}
    return true;
}
}
AHeliosBuLianShiP90QA::AHeliosBuLianShiP90QA(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;}
void AHeliosBuLianShiP90QA::Check(const TCHAR* Name,bool Pass)
{auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("name"),Name);R->SetBoolField(TEXT("passed"),Pass);Checks.Add(MakeShared<FJsonValueObject>(R));if(!Pass)++Failures;UE_LOG(LogTemp,Display,TEXT("BLS_QA %s %s"),Name,Pass?TEXT("PASS"):TEXT("FAIL"));}
void AHeliosBuLianShiP90QA::Capture(const TCHAR* Name){FScreenshotRequest::RequestScreenshot(Out/(FString(Name)+TEXT(".png")),true,false);}
void AHeliosBuLianShiP90QA::Key(FKey K,bool Down)
{if(auto* PC=UGameplayStatics::GetPlayerController(this,0))PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),K,Down?IE_Pressed:IE_Released,FPlatformTime::Cycles64()));}
AHeliosEnemyActor* AHeliosBuLianShiP90QA::SpawnTarget(float HP)
{
    if(IsValid(Target))Target->Destroy();FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Player->GetController()->SetControlRotation(FRotator(0,90,0));
    Target=GetWorld()->SpawnActor<AHeliosEnemyE01>(Player->GetActorLocation()+FVector(0,650,0),FRotator(0,-90,0),P);
    if(Target){Target->CurrentHP=HP;Target->SetActorTickEnabled(false);Target->StopShooting();Target->GetCharacterMovement()->StopMovementImmediately();Target->GetCharacterMovement()->DisableMovement();if(auto* AI=Cast<AAIController>(Target->GetController())){AI->StopMovement();if(AI->GetBrainComponent())AI->GetBrainComponent()->StopLogic(TEXT("bounded Chan QA target"));}}
    return Target;
}
void AHeliosBuLianShiP90QA::MarkAudio(const TCHAR* Name)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("BLSQAAudio")))return;
    auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("name"),Name);R->SetNumberField(TEXT("seconds"),FPlatformTime::Seconds()-AudioStartWall);
    if(auto* W=Player?Cast<AHeliosBuLianShiP90Weapon>(Player->GetEquippedWeapon()):nullptr){R->SetNumberField(TEXT("player_shots"),W->ShotsFired);R->SetNumberField(TEXT("player_sound_triggers"),W->FireAudio->TriggeredShots);R->SetNumberField(TEXT("player_playing"),W->FireAudio->PlayingCount());}
    if(IsValid(Pet)){R->SetNumberField(TEXT("pet_shots"),Pet->ShotsFired);R->SetNumberField(TEXT("pet_sound_triggers"),Pet->FireAudio->TriggeredShots);R->SetNumberField(TEXT("pet_playing"),Pet->FireAudio->PlayingCount());}
    AudioMarkers.Add(MakeShared<FJsonValueObject>(R));UE_LOG(LogTemp,Display,TEXT("BLS_QA_AUDIO_MARK name=%s seconds=%.6f"),Name,FPlatformTime::Seconds()-AudioStartWall);
}
int32 AHeliosBuLianShiP90QA::AudioTailCount()const
{int32 Count=0;for(const auto& C:AudioTails)if(C.IsValid()&&C->IsPlaying())++Count;return Count;}
void AHeliosBuLianShiP90QA::Finish()
{
    if(bFinished)return;bFinished=true;Key(EKeys::LeftMouseButton,false);Key(EKeys::W,false);
    if(bRecording)UAudioMixerBlueprintLibrary::StopRecordingOutput(this,EAudioRecordingExportType::WavFile,TEXT("runtime_audio"),Out);
    if(FParse::Param(FCommandLine::Get(),TEXT("BLSQAFXRepair")))HeliosAsyncFrameCapture::Finish(Out);
    FApp::SetUnfocusedVolumeMultiplier(PreviousUnfocusedVolume);
    if(auto* PC=UGameplayStatics::GetPlayerController(this,0))PC->ClearAudioListenerOverride();
    auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("passed"),Failures==0);R->SetArrayField(TEXT("checks"),Checks);R->SetStringField(TEXT("map"),GetWorld()->GetMapName());R->SetStringField(TEXT("scope"),TEXT("Real D3D12/Slate/input. Fixture portion: invulnerable player, passive targets, paused director. Then up to 60 seconds production director/enemies/health with automated aim/input. No full 30-wave run claim."));
    if(Pet){R->SetNumberField(TEXT("pet_shots"),Pet->ShotsFired);R->SetNumberField(TEXT("pet_reloads"),Pet->Reloads);R->SetNumberField(TEXT("pet_kills"),Pet->TrueKills);R->SetNumberField(TEXT("pet_scale"),Pet->GetMesh()->GetRelativeScale3D().Z);R->SetStringField(TEXT("pet_muzzle"),Pet->MuzzleLocation().ToString());}
    R->SetNumberField(TEXT("production_enemy_observations"),NormalEnemiesSeen);R->SetNumberField(TEXT("production_seconds"),Stage==22?Age:0);R->SetNumberField(TEXT("player_z_after_production"),Player?Player->GetActorLocation().Z:0);
    R->SetNumberField(TEXT("player_fixture_defeats"),Kills);R->SetNumberField(TEXT("unique_player_fixture_voice_indices"),VoiceIndices.Num());
    if(FParse::Param(FCommandLine::Get(),TEXT("BLSQAAudio")))
    {R->SetArrayField(TEXT("audio_markers"),AudioMarkers);R->SetStringField(TEXT("scope"),TEXT("Approved ordinary-P90 substitute audio probe. Real D3D12/menu/input, 50-round player magazine, one-shot/release/reload/switch/death tails, actual partner attack/2m-20m-40m listener recording/down/destruction, original defeat voice coexistence. Passive fixture targets, invulnerable player until the final real fatal damage. No production-wave or full voice listening audit claim."));}
    if(FParse::Param(FCommandLine::Get(),TEXT("BLSQAFXRepair")))
    {
        R->SetStringField(TEXT("scope"),TEXT("Bounded FX/audio repair comparison: real D3D12/Slate/input, 50-round magazine, bright/shade/companion firing, four original defeat lines, eight rapid lethal targets for effect overlap/recovery, movement/switch/reload. Passive fixture enemies, invulnerable player, paused director. Screenshot capture overhead is separately identified in FX samples."));
        R->SetNumberField(TEXT("max_fx_actors"),MaxFXActors);R->SetNumberField(TEXT("max_fx_mesh_components"),MaxFXComponents);R->SetArrayField(TEXT("fx_samples"),FXSamples);
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("BLSQASpatialOnly")))R->SetStringField(TEXT("scope"),TEXT("Dedicated spatial voice probe: real menu/input, passive enemy, close camera within 3 metres of companion, actual companion lethal damage and recorded spatial voice. No player voice or production combat claims from this probe."));
    TArray<TSharedPtr<FJsonValue>> Times;for(double F:Frames)Times.Add(MakeShared<FJsonValueNumber>(F));R->SetArrayField(TEXT("frame_seconds"),Times);
    FString Text;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Out/TEXT("report.json")));Next=FPlatformTime::Seconds()+3;Stage=99;
}
void AHeliosBuLianShiP90QA::Tick(float Dt)
{
    Super::Tick(Dt);const double Wall=FPlatformTime::Seconds();if(Stage==99){if(Wall>=Next)FPlatformMisc::RequestExit(false);return;}
    if(Started==0){PreviousUnfocusedVolume=FApp::GetUnfocusedVolumeMultiplier();FApp::SetUnfocusedVolumeMultiplier(1);Started=Wall;FParse::Value(FCommandLine::Get(),TEXT("BLSQAOut="),Out);if(Out.IsEmpty())Out=FPaths::ProjectDir()/TEXT("../qa/cfm_bulianshi_p90/runtime");Out=FPaths::ConvertRelativePathToFull(Out);IFileManager::Get().MakeDirectory(*Out,true);
#if WITH_EDITOR
        if(FParse::Param(FCommandLine::Get(),TEXT("BLSQAFixSourceBind")))
        {
            // FBX imported two ankle bind rotations incorrectly while retaining
            // the exact vertices, weights and animation poses. Restore only the
            // new body's source edit-bind frame; preserve every animation track.
            auto* Mesh=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Helios/CFMBuLianShiP90/SK_BuLianShi_TP"));FString Source;TSharedPtr<FJsonObject> Root;
            if(Mesh&&FFileHelper::LoadFileToString(Source,*(FPaths::ProjectDir()/TEXT("../qa/cfm_bulianshi_p90/source_shoe_bindings.json")))&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Source),Root))
            {
                const auto Bindings=Root->GetObjectField(TEXT("reference_bones"));auto& Ref=Mesh->GetRefSkeleton();TArray<FTransform> Global;
                {FReferenceSkeletonModifier Modifier(Ref,Mesh->GetSkeleton());for(int32 Bone=0;Bone<Ref.GetRawBoneNum();++Bone)
                {const int32 Parent=Ref.GetParentIndex(Bone);FTransform T=Ref.GetRefBonePose()[Bone];if(Parent>=0)T*=Global[Parent];const TArray<TSharedPtr<FJsonValue>>* Row=nullptr;
                if(Bindings->TryGetArrayField(Ref.GetBoneName(Bone).ToString(),Row)&&Row->Num()==10){auto V=[&](int32 I){return (*Row)[I]->AsNumber();};T=FTransform(FQuat(V(3),V(4),V(5),V(6)),FVector(V(0),V(1),V(2)),FVector(100));}
                Global.Add(T);Modifier.UpdateRefPoseTransform(Bone,Parent>=0?T.GetRelativeTransform(Global[Parent]):T);}}
                Mesh->GetSkeleton()->UpdateReferencePoseFromMesh(Mesh);Mesh->GetRefBasesInvMatrix().Empty();Mesh->CalculateInvRefMatrices();Mesh->MarkPackageDirty();Mesh->GetSkeleton()->MarkPackageDirty();FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
                const bool BodySaved=UPackage::SavePackage(Mesh->GetPackage(),Mesh,*(FPaths::ProjectContentDir()/TEXT("Helios/CFMBuLianShiP90/SK_BuLianShi_TP.uasset")),Args);
                const bool SkeletonSaved=UPackage::SavePackage(Mesh->GetSkeleton()->GetPackage(),Mesh->GetSkeleton(),*(FPaths::ProjectContentDir()/TEXT("Helios/CFMBuLianShiP90/SK_BuLianShi_TP_Skeleton.uasset")),Args);
                Check(TEXT("source_bind_repair_saved_only_new_body"),BodySaved&&SkeletonSaved);
            }else Check(TEXT("source_bind_repair_has_original_data"),false);
        }
#endif
    }
    if(Wall-Started>320){Check(TEXT("bounded_timeout"),false);Finish();return;}
    auto* PC=UGameplayStatics::GetPlayerController(this,0);auto* H=PC?Cast<AHeliosAdventureHUD>(PC->GetHUD()):nullptr;auto* D=AHeliosAdventureDirector::Find(GetWorld());Player=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));if(!H||!D||!Player||Wall-Started<5)return;
    auto Advance=[&](int32 S){Stage=S;Age=0;Next=Wall+.4;Captures.Reset();};Age+=Dt;if(Stage>5)Frames.Add(Dt);
    auto TimedCapture=[&](int32 I,float T,const TCHAR* N){if(Age>=T&&!Captures.Contains(I)){Captures.Add(I);Capture(N);}};
    if(FParse::Param(FCommandLine::Get(),TEXT("BLSQAFXRepair"))&&Stage>=7&&Stage<99)
    {
        int32 Actors=0,Components=0,Visible=0;
        for(TActorIterator<AActor> It(GetWorld());It;++It)
        {
            const FString Class=It->GetClass()->GetName();if(!Class.Contains(TEXT("SSXEffect"))&&!Class.Contains(TEXT("P90Effect")))continue;
            if(It->IsActorBeingDestroyed())continue;++Actors;TArray<UStaticMeshComponent*> Meshes;It->GetComponents(Meshes);Components+=Meshes.Num();for(auto* Mesh:Meshes)if(Mesh->IsVisible())++Visible;
        }
        MaxFXActors=FMath::Max(MaxFXActors,Actors);MaxFXComponents=FMath::Max(MaxFXComponents,Components);
        const bool Record=(Stage==7&&Age<1.25f)||(Stage==9&&Age<1.05f)||(Stage==11&&Age>=2&&Age<3.3f)||(Stage==14&&Age<1.2f)||(Stage==60&&Age<3);
        if(Wall>=NextFXFrame)
        {
            auto Sample=MakeShared<FJsonObject>();Sample->SetNumberField(TEXT("stage"),Stage);Sample->SetNumberField(TEXT("age"),Age);Sample->SetNumberField(TEXT("dt"),Dt);Sample->SetNumberField(TEXT("actors"),Actors);Sample->SetNumberField(TEXT("components"),Components);Sample->SetNumberField(TEXT("visible"),Visible);Sample->SetBoolField(TEXT("capture"),Record);FXSamples.Add(MakeShared<FJsonValueObject>(Sample));
            if(Record)HeliosAsyncFrameCapture::Request(Out/FString::Printf(TEXT("fx_%02d_%05d.jpg"),Stage,FXFrame++));NextFXFrame=Wall+.0333333;
        }
    }
    if((Stage==11||Stage==12)&&IsValid(Camera)&&IsValid(Pet))
    {
        FVector Offset(550,-380,100);
        if(Stage==12&&FParse::Param(FCommandLine::Get(),TEXT("BLSQASpatialOnly")))Offset=FVector(200,-100,65);
        if(Stage==11&&Age>=3.5f&&Age<4.5f)Offset=Pet->GetActorForwardVector()*500+FVector(0,0,80);
        else if(Stage==11&&Age>=4.5f&&Age<5.5f)Offset=Pet->GetActorRightVector()*500+FVector(0,0,80);
        else if(Stage==11&&Age>=5.5f&&Age<6.5f)Offset=-Pet->GetActorForwardVector()*500+FVector(0,0,80);
        Camera->SetActorLocation(Pet->GetActorLocation()+Offset);Camera->SetActorRotation((Pet->GetActorLocation()+FVector(0,0,-5)-Camera->GetActorLocation()).Rotation());
    }
    // Release the fixture trigger before the generic transition delay. Holding
    // it through that delay fires twice and exhausts a magazine before 11 kills.
    if(Stage==9&&Age>.025f)Key(EKeys::LeftMouseButton,false);
    const bool AudioProbe=FParse::Param(FCommandLine::Get(),TEXT("BLSQAAudio"));
    if(AudioProbe&&Stage==70&&Age>.02f)Key(EKeys::LeftMouseButton,false);
    if(AudioProbe&&Stage==11&&IsValid(Pet))
    {
        const int32 Phase=Age<4?0:Age<8?1:2;const float Distance=Phase==0?200:Phase==1?2000:4000;
        PC->SetAudioListenerOverride(nullptr,Pet->MuzzleLocation()+FVector(0,Distance,0),FRotator::ZeroRotator);
        if(Phase!=AudioDistancePhase){AudioDistancePhase=Phase;MarkAudio(Phase==0?TEXT("pet_near_2m"):Phase==1?TEXT("pet_mid_20m"):TEXT("pet_far_40m"));}
    }
    if(AudioProbe&&Stage==12&&IsValid(Pet))PC->SetAudioListenerOverride(nullptr,Pet->MuzzleLocation()+FVector(0,200,0),FRotator::ZeroRotator);
    if(AudioProbe&&(Stage==72||Stage==74||Stage==81)&&!bAudioReleased)
    {
        auto* Current=Cast<AHeliosBuLianShiP90Weapon>(Player->GetEquippedWeapon());
        if(Current&&Current->ShotsFired-AudioBeforeShots>=4)
        {
            Key(EKeys::LeftMouseButton,false);AudioSource=Current->FireAudio;AudioTails=AudioSource->PlayingChannels();AudioBeforeTriggers=AudioSource->TriggeredShots;bAudioReleased=true;AudioEventWall=Wall;
            if(Stage==72){Key(EKeys::R,true);Key(EKeys::R,false);MarkAudio(TEXT("player_release_then_reload"));}
            if(Stage==74){Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("rifle")));MarkAudio(TEXT("player_switch_during_tail"));Advance(75);}
            if(Stage==81){Player->SetCanBeDamaged(true);Player->TakeDamage(100000,FDamageEvent(),nullptr,Target);MarkAudio(TEXT("player_real_death"));Advance(82);}
        }
    }
    if(Stage==7)PC->SetControlRotation(FRotator(0,90,0)); // bounded fixture aim input
    if(Wall<Next)return;
    auto* W=Cast<AHeliosBuLianShiP90Weapon>(Player->GetEquippedWeapon());
    switch(Stage)
    {
    case 0:
#if WITH_DEV_AUTOMATION_TESTS
        if(HeliosMenuQANavigate)HeliosMenuQANavigate(true,false);
#endif
        Advance(1);return;
    case 1:Scroll(TEXT("步练师·灵魂舞者 / P90双脊龙"));Advance(2);return;
    case 2:Check(TEXT("actual_weapon_button_clicked"),Click(TEXT("步练师·灵魂舞者 / P90双脊龙")));Check(TEXT("selected_new_weapon"),H->GetPendingWeaponId()==TEXT("cfm_bulianshi_p90"));Capture(TEXT("01_weapon_menu"));Advance(3);return;
    case 3:
#if WITH_DEV_AUTOMATION_TESTS
        if(HeliosMenuQANavigate)HeliosMenuQANavigate(true,true);
#endif
        Advance(4);return;
    case 4:Scroll(TEXT("步练师·灵魂舞者 / P90双脊龙伙伴"));Advance(5);return;
    case 5:Check(TEXT("actual_pet_button_clicked"),Click(TEXT("步练师·灵魂舞者 / P90双脊龙伙伴")));Check(TEXT("independent_pet_selection"),H->GetSelectedPet()==EHeliosPetKind::BuLianShiP90&&H->GetPendingWeaponId()==TEXT("cfm_bulianshi_p90"));Capture(TEXT("02_pet_menu"));Advance(51);return;
    case 51: H->StartSelectedRun();D->SetActorTickEnabled(false);D->CleanupOwnedActors();Player->SetCanBeDamaged(false);Player->SetActorLocation(FVector(0,-2800,102),false,nullptr,ETeleportType::TeleportPhysics);PC->SetControlRotation(FRotator(0,90,0));Advance(6);return;
    case 6:
        if(Age<3)return;Pet=Cast<AHeliosBuLianShiP90Pet>(GetWorld()->GetSubsystem<UHeliosPetWorld>()->Pet);Check(TEXT("selected_pet_spawned"),Pet&&Pet->bModelReady);Check(TEXT("selected_weapon_spawned"),W!=nullptr);if(!W||!Pet){Finish();return;}
#if WITH_EDITOR
        // First-load shader preparation is outside the timed input/capture
        // sequence. A missing D3D shader must never look like a successful FX.
        if((FParse::Param(FCommandLine::Get(),TEXT("BLSQAFXRepair"))||FParse::Param(FCommandLine::Get(),TEXT("BLSQAAudio")))&&GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
#endif
        Check(TEXT("all_five_original_gun_renderers"),W->GunParts.Num()==5&&W->GunParts.ContainsByPredicate([](const auto& M){return !M||!M->GetSkeletalMeshAsset();})==false);
        Check(TEXT("source_arms_and_gun_loaded"),W->P90->GetSkeletalMeshAsset()&&W->GetFirstPersonMesh()->GetSkeletalMeshAsset());
        Check(TEXT("verified_bulianshi_sp_arms_and_body_materials"),BuLianShiMaterials(W->GetFirstPersonMesh())&&BuLianShiMaterials(Pet->GetMesh()));
        Check(TEXT("no_hunter07_component"),W->FindComponentByClass<UHeliosHunterDamageFeedback>()==nullptr);Check(TEXT("twentythree_original_kill_lines"),W->Voice->GetKillPoolSize()==23&&Pet->Voice->GetKillPoolSize()==23);Pet->SetActorTickEnabled(false);SpawnTarget(100000);UAudioMixerBlueprintLibrary::StartRecordingOutput(this,AudioProbe?180:100);bRecording=true;AudioStartWall=Wall;Capture(TEXT("03_bright_idle"));
        if(AudioProbe){Check(TEXT("approved_1p_3p_nonlooping_sources"),W->FireAudio->Sound&&!W->FireAudio->Sound->bLooping&&Pet->FireAudio->Sound&&!Pet->FireAudio->Sound->bLooping);Check(TEXT("partner_fire_outer_35m"),Pet->FireAudio->Attenuation&&FMath::Abs(Pet->FireAudio->Attenuation->Attenuation.AttenuationShapeExtents.X+Pet->FireAudio->Attenuation->Attenuation.FalloffDistance-3500)<1);MarkAudio(TEXT("player_50_start"));}
        if(FParse::Param(FCommandLine::Get(),TEXT("BLSQASpatialOnly"))){if(IsValid(Target))Target->Destroy();Pet->SetActorTickEnabled(true);PetStart=Pet->GetActorLocation();Player->SetActorLocation(Player->GetActorLocation()+FVector(0,2000,0),false,nullptr,ETeleportType::TeleportPhysics);Advance(10);return;}
        Key(EKeys::LeftMouseButton,true);Advance(7);return;
    case 7:
        TimedCapture(1,.65f,TEXT("04_bright_fire"));if(W&&W->GetBulletCount()>0&&Age<8)return;Key(EKeys::LeftMouseButton,false);Check(TEXT("continuous_fire_magazine"),W&&W->ShotsFired==W->ShotSettings.Magazine&&W->GetBulletCount()==0);Check(TEXT("nonlethal_damage_no_defeat_voice"),W&&W->TrueKills==0&&W->Voice->KillCount==0&&W->HitPellets>0);if(AudioProbe){Check(TEXT("fifty_shots_fifty_sound_triggers"),W->FireAudio->TriggeredShots==50&&W->FireAudio->StartedVoices==50);Check(TEXT("bounded_player_sound_voices"),W->FireAudio->PeakVoices<=W->FireAudio->VoiceLimit);MarkAudio(TEXT("player_50_stop_reload"));}Key(EKeys::R,true);Key(EKeys::R,false);Advance(8);return;
    case 8:
        TimedCapture(1,.45f,TEXT("05_reload_out"));TimedCapture(2,1.0f,TEXT("06_reload_in"));if(Age<2.4)return;Check(TEXT("reload_completed"),W&&W->GetBulletCount()==W->ShotSettings.Magazine&&!Player->IsReloading());if(AudioProbe){Check(TEXT("magazine_tail_finished_no_new_fire_in_reload"),W->FireAudio->TriggeredShots==50&&W->FireAudio->PlayingCount()==0);MarkAudio(TEXT("player_real_kill_voice_start"));}SpawnTarget(1);Key(EKeys::LeftMouseButton,true);Advance(9);return;
    case 9:
        if(Age<3.35f)return;if(W)VoiceIndices.Add(W->Voice->GetLastPlayedKillIndex());++Kills;
        {const int32 RequestedKills=FParse::Param(FCommandLine::Get(),TEXT("BLSQAAllVoices"))?23:4;
        if(Kills<RequestedKills){SpawnTarget(1);Key(EKeys::LeftMouseButton,true);Advance(9);return;}Check(TEXT("one_defeat_per_real_enemy"),W&&W->TrueKills==RequestedKills);Check(TEXT("random_nonrepeating_defeat_lines"),RequestedKills==23?VoiceIndices.Num()==23:VoiceIndices.Num()>=3);}
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
                FCollisionQueryParams Q(SCENE_QUERY_STAT(BLSQAClearFixture),false,Pet);Q.AddIgnoredActor(Player);Q.AddIgnoredActor(Target);FHitResult Hit;
                if(GetWorld()->LineTraceSingleByChannel(Hit,Pet->GetActorLocation()+FVector(0,0,35),At,ECC_Visibility,Q))continue;
                Target->SetActorLocation(At,false,nullptr,ETeleportType::TeleportPhysics);Placed=true;
            }
            Check(TEXT("partner_fixture_has_navigable_clear_sight"),Placed);
        }
        Camera=GetWorld()->SpawnActor<ACameraActor>();if(Pet){Camera->SetActorLocation(Pet->GetActorLocation()+FVector(550,-380,100));Camera->SetActorRotation((Pet->GetActorLocation()+FVector(0,0,-5)-Camera->GetActorLocation()).Rotation());Camera->GetCameraComponent()->FieldOfView=55;PC->SetViewTarget(Camera);}Advance(11);return;
    case 11:
        TimedCapture(1,3,TEXT("08_pet_holding_fire"));TimedCapture(2,8,TEXT("09_pet_reload"));
        TimedCapture(3,4,TEXT("08a_pet_front"));TimedCapture(4,5,TEXT("08b_pet_side"));TimedCapture(5,6,TEXT("08c_pet_back"));
        if(Age<11||(Age<20&&Pet&&Pet->Reloads<1))return;Check(TEXT("pet_fires_and_reloads"),Pet&&Pet->ShotsFired>=13&&Pet->Reloads>=1);Check(TEXT("gun_attached_to_source_weapon_bone"),Pet&&Pet->P90->GetAttachSocketName()==TEXT("b_RightWeapon"));if(AudioProbe){Check(TEXT("partner_shots_sound_triggers_synchronized"),Pet->ShotsFired==Pet->FireAudio->TriggeredShots);Check(TEXT("bounded_partner_sound_voices"),Pet->FireAudio->PeakVoices<=Pet->FireAudio->VoiceLimit);AudioPetTargetLocation=Target->GetActorLocation();MarkAudio(TEXT("pet_distance_probe_end_kill_voice"));}
        if(Pet)
        {
            const float Height=Pet->GetMesh()->GetSkeletalMeshAsset()->GetBounds().BoxExtent.Z*2*Pet->GetMesh()->GetRelativeScale3D().Z;Check(TEXT("height_matches_ssx_203cm"),FMath::Abs(Height-203.3593f)<.5f);
            const float Floor=Pet->GetActorLocation().Z-Pet->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            const FVector LeftToe=Pet->GetMesh()->GetSocketLocation(TEXT("b_LeftToe")),RightToe=Pet->GetMesh()->GetSocketLocation(TEXT("b_RightToe"));
            Check(TEXT("standing_source_toes_near_capsule_floor"),!LeftToe.ContainsNaN()&&!RightToe.ContainsNaN()&&FMath::Abs(LeftToe.Z-Floor)<8&&FMath::Abs(RightToe.Z-Floor)<8);
            UE_LOG(LogTemp,Display,TEXT("BLS_QA_PET height=%.4f capsule_bottom=%.4f left_toe=%s right_toe=%s right_weapon=%s muzzle=%s"),Height,Floor,*LeftToe.ToCompactString(),*RightToe.ToCompactString(),*Pet->GetMesh()->GetSocketLocation(TEXT("b_RightWeapon")).ToCompactString(),*Pet->MuzzleLocation().ToCompactString());
            auto Poses=MakeShared<FJsonObject>();
            for(const TCHAR* Bone:{TEXT("b_Hips"),TEXT("b_LeftAnkle"),TEXT("b_LeftToe"),TEXT("b_RightAnkle"),TEXT("b_RightToe"),TEXT("b_RightWeapon")})
            {const auto T=Pet->GetMesh()->GetSocketTransform(FName(Bone),RTS_Component);auto B=MakeShared<FJsonObject>();const auto V=T.GetLocation();const auto Q=T.GetRotation();const auto S=T.GetScale3D();TArray<TSharedPtr<FJsonValue>> A;for(double Value:{V.X,V.Y,V.Z,Q.X,Q.Y,Q.Z,Q.W,S.X,S.Y,S.Z})A.Add(MakeShared<FJsonValueNumber>(Value));B->SetArrayField(TEXT("translation_quaternion_scale"),A);Poses->SetObjectField(Bone,B);}
            FString Text;FJsonSerializer::Serialize(Poses,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Out/TEXT("source_bone_pose_runtime.json")));
            // Bone proximity cannot establish correct shoe skinning. Retain the
            // actual CPU skin result for comparison with original source vertices.
            if(auto* Render=Pet->GetMesh()->GetSkeletalMeshAsset()->GetResourceForRendering();Render&&!Render->LODRenderData.IsEmpty())
            {
                auto* Weights=Pet->GetMesh()->GetSkinWeightBuffer(0);TArray<FMatrix44f> Matrices;Pet->GetMesh()->GetCurrentRefToLocalMatrices(Matrices,0);TArray<FVector3f> Vertices;
                if(Weights)USkinnedMeshComponent::ComputeSkinnedPositions(Pet->GetMesh(),Vertices,Matrices,Render->LODRenderData[0],*Weights);
                TArray<TSharedPtr<FJsonValue>> Positions;for(const FVector3f& Vertex:Vertices)if(Vertex.Z<15){TArray<TSharedPtr<FJsonValue>> Row;for(double Value:{double(Vertex.X),double(Vertex.Y),double(Vertex.Z)})Row.Add(MakeShared<FJsonValueNumber>(Value));Positions.Add(MakeShared<FJsonValueArray>(Row));}
                auto Skin=MakeShared<FJsonObject>();Skin->SetArrayField(TEXT("shoe_vertices_component_cm"),Positions);Skin->SetNumberField(TEXT("total_vertices"),Vertices.Num());FJsonSerializer::Serialize(Skin,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Out/TEXT("source_shoe_skin_runtime.json")));
                const auto& Ref=Pet->GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton();TArray<FTransform> Bind;auto BindJson=MakeShared<FJsonObject>();
                for(int32 Bone=0;Bone<Ref.GetNum();++Bone){FTransform T=Ref.GetRefBonePose()[Bone];const int32 Parent=Ref.GetParentIndex(Bone);if(Parent>=0)T*=Bind[Parent];Bind.Add(T);TArray<TSharedPtr<FJsonValue>> Row;const FVector V=T.GetLocation(),S=T.GetScale3D();const FQuat Q=T.GetRotation();for(double Value:{V.X,V.Y,V.Z,Q.X,Q.Y,Q.Z,Q.W,S.X,S.Y,S.Z})Row.Add(MakeShared<FJsonValueNumber>(Value));BindJson->SetArrayField(Ref.GetBoneName(Bone).ToString(),Row);}
                auto Diagnostic=MakeShared<FJsonObject>();Diagnostic->SetObjectField(TEXT("reference_bones"),BindJson);TArray<TSharedPtr<FJsonValue>> Rows;const auto& Lod=Render->LODRenderData[0];
                if(Weights)for(const auto& Section:Lod.RenderSections)for(uint32 Vertex=Section.BaseVertexIndex;Vertex<Section.BaseVertexIndex+Section.NumVertices;++Vertex)
                {if(Vertices[Vertex].Z>=15)continue;auto Row=MakeShared<FJsonObject>();const FVector3f V=Lod.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Vertex);TArray<TSharedPtr<FJsonValue>> P;for(double Value:{double(V.X),double(V.Y),double(V.Z)})P.Add(MakeShared<FJsonValueNumber>(Value));Row->SetArrayField(TEXT("bind_position"),P);auto Influence=MakeShared<FJsonObject>();for(uint32 I=0;I<Weights->GetMaxBoneInfluences();++I){const uint16 Weight=Weights->GetBoneWeight(Vertex,I);if(Weight>0)Influence->SetNumberField(Ref.GetBoneName(Section.BoneMap[Weights->GetBoneIndex(Vertex,I)]).ToString(),Weight);}Row->SetObjectField(TEXT("weights"),Influence);Rows.Add(MakeShared<FJsonValueObject>(Row));}
                Diagnostic->SetArrayField(TEXT("shoe_vertices"),Rows);FJsonSerializer::Serialize(Diagnostic,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Out/TEXT("shoe_bind_diagnostic.json")));
            }
        }
        if(IsValid(Target)&&!FParse::Param(FCommandLine::Get(),TEXT("BLSQASpatialOnly"))&&!AudioProbe)Target->CurrentHP=1;BeforeShots=Pet?Pet->ShotsFired:0;Advance(12);return;
    case 12:
        if(AudioProbe&&Age>.65f&&IsValid(Target)&&!Captures.Contains(95)){Captures.Add(95);Target->CurrentHP=1;MarkAudio(TEXT("pet_near_listener_ready_for_real_kill"));}
        if(FParse::Param(FCommandLine::Get(),TEXT("BLSQASpatialOnly")))
        {
            // Let the actual audio listener reach the near camera before a
            // lethal hit; one-shot spatial sounds started outside 6 m are muted.
            if(IsValid(Target))Target->CurrentHP=1;
            TimedCapture(1,.65f,TEXT("09a_pet_near_real_defeat"));if(Age<4)return;
            Check(TEXT("pet_owns_real_defeat"),Pet&&Pet->TrueKills>0&&Pet->Voice->KillCount>0);
            Check(TEXT("spatial_probe_camera_within_3m"),Camera&&Pet&&FVector::Dist(Camera->GetActorLocation(),Pet->GetActorLocation())<300);
            Finish();return;
        }
        if(Age<3)return;Check(TEXT("pet_owns_real_defeat"),Pet&&Pet->TrueKills>0&&Pet->Voice->KillCount>0);if(Pet)Pet->SetActorTickEnabled(false);PC->ClearAudioListenerOverride();PC->SetViewTarget(Player);Player->SetActorLocation(FVector(0,1800,102),false,nullptr,ETeleportType::TeleportPhysics);PC->SetControlRotation(FRotator(0,90,0));SpawnTarget(100000);Key(EKeys::R,true);Key(EKeys::R,false);Advance(13);return;
    case 13:
        if(Age<3)return;Capture(TEXT("10_shade_idle"));Key(EKeys::LeftMouseButton,true);Advance(14);return;
    case 14:
        TimedCapture(1,.65f,TEXT("11_shade_fire"));if(Age<1.2f)return;Key(EKeys::LeftMouseButton,false);Key(EKeys::G,true);Key(EKeys::G,false);Advance(15);return;
    case 15:
        TimedCapture(1,.65f,TEXT("12_show"));TimedCapture(2,4.2f,TEXT("12a_show_middle"));TimedCapture(3,7.5f,TEXT("12b_show_late"));if(Age<11)return;Check(TEXT("same_weapon_still_active_after_show"),W&&Player->GetWeaponId()==TEXT("cfm_bulianshi_p90"));Check(TEXT("switch_to_old_rifle"),Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("rifle"))));Advance(16);return;
    case 16:
        if(Age<.7f)return;Check(TEXT("old_rifle_actual_equipped"),Player->GetWeaponId()==TEXT("rifle"));Check(TEXT("switch_back_to_chan"),Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("cfm_bulianshi_p90"))));Advance(17);return;
    case 17:
        if(Age<1.2f)return;SpawnTarget(100000);Key(EKeys::LeftMouseButton,true);Advance(18);return;
    case 18:
        TimedCapture(1,.05f,TEXT("13_after_switch_fire"));if(Age<.6f)return;Key(EKeys::LeftMouseButton,false);Check(TEXT("fires_after_equipment_switch"),W&&W->ShotsFired>=1);Key(EKeys::R,true);Key(EKeys::R,false);Advance(19);return;
    case 19:
        if(Age<.5f)return;Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("rifle")));Check(TEXT("switch_during_reload"),!Player->IsReloading()&&Player->GetWeaponId()==TEXT("rifle"));Advance(20);return;
    case 20:
        if(Age<.5f)return;Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("cfm_bulianshi_p90")));Advance(21);return;
    case 21:
        if(Age<1.2f)return;Check(TEXT("new_weapon_recovers_after_cancelled_reload"),W&&W->GetBulletCount()==W->ShotSettings.Magazine&&!Player->IsReloading());if(AudioProbe){AudioBeforeShots=W->ShotsFired;MarkAudio(TEXT("player_single_start"));Key(EKeys::LeftMouseButton,true);Advance(70);return;}if(FParse::Param(FCommandLine::Get(),TEXT("BLSQAFXRepair"))){StackKillsBefore=W?W->TrueKills:0;StackTargets=0;Key(EKeys::W,true);Key(EKeys::LeftMouseButton,true);Advance(60);return;}if(FParse::Param(FCommandLine::Get(),TEXT("BLSQAQuick"))){Finish();return;}if(IsValid(Target))Target->Destroy();Player->SetCanBeDamaged(true);if(Pet)Pet->SetActorTickEnabled(true);D->SetActorTickEnabled(true);Advance(22);return;
    case 70:
        if(Age<1.6f)return;Check(TEXT("single_shot_release_one_trigger_and_natural_tail_end"),W&&W->ShotsFired-AudioBeforeShots==1&&W->FireAudio->TriggeredShots==W->ShotsFired&&W->FireAudio->PlayingCount()==0);MarkAudio(TEXT("player_single_end"));Key(EKeys::R,true);Key(EKeys::R,false);Advance(71);return;
    case 71:
        if(Age<2.4f)return;AudioBeforeShots=W->ShotsFired;bAudioReleased=false;Key(EKeys::LeftMouseButton,true);Advance(72);return;
    case 72:
        if(bAudioReleased&&Wall-AudioEventWall>.15&&!Captures.Contains(90)){Captures.Add(90);Check(TEXT("reload_does_not_cut_legal_single_tail"),AudioTailCount()>0);}
        if(!bAudioReleased||Age<1.8f)return;Check(TEXT("reload_preserves_single_tails"),AudioTailCount()==0&&AudioSource.IsValid()&&AudioSource->TriggeredShots==AudioBeforeTriggers);MarkAudio(TEXT("player_reload_tail_end"));Advance(73);return;
    case 73:
        if(Age<1)return;Check(TEXT("reload_finishes_after_short_burst"),!Player->IsReloading());AudioBeforeShots=W->ShotsFired;bAudioReleased=false;Key(EKeys::LeftMouseButton,true);Advance(74);return;
    case 74:return;
    case 75:
        if(Age>.25f&&!Captures.Contains(90)){Captures.Add(90);Check(TEXT("switch_preserves_legal_single_tail"),AudioTailCount()>0);}
        if(Age<1.6f)return;Check(TEXT("switch_tail_ends_without_loop"),AudioTailCount()==0);Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("cfm_bulianshi_p90")));MarkAudio(TEXT("player_switch_tail_end"));Advance(76);return;
    case 76:
        if(Age<1.2f)return;SpawnTarget(100000);Target->SetActorLocation(AudioPetTargetLocation,false,nullptr,ETeleportType::TeleportPhysics);Pet->SetActorTickEnabled(true);AudioBeforePetShots=Pet->ShotsFired;PC->SetAudioListenerOverride(nullptr,Pet->MuzzleLocation()+FVector(0,200,0),FRotator::ZeroRotator);Advance(77);return;
    case 77:
        if(Pet->ShotsFired-AudioBeforePetShots<3){if(Age>12){Check(TEXT("partner_resume_actual_fire"),false);Finish();}return;}
        AudioSource=Pet->FireAudio;AudioTails=AudioSource->PlayingChannels();AudioBeforeTriggers=AudioSource->TriggeredShots;Pet->TakeDamage(100000,FDamageEvent(),nullptr,Target);MarkAudio(TEXT("pet_real_down"));Advance(78);return;
    case 78:
        if(Age>.25f&&!Captures.Contains(90)){Captures.Add(90);Check(TEXT("partner_down_preserves_legal_single_tail"),AudioTailCount()>0);}
        if(Age<1.6f)return;Check(TEXT("partner_down_stops_new_shots_tail_ends"),Pet->IsSleeping()&&AudioSource->TriggeredShots==AudioBeforeTriggers&&AudioTailCount()==0);Pet->Restore(0,.05f);AudioBeforePetShots=Pet->ShotsFired;Advance(79);return;
    case 79:
        if(Pet->ShotsFired<=AudioBeforePetShots){if(Age>12){Check(TEXT("partner_wake_actual_fire"),false);Finish();}return;}
        AudioTails=Pet->FireAudio->PlayingChannels();Pet->Destroy();MarkAudio(TEXT("pet_destroy_during_single_tail"));Advance(80);return;
    case 80:
        if(Age>.25f&&!Captures.Contains(90)){Captures.Add(90);Check(TEXT("partner_destroy_preserves_legal_single_tail"),AudioTailCount()>0);}
        if(Age<1.6f)return;Check(TEXT("partner_destroy_tail_ends_without_loop"),AudioTailCount()==0&&!IsValid(Pet));PC->ClearAudioListenerOverride();SpawnTarget(100000);AudioBeforeShots=W->ShotsFired;bAudioReleased=false;Key(EKeys::LeftMouseButton,true);Advance(81);return;
    case 81:return;
    case 82:
        if(Wall-AudioEventWall<1.6)return;Check(TEXT("player_real_death_no_new_shots_or_loop"),Player->IsDead()&&(!AudioSource.IsValid()||AudioSource->TriggeredShots==AudioBeforeTriggers)&&AudioTailCount()==0);MarkAudio(TEXT("player_death_tail_end"));Finish();return;
    case 60:
        if(StackTargets<8&&Age>.05f&&(!IsValid(Target)||Target->IsEnemyDead())){SpawnTarget(1);++StackTargets;}
        if(StackTargets==0){SpawnTarget(1);++StackTargets;}
        if(IsValid(Target)){FVector Eye;FRotator Aim;Player->GetActorEyesViewPoint(Eye,Aim);PC->SetControlRotation((Target->GetActorLocation()-Eye).Rotation());}
        if(Age>.5f)Key(EKeys::W,false);
        if(Age<7&&!(StackTargets==8&&IsValid(Target)&&Target->IsEnemyDead()))return;Key(EKeys::W,false);Key(EKeys::LeftMouseButton,false);Check(TEXT("eight_rapid_real_kills"),W&&W->TrueKills-StackKillsBefore==8);Advance(61);return;
    case 61:
        if(Age<2)return;
        {
            int32 NonLooping=0;for(TActorIterator<AHeliosBuLianShiP90Effect> It(GetWorld());It;++It)if(!It->IsActorBeingDestroyed()&&!It->IsLooping())++NonLooping;
            Check(TEXT("source_transient_effects_reclaimed"),NonLooping==0);Check(TEXT("bounded_effect_actor_count"),MaxFXActors<45);Check(TEXT("bounded_effect_component_count"),MaxFXComponents<650);
        }
        Finish();return;
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
