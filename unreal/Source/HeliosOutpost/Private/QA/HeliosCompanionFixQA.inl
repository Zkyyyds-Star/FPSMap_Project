// Bounded production-spawn fixture for the three companion regressions.
// Included by the existing flow runner so it shares the same isolated launcher.
#include "Pets/HeliosHumanoidPet.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Player/HeliosCharacterDefinition.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"

namespace
{
struct FCompanionFixState
{
    double Started=0,StageAt=0,NextCapture=0;
    int32 Stage=0,Failures=0,WeaponIndex=0,Frames=0,PoseFailures=0,HistoryFailures=0,CaptureIndex=0;
    FString Out;
    TArray<TSharedPtr<FJsonValue>> Checks,Samples;
    TArray<FName> Weapons;
    TArray<TWeakObjectPtr<AHeliosPetCompanion>> Actors;
    TMap<TWeakObjectPtr<AHeliosHumanoidPet>,TArray<FTransform>> PreviousBones;
    TMap<TWeakObjectPtr<AHeliosHumanoidPet>,uint32> PreviousCommits;
    TArray<int32> MovingFrames={0,0,0,0};
    TWeakObjectPtr<ACameraActor> Camera;
    FHeliosLoadout PlayerLoadout,BeforeRejected;
    bool Finished=false;
};
FCompanionFixState& CompanionFix(){static FCompanionFixState S;return S;}
void FixReport()
{
    auto& S=CompanionFix();if(S.Out.IsEmpty())return;
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("status"),S.Finished?TEXT("complete"):TEXT("running"));
    J->SetBoolField(TEXT("passed"),S.Finished&&S.Failures==0);J->SetNumberField(TEXT("failures"),S.Failures);
    J->SetArrayField(TEXT("checks"),S.Checks);J->SetArrayField(TEXT("samples"),S.Samples);
    J->SetNumberField(TEXT("pose_frames"),S.Frames);J->SetNumberField(TEXT("pose_failures"),S.PoseFailures);
    J->SetNumberField(TEXT("previous_pose_failures"),S.HistoryFailures);J->SetNumberField(TEXT("continuous_captures"),S.CaptureIndex);
    J->SetStringField(TEXT("scope"),TEXT("Production companion spawning and AI; normal launcher quality, motion blur and temporal AA remain enabled; real viewport continuous frames. No mesh/rig editing."));
    FString Text;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Text));
    FFileHelper::SaveStringToFile(Text,*(S.Out/TEXT("companions.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
void FixCheck(const FString& Name,bool Passed)
{
    auto& S=CompanionFix();auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("name"),Name);J->SetBoolField(TEXT("passed"),Passed);
    S.Checks.Add(MakeShared<FJsonValueObject>(J));if(!Passed)++S.Failures;
    UE_LOG(LogTemp,Display,TEXT("COMPANION_FIX_QA %s %s"),*Name,Passed?TEXT("PASS"):TEXT("FAIL"));FixReport();
}
void FixFinish()
{
    auto& S=CompanionFix();S.Finished=true;FixReport();
    UE_LOG(LogTemp,Display,TEXT("COMPANION_FIX_QA_COMPLETE failures=%d checks=%d captures=%d"),S.Failures,S.Checks.Num(),S.CaptureIndex);
    FPlatformMisc::RequestExitWithStatus(false,S.Failures?1:0);
}
bool FixVisible(const AHeliosPetCompanion* Pet)
{
    return IsValid(Pet)&&Pet->bModelReady&&Pet->GetMesh()->GetSkeletalMeshAsset()&&Pet->GetMesh()->IsVisible()&&!Pet->GetMesh()->bHiddenInGame;
}
void TickCompanionFixQA(AActor* Runner,float Dt)
{
    auto& S=CompanionFix();if(S.Finished)return;auto* World=Runner->GetWorld();
    const double Now=FPlatformTime::Seconds();
    if(!S.Started)
    {
        S.Started=S.StageAt=Now;FParse::Value(FCommandLine::Get(),TEXT("HeliosLoadoutOut="),S.Out);
        IFileManager::Get().MakeDirectory(*S.Out,true);IFileManager::Get().MakeDirectory(*(S.Out/TEXT("continuous")),true);
        for(const auto* D:UHeliosWeaponRegistry::GetAvailableWeapons())if(D->WeaponId!=TEXT("mech")&&D->WeaponId!=TEXT("fighter"))S.Weapons.Add(D->WeaponId);
        FixCheck(TEXT("thirteen_retained_primaries"),S.Weapons.Num()==13);
        FixCheck(TEXT("no_motion_blur_override"),!FParse::Param(FCommandLine::Get(),TEXT("HeliosNoMotionBlurQA")));
    }
    if(Now-S.Started>300){FixCheck(TEXT("bounded_timeout"),false);FixFinish();return;}
    auto* PC=UGameplayStatics::GetPlayerController(World,0);auto* Player=PC?Cast<AHeliosArenaCharacter>(PC->GetPawn()):nullptr;
    auto* HUD=PC?Cast<AHeliosAdventureHUD>(PC->GetHUD()):nullptr;auto* Director=AHeliosAdventureDirector::Find(World);
    auto* Roster=World->GetSubsystem<UHeliosPetWorld>();auto* Session=World->GetGameInstance()->GetSubsystem<UHeliosPetSession>();
    if(!Player||!HUD||!Director||!Roster||!Session)return;
    const double Age=Now-S.StageAt;
    auto Advance=[&](int32 Stage){S.Stage=Stage;S.StageAt=Now;FixReport();};
    // Suppress arbitrary encounter spawning; the four actors keep their normal
    // movement, navigation, recovery and hidden weapon animation ticks.
    if(S.Stage>0)Director->SetActorTickEnabled(false);
    Player->SetCanBeDamaged(false);
    if(S.Stage==0)
    {
        if(Age<3||HUD->GetMenuState()!=EHeliosAdventureMenu::WeaponSelect)return;
        Session->SetCount(4);const FName Characters[]={TEXT("cfm_chan"),TEXT("cfm_sunshangxiang"),TEXT("cfm_zhongxiaokui"),TEXT("cfm_yun_summer")};
        for(int32 I=0;I<4;++I){auto L=FHeliosLoadout::MakeDefault();L.CharacterId=Characters[I];if(I==3)L.CharacterVariantId=TEXT("SP");FixCheck(FString::Printf(TEXT("initial_slot_%d_accepted"),I),Session->SelectLoadout(I,L));}
        S.PlayerLoadout=HUD->GetPendingLoadout();HUD->StartSelectedRun();FixCheck(TEXT("normal_start_selected_run"),!Director->IsSelectingWeapon());
        S.Camera=World->SpawnActor<ACameraActor>();if(S.Camera.IsValid()){S.Camera->GetCameraComponent()->FieldOfView=80;PC->SetViewTarget(S.Camera.Get());}
        Advance(1);return;
    }
    if(S.Stage==1&&Age>3)
    {
        bool Ready=Roster->Pets.Num()==4;for(int32 I=0;I<4;++I){Ready&=Roster->Pets.IsValidIndex(I)&&FixVisible(Roster->Pets[I]);if(Roster->Pets.IsValidIndex(I)&&IsValid(Roster->Pets[I])){S.Actors.Add(Roster->Pets[I]);auto* P=Cast<AHeliosHumanoidPet>(Roster->Pets[I]);if(P)Runner->AddTickPrerequisiteComponent(P->GetCharacterPresentation());}}
        FixCheck(TEXT("four_visible_characters_include_chan"),Ready);if(!Ready){FixFinish();return;}
        Player->GetController()->SetControlRotation(FRotator(0,90,0));Advance(2);return;
    }
    if(S.Stage==2)
    {
        if(Age<8)Player->AddMovementInput(FVector(0,1,0));
        else if(Age<16)Player->AddMovementInput(FVector(1,0,0));
        else if(Age<24)Player->AddMovementInput(FVector(0,-1,0));
        else if(Age<32)Player->AddMovementInput(FVector(-1,0,0));
        FVector Center=FVector::ZeroVector;int32 Visible=0;
        for(int32 I=0;I<4;++I)if(Roster->Pets.IsValidIndex(I))if(auto* Pet=Cast<AHeliosHumanoidPet>(Roster->Pets[I]))
        {
            Center+=Pet->GetActorLocation();++Visible;auto* Presentation=Pet->GetCharacterPresentation();auto* Mesh=Pet->GetMesh();
            const uint32 Count=Presentation->GetPoseCommitCount();const auto* LastCount=S.PreviousCommits.Find(Pet);
            if(Presentation->GetPoseCommitFrame()!=GFrameCounter||(LastCount&&Count!=*LastCount+1))++S.PoseFailures;
            if(const auto* Previous=S.PreviousBones.Find(Pet))
            {
                const auto& Actual=Mesh->GetPreviousComponentTransformsArray();bool Match=Previous->Num()==Actual.Num();
                for(int32 B=0;Match&&B<Actual.Num();++B)Match=(*Previous)[B].Equals(Actual[B],.001f);
                if(!Match)++S.HistoryFailures;
            }
            S.PreviousBones.Add(Pet,Mesh->GetComponentSpaceTransforms());S.PreviousCommits.Add(Pet,Count);
            if(Pet->GetVelocity().Size2D()>30)++S.MovingFrames[I];
        }
        ++S.Frames;if(Visible)Center/=Visible;
        if(S.Camera.IsValid())
        {
            const int32 FocusSlot=FMath::Min(3,int32(Age/8));
            const auto* FocusPet=Roster->Pets.IsValidIndex(FocusSlot)?Roster->Pets[FocusSlot].Get():nullptr;
            const FVector Focus=(FocusPet?FocusPet->GetActorLocation():Center)+FVector(0,0,45);
            FVector Eye=Focus+FVector(-450,-300,130);float BestDistance=0;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(CompanionFixCamera),false);
            Query.AddIgnoredActor(Player);for(const auto& Pet:Roster->Pets)if(IsValid(Pet))Query.AddIgnoredActor(Pet);
            for(const FVector Offset:{FVector(-450,-300,130),FVector(450,-300,130),FVector(-450,300,130),FVector(450,300,130),FVector(0,-350,45),FVector(0,350,45)})
            {
                FHitResult Hit;FVector Candidate=Focus+Offset;
                if(World->LineTraceSingleByChannel(Hit,Focus,Candidate,ECC_WorldStatic,Query))Candidate=Hit.Location-Offset.GetSafeNormal()*30;
                const float Distance=FVector::Dist(Focus,Candidate);
                if(Distance>BestDistance){BestDistance=Distance;Eye=Candidate;}
            }
            S.Camera->SetActorLocationAndRotation(Eye,(Focus-Eye).Rotation());S.Camera->GetCameraComponent()->FieldOfView=60;
        }
        if(Now>=S.NextCapture)
        {
            const FString Name=FString::Printf(TEXT("continuous/frame_%04d.png"),S.CaptureIndex++);FScreenshotRequest::RequestScreenshot(S.Out/Name,true,false);S.NextCapture=Now+.125;
            auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("frame"),GFrameCounter);Row->SetNumberField(TEXT("age"),Age);Row->SetStringField(TEXT("file"),Name);
            TArray<TSharedPtr<FJsonValue>> Positions;for(const auto& Pet:Roster->Pets)if(IsValid(Pet))Positions.Add(MakeShared<FJsonValueString>(Pet->GetActorLocation().ToString()));Row->SetArrayField(TEXT("positions"),Positions);S.Samples.Add(MakeShared<FJsonValueObject>(Row));
        }
        if(Age<38)return;
        FixCheck(TEXT("exactly_one_final_pose_each_frame"),S.Frames>60&&S.PoseFailures==0);
        FixCheck(TEXT("previous_bones_are_previous_frame"),S.Frames>60&&S.HistoryFailures==0);
        for(int32 I=0;I<4;++I)FixCheck(FString::Printf(TEXT("slot_%d_actual_walking_observed"),I),S.MovingFrames[I]>10);
        FixCheck(TEXT("continuous_normal_quality_recording"),S.CaptureIndex>30);
        FixCheck(TEXT("normal_temporal_aa_quality"),IConsoleManager::Get().FindConsoleVariable(TEXT("sg.AntiAliasingQuality"))->GetInt()==3);
        FixCheck(TEXT("normal_postprocess_quality"),IConsoleManager::Get().FindConsoleVariable(TEXT("sg.PostProcessQuality"))->GetInt()==2);
        for(const auto& Pet:Roster->Pets)if(IsValid(Pet))Pet->Health=170;
        Advance(3);return;
    }
    if(S.Stage==3)
    {
        const FName Id=S.Weapons[S.WeaponIndex];
        for(int32 I=0;I<4;++I)FixCheck(FString::Printf(TEXT("slot_%d_primary_%s_selection"),I,*Id.ToString()),Session->SelectWeaponSlot(I,Id));
        Advance(4);return;
    }
    if(S.Stage==4&&Age>.8)
    {
        const FName Id=S.Weapons[S.WeaponIndex];
        for(int32 I=0;I<4;++I)
        {
            auto* Pet=Roster->Pets.IsValidIndex(I)?Cast<AHeliosHumanoidPet>(Roster->Pets[I]):nullptr;
            FixCheck(FString::Printf(TEXT("slot_%d_primary_%s_runtime"),I,*Id.ToString()),Pet&&S.Actors[I]==Pet&&FixVisible(Pet)&&Pet->Loadout==Session->GetLoadout(I)&&Pet->GetEquippedWeapon()&&Pet->Health<=170);
        }
        FixCheck(TEXT("runtime_primary_keeps_player"),Player->GetLoadout()==S.PlayerLoadout);
        if(++S.WeaponIndex<S.Weapons.Num()){Advance(3);return;}
        auto Invalid=Session->GetLoadout(0);Invalid.SecondaryWeaponId=TEXT("invalid");S.BeforeRejected=Session->GetLoadout(0);
        FixCheck(TEXT("invalid_selection_has_reason_and_keeps_pair"),!Session->SelectLoadout(0,Invalid)&&!Session->LastSelectionError.IsEmpty()&&Session->GetLoadout(0)==S.BeforeRejected);
        // Inject a stale runtime request to exercise the world swap rejection;
        // only this isolated in-memory fixture bypasses the validated menu.
        Session->SelectedLoadouts[0]=Invalid;Advance(5);return;
    }
    if(S.Stage==5&&Age>.8)
    {
        auto* Pet=Cast<AHeliosHumanoidPet>(Roster->Pets[0]);
        FixCheck(TEXT("failed_runtime_swap_keeps_existing_actor"),Pet&&S.Actors[0]==Pet&&Pet->Loadout==S.BeforeRejected&&FixVisible(Pet)&&Pet->Health<=170);
        Session->SelectedLoadouts[0]=S.BeforeRejected;Session->SetCount(1);FixCheck(TEXT("solo_chan_default_selected"),Session->SelectLoadout(0,FHeliosLoadout::MakeDefault()));Advance(6);return;
    }
    if(S.Stage==6&&Age>2)
    {
        FixCheck(TEXT("solo_chan_visible"),Roster->Pets.Num()==1&&FixVisible(Roster->Pets[0])&&Roster->Pets[0]->Loadout==FHeliosLoadout::MakeDefault());
        if(!Roster->Pets.Num()||!IsValid(Roster->Pets[0])){FixFinish();return;}
        Roster->Pets[0]->Restore(0,.6f);FixCheck(TEXT("chan_enters_down_state"),Roster->Pets[0]->IsSleeping());Advance(7);return;
    }
    if(S.Stage==7&&Age>5)
    {
        FixCheck(TEXT("chan_down_recovery_remains_visible"),FixVisible(Roster->Pets[0])&&!Roster->Pets[0]->IsSleeping()&&Roster->Pets[0]->Health==Roster->Pets[0]->Settings.MaxHealth);
        Director->RestartRun();Advance(8);return;
    }
    if(S.Stage==8&&Age>3)
    {
        FixCheck(TEXT("chan_restart_visible"),Roster->Pets.Num()==1&&FixVisible(Roster->Pets[0])&&Roster->Pets[0]->Loadout==FHeliosLoadout::MakeDefault());
        FixFinish();return;
    }
}
}
