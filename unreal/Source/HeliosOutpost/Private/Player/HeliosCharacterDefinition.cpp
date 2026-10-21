#include "Player/HeliosCharacterDefinition.h"
#include "Engine/SkeletalMesh.h"
#include "Player/HeliosGuanXiaoYuAbilityComponent.h"
#include "Engine/Texture2D.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
const TArray<TStrongObjectPtr<UHeliosCharacterDefinition>>& Definitions()
{
    static TArray<TStrongObjectPtr<UHeliosCharacterDefinition>> Result;
    if (!Result.IsEmpty()) return Result;
    const auto Add = [](const TCHAR* Id, const TCHAR* Name, const TCHAR* Root,
        const TCHAR* Arms, const TCHAR* Body, const TCHAR* Preview, const TCHAR* AnimationPrefix,
        const TCHAR* SourceId)
    {
        auto* D = NewObject<UHeliosCharacterDefinition>(GetTransientPackage(), NAME_None, RF_Transient);
        D->CharacterId = Id; D->DisplayName = FText::FromString(Name);
        D->Description = FText::FromString(TEXT("人物外观、手臂与语音独立选择；可搭配任意主武器。"));
        D->FirstPersonArmsMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(FString(Root)+Arms));
        D->ThirdPersonMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(FString(Root)+Body));
        const FString PreviewPackage(Preview);
        D->PreviewTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(PreviewPackage+TEXT(".")+FPaths::GetCleanFilename(PreviewPackage)));
        D->VoiceSourceId = SourceId; D->AnimationSourceId = SourceId;
        D->FirstPersonAnimationPrefix = AnimationPrefix;
        const FString ProfilePackage=FString(TEXT("/Game/Helios/LoadoutPresentation/DA_"))+Id;
        D->ActionProfile=TSoftObjectPtr<UHeliosCharacterActionProfile>(FSoftObjectPath(ProfilePackage+TEXT(".")+FPaths::GetCleanFilename(ProfilePackage)));
        FHeliosCharacterFormInfo Form;
        Form.DisplayName=D->DisplayName; Form.PreviewTexture=D->PreviewTexture; Form.ActionProfile=D->ActionProfile;
        Form.FirstPersonArmsMesh = D->FirstPersonArmsMesh; Form.ThirdPersonMesh = D->ThirdPersonMesh;
        Form.VoiceSourceId = D->VoiceSourceId; D->Forms.Add(Form);
        Result.Emplace(D);
        return D;
    };
    Add(TEXT("cfm_sunshangxiang"),TEXT("孙尚香"),TEXT("/Game/Helios/CFMSunShangXiang/"),
        TEXT("FP_Rebuild_20261004/SK_SunShangXiang_FP"),TEXT("SK_SunShangXiang_TP"),
        TEXT("/Game/Helios/CFMSunShangXiang/Textures/T_SSX_TP_Preview"),TEXT("A_SSX_FP_"),TEXT("cfm_sunshangxiang"));
    Add(TEXT("cfm_zhongxiaokui"),TEXT("钟小葵"),TEXT("/Game/Helios/CFMZhongXiaoKui/"),
        TEXT("SK_ZhongXiaoKui_FP"),TEXT("SK_ZhongXiaoKui_TP"),
        TEXT("/Game/Helios/CFMZhongXiaoKui/Textures/T_ZXK_TP_Preview"),TEXT("A_ZXK_FP_"),TEXT("cfm_zhongxiaokui"));
    Add(TEXT("cfm_chan"),TEXT("蝉·潜伏者"),TEXT("/Game/Helios/CFMChanZeus/"),
        TEXT("SK_DiaoChanArms_FP"),TEXT("SK_DiaoChan_TP"),
        TEXT("/Game/Helios/CFMChanZeus/Textures/T_Chan_TP_Preview"),TEXT("A_DIAOCHAN_FP_"),TEXT("cfm_chan_zeus"));
    auto* BLS=Add(TEXT("cfm_bulianshi"),TEXT("步练师"),TEXT("/Game/Helios/CFMBuLianShiP90/"),
        TEXT("SK_BuLianShiArms_FP"),TEXT("SK_BuLianShi_TP"),
        TEXT("/Game/Helios/CFMBuLianShiP90/Textures/T_BuLianShi_TP_Preview"),TEXT("A_BLS_FP_"),TEXT("cfm_bulianshi_p90"));
    auto* Kui=Add(TEXT("cfm_wangzhekui"),TEXT("王者葵"),TEXT("/Game/Helios/CFMWangZheKuiThunder/"),
        TEXT("SK_WangZheKuiArms_FP"),TEXT("SK_WangZheKui_TP"),
        TEXT("/Game/Helios/CFMWangZheKuiThunder/Textures/T_WangZheKui_TP_Preview"),TEXT("A_KUI_FP_"),TEXT("cfm_wangzhekui_thunder"));
    auto* Yun = Add(TEXT("cfm_yun_summer"),TEXT("云悠悠·沐夏微醺"),TEXT("/Game/Helios/CFMYunSummerMegatron/"),
        TEXT("SK_YUN_SP_FP"),TEXT("SK_YUN_SP_TP"),
        TEXT("/Game/Helios/CFMYunSummerMegatron/Textures/T_YUN_SP_Preview"),TEXT("A_YUN_SP_FP_"),TEXT("cfm_yun_summer_megatron"));
    const auto ProfileFor=[](FName Character,FName Variant)
    {
        const FString Package=FString::Printf(TEXT("/Game/Helios/LoadoutPresentation/Forms/%s/%s/DA_%s_%s"),*Character.ToString(),*Variant.ToString(),*Character.ToString(),*Variant.ToString());
        return TSoftObjectPtr<UHeliosCharacterActionProfile>(FSoftObjectPath(Package+TEXT(".")+FPaths::GetCleanFilename(Package)));
    };
    const auto AddCharacterForms=[&](UHeliosCharacterDefinition* D,const TCHAR* MeshStem,const TCHAR* PreviewStem,bool HasSpirit)
    {
        D->DefaultVariantId=HasSpirit?FName(TEXT("SP")):FName(TEXT("GR")); D->Forms.Reset();
        for(const TCHAR* Variant:{TEXT("GR"),TEXT("BL"),TEXT("SP")})
        {
            if(!HasSpirit && FString(Variant)==TEXT("SP"))continue;
            FHeliosCharacterFormInfo Form; Form.VariantId=Variant;
            const FString Root=FString::Printf(TEXT("/Game/Helios/CharacterForms/%s/%s/"),*D->CharacterId.ToString(),Variant);
            const FString FP=FString::Printf(TEXT("SK_%s_%s_FP"),MeshStem,Variant),TP=FString::Printf(TEXT("SK_%s_%s_TP"),MeshStem,Variant);
            Form.FirstPersonArmsMesh=TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(Root+TEXT("Models/")+FP+TEXT(".")+FP));
            Form.ThirdPersonMesh=TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(Root+TEXT("Models/")+TP+TEXT(".")+TP));
            const FString Preview=FString::Printf(TEXT("T_%s_%s_TP_Preview"),PreviewStem,Variant);
            Form.PreviewTexture=TSoftObjectPtr<UTexture2D>(FSoftObjectPath(Root+TEXT("Textures/")+Preview+TEXT(".")+Preview));
            Form.DisplayName=FText::FromString(Form.VariantId==TEXT("GR")?TEXT("保卫者"):Form.VariantId==TEXT("BL")?TEXT("潜伏者"):TEXT("灵魂舞者"));
            Form.ActionProfile=ProfileFor(D->CharacterId,Form.VariantId);
            Form.VoiceSourceId=D->VoiceSourceId; Form.VoicePrefix=Variant;
            D->Forms.Add(Form);
        }
        if(const auto* Form=D->FindForm(NAME_None))
        {D->FirstPersonArmsMesh=Form->FirstPersonArmsMesh;D->ThirdPersonMesh=Form->ThirdPersonMesh;D->PreviewTexture=Form->PreviewTexture;D->ActionProfile=Form->ActionProfile;}
        D->Description=FText::FromString(TEXT("人物形态与主武器独立选择；H 切换人物形态，保留当前主武器。"));
    };
    AddCharacterForms(BLS,TEXT("BuLianShi"),TEXT("BLS"),true);
    AddCharacterForms(Kui,TEXT("WangZheKui"),TEXT("KUI"),false);
    auto* Guan=Add(TEXT("cfm_guanxiaoyu"),TEXT("关小雨"),TEXT("/Game/Helios/CharacterForms/cfm_guanxiaoyu/GR/Models/"),
        TEXT("SK_GuanXiaoYu_GR_FP"),TEXT("SK_GuanXiaoYu_GR_TP"),TEXT("/Game/Helios/CharacterForms/cfm_guanxiaoyu/GR/Textures/T_GXY_GR_TP_Preview"),TEXT("A_GXY_GR_FP_"),TEXT("cfm_guanxiaoyu"));
    AddCharacterForms(Guan,TEXT("GuanXiaoYu"),TEXT("GXY"),true);
    auto* Qing=Add(TEXT("cfm_qingya"),TEXT("晴雅"),TEXT("/Game/Helios/CharacterForms/cfm_qingya/GR/Models/"),
        TEXT("SK_QingYa_GR_FP"),TEXT("SK_QingYa_GR_TP"),TEXT("/Game/Helios/CharacterForms/cfm_qingya/GR/Textures/T_QYA_GR_TP_Preview"),TEXT("A_QYA_GR_FP_"),TEXT("cfm_qingya"));
    AddCharacterForms(Qing,TEXT("QingYa"),TEXT("QYA"),true);
    Qing->DefaultVariantId=TEXT("GR");
    if(const auto* Form=Qing->FindForm(TEXT("GR")))
    {Qing->FirstPersonArmsMesh=Form->FirstPersonArmsMesh;Qing->ThirdPersonMesh=Form->ThirdPersonMesh;Qing->PreviewTexture=Form->PreviewTexture;Qing->ActionProfile=Form->ActionProfile;}
    Qing->Description=FText::FromString(TEXT("手游默认晴雅三形态；H 切换形态，J 比心，K 手枪手势，T 应急踢腿。每局随机获得一项天赋。"));
    for(auto& Form:Qing->Forms)if(Form.VariantId==TEXT("SP"))Form.DisplayName=FText::FromString(TEXT("灵魂武者"));
    Guan->DefaultVariantId=TEXT("GR");
    for(auto& Form:Guan->Forms)
    {
        if(Form.VariantId==TEXT("SP"))Form.DisplayName=FText::FromString(TEXT("灵魂武者"));
        const FString Name=TEXT("DA_GXY_Umbrella_")+Form.VariantId.ToString();
        const FString Package=TEXT("/Game/Helios/CharacterForms/cfm_guanxiaoyu/")+Form.VariantId.ToString()+TEXT("/Umbrella/")+Name;
        Form.UmbrellaProfile=TSoftObjectPtr<UHeliosGuanXiaoYuUmbrellaProfile>(FSoftObjectPath(Package+TEXT(".")+Name));
    }
    if(const auto* Form=Guan->FindForm(NAME_None)){Guan->FirstPersonArmsMesh=Form->FirstPersonArmsMesh;Guan->PreviewTexture=Form->PreviewTexture;Guan->ActionProfile=Form->ActionProfile;}
    Guan->Description=FText::FromString(TEXT("H 切换保卫者、潜伏者、灵魂武者；T 专属伞，右键重击与剑伞盾。"));
    Yun->DefaultVariantId=TEXT("SP");
    Yun->Description = FText::FromString(TEXT("三种人物形态独立搭配主武器；H 切换人物形态，J 敬酒互动。"));
    Yun->Forms.Reset();
    for (const TCHAR* Variant : {TEXT("GR"),TEXT("BL"),TEXT("SP")})
    {
        FHeliosCharacterFormInfo Form; Form.VariantId = Variant;
        Form.DisplayName=FText::FromString(Form.VariantId==TEXT("GR")?TEXT("保卫者"):Form.VariantId==TEXT("BL")?TEXT("潜伏者"):TEXT("灵魂武者"));
        const FString PreviewPackage=FString::Printf(TEXT("/Game/Helios/CFMYunSummerMegatron/Textures/T_YUN_%s_Preview"),Variant);
        Form.PreviewTexture=TSoftObjectPtr<UTexture2D>(FSoftObjectPath(PreviewPackage+TEXT(".")+FPaths::GetCleanFilename(PreviewPackage)));
        Form.ActionProfile=ProfileFor(Yun->CharacterId,Form.VariantId);
        const FString FormRoot=FString::Printf(TEXT("/Game/Helios/LoadoutPresentation/Forms/cfm_yun_summer/%s/"),Variant);
        const FString FP=FString::Printf(TEXT("SK_YUN_%s_FP"),Variant),TP=FString::Printf(TEXT("SK_YUN_%s_TP"),Variant);
        Form.FirstPersonArmsMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(FormRoot+TEXT("Source/")+FP+TEXT(".")+FP));
        Form.ThirdPersonMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(FormRoot+TEXT("Source/NativeTP/")+TP+TEXT(".")+TP));
        Form.FirstPersonTransform = FTransform(FRotator(0,-90,0),FVector(0,0,-6));
        Form.MovementMultiplier = Form.VariantId==TEXT("SP") ? 1.2f : 1.f;
        Form.VoiceSourceId = TEXT("cfm_yun_summer_megatron"); Form.VoicePrefix = Variant;
        Yun->Forms.Add(Form);
    }
    const auto* DefaultYun=Yun->FindForm(NAME_None);
    Yun->ActionProfile=DefaultYun->ActionProfile;Yun->FirstPersonArmsMesh=DefaultYun->FirstPersonArmsMesh;Yun->ThirdPersonMesh=DefaultYun->ThirdPersonMesh;
    for(const auto& Definition:Result)
    {
        for(auto& Form:Definition->Forms)
        {
            // Use the imported, weapon-compatible presentation meshes. V2 is
            // still an offline experiment and must not enter the live registry.
            const FString Variant=Form.VariantId.ToString();
            const FString Mesh=Form.VariantId.IsNone()?FString::Printf(TEXT("SK_TP_%s"),*Definition->CharacterId.ToString()):FString::Printf(TEXT("SK_TP_%s_%s"),*Definition->CharacterId.ToString(),*Variant);
            const FString Package=Form.VariantId.IsNone()?TEXT("/Game/Helios/LoadoutPresentation/MeshesV4/")+Mesh:FString::Printf(TEXT("/Game/Helios/LoadoutPresentation/Forms/%s/%s/Meshes/%s"),*Definition->CharacterId.ToString(),*Variant,*Mesh);
            Form.ThirdPersonMesh=TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(Package+TEXT(".")+Mesh));
        }
        if(const auto* DefaultForm=Definition->FindForm(NAME_None))Definition->ThirdPersonMesh=DefaultForm->ThirdPersonMesh;
    }
    return Result;
}
}

