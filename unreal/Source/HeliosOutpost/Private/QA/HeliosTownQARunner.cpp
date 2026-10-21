#include "QA/HeliosTownQARunner.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "Adventure/HeliosAdventureCharacter.h"
#include "Town/HeliosTownSubsystem.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Guid.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"

namespace
{
FVector QAReadVector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
{
    const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
    if (!Object->TryGetArrayField(Field, A) || A->Num()!=3) return FVector::ZeroVector;
    return FVector((*A)[0]->AsNumber(),(*A)[1]->AsNumber(),(*A)[2]->AsNumber())*100.;
}
FString ReportDirectory() { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("IkunTownQA")); }
void DescribeHit(const TSharedRef<FJsonObject>& Json, const FString& Prefix, const FHitResult& Hit)
{
    Json->SetStringField(Prefix+TEXT("_actor"),GetNameSafe(Hit.GetActor()));
    Json->SetStringField(Prefix+TEXT("_component"),GetNameSafe(Hit.GetComponent()));
    Json->SetNumberField(Prefix+TEXT("_instance"),Hit.Item);
    Json->SetStringField(Prefix+TEXT("_impact_cm"),Hit.ImpactPoint.ToString());
    Json->SetBoolField(Prefix+TEXT("_start_penetrating"),Hit.bStartPenetrating);
    if (auto* Mesh=Cast<UStaticMeshComponent>(Hit.GetComponent())) Json->SetStringField(Prefix+TEXT("_mesh"),GetNameSafe(Mesh->GetStaticMesh()));
}
TSharedRef<FJsonObject> ReadinessDiagnostic(UWorld* World,AHeliosAdventureCharacter* Player,const FVector& Requested)
{
    auto Json=MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("player_cm"),Player->GetActorLocation().ToString());
    Json->SetStringField(TEXT("requested_cm"),Requested.ToString());
    Json->SetStringField(TEXT("velocity_cm_s"),Player->GetVelocity().ToString());
    Json->SetNumberField(TEXT("movement_mode"),static_cast<int32>(Player->GetCharacterMovement()->MovementMode));
    Json->SetBoolField(TEXT("paused"),World->IsPaused());
    Json->SetNumberField(TEXT("world_seconds"),World->GetTimeSeconds());
    Json->SetBoolField(TEXT("movement_floor_walkable"),Player->GetCharacterMovement()->CurrentFloor.IsWalkableFloor());
    Json->SetNumberField(TEXT("distance_to_request_cm"),FVector::Dist(Player->GetActorLocation(),Requested));
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World); FNavLocation Local,Wide;
    const bool LocalOk=Nav && Nav->ProjectPointToNavigation(Player->GetActorLocation(),Local,FVector(100,100,250));
    const bool WideOk=Nav && Nav->ProjectPointToNavigation(Player->GetActorLocation(),Wide,FVector(1000,1000,500));
    Json->SetBoolField(TEXT("nav_local"),LocalOk); Json->SetBoolField(TEXT("nav_wide_10m"),WideOk);
    if (LocalOk) Json->SetStringField(TEXT("nav_local_cm"),Local.Location.ToString());
    if (WideOk) Json->SetStringField(TEXT("nav_wide_cm"),Wide.Location.ToString());
    FCollisionQueryParams Params(SCENE_QUERY_STAT(TownQADiagnostic),false,Player); FHitResult Ground,Overlap;
    const FVector P=Player->GetActorLocation();
    const bool GroundOk=World->LineTraceSingleByChannel(Ground,P+FVector(0,0,100),P-FVector(0,0,300),ECC_Visibility,Params);
    Json->SetBoolField(TEXT("ground_hit"),GroundOk); DescribeHit(Json,TEXT("ground"),Ground);
    FHitResult PawnGround;
    const bool PawnGroundOK=World->LineTraceSingleByChannel(PawnGround,P+FVector(0,0,100),P-FVector(0,0,300),ECC_Pawn,Params);
    Json->SetBoolField(TEXT("pawn_ground_hit"),PawnGroundOK); DescribeHit(Json,TEXT("pawn_ground"),PawnGround);
    const auto* Capsule=Player->GetCapsuleComponent();
    FFindFloorResult CapsuleFloor; Player->GetCharacterMovement()->FindFloor(P,CapsuleFloor,false);
    Json->SetBoolField(TEXT("capsule_floor_walkable"),CapsuleFloor.IsWalkableFloor());
    Json->SetNumberField(TEXT("capsule_floor_distance_cm"),CapsuleFloor.GetDistanceToFloor());
    DescribeHit(Json,TEXT("capsule_floor"),CapsuleFloor.HitResult);
    const FVector Supported=P+FVector(0,0,2.5f-CapsuleFloor.GetDistanceToFloor());
    Json->SetStringField(TEXT("supported_cm"),Supported.ToString());
    TArray<FOverlapResult> SupportedOverlaps;
    World->OverlapMultiByProfile(SupportedOverlaps,Supported,FQuat::Identity,TEXT("Pawn"),
        FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()),Params);
    TArray<TSharedPtr<FJsonValue>> Blockers;
    for (const auto& Hit:SupportedOverlaps) if (Hit.bBlockingHit)
        Blockers.Add(MakeShared<FJsonValueString>(GetNameSafe(Hit.GetActor())+TEXT("/")+GetNameSafe(Hit.GetComponent())+TEXT("#")+FString::FromInt(Hit.ItemIndex)));
    Json->SetArrayField(TEXT("supported_blockers"),Blockers);
    const bool OverlapOk=World->SweepSingleByChannel(Overlap,P,P+FVector(0,0,.1),FQuat::Identity,ECC_Pawn,
        FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()),Params);
    Json->SetBoolField(TEXT("player_blocked"),OverlapOk); DescribeHit(Json,TEXT("overlap"),Overlap);
    if (auto* Town=World->GetSubsystem<UHeliosTownSubsystem>())
    {
        TSharedPtr<FJsonObject> State; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Town->GetStateJson()),State);
        if (State)
        {
            Json->SetBoolField(TEXT("nav_ready"),State->GetBoolField(TEXT("nav_ready"))); Json->SetBoolField(TEXT("streaming_ready"),State->GetBoolField(TEXT("streaming_ready")));
            Json->SetBoolField(TEXT("arrival_pending"),State->GetBoolField(TEXT("arrival_pending")));
            Json->SetNumberField(TEXT("arrival_stable_seconds"),State->GetNumberField(TEXT("arrival_stable_seconds")));
            Json->SetStringField(TEXT("arrival_block_reason"),State->GetStringField(TEXT("arrival_block_reason")));
        }
    }
    return Json;
}
FString JsonText(const TSharedRef<FJsonObject>& Json)
{
    FString Text; FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Text)); return Text;
}
}

