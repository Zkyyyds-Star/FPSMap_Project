#include "Weapons/HeliosCombatWeapons.h"
#include "Audio/HeliosAudio.h"
#include "Weapons/HeliosWeaponDamageSettings.h"
#include "Weapons/HeliosWeaponTempoSettings.h"
#include "Pets/HeliosPetDog.h"

#include "Weapons/HeliosWeaponDefinition.h"
#include "Weapons/HeliosWeaponPresentationProfile.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Weapons/HeliosWeaponVFX.h"
#include "Weapons/HeliosImpactPresentationSubsystem.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Components/SceneComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/AudioComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

namespace HeliosCombatWeaponMath
{
void InitializeRocketWorldVelocity(UProjectileMovementComponent& Movement,
    const FVector& WorldDirection, float Speed)
{
    const float SafeSpeed = FMath::Max(1.0f, Speed);
    Movement.InitialSpeed = SafeSpeed;
    Movement.MaxSpeed = SafeSpeed;
    // ConfigureRocket runs before FinishSpawningActor. Explicit world-space velocity
    // prevents UProjectileMovementComponent from rotating it a second time on register.
    Movement.bInitialVelocityInLocalSpace = false;
    Movement.Velocity = WorldDirection.GetSafeNormal(SMALL_NUMBER, FVector::ForwardVector) * SafeSpeed;
}

FVector ResolveRocketSpawnLocation(const FVector& DesiredLocation,
    const FHitResult& MuzzleObstruction)
{
    return MuzzleObstruction.bBlockingHit ? MuzzleObstruction.Location : DesiredLocation;
}

FVector CalculatePelletEndpoint(const FVector& ViewStart,
    const FVector& WorldDirection, float Range)
{
    return ViewStart
        + WorldDirection.GetSafeNormal(SMALL_NUMBER, FVector::ForwardVector) * FMath::Max(1.0f, Range);
}

FVector ExtendTracePastSurface(const FVector& TraceStart,
    const FVector& SurfacePoint, float Extension)
{
    return SurfacePoint + (SurfacePoint - TraceStart).GetSafeNormal() * FMath::Max(0.0f, Extension);
}
}

AHeliosCombatWeapon::AHeliosCombatWeapon()
{
    PrimaryActorTick.bCanEverTick = true;

    FirstPersonModel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Helios First Person Model"));
    FirstPersonModel->SetupAttachment(GetFirstPersonMesh());
    FirstPersonModel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FirstPersonModel->SetGenerateOverlapEvents(false);
    FirstPersonModel->SetCanEverAffectNavigation(false);
    FirstPersonModel->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
    FirstPersonModel->bOnlyOwnerSee = true;
    FirstPersonModel->SetCastShadow(false);

    ThirdPersonModel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Helios Third Person Model"));
    ThirdPersonModel->SetupAttachment(GetThirdPersonMesh());
    ThirdPersonModel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ThirdPersonModel->SetGenerateOverlapEvents(false);
    ThirdPersonModel->SetCanEverAffectNavigation(false);
    ThirdPersonModel->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::WorldSpaceRepresentation);
    ThirdPersonModel->bOwnerNoSee = true;
}

void AHeliosCombatWeapon::BeginPlay()
{
    Super::BeginPlay();
    if (!MeshAssetPath.IsEmpty())
    {
        if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshAssetPath))
        {
            FirstPersonModel->SetStaticMesh(Mesh);
            ThirdPersonModel->SetStaticMesh(Mesh);
        }
    }
    if (!FireSoundPath.IsEmpty()) FireSound = LoadObject<USoundBase>(nullptr, *FireSoundPath);
    if (!ReloadSoundPath.IsEmpty()) ReloadSound = LoadObject<USoundBase>(nullptr, *ReloadSoundPath);
    DrySound = LoadObject<USoundBase>(nullptr,
        TEXT("/Game/Helios/Weapons/Audio/SFX_WeaponDry.SFX_WeaponDry"));
}

void AHeliosCombatWeapon::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    CancelReload();
    Super::EndPlay(EndPlayReason);
}

void AHeliosCombatWeapon::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (HasExternalPresentation())
    {
        FirstPersonModel->SetVisibility(false);
        return;
    }
    if (!bReloadAnimating || ReloadAnimationDuration <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    ReloadAnimationElapsed += DeltaSeconds;
    const float Alpha = FMath::Clamp(ReloadAnimationElapsed / ReloadAnimationDuration, 0.0f, 1.0f);
    const float Arc = FMath::Sin(Alpha * PI);
    const FVector AnimatedLocation = ModelRestLocation + FVector(0.0f, 0.0f, -11.0f * Arc);
    const FRotator AnimatedRotation(
        ModelRestRotation.Pitch - 24.0f * Arc,
        ModelRestRotation.Yaw,
        ModelRestRotation.Roll + 8.0f * Arc);
    FirstPersonModel->SetRelativeLocationAndRotation(AnimatedLocation, AnimatedRotation);
    ThirdPersonModel->SetRelativeLocationAndRotation(AnimatedLocation, AnimatedRotation);
}

