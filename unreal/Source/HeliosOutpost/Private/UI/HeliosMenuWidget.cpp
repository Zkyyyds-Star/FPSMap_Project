#include "UI/HeliosMenuWidget.h"
#include "Audio/HeliosAudioSettingsSubsystem.h"
#include "Widgets/Input/SSlider.h"
#include "Pets/HeliosPetDog.h"
#include "QA/HeliosMenuQA.h"
#include "UI/HeliosAdventureHUD.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Player/HeliosCharacterDefinition.h"
#include "Weapons/HeliosWeaponTempoSettings.h"
#include "Weapons/HeliosYunSummerMegatron.h"
#include "World/HeliosMapDefinition.h"
#include "Brushes/SlateNoResource.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace HeliosMenu
{
FLinearColor Color(const TCHAR* Hex) { return FLinearColor(FColor::FromHex(Hex)); }
const FLinearColor Paper = Color(TEXT("DFE7EB"));
const FLinearColor Muted = Color(TEXT("91A2AD"));
const FLinearColor Accent = Color(TEXT("91B9D0"));
const FLinearColor Danger = Color(TEXT("C08383"));

FSlateFontInfo Font(int32 Size)
{
    return FSlateFontInfo(FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("UI/Fonts/SimHei.ttf")), Size);
}
FSlateFontInfo NumberFont(int32 Size)
{
    return FCoreStyle::GetDefaultFontStyle("Regular", Size);
}
TSharedRef<STextBlock> Text(const TCHAR* Value, int32 Size = 18, FLinearColor Tint = Paper)
{
    return SNew(STextBlock).Text(FText::FromString(Value)).Font(Font(Size)).ColorAndOpacity(Tint);
}
TSharedRef<SWidget> Rule()
{
    return SNew(SBox).HeightOverride(1)[SNew(SBorder).Padding(0)
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(Color(TEXT("34424B")))];
}

// Menu buttons own their brushes and animate on Slate time, which continues while the world is paused.
class SMenuButton final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMenuButton) : _Selected(false), _Primary(false), _Dangerous(false), _ReleaseTheme(false), _ContentPadding(24,14) {}
        SLATE_ATTRIBUTE(bool, Selected)
        SLATE_ARGUMENT(bool, Primary)
        SLATE_ARGUMENT(bool, Dangerous)
        SLATE_ARGUMENT(bool, ReleaseTheme)
        SLATE_ARGUMENT(FMargin, ContentPadding)
        SLATE_EVENT(FOnClicked, OnClicked)
        SLATE_DEFAULT_SLOT(FArguments, Content)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        Selected = Args._Selected;
        bPrimary = Args._Primary;
        bDangerous = Args._Dangerous;
        bReleaseTheme = Args._ReleaseTheme;
        Style.SetNormal(FSlateNoResource()).SetHovered(FSlateNoResource())
            .SetPressed(FSlateNoResource()).SetDisabled(FSlateNoResource())
            .SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
        ChildSlot[SAssignNew(Button, SButton).ButtonStyle(&Style).ContentPadding(Args._ContentPadding)
            .HAlign(HAlign_Fill).VAlign(VAlign_Center).OnClicked(Args._OnClicked)[Args._Content.Widget]];
    }
    virtual void Tick(const FGeometry& Geometry, double Time, float Delta) override
    {
        SCompoundWidget::Tick(Geometry, Time, Delta);
        const bool Active = IsEnabled() && (Button->IsHovered() || Button->HasKeyboardFocus());
        Hover = FMath::FInterpConstantTo(Hover, Active ? 1.f : 0.f, Delta, 1.f / .15f);
    }
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Clip,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& WidgetStyle, bool Enabled) const override
    {
        const bool Picked = Selected.Get();
        FLinearColor Base = bPrimary ? Color(TEXT("345366")) : (Picked ? Color(TEXT("263D4B")) : Color(TEXT("18232B")));
        if(bReleaseTheme) Base=bPrimary?Color(TEXT("E8AA46")):Picked?Color(TEXT("29323B")):Color(TEXT("17212B"));
        if (bDangerous) Base = Color(TEXT("342429"));
        Base = FMath::Lerp(Base, bDangerous ? Color(TEXT("58373A")) : (bReleaseTheme?Color(TEXT("394754")):Color(TEXT("405B6C"))), Hover * .65f);
        if (Button->IsPressed()) Base *= .72f;
        Base.A = Enabled && IsEnabled() ? .98f : .4f;
        const auto* Brush = FCoreStyle::Get().GetBrush("WhiteBrush");
        FSlateDrawElement::MakeBox(Elements, Layer, G.ToPaintGeometry(), Brush, ESlateDrawEffect::None, Base * WidgetStyle.GetColorAndOpacityTint());
        FLinearColor Edge = bDangerous ? Danger : bReleaseTheme?Color(TEXT("E8AA46")):Accent;
        Edge.A = (Picked || bPrimary || Button->HasKeyboardFocus()) ? .95f : .18f + Hover * .5f;
        FSlateDrawElement::MakeBox(Elements, Layer + 1, G.ToPaintGeometry(FVector2f(G.GetLocalSize().X, 1), FSlateLayoutTransform()),
            Brush, ESlateDrawEffect::None, Edge * WidgetStyle.GetColorAndOpacityTint());
        if (Picked || Button->HasKeyboardFocus())
            FSlateDrawElement::MakeBox(Elements, Layer + 1, G.ToPaintGeometry(FVector2f(3, G.GetLocalSize().Y), FSlateLayoutTransform()),
                Brush, ESlateDrawEffect::None, Edge * WidgetStyle.GetColorAndOpacityTint());
        return SCompoundWidget::OnPaint(Args, G, Clip, Elements, Layer + 2, WidgetStyle, Enabled);
    }
private:
    FButtonStyle Style;
    TSharedPtr<SButton> Button;
    TAttribute<bool> Selected;
    bool bPrimary = false, bDangerous = false, bReleaseTheme = false;
    float Hover = 0;
};

class SYunSummerPreview final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SYunSummerPreview) : _Pet(false) {}
        SLATE_ARGUMENT(TWeakObjectPtr<AHeliosAdventureHUD>, HUD)
        SLATE_ARGUMENT(bool, Pet)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        for(int32 I=0;I<3;++I)
        {
            Textures[I].Reset(LoadObject<UTexture2D>(nullptr,*HeliosYunSummer::PreviewPath(EHeliosYunSummerForm(I))));
            Brushes[I].SetResourceObject(Textures[I].Get());
            Brushes[I].ImageSize=FVector2D(900,1100);
            Brushes[I].DrawAs=Textures[I].IsValid()?ESlateBrushDrawType::Image:ESlateBrushDrawType::NoDrawType;
        }
        ChildSlot[SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
            [SNew(SBox).WidthOverride(900).HeightOverride(1100)
                [SNew(SImage).Image_Lambda([this,H=Args._HUD,Pet=Args._Pet]
                    {return &Brushes[H.IsValid()?int32(H->GetYunSummerForm(Pet)):2];})]]];
    }
private:
    TStrongObjectPtr<UTexture2D> Textures[3];
    FSlateBrush Brushes[3];
};

class SYunSummerForms final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SYunSummerForms) : _Pet(false) {}
        SLATE_ARGUMENT(TWeakObjectPtr<AHeliosAdventureHUD>, HUD)
        SLATE_ARGUMENT(bool, Pet)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        auto Row=SNew(SHorizontalBox);
        for(int32 I=0;I<3;++I)
        {
            const auto Form=EHeliosYunSummerForm(I);
            Textures[I].Reset(LoadObject<UTexture2D>(nullptr,*HeliosYunSummer::IconPath(Form)));
            Brushes[I].SetResourceObject(Textures[I].Get());
            Brushes[I].ImageSize=I==2?FVector2D(164,180):FVector2D(99,60);
            Brushes[I].DrawAs=Textures[I].IsValid()?ESlateBrushDrawType::Image:ESlateBrushDrawType::NoDrawType;
            Row->AddSlot().FillWidth(1).Padding(I?4:0,0,0,0)
                [SNew(SBox).HeightOverride(54)
                    [SNew(SMenuButton).ReleaseTheme(true).ContentPadding(FMargin(4,3))
                        .IsEnabled_Lambda([H=Args._HUD,Pet=Args._Pet]{return H.IsValid()&&(!Pet||H->GetSelectedPetCount()>0);})
                        .Selected_Lambda([H=Args._HUD,Pet=Args._Pet,Form]
                        {
                            return H.IsValid()&&H->GetYunSummerForm(Pet)==Form&&
                                (Pet?H->GetSelectedPetCount()>0&&H->GetSelectedPet()==EHeliosPetKind::YunSummerMegatron:
                                     H->GetPendingWeaponId()==TEXT("cfm_yun_summer_megatron"));
                        })
                        .OnClicked_Lambda([H=Args._HUD,Pet=Args._Pet,Form]
                            {if(H.IsValid())H->SelectYunSummerForm(Form,Pet);return FReply::Handled();})
                        [SNew(SVerticalBox)
                            +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                                [SNew(SBox).WidthOverride(48).HeightOverride(28)
                                    [SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
                                        [SNew(SBox).WidthOverride(Brushes[I].ImageSize.X).HeightOverride(Brushes[I].ImageSize.Y)
                                            [SNew(SImage).Image(&Brushes[I])]]]]
                            +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,2,0,0)
                                [Text(HeliosYunSummer::FormLabel(Form),14)]]]];
        }
        ChildSlot[Row];
    }
