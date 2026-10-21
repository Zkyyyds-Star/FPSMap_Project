#include "Pets/HeliosFistVFXComponent.h"
#include "Pets/HeliosPetDog.h"
#include "Audio/HeliosAudio.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "GameFramework/Character.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"

bool UHeliosFistCameraModifier::ModifyCamera(float Dt,FMinimalViewInfo& View)
{
    if(Remaining<=0)return false;
    Remaining=FMath::Max(0.f,Remaining-Dt);
    const float Envelope=FMath::Square(Remaining/.16f)*Strength;
    const float Wave=FMath::Sin((.16f-Remaining)*110.f);
    View.Rotation.Pitch+=Wave*.32f*Envelope;
    View.Rotation.Roll+=Wave*.12f*Envelope;
    View.Location.Z+=Wave*.65f*Envelope;
    return false;
}
UHeliosFistVFXComponent::UHeliosFistVFXComponent(){PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickGroup=TG_PostPhysics;}
void UHeliosFistVFXComponent::BeginPlay()
{
    Super::BeginPlay();AddTickPrerequisiteActor(GetOwner());
    if(auto* Character=Cast<ACharacter>(GetOwner()))AddTickPrerequisiteComponent(Character->GetMesh());
    Surface=NewObject<UProceduralMeshComponent>(GetOwner());GetOwner()->AddInstanceComponent(Surface);
    Surface->SetupAttachment(this);Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);Surface->SetCanEverAffectNavigation(false);Surface->SetCastShadow(false);Surface->RegisterComponent();
    Surface->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Helios/Pets/UltimatumGR/M_FistAir.M_FistAir")));
    Whoosh=LoadObject<USoundBase>(nullptr,TEXT("/Game/Helios/Pets/UltimatumGR/SFX_FistSwing.SFX_FistSwing"));
    Punch=LoadObject<USoundBase>(nullptr,TEXT("/Game/Helios/Pets/UltimatumGR/SFX_FistHit.SFX_FistHit"));
    HeavyPunch=LoadObject<USoundBase>(nullptr,TEXT("/Game/Helios/Pets/UltimatumGR/SFX_FistHeavy.SFX_FistHeavy"));
    ChargeSound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Helios/Pets/UltimatumGR/SFX_FistCharge.SFX_FistCharge"));
    Particles.Reserve(240);
}
void UHeliosFistVFXComponent::Add(const FParticle& P)
{
    if(Particles.Num()>=240)Particles.RemoveAt(0,1,EAllowShrinking::No);
    Particles.Add(P);
}
void UHeliosFistVFXComponent::Sound(USoundBase* Cue,FVector Position,float Volume)
{
    auto* PC=UGameplayStatics::GetPlayerController(this,0);
    if(!Cue||!PC||!PC->PlayerCameraManager)return;
    const float Gain=FMath::Clamp(1.f-FVector::Dist(Position,PC->PlayerCameraManager->GetCameraLocation())/4500.f,0.f,1.f);
    if(Gain>.01f)HeliosAudio::PlaySoundAtLocation(this,Cue,Position,Volume*Gain*Gain*(Cast<AHeliosPetCompanion>(GetOwner())?Cast<AHeliosPetCompanion>(GetOwner())->RosterAudioGain():1.f));
}
void UHeliosFistVFXComponent::BeginCombo(){ChargeStep=0;bChargeSound=false;AttackTime=0;bHasLastFist=false;}
void UHeliosFistVFXComponent::Charge(FVector Left,FVector Right,float Age,float Dt)
{
    if(Age>.88f&&Age<1.1f&&!bChargeSound)
    {
        bChargeSound=true;Sound(ChargeSound,Left,.6f);
        if(FParse::Param(FCommandLine::Get(),TEXT("UltimatumRecord")))UE_LOG(LogTemp,Display,TEXT("FIST_AUDIO Charge heavy=1 time=%.6f"),GetWorld()->GetTimeSeconds());
    }
    ChargeStep+=Dt;if(ChargeStep<1.f/60.f)return;ChargeStep=0;
    const bool Heavy=Age>.83f&&Age<1.15f;
    const bool LeftActive=Age<.34f||Heavy,RightActive=Age>.45f&&Age<.75f;
    for(int Hand=0;Hand<2;++Hand)
    {
        if(!(Hand==0?LeftActive:RightActive))continue;
        const FVector Position=Hand==0?Left:Right;
        FParticle Core;Core.Position=Position;Core.Shape=EShape::Glow;Core.Size=Heavy?4.5f:2.5f;
        Core.Life=.05f;Core.Color=FLinearColor(.25f,.8f,1.f,.6f);Add(Core);
        for(int I=0;I<(Heavy?3:1);++I)
        {
            const FVector Offset=FMath::VRand()*(Heavy?14.f:8.f);
            FParticle P;P.Position=Position+Offset;P.Velocity=-Offset/.09f;P.Life=.09f;P.Size=.55f;
            P.Color=I==2?FLinearColor(.65f,.15f,1.f,.8f):FLinearColor(.12f,.75f,1.f,.85f);Add(P);
        }
    }
}

