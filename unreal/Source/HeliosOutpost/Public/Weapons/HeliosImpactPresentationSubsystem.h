#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "HeliosImpactPresentationSubsystem.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;
class UMaterialInterface;
class UDecalComponent;
class USoundBase;
class USoundAttenuation;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

UENUM()
enum class EHeliosImpactSurface : uint8 { Default, Concrete, Metal, Wood, Soil, Cloth, Armor, Skin };

/** Cosmetic actual-hit feedback. Never applies damage or changes hit reactions. */
UCLASS()
class HELIOSOUTPOST_API UHeliosImpactPresentationSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    void Preload();
    bool AreAssetsReady() const
    {
        return bPreloaded && Effects.Num()==8 && Decals.Num()==8 && Sounds.Num()==8 && Tracer && LaserHotspot && LaserEnemyHit && FlightMeshes.Num()==64
            && !Effects.Contains(nullptr) && !Decals.Contains(nullptr) && !Sounds.Contains(nullptr);
    }
    uint64 BeginShot() { return ++ShotSerial; }
    void ReportImpact(FName WeaponId, uint64 ShotId, const FHitResult& Hit, int32 Cluster = 0, bool bCreateMark = true);
    void ReportPellets(uint64 ShotId, const TArray<FHitResult>& Hits);
    void SpawnTracer(const FVector& Start, const FVector& End, bool bShotgun = false);
    void TrackProjectile(AActor* Projectile);
    void StopProjectile(AActor* Projectile, const FHitResult& Hit);
    void Clear(bool bIncludeDecals = false);
    static EHeliosImpactSurface ResolveSurface(const FHitResult& Hit);
    int32 GetActiveImpactCount() const { return Bursts.Num(); }
    int32 GetActiveDecalCount() const { return Marks.Num(); }
    uint64 GetPresentedCount() const { return PresentedCount; }
private:
    struct FBurst { TWeakObjectPtr<UNiagaraComponent> Component; double Ends = 0; };
    struct FMark { TWeakObjectPtr<UDecalComponent> Component; float Ends = 0; };
    struct FFlightTrail
    {
        TWeakObjectPtr<AActor> Projectile;
        FVector Start=FVector::ZeroVector;
        FVector End=FVector::ZeroVector;
        float Age=0.f;
        float Duration=0.f;
        bool bFollowingProjectile=false;
        bool bFading=false;
        bool bShotgun=false;
        FVector LastHead=FVector::ZeroVector;
        bool bActive=false;
    };
    TArray<FFlightTrail> FlightTrails;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> FlightMeshes;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> FlightHalos;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> FlightMaterials;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> HaloMaterials;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> ShotgunTails;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> ShotgunCoreMaterials;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> ShotgunHaloMaterials;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> ShotgunTailMaterials;
    UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> ShotgunFlare;
    int32 NextFlight=0;
    int32 AcquireFlight();
    void UpdateFlight(int32 Index, const FVector& Head, float Opacity = 1.f);
    void SpawnMark(FName WeaponId, uint64 ShotId, const FHitResult& Hit, int32 PelletIndex);
    TArray<FBurst> Bursts;
    TArray<FMark> Marks;
    TMap<uint64,float> Seen;
    TMap<uint64,float> SoundSeen;
    TMap<TWeakObjectPtr<AActor>,float> LaserLast;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraSystem>> Effects;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> Decals;
    UPROPERTY(Transient) TArray<TObjectPtr<USoundBase>> Sounds;
    UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> LaserHotspot;
    UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> LaserEnemyHit;
    UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> Tracer;
    UPROPERTY(Transient) TObjectPtr<USoundAttenuation> Attenuation;
    bool bPreloaded = false;
    uint64 ShotSerial = 0;
    uint64 PresentedCount = 0;
    void Burst(UNiagaraSystem* System, const FHitResult& Hit, float Lifetime, float Scale = 1.f);
    FHitResult RefineCosmeticHit(const FHitResult& Hit) const;
};
