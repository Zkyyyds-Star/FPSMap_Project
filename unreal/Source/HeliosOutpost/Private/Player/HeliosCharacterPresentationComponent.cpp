#include "Player/HeliosCharacterPresentationComponent.h"
#include "Player/HeliosCharacterActionProfile.h"
#include "Player/HeliosCharacterDefinition.h"
#include "Player/HeliosArenaCharacter.h"
#include "Pets/HeliosHumanoidPet.h"
#include "Weapons/HeliosFirstPersonPresentationComponent.h"
#include "Weapons/HeliosHandAnimInstance.h"
#include "Weapons/HeliosSSXVoice.h"
#include "Weapons/HeliosChanZeus.h"
#include "Weapons/HeliosBuLianShiP90.h"
#include "Weapons/HeliosWangZheKuiThunder.h"
#include "Weapons/HeliosZhongXiaoKui.h"
#include "Weapons/HeliosYunSummerMegatron.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Audio/HeliosAudio.h"
#include "Weapons/HeliosWeaponHolderContext.h"
#include "Bio/HeliosBioSupplyWeapon.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Animation/Skeleton.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "ShooterWeapon.h"
#include "Misc/ScopeExit.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#if WITH_EDITOR
#include "Rendering/SkeletalMeshModel.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#endif

UHeliosCharacterPresentationComponent::UHeliosCharacterPresentationComponent()
{PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickGroup=TG_PostUpdateWork;}
FString UHeliosCharacterPresentationComponent::ExportSkinGeometryForQA(USkeletalMesh* Mesh)
{
#if WITH_EDITOR
    // Reading the persisted UE LOD avoids FBX axis, vertex-welding, and weight
    // conversion assumptions in the independent contact audit. Never edit it.
    if(!Mesh||!Mesh->GetPathName().StartsWith(TEXT("/Game/Helios/")))return FString();
    const FSkeletalMeshModel* Model=Mesh->GetImportedModel();
    if(!Model||Model->LODModels.Num()==0)return FString();
    const FSkeletalMeshLODModel& LOD=Model->LODModels[0];
    const FReferenceSkeleton& Ref=Mesh->GetRefSkeleton();
    if(!LOD.NumVertices||Ref.GetNum()==0)return FString();
    auto Report=MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("mesh"),Mesh->GetPathName());
    Report->SetStringField(TEXT("method"),TEXT("Persisted UE imported LOD0 soft skin vertices and section bone maps; read only"));
    Report->SetStringField(TEXT("matrix_convention"),TEXT("Unreal row vectors; component ref matrices contain translation in row 3"));
    Report->SetNumberField(TEXT("weight_denominator"),65535);
    TArray<TSharedPtr<FJsonValue>> Bones;
    TArray<FTransform> ComponentRef;ComponentRef.SetNum(Ref.GetNum());
    for(int32 Index=0;Index<Ref.GetNum();++Index)
    {
        const int32 Parent=Ref.GetParentIndex(Index);
        ComponentRef[Index]=Parent==INDEX_NONE?Ref.GetRefBonePose()[Index]:Ref.GetRefBonePose()[Index]*ComponentRef[Parent];
        auto Bone=MakeShared<FJsonObject>();
        Bone->SetStringField(TEXT("name"),Ref.GetBoneName(Index).ToString());
        Bone->SetNumberField(TEXT("parent"),Parent);
        const FMatrix Matrix=ComponentRef[Index].ToMatrixWithScale();
        TArray<TSharedPtr<FJsonValue>> Rows;
        for(int32 Row=0;Row<4;++Row)
        {
            TArray<TSharedPtr<FJsonValue>> Values;
            for(int32 Col=0;Col<4;++Col)Values.Add(MakeShared<FJsonValueNumber>(Matrix.M[Row][Col]));
            Rows.Add(MakeShared<FJsonValueArray>(Values));
        }
        Bone->SetArrayField(TEXT("ref_component_matrix"),Rows);
        Bones.Add(MakeShared<FJsonValueObject>(Bone));
    }
    Report->SetArrayField(TEXT("bones"),Bones);
    TArray<TSharedPtr<FJsonValue>> Vertices;Vertices.SetNum(LOD.NumVertices);
    TArray<TSharedPtr<FJsonValue>> Sections;
    for(int32 SectionIndex=0;SectionIndex<LOD.Sections.Num();++SectionIndex)
    {
        const FSkelMeshSection& Section=LOD.Sections[SectionIndex];
        auto SectionJson=MakeShared<FJsonObject>();
        SectionJson->SetNumberField(TEXT("material_index"),Section.MaterialIndex);
        SectionJson->SetNumberField(TEXT("first_index"),Section.BaseIndex);
        SectionJson->SetNumberField(TEXT("triangles"),Section.NumTriangles);
        SectionJson->SetNumberField(TEXT("first_vertex"),Section.BaseVertexIndex);
        SectionJson->SetNumberField(TEXT("vertices"),Section.SoftVertices.Num());
        SectionJson->SetBoolField(TEXT("disabled"),Section.bDisabled);
        Sections.Add(MakeShared<FJsonValueObject>(SectionJson));
        for(int32 LocalIndex=0;LocalIndex<Section.SoftVertices.Num();++LocalIndex)
        {
            const int32 Index=Section.BaseVertexIndex+LocalIndex;
            if(!Vertices.IsValidIndex(Index)||Vertices[Index].IsValid())return FString();
            const FSoftSkinVertex& Vertex=Section.SoftVertices[LocalIndex];
            auto Item=MakeShared<FJsonObject>();
            Item->SetNumberField(TEXT("section"),SectionIndex);
            TArray<TSharedPtr<FJsonValue>> Position,Normal,Influences;
            for(int32 Axis=0;Axis<3;++Axis)
            {
                Position.Add(MakeShared<FJsonValueNumber>(Vertex.Position[Axis]));
                Normal.Add(MakeShared<FJsonValueNumber>(Vertex.TangentZ[Axis]));
            }
            Item->SetArrayField(TEXT("position_cm"),Position);Item->SetArrayField(TEXT("normal"),Normal);
            uint32 Sum=0;
            for(int32 Influence=0;Influence<MAX_TOTAL_INFLUENCES;++Influence)
            {
                const uint16 Weight=Vertex.InfluenceWeights[Influence];if(!Weight)continue;
                if(!Section.BoneMap.IsValidIndex(Vertex.InfluenceBones[Influence]))return FString();
                const int32 BoneIndex=Section.BoneMap[Vertex.InfluenceBones[Influence]];
                if(!Ref.IsValidIndex(BoneIndex))return FString();
                TArray<TSharedPtr<FJsonValue>> Value;
                Value.Add(MakeShared<FJsonValueNumber>(BoneIndex));
                Value.Add(MakeShared<FJsonValueNumber>(double(Weight)/65535.));
                Influences.Add(MakeShared<FJsonValueArray>(Value));Sum+=Weight;
            }
            if(!Sum)return FString();
            Item->SetNumberField(TEXT("raw_weight_sum"),Sum);Item->SetArrayField(TEXT("influences"),Influences);
            Vertices[Index]=MakeShared<FJsonValueObject>(Item);
        }
    }
    for(const auto& Vertex:Vertices)if(!Vertex.IsValid())return FString();
    TArray<TSharedPtr<FJsonValue>> Indices;
    for(uint32 Index:LOD.IndexBuffer)
    {if(Index>=LOD.NumVertices)return FString();Indices.Add(MakeShared<FJsonValueNumber>(Index));}
    Report->SetArrayField(TEXT("vertices"),Vertices);Report->SetArrayField(TEXT("indices"),Indices);Report->SetArrayField(TEXT("sections"),Sections);
    FString Result;FJsonSerializer::Serialize(Report,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Result));return Result;
