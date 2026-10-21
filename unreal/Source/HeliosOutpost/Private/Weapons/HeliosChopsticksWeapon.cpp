#include "Weapons/HeliosChopsticksWeapon.h"
#include "Audio/HeliosAudio.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Weapons/HeliosWeaponVFX.h"
#include "Player/HeliosArenaCharacter.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"

namespace
{
constexpr const TCHAR* Root = TEXT("/Game/Helios/Weapons/Chopsticks/");
template<typename T> T* Art(const TCHAR* Name)
{
    return LoadObject<T>(nullptr, *(FString(Root) + Name));
}
void Cosmetic(UStaticMeshComponent* Mesh)
{
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetGenerateOverlapEvents(false);
    Mesh->SetCanEverAffectNavigation(false);
    Mesh->SetCastShadow(false);
}
}

AHeliosChopstickProjectile::AHeliosChopstickProjectile()
{
    bUseTemplateDamageSettings = false;
    PrimaryActorTick.bCanEverTick = true;
    bExplodeOnHit = false; bDamageOwner = false;
    HitDamage = 40.f; PhysicsForce = 80.f; DeferredDestructionTime = 2.f;
    if (auto* Collision = FindComponentByClass<USphereComponent>()) Collision->SetSphereRadius(1.2f);
    if (auto* Movement = FindComponentByClass<UProjectileMovementComponent>())
    {
        Movement->InitialSpeed = Movement->MaxSpeed = 6500.f;
        Movement->ProjectileGravityScale = 0.f;
        Movement->bShouldBounce = false;
        Movement->bRotationFollowsVelocity = true;
        Movement->bForceSubStepping = true;
        Movement->MaxSimulationTimeStep = 1.f / 120.f;
        Movement->MaxSimulationIterations = 8;
    }
    Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Golden chopstick"));
    Visual->SetupAttachment(GetRootComponent()); Cosmetic(Visual);
    for (int32 I=0; I<2; ++I)
    {
        auto* Part = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Gold trail %d"),I));
        Part->SetupAttachment(GetRootComponent()); Cosmetic(Part); Trails.Add(Part);
    }
    for (int32 I=0; I<3; ++I)
    {
        auto* Part = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Chopstick echo %d"),I));
        Part->SetupAttachment(GetRootComponent()); Cosmetic(Part); Echoes.Add(Part);
    }
}

void AHeliosChopstickProjectile::Configure(const FVector& Direction, float Damage, float Speed,
    float Range, UNiagaraSystem* Trail, UNiagaraSystem* Impact, USoundBase* Sound)
{
    FlightDirection = Direction.GetSafeNormal(SMALL_NUMBER,FVector::ForwardVector);
    HitDamage = FMath::Max(0.f,Damage); MaxRange = FMath::Max(1.f,Range);
    TrailEffect = Trail; ImpactEffect = Impact; ImpactSound = Sound;
    if (auto* Movement = FindComponentByClass<UProjectileMovementComponent>())
        HeliosCombatWeaponMath::InitializeRocketWorldVelocity(*Movement,FlightDirection,Speed);
    // Must be resident before FinishSpawning: BeginPlay can resolve a near-wall hit.
    Visual->SetStaticMesh(Art<UStaticMesh>(TEXT("SM_Chopstick")));
    for (int32 I=0; I<Trails.Num(); ++I)
    {
        Trails[I]->SetStaticMesh(Art<UStaticMesh>(TEXT("SM_Trail")));
        Trails[I]->SetMaterial(0,Art<UMaterialInterface>(I==0 ? TEXT("M_GoldCore") : TEXT("M_GoldHalo")));
        Trails[I]->SetRelativeLocation(FVector(-18,0,0));
        Trails[I]->SetRelativeScale3D(I==0 ? FVector(1,.55f,.55f) : FVector(1.2f,1.8f,1.8f));
    }
    for (int32 I=0; I<Echoes.Num(); ++I)
    {
        Echoes[I]->SetStaticMesh(Visual->GetStaticMesh());
        for (int32 M=0; M<Visual->GetNumMaterials(); ++M)
            Echoes[I]->SetMaterial(M,Art<UMaterialInterface>(TEXT("M_GoldEcho")));
        Echoes[I]->SetRelativeLocation(FVector(-38.f-40.f*I,0,0));
        Echoes[I]->SetRelativeScale3D(FVector(1.f,.8f,.8f));
    }
}

void AHeliosChopstickProjectile::IgnorePartner(AHeliosChopstickProjectile* Partner)
{
    if (auto* Collision = FindComponentByClass<USphereComponent>())
    {
        Collision->IgnoreActorWhenMoving(Partner,true);
        Collision->IgnoreActorWhenMoving(GetOwner(),true);
        Collision->IgnoreActorWhenMoving(GetInstigator(),true);
    }
}

void AHeliosChopstickProjectile::BeginPlay()
{
    LaunchLocation = GetActorLocation();
    Super::BeginPlay();
}

