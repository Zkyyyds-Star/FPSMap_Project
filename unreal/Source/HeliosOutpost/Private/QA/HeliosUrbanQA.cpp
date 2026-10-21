#include "QA/HeliosUrbanQA.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "Adventure/HeliosAdventurePickup.h"
#include "Boss/HeliosEnemyBoss.h"
#include "Boss/HeliosUrbanHazard.h"
#include "Components/CapsuleComponent.h"
#include "Player/HeliosArenaCharacter.h"
#include "Pets/HeliosPetDog.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Variant_Shooter/Weapons/ShooterProjectile.h"
#include "Variant_Shooter/AI/ShooterAIController.h"
#include "Boss/HeliosUrbanBoss.h"
#include "World/HeliosMarker.h"
#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HeliosOutpost.h"

AHeliosUrbanQA::AHeliosUrbanQA()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
}

void AHeliosUrbanQA::BeginPlay()
{
    Super::BeginPlay();
    Started = FPlatformTime::Seconds();
    LastFrameWall=Started;
    bRendering=!FParse::Param(FCommandLine::Get(),TEXT("nullrhi"));
    bCombat=FParse::Param(FCommandLine::Get(),TEXT("UrbanCombatQA"));
    bFullRun=FParse::Param(FCommandLine::Get(),TEXT("UrbanFullRunQA"));
    bCombat=bCombat || bFullRun;
    FParse::Value(FCommandLine::Get(),TEXT("UrbanStopAfterWave="),NormalGoalWaves);
    NormalGoalWaves=FMath::Clamp(NormalGoalWaves,0,29);
    FParse::Value(FCommandLine::Get(),TEXT("UrbanCombatLimitSeconds="),NormalCombatLimitSeconds);
    NormalCombatLimitSeconds=FMath::Clamp(NormalCombatLimitSeconds,60.f,3600.f);
    FParse::Value(FCommandLine::Get(),TEXT("UrbanWeapon="),CombatWeapon);
    bSnapshot=FParse::Param(FCommandLine::Get(),TEXT("UrbanSnapshotQA"));
    bBossDemo=FParse::Param(FCommandLine::Get(),TEXT("UrbanBossDemoQA"));
    bSpawnAudit=FParse::Param(FCommandLine::Get(),TEXT("UrbanSpawnAudit"));
    bRoute=FParse::Param(FCommandLine::Get(),TEXT("UrbanRouteQA"));
    bSpawnRecovery=FParse::Param(FCommandLine::Get(),TEXT("UrbanRecoveryQA"));
    bClinic=FParse::Param(FCommandLine::Get(),TEXT("UrbanClinicQA"));
    FParse::Value(FCommandLine::Get(),TEXT("UrbanRepairQA="),RepairMode);
    FParse::Value(FCommandLine::Get(),TEXT("UrbanRepairRun="),RepairRunName);
}

