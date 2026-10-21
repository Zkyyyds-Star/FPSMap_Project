#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HeliosBioWaterAssetTools.generated.h"
class UMaterialInterface;

/** Narrow original-water resource compilation. No graph/color or package save. */
UCLASS()
class HELIOSENEMYEDITOR_API UHeliosBioWaterAssetTools : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="Helios|Editor|OriginalWater")
    static FString CompileOriginalWaterForRendering(UMaterialInterface* Material);
};
