#include "UI/HeliosAdventureHUD.h"
#include "Player/HeliosGuanXiaoYuAbilityComponent.h"
#include "Player/HeliosQingYaAbilityComponent.h"
#include "Player/HeliosCharacterDefinition.h"
#include "Weapons/HeliosWangZheKuiThunder.h"
#include "Weapons/HeliosYunSummerMegatron.h"
#include "Audio/HeliosAudio.h"
#include "Combat/HeliosKillFeedbackSubsystem.h"
#include "UI/SHeliosKillBadge.h"
#include "Weapons/HeliosMysteriousHand.h"
#include "Weapons/HeliosSunShangXiang.h"
#include "Weapons/HeliosZhongXiaoKui.h"
#include "Weapons/HeliosMechWeapon.h"
#include "Weapons/HeliosFighterWeapon.h"
#include "Engine/Canvas.h"
#include "Weapons/HeliosCalamityStaff.h"
#include "Pets/HeliosPetDog.h"
#include "Weapons/HeliosLightstickWeapon.h"
#include "Weapons/HeliosSoulBanner.h"
#include "Weapons/HeliosQingxiao.h"
#include "UI/SWhiteSuitBossBar.h"
#include "UI/HeliosMenuWidget.h"

namespace
{
FString HeliosCompactPetLabel(EHeliosPetKind Kind)
{
    if(Kind==EHeliosPetKind::ChanZeus)return TEXT("蝉 / 宙斯");
    if(Kind==EHeliosPetKind::WangZheKuiThunder)return TEXT("王者葵 / 王者之霆");
    if(Kind==EHeliosPetKind::BuLianShiP90)return TEXT("步练师 / P90");
    if(Kind==EHeliosPetKind::YunSummerMegatron)return TEXT("云悠悠 / 威震天");
    if(const auto* Definition=FHeliosPetDefinition::Find(Kind))
    {FString Name=Definition->Name,First,Rest;return Name.Split(TEXT(" · "),&First,&Rest)?First:Name;}
    return TEXT("伙伴");
}
}
#include "World/HeliosMapTravelSubsystem.h"
#include "World/HeliosMapDefinition.h"
#include "Town/HeliosTownSubsystem.h"
#include "MCP/HeliosMCPJson.h"
#include "Engine/GameInstance.h"
#include "Adventure/HeliosAdventureCharacter.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "Control/HeliosControlSubsystem.h"
#include "Boss/HeliosEnemyBoss.h"
#include "Boss/HeliosUrbanBoss.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Components/AudioComponent.h"
#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Sound/SoundBase.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateColorBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
const FLinearColor Ink(0.010f,0.006f,0.025f,1), Orange(1,0.36f,0.055f,1), Yellow(0.94f,1,0.12f,1), Blue(0.08f,0.72f,1,1), Mist(0.77f,0.79f,0.91f,1);
FSlateFontInfo IkunFont(int32 Size) { return FSlateFontInfo(FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()/TEXT("UI/Fonts/SimHei.ttf")),Size); }

class SHeliosAdventureHUD final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SHeliosAdventureHUD){} SLATE_ARGUMENT(TWeakObjectPtr<AHeliosAdventureHUD>,HUD) SLATE_END_ARGS()
    void Construct(const FArguments& A)
    {
        HUD=A._HUD;
        ChildSlot[SNew(SOverlay)
            + SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)[GameHUD()]
            + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(24,190,24,0)[UrbanSkipButton()]
            + SOverlay::Slot()[MakeHeliosMenu(HUD)]
            + SOverlay::Slot()[MakeHeliosTownOverview(HUD)]];
    }
