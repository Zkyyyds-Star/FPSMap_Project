#include "Combat/CombatDamageSettings.h"
#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogCombatDamage, Log, All);

namespace
{
struct FDamageDefault { const TCHAR* File; const TCHAR* Section; const TCHAR* Key; float Value; };
const FDamageDefault CombatDamageDefaults[] = {
    {TEXT("武器伤害.ini"), TEXT("突击步枪"), TEXT("每颗子弹"), 25.0f},
    {TEXT("武器伤害.ini"), TEXT("模板手枪"), TEXT("每颗子弹"), 25.0f},
    {TEXT("武器伤害.ini"), TEXT("模板榴弹"), TEXT("每次爆炸"), 50.0f},
    {TEXT("武器伤害.ini"), TEXT("模板投射物"), TEXT("普通子弹"), 25.0f},
    {TEXT("武器伤害.ini"), TEXT("模板投射物"), TEXT("榴弹爆炸"), 50.0f},
    {TEXT("武器伤害.ini"), TEXT("火箭发射器"), TEXT("中心伤害"), 360.0f},
    {TEXT("武器伤害.ini"), TEXT("火箭发射器"), TEXT("边缘伤害"), 60.0f},
    {TEXT("武器伤害.ini"), TEXT("激光枪"), TEXT("每次脉冲"), 8.0f},
    {TEXT("武器伤害.ini"), TEXT("霰弹枪"), TEXT("每颗弹丸近距离"), 14.0f},
    {TEXT("武器伤害.ini"), TEXT("霰弹枪"), TEXT("每颗弹丸远距离"), 3.0f},
    {TEXT("武器伤害.ini"), TEXT("金纹飞筷"), TEXT("每根筷子"), 40.0f},
    {TEXT("武器伤害.ini"), TEXT("可乐手雷"), TEXT("中心伤害"), 360.0f},
    {TEXT("武器伤害.ini"), TEXT("可乐手雷"), TEXT("边缘伤害"), 80.0f},
    {TEXT("武器伤害.ini"), TEXT("荧光棒"), TEXT("连击第一段"), 105.0f},
    {TEXT("武器伤害.ini"), TEXT("荧光棒"), TEXT("连击第二段"), 120.0f},
    {TEXT("武器伤害.ini"), TEXT("荧光棒"), TEXT("连击第三段"), 180.0f},
    {TEXT("武器伤害.ini"), TEXT("荧光棒"), TEXT("蓄力最低伤害"), 180.0f},
    {TEXT("武器伤害.ini"), TEXT("荧光棒"), TEXT("蓄力最高伤害"), 360.0f},
    {TEXT("武器伤害.ini"), TEXT("万魂幡"), TEXT("连击第一段"), 100.0f},
    {TEXT("武器伤害.ini"), TEXT("万魂幡"), TEXT("连击第二段"), 120.0f},
    {TEXT("武器伤害.ini"), TEXT("万魂幡"), TEXT("连击第三段"), 160.0f},
    {TEXT("武器伤害.ini"), TEXT("万魂幡"), TEXT("骷髅战士每次命中"), 25.0f},
    {TEXT("武器伤害.ini"), TEXT("万魂幡"), TEXT("骷髅法师每颗法球"), 30.0f},
    {TEXT("武器伤害.ini"), TEXT("青霄"), TEXT("普通挥剑"), 70.0f},
    {TEXT("武器伤害.ini"), TEXT("青霄"), TEXT("每柄飞剑"), 150.0f},
    {TEXT("武器伤害.ini"), TEXT("灾厄法杖"), TEXT("地震每次脉冲"), 100.0f},
    {TEXT("武器伤害.ini"), TEXT("灾厄法杖"), TEXT("火球中心伤害"), 280.0f},
    {TEXT("武器伤害.ini"), TEXT("灾厄法杖"), TEXT("火球边缘伤害"), 100.0f},
    {TEXT("武器伤害.ini"), TEXT("灾厄法杖"), TEXT("龙卷风每次脉冲"), 100.0f},
    {TEXT("武器伤害.ini"), TEXT("灾厄法杖"), TEXT("毒区域每次结算"), 100.0f},
    {TEXT("武器伤害.ini"), TEXT("灾厄法杖"), TEXT("防御塔地震火球伤害倍率"), 0.7f},
    {TEXT("武器伤害.ini"), TEXT("助战"), TEXT("刀盾狗每次命中"), 25.0f},
    {TEXT("武器伤害.ini"), TEXT("助战"), TEXT("刀盾狗格挡减伤比例"), 0.6f},
    {TEXT("武器伤害.ini"), TEXT("助战"), TEXT("蝴蝶雷神每颗子弹"), 12.0f},
    {TEXT("怪物伤害.ini"), TEXT("普通敌人"), TEXT("步枪兵每颗子弹"), 5.0f},
    {TEXT("怪物伤害.ini"), TEXT("普通敌人"), TEXT("冲锋兵每次命中"), 24.0f},
    {TEXT("怪物伤害.ini"), TEXT("普通敌人"), TEXT("游击兵每颗子弹"), 4.0f},
    {TEXT("怪物伤害.ini"), TEXT("普通敌人"), TEXT("篮球兵每颗篮球"), 22.0f},
    {TEXT("怪物伤害.ini"), TEXT("Boss"), TEXT("基础攻击备用"), 18.0f},
    {TEXT("怪物伤害.ini"), TEXT("Boss"), TEXT("近距离攻击"), 20.0f},
    {TEXT("怪物伤害.ini"), TEXT("Boss"), TEXT("冲锋攻击"), 30.0f},
    {TEXT("怪物伤害.ini"), TEXT("射击模板敌人"), TEXT("普通子弹"), 25.0f},
    {TEXT("怪物伤害.ini"), TEXT("射击模板敌人"), TEXT("榴弹爆炸"), 50.0f},
    {TEXT("武器伤害.ini"), TEXT("神秘人"), TEXT("激光伤害"), 60.0f},
    {TEXT("武器伤害.ini"), TEXT("神秘人"), TEXT("导弹中心伤害"), 200.0f},
    {TEXT("武器伤害.ini"), TEXT("神秘人"), TEXT("导弹边缘伤害"), 40.0f},
    {TEXT("武器伤害.ini"), TEXT("黑金巨神"), TEXT("机械臂横扫"), 600.0f},
    {TEXT("武器伤害.ini"), TEXT("黑金巨神"), TEXT("机械重踏"), 1000.0f},
    {TEXT("武器伤害.ini"), TEXT("黑金巨神"), TEXT("激光每次脉冲"), 180.0f},
    {TEXT("武器伤害.ini"), TEXT("终极猎手孙尚香"), TEXT("轻击整招"), 100.0f},
    {TEXT("武器伤害.ini"), TEXT("终极猎手孙尚香"), TEXT("重击整招"), 250.0f},
    {TEXT("武器伤害.ini"), TEXT("终极猎手孙尚香"), TEXT("技能每段"), 150.0f},
    {TEXT("武器伤害.ini"), TEXT("孙尚香伙伴"), TEXT("轻击整招"), 30.0f},
    {TEXT("武器伤害.ini"), TEXT("孙尚香伙伴"), TEXT("重击整招"), 75.0f},
    {TEXT("武器伤害.ini"), TEXT("孙尚香伙伴"), TEXT("技能每段"), 45.0f},
};
static_assert(UE_ARRAY_COUNT(CombatDamageDefaults) == static_cast<int32>(ECombatDamageType::Count));
}
FCombatDamageTable::FCombatDamageTable() { for (const auto& D : CombatDamageDefaults) Values.Add(D.Value); }
float FCombatDamageTable::Get(ECombatDamageType Type) const { return Values[static_cast<int32>(Type)]; }
float FCombatDamageTable::Default(ECombatDamageType Type) { return CombatDamageDefaults[static_cast<int32>(Type)].Value; }
const TCHAR* FCombatDamageTable::File(ECombatDamageType Type) { return CombatDamageDefaults[static_cast<int32>(Type)].File; }
const TCHAR* FCombatDamageTable::Section(ECombatDamageType Type) { return CombatDamageDefaults[static_cast<int32>(Type)].Section; }
const TCHAR* FCombatDamageTable::Key(ECombatDamageType Type) { return CombatDamageDefaults[static_cast<int32>(Type)].Key; }

