#include "HeliosTownEditorTools.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "AI/NavigationSystemConfig.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationData.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionRuntimeSpatialHash.h"
#include "UObject/UnrealType.h"

bool UHeliosTownEditorTools::ConfigureTownWorld(int32 CellSizeCm, float LoadingRangeCm)
{
    UWorld* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
    if(!World || World->IsGameWorld())return false;
    AWorldSettings* Settings=World->GetWorldSettings();
    if(!Settings)return false;
    Settings->Modify();
    UWorldPartition* Partition=World->GetWorldPartition();
    if(!Partition)Partition=UWorldPartition::CreateOrRepairWorldPartition(Settings,{},UWorldPartitionRuntimeSpatialHash::StaticClass());
    if(!Partition)return false;
    Partition->Modify();

    // Use a fresh native hash so rerunning the importer is deterministic. Grids is private in UE 5.8.
    UWorldPartitionRuntimeSpatialHash* Hash=NewObject<UWorldPartitionRuntimeSpatialHash>(Partition,NAME_None,RF_Transactional);
    FArrayProperty* GridsProperty=FindFProperty<FArrayProperty>(Hash->GetClass(),TEXT("Grids"));
    if(!GridsProperty || !CastField<FStructProperty>(GridsProperty->Inner))return false;
    FScriptArrayHelper Grids(GridsProperty,GridsProperty->ContainerPtrToValuePtr<void>(Hash));
    Grids.EmptyValues();
    Hash->SetDefaultValues();
    if(Grids.Num()!=1)return false;
    auto* Grid=reinterpret_cast<FSpatialHashRuntimeGrid*>(Grids.GetRawPtr(0));
    Grid->GridName=TEXT("MainGrid");
    Grid->CellSize=FMath::Max(3200,CellSizeCm);
    Grid->LoadingRange=FMath::Max(static_cast<float>(Grid->CellSize),LoadingRangeCm);
    Grid->Origin=FVector2D::ZeroVector;
    Grid->bBlockOnSlowStreaming=false;
    Grid->DebugColor=FLinearColor(.12f,.55f,.68f);
    Partition->RuntimeHash=Hash;
    Partition->SetEnableStreaming(true);

    // Persist this configuration on this map, leaving the project-wide navigation defaults alone.
    FObjectPropertyBase* NavProperty=FindFProperty<FObjectPropertyBase>(Settings->GetClass(),TEXT("NavigationSystemConfig"));
    if(!NavProperty)return false;
    UNavigationSystemConfig* Config=NewObject<UNavigationSystemConfig>(Settings,NAME_None,RF_Transactional);
    Config->NavigationSystemClass=FSoftClassPath(TEXT("/Script/HeliosOutpost.HeliosTownNavigationSystem"));
    NavProperty->SetObjectPropertyValue_InContainer(Settings,Config);
    Settings->SetNavigationSystemConfigOverride(nullptr);

    for(TActorIterator<ARecastNavMesh> It(World);It;++It)
    {
        ARecastNavMesh* Nav=*It;Nav->Modify();
        FNavDataConfig Agent=Nav->GetConfig();Agent.AgentRadius=44.f;
        Nav->SetConfig(Agent);
        if(FFloatProperty* Radius=FindFProperty<FFloatProperty>(Nav->GetClass(),TEXT("AgentRadius")))Radius->SetPropertyValue_InContainer(Nav,44.f);
        if(FEnumProperty* Generation=FindFProperty<FEnumProperty>(Nav->GetClass(),TEXT("RuntimeGeneration")))
            Generation->GetUnderlyingProperty()->SetIntPropertyValue(Generation->ContainerPtrToValuePtr<void>(Nav),static_cast<int64>(ERuntimeGenerationType::Dynamic));
        Nav->SetIsSpatiallyLoaded(false);
        Nav->SetRuntimeGrid(NAME_None);
        Nav->PostEditChange();Nav->MarkPackageDirty();
    }
    Hash->MarkPackageDirty();Partition->MarkPackageDirty();Settings->MarkPackageDirty();World->MarkPackageDirty();
    UE_LOG(LogTemp,Display,TEXT("IKUN_TOWN_EDITOR configured native spatial hash: MainGrid cell=12800 loading=32000 streaming=1 map-local navigation"));
    return true;
}

bool UHeliosTownEditorTools::MarkTownActor(AActor* Actor,bool bSpatial)
{
    if(!IsValid(Actor) || !GEditor || Actor->GetWorld()!=GEditor->GetEditorWorldContext().World())return false;
    Actor->Modify();Actor->SetIsSpatiallyLoaded(bSpatial);Actor->SetRuntimeGrid(bSpatial?FName(TEXT("MainGrid")):NAME_None);
    Actor->PostEditChange();Actor->MarkPackageDirty();return true;
}
