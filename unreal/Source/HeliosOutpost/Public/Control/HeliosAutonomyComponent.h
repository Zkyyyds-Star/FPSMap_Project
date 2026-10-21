#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NavigationPath.h"
#include "HeliosAutonomyComponent.generated.h"
class AHeliosArenaCharacter;
class AHeliosEnemyActor;

struct FHeliosEnemyMemory
{
    TWeakObjectPtr<AHeliosEnemyActor> Actor;
    FVector Location=FVector::ZeroVector;
    FString Type;
    float Health=0;
    double SeenAt=0;
    bool bVisible=false;
};

/** Goal execution on the normal player pawn; does not possess it or bypass movement/weapon rules. */
UCLASS()
class HELIOSOUTPOST_API UHeliosAutonomyComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UHeliosAutonomyComponent();
    virtual void TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* Function) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    TSharedPtr<class FJsonObject> SetGoal(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> GoalStatus(const FString& Id=TEXT("")) const;
    TSharedPtr<FJsonObject> Observation();
    void Cancel(const FString& Reason);
    void ResetMemory();
    bool IsActive() const { return Status==TEXT("accepted")||Status==TEXT("running"); }
    bool IsVisible(AHeliosEnemyActor* Enemy) const;
    void Sense();
private:
    AHeliosArenaCharacter* Player() const;
    void Decide();
    void RequestPath(const FVector& Location);
    void PathFailed();
    void Finish(const FString& NewStatus,const FString& Reason);
    void ClearInputs();
    TMap<FString,FHeliosEnemyMemory> Memory;
    TMap<FString,int32> Visited;
    TMap<FString,TSharedPtr<FJsonObject>> History;
    TArray<FVector> PathPoints;
    FString GoalId, Kind, TargetId, Status=TEXT("idle"), Action=TEXT("idle"), Detail;
    FString CombatTarget;
    FVector Destination=FVector::ZeroVector, PathDestination=FVector::ZeroVector, ProgressLocation=FVector::ZeroVector;
    int32 PathIndex=0, Failures=0;
    uint32 PathGeneration=0;
    bool bPathPending=false, bFiring=false;
    float SenseRemaining=0, DecisionRemaining=0;
    double StartedAt=0, ProgressAt=0, LastPathAt=0;
};
