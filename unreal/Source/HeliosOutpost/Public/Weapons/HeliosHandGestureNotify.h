#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "HeliosHandGestureNotify.generated.h"

/** Anticipation only. Damage and projectile creation stay in the weapon's commit clock. */
UCLASS()
class HELIOSOUTPOST_API UHeliosHandGestureNotify : public UAnimNotify
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bBoo = false;
    virtual void Notify(USkeletalMeshComponent* MeshComp,UAnimSequenceBase* Animation,const FAnimNotifyEventReference& Reference) override;
    virtual FString GetNotifyName_Implementation() const override { return TEXT("神秘人 / 蓄势"); }
};
