#include "Weapons/HeliosCalamityStaff.h"
#include "Weapons/HeliosCalamityWind.h"
#include "Weapons/HeliosWeaponVFX.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Engine/World.h"

namespace {
FLinearColor ChannelColor(EHeliosCalamitySpell S)
{
    const FLinearColor Colors[]={FLinearColor(1,.46f,.06f),FLinearColor(1,.075f,.008f),FLinearColor(.05f,.65f,1),FLinearColor(.22f,1,.025f)};
    return Colors[static_cast<int32>(S)];
}
}
void UHeliosCalamityChannel::Initialize(UHeliosCalamityState* InState,USceneComponent* Parent,FVector Grip,FVector Tip)
{
    State=InState;GripPoint=Grip;TipPoint=Tip;AttachToComponent(Parent,FAttachmentTransformRules::KeepRelativeTransform);
    if(!IsRegistered())RegisterComponent();
    Material=UMaterialInstanceDynamic::Create(LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Helios/Weapons/Calamity/M_CS_Channel")),this);
    const auto MakeInstances=[this](const TCHAR* Asset,int32 Count)
    {
        auto* M=NewObject<UInstancedStaticMeshComponent>(GetOwner());M->SetupAttachment(this);M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,Asset));M->SetMaterial(0,Material);
        M->SetCollisionEnabled(ECollisionEnabled::NoCollision);M->SetCastShadow(false);M->SetCanEverAffectNavigation(false);M->SetCullDistance(7000);M->RegisterComponent();
        for(int32 I=0;I<Count;++I)M->AddInstance(FTransform(FQuat::Identity,FVector::ZeroVector,FVector(.001f)));
        return M;
    };
    Ions=MakeInstances(TEXT("/Engine/BasicShapes/Sphere.Sphere"),40);Streaks=MakeInstances(TEXT("/Engine/BasicShapes/Cube.Cube"),72);
    Light=NewObject<UPointLightComponent>(GetOwner());Light->SetupAttachment(this);Light->SetIntensity(0);Light->SetAttenuationRadius(240);Light->SetCastShadows(false);Light->RegisterComponent();
    Cancel();
}
void UHeliosCalamityChannel::Start(EHeliosCalamitySpell InSpell)
{
    Cancel();Spell=InSpell;Age=0;bCharging=true;Ions->SetVisibility(true);Streaks->SetVisibility(true);
    const auto* Wind=Spell==EHeliosCalamitySpell::Tornado?State->Settings->WindPresentation.Get():nullptr;
    UMaterialInterface* ChannelArt=nullptr;
    if(Wind)if(const auto* R=Wind->Resources.Find(TEXT("M_CW_Channel")))ChannelArt=Cast<UMaterialInterface>(R->Get());
    if(!ChannelArt)ChannelArt=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Helios/Weapons/Calamity/M_CS_Channel"));
    Material=UMaterialInstanceDynamic::Create(ChannelArt,this);Ions->SetMaterial(0,Material);Streaks->SetMaterial(0,Material);
    Material->SetVectorParameterValue(TEXT("SpellColor"),ChannelColor(Spell));Light->SetLightColor(ChannelColor(Spell));
    ChargeAudio=State->Sound(State->Settings->Spells[static_cast<int32>(Spell)].ChargeSound.LoadSynchronous(),this,TipPoint,.65f);
    const FString Path=FString::Printf(TEXT("/Game/Helios/Weapons/Calamity/NS_CS_Channel%d"),static_cast<int32>(Spell));
    UNiagaraSystem* ChannelFX=nullptr;
    if(Wind)if(const auto* R=Wind->Resources.Find(TEXT("NS_CW_Channel")))ChannelFX=Cast<UNiagaraSystem>(R->Get());
    Sparks=UHeliosWeaponVFX::SpawnAttachedTrail(ChannelFX?ChannelFX:LoadObject<UNiagaraSystem>(nullptr,*Path),this,NAME_None,TipPoint,FRotator::ZeroRotator);
    if(Sparks){Sparks->ComponentTags.AddUnique(TEXT("Calamity"));Sparks->ComponentTags.AddUnique(FName(*State->GetOwner()->GetName()));}
}
bool UHeliosCalamityChannel::IsReady() const{return bCharging&&State&&State->Settings&&Age>=State->Settings->CastWindupSeconds;}
void UHeliosCalamityChannel::Release(){bCharging=false;Age=0;State->Sound(State->Settings->Spells[static_cast<int32>(Spell)].ReleaseSound.LoadSynchronous(),this,TipPoint,.7f);}
void UHeliosCalamityChannel::Cancel()
{
    bCharging=false;Age=100;Power=0;if(ChargeAudio)ChargeAudio->Stop();ChargeAudio=nullptr;
    if(Sparks)UHeliosWeaponVFX::ReleaseTracked(Sparks);Sparks=nullptr;
    if(Ions)Ions->SetVisibility(false);if(Streaks)Streaks->SetVisibility(false);if(Light)Light->SetIntensity(0);
}
void UHeliosCalamityChannel::EndPlay(EEndPlayReason::Type Reason){Cancel();Super::EndPlay(Reason);}
void UHeliosCalamityChannel::Advance(float Dt)
{
    Age+=Dt;if(!State||!State->Settings)return;
    const float Progress=FMath::Clamp(Age/FMath::Max(.01f,State->Settings->CastWindupSeconds),0.f,1.f);
    Power=bCharging?(.15f+.85f*Progress):FMath::Exp(-Age*11);
    if(!bCharging&&Age>.55f){Power=0;Ions->SetVisibility(false);Streaks->SetVisibility(false);Light->SetIntensity(0);return;}
    const float Clock=GetWorld()->GetTimeSeconds();const FVector Axis=(TipPoint-GripPoint).GetSafeNormal();FVector U,V;Axis.FindBestAxisVectors(U,V);
    const auto Spiral=[&](float T,float Offset,float Radius){const float A=T*15+Clock*18+Offset;return FMath::Lerp(GripPoint,TipPoint,T)+(U*FMath::Cos(A)+V*FMath::Sin(A))*Radius;};
    for(int32 I=0;I<40;++I)
    {
        const float T=FMath::Frac(I*.618034f+Progress*.9f),A=I*2.39996f+Clock*5;
        const float R=bCharging?FMath::Lerp(17.f,3.f,Progress)*(1-T*.6f):5+Age*75;
        FVector P=bCharging?Spiral(T,A,R):TipPoint+(U*FMath::Cos(A)+V*FMath::Sin(A)+Axis*FMath::Sin(I*7.f))*(5+Age*110);
        Ions->UpdateInstanceTransform(I,FTransform(FQuat::Identity,P,FVector((.004f+(I%4)*.0015f)*Power)),false,I==39);
    }
    for(int32 I=0;I<72;++I)
    {
        FVector A,B;
        if(I<54){const int32 Ribbon=I/18,J=I%18;const float T=J/18.f;const float R=4.5f+Ribbon*2.7f;A=Spiral(T,Ribbon*2.094f,R);B=Spiral(T+.045f,Ribbon*2.094f,R);}
        else {const int32 J=(I-54)/3,K=(I-54)%3;const float Angle=J*PI/3+Clock*3;const FVector End=TipPoint+(U*FMath::Cos(Angle)+V*FMath::Sin(Angle))*18+Axis*FMath::Sin(Angle*3)*9;A=FMath::Lerp(TipPoint,End,K/3.f);B=FMath::Lerp(TipPoint,End,(K+1)/3.f)+U*FMath::Sin(Clock*31+I)*2;}
        const FVector D=B-A;const float Thickness=(I<54?.10f:.17f)*Power;
        Streaks->UpdateInstanceTransform(I,FTransform(D.Rotation(),(A+B)*.5f,FVector(D.Size()/100,Thickness/100,Thickness/100)),false,I==71);
    }
    Material->SetScalarParameterValue(TEXT("Opacity"),Power*.8f);Material->SetScalarParameterValue(TEXT("Charge"),Power);
    Light->SetRelativeLocation(TipPoint);Light->SetIntensity((bCharging?1600:2400)*Power);
}
