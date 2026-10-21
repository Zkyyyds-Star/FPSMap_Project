#include "Weapons/HeliosCalamityWind.h"
#include "Weapons/HeliosCalamityStaff.h"
#include "Weapons/HeliosWeaponVFX.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

namespace
{
template<class T> T* Resource(const UHeliosCalamityWindDefinition* P,const TCHAR* Name)
{
    const auto* Value=P->Resources.Find(FName(Name));
    return Value?Cast<T>(Value->Get()):nullptr;
}
}

bool UHeliosCalamityWindDefinition::IsReady() const
{
    for(const TCHAR* Name:{TEXT("SM_CW_Body"),TEXT("SM_CW_Blade"),TEXT("SM_CW_Cut"),TEXT("SM_CW_Surface"),TEXT("SM_CW_Dust"),TEXT("SM_CW_Shard")})
        if(!Resource<UStaticMesh>(this,Name))return false;
    for(const TCHAR* Name:{TEXT("M_CW_Body"),TEXT("M_CW_Blade"),TEXT("M_CW_Cut"),TEXT("M_CW_Ground"),TEXT("M_CW_Wave"),TEXT("M_CW_Dust"),TEXT("M_CW_Stone")})
        if(!Resource<UMaterialInterface>(this,Name))return false;
    return true;
}

void UHeliosCalamityWindDefinition::Prewarm()
{
#if WITH_EDITOR
    FAssetCompilingManager::Get().FinishAllCompilation();
    for(const auto& Pair:Resources)if(auto* M=Cast<UMaterialInterface>(Pair.Value))M->EnsureIsComplete();
    if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
#endif
}

UStaticMeshComponent* UHeliosCalamityWindVisual::Layer(const TCHAR* Mesh,const TCHAR* Material,int32 Sort)
{
    auto* M=NewObject<UStaticMeshComponent>(GetOwner());M->SetupAttachment(this);
    M->SetStaticMesh(Resource<UStaticMesh>(Profile,Mesh));M->SetMaterial(0,Resource<UMaterialInterface>(Profile,Material));
    M->SetCollisionEnabled(ECollisionEnabled::NoCollision);M->SetGenerateOverlapEvents(false);
    M->SetCanEverAffectNavigation(false);M->SetCastShadow(false);M->SetCullDistance(10000);
    M->SetTranslucentSortPriority(Sort);M->RegisterComponent();M->SetBoundsScale(1.3f);
    if(auto* D=M->CreateDynamicMaterialInstance(0))
    {
        D->SetVectorParameterValue(TEXT("Tint"),Profile->Tint);
        D->SetScalarParameterValue(TEXT("EmissiveGain"),Profile->EmissiveGain);
        D->SetScalarParameterValue(TEXT("Opacity"),0);
    }
    Layers.Add(M);return M;
}

void UHeliosCalamityWindVisual::Initialize(AHeliosCalamityEffect* InField,UHeliosCalamityWindDefinition* InProfile)
{
    Field=InField;Profile=InProfile;
    PhaseOffset=FMath::Frac(FMath::Abs(Field->GetActorLocation().X*.0017f+Field->GetActorLocation().Y*.0031f+Field->GetUniqueID()*.13f))*7;
    Body=Layer(TEXT("SM_CW_Body"),TEXT("M_CW_Body"));
    InnerBody=Layer(TEXT("SM_CW_Body"),TEXT("M_CW_Body"));
    for(int32 I=0;I<3;++I)Blades.Add(Layer(TEXT("SM_CW_Blade"),TEXT("M_CW_Blade"),1));
    for(int32 I=0;I<6;++I)Cuts.Add(Layer(TEXT("SM_CW_Cut"),TEXT("M_CW_Cut"),1));
    Dust=Layer(TEXT("SM_CW_Dust"),TEXT("M_CW_Dust"));
    Ground=Layer(TEXT("SM_CW_Surface"),TEXT("M_CW_Ground"));
    Wave=Layer(TEXT("SM_CW_Surface"),TEXT("M_CW_Wave"),2);
    Ground->SetRelativeScale3D(FVector(Field->Radius/100,Field->Radius/100,1));
    Ground->SetRelativeLocation(FVector(0,0,2));
    Wave->SetRelativeLocation(FVector(0,0,7));
    // Only surface layers follow slopes; the vortex axis remains vertical.
    FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(WindSurface),false,Field->GetOwner());Q.AddIgnoredActor(Field);
    if(GetWorld()->LineTraceSingleByChannel(Hit,Field->GetActorLocation()+FVector(0,0,30),Field->GetActorLocation()-FVector(0,0,100),ECC_Visibility,Q)&&Hit.ImpactNormal.Z>.55f)
    {
        const FRotator Slope=FRotationMatrix::MakeFromZ(Hit.ImpactNormal).Rotator();
        Ground->SetRelativeRotation(Slope);Wave->SetRelativeRotation(Slope);
    }
    Debris=NewObject<UInstancedStaticMeshComponent>(GetOwner());Debris->SetupAttachment(this);
    Debris->SetStaticMesh(Resource<UStaticMesh>(Profile,TEXT("SM_CW_Shard")));
    Debris->SetMaterial(0,Resource<UMaterialInterface>(Profile,TEXT("M_CW_Stone")));
    Debris->SetCollisionEnabled(ECollisionEnabled::NoCollision);Debris->SetCanEverAffectNavigation(false);
    Debris->SetCastShadow(false);Debris->SetCullDistance(6000);Debris->RegisterComponent();
    for(int32 I=0;I<32;++I)Debris->AddInstance(FTransform(FQuat::Identity,FVector::ZeroVector,FVector::ZeroVector));
    Advance(0,1,0);
}

