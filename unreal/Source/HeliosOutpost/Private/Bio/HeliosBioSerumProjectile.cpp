#include "Bio/HeliosBioSerumProjectile.h"
#include "Bio/HeliosBioHotelGameMode.h"
#include "Bio/HeliosBioRoleDefinition.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

AHeliosBioSerumProjectile::AHeliosBioSerumProjectile()
{
    PrimaryActorTick.bCanEverTick=true;
    Collision=CreateDefaultSubobject<USphereComponent>(TEXT("SerumCollision"));SetRootComponent(Collision);
    Collision->InitSphereRadius(3.f);Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    Collision->SetCanEverAffectNavigation(false);Collision->SetNotifyRigidBodyCollision(true);
    Collision->OnComponentHit.AddDynamic(this,&AHeliosBioSerumProjectile::OnImpact);
    Movement=CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("SerumMovement"));Movement->SetUpdatedComponent(Collision);
    Movement->ProjectileGravityScale=0.f;Movement->bRotationFollowsVelocity=true;
    Movement->bForceSubStepping=true;Movement->MaxSimulationTimeStep=1.f/120.f;Movement->MaxSimulationIterations=256;
    // The actor clamps the remaining travel before movement. MovementComponent's
    // default owner prerequisite has the opposite order and would form a cycle.
    Movement->bTickBeforeOwner=false;
    Movement->AddTickPrerequisiteActor(this);
    // No replacement gun, mesh or invented source effect is displayed on the dart.
    SetLifeSpan(10.f);
}
void AHeliosBioSerumProjectile::Configure(int32 Participant,int32 Round,UHeliosBioRoleDefinition* Definition,const FVector& Direction)
{
    CasterId=Participant;ExpectedRound=Round;SourceRole=Definition;PreviousPosition=GetActorLocation();bConfigured=true;
    Collision->IgnoreActorWhenMoving(GetOwner(),true);
    if(GetOwner()){TArray<AActor*> Attached;GetOwner()->GetAttachedActors(Attached,true,true);for(auto* Actor:Attached)Collision->IgnoreActorWhenMoving(Actor,true);}
    if(!SourceRole||!IsCasterValid()){Cancel(TEXT("invalid_at_launch"));return;}
    Collision->SetSphereRadius(FMath::Max(.1f,SourceRole->SerumProjectileRadius));
    Movement->InitialSpeed=FMath::Max(1.f,SourceRole->SerumProjectileSpeed);Movement->MaxSpeed=Movement->InitialSpeed;
    Movement->Velocity=Direction.GetSafeNormal()*Movement->InitialSpeed;
    SetLifeSpan(FMath::Max(.1f,SourceRole->SecondaryRange/Movement->InitialSpeed)+.1f);
}
bool AHeliosBioSerumProjectile::IsCasterValid() const
{
    const auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());const auto* Caster=Mode?Mode->Model.FindParticipant(CasterId):nullptr;
    return bConfigured&&Mode&&Mode->RoundSerial==ExpectedRound&&Mode->Model.GetPhase()==EHeliosBioPhase::Active&&Caster&&Caster->bAlive&&!Caster->bPermanentlyDead&&Caster->FrozenSeconds<=0&&Caster->Team==EHeliosBioTeam::UltimateHunter&&Caster->HunterRoleId==TEXT("BioRedeemerHunter")&&Mode->ParticipantActor(CasterId)==GetOwner();
}
void AHeliosBioSerumProjectile::Cancel(const TCHAR* Reason)
{
    if(bResolved)return;bResolved=true;
    if(auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld())){++Mode->TotalSerumCancelled;Mode->LastSerumDiagnostic=FString::Printf(TEXT("cancelled caster=%d round=%d reason=%s"),CasterId,ExpectedRound,Reason);}
    UE_LOG(LogTemp,Display,TEXT("BIO_SERUM_CANCEL caster=%d round=%d reason=%s travelled=%.2f"),CasterId,ExpectedRound,Reason,TravelledDistance);Destroy();
}
void AHeliosBioSerumProjectile::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);if(bResolved)return;
    if(!IsCasterValid()){Cancel(TEXT("caster_or_round_changed"));return;}
    TravelledDistance+=FVector::Distance(PreviousPosition,GetActorLocation());PreviousPosition=GetActorLocation();
    if(!SourceRole||TravelledDistance>=SourceRole->SecondaryRange-KINDA_SMALL_NUMBER){Cancel(TEXT("range_exhausted"));return;}
    // Clamp this simulation step to the remaining physical travel; a fast dart
    // cannot hit beyond the configured range between actor/component ticks.
    Movement->MaxSpeed=FMath::Min(FMath::Max(1.f,SourceRole->SerumProjectileSpeed),FMath::Max(0.f,SourceRole->SecondaryRange-TravelledDistance)/FMath::Max(.0001f,DeltaSeconds));
    Movement->Velocity=Movement->Velocity.GetClampedToMaxSize(Movement->MaxSpeed);
}
void AHeliosBioSerumProjectile::OnImpact(UPrimitiveComponent*,AActor* Other,UPrimitiveComponent*,FVector,const FHitResult& Hit)
{Resolve(Other,Hit);}
void AHeliosBioSerumProjectile::ResolveInitialObstruction(const FHitResult& Hit)
{Resolve(Hit.GetActor(),Hit);}
void AHeliosBioSerumProjectile::Resolve(AActor* Other,const FHitResult& Hit)
{
    if(bResolved)return;
    if(!IsCasterValid()){Cancel(TEXT("caster_invalid_at_impact"));return;}
    auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());
    const float ActualDistance=TravelledDistance+FVector::Distance(PreviousPosition,Hit.ImpactPoint);
    if(!SourceRole||ActualDistance>SourceRole->SecondaryRange+KINDA_SMALL_NUMBER){Cancel(TEXT("impact_beyond_range"));return;}
    bResolved=true;Movement->StopMovementImmediately();
    const int32 Victim=Mode->ParticipantId(Other);const auto* Before=Mode->Model.FindParticipant(Victim);
    const float HealthBefore=Before?Before->Health:0.f;const auto TeamBefore=Before?Before->Team:EHeliosBioTeam::Human;
    const auto Result=Mode->Model.ApplyRescueSerum(Victim,CasterId);
    if(Result==EHeliosBioSerumResult::CuredZombie)++Mode->TotalSerumCures;
    else if(Result==EHeliosBioSerumResult::HealedHunter)++Mode->TotalSerumHeals;
    else if(Victim==INDEX_NONE)++Mode->TotalSerumBlocked;
    else ++Mode->TotalSerumRejected;
    const auto* After=Mode->Model.FindParticipant(Victim);
    Mode->LastSerumDiagnostic=FString::Printf(TEXT("caster=%d victim=%d result=%d blocker=%s component=%s health=%.2f->%.2f team=%d->%d travelled=%.2f"),CasterId,Victim,(int32)Result,*GetPathNameSafe(Other),*GetPathNameSafe(Hit.GetComponent()),HealthBefore,After?After->Health:0.f,(int32)TeamBefore,After?(int32)After->Team:-1,ActualDistance);
    UE_LOG(LogTemp,Display,TEXT("BIO_SERUM_IMPACT round=%d %s"),ExpectedRound,*Mode->LastSerumDiagnostic);
    if(Result!=EHeliosBioSerumResult::Rejected)
    {
        if(auto* Sound=SourceRole->SerumCureSound.LoadSynchronous())UGameplayStatics::PlaySoundAtLocation(this,Sound,Hit.ImpactPoint);
        if(Victim==0)Mode->ShowNotice(Result==EHeliosBioSerumResult::CuredZombie?TEXT("救赎血清：已恢复人类与原配装"):TEXT("救赎血清：猎手生命已回满"));
    }
    // Actor inventory/presentation follows the real team transition on its normal
    // tick. Healing a hunter never resets its loadout, identity, cooldown or scale.
    Destroy();
}