AHeliosTownQARunner::AHeliosTownQARunner()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
}
void AHeliosTownQARunner::BeginPlay()
{
    Super::BeginPlay(); StartTime = LastTickTime = FPlatformTime::Seconds();
    RunId=FGuid::NewGuid().ToString();
    IFileManager::Get().MakeDirectory(*ReportDirectory(),true);
    auto Running=MakeShared<FJsonObject>(); Running->SetStringField(TEXT("status"),TEXT("running"));
    Running->SetStringField(TEXT("run_id"),RunId); Running->SetBoolField(TEXT("passed"),false);
    FFileHelper::SaveStringToFile(JsonText(Running),*FPaths::Combine(ReportDirectory(),TEXT("smoke_report.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("IkunTown/Data/town.json")))
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Config) || !Config)
    { Record(TEXT("configuration"),false,TEXT("town.json unavailable")); Finish(); return; }
    IFileManager::Get().MakeDirectory(*ReportDirectory(),true);
    for (const auto& Value : Config->GetArrayField(TEXT("zones")))
    {
        const auto O=Value->AsObject(); FStep S; S.Kind=TEXT("checkpoint"); S.Id=O->GetStringField(TEXT("id"));
        S.Position=QAReadVector(O,TEXT("checkpoint")); S.Target=QAReadVector(O,TEXT("center")); Steps.Add(S);
    }
    // Continuous combat is verified by run_survival_acceptance.py through normal movement and fire.
    if (FParse::Param(FCommandLine::Get(), TEXT("HeliosTownSpawnQA")))
    {
        for (int32 Attempt = 0; Attempt < 8; ++Attempt)
        {
            FStep Death; Death.Kind=TEXT("death"); Death.Id=FString::Printf(TEXT("random_zone_%d"),Attempt); Steps.Add(Death);
        }
        return;
    }
    for (const auto& Value : Config->GetArrayField(TEXT("interiors")))
    {
        const auto O=Value->AsObject(); FStep S; S.Kind=TEXT("room"); S.Id=O->GetStringField(TEXT("id"));
        S.Position=QAReadVector(O,TEXT("position")); S.Target=QAReadVector(O,TEXT("center")); Steps.Add(S);
    }
    for (const auto& Value : Config->GetArrayField(TEXT("memes")))
    {
        const auto O=Value->AsObject(); const FString Kind=O->GetStringField(TEXT("kind"));
        if (Kind==TEXT("static") || Kind==TEXT("landmark")) continue;
        FStep S; S.Kind=TEXT("interact"); S.Id=O->GetStringField(TEXT("id")); S.Target=QAReadVector(O,TEXT("position"));
        S.Position=S.Target+FVector(0,-230,-20); Steps.Add(S);
    }
    FStep Death; Death.Kind=TEXT("death"); Death.Id=TEXT("full_health_checkpoint"); Steps.Add(Death);
    FStep Save; Save.Kind=TEXT("save"); Save.Id=TEXT("atomic_roundtrip"); Steps.Add(Save);
}

void AHeliosTownQARunner::EndPlay(const EEndPlayReason::Type Reason)
{
    if (!bFinished)
    {
        bInterrupted=true;
        Record(TEXT("interrupted"),false,FString::Printf(TEXT("end_play=%d step=%d run=%s"),static_cast<int32>(Reason),StepIndex,*RunId));
        Finish(false);
    }
    Super::EndPlay(Reason);
}

void AHeliosTownQARunner::Record(const FString& Name, bool bPassed, const FString& Detail)
{
    auto O=MakeShared<FJsonObject>(); O->SetStringField(TEXT("test"),Name); O->SetBoolField(TEXT("passed"),bPassed); O->SetStringField(TEXT("detail"),Detail);
    Results.Add(MakeShared<FJsonValueObject>(O)); if (!bPassed) ++Failures;
    UE_LOG(LogTemp,Display,TEXT("IKUN_TOWN_QA %s %s %s"),bPassed?TEXT("PASS"):TEXT("FAIL"),*Name,*Detail);
}

void AHeliosTownQARunner::NextStep()
{
    ++StepIndex; StepStart=FPlatformTime::Seconds(); bActionDone=false; bMovementReleased=false;
    RoomChecksStartedAt=-1.; bFloorLossLogged=false;
    if (!Steps.IsValidIndex(StepIndex)) { Finish(); return; }
    auto* Town=GetWorld()->GetSubsystem<UHeliosTownSubsystem>();
    auto* Player=Cast<AHeliosAdventureCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));
    const FStep& S=Steps[StepIndex];
    Player->SetCanBeDamaged(false);
    if (S.Kind==TEXT("checkpoint")) Town->TeleportToCheckpoint(S.Id);
    if (S.Kind==TEXT("checkpoint") || S.Kind==TEXT("room") || S.Kind==TEXT("boss") || S.Kind==TEXT("encounter") || S.Kind==TEXT("interact"))
    {
        Player->CancelCombatAndControl(); Town->BeginStreamingArrival(S.Position,true);
    }
    LastObservedPosition=Player->GetActorLocation(); LastObservedVelocity=Player->GetVelocity(); MinimumStepZ=LastObservedPosition.Z;
    LastMovementMode=static_cast<int32>(Player->GetCharacterMovement()->MovementMode);
    UE_LOG(LogTemp,Display,TEXT("IKUN_TOWN_QA STEP kind=%s id=%s target_cm=%s actual_cm=%s"),*S.Kind,*S.Id,*S.Position.ToString(),*LastObservedPosition.ToString());
    if (auto* PC=Cast<APlayerController>(Player->GetController()))
    {
        const FVector Direction=S.Target.IsNearlyZero()?FVector(0,1,0):(S.Target-Player->GetActorLocation());
        PC->SetControlRotation(FRotator(-5.f,Direction.Rotation().Yaw,0.f));
    }
}

