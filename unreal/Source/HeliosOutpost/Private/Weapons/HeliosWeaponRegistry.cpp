#include "Weapons/HeliosWeaponRegistry.h"
#include "Weapons/HeliosMysteriousHand.h"
#include "Weapons/HeliosSunShangXiang.h"
#include "Weapons/HeliosZhongXiaoKui.h"
#include "Weapons/HeliosMechWeapon.h"
#include "Weapons/HeliosFighterWeapon.h"
#include "Weapons/HeliosChanZeus.h"
#include "Weapons/HeliosBlackDragonSoul.h"
#include "Weapons/HeliosWangZheKuiThunder.h"
#include "Weapons/HeliosYunSummerMegatron.h"
#include "Weapons/HeliosDetachedMegatronWeapon.h"
#include "Weapons/HeliosBuLianShiP90.h"

#include "Weapons/HeliosCombatWeapons.h"
#include "Weapons/HeliosCalamityStaff.h"
#include "Weapons/HeliosChopsticksWeapon.h"
#include "Weapons/HeliosColaWeapon.h"
#include "Weapons/HeliosLightstickWeapon.h"
#include "Weapons/HeliosWeaponPresentationProfile.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "NiagaraSystem.h"
#include "ShooterWeapon.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
constexpr const TCHAR* RegistryPath = TEXT("/Game/Helios/Weapons/Definitions/DA_WeaponRegistry.DA_WeaponRegistry");

UHeliosWeaponDefinition* MakeNativeDefinition(
    const TCHAR* ObjectName, const TCHAR* Id, const FText& Name, const FText& Description,
    TSubclassOf<AShooterWeapon> Class, int32 Magazine, float Reload,
    const TCHAR* PreviewPath, const TCHAR* MeshPath)
{
    UHeliosWeaponDefinition* Definition = NewObject<UHeliosWeaponDefinition>(
        GetTransientPackage(), FName(ObjectName), RF_Transient);
    Definition->WeaponId = FName(Id);
    Definition->DisplayName = Name;
    Definition->Description = Description;
    Definition->WeaponClass = Class;
    Definition->MagazineSize = Magazine;
    Definition->ReloadSeconds = Reload;
    if (PreviewPath && *PreviewPath)
    {
        Definition->PreviewTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(PreviewPath));
    }
    if (MeshPath && *MeshPath)
    {
        Definition->WeaponMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(MeshPath));
    }
    return Definition;
}

