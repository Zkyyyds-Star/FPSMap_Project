#include "Weapons/HeliosZhongXiaoKui.h"
#include "Weapons/HeliosWeaponHolderContext.h"
#include "Weapons/HeliosZXKProjectile.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Weapons/HeliosSSXVoice.h"
#include "Player/HeliosArenaCharacter.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Combat/HeliosCombatTempoSettings.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void AHeliosZhongXiaoKuiWeapon::StartSourceAction(FName Clip,FName Recovery,int32 Kind)
{
    auto* P=Cast<APawn>(GetOwner());
    if(Age>=0||!bActive||!P||HeliosWeaponHolderContext::IsDead(P)||GetWorld()->IsPaused()||!Clips.FindRef(Clip))return;
    Action=Clip;Recover=Recovery;ActionKind=Kind;Age=0;bCommitted=false;++DamageAttackSerial;
    AudioBetween(-KINDA_SMALL_NUMBER,0);Pose();
}
void AHeliosZhongXiaoKuiWeapon::ShowSourceEffect(const FString& Name,FName Bone)
{
    auto* P=Cast<APawn>(GetOwner());if(!P)return;
    FVector Eye;FRotator View;HeliosWeaponHolderContext::View(P,Eye,View);
    FVector At=GetFirstPersonMesh()->GetComponentLocation();FRotator Rotation=View;
    if(!Bone.IsNone())
    {
        const FTransform Socket=GetFirstPersonMesh()->GetSocketTransform(Bone);
        At=Socket.GetLocation();Rotation=(Socket.GetRotation()*FRotator(0,90,0).Quaternion()).Rotator();
    }
    if(auto* E=HeliosZXK::Effect(this,Name,At,Rotation,true))
    {E->AttachToComponent(GetFirstPersonMesh(),FAttachmentTransformRules::KeepWorldTransform,Bone);
        if(Bone.IsNone()&&(Name.StartsWith(TEXT("FX_ShengHua_ZhongXiaoKui_Root_1P_A_"))||Name.StartsWith(TEXT("FX_ShengHua_ZhongXiaoKui_Root_1P_B_")))&&
            !Name.Contains(TEXT("Skill02"))&&!FParse::Param(FCommandLine::Get(),TEXT("HeliosZXKOriginalMeleeTranslationQA")))E->FollowLocalViewTranslation();
        Effects.Add(E);}
}
void AHeliosZhongXiaoKuiWeapon::StartFormAttack(bool Heavy)
{
    bHeavy=Heavy&&Form==1;
    if(Form==2)
    {
        if(DartAmmo<=0){RequestSourceReload();return;}
        const bool Second=(Combo++%2)!=0;
        StartSourceAction(Second?TEXT("C_Fire02"):TEXT("C_Fire01"),Second?TEXT("C_Fire02_Recove"):TEXT("C_Fire01_Recove"),0);
        if(Age>=0)ShowSourceEffect(Second?TEXT("FX_ShengHue_ZhongXiaoKui_b_RightHand_1P_C_R_Fire02"):TEXT("FX_ShengHue_ZhongXiaoKui_b_RightHand_1P_C_L_Fire01"),TEXT("b_RightHand"));return;
    }
    const TCHAR* Attacks[]={TEXT("B_FireSlightFirst"),TEXT("B_FireSlightSecond"),TEXT("B_FireSlightThird"),TEXT("B_FireSlightFourth"),TEXT("B_FireSlightFifth")};
    const TCHAR* Recoveries[]={TEXT("B_First_Recover"),TEXT("B_Second_Recover"),TEXT("B_Third_Recover"),TEXT("B_FireSlightFourth_Recove"),TEXT("B_FireSlightFifth_Recove")};
    const int32 I=Combo++%5;StartSourceAction(Heavy?FName(TEXT("B_Heavry")):FName(Attacks[I]),Heavy?NAME_None:FName(Recoveries[I]),0);
    if(Age>=0)ShowSourceEffect(TEXT("FX_ShengHua_ZhongXiaoKui_Root_1P_B_")+(Heavy?FString(TEXT("FireSlightHeavry")):Action.ToString().RightChop(2)));
}
void AHeliosZhongXiaoKuiWeapon::StartTransition(int32 To)
{
    if(Age>=0||To==Form)return;
    FName Clip=NAME_None;
    if(Form==0&&To==1)Clip=TEXT("Change_AToB");
    if(Form==1&&To==0)Clip=TEXT("Change_BToA");
    if(Form==1&&To==2)Clip=TEXT("C_Change_BToC");
    if(Form==2&&To==1)Clip=TEXT("C_Change_CToB");
    if(Clip.IsNone())return;
    PendingForm=To;bHeld=false;bWaveHeld=false;StartSourceAction(Clip,NAME_None,2);
    if(Age>=0&&To==2)ShowSourceEffect(TEXT("FX_ShengHue_ZhongXiaoKui_Jian_1P_Change_BToC"),TEXT("Jian"));
    if(Age>=0&&Form==2)ShowSourceEffect(TEXT("FX_ShengHue_ZhongXiaoKui_Root_1P_Change_CtoB"));
}
void AHeliosZhongXiaoKuiWeapon::ToggleForm(){if(HeliosWeaponHolderContext::IsCompanion(GetOwner()))return;if(Age<0){bQueuedRanged=false;StartTransition(Form==0?1:Form==2?1:0);}}
void AHeliosZhongXiaoKuiWeapon::ToggleRangedForm(){if(HeliosWeaponHolderContext::IsCompanion(GetOwner()))return;if(Age<0){bQueuedRanged=Form==0;StartTransition(Form==0?1:Form==1?2:1);}}
void AHeliosZhongXiaoKuiWeapon::RequestSourceReload(){if(Form==2&&Age<0&&DartAmmo<20){bHeld=false;StartSourceAction(TEXT("C_ChangeClip"),NAME_None,3);}}
void AHeliosZhongXiaoKuiWeapon::StartSpecialFire()
{
    if(HeliosWeaponHolderContext::IsCompanion(GetOwner())||Form==2||Age>=0||GetWorld()->GetTimeSeconds()<SkillCooldownEnd)return;
    bHeld=false;bHeavy=false;StartSourceAction(Form==0?TEXT("Skill01"):TEXT("B_Skill01"),NAME_None,1);
    if(Age>=0){SkillCooldownEnd=GetWorld()->GetTimeSeconds()+UHeliosCombatTempoSettings::Hunter(this,true,HeliosWeaponHolderContext::IsCompanion(GetOwner()),TEXT("技能冷却"),10,.05,600);ShowSourceEffect(Form==0?TEXT("FX_ShengHua_ZhongXiaoKui_Root_1P_A_skill01"):TEXT("FX_ShengHua_ZhongXiaoKui_Root_1P_B_skill01"));}
}
void AHeliosZhongXiaoKuiWeapon::StartSwordWave()
{
    if(Form!=1||Age>=0||GetWorld()->GetTimeSeconds()+KINDA_SMALL_NUMBER<WaveCooldownEnd)return;
    bHeld=false;bHeavy=false;StartSourceAction(TEXT("B_Skill02"),NAME_None,4);
    // The root prefab duplicates the traveling wave geometry at the camera.
    // Emit the original JianQi prefab once on the actual projectile event instead.
    if(Age>=0)WaveCooldownEnd=GetWorld()->GetTimeSeconds()+UHeliosCombatTempoSettings::Hunter(this,true,HeliosWeaponHolderContext::IsCompanion(GetOwner()),TEXT("剑气冷却"),.5f,.05,600);
}
void AHeliosZhongXiaoKuiWeapon::ThrowProjectile(bool SwordWave)
{
    auto* P=Cast<APawn>(GetOwner());if(!P||(!SwordWave&&DartAmmo<=0))return;
    FVector Eye;FRotator View;HeliosWeaponHolderContext::View(P,Eye,View);
    FActorSpawnParameters Params;Params.Owner=this;Params.Instigator=P;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    if(auto* Projectile=GetWorld()->SpawnActor<AHeliosZXKProjectile>(Eye+View.Vector()*45,View,Params))
    {Projectile->Initialize(this,View.Vector(),UHeliosCombatTempoSettings::Hunter(this,true,HeliosWeaponHolderContext::IsCompanion(GetOwner()),SwordWave?TEXT("剑气伤害"):TEXT("飞镖伤害"),ActiveDefinition->Damage*(SwordWave?2.f:1.f),0,100000),SwordWave,Eye);Projectiles.Add(Projectile);if(SwordWave)++WaveShots;else --DartAmmo;}
}
void AHeliosZhongXiaoKuiWeapon::ProjectileContact(bool Killed){++HitCount;if(Killed&&!HeliosWeaponHolderContext::PlayCharacterCue(GetOwner(),TEXT("Kill")))Voice->Play(TEXT("Kill"));}
FString AHeliosZhongXiaoKuiWeapon::Status() const
{
    if(Form==2)return FString::Printf(TEXT("飞镖 %d/20 · 左键投掷 / R 装填 / V 返回"),DartAmmo);
    return FString::Printf(TEXT("%s · 左键轻击 · Q 变身 / V 飞镖 / G 技能%s"),Form==0?TEXT("唐刀"):TEXT("强化唐刀"),Form==1?TEXT(" / 右键剑气"):TEXT(" / 右键重击"));
}