void FCombatDamageTable::LoadDirectory(const FString& Directory, bool bLogWarnings)
{
    *this = FCombatDamageTable();
    for (const TCHAR* Filename : {TEXT("武器伤害.ini"), TEXT("怪物伤害.ini")})
    {
        const FString Path = Directory / Filename;
        FString Contents;
        if (!FFileHelper::LoadFileToString(Contents, *Path))
        {
            if (bLogWarnings) UE_LOG(LogCombatDamage, Warning, TEXT("Cannot read %s; using defaults."), *Path);
            continue;
        }
        FConfigFile Config;
        Config.ProcessInputFileContents(Contents, Path);
        for (int32 I = 0; I < UE_ARRAY_COUNT(CombatDamageDefaults); ++I)
        {
            const auto& D = CombatDamageDefaults[I];
            if (FCString::Strcmp(D.File, Filename) != 0) continue;
            FString Text;
            const bool bPresent = Config.GetString(D.Section, D.Key, Text);
            Text.TrimStartAndEndInline();
            const float Value = static_cast<float>(FCString::Atod(*Text));
            const bool bRatioValid = I != static_cast<int32>(ECombatDamageType::DogReduction) || Value <= 1.f;
            if (bPresent && !Text.IsEmpty() && Text.IsNumeric() && FMath::IsFinite(Value) && Value >= 0.f && bRatioValid)
                Values[I] = Value;
            else if (bLogWarnings)
                UE_LOG(LogCombatDamage, Warning, TEXT("%s [%s] %s raw='%s' reason=%s; using %.9g."),
                    *Path, D.Section, D.Key, *Text,
                    !bPresent ? TEXT("missing key") : Text.IsEmpty() ? TEXT("empty value") :
                    !Text.IsNumeric() ? TEXT("not a number") : !FMath::IsFinite(Value) ? TEXT("not finite") :
                    Value < 0.f ? TEXT("negative damage") : TEXT("reduction outside [0,1]"), D.Value);
        }
    }
    const TPair<ECombatDamageType,ECombatDamageType> Bounds[] = {
        {ECombatDamageType::RocketMin,ECombatDamageType::RocketMax},
        {ECombatDamageType::ShotgunMin,ECombatDamageType::ShotgunMax},
        {ECombatDamageType::ColaMin,ECombatDamageType::ColaMax},
        {ECombatDamageType::HeavyMin,ECombatDamageType::HeavyMax},
        {ECombatDamageType::FireMin,ECombatDamageType::FireMax},
        {ECombatDamageType::MysteriousMissileMin,ECombatDamageType::MysteriousMissileMax}
    };
    for (const auto& Pair : Bounds)
        if (Get(Pair.Key) > Get(Pair.Value))
        {
            const float Fallback = FMath::Min(Default(Pair.Key), Get(Pair.Value));
            if (bLogWarnings) UE_LOG(LogCombatDamage, Warning, TEXT("[%s] %s exceeds maximum; using %.3g."),
                Section(Pair.Key), Key(Pair.Key), Fallback);
            Values[static_cast<int32>(Pair.Key)] = Fallback;
        }
    if (bLogWarnings)
        for (int32 I = 0; I < UE_ARRAY_COUNT(CombatDamageDefaults); ++I)
        {
            const auto& D = CombatDamageDefaults[I];
            UE_LOG(LogCombatDamage, Display, TEXT("COMBAT_VALUE %s|%s|%s=%.9g"), D.File, D.Section, D.Key, Values[I]);
        }
}
FString UCombatDamageSettings::Directory()
{
    return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Settings/战斗数值"));
}
void UCombatDamageSettings::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogCombatDamage, Display, TEXT("COMBAT_MAP_LOAD map=%s directory=%s"), *GetWorld()->GetMapName(), *Directory());
    Table.LoadDirectory(Directory());
    UE_LOG(LogCombatDamage, Display, TEXT("Loaded combat damage for map %s: %s"), *GetWorld()->GetMapName(), *Directory());
}
bool UCombatDamageSettings::DoesSupportWorldType(EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}
float UCombatDamageSettings::Resolve(const UObject* Context, ECombatDamageType Type)
{
    if (const UWorld* World = Context ? Context->GetWorld() : nullptr)
        if (const auto* Settings = World->GetSubsystem<UCombatDamageSettings>())
            return Settings->Table.Get(Type);
    return FCombatDamageTable::Default(Type);
}
