#include "Pets/HeliosRifleVFXComponent.h"
#include "Pets/HeliosPetDog.h"
#include "Audio/HeliosAudio.h"
#include "Pets/HeliosUltimatumPet.h"
#include "ProceduralMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
UHeliosRifleVFXComponent::UHeliosRifleVFXComponent(){PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickGroup=TG_PostUpdateWork;}
void UHeliosRifleVFXComponent::BeginPlay()
{
    Super::BeginPlay();AddTickPrerequisiteActor(GetOwner());if(auto* P=Cast<AHeliosUltimatumPet>(GetOwner()))AddTickPrerequisiteComponent(P->GetMesh());
    Surface=NewObject<UProceduralMeshComponent>(GetOwner());GetOwner()->AddInstanceComponent(Surface);Surface->SetupAttachment(this);Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);Surface->SetCanEverAffectNavigation(false);Surface->SetCastShadow(false);Surface->RegisterComponent();
    Surface->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Helios/Pets/UltimatumGR/M_RifleAir.M_RifleAir")));
    FireSound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Helios/Pets/UltimatumGR/SFX_RifleFire.SFX_RifleFire"));
    ReloadSound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Helios/Pets/UltimatumGR/SFX_RifleReload.SFX_RifleReload"));Particles.Reserve(240);
}
void UHeliosRifleVFXComponent::Add(const FParticle& P){if(Particles.Num()>=240)Particles.RemoveAt(0,1,EAllowShrinking::No);Particles.Add(P);}
void UHeliosRifleVFXComponent::Sound(USoundBase* Cue,FVector Position,float Volume)
{
    auto* PC=UGameplayStatics::GetPlayerController(this,0);if(!Cue||!PC||!PC->PlayerCameraManager)return;
    const float Gain=FMath::Clamp(1.f-FVector::Dist(Position,PC->PlayerCameraManager->GetCameraLocation())/6000.f,0.f,1.f);
    HeliosAudio::PlaySoundAtLocation(this,Cue,Position,Volume*Gain*Gain*(Cast<AHeliosPetCompanion>(GetOwner())?Cast<AHeliosPetCompanion>(GetOwner())->RosterAudioGain():1.f));
}
void UHeliosRifleVFXComponent::Shot(FVector Start,FVector End,FVector Normal,bool bHit)
{
    UE_LOG(LogTemp,Display,TEXT("RIFLE_AUDIO Fire time=%.6f"),GetWorld()->GetTimeSeconds());Sound(FireSound,Start,.8f);
    const FVector Direction=(End-Start).GetSafeNormal();
    FParticle P;P.Position=Start;P.End=End;P.Shape=EShape::Tracer;P.Size=2.1f;P.Life=.10f;P.Color=FLinearColor(.08,.6,1,.9);Add(P);
    P.Size=.65f;P.Color=FLinearColor(.95,1,1,1);Add(P);
    P.Shape=EShape::Glow;P.Size=6;P.Life=.045f;P.Color=FLinearColor(.85,.96,1,.9);Add(P);
    P.Shape=EShape::Ribbon;P.Position=Start;P.End=Start+Direction*15;P.Size=3;P.Life=.04f;Add(P);
    FVector Right,Up;Direction.FindBestAxisVectors(Right,Up);
    P.Shape=EShape::Shell;P.Position=Start-Direction*48;P.Velocity=GetOwner()->GetActorRightVector()*150+FVector(0,0,80);P.Size=.7f;P.Life=.38f;P.Color=FLinearColor(.7,.4,.12,.85);Add(P);
    if(bHit)
    {
        const FVector Contact=End+Normal*2;
        P=FParticle();P.Shape=EShape::Glow;P.Position=Contact;P.Size=5;P.Life=.07f;Add(P);
        for(int I=0;I<7;++I){P=FParticle();P.Position=Contact;P.Velocity=(Normal+FMath::VRand()*.6)*180;P.Size=.6f;P.Life=.18f;P.Color=I%2?FLinearColor(.2,.65,1,1):FLinearColor(1,.85,.45,1);Add(P);}
    }
}
void UHeliosRifleVFXComponent::Reload(){UE_LOG(LogTemp,Display,TEXT("RIFLE_AUDIO Reload time=%.6f"),GetWorld()->GetTimeSeconds());if(ReloadSound)ReloadAudio=HeliosAudio::SpawnSoundAttached(ReloadSound,this,NAME_None,FVector::ZeroVector,EAttachLocation::KeepRelativeOffset,true,.65f*(Cast<AHeliosPetCompanion>(GetOwner())?Cast<AHeliosPetCompanion>(GetOwner())->RosterAudioGain():1.f));}
void UHeliosRifleVFXComponent::Clear(){Particles.Reset();if(Surface)Surface->ClearAllMeshSections();if(IsValid(ReloadAudio))ReloadAudio->Stop();ReloadAudio=nullptr;}
void UHeliosRifleVFXComponent::EndPlay(EEndPlayReason::Type R){Clear();Super::EndPlay(R);}
void UHeliosRifleVFXComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Dt,Type,Function);if(!Surface)return;if(auto* P=Cast<AHeliosUltimatumPet>(GetOwner()))P->PostPose(Dt);
    auto* PC=UGameplayStatics::GetPlayerController(this,0);if(!PC||!PC->PlayerCameraManager)return;
    const FVector View=PC->PlayerCameraManager->GetCameraLocation();const FTransform X=GetComponentTransform();
    TArray<FVector> V,N;TArray<int32> Indices;TArray<FVector2D> UV;TArray<FLinearColor> C;TArray<FProcMeshTangent> Tangents;
    auto Vertex=[&](FVector P,FVector2D Tex,FLinearColor Color){V.Add(X.InverseTransformPosition(P));N.Add(FVector::UpVector);UV.Add(Tex);C.Add(Color);};
    for(int PIndex=Particles.Num()-1;PIndex>=0;--PIndex)
    {
        FParticle& P=Particles[PIndex];if(P.Fresh)P.Fresh=false;else P.Age+=Dt;
        if(P.Age>=P.Life){Particles.RemoveAtSwap(PIndex);continue;}if(P.Shape==EShape::Shell)P.Velocity.Z-=980*Dt;P.Position+=P.Velocity*Dt;
        FRotationMatrix Facing((View-P.Position).Rotation());FVector Right=Facing.GetUnitAxis(EAxis::Y),Up=Facing.GetUnitAxis(EAxis::Z);
        const float U=P.Age/P.Life;FLinearColor Color=P.Color;Color.A*=FMath::Square(1-U);const int Base=V.Num();
        if(P.Shape==EShape::Tracer)
        {
            const FVector Dir=(P.End-P.Position).GetSafeNormal();const float Length=FVector::Dist(P.End,P.Position);
            const float Head=FMath::Min(Length,120.f+P.Age*80000.f),Tail=FMath::Max(0.f,Head-300.f);
            const FVector A=P.Position+Dir*Tail,B=P.Position+Dir*Head;
            FVector Width=FVector::CrossProduct(Dir,View-(A+B)*.5f).GetSafeNormal()*P.Size;
            Vertex(A-Width,FVector2D(.5f,0),Color);Vertex(A+Width,FVector2D(.5f,1),Color);
            Vertex(B+Width*.4f,FVector2D(.5f,1),Color);Vertex(B-Width*.4f,FVector2D(.5f,0),Color);Indices.Append({Base,Base+1,Base+2,Base,Base+2,Base+3});
        }
        else if(P.Shape==EShape::Ribbon)
        {
            FVector Width=FVector::CrossProduct(P.End-P.Position,View-(P.End+P.Position)*.5f).GetSafeNormal()*P.Size*(1-U);
            Vertex(P.Position-Width,FVector2D(.5f,0),Color);Vertex(P.Position+Width,FVector2D(.5f,1),Color);
            Vertex(P.End+Width*.2f,FVector2D(.5f,1),Color);Vertex(P.End-Width*.2f,FVector2D(.5f,0),Color);Indices.Append({Base,Base+1,Base+2,Base,Base+2,Base+3});
        }
        else
        {
            const float Size=P.Size*(1-U*.5f);
            if((P.Shape==EShape::Spark||P.Shape==EShape::Shell)&&!P.Velocity.IsNearlyZero()){Up=P.Velocity.GetSafeNormal();Right=FVector::CrossProduct(Up,View-P.Position).GetSafeNormal();Up*=2.5f;}
            for(FVector2D Q:{FVector2D(-1,-1),FVector2D(-1,1),FVector2D(1,1),FVector2D(1,-1)})Vertex(P.Position+(Right*Q.X+Up*Q.Y)*Size,Q*.5+FVector2D(.5,.5),Color);
            Indices.Append({Base,Base+1,Base+2,Base,Base+2,Base+3});
        }
    }
    if(V.IsEmpty())Surface->ClearAllMeshSections();else Surface->CreateMeshSection_LinearColor(0,V,Indices,N,UV,C,Tangents,false);
}
