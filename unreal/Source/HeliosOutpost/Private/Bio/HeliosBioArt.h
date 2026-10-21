#pragma once
#include "Bio/HeliosBioRoleDefinition.h"
#include "Bio/HeliosBioRules.h"
#include "Misc/PackageName.h"

namespace HeliosBioArt
{
inline UHeliosBioRoleDefinition* Named(FName Role)
{
    if(Role.IsNone())return nullptr;
    const FString Package=FString::Printf(TEXT("/Game/Helios/CFMBioPyramid/Characters/DA_%s"),*Role.ToString());
    if(!FPackageName::DoesPackageExist(Package))return nullptr;
    return LoadObject<UHeliosBioRoleDefinition>(nullptr,*(Package+TEXT(".DA_")+Role.ToString()));
}
inline UHeliosBioRoleDefinition* Load(const FHeliosBioParticipant& P)
{
    if(P.Team==EHeliosBioTeam::Zombie&&!P.ZombieRoleId.IsNone())return Named(P.ZombieRoleId);
    if(!P.HunterRoleId.IsNone()&&P.Team==EHeliosBioTeam::UltimateHunter)return Named(P.HunterRoleId);
    const TCHAR* Name=P.Team==EHeliosBioTeam::Zombie?(P.ZombieForm==EHeliosBioZombieForm::Terminator?TEXT("BioTerminator"):TEXT("BioZombie")):
        P.Team==EHeliosBioTeam::UltimateHunter?TEXT("BioUltimateHunter"):TEXT("BioGhostHunter");
    const auto Get=[](const TCHAR* N){const FString Path=FString::Printf(TEXT("/Game/Helios/CFMBioHotel/Characters/DA_%s.DA_%s"),N,N);return LoadObject<UHeliosBioRoleDefinition>(nullptr,*Path);};
    // Current installed BIO_3RD help names Wukong and WhiteBone; preserve literal source identities.
    const TCHAR* ModernName=P.Team==EHeliosBioTeam::UltimateHunter?TEXT("BioSunWukong"):P.Team==EHeliosBioTeam::Zombie&&P.bMother?TEXT("BioBaiGuJing"):nullptr;
    if(P.bUseModernAvatar&&ModernName)
    {
        const FString ModernPath=FString::Printf(TEXT("/Game/Helios/CFMBioPyramid/Characters/DA_%s.DA_%s"),ModernName,ModernName);
        if(auto* Modern=LoadObject<UHeliosBioRoleDefinition>(nullptr,*ModernPath))return Modern;
    }
    auto* Result=Get(Name);
    if(!Result)Result=Get(P.Team==EHeliosBioTeam::Zombie?TEXT("BioTerminator"):TEXT("BioHumanHero"));
    return Result;
}
inline FString TeamName(EHeliosBioTeam Team)
{
    switch(Team){case EHeliosBioTeam::Human:return TEXT("人类");case EHeliosBioTeam::Zombie:return TEXT("生化幽灵");case EHeliosBioTeam::GhostHunter:return TEXT("幽灵猎手");default:return TEXT("终极猎手");}
}
}
