#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HeliosBioNavAudit.generated.h"

/** Read-only cooked navigation/physical geometry evidence for original Bio shores. */
UCLASS()
class HELIOSOUTPOST_API AHeliosBioNavAudit : public AActor
{
    GENERATED_BODY()
public:
    AHeliosBioNavAudit();
    virtual void Tick(float Dt) override;
private:
    bool bRecorded=false;
    float Age=0.f;
    void Record();
};