void AHeliosUrbanQA::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bFinished) return;
    if (!RepairMode.IsEmpty()) { TickResumedFixes(DeltaSeconds); return; }
    if (bSpawnRecovery) { TickSpawnRecovery(); return; }
    if (bClinic) { TickClinic(); return; }
    if (bBossDemo) { TickBossDemo(); return; }
    if (bSpawnAudit) { TickSpawnAudit(); return; }
    if (bRoute) { TickRoute(); return; }
    if (bSnapshot) { TickSnapshot(); return; }
    if (bCombat) { TickCombat(); return; }
    const double Now=FPlatformTime::Seconds();
    if (bRendering && Stage==1 && Now>LastFrameWall) RenderFrameTimes.Add(Now-LastFrameWall);
    LastFrameWall=Now;
    auto* Director = AHeliosAdventureDirector::Find(GetWorld());
    auto* Player = Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
    if (FPlatformTime::Seconds() - Started > 180) { Finish(false, TEXT("wall timeout")); return; }
    if (!Director || !Player) return;
    if (!Director->IsUrbanChallenge()) { Finish(false, TEXT("requires new map")); return; }
    if (Stage == 0)
    {
        if (FPlatformTime::Seconds() - Started < 5) return;
        if (!Director->StartRun(TEXT("rifle"))) { Finish(false,TEXT("weapon start failed")); return; }
        if (!Director->UrbanConfigError.IsEmpty()) { Finish(false,Director->UrbanConfigError); return; }
        // Keep original totals/caps/midbosses. Accelerate timing and use damage injection
        // solely to validate quota accounting and transitions, not combat difficulty.
        for (auto& Wave : Director->UrbanSchedule.Waves) { Wave.SpawnSeconds=.03f; Wave.RestSeconds=.15f; }
        Director->UrbanRestRemaining=.15f;
        Player->SetCanBeDamaged(false);
        Stage=1;
        return;
    }
    if (Stage == 1)
    {
        if (Director->UrbanWaveIndex != LastWave) { ++ClearedWaves; LastWave=Director->UrbanWaveIndex; }
        const int32 Alive=Director->GetAliveCount()+Director->GetBossCount();
        PeakAlive=FMath::Max(PeakAlive,Alive);
        if (!Director->UrbanSchedule.Waves.IsValidIndex(Director->UrbanWaveIndex)) { Finish(false,TEXT("wave index")); return; }
        const auto& Wave=Director->UrbanSchedule.Waves[Director->UrbanWaveIndex];
        if (Alive>Wave.AliveCap) { Finish(false,TEXT("alive cap exceeded")); return; }
        const int32 WaveNumber=Director->UrbanWaveIndex+1;
        if (bRendering && Alive>=Wave.AliveCap && (WaveNumber==1 || WaveNumber==10 || WaveNumber==30) && !CapturedWaves.Contains(WaveNumber))
        {
            CapturedWaves.Add(WaveNumber);
            const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/urban_challenge/screenshots"));
            IFileManager::Get().MakeDirectory(*Directory,true);
            FScreenshotRequest::RequestScreenshot(Directory/FString::Printf(TEXT("runtime_wave_%02d.png"),WaveNumber),true,false);
            return; // Render living actors before injecting the next batch of kills.
        }
        if (Director->IsRunOver())
        {
            if (!Director->IsUrbanVictory() || Director->GetKills()!=1192 || Director->GetBossKills()!=3)
            { Finish(false,TEXT("incorrect victory/quota")); return; }
            ++ClearedWaves; VerifiedKills=Director->GetKills();
            Director->RestartRun();
            Player->SetCanBeDamaged(true);
            if (Director->IsRunOver() || Director->UrbanWaveIndex!=0 || Director->GetKills()!=0)
            { Finish(false,TEXT("victory restart failed")); return; }
            Stage=2; StageStarted=FPlatformTime::Seconds();
            return;
        }
        const int32 Issued=Director->UrbanRegularIssued+Director->UrbanBossIssued;
        if (Alive>=Wave.AliveCap || Issued>=Wave.Total || (Alive>0 && Director->UrbanRestRemaining>0))
        {
            for (TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It)
                if (!It->IsEnemyDead()) UGameplayStatics::ApplyDamage(*It,10000000.f,Player->GetController(),Player,nullptr);
        }
        return;
    }
    if (Stage == 2)
    {
        // Restart grants normal spawn protection; do not misclassify it as a failure.
        if (FPlatformTime::Seconds()-StageStarted<3.0) return;
        UGameplayStatics::ApplyDamage(Player,1000000.f,nullptr,this,UDamageType::StaticClass());
        Stage=3; StageStarted=FPlatformTime::Seconds(); return;
    }
    if (Stage == 3)
    {
        if (!Director->IsRunOver() || !Player->IsDead())
        {
            if (FPlatformTime::Seconds()-StageStarted>2.0) Finish(false,TEXT("death did not end run"));
            return;
        }
        Director->RestartRun();
        if (Director->IsRunOver() || Player->IsDead() || Director->UrbanWaveIndex!=0 ||
            Director->GetAliveCount()!=0 || Director->GetBossCount()!=0)
        { Finish(false,TEXT("failure retry failed")); return; }
        Director->UrbanRestRemaining=600.f;
        Stage=4; StageStarted=FPlatformTime::Seconds(); return;
    }
    if (Stage == 4)
    {
        if (FPlatformTime::Seconds()-StageStarted<3.0) return;
        HazardHealth=Player->GetHealth();
        HazardCaster=GetWorld()->SpawnActor<AActor>();
        const FVector Foot=Player->GetActorLocation()-FVector(0,0,Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        const FTransform Transform(Foot+FVector(0,0,5));
        auto* Hazard=GetWorld()->SpawnActorDeferred<AHeliosUrbanHazard>(AHeliosUrbanHazard::StaticClass(),Transform);
        Hazard->Configure(HazardCaster,true,450.f,2.f,10.f); Hazard->FinishSpawning(Transform);
        Stage=5; StageStarted=FPlatformTime::Seconds(); return;
    }
    if (Stage == 5)
    {
        const double Age=FPlatformTime::Seconds()-StageStarted;
        if (Age<2.0)
        {
            if (!FMath::IsNearlyEqual(Player->GetHealth(),HazardHealth)) { Finish(false,TEXT("damage during warning")); return; }
            bWarningSafe=true; return;
        }
        if (Age<3.3) return;
        if (!FMath::IsNearlyEqual(Player->GetHealth(),HazardHealth-10.f)) { Finish(false,TEXT("impact damage missing or repeated")); return; }
        HazardHealth=Player->GetHealth();
        const FVector Foot=Player->GetActorLocation()-FVector(0,0,Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        // Separate canceled, outside-radius and wrong-floor hazards must all be harmless.
        for (const FVector Offset : {FVector(0,0,5),FVector(1000,0,5),FVector(0,0,-395)})
        {
            const FTransform Transform(Foot+Offset);
            auto* Hazard=GetWorld()->SpawnActorDeferred<AHeliosUrbanHazard>(AHeliosUrbanHazard::StaticClass(),Transform);
            Hazard->Configure(Offset.X==0 && Offset.Z>0 ? HazardCaster.Get() : this,true,450.f,2.f,10.f);
            Hazard->FinishSpawning(Transform);
        }
        HazardCaster->Destroy();
        Stage=6; StageStarted=FPlatformTime::Seconds(); return;
    }
    if (Stage == 6)
    {
        if (FPlatformTime::Seconds()-StageStarted<4.0) return;
        Finish(bWarningSafe && FMath::IsNearlyEqual(Player->GetHealth(),HazardHealth),TEXT("flow retries; warning, one-hit, cancellation, outside radius and floor separation"));
    }
}

void AHeliosUrbanQA::Finish(bool bPassed,const FString& Detail)
{
    bFinished=true;
    auto Report=MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("passed"),bPassed);
    Report->SetStringField(TEXT("detail"),Detail);
    Report->SetStringField(TEXT("scope"),TEXT("30-wave flow with real spawns/accelerated timing/injected kills plus hazard damage geometry/timing; NOT normal combat, visual or performance acceptance"));
    if (!bCombat) { Report->SetBoolField(TEXT("warning_safe"),bWarningSafe); Report->SetBoolField(TEXT("flow_stage_player_invulnerable"),true); }
    Report->SetNumberField(TEXT("cleared_waves"),ClearedWaves);
    Report->SetNumberField(TEXT("verified_kills"),VerifiedKills);
    Report->SetNumberField(TEXT("peak_alive"),PeakAlive);
    Report->SetNumberField(TEXT("wall_seconds"),FPlatformTime::Seconds()-Started);
    Report->SetBoolField(TEXT("rendering"),bRendering);
    if (bCombat)
    {
        Report->SetStringField(TEXT("scope"),bFullRun ? TEXT("Full normal-timing run, real weapon fire, original INIs, autonomous nav/aim/dodge and companion. No injected kills/invulnerability/timing acceleration. Character art still placeholder; not human difficulty acceptance.") : TEXT("90-second autonomous aiming with actual weapon shots, original wave timings and damage settings, live enemy AI and companion; no injected kills. Not full 30-wave balance or final-Boss acceptance."));
        Report->SetStringField(TEXT("weapon"),CombatWeapon);
        Report->SetBoolField(TEXT("full_normal_run"),bFullRun && NormalGoalWaves==0);
        Report->SetNumberField(TEXT("normal_goal_waves"),NormalGoalWaves);
        if(NormalGoalWaves>0)Report->SetStringField(TEXT("scope"),TEXT("Bounded normal-timing wave sample, original INIs and actual weapon/companion/input-equivalent actions; no injected kills/invulnerability/acceleration. Not full 30-wave acceptance."));
        Report->SetNumberField(TEXT("weapon_shot_attempts"),CombatShots);
        Report->SetNumberField(TEXT("enemy_attack_samples"),EnemyAttacks);
        Report->SetNumberField(TEXT("pet_hits"),PetHits);
        Report->SetNumberField(TEXT("pet_max_displacement_cm"),PetTravel);
        Report->SetNumberField(TEXT("player_health"),PlayerHealth);
        if(FParse::Param(FCommandLine::Get(),TEXT("UrbanPressureQA")))
        {
            Report->SetStringField(TEXT("scope"),TEXT("Initialized wave-30 pressure fixture with authored giant, normal enemy mix/spawn timings and original weapon/HP/damage. Actual shots and deaths; no injected kills or invulnerability. Not a normal progressed run."));
            Report->SetNumberField(TEXT("initial_wave"),30);
            Report->SetNumberField(TEXT("prior_waves_completed"),0);
            Report->SetNumberField(TEXT("cleared_waves"),0);
        }
    }
    Report->SetNumberField(TEXT("render_frame_samples"),RenderFrameTimes.Num());
    if (!RenderFrameTimes.IsEmpty())
    {
        double Sum=0; for (const double Time:RenderFrameTimes) Sum+=Time;
        RenderFrameTimes.Sort();
        Report->SetNumberField(TEXT("mean_frame_ms"),1000*Sum/RenderFrameTimes.Num());
        Report->SetNumberField(TEXT("p95_frame_ms"),1000*RenderFrameTimes[FMath::Min(RenderFrameTimes.Num()-1,FMath::FloorToInt(RenderFrameTimes.Num()*.95))]);
        Report->SetStringField(TEXT("profile_scope"),bCombat ? TEXT("offscreen actual equipped-weapon combat, original launcher quality; app frame intervals, not GPU timings or final-character benchmark") : TEXT("offscreen rendered accelerated flow, original launcher quality; not normal gameplay or final-Boss benchmark"));
    }
    FString Text; auto Writer=TJsonWriterFactory<>::Create(&Text); FJsonSerializer::Serialize(Report,Writer);
    FFileHelper::SaveStringToFile(Text,*(FPaths::ProjectDir()/(bFullRun ? (NormalGoalWaves>0?TEXT("../qa/urban_challenge/runtime_normal_sample.json"):TEXT("../qa/urban_challenge/runtime_full_combat.json")) : bCombat ? TEXT("../qa/urban_challenge/runtime_combat.json") : TEXT("../qa/urban_challenge/runtime_flow.json"))));
    UE_LOG(LogHeliosOutpost,Display,TEXT("URBAN_QA result=%s %s"),bPassed?TEXT("PASS"):TEXT("FAIL"),*Detail);
    FPlatformMisc::RequestExit(false);
}

