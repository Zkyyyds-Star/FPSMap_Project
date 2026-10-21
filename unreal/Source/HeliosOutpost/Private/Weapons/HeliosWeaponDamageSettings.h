#pragma once
#include "CoreMinimal.h"
class UHeliosWeaponDefinition;
// Mutates only the caller-owned transient copy, never a registry asset or class default.
void ApplyWeaponDamageSettings(const UObject* Context, UHeliosWeaponDefinition* Definition);
