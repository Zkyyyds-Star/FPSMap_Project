#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosBlackDragonSoulQA.generated.h"
UCLASS()
class AHeliosBlackDragonSoulQA : public AActor
{
 GENERATED_BODY()
public:
 AHeliosBlackDragonSoulQA();
 virtual void Tick(float Dt) override;
private:
 void Check(const TCHAR* Name,bool Pass);
 void Capture(const TCHAR* Name);
 void Finish();
 class AHeliosEnemyActor* SpawnTarget(float Distance,bool Boss=false);
 class AHeliosEnemyActor* PassiveEnemy(UClass* Type,const FVector& At,float HP=0);
 void BlastGroup();
 void ClearFixtures();
 void StartMotionCase();
 bool TickMotion(float Dt);
 int MotionCase=0,MotionPhase=0,MotionFrames=0,MotionImages=0,MotionShotsBefore=0;
 float MotionAge=0,MotionCaptureAge=0,MotionPhaseAge=0;
 uint32 MotionLastCommit=0;
 bool MotionSingleCommit=true,MotionSourcesHidden=true,MotionGunFollows=true,MotionMoved=false;
 TArray<TSharedPtr<class FJsonValue>> MotionSamples;
 int Stage=0,Failures=0;double Started=0,Next=0;float Age=0,NearDamage=0,FarDamage=0,RemainingBeforePause=0;
 FString Out;TArray<TSharedPtr<class FJsonValue>> Checks;
 UPROPERTY() TObjectPtr<class AHeliosArenaCharacter> Player;
 UPROPERTY() TObjectPtr<class AHeliosEnemyActor> Target;
 UPROPERTY() TObjectPtr<class AHeliosHumanoidPet> Pet;
 UPROPERTY() TObjectPtr<class ACameraActor> Camera;
 UPROPERTY() TObjectPtr<class AHeliosEnemyActor> Edge;
 UPROPERTY() TObjectPtr<class AHeliosEnemyActor> Outside;
 UPROPERTY() TObjectPtr<class AHeliosEnemyActor> Blocked;
 UPROPERTY() TObjectPtr<class AHeliosEnemyActor> Friendly;
 UPROPERTY() TArray<TObjectPtr<class AHeliosEnemyActor>> Ordinary;
 UPROPERTY() TArray<TObjectPtr<AActor>> Fixtures;
 UPROPERTY() TObjectPtr<class AHeliosBlackDragonSoulProjectile> Projectile;
 FVector PausedProjectile=FVector::ZeroVector;
 int BeforeKnockbacks=0;
};
