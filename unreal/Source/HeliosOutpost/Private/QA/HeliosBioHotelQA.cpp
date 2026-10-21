#include "QA/HeliosBioHotelQA.h"
#include "Bio/HeliosBioHotelGameMode.h"
#include "Bio/HeliosBioPlayer.h"
#include "Bio/HeliosBioNPC.h"
#include "Bio/HeliosBioSupply.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Variant_Shooter/Weapons/ShooterProjectile.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Particles/ParticleSystemComponent.h"
#include "Particles/ParticleSystem.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimationAsset.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "NavigationSystem.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Weapons/HeliosChanZeus.h"
#include "EngineUtils.h"
#include "Bio/HeliosBioArt.h"
#include "Bio/HeliosBioRoleDefinition.h"
#include "Animation/AnimSequence.h"
#include "NavigationData.h"
#include "NavigationPath.h"
#include "Bio/HeliosBioClone.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UnrealType.h"
#include "Camera/CameraComponent.h"
#include "Bio/HeliosBioMovingPlatform.h"
#include "Bio/HeliosBioFourSlashComponent.h"
#include "Components/CapsuleComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "Bio/HeliosBioPlatformTraversal.h"
#include "Bio/HeliosBioStaticJumpLink.h"
#include "NavLinkCustomComponent.h"
#include "NavMesh/NavMeshPath.h"
#include "NavMesh/RecastNavMesh.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Player/HeliosCharacterPresentationComponent.h"

