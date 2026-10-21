#include "Weapons/HeliosCalamityStaff.h"
#include "Weapons/HeliosWeaponVFX.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DecalComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/AudioComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "HAL/IConsoleManager.h"
#include "UObject/ConstructorHelpers.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

namespace {
template<class T>T* Resource(const UHeliosCalamityImpactDefinition* P,const TCHAR* Key){const auto* V=P->RangeResources.Find(FName(Key));if(!V||!V->Get())V=P->Resources.Find(FName(Key));return V?Cast<T>(V->Get()):nullptr;}
FLinearColor ImpactColor(bool Fire){return Fire?FLinearColor(1,.23f,.018f):FLinearColor(.08f,.85f,.16f);}
void Params(UMaterialInstanceDynamic* M,float Age,float Opacity,float Gain=1){if(M){M->SetScalarParameterValue(TEXT("EffectAge"),Age);M->SetScalarParameterValue(TEXT("Opacity"),Opacity);M->SetScalarParameterValue(TEXT("EmissiveGain"),Gain);}}
}
UHeliosCalamityImpactDefinition::UHeliosCalamityImpactDefinition()
{
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Pool(TEXT("/Game/Helios/Weapons/Calamity/Impact/M_CI_PoolRange"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Boundary(TEXT("/Game/Helios/Weapons/Calamity/Impact/M_CI_BoundaryRange"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Shockwave(TEXT("/Game/Helios/Weapons/Calamity/Impact/M_CI_ShockwaveRange"));
    RangeResources.Add(TEXT("M_CI_Pool"),Pool.Object);
    RangeResources.Add(TEXT("M_CI_Boundary"),Boundary.Object);
    RangeResources.Add(TEXT("M_CI_Shockwave"),Shockwave.Object);
}
void UHeliosCalamityImpactDefinition::Prewarm()
{
#if WITH_EDITOR
    // Resolve mesh/texture jobs before finishing materials, including the active SM5 quality.
    FAssetCompilingManager::Get().FinishAllCompilation();
    for(const auto& Pair:Resources)if(auto* M=Cast<UMaterialInterface>(Pair.Value))M->EnsureIsComplete();
    for(const auto& Pair:RangeResources)if(auto* M=Cast<UMaterialInterface>(Pair.Value))M->EnsureIsComplete();
    if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
#endif
}
void AHeliosCalamityEffect::BuildImpactPresentation()
{
    const auto* P=State->Settings->ImpactPresentation.Get();const bool Fire=Spell==EHeliosCalamitySpell::Fireball;
    Tail=Fire?FMath::Max(.1f,P->FireVisualSeconds-Duration):P->PoisonFadeSeconds;
    auto Add=[&](const TCHAR* Mesh,const TCHAR* Material){auto* C=NewObject<UStaticMeshComponent>(this);C->SetupAttachment(RootComponent);C->SetStaticMesh(Resource<UStaticMesh>(P,Mesh));C->SetMaterial(0,Resource<UMaterialInterface>(P,Material));C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetCastShadow(false);C->SetCanEverAffectNavigation(false);C->SetCullDistance(8000);C->RegisterComponent();Parts.Add(C);if(auto* M=C->CreateDynamicMaterialInstance(0)){M->SetVectorParameterValue(TEXT("SpellColor"),ImpactColor(Fire));M->SetVectorParameterValue(TEXT("EffectCenter"),GetActorLocation());M->SetScalarParameterValue(TEXT("EffectRadius"),Radius);Params(M,0,0,P->EmissiveGain);Materials.Add(M);}return C;};
    if(Fire){
        for(int32 I=0;I<6;++I){auto* C=Add(TEXT("SM_CI_FlamePetal"),TEXT("M_CI_FlamePetal"));C->SetRelativeRotation(FRotator(0,I*60+(I%2?7:-3),0));}
        Add(TEXT("SM_CI_Surface"),TEXT("M_CI_Shockwave"));Add(TEXT("SM_CS_Seal"),TEXT("M_CI_Sigil"));
    }else{
        Add(TEXT("SM_CI_LiquidCrown"),TEXT("M_CI_LiquidCrown"));Add(TEXT("SM_CI_Surface"),TEXT("M_CI_Pool"));Add(TEXT("SM_CI_Surface"),TEXT("M_CI_Boundary"));Add(TEXT("SM_CS_Seal"),TEXT("M_CI_Sigil"));
        Parts[1]->SetVisibility(bImpactGround);Parts[2]->SetVisibility(bImpactGround);Parts[3]->SetVisibility(bImpactGround);
    }
    if(bImpactGround){ImpactDecal=NewObject<UDecalComponent>(this);ImpactDecal->SetupAttachment(RootComponent);ImpactDecal->SetRelativeRotation(FRotator(-90,0,0));ImpactDecal->DecalSize=FVector(22,Radius,Radius);ImpactDecal->SetDecalMaterial(Resource<UMaterialInterface>(P,Fire?TEXT("M_CI_Scorch"):TEXT("M_CI_Corrosion")));ImpactDecal->SetFadeScreenSize(.002f);ImpactDecal->RegisterComponent();DecalMaterial=ImpactDecal->CreateDynamicMaterialInstance();Params(DecalMaterial,0,0);}
    Debris=NewObject<UInstancedStaticMeshComponent>(this);Debris->SetupAttachment(RootComponent);Debris->SetStaticMesh(Resource<UStaticMesh>(P,Fire?TEXT("SM_CI_GlassShard"):TEXT("SM_CI_GlassShard")));Debris->SetMaterial(0,Resource<UMaterialInterface>(P,Fire?TEXT("M_CI_Ember"):TEXT("M_CI_Glass")));Debris->SetCollisionEnabled(ECollisionEnabled::NoCollision);Debris->SetCastShadow(false);Debris->SetCanEverAffectNavigation(false);Debris->SetCullDistance(5500);Debris->RegisterComponent();
    for(int32 I=0;I<32;++I)Debris->AddInstance(FTransform(FQuat::Identity,FVector::ZeroVector,FVector(0)));
    Light->SetLightColor(ImpactColor(Fire));Light->SetAttenuationRadius(Fire?1050:650);
}
void AHeliosCalamityEffect::PlayImpactLayers()
{
    const auto* P=State->Settings->ImpactPresentation.Get();const bool Fire=Spell==EHeliosCalamitySpell::Fireball;Phase=EHeliosCalamityPhase::Active;
    State->Sound(State->Settings->Spells[static_cast<int32>(Spell)].ImpactSound.LoadSynchronous(),RootComponent,FVector::ZeroVector,Fire?.95f:.8f);
    if(Fire){State->DamageArea(GetActorLocation(),Spell,Scale);if(LoopAudio)LoopAudio->Stop();LoopAudio=nullptr;State->Shake(GetActorLocation(),.32f);}else StartLoop();
    const float Distance=FVector::Dist(GetOwner()->GetActorLocation(),GetActorLocation());if(Distance>8000)return;
    auto Spawn=[&](const TCHAR* Name,float S,FVector Offset){if(auto* C=UHeliosWeaponVFX::SpawnOneShot(this,Resource<UNiagaraSystem>(P,Name),GetActorTransform().TransformPosition(Offset),GetActorRotation(),FVector(S*P->ParticleScale*Radius/600.f,S*P->ParticleScale*Radius/600.f,S*P->ParticleScale),3.3f)){C->ComponentTags.AddUnique(TEXT("Calamity"));C->ComponentTags.AddUnique(FName(*GetOwner()->GetName()));}};
    if(Fire){Spawn(TEXT("NS_CI_FireBloom"),1,FVector(0,0,135));if(Distance<4500)Spawn(TEXT("NS_CI_Smoke"),1,FVector(0,0,180));Spawn(TEXT("NS_CI_Sparks"),1,FVector(0,0,35));}
    else {Spawn(TEXT("NS_CI_Splash"),1,FVector(0,0,35));Spawn(TEXT("NS_CI_Mist"),1,FVector(0,0,28));}
}
void AHeliosCalamityEffect::TickImpactPresentation(float Dt)
{
    const auto* P=State->Settings->ImpactPresentation.Get();const bool Fire=Spell==EHeliosCalamitySpell::Fireball;FieldAge+=Dt;
    const float End=Fire?P->FireVisualSeconds:Duration+P->PoisonFadeSeconds;
    const float Fade=Fire?FMath::Clamp((End-FieldAge)/1.1f,0.f,1.f):FMath::Clamp((End-FieldAge)/P->PoisonFadeSeconds,0.f,1.f);
    if(FieldAge>=Duration){Phase=EHeliosCalamityPhase::Dissipating;if(!bTailPlayed){bTailPlayed=true;State->Sound(State->Settings->Spells[static_cast<int32>(Spell)].TailSound.LoadSynchronous(),RootComponent,FVector::ZeroVector,.7f);}}
    if(Fire){
        for(int32 I=0;I<6;++I){const float T=FMath::Max(0.f,FieldAge-.018f*(I%3));const float Rise=FMath::SmoothStep(0.f,P->FireRiseSeconds,T);const float Dissolve=1-FMath::SmoothStep(.45f,1.2f,T);const float Spread=.12f+Rise*(.86f+(I%2)*.12f);Parts[I]->SetRelativeScale3D(FVector(Spread*Radius/400.f,Spread*Radius/400.f,(P->FireHeight/400)*(.12f+.88f*Rise)*(1-.25f*(1-Dissolve))));Parts[I]->SetVisibility(T<1.2f);Params(Materials[I],T,Dissolve,P->EmissiveGain);}
        const float Wave=FMath::Clamp((FieldAge-.04f)/.41f,0.f,1.f);Parts[6]->SetRelativeScale3D(FVector(Radius/100*1.02f,Radius/100*1.02f,1));Parts[6]->SetRelativeLocation(FVector(0,0,6));Parts[6]->SetVisibility(bImpactGround&&FieldAge<.7f);Params(Materials[6],FieldAge,(1-FMath::SmoothStep(.35f,.7f,FieldAge)),P->EmissiveGain);Materials[6]->SetScalarParameterValue(TEXT("WaveRadius"),Wave);
        Parts[7]->SetRelativeScale3D(FVector(Radius/100));Parts[7]->SetRelativeLocation(FVector(0,0,4));Parts[7]->SetVisibility(bImpactGround&&FieldAge<.55f);Params(Materials[7],FieldAge,.6f*(1-FMath::SmoothStep(.12f,.55f,FieldAge)),P->EmissiveGain);
        Params(DecalMaterial,FieldAge,Fade*FMath::SmoothStep(.1f,.5f,FieldAge),P->EmissiveGain);
        Light->SetIntensity((10000*FMath::Exp(-FieldAge*13)+2600*FMath::Exp(-FieldAge*3))*P->EmissiveGain);
    }else{
        const float Rise=FMath::SmoothStep(.03f,.22f,FieldAge),Collapse=1-FMath::SmoothStep(.24f,.64f,FieldAge);
        Parts[0]->SetRelativeScale3D(FVector((.3f+Rise*1.2f)*Radius/600.f,(.3f+Rise*1.2f)*Radius/600.f,P->PoisonCrownHeight/150*Rise*FMath::Max(.02f,Collapse)));Parts[0]->SetVisibility(FieldAge<.7f);Params(Materials[0],FieldAge,Collapse,P->EmissiveGain);
        const float Spread=FMath::SmoothStep(0.f,P->PoisonSpreadSeconds,FieldAge);Parts[1]->SetRelativeScale3D(FVector(Radius/100*1.02f,Radius/100*1.02f,1));Parts[1]->SetRelativeLocation(FVector(0,0,2));Params(Materials[1],FieldAge,Fade,FieldAge<Duration?P->EmissiveGain:0);Materials[1]->SetScalarParameterValue(TEXT("Spread"),Spread*Fade);
        Parts[2]->SetRelativeScale3D(FVector(Radius/100*1.02f,Radius/100*1.02f,1));Parts[2]->SetRelativeLocation(FVector(0,0,3));Params(Materials[2],FieldAge,FieldAge<Duration?.65f:0,P->EmissiveGain);
        Parts[3]->SetRelativeScale3D(FVector(Radius/100));Parts[3]->SetRelativeLocation(FVector(0,0,4));Params(Materials[3],FieldAge,.45f*(1-FMath::SmoothStep(.15f,.85f,FieldAge)),P->EmissiveGain);
        Params(DecalMaterial,FieldAge,.45f*Fade*Spread,0);Light->SetIntensity((1700*FMath::Exp(-FieldAge*12)+220)*Fade);
        if(FieldAge<Duration&&FieldAge>=ImpactFXAt&&FVector::DistSquared(GetOwner()->GetActorLocation(),GetActorLocation())<FMath::Square(4000.f)){
            ImpactFXAt=FieldAge+.8f;
            if(auto* C=UHeliosWeaponVFX::SpawnOneShot(this,Resource<UNiagaraSystem>(P,TEXT("NS_CI_Mist")),GetActorLocation()+ImpactNormal*20,GetActorRotation(),FVector(P->ParticleScale*Radius/600.f,P->ParticleScale*Radius/600.f,P->ParticleScale),1.1f)){C->ComponentTags.AddUnique(TEXT("Calamity"));C->ComponentTags.AddUnique(FName(*GetOwner()->GetName()));}
            if(auto* C=UHeliosWeaponVFX::SpawnOneShot(this,Resource<UNiagaraSystem>(P,TEXT("NS_CI_Bubbles")),GetActorLocation()+ImpactNormal*7,GetActorRotation(),FVector(P->ParticleScale*Radius/600.f,P->ParticleScale*Radius/600.f,P->ParticleScale),1.1f)){C->ComponentTags.AddUnique(TEXT("Calamity"));C->ComponentTags.AddUnique(FName(*GetOwner()->GetName()));}
        }
    }
    for(int32 I=0;I<32;++I){const float T=FieldAge,S=I*.618f,A=I*2.39996f,Speed=Fire?220+(I%6)*60:170+(I%6)*42;const float Z=(160+(I%7)*37)*T-490*T*T;const bool Alive=Z>0&&T<1.6f;const FVector Position(FMath::Cos(A)*Speed*T,FMath::Sin(A)*Speed*T,FMath::Max(0.f,Z));Debris->UpdateInstanceTransform(I,FTransform(FRotator(I*31+T*210,I*47+T*90,I*11),Position,FVector(Alive?(Fire?.7f:1.f)*(1+(I%3)*.25f):0)),false,I==31);}
    if(FieldAge>=End)Destroy();
}
