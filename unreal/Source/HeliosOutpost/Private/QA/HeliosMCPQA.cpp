#include "MCP/HeliosCodexMCPSubsystem.h"
#include "MCP/HeliosMCPJson.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Control/HeliosAutonomyComponent.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "UI/HeliosAdventureHUD.h"
#include "Pets/HeliosPetDog.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Weapons/HeliosMechWeapon.h"
#include "Camera/CameraComponent.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Adventure/HeliosAdventurePickup.h"
#include "Town/HeliosTownSupply.h"
#include "World/HeliosMapDefinition.h"
#include "Town/HeliosTownSubsystem.h"
#include "Player/HeliosArenaCharacter.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Boss/HeliosEnemyBoss.h"
#include "Control/HeliosControlSubsystem.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/CoreDelegates.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "NavigationData.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
TArray<double> TownFrameTimes;
FDelegateHandle TownFrameSampleHandle;
void SampleTownFrameTime()
{
    const double Delta=FApp::GetDeltaTime();
    if(Delta>0 && FMath::IsFinite(Delta) && TownFrameTimes.Num()<1000000)TownFrameTimes.Add(Delta);
}
// All callers are inside the command-line-gated QA dispatcher. Navigation,
// capsule clearance and visibility use the same world geometry as real play.
bool FindBossQAPosition(UWorld* World, const FVector& GroundOrigin, const FVector& Preferred,
    float Distance, float Radius, float HalfHeight, AActor* IgnoreA, AActor* IgnoreB,
    FVector& OutPosition, bool bRequireLOS = true)
{
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if (!Nav) return false;
    FNavAgentProperties AgentProps;
    AgentProps.AgentRadius = Radius;
    AgentProps.AgentHeight = HalfHeight * 2.0f;
    const ANavigationData* NavData = Nav->GetNavDataForProps(AgentProps, GroundOrigin);
    if (!NavData) return false;
    for (int32 Index = 0; Index < 40; ++Index)
    {
        const float Angle = Index == 0 ? 0.0f : ((Index + 1) / 2) * 9.0f * (Index % 2 ? 1.0f : -1.0f);
        const FVector Direction = Preferred.RotateAngleAxis(Angle, FVector::UpVector).GetSafeNormal2D();
        FNavLocation Projected;
        if (!Nav->ProjectPointToNavigation(GroundOrigin + Direction * Distance, Projected, FVector(80,80,140), NavData)) continue;
        if (FMath::Abs(FVector::Dist2D(Projected.Location, GroundOrigin) - Distance) > FMath::Max(45.0f, Distance * .15f)
            || FMath::Abs(Projected.Location.Z - GroundOrigin.Z) > 70.0f) continue;
        const FVector Position = Projected.Location + FVector(0,0,HalfHeight + 3.0f);
        FCollisionQueryParams Query(SCENE_QUERY_STAT(HeliosBossQASpawn), false, IgnoreA);
        if (IgnoreB) Query.AddIgnoredActor(IgnoreB);
        if (World->OverlapBlockingTestByChannel(Position, FQuat::Identity, ECC_Pawn,
            FCollisionShape::MakeCapsule(Radius, HalfHeight), Query)) continue;
        FHitResult Hit;
        if (bRequireLOS && World->LineTraceSingleByChannel(Hit, GroundOrigin + FVector(0,0,100),
            Projected.Location + FVector(0,0,100), ECC_Visibility, Query)) continue;
        const FPathFindingQuery PathQuery(nullptr, *NavData, GroundOrigin, Projected.Location);
        const FPathFindingResult Path = Nav->FindPathSync(AgentProps, PathQuery);
        if (!Path.IsSuccessful() || !Path.Path.IsValid() || Path.Path->IsPartial()) continue;
        OutPosition = Position;
        return true;
    }
    return false;
}

void StopBossQABrain(AHeliosEnemyBoss* Boss)
{
    if (auto* AI = Cast<AAIController>(Boss->GetController()))
    {
        AI->StopMovement();
        if (AI->GetBrainComponent()) AI->GetBrainComponent()->StopLogic(TEXT("Boss MCP fixture"));
    }
    Boss->GetCharacterMovement()->StopMovementImmediately();
}
}
#endif