private:
    TStrongObjectPtr<UTexture2D> Textures[3];
    FSlateBrush Brushes[3];
};

class SPetChoice final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPetChoice) {} SLATE_ARGUMENT(TWeakObjectPtr<AHeliosAdventureHUD>, HUD) SLATE_ARGUMENT(EHeliosPetKind, Kind) SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        if(Args._Kind==EHeliosPetKind::YunSummerMegatron)
        {
            ChildSlot[SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[SNew(SMenuButton).ReleaseTheme(true)
                    .IsEnabled_Lambda([H=Args._HUD]{return H.IsValid()&&H->GetSelectedPetCount()>0;})
                    .Selected_Lambda([H=Args._HUD]{return H.IsValid()&&H->GetSelectedPetCount()>0&&H->GetSelectedPet()==EHeliosPetKind::YunSummerMegatron;})
                    .OnClicked_Lambda([H=Args._HUD]{if(H.IsValid())H->SelectPet(EHeliosPetKind::YunSummerMegatron);return FReply::Handled();})
                    [SNew(SHorizontalBox)
                        +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(64).HeightOverride(82)[SNew(SYunSummerPreview).HUD(Args._HUD).Pet(true)]]
                        +SHorizontalBox::Slot().FillWidth(1).Padding(20,0).VAlign(VAlign_Center)[SNew(SVerticalBox)
                            +SVerticalBox::Slot().AutoHeight()[Text(TEXT("云悠悠·沐夏微醺"),20)]
                            +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)[SNew(STextBlock).Font(Font(14)).ColorAndOpacity(Muted)
                                .Text_Lambda([H=Args._HUD]{return FText::FromString(FString::Printf(TEXT("%s / 威震天"),HeliosYunSummer::FormLabel(H.IsValid()?H->GetYunSummerForm(true):EHeliosYunSummerForm::SP)));})]]
                        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Font(Font(14)).ColorAndOpacity(Accent)
                            .Text_Lambda([H=Args._HUD]{return FText::FromString(H.IsValid()&&H->GetSelectedPetCount()>0&&H->GetSelectedPet()==EHeliosPetKind::YunSummerMegatron?TEXT("已选择"):TEXT("选择伙伴"));})]]]
                +SVerticalBox::Slot().AutoHeight().Padding(0,6,0,0)[SNew(SYunSummerForms).HUD(Args._HUD).Pet(true)]];
            return;
        }
        const bool SSX=Args._Kind==EHeliosPetKind::SunShangXiang;
        const bool ZXK=Args._Kind==EHeliosPetKind::ZhongXiaoKui;
        const bool Chan=Args._Kind==EHeliosPetKind::ChanZeus;
        const bool Kui=Args._Kind==EHeliosPetKind::WangZheKuiThunder;
        const bool BLS=Args._Kind==EHeliosPetKind::BuLianShiP90;
        const bool Butterfly=Args._Kind==EHeliosPetKind::UltimatumGR;
        Preview.Reset(LoadObject<UTexture2D>(nullptr,ZXK?TEXT("/Game/Helios/CFMZhongXiaoKui/Textures/T_ZXK_TP_Preview.T_ZXK_TP_Preview"):SSX?TEXT("/Game/Helios/CFMSunShangXiang/Textures/T_SSX_TP_Preview.T_SSX_TP_Preview"):Butterfly?TEXT("/Game/Helios/Pets/UltimatumGR/T_UltimatumPreview.T_UltimatumPreview"):TEXT("/Game/Helios/Pets/UltimatumGR/T_DogPreview.T_DogPreview")));
        if(ZXK||SSX)if(auto* T=LoadObject<UTexture2D>(nullptr,ZXK?TEXT("/Game/Helios/UI/Menu/T_Menu_Hunter_zxk"):TEXT("/Game/Helios/UI/Menu/T_Menu_Hunter_ssx")))Preview.Reset(T);
        if(Kui)Preview.Reset(LoadObject<UTexture2D>(nullptr,TEXT("/Game/Helios/CFMWangZheKuiThunder/Textures/T_WangZheKui_TP_Preview")));
        if(Chan)Preview.Reset(LoadObject<UTexture2D>(nullptr,TEXT("/Game/Helios/CFMChanZeus/Textures/T_Chan_TP_Preview")));
        if(BLS)Preview.Reset(LoadObject<UTexture2D>(nullptr,TEXT("/Game/Helios/CFMBuLianShiP90/Textures/T_BuLianShi_TP_Preview")));
        Brush.SetResourceObject(Preview.Get());Brush.ImageSize=FVector2D(64,82);Brush.DrawAs=ESlateBrushDrawType::Image;
        ChildSlot[SNew(SMenuButton).ReleaseTheme(true).IsEnabled_Lambda([H=Args._HUD]{return H.IsValid()&&H->GetSelectedPetCount()>0;}).Selected_Lambda([H=Args._HUD,K=Args._Kind]{return H.IsValid()&&H->GetSelectedPetCount()>0&&H->GetSelectedPet()==K;})
            .OnClicked_Lambda([H=Args._HUD,K=Args._Kind]{if(H.IsValid())H->SelectPet(K);return FReply::Handled();})
            [SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(64).HeightOverride(82)[SNew(SImage).Image(&Brush)]]
                +SHorizontalBox::Slot().FillWidth(1).Padding(20,0).VAlign(VAlign_Center)[SNew(SVerticalBox)
                    +SVerticalBox::Slot().AutoHeight()[Text(Kui?TEXT("王者葵·保卫者 / 王者之霆"):BLS?TEXT("步练师·灵魂舞者 / P90双脊龙伙伴"):Chan?TEXT("蝉·潜伏者 / 宙斯"):ZXK?TEXT("钟小葵"):SSX?TEXT("孙尚香"):Butterfly?TEXT("蝴蝶·雷神助战"):TEXT("刀盾狗"),20)]
                    +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)[Text(Kui?TEXT("王者之霆霰弹 · 原保卫者语音"):BLS?TEXT("P90双脊龙 · 原SP随机语音"):Chan?TEXT("宙斯散弹 · 原角色随机语音"):ZXK?TEXT("A 形态 · 近战助战"):SSX?TEXT("双刃近战 · 终极技能"):Butterfly?TEXT("雷神步枪 · 连射助战"):TEXT("刀盾近战 · 正面防御"),14,Muted)]]
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Font(Font(14)).ColorAndOpacity(Accent)
                    .Text_Lambda([H=Args._HUD,K=Args._Kind]{return FText::FromString(H.IsValid()&&H->GetSelectedPet()==K?TEXT("已选择"):TEXT("选择伙伴"));})]]];
    }
private:
    TStrongObjectPtr<UTexture2D> Preview;
    FSlateBrush Brush;
};

