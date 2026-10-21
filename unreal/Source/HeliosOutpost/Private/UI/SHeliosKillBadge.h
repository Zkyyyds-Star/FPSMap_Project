#pragma once
#include "Combat/HeliosKillFeedbackSubsystem.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Rendering/DrawElements.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Misc/Paths.h"
#include "Widgets/SLeafWidget.h"

/** Full-size, hit-test-invisible layer. Anchoring uses the actual viewport geometry. */
class SHeliosKillBadge final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SHeliosKillBadge){} SLATE_ARGUMENT(TWeakObjectPtr<UHeliosKillFeedbackSubsystem>,Feedback) SLATE_END_ARGS()
    void Construct(const FArguments& Args) { Feedback=Args._Feedback; SetVisibility(EVisibility::HitTestInvisible); }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
    virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Cull,FSlateWindowElementList& Out,
        int32 Layer,const FWidgetStyle& Style,bool bParentEnabled) const override
    {
        const auto* F=Feedback.Get();
        if(!F || !F->IsVisible()) return Layer;
        UTexture2D* Texture=F->GetTexture(); if(!Texture) return Layer;
        const auto& State=F->GetState();
        const double Age=F->GetWorld()->GetTimeSeconds()-State.StartedAt;
        const float Fade=FMath::Clamp(float((State.Duration()-Age)/0.3),0.f,1.f);
        const float Pop=Age<0.18?FMath::Lerp(0.76f,1.08f,float(Age/0.18)):
            Age<0.32?FMath::Lerp(1.08f,1.f,float((Age-0.18)/0.14)):1.f;
        const FVector2D Size=Geometry.GetLocalSize();
        const float Fit=FMath::Clamp(float(Size.Y/1080.f),0.65f,1.4f);
        const float Width=240.f*Fit*Pop;
        const FVector2D Centre(Size.X*0.5f,Size.Y*0.73f);
        Brush.SetResourceObject(Texture); Brush.ImageSize=FVector2D(Width,Width);
        const FVector2D Origin=Centre-FVector2D(Width/2,Width/2+18*Fit);
        const float Flash=1.f+0.3f*FMath::Clamp(1.f-float(Age/0.14),0.f,1.f);
        FSlateDrawElement::MakeBox(Out,Layer+1,Geometry.ToPaintGeometry(FVector2D(Width,Width),FSlateLayoutTransform(Origin)),&Brush,
            ESlateDrawEffect::None,FLinearColor(Flash,Flash,Flash,Fade));
        const FSlateFontInfo Font(FPaths::ProjectContentDir()/TEXT("UI/Fonts/SimHei.ttf"),FMath::RoundToInt(21*Fit));
        const FSlateFontInfo SmallFont(FPaths::ProjectContentDir()/TEXT("UI/Fonts/SimHei.ttf"),FMath::RoundToInt(13*Fit));
        const FString Name=UHeliosKillFeedbackSubsystem::Label(State.Display);
        const FString Count=FString::Printf(TEXT("%d 连杀"),State.DisplayStreak);
        auto DrawCentred=[&](const FString& Text,const FSlateFontInfo& TextFont,float Y,FLinearColor Color)
        {
            const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
            const FVector2D Extent=Measure->Measure(Text,TextFont);
            const FVector2D Pos(Centre.X-Extent.X*0.5f,Y);
            FSlateDrawElement::MakeText(Out,Layer+2,Geometry.ToPaintGeometry(Extent,FSlateLayoutTransform(Pos+FVector2D(1,2))),Text,TextFont,ESlateDrawEffect::None,FLinearColor(0,0,0,Fade));
            Color.A=Fade;
            FSlateDrawElement::MakeText(Out,Layer+3,Geometry.ToPaintGeometry(Extent,FSlateLayoutTransform(Pos)),Text,TextFont,ESlateDrawEffect::None,Color);
        };
        DrawCentred(Name,Font,Centre.Y+82*Fit,FLinearColor(1.f,.88f,.52f));
        DrawCentred(Count,SmallFont,Centre.Y+126*Fit,FLinearColor(.8f,.9f,1.f));
        return Layer+3;
    }
private:
    TWeakObjectPtr<UHeliosKillFeedbackSubsystem> Feedback;
    mutable FSlateBrush Brush;
};
