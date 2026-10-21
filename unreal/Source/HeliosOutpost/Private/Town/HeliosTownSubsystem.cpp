#include "Town/HeliosTownSubsystem.h"
#include "Combat/HeliosCombatTempoSettings.h"
#include "World/HeliosMapDefinition.h"

#include "Adventure/HeliosAdventureDirector.h"
#include "Town/HeliosTownBall.h"
#include "Town/HeliosTownSupply.h"
#include "Adventure/HeliosAdventureCharacter.h"
#include "Boss/HeliosEnemyBoss.h"
#include "HeliosOutpost.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Weapons/HeliosMechWeapon.h"
#include "Weapons/HeliosFighterWeapon.h"
#include "UI/HeliosAdventureHUD.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Components/LightComponent.h"
#include "Components/TextRenderComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationPath.h"
#include "NavigationData.h"
#include "AI/Navigation/NavigationDirtyArea.h"
#include "NavMesh/RecastNavMesh.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
FString MapIdFor(const UWorld* World)
{
    const auto* Map=FHeliosMapRegistry::ForWorld(World);
    return Map ? Map->Id.ToString() : FString();
}
FString TownSavePath(const UWorld* World)
{
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosLoadoutQA"))||FParse::Param(FCommandLine::Get(),TEXT("HeliosLoadoutFlowQA")))return FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Automation/HeliosLoadout"),MapIdFor(World)+TEXT(".town.json"));
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosKillFeedbackQA"))) return FPaths::ProjectSavedDir()/TEXT("SaveGames")/(World->GetMapName()+TEXT("KillFeedbackQASave.json"));
    if (const auto* Map=FHeliosMapRegistry::ForWorld(World); Map && Map->PresentationProfile==TEXT("ghost_village"))
        return FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("SaveGames"),Map->SavePrefix+(FParse::Param(FCommandLine::Get(),TEXT("HeliosVillageQA"))?TEXT("QA.v1.json"):TEXT(".v1.json")));
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosQingxiaoQA")))return FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("SaveGames/HeliosQingxiaoTown.json"));
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"),
        FParse::Param(FCommandLine::Get(), TEXT("HeliosSoulBannerQA")) ? TEXT("HeliosSoulBannerTown.json") : FParse::Param(FCommandLine::Get(), TEXT("HeliosColaQA")) ? TEXT("HeliosColaTown.json") : FParse::Param(FCommandLine::Get(), TEXT("HeliosChopsticksQA")) ? TEXT("HeliosChopsticksTown.json") : FParse::Param(FCommandLine::Get(), TEXT("HeliosTownQA")) ? TEXT("IkunTownQA.v1.json") : TEXT("IkunTown.v1.json"));
}
FString TownBestPath(const UWorld* World)
{
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosLoadoutQA"))||FParse::Param(FCommandLine::Get(),TEXT("HeliosLoadoutFlowQA")))return FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Automation/HeliosLoadout"),MapIdFor(World)+TEXT(".town-best.json"));
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosKillFeedbackQA"))) return FPaths::ProjectSavedDir()/TEXT("SaveGames")/(World->GetMapName()+TEXT("KillFeedbackQABest.json"));
    if (const auto* Map=FHeliosMapRegistry::ForWorld(World); Map && Map->PresentationProfile==TEXT("ghost_village"))
        return FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("SaveGames"),Map->SavePrefix+(FParse::Param(FCommandLine::Get(),TEXT("HeliosVillageQA"))?TEXT("QABest.v1.json"):TEXT("Best.v1.json")));
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosQingxiaoQA")))return FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("SaveGames/HeliosQingxiaoTownBest.json"));
    return FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("SaveGames"),
        FParse::Param(FCommandLine::Get(),TEXT("HeliosSoulBannerQA")) ? TEXT("HeliosSoulBannerTownBest.json") : FParse::Param(FCommandLine::Get(),TEXT("HeliosColaQA")) ? TEXT("HeliosColaTownBest.json") : FParse::Param(FCommandLine::Get(),TEXT("HeliosChopsticksQA")) ? TEXT("HeliosChopsticksTownBest.json") : FParse::Param(FCommandLine::Get(),TEXT("HeliosTownQA")) ? TEXT("IkunTownQABest.v1.json") : TEXT("IkunTownBest.v1.json"));
}
void ReadTownBest(const UWorld* World,int32& Score,float& Seconds)
{
    FString Text; TSharedPtr<FJsonObject> Json;
    if (!FFileHelper::LoadFileToString(Text,*TownBestPath(World)) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Json) || !Json) return;
    double Version=0.,Value=0.; FString Map;
    if (!Json->TryGetNumberField(TEXT("version"),Version) || Version!=1. || !Json->TryGetStringField(TEXT("map_id"),Map) || Map!=MapIdFor(World)) return;
    if (Json->TryGetNumberField(TEXT("best_score"),Value)) Score=FMath::Max(Score,static_cast<int32>(Value));
    if (Json->TryGetNumberField(TEXT("best_seconds"),Value)) Seconds=FMath::Max(Seconds,static_cast<float>(Value));
}
bool AtomicTownJson(const FString& Path,const FString& Text)
{
    const FString Temp=Path+TEXT(".tmp");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    if (!FFileHelper::SaveStringToFile(Text,*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) return false;
#if PLATFORM_WINDOWS
    return ::MoveFileExW(*FPaths::ConvertRelativePathToFull(Temp),*FPaths::ConvertRelativePathToFull(Path),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    return IFileManager::Get().Move(*Path,*Temp,true,true,false,true);
#endif
}
bool ReadVector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FVector& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Object->TryGetArrayField(Key, Values) || Values->Num() != 3) return false;
    double X, Y, Z;
    if (!(*Values)[0]->TryGetNumber(X) || !(*Values)[1]->TryGetNumber(Y) || !(*Values)[2]->TryGetNumber(Z)
        || !FMath::IsFinite(X) || !FMath::IsFinite(Y) || !FMath::IsFinite(Z)) return false;
    Out = FVector(X, Y, Z) * 100.;
    return true;
}
TArray<TSharedPtr<FJsonValue>> VectorJson(const FVector& Vector)
{
    return { MakeShared<FJsonValueNumber>(Vector.X / 100.), MakeShared<FJsonValueNumber>(Vector.Y / 100.), MakeShared<FJsonValueNumber>(Vector.Z / 100.) };
}
TArray<TSharedPtr<FJsonValue>> SetJson(const TSet<FString>& Values)
{
    TArray<FString> Sorted = Values.Array(); Sorted.Sort();
    TArray<TSharedPtr<FJsonValue>> Result;
    for (const FString& Value : Sorted) Result.Add(MakeShared<FJsonValueString>(Value));
    return Result;
}
void ReadSet(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, TSet<FString>& Result)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (Object->TryGetArrayField(Key, Values))
        for (const auto& Value : *Values) { FString Id; if (Value->TryGetString(Id)) Result.Add(Id); }
}
}

UHeliosTownNavigationSystem::UHeliosTownNavigationSystem(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    bGenerateNavigationOnlyAroundNavigationInvokers = true;
    ActiveTilesUpdateInterval = 0.5f;
}

void UHeliosTownNavigationSystem::PostInitProperties()
{
    Super::PostInitProperties();
    // Enforce this world's policy after inherited Engine.ini defaults have loaded.
    bGenerateNavigationOnlyAroundNavigationInvokers = true;
    bTickWhilePaused = true;
    ActiveTilesUpdateInterval = 0.5f;
}

bool UHeliosTownSubsystem::IsTownMap(const UWorld* World)
{
    if (!World) return false;
    const auto* Map=FHeliosMapRegistry::ForWorld(World);
    return Map && Map->UsesTownSystems();
}

void UHeliosTownSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    bTown = GetWorld() && GetWorld()->IsGameWorld() && IsTownMap(GetWorld());
    if (bTown) { bConfigLoaded = LoadConfig(); if (bConfigLoaded) LoadProgress(); }
}

void UHeliosTownSubsystem::Deinitialize()
{
    if (bTown && bConfigLoaded) SaveProgress();
    Director = nullptr;
    Super::Deinitialize();
}

