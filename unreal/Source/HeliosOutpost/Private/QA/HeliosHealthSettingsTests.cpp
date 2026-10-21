#if WITH_DEV_AUTOMATION_TESTS
#include "QA/HeliosLiveSettingsWorld.h"
static UWorld* LiveSettingsWorld = nullptr;
#include "QA/HeliosSettingsExpected.h"
#include "Health/CreatureHealthSettings.h"
#include "Player/HeliosArenaCharacter.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Boss/HeliosEnemyBoss.h"
#include "Pets/HeliosUltimatumPet.h"
#include "Weapons/HeliosSoulBanner.h"
#include "ShooterCharacter.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Components/BoxComponent.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosHealthFileTest,"Helios.Health.FileValidation",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FHeliosHealthFileTest::RunTest(const FString& Parameters)
{
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("HealthTests");
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Path = FPaths::CreateTempFilename(*Directory, TEXT("Health"), TEXT(".ini"));
    FCreatureHealthTable Table;
    for (int32 I=0; I<static_cast<int32>(ECreatureHealthType::Count); ++I)
    {
        const auto Type=static_cast<ECreatureHealthType>(I);
        TestEqual(FCreatureHealthTable::Name(Type),Table.Get(Type),FCreatureHealthTable::Default(Type));
    }
    const FString Text=TEXT("[生物血量]\n玩家=245.5\n步枪兵=0\n冲锋兵=-2\n游击兵=abc\n篮球兵=NaN\nBoss=inf\n刀盾狗=3abc\n蝴蝶·雷神助战=1e100\n万魂幡骷髅战士=0.5\n射击模板玩家=700\n");
    if (!TestTrue(TEXT("Write UTF-8 fixture"),FFileHelper::SaveStringToFile(Text,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))) return false;
    Table.LoadFile(Path,false);
    TestEqual(TEXT("Chinese key and decimal"),Table.Get(ECreatureHealthType::Player),245.5f);
    TestEqual(TEXT("Fractional health allowed"),Table.Get(ECreatureHealthType::SoulWarrior),.5f);
    TestEqual(TEXT("Template configured"),Table.Get(ECreatureHealthType::ShooterPlayer),700.f);
    for(const auto Type:{ECreatureHealthType::Rifle,ECreatureHealthType::Charger,ECreatureHealthType::Skirmisher,
        ECreatureHealthType::Basketball,ECreatureHealthType::Boss,ECreatureHealthType::SwordDog,
        ECreatureHealthType::Ultimatum,ECreatureHealthType::SoulMage,ECreatureHealthType::ShooterNPC})
        TestEqual(FCreatureHealthTable::Name(Type),Table.Get(Type),FCreatureHealthTable::Default(Type));
    FFileHelper::SaveStringToFile(TEXT("[生物血量]\n玩家=321\n"),*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    TestEqual(TEXT("Existing snapshot unchanged by file edit"),Table.Get(ECreatureHealthType::Player),245.5f);
    FCreatureHealthTable NextSession; NextSession.LoadFile(Path,false);
    TestEqual(TEXT("Fresh load observes edit without compilation"),NextSession.Get(ECreatureHealthType::Player),321.f);
    IFileManager::Get().Delete(*Path);
    NextSession.LoadFile(Path,false);
    TestEqual(TEXT("Missing file resets to defaults"),NextSession.Get(ECreatureHealthType::Player),100.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosHealthActorsTest,"Helios.Health.ActorLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHeliosHealthActorsTest::RunTest(const FString& Parameters)
{
    FHeliosSettingsExpected Expected;
    if (!Expected.Load(*this)) return false;
    FTestWorldWrapper Fixture;
    UWorld* World=LiveSettingsWorld;
    if (!World)
    {
        if(!TestTrue(TEXT("Create world"),Fixture.CreateTestWorld(EWorldType::Game)))return false;
        World=Fixture.GetTestWorld(); World->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();
        if(!TestTrue(TEXT("Begin world"),Fixture.BeginPlayInTestWorld()))return false;
    }
    FHeliosLiveSettingsCleanup Cleanup(LiveSettingsWorld);
    TestNotNull(TEXT("Per-map health service"),World->GetSubsystem<UCreatureHealthSettings>());

    FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto CheckHP=[&](const TCHAR* Name,float Actual,ECreatureHealthType Type)
    {
        AddInfo(FString::Printf(TEXT("HEALTH_CHECK %s actual=%.3f configured=%.3f"),Name,Actual,Expected.Get(Type)));
        TestEqual(Name,Actual,Expected.Get(Type));
    };
    auto* Player=World->SpawnActorDeferred<AHeliosArenaCharacter>(AHeliosArenaCharacter::StaticClass(),FTransform(FVector(0,0,150)),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!TestNotNull(TEXT("Spawn player"),Player)) return false;
    FindFProperty<FFloatProperty>(Player->GetClass(),TEXT("MaxHealth"))->SetPropertyValue_InContainer(Player,9999.f);
    Player->FinishSpawning(FTransform(FVector(0,0,150)));
    // BeginPlay reloads the configured protection duration; disable it afterwards for this health-only fixture.
    FindFProperty<FFloatProperty>(Player->GetClass(),TEXT("SpawnProtectionSeconds"))->SetPropertyValue_InContainer(Player,0.f);
    Player->ResetForTest();
    CheckHP(TEXT("Player overrides authored max"),Player->GetHealth(),ECreatureHealthType::Player);
    TestEqual(TEXT("Player max getter"),Player->GetMaxHealth(),Player->GetHealth());
    FDamageEvent Damage;
    Player->TakeDamage(Player->GetMaxHealth()*.5f,Damage,nullptr,nullptr);
    TestEqual(TEXT("Player damage"),Player->GetHealth(),Player->GetMaxHealth()*.5f);
    Player->Heal(Player->GetMaxHealth()*2);
    TestEqual(TEXT("Healing capped at configured max"),Player->GetHealth(),Player->GetMaxHealth());
    Player->TakeDamage(Player->GetMaxHealth()*2,Damage,nullptr,nullptr);
    TestTrue(TEXT("Player defeated"),Player->IsDead());
    Player->ResetForTest();
    TestFalse(TEXT("Player revived"),Player->IsDead());
    TestEqual(TEXT("Respawn restores configured max"),Player->GetHealth(),Player->GetMaxHealth());

    const TPair<UClass*,ECreatureHealthType> Enemies[]={
        {AHeliosEnemyE01::StaticClass(),ECreatureHealthType::Rifle},
        {AHeliosEnemyE02::StaticClass(),ECreatureHealthType::Charger},
        {AHeliosEnemyE03::StaticClass(),ECreatureHealthType::Skirmisher},
        {AHeliosEnemyE04::StaticClass(),ECreatureHealthType::Basketball},
        {AHeliosEnemyBoss::StaticClass(),ECreatureHealthType::Boss}};
    int32 Index=0;
    for(const auto& Entry:Enemies)
    {
        auto* Enemy=World->SpawnActor<AHeliosEnemyActor>(Entry.Key,FVector(5000+Index++*500,0,150),FRotator::ZeroRotator,Spawn);
        if(!TestNotNull(TEXT("Spawn enemy"),Enemy)) return false;
        CheckHP(FCreatureHealthTable::Name(Entry.Value),Enemy->GetHealth(),Entry.Value);
        if(auto* Boss=Cast<AHeliosEnemyBoss>(Enemy))
        {
            TestEqual(TEXT("Boss configured max"),Boss->GetBossMaxHealth(),Expected.Get(ECreatureHealthType::Boss));
            Boss->CurrentHP=Boss->GetBossMaxHealth()*.51f;
            TestEqual(TEXT("Boss above half"),Boss->GetBossPhase(),1);
            Boss->CurrentHP=Boss->GetBossMaxHealth()*.5f;
            TestEqual(TEXT("Boss at half"),Boss->GetBossPhase(),2);
        }
    }
    auto* Floor=World->SpawnActor<AActor>();
    auto* Box=NewObject<UBoxComponent>(Floor); Floor->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(10000,10000,10));Box->SetCollisionProfileName(TEXT("BlockAll"));Box->RegisterComponent();
    Floor->SetActorLocation(FVector(0,0,-10));
    for(const bool bRifle:{false,true})
    {
        const auto Type=bRifle?ECreatureHealthType::Ultimatum:ECreatureHealthType::SwordDog;
        const FVector Position(bRifle?1500.f:-1500.f,0,120);
        auto* Pet=World->SpawnActor<AHeliosPetCompanion>(bRifle?AHeliosUltimatumPet::StaticClass():AHeliosPetDog::StaticClass(),Position,FRotator::ZeroRotator,Spawn);
        if(!TestNotNull(TEXT("Spawn pet"),Pet)) return false;
        Pet->CompanionOwner=Player;
        CheckHP(FCreatureHealthTable::Name(Type),Pet->Health,Type);
        Pet->Restore(Pet->Settings.MaxHealth*.4f,0);
        TestEqual(TEXT("Travel snapshot preserves injured health"),Pet->Health,Pet->Settings.MaxHealth*.4f);
        Pet->Restore(Pet->Settings.MaxHealth*2,0);
        TestEqual(TEXT("Travel snapshot clamps to max"),Pet->Health,Pet->Settings.MaxHealth);
        Pet->Restore(0,.1f);Pet->Tick(1.f);
        TestEqual(TEXT("Wake restores configured full health"),Pet->Health,Pet->Settings.MaxHealth);
    }
    auto* SummonFlag=World->SpawnActor<AHeliosSoulFlag>();
    if(!TestNotNull(TEXT("Summon flag fixture"),SummonFlag)) return false;
    for(int32 Role=0;Role<3;++Role)
    {
        auto* Summon=World->SpawnActor<AHeliosSoulSkeleton>(FVector(0,0,200),FRotator::ZeroRotator,Spawn);
        if(!TestNotNull(TEXT("Spawn summon"),Summon)) return false;
        Summon->Initialize(SummonFlag,Role);
        CheckHP(TEXT("Summon role"),Summon->Health,Role==2?ECreatureHealthType::SoulMage:ECreatureHealthType::SoulWarrior);
    }
    const auto* NPCClass=LoadClass<AShooterNPC>(nullptr,TEXT("/Game/Variant_Shooter/Blueprints/AI/BP_ShooterNPC.BP_ShooterNPC_C"));
    if(TestNotNull(TEXT("Template NPC class"),NPCClass))
    {
        auto* NPC=World->SpawnActor<AShooterNPC>(const_cast<UClass*>(NPCClass),FVector(0,5000,150),FRotator::ZeroRotator,Spawn);
        if(TestNotNull(TEXT("Template NPC"),NPC))CheckHP(TEXT("Template NPC"),NPC->CurrentHP,ECreatureHealthType::ShooterNPC);
    }
    auto* ShooterClass=LoadClass<AShooterCharacter>(nullptr,TEXT("/Game/Variant_Shooter/Blueprints/BP_ShooterCharacter.BP_ShooterCharacter_C"));
    if(TestNotNull(TEXT("Template player class"),ShooterClass))
    {
        auto* Shooter=World->SpawnActor<AShooterCharacter>(ShooterClass,FVector(0,-5000,150),FRotator::ZeroRotator,Spawn);
        if(TestNotNull(TEXT("Template player"),Shooter))
            CheckHP(TEXT("Template player maximum"),FindFProperty<FFloatProperty>(ShooterClass,TEXT("MaxHP"))->GetPropertyValue_InContainer(Shooter),ECreatureHealthType::ShooterPlayer);
    }
    return true;
}
bool HeliosCheckLiveHealth(UWorld* World)
{
    LiveSettingsWorld=World;
    struct FLive : FHeliosHealthActorsTest {
        FLive():FHeliosHealthActorsTest(TEXT("Helios.Live.Health")){}
        bool Execute(){return RunTest(TEXT("LiveWorld"));}
    } Test;
    const bool Ran=Test.Execute();
    LiveSettingsWorld=nullptr;
    FAutomationTestExecutionInfo Info; Test.GetExecutionInfo(Info);
    for(const auto& Entry:Info.GetEntries()) UE_LOG(LogTemp,Display,TEXT("LIVE_HEALTH %s"),*Entry.Event.Message);
    return Ran&&!Test.HasAnyErrors();
}
#endif
