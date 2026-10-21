#include "QA/HeliosLoadoutMenuQA.h"
#include "UI/HeliosAdventureHUD.h"
#include "Player/HeliosCharacterDefinition.h"
#include "Pets/HeliosPetDog.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SWindow.h"
#include "InputCoreTypes.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "UnrealClient.h"

namespace
{
bool HasLabel(const TSharedRef<SWidget>& Widget,const FString& Text)
{
    if(!Widget->GetVisibility().IsVisible())return false;
    if(Widget->GetTypeAsString()==TEXT("STextBlock")&&StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString()==Text)return true;
    auto* Children=Widget->GetChildren();
    for(int32 I=0;Children&&I<Children->Num();++I)if(HasLabel(Children->GetChildAt(I),Text))return true;
    return false;
}
TSharedPtr<SWidget> FindWidget(const TSharedRef<SWidget>& Widget,const FString& Text,const FString& Type)
{
    if(!Widget->GetVisibility().IsVisible())return nullptr;
    if(Widget->GetTypeAsString()==Type&&HasLabel(Widget,Text))return Widget;
    auto* Children=Widget->GetChildren();
    for(int32 I=0;Children&&I<Children->Num();++I)if(auto Found=FindWidget(Children->GetChildAt(I),Text,Type))return Found;
    return nullptr;
}
TSharedPtr<SWidget> FindWidget(const FString& Text,const FString& Type=TEXT("SButton"))
{
    // This is the window that actually hosts the game UI, including an offscreen viewport.
    if(GEngine&&GEngine->GameViewport)
        if(const auto Window=GEngine->GameViewport->GetWindow())
            if(auto Found=FindWidget(Window->GetContent(),Text,Type))return Found;
    if(FSlateApplication::IsInitialized())
        for(const auto& Window:FSlateApplication::Get().GetInteractiveTopLevelWindows())
            if(auto Found=FindWidget(Window->GetContent(),Text,Type))return Found;
    return nullptr;
}
bool EnabledInPath(const TSharedPtr<SWidget>& Widget)
{
    if(!Widget)return false;FWidgetPath Path;
    if(!FSlateApplication::Get().GeneratePathToWidgetUnchecked(Widget.ToSharedRef(),Path))return false;
    for(int32 I=0;I<Path.Widgets.Num();++I)if(!Path.Widgets[I].Widget->IsEnabled())return false;
    return true;
}
bool ClickWidget(const TSharedPtr<SWidget>& Button,const FString& Label)
{
    const auto Fail=[&Label](const TCHAR* Reason){UE_LOG(LogTemp,Display,TEXT("LOADOUT_MENU_QA_CLICK_REJECT label=%s reason=%s"),*Label,Reason);return false;};
    if(!Button)return Fail(TEXT("button_not_found"));
    if(!EnabledInPath(Button))return Fail(TEXT("widget_path_disabled_or_unavailable"));
    auto& Slate=FSlateApplication::Get();FWidgetPath Path;
    if(!Slate.GeneratePathToWidgetUnchecked(Button.ToSharedRef(),Path))return Fail(TEXT("widget_path_unavailable"));
    const auto Geometry=Button->GetCachedGeometry();if(Geometry.GetLocalSize().X<=1.f||Geometry.GetLocalSize().Y<=1.f)return Fail(TEXT("slate_layout_not_ready"));
    const FVector2D At=Geometry.LocalToAbsolute(Geometry.GetLocalSize()*.5f);
    TSet<FKey> Down;Down.Add(EKeys::LeftMouseButton);TSet<FKey> Up;
    FPointerEvent Press(FSlateApplication::CursorPointerIndex,At,At,Down,EKeys::LeftMouseButton,0,FModifierKeysState());
    FPointerEvent Release(FSlateApplication::CursorPointerIndex,At,At,Up,EKeys::LeftMouseButton,0,FModifierKeysState());
    Slate.ReleaseAllPointerCapture();Button->OnMouseEnter(Geometry,Press);
    auto PressReply=Button->OnMouseButtonDown(Geometry,Press);Slate.ProcessReply(Path,PressReply,&Path,&Press);
    auto ReleaseReply=Button->OnMouseButtonUp(Geometry,Release);Slate.ProcessReply(Path,ReleaseReply,&Path,&Release);
    return PressReply.IsEventHandled()&&ReleaseReply.IsEventHandled()?true:Fail(TEXT("pointer_event_unhandled"));
}
bool Click(const FString& Label){return ClickWidget(FindWidget(Label),Label);}
bool ClickSlotAction(int32 Slot,const FString& Label)
{
    const auto SlotButton=FindWidget(FString::Printf(TEXT("伙伴 %d"),Slot+1));
    if(!SlotButton)return false;FWidgetPath Path;
    if(!FSlateApplication::Get().GeneratePathToWidgetUnchecked(SlotButton.ToSharedRef(),Path))return false;
    for(int32 I=Path.Widgets.Num()-2;I>=0;--I)
        if(Path.Widgets[I].Widget->GetTypeAsString()==TEXT("SBorder"))
        {
            const auto Text=FindWidget(Path.Widgets[I].Widget,Label,TEXT("STextBlock"));
            // The entry must be readable on the actual rendered viewport.
            if(!Text||Text->GetCachedGeometry().GetLocalSize().X+1<Text->GetDesiredSize().X)return false;
            return ClickWidget(FindWidget(Path.Widgets[I].Widget,Label,TEXT("SButton")),Label);
        }
    return false;
}
bool ScrollTo(const FString& Label)
{
    auto Button=FindWidget(Label);auto Scroll=FindWidget(Label,TEXT("SScrollBox"));
    if(!Button||!Scroll)return false;
    StaticCastSharedPtr<SScrollBox>(Scroll)->ScrollDescendantIntoView(Button,false,EDescendantScrollDestination::Center);
    return true;
}
bool PrimaryButtonsEnabled(bool Expected)
{
    bool Found=false;
    for(const auto* Definition:UHeliosWeaponRegistry::GetAvailableWeapons())
    {
        if(Definition->WeaponId==TEXT("mech")||Definition->WeaponId==TEXT("fighter"))continue;
        const auto Button=FindWidget(Definition->DisplayName.ToString());
        if(!Button||EnabledInPath(Button)!=Expected)
        {
            UE_LOG(LogTemp,Display,TEXT("LOADOUT_MENU_QA_PRIMARY_CARD_REJECT weapon=%s expected_enabled=%d found=%d"),*Definition->WeaponId.ToString(),Expected,Button.IsValid());
            return false;
        }
        Found=true;
    }
    return Found;
}
}

