#include "QA/HeliosLoadoutFlowQA.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "Player/HeliosArenaCharacter.h"
#include "UI/HeliosAdventureHUD.h"
#include "Control/HeliosControlSubsystem.h"
#include "Pets/HeliosPetDog.h"
#include "Town/HeliosTownSubsystem.h"
#include "World/HeliosMapDefinition.h"
#include "World/HeliosMapTravelSubsystem.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Weapons/HeliosYunSummerMegatron.h"
#include "Weapons/HeliosMechWeapon.h"
#include "Weapons/HeliosFighterWeapon.h"
#include "Enemies/HeliosEnemyActor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "UObject/UnrealType.h"
#include "QA/HeliosCompanionFixQA.inl"

namespace
{
struct FFlowState
{
    bool bStarted=false,bFinished=false;
    int32 Stage=0,Failures=0,IkunMemeCount=0,GhostMemeCount=0;
    double StartedAt=0;
    FString Out,Map,IkunMeme,GhostMeme,Readiness;
    FHeliosLoadout Expected;
    FHeliosLoadout VehicleExpected;
    FTransform VehicleOriginalTransform;
    int32 VehicleCase=0;
    float VehicleMovementMultiplier=1.f;
    TArray<FHeliosLoadout> Roster;
    TArray<FString> IkunExplored,GhostExplored;
    TArray<TSharedPtr<FJsonValue>> Checks;
};
FFlowState& Flow(){static FFlowState S;return S;}
TSharedPtr<FJsonObject> ParseJson(const FString& Text)
{
    TSharedPtr<FJsonObject> J;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),J);return J;
}
bool ResultOK(const FString& Text)
{const auto J=ParseJson(Text);bool OK=false;return J&&J->TryGetBoolField(TEXT("ok"),OK)&&OK;}
bool HasString(const TSharedPtr<FJsonObject>& J,const TCHAR* Field,const FString& Wanted)
{
    const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(!J||!J->TryGetArrayField(Field,A))return false;
    for(const auto& V:*A){FString Value;if(V->TryGetString(Value)&&Value==Wanted)return true;}return false;
}
TArray<FString> ReadStrings(const TSharedPtr<FJsonObject>& J,const TCHAR* Field)
{
    TArray<FString> Result;const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
    if(J&&J->TryGetArrayField(Field,Values))for(const auto& V:*Values){FString Item;if(V->TryGetString(Item))Result.Add(Item);}return Result;
}
FString FirstMeme(UHeliosTownSubsystem* Town)
{
    const auto J=ParseJson(Town->GetStateJson());const TArray<TSharedPtr<FJsonValue>>* A=nullptr;
    if(!J||!J->TryGetArrayField(TEXT("memes"),A)||A->IsEmpty())return FString();
    for(const auto& V:*A)
    {const auto Item=V->AsObject();FString Id;bool Discovered=false;if(Item&&Item->TryGetStringField(TEXT("id"),Id)&&Item->TryGetBoolField(TEXT("discovered"),Discovered)&&!Discovered)return Id;}
    return FString();
}
TSharedPtr<FJsonObject> ReadTownSave(FName MapId)
{
    FString Text;FFileHelper::LoadFileToString(Text,*(FPaths::ProjectSavedDir()/TEXT("Automation/HeliosLoadout")/(MapId.ToString()+TEXT(".town.json"))));
    return ParseJson(Text);
}
bool SaveJson(const TSharedRef<FJsonObject>& Json,const FString& Path)
{
    FString Text;return FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Text))&&
        FFileHelper::SaveStringToFile(Text,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
}