AHeliosBioHotelQA::AHeliosBioHotelQA(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;}
void AHeliosBioHotelQA::Check(const TCHAR* Name,bool Passed)
{
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("name"),Name);Row->SetBoolField(TEXT("passed"),Passed);
    Checks.Add(MakeShared<FJsonValueObject>(Row));if(!Passed)++Failures;
    UE_LOG(LogTemp,Display,TEXT("BIOHOTEL_QA %s %s"),Name,Passed?TEXT("PASS"):TEXT("FAIL"));
}
void AHeliosBioHotelQA::Capture(const TCHAR* Name){RecordPresentation(Name);RecordPlatformDiagnostics(Name);FScreenshotRequest::RequestScreenshot(Out/(FString(Name)+TEXT(".png")),true,false);}
void AHeliosBioHotelQA::RecordPlatformDiagnostics(const TCHAR* Phase)
{
    auto Snapshot=MakeShared<FJsonObject>();Snapshot->SetStringField(TEXT("stage"),Phase);TArray<TSharedPtr<FJsonValue>> Platforms;
    for(TActorIterator<AHeliosBioMovingPlatform> It(GetWorld());It;++It)
    {
        auto Row=MakeShared<FJsonObject>();auto* PlatformMesh=It->GetPlatformMesh();Row->SetStringField(TEXT("actor"),It->GetPathName());Row->SetBoolField(TEXT("route_configured"),It->IsRouteConfigured());Row->SetNumberField(TEXT("source_loop_type_raw"),It->GetOriginalLoopType());Row->SetStringField(TEXT("actor_location"),It->GetActorLocation().ToString());Row->SetStringField(TEXT("mesh"),PlatformMesh&&PlatformMesh->GetStaticMesh()?PlatformMesh->GetStaticMesh()->GetPathName():TEXT(""));Row->SetBoolField(TEXT("mesh_is_actor_root"),PlatformMesh&&It->GetRootComponent()==PlatformMesh);if(PlatformMesh){Row->SetStringField(TEXT("relative_transform"),PlatformMesh->GetRelativeTransform().ToHumanReadableString());Row->SetStringField(TEXT("world_transform"),PlatformMesh->GetComponentTransform().ToHumanReadableString());Row->SetBoolField(TEXT("visibility_trace_blocked"),PlatformMesh->GetCollisionResponseToChannel(ECC_Visibility)==ECR_Block);}
        if(PlatformMesh&&PlatformMesh->GetStaticMesh())
        {
            auto* Setup=PlatformMesh->GetStaticMesh()->GetBodySetup();if(Setup){const FBox SourceCOL=Setup->AggGeom.CalcAABB(FTransform::Identity);Row->SetStringField(TEXT("simple_collision_local_bounds"),SourceCOL.ToString());}
            Row->SetBoolField(TEXT("physics_state_created"),PlatformMesh->IsPhysicsStateCreated());
            const FVector Probe=PlatformMesh->Bounds.Origin,Above=Probe+FVector(0,0,600),Below=Probe-FVector(0,0,600);FHitResult H;FCollisionQueryParams Q(SCENE_QUERY_STAT(BioPlatformSpecificCOLDiagnostic),false,Mode?Mode->Player:nullptr);
            Row->SetBoolField(TEXT("original_component_simple_ray"),PlatformMesh->LineTraceComponent(H,Above,Below,Q));if(H.bBlockingHit)Row->SetStringField(TEXT("original_component_simple_hit"),H.ImpactPoint.ToString());
            Q.bTraceComplex=true;H=FHitResult();Row->SetBoolField(TEXT("original_component_complex_ray"),PlatformMesh->LineTraceComponent(H,Above,Below,Q));
            Q.bTraceComplex=false;H=FHitResult();if(GetWorld()->LineTraceSingleByChannel(H,Above,Below,ECC_Visibility,Q)){Row->SetStringField(TEXT("world_first_blocker_actor"),GetNameSafe(H.GetActor()));Row->SetStringField(TEXT("world_first_blocker_point"),H.ImpactPoint.ToString());}
        }
        Row->SetBoolField(TEXT("original_route_trigger_enum_verified"),false);Row->SetStringField(TEXT("scope"),TEXT("Observed actor/mesh/configuration only; replay policy does not establish original trigger/LoopType semantics or rider traversal."));Platforms.Add(MakeShared<FJsonValueObject>(Row));
    }
    Snapshot->SetArrayField(TEXT("platforms"),Platforms);PlatformDiagnostics.Add(MakeShared<FJsonValueObject>(Snapshot));
}
void AHeliosBioHotelQA::RecordPresentation(const TCHAR* Name)
{
    auto Snapshot=MakeShared<FJsonObject>();Snapshot->SetStringField(TEXT("stage"),Name);
    TArray<TSharedPtr<FJsonValue>> Actors;
    TArray<int32> Selected;Selected.Add(0);const int32 Z=ZombieId();if(Z!=INDEX_NONE)Selected.AddUnique(Z);
    for(const auto& P:Mode->Model.GetParticipants())if(P.Id!=0&&P.IsHumanSide()&&P.bAlive){Selected.AddUnique(P.Id);break;}
    for(int32 Id:Selected)
    {
        AActor* Actor=Mode->ParticipantActor(Id);if(!Actor)continue;
        auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("participant"),Id);Row->SetStringField(TEXT("actor_class"),Actor->GetClass()->GetPathName());
        const auto* P=Mode->Model.FindParticipant(Id);Row->SetNumberField(TEXT("team"),P?static_cast<int32>(P->Team):-1);
        Row->SetBoolField(TEXT("hunter_choice_pending"),P&&P->bHunterChoicePending);Row->SetStringField(TEXT("hunter_role_key"),P?P->HunterRoleId.ToString():TEXT(""));
        UHeliosBioRoleDefinition* ActualDefinition=P&&P->Team!=EHeliosBioTeam::Human?HeliosBioArt::Load(*P):nullptr;
        Row->SetStringField(TEXT("role_definition"),ActualDefinition?ActualDefinition->GetPathName():TEXT(""));Row->SetStringField(TEXT("source_identity"),ActualDefinition?ActualDefinition->SourceIdentity:TEXT(""));
        Row->SetStringField(TEXT("identity_scope"),TEXT("Current BIO_3RD prefab identity; SunWukong model table ID 100001085 is distinct from old ultimate 100001062. Direct ID-to-prefab link is not independently established."));
        USkeletalMesh* ExpectedRoleMesh=ActualDefinition?(Id==0?ActualDefinition->FirstPersonMesh.LoadSynchronous():ActualDefinition->ThirdPersonMesh.LoadSynchronous()):nullptr;
        Row->SetBoolField(TEXT("actor_hidden"),Actor->IsHidden());Row->SetStringField(TEXT("position"),Actor->GetActorLocation().ToString());
        TArray<AActor*> Owners;Owners.Add(Actor);Actor->GetAttachedActors(Owners,false,true);
        AShooterWeapon* Weapon=Id==0?Mode->Player->GetEquippedWeapon():Cast<AHeliosBioNPC>(Actor)->GetEquippedWeapon();
        Row->SetStringField(TEXT("weapon_class"),Weapon?Weapon->GetClass()->GetPathName():TEXT(""));
        Row->SetStringField(TEXT("weapon_first_person_mesh"),Weapon&&Weapon->GetFirstPersonMesh()->GetSkeletalMeshAsset()?Weapon->GetFirstPersonMesh()->GetSkeletalMeshAsset()->GetPathName():TEXT(""));
        Row->SetStringField(TEXT("weapon_third_person_mesh"),Weapon&&Weapon->GetThirdPersonMesh()->GetSkeletalMeshAsset()?Weapon->GetThirdPersonMesh()->GetSkeletalMeshAsset()->GetPathName():TEXT(""));
        UE_LOG(LogTemp,Display,TEXT("BIOHOTEL_ACTUAL_WEAPON stage=%s id=%d class=%s fp_mesh=%s tp_mesh=%s"),Name,Id,Weapon?*Weapon->GetClass()->GetPathName():TEXT("EMPTY"),Weapon&&Weapon->GetFirstPersonMesh()->GetSkeletalMeshAsset()?*Weapon->GetFirstPersonMesh()->GetSkeletalMeshAsset()->GetPathName():TEXT("EMPTY"),Weapon&&Weapon->GetThirdPersonMesh()->GetSkeletalMeshAsset()?*Weapon->GetThirdPersonMesh()->GetSkeletalMeshAsset()->GetPathName():TEXT("EMPTY"));
        if(Weapon)Owners.AddUnique(Weapon);
        for(TActorIterator<AHeliosSSXEffect> It(GetWorld());It;++It){AActor* EffectOwner=It->GetOwner();if(EffectOwner==Actor||EffectOwner==Weapon)Owners.AddUnique(*It);}
        TArray<TSharedPtr<FJsonValue>> Meshes,Effects;
        bool VisibleBody=false,ActualAnimation=false,ActualRoleMesh=false;
        for(AActor* ComponentOwner:Owners)
        {
            if(!IsValid(ComponentOwner))continue;
            TArray<UActorComponent*> Components;ComponentOwner->GetComponents(Components);
            for(UActorComponent* Component:Components)
            {
                if(auto* Mesh=Cast<USkeletalMeshComponent>(Component))
                {
                    auto M=MakeShared<FJsonObject>();auto* Asset=Mesh->GetSkeletalMeshAsset();
                    const bool Visible=Mesh->IsVisible()&&!Mesh->bHiddenInGame&&!ComponentOwner->IsHidden();
                    M->SetStringField(TEXT("component"),Mesh->GetPathName());M->SetStringField(TEXT("mesh"),Asset?Asset->GetPathName():TEXT(""));M->SetBoolField(TEXT("visible"),Visible);
                    M->SetBoolField(TEXT("component_tick_enabled"),Mesh->IsComponentTickEnabled());
                    M->SetStringField(TEXT("relative_transform"),Mesh->GetRelativeTransform().ToHumanReadableString());M->SetStringField(TEXT("world_transform"),Mesh->GetComponentTransform().ToHumanReadableString());M->SetStringField(TEXT("world_bounds_origin"),Mesh->Bounds.Origin.ToString());M->SetStringField(TEXT("world_bounds_extent"),Mesh->Bounds.BoxExtent.ToString());
                    M->SetBoolField(TEXT("only_owner_see"),Mesh->bOnlyOwnerSee);M->SetBoolField(TEXT("owner_no_see"),Mesh->bOwnerNoSee);M->SetBoolField(TEXT("hidden_in_game"),Mesh->bHiddenInGame);
                    const FEnumProperty* ViewProperty=FindFProperty<FEnumProperty>(Mesh->GetClass(),TEXT("FirstPersonPrimitiveType"));M->SetNumberField(TEXT("first_person_primitive_enum"),ViewProperty?ViewProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ViewProperty->ContainerPtrToValuePtr<void>(Mesh)):-1);
                    if(Id==0&&Mode->Player->FirstPersonCamera){const FTransform ViewTransform=Mode->Player->FirstPersonCamera->GetComponentTransform();M->SetStringField(TEXT("camera_space_component"),ViewTransform.InverseTransformPosition(Mesh->GetComponentLocation()).ToString());if(Mesh->GetNumBones()>0){const FTransform BoneRoot=Mesh->GetBoneTransform(0);M->SetStringField(TEXT("bone0_world_scale"),BoneRoot.GetScale3D().ToString());M->SetStringField(TEXT("bone0_camera_space"),ViewTransform.InverseTransformPosition(BoneRoot.GetLocation()).ToString());}}
                    UAnimationAsset* Clip=Mesh->GetSingleNodeInstance()?Mesh->GetSingleNodeInstance()->GetCurrentAsset():nullptr;
                    M->SetStringField(TEXT("animation"),Clip?Clip->GetPathName():TEXT(""));
                    TArray<TSharedPtr<FJsonValue>> Materials;for(int32 I=0;I<Mesh->GetNumMaterials();++I)Materials.Add(MakeShared<FJsonValueString>(Mesh->GetMaterial(I)?Mesh->GetMaterial(I)->GetPathName():TEXT("")));
                    M->SetArrayField(TEXT("materials"),Materials);Meshes.Add(MakeShared<FJsonValueObject>(M));
                    if(ComponentOwner==Actor&&Visible&&Asset){VisibleBody=true;ActualAnimation|=Clip!=nullptr;ActualRoleMesh|=ExpectedRoleMesh&&Asset==ExpectedRoleMesh;}
                    UE_LOG(LogTemp,Display,TEXT("BIOHOTEL_ACTUAL_MESH stage=%s id=%d mesh=%s anim=%s visible=%d"),Name,Id,Asset?*Asset->GetPathName():TEXT("EMPTY"),Clip?*Clip->GetPathName():TEXT("EMPTY"),Visible);
                }
                if(auto* StaticMesh=Cast<UStaticMeshComponent>(Component))
                {
                    auto StaticRow=MakeShared<FJsonObject>();StaticRow->SetStringField(TEXT("kind"),TEXT("NativeStaticMesh"));
                    StaticRow->SetStringField(TEXT("component"),StaticMesh->GetPathName());StaticRow->SetStringField(TEXT("asset"),StaticMesh->GetStaticMesh()?StaticMesh->GetStaticMesh()->GetPathName():TEXT(""));
                    StaticRow->SetBoolField(TEXT("visible"),StaticMesh->IsVisible()&&!StaticMesh->bHiddenInGame&&!ComponentOwner->IsHidden());StaticRow->SetBoolField(TEXT("active"),StaticMesh->IsActive());
                    TArray<TSharedPtr<FJsonValue>> StaticMaterials;for(int32 I=0;I<StaticMesh->GetNumMaterials();++I)StaticMaterials.Add(MakeShared<FJsonValueString>(StaticMesh->GetMaterial(I)?StaticMesh->GetMaterial(I)->GetPathName():TEXT("")));
                    StaticRow->SetArrayField(TEXT("materials"),StaticMaterials);Effects.Add(MakeShared<FJsonValueObject>(StaticRow));
                }
                FString Kind,AssetPath;bool Active=false,Visible=false;
                if(auto* NiagaraFX=Cast<UNiagaraComponent>(Component)){Kind=TEXT("Niagara");AssetPath=NiagaraFX->GetAsset()?NiagaraFX->GetAsset()->GetPathName():TEXT("");Active=NiagaraFX->IsActive();Visible=NiagaraFX->IsVisible()&&!NiagaraFX->bHiddenInGame&&!ComponentOwner->IsHidden();}
                else if(auto* ParticleFX=Cast<UParticleSystemComponent>(Component)){Kind=TEXT("Particle");AssetPath=ParticleFX->Template?ParticleFX->Template->GetPathName():TEXT("");Active=ParticleFX->IsActive();Visible=ParticleFX->IsVisible()&&!ParticleFX->bHiddenInGame&&!ComponentOwner->IsHidden();}
                else if(auto* Audio=Cast<UAudioComponent>(Component)){Kind=TEXT("Audio");AssetPath=Audio->Sound?Audio->Sound->GetPathName():TEXT("");Active=Audio->IsPlaying();}
                if(!Kind.IsEmpty()){auto EffectRow=MakeShared<FJsonObject>();EffectRow->SetStringField(TEXT("component"),Component->GetPathName());EffectRow->SetStringField(TEXT("kind"),Kind);EffectRow->SetStringField(TEXT("asset"),AssetPath);EffectRow->SetBoolField(TEXT("active"),Active);EffectRow->SetBoolField(TEXT("visible"),Visible);Effects.Add(MakeShared<FJsonValueObject>(EffectRow));UE_LOG(LogTemp,Display,TEXT("BIOHOTEL_ACTUAL_FX stage=%s id=%d kind=%s asset=%s active=%d visible=%d"),Name,Id,*Kind,*AssetPath,Active,Visible);}
            }
        }
        Row->SetArrayField(TEXT("actual_skeletal_meshes"),Meshes);Row->SetArrayField(TEXT("actual_fx_audio"),Effects);
        Row->SetBoolField(TEXT("visible_body_mesh_present"),VisibleBody);Row->SetBoolField(TEXT("visible_body_animation_present"),ActualAnimation);
        Row->SetStringField(TEXT("fx_scope"),TEXT("Actor, attached actor and owned world SSX effects; empty/inactive effects explicitly reported."));
        if(P&&P->bAlive)Check(*(FString(Name)+FString::Printf(TEXT("_id%d_visible_body"),Id)),VisibleBody);
        if(P&&P->bAlive&&P->Team!=EHeliosBioTeam::Human)Check(*(FString(Name)+FString::Printf(TEXT("_id%d_actual_role_animation"),Id)),ActualAnimation);
        if(P&&P->bAlive&&P->Team!=EHeliosBioTeam::Human)Check(*(FString(Name)+FString::Printf(TEXT("_id%d_actual_role_mesh_matches_source"),Id)),ActualRoleMesh);
        if(bModernRoleFixtureActive&&P&&P->bAlive&&(P->Team==EHeliosBioTeam::UltimateHunter||P->bMother))Check(*(FString(Name)+FString::Printf(TEXT("_id%d_modern_role_identity"),Id)),ActualDefinition&&(P->Team==EHeliosBioTeam::UltimateHunter?ActualDefinition->SourceIdentity.Contains(TEXT("SunWuKong"),ESearchCase::IgnoreCase):ActualDefinition->SourceIdentity.Contains(TEXT("BaiGuJing"),ESearchCase::IgnoreCase)));
        if(P&&P->bAlive&&P->Team==EHeliosBioTeam::Human)Check(*(FString(Name)+FString::Printf(TEXT("_id%d_weapon_asset"),Id)),Weapon&&Weapon->GetFirstPersonMesh()->GetSkeletalMeshAsset()!=nullptr);
        Actors.Add(MakeShared<FJsonValueObject>(Row));
    }
    TArray<TSharedPtr<FJsonValue>> CloneRows;
    for(auto Clone:Mode->Clones)if(IsValid(Clone))
    {
        auto CR=MakeShared<FJsonObject>();CR->SetStringField(TEXT("actor"),Clone->GetPathName());CR->SetNumberField(TEXT("mother_id"),Clone->GetMotherId());CR->SetNumberField(TEXT("health"),Clone->CurrentHP);CR->SetNumberField(TEXT("max_health"),Clone->GetCloneMaxHealth());CR->SetBoolField(TEXT("active"),Clone->IsActiveClone());
        auto* CM=Clone->GetMesh();CR->SetStringField(TEXT("mesh"),CM&&CM->GetSkeletalMeshAsset()?CM->GetSkeletalMeshAsset()->GetPathName():TEXT(""));UAnimationAsset* CA=CM&&CM->GetSingleNodeInstance()?CM->GetSingleNodeInstance()->GetCurrentAsset():nullptr;CR->SetStringField(TEXT("animation"),CA?CA->GetPathName():TEXT(""));CloneRows.Add(MakeShared<FJsonValueObject>(CR));
    }
    Snapshot->SetArrayField(TEXT("clones"),CloneRows);Snapshot->SetArrayField(TEXT("actors"),Actors);PresentationEvidence.Add(MakeShared<FJsonValueObject>(Snapshot));
}
int32 AHeliosBioHotelQA::ZombieId() const
{for(const auto& P:Mode->Model.GetParticipants())if(P.Team==EHeliosBioTeam::Zombie&&P.bAlive)return P.Id;return INDEX_NONE;}
AHeliosSSXEffect* AHeliosBioHotelQA::FindSupplyEffect(AHeliosBioSupply* Supply) const
{if(!Supply)return nullptr;for(TActorIterator<AHeliosSSXEffect> It(GetWorld());It;++It)if(It->GetOwner()==Supply&&!It->IsActorBeingDestroyed())return *It;return nullptr;}
void AHeliosBioHotelQA::RecordSupplyEffects(const TCHAR* Phase)
{
    auto Snapshot=MakeShared<FJsonObject>();Snapshot->SetStringField(TEXT("stage"),Phase);TArray<TSharedPtr<FJsonValue>> Rows;
    for(auto Supply:Mode->Supplies)if(IsValid(Supply))
    {
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("supply_actor"),Supply->GetPathName());Row->SetNumberField(TEXT("kind"),static_cast<int32>(Supply->GetKind()));Row->SetBoolField(TEXT("available"),Supply->IsAvailable());Row->SetBoolField(TEXT("blue_source_visual_verified"),Supply->bBlueSupplySourceVerified);
        auto* Effect=FindSupplyEffect(Supply);Row->SetBoolField(TEXT("owned_effect_live"),Effect!=nullptr);Row->SetStringField(TEXT("effect_actor"),Effect?Effect->GetPathName():TEXT(""));Row->SetNumberField(TEXT("visible_tracks"),Effect?Effect->VisibleTrackCount():0);
        UObject* LibraryObject=nullptr;if(Effect)if(const FObjectPropertyBase* Property=FindFProperty<FObjectPropertyBase>(Effect->GetClass(),TEXT("Library")))LibraryObject=Property->GetObjectPropertyValue_InContainer(Effect);
        auto* EffectLibrary=Cast<UHeliosSSXEffectLibrary>(LibraryObject);Row->SetStringField(TEXT("actual_library"),EffectLibrary?EffectLibrary->GetPathName():TEXT(""));const auto Parsed=EffectLibrary?EffectLibrary->GetParsedRecipe():nullptr;Row->SetBoolField(TEXT("original_recipe_in_actual_library"),Parsed.IsValid()&&Parsed->HasField(TEXT("BioAvatar_RenLeiBox")));Row->SetStringField(TEXT("original_recipe_name"),TEXT("BioAvatar_RenLeiBox"));
        TArray<TSharedPtr<FJsonValue>> Meshes;if(Effect){TArray<UStaticMeshComponent*> Components;Effect->GetComponents(Components);for(auto* Component:Components){auto MeshRow=MakeShared<FJsonObject>();MeshRow->SetStringField(TEXT("mesh"),Component->GetStaticMesh()?Component->GetStaticMesh()->GetPathName():TEXT(""));MeshRow->SetBoolField(TEXT("visible"),Component->IsVisible()&&!Component->bHiddenInGame);TArray<TSharedPtr<FJsonValue>> Materials;for(int32 I=0;I<Component->GetNumMaterials();++I)Materials.Add(MakeShared<FJsonValueString>(Component->GetMaterial(I)?Component->GetMaterial(I)->GetPathName():TEXT("")));MeshRow->SetArrayField(TEXT("materials"),Materials);Meshes.Add(MakeShared<FJsonValueObject>(MeshRow));}}
        Row->SetArrayField(TEXT("actual_effect_meshes"),Meshes);Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    Snapshot->SetArrayField(TEXT("supplies"),Rows);SupplyEffectEvidence.Add(MakeShared<FJsonValueObject>(Snapshot));
}
void AHeliosBioHotelQA::Finish()
{
    const bool IdentityGatedLifecycle=bBlueUltimateIdentityVerified&&bGenericUltimateDeathObserved&&bGenericUltimateResetObserved;
    const bool BlueAccepted=bBlueSupplySourceVerified&&bBlueUltimateIdentityVerified&&bBlueUltimateCombatVerified&&IdentityGatedLifecycle&&bBlueEffectLifecycleObserved;
    auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("passed"),BlueAccepted&&Failures==0);Report->SetNumberField(TEXT("failures"),Failures);
    auto GenericDomain=MakeShared<FJsonObject>();GenericDomain->SetBoolField(TEXT("passed"),GenericFailures==0);GenericDomain->SetBoolField(TEXT("completed"),GenericFailures>=0);GenericDomain->SetNumberField(TEXT("failures"),GenericFailures>=0?GenericFailures:Failures);GenericDomain->SetStringField(TEXT("scope"),TEXT("Controlled generic infection, movement, combat, supply enum transformation, win and reset; not blue-box source acceptance."));Report->SetObjectField(TEXT("genericMechanisms"),GenericDomain);
    auto BlueDomain=MakeShared<FJsonObject>();BlueDomain->SetBoolField(TEXT("passed"),BlueAccepted);BlueDomain->SetStringField(TEXT("status"),BlueAccepted?TEXT("identity_and_lifecycle_verified"):TEXT("pending_source_identity_or_complete_lifecycle"));BlueDomain->SetBoolField(TEXT("BlueSupplySourceVerified"),bBlueSupplySourceVerified);BlueDomain->SetBoolField(TEXT("UltimateDirectIdentityVerified"),bBlueUltimateIdentityVerified);BlueDomain->SetStringField(TEXT("actual_ultimate_source_identity"),BlueUltimateIdentity);BlueDomain->SetStringField(TEXT("actual_supply_mesh"),BlueSupplyMesh);BlueDomain->SetBoolField(TEXT("identity_gated_real_combat"),bBlueUltimateCombatVerified);BlueDomain->SetBoolField(TEXT("blue_ultimate_death_and_reset_verified"),IdentityGatedLifecycle);BlueDomain->SetBoolField(TEXT("generic_ultimate_death_observed"),bGenericUltimateDeathObserved);BlueDomain->SetBoolField(TEXT("generic_ultimate_reset_observed"),bGenericUltimateResetObserved);BlueDomain->SetBoolField(TEXT("blue_effect_lifecycle_observed"),bBlueEffectLifecycleObserved);
    TArray<TSharedPtr<FJsonValue>> MissingBlue;if(!bBlueSupplySourceVerified)MissingBlue.Add(MakeShared<FJsonValueString>(TEXT("Directly verified blue supply source asset")));if(!bBlueUltimateIdentityVerified)MissingBlue.Add(MakeShared<FJsonValueString>(TEXT("Direct original100001062 mapping proof; model names or enum alone insufficient")));if(!IdentityGatedLifecycle)MissingBlue.Add(MakeShared<FJsonValueString>(TEXT("Identity-gated blue ultimate death and reset lifecycle")));if(!bBlueEffectLifecycleObserved)MissingBlue.Add(MakeShared<FJsonValueString>(TEXT("Real blue supply effect spawn/pickup/reappear/EndPlay lifecycle")));BlueDomain->SetArrayField(TEXT("missing"),MissingBlue);Report->SetObjectField(TEXT("blueBoxUltimateAcceptance"),BlueDomain);
    Report->SetArrayField(TEXT("supply_effect_evidence"),SupplyEffectEvidence);
    Report->SetArrayField(TEXT("floating_platform_diagnostics"),PlatformDiagnostics);auto PlatformDomain=MakeShared<FJsonObject>();PlatformDomain->SetBoolField(TEXT("requested"),FParse::Param(FCommandLine::Get(),TEXT("HeliosBioFloatingPlatformQA")));PlatformDomain->SetBoolField(TEXT("passed"),false);PlatformDomain->SetStringField(TEXT("status"),TEXT("diagnostics_only_jump_base_carry_return_AI_traversal_pending"));Report->SetObjectField(TEXT("floatingPlatformAcceptance"),PlatformDomain);
    auto ChoiceDomain=MakeShared<FJsonObject>();ChoiceDomain->SetBoolField(TEXT("passed"),false);ChoiceDomain->SetStringField(TEXT("status"),TEXT("Pending real chooser UI, verified legal role actor, combat, death and reset fixture."));ChoiceDomain->SetBoolField(TEXT("legacy_selection_bypass_used"),Mode&&!Mode->Model.Config.bRequireHunterSelection);TArray<TSharedPtr<FJsonValue>> AllowedRoles;if(Mode)for(FName RoleId:Mode->Model.Config.AllowedHunterRoles)AllowedRoles.Add(MakeShared<FJsonValueString>(RoleId.ToString()));ChoiceDomain->SetArrayField(TEXT("configured_allowed_roles"),AllowedRoles);Report->SetObjectField(TEXT("blueBoxHunterChoiceAcceptance"),ChoiceDomain);
    Report->SetBoolField(TEXT("modern_role_optional_fixture_ran"),bModernRoleFixtureActive);Report->SetStringField(TEXT("acceptance_scope"),TEXT("Overall acceptance remains false while required blue-box identity/lifecycle is unresolved. Generic mechanism results are independent."));
    auto ModernDomain=MakeShared<FJsonObject>();ModernDomain->SetBoolField(TEXT("ran"),bModernRoleFixtureActive);ModernDomain->SetBoolField(TEXT("passed"),bModernRoleFixtureActive&&GenericFailures>=0&&Failures==GenericFailures);ModernDomain->SetStringField(TEXT("scope"),TEXT("Optional current-version Wukong/BaiGu fixtures, excluded from blue-box identity acceptance."));Report->SetObjectField(TEXT("modernRoleFixtures"),ModernDomain);
    Report->SetArrayField(TEXT("checks"),Checks);Report->SetStringField(TEXT("map"),GetWorld()->GetOutermost()->GetName());
    Report->SetArrayField(TEXT("presentation_evidence"),PresentationEvidence);
    Report->SetNumberField(TEXT("total_participants"),Mode?Mode->Model.GetParticipants().Num():0);
    Report->SetNumberField(TEXT("configured_participants"),Mode?Mode->Model.Config.ParticipantCount:0);
    Report->SetStringField(TEXT("expected_map"),ExpectedMap);Report->SetStringField(TEXT("participant_count_scope"),TEXT("Mode.Config offline match setting; not a claim that this count is verified for the original floating scene."));
    Report->SetBoolField(TEXT("forcedHumanForIntegrationFixture"),true);
    Report->SetBoolField(TEXT("headless"),FParse::Param(FCommandLine::Get(),TEXT("NullRHI")));
    Report->SetBoolField(TEXT("rendered_acceptance"),false);
    Report->SetBoolField(TEXT("natural_full_round_verified"),false);
    Report->SetBoolField(TEXT("whitebone_player_mother_is_controlled_fixture"),true);
    Report->SetNumberField(TEXT("whitebone_fixture_mother_maxHP_at_clone_spawn"),CloneMotherMaxHPAtSpawn);
    Report->SetStringField(TEXT("modern_role_scope"),TEXT("Current BIO_3RD SunWukong and BaiGuJing source prefabs; separate from old Ultimate100001062. Mother promotion, clone damage and area target placement are explicit post-baseline fixtures."));
    Report->SetStringField(TEXT("supply_control_scope"),TEXT("Real supply actor late SetKind loads Normal then verified original Blue mesh. Synchronous Human/dead eligibility fixtures restore participant state before actor ticks; only Ghost Blue upgrades, Normal only heals/adds ammo."));
    Report->SetStringField(TEXT("project_saved_dir"),FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()));
    Report->SetStringField(TEXT("visual_review"),TEXT("Required: inspect actual PNG screenshots; assertions do not establish visual fidelity."));
    FString Text;auto Writer=TJsonWriterFactory<>::Create(&Text);FJsonSerializer::Serialize(Report,Writer);
    FFileHelper::SaveStringToFile(Text,*(Out/TEXT("report.json")));FPlatformMisc::RequestExit(false);
}
void AHeliosBioHotelQA::Tick(float Dt)
{
    Super::Tick(Dt);if(Stage==99)return;const double Now=FPlatformTime::Seconds();
    if(!Started)
    {
        Started=Now;FParse::Value(FCommandLine::Get(),TEXT("BioHotelQAOut="),Out);
        if(Out.IsEmpty()){UE_LOG(LogTemp,Error,TEXT("BIOHOTEL_QA requires BioHotelQAOut"));FPlatformMisc::RequestExit(false);return;}
        Out=FPaths::ConvertRelativePathToFull(Out);IFileManager::Get().MakeDirectory(*Out,true);Next=Now+2;
        FParse::Value(FCommandLine::Get(),TEXT("BioExpectedMap="),ExpectedMap);
        bObserveOnly=FParse::Param(FCommandLine::Get(),TEXT("BioObserveOnly"));
        bInputEvents=FParse::Param(FCommandLine::Get(),TEXT("BioInputEvents"))&&!bObserveOnly;
        FParse::Value(FCommandLine::Get(),TEXT("BioRoundSeed="),SoakSeed);
        FParse::Value(FCommandLine::Get(),TEXT("BioObserverSeconds="),ObserverSeconds);ObserverSeconds=FMath::Clamp(ObserverSeconds,30.f,1800.f);
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosBioFloatingAIQA"))){TickFloatingAI(Dt,Now);return;}
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosBioHunterChoiceQA"))){TickHunterChoice(Now);return;}
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosBioFloatingPlatformQA"))){TickPlatformPhysics(Now);return;}
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosBioPyramidSoak"))){TickNaturalRound(Dt,Now);return;}
    if(Now-Started>90){Check(TEXT("bounded_runtime"),false);Finish();return;}
    if(Stage==2&&Mode&&Mode->Player)Mode->Player->AddMovementInput(FVector(0,1,0),1.f);
    if(Stage==3&&!bLiveFireEvidenceRecorded&&Now>Next-1.2){RecordPresentation(TEXT("live_player_fire"));bLiveFireEvidenceRecorded=true;}
    if(Now<Next)return;
    switch(Stage)
    {
    case 0:
        Mode=AHeliosBioHotelGameMode::Find(GetWorld());Check(TEXT("bio_game_mode_loaded"),Mode!=nullptr);
        Check(TEXT("expected_map_loaded"),GetWorld()->GetOutermost()->GetName()==ExpectedMap);
        Check(TEXT("userdata_isolated"),FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()).Contains(TEXT("/userdata/"))||FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()).Contains(TEXT("\\userdata\\")));
        if(!Mode||!Mode->Player){Check(TEXT("real_local_player_exists"),false);Finish();return;}
        Check(TEXT("real_local_player_possessed"),Mode->Player->GetController()!=nullptr);
        Check(TEXT("offline_configured_participants"),Mode->Model.Config.ParticipantCount>=2&&Mode->Model.GetParticipants().Num()==Mode->Model.Config.ParticipantCount&&Mode->Bots.Num()==Mode->Model.Config.ParticipantCount-1);
        Capture(TEXT("01_pyramid_preparation"));Mode->Model.Config.bKeepLocalPlayerHuman=true;Mode->Model.Config.bRequireHunterSelection=false;Mode->Model.Config.PreparationSeconds=.2f;Mode->RestartBioRound();Next=Now+.7;Stage=1;break;
    case 1:
        Check(TEXT("natural_preparation_to_infection"),Mode->Model.GetPhase()==EHeliosBioPhase::Active&&ZombieId()!=INDEX_NONE);
        PlayerBefore=Mode->Player->GetActorLocation();BotBefore.Reset();BotTeamsBefore.Reset();for(auto B:Mode->Bots){BotBefore.Add(B->GetActorLocation());const auto* BP=Mode->Model.FindParticipant(B->ParticipantId);BotTeamsBefore.Add(BP?static_cast<int32>(BP->Team):-1);}
        Next=Now+5;Stage=2;break;
    case 2:
    {
        Check(TEXT("real_player_planar_movement"),FVector::Dist2D(PlayerBefore,Mode->Player->GetActorLocation())>50);
        Check(TEXT("player_actually_grounded"),Mode->Player->GetCharacterMovement()->IsMovingOnGround());
        auto Navigation=MakeShared<FJsonObject>();auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
        Navigation->SetBoolField(TEXT("navigation_system_present"),Nav!=nullptr);FNavLocation Projected;
        const ANavigationData* DefaultData=Nav?Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate):nullptr;Navigation->SetStringField(TEXT("player_default_nav_data"),DefaultData?DefaultData->GetName():TEXT(""));Navigation->SetStringField(TEXT("player_nav_scope"),TEXT("Player projection is diagnostic default-nav only; player is not a registered navigation agent."));
        const bool PlayerProjected=Nav&&Nav->ProjectPointToNavigation(Mode->Player->GetActorLocation(),Projected,FVector(150,150,300));
        Navigation->SetBoolField(TEXT("player_projects_to_nav"),PlayerProjected);
        Navigation->SetStringField(TEXT("projected_player_position"),PlayerProjected?Projected.Location.ToString():TEXT(""));
        int32 GroundedBots=0,ProjectedBots=0,ProjectedZombies=0;TArray<TSharedPtr<FJsonValue>> BotNavRows;
        for(auto B:Mode->Bots){const bool Grounded=B->GetCharacterMovement()->IsMovingOnGround();if(Grounded)++GroundedBots;FNavLocation At;const FNavAgentProperties& AgentProps=B->GetNavAgentPropertiesRef();const ANavigationData* AgentData=Nav?Nav->GetNavDataForProps(AgentProps,B->GetActorLocation()):nullptr;const bool OnNav=Nav&&Nav->ProjectPointToNavigation(B->GetActorLocation(),At,FVector(150,150,300),&AgentProps);if(OnNav)++ProjectedBots;const auto* BP=Mode->Model.FindParticipant(B->ParticipantId);if(OnNav&&BP&&BP->Team==EHeliosBioTeam::Zombie)++ProjectedZombies;auto BN=MakeShared<FJsonObject>();BN->SetNumberField(TEXT("id"),B->ParticipantId);BN->SetBoolField(TEXT("grounded"),Grounded);BN->SetBoolField(TEXT("nav_projected"),OnNav);BN->SetStringField(TEXT("nav_data"),AgentData?AgentData->GetName():TEXT(""));BN->SetNumberField(TEXT("agent_radius"),AgentProps.AgentRadius);BN->SetNumberField(TEXT("agent_height"),AgentProps.AgentHeight);BN->SetStringField(TEXT("position"),B->GetActorLocation().ToString());BotNavRows.Add(MakeShared<FJsonValueObject>(BN));}
        Navigation->SetNumberField(TEXT("grounded_bots"),GroundedBots);Navigation->SetNumberField(TEXT("nav_projected_bots"),ProjectedBots);
        Navigation->SetArrayField(TEXT("bots"),BotNavRows);Navigation->SetNumberField(TEXT("nav_projected_zombies"),ProjectedZombies);Check(TEXT("zombie_nav_projection"),ProjectedZombies>0);
        Navigation->SetStringField(TEXT("scope"),TEXT("Nav projections supplement actual measured movement; projection does not establish traversal."));
        FString NavText;auto NavWriter=TJsonWriterFactory<>::Create(&NavText);FJsonSerializer::Serialize(Navigation,NavWriter);FFileHelper::SaveStringToFile(NavText,*(Out/TEXT("navigation_evidence.json")));
        Check(TEXT("AI_actually_grounded"),GroundedBots>0);
        bool Moved=false,Fired=false,HumanMoved=false,ZombieMoved=false,HumanShot=false;TArray<TSharedPtr<FJsonValue>> MotionRows;
        for(int32 I=0;I<Mode->Bots.Num();++I){auto B=Mode->Bots[I];const float Planar=FVector::Dist2D(BotBefore[I],B->GetActorLocation());const bool IsHuman=BotTeamsBefore[I]==static_cast<int32>(EHeliosBioTeam::Human);Moved|=Planar>40;HumanMoved|=IsHuman&&Planar>40;ZombieMoved|=!IsHuman&&Planar>40;HumanShot|=IsHuman&&B->ShotsFired>0;Fired|=B->ShotsFired>0||B->MeleeAttacks>0;auto MR=MakeShared<FJsonObject>();MR->SetNumberField(TEXT("id"),B->ParticipantId);MR->SetNumberField(TEXT("initial_team"),BotTeamsBefore[I]);MR->SetNumberField(TEXT("planar_distance_cm"),Planar);MR->SetNumberField(TEXT("shots"),B->ShotsFired);MR->SetNumberField(TEXT("melee_attacks"),B->MeleeAttacks);MotionRows.Add(MakeShared<FJsonValueObject>(MR));B->bQAFreeze=true;}
        auto Motion=MakeShared<FJsonObject>();Motion->SetArrayField(TEXT("bots"),MotionRows);FString MotionText;auto MotionWriter=TJsonWriterFactory<>::Create(&MotionText);FJsonSerializer::Serialize(Motion,MotionWriter);FFileHelper::SaveStringToFile(MotionText,*(Out/TEXT("natural_combat_evidence.json")));
        Check(TEXT("natural_human_planar_movement"),HumanMoved);Check(TEXT("natural_zombie_planar_movement"),ZombieMoved);Check(TEXT("natural_human_gun_shots"),HumanShot);
        Check(TEXT("natural_AI_movement"),Moved);Check(TEXT("natural_AI_shooting_or_melee"),Fired);Capture(TEXT("02_natural_combat"));
        Mode->Player->ClearMovementInput();Next=Now+.4;Stage=20;break;
    }
    case 20:
    {
        Mode->Player->ClearMovementInput();Mode->Model.StartRound(Mode->Model.Config.ParticipantCount,1337);Mode->Model.Tick(.3f);Zombie=ZombieId();
        Next=Now+.5;Stage=26;break;
    }
    case 26:
    {
        auto Target=Mode->ParticipantActor(Zombie);if(!Target){Check(TEXT("real_zombie_target"),false);Finish();return;}
        FVector Eye;FRotator Aim;Mode->Player->GetActorEyesViewPoint(Eye,Aim);
        Target->SetActorLocation(Mode->Player->GetActorLocation()+FVector(350,0,0));
        Mode->Player->GetController()->SetControlRotation((Target->GetActorLocation()+FVector(0,0,45)-Eye).Rotation());
        HealthBefore=Mode->Model.FindParticipant(Zombie)->Health;ShotsBefore=Mode->Player->ShotsFired;AmmoBefore=Mode->Player->GetAmmo();
        bLiveFireEvidenceRecorded=false;Mode->Player->StartBioFire();Next=Now+1.5;Stage=3;break;
    }
    case 3:
        Mode->Player->StopBioFire();Check(TEXT("real_player_weapon_shots"),Mode->Player->ShotsFired>ShotsBefore);
        Check(TEXT("real_player_ammo_consumption"),Mode->Player->GetAmmo()<AmmoBefore);
        Check(TEXT("real_weapon_damage_reaches_rules"),Mode->Model.FindParticipant(Zombie)->Health<HealthBefore);
        Capture(TEXT("03_player_gunfire"));Next=Now+.4;Stage=23;break;
    case 23:
        Mode->DamageParticipant(Zombie,0,100000,false);
        Check(TEXT("zombie_death_waits_for_respawn"),!Mode->Model.FindParticipant(Zombie)->bAlive&&!Mode->Model.FindParticipant(Zombie)->bPermanentlyDead);
        Next=Now+Mode->Model.Config.RespawnSeconds+.5;Stage=4;break;
    case 4:
        Check(TEXT("zombie_respawns_in_real_world"),Mode->Model.FindParticipant(Zombie)->bAlive&&!Mode->ParticipantActor(Zombie)->IsHidden());
        Check(TEXT("infection_through_mode"),Mode->DamageParticipant(0,Zombie,1)&&Mode->Model.FindParticipant(0)->Team==EHeliosBioTeam::Zombie);
        Next=Now+.4;Stage=25;break;
    case 25:
        Capture(TEXT("04_infected_player"));Next=Now+.4;Stage=21;break;
    case 21:
        Mode->RestartBioRound();Next=Now+.7;Stage=5;break;
    case 5:
        Zombie=ZombieId();for(auto B:Mode->Bots)B->bQAFreeze=true;
        for(int32 Id=1;Id<Mode->Model.GetParticipants().Num();++Id){const auto* P=Mode->Model.FindParticipant(Id);if(P&&P->Team==EHeliosBioTeam::Human)Mode->DamageParticipant(Id,Zombie,1);}
        Check(TEXT("hunter_threshold"),Mode->Model.CanTransform(0));Check(TEXT("ghost_hunter_transform"),Mode->TransformParticipant(0)&&Mode->Model.FindParticipant(0)->Team==EHeliosBioTeam::GhostHunter);
        Next=Now+.5;Stage=6;break;
    case 6:
        Capture(TEXT("05_ghost_hunter"));Next=Now+.4;Stage=22;break;
    case 22:
    {
        auto Supply=Mode->Supplies.Num()?Mode->Supplies[0].Get():nullptr;
        if(Supply){Supply->SetKind(EHeliosBioSupplyKind::Normal);Supply->SetAvailable(true);}
        Check(TEXT("real_supply_actor_available"),Supply&&Supply->IsAvailable());
        const int32 NormalAmmoBefore=Mode->Player->GetReserveAmmo();
        Check(TEXT("normal_supply_never_upgrades_ghost"),Supply&&Mode->PickupSupply(0,Supply)&&Mode->Model.FindParticipant(0)->Team==EHeliosBioTeam::GhostHunter);
        Check(TEXT("normal_supply_adds_ammo"),Mode->Player->GetReserveAmmo()>NormalAmmoBefore);
        if(Supply){Supply->SetKind(EHeliosBioSupplyKind::BlueHunterUpgrade);Supply->SetAvailable(true);}
        BlueFixtureSupply=Supply;if(Supply)Supply->SetActorTickEnabled(false);Next=Now+.3;Stage=31;break;
    }
    case 31:
    {
        auto* Supply=BlueFixtureSupply.Get();RecordSupplyEffects(TEXT("blue_effect_before_pickup"));BlueFixtureEffect=FindSupplyEffect(Supply);
        Check(TEXT("blue_source_effect_alive_and_visible_tracks"),BlueFixtureEffect.IsValid()&&BlueFixtureEffect->VisibleTrackCount()>0);
        auto* ActualEffect=BlueFixtureEffect.Get();UObject* ActualLibrary=nullptr;if(ActualEffect)if(const FObjectPropertyBase* Property=FindFProperty<FObjectPropertyBase>(ActualEffect->GetClass(),TEXT("Library")))ActualLibrary=Property->GetObjectPropertyValue_InContainer(ActualEffect);
        Check(TEXT("blue_source_effect_original_library"),ActualLibrary&&ActualLibrary->GetPathName()==TEXT("/Game/Helios/CFMBioPyramid/BlueSupplyEffects/DA_BlueSupplyEffects.DA_BlueSupplyEffects"));
        bBlueEffectLifecycleObserved=BlueFixtureEffect.IsValid()&&BlueFixtureEffect->VisibleTrackCount()>0&&ActualLibrary&&ActualLibrary->GetPathName()==TEXT("/Game/Helios/CFMBioPyramid/BlueSupplyEffects/DA_BlueSupplyEffects.DA_BlueSupplyEffects");
        UStaticMesh* RealBlueMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Helios/CFMBioPyramid/Supply/SM_BlueBioSupply.SM_BlueBioSupply"));
        bool HasBlueMesh=false;if(Supply){TArray<UStaticMeshComponent*> BlueMeshes;Supply->GetComponents(BlueMeshes);for(auto* BlueMesh:BlueMeshes)HasBlueMesh|=RealBlueMesh&&BlueMesh->GetStaticMesh()==RealBlueMesh;}
        Check(TEXT("blue_supply_uses_verified_original_blue_visual"),Supply&&Supply->GetKind()==EHeliosBioSupplyKind::BlueHunterUpgrade&&Supply->bBlueSupplySourceVerified&&HasBlueMesh);
        auto* EligibilityState=const_cast<FHeliosBioParticipant*>(Mode->Model.FindParticipant(0));
        if(Supply&&EligibilityState)
        {
            // Synchronous eligibility fixtures restore state before any actor tick; no natural-event claim.
            const EHeliosBioTeam SavedTeam=EligibilityState->Team;const int32 SavedBlueCount=EligibilityState->BlueSuppliesCollected;
            EligibilityState->Team=EHeliosBioTeam::Human;
            Check(TEXT("human_cannot_consume_blue_supply"),!Mode->PickupSupply(0,Supply)&&Supply->IsAvailable()&&EligibilityState->BlueSuppliesCollected==SavedBlueCount);
            EligibilityState->Team=SavedTeam;const bool SavedAlive=EligibilityState->bAlive;EligibilityState->bAlive=false;
            Check(TEXT("dead_ghost_cannot_consume_blue_supply"),!Mode->PickupSupply(0,Supply)&&Supply->IsAvailable()&&EligibilityState->BlueSuppliesCollected==SavedBlueCount);
            EligibilityState->bAlive=SavedAlive;
        }
        const int32 BlueAmmoBefore=Mode->Player->GetReserveAmmo();
        Check(TEXT("supply_to_ultimate_hunter"),Supply&&Mode->PickupSupply(0,Supply)&&Mode->Model.FindParticipant(0)->Team==EHeliosBioTeam::UltimateHunter);
        Check(TEXT("blue_supply_does_not_add_normal_ammo"),Mode->Player->GetReserveAmmo()==BlueAmmoBefore);
        if(Supply)
        {
            const FBoolProperty* BlueProof=FindFProperty<FBoolProperty>(Supply->GetClass(),TEXT("bBlueSupplySourceVerified"));if(!BlueProof)BlueProof=FindFProperty<FBoolProperty>(Supply->GetClass(),TEXT("BlueSupplySourceVerified"));bBlueSupplySourceVerified=BlueProof&&BlueProof->GetPropertyValue_InContainer(Supply);
            TArray<UStaticMeshComponent*> SupplyMeshes;Supply->GetComponents(SupplyMeshes);for(auto* SupplyMesh:SupplyMeshes)if(SupplyMesh->GetStaticMesh()){BlueSupplyMesh=SupplyMesh->GetStaticMesh()->GetPathName();break;}
        }
        auto* BlueDefinition=LoadObject<UHeliosBioRoleDefinition>(nullptr,TEXT("/Game/Helios/CFMBioHotel/Characters/DA_BioUltimateHunter.DA_BioUltimateHunter"));auto* ActualUltimate=HeliosBioArt::Load(*Mode->Model.FindParticipant(0));BlueUltimateIdentity=ActualUltimate?ActualUltimate->SourceIdentity:TEXT("");
        bBlueUltimateIdentityVerified=BlueDefinition&&ActualUltimate==BlueDefinition&&BlueDefinition->bIdentityVerified&&BlueUltimateIdentity.Contains(TEXT("100001062"))&&!BlueUltimateIdentity.Contains(TEXT("SunWuKong"),ESearchCase::IgnoreCase)&&!BlueUltimateIdentity.Contains(TEXT("BaiGuJing"),ESearchCase::IgnoreCase);
        Next=Now+.3;Stage=32;break;
    }
    case 32:
    {
        const bool PickupDestroyed=(!BlueFixtureEffect.IsValid()||BlueFixtureEffect->IsActorBeingDestroyed())&&FindSupplyEffect(BlueFixtureSupply)==nullptr;Check(TEXT("blue_source_effect_destroyed_on_pickup"),PickupDestroyed);bBlueEffectLifecycleObserved&=PickupDestroyed;RecordSupplyEffects(TEXT("blue_effect_after_pickup"));
        if(BlueFixtureSupply)BlueFixtureSupply->SetAvailable(true);Next=Now+.3;Stage=33;break;
    }
    case 33:
    {
        auto* Reborn=FindSupplyEffect(BlueFixtureSupply);const bool Reappeared=Reborn&&Reborn!=BlueFixtureEffect.Get()&&Reborn->VisibleTrackCount()>0;Check(TEXT("blue_source_effect_reappears_on_available"),Reappeared);bBlueEffectLifecycleObserved&=Reappeared;RecordSupplyEffects(TEXT("blue_effect_reappeared"));BlueFixtureEffect=Reborn;
        if(BlueFixtureSupply)BlueFixtureSupply->Destroy();Next=Now+.3;Stage=34;break;
    }
    case 34:
    {
        const bool EndDestroyed=!BlueFixtureEffect.IsValid()||BlueFixtureEffect->IsActorBeingDestroyed();Check(TEXT("blue_source_effect_endplay_cleanup"),EndDestroyed);bBlueEffectLifecycleObserved&=EndDestroyed;RecordSupplyEffects(TEXT("blue_effect_after_supply_endplay"));
        Mode->Player->UseBioSkill();Check(TEXT("hunter_skill_activates"),Mode->Model.FindParticipant(0)->SkillRemaining>0);Next=Now+.5;Stage=7;break;
    }
    case 7:
        Capture(TEXT("06_ultimate_hunter"));Next=Now+.4;Stage=29;break;
    case 29:
    {
        if(Mode->Player->GetBioAttackCooldown()>0){Next=Now+Mode->Player->GetBioAttackCooldown()+.05;break;}
        Zombie=ZombieId();auto* Victim=Mode->ParticipantActor(Zombie);if(!Victim){Check(TEXT("real_heavy_target_exists"),false);Finish();return;}
        const FVector Direction=Mode->Player->GetControlRotation().Vector().GetSafeNormal2D();Victim->SetActorLocation(Mode->Player->GetActorLocation()+Direction*200.f);
        FVector View;FRotator Rotation;Mode->Player->GetActorEyesViewPoint(View,Rotation);Mode->Player->GetController()->SetControlRotation((Victim->GetActorLocation()+FVector(0,0,25)-View).Rotation());
        HealthBefore=Mode->Model.FindParticipant(Zombie)->Health;MeleeBefore=Mode->Player->MeleeAttacks;Mode->Player->StartHeavy();
        auto* ArtDefinition=HeliosBioArt::Load(*Mode->Model.FindParticipant(0));UAnimSequence* ExpectedHeavy=ArtDefinition?ArtDefinition->FPHeavy.LoadSynchronous():nullptr;
        bool ActualHeavy=false;TArray<USkeletalMeshComponent*> PlayerMeshes;Mode->Player->GetComponents(PlayerMeshes);
        for(auto* PlayerMesh:PlayerMeshes)if(PlayerMesh->IsVisible()&&!PlayerMesh->bHiddenInGame&&PlayerMesh->GetSingleNodeInstance()&&ExpectedHeavy&&PlayerMesh->GetSingleNodeInstance()->GetCurrentAsset()==ExpectedHeavy)ActualHeavy=true;
        Check(TEXT("real_source_heavy_animation"),ActualHeavy);RecordPresentation(TEXT("hunter_heavy_begin"));Next=Now+.3;Stage=30;break;
    }
    case 30:
        Check(TEXT("real_hunter_heavy_attack_committed"),Mode->Player->MeleeAttacks>MeleeBefore);Check(TEXT("real_hunter_heavy_damage"),Mode->Model.FindParticipant(Zombie)->Health<HealthBefore);
        bBlueUltimateCombatVerified=bBlueSupplySourceVerified&&bBlueUltimateIdentityVerified&&Mode->Player->MeleeAttacks>MeleeBefore&&Mode->Model.FindParticipant(Zombie)->Health<HealthBefore;
        Capture(TEXT("09_real_hunter_heavy"));Next=Now+.4;Stage=24;break;
    case 24:
        for(int32 Id=1;Id<Mode->Model.GetParticipants().Num();++Id){const auto* P=Mode->Model.FindParticipant(Id);if(P&&P->Team==EHeliosBioTeam::Zombie&&P->bAlive)Mode->DamageParticipant(Id,0,100000,true);}
        Check(TEXT("hunter_melee_permanent_death"),Mode->Model.FindParticipant(Zombie)->bPermanentlyDead);
        Check(TEXT("humans_win_by_elimination"),Mode->Model.GetWinner()==EHeliosBioWinner::Humans);
        SerialBefore=Mode->RoundSerial;Mode->RestartBioRound();Next=Now+.7;Stage=8;break;
    case 8:
        Check(TEXT("restart_resets_round"),Mode->RoundSerial>SerialBefore&&Mode->Model.GetWinner()==EHeliosBioWinner::None&&Mode->Model.FindParticipant(0)->Team==EHeliosBioTeam::Human);
        Zombie=ZombieId();for(auto B:Mode->Bots)B->bQAFreeze=true;
        for(int32 Id=0;Id<Mode->Model.GetParticipants().Num();++Id){const auto* P=Mode->Model.FindParticipant(Id);if(P&&P->Team==EHeliosBioTeam::Human)Mode->DamageParticipant(Id,Zombie,1);}
        Check(TEXT("zombies_win_all_infected"),Mode->Model.GetWinner()==EHeliosBioWinner::Zombies);Next=Now+.4;Stage=27;break;
    case 27:
        Capture(TEXT("07_zombies_win"));Next=Now+.7;Stage=9;break;
    case 9:
        Mode->RestartBioRound();Next=Now+.7;Stage=10;break;
    case 10:
        for(auto B:Mode->Bots)B->bQAFreeze=true;Mode->Model.Tick(Mode->Model.GetPhaseTimeRemaining()+.1f);
        Check(TEXT("humans_win_timeout"),Mode->Model.GetWinner()==EHeliosBioWinner::Humans);Next=Now+.4;Stage=28;break;
    case 28:
        Capture(TEXT("08_humans_win"));Next=Now+1;Stage=60;break;
    case 60:
        Mode->RestartBioRound();Next=Now+.7;Stage=61;break;
    case 61:
        for(auto B:Mode->Bots)B->bQAFreeze=true;Zombie=ZombieId();
        for(int32 Id=1;Id<Mode->Model.GetParticipants().Num();++Id){const auto* BP=Mode->Model.FindParticipant(Id);if(BP&&BP->Team==EHeliosBioTeam::Human)Mode->DamageParticipant(Id,Zombie,1);}
        Check(TEXT("generic_lifecycle_ghost_transform"),Mode->TransformParticipant(0));Next=Now+.5;Stage=62;break;
    case 62:
    {
        auto* Supply=Mode->Supplies.Num()?Mode->Supplies[0].Get():nullptr;if(Supply){Supply->SetActorTickEnabled(false);Supply->SetKind(EHeliosBioSupplyKind::BlueHunterUpgrade);Supply->SetAvailable(true);}
        Check(TEXT("generic_lifecycle_blue_upgrade"),Supply&&Mode->PickupSupply(0,Supply)&&Mode->Model.FindParticipant(0)->Team==EHeliosBioTeam::UltimateHunter);Next=Now+1.5;Stage=63;break;
    }
    case 63:
        Zombie=ZombieId();Check(TEXT("generic_ultimate_actual_lethal_damage"),Mode->DamageParticipant(0,Zombie,Mode->Model.FindParticipant(0)->MaxHealth+1.f,false));Next=Now+.4;Stage=64;break;
    case 64:
    {
        const auto* Dead=Mode->Model.FindParticipant(0);bGenericUltimateDeathObserved=Dead&&!Dead->bAlive&&Dead->Health<=0&&Dead->Team==EHeliosBioTeam::UltimateHunter&&Mode->Player->ActorHasTag(TEXT("Dead"))&&!Mode->Player->GetCharacterMovement()->IsMovingOnGround();Check(TEXT("generic_ultimate_player_tick_death_observed"),bGenericUltimateDeathObserved);
        SerialBefore=Mode->RoundSerial;Mode->Player->RequestBioRestart();Next=Now+.7;Stage=65;break;
    }
    case 65:
    {
        const auto* ResetState=Mode->Model.FindParticipant(0);bGenericUltimateResetObserved=Mode->RoundSerial>SerialBefore&&ResetState&&ResetState->bAlive&&ResetState->Team==EHeliosBioTeam::Human&&ResetState->Health>0&&!Mode->Player->ActorHasTag(TEXT("Dead"));Check(TEXT("generic_ultimate_normal_restart_to_alive_human"),bGenericUltimateResetObserved);GenericFailures=Failures;Next=Now+.3;Stage=FParse::Param(FCommandLine::Get(),TEXT("HeliosBioPyramidModernRoleQA"))?40:11;break;
    }
    case 40:
        bModernRoleFixtureActive=true;Mode->Model.Config.bUseCurrentVersionRoles=true;Mode->RestartBioRound();Next=Now+.7;Stage=41;break;
    case 41:
    {
        for(auto B:Mode->Bots)B->bQAFreeze=true;Zombie=ZombieId();
        Check(TEXT("whitebone_fixture_player_infection"),Mode->DamageParticipant(0,Zombie,1));
        // Explicit controlled fixture: the normal deterministic mother selection excludes local player.
        // Only this independent post-baseline skill fixture promotes it to mother for the real F handler.
        auto* MotherState=const_cast<FHeliosBioParticipant*>(Mode->Model.FindParticipant(0));
        if(!MotherState){Check(TEXT("whitebone_fixture_state"),false);Finish();return;}
        MotherState->bMother=true;MotherState->MaxHealth=MotherState->Health=Mode->Model.Config.MotherHealth;
        UE_LOG(LogTemp,Display,TEXT("BIOHOTEL_QA_FIXTURE local_player_mother=true health=%f; not a natural selection assertion"),MotherState->MaxHealth);
        Next=Now+1.5;Stage=42;break;
    }
    case 42:
    {
        auto* ArtDefinition=HeliosBioArt::Load(*Mode->Model.FindParticipant(0));
        Check(TEXT("modern_whitebone_source_identity"),ArtDefinition&&ArtDefinition->SourceIdentity.Contains(TEXT("BaiGuJing"),ESearchCase::IgnoreCase));
        const int32 CloneCountBefore=Mode->Clones.Num();CloneMotherMaxHPAtSpawn=Mode->Model.FindParticipant(0)->MaxHealth;Mode->Player->UseBioSkill();
        Check(TEXT("real_whitebone_F_handler_spawns_two_clones"),Mode->Clones.Num()==CloneCountBefore+2);
        if(Mode->Clones.Num()<2){Next=Now+.3;Stage=44;break;}
        CloneFirst=Mode->Clones[Mode->Clones.Num()-2];CloneSecond=Mode->Clones.Last();Next=Now+.3;Stage=43;break;
    }
    case 43:
    {
        const auto* Mother=Mode->Model.FindParticipant(0);auto* CloneArt=LoadObject<UHeliosBioRoleDefinition>(nullptr,TEXT("/Game/Helios/CFMBioPyramid/Characters/DA_BioKuiLei.DA_BioKuiLei"));
        USkeletalMesh* ExpectedClone=CloneArt?CloneArt->ThirdPersonMesh.LoadSynchronous():nullptr;
        const bool ClonesValid=IsValid(CloneFirst)&&IsValid(CloneSecond)&&CloneFirst->IsActiveClone()&&CloneSecond->IsActiveClone();
        Check(TEXT("whitebone_two_actual_clone_actors"),ClonesValid);
        Check(TEXT("whitebone_clone_maxHP_30_percent"),ClonesValid&&FMath::IsNearlyEqual(CloneFirst->GetCloneMaxHealth(),CloneMotherMaxHPAtSpawn*.3f)&&FMath::IsNearlyEqual(CloneSecond->GetCloneMaxHealth(),CloneMotherMaxHPAtSpawn*.3f));
        Check(TEXT("whitebone_original_clone_mesh"),ClonesValid&&ExpectedClone&&CloneFirst->GetMesh()->GetSkeletalMeshAsset()==ExpectedClone&&CloneSecond->GetMesh()->GetSkeletalMeshAsset()==ExpectedClone);
        if(ClonesValid)
        {
            const float MotherHP=Mother->Health,SecondHP=CloneSecond->CurrentHP,FirstHP=CloneFirst->CurrentHP;
            int32 HumanId=INDEX_NONE;for(const auto& HP:Mode->Model.GetParticipants())if(HP.bAlive&&HP.Team==EHeliosBioTeam::Human){HumanId=HP.Id;break;}
            const float Applied=CloneFirst->ApplyBioDamage(HumanId,10.f);
            Check(TEXT("whitebone_clone_independent_health"),Applied>0&&CloneFirst->CurrentHP<FirstHP&&FMath::IsNearlyEqual(CloneSecond->CurrentHP,SecondHP)&&FMath::IsNearlyEqual(Mode->Model.FindParticipant(0)->Health,MotherHP));
        }
        Capture(TEXT("10_whitebone_real_clones"));Next=Now+.5;Stage=44;break;
    }
    case 44:
        Mode->RestartBioRound();Next=Now+.7;Stage=45;break;
    case 45:
        for(auto B:Mode->Bots)B->bQAFreeze=true;Zombie=ZombieId();
        for(int32 Id=1;Id<Mode->Model.GetParticipants().Num();++Id){const auto* BP=Mode->Model.FindParticipant(Id);if(BP&&BP->Team==EHeliosBioTeam::Human)Mode->DamageParticipant(Id,Zombie,1);}
        Check(TEXT("wukong_fixture_ghost_transform"),Mode->TransformParticipant(0));Next=Now+.5;Stage=46;break;
    case 46:
    {
        auto Supply=Mode->Supplies.Num()?Mode->Supplies[0].Get():nullptr;if(Supply){Supply->SetKind(EHeliosBioSupplyKind::BlueHunterUpgrade);Supply->SetAvailable(true);}
        Check(TEXT("wukong_fixture_supply_transform"),Supply&&Mode->PickupSupply(0,Supply));Next=Now+1.5;Stage=47;break;
    }
    case 47:
    {
        auto* ArtDefinition=HeliosBioArt::Load(*Mode->Model.FindParticipant(0));Check(TEXT("modern_wukong_not_old_ultimate_identity"),ArtDefinition&&ArtDefinition->SourceIdentity.Contains(TEXT("SunWuKong"),ESearchCase::IgnoreCase)&&!ArtDefinition->SourceIdentity.Contains(TEXT("100001062")));
        Zombie=ZombieId();auto* AreaVictim=Cast<ACharacter>(Mode->ParticipantActor(Zombie));if(!AreaVictim){Check(TEXT("wukong_actual_aoe_victim"),false);Finish();return;}
        const FVector Facing=Mode->Player->GetControlRotation().Vector().GetSafeNormal2D();AreaVictim->SetActorLocation(Mode->Player->GetActorLocation()+Facing*250.f);AreaVictim->GetCharacterMovement()->StopMovementImmediately();
        // Keep real CharacterMovement physics, but prevent the frozen NPC actor's repeated
        // AI StopMovement from canceling the skill's real LaunchCharacter impulse.
        AreaVictim->SetActorTickEnabled(false);
        HealthBefore=Mode->Model.FindParticipant(Zombie)->Health;AreaVictimBefore=AreaVictim->GetActorLocation();AreaHitsBefore=Mode->TotalAreaSkillHits;
        Mode->Player->UseBioSkill();RecordPresentation(TEXT("modern_wukong_skill_begin"));
        const float SourceDelay=ArtDefinition&&ArtDefinition->FPSkill.LoadSynchronous()?ArtDefinition->FPSkill.LoadSynchronous()->GetPlayLength():.2f;
        Next=Now+SourceDelay+.2f;Stage=48;break;
    }
    case 48:
    {
        auto* AreaVictim=Cast<ACharacter>(Mode->ParticipantActor(Zombie));
        Check(TEXT("wukong_real_skill_area_damage"),Mode->TotalAreaSkillHits>AreaHitsBefore&&Mode->Model.FindParticipant(Zombie)->Health<HealthBefore);
        Check(TEXT("wukong_real_skill_knockback"),AreaVictim&&(FVector::Dist2D(AreaVictimBefore,AreaVictim->GetActorLocation())>10.f||AreaVictim->GetVelocity().Size2D()>20.f));
        if(AreaVictim)AreaVictim->SetActorTickEnabled(true);
        Capture(TEXT("11_wukong_real_area_skill"));Next=Now+.5;Stage=11;break;
    }
    default:Finish();Stage=99;break;
    }
}