void AHeliosCombatWeapon::ApplyDefinition(const UHeliosWeaponDefinition* Definition)
{
    if (!Definition)
    {
        return;
    }
    ActiveDefinition = DuplicateObject<UHeliosWeaponDefinition>(Definition, this);
    ApplyWeaponDamageSettings(this, ActiveDefinition);
    ApplyWeaponTempoSettings(this, ActiveDefinition);
    Definition = ActiveDefinition;
    ResidentActionSound=Definition->ActionSound.LoadSynchronous();
    ResidentEffects.Reset();
    for (const auto& SoftEffect : {Definition->MuzzleEffect,Definition->TrailEffect,Definition->ImpactEffect,Definition->EnemyImpactEffect,Definition->ExplosionEffect,Definition->TracerEffect})
        if (UNiagaraSystem* Effect=SoftEffect.LoadSynchronous()) ResidentEffects.Add(Effect);
    if (!FParse::Param(FCommandLine::Get(),TEXT("HeliosLegacyPresentation")))
    {
        if (UHeliosWeaponPresentationProfile* Art=Definition->PresentationProfile.LoadSynchronous())
        {
            ArtImpact=Art->ImpactEffect.LoadSynchronous(); ArtTrail=Art->TrailEffect.LoadSynchronous(); ArtExplosion=Art->ExplosionEffect.LoadSynchronous();
        }
    }
    RefireRate = FMath::Max(0.01f, Definition->RefireSeconds);
    FiringRecoil = FMath::Max(0.0f, Definition->Recoil);
    MuzzleOffset = FMath::Max(0.0f, Definition->MuzzleOffset);
    ModelMuzzleLocation = Definition->ModelMuzzleLocation;

    if (UStaticMesh* Mesh = Definition->WeaponMesh.LoadSynchronous())
    {
        FirstPersonModel->SetStaticMesh(Mesh);
        ThirdPersonModel->SetStaticMesh(Mesh);
    }
    if (!Definition->FireSound.IsNull()) FireSound = Definition->FireSound.LoadSynchronous();
    if (!Definition->ReloadSound.IsNull()) ReloadSound = Definition->ReloadSound.LoadSynchronous();
    if (!Definition->DrySound.IsNull()) DrySound = Definition->DrySound.LoadSynchronous();
}

void AHeliosCombatWeapon::Fire()
{
    if (bIsFiring && CurrentBullets > 0)
    {
        if (!HasExternalPresentation()) PlayWeaponSound(FireSound);
        if (!HasExternalPresentation() && ActiveDefinition && !ActiveDefinition->MuzzleEffect.IsNull())
        {
            const FRotator Aim = PawnOwner ? PawnOwner->GetBaseAimRotation() : GetActorRotation();
            UHeliosWeaponVFX::SpawnOneShot(this, ActiveDefinition->MuzzleEffect.Get(),
                GetVisualMuzzleLocation(), Aim, FVector::OneVector,
                ActiveDefinition->WeaponId == TEXT("shotgun") ? 0.10f : 0.16f);
        }
    }
    Super::Fire();
}

void AHeliosCombatWeapon::BeginReload(float DurationSeconds)
{
    StopFiring();
    bReloadAnimating = !HasExternalPresentation();
    ReloadAnimationDuration = FMath::Max(0.01f, DurationSeconds);
    ReloadAnimationElapsed = 0.0f;
    if (!HasExternalPresentation()) PlayWeaponSound(ReloadSound, 0.9f);
}

void AHeliosCombatWeapon::ReloadMagazine()
{
    Super::ReloadMagazine();
    CancelReload();
}

void AHeliosCombatWeapon::CancelReload()
{
    bReloadAnimating = false;
    ReloadAnimationDuration = 0.0f;
    ReloadAnimationElapsed = 0.0f;
    if (FirstPersonModel)
    {
        FirstPersonModel->SetRelativeLocationAndRotation(ModelRestLocation, ModelRestRotation);
    }
    if (ThirdPersonModel)
    {
        ThirdPersonModel->SetRelativeLocationAndRotation(ModelRestLocation, ModelRestRotation);
    }
}

void AHeliosCombatWeapon::PlayDryFire()
{
    PlayWeaponSound(DrySound, 0.7f);
}

void AHeliosCombatWeapon::PlayWeaponSound(USoundBase* Sound, float Volume) const
{
    if (Sound)
    {
        HeliosAudio::PlaySoundAtLocation(this, Sound, GetVisualMuzzleLocation(), Volume);
    }
}

