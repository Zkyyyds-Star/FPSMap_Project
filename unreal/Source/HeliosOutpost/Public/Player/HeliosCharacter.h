#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "HeliosCharacter.generated.h"

class UCameraComponent;
class UHeliosFighterFlightComponent;

UCLASS(BlueprintType)
class HELIOSOUTPOST_API AHeliosCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    AHeliosCharacter(const FObjectInitializer& ObjectInitializer=FObjectInitializer::Get());

    UPROPERTY(Transient) TObjectPtr<UHeliosFighterFlightComponent> FighterFlight;
    bool IsFighterMode() const { return FighterFlight!=nullptr; }
    void FighterDescend();
    void FighterStopDescend();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetActorEyesViewPoint(FVector& OutLocation, FRotator& OutRotation) const override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Helios|Camera")
    TObjectPtr<UCameraComponent> FirstPersonCamera;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Helios|Identity")
    FString ActorId = TEXT("player_01");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helios|Safety", meta = (Units = "cm"))
    float ResetBelowZ = -5000.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Helios|Movement", meta = (Units = "cm/s"))
    float WalkSpeed = 450.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Helios|Movement", meta = (Units = "cm/s"))
    float SprintSpeed = 650.0f;

    UFUNCTION(BlueprintCallable, Category = "Helios|Movement")
    void ResetToSafeSpawn();

    void SetSprintingFromAutomation(bool bSprint);
    void SetEquipmentMovementMultiplier(float Multiplier);
    float GetEquipmentMovementMultiplier() const { return EquipmentMovementMultiplier; }
    void ClearMovementInput();

    bool FindMechDeployment(FVector& OutFeet) const;
    bool EnterMechMode();
    void ExitMechMode();
    void UpdateMechView(float DeployAlpha,float Bob);
    bool IsMechMode() const { return bMechMode; }
    FVector GetGroundReference() const;
    FVector GetGameplayReference() const;
    const FString& GetMechDeploymentError() const { return MechDeploymentError; }

protected:
    /** Cancels local-control automation before subclasses process manual combat input. */
    void NotifyManualInput();
    virtual void QuitPlay();

private:
    void MoveForward(float Value);
    void MoveRight(float Value);
    void Turn(float Value);
    void LookUp(float Value);
    void StartJump();
    void EndJump();
    void ToggleSprint();

    void ApplySprint(bool bSprint);
    bool ResolveUrbanSafeSpawn(FTransform& InOut) const;

    FTransform SafeSpawnTransform;
    bool bManualSprintEnabled = false;
    bool bAppliedSprint = false;
    float EquipmentMovementMultiplier = 1.f;
    bool bMechMode = false;
    bool bSavedNavUpdate = true;
    float SavedRadius=34, SavedHalfHeight=96, SavedFOV=90, SavedWalk=450, SavedSprint=650, SavedJump=500;
    FVector SavedCamera=FVector::ZeroVector;
    FString MechDeploymentError;
};
