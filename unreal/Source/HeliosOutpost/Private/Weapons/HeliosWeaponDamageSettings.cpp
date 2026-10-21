#include "Weapons/HeliosWeaponDamageSettings.h"
#include "Weapons/HeliosMysteriousHand.h"
#include "Combat/CombatDamageSettings.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Weapons/HeliosMechWeapon.h"
#include "Weapons/HeliosLightstickWeapon.h"
#include "Weapons/HeliosSoulBanner.h"
#include "Weapons/HeliosQingxiao.h"
#include "Weapons/HeliosCalamityStaff.h"
#include "Weapons/HeliosColaWeapon.h"

void ApplyWeaponDamageSettings(const UObject* Context, UHeliosWeaponDefinition* D)
{
    if (!D) return;
    const auto Get = [Context](ECombatDamageType Type) { return UCombatDamageSettings::Resolve(Context, Type); };
    using T = ECombatDamageType;
    const FName Id = D->WeaponId;
    if (Id == TEXT("rifle")) D->Damage = D->MinimumDamage = Get(T::Rifle);
    else if (Id == TEXT("rocket")) { D->Damage = Get(T::RocketMax); D->MinimumDamage = Get(T::RocketMin); }
    else if (Id == TEXT("laser")) D->Damage = D->MinimumDamage = Get(T::Laser);
    else if (Id == TEXT("shotgun")) { D->Damage = Get(T::ShotgunMax); D->MinimumDamage = Get(T::ShotgunMin); }
    else if (Id == TEXT("chopsticks")) D->Damage = D->MinimumDamage = Get(T::Chopsticks);
    else if (Id == TEXT("cfm_sunshangxiang")) D->Damage = D->MinimumDamage = Get(T::SSXLight);
    if (auto* M = Cast<UHeliosMechDefinition>(D))
    { M->Damage=M->MinimumDamage=M->StompDamage=Get(T::MechStomp); M->BeamDamage=Get(T::MechBeam); }
    if (auto* H = Cast<UHeliosMysteriousHandDefinition>(D))
    { H->Damage=H->MinimumDamage=Get(T::MysteriousLaser); H->MissileMaxDamage=Get(T::MysteriousMissileMax); H->MissileMinDamage=Get(T::MysteriousMissileMin); }
    if (auto* C = Cast<UHeliosColaWeaponDefinition>(D))
    { C->Damage = Get(T::ColaMax); C->MinimumDamage = Get(T::ColaMin); }
    if (auto* L = Cast<UHeliosLightstickDefinition>(D))
    {
        L->ComboDamage = {Get(T::Light1), Get(T::Light2), Get(T::Light3)};
        L->Damage = L->ComboDamage[0];
        L->HeavyMinimumDamage = Get(T::HeavyMin); L->HeavyMaximumDamage = Get(T::HeavyMax);
    }
    if (auto* S = Cast<UHeliosSoulBannerDefinition>(D))
    {
        S->ComboDamage = {Get(T::Soul1), Get(T::Soul2), Get(T::Soul3)};
        S->Damage = S->ComboDamage[0];
    }
    if (auto* Q = Cast<UHeliosQingxiaoDefinition>(D))
    { Q->Damage = Get(T::QingxiaoSlash); Q->SwordDamage = Get(T::QingxiaoSword); }
    if (auto* C = Cast<UHeliosCalamityDefinition>(D))
    {
        if (C->Spells.Num() < 4) C->Spells.SetNum(4);
        C->Damage = C->Spells[0].Damage = Get(T::Earthquake);
        C->Spells[1].Damage = Get(T::FireMax); C->Spells[1].MinimumDamage = Get(T::FireMin);
        C->Spells[2].Damage = Get(T::Tornado); C->Spells[3].Damage = Get(T::Poison);
        C->TowerDamageScale = Get(T::TowerScale);
    }
}
