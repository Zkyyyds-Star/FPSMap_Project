#include "HeliosWeaponVFXTools.h"
#include "UObject/UnrealType.h"
#include "Weapons/HeliosColaVFXSettings.h"
#include "Stateless/Modules/NiagaraStatelessModule_SolveVelocitiesAndForces.h"

#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "NiagaraMeshRendererProperties.h"
#include "Stateless/Modules/NiagaraStatelessModule_ScaleColor.h"
#include "Stateless/Modules/NiagaraStatelessModule_SubUVAnimation.h"
#include "Stateless/Modules/NiagaraStatelessModule_ScaleSpriteSize.h"
#include "Stateless/Modules/NiagaraStatelessModule_ScaleMeshSize.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemEmitterState.h"
#include "Materials/Material.h"
#include "Stateless/NiagaraStatelessEmitter.h"
#include "Stateless/NiagaraStatelessSpawnInfo.h"
#include "Stateless/Modules/NiagaraStatelessModule_AccelerationForce.h"
#include "Stateless/Modules/NiagaraStatelessModule_AddVelocity.h"
#include "Stateless/Modules/NiagaraStatelessModule_CurlNoiseForce.h"
#include "Stateless/Modules/NiagaraStatelessModule_Drag.h"
#include "Stateless/Modules/NiagaraStatelessModule_GravityForce.h"
#include "Stateless/Modules/NiagaraStatelessModule_InitializeParticle.h"
#include "Stateless/Modules/NiagaraStatelessModule_ShapeLocation.h"

namespace
{
struct FHeliosStatelessPreset
{
    float LifetimeMin;
    float LifetimeMax;
    FLinearColor Color;
    FVector2f SpriteSize;
    int32 SpawnCount;
    float SpeedMin;
    float SpeedMax;
    float ConeAngle;
    float Radius;
};

bool ResolvePreset(FName Name, FHeliosStatelessPreset& Out)
{
    if(Name==TEXT("CWMotes")){Out={.3f,.75f,FLinearColor(.7f,2.2f,3.5f,.65f),FVector2f(2.2f,34),100,240,550,50,300};return true;}
    if(Name==TEXT("CWChannel")){Out={.12f,.4f,FLinearColor(.65f,1.2f,1.8f,.55f),FVector2f(.6f,8),22,5,35,140,8};return true;}
    if(Name.ToString().StartsWith(TEXT("CSChannel"))){const int32 I=FCString::Atoi(*Name.ToString().Right(1));const FLinearColor C[]={FLinearColor(2,.9f,.1f,.8f),FLinearColor(3,.2f,.01f,.8f),FLinearColor(.1f,1.5f,3,.8f),FLinearColor(.4f,2,.05f,.8f)};Out={.12f,.48f,C[FMath::Clamp(I,0,3)],FVector2f(.8f,7),28,8,55,160,12};return true;}
    if(Name==TEXT("CIFireBloom")){Out={.55f,1.15f,FLinearColor(1,1,1,.8f),FVector2f(200,280),9,35,160,60,100};return true;}
    if(Name==TEXT("CISmoke")){Out={1.0f,2.4f,FLinearColor(.45f,.39f,.34f,.35f),FVector2f(300,360),6,40,105,65,150};return true;}
    if(Name==TEXT("CISparks")){Out={.3f,1.15f,FLinearColor(3,1.2f,.08f,.9f),FVector2f(2,12),70,180,750,150,25};return true;}
    if(Name==TEXT("CISplash")){Out={.18f,.65f,FLinearColor(.1f,.85f,.13f,.9f),FVector2f(8,20),44,150,460,120,35};return true;}
    if(Name==TEXT("CIMist")){Out={.55f,1.05f,FLinearColor(.05f,.28f,.10f,.32f),FVector2f(210,80),10,5,20,170,440};return true;}
    if(Name==TEXT("CIBubbles")){Out={.2f,.85f,FLinearColor(.2f,.95f,.14f,.75f),FVector2f(12,12),22,3,20,20,450};return true;}
    if(Name==TEXT("CSFireStream")){Out={.08f,.22f,FLinearColor(2.8f,.24f,.008f,.65f),FVector2f(34,48),180,500,1000,22,8};return true;}
    if(Name==TEXT("CSPoisonStream")){Out={.10f,.3f,FLinearColor(.16f,.6f,.018f,.35f),FVector2f(12,20),100,250,520,30,5};return true;}
    if(Name==TEXT("CSFireEmbers")){Out={.35f,1.1f,FLinearColor(4,1.1f,.05f,.9f),FVector2f(2,18),55,200,750,175,30};return true;}
    if(Name==TEXT("CSFireSmoke")){Out={.5f,1.4f,FLinearColor(.075f,.06f,.048f,.38f),FVector2f(150,180),22,55,210,160,90};return true;}
    if(Name==TEXT("CSPoisonGlass")){Out={.15f,.65f,FLinearColor(.45f,.8f,.55f,.85f),FVector2f(2,7),32,170,470,170,8};return true;}
    if(Name==TEXT("CSPoisonSplash")){Out={.18f,.6f,FLinearColor(.3f,1.2f,.025f,.8f),FVector2f(4,13),52,100,480,170,15};return true;}
    if(Name==TEXT("CSHeavyDust")){Out={.7f,1.8f,FLinearColor(.20f,.17f,.13f,.35f),FVector2f(80,95),36,30,160,150,100};return true;}
    if(Name==TEXT("CSCast")){Out={.12f,.5f,FLinearColor(.8f,.12f,2.2f,.8f),FVector2f(2,18),26,60,210,160,8};return true;}
    if(Name==TEXT("CSEarth")){Out={.25f,1.25f,FLinearColor(.55f,.3f,.07f,.5f),FVector2f(26,30),40,80,260,150,100};return true;}
    if(Name==TEXT("CSFire")){Out={.12f,.7f,FLinearColor(3.f,.35f,.015f,.85f),FVector2f(75,75),48,80,460,170,35};return true;}
    if(Name==TEXT("CSFireTrail")){Out={.12f,.42f,FLinearColor(2.4f,.20f,.008f,.65f),FVector2f(38,42),7,20,60,170,6};return true;}
    if(Name==TEXT("CSWind")){Out={.3f,1.2f,FLinearColor(.15f,.38f,.46f,.18f),FVector2f(70,95),16,20,70,170,100};return true;}
    if(Name==TEXT("CSPoison")){Out={.4f,1.6f,FLinearColor(.16f,.38f,.025f,.24f),FVector2f(52,65),18,15,50,160,140};return true;}
    if(Name==TEXT("CSPoisonTrail")){Out={.15f,.4f,FLinearColor(.3f,1.f,.018f,.6f),FVector2f(3,6),6,10,35,160,4};return true;}
    if(Name==TEXT("SBDarkBirth"))
    { Out={.4f,1.35f,FLinearColor(.16f,.48f,.30f,.55f),FVector2f(1.1f,20),22,20,115,65,28};return true; }
    if(Name==TEXT("SXBirth"))
    { Out={.3f,1.1f,FLinearColor(.20f,2.3f,1.5f,.8f),FVector2f(3,40),28,50,190,65,32};return true; }
    if(Name==TEXT("SXImpact")||Name==TEXT("SXImpactFar"))
    { Out={.06f,.30f,FLinearColor(1.8f,3.8f,2.8f,.95f),FVector2f(3,32),Name==TEXT("SXImpactFar")?8:18,100,360,105,3};return true; }
    if(Name==TEXT("SXDissolve"))
    { Out={.25f,.8f,FLinearColor(.13f,1.5f,1.1f,.65f),FVector2f(2,24),20,20,125,65,25};return true; }
    if(Name==TEXT("SXCast"))
    { Out={.08f,.38f,FLinearColor(.55f,3.1f,2.f,.9f),FVector2f(2,26),16,60,180,100,10};return true; }
    if(Name==TEXT("SBDarkImpact"))
    { Out={.08f,.34f,FLinearColor(.45f,1.4f,.8f,.85f),FVector2f(.8f,15),14,95,270,80,2};return true; }
    if(Name==TEXT("SBDarkDissolve"))
    { Out={.35f,.85f,FLinearColor(.11f,.35f,.23f,.4f),FVector2f(1,9),18,8,60,110,30};return true; }
    if(Name==TEXT("QXBirth"))
    { Out={.12f,.28f,FLinearColor(.12f,1.3f,.90f,.65f),FVector2f(1.2f,12),16,25,110,100,8};return true; }
    if(Name==TEXT("QXImpact"))
    { Out={.08f,.28f,FLinearColor(.50f,1.6f,1.2f,.85f),FVector2f(1.3f,19),22,100,380,110,2};return true; }
    if(Name==TEXT("QXDissolve"))
    { Out={.1f,.28f,FLinearColor(.28f,1.1f,.72f,.45f),FVector2f(1.2f,4),15,25,100,120,12};return true; }
    if (Name == TEXT("SoulBirth"))
    { Out={.25f,1.15f,FLinearColor(.04f,1.8f,.62f,.65f),FVector2f(2,15),26,70,280,70,12}; return true; }
    if (Name.ToString().StartsWith(TEXT("Impact")))
    {
        Out={.12f,.42f,FLinearColor(.32f,.29f,.25f,.62f),FVector2f(7,9),8,55,175,70,1};
        if (Name==TEXT("ImpactConcrete")) Out={.14f,.48f,FLinearColor(.48f,.46f,.41f,.7f),FVector2f(10,12),10,70,240,65,1.2f};
        else if (Name==TEXT("ImpactMetal") || Name==TEXT("ImpactArmor")) Out={.045f,.16f,FLinearColor(2.8f,1.25f,.22f,.85f),FVector2f(.7f,7),7,160,440,68,.5f};
        else if (Name==TEXT("ImpactWood")) Out={.12f,.34f,FLinearColor(.34f,.22f,.10f,.85f),FVector2f(1.5f,5),9,90,270,65,1};
        else if (Name==TEXT("ImpactSoil")) Out={.20f,.55f,FLinearColor(.24f,.19f,.12f,.7f),FVector2f(14,12),8,35,125,80,2};
        else if (Name==TEXT("ImpactCloth")) Out={.10f,.27f,FLinearColor(.36f,.35f,.32f,.6f),FVector2f(1,3),6,35,110,65,.5f};
        else if (Name==TEXT("ImpactSkin")) Out={.01f,.02f,FLinearColor(0,0,0,0),FVector2f(0,0),0,0,0,0,0};
        return true;
    }
    if (Name == TEXT("ColaBubbles"))
    { Out={.15f,.40f,FLinearColor(.95f,.86f,.65f,.65f),FVector2f(2.8f,2.8f),4,12,40,30,2}; return true; }
    if (Name == TEXT("ChopsticksRelease"))
    {
        Out = {.055f,.13f,FLinearColor(3.4f,1.8f,.26f,.8f),FVector2f(2,24),9,110,330,60,3};
        return true;
    }
    if (Name == TEXT("ChopsticksTrail"))
    {
        Out = {.09f,.20f,FLinearColor(2.8f,1.35f,.12f,.65f),FVector2f(1.5f,7),3,12,45,28,1};
        return true;
    }
    if (Name == TEXT("ChopsticksImpact"))
    {
        Out = {.10f,.32f,FLinearColor(4.2f,2.2f,.36f,.9f),FVector2f(2,17),18,160,640,100,1};
        return true;
    }
    if (Name == TEXT("RocketMuzzle"))
    {
        Out = {0.045f, 0.10f, FLinearColor(8.0f, 1.2f, 0.08f, 1.0f), FVector2f(24.0f, 16.0f), 10, 550.0f, 1150.0f, 14.0f, 3.0f};
        return true;
    }
    if (Name == TEXT("ShotgunMuzzle"))
    {
        Out = {0.035f, 0.06f, FLinearColor(6.0f, 0.75f, 0.04f, 0.92f), FVector2f(30.0f, 22.0f), 18, 650.0f, 1450.0f, 26.0f, 5.0f};
        return true;
    }
    if (Name == TEXT("RocketTrail"))
    {
        Out = {0.42f, 0.58f, FLinearColor(0.42f, 0.16f, 0.035f, 0.72f), FVector2f(28.0f, 28.0f), 3, 20.0f, 65.0f, 35.0f, 5.0f};
        return true;
    }
    if (Name == TEXT("ShotgunTracer"))
    {
        Out = {0.075f, 0.08f, FLinearColor(5.0f, 1.35f, 0.16f, 1.0f), FVector2f(2.0f, 100.0f), 1, 1.0f, 1.0f, 0.0f, 0.0f};
        return true;
    }
    if (Name == TEXT("EnemyImpact"))
    {
        Out = {0.18f, 0.42f, FLinearColor(3.6f, 0.22f, 0.06f, 0.86f), FVector2f(9.0f, 5.0f), 9, 90.0f, 360.0f, 95.0f, 4.0f};
        return true;
    }
    if (Name == TEXT("RifleMuzzle"))
    {
        Out = {0.030f, 0.055f, FLinearColor(7.5f, 1.15f, 0.09f, 0.95f), FVector2f(20.0f, 12.0f), 7, 700.0f, 1350.0f, 11.0f, 1.5f};
        return true;
    }
    if (Name == TEXT("ShotgunImpact"))
    {
        Out = {0.16f, 0.34f, FLinearColor(0.32f, 0.25f, 0.17f, 0.55f), FVector2f(16.0f, 12.0f), 12, 90.0f, 260.0f, 75.0f, 2.0f};
        return true;
    }
    if (Name == TEXT("LaserBeam"))
    {
        Out = {0.105f, 0.120f, FLinearColor(0.05f, 2.2f, 8.0f, 0.92f), FVector2f(2.0f, 100.0f), 1, 1.0f, 1.0f, 0.0f, 0.0f};
        return true;
    }
    if (Name == TEXT("LaserHit"))
    {
        Out = {0.045f, 0.11f, FLinearColor(0.08f, 1.5f, 7.0f, 0.86f), FVector2f(12.0f, 12.0f), 5, 20.0f, 90.0f, 80.0f, 1.0f};
        return true;
    }
    return false;
}

void EnsureStatelessEmitterCount(UNiagaraSystem* System, int32 DesiredCount)
{
    if (!System || System->GetEmitterHandles().IsEmpty())
    {
        return;
    }
    UNiagaraStatelessEmitter* Source = System->GetEmitterHandles()[0].GetStatelessEmitter();
    while (Source && System->GetEmitterHandles().Num() < DesiredCount)
    {
        const int32 Index = System->GetEmitterHandles().Num();
        UNiagaraStatelessEmitter* Duplicate = Cast<UNiagaraStatelessEmitter>(StaticDuplicateObject(Source, System));
        if (!Duplicate)
        {
            break;
        }
        Duplicate->ClearFlags(RF_Standalone | RF_Public);
        Duplicate->SetUniqueEmitterName(FString::Printf(TEXT("TrailLayer%d"), Index));
        FNiagaraEmitterHandle NewHandle(*Duplicate);
        NewHandle.SetName(*FString::Printf(TEXT("TrailLayer%d"), Index), *System);
        System->AddEmitterHandleDirect(NewHandle);
    }
}
}


