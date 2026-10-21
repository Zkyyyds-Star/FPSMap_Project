#pragma once
#include "CoreMinimal.h"

// Shared by the runtime, editor authoring helper and QA. Units are cm and seconds.
namespace HeliosColaVFX
{
struct FLayer
{
    const TCHAR* Name;
    int32 Count;
    float LifeMin, LifeMax, SpeedMin, SpeedMax, Drag, Gravity, SpawnRadius;
    FVector2f Size;
    FLinearColor Color;
    int32 Material; // foam, liquid, mist
    bool bSpray, bAligned;
    float Delay,SurfaceConeAngle; // Niagara uses the full cone angle, not its half-angle.
};
inline const FLayer Layers[] = {
    {TEXT("LiquidJets"),32,.18f,.32f,2200,4000,3,980,12,{16,130},{.20f,.055f,.012f,.95f},1,true,true,0,95},
    {TEXT("FoamClumps"),180,.85f,1.5f,1200,2700,4.5f,980,18,{100,125},{.95f,.88f,.70f,.85f},0,false,false,.015f,150},
    {TEXT("FoamFragments"),220,.65f,1.25f,2100,4400,5.5f,980,14,{30,44},{.95f,.90f,.77f,.8f},0,false,false,.03f,170},
    {TEXT("EdgeDroplets"),160,.58f,.72f,5700,9000,6,980,12,{10,42},{.23f,.075f,.02f,.9f},1,true,true,.01f,170},
    {TEXT("EdgeFoam"),110,.58f,.72f,5800,9000,6,980,12,{30,38},{.92f,.87f,.72f,.75f},0,true,true,.015f,170},
    {TEXT("FineMist"),48,1.2f,1.8f,550,1250,3,70,18,{145,175},{.70f,.63f,.48f,.12f},2,false,false,.07f,170}
};
inline constexpr float ReleaseSeconds=2.f;
inline constexpr int32 ParticleBudget=750;
inline const TCHAR* MaterialNames[]={TEXT("M_ColaFoamCinematic"),TEXT("M_ColaLiquidCinematic"),TEXT("M_ColaMistCinematic")};
inline const TCHAR* MaterialParameters[]={TEXT("User.FoamMaterial"),TEXT("User.LiquidMaterial"),TEXT("User.MistMaterial")};
inline float Travel(float Speed,float Drag,float Gravity,float Time)
{
    const float Lambda=(1.f-FMath::Exp(-Drag*Time))/Drag;
    // Conservative over every launch direction, including the downward air burst.
    return Speed*Lambda+Gravity/Drag*(Time-Lambda);
}
inline float Envelope()
{
    float Radius=1;
    for(const auto& L:Layers)
        Radius=FMath::Max(Radius,L.SpawnRadius+Travel(L.SpeedMax,L.Drag,L.Gravity,L.LifeMax)+.5f*L.Size.Size());
    return Radius;
}
inline int32 ParticleCount()
{
    int32 Count=0;for(const auto& L:Layers)Count+=L.Count;return Count;
}
}
