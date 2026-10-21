#pragma once

#include "Combat/CombatDamageSettings.h"
#include "Health/CreatureHealthSettings.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"

// The isolated runner writes this oracle directly from its fixture values.
// Never compute an edited fixture's expected result through the production loader.
struct FHeliosSettingsExpected
{
    FCombatDamageTable Damage;
    FCreatureHealthTable Health;
    TSharedPtr<FJsonObject> Oracle;

    static FString Key(ECombatDamageType T)
    {
        return FString::Printf(TEXT("%s|%s|%s"), FCombatDamageTable::File(T), FCombatDamageTable::Section(T), FCombatDamageTable::Key(T));
    }
    static FString Key(ECreatureHealthType T)
    {
        return FString(TEXT("生物血量.ini|生物血量|")) + FCreatureHealthTable::Name(T);
    }
    bool Load(FAutomationTestBase& Test)
    {
        FString Path;
        if (!FParse::Value(FCommandLine::Get(), TEXT("HeliosExpectedCombat="), Path))
        {
            Damage.LoadDirectory(UCombatDamageSettings::Directory());
            Health.LoadFile(UCombatDamageSettings::Directory() / TEXT("生物血量.ini"));
            return true;
        }
        FString Text;
        if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Oracle) || !Oracle)
        {
            Test.AddError(TEXT("Cannot read independent combat settings oracle: ") + Path);
            return false;
        }
        bool Valid = true;
        auto Check = [&](const FString& K)
        {
            double Value;
            if (!Oracle->TryGetNumberField(K, Value) || !FMath::IsFinite(Value))
            { Test.AddError(TEXT("Missing/invalid independent expectation: ") + K); Valid = false; }
        };
        for (int32 I=0; I<static_cast<int32>(ECombatDamageType::Count); ++I) Check(Key(static_cast<ECombatDamageType>(I)));
        for (int32 I=0; I<static_cast<int32>(ECreatureHealthType::Count); ++I) Check(Key(static_cast<ECreatureHealthType>(I)));
        return Valid;
    }
    float Get(ECombatDamageType T) const { return Oracle ? float(Oracle->GetNumberField(Key(T))) : Damage.Get(T); }
    float Get(ECreatureHealthType T) const { return Oracle ? float(Oracle->GetNumberField(Key(T))) : Health.Get(T); }
};
