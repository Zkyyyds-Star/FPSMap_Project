#include "QA/HeliosMinionCombatQA.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Enemies/HeliosEnemyTestDirector.h"
#include "Player/HeliosArenaCharacter.h"
#include "Pets/HeliosPetDog.h"
#include "Pets/HeliosUltimatumPet.h"
#include "Weapons/HeliosSoulBanner.h"
#include "Weapons/HeliosBasketballProjectile.h"
#include "World/HeliosNavBounds.h"
#include "Variant_Shooter/AI/ShooterAIController.h"
#include "BrainComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
constexpr int32 ScenarioCount = 7;
constexpr int32 SeedCount = 3;
const TCHAR* Scenarios[] = { TEXT("warrior"), TEXT("mage"), TEXT("sword_pet"), TEXT("rifle_pet"),
    TEXT("mixed"), TEXT("corner"), TEXT("corridor") };
FActorSpawnParameters SpawnParams(AActor* Owner = nullptr)
{
    FActorSpawnParameters P;
    P.Owner = Owner;
    P.Instigator = Cast<APawn>(Owner);
    P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    return P;
}
}

AHeliosMinionCombatQA::AHeliosMinionCombatQA() { PrimaryActorTick.bCanEverTick = true; }

AActor* AHeliosMinionCombatQA::Box(const FVector& Center, const FVector& Extent)
{
    AActor* A = GetWorld()->SpawnActor<AActor>();
    auto* C = NewObject<UStaticMeshComponent>(A);
    A->AddInstanceComponent(C); A->SetRootComponent(C);
    C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    C->SetCollisionProfileName(TEXT("BlockAll"));
    C->SetCanEverAffectNavigation(true); C->RegisterComponent();
    A->SetActorLocation(Center); A->SetActorScale3D(Extent / 50.f);
    return A;
}

void AHeliosMinionCombatQA::PrepareArena()
{
    for (TActorIterator<AHeliosEnemyTestDirector> It(GetWorld()); It; ++It) It->Destroy();
    for (TActorIterator<AHeliosEnemyActor> It(GetWorld()); It; ++It) It->Destroy();
    Box(Origin - FVector(0,0,50), FVector(3000,2200,50));
    auto* Bounds = GetWorld()->SpawnActor<AHeliosNavBounds>(Origin, FRotator::ZeroRotator);
    Bounds->NavigationBounds->SetBoxExtent(FVector(3100,2300,500));
    Bounds->RebuildNavigation();
    Player->GetCharacterMovement()->DisableMovement();
    Player->SetActorLocation(Origin + FVector(0,1800,100));
    Player->Tags.Remove(TEXT("Player")); // Passive owner, never a combat candidate.
    Player->SetActorEnableCollision(false); Player->SetCanBeDamaged(false);
    bPrepared = true;
}

void AHeliosMinionCombatQA::ClearCase()
{
    for (TActorIterator<AHeliosBasketballProjectile> It(GetWorld()); It; ++It) It->Destroy();
    for (TActorIterator<AHeliosSoulBolt> It(GetWorld()); It; ++It) It->Destroy();
    for (auto A : Actors)
    {
        if (!A.IsValid()) continue;
        if (APawn* P = Cast<APawn>(A.Get()))
            if (auto* AI = Cast<AAIController>(P->GetController()))
            {
                if (AI->GetBrainComponent()) AI->GetBrainComponent()->StopLogic(TEXT("Minion QA cleanup"));
                AI->UnPossess(); AI->Destroy();
            }
        A->Destroy();
    }
    Actors.Reset(); Friendlies.Reset(); Enemy.Reset();
}

