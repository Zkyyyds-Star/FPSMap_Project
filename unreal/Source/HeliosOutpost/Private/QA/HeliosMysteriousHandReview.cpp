#include "QA/HeliosMysteriousHandReview.h"
#include "Weapons/HeliosMysteriousHand.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Player/HeliosArenaCharacter.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HighResScreenshot.h"
#include "UnrealClient.h"

AHeliosMysteriousHandReview::AHeliosMysteriousHandReview()
{PrimaryActorTick.bCanEverTick=true;DefaultPawnClass=AHeliosArenaCharacter::StaticClass();HUDClass=AHeliosMysteriousHandReviewHUD::StaticClass();}
void AHeliosMysteriousHandReview::BeginPlay()
{
    Super::BeginPlay();
    if(FParse::Param(FCommandLine::Get(),TEXT("MHRecord"))){FApp::SetUseFixedTimeStep(true);FApp::SetFixedDeltaTime(FParse::Param(FCommandLine::Get(),TEXT("MHGestureReview"))?1.0/60.0:1.0/30.0);}
    auto Box=[&](FVector P,FVector S)
    {
        auto* A=GetWorld()->SpawnActor<AStaticMeshActor>(P,FRotator::ZeroRotator);auto* M=A->GetStaticMeshComponent();M->SetMobility(EComponentMobility::Movable);
        M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));M->SetWorldScale3D(S);M->SetCollisionProfileName(TEXT("BlockAll"));
        if(auto* Mat=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Helios/Weapons/MysteriousHand/M_MH_Cuff")))M->SetMaterial(0,Mat);
    };
    Box(FVector(1300,0,-30),FVector(38,30,.5));Box(FVector(3000,0,400),FVector(.5,30,9));
    for(int32 I=-3;I<=3;++I)Box(FVector(2950,I*420,240),FVector(.4,.15,5));
    auto* Sun=GetWorld()->SpawnActor<ADirectionalLight>(FVector(0,0,500),FRotator(-45,-25,0));Sun->GetLightComponent()->SetIntensity(3.0f);
    for(int32 I=0;I<3;++I)
    {
        auto* L=GetWorld()->SpawnActor<APointLight>(FVector(I*700-150,-200,350),FRotator::ZeroRotator);
        L->PointLightComponent->SetIntensity(22000);L->PointLightComponent->SetAttenuationRadius(1800);L->PointLightComponent->SetLightColor(I==2?FLinearColor(.5,.35,1):FLinearColor(1,.8,.64));
    }
}
void AHeliosMysteriousHandReview::Tick(float Dt)
{
    Super::Tick(Dt);Age+=Dt;
    auto* PC=UGameplayStatics::GetPlayerController(this,0);if(!PC)return;
    if(!bSetup&&Age>.5)
    {
        Player=Cast<AHeliosArenaCharacter>(PC->GetPawn());if(!Player)return;
        Player->SetActorLocation(FVector(0,0,100));PC->SetControlRotation(FRotator(0,0,0));
        Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("mysterious_hand")));Player->GetCharacterMovement()->DisableMovement();
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        for(int32 I=0;I<3;++I)
        {
            auto* E=GetWorld()->SpawnActor<AHeliosEnemyE01>(FVector(1250+I*280,(I-1)*170,100),FRotator(0,180,0),P);
            E->StopShooting();E->SetActorTickEnabled(false);E->GetCharacterMovement()->DisableMovement();if(E->GetController())E->GetController()->Destroy();Enemies.Add(E);
        }
        bSetup=true;Age=0;PC->ConsoleCommand(TEXT("r.BloomQuality 4"));PC->ConsoleCommand(TEXT("r.MotionBlurQuality 0"));
        Warmup=FParse::Param(FCommandLine::Get(),TEXT("MHGestureReview"))?3.f:0.f;
        PC->ConsoleCommand(TEXT("r.EyeAdaptationQuality 0"));PC->ConsoleCommand(TEXT("DisableAllScreenMessages"));
    }
    if(!bSetup)return;
    // Let newly imported materials and temporal history settle before the first review frame.
    if(Warmup>0){Warmup-=Dt;Age=0;return;}
    auto* Hand=Cast<AHeliosMysteriousHand>(Player->GetEquippedWeapon());if(!Hand)return;
    if(!FParse::Param(FCommandLine::Get(),TEXT("MHManual")))
    {
        const bool AssistReview=FParse::Param(FCommandLine::Get(),TEXT("MHAssistReview"));
        const bool GestureReview=FParse::Param(FCommandLine::Get(),TEXT("MHGestureReview"));
        if(GestureReview)
        {
            // Includes full idle loop, repeated clicks, alternating buffered gestures and cancellation.
            static const float Times[]={3.2f,3.72f,5.f,5.92f,7.6f,8.12f,9.1f,10.7f,10.80f,11.15f,12.f,12.12f,12.65f};
            static const uint8 Actions[]={1,1,2,2,1,2,1,1,3,4,2,5,1};
            if(Step<UE_ARRAY_COUNT(Times)&&Age>=Times[Step])
            {
                switch(Actions[Step])
                {
                case 1: Hand->StartFiring();Hand->StopFiring();break;
                case 2: PC->SetControlRotation(FRotator(-8,0,0));Hand->StartSecondaryFire();Hand->StopSecondaryFire();break;
                case 3: Hand->DeactivateWeapon();break;
                case 4: Hand->ActivateWeapon(TEXT("Player"));break;
                case 5: Hand->CancelReload();break;
                }
                UE_LOG(LogTemp,Display,TEXT("MH_GESTURE_REVIEW age=%.4f action=%d lasers=%d missiles=%d"),Age,Actions[Step],Hand->GetLaserCount(),Hand->GetMissileCount());
                ++Step;
            }
        }
        else if(AssistReview)
        {
            if(Step==0&&Age>2){Hand->StartSecondaryFire();Hand->StopSecondaryFire();Step++;}
            if(Step==1&&Age>2.15f)
            {
                for(const auto& Enemy:Enemies)if(IsValid(Enemy))Enemy->AddActorWorldOffset(FVector(120,350,0));
                PC->SetControlRotation(FRotator(0,8,0));Step++;
            }
            if(Step==2&&Age>5){Hand->StartSecondaryFire();Hand->StopSecondaryFire();Step++;}
        }
        else
        {
        if(Step==0&&Age>2){Hand->StartFiring();Hand->StopFiring();Step++;}
        if(Step==1&&Age>4){Hand->StartFiring();Hand->StopFiring();Step++;}
        if(Step==2&&Age>6){PC->SetControlRotation(FRotator(-8,0,0));Hand->StartSecondaryFire();Hand->StopSecondaryFire();Step++;}
        if(Step==3&&Age>8){PC->SetControlRotation(FRotator(0,0,0));Step++;}
        if(Step==4&&Age>10){Hand->StartFiring();Hand->StopFiring();Step++;}
        if(Step==5&&Age>12){PC->SetControlRotation(FRotator(-9,12,0));Hand->StartSecondaryFire();Hand->StopSecondaryFire();Step++;}
        if(Step==6&&Age>14){PC->SetControlRotation(FRotator(0,0,0));Step++;}
        }
        if(Age>(GestureReview?15.f:AssistReview?8.f:16.f))
        {
            const FString Report=FString::Printf(TEXT("{\"lasers\":%d,\"missiles\":%d,\"elapsed\":%.3f,\"frames\":%d}"),Hand->GetLaserCount(),Hand->GetMissileCount(),Age,Frame);
            FFileHelper::SaveStringToFile(Report,*(FPaths::ProjectSavedDir()/(GestureReview?TEXT("MysteriousHand/gestures/review.json"):AssistReview?TEXT("MysteriousHand/assist/review.json"):TEXT("MysteriousHand/review.json"))));
            PC->ConsoleCommand(TEXT("quit"));return;
        }
    }
    if(Age>=NextCapture)
    {
        const bool Record=FParse::Param(FCommandLine::Get(),TEXT("MHRecord"));
        NextCapture=Age+(Record?0.f:.2f);
        const FString Dir=FPaths::ProjectSavedDir()/(FParse::Param(FCommandLine::Get(),TEXT("MHGestureReview"))?TEXT("MysteriousHand/gestures/frames"):FParse::Param(FCommandLine::Get(),TEXT("MHAssistReview"))?TEXT("MysteriousHand/assist/frames"):TEXT("MysteriousHand/frames"));IFileManager::Get().MakeDirectory(*Dir,true);
        FScreenshotRequest::RequestScreenshot(Dir/FString::Printf(TEXT("frame_%05d.png"),Frame++),true,false);
    }
}
void AHeliosMysteriousHandReviewHUD::DrawHUD()
{
    Super::DrawHUD();if(!Canvas||!PlayerOwner)return;auto* P=Cast<AHeliosArenaCharacter>(PlayerOwner->GetPawn());if(!P)return;
    auto* H=Cast<AHeliosMysteriousHand>(P->GetEquippedWeapon());if(!H)return;
    const float W=Canvas->ClipX,Y=Canvas->ClipY;const FLinearColor Purple(.65f,.4f,1),White(.9f,.9f,.94f);
    DrawText(TEXT("MYSTERIOUS HAND"),White,42,32,GEngine->GetLargeFont(),1.35f);
    DrawText(TEXT("01 / PROVOCATION     02 / POINT     03 / VERDICT"),Purple,44,78,GEngine->GetSmallFont(),1.1f);
    DrawText(TEXT("LMB   SINGLE LASER     |     RMB   FIXED-POINT MISSILE"),White,42,Y-55,GEngine->GetMediumFont(),.85f);
    DrawRect(White,W*.5f-2,Y*.5f-2,4,4);H->DrawHandHUD(Canvas);
}