private:
    TSharedRef<SWidget> UrbanSkipButton()
    {
        return SNew(SVerticalBox)
            .Visibility_Lambda([this]{return HUD.IsValid() && HUD->IsUrbanSkipVisible() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;})
            + SVerticalBox::Slot().AutoHeight()
            [SNew(SBox).WidthOverride(180).HeightOverride(44)
                [SNew(SButton).IsFocusable(false)
                    .IsEnabled_Lambda([this]{return HUD.IsValid() && HUD->CanSkipUrbanWave();})
                    .OnPressed_Lambda([this]{if(HUD.IsValid()) HUD->BeginUrbanSkipClick();})
                    .OnClicked_Lambda([this]{if(HUD.IsValid()) HUD->FinishUrbanSkipClick(); return FReply::Handled();})
                    .ToolTipText(FText::FromString(TEXT("只跳过当前一波。跳过的敌人不计击杀、不发奖励。准备、暂停和死亡时不可用。")))
                    [Text(FText::FromString(TEXT("跳过本回合")),17,FLinearColor::White,ETextJustify::Center)]]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,5,0,0)
                [Text(FText::FromString(TEXT("按住 Tab 显示鼠标并点击")),11,Mist,ETextJustify::Right)];
    }
    // All dimensions are Slate units; the viewport supplies DPI scaling.
    TSharedRef<STextBlock> Text(TAttribute<FText> Value, int32 Size, FLinearColor Color,
        ETextJustify::Type Justification = ETextJustify::Left)
    {
        return SNew(STextBlock).Text(Value).Font(IkunFont(Size)).ColorAndOpacity(Color)
            .ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor(0,0,0,.85f))
            .Justification(Justification).AutoWrapText(true);
    }
    template<typename Getter>
    TAttribute<FText> Bound(Getter Value) { return TAttribute<FText>::CreateLambda(MoveTemp(Value)); }

    TSharedRef<STextBlock> BottomText(TAttribute<FText> Value, int32 Size, FLinearColor Color,
        ETextJustify::Type Justification = ETextJustify::Left)
    {
        return SNew(STextBlock).Text(Value).Font(IkunFont(Size)).ColorAndOpacity(Color)
            .ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor(0,0,0,.85f))
            .Justification(Justification).AutoWrapText(false)
            .WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)
            .WrapTextAt_Lambda([this]{return FMath::Max(1.f,GetCachedGeometry().GetLocalSize().X*.5f-36.f);});
    }

    TSharedRef<SWidget> Panel(const FString& Head, TFunction<FText()> Value, FLinearColor Color, bool Right)
    {
        const auto Justification = Right ? ETextJustify::Right : ETextJustify::Left;
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()[Text(FText::FromString(Head),12,Color,Justification)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)
                [Text(Bound(MoveTemp(Value)),15,FLinearColor::White,Justification)];
    }
    TSharedRef<SWidget> GameHUD()
    {
        return SNew(SOverlay)
        .Visibility_Lambda([this]{return HUD.IsValid()&&HUD->GetMenuState()==EHeliosAdventureMenu::None?EVisibility::HitTestInvisible:EVisibility::Collapsed;})
        + SOverlay::Slot()[SNew(SBox).Visibility_Lambda([this]
            {
                if(auto* Player=HUD.IsValid()?Cast<AHeliosArenaCharacter>(HUD->GetOwningPawn()):nullptr)
                    if(auto* Weapon=Cast<AHeliosYunSummerMegatronWeapon>(Player->GetEquippedWeapon()))
                        if(Weapon->IsKillEffectActive())return EVisibility::Collapsed;
                return EVisibility::HitTestInvisible;
            })[SNew(SHeliosKillBadge).Feedback(HUD.IsValid()?HUD->GetWorld()->GetSubsystem<UHeliosKillFeedbackSubsystem>():nullptr)]]
        + SOverlay::Slot().VAlign(VAlign_Top).Padding(24)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Top).Padding(0,0,12,0)
                [Panel(TEXT("IKUN // 生存练习"),[this]{return HUD.IsValid()?HUD->GetZoneText():FText::GetEmpty();},Blue,false)]
            + SHorizontalBox::Slot().FillWidth(2).HAlign(HAlign_Center).VAlign(VAlign_Top)
                [BossPanel()]
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Top).Padding(12,0,0,0)
                [Panel(TEXT("舞台数据"),[this]{return HUD.IsValid()?HUD->GetRunText():FText::GetEmpty();},Orange,true)]]
        + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(24,154,24,0)
            [SNew(SBox).MaxDesiredWidth(700)
                .Visibility_Lambda([this]{return HUD.IsValid()&&HUD->IsIkunAlertVisible()?EVisibility::HitTestInvisible:EVisibility::Collapsed;})
                [Text(Bound([this]{return HUD.IsValid()?HUD->GetIkunAlertText():FText::GetEmpty();}),19,Yellow,ETextJustify::Center)]]
        + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(24,0,24,48)
            [Text(Bound([this]{return HUD.IsValid()?HUD->GetTownInteractionText():FText::GetEmpty();}),17,FLinearColor::White,ETextJustify::Center)]
        + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
            [SNew(SBox).Visibility_Lambda([this]{return HUD.IsValid()&&HUD->IsFighterHUD()?EVisibility::Collapsed:EVisibility::HitTestInvisible;})[Text(FText::FromString(TEXT("＋")),22,FLinearColor(.72f,.94f,1,.82f),ETextJustify::Center)]]
        // Separate, equally sized columns prevent long status/control strings from overlapping.
        + SOverlay::Slot().VAlign(VAlign_Bottom).Padding(24)
        [SNew(SHorizontalBox).Visibility_Lambda([this]{return HUD.IsValid()&&HUD->IsFighterHUD()?EVisibility::Collapsed:EVisibility::HitTestInvisible;})
            + SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(0,0,12,0)
                [StatusPanel()]
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Bottom).Padding(12,0,0,0)
                [BottomText(Bound([this]{return HUD.IsValid()?HUD->GetControlsText():FText::GetEmpty();}),12,Mist,ETextJustify::Right)]];
    }
    FHeliosStatusPanelData Status;
    FProgressBarStyle HealthBarStyle;
    virtual void Tick(const FGeometry& Geometry, const double Time, const float Delta) override
    {
        SCompoundWidget::Tick(Geometry, Time, Delta);
        Status = HUD.IsValid() ? HUD->GetStatusPanelData() : FHeliosStatusPanelData();
    }
    bool LowHealth() const { return Status.bHasPlayer && FHeliosStatusPanelData::Fraction(Status.Health, Status.MaxHealth) < .3f; }
    float StatusWidth() const { return FMath::Clamp(GetCachedGeometry().GetLocalSize().X * .5f - 36.f, 1.f, 380.f); }
    TSharedRef<STextBlock> StatusText(TAttribute<FText> Value, int32 Size, FLinearColor Color)
    {
        return SNew(STextBlock).Text(Value).Font(IkunFont(Size)).ColorAndOpacity(Color)
            .AutoWrapText(false).WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)
            .WrapTextAt_Lambda([this]{ return FMath::Max(1.f, StatusWidth()-28.f); });
    }
    TSharedRef<SWidget> StatusPanel()
    {
        const FLinearColor Dim(.65f,.72f,.8f), Danger(1.f,.28f,.25f), Green(.25f,.83f,.65f);
        HealthBarStyle = FProgressBarStyle()
            .SetBackgroundImage(FSlateColorBrush(FLinearColor(.035f,.055f,.075f)))
            .SetFillImage(FSlateColorBrush(FLinearColor::White));
        return SNew(SBox).WidthOverride_Lambda([this]{ return StatusWidth(); })
        [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(.015f,.025f,.04f,.06f)).Padding(14)
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [StatusText(Bound([this]{return FText::FromString(Status.bHasPlayer ? FString::Printf(TEXT("体力 %.0f / %.0f%s"), Status.Health, Status.MaxHealth, LowHealth()?TEXT(" · 体力偏低"):TEXT("")) : TEXT("体力 —"));}),20,FLinearColor::White)]
                + SVerticalBox::Slot().AutoHeight().Padding(0,7,0,10)
                [SNew(SBox).HeightOverride(5)[SNew(SProgressBar).Style(&HealthBarStyle).BorderPadding(FVector2D::ZeroVector)
                    .Percent_Lambda([this]()->TOptional<float>{return FHeliosStatusPanelData::Fraction(Status.Health,Status.MaxHealth);})
                    .FillColorAndOpacity_Lambda([this,Danger]{return LowHealth()?Danger:Blue;})]]
                + SVerticalBox::Slot().AutoHeight()[StatusText(Bound([this]{return Status.WeaponName;}),16,FLinearColor::White)]
                + SVerticalBox::Slot().AutoHeight().Padding(0,5,0,0)[StatusText(Bound([this]{return Status.ResourceText;}),13,Dim)]
                + SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)
                [SNew(SBox).Visibility_Lambda([this]{return Status.bReloading?EVisibility::HitTestInvisible:EVisibility::Collapsed;})[StatusText(FText::FromString(TEXT("补充中")),13,Orange)]]
                + SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)
                [SNew(SBox).Visibility_Lambda([this]{return Status.bAutonomy?EVisibility::HitTestInvisible:EVisibility::Collapsed;})[StatusText(FText::FromString(TEXT("AI 托管中")),12,Blue)]]
                + SVerticalBox::Slot().AutoHeight().Padding(0,10,0,0)
                [SNew(SBox).Visibility_Lambda([this]{return Status.PetCount>1?EVisibility::HitTestInvisible:EVisibility::Collapsed;})
                    [StatusText(Bound([this]{return Status.PetRosterStatus;}),12,Dim)]]
                + SVerticalBox::Slot().AutoHeight()
                [SNew(SVerticalBox).Visibility_Lambda([this]{return Status.bHasPet&&Status.PetCount<=1?EVisibility::HitTestInvisible:EVisibility::Collapsed;})
                    + SVerticalBox::Slot().AutoHeight().Padding(0,10,0,10)
                    [SNew(SBox).HeightOverride(1)[SNew(SBorder).Padding(0).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.3f,.42f,.53f,.4f))]]
                    + SVerticalBox::Slot().AutoHeight()[StatusText(Bound([this]{return FText::Format(FText::FromString(TEXT("伙伴 · {0}")),Status.PetName);}),14,FLinearColor::White)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,5,0,0)
                    [SNew(STextBlock).Font(IkunFont(13)).AutoWrapText(true)
                        .Text_Lambda([this]{return FText::FromString(Status.PetActivity.ToString()+(Status.bHasPetDistance?FString::Printf(TEXT(" · %.0f 米"),Status.PetDistanceMetres):FString()));})
                        .ColorAndOpacity_Lambda([this,Dim]{return Status.bPetSleeping?Orange:Dim;})]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,5,0,0)
                    [StatusText(Bound([this]{return FText::FromString(FString::Printf(TEXT("生命 %.0f / %.0f"),Status.PetHealth,Status.PetMaxHealth));}),13,Dim)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,0)
                    [SNew(SBox).HeightOverride(5)[SNew(SProgressBar).Style(&HealthBarStyle).BorderPadding(FVector2D::ZeroVector)
                        .Percent_Lambda([this]()->TOptional<float>{return FHeliosStatusPanelData::Fraction(Status.PetHealth,Status.PetMaxHealth);})
                        .FillColorAndOpacity_Lambda([this,Danger,Green]{return FHeliosStatusPanelData::Fraction(Status.PetHealth,Status.PetMaxHealth)<.3f?Danger:Green;})]]
                ]
            ]];
    }

    TSharedRef<SWidget> BossPanel()
    {
        return SNew(SBox)
            .WidthOverride_Lambda([this]{return FMath::Clamp(GetCachedGeometry().GetLocalSize().X*.5f-24.f,1.f,720.f);})
            .HeightOverride(80)
            [SNew(SWhiteSuitBossBar).HUD(HUD)];
    }

    TWeakObjectPtr<AHeliosAdventureHUD> HUD;
};
}

