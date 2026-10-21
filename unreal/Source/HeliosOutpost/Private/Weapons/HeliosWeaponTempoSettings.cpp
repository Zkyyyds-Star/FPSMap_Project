#include "Weapons/HeliosWeaponTempoSettings.h"
#include "Combat/HeliosCombatTempoSettings.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Weapons/HeliosMechWeapon.h"
#include "Weapons/HeliosLightstickWeapon.h"
#include "Weapons/HeliosSoulBanner.h"
#include "Weapons/HeliosQingxiao.h"
#include "Weapons/HeliosCalamityStaff.h"
#include "Weapons/HeliosColaWeapon.h"
#include "Weapons/HeliosMysteriousHand.h"

namespace
{
const TCHAR* WeaponName(FName Id)
{
    if (Id == TEXT("rifle")) return TEXT("突击步枪");
    if (Id == TEXT("rocket")) return TEXT("火箭发射器");
    if (Id == TEXT("laser")) return TEXT("激光枪");
    if (Id == TEXT("shotgun")) return TEXT("霰弹枪");
    if (Id == TEXT("chopsticks")) return TEXT("金纹飞筷");
    if (Id == TEXT("cola")) return TEXT("可乐手雷");
    return nullptr;
}
}

float GetConfiguredReloadSeconds(const UObject* Context, const UHeliosWeaponDefinition* D)
{
    if (!D) return 0.f;
    const TCHAR* Name = WeaponName(D->WeaponId);
    if (!Name) return D->ReloadSeconds;
    return UHeliosCombatTempoSettings::Resolve(Context, EHeliosTempoFile::Recovery,
        TEXT("武器装填秒"), Name, D->ReloadSeconds);
}

