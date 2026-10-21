#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosBioSerumProjectile.generated.h"
class USphereComponent;
class UProjectileMovementComponent;
class UHeliosBioRoleDefinition;

/** A first-impact serum projectile. Cure/heal authority is always the shared rules. */
UCLASS()
class HELIOSOUTPOST_API AHeliosBioSerumProjectile : public AActor
{
    GENERATED_BODY()
public:
    AHeliosBioSerumProjectile();
    void Configure(int32 Participant,int32 Round,UHeliosBioRoleDefinition* Definition,const FVector& Direction);
    void ResolveInitialObstruction(const FHitResult& Hit);
    virtual void Tick(float DeltaSeconds) override;
    int32 GetCasterId() const { return CasterId; }
    int32 GetExpectedRound() const { return ExpectedRound; }
    float GetTravelledDistance() const { return TravelledDistance; }
private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Collision;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProjectileMovementComponent> Movement;
    UPROPERTY(Transient) TObjectPtr<UHeliosBioRoleDefinition> SourceRole;
    int32 CasterId=INDEX_NONE,ExpectedRound=INDEX_NONE;
    float TravelledDistance=0.f;
    FVector PreviousPosition=FVector::ZeroVector;
    bool bConfigured=false,bResolved=false;
    bool IsCasterValid() const;
    void Cancel(const TCHAR* Reason);
    UFUNCTION() void OnImpact(UPrimitiveComponent* Component,AActor* Other,UPrimitiveComponent* OtherComponent,FVector NormalImpulse,const FHitResult& Hit);
    void Resolve(AActor* Other,const FHitResult& Hit);
};
