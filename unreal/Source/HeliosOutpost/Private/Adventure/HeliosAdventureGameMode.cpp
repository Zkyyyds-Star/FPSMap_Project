#include "Adventure/HeliosAdventureGameMode.h"
#include "QA/HeliosKillFeedbackQA.h"

#include "Adventure/HeliosAdventureCharacter.h"
#include "QA/HeliosTownQARunner.h"
#include "Town/HeliosTownSubsystem.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "UI/HeliosAdventureHUD.h"
#include "QA/HeliosWeaponQA.h"
#include "QA/HeliosChopsticksQA.h"
#include "QA/HeliosColaQA.h"
#include "QA/HeliosLightstickDemo.h"
#include "QA/HeliosSoulBannerQA.h"
#include "QA/HeliosQingxiaoQA.h"
#include "QA/HeliosCalamityDemo.h"
#include "QA/HeliosMechDemo.h"
#include "QA/HeliosPetDemo.h"
#include "QA/HeliosRealismQA.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

AHeliosAdventureGameMode::AHeliosAdventureGameMode()
{
    DefaultPawnClass = AHeliosAdventureCharacter::StaticClass();
    HUDClass = AHeliosAdventureHUD::StaticClass();
}

void AHeliosAdventureGameMode::StartPlay()
{
    Super::StartPlay();

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    if (!AHeliosAdventureDirector::Find(World))
    {
        World->SpawnActor<AHeliosAdventureDirector>();
    }

    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosLoadoutFlowQA"))||FParse::Param(FCommandLine::Get(),TEXT("HeliosCompanionFixQA")))
        if(UClass* QAClass=LoadClass<AActor>(nullptr,TEXT("/Script/HeliosOutpost.HeliosLoadoutFlowQA")))World->SpawnActor<AActor>(QAClass);
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosGuanXiaoYuQA")))
        if(UClass* QAClass=LoadClass<AActor>(nullptr,TEXT("/Script/HeliosOutpost.HeliosGuanXiaoYuQA")))World->SpawnActor<AActor>(QAClass);
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosQingYaQA")))
        if(UClass* QAClass=LoadClass<AActor>(nullptr,TEXT("/Script/HeliosOutpost.HeliosQingYaQA")))World->SpawnActor<AActor>(QAClass);
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosLoadoutMenuQA")))
        if(UClass* QAClass=LoadClass<AActor>(nullptr,TEXT("/Script/HeliosOutpost.HeliosLoadoutMenuQA")))World->SpawnActor<AActor>(QAClass);
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosLoadoutQA"))&&!FParse::Param(FCommandLine::Get(),TEXT("HeliosLoadoutFlowQA"))&&!FParse::Param(FCommandLine::Get(),TEXT("HeliosLoadoutMenuQA"))&&!FParse::Param(FCommandLine::Get(),TEXT("HeliosCompanionFixQA")))
        if(UClass* QAClass=LoadClass<AActor>(nullptr,TEXT("/Script/HeliosOutpost.HeliosLoadoutQA")))World->SpawnActor<AActor>(QAClass);

    FString QAMode;
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosAudioSettingsQA")))
        if(UClass* QAClass=LoadClass<AActor>(nullptr,TEXT("/Script/HeliosOutpost.HeliosAudioSettingsQA")))World->SpawnActor<AActor>(QAClass);
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosMultiPetQA")))
        if(UClass* QAClass=LoadClass<AActor>(nullptr,TEXT("/Script/HeliosOutpost.HeliosMultiPetQA")))World->SpawnActor<AActor>(QAClass);
    if (FParse::Param(FCommandLine::Get(), TEXT("HeliosUrbanQA")))
        if (UClass* QAClass=LoadClass<AActor>(nullptr,TEXT("/Script/HeliosOutpost.HeliosUrbanQA"))) World->SpawnActor<AActor>(QAClass);
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosBuLianShiP90QA")))
        if(UClass* QAClass=LoadClass<AActor>(nullptr,TEXT("/Script/HeliosOutpost.HeliosBuLianShiP90QA")))World->SpawnActor<AActor>(QAClass);
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosWangZheKuiThunderQA")))
        if(UClass* QAClass=LoadClass<AActor>(nullptr,TEXT("/Script/HeliosOutpost.HeliosWangZheKuiThunderQA")))World->SpawnActor<AActor>(QAClass);
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosYunSummerMegatronQA")))
        if(UClass* QAClass=LoadClass<AActor>(nullptr,TEXT("/Script/HeliosOutpost.HeliosYunSummerMegatronQA")))World->SpawnActor<AActor>(QAClass);
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosChanZeusQA")))
        if(UClass* QAClass=LoadClass<AActor>(nullptr,TEXT("/Script/HeliosOutpost.HeliosChanZeusQA")))World->SpawnActor<AActor>(QAClass);
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosBlackDragonSoulQA")))
        if(UClass* QAClass=LoadClass<AActor>(nullptr,TEXT("/Script/HeliosOutpost.HeliosBlackDragonSoulQA")))World->SpawnActor<AActor>(QAClass);
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosKillFeedbackQA"))) World->SpawnActor<AHeliosKillFeedbackQA>();
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosSSXDemo")))
        if(auto* C=LoadClass<AActor>(nullptr,TEXT("/Script/HeliosOutpost.HeliosSSXDemo")))World->SpawnActor<AActor>(C);
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosFighterDemo")) || FParse::Param(FCommandLine::Get(),TEXT("VXQuickStart")))
        if(auto* C=LoadClass<AActor>(nullptr,TEXT("/Script/HeliosOutpost.HeliosFighterDemo")))World->SpawnActor<AActor>(C);
    if (FParse::Param(FCommandLine::Get(), TEXT("HeliosMechDemo"))) World->SpawnActor<AHeliosMechDemo>();
    if (FParse::Param(FCommandLine::Get(), TEXT("HeliosCalamityDemo"))) World->SpawnActor<AHeliosCalamityDemo>();
    if (FParse::Param(FCommandLine::Get(), TEXT("HeliosQingxiaoQA"))) World->SpawnActor<AHeliosQingxiaoQA>();
    if (FParse::Param(FCommandLine::Get(), TEXT("HeliosSoulBannerQA"))) World->SpawnActor<AHeliosSoulBannerQA>();
    if (FParse::Param(FCommandLine::Get(), TEXT("HeliosPetDemo"))) World->SpawnActor<AHeliosPetDemo>();
    // Butterfly is retired; its old demonstration flag must not bypass the live catalogue.
    if (FParse::Param(FCommandLine::Get(), TEXT("HeliosLightstickDemo"))) World->SpawnActor<AHeliosLightstickDemo>();
    if (FParse::Param(FCommandLine::Get(), TEXT("HeliosColaQA"))) World->SpawnActor<AHeliosColaQA>();
    if (FParse::Param(FCommandLine::Get(), TEXT("HeliosChopsticksQA"))) World->SpawnActor<AHeliosChopsticksQA>();
    if (FParse::Param(FCommandLine::Get(), TEXT("HeliosAwarenessQA")))
        if (UClass* QAClass = LoadClass<AActor>(nullptr, TEXT("/Script/HeliosOutpost.HeliosAwarenessQA")))
            World->SpawnActor<AActor>(QAClass);
    if (FParse::Param(FCommandLine::Get(), TEXT("HeliosTownQA"))
        && !FParse::Param(FCommandLine::Get(), TEXT("HeliosTownSkipSmoke")) && UHeliosTownSubsystem::IsTownMap(World))
    {
        World->SpawnActor<AHeliosTownQARunner>();
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("HeliosRealismQA")))
    {
        World->SpawnActor<AHeliosRealismQA>();
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("HeliosWeaponQA")))
    {
        World->SpawnActor<AHeliosWeaponQA>();
    }
    if (FParse::Value(FCommandLine::Get(), TEXT("HeliosAdventureQA="), QAMode) && !QAMode.IsEmpty())
    {
        if (UClass* QAClass = LoadClass<AActor>(nullptr,
            TEXT("/Script/HeliosOutpost.HeliosAdventureQA")))
        {
            World->SpawnActor<AActor>(QAClass);
        }
    }
}
