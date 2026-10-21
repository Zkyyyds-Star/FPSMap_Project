#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Pets/HeliosHumanoidPet.h"
#include "HeliosGuanXiaoYuQA.generated.h"
class AHeliosArenaCharacter;
class APlayerController;
class AHeliosAdventureHUD;
class AHeliosAdventureDirector;
class AShooterWeapon;
class FJsonValue;
UCLASS(NotBlueprintable)
class HELIOSOUTPOST_API AHeliosGuanXiaoYuQAPet : public AHeliosHumanoidPet
{
    GENERATED_BODY()
public:
    void ProbeAttack(){UpdateAttack(1.f/60);}
    void ProbeMotion(float Dt){UpdateLocomotion(Dt);SetPose(GetVelocity().Size2D()>20?FName("Move"):FName("Idle"));}
};
UCLASS(NotBlueprintable)
class HELIOSOUTPOST_API AHeliosGuanXiaoYuQA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosGuanXiaoYuQA();
    virtual void BeginPlay()override;
    virtual void Tick(float Dt)override;
private:
    void Check(const FString& Name,bool Passed);
    void Advance(int32 Next);
    bool Capture(const FString& Name);
    void Finish();
    void TickMotion(float Dt,double Age);
    void TickFirstPerson(float Dt,double Age);
    FString Out,Case;
    int32 Phase=0,CaseIndex=0,Failures=0,Ammo=0;
    double PhaseStarted=0,Started=0;
    bool bMotionQA=false,bFirstPersonQA=false,bOnePosePerFrame=true;
    int32 MotionCapture=0,MotionFrames=0;
    uint32 LastMotionCommit=0;
    uint64 LastMotionFrame=0;
    TArray<TSharedPtr<FJsonValue>> Checks;
    UPROPERTY() TObjectPtr<AHeliosArenaCharacter> Player;
    UPROPERTY() TObjectPtr<APlayerController> Controller;
    UPROPERTY() TObjectPtr<AHeliosAdventureHUD> HUD;
    UPROPERTY() TObjectPtr<AHeliosAdventureDirector> Director;
    UPROPERTY() TObjectPtr<AHeliosEnemyActor> Target;
    UPROPERTY() TObjectPtr<AHeliosGuanXiaoYuQAPet> Pet;
    UPROPERTY() TObjectPtr<AShooterWeapon> Primary;
};
