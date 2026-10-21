#include "QA/HeliosBioSupplyWeaponQA.h"
#include "QA/HeliosBioSupplyPresentationQA.h"
#include "Bio/HeliosBioHotelGameMode.h"
#include "Bio/HeliosBioNPC.h"
#include "Bio/HeliosBioPlayer.h"
#include "Bio/HeliosBioSupply.h"
#include "Bio/HeliosBioSupplyWeapon.h"
#include "Bio/HeliosBioSupplyWeaponDefinition.h"
#include "Bio/HeliosBioRoundInventoryComponent.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Player/HeliosCharacterDefinition.h"
#include "Player/HeliosCharacterActionProfile.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Materials/MaterialInterface.h"
#include "SkeletalRenderPublic.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "AIController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/SkeletalMesh.h"
#include "Sound/SoundBase.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "InputCoreTypes.h"
#include "UnrealClient.h"

namespace
{
bool AppendSupplyProgress(const FString& Directory,const TCHAR* Kind,double WorldSeconds,const TSharedRef<FJsonObject>& Data)
{
    if(Directory.IsEmpty())return true;
    auto Event=MakeShared<FJsonObject>();Event->SetStringField(TEXT("kind"),Kind);Event->SetNumberField(TEXT("world_seconds"),WorldSeconds);Event->SetObjectField(TEXT("data"),Data);
    FString Text;FJsonSerializer::Serialize(Event,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));Text+=TEXT("\n");
    // Preserve completed real observations even if the engine terminates before Finish.
    // A partial journal is diagnostic evidence and never substitutes for a final report.
    return FFileHelper::SaveStringToFile(Text,*(Directory/TEXT("supply_progress.jsonl")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
}
FString PackageSHA1(const FString& File)
{
    TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*File))return FString();
    uint8 Hash[20];FSHA1::HashBuffer(Bytes.GetData(),Bytes.Num(),Hash);return BytesToHex(Hash,20);
}
uint8 Gates(const UHeliosBioSupplyWeaponDefinition* D)
{return (D->bSourceIdentityVerified?1:0)|(D->bPresentationVerified?2:0)|(D->bMuzzleBridgeVerified?4:0)|(D->bGameplayVerified?8:0);}
TSharedRef<FJsonObject> NumericTransform(const FTransform& Transform)
{
    auto Row=MakeShared<FJsonObject>();const FVector P=Transform.GetTranslation(),S=Transform.GetScale3D();const FQuat Q=Transform.GetRotation();
    const auto Array=[](std::initializer_list<double> Values){TArray<TSharedPtr<FJsonValue>> Result;for(double Value:Values)Result.Add(MakeShared<FJsonValueNumber>(Value));return Result;};
    Row->SetArrayField(TEXT("translation_cm"),Array({P.X,P.Y,P.Z}));Row->SetArrayField(TEXT("rotation_xyzw"),Array({Q.X,Q.Y,Q.Z,Q.W}));Row->SetArrayField(TEXT("scale"),Array({S.X,S.Y,S.Z}));return Row;
}
bool ContinuousFlatSourceSupport(UWorld* World,const FVector& Start,const FVector& Direction,float Feet,const FCollisionQueryParams& Query)
{
    for(float Distance:{40.f,80.f,120.f,160.f,200.f,220.f})
    {
        const FVector At=Start+Direction*Distance;FHitResult Floor;
        if(!World->LineTraceSingleByObjectType(Floor,FVector(At.X,At.Y,Feet+25),FVector(At.X,At.Y,Feet-25),FCollisionObjectQueryParams(ECC_WorldStatic),Query)||Floor.ImpactNormal.Z<.95f||FMath::Abs(Floor.ImpactPoint.Z-Feet)>3)return false;
    }
    return true;
}
bool OriginalStunThrowHasNoAudioEvent(FString& EvidenceFile)
{
    EvidenceFile=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/cfm_bio_box_weapons/stun_controller_source_audit.json"));
    FString Text;TSharedPtr<FJsonObject> Document;
    if(!FFileHelper::LoadFileToString(Text,*EvidenceFile)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Document)||!Document||Document->GetIntegerField(TEXT("originalItemId"))!=10802015)return false;
    for(const auto& ViewValue:Document->GetArrayField(TEXT("views")))
    {
        const auto View=ViewValue->AsObject();if(!View||View->GetStringField(TEXT("scene"))!=TEXT("Prefab_1P_ZhenHanDan_Go"))continue;
        for(const auto& MotionValue:View->GetArrayField(TEXT("effectiveMotionSlots")))
        {
            const auto Motion=MotionValue->AsObject();
            if(Motion&&Motion->GetStringField(TEXT("slot"))==TEXT("ThrowWeapon_082_ShouLei_HQL_1P_WeaponThrow")&&Motion->GetStringField(TEXT("clip"))==TEXT("ThrowWeapon_090_ShouLei_HWS_1P_WeaponThrow"))return Motion->GetArrayField(TEXT("events")).IsEmpty();
        }
    }
    return false;
}
}
AHeliosBioSupplyWeaponQA::AHeliosBioSupplyWeaponQA()
{PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void AHeliosBioSupplyWeaponQA::EndPlay(const EEndPlayReason::Type Reason)
{if(!bFinished&&!Out.IsEmpty())Finish(TEXT("Normal runtime shutdown/independent input observer completion"));Super::EndPlay(Reason);}
void AHeliosBioSupplyWeaponQA::Check(const FString& Name,bool Passed)
{
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("name"),Name);Row->SetBoolField(TEXT("passed"),Passed);Checks.Add(MakeShared<FJsonValueObject>(Row));
    if(!AppendSupplyProgress(Out,TEXT("check"),GetWorld()->GetTimeSeconds(),Row)){++Failures;UE_LOG(LogTemp,Error,TEXT("BIO_SUPPLY_QA progress journal write failed"));}
    if(!Passed)++Failures;UE_LOG(LogTemp,Display,TEXT("BIO_SUPPLY_QA %s %s"),*Name,Passed?TEXT("PASS"):TEXT("FAIL"));
}
bool AHeliosBioSupplyWeaponQA::LoadCandidates()
{
    for(int32 I=1;I<5;++I)
    {
        auto* D=UHeliosBioSupplyWeaponDefinition::LoadForSlot(I);
        const int32 Id=UHeliosBioSupplyWeaponDefinition::ItemForSlot(I);
        if(!D||!D->bSourceIdentityVerified||D->OriginalItemId!=Id||D->WeaponId!=FName(*FString::Printf(TEXT("cfm_bio_supply_%d"),Id))||
            !D->WeaponClass||!D->WeaponClass->IsChildOf(AHeliosBioSupplyWeapon::StaticClass())||D->SourceEvidence.IsEmpty())
        {Blocker=FString::Printf(TEXT("Original identity/class/evidence rejected for slot%d item%d; source identity is never fabricated by this fixture"),I,Id);return false;}
        const FString File=FPackageName::LongPackageNameToFilename(D->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
        const FString Hash=PackageSHA1(File);if(Hash.IsEmpty()){Blocker=TEXT("Cannot hash original definition package");return false;}
        OriginalPackageHashes.Add(File,Hash);Originals.Add(D);OriginalGateBits.Add(Gates(D));
        auto* Candidate=DuplicateObject<UHeliosBioSupplyWeaponDefinition>(D,this);
        // Identity remains the proven original value. Only these three gates
        // are opened on an unsaved transient candidate for actual acceptance.
        Candidate->SetFlags(RF_Transient);Candidate->bPresentationVerified=true;Candidate->bMuzzleBridgeVerified=true;Candidate->bGameplayVerified=true;
        Candidates.Add(Candidate);
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("unsaved_candidate_definition"));Row->SetNumberField(TEXT("slot"),I);Row->SetNumberField(TEXT("original_item_id"),Id);Row->SetStringField(TEXT("source_definition"),D->GetPathName());Row->SetStringField(TEXT("candidate"),Candidate->GetPathName());Row->SetStringField(TEXT("source_evidence"),D->SourceEvidence);Row->SetStringField(TEXT("package_file"),File);Row->SetStringField(TEXT("original_package_sha1"),Hash);Row->SetNumberField(TEXT("original_gate_bits"),Gates(D));Row->SetBoolField(TEXT("original_source_identity_asserted_not_overridden"),Candidate->bSourceIdentityVerified==D->bSourceIdentityVerified);Observations.Add(MakeShared<FJsonValueObject>(Row));
        bool ExpectedFireAudio=Candidate->FireSound.LoadSynchronous()!=nullptr&&Candidate->ThirdPersonFireSound.LoadSynchronous()!=nullptr;
        if(Id==10802015)
        {
            bool Prepare=false,Equip=false,NoFireCue=true;
            for(const auto& Cue:Candidate->SourceAudioCues)
            {
                if(Cue.Action==TEXT("Fire"))NoFireCue=false;
                if(!Cue.bFirstPerson||!Cue.Sound.LoadSynchronous())continue;
                Prepare|=Cue.Action==TEXT("Prepare")&&FMath::IsNearlyEqual(Cue.NativeSeconds,.38704928755760193f,.000001f);
                Equip|=Cue.Action==TEXT("Equip")&&FMath::IsNearlyEqual(Cue.NativeSeconds,.11948531121015549f,.000001f);
            }
            FString EvidenceFile;const bool SilentSource=OriginalStunThrowHasNoAudioEvent(EvidenceFile);
            ExpectedFireAudio=SilentSource&&Candidate->FireSound.IsNull()&&Candidate->ThirdPersonFireSound.IsNull()&&NoFireCue&&Prepare&&Equip;
            auto Audio=MakeShared<FJsonObject>();Audio->SetStringField(TEXT("kind"),TEXT("original_stun_silent_Throw_and_native_audio_cues"));Audio->SetStringField(TEXT("controller_evidence"),EvidenceFile);Audio->SetStringField(TEXT("controller_evidence_sha1"),PackageSHA1(EvidenceFile));Audio->SetBoolField(TEXT("literal_original_Throw_has_no_audio_event"),SilentSource);Audio->SetBoolField(TEXT("no_fabricated_Fire_audio_or_cue"),Candidate->FireSound.IsNull()&&Candidate->ThirdPersonFireSound.IsNull()&&NoFireCue);Audio->SetBoolField(TEXT("actual_Prepare_audio_at_0_387049288"),Prepare);Audio->SetBoolField(TEXT("actual_Equip_audio_at_0_119485311"),Equip);Audio->SetStringField(TEXT("scope"),TEXT("Original Throw is silent; source Prepare/Equip callback clocks and actual loaded sounds are asserted independently. This resource check does not claim original C# event dispatch or audible runtime acceptance."));Observations.Add(MakeShared<FJsonValueObject>(Audio));
            Check(TEXT("item10802015_original_silent_Throw_and_exact_Prepare_Equip_cues"),ExpectedFireAudio);
        }
        for(const auto& Issue: TArray<TPair<FString,bool>>{
            {TEXT("native_fp_mesh"),Candidate->NativeFirstPersonMesh.LoadSynchronous()!=nullptr},
            {TEXT("source_expected_FP_and_TP_fire_audio"),ExpectedFireAudio},
            {TEXT("impact_audio"),Candidate->FireType==EHeliosBioSupplyFireType::Hitscan||Candidate->ActionSound.LoadSynchronous()!=nullptr},
            {TEXT("source_effect_library"),!Candidate->EffectLibrary.IsEmpty()}})
        {
            if(Issue.Value)continue;auto Missing=MakeShared<FJsonObject>();Missing->SetNumberField(TEXT("item"),Id);Missing->SetStringField(TEXT("pending"),Issue.Key);SourceIssues.Add(MakeShared<FJsonValueObject>(Missing));
        }
        if(!Candidate->IsReady()){Blocker=TEXT("Candidate still fails native IsReady; missing resources were not bypassed");return false;}
    }
    return Candidates.Num()==4;
}
bool AHeliosBioSupplyWeaponQA::OriginalsUnchanged()
{
    bool Same=Originals.Num()==OriginalGateBits.Num();
    for(int32 I=0;I<Originals.Num();++I)Same&=Gates(Originals[I])==OriginalGateBits[I];
    for(const auto& Entry:OriginalPackageHashes)Same&=PackageSHA1(Entry.Key)==Entry.Value;
    return Same;
}
void AHeliosBioSupplyWeaponQA::FreezeBots()
{
    for(auto Bot:Mode->Bots)if(Bot)
    {
        Bot->bQAFreeze=true;if(auto* Gun=Bot->GetEquippedWeapon())Gun->StopFiring();
        if(auto* AI=Cast<AAIController>(Bot->GetController()))AI->StopMovement();
        if(Bot->GetCharacterMovement()->IsMovingOnGround())Bot->GetCharacterMovement()->StopMovementImmediately();
    }
}
TSharedRef<FJsonObject> AHeliosBioSupplyWeaponQA::InventorySnapshot(UHeliosBioRoundInventoryComponent* Inventory) const
{
    auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("active_slot"),Inventory?Inventory->GetActiveSlot():-1);TArray<TSharedPtr<FJsonValue>> Slots;
    for(int32 I=0;I<5;++I)
    {
        auto Entry=MakeShared<FJsonObject>();auto* Gun=Inventory?Inventory->GetWeapon(I):nullptr;
        Entry->SetNumberField(TEXT("slot"),I);Entry->SetStringField(TEXT("weapon"),GetPathNameSafe(Gun));Entry->SetStringField(TEXT("weapon_class"),Gun?Gun->GetClass()->GetPathName():TEXT(""));Entry->SetStringField(TEXT("weapon_id"),Inventory?Inventory->GetWeaponId(I).ToString():TEXT(""));Entry->SetNumberField(TEXT("actual_bullets"),Gun?Gun->GetBulletCount():0);Entry->SetNumberField(TEXT("actual_reserve"),Inventory?Inventory->GetReserve(I):0);
        if(auto* Native=Cast<AHeliosBioSupplyWeapon>(Gun)){Entry->SetNumberField(TEXT("actual_native_shots"),Native->GetNativeShotCount());Entry->SetNumberField(TEXT("actual_native_hits"),Native->GetNativeHitCount());}
        Slots.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Row->SetArrayField(TEXT("slots"),Slots);return Row;
}
void AHeliosBioSupplyWeaponQA::Observe(const FString& Kind)
{
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),Kind);Row->SetNumberField(TEXT("world_seconds"),GetWorld()->GetTimeSeconds());Row->SetNumberField(TEXT("wall_seconds"),FPlatformTime::Seconds()-StartedWall);Row->SetObjectField(TEXT("player_inventory"),InventorySnapshot(Mode->Player->GetRoundInventory()));Row->SetNumberField(TEXT("phase"),int32(Mode->Model.GetPhase()));Row->SetNumberField(TEXT("round"),Mode->RoundSerial);Row->SetStringField(TEXT("player_location"),Mode->Player->GetActorLocation().ToString());Observations.Add(MakeShared<FJsonValueObject>(Row));
    if(!AppendSupplyProgress(Out,TEXT("observation"),GetWorld()->GetTimeSeconds(),Row)){++Failures;UE_LOG(LogTemp,Error,TEXT("BIO_SUPPLY_QA progress journal write failed"));}
}
bool AHeliosBioSupplyWeaponQA::GrantManual()
{
    auto* Inventory=Mode->Player->GetRoundInventory();
    if(!Inventory||!Inventory->HasSlot(0)){Blocker=TEXT("Normal spawned player's baseline inventory missing");return false;}
    for(auto Candidate:Candidates)
    {
        FString Error;if(!Inventory->CanGrant(Candidate,&Error)||!Inventory->TryGrant(Candidate)){Blocker=TEXT("Actual source/pair acquisition rejected: ")+Error;return false;}
    }
    Inventory->Select(0);bFixturePrepared=true;Observe(TEXT("manual_candidate_four_extras_granted_by_production_TryGrant"));return true;
}
AHeliosBioSupply* AHeliosBioSupplyWeaponQA::MakeSupply(UHeliosBioSupplyWeaponDefinition* Definition)
{
    auto* Player=Mode->Player.Get();const float Feet=Player->GetActorLocation().Z-Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BioSupplyQAFloor),false,Player);for(auto Bot:Mode->Bots)Query.AddIgnoredActor(Bot);
    for(int32 Direction=0;Direction<16;++Direction)
    {
        const FVector Offset=FVector(80,0,0).RotateAngleAxis(Direction*22.5f,FVector::UpVector);const FVector Wanted=Player->GetActorLocation()+Offset;FHitResult Floor;
        if(!GetWorld()->LineTraceSingleByObjectType(Floor,FVector(Wanted.X,Wanted.Y,Feet+25),FVector(Wanted.X,Wanted.Y,Feet-35),FCollisionObjectQueryParams(ECC_WorldStatic),Query)||Floor.ImpactNormal.Z<.95f||FMath::Abs(Floor.ImpactPoint.Z-Feet)>3)continue;
        const FVector At(Wanted.X,Wanted.Y,Floor.ImpactPoint.Z+2);auto* Supply=GetWorld()->SpawnActor<AHeliosBioSupply>(At,FRotator::ZeroRotator);
        if(!Supply)continue;Supply->SetActorTickEnabled(false);Supply->SetKind(EHeliosBioSupplyKind::Normal);Supply->SetWeaponContent(Definition);
        if(!Supply->CanReachPickup(Player)){Supply->Destroy();continue;}FixtureSupplies.Add(Supply);return Supply;
    }
    Blocker=TEXT("No actual same-level source floor/visibility for the80cm pickup fixture");return nullptr;
}
bool AHeliosBioSupplyWeaponQA::PrepareVictim()
{
    if(Victim.IsValid()){const auto* State=Mode->Model.FindParticipant(Victim->ParticipantId);if(!State||!State->bAlive)Victim.Reset();}
    if(!Victim.IsValid())for(const auto& P:Mode->Model.GetParticipants())if(P.bAlive&&P.Team==EHeliosBioTeam::Zombie)
        if(auto* Bot=Cast<AHeliosBioNPC>(Mode->ParticipantActor(P.Id))){Victim=Bot;break;}
    if(!Victim.IsValid())return false;
    auto* Player=Mode->Player.Get();const float Feet=Player->GetActorLocation().Z-Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();auto* Enemy=Victim.Get();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BioSupplyQAActualTarget),true,Player);Query.AddIgnoredActor(Enemy);for(auto Bot:Mode->Bots)if(Bot!=Enemy)Query.AddIgnoredActor(Bot);
    const auto* Definition=Candidates.IsValidIndex(Slot-1)?Candidates[Slot-1].Get():nullptr;
    const TArray<float> Distances=Definition&&Definition->ProjectileGravityScale>0?TArray<float>{650.f,500.f,850.f}:TArray<float>{1200.f,900.f,650.f};
    for(float Distance:Distances)for(int32 I=0;I<16;++I)
    {
        const FVector Wanted=Player->GetActorLocation()+FVector(Distance,0,0).RotateAngleAxis(I*22.5f,FVector::UpVector);FHitResult Floor;
        if(!GetWorld()->LineTraceSingleByObjectType(Floor,FVector(Wanted.X,Wanted.Y,Feet+25),FVector(Wanted.X,Wanted.Y,Feet-35),FCollisionObjectQueryParams(ECC_WorldStatic),Query)||Floor.ImpactNormal.Z<.95f||FMath::Abs(Floor.ImpactPoint.Z-Feet)>3)continue;
        const FVector At(Wanted.X,Wanted.Y,Floor.ImpactPoint.Z+Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2);
        if(GetWorld()->OverlapBlockingTestByChannel(At,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius(),Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),Query))continue;
        FVector Muzzle;auto* Gun=TestWeapon.Get();FVector Eyes;FRotator Aim;Player->GetActorEyesViewPoint(Eyes,Aim);FHitResult Wall;
        if(!Gun||!Gun->GetNativeMuzzle(Muzzle)||GetWorld()->SweepSingleByChannel(Wall,Eyes,Muzzle,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(3.f),Query)||GetWorld()->LineTraceSingleByChannel(Wall,Muzzle,At,ECC_Visibility,Query))continue;
        const FVector Before=Enemy->GetActorLocation();Enemy->SetActorLocation(At,false,nullptr,ETeleportType::TeleportPhysics);Enemy->bQAFreeze=true;
        if(auto* PC=Cast<APlayerController>(Player->GetController()))PC->SetControlRotation((At-Eyes).Rotation());
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("frozen_real_opponent_reposition"));Row->SetStringField(TEXT("before"),Before.ToString());Row->SetStringField(TEXT("after"),At.ToString());Row->SetStringField(TEXT("actual_native_muzzle"),Muzzle.ToString());Row->SetStringField(TEXT("floor_component"),GetPathNameSafe(Floor.GetComponent()));Row->SetBoolField(TEXT("same_static_floor_capsule_and_actual_muzzle_clear"),true);Observations.Add(MakeShared<FJsonValueObject>(Row));return true;
    }
    return false;
}
bool AHeliosBioSupplyWeaponQA::BeginNativeItem(double Now)
{
    AudioWindowStart=FPlatformTime::Seconds();
    auto* Inventory=Mode->Player->GetRoundInventory();if(!Inventory->Select(Slot,bFormCase))return false;
    TestWeapon=Cast<AHeliosBioSupplyWeapon>(Inventory->GetWeapon(Slot));if(!TestWeapon.IsValid())return false;
    StageAt=Now;Stage=5;bFiring=false;bReloadStarted=false;bInterruptTestDone=false;bPlayerMoveComplete=false;FPPose.Reset();return true;
}
void AHeliosBioSupplyWeaponQA::TickNativeItem(double Now)
{
    auto* Gun=TestWeapon.Get();auto* Player=Mode->Player.Get();auto* Inventory=Player->GetRoundInventory();auto* D=Candidates[Slot-1].Get();
    if(!Gun){Finish(TEXT("Actual native item disappeared"));return;}
    if(Stage==5)
    {
        if(Gun->IsNativeActionBusy()){if(Now-StageAt>8)Finish(TEXT("Actual native Equip never recovered"));return;}
        if(bFormCase&&!bPlayerMoveComplete){if(!PrepareMove(Player)){Check(SelectedForm+TEXT("_Player_native_Move_lane_available"),false);Finish(TEXT("No actual220cm same-level Player movement corridor"));return;}Stage=50;StageAt=Now;return;}
        if(!PrepareVictim()){Finish(TEXT("No same-level true opponent / actual source muzzle line for native firing"));return;}
        const auto* Enemy=Mode->Model.FindParticipant(Victim->ParticipantId);const auto* Human=Mode->Model.FindParticipant(0);
        HealthBefore=Enemy->Health;FrozenBefore=Enemy->FrozenSeconds;DamageBefore=Human->DamageDealt;AmmoBefore=Gun->GetBulletCount();ReserveBefore=Inventory->GetReserve(Slot);ShotsBefore=Gun->GetNativeShotCount();HitsBefore=Gun->GetNativeHitCount();
        Player->StartBioFire();bFiring=true;Stage=6;StageAt=Now;return;
    }
    if(Stage==6)
    {
        if(Gun->GetNativeShotCount()>ShotsBefore&&bFiring){Player->StopBioFire();bFiring=false;}
        const auto* Enemy=Mode->Model.FindParticipant(Victim->ParticipantId);const auto* Human=Mode->Model.FindParticipant(0);
        const bool Effect=D->FireType==EHeliosBioSupplyFireType::ConcussionProjectile?Enemy->FrozenSeconds>FrozenBefore:Enemy->Health<HealthBefore&&Human->DamageDealt>DamageBefore;
        if(Gun->GetNativeHitCount()>HitsBefore&&Effect)
        {
            const bool Conserved=AmmoBefore-Gun->GetBulletCount()==Gun->GetNativeShotCount()-ShotsBefore&&Inventory->GetReserve(Slot)==ReserveBefore;
            Check(FString::Printf(TEXT("item%d_player_actual_native_shot_ammo_and_effect"),D->OriginalItemId),Conserved&&Gun->GetNativeShotCount()>ShotsBefore);
            Observe(TEXT("actual_native_fire_effect_observed"));
            if(bFormCase){Stage=65;StageAt=Now;return;}
            ReloadAmmoBefore=Gun->GetBulletCount();ReloadReserveBefore=Inventory->GetReserve(Slot);ReloadShotsBefore=Gun->GetNativeShotCount();Player->ReloadBio();bReloadStarted=Player->IsReloading();Stage=7;StageAt=Now;return;
        }
        if(Now-StageAt>FMath::Max(8.f,D->ProjectileLifetime+2)){Player->StopBioFire();Check(FString::Printf(TEXT("item%d_player_actual_native_effect"),D->OriginalItemId),false);Finish(TEXT("No attributed actual native hit/HP or concussion effect; no simulated damage accepted"));}return;
    }
    if(Stage==7)
    {
        if(bFormCase&&!bInterruptTestDone&&Player->IsReloading()&&Now-ReloadStartedAt>=.15)
        {
            InterruptAmmo=Gun->GetBulletCount();InterruptReserve=Inventory->GetReserve(Slot);
            const bool Switched=Inventory->Select(0);Check(SelectedForm+TEXT("_item")+FString::FromInt(D->OriginalItemId)+TEXT("_real_switch_during_reload_cancels_pending_commit"),Switched&&!Player->IsReloading()&&Player->GetEquippedWeapon()==Inventory->GetWeapon(0));
            if(!Switched){Finish(TEXT("Actual inventory switch during reload rejected"));return;}bInterruptTestDone=true;Stage=8;StageAt=Now;return;
        }
        if(Player->IsReloading()){if(Now-StageAt>D->ReloadSeconds+3)Finish(TEXT("Real native reload did not finish"));return;}
        const int32 Loaded=ReloadReserveBefore-Inventory->GetReserve(Slot);
        Check(FString::Printf(TEXT("item%d_player_actual_reload_conservation"),D->OriginalItemId),bReloadStarted&&Loaded>0&&Gun->GetBulletCount()==ReloadAmmoBefore+Loaded&&Gun->GetNativeShotCount()==ReloadShotsBefore);
        ++CompletedNativeItems;if(bFormCase){if(!BeginNPCItem(Now))Finish(Blocker);return;}++Slot;
        if(Slot<5){if(!BeginNativeItem(Now))Finish(TEXT("Next real inventory slot cannot be selected"));}
        else Finish(TEXT("Checkpoint: four-player native core executed; independent17-form AI/action/Down, reload-switch, wall competition and infection/restore coverage must still run before whole-weapon acceptance"));
    }
    if(Stage==50)
    {
        if(Now-StageAt<.6){Player->AddMovementInput(MoveDirection);return;}
        Check(SelectedForm+TEXT("_Player_item")+FString::FromInt(D->OriginalItemId)+TEXT("_actual_Move_travel_and_native_skin"),FVector::Dist2D(MoveStart,Player->GetActorLocation())>40&&PosePassed(true,TEXT("Move")));Stage=51;StageAt=Now;return;
    }
    if(Stage==51)
    {
        if(Player->GetVelocity().Size2D()>10){if(Now-StageAt>3)Finish(TEXT("Actual Player walk did not settle after input stopped"));return;}
        if(Now-StageAt<.5)return;bPlayerMoveComplete=true;Stage=5;StageAt=Now;return;
    }
    if(Stage==65)
    {
        if(Gun->IsNativeActionBusy()){if(Now-StageAt>8)Finish(TEXT("Actual native Fire/Throw source action did not recover"));return;}
        if(Now-StageAt<.3)return;
        ReloadAmmoBefore=Gun->GetBulletCount();ReloadReserveBefore=Inventory->GetReserve(Slot);ReloadShotsBefore=Gun->GetNativeShotCount();FPPose.Remove(TEXT("ChangeClip"));Player->ReloadBio();bReloadStarted=Player->IsReloading();ReloadStartedAt=Now;Stage=7;StageAt=Now;return;
    }
    if(Stage==8)
    {
        if(Now-StageAt<D->ReloadSeconds+.25)return;
        Check(SelectedForm+TEXT("_item")+FString::FromInt(D->OriginalItemId)+TEXT("_inactive_interrupted_reload_never_fills_or_spends_reserve"),Gun->GetBulletCount()==InterruptAmmo&&Inventory->GetReserve(Slot)==InterruptReserve);
        if(!Inventory->Select(Slot)){Finish(TEXT("Actual native slot could not be reselected after cancelled reload"));return;}Stage=9;StageAt=Now;return;
    }
    if(Stage==9)
    {
        if(Gun->IsNativeActionBusy()){if(Now-StageAt>8)Finish(TEXT("Actual reselect Equip did not recover"));return;}
        ReloadAmmoBefore=Gun->GetBulletCount();ReloadReserveBefore=Inventory->GetReserve(Slot);ReloadShotsBefore=Gun->GetNativeShotCount();FPPose.Remove(TEXT("ChangeClip"));Player->ReloadBio();bReloadStarted=Player->IsReloading();ReloadStartedAt=Now;Stage=7;StageAt=Now;return;
    }
}
bool AHeliosBioSupplyWeaponQA::SetNPCInspectionCamera()
{
    auto* Bot=FormBot.Get();auto* PC=GetWorld()->GetFirstPlayerController();if(!Bot||!PC)return false;
    const FVector Chest=Bot->GetActorLocation()+FVector(0,0,35);FCollisionQueryParams Query(SCENE_QUERY_STAT(BioSupplyQATPCamera),false,Bot);for(auto Other:Mode->Bots)Query.AddIgnoredActor(Other);Query.AddIgnoredActor(Mode->Player);
    for(int32 I=0;I<16;++I)
    {
        const FVector At=Chest+FVector(350,0,90).RotateAngleAxis(I*22.5f,FVector::UpVector);FHitResult Wall;
        if(GetWorld()->SweepSingleByObjectType(Wall,Chest,At,FQuat::Identity,FCollisionObjectQueryParams(ECC_WorldStatic),FCollisionShape::MakeSphere(5.f),Query))continue;
        if(!VisualCamera)VisualCamera=GetWorld()->SpawnActor<ACameraActor>();if(!VisualCamera)return false;
        VisualCamera->SetActorLocationAndRotation(At,(Chest-At).Rotation());VisualCamera->GetCameraComponent()->SetFieldOfView(75);PC->SetViewTarget(VisualCamera);return true;
    }
    return false;
}
void AHeliosBioSupplyWeaponQA::QueueAudioObservation(bool FirstPerson)
{
    (void)FirstPerson;
    const FString Text=UHeliosBioSupplyPresentationQA::ReadAudioEvents(this,AudioSerial);TSharedPtr<FJsonObject> Parsed;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Parsed)||!Parsed){bAudioObservationFailed=true;return;}
    double Serial=AudioSerial;Parsed->TryGetNumberField(TEXT("lastSerial"),Serial);AudioSerial=int64(Serial);
    bool Truncated=false;Parsed->TryGetBoolField(TEXT("history_truncated_before_cursor"),Truncated);bAudioObservationFailed|=Truncated;
    if(!Parsed->GetArrayField(TEXT("events")).IsEmpty())
    {
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("actual_shared_engine_audio_and_render_event_drain"));Row->SetObjectField(TEXT("actual_events"),Parsed);Observations.Add(MakeShared<FJsonValueObject>(Row));
    }
}
void AHeliosBioSupplyWeaponQA::ObserveNativeAV(AHeliosBioSupplyWeapon* Gun,bool FirstPerson)
{
    if(!Gun||!Candidates.IsValidIndex(Slot-1))return;const auto* D=Candidates[Slot-1].Get();const FString Key=FString::FromInt(D->OriginalItemId)+(FirstPerson?TEXT("/FP"):TEXT("/TP"));
    QueueAudioObservation(FirstPerson);
    const auto Owned=[&](AActor* Actor){for(int32 Depth=0;Actor&&Depth<4;++Depth,Actor=Actor->GetOwner())if(Actor==Gun)return true;return false;};
    TArray<TSharedPtr<FJsonValue>> FX;
    for(TActorIterator<AHeliosSSXEffect> It(GetWorld());It;++It)if(Owned(*It))
    {
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("actual_effect_actor"),It->GetPathName());Row->SetNumberField(TEXT("real_visible_tracks"),It->VisibleTrackCount());bool Rendered=false;TArray<UPrimitiveComponent*> Components;It->GetComponents(Components);
        TArray<TSharedPtr<FJsonValue>> Materials;for(auto* Component:Components)if(Component&&Component->IsVisible())
        {Rendered|=Component->WasRecentlyRendered(.2f);for(int32 I=0;I<Component->GetNumMaterials();++I)if(auto* Material=Component->GetMaterial(I))Materials.Add(MakeShared<FJsonValueString>(Material->GetPathName()));}
        Row->SetBoolField(TEXT("actual_recent_render_timestamp"),Rendered);Row->SetArrayField(TEXT("actual_draw_materials"),Materials);FX.Add(MakeShared<FJsonValueObject>(Row));if(Rendered&&It->VisibleTrackCount()>0)ActuallyRenderedFX.Add(Key);
    }
    const int32 Shots=Gun->GetNativeShotCount(),Hits=Gun->GetNativeHitCount();const bool ShotEdge=Shots>AVShots.FindRef(Gun->GetPathName()),HitEdge=Hits>AVHits.FindRef(Gun->GetPathName());const bool NewRenderedFX=Shots>0&&ActuallyRenderedFX.Contains(Key)&&!CapturedViewPhases.Contains(Key+TEXT("/rendered_fx"));
    if(ShotEdge||HitEdge||NewRenderedFX)
    {
        AVShots.Add(Gun->GetPathName(),Shots);AVHits.Add(Gun->GetPathName(),Hits);FString Form=SelectedForm;Form.ReplaceInline(TEXT("/"),TEXT("__"));const TCHAR* Phase=HitEdge?TEXT("actual_impact"):NewRenderedFX?TEXT("rendered_fx"):TEXT("first_shot");const FString Capture=Out/(FString::Printf(TEXT("%s_%d_%s_%s_shot_%d_hit_%d.png"),*Form,D->OriginalItemId,FirstPerson?TEXT("FP"):TEXT("NPC_TP"),Phase,Shots,Hits));
        const FString CapturePhase=Key+TEXT("/")+Phase;
        const bool CaptureRequested=!CapturedViewPhases.Contains(CapturePhase);if(CaptureRequested){FScreenshotRequest::RequestScreenshot(Capture,true,false);RenderCaptures.Add(Capture);CapturedViewPhases.Add(CapturePhase);}
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("actual_native_shot_visual_record"));Row->SetStringField(TEXT("item_view"),Key);Row->SetBoolField(TEXT("real_native_shot_edge"),ShotEdge);Row->SetBoolField(TEXT("real_native_hit_edge"),HitEdge);Row->SetBoolField(TEXT("first_recently_rendered_effect_edge"),NewRenderedFX);Row->SetStringField(TEXT("actual_weapon"),Gun->GetPathName());Row->SetNumberField(TEXT("native_shots"),Shots);Row->SetNumberField(TEXT("native_hits"),Gun->GetNativeHitCount());Row->SetBoolField(TEXT("actual_viewport_capture_requested"),CaptureRequested);Row->SetStringField(TEXT("requested_actual_viewport_capture"),CaptureRequested?Capture:FString());Row->SetStringField(TEXT("actual_camera_target"),GetPathNameSafe(GetWorld()->GetFirstPlayerController()->GetViewTarget()));FVector Muzzle;if(Gun->GetNativeMuzzle(Muzzle))Row->SetStringField(TEXT("real_native_muzzle"),Muzzle.ToString());Row->SetArrayField(TEXT("actual_live_owned_effects"),FX);Row->SetBoolField(TEXT("rendered_visual_reviewed"),false);
        const FString Snapshot=UHeliosBioSupplyPresentationQA::SnapshotParticipant(Gun->GetOwner(),FirstPerson?static_cast<AActor*>(Victim.Get()):FormBot.IsValid()?FormBot->GetDesiredCombatTarget():nullptr);TSharedPtr<FJsonObject> Parsed;if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Snapshot),Parsed)&&Parsed)Row->SetObjectField(TEXT("actual_shared_presentation_snapshot"),Parsed);
        if(auto* Presentation=Gun->GetOwner()->FindComponentByClass<UHeliosCharacterPresentationComponent>())if(auto* Visual=Presentation->GetWeaponMesh())
        {auto Attachment=MakeShared<FJsonObject>();Attachment->SetStringField(TEXT("actual_component"),Visual->GetPathName());Attachment->SetStringField(TEXT("actual_attach_parent"),GetPathNameSafe(Visual->GetAttachParent()));Attachment->SetStringField(TEXT("actual_socket"),Visual->GetAttachSocketName().ToString());Attachment->SetObjectField(TEXT("actual_relative"),NumericTransform(Visual->GetRelativeTransform()));Attachment->SetObjectField(TEXT("actual_world"),NumericTransform(Visual->GetComponentTransform()));Row->SetObjectField(TEXT("actual_selected_weapon_attachment"),Attachment);}
        Observations.Add(MakeShared<FJsonValueObject>(Row));
    }
}
bool AHeliosBioSupplyWeaponQA::CheckNativeAudio(AHeliosBioSupplyWeapon* Gun,bool FirstPerson)
{
    QueueAudioObservation(FirstPerson);if(!Gun)return false;const auto* D=Candidates[Slot-1].Get();TArray<TSharedPtr<FJsonObject>> Events;
    for(const auto& Value:Observations)
    {
        const auto Row=Value->AsObject();if(!Row||Row->GetStringField(TEXT("kind"))!=TEXT("actual_shared_engine_audio_and_render_event_drain"))continue;
        for(const auto& Event:Row->GetObjectField(TEXT("actual_events"))->GetArrayField(TEXT("events")))if(const auto Actual=Event->AsObject();Actual&&Actual->GetNumberField(TEXT("wall_seconds"))>=AudioWindowStart)Events.Add(Actual);
    }
    const auto Played=[&](USoundBase* Sound)
    {
        if(!Sound)return false;bool Created=false,ClockOrSource=false;
        for(const auto& Event:Events)
        {
            const FString Kind=Event->GetStringField(TEXT("kind"));FString ActualSound;if(!Event->TryGetStringField(TEXT("actual_sound"),ActualSound)||ActualSound!=Sound->GetPathName())continue;
            Created|=Kind==TEXT("engine_active_created");if(!Kind.StartsWith(TEXT("engine_active_")))continue;
            double Time=0,Sources=0;Event->TryGetNumberField(TEXT("playback_seconds"),Time);Event->TryGetNumberField(TEXT("real_sources"),Sources);ClockOrSource|=Time>0||Sources>0;
        }
        return Created&&ClockOrSource;
    };
    const auto Requested=[&](FName Action,USoundBase* Sound)
    {
        if(!Sound)return false;
        for(const auto& Event:Events)
        {
            if(Event->GetStringField(TEXT("kind"))!=TEXT("native_PlaySound_request"))continue;
            if(Event->GetStringField(TEXT("source_actor"))==Gun->GetPathName()&&Event->GetStringField(TEXT("actual_sound"))==Sound->GetPathName()&&Event->GetStringField(TEXT("action"))==Action.ToString()&&Event->GetBoolField(TEXT("first_person"))==FirstPerson)return true;
        }
        return false;
    };
    bool Passed=true;
    if(D->OriginalItemId==10802015)
    {
        for(FName Action:{FName(TEXT("Equip")),FName(TEXT("Prepare"))})
        {
            const auto* Ref=D->SourceActionSounds.Find(FName(*(FString(FirstPerson?TEXT("FP_"):TEXT("TP_"))+Action.ToString())));auto* Sound=Ref?Ref->LoadSynchronous():nullptr;
            Passed&=Requested(Action,Sound)&&Played(Sound);
        }
        for(const auto& Event:Events)if(Event->GetStringField(TEXT("kind"))==TEXT("native_PlaySound_request")&&Event->GetStringField(TEXT("source_actor"))==Gun->GetPathName()&&Event->GetStringField(TEXT("action"))==TEXT("Fire"))Passed=false;
    }
    else
    {auto* Sound=(FirstPerson?D->FireSound:D->ThirdPersonFireSound).LoadSynchronous();Passed=Requested(TEXT("Fire"),Sound)&&Played(Sound);}
    if(D->FireType!=EHeliosBioSupplyFireType::Hitscan)Passed&=Played(D->ActionSound.LoadSynchronous());
    Check(SelectedForm+TEXT("_item")+FString::FromInt(D->OriginalItemId)+(FirstPerson?TEXT("_FP"):TEXT("_NPC_TP"))+TEXT("_actual_native_audio_request_engine_creation_and_playback"),Passed);return Passed;
}
bool AHeliosBioSupplyWeaponQA::SetupFormFixture()
{
    // Planning uses a separate rules value; the actual match always advances on
    // real world time. Put both mothers in the repeated part of the29-NPC roster.
    Mode->Model.Config.InitialMotherCount=2;Mode->Model.Config.bMothersAreTerminators=false;
    Mode->Model.Config.MotherHealth=Mode->Model.Config.ZombieHealth=100000000.f;
    int32 FixtureSeed=INDEX_NONE;
    for(int32 CandidateSeed=0;CandidateSeed<20000;++CandidateSeed)
    {
        FHeliosBioRules Planning;Planning.Config=Mode->Model.Config;Planning.StartRound(30,CandidateSeed);Planning.Tick(Planning.Config.PreparationSeconds+.1f);
        if(Planning.FindParticipant(28)->bMother&&Planning.FindParticipant(29)->bMother){FixtureSeed=CandidateSeed;break;}
    }
    if(FixtureSeed==INDEX_NONE){Blocker=TEXT("Cannot plan two actual mothers in repeated forms without losing a human form");return false;}
    Mode->BaseRoundSeed=FixtureSeed-Mode->RoundSerial*103;Mode->RestartBioRound();FreezeBots();
    TMap<FString,AHeliosBioNPC*> ByForm;
    for(auto Bot:Mode->Bots)if(Bot&&Bot->ParticipantId<28)
    {const auto& Loadout=Bot->GetHumanLoadout();const FString Key=Loadout.CharacterId.ToString()+TEXT("/")+Loadout.CharacterVariantId.ToString();if(!ByForm.Contains(Key))ByForm.Add(Key,Bot);}
    TArray<FString> Forms;ByForm.GetKeys(Forms);Forms.Sort();
    if(Forms.Num()!=17||!Forms.IsValidIndex(FormIndex)){Blocker=TEXT("Actual own-NPC roster does not expose the requested one of17 distinct forms");return false;}
    SelectedForm=Forms[FormIndex];FormBot=ByForm.FindChecked(SelectedForm);FormBaseline=FormBot->GetHumanLoadout();
    Victim=Cast<AHeliosBioNPC>(Mode->ParticipantActor(29));OtherMother=Cast<AHeliosBioNPC>(Mode->ParticipantActor(28));
    for(auto Bot:Mode->Bots)if(Bot&&Bot!=FormBot.Get()&&Bot->ParticipantId<28){SerumCaster=Bot;break;}
    if(!Mode->Player->EquipBioLoadout(FormBaseline)||!Victim.IsValid()||!OtherMother.IsValid()||!SerumCaster.IsValid()){Blocker=TEXT("Actual own-form player equipment/mothers/restore rule fixture unavailable");return false;}
    for(AActor* Actor:{static_cast<AActor*>(Mode->Player.Get()),static_cast<AActor*>(FormBot.Get())})
        if(auto* Presentation=Actor->FindComponentByClass<UHeliosCharacterPresentationComponent>())AddTickPrerequisiteComponent(Presentation);
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("isolated_own_form_real_weapon_fixture"));Row->SetNumberField(TEXT("form_index"),FormIndex);Row->SetStringField(TEXT("form"),SelectedForm);Row->SetNumberField(TEXT("real_NPC_participant"),FormBot->ParticipantId);Row->SetNumberField(TEXT("source_roster_distinct_forms"),Forms.Num());Row->SetNumberField(TEXT("seed"),FixtureSeed);Row->SetNumberField(TEXT("actual_preparation_seconds"),Mode->Model.Config.PreparationSeconds);Row->SetNumberField(TEXT("actual_activity_seconds"),Mode->Model.Config.RoundSeconds);Row->SetStringField(TEXT("baseline_weapon"),FormBaseline.PrimaryWeaponId.ToString());Row->SetStringField(TEXT("fixture_overrides"),TEXT("Two repeated-form mothers with100000000 HP, frozen non-tested NPCs, selected Player uses this actual NPC's own form. Persistent selected loadout/assets are untouched. No tested shot/ammo/damage/pose is synthesized."));Observations.Add(MakeShared<FJsonValueObject>(Row));return true;
}
const FHeliosCharacterWeaponPresentation* AHeliosBioSupplyWeaponQA::SourcePair() const
{
    const auto* Character=FHeliosCharacterRegistry::FindCharacter(FormBaseline.CharacterId);const auto* Form=Character?Character->FindForm(FormBaseline.CharacterVariantId):nullptr;
    auto* Profile=Form?Form->ActionProfile.LoadSynchronous():nullptr;
    return Profile&&Candidates.IsValidIndex(Slot-1)?Profile->FindWeapon(Candidates[Slot-1]->WeaponId):nullptr;
}
void AHeliosBioSupplyWeaponQA::SampleNativePose(AActor* ParticipantActor,AHeliosBioSupplyWeapon* Gun,bool FirstPerson,FName Semantic)
{
    const auto* Pair=SourcePair();auto* Presentation=ParticipantActor?ParticipantActor->FindComponentByClass<UHeliosCharacterPresentationComponent>():nullptr;auto* Visible=Presentation?Presentation->GetArmsMesh():nullptr;
    auto* Driver=Gun?Gun->GetFirstPersonMesh():nullptr;auto* DriverNode=Driver?Driver->GetSingleNodeInstance():nullptr;auto* Node=FirstPerson?DriverNode:Visible?Visible->GetSingleNodeInstance():nullptr;
    auto* ExpectedMesh=Pair?(FirstPerson?Pair->FirstPersonMesh:Pair->ThirdPersonMesh).LoadSynchronous():nullptr;
    const auto* TargetRef=Pair?Pair->ThirdPersonClips.Find(Semantic):nullptr;
    const auto* SourceRef=Candidates.IsValidIndex(Slot-1)?Candidates[Slot-1]->NativeFirstPersonClips.Find(Semantic):nullptr;
    auto* ExpectedClip=FirstPerson?(SourceRef?SourceRef->LoadSynchronous():nullptr):(TargetRef?TargetRef->LoadSynchronous():nullptr);
    if(!Visible||!ExpectedMesh||Visible->GetSkeletalMeshAsset()!=ExpectedMesh||!Visible->IsVisible()||Visible->bHiddenInGame||!Node||!ExpectedClip||Node->GetCurrentAsset()!=ExpectedClip)return;
    if(FirstPerson&&Visible->LeaderPoseComponent.Get()!=Driver)return;
    if(!FirstPerson&&Presentation->GetPoseCommitFrame()!=GFrameCounter)return;
    TArray<FFinalSkinVertex> Vertices;Visible->GetCPUSkinnedVertices(Vertices,0);if(Vertices.IsEmpty())return;
    TArray<FVector> Surface;for(int32 I=0;I<Vertices.Num();I+=FMath::Max(1,Vertices.Num()/96)){const FVector Point(Vertices[I].Position);if(Point.ContainsNaN())return;Surface.Add(Point);}
    auto& Probe=(FirstPerson?FPPose:TPPose).FindOrAdd(Semantic);const float Seconds=Node->GetCurrentTime();bool Changed=false;
    if(Probe.Clip==ExpectedClip&&Probe.Surface.Num()==Surface.Num())for(int32 I=0;I<Surface.Num();++I)Changed|=FVector::DistSquared(Probe.Surface[I],Surface[I])>.000001;
    const bool Clock=Probe.Clip==ExpectedClip&&FMath::Abs(Seconds-Probe.Seconds)>.0001f;
    if(Probe.Clip!=ExpectedClip){Probe.Clip=ExpectedClip;Probe.Surface=Surface;}
    ++Probe.Samples;if(Clock)++Probe.ClockAdvances;if(Changed)++Probe.SurfaceAdvances;Probe.Seconds=Seconds;
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("real_native_own_skin_pose"));Row->SetStringField(TEXT("form"),SelectedForm);Row->SetNumberField(TEXT("item"),Candidates[Slot-1]->OriginalItemId);Row->SetStringField(TEXT("view"),FirstPerson?TEXT("FP"):TEXT("TP"));Row->SetStringField(TEXT("action"),Semantic.ToString());Row->SetStringField(TEXT("actual_skin"),ExpectedMesh->GetPathName());Row->SetStringField(TEXT("actual_clip"),ExpectedClip->GetPathName());Row->SetNumberField(TEXT("clip_seconds"),Seconds);Row->SetBoolField(TEXT("real_clock_advance"),Clock);Row->SetBoolField(TEXT("real_skin_local_deformation"),Changed);Row->SetNumberField(TEXT("actual_skinned_surface_samples"),Surface.Num());Row->SetNumberField(TEXT("pose_commit_count"),Presentation->GetPoseCommitCount());Observations.Add(MakeShared<FJsonValueObject>(Row));
}
bool AHeliosBioSupplyWeaponQA::PosePassed(bool FirstPerson,FName Semantic) const
{const auto* Probe=(FirstPerson?FPPose:TPPose).Find(Semantic);return Probe&&Probe->Samples>=2&&Probe->ClockAdvances>0&&Probe->SurfaceAdvances>0;}
void AHeliosBioSupplyWeaponQA::SampleCurrentActions()
{
    if(!bFormCase)return;
    ObserveNativeAV(TestWeapon.Get(),true);
    if(FormBot.IsValid())ObserveNativeAV(Cast<AHeliosBioSupplyWeapon>(FormBot->GetEquippedWeapon()),false);
    for(FName Action:{FName(TEXT("Idle")),FName(TEXT("Move")),FName(TEXT("Equip")),FName(TEXT("Prepare")),FName(TEXT("Fire")),FName(TEXT("ChangeClip")),FName(TEXT("Down"))})
    {
        SampleNativePose(Mode->Player,TestWeapon.Get(),true,Action);
        if(FormBot.IsValid())SampleNativePose(FormBot.Get(),Cast<AHeliosBioSupplyWeapon>(FormBot->GetEquippedWeapon()),false,Action);
    }
}
bool AHeliosBioSupplyWeaponQA::PrepareMove(ACharacter* Character)
{
    if(!Character||!Character->GetCharacterMovement()->IsMovingOnGround())return false;
    auto* Capsule=Character->GetCapsuleComponent();const float Feet=Character->GetActorLocation().Z-Capsule->GetScaledCapsuleHalfHeight();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BioSupplyQARealWalk),false,Character);TArray<AActor*> Attached;Character->GetAttachedActors(Attached,true,true);Query.AddIgnoredActors(Attached);
    for(int32 I=0;I<16;++I)
    {
        const FVector Direction=FVector(1,0,0).RotateAngleAxis(I*22.5f,FVector::UpVector),End=Character->GetActorLocation()+Direction*220;FHitResult Floor,Wall;
        if(!GetWorld()->LineTraceSingleByObjectType(Floor,FVector(End.X,End.Y,Feet+25),FVector(End.X,End.Y,Feet-25),FCollisionObjectQueryParams(ECC_WorldStatic),Query)||FMath::Abs(Floor.ImpactPoint.Z-Feet)>3||Floor.ImpactNormal.Z<.95f)continue;
        if(GetWorld()->SweepSingleByChannel(Wall,Character->GetActorLocation(),End,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()),Query))continue;
        if(!ContinuousFlatSourceSupport(GetWorld(),Character->GetActorLocation(),Direction,Feet,Query))continue;
        MoveDirection=Direction;MoveStart=Character->GetActorLocation();return true;
    }
    return false;
}
bool AHeliosBioSupplyWeaponQA::MeasureSole(AHeliosBioNPC* Bot,const FString& Phase)
{
    auto* Presentation=Bot?Bot->FindComponentByClass<UHeliosCharacterPresentationComponent>():nullptr;auto* Mesh=Presentation?Presentation->GetArmsMesh():nullptr;
    if(!Bot||!Mesh||!Mesh->GetSkeletalMeshAsset())return false;
    const auto* Render=Mesh->GetSkeletalMeshRenderData();auto* Weights=Mesh->GetSkinWeightBuffer(0);
    if(!Render||!Render->LODRenderData.IsValidIndex(0)||!Weights||!Weights->GetDataVertexBuffer()->GetWeightData())return false;
    TArray<FFinalSkinVertex> Vertices;Mesh->GetCPUSkinnedVertices(Vertices,0);const auto& LOD=Render->LODRenderData[0];FVector Sole=FVector::ZeroVector;float Lowest=MAX_flt;int32 FootVertices=0;
    for(const auto& Section:LOD.RenderSections)for(uint32 I=Section.BaseVertexIndex;I<Section.BaseVertexIndex+Section.NumVertices&&I<uint32(Vertices.Num());++I)
    {
        bool Foot=false;
        for(uint32 W=0;W<Weights->GetMaxBoneInfluences();++W)
        {
            const uint32 Local=Weights->GetBoneIndex(I,W);if(!Weights->GetBoneWeight(I,W)||!Section.BoneMap.IsValidIndex(Local))continue;
            const FString Name=Mesh->GetBoneName(Section.BoneMap[Local]).ToString();Foot|=Name.Contains(TEXT("foot"),ESearchCase::IgnoreCase)||Name.Contains(TEXT("toe"),ESearchCase::IgnoreCase)||Name.Contains(TEXT("ankle"),ESearchCase::IgnoreCase);
        }
        if(!Foot)continue;++FootVertices;const FVector World=Mesh->GetComponentTransform().TransformPosition(FVector(Vertices[I].Position));if(World.Z<Lowest){Lowest=World.Z;Sole=World;}
    }
    const float Feet=Bot->GetActorLocation().Z-Bot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();FCollisionQueryParams Query(SCENE_QUERY_STAT(BioSupplyQARealSole),false,Bot);TArray<AActor*> Attached;Bot->GetAttachedActors(Attached,true,true);Query.AddIgnoredActors(Attached);for(auto Other:Mode->Bots)Query.AddIgnoredActor(Other);Query.AddIgnoredActor(Mode->Player);
    FHitResult Floor;const bool Hit=FootVertices>0&&GetWorld()->LineTraceSingleByObjectType(Floor,FVector(Sole.X,Sole.Y,Feet+30),FVector(Sole.X,Sole.Y,Feet-150),FCollisionObjectQueryParams(ECC_WorldStatic),Query);
    const bool Passed=Hit&&Bot->GetCharacterMovement()->IsMovingOnGround()&&FMath::Abs(Lowest-Floor.ImpactPoint.Z)<=3;
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("actual_native_body_skinned_shoe_sole"));Row->SetStringField(TEXT("phase"),Phase);Row->SetStringField(TEXT("form"),SelectedForm);Row->SetStringField(TEXT("actual_body"),Mesh->GetSkeletalMeshAsset()->GetPathName());Row->SetNumberField(TEXT("actual_foot_weighted_vertices"),FootVertices);Row->SetBoolField(TEXT("source_static_floor_hit"),Hit);Row->SetBoolField(TEXT("actual_grounded"),Bot->GetCharacterMovement()->IsMovingOnGround());if(Hit){Row->SetNumberField(TEXT("sole_ground_gap_cm"),Lowest-Floor.ImpactPoint.Z);Row->SetStringField(TEXT("actual_sole_world"),Sole.ToString());Row->SetStringField(TEXT("source_floor_component"),GetPathNameSafe(Floor.GetComponent()));}Observations.Add(MakeShared<FJsonValueObject>(Row));Check(SelectedForm+TEXT("_")+Phase+TEXT("_actual_sole_within3cm"),Passed);return Passed;
}
bool AHeliosBioSupplyWeaponQA::PlaceOpponent(ACharacter* Human,AHeliosBioNPC* Enemy,float Distance,bool NearMelee)
{
    if(!Human||!Enemy)return false;const float Feet=Human->GetActorLocation().Z-Human->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BioSupplyQARealOpponent),false,Human);Query.AddIgnoredActor(Enemy);TArray<AActor*> Attached;Human->GetAttachedActors(Attached,true,true);Query.AddIgnoredActors(Attached);
    for(int32 I=0;I<16;++I)
    {
        const FVector Wanted=Human->GetActorLocation()+FVector(Distance,0,0).RotateAngleAxis(I*22.5f,FVector::UpVector);FHitResult Floor,Wall;
        if(!GetWorld()->LineTraceSingleByObjectType(Floor,FVector(Wanted.X,Wanted.Y,Feet+25),FVector(Wanted.X,Wanted.Y,Feet-25),FCollisionObjectQueryParams(ECC_WorldStatic),Query)||FMath::Abs(Floor.ImpactPoint.Z-Feet)>3||Floor.ImpactNormal.Z<.95f)continue;
        const FVector At(Wanted.X,Wanted.Y,Floor.ImpactPoint.Z+Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2);
        if(GetWorld()->OverlapBlockingTestByChannel(At,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius(),Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),Query))continue;
        if(NearMelee&&Mode->AreOpponents(Enemy->ParticipantId,Mode->ParticipantId(Human)))
        {
            bool AnotherCloser=false;const float HumanDistance=FVector::Dist(At,Human->GetActorLocation());
            for(const auto& P:Mode->Model.GetParticipants())if(P.bAlive&&P.Id!=Mode->ParticipantId(Human)&&Mode->AreOpponents(Enemy->ParticipantId,P.Id))if(auto* Other=Mode->ParticipantActor(P.Id))AnotherCloser|=FVector::Dist(At,Other->GetActorLocation())<HumanDistance+10;
            if(AnotherCloser)continue;
        }
        FVector Eye;FRotator Rotation;Human->GetActorEyesViewPoint(Eye,Rotation);
        if(GetWorld()->LineTraceSingleByChannel(Wall,Eye,At+FVector(0,0,25),ECC_Visibility,Query))continue;
        if(!NearMelee)
        {
            auto* Gun=Cast<AHeliosBioSupplyWeapon>(Cast<AHeliosBioNPC>(Human)?Cast<AHeliosBioNPC>(Human)->GetEquippedWeapon():Mode->Player->GetEquippedWeapon());FVector Muzzle;
            if(!Gun||!Gun->GetNativeMuzzle(Muzzle)||GetWorld()->SweepSingleByChannel(Wall,Eye,Muzzle,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(3.f),Query)||GetWorld()->SweepSingleByChannel(Wall,Muzzle,At+FVector(0,0,35),FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(3.f),Query))continue;
        }
        const FVector Before=Enemy->GetActorLocation();Enemy->SetActorLocation(At,false,nullptr,ETeleportType::TeleportPhysics);Enemy->bQAFreeze=true;Enemy->bQAHoldDecisions=false;
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),NearMelee?TEXT("real_same_floor_mother_melee_fixture"):TEXT("real_same_floor_native_enemy_fixture"));Row->SetStringField(TEXT("before"),Before.ToString());Row->SetStringField(TEXT("after"),At.ToString());Row->SetNumberField(TEXT("participant"),Enemy->ParticipantId);Row->SetNumberField(TEXT("distance_cm"),Distance);Row->SetStringField(TEXT("actual_static_floor"),GetPathNameSafe(Floor.GetComponent()));Row->SetBoolField(TEXT("same_level_actual_capsule_and_weapon_corridor_clear"),true);Observations.Add(MakeShared<FJsonValueObject>(Row));return true;
    }
    return false;
}
bool AHeliosBioSupplyWeaponQA::BeginNPCItem(double Now)
{
    auto* Bot=FormBot.Get();auto* Inventory=Bot?Bot->GetRoundInventory():nullptr;if(!Inventory)return false;
    Bot->bQAFreeze=false;Bot->bQAHoldDecisions=true;Inventory->ResetRound(false);
    FString Error;if(!Inventory->CanGrant(Candidates[Slot-1],&Error)||!Inventory->TryGrant(Candidates[Slot-1])){Blocker=TEXT("Actual own-form NPC acquisition rejected: ")+Error;return false;}
    TPPose.Reset();bNPCEmptySeen=bNPCReloadSeen=bNPCDamageSeen=bNPCSelectedNaturally=false;
    if(!SetNPCInspectionCamera()){Blocker=TEXT("No real unobstructed NPC inspection camera corridor");return false;}
    Stage=200;StageAt=Now;return true;
}
void AHeliosBioSupplyWeaponQA::CapturePreservedInventory(UHeliosBioRoundInventoryComponent* Inventory)
{
    PreservedWeapons.Reset();PreservedAmmo.Reset();PreservedReserve.Reset();InterruptedSlot=Inventory->GetActiveSlot();
    for(int32 I=0;I<5;++I){auto* Gun=Inventory->GetWeapon(I);PreservedWeapons.Add(Gun);PreservedAmmo.Add(Gun?Gun->GetBulletCount():-1);PreservedReserve.Add(Inventory->GetReserve(I));}
}
bool AHeliosBioSupplyWeaponQA::SamePreservedInventory(UHeliosBioRoundInventoryComponent* Inventory) const
{
    if(!Inventory||Inventory->GetActiveSlot()!=InterruptedSlot||PreservedAmmo.Num()!=5)return false;
    for(int32 I=0;I<5;++I)
    {
        auto* Gun=Inventory->GetWeapon(I);
        if(Gun!=PreservedWeapons[I].Get()||(Gun?Gun->GetBulletCount():-1)!=PreservedAmmo[I]||Inventory->GetReserve(I)!=PreservedReserve[I])return false;
    }
    return true;
}
void AHeliosBioSupplyWeaponQA::TickNPCItem(double Now,float Dt)
{
    (void)Dt;
    auto* Bot=FormBot.Get();auto* Inventory=Bot?Bot->GetRoundInventory():nullptr;auto* Gun=Inventory?Cast<AHeliosBioSupplyWeapon>(Inventory->GetWeapon(Slot)):nullptr;
    if(!Bot||!Gun||!Victim.IsValid()||!OtherMother.IsValid()){Finish(TEXT("Real own-form NPC weapon or mothers disappeared"));return;}
    const auto* D=Candidates[Slot-1].Get();const auto* Human=Mode->Model.FindParticipant(Bot->ParticipantId);
    const auto* Enemy=Mode->Model.FindParticipant(Victim->ParticipantId);const auto* Other=Mode->Model.FindParticipant(OtherMother->ParticipantId);
    const auto Fail=[&](const TCHAR* Why){Check(SelectedForm+TEXT("_NPC_item")+FString::FromInt(D->OriginalItemId)+TEXT("_real_window_completed"),false);Finish(Why);};
    if(Stage==200)
    {
        if(Gun->IsNativeActionBusy()){if(Now-StageAt>8)Fail(TEXT("Actual NPC native Equip action did not recover"));return;}
        if(!PrepareMove(Bot)){Fail(TEXT("No actual same-level220cm capsule-clear NPC native movement lane"));return;}
        Stage=201;StageAt=Now;return;
    }
    if(Stage==201)
    {
        if(Now-StageAt<.6){Bot->AddMovementInput(MoveDirection);return;}
        Check(SelectedForm+TEXT("_NPC_item")+FString::FromInt(D->OriginalItemId)+TEXT("_actual_Move_travel_and_native_skin"),FVector::Dist2D(MoveStart,Bot->GetActorLocation())>40&&PosePassed(false,TEXT("Move")));
        Stage=202;StageAt=Now;return;
    }
    if(Stage==202)
    {
        if(Bot->GetVelocity().Size2D()>10){if(Now-StageAt>3)Fail(TEXT("NPC actual walk never settled after input ended"));return;}
        if(Now-StageAt<.5)return;
        MeasureSole(Bot,TEXT("item")+FString::FromInt(D->OriginalItemId)+TEXT("_standing_native_Idle"));
        if(!SetNPCInspectionCamera()){Fail(TEXT("Actual post-move NPC render viewpoint is obstructed"));return;}
        const float Distance=Slot==2?1400.f:Slot==3?650.f:1200.f;
        if(!PlaceOpponent(Bot,Victim.Get(),Distance)||!PlaceOpponent(Victim.Get(),OtherMother.Get(),200.f,true)){Fail(TEXT("No real supported native NPC muzzle/cluster lane"));return;}
        Stage=203;StageAt=Now;return;
    }
    if(Stage==203)
    {
        // A previous Player concussion must expire on actual match time before
        // production selection can choose the grenade again.
        if(Enemy->FrozenSeconds>0||Other->FrozenSeconds>0){if(Now-StageAt>8)Fail(TEXT("Actual prior concussion did not expire"));return;}
        NPCHealthBefore=Enemy->Health+Other->Health;NPCFrozenBefore=FMath::Max(Enemy->FrozenSeconds,Other->FrozenSeconds);NPCDamageBefore=Human->DamageDealt;
        NPCShotsBefore=Gun->GetNativeShotCount();NPCHitsBefore=Gun->GetNativeHitCount();NPCAmmoBefore=Gun->GetBulletCount();NPCReserveBefore=Inventory->GetReserve(Slot);
        Bot->bQAHoldDecisions=false;Stage=204;StageAt=Now;return;
    }
    if(Stage==204||Stage==205)
    {
        const bool ActualEffect=D->FireType==EHeliosBioSupplyFireType::ConcussionProjectile?FMath::Max(Enemy->FrozenSeconds,Other->FrozenSeconds)>NPCFrozenBefore:Enemy->Health+Other->Health<NPCHealthBefore&&Human->DamageDealt>NPCDamageBefore;
        bNPCDamageSeen|=ActualEffect&&Gun->GetNativeHitCount()>NPCHitsBefore;
        if(Stage==204&&Gun->GetNativeShotCount()>NPCShotsBefore)
        {
            const auto* Target=Bot->GetDesiredCombatTarget();bNPCSelectedNaturally=Inventory->GetActiveSlot()==Slot&&Bot->GetEquippedWeapon()==Gun&&(Target==Victim.Get()||Target==OtherMother.Get());
            Bot->bQAHoldDecisions=true;Stage=205;
        }
        if(Stage==205&&Gun->GetBulletCount()==0&&!bNPCEmptySeen)
        {bNPCEmptySeen=true;NPCEmptyShots=Gun->GetNativeShotCount();NPCEmptyReserve=Inventory->GetReserve(Slot);Gun->StopFiring();Observe(TEXT("actual_NPC_magazine_exhausted_without_ammo_injection"));}
        if(bNPCEmptySeen)
        {
            if(Slot==3&&(Enemy->FrozenSeconds>0||Other->FrozenSeconds>0||Gun->IsNativeThrowBusy()))return;
            Bot->bQAHoldDecisions=false;
            if(Bot->GetReloadRemaining()>0&&Bot->GetEquippedWeapon()==Gun)
            {bNPCReloadSeen=true;Bot->bQAHoldDecisions=true;TPPose.Remove(Slot==3?FName(TEXT("Equip")):FName(TEXT("ChangeClip")));Stage=206;ReloadStartedAt=Now;StageAt=Now;return;}
        }
        if(Now-StageAt>FMath::Max(15.f,NPCAmmoBefore*D->RefireSeconds+D->ReloadSeconds+D->ProjectileLifetime+12))Fail(TEXT("Production native NPC selection/fire/finite empty-mag reload did not progress"));return;
    }
    if(Stage==206)
    {
        if(Bot->GetReloadRemaining()>0){if(Now-StageAt>D->ReloadSeconds+3)Fail(TEXT("Actual NPC reload did not finish on world time"));return;}
        Gun->StopFiring();const int32 Loaded=NPCEmptyReserve-Inventory->GetReserve(Slot);
        const bool Conserved=NPCAmmoBefore+NPCReserveBefore-Gun->GetBulletCount()-Inventory->GetReserve(Slot)==Gun->GetNativeShotCount()-NPCShotsBefore;
        Check(SelectedForm+TEXT("_NPC_item")+FString::FromInt(D->OriginalItemId)+TEXT("_production_choice_actual_hit_damage_ammo"),bNPCSelectedNaturally&&bNPCDamageSeen&&Conserved);
        Check(SelectedForm+TEXT("_NPC_item")+FString::FromInt(D->OriginalItemId)+TEXT("_natural_empty_actual_reload_conservation"),bNPCEmptySeen&&bNPCReloadSeen&&Now-ReloadStartedAt>D->ReloadSeconds-.15&&Loaded==D->MagazineSize&&Gun->GetBulletCount()==Loaded&&Gun->GetNativeShotCount()==NPCEmptyShots);
        Check(SelectedForm+TEXT("_NPC_item")+FString::FromInt(D->OriginalItemId)+TEXT("_actual_reload_source_clock_and_own_skin"),PosePassed(false,Slot==3?FName(TEXT("Equip")):FName(TEXT("ChangeClip"))));
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("NPC_native_finite_fire_reload_result"));Row->SetStringField(TEXT("form"),SelectedForm);Row->SetNumberField(TEXT("item"),D->OriginalItemId);Row->SetNumberField(TEXT("actual_native_shot_delta"),Gun->GetNativeShotCount()-NPCShotsBefore);Row->SetNumberField(TEXT("actual_native_hit_delta"),Gun->GetNativeHitCount()-NPCHitsBefore);Row->SetNumberField(TEXT("actual_victims_HP_loss"),NPCHealthBefore-Enemy->Health-Other->Health);Row->SetNumberField(TEXT("actual_attributed_damage"),Human->DamageDealt-NPCDamageBefore);Row->SetBoolField(TEXT("actual_Think_selected_source_item"),bNPCSelectedNaturally);Row->SetBoolField(TEXT("exact_magazine_reserve_shot_conservation"),Conserved);Row->SetStringField(TEXT("actual_AI_movement_diagnostic"),Bot->GetMovementDiagnostic());Observations.Add(MakeShared<FJsonValueObject>(Row));
        CapturePreservedInventory(Inventory);DownSequenceBefore=Bot->GetHumanDownCommit().Sequence;DownShotsBefore=Gun->GetNativeShotCount();
        if(auto* Presentation=Bot->FindComponentByClass<UHeliosCharacterPresentationComponent>()){DownPoseCountBefore=Presentation->GetPoseCommitCount();DownFrameBefore=Presentation->GetPoseCommitFrame();}
        MotherMeleeBefore=Victim->MeleeAttacks;MotherInfectionsBefore=Enemy->Infections;bNPCInfectionSeen=false;bNPCStoppedAfterInfection=true;
        if(!PlaceOpponent(Bot,Victim.Get(),80,true)){Fail(TEXT("No same-floor80cm real mother melee position"));return;}
        Victim->bQAFreeze=false;Stage=207;StageAt=Now;return;
    }
    if(Stage==207)
    {
        if(Human->Team==EHeliosBioTeam::Zombie)
        {
            bNPCInfectionSeen=Victim->MeleeAttacks>MotherMeleeBefore&&Enemy->Infections>MotherInfectionsBefore;
            Check(SelectedForm+TEXT("_NPC_item")+FString::FromInt(D->OriginalItemId)+TEXT("_actual_mother_melee_infection"),bNPCInfectionSeen);
            Victim->bQAFreeze=true;Stage=208;StageAt=Now;return;
        }
        if(Now-StageAt>10)Fail(TEXT("Actual mother did not melee-infect the tested NPC"));return;
    }
    if(Stage==208)
    {
        bNPCStoppedAfterInfection&=Gun->GetNativeShotCount()==DownShotsBefore&&Bot->GetReloadRemaining()==0;
        if(Bot->IsHumanInfectionTransitionBlocked()){Fail(TEXT("Production Down transition is explicitly blocked"));return;}
        if(Bot->IsHumanInfectionTransition()){if(Now-StageAt>8)Fail(TEXT("Production Down final handoff did not complete"));return;}
        const auto& Commit=Bot->GetHumanDownCommit();const auto* Pair=SourcePair();const auto* Ref=Pair?Pair->ThirdPersonClips.Find(TEXT("Down")):nullptr;auto* Down=Ref?Ref->LoadSynchronous():nullptr;
        const bool Final=Commit.Sequence==DownSequenceBefore+1&&Commit.Clip.Get()==Down&&Pair&&Commit.Body.Get()==Pair->ThirdPersonMesh.LoadSynchronous()&&Commit.Duration>0&&FMath::IsNearlyEqual(Commit.SampleSeconds,Commit.Duration,.0001f)&&Commit.PoseCommitCount>DownPoseCountBefore&&Commit.PoseCommitFrame>DownFrameBefore;
        Check(SelectedForm+TEXT("_NPC_item")+FString::FromInt(D->OriginalItemId)+TEXT("_native_Down_real_clock_surface_final_commit"),bNPCInfectionSeen&&PosePassed(false,TEXT("Down"))&&Final);
        Check(SelectedForm+TEXT("_NPC_item")+FString::FromInt(D->OriginalItemId)+TEXT("_infection_stops_actual_former_native_gun"),bNPCStoppedAfterInfection);
        auto* Caster=const_cast<FHeliosBioParticipant*>(Mode->Model.FindParticipant(SerumCaster->ParticipantId));
        Caster->Team=EHeliosBioTeam::UltimateHunter;Caster->HunterRoleId=TEXT("BioRedeemerHunter");Caster->FrozenSeconds=0;Caster->bAlive=true;Caster->bPermanentlyDead=false;
        const FVector Before=Bot->GetActorLocation();const auto Result=Mode->Model.ApplyRescueSerum(Bot->ParticipantId,Caster->Id);bNPCModelRestore=Result==EHeliosBioSerumResult::CuredZombie;
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("explicit_shared_cure_rule_inventory_restore_transaction"));Row->SetNumberField(TEXT("target"),Bot->ParticipantId);Row->SetNumberField(TEXT("rule_result"),int32(Result));Row->SetStringField(TEXT("before_location"),Before.ToString());Row->SetStringField(TEXT("scope"),TEXT("Caster identity is an explicit isolated permission fixture; invokes the production shared serum rule to test inventory restoration. This is not a projectile-hit or original serum-input acceptance claim."));Observations.Add(MakeShared<FJsonValueObject>(Row));
        Stage=209;StageAt=Now;return;
    }
    if(Stage==209)
    {
        if(Now-StageAt<.3)return;
        const auto* Presentation=Bot->FindComponentByClass<UHeliosCharacterPresentationComponent>();const auto* Pair=SourcePair();
        Check(SelectedForm+TEXT("_NPC_item")+FString::FromInt(D->OriginalItemId)+TEXT("_cure_restores_own_instances_ammo_and_body"),bNPCModelRestore&&Human->Team==EHeliosBioTeam::Human&&SamePreservedInventory(Inventory)&&Presentation&&Pair&&Presentation->GetArmsMesh()->GetSkeletalMeshAsset()==Pair->ThirdPersonMesh.LoadSynchronous()&&Bot->GetHumanLoadout().CharacterId==FormBaseline.CharacterId&&Bot->GetHumanLoadout().CharacterVariantId==FormBaseline.CharacterVariantId);
        MeasureSole(Bot,TEXT("item")+FString::FromInt(D->OriginalItemId)+TEXT("_cured_own_body"));
        Bot->bQAFreeze=true;Bot->bQAHoldDecisions=false;++CompletedNPCItems;CompleteItemWindow(Now);return;
    }
}
bool AHeliosBioSupplyWeaponQA::BeginTouchPickup(double Now)
{
    auto* Player=Mode->Player.Get();const float Feet=Player->GetActorLocation().Z-Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();FCollisionQueryParams Query(SCENE_QUERY_STAT(BioSupplyQAAutoTouch),false,Player);TArray<AActor*> Attached;Player->GetAttachedActors(Attached,true,true);Query.AddIgnoredActors(Attached);
    for(int32 I=0;I<16;++I)
    {
        const FVector Direction=FVector(1,0,0).RotateAngleAxis(I*22.5f,FVector::UpVector),Wanted=Player->GetActorLocation()+Direction*220;FHitResult Floor,Wall;
        if(!GetWorld()->LineTraceSingleByObjectType(Floor,FVector(Wanted.X,Wanted.Y,Feet+25),FVector(Wanted.X,Wanted.Y,Feet-25),FCollisionObjectQueryParams(ECC_WorldStatic),Query)||FMath::Abs(Floor.ImpactPoint.Z-Feet)>3||Floor.ImpactNormal.Z<.95f)continue;
        if(GetWorld()->SweepSingleByChannel(Wall,Player->GetActorLocation(),Wanted,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Player->GetCapsuleComponent()->GetScaledCapsuleRadius(),Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),Query))continue;
        if(!ContinuousFlatSourceSupport(GetWorld(),Player->GetActorLocation(),Direction,Feet,Query))continue;
        const FVector At(Wanted.X,Wanted.Y,Floor.ImpactPoint.Z+2);bool Occupied=false;
        for(auto Bot:Mode->Bots)if(Bot&&Mode->Model.FindParticipant(Bot->ParticipantId)->bAlive)Occupied|=FVector::DistSquared(At,Bot->GetActorLocation())<FMath::Square(165.f);
        if(Occupied)continue;
        auto* Supply=GetWorld()->SpawnActor<AHeliosBioSupply>(At,FRotator::ZeroRotator);if(!Supply)continue;Supply->SetKind(EHeliosBioSupplyKind::Normal);Supply->SetWeaponContent(Candidates[Slot-1]);FixtureSupplies.Add(Supply);
        if(Supply->CanReachPickup(Player)){Supply->Destroy();continue;}
        TouchSupply=Supply;MoveDirection=Direction;MoveStart=Player->GetActorLocation();TouchScoreBefore=Mode->Model.FindParticipant(0)->SuppliesCollected;TouchReserveBefore=Player->GetRoundInventory()->GetReserve(0);
        Stage=21;StageAt=Now;return true;
    }
    return false;
}
void AHeliosBioSupplyWeaponQA::CompleteInitialAcquisition(double Now)
{
    auto* Inventory=Mode->Player->GetRoundInventory();TSet<AShooterWeapon*> Instances;
    for(int32 I=0;I<5;++I)Instances.Add(Inventory->GetWeapon(I));Check(TEXT("five_distinct_actual_weapon_instances_coexist"),Instances.Num()==5&&!Instances.Contains(nullptr));
    for(int32 I=0;I<5;++I)Check(FString::Printf(TEXT("slot%d_shared_Select_uses_actual_instance"),I),Inventory->Select(I)&&Mode->Player->GetEquippedWeapon()==Inventory->GetWeapon(I));
    if(bFormCase){CheckInventoryTransactions();return;}Slot=1;if(!BeginNativeItem(Now))Finish(TEXT("First acquired native item cannot be equipped"));
}
void AHeliosBioSupplyWeaponQA::TickTouchPickup(double Now)
{
    if(Stage==20)
    {
        if(!BeginTouchPickup(Now)){Check(TEXT("real220cm_walk_to_automatic_supply_lane_available"),false);Finish(TEXT("No supported capsule-clear220cm real walk-in supply lane outside all initial pickup radii"));}return;
    }
    auto* Supply=TouchSupply.Get();auto* Inventory=Mode->Player->GetRoundInventory();
    if(!Supply){Finish(TEXT("Actual automatic-touch box disappeared before transaction observation"));return;}
    if(Stage==21)
    {
        if(!Supply->IsAvailable())
        {
            auto* Gun=Cast<AHeliosBioSupplyWeapon>(Inventory->GetWeapon(Slot));const bool Passed=Gun&&Inventory->GetActiveSlot()==Slot&&Mode->Player->GetEquippedWeapon()==Gun&&Mode->Model.FindParticipant(0)->SuppliesCollected==TouchScoreBefore+1&&Inventory->GetReserve(Slot)==Candidates[Slot-1]->PickupReserveAmmo&&Inventory->GetReserve(0)==FMath::Min(1800,TouchReserveBefore+300)&&FVector::Dist2D(MoveStart,Mode->Player->GetActorLocation())>40&&Mode->Player->GetCharacterMovement()->IsMovingOnGround()&&Supply->IsActorTickEnabled();
            Check(FString::Printf(TEXT("item%d_actual_walk_in_production_supply_tick_auto_equips"),Candidates[Slot-1]->OriginalItemId),Passed);
            auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("normal_input_motor_walk_into_actual_supply_auto_tick"));Row->SetNumberField(TEXT("item"),Candidates[Slot-1]->OriginalItemId);Row->SetStringField(TEXT("walk_start"),MoveStart.ToString());Row->SetStringField(TEXT("actual_player_after"),Mode->Player->GetActorLocation().ToString());Row->SetNumberField(TEXT("actual_walk_distance_cm"),FVector::Dist2D(MoveStart,Mode->Player->GetActorLocation()));Row->SetStringField(TEXT("actual_box"),Supply->GetPathName());Row->SetBoolField(TEXT("actor_tick_was_enabled"),Supply->IsActorTickEnabled());Row->SetBoolField(TEXT("QA_called_PickupSupply_for_this_box"),false);Row->SetBoolField(TEXT("real_auto_touch_equip_and_rule_score"),Passed);Observations.Add(MakeShared<FJsonValueObject>(Row));if(Passed)++AutomaticTouchItems;Stage=22;StageAt=Now;return;
        }
        if(Now-StageAt>4||FVector::Dist2D(MoveStart,Mode->Player->GetActorLocation())>200){Check(TEXT("real_auto_touch_pickup_occurred_before_lane_end"),false);Finish(TEXT("Actual moving Player did not auto-pick the native box; no direct pickup substitute accepted"));return;}
        Mode->Player->AddMovementInput(MoveDirection);return;
    }
    if(Stage==22)
    {
        if(Mode->Player->GetVelocity().Size2D()>10){if(Now-StageAt>3)Finish(TEXT("Actual walk-in pickup movement did not settle"));return;}
        ++Slot;if(Slot<5){Stage=20;StageAt=Now;}else CompleteInitialAcquisition(Now);
    }
}
void AHeliosBioSupplyWeaponQA::CheckInventoryTransactions()
{
    auto* Inventory=Mode->Player->GetRoundInventory();auto* Original=Inventory->GetWeapon(1);const int32 Reserve=Inventory->GetReserve(1),OtherReserve=Inventory->GetReserve(2),OtherAmmo=Inventory->GetWeapon(2)->GetBulletCount(),BaselineReserve=Inventory->GetReserve(0);
    auto* Duplicate=MakeSupply(Candidates[0]);const int32 Score=Mode->Model.FindParticipant(0)->SuppliesCollected;
    const bool DuplicateGranted=Duplicate&&Mode->PickupSupply(0,Duplicate);
    bDuplicateTransaction=DuplicateGranted&&Inventory->GetWeapon(1)==Original&&Inventory->GetReserve(1)==FMath::Min(Candidates[0]->MaximumReserveAmmo,Reserve+Candidates[0]->PickupReserveAmmo)&&Inventory->GetReserve(2)==OtherReserve&&Inventory->GetWeapon(2)->GetBulletCount()==OtherAmmo&&Inventory->GetReserve(0)==FMath::Min(1800,BaselineReserve+300)&&Mode->Model.FindParticipant(0)->SuppliesCollected==Score+1;
    Check(TEXT("duplicate_production_transaction_retains_instance_and_only_its_reserve"),bDuplicateTransaction);
    if(auto* Competitor=SerumCaster.Get())if(auto* Supply=MakeSupply(Candidates[0]))
    {
        const float Feet=Mode->Player->GetActorLocation().Z-Mode->Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();FCollisionQueryParams Query(SCENE_QUERY_STAT(BioSupplyQACompetition),false,Competitor);
        for(int32 I=0;I<16;++I)
        {
            const FVector Wanted=Supply->GetActorLocation()+FVector(80,0,0).RotateAngleAxis(I*22.5f,FVector::UpVector);FHitResult Floor;
            if(!GetWorld()->LineTraceSingleByObjectType(Floor,FVector(Wanted.X,Wanted.Y,Feet+25),FVector(Wanted.X,Wanted.Y,Feet-25),FCollisionObjectQueryParams(ECC_WorldStatic),Query)||FMath::Abs(Floor.ImpactPoint.Z-Feet)>3||Floor.ImpactNormal.Z<.95f)continue;
            const FVector At(Wanted.X,Wanted.Y,Floor.ImpactPoint.Z+Competitor->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2);
            if(GetWorld()->OverlapBlockingTestByChannel(At,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Competitor->GetCapsuleComponent()->GetScaledCapsuleRadius(),Competitor->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),Query))continue;
            const FVector Before=Competitor->GetActorLocation();Competitor->SetActorLocation(At,false,nullptr,ETeleportType::TeleportPhysics);
            if(!Supply->CanReachPickup(Competitor)||!Supply->CanReachPickup(Mode->Player))continue;
            CompetitionSupply=Supply;CompetitionPlayerScore=Mode->Model.FindParticipant(0)->SuppliesCollected;CompetitionOtherScore=Mode->Model.FindParticipant(Competitor->ParticipantId)->SuppliesCollected;
            CompetitionActiveSlotBefore=Inventory->GetActiveSlot();CompetitionReserveBefore=Inventory->GetReserve(1);CompetitionBaselineReserveBefore=Inventory->GetReserve(0);
            auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("two_actual_eligible_participants_before_automatic_supply_tick"));Row->SetStringField(TEXT("competitor_before"),Before.ToString());Row->SetStringField(TEXT("competitor_supported_after"),At.ToString());Row->SetStringField(TEXT("actual_static_support"),GetPathNameSafe(Floor.GetComponent()));Row->SetBoolField(TEXT("both_actual_CanReachPickup"),true);Row->SetBoolField(TEXT("QA_calls_PickupSupply_for_competition"),false);Observations.Add(MakeShared<FJsonValueObject>(Row));
            Supply->SetActorTickEnabled(true);Stage=24;StageAt=GetWorld()->GetTimeSeconds();return;
        }
    }
    Check(TEXT("real_two_participant_auto_tick_competition_fixture_available"),false);Finish(TEXT("No real same-floor clear position with two eligible participants for automatic supply competition"));
}
bool AHeliosBioSupplyWeaponQA::BeginAutomaticReject(bool DifferentFloor,double Now)
{
    for(auto Bot:Mode->Bots)if(Bot&&Mode->Model.FindParticipant(Bot->ParticipantId)->Team==EHeliosBioTeam::Human)
    {
        const float Feet=Bot->GetActorLocation().Z-Bot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();FCollisionQueryParams Query(SCENE_QUERY_STAT(BioSupplyQAAutomaticReject),false,Bot);for(const auto& P:Mode->Model.GetParticipants())if(auto* Actor=Mode->ParticipantActor(P.Id))Query.AddIgnoredActor(Actor);
        for(int32 I=0;I<32;++I)
        {
            const FVector Direction=FVector(1,0,0).RotateAngleAxis(I*11.25f,FVector::UpVector);FVector At;FHitResult Wall,Floor;
            if(DifferentFloor)
            {
                const FVector XY=Bot->GetActorLocation()+Direction*90;
                if(!GetWorld()->LineTraceSingleByObjectType(Floor,FVector(XY.X,XY.Y,Feet+140),FVector(XY.X,XY.Y,Feet+66),FCollisionObjectQueryParams(ECC_WorldStatic),Query)||Floor.ImpactNormal.Z<.95f)continue;
                At=FVector(XY.X,XY.Y,Floor.ImpactPoint.Z+2);
            }
            else
            {
                const FVector Start=FVector(Bot->GetActorLocation().X,Bot->GetActorLocation().Y,Feet+40);
                if(!GetWorld()->LineTraceSingleByObjectType(Wall,Start,Start+Direction*120,FCollisionObjectQueryParams(ECC_WorldStatic),Query)||FMath::Abs(Wall.ImpactNormal.Z)>.3f)continue;
                const FVector Beyond=Wall.ImpactPoint+Direction*20;
                if(!GetWorld()->LineTraceSingleByObjectType(Floor,FVector(Beyond.X,Beyond.Y,Feet+25),FVector(Beyond.X,Beyond.Y,Feet-25),FCollisionObjectQueryParams(ECC_WorldStatic),Query)||FMath::Abs(Floor.ImpactPoint.Z-Feet)>3||Floor.ImpactNormal.Z<.95f)continue;
                At=FVector(Beyond.X,Beyond.Y,Floor.ImpactPoint.Z+2);
            }
            if(FVector::DistSquared(Bot->GetActorLocation(),At)>=22500)continue;
            const FVector ProductionStart=FVector(Bot->GetActorLocation().X,Bot->GetActorLocation().Y,Feet+FMath::Min(90.f,Bot->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()*1.5f));FHitResult ExactWall;
            const bool VisibilityBlocked=GetWorld()->LineTraceSingleByChannel(ExactWall,ProductionStart,At+FVector(0,0,40),ECC_Visibility,Query);
            if(DifferentFloor?VisibilityBlocked:!VisibilityBlocked)continue;
            auto* Supply=GetWorld()->SpawnActor<AHeliosBioSupply>(At,FRotator::ZeroRotator);if(!Supply)continue;Supply->SetActorTickEnabled(false);Supply->SetKind(EHeliosBioSupplyKind::Normal);Supply->SetWeaponContent(Candidates[0]);FixtureSupplies.Add(Supply);
            bool OtherCanReach=false;for(const auto& P:Mode->Model.GetParticipants())if(P.bAlive&&P.Id!=Bot->ParticipantId)OtherCanReach|=Supply->CanReachPickup(Mode->ParticipantActor(P.Id));
            if(OtherCanReach||Supply->CanReachPickup(Bot)){Supply->Destroy();continue;}
            RejectSupply=Supply;RejectParticipant=Bot->ParticipantId;RejectScore=Mode->Model.FindParticipant(RejectParticipant)->SuppliesCollected;RejectReserve=Bot->GetRoundInventory()->GetReserve(0);
            auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),DifferentFloor?TEXT("actual_different_floor_automatic_pickup_rejection_fixture"):TEXT("actual_original_wall_automatic_pickup_rejection_fixture"));Row->SetNumberField(TEXT("participant"),RejectParticipant);Row->SetNumberField(TEXT("actual_center_to_box_cm"),FVector::Dist(Bot->GetActorLocation(),At));Row->SetNumberField(TEXT("actual_support_height_delta_cm"),Floor.ImpactPoint.Z-Feet);Row->SetStringField(TEXT("actual_static_support"),GetPathNameSafe(Floor.GetComponent()));Row->SetStringField(TEXT("actual_pickup_LOS_blocker"),GetPathNameSafe(ExactWall.GetComponent()));Row->SetBoolField(TEXT("exact_production_pickup_visibility_blocked"),VisibilityBlocked);Row->SetBoolField(TEXT("QA_calls_PickupSupply_for_rejection"),false);Observations.Add(MakeShared<FJsonValueObject>(Row));
            Supply->SetActorTickEnabled(true);Stage=DifferentFloor?27:26;StageAt=Now;return true;
        }
    }
    return false;
}
void AHeliosBioSupplyWeaponQA::TickInventoryTransactions(double Now)
{
    if(Stage==24)
    {
        if(!CompetitionSupply.IsValid()){Finish(TEXT("Real competition supply disappeared"));return;}
        if(CompetitionSupply->IsAvailable()){if(Now-StageAt>2){Check(TEXT("production_supply_tick_committed_real_competition"),false);Finish(TEXT("Automatic production supply actor did not resolve two eligible participants"));}return;}
        auto* Inventory=Mode->Player->GetRoundInventory();
        // This box contains an already owned slot. Production TryGrant adds
        // reserve and retains the previously selected weapon for duplicates.
        bCompetitionAuto=Mode->Model.FindParticipant(0)->SuppliesCollected==CompetitionPlayerScore+1&&Mode->Model.FindParticipant(SerumCaster->ParticipantId)->SuppliesCollected==CompetitionOtherScore&&Inventory->GetActiveSlot()==CompetitionActiveSlotBefore&&Inventory->GetReserve(1)==FMath::Min(Candidates[0]->MaximumReserveAmmo,CompetitionReserveBefore+Candidates[0]->PickupReserveAmmo)&&Inventory->GetReserve(0)==FMath::Min(1800,CompetitionBaselineReserveBefore+300);
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("actual_automatic_competition_owned_slot_result"));Row->SetNumberField(TEXT("active_slot_before"),CompetitionActiveSlotBefore);Row->SetNumberField(TEXT("active_slot_after"),Inventory->GetActiveSlot());Row->SetNumberField(TEXT("player_actual_score_delta"),Mode->Model.FindParticipant(0)->SuppliesCollected-CompetitionPlayerScore);Row->SetNumberField(TEXT("competitor_actual_score_delta"),Mode->Model.FindParticipant(SerumCaster->ParticipantId)->SuppliesCollected-CompetitionOtherScore);Row->SetNumberField(TEXT("owned_slot_reserve_before"),CompetitionReserveBefore);Row->SetNumberField(TEXT("owned_slot_reserve_after"),Inventory->GetReserve(1));Row->SetNumberField(TEXT("baseline_reserve_before"),CompetitionBaselineReserveBefore);Row->SetNumberField(TEXT("baseline_reserve_after"),Inventory->GetReserve(0));Row->SetBoolField(TEXT("duplicate_pickup_retains_active_slot_and_exact_reserves"),bCompetitionAuto);Observations.Add(MakeShared<FJsonValueObject>(Row));
        Check(TEXT("production_automatic_tick_single_winner_two_eligible_participants"),bCompetitionAuto);RepeatedReserve=Mode->Player->GetRoundInventory()->GetReserve(1);Stage=25;StageAt=Now;return;
    }
    if(Stage==25)
    {
        if(Now-StageAt<.6)return;
        bRepeatAuto=CompetitionSupply.IsValid()&&!CompetitionSupply->IsAvailable()&&CompetitionSupply->IsActorTickEnabled()&&Mode->Model.FindParticipant(0)->SuppliesCollected==CompetitionPlayerScore+1&&Mode->Model.FindParticipant(SerumCaster->ParticipantId)->SuppliesCollected==CompetitionOtherScore&&Mode->Player->GetRoundInventory()->GetReserve(1)==RepeatedReserve;
        Check(TEXT("consumed_same_box_real_tick_never_duplicates_eligible_contact"),bRepeatAuto);
        if(!BeginAutomaticReject(false,Now)){Check(TEXT("actual_source_wall_auto_tick_fixture_available"),false);Finish(TEXT("No nearby supported original wall rejecting only the tested eligible participant"));}return;
    }
    if(Stage==26||Stage==27)
    {
        if(Now-StageAt<.6)return;auto* Bot=Cast<AHeliosBioNPC>(Mode->ParticipantActor(RejectParticipant));
        const bool Rejected=RejectSupply.IsValid()&&RejectSupply->IsAvailable()&&RejectSupply->IsActorTickEnabled()&&Bot&&Mode->Model.FindParticipant(RejectParticipant)->SuppliesCollected==RejectScore&&Bot->GetRoundInventory()->GetReserve(0)==RejectReserve;
        Check(Stage==26?TEXT("actual_original_wall_blocks_repeated_production_supply_auto_ticks"):TEXT("actual_other_source_floor_blocks_repeated_production_supply_auto_ticks"),Rejected);
        if(Stage==26)
        {
            bWallAuto=Rejected;if(RejectSupply.IsValid())RejectSupply->SetAvailable(false);
            if(!BeginAutomaticReject(true,Now)){Check(TEXT("actual_other_floor_auto_tick_fixture_available"),false);Finish(TEXT("No real nearby other source floor within150cm with clear LOS and more65cm support difference"));}return;
        }
        bFloorAuto=Rejected;if(RejectSupply.IsValid())RejectSupply->SetAvailable(false);bInventoryCases=bDuplicateTransaction&&bCompetitionAuto&&bRepeatAuto&&bWallAuto&&bFloorAuto;
        Slot=1;if(!BeginNativeItem(Now))Finish(TEXT("First acquired native item cannot be equipped after real automatic pickup isolation cases"));
    }
}

