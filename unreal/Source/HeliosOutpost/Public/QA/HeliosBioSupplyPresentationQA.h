#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Subsystems/AudioEngineSubsystem.h"
#include "ActiveSoundUpdateInterface.h"
#include "HeliosBioSupplyPresentationQA.generated.h"

class AActor;
class USoundBase;

/** Passive, bounded audio-thread event capture, enabled only by supply QA flags. */
UCLASS()
class HELIOSOUTPOST_API UHeliosBioSupplyAudioQA : public UAudioEngineSubsystem, public IActiveSoundUpdateInterface
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Update() override;
    virtual void NotifyActiveSoundCreated(FActiveSound& Sound) override;
    virtual void NotifyActiveSoundDeleting(const FActiveSound& Sound) override;
    virtual void NotifyVirtualizedSoundCreated(FActiveSound& Sound) override;
    virtual void NotifyVirtualizedSoundDeleting(const FActiveSound& Sound) override;
private:
    void ObserveSound(const TCHAR* Kind,const FActiveSound& Sound);
    double NextSampleWall=0;
};

/** Shared observation only. Does not fire, move, select equipment or alter gates. */
UCLASS()
class HELIOSOUTPOST_API UHeliosBioSupplyPresentationQA : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="Helios|QA|BioSupply")
    static FString SnapshotParticipant(AActor* Participant,AActor* Target);

    /** Cumulative audio/request/render events; retain lastSerial for the next call. */
    UFUNCTION(BlueprintCallable,Category="Helios|QA|BioSupply",meta=(WorldContext="WorldContextObject"))
    static FString ReadAudioEvents(const UObject* WorldContextObject,int64 AfterSerial=0);

    /** Observation hook immediately before the real PlaySound call; never plays audio. */
    static void RecordSoundRequest(AActor* Source,USoundBase* Sound,FName Action,float ExpectedNativeSeconds,
        float ActualNativeSeconds,bool FirstPerson,const TCHAR* Kind);
};