class SMenuBackdrop final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SMenuBackdrop) {} SLATE_ARGUMENT(TWeakObjectPtr<AHeliosAdventureHUD>, HUD) SLATE_END_ARGS()
    void Construct(const FArguments& Args) { HUD = Args._HUD; }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1920, 1080); }
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& E,
        int32 L, const FWidgetStyle&, bool) const override
    {
        const bool Weapon = HUD.IsValid() && HUD->GetMenuState() == EHeliosAdventureMenu::WeaponSelect;
        const bool Confirm = HUD.IsValid() && HUD->GetMenuState() == EHeliosAdventureMenu::RestartConfirm;
        const FVector2f Size = G.GetLocalSize();
        // Full viewport background is separate from the fitted content, including on ultrawide displays.
        FLinearColor Left = Color(TEXT("0D141A")), Middle = Color(TEXT("1A2832")), Right = Color(TEXT("10181F"));
        if(Weapon){Left=Color(TEXT("0C1218"));Middle=Color(TEXT("17212B"));Right=Color(TEXT("0C1218"));}
        Left.A = Weapon ? 1.f : (Confirm ? .94f : .98f);
        Middle.A = Weapon ? 1.f : (Confirm ? .94f : .82f);
        Right.A = Weapon ? 1.f : (Confirm ? .94f : .64f);
        TArray<FSlateGradientStop> Stops;
        Stops.Emplace(FVector2f(0, 0), Left);
        Stops.Emplace(FVector2f(Size.X * .48f, 0), Middle);
        Stops.Emplace(FVector2f(Size.X, 0), Right);
        FSlateDrawElement::MakeGradient(E, L, G.ToPaintGeometry(), Stops, Orient_Vertical);
        return L + 1;
    }
private:
    TWeakObjectPtr<AHeliosAdventureHUD> HUD;
};

class SMenu final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMenu) {} SLATE_ARGUMENT(TWeakObjectPtr<AHeliosAdventureHUD>, HUD) SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        HUD = Args._HUD;
        EquipmentOpen=FParse::Param(FCommandLine::Get(),TEXT("HeliosEquipmentPageQA"));
#if WITH_DEV_AUTOMATION_TESTS
        HeliosMenuQANavigate=[Weak=TWeakPtr<SMenu>(SharedThis(this))](bool Equipment,bool Pets){if(auto M=Weak.Pin()){M->EquipmentOpen=Equipment;M->BrowsePets=Pets;}};
#endif
        BossPortrait.DrawAs = ESlateBrushDrawType::NoDrawType;
        if (UTexture2D* Portrait = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Helios/UI/Menu/T_Menu_ikun_basketball.T_Menu_ikun_basketball")))
        {
            Textures.Emplace(Portrait);
            BossPortrait.SetResourceObject(Portrait);
            BossPortrait.DrawAs = ESlateBrushDrawType::Image;
            BossPortrait.ImageSize = FVector2D(Portrait->GetSizeX(), Portrait->GetSizeY());
        }
        for (auto* Definition : UHeliosWeaponRegistry::GetAvailableWeapons())
        {
            if (!Definition) continue;
            const FString Id = Definition->WeaponId.ToString();
            const FString Path = FString::Printf(TEXT("/Game/Helios/UI/Menu/T_Menu_%s.T_Menu_%s"), *Id, *Id);
            UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *Path);
            const bool bMenuArt = Texture != nullptr;
            if (!Texture) Texture = Definition->PreviewTexture.LoadSynchronous();
            auto Brush = MakeShared<FSlateBrush>();
            Brush->DrawAs = ESlateBrushDrawType::Image;
            if (Texture)
            {
                Textures.Emplace(Texture);
                Brush->SetResourceObject(Texture);
                // Texture compilation can still expose its placeholder dimensions at construction.
                // Authored menu images have a fixed 2:1 artboard independent of streaming state.
                Brush->ImageSize = bMenuArt ? FVector2D(2048, 1024) : FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
            }
            else Brush->DrawAs = ESlateBrushDrawType::NoDrawType;
            if(Id.StartsWith(TEXT("cfm_"))&&Id!=TEXT("cfm_heilonghun"))Brush->DrawAs=ESlateBrushDrawType::NoDrawType;
            Previews.Add(Definition->WeaponId, Brush);
            Definitions.Emplace(Definition);
        }
        for(auto* D:FHeliosCharacterRegistry::GetAvailableCharacters())
        {
            Characters.Emplace(D);
            if(auto* T=D->PreviewTexture.LoadSynchronous())
            {Textures.Emplace(T);auto B=MakeShared<FSlateBrush>();B->SetResourceObject(T);B->ImageSize=FVector2D(T->GetSizeX(),T->GetSizeY());B->DrawAs=ESlateBrushDrawType::Image;CharacterPreviews.Add(D->CharacterId,B);}
            for(const auto& Form:D->Forms)if(!Form.VariantId.IsNone())
                if(auto* T=Form.PreviewTexture.LoadSynchronous())
                {Textures.Emplace(T);auto B=MakeShared<FSlateBrush>();B->SetResourceObject(T);B->ImageSize=FVector2D(T->GetSizeX(),T->GetSizeY());B->DrawAs=ESlateBrushDrawType::Image;FormPreviews.Add(FormPreviewKey(D->CharacterId,Form.VariantId),B);}
        }
        if(auto* T=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Helios/Pets/UltimatumGR/T_DogPreview")))
        {Textures.Emplace(T);auto B=MakeShared<FSlateBrush>();B->SetResourceObject(T);B->ImageSize=FVector2D(T->GetSizeX(),T->GetSizeY());B->DrawAs=ESlateBrushDrawType::Image;CharacterPreviews.Add(TEXT("sword_dog"),B);}
        SetVisibility(TAttribute<EVisibility>::CreateLambda([this]
        {
            if ((State() == EHeliosAdventureMenu::None || State() == EHeliosAdventureMenu::TownMap))
            {
                PreviousState = EHeliosAdventureMenu::None;
                return EVisibility::Collapsed;
            }
            return EVisibility::SelfHitTestInvisible;
        }));
        ChildSlot[SNew(SOverlay)
            + SOverlay::Slot()[SNew(SMenuBackdrop).HUD(HUD).Visibility(EVisibility::HitTestInvisible)]
            + SOverlay::Slot()[SNew(SScaleBox).Stretch(EStretch::ScaleToFit).HAlign(HAlign_Center).VAlign(VAlign_Center)
                [SNew(SBox).WidthOverride(1920).HeightOverride(1080)
                    [SAssignNew(Content, SOverlay)
                        + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(0, 100, 115, 100)
                            [SNew(SBox).WidthOverride(490).HeightOverride(740)
                                .Visibility_Lambda([this]{ return !AudioOpen && (State()==EHeliosAdventureMenu::Pause || State()==EHeliosAdventureMenu::Death) ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })[BossArt()]]
                        + SOverlay::Slot().Padding(80, 58)[WeaponPage()]
                        + SOverlay::Slot().Padding(100, 72)[PausePage()]
                        + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[ConfirmPage()]
                        + SOverlay::Slot().Padding(100, 72)[DeathPage()]
                        + SOverlay::Slot().Padding(120,72)[AudioPage()]]]]];
    }
    virtual void Tick(const FGeometry& G, double Time, float Delta) override
    {
        SCompoundWidget::Tick(G, Time, Delta);
        if (State() != PreviousState)
        {
            PreviousState = State();
            if(State()==EHeliosAdventureMenu::None)AudioOpen=false;
            Fade = 0;
        }
        Fade = FMath::Min(1.f, Fade + Delta / .15f);
        Content->SetRenderOpacity(Fade);
        if(EquipmentOpen!=LastEquipmentOpen){LastEquipmentOpen=EquipmentOpen;LastVisibleWeapon=NAME_None;}
        if (HUD.IsValid() && WeaponScroll.IsValid() && LastVisibleWeapon != HUD->GetPendingWeaponId())
        {
            LastVisibleWeapon = HUD->GetPendingWeaponId();
            if (const auto* Row = WeaponRows.Find(LastVisibleWeapon))
                WeaponScroll->ScrollDescendantIntoView(*Row, false, EDescendantScrollDestination::IntoView);
        }
    }
virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnKeyDown(const FGeometry& G,const FKeyEvent& E) override
    {
        if(E.GetKey()==EKeys::Escape&&AudioOpen){AudioOpen=false;return FReply::Handled();}
        if(E.GetKey()==EKeys::Escape&&State()==EHeliosAdventureMenu::WeaponSelect&&EquipmentOpen){EquipmentOpen=false;return FReply::Handled();}
        return SCompoundWidget::OnKeyDown(G,E);
    }
