#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "HeliosChanZeusQA.generated.h"
UCLASS()
class HELIOSOUTPOST_API AHeliosChanZeusQA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosChanZeusQA();
    virtual void Tick(float Dt)override;
private:
    void Check(const TCHAR* Name,bool Pass);
    void Capture(const TCHAR* Name);
    void Finish();
    void Key(FKey Key,bool Down);
    class AHeliosEnemyActor* SpawnTarget(float HP);
    FString Out;
    double Started=0,Next=0;
    float Age=0;
    int32 Stage=0,Kills=0,Failures=0,BeforeShots=0,NormalEnemiesSeen=0;
    bool bFinished=false,bRecording=false;
    float PreviousUnfocusedVolume=0;
    FVector PetStart;
    TSet<int32> VoiceIndices,Captures;
    TArray<double> Frames;
    TArray<TSharedPtr<class FJsonValue>> Checks;
    UPROPERTY(Transient) TObjectPtr<class AHeliosArenaCharacter> Player;
    UPROPERTY(Transient) TObjectPtr<class AHeliosEnemyActor> Target;
    UPROPERTY(Transient) TObjectPtr<class AHeliosChanZeusPet> Pet;
    UPROPERTY(Transient) TObjectPtr<class ACameraActor> Camera;
};
