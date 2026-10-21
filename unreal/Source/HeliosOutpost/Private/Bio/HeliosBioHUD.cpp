#include "Bio/HeliosBioHUD.h"
#include "Bio/HeliosBioHotelGameMode.h"
#include "Bio/HeliosBioPlayer.h"
#include "Bio/HeliosBioSupply.h"
#include "Bio/HeliosBioArt.h"
#include "Bio/HeliosBioRoundInventoryComponent.h"
#include "Bio/HeliosBioSupplyWeaponDefinition.h"
#include "World/HeliosMapDefinition.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/GameViewportClient.h"
#include "Components/InputComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

void AHeliosBioHUD::BeginPlay()
{
    Super::BeginPlay();
    const auto Font=[](int32 Size){return FSlateFontInfo(FPaths::ProjectContentDir()/TEXT("UI/Fonts/SimHei.ttf"),Size);};
    auto* M=AHeliosBioHotelGameMode::Find(GetWorld());
    if(M&&M->bFloatingProfile)
    {
        SupplyLabelFont=NewObject<UFont>(this);SupplyLabelFont->FontCacheType=EFontCacheType::Runtime;
        SupplyLabelFont->LegacyFontSize=16;SupplyLabelFont->LegacyFontName=TEXT("Regular");
        SupplyLabelFont->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.Emplace(TEXT("Regular"),FPaths::ProjectContentDir()/TEXT("UI/Fonts/SimHei.ttf"),EFontHinting::Default,EFontLoadingPolicy::LazyLoad);
    }
    TSharedRef<SVerticalBox> Choices=SNew(SVerticalBox);
    if(M)for(const auto& Option:M->HunterOptions())
    {
        const FName RoleId=Option.Id;
        const FString Label=Option.Name+TEXT(" · ")+Option.Description+(Option.bAvailable?TEXT(""):TEXT("（暂未开放）"));
        Choices->AddSlot().AutoHeight().Padding(0,4)
        [SNew(SButton).IsEnabled(Option.bAvailable).OnClicked_Lambda([this,RoleId]{if(auto* G=AHeliosBioHotelGameMode::Find(GetWorld()))G->SelectHunter(0,RoleId);return FReply::Handled();})
            [SNew(STextBlock).Font(Font(19)).Text(FText::FromString(Label))]];
    }
    Root=SNew(SOverlay)
    +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(28)
    [SNew(SBorder).BorderBackgroundColor(FLinearColor(.025f,.04f,.06f,.8f)).Padding(18)
        [SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text_Lambda([this]{const auto* D=FHeliosMapRegistry::ForWorld(GetWorld());return FText::FromString((D?D->DisplayName.ToString():FString(TEXT("生化地图")))+TEXT("  |  单机生化"));}).Font(Font(26)).ColorAndOpacity(FLinearColor(.4f,.85f,1))]
        +SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(STextBlock).Font(Font(19)).Text_Lambda([this]
        {auto* G=AHeliosBioHotelGameMode::Find(GetWorld());if(!G)return FText::GetEmpty();const int32 Sec=FMath::CeilToInt(G->Model.GetPhaseTimeRemaining());return FText::FromString(FString::Printf(TEXT("第 %d 回合  ·  %s %02d:%02d  ·  幸存 %d  /  %d"),G->RoundSerial,G->Model.GetPhase()==EHeliosBioPhase::Preparation?TEXT("感染倒数"):TEXT("剩余"),Sec/60,Sec%60,G->Model.CountLivingHumanSide(),G->Model.GetParticipants().Num()));})]
        +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Font(16)).Text_Lambda([this]
        {auto* G=AHeliosBioHotelGameMode::Find(GetWorld());return FText::FromString(G?FString::Printf(TEXT("人类胜场 %d    生化幽灵胜场 %d"),G->HumanWins,G->ZombieWins):TEXT(""));})]]]
    +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0,175,0,0)
    [SNew(STextBlock).Font(Font(24)).ColorAndOpacity(FLinearColor(1,.8f,.35f)).Text_Lambda([this]
    {auto* G=AHeliosBioHotelGameMode::Find(GetWorld());return FText::FromString(G&&G->NoticeSeconds>0?G->Notice:TEXT(""));})]
    +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(28)
    [SNew(SBorder).BorderBackgroundColor(FLinearColor(.025f,.04f,.06f,.8f)).Padding(18)
        [SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Font(25)).Text_Lambda([this]
        {auto* G=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=G?G->Model.FindParticipant(0):nullptr;if(!P)return FText::GetEmpty();const FString State=P->bAlive?FString():P->bPermanentlyDead?FString(TEXT("  本回合已被淘汰 · ← → 切换观战")):FString::Printf(TEXT("  复活 %.1f s · ← → 观战"),P->RespawnSeconds);return FText::FromString(FString::Printf(TEXT("%s  |  HP %.0f / %.0f%s"),*HeliosBioArt::TeamName(P->Team),P->Health,P->MaxHealth,*State));})]
        +SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(STextBlock).Font(Font(20)).Text_Lambda([this]
        {auto* G=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=G?G->Model.FindParticipant(0):nullptr;if(!P||!G->Player)return FText::GetEmpty();return FText::FromString(P->Team==EHeliosBioTeam::Human?FString::Printf(TEXT("弹药  %d / %d   %s"),G->Player->GetAmmo(),G->Player->GetReserveAmmo(),G->Player->IsReloading()?TEXT("换弹中"):TEXT("")):FString::Printf(TEXT("F %s  ·  %s   进化 %d"),*G->ParticipantSkillDescription(0),P->SkillCooldownRemaining>0?*FString::Printf(TEXT("冷却 %.1f s"),P->SkillCooldownRemaining):TEXT("就绪"),P->EvolutionLevel));})]
        +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Font(15)).Text_Lambda([this]
        {auto* G=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=G?G->Model.FindParticipant(0):nullptr;if(!P)return FText::GetEmpty();FString Hint;if(P->bHunterChoicePending)Hint=TEXT("蓝箱资格已获得 · E 选择猎手");else if(G->Model.CanTransform(0))Hint=TEXT("E 变身幽灵猎手 · 蓝箱可进阶");else if(P->Team==EHeliosBioTeam::GhostHunter)Hint=TEXT("拾取蓝箱，选择高级猎手");else if(P->Team==EHeliosBioTeam::Zombie&&P->bMother&&P->ZombieForm==EHeliosBioZombieForm::Terminator)Hint=G->Model.Config.bAbyssSourceAvailable?TEXT("满级后拾红箱进化深渊"):TEXT("红箱深渊分支正在补齐");if(P->FrozenSeconds>0)Hint=FString::Printf(TEXT("冻结 %.1f s"),P->FrozenSeconds);else if(P->ShieldRemaining>0)Hint=FString::Printf(TEXT("护盾 %.1f s"),P->ShieldRemaining);return FText::FromString(Hint);})]
        +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Font(15)).Text_Lambda([this]
        {auto* G=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=G?G->Model.FindParticipant(0):nullptr;return P?FText::FromString(FString::Printf(TEXT("本回合 击杀 %d · 感染 %d · 补给 %d"),P->Kills,P->Infections,P->SuppliesCollected)):FText::GetEmpty();})]
        +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Font(15)).Text_Lambda([this]
        {auto* G=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=G?G->Model.FindParticipant(0):nullptr;if(!P||!P->bAlive||!P->IsHunter())return FText::GetEmpty();
        if(P->HunterRoleId==TEXT("BioMechanicalHunter"))return FText::FromString(FString::Printf(TEXT("1 机械臂 · 2 XM214  能量 %.0f/%.0f · G 奥术雷 %s"),P->WeaponEnergy,P->WeaponMaxEnergy,P->SecondaryCooldownRemaining>0?*FString::Printf(TEXT("%.1fs"),P->SecondaryCooldownRemaining):TEXT("就绪")));
        if(P->HunterRoleId==TEXT("BioRedeemerHunter"))return FText::FromString(FString::Printf(TEXT("G 救赎血清 · 僵尸恢复人类 / 猎手回满血 · %s"),P->SecondaryCooldownRemaining>0?*FString::Printf(TEXT("冷却 %.1fs"),P->SecondaryCooldownRemaining):TEXT("就绪")));
        if(P->HunterRoleId==TEXT("BioZhongXiaoKuiHunter"))return FText::FromString(FString::Printf(TEXT("G 傩·刃解 %s%s"),P->SecondaryCooldownRemaining>0?*FString::Printf(TEXT("冷却 %.1fs"),P->SecondaryCooldownRemaining):TEXT("就绪"),P->bGhostMaskActive?*FString::Printf(TEXT(" · 鬼面 %.1fs%s"),P->SecondarySkillRemaining,P->bFirstAttackSwordWave||P->bHeavySwordWaveReady?TEXT(" · 剑气就绪"):TEXT("")):TEXT("")));
        return FText::GetEmpty();})]
        +SVerticalBox::Slot().AutoHeight().Padding(0,5)[SNew(STextBlock).Font(Font(15)).AutoWrapText(true).WrapTextAt(620).Text_Lambda([this]
        {auto* G=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=G?G->Model.FindParticipant(0):nullptr;auto* Inventory=G&&G->Player?G->Player->GetRoundInventory():nullptr;return P&&P->bAlive&&P->Team==EHeliosBioTeam::Human&&Inventory&&Inventory->IsEnabled()?FText::FromString(Inventory->Describe()+TEXT("\n1 原武器 · 2 榴弹 · 3 火箭 · 4 震撼弹 · 5 机枪 · 滚轮切换")):FText::GetEmpty();})]
        +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Font(15)).Text(FText::FromString(TEXT("WASD 移动 · 空格跳跃 · C 蹲伏 · 左/右键攻击 · R 换弹\nE 猎手变身 · F 角色技能 · F5 下一局 · F6 地图菜单 · Esc 暂停")))]]]
    +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
    [SNew(SBorder).BorderBackgroundColor(FLinearColor(.02f,.03f,.05f,.95f)).Padding(35).Visibility_Lambda([this]
        {auto* G=AHeliosBioHotelGameMode::Find(GetWorld());return GetWorld()->IsPaused()||(G&&G->Model.GetPhase()==EHeliosBioPhase::RoundOver)?EVisibility::Visible:EVisibility::Collapsed;})
        [SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,20)[SNew(STextBlock).Font(Font(30)).Text_Lambda([this]
        {auto* G=AHeliosBioHotelGameMode::Find(GetWorld());return FText::FromString(G&&G->Model.GetPhase()==EHeliosBioPhase::RoundOver?(G->Model.GetWinner()==EHeliosBioWinner::Humans?TEXT("人类胜利"):TEXT("生化幽灵胜利")):TEXT("已暂停"));})]
        +SVerticalBox::Slot().AutoHeight().Padding(0,5)[SNew(SButton).OnClicked_Lambda([this]{if(auto* G=AHeliosBioHotelGameMode::Find(GetWorld())){if(G->Model.GetPhase()==EHeliosBioPhase::RoundOver)G->RestartBioRound();else G->PauseBio();}return FReply::Handled();})[SNew(STextBlock).Font(Font(22)).Text(FText::FromString(TEXT("继续 / 下一回合")))]]
        +SVerticalBox::Slot().AutoHeight().Padding(0,5)[SNew(SButton).OnClicked_Lambda([this]{if(auto* G=AHeliosBioHotelGameMode::Find(GetWorld()))G->ReturnToMapMenu();return FReply::Handled();})[SNew(STextBlock).Font(Font(22)).Text(FText::FromString(TEXT("返回地图菜单")))]]]];
    StaticCastSharedPtr<SOverlay>(Root)->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)
    [SNew(SBorder).BorderBackgroundColor(FLinearColor(.02f,.025f,.04f,.97f)).Padding(30)
        .Visibility_Lambda([this]{auto* G=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=G?G->Model.FindParticipant(0):nullptr;return G&&G->bHunterChooserOpen&&P&&P->bAlive&&P->bHunterChoicePending?EVisibility::Visible:EVisibility::Collapsed;})
        [SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,16)[SNew(STextBlock).Font(Font(27)).ColorAndOpacity(FLinearColor(.35f,.8f,1.f)).Text(FText::FromString(TEXT("选择高级猎手"))) ]
        +SVerticalBox::Slot().AutoHeight()[Choices]
        +SVerticalBox::Slot().AutoHeight().Padding(0,12,0,0)[SNew(SButton).OnClicked_Lambda([this]{if(auto* G=AHeliosBioHotelGameMode::Find(GetWorld()))G->CloseHunterChooser();return FReply::Handled();})[SNew(STextBlock).Font(Font(17)).Text(FText::FromString(TEXT("稍后选择 · 按 E 再打开")))]]]];
    if(GEngine&&GEngine->GameViewport)GEngine->GameViewport->AddViewportWidgetContent(Root.ToSharedRef(),50);
    if(PlayerOwner&&M)
    {InputPriority=30;EnableInput(PlayerOwner);if(InputComponent){auto& Escape=InputComponent->BindKey(EKeys::Escape,IE_Pressed,M,&AHeliosBioHotelGameMode::PauseBio);Escape.bExecuteWhenPaused=true;Escape.bConsumeInput=true;auto& Menu=InputComponent->BindKey(EKeys::F6,IE_Pressed,M,&AHeliosBioHotelGameMode::ReturnToMapMenu);Menu.bExecuteWhenPaused=true;}}
}
void AHeliosBioHUD::EndPlay(const EEndPlayReason::Type Reason)
{if(Root.IsValid()&&GEngine&&GEngine->GameViewport)GEngine->GameViewport->RemoveViewportWidgetContent(Root.ToSharedRef());Root.Reset();Super::EndPlay(Reason);}
void AHeliosBioHUD::DrawHUD()
{
    Super::DrawHUD();if(!Canvas)return;
    const float X=Canvas->ClipX*.5f,Y=Canvas->ClipY*.5f;
    DrawLine(X-12,Y,X-4,Y,FLinearColor::White,1.2f);DrawLine(X+4,Y,X+12,Y,FLinearColor::White,1.2f);DrawLine(X,Y-12,X,Y-4,FLinearColor::White,1.2f);DrawLine(X,Y+4,X,Y+12,FLinearColor::White,1.2f);
    auto* G=AHeliosBioHotelGameMode::Find(GetWorld());if(!G)return;
    if(G->Model.CanTransform(0))DrawText(TEXT("E - HUNTER"),FLinearColor::Yellow,X-60,Y+35,nullptr,1.2f);
    for(auto S:G->Supplies)if(S&&S->IsAvailable())
    {const FVector Screen=Project(S->GetActorLocation()+FVector(0,0,110));const bool Red=S->GetKind()==EHeliosBioSupplyKind::RedTerminatorEvolution,Blue=S->GetKind()==EHeliosBioSupplyKind::BlueHunterUpgrade;auto* Content=S->GetWeaponContent();const FString Label=Content?Content->DisplayName.ToString():Red?TEXT("ABYSS BOX"):Blue?TEXT("HUNTER BOX"):TEXT("SUPPLY");if(Screen.Z>0&&Screen.X>20&&Screen.X<Canvas->ClipX-90&&Screen.Y>20&&Screen.Y<Canvas->ClipY-20)DrawText(Label,Red?FLinearColor(1,.2f,.15f):Blue?FLinearColor(.2f,.65f,1.f):FLinearColor(.4f,1,.4f),Screen.X,Screen.Y,Content?SupplyLabelFont.Get():nullptr);}
}