void AHeliosUrbanQA::TickRoute()
{
    auto* Director=AHeliosAdventureDirector::Find(GetWorld());
    auto* Player=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));
    const double Now=FPlatformTime::Seconds();
    if (!Director || !Player || Now-Started<8) return;
    if (Stage==0)
    {
        if (!Director->StartRun(FName(*CombatWeapon))) { FPlatformMisc::RequestExit(false); return; }
        Director->UrbanRestRemaining=600; Stage=1; StageStarted=Now;
    }
    const FVector Goals[]={FVector(-3800,-3100,0),FVector(-3800,0,400),FVector(-3800,1000,400),FVector(3800,1000,400),FVector(3800,3100,0),FVector(3800,0,400),FVector(3800,-3100,0),FVector(-3800,3100,0),FVector(-3800,0,400),FVector(0,-1000,400),FVector(3800,-1000,400),FVector(3800,-3100,0),FVector(0,-3700,0)};
    const FVector Foot=Player->GetNavAgentLocation();
    RoutePlayerHeight=FMath::Max(RoutePlayerHeight,float(Foot.Z));
    for (TActorIterator<AHeliosPetCompanion> It(GetWorld());It;++It)
    {
        RoutePetHeight=FMath::Max(RoutePetHeight,float(It->GetNavAgentLocation().Z));
        float& Height=RouteCompanionHeights.FindOrAdd(It->GetClass()->GetName());
        Height=FMath::Max(Height,float(It->GetNavAgentLocation().Z));
        PetHits=FMath::Max(PetHits,It->HitsDelivered);
    }
    if (RouteReached<UE_ARRAY_COUNT(Goals))
    {
        const FVector Goal=Goals[RouteReached];
        if (FVector::Dist2D(Foot,Goal)<170 && FMath::Abs(Foot.Z-Goal.Z)<100)
        { ++RouteReached; NavigationRefresh=0; }
        else
        {
            NavigationRefresh-=GetWorld()->GetDeltaSeconds();
            if (NavigationRefresh<=0)
            {
                NavigationRefresh=.25f; NavigationPoints.Reset();
                if (auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Foot,Goal,Player))
                    if (Path->IsValid() && !Path->IsPartial()) NavigationPoints=Path->PathPoints;
            }
            for (const FVector Point:NavigationPoints)
                if (FVector::Dist2D(Foot,Point)>80)
                { Player->AddMovementInput((Point-Foot).GetSafeNormal2D(),1); break; }
        }
    }
    if ((RouteReached==UE_ARRAY_COUNT(Goals) && Now-StageStarted>10) || Now-StageStarted>180)
    {
        bool Passed=RouteReached==UE_ARRAY_COUNT(Goals) && RoutePlayerHeight>350 && !RouteCompanionHeights.IsEmpty();
        TArray<FString> Heights;
        for(const auto& Pair:RouteCompanionHeights)
        { Passed=Passed && Pair.Value>350; Heights.Add(FString::Printf(TEXT("\"%s\":%.1f"),*Pair.Key,Pair.Value)); }
        const FString Report=FString::Printf(TEXT("{\"passed\":%s,\"scope\":\"real player movement over four ramps/two upper decks/bridge; equipped companion follows with normal catch-up rules; no QA teleports or enemies\",\"waypoints_reached\":%d,\"player_max_nav_height_cm\":%.1f,\"companion_max_nav_height_cm\":%.1f,\"wall_seconds\":%.1f,\"player_position\":\"%s\"}"),Passed?TEXT("true"):TEXT("false"),RouteReached,RoutePlayerHeight,RoutePetHeight,Now-StageStarted,*Player->GetActorLocation().ToString());
        FString DetailedReport=Report.LeftChop(1)+FString::Printf(TEXT(",\"weapon\":\"%s\",\"companion_heights_by_class\":{%s}}"),*CombatWeapon,*FString::Join(Heights,TEXT(",")));
        FFileHelper::SaveStringToFile(DetailedReport,*(FPaths::ProjectDir()/FString::Printf(TEXT("../qa/urban_challenge/route_%s.json"),*CombatWeapon)));
        bFinished=true; FPlatformMisc::RequestExit(false);
    }
}

