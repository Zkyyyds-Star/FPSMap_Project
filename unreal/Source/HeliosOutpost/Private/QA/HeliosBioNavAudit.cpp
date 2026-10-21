#include "QA/HeliosBioNavAudit.h"
#include "Bio/HeliosBioHotelGameMode.h"
#include "Bio/HeliosBioPlayer.h"
#include "Bio/HeliosBioNPC.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavCollision.h"
#include "PhysicsEngine/BodySetup.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

namespace
{
using FObject=TSharedPtr<FJsonObject>;
using FValue=TSharedPtr<FJsonValue>;
TArray<FValue> VectorValues(const FVector& V)
{return {MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)};}
FObject BoxValues(const FBox& Box)
{auto Row=MakeShared<FJsonObject>();Row->SetBoolField(TEXT("valid"),Box.IsValid!=0);if(Box.IsValid){Row->SetArrayField(TEXT("min"),VectorValues(Box.Min));Row->SetArrayField(TEXT("max"),VectorValues(Box.Max));}return Row;}
struct FProbe {const TCHAR* Name;FVector Surface;};
const TArray<FProbe> Probes={
    {TEXT("normal_spawn"),FVector(991,1293,16.318935)},
    {TEXT("east_ground_takeoff"),FVector(829.3,561.703,16.318935)},
    {TEXT("east_box90"),FVector(829.3,502.4,90.323044)},
    {TEXT("east_box161"),FVector(900.6,502.4,161.623047)},
    {TEXT("east_box261"),FVector(900.6,405,261.02301)},
    {TEXT("east_box331"),FVector(900.6,330.8,331.923035)},
    {TEXT("east_ramp_low"),FVector(970,340,331.923035)},
    {TEXT("east_ramp_middle"),FVector(970,320,341.054687)},
    {TEXT("east_ramp_high"),FVector(970,300,351.295)},
    {TEXT("east_shore353"),FVector(1007,266,353.936035)},
    {TEXT("middle_circle_exit1"),FVector(-425.61,130.37,235.9281)},
    {TEXT("middle_circle_exit2"),FVector(-406.71,98.96,235.9281)},
    {TEXT("middle_circle_exit3"),FVector(-357.73,91.93,235.9281)},
    {TEXT("middle_ground_drop"),FVector(-400,260,16.318935)},
    {TEXT("middle_south_box89"),FVector(-205,-380,89.334183)},
    {TEXT("middle_south_box161"),FVector(-205,-450,161.904251)},
    {TEXT("middle_south_box235"),FVector(-205,-520,235.123535)},
    {TEXT("west_ground34_shore283"),FVector(-1670,293,283.668518)},
    {TEXT("west_ground34_start283"),FVector(-1670,110,283.668518)},
    {TEXT("west_box231"),FVector(-1660,31,231.041443)},
    {TEXT("west_box171"),FVector(-1680,-30,171.641434)},
    {TEXT("west_base_slope"),FVector(-1660,-80,112.909271)},
    {TEXT("west_route_point_water"),FVector(-1472.4,293,-27.5)}
};
FObject PolyValues(ARecastNavMesh* Mesh,NavNodeRef Ref)
{
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("ref"),FString::Printf(TEXT("%llu"),static_cast<unsigned long long>(Ref)));
    FVector Center;TArray<FVector> Vertices;TArray<NavNodeRef> Neighbors;uint16 PolyFlags=0,AreaFlags=0;
    if(Mesh->GetPolyCenter(Ref,Center))Row->SetArrayField(TEXT("center"),VectorValues(Center));
    if(Mesh->GetPolyVerts(Ref,Vertices)){TArray<FValue> Values;for(const auto& V:Vertices)Values.Add(MakeShared<FJsonValueArray>(VectorValues(V)));Row->SetArrayField(TEXT("vertices"),Values);}
    Row->SetNumberField(TEXT("area_id"),Mesh->GetPolyAreaID(Ref));Row->SetNumberField(TEXT("surface_area_cm2"),Mesh->GetPolySurfaceArea(Ref));
    if(Mesh->GetPolyFlags(Ref,PolyFlags,AreaFlags)){Row->SetNumberField(TEXT("poly_flags"),PolyFlags);Row->SetNumberField(TEXT("area_flags"),AreaFlags);}
    Mesh->GetPolyNeighbors(Ref,Neighbors);TArray<FValue> NeighborValues;for(auto Other:Neighbors)NeighborValues.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%llu"),static_cast<unsigned long long>(Other))));Row->SetArrayField(TEXT("neighbor_refs"),NeighborValues);
    Row->SetBoolField(TEXT("custom_link"),Mesh->IsCustomLink(Ref));return Row;
}
}
AHeliosBioNavAudit::AHeliosBioNavAudit(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void AHeliosBioNavAudit::Tick(float Dt)
{
    Super::Tick(Dt);if(bRecorded||!GetWorld()||GetWorld()->IsPaused())return;Age+=Dt;if(Age<2.f)return;
    bRecorded=true;Record();
}
void AHeliosBioNavAudit::Record()
{
    auto Root=MakeShared<FJsonObject>();Root->SetStringField(TEXT("schema"),TEXT("HeliosBioNavAudit.v1"));Root->SetStringField(TEXT("map"),GetWorld()->GetOutermost()->GetName());
    Root->SetStringField(TEXT("scope"),TEXT("Read-only original-map runtime physics, nav collision, registered Recast data/polygons and path queries. No participant reposition, QA threshold change, geometry/asset alteration or acceptance result."));
    auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if(!Mode||!Mode->Player||!Nav)Root->SetStringField(TEXT("blocker"),TEXT("Actual Bio player or navigation system missing"));
    else
    {
        const auto& Props=Mode->Player->GetNavAgentPropertiesRef();const FVector OriginalSpawn=Mode->SpawnLocation(0)-FVector(0,0,Mode->Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        Root->SetArrayField(TEXT("actual_player_nav_agent_location"),VectorValues(Mode->Player->GetNavAgentLocation()));Root->SetArrayField(TEXT("normal_source_spawn_nav_agent_location"),VectorValues(OriginalSpawn));
        Root->SetNumberField(TEXT("actual_radius_cm"),Props.AgentRadius);Root->SetNumberField(TEXT("actual_height_cm"),Props.AgentHeight);
        Root->SetStringField(TEXT("selected_registered_nav_data"),GetPathNameSafe(Nav->GetNavDataForProps(Props,OriginalSpawn)));Root->SetStringField(TEXT("default_nav_data"),GetPathNameSafe(Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)));
        TArray<FValue> Bounds;
        for(TActorIterator<ANavMeshBoundsVolume> It(GetWorld());It;++It){auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("actor"),It->GetPathName());Row->SetObjectField(TEXT("component_bounds"),BoxValues(It->GetComponentsBoundingBox(true)));Bounds.Add(MakeShared<FJsonValueObject>(Row));}Root->SetArrayField(TEXT("nav_bounds_volumes"),Bounds);
        FCollisionQueryParams Query(SCENE_QUERY_STAT(BioNavReadOnlyAudit),false,this);Query.bReturnFaceIndex=true;for(auto Bot:Mode->Bots)Query.AddIgnoredActor(Bot);Query.AddIgnoredActor(Mode->Player);
        FCollisionObjectQueryParams Static(ECC_WorldStatic);
        TArray<FValue> Physics;
        for(const auto& Probe:Probes)
        {
            auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("probe"),Probe.Name);Row->SetArrayField(TEXT("original_surface_cm"),VectorValues(Probe.Surface));
            for(bool Complex:{false,true})
            {
                Query.bTraceComplex=Complex;FHitResult Hit;const bool Found=GetWorld()->LineTraceSingleByObjectType(Hit,Probe.Surface+FVector(0,0,25),Probe.Surface-FVector(0,0,35),Static,Query);
                auto Floor=MakeShared<FJsonObject>();Floor->SetBoolField(TEXT("hit"),Found);
                if(Found){Floor->SetStringField(TEXT("component"),GetPathNameSafe(Hit.Component.Get()));Floor->SetArrayField(TEXT("point"),VectorValues(Hit.ImpactPoint));Floor->SetArrayField(TEXT("signed_normal"),VectorValues(Hit.ImpactNormal));Floor->SetNumberField(TEXT("face_index"),Hit.FaceIndex);Floor->SetNumberField(TEXT("mobility"),Hit.Component.IsValid()?int32(Hit.Component->Mobility.GetValue()):-1);}
                Row->SetObjectField(Complex?TEXT("complex_static_floor"):TEXT("production_simple_static_floor"),Floor);
            }
            Query.bTraceComplex=false;TArray<FOverlapResult> Overlaps;
            GetWorld()->OverlapMultiByObjectType(Overlaps,Probe.Surface+FVector(0,0,72),FQuat::Identity,Static,FCollisionShape::MakeCapsule(20,70),Query);
            TArray<FValue> Blocking;for(const auto& Hit:Overlaps)if(Hit.bBlockingHit)Blocking.Add(MakeShared<FJsonValueString>(GetPathNameSafe(Hit.GetComponent())));
            Row->SetArrayField(TEXT("r20_h140_capsule_blocking_at_surface_plus2"),Blocking);Physics.Add(MakeShared<FJsonValueObject>(Row));
        }
        Root->SetArrayField(TEXT("actual_cooked_physics_probes"),Physics);
        TArray<FValue> Components;
        for(TActorIterator<AActor> It(GetWorld());It;++It)
        {
            TArray<UStaticMeshComponent*> MeshComponents;It->GetComponents(MeshComponents);
            for(auto* Component:MeshComponents)
            {
                UStaticMesh* Asset=Component->GetStaticMesh();if(!Asset||!Asset->GetPathName().Contains(TEXT("CFMBioPyramidFloating/Map")))continue;
                auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("component"),Component->GetPathName());Row->SetStringField(TEXT("mesh"),Asset->GetPathName());Row->SetNumberField(TEXT("mobility"),int32(Component->Mobility.GetValue()));Row->SetBoolField(TEXT("registered"),Component->IsRegistered());Row->SetBoolField(TEXT("can_ever_affect_navigation"),Component->CanEverAffectNavigation());Row->SetNumberField(TEXT("collision_enabled"),int32(Component->GetCollisionEnabled()));Row->SetBoolField(TEXT("mesh_has_navigation_data"),Asset->bHasNavigationData);Row->SetNumberField(TEXT("world_transform_determinant"),Component->GetComponentTransform().ToMatrixWithScale().Determinant());Row->SetObjectField(TEXT("bounds"),BoxValues(Component->Bounds.GetBox()));
                if(UBodySetup* Body=Asset->GetBodySetup()){Row->SetNumberField(TEXT("collision_trace_flag"),int32(Body->CollisionTraceFlag));Row->SetBoolField(TEXT("double_sided_geometry"),Body->bDoubleSidedGeometry);}
                if(UNavCollisionBase* Collision=Asset->GetNavCollision())
                {
                    Row->SetStringField(TEXT("nav_collision"),Collision->GetPathName());Row->SetBoolField(TEXT("nav_dynamic_obstacle"),Collision->IsDynamicObstacle());Row->SetBoolField(TEXT("nav_has_convex_geometry"),Collision->HasConvexGeometry());
                    const auto& Tri=Collision->GetTriMeshCollision();Row->SetNumberField(TEXT("nav_tri_vertices"),Tri.VertexBuffer.Num());Row->SetNumberField(TEXT("nav_tri_indices"),Tri.IndexBuffer.Num());Row->SetNumberField(TEXT("nav_convex_vertices"),Collision->GetConvexCollision().VertexBuffer.Num());
                    TArray<FValue> Surfaces;
                    for(const auto& Probe:Probes)
                    {
                        TArray<FValue> NearTriangles;
                        for(int32 Index=0;Index+2<Tri.IndexBuffer.Num();Index+=3)
                        {
                            const int32 A=Tri.IndexBuffer[Index],B=Tri.IndexBuffer[Index+1],C=Tri.IndexBuffer[Index+2];if(!Tri.VertexBuffer.IsValidIndex(A)||!Tri.VertexBuffer.IsValidIndex(B)||!Tri.VertexBuffer.IsValidIndex(C))continue;
                            const FVector V0=Component->GetComponentTransform().TransformPosition(Tri.VertexBuffer[A]),V1=Component->GetComponentTransform().TransformPosition(Tri.VertexBuffer[B]),V2=Component->GetComponentTransform().TransformPosition(Tri.VertexBuffer[C]);FBox Box(EForceInit::ForceInit);Box+=V0;Box+=V1;Box+=V2;
                            if(Probe.Surface.X<Box.Min.X-1||Probe.Surface.X>Box.Max.X+1||Probe.Surface.Y<Box.Min.Y-1||Probe.Surface.Y>Box.Max.Y+1||FMath::Abs(Box.Min.Z-Probe.Surface.Z)>3||FMath::Abs(Box.Max.Z-Probe.Surface.Z)>3)continue;
                            auto Triangle=MakeShared<FJsonObject>();Triangle->SetNumberField(TEXT("index"),Index/3);Triangle->SetArrayField(TEXT("signed_geometric_normal"),VectorValues(FVector::CrossProduct(V1-V0,V2-V0).GetSafeNormal()));Triangle->SetArrayField(TEXT("v0"),VectorValues(V0));Triangle->SetArrayField(TEXT("v1"),VectorValues(V1));Triangle->SetArrayField(TEXT("v2"),VectorValues(V2));NearTriangles.Add(MakeShared<FJsonValueObject>(Triangle));
                        }
                        if(!NearTriangles.IsEmpty()){auto Surface=MakeShared<FJsonObject>();Surface->SetStringField(TEXT("probe"),Probe.Name);Surface->SetArrayField(TEXT("near_original_surface_triangles"),NearTriangles);Surfaces.Add(MakeShared<FJsonValueObject>(Surface));}
                    }
                    Row->SetArrayField(TEXT("actual_navigation_export_surface_triangles"),Surfaces);
                }
                Components.Add(MakeShared<FJsonValueObject>(Row));
            }
        }
        Root->SetArrayField(TEXT("source_static_mesh_navigation_properties"),Components);
        TArray<FValue> NavMeshes;
        for(TActorIterator<ARecastNavMesh> It(GetWorld());It;++It)
        {
            auto* Mesh=*It;auto Row=MakeShared<FJsonObject>();const auto& Config=Mesh->GetConfig();Row->SetStringField(TEXT("actor"),Mesh->GetPathName());Row->SetNumberField(TEXT("build_agent_radius"),Mesh->AgentRadius);Row->SetNumberField(TEXT("build_agent_height"),Mesh->AgentHeight);Row->SetNumberField(TEXT("registered_agent_radius"),Config.AgentRadius);Row->SetNumberField(TEXT("registered_agent_height"),Config.AgentHeight);Row->SetNumberField(TEXT("cell_size"),Mesh->GetCellSize(ENavigationDataResolution::Default));Row->SetNumberField(TEXT("cell_height"),Mesh->GetCellHeight(ENavigationDataResolution::Default));Row->SetNumberField(TEXT("step_height"),Mesh->GetAgentMaxStepHeight(ENavigationDataResolution::Default));Row->SetNumberField(TEXT("slope"),Mesh->AgentMaxSlope);Row->SetNumberField(TEXT("min_region_area"),Mesh->MinRegionArea);Row->SetNumberField(TEXT("merge_region_size"),Mesh->MergeRegionSize);Row->SetNumberField(TEXT("tile_size"),Mesh->GetTileSizeUU());Row->SetNumberField(TEXT("active_tiles"),Mesh->GetNumActiveTiles());
            TArray<FValue> Tiles;TArray<FNavTileRef> Refs;Mesh->GetAllNavMeshTiles(Refs);
            for(auto Ref:Refs){auto Tile=MakeShared<FJsonObject>();int32 X=0,Y=0,Layer=0;Mesh->GetNavMeshTileXY(Ref,X,Y,Layer);Tile->SetStringField(TEXT("ref"),FString::Printf(TEXT("%llu"),static_cast<unsigned long long>(Ref)));Tile->SetNumberField(TEXT("x"),X);Tile->SetNumberField(TEXT("y"),Y);Tile->SetNumberField(TEXT("layer"),Layer);Tile->SetObjectField(TEXT("bounds"),BoxValues(Mesh->GetNavMeshTileBounds(Ref)));Tiles.Add(MakeShared<FJsonValueObject>(Tile));}Row->SetArrayField(TEXT("populated_tile_bounds"),Tiles);
            TArray<FValue> Points;
            for(const auto& Probe:Probes)
            {
                auto Point=MakeShared<FJsonObject>();Point->SetStringField(TEXT("probe"),Probe.Name);const FVector Wanted=Probe.Surface+FVector(0,0,2);Point->SetArrayField(TEXT("wanted"),VectorValues(Wanted));TArray<FValue> Projections;
                for(const auto& Extent:{FVector(70,70,180),FVector(10,10,20),FVector(300,300,400)})
                {FNavLocation Projected;const bool Found=Nav->ProjectPointToNavigation(Wanted,Projected,Extent,Mesh);auto Project=MakeShared<FJsonObject>();Project->SetArrayField(TEXT("extent"),VectorValues(Extent));Project->SetBoolField(TEXT("found"),Found);if(Found){Project->SetArrayField(TEXT("point"),VectorValues(Projected.Location));Project->SetObjectField(TEXT("poly"),PolyValues(Mesh,Projected.NodeRef));}Projections.Add(MakeShared<FJsonValueObject>(Project));}Point->SetArrayField(TEXT("projections"),Projections);
                TArray<FNavPoly> Polys;Mesh->GetPolysInBox(FBox(Wanted-FVector(90,90,500),Wanted+FVector(90,90,500)),Polys);TArray<FValue> Values;for(const auto& Poly:Polys)Values.Add(MakeShared<FJsonValueObject>(PolyValues(Mesh,Poly.Ref)));Point->SetArrayField(TEXT("nearby_cooked_polygons"),Values);Points.Add(MakeShared<FJsonValueObject>(Point));
            }
            Row->SetArrayField(TEXT("original_surface_probes"),Points);NavMeshes.Add(MakeShared<FJsonValueObject>(Row));
        }
        Root->SetArrayField(TEXT("actual_recast_nav_data"),NavMeshes);
        TArray<FValue> Paths;
        for(const auto& Start:Probes)for(const auto& End:Probes)
        {
            if(FString(Start.Name)!=TEXT("normal_spawn")&&FString(Start.Name)!=TEXT("east_box261")&&FString(Start.Name)!=TEXT("east_box331")&&FString(Start.Name)!=TEXT("middle_circle_exit2")&&FString(Start.Name)!=TEXT("west_ground34_shore283"))continue;
            auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("from"),Start.Name);Row->SetStringField(TEXT("to"),End.Name);auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Start.Surface+FVector(0,0,2),End.Surface+FVector(0,0,2),Mode->Player);
            Row->SetBoolField(TEXT("exists"),Path!=nullptr);Row->SetBoolField(TEXT("valid"),Path&&Path->IsValid());Row->SetBoolField(TEXT("partial"),Path&&Path->IsPartial());if(Path){Row->SetNumberField(TEXT("length"),Path->GetPathLength());TArray<FValue> Points;for(const auto& Point:Path->PathPoints)Points.Add(MakeShared<FJsonValueArray>(VectorValues(Point)));Row->SetArrayField(TEXT("points"),Points);}Paths.Add(MakeShared<FJsonValueObject>(Row));
        }
        Root->SetArrayField(TEXT("original_surface_paths_with_actual_player_agent"),Paths);
    }
    FString Out;FParse::Value(FCommandLine::Get(),TEXT("BioNavAuditOut="),Out);if(Out.IsEmpty())Out=FPaths::ProjectSavedDir()/TEXT("BioNavAudit");IFileManager::Get().MakeDirectory(*Out,true);
    FString Text;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Text));const bool Saved=FFileHelper::SaveStringToFile(Text,*(Out/TEXT("nav_report.json")));
    UE_LOG(LogTemp,Display,TEXT("BIO_NAV_AUDIT_RECORDED saved=%d path=%s"),Saved,*(Out/TEXT("nav_report.json")));FGenericPlatformMisc::RequestExit(false);
}