AHeliosLoadoutFlowQA::AHeliosLoadoutFlowQA()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.bTickEvenWhenPaused=true;
    // Paused menu frames must keep polling; stages already use a real-time clock.
    PrimaryActorTick.TickInterval=0.f;
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosCompanionFixQA")))PrimaryActorTick.TickGroup=TG_PostUpdateWork;
}
void AHeliosLoadoutFlowQA::BeginPlay()
{
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosCompanionFixQA"))){Super::BeginPlay();return;}
    Super::BeginPlay();auto& S=Flow();StageStarted=FPlatformTime::Seconds();
    if(const auto* Map=FHeliosMapRegistry::ForWorld(GetWorld()))S.Map=Map->Id.ToString();
    if(!S.bStarted)
    {
        S.bStarted=true;S.StartedAt=StageStarted;S.Out=FPaths::ProjectSavedDir()/TEXT("LoadoutFlowQA");
        FParse::Value(FCommandLine::Get(),TEXT("HeliosLoadoutOut="),S.Out);IFileManager::Get().MakeDirectory(*S.Out,true);
        Check(TEXT("starts_in_adventure"),S.Map==TEXT("HeliosAdventure"));
        if(S.Map!=TEXT("HeliosAdventure")){Finish();return;}
    }
    WriteReport();
}
void AHeliosLoadoutFlowQA::Check(const FString& Name,bool Pass)
{
    auto& S=Flow();auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("name"),Name);J->SetStringField(TEXT("map"),S.Map);
    J->SetNumberField(TEXT("stage"),S.Stage);J->SetBoolField(TEXT("passed"),Pass);S.Checks.Add(MakeShared<FJsonValueObject>(J));if(!Pass)++S.Failures;
    UE_LOG(LogTemp,Display,TEXT("LOADOUT_FLOW_QA stage=%d map=%s %s %s"),S.Stage,*S.Map,*Name,Pass?TEXT("PASS"):TEXT("FAIL"));WriteReport();
}
void AHeliosLoadoutFlowQA::WriteReport()const
{
    const auto& S=Flow();if(S.Out.IsEmpty())return;auto J=MakeShared<FJsonObject>();
    J->SetStringField(TEXT("status"),S.bFinished?TEXT("complete"):TEXT("running"));J->SetBoolField(TEXT("passed"),S.bFinished&&S.Failures==0);
    J->SetNumberField(TEXT("failures"),S.Failures);J->SetNumberField(TEXT("stage"),S.Stage);J->SetStringField(TEXT("map"),S.Map);
    J->SetArrayField(TEXT("checks"),S.Checks);J->SetBoolField(TEXT("isolated_saves"),true);J->SetStringField(TEXT("last_readiness"),S.Readiness);
    auto L=MakeShared<FJsonObject>();HeliosLoadout::WriteJson(L,S.Expected);J->SetObjectField(TEXT("expected_loadout"),L);
    FString Text;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(S.Out/TEXT("flow.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
void AHeliosLoadoutFlowQA::Finish()
{
    auto& S=Flow();if(S.bFinished)return;S.bFinished=true;WriteReport();
    UE_LOG(LogTemp,Display,TEXT("LOADOUT_FLOW_QA_COMPLETE failures=%d checks=%d"),S.Failures,S.Checks.Num());FPlatformMisc::RequestExitWithStatus(false,S.Failures?1:0);
}
void AHeliosLoadoutFlowQA::Tick(float Dt)
{
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosCompanionFixQA"))){Super::Tick(Dt);TickCompanionFixQA(this,Dt);return;}
    Super::Tick(Dt);auto& S=Flow();if(S.bFinished)return;
    const double Now=FPlatformTime::Seconds();if(LastStage!=S.Stage){LastStage=S.Stage;StageStarted=Now;StageGameStarted=GetWorld()->GetTimeSeconds();}
    auto* D=AHeliosAdventureDirector::Find(GetWorld());auto* P=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));
    auto* PC=UGameplayStatics::GetPlayerController(this,0);auto* HUD=PC?Cast<AHeliosAdventureHUD>(PC->GetHUD()):nullptr;
    auto* Town=GetWorld()->GetSubsystem<UHeliosTownSubsystem>();auto* Travel=GetGameInstance()->GetSubsystem<UHeliosMapTravelSubsystem>();
    auto* Session=GetGameInstance()->GetSubsystem<UHeliosPetSession>();
    if(Town&&Town->IsTownWorld())S.Readiness=Town->GetStateJson();
    if(Now-S.StartedAt>900.||Now-StageStarted>240.){Check(TEXT("flow_readiness_timeout"),false);Finish();return;}
    if(!D||!P||!HUD||!Travel||!Session)return;
    // This fixture checks state transitions; arbitrary hostile attacks cannot change its result.
    for(TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It)It->Destroy();
    P->SetCanBeDamaged(false);
    const double Age=Now-StageStarted;
    if(S.Stage==0)
    {
        if(Age<2.||HUD->GetMenuState()!=EHeliosAdventureMenu::WeaponSelect)return;
        // Exercise the live legacy context, restoring memory before any roster/UI changes.
        auto* YunSession=GetGameInstance()->GetSubsystem<UHeliosYunSummerSession>();
        const FString YunBefore=S.Out/TEXT("yun_companion_context_before.json"),PetBefore=S.Out/TEXT("companions_before_context.json");
        const auto OriginalRoster=Session->SelectedLoadouts;const auto OriginalKinds=Session->SelectedKinds;const auto OriginalSnapshots=Session->Snapshots;
        const auto OriginalKind=Session->SelectedKind,OriginalSnapshotKind=Session->SnapshotKind;
        const auto OriginalPlayerForm=YunSession?YunSession->GetPlayerForm():EHeliosYunSummerForm::SP;
        TArray<EHeliosYunSummerForm> OriginalPetForms;for(int32 I=0;I<4;++I)OriginalPetForms.Add(YunSession?YunSession->GetPetForm(I):EHeliosYunSummerForm::SP);
        const int32 OriginalCount=Session->GetCount();const bool OriginalHasSnapshot=Session->bHasSnapshot;
        const float OriginalHealth=Session->Health,OriginalSleep=Session->SleepRemaining;
        const bool SnapshotReady=YunSession&&YunSession->SaveSelection(YunBefore)&&Session->SaveSelection(PetBefore);
        auto LegacyContext=MakeShared<FJsonObject>();LegacyContext->SetNumberField(TEXT("version"),1);
        LegacyContext->SetNumberField(TEXT("player"),uint8(YunSession?YunSession->GetPlayerForm():EHeliosYunSummerForm::SP));
        const EHeliosYunSummerForm OldForms[]={EHeliosYunSummerForm::BL,EHeliosYunSummerForm::GR,EHeliosYunSummerForm::SP,EHeliosYunSummerForm::BL};
        const FName OldVariants[]={TEXT("BL"),TEXT("GR"),TEXT("SP"),TEXT("BL")};
        const FName ExplicitVariants[]={TEXT("GR"),TEXT("SP"),TEXT("BL"),TEXT("SP")};
        const FName ExplicitPrimaries[]={TEXT("cfm_chan_zeus"),TEXT("cola"),TEXT("cfm_bulianshi_p90"),TEXT("cfm_wangzhekui_thunder")};
        TArray<TSharedPtr<FJsonValue>> OldPetForms,OldSlots,ExplicitLoadouts;
        for(int32 I=0;I<4;++I)
        {
            OldPetForms.Add(MakeShared<FJsonValueNumber>(uint8(OldForms[I])));OldSlots.Add(MakeShared<FJsonValueNumber>(7));
            auto L=FHeliosLoadout::FromLegacyWeapon(TEXT("cfm_yun_summer_megatron"));L.CharacterVariantId=ExplicitVariants[I];L.PrimaryWeaponId=ExplicitPrimaries[I];
            auto Item=MakeShared<FJsonObject>();HeliosLoadout::WriteJson(Item,L);ExplicitLoadouts.Add(MakeShared<FJsonValueObject>(Item));
        }
        LegacyContext->SetArrayField(TEXT("pets"),OldPetForms);
        auto LegacyPets=MakeShared<FJsonObject>();LegacyPets->SetNumberField(TEXT("version"),2);LegacyPets->SetNumberField(TEXT("count"),4);LegacyPets->SetArrayField(TEXT("slots"),OldSlots);
        auto ExplicitPets=MakeShared<FJsonObject>();ExplicitPets->SetNumberField(TEXT("version"),3);ExplicitPets->SetNumberField(TEXT("count"),4);ExplicitPets->SetArrayField(TEXT("slots"),OldSlots);ExplicitPets->SetArrayField(TEXT("loadouts"),ExplicitLoadouts);
        const FString ContextFixture=S.Out/TEXT("yun_companion_context_fixture.json"),LegacyFixture=S.Out/TEXT("companions_v2_yun_fixture.json"),ExplicitFixture=S.Out/TEXT("companions_v3_yun_fixture.json");
        const bool FixturesReady=SnapshotReady&&SaveJson(LegacyContext,ContextFixture)&&SaveJson(LegacyPets,LegacyFixture)&&SaveJson(ExplicitPets,ExplicitFixture)&&YunSession->LoadSelection(ContextFixture);
        Check(TEXT("companion_legacy_yun_fixture_uses_isolated_paths"),FixturesReady);
        const bool LegacyLoaded=FixturesReady&&Session->LoadSelection(LegacyFixture);
        Check(TEXT("companion_v2_yun_four_slots_load"),LegacyLoaded&&Session->GetCount()==4);
        for(int32 I=0;I<4;++I)
        {
            auto Expected=FHeliosLoadout::FromLegacyWeapon(TEXT("cfm_yun_summer_megatron"));Expected.CharacterVariantId=OldVariants[I];
            Check(FString::Printf(TEXT("companion_v2_yun_slot_%d_uses_own_context"),I),LegacyLoaded&&Session->GetLoadout(I)==Expected&&Session->GetKind(I)==EHeliosPetKind::YunSummerMegatron);
        }
        const bool ExplicitLoaded=FixturesReady&&Session->LoadSelection(ExplicitFixture);
        Check(TEXT("companion_v3_yun_four_slots_load"),ExplicitLoaded&&Session->GetCount()==4);
        for(int32 I=0;I<4;++I)
        {
            auto Expected=FHeliosLoadout::FromLegacyWeapon(TEXT("cfm_yun_summer_megatron"));Expected.CharacterVariantId=ExplicitVariants[I];Expected.PrimaryWeaponId=ExplicitPrimaries[I];
            Check(FString::Printf(TEXT("companion_v3_yun_slot_%d_explicit_variant_wins"),I),ExplicitLoaded&&Session->GetLoadout(I)==Expected&&YunSession->GetPetForm(I)==OldForms[I]);
        }
        // LoadSelection is memory-only; restore the runtime health/sleep snapshots it resets.
        const bool YunRestored=SnapshotReady&&YunSession->LoadSelection(YunBefore);
        const bool PetsRestored=SnapshotReady&&Session->LoadSelection(PetBefore);
        Session->Snapshots=OriginalSnapshots;Session->SnapshotKind=OriginalSnapshotKind;Session->bHasSnapshot=OriginalHasSnapshot;Session->Health=OriginalHealth;Session->SleepRemaining=OriginalSleep;
        bool AllFormsRestored=YunRestored&&YunSession->GetPlayerForm()==OriginalPlayerForm;
        for(int32 I=0;I<4;++I)AllFormsRestored=AllFormsRestored&&YunSession->GetPetForm(I)==OriginalPetForms[I];
        Check(TEXT("companion_legacy_yun_context_restored_in_memory"),AllFormsRestored&&PetsRestored&&Session->GetCount()==OriginalCount&&Session->SelectedLoadouts==OriginalRoster&&Session->SelectedKinds==OriginalKinds&&Session->SelectedKind==OriginalKind);
        const auto Before=HUD->GetPendingLoadout();HUD->SelectCharacter(TEXT("cfm_sunshangxiang"));
        Check(TEXT("hud_character_keeps_primary"),HUD->GetPendingLoadout().CharacterId==TEXT("cfm_sunshangxiang")&&HUD->GetPendingLoadout().PrimaryWeaponId==Before.PrimaryWeaponId);
        HUD->SelectWeapon(TEXT("cfm_bulianshi_p90"));S.Expected=HUD->GetPendingLoadout();
        Check(TEXT("hud_primary_keeps_character"),S.Expected.CharacterId==TEXT("cfm_sunshangxiang")&&S.Expected.PrimaryWeaponId==TEXT("cfm_bulianshi_p90"));
        Check(TEXT("menu_pending_is_not_runtime_snapshot"),!D->CaptureActiveLoadout()&&HUD->GetPendingLoadout()==S.Expected);
        HUD->SelectPetCount(4);const TCHAR* Characters[]={TEXT("cfm_sunshangxiang"),TEXT("cfm_zhongxiaokui"),TEXT("cfm_chan"),TEXT("cfm_wangzhekui")};
        const TCHAR* Primaries[]={TEXT("cfm_chan_zeus"),TEXT("cola"),TEXT("cfm_bulianshi_p90"),TEXT("cfm_wangzhekui_thunder")};
        for(int32 I=0;I<4;++I)
        {
            HUD->SelectPetSlot(I);HUD->SelectPetCharacter(Characters[I]);HUD->SelectPetWeapon(Primaries[I]);S.Roster.Add(Session->GetLoadout(I));
            Check(FString::Printf(TEXT("roster_slot_%d_independent"),I),S.Roster[I].CharacterId==Characters[I]&&S.Roster[I].PrimaryWeaponId==Primaries[I]);
        }
        Check(TEXT("dog_can_be_selected"),Session->SelectCharacterSlot(0,TEXT("sword_dog")));
        Check(TEXT("dog_has_fixed_empty_primary"),Session->GetLoadout(0).PrimaryWeaponId.IsNone()&&!Session->SelectWeaponSlot(0,TEXT("cola")));
        Check(TEXT("dog_can_return_to_human"),Session->SelectLoadout(0,S.Roster[0]));
        for(const TCHAR* Id:{TEXT("rifle"),TEXT("rocket"),TEXT("laser"),TEXT("shotgun")})
        {HUD->SelectWeapon(Id);Check(FString(TEXT("hud_rejects_"))+Id,HUD->GetPendingLoadout()==S.Expected&&!UHeliosWeaponRegistry::FindWeapon(Id));}
        HUD->StartSelectedRun();Check(TEXT("hud_starts_selected_pair"),!D->IsSelectingWeapon()&&P->GetLoadout()==S.Expected);
        if(D->IsSelectingWeapon()){Finish();return;}S.Stage=1;return;
    }
    if(S.Stage==1)
    {
        if(Age<3.)return;
        const auto* Pets=GetWorld()->GetSubsystem<UHeliosPetWorld>();Check(TEXT("four_runtime_companions"),Pets&&Pets->Pets.Num()==4);
        if(Pets)for(int32 I=0;I<4;++I)Check(FString::Printf(TEXT("runtime_roster_slot_%d"),I),Pets->Pets.IsValidIndex(I)&&IsValid(Pets->Pets[I])&&Pets->Pets[I]->Loadout==S.Roster[I]);
        Check(TEXT("old_player_equip_succeeds"),P->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("cola"))));
        Check(TEXT("old_player_equip_keeps_character"),P->GetLoadout().CharacterId==S.Expected.CharacterId&&P->GetLoadout().CharacterVariantId==S.Expected.CharacterVariantId);
        D->EnterWeaponSelection();Check(TEXT("old_start_run_succeeds"),D->StartRun(TEXT("cfm_chan_zeus")));
        Check(TEXT("old_start_run_keeps_character"),P->GetLoadout().CharacterId==S.Expected.CharacterId&&P->GetLoadout().CharacterVariantId==S.Expected.CharacterVariantId);
        auto* Control=GetWorld()->GetSubsystem<UHeliosControlSubsystem>();
        D->EnterWeaponSelection();
        if(Control)
        {
            for(const TCHAR* Id:{TEXT("rifle"),TEXT("rocket"),TEXT("laser"),TEXT("shotgun")})
            {
                const FString Json=FString::Printf(TEXT("{\"weapon_id\":\"%s\"}"),Id);
                Check(FString(TEXT("control_rejects_"))+Id,!ResultOK(Control->DispatchLocalCommand(TEXT("start_adventure"),UHeliosControlSubsystem::GetMainPlayerActorId(),Json)));
                Check(FString(TEXT("director_rejects_"))+Id,!D->StartRun(Id));
                auto Invalid=S.Expected;Invalid.PrimaryWeaponId=Id;const auto PendingBefore=Travel->PendingLoadout;
                Check(FString(TEXT("travel_rejects_"))+Id,!Travel->RequestTravelWithLoadout(TEXT("ikun_town"),Invalid)&&Travel->PendingLoadout==PendingBefore);
            }
            Check(TEXT("old_control_weapon_id_succeeds"),ResultOK(Control->DispatchLocalCommand(TEXT("start_adventure"),UHeliosControlSubsystem::GetMainPlayerActorId(),TEXT("{\"weapon_id\":\"cfm_bulianshi_p90\"}"))));
            Check(TEXT("old_control_weapon_id_keeps_character"),P->GetLoadout().CharacterId==S.Expected.CharacterId&&P->GetLoadout().CharacterVariantId==S.Expected.CharacterVariantId);
            D->EnterWeaponSelection();
            Check(TEXT("new_control_primary_id_succeeds"),ResultOK(Control->DispatchLocalCommand(TEXT("start_adventure"),UHeliosControlSubsystem::GetMainPlayerActorId(),TEXT("{\"primary_weapon_id\":\"cfm_chan_zeus\"}"))));
            Check(TEXT("new_control_primary_id_keeps_character"),P->GetLoadout().CharacterId==S.Expected.CharacterId);
        }
        else Check(TEXT("control_exists"),false);
        S.Expected.PrimaryWeaponId=TEXT("cfm_chan_zeus");Check(TEXT("selected_pair_after_control"),P->GetLoadout()==S.Expected&&D->GetSelectedLoadout()==S.Expected);
        auto Hot=S.Expected;Hot.CharacterId=TEXT("cfm_bulianshi");Hot.CharacterVariantId=TEXT("BL");Hot.PrimaryWeaponId=TEXT("cola");
        Check(TEXT("runtime_hot_loadout_equips"),P->EquipLoadout(Hot));S.Expected=Hot;
        Check(TEXT("runtime_hot_loadout_updates_director"),P->GetLoadout()==S.Expected&&D->GetSelectedLoadout()==S.Expected);
        S.VehicleOriginalTransform=P->GetActorTransform();S.VehicleCase=0;S.Stage=11;return;
    }
    if(S.Stage>=11&&S.Stage<=15)
    {
        const FString Prefix=S.VehicleCase==0?TEXT("vehicle_yun_SP"):TEXT("vehicle_bls_BL");
        if(S.Stage==11)
        {
            if(Age<.2)return;
            S.VehicleExpected=S.Expected;S.VehicleExpected.CharacterId=S.VehicleCase==0?TEXT("cfm_yun_summer"):TEXT("cfm_bulianshi");
            S.VehicleExpected.CharacterVariantId=S.VehicleCase==0?TEXT("SP"):TEXT("BL");
            S.VehicleMovementMultiplier=S.VehicleCase==0?HeliosYunSummer::Setting(false,TEXT("灵魂武者移速倍率"),1.2f,1,3):1.f;
            P->ClearMovementInput();P->GetCharacterMovement()->StopMovementImmediately();
            Check(Prefix+TEXT("_handheld_fixture_equips"),P->EquipLoadout(S.VehicleExpected)&&P->GetLoadout()==S.VehicleExpected);
            Check(Prefix+TEXT("_handheld_movement"),FMath::IsNearlyEqual(P->GetEquipmentMovementMultiplier(),S.VehicleMovementMultiplier,.001f));
            FVector Feet;Check(Prefix+TEXT("_mech_deployment_space"),P->FindMechDeployment(Feet));
            Check(Prefix+TEXT("_old_mech_equip_succeeds"),P->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("mech"))));
            S.Stage=12;return;
        }
        if(S.Stage==12)
        {
            if(Age<1.)return;auto Vehicle=S.VehicleExpected;Vehicle.PrimaryWeaponId=TEXT("mech");
            Check(Prefix+TEXT("_mech_really_active"),Cast<AHeliosMechWeapon>(P->GetEquippedWeapon())&&P->IsMechMode()&&!P->IsFighterMode());
            Check(Prefix+TEXT("_mech_preserves_full_form"),P->GetLoadout()==Vehicle&&D->GetSelectedLoadout()==Vehicle);
            Check(Prefix+TEXT("_mech_movement_is_one"),FMath::IsNearlyEqual(P->GetEquipmentMovementMultiplier(),1.f,.001f));
            Check(Prefix+TEXT("_old_mech_return_succeeds"),P->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(S.VehicleExpected.PrimaryWeaponId)));
            S.Stage=13;return;
        }
        if(S.Stage==13)
        {
            if(Age<.5)return;
            Check(Prefix+TEXT("_mech_return_preserves_full_form"),P->GetLoadout()==S.VehicleExpected&&D->GetSelectedLoadout()==S.VehicleExpected&&!P->IsMechMode()&&!P->IsFighterMode());
            Check(Prefix+TEXT("_mech_return_movement"),FMath::IsNearlyEqual(P->GetEquipmentMovementMultiplier(),S.VehicleMovementMultiplier,.001f));
            Check(Prefix+TEXT("_old_fighter_equip_succeeds"),P->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("fighter"))));
            S.Stage=14;return;
        }
        if(S.Stage==14)
        {
            if(Age<1.)return;auto Vehicle=S.VehicleExpected;Vehicle.PrimaryWeaponId=TEXT("fighter");
            const auto* Fighter=Cast<AHeliosFighterWeapon>(P->GetEquippedWeapon());
            Check(Prefix+TEXT("_fighter_really_active"),Fighter&&Fighter->bActive&&P->IsFighterMode()&&!P->IsMechMode()&&P->GetCharacterMovement()->MovementMode==MOVE_Flying);
            Check(Prefix+TEXT("_fighter_preserves_full_form"),P->GetLoadout()==Vehicle&&D->GetSelectedLoadout()==Vehicle);
            Check(Prefix+TEXT("_fighter_movement_is_one"),FMath::IsNearlyEqual(P->GetEquipmentMovementMultiplier(),1.f,.001f));
            Check(Prefix+TEXT("_old_fighter_return_succeeds"),P->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(S.VehicleExpected.PrimaryWeaponId)));
            S.Stage=15;return;
        }
        if(Age<.5)return;
        Check(Prefix+TEXT("_fighter_return_preserves_full_form"),P->GetLoadout()==S.VehicleExpected&&D->GetSelectedLoadout()==S.VehicleExpected&&!P->IsMechMode()&&!P->IsFighterMode());
        Check(Prefix+TEXT("_fighter_return_movement"),FMath::IsNearlyEqual(P->GetEquipmentMovementMultiplier(),S.VehicleMovementMultiplier,.001f));
        P->ClearMovementInput();P->GetCharacterMovement()->StopMovementImmediately();P->SetActorTransform(S.VehicleOriginalTransform,false,nullptr,ETeleportType::TeleportPhysics);
        if(++S.VehicleCase<2){S.Stage=11;return;}
        Check(TEXT("vehicle_flow_restores_hot_selected_loadout"),P->EquipLoadout(S.Expected)&&P->GetLoadout()==S.Expected&&D->GetSelectedLoadout()==S.Expected);
        S.Stage=2;return;
    }
    if(S.Stage==2)
    {
        const auto* Protection=FindFProperty<FFloatProperty>(P->GetClass(),TEXT("SpawnProtectionSeconds"));
        const float Grace=Protection?Protection->GetPropertyValue_InContainer(P):3.f;
        if(GetWorld()->GetTimeSeconds()-StageGameStarted<Grace+1.)return;
        P->SetCanBeDamaged(true);P->TakeDamage(P->GetMaxHealth()+100000.f,FDamageEvent(),nullptr,this);
        Check(TEXT("actual_death_ends_run"),P->IsDead()&&D->IsRunOver());if(!P->IsDead()){Finish();return;}S.Stage=3;return;
    }
    if(S.Stage==3)
    {
        if(Age<.5||HUD->GetMenuState()!=EHeliosAdventureMenu::Death)return;
        HUD->RequestRestartConfirmation();HUD->ConfirmRestart();Check(TEXT("death_restart_returns_to_selection"),HUD->GetMenuState()==EHeliosAdventureMenu::WeaponSelect&&HUD->GetPendingLoadout()==S.Expected);
        HUD->StartSelectedRun();Check(TEXT("death_restart_restores_same_pair"),!P->IsDead()&&!D->IsSelectingWeapon()&&P->GetLoadout()==S.Expected);S.Stage=4;return;
    }
    if(S.Stage==4)
    {
        if(Age<1.)return;S.Stage=5;const bool Accepted=Travel->RequestTravel(TEXT("ikun_town"),S.Expected.PrimaryWeaponId);
        Check(TEXT("old_travel_to_ikun_accepted"),Accepted);Check(TEXT("old_travel_preserves_full_pending_pair"),Travel->PendingLoadout==S.Expected);
        if(!Accepted)Finish();return;
    }
    if(S.Stage==5||S.Stage==7||S.Stage==8||S.Stage==9)
    {
        if(!Town||!Town->IsTownWorld()||!Town->IsWorldReady()||Travel->bPendingStart||D->IsSelectingWeapon())return;
        auto* RuntimeRoster=GetWorld()->GetSubsystem<UHeliosPetWorld>();
        auto* Chan=RuntimeRoster&&RuntimeRoster->Pets.IsValidIndex(2)?RuntimeRoster->Pets[2].Get():nullptr;
        if(!FixVisible(Chan))return;
        Check(TEXT("arrival_keeps_player_pair"),P->GetLoadout()==S.Expected);Check(TEXT("arrival_keeps_saved_pair"),Town->GetSavedLoadout()==S.Expected);
        Check(TEXT("arrival_keeps_four_roster_slots"),Session->GetCount()==4&&Session->SelectedLoadouts==S.Roster);
        Check(TEXT("arrival_chan_has_visible_model"),FixVisible(Chan)&&Chan->Loadout.CharacterId==TEXT("cfm_chan"));
        if(S.Stage==5)
        {
            Check(TEXT("arrived_at_ikun"),S.Map==TEXT("ikun_town"));Check(TEXT("reset_isolated_ikun_progress"),Town->ResetProgress());D->RestartRun();S.Stage=6;return;
        }
        if(S.Stage==7)
        {
            Check(TEXT("arrived_at_ghost"),S.Map==TEXT("ikun_ghost_village"));Town->ResetProgress();D->RestartRun();S.Stage=10;return;
        }
        const FString Meme=S.Stage==8?S.IkunMeme:S.GhostMeme;const int32 Minimum=S.Stage==8?S.IkunMemeCount:S.GhostMemeCount;
        const auto State=ParseJson(Town->GetStateJson());Check(TEXT("reload_restores_discovered_progress"),HasString(State,TEXT("discovered_memes"),Meme)&&Town->GetMemeCount()>=Minimum);
        bool ExploredKept=true;for(const auto& Zone:S.Stage==8?S.IkunExplored:S.GhostExplored)ExploredKept&=HasString(State,TEXT("explored_zones"),Zone);
        Check(TEXT("reload_restores_explored_districts"),ExploredKept);
        const auto Save=ReadTownSave(FName(*S.Map));FHeliosLoadout Saved;
        Check(TEXT("reload_keeps_disk_loadout"),HeliosLoadout::ReadJson(Save,Saved)&&Saved==S.Expected);
        if(S.Stage==8)
        {
            S.Stage=9;const bool Accepted=Travel->RequestTravelWithLoadout(TEXT("ikun_ghost_village"),S.Expected);Check(TEXT("return_to_ghost_accepted"),Accepted);if(!Accepted)Finish();return;
        }
        Finish();return;
    }
    if(S.Stage==6||S.Stage==10)
    {
        if(!Town||!Town->IsWorldReady()||D->IsSelectingWeapon())return;
        const bool Ikun=S.Stage==6;FString& Meme=Ikun?S.IkunMeme:S.GhostMeme;Meme=FirstMeme(Town);
        if(Ikun)
        {
            auto* YunSession=GetGameInstance()->GetSubsystem<UHeliosYunSummerSession>();
            const auto OriginalForm=YunSession?YunSession->GetPlayerForm():EHeliosYunSummerForm::SP;
            const FString LegacySnapshot=S.Out/TEXT("yun_legacy_before.json"),LegacyFixture=S.Out/TEXT("yun_legacy_fixture.json");
            const bool SnapshotReady=YunSession&&YunSession->SaveSelection(LegacySnapshot);
            auto Fixture=MakeShared<FJsonObject>();Fixture->SetNumberField(TEXT("version"),1);Fixture->SetNumberField(TEXT("player"),uint8(EHeliosYunSummerForm::BL));
            TArray<TSharedPtr<FJsonValue>> PetForms;
            for(int32 I=0;I<4;++I)PetForms.Add(MakeShared<FJsonValueNumber>(uint8(YunSession?YunSession->GetPetForm(I):EHeliosYunSummerForm::SP)));
            Fixture->SetArrayField(TEXT("pets"),PetForms);FString FixtureText;FJsonSerializer::Serialize(Fixture,TJsonWriterFactory<>::Create(&FixtureText));
            const bool FixtureReady=SnapshotReady&&FFileHelper::SaveStringToFile(FixtureText,*LegacyFixture,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)&&YunSession->LoadSelection(LegacyFixture);
            Check(TEXT("town_legacy_yun_fixture_uses_isolated_paths"),FixtureReady);
            auto LegacyYun=FHeliosLoadout::FromLegacyWeapon(TEXT("cfm_yun_summer_megatron"));
            Check(TEXT("town_empty_yun_variant_reads_legacy_context"),FixtureReady&&P->EquipLoadout(LegacyYun)&&P->GetLoadout().CharacterVariantId==TEXT("BL"));
            auto Yun=FHeliosLoadout::FromLegacyWeapon(TEXT("cfm_yun_summer_megatron"));Yun.CharacterVariantId=TEXT("GR");
            Check(TEXT("town_yun_form_equips"),P->EquipLoadout(Yun)&&P->GetLoadout()==Yun);
            Check(TEXT("town_explicit_yun_variant_overrides_legacy_context"),P->GetLoadout().CharacterVariantId==TEXT("GR")&&YunSession&&YunSession->GetPlayerForm()==EHeliosYunSummerForm::BL);
            P->ToggleCharacterForm();const auto Runtime=P->GetLoadout();
            Check(TEXT("town_yun_h_changes_runtime_form"),Runtime.CharacterId==Yun.CharacterId&&Runtime.CharacterVariantId==TEXT("BL"));
            Check(TEXT("town_yun_h_updates_director"),D->GetSelectedLoadout()==Runtime);
            Check(TEXT("town_save_captures_runtime_form"),Town->SaveProgress()&&Town->GetSavedLoadout()==Runtime);
            FHeliosLoadout Disk;Check(TEXT("town_disk_captures_runtime_form"),HeliosLoadout::ReadJson(ReadTownSave(FName(*S.Map)),Disk)&&Disk==Runtime);
            Check(TEXT("town_legacy_yun_context_restored_in_memory"),SnapshotReady&&YunSession->LoadSelection(LegacySnapshot)&&YunSession->GetPlayerForm()==OriginalForm);
            Check(TEXT("town_hot_loadout_restored"),P->EquipLoadout(S.Expected));
            Check(TEXT("town_save_captures_restored_hot_loadout"),Town->SaveProgress()&&Town->GetSavedLoadout()==S.Expected);
        }
        for(const TCHAR* Id:{TEXT("rifle"),TEXT("rocket"),TEXT("laser"),TEXT("shotgun")})
        {Town->StartTown(Id);Check(FString(TEXT("town_rejects_"))+Id,Town->GetSavedLoadout()==S.Expected);}
        Check(TEXT("town_fixture_meme_exists"),!Meme.IsEmpty());
        const int32 Before=Town->GetMemeCount();Check(TEXT("town_progress_changes"),!Meme.IsEmpty()&&Town->UnlockMeme(Meme)&&Town->GetMemeCount()==Before+1);
        if(Ikun)S.IkunMemeCount=Town->GetMemeCount();else S.GhostMemeCount=Town->GetMemeCount();
        Check(TEXT("town_save_commits"),Town->SaveProgress());const auto Save=ReadTownSave(FName(*S.Map));FHeliosLoadout Saved;
        Check(TEXT("town_disk_preserves_pair"),HeliosLoadout::ReadJson(Save,Saved)&&Saved==S.Expected);Check(TEXT("town_disk_preserves_progress"),HasString(Save,TEXT("discovered_memes"),Meme));
        (Ikun?S.IkunExplored:S.GhostExplored)=ReadStrings(Save,TEXT("explored_zones"));
        S.Stage=Ikun?7:8;const bool Accepted=Travel->RequestTravelWithLoadout(Ikun?FName(TEXT("ikun_ghost_village")):FName(TEXT("ikun_town")),S.Expected);
        Check(TEXT("full_loadout_travel_accepted"),Accepted);Check(TEXT("full_loadout_pending_preserved"),Travel->PendingLoadout==S.Expected);if(!Accepted)Finish();return;
    }
}
