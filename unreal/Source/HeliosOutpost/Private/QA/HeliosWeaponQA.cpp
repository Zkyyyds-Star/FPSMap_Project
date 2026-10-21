#include "QA/HeliosWeaponQA.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Adventure/HeliosAdventureCharacter.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "UI/HeliosAdventureHUD.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Weapons/HeliosCombatWeapons.h"
#include "Weapons/HeliosWeaponVFX.h"
#include "Control/HeliosControlSubsystem.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
const FName Ids[] = {TEXT("rifle"),TEXT("rocket"),TEXT("laser"),TEXT("shotgun"),TEXT("chopsticks")};
struct FWeaponDirectionCase
{
    const TCHAR* Label;
    FRotator Rotation;
};
const FWeaponDirectionCase DirectionCases[] = {
    {TEXT("yaw_0"), FRotator(0, 0, 0)},
    {TEXT("yaw_90"), FRotator(0, 90, 0)},
    {TEXT("yaw_180"), FRotator(0, 180, 0)},
    {TEXT("yaw_270"), FRotator(0, 270, 0)},
    {TEXT("pitch_30"), FRotator(30, 0, 0)},
    {TEXT("pitch_minus_30"), FRotator(-30, 0, 0)}
};
constexpr int32 DirectionCaseCount = UE_ARRAY_COUNT(DirectionCases);
}
AHeliosWeaponQA::AHeliosWeaponQA()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
}
void AHeliosWeaponQA::BeginPlay() { Super::BeginPlay(); Started=StageWall=FPlatformTime::Seconds(); }
void AHeliosWeaponQA::SetStage(int32 NewStage)
{
    Stage=NewStage; StageWall=FPlatformTime::Seconds(); StageGame=GetWorld()->GetTimeSeconds();
}
void AHeliosWeaponQA::Input(FName Action, EInputEvent Event)
{
    if (!Player || !Player->InputComponent) return;
    for (int32 I=0; I<Player->InputComponent->GetNumActionBindings(); ++I)
    {
        const FInputActionBinding& B=Player->InputComponent->GetActionBinding(I);
        if (B.GetActionName()==Action && B.KeyEvent==Event) B.ActionDelegate.Execute(EKeys::LeftMouseButton);
    }
}
void AHeliosWeaponQA::Check(const FString& Name,bool Passed,const FString& Detail)
{
    TSharedRef<FJsonObject> R=MakeShared<FJsonObject>();
    R->SetStringField(TEXT("name"),Name); R->SetBoolField(TEXT("passed"),Passed); R->SetStringField(TEXT("detail"),Detail);
    Checks.Add(MakeShared<FJsonValueObject>(R)); if (!Passed) ++Failures;
    UE_LOG(LogTemp,Display,TEXT("WEAPON_QA %s %s %s"),*Name,Passed?TEXT("PASS"):TEXT("FAIL"),*Detail);
}
void AHeliosWeaponQA::CleanupFixture()
{
    ObservedRocket=nullptr;
    if (IsValid(Target))
    {
        AController* C=Target->GetController();
        for (TActorIterator<AShooterWeapon> It(GetWorld());It;++It) if (It->GetOwner()==Target) It->Destroy();
        Target->Destroy(); if (C) C->Destroy();
    }
    Target=nullptr;
    if (IsValid(Wall)) Wall->Destroy(); Wall=nullptr;
}
AHeliosEnemyActor* AHeliosWeaponQA::SpawnTarget(FVector Location)
{
    FActorSpawnParameters P; P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AHeliosEnemyActor* E=GetWorld()->SpawnActor<AHeliosEnemyActor>(Location,FRotator::ZeroRotator,P);
    if (E)
    {
        E->StopShooting();
        if (AAIController* AI=Cast<AAIController>(E->GetController()))
        {
            if (AI->GetBrainComponent()) AI->GetBrainComponent()->StopLogic(TEXT("Weapon QA stationary target"));
            AI->StopMovement();
        }
        E->GetCharacterMovement()->DisableMovement(); E->SetActorTickEnabled(false);
    }
    return E;
}
AShooterWeapon* AHeliosWeaponQA::FindPlayerWeapon() const
{
    if (!GetWorld() || !Player) return nullptr;
    for (TActorIterator<AShooterWeapon> It(GetWorld()); It; ++It)
        if (It->GetOwner()==Player && !It->IsActorBeingDestroyed()) return *It;
    return nullptr;
}
void AHeliosWeaponQA::PrepareDirectionalRun(const FRotator& Rotation, bool bSpawnTarget)
{
    CleanupFixture();
    Player->SetActorLocation(Origin,false,nullptr,ETeleportType::TeleportPhysics);
    Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
    Player->GetController()->SetControlRotation(Rotation);
    ExpectedDirection=Rotation.Vector();
    if (bSpawnTarget)
    {
        const FVector ViewStart=Player->FirstPersonCamera->GetComponentLocation();
        Target=SpawnTarget(ViewStart+ExpectedDirection*450.0f);
        if (Target) Target->CurrentHP=1000.0f;
    }
}
void AHeliosWeaponQA::StartWeapon()
{
    CleanupFixture(); Director->EnterWeaponSelection();
    Check(Ids[WeaponIndex].ToString()+TEXT("_start"),Director->StartRun(Ids[WeaponIndex]));
    Check(TEXT("active_run_rejects_selection"),!Director->StartRun(TEXT("rifle")));
    Player->SetActorLocation(Origin,false,nullptr,ETeleportType::TeleportPhysics);
    Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
    Player->GetController()->SetControlRotation(FRotator::ZeroRotator);
    Target=SpawnTarget(Origin+FVector(450,0,50));
    Check(TEXT("target_created"),IsValid(Target));
    Check(Ids[WeaponIndex].ToString()+TEXT("_full_magazine"),Player->GetAmmo()==Player->GetMagazineSize());
    SetStage(2);
}
void AHeliosWeaponQA::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bFinished) return;
    if (FPlatformTime::Seconds()-Started>120) { Check(TEXT("timeout"),false); Finish(); return; }
    if (!Director) Director=AHeliosAdventureDirector::Find(GetWorld());
    if (!Player) Player=Cast<AHeliosAdventureCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));
    if (!Director || !Player) return;
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosExplosionDemo"))) { TickExplosionDemo(DeltaSeconds); return; }
    const float Age=GetWorld()->GetTimeSeconds()-StageGame;
    const double WallAge=FPlatformTime::Seconds()-StageWall;
    if ((Stage==2 || Stage==3 || Stage==13 || Stage==14) && IsValid(Target))
        Player->GetController()->SetControlRotation((Target->GetActorLocation()-Player->FirstPersonCamera->GetComponentLocation()).Rotation());
    const FString Id=Ids[FMath::Clamp(WeaponIndex,0,UE_ARRAY_COUNT(Ids)-1)].ToString();
    switch(Stage)
    {
    case 0:
        Check(TEXT("starts_in_selection"),Director->IsSelectingWeapon() && GetWorld()->IsPaused());
        Check(TEXT("registry_seven_weapons"),Director->GetAvailableWeapons().Num()==7 && UHeliosWeaponRegistry::FindWeapon(TEXT("cola")) && UHeliosWeaponRegistry::FindWeapon(TEXT("lightstick")));
        Check(TEXT("invalid_weapon_rejected"),!Director->StartRun(TEXT("missing_weapon")));
        if (UHeliosControlSubsystem* C=GetWorld()->GetSubsystem<UHeliosControlSubsystem>())
        {
            Check(TEXT("local_weapon_list"),C->DispatchLocalCommand(TEXT("get_available_weapons"),TEXT("player_01"),TEXT("{}")).Contains(TEXT("shotgun")));
            Check(TEXT("local_invalid_weapon"),C->DispatchLocalCommand(TEXT("start_adventure"),TEXT("player_01"),TEXT("{\"weapon_id\":\"missing\"}")).Contains(TEXT("invalid_weapon")));
        }
        HealthBefore=Player->GetHealth(); UGameplayStatics::ApplyDamage(Player,500,nullptr,this,UDamageType::StaticClass());
        Check(TEXT("selection_invulnerable"),Player->GetHealth()==HealthBefore);
        SetStage(1); break;
    case 1:
        if (WallAge<0.3) break;
        Check(TEXT("selection_no_spawn_or_time"),Director->GetAliveCount()==0 && Director->GetSurvivalSeconds()==0);
        StartWeapon(); break;
    case 2:
        if (Age<1.0f) break;
        AmmoBefore=Player->GetAmmo(); Input(TEXT("HeliosFire"),IE_Pressed);
        Check(Id+TEXT("_one_trigger_ammo"),Player->GetAmmo()==AmmoBefore-(WeaponIndex==2?2:1),FString::Printf(TEXT("before=%d after=%d"),AmmoBefore,Player->GetAmmo()));
        SetStage(3); break;
    case 3:
        if (IsValid(Target) && !Target->IsEnemyDead() && Age<8.0f) break;
        Check(Id+TEXT("_kills_enemy"),IsValid(Target) && Target->IsEnemyDead(),IsValid(Target)?FString::Printf(TEXT("hp=%f"),Target->GetHealth()):TEXT("target_missing"));
        Input(TEXT("HeliosFire"),IE_Released);
        Player->GetController()->SetControlRotation(FRotator(60,90,0));
        if (Player->GetAmmo()>0) Input(TEXT("HeliosFire"),IE_Pressed);
        SetStage(4); break;
    case 4:
        if (Player->GetAmmo()>0 && Age<10) break;
        Input(TEXT("HeliosFire"),IE_Released);
        Check(Id+TEXT("_empty"),Player->GetAmmo()==0);
        SetStage(5); break;
    case 5:
        if (Age<0.35) break;
        Check(Id+TEXT("_empty_waits_for_attack"),Player->GetAmmo()==0 && !Player->IsReloading());
        PrepareDirectionalRun(FRotator::ZeroRotator,true);
        HealthBefore=Target ? Target->GetHealth() : 0;
        Director->SetRunPaused(true);
        Input(TEXT("HeliosFire"),IE_Pressed);
        Player->StartFireFromAutomation();
        Check(Id+TEXT("_paused_empty_attack_blocked"),!Player->IsReloading() && Player->GetAmmo()==0);
        Input(TEXT("HeliosFire"),IE_Released);
        Director->SetRunPaused(false);
        Input(TEXT("HeliosFire"),IE_Pressed);
        Check(Id+TEXT("_empty_attack_reload_started"),Player->IsReloading() && Player->GetAmmo()==0);
        SetStage(6); break;
    case 6:
        if (Age<UHeliosWeaponRegistry::FindWeapon(Ids[WeaponIndex])->ReloadSeconds*0.5f) break;
        Check(Id+TEXT("_empty_reload_has_delay"),Player->IsReloading() && Player->GetAmmo()==0);
        Input(TEXT("HeliosFire"),IE_Released);
        Input(TEXT("HeliosFire"),IE_Pressed);
        Player->StartFireFromAutomation();
        // Keep the original start time to detect a restarted reload timer.
        Stage=7; break;
    case 7:
        if (Age<UHeliosWeaponRegistry::FindWeapon(Ids[WeaponIndex])->ReloadSeconds+0.25f) break;
        Check(Id+TEXT("_empty_reload_completed_without_refire"),!Player->IsReloading() && Player->GetAmmo()==Player->GetMagazineSize());
        Check(Id+TEXT("_reload_blocks_damage"),IsValid(Target) && Target->GetHealth()==HealthBefore);
        Input(TEXT("HeliosFire"),IE_Released);
        Input(TEXT("HeliosFire"),IE_Pressed);
        Input(TEXT("HeliosFire"),IE_Released);
        Check(Id+TEXT("_fresh_attack_after_reload"),Player->GetAmmo()<Player->GetMagazineSize());
        SetStage(8); break;
    case 8:
        if (Age<0.3f) break;
        if (Player->GetAmmo()==0)
        {
            Player->StartFireFromAutomation();
            Check(Id+TEXT("_automation_empty_attack_reload_started"),Player->IsReloading());
        }
        else
        {
            Input(TEXT("HeliosReload"),IE_Pressed);
            Check(Id+TEXT("_manual_reload_started"),Player->IsReloading());
        }
        SetStage(9); break;
    case 9:
        if (Age<UHeliosWeaponRegistry::FindWeapon(Ids[WeaponIndex])->ReloadSeconds+0.25f) break;
        Check(Id+TEXT("_second_reload_completed"),!Player->IsReloading() && Player->GetAmmo()==Player->GetMagazineSize());
        if (WeaponIndex+1<UE_ARRAY_COUNT(Ids)) { ++WeaponIndex; StartWeapon(); }
        else
        {
            Input(TEXT("HeliosFire"),IE_Pressed);
            Input(TEXT("HeliosFire"),IE_Released);
            Input(TEXT("HeliosReload"),IE_Pressed);
            Check(TEXT("switch_fixture_reloading"),Player->IsReloading());
            Check(TEXT("equip_ammo_free_weapon"),Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("lightstick"))));
            Check(TEXT("switch_cancels_reload"),!Player->IsReloading());
            AmmoBefore=Player->GetAmmo();
            Input(TEXT("HeliosFire"),IE_Pressed);
            Check(TEXT("ammo_free_attack_does_not_reload"),!Player->IsReloading() && Player->GetAmmo()==AmmoBefore);
            Input(TEXT("HeliosFire"),IE_Released);
            Input(TEXT("HeliosReload"),IE_Pressed);
            Check(TEXT("ammo_free_manual_reload_ignored"),!Player->IsReloading());
            SetStage(31);
        }
        break;
    case 31:
        if (Age<UHeliosWeaponRegistry::FindWeapon(TEXT("chopsticks"))->ReloadSeconds+0.25f) break;
        Check(TEXT("switch_has_no_pending_reload"),!Player->IsReloading() && Player->GetAmmo()==AmmoBefore);
        WeaponIndex=1; StartWeapon(); SetStage(10);
        break;
    case 10:
        if (Age<1) break;
        CleanupFixture();
        Target=SpawnTarget(Origin+FVector(700,0,50));
        {
            AStaticMeshActor* Block=GetWorld()->SpawnActor<AStaticMeshActor>(Origin+FVector(500,0,50),FRotator::ZeroRotator);
            Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
            Block->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
            Block->SetActorScale3D(FVector(0.3f,5,5));
            Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll")); Wall=Block;
        }
        HealthBefore=Target->GetHealth(); Player->GetController()->SetControlRotation(FRotator::ZeroRotator);
        Input(TEXT("HeliosFire"),IE_Pressed); SetStage(11); break;
    case 11:
        if (Age<1.5) break;
        Input(TEXT("HeliosFire"),IE_Released);
        Check(TEXT("rocket_wall_occlusion"),Target->GetHealth()==HealthBefore);
        Check(TEXT("rocket_no_self_damage"),Player->GetHealth()==100);
        WeaponIndex=2; StartWeapon(); SetStage(12); break;
    case 12:
        if (Age<1) break;
        Target->CurrentHP=1000; Input(TEXT("HeliosFire"),IE_Pressed); SetStage(13); break;
    case 13:
        if (Age<0.4) break;
        Input(TEXT("HeliosFire"),IE_Released);
        HealthBefore=Target->GetHealth(); AmmoBefore=Player->GetAmmo(); SetStage(14); break;
    case 14:
        if (Age<0.4) break;
        Check(TEXT("laser_release_stops_damage_and_energy"),Target->GetHealth()==HealthBefore && Player->GetAmmo()==AmmoBefore);
        Input(TEXT("HeliosFire"),IE_Pressed); Director->SetRunPaused(true);
        HealthBefore=Target->GetHealth(); AmmoBefore=Player->GetAmmo(); SetStage(15); break;
    case 15:
        if (WallAge<0.3) break;
        Check(TEXT("pause_freezes_combat"),Target->GetHealth()==HealthBefore && Player->GetAmmo()==AmmoBefore);
        Director->SetRunPaused(false); SetStage(16); break;
    case 16:
        if (Age<0.4) break;
        Check(TEXT("laser_not_resumed_after_pause"),Target->GetHealth()==HealthBefore && Player->GetAmmo()==AmmoBefore);
        // Protection expires after three game seconds; wait before testing actual defeat.
        SetStage(17); break;
    case 17:
        if (Age<3.2) break;
        Input(TEXT("HeliosFire"),IE_Pressed);
        Input(TEXT("HeliosReload"),IE_Pressed);
        Check(TEXT("death_fixture_reloading"),Player->IsReloading());
        UGameplayStatics::ApplyDamage(Player,1000,nullptr,this,UDamageType::StaticClass());
        Check(TEXT("death_ends_run"),Player->IsDead() && Director->IsRunOver());
        Check(TEXT("death_cancels_reload"),!Player->IsReloading());
        Input(TEXT("HeliosFire"),IE_Pressed);
        Player->StartFireFromAutomation();
        Check(TEXT("dead_attack_does_not_reload"),!Player->IsReloading());
        HealthBefore=Target->GetHealth(); SetStage(18); break;
    case 18:
        if (WallAge<0.2) break;
        Check(TEXT("death_stops_laser"),Target->GetHealth()==HealthBefore);
        if (AHeliosAdventureHUD* HUD=Cast<AHeliosAdventureHUD>(UGameplayStatics::GetPlayerController(this,0)->GetHUD())) HUD->ConfirmRestart();
        else Director->EnterWeaponSelection();
        Check(TEXT("death_reopens_selection"),Director->IsSelectingWeapon());
        Director->StartRun(TEXT("shotgun"));
        CleanupFixture();
        for (int32 I=0; I<3; ++I) Director->RestartRun();
        SetStage(19); break;
    case 19:
        if (Age<0.3) break;
        {
            int32 Weapons=0, Rockets=0;
            for (TActorIterator<AShooterWeapon> It(GetWorld());It;++It) if (It->GetOwner()==Player && !It->IsActorBeingDestroyed()) ++Weapons;
            for (TActorIterator<AHeliosRocketProjectile> It(GetWorld());It;++It) if (!It->IsActorBeingDestroyed()) ++Rockets;
            Check(TEXT("three_restarts_single_weapon_no_rockets"),Weapons==1 && Rockets==0 && !Player->IsDead(),FString::Printf(TEXT("weapons=%d rockets=%d"),Weapons,Rockets));
            Check(TEXT("local_restart_keeps_weapon"),Player->GetWeaponId()==TEXT("shotgun"));
            UHeliosControlSubsystem* C=GetWorld()->GetSubsystem<UHeliosControlSubsystem>();
            FHeliosActorState S;
            Check(TEXT("local_get_state"),C && C->GetState(TEXT("player_01"),S).bSuccess);
            Check(TEXT("local_look"),C && C->Look(TEXT("player_01"),10,0).bSuccess);
            Check(TEXT("local_move"),C && C->Move(TEXT("player_01"),FVector2D(0,1),0.2f,false).bSuccess);
            Check(TEXT("local_stop"),C && C->Stop(TEXT("player_01")).bSuccess);
        }
        CleanupFixture(); Director->EnterWeaponSelection();
        Check(TEXT("rifle_orientation_start"),Director->StartRun(TEXT("rifle")));
        Player->SetActorLocation(Origin,false,nullptr,ETeleportType::TeleportPhysics);
        Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
        Player->GetController()->SetControlRotation(FRotator::ZeroRotator);
        SetStage(20); break;
    case 20:
        if (Age<0.15f) break;
        {
            AShooterWeapon* Rifle=FindPlayerWeapon();
            USkeletalMeshComponent* Mesh=Rifle ? Rifle->GetFirstPersonMesh() : nullptr;
            const bool bHasMuzzle=Mesh && Mesh->DoesSocketExist(TEXT("Muzzle"));
            const FVector CameraForward=Player->FirstPersonCamera->GetForwardVector();
            const FVector MuzzleForward=bHasMuzzle
                ? Mesh->GetSocketRotation(TEXT("Muzzle")).Vector() : FVector::ZeroVector;
            const float Alignment=FVector::DotProduct(CameraForward,MuzzleForward);
            Check(TEXT("rifle_saved_muzzle_forward_matches_camera"),bHasMuzzle && Alignment>0.98f,
                FString::Printf(TEXT("socket=%s dot=%.4f camera=%s muzzle=%s"),
                    bHasMuzzle?TEXT("true"):TEXT("false"),Alignment,
                    *CameraForward.ToCompactString(),*MuzzleForward.ToCompactString()));
        }
        CleanupFixture(); Director->EnterWeaponSelection();
        Check(TEXT("rocket_direction_runtime_start"),Director->StartRun(TEXT("rocket")));
        DirectionIndex=0; PrepareDirectionalRun(DirectionCases[DirectionIndex].Rotation,false);
        SetStage(21); break;
    case 21:
        if (Age<0.15f) break;
        Input(TEXT("HeliosFire"),IE_Pressed); Input(TEXT("HeliosFire"),IE_Released);
        for (TActorIterator<AHeliosRocketProjectile> It(GetWorld());It;++It)
            if (It->GetOwner()==Player && !It->IsActorBeingDestroyed()) { ObservedRocket=*It; break; }
        Check(FString::Printf(TEXT("rocket_%s_spawned"),DirectionCases[DirectionIndex].Label),
            IsValid(ObservedRocket));
        if (IsValid(ObservedRocket))
        {
            RocketStart=ObservedRocket->GetActorLocation();
            const auto* Movement=ObservedRocket->FindComponentByClass<UProjectileMovementComponent>();
            Check(TEXT("rocket_boost_speed_100_mps"),Movement && FMath::IsNearlyEqual(Movement->Velocity.Size(),10000.0f,1.0f));
        }
        SetStage(22); break;
    case 22:
        if (Age<0.12f) break;
        {
            const FVector Displacement=IsValid(ObservedRocket)
                ? ObservedRocket->GetActorLocation()-RocketStart : FVector::ZeroVector;
            const float Distance=Displacement.Size();
            const float Alignment=FVector::DotProduct(
                Displacement.GetSafeNormal(),ExpectedDirection.GetSafeNormal());
            Check(FString::Printf(TEXT("rocket_%s_world_flight"),DirectionCases[DirectionIndex].Label),
                IsValid(ObservedRocket) && Distance>20.0f && Alignment>0.98f,
                FString::Printf(TEXT("distance=%.2f dot=%.4f expected=%s actual=%s"),Distance,Alignment,
                    *ExpectedDirection.ToCompactString(),*Displacement.GetSafeNormal().ToCompactString()));
        }
        if (++DirectionIndex<DirectionCaseCount)
        {
            Director->RestartRun();
            PrepareDirectionalRun(DirectionCases[DirectionIndex].Rotation,false);
            SetStage(21);
        }
        else
        {
            CleanupFixture(); Director->EnterWeaponSelection();
            Check(TEXT("shotgun_direction_runtime_start"),Director->StartRun(TEXT("shotgun")));
            DirectionIndex=0; PrepareDirectionalRun(DirectionCases[DirectionIndex].Rotation,true);
            Check(TEXT("shotgun_direction_target_created"),IsValid(Target));
            SetStage(23);
        }
        break;
    case 23:
        if (Age<0.15f) break;
        HealthBefore=IsValid(Target)?Target->GetHealth():0.0f; AmmoBefore=Player->GetAmmo();
        Input(TEXT("HeliosFire"),IE_Pressed); Input(TEXT("HeliosFire"),IE_Released);
        SetStage(24); break;
    case 24:
        if (Age<0.12f) break;
        Check(FString::Printf(TEXT("shotgun_%s_real_hit"),DirectionCases[DirectionIndex].Label),
            IsValid(Target) && Target->GetHealth()<HealthBefore && Player->GetAmmo()==AmmoBefore-1,
            IsValid(Target)?FString::Printf(TEXT("before=%.1f after=%.1f ammo=%d->%d"),
                HealthBefore,Target->GetHealth(),AmmoBefore,Player->GetAmmo()):TEXT("target_missing"));
        if (++DirectionIndex<DirectionCaseCount)
        {
            Director->RestartRun();
            PrepareDirectionalRun(DirectionCases[DirectionIndex].Rotation,true);
            SetStage(23);
        }
        else
        {
            Director->RestartRun(); PrepareDirectionalRun(FRotator::ZeroRotator,false);
            const FVector ViewStart=Player->FirstPersonCamera->GetComponentLocation();
            Target=SpawnTarget(ViewStart+FVector::ForwardVector*300.0f);
            if (Target) Target->CurrentHP=1000.0f;
            AStaticMeshActor* Block=GetWorld()->SpawnActor<AStaticMeshActor>(
                ViewStart+FVector::ForwardVector*60.0f,FRotator::ZeroRotator);
            if (Block)
            {
                Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
                Block->GetStaticMeshComponent()->SetStaticMesh(
                    LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
                Block->SetActorScale3D(FVector(0.05f,3.0f,3.0f));
                Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll")); Wall=Block;
            }
            Check(TEXT("shotgun_near_wall_fixture_created"),IsValid(Target)&&IsValid(Wall));
            SetStage(25);
        }
        break;
    case 25:
        if (Age<0.15f) break;
        HealthBefore=IsValid(Target)?Target->GetHealth():0.0f; AmmoBefore=Player->GetAmmo();
        Input(TEXT("HeliosFire"),IE_Pressed); Input(TEXT("HeliosFire"),IE_Released);
        SetStage(26); break;
    case 26:
        if (Age<0.12f) break;
        Check(TEXT("shotgun_near_wall_blocks_camera_to_muzzle_ray"),
            IsValid(Target) && Target->GetHealth()==HealthBefore && Player->GetAmmo()==AmmoBefore-1,
            IsValid(Target)?FString::Printf(TEXT("health=%.1f ammo=%d->%d"),
                Target->GetHealth(),AmmoBefore,Player->GetAmmo()):TEXT("target_missing"));
        CleanupFixture(); Director->EnterWeaponSelection();
        Director->StartRun(TEXT("rocket")); PrepareDirectionalRun(FRotator::ZeroRotator,false);
        {
            const FVector ViewStart=Player->FirstPersonCamera->GetComponentLocation();
            Target=SpawnTarget(ViewStart+FVector::ForwardVector*300.0f);
            if (Target) Target->CurrentHP=1000.0f;
            AStaticMeshActor* Block=GetWorld()->SpawnActor<AStaticMeshActor>(
                ViewStart+FVector::ForwardVector*60.0f,FRotator::ZeroRotator);
            if (Block)
            {
                Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
                Block->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
                Block->SetActorScale3D(FVector(0.05f,3.0f,3.0f));
                Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll")); Wall=Block;
            }
        }
        SetStage(27); break;
    case 27:
        if (Age<0.15f) break;
        HealthBefore=IsValid(Target)?Target->GetHealth():0.0f;
        Input(TEXT("HeliosFire"),IE_Pressed); Input(TEXT("HeliosFire"),IE_Released);
        SetStage(28); break;
    case 28:
        if (Age<0.15f) break;
        {
            int32 ExplosionCount=0, RocketCount=0;
            for(TActorIterator<AHeliosExplosionEffect> It(GetWorld());It;++It) if(!It->IsActorBeingDestroyed()) ++ExplosionCount;
            for(TActorIterator<AHeliosRocketProjectile> It(GetWorld());It;++It) if(!It->IsActorBeingDestroyed()) ++RocketCount;
            Check(TEXT("rocket_near_wall_single_explosion_no_leak"),
                IsValid(Target)&&Target->GetHealth()==HealthBefore&&Player->GetHealth()==100&&Player->GetAmmo()==0&&ExplosionCount==1&&RocketCount==0,
                FString::Printf(TEXT("explosions=%d rockets=%d ammo=%d target_hp=%.1f"),ExplosionCount,RocketCount,Player->GetAmmo(),Target?Target->GetHealth():-1));
        }
        Director->RestartRun();
        Check(TEXT("restart_clears_all_tracked_weapon_effects"),UHeliosWeaponVFX::GetTrackedCount(this)==0);
        CleanupFixture();
        PrepareDirectionalRun(FRotator::ZeroRotator,false);
        {
            const FVector View=Player->FirstPersonCamera->GetComponentLocation();
            Target=SpawnTarget(View+FVector(850.0f,1000.0f,0.0f));
            if(Target) Target->CurrentHP=1000.0f;
            auto* Block=GetWorld()->SpawnActor<AStaticMeshActor>(View+FVector(1000.0f,0,0),FRotator::ZeroRotator);
            if(Block)
            {
                Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
                Block->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
                Block->SetActorScale3D(FVector(0.05f,3.0f,3.0f));
                Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll")); Wall=Block;
            }
        }
        SetStage(29); break;
    case 29:
        if(Age<0.15f) break;
        Input(TEXT("HeliosFire"),IE_Pressed); Input(TEXT("HeliosFire"),IE_Released);
        SetStage(30); break;
    case 30:
        if(Age<0.3f) break;
        Check(TEXT("rocket_boost_blast_hits_at_10m_with_scaled_damage"),
            IsValid(Target) && Target->GetHealth()>770.0f && Target->GetHealth()<810.0f,
            FString::Printf(TEXT("target_hp=%.1f expected_about_791"),Target?Target->GetHealth():-1));
        Check(TEXT("rocket_boost_expanded_blast_no_self_damage"),Player->GetHealth()==100);
        Finish(); break;
    }
}
void AHeliosWeaponQA::Finish()
{
    bFinished=true; CleanupFixture();
    TSharedRef<FJsonObject> R=MakeShared<FJsonObject>();
    R->SetStringField(TEXT("status"),Failures?TEXT("failed"):TEXT("passed"));
    R->SetBoolField(TEXT("instrumented_real_input_bindings_and_stationary_enemy_fixtures"),true);
    R->SetBoolField(TEXT("runtime_cardinal_pitch_weapon_directions"),true);
    R->SetArrayField(TEXT("checks"),Checks); R->SetNumberField(TEXT("failures"),Failures);
    R->SetNumberField(TEXT("wall_seconds"),FPlatformTime::Seconds()-Started);
    FString Out; FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Out));
    const FString Dir=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/weapons"));
    IFileManager::Get().MakeDirectory(*Dir,true);
    FFileHelper::SaveStringToFile(Out,*(Dir/TEXT("runtime_weapon_qa.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    FPlatformMisc::RequestExit(false);
}


void AHeliosWeaponQA::TickExplosionDemo(float DeltaSeconds)
{
    if(DemoFrame==-150)
    {
        Director->StartRun(TEXT("rocket"));
        Director->SetActorTickEnabled(false);
    }
    const int32 Area=FMath::Clamp(DemoFrame/180,0,2);
    const int32 Local=FMath::Max(DemoFrame,0)%180;
    if(DemoFrame==-150 || (DemoFrame>=0 && Local==0))
    {
        Director->RestartRun(); Director->SetActorTickEnabled(false);
        const FVector Positions[]={FVector(-6000,4800,100),FVector(-3000,1700,100),FVector(-6300,600,100)};
        const FVector Targets[]={FVector(-4100,4300,0),FVector(-1200,1400,100),FVector(-4300,-250,80)};
        Player->SetActorLocation(Positions[Area],false,nullptr,ETeleportType::TeleportPhysics);
        Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
        Player->GetController()->SetControlRotation((Targets[Area]-Player->FirstPersonCamera->GetComponentLocation()).Rotation());
    }
    if(DemoFrame>=0)
    {
        if(Local==35 || Local==130) {Input(TEXT("HeliosFire"),IE_Pressed); Input(TEXT("HeliosFire"),IE_Released);}
        if(Local==55) Input(TEXT("HeliosReload"),IE_Pressed);
        const FString Folder=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/weapons/explosion_frames"));
        IFileManager::Get().MakeDirectory(*Folder,true);
        FScreenshotRequest::RequestScreenshot(Folder/FString::Printf(TEXT("frame_%04d.png"),DemoFrame),true,false);
    }
    ++DemoFrame;
    if(DemoFrame>=540)
    {
        Director->RestartRun();
        Check(TEXT("explosion_demo_restart_clears_effects"),UHeliosWeaponVFX::GetTrackedCount(this)==0);
        Finish();
    }
}
