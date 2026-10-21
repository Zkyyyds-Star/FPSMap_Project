#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosBioSkillProjectile.generated.h"
class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;
class UHeliosBioRoleDefinition;

/** A real collision-driven role projectile. Source art is supplied by the verified role. */
UCLASS()
class HELIOSOUTPOST_API AHeliosBioSkillProjectile : public AActor
{
    GENERATED_BODY()
public:
    AHeliosBioSkillProjectile();
    void Configure(int32 Participant,int32 Round,UHeliosBioRoleDefinition* Definition,const FVector& Direction,bool Arcane=false,bool SwordWave=false);
    virtual void Tick(float DeltaSeconds) override;
private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Collision;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Visual;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProjectileMovementComponent> Movement;
    UPROPERTY(Transient) TObjectPtr<UHeliosBioRoleDefinition> SourceRole;
    int32 CasterId=INDEX_NONE,ExpectedRound=INDEX_NONE;
    bool bResolved=false;
    bool bArcane=false,bSwordWave=false;
    UFUNCTION() void OnImpact(UPrimitiveComponent* Component,AActor* Other,UPrimitiveComponent* OtherComponent,FVector NormalImpulse,const FHitResult& Hit);
    void Resolve(const FVector& Position);
};

