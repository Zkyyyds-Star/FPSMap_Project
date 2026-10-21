#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosAudioSettingsQA.generated.h"
UCLASS()
class HELIOSOUTPOST_API AHeliosAudioSettingsQA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosAudioSettingsQA();
    virtual void Tick(float Delta) override;
private:
    void Check(const FString&,bool);
    void Record(const FString& Name);
    void EndRecord();
    void Mark(const FString&,int32 Bus=-1);
    void Shot(const FString&);
    void Finish(bool Exit=true);
    void QueueTone(int32);
    int32 Stage=0,Bus=1,Failures=0,FireCase=0;
    double Started=0,Next=0,RecordingAt=0,NextShot=0;
    int32 MusicPlayCount=0;
    float MusicSeconds=0;
    float SourcePhaseSeconds=.5f, OriginalSourceDuration=0;
    bool OriginalSourceLooping=false;
    FString Out,RecordingName;
    bool Finished=false;
    UPROPERTY(Transient) TArray<TObjectPtr<class USoundWaveProcedural>> Waves;
    UPROPERTY(Transient) TArray<TObjectPtr<class UAudioComponent>> Tones;
    UPROPERTY(Transient) TObjectPtr<class UAudioComponent> MusicBefore;
    UPROPERTY(Transient) TObjectPtr<AActor> AudioTestOwner;
    TArray<int64> ToneSamples;
    TArray<TSharedPtr<class FJsonValue>> Checks,Segments;
};
