#include "MCP/HeliosBridgeProbeSubsystem.h"

#include "Components/PrimitiveComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HeliosOutpost.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TimerManager.h"

namespace
{
constexpr double SeamX = 4400.0;
constexpr double SeamY = 2000.0;

TArray<TSharedPtr<FJsonValue>> JsonVector(const FVector& Value)
{
    return {
        MakeShared<FJsonValueNumber>(Value.X),
        MakeShared<FJsonValueNumber>(Value.Y),
        MakeShared<FJsonValueNumber>(Value.Z)
    };
}

bool IsBridgeProbeRequested()
{
    FString Value;
    return FParse::Param(FCommandLine::Get(), TEXT("HeliosBridgeProbe"))
        || FParse::Value(FCommandLine::Get(), TEXT("HeliosBridgeProbe="), Value);
}
}

bool UHeliosBridgeProbeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    if (!Super::ShouldCreateSubsystem(Outer) || !IsBridgeProbeRequested())
    {
        return false;
    }
    const UWorld* World = Cast<UWorld>(Outer);
    return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UHeliosBridgeProbeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    // Delay until registered components have entered the runtime physics scene.
    InWorld.GetTimerManager().SetTimer(ProbeTimer, this,
        &UHeliosBridgeProbeSubsystem::RunProbe, 0.25f, false);
}

TSharedPtr<FJsonObject> UHeliosBridgeProbeSubsystem::Trace(const FVector& Start, const FVector& End,
    const bool bTraceComplex, const FString& Name) const
{
    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("name"), Name);
    Item->SetArrayField(TEXT("start_cm"), JsonVector(Start));
    Item->SetArrayField(TEXT("end_cm"), JsonVector(End));
    Item->SetBoolField(TEXT("trace_complex"), bTraceComplex);

    FCollisionQueryParams Params(SCENE_QUERY_STAT(HeliosBridgeProbe), bTraceComplex);
    Params.bFindInitialOverlaps = true;
    FHitResult Hit;
    const bool bBlockingHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
    Item->SetBoolField(TEXT("blocking_hit"), bBlockingHit);
    Item->SetBoolField(TEXT("initial_overlap"), Hit.bStartPenetrating);
    Item->SetArrayField(TEXT("location_cm"), JsonVector(Hit.Location));
    Item->SetArrayField(TEXT("impact_point_cm"), JsonVector(Hit.ImpactPoint));
    Item->SetNumberField(TEXT("distance_cm"), Hit.Distance);
    Item->SetStringField(TEXT("actor"), GetPathNameSafe(Hit.GetActor()));
    Item->SetStringField(TEXT("component"), GetPathNameSafe(Hit.GetComponent()));
    if (const UPrimitiveComponent* Component = Hit.GetComponent())
    {
        Item->SetNumberField(TEXT("component_collision_enabled"), static_cast<int32>(Component->GetCollisionEnabled()));
        Item->SetNumberField(TEXT("visibility_response"), static_cast<int32>(Component->GetCollisionResponseToChannel(ECC_Visibility)));
    }
    Item->SetBoolField(TEXT("hit_upper_bridge"), bBlockingHit && Hit.ImpactPoint.Z > 300.0);
    return Item;
}

TSharedPtr<FJsonObject> UHeliosBridgeProbeSubsystem::SphereTrace(const float RadiusCm,
    const bool bTraceComplex) const
{
    const FVector Start(SeamX, SeamY, 500.0);
    const FVector End(SeamX, SeamY, 250.0);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(HeliosBridgeProbeSphere), bTraceComplex);
    Params.bFindInitialOverlaps = true;
    FHitResult Hit;
    const bool bBlockingHit = GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity,
        ECC_Visibility, FCollisionShape::MakeSphere(RadiusCm), Params);

    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetNumberField(TEXT("radius_cm"), RadiusCm);
    Item->SetBoolField(TEXT("trace_complex"), bTraceComplex);
    Item->SetBoolField(TEXT("blocking_hit"), bBlockingHit);
    Item->SetBoolField(TEXT("initial_overlap"), Hit.bStartPenetrating);
    Item->SetArrayField(TEXT("location_cm"), JsonVector(Hit.Location));
    Item->SetArrayField(TEXT("impact_point_cm"), JsonVector(Hit.ImpactPoint));
    Item->SetStringField(TEXT("actor"), GetPathNameSafe(Hit.GetActor()));
    Item->SetStringField(TEXT("component"), GetPathNameSafe(Hit.GetComponent()));
    Item->SetBoolField(TEXT("hit_upper_bridge"), bBlockingHit && Hit.ImpactPoint.Z > 300.0);
    return Item;
}

