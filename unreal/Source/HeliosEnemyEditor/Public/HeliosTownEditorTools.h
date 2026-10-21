#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HeliosTownEditorTools.generated.h"

class AActor;

/** Native authoring bridge for engine types not exported to Unreal Python. Editor module only. */
UCLASS()
class HELIOSENEMYEDITOR_API UHeliosTownEditorTools : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Helios|Editor|Town")
    static bool ConfigureTownWorld(int32 CellSizeCm=12800, float LoadingRangeCm=32000.f);

    UFUNCTION(BlueprintCallable, Category="Helios|Editor|Town")
    static bool MarkTownActor(AActor* Actor, bool bSpatial);
};
