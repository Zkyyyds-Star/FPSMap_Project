#include "Weapons/HeliosQingxiao.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/Pawn.h"

namespace
{
struct FQXLayer
{
    TArray<FVector> V,N;
    TArray<int32> T;
    TArray<FVector2D> UV;
    TArray<FLinearColor> C;
    TArray<FProcMeshTangent> Tangents;
    void Pair(const FVector& P,const FVector& Side,float Width,float U,FLinearColor Color,bool Connect)
    {
        const int32 K=V.Num();V.Add(P-Side*Width);V.Add(P+Side*Width);
        N.Add(FVector::UpVector);N.Add(FVector::UpVector);
        UV.Add(FVector2D(U,0));UV.Add(FVector2D(U,1));C.Add(Color);C.Add(Color);
        if(Connect)T.Append({K-2,K-1,K,K,K-1,K+1});
    }
    void Blade(const FVector& P,const FVector& Axis,const FVector& Side,float Length,float Width,FLinearColor Color)
    {
        Pair(P-Axis*Length*.35f,Side,0,0,Color,false);
        Pair(P,Side,Width,.35f,Color,true);
        Pair(P+Axis*Length*.65f,Side,0,1,Color,true);
    }
    void Submit(UProceduralMeshComponent* Mesh,int32 Index)
    { Mesh->CreateMeshSection_LinearColor(Index,V,T,N,UV,C,Tangents,false); }
};
}

void UHeliosQingxiaoShakePattern::GetShakePatternInfoImpl(FCameraShakeInfo& Info) const
{Info.Duration=FCameraShakeDuration(.12f);Info.BlendIn=0;Info.BlendOut=.05f;}
void UHeliosQingxiaoShakePattern::StartShakePatternImpl(const FCameraShakePatternStartParams&)
{Elapsed=0;}
void UHeliosQingxiaoShakePattern::UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& P,FCameraShakePatternUpdateResult& R)
{
    Elapsed+=P.DeltaTime;
    const float E=FMath::Square(FMath::Clamp(1-Elapsed/.12f,0.f,1.f));
    R.Location=FVector(-.55f,Direction*.22f,.10f)*FMath::Cos(Elapsed*55)*E;
    R.Rotation=FRotator(-.20f,Direction*.15f,Direction*.22f)*FMath::Cos(Elapsed*55)*E;
}
UHeliosQingxiaoShake::UHeliosQingxiaoShake():Super(FObjectInitializer::Get())
{bSingleInstance=true;SetRootShakePattern(CreateDefaultSubobject<UHeliosQingxiaoShakePattern>(TEXT("Sword contact")));}