const TArray<TStrongObjectPtr<UHeliosWeaponDefinition>>& NativeDefinitions()
{
    static TArray<TStrongObjectPtr<UHeliosWeaponDefinition>> Definitions;
    if (Definitions.IsEmpty())
    {
        TSubclassOf<AShooterWeapon> RifleClass = LoadClass<AShooterWeapon>(nullptr,
            TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_Rifle.BP_ShooterWeapon_Rifle_C"));
        Definitions.Emplace(MakeNativeDefinition(TEXT("NativeWeapon_Rifle"), TEXT("rifle"),
            NSLOCTEXT("HeliosWeapons", "RifleName", "突击步枪"),
            NSLOCTEXT("HeliosWeapons", "RifleDescription", "均衡可靠的全自动步枪。"),
            RifleClass, 30, 1.1f,
            TEXT("/Game/Helios/Weapons/Textures/T_Rifle.T_Rifle"), TEXT("")));
        Definitions.Emplace(MakeNativeDefinition(TEXT("NativeWeapon_Rocket"), TEXT("rocket"),
            NSLOCTEXT("HeliosWeapons", "RocketName", "火箭发射器"),
            NSLOCTEXT("HeliosWeapons", "RocketDescription", "发射低速火箭，近距离爆炸伤害极高。"),
            AHeliosRocketWeapon::StaticClass(), 1, 2.0f,
            TEXT("/Game/Helios/Weapons/Textures/T_Rocket.T_Rocket"),
            TEXT("/Game/Helios/Weapons/Meshes/SM_Rocket.SM_Rocket")));
        Definitions.Emplace(MakeNativeDefinition(TEXT("NativeWeapon_Laser"), TEXT("laser"),
            NSLOCTEXT("HeliosWeapons", "LaserName", "激光枪"),
            NSLOCTEXT("HeliosWeapons", "LaserDescription", "使用可充能电池的持续精准光束。"),
            AHeliosLaserWeapon::StaticClass(), 100, 1.5f,
            TEXT("/Game/Helios/Weapons/Textures/T_Laser.T_Laser"),
            TEXT("/Game/Helios/Weapons/Meshes/SM_Laser.SM_Laser")));
        Definitions.Emplace(MakeNativeDefinition(TEXT("NativeWeapon_Shotgun"), TEXT("shotgun"),
            NSLOCTEXT("HeliosWeapons", "ShotgunName", "霰弹枪"),
            NSLOCTEXT("HeliosWeapons", "ShotgunDescription", "一次发射十颗弹丸，近距离威力强劲。"),
            AHeliosShotgunWeapon::StaticClass(), 8, 2.0f,
            TEXT("/Game/Helios/Weapons/Textures/T_Shotgun.T_Shotgun"),
            TEXT("/Game/Helios/Weapons/Meshes/SM_Shotgun.SM_Shotgun")));

        UHeliosWeaponDefinition* Rifle = Definitions[0].Get();
        Rifle->AttachmentRotation = FRotator(0.0f, -90.0f, 0.0f);
        Rifle->AttachmentScale = FVector(0.8f);

        UHeliosWeaponDefinition* Rocket = Definitions[1].Get();
        Rocket->MuzzleEffect = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Helios/Weapons/Effects/NS_RocketMuzzle.NS_RocketMuzzle")));
        Rocket->TrailEffect = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Helios/Weapons/Effects/NS_RocketTrail.NS_RocketTrail")));
        Rocket->ExplosionEffect = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Helios/Weapons/Effects/NS_RocketExplosion.NS_RocketExplosion")));
        Rocket->Damage = 360.0f;
        Rocket->MinimumDamage = 60.0f;
        Rocket->Range = 2000.0f;
        Rocket->ProjectileSpeed = 10000.0f;
        Rocket->RefireSeconds = 0.85f;
        Rocket->Recoil = 2.2f;
        Rocket->ModelMuzzleLocation = FVector(102.0f, 0.0f, 12.0f);
        Rocket->FireSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(
            TEXT("/Game/Helios/Weapons/Audio/SFX_RocketFire.SFX_RocketFire")));
        Rocket->ReloadSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(
            TEXT("/Game/Helios/Weapons/Audio/SFX_RocketReload.SFX_RocketReload")));
        Rocket->ActionSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(
            TEXT("/Game/Helios/Weapons/Audio/SFX_RocketImpact.SFX_RocketImpact")));

        UHeliosWeaponDefinition* Laser = Definitions[2].Get();
        Laser->Damage = 8.0f;
        Laser->MinimumDamage = 8.0f;
        Laser->Range = 6000.0f;
        Laser->RefireSeconds = 0.1f;
        Laser->PulseSeconds = 0.1f;
        Laser->EnergyPerPulse = 2;
        Laser->Recoil = 0.08f;
        Laser->ModelMuzzleLocation = FVector(102.0f, 0.0f, 10.0f);
        Laser->ActionSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(
            TEXT("/Game/Helios/Weapons/Audio/SFX_LaserLoop.SFX_LaserLoop")));
        Laser->ReloadSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(
            TEXT("/Game/Helios/Weapons/Audio/SFX_LaserReload.SFX_LaserReload")));

        UHeliosWeaponDefinition* Shotgun = Definitions[3].Get();
        Shotgun->MuzzleEffect = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Helios/Weapons/Effects/NS_ShotgunMuzzle.NS_ShotgunMuzzle")));
        Shotgun->ImpactEffect = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Helios/Weapons/Effects/NS_ShotgunImpact.NS_ShotgunImpact")));
        Shotgun->EnemyImpactEffect = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Helios/Weapons/Effects/NS_EnemyImpact.NS_EnemyImpact")));
        Shotgun->TracerEffect = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Helios/Weapons/Effects/NS_ShotgunTracer.NS_ShotgunTracer")));
        Shotgun->Damage = 14.0f;
        Shotgun->MinimumDamage = 3.0f;
        Shotgun->Range = 3000.0f;
        Shotgun->FalloffStart = 800.0f;
        Shotgun->PelletCount = 10;
        Shotgun->SpreadHalfAngle = 5.0f;
        Shotgun->RefireSeconds = 0.85f;
        Shotgun->Recoil = 2.8f;
        Shotgun->ModelMuzzleLocation = FVector(118.0f, 0.0f, 13.0f);
        Shotgun->FireSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(
            TEXT("/Game/Helios/Weapons/Audio/SFX_ShotgunFire.SFX_ShotgunFire")));
        Shotgun->ReloadSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(
            TEXT("/Game/Helios/Weapons/Audio/SFX_ShotgunReload.SFX_ShotgunReload")));

        Definitions.Emplace(MakeNativeDefinition(TEXT("NativeWeapon_Chopsticks"),TEXT("chopsticks"),
            NSLOCTEXT("HeliosWeapons","ChopsticksName","金纹飞筷"),
            NSLOCTEXT("HeliosWeapons","ChopsticksDescription","双筷齐出，金芒破空。每束六对，按住左键连掷，按 R 补充。"),
            AHeliosChopsticksWeapon::StaticClass(),6,1.4f,
            TEXT("/Game/Helios/Weapons/Chopsticks/T_Chopsticks"),TEXT("/Game/Helios/Weapons/Chopsticks/SM_Chopsticks")));
        auto* Chopsticks=Definitions.Last().Get();
        Chopsticks->Damage=Chopsticks->MinimumDamage=40.f; Chopsticks->Range=5000.f;
        Chopsticks->ProjectileSpeed=6500.f; Chopsticks->RefireSeconds=.4f; Chopsticks->Recoil=.18f;
        Chopsticks->PelletCount=2; Chopsticks->MuzzleOffset=0; Chopsticks->ModelMuzzleLocation=FVector(20,0,0);
        Chopsticks->PresentationProfile=TSoftObjectPtr<UHeliosWeaponPresentationProfile>(FSoftObjectPath(TEXT("/Game/Helios/Weapons/Chopsticks/DA_ChopsticksPresentation")));
        Chopsticks->FireSound=TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Helios/Weapons/Chopsticks/SFX_ChopsticksThrow")));
        Chopsticks->ReloadSound=TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Helios/Weapons/Chopsticks/SFX_ChopsticksReload")));
        Chopsticks->ActionSound=TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Helios/Weapons/Chopsticks/SFX_ChopsticksImpact")));
        Chopsticks->MuzzleEffect=TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Helios/Weapons/Chopsticks/NS_ChopsticksRelease")));
        Chopsticks->TrailEffect=TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Helios/Weapons/Chopsticks/NS_ChopsticksTrail")));
        Chopsticks->ImpactEffect=TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Helios/Weapons/Chopsticks/NS_ChopsticksImpact")));

        for (int32 Index = 1; Index < Definitions.Num(); ++Index)
        {
            Definitions[Index]->AttachmentLocation = FVector(55.0f, 22.0f, -30.0f);
            Definitions[Index]->AttachmentScale = FVector(0.55f);
            Definitions[Index]->DrySound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(
                TEXT("/Game/Helios/Weapons/Audio/SFX_WeaponDry.SFX_WeaponDry")));
        }
        auto* Cola=NewObject<UHeliosColaWeaponDefinition>(GetTransientPackage(),TEXT("NativeWeapon_Cola"),RF_Transient);
        Cola->WeaponMesh=TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Helios/Weapons/Cola/SM_ColaCan")));
        Cola->PreviewTexture=TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Helios/Weapons/Cola/T_Cola")));
        Cola->PresentationProfile=TSoftObjectPtr<UHeliosWeaponPresentationProfile>(FSoftObjectPath(TEXT("/Game/Helios/Weapons/Cola/DA_ColaPresentation")));
        Definitions.Emplace(Cola);
        auto* Staff=NewObject<UHeliosCalamityDefinition>(GetTransientPackage(),TEXT("NativeWeapon_Calamity"),RF_Transient);
        Staff->WeaponClass=AHeliosCalamityStaff::StaticClass();
        Staff->Description=FText::FromString(TEXT("四灾听令，王权化塔。左键随机释放地震、火球、龙卷风、毒药瓶；右键本体化为防御塔，20秒后自动回归。"));
        Staff->WeaponMesh=TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Helios/Weapons/Calamity/SM_CS_Staff")));
        Staff->PreviewTexture=TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Helios/Weapons/Calamity/T_Calamity")));
        Staff->PresentationProfile=TSoftObjectPtr<UHeliosWeaponPresentationProfile>(FSoftObjectPath(TEXT("/Game/Helios/Weapons/Calamity/DA_CalamityPresentation")));
        Definitions.Emplace(Staff);
        Definitions.Emplace(NewObject<UHeliosSunShangXiangDefinition>(GetTransientPackage(), TEXT("NativeWeapon_SunShangXiang"), RF_Transient));
        Definitions.Emplace(NewObject<UHeliosChanZeusDefinition>(GetTransientPackage(), TEXT("NativeWeapon_ChanZeus"), RF_Transient));
        Definitions.Emplace(NewObject<UHeliosBlackDragonSoulDefinition>(GetTransientPackage(), TEXT("NativeWeapon_BlackDragonSoul"), RF_Transient));
        Definitions.Emplace(NewObject<UHeliosWangZheKuiThunderDefinition>(GetTransientPackage(), TEXT("NativeWeapon_WangZheKuiThunder"), RF_Transient));
        Definitions.Emplace(NewObject<UHeliosYunSummerMegatronDefinition>(GetTransientPackage(), TEXT("NativeWeapon_YunSummerMegatron"), RF_Transient));
        Definitions.Last()->WeaponClass=AHeliosDetachedMegatronWeapon::StaticClass();
        Definitions.Emplace(NewObject<UHeliosBuLianShiP90Definition>(GetTransientPackage(), TEXT("NativeWeapon_BuLianShiP90"), RF_Transient));
        Definitions.Emplace(NewObject<UHeliosZhongXiaoKuiDefinition>(GetTransientPackage(), TEXT("NativeWeapon_ZhongXiaoKui"), RF_Transient));
        Definitions.Emplace(NewObject<UHeliosMysteriousHandDefinition>(GetTransientPackage(), TEXT("NativeWeapon_MysteriousHand"), RF_Transient));
        Definitions.Emplace(NewObject<UHeliosMechDefinition>(GetTransientPackage(), TEXT("NativeWeapon_Mech"), RF_Transient));
        Definitions.Emplace(NewObject<UHeliosFighterDefinition>(GetTransientPackage(), TEXT("NativeWeapon_Fighter"), RF_Transient));

    }
    return Definitions;
}