AHeliosAdventureHUD::AHeliosAdventureHUD(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;}
void AHeliosAdventureHUD::BeginPlay(){Super::BeginPlay();PendingMapId=GetCurrentMapAsset();EntranceSound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Helios/UI/IKUN/SFX_IkunEntrance.SFX_IkunEntrance"));ReactSound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Helios/UI/IKUN/SFX_IkunReact.SFX_IkunReact"));ResetBossPresentation();if(GEngine&&GEngine->GameViewport){RootWidget=SNew(SHeliosAdventureHUD).HUD(this);GEngine->GameViewport->AddViewportWidgetContent(RootWidget.ToSharedRef(),50);}if(PlayerOwner){InputPriority=20;EnableInput(PlayerOwner);if(InputComponent){auto& R=InputComponent->BindKey(EKeys::F5,IE_Pressed,this,&AHeliosAdventureHUD::HandleRestartKey);R.bExecuteWhenPaused=true;R.bConsumeInput=true;auto& P=InputComponent->BindKey(EKeys::Escape,IE_Pressed,this,&AHeliosAdventureHUD::TogglePauseMenu);P.bExecuteWhenPaused=true;P.bConsumeInput=true;if(IsTownWorld()){auto& M=InputComponent->BindKey(EKeys::M,IE_Pressed,this,&AHeliosAdventureHUD::ToggleTownMap);M.bExecuteWhenPaused=true;M.bConsumeInput=true;InputComponent->BindKey(EKeys::E,IE_Pressed,this,&AHeliosAdventureHUD::InteractTown);}}}}
void AHeliosAdventureHUD::EndPlay(const EEndPlayReason::Type R){if(auto* Town=GetWorld()->GetSubsystem<UHeliosTownSubsystem>())if(Town->IsTownWorld())Town->SaveProgress();StopMemeSound();if(RootWidget.IsValid()&&GEngine&&GEngine->GameViewport)GEngine->GameViewport->RemoveViewportWidgetContent(RootWidget.ToSharedRef());RootWidget.Reset();Super::EndPlay(R);}
void AHeliosAdventureHUD::Tick(float D)
{
    Super::Tick(D);
    if(auto* Feedback=GetWorld()->GetSubsystem<UHeliosKillFeedbackSubsystem>())
    {
        Feedback->SetSoundEnabled(bMemeSoundEnabled);
        if(MemeAudioComponent) MemeAudioComponent->SetVolumeMultiplier(.88f*Feedback->OtherVoiceGain());
    }
    auto* Dir=GetDirector(); if(!Dir)return;
    if(Dir->IsUrbanChallenge() && InputComponent && !bUrbanPointerBound)
    {
        bUrbanPointerBound=true;
        InputComponent->BindKey(EKeys::Tab,IE_Pressed,this,&AHeliosAdventureHUD::ShowUrbanPointer).bConsumeInput=true;
        auto& Release=InputComponent->BindKey(EKeys::Tab,IE_Released,this,&AHeliosAdventureHUD::HideUrbanPointer);
        Release.bConsumeInput=true; Release.bExecuteWhenPaused=true;
    }
    if(auto* Travel=GetGameInstance()->GetSubsystem<UHeliosMapTravelSubsystem>())
    {
        if(Travel->bPendingStart && GetWorld()->GetMapName().Contains(Travel->DestinationMap.ToString()))
        {
            PendingMapId=Travel->DestinationMap; PendingLoadout=Travel->PendingLoadout;
            if(Travel->PendingWeaponId!=PendingLoadout.PrimaryWeaponId)PendingLoadout.PrimaryWeaponId=Travel->PendingWeaponId;
            auto* Town=GetWorld()->GetSubsystem<UHeliosTownSubsystem>();
            if(Travel->bResetTownOnArrival && Town && Town->IsTownWorld()) { Town->ResetProgress(); Travel->bResetTownOnArrival=false; }
            if(!IsTownWorld() || (Town && Town->IsWorldReady()))
            {
                if(Dir->StartRunWithLoadout(PendingLoadout)) { Travel->bPendingStart=false; bTravelRequested=false; MenuState=EHeliosAdventureMenu::None; ApplyMenuInput(false); }
            }
            else { MenuState=EHeliosAdventureMenu::WeaponSelect; SelectionError=TEXT("小镇正在载入，请稍候……"); ApplyMenuInput(true); }
            return;
        }
    }
    if(Dir->IsSelectingWeapon())
    {
        if(MenuState==EHeliosAdventureMenu::RestartConfirm)return;
        if(MenuState!=EHeliosAdventureMenu::WeaponSelect) { PendingLoadout=Dir->GetSelectedLoadout();MenuState=EHeliosAdventureMenu::WeaponSelect;SelectionError.Empty();ResetBossPresentation();ApplyMenuInput(true); }
        return;
    }
    UpdateBossPresentation();
    TickBossBar(D);
    if(Dir->IsRunOver()) { if(!IsTownWorld()&&MenuState!=EHeliosAdventureMenu::Death&&MenuState!=EHeliosAdventureMenu::RestartConfirm){MenuState=EHeliosAdventureMenu::Death;ApplyMenuInput(true);}return; }
    if(MenuState!=EHeliosAdventureMenu::None&&GetWorld()&&!GetWorld()->IsPaused()){MenuState=EHeliosAdventureMenu::None;ApplyMenuInput(false);}
}
AHeliosAdventureDirector* AHeliosAdventureHUD::GetDirector()const{return AHeliosAdventureDirector::Find(GetWorld());} double AHeliosAdventureHUD::GetRealTimeSeconds()const{return FPlatformTime::Seconds();}
FText AHeliosAdventureHUD::GetBossAttackText() const
{
    if (auto* Urban=Cast<AHeliosUrbanBoss>(PrimaryBoss)) return FText::FromString(Urban->GetUrbanSkillText());
    if(!IsValid(PrimaryBoss))return FText::GetEmpty();
    const bool Active=PrimaryBoss->IsBossAttackReleasing();
    if(!Active && PrimaryBoss->GetBossAttackType()!=EHeliosBossAttackType::None && PrimaryBoss->GetBossWindupProgress()>=1.f)
        return FText::FromString(TEXT("收招间隙 · 抓住机会反击"));
    switch(PrimaryBoss->GetBossAttackType())
    {
    case EHeliosBossAttackType::ShoulderCharge:return FText::FromString(Active?TEXT("铁山靠 · 侧移躲开冲撞"):TEXT("沉肩蓄力 · 准备向两侧闪避"));
    case EHeliosBossAttackType::CloseShoulder:return FText::FromString(Active?TEXT("贴身短靠 · 拉开身位"):TEXT("沉肩蓄力 · 留意贴身撞击"));
    case EHeliosBossAttackType::SonicCone:return FText::FromString(Active?TEXT("音浪袭来 · 离开正面或寻找掩体"):TEXT("正面音浪蓄力 · 注意朝向"));
    case EHeliosBossAttackType::SonicRadial:return FText::FromString(Active?TEXT("全向震波 · 远离中心或躲入掩体"):TEXT("全向震波蓄力 · 拉开距离"));
    default:return FText::FromString(PrimaryBoss->GetBossPhase()==2?TEXT("二阶段 · 铁山靠攻势加快"):TEXT("观察动作 · 躲避后抓住反击机会"));
    }
}
float AHeliosAdventureHUD::GetBossWindupFraction() const
{
    if (auto* Urban=Cast<AHeliosUrbanBoss>(PrimaryBoss)) return Urban->GetUrbanWarningProgress();
    return IsValid(PrimaryBoss) && !PrimaryBoss->IsBossAttackReleasing()
        && PrimaryBoss->GetBossAttackType()!=EHeliosBossAttackType::None
        ? PrimaryBoss->GetBossWindupProgress() : 0.f;
}
void AHeliosAdventureHUD::ResetBossPresentation(){BossBarNumber=INDEX_NONE;BossBarOpacity=0.f;BossHitFlash=0.f;BossTrailDelay=0.f;PrimaryBoss=nullptr;LastBossSpawnSerial=0;LastBossKills=0;LastBossPhase=0;IkunAlert.Empty();IkunAlertUntilSeconds=0;StopMemeSound();}
AHeliosEnemyBoss* AHeliosAdventureHUD::ChoosePrimaryBoss(const TArray<AHeliosEnemyBoss*>& Bosses)const{if(!PlayerOwner||Bosses.IsEmpty())return nullptr;FVector Eye;FRotator Rot;PlayerOwner->GetPlayerViewPoint(Eye,Rot);FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(IkunBossHUDTrace),true,PlayerOwner->GetPawn());if(GetWorld()&&GetWorld()->LineTraceSingleByChannel(Hit,Eye,Eye+Rot.Vector()*25000,ECC_Visibility,Q))if(auto* B=Cast<AHeliosEnemyBoss>(Hit.GetActor()))if(!B->IsEnemyDead())return B;AHeliosEnemyBoss* V=nullptr,*N=nullptr;float VD=MAX_flt,ND=MAX_flt;FVector O=PlayerOwner->GetPawn()?PlayerOwner->GetPawn()->GetActorLocation():Eye;for(auto* B:Bosses){if(!IsValid(B)||B->IsEnemyDead())continue;float D=FVector::DistSquared(O,B->GetActorLocation());if(D<ND){ND=D;N=B;}if(PlayerOwner->LineOfSightTo(B,Eye)&&D<VD){VD=D;V=B;}}return V?V:N;}
void AHeliosAdventureHUD::UpdateBossPresentation(){auto* D=GetDirector();if(!D)return;if(D->IsUrbanChallenge()){LastBossSpawnSerial=D->GetBossSpawnSerial();LastBossKills=D->GetBossKills();PrimaryBoss=ChoosePrimaryBoss(D->GetBossActors());IkunAlert.Empty();return;}int32 S=D->GetBossSpawnSerial();if(S<LastBossSpawnSerial)ResetBossPresentation();if(S>LastBossSpawnSerial){LastBossSpawnSerial=S;IkunAlert=TEXT("高能预警 · 新练习生强势登场！");IkunAlertUntilSeconds=GetRealTimeSeconds()+3.2;PlayMemeSound(true);}int32 K=D->GetBossKills();if(K>LastBossKills){LastBossKills=K;IkunAlert=TEXT("全场沸腾 · 完成一次漂亮收官！");IkunAlertUntilSeconds=GetRealTimeSeconds()+2.8;}PrimaryBoss=ChoosePrimaryBoss(D->GetBossActors());if(PrimaryBoss){int32 P=PrimaryBoss->GetBossPhase();if(LastBossPhase>0&&P!=LastBossPhase){IkunAlert=FString::Printf(TEXT("第 %d 阶段 · 铁山靠加速"),P);IkunAlertUntilSeconds=GetRealTimeSeconds()+2.8;PlayMemeSound(false);}LastBossPhase=P;}else LastBossPhase=0;}
void AHeliosAdventureHUD::PlayMemeSound(bool E){if(const auto* Map=FHeliosMapRegistry::ForWorld(GetWorld()); Map && Map->PresentationProfile==TEXT("ghost_village"))return;if(!bMemeSoundEnabled||MenuState!=EHeliosAdventureMenu::None)return;double N=GetRealTimeSeconds();if(N-LastMemeSoundSeconds<8)return;USoundBase*S=E?EntranceSound.Get():ReactSound.Get();if(!S)return;StopMemeSound();MemeAudioComponent=HeliosAudio::SpawnSound2D(this,S,.88f);LastMemeSoundSeconds=N;}void AHeliosAdventureHUD::StopMemeSound(){if(MemeAudioComponent){MemeAudioComponent->FadeOut(.12f,0);MemeAudioComponent=nullptr;}}void AHeliosAdventureHUD::ToggleMemeSound(){bMemeSoundEnabled=!bMemeSoundEnabled;if(auto* Feedback=GetWorld()->GetSubsystem<UHeliosKillFeedbackSubsystem>())Feedback->SetSoundEnabled(bMemeSoundEnabled);if(!bMemeSoundEnabled)StopMemeSound();}
void AHeliosAdventureHUD::TogglePauseMenu(){if(MenuState==EHeliosAdventureMenu::Death||MenuState==EHeliosAdventureMenu::WeaponSelect)return;if(MenuState==EHeliosAdventureMenu::RestartConfirm){CancelRestartConfirmation();return;}if(MenuState==EHeliosAdventureMenu::None){MenuState=EHeliosAdventureMenu::Pause;if(auto*D=GetDirector())D->SetRunPaused(true);ApplyMenuInput(true);}else ContinueRun();}
void AHeliosAdventureHUD::RequestRestartConfirmation(){if(MenuState==EHeliosAdventureMenu::WeaponSelect)return;bNewTownConfirm=IsTownWorld();MenuBeforeConfirm=MenuState;MenuState=EHeliosAdventureMenu::RestartConfirm;if(auto*D=GetDirector())D->SetRunPaused(true);ApplyMenuInput(true);}void AHeliosAdventureHUD::CancelRestartConfirmation(){bNewTownConfirm=false;MenuState=MenuBeforeConfirm==EHeliosAdventureMenu::None?EHeliosAdventureMenu::Pause:MenuBeforeConfirm;if(MenuState==EHeliosAdventureMenu::WeaponSelect&&IsTownWorld())UGameplayStatics::SetGamePaused(this,false);ApplyMenuInput(true);}void AHeliosAdventureHUD::ContinueRun(){if(MenuState==EHeliosAdventureMenu::Death||MenuState==EHeliosAdventureMenu::WeaponSelect)return;MenuState=EHeliosAdventureMenu::None;if(auto*D=GetDirector())D->SetRunPaused(false);ApplyMenuInput(false);}
void AHeliosAdventureHUD::ConfirmRestart()
{
    const bool FromSelection=MenuBeforeConfirm==EHeliosAdventureMenu::WeaponSelect;
    const FHeliosLoadout ChosenLoadout=PendingLoadout;
    if(bNewTownConfirm || IsTownWorld())
    {
        bNewTownConfirm=false;
        if(auto* Town=GetWorld()->GetSubsystem<UHeliosTownSubsystem>())if(Town->IsTownWorld() && GetCurrentMapAsset()==PendingMapId)Town->ResetProgress();
        if(GetCurrentMapAsset()!=PendingMapId)
        {
            auto* Travel=GetGameInstance()->GetSubsystem<UHeliosMapTravelSubsystem>();
            Travel->bResetTownOnArrival=true;MenuState=EHeliosAdventureMenu::WeaponSelect;StartSelectedRun();return;
        }
    }
    if(auto* D=GetDirector()){D->EnterWeaponSelection();PendingLoadout=FromSelection?ChosenLoadout:D->GetSelectedLoadout();}
    SelectionError.Empty();ResetBossPresentation();MenuState=EHeliosAdventureMenu::WeaponSelect;ApplyMenuInput(true);
}
void AHeliosAdventureHUD::SelectPet(EHeliosPetKind Kind)
{
    if(MenuState!=EHeliosAdventureMenu::WeaponSelect)return;
    if(auto* Session=GetGameInstance()->GetSubsystem<UHeliosPetSession>())Session->SelectSlot(ActivePetSlot,Kind);
}
void AHeliosAdventureHUD::SelectPetCount(int32 Count)
{
    if(MenuState!=EHeliosAdventureMenu::WeaponSelect)return;
    if(auto* Session=GetGameInstance()->GetSubsystem<UHeliosPetSession>())Session->SetCount(Count);
    ActivePetSlot=FMath::Clamp(ActivePetSlot,0,FMath::Max(0,GetSelectedPetCount()-1));
}
void AHeliosAdventureHUD::SelectPetSlot(int32 Slot)
{if(MenuState==EHeliosAdventureMenu::WeaponSelect&&Slot>=0&&Slot<GetSelectedPetCount())ActivePetSlot=Slot;}
int32 AHeliosAdventureHUD::GetSelectedPetCount()const
{if(auto* S=GetGameInstance()->GetSubsystem<UHeliosPetSession>())return S->GetCount();return 1;}
EHeliosPetKind AHeliosAdventureHUD::GetPetKind(int32 Slot)const
{if(auto* S=GetGameInstance()->GetSubsystem<UHeliosPetSession>())return S->GetKind(Slot);return EHeliosPetKind::SwordDog;}
FString AHeliosAdventureHUD::GetPetRosterSummary()const
{
    if(!GetSelectedPetCount())return TEXT("0 位伙伴 · 独自行动");
    FString Result=FString::Printf(TEXT("%d 位伙伴"),GetSelectedPetCount());
    for(int32 I=0;I<GetSelectedPetCount();++I)
    {
        const FHeliosLoadout Loadout=GetPetLoadout(I);
        if(Loadout.CharacterId==TEXT("sword_dog")){Result+=FString::Printf(TEXT("\n%d · 刀盾狗"),I+1);continue;}
        const auto* Character=FHeliosCharacterRegistry::FindCharacter(Loadout.CharacterId);
        const auto Resolved=FHeliosLoadout::ResolveLegacyVariant(Loadout,GetGameInstance(),I);
        const auto* Form=Character?Character->FindForm(Resolved.CharacterVariantId):nullptr;
        const FString Name=Character?Character->DisplayName.ToString()+(Character->Forms.Num()>1&&Form?TEXT(" · ")+Form->DisplayName.ToString():FString()):TEXT("人物");
        const auto* Weapon=UHeliosWeaponRegistry::FindWeapon(Loadout.PrimaryWeaponId);
        const FString WeaponName=Loadout.PrimaryWeaponId==TEXT("cfm_yun_summer_megatron")?TEXT("威震天"):Weapon?Weapon->DisplayName.ToString():TEXT("主武器");
        Result+=FString::Printf(TEXT("\n%d · %s / %s"),I+1,*Name,*WeaponName);
    }
    return Result;
}
FHeliosLoadout AHeliosAdventureHUD::GetPetLoadout(int32 Slot)const
{
    if(auto* Session=GetGameInstance()?GetGameInstance()->GetSubsystem<UHeliosPetSession>():nullptr)return Session->GetLoadout(Slot);
    return FHeliosLoadout::MakeDefault();
}
void AHeliosAdventureHUD::SelectPetCharacter(FName Id,FName Variant)
{
    if(MenuState!=EHeliosAdventureMenu::WeaponSelect || GetSelectedPetCount()==0)return;
    if(auto* Session=GetGameInstance()->GetSubsystem<UHeliosPetSession>())
    {if(!Session->SelectCharacterSlot(ActivePetSlot,Id,Variant))SelectionError=Session->LastSelectionError;else SelectionError.Empty();}
}
void AHeliosAdventureHUD::SelectPetWeapon(FName Id)
{
    if(MenuState!=EHeliosAdventureMenu::WeaponSelect || GetSelectedPetCount()==0)return;
    if(auto* Session=GetGameInstance()->GetSubsystem<UHeliosPetSession>())
    {if(!Session->SelectWeaponSlot(ActivePetSlot,Id))SelectionError=Session->LastSelectionError;else SelectionError.Empty();}
}
EHeliosPetKind AHeliosAdventureHUD::GetSelectedPet() const
{
    if(auto* Session=GetGameInstance()->GetSubsystem<UHeliosPetSession>())return Session->GetKind(FMath::Min(ActivePetSlot,FMath::Max(0,Session->GetCount()-1)));
    return EHeliosPetKind::SwordDog;
}
EHeliosYunSummerForm AHeliosAdventureHUD::GetYunSummerForm(bool bPet) const
{
    const FName Variant=GetSelectedCharacterVariant(bPet);
    if(Variant==TEXT("GR"))return EHeliosYunSummerForm::GR;
    if(Variant==TEXT("BL"))return EHeliosYunSummerForm::BL;
    if(Variant==TEXT("SP"))return EHeliosYunSummerForm::SP;
    return EHeliosYunSummerForm::SP;
}
void AHeliosAdventureHUD::SelectYunSummerForm(EHeliosYunSummerForm Form,bool bPet)
{
    if(MenuState!=EHeliosAdventureMenu::WeaponSelect || (bPet&&GetSelectedPetCount()==0))return;
    const FHeliosLoadout Current=bPet?GetPetLoadout(ActivePetSlot):PendingLoadout;
    if(Current.CharacterId!=TEXT("cfm_yun_summer"))return;
    SelectCharacterForm(HeliosYunSummer::FormCode(Form),bPet);
}
FName AHeliosAdventureHUD::GetSelectedCharacterVariant(bool bPet) const
{
    const auto Loadout=FHeliosLoadout::ResolveLegacyVariant(bPet?GetPetLoadout(ActivePetSlot):PendingLoadout,
        GetGameInstance(),bPet?ActivePetSlot:INDEX_NONE);
    return Loadout.CharacterVariantId;
}
void AHeliosAdventureHUD::SelectCharacterForm(FName Variant,bool bPet)
{
    if(MenuState!=EHeliosAdventureMenu::WeaponSelect||(bPet&&GetSelectedPetCount()==0))return;
    auto Candidate=bPet?GetPetLoadout(ActivePetSlot):PendingLoadout;
    const auto* Character=FHeliosCharacterRegistry::FindCharacter(Candidate.CharacterId);
    const auto* Form=Character?Character->FindForm(Variant):nullptr;
    if(!Form||!Form->bPresentationReady)return;
    Candidate.CharacterVariantId=Form->VariantId;
    if(bPet)
    {
        if(auto* Pets=GetGameInstance()->GetSubsystem<UHeliosPetSession>())
            if(!Pets->SelectLoadout(ActivePetSlot,Candidate)){SelectionError=TEXT("伙伴形态选择失败。");return;}
        SelectionError.Empty();
    }
    else SelectLoadout(Candidate);
}
FText AHeliosAdventureHUD::GetSelectedCharacterName()const
{
    if(const auto* D=FHeliosCharacterRegistry::FindCharacter(PendingLoadout.CharacterId))
    {
        const auto* Form=D->FindForm(GetSelectedCharacterVariant());
        if(D->Forms.Num()>1&&Form)return FText::FromString(D->DisplayName.ToString()+TEXT(" · ")+Form->DisplayName.ToString());
        return D->DisplayName;
    }
    return FText::GetEmpty();
}
FText AHeliosAdventureHUD::GetSelectedEquipmentName()const
{
    if(PendingLoadout.PrimaryWeaponId==TEXT("cfm_yun_summer_megatron"))return FText::FromString(TEXT("威震天"));
    if(const auto* D=UHeliosWeaponRegistry::FindWeapon(PendingLoadout.PrimaryWeaponId))return D->DisplayName;
    return FText::GetEmpty();
}
void AHeliosAdventureHUD::SelectCharacter(FName Id,FName Variant)
{
    if(MenuState!=EHeliosAdventureMenu::WeaponSelect)return;
    const auto* Character=FHeliosCharacterRegistry::FindCharacter(Id);
    if(!Character || !Character->bPlayerSelectable || !Character->bPresentationReady)return;
    auto Candidate=PendingLoadout;
    const bool SameCharacter=Candidate.CharacterId==Character->CharacterId;
    Candidate.CharacterId=Character->CharacterId;
    if(!SameCharacter||!Variant.IsNone())Candidate.CharacterVariantId=Variant;
    SelectLoadout(Candidate);
}
void AHeliosAdventureHUD::SelectLoadout(const FHeliosLoadout& Loadout)
{
    if(MenuState!=EHeliosAdventureMenu::WeaponSelect)return;
    FString Error;if(!FHeliosLoadout::Validate(Loadout,Error)||Loadout.CharacterId==TEXT("sword_dog")){SelectionError=Error;return;}
    PendingLoadout=FHeliosLoadout::ResolveLegacyVariant(Loadout,GetGameInstance());SelectionError.Empty();
}
void AHeliosAdventureHUD::SelectWeapon(FName Id)
{
    if(MenuState!=EHeliosAdventureMenu::WeaponSelect)return;
    auto Candidate=PendingLoadout;Candidate.PrimaryWeaponId=Id;SelectLoadout(Candidate);
}
void AHeliosAdventureHUD::StartSelectedRun()
{
    if(MenuState!=EHeliosAdventureMenu::WeaponSelect||bTravelRequested)return;
    const FName Current=GetCurrentMapAsset();
    if(Current!=PendingMapId)
    {
        auto* Travel=GetGameInstance()->GetSubsystem<UHeliosMapTravelSubsystem>();
        if(Travel && Travel->RequestTravelWithLoadout(PendingMapId,PendingLoadout)){bTravelRequested=true;SelectionError=TEXT("正在前往目的地……");}
        else SelectionError=TEXT("地图尚未就绪，请检查地图文件。");
        return;
    }
    if(IsTownWorld())if(auto* Town=GetWorld()->GetSubsystem<UHeliosTownSubsystem>())if(!Town->IsWorldReady()) { SelectionError=TEXT("小镇正在载入，请稍候再试。");return; }
    if(auto* Dir=GetDirector())if(Dir->StartRunWithLoadout(PendingLoadout)){ResetBossPresentation();MenuState=EHeliosAdventureMenu::None;ApplyMenuInput(false);return;}
    SelectionError=PendingLoadout.PrimaryWeaponId==TEXT("fighter")?TEXT("附近空间不足，无法部署隼影战斗机。请更换开阔的出生地点。"):PendingLoadout.PrimaryWeaponId==TEXT("mech")?TEXT("附近没有足够空间部署20米机甲，请前往开阔地带。"):TEXT("装备加载失败，请重新选择。");
}
void AHeliosAdventureHUD::ShowDeploymentFailure(const FString& Reason)
{
    if(auto* D=GetDirector())D->EnterWeaponSelection();
    if(auto* Travel=GetGameInstance()->GetSubsystem<UHeliosMapTravelSubsystem>())Travel->bPendingStart=false;
    bTravelRequested=false;if(auto* P=PlayerOwner?Cast<AHeliosArenaCharacter>(PlayerOwner->GetPawn()):nullptr)PendingLoadout=P->GetLoadout();MenuState=EHeliosAdventureMenu::WeaponSelect;
    SelectionError=Reason;ResetBossPresentation();ApplyMenuInput(true);
}
void AHeliosAdventureHUD::HandleRestartKey(){if(MenuState==EHeliosAdventureMenu::RestartConfirm)ConfirmRestart();else RequestRestartConfirmation();}void AHeliosAdventureHUD::QuitToDesktop(){if(auto* Town=GetWorld()->GetSubsystem<UHeliosTownSubsystem>())if(Town->IsTownWorld())Town->SaveProgress();UKismetSystemLibrary::QuitGame(this,PlayerOwner,EQuitPreference::Quit,false);}
void AHeliosAdventureHUD::ApplyMenuInput(bool O){if(!PlayerOwner)return;if(bUrbanPointer){bUrbanPointer=false;PlayerOwner->SetIgnoreLookInput(false);}if(O)if(auto* P=Cast<AHeliosArenaCharacter>(PlayerOwner->GetPawn()))if(auto* L=Cast<AHeliosLightstickWeapon>(P->GetEquippedWeapon()))L->SuspendInput();if(O){StopMemeSound();if(auto* Feedback=GetWorld()->GetSubsystem<UHeliosKillFeedbackSubsystem>())Feedback->SuspendAudio();}PlayerOwner->bShowMouseCursor=O;if(O){FInputModeGameAndUI M;M.SetHideCursorDuringCapture(false);M.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);PlayerOwner->SetInputMode(M);}else PlayerOwner->SetInputMode(FInputModeGameOnly());}