AHeliosQingxiaoBurst::AHeliosQingxiaoBurst()
{
    PrimaryActorTick.bCanEverTick=true;
    Mesh=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Surface sword burst"));SetRootComponent(Mesh);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetGenerateOverlapEvents(false);
    Mesh->SetCanEverAffectNavigation(false);Mesh->SetCastShadow(false);
    InitialLifeSpan=.65f;
}
void AHeliosQingxiaoBurst::Initialize(const FVector& Point,const FVector& Normal,const FVector& CutDirection,EQingxiaoBurst Kind,float Strength,int32 Variation)
{
    Style=Kind;Scale=FMath::Clamp(Strength,.1f,3.f);Seed=Variation;
    if(Style==EQingxiaoBurst::Wall)
        if(auto* P=Cast<APawn>(GetOwner()))
        {
            const float Distance=FVector::Distance(Point,P->GetPawnViewLocation());
            Scale*=FMath::Clamp(Distance/450.f,.20f,1.f);
            Opacity=FMath::Lerp(.5f,1.f,FMath::Clamp(Distance/450.f,0.f,1.f));
        }
    Facing=Normal.GetSafeNormal(SMALL_NUMBER,FVector::UpVector);
    Right=FVector::VectorPlaneProject(CutDirection,Facing).GetSafeNormal();
    if(Right.IsNearlyZero())Right=FVector::CrossProduct(Facing,FMath::Abs(Facing.Z)>.95f?FVector::RightVector:FVector::UpVector).GetSafeNormal();
    Up=FVector::CrossProduct(Facing,Right).GetSafeNormal();
    Anchor=Point+Facing*2.5f;SetActorLocation(Anchor);
    if(Follow.IsValid())FollowStart=Follow->GetActorLocation();
    Mesh->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Helios/Weapons/Qingxiao/M_QX_BurstBody")));
    Mesh->SetMaterial(1,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Helios/Weapons/Qingxiao/M_QX_BurstGlow")));
    Tick(0);
}
void AHeliosQingxiaoBurst::Tick(float Dt)
{
    Super::Tick(Dt);Age+=Dt;
    const bool Wall=Style==EQingxiaoBurst::Wall,Launch=Style==EQingxiaoBurst::Launch;
    const float Lifetime=Wall?.55f:Launch?.24f:.30f;
    if(Age>=Lifetime){Destroy();return;}
    if(Follow.IsValid())SetActorLocation(Anchor+Follow->GetActorLocation()-FollowStart);
    FQXLayer Body,Glow;
    const float P=Age/Lifetime,Fade=FMath::Square(1-P)*Opacity;
    // Non-additive blue body preserves the shape against a bright sky or pale wall.
    const FLinearColor Cyan(.04f,.45f,.8f,Fade*.8f),Gold(1.f,.40f,.025f,Fade),White(.55f,.85f,1.f,Fade);
    const float Flash=FMath::Clamp(1-Age/(Wall?.065f:.045f),0.f,1.f)*Opacity;
    if(Flash>0)
    {
        Glow.Blade(Facing*3,Right,Up,(Wall?115.f:Launch?60.f:82.f)*Scale,(Wall?13.f:7.f)*Scale,FLinearColor(1.f,.6f,.16f,Flash));
        Glow.Blade(Facing*4,Up,Right,(Wall?72.f:35.f)*Scale,4*Scale,FLinearColor(.55f,.85f,1.f,Flash));
    }
    const float RingTime=Wall?.25f:Launch?.20f:.16f;
    if(Age<RingTime)
    {
        const float Q=Age/RingTime;
        const float Radius=Scale*(Launch?FMath::Lerp(42.f,9.f,Q):FMath::Lerp(10.f,Wall?115.f:53.f,1-FMath::Square(1-Q)));
        for(int I=0;I<=56;++I)
        {
            const float A=I*2*PI/56;const FVector Radial=Right*FMath::Cos(A)+Up*FMath::Sin(A);
            Glow.Pair(Radial*Radius+Facing*4,Radial,(1-Q)*2.3f*Scale,I/56.f,FLinearColor(.07f,.55f,.9f,(1-Q)*.65f*Opacity),I>0);
        }
    }
    if(!Launch)
    {
        const float CutGrow=FMath::Clamp((Age+.016f)/.055f,0.f,1.f);
        Body.Blade(Facing*3,Right,Up,(Wall?120.f:95.f)*Scale*CutGrow,6*Scale,FLinearColor(.07f,.40f,.62f,Fade*.75f));
        Glow.Blade(Facing*4,Right,Up,(Wall?122.f:97.f)*Scale*CutGrow,1.4f*Scale,White);
    }
    const int Count=Wall?22:Launch?8:11;
    for(int I=0;I<Count;++I)
    {
        const float A=I*2.399963f+Seed*.61f;
        const FVector Radial=Right*FMath::Cos(A)+Up*FMath::Sin(A);
        const float Speed=(Wall?155.f:95.f)+(I%5)*45.f;
        // World impacts send shards exclusively into the outward hemisphere.
        const FVector Travel=(Radial*Speed+Facing*(Wall?55.f+(I%4)*25.f:12.f))*Age*Scale;
        const FVector Axis=(Radial+Facing*.3f).GetSafeNormal();
        const FVector Side=FVector::CrossProduct(Axis,Facing).GetSafeNormal(SMALL_NUMBER,Up);
        const float Length=(Wall?24.f:12.f)+(I%4)*6.f;
        Body.Blade(Travel,Axis,Side,Length*Scale*(1-P*.6f),2.5f*Scale*(1-P),Cyan);
        Glow.Blade(Travel+Facing*.2f,Axis,Side,Length*Scale, .8f*Scale,I%3==0?Gold:White);
    }
    Body.Submit(Mesh,0);Glow.Submit(Mesh,1);
}