UHeliosWeaponDefinition* FindRegistered(FName Id)
{
    if (UHeliosWeaponRegistry* Registry = LoadObject<UHeliosWeaponRegistry>(nullptr, RegistryPath))
    {
        for (const TSoftObjectPtr<UHeliosWeaponDefinition>& SoftDefinition : Registry->RegisteredDefinitions)
        {
            if (UHeliosWeaponDefinition* Definition = SoftDefinition.LoadSynchronous())
            {
                if (Definition->WeaponId == Id && Definition->WeaponClass)
                {
                    return Definition;
                }
            }
        }
    }
    return nullptr;
}
}

TArray<UHeliosWeaponDefinition*> UHeliosWeaponRegistry::GetAvailableWeapons()
{
    TArray<UHeliosWeaponDefinition*> Result;
    Result.Reserve(7);
    for (const TStrongObjectPtr<UHeliosWeaponDefinition>& Native : NativeDefinitions())
    {
        if (IsRetiredWeapon(Native->WeaponId)) continue;
        UHeliosWeaponDefinition* Definition = FindRegistered(Native->WeaponId);
        Result.Add(Definition ? Definition : Native.Get());
    }
    if (UHeliosWeaponRegistry* Registry = LoadObject<UHeliosWeaponRegistry>(nullptr, RegistryPath))
    {
        TSet<FName> ExistingIds;
        for (const UHeliosWeaponDefinition* Definition : Result)
        {
            if (Definition) ExistingIds.Add(Definition->WeaponId);
        }
        for (const TSoftObjectPtr<UHeliosWeaponDefinition>& SoftDefinition : Registry->RegisteredDefinitions)
        {
            UHeliosWeaponDefinition* Definition = SoftDefinition.LoadSynchronous();
            if (Definition && !Definition->WeaponId.IsNone() && !IsRetiredWeapon(Definition->WeaponId) && Definition->WeaponClass &&
                !ExistingIds.Contains(Definition->WeaponId))
            {
                Result.Add(Definition);
                ExistingIds.Add(Definition->WeaponId);
            }
        }
    }
    for (auto* Definition : Result)
    {
        const FName Id = Definition->WeaponId;
        if (Id == TEXT("cfm_yun_summer_megatron")) Definition->WeaponClass = AHeliosDetachedMegatronWeapon::StaticClass();
        const TCHAR* Name = Id == TEXT("cfm_sunshangxiang") ? TEXT("孙尚香双刃") :
            Id == TEXT("cfm_zhongxiaokui") ? TEXT("唐刀") :
            Id == TEXT("cfm_chan_zeus") ? TEXT("宙斯") :
            Id == TEXT("cfm_bulianshi_p90") ? TEXT("P90双脊龙") :
            Id == TEXT("cfm_wangzhekui_thunder") ? TEXT("王者之霆") :
            Id == TEXT("cfm_yun_summer_megatron") ? TEXT("威震天") :
            Id == TEXT("mysterious_hand") ? TEXT("神秘手势") : nullptr;
        if (Name) Definition->DisplayName = FText::FromString(Name);
        const TCHAR* Description = Id == TEXT("cfm_sunshangxiang") ? TEXT("双刃近战。左键轻击，右键重击，G 四连斩；无需弹药。") :
            Id == TEXT("cfm_zhongxiaokui") ? TEXT("唐刀、强化唐刀与飞镖。Q 切换唐刀模式，V 切换飞镖，G 技能；强化唐刀右键剑气，R 装填。") :
            Id == TEXT("cfm_chan_zeus") ? TEXT("宙斯霰弹枪。左键连续射击，R 换弹，G 展示。") :
            Id == TEXT("cfm_bulianshi_p90") ? TEXT("P90双脊龙。左键连续射击，R 换弹，G 展示。") :
            Id == TEXT("cfm_wangzhekui_thunder") ? TEXT("王者之霆。左键连续射击，Q/V 或右键切换普通霰弹与独头弹，R 换弹，G 展示。") :
            Id == TEXT("cfm_yun_summer_megatron") ? TEXT("威震天挑战榴弹。左键射击，R 换弹，G 展示。") : nullptr;
        if (Description) Definition->Description = FText::FromString(Description);
    }
    return Result;
}

bool UHeliosWeaponRegistry::IsRetiredWeapon(FName WeaponId)
{
    return WeaponId == TEXT("rifle") || WeaponId == TEXT("rocket") ||
        WeaponId == TEXT("laser") || WeaponId == TEXT("shotgun");
}

UHeliosWeaponDefinition* UHeliosWeaponRegistry::FindWeapon(FName WeaponId)
{
    if (WeaponId.IsNone() || IsRetiredWeapon(WeaponId))
    {
        return nullptr;
    }
    for (UHeliosWeaponDefinition* Definition : GetAvailableWeapons())
    {
        if (Definition && Definition->WeaponId == WeaponId)
        {
            return Definition;
        }
    }
    return nullptr;
}