bool UHeliosTownSubsystem::LoadConfig()
{
    FString Text;
    TSharedPtr<FJsonObject> Json;
    if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectContentDir(), FHeliosMapRegistry::ForWorld(GetWorld())->TownConfigPath))
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json)
    { ConfigError = TEXT("小镇配置无法读取"); return false; }
    double Version = 0;
    FString MapId;
    if (!Json->TryGetNumberField(TEXT("version"), Version) || Version != 1
        || !Json->TryGetStringField(TEXT("map_id"), MapId) || MapId != MapIdFor(GetWorld()))
    { ConfigError = TEXT("小镇配置版本不兼容"); return false; }
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Json->TryGetArrayField(TEXT("zones"), Values)) return false;
    TSet<FString> ZoneIds;
    for (const auto& Value : *Values)
    {
        const auto Object = Value->AsObject(); if (!Object) return false;
        FHeliosTownZone Zone;
        if (!Object->TryGetStringField(TEXT("id"), Zone.Id) || Zone.Id.IsEmpty() || ZoneIds.Contains(Zone.Id)
            || !ReadVector(Object, TEXT("center"), Zone.Center) || !ReadVector(Object, TEXT("extent"), Zone.Extent)
            || !ReadVector(Object, TEXT("checkpoint"), Zone.Checkpoint) || Zone.Extent.GetMin() <= 0) return false;
        Object->TryGetStringField(TEXT("name"), Zone.Name);
        ZoneIds.Add(Zone.Id); Zones.Add(Zone);
    }
    if (Zones.Num() != 8) { ConfigError = TEXT("小镇需要八个地区"); return false; }
    if (!Json->TryGetArrayField(TEXT("encounters"), Values)) return false;
    TSet<FString> EncounterIds;
    int32 BossCount = 0;
    for (const auto& Value : *Values)
    {
        const auto Object = Value->AsObject(); if (!Object) return false;
        FHeliosTownEncounter Encounter;
        if (!Object->TryGetStringField(TEXT("id"), Encounter.Id) || Encounter.Id.IsEmpty() || EncounterIds.Contains(Encounter.Id)
            || !Object->TryGetStringField(TEXT("zone_id"), Encounter.ZoneId) || !ZoneIds.Contains(Encounter.ZoneId)
            || !ReadVector(Object, TEXT("center"), Encounter.Center)) return false;
        Object->TryGetBoolField(TEXT("boss"), Encounter.bBoss);
        double Radius; if (Object->TryGetNumberField(TEXT("radius"), Radius)) Encounter.Radius = FMath::Clamp(Radius * 100., 1000., 10000.);
        const TArray<TSharedPtr<FJsonValue>>* Spawns = nullptr;
        if (!Object->TryGetArrayField(TEXT("spawns"), Spawns)) return false;
        for (const auto& Spawn : *Spawns)
        {
            TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>(); Wrapper->SetField(TEXT("p"), Spawn);
            FVector Position; if (!ReadVector(Wrapper, TEXT("p"), Position)) return false;
            Encounter.Spawns.Add(Position);
        }
        const TArray<TSharedPtr<FJsonValue>>* Types = nullptr;
        if (Object->TryGetArrayField(TEXT("enemy_types"), Types))
            for (const auto& Type : *Types) { double Number; if (Type->TryGetNumber(Number)) Encounter.EnemyTypes.Add(FMath::Clamp(static_cast<int32>(Number), 1, 4)); }
        if (Encounter.EnemyTypes.IsEmpty()) Encounter.EnemyTypes = { 1, 2, 3, 4 };
        if (Encounter.bBoss) ++BossCount;
        if (Encounter.Spawns.IsEmpty()) return false;
        EncounterIds.Add(Encounter.Id); Encounters.Add(Encounter);
    }
    if (BossCount != 3) { ConfigError = TEXT("小镇需要三个首领地点"); return false; }
    auto ReadPoints = [&](const TCHAR* Field, TArray<FHeliosTownMeme>& Points)
    {
        const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
        if (!Json->TryGetArrayField(Field, Entries)) return;
        for (const auto& Entry : *Entries)
        {
            const auto Object = Entry->AsObject(); if (!Object) continue;
            FHeliosTownMeme Point;
            if (!Object->TryGetStringField(TEXT("id"), Point.Id) || !ReadVector(Object, TEXT("position"), Point.Position)) continue;
            Object->TryGetStringField(TEXT("name"), Point.Name); Object->TryGetStringField(TEXT("kind"), Point.Kind);
            Point.bHasTarget = ReadVector(Object, TEXT("target"), Point.Target);
            Object->TryGetStringField(TEXT("zone_id"), Point.ZoneId);
            Object->TryGetStringField(TEXT("room_id"), Point.RoomId);
            Point.bIndoorDiscovery = !Point.RoomId.IsEmpty()
                && ReadVector(Object,TEXT("space_center"),Point.SpaceCenter)
                && ReadVector(Object,TEXT("space_extent"),Point.SpaceExtent)
                && Point.SpaceExtent.GetMin() > 0.f;
            if (Point.bIndoorDiscovery)
            {
                double Radius = 3.; Object->TryGetNumberField(TEXT("radius_m"),Radius);
                Point.DiscoveryRadius = FMath::Clamp(static_cast<float>(Radius*100.),50.f,900.f);
                Object->TryGetNumberField(TEXT("floor_id"),Point.FloorId);
            }
            Points.Add(Point);
        }
    };
    ReadPoints(TEXT("memes"), Memes); ReadPoints(TEXT("interiors"), Interiors); ReadPoints(TEXT("supplies"), Supplies);
    for (auto& Supply : Supplies)
        if (Supply.ZoneId.IsEmpty())
            for (const auto& Zone : Zones)
                if (FBox(Zone.Center-Zone.Extent,Zone.Center+Zone.Extent).IsInsideOrOn(Supply.Position)) Supply.ZoneId=Zone.Id;
    UE_LOG(LogHeliosOutpost, Display, TEXT("IKUN_TOWN config zones=%d encounters=%d memes=%d"), Zones.Num(), Encounters.Num(), Memes.Num());
    return true;
}

void UHeliosTownSubsystem::LoadProgress()
{
    FString Text; TSharedPtr<FJsonObject> Json;
    if (!FFileHelper::LoadFileToString(Text, *TownSavePath(GetWorld())) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json) return;
    double Version = 0; FString MapId;
    if (!Json->TryGetNumberField(TEXT("version"), Version) || (Version != 1 && Version != 2) || !Json->TryGetStringField(TEXT("map_id"), MapId) || MapId != MapIdFor(GetWorld())) return;
    Json->TryGetStringField(TEXT("checkpoint_id"), CheckpointId);
    HeliosLoadout::ReadJson(Json,SavedLoadout);
    SavedLoadout=FHeliosLoadout::ResolveLegacyVariant(SavedLoadout,GetWorld()?GetWorld()->GetGameInstance():nullptr); WeaponId=SavedLoadout.PrimaryWeaponId;
    ReadSet(Json, TEXT("explored_zones"), Explored); ReadSet(Json, TEXT("discovered_memes"), Discovered); ReadSet(Json, TEXT("completed_encounters"), Completed);
    TSet<FString> ValidZones, ValidMemes, ValidEncounters;
    for (const auto& Z : Zones) ValidZones.Add(Z.Id);
    for (const auto& M : Memes) ValidMemes.Add(M.Id);
    for (const auto& E : Encounters) ValidEncounters.Add(E.Id);
    Explored = Explored.Intersect(ValidZones); Discovered = Discovered.Intersect(ValidMemes); Completed = Completed.Intersect(ValidEncounters);
    if (!ValidZones.Contains(CheckpointId)) CheckpointId.Reset();
}

