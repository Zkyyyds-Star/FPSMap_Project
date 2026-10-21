#include "QA/HeliosPetDemo.h"
#include "Pets/HeliosPetDog.h"
#include "Pets/HeliosPetVFXComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Town/HeliosTownSubsystem.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "Adventure/HeliosAdventureCharacter.h"
#include "UI/HeliosAdventureHUD.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Variant_Shooter/AI/ShooterAIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/DamageType.h"
#include "Components/SkeletalMeshComponent.h"
#include "BrainComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

AHeliosPetDemo::AHeliosPetDemo(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void AHeliosPetDemo::SetCameraView(AHeliosPetCompanion* Pet,FVector Offset,FVector Focus,float Fov)
{
    const FVector Look=Pet->GetActorLocation()+Focus;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(PetDemoCamera),false,Pet);Q.AddIgnoredActor(Enemy);Q.AddIgnoredActor(UGameplayStatics::GetPlayerPawn(this,0));
    FVector Best=Look+Offset;float BestFraction=-1;
    for(float Angle:{0.f,45.f,-45.f,90.f,-90.f,135.f,-135.f,180.f})
    {
        const FVector Candidate=Pet->GetActorLocation()+Offset.RotateAngleAxis(Angle,FVector::UpVector);
        FHitResult Hit;float Fraction=GetWorld()->LineTraceSingleByChannel(Hit,Look,Candidate,ECC_Visibility,Q)?Hit.Time:1.f;
        FHitResult BodyHit;if(GetWorld()->LineTraceSingleByChannel(BodyHit,Pet->GetActorLocation(),Candidate,ECC_Visibility,Q))Fraction=FMath::Min(Fraction,BodyHit.Time);
        if(Fraction>BestFraction){BestFraction=Fraction;Best=FMath::Lerp(Look,Candidate,FMath::Max(.1f,Fraction-.015f));}
        if(Fraction>=1)break;
    }
    Camera->SetActorLocation(Best);Camera->SetActorRotation((Look-Best).Rotation());Camera->GetCameraComponent()->SetFieldOfView(Fov);
}
void AHeliosPetDemo::CheckMelee(AHeliosPetCompanion* Pet,int32 Phase)
{
    const FVector Origin=Pet->GetActorLocation();Pet->SetActorRotation(FRotator::ZeroRotator);Pet->Target=Enemy;
    FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    if(Phase==0)
    {
        const float HP=Enemy->CurrentHP;const int32 Bursts=Pet->Effects->GetHitBursts();
        Pet->bHitCommitted=false;Pet->AttackHit();Pet->AttackHit();
        Check(TEXT("490cm_hits_once_for_25"),FMath::IsNearlyEqual(HP-Enemy->CurrentHP,25.f)&&Pet->Effects->GetHitBursts()==Bursts+1);
        MeleeHP=Enemy->CurrentHP;MeleeBursts=Pet->Effects->GetHitBursts();
        Enemy->SetActorLocation(Origin+FVector(510,0,48));Pet->bHitCommitted=false;Pet->AttackHit();
        Check(TEXT("510cm_misses_without_hit_flash"),Enemy->CurrentHP==MeleeHP&&Pet->Effects->GetHitBursts()==MeleeBursts);
        Enemy->SetActorLocation(Origin+FVector(-300,0,48));Pet->bHitCommitted=false;Pet->AttackHit();
        Check(TEXT("behind_pet_misses"),Enemy->CurrentHP==MeleeHP);
        Enemy->SetActorLocation(Origin+FVector(490,0,48));
        auto* Wall=GetWorld()->SpawnActor<AStaticMeshActor>(Origin+FVector(240,0,45),FRotator::ZeroRotator,P);MeleeWall=Wall;
        auto* M=Wall->GetStaticMeshComponent();M->SetMobility(EComponentMobility::Movable);M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Wall->SetActorScale3D(FVector(.12f,3,3));M->SetCollisionProfileName(TEXT("BlockAll"));M->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Wall->SetActorEnableCollision(true);
        return;
    }
    if(Phase==1)
    {
        FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(PetTestWall),false,Pet);
        const bool WallInQuery=GetWorld()->LineTraceSingleByChannel(Hit,Origin+FVector(0,0,25),Enemy->GetActorLocation(),ECC_Visibility,Q)&&Hit.GetActor()==MeleeWall;
        UE_LOG(LogTemp,Display,TEXT("PET_TEST_WALL ready=%d actor=%s wall=%s"),WallInQuery,*GetNameSafe(Hit.GetActor()),*MeleeWall->GetActorLocation().ToString());
        Pet->RecentPlayerTarget.Reset();Pet->EngagedUntil.Empty();Pet->Target.Reset();
        if(auto* AI=Cast<AShooterAIController>(Enemy->GetController()))AI->ResetAwareness();
        Pet->RefreshDecision();
        Check(TEXT("unengaged_enemy_behind_wall_is_acquired"),WallInQuery&&Pet->Target.Get()==Enemy);
        Check(TEXT("nearby_occluded_enemy_keeps_pet_moving"),Pet->bMoveRequested);
        if(auto* AI=Cast<AAIController>(Pet->GetController()))AI->StopMovement();Pet->bMoveRequested=false;
        Pet->bHitCommitted=false;Pet->AttackHit();Check(TEXT("wall_blocks_melee_and_hit_flash"),WallInQuery&&Enemy->CurrentHP==MeleeHP&&Pet->Effects->GetHitBursts()==MeleeBursts);MeleeWall->Destroy();MeleeWall=nullptr;return;
    }
    auto* Other=GetWorld()->SpawnActor<AHeliosEnemyActor>(Origin+FVector(350,150,48),FRotator::ZeroRotator,P);Other->CurrentHP=1000;Other->StopShooting();Other->SetActorTickEnabled(false);
    Pet->bHitCommitted=false;Pet->AttackHit();Check(TEXT("only_current_target_takes_damage"),Other->CurrentHP==1000&&FMath::IsNearlyEqual(MeleeHP-Enemy->CurrentHP,25.f));Other->Destroy();
    Pet->Effects->Clear();Pet->Target.Reset();Pet->bHitCommitted=false;
}
void AHeliosPetDemo::CheckAcquisition(AHeliosPetCompanion* Pet)
{
    auto* Player=Pet->CompanionOwner.Get();
    const FVector PlayerPosition=Player->GetActorLocation();
    const FVector PetPosition=Pet->GetActorLocation();
    const FVector EnemyPosition=Enemy->GetActorLocation();
    TMap<AHeliosEnemyActor*,FVector> OtherPositions;
    for(TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It)
        if(*It!=Enemy){OtherPositions.Add(*It,It->GetActorLocation());It->SetActorLocation(It->GetActorLocation()+FVector(1000000,0,0));}
    Pet->RecentPlayerTarget.Reset();Pet->EngagedUntil.Empty();
    if(auto* AI=Cast<AShooterAIController>(Enemy->GetController()))AI->ResetAwareness();
    // Round only the fixture's known reachable ground to centimetre coordinates.
    // Subtracting exactly 5000 from a fractional world coordinate may round the
    // actual boundary just above 5000; do not alter the production radius/test.
    FVector RangeAnchor=EnemyPosition;
    RangeAnchor.X=FMath::RoundToDouble(RangeAnchor.X);RangeAnchor.Y=FMath::RoundToDouble(RangeAnchor.Y);
    FVector RangePetPosition=RangeAnchor-FVector(490,0,0);RangePetPosition.Z=PetPosition.Z;
    for(float Distance:{4900.f,5000.f,5100.f})
    {
        const FVector OriginalOwner=EnemyPosition-FVector(Distance,0,0);
        const double OriginalRangeSq=FVector::DistSquared2D(OriginalOwner,EnemyPosition);
        const double OriginalLeashSq=FVector::DistSquared2D(OriginalOwner,PetPosition);
        const double RadiusSq=FMath::Square(double(Pet->Settings.CombatRadius));
        UE_LOG(LogTemp,Display,TEXT("PET_TEST_ACQUISITION_ORIGINAL requested_cm=%.0f owner_enemy_cm=%.17g owner_enemy_sq=%.17g owner_dog_cm=%.17g outside_radius=%d outside_leash=%d"),
            Distance,FMath::Sqrt(OriginalRangeSq),OriginalRangeSq,FMath::Sqrt(OriginalLeashSq),OriginalRangeSq>RadiusSq,OriginalLeashSq>RadiusSq);
        if(auto* AI=Cast<AAIController>(Pet->GetController()))AI->StopMovement();
        Pet->GetCharacterMovement()->StopMovementImmediately();Pet->bMoveRequested=false;
        Enemy->SetActorLocation(RangeAnchor);Pet->SetActorLocation(RangePetPosition);
        Player->SetActorLocation(RangeAnchor-FVector(Distance,0,0));
        Pet->RecentPlayerTarget.Reset();Pet->EngagedUntil.Empty();Pet->Target.Reset();
        const double ActualRangeSq=FVector::DistSquared2D(Player->GetActorLocation(),Enemy->GetActorLocation());
        const double ActualLeashSq=FVector::DistSquared2D(Player->GetActorLocation(),Pet->GetActorLocation());
        const bool Reachable=Pet->CanReach(Enemy->GetActorLocation());
        Pet->RefreshDecision();
        const bool FixtureValid=ActualRangeSq==FMath::Square(double(Distance))&&ActualLeashSq<=RadiusSq&&Reachable;
        UE_LOG(LogTemp,Display,TEXT("PET_TEST_ACQUISITION requested_cm=%.0f owner_enemy_cm=%.17g owner_enemy_sq=%.17g owner_dog_cm=%.17g radius_cm=%.17g reachable=%d fixture_valid=%d acquired=%d owner=%s dog=%s enemy=%s"),
            Distance,FMath::Sqrt(ActualRangeSq),ActualRangeSq,FMath::Sqrt(ActualLeashSq),double(Pet->Settings.CombatRadius),Reachable,FixtureValid,Pet->Target.Get()==Enemy,
            *Player->GetActorLocation().ToString(),*Pet->GetActorLocation().ToString(),*Enemy->GetActorLocation().ToString());
        Check(FString::Printf(TEXT("unengaged_enemy_%.0fm_%s"),Distance/100.f,Distance<=5000?TEXT("acquired"):TEXT("rejected")),
            FixtureValid&&(Distance<=5000?Pet->Target.Get()==Enemy:!Pet->Target.IsValid()));
    }
    Player->SetActorLocation(PlayerPosition);Pet->SetActorLocation(PetPosition);Enemy->SetActorLocation(EnemyPosition);
    Enemy->SetActorLocation(EnemyPosition+FVector(0,0,100000));Pet->RefreshDecision();
    Check(TEXT("unreachable_enemy_is_rejected"),!Pet->Target.IsValid());
    FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Reachable=GetWorld()->SpawnActor<AHeliosEnemyActor>(EnemyPosition,FRotator::ZeroRotator,P);
    Reachable->StopShooting();Reachable->SetActorTickEnabled(false);
    if(auto* AI=Cast<AShooterAIController>(Reachable->GetController())){AI->ResetAwareness();if(AI->GetBrainComponent())AI->GetBrainComponent()->StopLogic(TEXT("Acquisition test"));}
    Pet->RecentPlayerTarget=Enemy;Pet->RecentPlayerHitTime=GetWorld()->GetTimeSeconds();Pet->RefreshDecision();
    Check(TEXT("unreachable_priority_target_falls_back_to_reachable_enemy"),Pet->Target.Get()==Reachable);
    Reachable->Destroy();Enemy->SetActorLocation(EnemyPosition);
    Pet->RecentPlayerTarget.Reset();Pet->EngagedUntil.Empty();Pet->Target.Reset();
    for(const auto& Entry:OtherPositions)Entry.Key->SetActorLocation(Entry.Value);
    if(auto* AI=Cast<AAIController>(Pet->GetController()))AI->StopMovement();Pet->bMoveRequested=false;
}
void AHeliosPetDemo::Check(const FString& Name,bool Passed)
{
    if(!Passed)++Failures;if(!Results.IsEmpty())Results+=TEXT(",\n");
    Results+=FString::Printf(TEXT("{\"name\":\"%s\",\"passed\":%s}"),*Name,Passed?TEXT("true"):TEXT("false"));
    UE_LOG(LogTemp,Display,TEXT("PET_CHECK %s %s"),*Name,Passed?TEXT("PASS"):TEXT("FAIL"));
}
void AHeliosPetDemo::Capture(const FString& Name)
{
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosPetRecord")))return;
    FString Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/pet_v2")/GetWorld()->GetMapName()/(Name+TEXT(".png")));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);FScreenshotRequest::RequestScreenshot(Path,true,false);
}
void AHeliosPetDemo::Finish()
{
    FString Dir=FPaths::ProjectDir()/TEXT("../qa/pet_v2")/GetWorld()->GetMapName();IFileManager::Get().MakeDirectory(*Dir,true);
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosPetRecord")))
    {
        FString Frames;for(int32 I=0;I<FrameTimes.Num();++I){if(I)Frames+=TEXT(",");Frames+=FString::Printf(TEXT("{\"file\":\"frames/frame_%05d.png\",\"time\":%.6f}"),I,FrameTimes[I]);}
        FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"duration\":%.3f,\"frames\":[%s]}"),RecordAge,*Frames),*(Dir/TEXT("capture.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        Stage=99;FPlatformMisc::RequestExit(false);return;
    }
    FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"status\":\"%s\",\"checks\":[%s]}"),Failures?TEXT("failed"):TEXT("passed"),*Results),*(Dir/TEXT("report.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    Stage=99;FPlatformMisc::RequestExit(false);
}
void AHeliosPetDemo::Tick(float Dt)
{
    Super::Tick(Dt);if(Stage==99)return;
    if(!Started)Started=FPlatformTime::Seconds();
    if(FPlatformTime::Seconds()-Started>180){Check(TEXT("timeout"),false);Finish();return;}
    auto* Player=Cast<AHeliosAdventureCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));
    auto* Director=AHeliosAdventureDirector::Find(GetWorld());if(!Player||!Director)return;
    auto* PC=Cast<APlayerController>(Player->GetController());auto* HUD=PC?Cast<AHeliosAdventureHUD>(PC->GetHUD()):nullptr;if(!HUD)return;
    auto* Pets=GetWorld()->GetSubsystem<UHeliosPetWorld>();auto* Pet=Pets?Pets->Pet.Get():nullptr;
    const bool Record=FParse::Param(FCommandLine::Get(),TEXT("HeliosPetRecord"));
    if(Record&&Stage>=2&&Stage<8&&!UGameplayStatics::IsGamePaused(this))
    {
        RecordAge+=Dt;RecordStep+=Dt;
        if(RecordAge>=15.f){Finish();return;}
        if(RecordStep>=.1f)
        {
            RecordStep=0;const FString Dir=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/pet_v2")/GetWorld()->GetMapName()/TEXT("frames"));IFileManager::Get().MakeDirectory(*Dir,true);
            FScreenshotRequest::RequestScreenshot(Dir/FString::Printf(TEXT("frame_%05d.png"),RecordedFrames++),true,false);FrameTimes.Add(RecordAge);
        }
    }
    Age+=Dt;
    if(Stage==0)
    {
        if(auto* Town=Director->GetTownSubsystem())if(!Town->IsWorldReady())return;
        HUD->SelectWeapon(TEXT("cfm_chan_zeus"));HUD->StartSelectedRun();if(Director->IsSelectingWeapon())return;
        Director->SetActorTickEnabled(true);Player->SetCanBeDamaged(false);PC->SetControlRotation(FRotator::ZeroRotator);Stage=1;Age=0;return;
    }
    if(!Pet)return;
    if(Stage==1&&Age>5)
    {
        Director->SetActorTickEnabled(false);Check(TEXT("round_model_and_nine_animations"),Pet->bModelReady&&Pet->Poses.Num()==9&&Pet->GetMesh()->DoesSocketExist(TEXT("eyes_closed")));
        const FVector Face=Pet->GetMesh()->GetSocketLocation(TEXT("nose"))-Pet->GetMesh()->GetSocketLocation(TEXT("body"));
        UE_LOG(LogTemp,Display,TEXT("PET_FACE local=%s forward_dot=%.2f"),*Face.ToString(),FVector::DotProduct(Face.GetSafeNormal2D(),Pet->GetActorForwardVector()));
        Check(TEXT("model_faces_movement_direction"),FVector::DotProduct(Face.GetSafeNormal2D(),Pet->GetActorForwardVector())>.8f);
        Check(TEXT("double_follow_speed_and_acceleration"),Pet->Settings.MovementMultiplier==2.f&&Pet->Settings.MeleeRange==500.f&&Pet->GetCharacterMovement()->MaxWalkSpeed==1040.f&&Pet->GetCharacterMovement()->MaxAcceleration==2400.f);
        Check(TEXT("configured_25m_follow_50m_catchup_50m_combat_30s"),Pet->Settings.FollowDistance==2500&&Pet->Settings.CatchUpDistance==5000&&Pet->Settings.CombatRadius==5000&&Pet->Settings.SleepSeconds==30);
        FVector Delta=Pet->GetActorLocation()-Player->GetActorLocation();
        UE_LOG(LogTemp,Display,TEXT("PET_DEMO follow offset=%s distance=%.2f"),*Delta.ToString(),Delta.Size2D());
        Check(TEXT("safe_rear_follow_position"),Delta.Size2D()<=3100&&FVector::DotProduct(Delta.GetSafeNormal2D(),Player->GetActorForwardVector())<0);
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Camera=GetWorld()->SpawnActor<ACameraActor>(Pet->GetActorLocation()+FVector(350,-380,180),FRotator::ZeroRotator,P);
        Camera->SetActorRotation((Pet->GetActorLocation()+FVector(0,0,10)-Camera->GetActorLocation()).Rotation());Camera->GetCameraComponent()->SetFieldOfView(60);PC->SetViewTarget(Camera);
        Stage=2;Age=0;return;
    }
    if(Stage==2&&Age>.7f&&!bIdleCaptured){Capture(TEXT("01_follow"));bIdleCaptured=true;}
    if(Stage==2&&Age>1.3f)
    {
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Enemy=GetWorld()->SpawnActor<AHeliosEnemyActor>(Pet->GetActorLocation()+FVector(490,0,48),FRotator(0,180,0),P);
        Enemy->CurrentHP=10000;Enemy->StopShooting();Enemy->GetCharacterMovement()->DisableMovement();Enemy->SetActorTickEnabled(false);
        if(auto* AI=Cast<AAIController>(Enemy->GetController())){if(AI->GetBrainComponent())AI->GetBrainComponent()->StopLogic(TEXT("Pet demo"));AI->StopMovement();}
        if(!Record){Pet->SetActorTickEnabled(false);CheckAcquisition(Pet);CheckMelee(Pet,0);Stage=10;Age=0;return;}
        Stage=11;Age=0;return;
    }
    if(Stage==10&&Age>.2f){CheckMelee(Pet,1);Stage=11;Age=0;return;}
    if(Stage==11&&Age>.2f)
    {
        if(!Record)
        {
            CheckMelee(Pet,2);
            Pet->RecentPlayerTarget.Reset();Pet->EngagedUntil.Empty();
            if(auto* AI=Cast<AShooterAIController>(Enemy->GetController()))AI->ResetAwareness();
            FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Wall=GetWorld()->SpawnActor<AStaticMeshActor>(Pet->GetActorLocation()+FVector(240,0,45),FRotator::ZeroRotator,P);MeleeWall=Wall;
            auto* Mesh=Wall->GetStaticMeshComponent();Mesh->SetMobility(EComponentMobility::Movable);Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
            Wall->SetActorScale3D(FVector(.12f,3,3));Mesh->SetCollisionProfileName(TEXT("BlockAll"));Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            Before=Enemy->CurrentHP;Stage=12;Age=0;return;
        }
        SetCameraView(Pet,FVector(600,-820,360),FVector(200,0,20),60);
        UGameplayStatics::ApplyDamage(Enemy,1,PC,Player,UDamageType::StaticClass());Before=Enemy->CurrentHP;Stage=3;Age=0;return;
    }
    if(Stage==12&&Age>1.f)
    {
        Check(TEXT("detour_starts_with_blocked_sight"),!Pet->HasClearLine(Enemy));
        Pet->SetActorTickEnabled(true);Pet->DecisionRemaining=0;Stage=13;Age=0;return;
    }
    if(Stage==13)
    {
        if(Enemy->CurrentHP>=Before&&Age<12.f)return;
        Check(TEXT("pet_routes_around_wall_and_hits_without_player_attack"),Enemy->CurrentHP<Before&&Pet->HasClearLine(Enemy));
        MeleeWall->Destroy();MeleeWall=nullptr;
        SetCameraView(Pet,FVector(600,-820,360),FVector(200,0,20),60);
        MovingEnemyDirection=(Player->GetActorLocation()-Enemy->GetActorLocation()).GetSafeNormal2D();
        MovingStartHits=Pet->HitsDelivered;bMovedDuringSwing=false;
        Stage=14;Age=0;return;
    }
    if(Stage==14)
    {
        // Move the enemy away, then diagonally and back across the pet's heading.
        // Sweeping keeps this fixture inside the same physical world as normal AI.
        const float Angle=Age<2.f?0.f:Age<4.f?65.f:-65.f;
        const FVector Step=MovingEnemyDirection.RotateAngleAxis(Angle,FVector::UpVector)*400.f*Dt;
        Enemy->SetActorLocation(Enemy->GetActorLocation()+Step,true);
        if(Pet->AttackAge>=0.f&&Pet->GetVelocity().Size2D()>100.f)bMovedDuringSwing=true;
        if(Age<6.f)return;
        const int32 Hits=Pet->HitsDelivered-MovingStartHits;
        Check(TEXT("pet_moves_while_swinging_at_moving_enemy"),bMovedDuringSwing);
        Check(TEXT("moving_enemy_is_hit_repeatedly"),Hits>=3);
        Check(TEXT("moving_attacks_preserve_cooldown"),Hits<=FMath::CeilToInt(Age/Pet->Settings.AttackInterval)+1);
        Before=Enemy->CurrentHP;Stage=3;Age=0;return;
    }
    if(Stage==3)
    {
        if(!Record&&Age>2.3f&&!bFarCombatCaptured)
        {
            SetCameraView(Pet,FVector(1750,-1750,350),FVector(200,0,20),70);
            if(Pet->AttackAge>.3f&&Pet->AttackAge<.41f){Capture(TEXT("05_combat_25m"));bFarCombatCaptured=true;}
        }
        if(!Record&&bFarCombatCaptured&&Age>3.6f)
        {SetCameraView(Pet,FVector(600,-820,360),FVector(200,0,20),60);}
        if(Pet->AttackAge>.30f&&Pet->AttackAge<.39f&&Age>1&&Age<2)Capture(TEXT("02_combat"));
        if(Age<(Record?7.f:4.f) || (!Pet->IsGuarding() && Age<(Record?9.f:6.f)))return;
        Check(TEXT("live_enemy_damage"),Pet->HitsDelivered>0&&Enemy->CurrentHP<Before);
        Check(TEXT("double_combat_speed"),Pet->GetCharacterMovement()->MaxWalkSpeed==1300.f);
        Check(TEXT("enemy_retaliates_against_pet"),Cast<AShooterAIController>(Enemy->GetController())&&Cast<AShooterAIController>(Enemy->GetController())->GetCurrentTarget()==Pet);
        UE_LOG(LogTemp,Display,TEXT("PET_DEMO hits=%d damage=%.1f target=%s"),Pet->HitsDelivered,Before-Enemy->CurrentHP,*GetNameSafe(Pet->Target.Get()));
        UGameplayStatics::ApplyDamage(Pet,100,PC,Player,UDamageType::StaticClass());Check(TEXT("immune_to_owner"),Pet->Health==Pet->Settings.MaxHealth);
        const float Shielded=UGameplayStatics::ApplyDamage(Pet,20,Enemy->GetController(),Enemy,UDamageType::StaticClass());
        Check(TEXT("front_shield_reduces_direct_damage_60_percent"),FMath::IsNearlyEqual(Shielded,8.f,.01f));
        Enemy->SetActorLocation(Player->GetActorLocation()+FVector(5100,0,0));Pet->RefreshDecision();
        Check(TEXT("enemy_beyond_50m_is_rejected"),Pet->Target.Get()!=Enemy);
        Enemy->Destroy();Enemy=nullptr;Pet->RefreshDecision();
        Check(TEXT("destroyed_target_is_cleared"),!Pet->Target.IsValid());
        SetCameraView(Pet,FVector(300,-370,165),FVector(0,0,35),55);
        Stage=4;Age=0;return;
    }
    if(Stage==4&&Age>1)
    {
        const FVector Original=Pet->GetActorLocation();Pet->SetActorLocation(Player->GetActorLocation()-FVector(5500,0,0));Pet->RefreshDecision();
        Check(TEXT("over_50m_switches_to_fast_catchup"),Pet->State==EHeliosPetState::CatchingUp&&Pet->GetCharacterMovement()->MaxWalkSpeed==2000);
        Pet->SetActorLocation(Original);Pet->RefreshDecision();
        UGameplayStatics::ApplyDamage(Pet,1000,nullptr,this,UDamageType::StaticClass());
        Check(TEXT("down_enters_30_second_sleep"),Pet->IsSleeping()&&Pet->SleepRemaining>=29.9f);SleptAt=GetWorld()->GetTimeSeconds();
        Stage=5;Age=0;return;
    }
    if(Stage==5&&Age>1)
    {
        Check(TEXT("sleep_pose_and_world_zzz"),Pet->CurrentPose==TEXT("Sleep")&&Pet->Effects->HasSleepingLetters()&&Pet->Effects->GetLiveParticleCount()==0);
        Capture(TEXT("03_sleep"));if(Record){Stage=7;return;}Before=Pet->SleepRemaining;UGameplayStatics::SetGamePaused(this,true);PausedAt=0;Stage=6;return;
    }
    if(Stage==6)
    {
        if(PausedAt==0){Before=Pet->SleepRemaining;PausedAt=FPlatformTime::Seconds();return;}
        if(FPlatformTime::Seconds()-PausedAt<1)return;
        Check(TEXT("pause_freezes_sleep"),FMath::IsNearlyEqual(Before,Pet->SleepRemaining,.001f));
        UGameplayStatics::SetGamePaused(this,false);Stage=7;return;
    }
    if(Stage==7&&!Pet->IsSleeping())
    {
        Check(TEXT("full_hp_wake_after_30_game_seconds"),Pet->Health==Pet->Settings.MaxHealth&&GetWorld()->GetTimeSeconds()-SleptAt>=29.9f);
        Stage=8;Age=0;return;
    }
    if(Stage==7&&!Record&&Pet->IsSleeping())
    {
        if(Pet->SleepRemaining<26.5f&&!bFarSleepCaptured)
        {
            SetCameraView(Pet,FVector(1750,-1750,350),FVector(0,0,50),70);
            if(Pet->SleepRemaining<26){Capture(TEXT("06_sleep_25m"));bFarSleepCaptured=true;}
        }
        if(Pet->SleepRemaining<25&&bFarSleepCaptured)
        {SetCameraView(Pet,FVector(300,-370,165),FVector(0,0,35),55);}
    }
    if(Stage==8&&Age>1)
    {
        Capture(TEXT("04_awake"));OldPet=Pet;Director->SetActorTickEnabled(true);Director->RestartRun();HUD->SelectWeapon(TEXT("cfm_chan_zeus"));HUD->StartSelectedRun();Stage=9;Age=0;return;
    }
    if(Stage==9&&Age>2)
    {
        int Count=0;for(TActorIterator<AHeliosPetCompanion> It(GetWorld());It;++It)++Count;
        Check(TEXT("restart_has_one_healthy_pet"),Count==1&&Pet!=OldPet.Get()&&Pet->Health==Pet->Settings.MaxHealth&&!Pet->IsSleeping());Finish();
    }
}