FVector AHeliosCombatWeapon::GetVisualMuzzleLocation() const
{
    if (HasExternalPresentation() && GetFirstPersonMesh()->DoesSocketExist(TEXT("Muzzle")))
        return GetFirstPersonMesh()->GetSocketLocation(TEXT("Muzzle"));
    if (FirstPersonModel && FirstPersonModel->DoesSocketExist(TEXT("Muzzle")))
    {
        return FirstPersonModel->GetSocketLocation(TEXT("Muzzle"));
    }
    if (FirstPersonModel)
    {
        return FirstPersonModel->GetComponentTransform().TransformPosition(ModelMuzzleLocation);
    }
    const FVector Forward = PawnOwner ? PawnOwner->GetBaseAimRotation().Vector() : GetActorForwardVector();
    return GetActorLocation() + Forward * FMath::Max(35.0f, MuzzleOffset);
}

FHitResult AHeliosCombatWeapon::TraceFromMuzzle(
    const FVector& ViewStart, const FVector& DesiredEnd, FName TraceTag) const
{
    FHitResult Result;
    if (!GetWorld())
    {
        return Result;
    }
    const FVector Muzzle = GetVisualMuzzleLocation();
    FCollisionQueryParams Params(TraceTag, true, GetOwner());
    Params.bReturnPhysicalMaterial = true;
    if (auto* Pets=GetWorld()->GetSubsystem<UHeliosPetWorld>()) Pets->IgnoreAll(Params);
    Params.AddIgnoredActor(this);
    Params.AddIgnoredActor(GetOwner());

    // A gun model may visually cross a nearby wall. The camera-to-muzzle segment is
    // authoritative in that case, so neither hitscan nor a beam starts behind it.
    if (GetWorld()->LineTraceSingleByChannel(Result, ViewStart, Muzzle, ECC_Visibility, Params))
    {
        return Result;
    }
    GetWorld()->LineTraceSingleByChannel(Result, Muzzle, DesiredEnd, ECC_Visibility, Params);
    if (!Result.bBlockingHit)
    {
        Result.TraceStart = Muzzle;
        Result.TraceEnd = DesiredEnd;
    }
    return Result;
}

AHeliosRocketProjectile::AHeliosRocketProjectile()
{
    bUseTemplateDamageSettings = false;
    InitialLifeSpan = 8.0f;
    HitDamage = 360.0f;
    MinimumExplosionDamage = 60.0f;
    ExplosionRadius = 2000.0f;
    bExplodeOnHit = true;
    bDamageOwner = false;
    DeferredDestructionTime = 0.0f;

    ProjectileVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Rocket Visual"));
    ProjectileVisual->SetupAttachment(GetRootComponent());
    ProjectileVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ProjectileVisual->SetGenerateOverlapEvents(false);
    ProjectileVisual->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
    ProjectileVisual->SetRelativeScale3D(FVector(0.08f, 0.08f, 0.22f));

    FlightLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("Rocket Light"));
    FlightLight->SetupAttachment(GetRootComponent());
    FlightLight->SetLightColor(FLinearColor(1.0f, 0.22f, 0.03f));
    FlightLight->SetIntensity(900.0f);
    FlightLight->SetCastShadows(false);
    FlightLight->SetAttenuationRadius(170.0f);

    if (UProjectileMovementComponent* Movement = FindComponentByClass<UProjectileMovementComponent>())
    {
        HeliosCombatWeaponMath::InitializeRocketWorldVelocity(
            *Movement, GetActorForwardVector(), 10000.0f);
        Movement->ProjectileGravityScale = 0.0f;
        Movement->bShouldBounce = false;
    }
    if (USphereComponent* Sphere = FindComponentByClass<USphereComponent>())
    {
        Sphere->SetSphereRadius(8.0f);
    }
}

void AHeliosRocketProjectile::BeginPlay()
{
    Super::BeginPlay();
    if (IsActorBeingDestroyed() || bHit) return;
    ProjectileVisual->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone")));
    ProjectileVisual->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Helios/Weapons/Materials/M_RocketOrange.M_RocketOrange")));
    if (!ImpactSound)
    {
        ImpactSound = LoadObject<USoundBase>(nullptr,
            TEXT("/Game/Helios/Weapons/Audio/SFX_RocketImpact.SFX_RocketImpact"));
    }
}

void AHeliosRocketProjectile::ConfigureEffects(UNiagaraSystem* InTrail, UNiagaraSystem* InExplosion)
{
    TrailSystem = InTrail;
    ExplosionSystem = InExplosion;
}

void AHeliosRocketProjectile::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bHit || !TrailSystem) return;
    TrailElapsed += DeltaSeconds;
    if (TrailElapsed >= 0.05f)
    {
        TrailElapsed = FMath::Fmod(TrailElapsed, 0.05f);
        UHeliosWeaponVFX::SpawnOneShot(this, TrailSystem,
            GetActorLocation() - GetActorForwardVector() * 12.0f,
            (-GetActorForwardVector()).Rotation(), FVector::OneVector, 0.6f);
    }
}

void AHeliosRocketProjectile::EndPlay(const EEndPlayReason::Type Reason)
{
    if (TrailComponent) UHeliosWeaponVFX::ReleaseTracked(TrailComponent);
    TrailComponent = nullptr;
    Super::EndPlay(Reason);
}