void UHeliosFistVFXComponent::Trail(FVector Start,FVector End,bool Heavy)
{
    if(Start.Equals(End,.1f))return;
    FParticle P;P.Position=Start;P.End=End;P.Shape=EShape::Ribbon;P.Size=Heavy?3.5f:1.8f;P.Life=Heavy?.16f:.10f;P.Color=FLinearColor(.05f,.6f,1.f,.7f);Add(P);
    P.Size*=.25f;P.Color=FLinearColor(.75f,1.f,1.f,.9f);Add(P);
}
void UHeliosFistVFXComponent::Swing(int32 PunchIndex,FVector Position,FVector Direction)
{
    const bool Heavy=PunchIndex==2;
    if(FParse::Param(FCommandLine::Get(),TEXT("UltimatumRecord")))UE_LOG(LogTemp,Display,TEXT("FIST_AUDIO Swing heavy=%d time=%.6f"),Heavy,GetWorld()->GetTimeSeconds());
    Sound(Whoosh,Position,Heavy?.85f:.55f);

}
void UHeliosFistVFXComponent::Impact(int32 PunchIndex,FVector Fist,FVector Position,FVector Direction)
{
    const bool Heavy=PunchIndex==2;
    if(FParse::Param(FCommandLine::Get(),TEXT("UltimatumRecord")))UE_LOG(LogTemp,Display,TEXT("FIST_AUDIO Hit heavy=%d time=%.6f"),Heavy,GetWorld()->GetTimeSeconds());
    ++Hits;Sound(Heavy?HeavyPunch:Punch,Position,Heavy?1.f:.75f);
    FParticle Core;Core.Position=Position;Core.Shape=EShape::Glow;Core.Size=Heavy?14.f:8.f;Core.Life=Heavy?.07f:.045f;Core.Color=FLinearColor(1.f,1.f,1.f,1.f);Add(Core);
    for(int I=0;I<(Heavy?28:12);++I)
    {
        FParticle P;P.Position=Position;P.Velocity=(Direction*.9f+FMath::VRand()*.65f)*(Heavy?320.f:200.f);
        P.Size=I%4?.65f:1.1f;P.Life=Heavy?.24f:.15f;
        P.Color=I%5==0?FLinearColor(.65f,.2f,1.f,.9f):I%3?FLinearColor(.12f,.7f,1.f,.9f):FLinearColor(.85f,1.f,1.f,1.f);Add(P);
    }
    // Short radial streaks remain anchored at contact; no disks or remote connecting arcs.
    FVector Right,Up;Direction.FindBestAxisVectors(Right,Up);
    for(int I=0;I<(Heavy?7:4);++I)
    {
        const float Angle=I*2.39996f;
        const FVector Axis=Right*FMath::Cos(Angle)+Up*FMath::Sin(Angle);
        FParticle P;P.Shape=EShape::Ribbon;P.Position=Position+Axis*2;P.End=Position+Axis*(Heavy?24.f:12.f);
        P.Size=Heavy?1.4f:.8f;P.Life=.095f;P.Color=FLinearColor(.45f,.8f,1.f,.8f);Add(P);
    }
    if(Heavy)
    {
        auto* PC=UGameplayStatics::GetPlayerController(this,0);APlayerCameraManager* Manager=PC?PC->PlayerCameraManager.Get():nullptr;
        if(Manager)
        {
            const float Gain=FMath::Clamp(1.f-FVector::Dist(Position,Manager->GetCameraLocation())/1200.f,0.f,1.f);
            if(Gain>0)
            {
                if(!CameraKick.IsValid())CameraKick=Cast<UHeliosFistCameraModifier>(Manager->AddNewCameraModifier(UHeliosFistCameraModifier::StaticClass()));
                if(CameraKick.IsValid()){CameraKick->Remaining=.16f;CameraKick->Strength=Gain;}
            }
        }
    }
}
void UHeliosFistVFXComponent::Clear(){Particles.Reset();AttackTime=-1;bHasLastFist=false;ChargeStep=0;bChargeSound=false;if(Surface)Surface->ClearAllMeshSections();if(CameraKick.IsValid())CameraKick->Remaining=0;}