void AHeliosUrbanQA::TickSpawnRecovery()
{
    auto* Director=AHeliosAdventureDirector::Find(GetWorld());
    auto* Player=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));
    const double Now=FPlatformTime::Seconds();
    if(!Director || !Player || Now-Started<8)return;
    if(Stage==0)
    {
        if(!Director->StartRun(TEXT("rifle"))) { FPlatformMisc::RequestExit(false);return; }
        Director->UrbanRestRemaining=0; Director->UrbanSpawnRemaining=0; StageStarted=Now; Stage=1;
        int32 VisibleBlocked=0,Allowed=0,Index=0;
        for(AHeliosMarker* Marker:Director->EnemyMarkers)
        {
            if(!IsValid(Marker))continue;
            ++Index; Marker->SetActorLocation(FVector(180,0,0));
            FVector Position; FString Reason;
            if(!Director->IsSpawnCandidateValid(*Marker,EHeliosEnemyType::E01_Rifle,Position,false,&Reason) && Reason==TEXT("visible"))++VisibleBlocked;
            if(Director->IsSpawnCandidateValid(*Marker,EHeliosEnemyType::E01_Rifle,Position,true))++Allowed;
        }
        bRecoveryStarvation=VisibleBlocked==Director->EnemyMarkers.Num() && Allowed>0;
        UE_LOG(LogHeliosOutpost,Display,TEXT("URBAN_RECOVERY_QA visible_blocked=%d allowed_after_warning=%d"),VisibleBlocked,Allowed);
    }
    if(Director->UrbanRecoveryWarningRemaining>0 && Director->GetAliveCount()>0)bRecoveryNoEarlySpawn=false;
    if(Director->UrbanRecoverySpawns>0 || Now-StageStarted>20)
    {
        const bool Passed=bRecoveryStarvation && bRecoveryNoEarlySpawn && Director->UrbanRecoverySpawns==1 && Director->UrbanLastRecoveryWarningWallSeconds>=3.0 && Director->GetAliveCount()==1 && Director->Kills==0 && Director->UrbanRegularIssued==1;
        const FString Report=FString::Printf(TEXT("{\"passed\":%s,\"scope\":\"controlled visible-marker starvation fixture, no injected kills or invulnerability; not normal full combat\",\"all_markers_visibility_blocked\":%s,\"no_spawn_during_warning\":%s,\"configured_warning_seconds\":3.0,\"measured_wall_warning_seconds\":%.6f,\"recovery_spawns\":%d,\"actual_live\":%d,\"counted_kills\":%d,\"quota_consumed\":%d,\"remaining_quota\":%d}"),Passed?TEXT("true"):TEXT("false"),bRecoveryStarvation?TEXT("true"):TEXT("false"),bRecoveryNoEarlySpawn?TEXT("true"):TEXT("false"),Director->UrbanLastRecoveryWarningWallSeconds,Director->UrbanRecoverySpawns,Director->GetAliveCount(),Director->Kills,Director->UrbanRegularIssued,15-Director->UrbanRegularIssued);
        FFileHelper::SaveStringToFile(Report,*(FPaths::ProjectDir()/TEXT("../qa/urban_challenge/recovery.json")));
        bFinished=true; FPlatformMisc::RequestExit(false);
    }
}

void AHeliosUrbanQA::TickClinic()
{
    auto* Director=AHeliosAdventureDirector::Find(GetWorld());
    auto* Player=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));
    const double Now=FPlatformTime::Seconds();
    if(!Director || !Player || Now-Started<8)return;
    if(Stage==0)
    {
        if(!Director->StartRun(TEXT("rifle"))) { FPlatformMisc::RequestExit(false);return; }
        Director->UrbanRestRemaining=600; StageStarted=Now; Stage=1; PlayerHealth=Player->GetHealth();
        // Isolate enemy access, so an attacking companion cannot kill the test charger first.
        for(TActorIterator<AHeliosPetCompanion> It(GetWorld());It;++It)It->Destroy();
        bool Spawned=false;
        for(AHeliosMarker* Marker:Director->EnemyMarkers)
            if(IsValid(Marker) && Director->TrySpawnEnemy(false,Marker,EHeliosEnemyType::E02_Charger)) {Spawned=true;break;}
        if(!Spawned)StageStarted=Now-60;
    }
    // Initial weapon activation can summon a companion on a later tick.
    for(TActorIterator<AHeliosPetCompanion> It(GetWorld());It;++It)It->Destroy();
    for(TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It)
    {
        if(It->IsEnemyDead())continue;
        const FVector Foot=It->GetNavAgentLocation();
        if(FMath::Abs(Foot.X)<950 && Foot.Y>-4450 && Foot.Y<-3330 && Foot.Z<100)bClinicEnemyEntered=true;
        if(Now-LastProgress>5)
        {
            auto* AI=Cast<AShooterAIController>(It->GetController());
            UE_LOG(LogHeliosOutpost,Display,TEXT("URBAN_CLINIC enemy=%s location=%s velocity=%s investigating=%d path_status=%d target=%s"),
                *It->GetName(),*It->GetActorLocation().ToString(),*It->GetVelocity().ToString(),AI?AI->IsInvestigating():false,
                AI?int32(AI->GetMoveStatus()):-1,AI&&AI->GetCurrentTarget()?*AI->GetCurrentTarget()->GetName():TEXT("none"));
        }
    }
    if(Now-LastProgress>5)LastProgress=Now;
    const float Damage=PlayerHealth-Player->GetHealth();
    if((bClinicEnemyEntered && Damage>0) || Now-StageStarted>45 || Player->GetHealth()<=0)
    {
        const bool Passed=bClinicEnemyEntered && Damage>0;
        const FString Report=FString::Printf(TEXT("{\"passed\":%s,\"scope\":\"isolated actual charger/navigation/damage fixture in clinic, companion disabled only in fixture; no damage injection or health buff; not normal survival balance\",\"enemy_entered_clinic\":%s,\"actual_damage\":%.1f,\"wall_seconds\":%.2f,\"player_position\":\"%s\"}"),Passed?TEXT("true"):TEXT("false"),bClinicEnemyEntered?TEXT("true"):TEXT("false"),Damage,Now-StageStarted,*Player->GetActorLocation().ToString());
        FFileHelper::SaveStringToFile(Report,*(FPaths::ProjectDir()/TEXT("../qa/urban_challenge/clinic_access.json")));
        bFinished=true; FPlatformMisc::RequestExit(false);
    }
}

