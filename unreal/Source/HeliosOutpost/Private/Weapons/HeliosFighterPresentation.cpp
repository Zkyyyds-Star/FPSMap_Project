#include "Audio/HeliosAudio.h"
#include "Weapons/HeliosFighterWeapon.h"
#include "Player/HeliosArenaCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundBase.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"

namespace VXArt
{
template<class T>T* Load(const TCHAR* N){return LoadObject<T>(nullptr,*(FString(TEXT("/Game/Helios/Weapons/Fighter/"))+N));}
UStaticMeshComponent* Part(AActor* Owner,const TCHAR* Name,const TCHAR* Material=nullptr)
{
    auto* C=NewObject<UStaticMeshComponent>(Owner);C->SetStaticMesh(Load<UStaticMesh>(Name));
    C->SetMobility(EComponentMobility::Movable);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetGenerateOverlapEvents(false);C->SetCanEverAffectNavigation(false);C->SetCastShadow(false);
    if(Material)C->SetMaterial(0,Load<UMaterialInterface>(Material));C->RegisterComponent();return C;
}
}
void UHeliosFighterPresentation::Initialize()
{
    if(Hull)return;auto* W=Cast<AHeliosFighterWeapon>(GetOwner());auto* P=W?W->Player():nullptr;if(!P)return;
    Hull=VXArt::Part(W,TEXT("SM_VX_Hull"));Hull->SetCastShadow(true);Hull->SetOwnerNoSee(true);Hull->SetCastHiddenShadow(true);
    Cockpit=VXArt::Part(W,TEXT("SM_VX_Cockpit"));Cockpit->AttachToComponent(P->FirstPersonCamera,FAttachmentTransformRules::SnapToTargetNotIncludingScale);Cockpit->SetOnlyOwnerSee(true);
    InstrumentLight=NewObject<UPointLightComponent>(W);InstrumentLight->SetupAttachment(P->FirstPersonCamera);InstrumentLight->SetRelativeLocation(FVector(28,0,5));InstrumentLight->SetLightColor(FLinearColor(.24f,.66f,1));InstrumentLight->SetIntensity(14);InstrumentLight->SetAttenuationRadius(180);InstrumentLight->SetCastShadows(false);InstrumentLight->RegisterComponent();
    SavedCockpitOffset=Cockpit->GetRelativeLocation();
    for(int I=0;I<12;++I)
    {
        Beams.Add(VXArt::Part(W,TEXT("Effects/SM_VX_BeamRound"),TEXT("Effects/M_VXFX_Core")));
        Halos.Add(VXArt::Part(W,TEXT("Effects/SM_VX_BeamRound"),TEXT("Effects/M_VXFX_Halo")));
        BeamMaterials.Add(Beams.Last()->CreateDynamicMaterialInstance(0));BeamMaterials.Add(Halos.Last()->CreateDynamicMaterialInstance(0));BeamAges.Add(1);
    }
    for(int I=0;I<2;++I)Thrusters.Add(VXArt::Part(W,TEXT("SM_VX_Glow"),TEXT("Effects/M_VXFX_Thruster")));
    for(int I=0;I<2;++I)Thrusters.Add(VXArt::Part(W,TEXT("SM_VX_Missile")));
    for(const TCHAR* N:{TEXT("Deploy"),TEXT("EngineLoop"),TEXT("Laser"),TEXT("Lock"),TEXT("Missile"),TEXT("Explosion"),TEXT("Overheat")})
        Sounds.Add(FName(N),VXArt::Load<USoundBase>(*(FString(TEXT("SFX_VX_"))+N)));
    for(const TCHAR* N:{TEXT("LaserPunch"),TEXT("Ejection"),TEXT("Ignition"),TEXT("Blast")})
        Sounds.Add(FName(N),VXArt::Load<USoundBase>(*(FString(TEXT("Effects/SFX_VX_"))+N)));
    for(const TCHAR* N:{TEXT("Muzzle"),TEXT("Impact"),TEXT("Launch"),TEXT("Ignition"),TEXT("Trail"),TEXT("Explosion"),TEXT("Dust")})
        EffectSystems.Add(FName(N),VXArt::Load<UNiagaraSystem>(*(FString(TEXT("Effects/NS_VX_"))+N)));
    for(int I=0;I<3;++I)
    {
        auto* L=NewObject<UPointLightComponent>(W);L->SetMobility(EComponentMobility::Movable);L->SetCastShadows(false);L->SetIntensity(0);
        L->SetAttenuationRadius(I==2?1800:400);L->SetLightColor(I==2?FLinearColor(1,.32f,.05f):FLinearColor(.18f,.65f,1));L->RegisterComponent();FlashLights.Add(L);
    }
    Clear();
}
void UHeliosFighterPresentation::Sound(const TCHAR* N,float Volume)
{if(AttackAudio.Num()<24)if(auto* S=Sounds.Find(FName(N));S&&*S)if(auto* A=HeliosAudio::SpawnSound2D(this,*S,Volume,1,0,nullptr,false,false))AttackAudio.Add(A);}
void UHeliosFighterPresentation::Clear()
{
    if(Hull)Hull->SetVisibility(false);if(Cockpit)Cockpit->SetVisibility(false);if(InstrumentLight)InstrumentLight->SetVisibility(false);
    for(auto& C:Beams)C->SetVisibility(false);for(auto& C:Halos)C->SetVisibility(false);for(auto& C:Thrusters)C->SetVisibility(false);
    for(auto& E:Effects)if(IsValid(E)){E->DeactivateImmediate();E->ReleaseToPool();}Effects.Reset();EffectTimes.Reset();
    for(auto& A:AttackAudio)if(IsValid(A)){A->Stop();A->DestroyComponent();}AttackAudio.Reset();
    for(auto& L:FlashLights)L->SetIntensity(0);Feedback=Flash=BlastFlash=0;
    if(Cockpit){Cockpit->SetRelativeLocation(SavedCockpitOffset);Cockpit->SetRelativeRotation(FRotator::ZeroRotator);}
    for(float& A:BeamAges)A=1;
    if(EngineAudio){EngineAudio->Stop();EngineAudio->DestroyComponent();EngineAudio=nullptr;}
}
void UHeliosFighterPresentation::Update(float Dt)
{
    auto* W=Cast<AHeliosFighterWeapon>(GetOwner());auto* P=W?W->Player():nullptr;if(!P||!W->bActive||!Hull)return;
    Clock+=Dt;Hull->SetVisibility(true);Cockpit->SetVisibility(true);InstrumentLight->SetVisibility(true);
    for(int I=AttackAudio.Num()-1;I>=0;--I)if(!IsValid(AttackAudio[I])||!AttackAudio[I]->IsPlaying())
    {if(IsValid(AttackAudio[I]))AttackAudio[I]->DestroyComponent();AttackAudio.RemoveAtSwap(I);}
    for(int I=Effects.Num()-1;I>=0;--I)
    {
        EffectTimes[I]-=Dt;if(EffectTimes[I]<=0||!IsValid(Effects[I]))
        {if(IsValid(Effects[I])){Effects[I]->DeactivateImmediate();Effects[I]->ReleaseToPool();}Effects.RemoveAtSwap(I);EffectTimes.RemoveAtSwap(I);}
    }
    Feedback=FMath::Max(0.f,Feedback-Dt*3.5f);Flash=FMath::Max(0.f,Flash-Dt*12);BlastFlash=FMath::Max(0.f,BlastFlash-Dt*4);
    const float Speed=P->GetVelocity().Size();Bank=FMath::FInterpTo(Bank,-W->Flight->ForwardRightUp.Y*2.5f,Dt,4);
    FRotator R=W->Flight->HullRotation;R.Roll=Bank*2;Hull->SetWorldLocationAndRotation(P->GetActorLocation(),R);
    const float Kick=Feedback*W->Settings->CockpitFeedback;
    Cockpit->SetRelativeRotation(FRotator(Kick*FMath::Sin(Clock*51),0,FMath::Clamp(Bank+Kick*FMath::Sin(Clock*43),-3.f,3.f)));
    Cockpit->SetRelativeLocation(SavedCockpitOffset+FVector(-Kick*.7f,0,Kick*.4f*FMath::Sin(Clock*67)));
    InstrumentLight->SetIntensity(14+Flash*90);
    for(int I=0;I<2;++I){FlashLights[I]->SetWorldLocation(W->Muzzle(I));FlashLights[I]->SetIntensity(I==FlashSide?Flash*2200:0);}
    FlashLights[2]->SetIntensity(BlastFlash*45000);
    int32 Width=1920,Height=1080;if(auto* PC=Cast<APlayerController>(P->GetController()))PC->GetViewportSize(Width,Height);
    const float Aspect=float(FMath::Max(Width,1))/FMath::Max(Height,1);
    const float BaseFOV=W->Flight->bBoost?95.f:90.f;
    const float ViewFOV=FMath::RadiansToDegrees(2*FMath::Atan(FMath::Tan(FMath::DegreesToRadians(BaseFOV*.5f))*FMath::Max(1.f,Aspect/(16.f/9.f))));
    P->FirstPersonCamera->SetFieldOfView(FMath::FInterpTo(P->FirstPersonCamera->FieldOfView,ViewFOV,Dt,3));
    if(!EngineAudio)if(auto* S=Sounds.Find(TEXT("EngineLoop"));S&&*S){EngineAudio=HeliosAudio::SpawnSound2D(this,*S,.20f,1,0,nullptr,false,false);}
    if(EngineAudio){EngineAudio->SetPitchMultiplier(.8f+Speed/6000.f);EngineAudio->SetVolumeMultiplier(.16f+Speed/16000.f);}
    for(int I=0;I<2;++I)
    {auto* C=Thrusters[I].Get();C->SetVisibility(true);C->SetWorldLocationAndRotation(P->GetActorLocation()+R.RotateVector(FVector(-240,I?128:-128,-10)),R);C->SetWorldScale3D(FVector(42+Speed*.025f,18,18)*(1+.05f*FMath::Sin(Clock*31)));}
    for(int I=0;I<2;++I)
    {auto* C=Thrusters[I+2].Get();C->SetVisibility(W->MissileRemaining<=0||I!=(W->MissileCount-1)%2);C->SetWorldLocationAndRotation(P->GetActorLocation()+R.RotateVector(FVector(90,I?170:-170,-70)),R);}
    for(int I=0;I<Beams.Num();++I)
    {
        BeamAges[I]+=Dt;const float A=BeamAges[I]/W->Settings->LaserVisualSeconds;
        Beams[I]->SetVisibility(A<1);Halos[I]->SetVisibility(A<1);
        for(int J=0;J<2;++J)if(auto* M=BeamMaterials[I*2+J].Get())M->SetScalarParameterValue(TEXT("Fade"),FMath::Clamp((1-A)*2,0.f,1.f));
    }
}
bool UHeliosFighterPresentation::EffectsAllowed() const
{
    const auto* W=Cast<AHeliosFighterWeapon>(GetOwner());if(!W||!W->bActive||GetWorld()->IsPaused())return false;
    if(!FParse::Param(FCommandLine::Get(),TEXT("RenderOffscreen"))&&GEngine&&GEngine->GameViewport&&GEngine->GameViewport->Viewport)
        return GEngine->GameViewport->Viewport->HasFocus();
    return true;
}
void UHeliosFighterPresentation::Effect(const TCHAR* Name,const FVector& At,const FVector& Dir,float Life,float Scale)
{
    if(!EffectsAllowed())return;auto* W=Cast<AHeliosFighterWeapon>(GetOwner());auto* P=W->Player();
    if(!P||FVector::DistSquared(P->GetActorLocation(),At)>FMath::Square(W->Settings->EffectCullDistance))return;
    // Trail particles yield the pool budget to impacts/explosions. No per-shot asset loads.
    if(Effects.Num()>=(FCString::Strcmp(Name,TEXT("Trail"))==0?150:192))return;
    if(auto* S=EffectSystems.Find(FName(Name));S&&*S)
        if(auto* E=UNiagaraFunctionLibrary::SpawnSystemAtLocation(this,*S,At,Dir.Rotation(),FVector(Scale),false,true,ENCPoolMethod::ManualRelease,false))
        {Effects.Add(E);EffectTimes.Add(Life);PeakEffectCount=FMath::Max(PeakEffectCount,Effects.Num());}
}
void UHeliosFighterPresentation::SoundAt(const TCHAR* Name,const FVector& At,float Volume,float Pitch)
{
    const auto* W=Cast<AHeliosFighterWeapon>(GetOwner());const auto* P=W?W->Player():nullptr;if(!P||!EffectsAllowed())return;
    const float Distance=FVector::Distance(P->GetActorLocation(),At);
    const float Gain=Volume/(1+FMath::Square(Distance/5000.f));
    if(Gain>.008f&&AttackAudio.Num()<24)if(auto* S=Sounds.Find(FName(Name));S&&*S)
        if(auto* A=HeliosAudio::SpawnSoundAtLocation(this,*S,At,FRotator::ZeroRotator,Gain,Pitch,0,nullptr,nullptr,false))AttackAudio.Add(A);
    if(Gain>.008f&&FParse::Param(FCommandLine::Get(),TEXT("VXRecord")))
    {
        FString Variant;FParse::Value(FCommandLine::Get(),TEXT("VXVariant="),Variant);
        const FString File=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/fighter")/Variant/TEXT("audio.csv"));
        FFileHelper::SaveStringToFile(FString::Printf(TEXT("%s,%.6f,%.4f,%.4f\n"),Name,GetWorld()->GetTimeSeconds(),Gain,Pitch),*File,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
    }
}
void UHeliosFighterPresentation::Pulse(const FVector& A,const FVector& B,const FHitResult& Hit,int32 Side)
{
    if(!Beams.Num()||!EffectsAllowed())return;auto* W=Cast<AHeliosFighterWeapon>(GetOwner());
    const int I=NextBeam++%Beams.Num();BeamAges[I]=0;const FVector D=B-A;
    for(int J=0;J<2;++J)
    {auto* C=J?Halos[I].Get():Beams[I].Get();const float Radius=J?W->Settings->LaserHaloRadius:W->Settings->LaserCoreRadius;C->SetWorldLocationAndRotation(A,D.Rotation());C->SetWorldScale3D(FVector(D.Size(),Radius,Radius));C->SetVisibility(true);if(BeamMaterials[I*2+J])BeamMaterials[I*2+J]->SetScalarParameterValue(TEXT("Fade"),1);}
    Effect(TEXT("Muzzle"),A,D.GetSafeNormal(),.15f);
    if(Hit.bBlockingHit)Effect(TEXT("Impact"),B+Hit.ImpactNormal*2,Hit.ImpactNormal,.6f);
    Flash=1;FlashSide=Side;Feedback=FMath::Max(Feedback,.22f);SoundAt(TEXT("LaserPunch"),A,.42f,Side?1.025f:.985f);
}
void UHeliosFighterPresentation::MissileLaunch(const FVector& At,const FVector& Dir)
{Effect(TEXT("Launch"),At,-Dir,.45f);SoundAt(TEXT("Ejection"),At,.6f);Feedback=FMath::Max(Feedback,.75f);}
void UHeliosFighterPresentation::MissileIgnition(const FVector& At,const FVector& Dir)
{Effect(TEXT("Ignition"),At,-Dir,.55f);SoundAt(TEXT("Ignition"),At,.85f);}
void UHeliosFighterPresentation::MissileTrail(const FVector& At,const FVector& Dir)
{Effect(TEXT("Trail"),At,-Dir,.9f);}
void UHeliosFighterPresentation::Detonation(const FVector& At,const FVector& Normal)
{
    auto* W=Cast<AHeliosFighterWeapon>(GetOwner());if(!W||!EffectsAllowed())return;
    const float Scale=W->Settings->MissileRadius/500.f;
    Effect(TEXT("Explosion"),At,FVector::UpVector,2.6f,Scale);
    FCollisionQueryParams Q(SCENE_QUERY_STAT(VXBlastGround),false,W);Q.AddIgnoredActor(W->Player());FHitResult Ground;
    if(GetWorld()->LineTraceSingleByChannel(Ground,At+FVector(0,0,30),At-FVector(0,0,150),ECC_WorldStatic,Q)&&Ground.ImpactNormal.Z>.55f)
        Effect(TEXT("Dust"),Ground.ImpactPoint+FVector(0,0,8),Ground.ImpactNormal,1.5f,Scale);
    SoundAt(TEXT("Blast"),At,1);
    if(auto* P=W->Player())Feedback=FMath::Max(Feedback,FMath::Clamp(1.f-float(FVector::Dist(P->GetActorLocation(),At))/7000.f,0.f,1.f));
    BlastFlash=1;if(FlashLights.Num()>2)FlashLights[2]->SetWorldLocation(At+Normal*60);
}
void AHeliosFighterWeapon::DrawFighterHUD(UCanvas* C)
{
    auto* P=Player();if(!C||!bActive||!P||!GEngine)return;
    const float S=FMath::Min(C->ClipX/1920.f,C->ClipY/1080.f),X=C->ClipX*.5f,Y=C->ClipY*.5f;
    const FLinearColor Cyan(.23f,.88f,1,.88f),Dim(.20f,.55f,.66f,.7f),Amber(1,.5f,.14f,.95f),White(.79f,.94f,1,.95f);
    auto Line=[&](float A,float B,float D,float E,FLinearColor Col,float Thick=1){FCanvasLineItem L(FVector2D(A,B),FVector2D(D,E));L.SetColor(Col);L.LineThickness=Thick*S;C->DrawItem(L);};
    auto Text=[&](FString T,float A,float B,FLinearColor Col,float Scale=1.f){FCanvasTextItem V(FVector2D(A,B),FText::FromString(T),GEngine->GetSmallFont(),Col);V.Scale=FVector2D(S*Scale);V.EnableShadow(FLinearColor(0,.015f,.025f,.8f));C->DrawItem(V);};
    auto Bar=[&](float A,float B,float Width,float Value,FLinearColor Col){Line(A,B,A+Width*S,B,Dim,4);Line(A,B,A+Width*S*FMath::Clamp(Value,0.f,1.f),B,Col,4);};
    for(int I=0;I<32;++I){const float A=I*UE_TWO_PI/32,B=(I+.65f)*UE_TWO_PI/32;Line(X+FMath::Cos(A)*21*S,Y+FMath::Sin(A)*21*S,X+FMath::Cos(B)*21*S,Y+FMath::Sin(B)*21*S,Cyan);}
    Line(X-7*S,Y,X+7*S,Y,Cyan);Line(X,Y-7*S,X,Y+7*S,Cyan);
    Line(X-90*S,Y,X-45*S,Y,Dim);Line(X+45*S,Y,X+90*S,Y,Dim);
    for(int I=-2;I<=2;++I)if(I){float DY=Y+I*55*S;Line(X-65*S,DY,X-22*S,DY,Dim);Line(X+22*S,DY,X+65*S,DY,Dim);}
    Text(FString::Printf(TEXT("%.0f"),P->GetVelocity().Size()/100),X-258*S,Y-15*S,White,1.45f);Text(TEXT("M/S"),X-258*S,Y+12*S,Dim);
    Text(Flight->Altitude<0?TEXT("---"):FString::Printf(TEXT("%.0f"),Flight->Altitude),X+220*S,Y-15*S,White,1.45f);Text(TEXT("ALT / M"),X+220*S,Y+12*S,Dim);
    Text(TEXT("VX - 01    /    FALCON"),X-103*S,90*S,White,1.1f);
    Text(Flight->bBoost?TEXT("BOOST"):TEXT("HOVER ASSIST"),X-55*S,115*S,Flight->bBoost?Amber:Dim);
    if(LockCandidate.IsValid())if(auto* PC=Cast<APlayerController>(P->GetController()))
    {
        FVector2D Screen;if(PC->ProjectWorldLocationToScreen(LockCandidate->GetActorLocation(),Screen))
        {
            const bool Locked=LockProgress>=Settings->LockSeconds;const auto Col=Locked?Amber:Cyan;const float R=34*S;
            for(int A:{-1,1})for(int B:{-1,1}){Line(Screen.X+A*R,Screen.Y+B*R,Screen.X+A*(R-12*S),Screen.Y+B*R,Col,2);Line(Screen.X+A*R,Screen.Y+B*R,Screen.X+A*R,Screen.Y+B*(R-12*S),Col,2);}
            Text(Locked?TEXT("LOCKED"):TEXT("ACQUIRING"),Screen.X-29*S,Screen.Y+R+8*S,Col);
        }
    }
    const float PanelY=C->ClipY*.84f;
    Text(TEXT("PULSE ARRAY"),X-435*S,PanelY,Dim);Text(CoolRemaining>0?TEXT("THERMAL LOCK"):TEXT("ARMED"),X-435*S,PanelY+22*S,CoolRemaining>0?Amber:Cyan);Bar(X-435*S,PanelY+48*S,148,Heat,CoolRemaining>0?Amber:Cyan);
    Text(TEXT("GUIDED MISSILE"),X+287*S,PanelY,Dim);Text(MissileRemaining>0?FString::Printf(TEXT("REARM  %.1f"),MissileRemaining):TEXT("READY / INF"),X+287*S,PanelY+22*S,MissileRemaining>0?Amber:Cyan);Bar(X+287*S,PanelY+48*S,148,1-MissileRemaining/Settings->MissileCooldown,Cyan);
    Text(FString::Printf(TEXT("HULL  %.0f%%"),P->GetHealth()/P->GetMaxHealth()*100),X-48*S,PanelY+11*S,White);Bar(X-55*S,PanelY+42*S,110,P->GetHealth()/P->GetMaxHealth(),Cyan);
    Text(TEXT("WASD  FLIGHT    SPACE / CTRL  ALTITUDE    SHIFT  TOGGLE BOOST    LMB  LASER    RMB  MISSILE"),X-341*S,C->ClipY-24*S,White,.9f);
    const FVector At=P->GetActorLocation();const FBox& B=Flight->FlightBounds;
    if(Flight->bBoundary||B.Max.Z-At.Z<500||B.Max.X-At.X<500||At.X-B.Min.X<500||B.Max.Y-At.Y<500||At.Y-B.Min.Y<500)Text(TEXT("FLIGHT ENVELOPE LIMIT"),X-98*S,Y-150*S,Amber,1.1f);
}