void AHeliosRocketProjectile::ConfigureRocket(float InMaxDamage, float InMinDamage,
    float InRadius, float InSpeed, USoundBase* InImpactSound)
{
    HitDamage = FMath::Max(0.0f, InMaxDamage);
    MinimumExplosionDamage = FMath::Clamp(InMinDamage, 0.0f, HitDamage);
    ExplosionRadius = FMath::Max(1.0f, InRadius);
    if (InImpactSound) ImpactSound = InImpactSound;
    if (UProjectileMovementComponent* Movement = FindComponentByClass<UProjectileMovementComponent>())
    {
        HeliosCombatWeaponMath::InitializeRocketWorldVelocity(
            *Movement, GetActorForwardVector(), InSpeed);
    }
}

void AHeliosRocketProjectile::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other,
    UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal,
    FVector NormalImpulse, const FHitResult& Hit)
{
    if (bHit || !GetWorld())
    {
        return;
    }
    bHit = true;
    SetActorEnableCollision(false);
    if (UProjectileMovementComponent* Movement = FindComponentByClass<UProjectileMovementComponent>())
    {
        Movement->StopMovementImmediately();
        Movement->Deactivate();
    }

    FVector Center = GetActorLocation();
    if (!Hit.ImpactPoint.IsNearlyZero()) Center = FVector(Hit.ImpactPoint);
    TArray<FOverlapResult> Overlaps;
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_Pawn);
    FCollisionQueryParams OverlapParams(SCENE_QUERY_STAT(HeliosRocketExplosion), false, this);
    OverlapParams.AddIgnoredActor(this);
    OverlapParams.AddIgnoredActor(GetOwner());
    OverlapParams.AddIgnoredActor(GetInstigator());
    GetWorld()->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, Objects,
        FCollisionShape::MakeSphere(ExplosionRadius), OverlapParams);

    TSet<TWeakObjectPtr<AActor>> Damaged;
    for (const FOverlapResult& Overlap : Overlaps)
    {
        AActor* Target = Overlap.GetActor();
        const TWeakObjectPtr<AActor> TargetPtr(Target);
        if (!IsValid(Target) || Target == GetOwner() || Target == GetInstigator() || Damaged.Contains(TargetPtr))
        {
            continue;
        }

        const FVector TargetPoint = Target->GetActorLocation();
        FHitResult Occlusion;
        FCollisionQueryParams LosParams(SCENE_QUERY_STAT(HeliosRocketOcclusion), true, this);
        LosParams.AddIgnoredActor(this);
        LosParams.AddIgnoredActor(GetOwner());
        LosParams.AddIgnoredActor(GetInstigator());
        const FVector LosStart = Center + HitNormal.GetSafeNormal() * 5.0f;
        const bool bBlocked = GetWorld()->LineTraceSingleByChannel(
            Occlusion, LosStart, TargetPoint, ECC_Visibility, LosParams);
        if (bBlocked && Occlusion.GetActor() != Target)
        {
            continue;
        }

        const float DistanceAlpha = FMath::Clamp(FVector::Distance(Center, TargetPoint) / ExplosionRadius, 0.0f, 1.0f);
        const float Damage = FMath::Lerp(HitDamage, MinimumExplosionDamage, DistanceAlpha);
        APawn* InstigatorPawn = GetInstigator();
        UGameplayStatics::ApplyDamage(Target, Damage,
            InstigatorPawn ? InstigatorPawn->GetController() : nullptr, this, HitDamageType);
        Damaged.Add(TargetPtr);
    }

    FActorSpawnParameters EffectParams;
    EffectParams.Owner = GetOwner();
    EffectParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    if (AHeliosExplosionEffect* Effect = GetWorld()->SpawnActor<AHeliosExplosionEffect>(
        AHeliosExplosionEffect::StaticClass(), Center, FRotator::ZeroRotator, EffectParams))
        Effect->SetEffect(ExplosionSystem, ExplosionRadius, HitNormal);
    if (ImpactSound)
    {
        HeliosAudio::PlaySoundAtLocation(this, ImpactSound, Center);
    }
    BP_OnProjectileHit(Hit);
    Destroy();
}

AHeliosRocketWeapon::AHeliosRocketWeapon()
{
    MagazineSize = 1;
    RefireRate = 0.85f;
    FiringRecoil = 2.2f;
    bFullAuto = false;
    MeshAssetPath = TEXT("/Game/Helios/Weapons/Meshes/SM_Rocket.SM_Rocket");
    FireSoundPath = TEXT("/Game/Helios/Weapons/Audio/SFX_RocketFire.SFX_RocketFire");
    ReloadSoundPath = TEXT("/Game/Helios/Weapons/Audio/SFX_RocketReload.SFX_RocketReload");
}

void AHeliosRocketWeapon::ApplyDefinition(const UHeliosWeaponDefinition* Definition)
{
    Super::ApplyDefinition(Definition);
}

