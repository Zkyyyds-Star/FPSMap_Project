#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HeliosBioFourSlashComponent.generated.h"

class UHeliosBioRoleDefinition;
class USkeletalMeshComponent;
class UAnimSequence;
class AHeliosSSXEffect;
class AHeliosBioHotelGameMode;

/** Four verified native clips. Damage timing is explicit standalone tuning, never an FX event. */
UCLASS(ClassGroup=(Bio),meta=(BlueprintSpawnableComponent))
class HELIOSOUTPOST_API UHeliosBioFourSlashComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UHeliosBioFourSlashComponent();
    bool BeginSourceSequence(int32 ParticipantId,UHeliosBioRoleDefinition* Role,USkeletalMeshComponent* ViewMesh,bool FP);
    bool IsBusy() const { return bBusy; }
    void Cancel();
    int32 GetCurrentStage() const { return Stage; }
    int32 GetCompletedStages() const { return CompletedStages; }
    int32 GetHitCallbacks() const { return HitCallbacks; }
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;

    UPROPERTY(EditAnywhere,Category="BioFourSlash|Standalone Tuning") TArray<float> StageDamage = {900.f,900.f,900.f,900.f};
    UPROPERTY(EditAnywhere,Category="BioFourSlash|Standalone Tuning") TArray<float> StageHitFraction = {.45f,.45f,.5f,.55f};
    UPROPERTY(EditAnywhere,Category="BioFourSlash|Standalone Tuning",meta=(ClampMin="1")) float MeleeRange = 320.f;
    UPROPERTY(EditAnywhere,Category="BioFourSlash|Standalone Tuning",meta=(ClampMin="-1",ClampMax="1")) float ConeDot = .55f;
    UPROPERTY(EditAnywhere,Category="BioFourSlash|Standalone Tuning",meta=(ClampMin="0.01")) float PlaybackRate = 1.f;
    UPROPERTY(EditAnywhere,Category="BioFourSlash") bool bPlayVerifiedSourceEffects = true;
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY(Transient) TObjectPtr<UHeliosBioRoleDefinition> ActiveRole;
    UPROPERTY(Transient) TArray<TObjectPtr<UAnimSequence>> Clips;
    TWeakObjectPtr<USkeletalMeshComponent> ActiveMesh;
    TArray<TWeakObjectPtr<AHeliosSSXEffect>> Effects;
    int32 ActiveParticipantId = INDEX_NONE,ExpectedRound = INDEX_NONE,Stage = INDEX_NONE;
    int32 CompletedStages = 0,HitCallbacks = 0;
    float StageAge = 0.f,ActiveRate = 1.f;
    bool bBusy = false,bFirstPerson = false,bStageHitDispatched = false;
    bool ContextIsValid(AHeliosBioHotelGameMode* Mode) const;
    void StartStage();
    void DispatchStageHit(AHeliosBioHotelGameMode* Mode);
};
