#include "Pets/HeliosPetDog.h"
#include "Player/HeliosGuanXiaoYuAbilityComponent.h"
#include "Pets/HeliosHumanoidPet.h"
#include "Player/HeliosCharacterDefinition.h"
#include "Player/HeliosCharacterActionProfile.h"
#include "Engine/SkeletalMesh.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "World/HeliosMapDefinition.h"
#include "Combat/CombatDamageSettings.h"
#include "Combat/HeliosCombatTempoSettings.h"
#include "Health/CreatureHealthSettings.h"
#include "Pets/HeliosMeleeRules.h"
#include "Pets/HeliosUltimatumPet.h"
#include "Pets/HeliosSunShangXiangPet.h"
#include "Pets/HeliosZhongXiaoKuiPet.h"
#include "Pets/HeliosChanZeusPet.h"
#include "Pets/HeliosWangZheKuiThunderPet.h"
#include "Pets/HeliosYunSummerMegatronPet.h"
#include "Pets/HeliosBuLianShiP90Pet.h"
#include "Pets/HeliosPetVFXComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "Player/HeliosArenaCharacter.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Town/HeliosTownSubsystem.h"
#include "Variant_Shooter/AI/ShooterAIController.h"
#include "AIController.h"
#include "AI/Navigation/AvoidanceManager.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Navigation/PathFollowingComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Sound/SoundConcurrency.h"

const TArray<FHeliosPetDefinition>& FHeliosPetDefinition::All()
{
    static const TArray<FHeliosPetDefinition> Definitions={
        {EHeliosPetKind::SwordDog,TEXT("刀盾狗"),AHeliosPetDog::StaticClass()},
        {EHeliosPetKind::SunShangXiang,TEXT("孙尚香"),AHeliosSunShangXiangPet::StaticClass()},
        {EHeliosPetKind::ZhongXiaoKui,TEXT("钟小葵"),AHeliosZhongXiaoKuiPet::StaticClass()},
        {EHeliosPetKind::ChanZeus,TEXT("蝉·潜伏者"),AHeliosChanZeusPet::StaticClass()},
        {EHeliosPetKind::WangZheKuiThunder,TEXT("王者葵·保卫者"),AHeliosWangZheKuiThunderPet::StaticClass()},
        {EHeliosPetKind::YunSummerMegatron,TEXT("云悠悠·沐夏微醺"),AHeliosYunSummerMegatronPet::StaticClass()},
        {EHeliosPetKind::BuLianShiP90,TEXT("步练师·灵魂舞者"),AHeliosBuLianShiP90Pet::StaticClass()}};
    return Definitions;
}
const FHeliosPetDefinition* FHeliosPetDefinition::Find(EHeliosPetKind Kind)
{ for(const auto& D:All())if(D.Kind==Kind)return &D;return nullptr; }

AHeliosPetCompanion::AHeliosPetCompanion()
{
    PrimaryActorTick.bCanEverTick = true;
    AIControllerClass = AAIController::StaticClass();
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
    Tags.Add(TEXT("HeliosPet")); Tags.Add(TEXT("Friendly"));
    GetCapsuleComponent()->InitCapsuleSize(38.f, 48.f);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetMesh()->SetRelativeLocation(FVector(0,0,-GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()));
    GetMesh()->SetRelativeRotation(FRotator(0,-90,0));
    bUseControllerRotationYaw = false;
    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->RotationRate = FRotator(0,360,0);
    Effects=CreateDefaultSubobject<UHeliosPetVFXComponent>(TEXT("Pet Effects"));Effects->SetupAttachment(GetRootComponent());
    GetCharacterMovement()->MaxWalkSpeed = 520.f*Settings.MovementMultiplier;
    GetCharacterMovement()->MaxAcceleration = 1200.f*Settings.MovementMultiplier;
    GetCharacterMovement()->BrakingDecelerationWalking *= Settings.MovementMultiplier;
    GetCharacterMovement()->NavAgentProps.AgentRadius = 38.f;
    GetCharacterMovement()->NavAgentProps.AgentHeight = 192.f;
    GetCharacterMovement()->SetUpdateNavAgentWithOwnersCollisions(false);
}

void AHeliosPetCompanion::BeginPlay()
{
    Super::BeginPlay();
    const TCHAR* PetName = Kind == EHeliosPetKind::ZhongXiaoKui ? TEXT("钟小葵") : Kind == EHeliosPetKind::SunShangXiang ? TEXT("孙尚香") : Kind == EHeliosPetKind::UltimatumGR ? TEXT("蝴蝶雷神") : TEXT("刀盾狗");
    Settings.SleepSeconds = UHeliosCombatTempoSettings::Resolve(this, EHeliosTempoFile::Recovery,
        TEXT("宠物恢复秒"), PetName, Settings.SleepSeconds);
    if (Kind == EHeliosPetKind::SwordDog)
    {
        Settings.AttackInterval = UHeliosCombatTempoSettings::Resolve(this, EHeliosTempoFile::Attack,
            TEXT("宠物与召唤物"), TEXT("刀盾狗攻击间隔"), Settings.AttackInterval);
        Settings.MovementMultiplier = UHeliosCombatTempoSettings::Resolve(this,
            EHeliosTempoFile::Movement, TEXT("刀盾狗"), TEXT("全状态移动倍率"),
            Settings.MovementMultiplier);
        GetCharacterMovement()->MaxWalkSpeed = 520.f * Settings.MovementMultiplier;
        GetCharacterMovement()->MaxAcceleration = 1200.f * Settings.MovementMultiplier;
        GetCharacterMovement()->BrakingDecelerationWalking =
            GetDefault<UCharacterMovementComponent>()->BrakingDecelerationWalking * Settings.MovementMultiplier;
    }
    if(Kind!=EHeliosPetKind::ZhongXiaoKui)Settings.MaxHealth = UCreatureHealthSettings::Resolve(this,
        Kind == EHeliosPetKind::SunShangXiang ? ECreatureHealthType::SunShangXiang : Kind == EHeliosPetKind::UltimatumGR ? ECreatureHealthType::Ultimatum : ECreatureHealthType::SwordDog);
    if(Kind==EHeliosPetKind::SunShangXiang||Kind==EHeliosPetKind::ZhongXiaoKui)
    {
        const bool ZXK=Kind==EHeliosPetKind::ZhongXiaoKui;
        const auto H=[this,ZXK](const TCHAR* K,float D,float Min,float Max){return UHeliosCombatTempoSettings::Hunter(this,ZXK,true,K,D,Min,Max);};
        Settings.MaxHealth=H(TEXT("生命"),Settings.MaxHealth,1,1000000);
        Settings.MeleeRange=H(TEXT("攻击距离"),Settings.MeleeRange,10,10000);
        Settings.SleepSeconds=H(TEXT("倒地恢复时间"),Settings.SleepSeconds,.1,600);
        Settings.ShieldReduction=H(TEXT("格挡减伤比例"),Settings.ShieldReduction,0,.95);
        Settings.MovementMultiplier=H(TEXT("移动倍率"),Settings.MovementMultiplier,.1,5);
        Settings.FollowDistance=H(TEXT("跟随距离"),Settings.FollowDistance,100,10000);
        Settings.CatchUpDistance=FMath::Max(Settings.FollowDistance,H(TEXT("追赶距离"),Settings.CatchUpDistance,100,20000));
        Settings.CombatRadius=H(TEXT("寻敌半径"),Settings.CombatRadius,100,20000);
        Settings.FollowMinSpeed=H(TEXT("跟随最低速度"),520,10,5000);
        Settings.FollowMaxSpeed=FMath::Max(Settings.FollowMinSpeed,H(TEXT("跟随最高速度"),900,10,5000));
        Settings.ChaseSpeed=H(TEXT("战斗追敌速度"),650,10,5000);
        Settings.CatchUpSpeed=H(TEXT("远距追赶速度"),1000,10,5000);
    }
    Health = Settings.MaxHealth;
    Settings.Damage = UCombatDamageSettings::Resolve(this,ECombatDamageType::Dog);
    if(Kind == EHeliosPetKind::SwordDog) Settings.ShieldReduction = UCombatDamageSettings::Resolve(this,ECombatDamageType::DogReduction);
    LoadModel();
    PreviousProgressPosition = GetActorLocation();
    SetPose(TEXT("Idle"));
}