namespace
{
void SetCurve(FNiagaraDistributionBase& D, int32 Channels, float Start, float Middle, float End, bool bAlphaOnly=false)
{
    D.Mode=ENiagaraDistributionMode::NonUniformCurve;
    D.ChannelCurves.SetNum(Channels);
    for(int32 C=0;C<Channels;++C)
    {
        auto& Curve=D.ChannelCurves[C]; Curve.Reset();
        const bool Flat=bAlphaOnly && C<3;
        Curve.AddKey(0.0f,Flat?1.0f:Start);
        Curve.AddKey(0.35f,Flat?1.0f:Middle);
        Curve.AddKey(1.0f,Flat?1.0f:End);
    }
    D.UpdateValuesFromDistribution();
}
FString ConfigureColaCinematic(UNiagaraSystem* System,bool bSpray,bool bAir)
{
    using namespace HeliosColaVFX;
    System->Modify();EnsureStatelessEmitterCount(System,3);
    if(System->GetEmitterHandles().Num()!=3)return TEXT("error:cola needs three lightweight emitters");
    int32 Index=0,Total=0;
    for(const auto& L:Layers)
    {
        if(L.bSpray!=bSpray)continue;
        auto& H=System->GetEmitterHandles()[Index++];auto* E=H.GetStatelessEmitter();
        if(!E)return TEXT("error:cola requires lightweight emitters");
        E->Modify();H.SetName(L.Name,*System);Total+=L.Count;
        System->bFixedBounds=true;System->SetFixedBounds(FBox(FVector(-Envelope()-128),FVector(Envelope()+128)));
        for(auto* R:E->GetRenderers())
        {
            R->Modify();R->SetIsEnabled(false);
            if(auto* S=Cast<UNiagaraSpriteRendererProperties>(R))
            {
                S->SetIsEnabled(true);
                S->Material=LoadObject<UMaterialInterface>(nullptr,*(FString(TEXT("/Game/Helios/Weapons/Cola/"))+MaterialNames[L.Material]));
                if(!S->Material)return TEXT("error:missing cinematic cola material");
                S->MaterialUserParamBinding.Parameter=FNiagaraVariable(FNiagaraTypeDefinition::GetUMaterialDef(),MaterialParameters[L.Material]);
                S->Alignment=L.bAligned?ENiagaraSpriteAlignment::VelocityAligned:ENiagaraSpriteAlignment::Unaligned;
                S->FacingMode=ENiagaraSpriteFacingMode::FaceCamera;
            }
            R->PostEditChange();
        }
        // Three short bursts introduce temporal breakup without a persistent emitter.
        while(E->GetNumSpawnInfos()<3)E->AddSpawnInfo();
        for(int32 S=0;S<E->GetNumSpawnInfos();++S)
        {
            auto* Spawn=E->GetSpawnInfoByIndex(S);Spawn->bEnabled=S<3;
            if(S>=3)continue;
            Spawn->Type=ENiagaraStatelessSpawnInfoType::Burst;Spawn->SpawnTime=L.Delay+S*.02f;
            Spawn->Amount=FNiagaraDistributionRangeInt(L.Count/3+(S<L.Count%3?1:0));
            Spawn->bLoopCountLimitEnabled=true;Spawn->LoopCountLimit=FNiagaraDistributionRangeInt(1);
        }
        for(UNiagaraStatelessModule* M:E->GetModules())
        {
            M->Modify();
            M->SetIsModuleEnabled(M->IsA<UNiagaraStatelessModule_InitializeParticle>()||M->IsA<UNiagaraStatelessModule_ShapeLocation>()||
                M->IsA<UNiagaraStatelessModule_AddVelocity>()||M->IsA<UNiagaraStatelessModule_AccelerationForce>()||
                M->IsA<UNiagaraStatelessModule_Drag>()||M->IsA<UNiagaraStatelessModule_SolveVelocitiesAndForces>()||
                M->IsA<UNiagaraStatelessModule_ScaleColor>()||M->IsA<UNiagaraStatelessModule_ScaleSpriteSize>());
            if(auto* Init=Cast<UNiagaraStatelessModule_InitializeParticle>(M))
            {
                Init->LifetimeDistribution=FNiagaraDistributionRangeFloat(L.LifeMin,L.LifeMax);
                Init->MassDistribution=FNiagaraDistributionRangeFloat(1.f);
                Init->ColorDistribution.InitConstant(L.Color);
                Init->SpriteSizeDistribution.Mode=ENiagaraDistributionMode::NonUniformRange;
                Init->SpriteSizeDistribution.Min=L.Size*.55f;Init->SpriteSizeDistribution.Max=L.Size;
                Init->SpriteSizeDistribution.ChannelConstantsAndRanges={L.Size.X*.55f,L.Size.Y*.55f,L.Size.X,L.Size.Y};
                Init->SpriteRotationDistribution=FNiagaraDistributionRangeFloat(0,360);
            }
            if(auto* V=Cast<UNiagaraStatelessModule_AddVelocity>(M))
            {
                V->VelocityType=ENSM_VelocityType::InCone;V->ConeRotationType=ENSM_ConeRotationType::Direction;
                V->ConeDirection.InitConstant(FVector3f::ZAxisVector);V->ConeAngle=bAir?360.f:L.SurfaceConeAngle;
                V->InnerCone=!bAir&&L.LifeMax<.8f&&L.Count>100?120.f:0.f;
                V->ConeVelocityDistribution=FNiagaraDistributionRangeFloat(L.SpeedMin,L.SpeedMax);
                V->ConeVelocityScale=FNiagaraDistributionRangeFloat(1);V->CoordinateSpace=ENiagaraCoordinateSpace::Local;
            }
            if(auto* S=Cast<UNiagaraStatelessModule_ShapeLocation>(M))
            {S->ShapePrimitive=ENSM_ShapePrimitive::Sphere;S->SphereRadius=FNiagaraDistributionRangeFloat(0,L.SpawnRadius);}
            if(auto* A=Cast<UNiagaraStatelessModule_AccelerationForce>(M))
            {A->AccelerationDistribution.InitConstant(FVector3f(0,0,-L.Gravity));A->CoordinateSpace=ENiagaraCoordinateSpace::World;}
            if(auto* D=Cast<UNiagaraStatelessModule_Drag>(M))D->DragDistribution=FNiagaraDistributionRangeFloat(L.Drag);
            if(auto* C=Cast<UNiagaraStatelessModule_ScaleColor>(M))SetCurve(C->ScaleDistribution,4,L.Material==2?0.f:1.f,.8f,0,true);
            if(auto* S=Cast<UNiagaraStatelessModule_ScaleSpriteSize>(M))SetCurve(S->ScaleDistribution,2,.35f,1.f,L.Material==2?1.f:.22f);
            M->PostEditChange();
        }
        E->PostEditChange();
    }
    for(int32 M=0;M<3;++M)System->GetExposedParameters().AddParameter(FNiagaraVariable(FNiagaraTypeDefinition::GetUMaterialDef(),MaterialParameters[M]));
    System->RequestCompile(false);System->MarkPackageDirty();
    return FString::Printf(TEXT("ok:cola cinematic; emitters=3; particles=%d; envelope_cm=%.3f; air=%d; drag+gravity; max_release=2s"),Total,Envelope(),bAir);
}

FString ConfigureExplosion(UNiagaraSystem* System, bool bRealism=false)
{
    System->Modify(); EnsureStatelessEmitterCount(System,5);
    if(System->GetEmitterHandles().Num()!=5) return TEXT("error:explosion needs lightweight source");
    const FHeliosStatelessPreset Layers[]={
        {0.18f,0.24f,FLinearColor(5,2.0f,0.5f,0.75f),FVector2f(680,680),1,0,0,180,0},
        {0.32f,0.40f,FLinearColor(2.5f,0.32f,0.025f,0.8f),FVector2f(680,680),7,350,550,180,50},
        {0.85f,1.18f,FLinearColor(0.14f,0.13f,0.12f,0.26f),FVector2f(260,260),9,80,160,80,110},
        {0.30f,0.58f,FLinearColor(6,1.8f,0.18f,0.85f),FVector2f(7,36),32,750,1800,180,30},
        {0.5f,0.5f,FLinearColor(1.0f,0.42f,0.12f,0.18f),FVector2f(1,1),1,0,0,180,0}
    };
    const TCHAR* Names[]={TEXT("WhiteHotCore"),TEXT("OrangeFireLobes"),TEXT("ShortGreySmoke"),TEXT("RadialSparks"),TEXT("RadiusShockwave")};
    for(int32 I=0;I<5;++I)
    {
        auto& H=System->GetEmitterHandles()[I]; auto* E=H.GetStatelessEmitter();
        if(!E) return TEXT("error:non-lightweight explosion emitter");
        if(!E->GetModule(UNiagaraStatelessModule_ScaleMeshSize::StaticClass()) || !E->GetModule(UNiagaraStatelessModule_ScaleColor::StaticClass())) return TEXT("error:required curve modules missing");
        const auto& V=Layers[I]; H.SetName(Names[I],*System); E->Modify();
        if(I==4)
        {
            bool HasMesh=false; for(auto* R:E->GetRenderers()) HasMesh|=R->IsA<UNiagaraMeshRendererProperties>();
            if(!HasMesh) E->AddRenderer(NewObject<UNiagaraMeshRendererProperties>(E),FGuid());
        }
        for(auto* R:E->GetRenderers())
        {
            R->Modify(); R->SetIsEnabled(false);
            if(auto* Sprite=Cast<UNiagaraSpriteRendererProperties>(R); Sprite && I!=4)
            {
                Sprite->SetIsEnabled(true);
                const TCHAR* MaterialPath = bRealism
                    ? (I==2 ? TEXT("/Game/Helios/Weapons/Realism/Effects/Materials/M_RFX_Smoke.M_RFX_Smoke") : (I==3 ? TEXT("/Game/Helios/Weapons/Realism/Effects/Materials/M_RFX_Sparks.M_RFX_Sparks") : TEXT("/Game/Helios/Weapons/Realism/Effects/Materials/M_RFX_Fire.M_RFX_Fire")))
                    : (I==2 ? TEXT("/Game/Helios/Weapons/Effects/M_ExplosionSmoke.M_ExplosionSmoke") : TEXT("/Game/Helios/Weapons/Effects/M_ExplosionFire.M_ExplosionFire"));
                Sprite->Material=LoadObject<UMaterialInterface>(nullptr, MaterialPath);
                Sprite->Alignment=I==3?ENiagaraSpriteAlignment::VelocityAligned:ENiagaraSpriteAlignment::Unaligned;
                Sprite->FacingMode=ENiagaraSpriteFacingMode::FaceCamera;
            }
            if(auto* Mesh=Cast<UNiagaraMeshRendererProperties>(R); Mesh && I==4)
            {
                Mesh->SetIsEnabled(true); Mesh->Meshes.SetNum(1);
                Mesh->Meshes[0].Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
                Mesh->bOverrideMaterials=true; Mesh->OverrideMaterials.SetNum(1);
                Mesh->OverrideMaterials[0].ExplicitMat=LoadObject<UMaterialInterface>(nullptr,bRealism
                    ? TEXT("/Game/Helios/Weapons/Realism/Effects/Materials/M_RFX_Shockwave.M_RFX_Shockwave")
                    : TEXT("/Game/Helios/Weapons/Effects/M_ExplosionWave.M_ExplosionWave"));
            }
            R->PostEditChange();
        }
        if(auto* Spawn=E->GetSpawnInfoByIndex(0))
        { Spawn->Type=ENiagaraStatelessSpawnInfoType::Burst; Spawn->SpawnTime=0; Spawn->Amount=FNiagaraDistributionRangeInt(V.SpawnCount); }
        for(UNiagaraStatelessModule* M:E->GetModules())
        {
            M->Modify();
            // Explicit whitelist removes template gravity, speed scaling and repeat noise.
            M->SetIsModuleEnabled(M->IsA<UNiagaraStatelessModule_InitializeParticle>() || M->IsA<UNiagaraStatelessModule_AddVelocity>() || M->IsA<UNiagaraStatelessModule_ShapeLocation>() || M->IsA<UNiagaraStatelessModule_ScaleColor>() || M->IsA<UNiagaraStatelessModule_ScaleSpriteSize>() || M->IsA<UNiagaraStatelessModule_ScaleMeshSize>());
            if(auto* Init=Cast<UNiagaraStatelessModule_InitializeParticle>(M))
            {
                Init->LifetimeDistribution=FNiagaraDistributionRangeFloat(V.LifetimeMin,V.LifetimeMax);
                Init->ColorDistribution.InitConstant(V.Color); Init->SpriteSizeDistribution.InitConstant(V.SpriteSize);
                Init->MeshScaleDistribution.InitConstant(FVector3f(40,40,40));
                Init->SpriteRotationDistribution=FNiagaraDistributionRangeFloat(0,360);
            }
            if(auto* Vel=Cast<UNiagaraStatelessModule_AddVelocity>(M))
            {
                Vel->VelocityType=ENSM_VelocityType::InCone; Vel->ConeVelocityDistribution=FNiagaraDistributionRangeFloat(V.SpeedMin,V.SpeedMax);
                Vel->ConeRotationType=ENSM_ConeRotationType::Direction; Vel->ConeDirection.InitConstant(FVector3f::XAxisVector);
                Vel->ConeAngle=V.ConeAngle; Vel->InnerCone=0; Vel->CoordinateSpace=ENiagaraCoordinateSpace::Local;
            }
            if(auto* Shape=Cast<UNiagaraStatelessModule_ShapeLocation>(M))
            {Shape->ShapePrimitive=ENSM_ShapePrimitive::Sphere; Shape->SphereRadius=FNiagaraDistributionRangeFloat(V.Radius);}
            if(auto* Color=Cast<UNiagaraStatelessModule_ScaleColor>(M)) SetCurve(Color->ScaleDistribution,4,I==2?0.0f:1.0f,0.65f,0,true);
            if(auto* Size=Cast<UNiagaraStatelessModule_ScaleSpriteSize>(M)) SetCurve(Size->ScaleDistribution,2,I==3?1.0f:0.3f,1.0f,I==2?1.8f:1.0f);
            if(auto* Size=Cast<UNiagaraStatelessModule_ScaleMeshSize>(M)) SetCurve(Size->ScaleDistribution,3,0.02f,0.5f,1.0f);
            M->PostEditChange();
        }
        E->PostEditChange();
    }
    System->GetExposedParameters().AddParameter(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.BlastRadius")));
    System->GetExposedParameters().AddParameter(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(),TEXT("User.ImpactNormal")));
    System->RequestCompile(false); System->MarkPackageDirty();
    return FString::Printf(TEXT("ok:%s 5 layers; core/fire/smoke/sparks/shockwave; radius 2000cm; maximum lifetime 1.18s"), bRealism ? TEXT("realism") : TEXT("legacy"));
}

FString ConfigureLaserEnemyHit(UNiagaraSystem* System,bool bShotgun=false)
{
    System->Modify(); EnsureStatelessEmitterCount(System, 4);
    if (System->GetEmitterHandles().Num() < 4) return TEXT("error:laser enemy hit needs lightweight source");
    const FHeliosStatelessPreset LaserLayers[] = {
        {.035f,.060f,FLinearColor(.65f,5.5f,12.0f,1.0f),FVector2f(20,20),1,0,0,0,0},
        {.09f,.22f,FLinearColor(.08f,1.6f,7.5f,.92f),FVector2f(1.2f,12),12,150,520,180,1.0f},
        {.12f,.28f,FLinearColor(.18f,2.4f,9.0f,.72f),FVector2f(14,16),8,20,80,180,2.0f},
        {.20f,.35f,FLinearColor(.05f,1.0f,4.5f,.42f),FVector2f(17,17),3,5,18,180,1.5f},
    };
    const FHeliosStatelessPreset ShotgunLayers[] = {
        {.035f,.065f,FLinearColor(3.f,1.6f,.4f,1),FVector2f(18,18),1,0,0,0,0},
        {.10f,.25f,FLinearColor(3.f,.65f,.04f,.9f),FVector2f(1.1f,16),14,170,650,75,.8f},
        {.14f,.30f,FLinearColor(2.f,.20f,.01f,.65f),FVector2f(2,5),6,50,180,80,1},
        {.12f,.30f,FLinearColor(2.f,.55f,.05f,.55f),FVector2f(28,28),1,0,0,0,0},
    };
    const auto* Layers=bShotgun?ShotgunLayers:LaserLayers;
    const TCHAR* Names[] = {TEXT("WhiteCyanCore"),TEXT("RadialIonSparks"),TEXT("ShortArcFragments"),TEXT("ExpandingHalo")};
    const TCHAR* Materials[] = {
        TEXT("/Game/Helios/Weapons/Realism/Impacts/M_LaserEnemyCore.M_LaserEnemyCore"),
        TEXT("/Game/Helios/Weapons/Realism/Impacts/M_LaserEnemySpark.M_LaserEnemySpark"),
        TEXT("/Game/Helios/Weapons/Realism/Impacts/M_LaserEnemyArc.M_LaserEnemyArc"),
        TEXT("/Game/Helios/Weapons/Realism/Impacts/M_LaserEnemyHalo.M_LaserEnemyHalo"),
    };
    const TCHAR* ShotgunMaterials[] = {
        TEXT("/Game/Helios/Weapons/Realism/Impacts/M_ShotgunFlash"),
        TEXT("/Game/Helios/Weapons/Realism/Impacts/M_ShotgunSparks"),
        TEXT("/Game/Helios/Weapons/Realism/Impacts/M_ShotgunSparks"),
        TEXT("/Game/Helios/Weapons/Realism/Impacts/M_ShotgunRing"),
    };
    const TCHAR* ShotgunNames[]={TEXT("HotImpact"),TEXT("FanSparks"),TEXT("CoolingEmbers"),TEXT("ImpactRing")};
    for (int32 I=0; I<4; ++I)
    {
        FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[I];
        UNiagaraStatelessEmitter* Emitter = Handle.GetStatelessEmitter();
        if (!Emitter) return TEXT("error:laser enemy hit requires stateless emitters");
        UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, bShotgun?ShotgunMaterials[I]:Materials[I]);
        if (!Material) return FString::Printf(TEXT("error:missing laser enemy hit material %d"), I);
        const FHeliosStatelessPreset& Value = Layers[I]; Handle.SetName(bShotgun?ShotgunNames[I]:Names[I], *System); Emitter->Modify();
        for (UNiagaraRendererProperties* Renderer : Emitter->GetRenderers())
        {
            Renderer->Modify(); Renderer->SetIsEnabled(false);
            if (UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer))
            {
                Sprite->SetIsEnabled(true); Sprite->Material = Material;
                Sprite->Alignment = I == 1 ? ENiagaraSpriteAlignment::VelocityAligned : ENiagaraSpriteAlignment::Unaligned;
                Sprite->FacingMode = ENiagaraSpriteFacingMode::FaceCamera;
            }
            Renderer->PostEditChange();
        }
        if (FNiagaraStatelessSpawnInfo* Spawn = Emitter->GetSpawnInfoByIndex(0))
        { Spawn->Type=ENiagaraStatelessSpawnInfoType::Burst; Spawn->SpawnTime=0; Spawn->Amount=FNiagaraDistributionRangeInt(Value.SpawnCount); }
        for (UNiagaraStatelessModule* Module : Emitter->GetModules())
        {
            if (!Module) continue;
            Module->Modify();
            Module->SetIsModuleEnabled(Module->IsA<UNiagaraStatelessModule_InitializeParticle>() || Module->IsA<UNiagaraStatelessModule_AddVelocity>() || Module->IsA<UNiagaraStatelessModule_ShapeLocation>() || Module->IsA<UNiagaraStatelessModule_ScaleColor>() || Module->IsA<UNiagaraStatelessModule_ScaleSpriteSize>());
            if (UNiagaraStatelessModule_InitializeParticle* Init=Cast<UNiagaraStatelessModule_InitializeParticle>(Module))
            { Init->LifetimeDistribution=FNiagaraDistributionRangeFloat(Value.LifetimeMin,Value.LifetimeMax); Init->ColorDistribution.InitConstant(Value.Color); Init->SpriteSizeDistribution.InitConstant(Value.SpriteSize); Init->SpriteRotationDistribution=FNiagaraDistributionRangeFloat(0,360); }
            if (UNiagaraStatelessModule_AddVelocity* Velocity=Cast<UNiagaraStatelessModule_AddVelocity>(Module))
            { Velocity->VelocityType=ENSM_VelocityType::InCone; Velocity->ConeVelocityDistribution=FNiagaraDistributionRangeFloat(Value.SpeedMin,Value.SpeedMax); Velocity->ConeRotationType=ENSM_ConeRotationType::Direction; Velocity->ConeDirection.InitConstant(FVector3f::XAxisVector); Velocity->ConeAngle=Value.ConeAngle; Velocity->InnerCone=0; Velocity->CoordinateSpace=ENiagaraCoordinateSpace::Local; }
            if (UNiagaraStatelessModule_ShapeLocation* Shape=Cast<UNiagaraStatelessModule_ShapeLocation>(Module))
            { Shape->ShapePrimitive=ENSM_ShapePrimitive::Sphere; Shape->SphereRadius=FNiagaraDistributionRangeFloat(Value.Radius); }
            if (UNiagaraStatelessModule_ScaleColor* Color=Cast<UNiagaraStatelessModule_ScaleColor>(Module)) SetCurve(Color->ScaleDistribution,4,1.0f,I==0?.8f:.65f,0.0f,true);
            if (UNiagaraStatelessModule_ScaleSpriteSize* Size=Cast<UNiagaraStatelessModule_ScaleSpriteSize>(Module)) SetCurve(Size->ScaleDistribution,2,I==0?.45f:(I==3?.35f:.65f),I==3?1.45f:1.0f,I==3?2.0f:.25f);
            Module->PostEditChange();
        }
        Emitter->PostEditChange();
    }
    System->RequestCompile(false); System->MarkPackageDirty();
    if(bShotgun) return TEXT("ok:shotgun flare 4 layers; flash/sparks/embers/ring; 22 particles; maximum lifetime 0.30s");
    return TEXT("ok:laser enemy hit 4 layers; core/sparks/arcs/halo; 24 particles; maximum lifetime 0.35s");
}

