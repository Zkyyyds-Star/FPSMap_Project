#pragma once
#include "CoreMinimal.h"
#include "Boss/HeliosEnemyBoss.h"
#include "HeliosUrbanBoss.generated.h"
class UStaticMeshComponent;
class UTextRenderComponent;
class AHeliosUrbanHazard;
class UAnimSequence;

/** Independent urban controller, reference-informed giant and synchronized authored skills. */
UCLASS()
class HELIOSOUTPOST_API AHeliosUrbanBoss : public AHeliosEnemyBoss
{
    GENERATED_BODY()
    friend class AHeliosUrbanQA;
public:
    AHeliosUrbanBoss();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void StartShooting(AActor*) override {}
    virtual void StopShooting() override {}
    FString GetUrbanSkillText() const;
    float GetUrbanWarningProgress() const;
    int32 GetBasketballCasts() const { return BasketballCasts; }
    int32 GetChargeCasts() const { return ChargeCasts; }
    int32 GetChargeContacts() const { return ChargeContacts; }
    bool HasUrbanCharacter() const { return bUrbanCharacterReady; }
    float GetUrbanCharacterHeight() const { return UrbanCharacterHeight; }
    int32 GetUrbanAnimationCount() const;
protected:
    virtual void ApplyTypeDefaults() override;
    virtual void LoadAnimationAssets() override;
    virtual void BeginAttackCycle() override {}
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Die() override;
private:
    void StartBasketballs(APawn* Target);
    void StartCharge(APawn* Target);
    void TickCharge(float DeltaSeconds);
    void SpawnHazard(FVector Foot,bool bBasketball,float Radius,float Warning,float Damage);
    void ClearUrbanSkills();
    void TickUrbanPresentation(float DeltaSeconds);
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Placeholder;
    UPROPERTY() TObjectPtr<UTextRenderComponent> Label;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> HeldBall;
    UPROPERTY() TObjectPtr<UAnimSequence> DribbleAnimation;
    UPROPERTY() TObjectPtr<UAnimSequence> BarrageAnimation;
    UPROPERTY() TObjectPtr<UAnimSequence> UrbanChargeWindup;
    UPROPERTY() TObjectPtr<UAnimSequence> UrbanChargeRun;
    UPROPERTY() TObjectPtr<UAnimSequence> RuptureAnimation;
    float PresentationAge=0;
    float DeathAge=0;
    float UrbanCharacterHeight=0;
    bool bUrbanCharacterReady=false;
    UPROPERTY() TArray<TObjectPtr<AHeliosUrbanHazard>> Hazards;
    TSet<TWeakObjectPtr<APawn>> ChargeHitTargets;
    FVector ChargeOrigin=FVector::ZeroVector;
    FVector ChargeEnd=FVector::ZeroVector;
    FVector ChargeDirection=FVector::ForwardVector;
    float Cooldown=4;
    float SkillAge=0;
    float ChargeWarning=2.8f;
    float BasketballWarning=3.2f;
    float BasketballDamage=55;
    float ChargeDamage=85;
    float ChargeWidth=240;
    float ChargeLength=0;
    float Travel=0;
    float MoveRefresh=0;
    int32 Skill=0;
    int32 BasketballCasts=0;
    int32 ChargeCasts=0;
    int32 ChargeContacts=0;
    bool bNextCharge=false;
};