void AHeliosPetCompanion::LoadModel()
{
    GetMesh()->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Helios/Pets/RoundDog/SK_RoundSwordDog.SK_RoundSwordDog")));
    bModelReady = GetMesh()->GetSkeletalMeshAsset() != nullptr;
    for (const FName Name : {FName("Idle"),FName("Move"),FName("Attack"),FName("AttackAlt"),FName("Guard"),FName("Hit"),FName("Down"),FName("Sleep"),FName("Wake")})
    {
        const FString Asset = FString::Printf(TEXT("/Game/Helios/Pets/RoundDog/A_RoundDog_%s.A_RoundDog_%s"), *Name.ToString(), *Name.ToString());
        if (UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *Asset)) Poses.Add(Name,Sequence);
    }
    if (!bModelReady) UE_LOG(LogTemp, Warning, TEXT("HELIOS_PET model_missing: import the authorized SwordDog model before visual delivery"));
}

void AHeliosPetCompanion::EndPlay(const EEndPlayReason::Type Reason)
{ if(Effects)Effects->Clear();Super::EndPlay(Reason); }

void AHeliosPetCompanion::SetPose(FName Name, bool bLoop)
{
    if(auto* Anim=GetMesh()->GetSingleNodeInstance())Anim->SetPlayRate(Name==TEXT("Move")?FMath::Clamp(GetVelocity().Size2D()/520.f,.8f,3.f):1.f);
    if (CurrentPose == Name) return;
    CurrentPose = Name;
    if (const auto* Sequence = Poses.Find(Name)) GetMesh()->PlayAnimation(*Sequence, bLoop);
}

bool AHeliosPetCompanion::HasClearLine(AActor* Actor) const
{
    if (!IsValid(Actor)) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(PetSight), true, this);
    Query.AddIgnoredActor(CompanionOwner.Get());
    if(auto* Roster=GetWorld()->GetSubsystem<UHeliosPetWorld>())Roster->IgnoreAll(Query);
    FHitResult Hit;
    const FVector Start = GetActorLocation()+FVector(0,0,25);
    return !GetWorld()->LineTraceSingleByChannel(Hit, Start, Actor->GetActorLocation(), ECC_Visibility, Query) || Hit.GetActor()==Actor;
}

bool AHeliosPetCompanion::CanReach(const FVector& Destination) const
{
    UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),GetActorLocation(),Destination,const_cast<AHeliosPetCompanion*>(this));
    return Path && Path->IsValid() && !Path->IsPartial();
}

bool AHeliosPetCompanion::FindFollowPosition(FVector& Out) const
{
    if (!CompanionOwner.IsValid()) return false;
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!Nav) return false;
    const FVector Origin = CompanionOwner->GetActorLocation();
    // Try the desired rear position first, then nearby rear arcs, then closer safe ground.
    for (const float Distance : {Settings.FollowDistance, Settings.FollowDistance*.8f, 1200.f, 400.f})
        for (const float Angle : {0.f, 25.f, -25.f, 55.f, -55.f})
        {
            FNavLocation Projected;
            const float SlotAngle=RosterSize>1?(RosterSlot-(RosterSize-1)*.5f)*24.f:0.f;
            const FVector Candidate = Origin-FollowHeading.RotateAngleAxis(Angle+SlotAngle,FVector::UpVector)*Distance;
            if (!Nav->ProjectPointToNavigation(Candidate,Projected,FVector(250,250,400),&GetNavAgentPropertiesRef())) continue;
            const FVector Center=Projected.Location+FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2);
            if(RosterSize>1)
            {
                const float Radius=GetCapsuleComponent()->GetScaledCapsuleRadius();
                if(FVector::Dist2D(Center,Origin)<Radius+CompanionOwner->GetCapsuleComponent()->GetScaledCapsuleRadius()+120)continue;
                bool Crowded=false;
                if(auto* Roster=GetWorld()->GetSubsystem<UHeliosPetWorld>())for(const auto& Other:Roster->Pets)if(IsValid(Other)&&Other!=this)
                {
                    const float Gap=Radius+Other->GetCapsuleComponent()->GetScaledCapsuleRadius()+80;
                    Crowded|=FVector::Dist2D(Center,Other->GetActorLocation())<Gap;
                    if(Other->State!=EHeliosPetState::Fighting&&!Other->LastGoal.IsNearlyZero())
                        Crowded|=FVector::Dist2D(Center,Other->LastGoal)<Gap+200;
                }
                if(Crowded)continue;
            }
            UNavigationPath* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Origin,Projected.Location,CompanionOwner.Get());
            if (!Path || !Path->IsValid() || Path->IsPartial()) continue;
            FCollisionQueryParams Q(SCENE_QUERY_STAT(PetSpawn),false,this); Q.AddIgnoredActor(CompanionOwner.Get());
            if (!GetWorld()->OverlapBlockingTestByChannel(Center,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(GetCapsuleComponent()->GetScaledCapsuleRadius(),GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),Q))
            { Out=Center; return true; }
        }
    return false;
}

bool AHeliosPetCompanion::RejoinOwner()
{
    FVector Place;
    if (!FindFollowPosition(Place)) return false;
    if (auto* AI=Cast<AAIController>(GetController())) AI->StopMovement();
    GetCharacterMovement()->StopMovementImmediately();
    const bool Success=TeleportTo(Place,FollowHeading.Rotation(),false,false);
    if (Success) { PreviousProgressPosition=Place; StuckSeconds=0.f; bMoveRequested=false; }
    return Success;
}

void AHeliosPetCompanion::MoveTowards(const FVector& Destination, float Acceptance, float FailedMoveSeconds)
{
    auto* AI=Cast<AAIController>(GetController());
    if (!AI) return;
    if (FVector::Dist2D(Destination,GetActorLocation())<=Acceptance)
    { AI->StopMovement(); bMoveRequested=false; return; }
    const float RepathDistance=State==EHeliosPetState::Fighting?50.f:150.f;
    if (bMoveRequested && FVector::Dist2D(LastGoal,Destination)<RepathDistance && AI->GetMoveStatus()==EPathFollowingStatus::Moving) return;
    LastGoal=Destination;
    bMoveRequested=AI->MoveToLocation(Destination,Acceptance,true,true,true,false,nullptr,false)!=EPathFollowingRequestResult::Failed;
    if (!bMoveRequested) StuckSeconds+=FailedMoveSeconds;
}

void AHeliosPetCompanion::TrackCombatTarget(float Dt)
{
    auto* Enemy=Target.Get();
    if(!Enemy)return;
    const bool Visible=HasClearLine(Enemy);
    // Leave room for the enemy to move during the swing instead of stopping at
    // the outer edge of melee range. Continue path following throughout attacks.
    MoveTowards(Enemy->GetActorLocation(),Visible?FMath::Max(30.f,Settings.MeleeRange*.45f):30.f,Dt);
    const FVector Offset=Enemy->GetActorLocation()-GetActorLocation();
    if(Visible&&Offset.SizeSquared2D()<=FMath::Square(Settings.MeleeRange*1.5f)&&!Offset.IsNearlyZero())
        SetActorRotation(FMath::RInterpTo(GetActorRotation(),Offset.GetSafeNormal2D().Rotation(),Dt,16.f));
}

