#pragma once

class UObject;
class UHeliosWeaponDefinition;

void ApplyWeaponTempoSettings(const UObject* Context, UHeliosWeaponDefinition* Definition);
float GetConfiguredReloadSeconds(const UObject* Context, const UHeliosWeaponDefinition* Definition);