bool AHeliosAdventureHUD::IsUrbanSkipVisible() const
{
    const auto* D=GetDirector();
    return MenuState==EHeliosAdventureMenu::None && D && D->IsUrbanChallenge() && !D->IsRunOver() && !D->IsSelectingWeapon();
}
bool AHeliosAdventureHUD::CanSkipUrbanWave() const
{
    const auto* D=GetDirector();
    return IsUrbanSkipVisible() && D && D->CanSkipUrbanWave();
}
void AHeliosAdventureHUD::BeginUrbanSkipClick()
{
    UrbanClickRun=UrbanClickWave=INDEX_NONE;
    if(CanSkipUrbanWave()) if(const auto* D=GetDirector())
    { UrbanClickRun=D->GetRunSerial(); UrbanClickWave=D->GetUrbanWaveNumber(); }
}
void AHeliosAdventureHUD::FinishUrbanSkipClick()
{
    const int32 Run=UrbanClickRun, Wave=UrbanClickWave;
    UrbanClickRun=UrbanClickWave=INDEX_NONE;
    if(CanSkipUrbanWave()) if(auto* D=GetDirector()) D->TrySkipUrbanWave(Run,Wave);
}
void AHeliosAdventureHUD::ShowUrbanPointer()
{
    if(!PlayerOwner || !IsUrbanSkipVisible() || bUrbanPointer)return;
    for(FKey Key:{EKeys::LeftMouseButton,EKeys::RightMouseButton})
        PlayerOwner->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),Key,IE_Released,FPlatformTime::Cycles64()));
    bUrbanPointer=true; PlayerOwner->SetIgnoreLookInput(true); PlayerOwner->bShowMouseCursor=true;
    FInputModeGameAndUI Mode; Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways); PlayerOwner->SetInputMode(Mode);
}
void AHeliosAdventureHUD::HideUrbanPointer()
{
    if(bUrbanPointer)ApplyMenuInput(MenuState!=EHeliosAdventureMenu::None);
}
FText AHeliosAdventureHUD::GetZoneText()const{if(IsTownWorld())if(auto* T=GetWorld()->GetSubsystem<UHeliosTownSubsystem>())return FText::FromString(FString::Printf(TEXT("%s\n探索街区 %d / 8"),*T->GetCurrentZoneName(),T->GetExploredDistrictCount()));auto*D=GetDirector();return FText::FromString(FString::Printf(TEXT("当前球场 %s\n解锁分区 %d / 7"),D?*D->GetZoneName():TEXT("灯光调试中"),D?D->GetExploredCount():0));}
FText AHeliosAdventureHUD::GetRunText()const
{
    auto* D=GetDirector();
    const int32 Seconds=D?FMath::Max(0,FMath::FloorToInt(D->GetSurvivalSeconds())):0;
    if (D && D->IsUrbanChallenge()) return FText::FromString(D->GetUrbanWaveText());
    if(IsTownWorld())if(auto* Town=GetWorld()->GetSubsystem<UHeliosTownSubsystem>())
    {
        const int32 UntilBoss=D?FMath::Max(0,FMath::CeilToInt(D->GetSecondsUntilNextBoss())):60;
        return FText::FromString(FString::Printf(TEXT("本次 %02d:%02d   淘汰 %d\n普通敌人 %d / %d\n首领倒计时 %02d:%02d\n首领在场 %d   候场 %d\n发现彩蛋 %d / 100"),
            Seconds/60,Seconds%60,D?D->GetKills():0,D?D->GetAliveCount():0,D?D->GetEnemyCap():4,
            UntilBoss/60,UntilBoss%60,D?D->GetBossCount():0,D?D->GetPendingBossCount():0,Town->GetDiscoveredMemeCount()));
    }
    return FText::FromString(FString::Printf(TEXT("练习 %02d:%02d\n热度 %06d   淘汰 %02d"),Seconds/60,Seconds%60,D?D->GetScore():0,D?D->GetKills():0));
}
FHeliosStatusPanelData AHeliosAdventureHUD::GetStatusPanelData() const
{
    FHeliosStatusPanelData Data;
    auto* P = PlayerOwner ? Cast<AHeliosAdventureCharacter>(PlayerOwner->GetPawn()) : nullptr;
    Data.WeaponName = FText::FromString(TEXT("候场"));
    if (IsValid(P))
    {
        Data.bHasPlayer = true;
        Data.Health = P->GetHealth(); Data.MaxHealth = P->GetMaxHealth();
        Data.WeaponName = P->GetWeaponDisplayName(); Data.bReloading = P->IsReloading();
        const FName Id = P->GetWeaponId();
        const auto Loadout=P->GetLoadout();
        if(Id!=TEXT("mech")&&Id!=TEXT("fighter"))
            if(const auto* Character=FHeliosCharacterRegistry::FindCharacter(Loadout.CharacterId))
            {
                FString Name=Character->DisplayName.ToString();
                if(const auto* Form=Character->FindForm(Loadout.CharacterVariantId);Form&&Character->Forms.Num()>1)Name+=TEXT(" / ")+Form->DisplayName.ToString();
                Data.WeaponName=FText::FromString(Name+TEXT(" / ")+Data.WeaponName.ToString());
            }
        if (Id == TEXT("cfm_yun_summer_megatron"))
        {
            Data.ResourceText=FText::FromString(FString::Printf(TEXT("榴弹 %d / %d · 无限备用弹药"),P->GetAmmo(),P->GetMagazineSize()));
        }
        else if (Id == TEXT("cfm_wangzhekui_thunder"))
        { if(auto* W=Cast<AHeliosWangZheKuiThunderWeapon>(P->GetEquippedWeapon()))Data.ResourceText=FText::FromString(W->Status()); }
        else if (Id == TEXT("cfm_sunshangxiang"))
        { if(auto* W=Cast<AHeliosSunShangXiangWeapon>(P->GetEquippedWeapon()))Data.ResourceText=FText::FromString(W->Status()); }
        else if (Id == TEXT("cfm_zhongxiaokui"))
        { if(auto* W=Cast<AHeliosZhongXiaoKuiWeapon>(P->GetEquippedWeapon()))Data.ResourceText=FText::FromString(W->Status()); }
        else if (Id == TEXT("fighter"))
        { if(auto* F=Cast<AHeliosFighterWeapon>(P->GetEquippedWeapon()))Data.ResourceText=FText::FromString(F->Status()); }
        else if (Id == TEXT("mech"))
        { if(auto* M=Cast<AHeliosMechWeapon>(P->GetEquippedWeapon()))Data.ResourceText=FText::FromString(M->Status()); }
        else if (Id == TEXT("calamity"))
        { if (auto* State = UHeliosCalamityState::Get(P, false)) Data.ResourceText = State->Status(); }
        else if (Id == TEXT("qingxiao"))
        { if (auto* State = UHeliosQingxiaoFormation::Get(P, false)) Data.ResourceText = FText::Format(FText::FromString(TEXT("{0} · 100米")), State->Status()); }
        else if (Id == TEXT("soulbanner"))
        { if (auto* State = UHeliosSoulArmy::Get(P, false)) Data.ResourceText = State->Status(); }
        else if (Id == TEXT("mysterious_hand"))
        {
            const auto* Hand=Cast<AHeliosMysteriousHand>(P->GetEquippedWeapon());
            Data.ResourceText=FText::FromString(Hand?Hand->GetHandStatus():TEXT("无限点射 · 定点轰炸"));
        }
        else if (Id == TEXT("lightstick"))
        {
            const auto* Weapon = Cast<AHeliosLightstickWeapon>(P->GetEquippedWeapon());
            Data.ResourceText = FText::FromString(FString::Printf(TEXT("近战 5m / 剑气 100m · 蓄力 %d%%"), Weapon ? FMath::RoundToInt(Weapon->GetCharge()*100) : 0));
        }
        else
        {
            const TCHAR* Label = Id == TEXT("cola") ? TEXT("饮料") : Id == TEXT("chopsticks") ? TEXT("飞筷") : Id == TEXT("laser") ? TEXT("能量") : TEXT("弹药");
            const TCHAR* Unit = Id == TEXT("cola") ? TEXT(" 罐") : Id == TEXT("chopsticks") ? TEXT(" 对") : TEXT("");
            Data.ResourceText = FText::FromString(FString::Printf(TEXT("%s %d / %d%s"), Label, P->GetAmmo(), P->GetMagazineSize(), Unit));
        }
    }
    if (const auto* World = GetWorld())
    {
        if (const auto* Control = World->GetSubsystem<UHeliosControlSubsystem>()) Data.bAutonomy = Control->IsAutonomyActive();
        const auto* Pets = World->GetSubsystem<UHeliosPetWorld>();
        const auto* Pet = Pets ? Pets->Pet.Get() : nullptr;
        FString RosterStatus;
        if(Pets)for(const auto& Companion:Pets->Pets)if(IsValid(Companion)&&Companion->CompanionOwner.Get()==P)
        {
            ++Data.PetCount;
            const TCHAR* State=Companion->State==EHeliosPetState::Fighting?TEXT("交战"):Companion->State==EHeliosPetState::Waking?TEXT("起身"):TEXT("跟随");
            const FString Life=Companion->IsSleeping()?FString::Printf(TEXT("休息 %d 秒"),FMath::CeilToInt(Companion->SleepRemaining)):FString::Printf(TEXT("%s %.0f / %.0f"),State,Companion->Health,Companion->Settings.MaxHealth);
            FString Label=HeliosCompactPetLabel(Companion->Kind);
            if(const auto* Character=FHeliosCharacterRegistry::FindCharacter(Companion->Loadout.CharacterId))
            {
                Label=Character->DisplayName.ToString();
                if(const auto* Form=Character->FindForm(Companion->Loadout.CharacterVariantId);Form&&Character->Forms.Num()>1)
                    Label+=TEXT(" · ")+Form->DisplayName.ToString();
            }
            RosterStatus+=FString::Printf(TEXT("%d · %s  %s\n"),Companion->RosterSlot+1,*Label,*Life);
        }
        Data.PetRosterStatus=FText::FromString(RosterStatus.TrimEnd());
        if (!FParse::Param(FCommandLine::Get(), TEXT("HeliosNoPet")) && IsValid(Pet) && Pet->CompanionOwner.Get() == P && IsValid(P))
        {
            Data.bHasPet = true; Data.PetName = FText::FromString(Pet->DisplayName());
            Data.PetHealth = Pet->Health; Data.PetMaxHealth = Pet->Settings.MaxHealth;
            Data.bPetSleeping = Pet->IsSleeping();
            const TCHAR* Activity = TEXT("跟随");
            switch (Pet->State)
            {
            case EHeliosPetState::CatchingUp: Activity = TEXT("追赶"); break;
            case EHeliosPetState::Fighting: Activity = TEXT("助战"); break;
            case EHeliosPetState::Returning: Activity = TEXT("返回"); break;
            case EHeliosPetState::Waking: Activity = TEXT("起身"); break;
            default: break;
            }
            Data.PetActivity = FText::FromString(Data.bPetSleeping ? FString::Printf(TEXT("休息 %d 秒"), FMath::Max(0, FMath::CeilToInt(Pet->SleepRemaining))) : FString(Activity));
            Data.bHasPetDistance = Pet->CompanionOwner.IsValid();
            Data.PetDistanceMetres = FVector::Dist2D(Pet->GetActorLocation(), P->GetActorLocation()) / 100.f;
        }
    }
    return Data;
}

