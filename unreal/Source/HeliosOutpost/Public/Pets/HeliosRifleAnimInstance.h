#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "HeliosRifleAnimInstance.generated.h"
UCLASS(Transient)
class HELIOSOUTPOST_API UHeliosRifleAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) TObjectPtr<class UAnimSequence> BodyPose;
    UPROPERTY(Transient) TObjectPtr<class UAnimSequence> SidePose;
    float ExplicitTime=-1,PoseRate=1,AimPitch=0,Recoil=0;
    float SideWeight=0,MoveSpeed=0,MoveDirection=0,LocomotionPhase=0;
    bool bLocomotion=false;
    bool bLoopPose=true,bAim=false;
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
};
