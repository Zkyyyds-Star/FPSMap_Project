#include "QA/HeliosYunSummerMegatronQA.h"
#include "QA/HeliosAsyncFrameCapture.h"
#include "Weapons/HeliosYunSummerMegatron.h"
#include "Pets/HeliosYunSummerMegatronPet.h"
#include "Weapons/HeliosWangZheKuiThunder.h"
#include "Weapons/HeliosSSXVoice.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Weapons/HeliosHandAnimInstance.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Player/HeliosArenaCharacter.h"
#include "UI/HeliosAdventureHUD.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Boss/HeliosEnemyBoss.h"
#include "AudioMixerBlueprintLibrary.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/App.h"
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
bool HasLabel(const TSharedRef<SWidget>& Widget,const FString& Text)
{
    if(Widget->GetTypeAsString()==TEXT("STextBlock")&&StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString()==Text)return true;
    auto* Children=Widget->GetChildren();
    for(int32 I=0;Children&&I<Children->Num();++I)if(HasLabel(Children->GetChildAt(I),Text))return true;
    return false;
}
TSharedPtr<SWidget> FindWidget(const TSharedRef<SWidget>& Widget,const FString& Text,const FString& Type)
{
    if(!Widget->GetVisibility().IsVisible())return nullptr;
    if(Widget->GetTypeAsString()==Type&&HasLabel(Widget,Text))return Widget;
    auto* Children=Widget->GetChildren();
    for(int32 I=0;Children&&I<Children->Num();++I)if(auto Found=FindWidget(Children->GetChildAt(I),Text,Type))return Found;
    return nullptr;
}
TSharedPtr<SWidget> FindWidget(const FString& Text,const FString& Type)
{
    if(FSlateApplication::IsInitialized())for(const auto& Window:FSlateApplication::Get().GetInteractiveTopLevelWindows())
        if(auto Found=FindWidget(Window->GetContent(),Text,Type))return Found;
    return nullptr;
}
bool VisibleWidgetType(const TSharedRef<SWidget>& Widget,const FString& Type)
{
    if(!Widget->GetVisibility().IsVisible())return false;
    if(Widget->GetTypeAsString()==Type)return true;
    auto* Children=Widget->GetChildren();
    for(int32 I=0;Children&&I<Children->Num();++I)if(VisibleWidgetType(Children->GetChildAt(I),Type))return true;
    return false;
}
bool VisibleWidgetType(const FString& Type)
{
    if(FSlateApplication::IsInitialized())for(const auto& Window:FSlateApplication::Get().GetInteractiveTopLevelWindows())
        if(VisibleWidgetType(Window->GetContent(),Type))return true;
    return false;
}
void ScrollToYun()
{
    auto Button=FindWidget(TEXT("云悠悠·沐夏微醺"),TEXT("SButton"));
    auto Scroll=FindWidget(TEXT("云悠悠·沐夏微醺"),TEXT("SScrollBox"));
    if(Button&&Scroll)StaticCastSharedPtr<SScrollBox>(Scroll)->ScrollDescendantIntoView(Button,true,EDescendantScrollDestination::Center);
}
bool ClickLabel(const FString& Text,bool InRoster=true)
{
    auto Scroll=InRoster?FindWidget(TEXT("云悠悠·沐夏微醺"),TEXT("SScrollBox")):nullptr;
    auto Button=Scroll?FindWidget(Scroll.ToSharedRef(),Text,TEXT("SButton")):FindWidget(Text,TEXT("SButton"));
    if(!Button||!Button->IsEnabled())return false;
    auto& Slate=FSlateApplication::Get();FWidgetPath Path;
    if(!Slate.GeneratePathToWidgetUnchecked(Button.ToSharedRef(),Path))return false;
    const auto Geometry=Button->GetCachedGeometry();
    if(Geometry.GetLocalSize().X<=0||Geometry.GetLocalSize().Y<=0)return false;
    const FVector2D At=Geometry.LocalToAbsolute(Geometry.GetLocalSize()*.5f);
    TSet<FKey> Down;Down.Add(EKeys::LeftMouseButton);TSet<FKey> Up;
    FPointerEvent Press(FSlateApplication::CursorPointerIndex,At,At,Down,EKeys::LeftMouseButton,0,FModifierKeysState());
    FPointerEvent Release(FSlateApplication::CursorPointerIndex,At,At,Up,EKeys::LeftMouseButton,0,FModifierKeysState());
    Slate.ReleaseAllPointerCapture();Button->OnMouseEnter(Geometry,Press);
    auto P=Button->OnMouseButtonDown(Geometry,Press);Slate.ProcessReply(Path,P,&Path,&Press);
    auto R=Button->OnMouseButtonUp(Geometry,Release);Slate.ProcessReply(Path,R,&Path,&Release);
    return P.IsEventHandled()&&R.IsEventHandled();
}
const EHeliosYunSummerForm RosterForms[]={EHeliosYunSummerForm::GR,EHeliosYunSummerForm::BL,EHeliosYunSummerForm::SP,EHeliosYunSummerForm::GR};
void SuspendPets(UWorld* World,bool Suspend)
{
    if(auto* Pets=World->GetSubsystem<UHeliosPetWorld>())for(auto& Pet:Pets->Pets)if(Pet)Pet->SetActorTickEnabled(!Suspend);
}
bool NoOldWeaponEffects(UWorld* World,AActor* Current)
{
    for(TActorIterator<AHeliosSSXEffect> It(World);It;++It)
        if(auto* Owner=Cast<AHeliosYunSummerMegatronWeapon>(It->GetOwner());Owner&&Owner!=Current)return false;
    return true;
}
}

