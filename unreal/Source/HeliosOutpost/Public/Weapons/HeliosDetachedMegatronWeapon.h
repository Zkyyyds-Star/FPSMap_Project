#pragma once
#include "CoreMinimal.h"
#include "Weapons/HeliosYunSummerMegatron.h"
#include "HeliosDetachedMegatronWeapon.generated.h"

/** Preserves the frozen source gun while removing character side effects from mixed loadouts. */
UCLASS()
class HELIOSOUTPOST_API AHeliosDetachedMegatronWeapon : public AHeliosYunSummerMegatronWeapon
{
    GENERATED_BODY()
public:
    virtual void ApplyDefinition(const UHeliosWeaponDefinition* Definition) override;
    virtual void ActivateWeapon(const FName& Tag) override;
    virtual void Tick(float Dt) override;
    virtual bool HandleOwnerCombatCue(FName Cue) override;
protected:
    virtual void FireProjectile(const FVector& Target) override;
private:
    void KeepCharacterOwnership();
    bool bDetachedVoice = false;
    int32 ObservedKills = 0;
};
