#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HeliosHunterDamageFeedback.generated.h"
UCLASS()
class HELIOSOUTPOST_API UHeliosHunterDamageFeedback : public UActorComponent
{
 GENERATED_BODY()
public:
 UHeliosHunterDamageFeedback();
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Fn) override;
 bool Emit(uint64 DamageEvent);
 void Clear();
 int32 Played=0,Duplicates=0,Replaced=0;
 int32 ActiveCount() const;
 bool bZXK=false;
private:
 UPROPERTY(Transient) TObjectPtr<class USoundBase> Sound;
 UPROPERTY(Transient) TArray<TObjectPtr<class UAudioComponent>> Channels;
 TArray<uint64> RecentEvents;
 double LastPlayed=-1000;
};
