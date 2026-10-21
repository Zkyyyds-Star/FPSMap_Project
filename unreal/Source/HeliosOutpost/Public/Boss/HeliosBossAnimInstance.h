#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "HeliosBossAnimInstance.generated.h"
class UAnimSequence;

/** Explicit boss timeline sampling, with a 150ms two-pose transition. */
UCLASS(Transient)
class HELIOSOUTPOST_API UHeliosBossAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    UHeliosBossAnimInstance();
    void SetTimelinePose(UAnimSequence* Sequence, float Seconds, bool bLoop, float DeltaSeconds);
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> CurrentSequence;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> PreviousSequence;
    float CurrentSeconds = 0.0f;
    float PreviousSeconds = 0.0f;
    float TransitionAlpha = 1.0f;
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
};
