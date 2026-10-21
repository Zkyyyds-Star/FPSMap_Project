#include "QA/HeliosMissileCinematicReview.h"
#include "Weapons/HeliosMysteriousHand.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Weapons/HeliosWeaponVFX.h"
#include "Weapons/HeliosHandImpact.h"
#include "Player/HeliosArenaCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

AHeliosMissileCinematicReview::AHeliosMissileCinematicReview()
{PrimaryActorTick.bCanEverTick=true;DefaultPawnClass=AHeliosArenaCharacter::StaticClass();HUDClass=AHeliosMissileCinematicHUD::StaticClass();}

void AHeliosMissileCinematicReview::BeginPlay()
{
    Super::BeginPlay();
    bRecord=FParse::Param(FCommandLine::Get(),TEXT("MHRecord"));bStress=FParse::Param(FCommandLine::Get(),TEXT("MHStress"));
    FString Name=TEXT("preview");FParse::Value(FCommandLine::Get(),TEXT("MHOutput="),Name);
    Output=FPaths::ProjectSavedDir()/TEXT("MysteriousHand")/Name;IFileManager::Get().MakeDirectory(*(Output/TEXT("frames")),true);
    if(bRecord){FApp::SetUseFixedTimeStep(true);FApp::SetFixedDeltaTime(1.0/30.0);}
    auto Box=[&](FVector P,FVector S)
    {
        auto* A=GetWorld()->SpawnActor<AStaticMeshActor>(P,FRotator::ZeroRotator);auto* M=A->GetStaticMeshComponent();M->SetMobility(EComponentMobility::Movable);
        M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));M->SetWorldScale3D(S);M->SetCollisionProfileName(TEXT("BlockAll"));
        return A;
    };
    Box(FVector(3500,1000,-80),FVector(180,150,1.5));
    Box(FVector(6800,0,450),FVector(1,100,9));
    for(int32 I=-4;I<=5;++I)
    {
        Box(FVector(6500,I*700,380),FVector(2,1.2,7.6));
        Box(FVector(1500,I*1000,-2),FVector(80,.04,.04));
        Box(FVector(I*1000,0,-2),FVector(.04,90,.04));
    }
    Box(FVector(4500,2500,800),FVector(20,20,.6));
    for(float X:{-900.f,900.f})for(float Y:{-900.f,900.f})Box(FVector(4500+X,2500+Y,390),FVector(.8,.8,7.8));
    auto* Sun=GetWorld()->SpawnActor<ADirectionalLight>(FVector(0,0,500),FRotator(-32,-35,0));
    auto* Directional=Cast<UDirectionalLightComponent>(Sun->GetLightComponent());Directional->SetIntensity(2.5f);Directional->SetLightColor(FLinearColor(1,.86f,.72f));Directional->SetAtmosphereSunLight(true);
    GetWorld()->SpawnActor<ASkyAtmosphere>();
    auto* Fill=GetWorld()->SpawnActor<ADirectionalLight>(FVector(0,0,500),FRotator(-40,140,0));Fill->GetLightComponent()->SetIntensity(.8f);Fill->GetLightComponent()->SetLightColor(FLinearColor(.46f,.6f,1));Fill->GetLightComponent()->SetCastShadows(false);
    WideCamera=GetWorld()->SpawnActor<ACameraActor>(FVector(-5400,-3000,2300),(FVector(1700,0,2500)-FVector(-5400,-3000,2300)).Rotation());WideCamera->GetCameraComponent()->SetFieldOfView(80);
}

