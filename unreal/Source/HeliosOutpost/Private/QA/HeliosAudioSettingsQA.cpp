#include "QA/HeliosAudioSettingsQA.h"
#include "Audio/HeliosAudio.h"
#include "Audio/HeliosAudioSettingsSubsystem.h"
#include "Audio/HeliosMusicSubsystem.h"
#include "Pets/HeliosPetDog.h"
#include "Weapons/HeliosP90SubstituteFireAudio.h"
#include "Weapons/HeliosSSXVoice.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Weapons/HeliosBuLianShiP90.h"
#include "Weapons/HeliosColaWeapon.h"
#include "Player/HeliosArenaCharacter.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "UI/HeliosAdventureHUD.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Components/AudioComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Sound/SoundWaveProcedural.h"
#include "Sound/SoundBase.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/App.h"
#include "Kismet/GameplayStatics.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Text/STextBlock.h"
#include "UnrealClient.h"
namespace
{
int32 AudioTravelStep=0;
TWeakObjectPtr<UAudioComponent> AudioTravelMusic;
int32 AudioTravelPlayCount=0;
bool HasLabel(const TSharedRef<SWidget>& W,const FString& L)
{if(W->GetTypeAsString()==TEXT("STextBlock")&&StaticCastSharedRef<STextBlock>(W)->GetText().ToString()==L)return true;auto* C=W->GetChildren();for(int32 I=0;C&&I<C->Num();++I)if(HasLabel(C->GetChildAt(I),L))return true;return false;}
TSharedPtr<SWidget> Widget(const TSharedRef<SWidget>& W,const FString& L,const FString& Type)
{if(!W->GetVisibility().IsVisible())return nullptr;auto* C=W->GetChildren();for(int32 I=0;C&&I<C->Num();++I)if(auto F=Widget(C->GetChildAt(I),L,Type))return F;if(W->GetTypeAsString()==Type&&(L.IsEmpty()||HasLabel(W,L)))return W;return nullptr;}
TSharedPtr<SWidget> Widget(const FString& L,const FString& Type)
{for(auto& W:FSlateApplication::Get().GetInteractiveTopLevelWindows())if(auto F=Widget(W->GetContent(),L,Type))return F;return nullptr;}
bool Press(const TSharedPtr<SWidget>& B,float X=.5f)
{
    if(!B)return false;auto& S=FSlateApplication::Get();FWidgetPath Path;if(!S.GeneratePathToWidgetUnchecked(B.ToSharedRef(),Path))return false;
    const auto G=B->GetCachedGeometry();const auto At=G.LocalToAbsolute(FVector2D(G.GetLocalSize().X*X,G.GetLocalSize().Y*.5));TSet<FKey> Down;Down.Add(EKeys::LeftMouseButton);TSet<FKey> Up;
    FPointerEvent D(FSlateApplication::CursorPointerIndex,At,At,Down,EKeys::LeftMouseButton,0,FModifierKeysState()),U(FSlateApplication::CursorPointerIndex,At,At,Up,EKeys::LeftMouseButton,0,FModifierKeysState());
    S.ReleaseAllPointerCapture();B->OnMouseEnter(G,D);auto A=B->OnMouseButtonDown(G,D);S.ProcessReply(Path,A,&Path,&D);auto Z=B->OnMouseButtonUp(G,U);S.ProcessReply(Path,Z,&Path,&U);return A.IsEventHandled()&&Z.IsEventHandled();
}
bool Button(const FString& L){return Press(Widget(L,TEXT("SButton")));}
bool Slider(EHeliosAudioBus B){auto R=Widget(UHeliosAudioSettingsSubsystem::Label(B),TEXT("SBorder"));return R&&Press(Widget(R.ToSharedRef(),TEXT(""),TEXT("SSlider")));}
}
AHeliosAudioSettingsQA::AHeliosAudioSettingsQA(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;}
void AHeliosAudioSettingsQA::Check(const FString& N,bool P){auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("name"),N);J->SetBoolField(TEXT("passed"),P);Checks.Add(MakeShared<FJsonValueObject>(J));if(!P)++Failures;UE_LOG(LogTemp,Display,TEXT("AUDIO_SETTINGS_QA %s %s"),P?TEXT("PASS"):TEXT("FAIL"),*N);}
void AHeliosAudioSettingsQA::Shot(const FString& N){FScreenshotRequest::RequestScreenshot(Out/(N+TEXT(".png")),true,false);}
void AHeliosAudioSettingsQA::Record(const FString& N){RecordingName=N;RecordingAt=FPlatformTime::Seconds();UAudioMixerBlueprintLibrary::StartRecordingOutput(this,120);}
void AHeliosAudioSettingsQA::EndRecord(){if(!RecordingName.IsEmpty()){UAudioMixerBlueprintLibrary::StopRecordingOutput(this,EAudioRecordingExportType::WavFile,RecordingName,Out);RecordingName.Empty();}}
void AHeliosAudioSettingsQA::Mark(const FString& N,int32 B)
{auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("recording"),RecordingName);J->SetStringField(TEXT("phase"),N);J->SetNumberField(TEXT("time"),FPlatformTime::Seconds()-RecordingAt);J->SetNumberField(TEXT("bus"),B);Segments.Add(MakeShared<FJsonValueObject>(J));}
void AHeliosAudioSettingsQA::QueueTone(int32 I)
{TArray<int16> Samples;Samples.SetNumUninitialized(48000);for(int32 K=0;K<48000;++K)Samples[K]=int16(1500*FMath::Sin(2*PI*(240+I*170)*double(ToneSamples[I]++)/48000));Waves[I]->QueueAudio(reinterpret_cast<uint8*>(Samples.GetData()),Samples.Num()*2);}
void AHeliosAudioSettingsQA::Finish(bool Exit)
{EndRecord();auto J=MakeShared<FJsonObject>();J->SetBoolField(TEXT("passed"),Failures==0);J->SetArrayField(TEXT("checks"),Checks);J->SetArrayField(TEXT("segments"),Segments);J->SetStringField(TEXT("scope"),TEXT("Actual Slate controls, live UE sound-class mix, continuing procedural sources, existing playlist and approved P90 player/companion sources. WAV analysis required separately."));FString T;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&T));FFileHelper::SaveStringToFile(T,*(Out/TEXT("audio_report.json")));Finished=true;if(Exit)FPlatformMisc::RequestExit(false);}
void AHeliosAudioSettingsQA::Tick(float Dt)
{
    Super::Tick(Dt);if(Finished)return;const double Now=FPlatformTime::Seconds();
    if(!Started){Started=Now;Next=Now+6;FParse::Value(FCommandLine::Get(),TEXT("MultiQAOut="),Out);Out/=TEXT("audio");if(FParse::Param(FCommandLine::Get(),TEXT("HeliosAudioReloadQA")))Out/=GetWorld()->GetMapName();IFileManager::Get().MakeDirectory(*Out,true);FApp::SetUnfocusedVolumeMultiplier(1);}
    for(int32 I=0;I<Waves.Num();++I)if(Waves[I]->GetAvailableAudioByteCount()<48000)QueueTone(I);
    if(Now-Started>240){Check(TEXT("bounded_timeout"),false);Finish();return;}
    auto* P=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));auto* H=P&&P->GetController()?Cast<AHeliosAdventureHUD>(Cast<APlayerController>(P->GetController())->GetHUD()):nullptr;
    auto* A=UHeliosAudioSettingsSubsystem::Get(this);auto* M=GetGameInstance()->GetSubsystem<UHeliosMusicSubsystem>();auto* D=AHeliosAdventureDirector::Find(GetWorld());auto* R=GetWorld()->GetSubsystem<UHeliosPetWorld>();
    if(!P||!H||!A||!D||!M||!M->GetMusicComponent())return;
    if(Stage==22&&Now>=NextShot){NextShot=Now+.15;
        if(auto* F=P->GetEquippedWeapon()->FindComponentByClass<UHeliosP90SubstituteFireAudio>())F->PlayShot(P->GetActorLocation());
        for(auto& Pet:R->Pets)if(auto* F=Pet->FindComponentByClass<UHeliosP90SubstituteFireAudio>())F->PlayShot(Pet->GetActorLocation());}
    if(Now<Next)return;
    auto Wait=[&](int32 S,float T){Stage=S;Next=Now+T;};
    switch(Stage)
    {
    case 0:
        if(FParse::Param(FCommandLine::Get(),TEXT("HeliosLoopSourceOnlyQA")))
        {
            A->ResetDefaults();A->SetEnabled(EHeliosAudioBus::Music,false);
            H->SelectWeapon(TEXT("rifle"));H->SelectPetCount(0);H->StartSelectedRun();D->SetActorTickEnabled(false);P->SetCanBeDamaged(false);Wait(147,4);return;
        }
        if(FParse::Param(FCommandLine::Get(),TEXT("HeliosAudioReloadQA")))
        {for(int32 I=0;I<int32(EHeliosAudioBus::Count);++I){Check(FString::Printf(TEXT("saved_volume_%d"),I),FMath::IsNearlyEqual(A->Volume(EHeliosAudioBus(I)),.37f,.001f));Check(FString::Printf(TEXT("saved_switch_%d"),I),A->Enabled(EHeliosAudioBus(I))==(I%2==0));}if(AudioTravelStep)Check(TEXT("map_travel_keeps_current_playlist_component"),M->GetMusicComponent()==AudioTravelMusic&&M->GetPlayCount()==AudioTravelPlayCount);Button(TEXT("声音设置"));Wait(99,1);return;}
        A->ResetDefaults();Check(TEXT("actual_open_audio_menu"),Button(TEXT("声音设置")));Wait(1,1);return;
    case 1:
        Shot(TEXT("audio_default"));Wait(101,.5f);return;
    case 101:
        for(int32 I=0;I<int32(EHeliosAudioBus::Count);++I){const auto B=EHeliosAudioBus(I);Check(FString::Printf(TEXT("actual_slider_%d_half"),I),Slider(B)&&FMath::IsNearlyEqual(A->Volume(B),.5f,.001f));Check(FString::Printf(TEXT("actual_switch_%d_off"),I),Button(FString(A->Label(B))+TEXT(" · 开"))&&!A->Enabled(B));Check(FString::Printf(TEXT("mute_keeps_slider_%d"),I),FMath::IsNearlyEqual(A->Volume(B),.5f,.001f));Check(FString::Printf(TEXT("actual_switch_%d_on"),I),Button(FString(A->Label(B))+TEXT(" · 关"))&&A->Enabled(B));}Wait(2,1);return;
    case 2:
        Shot(TEXT("audio_half"));Wait(102,.5f);return;
    case 102:Check(TEXT("actual_reset_defaults"),Button(TEXT("恢复默认")));Wait(3,1);return;
    case 3:
        for(int32 I=0;I<int32(EHeliosAudioBus::Count);++I)Check(FString::Printf(TEXT("reset_default_%d"),I),A->Enabled(EHeliosAudioBus(I))&&FMath::IsNearlyEqual(A->Volume(EHeliosAudioBus(I)),I==1?.2f:1.f,.001f));
        Check(TEXT("actual_back_to_original_menu"),Button(TEXT("←  返回")));M->GetMusicComponent()->SetPaused(true);
        for(int32 I=0;I<int32(EHeliosAudioBus::Count);++I){A->SetVolume(EHeliosAudioBus(I),1);A->SetEnabled(EHeliosAudioBus(I),true);}
        Record(TEXT("bus_tones"));
        for(int32 I=1;I<int32(EHeliosAudioBus::Count);++I){auto* W=NewObject<USoundWaveProcedural>(this,*FString::Printf(TEXT("QATone_%d"),I));W->SetSampleRate(48000);W->NumChannels=1;W->Duration=INDEFINITELY_LOOPING_DURATION;W->bLooping=true;W->VirtualizationMode=EVirtualizationMode::PlayWhenSilent;Waves.Add(W);ToneSamples.Add(0);QueueTone(I-1);QueueTone(I-1);
            UAudioComponent* C=nullptr;if(I==int32(EHeliosAudioBus::Environment))C=UGameplayStatics::SpawnSound2D(this,W,1,1,0,nullptr,false,false);else{C=HeliosAudio::CreateSound2D(this,W,1,1,0,nullptr,false,false);A->RouteTo(C,EHeliosAudioBus(I));if(C)C->Play();}Tones.Add(C);Check(FString::Printf(TEXT("continuing_tone_%d_started"),I),C&&C->IsPlaying());}
        Mark(TEXT("all_full"));Bus=1;Wait(4,3);return;
    case 4:A->SetVolume(EHeliosAudioBus(Bus),.5);Mark(TEXT("half"),Bus);Wait(5,2);return;
    case 5:A->SetEnabled(EHeliosAudioBus(Bus),false);Mark(TEXT("muted"),Bus);Wait(6,2);return;
    case 6:A->SetEnabled(EHeliosAudioBus(Bus),true);A->SetVolume(EHeliosAudioBus(Bus),1);Mark(TEXT("restored"),Bus);if(++Bus<int32(EHeliosAudioBus::Count)){Wait(4,1);return;}Wait(7,2);return;
    case 7:A->SetVolume(EHeliosAudioBus::Master,.5);Mark(TEXT("master_half"));Wait(8,2);return;
    case 8:A->SetEnabled(EHeliosAudioBus::Master,false);Mark(TEXT("master_muted"));Wait(9,2);return;
    case 9:A->SetEnabled(EHeliosAudioBus::Master,true);A->SetVolume(EHeliosAudioBus::Master,1);Mark(TEXT("master_restored"));Wait(10,2);return;
    case 10:
        for(int32 I=0;I<Tones.Num();++I){Check(FString::Printf(TEXT("loop_%d_survived_live_mutes"),I+1),Tones[I]&&Tones[I]->IsPlaying());if(Tones[I])Tones[I]->Stop();}EndRecord();Waves.Empty();Tones.Empty();
        A->ResetDefaults();M->GetMusicComponent()->SetPaused(false);MusicBefore=M->GetMusicComponent();MusicPlayCount=M->GetPlayCount();MusicSeconds=M->GetPlaybackSeconds();Record(TEXT("actual_music"));Mark(TEXT("music_on"));Wait(11,3);return;
    case 11:A->SetEnabled(EHeliosAudioBus::Music,false);Mark(TEXT("music_off"));Wait(12,3);return;
    case 12:Check(TEXT("existing_music_continues_while_muted"),M->GetMusicComponent()==MusicBefore&&MusicBefore->IsPlaying()&&M->GetPlayCount()==MusicPlayCount);A->SetEnabled(EHeliosAudioBus::Music,true);Mark(TEXT("music_restored"));Wait(13,3);return;
    case 13:
        Check(TEXT("music_unmutes_without_restart"),M->GetMusicComponent()==MusicBefore&&M->GetPlayCount()==MusicPlayCount&&M->GetPlaybackSeconds()>MusicSeconds);EndRecord();A->SetEnabled(EHeliosAudioBus::Music,false);
        H->SelectWeapon(TEXT("cfm_bulianshi_p90"));H->SelectPetCount(4);for(int32 I=0;I<4;++I){H->SelectPetSlot(I);H->SelectPet(EHeliosPetKind::BuLianShiP90);}H->StartSelectedRun();D->SetActorTickEnabled(false);P->SetCanBeDamaged(false);Wait(20,4);return;
    case 20:
        Check(TEXT("real_p90_weapon_and_four_partners"),P->GetEquippedWeapon()&&Cast<AHeliosBuLianShiP90Weapon>(P->GetEquippedWeapon())&&R->Pets.Num()==4);if(R->Pets.Num()!=4){Finish();return;}
        for(auto& Pet:R->Pets){Pet->SuspendCombat();Pet->SetActorTickEnabled(false);Pet->GetCharacterMovement()->DisableMovement();Pet->SetActorLocation(P->GetActorLocation()+FVector((Pet->RosterSlot%2?1:-1)*180,(Pet->RosterSlot/2?1:-1)*130,8),false);}
        FireCase=0;Wait(21,2);return;
    case 21:
        A->SetEnabled(EHeliosAudioBus::Weapon,FireCase!=1&&FireCase!=3);A->SetEnabled(EHeliosAudioBus::Companion,FireCase!=0&&FireCase!=3);Wait(121,.25f);return;
    case 121:Record(FString::Printf(TEXT("p90_%d"),FireCase));NextShot=Now;Wait(22,3);return;
    case 22:
        if(auto* F=P->GetEquippedWeapon()->FindComponentByClass<UHeliosP90SubstituteFireAudio>())for(auto& C:F->PlayingChannels())if(C.IsValid())Check(TEXT("player_A_15854_weapon_bus"),C->SoundClassOverride==A->BusClass(EHeliosAudioBus::Weapon));
        for(auto& Pet:R->Pets)if(auto* F=Pet->FindComponentByClass<UHeliosP90SubstituteFireAudio>())for(auto& C:F->PlayingChannels())if(C.IsValid())Check(TEXT("partner_B_15855_companion_bus"),C->SoundClassOverride==A->BusClass(EHeliosAudioBus::Companion));
        Wait(23,1.4);return;
    case 23:EndRecord();if(++FireCase<4){Wait(21,1.5);return;}FireCase=0;Wait(24,2);return;
    case 24:
        A->SetEnabled(EHeliosAudioBus::Weapon,false);A->SetEnabled(EHeliosAudioBus::Companion,FireCase!=0&&FireCase!=3);A->SetEnabled(EHeliosAudioBus::CharacterVoice,FireCase!=1&&FireCase!=3);
        Wait(124,.25f);return;
    case 124:
        Record(FString::Printf(TEXT("voice_%d"),FireCase));
        if(auto* V=P->GetEquippedWeapon()->FindComponentByClass<UHeliosSSXVoiceComponent>()){V->Stop();V->Play(TEXT("Kill"));}
        for(auto& Pet:R->Pets)if(auto* V=Pet->FindComponentByClass<UHeliosSSXVoiceComponent>()){V->Stop();V->Play(TEXT("Kill"));}
        Wait(25,3);return;
    case 25:
        EndRecord();if(auto* V=P->GetEquippedWeapon()->FindComponentByClass<UHeliosSSXVoiceComponent>())V->Stop();for(auto& Pet:R->Pets)if(auto* V=Pet->FindComponentByClass<UHeliosSSXVoiceComponent>())V->Stop();
        if(++FireCase<4){Wait(24,1);return;}
        A->SetEnabled(EHeliosAudioBus::Weapon,true);A->SetEnabled(EHeliosAudioBus::Companion,true);A->SetEnabled(EHeliosAudioBus::CharacterVoice,true);
        // Save non-default values with alternating switches for a separate fresh process.
        for(int32 I=0;I<int32(EHeliosAudioBus::Count);++I){A->SetVolume(EHeliosAudioBus(I),.37f);A->SetEnabled(EHeliosAudioBus(I),I%2==0);}A->FlushSettings();H->RequestRestartConfirmation();H->ConfirmRestart();Wait(98,1);return;
    case 98:Button(TEXT("声音设置"));Wait(99,1);return;
    case 99:Shot(TEXT("audio_saved"));Wait(100,1);return;
    case 100:
        if(FParse::Param(FCommandLine::Get(),TEXT("HeliosAudioReloadQA"))&&!AudioTravelStep){AudioTravelStep=1;AudioTravelMusic=M->GetMusicComponent();AudioTravelPlayCount=M->GetPlayCount();Finish(false);UGameplayStatics::OpenLevel(this,TEXT("/Game/Helios/Maps/HeliosAdventure"));return;}
        Finish();return;
    case 147:
    {
        const FTransform Transform(FRotator::ZeroRotator,P->GetActorLocation()+FVector(150,0,0));
        auto* Can=GetWorld()->SpawnActorDeferred<AHeliosColaProjectile>(AHeliosColaProjectile::StaticClass(),Transform,P,P,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Can){Check(TEXT("actual_cola_source_spawned"),false);Finish();return;}
        auto* Definition=NewObject<UHeliosColaWeaponDefinition>(this);Definition->FuseSeconds=120;Definition->ProjectileSpeed=0;Definition->UpwardSpeed=0;
        Can->Configure(FVector::ForwardVector,Definition);Can->SetActorEnableCollision(false);Can->FinishSpawning(Transform);Can->SetActorTickEnabled(false);
        if(auto* Movement=Can->FindComponentByClass<UProjectileMovementComponent>()){Movement->StopMovementImmediately();Movement->SetComponentTickEnabled(false);}
        auto* C=Can->FindComponentByClass<UAudioComponent>();Tones.Add(C);
        Check(TEXT("actual_cola_hiss_original_asset_and_weapon_bus"),C&&C->Sound&&C->Sound->GetName()==TEXT("SFX_ColaHiss")&&C->SoundClassOverride==A->BusClass(EHeliosAudioBus::Weapon));
        Check(TEXT("actual_cola_hiss_survives_silence_policy"),C&&C->Sound&&C->Sound->VirtualizationMode==EVirtualizationMode::PlayWhenSilent);
        OriginalSourceDuration=C&&C->Sound?C->Sound->GetDuration():0;OriginalSourceLooping=C&&C->Sound&&C->Sound->IsLooping();
        SourcePhaseSeconds=OriginalSourceLooping?.5f:FMath::Clamp((OriginalSourceDuration-.2f)/3.f,.1f,.5f);
        UE_LOG(LogTemp,Display,TEXT("HELIOS_AUDIO_SOURCE original_hiss_looping=%d duration=%.3f phase_seconds=%.3f"),OriginalSourceLooping,OriginalSourceDuration,SourcePhaseSeconds);
        Record(TEXT("actual_loop"));Mark(TEXT("loop_on"));Wait(150,SourcePhaseSeconds);return;
    }
    case 150:A->SetEnabled(EHeliosAudioBus::Weapon,false);Mark(TEXT("loop_off"));Wait(151,SourcePhaseSeconds);return;
    case 151:Check(TEXT("actual_cola_source_keeps_same_playing_component"),Tones[0]&&Tones[0]->IsPlaying());A->SetEnabled(EHeliosAudioBus::Weapon,true);Mark(TEXT("loop_restored"));Wait(152,SourcePhaseSeconds);return;
    case 152:
        Check(TEXT("actual_cola_source_restores_without_new_component"),Tones[0]&&Tones[0]->IsPlaying());EndRecord();if(Tones[0])Tones[0]->GetOwner()->Destroy();Tones.Empty();
        AudioTestOwner=GetWorld()->SpawnActor<AActor>();
        if(auto* Fire=NewObject<UHeliosP90SubstituteFireAudio>(AudioTestOwner))
        {Fire->RegisterComponent();Fire->Initialize(false,.08f);Fire->PlayShot(FVector::ZeroVector);for(auto& C:Fire->PlayingChannels())if(C.IsValid())Tones.Add(C.Get());}
        Check(TEXT("p90_A_tail_uses_original_world_ownership"),Tones.Num()==1&&Tones[0]&&Tones[0]->GetOwner()==GetWorld()->GetWorldSettings()&&!Tones[0]->bStopWhenOwnerDestroyed);Wait(153,.15f);return;
    case 153:AudioTestOwner->Destroy();AudioTestOwner=nullptr;Wait(154,.2f);return;
    case 154:Check(TEXT("p90_A_tail_finishes_after_source_owner_destroyed"),Tones.Num()==1&&IsValid(Tones[0])&&Tones[0]->IsPlaying());Wait(155,1.5f);return;
    case 155:Check(TEXT("p90_A_tail_auto_ends_without_audio_timer"),Tones.Num()==1&&(!IsValid(Tones[0])||!Tones[0]->IsPlaying()));Finish();return;
    }
}
