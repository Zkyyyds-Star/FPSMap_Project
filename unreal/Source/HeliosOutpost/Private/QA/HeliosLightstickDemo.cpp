#include "QA/HeliosLightstickDemo.h"
#include "Weapons/HeliosLightstickWeapon.h"
#include "Weapons/HeliosLightstickWave.h"
#include "Components/SphereComponent.h"
#include "ShaderCompiler.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Adventure/HeliosAdventureCharacter.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "UI/HeliosAdventureHUD.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Weapons/HeliosFirstPersonPresentationComponent.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

AHeliosLightstickDemo::AHeliosLightstickDemo(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
AHeliosLightstickTestTarget::AHeliosLightstickTestTarget()
{
    Sphere=CreateDefaultSubobject<USphereComponent>(TEXT("Contact target"));RootComponent=Sphere;Sphere->InitSphereRadius(20);
    Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Sphere->SetCollisionObjectType(ECC_Pawn);Sphere->SetCollisionResponseToAllChannels(ECR_Ignore);Sphere->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    SetCanBeDamaged(true);
}
void AHeliosLightstickDemo::RunFocusedChecks()
{
    auto* W=Cast<AHeliosLightstickWeapon>(Player->GetEquippedWeapon());if(!W){Check(TEXT("weapon_available"),false);return;}
    auto* D=W->Settings.Get();
    Check(TEXT("saved_damage_and_timing"),D&&D->ComboDamage==TArray<float>({105,120,180})&&FMath::IsNearlyEqual(D->ComboSeconds[0],.21f)&&FMath::IsNearlyEqual(D->ComboSeconds[1],.23f)&&FMath::IsNearlyEqual(D->ComboSeconds[2],.325f)&&D->Range==500&&D->WaveRange==10000&&D->WaveSpeed==10000&&D->WaveAngleDegrees==180&&D->WaveSegments==128);
    FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    FVector Eye=Player->FirstPersonCamera->GetComponentLocation();
    auto* T=GetWorld()->SpawnActor<AHeliosLightstickTestTarget>(Eye+FVector(110,0,0),FRotator::ZeroRotator,P);T->Sphere->SetSphereRadius(115);
    Target->SetActorEnableCollision(false);for(auto& E:FarTargets)E->SetActorEnableCollision(false);W->CancelAttack();const int32 Serial=W->GetAttackSerial();W->StartFiring();W->Tick(.77f);W->StopFiring();
    Check(TEXT("low_fps_preserves_765ms_combo"),W->GetAttackSerial()-Serial==4);
    Check(TEXT("three_hits_105_120_180_once"),T->HitCount==3&&FMath::IsNearlyEqual(T->TotalDamage,405.f,.01f));W->CancelAttack();
    UE_LOG(LogTemp,Display,TEXT("ENCORE_DAMAGE observed=%f hits=%d"),T->TotalDamage,T->HitCount);
    T->Sphere->SetSphereRadius(5);T->TotalDamage=0;T->SetActorLocation(Eye+FVector(504,0,0));W->HitActors.Reset();W->TraceBlade(Eye+FVector(30,0,0),Eye+FVector(500,0,0),105);
    Check(TEXT("499cm_contact_hits"),T->TotalDamage==105);
    T->TotalDamage=0;T->SetActorLocation(Eye+FVector(506,0,0));W->HitActors.Reset();W->TraceBlade(Eye+FVector(30,0,0),Eye+FVector(500,0,0),105);
    Check(TEXT("501cm_contact_rejected_despite_sweep_radius"),T->TotalDamage==0);T->Destroy();
    FVector Lane=Eye+FVector(0,0,15000);TArray<AHeliosLightstickTestTarget*> Targets;
    const TArray<FVector> Points={FVector(2000,0,0),FVector(5000,0,0),FVector(1200,-6800,0),FVector(1200,6800,0),FVector(6000,1000,1500),FVector(9900,0,0),FVector(10040,0,0),FVector(-1000,0,0),FVector(4000,4000,0)};
    for(const FVector& Point:Points){auto* A=GetWorld()->SpawnActor<AHeliosLightstickTestTarget>(Lane+Point,FRotator::ZeroRotator,P);A->Sphere->SetSphereRadius(32);Targets.Add(A);}
    P.Owner=W;P.Instigator=Player;
    auto Wave=[&](float Charge){auto* A=GetWorld()->SpawnActor<AHeliosLightstickWave>(Lane,FRotator::ZeroRotator,P);A->Initialize(D,Charge,Lane);W->OwnedWaves.Add(A);return A;};
    auto Reset=[&](){for(auto* B:Targets){B->TotalDamage=0;B->HitCount=0;}};
    Check(TEXT("saved_height_and_translucency"),D->WaveHeightOffset==-45&&FMath::IsNearlyEqual(D->WaveBodyOpacity,.25f)&&FMath::IsNearlyEqual(D->WaveEdgeOpacity,.12f)&&D->WaveGhostOpacity<=.08f&&D->WaveMaterial.LoadSynchronous()->GetBlendMode()==BLEND_Translucent&&D->SlashMesh.LoadSynchronous()&&D->SlashMaterial.LoadSynchronous());
    auto* Visual=Wave(0);
    bool Flat=true,RadiusAligned=true;
    for(float R:{500.f,3000.f,9990.f})
    {
        Visual->UpdateWaveMesh(R);
        const auto& V=Visual->WaveSurface->GetProcMeshSection(1)->ProcVertexBuffer;
        for(int32 I=0;I<V.Num();I+=4)
        {
            FVector C=(V[I].Position+V[I+1].Position+V[I+2].Position+V[I+3].Position)*.25;
            Flat&=FMath::IsNearlyEqual(float(C.Z),-45.f,.01f);RadiusAligned&=FMath::IsNearlyEqual(float(C.Size()),R,.1f);
        }
    }
    Check(TEXT("near_mid_far_blade_fixed_45cm_below_eye"),Flat);Check(TEXT("visual_matches_spherical_expansion_radius"),RadiusAligned);Visual->Destroy();W->ClearSlashEffects();
    auto* A=Wave(1);A->Tick(.19f);Check(TEXT("wave_does_not_hit_before_arrival"),Targets[0]->HitCount==0);
    A->Tick(.01f);Check(TEXT("20m_target_hit_after_02_seconds"),Targets[0]->TotalDamage==360&&Targets[1]->HitCount==0);
    A->Tick(.3f);Check(TEXT("50m_target_hit_after_05_seconds"),Targets[1]->TotalDamage==360&&Targets[2]->HitCount==0);
    A->Tick(.5f);
    bool All=true;for(int32 I:{0,1,2,3,4,5,8})All&=Targets[I]->TotalDamage==360&&Targets[I]->HitCount==1;
    Check(TEXT("180_degree_left_center_right_height_pierce_once"),All);
    Check(TEXT("behind_and_beyond_100m_untouched"),Targets[6]->HitCount==0&&Targets[7]->HitCount==0);
    Check(TEXT("fixed_origin_expires_at_100m_in_one_second"),!A->IsFlying()&&FMath::IsNearlyEqual(A->GetDistanceTravelled(),10000.f,.01f)&&A->GetActorLocation().Equals(Lane,.01f));A->Destroy();Reset();
    auto* Block=GetWorld()->SpawnActor<AStaticMeshActor>(Lane+FVector(3000,0,0),FRotator::ZeroRotator,P);
    Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);Block->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Block->SetActorScale3D(FVector(.1f,4,8));Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
    A=Wave(0);A->Tick(.5f);Check(TEXT("central_wall_does_not_destroy_wave"),A->IsFlying()&&Targets[0]->TotalDamage==180&&Targets[1]->HitCount==0);
    A->Tick(.5f);Check(TEXT("wall_blocks_only_covered_direction"),Targets[1]->HitCount==0&&Targets[5]->HitCount==0&&Targets[2]->TotalDamage==180&&Targets[3]->TotalDamage==180&&Targets[4]->TotalDamage==180&&Targets[8]->TotalDamage==180);A->Destroy();
    int32 WallSlot=INDEX_NONE,WallCount=0;for(int32 I=24;I<W->SlashMarks.Num();++I)if(W->SlashAges[I]<1){WallSlot=I;++WallCount;}
    Check(TEXT("wall_regions_merge_without_segment_stride"),WallCount>0&&WallCount<=4);
    Check(TEXT("wall_slash_survives_wave_destruction"),WallSlot!=INDEX_NONE&&W->SlashMarks[WallSlot]->IsVisible());
    if(WallSlot!=INDEX_NONE)
    {
        Check(TEXT("wall_slash_is_surface_aligned_120_to_180cm"),FVector::DotProduct(W->SlashMarks[WallSlot]->GetUpVector(),W->SlashNormals[WallSlot])>.99f&&W->SlashMarks[WallSlot]->GetComponentScale().X>=1.2f&&W->SlashMarks[WallSlot]->GetComponentScale().X<=1.8f);
        FHitResult H(Targets[0],Targets[0]->Sphere,Lane+FVector(2000,0,0),-FVector::ForwardVector);H.ImpactPoint=Lane+FVector(2000,0,0);H.ImpactNormal=-FVector::ForwardVector;
        for(int32 I=0;I<30;++I)W->PlaySlashImpact(H,FVector::RightVector,false);
        Check(TEXT("enemy_bursts_cannot_evict_wall_pool"),W->SlashMarks[WallSlot]->IsVisible()&&W->SlashMarks.Num()==32&&W->SlashFragments.Num()==48);
        W->UpdateSlashEffects(.26f);bool EnemiesGone=true;for(int32 I=0;I<24;++I)EnemiesGone&=!W->SlashMarks[I]->IsVisible();
        Check(TEXT("enemy_cut_fades_after_025_seconds"),EnemiesGone);
        W->UpdateSlashEffects(.69f);Check(TEXT("wall_cut_remains_for_full_second"),W->SlashMarks[WallSlot]->IsVisible());
        W->UpdateSlashEffects(.06f);Check(TEXT("wall_cut_fades_after_one_second"),!W->SlashMarks[WallSlot]->IsVisible());
    }
    Reset();
    Block->SetActorLocation(Lane+FVector(30,0,0));Block->SetActorScale3D(FVector(.1f,.1f,8));A=Wave(1);A->Tick(.75f);
    Check(TEXT("near_wall_blocks_center_but_side_arcs_survive"),A->IsFlying()&&Targets[1]->HitCount==0&&Targets[2]->TotalDamage==360&&Targets[3]->TotalDamage==360);A->Destroy();Block->Destroy();
    for(auto* B:Targets)B->Destroy();
    W->CancelAttack();W->ClearWaves();W->ClearSlashEffects();Target->SetActorEnableCollision(true);for(auto& E:FarTargets)E->SetActorEnableCollision(true);
    // Finish GPU material jobs once, including the actual wave material, before recording.
    if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
}
void AHeliosLightstickDemo::Check(const FString& Name,bool Passed)
{
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosLightstickCaptureOnly"))&&Name!=TEXT("trigger_volume_does_not_block_wave")&&!Name.StartsWith(TEXT("visual_")))return;
    if(!Passed)++Failures;if(!Results.IsEmpty())Results+=TEXT(",\n");
    Results+=FString::Printf(TEXT("{\"name\":\"%s\",\"passed\":%s}"),*Name,Passed?TEXT("true"):TEXT("false"));
    UE_LOG(LogTemp,Display,TEXT("LIGHTSTICK_CHECK %s %s"),*Name,Passed?TEXT("PASS"):TEXT("FAIL"));
}
void AHeliosLightstickDemo::Capture(const FString& Name)
{
    const FString Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/lightstick")/(Name+TEXT(".png")));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);FScreenshotRequest::RequestScreenshot(Path,true,false);
}
void AHeliosLightstickDemo::Tick(float Dt)
{
    Super::Tick(Dt);if(Stage==99)return;
    if(!Started)Started=FPlatformTime::Seconds();
    if(FPlatformTime::Seconds()-Started>420){Check(TEXT("timeout"),false);Stage=98;}
    if(!Player)Player=Cast<AHeliosAdventureCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));
    if(!Director)Director=AHeliosAdventureDirector::Find(GetWorld());if(!Player||!Director)return;
    auto* PC=Cast<APlayerController>(Player->GetController());auto* HUD=PC?Cast<AHeliosAdventureHUD>(PC->GetHUD()):nullptr;if(!HUD)return;
    Age+=Dt;auto* Weapon=Cast<AHeliosLightstickWeapon>(Player->GetEquippedWeapon());
    if(Stage==0){HUD->SelectWeapon(TEXT("lightstick"));Stage=1;Age=0;return;}
    if(Stage==1){if(Age<1.5)return;Capture(TEXT("01_menu"));Stage=2;Age=0;return;}
    if(Stage==2)
    {
        if(Age<.2f)return;HUD->StartSelectedRun();Director->SetActorTickEnabled(false);Player->SetCanBeDamaged(false);
        Check(TEXT("menu_equips_lightstick"),Player->GetWeaponId()==TEXT("lightstick"));
        Check(TEXT("hands_and_weapon_ready"),Player->GetPresentation()&&Player->GetPresentation()->IsReady());
        PC->SetControlRotation(FRotator::ZeroRotator);
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Target=GetWorld()->SpawnActor<AHeliosEnemyActor>(Player->GetActorLocation()+FVector(350,0,0),FRotator(0,180,0),Params);
        Target->CurrentHP=10000;Target->StopShooting();Target->GetCharacterMovement()->DisableMovement();Target->SetActorTickEnabled(false);
        if(auto* AI=Cast<AAIController>(Target->GetController())){if(AI->GetBrainComponent())AI->GetBrainComponent()->StopLogic(TEXT("Weapon demonstration"));AI->StopMovement();}
        for(const FVector& Offset:{FVector(800,-450,25),FVector(1100,450,25),FVector(2300,0,25)})
        {
            auto* E=GetWorld()->SpawnActor<AHeliosEnemyActor>(Player->GetActorLocation()+Offset,FRotator(0,180,0),Params);
            E->CurrentHP=10000;E->StopShooting();E->GetCharacterMovement()->DisableMovement();E->SetActorTickEnabled(false);
            if(auto* AI=Cast<AAIController>(E->GetController())){if(AI->GetBrainComponent())AI->GetBrainComponent()->StopLogic(TEXT("Wave demonstration"));AI->StopMovement();}FarTargets.Add(E);
        }
        Stage=3;Age=0;return;
    }
    if(Stage==3)
    {
        if(Age<2.f)return;
        {
            const FVector Lane=Player->GetActorLocation()+FVector(0,0,3000);FActorSpawnParameters Params;Params.Owner=Weapon;Params.Instigator=Player;
            auto* Trigger=GetWorld()->SpawnActor<AStaticMeshActor>(Lane+FVector(100,0,-108),FRotator::ZeroRotator,Params);
            auto* C=Trigger->GetStaticMeshComponent();C->SetMobility(EComponentMobility::Movable);C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));C->SetCollisionEnabled(ECollisionEnabled::QueryOnly);C->SetCollisionObjectType(ECC_WorldDynamic);C->SetCollisionResponseToAllChannels(ECR_Ignore);
            auto* TestWave=GetWorld()->SpawnActor<AHeliosLightstickWave>(Lane,FRotator::ZeroRotator,Params);TestWave->Initialize(Weapon->Settings,0,Lane);TestWave->Tick(.1f);
            Check(TEXT("trigger_volume_does_not_block_wave"),TestWave->IsFlying()&&TestWave->GetDistanceTravelled()>170);TestWave->Destroy();Trigger->Destroy();
        }
        if(!FParse::Param(FCommandLine::Get(),TEXT("HeliosLightstickCaptureOnly")))RunFocusedChecks();
        else {Weapon->Settings->WaveMaterial.LoadSynchronous();if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();}
        Capture(TEXT("02_idle"));Stage=4;Age=0;Before=Target->CurrentHP;WaveSerialBefore=Weapon?Weapon->GetWavesReleased():0;return;
    }
    if(Stage>=4&&Stage<90)
    {
        Capture(FString::Printf(TEXT("frames/frame_%05d"),Frame++));
        if(Stage==4&&Age>=1){Player->StartFireFromAutomation();Stage=5;}
        if(Stage==5&&Age>=2.4f){Player->StopFireFromAutomation();Stage=6;}
        if(Stage==6&&Age>=3)
        {
            bool ReadableCut=false;
            for(int32 I=0;I<FMath::Min(24,Weapon->SlashMarks.Num());++I)
                if(Weapon->SlashSurfaces[I].Get()==Target)ReadableCut|=FVector::DotProduct(Weapon->SlashNormals[I],(Player->FirstPersonCamera->GetComponentLocation()-Weapon->SlashMarks[I]->GetComponentLocation()).GetSafeNormal())>.995f;
            Check(TEXT("visual_enemy_cut_faces_viewer_at_grazing_contact"),ReadableCut);
            Check(TEXT("live_melee_damage"),Before-Target->CurrentHP>0);
            Check(TEXT("aim_returns_without_accumulation"),FMath::Abs(PC->GetControlRotation().Pitch)<.01f);
            Check(TEXT("unlimited_no_reload"),Player->GetAmmo()==1&&!Player->IsReloading());
            Before=FarTargets[0]->CurrentHP;Target->SetActorLocation(Player->GetActorLocation()+FVector(130,240,0));Player->StartSecondaryAttack();Stage=7;
        }
        if(Stage==7&&Age>=4.2f){Check(TEXT("full_charge"),Weapon&&Weapon->GetCharge()>=.99f);Player->StopSecondaryAttack();Stage=8;}
        if(Stage==8&&Age>=5.6f)
        {
            Check(TEXT("live_heavy_damage_360"),FMath::IsNearlyEqual(Before-FarTargets[0]->CurrentHP,360.f,.1f));
            Check(TEXT("one_wave_per_release"),Weapon&&Weapon->GetWavesReleased()==WaveSerialBefore+1);
            Wall=GetWorld()->SpawnActor<AStaticMeshActor>(Player->GetActorLocation()+FVector(240,0,40),FRotator::ZeroRotator);
            Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
            Wall->SetActorScale3D(FVector(.12f,2,3));Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
            Target->SetActorLocation(Player->GetActorLocation()+FVector(350,0,0));Before=Target->CurrentHP;Player->StartFireFromAutomation();Player->StopFireFromAutomation();Stage=9;
        }
        if(Stage==9&&Age>=6.8f)
        {
            Check(TEXT("wall_blocks_damage"),Target->CurrentHP==Before);Wall->Destroy();Wall=nullptr;
            Player->StartSecondaryAttack();Stage=10;
        }
        if(Stage==10&&Age>=6.9f)
        {
            ChargeBefore=Weapon?Weapon->GetCharge():0;
            FActorSpawnParameters Params;Params.Owner=Weapon;Params.Instigator=Player;
            FVector Lane=Player->FirstPersonCamera->GetComponentLocation()+FVector(0,0,3000);
            PausedWave=GetWorld()->SpawnActor<AHeliosLightstickWave>(Lane,FRotator::ZeroRotator,Params);PausedWave->Initialize(Weapon->Settings,1,Lane);Weapon->OwnedWaves.Add(PausedWave);Before=PausedWave->GetDistanceTravelled();WaveSerialBefore=Weapon->GetWavesReleased();
            FHitResult Cut(Target,nullptr,Player->GetActorLocation()+FVector(220,0,60),-FVector::ForwardVector);Cut.ImpactPoint=Player->GetActorLocation()+FVector(220,0,60);Cut.ImpactNormal=-FVector::ForwardVector;
            Weapon->PlaySlashImpact(Cut,FVector::RightVector,true);PausedSlashSlot=24+(Weapon->NextWallSlash-1)%8;
            HUD->TogglePauseMenu();PauseStarted=0;Stage=90;return;
        }
        if(Stage==11&&Age>=8.0f){Check(TEXT("resume_does_not_release_attack"),Weapon&&!Weapon->IsAttacking()&&!Weapon->IsCharging()&&Weapon->GetWavesReleased()==WaveSerialBefore);Player->StartFireFromAutomation();Stage=12;}
        if(Stage==12&&Age>=10.8f){Player->StopFireFromAutomation();Stage=13;}
        if(Stage==13&&Age>=11.6f){Target->SetActorLocation(Player->GetActorLocation()+FVector(130,240,0));Player->StartSecondaryAttack();Stage=14;}
        if(Stage==14&&Age>=13.f){Player->StopSecondaryAttack();Stage=15;}
        if(Stage==15&&Age>=14.4f){Target->SetActorLocation(Player->GetActorLocation()+FVector(350,0,0));Player->StartFireFromAutomation();Stage=16;}
        if(Stage==16&&Age>=16.9f){Player->StopFireFromAutomation();Stage=17;}
        if(Stage==17&&Age>=17.6f){Target->SetActorLocation(Player->GetActorLocation()+FVector(130,240,0));Player->StartSecondaryAttack();Stage=18;}
        if(Stage==18&&Age>=18.8f){Player->StopSecondaryAttack();Stage=19;}
        if(Frame>=600){Stage=91;Age=0;}
        return;
    }
    if(Stage==90)
    {
        // Begin sampling after the already scheduled tick of the pause-request frame.
        if(PauseStarted==0){Before=PausedWave->GetDistanceTravelled();PausedSlashAge=Weapon->SlashAges[PausedSlashSlot];PauseStarted=FPlatformTime::Seconds();return;}
        if(FPlatformTime::Seconds()-PauseStarted<.35)return;
        Check(TEXT("pause_freezes_charge"),Weapon&&FMath::IsNearlyEqual(Weapon->GetCharge(),ChargeBefore,.001f));
        Check(TEXT("pause_freezes_flying_wave"),PausedWave&&PausedWave->GetDistanceTravelled()==Before);
        Check(TEXT("pause_freezes_persistent_slash"),Weapon->SlashAges[PausedSlashSlot]==PausedSlashAge&&Weapon->SlashMarks[PausedSlashSlot]->IsVisible());
        HUD->ContinueRun();Stage=11;Age=7.1f;return;
    }
    if(Stage==91)
    {
        Player->CancelCombatAndControl();Weapon->ReleasedCharge=1;Weapon->bWaveReleased=false;Weapon->ReleaseAim=FRotator::ZeroRotator;Weapon->ReleaseWave();
        FHitResult Cut(Target,nullptr,Target->GetActorLocation(),-FVector::ForwardVector);Cut.ImpactPoint=Target->GetActorLocation();Cut.ImpactNormal=-FVector::ForwardVector;Weapon->PlaySlashImpact(Cut,FVector::RightVector,true);
        TWeakObjectPtr<UStaticMeshComponent> OldMark=Weapon->SlashMarks[24+(Weapon->NextWallSlash-1)%8];
        TWeakObjectPtr<AHeliosLightstickWave> OldWave=Weapon->OwnedWaves.Last();Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("rifle")));
        Check(TEXT("switch_cleans_old_weapon_and_wave"),Player->GetWeaponId()==TEXT("rifle")&&!OldWave.IsValid());
        Check(TEXT("switch_cleans_persistent_slash"),!OldMark.IsValid()||!OldMark->IsVisible());
        Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("lightstick")));Player->StartSecondaryAttack();
        Weapon=Cast<AHeliosLightstickWeapon>(Player->GetEquippedWeapon());Weapon->bWaveReleased=false;Weapon->ReleaseWave();Weapon->PlaySlashImpact(Cut,FVector::RightVector,true);
        Player->SetCanBeDamaged(true);UGameplayStatics::ApplyDamage(Player,10000,nullptr,this,UDamageType::StaticClass());Stage=92;Age=0;return;
    }
    if(Stage==92)
    {
        bool AllClear=true;for(const auto& Mark:Weapon->SlashMarks)AllClear&=!Mark->IsVisible();Check(TEXT("death_clears_persistent_slashes"),AllClear);
        Check(TEXT("death_cancels_charge_and_waves"),Player->IsDead()&&Weapon&&!Weapon->IsCharging()&&!Weapon->IsAttacking()&&Weapon->GetOwnedWaveCount()==0);Stage=98;
    }
    if(Stage==98)
    {
        const FString Json=FString::Printf(TEXT("{\"status\":\"%s\",\"frames\":%d,\"checks\":[%s]}"),Failures?TEXT("failed"):TEXT("passed"),Frame,*Results);
        const TCHAR* Report=FParse::Param(FCommandLine::Get(),TEXT("HeliosLightstickCaptureOnly"))?TEXT("../qa/lightstick/capture_report.json"):TEXT("../qa/lightstick/runtime_report.json");
        FFileHelper::SaveStringToFile(Json,*(FPaths::ProjectDir()/Report),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        Stage=99;FPlatformMisc::RequestExit(false);
    }
}