bool UHeliosTownSubsystem::SaveProgress()
{
    if (!bTown || !bConfigLoaded) return false;
    if (bSessionStarted && Director && !Director->IsSelectingWeapon() && !Director->IsRunOver() &&
        IsValid(Director->Player) && IsValid(Director->Player->GetEquippedWeapon()))
    {
        const FHeliosLoadout Runtime=Director->Player->GetLoadout();
        FString Error;
        if (FHeliosLoadout::Validate(Runtime,Error) && Runtime.CharacterId!=TEXT("sword_dog"))
        {
            SavedLoadout=Runtime; WeaponId=Runtime.PrimaryWeaponId;
            Director->CaptureActiveLoadout();
        }
    }
    const int32 ExplorationScore=Explored.Num()*100+Discovered.Num()*25+Completed.Num()*150+GetDefeatedBossCount()*500;
    int32 PreviousBest=0; float PreviousSeconds=0.f; ReadTownBest(GetWorld(),PreviousBest,PreviousSeconds);
    const float CurrentSeconds=Director ? Director->SurvivalSeconds : 0.f;
    const int32 NewBest=FMath::Max(PreviousBest,ExplorationScore);
    const float NewBestSeconds=FMath::Max(PreviousSeconds,CurrentSeconds);
    if (Director) { Director->Score=ExplorationScore; Director->BestScore=NewBest; Director->BestSeconds=NewBestSeconds; }
    auto Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("version"), 2); Json->SetStringField(TEXT("map_id"), MapIdFor(GetWorld()));
    Json->SetStringField(TEXT("checkpoint_id"), CheckpointId); HeliosLoadout::WriteJson(Json,SavedLoadout);
    Json->SetArrayField(TEXT("explored_zones"), SetJson(Explored)); Json->SetArrayField(TEXT("discovered_memes"), SetJson(Discovered));
    Json->SetArrayField(TEXT("completed_encounters"), SetJson(Completed));
    Json->SetNumberField(TEXT("exploration_score"),ExplorationScore);
    FString Text; FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text));
    const bool bSaved=AtomicTownJson(TownSavePath(GetWorld()),Text);
    bool bBestSaved=true;
    if (NewBest>PreviousBest || NewBestSeconds>PreviousSeconds || !FPaths::FileExists(TownBestPath(GetWorld())))
    {
        auto Best=MakeShared<FJsonObject>(); Best->SetNumberField(TEXT("version"),1); Best->SetStringField(TEXT("map_id"),MapIdFor(GetWorld()));
        Best->SetNumberField(TEXT("best_score"),NewBest); Best->SetNumberField(TEXT("best_seconds"),NewBestSeconds);
        FString BestText; FJsonSerializer::Serialize(Best,TJsonWriterFactory<>::Create(&BestText));
        bBestSaved=AtomicTownJson(TownBestPath(GetWorld()),BestText);
    }
    if (!bSaved) UE_LOG(LogHeliosOutpost, Warning, TEXT("IKUN_TOWN save failed; previous committed progress retained"));
    SinceSave = 0.f; return bSaved && bBestSaved;
}

void UHeliosTownSubsystem::AttachDirector(AHeliosAdventureDirector* InDirector)
{
    Director = InDirector;
    if (!bTown || !Director) return;
    Director->Player = Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
    Director->SelectedWeaponId = WeaponId; Director->SelectedLoadout=SavedLoadout;
    ReadTownBest(GetWorld(),Director->BestScore,Director->BestSeconds);
    Director->Score=Explored.Num()*100+Discovered.Num()*25+Completed.Num()*150+GetDefeatedBossCount()*500;
    Director->BestScore=FMath::Max(Director->BestScore,Director->Score);
    ResetSurvival(true);
    SelectRandomSpawnZone();
    if (Director->Player)
    {
        BeginStreamingArrival(GetCheckpointLocation());
    }
    UpdateReadiness();
}

void UHeliosTownSubsystem::BeginStreamingArrival(const FVector& Destination, bool bHoldMovementAfterReady)
{
    if (!Director || !Director->Player) return;
    // Checkpoints describe the ordinary pawn centre. Stream with that shape, then
    // deploy the full mech only after the destination's collision is stable.
    if(auto* F=Cast<AHeliosFighterWeapon>(Director->Player->GetEquippedWeapon()))F->SuspendForArrival();
    ArrivalMech=Cast<AHeliosMechWeapon>(Director->Player->GetEquippedWeapon());
    if(ArrivalMech.IsValid())ArrivalMech->SuspendForArrival();
    auto* Movement=Director->Player->GetCharacterMovement();
    Movement->StopMovementImmediately(); Movement->DisableMovement();
    ArrivalDestination=Destination; ArrivalReadySince=-1.; ArrivalGroundSignature.Reset(); ArrivalBlockReason=TEXT("streaming");
    bHoldArrivalMovement=bHoldMovementAfterReady; bRestorePending=true; bNavReady=false; bStreamingReady=false;
    Director->Player->SetActorLocation(Destination,false,nullptr,ETeleportType::TeleportPhysics);
    UE_LOG(LogHeliosOutpost,Display,TEXT("IKUN_TOWN arrival_begin target_cm=%s hold=%d"),*Destination.ToString(),bHoldArrivalMovement);
}

bool UHeliosTownSubsystem::ReleaseArrivalMovement()
{
    if (!Director || !Director->Player || !IsWorldReady()) return false;
    bHoldArrivalMovement=false;
    Director->Player->GetCharacterMovement()->SetMovementMode(Director->Player->IsFighterMode()?MOVE_Flying:MOVE_Walking);
    return true;
}

void UHeliosTownSubsystem::UpdateReadiness()
{
    if (!Director || !Director->Player) return;
    bStreamingReady = true;
    if (const auto* Partition = GetWorld()->GetSubsystem<UWorldPartitionSubsystem>()) bStreamingReady = Partition->IsStreamingCompleted();
    for (const ULevelStreaming* Level : GetWorld()->GetStreamingLevels())
        if (Level && Level->ShouldBeVisible() && !Level->IsLevelVisible()) bStreamingReady = false;
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (Nav && !bInvoked) { Nav->RegisterNavigationInvoker(Director->Player, 12000.f, 16000.f); bInvoked = true; }
    FNavLocation Projected;
    bNavReady = Nav && Nav->ProjectPointToNavigation(Director->Player->GetGameplayReference(), Projected, FVector(100,100,250));
    FHitResult Ground;
    const FVector Position = Director->Player->GetGameplayReference();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(TownReady), false, Director->Player);
    // Movement needs a walkable Pawn-blocking floor, not merely a visible surface.
    const bool bFloor=GetWorld()->LineTraceSingleByChannel(Ground,Position+FVector(0,0,100),Position-FVector(0,0,300),ECC_Pawn,Params)
        && Director->Player->GetCharacterMovement()->IsWalkable(Ground);
    bStreamingReady &= bFloor;
    if (bStreamingReady && !bNavReady && Nav)
    {
        const double Now=GetWorld()->GetTimeSeconds();
        if (MissingNavigationSince<0.) MissingNavigationSince=Now;
        // A streamed collision component can arrive after an invoker tile was first requested.
        // Requeue only nearby geometry; readiness still requires a real navigation projection.
        if (Now-MissingNavigationSince>=2. && Now-LastNavigationRepairTime>=5.)
        {
            const FVector Extent(2000,2000,600);
            Nav->AddDirtyArea(FBox(Position-Extent,Position+Extent),ENavigationDirtyFlag::All,TEXT("IkunTownStreamedGroundRecovery"));
            LastNavigationRepairTime=Now; ++NavigationRepairCount;
            UE_LOG(LogHeliosOutpost,Display,TEXT("IKUN_TOWN local_nav_repair=%d time=%.2f position_cm=%s radius_cm=2000"),
                NavigationRepairCount,Now,*Position.ToString());
        }
    }
    else MissingNavigationSince=-1.;
    if (bRestorePending)
    {
        auto* Movement=Director->Player->GetCharacterMovement();
        Movement->StopMovementImmediately(); Movement->DisableMovement();
        const bool bAtDestination=FVector::Dist2D(Position,ArrivalDestination)<=25.f && FMath::Abs(Position.Z-ArrivalDestination.Z)<=100.f;
        const FString Signature=GetPathNameSafe(Ground.GetComponent())+FString::FromInt(Ground.Item);
        const double Now=GetWorld()->GetTimeSeconds();
        if (!bConfigLoaded || !bStreamingReady || !bNavReady || !bAtDestination)
        {
            ArrivalReadySince=-1.;
            ArrivalBlockReason=!bAtDestination?TEXT("displaced"):!bStreamingReady?TEXT("streaming_or_pawn_floor"):!bNavReady?TEXT("navigation"):TEXT("configuration");
        }
        else if (ArrivalReadySince<0. || Signature!=ArrivalGroundSignature || FMath::Abs(Ground.ImpactPoint.Z-ArrivalGroundZ)>2.f)
        {
            ArrivalReadySince=Now; ArrivalGroundSignature=Signature; ArrivalGroundZ=Ground.ImpactPoint.Z; ArrivalBlockReason=TEXT("stabilizing");
        }
        else if (Now-ArrivalReadySince>=0.75)
        {
            const auto* Capsule=Director->Player->GetCapsuleComponent();
            // A centre ray can hit below a curb touched by the capsule edge. Use the same
            // swept floor calculation as walking, so arrival never lowers the pawn into it.
            FFindFloorResult CapsuleFloor; Movement->FindFloor(Position,CapsuleFloor,false);
            if (!CapsuleFloor.IsWalkableFloor()) { ArrivalBlockReason=TEXT("capsule_floor"); return; }
            const FVector Supported=Position+FVector(0,0,2.5f-CapsuleFloor.GetDistanceToFloor());
            const bool bClear=!GetWorld()->OverlapBlockingTestByProfile(Supported,FQuat::Identity,TEXT("Pawn"),
                FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()),Params);
            ArrivalBlockReason=bClear?TEXT(""):TEXT("supported_capsule_overlap");
            if (bClear)
            {
                Director->Player->SetActorLocation(Supported,false,nullptr,ETeleportType::TeleportPhysics);
                bRestorePending=false;
                if(auto* F=Cast<AHeliosFighterWeapon>(Director->Player->GetEquippedWeapon()))
                {
                    if(!F->TryResumeAfterArrival())
                    {
                        LastMessage=TEXT("附近空间不足，无法部署隼影战斗机。");bHoldArrivalMovement=false;
                        ReleaseArrivalMovement();Director->EnterWeaponSelection();
                        if(auto* PC=UGameplayStatics::GetPlayerController(this,0))if(auto* HUD=Cast<AHeliosAdventureHUD>(PC->GetHUD()))HUD->ShowDeploymentFailure(LastMessage);
                        return;
                    }
                }
                if(auto* Mech=ArrivalMech.Get())
                {
                    ArrivalMech.Reset();
                    if(Director->Player->GetEquippedWeapon()==Mech)
                    {
                        if(!Mech->TryResumeAfterArrival())
                        {
                            LastMessage=Director->Player->GetMechDeploymentError();
                            ArrivalBlockReason=TEXT("mech_deployment_space");bHoldArrivalMovement=false;
                            ReleaseArrivalMovement();Director->EnterWeaponSelection();
                            if(auto* PC=UGameplayStatics::GetPlayerController(this,0))
                                if(auto* HUD=Cast<AHeliosAdventureHUD>(PC->GetHUD()))HUD->ShowDeploymentFailure(LastMessage);
                            return;
                        }
                        ArrivalDestination=Director->Player->GetGameplayReference();
                    }
                }
                UE_LOG(LogHeliosOutpost,Display,TEXT("IKUN_TOWN arrival_ready target_cm=%s actual_cm=%s stable_seconds=%.3f hold=%d"),
                    *ArrivalDestination.ToString(),*Supported.ToString(),Now-ArrivalReadySince,bHoldArrivalMovement);
                if (!bHoldArrivalMovement) ReleaseArrivalMovement();
            }
        }
    }
    else if (bHoldArrivalMovement)
    {
        Director->Player->GetCharacterMovement()->StopMovementImmediately();
        Director->Player->GetCharacterMovement()->DisableMovement();
    }
}