FText AHeliosAdventureHUD::GetVitalsText()const
{
    if(auto* Player=PlayerOwner?Cast<AHeliosArenaCharacter>(PlayerOwner->GetPawn()):nullptr)
        if(auto* Weapon=Cast<AHeliosYunSummerMegatronWeapon>(Player->GetEquippedWeapon()))return FText::FromString(Weapon->Status());
    if(auto* P=PlayerOwner?Cast<AHeliosArenaCharacter>(PlayerOwner->GetPawn()):nullptr)if(auto* K=Cast<AHeliosWangZheKuiThunderWeapon>(P->GetEquippedWeapon()))return FText::FromString(K->Status());if(auto* P=PlayerOwner?Cast<AHeliosArenaCharacter>(PlayerOwner->GetPawn()):nullptr)if(auto* M=Cast<AHeliosMechWeapon>(P->GetEquippedWeapon()))return FText::FromString(M->Status());if(auto* P=PlayerOwner?Cast<AHeliosAdventureCharacter>(PlayerOwner->GetPawn()):nullptr;P && P->GetWeaponId()==TEXT("calamity")){auto* S=UHeliosCalamityState::Get(P);return FText::FromString(FString::Printf(TEXT("体力 %03d // 灾厄王权 // %s"),FMath::RoundToInt(P->GetHealth()),*S->Status().ToString()));}auto*P=PlayerOwner?Cast<AHeliosAdventureCharacter>(PlayerOwner->GetPawn()):nullptr;if(P && P->GetWeaponId()==TEXT("qingxiao")){auto* F=UHeliosQingxiaoFormation::Get(P);return FText::FromString(FString::Printf(TEXT("体力 %03d // 青霄 // %s · 100米"),FMath::RoundToInt(P->GetHealth()),*F->Status().ToString()));}if(P && P->GetWeaponId()==TEXT("soulbanner")){auto* Army=UHeliosSoulArmy::Get(P);return FText::FromString(FString::Printf(TEXT("体力 %03d // 万魂幡 // %s"),FMath::RoundToInt(P->GetHealth()),*Army->Status().ToString()));}if(P && P->GetWeaponId()==TEXT("lightstick")){const auto* L=Cast<AHeliosLightstickWeapon>(P->GetEquippedWeapon());return FText::FromString(FString::Printf(TEXT("体力 %03d // 安可 · 脉冲应援棒 // 近战 5m / 剑气 100m · 蓄力 %03d%%"),FMath::RoundToInt(P->GetHealth()),L?FMath::RoundToInt(L->GetCharge()*100):0));}FString C;if(const UWorld*W=GetWorld())if(const auto*X=W->GetSubsystem<UHeliosControlSubsystem>())C=X->IsAutonomyActive()?TEXT(" · AI 托管中"):TEXT("");return FText::FromString(FString::Printf(TEXT("体力 %03d // %s // %s %d / %d%s%s"),P?FMath::RoundToInt(P->GetHealth()):0,P?*P->GetWeaponDisplayName().ToString():TEXT("候场"),P&&P->GetWeaponId()==TEXT("cola")?TEXT("饮料"):P&&P->GetWeaponId()==TEXT("chopsticks")?TEXT("飞筷"):P&&P->GetWeaponId()==TEXT("laser")?TEXT("能量"):TEXT("弹药"),P?P->GetAmmo():0,P?P->GetMagazineSize():30,*(FString(P&&P->GetWeaponId()==TEXT("cola")?TEXT(" 罐"):P&&P->GetWeaponId()==TEXT("chopsticks")?TEXT(" 对"):TEXT(""))+(P&&P->IsReloading()?TEXT(" · 补充中"):TEXT(""))),*C));
}
FText AHeliosAdventureHUD::GetBossTitleText()const{if(Cast<AHeliosUrbanBoss>(PrimaryBoss))return FText::FromString(TEXT("URBAN BOSS - GEOMETRY PLACEHOLDER"));return PrimaryBoss?FText::FromString(FString::Printf(TEXT("BOSS %02d · 第 %d 阶段"),PrimaryBoss->GetBossNumber(),PrimaryBoss->GetBossPhase())):FText::GetEmpty();}FText AHeliosAdventureHUD::GetBossMetaText()const{auto*D=GetDirector();if(!D)return FText::GetEmpty();float N=D->GetSecondsUntilNextBoss();FString C=N>86400.f?TEXT("自由练习"):N>=0?FString::Printf(TEXT("下位 %.0fs"),N):TEXT("本轮已齐");return FText::FromString(FString::Printf(TEXT("场上 %d // 候场 %d // %s"),D->GetBossCount(),D->GetPendingBossCount(),*C));}float AHeliosAdventureHUD::GetBossHealthFraction()const{return PrimaryBoss?FMath::Clamp(PrimaryBoss->GetHealth()/PrimaryBoss->GetBossMaxHealth(),0.f,1.f):0;}bool AHeliosAdventureHUD::HasActiveBoss()const{return IsValid(PrimaryBoss)&&!PrimaryBoss->IsEnemyDead();}bool AHeliosAdventureHUD::IsIkunAlertVisible()const{return !IkunAlert.IsEmpty()&&GetRealTimeSeconds()<IkunAlertUntilSeconds;}FText AHeliosAdventureHUD::GetIkunAlertText()const{return FText::FromString(IkunAlert);}
FText AHeliosAdventureHUD::GetMenuTitle()const{if(auto* D=GetDirector();D&&D->IsUrbanVictory()&&MenuState==EHeliosAdventureMenu::Death)return FText::FromString(TEXT("30波挑战通关"));if(MenuState==EHeliosAdventureMenu::Death)return FText::FromString(TEXT("行动结束"));if(MenuState==EHeliosAdventureMenu::RestartConfirm)return FText::FromString(TEXT("重新开始"));if(MenuState==EHeliosAdventureMenu::WeaponSelect)return FText::FromString(TEXT("装备配置"));return FText::FromString(TEXT("行动暂停"));}
FText AHeliosAdventureHUD::GetMenuDetails()const{if(auto* D=GetDirector();D&&D->IsUrbanChallenge()&&MenuState==EHeliosAdventureMenu::Death)return FText::FromString(D->GetUrbanWaveText()+TEXT("\n灰盒开发预览：新Boss人物与技能未完成"));if(MenuState==EHeliosAdventureMenu::RestartConfirm && (bNewTownConfirm||IsTownWorld()))return FText::FromString(TEXT("将重新开始生存并清空当前地图探索与彩蛋存档。最高记录会保留。此操作无法撤销。"));if(MenuState==EHeliosAdventureMenu::WeaponSelect&&IsPendingTownMap())return FText::FromString(SelectionError.IsEmpty()?TEXT("持续生存 · 8 个街区 · 100 处彩蛋 · 探索进度自动保存"):SelectionError);if(MenuState==EHeliosAdventureMenu::WeaponSelect)return FText::FromString(SelectionError.IsEmpty()?TEXT("每局携带一件装备 · 无限备用弹药 · 开场准备 10 秒"):SelectionError);auto*D=GetDirector();if(MenuState==EHeliosAdventureMenu::Death&&D){int32 S=FMath::FloorToInt(D->GetSurvivalSeconds());return FText::FromString(FString::Printf(TEXT("本次练习 %02d:%02d   热度 %d\n淘汰 %d   BOSS 收官 %d   解锁分区 %d / 7\n两年半后再回首：最高热度 %d"),S/60,S%60,D->GetScore(),D->GetKills(),D->GetBossKills(),D->GetExploredCount(),D->GetBestScore()));}if(MenuState==EHeliosAdventureMenu::RestartConfirm)return FText::FromString(TEXT("当前舞台进度会结束。再次按 F5 也可确认。"));return FText::FromString(TEXT("计时与敌人已暂停。调整状态，随时回到舞台。"));}

