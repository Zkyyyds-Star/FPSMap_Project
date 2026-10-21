#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "HeliosAttackNotify.generated.h"

/** Animation-authored impact point. Exactly one damage dispatch is allowed per attack cycle. */
UCLASS(meta = (DisplayName = "HeliosAttack"))
class HELIOSOUTPOST_API UHeliosAttackNotify : public UAnimNotify
{
    GENERATED_BODY()

public:
    virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
        const FAnimNotifyEventReference& EventReference) override;
    virtual FString GetNotifyName_Implementation() const override;
};

