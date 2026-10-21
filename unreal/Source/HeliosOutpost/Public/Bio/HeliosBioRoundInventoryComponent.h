#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Player/HeliosLoadout.h"
#include "HeliosBioRoundInventoryComponent.generated.h"
class AShooterWeapon;
class UHeliosBioSupplyWeaponDefinition;

USTRUCT()
struct FHeliosBioRoundWeaponSlot
{
    GENERATED_BODY()
    UPROPERTY(Transient) TObjectPtr<AShooterWeapon> Weapon;
    UPROPERTY(Transient) TObjectPtr<UHeliosBioSupplyWeaponDefinition> Definition;
    UPROPERTY(Transient) FHeliosLoadout Loadout;
    int32 ReserveAmmo=0,MagazineSize=0;
    float ReloadSeconds=0;
};

/** Five transient slots. Slot zero owns this round's original human equipment. */
UCLASS()
class HELIOSOUTPOST_API UHeliosBioRoundInventoryComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UHeliosBioRoundInventoryComponent();
    bool IsEnabled() const;
    void RegisterBaseline(AShooterWeapon* Weapon,const FHeliosLoadout& Loadout,int32 Reserve,int32 Magazine,float Reload);
    bool CanGrant(const UHeliosBioSupplyWeaponDefinition* Definition,FString* Error=nullptr) const;
    bool TryGrant(UHeliosBioSupplyWeaponDefinition* Definition);
    bool Select(int32 Slot,bool Force=false);
    bool Cycle(int32 Direction);
    void CaptureActive();
    void Suspend();
    bool Restore();
    void ResetRound(bool DestroyBaseline=false);
    void AddBaselineAmmo(int32 Amount=300);
    int32 GetActiveSlot() const{return ActiveSlot;}
    bool HasSlot(int32 Slot) const;
    int32 GetReserve(int32 Slot) const;
    AShooterWeapon* GetWeapon(int32 Slot) const;
    FName GetWeaponId(int32 Slot) const;
    FString Describe() const;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY(Transient) TArray<FHeliosBioRoundWeaponSlot> Slots;
    int32 ActiveSlot=0;
    bool CanOperate() const;
    bool Preflight(const FHeliosLoadout& Loadout,bool Throwable,FString* Error,const UHeliosBioSupplyWeaponDefinition* SupplyDefinition=nullptr) const;
    bool CommitSlot(int32 Slot);
};
