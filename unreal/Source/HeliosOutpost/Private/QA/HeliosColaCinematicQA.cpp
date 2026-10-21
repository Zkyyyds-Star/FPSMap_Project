#include "QA/HeliosColaQA.h"
#include "Weapons/HeliosColaWeapon.h"
#include "Weapons/HeliosColaVFXSettings.h"
#include "Weapons/HeliosWeaponVFX.h"
#include "Adventure/HeliosAdventureCharacter.h"
#include "Camera/CameraActor.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "DrawDebugHelpers.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

void AHeliosColaQA::CinematicBurst(const FVector& Position,float Radius,const FVector& Normal,AActor* Surface,const FVector& Point)
{
    const FTransform Transform(Position);
    auto* Projectile=GetWorld()->SpawnActorDeferred<AHeliosColaProjectile>(AHeliosColaProjectile::StaticClass(),Transform,Player,Player,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    auto* Definition=NewObject<UHeliosColaWeaponDefinition>(this);Definition->OuterRadius=Radius;Definition->InnerRadius=FMath::Min(300.f,Radius*.2f);
    Projectile->Configure(FVector::ZeroVector,Definition);
    auto* Movement=Projectile->FindComponentByClass<UProjectileMovementComponent>();Movement->Velocity=FVector::ZeroVector;Movement->ProjectileGravityScale=0;
    UGameplayStatics::FinishSpawningActor(Projectile,Transform);
    static_cast<AActor*>(Projectile)->NotifyHit(nullptr,Surface,nullptr,true,Point,Normal,FVector::ZeroVector,FHitResult());
}

void AHeliosColaQA::StartCinematicShot(int32 Scenario)
{
    UHeliosWeaponVFX::ClearWorld(this);
    if(IsValid(Wall)){Wall->Destroy();Wall=nullptr;}
    if(IsValid(CinematicCamera)){CinematicCamera->Destroy();CinematicCamera=nullptr;}
    Blast=Origin+FVector(800,0,-93);
    if(Scenario==2)Blast.Z+=1000;
    FVector Normal=Scenario==2?FVector::ZeroVector:FVector::UpVector;
    FVector Point=Blast-FVector(0,0,7);
    AActor* Surface=Scenario==2?nullptr:Floor.Get();
    if(Scenario==1)
    {
        Wall=Box(Blast+FVector(30,0,300),FVector(.4,14,8));Surface=Wall;
        Normal=FVector(-1,0,0);Point=Blast+FVector(10,0,0);
    }
    auto* PC=Cast<APlayerController>(Player->GetController());
    const FVector CameraLocation=Blast+FVector(-1800,-1900,1400);
    if(Scenario==3)
    {
        Player->SetActorLocation(Blast+FVector(-400,-80,105));
        PC->SetControlRotation(FRotator(-6,0,0));PC->SetViewTarget(Player);
    }
    else
    {
        CinematicCamera=GetWorld()->SpawnActor<ACameraActor>(CameraLocation,(Blast-CameraLocation).Rotation());
        PC->SetViewTarget(CinematicCamera);
    }
    CinematicFrame=0;
    const float Radius=Scenario==4?500.f:1500.f;
    CinematicBurst(Blast,Radius,Normal,Surface,Point);
    if(Scenario==5)
        for(int32 I=1;I<3;++I)CinematicBurst(Blast+FVector(0,I*180,0),Radius,Normal,Surface,Point);
    Next(201);
}

void AHeliosColaQA::CinematicTick(float)
{
    using namespace HeliosColaVFX;
    if(Stage==200)
    {
        if(Age<2.1f)return;
#if WITH_EDITOR
        for(const TCHAR* Name:{TEXT("NS_ColaFoam"),TEXT("NS_ColaSpray"),TEXT("NS_ColaFoamAir"),TEXT("NS_ColaSprayAir")})
            LoadObject<UNiagaraSystem>(nullptr,*(FString(TEXT("/Game/Helios/Weapons/Cola/"))+Name));
        FAssetCompilingManager::Get().FinishAllCompilation();
#endif
        Check(TEXT("cinematic_particle_budget"),ParticleCount()<=ParticleBudget&&ParticleBudget<=1000,FString::FromInt(ParticleCount()));
        bool StartsAtImpact=true,FinishesInTime=true;
        for(const auto& L:HeliosColaVFX::Layers)
        {
            StartsAtImpact&=L.SpawnRadius*1500.f/Envelope()<30.f;
            FinishesInTime&=L.Delay+.04f+L.LifeMax<ReleaseSeconds;
        }
        Check(TEXT("particles_born_within_30cm"),StartsAtImpact);
        Check(TEXT("all_layers_finish_before_two_seconds"),FinishesInTime);
        const float EdgeAtSixTenths=Travel(9000,6,980,.6f)/Envelope();
        Check(TEXT("outer_droplets_approach_radius_by_0_6s"),EdgeAtSixTenths>=.9f&&EdgeAtSixTenths<=1.f,FString::SanitizeFloat(EdgeAtSixTenths));
        const float GroundReach=9000.f*(1.f-FMath::Exp(-6.f*.6f))/6.f*FMath::Sin(FMath::DegreesToRadians(HeliosColaVFX::Layers[3].SurfaceConeAngle*.5f))/Envelope();
        Check(TEXT("ground_plane_reach_above_90_percent"),GroundReach>=.9f,FString::SanitizeFloat(GroundReach));
        // Material permutations can be requested only after a renderer is first used.
        // Exercise surface and air variants before waiting, then record fresh bursts.
        CinematicBurst(Origin+FVector(800,0,-93),1500,FVector::UpVector,Floor,Origin+FVector(800,0,-100));
        CinematicBurst(Origin+FVector(800,0,1000),1500,FVector::ZeroVector,nullptr,Origin+FVector(800,0,1000));
        Next(202);return;
    }
    if(Stage==202)
    {
        if(FPlatformTime::Seconds()-WallStart<2.5)return;
#if WITH_EDITOR
        FAssetCompilingManager::Get().FinishAllCompilation();
        if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
#endif
        StartCinematicShot(0);return;
    }
    if(Stage==201)
    {
        const TCHAR* Names[]={TEXT("ground"),TEXT("wall"),TEXT("air"),TEXT("first_person"),TEXT("radius_500"),TEXT("triple"),TEXT("radius_1500")};
        if(CinematicScenario==4||CinematicScenario==6)DrawDebugSphere(GetWorld(),Blast,CinematicScenario==4?500:1500,32,FColor::Cyan,false,0,0,2);
        if(CinematicFrame%2==0&&Age<2.06f)Capture(FString::Printf(TEXT("cinematic/%s/frame_%04d"),Names[CinematicScenario],CinematicFrame/2));
        if(CinematicFrame==5)
        {
            int32 Bursts=0;bool CorrectScale=true,CorrectVariant=true,CorrectDirection=true;
            for(TObjectIterator<UNiagaraComponent> I;I;++I)
            {
                if(I->GetWorld()!=GetWorld()||!I->GetAsset()||!I->IsActive())continue;
                const FString N=I->GetAsset()->GetName();
                if(!N.StartsWith(TEXT("NS_ColaFoam"))&&!N.StartsWith(TEXT("NS_ColaSpray")))continue;
                ++Bursts;CorrectScale&=FMath::IsNearlyEqual(I->GetComponentScale().X*Envelope(),CinematicScenario==4?500.f:1500.f,1.f);
                CorrectVariant&=N.EndsWith(TEXT("Air"))==(CinematicScenario==2);
                const FVector ExpectedNormal=CinematicScenario==1?FVector(-1,0,0):FVector::UpVector;
                CorrectDirection&=FVector::DotProduct(I->GetUpVector(),ExpectedNormal)>.99f;
            }
            Check(FString(Names[CinematicScenario])+TEXT("_burst_count"),Bursts==(CinematicScenario==5?6:2));
            Check(FString(Names[CinematicScenario])+TEXT("_damage_radius_scale"),CorrectScale);
            Check(FString(Names[CinematicScenario])+TEXT("_surface_or_air_variant"),CorrectVariant&&CorrectDirection);
        }
        ++CinematicFrame;
        if(Age<2.2f)return;
        Check(FString(Names[CinematicScenario])+TEXT("_particles_released"),UHeliosWeaponVFX::GetTrackedCount(this)==0);
        if(++CinematicScenario<7){StartCinematicShot(CinematicScenario);return;}
        Next(210);return;
    }
    if(Stage==210)
    {
        if(Age<1)return;
        // No screenshots or file I/O in the measured intervals; use wall time, not fixed simulation dt.
        LastPerfTime=FPlatformTime::Seconds();Next(211);return;
    }
    const double Now=FPlatformTime::Seconds(),Milliseconds=(Now-LastPerfTime)*1000.;LastPerfTime=Now;
    if(Stage==211)
    {
        BaselineFrames.Add(Milliseconds);
        if(BaselineFrames.Num()<120)return;
        PerfShots=0;Next(212);return;
    }
    if(Stage==212)
    {
        if(Age<2.1f)OverlapFrames.Add(Milliseconds);
        if(PerfShots<3&&Age>=PerfShots*.4f)
        {CinematicBurst(Blast+FVector(0,PerfShots*100,0),1500,FVector::UpVector,Floor,Blast-FVector(0,0,7));++PerfShots;}
        if(Age<3)return;
        auto Percentile=[](TArray<double> Values,float P){Values.Sort();return Values.IsEmpty()?0.:Values[FMath::Clamp(FMath::FloorToInt((Values.Num()-1)*P),0,Values.Num()-1)];};
        const double Base95=Percentile(BaselineFrames,.95f),Burst95=Percentile(OverlapFrames,.95f);
        Check(TEXT("overlapping_blasts_release_all_components"),UHeliosWeaponVFX::GetTrackedCount(this)==0);
        Check(TEXT("overlap_frame_time_budget"),Burst95<=FMath::Max(33.3,Base95*2),FString::Printf(TEXT("baseline_p95=%.3fms overlap_p95=%.3fms"),Base95,Burst95));
        FString Tag;FParse::Value(FCommandLine::Get(),TEXT("ColaCapture="),Tag);
        const FString Dir=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/cola")/Tag);
        FString Csv=TEXT("phase,frame,wall_ms\n");
        for(int32 I=0;I<BaselineFrames.Num();++I)Csv+=FString::Printf(TEXT("baseline,%d,%.4f\n"),I,BaselineFrames[I]);
        for(int32 I=0;I<OverlapFrames.Num();++I)Csv+=FString::Printf(TEXT("three_blasts,%d,%.4f\n"),I,OverlapFrames[I]);
        FFileHelper::SaveStringToFile(Csv,*(Dir/TEXT("frame_times.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        Finish();
    }
}