void UHeliosCalamityWindVisual::PlayActivation()
{
    Field->State->Shake(Field->GetActorLocation(),.3f);
    Motes=UHeliosWeaponVFX::SpawnAttachedTrail(Resource<UNiagaraSystem>(Profile,TEXT("NS_CW_Motes")),this,NAME_None,FVector(0,0,90),FRotator::ZeroRotator);
    if(Motes)
    {
        Motes->ComponentTags.AddUnique(TEXT("Calamity"));Motes->ComponentTags.AddUnique(FName(*Field->GetOwner()->GetName()));
        Motes->SetRelativeScale3D(FVector(Field->Radius/1000,Field->Radius/1000,1));
    }
}

float UHeliosCalamityWindVisual::GetGroundRadius() const{return Ground?Ground->GetRelativeScale3D().X*100:0;}

void UHeliosCalamityWindVisual::Advance(float Formation,float Fade,float ActiveAge)
{
    const float T=Field->FieldAge+PhaseOffset;
    const bool Tail=Field->Phase==EHeliosCalamityPhase::Dissipating;
    const float Dissolve=Tail?1-Fade:0;
    const float Rise=FMath::SmoothStep(0.f,1.f,Formation);
    const float XY=Field->Radius/1000.f,Z=Profile->Height/720.f;
    const float Distance=FVector::Distance(Field->GetOwner()->GetActorLocation(),Field->GetActorLocation());
    for(auto M:Layers)
        if(auto* D=Cast<UMaterialInstanceDynamic>(M->GetMaterial(0)))
        {
            D->SetScalarParameterValue(TEXT("EffectAge"),T);
            D->SetScalarParameterValue(TEXT("Formation"),Rise);
            D->SetScalarParameterValue(TEXT("Dissolve"),Dissolve);
            D->SetScalarParameterValue(TEXT("Opacity"),Fade);
            D->SetScalarParameterValue(TEXT("Active"),Field->Phase==EHeliosCalamityPhase::Active?1:0);
        }
    Body->SetRelativeScale3D(FVector(XY*(.6f+.4f*Rise),XY*(.6f+.4f*Rise),Z));
    Body->SetRelativeRotation(FRotator(0,T*85,0));
    if(auto* D=Cast<UMaterialInstanceDynamic>(Body->GetMaterial(0)))D->SetScalarParameterValue(TEXT("Opacity"),Fade*Profile->Density*Rise);
    InnerBody->SetRelativeScale3D(FVector(XY*.67f,XY*.67f,Z*.95f));
    InnerBody->SetRelativeRotation(FRotator(0,-T*110+67,0));
    if(auto* D=Cast<UMaterialInstanceDynamic>(InnerBody->GetMaterial(0)))D->SetScalarParameterValue(TEXT("Opacity"),Fade*Profile->Density*Rise*.72f);
    const float CuttingPulse=Field->Phase==EHeliosCalamityPhase::Active?FMath::Exp(-FMath::Frac(ActiveAge*5)*10):0;
    for(int32 I=0;I<3;++I)
    {
        auto* B=Blades[I].Get();const float W=1-I*.085f;
        B->SetRelativeScale3D(FVector(XY*W,XY*W,Z*(1-I*.045f)));
        B->SetRelativeRotation(FRotator(0,T*(240+I*43)+I*120,0));
        B->SetRelativeLocation(FVector(0,0,Dissolve*(95+I*25)));
        B->SetVisibility(I==0||Distance<6000);
        if(auto* D=Cast<UMaterialInstanceDynamic>(B->GetMaterial(0)))
        {D->SetScalarParameterValue(TEXT("Seed"),I*2.17f);D->SetScalarParameterValue(TEXT("Opacity"),Fade*Rise*(.85f-I*.1f));D->SetScalarParameterValue(TEXT("EmissiveGain"),Profile->EmissiveGain*(1+CuttingPulse*.28f));}
    }
    for(int32 I=0;I<Cuts.Num();++I)
    {
        auto* Cut=Cuts[I].Get();const float Cycle=FMath::Frac(ActiveAge*(1.8f+I*.12f)+I/6.f);
        const float Spread=(.45f+Cycle*.7f)*(1+I*.07f)*XY;
        Cut->SetRelativeScale3D(FVector(Spread,Spread,1));
        Cut->SetRelativeLocation(FVector(0,0,(65+I*86+Cycle*45+Dissolve*140)*Z));
        Cut->SetRelativeRotation(FRotator(12*FMath::Sin(T*3+I)+I*2,T*(390+I*31)+I*61,9*FMath::Cos(T*4+I)));
        const float Slash=FMath::SmoothStep(0.f,.09f,Cycle)*FMath::Pow(1-Cycle,1.4f);
        Cut->SetVisibility(Field->Phase!=EHeliosCalamityPhase::Telegraph);
        if(auto* D=Cast<UMaterialInstanceDynamic>(Cut->GetMaterial(0)))D->SetScalarParameterValue(TEXT("Opacity"),Slash*.78f*Fade);
    }
    Dust->SetRelativeScale3D(FVector(XY,XY,1));Dust->SetRelativeRotation(Ground->GetRelativeRotation());Dust->AddLocalRotation(FRotator(0,T*49,0));
    Dust->SetVisibility(Distance<6500);
    if(auto* D=Cast<UMaterialInstanceDynamic>(Dust->GetMaterial(0)))D->SetScalarParameterValue(TEXT("Opacity"),Fade*(.2f+.8f*Rise));
    if(auto* D=Cast<UMaterialInstanceDynamic>(Ground->GetMaterial(0)))D->SetScalarParameterValue(TEXT("Opacity"),Fade*(.12f+.88f*Rise));
    const float WaveProgress=ActiveAge<.42f?ActiveAge/.42f:FMath::Frac((ActiveAge-.42f)*1.6f);
    Wave->SetRelativeScale3D(FVector(Field->Radius/100,Field->Radius/100,1));
    Wave->SetVisibility(Field->Phase==EHeliosCalamityPhase::Active);
    if(auto* D=Cast<UMaterialInstanceDynamic>(Wave->GetMaterial(0)))
    {D->SetScalarParameterValue(TEXT("WaveRadius"),WaveProgress);D->SetScalarParameterValue(TEXT("Opacity"),(1-WaveProgress)*(ActiveAge<.42f?.95f:.28f));}
    const int32 VisibleCount=Distance>6000?0:Distance>3000?16:32;
    for(int32 I=0;I<32;++I)
    {
        const float Seed=I*.618034f;
        // Tail freezes birth phase so fragments leave upward without respawning at the base.
        const float Cycle=FMath::Frac(ActiveAge*.46f+Seed);
        const float A=I*2.39996f+T*(5.6f+I%4*.33f);
        const float R=Field->Phase==EHeliosCalamityPhase::Telegraph?FMath::Lerp(800.f,135.f,Rise):95+Cycle*Cycle*340;
        const float H=Field->Phase==EHeliosCalamityPhase::Telegraph?5+Rise*12:18+Cycle*660+Dissolve*175;
        const float S=(.9f+(I%5)*.34f)*Fade*FMath::Min(1.f,Formation*4)*((I<VisibleCount)?1:0);
        Debris->UpdateInstanceTransform(I,FTransform(FRotator(T*120+I*13,T*75+I*29,T*93),FVector(FMath::Cos(A)*R*XY,FMath::Sin(A)*R*XY,H*Z),FVector(S)),false,I==31);
    }
    if(Motes)
    {
        Motes->SetVisibility(Distance<4500);
        if(Tail&&!bStoppedMotes){Motes->Deactivate();bStoppedMotes=true;}
    }
    Field->Light->SetLightColor(Profile->Tint);
    Field->Light->SetIntensity((650+3600*FMath::Exp(-ActiveAge*11)+CuttingPulse*450)*Rise*Fade*Profile->EmissiveGain);
}

void UHeliosCalamityWindVisual::EndPlay(EEndPlayReason::Type Reason)
{
    if(Motes)UHeliosWeaponVFX::ReleaseTracked(Motes);Motes=nullptr;
    Super::EndPlay(Reason);
}
