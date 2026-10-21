#include "Boss/HeliosUrbanBoss.h"
#include "Boss/HeliosUrbanHazard.h"
#include "Boss/HeliosBossAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/ConstructorHelpers.h"
#include "HeliosOutpost.h"

namespace
{
FVector FootOf(const APawn* Pawn)
{
    FVector Result=Pawn->GetActorLocation();
    if (const auto* Character=Cast<ACharacter>(Pawn)) Result.Z-=Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    return Result;
}
bool UrbanTarget(const APawn* Pawn)
{
    return IsValid(Pawn) && !Pawn->ActorHasTag(TEXT("Dead")) &&
        (Pawn->IsPlayerControlled() || Pawn->ActorHasTag(TEXT("HeliosPet")) || Pawn->ActorHasTag(TEXT("HeliosSummon")));
}
}
AHeliosUrbanBoss::AHeliosUrbanBoss()
{
    SkipEntranceForUrbanMidboss();
    Tags.AddUnique(TEXT("UrbanGiant"));
    Placeholder=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TemporaryGeometry"));
    Placeholder->SetupAttachment(GetRootComponent());
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    Placeholder->SetStaticMesh(Cylinder.Object);
    Placeholder->SetRelativeLocation(FVector(0,0,125));
    Placeholder->SetRelativeScale3D(FVector(2,2,5));
    Placeholder->SetCollisionProfileName(TEXT("Pawn"));
    Placeholder->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Placeholder->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    Placeholder->SetCanEverAffectNavigation(false);
    Label=CreateDefaultSubobject<UTextRenderComponent>(TEXT("TemporaryGeometryLabel"));
    Label->SetupAttachment(GetRootComponent()); Label->SetRelativeLocation(FVector(0,0,440));
    Label->SetText(FText::FromString(TEXT("BOSS PLACEHOLDER"))); Label->SetWorldSize(42);
    Label->SetHorizontalAlignment(EHTA_Center); Label->SetTextRenderColor(FColor::Yellow);
    HeldBall=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GiantBasketballProp"));
    HeldBall->SetupAttachment(GetRootComponent());
    static ConstructorHelpers::FObjectFinder<UStaticMesh> BallMesh(TEXT("/Game/UrbanChallenge/Meshes/UC_Basketball.UC_Basketball"));
    HeldBall->SetStaticMesh(BallMesh.Object); HeldBall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    HeldBall->SetCanEverAffectNavigation(false); HeldBall->SetRelativeScale3D(FVector(.64f));
    HeldBall->SetVisibility(false);
}
void AHeliosUrbanBoss::ApplyTypeDefaults()
{
    Super::ApplyTypeDefaults();
    if (auto* Giant=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/UrbanChallenge/Giant/SK_UrbanGiant.SK_UrbanGiant")))
    {
        GetMesh()->SetSkeletalMeshAsset(Giant);
        // Derive this asset's forward axis from its foot, instead of inheriting
        // the unrelated legacy mesh's -90 degree import correction.
        const FReferenceSkeleton& Ref=Giant->GetRefSkeleton();
        const auto RefPosition=[&Ref](const FName Bone)
        {
            int32 Index=Ref.FindBoneIndex(Bone);
            FTransform Pose=FTransform::Identity;
            while(Index!=INDEX_NONE) { Pose*=Ref.GetRefBonePose()[Index]; Index=Ref.GetParentIndex(Index); }
            return Pose.GetTranslation();
        };
        const FVector AuthoredForward=(RefPosition(TEXT("ball_l"))-RefPosition(TEXT("foot_l"))).GetSafeNormal2D();
        if(!AuthoredForward.IsNearlyZero()) GetMesh()->SetRelativeRotation(FRotator(0,-AuthoredForward.Rotation().Yaw,0));
        const float Height=Giant->GetBounds().BoxExtent.Z*2.f;
        GetMesh()->SetRelativeScale3D(FVector(500.f/FMath::Max(Height,1.f)));
        const float SoleOffset=(Giant->GetBounds().Origin.Z-Giant->GetBounds().BoxExtent.Z)*GetMesh()->GetRelativeScale3D().Z;
        GetMesh()->SetRelativeLocation(FVector(0,0,-GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-SoleOffset));
        HeldBall->SetRelativeScale3D(FVector(.24f*GetMesh()->GetRelativeScale3D().Z));
        GetMesh()->SetVisibility(true,true); GetMesh()->SetHiddenInGame(false,true);
        GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        UrbanCharacterHeight=Height*GetMesh()->GetRelativeScale3D().Z;
        bUrbanCharacterReady=true;
    }
}
void AHeliosUrbanBoss::LoadAnimationAssets()
{
    const auto Load=[](const TCHAR* Name)->UAnimSequence*
    {
        return LoadObject<UAnimSequence>(nullptr,*FString::Printf(TEXT("/Game/UrbanChallenge/Giant/Animations/A_UrbanGiant_%s.A_UrbanGiant_%s"),Name,Name));
    };
    IdleAnimation=Load(TEXT("Idle")); WalkAnimation=Load(TEXT("Walk")); RunAnimation=Load(TEXT("Run"));
    HitAnimation=Load(TEXT("Hit")); DeathAnimation=Load(TEXT("Death")); DribbleAnimation=Load(TEXT("Dribble"));
    BarrageAnimation=Load(TEXT("Barrage")); UrbanChargeWindup=Load(TEXT("ChargeWindup"));
    UrbanChargeRun=Load(TEXT("ChargeRun")); RuptureAnimation=Load(TEXT("Rupture"));
    GetMesh()->SetAnimInstanceClass(UHeliosBossAnimInstance::StaticClass());
}
int32 AHeliosUrbanBoss::GetUrbanAnimationCount() const
{
    int32 Count=0;
    for (const auto* Animation:{IdleAnimation.Get(),WalkAnimation.Get(),RunAnimation.Get(),HitAnimation.Get(),DeathAnimation.Get(),DribbleAnimation.Get(),BarrageAnimation.Get(),UrbanChargeWindup.Get(),UrbanChargeRun.Get(),RuptureAnimation.Get()})
        if(Animation) ++Count;
    return Count;
}
void AHeliosUrbanBoss::TickUrbanPresentation(float DeltaSeconds)
{
    if (!bUrbanCharacterReady) return;
    PresentationAge+=DeltaSeconds;
    UAnimSequence* Sequence=GetVelocity().Size2D()>25?WalkAnimation.Get():DribbleAnimation.Get();
    float Time=PresentationAge; bool Loop=true;
    if (IsEnemyDead()) { Sequence=DeathAnimation; DeathAge+=DeltaSeconds; Time=DeathAge; Loop=false; }
    else if(Skill==1) { Sequence=BarrageAnimation; Time=SkillAge; Loop=false; }
    else if(Skill==2)
    {
        Sequence=SkillAge<ChargeWarning?UrbanChargeWindup.Get():UrbanChargeRun.Get();
        Time=SkillAge<ChargeWarning?SkillAge/ChargeWarning*2.8f:(SkillAge-ChargeWarning)*1.8f; Loop=SkillAge>=ChargeWarning;
    }
    else if(Skill==3) { Sequence=RuptureAnimation; Time=SkillAge; Loop=false; }
    if(auto* Anim=Cast<UHeliosBossAnimInstance>(GetMesh()->GetAnimInstance())) Anim->SetTimelinePose(Sequence,Time,Loop,DeltaSeconds);
    HeldBall->SetVisibility(!IsEnemyDead() && (Skill==0 || (Skill==1 && SkillAge<3.2f)));
    if(HeldBall->IsVisible())
    {
        FVector Position=GetMesh()->GetSocketLocation(TEXT("UrbanBall_R"));
        if(Skill==1) Position=(Position+GetMesh()->GetSocketLocation(TEXT("UrbanBall_L")))*.5f;
        else
        {
            // The same 1.2-second authored trajectory drives the baked wrist pose.
            // During locomotion the right hand carries the ball at a fixed height.
            const float Scale=GetMesh()->GetRelativeScale3D().Z;
            const float Phase=FMath::Fmod(PresentationAge,1.2f)/1.2f;
            const float V=FMath::Abs(2.f*Phase-1.f);
            const float SourceHeight=GetVelocity().Size2D()>25.f?102.f:13.2f+84.f*(2.f*V-V*V);
            const auto Bounds=GetMesh()->GetSkeletalMeshAsset()->GetBounds();
            const float SoleHeight=Bounds.Origin.Z-Bounds.BoxExtent.Z;
            Position=FootOf(this)+(GetActorRightVector()*30.f+GetActorForwardVector()*22.f)*Scale;
            Position.Z+=(SourceHeight-SoleHeight)*Scale;
        }
        HeldBall->SetWorldLocation(Position); HeldBall->SetWorldRotation(FRotator(PresentationAge*170,PresentationAge*70,0));
    }
}
void AHeliosUrbanBoss::BeginPlay()
{
    Super::BeginPlay();
    GetMesh()->SetVisibility(bUrbanCharacterReady,true); GetMesh()->SetHiddenInGame(!bUrbanCharacterReady,true);
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if (auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/UrbanChallenge/Materials/M_UC_metal.M_UC_metal"))) Placeholder->SetMaterial(0,Material);
    // Retain the established projectile hit volume and movement capsule; only its rendering changes.
    Placeholder->SetVisibility(!bUrbanCharacterReady); Label->SetVisibility(!bUrbanCharacterReady);
    if (auto* AI=Cast<AAIController>(GetController()))
    {
        if (AI->GetBrainComponent()) AI->GetBrainComponent()->StopLogic(TEXT("Independent urban skill controller"));
        AI->StopMovement();
    }
    GetCharacterMovement()->MaxWalkSpeed=350;
    FString Text; TSharedPtr<FJsonObject> Config;
    if (FFileHelper::LoadFileToString(Text,*(FPaths::ProjectContentDir()/TEXT("UrbanChallenge/Data/boss.json"))) &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Config) && Config)
    {
        double Number;
        if (Config->TryGetNumberField(TEXT("basketball_warning_seconds"),Number)) BasketballWarning=FMath::Clamp(float(Number),2.5f,6.f);
        if (Config->TryGetNumberField(TEXT("charge_warning_seconds"),Number)) ChargeWarning=FMath::Clamp(float(Number),2.5f,6.f);
        if (Config->TryGetNumberField(TEXT("basketball_damage"),Number)) BasketballDamage=FMath::Clamp(float(Number),0.f,300.f);
        if (Config->TryGetNumberField(TEXT("charge_damage"),Number)) ChargeDamage=FMath::Clamp(float(Number),0.f,300.f);
    }
    UE_LOG(LogHeliosOutpost,Display,TEXT("URBAN_BOSS placeholder=%d health=%.0f independent_skills=1 character_height=%.1f animations=%d mesh_yaw=%.1f"),bUrbanCharacterReady?0:1,GetHealth(),UrbanCharacterHeight,GetUrbanAnimationCount(),GetMesh()->GetRelativeRotation().Yaw);
}
void AHeliosUrbanBoss::SpawnHazard(FVector Foot,bool bBasketball,float Radius,float Warning,float Damage)
{
    const FTransform Transform(Foot+FVector(0,0,5));
    auto* Hazard=GetWorld()->SpawnActorDeferred<AHeliosUrbanHazard>(AHeliosUrbanHazard::StaticClass(),Transform,this,nullptr);
    if (!Hazard) return;
    Hazard->Configure(this,bBasketball,Radius,Warning,Damage); Hazard->FinishSpawning(Transform); Hazards.Add(Hazard);
}
void AHeliosUrbanBoss::StartBasketballs(APawn* Target)
{
    Skill=1; SkillAge=0; ++BasketballCasts; bNextCharge=true;
    if (auto* AI=Cast<AAIController>(GetController())) AI->StopMovement();
    const FVector Foot=FootOf(Target);
    SetActorRotation((Target->GetActorLocation()-GetActorLocation()).GetSafeNormal2D().Rotation());
    // Lock the six visible centres now. No target-following reticles after warning.
    const FVector Offsets[]={FVector(0,0,0),FVector(650,0,0),FVector(-650,0,0),FVector(0,650,0),FVector(0,-650,0),FVector(480,480,0)};
    for (int32 Index=0;Index<UE_ARRAY_COUNT(Offsets);++Index)
    {
        FVector Point=Foot+Offsets[Index]; FHitResult Hit;
        Point.X=FMath::Clamp(Point.X,-5500.f,5500.f); Point.Y=FMath::Clamp(Point.Y,-4500.f,4500.f);
        FCollisionQueryParams Query(SCENE_QUERY_STAT(UrbanBombGround),false,this); Query.AddIgnoredActor(Target);
        // Descending missiles strike the upper surface of bridges; shelter below stays safe.
        if (!GetWorld()->LineTraceSingleByChannel(Hit,Point+FVector(0,0,2000),Point-FVector(0,0,1800),ECC_Visibility,Query)) continue;
        Point.Z=Hit.ImpactPoint.Z;
        SpawnHazard(Point,true,420,BasketballWarning+Index*.22f,BasketballDamage);
    }
    UE_LOG(LogHeliosOutpost,Display,TEXT("URBAN_BOSS basketball cast=%d warning=%.2f locked_centres=6"),BasketballCasts,BasketballWarning);
}
void AHeliosUrbanBoss::StartCharge(APawn* Target)
{
    Skill=2; SkillAge=0; Travel=0; ++ChargeCasts; bNextCharge=false; ChargeHitTargets.Reset();
    if (auto* AI=Cast<AAIController>(GetController())) AI->StopMovement();
    ChargeOrigin=GetActorLocation();
    ChargeDirection=(Target->GetActorLocation()-ChargeOrigin).GetSafeNormal2D();
    const float Length=FMath::Clamp(FVector::Dist2D(Target->GetActorLocation(),ChargeOrigin)+350.f,700.f,1800.f);
    ChargeEnd=ChargeOrigin+ChargeDirection*Length;
    // The five-metre giant stays in the open central court, clear of bridges.
    ChargeEnd.X=FMath::Clamp(ChargeEnd.X,-2900.f,2900.f); ChargeEnd.Y=FMath::Clamp(ChargeEnd.Y,-500.f,500.f);
    ChargeDirection=(ChargeEnd-ChargeOrigin).GetSafeNormal2D(); ChargeLength=FVector::Dist2D(ChargeOrigin,ChargeEnd);
    const FVector Foot=FootOf(this);
    const int32 Points=FMath::Max(2,FMath::CeilToInt(ChargeLength/300.f));
    // The actual sweep is a continuous capsule with half-width ChargeWidth.
    // Sampled circles of that same radius under-cover its sides between centres.
    // UC_WarningRing has a 0.93 inner radius and 96 sides: keep the whole damage
    // corridor inside the visible inner boundary, without widening actual damage.
    const float Spacing=ChargeLength/Points;
    const float WarningRadius=FMath::Sqrt(FMath::Square(ChargeWidth)+FMath::Square(Spacing*.5f))
        /(.93f*FMath::Cos(PI/96.f))+1.f;
    for (int32 Index=0;Index<=Points;++Index)
        SpawnHazard(Foot+ChargeDirection*(ChargeLength*Index/Points),false,WarningRadius,ChargeWarning+1.3f,0);
    SetActorRotation(ChargeDirection.Rotation());
    UE_LOG(LogHeliosOutpost,Display,TEXT("URBAN_BOSS charge cast=%d warning=%.2f length=%.0f half_width=%.0f marker_radius=%.2f marker_spacing=%.2f"),ChargeCasts,ChargeWarning,ChargeLength,ChargeWidth,WarningRadius,Spacing);
}
void AHeliosUrbanBoss::TickCharge(float DeltaSeconds)
{
    if (SkillAge<ChargeWarning) return;
    float Remaining=DeltaSeconds;
    while (Remaining>0 && Travel<ChargeLength)
    {
        const float Dt=FMath::Min(Remaining,1.f/60.f); Remaining-=Dt;
        const FVector Before=GetActorLocation(); FHitResult Hit;
        SetActorLocation(Before+ChargeDirection*FMath::Min(1400.f*Dt,ChargeLength-Travel),true,&Hit);
        const FVector After=GetActorLocation(); Travel+=FVector::Dist2D(Before,After);
        const FVector Floor=FootOf(this);
        for (TActorIterator<APawn> It(GetWorld());It;++It)
        {
            APawn* Pawn=*It; if (!UrbanTarget(Pawn) || ChargeHitTargets.Contains(Pawn)) continue;
            const FVector Foot=FootOf(Pawn);
            if (FMath::Abs(Foot.Z-Floor.Z)>100 || FMath::PointDistToSegment(FVector(Foot.X,Foot.Y,Before.Z),Before,After)>ChargeWidth) continue;
            FHitResult Cover; FCollisionQueryParams Query(SCENE_QUERY_STAT(UrbanChargeCover),false,this); Query.AddIgnoredActor(Pawn);
            if (GetWorld()->LineTraceSingleByChannel(Cover,Floor+FVector(0,0,70),Foot+FVector(0,0,80),ECC_Visibility,Query)) continue;
            ChargeHitTargets.Add(Pawn); ++ChargeContacts;
            UGameplayStatics::ApplyDamage(Pawn,ChargeDamage,GetInstigatorController(),this,nullptr);
        }
        if (Hit.bBlockingHit || FVector::Dist2D(Before,After)<.01f) { Travel=ChargeLength; break; }
    }
    if (Travel>=ChargeLength || SkillAge>=ChargeWarning+1.5f)
    {
        SpawnHazard(FootOf(this),false,650,2.5f,ChargeDamage*.45f);
        Skill=3; SkillAge=0;
    }
}
void AHeliosUrbanBoss::Tick(float DeltaSeconds)
{
    // Like E05's blended graph, this timeline must bypass single-node locomotion.
    // Actor health/death callbacks and character movement remain inherited.
    AShooterNPC::Tick(DeltaSeconds);
    TickUrbanPresentation(DeltaSeconds);
    if (IsEnemyDead()) { Placeholder->SetVisibility(false); Label->SetVisibility(false); return; }
    APawn* Target=UGameplayStatics::GetPlayerPawn(this,0);
    if (!UrbanTarget(Target)) { ClearUrbanSkills(); return; }
    Label->SetWorldRotation((Target->GetActorLocation()-Label->GetComponentLocation()).Rotation());
    Hazards.RemoveAll([](const auto& Hazard){return !IsValid(Hazard);});
    if (Skill)
    {
        SkillAge+=DeltaSeconds;
        if (Skill==2) TickCharge(DeltaSeconds);
        else if ((Skill==1 && SkillAge>=BasketballWarning+2.2f) || (Skill==3 && SkillAge>=3.f))
        { Skill=0; SkillAge=0; Cooldown=GetBossPhase()==2?6.f:8.f; }
        return;
    }
    Cooldown-=DeltaSeconds; MoveRefresh-=DeltaSeconds;
    if (Cooldown<=0) { if (bNextCharge) StartCharge(Target); else StartBasketballs(Target); return; }
    if (MoveRefresh<=0)
    {
        MoveRefresh=.6f;
        FVector Goal=Target->GetActorLocation(); Goal.X=FMath::Clamp(Goal.X,-2900.f,2900.f); Goal.Y=FMath::Clamp(Goal.Y,-500.f,500.f); Goal.Z=125;
        if (auto* AI=Cast<AAIController>(GetController())) AI->MoveToLocation(Goal,350,false,true,false,true);
    }
}
FString AHeliosUrbanBoss::GetUrbanSkillText() const
{
    if (Skill==1) return TEXT("Basketball barrage - leave the marked circles");
    if (Skill==2) return SkillAge<ChargeWarning ? TEXT("Charge warning - leave the marked corridor") : TEXT("Ground rupture - move sideways");
    if (Skill==3) return TEXT("Aftershock warning - leave the marked impact area");
    return TEXT("Urban Giant - basketball barrage and ground rupture");
}
float AHeliosUrbanBoss::GetUrbanWarningProgress() const
{
    return Skill==2?FMath::Clamp(SkillAge/ChargeWarning,0.f,1.f):Skill==1?FMath::Clamp(SkillAge/BasketballWarning,0.f,1.f):0;
}
void AHeliosUrbanBoss::ClearUrbanSkills()
{
    for (const auto& Hazard:Hazards) if (IsValid(Hazard)) Hazard->Destroy();
    Hazards.Reset(); Skill=0;
}
void AHeliosUrbanBoss::Die() { ClearUrbanSkills(); HeldBall->SetVisibility(false); Placeholder->SetCollisionEnabled(ECollisionEnabled::NoCollision); Super::Die(); }
void AHeliosUrbanBoss::EndPlay(const EEndPlayReason::Type Reason) { ClearUrbanSkills(); Super::EndPlay(Reason); }
