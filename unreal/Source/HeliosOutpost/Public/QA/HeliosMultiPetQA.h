#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosMultiPetQA.generated.h"

UCLASS()
class HELIOSOUTPOST_API AHeliosMultiPetQA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosMultiPetQA();
    virtual void Tick(float Dt) override;
private:
    void Check(const FString& Name,bool Pass);
    void Finish(bool Exit=true);
    void Shot(const FString& Name);
    void AuditCapsules(const FString& Name);
    FString Out;
    int32 Stage=0,CountCase=0,KindCase=0,Failures=0;
    double Started=0,Next=0,PreviousFrame=0;
    FVector FollowStart;
    FVector WalkStart;
    UPROPERTY(Transient) TObjectPtr<AActor> SpawnBlocker;
    TArray<FVector> PetStarts;
    TArray<int32> BeforeHits;
    TArray<TSharedPtr<class FJsonValue>> Checks,Samples;
    TArray<double> Frames;
    bool bFinished=false,bRecording=false;
    UPROPERTY(Transient) TObjectPtr<class AHeliosEnemyActor> Target;
    UPROPERTY(Transient) TObjectPtr<class ACameraActor> Camera;
};
