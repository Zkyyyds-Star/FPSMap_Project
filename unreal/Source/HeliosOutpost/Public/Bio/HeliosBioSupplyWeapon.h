#pragma once
#include "CoreMinimal.h"
#include "Weapons/HeliosCombatWeapons.h"
#include "HeliosBioSupplyWeapon.generated.h"
class UHeliosBioSupplyWeaponDefinition;
class UAnimSequence;
class USphereComponent;
class UProjectileMovementComponent;
class UStaticMeshComponent;
class AHeliosSSXEffect;
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;

/** Native BIO item driver. Selected-character skin is supplied by the pair profile. */
UCLASS()
class HELIOSOUTPOST_API AHeliosBioSupplyWeapon : public AHeliosCombatWeapon
{
    GENERATED_BODY()
public:
    AHeliosBioSupplyWeapon();
    virtual void ApplyDefinition(const UHeliosWeaponDefinition* Definition) override;
    virtual void ActivateWeapon(const FName& OwnerTag) override;
    virtual void DeactivateWeapon() override;
    virtual void Tick(float Dt) override;
    virtual void BeginReload(float Seconds) override;
    virtual void CancelReload() override;
    /** Cancels an unreleased native throw without consuming its physical round. */
    bool CancelPendingThrow(const TCHAR* Reason=TEXT("Cancelled"));
    bool GetNativeMuzzle(FVector& Position) const;
    bool CanReachNativeTarget(const FVector& Target) const;
    bool IsNativeActionBusy() const{return NativeActionRemaining>0;}
    bool IsNativeThrowBusy() const{return bPendingThrow||(bThrownPropHidden&&NativeActionRemaining>0);}
    int32 GetNativeHitCount() const{return NativeHits;}
    int32 GetNativeShotCount() const{return NativeShots;}
    const FHitResult& GetLastNativeImpact() const{return LastNativeImpact;}
    bool ShouldKeepThirdPersonIdle(UAnimSequence* Source) const;
protected:
    virtual void FireProjectile(const FVector& Target) override;
private:
    friend class AHeliosBioSupplyProjectile;
    void SetNativePose(FName Action,bool Loop,float Duration=0);
    void UpdateThrowableVisual();
    void PlaySourceActionSound(FName Action);
    bool HasSourceAudioCues(FName Action) const;
    void TickSourceAudio(float Dt);
    UPROPERTY(Transient) TObjectPtr<UHeliosBioSupplyWeaponDefinition> BioDefinition;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> NativeClip;
    float NativeActionRemaining=0;
    FName NativeSoundAction;
    float NativeSoundAge=0,NativeSoundRate=1;
    TSet<int32> PlayedSourceCues;
    bool bNativeReload=false,bNativeEquipped=false;
    bool bPendingThrow=false,bReleasingThrow=false,bThrowPreparing=false;
    bool bThrownPropHidden=false;
    float ThrowReleaseRemaining=0;
    int32 PendingThrowRound=INDEX_NONE;
    FVector PendingThrowTarget=FVector::ZeroVector;
    int32 NativeShots=0,NativeHits=0;
    FHitResult LastNativeImpact;
};

/** Source mesh/FX on a real swept sphere driven by ProjectileMovement. */
UCLASS()
class HELIOSOUTPOST_API AHeliosBioSupplyProjectile : public AActor
{
    GENERATED_BODY()
public:
    AHeliosBioSupplyProjectile();
    void Initialize(UHeliosBioSupplyWeaponDefinition* Definition,int32 Participant,int32 Round,const FVector& Direction);
    virtual void Tick(float Dt) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UFUNCTION() void OnNativeHit(UPrimitiveComponent* Component,AActor* Other,UPrimitiveComponent* OtherComponent,FVector Impulse,const FHitResult& Hit);
    void Detonate(const FVector& At,const FVector& Normal);
    bool IsLaunchValid() const;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Collision;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Visual;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProjectileMovementComponent> Movement;
    UPROPERTY(Transient) TObjectPtr<UHeliosBioSupplyWeaponDefinition> BioDefinition;
    UPROPERTY(Transient) TObjectPtr<AHeliosSSXEffect> Trail;
    int32 LaunchParticipant=INDEX_NONE,LaunchRound=0;
    float Age=0;
    FVector BirthLocation=FVector::ZeroVector;
    bool bDetonated=false;
};

/** Original LineRenderer data on a camera-facing strip with actual shot endpoints. */
UCLASS()
class HELIOSOUTPOST_API AHeliosBioSupplyTracer : public AActor
{
    GENERATED_BODY()
public:
    AHeliosBioSupplyTracer();
    bool Initialize(UHeliosBioSupplyWeaponDefinition* Definition,const FVector& From,const FVector& To,bool FP);
    virtual void Tick(float Dt) override;
private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Ribbon;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Material;
    FVector Start=FVector::ZeroVector,End=FVector::ZeroVector;
    FLinearColor StartColor=FLinearColor::White,EndColor=FLinearColor::White;
    float Age=0,Duration=0,Width=0,StartU=0,EndU=0,TilingU=1,TilingV=1;
};