void UHeliosTownSubsystem::StartTown(FName InWeaponId)
{
    FHeliosLoadout Loadout=SavedLoadout;Loadout.PrimaryWeaponId=InWeaponId;
    StartTownWithLoadout(Loadout);
}
void UHeliosTownSubsystem::StartTownWithLoadout(const FHeliosLoadout& Loadout)
{
    FString Error;if(!Director||!Director->Player||!FHeliosLoadout::Validate(Loadout,Error)||Loadout.CharacterId==TEXT("sword_dog"))return;
    SavedLoadout=FHeliosLoadout::ResolveLegacyVariant(Loadout,GetWorld()?GetWorld()->GetGameInstance():nullptr); WeaponId=SavedLoadout.PrimaryWeaponId;
    if (!bSessionStarted)
    {
        ResetUnfinishedEncounters();
        Director->Player->ResetForTest();
        BeginStreamingArrival(GetCheckpointLocation());
        bSessionStarted=true;
    }
    Director->RunSpawnLocation = GetCheckpointLocation();
    Director->bRunOver = false; Director->bSelectingWeapon = false;
    Director->ExploredZones = Explored;
    SaveProgress();
}

void UHeliosTownSubsystem::TickTown(float DeltaSeconds)
{
    if (!bTown || !Director || !Director->Player || !bConfigLoaded) return;
    const bool bWasReady=IsWorldReady();
    UpdateReadiness();
    if (bRespawning)
    {
        if (GetWorld()->GetTimeSeconds() >= RespawnTime) FinishRespawn();
        return;
    }
    if (!bWasReady || !GetTimerStopReason().IsEmpty()) return;
    Director->SurvivalSeconds += DeltaSeconds;
    UpdateSurvival(DeltaSeconds);
    UpdateExploration();
    SinceAttempt += DeltaSeconds; SinceSave += DeltaSeconds;
    if (SinceAttempt >= 0.75f) { SinceAttempt = 0.f; UpdateSupplies(); }
    if (SinceSave >= 30.f) SaveProgress();
}

void UHeliosTownSubsystem::SelectRandomSpawnZone()
{
    if (!bConfigLoaded || Zones.IsEmpty()) return;
    // Roll once per birth, independently of saved progress and explored districts.
    // Keep the chosen checkpoint stable while its world partition cells load.
    const auto& Zone = Zones[FMath::RandRange(0, Zones.Num() - 1)];
    CheckpointId = Zone.Id;
    CurrentZoneId = Zone.Id;
    if (Director) Director->RunSpawnLocation = Zone.Checkpoint;
    UE_LOG(LogHeliosOutpost, Display, TEXT("IKUN_TOWN random_spawn zone=%s target_cm=%s"), *Zone.Id, *Zone.Checkpoint.ToString());
}

FVector UHeliosTownSubsystem::GetCheckpointLocation() const
{
    for (const auto& Zone : Zones) if (Zone.Id == CheckpointId) return Zone.Checkpoint;
    return Zones.IsEmpty() ? FVector(0,0,110) : Zones[0].Checkpoint;
}
FString UHeliosTownSubsystem::GetCurrentZoneName() const
{
    for (const auto& Zone : Zones) if (Zone.Id == CurrentZoneId) return Zone.Name;
    return TEXT("小镇郊野");
}
int32 UHeliosTownSubsystem::GetDefeatedBossCount() const
{
    int32 Count = 0; for (const auto& E : Encounters) if (E.bBoss && Completed.Contains(E.Id)) ++Count;
    return Count;
}

void UHeliosTownSubsystem::UpdateExploration()
{
    const FVector Location = Director->Player->GetGameplayReference();
    CurrentZoneId.Reset();
    for (const auto& Zone : Zones)
    {
        if (FBox(Zone.Center-Zone.Extent, Zone.Center+Zone.Extent).IsInsideOrOn(Location))
        {
            CurrentZoneId = Zone.Id;
            if (!Explored.Contains(Zone.Id)) { Explored.Add(Zone.Id); Director->ExploredZones = Explored; SaveProgress(); }
        }
        if (FVector::Dist2D(Location, Zone.Checkpoint) <= 750.f && CheckpointId != Zone.Id)
        {
            CheckpointId = Zone.Id; LastMessage = FString::Printf(TEXT("已保存检查点：%s"), *Zone.Name); SaveProgress();
        }
    }
    Director->CurrentZoneName = GetCurrentZoneName();
    for (const auto& Meme : Memes)
        if (!Discovered.Contains(Meme.Id)
            && (Meme.Kind == TEXT("landmark") || Meme.Kind == TEXT("static"))
            && FVector::Dist(Location,Meme.Position) < Meme.DiscoveryRadius
            && (!Meme.bIndoorDiscovery || CanDiscoverIndoor(Meme))) UnlockMeme(Meme.Id);
}

bool UHeliosTownSubsystem::CanDiscoverIndoor(const FHeliosTownMeme& Meme) const
{
    if (!Director || !Director->Player) return false;
    const auto* Player = Director->Player.Get();
    if (!FBox(Meme.SpaceCenter-Meme.SpaceExtent,Meme.SpaceCenter+Meme.SpaceExtent).IsInsideOrOn(Player->GetActorLocation())) return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(VillageLoreVisibility),false,Player);
    FHitResult Hit;
    return !GetWorld()->LineTraceSingleByChannel(Hit,Player->GetPawnViewLocation(),Meme.Position,ECC_Visibility,Params);
}