void ApplyWeaponTempoSettings(const UObject* Context, UHeliosWeaponDefinition* D)
{
    if (!D) return;
    const auto Attack = [Context](const TCHAR* Section, const TCHAR* Key, float Default)
    {
        return UHeliosCombatTempoSettings::Resolve(Context, EHeliosTempoFile::Attack,
            Section, Key, Default);
    };
    const auto Recovery = [Context](const TCHAR* Section, const TCHAR* Key, float Default)
    {
        return UHeliosCombatTempoSettings::Resolve(Context, EHeliosTempoFile::Recovery,
            Section, Key, Default);
    };

    D->ReloadSeconds = GetConfiguredReloadSeconds(Context, D);
    const FName Id = D->WeaponId;
    if (Id == TEXT("rocket")) D->RefireSeconds = Attack(TEXT("玩家武器"), TEXT("火箭发射器开火间隔"), D->RefireSeconds);
    else if (Id == TEXT("laser"))
    {
        D->RefireSeconds = Attack(TEXT("玩家武器"), TEXT("激光枪脉冲间隔"), D->PulseSeconds);
        D->PulseSeconds = D->RefireSeconds;
    }
    else if (Id == TEXT("shotgun")) D->RefireSeconds = Attack(TEXT("玩家武器"), TEXT("霰弹枪开火间隔"), D->RefireSeconds);
    else if (Id == TEXT("chopsticks")) D->RefireSeconds = Attack(TEXT("玩家武器"), TEXT("金纹飞筷开火间隔"), D->RefireSeconds);
    else if (Id == TEXT("cola")) D->RefireSeconds = Attack(TEXT("玩家武器"), TEXT("可乐手雷投掷间隔"), D->RefireSeconds);

    if (auto* C = Cast<UHeliosColaWeaponDefinition>(D))
        C->ReleaseSeconds = Attack(TEXT("玩家武器"), TEXT("可乐手雷出手前摇"), C->ReleaseSeconds);
    if (auto* L = Cast<UHeliosLightstickDefinition>(D))
    {
        if (L->ComboSeconds.Num() < 3) L->ComboSeconds.SetNum(3);
        const TCHAR* Keys[] = {TEXT("荧光棒连击第一段"), TEXT("荧光棒连击第二段"), TEXT("荧光棒连击第三段")};
        const float Defaults[] = {.21f, .23f, .325f};
        for (int32 I = 0; I < 3; ++I)
            L->ComboSeconds[I] = Attack(TEXT("近战与法术"), Keys[I],
                L->ComboSeconds[I] > 0.f ? L->ComboSeconds[I] : Defaults[I]);
        L->ComboResetSeconds = Attack(TEXT("近战与法术"), TEXT("荧光棒连击重置等待"), L->ComboResetSeconds);
        L->ChargeSeconds = Attack(TEXT("近战与法术"), TEXT("荧光棒蓄力时间"), L->ChargeSeconds);
        L->HeavyRecoverySeconds = Attack(TEXT("近战与法术"), TEXT("荧光棒重击后摇"), L->HeavyRecoverySeconds);
    }
    if (auto* S = Cast<UHeliosSoulBannerDefinition>(D))
    {
        if (S->ComboSeconds.Num() < 3) S->ComboSeconds.SetNum(3);
        const TCHAR* Keys[] = {TEXT("万魂幡连击第一段"), TEXT("万魂幡连击第二段"), TEXT("万魂幡连击第三段")};
        const float Defaults[] = {.55f, .55f, .75f};
        for (int32 I = 0; I < 3; ++I)
            S->ComboSeconds[I] = Attack(TEXT("近战与法术"), Keys[I],
                S->ComboSeconds[I] > 0.f ? S->ComboSeconds[I] : Defaults[I]);
        S->PlantSeconds = Attack(TEXT("近战与法术"), TEXT("万魂幡插旗动作"), S->PlantSeconds);
        S->CooldownSeconds = Recovery(TEXT("技能冷却秒"), TEXT("万魂幡召唤"), S->CooldownSeconds);
    }
    if (auto* Q = Cast<UHeliosQingxiaoDefinition>(D))
    {
        Q->SlashSeconds = Attack(TEXT("近战与法术"), TEXT("青霄挥剑间隔"), Q->SlashSeconds);
        Q->SummonSeconds = Attack(TEXT("近战与法术"), TEXT("青霄召剑动作"), Q->SummonSeconds);
        Q->LaunchInterval = Attack(TEXT("近战与法术"), TEXT("青霄飞剑发射间隔"), Q->LaunchInterval);
    }
    if (auto* C = Cast<UHeliosCalamityDefinition>(D))
    {
        C->RefireSeconds = Attack(TEXT("近战与法术"), TEXT("灾厄法杖施法间隔"), C->RefireSeconds);
        C->CastWindupSeconds = Attack(TEXT("近战与法术"), TEXT("灾厄法杖施法前摇"), C->CastWindupSeconds);
        C->TowerInterval = Attack(TEXT("近战与法术"), TEXT("灾厄防御塔施法间隔"), C->TowerInterval);
        C->TowerCooldown = Recovery(TEXT("技能冷却秒"), TEXT("灾厄防御塔"), C->TowerCooldown);
    }
    if (auto* M = Cast<UHeliosMechDefinition>(D))
    {
        M->SweepSeconds=Attack(TEXT("黑金巨神"),TEXT("横扫间隔"),M->SweepSeconds);
        M->StompSeconds=Attack(TEXT("黑金巨神"),TEXT("踩踏间隔"),M->StompSeconds);
        M->ChargeSeconds=Attack(TEXT("黑金巨神"),TEXT("激光蓄能"),M->ChargeSeconds);
        M->MaximumBeamSeconds=Attack(TEXT("黑金巨神"),TEXT("持续发射上限"),M->MaximumBeamSeconds);
        M->CooldownSeconds=Recovery(TEXT("黑金巨神"),TEXT("激光冷却"),M->CooldownSeconds);
    }
    if (auto* H = Cast<UHeliosMysteriousHandDefinition>(D))
        H->RefireSeconds = Attack(TEXT("玩家武器"), TEXT("神秘人激光间隔"), H->RefireSeconds);
}
