#pragma once
#include "CoreMinimal.h"
#include "Subsystems/AudioEngineSubsystem.h"
#include "ActiveSoundUpdateInterface.h"
#include "HeliosAudioRoutingSubsystem.generated.h"

/** Covers engine animation notifies, Blueprint sounds and ambient components. */
UCLASS()
class HELIOSOUTPOST_API UHeliosAudioRoutingSubsystem : public UAudioEngineSubsystem, public IActiveSoundUpdateInterface
{
    GENERATED_BODY()
public:
    virtual void NotifyActiveSoundCreated(FActiveSound& ActiveSound) override;
    static void Publish(UWorld* World,const TArray<TObjectPtr<class USoundClass>>& Classes);
    static void Forget(const TArray<TObjectPtr<class USoundClass>>& Classes);
};
