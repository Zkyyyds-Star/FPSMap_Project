#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HeliosWeaponVFXTools.generated.h"

class UNiagaraSystem;

/**
 * UE 5.8 editor-only authoring bridge for editable, bounded weapon Niagara FX.
 *
 * Supported presets retain the original names and add RifleMuzzle, ShotgunImpact,
 * LaserBeam and LaserHit for /Game/Helios/Weapons/Realism/Effects.  Every preset
 * only edits stateless emitters copied from Epic's editable lightweight templates.
 */
UCLASS()
class HELIOSENEMYEDITOR_API UHeliosWeaponVFXTools : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Applies a named Helios preset to all stateless emitters and recompiles the system. */
    UFUNCTION(BlueprintCallable, Category = "Helios|Editor|VFX")
    static FString ConfigureWeaponEffect(UNiagaraSystem* System, FName Preset);

    /**
     * Applies an isolated realism preset and its authored sprite material layers.
     * This never changes the legacy ConfigureWeaponEffect presets or their assets.
     */
    UFUNCTION(BlueprintCallable, Category = "Helios|Editor|VFX")
    static FString ConfigureRealismEffect(UNiagaraSystem* System, FName Preset);

    UFUNCTION(BlueprintCallable, Category = "Helios|Editor|VFX")
    static FString ConfigureMysteriousEffect(UNiagaraSystem* System, FName Kind);

    /** Isolated VX-01 layers; never rewrites another weapon's effect. */
    UFUNCTION(BlueprintCallable, Category = "Helios|Editor|VFX")
    static FString ConfigureFighterEffect(UNiagaraSystem* System, FName Kind);
};
