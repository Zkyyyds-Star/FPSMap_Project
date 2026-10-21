#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CreatureHealthSettings.generated.h"

enum class ECreatureHealthType : uint8
{
    Player, Rifle, Charger, Skirmisher, Basketball, Boss,
    SwordDog, Ultimatum, SoulWarrior, SoulMage, ShooterPlayer, ShooterNPC, SunShangXiang, Count
};

/** One snapshot of the editable health file. Invalid entries retain their own defaults. */
struct TP_FIRSTPERSON_API FCreatureHealthTable
{
    FCreatureHealthTable();
    void LoadFile(const FString& Path, bool bLogWarnings = true);
    float Get(ECreatureHealthType Type) const;
    static float Default(ECreatureHealthType Type);
    static const TCHAR* Name(ECreatureHealthType Type);
private:
    TArray<float> Values;
};

/** Map-owned snapshot loaded before actors begin play; travel/reload reads fresh files. */
UCLASS()
class TP_FIRSTPERSON_API UCreatureHealthSettings : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
    static float Resolve(const UObject* WorldContext, ECreatureHealthType Type);
private:
    FCreatureHealthTable Table;
};