void AHeliosPetCompanion::ObserveDamage(AActor* Damaged, float Damage, const UDamageType*, AController* InstigatorController, AActor* Causer)
{
    if(Damage>0&&Cast<AHeliosHumanoidPet>(this)&&InstigatorController==GetController()&&Cast<AHeliosEnemyActor>(Damaged))
    {++HitsDelivered;EngagedUntil.Add(Cast<AHeliosEnemyActor>(Damaged),GetWorld()->GetTimeSeconds()+5.);}
    if(Causer&&(Causer->ActorHasTag(TEXT("HeliosSummon"))||Causer->ActorHasTag(TEXT("HeliosPet"))))return;
    auto* Enemy=Cast<AHeliosEnemyActor>(Damaged);
    if (Damage<=0 || !Enemy || !CompanionOwner.IsValid()) return;
    if (InstigatorController==CompanionOwner->GetController())
    {
        RecentPlayerTarget=Enemy; RecentPlayerHitTime=GetWorld()->GetTimeSeconds();
        EngagedUntil.Add(Enemy,RecentPlayerHitTime+5.);
    }
}

bool AHeliosPetCompanion::CanEngageTarget(AActor* Actor) const { return Actor&&CanReach(Actor->GetActorLocation()); }
double AHeliosPetCompanion::CombatTargetScore(AHeliosEnemyActor* Enemy,bool Recent,bool Threat) const
{ return HeliosMelee::TargetScore(Recent,Threat,FVector::DistSquared2D(GetActorLocation(),Enemy->GetActorLocation())); }

void AHeliosPetCompanion::RefreshDecision()
{
    if (!CompanionOwner.IsValid() || IsSleeping() || State==EHeliosPetState::Waking) return;
    GetCharacterMovement()->MaxAcceleration=1200.f*Settings.MovementMultiplier;
    GetCharacterMovement()->BrakingDecelerationWalking=GetDefault<UCharacterMovementComponent>()->BrakingDecelerationWalking*Settings.MovementMultiplier;
    const FVector OwnerPosition=CompanionOwner->GetActorLocation();
    const double Now=GetWorld()->GetTimeSeconds();
    for (auto It=EngagedUntil.CreateIterator(); It; ++It) if (!It.Key().IsValid() || It.Value()<Now) It.RemoveCurrent();
    AHeliosEnemyActor* Best=nullptr; double BestScore=1.e30;
    const bool bOutsideLeash=FVector::DistSquared2D(OwnerPosition,GetActorLocation())>FMath::Square(Settings.CombatRadius);
    for (TActorIterator<AHeliosEnemyActor> It(GetWorld()); It; ++It)
    {
        AHeliosEnemyActor* Enemy=*It;
        if (bOutsideLeash || Enemy->IsEnemyDead() || FVector::DistSquared2D(OwnerPosition,Enemy->GetActorLocation())>FMath::Square(Settings.CombatRadius)) continue;
        auto* AI=Cast<AShooterAIController>(Enemy->GetController());
        AActor* EnemyTarget=AI?AI->GetCurrentTarget():nullptr;
        const bool Threat=EnemyTarget==CompanionOwner.Get() || EnemyTarget==this;
        if (Threat) EngagedUntil.Add(Enemy,Now+3.);
        const bool Recent=RecentPlayerTarget.Get()==Enemy && Now-RecentPlayerHitTime<=5.;
        // Acquire any reachable enemy around the owner, including enemies behind cover.
        // Visibility is checked when swinging and applying melee damage, not when acquiring.
        const double Score=CombatTargetScore(Enemy,Recent,Threat);
        if (Score<BestScore && CanEngageTarget(Enemy)) {Best=Enemy;BestScore=Score;}
    }
    if (Best)
    {
        Target=Best; State=EHeliosPetState::Fighting;
        GetCharacterMovement()->MaxWalkSpeed=Settings.ChaseSpeed*Settings.MovementMultiplier;
        // A nearby target behind a wall still needs a path around it; do not stop
        // at melee distance until the target is visible.
        TrackCombatTarget(0.f);
    }
    else
    {
        const bool WasFighting=Target.IsValid() || State==EHeliosPetState::Returning;
        Target.Reset();
        const float Distance=FVector::Dist2D(GetActorLocation(),OwnerPosition);
        State=Distance>Settings.CatchUpDistance?EHeliosPetState::CatchingUp:WasFighting?EHeliosPetState::Returning:EHeliosPetState::Following;
        GetCharacterMovement()->MaxWalkSpeed=(State==EHeliosPetState::CatchingUp?Settings.CatchUpSpeed:FMath::Clamp(CompanionOwner->GetVelocity().Size2D()+150.f,Settings.FollowMinSpeed,Settings.FollowMaxSpeed))*Settings.MovementMultiplier;
        FVector Goal;
        if (FindFollowPosition(Goal))
        {
            const float GoalDistance=FVector::Dist2D(GetActorLocation(),Goal);
            // Five metres of slack around the desired 25m avoids nervous shuffling.
            MoveTowards(Goal,RosterSize>1?100.f:450.f);
            if (GoalDistance<(RosterSize>1?100.f:450.f)) State=EHeliosPetState::Following;
        }
        else { StuckSeconds+=.5f; bMoveRequested=false; }
    }
}

bool AHeliosPetCompanion::CanMeleeHit(AHeliosEnemyActor* Enemy) const
{
    if(!Enemy || Enemy->IsEnemyDead() || !CompanionOwner.IsValid())return false;
    return HeliosMelee::InStrike(this,Enemy,CompanionOwner->GetActorLocation(),Settings.CombatRadius,Settings.MeleeRange) && HasClearLine(Enemy);
}

void AHeliosPetCompanion::AttackHit()
{
    if(bHitCommitted)return;
    bHitCommitted=true;
    AHeliosEnemyActor* Enemy=Target.Get();
    if(!CanMeleeHit(Enemy))return;
    const FVector Start=GetActorLocation()+FVector(0,0,25);
    FHitResult Contact;FCollisionQueryParams Query(SCENE_QUERY_STAT(PetMeleeContact),false,this);Query.AddIgnoredActor(CompanionOwner.Get());
    if(auto* Roster=GetWorld()->GetSubsystem<UHeliosPetWorld>())Roster->IgnoreAll(Query);
    const bool Hit=GetWorld()->LineTraceSingleByChannel(Contact,Start,Enemy->GetActorLocation(),ECC_Visibility,Query);
    if(Hit&&Contact.GetActor()!=Enemy)return;
    if(UGameplayStatics::ApplyDamage(Enemy,Settings.Damage,GetController(),this,UDamageType::StaticClass())>0.f)
    {
        ++HitsDelivered;Effects->Hit(Hit?Contact.ImpactPoint:Enemy->GetActorLocation());
        EngagedUntil.Add(Enemy,GetWorld()->GetTimeSeconds()+5.);
    }
}

void AHeliosPetCompanion::ForgetPetThreats()
{
    for (TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It)
        if (auto* AI=Cast<AShooterAIController>(It->GetController()))
            if (AI->GetCurrentTarget()==this || AI->GetTrackingTarget()==this)
            { AI->ResetAwareness(); It->StopShooting(); }
}

