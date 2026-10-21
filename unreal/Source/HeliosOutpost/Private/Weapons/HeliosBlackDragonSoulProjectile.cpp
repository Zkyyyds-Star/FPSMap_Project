#include "Weapons/HeliosBlackDragonSoul.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Weapons/HeliosWeaponHolderContext.h"
#include "Weapons/HeliosImpactPresentationSubsystem.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Pets/HeliosPetDog.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Engine/OverlapResult.h"

namespace
{
constexpr const TCHAR* Library=TEXT("/Game/Helios/CFMBlackDragonSoul/Effects/DA_HLH_Effects");
void IgnoreFriends(UWorld* World,FCollisionQueryParams& Query)
{
    Query.AddIgnoredActor(UGameplayStatics::GetPlayerPawn(World,0));
    if(auto* Pets=World->GetSubsystem<UHeliosPetWorld>())Pets->IgnoreAll(Query);
    for(TActorIterator<AHeliosPetCompanion> It(World);It;++It)Query.AddIgnoredActor(*It);
    for(TActorIterator<AHeliosEnemyActor> It(World);It;++It)
        if(It->ActorHasTag(TEXT("Friendly"))||It->IsEnemyDead())Query.AddIgnoredActor(*It);
}
}
AHeliosBlackDragonSoulProjectile::AHeliosBlackDragonSoulProjectile()
{
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickGroup=TG_PostPhysics;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Original flame projectile"));
    SetActorEnableCollision(false);
}
void AHeliosBlackDragonSoulProjectile::InitializeShot(const UHeliosBlackDragonSoulDefinition* D,const FVector& Direction,AActor* SourceWeapon)
{
    Heading=Direction.GetSafeNormal(); Source=SourceWeapon;
    Damage=FMath::Max(0.f,D->Damage); MinimumDamage=FMath::Clamp(D->MinimumDamage,0.f,Damage);
    Inner=FMath::Max(0.f,D->ExplosionInnerRadius); Outer=FMath::Max(Inner+.01f,D->ExplosionOuterRadius);
    Range=FMath::Max(1.f,D->Range); Speed=FMath::Max(1.f,D->ProjectileSpeed); KnockbackSpeed=D->KnockbackSpeed;
    ProjectileEffect=D->ProjectileEffectName; ExplosionEffect=D->ExplosionEffectName;
    if(HeliosWeaponHolderContext::IsCompanion(GetOwner()))ProjectileEffect.ReplaceInline(TEXT("1P"),TEXT("3P"));
}
void AHeliosBlackDragonSoulProjectile::BeginPlay()
{
    Super::BeginPlay();
    LastMovementTime=GetWorld()->GetTimeSeconds();
    auto* State=UHeliosBlackDragonSoulState::Get(GetOwner()); State->TrackWorldActor(this);
    FlightEffect=AHeliosSSXEffect::Spawn(GetOwner(),ProjectileEffect,GetActorLocation(),Heading.Rotation(),false,1,Library);
    if(FlightEffect)
    {
        FlightEffect->AttachToActor(this,FAttachmentTransformRules::KeepWorldTransform);
        FlightEffect->FollowTravelingEmitter(); State->TrackWorldActor(FlightEffect);
    }
    UE_LOG(LogTemp,Display,TEXT("HLH_PROJECTILE speed=%.0f range=%.0f flight_effect=%s"),Speed,Range,FlightEffect?TEXT("ready"):TEXT("missing"));
}
void AHeliosBlackDragonSoulProjectile::Tick(float Dt)
{
    Super::Tick(Dt); if(bExploded||GetWorld()->IsPaused())return;
    if(HeliosWeaponHolderContext::IsDead(GetOwner())){Destroy();return;}
    // A shot spawned late in a frame must not consume time before it existed.
    const double Now=GetWorld()->GetTimeSeconds();
    const float Elapsed=FMath::Max(0.f,float(Now-LastMovementTime));LastMovementTime=Now;
    if(Elapsed<=0)return;
    const FVector Start=GetActorLocation(); const float Step=FMath::Min(Speed*Elapsed,Range-Travelled);
    const FVector End=Start+Heading*Step;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BlackDragonSoulFlight),true,GetOwner());
    Query.AddIgnoredActor(this); if(Source.IsValid())Query.AddIgnoredActor(Source.Get()); IgnoreFriends(GetWorld(),Query);
    FHitResult Hit;
    if(GetWorld()->SweepSingleByChannel(Hit,Start,End,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(8.f),Query))
    {Travelled+=FVector::Distance(Start,Hit.Location); SetActorLocation(Hit.Location); Explode(Hit.ImpactPoint,Hit.ImpactNormal);return;}
    SetActorLocation(End); Travelled+=Step;
    if(Travelled>=Range-.01f)Explode(End,-Heading);
}
void AHeliosBlackDragonSoulProjectile::Explode(const FVector& Center,const FVector& Normal)
{
    if(bExploded||!GetWorld()||HeliosWeaponHolderContext::IsDead(GetOwner()))return;
    bExploded=true;
    auto* State=UHeliosBlackDragonSoulState::Get(GetOwner()); ++State->Explosions; State->LastExplosion=Center;
    if(FlightEffect){FlightEffect->Destroy();FlightEffect=nullptr;}
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BlackDragonSoulBlast),false,GetOwner());
    Query.AddIgnoredActor(this); if(Source.IsValid())Query.AddIgnoredActor(Source.Get()); IgnoreFriends(GetWorld(),Query);
    FCollisionObjectQueryParams Types; Types.AddObjectTypesToQuery(ECC_Pawn);
    TArray<FOverlapResult> Overlaps;
    GetWorld()->OverlapMultiByObjectType(Overlaps,Center,FQuat::Identity,Types,FCollisionShape::MakeSphere(Outer),Query);
    // Other pawns and corpses cannot shield a group from an explosion; cover can.
    FCollisionQueryParams Cover=Query; Cover.bTraceComplex=true;
    for(TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It)Cover.AddIgnoredActor(*It);
    TSet<AHeliosEnemyActor*> Damaged; const FVector LosStart=Center+Normal.GetSafeNormal()*5;
    for(const auto& Overlap:Overlaps)
    {
        auto* Enemy=Cast<AHeliosEnemyActor>(Overlap.GetActor());
        if(!IsValid(Enemy)||Enemy->IsEnemyDead()||Enemy->ActorHasTag(TEXT("Friendly"))||Damaged.Contains(Enemy))continue;
        const float Distance=FVector::Distance(Center,Enemy->GetActorLocation()); if(Distance>Outer)continue;
        FHitResult Block; if(GetWorld()->LineTraceSingleByChannel(Block,LosStart,Enemy->GetActorLocation(),ECC_Visibility,Cover))continue;
        const float Alpha=FMath::Clamp((Distance-Inner)/(Outer-Inner),0.f,1.f);
        const float HitDamage=FMath::Lerp(Damage,MinimumDamage,Alpha); const float Before=Enemy->CurrentHP;
        Damaged.Add(Enemy);
        UGameplayStatics::ApplyDamage(Enemy,HitDamage,GetInstigatorController(),this,UDamageType::StaticClass());
        if(Enemy->CurrentHP>=Before)continue;
        ++State->EnemyHits;
        if(Enemy->IsEnemyDead())HeliosWeaponHolderContext::PlayCharacterCue(GetOwner(),TEXT("Kill"));
        else if(Enemy->EnemyType!=EHeliosEnemyType::E05_Boss)
        {
            FVector Away=(Enemy->GetActorLocation()-Center).GetSafeNormal2D();if(Away.IsNearlyZero())Away=Heading.GetSafeNormal2D();
            if(auto* Controller=Enemy->GetController())Controller->StopMovement();
            Enemy->LaunchCharacter(Away*KnockbackSpeed,true,false);++State->Knockbacks;
        }
        UE_LOG(LogTemp,Display,TEXT("HLH_BLAST_HIT target=%s damage=%.3f distance=%.3f hp=%.3f"),*Enemy->GetName(),Before-Enemy->CurrentHP,Distance,Enemy->CurrentHP);
    }
    if(auto* Effect=AHeliosSSXEffect::Spawn(GetOwner(),ExplosionEffect,Center,FRotator::ZeroRotator,false,1,Library))
    { Effect->SetActorScale3D(FVector(Outer/800.f));Effect->ResetBirthAnchors();State->TrackWorldActor(Effect); }
    UE_LOG(LogTemp,Display,TEXT("HLH_BLAST center=%s radius=%.0f unique_targets=%d range_travelled=%.3f"),*Center.ToString(),Outer,Damaged.Num(),Travelled);
    Destroy();
}
void AHeliosBlackDragonSoulProjectile::EndPlay(const EEndPlayReason::Type Reason)
{ if(IsValid(FlightEffect))FlightEffect->Destroy();FlightEffect=nullptr;Super::EndPlay(Reason); }
