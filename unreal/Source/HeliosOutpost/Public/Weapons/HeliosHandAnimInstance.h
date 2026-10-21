#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "HeliosHandAnimInstance.generated.h"

class UAnimSequence;

/** Weapon-clock sampling. Inertialization retains the actually displayed pose and velocity. */
UCLASS(Transient)
class HELIOSOUTPOST_API UHeliosHandAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    UHeliosHandAnimInstance();
    void SetTimelinePose(UAnimSequence* Sequence, float Seconds, bool bLoop, bool bRestart = false);
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> Sequence;
    float SampleSeconds = 0.f;
    uint32 TransitionSerial = 0;
    // Fast source attacks/directional clips may have incompatible endpoint velocities.
    bool bPreserveTransitionVelocity = true;
    float TimelineRate = 1.f;
    static constexpr float BlendSeconds = .08f;
    float BlendDuration = BlendSeconds;
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
};