void AHeliosMinionCombatQA::BeginCase()
{
    ClearCase();
    const int32 Type = (CaseIndex / ScenarioCount) % 4;
    const int32 Scenario = CaseIndex % ScenarioCount;
    const int32 Seed = 230923 + CaseIndex / (4 * ScenarioCount);
    FMath::RandInit(Seed); FMath::SRandInit(Seed);
    UClass* Types[] = { AHeliosEnemyE01::StaticClass(), AHeliosEnemyE02::StaticClass(),
        AHeliosEnemyE03::StaticClass(), AHeliosEnemyE04::StaticClass() };
    const auto Definition = FHeliosEnemyDefinition::ForType(static_cast<EHeliosEnemyType>(Type+1));
    Enemy = GetWorld()->SpawnActor<AHeliosEnemyActor>(Types[Type],
        Origin + FVector(-650,0,Definition.CapsuleHalfHeight+2), FRotator::ZeroRotator, SpawnParams());
    Actors.Add(Enemy.Get());
    if (Scenario == 5)
    {
        Actors.Add(Box(Origin+FVector(0,220,200),FVector(30,330,200)));
        Actors.Add(Box(Origin+FVector(-270,520,200),FVector(300,30,200)));
    }
    if (Scenario == 6)
        for (float Side : {-1.f, 1.f}) Actors.Add(Box(Origin+FVector(0,Side*210,200),FVector(1600,30,200)));

    auto AddPet = [&](bool Rifle, float Y)
    {
        auto* P = GetWorld()->SpawnActor<AHeliosPetCompanion>(Rifle ? AHeliosUltimatumPet::StaticClass() : AHeliosPetDog::StaticClass(),
            Origin+FVector(650,Y,110),FRotator(0,180,0),SpawnParams(Player.Get()));
        P->CompanionOwner=Player; Actors.Add(P); Friendlies.Add(P);
    };
    auto AddSkeleton = [&](int32 SoldierRole, float Y)
    {
        auto* Flag=GetWorld()->SpawnActor<AHeliosSoulFlag>(Origin+FVector(650,Y,0),FRotator(0,180,0),SpawnParams(Player.Get()));
        Flag->Army=UHeliosSoulArmy::Get(Player.Get());
        auto* S=GetWorld()->SpawnActor<AHeliosSoulSkeleton>(Origin+FVector(650,Y,86),FRotator(0,180,0),SpawnParams(Player.Get()));
        S->Initialize(Flag,SoldierRole); Flag->Soldiers.Add(S);
        Actors.Add(S); Actors.Add(Flag); Friendlies.Add(S);
    };
    if (Scenario==0) AddSkeleton(0,0);
    else if (Scenario==1) AddSkeleton(2,0);
    else if (Scenario==2) AddPet(false,0);
    else if (Scenario==3) AddPet(true,0);
    else { AddSkeleton(0,-80); AddSkeleton(2,80); AddPet(Scenario==4,0); }
    CaseTime=0; FirstAttack=FirstAcquire=-1; Committed=Attempts=Acquisitions=0;
    HitSeconds=DamageDealt=0; LastTarget.Reset();
    InitialFriendlyHP=LastFriendlyHP=FriendlyHealth(); EnemyHP=Enemy->GetHealth();
    UE_LOG(LogTemp,Display,TEXT("MINION_QA begin case=%d E%02d scenario=%s seed=%d hp=%.1f"),CaseIndex,Type+1,Scenarios[Scenario],Seed,EnemyHP);
}

float AHeliosMinionCombatQA::FriendlyHealth() const
{
    float HP=0;
    for (auto A : Friendlies)
    {
        if (auto* S=Cast<AHeliosSoulSkeleton>(A.Get())) HP+=FMath::Max(0.f,S->Health);
        if (auto* P=Cast<AHeliosPetCompanion>(A.Get())) HP+=FMath::Max(0.f,P->Health);
    }
    return HP;
}

void AHeliosMinionCombatQA::EndCase()
{
    auto R=MakeShared<FJsonObject>();
    R->SetNumberField(TEXT("enemy_type"),(CaseIndex/ScenarioCount)%4+1);
    R->SetStringField(TEXT("scenario"),Scenarios[CaseIndex%ScenarioCount]);
    R->SetNumberField(TEXT("seed"),230923+CaseIndex/(4*ScenarioCount));
    R->SetNumberField(TEXT("elapsed"),CaseTime);
    R->SetNumberField(TEXT("first_acquire_seconds"),FirstAcquire);
    R->SetNumberField(TEXT("first_attack_seconds"),FirstAttack);
    R->SetNumberField(TEXT("committed"),Committed);
    R->SetNumberField(TEXT("attack_cycles"),Attempts);
    R->SetNumberField(TEXT("target_changes"),Acquisitions);
    R->SetNumberField(TEXT("damage_dealt"),DamageDealt);
    R->SetNumberField(TEXT("enemy_hp"),EnemyHP);
    R->SetNumberField(TEXT("hit_state_seconds"),HitSeconds);
    Results.Add(MakeShared<FJsonValueObject>(R));
    UE_LOG(LogTemp,Display,TEXT("MINION_QA end case=%d committed=%d damage=%.1f first=%.2f hit=%.2f hp=%.1f"),
        CaseIndex,Committed,DamageDealt,FirstAttack,HitSeconds,EnemyHP);
}