void AHeliosRocketWeapon::FireProjectile(const FVector& TargetLocation)
{
    if (!GetWorld() || !WeaponOwner || !PawnOwner)
    {
        return;
    }
    FVector ViewStart;
    FRotator ViewRotation;
    PawnOwner->GetActorEyesViewPoint(ViewStart, ViewRotation);
    FVector SpawnLocation = GetVisualMuzzleLocation();
    FVector Direction = (TargetLocation - SpawnLocation).GetSafeNormal(SMALL_NUMBER, ViewRotation.Vector());
    SpawnLocation += Direction * MuzzleOffset;

    FCollisionQueryParams MuzzleParams(SCENE_QUERY_STAT(HeliosRocketMuzzle), false, GetOwner());
    if (auto* Pets=GetWorld()->GetSubsystem<UHeliosPetWorld>()) Pets->IgnoreAll(MuzzleParams);
    MuzzleParams.AddIgnoredActor(this);
    MuzzleParams.AddIgnoredActor(GetOwner());
    MuzzleParams.AddIgnoredActor(PawnOwner);
    FHitResult MuzzleObstruction;
    GetWorld()->SweepSingleByChannel(MuzzleObstruction, ViewStart, SpawnLocation,
        FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(8.0f), MuzzleParams);
    if (MuzzleObstruction.bBlockingHit)
    {
        SpawnLocation = HeliosCombatWeaponMath::ResolveRocketSpawnLocation(
            SpawnLocation, MuzzleObstruction);
        Direction = (TargetLocation - SpawnLocation).GetSafeNormal(SMALL_NUMBER, ViewRotation.Vector());
    }
    const FTransform SpawnTransform(Direction.Rotation(), SpawnLocation, FVector::OneVector);
    AHeliosRocketProjectile* Rocket = GetWorld()->SpawnActorDeferred<AHeliosRocketProjectile>(
        AHeliosRocketProjectile::StaticClass(), SpawnTransform, GetOwner(), PawnOwner,
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (Rocket)
    {
        bShotCommitted = true;
        const float MaxDamage = ActiveDefinition ? ActiveDefinition->Damage : 360.0f;
        const float MinDamage = ActiveDefinition ? ActiveDefinition->MinimumDamage : 60.0f;
        const float Radius = ActiveDefinition ? ActiveDefinition->Range : 2000.0f;
        const float Speed = ActiveDefinition ? ActiveDefinition->ProjectileSpeed : 10000.0f;
        USoundBase* Impact = ResidentActionSound;
        Rocket->ConfigureRocket(MaxDamage, MinDamage, Radius, Speed, Impact);
        Rocket->ConfigureEffects(HasExternalPresentation() && ArtTrail ? ArtTrail.Get() : (ActiveDefinition ? ActiveDefinition->TrailEffect.Get() : nullptr),
            HasExternalPresentation() && ArtExplosion ? ArtExplosion.Get() : (ActiveDefinition ? ActiveDefinition->ExplosionEffect.Get() : nullptr));
        Rocket->SetNoiseTag(NoiseOwnerTag);
        UGameplayStatics::FinishSpawningActor(Rocket, SpawnTransform);
    }
    WeaponOwner->PlayFiringMontage(FiringMontage);
    WeaponOwner->AddWeaponRecoil(FiringRecoil);
    --CurrentBullets;
    WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
}

AHeliosLaserWeapon::AHeliosLaserWeapon()
{
    MagazineSize = 100;
    RefireRate = 0.1f;
    FiringRecoil = 0.08f;
    bFullAuto = true;
    MeshAssetPath = TEXT("/Game/Helios/Weapons/Meshes/SM_Laser.SM_Laser");
    ReloadSoundPath = TEXT("/Game/Helios/Weapons/Audio/SFX_LaserReload.SFX_LaserReload");

    BeamVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Laser Beam"));
    BeamVisual->SetupAttachment(GetRootComponent());
    BeamVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BeamVisual->SetGenerateOverlapEvents(false);
    BeamVisual->SetCastShadow(false);
    BeamVisual->bOnlyOwnerSee = true;
    BeamVisual->SetVisibility(false);

    BeamHalo = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Laser Halo"));
    BeamHalo->SetupAttachment(GetRootComponent());
    BeamHalo->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BeamHalo->SetGenerateOverlapEvents(false); BeamHalo->SetCastShadow(false);
    BeamHalo->bOnlyOwnerSee = true; BeamHalo->SetVisibility(false);

    ImpactLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("Laser Impact Light"));
    ImpactLight->SetupAttachment(GetRootComponent());
    ImpactLight->SetLightColor(FLinearColor(0.05f, 0.45f, 1.0f));
    ImpactLight->SetIntensity(2600.0f);
    ImpactLight->SetAttenuationRadius(120.0f);
    ImpactLight->SetCastShadows(false);
    ImpactLight->SetVisibility(false);
}

