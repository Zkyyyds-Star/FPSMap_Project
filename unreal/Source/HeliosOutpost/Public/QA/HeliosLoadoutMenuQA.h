#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Player/HeliosLoadout.h"
#include "HeliosLoadoutMenuQA.generated.h"

class AHeliosAdventureHUD;
class FJsonValue;

/** Bounded real Slate clicks and six captures of the independent loadout menu. */
UCLASS(NotBlueprintable)
class HELIOSOUTPOST_API AHeliosLoadoutMenuQA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosLoadoutMenuQA();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
private:
    void BuildSteps();
    void Check(const FString& Name,bool bPass);
    void ClickCheck(const TCHAR* Label,const TCHAR* Name);
    void Capture(const TCHAR* Name);
    void WriteReport() const;
    void Finish();
    TWeakObjectPtr<AHeliosAdventureHUD> HUD;
    TArray<TFunction<void()>> Steps;
    TArray<TSharedPtr<FJsonValue>> Checks;
    TArray<FString> Captures;
    TArray<FHeliosLoadout> Roster;
    TArray<FName> BeforePrimary;
    FHeliosLoadout PlayerExpected;
    FString Out;
    double Started=0,Next=0;
    int32 Step=0,Failures=0;
    bool bFinished=false;
};