void AHeliosUrbanQA::TickSpawnAudit()
{
    auto* Director=AHeliosAdventureDirector::Find(GetWorld());
    auto* Player=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));
    if (!Director || !Player || FPlatformTime::Seconds()-Started<8) return;
    if (Stage==0)
    {
        if (!Director->StartRun(TEXT("rifle"))) { FPlatformMisc::RequestExit(false); return; }
        Director->UrbanRestRemaining=600; Stage=1;
    }
    const int32 Index=Stage-1;
    if (Index>=35)
    {
        FString Report=TEXT("{\"scope\":\"teleported grid spawn-safety audit, not combat\",\"positions\":[")+FString::Join(SpawnAuditRows,TEXT(","))+TEXT("]}");
        FFileHelper::SaveStringToFile(Report,*(FPaths::ProjectDir()/TEXT("../qa/urban_challenge/spawn_grid.json")));
        bFinished=true; FPlatformMisc::RequestExit(false); return;
    }
    const FVector Point((Index%7-3)*1500.f,(Index/7-2)*1500.f,102);
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()); FNavLocation Ground;
    if (!Nav || !Nav->ProjectPointToNavigation(Point,Ground,FVector(120,120,180))) { ++Stage; return; }
    Player->SetActorLocation(Ground.Location+FVector(0,0,102),false,nullptr,ETeleportType::TeleportPhysics);
    int32 Valid=0; int32 Hidden=0;
    for (AHeliosMarker* Marker:Director->EnemyMarkers)
    {
        if (!IsValid(Marker)) continue;
        if (!Director->IsCandidateVisible(Marker->GetActorLocation()+FVector(0,0,100))) ++Hidden;
        for (int32 Type=1;Type<=4;++Type)
        { FVector Spawn; if (Director->IsSpawnCandidateValid(*Marker,static_cast<EHeliosEnemyType>(Type),Spawn)) ++Valid; }
    }
    SpawnAuditRows.Add(FString::Printf(TEXT("{\"x\":%.0f,\"y\":%.0f,\"z\":%.0f,\"valid_type_marker_pairs\":%d,\"hidden_markers\":%d}"),Ground.Location.X,Ground.Location.Y,Ground.Location.Z,Valid,Hidden));
    ++Stage;
}

void AHeliosUrbanQA::TickBossDemo()
{
    auto* Director=AHeliosAdventureDirector::Find(GetWorld());
    auto* Player=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));
    const double Now=FPlatformTime::Seconds();
    if (!Director || !Player) return;
    if (Stage==0)
    {
        if (Now-Started<8) return;
        if (!Director->IsUrbanChallenge() || !Director->StartRun(TEXT("rifle")))
        { bFinished=true; FPlatformMisc::RequestExit(false); return; }
        Director->UrbanRestRemaining=600;
        Player->SetActorLocation(FVector(0,-500,102),false,nullptr,ETeleportType::TeleportPhysics);
        DemoBoss=GetWorld()->SpawnActor<AHeliosUrbanBoss>(FVector(-2200,0,125),FRotator::ZeroRotator);
        if (DemoBoss)
        {
            DemoBoss->SetBossNumber(30);
            Director->Bosses.Add(DemoBoss); // Isolated fixture also verifies the actual HUD identity.
        }
        if (!FParse::Param(FCommandLine::Get(),TEXT("UrbanBossFirstPerson")))
            DemoCamera=GetWorld()->SpawnActor<ACameraActor>(FVector(-600,-350,3000),FRotator::ZeroRotator);
        if (DemoCamera)
        {
            DemoCamera->GetCameraComponent()->SetFieldOfView(75);
            // Look down into the court instead of hiding the fissure behind a bridge.
            DemoCamera->SetActorRotation(FRotator(-90,90,0));
            DemoCamera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount=true;
            DemoCamera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount=0;
            Cast<APlayerController>(Player->GetController())->SetViewTarget(DemoCamera);
        }
        Stage=1; StageStarted=Now; LastFrameWall=Now; PlayerHealth=Player->GetHealth();
    }
    if (!IsValid(DemoBoss)) return;
    if (bRendering && Now>LastFrameWall) RenderFrameTimes.Add(Now-LastFrameWall);
    LastFrameWall=Now;
    const FString Skill=DemoBoss->GetUrbanSkillText();
    FString Capture;
    if (Skill.StartsWith(TEXT("Basketball")))
    {
        if (DemoBoss->GetUrbanWarningProgress()>.4f && DemoBoss->GetUrbanWarningProgress()<.8f) Capture=TEXT("basketball_warning");
        for (TActorIterator<AHeliosUrbanHazard> It(GetWorld());It;++It)
            if (It->HasImpacted() && It->GetSecondsSinceImpact()>.18f) { Capture=TEXT("basketball_impact"); break; }
    }
    else if (Skill.StartsWith(TEXT("Charge warning")) && DemoBoss->GetUrbanWarningProgress()>.4f) Capture=TEXT("charge_warning");
    else if (Skill.StartsWith(TEXT("Ground rupture"))) Capture=TEXT("charge_active");
    else if (Skill.StartsWith(TEXT("Aftershock")))
        for (TActorIterator<AHeliosUrbanHazard> It(GetWorld());It;++It)
            if (It->GetOwner()==DemoBoss && It->HasImpacted() && It->GetSecondsSinceImpact()>.18f && !It->IsAirstrike() && It->IsDamaging())
            { Capture=TEXT("ground_fissure_impact"); break; }
    FVector Aim=DemoBoss->GetActorLocation()+FVector(0,0,150);
    if (!FParse::Param(FCommandLine::Get(),TEXT("UrbanBossFirstPerson")) && Capture.StartsWith(TEXT("basketball")))
        for (TActorIterator<AHeliosUrbanHazard> It(GetWorld());It;++It)
            if (It->GetOwner()==DemoBoss) { Aim=It->GetActorLocation()+FVector(0,0,100); break; }
    Player->GetController()->SetControlRotation((Aim-Player->FirstPersonCamera->GetComponentLocation()).Rotation());
    if (bRendering && !Capture.IsEmpty() && !BossDemoCaptures.Contains(Capture))
    {
        BossDemoCaptures.Add(Capture);
        FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/urban_challenge/screenshots"))/TEXT("boss_demo_")+Capture+TEXT(".png"),true,false);
    }
    // Fixture stays on the court and evades warnings with real movement, no health override.
    FVector Dodge=FVector::ZeroVector;
    const FVector Foot=Player->GetActorLocation()-FVector(0,0,Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    for (TActorIterator<AHeliosUrbanHazard> It(GetWorld());It;++It)
    {
        if (It->HasImpacted() || FMath::Abs(Foot.Z-It->GetActorLocation().Z)>100) continue;
        const FVector Offset=Foot-It->GetActorLocation();
        if (Offset.Size2D()<It->GetDangerRadius()+180)
            Dodge+=(Offset.IsNearlyZero()?FVector(0,-1,0):Offset.GetSafeNormal2D())*(It->GetDangerRadius()+180-Offset.Size2D());
    }
    if (!Dodge.IsNearlyZero()) Player->AddMovementInput(Dodge.GetSafeNormal2D(),1);
    if (Now-StageStarted>55 || Player->IsDead())
    {
        const bool Passed=DemoBoss->GetBasketballCasts()>0 && DemoBoss->GetChargeCasts()>0 && !Player->IsDead() && (!bRendering || BossDemoCaptures.Num()==5)
            && DemoBoss->HasUrbanCharacter() && DemoBoss->GetUrbanAnimationCount()==10 && FMath::IsNearlyEqual(DemoBoss->GetUrbanCharacterHeight(),500.f,1.f);
        FString Report=FString::Printf(TEXT("{\"passed\":%s,\"scope\":\"isolated normal-timing authored giant demo; not full-wave or user art acceptance\",\"basketball_casts\":%d,\"charge_casts\":%d,\"charge_contacts\":%d,\"captures\":%d,\"health_before\":%.1f,\"health_after\":%.1f,\"invulnerable\":false}"),Passed?TEXT("true"):TEXT("false"),DemoBoss->GetBasketballCasts(),DemoBoss->GetChargeCasts(),DemoBoss->GetChargeContacts(),BossDemoCaptures.Num(),PlayerHealth,Player->GetHealth());
        Report.LeftChopInline(1);
        Report+=FString::Printf(TEXT(",\"character_loaded\":%s,\"character_height_cm\":%.3f,\"animation_count\":%d,\"first_person\":%s}"),
            DemoBoss->HasUrbanCharacter()?TEXT("true"):TEXT("false"),DemoBoss->GetUrbanCharacterHeight(),DemoBoss->GetUrbanAnimationCount(),DemoCamera?TEXT("false"):TEXT("true"));
        DemoBoss->Destroy();
        int32 Remaining=0;
        for (TActorIterator<AHeliosUrbanHazard> It(GetWorld());It;++It) if (!It->IsActorBeingDestroyed()) ++Remaining;
        double Sum=0; for(const double Time:RenderFrameTimes) Sum+=Time;
        RenderFrameTimes.Sort();
        const double Mean=RenderFrameTimes.IsEmpty()?0:1000*Sum/RenderFrameTimes.Num();
        const double P95=RenderFrameTimes.IsEmpty()?0:1000*RenderFrameTimes[FMath::Min(RenderFrameTimes.Num()-1,FMath::FloorToInt(RenderFrameTimes.Num()*.95))];
        Report.LeftChopInline(1);
        Report+=FString::Printf(TEXT(",\"remaining_hazards\":%d,\"render_frame_samples\":%d,\"mean_frame_ms\":%.6f,\"p95_frame_ms\":%.6f,\"profile_scope\":\"isolated offscreen authored giant skills, app intervals; not GPU or full-wave benchmark\"}"),Remaining,RenderFrameTimes.Num(),Mean,P95);
        UE_LOG(LogHeliosOutpost,Display,TEXT("URBAN_BOSS_DEMO %s remaining_hazards=%d"),*Report,Remaining);
        FFileHelper::SaveStringToFile(Report,*(FPaths::ProjectDir()/TEXT("../qa/urban_challenge/boss_demo.json")));
        bFinished=true; FPlatformMisc::RequestExit(false);
    }
}

