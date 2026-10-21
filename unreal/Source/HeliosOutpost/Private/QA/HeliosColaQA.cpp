#include "QA/HeliosColaQA.h"
#include "Weapons/HeliosColaWeapon.h"
#include "Weapons/HeliosColaVFXSettings.h"
#include "Town/HeliosTownSubsystem.h"
#include "Adventure/HeliosAdventureCharacter.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "UI/HeliosAdventureHUD.h"
#include "Weapons/HeliosFirstPersonPresentationComponent.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Weapons/HeliosWeaponVFX.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraActor.h"
#include "DrawDebugHelpers.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
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
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

namespace { const FRotator Directions[]={FRotator(0,0,0),FRotator(0,90,0),FRotator(0,180,0),FRotator(0,270,0),FRotator(30,42,0),FRotator(-30,215,0)}; }
AHeliosColaQA::AHeliosColaQA(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;}
void AHeliosColaQA::Next(int32 V){Stage=V;Age=0;WallStart=FPlatformTime::Seconds();}
void AHeliosColaQA::Check(const FString& N,bool P,const FString& D)
{
    auto C=MakeShared<FJsonObject>();C->SetStringField(TEXT("name"),N);C->SetBoolField(TEXT("passed"),P);C->SetStringField(TEXT("detail"),D);
    Checks.Add(MakeShared<FJsonValueObject>(C));if(!P)++Failures;
    UE_LOG(LogTemp,Display,TEXT("COLA_QA %s %s %s"),*N,P?TEXT("PASS"):TEXT("FAIL"),*D);
}
int32 AHeliosColaQA::Count()const
{int32 N=0;for(TActorIterator<AHeliosColaProjectile> I(GetWorld());I;++I)if(IsValid(*I)&&!I->IsActorBeingDestroyed())++N;return N;}
AHeliosColaProjectile* AHeliosColaQA::LastCan()const
{AHeliosColaProjectile* P=nullptr;for(TActorIterator<AHeliosColaProjectile> I(GetWorld());I;++I)if(IsValid(*I)&&!I->IsActorBeingDestroyed())P=*I;return P;}
void AHeliosColaQA::Shot(){Player->StartFireFromAutomation();Player->StopFireFromAutomation();}
void AHeliosColaQA::Equip(){Player->StopFireFromAutomation();Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("cola")));}
AHeliosEnemyActor* AHeliosColaQA::TargetAt(const FVector& Location)
{
    FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Target=GetWorld()->SpawnActor<AHeliosEnemyActor>(Location,FRotator::ZeroRotator,P);
    if(Target)
    {
        Target->StopShooting();Target->CurrentHP=10000;Target->GetCharacterMovement()->DisableMovement();Target->SetActorTickEnabled(false);
        if(auto* AI=Cast<AAIController>(Target->GetController())){if(AI->GetBrainComponent())AI->GetBrainComponent()->StopLogic(TEXT("Cola QA"));AI->StopMovement();}
        Targets.Add(Target);
    }
    return Target;
}
AStaticMeshActor* AHeliosColaQA::Box(const FVector& Location,const FVector& Scale)
{
    auto* B=GetWorld()->SpawnActor<AStaticMeshActor>(Location,FRotator::ZeroRotator);
    B->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
    B->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    B->SetActorScale3D(Scale);B->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));return B;
}
AHeliosColaProjectile* AHeliosColaQA::SpawnCan(const FVector& Location,bool Stationary,float Fuse)
{
    const FTransform T(FRotator::ZeroRotator,Location);
    auto* P=GetWorld()->SpawnActorDeferred<AHeliosColaProjectile>(AHeliosColaProjectile::StaticClass(),T,Player,Player,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    auto* D=NewObject<UHeliosColaWeaponDefinition>(this);D->FuseSeconds=Fuse;
    P->Configure(FVector::ForwardVector,D);
    if(Stationary){auto* M=P->FindComponentByClass<UProjectileMovementComponent>();M->Velocity=FVector::ZeroVector;M->ProjectileGravityScale=0;}
    UGameplayStatics::FinishSpawningActor(P,T);return P;
}
void AHeliosColaQA::Capture(const FString& N)
{
    FString Tag;FParse::Value(FCommandLine::Get(),TEXT("ColaCapture="),Tag);
    const FString Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/cola")/Tag/(N+TEXT(".png")));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);FScreenshotRequest::RequestScreenshot(Path,true,false);
}
void AHeliosColaQA::Tick(float Dt)
{
    Super::Tick(Dt);if(bDone)return;
    if(!Started)Started=FPlatformTime::Seconds();
    if(FPlatformTime::Seconds()-Started>240){Check(TEXT("timeout"),false);Finish();return;}
    if(!Player)Player=Cast<AHeliosAdventureCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));
    if(!Director)Director=AHeliosAdventureDirector::Find(GetWorld());if(!Player||!Director)return;
    Age+=Dt;
    auto* PC=Cast<APlayerController>(Player->GetController());auto* HUD=PC?Cast<AHeliosAdventureHUD>(PC->GetHUD()):nullptr;
    if(!PC||!HUD)return;
    if(Stage>=200&&Stage<300){CinematicTick(Dt);return;}
    switch(Stage)
    {
    case 0:
        // Town finishes loading its saved selection asynchronously; wait for both
        // the world and HUD before choosing the weapon used by this fixture.
        if(auto* Town=GetWorld()->GetSubsystem<UHeliosTownSubsystem>();Town&&Town->IsTownWorld()&&!Town->IsWorldReady())break;
        if(HUD->GetMenuState()!=EHeliosAdventureMenu::WeaponSelect)break;
        bDemo=FParse::Param(FCommandLine::Get(),TEXT("HeliosColaDemo"));bMenuOnly=FParse::Param(FCommandLine::Get(),TEXT("HeliosColaMenu"));
        if(FParse::Param(FCommandLine::Get(),TEXT("ColaExpectSaved")))
            if(auto* Town=GetWorld()->GetSubsystem<UHeliosTownSubsystem>())
                Check(TEXT("fresh_process_restores_saved_cola"),Town->GetSavedWeaponId()==TEXT("cola"));
        HUD->SelectWeapon(TEXT("cola"));
        Check(TEXT("menu_selection_accepts_cola"),HUD->GetPendingWeaponId()==TEXT("cola"));
        {
            bool HasExistingWeapons=true;
            for(const auto& Id:{TEXT("rifle"),TEXT("rocket"),TEXT("laser"),TEXT("shotgun"),TEXT("chopsticks"),TEXT("cola")})
                HasExistingWeapons&=UHeliosWeaponRegistry::FindWeapon(Id)!=nullptr;
            Check(TEXT("existing_weapon_definitions_preserved"),HasExistingWeapons,
                FString::Printf(TEXT("registered=%d"),UHeliosWeaponRegistry::GetAvailableWeapons().Num()));
        }
        Check(TEXT("editable_cola_definition"),Cast<UHeliosColaWeaponDefinition>(UHeliosWeaponRegistry::FindWeapon(TEXT("cola")))!=nullptr);
        if(const auto* D=Cast<UHeliosColaWeaponDefinition>(UHeliosWeaponRegistry::FindWeapon(TEXT("cola"))))
            Check(TEXT("registered_blast_radius_cm"),D->OuterRadius>0,FString::SanitizeFloat(D->OuterRadius));
        Next(1);break;
    case 1:
        if(FPlatformTime::Seconds()-WallStart<1.5)break;
        Capture(TEXT("01_menu"));Next(2);break;
    case 2:
        if(FPlatformTime::Seconds()-WallStart<.5)break;
        if(auto* Town=GetWorld()->GetSubsystem<UHeliosTownSubsystem>();Town&&Town->IsTownWorld()&&!Town->IsWorldReady())break;
        if(bMenuOnly){Finish();break;}
        HUD->StartSelectedRun();Director->SetActorTickEnabled(false);Player->SetCanBeDamaged(false);
        Check(TEXT("selection_equips_cola"),Player->GetWeaponId()==TEXT("cola")&&Player->GetAmmo()==20);
        Check(TEXT("hands_and_actions_ready"),Player->GetPresentation()&&Player->GetPresentation()->IsReady());
        if(auto* Town=GetWorld()->GetSubsystem<UHeliosTownSubsystem>();Town&&Town->IsTownWorld())
        {
            Check(TEXT("town_cola_save_succeeds"),Town->GetSavedWeaponId()==TEXT("cola")&&Town->SaveProgress());
            FString Saved;TSharedPtr<FJsonObject> Json;
            const bool Read=FFileHelper::LoadFileToString(Saved,*(FPaths::ProjectSavedDir()/TEXT("SaveGames/HeliosColaTown.json")))&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Saved),Json);
            Check(TEXT("town_save_contains_cola_id"),Read&&Json.IsValid()&&Json->GetStringField(TEXT("weapon_id"))==TEXT("cola"));
        }
        Next(3);break;
    case 3:
        if(Age<.8f)break;Capture(TEXT("02_held_can"));
        if(FParse::Param(FCommandLine::Get(),TEXT("HeliosDrinkDemo")))
        {
            bDrinkDemo=true;Aim=PC->GetControlRotation();Aim.Pitch=-8;DrinkIndex=0;Next(400);break;
        }
        if(bDemo)
        {
            Aim=PC->GetControlRotation();Aim.Pitch=-8;Origin=Player->GetActorLocation();
            const FVector Base=Origin+Aim.Vector()*1000;
            for(int I=0;I<3;++I)TargetAt(Base+FRotationMatrix(Aim).GetUnitAxis(EAxis::Y)*(I-1)*140);
            Next(100);break;
        }
        Player->SetActorLocation(Origin,false,nullptr,ETeleportType::TeleportPhysics);Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
        PC->SetControlRotation(Aim);Floor=Box(Origin+FVector(800,0,-150),FVector(70,70,1));
        if(FParse::Param(FCommandLine::Get(),TEXT("HeliosColaCinematic"))){Next(200);return;}
        Next(10);break;
    case 10:
        if(auto* W=Cast<AHeliosColaWeapon>(Player->GetEquippedWeapon())){DrinkBefore=W->GetDrinkId();DrinkDrawsBefore=W->GetDrinkDrawCount();}
        Shot();Check(TEXT("tap_starts_windup_without_ammo_cost"),Player->GetAmmo()==20&&Count()==0);Next(11);break;
    case 11:
        if(Age<.20f)break;Check(TEXT("no_release_before_0_35"),Player->GetAmmo()==20&&Count()==0);Next(12);break;
    case 12:
        if(Age<.21f)break;Can=LastCan();
        Check(TEXT("thrown_drink_matches_held"),Can&&Can->GetDrinkId()==DrinkBefore);
        if(auto* W=Cast<AHeliosColaWeapon>(Player->GetEquippedWeapon()))Check(TEXT("one_draw_after_committed_throw"),W->GetDrinkDrawCount()==DrinkDrawsBefore+1);
        Check(TEXT("tap_release_commits_exactly_one"),Player->GetAmmo()==19&&Count()==1);
        Check(TEXT("world_can_visible"),IsValid(Can)&&!Can->IsHidden());Next(13);break;
    case 13:
        if(Age<1.2f)break;
        Check(TEXT("ground_impact_explodes_before_old_fuse"),!IsValid(Can)||Can->IsActorBeingDestroyed());
        Can=SpawnCan(Origin+FVector(0,0,1000),true);
        Next(14);break;
    case 14:
        if(Age<2.4f)break;Check(TEXT("airborne_can_waits_for_impact"),IsValid(Can)&&!Can->IsActorBeingDestroyed());if(IsValid(Can))Can->Destroy();Equip();Player->StartFireFromAutomation();Next(15);break;
    case 15:
        if(Age<2.6f)break;Check(TEXT("hold_only_throws_one"),Player->GetAmmo()==19);
        Player->StopFireFromAutomation();Shot();Shot();Shot();Next(16);break;
    case 16:
        if(Age<.45f)break;Check(TEXT("rapid_clicks_no_duplicate"),Player->GetAmmo()==18);Shot();Next(17);break;
    case 17:
        if(Age<.85f)break;Check(TEXT("cooldown_click_not_queued"),Player->GetAmmo()==18);Shot();Next(18);break;
    case 18:
        if(Age<1.2f)break;
        if(Player->GetAmmo()>0){Shot();Next(18);break;}
        Check(TEXT("twenty_cans_empty"),Player->GetAmmo()==0);Shot();
        Check(TEXT("empty_does_not_go_negative"),Player->GetAmmo()==0);
        {bool Visible=false;for(TActorIterator<AHeliosColaWeapon> I(GetWorld());I;++I)if(I->GetOwner()==Player)Visible|=I->GetFirstPersonMesh()->IsVisible();Check(TEXT("empty_hand_hides_can"),!Visible);}
        Player->StartReloadFromAutomation();Shot();Next(19);break;
    case 19:
        if(Age<1.3f)break;Check(TEXT("reload_blocks_attack_and_early_refill"),Player->GetAmmo()==0&&Player->IsReloading());Next(20);break;
    case 20:
        if(Age<.6f)break;Check(TEXT("reload_1_8_fills_twenty"),Player->GetAmmo()==20&&!Player->IsReloading());
        Equip();Shot();Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("rifle")));Before=Count();Next(21);break;
    case 21:
        if(Age<.5f)break;Check(TEXT("switch_before_release_cancels_throw"),Count()<=Before);Equip();Shot();Next(22);break;
    case 22:
        if(Age<.45f)break;Can=LastCan();Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("rifle")));
        Check(TEXT("switch_after_release_preserves_can"),IsValid(Can)&&!Can->IsActorBeingDestroyed());
        PauseFuseAge=IsValid(Can)?Can->GetFuseAge():-1;UGameplayStatics::SetGamePaused(this,true);Next(23);break;
    case 23:
        if(FPlatformTime::Seconds()-WallStart<.55)break;
        Check(TEXT("pause_freezes_flight"),IsValid(Can)&&FMath::IsNearlyEqual(Can->GetFuseAge(),PauseFuseAge,.04f));
        UGameplayStatics::SetGamePaused(this,false);Next(24);break;
    case 24:
        if(Age<2.4f)break;Check(TEXT("unpause_finishes_explosion"),Count()==0);Next(30);break;
    case 30:
        Equip();Aim=Directions[DirectionIndex];PC->SetControlRotation(Aim);Shot();Next(31);break;
    case 31:
        if(Age<.4f)break;Can=LastCan();
        if(IsValid(Can))
        {
            FVector Expected=Aim.Vector()*3200+FVector(0,0,250-980*Can->GetFuseAge());
            Check(FString::Printf(TEXT("throw_direction_%d"),DirectionIndex),FVector::DotProduct(Can->GetVelocity().GetSafeNormal(),Expected.GetSafeNormal())>.985f);
            Can->Destroy();
        }else Check(TEXT("direction_can_spawned"),false);
        if(++DirectionIndex<UE_ARRAY_COUNT(Directions))Next(30);else Next(32);break;
    case 32:
        Aim=FRotator::ZeroRotator;PC->SetControlRotation(Aim);Equip();
        Wall=Box(Player->FirstPersonCamera->GetComponentLocation()+FVector(12,0,0),FVector(.1,5,5));Shot();Next(33);break;
    case 33:
        if(Age<.5f)break;Check(TEXT("near_wall_no_spawn_no_ammo_cost"),Count()==0&&Player->GetAmmo()==20);Wall->Destroy();Wall=nullptr;
        // A broad staircase verifies immediate detonation on a swept collision.
        for(int I=0;I<4;++I)Box(Origin+FVector(700+I*100,0,-120+I*25),FVector(1,8,.5));
        Can=SpawnCan(Origin+FVector(550,0,150));Next(34);break;
    case 34:
        if(Age<1.8f)break;Check(TEXT("stairs_impact_explodes"),!IsValid(Can)||Can->IsActorBeingDestroyed());
        if(IsValid(Can))Can->Destroy();
        Blast=Origin+FVector(0,1600,100);
        TargetAt(Blast+FVector(240,0,0));TargetAt(Blast+FVector(-900,0,0));TargetAt(Blast+FVector(0,-1500,0));TargetAt(Blast+FVector(0,2100,0));
        TargetAt(Blast+FVector(0,900,0));Wall=Box(Blast+FVector(0,450,0),FVector(2,.1,4));
        TargetAt(Blast+FVector(660,660,0))->Tags.AddUnique(TEXT("Player"));
        // Multiple overlap components must still incur one radial hit.
        {auto* S=NewObject<USphereComponent>(Targets[0]);S->InitSphereRadius(35);S->SetupAttachment(Targets[0]->GetRootComponent());
        S->SetCollisionObjectType(ECC_Pawn);S->SetCollisionEnabled(ECollisionEnabled::QueryOnly);S->SetCollisionResponseToAllChannels(ECR_Ignore);S->RegisterComponent();}
        Can=SpawnCan(Blast,true,.4f);static_cast<AActor*>(Can.Get())->NotifyHit(nullptr,Floor,nullptr,true,Blast,FVector::UpVector,FVector::ZeroVector,FHitResult());Next(35);break;
    case 35:
        if(Age<.6f)break;
        {
            const float Expected[]={360,220,80,0,0,0};
            for(int I=0;I<6;++I)Check(FString::Printf(TEXT("blast_target_%d_expected_%.0f"),I,Expected[I]),
                FMath::IsNearlyEqual(10000-Targets[I]->CurrentHP,Expected[I],.5f),FString::SanitizeFloat(10000-Targets[I]->CurrentHP));
            const float GroundZ=Floor->GetStaticMeshComponent()->Bounds.Origin.Z+Floor->GetStaticMeshComponent()->Bounds.BoxExtent.Z;
            const float Height=Blast.Z-GroundZ;
            const float ExpectedRadius=FMath::Sqrt(1500.f*1500.f-Height*Height);
            bool FoundSplash=false;
            for(TActorIterator<AHeliosColaSplash> I(GetWorld());I;++I)
            {
                if(FVector::Dist2D(I->GetActorLocation(),Blast)>2)continue;
                FoundSplash=true;
                Check(TEXT("splash_retains_damage_sphere_ground_intersection"),FMath::IsNearlyEqual(I->GetBlastRadius(),ExpectedRadius,1.f));
                TInlineComponentArray<UStaticMeshComponent*> Meshes(*I);
                bool HasRing=false;
                for(auto* Mesh:Meshes)HasRing|=Mesh->GetName()==TEXT("Foam pressure ring");
                Check(TEXT("ground_white_ring_removed"),!HasRing);
            }
            Check(TEXT("impact_preserves_ground_splash"),FoundSplash);
            int32 BurstCount=0;
            for(TObjectIterator<UNiagaraComponent> I;I;++I)
            {
                if(I->GetWorld()!=GetWorld()||!I->GetAsset()||!I->IsActive()||!I->GetComponentLocation().Equals(Blast,2))continue;
                const FString Name=I->GetAsset()->GetName();
                const float AuthoredRadius=Name.StartsWith(TEXT("NS_ColaFoam"))||Name.StartsWith(TEXT("NS_ColaSpray"))?HeliosColaVFX::Envelope():0;
                if(AuthoredRadius>0){++BurstCount;Check(Name+TEXT("_envelope_matches_damage_radius"),FMath::IsNearlyEqual(I->GetComponentScale().X*AuthoredRadius,1500.f,1.f));}
            }
            // NullRHI can suppress GPU particle activation; rendered runs must see both bursts.
            if(!FParse::Param(FCommandLine::Get(),TEXT("nullrhi")))Check(TEXT("both_blast_bursts_scaled"),BurstCount==2);
            auto* Custom=GetWorld()->SpawnActorDeferred<AHeliosColaSplash>(AHeliosColaSplash::StaticClass(),FTransform(Blast),Player,Player,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if(Custom)
            {
                Custom->ConfigureBlastRadius(500);UGameplayStatics::FinishSpawningActor(Custom,FTransform(Blast));Custom->Tick(.5f);
                TInlineComponentArray<UStaticMeshComponent*> Meshes(Custom);
                bool HasRing=false;
                for(auto* Mesh:Meshes)HasRing|=Mesh->GetName()==TEXT("Foam pressure ring");
                Check(TEXT("custom_radius_has_no_ground_ring"),!HasRing);
                Custom->Destroy();
            }
            if(!FParse::Param(FCommandLine::Get(),TEXT("nullrhi")))
            {
                const FVector CameraLocation=Blast+FVector(-2300,-2300,2300);
                if(auto* Camera=GetWorld()->SpawnActor<ACameraActor>(CameraLocation,(Blast-CameraLocation).Rotation()))
                {
                    Camera->SetLifeSpan(.45f);PC->SetViewTarget(Camera);
                    DrawDebugSphere(GetWorld(),Blast,1500,48,FColor::Cyan,false,.4f,0,3);
                    Capture(TEXT("03_blast_radius_alignment"));
                }
            }
        }
        Check(TEXT("impact_triggers_radial_blast"),!IsValid(Can)||Can->IsActorBeingDestroyed());
        Next(351);break;
    case 351:
        // Let the requested screenshot render before triggering the owner-damage test.
        if(Age<.5f)break;
        Before=Player->GetHealth();Player->SetCanBeDamaged(true);Can=SpawnCan(Player->GetActorLocation()+FVector(20,0,0),true,.3f);static_cast<AActor*>(Can.Get())->NotifyHit(nullptr,Floor,nullptr,true,Can->GetActorLocation(),FVector::UpVector,FVector::ZeroVector,FHitResult());Next(36);break;
    case 36:
        PC->SetViewTarget(Player);
        if(Age<.6f)break;Check(TEXT("no_owner_damage"),Player->GetHealth()==Before);
        Equip();Shot();Player->CancelCombatAndControl();Next(37);break;
    case 37:
        if(Age<.5f)break;Check(TEXT("cancel_pending_no_ammo_cost"),Count()==0&&Player->GetAmmo()==20);
        Shot();Next(38);break;
    case 38:
        if(Age<.8f)break; // cooldown rejection itself leaves no queued attack
        Shot();Next(39);break;
    case 39:
        if(Age<.4f)break;Director->RestartRun();Director->SetActorTickEnabled(false);
        Check(TEXT("restart_keeps_cola_and_refills"),Player->GetWeaponId()==TEXT("cola")&&Player->GetAmmo()==20);
        Check(TEXT("restart_cleans_projectiles"),Count()==0);Next(40);break;
    case 40:
        if(Age<4.5f)break;Check(TEXT("effects_and_splashes_cleanup"),UHeliosWeaponVFX::GetTrackedCount(this)==0 && !TActorIterator<AHeliosColaSplash>(GetWorld()));
        for(const auto& Id:{TEXT("rifle"),TEXT("rocket"),TEXT("laser"),TEXT("shotgun"),TEXT("chopsticks"),TEXT("cola")})
            Check(FString(Id)+TEXT("_equip_compatibility"),Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(Id)));
        Shot();Player->SetCanBeDamaged(true);UGameplayStatics::ApplyDamage(Player,10000,nullptr,this,UDamageType::StaticClass());Next(41);break;
    case 41:
        if(FPlatformTime::Seconds()-WallStart<.5)break;
        Check(TEXT("death_cancels_pending_throw"),Count()==0);Finish();break;
    case 100:
        PC->SetControlRotation(Aim);
        if(DemoShot<3 && Age>1.f+DemoShot*2.4f){Shot();++DemoShot;}
        if(Age>7.3f && Age<7.5f)Player->StartReloadFromAutomation();
        if(Age>9.5f&&DemoShot==3){Shot();++DemoShot;}
        if(Age>=13.f){Check(TEXT("demo_four_throws_and_reload"),Player->GetPresentation()->GetShotPresentationCount()==4);Finish();break;}
        Capture(FString::Printf(TEXT("frames/frame_%05d"),Frame++));break;
    case 400:
        if(auto* W=Cast<AHeliosColaWeapon>(Player->GetEquippedWeapon()))
        {
            Check(FString::Printf(TEXT("variant_%d_selectable"),DrinkIndex),W->SelectDrinkForQA(DrinkIndex));
            DrinkBefore=W->GetDrinkId();DrinkDrawsBefore=W->GetDrinkDrawCount();
        }
        PC->SetControlRotation(Aim);Next(401);break;
    case 401:
        if(Age<.65f)break;
        DrinkFrames.Add(TEXT("held_")+DrinkBefore.ToString(),Frame);Next(402);break;
    case 402:
        if(Age<.35f)break;
        Shot();Next(403);break;
    case 403:
        if(Age<.20f)break;
        DrinkFrames.Add(TEXT("prepare_")+DrinkBefore.ToString(),Frame);Next(404);break;
    case 404:
        if(Age<.17f)break;
        Can=LastCan();
        Check(TEXT("flight_matches_")+DrinkBefore.ToString(),Can&&Can->GetDrinkId()==DrinkBefore);
        if(auto* W=Cast<AHeliosColaWeapon>(Player->GetEquippedWeapon()))
            Check(TEXT("next_draw_once_")+DrinkBefore.ToString(),W->GetDrinkDrawCount()==DrinkDrawsBefore+1);
        DrinkFrames.Add(TEXT("release_")+DrinkBefore.ToString(),Frame);Next(405);break;
    case 405:
        if(Age<.72f)break;
        DrinkFrames.Add(TEXT("recover_")+DrinkBefore.ToString(),Frame);
        if(++DrinkIndex<5){Next(400);break;}
        if(auto* W=Cast<AHeliosColaWeapon>(Player->GetEquippedWeapon())){DrinkBefore=W->GetDrinkId();DrinkDrawsBefore=W->GetDrinkDrawCount();}
        Player->StartReloadFromAutomation();Next(406);break;
    case 406:
        if(Age<.5f)break;DrinkFrames.Add(TEXT("reload_lowered"),Frame);
        if(auto* W=Cast<AHeliosColaWeapon>(Player->GetEquippedWeapon()))Check(TEXT("reload_does_not_draw_early"),W->GetDrinkId()==DrinkBefore&&W->GetDrinkDrawCount()==DrinkDrawsBefore);
        Next(407);break;
    case 407:
        if(Age<1.4f)break;
        if(auto* W=Cast<AHeliosColaWeapon>(Player->GetEquippedWeapon()))
        {
            Check(TEXT("completed_reload_draws_once"),W->GetDrinkDrawCount()==DrinkDrawsBefore+1&&Player->GetAmmo()==20);
            DrinkBefore=W->GetDrinkId();DrinkDrawsBefore=W->GetDrinkDrawCount();
            W->BeginReload(1.8f);W->CancelReload();
            Check(TEXT("cancelled_reload_preserves_drink"),W->GetDrinkId()==DrinkBefore&&W->GetDrinkDrawCount()==DrinkDrawsBefore);
            // Actual wall rejection exercises the commit boundary, rather than only RNG helpers.
            Player->GetCharacterMovement()->DisableMovement();
            const FVector Eye=Player->FirstPersonCamera->GetComponentLocation();
            Wall=Box(Eye+Aim.Vector()*18,FVector(.10,2,2));Shot();
        }
        Next(408);break;
    case 408:
        if(Age<.5f)break;
        if(auto* W=Cast<AHeliosColaWeapon>(Player->GetEquippedWeapon()))
            Check(TEXT("blocked_throw_preserves_drink_and_draws"),W->GetDrinkId()==DrinkBefore&&W->GetDrinkDrawCount()==DrinkDrawsBefore&&Player->GetAmmo()==20,
                FString::Printf(TEXT("ammo=%d draws=%d before=%d"),Player->GetAmmo(),W->GetDrinkDrawCount(),DrinkDrawsBefore));
        if(Wall)Wall->Destroy();DrinkFrames.Add(TEXT("reload_finished"),Frame);Next(409);break;
    case 409:
        if(Age<.5f)break;
        Check(TEXT("five_drinks_presented"),DrinkIndex==5);
        if(auto* W=Cast<AHeliosColaWeapon>(Player->GetEquippedWeapon()))
        {
            int32 Seed=9192026;FParse::Value(FCommandLine::Get(),TEXT("HeliosDrinkSeed="),Seed);FRandomStream Expected(Seed);
            for(int32 I=0;I<W->GetDrinkDrawCount();++I)Expected.RandRange(0,4);
            const FName Ids[]={TEXT("cola"),TEXT("sprite"),TEXT("fanta"),TEXT("beer"),TEXT("redbull")};
            TSet<FName> Seen;bool Match=true;
            for(int32 I=0;I<64;++I){W->ReloadMagazine();Seen.Add(W->GetDrinkId());Match&=W->GetDrinkId()==Ids[Expected.RandRange(0,4)];}
            Check(TEXT("seeded_random_stream_is_reproducible"),Match);
            Check(TEXT("seeded_draws_cover_all_five_drinks"),Seen.Num()==5);
        }
        Finish();break;
    }
    if(bDrinkDemo&&!bDone)Capture(FString::Printf(TEXT("frames/frame_%05d"),Frame++));
}
void AHeliosColaQA::Finish()
{
    bDone=true;if(Player)Player->StopFireFromAutomation();
    auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("status"),Failures?TEXT("failed"):TEXT("passed"));R->SetNumberField(TEXT("failures"),Failures);
    R->SetStringField(TEXT("map"),GetWorld()->GetMapName());R->SetArrayField(TEXT("checks"),Checks);R->SetNumberField(TEXT("capture_frames"),Frame);
    auto KeyFrames=MakeShared<FJsonObject>();for(const auto& Pair:DrinkFrames)KeyFrames->SetNumberField(Pair.Key,Pair.Value);R->SetObjectField(TEXT("drink_keyframes"),KeyFrames);
    FString Out;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Out));
    FString Tag;FParse::Value(FCommandLine::Get(),TEXT("ColaCapture="),Tag);
    const FString Dir=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/cola")/Tag);
    IFileManager::Get().MakeDirectory(*Dir,true);FFileHelper::SaveStringToFile(Out,*(Dir/TEXT("runtime_report.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    FPlatformMisc::RequestExit(false);
}
