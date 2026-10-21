#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosColaQA.generated.h"
class AHeliosAdventureCharacter;
class AHeliosAdventureDirector;
class AHeliosEnemyActor;
class AHeliosColaProjectile;
class AStaticMeshActor;
class FJsonValue;
UCLASS()
class HELIOSOUTPOST_API AHeliosColaQA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosColaQA();
    virtual void Tick(float Dt) override;
private:
    void Check(const FString& Name,bool Pass,const FString& Detail=FString());
    void Next(int32 Value);
    void Shot();
    void Equip();
    void Capture(const FString& Name);
    void Finish();
    void StartCinematicShot(int32 Scenario);
    void CinematicTick(float Dt);
    void CinematicBurst(const FVector& Position,float Radius,const FVector& Normal,AActor* Surface,const FVector& Point);
    int32 Count() const;
    AHeliosColaProjectile* LastCan() const;
    AHeliosColaProjectile* SpawnCan(const FVector& Location,bool Stationary=false,float Fuse=2.2f);
    AHeliosEnemyActor* TargetAt(const FVector& Location);
    AStaticMeshActor* Box(const FVector& Location,const FVector& Scale);
    UPROPERTY() TObjectPtr<AHeliosAdventureCharacter> Player;
    UPROPERTY() TObjectPtr<AHeliosAdventureDirector> Director;
    UPROPERTY() TArray<TObjectPtr<AHeliosEnemyActor>> Targets;
    UPROPERTY() TObjectPtr<AStaticMeshActor> Wall;
    UPROPERTY() TObjectPtr<AStaticMeshActor> Floor;
    UPROPERTY() TObjectPtr<AHeliosColaProjectile> Can;
    int32 Stage=0,Failures=0,DirectionIndex=0,Frame=0,DemoShot=0;
    float Age=0,Before=0,PauseFuseAge=0;
    double WallStart=0,Started=0;
    bool bDone=false,bDemo=false,bMenuOnly=false;
    bool bDrinkDemo=false;
    int32 DrinkIndex=0,DrinkDrawsBefore=0;
    FName DrinkBefore;
    TMap<FString,int32> DrinkFrames;
    FVector Origin=FVector(-20000,-20000,1600), Blast;
    FRotator Aim=FRotator::ZeroRotator;
    TArray<TSharedPtr<FJsonValue>> Checks;
    int32 CinematicScenario=0,CinematicFrame=0,PerfShots=0;
    double LastPerfTime=0;
    TArray<double> BaselineFrames,OverlapFrames;
    UPROPERTY() TObjectPtr<AActor> CinematicCamera;
};
