#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HeliosWeaponVFX.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class USceneComponent;

/** Small, pooled Niagara facade shared by native and Blueprint weapons. */
UCLASS()
class HELIOSOUTPOST_API UHeliosWeaponVFX : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Fire-and-forget effect. The component returns to Niagara's pool on completion. */
    UFUNCTION(BlueprintCallable, Category = "Helios|Weapons|VFX", meta = (WorldContext = "WorldContextObject"))
    static UNiagaraComponent* SpawnOneShot(
        const UObject* WorldContextObject,
        UNiagaraSystem* System,
        const FVector& Location,
        const FRotator& Rotation = FRotator::ZeroRotator,
        const FVector& Scale = FVector(1.0f, 1.0f, 1.0f),
        float MaxLifetimeSeconds = 0.0f);

    /** Persistent pooled effect, intended for rocket trails. Call ReleaseTracked when finished. */
    UFUNCTION(BlueprintCallable, Category = "Helios|Weapons|VFX")
    static UNiagaraComponent* SpawnAttachedTrail(
        UNiagaraSystem* System,
        USceneComponent* AttachTo,
        FName SocketName = NAME_None,
        const FVector& RelativeLocation = FVector::ZeroVector,
        const FRotator& RelativeRotation = FRotator::ZeroRotator);

    /** Sets the standard User.Start and User.End position parameters used by tracers. */
    UFUNCTION(BlueprintCallable, Category = "Helios|Weapons|VFX")
    static void SetTracerEndpoints(UNiagaraComponent* Component, const FVector& Start, const FVector& End);

    /** Stops one persistent effect and returns it to Niagara's component pool. */
    UFUNCTION(BlueprintCallable, Category = "Helios|Weapons|VFX")
    static void ReleaseTracked(UNiagaraComponent* Component);

    /** Releases every persistent weapon effect registered in this world. */
    UFUNCTION(BlueprintCallable, Category = "Helios|Weapons|VFX", meta = (WorldContext = "WorldContextObject"))
    static void ClearWorld(const UObject* WorldContextObject);

    /** Lightweight QA hook; returns valid persistent/timed effects in this world. */
    static int32 GetTrackedCount(const UObject* WorldContextObject);

private:
    static TMap<TWeakObjectPtr<UWorld>, TArray<TWeakObjectPtr<UNiagaraComponent>>> TrackedByWorld;
    static TMap<TWeakObjectPtr<UNiagaraComponent>, FTimerHandle> ReleaseTimers;
    static void Track(UNiagaraComponent* Component);
    static void Compact(UWorld* World);
};