void AHeliosChopstickProjectile::Tick(float Dt)
{
    Super::Tick(Dt);
    if (const auto* Player=Cast<AHeliosArenaCharacter>(GetInstigator()); Player && Player->IsDead())
    { Destroy(); return; }
    if (bHit)
    {
        HitAge += Dt;
        Visual->SetRelativeScale3D(FVector(FMath::Clamp((2.f-HitAge)/.25f,0.f,1.f)));
        return;
    }
    FlightAge += Dt; SparkAge += Dt;
    if (FVector::DistSquared(LaunchLocation,GetActorLocation()) >= FMath::Square(MaxRange) || FlightAge > 3.f)
    { Destroy(); return; }
    // A short tapered core and wider halo follow the actual swept trajectory.
    const float Growth=FMath::Clamp(FlightAge/.045f,0.f,1.f);
    Trails[0]->SetRelativeScale3D(FVector(Growth,.55f,.55f));
    Trails[1]->SetRelativeScale3D(FVector(Growth*1.2f,1.8f,1.8f));
    for (int32 I=0; I<Echoes.Num(); ++I) Echoes[I]->SetVisibility(FlightAge>.009f*(I+1));
    if (SparkAge >= .055f && TrailEffect)
    {
        SparkAge = 0.f;
        UHeliosWeaponVFX::SpawnOneShot(this,TrailEffect,GetActorLocation(),(-FlightDirection).Rotation(),FVector::OneVector,.24f);
    }
}

void AHeliosChopstickProjectile::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other,
    UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal,
    FVector NormalImpulse, const FHitResult& Hit)
{
    if (bHit || Other==GetInstigator() || Other==GetOwner()) return;
    if (FVector::DistSquared(LaunchLocation,Hit.ImpactPoint)>FMath::Square(MaxRange)) { Destroy(); return; }
    Super::NotifyHit(MyComp,Other,OtherComp,bSelfMoved,HitLocation,HitNormal,NormalImpulse,Hit);
    for (UStaticMeshComponent* Part:Trails) Part->SetVisibility(false);
    for (UStaticMeshComponent* Part:Echoes) Part->SetVisibility(false);
    SetActorLocation(Hit.ImpactPoint-FlightDirection*12.f,false,nullptr,ETeleportType::TeleportPhysics);
    if (IsValid(OtherComp)) AttachToComponent(OtherComp,FAttachmentTransformRules::KeepWorldTransform,Hit.BoneName);
    if (ImpactEffect) UHeliosWeaponVFX::SpawnOneShot(this,ImpactEffect,Hit.ImpactPoint+Hit.ImpactNormal*2,
        Hit.ImpactNormal.Rotation(),FVector::OneVector,.48f);
    if (ImpactSound) HeliosAudio::PlaySoundAtLocation(this,ImpactSound,Hit.ImpactPoint,.55f);
    FActorSpawnParameters Params; Params.Owner=GetOwner();
    Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    GetWorld()->SpawnActor<AHeliosChopstickImpact>(Hit.ImpactPoint+Hit.ImpactNormal*2,Hit.ImpactNormal.Rotation(),Params);
}

void AHeliosChopstickProjectile::EndPlay(EEndPlayReason::Type Reason)
{
    Super::EndPlay(Reason);
}

AHeliosChopsticksWeapon::AHeliosChopsticksWeapon()
{
    MagazineSize=6; RefireRate=.4f; FiringRecoil=.18f; bFullAuto=true; MuzzleOffset=0;
    MeshAssetPath=TEXT("/Game/Helios/Weapons/Chopsticks/SM_Chopsticks");
    FireSoundPath=TEXT("/Game/Helios/Weapons/Chopsticks/SFX_ChopsticksThrow");
    ReloadSoundPath=TEXT("/Game/Helios/Weapons/Chopsticks/SFX_ChopsticksReload");
    ReleaseArc=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Wrist gold arc"));
    ReleaseArc->SetupAttachment(GetFirstPersonMesh());Cosmetic(ReleaseArc);
    ReleaseArc->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
    ReleaseArc->SetOnlyOwnerSee(true);ReleaseArc->SetVisibility(false);
}

void AHeliosChopsticksWeapon::BeginPlay()
{
    Super::BeginPlay();ReleaseArc->SetStaticMesh(Art<UStaticMesh>(TEXT("SM_GoldArc")));
    ArcMaterial=UMaterialInstanceDynamic::Create(Art<UMaterialInterface>(TEXT("M_GoldHalo")),this);
    if(ArcMaterial)ReleaseArc->SetMaterial(0,ArcMaterial);
}
void AHeliosChopsticksWeapon::Tick(float Dt)
{
    Super::Tick(Dt);ReleaseAge+=Dt;
    ReleaseArc->SetVisibility(ReleaseAge<.14f);
    ReleaseArc->SetRelativeLocationAndRotation(FVector(12,0,3),FRotator(0,0,-55+ReleaseAge*600));
    if(ArcMaterial)ArcMaterial->SetScalarParameterValue(TEXT("Opacity"),FMath::Max(0.f,1.f-ReleaseAge/.14f)*.65f);
}

