#include "Pets/HeliosRifleAnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
struct FHeliosRifleProxy : FAnimInstanceProxy
{
    explicit FHeliosRifleProxy(UAnimInstance* I):FAnimInstanceProxy(I){}
    UAnimSequence* Body=nullptr;UAnimSequence* Previous=nullptr;UAnimSequence* Side=nullptr;UAnimSequence* PreviousSide=nullptr;
    float Time=0,PreviousTime=0,Blend=1,Pitch=0,Phase=0,Rate=1,PreviousRate=1,SideWeight=0,PreviousSideWeight=0;
    bool Moving=false,PreviousLoop=true,Loop=true;
    virtual void PreUpdate(UAnimInstance* I,float Dt) override
    {
        FAnimInstanceProxy::PreUpdate(I,Dt);auto* A=CastChecked<UHeliosRifleAnimInstance>(I);
        if(Body!=A->BodyPose)
        {
            Previous=Body;PreviousTime=Time;PreviousRate=Rate;PreviousSide=Side;PreviousSideWeight=SideWeight;
            PreviousLoop=Loop;
            Blend=Previous?0:1;Time=0;
        }
        Blend=FMath::Min(1.f,Blend+Dt/.20f);Body=A->BodyPose;Moving=A->bLocomotion;
        Rate=A->PoseRate;Side=A->SidePose;SideWeight=A->SideWeight;Loop=A->bLoopPose;
        if(Body)
        {
            const float Length=FMath::Max(.01f,Body->GetPlayLength());
            if(Moving){Phase=FMath::Fmod(Phase+Dt*Rate/Length,1.f);Time=Phase*Length;}
            else{Time=A->ExplicitTime>=0?A->ExplicitTime:Time+Dt*Rate;Time=A->bLoopPose?FMath::Fmod(Time,Length):FMath::Min(Time,Length);}
        }
        if(Previous&&Blend<1)
        {
            const float Length=FMath::Max(.01f,Previous->GetPlayLength());
            PreviousTime+=Dt*PreviousRate;
            PreviousTime=PreviousLoop?FMath::Fmod(PreviousTime,Length):FMath::Min(PreviousTime,Length);
        }
        A->LocomotionPhase=Phase;
        Pitch=A->bAim?A->AimPitch+A->Recoil:0;
    }
    virtual bool Evaluate(FPoseContext& O) override
    {
        O.ResetToRefPose();if(!Body)return true;FAnimationPoseData Data(O);
        Body->GetAnimationPose(Data,FAnimExtractContext(double(Time),false));
        if(Side&&SideWeight>0){FPoseContext Other(O);FAnimationPoseData D(Other);Side->GetAnimationPose(D,FAnimExtractContext(double(Phase*Side->GetPlayLength()),false));FAnimationRuntime::BlendTwoPosesTogetherInPlace(Data,D,1-SideWeight);}
        if(Previous&&Blend<1)
        {
            FPoseContext From(O);FAnimationPoseData D(From);Previous->GetAnimationPose(D,FAnimExtractContext(double(PreviousTime),false));
            if(PreviousSide&&PreviousSideWeight>0)
            {
                FPoseContext Other(O);FAnimationPoseData S(Other);PreviousSide->GetAnimationPose(S,FAnimExtractContext(double(PreviousTime/FMath::Max(.01f,Previous->GetPlayLength())*PreviousSide->GetPlayLength()),false));
                FAnimationRuntime::BlendTwoPosesTogetherInPlace(D,S,1-PreviousSideWeight);
            }
            FAnimationRuntime::BlendTwoPosesTogetherInPlace(Data,D,Blend);
        }
        const FBoneContainer& Bones=O.Pose.GetBoneContainer();const int32 Raw=Bones.GetReferenceSkeleton().FindBoneIndex(TEXT("M-bone-Spine1"));
        const FCompactPoseBoneIndex Spine=Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Raw));
        if(Spine.GetInt()!=INDEX_NONE&&FMath::Abs(Pitch)>.001f)
        {
            FTransform Parent=FTransform::Identity;FCompactPoseBoneIndex P=O.Pose.GetParentBoneIndex(Spine);
            while(P.GetInt()!=INDEX_NONE){Parent=Parent*O.Pose[P];P=O.Pose.GetParentBoneIndex(P);}
            const FVector Axis=Parent.GetRotation().Inverse().RotateVector(FVector::ForwardVector);
            O.Pose[Spine].SetRotation((FQuat(Axis,FMath::DegreesToRadians(Pitch))*O.Pose[Spine].GetRotation()).GetNormalized());
        }
        return true;
    }
};
FAnimInstanceProxy* UHeliosRifleAnimInstance::CreateAnimInstanceProxy(){return new FHeliosRifleProxy(this);}
void UHeliosRifleAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* P){delete P;}
