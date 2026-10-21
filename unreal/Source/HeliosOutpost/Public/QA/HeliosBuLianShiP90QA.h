#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "HeliosBuLianShiP90QA.generated.h"
UCLASS()
class HELIOSOUTPOST_API AHeliosBuLianShiP90QA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosBuLianShiP90QA();
    virtual void Tick(float Dt)override;
private:
    void Check(const TCHAR* Name,bool Pass);
    void Capture(const TCHAR* Name);
    void Finish();
    void Key(FKey Key,bool Down);
    void MarkAudio(const TCHAR* Name);
    int32 AudioTailCount()const;
    class AHeliosEnemyActor* SpawnTarget(float HP);
    FString Out;
    double Started=0,Next=0;
    float Age=0;
    int32 Stage=0,Kills=0,Failures=0,BeforeShots=0,NormalEnemiesSeen=0;
    bool bFinished=false,bRecording=false;
    float PreviousUnfocusedVolume=0;
    double NextFXFrame=0;
    int32 FXFrame=0,MaxFXActors=0,MaxFXComponents=0,StackTargets=0,StackKillsBefore=0;
    TArray<TSharedPtr<class FJsonValue>> FXSamples;
    TArray<TSharedPtr<class FJsonValue>> AudioMarkers;
    TArray<TWeakObjectPtr<class UAudioComponent>> AudioTails;
    TWeakObjectPtr<class UHeliosP90SubstituteFireAudio> AudioSource;
    double AudioStartWall=0,AudioEventWall=0;
    int32 AudioDistancePhase=-1,AudioBeforeShots=0,AudioBeforeTriggers=0,AudioBeforePetShots=0;
    bool bAudioReleased=false;
    FVector AudioPetTargetLocation;
    FVector PetStart;
    TSet<int32> VoiceIndices,Captures;
    TArray<double> Frames;
    TArray<TSharedPtr<class FJsonValue>> Checks;
    UPROPERTY(Transient) TObjectPtr<class AHeliosArenaCharacter> Player;
    UPROPERTY(Transient) TObjectPtr<class AHeliosEnemyActor> Target;
    UPROPERTY(Transient) TObjectPtr<class AHeliosBuLianShiP90Pet> Pet;
    UPROPERTY(Transient) TObjectPtr<class ACameraActor> Camera;
};
