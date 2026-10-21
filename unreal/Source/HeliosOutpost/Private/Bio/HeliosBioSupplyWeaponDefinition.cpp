#include "Bio/HeliosBioSupplyWeaponDefinition.h"
#include "ShooterWeapon.h"
#include "Bio/HeliosBioSupplyWeapon.h"
#include "Misc/PackageName.h"

int32 UHeliosBioSupplyWeaponDefinition::SlotForItem(int32 Id)
{
    switch(Id){case 10801095:return 1;case 10804013:return 2;case 10802015:return 3;case 10301005:return 4;default:return INDEX_NONE;}
}
int32 UHeliosBioSupplyWeaponDefinition::ItemForSlot(int32 Slot)
{
    static constexpr int32 Ids[]={0,10801095,10804013,10802015,10301005};
    return Slot>=1&&Slot<5?Ids[Slot]:0;
}
UHeliosBioSupplyWeaponDefinition* UHeliosBioSupplyWeaponDefinition::LoadForSlot(int32 Slot)
{
    const int32 Id=ItemForSlot(Slot);if(!Id)return nullptr;
    const FString Path=FString::Printf(TEXT("/Game/Helios/CFMBioPyramidFloating/SupplyWeapons/DA_BioSupply_%d.DA_BioSupply_%d"),Id,Id);
    if(!FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Path)))return nullptr;
    return LoadObject<UHeliosBioSupplyWeaponDefinition>(nullptr,*Path,nullptr,LOAD_NoWarn);
}
FString UHeliosBioSupplyWeaponDefinition::LabelForSlot(int32 Slot)
{
    switch(Slot){case 0:return TEXT("原武器");case 1:return TEXT("FN FAL榴弹版");case 2:return TEXT("火箭筒-生化战神");case 3:return TEXT("生化震撼弹");case 4:return TEXT("M60-生化战神");default:return FString();}
}
bool UHeliosBioSupplyWeaponDefinition::IsReady() const
{
    const FName Expected(*FString::Printf(TEXT("cfm_bio_supply_%d"),OriginalItemId));
    return SlotForItem(OriginalItemId)!=INDEX_NONE&&WeaponId==Expected&&WeaponClass&&WeaponClass->IsChildOf(AHeliosBioSupplyWeapon::StaticClass())&&
        bSourceIdentityVerified&&bPresentationVerified&&bGameplayVerified&&bMuzzleBridgeVerified&&!NativeFirstPersonMesh.IsNull()&&!SourceEvidence.IsEmpty()&&
        MagazineSize>0&&PickupReserveAmmo>=0&&MaximumReserveAmmo>=PickupReserveAmmo;
}