void AHeliosBioSupplyWeaponQA::CompleteItemWindow(double Now)
{
    CheckNativeAudio(TestWeapon.Get(),true);CheckNativeAudio(FormBot.IsValid()?Cast<AHeliosBioSupplyWeapon>(FormBot->GetRoundInventory()->GetWeapon(Slot)):nullptr,false);
    if(auto* PC=GetWorld()->GetFirstPlayerController())PC->SetViewTarget(Mode->Player);
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("form"),SelectedForm);Row->SetNumberField(TEXT("item"),Candidates[Slot-1]->OriginalItemId);
    bool NativeActions=true;TArray<TSharedPtr<FJsonValue>> Results;
    for(bool FP:{true,false})for(FName Action:{FName(TEXT("Idle")),FName(TEXT("Move")),FName(TEXT("Equip")),FName(TEXT("Fire")),FName(TEXT("ChangeClip"))})
    {
        if(!FP&&((Slot==2&&Action==TEXT("Equip"))||(Slot==3&&Action==TEXT("ChangeClip"))))continue;
        const bool Passed=PosePassed(FP,Action);NativeActions&=Passed;
        Check(SelectedForm+TEXT("_item")+FString::FromInt(Candidates[Slot-1]->OriginalItemId)+(FP?TEXT("_FP_"):TEXT("_TP_"))+Action.ToString()+TEXT("_actual_source_clock_and_own_skin"),Passed);
        auto Result=MakeShared<FJsonObject>();Result->SetStringField(TEXT("view"),FP?TEXT("FP"):TEXT("TP"));Result->SetStringField(TEXT("action"),Action.ToString());Result->SetBoolField(TEXT("passed"),Passed);const auto* Probe=(FP?FPPose:TPPose).Find(Action);Result->SetNumberField(TEXT("actual_samples"),Probe?Probe->Samples:0);Result->SetNumberField(TEXT("actual_clock_advances"),Probe?Probe->ClockAdvances:0);Result->SetNumberField(TEXT("actual_local_skinned_surface_advances"),Probe?Probe->SurfaceAdvances:0);Results.Add(MakeShared<FJsonValueObject>(Result));
    }
    Row->SetArrayField(TEXT("actual_actions"),Results);Row->SetBoolField(TEXT("all_required_source_actions_observed"),NativeActions);Row->SetStringField(TEXT("original_missing_action_policy"),Slot==2?TEXT("Original rocket TP has no Equip state; original Idle/Move retained during FP Equip"):Slot==3?TEXT("Original stun has no reload; local replenishment uses source FP Equip, original TP Equip; Prepare has no TP state and retains original Idle/Move"):TEXT("All declared required source states retained"));FormWindows.Add(MakeShared<FJsonValueObject>(Row));
    ++Slot;if(Slot<5){if(!BeginNativeItem(Now))Finish(TEXT("Next real Player native item cannot be selected"));}else BeginPlayerRestore(Now);
}
void AHeliosBioSupplyWeaponQA::BeginPlayerRestore(double Now)
{
    CapturePreservedInventory(Mode->Player->GetRoundInventory());MotherMeleeBefore=Victim->MeleeAttacks;MotherInfectionsBefore=Mode->Model.FindParticipant(Victim->ParticipantId)->Infections;
    if(!PlaceOpponent(Mode->Player,Victim.Get(),80,true)){Check(TEXT("real_Player_infection_same_floor_melee_fixture_available"),false);Finish(TEXT("No real same-floor80cm Player infection lane"));return;}
    Victim->bQAFreeze=false;Stage=300;StageAt=Now;
}
void AHeliosBioSupplyWeaponQA::TickInventoryRestore(double Now)
{
    const auto* State=Mode->Model.FindParticipant(0);
    if(Stage==300)
    {
        if(State->Team!=EHeliosBioTeam::Zombie){if(Now-StageAt>10){Check(TEXT("actual_Player_mother_melee_infection"),false);Finish(TEXT("Mother real melee did not infect Player"));}return;}
        const auto* Mother=Mode->Model.FindParticipant(Victim->ParticipantId);Check(TEXT("actual_Player_mother_melee_infection"),Victim->MeleeAttacks>MotherMeleeBefore&&Mother->Infections>MotherInfectionsBefore);
        Victim->bQAFreeze=true;Stage=301;StageAt=Now;Observe(TEXT("actual_Player_infection_suspended_all_five_instances"));return;
    }
    if(Stage==301)
    {
        if(Now-StageAt<.2)return;
        auto* Caster=const_cast<FHeliosBioParticipant*>(Mode->Model.FindParticipant(SerumCaster->ParticipantId));Caster->Team=EHeliosBioTeam::UltimateHunter;Caster->HunterRoleId=TEXT("BioRedeemerHunter");Caster->bAlive=true;Caster->bPermanentlyDead=false;Caster->FrozenSeconds=0;
        bPlayerRestored=Mode->Model.ApplyRescueSerum(0,Caster->Id)==EHeliosBioSerumResult::CuredZombie;Stage=302;StageAt=Now;return;
    }
    if(Stage==302)
    {
        if(Now-StageAt<.3)return;
        Check(TEXT("shared_cure_rule_Player_restores_own_five_instances_ammo_and_active_slot"),bPlayerRestored&&State->Team==EHeliosBioTeam::Human&&SamePreservedInventory(Mode->Player->GetRoundInventory()));Observe(TEXT("actual_Player_cure_inventory_restored_without_refill"));
        // Ordinary shared restart, using the same roster seed solely so each
        // original independent form/loadout can be compared exactly.
        Mode->BaseRoundSeed=Mode->CurrentRoundSeed-Mode->RoundSerial*103;Mode->RestartBioRound();FreezeBots();Stage=303;StageAt=Now;return;
    }
    if(Stage==303)
    {
        if(Now-StageAt<3)return;
        auto* Inventory=Mode->Player->GetRoundInventory();bool ExtrasGone=Inventory->GetActiveSlot()==0&&Inventory->HasSlot(0)&&Mode->Model.FindParticipant(0)->Team==EHeliosBioTeam::Human;
        for(int32 I=1;I<5;++I)ExtrasGone&=!Inventory->HasSlot(I)&&!PreservedWeapons[I].IsValid();
        Check(TEXT("ordinary_restart_clears_round_extras_and_restores_baseline"),ExtrasGone&&Inventory->GetReserve(0)==600);
        auto* Bot=FormBot.Get();Check(SelectedForm+TEXT("_ordinary_restart_restores_own_original_loadout"),Bot&&Bot->GetHumanLoadout()==FormBaseline&&Bot->GetRoundInventory()->GetActiveSlot()==0&&Bot->GetRoundInventory()->GetWeaponId(0)==FormBaseline.PrimaryWeaponId);
        if(Bot)MeasureSole(Bot,TEXT("normal_restart"));Finish(TEXT("Selected own form actual four-weapon FP/TP windows and inventory lifecycle executed; aggregate17 distinct form reports before whole acceptance"));
    }
}
void AHeliosBioSupplyWeaponQA::Finish(const FString& Reason)
{
    if(bFinished)return;bFinished=true;if(Mode&&Mode->Player)Mode->Player->StopBioFire();
    if(bFormCase){QueueAudioObservation(true);Check(TEXT("cumulative_audio_render_observer_history_retained"),!bAudioObservationFailed);}
    const bool Preserved=OriginalsUnchanged();Check(TEXT("saved_definition_gate_bits_and_package_hashes_unchanged"),Preserved);
    auto Report=MakeShared<FJsonObject>();Report->SetStringField(TEXT("status"),bManual?(bFixturePrepared&&Failures==0?TEXT("manual_candidate_fixture_recorded"):TEXT("blocked")):Reason.IsEmpty()?TEXT("tested"):TEXT("coverage_pending_or_blocked"));Report->SetStringField(TEXT("reason"),Reason);Report->SetNumberField(TEXT("failures"),Failures);Report->SetBoolField(TEXT("manual_fixture"),bManual);Report->SetBoolField(TEXT("candidate_fixture"),true);Report->SetBoolField(TEXT("natural_round_verified"),false);Report->SetBoolField(TEXT("physical_input_verified_by_this_actor"),false);Report->SetBoolField(TEXT("gameplay_gate_changed"),!Preserved);Report->SetBoolField(TEXT("fixture_prepared"),bFixturePrepared);Report->SetBoolField(TEXT("core_player_acceptance"),!bManual&&Failures==0&&CompletedNativeItems==4);Report->SetBoolField(TEXT("whole_four_weapon_acceptance"),false);Report->SetNumberField(TEXT("actual_player_native_items_completed"),CompletedNativeItems);Report->SetBoolField(TEXT("all17_form_native_actions_verified"),false);Report->SetArrayField(TEXT("checks"),Checks);Report->SetArrayField(TEXT("observations"),Observations);Report->SetArrayField(TEXT("source_pending"),SourceIssues);Report->SetStringField(TEXT("scope"),TEXT("Unsaved candidate gate clones only. SourceIdentity must already be true. Production inventory/native weapon actions and actual collision, ammunition and effects are observed; manual input is supplied externally. Checkpoint does not establish whole four-weapon/17-form acceptance."));
    Report->SetBoolField(TEXT("selected_form_fixture"),bFormCase);Report->SetStringField(TEXT("selected_form"),SelectedForm);Report->SetNumberField(TEXT("selected_form_index"),FormIndex);Report->SetNumberField(TEXT("actual_NPC_native_items_completed"),CompletedNPCItems);Report->SetArrayField(TEXT("selected_form_native_windows"),FormWindows);
    Report->SetNumberField(TEXT("actual_auto_touch_pickup_items"),AutomaticTouchItems);Report->SetBoolField(TEXT("actual_auto_tick_competition_and_repeat_verified"),bCompetitionAuto&&bRepeatAuto);Report->SetBoolField(TEXT("actual_auto_tick_wall_and_floor_rejection_verified"),bWallAuto&&bFloorAuto);Report->SetNumberField(TEXT("last_cumulative_engine_observation_serial"),AudioSerial);
    TArray<TSharedPtr<FJsonValue>> Captures;for(const FString& Capture:RenderCaptures)Captures.Add(MakeShared<FJsonValueString>(Capture));Report->SetArrayField(TEXT("actual_viewport_captures"),Captures);TArray<TSharedPtr<FJsonValue>> EffectViews;for(const FString& Key:ActuallyRenderedFX)EffectViews.Add(MakeShared<FJsonValueString>(Key));Report->SetArrayField(TEXT("actual_recently_rendered_native_effect_item_views"),EffectViews);
    Report->SetBoolField(TEXT("selected_form_four_weapon_actions_and_inventory_acceptance"),bFormCase&&CompletedNativeItems==4&&CompletedNPCItems==4&&FormWindows.Num()==4&&bInventoryCases&&bPlayerRestored&&(FormIndex!=0||AutomaticTouchItems==4)&&Failures==0&&SourceIssues.IsEmpty()&&Preserved);
    Report->SetBoolField(TEXT("rendered_visual_reviewed"),false);Report->SetStringField(TEXT("selected_form_scope"),TEXT("Actual one of17 independent NPC source forms is used for Player FP and its own NPC TP. Every item has real input-motor travel, original animation clock and CPU skinned local-surface progression, true native weapon fire/damage/ammo and natural empty reload. Actual mother melee and committed original Down precede shared production cure-rule inventory restoration. Cure caster permission is an explicit state fixture; serum projectile correctness is verified independently. Aggregate all17 distinct successful reports and review actual renders before whole-weapon acceptance."));
    FString Text;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Out/TEXT("supply_weapon_report.json")));FPlatformMisc::RequestExit(false);
}
void AHeliosBioSupplyWeaponQA::Tick(float Dt)
{
    Super::Tick(Dt);if(bFinished)return;const double Wall=FPlatformTime::Seconds();const double Now=GetWorld()->GetTimeSeconds();
    if(StartedWall<=0)
    {
        StartedWall=Wall;bManual=FParse::Param(FCommandLine::Get(),TEXT("BioSupplyManualQA"));FParse::Value(FCommandLine::Get(),TEXT("BioSupplyWeaponQAOut="),Out);if(Out.IsEmpty())Out=FPaths::ProjectDir()/TEXT("../qa/cfm_bio_box_weapons/supply_runtime");IFileManager::Get().MakeDirectory(*Out,true);
        ExpectedMap=TEXT("/Game/Helios/Maps/CFMBioPyramidFloating");FParse::Value(FCommandLine::Get(),TEXT("BioExpectedMap="),ExpectedMap);FParse::Value(FCommandLine::Get(),TEXT("BioSupplyMaximumSeconds="),MaximumWall);
        FParse::Value(FCommandLine::Get(),TEXT("BioSupplyFormIndex="),FormIndex);bFormCase=!bManual&&FormIndex>=0;
    }
    if(Wall-StartedWall>MaximumWall){Finish(Stage==0?TEXT("Initialization timed out waiting for Bio mode/player and 29 actual bots"):TEXT("Bounded real wall-clock observation window ended"));return;}
    if(Stage==0)
    {
        Mode=AHeliosBioHotelGameMode::Find(GetWorld());if(!Mode||!Mode->Player||Mode->Bots.Num()!=29)return;
        if(!Mode->bFloatingProfile||GetWorld()->GetOutermost()->GetName()!=ExpectedMap){Finish(TEXT("Expected floating map/profile not loaded"));return;}
        FreezeBots();Mode->Model.Config.bKeepLocalPlayerHuman=true;
        if(!LoadCandidates()){Finish(Blocker);return;}
        if(bFormCase&&!SetupFormFixture()){Finish(Blocker);return;}
        if(bManual)
        {
            if(!GrantManual()){Finish(Blocker);return;}Mode->ShowNotice(TEXT("候选武器隔离验收：2–5/滚轮切换，原键鼠操作；F9保存退出"),8);Stage=10;NextObservation=Now;return;
        }
        Mode->Model.Config.NormalSupplyCount=Mode->Model.Config.BlueSupplyCount=Mode->Model.Config.RedSupplyCount=0;
        for(auto Supply:Mode->Supplies)if(Supply)Supply->SetAvailable(false);
        Stage=1;StageAt=Now;return;
    }
    if(auto* PC=GetWorld()->GetFirstPlayerController();PC&&PC->WasInputKeyJustPressed(EKeys::F9)){Observe(TEXT("actual_raw_F9_finish_edge"));Finish(TEXT("External operator ended candidate fixture"));return;}
    if(Stage==10){FreezeBots();if(Now>=NextObservation){Observe(TEXT("manual_actual_inventory_snapshot"));NextObservation=Now+.2;}return;}
    if(GetWorld()->IsPaused())return;
    if(bFormCase&&Now>=NextPoseSample){SampleCurrentActions();NextPoseSample=Now+.02;}
    if(Stage==1&&Mode->Model.GetPhase()==EHeliosBioPhase::Active)
    {
        if(bFormCase)
        {
            const auto* First=Mode->Model.FindParticipant(29);const auto* Second=Mode->Model.FindParticipant(28);
            if(!First||!Second||!First->bMother||!Second->bMother||First->Team!=EHeliosBioTeam::Zombie||Second->Team!=EHeliosBioTeam::Zombie){Finish(TEXT("Actual real-time mother selection differs from the separately planned seed"));return;}
            for(int32 Id:{28,29})if(auto* Mother=const_cast<FHeliosBioParticipant*>(Mode->Model.FindParticipant(Id))){Mother->SkillCooldownRemaining=Mother->SecondaryCooldownRemaining=MaximumWall;Mother->bMother=false;}
        }
        Slot=1;Stage=bFormCase&&FormIndex==0?20:2;StageAt=Now;return;
    }
    if(Stage==2)
    {
        auto* Inventory=Mode->Player->GetRoundInventory();auto* Supply=MakeSupply(Candidates[Slot-1]);if(!Supply){Finish(Blocker);return;}
        const int32 Score=Mode->Model.FindParticipant(0)->SuppliesCollected;const int32 BaselineReserve=Inventory->GetReserve(0);
        const bool Granted=Mode->PickupSupply(0,Supply);Check(FString::Printf(TEXT("slot%d_production_pickup_granted"),Slot),Granted&&!Supply->IsAvailable()&&Inventory->HasSlot(Slot)&&Mode->Model.FindParticipant(0)->SuppliesCollected==Score+1);
        Check(FString::Printf(TEXT("slot%d_pickup_ammo_baseline_only"),Slot),Inventory->GetReserve(0)==FMath::Min(1800,BaselineReserve+300)&&Inventory->GetReserve(Slot)==Candidates[Slot-1]->PickupReserveAmmo);
        Check(FString::Printf(TEXT("slot%d_same_box_cannot_be_picked_twice"),Slot),!Mode->PickupSupply(0,Supply)&&Mode->Model.FindParticipant(0)->SuppliesCollected==Score+1);Observe(TEXT("actual_production_pickup_transaction"));
        if(!Granted){Finish(TEXT("Production pickup rejected real candidate source/pair"));return;}
        ++Slot;if(Slot<5)return;
        CompleteInitialAcquisition(Now);return;
    }
    if((Stage>=5&&Stage<=9)||Stage==50||Stage==51||Stage==65)TickNativeItem(Now);
    else if(Stage>=20&&Stage<=22)TickTouchPickup(Now);
    else if(Stage>=24&&Stage<=27)TickInventoryTransactions(Now);
    else if(Stage>=200&&Stage<=209)TickNPCItem(Now,Dt);
    else if(Stage>=300&&Stage<=303)TickInventoryRestore(Now);
}