void AHeliosBioHotelQA::SendNaturalKey(FKey Key,bool Down)
{
    if(!Mode||!Mode->Player)return;
    auto* PC=Cast<APlayerController>(Mode->Player->GetController());if(!PC)return;
    PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),Key,Down?IE_Pressed:IE_Released,FPlatformTime::Cycles64()));
    if(Down)HeldNaturalKeys.Add(Key);else HeldNaturalKeys.Remove(Key);++InputEventsSent;
    auto Event=MakeShared<FJsonObject>();Event->SetStringField(TEXT("key"),Key.ToString());Event->SetBoolField(TEXT("down"),Down);Event->SetNumberField(TEXT("game_seconds"),GetWorld()->GetTimeSeconds());NaturalInputEvidence.Add(MakeShared<FJsonValueObject>(Event));
}
void AHeliosBioHotelQA::ReleaseNaturalKeys()
{
    const auto Keys=HeldNaturalKeys.Array();for(FKey Key:Keys)SendNaturalKey(Key,false);
    bInputFireHeld=false;bInputMoveHeld=false;
}
void AHeliosBioHotelQA::SampleNPCDiagnostics(AHeliosBioNPC* NPC,const FHeliosBioParticipant& State,const TSharedPtr<FJsonObject>& Row,double Now)
{
    auto& Evidence=NaturalActorEvidence.FindOrAdd(State.Id);
    const float Dt=Evidence.LastSample>0?FMath::Min(1.f,float(Now-Evidence.LastSample)):0.f;
    const FVector Position=NPC->GetActorLocation();const float Delta=Evidence.LastSample>0?FVector::Dist2D(Position,Evidence.LastPosition):0.f;
    const auto Traversal=HeliosBioPlatformTraversal::GetDiagnostics(NPC);auto* Movement=NPC->GetCharacterMovement();auto* AI=Cast<AAIController>(NPC->GetController());
    Row->SetStringField(TEXT("movement_intent"),NPC->GetMovementIntent().ToString());Row->SetBoolField(TEXT("movement_intent_active"),NPC->IsMoveIntentActive());
    Row->SetStringField(TEXT("movement_goal"),NPC->GetMovementGoal().ToString());Row->SetNumberField(TEXT("last_move_result"),NPC->GetLastMoveResult());Row->SetNumberField(TEXT("npc_blocked_seconds"),NPC->GetBlockedSeconds());Row->SetStringField(TEXT("movement_diagnostic"),NPC->GetMovementDiagnostic());
    Row->SetStringField(TEXT("target"),GetNameSafe(NPC->GetDesiredCombatTarget()));Row->SetNumberField(TEXT("ai_move_status"),AI?int32(AI->GetMoveStatus()):INDEX_NONE);
    Row->SetStringField(TEXT("floating_action"),NPC->GetFloatingActionDiagnostic());
    Row->SetNumberField(TEXT("platform_ground_seconds"),Traversal.GroundSeconds);Row->SetNumberField(TEXT("platform_route_seconds"),Traversal.RouteSeconds);Row->SetNumberField(TEXT("platform_wait_remaining"),Traversal.WaitRemaining);
    auto* PathFollowing=AI?AI->GetPathFollowingComponent():nullptr;
    Row->SetBoolField(TEXT("path_valid"),PathFollowing&&PathFollowing->HasValidPath());Row->SetStringField(TEXT("velocity"),NPC->GetVelocity().ToString());
    Row->SetStringField(TEXT("movement_base"),GetNameSafe(NPC->GetMovementBase()));Row->SetStringField(TEXT("platform_traversal"),HeliosBioPlatformTraversal::DebugState(NPC));Row->SetBoolField(TEXT("platform_waiting"),Traversal.bWaitingAtDock);Row->SetNumberField(TEXT("platform_boardings"),Traversal.Boardings);Row->SetNumberField(TEXT("platform_departures"),Traversal.Departures);
    const auto& Loadout=NPC->GetHumanLoadout();Row->SetStringField(TEXT("human_character"),Loadout.CharacterId.ToString());Row->SetStringField(TEXT("human_variant"),Loadout.CharacterVariantId.ToString());Row->SetStringField(TEXT("human_weapon"),Loadout.PrimaryWeaponId.ToString());
    USkeletalMeshComponent* Body=NPC->GetMesh();
    if(auto* Presentation=NPC->FindComponentByClass<UHeliosCharacterPresentationComponent>())if(Presentation->GetArmsMesh()&&Presentation->GetArmsMesh()->IsVisible()&&!Presentation->GetArmsMesh()->bHiddenInGame)Body=Presentation->GetArmsMesh();
    auto* Node=Body?Body->GetSingleNodeInstance():nullptr;const FString Clip=Node?GetNameSafe(Node->GetCurrentAsset()):FString();const float ClipTime=Node?Node->GetCurrentTime():0.f;
    Row->SetStringField(TEXT("body_mesh"),Body?GetNameSafe(Body->GetSkeletalMeshAsset()):TEXT(""));Row->SetStringField(TEXT("body_clip"),Clip);Row->SetNumberField(TEXT("body_clip_seconds"),ClipTime);
    TArray<FTransform> Pose;float FootZ=MAX_flt;int32 FootBones=0;
    if(Body)for(int32 I=0;I<Body->GetNumBones();++I)
    {
        const FString Name=Body->GetBoneName(I).ToString();const FTransform Bone=Body->GetBoneTransform(I);
        const bool Foot=Name.Contains(TEXT("foot"),ESearchCase::IgnoreCase)||Name.Contains(TEXT("toe"),ESearchCase::IgnoreCase)||Name.Contains(TEXT("ankle"),ESearchCase::IgnoreCase);
        if(Foot){FootZ=FMath::Min(FootZ,float(Bone.GetLocation().Z));++FootBones;}
        if((Foot||Name.Contains(TEXT("hand"),ESearchCase::IgnoreCase)||Name.Contains(TEXT("spine"),ESearchCase::IgnoreCase))&&Pose.Num()<12)Pose.Add(Bone.GetRelativeTransform(Body->GetComponentTransform()));
    }
    bool PoseAdvanced=false;
    if(Evidence.Clip==Clip&&Pose.Num()==Evidence.Pose.Num())for(int32 I=0;I<Pose.Num();++I)
        PoseAdvanced|=FVector::DistSquared(Pose[I].GetLocation(),Evidence.Pose[I].GetLocation())>.01f||Pose[I].GetRotation().AngularDistance(Evidence.Pose[I].GetRotation())>.001f;
    const bool ClockAdvanced=Evidence.Clip==Clip&&FMath::Abs(ClipTime-Evidence.ClipTime)>.0001f;
    if(ClockAdvanced)++Evidence.ClockAdvances;if(PoseAdvanced)++Evidence.PoseAdvances;
    Row->SetBoolField(TEXT("clip_clock_advanced"),ClockAdvanced);Row->SetBoolField(TEXT("bone_pose_advanced"),PoseAdvanced);Row->SetNumberField(TEXT("sample_motion_cm"),Delta);
    const bool Eligible=State.bAlive&&State.FrozenSeconds<=0&&Mode->Model.GetPhase()==EHeliosBioPhase::Active&&!GetWorld()->IsPaused();
    const bool Moving=Movement->IsMovingOnGround()&&NPC->GetVelocity().Size2D()>20.f;
    if(Eligible&&Moving){++Evidence.MotionSamples;Evidence.AnimationStall=Evidence.Clip==Clip&&!PoseAdvanced?Evidence.AnimationStall+Dt:0.f;}else Evidence.AnimationStall=0.f;
    const bool MovingToGoal=(Traversal.bOwnsMovement&&!Traversal.bWaitingAtDock&&(Traversal.Step==0||Traversal.Step==5))||(!Traversal.bOwnsMovement&&NPC->IsMoveIntentActive());
    const FVector Goal=Traversal.bOwnsMovement?Traversal.MovementGoal:NPC->GetMovementGoal();
    if(Eligible&&Movement->IsMovingOnGround()&&MovingToGoal&&FVector::Dist2D(Position,Goal)>100.f&&Delta<2.f)Evidence.MovementStall+=Dt;else Evidence.MovementStall=0.f;
    Evidence.LongestAnimationStall=FMath::Max(Evidence.LongestAnimationStall,Evidence.AnimationStall);Evidence.LongestMovementStall=FMath::Max(Evidence.LongestMovementStall,Evidence.MovementStall);
    Row->SetNumberField(TEXT("animation_stalled_seconds"),Evidence.AnimationStall);Row->SetNumberField(TEXT("movement_stalled_seconds"),Evidence.MovementStall);Row->SetNumberField(TEXT("foot_bones"),FootBones);
    const float CapsuleFeet=Position.Z-NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();Row->SetNumberField(TEXT("capsule_feet_z"),CapsuleFeet);
    FHitResult Floor;FCollisionQueryParams Query(SCENE_QUERY_STAT(BioNaturalFootGround),false,NPC);for(auto Bot:Mode->Bots)Query.AddIgnoredActor(Bot);Query.AddIgnoredActor(Mode->Player);Query.AddIgnoredActor(NPC->GetEquippedWeapon());
    const bool FloorHit=GetWorld()->LineTraceSingleByChannel(Floor,FVector(Position.X,Position.Y,CapsuleFeet+20.f),FVector(Position.X,Position.Y,CapsuleFeet-100.f),ECC_Visibility,Query);
    Row->SetBoolField(TEXT("feet_ground_trace"),FloorHit);
    Row->SetNumberField(TEXT("native_floor_distance_cm"),Movement->CurrentFloor.GetDistanceToFloor());
    Row->SetStringField(TEXT("native_floor_impact"),Movement->CurrentFloor.HitResult.ImpactPoint.ToString());
    Row->SetStringField(TEXT("native_floor_component"),GetNameSafe(Movement->CurrentFloor.HitResult.GetComponent()));
    FHitResult PhysicalFloor;
    const bool PhysicalFloorHit=GetWorld()->LineTraceSingleByChannel(PhysicalFloor,FVector(Position.X,Position.Y,CapsuleFeet+20.f),FVector(Position.X,Position.Y,CapsuleFeet-100.f),ECC_Pawn,Query);
    Row->SetNumberField(TEXT("physical_center_floor_gap_cm"),PhysicalFloorHit?CapsuleFeet-PhysicalFloor.ImpactPoint.Z:-1000.f);
    if(FootBones&&FloorHit)
    {
        const float Gap=FootZ-Floor.ImpactPoint.Z;Row->SetNumberField(TEXT("visual_foot_z"),FootZ);Row->SetNumberField(TEXT("visual_foot_ground_gap_cm"),Gap);Row->SetNumberField(TEXT("capsule_ground_gap_cm"),CapsuleFeet-Floor.ImpactPoint.Z);
        if(Eligible&&Movement->IsMovingOnGround())
        {++Evidence.GroundSamples;Evidence.MaxAbsoluteFootGap=FMath::Max(Evidence.MaxAbsoluteFootGap,FMath::Abs(Gap));Evidence.FootGapAge=(Gap>35.f||Gap<-20.f)?Evidence.FootGapAge+Dt:0.f;}
        else Evidence.FootGapAge=0.f;
    }
    else Evidence.FootGapAge=0.f;
    if(Evidence.FootGapAge>3.f&&!Evidence.bFootFailed){Evidence.bFootFailed=true;Check(*FString::Printf(TEXT("natural_id%d_rendered_feet_near_ground"),State.Id),false);}
    if(Evidence.AnimationStall>3.f&&!Evidence.bAnimationFailed){Evidence.bAnimationFailed=true;Check(*FString::Printf(TEXT("natural_id%d_moving_bone_pose_advances"),State.Id),false);}
    if(Evidence.MovementStall>8.f&&!Evidence.bMovementFailed){Evidence.bMovementFailed=true;Check(*FString::Printf(TEXT("natural_id%d_complete_move_intent_progress"),State.Id),false);}
    Evidence.LastSample=Now;Evidence.LastPosition=Position;Evidence.Clip=Clip;Evidence.ClipTime=ClipTime;Evidence.Pose=MoveTemp(Pose);
}
void AHeliosBioHotelQA::SampleNaturalRound(double Now)
{
    auto Sample=MakeShared<FJsonObject>();Sample->SetNumberField(TEXT("wall_seconds"),Now-Started);Sample->SetNumberField(TEXT("game_seconds"),GetWorld()->GetTimeSeconds());Sample->SetNumberField(TEXT("phase"),static_cast<int32>(Mode->Model.GetPhase()));Sample->SetNumberField(TEXT("remaining_seconds"),Mode->Model.GetPhaseTimeRemaining());Sample->SetNumberField(TEXT("infections"),Mode->TotalInfections);Sample->SetNumberField(TEXT("supply_pickups"),Mode->TotalSupplies);
    if(auto* PC=Cast<APlayerController>(Mode->Player->GetController()))
    {
        TArray<TSharedPtr<FJsonValue>> Keys;for(const FKey& Key:{EKeys::W,EKeys::A,EKeys::S,EKeys::D,EKeys::SpaceBar,EKeys::LeftMouseButton,EKeys::Escape,EKeys::E,EKeys::F5})if(PC->IsInputKeyDown(Key))Keys.Add(MakeShared<FJsonValueString>(Key.ToString()));
        Sample->SetArrayField(TEXT("local_keys_down"),Keys);Sample->SetBoolField(TEXT("mouse_cursor_visible"),PC->bShowMouseCursor);Sample->SetBoolField(TEXT("world_paused"),GetWorld()->IsPaused());Sample->SetBoolField(TEXT("hunter_chooser_open"),Mode->bHunterChooserOpen);
    }
    TMap<int32,int32> SupplyKinds;for(auto Supply:Mode->Supplies)if(IsValid(Supply)&&Supply->IsAvailable())++SupplyKinds.FindOrAdd(int32(Supply->GetKind()));
    auto SupplyCounts=MakeShared<FJsonObject>();for(const auto& Pair:SupplyKinds)SupplyCounts->SetNumberField(FString::FromInt(Pair.Key),Pair.Value);Sample->SetObjectField(TEXT("available_supply_counts_by_kind"),SupplyCounts);
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());TArray<TSharedPtr<FJsonValue>> Rows;
    for(const auto& State:Mode->Model.GetParticipants())
    {
        auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("id"),State.Id);Row->SetNumberField(TEXT("team"),static_cast<int32>(State.Team));Row->SetBoolField(TEXT("alive"),State.bAlive);Row->SetNumberField(TEXT("health"),State.Health);
        Row->SetBoolField(TEXT("hunter_choice_pending"),State.bHunterChoicePending);Row->SetStringField(TEXT("hunter_role_key"),State.HunterRoleId.ToString());
        AActor* Participant=Mode->ParticipantActor(State.Id);Row->SetBoolField(TEXT("actual_actor_present"),IsValid(Participant));
        if(IsValid(Participant))
        {
            Row->SetStringField(TEXT("actor"),Participant->GetPathName());Row->SetStringField(TEXT("position"),Participant->GetActorLocation().ToString());
            const FVector* Previous=SoakPreviousPositions.Find(State.Id);const float Delta=Previous?FVector::Dist2D(*Previous,Participant->GetActorLocation()):0.f;Row->SetNumberField(TEXT("planar_sample_distance_cm"),Delta);SoakPreviousPositions.Add(State.Id,Participant->GetActorLocation());
            auto* Character=Cast<ACharacter>(Participant);const bool Grounded=Character&&Character->GetCharacterMovement()->IsMovingOnGround();Row->SetBoolField(TEXT("grounded"),Grounded);if(Grounded&&State.bAlive)++SoakGrounded;
            int32 Shots=0,Melee=0;if(State.Id==0){Shots=Mode->Player->ShotsFired;Melee=Mode->Player->MeleeAttacks;}else if(auto* NPC=Cast<AHeliosBioNPC>(Participant)){Shots=NPC->ShotsFired;Melee=NPC->MeleeAttacks;}
            const int32* OldShots=SoakPreviousShots.Find(State.Id);const int32* OldMelee=SoakPreviousMelee.Find(State.Id);const int32 ShotDelta=OldShots?FMath::Max(0,Shots-*OldShots):0,MeleeDelta=OldMelee?FMath::Max(0,Melee-*OldMelee):0;
            const auto* PreviousTeam=SoakPreviousTeams.Find(State.Id);const auto EventTeam=PreviousTeam?*PreviousTeam:State.Team;
            SoakPreviousShots.Add(State.Id,Shots);SoakPreviousMelee.Add(State.Id,Melee);SoakPreviousTeams.Add(State.Id,State.Team);Row->SetNumberField(TEXT("shots"),Shots);Row->SetNumberField(TEXT("melee"),Melee);
            if(Mode->Model.GetPhase()==EHeliosBioPhase::Active||Stage==2)
            {
                // Events remain countable when their owner died or changed team between samples.
                if(EventTeam==EHeliosBioTeam::Human)SoakHumanShots+=ShotDelta;
                if(EventTeam==EHeliosBioTeam::Zombie||State.Team==EHeliosBioTeam::Zombie)SoakZombieMelee+=MeleeDelta;
                if(State.bAlive){if(State.Team==EHeliosBioTeam::Human)bSoakHumanMoved|=Grounded&&Delta>15.f;else if(State.Team==EHeliosBioTeam::Zombie)bSoakZombieMoved|=Grounded&&Delta>15.f;}
            }
            if(auto* NPC=Cast<AHeliosBioNPC>(Participant))
            {
                const FNavAgentProperties& Props=NPC->GetNavAgentPropertiesRef();const ANavigationData* Data=Nav?Nav->GetNavDataForProps(Props,NPC->GetActorLocation()):nullptr;FNavLocation At;
                const bool Projected=Nav&&Nav->ProjectPointToNavigation(NPC->GetActorLocation(),At,FVector(150,150,300),&Props);++SoakNavQueries;if(Projected)++SoakNavSuccess;
                Row->SetBoolField(TEXT("nav_projected"),Projected);Row->SetStringField(TEXT("nav_data"),Data?Data->GetName():TEXT(""));Row->SetNumberField(TEXT("agent_radius"),Props.AgentRadius);Row->SetNumberField(TEXT("agent_height"),Props.AgentHeight);
                SampleNPCDiagnostics(NPC,State,Row,Now);
            }
            else Row->SetStringField(TEXT("nav_scope"),TEXT("Local player is not a registered navigation agent."));
        }
        Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    Sample->SetArrayField(TEXT("participants"),Rows);FString Line;auto Writer=TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);FJsonSerializer::Serialize(Sample,Writer);Line+=TEXT("\n");
    FFileHelper::SaveStringToFile(Line,*(Out/TEXT("soak_samples.jsonl")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);++SoakSamples;
}