FString ConfigureWhiteSuit(UNiagaraSystem* System,FName Preset)
{
    System->Modify(); EnsureStatelessEmitterCount(System, 4);
    if (System->GetEmitterHandles().Num() < 4) return TEXT("error:laser enemy hit needs lightweight source");
    const bool Windup=Preset==TEXT("BossWindup"), Trail=Preset==TEXT("BossTrail");
    const FHeliosStatelessPreset ImpactLayers[]={
        {.065f,.16f,FLinearColor(7,5.5f,3.f,1),FVector2f(160,160),1,0,0,0,0},
        {.18f,.48f,FLinearColor(6,4.1f,1.5f,.95f),FVector2f(4.5f,70),36,220,880,120,8},
        {.22f,.58f,FLinearColor(.42f,.35f,.23f,.32f),FVector2f(34,44),12,80,220,145,8},
        {.18f,.42f,FLinearColor(5,3.8f,1.7f,.8f),FVector2f(220,220),1,0,0,0,0}};
    const FHeliosStatelessPreset WindupLayers[]={
        {.3f,.65f,FLinearColor(1.4f,1.1f,.58f,.3f),FVector2f(48,48),1,0,0,0,0},
        {.25f,.55f,FLinearColor(2.2f,1.7f,.65f,.65f),FVector2f(1,18),18,12,65,165,35},
        {.2f,.5f,FLinearColor(.36f,.31f,.23f,.16f),FVector2f(18,25),6,15,50,160,30},
        {.35f,.7f,FLinearColor(1.4f,1,.45f,.4f),FVector2f(90,90),1,0,0,0,0}};
    const FHeliosStatelessPreset TrailLayers[]={
        {.10f,.20f,FLinearColor(5.4f,4.1f,2.6f,.5f),FVector2f(60,60),1,0,0,0,0},
        {.16f,.32f,FLinearColor(5.7f,4.2f,2.2f,.85f),FVector2f(4.0f,130),20,140,480,28,30},
        {.15f,.30f,FLinearColor(.35f,.29f,.20f,.18f),FVector2f(22,35),3,25,70,45,15},
        {.12f,.26f,FLinearColor(3.8f,2.8f,1.4f,.45f),FVector2f(90,90),1,0,0,0,0}};
    const auto* Layers=Windup?WindupLayers:(Trail?TrailLayers:ImpactLayers);
    const TCHAR* Names[]={TEXT("WhiteGoldCore"),TEXT("DirectionalSilkSparks"),TEXT("GroundDust"),TEXT("ImpactHalo")};
    const TCHAR* Materials[]={TEXT("M_WS_Core"),TEXT("M_WS_Streak"),TEXT("M_WS_Dust"),TEXT("M_WS_Ring")};
    for (int32 I=0; I<4; ++I)
    {
        FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[I];
        UNiagaraStatelessEmitter* Emitter = Handle.GetStatelessEmitter();
        if (!Emitter) return TEXT("error:laser enemy hit requires stateless emitters");
        UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *(FString(TEXT("/Game/Helios/Enemies/E05WhiteSuit/Effects/"))+Materials[I]));
        if (!Material) return FString::Printf(TEXT("error:missing laser enemy hit material %d"), I);
        const FHeliosStatelessPreset& Value = Layers[I]; Handle.SetName(Names[I], *System); Emitter->Modify();
        for (UNiagaraRendererProperties* Renderer : Emitter->GetRenderers())
        {
            Renderer->Modify(); Renderer->SetIsEnabled(false);
            if (UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer))
            {
                Sprite->SetIsEnabled(true); Sprite->Material = Material;
                Sprite->Alignment = I == 1 ? ENiagaraSpriteAlignment::VelocityAligned : ENiagaraSpriteAlignment::Unaligned;
                Sprite->FacingMode = ENiagaraSpriteFacingMode::FaceCamera;
            }
            Renderer->PostEditChange();
        }
        if (FNiagaraStatelessSpawnInfo* Spawn = Emitter->GetSpawnInfoByIndex(0))
        { Spawn->Type=ENiagaraStatelessSpawnInfoType::Burst; Spawn->SpawnTime=0; Spawn->Amount=FNiagaraDistributionRangeInt(Value.SpawnCount); }
        for (UNiagaraStatelessModule* Module : Emitter->GetModules())
        {
            if (!Module) continue;
            Module->Modify();
            Module->SetIsModuleEnabled(Module->IsA<UNiagaraStatelessModule_InitializeParticle>() || Module->IsA<UNiagaraStatelessModule_AddVelocity>() || Module->IsA<UNiagaraStatelessModule_ShapeLocation>() || Module->IsA<UNiagaraStatelessModule_ScaleColor>() || Module->IsA<UNiagaraStatelessModule_ScaleSpriteSize>());
            if (UNiagaraStatelessModule_InitializeParticle* Init=Cast<UNiagaraStatelessModule_InitializeParticle>(Module))
            { Init->LifetimeDistribution=FNiagaraDistributionRangeFloat(Value.LifetimeMin,Value.LifetimeMax); Init->ColorDistribution.InitConstant(Value.Color); Init->SpriteSizeDistribution.InitConstant(Value.SpriteSize); Init->SpriteRotationDistribution=FNiagaraDistributionRangeFloat(0,360); }
            if (UNiagaraStatelessModule_AddVelocity* Velocity=Cast<UNiagaraStatelessModule_AddVelocity>(Module))
            { Velocity->VelocityType=ENSM_VelocityType::InCone; Velocity->ConeVelocityDistribution=FNiagaraDistributionRangeFloat(Value.SpeedMin,Value.SpeedMax); Velocity->ConeRotationType=ENSM_ConeRotationType::Direction; Velocity->ConeDirection.InitConstant(FVector3f::XAxisVector); Velocity->ConeAngle=Value.ConeAngle; Velocity->InnerCone=0; Velocity->CoordinateSpace=ENiagaraCoordinateSpace::Local; }
            if (UNiagaraStatelessModule_ShapeLocation* Shape=Cast<UNiagaraStatelessModule_ShapeLocation>(Module))
            { Shape->ShapePrimitive=ENSM_ShapePrimitive::Sphere; Shape->SphereRadius=FNiagaraDistributionRangeFloat(Value.Radius); }
            if (UNiagaraStatelessModule_ScaleColor* Color=Cast<UNiagaraStatelessModule_ScaleColor>(Module)) SetCurve(Color->ScaleDistribution,4,1.0f,I==0?.8f:.65f,0.0f,true);
            if (UNiagaraStatelessModule_ScaleSpriteSize* Size=Cast<UNiagaraStatelessModule_ScaleSpriteSize>(Module)) SetCurve(Size->ScaleDistribution,2,I==0?.45f:(I==3?.35f:.65f),I==3?1.45f:1.0f,I==3?2.0f:.25f);
            Module->PostEditChange();
        }
        Emitter->PostEditChange();
    }
    System->RequestCompile(false); System->MarkPackageDirty();
    return TEXT("ok:white suit layered Niagara effect");
}

