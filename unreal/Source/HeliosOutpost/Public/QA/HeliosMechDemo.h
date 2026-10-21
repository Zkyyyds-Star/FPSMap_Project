#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosMechDemo.generated.h"
class AHeliosArenaCharacter;
class AHeliosEnemyActor;
UCLASS()
class HELIOSOUTPOST_API AHeliosMechDemo : public AActor
{
    GENERATED_BODY()
public:
    AHeliosMechDemo();
    virtual void Tick(float DeltaSeconds) override;
private:
    void Check(const TCHAR* Name,bool Passed);
    void Capture(const TCHAR* Name);
    float Health() const;
    void Finish();
    UPROPERTY(Transient) TObjectPtr<AHeliosArenaCharacter> Player;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosEnemyActor>> Targets;
    int32 Stage=0,Failures=0;
    float Age=0,Before=0;
    double Started=0;
    bool bCaptured=false;
    FVector Ground;
    FString Checks;
};