const FHeliosCharacterFormInfo* UHeliosCharacterDefinition::FindForm(FName VariantId) const
{
    if(VariantId.IsNone())VariantId=DefaultVariantId;
    for (const auto& Form : Forms) if (Form.VariantId == VariantId) return &Form;
    return nullptr;
}
TArray<UHeliosCharacterDefinition*> FHeliosCharacterRegistry::GetAvailableCharacters(bool bIncludeDeferred)
{
    TArray<UHeliosCharacterDefinition*> Result;
    for (const auto& D : Definitions()) if (bIncludeDeferred || D->bPresentationReady) Result.Add(D.Get());
    return Result;
}
UHeliosCharacterDefinition* FHeliosCharacterRegistry::FindCharacter(FName Id)
{
    Id = CanonicalCharacterId(Id);
    for (const auto& D : Definitions()) if (D->CharacterId==Id) return D.Get();
    return nullptr;
}
FName FHeliosCharacterRegistry::CanonicalCharacterId(FName Id)
{
    if (Id==TEXT("cfm_chan_zeus")) return TEXT("cfm_chan");
    if (Id==TEXT("cfm_bulianshi_p90")) return TEXT("cfm_bulianshi");
    if (Id==TEXT("cfm_wangzhekui_thunder")) return TEXT("cfm_wangzhekui");
    if (Id==TEXT("cfm_yun_summer_megatron")) return TEXT("cfm_yun_summer");
    return Id;
}
