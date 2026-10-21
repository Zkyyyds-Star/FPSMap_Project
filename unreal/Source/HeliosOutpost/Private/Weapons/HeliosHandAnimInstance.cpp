#include "Weapons/HeliosHandAnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"

namespace
{
FVector RotationVector(FQuat Q)
{
    Q.Normalize();if(Q.W<0)Q=Q*-1.0;
    const FQuat Log=Q.Log();return FVector(Log.X,Log.Y,Log.Z)*2.0;
}
FQuat RotationFromVector(const FVector& V)
{
    const double Angle=V.Size();return Angle>UE_SMALL_NUMBER?FQuat(V/Angle,Angle):FQuat::Identity;
}
struct FHandInertia
{
    FVector Position=FVector::ZeroVector,Velocity=FVector::ZeroVector;
    FVector Rotation=FVector::ZeroVector,AngularVelocity=FVector::ZeroVector;
    FVector Scale=FVector::ZeroVector;
};
struct FHandAnimProxy final : FAnimInstanceProxy
{
    explicit FHandAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}
    UAnimSequence* Sequence=nullptr;
    float Seconds=0.f,Dt=0.f,HistoryDt=1.f/60.f,Elapsed=1.f;
    uint32 Serial=0;
    bool bTransition=false;
    bool bPreserveVelocity=true;
    float TimelineRate=1.f;
    float BlendDuration=UHeliosHandAnimInstance::BlendSeconds;
    TArray<FTransform> Last,Previous,Displayed;
    TArray<FHandInertia> Inertia;
    virtual void PreUpdate(UAnimInstance* Instance,float DeltaSeconds) override
    {
        FAnimInstanceProxy::PreUpdate(Instance,DeltaSeconds);
        const auto* Hand=CastChecked<UHeliosHandAnimInstance>(Instance);
        bTransition|=Sequence && Serial!=Hand->TransitionSerial;
        Serial=Hand->TransitionSerial;Sequence=Hand->Sequence;Seconds=Hand->SampleSeconds;Dt=DeltaSeconds;bPreserveVelocity=Hand->bPreserveTransitionVelocity;
        TimelineRate=Hand->TimelineRate;BlendDuration=FMath::Max(.001f,Hand->BlendDuration);
        // A socket refresh can evaluate at zero delta and suppress the later
        // evaluation. Advance transition time on animation update, not evaluation.
        Elapsed+=FMath::Max(DeltaSeconds,0.f);
    }
    virtual bool Evaluate(FPoseContext& Output) override
    {
        Output.ResetToRefPose();if(!Sequence)return true;
        FAnimationPoseData Data(Output);Sequence->GetAnimationPose(Data,FAnimExtractContext(Seconds,false));
        const int32 Count=Output.Pose.GetNumBones();
        if(Displayed.Num()!=Count){Last.Empty();Previous.Empty();Displayed.Empty();Inertia.Empty();Elapsed=1.f;}
        if(bTransition && Displayed.Num()==Count)
        {
            // Sample incoming velocity; decay the difference from the displayed pose/velocity.
            FPoseContext Ahead(Output);Ahead.ResetToRefPose();FAnimationPoseData AheadData(Ahead);
            constexpr float SampleStep=1.f/120.f;
            Sequence->GetAnimationPose(AheadData,FAnimExtractContext(FMath::Min(Seconds+SampleStep*TimelineRate,Sequence->GetPlayLength()),false));
            Inertia.SetNum(Count);
            for(int32 I=0;I<Count;++I)
            {
                const FCompactPoseBoneIndex Index(I);const FTransform& Target=Output.Pose[Index];const FTransform& Next=Ahead.Pose[Index];
                FVector OutVelocity=FVector::ZeroVector,OutAngular=FVector::ZeroVector;
                if(Previous.Num()==Count && Last.Num()==Count && HistoryDt>UE_SMALL_NUMBER)
                {
                    OutVelocity=(Last[I].GetTranslation()-Previous[I].GetTranslation())/HistoryDt;
                    OutAngular=RotationVector(Last[I].GetRotation()*Previous[I].GetRotation().Inverse())/HistoryDt;
                }
                FHandInertia& B=Inertia[I];
                B.Position=Displayed[I].GetTranslation()+OutVelocity*Dt-Target.GetTranslation();
                B.Velocity=OutVelocity-(Next.GetTranslation()-Target.GetTranslation())/SampleStep;
                B.Rotation=RotationVector(RotationFromVector(OutAngular*Dt)*Displayed[I].GetRotation()*Target.GetRotation().Inverse());
                B.AngularVelocity=OutAngular-RotationVector(Next.GetRotation()*Target.GetRotation().Inverse())/SampleStep;
                B.Scale=Displayed[I].GetScale3D()-Target.GetScale3D();
                if(!bPreserveVelocity)
                {
                    B.Position=Displayed[I].GetTranslation()-Target.GetTranslation();B.Velocity=FVector::ZeroVector;
                    B.Rotation=RotationVector(Displayed[I].GetRotation()*Target.GetRotation().Inverse());B.AngularVelocity=FVector::ZeroVector;
                }
            }
            Elapsed=0.f;
        }
        bTransition=false;
        if(Inertia.Num()==Count && Elapsed<BlendDuration)
        {
            const float Duration=BlendDuration;
            const float T=FMath::Clamp(Elapsed/Duration,0.f,1.f),T2=T*T,T3=T2*T;
            const float PositionWeight=2*T3-3*T2+1,VelocityWeight=(T3-2*T2+T)*Duration;
            for(int32 I=0;I<Count;++I)
            {
                FTransform& Bone=Output.Pose[FCompactPoseBoneIndex(I)];const FHandInertia& B=Inertia[I];
                Bone.AddToTranslation(B.Position*PositionWeight+B.Velocity*VelocityWeight);
                Bone.SetRotation((RotationFromVector(B.Rotation*PositionWeight+B.AngularVelocity*VelocityWeight)*Bone.GetRotation()).GetNormalized());
                Bone.SetScale3D(Bone.GetScale3D()+B.Scale*PositionWeight);
            }
        }
        // Track every displayed pose, retaining separate positive-delta velocity history.
        Displayed.SetNum(Count);
        for(int32 I=0;I<Count;++I)Displayed[I]=Output.Pose[FCompactPoseBoneIndex(I)];
        if(Dt>UE_SMALL_NUMBER || Last.Num()!=Count)
        {
            Previous=Last;Last.SetNum(Count);
            for(int32 I=0;I<Count;++I)Last[I]=Output.Pose[FCompactPoseBoneIndex(I)];
            if(Dt>UE_SMALL_NUMBER)HistoryDt=Dt;
        }
        return true;
    }
};
}

UHeliosHandAnimInstance::UHeliosHandAnimInstance()
{
    RootMotionMode = ERootMotionMode::NoRootMotionExtraction;
    bUseMultiThreadedAnimationUpdate = false;
}
void UHeliosHandAnimInstance::SetTimelinePose(UAnimSequence* InSequence, float Seconds, bool bLoop, bool bRestart)
{
    if (!InSequence) return;
    if (Sequence != InSequence || bRestart) ++TransitionSerial;
    Sequence = InSequence;
    const float Length = FMath::Max(InSequence->GetPlayLength(), KINDA_SMALL_NUMBER);
    SampleSeconds = bLoop ? FMath::Fmod(FMath::Max(Seconds, 0.f), Length)
        : FMath::Clamp(Seconds, 0.f, Length);
}
FAnimInstanceProxy* UHeliosHandAnimInstance::CreateAnimInstanceProxy() { return new FHandAnimProxy(this); }
void UHeliosHandAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) { delete Proxy; }
