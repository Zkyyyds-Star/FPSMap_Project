#include "Weapons/HeliosColaWeapon.h"
#include "Audio/HeliosAudio.h"
#include "Audio/HeliosAudioSettingsSubsystem.h"
#include "Weapons/HeliosWeaponDamageSettings.h"
#include "Weapons/HeliosColaVFXSettings.h"
#include "Player/HeliosArenaCharacter.h"
#include "Weapons/HeliosFirstPersonPresentationComponent.h"
#include "Weapons/HeliosWeaponVFX.h"
#include "Components/AudioComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "TimerManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
template<typename T> T* ColaArt(const TCHAR* Name)
{ return LoadObject<T>(nullptr, *(FString(TEXT("/Game/Helios/Weapons/Cola/"))+Name)); }
void Cosmetic(UStaticMeshComponent* Mesh)
{
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetGenerateOverlapEvents(false); Mesh->SetCanEverAffectNavigation(false); Mesh->SetCastShadow(false);
}
UHeliosFirstPersonPresentationComponent* Presentation(APawn* Pawn)
{ return Pawn ? Pawn->FindComponentByClass<UHeliosFirstPersonPresentationComponent>() : nullptr; }
const TArray<FHeliosDrinkVariant>& Drinks(const UHeliosColaWeaponDefinition* Definition)
{
    return Definition && !Definition->DrinkVariants.IsEmpty() ? Definition->DrinkVariants : GetDefault<UHeliosColaWeaponDefinition>()->DrinkVariants;
}
void ApplyDrinkLabel(UMeshComponent* Mesh, UMaterialInterface* Material, bool bMirror)
{
    if(!Mesh || !Material)return;
    const int32 Slot=Mesh->GetMaterialIndex(TEXT("M_DrinkLabel"));
    if(Slot==INDEX_NONE)return;
    auto* Label=UMaterialInstanceDynamic::Create(Material,Mesh);
    Label->SetScalarParameterValue(TEXT("FlipU"),bMirror?1.f:0.f);
    Mesh->SetMaterial(Slot,Label);
}
}

UHeliosColaWeaponDefinition::UHeliosColaWeaponDefinition()
{
    WeaponId=TEXT("cola"); DisplayName=NSLOCTEXT("HeliosWeapons","DrinkName","饮料手雷");
    Description=NSLOCTEXT("HeliosWeapons","DrinkDescription","可乐、雪碧、芬达、啤酒、红牛，逐罐随机！单击左键投掷，碰撞爆破；每组 20 罐，按 R 补充。");
    const TCHAR* Ids[]={TEXT("cola"),TEXT("sprite"),TEXT("fanta"),TEXT("beer"),TEXT("redbull")};
    const TCHAR* Names[]={TEXT("可乐"),TEXT("雪碧"),TEXT("芬达"),TEXT("啤酒"),TEXT("红牛")};
    for(int32 I=0;I<5;++I)
    {
        FHeliosDrinkVariant V;V.Id=Ids[I];V.DisplayName=FText::FromString(Names[I]);
        V.LabelMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(FString(TEXT("/Game/Helios/Weapons/Cola/MI_Drink_"))+Ids[I]));
        DrinkVariants.Add(V);
    }
    WeaponClass=AHeliosColaWeapon::StaticClass(); MagazineSize=20; ReloadSeconds=1.8f;
    Damage=360; MinimumDamage=80; ProjectileSpeed=3200; RefireSeconds=1.1f; Recoil=0.08f;
    Range=1500; MuzzleOffset=0; ModelMuzzleLocation=FVector(2,0,6);
    AttachmentLocation=FVector(34,13,-9); AttachmentScale=FVector::OneVector;
}

AHeliosColaProjectile::AHeliosColaProjectile()
{
    bUseTemplateDamageSettings = false;
    PrimaryActorTick.bCanEverTick=true; HitDamage=360; bDamageOwner=false; bExplodeOnHit=false;
    InitialLifeSpan=10;
    auto* Collision=FindComponentByClass<USphereComponent>(); Collision->SetSphereRadius(6.f);
    auto* Movement=FindComponentByClass<UProjectileMovementComponent>();
    Movement->InitialSpeed=0; Movement->MaxSpeed=0; Movement->bInitialVelocityInLocalSpace=false;
    Movement->ProjectileGravityScale=1; Movement->bShouldBounce=false;
    Movement->Bounciness=.45f; Movement->Friction=.25f; Movement->BounceVelocityStopSimulatingThreshold=45;
    Movement->bForceSubStepping=true; Movement->MaxSimulationTimeStep=1.f/120; Movement->MaxSimulationIterations=12;
    Visual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Red cola can"));
    Visual->SetupAttachment(GetRootComponent()); Cosmetic(Visual);
    Hiss=CreateDefaultSubobject<UAudioComponent>(TEXT("Pressure hiss")); Hiss->SetupAttachment(GetRootComponent());
    Hiss->bAutoActivate=false; Hiss->bAutoDestroy=false; Hiss->bOverrideAttenuation=true;
    Hiss->AttenuationOverrides.bAttenuate=true; Hiss->AttenuationOverrides.AttenuationShapeExtents=FVector(100);
    Hiss->AttenuationOverrides.FalloffDistance=1800; Hiss->AttenuationOverrides.bSpatialize=true;
}