void AHeliosPetCompanion::Sleep()
{
    CancelAttack();
    Health=0; SleepRemaining=Settings.SleepSeconds; State=EHeliosPetState::Sleeping;
    Tags.AddUnique(TEXT("Dead")); Target.Reset(); AttackAge=-1.f; bMoveRequested=false;
    if (auto* AI=Cast<AAIController>(GetController())) {AI->StopMovement();AI->ClearFocus(EAIFocusPriority::Gameplay);}
    GetCharacterMovement()->DisableMovement();
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DownRemaining=RecoveryDuration(TEXT("Down"),.6f);HitPoseRemaining=0;GetMesh()->SetRelativeLocation(FVector(0,0,-GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()));
    SetPose(TEXT("Down"),false);Effects->SetSleeping(true);ForgetPetThreats();
    UE_LOG(LogTemp,Display,TEXT("HELIOS_PET sleep duration=%.2f"),SleepRemaining);
}

void AHeliosPetCompanion::Restore(float InHealth,float InSleepRemaining)
{
    if (InSleepRemaining>0.f || InHealth<=0.f) {Sleep();SleepRemaining=FMath::Max(.01f,InSleepRemaining);DownRemaining=0;SetPose(TEXT("Sleep"));}
    else Health=FMath::Min(InHealth,Settings.MaxHealth);
}

float AHeliosPetCompanion::TakeDamage(float Amount,const FDamageEvent& Event,AController* InstigatorController,AActor* Causer)
{
    if (!CanBeDamaged() || IsSleeping() || State==EHeliosPetState::Waking || Amount<=0.f) return 0;
    const APawn* Pawn=InstigatorController?InstigatorController->GetPawn():nullptr;
    if ((Pawn && (Pawn->IsPlayerControlled() || Pawn->ActorHasTag(TEXT("HeliosPet"))))
        || (Causer && (Causer->ActorHasTag(TEXT("HeliosPet")) || Causer==CompanionOwner.Get()))) return 0;
    if(auto* Ability=FindComponentByClass<UHeliosGuanXiaoYuAbilityComponent>();Ability&&Ability->TryAbsorbDamage(Amount,Event,InstigatorController,Causer)){++Blocks;return 0;}
    FVector Source=Pawn?Pawn->GetActorLocation():Causer?Causer->GetActorLocation():GetActorLocation()-GetActorForwardVector();
    if (Settings.ShieldReduction>0 && !Event.IsOfType(FRadialDamageEvent::ClassID) && State==EHeliosPetState::Fighting && AttackAge<0.f
        && FVector::DotProduct(GetActorForwardVector(),(Source-GetActorLocation()).GetSafeNormal2D())>=.5f)
    { Amount*=1.f-Settings.ShieldReduction; ++Blocks; SetPose(TEXT("Guard"));Effects->Block(GetMesh()->GetSocketLocation(TEXT("shield_center"))); }
    else if(AttackAge<0){HitPoseRemaining=.25f;SetPose(TEXT("Hit"),false);}
    const float Applied=FMath::Min(Health,Amount); Health-=Applied;
    if (auto* Enemy=Cast<AHeliosEnemyActor>(const_cast<APawn*>(Pawn))) EngagedUntil.Add(Enemy,GetWorld()->GetTimeSeconds()+5.);
    if (Health<=0) Sleep();
    return Applied;
}

bool AHeliosPetCompanion::OutsideView() const
{
    const auto* PC=CompanionOwner.IsValid()?Cast<APlayerController>(CompanionOwner->GetController()):nullptr;
    if (!PC || !PC->PlayerCameraManager) return false;
    const FVector Direction=(GetActorLocation()-PC->PlayerCameraManager->GetCameraLocation()).GetSafeNormal();
    return FVector::DotProduct(Direction,PC->PlayerCameraManager->GetActorForwardVector())<-.1f;
}

void AHeliosPetCompanion::Tick(float Dt)
{
    Super::Tick(Dt);
    if (Dt<=0 || !CompanionOwner.IsValid()) return;
    const FVector DesiredHeading=CompanionOwner->GetActorForwardVector().GetSafeNormal2D();
    FollowHeading=FMath::VInterpTo(FollowHeading,DesiredHeading,Dt,.8f).GetSafeNormal2D();
    if (IsSleeping())
    {
        DownRemaining=FMath::Max(0.f,DownRemaining-Dt);if(DownRemaining<=0)SetPose(TEXT("Sleep"));
        SleepRemaining=FMath::Max(0.f,SleepRemaining-Dt);
        UpdateRecoveryGround(Dt);
        if (SleepRemaining>0.f) return;
        FHitResult Ground; FCollisionQueryParams Q(SCENE_QUERY_STAT(PetWakeFloor),false,this);
        const bool Floor=GetWorld()->LineTraceSingleByChannel(Ground,GetActorLocation()+FVector(0,0,20),GetActorLocation()-FVector(0,0,150),ECC_WorldStatic,Q);
        if ((!Floor || FVector::Dist2D(GetActorLocation(),CompanionOwner->GetActorLocation())>Settings.CatchUpDistance) && !RejoinOwner()) return;
        if (GetWorld()->OverlapBlockingTestByChannel(GetActorLocation(),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(GetCapsuleComponent()->GetScaledCapsuleRadius(),GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),Q) && !RejoinOwner()) return;
        Health=Settings.MaxHealth; Tags.Remove(TEXT("Dead"));
        GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        if(!HoldMovementUntilStanding())GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        State=EHeliosPetState::Waking;WakeRemaining=RecoveryDuration(TEXT("Wake"),1.f);Effects->SetSleeping(false);SetPose(TEXT("Wake"),false);
        UE_LOG(LogTemp,Display,TEXT("HELIOS_PET awake hp=%.0f"),Health);
        return;
    }
    if (State==EHeliosPetState::Waking)
    {
        WakeRemaining-=Dt;
        GetMesh()->SetRelativeLocation(FVector(0,0,-GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()));
        UpdateRecoveryGround(Dt);
        if(WakeRemaining<=0){State=EHeliosPetState::Following;DecisionRemaining=0;
            if(HoldMovementUntilStanding())GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            RecoveryFinished();}
        return;
    }
    if (CompanionOwner->IsDead()) {if(auto* AI=Cast<AAIController>(GetController()))AI->StopMovement();return;}
    Cooldown=FMath::Max(0.f,Cooldown-Dt);
    if(State==EHeliosPetState::Fighting&&(!Target.IsValid()||Target->IsEnemyDead()
        ||FVector::DistSquared2D(CompanionOwner->GetActorLocation(),Target->GetActorLocation())>FMath::Square(Settings.CombatRadius)))
    {
        CancelAttack();Target.Reset();AttackAge=-1.f;bHitCommitted=true;CurrentPose=NAME_None;DecisionRemaining=0;
        if(auto* AI=Cast<AAIController>(GetController()))AI->StopMovement();bMoveRequested=false;
    }
    // Keep the current swing's target stable, but track its latest position on
    // every frame, including wind-up and recovery.
    if(AttackAge<0.f)
    {
        DecisionRemaining-=Dt;
        if(DecisionRemaining<=0.f){DecisionRemaining=.5f;RefreshDecision();}
    }
    if(Target.IsValid())TrackCombatTarget(Dt);
    UpdateLocomotion(Dt);
    HitPoseRemaining=FMath::Max(0.f,HitPoseRemaining-Dt);
    UpdateAttack(Dt);
    if(bMoveRequested)
    {
        if(FVector::Dist2D(PreviousProgressPosition,GetActorLocation())>50.f){PreviousProgressPosition=GetActorLocation();StuckSeconds=0;}
        else StuckSeconds+=Dt;
    }
    else if (GetVelocity().Size2D()>20.f) StuckSeconds=0;
    if(StuckSeconds>=5.f && OutsideView()){Target.Reset();RejoinOwner();DecisionRemaining=0;}
}

