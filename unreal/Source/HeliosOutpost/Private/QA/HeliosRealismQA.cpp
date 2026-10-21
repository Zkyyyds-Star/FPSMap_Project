#include "QA/HeliosRealismQA.h"
#include "Adventure/HeliosAdventureCharacter.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "Weapons/HeliosFirstPersonPresentationComponent.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Weapons/HeliosWeaponPresentationProfile.h"
#include "Weapons/HeliosImpactPresentationSubsystem.h"
#include "Enemies/HeliosEnemyActor.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Materials/MaterialInterface.h"
#include "ShooterWeapon.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/App.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"

namespace {
const FName RealismIds[]={TEXT("rifle"),TEXT("rocket"),TEXT("laser"),TEXT("shotgun")};
const FVector RealismStart(-5400.f,-4400.f,100.f); // Authored player_01, metres converted to centimetres.
const FRotator RealismView(0.f,47.f,0.f);
// Opt-in QA silhouette pass uses the game's actual camera, first-person render
// projection, skeletal pose and runtime recoil. It never runs in normal play.
void ApplySilhouettePass(UWorld* World, AHeliosAdventureCharacter* Player)
{
    USkeletalMeshComponent* Arms=Player->GetPresentation()?Player->GetPresentation()->GetArmsMesh():nullptr;
    UMaterialInterface* White=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Helios/Weapons/Realism/Impacts/M_QAMask"));
    for(TActorIterator<AActor> It(World);It;++It)
    {
        AShooterWeapon* OwnedWeapon=Cast<AShooterWeapon>(*It);
        USkeletalMeshComponent* Gun=OwnedWeapon&&OwnedWeapon->GetOwner()==Player?OwnedWeapon->GetFirstPersonMesh():nullptr;
        TInlineComponentArray<UPrimitiveComponent*> Components(*It);
        for(UPrimitiveComponent* Component:Components)
        {
            const bool bKeep=Component==Arms||Component==Gun;
            Component->SetHiddenInGame(!bKeep,false);
            if(bKeep&&White) for(int32 Slot=0;Slot<Component->GetNumMaterials();++Slot) Component->SetMaterial(Slot,White);
        }
    }
}
}
AHeliosRealismQA::AHeliosRealismQA()
{
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.bTickEvenWhenPaused=true;
}
void AHeliosRealismQA::Check(const FString& Name,bool bPass,const FString& Detail)
{
    auto C=MakeShared<FJsonObject>(); C->SetStringField(TEXT("name"),Name); C->SetBoolField(TEXT("passed"),bPass); C->SetStringField(TEXT("detail"),Detail);
    Checks.Add(MakeShared<FJsonValueObject>(C)); if(!bPass) ++Failures;
    UE_LOG(LogTemp,Display,TEXT("REALISM_QA %s %s %s"),*Name,bPass?TEXT("PASS"):TEXT("FAIL"),*Detail);
}
void AHeliosRealismQA::StartWeapon()
{
    Director->EnterWeaponSelection();
    Check(RealismIds[Index].ToString()+TEXT("_start"),Director->StartRun(RealismIds[Index]));
    Director->SetActorTickEnabled(false); Player->SetCanBeDamaged(false);
    AController* Controller=Player->GetController();
    if(!Controller)
    {
        Check(RealismIds[Index].ToString()+TEXT("_controller"),false,TEXT("Player has no controller"));
        Finish();
        return;
    }
    Player->SetActorLocation(RealismStart,false,nullptr,ETeleportType::TeleportPhysics);
    Aim=RealismView;
    Controller->SetControlRotation(Aim);
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosRealismMask")))
    {
        if(auto* PC=Cast<APlayerController>(Controller))
        {
            for(const TCHAR* Command:{TEXT("ShowFlag.Atmosphere 0"),TEXT("ShowFlag.Fog 0"),TEXT("ShowFlag.VolumetricFog 0"),TEXT("ShowFlag.PostProcessing 0"),TEXT("ShowFlag.AntiAliasing 0"),TEXT("ShowFlag.Particles 0"),TEXT("ShowFlag.Decals 0")}) PC->ConsoleCommand(Command);
        }
        Check(TEXT("silhouette_material_ready"),LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Helios/Weapons/Realism/Impacts/M_QAMask"))!=nullptr);
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosImpactShowcase")))
    {
        for(TActorIterator<AActor> It(GetWorld());It;++It)
            if(It->ActorHasTag(TEXT("HeliosImpactFixture"))) It->Destroy();
        const FVector Eye=Player->FirstPersonCamera->GetComponentLocation();
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        for(int32 Surface=0;Surface<2;++Surface)
        {
            const FRotator Heading(0,RealismView.Yaw+(Surface==0?-14.f:0.f),0);
            auto* Panel=GetWorld()->SpawnActor<AStaticMeshActor>(Eye+Heading.Vector()*650.f,Heading,Params);
            Panel->Tags.Add(TEXT("HeliosImpactFixture"));
            auto* Mesh=Panel->GetStaticMeshComponent(); Mesh->SetMobility(EComponentMobility::Movable);
            Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube")));
            Mesh->SetWorldScale3D(FVector(.12f,1.5f,2.4f));
            Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Mesh->SetCollisionResponseToAllChannels(ECR_Block);
            Mesh->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,Surface==0?TEXT("/Game/Helios/Materials/MI_Concrete"):TEXT("/Game/Helios/Materials/MI_Metal")));
            Mesh->SetPhysMaterialOverride(LoadObject<UPhysicalMaterial>(nullptr,Surface==0?TEXT("/Game/Helios/Weapons/Realism/Impacts/PM_Concrete"):TEXT("/Game/Helios/Weapons/Realism/Impacts/PM_Metal")));
        }
        auto* Enemy=GetWorld()->SpawnActor<AHeliosEnemyActor>(Eye+FRotator(0,RealismView.Yaw+14.f,0).Vector()*650.f-FVector(0,0,45.f),FRotator(0,RealismView.Yaw+180,0),Params);
        if(Enemy)
        {
            Enemy->Tags.Add(TEXT("HeliosImpactFixture"));Enemy->CurrentHP=10000;Enemy->StopShooting();
            Enemy->GetCharacterMovement()->DisableMovement();Enemy->SetActorTickEnabled(false);
            if(auto* AI=Cast<AAIController>(Enemy->GetController()))
            {
                if(AI->GetBrainComponent()) AI->GetBrainComponent()->StopLogic(TEXT("Impact showcase"));
                AI->StopMovement();
            }
        }
    }
    Events=0; Age=0; FrameTimes.Reset(); PreviousWall=FPlatformTime::Seconds();
    ReloadStartAge=-1; ReloadExpectedSeconds=0; ReloadFrameTolerance=0.05f;
    bReloadEarlyChecked=false; bReloadCompletionChecked=false;
    if(!bLegacy)
    {
        auto* P=Player->GetPresentation();
        Check(RealismIds[Index].ToString()+TEXT("_hands_ready"),P&&P->IsReady());
        auto* Impacts=GetWorld()->GetSubsystem<UHeliosImpactPresentationSubsystem>();
        Check(RealismIds[Index].ToString()+TEXT("_impact_assets_resident"),Impacts&&Impacts->AreAssetsReady());
        Check(RealismIds[Index].ToString()+TEXT("_animation_instance"),P&&P->GetArmsMesh()&&P->GetArmsMesh()->GetAnimInstance());
        if(P&&P->GetArmsMesh()) Check(RealismIds[Index].ToString()+TEXT("_right_grip"),P->GetArmsMesh()->DoesSocketExist(TEXT("Grip_R")));
        if(P&&P->GetArmsMesh()) Check(RealismIds[Index].ToString()+TEXT("_left_grip"),P->GetArmsMesh()->DoesSocketExist(TEXT("Grip_L")));
        if(P&&P->GetArmsMesh())
        {
            const FTransform Grip=P->GetArmsMesh()->GetSocketTransform(TEXT("Grip_L"),RTS_Component);
            Check(RealismIds[Index].ToString()+TEXT("_animated_centimetres"),
                Grip.GetLocation().Size()>10.f && Grip.GetLocation().Size()<100.f && Grip.GetScale3D().Equals(FVector::OneVector,.02f),
                Grip.ToHumanReadableString());
        }
        for(TActorIterator<AShooterWeapon> W(GetWorld());W;++W) if(W->GetOwner()==Player && W->GetFirstPersonMesh())
        {
            Check(RealismIds[Index].ToString()+TEXT("_muzzle_socket"),W->GetFirstPersonMesh()->DoesSocketExist(TEXT("Muzzle")));
            if(RealismIds[Index]==TEXT("rifle") || RealismIds[Index]==TEXT("shotgun"))
                Check(RealismIds[Index].ToString()+TEXT("_eject_socket"),W->GetFirstPersonMesh()->DoesSocketExist(TEXT("Eject")));
        }
    }
}
void AHeliosRealismQA::Tick(float Dt)
{
    Super::Tick(Dt); if(bFinished) return;
    if(!Director) Director=AHeliosAdventureDirector::Find(GetWorld());
    if(!Player) Player=Cast<AHeliosAdventureCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));
    if(!Director||!Player) return;
    if(Index<0)
    {
        bLegacy=FParse::Param(FCommandLine::Get(),TEXT("HeliosLegacyPresentation"));
        bDemo=FParse::Param(FCommandLine::Get(),TEXT("HeliosRealismDemo"));
        if(bDemo) Check(TEXT("demo_fixed_timestep"),FApp::UseFixedTimeStep() && FMath::IsNearlyEqual(FApp::GetFixedDeltaTime(),1.0/30.0,0.0001));
        FString Only;
        if(FParse::Value(FCommandLine::Get(),TEXT("HeliosRealismOnly="),Only))
        {
            for(int32 Candidate=0;Candidate<UE_ARRAY_COUNT(RealismIds);++Candidate)
            {
                if(Only.Equals(RealismIds[Candidate].ToString(),ESearchCase::IgnoreCase))
                {
                    TestStartIndex=Candidate; TestEndIndex=Candidate; break;
                }
            }
        }
        Index=TestStartIndex; StartWeapon(); return;
    }
    const double Wall=FPlatformTime::Seconds();
    if(Age>5.f&&!bDemo&&PreviousWall>0) FrameTimes.Add((Wall-PreviousWall)*1000.0);
    PreviousWall=Wall; Age+=Dt;
    const bool bImpactShowcase=FParse::Param(FCommandLine::Get(),TEXT("HeliosImpactShowcase"));
    if(bImpactShowcase) Aim=FRotator(0,RealismView.Yaw+(Age<2.7f?-14.f:(Age<6.8f?0.f:14.f)),0);
    if(AController* Controller=Player->GetController()) Controller->SetControlRotation(Aim);
    else
    {
        Check(RealismIds[Index].ToString()+TEXT("_controller_alive"),false,TEXT("Player lost controller during QA"));
        Finish();
        return;
    }
    auto Once=[this](uint32 Bit,float Time){if(Age>=Time&&!(Events&Bit)){Events|=Bit;return true;}return false;};
    if(bImpactShowcase && Once(32768,7.f)) Player->StartFireFromAutomation();
    if(bImpactShowcase && Once(65536,7.12f)) Player->StopFireFromAutomation();
    if(Once(1,2.f)) Player->StartFireFromAutomation();
    if(Once(2,2.05f)) Player->StopFireFromAutomation();
    if(Once(4,3.f)) Player->StartFireFromAutomation();
    if(Once(8,4.1f)) Player->StopFireFromAutomation();
    if(Once(16,4.5f))
    {
        AmmoBeforeReload=Player->GetAmmo(); Player->StartReloadFromAutomation();
        if(const UHeliosWeaponDefinition* D=UHeliosWeaponRegistry::FindWeapon(RealismIds[Index]))
        {
            ReloadExpectedSeconds=FMath::Max(0.f,D->ReloadSeconds);
        }
        ReloadStartAge=Age;
        ReloadFrameTolerance=FMath::Clamp(Dt*1.5f,0.02f,0.12f);
        Check(RealismIds[Index].ToString()+TEXT("_reload_started"),Player->IsReloading());
        Player->StartReloadFromAutomation(); Player->StartFireFromAutomation();
        Check(RealismIds[Index].ToString()+TEXT("_reload_blocks_fire"),Player->GetAmmo()==AmmoBeforeReload);
        if(!bLegacy)
        {
            auto* P=Player->GetPresentation();
            Check(RealismIds[Index].ToString()+TEXT("_reload_animating"),P&&P->IsReloadPlaying());
            const auto* D=UHeliosWeaponRegistry::FindWeapon(RealismIds[Index]);
            const int32 Cost=RealismIds[Index]==TEXT("laser")?FMath::Max(1,D->EnergyPerPulse):1;
            const int32 ExpectedShots=(Player->GetMagazineSize()-AmmoBeforeReload)/Cost;
            Check(RealismIds[Index].ToString()+TEXT("_shot_events_match_ammo"),P&&P->GetShotPresentationCount()==ExpectedShots,
                FString::Printf(TEXT("events=%d expected=%d cost=%d"),P?P->GetShotPresentationCount():-1,ExpectedShots,Cost));
        }
    }
    if(ReloadStartAge>=0 && Once(16384,ReloadStartAge+.2f)) Player->StartReloadFromAutomation();
    if(ReloadStartAge>=0 && !bReloadEarlyChecked &&
        Age>=ReloadStartAge+ReloadExpectedSeconds*.6f &&
        Age<ReloadStartAge+ReloadExpectedSeconds-FMath::Max(Dt,ReloadFrameTolerance))
    {
        bReloadEarlyChecked=true;
        Check(RealismIds[Index].ToString()+TEXT("_reload_not_early"),Player->IsReloading(),
            FString::Printf(TEXT("elapsed=%.3f expected=%.3f tolerance=%.3f"),Age-ReloadStartAge,ReloadExpectedSeconds,ReloadFrameTolerance));
        if(!bLegacy)
        {
            auto* P=Player->GetPresentation();
            Check(RealismIds[Index].ToString()+TEXT("_reload_animation_advanced"),P&&P->GetArmsMesh()&&P->GetArmsMesh()->GetPosition()>ReloadExpectedSeconds*.4f);
        }
    }
    if(ReloadStartAge>=0 && !bReloadCompletionChecked &&
        Age>=ReloadStartAge+ReloadExpectedSeconds+ReloadFrameTolerance)
    {
        bReloadCompletionChecked=true;
        Check(RealismIds[Index].ToString()+TEXT("_reload_completed"),!Player->IsReloading()&&Player->GetAmmo()==Player->GetMagazineSize(),
            FString::Printf(TEXT("elapsed=%.3f expected=%.3f tolerance=%.3f; repeated input at +0.2s"),Age-ReloadStartAge,ReloadExpectedSeconds,ReloadFrameTolerance));
        if(!bLegacy) Check(RealismIds[Index].ToString()+TEXT("_presentation_shots"),Player->GetPresentation()&&Player->GetPresentation()->GetShotPresentationCount()>0);
    }
    if(!bDemo)
    {
        if(Once(64,8.f)) Player->StartFireFromAutomation();
        if(Once(128,10.f)) Player->StopFireFromAutomation();
        if(Once(256,10.5f)) Player->StartReloadFromAutomation();
        if(Once(1024,10.8f))
        {
            Player->CancelCombatAndControl();
            Check(RealismIds[Index].ToString()+TEXT("_reload_cancelled"),!Player->IsReloading()&&Player->GetAmmo()<Player->GetMagazineSize());
            if(!bLegacy) Check(RealismIds[Index].ToString()+TEXT("_reload_pose_cancelled"),Player->GetPresentation()&&!Player->GetPresentation()->IsReloadPlaying());
        }
        if(Once(2048,11.1f))
        {
            Player->ResetForTest();
            Player->SetActorLocation(RealismStart,false,nullptr,ETeleportType::TeleportPhysics);
            Check(RealismIds[Index].ToString()+TEXT("_reset_reloaded"),!Player->IsReloading()&&Player->GetAmmo()==Player->GetMagazineSize());
        }
        if(Once(4096,11.8f))
        {
            const bool bReequipped=Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(RealismIds[Index]));
            int32 OwnedWeapons=0;
            for(TActorIterator<AShooterWeapon> W(GetWorld());W;++W)
                if(IsValid(*W)&&!W->IsActorBeingDestroyed()&&W->GetOwner()==Player) ++OwnedWeapons;
            Check(RealismIds[Index].ToString()+TEXT("_reequip"),bReequipped&&Player->GetWeaponId()==RealismIds[Index]);
            Check(RealismIds[Index].ToString()+TEXT("_single_owned_weapon"),OwnedWeapons==1,
                FString::Printf(TEXT("owned_weapons=%d"),OwnedWeapons));
        }
        if(Once(8192,13.f)) { Player->StartFireFromAutomation(); Player->StopFireFromAutomation(); }
    }
    if(bDemo && Age<8.f)
    {
        const bool bMask=FParse::Param(FCommandLine::Get(),TEXT("HeliosRealismMask"));
        if(bMask) ApplySilhouettePass(GetWorld(),Player);
        FString CaptureFolder=TEXT("frames");
        FParse::Value(FCommandLine::Get(),TEXT("HeliosRealismCaptureDir="),CaptureFolder);
        const FString Dir=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/realism")/CaptureFolder);
        IFileManager::Get().MakeDirectory(*Dir,true);
        const FString Filename=FString::Printf(TEXT("frame_%05d.png"),MovieFrame++);
        FScreenshotRequest::RequestScreenshot(Dir/Filename,!bMask,false);
        auto Frame=MakeShared<FJsonObject>();
        Frame->SetStringField(TEXT("file"),Filename); Frame->SetStringField(TEXT("weapon_id"),RealismIds[Index].ToString());
        Frame->SetNumberField(TEXT("simulation_seconds"),Age);
        if(auto* P=Player->GetPresentation();P&&P->GetArmsMesh())
        {
            Frame->SetNumberField(TEXT("reload_phase"),P->GetReloadProgress());
            Frame->SetNumberField(TEXT("arms_animation_seconds"),P->GetArmsMesh()->GetPosition());
            const FVector Contact=P->GetArmsMesh()->GetComponentTransform().InverseTransformPosition(P->GetArmsMesh()->GetSocketLocation(TEXT("Grip_L")));
            Frame->SetStringField(TEXT("left_grip_mesh_cm"),Contact.ToString());
        }
        CapturedFrames.Add(MakeShared<FJsonValueObject>(Frame));
    }
    if(Age>=(bDemo?8.f:15.f))
    {
        Player->StopFireFromAutomation(); FinishWeapon();
        if(++Index>TestEndIndex) Finish(); else StartWeapon();
    }
}
void AHeliosRealismQA::FinishWeapon()
{
    if(!bLegacy) if(auto* Impacts=GetWorld()->GetSubsystem<UHeliosImpactPresentationSubsystem>())
    {
        Check(RealismIds[Index].ToString()+TEXT("_impact_pool_bounded"),Impacts->GetActiveImpactCount()<=24 && Impacts->GetActiveDecalCount()<=96,
            FString::Printf(TEXT("impacts=%d decals=%d presented=%llu"),Impacts->GetActiveImpactCount(),Impacts->GetActiveDecalCount(),Impacts->GetPresentedCount()));
    }
    FrameTimes.Sort(); auto R=MakeShared<FJsonObject>();
    const UHeliosWeaponDefinition* D=UHeliosWeaponRegistry::FindWeapon(RealismIds[Index]);
    R->SetStringField(TEXT("weapon_id"),RealismIds[Index].ToString());
    R->SetNumberField(TEXT("magazine_size"),D->MagazineSize); R->SetNumberField(TEXT("reload_seconds"),D->ReloadSeconds);
    R->SetNumberField(TEXT("damage"),D->Damage); R->SetNumberField(TEXT("refire_seconds"),D->RefireSeconds);
    R->SetNumberField(TEXT("sample_frames"),FrameTimes.Num());
    double Sum=0; for(double T:FrameTimes) Sum+=T;
    if(!FrameTimes.IsEmpty())
    {
        R->SetNumberField(TEXT("average_fps"),1000.0/(Sum/FrameTimes.Num()));
        R->SetNumberField(TEXT("p95_frame_ms"),FrameTimes[FMath::Clamp(FMath::CeilToInt(FrameTimes.Num()*.95)-1,0,FrameTimes.Num()-1)]);
    }
    Results.Add(MakeShared<FJsonValueObject>(R));
}
void AHeliosRealismQA::Finish()
{
    bFinished=true; Director->EnterWeaponSelection();
    auto R=MakeShared<FJsonObject>(); R->SetStringField(TEXT("status"),Failures?TEXT("failed"):TEXT("passed"));
    R->SetBoolField(TEXT("legacy_presentation"),bLegacy); R->SetBoolField(TEXT("fixed_step_demo"),bDemo);
    R->SetBoolField(TEXT("runtime_silhouette"),FParse::Param(FCommandLine::Get(),TEXT("HeliosRealismMask")));
    R->SetStringField(TEXT("measurement"),TEXT("Real elapsed game frames; first five seconds of each weapon excluded; 1080p DX11 existing scalability; demo frames are not performance evidence"));
    R->SetStringField(TEXT("fixed_camera"),TEXT("player_01: (-5400,-4400,100) cm; yaw 47, pitch 0"));
    R->SetArrayField(TEXT("checks"),Checks); R->SetArrayField(TEXT("weapons"),Results); R->SetNumberField(TEXT("failures"),Failures);
    if(bDemo)
    {
        R->SetArrayField(TEXT("capture_frames"),CapturedFrames);
        FString CaptureFolder=TEXT("frames"); FParse::Value(FCommandLine::Get(),TEXT("HeliosRealismCaptureDir="),CaptureFolder);
        R->SetStringField(TEXT("capture_directory"),CaptureFolder);
    }
    FString Out; FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Out));
    const FString Dir=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/realism"));
    IFileManager::Get().MakeDirectory(*Dir,true);
    FFileHelper::SaveStringToFile(Out,*(Dir/(bDemo?TEXT("demo_report.json"):(bLegacy?TEXT("baseline_performance.json"):TEXT("realism_performance.json")))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    FPlatformMisc::RequestExit(false);
}