void AHeliosColaProjectile::Configure(const FVector& Direction,const UHeliosColaWeaponDefinition* Definition,int32 DrinkIndex)
{
    const auto* D=Definition ? Definition : GetDefault<UHeliosColaWeaponDefinition>();
    Fuse=FMath::Max(.01f,D->FuseSeconds); Inner=FMath::Max(0.f,D->InnerRadius);
    Outer=FMath::Max(Inner+1,D->OuterRadius); HitDamage=FMath::Max(0.f,D->Damage);
    MinDamage=FMath::Clamp(D->MinimumDamage,0.f,HitDamage);
    auto* Movement=FindComponentByClass<UProjectileMovementComponent>();
    Movement->Velocity=Direction.GetSafeNormal()*D->ProjectileSpeed+FVector(0,0,D->UpwardSpeed);
    Movement->Bounciness=FMath::Clamp(D->Bounciness,0.f,1.f); Movement->Friction=FMath::Clamp(D->BounceFriction,0.f,1.f);
    auto* Collision=FindComponentByClass<USphereComponent>();
    Collision->IgnoreActorWhenMoving(GetInstigator(),true); Collision->IgnoreActorWhenMoving(GetOwner(),true);
    Visual->SetStaticMesh(ColaArt<UStaticMesh>(TEXT("SM_ColaCan")));
    const auto& Variants=Drinks(D);
    const auto& Variant=Variants[Variants.IsValidIndex(DrinkIndex)?DrinkIndex:0];
    DrinkId=Variant.Id;ApplyDrinkLabel(Visual,Variant.LabelMaterial.LoadSynchronous(),false);
    Foam=ColaArt<UNiagaraSystem>(TEXT("NS_ColaFoam")); Spray=ColaArt<UNiagaraSystem>(TEXT("NS_ColaSpray"));
    FoamAir=ColaArt<UNiagaraSystem>(TEXT("NS_ColaFoamAir"));SprayAir=ColaArt<UNiagaraSystem>(TEXT("NS_ColaSprayAir"));
    Bubbles=ColaArt<UNiagaraSystem>(TEXT("NS_ColaBubbles"));
    BounceSound=ColaArt<USoundBase>(TEXT("SFX_ColaBounce")); ExplosionSound=ColaArt<USoundBase>(TEXT("SFX_ColaExplosion"));
    Hiss->SetSound(ColaArt<USoundBase>(TEXT("SFX_ColaHiss")));
}

