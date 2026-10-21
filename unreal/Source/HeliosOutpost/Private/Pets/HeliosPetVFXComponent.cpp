#include "Pets/HeliosPetVFXComponent.h"
#include "Pets/HeliosPetDog.h"
#include "Audio/HeliosAudio.h"
#include "ProceduralMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "Weapons/HeliosWeaponVFX.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

UHeliosPetVFXComponent::UHeliosPetVFXComponent(){PrimaryComponentTick.bCanEverTick=true;}
void UHeliosPetVFXComponent::BeginPlay()
{
    Super::BeginPlay();
    auto* Mat=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Helios/Pets/RoundDog/M_PetGlow.M_PetGlow"));
    auto Mesh=[&](FName Name){auto* M=NewObject<UProceduralMeshComponent>(GetOwner(),Name);GetOwner()->AddInstanceComponent(M);M->SetupAttachment(this);M->SetCollisionEnabled(ECollisionEnabled::NoCollision);M->SetCanEverAffectNavigation(false);M->SetCastShadow(false);M->RegisterComponent();M->SetMaterial(0,Mat);return M;};
    ArcMesh=Mesh(TEXT("Pet warm gold arc"));SparkMesh=Mesh(TEXT("Pet hit stars"));
    for(int32 I=0;I<3;++I)
    {
        auto* T=NewObject<UTextRenderComponent>(GetOwner());GetOwner()->AddInstanceComponent(T);T->SetupAttachment(this);
        T->SetText(FText::FromString(I==0?TEXT("Z"):TEXT("z")));T->SetHorizontalAlignment(EHTA_Center);T->SetVerticalAlignment(EVRTA_TextCenter);
        T->SetWorldSize(26+I*5);T->SetTextRenderColor(FColor(255,244,207));T->SetCollisionEnabled(ECollisionEnabled::NoCollision);T->SetCastShadow(false);T->RegisterComponent();T->SetVisibility(false);Letters.Add(T);
    }
    SwingSound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Helios/Pets/RoundDog/SFX_PetSwing.SFX_PetSwing"));
    HitSound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Helios/Pets/RoundDog/SFX_PetHit.SFX_PetHit"));
    BlockSound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Helios/Pets/RoundDog/SFX_PetBlock.SFX_PetBlock"));
    ImpactSystem=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Helios/Weapons/Realism/Impacts/NS_Impact_Armor.NS_Impact_Armor"));
}
void UHeliosPetVFXComponent::PlayNearby(USoundBase* Sound,const FVector& Position,float Volume)
{
    auto* PC=UGameplayStatics::GetPlayerController(this,0);
    if(PC&&PC->PlayerCameraManager){const float Gain=FMath::Clamp(1.f-FVector::Dist(Position,PC->PlayerCameraManager->GetCameraLocation())/3500.f,0.f,1.f);if(Gain>.01f)HeliosAudio::PlaySoundAtLocation(this,Sound,Position,Volume*Gain*Gain*(Cast<AHeliosPetCompanion>(GetOwner())?Cast<AHeliosPetCompanion>(GetOwner())->RosterAudioGain():1.f));}
}
void UHeliosPetVFXComponent::OnPooledEffectFinished(UNiagaraComponent* Component)
{ Component->OnSystemFinished.RemoveDynamic(this,&UHeliosPetVFXComponent::OnPooledEffectFinished);PooledEffects.Remove(Component); }
void UHeliosPetVFXComponent::BeginSwing(float InReach,bool Diagonal)
{
    SwingAge=0;Reach=InReach;bDiagonal=Diagonal;SwingOrigin=GetOwner()->GetActorLocation()+FVector(0,0,12);SwingRotation=GetOwner()->GetActorRotation();
}
void UHeliosPetVFXComponent::Burst(const FVector& Position,bool Shield)
{
    auto* PC=UGameplayStatics::GetPlayerController(this,0);const FVector View=PC&&PC->PlayerCameraManager?PC->PlayerCameraManager->GetCameraLocation():Position+FVector(100,0,0);
    const FRotationMatrix R((View-Position).Rotation());const FVector Right=R.GetUnitAxis(EAxis::Y),Up=R.GetUnitAxis(EAxis::Z);
    if(Sparks.Num()>40)Sparks.RemoveAt(0,Sparks.Num()-40);
    for(int32 I=0;I<10;++I)
    {
        const float A=I*UE_TWO_PI/10;FSpark P;P.Position=Position;P.Velocity=(Right*FMath::Cos(A)+Up*FMath::Sin(A))*(Shield?95.f:145.f);
        P.Size=I%3==0?15.f:8.f;P.Life=Shield?.32f:.26f;P.Color=I%3==0?FLinearColor(1.f,.94f,.67f,1):FLinearColor(1.f,.55f,.10f,1);Sparks.Add(P);
    }
    FSpark Center;Center.Position=Position;Center.Velocity=FVector::ZeroVector;Center.Size=Shield?34:30;Center.Life=Shield?.3f:.15f;Center.Color=FLinearColor(1,.98,.85,1);Center.Ring=Shield;Sparks.Add(Center);
    PlayNearby(Shield?BlockSound:HitSound,Position,.45f);
}
void UHeliosPetVFXComponent::Hit(const FVector& Position)
{
    ++HitBursts;Burst(Position,false);
    if(auto* Effect=UHeliosWeaponVFX::SpawnOneShot(this,ImpactSystem,Position,FRotator::ZeroRotator,FVector(.28f),.3f))
    {PooledEffects.Add(Effect);Effect->OnSystemFinished.AddUniqueDynamic(this,&UHeliosPetVFXComponent::OnPooledEffectFinished);}
}
void UHeliosPetVFXComponent::Block(const FVector& Position){Burst(Position,true);}
void UHeliosPetVFXComponent::SetSleeping(bool Sleeping)
{
    Clear();bSleeping=Sleeping;SleepAge=0;
}
void UHeliosPetVFXComponent::Clear()
{
    SwingAge=-1;Sparks.Reset();bSleeping=false;
    for(UNiagaraComponent* Effect:PooledEffects)if(IsValid(Effect)){Effect->OnSystemFinished.RemoveDynamic(this,&UHeliosPetVFXComponent::OnPooledEffectFinished);UHeliosWeaponVFX::ReleaseTracked(Effect);}PooledEffects.Reset();
    if(ArcMesh)ArcMesh->ClearAllMeshSections();if(SparkMesh)SparkMesh->ClearAllMeshSections();for(UTextRenderComponent* T:Letters)if(T)T->SetVisibility(false);
}
void UHeliosPetVFXComponent::UpdateArc()
{
    if(SwingAge<.2f||SwingAge>.53f){ArcMesh->ClearAllMeshSections();return;}
    TArray<FVector> V,N;TArray<int32> Indices;TArray<FVector2D> UV;TArray<FLinearColor> C;TArray<FProcMeshTangent> Tangents;
    const float Progress=FMath::Clamp((SwingAge-.2f)/.15f,0.f,1.f),Fade=FMath::Clamp((.53f-SwingAge)/.16f,0.f,1.f);
    FRotator Tilt=SwingRotation;if(bDiagonal)Tilt.Roll=26.f;
    const FTransform World=GetComponentTransform();
    FCollisionQueryParams Q(SCENE_QUERY_STAT(PetArcOcclusion),false,GetOwner());Q.AddIgnoredActor(GetOwner()->GetOwner());
    for(int Layer=0;Layer<2;++Layer)
    {
        const int Base=V.Num();const float Width=Layer==0?38.f:9.f;
        for(int I=0;I<=32;++I)
        {
            const float T=I/32.f;const float Lead=-60.f+120.f*Progress;const float Angle=FMath::DegreesToRadians(FMath::Lerp(FMath::Max(-60.f,Lead-76.f),Lead,T));
            FVector Direction=Tilt.RotateVector(FVector(FMath::Cos(Angle),FMath::Sin(Angle),0));
            float Radius=Reach;FHitResult Hit;
            if(GetWorld()->LineTraceSingleByChannel(Hit,SwingOrigin,SwingOrigin+Direction*Reach,ECC_Visibility,Q))Radius=FMath::Max(25.f,Hit.Distance-3.f);
            for(int Edge=0;Edge<2;++Edge)
            {
                V.Add(World.InverseTransformPosition(SwingOrigin+Direction*FMath::Max(15.f,Radius-(Edge?0:Width))));N.Add(FVector::UpVector);UV.Add(FVector2D(T,Edge));
                FLinearColor Color=Layer==0?FLinearColor(1,.57f,.08f,1):FLinearColor(1,.95f,.66f,1);Color.A=Fade*FMath::Pow(T,.7f)*(Layer==0?.48f:.95f);C.Add(Color);
            }
            if(I>0){int J=Base+(I-1)*2;Indices.Append({J,J+2,J+1,J+1,J+2,J+3});}
        }
    }
    ArcMesh->CreateMeshSection_LinearColor(0,V,Indices,N,UV,C,Tangents,false);
}
void UHeliosPetVFXComponent::UpdateSparks()
{
    if(Sparks.IsEmpty()){SparkMesh->ClearAllMeshSections();return;}
    auto* PC=UGameplayStatics::GetPlayerController(this,0);const FRotator Camera=PC&&PC->PlayerCameraManager?PC->PlayerCameraManager->GetCameraRotation():FRotator::ZeroRotator;
    const FRotationMatrix R(Camera);const FVector Right=R.GetUnitAxis(EAxis::Y),Up=R.GetUnitAxis(EAxis::Z);const auto World=GetComponentTransform();
    TArray<FVector> V,N;TArray<int32> Indices;TArray<FVector2D> UV;TArray<FLinearColor> C;TArray<FProcMeshTangent> Tangents;
    for(const FSpark& P:Sparks)
    {
        int Base=V.Num();FLinearColor Color=P.Color;Color.A=FMath::Clamp(1-P.Age/P.Life,0.f,1.f);float Size=P.Size*(.7f+.3f*Color.A);
        if(P.Ring)
        {
            for(int I=0;I<=32;++I)
            {
                const float A=I*UE_TWO_PI/32;for(int Edge=0;Edge<2;++Edge){float Rad=P.Size+P.Age*60-Edge*4;V.Add(World.InverseTransformPosition(P.Position+(Right*FMath::Cos(A)+Up*FMath::Sin(A))*Rad));N.Add(FVector::UpVector);UV.Add(FVector2D(I/32.f,Edge));C.Add(Color);}
                if(I>0){int J=Base+(I-1)*2;Indices.Append({J,J+2,J+1,J+1,J+2,J+3});}
            }
            continue;
        }
        V.Add(World.InverseTransformPosition(P.Position));N.Add(FVector::UpVector);UV.Add(FVector2D(.5,.5));C.Add(Color);
        for(int I=0;I<8;++I)
        {
            float A=I*UE_PI/4,Rad=Size*(I%2?.22f:1.f);V.Add(World.InverseTransformPosition(P.Position+(Right*FMath::Cos(A)+Up*FMath::Sin(A))*Rad));N.Add(FVector::UpVector);UV.Add(FVector2D(0,0));C.Add(Color);
            Indices.Append({Base,Base+I+1,Base+(I+1)%8+1});
        }
    }
    SparkMesh->CreateMeshSection_LinearColor(0,V,Indices,N,UV,C,Tangents,false);
}
void UHeliosPetVFXComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Dt,Type,Function);if(Dt<=0||UGameplayStatics::IsGamePaused(this))return;
    if(SwingAge>=0)
    {
        // The pet can translate and turn during a swing; keep the visible blade
        // arc and its wall clipping aligned with the current melee origin.
        SwingOrigin=GetOwner()->GetActorLocation()+FVector(0,0,12);
        SwingRotation=GetOwner()->GetActorRotation();
        float Before=SwingAge;SwingAge+=Dt;if(Before<.2f&&SwingAge>=.2f)PlayNearby(SwingSound,SwingOrigin,.35f);
        if(SwingAge>.54f){SwingAge=-1;ArcMesh->ClearAllMeshSections();}else UpdateArc();
    }
    for(auto& P:Sparks){P.Age+=Dt;P.Position+=P.Velocity*Dt;P.Velocity.Z-=35*Dt;}Sparks.RemoveAll([](const FSpark& P){return P.Age>=P.Life;});UpdateSparks();
    if(bSleeping)
    {
        SleepAge+=Dt;auto* PC=UGameplayStatics::GetPlayerController(this,0);
        for(int32 I=0;I<Letters.Num();++I)
        {
            float Phase=FMath::Fmod(FMath::Max(0.f,SleepAge-.6f)+I*.8f,2.4f)/2.4f;
            auto* T=Letters[I].Get();T->SetVisibility(SleepAge>.6f&&Phase>.03f&&Phase<.97f);
            T->SetRelativeLocation(FVector(-8+18*FMath::Sin(Phase*UE_PI),I*15.f-15,65+Phase*62));T->SetWorldSize(24+14*Phase);
            if(PC&&PC->PlayerCameraManager)T->SetWorldRotation((PC->PlayerCameraManager->GetCameraLocation()-T->GetComponentLocation()).Rotation());
        }
    }
}
void UHeliosPetVFXComponent::EndPlay(const EEndPlayReason::Type Reason){Clear();Super::EndPlay(Reason);}