void AHeliosLaserWeapon::BeginPlay()
{
    Super::BeginPlay();
    BeamVisual->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
    BeamHalo->SetStaticMesh(BeamVisual->GetStaticMesh());
    if (UMaterialInterface* LaserMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Helios/Weapons/Materials/M_LaserBlue.M_LaserBlue")))
    {
        BeamVisual->SetMaterial(0, LaserMaterial);
    }
    LoopSound = LoadObject<USoundBase>(nullptr,
        TEXT("/Game/Helios/Weapons/Audio/SFX_LaserLoop.SFX_LaserLoop"));
}

void AHeliosLaserWeapon::ApplyDefinition(const UHeliosWeaponDefinition* Definition)
{
    Super::ApplyDefinition(Definition);
    Definition = ActiveDefinition;
    if (!Definition) return;
    LaserDamage = FMath::Max(0.0f, Definition->Damage);
    LaserRange = FMath::Max(1.0f, Definition->Range);
    LaserPulseSeconds = FMath::Max(0.01f, Definition->PulseSeconds);
    LaserEnergyPerPulse = FMath::Max(1, Definition->EnergyPerPulse);
    if (!Definition->ActionSound.IsNull()) LoopSound = Definition->ActionSound.LoadSynchronous();
    if (!FParse::Param(FCommandLine::Get(),TEXT("HeliosLegacyPresentation")))
    {
        if (UHeliosWeaponPresentationProfile* Art=Definition->PresentationProfile.LoadSynchronous())
        {
            if(UMaterialInterface* Core=Art->BeamCoreMaterial.LoadSynchronous()) BeamVisual->SetMaterial(0,Core);
            if(UMaterialInterface* Halo=Art->BeamHaloMaterial.LoadSynchronous()) BeamHalo->SetMaterial(0,Halo);
        }
    }
}

void AHeliosLaserWeapon::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!HasExternalPresentation() || !bIsFiring || !PawnOwner) return;
    FVector ViewStart; FRotator ViewRotation; PawnOwner->GetActorEyesViewPoint(ViewStart,ViewRotation);
    const FVector Desired=ViewStart+ViewRotation.Vector()*LaserRange;
    const FHitResult Hit=TraceFromMuzzle(ViewStart,Desired,TEXT("HeliosLaserPresentation"));
    UpdateBeam(GetVisualMuzzleLocation(),Hit.bBlockingHit?Hit.ImpactPoint:Desired,true,Hit.bBlockingHit);
}

void AHeliosLaserWeapon::StartFiring()
{
    if (!WeaponOwner || !GetWorld() || CurrentBullets < LaserEnergyPerPulse)
    {
        PlayDryFire();
        StopFiring();
        return;
    }
    if (bIsFiring)
    {
        return;
    }
    bIsFiring = true;
    if (LoopSound && !LoopAudio)
    {
        LoopAudio = HeliosAudio::SpawnSoundAttached(LoopSound, FirstPersonModel);
    }
    LaserPulse();
    if (bIsFiring)
    {
        GetWorldTimerManager().SetTimer(LaserPulseTimer, this,
            &AHeliosLaserWeapon::LaserPulse, LaserPulseSeconds, true);
    }
}

void AHeliosLaserWeapon::StopFiring()
{
    if (GetWorld())
    {
        GetWorldTimerManager().ClearTimer(LaserPulseTimer);
    }
    UpdateBeam(FVector::ZeroVector, FVector::ZeroVector, false);
    if (LoopAudio)
    {
        LoopAudio->Stop();
        LoopAudio->DestroyComponent();
        LoopAudio = nullptr;
    }
    Super::StopFiring();
}

void AHeliosLaserWeapon::DeactivateWeapon()
{
    StopFiring();
    Super::DeactivateWeapon();
}

void AHeliosLaserWeapon::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopFiring();
    Super::EndPlay(EndPlayReason);
}

void AHeliosLaserWeapon::LaserPulse()
{
    if (!bIsFiring || !WeaponOwner || !PawnOwner || CurrentBullets < LaserEnergyPerPulse)
    {
        if (CurrentBullets < LaserEnergyPerPulse) PlayDryFire();
        StopFiring();
        return;
    }

    FVector ViewStart;
    FRotator ViewRotation;
    PawnOwner->GetActorEyesViewPoint(ViewStart, ViewRotation);
    const FVector DesiredEnd = ViewStart + ViewRotation.Vector() * LaserRange;
    const FHitResult Hit = TraceFromMuzzle(ViewStart, DesiredEnd, TEXT("HeliosLaserTrace"));
    const FVector BeamStart = GetVisualMuzzleLocation();
    const FVector BeamEnd = Hit.bBlockingHit ? Hit.ImpactPoint : DesiredEnd;
    UpdateBeam(BeamStart, BeamEnd, true, Hit.bBlockingHit);
    if (Hit.bBlockingHit && HasExternalPresentation())
        if (auto* Feedback=GetWorld()->GetSubsystem<UHeliosImpactPresentationSubsystem>())
            Feedback->ReportImpact(TEXT("laser"),Feedback->BeginShot(),Hit);
    ImpactLight->SetVisibility(Hit.bBlockingHit);

    if (AActor* Target = Hit.GetActor())
    {
        if (Target != GetOwner() && Target != PawnOwner)
        {
            UGameplayStatics::ApplyPointDamage(Target, LaserDamage, (BeamEnd - BeamStart).GetSafeNormal(),
                Hit, PawnOwner->GetController(), this, UDamageType::StaticClass());
        }
    }
    CurrentBullets = FMath::Max(0, CurrentBullets - LaserEnergyPerPulse);
    WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
    OnShotPresented.Broadcast();
    WeaponOwner->AddWeaponRecoil(FiringRecoil);
    TimeOfLastShot = GetWorld()->GetTimeSeconds();
    MakeNoise(ShotLoudness, PawnOwner, PawnOwner->GetActorLocation(), ShotNoiseRange, NoiseOwnerTag);
}

