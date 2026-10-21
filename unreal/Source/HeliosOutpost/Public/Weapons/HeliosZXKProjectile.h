#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosZXKProjectile.generated.h"
class AHeliosZhongXiaoKuiWeapon;
class AHeliosEnemyActor;
class UStaticMeshComponent;
UCLASS()
class HELIOSOUTPOST_API AHeliosZXKProjectile : public AActor
{
    GENERATED_BODY()
public:
    AHeliosZXKProjectile();
    void Initialize(AHeliosZhongXiaoKuiWeapon* Weapon,const FVector& Direction,float Damage,bool SwordWave,const FVector& BirthEye);
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    friend class AHeliosSSXDemo;
    UPROPERTY(Transient) TObjectPtr<class AHeliosSSXEffect> FlightEffect;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
    TWeakObjectPtr<AHeliosZhongXiaoKuiWeapon> Source;
    TSet<TWeakObjectPtr<AHeliosEnemyActor>> HitEnemies;
    FVector Velocity=FVector::ZeroVector;
    FVector WaveBirthEye=FVector::ZeroVector;
    float Amount=100,Age=0,Lifetime=2,HitRadius=120;
    bool bWave=false;
};
