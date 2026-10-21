#include "Boss/HeliosAttackNotify.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Components/SkeletalMeshComponent.h"

void UHeliosAttackNotify::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
    const FAnimNotifyEventReference& EventReference)
{
    Super::Notify(MeshComp, Animation, EventReference);
    if (MeshComp)
    {
        if (AHeliosEnemyActor* Enemy = Cast<AHeliosEnemyActor>(MeshComp->GetOwner()))
        {
            Enemy->HandleAttackNotify();
        }
    }
}

FString UHeliosAttackNotify::GetNotifyName_Implementation() const
{
    return TEXT("HeliosAttack");
}
