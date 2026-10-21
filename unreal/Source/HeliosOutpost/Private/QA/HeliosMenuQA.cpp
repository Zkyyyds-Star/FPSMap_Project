#include "QA/HeliosMenuQA.h"
#include "Pets/HeliosPetDog.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "UI/HeliosAdventureHUD.h"
#include "Adventure/HeliosAdventureCharacter.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"
TFunction<void(bool,bool)> HeliosMenuQANavigate;

namespace
{
struct FMenuAcceptance
{
    TWeakObjectPtr<AHeliosAdventureHUD> HUD;
    TArray<TSharedPtr<FJsonValue>> Checks;
    FString Output;
    double Next = 0;
    int32 Stage = 0, Failures = 0;
    float PausedTime = 0;
    const FName Ids[4] = { TEXT("rifle"), TEXT("rocket"), TEXT("laser"), TEXT("shotgun") };
    void Check(const TCHAR* Name, bool Pass)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("name"), Name);
        Row->SetBoolField(TEXT("passed"), Pass);
        Checks.Add(MakeShared<FJsonValueObject>(Row));
        if (!Pass) ++Failures;
        UE_LOG(LogTemp, Display, TEXT("MENU_QA %s %s"), Name, Pass ? TEXT("PASS") : TEXT("FAIL"));
    }
    void Shot(const FString& Name)
    {
        FScreenshotRequest::RequestScreenshot(Output / (Name + TEXT(".png")), true, false);
    }
    int32 LayoutStage = 0;
    float OriginalApplicationScale = 1.f;
    FRotator LayoutAim;
    int32 PresentationStage=0;
    bool TickPresentation(AHeliosAdventureHUD* H,APlayerController* PC)
    {
        if(!FParse::Param(FCommandLine::Get(),TEXT("HeliosMenuPresentationQA")))return false;
        const FIntPoint Sizes[]={FIntPoint(1280,720),FIntPoint(1920,1080),FIntPoint(2560,1440),FIntPoint(2560,1080)};
        if(PresentationStage>=24)return false;
        const int32 Step=PresentationStage++,Index=Step/6,Page=(Step%6)/2;
        if(Step%2==0)
        {
            PC->ConsoleCommand(FString::Printf(TEXT("r.SetRes %dx%dw"),Sizes[Index].X,Sizes[Index].Y),true);
            H->SelectWeapon(TEXT("cfm_sunshangxiang"));H->SelectPet(EHeliosPetKind::ZhongXiaoKui);
            if(HeliosMenuQANavigate)HeliosMenuQANavigate(Page>0,Page==2);
        }
        else
        {
            Check(*FString::Printf(TEXT("menu_layout_%d_%d"),Index,Page),GEngine->GameViewport->Viewport->GetSizeXY()==Sizes[Index]&&H->GetMenuState()==EHeliosAdventureMenu::WeaponSelect);
            Check(*FString::Printf(TEXT("independent_selections_%d_%d"),Index,Page),H->GetPendingWeaponId()==TEXT("cfm_sunshangxiang")&&H->GetSelectedPet()==EHeliosPetKind::ZhongXiaoKui);
            Shot(FString::Printf(TEXT("release_%dx%d_page%d"),Sizes[Index].X,Sizes[Index].Y,Page));
        }
        return true;
    }
    bool TickLayout(AHeliosAdventureHUD* H, AHeliosAdventureDirector* D,
        AHeliosAdventureCharacter* P, APlayerController* PC)
    {
        // Opt-in real viewport captures, after the existing menu regression checks.
        if (!FParse::Param(FCommandLine::Get(), TEXT("HeliosHUDLayoutQA"))) return false;
        struct FLayoutCase { int32 Width, Height; float Scale; const TCHAR* Name; };
        const FLayoutCase Cases[] = {
            {1280,720,1.f,TEXT("09_hud_1280x720")},
            {1920,1080,1.f,TEXT("10_hud_1920x1080")},
            {2560,1440,1.f,TEXT("11_hud_2560x1440")},
            {2560,1080,1.f,TEXT("12_hud_ultrawide")},
            {960,540,1.f,TEXT("13_hud_narrow")},
            {1920,1080,1.25f,TEXT("14_hud_scale125")},
            {1920,1080,1.5f,TEXT("15_hud_scale150")}
        };
        const int32 Step = LayoutStage++;
        if (Step == 0)
        {
            OriginalApplicationScale = FSlateApplication::Get().GetApplicationScale();
            H->SelectWeapon(TEXT("rifle")); H->StartSelectedRun();
            P->SetCanBeDamaged(false);
            LayoutAim = PC->GetControlRotation();
            LayoutAim.Pitch = 0; LayoutAim.Roll = 0;
            PC->SetControlRotation(LayoutAim);
            return true;
        }
        if (Step <= UE_ARRAY_COUNT(Cases)*2)
        {
            const auto& C = Cases[(Step-1)/2];
            if (Step % 2)
            {
                FSlateApplication::Get().SetApplicationScale(OriginalApplicationScale*C.Scale);
                PC->ConsoleCommand(FString::Printf(TEXT("r.SetRes %dx%dw"),C.Width,C.Height),true);
            }
            else
            {
                const FIntPoint Actual = GEngine->GameViewport->Viewport->GetSizeXY();
                Check(C.Name, Actual == FIntPoint(C.Width,C.Height) && H->GetMenuState()==EHeliosAdventureMenu::None);
                Shot(C.Name);
            }
            return true;
        }
        switch (Step-UE_ARRAY_COUNT(Cases)*2)
        {
        case 1:
            FSlateApplication::Get().SetApplicationScale(OriginalApplicationScale);
            PC->ConsoleCommand(TEXT("r.SetRes 1920x1080w"),true);
            LayoutAim.Pitch=55; PC->SetControlRotation(LayoutAim);
            break;
        case 2: Shot(TEXT("16_hud_bright_sky")); break;
        case 3: LayoutAim.Pitch=-35; LayoutAim.Yaw+=135; PC->SetControlRotation(LayoutAim); break;
        case 4: Shot(TEXT("17_hud_dark_ground")); break;
        case 5:
            P->StartFireFromAutomation(); P->StopFireFromAutomation();
            break;
        case 6:
            P->StartReloadFromAutomation(); Next=FPlatformTime::Seconds()+.2;
            break;
        case 7:
            Check(TEXT("hud_reload_visible"),P->IsReloading() && H->GetVitalsText().ToString().Contains(TEXT("装填中")));
            Shot(TEXT("18_hud_reloading")); Next=FPlatformTime::Seconds()+3;
            break;
        case 8:
            Check(TEXT("hud_reload_completed"),!P->IsReloading() && P->GetAmmo()==P->GetMagazineSize());
            Check(TEXT("hud_boss_visible"),H->HasActiveBoss());
            LayoutAim.Pitch=0; PC->SetControlRotation(LayoutAim);
            break;
        case 9: Shot(TEXT("19_hud_boss")); break;
        case 10: H->TogglePauseMenu(); break;
        case 11: H->ContinueRun(); break;
        case 12:
            Check(TEXT("hud_returns_after_pause"),H->GetMenuState()==EHeliosAdventureMenu::None && !H->GetWorld()->IsPaused());
            Shot(TEXT("20_hud_after_pause")); break;
        case 13: H->ConfirmRestart(); H->StartSelectedRun(); break;
        case 14:
            Check(TEXT("hud_returns_after_restart"),H->GetMenuState()==EHeliosAdventureMenu::None && P->GetAmmo()==P->GetMagazineSize());
            Shot(TEXT("21_hud_after_restart")); break;
        default: return false;
        }
        return true;
    }
    bool Tick(float)
    {
        if (!HUD.IsValid()) return false;
        if (FPlatformTime::Seconds() < Next) return true;
        auto* H = HUD.Get();
        auto* D = AHeliosAdventureDirector::Find(H->GetWorld());
        auto* PC = H->GetWorld()->GetFirstPlayerController();
        auto* P = PC ? Cast<AHeliosAdventureCharacter>(PC->GetPawn()) : nullptr;
        if (!D || !P || !PC) { Next = FPlatformTime::Seconds() + 1; return true; }
        Next = FPlatformTime::Seconds() + 2;
        switch (Stage++)
        {
        case 0:
            Check(TEXT("initial_selection_and_cursor"), H->GetMenuState() == EHeliosAdventureMenu::WeaponSelect && PC->bShowMouseCursor);
            for (FName Id : Ids)
            {
                FString Asset = FString::Printf(TEXT("/Game/Helios/UI/Menu/T_Menu_%s.T_Menu_%s"), *Id.ToString(), *Id.ToString());
                auto* T = LoadObject<UTexture2D>(nullptr, *Asset);
                Check(*(Id.ToString() + TEXT("_menu_texture_2048x1024")), T && T->GetSizeX() == 2048 && T->GetSizeY() == 1024);
            }
            H->SelectWeapon(Ids[0]); Shot(TEXT("01_weapon_rifle"));
            break;
        case 1: H->SelectWeapon(Ids[1]); Shot(TEXT("02_weapon_rocket")); break;
        case 2: H->SelectWeapon(Ids[2]); Shot(TEXT("03_weapon_laser")); break;
        case 3: H->SelectWeapon(Ids[3]); Shot(TEXT("04_weapon_shotgun")); break;
        case 4:
        case 5:
        case 6:
        case 7:
        {
            const int32 Index = Stage - 5;
            if (Index > 0)
                Check(*(Ids[Index - 1].ToString() + TEXT("_selection_starts_matching_weapon")), P->GetWeaponId() == Ids[Index - 1] && H->GetMenuState() == EHeliosAdventureMenu::None && !PC->bShowMouseCursor);
            H->ConfirmRestart();
            H->SelectWeapon(Ids[Index]);
            H->StartSelectedRun();
            if (Index == 0) Shot(TEXT("05_gameplay_hud"));
            break;
        }
        case 8:
            Check(TEXT("shotgun_selection_starts_matching_weapon"), P->GetWeaponId() == Ids[3] && H->GetMenuState() == EHeliosAdventureMenu::None && !PC->bShowMouseCursor);
            H->TogglePauseMenu();
            PausedTime = D->GetSurvivalSeconds();
            break;
        case 9:
            Check(TEXT("pause_freezes_time_and_releases_cursor"), H->GetWorld()->IsPaused() && PC->bShowMouseCursor && D->GetSurvivalSeconds() == PausedTime);
            Shot(TEXT("06_pause"));
            break;
        case 10:
            H->ToggleMemeSound(); Check(TEXT("sound_off"), !H->IsMemeSoundEnabled());
            H->ToggleMemeSound(); Check(TEXT("sound_on"), H->IsMemeSoundEnabled());
            H->RequestRestartConfirmation();
            break;
        case 11:
            Check(TEXT("restart_confirmation_paused"), H->GetMenuState() == EHeliosAdventureMenu::RestartConfirm && H->GetWorld()->IsPaused());
            Shot(TEXT("07_restart_confirm")); break;
        case 12:
            H->TogglePauseMenu();
            Check(TEXT("escape_confirmation_returns_to_pause"), H->GetMenuState() == EHeliosAdventureMenu::Pause);
            H->ContinueRun();
            Check(TEXT("continue_recaptures_game_input"), H->GetMenuState() == EHeliosAdventureMenu::None && !PC->bShowMouseCursor && !H->GetWorld()->IsPaused());
            break;
        case 13: D->EndRun(); break;
        case 14:
            Check(TEXT("death_opens_results"), H->GetMenuState() == EHeliosAdventureMenu::Death && PC->bShowMouseCursor);
            Shot(TEXT("08_death")); break;
        case 15:
            H->ConfirmRestart();
            Check(TEXT("death_restart_returns_to_selection"), H->GetMenuState() == EHeliosAdventureMenu::WeaponSelect && D->IsSelectingWeapon());
            H->SelectWeapon(TEXT("invalid_menu_qa_weapon"));
            Check(TEXT("invalid_weapon_is_rejected"), H->GetPendingWeaponId() == Ids[3]);
            break;
        default:
        {
            if(TickPresentation(H,PC))return true;
            if (TickLayout(H,D,P,PC)) return true;
            auto Report = MakeShared<FJsonObject>();
            Report->SetStringField(TEXT("status"), Failures ? TEXT("failed") : TEXT("passed"));
            Report->SetNumberField(TEXT("failures"), Failures);
            Report->SetArrayField(TEXT("checks"), Checks);
            Report->SetStringField(TEXT("method"), TEXT("actual game; public HUD actions; engine screenshots include Slate; death triggered through EndRun"));
            if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
            {
                const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
                Report->SetNumberField(TEXT("width"), Size.X); Report->SetNumberField(TEXT("height"), Size.Y);
            }
            FString Json;
            FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
            FFileHelper::SaveStringToFile(Json, *(Output / TEXT("report.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            H->QuitToDesktop();
            return false;
        }
        }
        return true;
    }
};
}
#endif

void StartHeliosMenuQA(TWeakObjectPtr<AHeliosAdventureHUD> HUD)
{
#if WITH_DEV_AUTOMATION_TESTS
    if (!FParse::Param(FCommandLine::Get(), TEXT("HeliosMenuQA"))) return;
    auto Runner = MakeShared<FMenuAcceptance>();
    Runner->HUD = HUD;
    Runner->Output = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../qa/menu_redesign/runtime"));
    FParse::Value(FCommandLine::Get(), TEXT("HeliosMenuQAOutput="), Runner->Output);
    IFileManager::Get().MakeDirectory(*Runner->Output, true);
    Runner->Next = FPlatformTime::Seconds() + 6;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Runner](float Delta) { return Runner->Tick(Delta); }));
#endif
}
