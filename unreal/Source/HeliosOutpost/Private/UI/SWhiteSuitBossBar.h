#pragma once

#include "UI/HeliosAdventureHUD.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"
#include "Misc/Paths.h"

/** Lightweight boss presentation in Slate units; gameplay owns health and damage. */
class SWhiteSuitBossBar final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SWhiteSuitBossBar) {} SLATE_ARGUMENT(TWeakObjectPtr<AHeliosAdventureHUD>, HUD) SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        HUD = Args._HUD;
        const FString FontPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("UI/Fonts/SimHei.ttf"));
        TitleFont = FSlateFontInfo(FontPath, 19);
        DetailFont = FSlateFontInfo(FontPath, 11);
        MetaFont = FSlateFontInfo(FontPath, 10);
        SetVisibility(EVisibility::HitTestInvisible);
    }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(720, 80); }
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&,
        FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool) const override
    {
        if (!HUD.IsValid() || HUD->GetBossBarOpacity() < .005f || G.GetLocalSize().X < 32.f) return Layer;
        const float Width = FMath::Min(720.f, G.GetLocalSize().X);
        const float Alpha = HUD->GetBossBarOpacity() * Style.GetColorAndOpacityTint().A;
        const FLinearColor Silver(.64f, .73f, .81f), White(.91f, .95f, 1.f);
        const FLinearColor Dim(.43f, .51f, .59f), Orange(.95f, .36f, .12f);
        const FSlateBrush* Brush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
        const auto Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        // The viewport scales the whole HUD at 720p. Keep this critical text legible
        // without applying another scale to the bar or changing the global HUD DPI.
        const float ViewScale = FMath::Max(.1f, G.GetAccumulatedLayoutTransform().GetScale());
        auto ReadableFont = [ViewScale](FSlateFontInfo Font)
        {
            Font.Size = FMath::RoundToInt(Font.Size / FMath::Min(1.f, ViewScale));
            return Font;
        };
        FSlateFontInfo TitlePaintFont = ReadableFont(TitleFont);
        TitlePaintFont.Size = FMath::Min(23.f, TitlePaintFont.Size);
        const FSlateFontInfo DetailPaintFont = ReadableFont(DetailFont);
        const FSlateFontInfo MetaPaintFont = ReadableFont(MetaFont);
        Out.PushClip(FSlateClippingZone(G));
        auto Rect = [&](float X, float Y, float W, float H, FLinearColor Color, int32 Z = 0)
        {
            if (W <= 0.f || H <= 0.f) return;
            Color.A *= Alpha;
            FSlateDrawElement::MakeBox(Out, Layer + Z,
                G.ToPaintGeometry(FVector2D(W, H), FSlateLayoutTransform(FVector2D(X, Y))),
                Brush, ESlateDrawEffect::None, Color);
        };
        auto Line = [&](TArray<FVector2D> Points, FLinearColor Color, float Thickness = 1.f)
        {
            Color.A *= Alpha;
            FSlateDrawElement::MakeLines(Out, Layer + 4, G.ToPaintGeometry(), Points,
                ESlateDrawEffect::None, Color, true, Thickness);
        };
        auto Fit = [&](FString Value, const FSlateFontInfo& Font, float Available)
        {
            if (Available <= 0.f) return FString();
            if (Measure->Measure(Value, Font).X <= Available) return Value;
            while (!Value.IsEmpty() && Measure->Measure(Value + TEXT("…"), Font).X > Available)
                Value.LeftChopInline(1);
            return Value.IsEmpty() ? FString() : Value + TEXT("…");
        };
        auto Text = [&](float X, float Y, const FString& Value, const FSlateFontInfo& Font, FLinearColor Color)
        {
            if (Value.IsEmpty()) return;
            FSlateDrawElement::MakeText(Out, Layer + 5,
                G.ToPaintGeometry(FVector2D(Width, 28), FSlateLayoutTransform(FVector2D(X + 1, Y + 1))),
                Value, Font, ESlateDrawEffect::None, FLinearColor(0, 0, 0, .85f * Alpha));
            Color.A *= Alpha;
            FSlateDrawElement::MakeText(Out, Layer + 6,
                G.ToPaintGeometry(FVector2D(Width, 28), FSlateLayoutTransform(FVector2D(X, Y))),
                Value, Font, ESlateDrawEffect::None, Color);
        };
        // Feathered local contrast underneath each row instead of a boxed panel.
        auto Shade = [&](float Y, float H, float Opacity)
        {
            constexpr int32 Steps = 40;
            for (int32 I = 0; I < Steps; ++I)
            {
                const float T = (I + .5f) / Steps;
                const float Feather = FMath::Clamp(FMath::Min(T, 1.f - T) * 8.f, 0.f, 1.f);
                Rect(Width * I / Steps, Y, Width / Steps, H, FLinearColor(.005f, .008f, .014f, Opacity * Feather));
            }
        };
        Shade(0, 30, .22f);
        Shade(31, 22, .32f);
        Shade(56, 21, .18f);
        constexpr float X = 12.f, Y = 36.f, H = 10.f;
        const float W = Width - X * 2.f;
        FString Name, Identity;
        HUD->GetBossBarTitle().Split(TEXT("/"), &Name, &Identity);
        Name.TrimStartAndEndInline();
        Identity.TrimStartAndEndInline();
        const float IdentityWidth = Measure->Measure(Identity, DetailPaintFont).X;
        Text(X, 0, Fit(Name, TitlePaintFont, W - IdentityWidth - 16.f), TitlePaintFont, White);
        Text(Width - X - IdentityWidth, 8, Identity, DetailPaintFont, Silver);
        const float Health = FMath::Clamp(HUD->GetBossDisplayFraction(), 0.f, 1.f);
        const float Trail = FMath::Clamp(HUD->GetBossTrailFraction(), Health, 1.f);
        const float Fill = W * Health;
        const float Flash = FMath::Clamp(HUD->GetBossHitFlash(), 0.f, 1.f);
        Rect(X, Y, W, H, FLinearColor(.025f, .017f, .024f, .85f), 1);
        Rect(X + Fill, Y, W * (Trail - Health), H, FLinearColor(.73f, .32f, .14f, .65f), 2);
        if (Fill > .01f)
        {
            constexpr int32 Steps = 64;
            for (int32 I = 0; I < Steps; ++I)
            {
                const float T = I / float(Steps);
                const FLinearColor Color = FMath::Lerp(FLinearColor(.19f, .014f, .033f), FLinearColor(.65f, .055f, .065f), T);
                Rect(X + Fill * T, Y, Fill / Steps, H, Color, 3);
            }
            Rect(X, Y, Fill, 1.f, FLinearColor(.95f, .38f, .36f, .4f), 4);
            const float Glint = FMath::Min(2.f, Fill);
            Rect(X + Fill - Glint, Y, Glint, H, White.CopyWithNewOpacity(Flash * .9f), 4);
        }
        // Open silver rails and clipped end ornaments keep the silhouette light.
        Line({FVector2D(2, Y + 5), FVector2D(8, Y - 3), FVector2D(Width - 8, Y - 3), FVector2D(Width - 2, Y + 5)}, Silver.CopyWithNewOpacity(.65f));
        Line({FVector2D(2, Y + 5), FVector2D(8, Y + H + 3), FVector2D(Width - 8, Y + H + 3), FVector2D(Width - 2, Y + 5)}, Silver.CopyWithNewOpacity(.28f));
        Line({FVector2D(5, Y + 5), FVector2D(10, Y), FVector2D(10, Y + H)}, White.CopyWithNewOpacity(.8f));
        Line({FVector2D(Width - 5, Y + 5), FVector2D(Width - 10, Y), FVector2D(Width - 10, Y + H)}, White.CopyWithNewOpacity(.8f));
        Rect(X + W * .5f, Y + H + 2, 1, 3, Silver.CopyWithNewOpacity(.6f), 4);
        Rect(X, 27, 22, 1, FLinearColor(.08f, .72f, 1.f, .6f), 4);
        const float Windup = FMath::Clamp(HUD->GetBossWindupFraction(), 0.f, 1.f);
        if (Windup > 0.f && Windup < 1.f)
        {
            Rect(X, 52, W, 1, Orange.CopyWithNewOpacity(.12f), 2);
            Rect(X, 52, W * Windup, 1, Orange.CopyWithNewOpacity(.9f), 3);
        }
        const FString Attack = HUD->GetBossAttackText().ToString();
        const FString Meta = HUD->GetBossMetaText().ToString();
        const float MetaWidth = Measure->Measure(Meta, MetaPaintFont).X;
        const float AttackWidth = Measure->Measure(Attack, DetailPaintFont).X;
        const bool ShowMeta = Width * ViewScale >= 560.f && AttackWidth + MetaWidth + 24.f <= W;
        Text(X, 56, Fit(Attack, DetailPaintFont, ShowMeta ? W - MetaWidth - 24.f : W), DetailPaintFont,
            Windup > 0.f && Windup < 1.f ? FLinearColor(.95f, .62f, .38f) : Silver);
        if (ShowMeta) Text(Width - X - MetaWidth, 60, Meta, MetaPaintFont, Dim);
        Out.PopClip();
        return Layer + 6;
    }
private:
    TWeakObjectPtr<AHeliosAdventureHUD> HUD;
    FSlateFontInfo TitleFont, DetailFont, MetaFont;
};