void AHeliosBioHotelQA::FinishNaturalRound(bool Completed)
{
    Stage=99;if(!bObserveOnly){ReleaseNaturalKeys();if(Mode&&Mode->Player){Mode->Player->StopBioFire();Mode->Player->ClearMovementInput();}}
    if(Completed)
    {
        Check(TEXT("natural_human_grounded_planar_movement"),bSoakHumanMoved);Check(TEXT("natural_zombie_grounded_planar_movement"),bSoakZombieMoved);Check(TEXT("natural_human_real_gun_shots"),SoakHumanShots>0);
        if(!bObserveOnly)Check(TEXT("natural_round_restart_input_or_handler"),bSoakRestarted);
        Check(TEXT("natural_every_NPC_diagnostics_sampled"),Mode&&NaturalActorEvidence.Num()==Mode->Bots.Num());
        for(const auto& Pair:NaturalActorEvidence)
        {
            const auto& Evidence=Pair.Value;
            Check(*FString::Printf(TEXT("natural_id%d_ground_foot_measurement_available"),Pair.Key),Evidence.GroundSamples>0);
            Check(*FString::Printf(TEXT("natural_id%d_animation_pose_progress_observed"),Pair.Key),Evidence.PoseAdvances>0);
        }
        if(bInputEvents){Check(TEXT("controller_input_move_event"),bInputMovementVerified);Check(TEXT("controller_input_jump_event"),bInputJumpVerified);Check(TEXT("controller_input_pause_resume_UI"),bInputPauseVerified&&bInputResumeVerified);}
    }
    auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("passed"),Completed&&Failures==0);Report->SetBoolField(TEXT("completed_natural_round"),Completed);Report->SetBoolField(TEXT("completed_full_timer_round"),Completed&&SoakRemainingAtWinner<=.1f&&SoakActiveDuration>=SoakConfiguredRound-1.f);
    Report->SetBoolField(TEXT("headless"),FParse::Param(FCommandLine::Get(),TEXT("NullRHI")));Report->SetBoolField(TEXT("rendered_acceptance"),false);Report->SetBoolField(TEXT("blue_box_ultimate_acceptance"),false);Report->SetBoolField(TEXT("forcedHumanForIntegrationFixture"),false);Report->SetStringField(TEXT("blue_identity_status"),TEXT("Not established by natural gameplay or Ultimate enum state."));
    Report->SetNumberField(TEXT("failures"),Failures);Report->SetArrayField(TEXT("checks"),Checks);Report->SetArrayField(TEXT("presentation_evidence"),PresentationEvidence);Report->SetStringField(TEXT("map"),GetWorld()->GetOutermost()->GetName());Report->SetStringField(TEXT("project_saved_dir"),FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()));
    Report->SetStringField(TEXT("expected_map"),ExpectedMap);Report->SetStringField(TEXT("participant_count_scope"),TEXT("Actual offline Mode.Config setting; original floating-scene participant count not independently verified."));Report->SetArrayField(TEXT("floating_platform_diagnostics"),PlatformDiagnostics);auto PlatformDomain=MakeShared<FJsonObject>();PlatformDomain->SetBoolField(TEXT("requested"),FParse::Param(FCommandLine::Get(),TEXT("HeliosBioFloatingPlatformQA")));PlatformDomain->SetBoolField(TEXT("passed"),false);PlatformDomain->SetStringField(TEXT("status"),TEXT("Behavior fixture pending; observed platform transforms only."));Report->SetObjectField(TEXT("floatingPlatformAcceptance"),PlatformDomain);
    Report->SetNumberField(TEXT("total_participants"),Mode?Mode->Model.GetParticipants().Num():0);Report->SetNumberField(TEXT("preparation_seconds_config"),Mode?Mode->Model.Config.PreparationSeconds:0);Report->SetNumberField(TEXT("round_seconds_config"),Mode?Mode->Model.Config.RoundSeconds:0);Report->SetNumberField(TEXT("active_game_seconds"),SoakActiveDuration);Report->SetNumberField(TEXT("remaining_seconds_at_winner"),SoakRemainingAtWinner);Report->SetNumberField(TEXT("natural_winner"),SoakWinner);Report->SetNumberField(TEXT("natural_infections"),SoakInfections);Report->SetNumberField(TEXT("human_shots_observed"),SoakHumanShots);Report->SetNumberField(TEXT("zombie_melee_observed"),SoakZombieMelee);Report->SetNumberField(TEXT("samples"),SoakSamples);Report->SetNumberField(TEXT("bot_nav_queries"),SoakNavQueries);Report->SetNumberField(TEXT("bot_nav_projection_successes"),SoakNavSuccess);Report->SetNumberField(TEXT("bot_nav_projection_ratio"),SoakNavQueries?double(SoakNavSuccess)/SoakNavQueries:0);Report->SetNumberField(TEXT("living_grounded_observations"),SoakGrounded);Report->SetBoolField(TEXT("normal_restart_verified"),bSoakRestarted);
    TArray<TSharedPtr<FJsonValue>> ActorDiagnostics;
    for(const auto& Pair:NaturalActorEvidence)
    {
        auto Row=MakeShared<FJsonObject>();const auto& Evidence=Pair.Value;Row->SetNumberField(TEXT("id"),Pair.Key);Row->SetNumberField(TEXT("ground_foot_samples"),Evidence.GroundSamples);Row->SetNumberField(TEXT("motion_samples"),Evidence.MotionSamples);Row->SetNumberField(TEXT("animation_clock_advances"),Evidence.ClockAdvances);Row->SetNumberField(TEXT("bone_pose_advances"),Evidence.PoseAdvances);Row->SetNumberField(TEXT("max_absolute_foot_gap_cm"),Evidence.MaxAbsoluteFootGap);Row->SetNumberField(TEXT("longest_animation_stall_seconds"),Evidence.LongestAnimationStall);Row->SetNumberField(TEXT("longest_move_intent_stall_seconds"),Evidence.LongestMovementStall);Row->SetBoolField(TEXT("foot_alignment_failed"),Evidence.bFootFailed);Row->SetBoolField(TEXT("animation_progress_failed"),Evidence.bAnimationFailed);Row->SetBoolField(TEXT("movement_progress_failed"),Evidence.bMovementFailed);ActorDiagnostics.Add(MakeShared<FJsonValueObject>(Row));
    }
    Report->SetArrayField(TEXT("per_NPC_diagnostics"),ActorDiagnostics);Report->SetNumberField(TEXT("round_seed"),SoakSeed);Report->SetBoolField(TEXT("observer_only"),bObserveOnly);Report->SetBoolField(TEXT("controller_input_events"),bInputEvents);Report->SetBoolField(TEXT("physical_device_input_verified"),false);Report->SetNumberField(TEXT("controller_input_events_sent"),InputEventsSent);Report->SetArrayField(TEXT("input_event_evidence"),NaturalInputEvidence);Report->SetBoolField(TEXT("pause_resume_UI_verified"),bInputPauseVerified&&bInputResumeVerified);
    Report->SetStringField(TEXT("scope"),bObserveOnly?TEXT("Observation only: no injected movement, aim, fire, transform, restart, damage, teleport, timer advance or Bot freeze. Physical device/UI operation requires separate external observation."):TEXT("Unmodified production round configuration and natural AI. PlayerController input events drive keys, fire and pause/restart; aim uses control rotation. No fixture damage, teleport, timer advance, Bot freeze or forced winner. Early victory is separately labeled."));
    Report->SetStringField(TEXT("restart_path"),bObserveOnly?TEXT("Observed only; no restart injected"):bInputEvents?TEXT("PlayerController F5 pressed/released events"):TEXT("RequestBioRestart handler"));
    FString Text;auto Writer=TJsonWriterFactory<>::Create(&Text);FJsonSerializer::Serialize(Report,Writer);FFileHelper::SaveStringToFile(Text,*(Out/TEXT("soak_report.json")));FPlatformMisc::RequestExit(false);
}

void AHeliosBioHotelQA::TickNaturalRound(float Dt,double Now)
{
    if(Now-Started>(bObserveOnly?ObserverSeconds:360.f)){if(!bObserveOnly)Check(TEXT("natural_round_wall_bound"),false);FinishNaturalRound(bObserveOnly&&SoakWinner!=0);return;}
    if(Stage==0)
    {
        Mode=AHeliosBioHotelGameMode::Find(GetWorld());if(!Mode||!Mode->Player||!Mode->Player->GetController()){if(Now-Started>15){Check(TEXT("natural_round_ready"),false);FinishNaturalRound(false);}return;}
        SoakConfiguredCount=Mode->Model.Config.ParticipantCount;SoakConfiguredPreparation=Mode->Model.Config.PreparationSeconds;SoakConfiguredRound=Mode->Model.Config.RoundSeconds;
        Check(TEXT("natural_round_correct_map"),GetWorld()->GetOutermost()->GetName()==ExpectedMap);TSet<AActor*> RealActors;for(const auto& RP:Mode->Model.GetParticipants())if(IsValid(Mode->ParticipantActor(RP.Id)))RealActors.Add(Mode->ParticipantActor(RP.Id));Check(TEXT("natural_round_offline_config_actual_participants"),SoakConfiguredCount>=2&&Mode->Model.GetParticipants().Num()==SoakConfiguredCount&&Mode->Bots.Num()==SoakConfiguredCount-1&&RealActors.Num()==SoakConfiguredCount);
        Check(TEXT("natural_round_inclusive_selection_and_valid_tuning"),SoakConfiguredPreparation>=0&&SoakConfiguredRound>0&&!Mode->Model.Config.bKeepLocalPlayerHuman);
        const FString Saved=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());Check(TEXT("natural_round_isolated_userdata"),Saved.Contains(TEXT("/userdata/"))||Saved.Contains(TEXT("\\userdata\\")));
        SoakSerial=Mode->RoundSerial;Capture(TEXT("soak_01_preparation"));SampleNaturalRound(Now);SoakNextSample=Now+.25;Stage=1;
        if(bInputEvents){InputTestStage=1;InputTestAt=Now+.5;}
    }
    if(bObserveOnly&&Mode&&Mode->Player)
    {
        if(auto* PC=Cast<APlayerController>(Mode->Player->GetController()))if(PC->IsInputKeyDown(EKeys::F9))
        {Capture(TEXT("observer_05_manual_finish"));SampleNaturalRound(Now);FinishNaturalRound(SoakWinner!=0);return;}
    }
    if(Now>=SoakNextSample){SampleNaturalRound(Now);SoakNextSample=Now+.25;}
    if(bInputEvents&&InputTestStage>0&&InputTestStage<6&&Now>=InputTestAt)
    {
        switch(InputTestStage)
        {
        case 1:InputSmokeBefore=Mode->Player->GetActorLocation();SendNaturalKey(EKeys::W,true);InputTestAt=Now+1;InputTestStage=2;break;
        case 2:SendNaturalKey(EKeys::W,false);bInputMovementVerified=FVector::Dist2D(InputSmokeBefore,Mode->Player->GetActorLocation())>10.f;SendNaturalKey(EKeys::SpaceBar,true);InputTestAt=Now+.2;InputTestStage=3;break;
        case 3:bInputJumpVerified=Mode->Player->GetCharacterMovement()->IsFalling();SendNaturalKey(EKeys::SpaceBar,false);SendNaturalKey(EKeys::Escape,true);SendNaturalKey(EKeys::Escape,false);InputTestAt=Now+.3;InputTestStage=4;break;
        case 4:bInputPauseVerified=GetWorld()->IsPaused()&&Cast<APlayerController>(Mode->Player->GetController())->bShowMouseCursor;Capture(TEXT("soak_04_event_pause_UI"));SendNaturalKey(EKeys::Escape,true);SendNaturalKey(EKeys::Escape,false);InputTestAt=Now+.3;InputTestStage=5;break;
        case 5:bInputResumeVerified=!GetWorld()->IsPaused()&&!Cast<APlayerController>(Mode->Player->GetController())->bShowMouseCursor;InputTestStage=6;break;
        }
    }
    const auto Phase=Mode->Model.GetPhase();
    if(Stage==1&&Phase==EHeliosBioPhase::Active){SoakActiveWall=Now;SoakActiveGame=GetWorld()->GetTimeSeconds();Stage=2;Check(TEXT("natural_preparation_completed"),true);}
    if(Stage==2&&Phase==EHeliosBioPhase::Active&&!bSoakCombatImage&&Now-SoakActiveWall>=2){Capture(TEXT("soak_02_natural_combat"));bSoakCombatImage=true;}
    if(Stage==2&&Phase==EHeliosBioPhase::Active&&!bObserveOnly&&!GetWorld()->IsPaused())
    {
        const auto* Local=Mode->Model.FindParticipant(0);
        if(Local&&Local->bAlive)
        {
            if(Mode->Model.CanTransform(0)){if(bInputEvents){if(InputPulseUntil<Now){SendNaturalKey(EKeys::E,true);SendNaturalKey(EKeys::E,false);InputPulseUntil=Now+2;}}else Mode->Player->TryTransform();}AActor* Target=nullptr;float Nearest=FLT_MAX;
            for(const auto& Candidate:Mode->Model.GetParticipants())if(Candidate.bAlive&&Mode->AreOpponents(0,Candidate.Id)){AActor* Actor=Mode->ParticipantActor(Candidate.Id);if(Actor){const float Distance=FVector::DistSquared(Mode->Player->GetActorLocation(),Actor->GetActorLocation());if(Distance<Nearest){Nearest=Distance;Target=Actor;}}}
            if(Target)
            {
                FVector Eye;FRotator Aim;Mode->Player->GetActorEyesViewPoint(Eye,Aim);const FVector Goal=Target->GetActorLocation()+FVector(0,0,25);Mode->Player->GetController()->SetControlRotation((Goal-Eye).Rotation());
                FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(BioNaturalAim),false,Mode->Player);Query.AddIgnoredActor(Mode->Player->GetEquippedWeapon());const bool Clear=!GetWorld()->LineTraceSingleByChannel(Hit,Eye,Goal,ECC_Visibility,Query)||Hit.GetActor()==Target;
                if(bInputEvents)
                {
                    if(Clear!=bInputFireHeld){SendNaturalKey(EKeys::LeftMouseButton,Clear);bInputFireHeld=Clear;}
                    if(Local->Team==EHeliosBioTeam::Human&&Mode->Player->GetAmmo()==0&&InputPulseUntil<Now){SendNaturalKey(EKeys::R,true);SendNaturalKey(EKeys::R,false);InputPulseUntil=Now+1;}
                    const FKey MoveKey=Local->Team==EHeliosBioTeam::Human?EKeys::D:EKeys::W;
                    if(!bInputMoveHeld||NaturalMoveKey!=MoveKey){if(bInputMoveHeld)SendNaturalKey(NaturalMoveKey,false);SendNaturalKey(MoveKey,true);NaturalMoveKey=MoveKey;bInputMoveHeld=true;}
                }
                else
                {
                    if(Clear)Mode->Player->StartBioFire();else Mode->Player->StopBioFire();if(Local->Team==EHeliosBioTeam::Human&&Mode->Player->GetAmmo()==0)Mode->Player->ReloadBio();
                    const FVector Flat=(Target->GetActorLocation()-Mode->Player->GetActorLocation()).GetSafeNormal2D();Mode->Player->AddMovementInput(Local->Team==EHeliosBioTeam::Human?FVector::CrossProduct(FVector::UpVector,Flat):Flat,1.f);
                }
            }
            else if(bInputEvents)ReleaseNaturalKeys();
        }
        else{ReleaseNaturalKeys();Mode->Player->StopBioFire();Mode->Player->ClearMovementInput();}
    }
    if(Stage==2&&Phase==EHeliosBioPhase::RoundOver)
    {
        SampleNaturalRound(Now);if(!bObserveOnly){ReleaseNaturalKeys();Mode->Player->StopBioFire();Mode->Player->ClearMovementInput();}SoakWinner=static_cast<int32>(Mode->Model.GetWinner());SoakActiveDuration=GetWorld()->GetTimeSeconds()-SoakActiveGame;SoakRemainingAtWinner=Mode->Model.GetPhaseTimeRemaining();SoakInfections=Mode->TotalInfections;SoakWinnerWall=Now;Check(TEXT("natural_winner_observed"),Mode->Model.GetWinner()!=EHeliosBioWinner::None);Stage=3;
        if(!bSoakCombatImage){Check(TEXT("natural_active_combat_image_observed"),false);}
    }
    if(Stage==3&&Now-SoakWinnerWall>=.5){Capture(TEXT("soak_03_natural_winner"));Next=Now+.6;Stage=4;}
    if(Stage==4&&Now>=Next){if(bObserveOnly){bSoakRestarted=Mode->RoundSerial>SoakSerial;if(bSoakRestarted||Now-SoakWinnerWall>30.f)FinishNaturalRound(true);return;}if(bInputEvents){SendNaturalKey(EKeys::F5,true);SendNaturalKey(EKeys::F5,false);}else Mode->Player->RequestBioRestart();Next=Now+2;Stage=5;}
    if(Stage==5&&Now>=Next){bSoakRestarted=Mode->RoundSerial>SoakSerial&&Mode->Model.GetWinner()==EHeliosBioWinner::None&&Mode->Model.GetParticipants().Num()==SoakConfiguredCount;Check(TEXT("natural_round_config_remained_unmodified"),Mode->Model.Config.ParticipantCount==SoakConfiguredCount&&FMath::IsNearlyEqual(Mode->Model.Config.PreparationSeconds,SoakConfiguredPreparation)&&FMath::IsNearlyEqual(Mode->Model.Config.RoundSeconds,SoakConfiguredRound));FinishNaturalRound(true);}
}

void AHeliosBioHotelQA::FinishSpecial(const TCHAR* Domain,const FString& Blocker)
{
    if(FString(Domain)==TEXT("FloatingPlatform")&&!FParse::Param(FCommandLine::Get(),TEXT("BioLegacyPlatformFixture"))&&Mode)
    {
        ACharacter* Actor=bPlatformRoutePlayer?static_cast<ACharacter*>(Mode->Player.Get()):static_cast<ACharacter*>(AIBoardBot.Get());
        if(IsValid(Actor)){if(AHeliosBioStaticJumpLink::IsTraversing(Actor))AHeliosBioStaticJumpLink::Forget(Actor);const auto D=AHeliosBioStaticJumpLink::GetDiagnostics(Actor);const int32 Attempts=FMath::Max(0,D.Attempts-RouteStaticJumpAttemptsBefore),Landings=FMath::Max(0,D.Landings-RouteStaticJumpLandingsBefore),Failed=FMath::Max(0,D.Failures-RouteStaticJumpFailuresBefore);if(bPlatformRoutePlayer){PlayerStaticJumpAttempts=Attempts;PlayerStaticJumpLandings=Landings;PlayerStaticJumpFailures=Failed;}else{AIStaticJumpAttempts=Attempts;AIStaticJumpLandings=Landings;AIStaticJumpFailures=Failed;}}
    }
    Stage=99;if(Mode&&Mode->Player){Mode->Player->StopBioFire();Mode->Player->ClearMovementInput();}
    if(FString(Domain)==TEXT("FloatingPlatform")&&!FParse::Param(FCommandLine::Get(),TEXT("BioLegacyPlatformFixture")))
    {
        auto Report=MakeShared<FJsonObject>();const bool Passed=Failures==0&&Blocker.IsEmpty()&&bPlayerRouteCompleted&&bAIRouteCompleted&&bProductionPlatformWeaponBlocked;
        Report->SetBoolField(TEXT("passed"),Passed);Report->SetStringField(TEXT("domain"),Domain);Report->SetStringField(TEXT("status"),Passed?TEXT("independent_routes_observed"):TEXT("incomplete_or_blocked"));Report->SetStringField(TEXT("blocker"),Blocker);Report->SetNumberField(TEXT("failures"),Failures);
        Report->SetBoolField(TEXT("legacy_forced_position_fixture_used"),false);Report->SetBoolField(TEXT("player_normal_spawn_walk_jump_route_verified"),bPlayerRouteCompleted);Report->SetNumberField(TEXT("player_completed_one_way_legs"),PlayerRouteLegs);Report->SetNumberField(TEXT("player_completed_round_trips"),PlayerRouteLegs/2);Report->SetStringField(TEXT("player_route_blocker"),PlayerRouteBlocker);
        Report->SetNumberField(TEXT("actual_original_platform_native_impacts"),PlatformNativeImpacts);Report->SetStringField(TEXT("platform_weapon_evidence_blocker"),PlatformWeaponEvidenceBlocker);Report->SetStringField(TEXT("platform_weapon_native_observer_scope"),TEXT("Original Zeus native pellet FHitResult with committed shot/ammo proof; actual AShooterProjectile impact remains supported. Other native weapon overrides require their own real impact observer."));
        Report->SetNumberField(TEXT("player_registered_static_jump_attempts"),PlayerStaticJumpAttempts);Report->SetNumberField(TEXT("player_registered_static_jump_landings"),PlayerStaticJumpLandings);Report->SetNumberField(TEXT("player_registered_static_jump_failures"),PlayerStaticJumpFailures);Report->SetBoolField(TEXT("player_registered_static_jump_observed"),PlayerStaticJumpAttempts>0&&PlayerStaticJumpLandings>0);Report->SetNumberField(TEXT("AI_registered_static_jump_attempts"),AIStaticJumpAttempts);Report->SetNumberField(TEXT("AI_registered_static_jump_landings"),AIStaticJumpLandings);Report->SetNumberField(TEXT("AI_registered_static_jump_failures"),AIStaticJumpFailures);
        Report->SetBoolField(TEXT("AI_normal_spawn_complete_route_verified"),bAIRouteCompleted);Report->SetNumberField(TEXT("AI_completed_one_way_legs"),AIRouteLegs);Report->SetNumberField(TEXT("AI_completed_round_trips"),AIRouteLegs/2);Report->SetStringField(TEXT("AI_route_blocker"),AIRouteBlocker);Report->SetBoolField(TEXT("AI_motion_forced_by_fixture"),false);Report->SetBoolField(TEXT("AI_target_player_repositioned_as_fixture"),RouteTargetRepositions>0);Report->SetNumberField(TEXT("AI_target_fixture_repositions"),RouteTargetRepositions);
        Report->SetNumberField(TEXT("AI_opponent_fixture_real_platform_landings"),RouteTargetPlatformLandings);Report->SetBoolField(TEXT("AI_opponent_fixture_platform_landing_pending"),bRouteTargetPlatformLandingPending);
        Report->SetBoolField(TEXT("production_weapon_platform_obstruction_verified"),bProductionPlatformWeaponBlocked);Report->SetBoolField(TEXT("production_projectile_obstruction_verified"),bProductionPlatformWeaponBlocked);Report->SetNumberField(TEXT("production_projectile_original_platform_impacts"),PlatformProjectileImpacts);Report->SetBoolField(TEXT("AI_boarding_verified"),bAIRouteCompleted);Report->SetBoolField(TEXT("natural_full_round_verified"),false);Report->SetBoolField(TEXT("original_route_trigger_semantics_verified"),false);Report->SetBoolField(TEXT("rendered_acceptance"),false);Report->SetBoolField(TEXT("headless"),FParse::Param(FCommandLine::Get(),TEXT("NullRHI")));Report->SetStringField(TEXT("map"),GetWorld()->GetOutermost()->GetName());Report->SetStringField(TEXT("expected_map"),ExpectedMap);
        Report->SetStringField(TEXT("scope"),TEXT("Independent player and AI routes from normal round-reset spawns. Player follows every point of a complete path, preserving native flags/custom IDs, and uses the shared native Jump/air-input controller until actual landing. AI uses production target selection/tactical goals/traversal, without artificial ground-route cost inflation. Other NPCs are frozen, abilities suppressed, round duration extended, and AI opponent position/HP are explicit fixtures. For every AI leg the opponent is placed above the original platform and must really land with that movement base, providing a necessary boarding objective even when walking to a static target is faster. After actual AI boarding/carry the opponent moves to a supported high exit, then to the continuation floor after actual AI departure. Every opponent migration is reported; the tested AI is never repositioned. Four one-way legs per actor require physical waiting/boarding/carry/departure and continued walking/pursuit; platform timing/collision is unmodified. This is controlled-fixture capability evidence, not natural-round occurrence, every static jump link, or source trigger enum semantics."));
        TArray<TSharedPtr<FJsonValue>> Missing;if(!bPlayerRouteCompleted)Missing.Add(MakeShared<FJsonValueString>(TEXT("Player four complete one-way legs from normal spawn")));if(!bAIRouteCompleted)Missing.Add(MakeShared<FJsonValueString>(TEXT("AI four complete one-way legs from normal spawn")));if(!bProductionPlatformWeaponBlocked)Missing.Add(MakeShared<FJsonValueString>(TEXT("Real production firearm ammo consumption with original platform obstruction and victim HP unchanged")));Missing.Add(MakeShared<FJsonValueString>(TEXT("Uncontrolled natural-round traversal occurrence was not measured by this fixture")));Report->SetArrayField(TEXT("not_observed_mechanisms"),Missing);Report->SetArrayField(TEXT("checks"),Checks);Report->SetArrayField(TEXT("route_evidence"),PlatformRouteEvidence);Report->SetArrayField(TEXT("platform_diagnostics"),PlatformDiagnostics);Report->SetArrayField(TEXT("presentation_evidence"),PresentationEvidence);
        FString Text;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Out/TEXT("platform_report.json")));FPlatformMisc::RequestExit(false);return;
    }
    const bool Hunter=FString(Domain)==TEXT("HunterChoice");const bool Mechanisms=Blocker.IsEmpty()&&Failures==0&&(Hunter?(bChoiceSelected&&bChoiceSequenceCompleted&&bGenericUltimateDeathObserved&&bGenericUltimateResetObserved):bPlatformCarried&&bPlatformRayBlocked&&bAIBoardLaunchObserved&&bAIBoardLanded&&bAIBoardCarried);
    auto Report=MakeShared<FJsonObject>();Report->SetBoolField(TEXT("passed"),Mechanisms);Report->SetStringField(TEXT("domain"),Domain);Report->SetStringField(TEXT("status"),Blocker.IsEmpty()?TEXT("observed"):TEXT("blocked"));Report->SetStringField(TEXT("blocker"),Blocker);Report->SetNumberField(TEXT("failures"),Failures);Report->SetArrayField(TEXT("checks"),Checks);Report->SetArrayField(TEXT("presentation_evidence"),PresentationEvidence);Report->SetArrayField(TEXT("platform_diagnostics"),PlatformDiagnostics);Report->SetStringField(TEXT("map"),GetWorld()->GetOutermost()->GetName());Report->SetStringField(TEXT("expected_map"),ExpectedMap);Report->SetBoolField(TEXT("headless"),FParse::Param(FCommandLine::Get(),TEXT("NullRHI")));Report->SetBoolField(TEXT("rendered_acceptance"),false);Report->SetBoolField(TEXT("legacy_fallback_used"),false);Report->SetStringField(TEXT("selected_role"),ChoiceRole.ToString());Report->SetBoolField(TEXT("four_source_stages_completed"),bChoiceSequenceCompleted);Report->SetBoolField(TEXT("death_and_reset_observed"),bGenericUltimateDeathObserved&&bGenericUltimateResetObserved);Report->SetBoolField(TEXT("rider_carry_observed"),bPlatformCarried);Report->SetBoolField(TEXT("actual_visibility_obstruction"),bPlatformRayBlocked);Report->SetBoolField(TEXT("production_projectile_obstruction_verified"),false);Report->SetBoolField(TEXT("AI_boarding_verified"),false);Report->SetBoolField(TEXT("original_route_trigger_semantics_verified"),false);Report->SetStringField(TEXT("scope"),Hunter?TEXT("Real blue pending/cancel/select handlers, available verified role and four-slash component, actual death and normal restart. This is mechanism evidence; raw UI mouse click is not injected."):TEXT("Original configured platform, real collision trace and CharacterMovement jump/base/carry/return fixture. AI boarding and original route-trigger semantics remain unverified; trace alone is not production projectile verification."));
    if(!Hunter){Report->SetBoolField(TEXT("legacy_forced_position_fixture_used"),true);Report->SetBoolField(TEXT("complete_independent_route_verified"),false);Report->SetStringField(TEXT("scope"),TEXT("Legacy forced-position landing/carry/boarding diagnostics only. These checks do not count as normal-spawn complete player/AI traversal or production firearm obstruction acceptance."));}
    Report->SetBoolField(TEXT("AI_boarding_verified"),bAIBoardLaunchObserved&&bAIBoardLanded&&bAIBoardCarried);Report->SetArrayField(TEXT("actual_AI_boarding_samples"),AIBoardEvidence);Report->SetBoolField(TEXT("player_platform_basic_observed"),bPlatformCarried&&bPlatformRayBlocked);Report->SetArrayField(TEXT("actual_hunter_candidates"),HunterCandidateEvidence);FString Text;auto Writer=TJsonWriterFactory<>::Create(&Text);FJsonSerializer::Serialize(Report,Writer);FFileHelper::SaveStringToFile(Text,*(Out/(Hunter?TEXT("hunter_choice_report.json"):TEXT("platform_legacy_report.json"))));FPlatformMisc::RequestExit(false);
}

