#include "HeliosWeaponVFXTools.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraSpriteRendererProperties.h"
#include "Materials/MaterialInterface.h"
#include "Stateless/NiagaraStatelessEmitter.h"
#include "Stateless/NiagaraStatelessSpawnInfo.h"
#include "Stateless/Modules/NiagaraStatelessModule_InitializeParticle.h"
#include "Stateless/Modules/NiagaraStatelessModule_AddVelocity.h"
#include "Stateless/Modules/NiagaraStatelessModule_ShapeLocation.h"
#include "Stateless/Modules/NiagaraStatelessModule_ScaleColor.h"
#include "Stateless/Modules/NiagaraStatelessModule_ScaleSpriteSize.h"
#include "Stateless/Modules/NiagaraStatelessModule_AccelerationForce.h"
#include "Stateless/Modules/NiagaraStatelessModule_Drag.h"
#include "Stateless/Modules/NiagaraStatelessModule_SolveVelocitiesAndForces.h"

namespace VXAuthor
{
struct Layer
{
    const TCHAR* Name; const TCHAR* Material;
    int Count; float LifeMin,LifeMax; FLinearColor Color; FVector2f Size;
    float SpeedMin,SpeedMax,Cone,Radius,Delay=0,Gravity=0,Drag=0;
    bool Aligned=false; float EndSize=1;
};
void Curve(FNiagaraDistributionBase& D,int Channels,float A,float B,float C,bool Alpha=false)
{
    D.Mode=ENiagaraDistributionMode::NonUniformCurve;D.ChannelCurves.SetNum(Channels);
    for(int I=0;I<Channels;++I){auto& R=D.ChannelCurves[I];R.Reset();const bool Flat=Alpha&&I<3;R.AddKey(0,Flat?1:A);R.AddKey(.25f,Flat?1:B);R.AddKey(1,Flat?1:C);}D.UpdateValuesFromDistribution();
}
}
FString UHeliosWeaponVFXTools::ConfigureFighterEffect(UNiagaraSystem* System,FName Kind)
{
    using namespace VXAuthor;
    if(!System||System->GetEmitterHandles().IsEmpty())return TEXT("error:missing lightweight template");
    TArray<Layer> Layers;
    if(Kind==TEXT("Muzzle"))Layers={
        {TEXT("WhitePlasma"),TEXT("Fire"),3,.035f,.085f,{.65f,.88f,1,.85f},{42,62},120,450,12,2,0,0,0,false,.35f},
        {TEXT("BlueCorona"),TEXT("Glow"),1,.06f,.10f,{.025f,.35f,1,.6f},{95,95},0,0,0,0},
        {TEXT("EnergyJets"),TEXT("Spark"),7,.04f,.12f,{.25f,.65f,1,1},{2,34},600,1200,13,3,0,0,2,true,.2f}};
    else if(Kind==TEXT("Impact"))Layers={
        {TEXT("WhiteContact"),TEXT("Glow"),1,.055f,.11f,{.65f,.88f,1,.95f},{68,68},0,0,0,0},
        {TEXT("IonSparks"),TEXT("Spark"),16,.11f,.34f,{.18f,.6f,1,.95f},{2.8f,40},160,900,125,3,0,500,1,true,.12f},
        {TEXT("HotFlecks"),TEXT("Spark"),7,.16f,.42f,{1,.33f,.045f,.9f},{2,16},120,500,120,2,0,700,1,true,.1f},
        {TEXT("ContactVapor"),TEXT("Smoke"),3,.2f,.55f,{.33f,.4f,.44f,.32f},{55,65},15,65,65,6,.025f,-15,1,false,1.4f}};
    else if(Kind==TEXT("Launch"))Layers={
        {TEXT("EjectionGas"),TEXT("Smoke"),8,.15f,.40f,{.62f,.67f,.72f,.34f},{48,65},70,210,55,8,0,0,3,false,1.4f},
        {TEXT("IgniterFlecks"),TEXT("Spark"),5,.08f,.22f,{1,.5f,.12f,.8f},{2,22},120,320,30,2,0,60,1,true,.2f}};
    else if(Kind==TEXT("Ignition"))Layers={
        {TEXT("HotIgnition"),TEXT("Fire"),5,.06f,.16f,{1,.45f,.1f,.85f},{48,80},150,420,20,8,0,0,1,false,.3f},
        {TEXT("IgnitionCorona"),TEXT("Glow"),1,.04f,.09f,{1,.68f,.3f,.8f},{105,105},0,0,0,0},
        {TEXT("IgnitionSmoke"),TEXT("Smoke"),4,.2f,.5f,{.6f,.63f,.65f,.3f},{80,90},10,70,50,15,0,0,2,false,1.6f}};
    else if(Kind==TEXT("Trail"))Layers={
        {TEXT("ExhaustVapor"),TEXT("Smoke"),3,.50f,.85f,{.68f,.70f,.72f,.30f},{105,120},4,26,120,36,0,-12,2,false,1.8f}};
    else if(Kind==TEXT("Explosion"))Layers={
        {TEXT("WhiteHotFlash"),TEXT("Glow"),1,.05f,.10f,{1,.78f,.42f,.95f},{520,520},0,0,0,0,0,0,0,false,.3f},
        {TEXT("FireLobes"),TEXT("Fire"),14,.30f,.60f,{1,.19f,.012f,.80f},{280,360},180,680,360,40,.018f,-50,1.1f,false,1.4f},
        {TEXT("HotInterior"),TEXT("Fire"),6,.15f,.35f,{1,.6f,.15f,.75f},{190,230},80,300,360,30,0,0,1,false,.65f},
        {TEXT("RollingSmoke"),TEXT("Smoke"),16,1.4f,2.4f,{.16f,.145f,.13f,.50f},{320,370},55,180,160,85,.13f,-60,1.5f,false,2.1f},
        {TEXT("RadialSparks"),TEXT("Spark"),52,.35f,.95f,{1,.37f,.035f,.92f},{3,55},450,1550,360,25,0,780,.35f,true,.1f},
        {TEXT("EmberFragments"),TEXT("Fire"),16,.4f,1.25f,{1,.12f,.006f,.7f},{9,14},200,800,270,20,.03f,600,.6f,false,.05f}};
    else if(Kind==TEXT("Dust"))Layers={
        {TEXT("GroundDust"),TEXT("Smoke"),22,.5f,1.4f,{.38f,.32f,.23f,.25f},{140,95},170,600,180,25,0,0,3,false,2.1f}};
    else return TEXT("error:unknown VX preset");
    System->Modify();
    auto* Source=System->GetEmitterHandles()[0].GetStatelessEmitter();if(!Source)return TEXT("error:requires stateless emitter");
    while(System->GetEmitterHandles().Num()<Layers.Num())
    {
        auto* E=Cast<UNiagaraStatelessEmitter>(StaticDuplicateObject(Source,System));E->ClearFlags(RF_Standalone|RF_Public);
        E->SetUniqueEmitterName(FString::Printf(TEXT("VXLayer%d"),System->GetEmitterHandles().Num()));FNiagaraEmitterHandle H(*E);System->AddEmitterHandleDirect(H);
    }
    if(System->GetEmitterHandles().Num()!=Layers.Num())return TEXT("error:unexpected layer count; use fresh template");
    System->bFixedBounds=true;System->SetFixedBounds(FBox(FVector(-2200),FVector(2200)));
    int Total=0;
    for(int I=0;I<Layers.Num();++I)
    {
        const auto& L=Layers[I];Total+=L.Count;auto& H=System->GetEmitterHandles()[I];auto* E=H.GetStatelessEmitter();E->Modify();H.SetName(L.Name,*System);
        for(auto* R:E->GetRenderers())
        {
            R->Modify();R->SetIsEnabled(false);
            if(auto* S=Cast<UNiagaraSpriteRendererProperties>(R))
            {
                S->SetIsEnabled(true);S->Material=LoadObject<UMaterialInterface>(nullptr,*(FString(TEXT("/Game/Helios/Weapons/Fighter/Effects/M_VXFX_"))+L.Material));
                if(!S->Material)return TEXT("error:missing VX material");
                S->Alignment=L.Aligned?ENiagaraSpriteAlignment::VelocityAligned:ENiagaraSpriteAlignment::Unaligned;S->FacingMode=ENiagaraSpriteFacingMode::FaceCamera;
            }R->PostEditChange();
        }
        if(!E->GetNumSpawnInfos())E->AddSpawnInfo();
        for(int J=0;J<E->GetNumSpawnInfos();++J)
        {auto* S=E->GetSpawnInfoByIndex(J);S->bEnabled=J==0;S->Type=ENiagaraStatelessSpawnInfoType::Burst;S->SpawnTime=L.Delay;S->Amount=FNiagaraDistributionRangeInt(L.Count);S->bLoopCountLimitEnabled=true;S->LoopCountLimit=FNiagaraDistributionRangeInt(1);}
        for(UNiagaraStatelessModule* M:E->GetModules())
        {
            M->Modify();M->SetIsModuleEnabled(M->IsA<UNiagaraStatelessModule_InitializeParticle>()||M->IsA<UNiagaraStatelessModule_AddVelocity>()||M->IsA<UNiagaraStatelessModule_ShapeLocation>()||M->IsA<UNiagaraStatelessModule_ScaleColor>()||M->IsA<UNiagaraStatelessModule_ScaleSpriteSize>()||M->IsA<UNiagaraStatelessModule_AccelerationForce>()||M->IsA<UNiagaraStatelessModule_Drag>()||M->IsA<UNiagaraStatelessModule_SolveVelocitiesAndForces>());
            if(auto* P=Cast<UNiagaraStatelessModule_InitializeParticle>(M))
            {P->LifetimeDistribution=FNiagaraDistributionRangeFloat(L.LifeMin,L.LifeMax);P->MassDistribution=FNiagaraDistributionRangeFloat(1);P->ColorDistribution.InitConstant(L.Color);P->SpriteSizeDistribution.InitConstant(L.Size);P->SpriteRotationDistribution=FNiagaraDistributionRangeFloat(0,360);}
            if(auto* V=Cast<UNiagaraStatelessModule_AddVelocity>(M))
            {V->VelocityType=ENSM_VelocityType::InCone;V->ConeRotationType=ENSM_ConeRotationType::Direction;V->ConeDirection.InitConstant(FVector3f::XAxisVector);V->ConeAngle=L.Cone;V->InnerCone=Kind==TEXT("Dust")?175.f:0.f;V->ConeVelocityDistribution=FNiagaraDistributionRangeFloat(L.SpeedMin,L.SpeedMax);V->ConeVelocityScale=FNiagaraDistributionRangeFloat(1);V->CoordinateSpace=ENiagaraCoordinateSpace::Local;}
            if(auto* S=Cast<UNiagaraStatelessModule_ShapeLocation>(M)){S->ShapePrimitive=ENSM_ShapePrimitive::Sphere;S->SphereRadius=FNiagaraDistributionRangeFloat(0,L.Radius);}
            if(auto* A=Cast<UNiagaraStatelessModule_AccelerationForce>(M)){A->AccelerationDistribution.InitConstant(FVector3f(0,0,-L.Gravity));A->CoordinateSpace=ENiagaraCoordinateSpace::World;}
            if(auto* D=Cast<UNiagaraStatelessModule_Drag>(M))D->DragDistribution=FNiagaraDistributionRangeFloat(L.Drag);
            if(auto* C=Cast<UNiagaraStatelessModule_ScaleColor>(M))Curve(C->ScaleDistribution,4,FCString::Strcmp(L.Material,TEXT("Smoke"))==0?0.f:1.f,.85f,0,true);
            if(auto* S=Cast<UNiagaraStatelessModule_ScaleSpriteSize>(M))Curve(S->ScaleDistribution,2,L.Aligned?1.f:.45f,1.f,L.EndSize);
            M->PostEditChange();
        }E->PostEditChange();
    }
    System->RequestCompile(false);System->MarkPackageDirty();return FString::Printf(TEXT("ok:VX %s layers=%d particles=%d"),*Kind.ToString(),Layers.Num(),Total);
}
