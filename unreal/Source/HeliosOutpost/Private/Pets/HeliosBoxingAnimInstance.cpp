#include "Pets/HeliosBoxingAnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "GameFramework/Pawn.h"
#include "AnimationRuntime.h"

struct FHeliosBoxingProxy : FAnimInstanceProxy
{
    explicit FHeliosBoxingProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}
    UAnimSequence* Body = nullptr;
    UAnimSequence* Previous = nullptr;
    float PreviousTime = 0.f, Transition = 1.f;
    float BodyTime = 0.f;
    virtual void PreUpdate(UAnimInstance* Instance, float Dt) override
    {
        FAnimInstanceProxy::PreUpdate(Instance, Dt);
        const auto* Inputs = CastChecked<UHeliosBoxingAnimInstance>(Instance);
        if (Body != Inputs->BodyPose)
        { Previous=Body;PreviousTime=BodyTime;Transition=Previous?0.f:1.f;BodyTime=0.f; }
        Transition=FMath::Min(1.f,Transition+Dt/.14f);
        Body = Inputs->BodyPose;
        const bool Boxing = Inputs->ExplicitTime >= 0.f;
        if (Body)
        {
            BodyTime = Boxing ? Inputs->ExplicitTime : BodyTime + Dt * Inputs->PoseRate;
            BodyTime = Inputs->bLoopPose ? FMath::Fmod(BodyTime, FMath::Max(.01f, Body->GetPlayLength())) : FMath::Min(BodyTime, Body->GetPlayLength());
        }

    }
    virtual bool Evaluate(FPoseContext& Output) override
    {
        Output.ResetToRefPose();
        if (!Body) return true;
        FAnimationPoseData BodyData(Output);
        Body->GetAnimationPose(BodyData, FAnimExtractContext(static_cast<double>(BodyTime), false));
        if(Previous&&Transition<1.f)
        {
            FPoseContext From(Output);From.ResetToRefPose();FAnimationPoseData FromData(From);
            Previous->GetAnimationPose(FromData,FAnimExtractContext(static_cast<double>(PreviousTime),false));
            FAnimationRuntime::BlendTwoPosesTogetherInPlace(BodyData,FromData,Transition);
        }
        return true;
    }
};
FAnimInstanceProxy* UHeliosBoxingAnimInstance::CreateAnimInstanceProxy() { return new FHeliosBoxingProxy(this); }
void UHeliosBoxingAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) { delete Proxy; }
