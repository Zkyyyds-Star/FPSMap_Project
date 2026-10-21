#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "HeliosWangZheKuiThunderQA.generated.h"
UCLASS()
class HELIOSOUTPOST_API AHeliosWangZheKuiThunderQA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosWangZheKuiThunderQA();
    virtual void Tick(float Dt)override;
private:
    void Check(const TCHAR* Name,bool Pass);
    void Capture(const TCHAR* Name);
    void Finish();
    void Key(FKey Key,bool Down);
    class AHeliosEnemyActor* SpawnTarget(float HP);
    void SampleReloadEffects(class AHeliosWangZheKuiThunderWeapon* Weapon);
    FString Out;
    double Started=0,Next=0;
    float Age=0;
    int32 MotionFrame=0,SlugStartShots=0,SlugStartAmmo=0;
    double LastMotion=0;
    int32 Stage=0,Kills=0,Failures=0,BeforeShots=0,NormalEnemiesSeen=0;
    bool bFinished=false,bRecording=false;
    float PreviousUnfocusedVolume=0;
    FVector PetStart;
    TSet<int32> VoiceIndices,Captures;
    TArray<double> Frames;
    TArray<TSharedPtr<class FJsonValue>> Checks;
    TArray<TSharedPtr<class FJsonValue>> ReloadCases;
    int32 ReloadCase=0,ReloadSamples=0,ReloadGeometrySamples=0;
    float ReloadTestSeconds=2.3f,ReloadFirstVisible=-1,ReloadLastVisible=-1;
    float ReloadPositionError=0,ReloadRotationError=0,LegacyPositionError=0;
    float ReloadFrameTolerance=0;
    bool bReloadMoving=false,bReloadAttachmentsValid=true;
    FVector ReloadStartLocation;
    UPROPERTY(Transient) TObjectPtr<class AHeliosArenaCharacter> Player;
    UPROPERTY(Transient) TObjectPtr<class AHeliosEnemyActor> Target;
    UPROPERTY(Transient) TObjectPtr<class AHeliosWangZheKuiThunderPet> Pet;
    UPROPERTY(Transient) TObjectPtr<class ACameraActor> Camera;
    UPROPERTY(Transient) TObjectPtr<class AHeliosSSXEffect> Probe;
};