void AHeliosBioHotelQA::TickHunterChoice(double Now)
{
    if(Now-Started>(FParse::Param(FCommandLine::Get(),TEXT("BioManualHunterChoice"))?360.f:120.f)){Check(TEXT("choice_fixture_wall_bound"),false);FinishSpecial(TEXT("HunterChoice"),TEXT("Timeout"));return;}if(Now<Next)return;
    if(ChoiceSequence&&ChoiceSequence->IsBusy()&&ChoiceSequence->GetCurrentStage()!=ChoiceLastStage)
    {
        ChoiceLastStage=ChoiceSequence->GetCurrentStage();auto* ArtDefinition=HeliosBioArt::Named(ChoiceRole);UAnimSequence* ExpectedClip=ArtDefinition&&ArtDefinition->FPSkillChain.IsValidIndex(ChoiceLastStage)?ArtDefinition->FPSkillChain[ChoiceLastStage].LoadSynchronous():nullptr;bool ActualClip=false;TArray<USkeletalMeshComponent*> ViewMeshes;Mode->Player->GetComponents(ViewMeshes);for(auto* ViewMesh:ViewMeshes)if(ViewMesh->IsVisible()&&!ViewMesh->bHiddenInGame&&ViewMesh->GetSingleNodeInstance()&&ExpectedClip&&ViewMesh->GetSingleNodeInstance()->GetCurrentAsset()==ExpectedClip)ActualClip=true;Check(*FString::Printf(TEXT("choice_real_source_clip_stage_%d"),ChoiceLastStage),ActualClip);RecordPresentation(*FString::Printf(TEXT("choice_real_slash_stage_%d"),ChoiceLastStage));
    }
    switch(Stage)
    {
    case 0:
        Mode=AHeliosBioHotelGameMode::Find(GetWorld());if(!Mode||!Mode->Player){FinishSpecial(TEXT("HunterChoice"),TEXT("No real mode/player"));return;}Check(TEXT("choice_expected_map"),GetWorld()->GetOutermost()->GetName()==ExpectedMap);Mode->Model.Config.bRequireHunterSelection=true;Mode->Model.Config.bKeepLocalPlayerHuman=true;Mode->Model.Config.PreparationSeconds=.2f;Mode->RestartBioRound();for(auto Bot:Mode->Bots)if(Bot)Bot->bQAFreeze=true;Next=Now+.7;Stage=1;break;
    case 1:
    {
        for(auto B:Mode->Bots)B->bQAFreeze=true;Zombie=ZombieId();for(int32 Id=1;Id<Mode->Model.GetParticipants().Num();++Id){const auto* BP=Mode->Model.FindParticipant(Id);if(BP&&BP->Team==EHeliosBioTeam::Human)Mode->DamageParticipant(Id,Zombie,1);}Check(TEXT("choice_ghost_threshold"),Mode->TransformParticipant(0));
        auto* Supply=Mode->Supplies.Num()?Mode->Supplies[0].Get():nullptr;if(Supply){Supply->SetActorTickEnabled(false);Supply->SetKind(EHeliosBioSupplyKind::BlueHunterUpgrade);Supply->SetAvailable(true);}Check(TEXT("choice_real_blue_pickup"),Supply&&Supply->bBlueSupplySourceVerified&&Mode->PickupSupply(0,Supply));const auto* Local=Mode->Model.FindParticipant(0);Check(TEXT("blue_pending_retains_ghost"),Local&&Local->Team==EHeliosBioTeam::GhostHunter&&Local->bHunterChoicePending&&Mode->bHunterChooserOpen);Next=Now+.4;Stage=2;break;
    }
    case 2:
        Capture(TEXT("choice_01_pending"));Next=Now+.2;Stage=22;break;
    case 22:
    {
        Mode->CloseHunterChooser();Check(TEXT("choice_cancel_preserves_pending_ghost"),!Mode->bHunterChooserOpen&&Mode->Model.FindParticipant(0)->Team==EHeliosBioTeam::GhostHunter&&Mode->Model.FindParticipant(0)->bHunterChoicePending);Mode->OpenHunterChooser();Check(TEXT("choice_reopen_normal_handler"),Mode->bHunterChooserOpen);Check(TEXT("choice_invalid_role_rejected"),!Mode->SelectHunter(0,TEXT("unverified_role_fixture")));
        int32 Available=0;bool HasWukong=false;for(const auto& Option:Mode->HunterOptions()){auto* ArtDefinition=HeliosBioArt::Named(Option.Id);auto Candidate=MakeShared<FJsonObject>();Candidate->SetStringField(TEXT("role_key"),Option.Id.ToString());Candidate->SetBoolField(TEXT("available"),Option.bAvailable);Candidate->SetBoolField(TEXT("identity_verified"),ArtDefinition&&ArtDefinition->bIdentityVerified);Candidate->SetStringField(TEXT("source_identity"),ArtDefinition?ArtDefinition->SourceIdentity:TEXT(""));Candidate->SetStringField(TEXT("actual_DA"),ArtDefinition?ArtDefinition->GetPathName():TEXT(""));Candidate->SetStringField(TEXT("FP_mesh"),ArtDefinition?ArtDefinition->FirstPersonMesh.ToSoftObjectPath().ToString():TEXT(""));Candidate->SetStringField(TEXT("TP_mesh"),ArtDefinition?ArtDefinition->ThirdPersonMesh.ToSoftObjectPath().ToString():TEXT(""));HunterCandidateEvidence.Add(MakeShared<FJsonValueObject>(Candidate));if(Option.bAvailable&&ArtDefinition&&ArtDefinition->bIdentityVerified&&ArtDefinition->FirstPersonMesh.LoadSynchronous()&&ArtDefinition->ThirdPersonMesh.LoadSynchronous()){++Available;HasWukong|=ArtDefinition->SourceIdentity.Contains(TEXT("SunWuKong"),ESearchCase::IgnoreCase);if(ArtDefinition->SourceIdentity.Contains(TEXT("SunShangXiang"),ESearchCase::IgnoreCase)&&ArtDefinition->FPSkillChain.Num()==4)ChoiceRole=Option.Id;}}
        Check(TEXT("choice_at_least_two_verified_candidates"),Available>=2);Check(TEXT("choice_verified_wukong_candidate_available"),HasWukong);if(ChoiceRole.IsNone()){FinishSpecial(TEXT("HunterChoice"),TEXT("No available verified four-source-slash role; legacy fallback forbidden"));return;}
        if(FParse::Param(FCommandLine::Get(),TEXT("BioManualHunterChoice"))){Stage=20;Next=Now+.1;UE_LOG(LogTemp,Display,TEXT("BIO_MANUAL_CHOICE_WAITING_FOR_REAL_MENU_CLICK"));break;}
        Check(TEXT("choice_confirm_real_select_handler"),Mode->SelectHunter(0,ChoiceRole));const auto* Local=Mode->Model.FindParticipant(0);bChoiceSelected=Local&&Local->Team==EHeliosBioTeam::UltimateHunter&&Local->HunterRoleId==ChoiceRole&&!Local->bHunterChoicePending;Check(TEXT("choice_confirmed_role_key"),bChoiceSelected);Next=Now+1.6;Stage=3;break;
    }
    case 20:
    {
        const auto* Local=Mode->Model.FindParticipant(0);
        if(!Local||Local->bHunterChoicePending||Local->Team!=EHeliosBioTeam::UltimateHunter){Next=Now+.1;break;}
        ChoiceRole=Local->HunterRoleId;bChoiceSelected=!ChoiceRole.IsNone();Check(TEXT("manual_menu_choice_updates_real_role"),bChoiceSelected);
        auto* ArtDefinition=HeliosBioArt::Named(ChoiceRole);
        if(!ArtDefinition||ArtDefinition->FPSkillChain.Num()!=4){FinishSpecial(TEXT("HunterChoice"),TEXT("Manual selection observed; this independent fixture requires SunShangXiang for four-slash continuation"));return;}
        UE_LOG(LogTemp,Display,TEXT("BIO_MANUAL_CHOICE_REAL_ROLE_SELECTED %s"),*ChoiceRole.ToString());Next=Now+1.6;Stage=3;break;
    }
    case 3:
    {
        Capture(TEXT("choice_02_selected"));ChoiceSequence=Mode->Player->FindComponentByClass<UHeliosBioFourSlashComponent>();auto* ArtDefinition=HeliosBioArt::Named(ChoiceRole);USkeletalMeshComponent* ActualView=nullptr;TArray<USkeletalMeshComponent*> Meshes;Mode->Player->GetComponents(Meshes);for(auto* Mesh:Meshes)if(ArtDefinition&&Mesh->IsVisible()&&!Mesh->bHiddenInGame&&Mesh->GetSkeletalMeshAsset()==ArtDefinition->FirstPersonMesh.LoadSynchronous())ActualView=Mesh;
        Check(TEXT("choice_actual_selected_source_mesh"),ActualView!=nullptr);if(!ChoiceSequence||!ActualView){FinishSpecial(TEXT("HunterChoice"),TEXT("Real player-bound four-slash component or selected source view mesh missing"));return;}
        Zombie=ZombieId();auto* Victim=Mode->ParticipantActor(Zombie);if(!Victim){FinishSpecial(TEXT("HunterChoice"),TEXT("No actual live zombie target"));return;}const FVector Forward=Mode->Player->GetControlRotation().Vector().GetSafeNormal2D();Victim->SetActorLocation(Mode->Player->GetActorLocation()+Forward*200);FVector Eye;FRotator Aim;Mode->Player->GetActorEyesViewPoint(Eye,Aim);Mode->Player->GetController()->SetControlRotation((Victim->GetActorLocation()+FVector(0,0,25)-Eye).Rotation());HealthBefore=Mode->Model.FindParticipant(Zombie)->Health;
        Mode->Player->UseBioSkill();Check(TEXT("choice_real_four_source_sequence_started"),ChoiceSequence->IsBusy());const int32 BusyMeleeBefore=Mode->Player->MeleeAttacks,BusyShotsBefore=Mode->Player->ShotsFired;Mode->Player->StartBioFire();Mode->Player->StopBioFire();Check(TEXT("choice_busy_blocks_normal_fire"),ChoiceSequence->IsBusy()&&Mode->Player->MeleeAttacks==BusyMeleeBefore&&Mode->Player->ShotsFired==BusyShotsBefore);Next=Now+.05;Stage=4;break;
    }
    case 4:
        if(ChoiceSequence->IsBusy()){Next=Now+.05;break;}bChoiceSequenceCompleted=ChoiceSequence->GetCompletedStages()==4&&ChoiceSequence->GetHitCallbacks()==4;Check(TEXT("choice_real_four_completed_hit_callbacks"),bChoiceSequenceCompleted);Check(TEXT("choice_actual_slash_damage"),Mode->Model.FindParticipant(Zombie)->Health<HealthBefore);Capture(TEXT("choice_03_four_slashes"));
        if(ChoiceSequence){auto* ArtDefinition=HeliosBioArt::Named(ChoiceRole);TArray<USkeletalMeshComponent*> Meshes;Mode->Player->GetComponents(Meshes);for(auto* Mesh:Meshes)if(ArtDefinition&&Mesh->IsVisible()&&Mesh->GetSkeletalMeshAsset()==ArtDefinition->FirstPersonMesh.LoadSynchronous()){ChoiceSequence->BeginSourceSequence(0,ArtDefinition,Mesh,true);break;}ChoiceSequence->Cancel();Check(TEXT("choice_explicit_sequence_cancel"),!ChoiceSequence->IsBusy());}
        Next=Now+.4;Stage=5;break;
    case 5:
        Zombie=ZombieId();if(Zombie==INDEX_NONE){FinishSpecial(TEXT("HunterChoice"),TEXT("No live zombie available for real role death"));return;}Check(TEXT("choice_real_lethal_damage"),Mode->DamageParticipant(0,Zombie,Mode->Model.FindParticipant(0)->MaxHealth+1));Next=Now+.4;Stage=6;break;
    case 6:
        bGenericUltimateDeathObserved=!Mode->Model.FindParticipant(0)->bAlive&&Mode->Player->ActorHasTag(TEXT("Dead"))&&(!ChoiceSequence||!ChoiceSequence->IsBusy());Check(TEXT("choice_death_tick_cancels_sequence"),bGenericUltimateDeathObserved);Capture(TEXT("choice_04_death"));SerialBefore=Mode->RoundSerial;Next=Now+.4;Stage=7;break;
    case 7:
        Mode->Player->RequestBioRestart();Next=Now+.7;Stage=8;break;
    default:
        bGenericUltimateResetObserved=Mode->RoundSerial>SerialBefore&&Mode->Model.FindParticipant(0)->bAlive&&Mode->Model.FindParticipant(0)->Team==EHeliosBioTeam::Human&&Mode->Model.FindParticipant(0)->HunterRoleId.IsNone()&&!Mode->Model.FindParticipant(0)->bHunterChoicePending;Check(TEXT("choice_normal_reset_clears_role_pending"),bGenericUltimateResetObserved);FinishSpecial(TEXT("HunterChoice"));break;
    }
}

void AHeliosBioHotelQA::TickLegacyPlatformPhysics(double Now)
{
    if(Now-Started>60){Check(TEXT("platform_fixture_wall_bound"),false);FinishSpecial(TEXT("FloatingPlatform"),TEXT("Timeout"));return;}
    if(Stage==5&&Mode&&Mode->Player)Mode->Player->AddMovementInput(FVector(1,0,0),1.f);if(Now<Next)return;
    switch(Stage)
    {
    case 0:
    {
        Mode=AHeliosBioHotelGameMode::Find(GetWorld());if(!Mode||!Mode->Player){FinishSpecial(TEXT("FloatingPlatform"),TEXT("Real player/mode missing"));return;}Check(TEXT("platform_expected_map"),GetWorld()->GetOutermost()->GetName()==ExpectedMap);for(TActorIterator<AHeliosBioMovingPlatform> It(GetWorld());It;++It)if(It->IsRouteConfigured()&&It->GetPlatformMesh()->GetStaticMesh()){PhysicsPlatform=*It;break;}if(!PhysicsPlatform){FinishSpecial(TEXT("FloatingPlatform"),TEXT("Imported original configured platform missing; substitute geometry forbidden"));return;}
        PhysicsPlatform->ResetSourceRoute(false);auto* Mesh=PhysicsPlatform->GetPlatformMesh();Check(TEXT("platform_actual_mesh_is_root"),PhysicsPlatform->GetRootComponent()==Mesh);const FVector Landing(Mesh->Bounds.Origin.X,Mesh->Bounds.Origin.Y,Mesh->Bounds.Origin.Z+Mesh->Bounds.BoxExtent.Z+Mode->Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+180);Mode->Player->SetActorLocation(Landing);Mode->Player->GetCharacterMovement()->StopMovementImmediately();Mode->Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);Next=Now+3;Stage=1;break;
    }
    case 1:
        Check(TEXT("platform_real_landing_sets_movement_base"),Mode->Player->GetMovementBase()==PhysicsPlatform->GetPlatformMesh()&&Mode->Player->GetCharacterMovement()->IsMovingOnGround());Capture(TEXT("platform_01_boarded"));Mode->Player->Jump();Next=Now+.3;Stage=2;break;
    case 2:
        Check(TEXT("platform_actual_jump_detaches"),Mode->Player->GetCharacterMovement()->IsFalling());Mode->Player->StopJumping();Next=Now+2;Stage=3;break;
    case 3:
        Check(TEXT("platform_jump_lands_back_on_source"),Mode->Player->GetMovementBase()==PhysicsPlatform->GetPlatformMesh()&&Mode->Player->GetCharacterMovement()->IsMovingOnGround());PlatformBefore=PhysicsPlatform->GetActorLocation();PlatformPlayerBefore=Mode->Player->GetActorLocation();PlatformRelativeBefore=PlatformPlayerBefore-PlatformBefore;PhysicsPlatform->SetRouteRunning(true);Next=Now+2;Stage=4;break;
    case 4:
    {
        const FVector Relative=Mode->Player->GetActorLocation()-PhysicsPlatform->GetActorLocation();bPlatformCarried=FVector::Dist(PlatformBefore,PhysicsPlatform->GetActorLocation())>20&&FVector::Dist(PlatformPlayerBefore,Mode->Player->GetActorLocation())>20&&FVector::Dist(PlatformRelativeBefore,Relative)<100&&Mode->Player->GetMovementBase()==PhysicsPlatform->GetPlatformMesh();Check(TEXT("platform_real_route_moves_and_carries_base"),bPlatformCarried);auto* Mesh=PhysicsPlatform->GetPlatformMesh();FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(BioPlatformObstruction),true,Mode->Player);const FVector Origin=Mesh->Bounds.Origin;bPlatformRayBlocked=GetWorld()->LineTraceSingleByChannel(Hit,Origin+FVector(0,0,Mesh->Bounds.BoxExtent.Z+200),Origin-FVector(0,0,Mesh->Bounds.BoxExtent.Z+200),ECC_Visibility,Query)&&Hit.GetActor()==PhysicsPlatform;Check(TEXT("platform_actual_original_mesh_blocks_visibility_ray"),bPlatformRayBlocked);Capture(TEXT("platform_02_real_carry"));Mode->Player->Jump();Next=Now+5;Stage=5;break;
    }
    default:
        Mode->Player->StopJumping();Check(TEXT("platform_real_grounded_return_off_base"),Mode->Player->GetCharacterMovement()->IsMovingOnGround()&&Mode->Player->GetMovementBase()!=PhysicsPlatform->GetPlatformMesh());Capture(TEXT("platform_03_grounded_return"));Next=Now+.5;Stage=6;break;
    case 6:
        Mode->Model.Config.PreparationSeconds=.2f;Mode->Model.Config.bKeepLocalPlayerHuman=true;Mode->RestartBioRound();PhysicsPlatform->ResetSourceRoute(false);Next=Now+.7;Stage=7;break;
    case 7:
    {
        for(auto B:Mode->Bots)B->bQAFreeze=true;Zombie=ZombieId();if(Zombie==INDEX_NONE){FinishSpecial(TEXT("FloatingPlatform"),TEXT("No active actual zombie for AI boarding"));return;}
        for(int32 Id=1;Id<Mode->Model.GetParticipants().Num();++Id){const auto* BP=Mode->Model.FindParticipant(Id);if(BP&&BP->Team==EHeliosBioTeam::Human)Mode->DamageParticipant(Id,Zombie,1);}Mode->TransformParticipant(0);AIBoardBot=Cast<AHeliosBioNPC>(Mode->ParticipantActor(Zombie));
        if(!AIBoardBot){FinishSpecial(TEXT("FloatingPlatform"),TEXT("Actual zombie NPC missing"));return;}
        auto* Mesh=PhysicsPlatform->GetPlatformMesh();auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());const FVector Center=Mesh->Bounds.Origin,Extent=Mesh->Bounds.BoxExtent;const float Radius=AIBoardBot->GetCapsuleComponent()->GetScaledCapsuleRadius();bool DockFound=false;FVector Dock=FVector::ZeroVector;
        for(int32 Sample=0;Sample<32&&!DockFound;++Sample)
        {
            const float Angle=2*PI*Sample/32.f;const FVector Wanted=Center+FVector(FMath::Cos(Angle)*(Extent.X+Radius+90.f),FMath::Sin(Angle)*(Extent.Y+Radius+90.f),0);FNavLocation Projected;if(!Nav||!Nav->ProjectPointToNavigation(Wanted,Projected,FVector(70,70,180),&AIBoardBot->GetNavAgentPropertiesRef()))continue;
            if(FMath::Abs(Projected.Location.X-Center.X)<=Extent.X&&FMath::Abs(Projected.Location.Y-Center.Y)<=Extent.Y)continue;
            FHitResult Floor;FCollisionQueryParams Query(SCENE_QUERY_STAT(BioQAAIStaticDock),false,AIBoardBot);if(!GetWorld()->LineTraceSingleByChannel(Floor,Projected.Location+FVector(0,0,70),Projected.Location-FVector(0,0,180),ECC_Visibility,Query)||!Floor.GetComponent()||Floor.GetComponent()->Mobility!=EComponentMobility::Static||Floor.ImpactNormal.Z<AIBoardBot->GetCharacterMovement()->GetWalkableFloorZ())continue;
            if(FVector::Dist2D(Floor.ImpactPoint,Center)>HeliosBioPlatformTraversal::Tuning().MaxBoardingDistance||FMath::Abs(Floor.ImpactPoint.Z-Projected.Location.Z)>70.f)continue;Dock=Floor.ImpactPoint;DockFound=true;
        }
        if(!DockFound){FinishSpecial(TEXT("FloatingPlatform"),TEXT("No legal static shore NavAgent dock for original platform layout; no substitute geometry created"));return;}
        AIBoardBot->SetActorLocation(Dock+FVector(0,0,AIBoardBot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2));AIBoardBot->GetCharacterMovement()->StopMovementImmediately();AIBoardBot->GetCharacterMovement()->SetMovementMode(MOVE_Walking);HeliosBioPlatformTraversal::ForgetBot(AIBoardBot);
        const FVector Landing(Center.X,Center.Y,Center.Z+Extent.Z+Mode->Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+180);Mode->Player->SetActorLocation(Landing);Mode->Player->GetCharacterMovement()->StopMovementImmediately();Mode->Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);Next=Now+3;Stage=8;break;
    }
    case 8:
        Check(TEXT("AI_target_player_on_real_platform_base"),Mode->Player->GetMovementBase()==PhysicsPlatform->GetPlatformMesh());if(Mode->Player->GetMovementBase()!=PhysicsPlatform->GetPlatformMesh()){FinishSpecial(TEXT("FloatingPlatform"),TEXT("Real player target failed to land on source platform"));return;}AIBoardStart=AIBoardBot->GetActorLocation();AIBoardBot->bQAFreeze=false;AIBoardDeadline=Now+16;Next=Now+.1;Stage=9;break;
    case 9:
    {
        const FString NativeStep=HeliosBioPlatformTraversal::DebugState(AIBoardBot);auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("native_helper_debug"),NativeStep);Row->SetStringField(TEXT("position"),AIBoardBot->GetActorLocation().ToString());Row->SetStringField(TEXT("velocity"),AIBoardBot->GetVelocity().ToString());Row->SetBoolField(TEXT("falling"),AIBoardBot->GetCharacterMovement()->IsFalling());Row->SetBoolField(TEXT("platform_base"),AIBoardBot->GetMovementBase()==PhysicsPlatform->GetPlatformMesh());Row->SetNumberField(TEXT("planar_from_shore"),FVector::Dist2D(AIBoardStart,AIBoardBot->GetActorLocation()));AIBoardEvidence.Add(MakeShared<FJsonValueObject>(Row));UE_LOG(LogTemp,Display,TEXT("BIO_PLATFORM_QA_NATIVE_AI %s position=%s base=%s"),*NativeStep,*AIBoardBot->GetActorLocation().ToString(),*GetNameSafe(AIBoardBot->GetMovementBase()));
        bAIBoardLaunchObserved|=NativeStep.Contains(TEXT("step=1"))&&AIBoardBot->GetCharacterMovement()->IsFalling();bAIBoardLanded=AIBoardBot->GetMovementBase()==PhysicsPlatform->GetPlatformMesh()&&AIBoardBot->GetCharacterMovement()->IsMovingOnGround();
        if(bAIBoardLanded){Check(TEXT("AI_native_helper_real_launch_observed"),bAIBoardLaunchObserved);AIBoardRelative=AIBoardBot->GetActorLocation()-PhysicsPlatform->GetActorLocation();PlatformBefore=PhysicsPlatform->GetActorLocation();AIBoardStart=AIBoardBot->GetActorLocation();PhysicsPlatform->SetRouteRunning(true);Next=Now+2;Stage=10;break;}
        if(Now>=AIBoardDeadline){Check(TEXT("AI_real_source_platform_boarding"),false);FinishSpecial(TEXT("FloatingPlatform"),TEXT("Native AI traversal timed out; inspect actual helper debug samples"));return;}Next=Now+.1;break;
    }
    case 10:
        bAIBoardCarried=AIBoardBot->GetMovementBase()==PhysicsPlatform->GetPlatformMesh()&&FVector::Dist(PlatformBefore,PhysicsPlatform->GetActorLocation())>20&&FVector::Dist(AIBoardStart,AIBoardBot->GetActorLocation())>20&&FVector::Dist(AIBoardRelative,AIBoardBot->GetActorLocation()-PhysicsPlatform->GetActorLocation())<150;Check(TEXT("AI_actual_platform_base_carry"),bAIBoardCarried);Capture(TEXT("platform_04_native_AI_boarding"));Next=Now+.5;Stage=11;break;
    case 11:FinishSpecial(TEXT("FloatingPlatform"));break;
    }
}