AHeliosYunSummerMegatronQA::AHeliosYunSummerMegatronQA()
{
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;
}
void AHeliosYunSummerMegatronQA::Check(const TCHAR* Name,bool Passed)
{
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("name"),Name);Row->SetBoolField(TEXT("passed"),Passed);
    Checks.Add(MakeShared<FJsonValueObject>(Row));if(!Passed)++Failures;
    UE_LOG(LogTemp,Display,TEXT("YUN_QA %s %s"),Name,Passed?TEXT("PASS"):TEXT("FAIL"));
}
void AHeliosYunSummerMegatronQA::Capture(const TCHAR* Name)
{FScreenshotRequest::RequestScreenshot(Out/(FString(Name)+TEXT(".png")),true,false);}
void AHeliosYunSummerMegatronQA::TickArmsView(AHeliosYunSummerMegatronWeapon* Weapon)
{
    struct FSample {const TCHAR* Clip;float Seconds;const TCHAR* Label;};
    static const FSample Samples[]={
        {TEXT("Idle"),0,TEXT("idle")},{TEXT("Move"),.25f,TEXT("move")},
        {TEXT("Equip"),.4f,TEXT("equip")},{TEXT("Fire"),.12f,TEXT("fire")},
        {TEXT("ChangeClip"),.45f,TEXT("reload_out")},{TEXT("ChangeClip"),1.05f,TEXT("reload_in")},
        {TEXT("Show"),1.4f,TEXT("inspect")},{TEXT("Show"),3.7f,TEXT("inspect_press")},
        {TEXT("Cheers"),.9f,TEXT("cheers")},{TEXT("Cheers"),1.8f,TEXT("cheers_late")},
        {TEXT("Cheers"),2.8f,TEXT("cheers_end")}
    };
    const double Wall=FPlatformTime::Seconds();
    if(Stage==200)
    {
        if(Age<3||!Weapon)return;
        SuspendPets(GetWorld(),true);Weapon->StopFiring();Weapon->CancelReload();
        Stage=210;Next=Wall+.4;return;
    }
    if(Stage==210)
    {
        if(ArmsSample>=UE_ARRAY_COUNT(Samples)*3){Finish();return;}
        const auto Form=EHeliosYunSummerForm(ArmsSample/UE_ARRAY_COUNT(Samples));
        const auto& Sample=Samples[ArmsSample%UE_ARRAY_COUNT(Samples)];
        Weapon->SetActorTickEnabled(true);Weapon->SetForm(Form);Weapon->SetActorTickEnabled(false);
        const FString Prefix=FString(TEXT("A_YUN_"))+HeliosYunSummer::FormCode(Form);
        const bool Cheers=FName(Sample.Clip)==TEXT("Cheers");
        Weapon->Megatron->SetVisibility(!Cheers);Weapon->Cup->SetVisibility(Cheers);
        for(TActorIterator<AHeliosSSXEffect> It(GetWorld());It;++It)
            if(It->GetOwner()==Weapon)It->SetActorHiddenInGame(Cheers);
        auto SampleMesh=[&](USkeletalMeshComponent* Mesh,UAnimSequence* Clip)
        {
            Mesh->SetComponentTickEnabled(false);
            if(auto* Anim=Cast<UHeliosHandAnimInstance>(Mesh->GetAnimInstance()))
            {
                Anim->BlendDuration=.001f;Anim->SetTimelinePose(Clip,Sample.Seconds,false,true);
                Mesh->TickAnimation(1.f/60.f,false);Mesh->RefreshBoneTransforms();
                // The first evaluation starts the transition at its old pose.
                // Advance it past the QA-only blend before freezing the frame.
                Mesh->TickAnimation(1.f/60.f,false);Mesh->RefreshBoneTransforms();
            }
        };
        SampleMesh(Weapon->GetFirstPersonMesh(),HeliosYunSummer::Animation(Prefix+TEXT("_FP_"),Sample.Clip));
        if(Cheers)SampleMesh(Weapon->Cup,HeliosYunSummer::Animation(Prefix+TEXT("_CUP_FP_"),TEXT("Cheers")));
        else SampleMesh(Weapon->Megatron,HeliosYunSummer::Animation(TEXT("A_MEGATRON_FP_"),Sample.Clip));
        Check(*FString::Printf(TEXT("%s_%s_authored_group_alignment"),HeliosYunSummer::FormCode(Form),Sample.Label),
            Weapon->GetFirstPersonMesh()->GetRelativeLocation().Equals(FVector(0,0,-6),.01f)
            &&Weapon->Megatron->GetRelativeTransform().Equals(FTransform::Identity,.01f)
            &&Weapon->Cup->GetRelativeTransform().Equals(FTransform::Identity,.01f));
        Stage=211;Next=Wall+.5;return;
    }
    if(Stage==211)
    {
        const auto Form=EHeliosYunSummerForm(ArmsSample/UE_ARRAY_COUNT(Samples));
        const auto& Sample=Samples[ArmsSample%UE_ARRAY_COUNT(Samples)];
        Capture(*FString::Printf(TEXT("arms_%s_%s"),HeliosYunSummer::FormCode(Form),Sample.Label));
        ++ArmsSample;Stage=210;Next=Wall+.5;
    }
}
void AHeliosYunSummerMegatronQA::Key(FKey Key,bool Down)
{
    if(auto* PC=UGameplayStatics::GetPlayerController(this,0))
        PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),Key,Down?IE_Pressed:IE_Released,FPlatformTime::Cycles64()));
}
AHeliosEnemyActor* AHeliosYunSummerMegatronQA::SpawnTarget(const FVector& At,float HP)
{
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Enemy=GetWorld()->SpawnActor<AHeliosEnemyE01>(At,FRotator(0,-90,0),Params);
    if(Enemy)
    {
        Enemy->CurrentHP=HP;Enemy->SetActorTickEnabled(false);Enemy->StopShooting();
        Enemy->GetCharacterMovement()->StopMovementImmediately();Enemy->GetCharacterMovement()->DisableMovement();
        if(auto* AI=Cast<AAIController>(Enemy->GetController())){AI->StopMovement();if(AI->GetBrainComponent())AI->GetBrainComponent()->StopLogic(TEXT("bounded Yun fixture"));}
    }
    return Enemy;
}
void AHeliosYunSummerMegatronQA::BlastFixtures(AHeliosYunSummerMegatronWeapon* Weapon)
{
    if(IsValid(Target))Target->Destroy();
    const FVector Center=Player->GetActorLocation()+FVector(0,650,45);
    auto* Near=SpawnTarget(Center,100000);
    auto* Far=SpawnTarget(Center+FVector(350,0,0),100000);
    auto* Outside=SpawnTarget(Center+FVector(700,0,0),100000);
    auto* Pets=GetWorld()->GetSubsystem<UHeliosPetWorld>();
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto SpawnBlast=[&](const FVector& At)
    {
        auto* Grenade=GetWorld()->SpawnActor<AHeliosMegatronGrenade>(At,FRotator::ZeroRotator,Params);
        if(Grenade){Grenade->Initialize(Weapon,Player,FVector::ForwardVector,HeliosYunSummer::Tuning(false),true);Grenade->Explode(At,FVector::UpVector);}
        return Grenade;
    };
    auto* Grenade=SpawnBlast(Center);
    const float NearDamage=Near?100000-Near->CurrentHP:0,FarDamage=Far?100000-Far->CurrentHP:0;
    Check(TEXT("blast_falloff_and_radius"),NearDamage>600&&NearDamage<=720.1f&&FarDamage>=240&&FarDamage<NearDamage&&Outside&&Outside->CurrentHP==100000);
    const float After=Near?Near->CurrentHP:0;
    if(Grenade)Grenade->Explode(Center,FVector::UpVector);
    Check(TEXT("one_damage_settlement_per_grenade"),Grenade&&Grenade->Hits==2&&Near&&Near->CurrentHP==After);
    if(Near)Near->Destroy();if(Far)Far->Destroy();if(Outside)Outside->Destroy();

    const float Health=Player->GetHealth();TArray<float> PetHealth;TArray<FVector> PetLocations;
    for(int32 I=0;I<Pets->Pets.Num();++I){auto& Pet=Pets->Pets[I];PetHealth.Add(Pet->Health);PetLocations.Add(Pet->GetActorLocation());Pet->SetActorLocation(Player->GetActorLocation()+FVector(80+40*I,0,0));}
    Player->SetCanBeDamaged(true);SpawnBlast(Player->GetActorLocation()+FVector(0,0,40));
    Check(TEXT("blast_excludes_damage_enabled_player_inside_radius"),Player->GetHealth()==Health);
    bool Healthy=true;for(int32 I=0;I<Pets->Pets.Num();++I){Healthy&=Pets->Pets[I]->Health==PetHealth[I];Pets->Pets[I]->SetActorLocation(PetLocations[I]);}
    Check(TEXT("blast_excludes_four_companions_inside_radius"),Healthy);
    FRadialDamageEvent FriendlyEvent;
    const float FriendlyDamage=Player->TakeDamage(50,FriendlyEvent,Player->GetController(),Weapon);
    Check(TEXT("character_rejects_other_friendly_radial_damage"),FriendlyDamage==0&&Player->GetHealth()==Health);
    Player->SetCanBeDamaged(false);

    // A real blocking primitive tests blast line-of-sight independently of camera aiming.
    auto* Wall=GetWorld()->SpawnActor<AActor>();
    auto* Block=NewObject<UStaticMeshComponent>(Wall);Wall->SetRootComponent(Block);Block->RegisterComponent();
    Block->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Block->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Block->SetCollisionResponseToAllChannels(ECR_Block);
    Block->SetWorldScale3D(FVector(.4f,4,4));Wall->SetActorLocation(Center+FVector(150,0,0));
    auto* Behind=SpawnTarget(Center+FVector(300,0,0),100000);SpawnBlast(Center);
    Check(TEXT("solid_wall_blocks_explosion"),Behind&&Behind->CurrentHP==100000);
    auto* MuzzleBlocked=HeliosYunSummer::Launch(Weapon,Player,Center-FVector(400,0,0),FVector::RightVector,Center+FVector(300,0,0),HeliosYunSummer::Tuning(false),true);
    Check(TEXT("camera_to_muzzle_obstruction_stops_launch"),MuzzleBlocked&&MuzzleBlocked->HasExploded());
    auto* Swept=HeliosYunSummer::Launch(Weapon,Player,Center-FVector(400,0,0),FVector::ForwardVector,Center-FVector(400,0,0),HeliosYunSummer::Tuning(false),true);
    const bool InitiallyFlying=Swept&&!Swept->HasExploded();if(Swept)Swept->Tick(.1f);
    Check(TEXT("fast_projectile_sweep_cannot_tunnel_through_wall"),InitiallyFlying&&Swept->HasExploded()&&Behind&&Behind->CurrentHP==100000);
    if(Behind)Behind->Destroy();Wall->Destroy();

    Check(TEXT("nonlethal_blasts_do_not_spawn_kill_effect"),Weapon->KillEffectsSpawned==0);
    auto* Kill=SpawnTarget(Center,1);const int32 Before=Weapon->TrueKills;SpawnBlast(Center);
    Check(TEXT("real_lethal_transition_counted_once"),Kill&&Kill->CurrentHP<=0&&Weapon->TrueKills==Before+1);
    Check(TEXT("real_lethal_transition_spawns_original_kill_effect"),Weapon->KillEffectsSpawned==1);
    if(Kill)Kill->Destroy();
    auto* Boss=GetWorld()->SpawnActor<AHeliosEnemyBoss>(Center,FRotator::ZeroRotator,Params);
    if(Boss){Boss->SetActorTickEnabled(false);Boss->StopShooting();Boss->CurrentHP=3600;SpawnBlast(Center);}
    Check(TEXT("grenade_damages_actual_boss_without_head_multiplier"),Boss&&Boss->CurrentHP<3600&&Boss->CurrentHP>=2879.9f);
    if(Boss)Boss->Destroy();
    Target=SpawnTarget(Player->GetActorLocation()+FVector(0,700,0),100000);
}
void AHeliosYunSummerMegatronQA::Finish()
{
    if(bFinished)return;bFinished=true;Key(EKeys::LeftMouseButton,false);Key(EKeys::LeftShift,false);
    if(bRecording)UAudioMixerBlueprintLibrary::StopRecordingOutput(this,EAudioRecordingExportType::WavFile,TEXT("audio_capture"),Out,nullptr);
    FApp::SetUnfocusedVolumeMultiplier(PreviousUnfocusedVolume);HeliosAsyncFrameCapture::Finish(Out);
    auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("passed"),Failures==0);Report->SetNumberField(TEXT("failures"),Failures);
    Report->SetArrayField(TEXT("checks"),Checks);Report->SetNumberField(TEXT("maximum_flight_actors"),MaximumFlightActors);
    Report->SetNumberField(TEXT("maximum_visible_effect_tracks"),MaximumVisibleEffectTracks);
    Report->SetNumberField(TEXT("production_enemy_observations"),EnemyObservations);
    Report->SetStringField(TEXT("scope"),FParse::Param(FCommandLine::Get(),TEXT("YunQAArmsOnly"))
        ?TEXT("Real D3D12 rendering and Slate menu/save checks, followed by 33 frozen authored FP poses for manual arm-edge/contact inspection. Group-transform checks are not automatic pixel assertions or a complete normal-input combat test.")
        :TEXT("Real D3D12 rendering, actual Slate pointer/input, independent save roundtrip, real swept projectiles and radial damage fixtures, source meshes/actions/audio, four-companion combat and WangZheKui regression. Editor game target; no packaged-mobile or perfect-original-renderer claim."));
    TArray<TSharedPtr<FJsonValue>> FrameValues;for(double Frame:Frames)FrameValues.Add(MakeShared<FJsonValueNumber>(Frame));Report->SetArrayField(TEXT("frame_seconds"),FrameValues);
    FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*(Out/TEXT("report.json")));
    Stage=99;Next=FPlatformTime::Seconds()+3;
}
void AHeliosYunSummerMegatronQA::Tick(float Dt)
{
    Super::Tick(Dt);const double Wall=FPlatformTime::Seconds();
    if(Stage==99){if(Wall>=Next)FPlatformMisc::RequestExit(false);return;}
    if(Started==0)
    {
        Started=Wall;PreviousUnfocusedVolume=FApp::GetUnfocusedVolumeMultiplier();FApp::SetUnfocusedVolumeMultiplier(1);
        FParse::Value(FCommandLine::Get(),TEXT("YunQAOut="),Out);if(Out.IsEmpty())Out=FPaths::ProjectDir()/TEXT("../qa/cfm_yun_summer_megatron/runtime");
        Out=FPaths::ConvertRelativePathToFull(Out);IFileManager::Get().MakeDirectory(*Out,true);
        FString Checkpoint;TSharedPtr<FJsonObject> Saved;
        if(FFileHelper::LoadFileToString(Checkpoint,*(Out/TEXT("travel_checkpoint.json")))&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Checkpoint),Saved)&&Saved.IsValid())
        {
            Checks=Saved->GetArrayField(TEXT("checks"));for(auto& Row:Checks)if(!Row->AsObject()->GetBoolField(TEXT("passed")))++Failures;
            MotionFrame=int32(Saved->GetNumberField(TEXT("motion_frame")));PreviousUnfocusedVolume=float(Saved->GetNumberField(TEXT("original_unfocused_volume")));Stage=90;
        }
    }
    if(Wall-Started>260){Check(TEXT("bounded_timeout"),false);Finish();return;}
    auto* PC=UGameplayStatics::GetPlayerController(this,0);auto* HUD=PC?Cast<AHeliosAdventureHUD>(PC->GetHUD()):nullptr;
    auto* Director=AHeliosAdventureDirector::Find(GetWorld());Player=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));
    if(!HUD||!Director||!Player||Wall-Started<5)return;
    auto* Session=GetGameInstance()->GetSubsystem<UHeliosYunSummerSession>();
    auto* Weapon=Cast<AHeliosYunSummerMegatronWeapon>(Player->GetEquippedWeapon());
    auto* Pets=GetWorld()->GetSubsystem<UHeliosPetWorld>();
    auto Advance=[&](int32 Value){Stage=Value;Age=0;Next=Wall+.4;Captures.Reset();};Age+=Dt;
    auto Shot=[&](){if(IsValid(Target)){FVector Eye;FRotator Look;Player->GetActorEyesViewPoint(Eye,Look);PC->SetControlRotation((Target->GetActorLocation()-Eye).Rotation());}BeforeAmmo=Weapon?Weapon->GetBulletCount():0;BeforeShots=Weapon?Weapon->ShotsFired:0;Key(EKeys::LeftMouseButton,true);};
    auto TimedCapture=[&](int32 Id,float Time,const TCHAR* Name){if(Age>=Time&&!Captures.Contains(Id)){Captures.Add(Id);Capture(Name);}};
    if(Stage>=200){if(Wall>=Next)TickArmsView(Weapon);return;}
    if(Stage==27&&Weapon&&!Player->IsDead())
    {
        FVector Eye;FRotator Look;Player->GetActorEyesViewPoint(Eye,Look);
        FCollisionQueryParams Sight(SCENE_QUERY_STAT(YunQAProductionAim),true,Player);Sight.AddIgnoredActor(Weapon);if(Pets)Pets->IgnoreAll(Sight);
        AHeliosEnemyActor* AimTarget=nullptr;float Best=FMath::Square(Weapon->ShotSettings.Range);
        for(TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It)
        {
            auto* Enemy=*It;if(Enemy->IsEnemyDead()||Enemy->ActorHasTag(TEXT("Friendly")))continue;
            const float Distance=FVector::DistSquared(Eye,Enemy->GetActorLocation());if(Distance>FMath::Square(Weapon->ShotSettings.Range))continue;
            const float Weight=Distance*(Cast<AHeliosEnemyBoss>(Enemy)?.25f:1.f);if(Weight>=Best)continue;
            FHitResult Block;if(GetWorld()->LineTraceSingleByChannel(Block,Eye,Enemy->GetActorLocation(),ECC_Visibility,Sight)&&Block.GetActor()!=Enemy)continue;
            Best=Weight;AimTarget=Enemy;
        }
        if(AimTarget)PC->SetControlRotation(FMath::RInterpTo(PC->GetControlRotation(),(AimTarget->GetActorLocation()-Eye).Rotation(),Dt,10.f));
    }
    if(Stage>=10)
    {
        Frames.Add(Dt);int32 Flights=0,Visible=0;
        for(TActorIterator<AHeliosMegatronGrenade> It(GetWorld());It;++It)if(!It->HasExploded())++Flights;
        for(TActorIterator<AHeliosSSXEffect> It(GetWorld());It;++It)Visible+=It->VisibleTrackCount();
        MaximumFlightActors=FMath::Max(MaximumFlightActors,Flights);MaximumVisibleEffectTracks=FMath::Max(MaximumVisibleEffectTracks,Visible);
        if(Wall-LastMotion>.1){LastMotion=Wall;HeliosAsyncFrameCapture::Request(Out/FString::Printf(TEXT("motion_%05d.jpg"),MotionFrame++));}
    }
    if((Stage==12||Stage==16||Stage==19||Stage==24)&&Age>.06f)Key(EKeys::LeftMouseButton,false);
    if(Stage==17&&Age>.1f)Key(EKeys::LeftShift,false);
    if(Wall<Next)return;
    switch(Stage)
    {
    case 0:
    {
        auto* Defaults=NewObject<UHeliosYunSummerSession>(GetGameInstance());
        bool DefaultSP=Defaults->GetPlayerForm()==EHeliosYunSummerForm::SP;
        for(int32 I=0;I<4;++I)DefaultSP&=Defaults->GetPetForm(I)==EHeliosYunSummerForm::SP;
        Check(TEXT("fresh_session_defaults_all_soul"),DefaultSP);Session->SetPlayerForm(EHeliosYunSummerForm::SP);
#if WITH_DEV_AUTOMATION_TESTS
        if(HeliosMenuQANavigate)HeliosMenuQANavigate(true,false);
#endif
        Advance(1);return;
    }
    case 1:ScrollToYun();Advance(2);return;
    case 2:
        Check(TEXT("actual_single_weapon_card_clicked"),ClickLabel(TEXT("云悠悠·沐夏微醺")));
        Check(TEXT("single_weapon_id_selected"),HUD->GetPendingWeaponId()==TEXT("cfm_yun_summer_megatron"));
        Check(TEXT("actual_gr_button_clicked"),ClickLabel(TEXT("保卫者")));
        Check(TEXT("gr_choice_not_overwritten_by_card"),HUD->GetYunSummerForm(false)==EHeliosYunSummerForm::GR);
        Capture(TEXT("01_equipment_gr"));Advance(3);return;
    case 3:
        Check(TEXT("actual_bl_button_clicked"),ClickLabel(TEXT("潜伏者")));
        Check(TEXT("bl_choice_not_overwritten_by_card"),HUD->GetYunSummerForm(false)==EHeliosYunSummerForm::BL);
        Capture(TEXT("02_equipment_bl"));Advance(4);return;
    case 4:
        Check(TEXT("actual_sp_button_clicked"),ClickLabel(TEXT("灵魂武者")));
        Check(TEXT("sp_choice_not_overwritten_by_card"),HUD->GetYunSummerForm(false)==EHeliosYunSummerForm::SP);
        Capture(TEXT("03_equipment_sp"));Advance(40);return;
    case 40:
        HUD->SelectPetCount(4);HUD->SelectPetSlot(0);
#if WITH_DEV_AUTOMATION_TESTS
        if(HeliosMenuQANavigate)HeliosMenuQANavigate(true,true);
#endif
        Advance(5);return;
    case 5:ScrollToYun();Advance(6);return;
    case 6:
        Check(*FString::Printf(TEXT("slot_%d_actual_character_button"),Slot),ClickLabel(TEXT("云悠悠·沐夏微醺")));
        Check(*FString::Printf(TEXT("slot_%d_actual_form_button"),Slot),ClickLabel(HeliosYunSummer::FormLabel(RosterForms[Slot])));
        Check(*FString::Printf(TEXT("slot_%d_independent_choice"),Slot),HUD->GetSelectedPet()==EHeliosPetKind::YunSummerMegatron&&Session->GetPetForm(Slot)==RosterForms[Slot]&&Session->GetPlayerForm()==EHeliosYunSummerForm::SP);
        Capture(*FString::Printf(TEXT("04_partner_slot_%d"),Slot));Advance(61);return;
    case 61:
        ++Slot;
        if(Slot<4){HUD->SelectPetSlot(Slot);Advance(6);return;}
        Advance(7);return;
    case 7:
    {
        const FString Save=Out/TEXT("forms_roundtrip.json");Check(TEXT("save_independent_forms"),Session->SaveSelection(Save));
        auto* Restored=NewObject<UHeliosYunSummerSession>(GetGameInstance());bool RestoredOK=Restored->LoadSelection(Save)&&Restored->GetPlayerForm()==EHeliosYunSummerForm::SP;
        for(int32 I=0;I<4;++I)RestoredOK&=Restored->GetPetForm(I)==RosterForms[I];Check(TEXT("restore_player_and_four_forms"),RestoredOK);
        const FString Invalid=Out/TEXT("forms_invalid.json");
        FFileHelper::SaveStringToFile(TEXT("{\"player\":0,\"pets\":[0,1,2,9]}"),*Invalid);
        bool Unchanged=!Restored->LoadSelection(Invalid)&&Restored->GetPlayerForm()==EHeliosYunSummerForm::SP;
        for(int32 I=0;I<4;++I)Unchanged&=Restored->GetPetForm(I)==RosterForms[I];Check(TEXT("invalid_form_save_rejected_atomically"),Unchanged);
        FFileHelper::SaveStringToFile(TEXT("{\"player\":0,"),*Invalid);Check(TEXT("truncated_form_save_rejected"),!Restored->LoadSelection(Invalid));
        auto* Legacy=NewObject<UHeliosPetSession>(GetGameInstance());const FString LegacyFile=Out/TEXT("legacy_pet_selection.json");
        FFileHelper::SaveStringToFile(TEXT("{\"version\":1,\"selected_kind\":6}"),*LegacyFile);
        Check(TEXT("old_wangzhekui_selection_remains_compatible"),Legacy->LoadSelection(LegacyFile)&&Legacy->GetKind(0)==EHeliosPetKind::WangZheKuiThunder);
        if(FParse::Param(FCommandLine::Get(),TEXT("YunQAMenuOnly"))){Finish();return;}
        HUD->StartSelectedRun();Director->SetActorTickEnabled(false);Director->CleanupOwnedActors();
        Player->SetCanBeDamaged(false);Player->SetActorLocation(FVector(0,-2800,102),false,nullptr,ETeleportType::TeleportPhysics);PC->SetControlRotation(FRotator(0,90,0));
        Advance(FParse::Param(FCommandLine::Get(),TEXT("YunQAArmsOnly"))?200:10);return;
    }
    case 10:
    {
        if(Age<3)return;Check(TEXT("weapon_spawned"),Weapon!=nullptr);Check(TEXT("four_companions_spawned"),Pets&&Pets->Pets.Num()==4);
        if(!Weapon||!Pets||Pets->Pets.Num()!=4){Finish();return;}
        Check(TEXT("source_fp_arms_and_gun"),Weapon->Megatron->GetSkeletalMeshAsset()&&Weapon->GetFirstPersonMesh()->GetSkeletalMeshAsset());
        bool Models=true;for(int32 I=0;I<4;++I){auto* Pet=Cast<AHeliosYunSummerMegatronPet>(Pets->Pets[I]);Models&=Pet&&Pet->bModelReady&&Pet->GetForm()==RosterForms[I]&&Pet->Megatron->GetStaticMesh()!=nullptr;}
        Check(TEXT("four_source_bodies_keep_slot_forms"),Models);SuspendPets(GetWorld(),true);
        for(int32 I=0;I<4;++I){auto* Pet=Cast<AHeliosYunSummerMegatronPet>(Pets->Pets[I]);PetTransforms.Add(Pet->GetActorTransform());Pet->SuspendCombat();Pet->GetCharacterMovement()->StopMovementImmediately();
            Pet->SetActorLocation(Player->GetActorLocation()+FVector(-300+200*I,500,0));Pet->SetActorRotation(FRotator(0,90,0));
            if(auto* Anim=Cast<UHeliosHandAnimInstance>(Pet->GetMesh()->GetAnimInstance()))Anim->SetTimelinePose(HeliosYunSummer::Animation(FString(TEXT("A_YUN_"))+HeliosYunSummer::FormCode(Pet->GetForm())+TEXT("_TP_"),TEXT("Idle")),0,true);}
        Camera=GetWorld()->SpawnActor<ACameraActor>();Camera->GetCameraComponent()->FieldOfView=60;
        Camera->SetActorLocation(Player->GetActorLocation()+FVector(0,1500,150));Camera->SetActorRotation((Player->GetActorLocation()+FVector(0,500,0)-Camera->GetActorLocation()).Rotation());PC->SetViewTarget(Camera);Advance(101);return;
    }
    case 101:Capture(TEXT("05a_four_original_forms"));Advance(102);return;
    case 102:
    {
        PC->SetViewTarget(Player);for(int32 I=0;I<4;++I)Pets->Pets[I]->SetActorTransform(PetTransforms[I]);
        BaseSpeed=Player->GetCharacterMovement()->MaxWalkSpeed/1.2f;
        Check(TEXT("soul_movement_20_percent"),Weapon->GetForm()==EHeliosYunSummerForm::SP&&FMath::Abs(Player->GetCharacterMovement()->MaxWalkSpeed-BaseSpeed*1.2f)<.1f);
        BlastFixtures(Weapon);UE_LOG(LogTemp,Display,TEXT("YUN_AUDIO_BEGIN segment=pretravel"));UAudioMixerBlueprintLibrary::StartRecordingOutput(this,220);bRecording=true;
        Capture(TEXT("05_soul_idle"));Shot();Advance(12);return;
    }
    case 12:
        if(Age<.6f)return;
        Check(TEXT("one_real_click_one_round"),Weapon&&Weapon->ShotsFired==BeforeShots+1&&Weapon->GetBulletCount()==BeforeAmmo-1);
        Check(TEXT("real_projectile_hits_enemy"),Weapon&&Weapon->HitsDelivered>=3&&Target&&Target->CurrentHP<100000);
        BeforeAmmo=Weapon->GetBulletCount();Key(EKeys::Q,true);Key(EKeys::Q,false);Advance(13);return;
    case 13:
        Check(TEXT("q_cycles_to_gr_without_refill"),Weapon&&Weapon->GetForm()==EHeliosYunSummerForm::GR&&Weapon->GetBulletCount()==BeforeAmmo&&HUD->GetYunSummerForm(false)==EHeliosYunSummerForm::GR);
        Check(TEXT("gr_removes_soul_speed"),FMath::Abs(Player->GetCharacterMovement()->MaxWalkSpeed-BaseSpeed)<.1f);
        Capture(TEXT("06_gr_idle"));Key(EKeys::R,true);Key(EKeys::R,false);Key(EKeys::Q,true);Key(EKeys::Q,false);
        Advance(14);return;
    case 14:
        if(!Captures.Contains(8)){Captures.Add(8);Check(TEXT("q_during_reload_is_deferred"),Weapon&&Weapon->IsReloading()&&Weapon->GetForm()==EHeliosYunSummerForm::GR);}
        TimedCapture(1,.45f,TEXT("07_reload_clip_out"));TimedCapture(2,1.05f,TEXT("08_reload_clip_in"));
        if(Age<2.2f||Player->IsReloading()){if(Age>5){Check(TEXT("reload_bounded_completion"),false);Finish();}return;}
        Check(TEXT("source_reload_finishes_and_applies_pending_bl"),Weapon&&Weapon->GetBulletCount()==20&&!Player->IsReloading()&&Weapon->GetForm()==EHeliosYunSummerForm::BL);
        Advance(145);return;
    case 145:Capture(TEXT("09_bl_idle"));Advance(146);return;
    case 146:Key(EKeys::G,true);Key(EKeys::G,false);Advance(15);return;
    case 15:
        TimedCapture(1,1.4f,TEXT("10_megatron_inspect"));TimedCapture(2,3.7f,TEXT("11_megatron_inspect_press"));if(Age<4)return;
        Check(TEXT("g_uses_source_show_action"),Weapon&&Weapon->IsShowing());Shot();Advance(16);return;
    case 16:
        Check(TEXT("fire_interrupts_inspect"),Weapon&&!Weapon->IsShowing()&&Weapon->ShotsFired==BeforeShots+1);
        Key(EKeys::LeftShift,true);Advance(161);return;
    case 161:
        Key(EKeys::G,true);Key(EKeys::G,false);Advance(17);return;
    case 17:
        if(!Captures.Contains(8)){Captures.Add(8);Check(TEXT("shift_g_uses_original_cup_and_cheers"),Weapon&&Weapon->Cup->GetSkeletalMeshAsset()&&Weapon->Cup->IsVisible()&&!Weapon->Megatron->IsVisible());}
        TimedCapture(1,.9f,TEXT("12_yun_cheers"));if(Age<3.7f)return;Check(TEXT("cheers_finishes"),Weapon&&!Weapon->IsShowing());Player->SetSprintingFromAutomation(false);
        BeforeAmmo=Weapon->GetBulletCount();Key(EKeys::Q,true);Key(EKeys::Q,false);Advance(18);return;
    case 18:
        Check(TEXT("full_form_cycle_preserves_ammo"),Weapon&&Weapon->GetForm()==EHeliosYunSummerForm::SP&&Weapon->GetBulletCount()==BeforeAmmo);
        Key(EKeys::R,true);Key(EKeys::R,false);Advance(181);return;
    case 181:
        if(Age<2.2f)return;BeforeShots=Weapon->ShotsFired;Key(EKeys::LeftMouseButton,true);Advance(182);return;
    case 182:
        TimedCapture(1,1.2f,TEXT("13_energy_grenades"));if(Age<10.9f)return;Key(EKeys::LeftMouseButton,false);
        Check(TEXT("held_fire_exhausts_finite_20_round_magazine"),Weapon&&Weapon->ShotsFired==BeforeShots+20&&Weapon->GetBulletCount()==0);
        Key(EKeys::Q,true);Key(EKeys::Q,false);Check(TEXT("empty_form_switch_cannot_refill"),Weapon&&Weapon->GetBulletCount()==0);
        Key(EKeys::R,true);Key(EKeys::R,false);Advance(183);return;
    case 183:
        if(Age<2.2f)return;Check(TEXT("unlimited_reserve_reload_restores_20"),Weapon&&Weapon->GetBulletCount()==20);
        if(IsValid(Target))Target->Destroy();Target=SpawnTarget(Player->GetActorLocation()+FVector(0,700,0),1);BeforeKills=Weapon->TrueKills;
        {FVector Eye;FRotator Look;Player->GetActorEyesViewPoint(Eye,Look);PC->SetControlRotation((Target->GetActorLocation()-Eye).Rotation());}Advance(184);return;
    case 184:
        Shot();
        Key(EKeys::Q,true);Key(EKeys::Q,false);Key(EKeys::Q,true);Key(EKeys::Q,false);Advance(19);return;
    case 19:
        TimedCapture(3,.45f,TEXT("13a_original_kill_emblem"));
        if(Age<2)return;Check(TEXT("actual_shot_lethal_voice"),Weapon&&Weapon->TrueKills>=BeforeKills+1&&Weapon->Voice->KillCount>0);
        Check(TEXT("actual_lethal_shot_spawns_original_kill_effect"),Weapon&&Weapon->KillEffectsSpawned>=2);
        Check(TEXT("original_kill_effect_not_covered_by_generic_badge"),Weapon&&Weapon->IsKillEffectActive()&&!VisibleWidgetType(TEXT("SHeliosKillBadge")));
        Check(TEXT("rapid_form_switch_keeps_refire_and_spent_round"),Weapon&&Weapon->ShotsFired==BeforeShots+1&&Weapon->GetBulletCount()==BeforeAmmo-1);
        if(IsValid(Target))Target->Destroy();Target=SpawnTarget(Player->GetActorLocation()+FVector(0,1000,0),100000);
        SuspendPets(GetWorld(),false);Camera=GetWorld()->SpawnActor<ACameraActor>();Camera->GetCameraComponent()->FieldOfView=65;
        Camera->SetActorLocation(Player->GetActorLocation()+FVector(650,700,180));Camera->SetActorRotation((Player->GetActorLocation()+FVector(0,-100,0)-Camera->GetActorLocation()).Rotation());PC->SetViewTarget(Camera);Advance(20);return;
    case 20:
        TimedCapture(1,4,TEXT("14_three_forms_four_partners"));TimedCapture(2,13,TEXT("15_partner_pve_reload"));if(Age<18)return;
    {
        bool Fired=true,Healthy=true;for(auto& Actor:Pets->Pets){auto* Pet=Cast<AHeliosYunSummerMegatronPet>(Actor);Fired&=Pet&&Pet->ShotsFired>0;Healthy&=Pet&&Pet->Health==Pet->Settings.MaxHealth;}
        Check(TEXT("all_four_companions_fire_independently"),Fired);Check(TEXT("four_companions_no_friendly_blast_damage"),Healthy);
        auto* Pet=Cast<AHeliosYunSummerMegatronPet>(Pets->Pets[0]);Check(TEXT("companion_finite_magazine_reloads"),Pet&&Pet->Reloads>0);
        if(Pet){Pet->SetActorLocation(Player->GetActorLocation()+FVector(0,500,0));Pet->TakeDamage(100000,FDamageEvent(),nullptr,nullptr);Check(TEXT("companion_down_uses_existing_recovery"),Pet->IsSleeping()&&Pet->Health==0);Pet->SleepRemaining=1.8f;
            Camera->SetActorLocation(Pet->GetActorLocation()+FVector(450,200,200));Camera->SetActorRotation((Pet->GetActorLocation()-FVector(0,0,55)-Camera->GetActorLocation()).Rotation());}
        Advance(21);return;
    }
    case 21:
        TimedCapture(4,.55f,TEXT("15a_original_partner_down"));
        if(!Captures.Contains(9)){Captures.Add(9);auto* Pet=Cast<AHeliosYunSummerMegatronPet>(Pets->Pets[0]);auto* Anim=Pet?Cast<UHeliosHandAnimInstance>(Pet->GetMesh()->GetAnimInstance()):nullptr;
            Check(TEXT("original_down_clip_advances_before_ground_pose"),Pet&&Pet->IsSleeping()&&Anim&&Anim->Sequence&&Anim->Sequence->GetName().Contains(TEXT("Dead_Front"))&&Anim->SampleSeconds>.1f&&Anim->SampleSeconds<1.133334f);}
        if(Age<3)return;{auto* Pet=Cast<AHeliosYunSummerMegatronPet>(Pets->Pets[0]);Check(TEXT("companion_recovers_with_model_and_form"),Pet&&!Pet->IsSleeping()&&Pet->Health==Pet->Settings.MaxHealth&&Pet->GetForm()==RosterForms[0]);}
        PC->SetViewTarget(Player);
        SuspendPets(GetWorld(),true);if(IsValid(Target))Target->Destroy();
        Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("rifle")));Advance(22);return;
    case 22:
        Check(TEXT("unload_restores_movement"),FMath::Abs(Player->GetCharacterMovement()->MaxWalkSpeed-BaseSpeed)<.1f);
        Check(TEXT("unload_has_no_old_weapon_effects"),NoOldWeaponEffects(GetWorld(),Player->GetEquippedWeapon()));
        Check(TEXT("wangzhekui_regression_equips"),Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("cfm_wangzhekui_thunder"))));Advance(23);return;
    case 23:
        if(Age<1.2f)return;Check(TEXT("wangzhekui_generic_badge_layer_remains_enabled"),VisibleWidgetType(TEXT("SHeliosKillBadge")));
        Target=SpawnTarget(Player->GetActorLocation()+FVector(0,700,0),100000);Shot();Advance(24);return;
    case 24:
        {auto* Kui=Cast<AHeliosWangZheKuiThunderWeapon>(Player->GetEquippedWeapon());Check(TEXT("wangzhekui_regression_fires_and_hits"),Kui&&Kui->ShotsFired>0&&Kui->HitPellets>0);}
        if(IsValid(Target))Target->Destroy();Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("cfm_yun_summer_megatron")));Advance(25);return;
    case 25:
        if(Age<1.2f)return;Check(TEXT("reequip_restores_selected_form"),Weapon&&Weapon->GetForm()==Session->GetPlayerForm()&&Weapon->GetBulletCount()==20);
        Shot();Advance(251);return;
    case 251:
        Key(EKeys::LeftMouseButton,false);Key(EKeys::R,true);Key(EKeys::R,false);Advance(2511);return;
    case 2511:
        Check(TEXT("source_reload_active_before_death"),Player->IsReloading());Player->SetCanBeDamaged(true);Player->TakeDamage(100000,FDamageEvent(),nullptr,nullptr);
        Check(TEXT("real_death_cancels_reload_and_speed_bonus"),Player->GetHealth()==0&&!Player->IsReloading()&&Player->GetEquipmentMovementMultiplier()==1);
        Capture(TEXT("19_actual_player_death"));Advance(252);return;
    case 252:
        Director->RestartRun();Director->SetActorTickEnabled(false);Director->CleanupOwnedActors();Advance(26);return;
    case 26:
        if(Age<3)return;Check(TEXT("restart_keeps_selected_form_and_fresh_magazine"),Weapon&&Weapon->GetForm()==Session->GetPlayerForm()&&Weapon->GetBulletCount()==20);
        {bool Forms=true;for(int32 I=0;I<Pets->Pets.Num();++I){auto* Pet=Cast<AHeliosYunSummerMegatronPet>(Pets->Pets[I]);Forms&=Pet&&Pet->GetForm()==RosterForms[I];}Check(TEXT("restart_keeps_four_independent_forms"),Pets->Pets.Num()==4&&Forms);}
        SuspendPets(GetWorld(),true);BeforeHP=Player->GetHealth();Player->SetCanBeDamaged(true);Player->SetActorLocation(Player->GetActorLocation()+FVector(0,0,1200),false,nullptr,ETeleportType::TeleportPhysics);Advance(261);return;
    case 261:
        if(Age<4)return;Check(TEXT("high_fall_has_no_damage"),!Player->GetCharacterMovement()->IsFalling()&&Player->GetHealth()==BeforeHP);
        HUD->RequestRestartConfirmation();HUD->ConfirmRestart();
