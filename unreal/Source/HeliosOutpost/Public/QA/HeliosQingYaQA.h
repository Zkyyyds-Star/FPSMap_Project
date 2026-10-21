#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosQingYaQA.generated.h"
class AHeliosArenaCharacter;
class APlayerController;
class AHeliosAdventureHUD;
class AHeliosAdventureDirector;
class AHeliosHumanoidPet;
class AHeliosEnemyActor;
class FJsonValue;
UCLASS(NotBlueprintable)
class HELIOSOUTPOST_API AHeliosQingYaQA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosQingYaQA();
    virtual void BeginPlay()override;
    virtual void Tick(float Dt)override;
private:
    void TickTestbed();
    void Check(const FString& Name,bool Passed);
    void Advance(int32 Next);
    bool Capture(const FString& Name);
    void Finish();
    FString Out,Case;
    int32 Phase=0,CaseIndex=0,Failures=0,PoseIndex=0,Draws=0,Ammo=0,CompletedPairs=0;
    double Started=0,PhaseStarted=0;
    TArray<FName> Weapons;
    TArray<TSharedPtr<FJsonValue>> Checks;
    UPROPERTY() TObjectPtr<AHeliosArenaCharacter> Player;
    UPROPERTY() TObjectPtr<APlayerController> PC;
    UPROPERTY() TObjectPtr<AHeliosAdventureHUD> HUD;
    UPROPERTY() TObjectPtr<AHeliosAdventureDirector> Director;
    UPROPERTY() TObjectPtr<AHeliosHumanoidPet> Pet;
    UPROPERTY() TObjectPtr<AHeliosEnemyActor> Target;
};
