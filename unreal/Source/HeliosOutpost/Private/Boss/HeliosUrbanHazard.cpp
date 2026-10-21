#include "Boss/HeliosUrbanHazard.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AHeliosUrbanHazard::AHeliosUrbanHazard()
{
    PrimaryActorTick.bCanEverTick=true;
    Warning=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Warning")); SetRootComponent(Warning);
    Ball=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Basketball")); Ball->SetupAttachment(Warning);
    Burst=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Impact")); Burst->SetupAttachment(Warning);
    Fissure=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GroundFissure")); Fissure->SetupAttachment(Warning);
    Trail=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BasketballTrail")); Trail->SetupAttachment(Warning);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Ring(TEXT("/Game/UrbanChallenge/Meshes/UC_WarningRing.UC_WarningRing"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Basketball(TEXT("/Game/UrbanChallenge/Meshes/UC_Basketball.UC_Basketball"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Chip(TEXT("/Game/UrbanChallenge/Meshes/UC_FracturedRock.UC_FracturedRock"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Crack(TEXT("/Game/UrbanChallenge/Meshes/UC_GroundFissure.UC_GroundFissure"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Fissure->SetStaticMesh(Crack.Object); Trail->SetStaticMesh(Cylinder.Object);
    Warning->SetStaticMesh(Ring.Object); Ball->SetStaticMesh(Basketball.Object); Burst->SetStaticMesh(Ring.Object);
    for (auto* Mesh : {Warning.Get(),Ball.Get(),Burst.Get(),Fissure.Get(),Trail.Get()})
    {
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetCastShadow(false); Mesh->SetCanEverAffectNavigation(false);
    }
    Ball->SetAbsolute(false,false,true); Burst->SetAbsolute(false,false,true);
    Trail->SetAbsolute(false,false,true);
    Fissure->SetVisibility(false); Trail->SetVisibility(false);
    Ball->SetVisibility(false); Burst->SetVisibility(false);
    Shockwave=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SecondaryGroundShockwave"));
    Shockwave->SetupAttachment(Warning); Shockwave->SetStaticMesh(Ring.Object);
    Shockwave->SetAbsolute(false,false,true); Shockwave->SetVisibility(false);
    Shockwave->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Shockwave->SetCanEverAffectNavigation(false); Shockwave->SetCastShadow(false);
    for (int32 Index=0;Index<8;++Index)
    {
        auto* Cloud=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Dust%02d"),Index));
        Cloud->SetupAttachment(Warning); Cloud->SetStaticMesh(Sphere.Object);
        Cloud->SetAbsolute(false,false,true); Cloud->SetVisibility(false);
        Cloud->SetCollisionEnabled(ECollisionEnabled::NoCollision); Cloud->SetCanEverAffectNavigation(false); Cloud->SetCastShadow(false);
        Dust.Add(Cloud);
        auto* Spark=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Spark%02d"),Index));
        Spark->SetupAttachment(Warning); Spark->SetStaticMesh(Cylinder.Object);
        Spark->SetAbsolute(false,false,true); Spark->SetVisibility(false);
        Spark->SetCollisionEnabled(ECollisionEnabled::NoCollision); Spark->SetCanEverAffectNavigation(false); Spark->SetCastShadow(false);
        Sparks.Add(Spark);
    }
    for (int32 Index=0;Index<16;++Index)
    {
        auto* Mesh=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Debris%02d"),Index));
        Mesh->SetupAttachment(Warning); Mesh->SetStaticMesh(Chip.Object);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh->SetCanEverAffectNavigation(false);
        Mesh->SetAbsolute(false,false,true); Mesh->SetVisibility(false);
        Mesh->SetCastShadow(false); Debris.Add(Mesh);
    }
    Flash=CreateDefaultSubobject<UPointLightComponent>(TEXT("LocalImpactFlash")); Flash->SetupAttachment(Warning);
    Flash->SetCastShadows(false); Flash->SetAttenuationRadius(900); Flash->SetIntensity(0);
    Flash->SetLightColor(FLinearColor(1,.35f,.05f));
}

void AHeliosUrbanHazard::Configure(AActor* Caster,bool bBasketball,float Radius,float WarningSeconds,float Damage)
{
    SetOwner(Caster); bAirstrike=bBasketball;
    DangerRadius=FMath::Clamp(Radius,200.f,1100.f);
    TelegraphSeconds=FMath::Max(2.f,WarningSeconds); ImpactDamage=FMath::Max(0.f,Damage);
    SetActorScale3D(FVector(DangerRadius/100.f,DangerRadius/100.f,1.f));
}

