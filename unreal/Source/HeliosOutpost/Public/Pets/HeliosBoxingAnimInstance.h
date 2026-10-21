#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "HeliosBoxingAnimInstance.generated.h"

class UAnimSequence;
UCLASS(Transient)
class HELIOSOUTPOST_API UHeliosBoxingAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    // Game-thread inputs; the proxy snapshots them before parallel evaluation.
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> BodyPose;
    float ExplicitTime = -1.f;
    float PoseRate = 1.f;
    bool bLoopPose = true;
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
};