void AHeliosUrbanQA::TickSnapshot()
{
    auto* Director=AHeliosAdventureDirector::Find(GetWorld());
    auto* Player=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));
    const double Now=FPlatformTime::Seconds();
    if (!Director || !Player) return;
    if (!Director->IsUrbanChallenge()) { FPlatformMisc::RequestExit(false); return; }
    if (Stage==0)
    {
        if (Now-Started<8) return;
        if (!Director->StartRun(TEXT("rifle"))) { FPlatformMisc::RequestExit(false); return; }
        Director->UrbanRestRemaining=600;
        // Isolated screenshot fixture: use the open court, not the clinic doorway.
        // Normal full-run combat never uses this relocation.
        Player->SetActorLocation(FVector(-2000,-1700,102),false,nullptr,ETeleportType::TeleportPhysics);
        Player->GetController()->SetControlRotation(FRotator::ZeroRotator);
        const FVector Foot=Player->GetActorLocation()-FVector(0,0,Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        const FTransform Transform(Foot+Player->GetControlRotation().Vector()*600+FVector(0,0,5));
        auto* Hazard=GetWorld()->SpawnActorDeferred<AHeliosUrbanHazard>(AHeliosUrbanHazard::StaticClass(),Transform);
        Hazard->Configure(this,true,450,5,10); Hazard->FinishSpawning(Transform);
        SnapshotHazard=Hazard;
        Stage=1; StageStarted=Now;
    }
    if (IsValid(SnapshotHazard) && ((Stage==1 && SnapshotHazard->GetElapsedSeconds()>2) || (Stage==2 && SnapshotHazard->HasImpacted() && SnapshotHazard->GetSecondsSinceImpact()>.18f)))
    {
        FString Name=TEXT("snapshot"); FParse::Value(FCommandLine::Get(),TEXT("UrbanSnapshotName="),Name);
        Name=FPaths::MakeValidFileName(Name);
        const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/urban_challenge/screenshots"));
        FScreenshotRequest::RequestScreenshot(Directory/Name+(Stage==1 ? TEXT("_warning.png") : TEXT("_impact.png")),true,false);
        ++Stage;
        if (Stage==3) StageStarted=Now;
    }
    if ((Stage==3 && Now-StageStarted>2) || Now-Started>45) { bFinished=true; FPlatformMisc::RequestExit(false); }
}

