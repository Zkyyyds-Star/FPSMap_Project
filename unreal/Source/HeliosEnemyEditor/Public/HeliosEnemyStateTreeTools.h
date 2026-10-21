#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HeliosEnemyStateTreeTools.generated.h"

class UStateTree;

/** Narrow editor-only bridge used by the deterministic StateTree adaptation script. */
UCLASS()
class HELIOSENEMYEDITOR_API UHeliosEnemyStateTreeTools : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Disable template-only StateTree debug text tasks before shipping/capture. */
    UFUNCTION(BlueprintCallable, Category="Helios|Editor|StateTree")
    static int32 DisableDebugTextTasks(UStateTree* StateTree);

    /** Repair and describe the official shoot-target data flow after schema adaptation. */
    UFUNCTION(BlueprintCallable, Category="Helios|Editor|StateTree")
    static FString RepairShooterTargetBindings(UStateTree* StateTree);

    /**
     * Marks the asset's public contract dirty, compiles it synchronously, and returns
     * a JSON record of both the editor schema and the newly baked runtime schema.
     */
    UFUNCTION(BlueprintCallable, Category="Helios|Editor|StateTree")
    static FString CompileAndDescribe(UStateTree* StateTree);
};
