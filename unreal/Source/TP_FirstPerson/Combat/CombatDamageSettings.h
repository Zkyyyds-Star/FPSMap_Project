#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CombatDamageSettings.generated.h"

enum class ECombatDamageType : uint8
{
    Rifle, Pistol, Grenade, TemplateBullet, TemplateGrenade, RocketMax, RocketMin, Laser, ShotgunMax, ShotgunMin, Chopsticks, ColaMax, ColaMin, Light1, Light2, Light3, HeavyMin, HeavyMax, Soul1, Soul2, Soul3, SoulWarrior, SoulMage, QingxiaoSlash, QingxiaoSword, Earthquake, FireMax, FireMin, Tornado, Poison, TowerScale, Dog, DogReduction, Ultimatum, EnemyRifle, EnemyCharger, EnemySkirmisher, EnemyBasketball, BossLegacy, BossClose, BossCharge, NPCBullet, NPCGrenade, MysteriousLaser, MysteriousMissileMax, MysteriousMissileMin, MechSweep, MechStomp, MechBeam, SSXLight, SSXHeavy, SSXSkill, SSXPetLight, SSXPetHeavy, SSXPetSkill, Count
};

/** A validated snapshot. Every key has its own fallback; zero damage is valid. */
struct TP_FIRSTPERSON_API FCombatDamageTable
{
    FCombatDamageTable();
    void LoadDirectory(const FString& Directory, bool bLogWarnings = true);
    float Get(ECombatDamageType Type) const;
    static float Default(ECombatDamageType Type);
    static const TCHAR* File(ECombatDamageType Type);
    static const TCHAR* Section(ECombatDamageType Type);
    static const TCHAR* Key(ECombatDamageType Type);
private:
    TArray<float> Values;
};

/** Immutable during a map's lifetime; each new gameplay world reads fresh files. */
UCLASS()
class TP_FIRSTPERSON_API UCombatDamageSettings : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
    static float Resolve(const UObject* Context, ECombatDamageType Type);
    static FString Directory();
private:
    FCombatDamageTable Table;
};
