#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "HeliosLightstickDemo.generated.h"
class AHeliosAdventureCharacter;
class AHeliosAdventureDirector;
class AHeliosEnemyActor;
class AStaticMeshActor;
class USphereComponent;
UCLASS()
class AHeliosLightstickTestTarget : public APawn
{
    GENERATED_BODY()
public:
    AHeliosLightstickTestTarget();
    virtual float TakeDamage(float Amount,const FDamageEvent&,AController*,AActor*) override { TotalDamage+=Amount;++HitCount;return Amount; }
    UPROPERTY() TObjectPtr<USphereComponent> Sphere;
    float TotalDamage=0;
    int32 HitCount=0;
};
UCLASS()
class HELIOSOUTPOST_API AHeliosLightstickDemo : public AActor
{
    GENERATED_BODY()
public:
    AHeliosLightstickDemo();
    virtual void Tick(float Dt) override;
private:
    void Capture(const FString& Name);
    void Check(const FString& Name,bool Passed);
    void RunFocusedChecks();
    UPROPERTY() TObjectPtr<AHeliosAdventureCharacter> Player;
    UPROPERTY() TObjectPtr<AHeliosAdventureDirector> Director;
    UPROPERTY() TObjectPtr<AHeliosEnemyActor> Target;
    UPROPERTY() TObjectPtr<AStaticMeshActor> Wall;
    UPROPERTY() TArray<TObjectPtr<AHeliosEnemyActor>> FarTargets;
    UPROPERTY() TObjectPtr<class AHeliosLightstickWave> PausedWave;
    FVector OriginalLocation;
    int32 PausedSlashSlot=24;
    float PausedSlashAge=0;
    int32 Stage=0,Frame=0,Failures=0;
    float Age=0,Before=0,ChargeBefore=0;
    int32 WaveSerialBefore=0;
    double PauseStarted=0,Started=0;
    FString Results;
};