bool AHeliosAdventureHUD::IsTownWorld() const { return UHeliosTownSubsystem::IsTownMap(GetWorld()); }
bool AHeliosAdventureHUD::IsPendingTownMap() const { const auto* Map=FHeliosMapRegistry::Find(PendingMapId); return Map && Map->UsesTownSystems(); }
FName AHeliosAdventureHUD::GetCurrentMapAsset() const { const auto* Map=FHeliosMapRegistry::ForWorld(GetWorld()); return Map?Map->AssetMapName():FName(TEXT("HeliosAdventure")); }
void AHeliosAdventureHUD::SelectMap(FName Id) { if(const auto* Map=FHeliosMapRegistry::Find(Id))if(MenuState==EHeliosAdventureMenu::WeaponSelect) { PendingMapId=Map->AssetMapName(); SelectionError.Empty();if(auto* Travel=GetGameInstance()->GetSubsystem<UHeliosMapTravelSubsystem>())Travel->bResetTownOnArrival=false; } }
void AHeliosAdventureHUD::ToggleTownMap()
{
    if(!IsTownWorld())return;
    if(MenuState==EHeliosAdventureMenu::TownMap){ContinueRun();return;}
    if(MenuState!=EHeliosAdventureMenu::None)return;
    MenuState=EHeliosAdventureMenu::TownMap;
    if(auto* D=GetDirector())D->SetRunPaused(true);
    ApplyMenuInput(true);
}
void AHeliosAdventureHUD::InteractTown()
{
    if(!IsTownWorld()||MenuState!=EHeliosAdventureMenu::None)return;
    if(auto* T=GetWorld()->GetSubsystem<UHeliosTownSubsystem>())T->Interact(Cast<AHeliosAdventureCharacter>(PlayerOwner->GetPawn()));
}
void AHeliosAdventureHUD::RequestNewTown()
{
    bNewTownConfirm=true;MenuBeforeConfirm=MenuState;MenuState=EHeliosAdventureMenu::RestartConfirm;
    if(auto* D=GetDirector())D->SetRunPaused(true);ApplyMenuInput(true);
}
FText AHeliosAdventureHUD::GetTownInteractionText() const
{
    if(!IsTownWorld())return FText::GetEmpty();
    if(auto* T=GetWorld()->GetSubsystem<UHeliosTownSubsystem>()) { auto State=HeliosMCP::Parse(T->GetStateJson()); return FText::FromString(HeliosMCP::String(State,TEXT("interaction_prompt"))); }
    return FText::GetEmpty();
}
FText AHeliosAdventureHUD::GetControlsText() const
{
    const auto* P=PlayerOwner?Cast<AHeliosArenaCharacter>(PlayerOwner->GetPawn()):nullptr;
    const FName Id=P?P->GetWeaponId():NAME_None;
    const auto CharacterControls=[P](const TCHAR* Controls)
    {
        FString Result(Controls);
        if(P)if(const auto* Character=FHeliosCharacterRegistry::FindCharacter(P->GetLoadout().CharacterId))
        {
            if(Character->Forms.Num()>1)Result+=TEXT(" · H 人物形态");
            if(Character->CharacterId==TEXT("cfm_guanxiaoyu")){Result+=TEXT(" · ")+P->GetGuanXiaoYuAbility()->StatusText();if(!P->GetGuanXiaoYuAbility()->LastError.IsEmpty())Result+=TEXT(" · ")+P->GetGuanXiaoYuAbility()->LastError;}
            if(Character->CharacterId==TEXT("cfm_yun_summer"))Result+=TEXT(" · J 敬酒互动");
            if(Character->CharacterId==TEXT("cfm_qingya"))Result+=TEXT("\n")+P->GetQingYaAbility()->StatusText();
        }
        return FText::FromString(Result);
    };
    if(P&&P->GetGuanXiaoYuAbility()->IsUsingUmbrella())return CharacterControls(TEXT("左键 轻击 · 右键 重击 / 剑伞盾 · Esc 暂停"));
    if(IsFighterHUD())return FText::FromString(TEXT("WASD 飞行 · 空格 / Ctrl 升降 · Shift 切换加速 · 左键 激光 · 右键 导弹 · Esc 暂停"));
    if(Id==TEXT("mech"))return FText::FromString(TEXT("按住左键 交替重踏 · 按住右键 毁灭激光 · WASD 移动 · Shift 切换奔跑 · Esc 暂停"));
    if(Id==TEXT("cfm_yun_summer_megatron"))return CharacterControls(TEXT("左键 连续射击 · R 换弹 · G 展示 · Esc 暂停"));
    if(Id==TEXT("cfm_zhongxiaokui"))return CharacterControls(TEXT("左键 攻击 · Q 武器形态 · V 飞镖 · G 技能 · B形态右键剑气 · R 装填 · Esc 暂停"));
    if(Id==TEXT("cfm_sunshangxiang"))return CharacterControls(TEXT("左键 双刃连击 · G 终极技能 · WASD 移动 · Esc 暂停"));
    if(Id==TEXT("cfm_heilonghun"))return CharacterControls(TEXT("左键 单发重炮 · 命中8米爆炸 · R 换弹 · 后台装填 · Esc 暂停"));
    if(Id==TEXT("cfm_wangzhekui_thunder"))return CharacterControls(TEXT("按住左键 连续射击 · 右键 霰弹 / 独头弹 · R 换弹 · G 展示 · Esc 暂停"));
    if(Id==TEXT("cfm_chan_zeus")||Id==TEXT("cfm_bulianshi_p90"))return CharacterControls(TEXT("按住左键 连续射击 · R 换弹 · G 展示 · WASD 移动 · Esc 暂停"));
    if(Id==TEXT("mysterious_hand"))return CharacterControls(TEXT("左键 激光60米 · 右键 辅助选中 · 定点轰炸200米 · 无需弹药 · WASD 移动 · Esc 暂停"));
    if(Id==TEXT("calamity"))return CharacterControls(TEXT("左键 随机四灾 · 按住 连续施法 · 右键 化塔20秒 · 12米部署 / 35米索敌 · Esc 暂停"));
    if(Id==TEXT("qingxiao"))return CharacterControls(TEXT("左键 轻击5米 · 轻快连斩 · 右键 五剑归元 · 100米索敌 · 飞剑遇墙爆开 · 五剑消散即可再召 · Esc 暂停"));
    if(Id==TEXT("soulbanner"))return CharacterControls(TEXT("按住左键 挥幡三连击 · 右键 插旗召唤 · 每旗两近战一术士 · WASD 移动 · Esc 暂停"));
    if(Id==TEXT("lightstick"))return CharacterControls(TEXT("按住左键 高速三连斩 · 右键蓄力 / 松开 180°扩散剑气 · WASD 移动 · Esc 暂停"));
    if(Id==TEXT("cola"))return CharacterControls(IsTownWorld()?TEXT("左键 投掷 · R 补充 · WASD 移动 · E 互动 · M 地图 · Esc 暂停"):TEXT("左键 投掷 · R 补充 · WASD 移动 · Shift 切换奔跑 · Esc 暂停"));
    return CharacterControls(IsTownWorld()?TEXT("WASD 移动 · Shift 切换奔跑 · R 装填 · E 互动 · M 区域地图 · Esc 暂停"):TEXT("WASD 移动 · Shift 切换奔跑 · R 装填 · Esc 中场休息 · F5 再练一局"));
}