FName ResolveRealismBasePreset(FName Preset)
{
    if (Preset.ToString().StartsWith(TEXT("Impact")) || Preset==TEXT("ShotgunTracer")) return Preset;
    if (Preset == TEXT("RealismRifleMuzzle")) return TEXT("RifleMuzzle");
    if (Preset == TEXT("RealismShotgunMuzzle")) return TEXT("ShotgunMuzzle");
    if (Preset == TEXT("RealismShotgunImpact")) return TEXT("ShotgunImpact");
    if (Preset == TEXT("RealismRocketMuzzle")) return TEXT("RocketMuzzle");
    if (Preset == TEXT("RealismRocketFlight")) return TEXT("RocketTrail");
    if (Preset == TEXT("RealismLaserBeam")) return TEXT("LaserBeam");
    if (Preset == TEXT("RealismLaserHit")) return TEXT("LaserHit");
    return NAME_None;
}

const TCHAR* GetRealismMaterial(FName BasePreset, int32 EmitterIndex)
{
    if (BasePreset==TEXT("ShotgunTracer") || BasePreset==TEXT("ImpactMetal") || BasePreset==TEXT("ImpactArmor"))
        return TEXT("/Game/Helios/Weapons/Realism/Effects/Materials/M_RFX_Sparks.M_RFX_Sparks");
    if (BasePreset.ToString().StartsWith(TEXT("Impact")))
        return TEXT("/Game/Helios/Weapons/Realism/Effects/Materials/M_RFX_Smoke.M_RFX_Smoke");
    if (BasePreset == TEXT("LaserBeam")) return EmitterIndex == 0
        ? TEXT("/Game/Helios/Weapons/Realism/Effects/Materials/M_RFX_LaserCore.M_RFX_LaserCore")
        : TEXT("/Game/Helios/Weapons/Realism/Effects/Materials/M_RFX_LaserHalo.M_RFX_LaserHalo");
    if (BasePreset == TEXT("LaserHit")) return EmitterIndex == 0
        ? TEXT("/Game/Helios/Weapons/Realism/Effects/Materials/M_RFX_Sparks.M_RFX_Sparks")
        : TEXT("/Game/Helios/Weapons/Realism/Effects/Materials/M_RFX_LaserHalo.M_RFX_LaserHalo");
    if (BasePreset == TEXT("ShotgunImpact")) return EmitterIndex == 0
        ? TEXT("/Game/Helios/Weapons/Realism/Effects/Materials/M_RFX_Smoke.M_RFX_Smoke")
        : TEXT("/Game/Helios/Weapons/Realism/Effects/Materials/M_RFX_Sparks.M_RFX_Sparks");
    if (BasePreset == TEXT("RocketTrail")) return EmitterIndex == 0
        ? TEXT("/Game/Helios/Weapons/Realism/Effects/Materials/M_RFX_Fire.M_RFX_Fire")
        : TEXT("/Game/Helios/Weapons/Realism/Effects/Materials/M_RFX_Smoke.M_RFX_Smoke");
    return EmitterIndex == 0
        ? TEXT("/Game/Helios/Weapons/Realism/Effects/Materials/M_RFX_Fire.M_RFX_Fire")
        : TEXT("/Game/Helios/Weapons/Realism/Effects/Materials/M_RFX_Smoke.M_RFX_Smoke");
}

