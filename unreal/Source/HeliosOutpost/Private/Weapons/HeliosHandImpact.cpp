#include "Weapons/HeliosHandImpact.h"
#include "Audio/HeliosAudio.h"
#include "Weapons/HeliosWeaponVFX.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DecalComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"

namespace
{
template<class T> T* ImpactAsset(const TCHAR* Name)
{ return LoadObject<T>(nullptr,*(FString(TEXT("/Game/Helios/Weapons/MysteriousHand/"))+Name)); }
}

AHeliosHandImpact::AHeliosHandImpact()
{
    PrimaryActorTick.bCanEverTick=true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("ImpactRoot")));
    Flash=CreateDefaultSubobject<UPointLightComponent>(TEXT("Flash"));Flash->SetupAttachment(RootComponent);
    Flash->SetCastShadows(false);Flash->SetIntensity(0);Flash->SetAttenuationRadius(2000);
    Wave=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SurfaceWave"));Wave->SetupAttachment(RootComponent);
    Heat=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeatHaze"));Heat->SetupAttachment(RootComponent);
    for(auto* M:{Wave.Get(),Heat.Get()}){M->SetCollisionEnabled(ECollisionEnabled::NoCollision);M->SetCastShadow(false);M->SetReceivesDecals(false);}
    Scorch=CreateDefaultSubobject<UDecalComponent>(TEXT("Scorch"));Scorch->SetupAttachment(RootComponent);
    Scorch->SetVisibility(false);
}

void AHeliosHandImpact::Initialize(const FVector& SurfaceNormal,float BlastRadius)
{
    Radius=FMath::Max(1.f,BlastRadius);const FVector N=SurfaceNormal.GetSafeNormal(SMALL_NUMBER,FVector::UpVector);
    // Limit presentation accumulation only; never suppress projectile damage.
    int32 Lights=0,Count=0;AHeliosHandImpact* Oldest=nullptr;
    for(TActorIterator<AHeliosHandImpact> It(GetWorld());It;++It)
    {
        if(*It==this)continue;++Count;
        if(!Oldest||It->Age>Oldest->Age)Oldest=*It;
        if(It->Age<.3f&&++Lights>=2)It->Flash->SetVisibility(false);
    }
    if(Count>=16&&Oldest)Oldest->Destroy();
    Wave->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane")));
    Wave->SetRelativeLocation(N*8);Wave->SetWorldRotation(FRotationMatrix::MakeFromZ(N).Rotator());
    WaveMaterial=UMaterialInstanceDynamic::Create(ImpactAsset<UMaterialInterface>(TEXT("M_MH_Shockwave")),this);
    Wave->SetMaterial(0,WaveMaterial);
    Heat->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));
    Heat->SetRelativeLocation(N*70);
    HeatMaterial=UMaterialInstanceDynamic::Create(ImpactAsset<UMaterialInterface>(TEXT("M_MH_Heat")),this);
    Heat->SetMaterial(0,HeatMaterial);
    Flash->SetRelativeLocation(N*140);Flash->SetLightColor(FLinearColor(1,.46,.12));
    Flash->SetIntensity(220000);Flash->SetAttenuationRadius(Radius*2);
    for(const TCHAR* Name:{TEXT("NS_MH_Explosion"),TEXT("NS_MH_SurfaceDust")})
    {
        const bool Surface=FCString::Strcmp(Name,TEXT("NS_MH_SurfaceDust"))==0;
        if(auto* FX=UHeliosWeaponVFX::SpawnOneShot(this,ImpactAsset<UNiagaraSystem>(Name),GetActorLocation()+N*(Surface?14:65),Surface?FRotationMatrix::MakeFromZ(N).Rotator():FRotator::ZeroRotator,FVector(Radius/1000.f),4.6f))
        {FX->SetVariableVec3(TEXT("User.Direction"),N);FX->SetVariableFloat(TEXT("User.BlastRadius"),Radius);FX->ReinitializeSystem();Effects.Add(FX);}
    }
    FHitResult Ground;FCollisionQueryParams Q(SCENE_QUERY_STAT(MHScorch),false,this);
    if(GetWorld()->LineTraceSingleByChannel(Ground,GetActorLocation()+N*35,GetActorLocation()-N*120,ECC_Visibility,Q)&&Ground.GetActor()&&!Ground.GetActor()->IsA<APawn>())
    {
        Scorch->SetDecalMaterial(ImpactAsset<UMaterialInterface>(TEXT("M_MH_Scorch")));
        Scorch->SetWorldLocation(Ground.ImpactPoint+Ground.ImpactNormal*2);
        Scorch->SetWorldRotation(FRotationMatrix::MakeFromX(-Ground.ImpactNormal).Rotator());
        Scorch->DecalSize=FVector(35,Radius*.36f,Radius*.36f);Scorch->SetFadeOut(4,4,false);Scorch->SetVisibility(true);
    }
    HeliosAudio::PlaySoundAtLocation(this,ImpactAsset<USoundBase>(TEXT("SFX_MH_ExplosionCinematic")),GetActorLocation(),1.f,FMath::FRandRange(.96f,1.04f));
    UE_LOG(LogTemp,Display,TEXT("MH_IMPACT time=%.4f x=%.1f y=%.1f z=%.1f radius=%.1f"),GetWorld()->GetTimeSeconds(),GetActorLocation().X,GetActorLocation().Y,GetActorLocation().Z,Radius);
    UGameplayStatics::PlayWorldCameraShake(this,UHeliosHandImpactShake::StaticClass(),GetActorLocation(),300,2800,1.5f,false);
    SetLifeSpan(8.1f);Tick(0);
}