private:
    EHeliosAdventureMenu State() const { return HUD.IsValid() ? HUD->GetMenuState() : EHeliosAdventureMenu::None; }
    AHeliosAdventureDirector* Director() const { return HUD.IsValid() ? AHeliosAdventureDirector::Find(HUD->GetWorld()) : nullptr; }
    const UHeliosWeaponDefinition* Selected() const
    { return HUD.IsValid() ? UHeliosWeaponRegistry::FindWeapon(HUD->GetPendingWeaponId()) : nullptr; }
    TAttribute<EVisibility> Show(EHeliosAdventureMenu S)
    { return TAttribute<EVisibility>::CreateLambda([this, S]{ return !AudioOpen && State() == S ? EVisibility::Visible : EVisibility::Collapsed; }); }
    TSharedRef<SWidget> LabelButton(const TCHAR* Label, TFunction<void()> Action, bool Primary = false, bool Dangerous = false)
    {
        return SNew(SBox).HeightOverride(76)[SNew(SMenuButton).Primary(Primary).Dangerous(Dangerous)
            .OnClicked_Lambda([Action = MoveTemp(Action)] { Action(); return FReply::Handled(); })
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Text(Label, 20, Dangerous ? Danger : Paper)]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Text(Primary ? TEXT("→") : TEXT("·"), 22, Muted)]]];
    }
    UHeliosAudioSettingsSubsystem* Audio() const { return HUD.IsValid()?UHeliosAudioSettingsSubsystem::Get(HUD.Get()):nullptr; }
    TSharedRef<SWidget> SoundButton(bool Release=false)
    {
        return SNew(SBox).HeightOverride(76)[SNew(SMenuButton).ReleaseTheme(Release)
            .OnClicked_Lambda([this]{AudioOpen=true;FSlateApplication::Get().SetKeyboardFocus(SharedThis(this));return FReply::Handled();})
            [Text(TEXT("声音设置"),18)]];
    }
    TSharedRef<SWidget> AudioPage()
    {
        auto Rows=SNew(SVerticalBox);
        for(int32 I=0;I<int32(EHeliosAudioBus::Count);++I)
        {
            const auto B=EHeliosAudioBus(I);
            Rows->AddSlot().AutoHeight().Padding(0,0,0,6)[SNew(SBorder).Padding(22,8).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Color(I?TEXT("17212B"):TEXT("22313D")))
                [SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(260)[Text(UHeliosAudioSettingsSubsystem::Label(B),22)]]
                    +SHorizontalBox::Slot().AutoWidth().Padding(0,0,34,0)[SNew(SBox).WidthOverride(220).HeightOverride(48)[SNew(SMenuButton).ReleaseTheme(true).ContentPadding(FMargin(8,8))
                        .OnClicked_Lambda([this,B]{if(auto* A=Audio())A->SetEnabled(B,!A->Enabled(B));return FReply::Handled();})
                        [SNew(STextBlock).Font(Font(18)).ColorAndOpacity(Accent).Text_Lambda([this,B]{return FText::FromString(FString(UHeliosAudioSettingsSubsystem::Label(B))+(Audio()&&Audio()->Enabled(B)?TEXT(" · 开"):TEXT(" · 关")));})]]]
                    +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(SSlider).MinValue(0).MaxValue(1).StepSize(.01f)
                        .Value_Lambda([this,B]{return Audio()?Audio()->Volume(B):1.f;})
                        .OnValueChanged_Lambda([this,B](float V){if(auto* A=Audio())A->SetVolume(B,V);})
                        .SliderBarColor(Muted).SliderHandleColor(Accent)]
                    +SHorizontalBox::Slot().AutoWidth().Padding(28,0,0,0).VAlign(VAlign_Center)[SNew(SBox).WidthOverride(80)[SNew(STextBlock).Font(NumberFont(22)).ColorAndOpacity(Paper)
                        .Text_Lambda([this,B]{return FText::FromString(FString::Printf(TEXT("%d%%"),FMath::RoundToInt((Audio()?Audio()->Volume(B):1.f)*100)));})]]]];
        }
        return SNew(SVerticalBox).Visibility_Lambda([this]{return AudioOpen?EVisibility::Visible:EVisibility::Collapsed;})
            +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(180)[ReleaseButton(TEXT("←  返回"),[this]{if(auto* A=Audio())A->FlushSettings();AudioOpen=false;})]]
                +SHorizontalBox::Slot().FillWidth(1).Padding(28,0).VAlign(VAlign_Center)[Text(TEXT("声音设置"),36,Paper)]
                +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(220)[ReleaseButton(TEXT("恢复默认"),[this]{if(auto* A=Audio())A->ResetDefaults();})]]]
            +SVerticalBox::Slot().AutoHeight().Padding(0,24,0,22)[Text(TEXT("独立开关与音量 · 立即生效 · 自动保存"),18,Muted)]
            +SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[Rows]]
            +SVerticalBox::Slot().AutoHeight().Padding(0,20,0,12)[Text(TEXT("伙伴：射击、动作与语音   /   角色语音：玩家角色台词   /   玩家受击：非语音受击音效"),18,Muted)]
            +SVerticalBox::Slot().AutoHeight()[Text(TEXT("环境与敌人包含脚步、拾取和环境声。关闭声音会保留滑块音量；总音量同时作用于各项。"),18,Muted)];
    }
    TSharedRef<SWidget> Brand()
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1)[Text(TEXT("赫利俄斯前哨站"), 18, Muted)]
            + SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text(FText::FromString(TEXT("IKUN  /  生存行动"))).Font(Font(15)).ColorAndOpacity(Muted)];
    }
    TSharedRef<SWidget> WeaponList()
    {
        auto List = SNew(SVerticalBox);
        int32 Index = 0;
        for (const auto& Definition : Definitions)
        {
            const FName Id = Definition->WeaponId;
            const FText Name = Definition->DisplayName;
            const FString Number = FString::Printf(TEXT("%02d"), ++Index);
            TSharedPtr<SBox> Row;
            List->AddSlot().AutoHeight().Padding(0, 0, 0, 14)
                [SAssignNew(Row, SBox).HeightOverride(110)[SNew(SMenuButton)
                    .Selected_Lambda([this, Id]{ return HUD.IsValid() && HUD->GetPendingWeaponId() == Id; })
                    .OnClicked_Lambda([this, Id]{ if (HUD.IsValid()) HUD->SelectWeapon(Id); return FReply::Handled(); })
                    [SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 2, 24, 0)
                            [SNew(STextBlock).Text(FText::FromString(Number)).Font(NumberFont(16)).ColorAndOpacity(Muted)]
                        + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                            [SNew(SVerticalBox)
                                + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Name).Font(Font(24)).ColorAndOpacity(Paper)]
                                + SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
                                    [SNew(STextBlock).Text_Lambda([this, Id]
                                        { return FText::FromString(HUD.IsValid() && HUD->GetPendingWeaponId() == Id ? TEXT("已装备") : TEXT("选择装备")); })
                                        .Font(Font(14)).ColorAndOpacity_Lambda([this, Id]
                                            { return HUD.IsValid() && HUD->GetPendingWeaponId() == Id ? Accent : Muted; })]]]]];
            WeaponRows.Add(Id, Row);
        }
        return List;
    }
    TSharedRef<SWidget> WeaponArt()
    {
        // A stack preserves each image's native aspect ratio, including the fallback preview.
        auto Images = SNew(SOverlay);
        for (const auto& Pair : Previews)
        {
            const FName Id = Pair.Key;
            const auto Brush = Pair.Value;
            Images->AddSlot()[SNew(SScaleBox).Stretch(EStretch::ScaleToFit).HAlign(HAlign_Center).VAlign(VAlign_Center)
                .Visibility_Lambda([this, Id]{ return HUD.IsValid() && HUD->GetPendingWeaponId() == Id ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
                [SNew(SBox).WidthOverride(Brush->ImageSize.X).HeightOverride(Brush->ImageSize.Y)[SNew(SImage).Image(Brush.Get())]]];
        }
        return Images;
    }
    TSharedRef<SWidget> WeaponMetric(const TCHAR* Name, TFunction<FText()> Value)
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()[Text(Name, 15, Muted)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
                [SNew(STextBlock).Text_Lambda(MoveTemp(Value)).Font(Font(26)).ColorAndOpacity(Paper)];
    }
    TSharedRef<SWidget> BossArt()
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                [Text(TEXT("IKUN  /  巨星登场"), 17, Color(TEXT("E5C780")))]
            + SVerticalBox::Slot().FillHeight(1).Padding(0, 8)
                [SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
                    [SNew(SBox).WidthOverride(BossPortrait.ImageSize.X).HeightOverride(BossPortrait.ImageSize.Y)[SNew(SImage).Image(&BossPortrait)]]]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                [Text(TEXT("练习两年半 · 气场两米五"), 14, Muted)];
    }
    TSharedRef<SWidget> MapCard(FName Id, const TCHAR* Title, const TCHAR* Detail)
    {
        return SNew(SMenuButton).ReleaseTheme(true).Selected_Lambda([this,Id]{return HUD.IsValid()&&HUD->GetPendingMapId()==Id;})
            .OnClicked_Lambda([this,Id]{if(HUD.IsValid())HUD->SelectMap(Id);return FReply::Handled();})
            [SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[Text(Title,22)]
                +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)[Text(Detail,15,Muted)]];
    }
    TSharedRef<SWidget> MapCards()
    {
        auto Cards=SNew(SHorizontalBox);
        for(const auto& Map:FHeliosMapRegistry::All())
        {
            const FString Detail=Map.GameplayRules==TEXT("bio_infection")?TEXT("离线生化 · 感染 / 猎手变身 / 补给"):Map.UsesTownSystems()?FString::Printf(TEXT("持续生存 · %d 个街区 · 探索自动保存"),Map.DistrictCount):FString::Printf(TEXT("经典生存 · %d 个分区 · 波次挑战"),Map.DistrictCount);
            Cards->AddSlot().FillWidth(1).Padding(0,0,12,0)[MapCard(Map.AssetMapName(),*Map.DisplayName.ToString(),*Detail)];
        }
        return Cards;
    }

    TSharedRef<SWidget> ReleaseButton(const TCHAR* Label,TFunction<void()> Action,bool Primary=false)
    {
        return SNew(SBox).HeightOverride(76)[SNew(SMenuButton).ReleaseTheme(true).Primary(Primary)
            .OnClicked_Lambda([Action=MoveTemp(Action)]{Action();return FReply::Handled();})
            [Text(Label,22,Primary?Color(TEXT("0C1218")):Color(TEXT("EEF2F5")))]];
    }
    TSharedRef<SWidget> SelectionSummary()
    {
        return SNew(STextBlock).Font(Font(20)).ColorAndOpacity(Color(TEXT("A8B3BE"))).Text_Lambda([this]
        {return FText::FromString(FString::Printf(TEXT("%s / %s  ·  %d 位伙伴"),HUD.IsValid()?*HUD->GetSelectedCharacterName().ToString():TEXT("选择人物"),HUD.IsValid()?*HUD->GetSelectedEquipmentName().ToString():TEXT("选择主武器"),HUD.IsValid()?HUD->GetSelectedPetCount():0));}).AutoWrapText(true);
    }
    FHeliosLoadout ViewedLoadout()const
    {return HUD.IsValid()?(BrowsePets?HUD->GetPetLoadout(HUD->GetActivePetSlot()):HUD->GetPendingLoadout()):FHeliosLoadout::MakeDefault();}
    static FName FormPreviewKey(FName Character,FName Variant)
    {return FName(*(Character.ToString()+TEXT("__")+Variant.ToString()));}
    FText ViewedCharacterName()const
    {
        const auto L=ViewedLoadout();
        if(L.CharacterId==TEXT("sword_dog"))return FText::FromString(TEXT("刀盾狗"));
        if(const auto* D=FHeliosCharacterRegistry::FindCharacter(L.CharacterId))
        {
            const auto* Form=D->FindForm(HUD.IsValid()?HUD->GetSelectedCharacterVariant(BrowsePets):L.CharacterVariantId);
            return Form&&D->Forms.Num()>1?FText::FromString(D->DisplayName.ToString()+TEXT(" · ")+Form->DisplayName.ToString()):D->DisplayName;
        }
        return FText::GetEmpty();
    }
    FText ViewedWeaponName()const
    {
        const auto L=ViewedLoadout();
        if(L.CharacterId==TEXT("sword_dog"))return FText::FromString(TEXT("固定刀盾"));
        if(L.PrimaryWeaponId==TEXT("cfm_yun_summer_megatron"))return FText::FromString(TEXT("威震天"));
        if(const auto* D=UHeliosWeaponRegistry::FindWeapon(L.PrimaryWeaponId))return D->DisplayName;
        return FText::GetEmpty();
    }
    TSharedRef<SWidget> MainPage()
    {
        return SNew(SVerticalBox).Visibility_Lambda([this]{return EquipmentOpen?EVisibility::Collapsed:EVisibility::Visible;})
            +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1)[Text(TEXT("全民制作人"),28,Color(TEXT("EEF2F5")))]
                +SHorizontalBox::Slot().AutoWidth()[Text(TEXT("生存行动 / 作战大厅"),18,Muted)]]
            +SVerticalBox::Slot().FillHeight(1).Padding(0,48,0,36)[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(530)[SNew(SVerticalBox)
                    +SVerticalBox::Slot().AutoHeight().Padding(0,16,0,20)[Text(TEXT("整装出发"),62,Color(TEXT("EEF2F5")))]
                    +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,48)[Text(TEXT("选好装备，与伙伴并肩行动。"),22,Muted)]
                    +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,16)[ReleaseButton(TEXT("人物、武器与伙伴  →"),[this]{EquipmentOpen=true;FSlateApplication::Get().SetKeyboardFocus(SharedThis(this));})]
                    +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,16)[SoundButton(true)]
                    +SVerticalBox::Slot().AutoHeight()[ReleaseButton(TEXT("退出游戏"),[this]{if(HUD.IsValid())HUD->QuitToDesktop();})]
                    +SVerticalBox::Slot().FillHeight(1)
                    +SVerticalBox::Slot().AutoHeight()[Text(TEXT("当前出战"),18,Color(TEXT("E8AA46")))]
                    +SVerticalBox::Slot().AutoHeight().Padding(0,12,0,0)[SelectionSummary()]]]
                +SHorizontalBox::Slot().FillWidth(1).Padding(56,0,0,0)[SNew(SOverlay)
                    +SOverlay::Slot()[HeroArt()]
                    +SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom)[Text(TEXT("人物与主武器自由搭配，伙伴独立配装"),18,Muted)]]]
            +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,24)[MapCards()]
            +SVerticalBox::Slot().AutoHeight()[ActionFooter()];
    }
    TSharedRef<SWidget> ActionFooter()
    {
        return SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Text_Lambda([this]{return HUD.IsValid()?HUD->GetMenuDetails():FText::GetEmpty();}).Font(Font(18)).ColorAndOpacity(Muted).AutoWrapText(true)]
            +SHorizontalBox::Slot().AutoWidth().Padding(24,0)[SNew(SBox).WidthOverride(210).Visibility_Lambda([this]{return HUD.IsValid()&&HUD->IsPendingTownMap()?EVisibility::Visible:EVisibility::Collapsed;})[ReleaseButton(TEXT("新开探索"),[this]{if(HUD.IsValid())HUD->RequestNewTown();})]]
            +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(310).IsEnabled_Lambda([this]{return Selected()!=nullptr;})
                [SNew(SMenuButton).ReleaseTheme(true).Primary(true).OnClicked_Lambda([this]{if(HUD.IsValid())HUD->StartSelectedRun();return FReply::Handled();})
                    [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(HUD.IsValid()&&HUD->IsPendingTownMap()?TEXT("继续探索  →"):TEXT("开始行动  →"));}).Font(Font(24)).ColorAndOpacity(Color(TEXT("0C1218")))]]];
    }
    TSharedRef<SWidget> CharacterGrid(bool ForPet=false)
    {
        auto Grid=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(16,16));
        const auto AddCard=[this,Grid,ForPet](FName Id,FText Name,bool Ready)
        {
            auto Brush=CharacterPreviews.FindRef(Id);
            auto Art=SNew(SBox).HeightOverride(150);
            if(Brush)Art->SetContent(SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[SNew(SImage).Image(Brush.Get())]);
            Grid->AddSlot()[SNew(SBox).WidthOverride(376).HeightOverride(265)
                [SNew(SMenuButton).ReleaseTheme(true)
                    .IsEnabled_Lambda([this,ForPet,Ready]{return Ready&&HUD.IsValid()&&(!ForPet||HUD->GetSelectedPetCount()>0);})
                    .Selected_Lambda([this,Id,ForPet]{return HUD.IsValid()&&(ForPet?HUD->GetPetLoadout(HUD->GetActivePetSlot()).CharacterId:HUD->GetPendingCharacterId())==Id;})
                    .OnClicked_Lambda([this,Id,ForPet]{if(HUD.IsValid()){if(ForPet)HUD->SelectPetCharacter(Id);else HUD->SelectCharacter(Id);}return FReply::Handled();})
                    [SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight()[Art]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,10,0,0)[SNew(STextBlock).Text(Name).Font(Font(20)).ColorAndOpacity(Paper)]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)[Text(Ready?TEXT("选择人物"):TEXT("手臂修复中 · 已预留接口"),16,Ready?Accent:Muted)]]]];
        };
        for(const auto& D:Characters)if(ForPet?D->bCompanionSelectable:D->bPlayerSelectable)AddCard(D->CharacterId,D->DisplayName,D->bPresentationReady);
        if(ForPet)AddCard(TEXT("sword_dog"),FText::FromString(TEXT("刀盾狗 · 固定刀盾")),true);
        return Grid;
    }
    TSharedRef<SWidget> WeaponGrid(bool ForPet=false)
    {
        auto Grid=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(16,16));
        for(const auto& D:Definitions)
        {
            const FName Id=D->WeaponId;
            if(ForPet&&(Id==TEXT("mech")||Id==TEXT("fighter")))continue;
            const FText Name=Id==TEXT("cfm_yun_summer_megatron")?FText::FromString(TEXT("威震天")):D->DisplayName;
            auto Brush=Previews.FindRef(Id);
            auto Art=SNew(SBox).HeightOverride(110);
            if(Brush&&Brush->DrawAs!=ESlateBrushDrawType::NoDrawType)Art->SetContent(SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[SNew(SImage).Image(Brush.Get())]);
            else Art->SetContent(SNew(STextBlock).Text(Name).Font(Font(26)).ColorAndOpacity(Accent).AutoWrapText(true));
            TSharedPtr<SBox> Row;
            Grid->AddSlot()[SAssignNew(Row,SBox).WidthOverride(376).HeightOverride(250)
                [SNew(SMenuButton).ReleaseTheme(true)
                    .IsEnabled_Lambda([this,ForPet]{const auto L=ViewedLoadout();return HUD.IsValid()&&(!ForPet||(HUD->GetSelectedPetCount()>0&&L.CharacterId!=TEXT("sword_dog")));})
                    .Selected_Lambda([this,Id,ForPet]{return HUD.IsValid()&&(ForPet?HUD->GetPetLoadout(HUD->GetActivePetSlot()).PrimaryWeaponId:HUD->GetPendingWeaponId())==Id;})
                    .OnClicked_Lambda([this,Id,ForPet]{if(HUD.IsValid()){if(ForPet)HUD->SelectPetWeapon(Id);else HUD->SelectWeapon(Id);}return FReply::Handled();})
                    [SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight()[Art]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)[SNew(STextBlock).Text(Name).Font(Font(20)).ColorAndOpacity(Paper).AutoWrapText(true)]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)[Text(TEXT("装备到主武器槽"),16,Muted)]]]];
            if(!ForPet)WeaponRows.Add(Id,Row);
        }
        return Grid;
    }
    TSharedRef<SWidget> PetRosterControls()
    {
        auto Counts=SNew(SHorizontalBox);
        for(int32 N=0;N<=4;++N)Counts->AddSlot().FillWidth(1).Padding(0,0,8,0)
            [SNew(SMenuButton).ReleaseTheme(true).Selected_Lambda([this,N]{return HUD.IsValid()&&HUD->GetSelectedPetCount()==N;})
                .OnClicked_Lambda([this,N]{if(HUD.IsValid())HUD->SelectPetCount(N);return FReply::Handled();})[Text(*FString::Printf(TEXT("%d 位"),N),18)]];
        auto Slots=SNew(SHorizontalBox);
        for(int32 I=0;I<4;++I)Slots->AddSlot().FillWidth(1).Padding(0,0,8,0)
            [SNew(SBox).Visibility_Lambda([this,I]{return HUD.IsValid()&&I<HUD->GetSelectedPetCount()?EVisibility::Visible:EVisibility::Collapsed;})
                [SNew(SBorder).Padding(8).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Color(TEXT("17212B")))
                    [SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight()[SNew(SMenuButton).ReleaseTheme(true).Selected_Lambda([this,I]{return HUD.IsValid()&&HUD->GetActivePetSlot()==I;})
                            .OnClicked_Lambda([this,I]{if(HUD.IsValid())HUD->SelectPetSlot(I);return FReply::Handled();})[Text(*FString::Printf(TEXT("伙伴 %d"),I+1),17)]]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,6)[SNew(STextBlock).Font(Font(15)).ColorAndOpacity(Paper).AutoWrapText(true).Text_Lambda([this,I]
                            {const auto L=HUD.IsValid()?HUD->GetPetLoadout(I):FHeliosLoadout::MakeDefault();const auto* D=FHeliosCharacterRegistry::FindCharacter(L.CharacterId);return L.CharacterId==TEXT("sword_dog")?FText::FromString(TEXT("刀盾狗")):D?D->DisplayName:FText::FromString(TEXT("人物"));})]
                        +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Font(14)).ColorAndOpacity(Accent).AutoWrapText(true).Text_Lambda([this,I]
                            {const auto L=HUD.IsValid()?HUD->GetPetLoadout(I):FHeliosLoadout::MakeDefault();const auto* W=UHeliosWeaponRegistry::FindWeapon(L.PrimaryWeaponId);return FText::FromString(L.CharacterId==TEXT("sword_dog")?TEXT("固定刀盾"):FString(TEXT("主武器 · "))+(L.PrimaryWeaponId==TEXT("cfm_yun_summer_megatron")?TEXT("威震天"):W?W->DisplayName.ToString():TEXT("未选择")));})]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)[SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().FillWidth(.45f).Padding(0,0,4,0)[SNew(SMenuButton).ReleaseTheme(true).ContentPadding(FMargin(3,8))
                                .OnClicked_Lambda([this,I]{if(HUD.IsValid())HUD->SelectPetSlot(I);BrowsePets=true;BrowsePetWeapons=false;return FReply::Handled();})[Text(TEXT("选择人物"),13)]]
                            +SHorizontalBox::Slot().FillWidth(.55f)[SNew(SMenuButton).ReleaseTheme(true).ContentPadding(FMargin(3,8))
                                .IsEnabled_Lambda([this,I]{return HUD.IsValid()&&HUD->GetPetLoadout(I).CharacterId!=TEXT("sword_dog");})
                                .OnClicked_Lambda([this,I]{if(HUD.IsValid())HUD->SelectPetSlot(I);BrowsePets=true;BrowsePetWeapons=true;return FReply::Handled();})[Text(TEXT("选择主武器"),13)]]]]]];
        return SNew(SVerticalBox).Visibility_Lambda([this]{return BrowsePets?EVisibility::Visible:EVisibility::Collapsed;})
            +SVerticalBox::Slot().AutoHeight()[Text(TEXT("先选人数，再逐个选择伙伴 · 可重复选择"),18,Muted)]
            +SVerticalBox::Slot().AutoHeight().Padding(0,12,0,12)[Counts]
            +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)[Slots]
            +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)[SNew(STextBlock).Font(Font(18)).ColorAndOpacity(Accent).Text_Lambda([this]
                {return FText::FromString(HUD.IsValid()&&HUD->GetSelectedPetCount()>0?FString::Printf(TEXT("正在给伙伴 %d 选择%s"),HUD->GetActivePetSlot()+1,BrowsePetWeapons?TEXT("主武器"):TEXT("人物")):TEXT("独自行动 · 本局不会生成伙伴"));})];
    }
    TSharedRef<SWidget> CharacterArt(bool PlayerOnly=false)
    {
        auto Art=SNew(SOverlay);
        for(const auto& Pair:CharacterPreviews)
        {
            const FName Id=Pair.Key;const auto Brush=Pair.Value;
            Art->AddSlot()[SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
                .Visibility_Lambda([this,Id,PlayerOnly]
                {
                    if(!HUD.IsValid())return EVisibility::Collapsed;
                    const auto L=PlayerOnly?HUD->GetPendingLoadout():ViewedLoadout();
                    const auto Variant=HUD->GetSelectedCharacterVariant(!PlayerOnly&&BrowsePets);
                    return L.CharacterId==Id&&!FormPreviews.Contains(FormPreviewKey(Id,Variant))?EVisibility::HitTestInvisible:EVisibility::Collapsed;
                })
                [SNew(SImage).Image(Brush.Get())]];
        }
        for(const auto& Pair:FormPreviews)
        {
            const FName Key=Pair.Key;const auto Brush=Pair.Value;
            Art->AddSlot()[SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
                .Visibility_Lambda([this,Key,PlayerOnly]
                {
                    if(!HUD.IsValid())return EVisibility::Collapsed;
                    const auto L=PlayerOnly?HUD->GetPendingLoadout():ViewedLoadout();
                    return FormPreviewKey(L.CharacterId,HUD->GetSelectedCharacterVariant(!PlayerOnly&&BrowsePets))==Key?EVisibility::HitTestInvisible:EVisibility::Collapsed;
                })[SNew(SImage).Image(Brush.Get())]];
        }
        return Art;
    }
    TSharedRef<SWidget> CharacterFormChoices()
    {
        auto Forms=SNew(SVerticalBox);
        for(const auto& D:Characters)if(D->Forms.Num()>1)
        {
            const FName Character=D->CharacterId;
            auto Row=SNew(SHorizontalBox).Visibility_Lambda([this,Character]
                {return ViewedLoadout().CharacterId==Character?EVisibility::Visible:EVisibility::Collapsed;});
            for(const auto& Form:D->Forms)
            {
                const FName Variant=Form.VariantId;const bool Ready=Form.bPresentationReady;
                Row->AddSlot().FillWidth(1).Padding(0,0,8,0)
                    [SNew(SMenuButton).ReleaseTheme(true)
                        .IsEnabled_Lambda([this,Ready]{return Ready&&HUD.IsValid()&&(!BrowsePets||HUD->GetSelectedPetCount()>0);})
                        .Selected_Lambda([this,Variant]{return HUD.IsValid()&&HUD->GetSelectedCharacterVariant(BrowsePets)==Variant;})
                        .OnClicked_Lambda([this,Variant]{if(HUD.IsValid())HUD->SelectCharacterForm(Variant,BrowsePets);return FReply::Handled();})
                        [SNew(STextBlock).Text(Form.DisplayName).Font(Font(17)).ColorAndOpacity(Paper)]];
            }
            Forms->AddSlot().AutoHeight()[Row];
        }
        return Forms;
    }
    TSharedRef<SWidget> HeroArt(){return CharacterArt(true);}
    FText EquipmentDescription()const
    {
        const auto L=ViewedLoadout();
        if(BrowsePets&&HUD.IsValid()&&!HUD->GetSelectedPetCount())return FText::FromString(TEXT("独自行动。选择人数后，为每个伙伴分别选择人物和主武器。"));
        if(L.CharacterId==TEXT("sword_dog"))return FText::FromString(TEXT("刀盾狗保留固定刀盾、近战和盾牌防御，无需另选武器。"));
        if(L.CharacterId==TEXT("cfm_yun_summer"))return FText::FromString(TEXT("三种人物形态可自由搭配主武器。H 切换人物形态 · J 敬酒互动。"));
        if(BrowseCharacters||(BrowsePets&&!BrowsePetWeapons))
        {if(const auto* D=FHeliosCharacterRegistry::FindCharacter(L.CharacterId))return D->Description;}
        if(L.PrimaryWeaponId==TEXT("cfm_sunshangxiang"))return FText::FromString(TEXT("双刃近战。左键连击，G 终极技能；使用所选人物的手臂与语音。"));
        if(L.PrimaryWeaponId==TEXT("cfm_zhongxiaokui"))return FText::FromString(TEXT("唐刀、强化唐刀与飞镖。Q 切换武器形态 · V 切换飞镖 · G 技能 · B形态右键剑气 · R 装填。"));
        if(L.PrimaryWeaponId==TEXT("cfm_chan_zeus"))return FText::FromString(TEXT("宙斯散弹。左键连续射击 · R 换弹 · G 展示；使用所选人物的手臂与语音。"));
        if(L.PrimaryWeaponId==TEXT("cfm_bulianshi_p90"))return FText::FromString(TEXT("P90双脊龙。左键连续射击 · R 换弹 · G 展示；使用所选人物的手臂与语音。"));
        if(L.PrimaryWeaponId==TEXT("cfm_wangzhekui_thunder"))return FText::FromString(TEXT("王者之霆。左键连续射击 · 右键切换霰弹 / 独头弹 · R 换弹 · G 展示。"));
        if(L.PrimaryWeaponId==TEXT("cfm_yun_summer_megatron"))return FText::FromString(TEXT("威震天挑战榴弹。左键连续射击 · R 换弹 · G 展示；使用所选人物的手臂与语音。"));
        if(const auto* D=UHeliosWeaponRegistry::FindWeapon(L.PrimaryWeaponId))return D->Description;
        return FText::GetEmpty();
    }
    TSharedRef<SWidget> EquipmentPage()
    {
        auto Tabs=SNew(SHorizontalBox);
        Tabs->AddSlot().FillWidth(1).Padding(0,0,12,0)[ReleaseButton(TEXT("玩家人物"),[this]{BrowsePets=false;BrowseCharacters=true;})];
        Tabs->AddSlot().FillWidth(1).Padding(0,0,12,0)[ReleaseButton(TEXT("玩家主武器"),[this]{BrowsePets=false;BrowseCharacters=false;})];
        Tabs->AddSlot().FillWidth(1)[ReleaseButton(TEXT("AI 伙伴"),[this]{BrowsePets=true;})];
        auto PetTabs=SNew(SHorizontalBox);
        PetTabs->AddSlot().FillWidth(1).Padding(0,0,12,0)[ReleaseButton(TEXT("伙伴人物"),[this]{BrowsePetWeapons=false;})];
        PetTabs->AddSlot().FillWidth(1)[ReleaseButton(TEXT("伙伴主武器"),[this]{BrowsePetWeapons=true;})];
        auto Grids=SNew(SOverlay);
        Grids->AddSlot()[SNew(SScrollBox).Visibility_Lambda([this]{return !BrowsePets&&BrowseCharacters?EVisibility::Visible:EVisibility::Collapsed;})+SScrollBox::Slot()[CharacterGrid()]];
        Grids->AddSlot()[SAssignNew(WeaponScroll,SScrollBox).Visibility_Lambda([this]{return !BrowsePets&&!BrowseCharacters?EVisibility::Visible:EVisibility::Collapsed;})+SScrollBox::Slot()[WeaponGrid()]];
        Grids->AddSlot()[SNew(SScrollBox).Visibility_Lambda([this]{return BrowsePets&&!BrowsePetWeapons?EVisibility::Visible:EVisibility::Collapsed;})+SScrollBox::Slot()[CharacterGrid(true)]];
        Grids->AddSlot()[SNew(SScrollBox).Visibility_Lambda([this]{return BrowsePets&&BrowsePetWeapons?EVisibility::Visible:EVisibility::Collapsed;})+SScrollBox::Slot()[WeaponGrid(true)]];
        return SNew(SVerticalBox).Visibility_Lambda([this]{return EquipmentOpen?EVisibility::Visible:EVisibility::Collapsed;})
            +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(180)[ReleaseButton(TEXT("← 大厅"),[this]{EquipmentOpen=false;})]]
                +SHorizontalBox::Slot().FillWidth(1).Padding(28,0).VAlign(VAlign_Center)[Text(TEXT("人物、武器与伙伴"),34,Paper)]]
            +SVerticalBox::Slot().AutoHeight().Padding(0,22,0,20)[Tabs]
            +SVerticalBox::Slot().FillHeight(1)[SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(800)[SNew(SVerticalBox)
                    +SVerticalBox::Slot().AutoHeight()[PetRosterControls()]
                    +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)[SNew(SBox).Visibility_Lambda([this]{return BrowsePets?EVisibility::Visible:EVisibility::Collapsed;})[PetTabs]]
                    +SVerticalBox::Slot().FillHeight(1)[Grids]]]
                +SHorizontalBox::Slot().FillWidth(1).Padding(28,0,0,0)[SNew(SBorder).Padding(24).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Color(TEXT("17212B")))
                    [SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(BrowsePets?TEXT("当前伙伴配装"):TEXT("当前玩家配装"));}).Font(Font(18)).ColorAndOpacity(Accent)]
                        +SVerticalBox::Slot().FillHeight(1).Padding(0,16)[CharacterArt()]
                        +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text_Lambda([this]{return ViewedCharacterName();}).Font(Font(26)).ColorAndOpacity(Paper)]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,12,0,0)[CharacterFormChoices()]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,10,0,0)[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(FString(TEXT("主武器 · "))+ViewedWeaponName().ToString());}).Font(Font(22)).ColorAndOpacity(Accent)]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,10,0,18)[Text(TEXT("副武器、近战槽 · 暂未开放"),16,Muted)]
                        +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text_Lambda([this]{return EquipmentDescription();}).Font(Font(18)).ColorAndOpacity(Muted).AutoWrapText(true)]]]]
            +SVerticalBox::Slot().AutoHeight().Padding(0,20,0,16)[MapCards()]
            +SVerticalBox::Slot().AutoHeight()[ActionFooter()];
    }
    TSharedRef<SWidget> WeaponPage()
    {
        return SNew(SOverlay).Visibility(Show(EHeliosAdventureMenu::WeaponSelect))
            +SOverlay::Slot()[MainPage()]+SOverlay::Slot()[EquipmentPage()];
    }
    TSharedRef<SWidget> PausePage()
    {
        return SNew(SVerticalBox).Visibility(Show(EHeliosAdventureMenu::Pause))
            + SVerticalBox::Slot().AutoHeight()[Brand()]
            + SVerticalBox::Slot().FillHeight(1).VAlign(VAlign_Center).HAlign(HAlign_Left)
                [SNew(SBox).WidthOverride(520)
                    [SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()[Text(TEXT("行动暂停"), 48)]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 18, 0, 52)[Text(TEXT("暂歇片刻，随时继续探索。"), 18, Muted)]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)[LabelButton(TEXT("继续游戏"), [this]{ if (HUD.IsValid()) HUD->ContinueRun(); }, true)]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)[LabelButton(TEXT("重新开始"), [this]{ if (HUD.IsValid()) HUD->RequestRestartConfirmation(); })]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)[SoundButton()]
                        + SVerticalBox::Slot().AutoHeight()[LabelButton(TEXT("退出游戏"), [this]{ if (HUD.IsValid()) HUD->QuitToDesktop(); })]]]
            + SVerticalBox::Slot().AutoHeight()[Rule()]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 22, 0, 0)[Text(TEXT("Esc  继续游戏                                      计时与敌人已暂停"), 15, Muted)];
    }
    TSharedRef<SWidget> ConfirmPage()
    {
        return SNew(SBox).WidthOverride(800).Visibility(Show(EHeliosAdventureMenu::RestartConfirm))
            [SNew(SBorder).Padding(48).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Color(TEXT("18232B")))
                [SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()[Text(TEXT("重新开始"), 34)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 24, 0, 38)
                        [SNew(STextBlock).Text_Lambda([this]{return HUD.IsValid()?HUD->GetMenuDetails():FText::GetEmpty();}).Font(Font(19)).ColorAndOpacity(Muted).WrapTextAt(700)]
                    + SVerticalBox::Slot().AutoHeight()[Rule()]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 28, 0, 0)
                        [SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 12, 0)[LabelButton(TEXT("返回"), [this]{ if (HUD.IsValid()) HUD->CancelRestartConfirmation(); }, true)]
                            + SHorizontalBox::Slot().FillWidth(1)[LabelButton(TEXT("确认重开"), [this]{ if (HUD.IsValid()) HUD->ConfirmRestart(); }, false, true)]]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 22, 0, 0)[Text(TEXT("Esc  返回                         F5  确认重开"), 14, Muted)]]];
    }
    TSharedRef<SWidget> Result(const TCHAR* Label, TFunction<FText()> Value, bool Large = false)
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()[Text(Label, 17, Muted)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 16, 0, 0)
                [SNew(STextBlock).Text_Lambda(MoveTemp(Value)).Font(NumberFont(Large ? 62 : 32)).ColorAndOpacity(Paper)];
    }
    TSharedRef<SWidget> DeathPage()
    {
        return SNew(SVerticalBox).Visibility(Show(EHeliosAdventureMenu::Death))
            + SVerticalBox::Slot().AutoHeight()[Brand()]
            + SVerticalBox::Slot().FillHeight(1).VAlign(VAlign_Center).HAlign(HAlign_Left)
                [SNew(SBox).WidthOverride(1140)
                    [SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()[Text(TEXT("行动记录"), 16, Danger)]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 18, 0, 46)[Text(TEXT("行动结束"), 54)]
                        + SVerticalBox::Slot().AutoHeight()
                            [SNew(SHorizontalBox)
                                + SHorizontalBox::Slot().FillWidth(1)[Result(TEXT("存活时间"), [this]
                                    { int32 S = Director() ? FMath::FloorToInt(Director()->GetSurvivalSeconds()) : 0; return FText::FromString(FString::Printf(TEXT("%02d:%02d"), S / 60, S % 60)); }, true)]
                                + SHorizontalBox::Slot().FillWidth(1)[Result(TEXT("本次得分"), [this]{ return FText::AsNumber(Director() ? Director()->GetScore() : 0); }, true)]]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 34, 0, 28)[Rule()]
                        + SVerticalBox::Slot().AutoHeight()
                            [SNew(SHorizontalBox)
                                + SHorizontalBox::Slot().FillWidth(1)[Result(TEXT("击杀"), [this]{ return FText::AsNumber(Director() ? Director()->GetKills() : 0); })]
                                + SHorizontalBox::Slot().FillWidth(1)[Result(TEXT("BOSS 击败"), [this]{ return FText::AsNumber(Director() ? Director()->GetBossKills() : 0); })]
                                + SHorizontalBox::Slot().FillWidth(1)[Result(TEXT("探索分区"), [this]{ return FText::FromString(FString::Printf(TEXT("%d / 7"), Director() ? Director()->GetExploredCount() : 0)); })]
                                + SHorizontalBox::Slot().FillWidth(1)[Result(TEXT("历史最佳"), [this]{ return FText::AsNumber(Director() ? Director()->GetBestScore() : 0); })]]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 52, 0, 0)
                            [SNew(SHorizontalBox)
                                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 14, 0)[SNew(SBox).WidthOverride(340)[LabelButton(TEXT("重新开始"), [this]{ if (HUD.IsValid()) HUD->ConfirmRestart(); }, true)]]
                                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 14, 0)[SNew(SBox).WidthOverride(210)[SoundButton()]]
                                + SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(230)[LabelButton(TEXT("退出游戏"), [this]{ if (HUD.IsValid()) HUD->QuitToDesktop(); })]]]]]
            + SVerticalBox::Slot().AutoHeight()[Rule()]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 22, 0, 0)[Text(TEXT("赫利俄斯前哨站  /  探险生存"), 15, Muted)];
    }
    TWeakObjectPtr<AHeliosAdventureHUD> HUD;
    TArray<TStrongObjectPtr<UHeliosWeaponDefinition>> Definitions;
    TArray<TStrongObjectPtr<UHeliosCharacterDefinition>> Characters;
    TMap<FName,TSharedPtr<FSlateBrush>> CharacterPreviews;
    TMap<FName,TSharedPtr<FSlateBrush>> FormPreviews;
    TArray<TStrongObjectPtr<UTexture2D>> Textures;
    TMap<FName, TSharedPtr<FSlateBrush>> Previews;
    TMap<EHeliosPetKind,TSharedPtr<FSlateBrush>> PetPreviews;
    FSlateBrush BossPortrait;
    TSharedPtr<SOverlay> Content;
    TSharedPtr<SScrollBox> WeaponScroll;
    TMap<FName, TSharedPtr<SWidget>> WeaponRows;
    FName LastVisibleWeapon;
    EHeliosAdventureMenu PreviousState = EHeliosAdventureMenu::None;
    float Fade = 1;
    bool EquipmentOpen=false,BrowsePets=false,BrowseCharacters=true,BrowsePetWeapons=false,LastEquipmentOpen=false,AudioOpen=false;
};
}

TSharedRef<SWidget> MakeHeliosMenu(TWeakObjectPtr<AHeliosAdventureHUD> HUD)
{
    StartHeliosMenuQA(HUD);
    return SNew(HeliosMenu::SMenu).HUD(HUD);
}