void AHeliosTownQARunner::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds); if (bFinished) return;
    const double Now=FPlatformTime::Seconds(), WallDelta=Now-LastTickTime; LastTickTime=Now;
    // Menus pause the director, streaming and physics. Do not turn that into QA failures
    // or teleport the frozen pawn through additional test destinations during a pause.
    if (GetWorld()->IsPaused())
    {
        StartTime+=WallDelta; StepStart+=WallDelta; MovementReleasedAt+=WallDelta; ActionStartedAt+=WallDelta;
        if (RoomChecksStartedAt>=0.) RoomChecksStartedAt+=WallDelta;
        return;
    }
    Frames.Add(DeltaSeconds);
    auto* Town=GetWorld()->GetSubsystem<UHeliosTownSubsystem>(); auto* Director=AHeliosAdventureDirector::Find(GetWorld());
    auto* Player=Cast<AHeliosAdventureCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));
    if (!Town || !Director || !Player) return;
    if (FPlatformTime::Seconds()-StartTime>1200.) { Record(TEXT("timeout"),false,TEXT("smoke run exceeded20minutes")); Finish(); return; }
    if (!bStarted)
    {
        if (!Town->IsWorldReady())
        {
            if (FPlatformTime::Seconds()-StartTime>90.) { Record(TEXT("initial_ready"),false,Town->GetStateJson()); Finish(); }
            return;
        }
        Town->ResetProgress(); Director->StartRun(TEXT("rifle")); bStarted=true;
        Record(TEXT("initial_ready"),true,Town->GetStateJson()); NextStep(); return;
    }
    if (!Steps.IsValidIndex(StepIndex)) return;
    const FStep& S=Steps[StepIndex]; const double Age=FPlatformTime::Seconds()-StepStart;
    const FVector Actual=Player->GetActorLocation(); MinimumStepZ=FMath::Min(MinimumStepZ,static_cast<float>(Actual.Z));
    const bool bArrivalStep=S.Kind==TEXT("checkpoint") || S.Kind==TEXT("room") || S.Kind==TEXT("boss") || S.Kind==TEXT("encounter") || S.Kind==TEXT("interact");
    if (bArrivalStep && !bFloorLossLogged && Actual.Z<S.Position.Z-150.f)
    {
        bFloorLossLogged=true;
        UE_LOG(LogTemp,Warning,TEXT("IKUN_TOWN_QA floor_lost kind=%s id=%s elapsed=%.3f released=%d detail=%s"),
            *S.Kind,*S.Id,Age,bMovementReleased,*JsonText(ReadinessDiagnostic(GetWorld(),Player,S.Position)));
    }
    if (bArrivalStep && FVector::Dist2D(Actual,S.Position)>300.f)
    {
        auto Detail=ReadinessDiagnostic(GetWorld(),Player,S.Position);
        Detail->SetStringField(TEXT("reason"),TEXT("player_left_requested_arrival"));
        Detail->SetStringField(TEXT("previous_position_cm"),LastObservedPosition.ToString());
        Detail->SetStringField(TEXT("previous_velocity_cm_s"),LastObservedVelocity.ToString());
        Detail->SetNumberField(TEXT("previous_movement_mode"),LastMovementMode);
        Detail->SetNumberField(TEXT("minimum_step_z_cm"),MinimumStepZ);
        Detail->SetBoolField(TEXT("qa_released_movement"),bMovementReleased);
        Record(S.Kind+TEXT("/")+S.Id,false,JsonText(Detail)); NextStep(); return;
    }
    LastObservedPosition=Actual; LastObservedVelocity=Player->GetVelocity();
    LastMovementMode=static_cast<int32>(Player->GetCharacterMovement()->MovementMode);
    if (Director->GetAliveCount()>Town->GetSurvivalEnemyCap() || Director->GetBossCount()>1)
    { Record(TEXT("enemy_cap"),false,Town->GetStateJson()); Finish(); return; }
    if (Age<2.0) return;
    if (S.Kind==TEXT("death"))
    {
        if (!bActionDone)
        {
            if (Age<4.) return;
            Player->SetCanBeDamaged(true); UGameplayStatics::ApplyDamage(Player,100000.f,nullptr,this,nullptr); bActionDone=true; return;
        }
        if (Age<9.) return;
        const bool Passed=!Player->IsDead() && Player->GetHealth()>=99.f && Town->IsWorldReady()
            && Player->GetCharacterMovement()->IsMovingOnGround() && Player->GetCharacterMovement()->CurrentFloor.IsWalkableFloor()
            && FVector::Dist(Player->GetActorLocation(),Town->GetCheckpointLocation())<300.f;
        if (!Passed && Age<45.) return;
        Record(S.Kind+TEXT("/")+S.Id,Passed,Town->GetStateJson()); NextStep(); return;
    }
    if (S.Kind==TEXT("save"))
    {
        FString Text; TSharedPtr<FJsonObject> Saved;
        const bool Passed=Town->SaveProgress() && FFileHelper::LoadFileToString(Text,*FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("SaveGames/IkunTownQA.v1.json")))
            && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Saved) && Saved && Saved->GetIntegerField(TEXT("version"))==1
            && Saved->GetStringField(TEXT("checkpoint_id"))==Town->GetCheckpointId()
            && Saved->GetArrayField(TEXT("completed_encounters")).Num()==Town->GetCompletedEncounterCount();
        Record(S.Kind+TEXT("/")+S.Id,Passed,TEXT("version/checkpoint/completed IDs compared with committed QA slot")); NextStep(); return;
    }
    if (!Town->IsWorldReady())
    {
        if (Age>45.) { Record(S.Kind+TEXT("/")+S.Id,false,JsonText(ReadinessDiagnostic(GetWorld(),Player,S.Position))); NextStep(); }
        return;
    }
    // Room target navigation/path/clearance is checked while the pawn remains frozen.
    // Only the shared arrival gate may release movement after stable Pawn collision and nav.
    if (S.Kind!=TEXT("room") && !bMovementReleased)
    {
        if (!Town->ReleaseArrivalMovement()) return;
        bMovementReleased=true; MovementReleasedAt=FPlatformTime::Seconds(); return;
    }
    if (S.Kind!=TEXT("room") && FPlatformTime::Seconds()-MovementReleasedAt<0.5) return;
    if (S.Kind!=TEXT("room") && (!Player->GetCharacterMovement()->IsMovingOnGround() || !Player->GetCharacterMovement()->CurrentFloor.IsWalkableFloor()))
    {
        if (Age>45.) { Record(S.Kind+TEXT("/")+S.Id,false,JsonText(ReadinessDiagnostic(GetWorld(),Player,S.Position))); NextStep(); }
        return;
    }
    if (S.Kind==TEXT("checkpoint"))
    {
        if (bActionDone) { if (Age>3.) NextStep(); return; }
        Record(S.Kind+TEXT("/")+S.Id,FVector::Dist(Player->GetActorLocation(),S.Position)<300.f,Town->GetStateJson());
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(ReportDirectory(),S.Id+TEXT(".png")),false,false); bActionDone=true; return;
    }
    if (S.Kind==TEXT("room"))
    {
        if (RoomChecksStartedAt<0.) RoomChecksStartedAt=FPlatformTime::Seconds();
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()); FNavLocation Entrance,Center;
        auto Detail=ReadinessDiagnostic(GetWorld(),Player,S.Position);
        const bool EntranceOK=Nav && Nav->ProjectPointToNavigation(S.Position,Entrance,FVector(80,80,240));
        const bool CenterOK=Nav && Nav->ProjectPointToNavigation(S.Target,Center,FVector(80,80,240));
        Detail->SetBoolField(TEXT("entrance_projected"),EntranceOK); Detail->SetBoolField(TEXT("center_projected"),CenterOK);
        Detail->SetStringField(TEXT("target_cm"),S.Target.ToString());
        bool Passed=EntranceOK && CenterOK;
        if (EntranceOK) Detail->SetStringField(TEXT("entrance_nav_cm"),Entrance.Location.ToString());
        if (CenterOK) Detail->SetStringField(TEXT("center_nav_cm"),Center.Location.ToString());
        if (EntranceOK && CenterOK)
        {
            auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Entrance.Location,Center.Location,Player);
            Passed=Path && Path->IsValid() && !Path->IsPartial();
            Detail->SetBoolField(TEXT("path_valid"),Path && Path->IsValid()); Detail->SetBoolField(TEXT("path_partial"),!Path || Path->IsPartial());
            TArray<TSharedPtr<FJsonValue>> Points;
            if (Path) for (const FVector& Point:Path->PathPoints) Points.Add(MakeShared<FJsonValueString>(Point.ToString()));
            Detail->SetArrayField(TEXT("path_points_cm"),Points);
            FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(TownQARoom),false,Player);
            float H=Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            float R=Player->GetCapsuleComponent()->GetScaledCapsuleRadius();
            Detail->SetNumberField(TEXT("player_capsule_radius_cm"),R);
            Detail->SetNumberField(TEXT("player_capsule_half_height_cm"),H);
            for (int32 Type=1;Type<=4;++Type)
            {
                const auto Definition=FHeliosEnemyDefinition::ForType(static_cast<EHeliosEnemyType>(Type));
                R=FMath::Max(R,Definition.CapsuleRadius); H=FMath::Max(H,Definition.CapsuleHalfHeight);
            }
            Detail->SetNumberField(TEXT("sweep_envelope_radius_cm"),R);
            Detail->SetNumberField(TEXT("sweep_envelope_half_height_cm"),H);
            Detail->SetStringField(TEXT("sweep_envelope_agents"),TEXT("player,E01,E02,E03,E04"));
            const bool Blocked=GetWorld()->SweepSingleByChannel(Hit,Entrance.Location+FVector(0,0,H+5),Center.Location+FVector(0,0,H+5),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(R,H),Params);
            Passed &= !Blocked; Detail->SetBoolField(TEXT("sweep_blocked"),Blocked); DescribeHit(Detail,TEXT("sweep"),Hit);
        }
        if (!Passed && FPlatformTime::Seconds()-RoomChecksStartedAt<8.) return;
        if (Passed && !bMovementReleased)
        {
            if (!Town->ReleaseArrivalMovement()) return;
            bMovementReleased=true; MovementReleasedAt=FPlatformTime::Seconds(); return;
        }
        if (Passed && FPlatformTime::Seconds()-MovementReleasedAt<0.5) return;
        if (Passed)
        {
            const bool Settled=Player->GetCharacterMovement()->IsMovingOnGround() && Player->GetCharacterMovement()->CurrentFloor.IsWalkableFloor();
            if (!Settled && FPlatformTime::Seconds()-MovementReleasedAt<2.) return;
            Passed=Settled;
        }
        Detail->SetNumberField(TEXT("minimum_step_z_cm"),MinimumStepZ);
        Detail->SetBoolField(TEXT("qa_released_movement"),bMovementReleased);
        Record(S.Kind+TEXT("/")+S.Id,Passed,JsonText(Detail)); NextStep(); return;
    }
    if (S.Kind==TEXT("interact"))
    {
        if (!bActionDone) { Town->Interact(Player); bActionDone=true; ActionStartedAt=FPlatformTime::Seconds(); return; }
        if (FPlatformTime::Seconds()-ActionStartedAt<4.) return;
        TSharedPtr<FJsonObject> State; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Town->GetStateJson()),State); bool Passed=false;
        if (State) for (const auto& Value:State->GetArrayField(TEXT("discovered_memes"))) Passed|=Value->AsString()==S.Id;
        Record(S.Kind+TEXT("/")+S.Id,Passed,Town->GetStateJson()); NextStep(); return;
    }
}