void AHeliosUrbanQA::TickCombat()
{
    auto* Director=AHeliosAdventureDirector::Find(GetWorld());
    auto* Player=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));
    const double Now=FPlatformTime::Seconds();
    if (!Director || !Player) return;
    if (!Director->IsUrbanChallenge()) { Finish(false,TEXT("combat QA requires urban map")); return; }
    if (!bCombatStarted)
    {
        if (Now-Started<5) return;
        if (!Director->StartRun(FName(*CombatWeapon))) { Finish(false,TEXT("combat start failed")); return; }
        if (!bFullRun) Player->SetActorLocation(FVector(0,-500,102),false,nullptr,ETeleportType::TeleportPhysics);
        if(FParse::Param(FCommandLine::Get(),TEXT("UrbanPressureQA")))
        {
            // Initial stress fixture; never count this as completing the preceding 29 waves.
            Director->UrbanWaveIndex=29; Director->UrbanRestRemaining=0;
            auto* Giant=GetWorld()->SpawnActor<AHeliosUrbanBoss>(FVector(-2200,0,125),FRotator::ZeroRotator);
            if(Giant) { Giant->SetBossNumber(30); Director->Bosses.Add(Giant); }
            UE_LOG(LogHeliosOutpost,Display,TEXT("URBAN_PRESSURE initial_wave=30 fixture_boss=1 prior_waves_completed=0 damage_override=0"));
        }
        bCombatStarted=true; StageStarted=Now; Stage=1;
    }
    if (bRendering && Now>LastFrameWall) RenderFrameTimes.Add(Now-LastFrameWall);
    LastFrameWall=Now;
    PeakAlive=FMath::Max(PeakAlive,Director->GetAliveCount()+Director->GetBossCount());
    VerifiedKills=Director->GetKills(); ClearedWaves=Director->UrbanWaveIndex;
    PlayerHealth=Player->GetHealth();
    for (TActorIterator<AHeliosPetCompanion> It(GetWorld());It;++It)
    {
        if (!bPetObserved) { PetOrigin=It->GetActorLocation(); bPetObserved=true; }
        PetTravel=FMath::Max(PetTravel,FVector::Dist(PetOrigin,It->GetActorLocation()));
        PetHits=FMath::Max(PetHits,It->HitsDelivered);
    }
    const FVector View=Player->FirstPersonCamera->GetComponentLocation();
    AHeliosEnemyActor* Target=nullptr;
    AHeliosEnemyActor* Approach=nullptr;
    float ApproachDistance=FLT_MAX;
    float Nearest=FLT_MAX;
    int32 TargetRank=MAX_int32;
    const bool bMelee=CombatWeapon==TEXT("cfm_sunshangxiang") || CombatWeapon==TEXT("cfm_zhongxiaokui");
    int32 Attacks=0;
    for (TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It)
    {
        Attacks+=It->GetAttackCount();
        if (It->IsEnemyDead()) continue;
        const float Distance=FVector::DistSquared(View,It->GetActorLocation());
        if (Distance<ApproachDistance) { ApproachDistance=Distance; Approach=*It; }
        // Input-equivalent QA tactics: deal with visible shooters before kiting chargers.
        // All weapons, enemy stats, normal timings and damage validation remain intact.
        const int32 Rank=!bMelee && bFullRun ?
            ((It->EnemyType==EHeliosEnemyType::E01_Rifle || It->EnemyType==EHeliosEnemyType::E03_Skirmisher)?0:
             It->EnemyType==EHeliosEnemyType::E02_Charger?2:1):0;
        if (Rank>TargetRank || (Rank==TargetRank && Distance>=Nearest)) continue;
        FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(UrbanCombatAim),false,Player);
        if (GetWorld()->LineTraceSingleByChannel(Hit,View,It->GetActorLocation(),ECC_Visibility,Query) && Hit.GetActor()!=*It) continue;
        TargetRank=Rank; Nearest=Distance; Target=*It;
    }
    EnemyAttacks=FMath::Max(EnemyAttacks,Attacks);
    if (Target && !Player->IsDead() && (!bMelee || FVector::Dist2D(Player->GetActorLocation(),Target->GetActorLocation())<280.f))
    {
        FVector Aim=Target->GetActorLocation();
        if (bFullRun && !bMelee) Aim+=Target->GetVelocity()*FMath::Clamp(FVector::Dist(Aim,View)/ObservedProjectileSpeed,0.f,.5f);
        Player->GetController()->SetControlRotation((Aim-View).Rotation());
        if(bFullRun && bMelee && Now-LastSpecialInputWall>=8.0 && FVector::Dist2D(Player->GetActorLocation(),Target->GetActorLocation())<260)
        { Player->StartSpecialAttack(); LastSpecialInputWall=Now; }
        const int32 Ammo=Player->GetAmmo();
        Player->StartFireFromAutomation();
        if (Player->GetAmmo()<Ammo) ++CombatShots;
    }
    else Player->StopFireFromAutomation();
    if (bFullRun)
    {
        // Aim and movement are independent: shoot the ranged threat while keeping
        // distance from the nearest live enemy instead of running into chargers.
        AHeliosEnemyActor* MoveThreat=!bMelee && Approach ? Approach : Target;
        const float MoveThreatDistance=MoveThreat?FVector::Dist2D(Player->GetActorLocation(),MoveThreat->GetActorLocation()):FLT_MAX;
        FVector Goal=Approach ? Approach->GetActorLocation() : FVector(0,0,102);
        if (MoveThreat && MoveThreatDistance<(bMelee?180.f:1300.f))
        {
            const FVector Away=(Player->GetActorLocation()-MoveThreat->GetActorLocation()).GetSafeNormal2D();
            Goal=Player->GetActorLocation()+FVector::CrossProduct(Away,FVector::UpVector)*(bMelee?70.f:450.f)+Away*(bMelee?10.f:200.f);
        }
        // Reach an actual medical pickup through navigation/overlap, never heal by QA injection.
        if (Player->GetHealth()<Player->GetMaxHealth()*.75f && (bMelee || MoveThreatDistance>800.f))
        {
            float MedicalDistance=FLT_MAX;
            for (TActorIterator<AHeliosAdventurePickup> It(GetWorld());It;++It)
            {
                if (!It->IsMedical() || It->IsActorBeingDestroyed()) continue;
                const float Distance=FVector::DistSquared(Player->GetActorLocation(),It->GetActorLocation());
                if (Distance<MedicalDistance)
                { MedicalDistance=Distance; Goal=It->GetActorLocation(); }
            }
        }
        FVector Dodge=FVector::ZeroVector;
        const FVector Foot=Player->GetActorLocation()-FVector(0,0,Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        for (TActorIterator<AHeliosUrbanHazard> It(GetWorld());It;++It)
        {
            if (It->HasImpacted() || FMath::Abs(Foot.Z-It->GetActorLocation().Z)>100) continue;
            const FVector Offset=Foot-It->GetActorLocation(); const float Distance=Offset.Size2D();
            if (Distance<It->GetDangerRadius()+150)
                Dodge+=(Distance>5?Offset.GetSafeNormal2D():FVector(1,0,0))*(It->GetDangerRadius()+150-Distance);
        }
        if (!Dodge.IsNearlyZero()) Goal=Player->GetActorLocation()+Dodge.GetSafeNormal2D()*800;
        const FVector Position=Player->GetActorLocation();
        SteeringStuckSeconds=FVector::Dist2D(Position,LastSteeringPosition)<1 && FVector::Dist2D(Position,Goal)>250
            ? SteeringStuckSeconds+GetWorld()->GetDeltaSeconds() : 0;
        LastSteeringPosition=Position;
        if (SteeringStuckSeconds>2 && FMath::Abs(Position.X)>3400 && FMath::Abs(Position.X)<4500 && FMath::Abs(Position.Y)>1400 && FMath::Abs(Position.Y)<3100)
        {
            SteeringRecoveryGoal=FVector(Position.X>0?3800:-3800,Position.Y>0?3200:-3200,10);
            bSteeringRecovery=true; SteeringStuckSeconds=0; NavigationRefresh=0;
            UE_LOG(LogHeliosOutpost,Display,TEXT("URBAN_FULL_NAV recovery walk_to_ramp_foot=%s"),*SteeringRecoveryGoal.ToString());
        }
        if (bSteeringRecovery)
        {
            if (FVector::Dist2D(Position,SteeringRecoveryGoal)<120) { bSteeringRecovery=false; NavigationRefresh=0; }
            else Goal=SteeringRecoveryGoal;
        }
        Goal.X=FMath::Clamp(Goal.X,-5450.f,5450.f); Goal.Y=FMath::Clamp(Goal.Y,-4250.f,4250.f);
        NavigationRefresh-=GetWorld()->GetDeltaSeconds();
        if (NavigationRefresh<=0)
        {
            NavigationRefresh=.4f; NavigationPoints.Reset();
            if (auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Player->GetNavAgentLocation(),Goal,Player))
                if (Path->IsValid() && !Path->IsPartial()) NavigationPoints=Path->PathPoints;
            if (NavigationPoints.IsEmpty())
            {
                // Walk back onto a nearby valid nav polygon when steering grazes a cover edge.
                // No teleport, fabricated kills or relaxation of actual spawn safety rules.
                if (auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
                {
                    FNavLocation StartNav,GoalNav;
                    if (Nav->ProjectPointToNavigation(Player->GetNavAgentLocation(),StartNav,FVector(600,600,800)) &&
                        Nav->ProjectPointToNavigation(Goal,GoalNav,FVector(300,300,500)))
                    {
                        NavigationPoints.Add(StartNav.Location);
                        if (auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),StartNav.Location,GoalNav.Location,Player))
                            if (Path->IsValid() && !Path->IsPartial()) NavigationPoints.Append(Path->PathPoints);
                    }
                }
            }
            for (TActorIterator<AShooterProjectile> It(GetWorld());It;++It)
                if (It->GetInstigator()==Player)
                    if (auto* Movement=It->FindComponentByClass<UProjectileMovementComponent>())
                        if (Movement->InitialSpeed>100) { ObservedProjectileSpeed=Movement->InitialSpeed; break; }
        }
        for (const FVector Point:NavigationPoints)
            if (FVector::Dist2D(Player->GetActorLocation(),Point)>35)
            { Player->AddMovementInput((Point-Player->GetActorLocation()).GetSafeNormal2D(),1); break; }
        if (Now-LastProgress>10)
        {
            LastProgress=Now;
            int32 SafeMarkers=0;
            for (AHeliosMarker* Marker:Director->EnemyMarkers)
            { FVector Location; if (IsValid(Marker) && Director->IsSpawnCandidateValid(*Marker,EHeliosEnemyType::E01_Rifle,Location)) ++SafeMarkers; }
            UE_LOG(LogHeliosOutpost,Display,TEXT("URBAN_FULL_NAV player=%s nav=%s path_points=%d safe_markers=%d issued_regular=%d issued_boss=%d rest=%.2f"),*Player->GetActorLocation().ToString(),*Player->GetNavAgentLocation().ToString(),NavigationPoints.Num(),SafeMarkers,Director->UrbanRegularIssued,Director->UrbanBossIssued,Director->UrbanRestRemaining);
            const FString Progress=FString::Printf(TEXT("{\"wave\":%d,\"kills\":%d,\"alive\":%d,\"bosses\":%d,\"health\":%.1f,\"wall_seconds\":%.1f,\"projectile_speed\":%.1f,\"injected_kills\":false,\"invulnerable\":false}"),Director->UrbanWaveIndex+1,VerifiedKills,Director->GetAliveCount(),Director->GetBossCount(),PlayerHealth,Now-StageStarted,ObservedProjectileSpeed);
            FFileHelper::SaveStringToFile(Progress,*(FPaths::ProjectDir()/TEXT("../qa/urban_challenge/runtime_full_progress.json")));
            UE_LOG(LogHeliosOutpost,Display,TEXT("URBAN_FULL %s"),*Progress);
        }
    }
    const int32 Wave=Director->UrbanWaveIndex+1;
    const int32 Capture=bFullRun ? ((Wave==1 || Wave==10 || Wave==20 || Wave==30)?Wave:0) : FMath::FloorToInt((Now-StageStarted)/30);
    if (bRendering && Capture>0 && !CapturedWaves.Contains(Capture) &&
        (bFullRun ? ((Wave==1 && Director->GetAliveCount()>0 && Now-StageStarted>20) || (Wave!=1 && Director->GetBossCount()>0)) : true))
    {
        CapturedWaves.Add(Capture);
        const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/urban_challenge/screenshots"));
        const FString FileName=bFullRun?FString::Printf(TEXT("full_combat_wave_%02d.png"),Capture):FString::Printf(TEXT("combat_%02d.png"),Capture);
        FScreenshotRequest::RequestScreenshot(Directory/FileName,true,false);
    }
    const bool ReachedNormalGoal=bFullRun && NormalGoalWaves>0 && Director->UrbanWaveIndex>=NormalGoalWaves;
    if (Now-StageStarted>=(bFullRun?NormalCombatLimitSeconds:90) || Director->IsRunOver() || ReachedNormalGoal)
    {
        Player->StopFireFromAutomation();
        Finish(bFullRun ? (NormalGoalWaves>0 ? ReachedNormalGoal && !Player->IsDead() : Director->IsUrbanVictory() && VerifiedKills==1192) : VerifiedKills>0 && CombatShots>0 && EnemyAttacks>0 && PetTravel>200,
            bFullRun ? TEXT("normal full-run autonomous weapon/nav/dodge; see scope and final character limitation") : FParse::Param(FCommandLine::Get(),TEXT("UrbanPressureQA"))
                ? TEXT("wave-30 initialized pressure fixture; original weapon damage; prior waves not completed; no full-run verdict")
                : TEXT("actual selected-weapon fire/live enemy attacks/companion motion sample; no full-run difficulty verdict"));
    }
}
