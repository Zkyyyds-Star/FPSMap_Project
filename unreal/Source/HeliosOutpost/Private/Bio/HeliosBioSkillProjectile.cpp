#include "Bio/HeliosBioSkillProjectile.h"
#include "Bio/HeliosBioHotelGameMode.h"
#include "Bio/HeliosBioRoleDefinition.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/World.h"

AHeliosBioSkillProjectile::AHeliosBioSkillProjectile()
{
    PrimaryActorTick.bCanEverTick=true;
    Collision=CreateDefaultSubobject<USphereComponent>(TEXT("SkillCollision"));SetRootComponent(Collision);
    Collision->InitSphereRadius(12);Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    Collision->SetNotifyRigidBodyCollision(true);Collision->SetCanEverAffectNavigation(false);
    Collision->OnComponentHit.AddDynamic(this,&AHeliosBioSkillProjectile::OnImpact);
    Visual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SourceSkillVisual"));Visual->SetupAttachment(Collision);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);Visual->SetCanEverAffectNavigation(false);
    Movement=CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("SkillMovement"));Movement->UpdatedComponent=Collision;
    Movement->InitialSpeed=1200;Movement->MaxSpeed=1600;Movement->bRotationFollowsVelocity=true;
    SetLifeSpan(4.f);
}
void AHeliosBioSkillProjectile::Configure(int32 Participant,int32 Round,UHeliosBioRoleDefinition* Definition,const FVector& Direction,bool Arcane,bool SwordWave)
{
    CasterId=Participant;ExpectedRound=Round;SourceRole=Definition;bArcane=Arcane;bSwordWave=SwordWave;
    Collision->IgnoreActorWhenMoving(GetOwner(),true);
    if(GetOwner()){TArray<AActor*> Attached;GetOwner()->GetAttachedActors(Attached,true,true);for(auto A:Attached)Collision->IgnoreActorWhenMoving(A,true);}
    if(SourceRole)
    {
        Visual->SetStaticMesh(bArcane?SourceRole->SecondaryProjectileMesh.LoadSynchronous():bSwordWave?nullptr:SourceRole->SkillProjectileMesh.LoadSynchronous());
        const bool Grenade=bArcane||SourceRole->Ability==EHeliosBioAbility::FreezeGrenade;
        Movement->ProjectileGravityScale=Grenade?1.f:0.f;
        Movement->Velocity=Direction.GetSafeNormal()*1200.f+(Grenade?FVector(0,0,200):FVector::ZeroVector);
        if(bSwordWave&&!SourceRole->SwordWaveEffect.IsEmpty()&&!SourceRole->SkillEffectLibrary.IsEmpty())
            if(auto* FX=AHeliosSSXEffect::Spawn(this,*SourceRole->SwordWaveEffect,GetActorLocation(),GetActorRotation(),false,1.f,*SourceRole->SkillEffectLibrary))FX->AttachToComponent(Collision,FAttachmentTransformRules::KeepWorldTransform);
        if(bArcane&&!SourceRole->SecondaryProjectileEffect.IsEmpty()&&!SourceRole->SkillEffectLibrary.IsEmpty())
            if(auto* FX=AHeliosSSXEffect::Spawn(this,*SourceRole->SecondaryProjectileEffect,GetActorLocation(),GetActorRotation(),false,1.f,*SourceRole->SkillEffectLibrary))FX->AttachToComponent(Collision,FAttachmentTransformRules::KeepWorldTransform);
    }
}
void AHeliosBioSkillProjectile::Tick(float Dt)
{
    Super::Tick(Dt);auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());
    if(!Mode||Mode->RoundSerial!=ExpectedRound||Mode->Model.GetPhase()!=EHeliosBioPhase::Active){Destroy();return;}
    // Grenades have a fuse even when they do not strike a surface.
    if(SourceRole&&(bArcane||SourceRole->Ability==EHeliosBioAbility::FreezeGrenade)&&GetGameTimeSinceCreation()>=1.5f)Resolve(GetActorLocation());
}
void AHeliosBioSkillProjectile::OnImpact(UPrimitiveComponent*,AActor*,UPrimitiveComponent*,FVector,const FHitResult& Hit)
{Resolve(Hit.ImpactPoint);}
void AHeliosBioSkillProjectile::Resolve(const FVector& Position)
{
    if(bResolved)return;bResolved=true;
    auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());
    if(!Mode||!SourceRole||Mode->RoundSerial!=ExpectedRound||Mode->Model.GetPhase()!=EHeliosBioPhase::Active){Destroy();return;}
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BioSkillProjectileLOS),false,this);Query.AddIgnoredActor(GetOwner());
    for(const auto& P:Mode->Model.GetParticipants())
    {
        auto* Victim=Mode->ParticipantActor(P.Id);
        const float Range=bArcane||bSwordWave?SourceRole->SecondaryRange:SourceRole->SkillRange;
        if(!P.bAlive||!Victim||!Mode->AreOpponents(CasterId,P.Id)||FVector::DistSquared(Position,Victim->GetActorLocation())>FMath::Square(Range))continue;
        if(bSwordWave&&FVector::DotProduct((Victim->GetActorLocation()-Position).GetSafeNormal(),GetActorForwardVector())<-.1f)continue;
        FHitResult Obstruction;const FVector Origin=Position+FVector(0,0,15);
        if(GetWorld()->LineTraceSingleByChannel(Obstruction,Origin,Victim->GetActorLocation(),ECC_Visibility,Query)&&Obstruction.GetActor()!=Victim)continue;
        if(SourceRole->Ability==EHeliosBioAbility::FreezeGrenade&&!bArcane&&!bSwordWave)Mode->Model.FreezeParticipant(P.Id,CasterId,SourceRole->SkillDuration);
        else if(Mode->DamageParticipant(P.Id,CasterId,bArcane||bSwordWave?SourceRole->SecondaryDamage:SourceRole->SkillDamage,false)&&bArcane)Mode->Model.DevolveDefeatedZombie(P.Id);
    }
    UE_LOG(LogTemp,Display,TEXT("BIO_ROLE_PROJECTILE_RESOLVED caster=%d ability=%d round=%d"),CasterId,(int32)SourceRole->Ability,ExpectedRound);
    Destroy();
}