void AHeliosLaserWeapon::UpdateBeam(const FVector& Start, const FVector& End, bool bVisible, bool bHasHit)
{
    if (!BeamVisual || !ImpactLight)
    {
        return;
    }
    BeamVisual->SetVisibility(bVisible);
    BeamHalo->SetVisibility(bVisible && HasExternalPresentation());
    ImpactLight->SetVisibility(bVisible && bHasHit);
    if (!bVisible)
    {
        return;
    }
    const FVector Delta = End - Start;
    const float Length = Delta.Size();
    BeamVisual->SetWorldLocation((Start + End) * 0.5f);
    BeamVisual->SetWorldRotation(FRotationMatrix::MakeFromZ(Delta.GetSafeNormal()).Rotator());
    const float CoreWidth=HasExternalPresentation()?.007f:.018f;
    BeamVisual->SetWorldScale3D(FVector(CoreWidth, CoreWidth, FMath::Max(0.001f, Length / 100.0f)));
    if (HasExternalPresentation())
    {
        BeamHalo->SetWorldLocationAndRotation(BeamVisual->GetComponentLocation(),BeamVisual->GetComponentRotation());
        BeamHalo->SetWorldScale3D(FVector(.028f,.028f,FMath::Max(.001f,Length/100.f)));
    }
    ImpactLight->SetWorldLocation(End);
}

AHeliosShotgunWeapon::AHeliosShotgunWeapon()
{
    MagazineSize = 8;
    RefireRate = 0.85f;
    FiringRecoil = 2.8f;
    bFullAuto = false;
    MeshAssetPath = TEXT("/Game/Helios/Weapons/Meshes/SM_Shotgun.SM_Shotgun");
    FireSoundPath = TEXT("/Game/Helios/Weapons/Audio/SFX_ShotgunFire.SFX_ShotgunFire");
    ReloadSoundPath = TEXT("/Game/Helios/Weapons/Audio/SFX_ShotgunReload.SFX_ShotgunReload");
}

void AHeliosShotgunWeapon::ApplyDefinition(const UHeliosWeaponDefinition* Definition)
{
    Super::ApplyDefinition(Definition);
    Definition = ActiveDefinition;
    if (!Definition) return;
    PelletDamage = FMath::Max(0.0f, Definition->Damage);
    PelletMinDamage = FMath::Clamp(Definition->MinimumDamage, 0.0f, PelletDamage);
    ShotgunRange = FMath::Max(1.0f, Definition->Range);
    FalloffStart = FMath::Clamp(Definition->FalloffStart, 0.0f, ShotgunRange);
    PelletCount = FMath::Max(1, Definition->PelletCount);
    SpreadHalfAngle = FMath::Clamp(Definition->SpreadHalfAngle, 0.0f, 45.0f);
}

