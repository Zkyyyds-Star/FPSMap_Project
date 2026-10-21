#include "Weapons/HeliosLightstickWave.h"
#include "Weapons/HeliosLightstickWeapon.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundBase.h"

namespace {
void Cosmetic(UPrimitiveComponent* C){C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetGenerateOverlapEvents(false);C->SetCastShadow(false);C->SetCanEverAffectNavigation(false);C->bReceivesDecals=false;}
// Capsule contact interval, including high/low targets; no artificial wave sweep radius.
void ContactInterval(AActor* A,const FVector& Center,const FVector& Origin,FVector& Contact,float& Near,float& Far)
{
    float Radius=20,HalfSegment=0;FVector Up=FVector::UpVector;
    if(auto* C=A->FindComponentByClass<UCapsuleComponent>()){Radius=C->GetScaledCapsuleRadius();HalfSegment=FMath::Max(0.f,C->GetScaledCapsuleHalfHeight()-Radius);Up=C->GetUpVector();}
    else if(auto* S=A->FindComponentByClass<USphereComponent>())Radius=S->GetScaledSphereRadius();
    const FVector P=Center-Up*HalfSegment,Q=Center+Up*HalfSegment;
    const FVector Axis=FMath::ClosestPointOnSegment(Origin,P,Q);const FVector Delta=Axis-Origin;
    Near=FMath::Max(0.f,float(Delta.Size())-Radius);Far=FMath::Max(FVector::Dist(Origin,P),FVector::Dist(Origin,Q))+Radius;
    Contact=Origin+Delta.GetSafeNormal()*Near;
}
}
AHeliosLightstickWave::AHeliosLightstickWave()
{
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostPhysics;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Fixed wave origin"));
    WaveSurface=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Continuous expanding blade"));WaveSurface->SetupAttachment(RootComponent);Cosmetic(WaveSurface);
    WaveSurface->bUseAsyncCooking=false;
}
void AHeliosLightstickWave::GatherTargets(TArray<AActor*>& Out) const
{
    FCollisionObjectQueryParams Types;Types.AddObjectTypesToQuery(ECC_Pawn);
    FCollisionQueryParams Q(SCENE_QUERY_STAT(EncoreSectorCandidates),false,this);Q.AddIgnoredActor(GetOwner());Q.AddIgnoredActor(GetInstigator());
    TArray<FOverlapResult> Hits;GetWorld()->OverlapMultiByObjectType(Hits,GetActorLocation(),FQuat::Identity,Types,FCollisionShape::MakeSphere(FMath::Max(1.f,Settings->WaveRange)),Q);
    for(const auto& H:Hits)if(auto* A=Cast<APawn>(H.GetActor()))if(A->CanBeDamaged())Out.AddUnique(A);
}
bool AHeliosLightstickWave::FindWall(const FVector& End,FHitResult& Hit,AActor* Target,bool BladeHeight) const
{
    FCollisionObjectQueryParams Types;Types.AddObjectTypesToQuery(ECC_WorldStatic);Types.AddObjectTypesToQuery(ECC_WorldDynamic);
    FCollisionQueryParams Q(SCENE_QUERY_STAT(EncoreSectorOcclusion),false,this);Q.AddIgnoredActor(GetOwner());Q.AddIgnoredActor(GetInstigator());if(Target)Q.AddIgnoredActor(Target);
    TArray<FHitResult> Hits;GetWorld()->LineTraceMultiByObjectType(Hits,GetActorLocation()+FVector(0,0,BladeHeight?Settings->WaveHeightOffset:0),End,Types,Q);bool Found=false;float Nearest=MAX_flt;
    for(const auto& H:Hits)
    {
        if(!H.GetActor()||Cast<APawn>(H.GetActor())||!H.GetComponent()||H.GetComponent()->GetCollisionResponseToChannel(ECC_Visibility)!=ECR_Block)continue;
        if(H.Distance<Nearest){Hit=H;Nearest=H.Distance;Found=true;}
    }
    return Found;
}
void AHeliosLightstickWave::Initialize(UHeliosLightstickDefinition* D,float Charge,const FVector& Eye)
{
    Settings=D;SetActorLocation(Eye);SetActorRotation(FRotator(0,GetActorRotation().Yaw,0));
    Damage=FMath::Lerp(D->HeavyMinimumDamage,D->HeavyMaximumDamage,Charge);
    ReleasedCharge=Charge;Color=Charge>=.98f?FLinearColor(1.f,.72f,.32f):FLinearColor(.04f,.55f,1.f);
    SegmentCount=FMath::Clamp(D->WaveSegments,32,256);BlockedRadii.Init(MAX_flt,SegmentCount);
    auto* Mat=D->WaveMaterial.LoadSynchronous();
    for(int32 Layer=0;Layer<5;++Layer)
    {
        auto* M=UMaterialInstanceDynamic::Create(Layer==0?D->WaveCoreMaterial.LoadSynchronous():Mat,this);WaveSurface->SetMaterial(Layer,M);
        M->SetVectorParameterValue(TEXT("GlowColor"),Layer==0?(Charge>=.98f?FLinearColor(1.5f,1.2f,.7f):FLinearColor(.7f,1.2f,1.5f)):Layer==2?FLinearColor(.45f,.08f,.8f):Color);
        M->SetScalarParameterValue(TEXT("Energy"),Layer==0?1.4f:.65f+Charge*.15f);
        const float Opacity[]={.65f,D->WaveBodyOpacity,D->WaveEdgeOpacity,FMath::Min(.08f,D->WaveGhostOpacity),FMath::Min(.08f,D->WaveGhostOpacity)*.65f};M->SetScalarParameterValue(TEXT("Opacity"),Opacity[Layer]);Materials.Add(M);
    }
    ArcVertices.SetNum((SegmentCount+1)*4);ArcNormals.SetNum(ArcVertices.Num());ArcUV.SetNum(ArcVertices.Num());ArcColors.SetNum(ArcVertices.Num());
    for(int32 I=0;I<SegmentCount;++I)for(int32 J=0;J<4;++J)
    {
        int32 A=I*4+J,B=(I+1)*4+J,C=(I+1)*4+(J+1)%4,E=I*4+(J+1)%4;
        ArcTriangles.Append({A,B,C,A,C,E});
    }
    TArray<AActor*> Targets;GatherTargets(Targets);for(auto* A:Targets)PreviousCenters.Add(A,A->GetActorLocation());
    UpdateWaveMesh(0,true);
}
void AHeliosLightstickWave::AdvanceDamage(float Old,float Now)
{
    TArray<AActor*> Targets;GatherTargets(Targets);
    const FVector Origin=GetActorLocation(),Forward=GetActorForwardVector();
    const float CosHalf=FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(Settings->WaveAngleDegrees,1.f,180.f)*.5f));
    for(auto* A:Targets)
    {
        const FVector Center=A->GetActorLocation();const FVector Previous=PreviousCenters.Contains(A)?PreviousCenters[A]:Center;PreviousCenters.Add(A,Center);
        if(Damaged.Contains(A)||OccludedTargets.Contains(A))continue;
        FVector Contact,OldContact;float Near,Far,OldNear,OldFar;
        ContactInterval(A,Center,Origin,Contact,Near,Far);ContactInterval(A,Previous,Origin,OldContact,OldNear,OldFar);
        // The entire radius interval is tested, so a long frame never leaps over a target.
        const bool InShell=Near<=Now&&Far>=Old;
        const bool CrossedMovingFront=OldNear>Old&&Near<=Now;
        if((!InShell&&!CrossedMovingFront)||Near>Settings->WaveRange)continue;
        const FVector Flat=(Center-Origin).GetSafeNormal2D();
        if(!Flat.IsNearlyZero()&&FVector::DotProduct(Flat,Forward)<CosHalf-1.e-6f)continue;
        FHitResult Wall;
        if(FindWall(Contact,Wall,A)){OccludedTargets.Add(A);continue;}
        Damaged.Add(A);
        const FVector Direction=(Contact-Origin).GetSafeNormal(UE_SMALL_NUMBER,Forward);
        FHitResult H(A,Cast<UPrimitiveComponent>(A->GetRootComponent()),Contact,-Direction);H.bBlockingHit=true;H.Location=Contact;H.ImpactPoint=Contact;H.ImpactNormal=-Direction;
        UGameplayStatics::ApplyPointDamage(A,Damage,Direction,H,GetInstigatorController(),GetOwner(),UDamageType::StaticClass());Burst(H,false);
    }
}
void AHeliosLightstickWave::UpdateWaveMesh(float Radius,bool Create)
{
    const float Half=FMath::DegreesToRadians(FMath::Clamp(Settings->WaveAngleDegrees,1.f,180.f)*.5f);
    // These probes only clip the drawing. Damage uses exact per-target visibility above.
    if(!Create)for(int32 I=0;I<SegmentCount;++I)
    {
        if(BlockedRadii[I]!=MAX_flt)continue;
        float A=FMath::Lerp(-Half,Half,(I+.5f)/SegmentCount);FVector Dir(FMath::Cos(A),FMath::Sin(A),0);
        FVector Local=Dir*FMath::Sqrt(FMath::Max(0.f,Radius*Radius-FMath::Square(Settings->WaveHeightOffset)))+FVector(0,0,Settings->WaveHeightOffset);FHitResult H;
        if(FindWall(GetActorTransform().TransformPosition(Local),H,nullptr,true))
        {
            BlockedRadii[I]=FVector::Dist(GetActorLocation(),H.ImpactPoint);
            Burst(H,true);
        }
    }
    const float Heights[]={.025f,.32f,.43f,.28f,.20f},Widths[]={3.f,12.f,22.f,10.f,8.f},Offsets[]={0.f,0.f,0.f,150.f,330.f};
    const float LifetimeAlpha=FMath::Clamp((Settings->WaveRange-Radius)/250.f,0.f,1.f);
    for(int32 L=0;L<5;++L)
    {
        const float R=FMath::Max(0.f,Radius-Offsets[L]);
        for(int32 I=0;I<=SegmentCount;++I)
        {
            const float T=float(I)/SegmentCount,A=FMath::Lerp(-Half,Half,T);
            // Flat waist/chest-level crest; never lift the blade as its radius grows.
            const FVector Dir(FMath::Cos(A),FMath::Sin(A),0);
            const float Profile=.15f+.85f*FMath::Sin(T*PI);const float H=Settings->WaveBladeThickness*Heights[L]*Profile;
            const float Z=Settings->WaveHeightOffset;
            const float HorizontalRadius=FMath::Sqrt(FMath::Max(0.f,R*R-Z*Z));
            float Visible=R>1?LifetimeAlpha:0;
            const float Block=FMath::Min(BlockedRadii[FMath::Clamp(I-1,0,SegmentCount-1)],BlockedRadii[FMath::Min(I,SegmentCount-1)]);
            if(Block<MAX_flt)Visible*=FMath::Clamp((Block-R)/25.f,0.f,1.f);
            for(int32 J=0;J<4;++J)
            {
                const bool Outer=J==1||J==2,Top=J>=2;int32 K=I*4+J;
                ArcVertices[K]=Dir*FMath::Max(0.f,HorizontalRadius+(Outer?1.f:-1.f)*Widths[L])+FVector(0,0,Z+(Top?H:-H));
                ArcNormals[K]=Dir;ArcUV[K]=FVector2D(T,Top?1:0);ArcColors[K]=FLinearColor(1,1,1,Visible);
            }
        }
        if(Create)WaveSurface->CreateMeshSection_LinearColor(L,ArcVertices,ArcTriangles,ArcNormals,ArcUV,ArcColors,TArray<FProcMeshTangent>(),false,false);
        else WaveSurface->UpdateMeshSection_LinearColor(L,ArcVertices,ArcNormals,ArcUV,ArcColors,TArray<FProcMeshTangent>(),false);
    }
}
void AHeliosLightstickWave::Burst(const FHitResult& Hit,bool Wall)
{
    if(auto* Weapon=Cast<AHeliosLightstickWeapon>(GetOwner()))
    {
        const FVector Radial=(Hit.ImpactPoint-GetActorLocation()).GetSafeNormal2D();
        Weapon->PlaySlashImpact(Hit,FVector::CrossProduct(FVector::UpVector,Radial),Wall,ReleasedCharge);
    }
}
void AHeliosLightstickWave::Tick(float Dt)
{
    Super::Tick(Dt);if(!Settings)return;Age+=Dt;
    if(bFlying)
    {
        const float Old=Travelled;Travelled=FMath::Min(Travelled+FMath::Max(1.f,Settings->WaveSpeed)*Dt,FMath::Max(1.f,Settings->WaveRange));
        AdvanceDamage(Old,Travelled);UpdateWaveMesh(Travelled);
        if(Travelled>=Settings->WaveRange){bFlying=false;WaveSurface->SetVisibility(false);}
    }
    else {FadeAge+=Dt;if(FadeAge>.42f){Destroy();return;}}
}