void AHeliosMinionCombatQA::Finish(const FString& Error)
{
    bFinished=true; ClearCase();
    auto R=MakeShared<FJsonObject>();
    R->SetStringField(TEXT("status"),Error.IsEmpty()?TEXT("completed"):TEXT("failed"));
    R->SetStringField(TEXT("error"),Error);
    R->SetStringField(TEXT("policy"),TEXT("Real actors, default combat values, passive owner, authored animation notifies; no damage injection."));
    R->SetArrayField(TEXT("cases"),Results);
    FString Output; FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Output));
    FString Path=FPaths::ProjectDir()/TEXT("../qa/minion_logic/combat.json");
    FParse::Value(FCommandLine::Get(),TEXT("HeliosMinionReport="),Path);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    FFileHelper::SaveStringToFile(Output,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    FPlatformMisc::RequestExitWithStatus(false,Error.IsEmpty()?0:1);
}

void AHeliosMinionCombatQA::Tick(float Dt)
{
    Super::Tick(Dt); if(bFinished)return; Elapsed+=Dt;
    UGameplayStatics::SetGamePaused(GetWorld(),false);
    if(!bPrepared)
    {
        if(Elapsed<3)return;
        Player=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerPawn(this,0));
        if(!Player.IsValid()){Finish(TEXT("No player owner"));return;}
        PrepareArena(); return;
    }
    if(CaseIndex<0)
    {
        FNavLocation Point;
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
        if(Elapsed<6||!Nav||!Nav->ProjectPointToNavigation(Origin,Point,FVector(100,100,200)))
        {if(Elapsed>120)Finish(TEXT("Arena navigation unavailable"));return;}
        CaseIndex=0;
        FParse::Value(FCommandLine::Get(),TEXT("HeliosMinionCase="),CaseIndex);
        BeginCase(); return;
    }
    CaseTime+=Dt;
    if (Enemy.IsValid() && FParse::Param(FCommandLine::Get(),TEXT("HeliosMinionTrace"))
        && int32(CaseTime*4)!=int32((CaseTime-Dt)*4))
    {
        auto* AI=Cast<AShooterAIController>(Enemy->GetController());
        AActor* Target=AI?AI->GetCurrentTarget():nullptr;
        if(Target)
        {
            const FVector Muzzle=Enemy->GetMesh()->GetSocketLocation(TEXT("HandGrip_R"));
            FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(MinionQA),true,Enemy.Get());
            GetWorld()->LineTraceSingleByChannel(Hit,Muzzle,Target->GetActorLocation(),ECC_Visibility,Q);
            UE_LOG(LogTemp,Display,TEXT("MINION_TRACE t=%.2f self=%s target=%s muzzle=%s dist=%.1f range_dist=%.1f facing=%.2f blocker=%s target_name=%s state=%d cycles=%d notify=%d move=%d"),
                CaseTime,*Enemy->GetActorLocation().ToCompactString(),*Target->GetActorLocation().ToCompactString(),*Muzzle.ToCompactString(),
                Enemy->GetDistanceTo(Target),FVector::Distance(Muzzle,Target->GetActorLocation()),
                FVector::DotProduct(Enemy->GetActorForwardVector(),(Target->GetActorLocation()-Enemy->GetActorLocation()).GetSafeNormal()),
                *GetNameSafe(Hit.GetActor()),*Target->GetName(),int32(Enemy->GetCombatState()),Enemy->GetAttackCount(),Enemy->GetAttackNotifyCount(),int32(AI->GetMoveStatus()));
        }
    }
    const float HP=FriendlyHealth(); DamageDealt+=FMath::Max(0.f,LastFriendlyHP-HP); LastFriendlyHP=HP;
    if(Enemy.IsValid())
    {
        EnemyHP=Enemy->GetHealth(); Committed=Enemy->GetAttacksCommitted(); Attempts=Enemy->GetAttackCount();
        if(Committed>0&&FirstAttack<0)FirstAttack=CaseTime;
        if(Enemy->GetCombatState()==EHeliosEnemyCombatState::Hit)HitSeconds+=Dt;
        if(auto* AI=Cast<AShooterAIController>(Enemy->GetController()))
        {
            AActor* Target=AI->GetCurrentTarget();
            if(Target&&FirstAcquire<0)FirstAcquire=CaseTime;
            if(Target&&Target!=LastTarget.Get())++Acquisitions;
            LastTarget=Target;
        }
    }
    if(CaseTime>=12 || (CaseTime>2 && (EnemyHP<=0 || HP<=0)))
    {
        EndCase(); ++CaseIndex;
        int32 SingleCase=0;
        if(CaseIndex>=4*ScenarioCount*SeedCount || FParse::Value(FCommandLine::Get(),TEXT("HeliosMinionCase="),SingleCase))Finish(); else BeginCase();
    }
}
