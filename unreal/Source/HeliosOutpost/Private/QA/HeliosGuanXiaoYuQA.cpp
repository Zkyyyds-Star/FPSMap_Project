#include "QA/HeliosGuanXiaoYuQA.h"
#include "Player/HeliosArenaCharacter.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Player/HeliosGuanXiaoYuAbilityComponent.h"
#include "Player/HeliosCharacterDefinition.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "UI/HeliosAdventureHUD.h"
#include "ShooterWeapon.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/GameViewportClient.h"
#include "Engine/DamageEvents.h"
#include "Engine/Texture2D.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Materials/MaterialInterface.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "ContentStreaming.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif
AHeliosGuanXiaoYuQA::AHeliosGuanXiaoYuQA(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;}
void AHeliosGuanXiaoYuQA::BeginPlay()
{Super::BeginPlay();bMotionQA=FParse::Param(FCommandLine::Get(),TEXT("HeliosGuanXiaoYuMotionQA"));bFirstPersonQA=FParse::Param(FCommandLine::Get(),TEXT("HeliosGuanXiaoYuFPQA"));Out=FPaths::ProjectSavedDir()/TEXT("GuanXiaoYuQA");FParse::Value(FCommandLine::Get(),TEXT("HeliosGuanXiaoYuOut="),Out);IFileManager::Get().MakeDirectory(*Out,true);Started=PhaseStarted=FPlatformTime::Seconds();}
void AHeliosGuanXiaoYuQA::Advance(int32 Next){Phase=Next;PhaseStarted=FPlatformTime::Seconds();}
void AHeliosGuanXiaoYuQA::Check(const FString& Name,bool Passed)
{auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("case"),Case);Row->SetStringField(TEXT("name"),Name);Row->SetBoolField(TEXT("passed"),Passed);Checks.Add(MakeShared<FJsonValueObject>(Row));Failures+=Passed?0:1;UE_LOG(LogTemp,Display,TEXT("GXY_QA_CHECK %s %s %d"),*Case,*Name,Passed);}
bool AHeliosGuanXiaoYuQA::Capture(const FString& Name)
{
    TArray<UTexture2D*> Textures;
    auto Warm=[&](UMaterialInterface* Material){if(!Material)return;Material->SetForceMipLevelsToBeResident(false,false,60.f,0,true);TArray<UTexture*> Used;Material->GetUsedTextures(Used);for(auto* T:Used)if(auto* Texture=Cast<UTexture2D>(T))Textures.AddUnique(Texture);};
    for(AActor* Holder:{static_cast<AActor*>(Player.Get()),static_cast<AActor*>(Pet.Get())})if(Holder){TInlineComponentArray<UPrimitiveComponent*> Parts(Holder);for(auto* Part:Parts)for(int32 Slot=0;Slot<Part->GetNumMaterials();++Slot)Warm(Part->GetMaterial(Slot));}
    if(auto* Character=FHeliosCharacterRegistry::FindCharacter(Player->GetLoadout().CharacterId))if(auto* Form=Character->FindForm(Player->GetLoadout().CharacterVariantId))
        if(auto* Profile=Form->UmbrellaProfile.LoadSynchronous())
        {
            for(auto Path:{Profile->FirstPersonMesh,Profile->ThirdPersonMesh})if(auto* Mesh=Path.LoadSynchronous())for(const auto& Slot:Mesh->GetMaterials())Warm(Slot.MaterialInterface);
            for(const auto* Clips:{&Profile->FirstPersonClips,&Profile->ThirdPersonClips})for(const auto& Clip:*Clips)Clip.Value.LoadSynchronous();
            if(auto* Library=Profile->EffectLibrary.LoadSynchronous())for(auto Asset:Library->Dependencies)if(auto* Material=Cast<UMaterialInterface>(Asset.Get()))Warm(Material);
        }
#if WITH_EDITOR
    FAssetCompilingManager::Get().FinishAllCompilation();if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
#endif
    IStreamingManager::Get().StreamAllResources(10.f);IStreamingManager::Get().BlockTillAllRequestsFinished(10.f,true);
    bool Ready=true;for(auto* Texture:Textures)Ready&=Texture->IsFullyStreamedIn();
    // Newly encountered mips can finish their render-thread upload on the next
    // frame, especially at 30 FPS. Retry the motion capture after they settle.
    if((bMotionQA||bFirstPersonQA)&&!Ready)return false;
    Check(TEXT("capture_textures_ready"),Ready);
    FScreenshotRequest::RequestScreenshot(Out/(Case+TEXT("_")+Name+TEXT(".png")),true,false);
    return true;
}
void AHeliosGuanXiaoYuQA::Tick(float Dt)
{
    Super::Tick(Dt);const double Age=FPlatformTime::Seconds()-PhaseStarted;
    if(FPlatformTime::Seconds()-Started>240){Check(TEXT("runtime_timeout"),false);Finish();return;}
    if(!Player){Controller=GetWorld()->GetFirstPlayerController();Player=Controller?Cast<AHeliosArenaCharacter>(Controller->GetPawn()):nullptr;HUD=Controller?Cast<AHeliosAdventureHUD>(Controller->GetHUD()):nullptr;for(TActorIterator<AHeliosAdventureDirector> D(GetWorld());D;++D){Director=*D;break;}if(!Player||!HUD||!Director)return;}
#if WITH_EDITOR
    if(FAssetCompilingManager::Get().GetNumRemainingAssets()>0||(GShaderCompilingManager&&GShaderCompilingManager->GetNumRemainingJobs()>0))return;
#endif
    if(bMotionQA){TickMotion(Dt,Age);return;}
    if(bFirstPersonQA){TickFirstPerson(Dt,Age);return;}
    auto* Ability=Player->GetGuanXiaoYuAbility();const FName Variant=CaseIndex<2?FName("GR"):CaseIndex<4?FName("BL"):FName("SP");
    auto L=Player->GetLoadout();L.CharacterId=TEXT("cfm_guanxiaoyu");L.CharacterVariantId=Variant;L.PrimaryWeaponId=CaseIndex%2==0?FName("cfm_chan_zeus"):FName("cfm_sunshangxiang");
    switch(Phase)
    {
    case 0:
        if(Age<2)return;Case=TEXT("menu");HUD->SelectCharacter(TEXT("cfm_guanxiaoyu"));HUD->SelectWeapon(TEXT("cfm_chan_zeus"));Check(TEXT("player_menu_default_GR"),HUD->GetPendingCharacterId()==TEXT("cfm_guanxiaoyu")&&HUD->GetSelectedCharacterVariant()==TEXT("GR"));
        HUD->SelectPetCount(2);for(int32 Slot=0;Slot<2;++Slot){HUD->SelectPetSlot(Slot);HUD->SelectPetCharacter(TEXT("cfm_guanxiaoyu"),Slot==0?FName("BL"):FName("SP"));HUD->SelectPetWeapon(TEXT("cfm_chan_zeus"));Check(TEXT("companion_menu_accepts_form"),HUD->GetSelectedCharacterVariant(true)==(Slot==0?FName("BL"):FName("SP")));}Capture(TEXT("selection"));Advance(1);return;
    case 1:
        if(Age<1)return;HUD->SelectPetCount(0);HUD->StartSelectedRun();Check(TEXT("adventure_started"),!Director->IsSelectingWeapon()&&Player->GetLoadout().CharacterId==TEXT("cfm_guanxiaoyu"));Director->SetActorTickEnabled(false);Director->CleanupOwnedActors();Player->SetCanBeDamaged(false);Player->SetActorLocation(FVector(0,-2800,102),false,nullptr,ETeleportType::TeleportPhysics);Player->GetCharacterMovement()->DisableMovement();Controller->SetControlRotation(FRotator(0,90,0));
        for(TActorIterator<AHeliosPetCompanion> P(GetWorld());P;++P)P->Destroy();Check(TEXT("isolated_combat_fixture"),HUD->GetSelectedPetCount()==0);Advance(2);return;
    case 2:
        Case=Variant.ToString()+(CaseIndex%2==0?TEXT("_ranged"):TEXT("_melee"));Check(TEXT("form_primary_ready"),Player->EquipLoadout(L)&&Player->GetCharacterPresentation()->IsReady());Primary=Player->GetEquippedWeapon();if(!Primary){Finish();return;}Ammo=Primary->GetBulletCount();Advance(3);return;
    case 3:
        if(Age<1)return;Capture(TEXT("primary"));Player->StartFireFromAutomation();Player->StopFireFromAutomation();Advance(4);return;
    case 4:
        if(Age<1)return;Ammo=Primary->GetBulletCount();Check(TEXT("enter_umbrella"),Ability->ToggleUmbrella());Check(TEXT("visible_umbrella"),Ability->GetVisibleMesh()!=nullptr);Advance(5);return;
    case 5:
        if(Age<1)return;Capture(TEXT("umbrella"));Advance(9);return;
    case 9:
        if(Age<.1||Ability->CooldownRemaining()>0)return;Check(TEXT("heavy_with_shield"),Ability->StartHeavyAttack()&&Ability->ShieldRemaining()>0);
        Pet=GetWorld()->SpawnActorDeferred<AHeliosGuanXiaoYuQAPet>(AHeliosGuanXiaoYuQAPet::StaticClass(),FTransform(FRotator(0,90,0),FVector(130,-2510,102)),Player,Player,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);L.PrimaryWeaponId=TEXT("cfm_chan_zeus");Check(TEXT("companion_form_ready"),Pet->ApplyLoadout(L));Pet->CompanionOwner=Player;UGameplayStatics::FinishSpawningActor(Pet,FTransform(FRotator(0,90,0),FVector(130,-2510,102)));Pet->SetActorTickEnabled(false);Pet->GetCharacterMovement()->DisableMovement();
        {FActorSpawnParameters S;S.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;Target=GetWorld()->SpawnActor<AHeliosEnemyE01>(Pet->GetPawnViewLocation()+FVector(0,200,0),FRotator::ZeroRotator,S);Target->StopShooting();Target->CurrentHP=10000;Target->SetActorTickEnabled(false);Target->GetCharacterMovement()->DisableMovement();if(Target->GetController())Target->GetController()->Destroy();}
        Pet->Target=Target;Pet->State=EHeliosPetState::Fighting;Pet->ProbeAttack();Check(TEXT("companion_auto_umbrella_heavy"),Pet->GetGuanXiaoYuAbility()->IsUsingUmbrella()&&Pet->GetGuanXiaoYuAbility()->HeavyAttacks==1);Advance(6);return;
    case 6:
        if(Age<.3||Ability->ShieldRemaining()>1.7f)return;
        Check(TEXT("player_shield_effect_visible"),Ability->GetShieldEffect()&&Ability->GetShieldEffect()->VisibleTrackCount()>0);Check(TEXT("companion_shield_effect_visible"),Pet->GetGuanXiaoYuAbility()->GetShieldEffect()&&Pet->GetGuanXiaoYuAbility()->GetShieldEffect()->VisibleTrackCount()>0);Capture(TEXT("shield"));Advance(8);return;
    case 8:
        if(Age<.4||Pet->GetGuanXiaoYuAbility()->IsAttacking())return;UE_LOG(LogTemp,Display,TEXT("GXY_QA_DAMAGE %s actual=%.3f pet_hits=%d pet_strikes=%d"),*Case,10000-Target->CurrentHP,Pet->GetGuanXiaoYuAbility()->Hits,Pet->GetGuanXiaoYuAbility()->Strikes);Check(TEXT("companion_melee_damage"),FMath::IsNearlyEqual(10000-Target->CurrentHP,120.f));Check(TEXT("companion_first_block"),Pet->TakeDamage(10,FDamageEvent(),nullptr,Target)==0);Check(TEXT("companion_second_hurts"),Pet->TakeDamage(10,FDamageEvent(),nullptr,Target)==10);Check(TEXT("independent_player_shield"),Ability->ShieldRemaining()>0);
        Target->SetActorLocation(Pet->GetActorLocation()+FVector(0,400,0));Pet->ProbeAttack();Check(TEXT("companion_auto_returns_primary"),!Pet->GetGuanXiaoYuAbility()->IsUsingUmbrella());Check(TEXT("return_primary"),Ability->ToggleUmbrella()&&Player->GetEquippedWeapon()==Primary&&Primary->GetBulletCount()==Ammo);Capture(TEXT("companions"));Advance(7);return;
    case 7:
        if(Age<1)return;if(Pet)Pet->Destroy();if(Target)Target->Destroy();Pet=nullptr;Target=nullptr;if(++CaseIndex>=6){Finish();return;}Advance(2);return;
    }
}
void AHeliosGuanXiaoYuQA::TickMotion(float Dt,double Age)
{
    const FVector Anchor(0,-2460,102);
    const FName Variant=CaseIndex==0?FName("GR"):CaseIndex==1?FName("BL"):FName("SP");
    auto* Ability=Pet?Pet->GetGuanXiaoYuAbility():nullptr;
    auto HairPresent=[](USkeletalMeshComponent* Mesh){if(!Mesh)return false;int32 Count=0;for(auto* Material:Mesh->GetMaterials())if(Material&&Material->GetName().Contains(TEXT("Hair")))++Count;return Count==1;};
    switch(Phase)
    {
    case 0:
        if(Age<2)return;HUD->SelectCharacter(TEXT("cfm_guanxiaoyu"));HUD->SelectPetCount(0);HUD->StartSelectedRun();Director->SetActorTickEnabled(false);Director->CleanupOwnedActors();
        Player->SetActorLocation(FVector(0,-2800,102),false,nullptr,ETeleportType::TeleportPhysics);Player->SetCanBeDamaged(false);Player->GetCharacterMovement()->DisableMovement();Controller->SetControlRotation(FRotator(-4,90,0));
        Player->GetCharacterPresentation()->SetVisible(false);if(auto* Weapon=Player->GetEquippedWeapon())Weapon->SetActorHiddenInGame(true);
        for(TActorIterator<AHeliosPetCompanion> P(GetWorld());P;++P)P->Destroy();Advance(1);return;
    case 1:
        if(!Pet)
        {
            auto L=Player->GetLoadout();L.CharacterId=CaseIndex==3?FName("cfm_chan"):FName("cfm_guanxiaoyu");L.CharacterVariantId=CaseIndex==3?NAME_None:Variant;L.PrimaryWeaponId=TEXT("cfm_chan_zeus");
            Case=CaseIndex==3?TEXT("existing_chan"):Variant.ToString();
            Pet=GetWorld()->SpawnActorDeferred<AHeliosGuanXiaoYuQAPet>(AHeliosGuanXiaoYuQAPet::StaticClass(),FTransform(FRotator(0,-90,0),Anchor),Player,Player,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            Check(TEXT("motion_companion_ready"),Pet->ApplyLoadout(L));Pet->CompanionOwner=Player;UGameplayStatics::FinishSpawningActor(Pet,FTransform(FRotator(0,-90,0),Anchor));Pet->SetActorTickEnabled(false);Pet->GetCharacterMovement()->DisableMovement();Pet->GetCharacterMovement()->SetComponentTickEnabled(false);
            Pet->GetCharacterPresentation()->AddTickPrerequisiteActor(this);Pet->GetGuanXiaoYuAbility()->AddTickPrerequisiteActor(this);
        }
        Pet->ProbeMotion(Dt);if(Age<1)return;
        if(!Capture(TEXT("hair_primary")))return;if(CaseIndex<3)Check(TEXT("main_body_has_original_hair"),HairPresent(Pet->GetMesh()));MotionCapture=0;bOnePosePerFrame=true;LastMotionCommit=Pet->GetCharacterPresentation()->GetPoseCommitCount();LastMotionFrame=GFrameCounter;Advance(2);return;
    case 2:
    case 4:
    {
        const bool Umbrella=Phase==4;
        const uint32 Commits=Umbrella?Ability->GetPoseCommitCount():Pet->GetCharacterPresentation()->GetPoseCommitCount();
        // Asset compilation may temporarily skip this QA actor's checks while
        // presentation components still tick. Compare actual engine frames.
        bOnePosePerFrame&=Commits-LastMotionCommit<=FMath::Max(uint64(1),GFrameCounter-LastMotionFrame);LastMotionCommit=Commits;LastMotionFrame=GFrameCounter;++MotionFrames;
        FVector Offset=FVector::ZeroVector,Velocity=FVector::ZeroVector;
        const float T=Age;
        if(T<.75f){Offset.Y=-80*T;Velocity.Y=-80;}
        else if(T<1.5f){Offset.Y=-60+80*(T-.75f);Velocity.Y=80;}
        else if(T<2.25f){Offset.X=80*(T-1.5f);Velocity.X=80;}
        else Offset.X=60;
        Pet->SetActorLocation(Anchor+Offset);Pet->SetActorRotation(FRotator(0,-90+(T>1.5f?FMath::Min(T-1.5f,.75f)*40:0),0));Pet->GetCharacterMovement()->Velocity=Velocity;Pet->ProbeMotion(Dt);
        const float Times[]={.5f,1.25f,2.f,2.8f};const TCHAR* Names[]={TEXT("forward"),TEXT("backward"),TEXT("strafe_turn"),TEXT("stop")};
        if(MotionCapture<4&&Age>=Times[MotionCapture])if(Capture((Umbrella?TEXT("umbrella_"):TEXT("primary_"))+FString(Names[MotionCapture])))++MotionCapture;
        if(Age<3.2)return;
        Check(Umbrella?TEXT("umbrella_one_pose_per_frame"):TEXT("primary_one_pose_per_frame"),bOnePosePerFrame);
        Check(TEXT("single_visible_body"),int32(Pet->GetMesh()->IsVisible())+int32(Ability->IsUsingUmbrella()&&Ability->GetVisibleMesh()->IsVisible())==1);
        Pet->GetCharacterMovement()->Velocity=FVector::ZeroVector;
        if(Umbrella){Check(TEXT("return_retains_primary"),Ability->ToggleUmbrella()&&Pet->GetEquippedWeapon()==Primary&&Primary->GetBulletCount()==Ammo);Advance(5);}
        else Advance(CaseIndex==3?5:3);
        return;
    }
    case 3:
        if(!Ability->IsUsingUmbrella()){Primary=Pet->GetEquippedWeapon();Ammo=Primary->GetBulletCount();Check(TEXT("motion_enter_umbrella"),Ability->ToggleUmbrella());}
        if(Age<1||!Capture(TEXT("hair_umbrella")))return;Check(TEXT("umbrella_body_has_original_hair"),HairPresent(Ability->GetVisibleMesh()));MotionCapture=0;bOnePosePerFrame=true;LastMotionCommit=Ability->GetPoseCommitCount();LastMotionFrame=GFrameCounter;Advance(4);return;
    case 5:
        Pet->ProbeMotion(Dt);if(Age<.8||!Capture(TEXT("primary_restored")))return;Advance(6);return;
    case 6:
        if(Age<.2)return;Pet->Destroy();Pet=nullptr;if(++CaseIndex>=4){Finish();return;}Advance(1);return;
    }
}
void AHeliosGuanXiaoYuQA::TickFirstPerson(float Dt,double Age)
{
    static const FName Weapons[]={TEXT("cfm_bulianshi_p90"),TEXT("cfm_sunshangxiang"),TEXT("cfm_heilonghun")};
    auto* Ability=Player->GetGuanXiaoYuAbility();
    switch(Phase)
    {
    case 0:
        if(Age<2)return;HUD->SelectCharacter(TEXT("cfm_guanxiaoyu"));HUD->SelectWeapon(Weapons[0]);HUD->SelectPetCount(0);HUD->StartSelectedRun();
        Check(TEXT("adventure_started"),!Director->IsSelectingWeapon());Director->SetActorTickEnabled(false);Director->CleanupOwnedActors();Player->SetCanBeDamaged(false);
        Player->SetActorLocation(FVector(0,-2800,102),false,nullptr,ETeleportType::TeleportPhysics);Player->GetCharacterMovement()->DisableMovement();Controller->SetControlRotation(FRotator(0,90,0));Advance(1);return;
    case 1:
        {auto L=Player->GetLoadout();L.CharacterId=TEXT("cfm_guanxiaoyu");L.CharacterVariantId=TEXT("GR");L.PrimaryWeaponId=Weapons[CaseIndex];Case=TEXT("GR_")+Weapons[CaseIndex].ToString();
        Check(TEXT("first_person_form_ready"),Player->EquipLoadout(L)&&Player->GetCharacterPresentation()->IsReady());Primary=Player->GetEquippedWeapon();if(!Primary){Finish();return;}Advance(2);return;}
    case 2:
        if(Age<2.5||!Capture(TEXT("idle")))return;Advance(20);return;
    case 20:
        if(Age<.15)return;Player->StartFireFromAutomation();Advance(3);return;
    case 3:
        if(Age<.15||!Capture(TEXT("attack")))return;Player->StopFireFromAutomation();if(Primary->UsesAmmunition())Player->StartReloadFromAutomation();Advance(4);return;
    case 4:
        if(Age<.6)return;if(Primary->UsesAmmunition()){Check(TEXT("reload_animation_active"),Player->IsReloading());if(!Capture(TEXT("reload")))return;}Advance(5);return;
    case 5:
        if(Age<4||Player->IsReloading())return;Ammo=Primary->GetBulletCount();Check(TEXT("enter_umbrella"),Ability->ToggleUmbrella());Advance(6);return;
    case 6:
        if(Age<1||!Capture(TEXT("umbrella_idle")))return;Advance(21);return;
    case 21:
        if(Age<.15)return;Check(TEXT("light_attack_started"),Ability->StartLightAttack());Ability->StopLightAttack();Advance(7);return;
    case 7:
        if(Age<.22||!Capture(TEXT("umbrella_light")))return;Advance(8);return;
    case 8:
        if(Age<1||Ability->IsAttacking())return;Check(TEXT("heavy_attack_started"),Ability->StartHeavyAttack());Advance(9);return;
    case 9:
        if(Age<.28||!Capture(TEXT("umbrella_heavy")))return;Advance(10);return;
    case 10:
        if(Age<1||Ability->IsAttacking())return;Check(TEXT("return_primary_preserves_ammo"),Ability->ToggleUmbrella()&&Player->GetEquippedWeapon()==Primary&&Primary->GetBulletCount()==Ammo);Advance(11);return;
    case 11:
        Player->GetCharacterMovement()->Velocity=FVector(100,0,0);Player->AddActorWorldOffset(FVector(100*Dt,0,0));Controller->SetControlRotation(FRotator(0,90+Age*15,0));
        if(Age<.7||!Capture(TEXT("moving_primary")))return;Player->GetCharacterMovement()->Velocity=FVector::ZeroVector;Controller->SetControlRotation(FRotator(0,90,0));Advance(12);return;
    case 12:
        if(Age<.5)return;if(++CaseIndex>=UE_ARRAY_COUNT(Weapons)){Finish();return;}Advance(1);return;
    }
}
void AHeliosGuanXiaoYuQA::Finish()
{auto J=MakeShared<FJsonObject>();J->SetBoolField(TEXT("passed"),Failures==0&&CaseIndex==(bMotionQA?4:bFirstPersonQA?3:6));J->SetNumberField(TEXT("failures"),Failures);J->SetNumberField(TEXT("completed"),CaseIndex);J->SetBoolField(TEXT("motion_qa"),bMotionQA);J->SetBoolField(TEXT("first_person_qa"),bFirstPersonQA);J->SetNumberField(TEXT("motion_frames"),MotionFrames);J->SetArrayField(TEXT("checks"),Checks);FString Text;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Out/TEXT("report.json")));UE_LOG(LogTemp,Display,TEXT("GXY_RUNTIME_COMPLETE cases=%d failures=%d"),CaseIndex,Failures);SetActorTickEnabled(false);FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);}
