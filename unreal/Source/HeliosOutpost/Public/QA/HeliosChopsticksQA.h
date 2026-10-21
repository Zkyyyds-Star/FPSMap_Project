#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosChopsticksQA.generated.h"
class AHeliosAdventureCharacter;
class AHeliosAdventureDirector;
class AHeliosEnemyActor;
class AStaticMeshActor;
class FJsonValue;
UCLASS()
class HELIOSOUTPOST_API AHeliosChopsticksQA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosChopsticksQA();
    virtual void Tick(float Dt) override;
private:
    void Check(const FString& Name,bool Pass,const FString& Detail=FString());
    void Next(int32 Value);
    void TargetAt(const FVector& Position);
    void Shot();
    void Capture(const FString& Name);
    void Finish();
    int32 Count() const;
    UPROPERTY() TObjectPtr<AHeliosAdventureCharacter> Player;
    UPROPERTY() TObjectPtr<AHeliosAdventureDirector> Director;
    UPROPERTY() TObjectPtr<AHeliosEnemyActor> Target;
    UPROPERTY() TObjectPtr<AStaticMeshActor> Wall;
    int32 Stage=0,Failures=0,DirectionIndex=0,Frame=0;
    float Age=0,Before=0;
    double WallStart=0,Started=0;
    bool bDone=false,bDemo=false,bMenuOnly=false;
    FVector Origin=FVector(-20000,-20000,1600);
    FRotator Aim=FRotator::ZeroRotator;
    TArray<TSharedPtr<FJsonValue>> Checks;
};
