#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HeliosBioSupplyAssetQATools.generated.h"

class USoundWave;

/** Read-only saved native asset inspection. Available only in the editor module. */
UCLASS()
class HELIOSENEMYEDITOR_API UHeliosBioSupplyAssetQATools : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Imported source PCM, without WAV-export transformations or cooked decoding. */
    UFUNCTION(BlueprintCallable,Category="Helios|Editor|BioSupplyQA")
    static FString InspectImportedSoundPCM(USoundWave* Sound);
};