void AHeliosBioHotelQA::TickPlatformPhysics(double Now)
{
    if(FParse::Param(FCommandLine::Get(),TEXT("BioLegacyPlatformFixture")))TickLegacyPlatformPhysics(Now);
    else TickPlatformRoute(Now);
}
bool AHeliosBioHotelQA::SetPlatformWalkPath(ACharacter* Character,const FVector& Goal)
{
    PlatformWalkBlocker.Reset();
    if(!Character||AHeliosBioStaticJumpLink::IsTraversing(Character)||bRouteWalkJumpPending){PlatformWalkBlocker=TEXT("Cannot replace a complete path while an actual static jump owns movement");return false;}
    auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Character->GetNavAgentLocation(),Goal,Character);
    if(!Path||!Path->IsValid()||Path->IsPartial()||Path->PathPoints.IsEmpty())return false;
    PlatformWalkPath.Reset();PlatformWalkJumps.Reset();RoutePathIndex=0;RouteWalkJumpLink.Reset();
    const auto Native=Path->GetPath();bPlatformPathMetadataAvailable=Native.IsValid()&&Native->IsValid()&&!Native->IsPartial()&&!Native->GetPathPoints().IsEmpty();
    TSet<FNavLinkId> CompleteCorridorLinks;
    if(const auto* Corridor=Native.IsValid()?Native->CastPath<FNavMeshPath>():nullptr)
        if(const auto* Recast=Cast<ARecastNavMesh>(Corridor->GetNavigationDataUsed()))
            for(NavNodeRef Ref:Corridor->PathCorridor){const FNavLinkId Id=Recast->GetNavLinkUserId(Ref);if(Id.IsValid())CompleteCorridorLinks.Add(Id);}
    if(bPlatformPathMetadataAvailable)PlatformWalkPath=Native->GetPathPoints();
    else for(const FVector& Point:Path->PathPoints)PlatformWalkPath.Add(FNavPathPoint(Point));
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("event"),TEXT("complete_player_walk_path"));Row->SetStringField(TEXT("actor"),Character->GetPathName());Row->SetStringField(TEXT("goal"),Goal.ToString());Row->SetBoolField(TEXT("complete_path"),true);Row->SetBoolField(TEXT("native_flags_and_custom_link_ids_available"),bPlatformPathMetadataAvailable);
    TArray<TSharedPtr<FJsonValue>> CorridorIds;for(FNavLinkId Id:CompleteCorridorLinks)CorridorIds.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%llu"),static_cast<unsigned long long>(Id.GetId()))));Row->SetArrayField(TEXT("actual_complete_Recast_corridor_custom_link_ids"),CorridorIds);
    TArray<TSharedPtr<FJsonValue>> Points;
    for(int32 I=0;I<PlatformWalkPath.Num();++I)
    {
        const auto& Point=PlatformWalkPath[I];auto P=MakeShared<FJsonObject>();P->SetNumberField(TEXT("index"),I);P->SetStringField(TEXT("position"),Point.Location.ToString());P->SetNumberField(TEXT("flags"),Point.Flags);P->SetBoolField(TEXT("native_off_mesh_link"),FNavMeshNodeFlags(Point.Flags).IsNavLink());P->SetStringField(TEXT("custom_nav_link_id"),FString::Printf(TEXT("%llu"),static_cast<unsigned long long>(Point.CustomNavLinkId.GetId())));Points.Add(MakeShared<FJsonValueObject>(P));
        if(I+1>=PlatformWalkPath.Num())continue;
        const bool HasId=Point.CustomNavLinkId.IsValid(),IsOffMesh=FNavMeshNodeFlags(Point.Flags).IsNavLink();
        AHeliosBioStaticJumpLink* Matched=nullptr;FVector Destination=FVector::ZeroVector;FString Match;float StartError=MAX_flt,EndError=MAX_flt;
        for(TActorIterator<AHeliosBioStaticJumpLink> It(GetWorld());It;++It)
        {
            if(!It->IsSmartLinkEnabled())continue;
            const bool IdMatches=HasId&&It->GetSmartLinkComp()&&Point.CustomNavLinkId==It->GetSmartLinkComp()->GetId();if(HasId&&!IdMatches)continue;
            // String pulling can merge a landing and the next link's start into
            // an ordinary point. Its original flags remain untouched. The
            // complete native corridor must still contain this exact custom ID.
            const bool InNativeCorridor=Native.IsValid()&&It->GetSmartLinkComp()&&(CompleteCorridorLinks.Contains(It->GetSmartLinkComp()->GetId())||Native->ContainsCustomLink(It->GetSmartLinkComp()->GetId()));
            if(bPlatformPathMetadataAvailable&&!HasId&&!IsOffMesh&&!InNativeCorridor)continue;
            const FVector A=It->GetStartWorldFeet(),B=It->GetEndWorldFeet(),NextPoint=PlatformWalkPath[I+1].Location;
            const bool Forward=FVector::DistSquared(Point.Location,A)+FVector::DistSquared(NextPoint,B)<=FVector::DistSquared(Point.Location,B)+FVector::DistSquared(NextPoint,A);
            const float FromError=FVector::Dist(Point.Location,Forward?A:B),ToError=FVector::Dist(NextPoint,Forward?B:A);
            // Only the consecutive endpoints of this complete nav segment can select a link.
            // Proximity of the character to an unrelated jump actor is never a route shortcut.
            if(FromError>45.f||ToError>45.f)continue;
            if(Matched){PlatformWalkBlocker=FString::Printf(TEXT("Ambiguous registered static jump endpoints at native path point %d"),I);break;}
            Matched=*It;Destination=Forward?B:A;StartError=FromError;EndError=ToError;
            Match=IdMatches?TEXT("exact_native_custom_nav_link_id_and_consecutive_endpoints"):InNativeCorridor?TEXT("complete_native_corridor_custom_link_id_and_registered_consecutive_endpoints"):bPlatformPathMetadataAvailable?TEXT("native_off_mesh_flag_and_registered_consecutive_endpoints_without_custom_id"):TEXT("native_metadata_unavailable_registered_consecutive_endpoints");
            P->SetBoolField(TEXT("native_corridor_contains_registered_custom_link"),InNativeCorridor);
        }
        if(!PlatformWalkBlocker.IsEmpty())break;
        if(!Matched)
        {
            if(HasId||IsOffMesh||FMath::Abs(PlatformWalkPath[I+1].Location.Z-Point.Location.Z)>Character->GetCharacterMovement()->MaxStepHeight+5.f){PlatformWalkBlocker=FString::Printf(TEXT("Complete path point %d has a custom/off-mesh or above-step segment without proven registered consecutive static link endpoints"),I);break;}
            continue;
        }
        auto& Segment=PlatformWalkJumps.Add(I);Segment.Link=Matched;Segment.Destination=Destination;Segment.EndIndex=I+1;Segment.Match=Match;
        P->SetStringField(TEXT("registered_jump_actor"),Matched->GetPathName());P->SetStringField(TEXT("jump_match_basis"),Match);P->SetStringField(TEXT("registered_destination"),Destination.ToString());P->SetNumberField(TEXT("registered_start_endpoint_error_cm"),StartError);P->SetNumberField(TEXT("registered_end_endpoint_error_cm"),EndError);
    }
    Row->SetArrayField(TEXT("native_path_points"),Points);Row->SetNumberField(TEXT("registered_static_jump_segments"),PlatformWalkJumps.Num());Row->SetStringField(TEXT("blocker"),PlatformWalkBlocker);PlatformRouteEvidence.Add(MakeShared<FJsonValueObject>(Row));
    return PlatformWalkBlocker.IsEmpty();
}
bool AHeliosBioHotelQA::AdvancePlatformWalkPath(ACharacter* Character)
{
    // The production jump link owns all movement input while traversing, including alignment.
    if(AHeliosBioStaticJumpLink::IsTraversing(Character))return false;
    if(bRouteWalkJumpPending)
    {
        const auto D=AHeliosBioStaticJumpLink::GetDiagnostics(Character);const bool Landed=RouteWalkJumpLink.IsValid()&&D.Landings>RouteWalkJumpLandingsBefore&&D.Failures==RouteWalkJumpFailuresBefore&&D.bJumpIssued&&D.bAirborneObserved&&Character->GetCharacterMovement()->IsMovingOnGround();
        auto E=MakeShared<FJsonObject>();E->SetStringField(TEXT("event"),Landed?TEXT("player_static_nav_jump_real_landing"):TEXT("player_static_nav_jump_failed"));E->SetNumberField(TEXT("path_start_index"),RoutePathIndex);E->SetNumberField(TEXT("path_end_index"),RouteWalkJumpEndIndex);E->SetStringField(TEXT("link"),GetNameSafe(RouteWalkJumpLink.Get()));E->SetStringField(TEXT("actual_feet"),Character->GetNavAgentLocation().ToString());E->SetStringField(TEXT("registered_destination"),D.End.ToString());E->SetStringField(TEXT("native_jump_diagnostics"),AHeliosBioStaticJumpLink::DebugState(Character));E->SetBoolField(TEXT("real_native_jump_and_airborne_observed"),D.bJumpIssued&&D.bAirborneObserved);E->SetBoolField(TEXT("landed"),Landed);PlatformRouteEvidence.Add(MakeShared<FJsonValueObject>(E));
        bRouteWalkJumpPending=false;RouteWalkJumpLink.Reset();
        if(!Landed){PlatformWalkBlocker=FString::Printf(TEXT("Actual static jump failed before complete path point %d: %s"),RouteWalkJumpEndIndex,*AHeliosBioStaticJumpLink::DebugState(Character));return false;}
        RoutePathIndex=RouteWalkJumpEndIndex;RouteWalkJumpEndIndex=INDEX_NONE;RouteStillSeconds=0;
    }
    const FVector Feet=Character->GetNavAgentLocation();auto* Movement=Character->GetCharacterMovement();
    while(PlatformWalkPath.IsValidIndex(RoutePathIndex))
    {
        const auto& Point=PlatformWalkPath[RoutePathIndex];const bool AtPoint=FVector::DistSquared2D(Feet,Point.Location)<FMath::Square(45.f)&&FMath::Abs(Feet.Z-Point.Location.Z)<=Movement->MaxStepHeight+5.f;
        if(const auto* Segment=PlatformWalkJumps.Find(RoutePathIndex))
        {
            if(!AtPoint){Character->AddMovementInput((Point.Location-Character->GetActorLocation()).GetSafeNormal2D(),1.f);return false;}
            auto* Link=Segment->Link.Get();const auto Before=AHeliosBioStaticJumpLink::GetDiagnostics(Character);
            // Discard the previous walking command before the link motor begins, never during it.
            Character->ConsumeMovementInputVector();const bool Began=Link&&Link->BeginTraversal(Character,Segment->Destination);
            auto E=MakeShared<FJsonObject>();E->SetStringField(TEXT("event"),TEXT("player_complete_path_static_jump_begin"));E->SetNumberField(TEXT("path_start_index"),RoutePathIndex);E->SetNumberField(TEXT("path_end_index"),Segment->EndIndex);E->SetStringField(TEXT("link"),GetNameSafe(Link));E->SetStringField(TEXT("match_basis"),Segment->Match);E->SetStringField(TEXT("actual_feet"),Feet.ToString());E->SetStringField(TEXT("registered_destination"),Segment->Destination.ToString());E->SetBoolField(TEXT("public_native_jump_handler_started"),Began);PlatformRouteEvidence.Add(MakeShared<FJsonValueObject>(E));
            if(!Began){PlatformWalkBlocker=FString::Printf(TEXT("Registered static jump refused complete path segment %d: %s"),RoutePathIndex,*AHeliosBioStaticJumpLink::DebugState(Character));return false;}
            RouteWalkJumpLink=Link;RouteWalkJumpEndIndex=Segment->EndIndex;RouteWalkJumpLandingsBefore=Before.Landings;RouteWalkJumpFailuresBefore=Before.Failures;bRouteWalkJumpPending=true;return false;
        }
        if(!AtPoint){Character->AddMovementInput((Point.Location-Character->GetActorLocation()).GetSafeNormal2D(),1.f);return false;}
        ++RoutePathIndex;
    }
    return true;
}
bool AHeliosBioHotelQA::PlacePlatformOpponentFixture(ACharacter* TestedActor,const TCHAR* StageName)
{
    auto* Opponent=Mode?Mode->Player.Get():nullptr;auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if(!Opponent||!TestedActor||!Nav||!PhysicsPlatform)return false;
    auto* TargetCapsule=Opponent->GetCapsuleComponent();auto* TestCapsule=TestedActor->GetCapsuleComponent();
    const float TargetRadius=TargetCapsule->GetScaledCapsuleRadius(),TargetHalf=TargetCapsule->GetScaledCapsuleHalfHeight(),TestRadius=TestCapsule->GetScaledCapsuleRadius(),TestHalf=TestCapsule->GetScaledCapsuleHalfHeight();
    const FVector Previous=Opponent->GetActorLocation();FVector Away=FVector::BackwardVector;float Nearest=MAX_flt;
    for(const FVector& SourcePoint:PhysicsPlatform->GetRoutePoints()){const float Distance=FVector::DistSquared2D(SourcePoint,RouteExit);if(Distance<Nearest){Nearest=Distance;Away=(RouteExit-SourcePoint).GetSafeNormal2D();}}
    FCollisionObjectQueryParams Static(ECC_WorldStatic);FCollisionQueryParams TargetQuery(SCENE_QUERY_STAT(BioQASupportedHighOpponentFixture),false,Opponent);TArray<AActor*> TargetAttached;Opponent->GetAttachedActors(TargetAttached,true,true);TargetQuery.AddIgnoredActors(TargetAttached);
    FCollisionQueryParams LandingQuery(SCENE_QUERY_STAT(BioQARealLandingRoomWithOpponent),false,TestedActor);TArray<AActor*> TestAttached;TestedActor->GetAttachedActors(TestAttached,true,true);LandingQuery.AddIgnoredActors(TestAttached);
    const FCollisionShape TargetShape=FCollisionShape::MakeCapsule(TargetRadius,TargetHalf),LandingShape=FCollisionShape::MakeCapsule(TestRadius,TestHalf);
    FHitResult LandingFloor;if(!GetWorld()->LineTraceSingleByObjectType(LandingFloor,RouteExit+FVector(0,0,20),RouteExit-FVector(0,0,20),Static,TargetQuery)||!LandingFloor.GetComponent()||LandingFloor.GetComponent()->Mobility!=EComponentMobility::Static||LandingFloor.ImpactNormal.Z<TestedActor->GetCharacterMovement()->GetWalkableFloorZ()||FMath::Abs(LandingFloor.ImpactPoint.Z-RouteExit.Z)>3.f)return false;
    for(float Radius:{60.f,80.f,100.f,110.f,120.f,140.f})for(int32 Index=0;Index<16;++Index)
    {
        const float Angle=2*PI*Index/16.f;const FVector Direction(Away.X*FMath::Cos(Angle)-Away.Y*FMath::Sin(Angle),Away.X*FMath::Sin(Angle)+Away.Y*FMath::Cos(Angle),0);const FVector Wanted=RouteExit+Direction*Radius;FHitResult Floor;
        if(!GetWorld()->LineTraceSingleByObjectType(Floor,Wanted+FVector(0,0,20),Wanted-FVector(0,0,20),Static,TargetQuery)||!Floor.GetComponent()||Floor.GetComponent()->Mobility!=EComponentMobility::Static||Floor.ImpactNormal.Z<Opponent->GetCharacterMovement()->GetWalkableFloorZ()||FMath::Abs(Floor.ImpactPoint.Z-RouteExit.Z)>3.f)continue;
        bool Separate=true;for(const FVector& Shore:RouteFixtureExitShoreCandidates)if(FMath::Abs(Shore.Z-RouteExit.Z)<3.f&&FVector::Dist2D(Shore,Floor.ImpactPoint)<TargetRadius+TestRadius+8.f){Separate=false;break;}
        if(!Separate||FVector::Dist2D(RouteExit,Floor.ImpactPoint)<TargetRadius+TestRadius+8.f)continue;
        const FVector At=Floor.ImpactPoint+FVector(0,0,TargetHalf+2.f);if(GetWorld()->OverlapBlockingTestByChannel(At,FQuat::Identity,ECC_Pawn,TargetShape,TargetQuery))continue;
        FNavLocation Projected;if(!Nav->ProjectPointToNavigation(Floor.ImpactPoint+FVector(0,0,2),Projected,FVector(70,70,180),&TestedActor->GetNavAgentPropertiesRef())||FMath::Abs(Projected.Location.Z-Floor.ImpactPoint.Z)>20.f)continue;
        auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),RouteExit,Floor.ImpactPoint,TestedActor);if(!Path||!Path->IsValid()||Path->IsPartial())continue;
        Opponent->SetActorLocation(At);++RouteTargetRepositions;Opponent->GetCharacterMovement()->StopMovementImmediately();Opponent->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        // This real post-placement landing probe includes the opponent capsule.
        // Production preflight still includes Player too; collision is never disabled.
        const bool LandingClear=!GetWorld()->OverlapBlockingTestByChannel(LandingFloor.ImpactPoint+FVector(0,0,TestHalf+2.f),FQuat::Identity,ECC_Pawn,LandingShape,LandingQuery);
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("event"),TEXT("AI_opponent_fixture_target_reposition"));Row->SetStringField(TEXT("stage"),StageName);Row->SetNumberField(TEXT("leg"),RouteLeg);Row->SetStringField(TEXT("opponent_previous_actor_location"),Previous.ToString());Row->SetStringField(TEXT("opponent_actual_actor_location"),Opponent->GetActorLocation().ToString());Row->SetStringField(TEXT("opponent_original_static_floor"),Floor.ImpactPoint.ToString());Row->SetStringField(TEXT("opponent_original_floor_component"),GetPathNameSafe(Floor.GetComponent()));Row->SetStringField(TEXT("planned_landing_original_static_floor"),LandingFloor.ImpactPoint.ToString());Row->SetStringField(TEXT("planned_landing_floor_component"),GetPathNameSafe(LandingFloor.GetComponent()));Row->SetBoolField(TEXT("target_capsule_collision_clear_before_placement"),true);Row->SetBoolField(TEXT("actual_landing_capsule_clear_including_moved_opponent"),LandingClear);Row->SetNumberField(TEXT("actual_target_capsule_radius_cm"),TargetRadius);Row->SetNumberField(TEXT("actual_target_capsule_half_height_cm"),TargetHalf);Row->SetNumberField(TEXT("actual_landing_capsule_radius_cm"),TestRadius);Row->SetNumberField(TEXT("actual_landing_capsule_half_height_cm"),TestHalf);Row->SetNumberField(TEXT("actual_same_layer_floor_delta_cm"),Floor.ImpactPoint.Z-LandingFloor.ImpactPoint.Z);Row->SetNumberField(TEXT("actual_original_shore_to_target_complete_path_cost"),Path->GetPathLength());Row->SetStringField(TEXT("actual_target_original_extent_nav_projection"),Projected.Location.ToString());Row->SetStringField(TEXT("tested_AI_actual_feet_unchanged"),TestedActor->GetNavAgentLocation().ToString());Row->SetNumberField(TEXT("fixture_target_repositions"),RouteTargetRepositions);Row->SetBoolField(TEXT("production_ignores_opponent_collision"),false);PlatformRouteEvidence.Add(MakeShared<FJsonValueObject>(Row));
        if(LandingClear)return true;
        Opponent->SetActorLocation(Previous);++RouteTargetRepositions;auto Restore=MakeShared<FJsonObject>();Restore->SetStringField(TEXT("event"),TEXT("AI_opponent_fixture_rejected_location_restored"));Restore->SetStringField(TEXT("reason"),TEXT("real_landing_capsule_remained_blocked"));Restore->SetStringField(TEXT("restored_actor_location"),Previous.ToString());Restore->SetNumberField(TEXT("fixture_target_repositions"),RouteTargetRepositions);PlatformRouteEvidence.Add(MakeShared<FJsonValueObject>(Restore));
    }
    return false;
}
bool AHeliosBioHotelQA::PlacePlatformAboardOpponentFixture(ACharacter* TestedActor,const TCHAR* StageName)
{
    auto* Opponent=Mode?Mode->Player.Get():nullptr;auto* Mesh=PhysicsPlatform?PhysicsPlatform->GetPlatformMesh():nullptr;
    if(!Opponent||!TestedActor||!Mesh)return false;
    const FVector TestedBefore=TestedActor->GetActorLocation(),Previous=Opponent->GetActorLocation();
    const float Radius=Opponent->GetCapsuleComponent()->GetScaledCapsuleRadius(),Half=Opponent->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FVector Center=Mesh->Bounds.Origin,Extent=Mesh->Bounds.BoxExtent;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BioQAActualPlatformOpponentFixture),false,Opponent);
    TArray<AActor*> Attached;Opponent->GetAttachedActors(Attached,true,true);Query.AddIgnoredActors(Attached);
    const FCollisionShape Shape=FCollisionShape::MakeCapsule(Radius,Half);
    // This is solely an opponent fixture. Native gravity must establish its real
    // platform base afterward; the tested AI and the moving route are untouched.
    for(float Y:{.55f,-.55f,0.f})for(float X:{.55f,-.55f,0.f})
    {
        const FVector Probe=Center+FVector(X*Extent.X,Y*Extent.Y,0);FHitResult Floor;
        if(!GetWorld()->LineTraceSingleByChannel(Floor,Probe+FVector(0,0,Extent.Z+100),Probe-FVector(0,0,Extent.Z+100),ECC_Visibility,Query)||Floor.GetComponent()!=Mesh||Floor.ImpactNormal.Z<Opponent->GetCharacterMovement()->GetWalkableFloorZ())continue;
        const FVector At=Floor.ImpactPoint+FVector(0,0,Half+2.f);
        if(GetWorld()->OverlapBlockingTestByChannel(At,FQuat::Identity,ECC_Pawn,Shape,Query))continue;
        Opponent->SetActorLocation(At);Opponent->GetCharacterMovement()->StopMovementImmediately();Opponent->GetCharacterMovement()->SetMovementMode(MOVE_Falling);++RouteTargetRepositions;
        bRouteTargetPlatformLandingPending=true;bRouteTargetAboardFixture=false;RouteTargetPlatformPlacedAt=FPlatformTime::Seconds();
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("event"),TEXT("AI_opponent_fixture_target_reposition"));Row->SetStringField(TEXT("stage"),StageName);Row->SetNumberField(TEXT("leg"),RouteLeg);Row->SetStringField(TEXT("opponent_previous_actor_location"),Previous.ToString());Row->SetStringField(TEXT("opponent_actual_actor_location"),Opponent->GetActorLocation().ToString());Row->SetStringField(TEXT("actual_original_platform_floor"),Floor.ImpactPoint.ToString());Row->SetStringField(TEXT("actual_original_platform_component"),GetPathNameSafe(Floor.GetComponent()));Row->SetBoolField(TEXT("opponent_capsule_collision_clear_before_placement"),true);Row->SetBoolField(TEXT("real_native_gravity_platform_landing_pending"),true);Row->SetBoolField(TEXT("tested_AI_actor_location_unchanged"),TestedBefore.Equals(TestedActor->GetActorLocation(),.001f));Row->SetStringField(TEXT("tested_AI_actual_actor_location"),TestedActor->GetActorLocation().ToString());Row->SetNumberField(TEXT("fixture_target_repositions"),RouteTargetRepositions);Row->SetBoolField(TEXT("production_path_cost_or_AI_motion_overridden"),false);PlatformRouteEvidence.Add(MakeShared<FJsonValueObject>(Row));
        return TestedBefore.Equals(TestedActor->GetActorLocation(),.001f);
    }
    return false;
}
bool AHeliosBioHotelQA::PlanPlatformRoute(ACharacter* Character,bool ForAI)
{
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());if(!Nav||!PhysicsPlatform)return false;
    auto* Mesh=PhysicsPlatform->GetPlatformMesh();const FVector Extent=Mesh->Bounds.BoxExtent,Offset=Mesh->Bounds.Origin-PhysicsPlatform->GetActorLocation();
    const float Radius=Character->GetCapsuleComponent()->GetScaledCapsuleRadius();TArray<TArray<FVector>> Shores;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BioQAIndependentRouteStaticFloor),false,Character);for(const auto& P:Mode->Model.GetParticipants())Query.AddIgnoredActor(Mode->ParticipantActor(P.Id));
    FCollisionObjectQueryParams StaticFloor(ECC_WorldStatic);
    TMap<FString,int32> Counts;TMap<FString,int32> DetailCounts;int32 RoutePointIndex=INDEX_NONE;
    auto Evidence=[&](const TCHAR* Site,const FString& Result,const FVector& From,const FVector& To,const FHitResult* Hit=nullptr)
    {
        const FString Key=FString(Site)+TEXT("/")+Result;++Counts.FindOrAdd(Key);
        const FString SiteName(Site);const bool AllSamples=SiteName==TEXT("source_route_shore")||SiteName.Contains(TEXT("continue_goal"));if(!AllSamples&&DetailCounts.FindOrAdd(Key)++>=4)return;
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("event"),TEXT("route_planning_probe"));Row->SetStringField(TEXT("actor"),Character->GetPathName());Row->SetBoolField(TEXT("AI"),ForAI);Row->SetNumberField(TEXT("source_route_point_index"),RoutePointIndex);Row->SetStringField(TEXT("site"),Site);Row->SetStringField(TEXT("result"),Result);Row->SetStringField(TEXT("wanted_or_path_start"),From.ToString());Row->SetStringField(TEXT("projected_or_path_end"),To.ToString());
        if(Hit){Row->SetStringField(TEXT("hit_actor"),GetNameSafe(Hit->GetActor()));Row->SetStringField(TEXT("hit_component"),GetNameSafe(Hit->GetComponent()));Row->SetStringField(TEXT("impact_point"),Hit->ImpactPoint.ToString());Row->SetStringField(TEXT("impact_normal"),Hit->ImpactNormal.ToString());Row->SetNumberField(TEXT("mobility"),Hit->GetComponent()?int32(Hit->GetComponent()->Mobility.GetValue()):INDEX_NONE);}
        PlatformRouteEvidence.Add(MakeShared<FJsonValueObject>(Row));
    };
    auto PlanRow=MakeShared<FJsonObject>();PlanRow->SetStringField(TEXT("event"),TEXT("route_planning_inputs"));PlanRow->SetStringField(TEXT("actor"),Character->GetPathName());PlanRow->SetBoolField(TEXT("AI"),ForAI);PlanRow->SetStringField(TEXT("actor_location"),Character->GetActorLocation().ToString());PlanRow->SetStringField(TEXT("nav_agent_location"),Character->GetNavAgentLocation().ToString());PlanRow->SetNumberField(TEXT("nav_agent_radius"),Character->GetNavAgentPropertiesRef().AgentRadius);PlanRow->SetNumberField(TEXT("nav_agent_height"),Character->GetNavAgentPropertiesRef().AgentHeight);PlanRow->SetStringField(TEXT("platform_actor_location"),PhysicsPlatform->GetActorLocation().ToString());PlanRow->SetStringField(TEXT("platform_bounds_origin"),Mesh->Bounds.Origin.ToString());PlanRow->SetStringField(TEXT("platform_bounds_extent"),Extent.ToString());PlanRow->SetStringField(TEXT("platform_bounds_offset"),Offset.ToString());PlanRow->SetStringField(TEXT("static_floor_query"),TEXT("ECC_WorldStatic object query: moving platform cannot mask actual static shore floor"));
    TArray<TSharedPtr<FJsonValue>> OriginalPoints;for(const auto& Point:PhysicsPlatform->GetRoutePoints())OriginalPoints.Add(MakeShared<FJsonValueString>(Point.ToString()));PlanRow->SetArrayField(TEXT("original_route_points"),OriginalPoints);PlatformRouteEvidence.Add(MakeShared<FJsonValueObject>(PlanRow));
    auto Floor=[&](const FVector& Wanted,FVector& Point,const TCHAR* Site)
    {
        FNavLocation Projected;if(!Nav->ProjectPointToNavigation(Wanted,Projected,FVector(70,70,180),&Character->GetNavAgentPropertiesRef())){Evidence(Site,TEXT("nav_projection_failed"),Wanted,FVector::ZeroVector);return false;}
        FHitResult Hit;
        // The candidate is an authored STATIC shore. A moving source platform
        // crossing this ray must not replace or hide the static floor result.
        const bool FloorHit=GetWorld()->LineTraceSingleByObjectType(Hit,Projected.Location+FVector(0,0,70),Projected.Location-FVector(0,0,180),StaticFloor,Query);
        if(!FloorHit||!Hit.GetComponent()){Evidence(Site,TEXT("static_floor_ray_missed"),Wanted,Projected.Location,&Hit);return false;}
        if(Hit.GetComponent()->Mobility!=EComponentMobility::Static){Evidence(Site,TEXT("static_object_mobility_rejected"),Wanted,Projected.Location,&Hit);return false;}
        if(Hit.ImpactNormal.Z<Character->GetCharacterMovement()->GetWalkableFloorZ()){Evidence(Site,TEXT("floor_slope_rejected"),Wanted,Projected.Location,&Hit);return false;}
        if(FMath::Abs(Hit.ImpactPoint.Z-Projected.Location.Z)>70){Evidence(Site,TEXT("floor_nav_height_mismatch"),Wanted,Projected.Location,&Hit);return false;}
        Evidence(Site,TEXT("static_nav_floor_accepted"),Wanted,Projected.Location,&Hit);
        Point=Hit.ImpactPoint;return true;
    };
    auto PathLength=[&](const FVector& From,const FVector& To,float& Length,const TCHAR* Site)
    {auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),From,To,Character);if(!Path){Evidence(Site,TEXT("path_missing"),From,To);return false;}if(!Path->IsValid()){Evidence(Site,TEXT("path_invalid"),From,To);return false;}if(Path->IsPartial()){Evidence(Site,TEXT("path_partial"),From,To);return false;}Evidence(Site,TEXT("complete_path_accepted"),From,To);Length=Path->GetPathLength();return true;};
    auto Continuation=[&](const FVector& Shore,const FVector& Away,FVector& Goal,float& Cost,const TCHAR* FloorSite,const TCHAR* PathSite)
    {
        const FVector First=Away.IsNearlyZero()?FVector::ForwardVector:Away.GetSafeNormal2D();
        // Keep the existing 480/360 AI search. Radius is only a search input:
        // additionally prove a real ground path after the last native drop.
        const float ContinuationRadius=ForAI?480.f:240.f;
        const float MinimumContinuationDistance=ForAI?360.f:140.f;
        for(int32 Sample=0;Sample<16;++Sample)
        {
            const float Angle=2*PI*Sample/16.f;
            const FVector Direction(First.X*FMath::Cos(Angle)-First.Y*FMath::Sin(Angle),First.X*FMath::Sin(Angle)+First.Y*FMath::Cos(Angle),0);
            const FVector Wanted=Shore+Direction*ContinuationRadius;FVector Candidate;
            auto Attempt=MakeShared<FJsonObject>();Attempt->SetStringField(TEXT("event"),TEXT("continuation_direction_sample"));Attempt->SetStringField(TEXT("actor"),Character->GetPathName());Attempt->SetBoolField(TEXT("AI"),ForAI);Attempt->SetStringField(TEXT("site"),FloorSite);Attempt->SetNumberField(TEXT("source_route_point_index"),RoutePointIndex);Attempt->SetNumberField(TEXT("direction_index_away_first"),Sample);Attempt->SetNumberField(TEXT("sample_radius_cm"),ContinuationRadius);Attempt->SetStringField(TEXT("real_static_shore"),Shore.ToString());Attempt->SetStringField(TEXT("wanted"),Wanted.ToString());
            // Continuation describes the physical floor at the radial XY, which
            // may be below a small shore. The authored shore probes above keep
            // their original height and original navigation search unchanged.
            FHitResult PhysicalFloor;
            const bool PhysicalHit=GetWorld()->LineTraceSingleByObjectType(PhysicalFloor,Wanted+FVector(0,0,70),Wanted-FVector(0,0,650),StaticFloor,Query);
            Attempt->SetStringField(TEXT("unchanged_nav_projection_extent"),FVector(70,70,180).ToString());Attempt->SetBoolField(TEXT("original_static_floor_hit"),PhysicalHit);
            if(!PhysicalHit||!PhysicalFloor.GetComponent()||PhysicalFloor.GetComponent()->Mobility!=EComponentMobility::Static||PhysicalFloor.ImpactNormal.Z<Character->GetCharacterMovement()->GetWalkableFloorZ())
            {Attempt->SetStringField(TEXT("result"),TEXT("radial_XY_has_no_original_walkable_static_floor"));PlatformRouteEvidence.Add(MakeShared<FJsonValueObject>(Attempt));continue;}
            Attempt->SetStringField(TEXT("original_static_floor_point"),PhysicalFloor.ImpactPoint.ToString());Attempt->SetStringField(TEXT("original_static_floor_normal"),PhysicalFloor.ImpactNormal.ToString());Attempt->SetStringField(TEXT("original_static_floor_component"),GetNameSafe(PhysicalFloor.GetComponent()));Attempt->SetNumberField(TEXT("physical_floor_height_change_cm"),PhysicalFloor.ImpactPoint.Z-Wanted.Z);
            if(!Floor(PhysicalFloor.ImpactPoint,Candidate,FloorSite)){Attempt->SetStringField(TEXT("result"),TEXT("rejected_by_actual_projection_or_static_floor_probe"));PlatformRouteEvidence.Add(MakeShared<FJsonValueObject>(Attempt));continue;}
            Attempt->SetStringField(TEXT("actual_projected_static_goal"),Candidate.ToString());Attempt->SetNumberField(TEXT("actual_shore_to_goal_cm"),FVector::Dist2D(Candidate,Shore));
            if(FVector::Dist2D(Candidate,Shore)<MinimumContinuationDistance){Evidence(FloorSite,TEXT("continuation_distance_below_required_minimum"),Shore,Candidate);Attempt->SetNumberField(TEXT("minimum_continuation_distance_cm"),MinimumContinuationDistance);Attempt->SetStringField(TEXT("result"),TEXT("actual_continuation_below_required_minimum"));PlatformRouteEvidence.Add(MakeShared<FJsonValueObject>(Attempt));continue;}
            if(!PathLength(Shore,Candidate,Cost,PathSite)){Attempt->SetStringField(TEXT("result"),TEXT("no_actual_complete_shore_to_goal_path"));PlatformRouteEvidence.Add(MakeShared<FJsonValueObject>(Attempt));continue;}
            if(ForAI)
            {
                auto* FullPath=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Shore,Candidate,Character);const FNavPathSharedPtr Native=FullPath?FullPath->GetPath():FNavPathSharedPtr();
                FVector GroundStart=Shore;int32 LastDropIndex=INDEX_NONE;FString LastDrop;
                if(Native.IsValid())
                {
                    TSet<FNavLinkId> CorridorLinks;if(const auto* Corridor=Native->CastPath<FNavMeshPath>())if(const auto* Recast=Cast<ARecastNavMesh>(Corridor->GetNavigationDataUsed()))for(NavNodeRef Ref:Corridor->PathCorridor){const FNavLinkId Id=Recast->GetNavLinkUserId(Ref);if(Id.IsValid())CorridorLinks.Add(Id);}
                    const auto& Points=Native->GetPathPoints();
                    for(int32 I=0;I+1<Points.Num();++I)for(TActorIterator<AHeliosBioStaticJumpLink> Link(GetWorld());Link;++Link)
                    {
                        if(!Link->GetSmartLinkComp()||(!CorridorLinks.Contains(Link->GetSmartLinkComp()->GetId())&&!Native->ContainsCustomLink(Link->GetSmartLinkComp()->GetId())))continue;
                        const FVector A=Link->GetStartWorldFeet(),B=Link->GetEndWorldFeet();const bool Forward=FVector::DistSquared(Points[I].Location,A)+FVector::DistSquared(Points[I+1].Location,B)<=FVector::DistSquared(Points[I].Location,B)+FVector::DistSquared(Points[I+1].Location,A);
                        const FVector From=Forward?A:B,To=Forward?B:A;
                        if(FVector::Dist(Points[I].Location,From)>45.f||FVector::Dist(Points[I+1].Location,To)>45.f||From.Z-To.Z<=Character->GetCharacterMovement()->MaxStepHeight)continue;
                        if(I>=LastDropIndex){LastDropIndex=I;GroundStart=To;LastDrop=Link->GetPathName();}
                    }
                }
                auto* GroundPath=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),GroundStart,Candidate,Character);bool GroundComplete=GroundPath&&GroundPath->IsValid()&&!GroundPath->IsPartial();
                if(GroundComplete)if(const auto GroundNative=GroundPath->GetPath();GroundNative.IsValid())
                {
                    for(const auto& Point:GroundNative->GetPathPoints())if(Point.CustomNavLinkId.IsValid()||FNavMeshNodeFlags(Point.Flags).IsNavLink())GroundComplete=false;
                    if(const auto* Corridor=GroundNative->CastPath<FNavMeshPath>())if(const auto* Recast=Cast<ARecastNavMesh>(Corridor->GetNavigationDataUsed()))for(NavNodeRef Ref:Corridor->PathCorridor)if(Recast->GetNavLinkUserId(Ref).IsValid())GroundComplete=false;
                }
                const auto* Participant=Mode->Model.FindParticipant(Mode->ParticipantId(Character));const float Standoff=55.f,Acceptance=Participant&&Participant->IsHunter()?65.f:35.f,IntentTracking=20.f,LandingMargin=40.f;
                const float Length=GroundComplete?GroundPath->GetPathLength():0.f,Remaining=Length-Standoff-Acceptance-IntentTracking-LandingMargin;
                Attempt->SetStringField(TEXT("real_last_static_drop_landing"),GroundStart.ToString());Attempt->SetStringField(TEXT("matched_source_drop_link"),LastDrop);Attempt->SetBoolField(TEXT("actual_drop_to_candidate_complete_ground_path"),GroundComplete);Attempt->SetNumberField(TEXT("actual_drop_to_candidate_ground_path_cm"),Length);Attempt->SetNumberField(TEXT("production_near_spread_standoff_cm"),Standoff);Attempt->SetNumberField(TEXT("production_chase_acceptance_cm"),Acceptance);Attempt->SetNumberField(TEXT("production_move_intent_tracking_tolerance_cm"),IntentTracking);Attempt->SetNumberField(TEXT("native_drop_landing_margin_cm"),LandingMargin);Attempt->SetNumberField(TEXT("remaining_ground_path_after_production_standoff_cm"),Remaining);
                if(!GroundComplete||Remaining<=100.f){Attempt->SetStringField(TEXT("result"),TEXT("insufficient_real_ground_continuation_after_production_standoff"));PlatformRouteEvidence.Add(MakeShared<FJsonValueObject>(Attempt));continue;}
            }
            Goal=Candidate;Attempt->SetStringField(TEXT("result"),TEXT("first_actual_static_floor_complete_path_goal_accepted"));Attempt->SetNumberField(TEXT("actual_complete_path_cost"),Cost);PlatformRouteEvidence.Add(MakeShared<FJsonValueObject>(Attempt));return true;
        }
        Evidence(FloorSite,TEXT("all_16_real_shore_directions_exhausted"),Shore,Shore);return false;
    };
    for(const FVector& Point:PhysicsPlatform->GetRoutePoints())
    {
        ++RoutePointIndex;auto& Set=Shores.AddDefaulted_GetRef();const FVector Center=Point+Offset;
        for(int32 I=0;I<16;++I)
        {
            const float Angle=2*PI*I/16;FVector Shore;const FVector Wanted=Center+FVector(FMath::Cos(Angle)*(Extent.X+Radius+90),FMath::Sin(Angle)*(Extent.Y+Radius+90),0);
            if(Floor(Wanted,Shore,TEXT("source_route_shore"))){if(FMath::Abs(Shore.X-Center.X)<=Extent.X&&FMath::Abs(Shore.Y-Center.Y)<=Extent.Y)Evidence(TEXT("source_route_shore"),TEXT("projected_inside_platform_bounds"),Wanted,Shore);else{Set.Add(Shore);++Counts.FindOrAdd(TEXT("shore_candidates_outside_platform"));}}
        }
    }
    float Best=MAX_flt;bool Found=false;
    for(int32 Start=0;Start<Shores.Num();++Start)for(const FVector& Entry:Shores[Start])
    {
        RoutePointIndex=Start;
        float Approach;if(!PathLength(Character->GetNavAgentLocation(),Entry,Approach,TEXT("spawn_to_entry")))continue;
        for(int32 End=0;End<Shores.Num();++End)if(Start!=End)for(const FVector& Exit:Shores[End])
        {
            RoutePointIndex=End;
            ++Counts.FindOrAdd(TEXT("shore_pairs_considered"));const float RouteDistance=FVector::Dist(PhysicsPlatform->GetRoutePoints()[Start],PhysicsPlatform->GetRoutePoints()[End]);if(RouteDistance<600){++Counts.FindOrAdd(TEXT("source_route_distance_below_600"));continue;}
            FVector Goal;const FVector Away=(Exit-(PhysicsPlatform->GetRoutePoints()[End]+Offset)).GetSafeNormal2D();
            float FinalCost;if(!Continuation(Exit,Away,Goal,FinalCost,TEXT("exit_continue_goal"),TEXT("exit_to_continue_goal")))continue;
            // AI first pursues the real high shore opponent. Only after a real
            // departure landing does the fixture move that opponent to Goal.
            float DirectCost;if(ForAI&&PathLength(Character->GetNavAgentLocation(),Exit,DirectCost,TEXT("spawn_direct_to_high_shore_opponent"))&&Approach+RouteDistance+250>=DirectCost){Evidence(TEXT("AI_route_cost"),TEXT("ground_route_preferred"),Entry,Exit);if(Counts.FindRef(TEXT("AI_route_cost/ground_route_preferred"))<=4){auto Cost=MakeShared<FJsonObject>();Cost->SetStringField(TEXT("event"),TEXT("AI_planned_route_cost_rejection"));Cost->SetStringField(TEXT("actor"),Character->GetPathName());Cost->SetStringField(TEXT("actual_opponent_target_stage"),TEXT("high_static_shore_before_departure"));Cost->SetStringField(TEXT("actual_opponent_target"),Exit.ToString());Cost->SetNumberField(TEXT("direct_ground_cost"),DirectCost);Cost->SetNumberField(TEXT("entry_path_cost"),Approach);Cost->SetNumberField(TEXT("source_route_distance"),RouteDistance);Cost->SetNumberField(TEXT("unchanged_production_cost_margin"),250);PlatformRouteEvidence.Add(MakeShared<FJsonValueObject>(Cost));}continue;}
            // Prefer a long authored crossing, but require the entry path to be real.
            const float Score=Approach-RouteDistance*.5f;if(Score>=Best)continue;
            FVector ReturnGoal;const FVector EntryAway=(Entry-(PhysicsPlatform->GetRoutePoints()[Start]+Offset)).GetSafeNormal2D();float ReturnCost;
            RoutePointIndex=Start;
            if(!Continuation(Entry,EntryAway,ReturnGoal,ReturnCost,TEXT("return_continue_goal"),TEXT("entry_to_return_continue_goal")))continue;
            Best=Score;RouteEntry=Entry;RouteExit=Exit;RouteGoal=Goal;RouteReturnGoal=ReturnGoal;RouteFixtureEntryShoreCandidates=Shores[Start];RouteFixtureExitShoreCandidates=Shores[End];Found=true;
        }
    }
    auto Summary=MakeShared<FJsonObject>();Summary->SetStringField(TEXT("event"),TEXT("route_planning_filter_summary"));Summary->SetStringField(TEXT("actor"),Character->GetPathName());Summary->SetBoolField(TEXT("AI"),ForAI);Summary->SetBoolField(TEXT("route_found"),Found);auto CountObject=MakeShared<FJsonObject>();for(const auto& Count:Counts)CountObject->SetNumberField(Count.Key,Count.Value);Summary->SetObjectField(TEXT("filters"),CountObject);PlatformRouteEvidence.Add(MakeShared<FJsonValueObject>(Summary));UE_LOG(LogTemp,Display,TEXT("BIO_PLATFORM_ROUTE_PLAN actor=%s AI=%d route_found=%d shores=%d pairs=%d"),*Character->GetName(),ForAI,Found,Counts.FindRef(TEXT("shore_candidates_outside_platform")),Counts.FindRef(TEXT("shore_pairs_considered")));
    if(!Found)return false;RouteSpawn=Character->GetActorLocation();RouteLastPosition=RouteSpawn;RouteWalkingDistance=0;RoutePostExitDistance=0;RouteStillSeconds=0;RouteLeg=0;RouteState=0;RouteDepartureWaitAt=0;bRouteWait=bRouteJump=bRouteCarry=bRouteExitJump=bRouteAIResume=false;
    const auto JumpBaseline=AHeliosBioStaticJumpLink::GetDiagnostics(Character);RouteStaticJumpAttemptsBefore=JumpBaseline.Attempts;RouteStaticJumpLandingsBefore=JumpBaseline.Landings;RouteStaticJumpFailuresBefore=JumpBaseline.Failures;
    if(!ForAI&&!SetPlatformWalkPath(Character,RouteEntry))return false;
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("event"),TEXT("normal_spawn_route_planned"));Row->SetStringField(TEXT("actor"),Character->GetPathName());Row->SetBoolField(TEXT("AI"),ForAI);Row->SetStringField(TEXT("actual_normal_spawn"),RouteSpawn.ToString());Row->SetStringField(TEXT("source_spawn_expected"),Mode->SpawnLocation(Mode->ParticipantId(Character)).ToString());Row->SetStringField(TEXT("entry_static_shore"),RouteEntry.ToString());Row->SetStringField(TEXT("exit_static_shore"),RouteExit.ToString());Row->SetStringField(TEXT("continue_goal"),RouteGoal.ToString());Row->SetBoolField(TEXT("complete_entry_path"),true);PlatformRouteEvidence.Add(MakeShared<FJsonValueObject>(Row));return true;
}
void AHeliosBioHotelQA::SamplePlatformRoute(double Now)
{
    ACharacter* Character=bPlatformRoutePlayer?static_cast<ACharacter*>(Mode->Player.Get()):static_cast<ACharacter*>(AIBoardBot.Get());if(!Character||Now<RouteSampleAt)return;RouteSampleAt=Now+.1;
    const FVector Position=Character->GetActorLocation();const float Progress=FVector::Dist2D(Position,RouteLastPosition);const double Dt=RouteLastAt?Now-RouteLastAt:0;RouteLastAt=Now;RouteLastPosition=Position;
    if(Character->GetCharacterMovement()->IsMovingOnGround()&&Character->GetMovementBase()!=PhysicsPlatform->GetPlatformMesh()){RouteWalkingDistance+=Progress;if(RouteState==5)RoutePostExitDistance+=Progress;}
    RouteStillSeconds=Progress<1?RouteStillSeconds+Dt:0;
    auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("wall_elapsed"),Now-Started);Row->SetStringField(TEXT("actor"),Character->GetPathName());Row->SetBoolField(TEXT("AI"),!bPlatformRoutePlayer);Row->SetNumberField(TEXT("leg"),RouteLeg);Row->SetNumberField(TEXT("fixture_state"),RouteState);Row->SetStringField(TEXT("position"),Position.ToString());Row->SetStringField(TEXT("velocity"),Character->GetVelocity().ToString());Row->SetStringField(TEXT("movement_base"),GetNameSafe(Character->GetMovementBase()));Row->SetBoolField(TEXT("source_platform_base"),Character->GetMovementBase()==PhysicsPlatform->GetPlatformMesh());Row->SetBoolField(TEXT("falling"),Character->GetCharacterMovement()->IsFalling());Row->SetNumberField(TEXT("actual_jump_Z_velocity"),Character->GetCharacterMovement()->JumpZVelocity);Row->SetNumberField(TEXT("actual_max_walk_speed"),Character->GetCharacterMovement()->MaxWalkSpeed);Row->SetNumberField(TEXT("ground_walking_cm"),RouteWalkingDistance);Row->SetNumberField(TEXT("post_exit_walking_cm"),RoutePostExitDistance);Row->SetNumberField(TEXT("still_seconds"),RouteStillSeconds);Row->SetStringField(TEXT("platform_position"),PhysicsPlatform->GetActorLocation().ToString());Row->SetStringField(TEXT("continue_goal"),RouteGoal.ToString());Row->SetNumberField(TEXT("production_boardings_at_leg_start"),RouteBoardingsBefore);
    const auto Jump=AHeliosBioStaticJumpLink::GetDiagnostics(Character);const int32 Attempts=FMath::Max(0,Jump.Attempts-RouteStaticJumpAttemptsBefore),Landings=FMath::Max(0,Jump.Landings-RouteStaticJumpLandingsBefore),Failed=FMath::Max(0,Jump.Failures-RouteStaticJumpFailuresBefore);
    if(bPlatformRoutePlayer){PlayerStaticJumpAttempts=Attempts;PlayerStaticJumpLandings=Landings;PlayerStaticJumpFailures=Failed;}else{AIStaticJumpAttempts=Attempts;AIStaticJumpLandings=Landings;AIStaticJumpFailures=Failed;}
    Row->SetBoolField(TEXT("registered_static_jump_owns_movement"),Jump.bTraversing);Row->SetStringField(TEXT("registered_static_jump_diagnostics"),AHeliosBioStaticJumpLink::DebugState(Character));Row->SetNumberField(TEXT("registered_static_jump_attempts"),Attempts);Row->SetNumberField(TEXT("registered_static_jump_landings"),Landings);Row->SetNumberField(TEXT("registered_static_jump_failures"),Failed);Row->SetBoolField(TEXT("registered_static_jump_native_jump_issued"),Jump.bJumpIssued);Row->SetBoolField(TEXT("registered_static_jump_actual_airborne_observed"),Jump.bAirborneObserved);
    if(bPlatformRoutePlayer){Row->SetNumberField(TEXT("complete_path_index"),RoutePathIndex);Row->SetNumberField(TEXT("complete_path_point_count"),PlatformWalkPath.Num());Row->SetBoolField(TEXT("complete_path_native_link_metadata_available"),bPlatformPathMetadataAvailable);Row->SetBoolField(TEXT("waiting_for_registered_static_jump_landing"),bRouteWalkJumpPending);if(PlatformWalkPath.IsValidIndex(RoutePathIndex)){const auto& Point=PlatformWalkPath[RoutePathIndex];Row->SetStringField(TEXT("complete_path_next_point"),Point.Location.ToString());Row->SetNumberField(TEXT("complete_path_next_flags"),Point.Flags);Row->SetStringField(TEXT("complete_path_next_custom_link_id"),FString::Printf(TEXT("%llu"),static_cast<unsigned long long>(Point.CustomNavLinkId.GetId())));}}
    if(!bPlatformRoutePlayer){const auto D=HeliosBioPlatformTraversal::GetDiagnostics(AIBoardBot);Row->SetStringField(TEXT("production_traversal_debug"),HeliosBioPlatformTraversal::DebugState(AIBoardBot));Row->SetNumberField(TEXT("production_step"),D.Step);Row->SetNumberField(TEXT("production_ground_seconds"),D.GroundSeconds);Row->SetNumberField(TEXT("production_route_seconds"),D.RouteSeconds);Row->SetNumberField(TEXT("ground_walk_seconds"),D.GroundWalkSeconds);Row->SetNumberField(TEXT("static_jump_seconds"),D.StaticJumpSeconds);Row->SetNumberField(TEXT("physical_wall_walk_seconds"),D.PhysicalWallWalkSeconds);Row->SetNumberField(TEXT("static_alignment_seconds"),D.StaticAlignmentSeconds);Row->SetNumberField(TEXT("ground_static_links"),D.GroundStaticLinks);Row->SetBoolField(TEXT("ground_timing_complete"),D.bGroundTimingComplete);Row->SetStringField(TEXT("ground_timing_source_segments"),D.GroundTimingSegments);Row->SetNumberField(TEXT("platform_approach_seconds"),D.ApproachSeconds);Row->SetNumberField(TEXT("platform_departure_ground_seconds"),D.DepartureSeconds);Row->SetNumberField(TEXT("platform_boarding_flight_seconds"),D.PlatformBoardingFlightSeconds);Row->SetNumberField(TEXT("platform_wait_seconds"),D.PlatformWaitSeconds);Row->SetNumberField(TEXT("platform_carry_seconds"),D.PlatformCarrySeconds);Row->SetNumberField(TEXT("platform_departure_flight_seconds"),D.PlatformDepartureFlightSeconds);Row->SetBoolField(TEXT("production_waiting"),D.bWaitingAtDock);Row->SetNumberField(TEXT("production_boardings"),D.Boardings);Row->SetNumberField(TEXT("production_departures"),D.Departures);Row->SetStringField(TEXT("movement_intent"),AIBoardBot->GetMovementIntent().ToString());Row->SetNumberField(TEXT("move_result"),AIBoardBot->GetLastMoveResult());Row->SetNumberField(TEXT("movement_recoveries"),AIBoardBot->GetMovementRecoveries());Row->SetStringField(TEXT("movement_debug"),AIBoardBot->GetMovementDiagnostic());Row->SetBoolField(TEXT("opponent_fixture_platform_landing_pending"),bRouteTargetPlatformLandingPending);Row->SetBoolField(TEXT("opponent_fixture_real_platform_base"),Mode->Player&&Mode->Player->GetMovementBase()==PhysicsPlatform->GetPlatformMesh()&&Mode->Player->GetCharacterMovement()->IsMovingOnGround());Row->SetNumberField(TEXT("opponent_fixture_platform_landings"),RouteTargetPlatformLandings);}
    FString Text;FJsonSerializer::Serialize(Row,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));Text+=TEXT("\n");FFileHelper::SaveStringToFile(Text,*(Out/TEXT("platform_route_samples.jsonl")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
}
void AHeliosBioHotelQA::TickPlatformRoute(double Now)
{
    if(Now-Started>1000){Check(TEXT("independent_platform_routes_wall_bound"),false);FinishSpecial(TEXT("FloatingPlatform"),TEXT("Independent routes exceeded bounded wall duration"));return;}
    auto BeginAI=[&]()
    {
        if(Mode->Player){Mode->Player->StopBioFire();if(AHeliosBioStaticJumpLink::IsTraversing(Mode->Player))AHeliosBioStaticJumpLink::Forget(Mode->Player);const auto D=AHeliosBioStaticJumpLink::GetDiagnostics(Mode->Player);PlayerStaticJumpAttempts=FMath::Max(0,D.Attempts-RouteStaticJumpAttemptsBefore);PlayerStaticJumpLandings=FMath::Max(0,D.Landings-RouteStaticJumpLandingsBefore);PlayerStaticJumpFailures=FMath::Max(0,D.Failures-RouteStaticJumpFailuresBefore);}
        bRouteWalkJumpPending=false;RouteWalkJumpLink.Reset();PlatformWalkBlocker.Reset();bRouteWeaponProbe=false;bPlatformRoutePlayer=false;Mode->RestartBioRound();for(auto B:Mode->Bots)B->bQAFreeze=true;Stage=3;Next=Now+.4;
    };
    auto FailRoute=[&](const FString& Reason)
    {
        RecordPlatformDiagnostics(bPlatformRoutePlayer?TEXT("player_route_blocked"):TEXT("AI_route_blocked"));
        Check(bPlatformRoutePlayer?TEXT("player_complete_independent_route"):TEXT("AI_complete_independent_route"),false);
        if(bPlatformRoutePlayer){PlayerRouteBlocker=Reason;BeginAI();}else{AIRouteBlocker=Reason;FinishSpecial(TEXT("FloatingPlatform"),Reason);}
    };
    if(Stage==90||Stage==91)
    {
        // Screenshot requests are consumed by the renderer after this tick.
        // Preserve the completed route scene until the actual PNG exists,
        // instead of restarting the player or exiting before its render frame.
        const bool PlayerImage=Stage==90;const FString Name=PlayerImage?TEXT("platform_player_two_round_trips.png"):TEXT("platform_AI_two_round_trips.png");
        if(IFileManager::Get().FileSize(*(Out/Name))>=1024){if(PlayerImage)BeginAI();else FinishSpecial(TEXT("FloatingPlatform"));return;}
        if(Now-RouteStateAt>5){Check(TEXT("completed_platform_route_actual_GPU_screenshot_written"),false);FinishSpecial(TEXT("FloatingPlatform"),TEXT("Completed physical route produced no bounded GPU screenshot"));}return;
    }
    if(Stage==0)
    {
        if(Now<Next)return;Mode=AHeliosBioHotelGameMode::Find(GetWorld());if(!Mode||!Mode->Player){FinishSpecial(TEXT("FloatingPlatform"),TEXT("Actual mode/player missing"));return;}
        Check(TEXT("platform_expected_map"),GetWorld()->GetOutermost()->GetName()==ExpectedMap);for(auto Platform:Mode->MovingPlatforms)if(IsValid(Platform)&&Platform->IsRouteConfigured()&&Platform->GetPlatformMesh()->GetStaticMesh()){PhysicsPlatform=Platform;break;}
        if(!PhysicsPlatform){FinishSpecial(TEXT("FloatingPlatform"),TEXT("Original configured platform absent"));return;}
        // Isolated long fixture uses the ordinary restart and unchanged source route.
        Mode->Model.Config.RoundSeconds=1200;Mode->Model.Config.PreparationSeconds=.2f;Mode->Model.Config.bKeepLocalPlayerHuman=true;Mode->RestartBioRound();for(auto B:Mode->Bots)B->bQAFreeze=true;Stage=1;Next=Now+.4;return;
    }
    if((Stage==1||Stage==3)&&Now>=Next)
    {
        if(Mode->Model.GetPhase()!=EHeliosBioPhase::Active){Next=Now+.1;return;}
        auto NormalSpawn=[&](ACharacter* Character,int32 Participant,const TCHAR* CheckName)
        {
            const FVector Expected=Mode->SpawnLocation(Participant),Actual=Character->GetActorLocation();const float Half=Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();const FVector Bottom=Actual-FVector(0,0,Half);FFindFloorResult Native;Character->GetCharacterMovement()->FindFloor(Actual,Native,false);
            const float ExpectedGroundZ=Expected.Z-Mode->Model.Config.MovementCapsuleHalfHeight-2.f,Gap=Bottom.Z-Native.HitResult.ImpactPoint.Z;
            const bool Passed=FVector::Dist(Actual,Expected)<5.f&&Character->GetCharacterMovement()->IsMovingOnGround()&&Native.IsWalkableFloor()&&FMath::Abs(Native.HitResult.ImpactPoint.Z-ExpectedGroundZ)<1.f&&FMath::Abs(Gap-2.f)<1.f;
            auto E=MakeShared<FJsonObject>();E->SetStringField(TEXT("event"),TEXT("actual_normal_restart_spawn_and_native_support"));E->SetStringField(TEXT("actor"),Character->GetPathName());E->SetNumberField(TEXT("participant"),Participant);E->SetNumberField(TEXT("round_serial"),Mode->RoundSerial);E->SetStringField(TEXT("source_spawn_expected_actor_location"),Expected.ToString());E->SetStringField(TEXT("actual_actor_location"),Actual.ToString());E->SetStringField(TEXT("actual_capsule_feet"),Bottom.ToString());E->SetNumberField(TEXT("actual_capsule_half_height_cm"),Half);E->SetNumberField(TEXT("actual_actor_source_spawn_error_3D_cm"),FVector::Dist(Actual,Expected));E->SetNumberField(TEXT("expected_source_static_floor_Z"),ExpectedGroundZ);E->SetBoolField(TEXT("actual_native_grounded"),Character->GetCharacterMovement()->IsMovingOnGround());E->SetBoolField(TEXT("actual_native_floor_walkable"),Native.IsWalkableFloor());E->SetStringField(TEXT("actual_native_floor_component"),GetPathNameSafe(Native.HitResult.GetComponent()));E->SetStringField(TEXT("actual_native_floor_point"),Native.HitResult.ImpactPoint.ToString());E->SetNumberField(TEXT("actual_capsule_bottom_floor_gap_cm"),Gap);E->SetNumberField(TEXT("expected_normal_native_floor_gap_cm"),2);E->SetBoolField(TEXT("tested_actor_fixture_teleport_used"),false);E->SetBoolField(TEXT("passed"),Passed);PlatformRouteEvidence.Add(MakeShared<FJsonValueObject>(E));Check(CheckName,Passed);return Passed;
        };
        const int32 FixtureMother=ZombieId();
        for(auto B:Mode->Bots)
        {
            B->bQAFreeze=true;Mode->Model.Infect(B->ParticipantId,FixtureMother);
            // Explicit fixture health/evolution/CD overrides keep supplies and death
            // from replacing the route task; character motion remains production code.
            if(auto* P=const_cast<FHeliosBioParticipant*>(Mode->Model.FindParticipant(B->ParticipantId)))
            {P->bMother=false;P->EvolutionLevel=Mode->Model.Config.MaximumZombieEvolution;P->SkillCooldownRemaining=1200;P->SecondaryCooldownRemaining=1200;P->Health=P->MaxHealth=1000000;}
        }
        for(auto Supply:Mode->Supplies)if(IsValid(Supply))Supply->SetAvailable(false);
        if(Stage==1)
        {
            if(!PlanPlatformRoute(Mode->Player,false)){FailRoute(TEXT("No actual complete normal-spawn walk path to legal original route shore pair"));return;}
            RouteNativeZeus=Cast<AHeliosChanZeusWeapon>(Mode->Player->GetEquippedWeapon());
            if(auto* Native=RouteNativeZeus.Get())Native->OnNativeImpact.AddWeakLambda(this,[this](const FHeliosZeusNativeImpact& Impact)
            {
                auto* Source=RouteNativeZeus.Get();
                if(!bRouteWeaponProbe||!Source||Impact.SourceWeapon.Get()!=Source||Mode->Player->GetEquippedWeapon()!=Source||Impact.ShotSerial<=RouteNativeShotsBefore||Impact.ShotSerial!=Source->ShotsFired+1||!Impact.Hit.bBlockingHit||Impact.Hit.GetActor()!=PhysicsPlatform||Impact.Hit.GetComponent()!=PhysicsPlatform->GetPlatformMesh())return;
                ++PlatformNativeImpacts;PlatformNativeLastShotSerial=Impact.ShotSerial;
                auto E=MakeShared<FJsonObject>();E->SetStringField(TEXT("event"),TEXT("actual_native_Zeus_pellet_trace_hit_original_platform"));E->SetStringField(TEXT("source_weapon"),Source->GetPathName());E->SetNumberField(TEXT("native_shot_serial_pending_commit"),Impact.ShotSerial);E->SetNumberField(TEXT("actual_pellet_index"),Impact.PelletIndex);E->SetStringField(TEXT("actual_view"),Impact.View.ToString());E->SetStringField(TEXT("actual_native_muzzle"),Impact.Muzzle.ToString());E->SetStringField(TEXT("actual_pellet_direction"),Impact.Direction.ToString());E->SetBoolField(TEXT("actual_view_to_muzzle_obstructed"),Impact.bViewToMuzzleObstructed);E->SetStringField(TEXT("actual_hit_actor"),GetNameSafe(Impact.Hit.GetActor()));E->SetStringField(TEXT("actual_hit_component"),GetPathNameSafe(Impact.Hit.GetComponent()));E->SetStringField(TEXT("actual_hit_point"),Impact.Hit.ImpactPoint.ToString());E->SetStringField(TEXT("actual_hit_normal"),Impact.Hit.ImpactNormal.ToString());PlatformRouteEvidence.Add(MakeShared<FJsonValueObject>(E));
            });
            if(auto* Weapon=Mode->Player->GetEquippedWeapon())
            {
                const TWeakObjectPtr<AShooterWeapon> SourceWeapon=Weapon;Weapon->OnProjectilePresented.AddWeakLambda(this,[this,SourceWeapon](AShooterProjectile* Projectile)
                {
                    if(!bRouteWeaponProbe||!Projectile||!SourceWeapon.IsValid()||Mode->Player->GetEquippedWeapon()!=SourceWeapon.Get()||Projectile->GetInstigator()!=Mode->Player||Projectile->GetOwner()!=Mode->Player)return;
                    const TWeakObjectPtr<AShooterProjectile> SourceProjectile=Projectile;const int32 PendingShot=Mode->Player->ShotsFired+1,WindowShots=ShotsBefore;
                    Projectile->OnPresentationImpact.AddWeakLambda(this,[this,SourceWeapon,SourceProjectile,PendingShot,WindowShots](const FHitResult& Hit)
                    {
                        if(!bRouteWeaponProbe||WindowShots!=ShotsBefore||!SourceWeapon.IsValid()||!SourceProjectile.IsValid()||Mode->Player->GetEquippedWeapon()!=SourceWeapon.Get()||SourceProjectile->GetInstigator()!=Mode->Player||SourceProjectile->GetOwner()!=Mode->Player||PendingShot<=ShotsBefore||!Hit.bBlockingHit||Hit.GetActor()!=PhysicsPlatform||Hit.GetComponent()!=PhysicsPlatform->GetPlatformMesh())return;
                        ++PlatformProjectileImpacts;PlatformProjectileLastPendingPlayerShot=PendingShot;auto E=MakeShared<FJsonObject>();E->SetStringField(TEXT("event"),TEXT("actual_production_projectile_hit_original_platform"));E->SetStringField(TEXT("actual_source_weapon"),GetPathNameSafe(SourceWeapon.Get()));E->SetStringField(TEXT("actual_source_projectile"),GetPathNameSafe(SourceProjectile.Get()));E->SetNumberField(TEXT("pending_player_shot_serial"),PendingShot);E->SetBoolField(TEXT("source_projectile_owner_and_instigator_match_player"),true);E->SetStringField(TEXT("actor"),GetNameSafe(Hit.GetActor()));E->SetStringField(TEXT("component"),GetPathNameSafe(Hit.GetComponent()));E->SetStringField(TEXT("impact_point"),Hit.ImpactPoint.ToString());PlatformRouteEvidence.Add(MakeShared<FJsonValueObject>(E));
                    });
                });
            }
            if(!NormalSpawn(Mode->Player,0,TEXT("player_route_begins_at_normal_spawn"))){FailRoute(TEXT("Player did not begin at the full 3D normal restart spawn with original native floor support"));return;}Stage=2;
        }
        else
        {
            for(auto B:Mode->Bots)if(PlanPlatformRoute(B,true)){AIBoardBot=B;break;}
            if(!AIBoardBot){FailRoute(TEXT("No normal NPC spawn with complete entry path and production-beneficial original platform crossing"));return;}
            Zombie=AIBoardBot->ParticipantId;if(!Mode->TransformParticipant(0)){FailRoute(TEXT("Isolated AI target could not become the normal infection-resistant ghost hunter"));return;}if(auto* P=const_cast<FHeliosBioParticipant*>(Mode->Model.FindParticipant(0)))P->Health=P->MaxHealth=1000000;
            // The opponent placement is an explicit target fixture. The tested AI
            // remains at its real spawn and receives no QA movement command.
            // Ground pursuit can now legitimately beat a ferry. An actual opponent
            // aboard provides an unambiguous production reason for this capability test.
            if(!PlacePlatformAboardOpponentFixture(AIBoardBot,TEXT("original_platform_opponent_before_first_boarding_real_base_pending"))){FailRoute(TEXT("No collision-clear original-platform opponent fixture for first boarding"));return;}
            if(!NormalSpawn(AIBoardBot,Zombie,TEXT("AI_route_begins_at_normal_spawn"))){FailRoute(TEXT("AI did not begin at the full 3D normal restart spawn with original native floor support"));return;}RouteBoardingsBefore=RouteDeparturesBefore=0;AIBoardBot->bQAFreeze=false;Stage=4;
        }
        RouteStateAt=RouteLegAt=Now;RouteLastAt=0;RouteSampleAt=0;return;
    }
    if(Stage!=2&&Stage!=4)return;
    ACharacter* Character=bPlatformRoutePlayer?static_cast<ACharacter*>(Mode->Player.Get()):static_cast<ACharacter*>(AIBoardBot.Get());if(!IsValid(Character)){FailRoute(TEXT("Actual route actor disappeared"));return;}
    if(Mode->Model.GetPhase()!=EHeliosBioPhase::Active){FailRoute(TEXT("Fixture round ended before all crossings"));return;}
    if(Now-RouteLegAt>115){FailRoute(FString::Printf(TEXT("Leg %d timed out; state=%d position=%s helper=%s"),RouteLeg,RouteState,*Character->GetActorLocation().ToString(),bPlatformRoutePlayer?TEXT("real_player_input"):*HeliosBioPlatformTraversal::DebugState(AIBoardBot)));return;}
    SamplePlatformRoute(Now);auto* Movement=Character->GetCharacterMovement();const bool Based=Character->GetMovementBase()==PhysicsPlatform->GetPlatformMesh()&&Movement->IsMovingOnGround();
    auto SetState=[&](int32 State){RouteState=State;RouteStateAt=Now;RouteStillSeconds=0;};
    auto Steer=[&](const FVector& Goal,const FVector& LandingVelocity=FVector::ZeroVector)
    {
        if(AHeliosBioStaticJumpLink::IsTraversing(Character))return;
        if(Movement->IsFalling())
        {
            // Same native air-control controller as production static jumps.
            // Full directional input until overshoot cannot stop on small shores.
            const FVector Input=AHeliosBioStaticJumpLink::NativeAirInputToLanding(Character,Goal,LandingVelocity,GetWorld()->GetDeltaSeconds());
            Character->AddMovementInput(Input.GetSafeNormal2D(),FMath::Min(1.f,Input.Size2D()));
        }
        else Character->AddMovementInput((Goal-Character->GetActorLocation()).GetSafeNormal2D(),1.f);
    };
    auto Walk=[&](){return AdvancePlatformWalkPath(Character);};
    FVector Top;auto* Mesh=PhysicsPlatform->GetPlatformMesh();FHitResult TopHit;FCollisionQueryParams TopQuery(SCENE_QUERY_STAT(BioQAPlayerPlatformActualTop),false,Character);for(const auto& P:Mode->Model.GetParticipants())TopQuery.AddIgnoredActor(Mode->ParticipantActor(P.Id));
    const FVector Probe=Mesh->Bounds.Origin;const bool HasTop=GetWorld()->LineTraceSingleByChannel(TopHit,Probe+FVector(0,0,Mesh->Bounds.BoxExtent.Z+150),Probe-FVector(0,0,Mesh->Bounds.BoxExtent.Z+150),ECC_Visibility,TopQuery)&&TopHit.GetComponent()==Mesh&&TopHit.ImpactNormal.Z>=Movement->GetWalkableFloorZ();Top=HasTop?TopHit.ImpactPoint:Probe;
    if(bPlatformRoutePlayer)
    {
        if(RouteState==0){if(Walk()){if(RouteLeg==0){const bool Walked=RouteWalkingDistance>0&&Character->GetCharacterMovement()->IsMovingOnGround()&&!Based&&!bRouteWalkJumpPending&&!AHeliosBioStaticJumpLink::IsTraversing(Character);Check(TEXT("player_normal_spawn_to_original_shore_complete_path_actually_walked"),Walked);auto E=MakeShared<FJsonObject>();E->SetStringField(TEXT("event"),TEXT("player_normal_spawn_to_original_shore_path_completed_before_boarding"));E->SetStringField(TEXT("actual_normal_spawn"),RouteSpawn.ToString());E->SetStringField(TEXT("actual_original_shore_feet"),Character->GetNavAgentLocation().ToString());E->SetStringField(TEXT("original_entry_shore"),RouteEntry.ToString());E->SetNumberField(TEXT("actual_ground_walking_cm"),RouteWalkingDistance);E->SetNumberField(TEXT("completed_native_path_points"),RoutePathIndex);E->SetNumberField(TEXT("native_path_point_count"),PlatformWalkPath.Num());E->SetBoolField(TEXT("tested_player_fixture_teleport_used"),false);E->SetBoolField(TEXT("passed"),Walked);PlatformRouteEvidence.Add(MakeShared<FJsonValueObject>(E));if(!Walked){FailRoute(TEXT("Complete spawn-to-shore path did not produce actual grounded player walking"));return;}}SetState(1);Character->ConsumeMovementInputVector();}}
        else if(RouteState==1)
        {
            Character->ConsumeMovementInputVector();bRouteWait|=Now-RouteStateAt>.4;
            const float Gravity=-Movement->GetGravityZ(),Height=Top.Z-Character->GetNavAgentLocation().Z,Discriminant=FMath::Square(Movement->JumpZVelocity)-2*Gravity*Height;
            if(HasTop&&Gravity>0&&Discriminant>=0&&bRouteWait)
            {const float Flight=(Movement->JumpZVelocity+FMath::Sqrt(Discriminant))/Gravity;const FVector Predicted=Top+Mesh->GetComponentVelocity()*Flight;if(FVector::Dist2D(Character->GetActorLocation(),Predicted)<Movement->MaxWalkSpeed*Flight*.85f&&FVector::Dist2D(Character->GetActorLocation(),Top)<260){Steer(Predicted);Character->Jump();SetState(2);}}
        }
        else if(RouteState==2)
        {
            bRouteJump|=Movement->IsFalling();if(!Based)Steer(Top+FVector(0,0,2),Mesh->GetComponentVelocity());
            if(Based){Character->StopJumping();Character->ConsumeMovementInputVector();RouteRideRelative=Character->GetActorLocation()-PhysicsPlatform->GetActorLocation();RouteRidePlatform=PhysicsPlatform->GetActorLocation();RouteRideActor=Character->GetActorLocation();SetState(3);}
            else if(Now-RouteStateAt>3){FailRoute(TEXT("Actual player Jump did not land on original platform; no launch/teleport retry"));return;}
        }
        else if(RouteState==3)
        {
            Character->ConsumeMovementInputVector();if(!Based){FailRoute(TEXT("Player lost original movement base while riding"));return;}
            bRouteCarry|=FVector::Dist(RouteRidePlatform,PhysicsPlatform->GetActorLocation())>150&&FVector::Dist(RouteRideActor,Character->GetActorLocation())>150&&FVector::Dist(RouteRideRelative,Character->GetActorLocation()-PhysicsPlatform->GetActorLocation())<100;
            if(bRouteCarry&&FVector::Dist2D(Character->GetActorLocation(),RouteExit)<240){Steer(RouteExit);Character->Jump();SetState(4);}
        }
        else if(RouteState==4)
        {
            bRouteExitJump|=Movement->IsFalling();Steer(RouteExit+FVector(0,0,2));
            if(Movement->IsMovingOnGround()&&!Based){Character->StopJumping();if(!Character->GetMovementBase()||Character->GetMovementBase()->Mobility!=EComponentMobility::Static||FVector::Dist2D(Character->GetNavAgentLocation(),RouteExit)>=165.f||FMath::Abs(Character->GetNavAgentLocation().Z-RouteExit.Z)>=20.f||!SetPlatformWalkPath(Character,RouteGoal)){FailRoute(TEXT("Player departure did not land on the actual same-layer static shore with a complete continuation path"));return;}RoutePostExitDistance=0;SetState(5);}
            else if(Now-RouteStateAt>3){FailRoute(TEXT("Actual player Jump failed to leave the source platform for static shore"));return;}
        }
        else if(RouteState==5)Walk();
        if(!PlatformWalkBlocker.IsEmpty()){FailRoute(PlatformWalkBlocker);return;}
        // The obstruction test consumes ammo through the real equipped firearm;
        // a visibility precondition alone never satisfies this check.
        if(!bProductionPlatformWeaponBlocked&&!bRouteWeaponProbe&&Mode->Player->GetEquippedWeapon())for(auto B:Mode->Bots)
        {
            FVector Eye;FRotator Aim;Mode->Player->GetActorEyesViewPoint(Eye,Aim);const FVector Target=B->GetActorLocation()+FVector(0,0,25);FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(BioQAProductionPlatformWeapon),true,Mode->Player);Q.AddIgnoredActor(Mode->Player->GetEquippedWeapon());
            if(GetWorld()->LineTraceSingleByChannel(Hit,Eye,Target,ECC_Visibility,Q)&&Hit.GetActor()==PhysicsPlatform&&Mode->AreOpponents(0,B->ParticipantId)){Zombie=B->ParticipantId;HealthBefore=Mode->Model.FindParticipant(Zombie)->Health;AmmoBefore=Mode->Player->GetAmmo();ShotsBefore=Mode->Player->ShotsFired;RouteProjectileImpactsBefore=PlatformProjectileImpacts;RouteNativeImpactsBefore=PlatformNativeImpacts;RouteNativeShotsBefore=RouteNativeZeus.IsValid()?RouteNativeZeus->ShotsFired:0;bRouteWeaponProbe=true;bRouteWeaponAllBlocked=true;Mode->Player->GetController()->SetControlRotation((Target-Eye).Rotation());Mode->Player->StartBioFire();RouteWeaponAt=Now;break;}
        }
        if(bRouteWeaponProbe)
        {
            FVector Eye;FRotator Aim;Mode->Player->GetActorEyesViewPoint(Eye,Aim);auto* VictimActor=Mode->ParticipantActor(Zombie);const FVector Goal=VictimActor?VictimActor->GetActorLocation()+FVector(0,0,25):Eye;FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(BioQAWeaponObstructionThroughoutFire),true,Mode->Player);Q.AddIgnoredActor(Mode->Player->GetEquippedWeapon());bRouteWeaponAllBlocked&=VictimActor&&GetWorld()->LineTraceSingleByChannel(Hit,Eye,Goal,ECC_Visibility,Q)&&Hit.GetActor()==PhysicsPlatform;Mode->Player->GetController()->SetControlRotation((Goal-Eye).Rotation());
            const bool NativeCommitted=!RouteNativeZeus.IsValid()||RouteNativeZeus->ShotsFired>RouteNativeShotsBefore;
            const bool ActualShotCommitted=NativeCommitted&&Mode->Player->ShotsFired>ShotsBefore&&Mode->Player->GetAmmo()<AmmoBefore;
            // An aim ray is only a probe opportunity. Wait for the real source
            // shot/ammo commit; an empty or non-platform-hit window is rejected,
            // and never turns into evidence that the firearm was obstructed.
            if(Now-RouteWeaponAt>.35&&(ActualShotCommitted||Now-RouteWeaponAt>2.0))
            {
                Mode->Player->StopBioFire();const auto* Victim=Mode->Model.FindParticipant(Zombie);const auto* Native=RouteNativeZeus.Get();const int32 NativeAfter=Native?Native->ShotsFired:0;
                const bool ActualNativeImpact=Native&&Mode->Player->GetEquippedWeapon()==Native&&NativeAfter>RouteNativeShotsBefore&&PlatformNativeImpacts>RouteNativeImpactsBefore&&PlatformNativeLastShotSerial>RouteNativeShotsBefore&&PlatformNativeLastShotSerial<=NativeAfter;
                const bool ActualProjectileImpact=PlatformProjectileImpacts>RouteProjectileImpactsBefore&&PlatformProjectileLastPendingPlayerShot>ShotsBefore&&PlatformProjectileLastPendingPlayerShot<=Mode->Player->ShotsFired;
                bProductionPlatformWeaponBlocked=bRouteWeaponAllBlocked&&(ActualNativeImpact||ActualProjectileImpact)&&Victim&&Mode->Player->GetAmmo()<AmmoBefore&&Mode->Player->ShotsFired>ShotsBefore&&FMath::IsNearlyEqual(Victim->Health,HealthBefore);
                if(!Native&&!ActualProjectileImpact)PlatformWeaponEvidenceBlocker=TEXT("No actual AShooterProjectile platform impact observed; native weapon overrides other than Zeus are not instrumented by this fixture.");else if(Native&&!ActualNativeImpact)PlatformWeaponEvidenceBlocker=TEXT("No native Zeus platform hit matched a committed source shot in this probe window.");else PlatformWeaponEvidenceBlocker.Reset();
                if(bProductionPlatformWeaponBlocked)Check(TEXT("actual_firearm_platform_obstruction_ammo_and_HP"),true);auto E=MakeShared<FJsonObject>();E->SetStringField(TEXT("event"),TEXT("production_firearm_obstruction"));E->SetStringField(TEXT("candidate_window_result"),bProductionPlatformWeaponBlocked?TEXT("accepted_real_committed_shot_and_original_platform_impact"):TEXT("rejected_candidate_no_obstruction_acceptance"));E->SetBoolField(TEXT("real_shot_ammo_committed"),ActualShotCommitted);E->SetBoolField(TEXT("platform_blocked_all_sampled_aim_rays"),bRouteWeaponAllBlocked);E->SetBoolField(TEXT("accepted"),bProductionPlatformWeaponBlocked);E->SetStringField(TEXT("actual_weapon_class"),GetNameSafe(Mode->Player->GetEquippedWeapon()?Mode->Player->GetEquippedWeapon()->GetClass():nullptr));E->SetStringField(TEXT("actual_impact_mechanism"),Native?TEXT("original_Zeus_native_pellet_trace"):TEXT("actual_AShooterProjectile_impact_or_uninstrumented_override"));E->SetNumberField(TEXT("actual_original_platform_native_impacts"),PlatformNativeImpacts-RouteNativeImpactsBefore);E->SetNumberField(TEXT("native_shots_before"),RouteNativeShotsBefore);E->SetNumberField(TEXT("native_shots_after"),NativeAfter);E->SetNumberField(TEXT("last_actual_platform_native_shot_serial"),PlatformNativeLastShotSerial);E->SetBoolField(TEXT("native_hit_matches_committed_source_shot"),ActualNativeImpact);E->SetStringField(TEXT("native_impact_blocker"),PlatformWeaponEvidenceBlocker);E->SetNumberField(TEXT("actual_original_platform_projectile_impacts"),PlatformProjectileImpacts-RouteProjectileImpactsBefore);E->SetNumberField(TEXT("shots_before"),ShotsBefore);E->SetNumberField(TEXT("shots_after"),Mode->Player->ShotsFired);E->SetNumberField(TEXT("ammo_before"),AmmoBefore);E->SetNumberField(TEXT("ammo_after"),Mode->Player->GetAmmo());E->SetNumberField(TEXT("victim_HP_before"),HealthBefore);E->SetNumberField(TEXT("victim_HP_after"),Victim?Victim->Health:-1);PlatformRouteEvidence.Add(MakeShared<FJsonValueObject>(E));bRouteWeaponProbe=false;
            }
        }
        if((RouteState==0||RouteState==5)&&RouteStillSeconds>8&&!Movement->IsFalling()){FailRoute(FString::Printf(TEXT("Player complete nav walking path stalled over 8s at %s waypoint %d"),*Character->GetActorLocation().ToString(),RoutePathIndex));return;}
    }
    else
    {
        if(bRouteTargetPlatformLandingPending)
        {
            const bool Landed=Mode->Player->GetMovementBase()==PhysicsPlatform->GetPlatformMesh()&&Mode->Player->GetCharacterMovement()->IsMovingOnGround();
            if(Landed)
            {
                bRouteTargetPlatformLandingPending=false;bRouteTargetAboardFixture=true;++RouteTargetPlatformLandings;
                auto E=MakeShared<FJsonObject>();E->SetStringField(TEXT("event"),TEXT("AI_opponent_fixture_actual_native_platform_landing"));E->SetNumberField(TEXT("leg"),RouteLeg);E->SetStringField(TEXT("actual_opponent_feet"),Mode->Player->GetNavAgentLocation().ToString());E->SetStringField(TEXT("actual_original_movement_base"),GetPathNameSafe(Mode->Player->GetMovementBase()));E->SetBoolField(TEXT("actual_native_grounded_platform_base"),true);E->SetNumberField(TEXT("fixture_platform_landings"),RouteTargetPlatformLandings);E->SetStringField(TEXT("tested_AI_actual_feet"),Character->GetNavAgentLocation().ToString());E->SetBoolField(TEXT("tested_AI_movement_forced"),false);PlatformRouteEvidence.Add(MakeShared<FJsonValueObject>(E));
            }
            else if(Now-RouteTargetPlatformPlacedAt>2.0){FailRoute(TEXT("Explicit opponent fixture failed real native landing on original platform; tested AI was not repositioned"));return;}
        }
        const auto D=HeliosBioPlatformTraversal::GetDiagnostics(AIBoardBot);bRouteWait|=D.Step==5;bRouteJump|=D.Step==1&&Movement->IsFalling();bRouteExitJump|=D.Step==3&&Movement->IsFalling();
        if(Based&&bRouteCarry&&D.Reason.Contains(TEXT("all_jump_candidates_rejected"))&&RouteDepartureWaitAt<=0)RouteDepartureWaitAt=Now;
        if(RouteDepartureWaitAt>0&&Now-RouteDepartureWaitAt>20&&D.Departures<=RouteDeparturesBefore){FailRoute(FString::Printf(TEXT("Actual production departure did not complete within 20 seconds after a rejected real capsule preflight: %s"),*HeliosBioPlatformTraversal::DebugState(AIBoardBot)));return;}
        if(Based&&RouteState<3){RouteRideRelative=Character->GetActorLocation()-PhysicsPlatform->GetActorLocation();RouteRidePlatform=PhysicsPlatform->GetActorLocation();RouteRideActor=Character->GetActorLocation();SetState(3);}
        if(Based)bRouteCarry|=FVector::Dist(RouteRidePlatform,PhysicsPlatform->GetActorLocation())>150&&FVector::Dist(RouteRideActor,Character->GetActorLocation())>150&&FVector::Dist(RouteRideRelative,Character->GetActorLocation()-PhysicsPlatform->GetActorLocation())<150;
        if(bRouteTargetAboardFixture&&Based&&bRouteCarry)
        {
            if(!PlacePlatformOpponentFixture(Character,TEXT("supported_high_exit_opponent_after_actual_AI_boarding_and_carry"))){FailRoute(TEXT("No supported high exit opponent fixture after actual return boarding and carry"));return;}
            bRouteTargetAboardFixture=false;
        }
        if(bRouteCarry&&D.Departures>RouteDeparturesBefore&&Movement->IsMovingOnGround()&&!Based&&RouteState!=5&&Character->GetMovementBase()&&Character->GetMovementBase()->Mobility==EComponentMobility::Static&&FVector::Dist2D(Character->GetNavAgentLocation(),RouteExit)<165.f&&FMath::Abs(Character->GetNavAgentLocation().Z-RouteExit.Z)<20.f)
        {
            auto Target=MakeShared<FJsonObject>();Target->SetStringField(TEXT("event"),TEXT("AI_opponent_fixture_target_reposition"));Target->SetStringField(TEXT("stage"),TEXT("original_continuation_floor_after_real_departure_landing"));Target->SetStringField(TEXT("actual_AI_static_shore_landing_feet"),Character->GetNavAgentLocation().ToString());Target->SetStringField(TEXT("original_static_shore"),RouteExit.ToString());Target->SetStringField(TEXT("opponent_static_floor_target"),RouteGoal.ToString());Target->SetNumberField(TEXT("actual_production_departures"),D.Departures);Target->SetNumberField(TEXT("departures_at_leg_start"),RouteDeparturesBefore);
            Mode->Player->SetActorLocation(RouteGoal+FVector(0,0,Mode->Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2));++RouteTargetRepositions;Mode->Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);Target->SetNumberField(TEXT("fixture_target_repositions"),RouteTargetRepositions);PlatformRouteEvidence.Add(MakeShared<FJsonValueObject>(Target));RoutePostExitDistance=0;SetState(5);
        }
        if(RouteState==5)bRouteAIResume|=AIBoardBot->GetMovementIntent()==TEXT("Chase")&&Movement->Velocity.Size2D()>10&&AIBoardBot->GetDesiredCombatTarget()==Mode->Player;
        if(RouteWalkingDistance<5&&Now-RouteLegAt>20&&!D.bWaitingAtDock&&D.Step!=1&&D.Step!=3){FailRoute(TEXT("Production AI did not walk from its normal spawn; inspect path/result/recovery samples"));return;}
    }
    if(RouteState==5&&Movement->IsMovingOnGround()&&!Based&&FVector::Dist2D(Character->GetNavAgentLocation(),RouteGoal)<165&&RoutePostExitDistance>100&&(!bPlatformRoutePlayer||(!bRouteWalkJumpPending&&!AHeliosBioStaticJumpLink::IsTraversing(Character)&&!PlatformWalkPath.IsValidIndex(RoutePathIndex))))
    {
        const bool LegPassed=bRouteWait&&bRouteJump&&bRouteCarry&&bRouteExitJump&&(bPlatformRoutePlayer||bRouteAIResume);Check(*(FString::Printf(TEXT("%s_actual_complete_platform_leg_%d"),bPlatformRoutePlayer?TEXT("player"):TEXT("AI"),RouteLeg)),LegPassed);
        auto E=MakeShared<FJsonObject>();E->SetStringField(TEXT("event"),TEXT("complete_one_way_leg"));E->SetBoolField(TEXT("AI"),!bPlatformRoutePlayer);E->SetNumberField(TEXT("leg"),RouteLeg);E->SetBoolField(TEXT("passed"),LegPassed);E->SetBoolField(TEXT("wait_observed"),bRouteWait);E->SetBoolField(TEXT("boarding_jump_observed"),bRouteJump);E->SetBoolField(TEXT("real_base_carry_observed"),bRouteCarry);E->SetBoolField(TEXT("departure_jump_observed"),bRouteExitJump);E->SetBoolField(TEXT("continued_chase_observed"),bRouteAIResume);E->SetNumberField(TEXT("post_exit_walking_cm"),RoutePostExitDistance);PlatformRouteEvidence.Add(MakeShared<FJsonValueObject>(E));
        if(!LegPassed){FailRoute(TEXT("Physical leg reached target but a required wait/jump/carry/departure/continuation event was not observed"));return;}
        ++RouteLeg;if(bPlatformRoutePlayer)PlayerRouteLegs=RouteLeg;else AIRouteLegs=RouteLeg;
        if(RouteLeg==4)
        {
            const bool Headless=FParse::Param(FCommandLine::Get(),TEXT("NullRHI"));RouteStateAt=Now;
            if(bPlatformRoutePlayer){bPlayerRouteCompleted=true;Capture(TEXT("platform_player_two_round_trips"));if(Headless)BeginAI();else Stage=90;}
            else{bAIRouteCompleted=true;Capture(TEXT("platform_AI_two_round_trips"));if(!bProductionPlatformWeaponBlocked)Check(TEXT("production_platform_obstruction_observed"),false);if(Headless)FinishSpecial(TEXT("FloatingPlatform"));else Stage=91;}return;
        }
        Swap(RouteEntry,RouteExit);Swap(RouteGoal,RouteReturnGoal);Swap(RouteFixtureEntryShoreCandidates,RouteFixtureExitShoreCandidates);
        RouteState=0;RouteStateAt=RouteLegAt=Now;RoutePostExitDistance=0;RouteDepartureWaitAt=0;bRouteWait=bRouteJump=bRouteCarry=bRouteExitJump=bRouteAIResume=false;
        if(bPlatformRoutePlayer){Character->ConsumeMovementInputVector();if(!SetPlatformWalkPath(Character,RouteEntry)){FailRoute(TEXT("No complete walking path from departure continuation to return boarding shore"));return;}}
        else
        {
            const auto D=HeliosBioPlatformTraversal::GetDiagnostics(AIBoardBot);RouteBoardingsBefore=D.Boardings;RouteDeparturesBefore=D.Departures;
            // After the real continued ground pursuit a static high target may
            // be cheaper to chase by ground. An actual opponent riding the source
            // platform is instead a production reason to board. Only this target
            // fixture moves; the AI chooses and executes its normal full route.
            if(!PlacePlatformAboardOpponentFixture(Character,TEXT("original_platform_opponent_before_return_boarding_real_base_pending"))){FailRoute(TEXT("No collision-clear actual platform opponent fixture for return boarding"));return;}
        }
    }
}
