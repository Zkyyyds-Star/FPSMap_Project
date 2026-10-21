#include "Health/CreatureHealthSettings.h"
#include "Combat/CombatDamageSettings.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogCreatureHealth, Log, All);

namespace
{
    struct FHealthDefault { const TCHAR* Name; float Value; };
    const FHealthDefault CreatureHealthDefaults[] = {
        {TEXT("玩家"),100.f}, {TEXT("步枪兵"),200.f}, {TEXT("冲锋兵"),300.f},
        {TEXT("游击兵"),80.f}, {TEXT("篮球兵"),400.f}, {TEXT("Boss"),3600.f},
        {TEXT("刀盾狗"),150.f}, {TEXT("蝴蝶·雷神助战"),150.f},
        {TEXT("万魂幡骷髅战士"),150.f}, {TEXT("万魂幡骷髅法师"),100.f},
        {TEXT("射击模板玩家"),500.f}, {TEXT("射击模板普通敌人"),100.f}, {TEXT("孙尚香"),600.f}
    };
    static_assert(UE_ARRAY_COUNT(CreatureHealthDefaults) == static_cast<int32>(ECreatureHealthType::Count));
}

FCreatureHealthTable::FCreatureHealthTable()
{
    for (const auto& Entry : CreatureHealthDefaults) Values.Add(Entry.Value);
}

float FCreatureHealthTable::Default(ECreatureHealthType Type) { return CreatureHealthDefaults[static_cast<int32>(Type)].Value; }
const TCHAR* FCreatureHealthTable::Name(ECreatureHealthType Type) { return CreatureHealthDefaults[static_cast<int32>(Type)].Name; }
float FCreatureHealthTable::Get(ECreatureHealthType Type) const { return Values[static_cast<int32>(Type)]; }

void FCreatureHealthTable::LoadFile(const FString& Path, bool bLogWarnings)
{
    *this = FCreatureHealthTable();
    FString Contents;
    if (!FFileHelper::LoadFileToString(Contents, *Path))
    {
        if (bLogWarnings)
        {
            UE_LOG(LogCreatureHealth, Warning, TEXT("Cannot read %s; using default creature health."), *Path);
            for (const auto& Entry : CreatureHealthDefaults)
                UE_LOG(LogCreatureHealth, Display, TEXT("COMBAT_VALUE 生物血量.ini|生物血量|%s=%.9g"), Entry.Name, Entry.Value);
        }
        return;
    }
    FConfigFile Config;
    Config.ProcessInputFileContents(Contents, Path);
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(CreatureHealthDefaults); ++Index)
    {
        FString Text;
        const bool bPresent = Config.GetString(TEXT("生物血量"), CreatureHealthDefaults[Index].Name, Text);
        Text.TrimStartAndEndInline();
        const double Parsed = FCString::Atod(*Text);
        const float Value = static_cast<float>(Parsed);
        if (bPresent && !Text.IsEmpty() && Text.IsNumeric() && FMath::IsFinite(Value) && Value > 0.f)
            Values[Index] = Value;
        else if (bLogWarnings)
            UE_LOG(LogCreatureHealth, Warning, TEXT("%s: %s raw='%s' reason=%s; using %.9g."),
                *Path, CreatureHealthDefaults[Index].Name, *Text,
                !bPresent ? TEXT("missing key") : Text.IsEmpty() ? TEXT("empty value") :
                !Text.IsNumeric() ? TEXT("not a number") : !FMath::IsFinite(Value) ? TEXT("not finite") :
                TEXT("health must be positive"), CreatureHealthDefaults[Index].Value);
        if (bLogWarnings)
            UE_LOG(LogCreatureHealth, Display, TEXT("COMBAT_VALUE 生物血量.ini|生物血量|%s=%.9g"), CreatureHealthDefaults[Index].Name, Values[Index]);
    }
}

void UCreatureHealthSettings::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const FString Path = UCombatDamageSettings::Directory() / TEXT("生物血量.ini");
    Table.LoadFile(Path);
    UE_LOG(LogCreatureHealth, Display, TEXT("Loaded health settings for map %s: %s"), *GetWorld()->GetMapName(), *Path);
}
bool UCreatureHealthSettings::DoesSupportWorldType(EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

float UCreatureHealthSettings::Resolve(const UObject* WorldContext, ECreatureHealthType Type)
{
    if (const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr)
        if (const auto* Settings = World->GetSubsystem<UCreatureHealthSettings>())
            return Settings->Table.Get(Type);
    // Editor previews and class defaults have no play session.
    return FCreatureHealthTable::Default(Type);
}
