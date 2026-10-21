#include "Boss/HeliosBossEffectsComponent.h"
#include "Audio/HeliosAudio.h"
#include "Boss/HeliosEnemyBoss.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/AudioComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"

namespace
{
TArray<TWeakObjectPtr<UAudioComponent>> GlobalBossAudio;
TWeakObjectPtr<UWorld> LastAudioWorld, LastShakeWorld;
double LastAudioTime = -1000.0, LastShakeTime = -1000.0;
const TCHAR* FXRoot = TEXT("/Game/Helios/Enemies/E05Realistic/Effects/");
template<typename T> T* Asset(const TCHAR* Name)
{
    return LoadObject<T>(nullptr, *(FString(FXRoot) + Name + TEXT(".") + Name));
}
}

void UHeliosBossImpactPattern::GetShakePatternInfoImpl(FCameraShakeInfo& Info) const
{
    Info.Duration = FCameraShakeDuration(.24f); Info.BlendIn = .02f; Info.BlendOut = .16f;
}
void UHeliosBossImpactPattern::StartShakePatternImpl(const FCameraShakePatternStartParams&) { Elapsed=0.f; }
void UHeliosBossImpactPattern::UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& Result)
{
    Elapsed += Params.DeltaTime;
    const float Envelope=FMath::Square(FMath::Clamp(1.f-Elapsed/.24f,0.f,1.f));
    Result.Location = FVector(0.f,FMath::Sin(Elapsed*90.f)*.7f,FMath::Sin(Elapsed*73.f)*1.0f)*Envelope;
    Result.Rotation=FRotator(FMath::Sin(Elapsed*85.f)*.32f,0.f,FMath::Sin(Elapsed*67.f)*.20f)*Envelope;
}
UHeliosBossImpactShake::UHeliosBossImpactShake() : Super(FObjectInitializer::Get())
{
    bSingleInstance=true;
    SetRootShakePattern(CreateDefaultSubobject<UHeliosBossImpactPattern>(TEXT("ImpactPattern")));
}

