#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosWeaponQA.generated.h"
class AHeliosAdventureDirector;
class AHeliosAdventureCharacter;
class AHeliosEnemyActor;
class AHeliosRocketProjectile;
class AShooterWeapon;
class FJsonObject;
class FJsonValue;
UCLASS()
class HELIOSOUTPOST_API AHeliosWeaponQA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosWeaponQA();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
private:
    void Input(FName Action, EInputEvent Event);
    void Check(const FString& Name, bool Passed, const FString& Detail = FString());
    void StartWeapon();
    void SetStage(int32 NewStage);
    void CleanupFixture();
    AHeliosEnemyActor* SpawnTarget(FVector Location);
    AShooterWeapon* FindPlayerWeapon() const;
    void PrepareDirectionalRun(const FRotator& Rotation, bool bSpawnTarget);
    void Finish();
    void TickExplosionDemo(float DeltaSeconds);
    int32 DemoFrame = -150;
    UPROPERTY() TObjectPtr<AHeliosAdventureDirector> Director;
    UPROPERTY() TObjectPtr<AHeliosAdventureCharacter> Player;
    UPROPERTY() TObjectPtr<AHeliosEnemyActor> Target;
    UPROPERTY() TObjectPtr<AActor> Wall;
    UPROPERTY() TObjectPtr<AHeliosRocketProjectile> ObservedRocket;
    TArray<TSharedPtr<FJsonValue>> Checks;
    FVector Origin = FVector(20000,20000,2000);
    double Started = 0;
    double StageWall = 0;
    float StageGame = 0;
    float HealthBefore = 0;
    int32 AmmoBefore = 0;
    int32 Stage = 0;
    int32 WeaponIndex = 0;
    int32 DirectionIndex = 0;
    int32 Failures = 0;
    FVector ExpectedDirection = FVector::ForwardVector;
    FVector RocketStart = FVector::ZeroVector;
    bool bFinished = false;
};