void AHeliosUrbanHazard::BeginPlay()
{
    Super::BeginPlay();
    if (auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/UrbanChallenge/Materials/M_UC_Hazard.M_UC_Hazard")))
    {
        WarningMaterial=UMaterialInstanceDynamic::Create(Material,this);
        BurstMaterial=UMaterialInstanceDynamic::Create(Material,this);
        FissureMaterial=UMaterialInstanceDynamic::Create(Material,this);
        Warning->SetMaterial(0,WarningMaterial); Burst->SetMaterial(0,BurstMaterial);
        Fissure->SetMaterial(0,FissureMaterial); Trail->SetMaterial(0,BurstMaterial);
        Shockwave->SetMaterial(0,BurstMaterial);
        for (const auto& Mesh:Sparks) Mesh->SetMaterial(0,BurstMaterial);
        FissureMaterial->SetVectorParameterValue(TEXT("Color"),FLinearColor(1,.17f,.01f));
        BurstMaterial->SetVectorParameterValue(TEXT("Color"),FLinearColor(1,.42f,.03f));
    }
    if (auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/UrbanChallenge/Materials/M_UC_concrete.M_UC_concrete")))
        for (const auto& Mesh:Debris) Mesh->SetMaterial(0,Material);
    if (auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/UrbanChallenge/Materials/M_UC_ImpactDust.M_UC_ImpactDust")))
    {
        DustMaterial=UMaterialInstanceDynamic::Create(Material,this);
        for (const auto& Mesh:Dust) Mesh->SetMaterial(0,DustMaterial);
    }
    SetLifeSpan(bArrivalMarker?0.f:TelegraphSeconds+2.0f);
}

void AHeliosUrbanHazard::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    auto* Enemy=Cast<AHeliosEnemyActor>(GetOwner());
    if (!IsValid(GetOwner()) || (Enemy && Enemy->IsEnemyDead())) { Destroy(); return; }
    Age+=DeltaSeconds;
    if (WarningMaterial)
    {
        WarningMaterial->SetVectorParameterValue(TEXT("Color"),bArrivalMarker?FLinearColor(.005f,.35f,1.f):FLinearColor(1.f,.035f,.005f));
        WarningMaterial->SetScalarParameterValue(TEXT("Glow"),3.4f+.6f*FMath::Sin(Age*10.f));
    }
    if (bArrivalMarker) return; // Director removes the arrival cue only after its real-time warning.
    if (Age>=TelegraphSeconds && !bImpacted)
    {
        if (bAirstrike)
        {
            Ball->SetVisibility(true);
            Ball->SetRelativeLocation(FVector(0,0,FMath::Max(0.f,1800.f*(1.f-(Age-TelegraphSeconds)/.65f))));
            Ball->SetRelativeScale3D(FVector(1.5f));
            Ball->SetRelativeRotation(FRotator(Age*350.f,Age*180.f,Age*95.f));
            Trail->SetVisibility(true);
            Trail->SetWorldLocation(Ball->GetComponentLocation()+FVector(0,0,300));
            Trail->SetWorldScale3D(FVector(.09f,.09f,6.f));
        }
        if (!bAirstrike || Age>=TelegraphSeconds+.65f) Impact();
    }
    if (bImpacted)
    {
        const float After=Age-TelegraphSeconds-(bAirstrike?.65f:0.f);
        if (ImpactDamage<=0) return; // Corridor guide rings do not imply a second explosion.
        const float Radius=DangerRadius/100.f*FMath::Clamp(.35f+After*2.f,.35f,1.f);
        Burst->SetWorldScale3D(FVector(Radius,Radius,1));
        Burst->SetVisibility(After<.5f);
        Shockwave->SetWorldLocation(GetActorLocation()+FVector(0,0,5));
        const float SecondaryRadius=DangerRadius/100.f*FMath::Clamp(.2f+(After-.08f)*1.4f,.2f,1.f);
        Shockwave->SetWorldScale3D(FVector(SecondaryRadius,SecondaryRadius,1));
        Shockwave->SetVisibility(After>.08f && After<.65f);
        if (BurstMaterial) BurstMaterial->SetScalarParameterValue(TEXT("Glow"),FMath::Max(0.f,2.f*(1.f-After*2.f)));
        Flash->SetIntensity(80000.f*FMath::Max(0.f,1.f-After*5.f));
        Flash->SetWorldLocation(GetActorLocation()+FVector(0,0,100));
        Fissure->SetVisibility(!bAirstrike && After<1.2f);
        if (FissureMaterial) FissureMaterial->SetScalarParameterValue(TEXT("Glow"),FMath::Max(0.f,.6f*(1.f-After/1.2f)));
        // Translucent, low clouds stay inside 65% of the damaged area. Never obscure its rim.
        if (DustMaterial) DustMaterial->SetScalarParameterValue(TEXT("Opacity"),.12f*FMath::Clamp(After*8.f,0.f,1.f)*FMath::Max(0.f,1.f-After/1.2f));
        for (int32 Index=0;Index<Dust.Num();++Index)
        {
            const float Angle=Index*2.f*PI/Dust.Num()+.17f;
            const float Distance=DangerRadius*FMath::Min(.43f,.10f+After*.38f);
            auto* Cloud=Dust[Index].Get();
            Cloud->SetWorldLocation(GetActorLocation()+FVector(FMath::Cos(Angle)*Distance,FMath::Sin(Angle)*Distance,25.f+After*55.f));
            const float Size=FMath::Min(DangerRadius*.22f,35.f+After*110.f)/50.f;
            Cloud->SetWorldScale3D(FVector(Size,Size*.85f,Size*.42f));
            Cloud->SetVisibility(After>.02f && After<1.2f && DustMaterial!=nullptr);
            auto* Spark=Sparks[Index].Get();
            const FVector Velocity(FMath::Cos(Angle)*310.f,FMath::Sin(Angle)*310.f,360.f-After*700.f);
            Spark->SetWorldLocation(GetActorLocation()+FVector(FMath::Cos(Angle)*After*310.f,FMath::Sin(Angle)*After*310.f,35.f+After*360.f-After*After*350.f));
            Spark->SetWorldRotation(FRotationMatrix::MakeFromZ(Velocity).Rotator());
            Spark->SetWorldScale3D(FVector(.015f,.015f,.22f));
            Spark->SetVisibility(After<.55f);
        }
        for (int32 Index=0;Index<Debris.Num();++Index)
        {
            auto* Mesh=Debris[Index].Get();
            const float Angle=Index*2.f*PI/Debris.Num();
            const float Distance=FMath::Min(DangerRadius*(bAirstrike?.92f:.72f),40.f+After*(330.f+Index*17.f));
            const float Height=FMath::Max(5.f,20.f+After*(320.f+Index*11.f)-After*After*490.f);
            Mesh->SetWorldLocation(GetActorLocation()+FVector(FMath::Cos(Angle)*Distance,FMath::Sin(Angle)*Distance,Height));
            Mesh->SetWorldScale3D(FVector(.20f+Index%3*.06f,.16f,.23f)*(bAirstrike?1.f:2.3f));
            Mesh->SetWorldRotation(FRotator(After*(170.f+Index*13.f),Index*47.f,After*230.f));
            Mesh->SetVisibility(After<.9f);
        }
    }
}

void AHeliosUrbanHazard::Impact()
{
    bImpacted=true; Ball->SetVisibility(false); Trail->SetVisibility(false); Warning->SetVisibility(false); Burst->SetVisibility(ImpactDamage>0);
    for (TActorIterator<APawn> It(GetWorld());It;++It)
    {
        APawn* Pawn=*It;
        if (Pawn->ActorHasTag(TEXT("Dead")) || Cast<AHeliosEnemyActor>(Pawn) ||
            !(Pawn->IsPlayerControlled() || Pawn->ActorHasTag(TEXT("Player")) || Pawn->ActorHasTag(TEXT("HeliosPet")) || Pawn->ActorHasTag(TEXT("HeliosSummon")))) continue;
        FVector Foot=Pawn->GetActorLocation();
        if (const auto* Character=Cast<ACharacter>(Pawn)) Foot.Z-=Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        if (FMath::Abs(Foot.Z-GetActorLocation().Z)>100.f || FVector::DistSquared2D(Foot,GetActorLocation())>FMath::Square(DangerRadius)) continue;
        FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(UrbanHazardCover),true,this);
        Query.AddIgnoredActor(GetOwner()); Query.AddIgnoredActor(Pawn);
        if (GetWorld()->LineTraceSingleByChannel(Hit,GetActorLocation()+FVector(0,0,60),Foot+FVector(0,0,80),ECC_Visibility,Query)) continue;
        UGameplayStatics::ApplyDamage(Pawn,ImpactDamage,GetOwner()->GetInstigatorController(),this,nullptr);
    }
}
