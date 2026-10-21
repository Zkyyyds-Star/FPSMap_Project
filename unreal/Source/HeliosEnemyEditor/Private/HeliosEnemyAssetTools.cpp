#include "HeliosEnemyAssetTools.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/World.h"
#include "AssetCompilingManager.h"
#include "RenderingThread.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/Skeleton.h"
#include "AnimationRuntime.h"

int32 UHeliosEnemyAssetTools::ApplyUpperBodyCorrection(UAnimSequence* Target, UAnimSequence* AuthoredSource)
{
    if (!Target || !AuthoredSource || Target==AuthoredSource) return 0;
    const IAnimationDataModel* Source=AuthoredSource->GetDataModel();
    const IAnimationDataModel* Dest=Target->GetDataModel();
    const USkeleton* SourceSkeleton=AuthoredSource->GetSkeleton();
    const USkeleton* TargetSkeleton=Target->GetSkeleton();
    if (!Source || !Dest || !SourceSkeleton || !TargetSkeleton || Source->GetNumberOfKeys()<1 || Dest->GetNumberOfKeys()<2) return 0;

    const FName SpineBone(TEXT("spine_01"));
    const FName PelvisBone(TEXT("pelvis"));
    const FReferenceSkeleton& SourceRef=SourceSkeleton->GetReferenceSkeleton();
    const FReferenceSkeleton& TargetRef=TargetSkeleton->GetReferenceSkeleton();
    const int32 SourceSpineIndex=SourceRef.FindBoneIndex(SpineBone);
    const int32 TargetSpineIndex=TargetRef.FindBoneIndex(SpineBone);
    const int32 TargetPelvisIndex=TargetRef.FindBoneIndex(PelvisBone);
    if (SourceSpineIndex==INDEX_NONE || TargetSpineIndex==INDEX_NONE || TargetPelvisIndex==INDEX_NONE ||
        TargetRef.GetParentIndex(TargetSpineIndex)!=TargetPelvisIndex ||
        !Source->IsValidBoneTrackName(SpineBone) || !Dest->IsValidBoneTrackName(SpineBone) ||
        !Dest->IsValidBoneTrackName(PelvisBone)) return 0;

    const int32 KeyCount=Dest->GetNumberOfKeys();
    const int32 SourceKeyCount=Source->GetNumberOfKeys();
    auto EvaluateComponentPose=[](const IAnimationDataModel* Model,const FReferenceSkeleton& Ref,const FFrameTime& Frame,TArray<FTransform>& Local,TArray<FTransform>& Component)
    {
        Local=Ref.GetRefBonePose();
        for (int32 BoneIndex=0;BoneIndex<Ref.GetNum();++BoneIndex)
        {
            const FName BoneName=Ref.GetBoneName(BoneIndex);
            if (Model->IsValidBoneTrackName(BoneName))
                Local[BoneIndex]=Model->EvaluateBoneTrackTransform(BoneName,Frame,EAnimInterpolationType::Linear);
        }
        FAnimationRuntime::FillUpComponentSpaceTransforms(Ref,Local,Component);
    };

    // Preserve the original retargeted root/pelvis and legs.  spine_01 is the
    // boundary: express the authored component-space torso rotation relative
    // to the retargeted pelvis for every destination key.  Descendant authored
    // local tracks then retain their authored world facing without pinning the
    // torso translation or disturbing locomotion below the waist.
    TArray<FVector> SpinePositions,SpineScales;
    TArray<FQuat> SpineRotations;
    SpinePositions.Reserve(KeyCount);SpineRotations.Reserve(KeyCount);SpineScales.Reserve(KeyCount);
    TArray<FTransform> SourceLocal,SourceComponent,TargetLocal,TargetComponent;
    for (int32 Key=0;Key<KeyCount;++Key)
    {
        const double SourceFrame=static_cast<double>(Key)*(SourceKeyCount-1)/(KeyCount-1);
        EvaluateComponentPose(Source,SourceRef,FFrameTime::FromDecimal(SourceFrame),SourceLocal,SourceComponent);
        EvaluateComponentPose(Dest,TargetRef,FFrameTime(Key),TargetLocal,TargetComponent);
        FTransform AuthoredLocal=Source->EvaluateBoneTrackTransform(SpineBone,FFrameTime::FromDecimal(SourceFrame),EAnimInterpolationType::Linear);
        FQuat BoundaryRotation=SourceComponent[SourceSpineIndex].GetRelativeTransform(TargetComponent[TargetPelvisIndex]).GetRotation();
        BoundaryRotation.Normalize();
        SpinePositions.Add(AuthoredLocal.GetTranslation());
        SpineRotations.Add(BoundaryRotation);
        SpineScales.Add(AuthoredLocal.GetScale3D());
    }

    IAnimationDataController& Controller=Target->GetController();
    Controller.OpenBracket(FText::FromString(TEXT("Apply authored weapon and head proportion correction")),false);
    if (!Controller.SetBoneTrackKeys(SpineBone,SpinePositions,SpineRotations,SpineScales,false))
    {
        Controller.CloseBracket(false);
        return 0;
    }
    int32 Count=1;
    const TArray<FName> Bones={TEXT("spine_02"),TEXT("spine_03"),TEXT("neck_01"),TEXT("head"),
        TEXT("clavicle_l"),TEXT("upperarm_l"),TEXT("lowerarm_l"),TEXT("hand_l"),
        TEXT("clavicle_r"),TEXT("upperarm_r"),TEXT("lowerarm_r"),TEXT("hand_r"),TEXT("prop_ball")};
    for (FName Bone:Bones)
    {
        if (!Source->IsValidBoneTrackName(Bone) || !Dest->IsValidBoneTrackName(Bone)) continue;
        TArray<FVector> Positions,Scales; TArray<FQuat> Rotations;
        for (int32 Key=0;Key<KeyCount;++Key)
        {
            const double Frame=static_cast<double>(Key)*(Source->GetNumberOfKeys()-1)/(KeyCount-1);
            const FTransform Pose=Source->EvaluateBoneTrackTransform(Bone,FFrameTime::FromDecimal(Frame),EAnimInterpolationType::Linear);
            Positions.Add(Pose.GetTranslation()); Rotations.Add(Pose.GetRotation()); Scales.Add(Pose.GetScale3D());
        }
        if (Controller.SetBoneTrackKeys(Bone,Positions,Rotations,Scales,false)) ++Count;
    }
    Controller.CloseBracket(false);
    Target->MarkPackageDirty();
    return Count;
}

