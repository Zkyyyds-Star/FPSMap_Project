#include "Boss/HeliosBossAnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"

namespace
{
struct FHeliosBossAnimProxy final : FAnimInstanceProxy
{
    explicit FHeliosBossAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}
    UAnimSequence* Current = nullptr;
    UAnimSequence* Previous = nullptr;
    float CurrentTime = 0.0f, PreviousTime = 0.0f, Alpha = 1.0f;
    virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override
    {
        FAnimInstanceProxy::PreUpdate(Instance, DeltaSeconds);
        const UHeliosBossAnimInstance* Boss = CastChecked<UHeliosBossAnimInstance>(Instance);
        Current = Boss->CurrentSequence;
        Previous = Boss->PreviousSequence;
        CurrentTime = Boss->CurrentSeconds;
        PreviousTime = Boss->PreviousSeconds;
        Alpha = Boss->TransitionAlpha;
    }
    virtual bool Evaluate(FPoseContext& Output) override
    {
        Output.ResetToRefPose();
        if (!Current) return true;
        FAnimationPoseData CurrentData(Output);
        Current->GetAnimationPose(CurrentData, FAnimExtractContext(CurrentTime, false));
        if (Previous && Alpha < 1.0f)
        {
            FPoseContext PreviousPose(Output);
            PreviousPose.ResetToRefPose();
            FAnimationPoseData PreviousData(PreviousPose);
            Previous->GetAnimationPose(PreviousData, FAnimExtractContext(PreviousTime, false));
            FAnimationRuntime::BlendTwoPosesTogetherInPlace(CurrentData, PreviousData, Alpha);
        }
        return true;
    }
};
}

UHeliosBossAnimInstance::UHeliosBossAnimInstance()
{
    RootMotionMode = ERootMotionMode::NoRootMotionExtraction;
    bUseMultiThreadedAnimationUpdate = false;
}

void UHeliosBossAnimInstance::SetTimelinePose(UAnimSequence* Sequence, float Seconds, bool bLoop, float DeltaSeconds)
{
    if (!Sequence) return;
    if (CurrentSequence != Sequence)
    {
        PreviousSequence = CurrentSequence;
        PreviousSeconds = CurrentSeconds;
        CurrentSequence = Sequence;
        TransitionAlpha = PreviousSequence ? 0.0f : 1.0f;
    }
    else TransitionAlpha = FMath::Min(1.0f, TransitionAlpha + DeltaSeconds / 0.15f);
    const float Length = FMath::Max(Sequence->GetPlayLength(), KINDA_SMALL_NUMBER);
    CurrentSeconds = bLoop ? FMath::Fmod(FMath::Max(Seconds, 0.0f), Length)
        : FMath::Clamp(Seconds, 0.0f, Length);
    if (TransitionAlpha >= 1.0f) PreviousSequence = nullptr;
}
FAnimInstanceProxy* UHeliosBossAnimInstance::CreateAnimInstanceProxy() { return new FHeliosBossAnimProxy(this); }
void UHeliosBossAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) { delete InProxy; }
