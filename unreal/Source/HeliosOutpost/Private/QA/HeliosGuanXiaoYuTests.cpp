#if WITH_DEV_AUTOMATION_TESTS
#include "Player/HeliosGuanXiaoYuAbilityComponent.h"
#include "Player/HeliosArenaCharacter.h"
#include "Player/HeliosCharacterDefinition.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Pets/HeliosHumanoidPet.h"
#include "Enemies/HeliosEnemyActor.h"
#include "ShooterWeapon.h"
#include "Weapons/HeliosSSXVoice.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DamageEvents.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosGuanXiaoYuTest,"Helios.GuanXiaoYu.AbilityLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHeliosGuanXiaoYuTest::RunTest(const FString&)
{
    FTestWorldWrapper Wrapper;if(!Wrapper.CreateTestWorld(EWorldType::Game))return false;
    auto* World=Wrapper.GetTestWorld();World->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();if(!Wrapper.BeginPlayInTestWorld())return false;
    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Player=World->SpawnActor<AHeliosArenaCharacter>(FVector(0,0,100),FRotator::ZeroRotator,Spawn);
    auto* Controller=World->SpawnActor<APlayerController>();Controller->Possess(Player);Controller->SetControlRotation(FRotator::ZeroRotator);Player->GetCharacterMovement()->DisableMovement();
    // Advance the engine frame counter too: component ticks and TimerManager
    // intentionally run once per frame even in a synchronous test world.
    auto Step=[World](float Seconds,float FPS=60.f){for(float Left=Seconds;Left>UE_SMALL_NUMBER;){const float Dt=FMath::Min(Left,1/FPS);++GFrameCounter;World->Tick(LEVELTICK_All,Dt);Left-=Dt;}};
    auto* Character=FHeliosCharacterRegistry::FindCharacter(TEXT("cfm_guanxiaoyu"));if(!TestNotNull(TEXT("Registered Guan Xiao Yu"),Character))return false;
    TestEqual(TEXT("Three forms"),Character->Forms.Num(),3);TestEqual(TEXT("First selection defaults GR"),Character->DefaultVariantId,FName("GR"));
    auto Loadout=Player->GetLoadout();Loadout.CharacterId=TEXT("cfm_guanxiaoyu");Loadout.CharacterVariantId=TEXT("GR");
    for(FName Variant:{FName("GR"),FName("BL"),FName("SP")})
    {
        for(FName Weapon:{FName("cfm_chan_zeus"),FName("cfm_sunshangxiang"),FName("cfm_heilonghun")})
        {
            Loadout.CharacterVariantId=Variant;Loadout.PrimaryWeaponId=Weapon;
            TestTrue(TEXT("Form accepts ranged and melee primary"),Player->EquipLoadout(Loadout));
            TestTrue(TEXT("Selected form original kill voice pool"),Player->GetCharacterPresentation()->GetCharacterVoice()->GetKillPoolSize()==10);
            auto* Ability=Player->GetGuanXiaoYuAbility();auto* Primary=Player->GetEquippedWeapon();const int32 Ammo=Primary->GetBulletCount();
            TestTrue(TEXT("Exact umbrella mesh and clips load"),Ability->ToggleUmbrella());TestNotNull(TEXT("Selected umbrella visible mesh"),Ability->GetVisibleMesh());
            TestTrue(TEXT("Return primary"),Ability->ToggleUmbrella());TestTrue(TEXT("Same primary actor"),Primary==Player->GetEquippedWeapon());TestEqual(TEXT("Ammo retained"),Primary->GetBulletCount(),Ammo);
        }
    }
    Loadout.CharacterVariantId=TEXT("GR");Loadout.PrimaryWeaponId=TEXT("cfm_chan_zeus");TestTrue(TEXT("Test ranged loadout"),Player->EquipLoadout(Loadout));Step(3.2f);
    Player->StartFireFromAutomation();Player->StopFireFromAutomation();Step(.4f);Player->StartReloadFromAutomation();TestTrue(TEXT("Reload starts"),Player->IsReloading());TestFalse(TEXT("Reload rejects umbrella"),Player->GetGuanXiaoYuAbility()->ToggleUmbrella());Step(3.f);
    const auto SavedProfile=Character->Forms[0].UmbrellaProfile;Character->Forms[0].UmbrellaProfile=TSoftObjectPtr<UHeliosGuanXiaoYuUmbrellaProfile>();const auto* SavedPrimary=Player->GetEquippedWeapon();TestFalse(TEXT("Missing assets reject umbrella"),Player->GetGuanXiaoYuAbility()->ToggleUmbrella());TestTrue(TEXT("Failed load preserves current equipment"),Player->GetEquippedWeapon()==SavedPrimary&&!Player->GetGuanXiaoYuAbility()->IsUsingUmbrella()&&!Player->GetGuanXiaoYuAbility()->LastError.IsEmpty());Character->Forms[0].UmbrellaProfile=SavedProfile;
    auto* A=Player->GetGuanXiaoYuAbility();TestTrue(TEXT("Enter umbrella"),A->ToggleUmbrella());
    FVector Eye;FRotator View;Player->GetActorEyesViewPoint(Eye,View);
    auto* Enemy=World->SpawnActor<AHeliosEnemyE01>(Eye+FVector(200,0,0),FRotator::ZeroRotator,Spawn);Enemy->StopShooting();Enemy->SetActorTickEnabled(false);Enemy->GetCharacterMovement()->DisableMovement();if(Enemy->GetController())Enemy->GetController()->Destroy();
    A->ToggleUmbrella();Loadout.PrimaryWeaponId=TEXT("cfm_sunshangxiang");TestTrue(TEXT("Stage primary melee cancellation"),Player->EquipLoadout(Loadout));Step(2.f);Enemy->CurrentHP=10000;Player->StartFireFromAutomation();Player->StopFireFromAutomation();const float BeforeCancel=Enemy->CurrentHP;TestTrue(TEXT("Switch while primary attack is pending"),A->ToggleUmbrella());Step(1.5f);TestEqual(TEXT("Umbrella switch cancels pending primary damage"),Enemy->CurrentHP,BeforeCancel);A->ToggleUmbrella();Loadout.PrimaryWeaponId=TEXT("cfm_chan_zeus");Player->EquipLoadout(Loadout);A->ToggleUmbrella();
    auto Attack=[&](bool Heavy,float FPS=60.f){Enemy->CurrentHP=10000;const int32 Before=A->Hits;TestTrue(TEXT("Attack begins"),Heavy?A->StartHeavyAttack():A->StartLightAttack());A->StopLightAttack();Step(Heavy?1.4f:.7f,FPS);TestEqual(TEXT("Exactly one damage contact"),A->Hits-Before,1);TestEqual(TEXT("Configured damage applied"),10000-Enemy->CurrentHP,Heavy?120.f:60.f);};
    for(float FPS:{10.f,30.f,120.f}){Attack(false,FPS);Attack(true,FPS);}
    // Reject targets beyond the strict configured reach, behind the pawn and behind cover.
    for(FVector Position:{Eye+FVector(261,0,0),Eye-FVector(150,0,0)})
    {Enemy->SetActorLocation(Position);Enemy->CurrentHP=10000;A->StartLightAttack();A->StopLightAttack();Step(.7f);TestEqual(TEXT("Out of reach or rear target unchanged"),Enemy->CurrentHP,10000.f);}
    Enemy->SetActorLocation(Eye+FVector(200,0,0));auto* Wall=World->SpawnActor<AStaticMeshActor>(Eye+FVector(100,0,0),FRotator::ZeroRotator,Spawn);
    Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Wall->SetActorScale3D(FVector(.2f,3,3));Wall->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Wall->GetStaticMeshComponent()->SetCollisionResponseToAllChannels(ECR_Block);
    Enemy->CurrentHP=10000;A->StartLightAttack();A->StopLightAttack();Step(.7f);TestEqual(TEXT("Wall prevents melee damage"),Enemy->CurrentHP,10000.f);Wall->Destroy();
    auto* Friend=World->SpawnActor<AHeliosHumanoidPet>(Eye+FVector(130,30,0),FRotator::ZeroRotator,Spawn);Friend->CompanionOwner=Player;Friend->SetActorTickEnabled(false);Friend->GetCharacterMovement()->DisableMovement();
    const float FriendHP=Friend->Health;A->StartLightAttack();A->StopLightAttack();Step(.7f);TestEqual(TEXT("Friendly companion never damaged"),Friend->Health,FriendHP);Friend->Destroy();
    Step(10.1f);A->StartHeavyAttack();TestNotNull(TEXT("Original shield visual loaded"),A->GetShieldEffect());if(A->GetShieldEffect())TestTrue(TEXT("Shield has visible source tracks"),A->GetShieldEffect()->VisibleTrackCount()>0);const FDamageEvent Direct;
    TestEqual(TEXT("Zero damage preserves shield"),Player->TakeDamage(0,Direct,nullptr,Enemy),0.f);TestTrue(TEXT("Zero did not spend shield"),A->ShieldRemaining()>0);
    Player->SetCanBeDamaged(false);TestEqual(TEXT("Invulnerable damage ignored"),Player->TakeDamage(10,Direct,nullptr,Enemy),0.f);TestTrue(TEXT("Invulnerable did not spend shield"),A->ShieldRemaining()>0);Player->SetCanBeDamaged(true);
    Enemy->SetActorLocation(Player->GetActorLocation()+FVector(200,0,0));const float HP=Player->GetHealth();
    TestEqual(TEXT("First frontal direct attack blocked"),Player->TakeDamage(10,Direct,nullptr,Enemy),0.f);TestEqual(TEXT("Second frontal attack applied"),Player->TakeDamage(10,Direct,nullptr,Enemy),10.f);TestEqual(TEXT("Exactly one block"),Player->GetHealth(),HP-10);
    Step(10.1f);A->StartHeavyAttack();Enemy->SetActorLocation(Player->GetActorLocation()-FVector(200,0,0));TestEqual(TEXT("Rear damage applied"),Player->TakeDamage(10,Direct,nullptr,Enemy),10.f);TestTrue(TEXT("Rear damage keeps front shield"),A->ShieldRemaining()>0);
    Enemy->SetActorLocation(Player->GetActorLocation()+FRotator(0,61,0).Vector()*200);TestEqual(TEXT("Outside 120 degree shield arc hurts"),Player->TakeDamage(5,Direct,nullptr,Enemy),5.f);TestTrue(TEXT("Outside arc preserves shield"),A->ShieldRemaining()>0);
    Enemy->SetActorLocation(Player->GetActorLocation()+FVector(200,0,0));FRadialDamageEvent Radial;TestEqual(TEXT("Explosion applied"),Player->TakeDamage(10,Radial,nullptr,Enemy),10.f);TestTrue(TEXT("Explosion preserves shield"),A->ShieldRemaining()>0);
    auto* Hazard=World->SpawnActor<AActor>();Hazard->Tags.Add(TEXT("DamageOverTime"));TestEqual(TEXT("DOT applied"),Player->TakeDamage(5,Direct,nullptr,Hazard),5.f);TestTrue(TEXT("DOT preserves shield"),A->ShieldRemaining()>0);Hazard->Destroy();
    const float Recovery=A->AttackRecoveryRemaining(),Cooldown=A->CooldownRemaining();A->ToggleUmbrella();A->ToggleUmbrella();TestFalse(TEXT("Toggling cannot bypass recovery"),A->StartHeavyAttack());TestTrue(TEXT("Recovery retained"),FMath::IsNearlyEqual(Recovery,A->AttackRecoveryRemaining(),.001f));TestTrue(TEXT("Cooldown retained"),FMath::IsNearlyEqual(Cooldown,A->CooldownRemaining(),.001f));
    Loadout.CharacterVariantId=TEXT("BL");TestTrue(TEXT("Change form"),Player->EquipLoadout(Loadout));TestFalse(TEXT("Form change cancels umbrella"),A->IsUsingUmbrella());TestTrue(TEXT("Form change retains cooldown"),FMath::IsNearlyEqual(Cooldown,A->CooldownRemaining(),.001f));A->ToggleUmbrella();TestFalse(TEXT("Form change cannot bypass recovery"),A->StartHeavyAttack());
    const float BeforePause=A->CooldownRemaining();TestTrue(TEXT("Pause accepted"),Controller->SetPause(true));Step(1.f);TestTrue(TEXT("World paused"),World->IsPaused());TestTrue(TEXT("Paused clocks frozen"),FMath::IsNearlyEqual(BeforePause,A->CooldownRemaining(),.001f));Controller->SetPause(false);
    Step(10.1f);A->StartHeavyAttack();Step(2.05f);TestEqual(TEXT("Shield expires after 2 seconds"),A->ShieldRemaining(),0.f);TestTrue(TEXT("Shield remains on 10 second cooldown"),A->CooldownRemaining()>7.8f);
    // Independently owned component clocks and all companion appearances.
    for(FName Variant:{FName("GR"),FName("BL"),FName("SP")})
    {
        auto* Pet=World->SpawnActorDeferred<AHeliosHumanoidPet>(AHeliosHumanoidPet::StaticClass(),FTransform(FVector(0,700,100)),Player,Player,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);Loadout.CharacterVariantId=Variant;
        TestTrue(TEXT("Companion loadout stages"),Pet->ApplyLoadout(Loadout));Pet->CompanionOwner=Player;UGameplayStatics::FinishSpawningActor(Pet,FTransform(FVector(0,700,100)));Pet->SetActorTickEnabled(false);Pet->GetCharacterMovement()->DisableMovement();
        auto* Ability=Pet->GetGuanXiaoYuAbility();TestTrue(TEXT("Companion umbrella ready"),Ability->ToggleUmbrella());TestTrue(TEXT("Companion shield ready independently"),Ability->StartHeavyAttack());Enemy->SetActorLocation(Pet->GetActorLocation()+FVector(200,0,0));TestEqual(TEXT("Companion first frontal block"),Pet->TakeDamage(10,Direct,nullptr,Enemy),0.f);TestEqual(TEXT("Companion second attack hurts"),Pet->TakeDamage(10,Direct,nullptr,Enemy),10.f);
        TestTrue(TEXT("Player cooldown remains independent"),A->CooldownRemaining()<8.f);Pet->TakeDamage(Pet->Health+1,Direct,nullptr,Enemy);TestFalse(TEXT("Downed companion clears umbrella"),Ability->IsUsingUmbrella());TestEqual(TEXT("Downed companion clears shield"),Ability->ShieldRemaining(),0.f);Pet->Destroy();
    }
    Enemy->SetActorLocation(Player->GetActorLocation()+FVector(200,0,0));Player->TakeDamage(Player->GetHealth()+1,Direct,nullptr,Enemy);TestFalse(TEXT("Death clears umbrella"),A->IsUsingUmbrella());TestEqual(TEXT("Death clears shield"),A->ShieldRemaining(),0.f);
    return true;
}
#endif