bool UHeliosEnemyAssetTools::CapturePose(USceneCaptureComponent2D* Capture)
{
    if (!Capture || !Capture->GetWorld()) return false;
    // Commandlet scripts have no editor frame loop. Advance the frame identity
    // so the renderer cannot reuse the preceding view or skin-cache update.
    if (IsRunningCommandlet()) { ++GFrameCounter; ++GFrameNumber; }
    FAssetCompilingManager::Get().FinishAllCompilation();
    Capture->GetWorld()->UpdateWorldComponents(false, false);
    Capture->GetWorld()->SendAllEndOfFrameUpdates();
    FlushRenderingCommands();
    Capture->CaptureScene();
    FlushRenderingCommands();
    return true;
}

bool UHeliosEnemyAssetTools::EvaluatePose(USkeletalMeshComponent* Component, UAnimSequence* Animation, float TimeSeconds)
{
    if (!Component || !Animation) return false;
    if (IsRunningCommandlet()) { ++GFrameCounter; ++GFrameNumber; }
    Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    Component->SetAnimation(Animation);
    Component->SetPosition(TimeSeconds, false);
    Component->TickAnimation(0.0f, false);
    Component->RefreshBoneTransforms();
    Component->UpdateComponentToWorld();
    Component->MarkRenderTransformDirty();
    Component->MarkRenderDynamicDataDirty();
    return true;
}
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Animation/MorphTarget.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
TArray<TSharedPtr<FJsonValue>> VectorValues(const FVector& Value)
{
    return {MakeShared<FJsonValueNumber>(Value.X),MakeShared<FJsonValueNumber>(Value.Y),MakeShared<FJsonValueNumber>(Value.Z)};
}
}