FString ApplyRealismMaterials(UNiagaraSystem* System, FName BasePreset)
{
    int32 Edited = 0;
    for (int32 Index = 0; Index < System->GetEmitterHandles().Num(); ++Index)
    {
        UNiagaraStatelessEmitter* Emitter = System->GetEmitterHandles()[Index].GetStatelessEmitter();
        if (!Emitter) continue;
        UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, GetRealismMaterial(BasePreset, Index));
        if (!Material) return FString::Printf(TEXT("error:missing realism material for %s layer %d"), *BasePreset.ToString(), Index);
        for (UNiagaraRendererProperties* Renderer : Emitter->GetRenderers())
        {
            if (UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer))
            {
                Sprite->Modify(); Sprite->Material = Material; Sprite->PostEditChange(); ++Edited;
            }
        }
        for (UNiagaraStatelessModule* Module : Emitter->GetModules())
        {
            if (!Module) continue;
            Module->Modify();
            // Curves are only applied through the isolated realism API, preserving
            // legacy preset playback while giving every new layer a clean fade.
            if (UNiagaraStatelessModule_ScaleColor* Color = Cast<UNiagaraStatelessModule_ScaleColor>(Module))
            {
                SetCurve(Color->ScaleDistribution, 4, 1.0f, 0.78f, 0.0f, true);
            }
            if (UNiagaraStatelessModule_ScaleSpriteSize* Size = Cast<UNiagaraStatelessModule_ScaleSpriteSize>(Module))
            {
                const bool bBeam = BasePreset == TEXT("LaserBeam");
                SetCurve(Size->ScaleDistribution, 2, bBeam ? 1.0f : 0.42f, 1.0f,
                    bBeam ? 1.0f : (Index == 0 ? 0.25f : 1.65f));
            }
            Module->PostEditChange();
        }
    }
    System->RequestCompile(false); System->MarkPackageDirty();
    return FString::Printf(TEXT("ok:realism materials applied to %d sprite renderer(s)"), Edited);
}
}

