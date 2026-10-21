#include "UI/HeliosMenuWidget.h"
#include "UI/HeliosAdventureHUD.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "Town/HeliosTownSubsystem.h"
#include "World/HeliosMapDefinition.h"
#include "MCP/HeliosMCPJson.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
FSlateFontInfo TownFont(int32 Size){return FSlateFontInfo(FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()/TEXT("UI/Fonts/SimHei.ttf")),Size);}
class STownOverview final: public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(STownOverview){} SLATE_ARGUMENT(TWeakObjectPtr<AHeliosAdventureHUD>,HUD) SLATE_END_ARGS()
    void Construct(const FArguments& A)
    {
        HUD=A._HUD;FString Data;
        const auto* Definition=HUD.IsValid()?FHeliosMapRegistry::ForWorld(HUD->GetWorld()):nullptr;
        if(!Definition)return;
        MapTitle=Definition->DisplayName.ToString();
        if(FFileHelper::LoadFileToString(Data,*(FPaths::ProjectContentDir()/Definition->TownConfigPath)))
        {
            const auto Config=HeliosMCP::Parse(Data);const TSharedPtr<FJsonObject>* Geometry=nullptr;
            if(Config&&Config->TryGetObjectField(TEXT("map_geometry"),Geometry))MapGeometry=*Geometry;
            const TArray<TSharedPtr<FJsonValue>>* Size=nullptr;
            if(Config&&Config->TryGetArrayField(TEXT("size_m"),Size)&&Size->Num()==2)
                MapSize=FVector2D(FMath::Max(1.,(*Size)[0]->AsNumber()),FMath::Max(1.,(*Size)[1]->AsNumber()));
        }
    }
    virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(1280,900);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& E,int32 L,const FWidgetStyle&,bool)const override
    {
        if(!HUD.IsValid())return L;
        const auto* Brush=FCoreStyle::Get().GetBrush("WhiteBrush");
        auto Box=[&](FVector2f P,FVector2f S,FLinearColor C){FSlateDrawElement::MakeBox(E,L,G.ToPaintGeometry(S,FSlateLayoutTransform(P)),Brush,ESlateDrawEffect::None,C);};
        auto Text=[&](FVector2f P,const FString& S,int32 Size,FLinearColor C){FSlateDrawElement::MakeText(E,L+2,G.ToPaintGeometry(FVector2f(600,50),FSlateLayoutTransform(P)),S,TownFont(Size),ESlateDrawEffect::None,C);};
        const FLinearColor Paper(.82f,.87f,.86f),Muted(.42f,.53f,.54f),Accent(.90f,.68f,.27f),Blue(.32f,.79f,.93f);
        Box(FVector2f(0,0),FVector2f(1280,900),FLinearColor(.015f,.026f,.03f,.99f));
        Text(FVector2f(56,34),MapTitle,28,Paper);
        Text(FVector2f(56,80),FString::Printf(TEXT("%.0f × %.0f 米     北 ↑     M / Esc 返回探索"),MapSize.X,MapSize.Y),15,Muted);
        const float Scale=660.f/FMath::Max(MapSize.X,MapSize.Y);
        const FVector2f MapExtent(MapSize.X*Scale,MapSize.Y*Scale);
        const FVector2f Origin(56.f+(660.f-MapExtent.X)*.5f,142.f+(660.f-MapExtent.Y)*.5f);
        auto Point=[&](FVector V){return Origin+FVector2f((V.X+MapSize.X*.5f)*Scale,(MapSize.Y*.5f-V.Y)*Scale);};
        auto MapLabelPosition=[&](FVector2f Marker,const FString& Label,int32 Size)
        {
            const FVector2D Measured=FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Label,TownFont(Size));
            const float Width=static_cast<float>(Measured.X),Height=static_cast<float>(Measured.Y);
            float X=Marker.X+9.f;
            if(X+Width>Origin.X+MapExtent.X-6.f)X=Marker.X-9.f-Width;
            return FVector2f(FMath::Clamp(X,Origin.X+6.f,FMath::Max(Origin.X+6.f,Origin.X+MapExtent.X-6.f-Width)),
                FMath::Clamp(Marker.Y-10.f,Origin.Y+6.f,FMath::Max(Origin.Y+6.f,Origin.Y+MapExtent.Y-6.f-Height)));
        };
        Box(Origin,MapExtent,FLinearColor(.035f,.065f,.067f));
        for(int I=0;I<=10;++I){Box(Origin+FVector2f(I*MapExtent.X/10.f,0),FVector2f(1,MapExtent.Y),FLinearColor(.07f,.105f,.11f));Box(Origin+FVector2f(0,I*MapExtent.Y/10.f),FVector2f(MapExtent.X,1),FLinearColor(.07f,.105f,.11f));}
        // This geometry is exported from the same road/canal/bridge definitions as the built world.
        if(MapGeometry)
        {
            // Optional v4 scenery has no discovery or save-game identity.
            for(const TCHAR* Field:{TEXT("waters"),TEXT("facilities")})
            {
                const TArray<TSharedPtr<FJsonValue>>* Items=nullptr;
                if(!MapGeometry->TryGetArrayField(Field,Items))continue;
                const bool Water=FCString::Strcmp(Field,TEXT("waters"))==0;
                for(const auto& V:*Items)
                {
                    const auto Item=V->AsObject();if(!Item)continue;FVector Position,Size;
                    if(!HeliosMCP::ReadVector(Item,TEXT("position"),Position)||!HeliosMCP::ReadVector(Item,TEXT("size"),Size))continue;
                    Box(Point(Position)-FVector2f(Size.X*Scale*.5f,Size.Y*Scale*.5f),FVector2f(Size.X*Scale,Size.Y*Scale),
                        Water?FLinearColor(.035f,.16f,.21f):FLinearColor(.26f,.30f,.22f));
                }
            }
            const TArray<TSharedPtr<FJsonValue>>* Buildings=nullptr;
            if(MapGeometry->TryGetArrayField(TEXT("buildings"),Buildings))for(const auto& V:*Buildings)
            {
                const auto Building=V->AsObject();if(!Building)continue;
                FVector Position,Size;
                if(!HeliosMCP::ReadVector(Building,TEXT("position"),Position)||!HeliosMCP::ReadVector(Building,TEXT("size"),Size))continue;
                bool Hero=false;Building->TryGetBoolField(TEXT("hero"),Hero);
                Box(Point(Position)-FVector2f(Size.X*Scale*.5f,Size.Y*Scale*.5f),FVector2f(Size.X*Scale,Size.Y*Scale),Hero?FLinearColor(.36f,.29f,.19f):FLinearColor(.16f,.18f,.15f));
            }
            const TSharedPtr<FJsonObject>* Canal=nullptr;
            if(MapGeometry->TryGetObjectField(TEXT("canal"),Canal))
            {
                FVector Center,Size;HeliosMCP::ReadVector(*Canal,TEXT("center"),Center);HeliosMCP::ReadVector(*Canal,TEXT("size"),Size);
                Box(Point(Center)-FVector2f(Size.X*Scale/2,Size.Y*Scale/2),FVector2f(Size.X*Scale,Size.Y*Scale),FLinearColor(.035f,.16f,.21f));
            }
            const TArray<TSharedPtr<FJsonValue>>* Roads=nullptr;
            if(MapGeometry->TryGetArrayField(TEXT("roads"),Roads))for(const auto& V:*Roads)
            {
                const auto Road=V->AsObject();if(!Road)continue;
                FVector Start,End;double Width=12;Road->TryGetNumberField(TEXT("width_m"),Width);
                if(!HeliosMCP::ReadVector(Road,TEXT("start"),Start)||!HeliosMCP::ReadVector(Road,TEXT("end"),End))continue;
                TArray<FVector2D> Points{FVector2D(Point(Start)),FVector2D(Point(End))};
                FSlateDrawElement::MakeLines(E,L+1,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,FLinearColor(.17f,.22f,.22f),true,FMath::Max(1.f,static_cast<float>(Width)*Scale));
            }
            const TArray<TSharedPtr<FJsonValue>>* Bridges=nullptr;
            if(MapGeometry->TryGetArrayField(TEXT("bridges"),Bridges))for(const auto& V:*Bridges)
            {
                const auto Bridge=V->AsObject();if(!Bridge)continue;FVector Center,Size;
                HeliosMCP::ReadVector(Bridge,TEXT("center"),Center);HeliosMCP::ReadVector(Bridge,TEXT("size"),Size);
                const auto P=Point(Center);Box(P-FVector2f(Size.X*Scale/2,Size.Y*Scale/2),FVector2f(Size.X*Scale,Size.Y*Scale),FLinearColor(.42f,.43f,.34f));
                Text(P+FVector2f(17,-10),TEXT("桥"),12,Accent);
            }
        }
        auto* Town=HUD->GetWorld()->GetSubsystem<UHeliosTownSubsystem>();
        if(!Town)return L+3;
        auto State=HeliosMCP::Parse(Town->GetStateJson());
        const TArray<TSharedPtr<FJsonValue>>* Zones=nullptr;
        int32 Row=0;
        if(State&&State->TryGetArrayField(TEXT("zones"),Zones))for(const auto& V:*Zones)
        {
            const auto Z=V->AsObject();if(!Z)continue;
            FVector Center,Checkpoint;HeliosMCP::ReadVector(Z,TEXT("center"),Center);HeliosMCP::ReadVector(Z,TEXT("checkpoint"),Checkpoint);
            bool Explored=false;Z->TryGetBoolField(TEXT("explored"),Explored);
            const auto P=Point(Center);const FString Name=HeliosMCP::String(Z,TEXT("name"));
            Box(P-FVector2f(4,4),FVector2f(8,8),Explored?Paper:Muted);
            Text(MapLabelPosition(P,Name,13),Name,13,Explored?Paper:Muted);
            const auto C=Point(Checkpoint);Box(C-FVector2f(3,3),FVector2f(6,6),Accent);
            Text(FVector2f(776,354+Row*34),FString::Printf(TEXT("%s  %s"),Explored?TEXT("●"):TEXT("○"),*Name),18,Explored?Paper:Muted);++Row;
        }
        const auto Checkpoint=Point(Town->GetCheckpointLocation()/100.f);
        Box(Checkpoint-FVector2f(6,6),FVector2f(12,12),Accent);
        if(auto* Player=UGameplayStatics::GetPlayerCharacter(HUD->GetWorld(),0))
        {
            auto P=Point(Player->GetActorLocation()/100.f);P.X=FMath::Clamp(P.X,Origin.X,Origin.X+MapExtent.X);P.Y=FMath::Clamp(P.Y,Origin.Y,Origin.Y+MapExtent.Y);
            Box(P-FVector2f(6,6),FVector2f(12,12),Blue);Text(MapLabelPosition(P+FVector2f(0,18),TEXT("你在这里"),14),TEXT("你在这里"),14,Blue);
        }
        Text(FVector2f(776,145),TEXT("探索记录"),25,Paper);
        Text(FVector2f(776,194),FString::Printf(TEXT("街区 %d / 8   彩蛋 %d / 100"),Town->GetExploredDistrictCount(),Town->GetDiscoveredMemeCount()),18,Paper);
        if(auto* Director=AHeliosAdventureDirector::Find(HUD->GetWorld()))
        {
            const int32 Seconds=FMath::Max(0,FMath::FloorToInt(Director->GetSurvivalSeconds()));
            const int32 Next=FMath::Max(0,FMath::CeilToInt(Director->GetSecondsUntilNextBoss()));
            Text(FVector2f(776,232),FString::Printf(TEXT("本次 %02d:%02d   淘汰 %d"),Seconds/60,Seconds%60,Director->GetKills()),18,Paper);
            Text(FVector2f(776,270),FString::Printf(TEXT("普通敌人 %d / %d   首领 %d   候场 %d"),Director->GetAliveCount(),Director->GetEnemyCap(),Director->GetBossCount(),Director->GetPendingBossCount()),16,Accent);
            Text(FVector2f(776,308),FString::Printf(TEXT("下位首领 %02d:%02d"),Next/60,Next%60),18,Accent);
        }
        Text(FVector2f(776,658),TEXT("■ 蓝色：当前位置"),16,Blue);
        Text(FVector2f(776,694),TEXT("■ 金色：检查点"),16,Accent);
        Text(FVector2f(776,740),TEXT("阵亡后随机区域满血复活"),15,Muted);
        Text(FVector2f(56,830),TEXT("持续生存 · 探索与彩蛋自动保存 · 靠近互动点按 E 查看"),16,Muted);
        return L+3;
    }
private:TWeakObjectPtr<AHeliosAdventureHUD> HUD;TSharedPtr<FJsonObject> MapGeometry;FVector2D MapSize=FVector2D(1000,1000);FString MapTitle;
};
}
TSharedRef<SWidget> MakeHeliosTownOverview(TWeakObjectPtr<AHeliosAdventureHUD> HUD)
{
    return SNew(SScaleBox).Stretch(EStretch::ScaleToFit).Visibility_Lambda([HUD]{return HUD.IsValid()&&HUD->GetMenuState()==EHeliosAdventureMenu::TownMap?EVisibility::Visible:EVisibility::Collapsed;})
        [SNew(SBox).WidthOverride(1280).HeightOverride(900)[SNew(SOverlay)
            +SOverlay::Slot()[SNew(STownOverview).HUD(HUD)]
            +SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0,28,36,0)
                [SNew(SButton).OnClicked_Lambda([HUD]{if(HUD.IsValid())HUD->ToggleTownMap();return FReply::Handled();})
                    [SNew(STextBlock).Text(FText::FromString(TEXT("返回探索"))).Font(TownFont(16))]]]];
}