void UHeliosTownSubsystem::ResetUnfinishedEncounters()
{
    if (Director) Director->CleanupOwnedActors();
    for (TActorIterator<AHeliosTownBall> It(GetWorld()); It; ++It) It->Destroy();
    for (auto& Pair:SupplyActors) if (Pair.Value.IsValid()) Pair.Value->Destroy();
    SupplyActors.Reset();
    for (const auto& Supply:Supplies)
    {
        bool bHasEncounter=false, bAllCompleted=true;
        for (const auto& E:Encounters) if (E.ZoneId==Supply.ZoneId) { bHasEncounter=true; bAllCompleted &= Completed.Contains(E.Id); }
        if (!bHasEncounter || !bAllCompleted) ConsumedSupplies.Remove(Supply.Id);
    }
    for (auto& E : Encounters) { E.Actors.Reset(); E.Spawned = E.Defeated = E.SpawnAttempts = 0; E.LastSpawnFailure.Reset(); E.bTriggered = false; }
}

float UHeliosTownSubsystem::GetConfiguredRespawnDelay() const
{
    return UHeliosCombatTempoSettings::Resolve(this, EHeliosTempoFile::Recovery,
        TEXT("玩家复活秒"), TEXT("小镇死亡复活"), 1.5f);
}

void UHeliosTownSubsystem::RespawnAtCheckpoint()
{
    if (!bTown || !Director || bRespawning) return;
    SaveProgress(); ResetUnfinishedEncounters();
    ResetSurvival(false);
    const float Delay = GetConfiguredRespawnDelay();
    bRespawning = true; RespawnTime = GetWorld()->GetTimeSeconds() + Delay;
    LastMessage = TEXT("正在随机选择复活街区（8 选 1）");
}
void UHeliosTownSubsystem::FinishRespawn()
{
    if (!Director || !Director->Player) return;
    Director->Player->ResetForTest();
    SelectRandomSpawnZone();
    BeginStreamingArrival(GetCheckpointLocation());
    bRespawning = false;
    LastMessage = FString::Printf(TEXT("已满血复活于%s，生存时间保留，战斗将在准备后继续"), *GetCurrentZoneName());
    SaveProgress();
}

bool UHeliosTownSubsystem::ResetProgress()
{
    if (!bTown || !bConfigLoaded) return false;
    SaveProgress(); // Commit the completed journey's independent record before clearing current progress.
    ResetUnfinishedEncounters(); Explored.Reset(); Discovered.Reset(); Completed.Reset(); CheckpointId.Reset(); CurrentZoneId.Reset();
    SavedLoadout=FHeliosLoadout::MakeDefault(); WeaponId=SavedLoadout.PrimaryWeaponId; bRespawning=false; ConsumedSupplies.Reset();
    ResetSurvival(true); bSessionStarted=false;
    SelectRandomSpawnZone();
    if (Director && Director->Player)
    {
        Director->ExploredZones.Reset(); Director->BossKills = 0; Director->Kills = 0; Director->Score = 0; Director->SurvivalSeconds=0.f;
        Director->Player->ResetForTest(); BeginStreamingArrival(GetCheckpointLocation());
    }
    return SaveProgress();
}

bool UHeliosTownSubsystem::TeleportToCheckpoint(const FString& ZoneId)
{
    if (!bTown || !Director || !Director->Player) return false;
    for (const auto& Zone : Zones) if (Zone.Id == ZoneId)
    {
        ResetUnfinishedEncounters(); CheckpointId = ZoneId;
        Director->Player->CancelCombatAndControl();
        BeginStreamingArrival(Zone.Checkpoint);
        return SaveProgress();
    }
    return false;
}

bool UHeliosTownSubsystem::FindSafeSpawn(const FVector& Candidate, bool bBoss, int32 Type, FVector& Result, FString* Failure) const
{
    const auto Reject=[Failure](const TCHAR* Reason) { if (Failure) *Failure=Reason; return false; };
    if (!IsWorldReady() || !Director || !Director->Player) return Reject(TEXT("world_not_ready"));
    const FVector PlayerLocation = Director->Player->GetGameplayReference();
    const auto Definition = FHeliosEnemyDefinition::ForType(bBoss ? EHeliosEnemyType::E05_Boss : static_cast<EHeliosEnemyType>(Type));
    if (FVector::Dist2D(PlayerLocation, Candidate) < 2000.f || FVector::Dist2D(PlayerLocation, Candidate) > 8000.f) return Reject(TEXT("distance"));
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()); FNavLocation Projected,Start;
    const FNavAgentProperties Agent(Definition.CapsuleRadius,Definition.CapsuleHalfHeight*2.f);
    const ANavigationData* NavData=Nav?Nav->GetNavDataForProps(Agent,PlayerLocation):nullptr;
    if (!NavData || !Nav->ProjectPointToNavigation(Candidate+FVector(0,0,80),Projected,FVector(150,150,300),NavData)
        || !Nav->ProjectPointToNavigation(PlayerLocation,Start,FVector(100,100,250),NavData)) return Reject(TEXT("candidate_navigation"));
    const double SpawnDistance=FVector::Dist2D(PlayerLocation,Projected.Location);
    if (SpawnDistance<2000. || SpawnDistance>8000.) return Reject(TEXT("projected_distance"));
    for (const auto& Zone:Zones)
        if (FVector::Dist2D(Projected.Location,Zone.Checkpoint)<1200.f) return Reject(TEXT("checkpoint_protection"));
    FPathFindingQuery Query(Director->Player,*NavData,Start.Location,Projected.Location); Query.SetAllowPartialPaths(false);
    const FPathFindingResult Path=Nav->FindPathSync(Agent,Query);
    if (!Path.IsSuccessful() || !Path.Path.IsValid() || !Path.Path->IsValid() || Path.Path->IsPartial()) return Reject(TEXT("unreachable"));
    FCollisionQueryParams PathParams(SCENE_QUERY_STAT(TownSpawnPath),false,Director->Player);
    const auto& Points=Path.Path->GetPathPoints();
    if (Points.Num()>128) return Reject(TEXT("path_too_complex"));
    for (int32 Index=1;Index<Points.Num();++Index)
    {
        FHitResult Hit; const FVector Lift(0,0,Definition.CapsuleHalfHeight+3.f);
        if (GetWorld()->SweepSingleByProfile(Hit,Points[Index-1].Location+Lift,Points[Index].Location+Lift,FQuat::Identity,TEXT("Pawn"),
            FCollisionShape::MakeCapsule(Definition.CapsuleRadius,Definition.CapsuleHalfHeight),PathParams)) return Reject(TEXT("path_capsule_blocked"));
    }
    Result = Projected.Location + FVector(0,0,Definition.CapsuleHalfHeight + 3.f);
    if (Director->IsCandidateVisible(Result)) return Reject(TEXT("visible"));
    FCollisionQueryParams Params(SCENE_QUERY_STAT(TownSpawn), false, Director->Player);
    if (GetWorld()->OverlapBlockingTestByProfile(Result, FQuat::Identity, TEXT("Pawn"), FCollisionShape::MakeCapsule(Definition.CapsuleRadius, Definition.CapsuleHalfHeight), Params)) return Reject(TEXT("capsule_overlap"));
    return true;
}