void AHeliosColaProjectile::BeginPlay()
{
    Super::BeginPlay();
    // Impact triggers detonation; lifespan cleans up cans that never hit anything.
    if(auto* Audio=UHeliosAudioSettingsSubsystem::Get(this))Audio->Route(Hiss,this,Hiss->Sound);
    Hiss->SetVolumeMultiplier(.08f); Hiss->Play();
}
void AHeliosColaProjectile::NotifyHit(UPrimitiveComponent*,AActor* Other,UPrimitiveComponent*,bool,FVector HitLocation,FVector HitNormal,FVector,const FHitResult&)
{
    // Use the cola radial blast once, without base direct-hit damage.
    if(bHit || Other==GetOwner() || Other==GetInstigator())return;
    bSurfaceImpact=Other&&!Other->IsA<APawn>()&&!HitNormal.IsNearlyZero();
    ImpactSurfaceNormal=HitNormal.GetSafeNormal();ImpactSurfacePoint=HitLocation;
    Explode();
}
void AHeliosColaProjectile::Tick(float Dt)
{
    Super::Tick(Dt); if(bHit)return; Age+=Dt; BubbleAge+=Dt;
    if(GetVelocity().SizeSquared()>2000)Visual->AddLocalRotation(FRotator(370*Dt,110*Dt,220*Dt));
    Hiss->SetVolumeMultiplier(.08f+.55f*FMath::Square(FMath::Clamp(Age/Fuse,0.f,1.f)));
    if(Bubbles && BubbleAge>=.16f)
    {
        BubbleAge=0;
        UHeliosWeaponVFX::SpawnOneShot(this,Bubbles,GetActorLocation()+FVector(0,0,5),FRotator(90,0,0),FVector::OneVector,.5f);
    }
}
void AHeliosColaProjectile::Explode()
{
    if(bHit || !GetWorld())return;
    bHit=true; SetActorEnableCollision(false); Hiss->Stop(); Visual->SetVisibility(false);
    FindComponentByClass<UProjectileMovementComponent>()->StopMovementImmediately();
    const FVector Center=GetActorLocation();
    FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_Pawn);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ColaBlast),false,this);
    Query.AddIgnoredActor(GetOwner()); Query.AddIgnoredActor(GetInstigator());
    TArray<FOverlapResult> Overlaps;
    GetWorld()->OverlapMultiByObjectType(Overlaps,Center,FQuat::Identity,Objects,FCollisionShape::MakeSphere(Outer),Query);
    TSet<AActor*> Damaged;
    for(const FOverlapResult& Overlap:Overlaps)
    {
        AActor* Target=Overlap.GetActor();
        if(!IsValid(Target)||Damaged.Contains(Target)||Target==GetOwner()||Target==GetInstigator()||Target->ActorHasTag(TEXT("Player")))continue;
        if(GetOwner()&&GetOwner()->ActorHasTag(TEXT("Enemy"))&&Target->ActorHasTag(TEXT("Enemy")))continue;
        Damaged.Add(Target);
        const FVector TargetPoint=Target->GetActorLocation(); const float Distance=FVector::Distance(Center,TargetPoint);
        if(Distance>Outer)continue;
        FHitResult Block;
        if(GetWorld()->LineTraceSingleByChannel(Block,Center,TargetPoint,ECC_Visibility,Query)&&Block.GetActor()!=Target)continue;
        const float Damage=FMath::Lerp(HitDamage,MinDamage,FMath::Clamp((Distance-Inner)/(Outer-Inner),0.f,1.f));
        UGameplayStatics::ApplyDamage(Target,Damage,GetInstigatorController(),this,HitDamageType);
    }
    MakeNoise(3,GetInstigator(),Center,1800,TEXT("ColaExplosion"));
    // Surface impacts spray away from the receiving plane; pawn/air impacts radiate.
    const FRotator BurstRotation=bSurfaceImpact?FRotationMatrix::MakeFromZ(ImpactSurfaceNormal).Rotator():FRotator::ZeroRotator;
    const FVector BurstScale(Outer/HeliosColaVFX::Envelope());
    UMaterialInstanceDynamic* BurstMaterials[3]={};
    for(int32 M=0;M<3;++M)
    {
        BurstMaterials[M]=UMaterialInstanceDynamic::Create(ColaArt<UMaterialInterface>(HeliosColaVFX::MaterialNames[M]),GetWorld());
        if(auto* Material=BurstMaterials[M])
        {
            Material->SetVectorParameterValue(TEXT("SurfacePoint"),FLinearColor(ImpactSurfacePoint));
            Material->SetVectorParameterValue(TEXT("SurfaceNormal"),FLinearColor(ImpactSurfaceNormal));
            Material->SetScalarParameterValue(TEXT("SurfaceFadeEnabled"),bSurfaceImpact?1.f:0.f);
        }
    }
    for(UNiagaraSystem* Effect:{bSurfaceImpact?Foam.Get():FoamAir.Get(),bSurfaceImpact?Spray.Get():SprayAir.Get()})
        if(auto* Burst=UHeliosWeaponVFX::SpawnOneShot(this,Effect,Center,BurstRotation,BurstScale,HeliosColaVFX::ReleaseSeconds))
            for(int32 M=0;M<3;++M)if(BurstMaterials[M])Burst->SetVariableMaterial(HeliosColaVFX::MaterialParameters[M],BurstMaterials[M]);
    if(ExplosionSound)HeliosAudio::PlaySoundAtLocation(this,ExplosionSound,Center,.9f);
    FHitResult Floor;
    if(GetWorld()->LineTraceSingleByChannel(Floor,Center+FVector(0,0,4),Center-FVector(0,0,600),ECC_Visibility,Query))
    {
        const float Height=FVector::DotProduct(Center-Floor.ImpactPoint,Floor.ImpactNormal);
        const float GroundRadius=FMath::Sqrt(FMath::Max(0.f,Outer*Outer-Height*Height));
        const FVector GroundCenter=Center-Floor.ImpactNormal*Height;
        const FTransform Transform(FRotationMatrix::MakeFromZ(Floor.ImpactNormal).Rotator(),GroundCenter+Floor.ImpactNormal*1.5f);
        if(auto* SplashActor=GetWorld()->SpawnActorDeferred<AHeliosColaSplash>(AHeliosColaSplash::StaticClass(),Transform,
            GetOwner(),GetInstigator(),ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
        {
            SplashActor->ConfigureBlastRadius(GroundRadius);
            UGameplayStatics::FinishSpawningActor(SplashActor,Transform);
        }
    }
    Destroy();
}
void AHeliosColaProjectile::EndPlay(EEndPlayReason::Type Reason)
{
    GetWorld()->GetTimerManager().ClearTimer(FuseTimer); Hiss->Stop(); Super::EndPlay(Reason);
}

AHeliosColaWeapon::AHeliosColaWeapon()
{
    MagazineSize=20; RefireRate=1.1f; FiringRecoil=.08f; bFullAuto=false; MuzzleOffset=0;
    MeshAssetPath=TEXT("/Game/Helios/Weapons/Cola/SM_ColaCan");
    FireSoundPath=TEXT("/Game/Helios/Weapons/Cola/SFX_ColaThrow");
    ReloadSoundPath=TEXT("/Game/Helios/Weapons/Cola/SFX_ColaReload");
}
void AHeliosColaWeapon::ApplyDefinition(const UHeliosWeaponDefinition* D)
{
    Super::ApplyDefinition(D); D=ActiveDefinition; ColaDefinition=const_cast<UHeliosColaWeaponDefinition*>(Cast<UHeliosColaWeaponDefinition>(D));
    if(!ColaDefinition){ColaDefinition=NewObject<UHeliosColaWeaponDefinition>(this);ApplyWeaponDamageSettings(this,ColaDefinition);}
    const UHeliosColaWeaponDefinition* C=ColaDefinition;
    RefireRate=FMath::Max(C->RefireSeconds,C->ReleaseSeconds); FiringRecoil=C->Recoil;
    int32 Seed=static_cast<int32>(FPlatformTime::Cycles())^static_cast<int32>(GetUniqueID());
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosColaQA")))FParse::Value(FCommandLine::Get(),TEXT("HeliosDrinkSeed="),Seed);
    DrinkRandom.Initialize(Seed);DrinkDrawCount=0;DrawDrink();
}
FName AHeliosColaWeapon::GetDrinkId() const
{
    const auto& Variants=Drinks(ColaDefinition);
    return Variants.IsValidIndex(CurrentDrink)?Variants[CurrentDrink].Id:NAME_None;
}
void AHeliosColaWeapon::DrawDrink()
{
    CurrentDrink=DrinkRandom.RandRange(0,Drinks(ColaDefinition).Num()-1);++DrinkDrawCount;
    RefreshDrinkPresentation();
}
void AHeliosColaWeapon::RefreshDrinkPresentation()
{
    const auto& Variants=Drinks(ColaDefinition);if(!Variants.IsValidIndex(CurrentDrink))return;
    UMaterialInterface* Material=Variants[CurrentDrink].LabelMaterial.LoadSynchronous();
    ApplyDrinkLabel(GetFirstPersonMesh(),Material,HasExternalPresentation());
    ApplyDrinkLabel(FirstPersonModel,Material,false);ApplyDrinkLabel(ThirdPersonModel,Material,false);
}
bool AHeliosColaWeapon::SelectDrinkForQA(int32 Index)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("HeliosColaQA"))||!Drinks(ColaDefinition).IsValidIndex(Index)||bPending)return false;
    CurrentDrink=Index;RefreshDrinkPresentation();return true;
}
void AHeliosColaWeapon::StartFiring()
{
    if(bInputHeld)return; bInputHeld=true;
    const auto* Player=Cast<AHeliosArenaCharacter>(PawnOwner);
    if(!GetWorld()||!WeaponOwner||!PawnOwner||CurrentBullets<=0||bPending||
        (Player&&(Player->IsDead()||Player->IsReloading()))||GetWorld()->IsPaused())return;
    const double Now=GetWorld()->GetTimeSeconds(); if(Now-LastThrow<RefireRate)return;
    LastThrow=Now; bPending=true; RecoverAge=0;
    if(auto* P=Presentation(PawnOwner))P->BeginThrow();
    PlayWeaponSound(ColaArt<USoundBase>(TEXT("SFX_ColaPrepare")),.75f);
    const float Delay=ColaDefinition ? ColaDefinition->ReleaseSeconds : .35f;
    GetWorld()->GetTimerManager().SetTimer(ReleaseTimer,this,&AHeliosColaWeapon::Release,FMath::Max(.01f,Delay),false);
}
void AHeliosColaWeapon::StopFiring(){bInputHeld=false; bIsFiring=false;}
void AHeliosColaWeapon::Release()
{
    if(!bPending)return; bPending=false;
    const auto* Player=Cast<AHeliosArenaCharacter>(PawnOwner);
    if(!PawnOwner||!WeaponOwner||CurrentBullets<=0||(Player&&(Player->IsDead()||Player->IsReloading()))) {CancelThrow();return;}
    FVector View; FRotator Aim; PawnOwner->GetActorEyesViewPoint(View,Aim);
    FVector Start=GetVisualMuzzleLocation();
    // A sphere sweep includes the hand, can and near wall. Never teleport through cover.
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ColaRelease),false,PawnOwner); Query.AddIgnoredActor(this);
    FHitResult Block;
    if(GetWorld()->SweepSingleByChannel(Block,View,Start,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(6.f),Query))
    {CancelThrow();return;}
    if(GetWorld()->OverlapBlockingTestByChannel(Start,FQuat::Identity,ECC_WorldDynamic,FCollisionShape::MakeSphere(6.f),Query))
    {CancelThrow();return;}
    const FTransform Transform(Aim,Start);
    auto* Can=GetWorld()->SpawnActorDeferred<AHeliosColaProjectile>(AHeliosColaProjectile::StaticClass(),Transform,
        GetOwner(),PawnOwner,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Can){CancelThrow();return;}
    Can->Configure(Aim.Vector(),ColaDefinition,CurrentDrink); UGameplayStatics::FinishSpawningActor(Can,Transform);
    if(!IsValid(Can)||Can->IsActorBeingDestroyed()){CancelThrow();return;}
    --CurrentBullets; bShotCommitted=true;
    WeaponOwner->UpdateWeaponHUD(CurrentBullets,MagazineSize); WeaponOwner->AddWeaponRecoil(FiringRecoil);
    if(auto* P=Presentation(PawnOwner))P->CommitThrow();
    if(CurrentBullets>0)DrawDrink();
    OnProjectilePresented.Broadcast(Can); OnShotPresented.Broadcast();
    MakeNoise(1,PawnOwner,Start,300,NoiseOwnerTag);
}
void AHeliosColaWeapon::CancelThrow()
{
    if(GetWorld())GetWorld()->GetTimerManager().ClearTimer(ReleaseTimer);
    bPending=false;
    if(auto* P=Presentation(PawnOwner))P->CancelThrow();
}
void AHeliosColaWeapon::BeginReload(float Duration){CancelThrow(); Super::BeginReload(Duration);}
void AHeliosColaWeapon::ReloadMagazine()
{
    Super::ReloadMagazine();DrawDrink();
    // Show the replacement wrapper only as a fresh can is raised into view.
    if(auto* P=Presentation(PawnOwner);P&&P->IsReady())P->ReplayEquip();
}
void AHeliosColaWeapon::CancelReload(){CancelThrow();Super::CancelReload();}
void AHeliosColaWeapon::DeactivateWeapon(){CancelThrow();Super::DeactivateWeapon();}
void AHeliosColaWeapon::EndPlay(EEndPlayReason::Type Reason){CancelThrow();Super::EndPlay(Reason);}
void AHeliosColaWeapon::Tick(float Dt)
{
    Super::Tick(Dt); RecoverAge+=Dt;
    if(!HasExternalPresentation())FirstPersonModel->SetVisibility(CurrentBullets>0&&(bPending||RecoverAge>.9f));
}

AHeliosColaSplash::AHeliosColaSplash()
{
    PrimaryActorTick.bCanEverTick=true; InitialLifeSpan=4;
    RootComponent=Splash=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Cola puddle")); Cosmetic(Splash);
}
void AHeliosColaSplash::BeginPlay()
{
    Super::BeginPlay(); Splash->SetStaticMesh(ColaArt<UStaticMesh>(TEXT("SM_ColaSplash")));
    SplashMaterial=UMaterialInstanceDynamic::Create(ColaArt<UMaterialInterface>(TEXT("M_ColaSplash")),this);
    if(SplashMaterial)Splash->SetMaterial(0,SplashMaterial);
}
void AHeliosColaSplash::Tick(float Dt)
{
    Super::Tick(Dt); Age+=Dt;
    if(SplashMaterial)SplashMaterial->SetScalarParameterValue(TEXT("Opacity"),.6f*FMath::Clamp((4-Age)/1.5f,0.f,1.f));
}