void UHeliosFistVFXComponent::EndPlay(EEndPlayReason::Type Reason)
{
    Clear();if(CameraKick.IsValid())if(auto* PC=UGameplayStatics::GetPlayerController(this,0))if(PC->PlayerCameraManager)PC->PlayerCameraManager->RemoveCameraModifier(CameraKick.Get());
    Super::EndPlay(Reason);
}
void UHeliosFistVFXComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Dt,Type,Function);if(!Surface)return;
    if(AttackTime>=0)
    {
        if(auto* Character=Cast<ACharacter>(GetOwner()))
        {
            const FVector Left=Character->GetMesh()->GetSocketLocation(TEXT("fist_l"));
            const FVector Right=Character->GetMesh()->GetSocketLocation(TEXT("fist_r"));
            Charge(Left,Right,AttackTime,Dt);
            if(bHasLastFist)
            {
                if((AttackTime>=.12f&&AttackTime<=.37f)||(AttackTime>=.99f&&AttackTime<=1.32f))Trail(LastFist[0],Left,AttackTime>.9f);
                if(AttackTime>=.49f&&AttackTime<=.78f)Trail(LastFist[1],Right,false);
            }
            LastFist[0]=Left;LastFist[1]=Right;bHasLastFist=true;
        }
    }
    else bHasLastFist=false;
    auto* PC=UGameplayStatics::GetPlayerController(this,0);if(!PC||!PC->PlayerCameraManager)return;
    const FVector View=PC->PlayerCameraManager->GetCameraLocation();const FTransform X=GetComponentTransform();
    TArray<FVector> V,N;TArray<int32> Indices;TArray<FVector2D> UV;TArray<FLinearColor> C;TArray<FProcMeshTangent> Tangents;
    auto Vertex=[&](FVector P,FVector2D Tex,FLinearColor Color){V.Add(X.InverseTransformPosition(P));N.Add(FVector::UpVector);UV.Add(Tex);C.Add(Color);};
    for(int PIndex=Particles.Num()-1;PIndex>=0;--PIndex)
    {
        FParticle& P=Particles[PIndex];if(P.Fresh)P.Fresh=false;else P.Age+=Dt;
        if(P.Age>=P.Life){Particles.RemoveAtSwap(PIndex);continue;}P.Position+=P.Velocity*Dt;
        FRotationMatrix Facing((View-P.Position).Rotation());FVector Right=Facing.GetUnitAxis(EAxis::Y),Up=Facing.GetUnitAxis(EAxis::Z);
        const float U=P.Age/P.Life;FLinearColor Color=P.Color;Color.A*=FMath::Square(1-U);const int Base=V.Num();
        if(P.Shape==EShape::Ribbon)
        {
            FVector Width=FVector::CrossProduct(P.End-P.Position,View-(P.End+P.Position)*.5f).GetSafeNormal()*P.Size*(1-U);
            Vertex(P.Position-Width,FVector2D(.5f,0),Color);Vertex(P.Position+Width,FVector2D(.5f,1),Color);
            Vertex(P.End+Width*.2f,FVector2D(.5f,1),Color);Vertex(P.End-Width*.2f,FVector2D(.5f,0),Color);Indices.Append({Base,Base+1,Base+2,Base,Base+2,Base+3});
        }
        else
        {
            const float Size=P.Size*(1-U*.5f);
            if(P.Shape==EShape::Spark&&!P.Velocity.IsNearlyZero()){Up=P.Velocity.GetSafeNormal();Right=FVector::CrossProduct(Up,View-P.Position).GetSafeNormal();Up*=2.5f;}
            for(FVector2D Q:{FVector2D(-1,-1),FVector2D(-1,1),FVector2D(1,1),FVector2D(1,-1)})Vertex(P.Position+(Right*Q.X+Up*Q.Y)*Size,Q*.5+FVector2D(.5,.5),Color);
            Indices.Append({Base,Base+1,Base+2,Base,Base+2,Base+3});
        }
    }
    if(V.IsEmpty())Surface->ClearAllMeshSections();else Surface->CreateMeshSection_LinearColor(0,V,Indices,N,UV,C,Tangents,false);
}