void AHeliosTownQARunner::Finish(bool bAllowExit)
{
    if (bFinished) return; bFinished=true;
    auto Report=MakeShared<FJsonObject>(); Report->SetBoolField(TEXT("passed"),Failures==0); Report->SetNumberField(TEXT("failures"),Failures);
    Report->SetStringField(TEXT("suite"),TEXT("town_exploration_interiors_interactions_persistence"));
    Report->SetStringField(TEXT("combat_validation"),TEXT("separate_natural_survival_acceptance_required"));
    Report->SetStringField(TEXT("status"),bInterrupted?TEXT("interrupted"):TEXT("complete")); Report->SetStringField(TEXT("run_id"),RunId);
    Report->SetArrayField(TEXT("checks"),Results); Report->SetNumberField(TEXT("elapsed_seconds"),FPlatformTime::Seconds()-StartTime);
    Frames.Sort();
    if (!Frames.IsEmpty())
    {
        double Total=0.; for (float Frame:Frames) Total+=Frame;
        Report->SetNumberField(TEXT("average_fps"),Frames.Num()/FMath::Max(0.001,Total));
        Report->SetNumberField(TEXT("frame_ms_p95"),Frames[FMath::Min(Frames.Num()-1,FMath::FloorToInt(Frames.Num()*0.95))]*1000.f);
        Report->SetNumberField(TEXT("frame_ms_p99"),Frames[FMath::Min(Frames.Num()-1,FMath::FloorToInt(Frames.Num()*0.99))]*1000.f);
    }
    Report->SetNumberField(TEXT("used_physical_mb"),FPlatformMemory::GetStats().UsedPhysical/1048576.);
    FString Text; FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));
    IFileManager::Get().MakeDirectory(*ReportDirectory(),true);
    FFileHelper::SaveStringToFile(Text,*FPaths::Combine(ReportDirectory(),TEXT("smoke_report.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogTemp,Display,TEXT("IKUN_TOWN_QA COMPLETE failures=%d report=%s"),Failures,*ReportDirectory());
    if (bAllowExit && FParse::Param(FCommandLine::Get(),TEXT("HeliosTownQAExit"))) FPlatformMisc::RequestExit(false);
}
