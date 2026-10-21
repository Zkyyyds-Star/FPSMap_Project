#if WITH_DEV_AUTOMATION_TESTS
#include "Player/HeliosLoadout.h"
#include "Player/HeliosCharacterDefinition.h"
#include "Pets/HeliosPetDog.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosLoadoutMigrationTest,"Helios.Loadout.Migration",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHeliosLoadoutMigrationTest::RunTest(const FString& Parameters)
{
    const FHeliosLoadout Default=FHeliosLoadout::MakeDefault();
    TestEqual(TEXT("New character is Chan"),Default.CharacterId,FName(TEXT("cfm_chan")));
    TestEqual(TEXT("New primary is Zeus"),Default.PrimaryWeaponId,FName(TEXT("cfm_chan_zeus")));
    for(FName Id:{FName(TEXT("rifle")),FName(TEXT("rocket")),FName(TEXT("laser")),FName(TEXT("shotgun"))})
        TestTrue(TEXT("Retired weapon migrates to the complete default pair"),FHeliosLoadout::FromLegacyWeapon(Id)==Default);
    const TArray<TPair<FName,FName>> Pairs={
        {TEXT("cfm_sunshangxiang"),TEXT("cfm_sunshangxiang")},
        {TEXT("cfm_zhongxiaokui"),TEXT("cfm_zhongxiaokui")},
        {TEXT("cfm_chan_zeus"),TEXT("cfm_chan")},
        {TEXT("cfm_bulianshi_p90"),TEXT("cfm_bulianshi")},
        {TEXT("cfm_wangzhekui_thunder"),TEXT("cfm_wangzhekui")},
        {TEXT("cfm_yun_summer_megatron"),TEXT("cfm_yun_summer")}};
    for(const auto& Pair:Pairs)
    {
        const auto L=FHeliosLoadout::FromLegacyWeapon(Pair.Key);
        TestEqual(TEXT("Legacy pair keeps its character"),L.CharacterId,Pair.Value);
        TestEqual(TEXT("Legacy pair keeps its weapon"),L.PrimaryWeaponId,Pair.Key);
    }
    auto Yun=FHeliosLoadout::FromLegacyWeapon(TEXT("cfm_yun_summer_megatron"));
    Yun.CharacterVariantId=TEXT("GR");
    TestTrue(TEXT("Yun keeps identity and saved form"),FHeliosLoadout::Normalize(Yun)==Yun);
    FString YunError;
    TestTrue(TEXT("Yun keeps its original primary"),FHeliosLoadout::Validate(Yun,YunError));
    for(FName Id:{FName(TEXT("mech")),FName(TEXT("fighter"))})
    {
        auto CompatibleYun=Yun;CompatibleYun.PrimaryWeaponId=Id;
        TestTrue(TEXT("Yun remains compatible with vehicle modes"),FHeliosLoadout::Validate(CompatibleYun,YunError));
    }
    auto CrossWeaponYun=Yun;CrossWeaponYun.PrimaryWeaponId=TEXT("cfm_chan_zeus");
    TestTrue(TEXT("Ready Yun can independently equip Zeus"),FHeliosLoadout::Validate(CrossWeaponYun,YunError));
    const auto UnspecifiedYun=FHeliosLoadout::FromLegacyWeapon(TEXT("cfm_yun_summer_megatron"));
    TestTrue(TEXT("Legacy Yun does not overwrite the existing form session"),UnspecifiedYun.CharacterVariantId.IsNone());
    TestEqual(TEXT("Legacy enum dog stays zero"),int32(EHeliosPetKind::SwordDog),0);
    TestEqual(TEXT("Retired butterfly leaves its old numeric hole"),int32(EHeliosPetKind::UltimatumGR),1);
    TestEqual(TEXT("Yun numeric value is not shifted"),int32(EHeliosPetKind::YunSummerMegatron),7);
    auto Dog=Default;Dog.CharacterId=TEXT("sword_dog");Dog=FHeliosLoadout::Normalize(Dog);
    TestTrue(TEXT("Dog never inherits a human weapon"),Dog.PrimaryWeaponId.IsNone());
    for(FName Id:{FName(TEXT("mech")),FName(TEXT("fighter"))})
        TestEqual(TEXT("Vehicle mode remains intact"),FHeliosLoadout::FromLegacyWeapon(Id).PrimaryWeaponId,Id);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosLoadoutJsonTest,"Helios.Loadout.JsonAndIndependence",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHeliosLoadoutJsonTest::RunTest(const FString& Parameters)
{
    auto Original=FHeliosLoadout::MakeDefault();
    auto CharacterChange=Original;CharacterChange.CharacterId=TEXT("cfm_wangzhekui");
    CharacterChange=FHeliosLoadout::Normalize(CharacterChange);
    TestEqual(TEXT("Changing character keeps the primary"),CharacterChange.PrimaryWeaponId,Original.PrimaryWeaponId);
    auto WeaponChange=CharacterChange;WeaponChange.PrimaryWeaponId=TEXT("cfm_bulianshi_p90");
    WeaponChange=FHeliosLoadout::Normalize(WeaponChange);
    TestEqual(TEXT("Changing primary keeps character identity"),WeaponChange.CharacterId,CharacterChange.CharacterId);
    auto Json=MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("checkpoint_id"),TEXT("checkpoint_saved"));
    Json->SetNumberField(TEXT("exploration_score"),1234);
    HeliosLoadout::WriteJson(Json,WeaponChange);
    FHeliosLoadout Restored;
    TestTrue(TEXT("New loadout JSON is readable"),HeliosLoadout::ReadJson(Json,Restored));
    TestTrue(TEXT("Character and primary round trip independently"),Restored==WeaponChange);
    TestEqual(TEXT("Progress field is retained"),Json->GetStringField(TEXT("checkpoint_id")),FString(TEXT("checkpoint_saved")));
    TestEqual(TEXT("Legacy weapon field agrees with primary"),Json->GetStringField(TEXT("weapon_id")),WeaponChange.PrimaryWeaponId.ToString());
    auto Legacy=MakeShared<FJsonObject>();Legacy->SetStringField(TEXT("weapon_id"),TEXT("shotgun"));
    TestTrue(TEXT("Old single weapon JSON is readable"),HeliosLoadout::ReadJson(Legacy,Restored));
    TestTrue(TEXT("Deleted old weapon JSON migrates to valid default pair"),Restored==Original);
    Legacy->SetStringField(TEXT("weapon_id"),TEXT("cfm_bulianshi_p90"));
    HeliosLoadout::ReadJson(Legacy,Restored);
    TestEqual(TEXT("Old P90 restores its original character"),Restored.CharacterId,FName(TEXT("cfm_bulianshi")));
    auto Invalid=Original;Invalid.SecondaryWeaponId=TEXT("cola");
    FString Error;
    TestFalse(TEXT("Reserved slots cannot be selected"),FHeliosLoadout::Validate(Invalid,Error));
    TestFalse(TEXT("Rejected operation explains the reserved slot"),Error.IsEmpty());
    TestTrue(TEXT("Save normalization leaves reserved slots empty"),FHeliosLoadout::Normalize(Invalid).SecondaryWeaponId.IsNone());
    Invalid=Original;Invalid.CharacterId=TEXT("missing_character");
    TestFalse(TEXT("New operations reject unknown characters"),FHeliosLoadout::Validate(Invalid,Error));
    Invalid=Original;Invalid.PrimaryWeaponId=TEXT("rifle");
    TestFalse(TEXT("New operations reject removed primary weapons"),FHeliosLoadout::Validate(Invalid,Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosCompanionLoadoutMigrationTest,"Helios.Loadout.CompanionMigration",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHeliosCompanionLoadoutMigrationTest::RunTest(const FString& Parameters)
{
    const FString Directory=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Automation/HeliosLoadout/SessionMigration"));
    const FString Token=FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString InputPath=FPaths::Combine(Directory,Token+TEXT(".input.json"));
    const FString SavePath=FPaths::Combine(Directory,Token+TEXT(".saved.json"));
    IFileManager::Get().MakeDirectory(*Directory,true);
    auto* Instance=NewObject<UGameInstance>();
    auto* Session=NewObject<UHeliosPetSession>(Instance);
    Session->SelectionSavePathOverride=SavePath;
    auto WriteInput=[&](const FString& Json){return FFileHelper::SaveStringToFile(Json,*InputPath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);};
    if(!TestTrue(TEXT("Isolated legacy fixture can be saved"),WriteInput(TEXT("{\"version\":2,\"count\":4,\"slots\":[0,1,6,7]}"))))return false;
    TestTrue(TEXT("Version two roster migrates"),Session->LoadSelection(InputPath));
    TestEqual(TEXT("Dog keeps its slot"),Session->GetLoadout(0).CharacterId,FName(TEXT("sword_dog")));
    TestTrue(TEXT("Retired butterfly becomes Chan and Zeus"),Session->GetLoadout(1)==FHeliosLoadout::MakeDefault());
    TestEqual(TEXT("Kui keeps its old numeric slot"),Session->GetLoadout(2).CharacterId,FName(TEXT("cfm_wangzhekui")));
    TestEqual(TEXT("Yun keeps its old numeric slot"),Session->GetLoadout(3).CharacterId,FName(TEXT("cfm_yun_summer")));
    const TArray<FHeliosLoadout> Migrated=Session->SelectedLoadouts;
    Session->Snapshots[2].bValid=true;Session->Snapshots[2].Health=37.f;Session->Snapshots[2].SleepRemaining=11.f;
    Session->SetCount(2);Session->SetCount(4);
    TestTrue(TEXT("Hidden companion loadouts survive count changes"),Session->SelectedLoadouts==Migrated);
    TestEqual(TEXT("Hidden companion health survives count changes"),Session->Snapshots[2].Health,37.f);
    TestEqual(TEXT("Hidden companion sleep survives count changes"),Session->Snapshots[2].SleepRemaining,11.f);
    TestTrue(TEXT("Companion character selection succeeds"),Session->SelectCharacterSlot(1,TEXT("cfm_sunshangxiang")));
    TestEqual(TEXT("Companion character selection keeps primary"),Session->GetLoadout(1).PrimaryWeaponId,FName(TEXT("cfm_chan_zeus")));
    TestTrue(TEXT("Companion primary selection succeeds"),Session->SelectWeaponSlot(1,TEXT("cfm_bulianshi_p90")));
    TestEqual(TEXT("Companion primary selection keeps character"),Session->GetLoadout(1).CharacterId,FName(TEXT("cfm_sunshangxiang")));
    Session->Snapshots[1].bValid=true;Session->Snapshots[1].Health=19.f;Session->Snapshots[1].SleepRemaining=7.f;
    auto Changed=Session->GetLoadout(1);Changed.CharacterId=TEXT("cfm_chan");
    TestTrue(TEXT("Full companion loadout selection succeeds"),Session->SelectLoadout(1,Changed));
    TestEqual(TEXT("Loadout selection does not heal a companion"),Session->Snapshots[1].Health,19.f);
    TestEqual(TEXT("Loadout selection does not wake a companion"),Session->Snapshots[1].SleepRemaining,7.f);
    TestTrue(TEXT("Humanoid slot can return to the dog"),Session->SelectCharacterSlot(1,TEXT("sword_dog")));
    TestTrue(TEXT("Dog cannot inherit a primary"),Session->GetLoadout(1).PrimaryWeaponId.IsNone());
    const TArray<FHeliosLoadout> BeforeInvalid=Session->SelectedLoadouts;
    const int32 BeforeCount=Session->GetCount();
    WriteInput(TEXT("{\"version\":2,\"count\":4,\"slots\":[0,1,6,8]}"));
    TestFalse(TEXT("Out of range legacy enum is rejected"),Session->LoadSelection(InputPath));
    TestTrue(TEXT("Invalid legacy enum does not partially replace slots"),Session->SelectedLoadouts==BeforeInvalid);
    TestEqual(TEXT("Invalid legacy enum preserves count"),Session->GetCount(),BeforeCount);
    TestEqual(TEXT("Invalid legacy enum preserves health"),Session->Snapshots[1].Health,19.f);
    WriteInput(TEXT("{\"version\":3,\"count\":4,\"loadouts\":[{\"character_id\":\"cfm_chan\",\"primary_weapon_id\":\"cfm_chan_zeus\"}]}"));
    TestFalse(TEXT("Truncated loadout roster is rejected"),Session->LoadSelection(InputPath));
    TestTrue(TEXT("Truncated loadout roster preserves all slots"),Session->SelectedLoadouts==BeforeInvalid);
    TestEqual(TEXT("Truncated loadout roster preserves sleep"),Session->Snapshots[1].SleepRemaining,7.f);
    IFileManager::Get().Delete(*InputPath);IFileManager::Get().Delete(*SavePath);IFileManager::Get().Delete(*(SavePath+TEXT(".tmp")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosCharacterFormsCompatibilityTest,"Helios.Loadout.CharacterForms",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHeliosCharacterFormsCompatibilityTest::RunTest(const FString& Parameters)
{
    FString Error;
    for(const auto* Character:FHeliosCharacterRegistry::GetAvailableCharacters(false))
    {
        for(const auto& Form:Character->Forms)
        {
            auto Loadout=FHeliosLoadout::MakeDefault();Loadout.CharacterId=Character->CharacterId;Loadout.CharacterVariantId=Form.VariantId;
            Loadout.PrimaryWeaponId=TEXT("cola");
            TestTrue(TEXT("Each registered form accepts an independently chosen primary"),FHeliosLoadout::Validate(Loadout,Error));
            auto Json=MakeShared<FJsonObject>();Json->SetNumberField(TEXT("version"),3);Json->SetStringField(TEXT("checkpoint_id"),TEXT("form_checkpoint"));
            HeliosLoadout::WriteJson(Json,Loadout);FHeliosLoadout Restored;
            TestTrue(TEXT("Existing save schema reads explicit character forms"),HeliosLoadout::ReadJson(Json,Restored));
            TestTrue(TEXT("Form and unrelated primary round trip together"),Restored==Loadout);
            TestEqual(TEXT("Loadout write keeps the enclosing save version"),Json->GetIntegerField(TEXT("version")),3);
            TestEqual(TEXT("Loadout write keeps existing progress"),Json->GetStringField(TEXT("checkpoint_id")),FString(TEXT("form_checkpoint")));
            TestTrue(TEXT("Explicit forms take priority over legacy context"),FHeliosLoadout::ResolveLegacyVariant(Loadout,nullptr,2)==Loadout);
        }
    }
    auto Loadout=FHeliosLoadout::MakeDefault();Loadout.CharacterId=TEXT("cfm_bulianshi");
    TestEqual(TEXT("Empty BLS form migrates to its previous SP appearance"),FHeliosLoadout::Normalize(Loadout).CharacterVariantId,FName(TEXT("SP")));
    Loadout.CharacterId=TEXT("cfm_wangzhekui");
    TestEqual(TEXT("Empty Kui form migrates to its previous GR appearance"),FHeliosLoadout::Normalize(Loadout).CharacterVariantId,FName(TEXT("GR")));
    Loadout.CharacterId=TEXT("cfm_yun_summer");
    TestTrue(TEXT("Pure normalization keeps old Yun context unresolved"),FHeliosLoadout::Normalize(Loadout).CharacterVariantId.IsNone());
    TestEqual(TEXT("Missing Yun context uses the established SP fallback"),FHeliosLoadout::ResolveLegacyVariant(Loadout,nullptr).CharacterVariantId,FName(TEXT("SP")));
    auto Legacy=MakeShared<FJsonObject>();Legacy->SetStringField(TEXT("character_id"),TEXT("cfm_bulianshi"));Legacy->SetStringField(TEXT("weapon_id"),TEXT("cfm_wangzhekui_thunder"));
    FHeliosLoadout Restored;HeliosLoadout::ReadJson(Legacy,Restored);
    TestEqual(TEXT("An empty explicit character form uses its own default rather than the weapon source"),Restored.CharacterVariantId,FName(TEXT("SP")));
    TestEqual(TEXT("Legacy independent primary is preserved"),Restored.PrimaryWeaponId,FName(TEXT("cfm_wangzhekui_thunder")));
    Loadout.CharacterVariantId=TEXT("invalid_form");
    TestFalse(TEXT("Live selection rejects unknown forms rather than silently changing appearance"),FHeliosLoadout::Validate(Loadout,Error));
    Loadout.CharacterId=TEXT("sword_dog");Loadout.CharacterVariantId=TEXT("GR");Loadout.PrimaryWeaponId=NAME_None;
    TestFalse(TEXT("Dog cannot receive a humanoid form"),FHeliosLoadout::Validate(Loadout,Error));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosRepeatedCharacterSelectionTest,"Helios.Loadout.RepeatedCharacterSelection",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHeliosRepeatedCharacterSelectionTest::RunTest(const FString& Parameters)
{
    const FString Path=FPaths::ProjectSavedDir()/TEXT("Automation/HeliosLoadout/SessionMigration")/(FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".repeat.json"));
    auto* Instance=NewObject<UGameInstance>();auto* Session=NewObject<UHeliosPetSession>(Instance);
    Session->SelectionSavePathOverride=Path;Session->SetCount(4);
    const FName Characters[]={TEXT("cfm_bulianshi"),TEXT("cfm_wangzhekui"),TEXT("cfm_yun_summer"),TEXT("cfm_yun_summer")};
    const FName Variants[]={TEXT("BL"),TEXT("BL"),TEXT("GR"),TEXT("BL")};
    for(int32 I=0;I<4;++I)
    {
        auto Loadout=FHeliosLoadout::MakeDefault();Loadout.CharacterId=Characters[I];Loadout.CharacterVariantId=Variants[I];Loadout.PrimaryWeaponId=TEXT("cola");
        TestTrue(TEXT("Repeated-selection fixture accepts a nondefault explicit form"),Session->SelectLoadout(I,Loadout));
        Session->Snapshots[I].bValid=true;Session->Snapshots[I].Health=17.f;Session->Snapshots[I].SleepRemaining=9.f;
        TestTrue(TEXT("Selecting the same character without a variant succeeds"),Session->SelectCharacterSlot(I,Characters[I]));
        TestTrue(TEXT("Same-character selection retains its explicit form and primary"),Session->GetLoadout(I)==Loadout);
        TestTrue(TEXT("An independently selected primary is accepted after repeated character selection"),Session->SelectWeaponSlot(I,TEXT("cfm_chan_zeus")));
        TestEqual(TEXT("Primary selection preserves the previously selected form"),Session->GetLoadout(I).CharacterVariantId,Variants[I]);
        const auto* Definition=FHeliosCharacterRegistry::FindCharacter(Characters[I]);
        TestTrue(TEXT("An explicit variant still changes the same character"),Definition&&Session->SelectCharacterSlot(I,Characters[I],Definition->DefaultVariantId));
        TestEqual(TEXT("Explicit form selection has priority over the retained form"),Session->GetLoadout(I).CharacterVariantId,Definition?Definition->DefaultVariantId:NAME_None);
        TestEqual(TEXT("Repeated character and form selection does not heal saved companions"),Session->Snapshots[I].Health,17.f);
        TestEqual(TEXT("Repeated character and form selection does not wake saved companions"),Session->Snapshots[I].SleepRemaining,9.f);
    }
    TestTrue(TEXT("A different character can be selected without carrying an incompatible old form"),Session->SelectCharacterSlot(0,TEXT("cfm_wangzhekui")));
    TestEqual(TEXT("A different character uses its own established default form"),Session->GetLoadout(0).CharacterVariantId,FName(TEXT("GR")));
    TestEqual(TEXT("A different character still retains its independent primary"),Session->GetLoadout(0).PrimaryWeaponId,FName(TEXT("cfm_chan_zeus")));
    IFileManager::Get().Delete(*Path);IFileManager::Get().Delete(*(Path+TEXT(".tmp")));
    return true;
}
#endif