UHeliosBossEffectsComponent::UHeliosBossEffectsComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PostPhysics;
}
UStaticMeshComponent* UHeliosBossEffectsComponent::MakeVisual(UStaticMesh* Mesh, UMaterialInterface* Material)
{
    if (!Mesh || !Material) return nullptr;
    auto* C=NewObject<UStaticMeshComponent>(GetOwner());
    C->SetMobility(EComponentMobility::Movable); C->SetStaticMesh(Mesh); C->SetMaterial(0,Material);
    C->SetCollisionEnabled(ECollisionEnabled::NoCollision); C->SetGenerateOverlapEvents(false);
    C->SetCastShadow(false); C->bReceivesDecals=false; C->SetCanEverAffectNavigation(false);
    C->RegisterComponent(); C->SetVisibility(false); return C;
}
void UHeliosBossEffectsComponent::BeginPlay()
{
    Super::BeginPlay();
    WindupFX=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Helios/Enemies/E05WhiteSuit/Effects/NS_WS_Windup"));
    TrailFX=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Helios/Enemies/E05WhiteSuit/Effects/NS_WS_Trail"));
    ImpactFX=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Helios/Enemies/E05WhiteSuit/Effects/NS_WS_Impact"));
    RingMesh=Asset<UStaticMesh>(TEXT("SM_SonicRing")); ArcMesh=Asset<UStaticMesh>(TEXT("SM_SonicArc"));
    DustMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
    AirMaterial=Asset<UMaterialInterface>(TEXT("M_SonicAir")); DustMaterial=Asset<UMaterialInterface>(TEXT("M_SonicDust"));
    WindupSound=Asset<USoundBase>(TEXT("SFX_BossBreath")); ConeSound=Asset<USoundBase>(TEXT("SFX_SonicCone"));
    RadialSound=Asset<USoundBase>(TEXT("SFX_SonicRadial")); ImpactSound=Asset<USoundBase>(TEXT("SFX_ShoulderImpact"));
    SpatialAttenuation=NewObject<USoundAttenuation>(this);
    SpatialAttenuation->Attenuation.bAttenuate=true;
    SpatialAttenuation->Attenuation.bSpatialize=true;
    SpatialAttenuation->Attenuation.AttenuationShapeExtents=FVector(180.f);
    SpatialAttenuation->Attenuation.FalloffDistance=2200.f;
    for (int32 I=0;I<3;++I) if (auto* C=MakeVisual(RingMesh,AirMaterial))
    { Waves.Add(C); WaveMaterials.Add(C->CreateAndSetMaterialInstanceDynamic(0)); }
    for (int32 I=0;I<18;++I) if (auto* C=MakeVisual(DustMesh,DustMaterial))
    { auto& D=Dust.AddDefaulted_GetRef(); D.Mesh=C; D.Material=C->CreateAndSetMaterialInstanceDynamic(0); }
}
FVector UHeliosBossEffectsComponent::GroundPoint() const
{
    FVector P=GetOwner()->GetActorLocation();
    if (auto* C=Cast<ACharacter>(GetOwner())) P.Z-=C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    return P+FVector(0,0,8);
}
void UHeliosBossEffectsComponent::BeginWindup(EHeliosBossAttackType Type,float Duration)
{
    EffectType=Type; Age=0.f; WindupDuration=Duration; bWindup=true; bReleased=false;
    EffectOrigin=GroundPoint();
    SpawnWhiteGold(WindupFX,GroundPoint()+FVector(0,0,110),GetOwner()->GetActorForwardVector(),true);
    for (auto W:Waves) if(W) W->SetVisibility(false);
    PlaySpatial(WindupSound,GetOwner()->GetActorLocation(),.40f);
}
void UHeliosBossEffectsComponent::ReleaseAttack(EHeliosBossAttackType Type,FVector Origin,FVector Direction)
{
    EffectType=Type; Age=0.f; TrailClock=0.f; bWindup=false; bReleased=true;
    EffectOrigin=Origin;
    if(Type!=EHeliosBossAttackType::SonicCone)EffectOrigin.Z=GroundPoint().Z;
    EffectDirection=Direction.GetSafeNormal2D();
    if(EffectDirection.IsNearlyZero())EffectDirection=GetOwner()->GetActorForwardVector();
    const bool bCone=Type==EHeliosBossAttackType::SonicCone;
    for(auto W:Waves) if(W) {W->SetVisibility(false);W->SetStaticMesh(bCone?ArcMesh:RingMesh);}
    if(Type==EHeliosBossAttackType::ShoulderCharge || Type==EHeliosBossAttackType::CloseShoulder)
    {SpawnDust(GroundPoint(),-EffectDirection,8,.7f);PlaySpatial(ConeSound,Origin,.48f);}
    else
    {PlaySpatial(bCone?ConeSound:RadialSound,Origin,bCone?.78f:.88f);SpawnDust(GroundPoint(),EffectDirection,bCone?10:16,.8f);Shake(.18f,Origin);}
}
void UHeliosBossEffectsComponent::Impact(FVector Location,FVector Normal,bool bPlayerHit)
{
    SpawnWhiteGold(ImpactFX,Location,Normal);
    SpawnDust(Location,Normal,12,1.f); PlaySpatial(ImpactSound,Location,.86f);
    if(bPlayerHit)Shake(.8f,Location);
}
void UHeliosBossEffectsComponent::WaveHit(FVector Location)
{
    Shake(.42f,Location);
}
void UHeliosBossEffectsComponent::SpawnDust(FVector Position,FVector Direction,int32 Count,float Strength)
{
    const bool bRadial=EffectType==EHeliosBossAttackType::SonicRadial;
    for(auto& D:Dust)
    {
        if(Count<=0)break; if(D.Lifetime>0.f)continue; --Count;
        const float Angle=FMath::FRandRange(-PI,PI);
        const FVector Rad(FMath::Cos(Angle),FMath::Sin(Angle),0);
        D.Age=0;D.Lifetime=FMath::FRandRange(.55f,1.15f);D.Size=FMath::FRandRange(.35f,.8f)*Strength;
        D.Velocity=((bRadial?Rad:Direction)*FMath::FRandRange(130.f,320.f)+Rad*50.f+FVector(0,0,FMath::FRandRange(40.f,100.f)))*Strength;
        D.Mesh->SetWorldLocation(Position+Rad*FMath::FRandRange(10.f,45.f)); D.Mesh->SetVisibility(true);
        D.Material->SetScalarParameterValue(TEXT("Opacity"),.22f);
    }
}
void UHeliosBossEffectsComponent::PlaySpatial(USoundBase* Sound,FVector Position,float Volume)
{
    if(!Sound||!GetWorld())return;
    GlobalBossAudio.RemoveAll([](auto C){return !C.IsValid()||!C->IsPlaying();});
    double Now=GetWorld()->GetTimeSeconds();
    if(LastAudioWorld==GetWorld()&&Now-LastAudioTime<.15)return;
    int32 Active=0;for(auto C:GlobalBossAudio)if(C.IsValid()&&C->GetWorld()==GetWorld())++Active;
    if(Active>=3)return;
    // Explicit distance falloff also works with an imported SoundWave lacking attenuation settings.
    FVector Ear=UGameplayStatics::GetPlayerCameraManager(this,0)?UGameplayStatics::GetPlayerCameraManager(this,0)->GetCameraLocation():Position;
    Volume*=FMath::Clamp(1.f-FVector::Distance(Ear,Position)/2400.f,.0f,1.f);
    if(Volume<.015f)return;
    auto* C=HeliosAudio::SpawnSoundAtLocation(this,Sound,Position,FRotator::ZeroRotator,Volume,FMath::FRandRange(.96f,1.04f),0.f,SpatialAttenuation);
    if(C){Audio.Add(C);GlobalBossAudio.Add(C);LastAudioWorld=GetWorld();LastAudioTime=Now;}
}
void UHeliosBossEffectsComponent::Shake(float Scale,FVector Position)
{
    auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0);if(!Camera||!GetWorld())return;
    const double Now=GetWorld()->GetTimeSeconds();
    if(LastShakeWorld==GetWorld()&&Now-LastShakeTime<.22)return;
    Scale*=FMath::Clamp(1.f-FVector::Distance(Camera->GetCameraLocation(),Position)/900.f,0.f,1.f);
    if(Scale>.025f){Camera->StartCameraShake(UHeliosBossImpactShake::StaticClass(),FMath::Min(Scale,.8f));LastShakeWorld=GetWorld();LastShakeTime=Now;}
}
void UHeliosBossEffectsComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(Dt,TickType,TickFunction); Age+=Dt;
    LiveFX.RemoveAll([](auto C){return !IsValid(C)||!C->IsActive();});
    if(bReleased && (EffectType==EHeliosBossAttackType::ShoulderCharge || EffectType==EHeliosBossAttackType::CloseShoulder))
    {
        TrailClock-=Dt;
        if(TrailClock<=0.f) {TrailClock=.065f; SpawnWhiteGold(TrailFX,GetOwner()->GetActorLocation()+FVector(0,0,30),-EffectDirection);}
    }
    auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0);
    if(bWindup&&Waves.Num()>0)
    {
        const float P=FMath::Clamp(Age/FMath::Max(.01f,WindupDuration),0.f,1.f);
        auto* W=Waves[0].Get();W->SetStaticMesh(RingMesh);W->SetWorldLocation(GroundPoint()+FVector(0,0,12));
        W->SetWorldScale3D(FVector(45.f+P*20.f,45.f+P*20.f,25.f));W->SetVisibility(true);
        WaveMaterials[0]->SetScalarParameterValue(TEXT("Opacity"),.04f+P*.12f);
    }
    else if(bReleased&&EffectType!=EHeliosBossAttackType::ShoulderCharge&&EffectType!=EHeliosBossAttackType::CloseShoulder)
    {
        const bool bCone=EffectType==EHeliosBossAttackType::SonicCone;
        const float Speed=bCone?2000.f:1000.f,MaxRadius=bCone?1400.f:800.f;
        for(int32 I=0;I<Waves.Num();++I)
        {
            const float T=Age-I*.075f,Radius=T*Speed;
            const bool bVisible=T>=0&&Radius<=MaxRadius;
            auto* W=Waves[I].Get();W->SetVisibility(bVisible);if(!bVisible)continue;
            const float Fade=FMath::Clamp(1.f-Radius/MaxRadius,0.f,1.f);
            // The cone leaves the mouth then broadens downwards toward player height.
            W->SetWorldLocation(EffectOrigin+FVector(0,0,bCone?-FMath::Min(Radius*.08f,65.f):48.f));
            W->SetWorldRotation(EffectDirection.Rotation());
            W->SetWorldScale3D(FVector(FMath::Max(15.f,Radius),FMath::Max(15.f,Radius),bCone?FMath::Lerp(45.f,180.f,FMath::Min(Radius/350.f,1.f)):90.f));
            WaveMaterials[I]->SetScalarParameterValue(TEXT("Opacity"),Fade*(I==0?.30f:.12f));
        }
        if(Age>MaxRadius/Speed+.2f)bReleased=false;
    }
    for(auto& D:Dust)
    {
        if(D.Lifetime<=0)continue;D.Age+=Dt;
        if(D.Age>=D.Lifetime){D.Lifetime=0;D.Mesh->SetVisibility(false);continue;}
        D.Velocity*=FMath::Max(0.f,1.f-Dt*1.7f);D.Velocity.Z+=Dt*12.f;
        D.Mesh->AddWorldOffset(D.Velocity*Dt);
        if(Camera){const FVector V=Camera->GetCameraLocation()-D.Mesh->GetComponentLocation();D.Mesh->SetWorldRotation(FRotationMatrix::MakeFromZ(V).Rotator());}
        D.Mesh->SetWorldScale3D(FVector(D.Size*(1.f+D.Age*1.6f)));
        D.Material->SetScalarParameterValue(TEXT("Opacity"),.20f*FMath::Sin(PI*D.Age/D.Lifetime));
    }
    Audio.RemoveAll([](auto C){return !IsValid(C)||!C->IsPlaying();});
}
void UHeliosBossEffectsComponent::StopAll()
{
    bWindup=false;bReleased=false;EffectType=EHeliosBossAttackType::None;
    for(auto C:LiveFX) if(IsValid(C)) C->Deactivate();
    LiveFX.Reset();
    for(auto W:Waves)if(W)W->SetVisibility(false);
    for(auto& D:Dust){D.Lifetime=0;if(D.Mesh)D.Mesh->SetVisibility(false);}
    for(auto C:Audio)if(IsValid(C.Get()))C->Stop();Audio.Reset();
}
void UHeliosBossEffectsComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    StopAll();for(auto W:Waves)if(W)W->DestroyComponent();for(auto& D:Dust)if(D.Mesh)D.Mesh->DestroyComponent();
    Super::EndPlay(Reason);
}

void UHeliosBossEffectsComponent::EndDrive()
{
    bReleased=false;
    for(auto W:Waves) if(W) W->SetVisibility(false);
}
void UHeliosBossEffectsComponent::SpawnWhiteGold(UNiagaraSystem* System,FVector Location,FVector Direction,bool bAttach)
{
    if(!System || !GetWorld() || LiveFX.Num()>=16) return;
    const auto* Boss=Cast<AHeliosEnemyBoss>(GetOwner());
    const float Scale=Boss && Boss->GetBossPhase()==2 ? 1.18f : 1.f;
    UNiagaraComponent* C=UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(),System,Location,Direction.Rotation(),FVector(Scale),true,true,ENCPoolMethod::None);
    if(C){ if(bAttach) C->AttachToComponent(GetOwner()->GetRootComponent(),FAttachmentTransformRules::KeepWorldTransform); LiveFX.Add(C); }
}
