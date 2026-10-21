#include "HeliosWeaponVFXTools.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraMeshRendererProperties.h"
#include "Stateless/NiagaraStatelessEmitter.h"
#include "Stateless/NiagaraStatelessSpawnInfo.h"
#include "Stateless/Modules/NiagaraStatelessModule_InitializeParticle.h"
#include "Stateless/Modules/NiagaraStatelessModule_InitialMeshOrientation.h"
#include "Stateless/Modules/NiagaraStatelessModule_AddVelocity.h"
#include "Stateless/Modules/NiagaraStatelessModule_ShapeLocation.h"
#include "Stateless/Modules/NiagaraStatelessModule_ScaleColor.h"
#include "Stateless/Modules/NiagaraStatelessModule_ScaleSpriteSize.h"
#include "Stateless/Modules/NiagaraStatelessModule_ScaleMeshSize.h"
#include "Stateless/Modules/NiagaraStatelessModule_SubUVAnimation.h"
#include "Stateless/Modules/NiagaraStatelessModule_AccelerationForce.h"
#include "Stateless/Modules/NiagaraStatelessModule_Drag.h"
#include "Stateless/Modules/NiagaraStatelessModule_SolveVelocitiesAndForces.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UnrealType.h"

namespace
{
struct FMHLayer
{
    float Life;FLinearColor Color;FVector2f Size;int32 Count;float Speed;float Radius;
    const TCHAR* Material=TEXT("M_MH_Particle");const TCHAR* Mesh=nullptr;FVector3f MeshScale=FVector3f(1);
    float Delay=0, Gravity=0, Drag=0, Cone=170;
    bool Atlas=false;
    FVector3f Offset=FVector3f::ZeroVector;
};
void CurveMH(FNiagaraDistributionBase& D,int32 Channels,float A,float B,float C,bool Alpha=false)
{
    D.Mode=ENiagaraDistributionMode::NonUniformCurve;D.ChannelCurves.SetNum(Channels);
    for(int32 I=0;I<Channels;++I){auto& R=D.ChannelCurves[I];R.Reset();const bool Flat=Alpha&&I<3;R.AddKey(0,Flat?1:A);R.AddKey(.3f,Flat?1:B);R.AddKey(1,Flat?1:C);}D.UpdateValuesFromDistribution();
}
}
FString UHeliosWeaponVFXTools::ConfigureMysteriousEffect(UNiagaraSystem* System,FName Kind)
{
    if(!System||System->GetEmitterHandles().IsEmpty())return TEXT("error:missing template");
    TArray<FMHLayer> Layers;
    const bool Cinematic=Kind==TEXT("Explosion")||Kind==TEXT("SurfaceDust")||Kind==TEXT("Trail")||Kind==TEXT("Portal")||Kind==TEXT("Wake");
    const TCHAR* Cylinder=TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
    const TCHAR* Ring=TEXT("/Game/Helios/Weapons/MysteriousHand/SM_MH_Ring");
    if(Kind==TEXT("Beam"))
    {
        Layers={{.22f,FLinearColor(5,4,8,1),FVector2f(1),1,0,0,TEXT("M_MH_Beam"),Cylinder,FVector3f(.015f,.015f,1)},
                {.22f,FLinearColor(.65f,.15f,3,.25f),FVector2f(1),1,0,0,TEXT("M_MH_Beam"),Cylinder,FVector3f(.06f,.06f,1)}};
    }
    else if(Kind==TEXT("Marker")||Kind==TEXT("Portal"))
    {
        const bool Portal=Kind==TEXT("Portal");
        Layers={{Portal?.7f:1.05f,Portal?FLinearColor(.65f,.12f,2.1f,.8f):FLinearColor(2,.4f,.04f,.85f),FVector2f(1),1,0,0,TEXT("M_MH_Beam"),Ring,FVector3f(Portal?5.f:.8f)},
                {.5f,FLinearColor(1,.2f,2,.7f),FVector2f(4,35),Portal?42:8,Portal?320.f:15.f,Portal?130.f:35.f}};
        if(Portal)Layers.Add({.22f,FLinearColor(.9f,.35f,2.8f,.5f),FVector2f(360,110),1,0,0});
    }
    else if(Kind==TEXT("Explosion"))
    {
        Layers={
            {.075f,FLinearColor(9,6,3,1),FVector2f(600),1,0,0},
            {.52f,FLinearColor(2.8f,1.1f,.25f,.95f),FVector2f(1300),1,80,0,TEXT("M_MH_FireAtlas")},
            {.62f,FLinearColor(2.1f,.7f,.14f,.87f),FVector2f(700),7,370,75,TEXT("M_MH_FireAtlas")},
            {.8f,FLinearColor(1.5f,.42f,.08f,.75f),FVector2f(450),9,470,35,TEXT("M_MH_FireAtlas")},
            {3.65f,FLinearColor(.48f,.43f,.39f,.95f),FVector2f(530,610),15,155,180,TEXT("M_MH_SmokeAtlas")},
            {2.7f,FLinearColor(.58f,.49f,.4f,.47f),FVector2f(390,430),12,245,210,TEXT("M_MH_SmokeAtlas")},
            {1.1f,FLinearColor(7,2,.23f,1),FVector2f(3,39),85,1180,20},
            {1.4f,FLinearColor(.08f,.065f,.055f,1),FVector2f(9,18),24,850,25,TEXT("M_MH_DebrisSolid"),TEXT("/Game/Helios/Weapons/MysteriousHand/SM_MH_Debris"),FVector3f(1)},
            {.19f,FLinearColor(.65f,.12f,2.5f,.8f),FVector2f(3,85),16,1100,20}};
        for(int32 J=1;J<=5;++J){Layers[J].Atlas=true;Layers[J].Cone=J>=4?80.f:120.f;Layers[J].Drag=J>=4?.85f:1.5f;}
        Layers[1].Offset=FVector3f(0,0,180);Layers[2].Offset=FVector3f(0,0,100);Layers[3].Offset=FVector3f(0,0,80);
        Layers[2].Delay=.035f;Layers[3].Delay=.11f;
        Layers[4].Delay=.24f;Layers[4].Gravity=35;Layers[4].Offset=FVector3f(0,0,100);
        Layers[5].Delay=.17f;Layers[5].Gravity=18;Layers[5].Offset=FVector3f(0,0,50);
        for(int32 J=6;J<=7;++J){Layers[J].Delay=.025f;Layers[J].Gravity=-780;Layers[J].Drag=.55f;Layers[J].Cone=145;}
    }
    else if(Kind==TEXT("SurfaceDust"))
    {
        Layers={{1.6f,FLinearColor(.58f,.48f,.36f,.5f),FVector2f(400,180),40,1600,55,TEXT("M_MH_SmokeAtlas")},
                {.65f,FLinearColor(.75f,.2f,2,.4f),FVector2f(3,40),24,1550,15}};
        for(auto& L:Layers){L.Delay=.07f;L.Cone=178;L.Drag=1.7f;}
        Layers[0].Atlas=true;
    }
    else if(Kind==TEXT("Trail"))
    {
        Layers={{.085f,FLinearColor(9,3.6f,.6f,.9f),FVector2f(24,260),240,800,3},
                {.17f,FLinearColor(3,1.4f,.5f,.6f),FVector2f(75,170),100,450,7,TEXT("M_MH_FireAtlas")},
                {.25f,FLinearColor(.35f,.32f,.3f,.2f),FVector2f(110,160),0,100,12,TEXT("M_MH_SmokeAtlas")},
                {.16f,FLinearColor(.8f,.18f,2,.7f),FVector2f(2,65),65,350,16}};
        Layers[1].Atlas=Layers[2].Atlas=true;
        for(auto& L:Layers){L.Cone=12;L.Offset=FVector3f(0,0,60);}
    }
    else if(Kind==TEXT("Wake"))
    {Layers={{1.2f,FLinearColor(.4f,.38f,.37f,.45f),FVector2f(200,240),9,20,30,TEXT("M_MH_SmokeAtlas")}};Layers[0].Atlas=true;Layers[0].Cone=60;}
    else if(Kind==TEXT("LaserHit"))
    {Layers={{.12f,FLinearColor(5,3,7,1),FVector2f(18),1,0,0},{.28f,FLinearColor(1,.2f,3,.85f),FVector2f(1,10),18,230,2},{.20f,FLinearColor(1,.35f,2,.7f),FVector2f(1),1,0,0,TEXT("M_MH_Beam"),Ring,FVector3f(.3f)}};}
    else
    {Layers={{.07f,FLinearColor(5,3,8,1),FVector2f(7),1,0,0},{.14f,FLinearColor(.8f,.16f,2,.7f),FVector2f(1,6),9,35,1}};}
    System->Modify();
    if(Cinematic)System->GetExposedParameters().AddParameter(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.BlastRadius")));
    for(const TCHAR* P:{TEXT("User.Direction"),TEXT("User.CoreScale"),TEXT("User.HaloScale")})System->GetExposedParameters().AddParameter(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(),P));
    auto* Source=System->GetEmitterHandles()[0].GetStatelessEmitter();if(!Source)return TEXT("error:requires lightweight template");
    while(System->GetEmitterHandles().Num()<Layers.Num())
    {
        auto* E=Cast<UNiagaraStatelessEmitter>(StaticDuplicateObject(Source,System));E->ClearFlags(RF_Public|RF_Standalone);
        E->SetUniqueEmitterName(FString::Printf(TEXT("MH_Layer%d"),System->GetEmitterHandles().Num()));FNiagaraEmitterHandle H(*E);System->GetEmitterHandles().Add(H);
    }
    int32 I=0;
    for(auto& Handle:System->GetEmitterHandles())
    {
        auto* E=Handle.GetStatelessEmitter();if(!E)continue;E->Modify();
        if(Cinematic)
        {
            if(auto* B=FindFProperty<FStructProperty>(E->GetClass(),TEXT("FixedBounds")))
                *B->ContainerPtrToValuePtr<FBox>(E)=FBox(FVector(-1800,-1800,-1500),FVector(1800,1800,2500));
        }
        if(!E->GetModule(UNiagaraStatelessModule_InitialMeshOrientation::StaticClass()))
        {
            auto* A=FindFProperty<FArrayProperty>(E->GetClass(),TEXT("Modules"));auto* Modules=A->ContainerPtrToValuePtr<TArray<TObjectPtr<UNiagaraStatelessModule>>>(E);Modules->Add(NewObject<UNiagaraStatelessModule_InitialMeshOrientation>(E));
        }
        if(Cinematic)
        {
            auto* A=FindFProperty<FArrayProperty>(E->GetClass(),TEXT("Modules"));auto* Modules=A->ContainerPtrToValuePtr<TArray<TObjectPtr<UNiagaraStatelessModule>>>(E);
            for(UClass* Type:{UNiagaraStatelessModule_SubUVAnimation::StaticClass(),UNiagaraStatelessModule_AccelerationForce::StaticClass(),UNiagaraStatelessModule_Drag::StaticClass(),UNiagaraStatelessModule_SolveVelocitiesAndForces::StaticClass()})
                if(!E->GetModule(Type))Modules->Add(NewObject<UNiagaraStatelessModule>(E,Type));
        }
        const bool Enabled=I<Layers.Num();const FMHLayer L=Layers[FMath::Min(I,Layers.Num()-1)];
        auto* SP=E->GetSpawnInfoByIndex(0);if(!SP)SP=&E->AddSpawnInfo();SP->Amount=FNiagaraDistributionRangeInt(Enabled?L.Count:0);SP->SpawnTime=L.Delay;
        SP->Type=Kind==TEXT("Trail")?ENiagaraStatelessSpawnInfoType::Rate:ENiagaraStatelessSpawnInfoType::Burst;SP->Rate=FNiagaraDistributionRangeFloat(Enabled?L.Count:0);
        auto* StateProperty=FindFProperty<FStructProperty>(E->GetClass(),TEXT("EmitterState"));
        if(StateProperty){auto* State=StateProperty->ContainerPtrToValuePtr<FNiagaraEmitterStateData>(E);State->LoopBehavior=ENiagaraLoopBehavior::Once;State->LoopDuration=FNiagaraDistributionRangeFloat(Cinematic?4.5f:1.3f);}
        UMaterialInterface* Material=LoadObject<UMaterialInterface>(nullptr,*(FString(TEXT("/Game/Helios/Weapons/MysteriousHand/"))+L.Material));
        // Replace renderers, so repeated asset generation cannot accumulate hidden renderers.
        TArray<UNiagaraRendererProperties*> OldRenderers=E->GetRenderers();for(auto* R:OldRenderers)E->RemoveRenderer(R,FGuid());
        if(L.Mesh)
        {
            auto* R=NewObject<UNiagaraMeshRendererProperties>(E);R->Meshes.SetNum(1);R->Meshes[0].Mesh=LoadObject<UStaticMesh>(nullptr,L.Mesh);
            R->bOverrideMaterials=true;R->OverrideMaterials.SetNum(1);R->OverrideMaterials[0].ExplicitMat=Material;R->SetIsEnabled(Enabled);E->AddRenderer(R,FGuid());
        }
        else
        {
            auto* R=NewObject<UNiagaraSpriteRendererProperties>(E);
            R->SetIsEnabled(Enabled);R->Material=Material;R->FacingMode=ENiagaraSpriteFacingMode::FaceCamera;R->Alignment=L.Size.Y>L.Size.X*2?ENiagaraSpriteAlignment::VelocityAligned:ENiagaraSpriteAlignment::Unaligned;
            R->SubImageSize=L.Atlas?FVector2D(8,8):FVector2D(1);R->bSubImageBlend=L.Atlas;
            R->SortMode=ENiagaraSortMode::ViewDepth;E->AddRenderer(R,FGuid());R->PostEditChange();
        }
        for(UNiagaraStatelessModule* M:E->GetModules())
        {
            M->Modify();M->SetIsModuleEnabled(M->IsA<UNiagaraStatelessModule_InitializeParticle>()||M->IsA<UNiagaraStatelessModule_InitialMeshOrientation>()||M->IsA<UNiagaraStatelessModule_AddVelocity>()||M->IsA<UNiagaraStatelessModule_ShapeLocation>()||M->IsA<UNiagaraStatelessModule_ScaleColor>()||M->IsA<UNiagaraStatelessModule_ScaleSpriteSize>()||M->IsA<UNiagaraStatelessModule_ScaleMeshSize>());
            if(Cinematic)
            {
                if(auto* UV=Cast<UNiagaraStatelessModule_SubUVAnimation>(M)){M->SetIsModuleEnabled(L.Atlas);UV->NumFrames=64;UV->AnimationMode=ENSMSubUVAnimation_Mode::Linear;}
                if(auto* G=Cast<UNiagaraStatelessModule_AccelerationForce>(M)){M->SetIsModuleEnabled(true);G->AccelerationDistribution.InitConstant(FVector3f(0,0,L.Gravity));G->CoordinateSpace=ENiagaraCoordinateSpace::World;}
                if(auto* D=Cast<UNiagaraStatelessModule_Drag>(M)){M->SetIsModuleEnabled(true);D->DragDistribution=FNiagaraDistributionRangeFloat(L.Drag);}
                if(M->IsA<UNiagaraStatelessModule_SolveVelocitiesAndForces>())M->SetIsModuleEnabled(true);
            }
            if(auto* Init=Cast<UNiagaraStatelessModule_InitializeParticle>(M))
            {Init->LifetimeDistribution=FNiagaraDistributionRangeFloat(L.Life);Init->ColorDistribution.InitConstant(L.Color);Init->SpriteSizeDistribution.InitConstant(L.Size);Init->MeshScaleDistribution.InitConstant(L.MeshScale);Init->SpriteRotationDistribution=FNiagaraDistributionRangeFloat(0);}
            if(auto* Init=Cast<UNiagaraStatelessModule_InitializeParticle>(M);Init&&Cinematic)
            {Init->SpriteRotationDistribution=FNiagaraDistributionRangeFloat(L.Atlas?-180.f:0.f,L.Atlas?180.f:0.f);Init->InitialPositionDistribution.InitConstant(L.Offset);}
            if(auto* Init=Cast<UNiagaraStatelessModule_InitializeParticle>(M);Init&&Kind==TEXT("Beam"))
            {Init->MeshScaleDistribution.Mode=ENiagaraDistributionMode::Binding;Init->MeshScaleDistribution.ParameterBinding=FNiagaraVariableBase(FNiagaraTypeDefinition::GetVec3Def(),I==0?TEXT("User.CoreScale"):TEXT("User.HaloScale"));}
            if(auto* O=Cast<UNiagaraStatelessModule_InitialMeshOrientation>(M))
            {O->MeshOrientationMode=ENSMInitialMeshOrientationMode::OrientToAxis;O->MeshAxisToOrient.InitConstant(FVector3f::ZAxisVector);O->OrientationVector.Mode=ENiagaraDistributionMode::Binding;O->OrientationVector.ParameterBinding=FNiagaraVariableBase(FNiagaraTypeDefinition::GetVec3Def(),TEXT("User.Direction"));}
            if(auto* O=Cast<UNiagaraStatelessModule_InitialMeshOrientation>(M);O&&Kind==TEXT("Explosion")&&I==7)O->MeshOrientationMode=ENSMInitialMeshOrientationMode::Random;
            if(auto* V=Cast<UNiagaraStatelessModule_AddVelocity>(M))
            {V->VelocityType=ENSM_VelocityType::InCone;V->ConeVelocityDistribution=FNiagaraDistributionRangeFloat(L.Speed*.5f,L.Speed);V->ConeRotationType=ENSM_ConeRotationType::Direction;V->ConeDirection.InitConstant(FVector3f::ZAxisVector);V->ConeAngle=L.Cone;V->InnerCone=Kind==TEXT("SurfaceDust")?165:0;V->CoordinateSpace=Kind==TEXT("Explosion")||Kind==TEXT("Trail")?ENiagaraCoordinateSpace::World:ENiagaraCoordinateSpace::Local;}
            if(auto* S=Cast<UNiagaraStatelessModule_ShapeLocation>(M)){S->ShapePrimitive=ENSM_ShapePrimitive::Sphere;S->SphereRadius=FNiagaraDistributionRangeFloat(L.Radius);}
            if(auto* S=Cast<UNiagaraStatelessModule_ShapeLocation>(M);S&&Kind==TEXT("Wake")){S->ShapePrimitive=ENSM_ShapePrimitive::Cylinder;S->CylinderHeight=FNiagaraDistributionRangeFloat(200);S->CylinderRadius=FNiagaraDistributionRangeFloat(24);}
            if(auto* C=Cast<UNiagaraStatelessModule_ScaleColor>(M))CurveMH(C->ScaleDistribution,4,1,.8f,0,true);
            if(auto* S=Cast<UNiagaraStatelessModule_ScaleSpriteSize>(M))CurveMH(S->ScaleDistribution,2,.3f,1,.1f);
            if(auto* S=Cast<UNiagaraStatelessModule_ScaleSpriteSize>(M);S&&L.Atlas)CurveMH(S->ScaleDistribution,2,.5f,1,L.Life>1?1.8f:1.15f);
            if(auto* C=Cast<UNiagaraStatelessModule_ScaleColor>(M);C&&L.Atlas&&L.Life>1)CurveMH(C->ScaleDistribution,4,.12f,.85f,0,true);
            if(auto* S=Cast<UNiagaraStatelessModule_ScaleMeshSize>(M))
            {
                CurveMH(S->ScaleDistribution,3,Kind==TEXT("Beam")?1:.2f,1,Kind==TEXT("Beam")?.15f:1.3f);
                if(Kind==TEXT("Beam")){auto& Z=S->ScaleDistribution.ChannelCurves[2];Z.Reset();Z.AddKey(0,1);Z.AddKey(1,1);S->ScaleDistribution.UpdateValuesFromDistribution();}
            }
            M->PostEditChange();
        }
        E->PostEditChange();++I;
    }
    System->RequestCompile(false);System->WaitForCompilationComplete();System->PostEditChange();System->MarkPackageDirty();
    return FString::Printf(TEXT("ok:%s layers=%d"),*Kind.ToString(),Layers.Num());
}
