#include "Weapons/HeliosZXKProjectile.h"
#include "Weapons/HeliosHunterDamageFeedback.h"
#include "Weapons/HeliosZhongXiaoKui.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/DamageType.h"
#include "Combat/HeliosCombatTempoSettings.h"
#include "Player/HeliosArenaCharacter.h"
#include "Camera/CameraComponent.h"
AHeliosZXKProjectile::AHeliosZXKProjectile()
{
    PrimaryActorTick.bCanEverTick=true;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Projectile movement root"));
    Mesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Original flying dart"));Mesh->SetupAttachment(RootComponent);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetCastShadow(false);
    SetActorEnableCollision(false);
}
void AHeliosZXKProjectile::Initialize(AHeliosZhongXiaoKuiWeapon* Weapon,const FVector& Direction,float Damage,bool SwordWave,const FVector& BirthEye)
{
    Source=Weapon;Amount=Damage;bWave=SwordWave;
    WaveBirthEye=BirthEye;
    Velocity=Direction.GetSafeNormal()*UHeliosCombatTempoSettings::Hunter(this,true,false,bWave?TEXT("剑气速度"):TEXT("飞镖速度"),6000,10,50000);
    Lifetime=UHeliosCombatTempoSettings::Hunter(this,true,false,bWave?TEXT("剑气寿命"):TEXT("飞镖寿命"),2,.05,30);
    HitRadius=UHeliosCombatTempoSettings::Hunter(this,true,false,bWave?TEXT("剑气碰撞半径"):TEXT("飞镖碰撞半径"),bWave?180:120,1,2000);
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Helios/CFMZhongXiaoKui/SM_ZXK_Feidao_Projectile.SM_ZXK_Feidao_Projectile")));
    Mesh->SetVisibility(!bWave);Mesh->SetRelativeRotation(FRotator(-90,0,0));
    if(bWave)FlightEffect=HeliosZXK::Effect(this,TEXT("FX_ShengHua_ZhongXiaoKui_Root_1P_B_Skill02_JianQi"),GetActorLocation(),Direction.Rotation(),false);
    // The original flight prefab already contains the 998-vertex dart plus its
    // 56-vertex aura. Keep the static dart only as a missing-asset fallback.
    if(!bWave)FlightEffect=HeliosZXK::Effect(this,TEXT("FX_Shenghua_ZhongXiaoKui_Weapon_Root2_1P_C_Fire_Trail"),GetActorLocation(),Direction.Rotation(),false);
    if(FlightEffect)
    {
        FlightEffect->AttachToComponent(RootComponent,FAttachmentTransformRules::KeepWorldTransform);
        FlightEffect->FollowTravelingEmitter();FlightEffect->AddTickPrerequisiteActor(this);
        if(bWave)FlightEffect->MatchTravelLifetime(Lifetime);
        if(bWave)if(auto* Player=Cast<AHeliosArenaCharacter>(Weapon->GetOwner());Player&&Player->IsLocallyControlled())
            FlightEffect->NormalizeLocalWaveView(Player,WaveBirthEye);
        Mesh->SetVisibility(false);
    }
}
void AHeliosZXKProjectile::EndPlay(const EEndPlayReason::Type Reason)
{
    if(IsValid(FlightEffect))FlightEffect->Destroy();FlightEffect=nullptr;
    Super::EndPlay(Reason);
}
void AHeliosZXKProjectile::Tick(float Dt)
{
    Super::Tick(Dt);if(GetWorld()->IsPaused())return;
    auto* Weapon=Source.Get();if(!Weapon||(Age+=Dt)>Lifetime){Destroy();return;}
    const FVector Before=GetActorLocation(),After=Before+Velocity*Dt;SetActorLocation(After);
    Mesh->SetRelativeRotation(FRotator(-90,Age*720,0));
    // Public footage shows darts passing walls and damaging groups. These speed,
    // radius and lifetime values adapt that behavior to the project's combat.
    for(TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It)
    {
        auto* Enemy=*It;if(!Enemy->CanBeDamaged()||Enemy->IsEnemyDead()||HitEnemies.Contains(Enemy))continue;
        if(FMath::PointDistToSegment(Enemy->GetActorLocation()+FVector(0,0,25),Before,After)>HitRadius)continue;
        HitEnemies.Add(Enemy);auto* Pawn=Cast<APawn>(Weapon->GetOwner());
        const float PreviousHP=Enemy->CurrentHP;
        if(UGameplayStatics::ApplyDamage(Enemy,Amount,Pawn?Pawn->GetController():nullptr,Weapon,UDamageType::StaticClass())<=0||Enemy->CurrentHP>=PreviousHP)continue;
        Weapon->ProjectileContact(Enemy->IsEnemyDead());
        if(auto* Feedback=Weapon->FindComponentByClass<UHeliosHunterDamageFeedback>())Feedback->Emit((uint64(GetUniqueID())<<32)|1);
        HeliosZXK::Effect(Weapon,bWave?TEXT("FX_ShengHua_ZhongXiaoKui_b_Root_JIanQi_Hit"):TEXT("FX_ShengHua_ZhongXiaoKui_b_Root_FeiDao_Hit"),Enemy->GetActorLocation()+FVector(0,0,35),Velocity.Rotation(),false);
    }
}