void AHeliosShotgunWeapon::FireProjectile(const FVector& TargetLocation)
{
    if (!GetWorld() || !WeaponOwner || !PawnOwner)
    {
        return;
    }
    bShotCommitted = true;
    FVector ViewStart;
    FRotator ViewRotation;
    PawnOwner->GetActorEyesViewPoint(ViewStart, ViewRotation);
    // TargetLocation may have been calculated from a different eye origin. The pellet
    // cone is defined directly by the authoritative camera view instead.
    const FVector AimDirection = ViewRotation.Vector();
    const FVector Muzzle = GetVisualMuzzleLocation();
    TArray<FHitResult> PelletHits;
    auto* Feedback=HasExternalPresentation()?GetWorld()->GetSubsystem<UHeliosImpactPresentationSubsystem>():nullptr;
    for (int32 Pellet = 0; Pellet < PelletCount; ++Pellet)
    {
        const FVector Direction = FMath::VRandCone(AimDirection, FMath::DegreesToRadians(SpreadHalfAngle));
        const FVector CameraEnd = HeliosCombatWeaponMath::CalculatePelletEndpoint(
            ViewStart, Direction, ShotgunRange);
        FHitResult CameraHit;
        FCollisionQueryParams CameraParams(SCENE_QUERY_STAT(HeliosShotgunCamera), true, GetOwner());
        if (auto* Pets=GetWorld()->GetSubsystem<UHeliosPetWorld>()) Pets->IgnoreAll(CameraParams);
        CameraParams.AddIgnoredActor(this);
        CameraParams.AddIgnoredActor(GetOwner());
        CameraParams.AddIgnoredActor(PawnOwner);
        GetWorld()->LineTraceSingleByChannel(CameraHit, ViewStart, CameraEnd, ECC_Visibility, CameraParams);
        // Cross the camera trace's impact plane by a small amount. Ending exactly on
        // the surface can make the muzzle trace miss due to contact-point precision.
        const FVector DesiredEnd = CameraHit.bBlockingHit
            ? HeliosCombatWeaponMath::ExtendTracePastSurface(Muzzle, CameraHit.ImpactPoint)
            : CameraEnd;
        const FHitResult Hit = TraceFromMuzzle(ViewStart, DesiredEnd, TEXT("HeliosShotgunPellet"));
        if (Hit.bBlockingHit && HasExternalPresentation()) PelletHits.Add(Hit);
        // Every actual pellet gets its own visible flight, including unobstructed misses.
        if(Feedback) Feedback->SpawnTracer(Muzzle,Hit.bBlockingHit?FVector(Hit.ImpactPoint):DesiredEnd,true);
        if (ActiveDefinition && Pellet < 4 && !HasExternalPresentation())
        {
            const FVector End = Hit.bBlockingHit ? FVector(Hit.ImpactPoint) : DesiredEnd;
            if (!HasExternalPresentation())
            {
                UNiagaraComponent* Tracer = UHeliosWeaponVFX::SpawnOneShot(this,
                    ActiveDefinition->TracerEffect.Get(), Muzzle,
                    (End-Muzzle).Rotation(), FVector::OneVector, 0.08f);
                UHeliosWeaponVFX::SetTracerEndpoints(Tracer, Muzzle, End);
            }
            if (Hit.bBlockingHit)
            {
                UNiagaraSystem* Impact = Hit.GetActor() && Hit.GetActor()->IsA<APawn>()
                    ? ActiveDefinition->EnemyImpactEffect.Get()
                    : ActiveDefinition->ImpactEffect.Get();
                UHeliosWeaponVFX::SpawnOneShot(this, Impact,
                    Hit.ImpactPoint + Hit.ImpactNormal * 1.0f,
                    Hit.ImpactNormal.Rotation(), FVector::OneVector, 0.6f);
            }
        }
        if (AActor* Target = Hit.GetActor())
        {
            if (Target != GetOwner() && Target != PawnOwner)
            {
                const float Distance = FVector::Distance(Muzzle, Hit.ImpactPoint);
                const float Alpha = ShotgunRange > FalloffStart
                    ? FMath::Clamp((Distance - FalloffStart) / (ShotgunRange - FalloffStart), 0.0f, 1.0f)
                    : 1.0f;
                const float Damage = FMath::Lerp(PelletDamage, PelletMinDamage, Alpha);
                UGameplayStatics::ApplyPointDamage(Target, Damage, Direction, Hit,
                    PawnOwner->GetController(), this, UDamageType::StaticClass());
                if (UPrimitiveComponent* Component = Hit.GetComponent())
                {
                    if (Component->IsSimulatingPhysics())
                    {
                        Component->AddImpulseAtLocation(Direction * 110.0f, Hit.ImpactPoint);
                    }
                }
            }
        }
    }

    if(Feedback) Feedback->ReportPellets(Feedback->BeginShot(),PelletHits);
    WeaponOwner->PlayFiringMontage(FiringMontage);
    WeaponOwner->AddWeaponRecoil(FiringRecoil);
    --CurrentBullets;
    WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
}

AHeliosExplosionEffect::AHeliosExplosionEffect()
{
    PrimaryActorTick.bCanEverTick = true;
    SetLifeSpan(1.25f);
    Tags.Add(TEXT("HeliosTransientWeaponEffect"));
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("EffectRoot")));
    Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Explosion Light"));
    Light->SetupAttachment(GetRootComponent());
    Light->SetLightColor(FLinearColor(1.0f, 0.28f, 0.045f));
    Light->SetIntensity(28000.0f);
    Light->SetAttenuationRadius(1200.0f);
    Light->SetCastShadows(false);
}

void AHeliosExplosionEffect::SetEffect(UNiagaraSystem* System, float BlastRadius, const FVector& ImpactNormal)
{
    const FVector Normal=ImpactNormal.GetSafeNormal(UE_SMALL_NUMBER,FVector::UpVector);
    if(auto* FX=UHeliosWeaponVFX::SpawnOneShot(this,System,GetActorLocation()+Normal*8.0f,
        Normal.Rotation(),FVector(FMath::Max(BlastRadius,1.0f)/2000.0f),1.2f))
    {
        FX->SetVariableFloat(TEXT("User.BlastRadius"),BlastRadius);
        FX->SetVariableVec3(TEXT("User.ImpactNormal"),Normal);
    }
}

void AHeliosExplosionEffect::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Age += DeltaSeconds;
    const float Alpha = FMath::Clamp(Age / 0.25f, 0.0f, 1.0f);
    Light->SetIntensity(FMath::Lerp(28000.0f, 0.0f, Alpha));
}
