#include "QA/HeliosKillFeedbackQA.h"
#include "Combat/HeliosKillFeedbackSubsystem.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "Player/HeliosArenaCharacter.h"
#include "Boss/HeliosEnemyBoss.h"
#include "UI/HeliosAdventureHUD.h"
#include "Pets/HeliosPetDog.h"
#include "Town/HeliosTownSubsystem.h"
#include "Weapons/HeliosCombatWeapons.h"
#include "AudioMixerBlueprintLibrary.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "ImageUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

AHeliosKillFeedbackQA::AHeliosKillFeedbackQA()
{ PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.bTickEvenWhenPaused=true; Started=FPlatformTime::Seconds(); }
FString AHeliosKillFeedbackQA::OutputDirectory() const
{
    FString Dir; FParse::Value(FCommandLine::Get(),TEXT("HeliosKillQAOut="),Dir);
    if(Dir.IsEmpty()) Dir=FPaths::ProjectDir()/TEXT("../qa/kill_feedback/runtime");
    return FPaths::ConvertRelativePathToFull(Dir);
}
void AHeliosKillFeedbackQA::Check(const FString& Name,bool Pass)
{
    if(!Pass) ++Failures;
    if(!Checks.IsEmpty()) Checks+=TEXT(",");
    Checks+=FString::Printf(TEXT("{\"name\":\"%s\",\"passed\":%s}"),*Name,Pass?TEXT("true"):TEXT("false"));
    UE_LOG(LogTemp,Display,TEXT("IKUN_QA %s %s"),*Name,Pass?TEXT("PASS"):TEXT("FAIL"));
}
void AHeliosKillFeedbackQA::Finish()
{
    if(bFinished)return; bFinished=true;
    if(bRecording)
    {
        CaptureFrame();
        UAudioMixerBlueprintLibrary::StopRecordingOutput(this,EAudioRecordingExportType::WavFile,TEXT("game_audio"),OutputDirectory());
        FFileHelper::SaveStringToFile(FrameTimes,*(OutputDirectory()/TEXT("frames.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
    IFileManager::Get().MakeDirectory(*OutputDirectory(),true);
    FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"status\":\"%s\",\"failures\":%d,\"map\":\"%s\",\"checks\":[%s]}"),
        Failures?TEXT("failed"):TEXT("passed"),Failures,*GetWorld()->GetMapName(),*Checks),*(OutputDirectory()/TEXT("runtime_report.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    Stage=99; Age=0;
}
void AHeliosKillFeedbackQA::CaptureFrame()
{
    if(!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->Viewport)return;
    const double Time=FPlatformTime::Seconds()-RecordStarted;
    if(Time<NextFrame)return;
    auto Window=GEngine->GameViewport->GetWindow();
    if(!Window.IsValid())return;
    FIntVector Size;
    TArray<FColor> Pixels;
    if(!FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(),Pixels,Size))return;
    for(FColor& Pixel:Pixels)Pixel.A=255;
    TArray64<uint8> PNG;
    FImageUtils::PNGCompressImageArray(Size.X,Size.Y,MakeArrayView(Pixels),PNG);
    const FString Name=FString::Printf(TEXT("frame_%05d.png"),FrameIndex++);
    FFileHelper::SaveArrayToFile(PNG,*(OutputDirectory()/TEXT("frames")/Name));
    FrameTimes+=FString::Printf(TEXT("%s,%.6f\n"),*Name,Time);
    NextFrame=Time+1.0/15.0;
}
AHeliosEnemyActor* AHeliosKillFeedbackQA::SpawnTarget(bool Boss)
{
    FActorSpawnParameters P; P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* E=GetWorld()->SpawnActor<AHeliosEnemyActor>(Boss?AHeliosEnemyBoss::StaticClass():AHeliosEnemyE01::StaticClass(),
        Player->GetActorLocation()+FVector(550,0,0),FRotator(0,180,0),P);
    if(E)
    {
        E->Tags.Add(TEXT("IkunKillQATarget")); E->SetActorTickEnabled(false); E->StopShooting();
        E->GetCharacterMovement()->StopMovementImmediately(); E->GetCharacterMovement()->DisableMovement();
        if(auto* AI=Cast<AAIController>(E->GetController())) { AI->StopMovement(); if(AI->GetBrainComponent())AI->GetBrainComponent()->StopLogic(TEXT("Kill feedback QA")); AI->SetActorTickEnabled(false); }
    }
    return E;
}
void AHeliosKillFeedbackQA::CheckIntegration()
{
    auto* F=GetWorld()->GetSubsystem<UHeliosKillFeedbackSubsystem>();
    auto* PC=Player->GetController(); auto* W=Player->GetEquippedWeapon();
    auto* PetCause=GetWorld()->SpawnActor<AActor>(); PetCause->SetOwner(Player); PetCause->SetInstigator(Player); PetCause->Tags.Add(TEXT("HeliosPet"));
    auto* SummonCause=GetWorld()->SpawnActor<AActor>(); SummonCause->SetOwner(Player); SummonCause->Tags.Add(TEXT("HeliosSummon"));
    auto Kill=[&](AActor* Cause){auto* E=SpawnTarget(); if(E)UGameplayStatics::ApplyDamage(E,E->GetHealth()+100,PC,Cause,nullptr);return E;};
    F->ResetRun();
    auto* E=SpawnTarget();
    UGameplayStatics::ApplyDamage(E,0,PC,W,nullptr); Check(TEXT("zero_damage_not_a_kill"),F->GetState().Total==0);
    UGameplayStatics::ApplyDamage(E,FMath::Max(0.f,E->GetHealth()-1),PC,W,nullptr);
    UGameplayStatics::ApplyDamage(E,100,PC,PetCause,nullptr); Check(TEXT("player_wounds_pet_finishes_excluded"),F->GetState().Total==0); E->Destroy();
    E=SpawnTarget(); UGameplayStatics::ApplyDamage(E,FMath::Max(0.f,E->GetHealth()-1),PC,PetCause,nullptr);
    UGameplayStatics::ApplyDamage(E,100,PC,W,nullptr); Check(TEXT("pet_wounds_player_finishes_included"),F->GetState().Total==1);
    UGameplayStatics::ApplyDamage(E,100,PC,W,nullptr); Check(TEXT("corpse_damage_not_duplicated"),F->GetState().Total==1); E->Destroy();
    E=Kill(SummonCause); Check(TEXT("summon_with_player_controller_excluded"),F->GetState().Total==1); E->Destroy();
    auto* PetShot=GetWorld()->SpawnActor<AActor>(); PetShot->SetOwner(PetCause); PetShot->SetInstigator(Player);
    E=Kill(PetShot); Check(TEXT("pet_projectile_excluded"),F->GetState().Total==1); E->Destroy();
    auto* Shot=GetWorld()->SpawnActor<AActor>(); Shot->SetOwner(W); Shot->SetInstigator(Player);
    E=Kill(Shot); Check(TEXT("player_projectile_included"),F->GetState().Total==2); E->Destroy();
    E=Kill(W); E->Destroy(); E=Kill(W); E->Destroy(); Check(TEXT("group_kills_count_each_victim"),F->GetState().Total==4);
    E=SpawnTarget(); for(int I=0;I<4;++I) UGameplayStatics::ApplyDamage(E,E->GetHealth()/2,PC,W,nullptr);
    UGameplayStatics::ApplyDamage(E,E->GetHealth()+1,PC,W,nullptr); Check(TEXT("damage_over_time_counts_once"),F->GetState().Total==5); E->Destroy();
    E=SpawnTarget(); E->Destroy(); Check(TEXT("reclaim_not_a_kill"),F->GetState().Total==5);
    E=SpawnTarget(); UGameplayStatics::ApplyDamage(E,E->GetHealth()+1,nullptr,nullptr,nullptr); Check(TEXT("environment_not_a_kill"),F->GetState().Total==5); E->Destroy();
    // Exercise the actual rocket explosion/occlusion code and duplicate-hit guard.
    auto* BlastA=SpawnTarget(); auto* BlastB=SpawnTarget();
    BlastB->SetActorLocation(BlastA->GetActorLocation()+FVector(0,160,0));
    UGameplayStatics::ApplyDamage(BlastA,BlastA->GetHealth()-1,nullptr,nullptr,nullptr);
    UGameplayStatics::ApplyDamage(BlastB,BlastB->GetHealth()-1,nullptr,nullptr,nullptr);
    const FVector Centre=BlastA->GetActorLocation()+FVector(-130,80,0);
    const FTransform Transform(FRotator::ZeroRotator,Centre);
    auto* Rocket=GetWorld()->SpawnActorDeferred<AHeliosRocketProjectile>(AHeliosRocketProjectile::StaticClass(),Transform,Player,Player,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    Rocket->ConfigureRocket(100,100,300,0,nullptr); UGameplayStatics::FinishSpawningActor(Rocket,Transform);
    FHitResult Impact; Impact.ImpactPoint=Centre; Impact.ImpactNormal=FVector::UpVector;
    // Dispatch through AActor so the production virtual NotifyHit implementation executes.
    static_cast<AActor*>(Rocket)->NotifyHit(nullptr,nullptr,nullptr,true,Centre,FVector::UpVector,FVector::ZeroVector,Impact);
    Check(TEXT("real_rocket_blast_counts_two_victims"),BlastA->IsEnemyDead()&&BlastB->IsEnemyDead()&&F->GetState().Total==7);
    static_cast<AActor*>(Rocket)->NotifyHit(nullptr,nullptr,nullptr,true,Centre,FVector::UpVector,FVector::ZeroVector,Impact);
    Check(TEXT("repeated_rocket_hit_does_not_repeat_kills"),F->GetState().Total==7);
    BlastA->Destroy(); BlastB->Destroy();
    PetCause->Destroy(); SummonCause->Destroy(); PetShot->Destroy(); Shot->Destroy();
    F->ResetRun(); Check(TEXT("reset_clears_pending_and_chain"),F->GetState().Total==0&&!F->GetState().bPending);
}
void AHeliosKillFeedbackQA::Tick(float Dt)
{
    Super::Tick(Dt); Age+=Dt;
    if(Stage==99) { if(Age>2) FPlatformMisc::RequestExit(false); return; }
    if(bRecording)CaptureFrame();
    if(FPlatformTime::Seconds()-Started>180) { Check(TEXT("completed_before_timeout"),false); Finish(); return; }
    auto* PC=UGameplayStatics::GetPlayerController(this,0);
    auto* HUD=PC?Cast<AHeliosAdventureHUD>(PC->GetHUD()):nullptr;
    auto* D=AHeliosAdventureDirector::Find(GetWorld());
    auto* F=GetWorld()->GetSubsystem<UHeliosKillFeedbackSubsystem>();
    Player=PC?Cast<AHeliosArenaCharacter>(PC->GetPawn()):nullptr;
    if(!Player || !HUD || !D || !F) return;
    if(Stage==0 && Age>2)
    {
        if(auto* T=GetWorld()->GetSubsystem<UHeliosTownSubsystem>();T&&T->IsTownWorld()&&!T->IsWorldReady()) return;
        HUD->SelectWeapon(TEXT("rifle")); HUD->StartSelectedRun();
        if(D->IsSelectingWeapon())return;
        Stage=1; Age=0; return;
    }
    if(Stage==1 && Age>4)
    {
        if(auto* T=GetWorld()->GetSubsystem<UHeliosTownSubsystem>();T&&T->IsTownWorld()&&!T->IsWorldReady()) return;
        Player->SetCanBeDamaged(false); D->SetActorTickEnabled(false);
        Player->GetCharacterMovement()->StopMovementImmediately(); Player->GetCharacterMovement()->DisableMovement();
        if(auto* Pets=GetWorld()->GetSubsystem<UHeliosPetWorld>();Pets&&Pets->Pet) { Pets->Pet->SetActorTickEnabled(false);Pets->Pet->SetActorLocation(Player->GetActorLocation()+FVector(-4000,0,0)); }
        PC->SetControlRotation(FRotator(0,0,0));
        Check(TEXT("all_sixteen_assets_loaded"),F->HasAllAssets());
        Check(TEXT("fresh_world_has_no_streak"),F->GetState().Total==0);
        CheckIntegration();
        IFileManager::Get().MakeDirectory(*OutputDirectory(),true);
        FFileHelper::SaveStringToFile(TEXT("ready"),*(OutputDirectory()/TEXT("ready.txt")));
        Stage=2; Age=0; return;
    }
    if(Stage>=2)for(TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It)if(!It->ActorHasTag(TEXT("IkunKillQATarget")))It->Destroy();
    if(Stage==2)
    {
        if(FParse::Param(FCommandLine::Get(),TEXT("HeliosKillRecord"))&&!FPaths::FileExists(OutputDirectory()/TEXT("capture_started.txt")))return;
        if(FParse::Param(FCommandLine::Get(),TEXT("HeliosKillRecord")))
        {
            IFileManager::Get().MakeDirectory(*(OutputDirectory()/TEXT("frames")),true);
            RecordStarted=FPlatformTime::Seconds(); NextFrame=0;
            UAudioMixerBlueprintLibrary::StartRecordingOutput(this,60); bRecording=true;
        }
        Stage=3;Age=0;return;
    }
    if(Stage==3)
    {
        const float Delay=Sample==1?6.3f:Sample==0?1.f:1.65f;
        if(!Target && Age>=Delay)
        {
            Target=SpawnTarget(Sample==7); if(!Target){Check(TEXT("target_spawn"),false);Finish();return;}
            UGameplayStatics::ApplyDamage(Target,FMath::Max(0.f,Target->GetHealth()-1),nullptr,nullptr,nullptr);
            FVector Eye; FRotator View; PC->GetPlayerViewPoint(Eye,View);
            PC->SetControlRotation((Target->GetActorLocation()+FVector(0,0,15)-Eye).Rotation());
            bFired=false;bCaptured=false;DeathAge=-1; Age=0;
        }
        if(!Target)return;
        if(!bFired && Age>.4f) { Player->GetEquippedWeapon()->StartFiring(); bFired=true; }
        if(Target->IsEnemyDead() && DeathAge<0) { DeathAge=Age;Player->GetEquippedWeapon()->StopFiring(); }
        if(DeathAge>=0 && Age-DeathAge>.35f && !bCaptured)
        {
            static const EHeliosKillBadge Expected[]={EHeliosKillBadge::First,EHeliosKillBadge::Single,EHeliosKillBadge::Double,EHeliosKillBadge::Triple,EHeliosKillBadge::Quad,EHeliosKillBadge::Penta,EHeliosKillBadge::Hexa,EHeliosKillBadge::Boss};
            const auto Kind=Expected[Sample]; const FString Key=F->AssetKey(Kind);
            Check(TEXT("real_rifle_kill_")+Key,F->GetState().Display==Kind && F->IsVisible());
            Check(TEXT("audible_")+Key,F->IsAnnouncing());
            FScreenshotRequest::RequestScreenshot(OutputDirectory()/(Key+TEXT(".png")),true,false);
            bCaptured=true;
        }
        if(DeathAge>=0 && Age-DeathAge>1.3f)
        {
            Target->Destroy();Target=nullptr;++Sample;Age=0;
            if(Sample==8){Stage=4;Age=0;}
        }
        if(Target && Age>8) { Player->GetEquippedWeapon()->StopFiring();Check(TEXT("real_shot_reached_target"),false);Finish(); }
        return;
    }
    if(Stage==4 && Age>.2f)
    {
        HUD->ToggleMemeSound(); Check(TEXT("mute_stops_immediately"),!F->IsAnnouncing());
        Check(TEXT("mute_keeps_badge"),F->IsVisible()); HUD->ToggleMemeSound();
        const int32 Count=F->GetState().Streak; D->SetRunPaused(true);
        Check(TEXT("pause_stops_audio"),!F->IsAnnouncing()); Check(TEXT("pause_keeps_chain"),F->GetState().Streak==Count);
        D->SetRunPaused(false);
        Player->SetCanBeDamaged(true);
        UGameplayStatics::ApplyDamage(Player,Player->GetHealth()+100,nullptr,nullptr,nullptr);
        Check(TEXT("player_death_clears_feedback"),Player->IsDead()&&F->GetState().Total==0&&!F->IsAnnouncing());
        D->EnterWeaponSelection(); Check(TEXT("selection_resets_chain"),F->GetState().Total==0&&!F->IsVisible());
        Finish();
    }
}