#else
    return FString();
#endif
}
bool UHeliosCharacterPresentationComponent::FinalizeDerivedAnimation(UAnimSequence* Animation)
{
#if WITH_EDITOR
    if(!Animation||(!Animation->GetPathName().StartsWith(TEXT("/Game/Helios/LoadoutPresentation/"))&&!Animation->GetPathName().StartsWith(TEXT("/Game/Helios/CharacterForms/cfm_guanxiaoyu/"))&&!Animation->GetPathName().StartsWith(TEXT("/Game/Helios/CharacterForms/cfm_qingya/"))))return false;
    // A model's source rate alone does not change compressed runtime sampling.
    // Native ZXK tracks use 120Hz; notify population preserves that rate instead
    // of resampling corrected palm/weapon contact tracks to the first FBX's 30Hz.
    Animation->GetController().NotifyPopulated();Animation->PostEditChange();Animation->CacheDerivedDataForCurrentPlatform();Animation->MarkPackageDirty();return true;
#else
    return false;
#endif
}
bool UHeliosCharacterPresentationComponent::SetDerivedFingertipSocket(USkeletalMesh* Mesh,FName SocketName,FName BoneName,FVector LocalLocation)
{
#if WITH_EDITOR
    if(!Mesh||!Mesh->GetPathName().StartsWith(TEXT("/Game/Helios/LoadoutPresentation/"))||
       (SocketName!=TEXT("IndexTip")&&SocketName!=TEXT("ThumbTip"))||
       Mesh->GetRefSkeleton().FindBoneIndex(BoneName)==INDEX_NONE||LocalLocation.ContainsNaN())return false;
    // Different characters share motion skeletons but have different fingertip
    // lengths. Keep these offsets on each derived mesh; never overwrite the
    // shared skeleton's socket or modify a native motion donor.
    Mesh->Modify();USkeletalMeshSocket* Socket=nullptr;
    for(const auto& Candidate:Mesh->GetMeshOnlySocketList())
        if(Candidate&&Candidate->SocketName==SocketName){Socket=Candidate;break;}
    if(!Socket){Socket=NewObject<USkeletalMeshSocket>(Mesh);Socket->SocketName=SocketName;Mesh->GetMeshOnlySocketList().Add(Socket);}
    Socket->Modify();Socket->BoneName=BoneName;Socket->RelativeLocation=LocalLocation;
    Socket->RelativeRotation=FRotator::ZeroRotator;Socket->RelativeScale=FVector::OneVector;Socket->bForceAlwaysAnimated=true;
    Mesh->PostEditChange();Mesh->MarkPackageDirty();Mesh->RebuildSocketMap();return true;
#else
    return false;
#endif
}
bool UHeliosCharacterPresentationComponent::Configure(FName Character,FName Variant,FName Weapon)
{
    const auto* Definition=FHeliosCharacterRegistry::FindCharacter(Character);
    const auto* Form=Definition?Definition->FindForm(Variant):nullptr;
    if(!Definition||!Form||!Definition->bPresentationReady||!Form->bPresentationReady)return false;
    FString Path=Form->ActionProfile.ToSoftObjectPath().ToString();
    if(Path.IsEmpty())Path=Definition->ActionProfile.ToSoftObjectPath().ToString();
    if(Path.IsEmpty()&&Form->VariantId.IsNone())Path=FString::Printf(TEXT("/Game/Helios/LoadoutPresentation/DA_%s.DA_%s"),*Character.ToString(),*Character.ToString());
    auto* NewProfile=LoadObject<UHeliosCharacterActionProfile>(nullptr,*Path);
    if(!NewProfile||NewProfile->CharacterId!=Definition->CharacterId||NewProfile->CharacterVariantId!=Form->VariantId)
    {UE_LOG(LogTemp,Error,TEXT("LOADOUT_FORM_PRESENTATION_MISSING character=%s variant=%s profile=%s"),*Character.ToString(),*Form->VariantId.ToString(),*Path);return false;}
    const auto* NewPair=NewProfile->FindWeapon(Weapon);
    if(!NewPair||!NewPair->bGeneratedAndValidated)return false;
    // Reject missing/incompatible assets before changing the current presentation.
    auto* Mesh=NewPair->ThirdPersonMesh.LoadSynchronous();
    const auto* IdlePath=NewPair->ThirdPersonClips.Find(TEXT("Idle"));
    auto* Idle=IdlePath?IdlePath->LoadSynchronous():nullptr;
    if(!Mesh||!Idle||!Mesh->GetSkeleton()||Idle->GetSkeleton()!=Mesh->GetSkeleton())return false;
    CancelCharacterGesture();if(Voice)Voice->Stop();
    bReady=false;Profile=NewProfile;Pair=NewPair;ThirdPersonWeaponMode=0;LastMissingAction=NAME_None;LockedPose=NAME_None;PoseLockRemaining=0;
    PendingPose=NAME_None;bResetPoseHistory=true;PoseCommitFrame=MAX_uint64;
    Equipped.CharacterId=Definition->CharacterId;Equipped.CharacterVariantId=Form->VariantId;Equipped.PrimaryWeaponId=Weapon;
    return true;
}
bool UHeliosCharacterPresentationComponent::ConfigureFirstPerson(FName Character,FName Variant,FName Weapon,USceneComponent* Camera,AShooterWeapon* InWeapon)
{
    if(!Camera||!Configure(Character,Variant,Weapon))return false;bThirdPerson=false;CameraRoot=Camera;
    ConfigureVoice(Equipped.CharacterId,Equipped.CharacterVariantId);
    auto* Mesh=Pair->FirstPersonMesh.LoadSynchronous();if(!Mesh)return false;
    if(!VisibleMesh||!bOwnsMesh){VisibleMesh=NewObject<USkeletalMeshComponent>(GetOwner(),TEXT("SelectedCharacterArms"));VisibleMesh->SetupAttachment(Camera);VisibleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);VisibleMesh->SetCastShadow(false);VisibleMesh->SetOnlyOwnerSee(true);VisibleMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);VisibleMesh->RegisterComponent();bOwnsMesh=true;}
    VisibleMesh->SetLeaderPoseComponent(nullptr);VisibleMesh->SetSkeletalMesh(Mesh);VisibleMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);VisibleMesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;VisibleMesh->SetBoundsScale(4);VisibleMesh->SetVisibility(true);
    bReady=true;
    if(WeaponVisual)WeaponVisual->DestroyComponent();WeaponVisual=nullptr;
    if(AlternateWeaponVisual)AlternateWeaponVisual->DestroyComponent();AlternateWeaponVisual=nullptr;
    if(auto* WeaponMesh=Pair->FirstPersonWeaponMesh.LoadSynchronous())
    {WeaponVisual=NewObject<USkeletalMeshComponent>(GetOwner(),TEXT("SelectedFirstPersonWeapon"));WeaponVisual->SetupAttachment(VisibleMesh);WeaponVisual->SetSkeletalMesh(WeaponMesh);WeaponVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);WeaponVisual->SetCastShadow(false);WeaponVisual->SetOnlyOwnerSee(true);WeaponVisual->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);WeaponVisual->SetLeaderPoseComponent(VisibleMesh);WeaponVisual->SetBoundsScale(4);WeaponVisual->RegisterComponent();}
    BindWeaponSource(InWeapon);return true;
}
bool UHeliosCharacterPresentationComponent::ConfigureThirdPerson(FName Character,FName Variant,FName Weapon,USkeletalMeshComponent* Body)
{
    if(!Body||!Configure(Character,Variant,Weapon))return false;bThirdPerson=true;CameraRoot=nullptr;
    ConfigureVoice(Equipped.CharacterId,Equipped.CharacterVariantId);
    auto* Mesh=Pair->ThirdPersonMesh.LoadSynchronous();if(!Mesh)return false;
    if(bOwnsMesh&&VisibleMesh)VisibleMesh->DestroyComponent();bOwnsMesh=false;VisibleMesh=Body;Body->SetSkeletalMesh(Mesh);Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);Body->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    // Only the late presentation commit evaluates this visible body. Automatic
    // evaluation plus manual RefreshBoneTransforms flips its previous-pose buffer
    // twice in one frame and produces incorrect skeletal motion vectors.
    Body->SetComponentTickEnabled(false);Body->bEnableUpdateRateOptimizations=false;
    AddTickPrerequisiteActor(GetOwner());
    if(auto* Character=Cast<ACharacter>(GetOwner()))AddTickPrerequisiteComponent(Character->GetCharacterMovement());
    Body->UnHideBoneByName(TEXT("b_LeftWeapon"));
    bReady=true;
    if(WeaponVisual)WeaponVisual->DestroyComponent();WeaponVisual=nullptr;
    if(AlternateWeaponVisual)AlternateWeaponVisual->DestroyComponent();AlternateWeaponVisual=nullptr;
    if(auto* WeaponMesh=Pair->ThirdPersonWeaponMesh.LoadSynchronous())
    {WeaponVisual=NewObject<USkeletalMeshComponent>(GetOwner(),TEXT("SelectedThirdPersonWeapon"));WeaponVisual->SetupAttachment(Body);WeaponVisual->SetSkeletalMesh(WeaponMesh);WeaponVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);WeaponVisual->SetLeaderPoseComponent(Body);WeaponVisual->SetBoundsScale(3);WeaponVisual->RegisterComponent();WeaponVisual->SetComponentTickEnabled(false);}
    SetPose(TEXT("Idle"),0,true);return true;
}
bool UHeliosCharacterPresentationComponent::ApplyLoadout(USkeletalMeshComponent* Body,const FHeliosLoadout& L)
{return ConfigureThirdPerson(L.CharacterId,L.CharacterVariantId,L.PrimaryWeaponId,Body);}
bool UHeliosCharacterPresentationComponent::ApplyLoadout(USceneComponent* Camera,const FHeliosLoadout& L,AShooterWeapon* W)
{return ConfigureFirstPerson(L.CharacterId,L.CharacterVariantId,L.PrimaryWeaponId,Camera,W);}
bool UHeliosCharacterPresentationComponent::AttachWeapon(AShooterWeapon* W){BindWeaponSource(W);return W!=nullptr;}
void UHeliosCharacterPresentationComponent::BindWeaponSource(AShooterWeapon* W)
{
    if(IsValid(SourceWeapon))RemoveTickPrerequisiteActor(SourceWeapon);
    SourceWeapon=W;if(W)AddTickPrerequisiteActor(W);
}
UAnimSequence* UHeliosCharacterPresentationComponent::ResolvePose(FName Action)const
{
    if(bProfileGesture&&ActiveProfileGesture&&Action==Gesture)return ActiveProfileGesture->ThirdPersonClip.LoadSynchronous();
    if(!Pair)return nullptr;const auto& Clips=bThirdPerson?Pair->ThirdPersonClips:Pair->FirstPersonClips;
    const auto* P=Clips.Find(Action);return P?P->LoadSynchronous():nullptr;
}
bool UHeliosCharacterPresentationComponent::SetPose(FName Action,float Seconds,bool Loop)
{
    if(bUmbrellaOverride)return false;
    UAnimSequence* A=ResolvePose(Action);
    if(!bReady||!VisibleMesh||!A)
    {
        if(bReady&&LastMissingAction!=Action)
        {
            LastMissingAction=Action;
            UE_LOG(LogTemp,Error,TEXT("LOADOUT_ACTION_MISSING character=%s weapon=%s view=%s action=%s"),*Equipped.CharacterId.ToString(),*Equipped.PrimaryWeaponId.ToString(),bThirdPerson?TEXT("TP"):TEXT("FP"),*Action.ToString());
        }
        return false;
    }
    auto* Mesh=VisibleMesh->GetSkeletalMeshAsset();
    if(!Mesh||!A->GetSkeleton()||A->GetSkeleton()!=Mesh->GetSkeleton())
    {
        UE_LOG(LogTemp,Error,TEXT("LOADOUT_RIG_MISMATCH character=%s weapon=%s action=%s"),*Equipped.CharacterId.ToString(),*Equipped.PrimaryWeaponId.ToString(),*Action.ToString());
        return false;
    }
    if(bThirdPerson&&!bCommittingPose)
    {
        PendingPose=Action;PendingPoseSeconds=Seconds;bPendingPoseLoop=Loop;
        if(Action==TEXT("Hit")||Action==TEXT("Down")||Action==TEXT("Wake"))
        {LockedPose=Action;PoseLockRemaining=FMath::Max(.01f,A->GetPlayLength()-Seconds);}
        return true;
    }
    // This component samples after the body's normal tick. Finish its previous
    // evaluation before changing the single-node asset or sampling its proxy.
    VisibleMesh->HandleExistingParallelEvaluationTask(true,true);
#if WITH_EDITOR
    // Loading a newly derived sequence can start asynchronous compression.
    // LoadSynchronous completes UObject loading, not that animation task. Do not
    // evaluate the new asset until its current-platform compressed data exists.
    const bool WasCompressionReady=A->IsCompressedDataValid();
    if(!WasCompressionReady)A->WaitOnExistingCompression();
    const bool NeededCache=!A->IsCompressedDataValid();
    if(NeededCache)
    {
        A->CacheDerivedDataForCurrentPlatform();
        A->WaitOnExistingCompression();
    }
    if(!A->IsCompressedDataValid())
    {
        UE_LOG(LogTemp,Error,TEXT("LOADOUT_ACTION_COMPRESSION_INVALID character=%s weapon=%s action=%s asset=%s"),*Equipped.CharacterId.ToString(),*Equipped.PrimaryWeaponId.ToString(),*Action.ToString(),*A->GetPathName());
        return false;
    }
    if(!WasCompressionReady||NeededCache)
        UE_LOG(LogTemp,Display,TEXT("LOADOUT_ACTION_COMPRESSION_READY character=%s weapon=%s action=%s asset=%s"),*Equipped.CharacterId.ToString(),*Equipped.PrimaryWeaponId.ToString(),*Action.ToString(),*A->GetPathName());
#endif
    auto* Instance=VisibleMesh->GetSingleNodeInstance();
    if(!Instance)
    {
        UE_LOG(LogTemp,Error,TEXT("LOADOUT_ACTION_INSTANCE_MISSING character=%s weapon=%s action=%s"),*Equipped.CharacterId.ToString(),*Equipped.PrimaryWeaponId.ToString(),*Action.ToString());
        return false;
    }
    if(Instance->GetCurrentAsset()!=A)VisibleMesh->SetAnimation(A);
    if(bThirdPerson&&!bCommittingPose&&(Action==TEXT("Hit")||Action==TEXT("Down")||Action==TEXT("Wake")))
    {
        LockedPose=Action;
        PoseLockRemaining=FMath::Max(.01f,A->GetPlayLength()-Seconds);
    }
    // Keep the source weapon's authoritative action clock, including after a
    // first-use compression wait; this layer never advances damage or ammo.
    const float SampleSeconds=Loop?FMath::Fmod(FMath::Max(0.f,Seconds),FMath::Max(.001f,A->GetPlayLength())):FMath::Clamp(Seconds,0.f,A->GetPlayLength());
    VisibleMesh->SetPosition(SampleSeconds,false);
    VisibleMesh->Stop();
    VisibleMesh->TickAnimation(0,false);
    VisibleMesh->RefreshBoneTransforms();
    return true;
}
void UHeliosCharacterPresentationComponent::CommitThirdPersonPose()
{
    if(!bThirdPerson||!bReady||!VisibleMesh||PendingPose.IsNone()||PoseCommitFrame==GFrameCounter)return;
    bCommittingPose=true;
    const bool Committed=SetPose(PendingPose,PendingPoseSeconds,bPendingPoseLoop);
    bCommittingPose=false;
    if(!Committed)return;
    PoseCommitFrame=GFrameCounter;++PoseCommitCount;
    VisibleMesh->UpdateChildTransforms();
    if(bResetPoseHistory)
    {
        VisibleMesh->ClearMotionVector();
        if(WeaponVisual)WeaponVisual->ClearMotionVector();
        bResetPoseHistory=false;
    }
}
bool UHeliosCharacterPresentationComponent::PresentSourcePose(UAnimSequence* Source,float Seconds,bool Loop)
{
    if(!Source)return false;
    // A source controller with no TP motion leaves the body's ordinary idle or
    // locomotion pose running; it must not freeze moving legs into an equip alias.
    if(bThirdPerson)if(auto* Supply=Cast<AHeliosBioSupplyWeapon>(SourceWeapon);Supply&&Supply->ShouldKeepThirdPersonIdle(Source))return true;
    const FName Name(Source->GetName());if(bThirdPerson)if(auto* Target=ResolvePose(Name))Seconds*=Target->GetPlayLength()/FMath::Max(.001f,Source->GetPlayLength());return SetPose(Name,Seconds,Loop);
}
FTransform UHeliosCharacterPresentationComponent::GetAnchorTransform(FName Anchor)const
{if(!VisibleMesh)return FTransform::Identity;
    if(bThirdPerson){if(Anchor==TEXT("Muzzle")&&WeaponVisual&&Pair)return Pair->WeaponAttachment*VisibleMesh->GetSocketTransform(TEXT("b_RightWeapon"));
        // Mesh-specific sockets follow the real distal skin surface on the
        // full finger rig. Keep the native distal fallback for legacy meshes.
        if(Anchor==TEXT("IndexTip")||Anchor==TEXT("ThumbTip"))
        {
            if(VisibleMesh->DoesSocketExist(Anchor))return VisibleMesh->GetSocketTransform(Anchor);
            const FName Distal=Anchor==TEXT("IndexTip")?FName("Bip01_R_Finger11"):FName("Bip01_R_Finger01");
            if(VisibleMesh->DoesSocketExist(Distal))return VisibleMesh->GetSocketTransform(Distal);
        }
        const FName Bone=Anchor==TEXT("LeftHand")?FName("b_LeftHand"):FName("b_RightHand");return VisibleMesh->GetSocketTransform(Bone);}
    const FName* Name=Pair?Pair->Anchors.Find(Anchor):nullptr;return VisibleMesh->GetSocketTransform(Name?*Name:Anchor);}