void AHeliosPetCompanion::CancelAttack()
{
    AttackAge=-1.f;bHitCommitted=true;
    if(auto* Anim=GetMesh()->GetSingleNodeInstance())Anim->SetPlayRate(1.f);
    if(Effects)Effects->Clear();
}

void AHeliosPetCompanion::SuspendCombat()
{
    CancelAttack();Target.Reset();bMoveRequested=false;
    if(auto* AI=Cast<AAIController>(GetController()))AI->StopMovement();
    GetCharacterMovement()->StopMovementImmediately();
}

void AHeliosPetCompanion::UpdateAttack(float Dt)
{
    if (AttackAge>=0.f)
    {
        AttackAge+=Dt;
        if (!bHitCommitted && AttackAge>=.32f) AttackHit();
        if (AttackAge>=.77f) {AttackAge=-1.f;CurrentPose=NAME_None;DecisionRemaining=0;}
    }
    else if(HitPoseRemaining>0.f) { /* Preserve the short hit reaction while tracking. */ }
    else if (Target.IsValid() && !Target->IsEnemyDead() && FVector::Dist2D(GetActorLocation(),Target->GetActorLocation())<=Settings.MeleeRange && HasClearLine(Target.Get()))
    {
        if(Cooldown<=0.f&&CanMeleeHit(Target.Get())){AttackAge=0;Cooldown=Settings.AttackInterval;bHitCommitted=false;const bool Alternate=(AttackSerial++%2)!=0;SetPose(Alternate?TEXT("AttackAlt"):TEXT("Attack"),false);Effects->BeginSwing(Settings.MeleeRange,Alternate);}
        else SetPose(GetVelocity().Size2D()>20.f?TEXT("Move"):TEXT("Guard"));
    }
    else SetPose(GetVelocity().Size2D()>20.f?TEXT("Move"):TEXT("Idle"));
}

FText AHeliosPetCompanion::StatusText() const
{
    const TCHAR* Label=TEXT("跟随");
    switch(State){case EHeliosPetState::CatchingUp:Label=TEXT("追赶");break;case EHeliosPetState::Fighting:Label=TEXT("助战");break;case EHeliosPetState::Returning:Label=TEXT("返回");break;case EHeliosPetState::Waking:Label=TEXT("起身");break;default:break;}
    if(IsSleeping())return FText::FromString(FString::Printf(TEXT("%s  休息 %d 秒"),*DisplayName(),FMath::CeilToInt(SleepRemaining)));
    const float Metres=CompanionOwner.IsValid()?FVector::Dist2D(GetActorLocation(),CompanionOwner->GetActorLocation())/100.f:0;
    return FText::FromString(FString::Printf(TEXT("%s  %s · %.0f 米  %.0f / %.0f"),*DisplayName(),Label,Metres,Health,Settings.MaxHealth));
}

bool UHeliosPetWorld::DoesSupportWorldType(EWorldType::Type Type) const
{ return Type==EWorldType::Game || Type==EWorldType::PIE; }


void UHeliosPetWorld::Snapshot()
{
    if(!GetWorld()->GetGameInstance())return;
    auto* Session=GetWorld()->GetGameInstance()->GetSubsystem<UHeliosPetSession>();if(!Session)return;
    Session->Snapshots.SetNum(4);
    for(int32 I=0;I<Pets.Num();++I)if(IsValid(Pets[I]))
        Session->Snapshots[I]={true,Pets[I]->Kind,Pets[I]->Health,Pets[I]->SleepRemaining};
    if(IsValid(Pet)){Session->SnapshotKind=Pet->Kind;Session->bHasSnapshot=true;Session->Health=Pet->Health;Session->SleepRemaining=Pet->SleepRemaining;}
}
void UHeliosPetWorld::Clear(bool Save)
{
    if(Save)Snapshot();
    ResetPlayerAvoidance();
    for(auto& P:Pets)if(IsValid(P)){P->SuspendCombat();if(auto* AI=P->GetController())AI->Destroy();P->Destroy();}
    Pets.Reset();Pet=nullptr;
}
void UHeliosPetWorld::IgnoreAll(FCollisionQueryParams& Query) const
{for(const auto& P:Pets)if(IsValid(P))Query.AddIgnoredActor(P);}
USoundConcurrency* UHeliosPetWorld::GetVoiceConcurrency()
{
    if(!VoiceConcurrency){VoiceConcurrency=NewObject<USoundConcurrency>(this);VoiceConcurrency->Concurrency.MaxCount=2;VoiceConcurrency->Concurrency.ResolutionRule=EMaxConcurrentResolutionRule::PreventNew;}
    return VoiceConcurrency;
}
void UHeliosPetWorld::ObserveDamage(AActor* Damaged,float Damage,const UDamageType* Type,AController* Controller,AActor* Causer)
{for(const auto& P:Pets)if(IsValid(P))P->ObserveDamage(Damaged,Damage,Type,Controller,Causer);}

void UHeliosPetWorld::ResetPlayerAvoidance()
{
    if(PassivePlayerUID!=INDEX_NONE)
    {
        if(auto* Manager=GetWorld()->GetAvoidanceManager())Manager->RemoveAvoidanceObject(PassivePlayerUID);
        if(auto* Movement=PassivePlayerMovement.Get()){Movement->SetRVOAvoidanceUID(OriginalPlayerUID);Movement->SetRVOAvoidanceWeight(OriginalPlayerAvoidanceWeight);}
    }
    PassivePlayerUID=INDEX_NONE;PassivePlayerMovement.Reset();
}
void UHeliosPetWorld::UpdatePlayerAvoidance(AHeliosArenaCharacter* Player)
{
    auto* Movement=Player?Player->GetCharacterMovement():nullptr;
    if(!Movement||Player->IsDead()||Pets.Num()<2||Movement->bUseRVOAvoidance){ResetPlayerAvoidance();return;}
    auto* Manager=GetWorld()->GetAvoidanceManager();if(!Manager)return;
    if(PassivePlayerMovement.Get()!=Movement)
    {
        ResetPlayerAvoidance();OriginalPlayerUID=Movement->GetRVOAvoidanceUID();OriginalPlayerAvoidanceWeight=Movement->GetRVOAvoidanceWeight();
        if(!Manager->RegisterMovementComponent(Movement,1.f))return;
        PassivePlayerMovement=Movement;PassivePlayerUID=Movement->GetRVOAvoidanceUID();
    }
    // Publish the player as a moving obstacle for companions. Player movement
    // never enables or calculates RVO, so manual controls are not diverted.
    Manager->UpdateRVO(Movement);
}