#if WITH_DEV_AUTOMATION_TESTS
        if(HeliosMenuQANavigate)HeliosMenuQANavigate(true,false);
#endif
        Advance(262);return;
    case 262:ScrollToYun();Advance(263);return;
    case 263:
        Check(TEXT("menu_tracks_form_after_in_run_q_and_respawn"),HUD->GetYunSummerForm(false)==Session->GetPlayerForm()&&HUD->GetPendingWeaponId()==TEXT("cfm_yun_summer_megatron"));Capture(TEXT("20_menu_after_q_and_respawn"));
        Advance(264);return;
    case 264:
        if(FParse::Param(FCommandLine::Get(),TEXT("YunQAFixtureOnly"))){Finish();return;}
        if(FParse::Param(FCommandLine::Get(),TEXT("YunQATravel")))
        {
            if(bRecording){UAudioMixerBlueprintLibrary::StopRecordingOutput(this,EAudioRecordingExportType::WavFile,TEXT("pretravel_audio"),Out,nullptr);bRecording=false;}
            HeliosAsyncFrameCapture::Finish(Out);auto Saved=MakeShared<FJsonObject>();Saved->SetArrayField(TEXT("checks"),Checks);Saved->SetNumberField(TEXT("motion_frame"),MotionFrame);Saved->SetNumberField(TEXT("original_unfocused_volume"),PreviousUnfocusedVolume);
            FString Json;FJsonSerializer::Serialize(Saved,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*(Out/TEXT("travel_checkpoint.json")));
            HUD->SelectMap(Director->IsUrbanChallenge()?FName("HeliosAdventure"):FName("UrbanChallenge"));HUD->StartSelectedRun();Stage=98;return;
        }
        HUD->StartSelectedRun();Advance(90);return;
    case 98:return;
    case 90:
    {
        if(Age<3)return;Check(TEXT("map_travel_keeps_player_form"),Weapon&&Weapon->GetForm()==Session->GetPlayerForm()&&Weapon->GetBulletCount()==20);
        bool Forms=Pets&&Pets->Pets.Num()==4;for(int32 I=0;Pets&&I<Pets->Pets.Num();++I){auto* Pet=Cast<AHeliosYunSummerMegatronPet>(Pets->Pets[I]);Forms&=Pet&&Pet->GetForm()==RosterForms[I];}
        Check(TEXT("map_travel_keeps_four_independent_forms"),Forms);Capture(TEXT("21_after_map_travel"));BeforeKills=Weapon?Weapon->TrueKills:0;
        UE_LOG(LogTemp,Display,TEXT("YUN_AUDIO_BEGIN segment=aftertravel"));UAudioMixerBlueprintLibrary::StartRecordingOutput(this,90);bRecording=true;Player->SetCanBeDamaged(true);Director->SetActorTickEnabled(true);SuspendPets(GetWorld(),false);Key(EKeys::LeftMouseButton,true);Advance(27);return;
    }
    case 27:
        EnemyObservations+=Director->GetAliveCount();TimedCapture(1,15,TEXT("16_production_group_combat"));TimedCapture(2,55,TEXT("17_production_boss_combat"));
        if(Weapon&&Weapon->GetBulletCount()==0&&!Player->IsReloading()){Key(EKeys::R,true);Key(EKeys::R,false);}
        if(!Player->IsReloading())Key(EKeys::LeftMouseButton,true);
        if(Age<65)return;Key(EKeys::LeftMouseButton,false);
        Check(TEXT("production_enemies_observed"),EnemyObservations>0);
        Check(TEXT("player_projectiles_kill_real_production_enemies"),Weapon&&Weapon->TrueKills>BeforeKills);
        if(!Director->IsUrbanChallenge())Check(TEXT("production_boss_observed"),Director->GetBossSpawnSerial()>0);
        Check(TEXT("source_effects_and_projectile_tracks_visible"),MaximumFlightActors>0&&MaximumVisibleEffectTracks>0);Capture(TEXT("18_production_finish"));Finish();return;
    default:Check(TEXT("known_qa_stage"),false);Finish();return;
    }
}
