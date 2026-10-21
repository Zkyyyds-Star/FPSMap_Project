#include "QA/HeliosChopsticksQA.h"
#include "Weapons/HeliosChopsticksWeapon.h"
#include "Adventure/HeliosAdventureCharacter.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "UI/HeliosAdventureHUD.h"
#include "Weapons/HeliosFirstPersonPresentationComponent.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Weapons/HeliosWeaponVFX.h"
#include "Town/HeliosTownSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/DamageType.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

namespace { const FRotator Directions[]={FRotator(0,0,0),FRotator(0,90,0),FRotator(0,180,0),FRotator(0,270,0),FRotator(30,42,0),FRotator(-30,215,0)}; }
AHeliosChopsticksQA::AHeliosChopsticksQA(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;}
void AHeliosChopsticksQA::Next(int32 V){Stage=V;Age=0;WallStart=FPlatformTime::Seconds();}
void AHeliosChopsticksQA::Check(const FString& N,bool P,const FString& D)
{
    auto C=MakeShared<FJsonObject>();C->SetStringField(TEXT("name"),N);C->SetBoolField(TEXT("passed"),P);C->SetStringField(TEXT("detail"),D);
    Checks.Add(MakeShared<FJsonValueObject>(C));if(!P)++Failures;UE_LOG(LogTemp,Display,TEXT("CHOPSTICKS_QA %s %s %s"),*N,P?TEXT("PASS"):TEXT("FAIL"),*D);
}
int32 AHeliosChopsticksQA::Count()const
{int32 N=0;for(TActorIterator<AHeliosChopstickProjectile> I(GetWorld());I;++I)if(IsValid(*I)&&!I->IsActorBeingDestroyed())++N;return N;}
void AHeliosChopsticksQA::TargetAt(const FVector& P)
{
    if(IsValid(Target)){if(auto* C=Target->GetController())C->Destroy();Target->Destroy();}
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Target=GetWorld()->SpawnActor<AHeliosEnemyActor>(P,FRotator::ZeroRotator,Params);
    if(Target){Target->StopShooting();Target->CurrentHP=10000;Target->GetCharacterMovement()->DisableMovement();Target->SetActorTickEnabled(false);
        if(auto* AI=Cast<AAIController>(Target->GetController())){if(AI->GetBrainComponent())AI->GetBrainComponent()->StopLogic(TEXT("Chopsticks QA"));AI->StopMovement();}}
}
void AHeliosChopsticksQA::Shot(){Player->StartFireFromAutomation();Player->StopFireFromAutomation();}
void AHeliosChopsticksQA::Capture(const FString& N)
{
    FString Dir=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/chopsticks"));FString Tag;
    if(FParse::Value(FCommandLine::Get(),TEXT("ChopsticksCapture="),Tag))Dir/=Tag;
    const FString Path=Dir/(N+TEXT(".png"));IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);FScreenshotRequest::RequestScreenshot(Path,true,false);
}
void AHeliosChopsticksQA::Tick(float Dt)
{
    Super::Tick(Dt);if(bDone)return;
    if(!Started)Started=FPlatformTime::Seconds();
    if(FPlatformTime::Seconds()-Started>180){Check(TEXT("timeout"),false);Finish();return;}
    if(!Player)Player=Cast<AHeliosAdventureCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));
    if(!Director)Director=AHeliosAdventureDirector::Find(GetWorld());if(!Player||!Director)return;
    Age+=Dt;
    auto* PC=Cast<APlayerController>(Player->GetController());auto* HUD=PC?Cast<AHeliosAdventureHUD>(PC->GetHUD()):nullptr;
    if(!PC||!HUD)return;
    switch(Stage)
    {
    case 0:
        bDemo=FParse::Param(FCommandLine::Get(),TEXT("HeliosChopsticksDemo"));bMenuOnly=FParse::Param(FCommandLine::Get(),TEXT("HeliosChopsticksMenu"));
        HUD->SelectWeapon(TEXT("chopsticks"));
        Check(TEXT("chopsticks_still_registered"),UHeliosWeaponRegistry::FindWeapon(TEXT("chopsticks"))!=nullptr);
        Next(1);break;
    case 1:
        if(FPlatformTime::Seconds()-WallStart<1.5)break;
        Capture(TEXT("01_menu"));Next(2);break;
    case 2:
        if(FPlatformTime::Seconds()-WallStart<.4)break;
        if(bMenuOnly){Finish();break;}
        if(auto* Town=GetWorld()->GetSubsystem<UHeliosTownSubsystem>();Town&&Town->IsTownWorld()&&!Town->IsWorldReady())break;
        HUD->SelectWeapon(TEXT("chopsticks"));
        HUD->StartSelectedRun();Director->SetActorTickEnabled(false);Player->SetCanBeDamaged(false);
        Check(TEXT("selection_equips_chopsticks"),Player->GetWeaponId()==TEXT("chopsticks"));
        Check(TEXT("six_pairs"),Player->GetAmmo()==6);
        Check(TEXT("hands_and_animations_ready"),Player->GetPresentation()&&Player->GetPresentation()->IsReady());
        if(auto* Town=GetWorld()->GetSubsystem<UHeliosTownSubsystem>();Town&&Town->IsTownWorld())
        {
            Check(TEXT("town_saves_chopsticks"),Town->GetSavedWeaponId()==TEXT("chopsticks")&&Town->SaveProgress());
            FString Saved;TSharedPtr<FJsonObject> Json;
            const bool Read=FFileHelper::LoadFileToString(Saved,*(FPaths::ProjectSavedDir()/TEXT("SaveGames/HeliosChopsticksTown.json")))&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Saved),Json);
            Check(TEXT("town_save_contains_weapon_id"),Read&&Json.IsValid()&&Json->GetStringField(TEXT("weapon_id"))==TEXT("chopsticks"));
        }
        if(bDemo)
        {
            Origin=Player->GetActorLocation();Aim=PC->GetControlRotation();Aim.Pitch=0;
            TargetAt(Player->FirstPersonCamera->GetComponentLocation()+Aim.Vector()*950);
            Next(100);break;
        }
        Player->SetActorLocation(Origin,false,nullptr,ETeleportType::TeleportPhysics);Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying);PC->SetControlRotation(Aim);
        TargetAt(Player->FirstPersonCamera->GetComponentLocation()+Aim.Vector()*450);Next(3);break;
    case 3:
        if(Age<.5f)break;
        Before=Target->GetHealth();{int32 N=Count();Shot();Check(TEXT("one_pair_two_visible_projectiles"),Count()==N+2);}
        Check(TEXT("one_pair_ammo_cost"),Player->GetAmmo()==5);Next(4);break;
    case 4:
        if(Age<.35f)break;
        Check(TEXT("dual_hit_80_damage"),FMath::IsNearlyEqual(Before-Target->GetHealth(),80.f),FString::SanitizeFloat(Before-Target->GetHealth()));Next(5);break;
    case 5:
        if(Age<.3f)break;
        Check(TEXT("no_repeated_impact_damage"),FMath::IsNearlyEqual(Before-Target->GetHealth(),80.f));
        Before=Target->GetHealth();
        {
            FVector Start=Player->FirstPersonCamera->GetComponentLocation()+Aim.Vector()*60;
            FTransform Tr(Aim,Start);auto* P=GetWorld()->SpawnActorDeferred<AHeliosChopstickProjectile>(AHeliosChopstickProjectile::StaticClass(),Tr,Player,Player,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            P->Configure(Aim.Vector(),40,6500,5000,nullptr,nullptr,nullptr);UGameplayStatics::FinishSpawningActor(P,Tr);
        }
        Next(6);break;
    case 6:
        if(Age<.3f)break;
        Check(TEXT("single_stick_40_damage"),FMath::IsNearlyEqual(Before-Target->GetHealth(),40.f),FString::SanitizeFloat(Before-Target->GetHealth()));
        Wall=GetWorld()->SpawnActor<AStaticMeshActor>(Player->FirstPersonCamera->GetComponentLocation()+Aim.Vector()*110,FRotator::ZeroRotator);
        Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Wall->SetActorScale3D(FVector(.1f,5,5));Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
        Before=Target->GetHealth();Shot();Next(7);break;
    case 7:
        if(Age<.5f)break;
        Check(TEXT("thin_wall_blocks_fast_projectiles"),Target->GetHealth()==Before);
        Wall->SetActorLocation(Player->FirstPersonCamera->GetComponentLocation()+Aim.Vector()*15);Shot();Next(8);break;
    case 8:
        if(Age<.5f)break;
        Check(TEXT("near_wall_no_shoot_through"),Target->GetHealth()==Before);Wall->Destroy();Wall=nullptr;Next(9);break;
    case 9:
        Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("chopsticks")));
        Aim=Directions[DirectionIndex];PC->SetControlRotation(Aim);
        TargetAt(Player->FirstPersonCamera->GetComponentLocation()+Aim.Vector()*(DirectionIndex==0?4000.f:450.f));Next(10);break;
    case 10:
        if(Age<.5f)break;
        Before=Target->GetHealth();Shot();Next(11);break;
    case 11:
        if(Age<.85f)break;
        Check(FString::Printf(TEXT("direction_%d_dual_damage"),DirectionIndex),FMath::IsNearlyEqual(Before-Target->GetHealth(),80.f),FString::SanitizeFloat(Before-Target->GetHealth()));
        if(++DirectionIndex<UE_ARRAY_COUNT(Directions))Next(9);else Next(20);break;
    case 20:
        Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("chopsticks")));
        Aim=FRotator::ZeroRotator;PC->SetControlRotation(Aim);
        TargetAt(Player->FirstPersonCamera->GetComponentLocation()+Aim.Vector()*6000);Before=Target->GetHealth();Next(21);break;
    case 21:
        if(Age<.5f)break;Shot();Next(22);break;
    case 22:
        if(Age<1.f)break;
        Check(TEXT("beyond_50m_no_damage"),Target->GetHealth()==Before);
        Check(TEXT("expired_misses_destroyed"),Count()==0);Next(12);break;
    case 12:
        Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("chopsticks")));Player->StartFireFromAutomation();Next(13);break;
    case 13:
        if(Age<2.7f)break;
        Player->StopFireFromAutomation();Check(TEXT("held_trigger_empties_six_pairs"),Player->GetAmmo()==0);
        Shot();Check(TEXT("empty_cannot_fire"),Player->GetAmmo()==0);
        Player->StartReloadFromAutomation();Shot();Check(TEXT("reload_blocks_fire"),Player->IsReloading()&&Player->GetAmmo()==0);Next(14);break;
    case 14:
        if(Age<.6f)break;
        Check(TEXT("reload_not_early"),Player->GetAmmo()==0&&Player->IsReloading());Next(15);break;
    case 15:
        if(Age<.95f)break;
        Check(TEXT("reload_completes_at_1_4_seconds"),Player->GetAmmo()==6&&!Player->IsReloading());
        Shot();Player->StartReloadFromAutomation();Next(16);break;
    case 16:
        if(Age<.3f)break;
        Player->CancelCombatAndControl();Check(TEXT("reload_cancelled"),!Player->IsReloading()&&Player->GetAmmo()==5);Next(17);break;
    case 17:
        if(Age<1.5f)break;
        Check(TEXT("cancelled_reload_never_refills"),Player->GetAmmo()==5);
        Director->RestartRun();Director->SetActorTickEnabled(false);
        Check(TEXT("restart_keeps_weapon_and_fills_pairs"),Player->GetWeaponId()==TEXT("chopsticks")&&Player->GetAmmo()==6);
        Check(TEXT("restart_cleans_all_projectiles"),Count()==0);Next(18);break;
    case 18:
        if(Age<3.3f)break; // Honor the game's existing three-second spawn protection.
        Check(TEXT("effects_return_to_pool"),UHeliosWeaponVFX::GetTrackedCount(this)==0);
        for(const auto& Id:{TEXT("rifle"),TEXT("rocket"),TEXT("laser"),TEXT("shotgun"),TEXT("chopsticks")})
            Check(FString(Id)+TEXT("_equip_compatibility"),Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(Id)));
        Shot();Player->SetCanBeDamaged(true);UGameplayStatics::ApplyDamage(Player,10000,nullptr,this,UDamageType::StaticClass());Next(19);break;
    case 19:
        if(Age<.15f)break;
        Check(TEXT("death_cleans_chopsticks"),Count()==0);Finish();break;
    case 100:
        PC->SetControlRotation(Aim);
        if(Age>1.4f && Age<3.85f)Player->StartFireFromAutomation();
        if(Age>=3.85f)Player->StopFireFromAutomation();
        if(Age>=4.2f&&Age<4.3f)Player->StartReloadFromAutomation();
        if(Age>6.1f&&Age<8.3f)Player->StartFireFromAutomation();
        if(Age>=8.3f)Player->StopFireFromAutomation();
        if(Age>=9.f){Check(TEXT("demo_threw_and_reloaded"),Player->GetPresentation()->GetShotPresentationCount()>=6);Finish();break;}
        Capture(FString::Printf(TEXT("frames/frame_%05d"),Frame++));break;
    }
}
void AHeliosChopsticksQA::Finish()
{
    bDone=true;if(Player)Player->StopFireFromAutomation();
    auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("status"),Failures?TEXT("failed"):TEXT("passed"));R->SetNumberField(TEXT("failures"),Failures);
    R->SetStringField(TEXT("map"),GetWorld()->GetMapName());R->SetArrayField(TEXT("checks"),Checks);R->SetNumberField(TEXT("capture_frames"),Frame);
    FString Out;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Out));
    FString Dir=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/chopsticks"));FString Tag;
    if(FParse::Value(FCommandLine::Get(),TEXT("ChopsticksCapture="),Tag))Dir/=Tag;
    IFileManager::Get().MakeDirectory(*Dir,true);FFileHelper::SaveStringToFile(Out,*(Dir/TEXT("runtime_report.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    FPlatformMisc::RequestExit(false);
}