void AHeliosMissileCinematicReview::Tick(float Dt)
{
    Super::Tick(Dt);Age+=Dt;auto* PC=UGameplayStatics::GetPlayerController(this,0);if(!PC||bFinished)return;
    if(!bSetup&&Age>2)
    {
        Player=Cast<AHeliosArenaCharacter>(PC->GetPawn());if(!Player)return;
        Player->SetActorLocation(FVector(-900,-650,130));Player->GetCharacterMovement()->DisableMovement();
        Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("mysterious_hand")));
        PC->ConsoleCommand(TEXT("r.BloomQuality 4"));PC->ConsoleCommand(TEXT("r.MotionBlurQuality 0"));
        PC->ConsoleCommand(TEXT("r.EyeAdaptationQuality 0"));PC->ConsoleCommand(TEXT("DisableAllScreenMessages"));
        bSetup=true;Age=0;UE_LOG(LogTemp,Display,TEXT("MH_REVIEW_START time=%.4f"),GetWorld()->GetTimeSeconds());
    }
    if(!bSetup)return;
    auto* Hand=Cast<AHeliosMysteriousHand>(Player->GetEquippedWeapon());if(!Hand)return;
    auto Aim=[&](FVector Target){PC->SetControlRotation((Target-Player->FirstPersonCamera->GetComponentLocation()).Rotation());};
    auto Strike=[&](FVector Target){Aim(Target);PC->PlayerCameraManager->UpdateCamera(0);Hand->StartSecondaryFire();Hand->StopSecondaryFire();UE_LOG(LogTemp,Display,TEXT("MH_REVIEW_CLICK time=%.4f"),Age);};
    if(bStress)
    {
        if(Age>=NextStrike&&Age<32){Strike(FVector(1500,FMath::Sin(Age)*350,0));NextStrike=Age+1.05f;}
        if(Age>5&&Age<32)FrameTimes.Add(Dt*1000);
    }
    else
    {
        if(Step==0){Aim(FVector(1500,0,0));if(Age>1.5f){Strike(FVector(1500,0,0));PC->SetViewTarget(WideCamera);Hand->GetFirstPersonMesh()->SetVisibility(false);++Step;}}
        if(Step==1&&Age>7){PC->SetViewTarget(Player);Hand->GetFirstPersonMesh()->SetVisibility(true);Aim(FVector(1500,0,100));++Step;}
        if(Step==2&&Age>8){Strike(FVector(1500,0,0));++Step;}
        if(Step==3&&Age>10.5f){Strike(FVector(1800,400,0));++Step;}
        if(Step==4&&Age>11.55f){Strike(FVector(1650,-300,0));++Step;}
        if(Step==5&&Age>16)
        {
            Player->SetActorLocation(FVector(2200,2500,130));Strike(FVector(4500,2500,0));
            WideCamera->SetActorLocation(FVector(-900,-3500,2400));WideCamera->SetActorRotation((FVector(4300,2200,2300)-WideCamera->GetActorLocation()).Rotation());
            PC->SetViewTarget(WideCamera);Hand->GetFirstPersonMesh()->SetVisibility(false);++Step;
        }
    }
    if(Age>(bStress?42.f:24.f))
    {
        FrameTimes.Sort();const float P95=FrameTimes.IsEmpty()?0:FrameTimes[FMath::Min(FrameTimes.Num()-1,FMath::FloorToInt(FrameTimes.Num()*.95f))];
        int32 Impacts=0;for(TActorIterator<AHeliosHandImpact> It(GetWorld());It;++It)++Impacts;
        const FString Report=FString::Printf(TEXT("{\"missiles\":%d,\"elapsed\":%.3f,\"frames\":%d,\"p95_frame_ms\":%.3f,\"tracked_fx\":%d,\"remaining_impacts\":%d}"),Hand->GetMissileCount(),Age,Frame,P95,UHeliosWeaponVFX::GetTrackedCount(this),Impacts);
        FFileHelper::SaveStringToFile(Report,*(Output/TEXT("review.json")));bFinished=true;PC->ConsoleCommand(TEXT("quit"));return;
    }
    if(!bStress&&Age>=NextCapture)
    {
        NextCapture=Age+(bRecord?0.f:.1f);
        FScreenshotRequest::RequestScreenshot(Output/TEXT("frames")/FString::Printf(TEXT("frame_%05d.png"),Frame++),true,false);
    }
}

void AHeliosMissileCinematicHUD::DrawHUD()
{
    Super::DrawHUD();if(!Canvas||!PlayerOwner)return;
    const float W=Canvas->ClipX,H=Canvas->ClipY;const FLinearColor White(.88f,.9f,.94f),Gold(1,.62f,.26f);
    DrawRect(FLinearColor(.006f,.009f,.016f,.80f),0,0,W,94);DrawRect(FLinearColor(.006f,.009f,.016f,.80f),0,H-55,W,55);
    DrawText(TEXT("MYSTERIOUS HAND  /  ORBITAL VERDICT"),White,38,23,GEngine->GetLargeFont(),1.1f);
    DrawText(TEXT("60 m DESCENT     /     10 m BLAST RADIUS     /     NO COOLDOWN"),Gold,40,62,GEngine->GetSmallFont(),1.1f);
    DrawText(TEXT("IN-ENGINE CAPTURE   /   REALTIME NIAGARA   /   FIRE + ASH + ENERGY"),White,40,H-38,GEngine->GetSmallFont(),1.f);
    if(PlayerOwner->GetViewTarget()==PlayerOwner->GetPawn())
    {
        DrawRect(White,W*.5f-1,H*.5f-4,2,8);DrawRect(White,W*.5f-4,H*.5f-1,8,2);
        if(auto* P=Cast<AHeliosArenaCharacter>(PlayerOwner->GetPawn()))if(auto* Hand=Cast<AHeliosMysteriousHand>(P->GetEquippedWeapon()))Hand->DrawHandHUD(Canvas);
    }
}