void AHeliosHandImpact::Tick(float Dt)
{
    Super::Tick(Dt);Age+=Dt;
    const float T=FMath::Clamp((Age-.06f)/.59f,0.f,1.f);
    const float Expansion=1-FMath::Square(1-T);
    Wave->SetWorldScale3D(FVector(FMath::Max(.02f,2*Radius*Expansion/91.f)));
    if(WaveMaterial)WaveMaterial->SetScalarParameterValue(TEXT("Age"),Age);
    Heat->SetWorldScale3D(FVector(FMath::Max(.1f,Radius*.009f*FMath::Clamp(Age/.3f,0.f,1.f))));
    if(HeatMaterial)HeatMaterial->SetScalarParameterValue(TEXT("Strength"),.055f*FMath::Square(FMath::Max(0.f,1-Age/.32f)));
    Flash->SetIntensity(220000*FMath::Exp(-Age*20));
    if(Age>.35f){Flash->SetVisibility(false);Heat->SetVisibility(false);}
    if(Age>.7f)Wave->SetVisibility(false);
    if(Age>4.7f){Effects.Empty();SetActorTickEnabled(false);}
}

void AHeliosHandImpact::EndPlay(const EEndPlayReason::Type Reason)
{
    // Niagara components return to the shared pool through their own lifetime timers.
    // Do not release a stored pooled pointer here: it may already represent a newer effect.
    Super::EndPlay(Reason);
}

void UHeliosHandImpactPattern::GetShakePatternInfoImpl(FCameraShakeInfo& Info) const
{Info.Duration=FCameraShakeDuration(.22f);Info.BlendIn=0;Info.BlendOut=.1f;}
void UHeliosHandImpactPattern::UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& P,FCameraShakePatternUpdateResult& R)
{
    Age+=P.DeltaTime;const float E=FMath::Square(FMath::Max(0.f,1-Age/.22f));
    R.Location.Z=FMath::Sin(Age*125)*1.4f*E;
    R.Rotation.Pitch=FMath::Sin(Age*103)*.32f*E;R.Rotation.Roll=FMath::Sin(Age*137)*.15f*E;
}
UHeliosHandImpactShake::UHeliosHandImpactShake(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{bSingleInstance=true;SetRootShakePattern(CreateDefaultSubobject<UHeliosHandImpactPattern>(TEXT("ImpactPattern")));}