bool UHeliosTownSubsystem::TrySpawn(FHeliosTownEncounter& E)
{
    ++E.SpawnAttempts; E.LastSpawnFailure.Reset();
    if ((E.bBoss && Director->GetBossCount() >= 1) || (!E.bBoss && Director->GetAliveCount() >= GetSurvivalEnemyCap())) { E.LastSpawnFailure=TEXT("alive_cap"); return false; }
    const int32 Type = E.EnemyTypes[E.Spawned % E.EnemyTypes.Num()];
    TArray<FVector> Candidates=E.Spawns;
    for (int32 Index = 0; Index < Candidates.Num(); ++Index)
    {
        const FVector Candidate = Candidates[(Index + E.Spawned) % Candidates.Num()]; FVector Position;
        FString Failure;
        if (!FindSafeSpawn(Candidate, E.bBoss, Type, Position,&Failure)) { E.LastSpawnFailure+=FString::Printf(TEXT("%d:%s;"),Index,*Failure); continue; }
        TSubclassOf<AHeliosEnemyActor> Class = Director->ClassForType(static_cast<EHeliosEnemyType>(Type));
        if (E.bBoss) Class = AHeliosEnemyBoss::StaticClass();
        const auto Definition=FHeliosEnemyDefinition::ForType(E.bBoss?EHeliosEnemyType::E05_Boss:static_cast<EHeliosEnemyType>(Type));
        // SpawnActor checks the class template before deferred initialization; FinishSpawning
        // checks again before BeginPlay applies the enemy definition. Keep both real checks.
        const auto* TemplateCapsule=Class.GetDefaultObject()->GetCapsuleComponent();
        const FVector ConstructionPosition=Position+FVector(0,0,FMath::Max(0.f,TemplateCapsule->GetScaledCapsuleHalfHeight()-Definition.CapsuleHalfHeight));
        const FTransform Transform(FRotator::ZeroRotator, Position);
        auto* Enemy = GetWorld()->SpawnActorDeferred<AHeliosEnemyActor>(Class,FTransform(FRotator::ZeroRotator,ConstructionPosition),Director,nullptr,ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding);
        if (!Enemy) { E.LastSpawnFailure+=TEXT("spawn_collision;"); continue; }
        Enemy->GetCapsuleComponent()->SetCapsuleSize(Definition.CapsuleRadius,Definition.CapsuleHalfHeight);
        Enemy->SetRuntimeIdentity(FString::Printf(TEXT("town_%s_%d"), *E.Id,Director->SpawnSerial+1), E.Id);
        if (auto* Boss = Cast<AHeliosEnemyBoss>(Enemy)) Boss->SetBossNumber(BossSpawnedTotal+1);
        Enemy->FinishSpawning(Transform);
        if (!IsValid(Enemy) || Enemy->IsActorBeingDestroyed()) { E.LastSpawnFailure+=TEXT("finish_spawn_collision;"); continue; }
        E.Actors.Add(Enemy); ++E.Spawned; ++Director->SpawnSerial; E.LastSpawnFailure.Reset();
        UE_LOG(LogHeliosOutpost,Display,TEXT("IKUN_TOWN encounter_spawn id=%s type=%d spawned=%d position_cm=%s"),*E.Id,Type,E.Spawned,*Position.ToString());
        if (auto* Boss = Cast<AHeliosEnemyBoss>(Enemy)) { Director->Bosses.Add(Boss); ++Director->BossSpawnSerial; }
        else Director->Enemies.Add(Enemy);
        return true;
    }
    if (E.SpawnAttempts==1 || E.SpawnAttempts%10==0)
        UE_LOG(LogHeliosOutpost,Display,TEXT("IKUN_TOWN encounter_wait id=%s attempts=%d reasons=%s"),*E.Id,E.SpawnAttempts,*E.LastSpawnFailure);
    return false;
}

int32 UHeliosTownSubsystem::GetSurvivalEnemyCap() const
{
    return 30;
}
float UHeliosTownSubsystem::GetSecondsUntilNextBoss() const
{
    return bBossPending?0.f:FMath::Max(0.f,NextBossAt-(Director?Director->SurvivalSeconds:0.f));
}
FString UHeliosTownSubsystem::GetTimerStopReason() const
{
    if (!Director || !Director->Player || !bSessionStarted || Director->bSelectingWeapon) return TEXT("preparation");
    if (GetWorld()->IsPaused()) return TEXT("paused");
    if (bRespawning || Director->Player->IsDead()) return TEXT("respawning");
    if (!IsWorldReady() || bHoldArrivalMovement) return TEXT("loading");
    return FString();
}
void UHeliosTownSubsystem::ResetSurvival(bool bResetClock)
{
    if (bResetClock && Director)
    {
        Director->SurvivalSeconds=0.f; Director->Kills=0; Director->BossKills=0;
        NormalSpawnedTotal=BossSpawnedTotal=0;
    }
    const float Now=Director?Director->SurvivalSeconds:0.f;
    NextNormalAt=Now+10.f; NextBossAt=NextBossAttemptAt=Now+60.f; bBossPending=false;
    SurvivalNormal=FHeliosTownEncounter(); SurvivalNormal.Id=TEXT("survival_normal"); SurvivalNormal.EnemyTypes={1,2,3,4};
    SurvivalBoss=FHeliosTownEncounter(); SurvivalBoss.Id=TEXT("survival_boss"); SurvivalBoss.bBoss=true; SurvivalBoss.EnemyTypes={5};
}
void UHeliosTownSubsystem::BuildSurvivalCandidates(FHeliosTownEncounter& Encounter)
{
    Encounter.Center=Director->Player->GetGameplayReference(); Encounter.Spawns.Reset();
    // 24 local, deterministic candidates; rotate retries to avoid repeating a blind alley.
    const float Turn=FMath::DegreesToRadians(static_cast<float>(Encounter.SpawnAttempts*37%360));
    for (int32 Ring=0;Ring<3;++Ring) for (int32 Sector=0;Sector<8;++Sector)
    {
        const float Angle=Turn+Sector*(2.f*PI/8.f);
        const float Radius=2500.f+Ring*2500.f;
        Encounter.Spawns.Add(Encounter.Center+FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,0));
    }
}
void UHeliosTownSubsystem::UpdateSurvival(float DeltaSeconds)
{
    const FVector PlayerPosition=Director->Player->GetGameplayReference();
    bool bKillsChanged=false;
    // Remove only confirmed deaths from the population. Reclamation never awards a kill.
    auto UpdateActors=[&](auto& Actors,bool bBoss)
    {
        for (int32 Index=Actors.Num()-1;Index>=0;--Index)
        {
            AHeliosEnemyActor* Enemy=Actors[Index];
            if (!IsValid(Enemy)) { Actors.RemoveAtSwap(Index); continue; }
            if (Enemy->IsEnemyDead())
            {
                ++Director->Kills; if (bBoss) ++Director->BossKills;
                Enemy->SetLifeSpan(5.f); Director->HiddenFarSeconds.Remove(Enemy); Actors.RemoveAtSwap(Index); bKillsChanged=true;
                continue;
            }
            float& Hidden=Director->HiddenFarSeconds.FindOrAdd(Enemy);
            if (FVector::Dist2D(PlayerPosition,Enemy->GetActorLocation())>16000.f && !Director->IsCandidateVisible(Enemy->GetActorLocation(),Enemy))
                Hidden+=DeltaSeconds;
            else Hidden=0.f;
            if (Hidden>=5.f)
            {
                Director->HiddenFarSeconds.Remove(Enemy); Director->DestroyEnemy(Enemy); Actors.RemoveAtSwap(Index);
            }
        }
    };
    UpdateActors(Director->Enemies,false); UpdateActors(Director->Bosses,true);
    if (bKillsChanged) SaveProgress();
    const float Now=Director->SurvivalSeconds;
    if (Now>=NextNormalAt)
    {
        if (Director->GetAliveCount()<GetSurvivalEnemyCap())
        {
            BuildSurvivalCandidates(SurvivalNormal);
            if (TrySpawn(SurvivalNormal)) { ++NormalSpawnedTotal; NextNormalAt=Now+2.f; }
            else NextNormalAt=Now+1.f;
        }
        else { SurvivalNormal.LastSpawnFailure=TEXT("alive_cap"); NextNormalAt=Now+1.f; }
    }
    if (Now>=NextBossAt)
    {
        bBossPending=true;
        do { NextBossAt+=60.f; } while (NextBossAt<=Now);
    }
    if (bBossPending && Now>=NextBossAttemptAt)
    {
        if (Director->GetBossCount()<1)
        {
            BuildSurvivalCandidates(SurvivalBoss);
            if (TrySpawn(SurvivalBoss)) { ++BossSpawnedTotal; bBossPending=false; }
        }
        else SurvivalBoss.LastSpawnFailure=TEXT("boss_alive");
        NextBossAttemptAt=Now+1.f;
    }
    // The Director owns live actors; weak spawn-history arrays need not grow for a long session.
    SurvivalNormal.Actors.Reset(); SurvivalBoss.Actors.Reset();
}
const FHeliosTownMeme* UHeliosTownSubsystem::NearestMeme() const
{
    if (!Director || !Director->Player) return nullptr;
    const FHeliosTownMeme* Best = nullptr; double Distance = 450.;
    for (const auto& Meme : Memes)
    {
        if (Meme.Kind == TEXT("landmark") || Meme.Kind == TEXT("static")) continue;
        const double D = FVector::Dist(Director->Player->GetGameplayReference(), Meme.Position);
        if (D < Distance) { Distance = D; Best = &Meme; }
    }
    return Best;
}
bool UHeliosTownSubsystem::UnlockMeme(const FString& MemeId)
{
    if (!bTown || Discovered.Contains(MemeId)) return false;
    for (const auto& Meme : Memes) if (Meme.Id == MemeId)
    { Discovered.Add(MemeId); LastMessage = TEXT("发现彩蛋：") + Meme.Name; SaveProgress(); return true; }
    return false;
}
bool UHeliosTownSubsystem::TriggerEncounter(const FString& Id)
{
    // Kept for old callers/save compatibility. Fixed encounter IDs no longer control combat.
    return false;
}
bool UHeliosTownSubsystem::Interact(AHeliosAdventureCharacter* Character)
{
    if (!bTown || !Director || Character != Director->Player || Character->IsDead() || GetWorld()->IsPaused()) return false;
    return InteractNearest();
}
bool UHeliosTownSubsystem::InteractNearest()
{
    const auto* Meme = NearestMeme(); if (!Meme) return false;
    if (GetWorld()->GetTimeSeconds() - LastInteractionTime < 1.0) return false;
    LastInteractionTime = GetWorld()->GetTimeSeconds();
    if (Meme->Kind == TEXT("basketball") || Meme->Kind == TEXT("vending"))
    {
        int32 Count = 0; for (TActorIterator<AHeliosTownBall> It(GetWorld()); It; ++It) ++Count;
        if (Count >= 12) return false;
        FVector Eyes; FRotator View; Director->Player->GetActorEyesViewPoint(Eyes, View);
        FVector Start = Eyes + View.Vector() * 85.f;
        if (Meme->Kind == TEXT("vending")) Start = Meme->Position + (Director->Player->GetGameplayReference()-Meme->Position).GetSafeNormal2D() * 95.f - FVector(0,0,45);
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
        auto* Ball = GetWorld()->SpawnActor<AHeliosTownBall>(AHeliosTownBall::StaticClass(), Start, FRotator::ZeroRotator, Params);
        if (!Ball) return false;
        if (Meme->Kind == TEXT("basketball"))
        {
            Ball->ThrowAtHoop(Meme->bHasTarget ? Meme->Target : Meme->Position+FVector(0,350,175), Meme->Id);
            LastMessage = TEXT("篮球已出手——落入篮筐即可点亮彩蛋");
            return true;
        }
        Ball->Roll(Director->Player->GetGameplayReference()-Meme->Position);
        Director->Player->Heal(35.f);
    }
    if (Meme->Kind == TEXT("lights"))
    {
        bool bChanged = false;
        for (TActorIterator<AActor> It(GetWorld()); It; ++It)
        {
            if (!It->ActorHasTag(FName(*Meme->Id)) && !It->ActorHasTag(FName(*(TEXT("MemeId=")+Meme->Id)))) continue;
            TInlineComponentArray<ULightComponent*> Lights(*It);
            for (auto* Light : Lights) { Light->SetVisibility(!Light->IsVisible()); bChanged = true; }
        }
        if (!bChanged) { LastMessage = TEXT("舞台灯光正在载入"); return false; }
    }
    if (Meme->Kind == TEXT("certificate"))
    {
        auto* Proof = GetWorld()->SpawnActor<AActor>();
        if (Proof)
        {
            auto* Text = NewObject<UTextRenderComponent>(Proof); Proof->SetRootComponent(Text); Text->RegisterComponent();
            Text->SetText(FText::FromString(TEXT("IKUN\nPRACTICE CERTIFICATE\n")+Meme->Id));
            Text->SetTextRenderColor(FColor(255,212,76)); Text->SetWorldSize(18.f); Text->SetHorizontalAlignment(EHTA_Center);
            Proof->SetActorLocation(Meme->Position+FVector(0,0,90));
            Proof->SetActorRotation((Director->Player->GetGameplayReference()-Proof->GetActorLocation()).Rotation()); Proof->SetLifeSpan(6.f);
        }
    }
    const bool bNew = UnlockMeme(Meme->Id);
    if (auto* Player = Cast<AHeliosAdventureCharacter>(Director->Player)) Player->PlayAdventureCue(TEXT("Pickup"));
    return bNew || Discovered.Contains(Meme->Id);
}

