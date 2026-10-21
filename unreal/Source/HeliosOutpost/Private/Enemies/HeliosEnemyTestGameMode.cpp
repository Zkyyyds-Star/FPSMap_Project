#include "Enemies/HeliosEnemyTestGameMode.h"
#include "Player/HeliosArenaCharacter.h"
#include "UI/HeliosEnemyHUD.h"
#include "Enemies/HeliosEnemyTestDirector.h"
#include "QA/HeliosEnemyQARunner.h"
#include "QA/HeliosEnemyExtendedQA.h"
#include "QA/HeliosQingxiaoQA.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

AHeliosEnemyTestGameMode::AHeliosEnemyTestGameMode()
{
    DefaultPawnClass=AHeliosArenaCharacter::StaticClass();
    HUDClass=AHeliosEnemyHUD::StaticClass();
}

void AHeliosEnemyTestGameMode::StartPlay()
{
    Super::StartPlay();
    bool bHasDirector=false;
    for (TActorIterator<AHeliosEnemyTestDirector> It(GetWorld());It;++It) {bHasDirector=true;break;}
    if (!bHasDirector) GetWorld()->SpawnActor<AHeliosEnemyTestDirector>();
    FString Mode;
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosQingYaQA")))
        if(UClass* QAClass=LoadClass<AActor>(nullptr,TEXT("/Script/HeliosOutpost.HeliosQingYaQA")))GetWorld()->SpawnActor<AActor>(QAClass);
    if (FParse::Param(FCommandLine::Get(), TEXT("HeliosMinionCombatQA")))
        if (UClass* QAClass = LoadClass<AActor>(nullptr, TEXT("/Script/HeliosOutpost.HeliosMinionCombatQA")))
            GetWorld()->SpawnActor<AActor>(QAClass);
    if (FParse::Param(FCommandLine::Get(), TEXT("HeliosQingxiaoQA"))) GetWorld()->SpawnActor<AHeliosQingxiaoQA>();
    if (FParse::Param(FCommandLine::Get(), TEXT("HeliosAwarenessQA")))
        if (UClass* QAClass = LoadClass<AActor>(nullptr, TEXT("/Script/HeliosOutpost.HeliosAwarenessQA")))
            GetWorld()->SpawnActor<AActor>(QAClass);
    if (FParse::Value(FCommandLine::Get(),TEXT("HeliosEnemyQA="),Mode))
        GetWorld()->SpawnActor<AHeliosEnemyQARunner>();
    if (FParse::Value(FCommandLine::Get(),TEXT("HeliosEnemyExtendedQA="),Mode))
        GetWorld()->SpawnActor<AHeliosEnemyExtendedQA>();
}