FString UHeliosEnemyAssetTools::DescribeSkeletalMesh(USkeletalMesh* Mesh)
{
    TSharedRef<FJsonObject> Result=MakeShared<FJsonObject>();
    if (!Mesh)
    {
        Result->SetStringField(TEXT("error"),TEXT("null_mesh"));
    }
    else
    {
        Result->SetStringField(TEXT("mesh"),Mesh->GetPathName());
        Result->SetBoolField(TEXT("has_vertex_colors"),Mesh->GetHasVertexColors());
        TArray<TSharedPtr<FJsonValue>> RenderColors;
        if (const FSkeletalMeshRenderData* RenderData=Mesh->GetResourceForRendering())
            for (const FSkeletalMeshLODRenderData& RenderLod : RenderData->LODRenderData)
                RenderColors.Add(MakeShared<FJsonValueNumber>(RenderLod.StaticVertexBuffers.ColorVertexBuffer.GetNumVertices()));
        Result->SetArrayField(TEXT("render_color_vertex_counts"),RenderColors);
        const FReferenceSkeleton& Ref=Mesh->GetRefSkeleton();
        const TArray<FTransform>& Poses=Ref.GetRefBonePose();
        TArray<TSharedPtr<FJsonValue>> Bones;
        int32 Roots=0;
        for (int32 Index=0;Index<Ref.GetNum();++Index)
        {
            TSharedRef<FJsonObject> Bone=MakeShared<FJsonObject>();
            Bone->SetStringField(TEXT("name"),Ref.GetBoneName(Index).ToString());
            Bone->SetNumberField(TEXT("parent"),Ref.GetParentIndex(Index));
            Bone->SetArrayField(TEXT("local_translation_cm"),VectorValues(Poses[Index].GetTranslation()));
            Bone->SetArrayField(TEXT("local_scale"),VectorValues(Poses[Index].GetScale3D()));
            Bone->SetArrayField(TEXT("local_rotation_pyr"),VectorValues(FVector(Poses[Index].Rotator().Pitch,Poses[Index].Rotator().Yaw,Poses[Index].Rotator().Roll)));
            Bones.Add(MakeShared<FJsonValueObject>(Bone));
            if (Ref.GetParentIndex(Index)==INDEX_NONE) ++Roots;
        }
        Result->SetArrayField(TEXT("bones"),Bones);
        Result->SetNumberField(TEXT("root_count"),Roots);
        const FBoxSphereBounds Bounds=Mesh->GetImportedBounds();
        Result->SetArrayField(TEXT("bounds_center_cm"),VectorValues(Bounds.Origin));
        Result->SetArrayField(TEXT("bounds_extent_cm"),VectorValues(Bounds.BoxExtent));
        Result->SetNumberField(TEXT("feet_z_cm"),Bounds.Origin.Z-Bounds.BoxExtent.Z);
        TArray<TSharedPtr<FJsonValue>> Morphs;
        for (UMorphTarget* Morph : Mesh->GetMorphTargets())
        {
            if (Morph) Morphs.Add(MakeShared<FJsonValueString>(Morph->GetName()));
        }
        Result->SetArrayField(TEXT("morph_targets"),Morphs);
        TArray<TSharedPtr<FJsonValue>> Lods;
        FSkeletalMeshModel* Model=Mesh->GetImportedModel();
        if (Model)
        {
            for (int32 Index=0;Index<Model->LODModels.Num();++Index)
            {
                const FSkeletalMeshLODModel& Lod=Model->LODModels[Index];
                int32 Unweighted=0,MaxInfluences=0,NonNormalized=0,VertexCount=0;
                FBox TightBounds(ForceInit);
                TSet<uint32> Colors;
                for (const FSkelMeshSection& Section : Lod.Sections)
                {
                    for (const FSoftSkinVertex& Vertex : Section.SoftVertices)
                    {
                        TightBounds+=FVector(Vertex.Position);
                        int32 Influences=0;uint32 Sum=0;
                        for (int32 I=0;I<MAX_TOTAL_INFLUENCES;++I)
                        {
                            if (Vertex.InfluenceWeights[I]>0) ++Influences;
                            Sum+=Vertex.InfluenceWeights[I];
                        }
                        MaxInfluences=FMath::Max(MaxInfluences,Influences);
                        if (!Sum) ++Unweighted;
                        else if (FMath::Abs(static_cast<int32>(Sum)-65535)>2) ++NonNormalized;
                        Colors.Add(Vertex.Color.DWColor());
                        ++VertexCount;
                    }
                }
                TSharedRef<FJsonObject> Data=MakeShared<FJsonObject>();
                Data->SetNumberField(TEXT("lod"),Index);
                Data->SetNumberField(TEXT("triangles"),Lod.IndexBuffer.Num()/3);
                Data->SetNumberField(TEXT("vertices"),VertexCount);
                Data->SetNumberField(TEXT("sections"),Lod.Sections.Num());
                Data->SetNumberField(TEXT("unweighted_vertices"),Unweighted);
                Data->SetNumberField(TEXT("max_influences"),MaxInfluences);
                Data->SetNumberField(TEXT("non_normalized_vertices"),NonNormalized);
                Data->SetNumberField(TEXT("unique_vertex_colors"),Colors.Num());
                Data->SetArrayField(TEXT("vertex_bounds_min_cm"),VectorValues(TightBounds.Min));
                Data->SetArrayField(TEXT("vertex_bounds_max_cm"),VectorValues(TightBounds.Max));
                Lods.Add(MakeShared<FJsonValueObject>(Data));
            }
        }
        Result->SetArrayField(TEXT("lods"),Lods);
    }
    FString Output;
    TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&Output);
    FJsonSerializer::Serialize(Result,Writer);
    return Output;
}

bool UHeliosEnemyAssetTools::EnsureSocket(USkeletalMesh* Mesh,FName SocketName,FName BoneName,FVector Offset,FRotator Rotation)
{
    if (!Mesh || Mesh->GetRefSkeleton().FindBoneIndex(BoneName)==INDEX_NONE) return false;
    Mesh->Modify();
    USkeletalMeshSocket* Socket=nullptr;
    for (USkeletalMeshSocket* Existing : Mesh->GetMeshOnlySocketList())
    {
        if (Existing && Existing->SocketName==SocketName) {Socket=Existing;break;}
    }
    if (!Socket)
    {
        Socket=NewObject<USkeletalMeshSocket>(Mesh,NAME_None,RF_Transactional);
        Mesh->GetMeshOnlySocketList().Add(Socket);
    }
    Socket->SocketName=SocketName;Socket->BoneName=BoneName;
    Socket->RelativeLocation=Offset;Socket->RelativeRotation=Rotation;Socket->RelativeScale=FVector::OneVector;
    Mesh->MarkPackageDirty();
    return true;
}
