#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "HeliosYunSummerMegatronQA.generated.h"

UCLASS()
class HELIOSOUTPOST_API AHeliosYunSummerMegatronQA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosYunSummerMegatronQA();
    virtual void Tick(float Dt) override;
private:
    void Check(const TCHAR* Name, bool Passed);
    void Capture(const TCHAR* Name);
    void Key(FKey Key, bool Down);
    void Finish();
    class AHeliosEnemyActor* SpawnTarget(const FVector& At, float HP);
    void BlastFixtures(class AHeliosYunSummerMegatronWeapon* Weapon);
    void TickArmsView(class AHeliosYunSummerMegatronWeapon* Weapon);
    FString Out;
    double Started=0, Next=0, LastMotion=0;
    float Age=0, PreviousUnfocusedVolume=0, BaseSpeed=0, BeforeHP=0;
    int32 Stage=0, Failures=0, Slot=0, BeforeAmmo=0, BeforeShots=0, BeforeKills=0, MotionFrame=0;
    int32 EnemyObservations=0, MaximumFlightActors=0, MaximumVisibleEffectTracks=0;
    int32 ArmsSample=0;
    bool bFinished=false, bRecording=false;
    TSet<int32> Captures;
    TArray<TSharedPtr<class FJsonValue>> Checks;
    TArray<double> Frames;
    TArray<FTransform> PetTransforms;
    UPROPERTY(Transient) TObjectPtr<class AHeliosArenaCharacter> Player;
    UPROPERTY(Transient) TObjectPtr<class AHeliosEnemyActor> Target;
    UPROPERTY(Transient) TObjectPtr<class ACameraActor> Camera;
};
