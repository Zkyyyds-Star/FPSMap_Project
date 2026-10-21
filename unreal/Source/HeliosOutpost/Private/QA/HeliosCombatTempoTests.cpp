#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/HeliosCombatTempoSettings.h"
#include "Boss/HeliosEnemyBoss.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Pets/HeliosPetDog.h"
#include "Pets/HeliosUltimatumPet.h"
#include "Player/HeliosArenaCharacter.h"
#include "Town/HeliosTownSubsystem.h"
#include "Weapons/HeliosLightstickWeapon.h"
#include "Weapons/HeliosSoulBanner.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Weapons/HeliosWeaponTempoSettings.h"
#include "ShooterWeapon.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosTempoFileValidationTest,
    "Helios.Tempo.FileValidation",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FHeliosTempoFileValidationTest::RunTest(const FString&)
{
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("TempoTests") / FGuid::NewGuid().ToString();
    IFileManager::Get().MakeDirectory(*Dir, true);
    auto Write = [this, &Dir](const TCHAR* File, const TCHAR* Body)
    {
        return TestTrue(File, FFileHelper::SaveStringToFile(Body, *(Dir / File),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    };
    if (!Write(TEXT("攻击速度.ini"), TEXT("[玩家武器]\n突击步枪开火间隔=0.25\n霰弹枪开火间隔=-1\n激光枪脉冲间隔=NaN\n")) ||
        !Write(TEXT("移动速度.ini"), TEXT("[玩家]\n步行=712.5\n冲刺=0\n")) ||
        !Write(TEXT("复活与冷却.ini"), TEXT("[玩家复活秒]\n复活保护=0\n小镇死亡复活=3abc\n")))
        return false;

    FHeliosCombatTempoTable First;
    First.LoadDirectory(Dir, false);
    using F = EHeliosTempoFile;
    TestEqual(TEXT("Chinese UTF-8 attack value"), First.Get(F::Attack, TEXT("玩家武器"), TEXT("突击步枪开火间隔"), .1f), .25f);
    TestEqual(TEXT("Negative attack interval falls back"), First.Get(F::Attack, TEXT("玩家武器"), TEXT("霰弹枪开火间隔"), .85f), .85f);
    TestEqual(TEXT("NaN attack interval falls back"), First.Get(F::Attack, TEXT("玩家武器"), TEXT("激光枪脉冲间隔"), .1f), .1f);
    TestEqual(TEXT("Decimal movement speed"), First.Get(F::Movement, TEXT("玩家"), TEXT("步行"), 450.f), 712.5f);
    TestEqual(TEXT("Zero movement speed falls back"), First.Get(F::Movement, TEXT("玩家"), TEXT("冲刺"), 650.f), 650.f);
    TestEqual(TEXT("Zero spawn protection is valid"), First.Get(F::Recovery, TEXT("玩家复活秒"), TEXT("复活保护"), 3.f, 0.f), 0.f);
    TestEqual(TEXT("Trailing characters fall back"), First.Get(F::Recovery, TEXT("玩家复活秒"), TEXT("小镇死亡复活"), 1.5f), 1.5f);

    Write(TEXT("攻击速度.ini"), TEXT("[玩家武器]\n突击步枪开火间隔=0.4\n"));
    TestEqual(TEXT("Current map keeps loaded snapshot"), First.Get(F::Attack, TEXT("玩家武器"), TEXT("突击步枪开火间隔"), .1f), .25f);
    FHeliosCombatTempoTable Next;
    Next.LoadDirectory(Dir, false);
    TestEqual(TEXT("Next map observes new file"), Next.Get(F::Attack, TEXT("玩家武器"), TEXT("突击步枪开火间隔"), .1f), .4f);
    IFileManager::Get().Delete(*(Dir / TEXT("移动速度.ini")));
    Next.LoadDirectory(Dir, false);
    TestEqual(TEXT("Missing file uses original value"), Next.Get(F::Movement, TEXT("玩家"), TEXT("步行"), 450.f), 450.f);
    IFileManager::Get().DeleteDirectory(*Dir, false, true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosTempoActorBindingTest,
    "Helios.Tempo.ActorBinding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHeliosTempoActorBindingTest::RunTest(const FString&)
{
    const bool bEdited = FParse::Param(FCommandLine::Get(), TEXT("HeliosTempoQA"));
    FTestWorldWrapper Fixture;
    if (!TestTrue(TEXT("Create gameplay world"), Fixture.CreateTestWorld(EWorldType::Game))) return false;
    UWorld* World = Fixture.GetTestWorld();
    World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
    if (!TestTrue(TEXT("Begin gameplay world"), Fixture.BeginPlayInTestWorld())) return false;
    TestNotNull(TEXT("Per-map tempo snapshot"), World->GetSubsystem<UHeliosCombatTempoSettings>());
    const auto Resolve = [World](EHeliosTempoFile File, const TCHAR* Section,
        const TCHAR* Key, float Fallback, float Minimum = .01f)
    {
        return UHeliosCombatTempoSettings::Resolve(World, File, Section, Key, Fallback, Minimum);
    };
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Player = World->SpawnActor<AHeliosArenaCharacter>(FVector(0, 0, 200), FRotator::ZeroRotator, Spawn);
    if (!TestNotNull(TEXT("Spawn player"), Player)) return false;
    const float Walk = Resolve(EHeliosTempoFile::Movement, TEXT("玩家"), TEXT("步行"), 450.f);
    const float Sprint = Resolve(EHeliosTempoFile::Movement, TEXT("玩家"), TEXT("冲刺"), 650.f);
    TestEqual(TEXT("Player walk speed is applied"), Player->GetCharacterMovement()->MaxWalkSpeed, Walk);
    Player->SetSprintingFromAutomation(true);
    TestEqual(TEXT("Player sprint speed is applied"), Player->GetCharacterMovement()->MaxWalkSpeed, Sprint);
    Player->SetSprintingFromAutomation(false);
    TestNotNull(TEXT("Rifle spawned"), Player->GetEquippedWeapon());
    if (Player->GetEquippedWeapon())
        TestEqual(TEXT("Rifle actual refire interval"), Player->GetEquippedWeapon()->GetRefireRate(),
            Resolve(EHeliosTempoFile::Attack, TEXT("玩家武器"), TEXT("突击步枪开火间隔"), .1f));

    auto* RifleEnemy = World->SpawnActor<AHeliosEnemyE01>(FVector(2000, 0, 200), FRotator::ZeroRotator, Spawn);
    if (!TestNotNull(TEXT("Spawn rifle enemy"), RifleEnemy)) return false;
    TestEqual(TEXT("Enemy attack interval"), RifleEnemy->AttackInterval,
        Resolve(EHeliosTempoFile::Attack, TEXT("步枪兵"), TEXT("攻击间隔"), 1.f));
    TestEqual(TEXT("Enemy chase speed"), RifleEnemy->GetCharacterMovement()->MaxWalkSpeed,
        Resolve(EHeliosTempoFile::Movement, TEXT("步枪兵"), TEXT("追击"), 460.f));
    auto* Boss = World->SpawnActor<AHeliosEnemyBoss>(FVector(5000, 0, 200), FRotator::ZeroRotator, Spawn);
    if (!TestNotNull(TEXT("Spawn boss"), Boss)) return false;
    TestEqual(TEXT("Boss charge cooldown"), Boss->GetShoulderAttackCooldownSeconds(),
        Resolve(EHeliosTempoFile::Attack, TEXT("Boss"), TEXT("冲锋冷却"), 4.5f));
    auto* Dog = World->SpawnActor<AHeliosPetDog>(FVector(-1200, 0, 200), FRotator::ZeroRotator, Spawn);
    if (!TestNotNull(TEXT("Spawn sword dog"), Dog)) return false;
    TestEqual(TEXT("Pet attack interval"), Dog->Settings.AttackInterval,
        Resolve(EHeliosTempoFile::Attack, TEXT("宠物与召唤物"), TEXT("刀盾狗攻击间隔"), 1.2f));
    TestEqual(TEXT("Pet movement multiplier reaches actual speed"), Dog->GetCharacterMovement()->MaxWalkSpeed,
        520.f * Resolve(EHeliosTempoFile::Movement, TEXT("刀盾狗"), TEXT("全状态移动倍率"), 2.f));
    auto* Butterfly = World->SpawnActor<AHeliosUltimatumPet>(FVector(-2200, 0, 200), FRotator::ZeroRotator, Spawn);
    if (!TestNotNull(TEXT("Spawn butterfly"), Butterfly)) return false;
    TestEqual(TEXT("Butterfly combat movement"), Butterfly->CombatStepSpeed,
        Resolve(EHeliosTempoFile::Movement, TEXT("蝴蝶雷神"), TEXT("战斗侧移"), 240.f));
    TestEqual(TEXT("Butterfly shot interval"), Butterfly->RifleSettings.ShotInterval,
        Resolve(EHeliosTempoFile::Attack, TEXT("宠物与召唤物"), TEXT("蝴蝶雷神每发间隔"), .09f));
    auto* Town = World->GetSubsystem<UHeliosTownSubsystem>();
    if (TestNotNull(TEXT("Town subsystem"), Town))
        TestEqual(TEXT("Town respawn delay used by actual path"), Town->GetConfiguredRespawnDelay(),
            Resolve(EHeliosTempoFile::Recovery, TEXT("玩家复活秒"), TEXT("小镇死亡复活"), 1.5f));

    if (bEdited)
    {
        TestEqual(TEXT("Edited walk value"), Walk, 612.f);
        TestEqual(TEXT("Edited rifle interval"), Player->GetEquippedWeapon()->GetRefireRate(), .24f);
        TestEqual(TEXT("Edited enemy chase"), RifleEnemy->GetCharacterMovement()->MaxWalkSpeed, 720.f);
        TestEqual(TEXT("Edited boss cooldown"), Boss->GetShoulderAttackCooldownSeconds(), 3.25f);
        TestEqual(TEXT("Edited dog speed"), Dog->GetCharacterMovement()->MaxWalkSpeed, 780.f);
        if (Town) TestEqual(TEXT("Edited town respawn"), Town->GetConfiguredRespawnDelay(), .4f);

        UHeliosWeaponDefinition* RocketDefinition = UHeliosWeaponRegistry::FindWeapon(TEXT("rocket"));
        if (!TestNotNull(TEXT("Rocket definition"), RocketDefinition) ||
            !TestTrue(TEXT("Equip rocket"), Player->EquipWeaponDefinition(RocketDefinition))) return false;
        TestEqual(TEXT("Shared definition remains unchanged"), RocketDefinition->ReloadSeconds, 2.f);
        const FFloatProperty* ReloadProperty = FindFProperty<FFloatProperty>(Player->GetClass(), TEXT("ReloadSeconds"));
        if (TestNotNull(TEXT("Reload property"), ReloadProperty))
            TestEqual(TEXT("Edited reload reaches player"), ReloadProperty->GetPropertyValue_InContainer(Player), 3.2f);
        Player->UpdateWeaponHUD(0, Player->GetMagazineSize());
        Player->StartReloadFromAutomation();
        TestTrue(TEXT("Reload begins"), Player->IsReloading());
        Fixture.TickTestWorld(.01f);
        for (int32 Frame = 0; Frame < 30; ++Frame) Fixture.TickTestWorld(.1f);
        TestTrue(TEXT("Reload still active before configured delay"), Player->IsReloading());
        for (int32 Frame = 0; Frame < 3; ++Frame) Fixture.TickTestWorld(.1f);
        TestFalse(TEXT("Reload finishes after configured delay"), Player->IsReloading());

        Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("lightstick")));
        if (auto* Lightstick = Cast<AHeliosLightstickWeapon>(Player->GetEquippedWeapon()))
        {
            Lightstick->StartFiring();
            TestEqual(TEXT("First melee attack begins"), Lightstick->GetAttackSerial(), 1);
            Lightstick->Tick(.3f);
            TestEqual(TEXT("Edited melee interval has not elapsed"), Lightstick->GetAttackSerial(), 1);
            Lightstick->Tick(.15f);
            TestEqual(TEXT("Next melee attack starts after interval"), Lightstick->GetAttackSerial(), 2);
            Lightstick->StopFiring();
        }
        else AddError(TEXT("Lightstick did not equip"));

        FDamageEvent FatalDamage;
        Player->TakeDamage(Player->GetMaxHealth() + 10.f, FatalDamage, nullptr, nullptr);
        TestTrue(TEXT("Arena player died"), Player->IsDead());
        Fixture.TickTestWorld(.01f);
        Fixture.TickTestWorld(.2f);
        TestTrue(TEXT("Arena reset waits for configured delay"), Player->IsDead());
        Fixture.TickTestWorld(.2f);
        TestFalse(TEXT("Arena reset completes after configured delay"), Player->IsDead());
    }
    return true;
}

#endif