float UHeliosCharacterPresentationComponent::GetRecoveryDuration(FName Pose,float Fallback)const
{if(auto* A=ResolvePose(Pose))return A->GetPlayLength();return Fallback;}
// Form voices use exact extracted event/media paths.
#include "Weapons/HeliosCharacterFormVoiceSources.inl"
#include "Weapons/HeliosQingYaVoiceSources.inl"

void UHeliosCharacterPresentationComponent::ConfigureVoice(FName Id,FName Variant)
{
    if(!Voice){Voice=NewObject<UHeliosSSXVoiceComponent>(GetOwner(),TEXT("SelectedCharacterVoice"));Voice->RegisterComponent();}
    if(Id==TEXT("cfm_chan"))HeliosChanZeus::InitializeVoice(Voice,bThirdPerson);
    else if(Id==TEXT("cfm_bulianshi"))HeliosBuLianShiP90::InitializeFormVoice(Voice,Variant,bThirdPerson);
    else if(Id==TEXT("cfm_wangzhekui"))HeliosWangZheKuiThunder::InitializeFormVoice(Voice,Variant,bThirdPerson);
    else if(Id==TEXT("cfm_yun_summer"))HeliosYunSummer::InitializeVoice(Voice,Variant==TEXT("GR")?EHeliosYunSummerForm::GR:Variant==TEXT("BL")?EHeliosYunSummerForm::BL:EHeliosYunSummerForm::SP,bThirdPerson);
    else if(Id==TEXT("cfm_guanxiaoyu"))HeliosCharacterFormVoice::Initialize(TEXT("cfm_guanxiaoyu"),Variant,Voice,bThirdPerson);
    else if(Id==TEXT("cfm_qingya"))HeliosQingYaVoice::Initialize(Variant,Voice,bThirdPerson);
    else if(Id==TEXT("cfm_zhongxiaokui"))HeliosZXK::InitializeVoice(Voice,bThirdPerson);
    else Voice->Initialize(bThirdPerson);
}
void UHeliosCharacterPresentationComponent::PlayCharacterCue(FName Cue)
{
    if(!Voice)return;
    if(Equipped.CharacterId!=TEXT("cfm_qingya")){Voice->Play(Cue);return;}
    if(Cue!=TEXT("Kill")){Voice->PlayOriginalEvent(Cue,Cue);return;}
    if(!GetWorld())return;
    const double Now=GetWorld()->GetTimeSeconds();int32 Streak=0;
    const auto* Feedback=GetWorld()->GetSubsystem<UHeliosKillFeedbackSubsystem>();
    if(Cast<AHeliosArenaCharacter>(GetOwner())&&Feedback&&Feedback->GetState().Total>0&&FMath::IsNearlyEqual(Feedback->GetState().LastKill,Now,1.e-4))
    {
        const auto& State=Feedback->GetState();
        // Central death reporting and native weapon callbacks may both arrive.
        if(QingYaLastVoiceKillTotal==State.Total&&QingYaLastVoiceKillTime==State.LastKill)return;
        QingYaLastVoiceKillTotal=State.Total;QingYaLastVoiceKillTime=State.LastKill;Streak=State.Streak;
    }
    else {QingYaLocalVoiceChain.Record(Now,false);Streak=QingYaLocalVoiceChain.Streak;}
    // The original bank supplies MultiKill2..8, but no generic single-kill
    // event. Headshot/grenade/knife lines must not be picked at random.
    if(Streak>=2)Voice->PlayOriginalEvent(*FString::Printf(TEXT("MultiKill%d"),FMath::Min(8,Streak)),TEXT("Kill"));
}
bool UHeliosCharacterPresentationComponent::SampleGestureMesh(USkeletalMeshComponent* Mesh,UAnimSequence* Clip,float Seconds)
{
    if(!Mesh||!Clip||!Mesh->GetSkeletalMeshAsset()||Mesh->GetSkeletalMeshAsset()->GetSkeleton()!=Clip->GetSkeleton())return false;
    Mesh->HandleExistingParallelEvaluationTask(true,true);
#if WITH_EDITOR
    if(!Clip->IsCompressedDataValid()){Clip->WaitOnExistingCompression();if(!Clip->IsCompressedDataValid()){Clip->CacheDerivedDataForCurrentPlatform();Clip->WaitOnExistingCompression();}}
    if(!Clip->IsCompressedDataValid())return false;
#endif
    Mesh->SetLeaderPoseComponent(nullptr);
    auto* Instance=Mesh->GetSingleNodeInstance();if(!Instance)return false;
    if(Instance->GetCurrentAsset()!=Clip)Mesh->SetAnimation(Clip);
    Mesh->SetPosition(FMath::Clamp(Seconds,0.f,Clip->GetPlayLength()),false);Mesh->Stop();Mesh->TickAnimation(0,false);Mesh->RefreshBoneTransforms();return true;
}
bool UHeliosCharacterPresentationComponent::PrepareYunGesture()
{
    const FString Root=FString::Printf(TEXT("/Game/Helios/LoadoutPresentation/Forms/cfm_yun_summer/%s/Source/"),*Equipped.CharacterVariantId.ToString());
    auto LoadMesh=[&](const FString& Name){return LoadObject<USkeletalMesh>(nullptr,*(Root+Name+TEXT(".")+Name));};
    auto LoadClip=[&](const FString& Name){return LoadObject<UAnimSequence>(nullptr,*(Root+Name+TEXT(".")+Name));};
    const FString Prefix=TEXT("YUN_")+Equipped.CharacterVariantId.ToString();
    USkeletalMesh* CupMesh=LoadMesh(TEXT("SK_")+Prefix+(bThirdPerson?TEXT("_Cup_TP"):TEXT("_Cup_FP")));
    GestureArmsClip=bThirdPerson?ResolvePose(TEXT("Cheers")):LoadClip(TEXT("A_")+Prefix+TEXT("_FP_Cheers"));
    GestureCupClip=bThirdPerson?GestureArmsClip.Get():LoadClip(TEXT("A_")+Prefix+TEXT("_CUP_FP_Cheers"));
    USkeletalMesh* ArmsMesh=bThirdPerson?nullptr:LoadMesh(TEXT("SK_")+Prefix+TEXT("_FP"));
    if(!CupMesh||!GestureArmsClip||!GestureCupClip||(!bThirdPerson&&(!CameraRoot||!ArmsMesh)))return false;
    if((!bThirdPerson&&ArmsMesh->GetSkeleton()!=GestureArmsClip->GetSkeleton())||CupMesh->GetSkeleton()!=GestureCupClip->GetSkeleton())return false;
    if(bThirdPerson&&(!VisibleMesh->GetSkeletalMeshAsset()||VisibleMesh->GetSkeletalMeshAsset()->GetSkeleton()!=CupMesh->GetSkeleton()))return false;
    if(!GestureCup){GestureCup=NewObject<USkeletalMeshComponent>(GetOwner(),TEXT("SelectedCharacterGestureCup"));GestureCup->SetCollisionEnabled(ECollisionEnabled::NoCollision);GestureCup->SetCastShadow(false);GestureCup->SetGenerateOverlapEvents(false);GestureCup->SetCanEverAffectNavigation(false);GestureCup->RegisterComponent();}
    GestureCup->SetLeaderPoseComponent(nullptr);GestureCup->SetSkeletalMesh(CupMesh);GestureCup->SetAnimationMode(EAnimationMode::AnimationSingleNode);GestureCup->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;GestureCup->SetBoundsScale(4);
    GestureCup->SetOnlyOwnerSee(!bThirdPerson);GestureCup->SetFirstPersonPrimitiveType(bThirdPerson?EFirstPersonPrimitiveType::None:EFirstPersonPrimitiveType::FirstPerson);
    if(bThirdPerson){GestureCup->AttachToComponent(VisibleMesh,FAttachmentTransformRules::KeepRelativeTransform);GestureCup->SetRelativeTransform(FTransform::Identity);GestureCup->SetLeaderPoseComponent(VisibleMesh);}
    else
    {
        if(!GestureArms){GestureArms=NewObject<USkeletalMeshComponent>(GetOwner(),TEXT("SelectedCharacterGestureArms"));GestureArms->SetCollisionEnabled(ECollisionEnabled::NoCollision);GestureArms->SetCastShadow(false);GestureArms->SetOnlyOwnerSee(true);GestureArms->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);GestureArms->RegisterComponent();}
        GestureArms->SetLeaderPoseComponent(nullptr);GestureArms->SetSkeletalMesh(ArmsMesh);GestureArms->SetAnimationMode(EAnimationMode::AnimationSingleNode);GestureArms->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;GestureArms->SetBoundsScale(4);
        const FTransform View(FRotator(0,-90,0),FVector(0,0,-6));
        GestureArms->AttachToComponent(CameraRoot,FAttachmentTransformRules::KeepRelativeTransform);GestureArms->SetRelativeTransform(View);
        GestureCup->AttachToComponent(CameraRoot,FAttachmentTransformRules::KeepRelativeTransform);GestureCup->SetRelativeTransform(View);
    }
    GestureCup->SetVisibility(false);if(GestureArms)GestureArms->SetVisibility(false);
    GestureDuration=GestureArmsClip->GetPlayLength();return GestureDuration>0;
}
bool UHeliosCharacterPresentationComponent::BeginProfileGesture(const FHeliosCharacterGesture& Definition)
{
    if(!IsValid(SourceWeapon)||!VisibleMesh||!GetWorld()||GetWorld()->IsPaused())return false;
    if(const auto* P=Cast<AHeliosArenaCharacter>(GetOwner());P&&P->IsReloading())return false;
    if(const auto* Pet=Cast<AHeliosHumanoidPet>(GetOwner());Pet&&Pet->IsReloading())return false;
    auto* Clip=(bThirdPerson?Definition.ThirdPersonClip:Definition.FirstPersonClip).LoadSynchronous();
    auto* Mesh=bThirdPerson?VisibleMesh->GetSkeletalMeshAsset():Definition.FirstPersonMesh.LoadSynchronous();
    if(!Clip||!Mesh||Clip->GetSkeleton()!=Mesh->GetSkeleton()||(!bThirdPerson&&!CameraRoot))return false;
    CancelCharacterGesture();ActiveProfileGesture=&Definition;bProfileGesture=true;Gesture=Definition.GestureId;
    GestureAge=0;GestureDuration=Clip->GetPlayLength();bGestureSoundPlayed=false;GestureAudioEvents=GestureEffectEvents=0;
    SourceWeapon->StopFiring();SourceWeapon->StopSecondaryFire();
    bSavedBodyVisible=VisibleMesh->IsVisible();bSavedWeaponVisible=WeaponVisual&&WeaponVisual->IsVisible();bSavedAlternateVisible=AlternateWeaponVisual&&AlternateWeaponVisual->IsVisible();
    if(!bThirdPerson)
    {
        VisibleMesh->SetVisibility(false);
        if(!GestureArms){GestureArms=NewObject<USkeletalMeshComponent>(GetOwner(),TEXT("SelectedCharacterGestureArms"));GestureArms->SetCollisionEnabled(ECollisionEnabled::NoCollision);GestureArms->SetCastShadow(false);GestureArms->SetOnlyOwnerSee(true);GestureArms->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);GestureArms->RegisterComponent();}
        GestureArms->SetLeaderPoseComponent(nullptr);GestureArms->SetSkeletalMesh(Mesh);GestureArms->SetAnimationMode(EAnimationMode::AnimationSingleNode);GestureArms->SetBoundsScale(4);GestureArms->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        GestureArms->AttachToComponent(CameraRoot,FAttachmentTransformRules::KeepRelativeTransform);GestureArms->SetRelativeTransform(Definition.FirstPersonTransform);GestureArms->SetVisibility(true);GestureArmsClip=Clip;SampleGestureMesh(GestureArms,Clip,0);
        auto* Driver=SourceWeapon->GetFirstPersonMesh();if(auto* P=GetOwner()->FindComponentByClass<UHeliosFirstPersonPresentationComponent>();P&&P->IsReady())Driver=P->GetArmsMesh();
        if(Driver&&Driver!=VisibleMesh){GestureHiddenDriver=Driver;bSavedDriverVisible=Driver->IsVisible();Driver->SetVisibility(false,false);}
    }
    else SetPose(Gesture,0,false);
    if(WeaponVisual)WeaponVisual->SetVisibility(false);if(AlternateWeaponVisual)AlternateWeaponVisual->SetVisibility(false);
    TArray<AActor*> Actors;SourceWeapon->GetAttachedActors(Actors,true,true);Actors.Add(SourceWeapon);
    for(auto* Actor:Actors)if(IsValid(Actor)){GestureActorVisibility.Add(Actor,Actor->IsHidden());Actor->SetActorHiddenInGame(true);}
    UE_LOG(LogTemp,Display,TEXT("CHARACTER_GESTURE_BEGIN character=%s variant=%s action=%s seconds=%.6f"),*Equipped.CharacterId.ToString(),*Equipped.CharacterVariantId.ToString(),*Gesture.ToString(),GestureDuration);
    return true;
}
bool UHeliosCharacterPresentationComponent::BeginCharacterGesture(FName InGesture)
{
    if(Profile&&bReady&&!HeliosWeaponHolderContext::IsDead(GetOwner()))
        if(const auto* Definition=Profile->FindGesture(InGesture))return BeginProfileGesture(*Definition);
    if(!bReady||!VisibleMesh||!IsValid(SourceWeapon)||!GetWorld()||GetWorld()->IsPaused()||HeliosWeaponHolderContext::IsDead(GetOwner()))return false;
    if(const auto* Player=Cast<AHeliosArenaCharacter>(GetOwner());Player&&Player->IsReloading())return false;
    if(const auto* Pet=Cast<AHeliosHumanoidPet>(GetOwner());Pet&&Pet->IsReloading())return false;
    if(Equipped.CharacterId==TEXT("cfm_yun_summer")&&InGesture==TEXT("Cheers"))
    {
        CancelCharacterGesture();
        if(!PrepareYunGesture())return false;
        SourceWeapon->StopFiring();Gesture=InGesture;GestureAge=0;bYunGesture=true;bGestureSoundPlayed=false;
        bSavedBodyVisible=VisibleMesh->IsVisible();bSavedWeaponVisible=WeaponVisual&&WeaponVisual->IsVisible();bSavedAlternateVisible=AlternateWeaponVisual&&AlternateWeaponVisual->IsVisible();
        if(!bThirdPerson)VisibleMesh->SetVisibility(false);
        if(WeaponVisual)WeaponVisual->SetVisibility(false);
        if(AlternateWeaponVisual)AlternateWeaponVisual->SetVisibility(false);
        if(!bThirdPerson)
        {
            auto* Driver=SourceWeapon->GetFirstPersonMesh();
            if(auto* P=GetOwner()->FindComponentByClass<UHeliosFirstPersonPresentationComponent>();P&&P->IsReady())Driver=P->GetArmsMesh();
            if(Driver&&Driver!=VisibleMesh){GestureHiddenDriver=Driver;bSavedDriverVisible=Driver->IsVisible();Driver->SetVisibility(false,false);}
        }
        TArray<AActor*> HiddenActors;SourceWeapon->GetAttachedActors(HiddenActors,true,true);HiddenActors.Add(SourceWeapon);
        for(auto* Actor:HiddenActors)if(IsValid(Actor)){GestureActorVisibility.Add(Actor,Actor->IsHidden());Actor->SetActorHiddenInGame(true);}
        if(GestureArms)GestureArms->SetVisibility(!bThirdPerson);GestureCup->SetVisibility(true);
        if(bThirdPerson)SetPose(TEXT("Cheers"),0,false);else {SampleGestureMesh(GestureArms,GestureArmsClip,0);SampleGestureMesh(GestureCup,GestureCupClip,0);}
        const TCHAR* Family=Equipped.CharacterVariantId==TEXT("GR")?TEXT("FamilyP"):Equipped.CharacterVariantId==TEXT("BL")?TEXT("FamilyB"):TEXT("FamilySP");
        const FString Recipe=FString::Printf(TEXT("FX_%s_%s_YunYouYou_JiuBei_%s"),bThirdPerson?TEXT("3P"):TEXT("1P"),Family,bThirdPerson?TEXT("TP"):TEXT("FP"));
        GestureEffect=HeliosYunSummer::AttachedEffect(GetOwner(),GestureCup,*Recipe,!bThirdPerson,bThirdPerson?FName(TEXT("b_RightWeapon")):FName(TEXT("B_Jiu")));
        if(GestureEffect)++GestureEffectEvents;
        UE_LOG(LogTemp,Display,TEXT("CHARACTER_GESTURE_BEGIN character=%s variant=%s weapon=%s view=%s duration=%.6f"),*Equipped.CharacterId.ToString(),*Equipped.CharacterVariantId.ToString(),*Equipped.PrimaryWeaponId.ToString(),bThirdPerson?TEXT("TP"):TEXT("FP"),GestureDuration);
        return true;
    }
    if(!ResolvePose(InGesture))return false;
    CancelCharacterGesture();Gesture=InGesture;GestureAge=0;GestureDuration=GetRecoveryDuration(InGesture,4);return true;
}
void UHeliosCharacterPresentationComponent::CancelCharacterGesture()
{
    const bool RestoreProfilePose=bProfileGesture&&bThirdPerson;
    if(bYunGesture||bProfileGesture)
    {
        for(const auto& Item:GestureActorVisibility)if(Item.Key.IsValid())Item.Key->SetActorHiddenInGame(Item.Value);
        if(VisibleMesh)VisibleMesh->SetVisibility(bSavedBodyVisible);
        if(WeaponVisual)WeaponVisual->SetVisibility(bSavedWeaponVisible);
        if(AlternateWeaponVisual)AlternateWeaponVisual->SetVisibility(bSavedAlternateVisible);
        // The source supplies motion while selected-character arms render it.
        // Generic cancellation can make its native arms visible just before J;
        // restoring that transient state would flash a second pair at the end.
        if(GestureHiddenDriver.IsValid())GestureHiddenDriver->SetVisibility(bReady&&bOwnsMesh?false:bSavedDriverVisible,false);
        if(GestureArms)GestureArms->SetVisibility(false);if(GestureCup)GestureCup->SetVisibility(false);
        if(IsValid(GestureEffect))GestureEffect->Destroy();GestureEffect=nullptr;
        if(GestureAudio)GestureAudio->Stop();GestureAudio=nullptr;
        UE_LOG(LogTemp,Display,TEXT("CHARACTER_GESTURE_END variant=%s weapon=%s seconds=%.6f"),*Equipped.CharacterVariantId.ToString(),*Equipped.PrimaryWeaponId.ToString(),GestureAge);
    }
    GestureActorVisibility.Reset();GestureHiddenDriver.Reset();bYunGesture=false;bProfileGesture=false;ActiveProfileGesture=nullptr;bGestureSoundPlayed=false;Gesture=NAME_None;GestureAge=0;
    if(RestoreProfilePose){PendingPose=TEXT("Idle");PendingPoseSeconds=0;bPendingPoseLoop=true;bResetPoseHistory=true;}
}
void UHeliosCharacterPresentationComponent::TickCharacterGesture(float Dt)
{
    GestureAge+=Dt;
    if(bProfileGesture&&ActiveProfileGesture)
    {
        if(GestureHiddenDriver.IsValid())GestureHiddenDriver->SetVisibility(false,false);
        if(bThirdPerson)SetPose(Gesture,GestureAge,false);else SampleGestureMesh(GestureArms,GestureArmsClip,GestureAge);
        if(!bGestureSoundPlayed&&GestureAge>=(bThirdPerson?ActiveProfileGesture->ThirdPersonSoundTime:ActiveProfileGesture->SoundTime))
        {
            bGestureSoundPlayed=true;
            if(auto* Sound=ActiveProfileGesture->Sound.LoadSynchronous())GestureAudio=bThirdPerson?HeliosAudio::SpawnSoundAtLocation(GetOwner(),Sound,VisibleMesh->GetComponentLocation()):HeliosAudio::SpawnSound2D(GetOwner(),Sound,.75f);
            if(GestureAudio)++GestureAudioEvents;
        }
    }
    else if(bYunGesture)
    {
        if(GestureHiddenDriver.IsValid())GestureHiddenDriver->SetVisibility(false,false);
        if(bThirdPerson)SetPose(TEXT("Cheers"),GestureAge,false);
        else {SampleGestureMesh(GestureArms,GestureArmsClip,GestureAge);SampleGestureMesh(GestureCup,GestureCupClip,GestureAge);}
        if(!bGestureSoundPlayed&&GestureAge>=.0644052f)
        {
            bGestureSoundPlayed=true;
            if(auto* Sound=HeliosYunSummer::Sound(41071))GestureAudio=bThirdPerson?HeliosAudio::SpawnSoundAtLocation(GetOwner(),Sound,GestureCup->GetComponentLocation(),FRotator::ZeroRotator,.75f):HeliosAudio::SpawnSound2D(GetOwner(),Sound,.75f);
            if(GestureAudio)++GestureAudioEvents;
        }
    }
    else SetPose(Gesture,GestureAge,false);
    if(GestureAge>=GestureDuration)CancelCharacterGesture();
}
void UHeliosCharacterPresentationComponent::SetUmbrellaOverride(bool Active)
{
    if(Active==bUmbrellaOverride)return;
    if(Active){CancelCharacterGesture();bUmbrellaBodyVisible=VisibleMesh&&VisibleMesh->IsVisible();bUmbrellaWeaponVisible=WeaponVisual&&WeaponVisual->IsVisible();bUmbrellaAlternateVisible=AlternateWeaponVisual&&AlternateWeaponVisual->IsVisible();}
    bUmbrellaOverride=Active;
    // The main body was not sampled while hidden. Its old bone history must
    // not become the previous visible pose when returning from the umbrella.
    if(!Active){bResetPoseHistory=true;if(VisibleMesh)VisibleMesh->ClearMotionVector();if(WeaponVisual)WeaponVisual->ClearMotionVector();}
    if(VisibleMesh)VisibleMesh->SetVisibility(Active?false:bUmbrellaBodyVisible);
    if(WeaponVisual)WeaponVisual->SetVisibility(Active?false:bUmbrellaWeaponVisible);
    if(AlternateWeaponVisual)AlternateWeaponVisual->SetVisibility(Active?false:bUmbrellaAlternateVisible);
}
void UHeliosCharacterPresentationComponent::SetVisible(bool Visible)
{
    if(!Visible)CancelCharacterGesture();
    if(VisibleMesh)VisibleMesh->SetVisibility(Visible);
    if(WeaponVisual)WeaponVisual->SetVisibility(Visible);
    if(AlternateWeaponVisual)AlternateWeaponVisual->SetVisibility(false);
    if(!Visible)
    {
        bReady=false;bSourceAction=false;
        if(VisibleMesh)VisibleMesh->SetLeaderPoseComponent(nullptr);
        if(WeaponVisual)WeaponVisual->SetLeaderPoseComponent(nullptr);
        SourceWeapon=nullptr;if(Voice)Voice->Stop();
    }
}
float UHeliosCharacterPresentationComponent::GetFirstPersonViewScale()const
{
    if(bProfileGesture&&ActiveProfileGesture)return ActiveProfileGesture->FirstPersonViewScale;
    // Native cheers keeps the original Yun depth framing for every main weapon.
    if(bYunGesture)return .2f;
    const auto W=Equipped.PrimaryWeaponId;
    return W==TEXT("cfm_sunshangxiang")||W==TEXT("cfm_chan_zeus")||W==TEXT("cfm_heilonghun")||W==TEXT("cfm_bulianshi_p90")||W==TEXT("cfm_yun_summer_megatron")?.2f:1.f;
}
void UHeliosCharacterPresentationComponent::UpdateThirdPersonWeaponMode(USkeletalMeshComponent* Driver)
{
    auto* ZXK=Cast<AHeliosZhongXiaoKuiWeapon>(SourceWeapon);
    if(!bThirdPerson||!VisibleMesh||!Driver||!ZXK)
    {
        if(bThirdPerson&&VisibleMesh&&VisibleMesh->IsBoneHiddenByName(TEXT("b_LeftWeapon")))VisibleMesh->UnHideBoneByName(TEXT("b_LeftWeapon"));
        if(AlternateWeaponVisual)AlternateWeaponVisual->SetVisibility(false);
        ThirdPersonWeaponMode=0;return;
    }
    ThirdPersonWeaponMode=ZXK->GetForm();
    // Stable darts use their own visual. Transitions retain the native blade
    // trajectory while the original pending-form clock reveals the dart.
    if(WeaponVisual)WeaponVisual->SetVisibility(ZXK->GetForm()!=2||ZXK->IsChangingForm());
    // The native FP animation puts Daoqiao around Z=-100 cm when the sheath
    // is taken out of view. Read that authored pose rather than moving its
    // camera-space hiding offset down beside the selected TP body's feet.
    // All five derived body skins have zero weights and no children on this
    // leaf bone, so hiding it affects only the independent weapon follower.
    const bool HideSheath=Driver->DoesSocketExist(TEXT("Daoqiao"))&&
        Driver->GetSocketTransform(TEXT("Daoqiao"),RTS_Component).GetLocation().Z < -80.f;
    if(VisibleMesh->IsBoneHiddenByName(TEXT("b_LeftWeapon"))!=HideSheath)
    {
        VisibleMesh->HandleExistingParallelEvaluationTask(true,true);
        if(HideSheath)VisibleMesh->HideBoneByName(TEXT("b_LeftWeapon"),PBO_None);
        else VisibleMesh->UnHideBoneByName(TEXT("b_LeftWeapon"));
        VisibleMesh->RefreshBoneTransforms();
    }
    auto* SourceDart=ZXK->GetDartMesh();
    const bool ShowDart=ZXK->ShouldDisplayDart()&&SourceDart&&SourceDart->GetStaticMesh();
    if(!ShowDart){if(AlternateWeaponVisual)AlternateWeaponVisual->SetVisibility(false);return;}
    if(!Driver->DoesSocketExist(TEXT("b_RightHand"))||!VisibleMesh->DoesSocketExist(TEXT("b_RightHand")))return;
    if(!AlternateWeaponVisual)
    {
        AlternateWeaponVisual=NewObject<UStaticMeshComponent>(GetOwner(),TEXT("SelectedThirdPersonDart"));
        AlternateWeaponVisual->SetupAttachment(VisibleMesh,TEXT("b_RightHand"));
        AlternateWeaponVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        AlternateWeaponVisual->SetGenerateOverlapEvents(false);AlternateWeaponVisual->SetCanEverAffectNavigation(false);
        AlternateWeaponVisual->RegisterComponent();
    }
    if(AlternateWeaponVisual->GetStaticMesh()!=SourceDart->GetStaticMesh())AlternateWeaponVisual->SetStaticMesh(SourceDart->GetStaticMesh());
    for(int32 Slot=0;Slot<SourceDart->GetNumMaterials();++Slot)AlternateWeaponVisual->SetMaterial(Slot,SourceDart->GetMaterial(Slot));
    // Preserve the authored palm-to-dart frame, including animated scale and
    // the source static mesh's .01 FBX unit correction. Character size is supplied
    // by the selected body's actual palm socket, independently of its skin.
    SourceDart->UpdateComponentToWorld();
    const FTransform Contact=SourceDart->GetComponentTransform().GetRelativeTransform(Driver->GetSocketTransform(TEXT("b_RightHand")));
    AlternateWeaponVisual->SetRelativeTransform(Contact);AlternateWeaponVisual->SetVisibility(true);
    AlternateWeaponVisual->UpdateComponentToWorld();
}
void UHeliosCharacterPresentationComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Fn)
{
    Super::TickComponent(Dt,Type,Fn);if(bUmbrellaOverride||!bReady||!VisibleMesh||!GetWorld()||GetWorld()->IsPaused())return;
    ON_SCOPE_EXIT {CommitThirdPersonPose();};
    if(!IsValid(SourceWeapon))return;
    if(HeliosWeaponHolderContext::IsDead(GetOwner())){CancelCharacterGesture();bSourceAction=false;return;}
    if(bThirdPerson){PoseLockRemaining=FMath::Max(0.f,PoseLockRemaining-Dt);if(PoseLockRemaining>0){CancelCharacterGesture();bSourceAction=false;return;}}
    if(!Gesture.IsNone()){TickCharacterGesture(Dt);return;}
    USkeletalMeshComponent* Driver=SourceWeapon->GetFirstPersonMesh();
    if(auto* P=GetOwner()->FindComponentByClass<UHeliosFirstPersonPresentationComponent>();P&&P->IsReady())Driver=P->GetArmsMesh();
    if(!Driver||Driver==VisibleMesh)return;
    if(!bThirdPerson&&WeaponVisual)
    {
        // Weapon section rendering (including ZXK's wave pass) follows the weapon
        // source. Character skin keeps the selected character's own materials.
        auto WeaponMaterialName=[](const UMaterialInterface* Material)
        {
            FString Name=Material->GetName();
            while(Name.RemoveFromEnd(TEXT("_WaveLate"))||Name.RemoveFromEnd(TEXT("_ClearFP"))){}
            return Name;
        };
        for(int32 Slot=0;Slot<WeaponVisual->GetNumMaterials();++Slot)
        {
            auto* Current=WeaponVisual->GetMaterial(Slot);if(!Current)continue;
            const FString Base=WeaponMaterialName(Current);
            for(int32 SourceSlot=0;SourceSlot<Driver->GetNumMaterials();++SourceSlot)
            {auto* SourceMaterial=Driver->GetMaterial(SourceSlot);if(!SourceMaterial)continue;const FString SourceName=WeaponMaterialName(SourceMaterial);if(SourceName==Base&&SourceMaterial!=Current){WeaponVisual->SetMaterial(Slot,SourceMaterial);break;}}
        }
    }
    UAnimSequence* Clip=nullptr;float Seconds=0;bool Loop=false;
    if(auto* HandAnim=Cast<UHeliosHandAnimInstance>(Driver->GetAnimInstance())){Clip=HandAnim->Sequence;Seconds=HandAnim->SampleSeconds;Loop=Clip&&(Clip->GetName().Contains(TEXT("Idle"))||Clip->GetName().Contains(TEXT("Move"))||Clip->GetName().Contains(TEXT("Run_")));}
    else if(auto* SingleNode=Driver->GetSingleNodeInstance()){Clip=Cast<UAnimSequence>(SingleNode->GetCurrentAsset());Seconds=SingleNode->GetCurrentTime();Loop=SingleNode->IsLooping();}
    bSourceAction=Clip&&!Clip->GetName().Contains(TEXT("Idle"))&&!Clip->GetName().Contains(TEXT("Move"))&&!Clip->GetName().Contains(TEXT("Run_"));
    if(!bThirdPerson){VisibleMesh->SetWorldTransform(Driver->GetComponentTransform());Driver->SetVisibility(false,false);
        if(Driver->GetSkeletalMeshAsset()&&Driver->GetSkeletalMeshAsset()->GetSkeleton()==VisibleMesh->GetSkeletalMeshAsset()->GetSkeleton()){VisibleMesh->SetLeaderPoseComponent(Driver);if(WeaponVisual)WeaponVisual->SetLeaderPoseComponent(Driver);}
        else{VisibleMesh->SetLeaderPoseComponent(nullptr);if(Clip)PresentSourcePose(Clip,Seconds,Loop);}}
    else
    {
        auto* ZXK=Cast<AHeliosZhongXiaoKuiWeapon>(SourceWeapon);
        const bool ModePose=ZXK&&(ZXK->GetForm()>0||ZXK->IsChangingForm()||ZXK->GetForm()!=ThirdPersonWeaponMode);
        if((bSourceAction||ModePose)&&Clip)PresentSourcePose(Clip,Seconds,Loop);
        CommitThirdPersonPose();
        UpdateThirdPersonWeaponMode(Driver);
    }
}
void UHeliosCharacterPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{CancelCharacterGesture();if(GestureCup)GestureCup->DestroyComponent();if(GestureArms)GestureArms->DestroyComponent();if(AlternateWeaponVisual)AlternateWeaponVisual->DestroyComponent();if(WeaponVisual)WeaponVisual->DestroyComponent();if(bOwnsMesh&&VisibleMesh)VisibleMesh->DestroyComponent();if(Voice)Voice->Stop();Super::EndPlay(Reason);}