void UHeliosTownSubsystem::RecordBasketballScore(const FString& Id)
{
    ++BasketballScores;
    UnlockMeme(Id);
    LastMessage = FString::Printf(TEXT("三分命中！已完成 %d 次投篮"), BasketballScores);
    if (Director) Director->PlayPlayerCue(TEXT("Pickup"));
}

void UHeliosTownSubsystem::UpdateSupplies()
{
    if (!Director || !Director->Player || !IsWorldReady()) return;
    const FVector Position=Director->Player->GetGameplayReference();
    for (auto It=SupplyActors.CreateIterator(); It; ++It)
        if (!It.Value().IsValid() || FVector::Dist2D(Position,It.Value()->GetActorLocation())>10000.f)
        { if (It.Value().IsValid()) It.Value()->Destroy(); It.RemoveCurrent(); }
    TArray<const FHeliosTownMeme*> Nearby;
    for (const auto& Supply:Supplies)
        if (!ConsumedSupplies.Contains(Supply.Id) && FVector::Dist2D(Position,Supply.Position)<=10000.f) Nearby.Add(&Supply);
    Nearby.Sort([&](const FHeliosTownMeme& A,const FHeliosTownMeme& B){return FVector::DistSquared(Position,A.Position)<FVector::DistSquared(Position,B.Position);});
    for (const auto* Supply:Nearby)
    {
        if (FVector::Dist2D(Position,Supply->Position)<=150.f && FMath::Abs(Position.Z-Supply->Position.Z)<220.f
            && Director->Player->Heal(35.f)>0.f)
        {
            ConsumedSupplies.Add(Supply->Id);
            if (auto* Actor=SupplyActors.Find(Supply->Id)) { if (Actor->IsValid()) Actor->Get()->Destroy(); SupplyActors.Remove(Supply->Id); }
            Director->PlayPlayerCue(TEXT("Pickup")); LastMessage=TEXT("医疗包：恢复35点生命"); continue;
        }
        if (SupplyActors.Contains(Supply->Id) || SupplyActors.Num()>=8) continue;
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        if (auto* Actor=GetWorld()->SpawnActor<AHeliosTownSupply>(AHeliosTownSupply::StaticClass(),Supply->Position,FRotator::ZeroRotator,Params))
        { Actor->Configure(Supply->Id); SupplyActors.Add(Supply->Id,Actor); }
    }
}