AHeliosLoadoutMenuQA::AHeliosLoadoutMenuQA()
{PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;PrimaryActorTick.TickInterval=0.f;}
void AHeliosLoadoutMenuQA::BeginPlay()
{
    Super::BeginPlay();Started=FPlatformTime::Seconds();Next=Started+4.;
    Out=FPaths::ProjectSavedDir()/TEXT("LoadoutMenuQA");FParse::Value(FCommandLine::Get(),TEXT("HeliosLoadoutOut="),Out);
    IFileManager::Get().MakeDirectory(*Out,true);Roster.SetNum(4);BeforePrimary.SetNum(4);WriteReport();
}
void AHeliosLoadoutMenuQA::Check(const FString& Name,bool Pass)
{
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("name"),Name);Row->SetBoolField(TEXT("passed"),Pass);Row->SetNumberField(TEXT("step"),Step);
    Checks.Add(MakeShared<FJsonValueObject>(Row));if(!Pass)++Failures;
    UE_LOG(LogTemp,Display,TEXT("LOADOUT_MENU_QA %s %s"),*Name,Pass?TEXT("PASS"):TEXT("FAIL"));WriteReport();
}
void AHeliosLoadoutMenuQA::ClickCheck(const TCHAR* Label,const TCHAR* Name){Check(Name,Click(Label));}
void AHeliosLoadoutMenuQA::Capture(const TCHAR* Name)
{Captures.Add(FString(Name)+TEXT(".png"));FScreenshotRequest::RequestScreenshot(Out/Captures.Last(),true,false);}
void AHeliosLoadoutMenuQA::WriteReport()const
{
    auto Report=MakeShared<FJsonObject>();Report->SetStringField(TEXT("status"),bFinished?TEXT("complete"):TEXT("running"));
    Report->SetBoolField(TEXT("passed"),bFinished&&Failures==0);Report->SetNumberField(TEXT("failures"),Failures);Report->SetArrayField(TEXT("checks"),Checks);
    Report->SetBoolField(TEXT("isolated_saves"),FParse::Param(FCommandLine::Get(),TEXT("HeliosLoadoutQA")));
    TArray<TSharedPtr<FJsonValue>> Images;for(const auto& Name:Captures)Images.Add(MakeShared<FJsonValueString>(Name));Report->SetArrayField(TEXT("captures"),Images);
    FString Text;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Out/TEXT("menu.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
void AHeliosLoadoutMenuQA::Finish()
{
    if(bFinished)return;
    bool ImagesReady=Captures.Num()==6;for(const auto& Name:Captures)ImagesReady&=IFileManager::Get().FileSize(*(Out/Name))>100;
    Check(TEXT("six_viewport_captures_written"),ImagesReady);bFinished=true;WriteReport();
    UE_LOG(LogTemp,Display,TEXT("LOADOUT_MENU_QA_COMPLETE failures=%d checks=%d captures=%d"),Failures,Checks.Num(),Captures.Num());
    FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}
void AHeliosLoadoutMenuQA::BuildSteps()
{
    Steps.Add([this]{
        // Arrange after world/HUD startup. SelectLoadout only accepts visible slots, so populate at count four.
        // A previous isolated run may have saved a fixed dog; every tested change below still uses Slate input.
        const bool Isolated=FParse::Param(FCommandLine::Get(),TEXT("HeliosLoadoutQA"));
        auto* Session=GetGameInstance()->GetSubsystem<UHeliosPetSession>();
        bool FixtureReady=Isolated&&IsValid(Session);
        UE_LOG(LogTemp,Display,TEXT("LOADOUT_MENU_QA_FIXTURE session=%s valid=%d isolated=%d initial_count=%d loadout_slots=%d kind_slots=%d"),
            *GetNameSafe(Session),IsValid(Session),Isolated,Session?Session->GetCount():-1,Session?Session->SelectedLoadouts.Num():-1,Session?Session->SelectedKinds.Num():-1);
        if(FixtureReady)
        {
            Session->SetCount(4);FixtureReady&=Session->GetCount()==4;
            for(int32 I=0;I<4;++I)
            {
                const bool Accepted=Session->SelectLoadout(I,FHeliosLoadout::MakeDefault());
                const auto Actual=Session->GetLoadout(I);
                UE_LOG(LogTemp,Display,TEXT("LOADOUT_MENU_QA_FIXTURE_SLOT slot=%d count=%d accepted=%d character=%s variant=%s primary=%s kind=%d"),
                    I,Session->GetCount(),Accepted,*Actual.CharacterId.ToString(),*Actual.CharacterVariantId.ToString(),*Actual.PrimaryWeaponId.ToString(),int32(Session->GetKind(I)));
                FixtureReady&=Accepted&&Actual==FHeliosLoadout::MakeDefault();
            }
            Session->SetCount(1);FixtureReady&=Session->GetCount()==1;
            for(int32 I=0;I<4;++I)FixtureReady&=Session->GetLoadout(I)==FHeliosLoadout::MakeDefault();
        }
        Check(TEXT("companion_fixture_four_humanoid_primaries"),FixtureReady);
    });
    // A step can click and inspect synchronous loadout data. Slate visibility/layout checks run in a later step.
    Steps.Add([this]{
        PlayerExpected=HUD->GetPendingLoadout();Check(TEXT("initial_selection_visible"),HUD->GetMenuState()==EHeliosAdventureMenu::WeaponSelect);
        bool PreviewsReady=true;for(const auto* Definition:FHeliosCharacterRegistry::GetAvailableCharacters())
        {
            PreviewsReady&=Definition->PreviewTexture.LoadSynchronous()!=nullptr;
            for(const auto& Form:Definition->Forms)PreviewsReady&=Form.PreviewTexture.LoadSynchronous()!=nullptr;
        }
        Check(TEXT("character_preview_assets_resolve"),PreviewsReady);
        ClickCheck(TEXT("人物、武器与伙伴  →"),TEXT("open_equipment_clicked"));
    });
    Steps.Add([this]{Check(TEXT("player_character_scroll"),ScrollTo(TEXT("孙尚香")));});
    Steps.Add([this]{ClickCheck(TEXT("孙尚香"),TEXT("player_character_clicked"));Check(TEXT("character_keeps_primary"),HUD->GetPendingCharacterId()==TEXT("cfm_sunshangxiang")&&HUD->GetPendingWeaponId()==PlayerExpected.PrimaryWeaponId);PlayerExpected=HUD->GetPendingLoadout();});
    Steps.Add([this]{Check(TEXT("yun_card_scroll"),ScrollTo(TEXT("云悠悠·沐夏微醺")));});
    Steps.Add([this]{
        const auto Yun=FindWidget(TEXT("云悠悠·沐夏微醺"));Check(TEXT("yun_card_visible_enabled"),Yun&&EnabledInPath(Yun));
        Check(TEXT("reserved_slots_notice_visible"),FindWidget(TEXT("副武器、近战槽 · 暂未开放"),TEXT("STextBlock")).IsValid());
        const auto Before=HUD->GetPendingLoadout();Check(TEXT("yun_character_clicked"),Click(TEXT("云悠悠·沐夏微醺")));
        Check(TEXT("yun_character_keeps_primary"),HUD->GetPendingCharacterId()==TEXT("cfm_yun_summer")&&HUD->GetPendingWeaponId()==Before.PrimaryWeaponId);
        Check(TEXT("butterfly_character_absent"),!FindWidget(TEXT("蝴蝶·雷神助战")));
    });
    static const TCHAR* YunVariants[]={TEXT("GR"),TEXT("BL"),TEXT("SP")};
    static const TCHAR* YunLabels[]={TEXT("保卫者"),TEXT("潜伏者"),TEXT("灵魂武者")};
    for(int32 I=0;I<3;++I)
    {
        Steps.Add([this,I]{
        const auto Before=HUD->GetPendingLoadout();Check(FString(TEXT("player_form_clicked_"))+YunVariants[I],Click(YunLabels[I]));
        const auto Actual=HUD->GetPendingLoadout();Check(FString(TEXT("player_form_keeps_primary_"))+YunVariants[I],Actual.CharacterId==Before.CharacterId&&Actual.PrimaryWeaponId==Before.PrimaryWeaponId&&Actual.CharacterVariantId==YunVariants[I]);
        PlayerExpected=Actual;
        });
        Steps.Add([this,I]{const auto Before=HUD->GetPendingLoadout();Check(FString(TEXT("player_same_character_keeps_form_"))+YunVariants[I],Click(TEXT("云悠悠·沐夏微醺"))&&HUD->GetPendingLoadout()==Before&&Before.CharacterVariantId==YunVariants[I]);});
    }
    Steps.Add([this]{Check(TEXT("yun_forms_notice_visible"),FindWidget(TEXT("三种人物形态可自由搭配主武器。H 切换人物形态 · J 敬酒互动。"),TEXT("STextBlock")).IsValid());Capture(TEXT("01_player_characters"));});
    Steps.Add([this]{ClickCheck(TEXT("玩家主武器"),TEXT("player_primary_tab_clicked"));});
    Steps.Add([this]{Check(TEXT("player_primary_scroll"),ScrollTo(TEXT("P90双脊龙")));});
    Steps.Add([this]{
        ClickCheck(TEXT("P90双脊龙"),TEXT("player_primary_clicked"));Check(TEXT("primary_keeps_character"),HUD->GetPendingCharacterId()==PlayerExpected.CharacterId&&HUD->GetPendingLoadout().CharacterVariantId==PlayerExpected.CharacterVariantId&&HUD->GetPendingWeaponId()==TEXT("cfm_bulianshi_p90"));PlayerExpected=HUD->GetPendingLoadout();
    });
    Steps.Add([this]{
        bool RetiredAbsent=true;for(const TCHAR* Label:{TEXT("突击步枪"),TEXT("火箭发射器"),TEXT("激光枪"),TEXT("霰弹枪")})RetiredAbsent&=!FindWidget(Label);
        Check(TEXT("retired_primary_cards_absent"),RetiredAbsent);Capture(TEXT("02_player_primary"));
    });
    Steps.Add([this]{ClickCheck(TEXT("玩家人物"),TEXT("player_character_tab_clicked"));});
    Steps.Add([this]{Check(TEXT("second_character_scroll"),ScrollTo(TEXT("钟小葵")));});
    Steps.Add([this]{ClickCheck(TEXT("钟小葵"),TEXT("second_character_clicked"));Check(TEXT("second_character_keeps_primary"),HUD->GetPendingCharacterId()==TEXT("cfm_zhongxiaokui")&&HUD->GetPendingWeaponId()==PlayerExpected.PrimaryWeaponId);PlayerExpected=HUD->GetPendingLoadout();});
    static const TCHAR* RepeatPlayerIds[]={TEXT("cfm_bulianshi"),TEXT("cfm_wangzhekui")};
    static const TCHAR* RepeatPlayerLabels[]={TEXT("步练师"),TEXT("王者葵")};
    for(int32 I=0;I<2;++I)
    {
        Steps.Add([this,I]{Check(FString(TEXT("repeat_player_character_scroll_"))+RepeatPlayerIds[I],ScrollTo(RepeatPlayerLabels[I]));});
        Steps.Add([this,I]{const auto Before=HUD->GetPendingLoadout();Check(FString(TEXT("repeat_player_character_clicked_"))+RepeatPlayerIds[I],Click(RepeatPlayerLabels[I])&&HUD->GetPendingCharacterId()==RepeatPlayerIds[I]&&HUD->GetPendingWeaponId()==Before.PrimaryWeaponId);});
        Steps.Add([this,I]{const auto Before=HUD->GetPendingLoadout();Check(FString(TEXT("repeat_player_form_selected_"))+RepeatPlayerIds[I],Click(TEXT("潜伏者"))&&HUD->GetPendingLoadout().CharacterVariantId==TEXT("BL")&&HUD->GetPendingWeaponId()==Before.PrimaryWeaponId);PlayerExpected=HUD->GetPendingLoadout();});
        Steps.Add([this,I]{const auto Before=HUD->GetPendingLoadout();Check(FString(TEXT("player_same_character_keeps_form_"))+RepeatPlayerIds[I],Click(RepeatPlayerLabels[I])&&HUD->GetPendingLoadout()==Before&&Before.CharacterVariantId==TEXT("BL"));});
    }
    Steps.Add([this]{ClickCheck(TEXT("AI 伙伴"),TEXT("companions_tab_clicked"));});
    Steps.Add([this]{ClickCheck(TEXT("4 位"),TEXT("four_companions_clicked"));Check(TEXT("four_companions_count_updates"),HUD->GetSelectedPetCount()==4);});
    Steps.Add([this]{
        bool AllVisible=HUD->GetSelectedPetCount()==4;
        for(int32 I=0;I<4;++I)
        {
            const auto Button=FindWidget(FString::Printf(TEXT("伙伴 %d"),I+1));
            AllVisible&=Button&&EnabledInPath(Button)&&Button->GetCachedGeometry().GetLocalSize().X>1.f&&Button->GetCachedGeometry().GetLocalSize().Y>1.f;
        }
        Check(TEXT("four_slots_visible"),AllVisible);
    });
    static const TCHAR* CharacterLabels[]={TEXT("步练师"),TEXT("云悠悠·沐夏微醺"),TEXT("钟小葵"),TEXT("王者葵")};
    static const TCHAR* CharacterIds[]={TEXT("cfm_bulianshi"),TEXT("cfm_yun_summer"),TEXT("cfm_zhongxiaokui"),TEXT("cfm_wangzhekui")};
    static const TCHAR* CompanionVariants[]={TEXT("BL"),TEXT("GR"),TEXT("None"),TEXT("BL")};
    static const TCHAR* CompanionFormLabels[]={TEXT("潜伏者"),TEXT("保卫者"),TEXT(""),TEXT("潜伏者")};
    for(int32 I=0;I<4;++I)
    {
        Steps.Add([this,I]{Check(FString::Printf(TEXT("slot_%d_clicked"),I),ClickSlotAction(I,TEXT("选择人物"))&&HUD->GetActivePetSlot()==I);BeforePrimary[I]=HUD->GetPetLoadout(I).PrimaryWeaponId;});
        Steps.Add([this,I]{Check(FString::Printf(TEXT("slot_%d_character_scroll"),I),ScrollTo(CharacterLabels[I]));});
        Steps.Add([this,I]{Check(FString::Printf(TEXT("slot_%d_character_clicked"),I),Click(CharacterLabels[I]));Roster[I]=HUD->GetPetLoadout(I);Check(FString::Printf(TEXT("slot_%d_character_keeps_primary"),I),Roster[I].CharacterId==CharacterIds[I]&&Roster[I].PrimaryWeaponId==BeforePrimary[I]);});
        if(I!=2)Steps.Add([this,I]{
            const auto Before=HUD->GetPetLoadout(I);Check(FString::Printf(TEXT("slot_%d_form_clicked"),I),Click(CompanionFormLabels[I]));Roster[I]=HUD->GetPetLoadout(I);
            Check(FString::Printf(TEXT("slot_%d_form_keeps_primary"),I),Roster[I].CharacterId==Before.CharacterId&&Roster[I].PrimaryWeaponId==Before.PrimaryWeaponId&&Roster[I].CharacterVariantId==CompanionVariants[I]);
        });
        if(I!=2)Steps.Add([this,I]{const auto Before=HUD->GetPetLoadout(I);Check(FString::Printf(TEXT("slot_%d_same_character_keeps_form"),I),Click(CharacterLabels[I])&&HUD->GetPetLoadout(I)==Before&&Before.CharacterVariantId==CompanionVariants[I]);});
    }
    Steps.Add([this]{Capture(TEXT("03_four_slot_characters"));});
    Steps.Add([this]{ClickCheck(TEXT("伙伴主武器"),TEXT("companion_primary_tab_clicked"));});
    // Slate evaluates the newly shown grid's visibility during its next prepass.
    Steps.Add([this]{Check(TEXT("companions_have_handheld_primaries"),PrimaryButtonsEnabled(true));});
    static const TCHAR* WeaponIds[]={TEXT("cola"),TEXT("cfm_chan_zeus"),TEXT("cfm_bulianshi_p90"),TEXT("lightstick")};
    for(int32 I=0;I<4;++I)
    {
        Steps.Add([this,I]{Check(FString::Printf(TEXT("slot_%d_primary_slot_clicked"),I),Click(FString::Printf(TEXT("伙伴 %d"),I+1)));});
        Steps.Add([this,I]{const auto* Definition=UHeliosWeaponRegistry::FindWeapon(WeaponIds[I]);Check(FString::Printf(TEXT("slot_%d_primary_scroll"),I),Definition&&ScrollTo(Definition->DisplayName.ToString()));});
        Steps.Add([this,I]{const auto* Definition=UHeliosWeaponRegistry::FindWeapon(WeaponIds[I]);Check(FString::Printf(TEXT("slot_%d_primary_clicked"),I),Definition&&Click(Definition->DisplayName.ToString()));const auto Actual=HUD->GetPetLoadout(I);Check(FString::Printf(TEXT("slot_%d_primary_keeps_character"),I),Actual.CharacterId==Roster[I].CharacterId&&Actual.CharacterVariantId==Roster[I].CharacterVariantId&&Actual.PrimaryWeaponId==WeaponIds[I]);Roster[I]=Actual;});
    }
    for(int32 Slot=0;Slot<4;++Slot)
    {
        Steps.Add([this,Slot]{Check(FString::Printf(TEXT("slot_%d_direct_primary_entry"),Slot),ClickSlotAction(Slot,TEXT("选择主武器"))&&HUD->GetActivePetSlot()==Slot);});
        Steps.Add([this,Slot]{Check(FString::Printf(TEXT("slot_%d_primary_heading"),Slot),FindWidget(FString::Printf(TEXT("正在给伙伴 %d 选择主武器"),Slot+1),TEXT("STextBlock")).IsValid());});
        for(const auto* Definition:UHeliosWeaponRegistry::GetAvailableWeapons())
        {
            const FName Id=Definition->WeaponId;if(Id==TEXT("mech")||Id==TEXT("fighter"))continue;
            Steps.Add([this,Id]{const auto* D=UHeliosWeaponRegistry::FindWeapon(Id);Check(TEXT("direct_entry_primary_scroll_")+Id.ToString(),D&&ScrollTo(D->DisplayName.ToString()));});
            Steps.Add([this,Slot,Id]{
                const auto* D=UHeliosWeaponRegistry::FindWeapon(Id);const auto Before=HUD->GetPetLoadout(Slot);
                TArray<FHeliosLoadout> Others;for(int32 I=0;I<4;++I)Others.Add(HUD->GetPetLoadout(I));
                const FString Prefix=FString::Printf(TEXT("slot_%d_all_primary_%s"),Slot,*Id.ToString());
                Check(Prefix+TEXT("_click"),D&&Click(D->DisplayName.ToString()));
                const auto After=HUD->GetPetLoadout(Slot);bool Independent=HUD->GetPendingLoadout()==PlayerExpected;
                for(int32 I=0;I<4;++I)if(I!=Slot)Independent&=HUD->GetPetLoadout(I)==Others[I];
                Check(Prefix+TEXT("_selection"),After.PrimaryWeaponId==Id&&After.CharacterId==Before.CharacterId&&After.CharacterVariantId==Before.CharacterVariantId&&Independent);
                FString Disk;TSharedPtr<FJsonObject> Json;const TArray<TSharedPtr<FJsonValue>>* Loadouts=nullptr;
                auto* Session=GetGameInstance()->GetSubsystem<UHeliosPetSession>();
                const FString Path=Session->SelectionSavePathOverride.IsEmpty()?FPaths::ProjectSavedDir()/TEXT("Automation/HeliosLoadout/HeliosCompanions.v3.json"):Session->SelectionSavePathOverride;
                FHeliosLoadout Saved;
                Check(Prefix+TEXT("_disk"),FFileHelper::LoadFileToString(Disk,*Path)&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Disk),Json)&&Json.IsValid()&&Json->TryGetArrayField(TEXT("loadouts"),Loadouts)&&Loadouts->IsValidIndex(Slot)&&HeliosLoadout::ReadJson((*Loadouts)[Slot]->AsObject(),Saved)&&Saved==After);
                Roster[Slot]=After;
            });
        }
    }
    Steps.Add([this]{bool Same=true;for(int32 I=0;I<4;++I)Same&=HUD->GetPetLoadout(I)==Roster[I];Check(TEXT("four_loadouts_remain_independent"),Same&&HUD->GetPendingLoadout()==PlayerExpected);Capture(TEXT("04_four_slot_primaries"));});
    Steps.Add([this]{
        const auto Before=HUD->GetPetLoadout(3);HUD->SelectPetWeapon(TEXT("rifle"));
        Check(TEXT("invalid_primary_reason_visible_and_pair_retained"),HUD->GetPetLoadout(3)==Before&&HUD->GetMenuDetails().ToString().Contains(TEXT("已移除")));
        auto* Session=GetGameInstance()->GetSubsystem<UHeliosPetSession>();const auto OldPath=Session->SelectionSavePathOverride;
        const FString BlockedPath=Out/TEXT("blocked_save_directory");IFileManager::Get().MakeDirectory(*BlockedPath,true);Session->SelectionSavePathOverride=BlockedPath;
        HUD->SelectPetWeapon(Before.PrimaryWeaponId==TEXT("cola")?FName("cfm_chan_zeus"):FName("cola"));Session->SelectionSavePathOverride=OldPath;
        Check(TEXT("save_failure_reason_visible_and_pair_retained"),HUD->GetPetLoadout(3)==Before&&HUD->GetMenuDetails().ToString().Contains(TEXT("存档写入失败")));
    });
    Steps.Add([this]{ClickCheck(TEXT("伙伴人物"),TEXT("companion_character_tab_clicked"));});
    Steps.Add([this]{Check(TEXT("dog_card_scroll"),ScrollTo(TEXT("刀盾狗 · 固定刀盾")));});
    Steps.Add([this]{ClickCheck(TEXT("刀盾狗 · 固定刀盾"),TEXT("dog_clicked"));const auto Dog=HUD->GetPetLoadout(3);Check(TEXT("dog_has_fixed_empty_slots"),Dog.CharacterId==TEXT("sword_dog")&&Dog.PrimaryWeaponId.IsNone()&&Dog.SecondaryWeaponId.IsNone()&&Dog.MeleeWeaponId.IsNone());});
    Steps.Add([this]{ClickCheck(TEXT("伙伴主武器"),TEXT("dog_primary_tab_clicked"));});
    Steps.Add([this]{
        Check(TEXT("dog_all_primary_buttons_disabled"),PrimaryButtonsEnabled(false));const auto Before=HUD->GetPetLoadout(3);const auto* Definition=UHeliosWeaponRegistry::FindWeapon(TEXT("cola"));
        Check(TEXT("dog_disabled_primary_click_keeps_loadout"),Definition&&!Click(Definition->DisplayName.ToString())&&HUD->GetPetLoadout(3)==Before);
        bool OtherSlots=true;for(int32 I=0;I<3;++I)OtherSlots&=HUD->GetPetLoadout(I)==Roster[I];Check(TEXT("dog_keeps_other_slots_and_player"),OtherSlots&&HUD->GetPendingLoadout()==PlayerExpected);Capture(TEXT("05_dog_fixed_primary"));
    });
    Steps.Add([this]{ClickCheck(TEXT("0 位"),TEXT("zero_companions_clicked"));});
    Steps.Add([this]{
        Check(TEXT("zero_companions_hides_slots"),HUD->GetSelectedPetCount()==0&&!FindWidget(TEXT("伙伴 1"))&&!FindWidget(TEXT("伙伴 4")));
        Check(TEXT("zero_companions_disables_primaries"),PrimaryButtonsEnabled(false));
        Check(TEXT("zero_companions_notice_visible"),FindWidget(TEXT("独自行动 · 本局不会生成伙伴"),TEXT("STextBlock")).IsValid());Capture(TEXT("06_zero_companions"));
    });
}
void AHeliosLoadoutMenuQA::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);if(bFinished)return;const double Now=FPlatformTime::Seconds();
    if(Now-Started>300.){Check(TEXT("menu_timeout"),false);Finish();return;}
    if(Now<Next)return;
    if(!HUD.IsValid())
    {
        const auto* Controller=UGameplayStatics::GetPlayerController(this,0);HUD=Controller?Cast<AHeliosAdventureHUD>(Controller->GetHUD()):nullptr;
        if(!HUD.IsValid()||HUD->GetMenuState()!=EHeliosAdventureMenu::WeaponSelect)return;
    }
    if(Steps.IsEmpty())
    {
        // BeginPlay's wall-clock delay can expire during engine initialization, before Slate's first layout.
        // Wait for the rendered button and its actual enabled ancestor path instead of clicking in frame zero.
        const auto Button=FindWidget(TEXT("人物、武器与伙伴  →"));
        if(!Button||!EnabledInPath(Button))return;
        const auto Size=Button->GetCachedGeometry().GetLocalSize();if(Size.X<=1.f||Size.Y<=1.f)return;
        Check(TEXT("initial_slate_layout_ready"),true);
        UE_LOG(LogTemp,Display,TEXT("LOADOUT_MENU_QA_LAYOUT_READY button_size=%s"),*Size.ToString());
        BuildSteps();Next=Now+.1;return;
    }
    if(Step>=Steps.Num()){Finish();return;}
    Steps[Step++]();Next=Now+.45;
}
