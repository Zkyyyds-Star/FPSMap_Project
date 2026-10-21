#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosFighterDemo.generated.h"
class AHeliosArenaCharacter;
class AHeliosEnemyActor;
UCLASS()
class HELIOSOUTPOST_API AHeliosFighterDemo : public AActor
{
    GENERATED_BODY()
public:
    AHeliosFighterDemo();
    virtual void Tick(float Dt) override;
private:
    void Check(const TCHAR* Name,bool Pass);
    void Capture(const FString& Name);
    void Finish();
    UPROPERTY(Transient) TObjectPtr<AHeliosArenaCharacter> Player;
    UPROPERTY(Transient) TObjectPtr<AHeliosEnemyActor> Target;
    FVector Start;
    int32 Stage=0,Failures=0,Frame=0,BeforeCount=0;
    float Age=0,Total=0,BeforeHealth=0,RecordClock=0;
    FString Checks;
    bool bShot=false;
    int32 FXCapture=0,PeakEffects=0;
    TArray<float> IdleFrames,CombatFrames;
};
