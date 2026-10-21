#pragma once

#include "CoreMinimal.h"
#include "Misc/ConfigCacheIni.h"
#include "Subsystems/WorldSubsystem.h"
#include "HeliosCombatTempoSettings.generated.h"

enum class EHeliosTempoFile : uint8 { Attack, Movement, Recovery };

/** The three text files are read once for each gameplay world. */
struct HELIOSOUTPOST_API FHeliosCombatTempoTable
{
    void LoadDirectory(const FString& Directory, bool bLogWarnings = true);
    float Get(EHeliosTempoFile File, const TCHAR* Section, const TCHAR* Key,
        float Fallback, float Minimum = 0.01f) const;
    static const TCHAR* Filename(EHeliosTempoFile File);

private:
    FConfigFile Attack;
    FConfigFile Movement;
    FConfigFile Recovery;
    bool bWarnings = true;
    mutable TSet<FString> ReportedKeys;
};

UCLASS()
class HELIOSOUTPOST_API UHeliosCombatTempoSettings : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

    static float Resolve(const UObject* Context, EHeliosTempoFile File,
        const TCHAR* Section, const TCHAR* Key, float Fallback, float Minimum = 0.01f);
    /** Same map-owned INI snapshot and staged Settings directory as existing combat files. */
    static float Hunter(const UObject* Context, bool ZhongXiaoKui, bool Pet,
        const TCHAR* Key, float Fallback, float Minimum=0.f, float Maximum=100000.f);

private:
    FHeliosCombatTempoTable Table;
    FConfigFile HunterFiles[2];
    mutable TSet<FString> HunterReported;
};