void UHeliosPetWorld::Tick(float Dt)
{
    UWorld* World=GetWorld();
    if(!World || !World->HasBegunPlay() || UGameplayStatics::IsGamePaused(World))return;
    if(!FHeliosMapRegistry::ForWorld(World))return;
    UpdatePlayerAvoidance(Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(World,0)));
    CheckRemaining-=Dt;if(CheckRemaining>0)return;CheckRemaining=.25f;
    auto* Director=AHeliosAdventureDirector::Find(World);
    auto* Player=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(World,0));
    if(!Director || !Player)return;
    if(Director->IsSelectingWeapon() || Director->IsRunOver())return;
    if(auto* Town=Director->GetTownSubsystem())if(!Town->IsWorldReady())return;
    auto* Session=World->GetGameInstance()->GetSubsystem<UHeliosPetSession>();if(!Session)return;
    Session->NormalizeLegacySelection();
    const int32 Serial=Director->GetRunSerial();
    const bool NewRun=LastRunSerial>=0 && Serial!=LastRunSerial;
    if(NewRun){Clear(false);Session->ResetSnapshots();}
    LastRunSerial=Serial;
    const int32 Count=FParse::Param(FCommandLine::Get(),TEXT("HeliosNoPet"))?0:Session->GetCount();
    for(int32 I=Count;I<Pets.Num();++I)if(IsValid(Pets[I]))
    {
        Session->Snapshots.SetNum(4);Session->Snapshots[I]={true,Pets[I]->Kind,Pets[I]->Health,Pets[I]->SleepRemaining};
        Pets[I]->SuspendCombat();if(auto* AI=Pets[I]->GetController())AI->Destroy();Pets[I]->Destroy();
    }
    Pets.SetNum(Count);Session->Snapshots.SetNum(4);
    for(int32 I=0;I<Count;++I)
    {
        const auto Kind=Session->GetKind(I);
        const FHeliosLoadout Selected=Session->GetLoadout(I);
        auto& Companion=Pets[I];
        TObjectPtr<AHeliosPetCompanion> PreviousCompanion=nullptr;
        ON_SCOPE_EXIT {if(!IsValid(Companion)&&IsValid(PreviousCompanion))Companion=PreviousCompanion;};
        if(auto* Humanoid=Cast<AHeliosHumanoidPet>(Companion);Humanoid&&Humanoid->Loadout!=Selected&&Kind!=EHeliosPetKind::SwordDog)
        {
            // A rejected hot swap keeps the actor, weapon and recovery state.
            // Do not fall through to destroying it and respawning every .25 s.
            if(!Humanoid->ApplyLoadout(Selected))continue;
        }
        if(IsValid(Companion)&&Companion->Loadout!=Selected)
        {
            FString Error;if(!HeliosPetLoadouts::ValidatePresentation(Selected,Error))continue;
            Session->Snapshots[I]={true,Companion->Kind,Companion->Health,Companion->SleepRemaining};
            PreviousCompanion=Companion;Companion=nullptr;
        }
        if(!IsValid(Companion))
        {
            FString Error;if(!HeliosPetLoadouts::ValidatePresentation(Selected,Error))
            {UE_LOG(LogTemp,Warning,TEXT("HELIOS_PET invalid_loadout slot=%d reason=%s"),I,*Error);continue;}
            const auto* Definition=FHeliosPetDefinition::Find(Kind);if(!Definition)continue;
            TSubclassOf<AHeliosPetCompanion> CompanionClass=Definition->Class;
            if(Kind!=EHeliosPetKind::SwordDog)CompanionClass=AHeliosHumanoidPet::StaticClass();
            const auto* Default=CompanionClass->GetDefaultObject<AHeliosPetCompanion>();
            const float Radius=Default->GetCapsuleComponent()->GetScaledCapsuleRadius(),Height=Default->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);if(!Nav)continue;
            FVector Place;bool Safe=false;
            const float BaseAngle=Count>1?(I-(Count-1)*.5f)*32.f:0.f;
            for(float Distance:{450.f,700.f,1000.f,1400.f})
            {
                for(float Offset:{0.f,22.f,-22.f,60.f,-60.f,120.f,-120.f})
                {
                    FNavLocation N;const FVector Candidate=Player->GetActorLocation()-Player->GetActorForwardVector().RotateAngleAxis(BaseAngle+Offset,FVector::UpVector)*Distance;
                    if(!Nav->ProjectPointToNavigation(Candidate,N,FVector(150,150,400)))continue;
                    auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,Player->GetActorLocation(),N.Location,Player);
                    if(!Path||!Path->IsValid()||Path->IsPartial())continue;
                    FCollisionQueryParams Q(SCENE_QUERY_STAT(PetRosterSpawn),false);
                    FHitResult Ground;if(!World->LineTraceSingleByChannel(Ground,N.Location+FVector(0,0,80),N.Location-FVector(0,0,150),ECC_WorldStatic,Q)||Ground.ImpactNormal.Z<.7f)continue;
                    Place=Ground.ImpactPoint+FVector(0,0,Height+3);
                    if(World->OverlapBlockingTestByChannel(Place,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Radius+3,Height),Q))continue;
                    bool Separated=FVector::Dist2D(Place,Player->GetActorLocation())>Radius+Player->GetCapsuleComponent()->GetScaledCapsuleRadius()+100;
                    for(const auto& Other:Pets)if(IsValid(Other)&&FVector::Dist2D(Place,Other->GetActorLocation())<Radius+Other->GetCapsuleComponent()->GetScaledCapsuleRadius()+100)Separated=false;
                    if(!Separated)continue;
                    Safe=true;break;
                }
                if(Safe)break;
            }
            if(!Safe){UE_LOG(LogTemp,Warning,TEXT("HELIOS_PET waiting_for_safe_spawn slot=%d"),I);continue;}
            FActorSpawnParameters Params;Params.Owner=Player;Params.Instigator=Player;
            Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
            const FTransform SpawnTransform(Player->GetActorRotation(),Place);
            Companion=World->SpawnActorDeferred<AHeliosPetCompanion>(CompanionClass,SpawnTransform,Player,Player,ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding);if(!Companion)continue;
            Companion->Loadout=Selected;Companion->Kind=Kind;Companion->CompanionOwner=Player;Companion->RosterSlot=I;Companion->RosterSize=Count;
            UGameplayStatics::FinishSpawningActor(Companion,SpawnTransform);
            if(!IsValid(Companion)||!Companion->bModelReady)
            {if(IsValid(Companion))Companion->Destroy();Companion=nullptr;continue;}
            if(IsValid(PreviousCompanion))
            {PreviousCompanion->SuspendCombat();if(auto* AI=PreviousCompanion->GetController())AI->Destroy();PreviousCompanion->Destroy();PreviousCompanion=nullptr;}
            Companion->GetCharacterMovement()->AvoidanceConsiderationRadius=220;
            Companion->GetCharacterMovement()->SetAvoidanceEnabled(Count>1);
            Player->MoveIgnoreActorAdd(Companion);Companion->MoveIgnoreActorAdd(Player);
            // Peers keep their capsule collision; RVO and distinct follow goals
            // steer around each other. Only the player ignores companion movement.
            const auto& Snap=Session->Snapshots[I];
            UE_LOG(LogTemp,Display,TEXT("HELIOS_PET restore slot=%d valid=%d hp=%.1f sleep=%.1f"),I,Snap.bValid,Snap.Health,Snap.SleepRemaining);
            if(Snap.bValid)Companion->Restore(Snap.Health,Snap.SleepRemaining);
            else if(I==0&&Session->bHasSnapshot)Companion->Restore(Session->Health,Session->SleepRemaining);
            UE_LOG(LogTemp,Display,TEXT("HELIOS_PET spawned run=%d slot=%d count=%d kind=%d actor=%s place=%s"),Serial,I,Count,int32(Kind),*Companion->GetName(),*Place.ToCompactString());
        }
        if(Companion->CompanionOwner.Get()!=Player){Companion->CompanionOwner=Player;Player->MoveIgnoreActorAdd(Companion);Companion->MoveIgnoreActorAdd(Player);if(!Companion->IsSleeping())Companion->RejoinOwner();}
        Companion->RosterSize=Count;
        if(Companion->GetCharacterMovement()->bUseRVOAvoidance!=(Count>1))Companion->GetCharacterMovement()->SetAvoidanceEnabled(Count>1);
    }
    Pet=Pets.IsValidIndex(0)?Pets[0]:nullptr;
    Snapshot();
}
void UHeliosPetWorld::Deinitialize()
{Snapshot();Clear(false);Super::Deinitialize();}