FString UHeliosTownSubsystem::GetStateJson() const
{
    auto Json = MakeShared<FJsonObject>();
    Json->SetBoolField(TEXT("ok"), bTown && bConfigLoaded); Json->SetStringField(TEXT("map_id"), MapIdFor(GetWorld()));
    Json->SetStringField(TEXT("display_name"), FHeliosMapRegistry::ForWorld(GetWorld())->DisplayName.ToString()); Json->SetNumberField(TEXT("version"), 1);
    Json->SetNumberField(TEXT("score"),Explored.Num()*100+Discovered.Num()*25+Completed.Num()*150+GetDefeatedBossCount()*500);
    Json->SetNumberField(TEXT("best_score"),Director ? Director->BestScore : 0);
    Json->SetNumberField(TEXT("best_seconds"),Director ? Director->BestSeconds : 0.f);
    Json->SetNumberField(TEXT("survival_seconds"),Director ? Director->SurvivalSeconds : 0.f);
    const float Seconds=Director?Director->SurvivalSeconds:0.f;
    Json->SetStringField(TEXT("mode"),TEXT("survival")); Json->SetStringField(TEXT("ruleset"),TEXT("survival"));
    const FString StopReason=GetTimerStopReason();
    Json->SetBoolField(TEXT("timer_running"),StopReason.IsEmpty()); Json->SetStringField(TEXT("timer_stop_reason"),StopReason);
    Json->SetNumberField(TEXT("kills"),Director?Director->Kills:0);
    Json->SetNumberField(TEXT("session_boss_kills"),Director?Director->BossKills:0);
    Json->SetNumberField(TEXT("normal_spawned_total"),NormalSpawnedTotal); Json->SetNumberField(TEXT("boss_spawned_total"),BossSpawnedTotal);
    Json->SetNumberField(TEXT("boss_pending"),GetPendingBossCount()); Json->SetNumberField(TEXT("pending_boss_count"),GetPendingBossCount());
    Json->SetNumberField(TEXT("boss_next_seconds"),GetSecondsUntilNextBoss()); Json->SetNumberField(TEXT("enemy_next_seconds"),FMath::Max(0.f,NextNormalAt-Seconds));
    Json->SetNumberField(TEXT("next_boss_seconds"),GetSecondsUntilNextBoss());
    Json->SetNumberField(TEXT("next_enemy_at"),NextNormalAt); Json->SetNumberField(TEXT("next_boss_at"),NextBossAt);
    Json->SetStringField(TEXT("spawn_failure"),SurvivalNormal.LastSpawnFailure); Json->SetStringField(TEXT("boss_spawn_failure"),SurvivalBoss.LastSpawnFailure);
    Json->SetNumberField(TEXT("spawn_attempts"),SurvivalNormal.SpawnAttempts); Json->SetNumberField(TEXT("boss_spawn_attempts"),SurvivalBoss.SpawnAttempts);
    auto Rules=MakeShared<FJsonObject>();
    Rules->SetNumberField(TEXT("first_enemy_seconds"),10); Rules->SetNumberField(TEXT("enemy_interval_seconds"),2);
    Rules->SetNumberField(TEXT("cap_start"),GetSurvivalEnemyCap()); Rules->SetNumberField(TEXT("cap_step_seconds"),120); Rules->SetNumberField(TEXT("cap_max"),GetSurvivalEnemyCap());
    Rules->SetNumberField(TEXT("boss_interval_seconds"),60); Rules->SetNumberField(TEXT("boss_alive_max"),1); Rules->SetNumberField(TEXT("boss_pending_max"),1);
    Rules->SetNumberField(TEXT("spawn_min_m"),20); Rules->SetNumberField(TEXT("spawn_max_m"),80); Rules->SetNumberField(TEXT("retry_seconds"),1);
    Rules->SetNumberField(TEXT("checkpoint_protection_m"),12);
    Rules->SetNumberField(TEXT("candidate_limit"),24); Json->SetObjectField(TEXT("survival_rules"),Rules);
    Json->SetStringField(TEXT("phase"),Director && Director->IsSelectingWeapon() ? TEXT("weapon_selection") : TEXT("running"));
    Json->SetBoolField(TEXT("paused"),GetWorld() && GetWorld()->IsPaused());
    Json->SetStringField(TEXT("zone_id"), CurrentZoneId); Json->SetStringField(TEXT("zone"), GetCurrentZoneName());
    Json->SetStringField(TEXT("checkpoint_id"), CheckpointId); Json->SetArrayField(TEXT("checkpoint"), VectorJson(GetCheckpointLocation()));
    HeliosLoadout::WriteJson(Json,SavedLoadout); Json->SetNumberField(TEXT("explored_count"), Explored.Num());
    Json->SetNumberField(TEXT("meme_count"), Discovered.Num()); Json->SetNumberField(TEXT("boss_kills"),Director?Director->BossKills:0);
    Json->SetNumberField(TEXT("legacy_boss_kills"),GetDefeatedBossCount());
    Json->SetNumberField(TEXT("basketball_scores"), BasketballScores);
    Json->SetNumberField(TEXT("supply_actor_count"),SupplyActors.Num());
    Json->SetArrayField(TEXT("consumed_supplies"),SetJson(ConsumedSupplies));
    Json->SetNumberField(TEXT("alive_count"), Director ? Director->GetAliveCount() : 0); Json->SetNumberField(TEXT("boss_count"), Director ? Director->GetBossCount() : 0);
    Json->SetNumberField(TEXT("enemy_cap"),GetSurvivalEnemyCap()); Json->SetNumberField(TEXT("boss_cap"), 1);
    Json->SetNumberField(TEXT("nav_generation_radius_cm"), 12000); Json->SetNumberField(TEXT("nav_removal_radius_cm"), 16000);
    Json->SetNumberField(TEXT("nav_repair_count"),NavigationRepairCount);
    Json->SetNumberField(TEXT("last_nav_repair_seconds"),LastNavigationRepairTime<0.?-1.:LastNavigationRepairTime);
    if (auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
    {
        Json->SetBoolField(TEXT("nav_invoker_only"), Nav->IsActiveTilesGenerationEnabled());
        if (auto* Recast = Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)))
            Json->SetNumberField(TEXT("nav_active_tiles"), Recast->GetNumActiveTiles());
    }
    Json->SetBoolField(TEXT("nav_ready"), bNavReady); Json->SetBoolField(TEXT("streaming_ready"), bStreamingReady); Json->SetBoolField(TEXT("ready"), IsWorldReady());
    Json->SetBoolField(TEXT("respawning"), bRespawning || bRestorePending); Json->SetStringField(TEXT("message"), LastMessage);
    Json->SetBoolField(TEXT("arrival_pending"),bRestorePending); Json->SetBoolField(TEXT("arrival_movement_held"),bHoldArrivalMovement);
    Json->SetArrayField(TEXT("arrival_destination"),VectorJson(ArrivalDestination));
    Json->SetNumberField(TEXT("arrival_stable_seconds"),ArrivalReadySince<0.?0.:GetWorld()->GetTimeSeconds()-ArrivalReadySince);
    Json->SetStringField(TEXT("arrival_block_reason"),ArrivalBlockReason);
    Json->SetStringField(TEXT("error"), ConfigError); Json->SetArrayField(TEXT("explored_zones"), SetJson(Explored));
    Json->SetArrayField(TEXT("discovered_memes"), SetJson(Discovered)); Json->SetArrayField(TEXT("completed_encounters"), SetJson(Completed));
    Json->SetStringField(TEXT("objective"),TEXT("探索小镇并持续生存，击退不断出现的敌人与首领"));
    const auto* Nearby = NearestMeme(); Json->SetStringField(TEXT("interaction_prompt"), Nearby ? TEXT("E · ") + Nearby->Name : TEXT(""));
    TArray<TSharedPtr<FJsonValue>> ZoneJson, MemeJson, EncounterJson, InteriorJson;
    for (const auto& Z : Zones)
    {
        auto O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("id"), Z.Id); O->SetStringField(TEXT("name"), Z.Name);
        O->SetArrayField(TEXT("center"), VectorJson(Z.Center)); O->SetArrayField(TEXT("extent"), VectorJson(Z.Extent)); O->SetArrayField(TEXT("checkpoint"), VectorJson(Z.Checkpoint));
        O->SetBoolField(TEXT("explored"), Explored.Contains(Z.Id)); ZoneJson.Add(MakeShared<FJsonValueObject>(O));
    }
    for (const auto& M : Memes)
    {
        auto O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("id"), M.Id); O->SetStringField(TEXT("name"), M.Name); O->SetStringField(TEXT("zone_id"), M.ZoneId);
        O->SetStringField(TEXT("kind"), M.Kind); O->SetArrayField(TEXT("position"), VectorJson(M.Position)); O->SetBoolField(TEXT("discovered"), Discovered.Contains(M.Id));
        if (M.bIndoorDiscovery && FParse::Param(FCommandLine::Get(),TEXT("HeliosVillageQA")))
        {
            O->SetStringField(TEXT("room_id"),M.RoomId);O->SetNumberField(TEXT("floor_id"),M.FloorId);
            const bool Near=Director && Director->Player && FVector::Dist(Director->Player->GetGameplayReference(),M.Position)<M.DiscoveryRadius;
            O->SetBoolField(TEXT("eligible_here"),Near && CanDiscoverIndoor(M));
        }
        MemeJson.Add(MakeShared<FJsonValueObject>(O));
    }
    for (const auto& E : Encounters)
    {
        auto O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("id"), E.Id); O->SetStringField(TEXT("zone_id"), E.ZoneId); O->SetBoolField(TEXT("boss"), E.bBoss);
        O->SetArrayField(TEXT("center"), VectorJson(E.Center)); O->SetBoolField(TEXT("active"), E.bTriggered && !Completed.Contains(E.Id)); O->SetBoolField(TEXT("completed"), Completed.Contains(E.Id));
        O->SetNumberField(TEXT("spawned"), E.Spawned); O->SetNumberField(TEXT("defeated"), E.Defeated); EncounterJson.Add(MakeShared<FJsonValueObject>(O));
        O->SetNumberField(TEXT("spawn_attempts"),E.SpawnAttempts); O->SetStringField(TEXT("spawn_failure"),E.LastSpawnFailure);
    }
    for (const auto& I : Interiors)
    {
        auto O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("id"), I.Id); O->SetStringField(TEXT("name"), I.Name); O->SetStringField(TEXT("zone_id"), I.ZoneId);
        O->SetArrayField(TEXT("position"), VectorJson(I.Position)); InteriorJson.Add(MakeShared<FJsonValueObject>(O));
    }
    Json->SetArrayField(TEXT("zones"), ZoneJson); Json->SetArrayField(TEXT("memes"), MemeJson); Json->SetArrayField(TEXT("encounters"), EncounterJson); Json->SetArrayField(TEXT("interiors"), InteriorJson);
    FString Text; FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text)); return Text;
}