void AHeliosAdventureHUD::TickBossBar(float D)
{
    const bool Active=HasActiveBoss();
    BossBarOpacity=FMath::FInterpConstantTo(BossBarOpacity,Active?1.f:0.f,D,3.5f);
    BossHitFlash=FMath::Max(0.f,BossHitFlash-D*4.f);
    if(!Active)
    {
        // Do not leave a sliver of health or a hit glint during the defeat fade.
        BossDisplayHP=BossTrailHP=BossLastHP=0.f;
        BossHitFlash=0.f;BossTrailDelay=0.f;
        if(BossBarOpacity<=0.f) BossBarNumber=INDEX_NONE;
        return;
    }
    const float HP=GetBossHealthFraction();
    if(BossBarNumber!=PrimaryBoss->GetBossNumber())
    {
        BossBarNumber=PrimaryBoss->GetBossNumber();BossDisplayHP=BossTrailHP=BossLastHP=HP;
        BossTrailDelay=0.f;BossHitFlash=0.f;
    }
    BossBarTitle=Cast<AHeliosUrbanBoss>(PrimaryBoss)
        ? FString::Printf(TEXT("都市巨人 · 篮球风暴 / 第 %d 阶段"),PrimaryBoss->GetBossPhase())
        : FString::Printf(TEXT("铁山靠  /  BOSS %02d      第 %d 阶段"),BossBarNumber,PrimaryBoss->GetBossPhase());
    if(HP<BossLastHP-.0001f) {BossTrailDelay=.35f;BossHitFlash=1.f;}
    BossLastHP=HP;
    BossDisplayHP=FMath::FInterpTo(BossDisplayHP,HP,D,15.f);
    BossTrailDelay=FMath::Max(0.f,BossTrailDelay-D);
    if(BossTrailDelay<=0.f) BossTrailHP=FMath::FInterpTo(BossTrailHP,HP,D,3.5f);
    BossTrailHP=FMath::Max(BossTrailHP,BossDisplayHP);
}

void AHeliosAdventureHUD::DrawHUD()
{
    Super::DrawHUD();
    if(MenuState!=EHeliosAdventureMenu::None || !PlayerOwner)return;
    if(auto* P=Cast<AHeliosArenaCharacter>(PlayerOwner->GetPawn()))
    {
        if(auto* H=Cast<AHeliosMysteriousHand>(P->GetEquippedWeapon()))H->DrawHandHUD(Canvas);
        if(auto* M=Cast<AHeliosMechWeapon>(P->GetEquippedWeapon()))M->DrawMechHUD(Canvas);
        if(auto* F=Cast<AHeliosFighterWeapon>(P->GetEquippedWeapon()))F->DrawFighterHUD(Canvas);
    }
}

bool AHeliosAdventureHUD::IsFighterHUD() const { auto* P=PlayerOwner?Cast<AHeliosArenaCharacter>(PlayerOwner->GetPawn()):nullptr;return P&&P->IsFighterMode(); }
