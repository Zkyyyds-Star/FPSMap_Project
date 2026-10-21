#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Bio/HeliosBioRules.h"
#include "HeliosBioSupply.generated.h"
class UStaticMeshComponent;
class AHeliosSSXEffect;
class UHeliosBioSupplyWeaponDefinition;
UCLASS()
class HELIOSOUTPOST_API AHeliosBioSupply : public AActor
{
    GENERATED_BODY()
public:
    AHeliosBioSupply();
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void SetAvailable(bool Available);
    bool IsAvailable() const { return bAvailable; }
    bool bHasOriginalMesh = false;
    void SetKind(EHeliosBioSupplyKind Kind);
    EHeliosBioSupplyKind GetKind() const { return SupplyKind; }
    void SetWeaponContent(UHeliosBioSupplyWeaponDefinition* Definition);
    UHeliosBioSupplyWeaponDefinition* GetWeaponContent() const{return WeaponContent;}
    bool CanReachPickup(const AActor* Participant) const;
    UPROPERTY(VisibleAnywhere) bool bBlueSupplySourceVerified = false; // Original visual PPtr chain only; transformation role identity is separately verified.
    UPROPERTY(VisibleAnywhere) bool bRedSupplySourceVerified=false;
private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BoxMesh;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> ContentPreview;
    UPROPERTY(Transient) TObjectPtr<UHeliosBioSupplyWeaponDefinition> WeaponContent;
    bool bAvailable = true;
    float CheckRemaining = 0.f;
    EHeliosBioSupplyKind SupplyKind = EHeliosBioSupplyKind::Normal;
    void ApplySourceAppearance();
    void RefreshSourceEffect();
    UPROPERTY(Transient) TObjectPtr<AHeliosSSXEffect> BlueEffect;
};
