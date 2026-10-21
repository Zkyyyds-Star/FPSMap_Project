#if WITH_DEV_AUTOMATION_TESTS
#include "QA/HeliosLiveSettingsWorld.h"
static UWorld* LiveSettingsWorld = nullptr;
#include "QA/HeliosSettingsSessionTests.inl"
#include "QA/HeliosSettingsExpected.h"
#include "Weapons/HeliosMysteriousHand.h"
#include "Weapons/HeliosColaWeapon.h"
#include "Weapons/HeliosChopsticksWeapon.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "Weapons/HeliosBasketballProjectile.h"
#include "Components/SphereComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Weapons/HeliosLightstickWave.h"
#include "Combat/CombatDamageSettings.h"
#include "Weapons/HeliosWeaponDamageSettings.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Weapons/HeliosCombatWeapons.h"
#include "Weapons/HeliosLightstickWeapon.h"
#include "Weapons/HeliosSoulBanner.h"
#include "Weapons/HeliosQingxiao.h"
#include "Weapons/HeliosCalamityStaff.h"
#include "Player/HeliosArenaCharacter.h"
#include "Boss/HeliosEnemyBoss.h"
#include "Pets/HeliosPetDog.h"
#include "Pets/HeliosUltimatumPet.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosDamageFileTest,"Helios.Damage.FileValidation",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FHeliosDamageFileTest::RunTest(const FString&)
{
    using T=ECombatDamageType;
    const FString Dir=FPaths::ProjectSavedDir()/TEXT("DamageTests")/FGuid::NewGuid().ToString();
    IFileManager::Get().MakeDirectory(*Dir,true);
    const FString File=Dir/TEXT("武器伤害.ini");
    const FString Text=TEXT("[突击步枪]\n每颗子弹=37.5\n[激光枪]\n每次脉冲=0\n[模板手枪]\n每颗子弹=-1\n[金纹飞筷]\n每根筷子=NaN\n[青霄]\n普通挥剑=3abc\n每柄飞剑=1e100\n[火箭发射器]\n中心伤害=10\n边缘伤害=99\n[助战]\n刀盾狗格挡减伤比例=1.2\n");
    TestTrue(TEXT("Write Chinese UTF8 fixture"),FFileHelper::SaveStringToFile(Text,*File,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    FCombatDamageTable A; A.LoadDirectory(Dir,false);
    TestEqual(TEXT("Decimal damage"),A.Get(T::Rifle),37.5f);
    TestEqual(TEXT("Zero damage"),A.Get(T::Laser),0.f);
    for(T Type:{T::Pistol,T::Chopsticks,T::QingxiaoSlash,T::QingxiaoSword,T::DogReduction,T::EnemyRifle})
        TestEqual(FCombatDamageTable::Key(Type),A.Get(Type),FCombatDamageTable::Default(Type));
    TestEqual(TEXT("Minimum cannot exceed maximum"),A.Get(T::RocketMin),10.f);
    FFileHelper::SaveStringToFile(TEXT("[突击步枪]\n每颗子弹=9\n"),*File,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    TestEqual(TEXT("Existing snapshot retains values"),A.Get(T::Rifle),37.5f);
    FCombatDamageTable B; B.LoadDirectory(Dir,false);
    TestEqual(TEXT("Fresh snapshot observes edits"),B.Get(T::Rifle),9.f);
    IFileManager::Get().Delete(*File);
    B.LoadDirectory(Dir,false);
    for(int32 I=0;I<static_cast<int32>(T::Count);++I)
        TestEqual(TEXT("Missing files restore every default"),B.Get(static_cast<T>(I)),FCombatDamageTable::Default(static_cast<T>(I)));
    IFileManager::Get().DeleteDirectory(*Dir);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosDamageActorsTest,"Helios.Damage.ActualCombat",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHeliosDamageActorsTest::RunTest(const FString& Parameters)
{
    using T=ECombatDamageType;
    FHeliosSettingsExpected Expected; if (!Expected.Load(*this)) return false;
    FTestWorldWrapper Fixture;
    UWorld* W=LiveSettingsWorld;
    if (!W)
    {
        if(!TestTrue(TEXT("Create world"),Fixture.CreateTestWorld(EWorldType::Game)))return false;
        W=Fixture.GetTestWorld(); W->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();
        if(!TestTrue(TEXT("Begin world"),Fixture.BeginPlayInTestWorld()))return false;
    }
    FHeliosLiveSettingsCleanup Cleanup(LiveSettingsWorld);
    TestNotNull(TEXT("Map settings"),W->GetSubsystem<UCombatDamageSettings>());
    for(int32 I=0;I<static_cast<int32>(T::Count);++I)
        TestEqual(FCombatDamageTable::Key(static_cast<T>(I)),UCombatDamageSettings::Resolve(W,static_cast<T>(I)),Expected.Get(static_cast<T>(I)));
    const float TestZ=LiveSettingsWorld?30200.f:200.f;
    FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* P=W->SpawnActor<AHeliosArenaCharacter>(FVector(0,-2000,TestZ),FRotator::ZeroRotator,Spawn);
    auto* E=W->SpawnActor<AHeliosEnemyE01>(FVector(2000,0,TestZ),FRotator::ZeroRotator,Spawn);
    if(!TestNotNull(TEXT("Player"),P)||!TestNotNull(TEXT("Target"),E))return false;
    P->SetActorTickEnabled(false); E->SetActorTickEnabled(false); E->StopShooting();
    auto CheckDamage=[&](const TCHAR* Name,float Before,float Want)
    {
        const float Actual=Before-E->GetHealth();
        AddInfo(FString::Printf(TEXT("DAMAGE_CHECK %s actual=%.3f expected=%.3f"),Name,Actual,Want));
        TestTrue(Name,FMath::IsNearlyEqual(Actual,Want,.02f));
    };
    auto RunBossContacts=[&]()
    {
        const FVector BossStart(5000,0,TestZ);
        auto* Boss=W->SpawnActor<AHeliosEnemyBoss>(BossStart,FRotator::ZeroRotator,Spawn);
        if(!TestNotNull(TEXT("Boss contact fixture"),Boss))return;
        Boss->SetActorTickEnabled(false);
        FindFProperty<FFloatProperty>(P->GetClass(),TEXT("SpawnProtectionSeconds"))->SetPropertyValue_InContainer(P,0);
        P->ResetForTest();
        auto ResetBossContact=[&](int32 Attack)
        {
            Boss->SetActorLocation(BossStart);
            Boss->BossAttack=Attack==0?EHeliosBossAttackType::CloseShoulder:EHeliosBossAttackType::ShoulderCharge;
            Boss->HitTargets.Reset();Boss->bChargeStopped=false;Boss->ChargeDistance=0;
            Boss->AttackDirection=FVector::ForwardVector;Boss->PreviousShoulder=Boss->ShoulderLocation();
        };
        auto HealthOf=[&](APawn* Target)
        {
            if(auto* Player=Cast<AHeliosArenaCharacter>(Target))return Player->GetHealth();
            if(auto* Pet=Cast<AHeliosPetCompanion>(Target))return Pet->Health;
            if(auto* Summon=Cast<AHeliosSoulSkeleton>(Target))return Summon->Health;
            return -1.f;
        };
        auto ResetTargetHealth=[&](APawn* Target)
        {
            if(auto* Player=Cast<AHeliosArenaCharacter>(Target))Player->Heal(Player->GetMaxHealth());
            else if(auto* Pet=Cast<AHeliosPetCompanion>(Target)){Pet->Health=1000.f;Pet->State=EHeliosPetState::Following;}
            else if(auto* Summon=Cast<AHeliosSoulSkeleton>(Target))Summon->Health=1000.f;
        };
        auto MakeBlocker=[&](bool bSolid)
        {
            auto* Wall=W->SpawnActor<AActor>();
            auto* Box=NewObject<UBoxComponent>(Wall);Wall->AddInstanceComponent(Box);Wall->SetRootComponent(Box);
            Box->SetBoxExtent(FVector(5,500,500));Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            Box->SetCollisionResponseToAllChannels(bSolid?ECR_Block:ECR_Ignore);
            if(!bSolid)Box->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
            Box->RegisterComponent();Wall->SetActorLocation(BossStart+FVector(75,0,0));
            return Wall;
        };
        auto CheckTarget=[&](APawn* Target,const FString& Name)
        {
            if(!TestNotNull(*Name,Target))return;
            Target->SetActorTickEnabled(false);
            for(int32 Attack=0;Attack<2;++Attack)for(float Distance:{120.f,249.f})
            {
                const FString Label=FString::Printf(TEXT("Boss %s attack=%d distance=%.0f"),*Name,Attack,Distance);
                ResetTargetHealth(Target);Target->SetActorLocation(BossStart+FVector(Distance,0,0));
                ResetBossContact(Attack);
                const float Want=Expected.Get(Attack==0?T::BossClose:T::BossCharge);
                const float Before=HealthOf(Target);const int32 HitsBefore=Boss->GetAttacksCommitted();
                Boss->TickShoulder(.2f);
                TestTrue(*(Label+TEXT(" contact damage")),FMath::IsNearlyEqual(Before-HealthOf(Target),Want,.02f));
                TestTrue(*(Label+TEXT(" stops on contact")),Boss->bChargeStopped);
                TestEqual(*(Label+TEXT(" applied hit count")),Boss->GetAttacksCommitted(),HitsBefore+(Want>0.f?1:0));
                Boss->DamageContactTarget(Target,Want,true);Boss->TickShoulder(.2f);
                TestTrue(*(Label+TEXT(" once per cast")),FMath::IsNearlyEqual(Before-HealthOf(Target),Want,.02f));
                ResetBossContact(Attack);Boss->TickShoulder(.2f);
                TestTrue(*(Label+TEXT(" next cast damages again")),FMath::IsNearlyEqual(Before-HealthOf(Target),Want*2.f,.02f));
            }
            // A visibility-only prop must not cancel a real collision. Solid cover must.
            for(bool bSolid:{false,true})
            {
                const FString Label=Name+(bSolid?TEXT(" solid cover"):TEXT(" visibility-only prop"));
                ResetTargetHealth(Target);Target->SetActorLocation(BossStart+FVector(120,0,0));
                auto* Wall=MakeBlocker(bSolid);ResetBossContact(0);
                const float Before=HealthOf(Target);Boss->TickShoulder(.2f);
                const float Want=bSolid?0.f:Expected.Get(T::BossClose);
                TestTrue(*Label,FMath::IsNearlyEqual(Before-HealthOf(Target),Want,.02f));
                Wall->Destroy();
            }
            Target->SetActorLocation(BossStart+FVector(0,1000,0));
            ResetTargetHealth(Target);ResetBossContact(0);
            const float SafeHealth=HealthOf(Target);Boss->TickShoulder(.2f);
            TestEqual(*(Name+TEXT(" no contact")),HealthOf(Target),SafeHealth);
        };

        CheckTarget(P,TEXT("player"));
        P->SetActorLocation(FVector(0,-2000,TestZ));
        P->SetCanBeDamaged(false);P->Heal(P->GetMaxHealth());
        P->SetActorLocation(BossStart+FVector(120,0,0));ResetBossContact(0);
        const float ProtectedHealth=P->GetHealth();const int32 ProtectedHits=Boss->GetAttacksCommitted();
        Boss->TickShoulder(.2f);
        TestEqual(TEXT("Protected player takes no contact damage"),P->GetHealth(),ProtectedHealth);
        TestEqual(TEXT("Protected contact is not an applied hit"),Boss->GetAttacksCommitted(),ProtectedHits);
        TestTrue(TEXT("Protected contact stops the drive"),Boss->bChargeStopped);
        P->SetCanBeDamaged(true);P->SetActorLocation(FVector(0,-2000,TestZ));

        auto* ProtectionProperty=FindFProperty<FFloatProperty>(P->GetClass(),TEXT("SpawnProtectionSeconds"));
        ProtectionProperty->SetPropertyValue_InContainer(P,3.f);P->ResetForTest();
        P->SetActorLocation(BossStart+FVector(120,0,0));ResetBossContact(0);
        const float RespawnHealth=P->GetHealth();const int32 RespawnHits=Boss->GetAttacksCommitted();
        Boss->TickShoulder(.2f);
        TestEqual(TEXT("Respawn protection rejects Boss contact damage"),P->GetHealth(),RespawnHealth);
        TestEqual(TEXT("Respawn protection is not an applied hit"),Boss->GetAttacksCommitted(),RespawnHits);
        TestTrue(TEXT("Respawn protected contact stops the drive"),Boss->bChargeStopped);
        ProtectionProperty->SetPropertyValue_InContainer(P,0.f);P->ResetForTest();
        P->SetActorLocation(FVector(0,-2000,TestZ));

        for(UClass* PetClass:{AHeliosPetDog::StaticClass(),AHeliosUltimatumPet::StaticClass()})
        {
            auto* Pet=W->SpawnActor<AHeliosPetCompanion>(PetClass,BossStart+FVector(120,0,0),FRotator::ZeroRotator,Spawn);
            if(!TestNotNull(TEXT("Pet contact fixture"),Pet))continue;
            CheckTarget(Pet,PetClass==AHeliosPetDog::StaticClass()?TEXT("sword dog"):TEXT("ultimatum pet"));
            if(PetClass==AHeliosPetDog::StaticClass())
            {
                Pet->Health=1000.f;Pet->State=EHeliosPetState::Fighting;Pet->AttackAge=-1.f;
                Pet->SetActorLocation(BossStart+FVector(120,0,0));Pet->SetActorRotation(FRotator(0,180,0));
                ResetBossContact(0);const float Before=Pet->Health;Boss->TickShoulder(.2f);
                const float Want=Expected.Get(T::BossClose)*(1.f-Pet->Settings.ShieldReduction);
                TestTrue(TEXT("Sword dog guard applies to Boss contact"),FMath::IsNearlyEqual(Before-Pet->Health,Want,.02f));
                Pet->Sleep();ResetBossContact(0);const int32 HitsBefore=Boss->GetAttacksCommitted();
                TestEqual(TEXT("Sleeping pet rejects direct damage"),Boss->DamageContactTarget(Pet,Expected.Get(T::BossClose),true),0.f);
                Boss->TickShoulder(.2f);
                TestEqual(TEXT("Sleeping pet is not an applied hit"),Boss->GetAttacksCommitted(),HitsBefore);
            }
            Pet->Destroy();
        }

        auto* ContactFlag=W->SpawnActor<AHeliosSoulFlag>(BossStart+FVector(0,2000,0),FRotator::ZeroRotator,Spawn);
        for(int32 Role:{0,2})
        {
            auto* Summon=W->SpawnActor<AHeliosSoulSkeleton>(BossStart+FVector(120,0,0),FRotator::ZeroRotator,Spawn);
            if(!TestNotNull(TEXT("Summon contact fixture"),Summon))continue;
            Summon->SetOwner(P);Summon->Initialize(ContactFlag,Role);
            TestEqual(TEXT("Summon retains Pawn-ignore"),Summon->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn),ECR_Ignore);
            CheckTarget(Summon,Role==0?TEXT("soul warrior"):TEXT("soul mage"));
            Summon->Tags.AddUnique(TEXT("Dead"));ResetBossContact(0);
            const float Before=Summon->Health;const int32 HitsBefore=Boss->GetAttacksCommitted();
            TestEqual(TEXT("Dead summon rejects direct damage"),Boss->DamageContactTarget(Summon,Expected.Get(T::BossClose),true),0.f);
            Boss->TickShoulder(.2f);
            TestEqual(TEXT("Dead summon health unchanged"),Summon->Health,Before);
            TestEqual(TEXT("Dead summon is not an applied hit"),Boss->GetAttacksCommitted(),HitsBefore);
            Summon->Tags.Remove(TEXT("Dead"));
            const float Want=Expected.Get(T::BossClose);
            if(Want>0.f)
            {
                Summon->Health=Want*.5f;Summon->SetActorLocation(BossStart+FVector(120,0,0));
                ResetBossContact(0);Boss->TickShoulder(.2f);
                TestFalse(TEXT("Lethal Boss contact kills summon"),Summon->Alive());
                TestEqual(TEXT("Dead summon collision disabled"),Summon->GetCapsuleComponent()->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
            }
            Summon->Destroy();
        }
        ContactFlag->Destroy();Boss->Destroy();P->SetActorLocation(FVector(0,-2000,TestZ));
    };
    if(Parameters==TEXT("BossContactOnly"))
    {
        RunBossContacts();
        return true;
    }
    // Registry assets must stay immutable. The 20-metre mech uses a dedicated deployment fixture.
    for(auto* D:UHeliosWeaponRegistry::GetAvailableWeapons())
    {
        const float Original=D->Damage;
        if(D->WeaponId==TEXT("mech"))
        {
            auto* Copy=DuplicateObject<UHeliosWeaponDefinition>(D,GetTransientPackage());
            ApplyWeaponDamageSettings(W,Copy);
            TestEqual(TEXT("Mech configured damage without deployment in this small fixture"),Copy->Damage,Expected.Get(T::MechStomp));
            TestEqual(TEXT("Mech registry asset unchanged"),D->Damage,Original);
            continue;
        }
        TestTrue(*D->WeaponId.ToString(),P->EquipWeaponDefinition(D));
        TestEqual(TEXT("Registry asset unchanged"),D->Damage,Original);
        if(auto* Weapon=Cast<AHeliosCombatWeapon>(P->GetEquippedWeapon()))
        {
            TestTrue(TEXT("Definition is a per-weapon copy"),Weapon->ActiveDefinition!=D);
            const auto* Active = Weapon->ActiveDefinition.Get();
            const TMap<FName,T> Primary = {{TEXT("rocket"),T::RocketMax},{TEXT("laser"),T::Laser},
                {TEXT("shotgun"),T::ShotgunMax},{TEXT("chopsticks"),T::Chopsticks},{TEXT("cola"),T::ColaMax},
                {TEXT("lightstick"),T::Light1},{TEXT("soulbanner"),T::Soul1},{TEXT("qingxiao"),T::QingxiaoSlash},
                {TEXT("calamity"),T::Earthquake},{TEXT("mysterious_hand"),T::MysteriousLaser},{TEXT("cfm_sunshangxiang"),T::SSXLight}};
            if (const auto* Type=Primary.Find(D->WeaponId))
                TestEqual(*D->WeaponId.ToString(),Active->Damage,Expected.Get(*Type));
            else if(D->WeaponId==TEXT("fighter"))AddInfo(TEXT("Fighter uses its independent settings schema, validated by Helios.Fighter tests."));
            else AddError(TEXT("Unmapped combat weapon in settings regression: ")+D->WeaponId.ToString());
            if (auto* C=Cast<UHeliosColaWeaponDefinition>(Active)) TestEqual(TEXT("Cola edge"),C->MinimumDamage,Expected.Get(T::ColaMin));
            if (D->WeaponId==TEXT("rocket")) TestEqual(TEXT("Rocket edge"),Active->MinimumDamage,Expected.Get(T::RocketMin));
            if (D->WeaponId==TEXT("shotgun")) TestEqual(TEXT("Shotgun far"),Active->MinimumDamage,Expected.Get(T::ShotgunMin));
            if (auto* H=Cast<UHeliosMysteriousHandDefinition>(Active))
            { TestEqual(TEXT("Missile center"),H->MissileMaxDamage,Expected.Get(T::MysteriousMissileMax)); TestEqual(TEXT("Missile edge"),H->MissileMinDamage,Expected.Get(T::MysteriousMissileMin)); }
            if (auto* Q=Cast<UHeliosQingxiaoDefinition>(Active)) TestEqual(TEXT("Flying sword"),Q->SwordDamage,Expected.Get(T::QingxiaoSword));
            if (auto* L=Cast<UHeliosLightstickDefinition>(Active))
            { TestEqual(TEXT("Heavy minimum"),L->HeavyMinimumDamage,Expected.Get(T::HeavyMin)); TestEqual(TEXT("Heavy maximum"),L->HeavyMaximumDamage,Expected.Get(T::HeavyMax)); }
            if (auto* S=Cast<UHeliosSoulBannerDefinition>(Active))
                for(int32 I=0; I<3; ++I) TestEqual(TEXT("Soul combo"),S->ComboDamage[I],Expected.Get(I==0?T::Soul1:I==1?T::Soul2:T::Soul3));
            if (auto* C=Cast<UHeliosCalamityDefinition>(Active))
            { TestEqual(TEXT("Fire center"),C->Spells[1].Damage,Expected.Get(T::FireMax)); TestEqual(TEXT("Fire edge"),C->Spells[1].MinimumDamage,Expected.Get(T::FireMin)); TestEqual(TEXT("Tower multiplier"),C->TowerDamageScale,Expected.Get(T::TowerScale)); }
        }
    }
    // Real template bullet/grenade collision dispatch, after setting damage before FinishSpawning.
    auto* BulletClass=LoadClass<AShooterProjectile>(nullptr,TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Projectiles/BP_ShooterProjectile_Bullet.BP_ShooterProjectile_Bullet_C"));
    auto* GrenadeClass=LoadClass<AShooterProjectile>(nullptr,TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Projectiles/BP_ShooterProjectile_Grenade.BP_ShooterProjectile_Grenade_C"));
    if(!TestNotNull(TEXT("Bullet asset"),BulletClass)||!TestNotNull(TEXT("Grenade asset"),GrenadeClass))return false;
    const TCHAR* Names[]={TEXT("Rifle"),TEXT("Pistol"),TEXT("GrenadeLauncher")};
    const T Types[]={T::Rifle,T::Pistol,T::Grenade};
    for(int32 I=0;I<3;++I)
    {
        const FString Path=FString::Printf(TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_%s.BP_ShooterWeapon_%s_C"),Names[I],Names[I]);
        UClass* Class=LoadClass<AShooterWeapon>(nullptr,*Path);
        if(!TestNotNull(TEXT("Template weapon"),Class))continue;
        const auto* Weapon=Class->GetDefaultObject<AShooterWeapon>();
        FTransform At(E->GetActorLocation()+FVector(-100,0,0));
        auto* Shot=W->SpawnActorDeferred<AShooterProjectile>(I==2?GrenadeClass:BulletClass,At,P,P,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!TestNotNull(TEXT("Spawn template projectile"),Shot))continue;
        Shot->ConfigureDamageForWeapon(Weapon); Shot->FinishSpawning(At);
        E->CurrentHP=10000;
        FHitResult Hit(E,E->GetCapsuleComponent(),E->GetActorLocation(),-FVector::ForwardVector);
        static_cast<AActor*>(Shot)->NotifyHit(nullptr,E,E->GetCapsuleComponent(),true,E->GetActorLocation(),-FVector::ForwardVector,FVector::ZeroVector,Hit);
        CheckDamage(Names[I],10000,Expected.Get(Types[I]));
        Shot->Destroy();
    }
    // Rocket falloff at half radius uses both configured endpoints.
    auto* Rocket=W->SpawnActor<AHeliosRocketProjectile>(E->GetActorLocation()+FVector(-500,0,0),FRotator::ZeroRotator,Spawn);
    Rocket->SetOwner(P); Rocket->SetInstigator(P);
    P->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("rocket")));
    const auto* RocketSettings=Cast<AHeliosCombatWeapon>(P->GetEquippedWeapon())->ActiveDefinition.Get();
    Rocket->ConfigureRocket(RocketSettings->Damage,RocketSettings->MinimumDamage,1000,1000,nullptr);
    E->CurrentHP=10000;
    FHitResult RocketHit; RocketHit.ImpactPoint=Rocket->GetActorLocation();
    static_cast<AActor*>(Rocket)->NotifyHit(nullptr,nullptr,nullptr,true,Rocket->GetActorLocation(),FVector::UpVector,FVector::ZeroVector,RocketHit);
    CheckDamage(TEXT("Rocket falloff"),10000,FMath::Lerp(Expected.Get(T::RocketMax),Expected.Get(T::RocketMin),.5f));
    Rocket->Destroy();
    // All three melee stages use the configured values and the real collision trace.
    auto* Light=UHeliosWeaponRegistry::FindWeapon(TEXT("lightstick"));
    if(TestNotNull(TEXT("Lightstick definition"),Light)&&P->EquipWeaponDefinition(Light))
    {
        P->SetActorLocation(E->GetActorLocation()-FVector(220,0,0));
        auto* Weapon=Cast<AHeliosLightstickWeapon>(P->GetEquippedWeapon());
        if(TestNotNull(TEXT("Lightstick weapon"),Weapon))for(int32 I=0;I<3;++I)
        {
            const T Type=I==0?T::Light1:I==1?T::Light2:T::Light3;
            TestEqual(TEXT("Combo configured"),Weapon->Settings->ComboDamage[I],Expected.Get(Type));
            E->CurrentHP=10000; Weapon->HitActors.Reset();
            Weapon->TraceBlade(E->GetActorLocation()-FVector(100,0,0),E->GetActorLocation()+FVector(100,0,0),Weapon->Settings->ComboDamage[I]);
            CheckDamage(TEXT("Melee stage"),10000,Expected.Get(Type));
        }
    }
    P->SetActorLocation(FVector(0,-2000,TestZ));
    P->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("calamity")));
    auto* State=UHeliosCalamityState::Get(P);
    if(TestNotNull(TEXT("Spell settings"),State->Settings.Get()))
    {
        const EHeliosCalamitySpell Spells[]={EHeliosCalamitySpell::Earthquake,EHeliosCalamitySpell::Fireball,EHeliosCalamitySpell::Tornado};
        const T SpellTypes[]={T::Earthquake,T::FireMax,T::Tornado};
        for(int32 I=0;I<3;++I)
        {
            E->CurrentHP=10000; State->DamageArea(E->GetActorLocation(),Spells[I],State->Settings->TowerDamageScale);
            CheckDamage(TEXT("Spell actual damage"),10000,Expected.Get(SpellTypes[I])*(I==2?1.f:Expected.Get(T::TowerScale)));
        }
        auto* Poison=W->SpawnActor<AHeliosCalamityEffect>(E->GetActorLocation(),FRotator::ZeroRotator,Spawn);
        Poison->Spell=EHeliosCalamitySpell::Poison;Poison->bField=true;Poison->Duration=10;State->Effects.Add(Poison);
        E->CurrentHP=10000; State->TickComponent(.2f,LEVELTICK_All,nullptr);
        CheckDamage(TEXT("Poison fixed pulse"),10000,Expected.Get(T::Poison));
        State->Effects.Add(Poison); E->CurrentHP=10000; State->TickComponent(.2f,LEVELTICK_All,nullptr);
        CheckDamage(TEXT("Poison overlap does not stack"),10000,Expected.Get(T::Poison));
        State->Effects.Reset();Poison->Destroy();
        E->CurrentHP=10000;
        State->DamageArea(E->GetActorLocation()+FVector(0,State->Settings->Spells[1].Radius,0),EHeliosCalamitySpell::Fireball,State->Settings->TowerDamageScale);
        CheckDamage(TEXT("Fireball edge damage"),10000,Expected.Get(T::FireMin)*Expected.Get(T::TowerScale));
    }
    // Verify configured pet values are applied at spawn.
    auto* Dog=W->SpawnActor<AHeliosPetCompanion>(FVector(0,6000,TestZ),FRotator::ZeroRotator,Spawn);
    TestEqual(TEXT("Dog damage"),Dog->Settings.Damage,Expected.Get(T::Dog));
    TestEqual(TEXT("Dog guard reduction"),Dog->Settings.ShieldReduction,Expected.Get(T::DogReduction));
    auto* Pet=W->SpawnActor<AHeliosUltimatumPet>(FVector(0,6500,TestZ),FRotator::ZeroRotator,Spawn);
    TestEqual(TEXT("Rifle companion damage"),Pet->RifleSettings.Damage,Expected.Get(T::Ultimatum));
    RunBossContacts();
    // Real attack paths with deterministic placement. These exercise the configured
    // runtime copies, not manually supplied expected damage values.
    P->SetActorLocation(FVector(20000,0,TestZ)); P->SetActorRotation(FRotator::ZeroRotator);
    auto AimTarget=[&](float Distance)
    {
        FVector Eye; FRotator Rotation; P->GetActorEyesViewPoint(Eye,Rotation);
        E->SetActorLocation(Eye+Rotation.Vector()*Distance); E->CurrentHP=10000;
    };
    P->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("laser"))); AimTarget(500);
    P->GetEquippedWeapon()->StartFiring(); P->GetEquippedWeapon()->StopFiring();
    CheckDamage(TEXT("Laser pulse real trace"),10000,Expected.Get(T::Laser));
    P->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("shotgun"))); AimTarget(400);
    if(auto* S=Cast<AHeliosShotgunWeapon>(P->GetEquippedWeapon()))
    {
        S->SpreadHalfAngle=0; S->PelletCount=1;
        S->FireProjectile(E->GetActorLocation());
        CheckDamage(TEXT("Shotgun near pellet"),10000,Expected.Get(T::ShotgunMax));
        E->CurrentHP=10000; S->FalloffStart=-100000; S->ShotgunRange=500;
        // Force alpha=1 without changing damage, for a deterministic endpoint check.
        S->FalloffStart=S->ShotgunRange;
        S->FireProjectile(E->GetActorLocation());
        CheckDamage(TEXT("Shotgun far pellet"),10000,Expected.Get(T::ShotgunMin));
    }
    P->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("chopsticks"))); AimTarget(500);
    if(auto* C=Cast<AHeliosCombatWeapon>(P->GetEquippedWeapon()))
    {
        const FTransform At(E->GetActorLocation()-FVector(100,0,0));
        auto* Shot=W->SpawnActorDeferred<AHeliosChopstickProjectile>(AHeliosChopstickProjectile::StaticClass(),At,P,P,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Shot->Configure(FVector::ForwardVector,C->ActiveDefinition->Damage,6500,5000,nullptr,nullptr,nullptr); Shot->FinishSpawning(At);
        FHitResult Hit(E,E->GetCapsuleComponent(),E->GetActorLocation(),-FVector::ForwardVector);
        static_cast<AActor*>(Shot)->NotifyHit(nullptr,E,E->GetCapsuleComponent(),true,E->GetActorLocation(),-FVector::ForwardVector,FVector::ZeroVector,Hit);
        CheckDamage(TEXT("Chopstick real collision"),10000,Expected.Get(T::Chopsticks)); Shot->Destroy();
    }
    P->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("cola"))); AimTarget(500);
    if(auto* C=Cast<AHeliosCombatWeapon>(P->GetEquippedWeapon()))
    {
        auto* D=Cast<UHeliosColaWeaponDefinition>(C->ActiveDefinition);
        for(float Alpha : {0.f,.5f,1.f})
        {
            const float Distance=FMath::Lerp(D->InnerRadius,D->OuterRadius,Alpha);
            auto* Shot=W->SpawnActor<AHeliosColaProjectile>(E->GetActorLocation()+FVector(0,-Distance,0),FRotator::ZeroRotator,Spawn);
            Shot->SetOwner(P);Shot->SetInstigator(P);Shot->Configure(FVector::ForwardVector,D);
            E->CurrentHP=10000;Shot->Explode();
            CheckDamage(TEXT("Cola explosion falloff"),10000,FMath::Lerp(Expected.Get(T::ColaMax),Expected.Get(T::ColaMin),Alpha));Shot->Destroy();
        }
    }
    P->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("mysterious_hand"))); AimTarget(800);
    if(auto* H=Cast<AHeliosMysteriousHand>(P->GetEquippedWeapon()))
    {
        H->CommitLaser(); CheckDamage(TEXT("Mysterious laser real trace"),10000,Expected.Get(T::MysteriousLaser));
        for(float Alpha : {0.f,.5f,1.f})
        {
            const FVector Center=E->GetActorLocation()+FVector(0,-H->Settings->MissileRadius*Alpha,0);
            auto* Missile=W->SpawnActor<AHeliosHandMissile>(Center+FVector(0,0,6000),FRotator::ZeroRotator,Spawn);
            Missile->SetOwner(P);Missile->SetInstigator(P);Missile->Launch(Center,FVector::UpVector,H->Settings);
            E->CurrentHP=10000;Missile->Explode(Center,FVector::UpVector);
            CheckDamage(TEXT("Mysterious missile falloff"),10000,FMath::Lerp(Expected.Get(T::MysteriousMissileMax),Expected.Get(T::MysteriousMissileMin),Alpha));Missile->Destroy();
        }
    }
    P->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("soulbanner"))); AimTarget(180);
    if(auto* S=Cast<AHeliosSoulBanner>(P->GetEquippedWeapon()))
        for(int32 I=0;I<3;++I)
        {
            S->Combo=I;S->HitActors.Reset();E->CurrentHP=10000;S->Sweep(.22f,.65f);
            CheckDamage(TEXT("Soul banner combo real sweep"),10000,Expected.Get(I==0?T::Soul1:I==1?T::Soul2:T::Soul3));
        }
    P->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("lightstick"))); AimTarget(500);
    if(auto* L=Cast<AHeliosLightstickWeapon>(P->GetEquippedWeapon()))
    {
        for(float Charge : {0.f,1.f})
        {
            auto* Wave=W->SpawnActor<AHeliosLightstickWave>(P->FirstPersonCamera->GetComponentLocation(),FRotator::ZeroRotator,Spawn);
            Wave->SetOwner(P);Wave->SetInstigator(P);E->CurrentHP=10000;
            Wave->Initialize(L->Settings,Charge,P->FirstPersonCamera->GetComponentLocation());
            for(int32 I=0;I<60&&Wave->IsFlying();++I)Wave->Tick(.02f);
            CheckDamage(TEXT("Lightstick heavy wave"),10000,Expected.Get(Charge==0?T::HeavyMin:T::HeavyMax));Wave->Destroy();
        }
    }
    P->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("qingxiao"))); AimTarget(180);
    if(auto* Q=Cast<AHeliosQingxiao>(P->GetEquippedWeapon()))
    {
        Q->StartFiring();Q->Sweep(0,1);Q->StopFiring();
        CheckDamage(TEXT("Qingxiao real blade sweep"),10000,Expected.Get(T::QingxiaoSlash));
        AimTarget(1500);
        auto* Formation=UHeliosQingxiaoFormation::Get(P);
        if(TestTrue(TEXT("Begin configured sword wave"),Formation->Begin(Q->Settings)))
        {
            Formation->Age=2;auto* Sword=Formation->Swords[0].Get();Sword->Target=E;
            for(int32 I=0;I<150&&!Sword->IsDissolving();++I)Sword->Tick(.02f);
            CheckDamage(TEXT("Qingxiao flying sword hit"),10000,Expected.Get(T::QingxiaoSword));
            Formation->Clear();
        }
    }
    // Enemy settings must reach their actual melee/hitscan attacks as well.
    P->SetActorLocation(FVector(30000,0,TestZ));
    for(const auto& Pair : TArray<TPair<UClass*,T>>{{AHeliosEnemyE01::StaticClass(),T::EnemyRifle},{AHeliosEnemyE02::StaticClass(),T::EnemyCharger},{AHeliosEnemyE03::StaticClass(),T::EnemySkirmisher},{AHeliosEnemyE04::StaticClass(),T::EnemyBasketball}})
    {
        P->Heal(P->GetMaxHealth());
        auto* Enemy=W->SpawnActor<AHeliosEnemyActor>(Pair.Key,P->GetActorLocation()-FVector(100,0,0),FRotator::ZeroRotator,Spawn);
        TestEqual(TEXT("Enemy configured attack"),Enemy->AttackDamage,Expected.Get(Pair.Value));
        if(Pair.Value!=T::EnemyBasketball)
        {
            Enemy->StartShooting(P);
            const float Before=P->GetHealth();Enemy->PerformHitscanOrMeleeAttack(P);
            TestTrue(TEXT("Enemy actual hit"),FMath::IsNearlyEqual(Before-P->GetHealth(),Expected.Get(Pair.Value),.02f));
        }
        else
        {
            Enemy->ThrowBasketball(P);
            bool Found=false;
            for(TActorIterator<AHeliosBasketballProjectile> It(W);It;++It)if(It->GetOwner()==Enemy)
            {
                Found=true;auto* Collision=It->FindComponentByClass<USphereComponent>();
                FHitResult Hit(P,P->GetCapsuleComponent(),P->GetActorLocation(),FVector::ForwardVector);
                const float Before=P->GetHealth();
                Collision->OnComponentHit.Broadcast(Collision,P,P->GetCapsuleComponent(),FVector::ZeroVector,Hit);
                TestTrue(TEXT("Basketball actual hit"),FMath::IsNearlyEqual(Before-P->GetHealth(),Expected.Get(T::EnemyBasketball),.02f));
            }
            TestTrue(TEXT("Basketball spawned"),Found);
        }
        Enemy->Destroy();
    }
    // Guard reduction is tested on the actual incoming damage path.
    Dog->State=EHeliosPetState::Fighting;Dog->AttackAge=-1;Dog->SetActorRotation(FRotator::ZeroRotator);
    E->SetActorLocation(Dog->GetActorLocation()+FVector(100,0,0));
    const float DogBefore=Dog->Health;FDamageEvent Event;
    Dog->TakeDamage(10,Event,nullptr,E);
    TestTrue(TEXT("Dog configured guard"),FMath::IsNearlyEqual(DogBefore-Dog->Health,10*(1-Expected.Get(T::DogReduction)),.02f));
    // Companions and summons use their configured values at the hit, too.
    E->SetActorLocation(FVector(40000,0,TestZ));E->CurrentHP=10000;
    P->SetActorLocation(E->GetActorLocation()-FVector(300,0,0));
    Dog->SetActorLocation(E->GetActorLocation()-FVector(100,0,0));Dog->CompanionOwner=P;Dog->Target=E;Dog->bHitCommitted=false;
    Dog->AttackHit();CheckDamage(TEXT("Dog actual melee"),10000,Expected.Get(T::Dog));
    Dog->SetActorLocation(FVector(0,6000,TestZ));
    auto* Flag=W->SpawnActor<AHeliosSoulFlag>(E->GetActorLocation(),FRotator::ZeroRotator,Spawn);
    auto* Skeleton=W->SpawnActor<AHeliosSoulSkeleton>(E->GetActorLocation()-FVector(100,0,0),FRotator::ZeroRotator,Spawn);
    Skeleton->SetOwner(P);Skeleton->Initialize(Flag,0);Skeleton->Target=E;E->CurrentHP=10000;
    Skeleton->Hit();CheckDamage(TEXT("Soul warrior actual melee"),10000,Expected.Get(T::SoulWarrior));
    Skeleton->SetActorLocation(E->GetActorLocation()-FVector(500,0,0));
    auto* Bolt=W->SpawnActor<AHeliosSoulBolt>(E->GetActorLocation()-FVector(100,0,0),FRotator::ZeroRotator,Spawn);
    Bolt->Caster=Skeleton;Bolt->Velocity=FVector(1500,0,0);E->CurrentHP=10000;Bolt->Tick(.1f);
    CheckDamage(TEXT("Soul mage actual bolt"),10000,Expected.Get(T::SoulMage));
    Bolt->Destroy();Skeleton->Destroy();Flag->Destroy();
    E->CurrentHP=10000;Pet->SetActorLocation(E->GetActorLocation()-FVector(500,0,0));
    P->SetActorLocation(Pet->GetActorLocation()-FVector(300,0,0));
    Pet->SetActorRotation(FRotator::ZeroRotator);Pet->CompanionOwner=P;Pet->Target=E;
    Pet->GetCharacterMovement()->DisableMovement();Pet->RifleSettings.SpreadDegrees=0;
    Pet->Rifle->SetWorldRotation((Pet->AimPoint(E)-Pet->GetMesh()->GetSocketLocation(TEXT("rifle_muzzle"))).Rotation());
    Pet->FireRound();CheckDamage(TEXT("Rifle companion actual bullet"),10000,Expected.Get(T::Ultimatum));
    Pet->SetActorLocation(FVector(0,6500,TestZ));
    for(bool NPC : {false,true})for(bool Grenade : {false,true})
    {
        AActor* Target=NPC?static_cast<AActor*>(P):E;
        APawn* Shooter=NPC?static_cast<APawn*>(E):P;
        P->Heal(P->GetMaxHealth());E->CurrentHP=10000;
        const float Before=NPC?P->GetHealth():E->GetHealth();
        const FTransform At(Target->GetActorLocation()+FVector(0,-100,0));
        auto* Shot=W->SpawnActorDeferred<AShooterProjectile>(Grenade?GrenadeClass:BulletClass,At,Shooter,Shooter,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Shot->ConfigureDamageForWeapon(nullptr);Shot->FinishSpawning(At);
        FHitResult Hit(Target,Target->FindComponentByClass<UCapsuleComponent>(),Target->GetActorLocation(),FVector::RightVector);
        static_cast<AActor*>(Shot)->NotifyHit(nullptr,Target,Target->FindComponentByClass<UCapsuleComponent>(),true,Target->GetActorLocation(),FVector::RightVector,FVector::ZeroVector,Hit);
        const float Actual=Before-(NPC?P->GetHealth():E->GetHealth());
        const T Type=NPC?(Grenade?T::NPCGrenade:T::NPCBullet):(Grenade?T::TemplateGrenade:T::TemplateBullet);
        TestTrue(TEXT("Template/NPC projectile actual damage"),FMath::IsNearlyEqual(Actual,Expected.Get(Type),.02f));
        Shot->Destroy();
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosBossContactRegressionTest,"Helios.Damage.BossSummonContact",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHeliosBossContactRegressionTest::RunTest(const FString&)
{
    struct FContactFixture : FHeliosDamageActorsTest
    {
        FContactFixture():FHeliosDamageActorsTest(TEXT("Helios.BossContactFixture")){}
        bool Execute(){return RunTest(TEXT("BossContactOnly"));}
    } ContactTest;
    const bool Ran=ContactTest.Execute();
    FAutomationTestExecutionInfo Info;ContactTest.GetExecutionInfo(Info);
    for(const auto& Entry:Info.GetEntries())AddEvent(Entry.Event);
    return Ran&&!ContactTest.HasAnyErrors();
}
bool HeliosCheckLiveDamage(UWorld* World)
{
    LiveSettingsWorld=World;
    struct FLive : FHeliosDamageActorsTest {
        FLive():FHeliosDamageActorsTest(TEXT("Helios.Live.Damage")){}
        bool Execute(){return RunTest(TEXT("LiveWorld"));}
    } Test;
    const bool Ran=Test.Execute();
    LiveSettingsWorld=nullptr;
    FAutomationTestExecutionInfo Info; Test.GetExecutionInfo(Info);
    for(const auto& Entry:Info.GetEntries()) UE_LOG(LogTemp,Display,TEXT("LIVE_DAMAGE %s"),*Entry.Event.Message);
    return Ran&&!Test.HasAnyErrors();
}
#endif
