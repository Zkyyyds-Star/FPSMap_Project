#include "Audio/HeliosAudio.h"
#include "Weapons/HeliosLightstickWeapon.h"
#include "Player/HeliosArenaCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

// Separate pools reserve wall marks even during a large multi-target wave.
void AHeliosLightstickWeapon::EnsureSlashPool()
{
    if(!Settings||!SlashMarks.IsEmpty())return;
    auto* Material=Settings->SlashMaterial.LoadSynchronous();
    for(int32 I=0;I<80;++I)
    {
        const bool Mark=I<32;
        auto* C=NewObject<UStaticMeshComponent>(this);
        C->SetStaticMesh(Mark?Settings->SlashMesh.LoadSynchronous():Settings->StreakMesh.LoadSynchronous());
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetGenerateOverlapEvents(false);
        C->SetCastShadow(false);C->SetCanEverAffectNavigation(false);C->bReceivesDecals=false;
        C->RegisterComponent();C->SetVisibility(false);
        auto* M=UMaterialInstanceDynamic::Create(Material,this);C->SetMaterial(0,M);
        if(Mark){SlashMarks.Add(C);SlashMaterials.Add(M);SlashAges.Add(10);SlashLives.Add(1);SlashNormals.Add(FVector::UpVector);SlashSurfaces.Add(nullptr);}
        else {SlashFragments.Add(C);FragmentMaterials.Add(M);FragmentAges.Add(10);FragmentPositions.Add(FVector::ZeroVector);FragmentVelocities.Add(FVector::ZeroVector);}
    }
}
void AHeliosLightstickWeapon::PlaySlashImpact(const FHitResult& H,const FVector& Direction,bool Wall,float Charge)
{
    EnsureSlashPool();if(SlashMarks.IsEmpty())return;
    FVector N=FVector(H.ImpactNormal).GetSafeNormal(UE_SMALL_NUMBER,-GetActorForwardVector());
    // Enemy cuts face the viewer at the actual contact; grazing capsule normals must not hide the slash edge-on.
    if(!Wall)if(auto* Player=Cast<AHeliosArenaCharacter>(GetOwner()))N=(Player->FirstPersonCamera->GetComponentLocation()-H.ImpactPoint).GetSafeNormal(UE_SMALL_NUMBER,N);
    if(Wall)for(int32 I=24;I<32;++I)
    {
        // Coalesce neighbouring arc segments, without resetting the one-second lifetime.
        if(SlashAges[I]<SlashLives[I]&&SlashSurfaces[I].Get()==H.GetActor()&&FVector::DotProduct(SlashNormals[I],N)>.92f&&
            FVector::DistSquared(SlashMarks[I]->GetComponentLocation(),H.ImpactPoint)<FMath::Square(130.f))return;
    }
    const int32 Index=Wall?24+(NextWallSlash++%8):NextEnemySlash++%24;
    const FVector Tangent=FVector::CrossProduct(N,FVector::UpVector).GetSafeNormal(UE_SMALL_NUMBER,FVector::RightVector);
    FVector Cut=FVector::VectorPlaneProject(Direction,N).GetSafeNormal();
    if(Cut.IsNearlyZero())Cut=Tangent;
    Cut=(Cut+FVector::VectorPlaneProject(FVector::UpVector,N)*.38f).GetSafeNormal();
    const FVector Point=H.ImpactPoint+N*2.5f;
    const float Length=Wall?FMath::Lerp(120.f,180.f,FMath::Clamp(Charge,0.f,1.f)):95.f;
    auto* Mark=SlashMarks[Index].Get();Mark->SetWorldLocationAndRotation(Point,FRotationMatrix::MakeFromXZ(Cut,N).Rotator());
    Mark->SetWorldScale3D(FVector(Length/100.f,Wall?1.35f:1.f,1));Mark->SetVisibility(true);
    SlashAges[Index]=0;SlashLives[Index]=Wall?1.f:.25f;SlashNormals[Index]=N;SlashSurfaces[Index]=H.GetActor();
    const FLinearColor Color=Charge>=.98f?FLinearColor(1.f,.72f,.32f):FLinearColor(.06f,.64f,1.f);
    SlashMaterials[Index]->SetVectorParameterValue(TEXT("GlowColor"),Color);
    SlashMaterials[Index]->SetScalarParameterValue(TEXT("Energy"),5.f);SlashMaterials[Index]->SetScalarParameterValue(TEXT("Opacity"),1.f);
    // Directional strips fan along the cutting plane, rather than random spherical sparks.
    const FVector Cross=FVector::CrossProduct(N,Cut).GetSafeNormal();
    for(int32 J=0;J<(Wall?8:4);++J)
    {
        const int32 I=NextFragment++%48;const float Side=J%2?1.f:-1.f;
        FVector V=(Cut*Side+Cross*FMath::FRandRange(-.3f,.3f)+N*.20f).GetSafeNormal();
        FragmentPositions[I]=Point+Cut*FMath::FRandRange(-Length*.3f,Length*.3f);
        FragmentVelocities[I]=V*FMath::FRandRange(240.f,520.f);FragmentAges[I]=0;
        FragmentMaterials[I]->SetVectorParameterValue(TEXT("GlowColor"),J%3?Color:FLinearColor(.55f,.12f,1.f));
        FragmentMaterials[I]->SetScalarParameterValue(TEXT("Energy"),2.5f);
    }
    const double Now=GetWorld()->GetTimeSeconds();
    if(Now-LastSlashAudio>.035){LastSlashAudio=Now;HeliosAudio::PlaySoundAtLocation(this,Settings->WaveImpactSound.LoadSynchronous(),Point,Wall?.7f:.5f);}
}
void AHeliosLightstickWeapon::UpdateSlashEffects(float Dt)
{
    for(int32 I=0;I<SlashMarks.Num();++I)
    {
        SlashAges[I]+=Dt;const float T=SlashAges[I]/SlashLives[I];SlashMarks[I]->SetVisibility(T<1);
        if(T>=1)continue;
        // A bright initial cut settles into a narrow readable wall mark, then fades.
        const float Fade=I>=24?FMath::Clamp((1-T)/.45f,0.f,1.f):FMath::Square(1-T);
        SlashMaterials[I]->SetScalarParameterValue(TEXT("Opacity"),Fade);
        SlashMaterials[I]->SetScalarParameterValue(TEXT("Energy"),1.35f+3.65f*FMath::Exp(-SlashAges[I]*32.f));
    }
    for(int32 I=0;I<SlashFragments.Num();++I)
    {
        FragmentAges[I]+=Dt;const float T=FragmentAges[I]/.25f;auto* C=SlashFragments[I].Get();C->SetVisibility(T<1);if(T>=1)continue;
        FragmentPositions[I]+=FragmentVelocities[I]*Dt;FragmentVelocities[I]*=FMath::Exp(-Dt*5);
        C->SetWorldLocationAndRotation(FragmentPositions[I],FRotationMatrix::MakeFromZ(FragmentVelocities[I]).Rotator());
        C->SetWorldScale3D(FVector(.009f,.009f,.28f*(1-T)+.06f));
        FragmentMaterials[I]->SetScalarParameterValue(TEXT("Opacity"),.85f*(1-T));
    }
}
void AHeliosLightstickWeapon::ClearSlashEffects()
{
    for(int32 I=0;I<SlashMarks.Num();++I){SlashAges[I]=10;SlashMarks[I]->SetVisibility(false);SlashSurfaces[I]=nullptr;}
    for(int32 I=0;I<SlashFragments.Num();++I){FragmentAges[I]=10;SlashFragments[I]->SetVisibility(false);}
}
