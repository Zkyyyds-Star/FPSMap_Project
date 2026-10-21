#if WITH_DEV_AUTOMATION_TESTS
#include "QA/HeliosSettingsExpected.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosSettingsSessionTest,"Helios.Damage.SessionSnapshots",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHeliosSettingsSessionTest::RunTest(const FString&)
{
    // This test changes only the private host's files, never the live project.
    FString OraclePath;
    FString Project=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    FPaths::NormalizeDirectoryName(Project);
    if(!FParse::Value(FCommandLine::Get(),TEXT("HeliosExpectedCombat="),OraclePath)
        || !Project.Contains(TEXT("/qa/combat_settings_repair/")) || !Project.EndsWith(TEXT("/host/unreal")))
    { AddInfo(TEXT("Session file-edit checks require the isolated verification runner.")); return true; }
    FHeliosSettingsExpected Expected;if(!Expected.Load(*this))return false;
    FTestWorldWrapper A;
    if(!A.CreateTestWorld(EWorldType::Game))return false;
    A.GetTestWorld()->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();
    if(!A.BeginPlayInTestWorld())return false;
    struct FRestore
    {
        TMap<FString,TArray<uint8>> Files;
        ~FRestore(){for(const auto& P:Files)FFileHelper::SaveArrayToFile(P.Value,*P.Key);}
    } Restore;
    TMap<FString,FString> Edited;
    for(int32 I=0;I<static_cast<int32>(ECombatDamageType::Count);++I)
    {
        const auto T=static_cast<ECombatDamageType>(I);
        Edited.FindOrAdd(FCombatDamageTable::File(T))+=FString::Printf(TEXT("[%s]\n%s=%.9g\n"),
            FCombatDamageTable::Section(T),FCombatDamageTable::Key(T),Expected.Get(T)*.25f);
    }
    FString Health=TEXT("[生物血量]\n");
    for(int32 I=0;I<static_cast<int32>(ECreatureHealthType::Count);++I)
    {
        const auto T=static_cast<ECreatureHealthType>(I);
        Health+=FString::Printf(TEXT("%s=%.9g\n"),FCreatureHealthTable::Name(T),Expected.Get(T)*2);
    }
    Edited.Add(TEXT("生物血量.ini"),Health);
    for(const auto& P:Edited)
    {
        const FString Path=UCombatDamageSettings::Directory()/P.Key;
        TArray<uint8> Original;
        if(!TestTrue(TEXT("Read isolated fixture"),FFileHelper::LoadFileToArray(Original,*Path)))return false;
        Restore.Files.Add(Path,MoveTemp(Original));
        if(!TestTrue(TEXT("Edit isolated fixture"),FFileHelper::SaveStringToFile(P.Value,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))return false;
    }
    FTestWorldWrapper B;
    if(!B.CreateTestWorld(EWorldType::Game))return false;
    B.GetTestWorld()->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();
    if(!B.BeginPlayInTestWorld())return false;
    // Same game instance, new world: disk edits must apply at map initialization.
    UWorld* NextMap=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Travel world"),NextMap))return false;
    NextMap->SetGameInstance(A.GetTestWorld()->GetGameInstance());
    for(int32 I=0;I<static_cast<int32>(ECombatDamageType::Count);++I)
    {
        const auto T=static_cast<ECombatDamageType>(I);
        TestEqual(TEXT("Original session keeps damage"),UCombatDamageSettings::Resolve(A.GetTestWorld(),T),Expected.Get(T));
        TestEqual(TEXT("New session sees changed damage"),UCombatDamageSettings::Resolve(B.GetTestWorld(),T),Expected.Get(T)*.25f);
        TestEqual(TEXT("Map transition reads changed damage"),UCombatDamageSettings::Resolve(NextMap,T),Expected.Get(T)*.25f);
    }
    for(int32 I=0;I<static_cast<int32>(ECreatureHealthType::Count);++I)
    {
        const auto T=static_cast<ECreatureHealthType>(I);
        TestEqual(TEXT("Original session keeps health"),UCreatureHealthSettings::Resolve(A.GetTestWorld(),T),Expected.Get(T));
        TestEqual(TEXT("New session sees changed health"),UCreatureHealthSettings::Resolve(B.GetTestWorld(),T),Expected.Get(T)*2);
        TestEqual(TEXT("Map transition reads changed health"),UCreatureHealthSettings::Resolve(NextMap,T),Expected.Get(T)*2);
    }
    NextMap->SetGameInstance(nullptr);NextMap->DestroyWorld(false);
    return true;
}
#endif