void UHeliosBridgeProbeSubsystem::RunProbe()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    TSharedPtr<FJsonObject> Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("probe"), TEXT("upper_bridge_seam_wp12_runtime"));
    Report->SetStringField(TEXT("map"), World->GetPathName());
    Report->SetNumberField(TEXT("world_type"), static_cast<int32>(World->WorldType));
    Report->SetBoolField(TEXT("begun_play"), World->HasBegunPlay());
    Report->SetBoolField(TEXT("physics_scene_valid"), World->GetPhysicsScene() != nullptr);
    Report->SetStringField(TEXT("started_utc"), FDateTime::UtcNow().ToIso8601());
    Report->SetNumberField(TEXT("declared_layout_gap_cm"), 0.0);

    struct FControl
    {
        const TCHAR* Name;
        FVector Start;
        FVector End;
        double MinimumHitZ;
    };
    const FControl Controls[] = {
        {TEXT("landing_ground"), FVector(-5400, -4400, 650), FVector(-5400, -4400, -650), -100.0},
        {TEXT("bridge_south_center"), FVector(4400, 1500, 650), FVector(4400, 1500, -650), 300.0},
        {TEXT("bridge_north_center"), FVector(4400, 2600, 650), FVector(4400, 2600, -650), 300.0},
        {TEXT("observation_deck_center"), FVector(5600, 4000, 650), FVector(5600, 4000, -650), 300.0},
    };

    bool bControlsPassed = World->GetPhysicsScene() != nullptr;
    TArray<TSharedPtr<FJsonValue>> ControlValues;
    for (const FControl& Control : Controls)
    {
        TSharedPtr<FJsonObject> Item = Trace(Control.Start, Control.End, false, Control.Name);
        const TArray<TSharedPtr<FJsonValue>>* Impact = nullptr;
        const bool bPassed = Item->GetBoolField(TEXT("blocking_hit"))
            && Item->TryGetArrayField(TEXT("impact_point_cm"), Impact)
            && Impact && Impact->Num() == 3 && (*Impact)[2]->AsNumber() >= Control.MinimumHitZ;
        Item->SetNumberField(TEXT("expected_min_z_cm"), Control.MinimumHitZ);
        Item->SetBoolField(TEXT("control_passed"), bPassed);
        bControlsPassed &= bPassed;
        ControlValues.Add(MakeShared<FJsonValueObject>(Item));
    }
    Report->SetArrayField(TEXT("collision_controls"), ControlValues);

    static const double XOffsets[] = {-190.0, -100.0, 0.0, 100.0, 190.0};
    static const double YOffsets[] = {-20.0, -5.0, -1.0, -0.1, -0.01, 0.0, 0.01, 0.1, 1.0, 5.0, 20.0};
    TArray<TSharedPtr<FJsonValue>> TraceValues;
    bool bExactSimpleHit = false;
    bool bExactComplexHit = false;
    for (const bool bTraceComplex : {false, true})
    {
        for (const double XOffset : XOffsets)
        {
            for (const double YOffset : YOffsets)
            {
                TSharedPtr<FJsonObject> Item = Trace(
                    FVector(SeamX + XOffset, SeamY + YOffset, 500),
                    FVector(SeamX + XOffset, SeamY + YOffset, 250),
                    bTraceComplex);
                Item->SetNumberField(TEXT("seam_offset_x_cm"), XOffset);
                Item->SetNumberField(TEXT("seam_offset_y_cm"), YOffset);
                if (XOffset == 0.0 && YOffset == 0.0)
                {
                    if (bTraceComplex) bExactComplexHit = Item->GetBoolField(TEXT("hit_upper_bridge"));
                    else bExactSimpleHit = Item->GetBoolField(TEXT("hit_upper_bridge"));
                }
                TraceValues.Add(MakeShared<FJsonValueObject>(Item));
            }
        }
    }
    Report->SetArrayField(TEXT("upper_only_line_traces"), TraceValues);

    TArray<TSharedPtr<FJsonValue>> SphereValues;
    for (const bool bTraceComplex : {false, true})
    {
        for (const float Radius : {0.1f, 1.0f, 5.0f, 34.0f, 42.0f})
        {
            SphereValues.Add(MakeShared<FJsonValueObject>(SphereTrace(Radius, bTraceComplex)));
        }
    }
    Report->SetArrayField(TEXT("sphere_traces_at_exact_seam"), SphereValues);

    TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
    Summary->SetBoolField(TEXT("controls_passed"), bControlsPassed);
    Summary->SetBoolField(TEXT("exact_simple_upper_hit"), bExactSimpleHit);
    Summary->SetBoolField(TEXT("exact_complex_upper_hit"), bExactComplexHit);
    if (!bControlsPassed)
    {
        Summary->SetStringField(TEXT("status"), TEXT("inconclusive"));
        Summary->SetStringField(TEXT("diagnosis"), TEXT("runtime_collision_controls_failed"));
    }
    else
    {
        Summary->SetStringField(TEXT("status"), TEXT("conclusive"));
        Summary->SetStringField(TEXT("diagnosis"),
            bExactSimpleHit
                ? TEXT("exact_seam_hits_simple_collision")
                : (bExactComplexHit ? TEXT("simple_collision_seam_only")
                    : TEXT("simple_and_complex_share_seam_or_precision_gap")));
    }
    Report->SetObjectField(TEXT("summary"), Summary);

    const FString RootDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("..")));
    const FString OutputFile = FPaths::Combine(RootDir, TEXT("qa/enemies/bridge_seam_runtime_probe.json"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputFile), true);
    FString Output;
    FJsonSerializer::Serialize(Report.ToSharedRef(), TJsonWriterFactory<>::Create(&Output));
    const bool bSaved = FFileHelper::SaveStringToFile(Output, *OutputFile,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogHeliosOutpost, Display,
        TEXT("HELIOS_BRIDGE_RUNTIME_PROBE status=%s diagnosis=%s saved=%s report=%s"),
        *Summary->GetStringField(TEXT("status")), *Summary->GetStringField(TEXT("diagnosis")),
        bSaved ? TEXT("true") : TEXT("false"), *OutputFile);

    if (!FParse::Param(FCommandLine::Get(), TEXT("HeliosBridgeProbeNoExit")))
    {
        FPlatformMisc::RequestExit(false);
    }
}
