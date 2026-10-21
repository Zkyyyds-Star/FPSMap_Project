#include "Weapons/HeliosWeaponDefinition.h"
#include "Engine/SkeletalMesh.h"

bool UHeliosWeaponDefinition::HasModel() const
{
    if (WeaponId == TEXT("rifle") && WeaponMesh.IsNull())
    {
        return true;
    }
    if (!SkeletalWeaponMesh.IsNull() && SkeletalWeaponMesh.LoadSynchronous()) return true;
    return !WeaponMesh.IsNull() && WeaponMesh.LoadSynchronous() != nullptr;
}