void AHeliosChopsticksWeapon::FireProjectile(const FVector& TargetLocation)
{
    if (!GetWorld() || !WeaponOwner || !PawnOwner || CurrentBullets<=0) return;
    FVector View; FRotator Aim; PawnOwner->GetActorEyesViewPoint(View,Aim);
    const FVector Right=FRotationMatrix(Aim).GetUnitAxis(EAxis::Y);
    const float Range=ActiveDefinition ? ActiveDefinition->Range : 5000.f;
    const FVector Target=View+(TargetLocation-View).GetClampedToMaxSize(Range);
    AHeliosChopstickProjectile* Pair[2]={nullptr,nullptr};
    FTransform Transforms[2];
    for (int32 I=0; I<2; ++I)
    {
        const float Side=I==0 ? -1.f : 1.f;
        FVector Start=GetVisualMuzzleLocation()+Right*(Side*3.f);
        FCollisionQueryParams Query(SCENE_QUERY_STAT(ChopsticksMuzzle),false,GetOwner());
        Query.AddIgnoredActor(this); Query.AddIgnoredActor(PawnOwner);
        FHitResult Block;
        if (GetWorld()->SweepSingleByChannel(Block,View,Start,FQuat::Identity,ECC_Visibility,
            FCollisionShape::MakeSphere(1.2f),Query)) Start=Block.Location;
        const FVector Direction=(Target+Right*(Side*2.f)-Start).GetSafeNormal(SMALL_NUMBER,Aim.Vector());
        Transforms[I]=FTransform(Direction.Rotation(),Start);
        Pair[I]=GetWorld()->SpawnActorDeferred<AHeliosChopstickProjectile>(AHeliosChopstickProjectile::StaticClass(),
            Transforms[I],GetOwner(),PawnOwner,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (Pair[I]) Pair[I]->Configure(Direction,ActiveDefinition ? ActiveDefinition->Damage : 40.f,
            ActiveDefinition ? ActiveDefinition->ProjectileSpeed : 6500.f,Range,
            ArtTrail ? ArtTrail.Get() : (ActiveDefinition ? ActiveDefinition->TrailEffect.Get() : nullptr),
            ArtImpact ? ArtImpact.Get() : (ActiveDefinition ? ActiveDefinition->ImpactEffect.Get() : nullptr),ResidentActionSound);
    }
    if (!Pair[0] || !Pair[1])
    {
        for (auto* P:Pair) if (P) P->Destroy();
        return;
    }
    Pair[0]->IgnorePartner(Pair[1]); Pair[1]->IgnorePartner(Pair[0]);
    Projectiles.RemoveAll([](const auto& P){return !P.IsValid();});
    for (int32 I=0; I<2; ++I)
    {
        Pair[I]->SetNoiseTag(NoiseOwnerTag);
        UGameplayStatics::FinishSpawningActor(Pair[I],Transforms[I]);
        Projectiles.Add(Pair[I]); OnProjectilePresented.Broadcast(Pair[I]);
    }
    bShotCommitted=true; --CurrentBullets;ReleaseAge=0;
    WeaponOwner->AddWeaponRecoil(FiringRecoil);
    WeaponOwner->UpdateWeaponHUD(CurrentBullets,MagazineSize);
}

void AHeliosChopsticksWeapon::ClearProjectiles()
{
    for (auto& P:Projectiles) if (P.IsValid()) P->Destroy();
    Projectiles.Reset();
    ReleaseAge=10;ReleaseArc->SetVisibility(false);
    if(GetWorld())for(TActorIterator<AHeliosChopstickImpact> I(GetWorld());I;++I)if(I->GetOwner()==GetOwner())I->Destroy();
}
void AHeliosChopsticksWeapon::DeactivateWeapon() { ClearProjectiles(); Super::DeactivateWeapon(); }
void AHeliosChopsticksWeapon::EndPlay(EEndPlayReason::Type Reason) { ClearProjectiles(); Super::EndPlay(Reason); }

AHeliosChopstickImpact::AHeliosChopstickImpact()
{
    PrimaryActorTick.bCanEverTick=true;
    RootComponent=Ring=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Gold shock ring")); Cosmetic(Ring);
    InitialLifeSpan=.35f;
}
void AHeliosChopstickImpact::BeginPlay()
{
    Super::BeginPlay(); Ring->SetStaticMesh(Art<UStaticMesh>(TEXT("SM_GoldRing")));
    Material=UMaterialInstanceDynamic::Create(Art<UMaterialInterface>(TEXT("M_GoldHalo")),this);
    if(Material) Ring->SetMaterial(0,Material);
    Ring->SetRelativeScale3D(FVector(.1f));
}
void AHeliosChopstickImpact::Tick(float Dt)
{
    Super::Tick(Dt); Age+=Dt;
    Ring->SetRelativeScale3D(FVector(.1f+Age*3.f));
    if(Material) Material->SetScalarParameterValue(TEXT("Opacity"),FMath::Clamp(1.f-Age/.35f,0.f,1.f)*.45f);
}