FString UHeliosWeaponVFXTools::ConfigureWeaponEffect(UNiagaraSystem* System, FName Preset)
{
    if (!System)
    {
        return TEXT("error:null system");
    }

    if(Preset==TEXT("RocketExplosion")) return ConfigureExplosion(System);
    if(Preset==TEXT("ColaFoam")||Preset==TEXT("ColaFoamAir"))return ConfigureColaCinematic(System,false,Preset==TEXT("ColaFoamAir"));
    if(Preset==TEXT("ColaSpray")||Preset==TEXT("ColaSprayAir"))return ConfigureColaCinematic(System,true,Preset==TEXT("ColaSprayAir"));

    FHeliosStatelessPreset Values{};
    if (!ResolvePreset(Preset, Values))
    {
        return FString::Printf(TEXT("error:unknown preset %s"), *Preset.ToString());
    }

    System->Modify();
    if(Preset.ToString().StartsWith(TEXT("SBDark")))EnsureStatelessEmitterCount(System,3);
    if(Preset.ToString().StartsWith(TEXT("SX")))EnsureStatelessEmitterCount(System,Preset==TEXT("SXImpactFar")?1:3);
    if (Preset == TEXT("RocketTrail"))
    {
        EnsureStatelessEmitterCount(System, 2);
    }
    if (Preset == TEXT("RocketMuzzle") || Preset == TEXT("RifleMuzzle") || Preset == TEXT("ShotgunMuzzle") ||
        Preset == TEXT("ShotgunImpact") || Preset == TEXT("LaserBeam") || Preset == TEXT("LaserHit"))
    {
        // Layer zero is the short emissive event; layer one becomes the restrained
        // smoke/halo/impact residue.  It is still an editable Niagara emitter.
        EnsureStatelessEmitterCount(System, 2);
    }
    if (Preset == TEXT("ShotgunTracer") || Preset == TEXT("LaserBeam"))
    {
        // Public component contract. UHeliosWeaponVFX consumes these values and
        // applies the Start->End transform to this lightweight local-X streak.
        System->GetExposedParameters().AddParameter(
            FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("User.Start")));
        System->GetExposedParameters().AddParameter(
            FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("User.End")));
    }
    int32 EditedEmitters = 0;
    int32 EmitterIndex = 0;
    TArray<FString> RendererReport;
    TArray<FString> ModuleReport;
    for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
    {
        UNiagaraStatelessEmitter* Emitter = Handle.GetStatelessEmitter();
        if (!Emitter)
        {
            continue;
        }

        Emitter->Modify();
        FHeliosStatelessPreset LayerValues = Values;
        const bool ImpactAtlas=Preset==TEXT("CIFireBloom")||Preset==TEXT("CISmoke")||Preset==TEXT("CIMist");
        if(ImpactAtlas&&!Emitter->GetModule(UNiagaraStatelessModule_SubUVAnimation::StaticClass())){
            auto* Property=FindFProperty<FArrayProperty>(Emitter->GetClass(),TEXT("Modules"));auto* Modules=Property->ContainerPtrToValuePtr<TArray<TObjectPtr<UNiagaraStatelessModule>>>(Emitter);Modules->Add(NewObject<UNiagaraStatelessModule_SubUVAnimation>(Emitter));
        }

        if(Preset.ToString().StartsWith(TEXT("SBDark")))
        {
            if(EmitterIndex==1)
            {
                const bool Birth=Preset==TEXT("SBDarkBirth");
                LayerValues={.3f,Birth?1.4f:.65f,FLinearColor(.035f,.045f,.038f,.30f),FVector2f(Birth?65:32,Birth?80:40),Birth?10:5,8,Birth?65.f:42.f,90,Birth?25.f:4.f};
                Handle.SetName(TEXT("SoftSoulSmoke"),*System);
            }
            else if(EmitterIndex==2)
            {
                LayerValues={.18f,.65f,FLinearColor(.27f,.23f,.15f,.45f),FVector2f(1.2f,2),8,20,100,110,8};
                Handle.SetName(TEXT("DustAndAsh"),*System);
            }
            else Handle.SetName(TEXT("FineSoulFilaments"),*System);
        }
        if(Preset.ToString().StartsWith(TEXT("SX")))
        {
            const bool Birth=Preset==TEXT("SXBirth");
            if(EmitterIndex==1){LayerValues={.12f,Birth?1.2f:.48f,FLinearColor(.025f,.50f,.34f,.36f),FVector2f(Birth?70:43,Birth?100:55),Birth?7:3,20,Birth?110.f:80.f,80,Birth?35.f:5.f};Handle.SetName(TEXT("JadeSoulFlames"),*System);}
            else if(EmitterIndex==2){LayerValues={.2f,Birth?.95f:.55f,FLinearColor(1.4f,.83f,.27f,.7f),FVector2f(8,13),Birth?12:6,45,155,130,8};Handle.SetName(TEXT("GoldenSealFragments"),*System);}
            else Handle.SetName(TEXT("BrightCuttingFilaments"),*System);
        }
        if (Preset == TEXT("RocketTrail") && EmitterIndex == 0)
        {
            LayerValues = {0.045f, 0.075f, FLinearColor(8.0f, 0.65f, 0.025f, 1.0f), FVector2f(13.0f, 9.0f), 2, 5.0f, 18.0f, 18.0f, 2.0f};
            Handle.SetName(TEXT("HotCore"), *System);
        }
        else if (Preset == TEXT("RocketTrail"))
        {
            LayerValues = {0.42f, 0.58f, FLinearColor(0.20f, 0.18f, 0.16f, 0.62f), FVector2f(30.0f, 30.0f), 3, 20.0f, 65.0f, 35.0f, 5.0f};
            Handle.SetName(TEXT("ShortSmoke"), *System);
        }
        else if (Preset == TEXT("RocketMuzzle") && EmitterIndex == 1)
        {
            LayerValues = {0.14f, 0.28f, FLinearColor(0.18f, 0.13f, 0.10f, 0.28f), FVector2f(18.0f, 14.0f), 5, 45.0f, 130.0f, 20.0f, 3.0f};
            Handle.SetName(TEXT("LaunchSmoke"), *System);
        }
        else if (Preset == TEXT("RifleMuzzle") && EmitterIndex == 1)
        {
            LayerValues = {0.18f, 0.32f, FLinearColor(0.16f, 0.14f, 0.12f, 0.24f), FVector2f(15.0f, 12.0f), 4, 35.0f, 95.0f, 18.0f, 2.5f};
            Handle.SetName(TEXT("BriefMuzzleSmoke"), *System);
        }
        else if (Preset == TEXT("ShotgunMuzzle") && EmitterIndex == 1)
        {
            LayerValues = {0.20f, 0.38f, FLinearColor(0.18f, 0.16f, 0.13f, 0.30f), FVector2f(25.0f, 18.0f), 6, 40.0f, 115.0f, 28.0f, 4.0f};
            Handle.SetName(TEXT("WideMuzzleSmoke"), *System);
        }
        else if (Preset == TEXT("ShotgunImpact") && EmitterIndex == 1)
        {
            LayerValues = {0.05f, 0.10f, FLinearColor(2.5f, 0.75f, 0.10f, 0.75f), FVector2f(3.0f, 12.0f), 5, 180.0f, 520.0f, 70.0f, 1.0f};
            Handle.SetName(TEXT("ImpactFlecks"), *System);
        }
        else if (Preset == TEXT("LaserBeam") && EmitterIndex == 1)
        {
            LayerValues = {0.055f, 0.075f, FLinearColor(0.02f, 0.35f, 2.8f, 0.32f), FVector2f(7.0f, 100.0f), 1, 1.0f, 1.0f, 0.0f, 0.0f};
            Handle.SetName(TEXT("SoftBeamHalo"), *System);
        }
        else if (Preset == TEXT("LaserHit") && EmitterIndex == 1)
        {
            LayerValues = {0.12f, 0.22f, FLinearColor(0.04f, 0.45f, 1.8f, 0.28f), FVector2f(15.0f, 15.0f), 3, 8.0f, 35.0f, 95.0f, 1.0f};
            Handle.SetName(TEXT("SoftHitHalo"), *System);
        }
        else if (EmitterIndex == 0 && (Preset == TEXT("RocketMuzzle") || Preset == TEXT("RifleMuzzle") || Preset == TEXT("ShotgunMuzzle") || Preset == TEXT("ShotgunImpact") || Preset == TEXT("LaserBeam") || Preset == TEXT("LaserHit")))
        {
            Handle.SetName(Preset == TEXT("LaserBeam") ? TEXT("BeamCore") : (Preset == TEXT("LaserHit") ? TEXT("HitCore") : TEXT("PrimaryBurst")), *System);
        }
        for (UNiagaraRendererProperties* Renderer : Emitter->GetRenderers())
        {
            if (UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer))
            {
                Sprite->Modify();
                Sprite->SetIsEnabled(true);
                if(Preset.ToString().StartsWith(TEXT("CS")))Sprite->Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Helios/Weapons/Calamity/M_CS_Particle"));
                if(Preset.ToString().StartsWith(TEXT("CW"))){Sprite->Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Helios/Weapons/Calamity/Wind/M_CW_Particle"));Sprite->FacingMode=ENiagaraSpriteFacingMode::FaceCamera;Sprite->Alignment=ENiagaraSpriteAlignment::VelocityAligned;}
                if(Preset.ToString().StartsWith(TEXT("CI"))){
                    const TCHAR* Mat=Preset==TEXT("CIFireBloom")?TEXT("M_CI_FireSprite"):Preset==TEXT("CISmoke")?TEXT("M_CI_SmokeSprite"):Preset==TEXT("CIMist")?TEXT("M_CI_MistSprite"):Preset==TEXT("CIBubbles")?TEXT("M_CI_BubbleSprite"):TEXT("M_CI_SparkSprite");
                    Sprite->Material=LoadObject<UMaterialInterface>(nullptr,*(FString(TEXT("/Game/Helios/Weapons/Calamity/Impact/"))+Mat));Sprite->FacingMode=ENiagaraSpriteFacingMode::FaceCamera;Sprite->Alignment=ENiagaraSpriteAlignment::Unaligned;
                    if(ImpactAtlas){Sprite->SubImageSize=FVector2D(8,4);Sprite->bSubImageBlend=true;}
                }

                if(Preset.ToString().StartsWith(TEXT("SBDark")))
                {
                    Sprite->Material=LoadObject<UMaterialInterface>(nullptr,EmitterIndex==1?TEXT("/Game/Helios/Weapons/SoulBanner/M_SB_Smoke"):TEXT("/Game/Helios/Weapons/SoulBanner/M_SB_Particle"));
                    Sprite->FacingMode=ENiagaraSpriteFacingMode::FaceCamera;
                    Sprite->Alignment=EmitterIndex==0?ENiagaraSpriteAlignment::VelocityAligned:ENiagaraSpriteAlignment::Unaligned;
                }
                if(Preset.ToString().StartsWith(TEXT("SX")))
                {
                    const TCHAR* Mat=EmitterIndex==1?TEXT("M_SX_Smoke"):EmitterIndex==2?TEXT("M_SX_SealParticle"):TEXT("M_SX_Particle");
                    Sprite->Material=LoadObject<UMaterialInterface>(nullptr,*(FString(TEXT("/Game/Helios/Weapons/SoulBanner/"))+Mat));
                    Sprite->FacingMode=ENiagaraSpriteFacingMode::FaceCamera;Sprite->Alignment=EmitterIndex==0?ENiagaraSpriteAlignment::VelocityAligned:ENiagaraSpriteAlignment::Unaligned;
                }
                if (Preset == TEXT("ShotgunTracer") || Preset == TEXT("LaserBeam"))
                {
                    Sprite->Alignment = ENiagaraSpriteAlignment::VelocityAligned;
                    Sprite->FacingMode = ENiagaraSpriteFacingMode::FaceCamera;
                }
                if(Preset.ToString().StartsWith(TEXT("Cola")))
                {
                    Sprite->Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Helios/Weapons/Cola/M_ColaParticle"));
                    Sprite->FacingMode=ENiagaraSpriteFacingMode::FaceCamera;
                    Sprite->Alignment=Preset==TEXT("ColaSpray") ? ENiagaraSpriteAlignment::VelocityAligned : ENiagaraSpriteAlignment::Unaligned;
                }
                if(Preset.ToString().StartsWith(TEXT("QX")))
                {
                    Sprite->Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Helios/Weapons/Qingxiao/M_QX_Particle"));
                    Sprite->FacingMode=ENiagaraSpriteFacingMode::FaceCamera;
                    Sprite->Alignment=ENiagaraSpriteAlignment::VelocityAligned;
                }
                const FString MaterialPath = Sprite->Material ? Sprite->Material->GetPathName() : TEXT("None");
                RendererReport.Add(FString::Printf(TEXT("%s:Sprite:%s"), *Handle.GetName().ToString(), *MaterialPath));
                Sprite->PostEditChange();
            }
            else if (Renderer)
            {
                Renderer->Modify();
                Renderer->SetIsEnabled(false);
                RendererReport.Add(FString::Printf(TEXT("%s:%s:disabled"), *Handle.GetName().ToString(), *Renderer->GetClass()->GetName()));
                Renderer->PostEditChange();
            }
        }
        if (FNiagaraStatelessSpawnInfo* Spawn = Emitter->GetSpawnInfoByIndex(0))
        {
            Spawn->Type = ENiagaraStatelessSpawnInfoType::Burst;
            Spawn->SpawnTime = 0.0f;
            Spawn->Amount = FNiagaraDistributionRangeInt(LayerValues.SpawnCount);
            if(Preset==TEXT("CWMotes")){
                Spawn->Type=ENiagaraStatelessSpawnInfoType::Rate;Spawn->Rate=FNiagaraDistributionRangeFloat(LayerValues.SpawnCount);
                auto* StateProperty=FindFProperty<FStructProperty>(Emitter->GetClass(),TEXT("EmitterState"));
                auto* EmitterState=StateProperty->ContainerPtrToValuePtr<FNiagaraEmitterStateData>(Emitter);EmitterState->LoopBehavior=ENiagaraLoopBehavior::Infinite;EmitterState->LoopDuration=FNiagaraDistributionRangeFloat(1);
                auto* BoundsProperty=FindFProperty<FStructProperty>(Emitter->GetClass(),TEXT("FixedBounds"));*BoundsProperty->ContainerPtrToValuePtr<FBox>(Emitter)=FBox(FVector(-400,-400,-200),FVector(400,400,700));
            }
            if(Preset==TEXT("CSFireStream")||Preset==TEXT("CSPoisonStream")){
                Spawn->Type=ENiagaraStatelessSpawnInfoType::Rate;Spawn->Rate=FNiagaraDistributionRangeFloat(LayerValues.SpawnCount);
                auto* StateProperty=FindFProperty<FStructProperty>(Emitter->GetClass(),TEXT("EmitterState"));
                auto* EmitterState=StateProperty->ContainerPtrToValuePtr<FNiagaraEmitterStateData>(Emitter);EmitterState->LoopBehavior=ENiagaraLoopBehavior::Infinite;EmitterState->LoopDuration=FNiagaraDistributionRangeFloat(1);
                auto* BoundsProperty=FindFProperty<FStructProperty>(Emitter->GetClass(),TEXT("FixedBounds"));*BoundsProperty->ContainerPtrToValuePtr<FBox>(Emitter)=FBox(FVector(-650,-180,-180),FVector(100,180,180));
            }

        }

        for (UNiagaraStatelessModule* Module : Emitter->GetModules())
        {
            if (!Module)
            {
                continue;
            }
            Module->Modify();
            if(Preset.ToString().StartsWith(TEXT("CW")) || Preset.ToString().StartsWith(TEXT("CI")) || Preset.ToString().StartsWith(TEXT("CS")) || Preset.ToString().StartsWith(TEXT("Cola")) || Preset.ToString().StartsWith(TEXT("QX")) || Preset.ToString().StartsWith(TEXT("SBDark")) || Preset.ToString().StartsWith(TEXT("SX")))
            {
                Module->SetIsModuleEnabled(Module->IsA<UNiagaraStatelessModule_InitializeParticle>() ||
                    Module->IsA<UNiagaraStatelessModule_AddVelocity>() || Module->IsA<UNiagaraStatelessModule_ShapeLocation>() ||
                    Module->IsA<UNiagaraStatelessModule_ScaleColor>() || Module->IsA<UNiagaraStatelessModule_ScaleSpriteSize>() || (ImpactAtlas&&Module->IsA<UNiagaraStatelessModule_SubUVAnimation>()));
                if(auto* Color=Cast<UNiagaraStatelessModule_ScaleColor>(Module))SetCurve(Color->ScaleDistribution,4,1,.8f,0,true);
                if(auto* Size=Cast<UNiagaraStatelessModule_ScaleSpriteSize>(Module))SetCurve(Size->ScaleDistribution,2,.45f,1,.2f);
            }
            if(ImpactAtlas)if(auto* UV=Cast<UNiagaraStatelessModule_SubUVAnimation>(Module)){UV->NumFrames=32;UV->AnimationMode=ENSMSubUVAnimation_Mode::Linear;}
            if ((Preset == TEXT("ShotgunTracer") || Preset == TEXT("LaserBeam")) &&
                (Module->IsA<UNiagaraStatelessModule_AccelerationForce>() ||
                 Module->IsA<UNiagaraStatelessModule_CurlNoiseForce>() ||
                 Module->IsA<UNiagaraStatelessModule_Drag>() ||
                 Module->IsA<UNiagaraStatelessModule_GravityForce>()))
            {
                Module->SetIsModuleEnabled(false);
            }
            if (UNiagaraStatelessModule_InitializeParticle* Init = Cast<UNiagaraStatelessModule_InitializeParticle>(Module))
            {
                Init->LifetimeDistribution = FNiagaraDistributionRangeFloat(LayerValues.LifetimeMin, LayerValues.LifetimeMax);
                Init->ColorDistribution.InitConstant(LayerValues.Color);
                Init->SpriteSizeDistribution.InitConstant(LayerValues.SpriteSize);
                if(Preset.ToString().StartsWith(TEXT("CI")))Init->SpriteRotationDistribution=FNiagaraDistributionRangeFloat(-18,18);
            }
            else if (UNiagaraStatelessModule_AddVelocity* Velocity = Cast<UNiagaraStatelessModule_AddVelocity>(Module))
            {
                Velocity->VelocityType = ENSM_VelocityType::InCone;
                Velocity->ConeVelocityDistribution = FNiagaraDistributionRangeFloat(LayerValues.SpeedMin, LayerValues.SpeedMax);
                Velocity->ConeRotationType = ENSM_ConeRotationType::Direction;
                Velocity->ConeDirection.InitConstant(FVector3f::XAxisVector);
                if(Preset.ToString().StartsWith(TEXT("CI")))Velocity->ConeDirection.InitConstant(FVector3f::ZAxisVector);
                if(Preset.ToString().StartsWith(TEXT("CW")))Velocity->ConeDirection.InitConstant(FVector3f::ZAxisVector);
                if(Preset==TEXT("CSFireStream")||Preset==TEXT("CSPoisonStream"))Velocity->ConeDirection.InitConstant(-FVector3f::XAxisVector);
                Velocity->ConeAngle = LayerValues.ConeAngle;
                Velocity->InnerCone = 0.0f;
                Velocity->CoordinateSpace = ENiagaraCoordinateSpace::Local;
            }
            else if (UNiagaraStatelessModule_ShapeLocation* Shape = Cast<UNiagaraStatelessModule_ShapeLocation>(Module))
            {
                Shape->ShapePrimitive = ENSM_ShapePrimitive::Sphere;
                Shape->SphereRadius = FNiagaraDistributionRangeFloat(LayerValues.Radius);
                if(Preset==TEXT("CWMotes"))Shape->ShapeScale.InitConstant(FVector3f(1,1,.3f));
                if(Preset==TEXT("CIMist")||Preset==TEXT("CIBubbles"))Shape->ShapeScale.InitConstant(FVector3f(1,1,.025f));
            }
            Module->PostEditChange();
            ModuleReport.Add(FString::Printf(TEXT("%s:%s:%s"), *Handle.GetName().ToString(),
                *Module->GetClass()->GetName(), Module->IsModuleEnabled() ? TEXT("enabled") : TEXT("disabled")));
        }
        Emitter->PostEditChange();
        ++EditedEmitters;
        ++EmitterIndex;
    }

    if (EditedEmitters == 0)
    {
        return TEXT("error:no stateless emitter found (preset requires UE 5.8 lightweight template)");
    }
    System->RequestCompile(false);
    System->MarkPackageDirty();
    return FString::Printf(TEXT("ok:%d stateless emitter(s) configured; renderers=[%s]; modules=[%s]; stateless_local_space=true"),
        EditedEmitters, *FString::Join(RendererReport, TEXT(", ")), *FString::Join(ModuleReport, TEXT(", ")));
}

FString UHeliosWeaponVFXTools::ConfigureRealismEffect(UNiagaraSystem* System, FName Preset)
{
    if (!System) return TEXT("error:null system");
    if (Preset.ToString().StartsWith(TEXT("Boss"))) return ConfigureWhiteSuit(System,Preset);
    if (Preset == TEXT("ShotgunFlare")) return ConfigureLaserEnemyHit(System,true);
    if (Preset == TEXT("LaserEnemyHit")) return ConfigureLaserEnemyHit(System);
    if (Preset == TEXT("RealismRocketExplosion")) return ConfigureExplosion(System, true);
    const FName BasePreset = ResolveRealismBasePreset(Preset);
    if (BasePreset.IsNone()) return FString::Printf(TEXT("error:unknown realism preset %s"), *Preset.ToString());
    const FString ConfigureResult = ConfigureWeaponEffect(System, BasePreset);
    if (!ConfigureResult.StartsWith(TEXT("ok:"))) return ConfigureResult;
    return ApplyRealismMaterials(System, BasePreset);
}
