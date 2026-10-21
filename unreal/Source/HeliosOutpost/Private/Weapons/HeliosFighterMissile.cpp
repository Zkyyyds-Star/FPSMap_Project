#include "Weapons/HeliosFighterWeapon.h"
#include "Weapons/HeliosWeaponVFX.h"
#include "Player/HeliosArenaCharacter.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Pets/HeliosPetDog.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"
#include "NiagaraSystem.h"
#include "Materials/MaterialInterface.h"

AHeliosFighterMissile::AHeliosFighterMissile()
{
    PrimaryActorTick.bCanEverTick=true;
    Mesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Missile"));SetRootComponent(Mesh);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetCanEverAffectNavigation(false);
    Flame=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Exhaust"));Flame->SetupAttachment(Mesh);
    Flame->SetCollisionEnabled(ECollisionEnabled::NoCollision);Flame->SetCastShadow(false);Flame->SetVisibility(false);
    FlameCore=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("White Exhaust Core"));FlameCore->SetupAttachment(Mesh);
    FlameCore->SetCollisionEnabled(ECollisionEnabled::NoCollision);FlameCore->SetCastShadow(false);FlameCore->SetVisibility(false);
    Tags.Add(TEXT("HeliosTransientWeaponEffect"));
}
void AHeliosFighterMissile::Launch(UHeliosFighterDefinition* D,AActor* Target,const FVector& Dir)
{
    Settings=D;LockedTarget=Target;Direction=Dir.GetSafeNormal();LastTrail=GetActorLocation();
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Helios/Weapons/Fighter/SM_VX_Missile")));
    auto* TrailMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Helios/Weapons/Fighter/SM_VX_Glow"));
    Flame->SetStaticMesh(TrailMesh);Flame->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Helios/Weapons/Fighter/Effects/M_VXFX_Exhaust")));Flame->SetRelativeLocation(FVector(-135,0,0));Flame->SetRelativeScale3D(FVector(95,15,15));
    FlameCore->SetStaticMesh(TrailMesh);FlameCore->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Helios/Weapons/Fighter/Effects/M_VXFX_HotCore")));FlameCore->SetRelativeLocation(FVector(-85,0,0));FlameCore->SetRelativeScale3D(FVector(45,6,6));
    SetLifeSpan(D->MissileLife);
}
void AHeliosFighterMissile::Tick(float Dt)
{
    Super::Tick(Dt);if(!Settings||bExploded)return;
    auto* W=Cast<AHeliosFighterWeapon>(GetOwner());if(!W||!W->bActive){Destroy();return;}
    float Remaining=FMath::Min(Dt,.25f);
    while(Remaining>UE_KINDA_SMALL_NUMBER&&!bExploded)
    {
        const float Step=FMath::Min(Remaining,1.f/120);Remaining-=Step;Age+=Step;
        if(Age>=Settings->MissileLife){Destroy();return;}
        if(Age>=.18f&&!bIgnitionPlayed){bIgnitionPlayed=true;LastTrail=GetActorLocation();W->Art->MissileIgnition(LastTrail,Direction);}
        if(auto* E=Cast<AHeliosEnemyActor>(LockedTarget.Get());E&&(E->IsEnemyDead()||!W->ClearSight(GetActorLocation(),E->GetActorLocation(),E)))LockedTarget.Reset();
        if(Age>=.18f&&LockedTarget.IsValid())
        {
            const FVector Desired=(LockedTarget->GetActorLocation()-GetActorLocation()).GetSafeNormal();
            const float Angle=FMath::Acos(FMath::Clamp(FVector::DotProduct(Direction,Desired),-1.f,1.f));
            if(Angle>UE_KINDA_SMALL_NUMBER)
            {const FQuat Turn=FQuat::FindBetweenNormals(Direction,Desired);Direction=FQuat::Slerp(FQuat::Identity,Turn,FMath::Min(1.f,FMath::DegreesToRadians(Settings->TurnDegrees)*Step/Angle)).RotateVector(Direction).GetSafeNormal();}
        }
        FVector Delta=Age<.18f?(Direction*650-FVector(0,0,240))*Step:Direction*Settings->MissileSpeed*Step;
        const FVector Start=GetActorLocation();FHitResult H;
        FCollisionQueryParams Q(SCENE_QUERY_STAT(FighterMissile),false,this);Q.AddIgnoredActor(W);Q.AddIgnoredActor(W->Player());
        if(auto* Pets=GetWorld()->GetSubsystem<UHeliosPetWorld>())Pets->IgnoreAll(Q);
        if(GetWorld()->SweepSingleByChannel(H,Start,Start+Delta,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(7),Q))
        {Explode(H.ImpactPoint,H.ImpactNormal);return;}
        SetActorLocationAndRotation(Start+Delta,Direction.Rotation());
        if(Age>=.18f)
        {
            FVector Segment=GetActorLocation()-LastTrail;
            while(Segment.Size()>=Settings->TrailSpacing)
            {LastTrail+=Segment.GetSafeNormal()*Settings->TrailSpacing;W->Art->MissileTrail(LastTrail-Direction*60,Direction);Segment=GetActorLocation()-LastTrail;}
        }
    }
    const bool Visible=Age>=.18f&&W->Art->EffectsAllowed();Flame->SetVisibility(Visible);FlameCore->SetVisibility(Visible);
    Flame->SetRelativeScale3D(FVector(95*(1+.12f*FMath::Sin(Age*83)),15,15));
}
void AHeliosFighterMissile::Explode(const FVector& At,const FVector& Normal)
{
    if(bExploded||!Settings)return;bExploded=true;
    auto* W=Cast<AHeliosFighterWeapon>(GetOwner());auto* P=W?W->Player():nullptr;
    const FVector Origin=At+Normal*12;
    for(TActorIterator<AHeliosEnemyActor> E(GetWorld());E;++E)
    {
        if(E->IsEnemyDead())continue;const float Distance=FVector::Dist(At,E->GetActorLocation());
        if(Distance>Settings->MissileRadius||!W||!W->ClearSight(Origin,E->GetActorLocation(),*E))continue;
        const float Damage=FMath::Lerp(Settings->MissileDamage,Settings->MissileMinimumDamage,Distance/Settings->MissileRadius);
        UGameplayStatics::ApplyDamage(*E,Damage,P?P->GetController():nullptr,W,UDamageType::StaticClass());
    }
    if(W&&W->Art)W->Art->Detonation(Origin,Normal);
    Destroy();
}
void AHeliosFighterMissile::EndPlay(EEndPlayReason::Type R)
{Super::EndPlay(R);}