TSharedPtr<FJsonObject> UHeliosCodexMCPSubsystem::RunQA(const TSharedPtr<FJsonObject>& Params)
{
    using namespace HeliosMCP;
#if WITH_DEV_AUTOMATION_TESTS
    auto* World=GetWorld(); auto* P=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(World,0));
    if(!P) return Result(false,TEXT("actor_unavailable"));
    const FString Action=String(Params,TEXT("action"));
    auto Out=Result();
    if(Action==TEXT("status_panel"))
    {
        auto* PC=World->GetFirstPlayerController();
        auto* HUD=PC?Cast<AHeliosAdventureHUD>(PC->GetHUD()):nullptr;
        if(!HUD)return Result(false,TEXT("hud_unavailable"));
        const FString Mode=String(Params,TEXT("mode"));
        if(Mode==TEXT("prepare"))
        {
            P->SetCanBeDamaged(false);
            if(auto* D=AHeliosAdventureDirector::Find(World))D->SetActorTickEnabled(false);
            for(TActorIterator<AHeliosEnemyActor> It(World);It;++It)It->Destroy();
        }
        if(Mode==TEXT("low"))
        {
            P->SetCanBeDamaged(true);
            UGameplayStatics::ApplyDamage(P,FMath::Max(0.f,P->GetHealth()-P->GetMaxHealth()*.2f),PC,nullptr,UDamageType::StaticClass());
            P->SetCanBeDamaged(false);
        }
        if(Mode==TEXT("max_health"))
        {
            FindFProperty<FFloatProperty>(P->GetClass(),TEXT("MaxHealth"))->SetPropertyValue_InContainer(P,250.f);
            P->Heal(250.f);
        }
        if(Mode==TEXT("reload"))
        {
            P->UpdateWeaponHUD(0,P->GetMagazineSize());
            P->StartReloadFromAutomation();
        }
        const FString WeaponId=String(Params,TEXT("weapon_id"));
        if(!WeaponId.IsEmpty())
        {
            auto* Definition=UHeliosWeaponRegistry::FindWeapon(FName(*WeaponId));
            if(!Definition||!P->EquipWeaponDefinition(Definition))return Result(false,TEXT("invalid_weapon"));
        }
        if(Mode==TEXT("pause"))HUD->TogglePauseMenu();
        if(Mode==TEXT("pet_sword"))World->GetGameInstance()->GetSubsystem<UHeliosPetSession>()->Select(EHeliosPetKind::SwordDog);
        if(Mode==TEXT("pet_rifle"))World->GetGameInstance()->GetSubsystem<UHeliosPetSession>()->Select(EHeliosPetKind::UltimatumGR);
        if(auto* Pets=World->GetSubsystem<UHeliosPetWorld>();Pets&&IsValid(Pets->Pet))
        {
            auto* Pet=Pets->Pet.Get();
            if(Mode==TEXT("sleep")){Pet->Restore(0.f,17.f);Pet->SetActorTickEnabled(false);}
            if(Mode==TEXT("wake")){Pet->State=EHeliosPetState::Waking;Pet->SleepRemaining=0;Pet->Health=Pet->Settings.MaxHealth;}
            if(Mode==TEXT("fight")){Pet->SetActorTickEnabled(false);Pet->State=EHeliosPetState::Fighting;Pet->Health=Pet->Settings.MaxHealth*.2f;}
            if(Mode==TEXT("follow")){Pet->State=EHeliosPetState::Following;Pet->Health=Pet->Settings.MaxHealth;Pet->SetActorTickEnabled(true);}
            if(Mode==TEXT("zero_max")){Pet->SetActorTickEnabled(false);Pet->Settings.MaxHealth=0;}
            if(Mode==TEXT("remove")){Pet->Destroy();Pets->Pet=nullptr;}
        }
        const FString Resolution=String(Params,TEXT("resolution"));
        if(Resolution==TEXT("1280x720w")||Resolution==TEXT("1920x1080w")||Resolution==TEXT("2560x1080w"))PC->ConsoleCommand(TEXT("r.SetRes ")+Resolution);
        const FString Shot=String(Params,TEXT("shot"));
        if(!Shot.IsEmpty()&&Shot.Len()<64&&!Shot.Contains(TEXT("/"))&&!Shot.Contains(TEXT("\\"))&&!Shot.Contains(TEXT("..")))
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/StatusPanel")/(Shot+TEXT(".png")),true,false);
        const auto Data=HUD->GetStatusPanelData();
        Out->SetNumberField(TEXT("health"),Data.Health);Out->SetNumberField(TEXT("max_health"),Data.MaxHealth);
        Out->SetNumberField(TEXT("fraction"),FHeliosStatusPanelData::Fraction(Data.Health,Data.MaxHealth));
        Out->SetStringField(TEXT("weapon"),Data.WeaponName.ToString());Out->SetStringField(TEXT("resource"),Data.ResourceText.ToString());
        Out->SetBoolField(TEXT("reloading"),Data.bReloading);
        Out->SetBoolField(TEXT("has_pet"),Data.bHasPet);Out->SetStringField(TEXT("pet"),Data.PetName.ToString());
        Out->SetStringField(TEXT("activity"),Data.PetActivity.ToString());Out->SetNumberField(TEXT("pet_health"),Data.PetHealth);
        Out->SetNumberField(TEXT("pet_fraction"),FHeliosStatusPanelData::Fraction(Data.PetHealth,Data.PetMaxHealth));
        Out->SetBoolField(TEXT("menu_visible"),HUD->GetMenuState()!=EHeliosAdventureMenu::None);
        return Out;
    }
    if(Action==TEXT("mech_probe")||Action==TEXT("mech_checkpoint")||Action==TEXT("mech_equip")||Action==TEXT("mech_secondary"))
    {
        auto* Town=World->GetSubsystem<UHeliosTownSubsystem>();
        if(Action==TEXT("mech_checkpoint"))
        {
            if(!Town||!Town->IsTownWorld()||!FParse::Param(FCommandLine::Get(),TEXT("HeliosTownQA")))return Result(false,TEXT("isolated_town_required"));
            return Result(Town->TeleportToCheckpoint(String(Params,TEXT("zone"))));
        }
        if(Action==TEXT("mech_equip"))return Result(P->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(FName(*String(Params,TEXT("weapon"))))));
        if(Action==TEXT("mech_secondary"))
        {
            auto* Mech=Cast<AHeliosMechWeapon>(P->GetEquippedWeapon());if(!Mech)return Result(false,TEXT("mech_required"));
            bool Held=false;Params->TryGetBoolField(TEXT("held"),Held);if(Held)Mech->StartSecondaryFire();else Mech->StopSecondaryFire();return Out;
        }
        const auto* W=Cast<AHeliosMechWeapon>(P->GetEquippedWeapon());
        Out->SetBoolField(TEXT("mech"),P->IsMechMode());Out->SetBoolField(TEXT("grounded"),P->GetCharacterMovement()->IsMovingOnGround());
        Out->SetArrayField(TEXT("feet"),Vector(P->GetGroundReference()));Out->SetArrayField(TEXT("reference"),Vector(P->GetGameplayReference()));
        Out->SetNumberField(TEXT("height"),P->FirstPersonCamera->GetComponentLocation().Z-P->GetGroundReference().Z);
        Out->SetNumberField(TEXT("steps"),W?W->MechArt->GetFootstepCount():0);Out->SetNumberField(TEXT("ammo"),P->GetAmmo());
        Out->SetNumberField(TEXT("state"),W?static_cast<int32>(W->GetState()):0);Out->SetNumberField(TEXT("heat"),W?W->GetHeat():0);
        Out->SetStringField(TEXT("deployment_error"),P->GetMechDeploymentError());
        Out->SetNumberField(TEXT("movement_mode"),P->GetCharacterMovement()->MovementMode);
        float Best=-1;FVector Direction=FVector::ForwardVector;
        FCollisionQueryParams Q(SCENE_QUERY_STAT(MechWalkProbe),false,P);Q.AddIgnoredActor(P->GetEquippedWeapon());
        auto* Capsule=P->GetCapsuleComponent();
        for(int32 I=0;I<16;++I)
        {
            const FVector D=FVector::ForwardVector.RotateAngleAxis(I*22.5f,FVector::UpVector);FHitResult Hit;
            const bool Blocked=World->SweepSingleByChannel(Hit,P->GetActorLocation()+FVector(0,0,5),P->GetActorLocation()+D*1600+FVector(0,0,5),FQuat::Identity,ECC_Pawn,
                FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()-5),Q);
            const float Distance=Blocked?Hit.Distance:1600;
            if(Distance>Best){Best=Distance;Direction=D;}
        }
        Out->SetNumberField(TEXT("clear_distance"),Best);Out->SetNumberField(TEXT("clear_yaw"),Direction.Rotation().Yaw);
        return Out;
    }
    if(Action.StartsWith(TEXT("village_")))
    {
        const auto* Map=FHeliosMapRegistry::ForWorld(World);
        auto* Town=World->GetSubsystem<UHeliosTownSubsystem>();
        if(!Map || Map->Id!=TEXT("ikun_ghost_village") || !Town || !FParse::Param(FCommandLine::Get(),TEXT("HeliosVillageQA")))
            return Result(false,TEXT("village_qa_required"));
        if(Action==TEXT("village_view"))
        {
            FVector Position,Target;
            if(!ReadVector(Params,TEXT("location_cm"),Position) || FMath::Abs(Position.X)>25000 || FMath::Abs(Position.Y)>20000 || Position.Z< -500 || Position.Z>15000)
                return Result(false,TEXT("invalid_village_position"));
            RevokeControl(TEXT("village_capture_setup"));
            Town->BeginStreamingArrival(Position);
            if(ReadVector(Params,TEXT("target_cm"),Target) && P->GetController())
                P->GetController()->SetControlRotation((Target-P->GetPawnViewLocation()).Rotation());
            return Out;
        }
        if(Action==TEXT("village_capture"))
        {
            const FString Name=String(Params,TEXT("name"));
            if(Name.IsEmpty() || Name.Len()>40)return Result(false,TEXT("invalid_capture_name"));
            for(TCHAR C:Name)if(!FChar::IsAlnum(C) && C!=TEXT('_'))return Result(false,TEXT("invalid_capture_name"));
            const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/ghost_village/shots"));
            IFileManager::Get().MakeDirectory(*Directory,true);
            FScreenshotRequest::RequestScreenshot(Directory/(Name+TEXT(".png")),false,false);
            Out->SetStringField(TEXT("file"),Directory/(Name+TEXT(".png")));return Out;
        }
        if(Action==TEXT("village_probe"))
        {
            FVector Target;auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
            Out->SetBoolField(TEXT("world_ready"),Town->IsWorldReady());
            Out->SetBoolField(TEXT("grounded"),P->GetCharacterMovement()->IsMovingOnGround());
            FHitResult GroundHit;FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(VillageProbe),false,P);
            const FVector Here=P->GetActorLocation();
            const bool HitFloor=World->LineTraceSingleByChannel(GroundHit,Here+FVector(0,0,800),Here-FVector(0,0,1000),ECC_Pawn,GroundParams);
            Out->SetBoolField(TEXT("floor_hit"),HitFloor);Out->SetArrayField(TEXT("floor_cm"),Vector(GroundHit.ImpactPoint));
            Out->SetStringField(TEXT("floor_component"),GetPathNameSafe(GroundHit.GetComponent()));
            Out->SetNumberField(TEXT("capsule_half_height"),P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
            FFindFloorResult Floor;P->GetCharacterMovement()->FindFloor(Here,Floor,false);
            Out->SetBoolField(TEXT("swept_floor"),Floor.IsWalkableFloor());Out->SetNumberField(TEXT("floor_distance"),Floor.GetDistanceToFloor());
            if(ReadVector(Params,TEXT("target_cm"),Target))
            {
                bool Boss=false;Params->TryGetBoolField(TEXT("boss"),Boss);
                const FNavAgentProperties AgentProps(Boss?44.f:38.f,Boss?250.f:192.f);
                const auto* AgentData=Nav?Nav->GetNavDataForProps(AgentProps):nullptr;
                FNavLocation Dest;const bool Projected=Nav&&AgentData&&Nav->ProjectPointToNavigation(Target,Dest,FVector(120,120,180),AgentData);
                Out->SetBoolField(TEXT("projected"),Projected);
                if(Projected)
                {
                    const auto* Data=AgentData;
                    FPathFindingQuery Query(P,*Data,P->GetNavAgentLocation(),Dest.Location);Query.SetAllowPartialPaths(false);
                    const auto Path=Nav->FindPathSync(Query);
                    Out->SetBoolField(TEXT("reachable"),Path.IsSuccessful()&&Path.Path.IsValid()&&!Path.Path->IsPartial());
                }
            }
            return Out;
        }
        return Result(false,TEXT("unknown_village_action"));
    }
    if(Action==TEXT("town_godmode"))
    {
        auto* Town=World->GetSubsystem<UHeliosTownSubsystem>();
        if(!Town||!Town->IsTownWorld())return Result(false,TEXT("town_unavailable"));
        if(!FParse::Param(FCommandLine::Get(),TEXT("HeliosTownQA")))return Result(false,TEXT("town_qa_isolated_save_required"));
        bool Enabled=true;Params->TryGetBoolField(TEXT("enabled"),Enabled);P->SetCanBeDamaged(!Enabled);
        Out->SetBoolField(TEXT("qa_isolated_save"),true);
        Out->SetBoolField(TEXT("invulnerable"),!P->CanBeDamaged());return Out;
    }
    if(Action==TEXT("town_frame_start")||Action==TEXT("town_frame_stop"))
    {
        if(TownFrameSampleHandle.IsValid()){FCoreDelegates::OnEndFrame.Remove(TownFrameSampleHandle);TownFrameSampleHandle.Reset();}
        if(Action==TEXT("town_frame_start"))
        {
            TownFrameTimes.Reset();TownFrameSampleHandle=FCoreDelegates::OnEndFrame.AddStatic(&SampleTownFrameTime);
            Out->SetBoolField(TEXT("sampling"),true);return Out;
        }
        double Total=0;for(double Delta:TownFrameTimes)Total+=Delta;
        TArray<double> Sorted=TownFrameTimes;Sorted.Sort([](double A,double B){return A>B;});
        const int32 LowCount=FMath::Max(1,FMath::CeilToInt(Sorted.Num()*.01));double LowTotal=0;
        for(int32 I=0;I<FMath::Min(LowCount,Sorted.Num());++I)LowTotal+=Sorted[I];
        Out->SetNumberField(TEXT("sample_count"),Sorted.Num());Out->SetNumberField(TEXT("sampled_seconds"),Total);
        Out->SetNumberField(TEXT("mean_fps"),Total>0?Sorted.Num()/Total:0);
        Out->SetNumberField(TEXT("one_percent_low_fps"),LowTotal>0?LowCount/LowTotal:0);
        Out->SetStringField(TEXT("method"),TEXT("Actual engine OnEndFrame FApp delta; 1% low is reciprocal of mean slowest 1% frame duration. Capture overhead included."));return Out;
    }
    if(Action==TEXT("town_medical_state")||Action==TEXT("town_damage"))
    {
        auto* Town=World->GetSubsystem<UHeliosTownSubsystem>();
        if(!Town||!Town->IsTownWorld()||!FParse::Param(FCommandLine::Get(),TEXT("HeliosTownQA")))return Result(false,TEXT("town_qa_isolated_save_required"));
        if(Action==TEXT("town_damage"))
        {
            const bool WasDamageable=P->CanBeDamaged();P->SetCanBeDamaged(true);
            UGameplayStatics::ApplyDamage(P,40.f,World->GetFirstPlayerController(),nullptr,UDamageType::StaticClass());P->SetCanBeDamaged(WasDamageable);
        }
        Out->SetNumberField(TEXT("health"),P->GetHealth());TArray<TSharedPtr<FJsonValue>> Pickups;
        for(TActorIterator<AHeliosAdventurePickup> It(World);It;++It)if(It->IsMedical())
        {
            auto Item=Object();Item->SetStringField(TEXT("id"),It->GetSourceMarkerId());Item->SetArrayField(TEXT("location_cm"),Vector(It->GetActorLocation()));Pickups.Add(MakeShared<FJsonValueObject>(Item));
        }
        for(TActorIterator<AHeliosTownSupply> It(World);It;++It)
        {
            auto Item=Object();Item->SetStringField(TEXT("id"),It->Tags.IsEmpty()?It->GetName():It->Tags[0].ToString());Item->SetArrayField(TEXT("location_cm"),Vector(It->GetActorLocation()));Pickups.Add(MakeShared<FJsonValueObject>(Item));
        }
        Out->SetArrayField(TEXT("pickups"),Pickups);return Out;
    }
    if(Action.StartsWith(TEXT("town_ui_"))||Action.StartsWith(TEXT("map_ui_")))
    {
        auto* PC=World->GetFirstPlayerController();auto* HUD=PC?Cast<AHeliosAdventureHUD>(PC->GetHUD()):nullptr;
        auto* Town=World->GetSubsystem<UHeliosTownSubsystem>();
        if(!HUD || (Action.StartsWith(TEXT("town_ui_"))&&(!Town||!Town->IsTownWorld())))return Result(false,TEXT("town_ui_unavailable"));
        if(Action==TEXT("town_ui_map"))HUD->ToggleTownMap();
        else if(Action==TEXT("town_ui_pause"))HUD->TogglePauseMenu();
        else if(Action==TEXT("town_ui_new_confirm"))HUD->RequestNewTown();
        else if(Action==TEXT("town_ui_cancel"))HUD->CancelRestartConfirmation();
        else if(Action==TEXT("town_ui_confirm")||Action==TEXT("map_ui_confirm"))
        {
            if(HUD->GetMenuState()!=EHeliosAdventureMenu::RestartConfirm)return Result(false,TEXT("confirmation_required"));
            if(Town&&Town->IsTownWorld()&&!FParse::Param(FCommandLine::Get(),TEXT("HeliosTownQA")))return Result(false,TEXT("town_qa_isolated_save_required"));
            HUD->ConfirmRestart();
        }
        else if(Action==TEXT("map_ui_selection")){if(auto* D=AHeliosAdventureDirector::Find(World))D->EnterWeaponSelection();}
        else if(Action==TEXT("map_ui_select")){HUD->SelectMap(FName(*String(Params,TEXT("map_id"))));HUD->SelectWeapon(FName(*String(Params,TEXT("weapon_id"))));}
        else if(Action==TEXT("map_ui_start"))HUD->StartSelectedRun();
        else if(Action==TEXT("map_ui_restart"))HUD->RequestRestartConfirmation();
        else if(Action==TEXT("map_ui_cancel"))HUD->CancelRestartConfirmation();
        else if(Action==TEXT("map_ui_quit"))HUD->QuitToDesktop();
        else if(Action!=TEXT("town_ui_state")&&Action!=TEXT("map_ui_state"))return Result(false,TEXT("unknown_qa_action"));
        FString Menu;
        switch(HUD->GetMenuState())
        {
        case EHeliosAdventureMenu::None:Menu=TEXT("none");break;
        case EHeliosAdventureMenu::Pause:Menu=TEXT("pause");break;
        case EHeliosAdventureMenu::RestartConfirm:Menu=TEXT("restart_confirm");break;
        case EHeliosAdventureMenu::Death:Menu=TEXT("death");break;
        case EHeliosAdventureMenu::WeaponSelect:Menu=TEXT("weapon_selection");break;
        case EHeliosAdventureMenu::TownMap:Menu=TEXT("town_map");break;
        }
        Out->SetStringField(TEXT("menu"),Menu);Out->SetBoolField(TEXT("paused"),World->IsPaused());
        Out->SetBoolField(TEXT("cursor"),PC->bShowMouseCursor);Out->SetStringField(TEXT("pending_weapon"),HUD->GetPendingWeaponId().ToString());
        Out->SetStringField(TEXT("pending_map"),HUD->GetPendingMapId().ToString());
        Out->SetNumberField(TEXT("health"),P->GetHealth());Out->SetStringField(TEXT("weapon"),P->GetWeaponId().ToString());
        Out->SetNumberField(TEXT("explored"),Town&&Town->IsTownWorld()?Town->GetExploredDistrictCount():0);Out->SetNumberField(TEXT("memes"),Town&&Town->IsTownWorld()?Town->GetDiscoveredMemeCount():0);Out->SetNumberField(TEXT("bosses"),Town&&Town->IsTownWorld()?Town->GetDefeatedBossCount():0);
        // Keep the old progress-only alias for existing QA consumers; runtime combat is separate.
        Out->SetNumberField(TEXT("legacy_boss_progress"),Town&&Town->IsTownWorld()?Town->GetDefeatedBossCount():0);
        Out->SetStringField(TEXT("hud_run_text"),HUD->GetRunText().ToString());
        if(auto* Director=AHeliosAdventureDirector::Find(World))
        {
            Out->SetNumberField(TEXT("survival_seconds"),Director->GetSurvivalSeconds());
            Out->SetNumberField(TEXT("kills"),Director->GetKills());
            Out->SetNumberField(TEXT("alive_count"),Director->GetAliveCount());
            Out->SetNumberField(TEXT("enemy_cap"),Director->GetEnemyCap());
            Out->SetNumberField(TEXT("boss_count"),Director->GetBossCount());
            Out->SetNumberField(TEXT("boss_pending"),Director->GetPendingBossCount());
            Out->SetNumberField(TEXT("boss_next_seconds"),Director->GetSecondsUntilNextBoss());
        }
        return Out;
    }
    if (Action.StartsWith(TEXT("boss_")))
    {
        auto* Director = AHeliosAdventureDirector::Find(World);
        if (!Director) return Result(false,TEXT("director_unavailable"));
        auto Snapshot = [&]()
        {
            auto State = Result();
            State->SetNumberField(TEXT("player_health"),P->GetHealth());
            State->SetBoolField(TEXT("player_dead"),P->IsDead());
            State->SetArrayField(TEXT("player_location_cm"),Vector(P->GetActorLocation()));
            State->SetNumberField(TEXT("score"),Director->GetScore());
            State->SetNumberField(TEXT("boss_kills"),Director->GetBossKills());
            State->SetNumberField(TEXT("run_serial"),Director->GetRunSerial());
            State->SetBoolField(TEXT("paused"),UGameplayStatics::IsGamePaused(World));
            TArray<TSharedPtr<FJsonValue>> BossStates;
            for (AHeliosEnemyBoss* Boss : Director->Bosses)
            {
                if (!IsValid(Boss)) continue;
                auto B = MakeShared<FJsonObject>();
                B->SetStringField(TEXT("actor_id"),Boss->GetRuntimeActorId());
                B->SetNumberField(TEXT("number"),Boss->GetBossNumber());
                B->SetNumberField(TEXT("health"),Boss->GetHealth());
                B->SetNumberField(TEXT("phase"),Boss->GetBossPhase());
                B->SetBoolField(TEXT("dead"),Boss->IsEnemyDead());
                B->SetBoolField(TEXT("parked"),Boss->ActorHasTag(TEXT("HeliosMCPBossParked")));
                B->SetStringField(TEXT("attack"),StaticEnum<EHeliosBossAttackType>()->GetNameStringByValue(static_cast<int64>(Boss->GetBossAttackType())));
                B->SetNumberField(TEXT("casts"),Boss->GetBossCastSerial());
                B->SetNumberField(TEXT("hits"),Boss->GetAttacksCommitted());
                B->SetNumberField(TEXT("windup"),Boss->GetBossWindupProgress());
                B->SetBoolField(TEXT("releasing"),Boss->IsBossAttackReleasing());
                B->SetNumberField(TEXT("elapsed"),Boss->GetBossAttackElapsed());
                B->SetNumberField(TEXT("wave_radius_cm"),Boss->GetBossWaveRadius());
                B->SetArrayField(TEXT("location_cm"),Vector(Boss->GetActorLocation()));
                B->SetArrayField(TEXT("direction"),Vector(Boss->GetBossAttackDirection()));
                BossStates.Add(MakeShared<FJsonValueObject>(B));
            }
            State->SetArrayField(TEXT("bosses"),BossStates);
            return State;
        };
        if (Action == TEXT("boss_state")) return Snapshot();
        if (Action == TEXT("boss_setup"))
        {
            if (Director->IsSelectingWeapon() || Director->IsRunOver()) return Result(false,TEXT("qa_start_adventure_first"));
            double Distance=650, Phase=1, Count=2, Health=0;
            Params->TryGetNumberField(TEXT("distance_cm"),Distance); Params->TryGetNumberField(TEXT("phase"),Phase);
            Params->TryGetNumberField(TEXT("count"),Count); Params->TryGetNumberField(TEXT("health"),Health);
            Distance=FMath::Clamp(Distance,180.0,1200.0); Count=FMath::Clamp(Count,1.0,2.0);
            RevokeControl(TEXT("boss_qa_setup"));
            Director->SetRunPaused(false);
            Director->SetActorTickEnabled(true);
            // Keep UpdateBosses live for the normal +250/death accounting, but
            // postpone all automatic spawns until RestartRun resets these fields.
            Director->NextEnemyAttemptSeconds=MAX_flt; Director->NextBossEventSeconds=MAX_flt;
            Director->NextMedicalSeconds=MAX_flt; Director->NextCollectibleSeconds=MAX_flt;
            Director->PendingBossNumbers.Reset();
            for(TActorIterator<AHeliosEnemyActor> It(World);It;++It) It->Destroy();
            Director->Enemies.Reset(); Director->Bosses.Reset();
            Director->CountedDeaths.Reset(); Director->CountedBossDeaths.Reset();
            for(TActorIterator<AActor> It(World);It;++It) if(It->ActorHasTag(TEXT("HeliosMCPFixture"))) It->Destroy();
            P->CancelCombatAndControl(); P->ResetForTest();
            if(Autonomy.IsValid()) Autonomy->ResetMemory();
            const FVector Ground = P->GetActorLocation()-FVector(0,0,P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
            const FVector Forward = P->GetControlRotation().Vector().GetSafeNormal2D();
            const FHeliosEnemyDefinition BossDefinition = FHeliosEnemyDefinition::ForType(EHeliosEnemyType::E05_Boss);
            for(int32 Index=0;Index<static_cast<int32>(Count);++Index)
            {
                FVector Location;
                if(!FindBossQAPosition(World,Ground,Forward.RotateAngleAxis(Index*65.0f,FVector::UpVector),
                    static_cast<float>(Distance),BossDefinition.CapsuleRadius,BossDefinition.CapsuleHalfHeight,P,nullptr,Location)) return Result(false,TEXT("qa_no_safe_boss_spawn"));
                const int32 Number=++Director->BossEventSerial;
                const FTransform Transform(FRotator(0,(P->GetActorLocation()-Location).Rotation().Yaw,0),Location);
                auto* Boss=World->SpawnActorDeferred<AHeliosEnemyBoss>(AHeliosEnemyBoss::StaticClass(),Transform,Director,nullptr,
                    ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding);
                if(!Boss) return Result(false,TEXT("qa_boss_spawn_failed"));
                Boss->SetBossNumber(Number);
                Boss->SetRuntimeIdentity(FString::Printf(TEXT("adventure_boss_r%03d_b%03d_E05"),Director->GetRunSerial(),Number),TEXT("mcp_boss_fixture"));
                Boss->Tags.Add(TEXT("HeliosMCPFixture"));
                Boss->FinishSpawning(Transform);
                Boss->CurrentHP=Health>0 ? FMath::Clamp(static_cast<float>(Health),1.0f,Boss->GetBossMaxHealth())
                    : Boss->GetBossMaxHealth()*(Phase>=2 ? .45f : 1.0f);
                Director->Bosses.Add(Boss); ++Director->BossSpawnSerial;
                StopBossQABrain(Boss); Boss->StartShooting(P);
            }
            return Snapshot();
        }
        double Number=1; Params->TryGetNumberField(TEXT("boss_number"),Number);
        AHeliosEnemyBoss* Selected=nullptr;
        for(AHeliosEnemyBoss* Boss:Director->Bosses) if(IsValid(Boss)&&Boss->GetBossNumber()==static_cast<int32>(Number)) { Selected=Boss; break; }
        if(!Selected) return Result(false,TEXT("qa_boss_missing"));
        if(Action==TEXT("boss_damage"))
        {
            double Damage=1000; Params->TryGetNumberField(TEXT("damage"),Damage);
            UGameplayStatics::ApplyDamage(Selected,FMath::Clamp(static_cast<float>(Damage),0.0f,100000.0f),P->GetController(),P,UDamageType::StaticClass());
            Director->UpdateBosses();
            return Snapshot();
        }
        if(Action==TEXT("boss_reposition"))
        {
            if(Selected->IsEnemyDead()) return Result(false,TEXT("qa_boss_dead"));
            if(P->IsDead()||Director->IsRunOver()) return Result(false,TEXT("qa_start_adventure_first"));
            if(Selected->GetBossAttackType()!=EHeliosBossAttackType::None)
            {
                // An old cast can finish safely while its owner is parked >20m away.
                if(Selected->ActorHasTag(TEXT("HeliosMCPBossParked"))) Selected->SetActorTickEnabled(true);
                return Result(false,TEXT("qa_boss_busy"));
            }
            double Distance=220; Params->TryGetNumberField(TEXT("distance_cm"),Distance);
            Distance=FMath::Clamp(Distance,180.0,1300.0);
            const FVector Ground=Selected->GetActorLocation()-FVector(0,0,Selected->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
            FVector PlayerPosition;
            if(!FindBossQAPosition(World,Ground,Selected->GetActorForwardVector(),static_cast<float>(Distance),
                P->GetCapsuleComponent()->GetScaledCapsuleRadius(),P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),P,Selected,PlayerPosition))
                return Result(false,TEXT("qa_no_safe_player_position"));
            for(auto* Boss:Director->GetBossActors()) if(IsValid(Boss)&&Boss!=Selected&&!Boss->IsEnemyDead())
            {
                FVector ParkPosition;
                if(!FindBossQAPosition(World,Ground,-Selected->GetActorForwardVector(),2400,
                    Boss->GetCapsuleComponent()->GetScaledCapsuleRadius(),Boss->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),P,Boss,ParkPosition,false))
                    return Result(false,TEXT("qa_no_safe_parking_position"));
                StopBossQABrain(Boss); Boss->StopShooting(); Boss->SetActorTickEnabled(false);
                Boss->GetCharacterMovement()->DisableMovement();
                if(!Boss->TeleportTo(ParkPosition,Boss->GetActorRotation(),false,false))
                    return Result(false,TEXT("qa_boss_parking_failed"));
                Boss->Tags.AddUnique(TEXT("HeliosMCPBossParked"));
            }
            P->CancelCombatAndControl(); P->Heal(10000.0f);
            P->GetCharacterMovement()->StopMovementImmediately();
            if(!P->TeleportTo(PlayerPosition,FRotator::ZeroRotator,false,false)) return Result(false,TEXT("qa_player_teleport_failed"));
            P->GetController()->SetControlRotation((Selected->GetActorLocation()-PlayerPosition).Rotation());
            Selected->Tags.Remove(TEXT("HeliosMCPBossParked")); Selected->SetActorTickEnabled(true);
            Selected->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            StopBossQABrain(Selected);
            Selected->SetActorRotation(FRotator(0,(PlayerPosition-Selected->GetActorLocation()).Rotation().Yaw,0));
            Selected->StartShooting(P);
            return Snapshot();
        }
        return Result(false,TEXT("unknown_qa_action"));
    }
    if(Action==TEXT("prepare"))
    {
        RevokeControl(TEXT("qa_setup"));
        if(auto* Director=AHeliosAdventureDirector::Find(World)) Director->SetActorTickEnabled(false);
        for(TActorIterator<AHeliosEnemyActor> It(World);It;++It) It->Destroy();
        for(TActorIterator<AActor> It(World);It;++It) if(It->ActorHasTag(TEXT("HeliosMCPFixture"))) It->Destroy();
        P->CancelCombatAndControl(); P->ResetForTest();
        if(Autonomy.IsValid()) Autonomy->ResetMemory();
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
        FVector Origin=P->GetNavAgentLocation(); FNavLocation Goal; bool Found=false;
        FMath::RandInit(9182026);
        for(int I=0;Nav&&I<100;++I)
        {
            if(!Nav->GetRandomReachablePointInRadius(Origin,1800,Goal)||FVector::Dist2D(Goal.Location,Origin)<700||FMath::Abs(Goal.Location.Z-Origin.Z)>50) continue;
            FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(HeliosMCPFixture),true,P);
            if(!World->LineTraceSingleByChannel(Hit,Origin+FVector(0,0,130),Goal.Location+FVector(0,0,130),ECC_Visibility,Query)) { Found=true; break; }
        }
        if(!Found) return Result(false,TEXT("qa_no_clear_route"));
        const FVector Direction=(Goal.Location-Origin).GetSafeNormal2D();
        P->GetController()->SetControlRotation(Direction.Rotation());
        Out->SetArrayField(TEXT("origin_cm"),Vector(Origin)); Out->SetArrayField(TEXT("goal_cm"),Vector(Goal.Location));
        return Out;
    }
    if(Action==TEXT("spawn_target"))
    {
        FVector Location; if(!ReadVector(Params,TEXT("location_cm"),Location)) return Result(false,TEXT("invalid_params"));
        double HP=100; Params->TryGetNumberField(TEXT("health"),HP);
        const FTransform Transform(FRotator::ZeroRotator,Location+FVector(0,0,80));
        auto* Enemy=World->SpawnActorDeferred<AHeliosEnemyActor>(AHeliosEnemyActor::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Enemy->SetRuntimeIdentity(TEXT("mcp_fixture_target"),TEXT("mcp_fixture")); Enemy->Tags.Add(TEXT("HeliosMCPFixture"));
        Enemy->FinishSpawning(Transform); Enemy->CurrentHP=static_cast<float>(HP);
        if(auto* AI=Cast<AAIController>(Enemy->GetController())) { AI->StopMovement(); if(AI->GetBrainComponent()) AI->GetBrainComponent()->StopLogic(TEXT("MCP fixture")); }
        Enemy->StopShooting(); Enemy->GetCharacterMovement()->StopMovementImmediately(); Enemy->GetCharacterMovement()->DisableMovement(); Enemy->SetActorTickEnabled(false);
        Out->SetStringField(TEXT("actor_id"),Enemy->RuntimeActorId); Out->SetNumberField(TEXT("health"),Enemy->GetHealth()); return Out;
    }
    if(Action==TEXT("target_state"))
    {
        for(TActorIterator<AHeliosEnemyActor> It(World);It;++It) if(It->RuntimeActorId==TEXT("mcp_fixture_target"))
        {
            Out->SetNumberField(TEXT("health"),It->GetHealth()); Out->SetBoolField(TEXT("dead"),It->IsEnemyDead());
            Out->SetArrayField(TEXT("location_cm"),Vector(It->GetActorLocation())); return Out;
        }
        return Result(false,TEXT("target_missing"));
    }
    if(Action==TEXT("turn"))
    {
        double Yaw=0; Params->TryGetNumberField(TEXT("yaw"),Yaw);
        auto Rot=P->GetControlRotation(); Rot.Yaw=Yaw; Rot.Pitch=0; P->GetController()->SetControlRotation(Rot); return Out;
    }
    if(Action==TEXT("manual_input"))
    {
        if(auto* Control=World->GetSubsystem<UHeliosControlSubsystem>()) Control->NotifyManualInput(P); return Out;
    }
    if(Action==TEXT("pause")||Action==TEXT("unpause"))
    {
        if(auto* Director=AHeliosAdventureDirector::Find(World)) Director->SetRunPaused(Action==TEXT("pause")); return Out;
    }
    if(Action==TEXT("kill_player"))
    {
        UGameplayStatics::ApplyDamage(P,10000,nullptr,nullptr,UDamageType::StaticClass()); return Out;
    }
    if(Action==TEXT("wall")||Action==TEXT("remove_wall"))
    {
        const FString Name=String(Params,TEXT("name"));
        const FName NamedTag=FName(*(TEXT("HeliosMCPWall_")+Name));
        for(TActorIterator<AActor> It(World);It;++It)
            if(It->ActorHasTag(TEXT("HeliosMCPWall"))&&(Name.IsEmpty()||It->ActorHasTag(NamedTag))) It->Destroy();
        if(Action==TEXT("remove_wall")) return Out;
        FVector Location,Extent;
        if(!ReadVector(Params,TEXT("location_cm"),Location)||!ReadVector(Params,TEXT("extent_cm"),Extent)) return Result(false,TEXT("invalid_params"));
        auto* Wall=World->SpawnActor<AActor>(); Wall->Tags.Add(TEXT("HeliosMCPFixture")); Wall->Tags.Add(TEXT("HeliosMCPWall"));
        Wall->Tags.Add(NamedTag);
        auto* Box=NewObject<UBoxComponent>(Wall); Wall->SetRootComponent(Box); Wall->AddInstanceComponent(Box);
        Box->SetBoxExtent(Extent); Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Box->SetCollisionResponseToAllChannels(ECR_Block);
        Box->RegisterComponent(); Wall->SetActorLocation(Location);
        double Yaw=0; Params->TryGetNumberField(TEXT("yaw"),Yaw); Wall->SetActorRotation(FRotator(0,Yaw,0)); return Out;
    }
    if(Action==TEXT("exit")) { FPlatformMisc::RequestExit(false); return Out; }
    return Result(false,TEXT("unknown_qa_action"));
#else
    return HeliosMCP::Result(false,TEXT("qa_disabled"));
#endif
}