namespace
{
FString PetSelectionPath(){return FPaths::ProjectSavedDir()/((FParse::Param(FCommandLine::Get(),TEXT("HeliosLoadoutQA"))||FParse::Param(FCommandLine::Get(),TEXT("HeliosLoadoutFlowQA")))?TEXT("Automation/HeliosLoadout/HeliosCompanions.v3.json"):TEXT("SaveGames/HeliosCompanions.v2.json"));}
}
namespace HeliosPetLoadouts
{
bool ValidatePresentation(const FHeliosLoadout& L,FString& Error)
{
    if(!FHeliosLoadout::Validate(L,Error))return false;
    if(L.CharacterId==TEXT("sword_dog"))return true;
    if(L.PrimaryWeaponId==TEXT("mech")||L.PrimaryWeaponId==TEXT("fighter"))
    {Error=TEXT("人形伙伴不能装备机甲或战斗机。");return false;}
    const auto* D=FHeliosCharacterRegistry::FindCharacter(L.CharacterId);
    const auto* Form=D?D->FindForm(L.CharacterVariantId):nullptr;
    if(!D||!D->bCompanionSelectable||!D->bPresentationReady||!Form||!Form->bPresentationReady)
    {Error=TEXT("所选伙伴人物或形态尚未就绪。");return false;}
    const auto* Profile=Form->ActionProfile.LoadSynchronous();
    if(!Profile||Profile->CharacterId!=D->CharacterId||Profile->CharacterVariantId!=Form->VariantId)
    {Error=TEXT("人物动作资源无法加载：")+Form->ActionProfile.ToSoftObjectPath().ToString();return false;}
    const auto* Pair=Profile->FindWeapon(L.PrimaryWeaponId);
    if(!Pair||!Pair->bGeneratedAndValidated){Error=TEXT("所选人物与主武器的动作配装尚未就绪。");return false;}
    auto* Mesh=Pair->ThirdPersonMesh.LoadSynchronous();
    if(!Mesh||!Mesh->GetSkeleton()){Error=TEXT("伙伴模型无法加载：")+Pair->ThirdPersonMesh.ToSoftObjectPath().ToString();return false;}
    for(const FName Action:{FName("Idle"),FName("Run_Fwd"),FName("Down"),FName("Wake")})
    {
        const auto* Path=Pair->ThirdPersonClips.Find(Action);auto* Clip=Path?Path->LoadSynchronous():nullptr;
        if(!Clip||Clip->GetSkeleton()!=Mesh->GetSkeleton())
        {Error=TEXT("伙伴动作缺失或骨架不兼容：")+Action.ToString();return false;}
    }
    for(const auto& Entry:Pair->ThirdPersonClips)
    {
        auto* Clip=Entry.Value.LoadSynchronous();
        if(!Clip||Clip->GetSkeleton()!=Mesh->GetSkeleton())
        {Error=TEXT("主武器动作缺失或骨架不兼容：")+Entry.Key.ToString();return false;}
    }
    if(!Pair->ThirdPersonWeaponMesh.IsNull())
    {
        auto* WeaponMesh=Pair->ThirdPersonWeaponMesh.LoadSynchronous();
        if(!WeaponMesh||WeaponMesh->GetSkeleton()!=Mesh->GetSkeleton())
        {Error=TEXT("主武器模型缺失或骨架不兼容。");return false;}
    }
    const auto* Weapon=UHeliosWeaponRegistry::FindWeapon(L.PrimaryWeaponId);
    if(!Weapon||!Weapon->WeaponClass){Error=TEXT("所选主武器的战斗类无法加载。");return false;}
    Error.Empty();return true;
}
FHeliosLoadout FromLegacyKind(EHeliosPetKind K)
{
    if(K==EHeliosPetKind::SwordDog){auto L=FHeliosLoadout::MakeDefault();L.CharacterId=TEXT("sword_dog");L.PrimaryWeaponId=NAME_None;return L;}
    const TCHAR* Id=K==EHeliosPetKind::SunShangXiang?TEXT("cfm_sunshangxiang"):K==EHeliosPetKind::ZhongXiaoKui?TEXT("cfm_zhongxiaokui"):K==EHeliosPetKind::BuLianShiP90?TEXT("cfm_bulianshi_p90"):K==EHeliosPetKind::WangZheKuiThunder?TEXT("cfm_wangzhekui_thunder"):K==EHeliosPetKind::YunSummerMegatron?TEXT("cfm_yun_summer_megatron"):TEXT("cfm_chan_zeus");
    return FHeliosLoadout::FromLegacyWeapon(Id);
}
EHeliosPetKind KindForLoadout(const FHeliosLoadout& L)
{
    if(L.CharacterId==TEXT("sword_dog"))return EHeliosPetKind::SwordDog;
    if(L.CharacterId==TEXT("cfm_sunshangxiang"))return EHeliosPetKind::SunShangXiang;
    if(L.CharacterId==TEXT("cfm_zhongxiaokui"))return EHeliosPetKind::ZhongXiaoKui;
    if(L.CharacterId==TEXT("cfm_bulianshi"))return EHeliosPetKind::BuLianShiP90;
    if(L.CharacterId==TEXT("cfm_wangzhekui"))return EHeliosPetKind::WangZheKuiThunder;
    if(L.CharacterId==TEXT("cfm_yun_summer"))return EHeliosPetKind::YunSummerMegatron;
    return EHeliosPetKind::ChanZeus;
}
}
void UHeliosPetSession::ResetSnapshots(){Snapshots.SetNum(4);for(auto& S:Snapshots)S.bValid=false;bHasSnapshot=false;}
void UHeliosPetSession::NormalizeLegacySelection()
{
    SelectedKinds.SetNum(4);Snapshots.SetNum(4);
    if(SelectedLoadouts.Num()!=4){SelectedLoadouts.SetNum(4);for(int32 I=0;I<4;++I)SelectedLoadouts[I]=HeliosPetLoadouts::FromLegacyKind(SelectedKinds[I]);}
    if(SelectedKind!=SelectedKinds[0]){SelectedKinds[0]=SelectedKind;SelectedLoadouts[0]=HeliosPetLoadouts::FromLegacyKind(SelectedKind);}
    for(int32 I=0;I<4;++I)
    {
        if(SelectedKinds[I]==EHeliosPetKind::UltimatumGR)SelectedKinds[I]=EHeliosPetKind::ChanZeus;
        if(HeliosPetLoadouts::KindForLoadout(SelectedLoadouts[I])!=SelectedKinds[I])SelectedLoadouts[I]=HeliosPetLoadouts::FromLegacyKind(SelectedKinds[I]);
        SelectedLoadouts[I]=FHeliosLoadout::ResolveLegacyVariant(SelectedLoadouts[I],GetGameInstance(),I);SelectedKinds[I]=HeliosPetLoadouts::KindForLoadout(SelectedLoadouts[I]);
    }
    SelectedKind=SelectedKinds[0];
}
EHeliosPetKind UHeliosPetSession::GetKind(int32 Slot)const{return SelectedKinds.IsValidIndex(Slot)?SelectedKinds[Slot]:EHeliosPetKind::SwordDog;}
FHeliosLoadout UHeliosPetSession::GetLoadout(int32 Slot)const{return SelectedLoadouts.IsValidIndex(Slot)?SelectedLoadouts[Slot]:HeliosPetLoadouts::FromLegacyKind(GetKind(Slot));}
void UHeliosPetSession::Select(EHeliosPetKind Kind){SetCount(1);SelectSlot(0,Kind);}
void UHeliosPetSession::SetCount(int32 Count){SelectedCount=FMath::Clamp(Count,0,4);NormalizeLegacySelection();SaveSelection();}
void UHeliosPetSession::SelectSlot(int32 Slot,EHeliosPetKind Kind)
{
    if(Slot<0||Slot>=SelectedCount)return;if(Kind==EHeliosPetKind::UltimatumGR)Kind=EHeliosPetKind::ChanZeus;if(!FHeliosPetDefinition::Find(Kind))return;
    SelectLoadout(Slot,FHeliosLoadout::ResolveLegacyVariant(HeliosPetLoadouts::FromLegacyKind(Kind),GetGameInstance(),Slot));
}
bool UHeliosPetSession::SelectLoadout(int32 Slot,const FHeliosLoadout& L)
{
    LastSelectionError.Empty();
    if(Slot<0||Slot>=SelectedCount){LastSelectionError=TEXT("请先选择伙伴人数和有效伙伴槽。");return false;}
    if(!FHeliosLoadout::Validate(L,LastSelectionError))return false;
    const auto Candidate=FHeliosLoadout::ResolveLegacyVariant(L,GetGameInstance(),Slot);
    if(!HeliosPetLoadouts::ValidatePresentation(Candidate,LastSelectionError))return false;
    NormalizeLegacySelection();const auto Previous=SelectedLoadouts[Slot];const auto PreviousKind=SelectedKinds[Slot];
    SelectedLoadouts[Slot]=Candidate;SelectedKinds[Slot]=HeliosPetLoadouts::KindForLoadout(Candidate);SelectedKind=SelectedKinds[0];
    if(!SaveSelection(SelectionSavePathOverride.IsEmpty()?PetSelectionPath():SelectionSavePathOverride))
    {SelectedLoadouts[Slot]=Previous;SelectedKinds[Slot]=PreviousKind;SelectedKind=SelectedKinds[0];LastSelectionError=TEXT("伙伴配装存档写入失败，已保留原配装。");return false;}
    return true;
}
bool UHeliosPetSession::SelectCharacterSlot(int32 Slot,FName CharacterId,FName VariantId)
{
    auto L=GetLoadout(Slot);const bool WasDog=L.CharacterId==TEXT("sword_dog"),SameCharacter=L.CharacterId==CharacterId;
    L.CharacterId=CharacterId;if(!SameCharacter||!VariantId.IsNone())L.CharacterVariantId=VariantId;
    if(CharacterId==TEXT("sword_dog")){L.PrimaryWeaponId=L.SecondaryWeaponId=L.MeleeWeaponId=NAME_None;}
    else if(WasDog)L.PrimaryWeaponId=FHeliosLoadout::MakeDefault().PrimaryWeaponId;
    return SelectLoadout(Slot,L);
}
bool UHeliosPetSession::SelectWeaponSlot(int32 Slot,FName WeaponId){auto L=GetLoadout(Slot);if(L.CharacterId==TEXT("sword_dog")){LastSelectionError=TEXT("刀盾狗使用固定刀盾。");return false;}L.PrimaryWeaponId=WeaponId;return SelectLoadout(Slot,L);}
bool UHeliosPetSession::SaveSelection(const FString& Path)const
{
    auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("version"),3);J->SetNumberField(TEXT("count"),SelectedCount);TArray<TSharedPtr<FJsonValue>> Slots,Loadouts;
    for(int32 I=0;I<4;++I){auto L=GetLoadout(I);Slots.Add(MakeShared<FJsonValueNumber>(int32(HeliosPetLoadouts::KindForLoadout(L))));auto Item=MakeShared<FJsonObject>();HeliosLoadout::WriteJson(Item,L);Loadouts.Add(MakeShared<FJsonValueObject>(Item));}
    J->SetArrayField(TEXT("slots"),Slots);J->SetArrayField(TEXT("loadouts"),Loadouts);FString Text;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Text));IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    const FString Temp=Path+TEXT(".tmp");return FFileHelper::SaveStringToFile(Text,*Temp)&&IFileManager::Get().Move(*Path,*Temp,true,true);
}
void UHeliosPetSession::SaveSelection()const{SaveSelection(SelectionSavePathOverride.IsEmpty()?PetSelectionPath():SelectionSavePathOverride);}
bool UHeliosPetSession::LoadSelection(const FString& Path)
{
    FString Text;TSharedPtr<FJsonObject> J;if(!FFileHelper::LoadFileToString(Text,*Path)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),J)||!J.IsValid())return false;
    const TArray<TSharedPtr<FJsonValue>>* Slots=nullptr;const TArray<TSharedPtr<FJsonValue>>* Loadouts=nullptr;double Count=1;
    TArray<EHeliosPetKind> Kinds=SelectedKinds;Kinds.SetNum(4);TArray<FHeliosLoadout> Parsed;Parsed.SetNum(4);for(int32 I=0;I<4;++I)Parsed[I]=GetLoadout(I);
    auto ValidCount=[&](int32 N){return J->TryGetNumberField(TEXT("count"),Count)&&FMath::IsFinite(Count)&&Count==FMath::FloorToDouble(Count)&&Count>=0&&Count<=4&&N<=4&&N>=Count;};
    if(J->TryGetArrayField(TEXT("loadouts"),Loadouts))
    {if(!ValidCount(Loadouts->Num()))return false;for(int32 I=0;I<Loadouts->Num();++I){const TSharedPtr<FJsonObject>* Item=nullptr;if(!(*Loadouts)[I]->TryGetObject(Item)||!HeliosLoadout::ReadJson(*Item,Parsed[I]))return false;Kinds[I]=HeliosPetLoadouts::KindForLoadout(Parsed[I]);}}
    else if(J->TryGetArrayField(TEXT("slots"),Slots))
    {if(!ValidCount(Slots->Num()))return false;for(int32 I=0;I<Slots->Num();++I){double K;if(!(*Slots)[I]->TryGetNumber(K)||!FMath::IsFinite(K)||K!=FMath::FloorToDouble(K)||K<0||K>7)return false;Parsed[I]=HeliosPetLoadouts::FromLegacyKind(EHeliosPetKind(int32(K)));Kinds[I]=HeliosPetLoadouts::KindForLoadout(Parsed[I]);}}
    else
    {double K;if(!J->TryGetNumberField(TEXT("selected_kind"),K)||!FMath::IsFinite(K)||K!=FMath::FloorToDouble(K)||K<0||K>7)return false;Parsed[0]=HeliosPetLoadouts::FromLegacyKind(EHeliosPetKind(int32(K)));Kinds[0]=HeliosPetLoadouts::KindForLoadout(Parsed[0]);Count=1;}
    for(int32 I=0;I<4;++I)Parsed[I]=FHeliosLoadout::ResolveLegacyVariant(Parsed[I],GetGameInstance(),I);
    SelectedKinds=MoveTemp(Kinds);SelectedLoadouts=MoveTemp(Parsed);SelectedCount=int32(Count);SelectedKind=SelectedKinds[0];ResetSnapshots();return true;
}
void UHeliosPetSession::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);Collection.InitializeDependency<UHeliosYunSummerSession>();Snapshots.SetNum(4);NormalizeLegacySelection();LoadSelection(PetSelectionPath());bool Override=false;
    for(const auto& Pair:TArray<TPair<const TCHAR*,EHeliosPetKind>>{{TEXT("HeliosSunShangXiang"),EHeliosPetKind::SunShangXiang},{TEXT("HeliosZhongXiaoKui"),EHeliosPetKind::ZhongXiaoKui},{TEXT("HeliosUltimatum"),EHeliosPetKind::ChanZeus},{TEXT("HeliosChanZeus"),EHeliosPetKind::ChanZeus},{TEXT("HeliosBuLianShiP90"),EHeliosPetKind::BuLianShiP90}})if(FParse::Param(FCommandLine::Get(),Pair.Key)){SelectedKind=Pair.Value;Override=true;}
    if(Override){SelectedCount=1;SelectedKinds[0]=SelectedKind;SelectedLoadouts[0]=FHeliosLoadout::ResolveLegacyVariant(HeliosPetLoadouts::FromLegacyKind(SelectedKind),GetGameInstance(),0);}
}
