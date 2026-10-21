#include "Player/HeliosLoadout.h"
#include "Player/HeliosCharacterDefinition.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Weapons/HeliosYunSummerMegatron.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"

namespace
{
bool RemovedWeapon(FName Id)
{ return Id==TEXT("rifle") || Id==TEXT("rocket") || Id==TEXT("laser") || Id==TEXT("shotgun"); }
}

FHeliosLoadout FHeliosLoadout::MakeDefault() { return FHeliosLoadout(); }
FHeliosLoadout FHeliosLoadout::FromLegacyWeapon(FName Id)
{
    FHeliosLoadout Result;
    if (Id.IsNone() || RemovedWeapon(Id)) return Result;
    Result.PrimaryWeaponId = Id;
    const FName Character = FHeliosCharacterRegistry::CanonicalCharacterId(Id);
    if (const auto* Definition=FHeliosCharacterRegistry::FindCharacter(Character))
    {
        Result.CharacterId=Character;
        if(Character!=TEXT("cfm_yun_summer"))Result.CharacterVariantId=Definition->DefaultVariantId;
    }
    return Result;
}
FHeliosLoadout FHeliosLoadout::Normalize(const FHeliosLoadout& Input)
{
    FHeliosLoadout Result=Input;
    Result.CharacterId=FHeliosCharacterRegistry::CanonicalCharacterId(Result.CharacterId);
    Result.SecondaryWeaponId=NAME_None; Result.MeleeWeaponId=NAME_None;
    if (Result.CharacterId==TEXT("sword_dog"))
    { Result.CharacterVariantId=NAME_None; Result.PrimaryWeaponId=NAME_None; return Result; }
    if (RemovedWeapon(Result.PrimaryWeaponId)) return MakeDefault();
    if (!FHeliosCharacterRegistry::FindCharacter(Result.CharacterId)) Result.CharacterId=TEXT("cfm_chan");
    if(const auto* Character=FHeliosCharacterRegistry::FindCharacter(Result.CharacterId))
    {
        if(Result.CharacterId==TEXT("cfm_yun_summer"))
        {if(!Result.CharacterVariantId.IsNone()&&!Character->FindForm(Result.CharacterVariantId))Result.CharacterVariantId=NAME_None;}
        else if(Result.CharacterVariantId.IsNone()||!Character->FindForm(Result.CharacterVariantId))Result.CharacterVariantId=Character->DefaultVariantId;
    }
    if (!UHeliosWeaponRegistry::FindWeapon(Result.PrimaryWeaponId)) Result.PrimaryWeaponId=TEXT("cfm_chan_zeus");
    return Result;
}
FHeliosLoadout FHeliosLoadout::ResolveLegacyVariant(const FHeliosLoadout& Input,UGameInstance* Instance,int32 CompanionSlot)
{
    auto Result=Normalize(Input);
    if(Result.CharacterId!=TEXT("cfm_yun_summer")||!Result.CharacterVariantId.IsNone())return Result;
    auto Form=EHeliosYunSummerForm::SP;
    if(Instance)
        if(const auto* Session=Instance->GetSubsystem<UHeliosYunSummerSession>())
        {
            if(CompanionSlot==INDEX_NONE)Form=Session->GetPlayerForm();
            else if(CompanionSlot>=0&&CompanionSlot<4)Form=Session->GetPetForm(CompanionSlot);
        }
    Result.CharacterVariantId=HeliosYunSummer::FormCode(Form);
    return Result;
}
bool FHeliosLoadout::Validate(const FHeliosLoadout& Input, FString& Error)
{
    Error.Empty();
    if (!Input.SecondaryWeaponId.IsNone() || !Input.MeleeWeaponId.IsNone())
    { Error=TEXT("副武器和近战槽尚未开放。"); return false; }
    if (Input.CharacterId==TEXT("sword_dog"))
    {
        if (!Input.PrimaryWeaponId.IsNone() || !Input.CharacterVariantId.IsNone())
        { Error=TEXT("刀盾狗使用自身固定刀盾。"); return false; }
        return true;
    }
    const auto* Character=FHeliosCharacterRegistry::FindCharacter(Input.CharacterId);
    if (!Character || Character->CharacterId!=Input.CharacterId)
    { Error=TEXT("未知人物。"); return false; }
    const auto* Form=Character->FindForm(Input.CharacterVariantId);
    if (!Form)
    { Error=TEXT("未知人物形态。"); return false; }
    if(!Character->bPresentationReady||!Form->bPresentationReady)
    { Error=TEXT("人物形态尚未准备完成。"); return false; }
    if (RemovedWeapon(Input.PrimaryWeaponId) || !UHeliosWeaponRegistry::FindWeapon(Input.PrimaryWeaponId))
    { Error=TEXT("未知或已移除的主武器。"); return false; }
    return true;
}
bool FHeliosLoadout::operator==(const FHeliosLoadout& Other) const
{
    return CharacterId==Other.CharacterId && CharacterVariantId==Other.CharacterVariantId &&
        PrimaryWeaponId==Other.PrimaryWeaponId && SecondaryWeaponId==Other.SecondaryWeaponId && MeleeWeaponId==Other.MeleeWeaponId;
}
bool HeliosLoadout::ReadJson(const TSharedPtr<FJsonObject>& Json,FHeliosLoadout& Out)
{
    if (!Json) return false;
    FString Weapon,Character,Variant,Secondary,Melee;
    const bool HasPrimary=Json->TryGetStringField(TEXT("primary_weapon_id"),Weapon);
    if (!HasPrimary && !Json->TryGetStringField(TEXT("weapon_id"),Weapon)) return false;
    FHeliosLoadout Parsed=FHeliosLoadout::FromLegacyWeapon(FName(*Weapon));
    if (Json->TryGetStringField(TEXT("character_id"),Character))
    {Parsed.CharacterId=FName(*Character);Parsed.CharacterVariantId=NAME_None;}
    if (Json->TryGetStringField(TEXT("character_variant_id"),Variant)) Parsed.CharacterVariantId=FName(*Variant);
    if (Json->TryGetStringField(TEXT("secondary_weapon_id"),Secondary)) Parsed.SecondaryWeaponId=FName(*Secondary);
    if (Json->TryGetStringField(TEXT("melee_weapon_id"),Melee)) Parsed.MeleeWeaponId=FName(*Melee);
    Out=FHeliosLoadout::Normalize(Parsed);
    return true;
}
void HeliosLoadout::WriteJson(const TSharedRef<FJsonObject>& Json,const FHeliosLoadout& Loadout)
{
    Json->SetStringField(TEXT("character_id"),Loadout.CharacterId.ToString());
    Json->SetStringField(TEXT("character_variant_id"),Loadout.CharacterVariantId.ToString());
    Json->SetStringField(TEXT("primary_weapon_id"),Loadout.PrimaryWeaponId.ToString());
    Json->SetStringField(TEXT("secondary_weapon_id"),Loadout.SecondaryWeaponId.ToString());
    Json->SetStringField(TEXT("melee_weapon_id"),Loadout.MeleeWeaponId.ToString());
    Json->SetStringField(TEXT("weapon_id"),Loadout.PrimaryWeaponId.ToString());
}
