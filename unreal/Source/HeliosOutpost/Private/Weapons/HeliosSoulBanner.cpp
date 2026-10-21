#include "Weapons/HeliosSoulBanner.h"
#include "Audio/HeliosAudio.h"
#include "Combat/HeliosCombatTempoSettings.h"
#include "Weapons/HeliosWeaponDamageSettings.h"
#include "Combat/CombatDamageSettings.h"
#include "Health/CreatureHealthSettings.h"
#include "Pets/HeliosPetDog.h"
#include "Pets/HeliosMeleeRules.h"
#include "Navigation/PathFollowingComponent.h"
#include "Weapons/HeliosFirstPersonPresentationComponent.h"
#include "Player/HeliosArenaCharacter.h"
#include "Weapons/HeliosWeaponHolderContext.h"
#include "Enemies/HeliosEnemyActor.h"
#include "AI/ShooterAIController.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "ProceduralMeshComponent.h"
#include "Sound/SoundBase.h"

namespace {
template<class T> T* Soul(const TCHAR* Name)
{
    const FString N(Name);const bool Xian=N.StartsWith(TEXT("SM_"))||N==TEXT("SK_SoulBanner")||N==TEXT("SK_SoulWarrior")||N==TEXT("SK_SoulMage")||N.StartsWith(TEXT("A_SoulWarrior_"))||N.StartsWith(TEXT("A_SoulMage_"));
    return LoadObject<T>(nullptr,*(FString(TEXT("/Game/Helios/Weapons/SoulBanner/"))+(Xian?TEXT("Xian/"):TEXT(""))+N));
}
void Cosmetic(UPrimitiveComponent* C){C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetGenerateOverlapEvents(false);C->SetCanEverAffectNavigation(false);C->SetCastShadow(false);}
void Sound(UWorld* W,const FVector& P,const TCHAR* Name,float Volume=.65f){HeliosAudio::PlaySoundAtLocation(W,Soul<USoundBase>(Name),P,Volume);}
bool Friendly(const AActor* A){return A&&(A->ActorHasTag(TEXT("Player"))||A->ActorHasTag(TEXT("Friendly"))||A->ActorHasTag(TEXT("HeliosPet"))||A->ActorHasTag(TEXT("HeliosSummon")));}
FActorSpawnParameters Spawn(APawn* Owner){FActorSpawnParameters P;P.Owner=Owner;P.Instigator=Owner;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;return P;}
float ViewDistance(UWorld* W,const FVector& P){auto* PC=UGameplayStatics::GetPlayerController(W,0);FVector Eye;FRotator R;if(!PC)return 0;PC->GetPlayerViewPoint(Eye,R);return FVector::Dist(Eye,P);}
// Stop at the first holder. A companion's outer owner can be the player, but
// its weapon, summoned soldiers and projectiles still belong to that companion.
AActor* SoulHolder(AActor* Source)
{
    for(AActor* Actor=Source;Actor;Actor=Actor->GetOwner())
    {
        if(Actor->FindComponentByClass<UHeliosSoulArmy>())return Actor;
        if(Cast<APawn>(Actor)&&!Actor->ActorHasTag(TEXT("HeliosSummon")))return Actor;
    }
    return nullptr;
}
FName SoulHolderTag(const AActor* Holder)
{return Holder?FName(*FString::Printf(TEXT("SoulHolder_%u"),Holder->GetUniqueID())):NAME_None;}
}

UHeliosSoulBannerDefinition::UHeliosSoulBannerDefinition(){WeaponId=TEXT("soulbanner");DisplayName=FText::FromString(TEXT("万魂幡"));MagazineSize=1;ReloadSeconds=0;Range=400;Damage=100;Recoil=0;}
UHeliosSoulArmy::UHeliosSoulArmy(){PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickInterval=.15f;}
UHeliosSoulArmy* UHeliosSoulArmy::Get(AActor* Player,bool Create)
{
    if(!Player)return nullptr;
    auto* Army=Player->FindComponentByClass<UHeliosSoulArmy>();
    if(!Army&&Create){Army=NewObject<UHeliosSoulArmy>(Player);Army->RegisterComponent();}
    return Army;
}
float UHeliosSoulArmy::Cooldown() const{return GetWorld()?FMath::Max(0.f,ReadyAt-GetWorld()->GetTimeSeconds()):0;}
int32 UHeliosSoulArmy::FlagCount() const{int32 N=0;for(auto F:Flags)if(IsValid(F)&&!F->IsActorBeingDestroyed())++N;return N;}
int32 UHeliosSoulArmy::SoldierCount() const{int32 N=0;for(auto F:Flags)if(IsValid(F))for(auto S:F->Soldiers)if(IsValid(S)&&S->Alive())++N;return N;}
FText UHeliosSoulArmy::Status() const
{
    FString S=FString::Printf(TEXT("旗阵 %d / 10   ·   阴兵 %d / 30   ·   %s"),FlagCount(),SoldierCount(),Cooldown()>0?*FString::Printf(TEXT("召唤 %.1fs"),Cooldown()):TEXT("召唤就绪"));
    if(GetWorld()->GetTimeSeconds()<MessageUntil)S+=TEXT("   ·   ")+Message;
    return FText::FromString(S);
}
bool UHeliosSoulArmy::FindPlacement(FVector& Ground,TArray<FVector>& Slots) const
{
    auto* P=Cast<APawn>(GetOwner());if(!P||HeliosWeaponHolderContext::IsDead(P))return false;
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());if(!Nav)return false;
    const FVector Forward=FRotator(0,HeliosWeaponHolderContext::ViewRotation(P).Yaw,0).Vector();
    FVector Desired=P->GetActorLocation()+Forward*210;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(SoulPlacement),false,P);
    for(TActorIterator<APawn> S(GetWorld());S;++S)if(Friendly(*S))Q.AddIgnoredActor(*S);
    FHitResult Wall,Floor;
    if(GetWorld()->LineTraceSingleByChannel(Wall,HeliosWeaponHolderContext::ViewLocation(P),Desired,ECC_Visibility,Q)){UE_LOG(LogTemp,Display,TEXT("SOUL_PLACEMENT obstruction %s"),*GetNameSafe(Wall.GetActor()));return false;}
    if(!GetWorld()->LineTraceSingleByChannel(Floor,Desired+FVector(0,0,100),Desired-FVector(0,0,350),ECC_Visibility,Q)||Floor.ImpactNormal.Z<.8f){UE_LOG(LogTemp,Display,TEXT("SOUL_PLACEMENT no_floor"));return false;}
    Ground=Floor.ImpactPoint;
    if(GetWorld()->OverlapBlockingTestByChannel(Ground+FVector(0,0,100),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(28,90),Q)){UE_LOG(LogTemp,Display,TEXT("SOUL_PLACEMENT occupied_base"));return false;}
    for(auto F:Flags)if(IsValid(F)&&FVector::DistSquared2D(F->GetActorLocation(),Ground)<FMath::Square(100.f))return false;
    Slots.Reset();
    for(int32 I=0;I<18 && Slots.Num()<3;++I)
    {
        const FVector C=Ground+Forward.RotateAngleAxis(I*60.f,FVector::UpVector)*(140+80*(I/6));
        FNavLocation N;if(!Nav->ProjectPointToNavigation(C,N,FVector(70,70,100)))continue;
        if(FMath::Abs(N.Location.Z-Ground.Z)>80)continue;
        FVector Center=N.Location+FVector(0,0,90);
        bool Close=false;for(auto& S:Slots)Close|=FVector::DistSquared2D(S,Center)<FMath::Square(90.f);if(Close)continue;
        if(GetWorld()->OverlapBlockingTestByChannel(Center,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(32,82),Q))continue;
        UNavigationPath* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),P->GetActorLocation(),N.Location,P);
        if(!Path||!Path->IsValid()||Path->IsPartial())continue;
        Slots.Add(Center);
    }
    if(Slots.Num()!=3)UE_LOG(LogTemp,Display,TEXT("SOUL_PLACEMENT insufficient_slots %d ground %s"),Slots.Num(),*Ground.ToString());
    return Slots.Num()==3;
}
bool UHeliosSoulArmy::Plant(const UHeliosSoulBannerDefinition* Settings)
{
    if(!Settings||Cooldown()>0)return false;
    FVector Ground;TArray<FVector> Slots;
    if(!FindPlacement(Ground,Slots)){Message=TEXT("此处无法布阵，请移至开阔地面");MessageUntil=GetWorld()->GetTimeSeconds()+2;return false;}
    auto* P=Cast<APawn>(GetOwner());auto* F=GetWorld()->SpawnActor<AHeliosSoulFlag>(Ground,FRotator(0,HeliosWeaponHolderContext::ViewRotation(P).Yaw,0),Spawn(P));
    if(!F)return false;
    F->Initialize(this,Settings,Slots);
    if(F->Soldiers.Num()!=3){F->Destroy();return false;}
    Flags.RemoveAll([](const auto& Item){return !IsValid(Item.Get())||Item->IsActorBeingDestroyed();});
    while(Flags.Num()>=FMath::Max(1,Settings->MaxFlags)){Flags[0]->Dismiss();Flags.RemoveAt(0);}
    Flags.Add(F);ReadyAt=GetWorld()->GetTimeSeconds()+Settings->CooldownSeconds;
    return true;
}
void UHeliosSoulArmy::ObservePlayerDamage(AHeliosEnemyActor* Enemy,AController* InstigatorController,AActor* Causer)
{
    if(Causer&&(Causer->ActorHasTag(TEXT("HeliosSummon"))||Causer->ActorHasTag(TEXT("HeliosPet"))))return;
    const auto* Player=Cast<APawn>(GetOwner());
    if(Player&&InstigatorController&&InstigatorController==Player->GetController())
    {RecentDamageTarget=Enemy;RecentDamageTime=GetWorld()->GetTimeSeconds();}
}
void UHeliosSoulArmy::TrackTrail(AHeliosSoulEffect* Effect)
{
    if(!IsValid(Effect))return;
    OwnedTrails.RemoveAll([](const auto& E){return !E.IsValid()||E->IsActorBeingDestroyed();});
    OwnedTrails.AddUnique(Effect);
}
void UHeliosSoulArmy::TrackBurst(UNiagaraComponent* Effect)
{
    if(!IsValid(Effect)||!GetOwner())return;
    const FName HolderTag=SoulHolderTag(GetOwner());
    // AutoRelease may reuse the same component for another holder. Replace the
    // lease tag so an old weak reference cannot stop its next owner's effect.
    Effect->ComponentTags.RemoveAll([](FName Tag){return Tag.ToString().StartsWith(TEXT("SoulHolder_"));});
    Effect->ComponentTags.AddUnique(TEXT("SoulCosmetic"));Effect->ComponentTags.AddUnique(HolderTag);
    OwnedBursts.RemoveAll([HolderTag](const auto& E){return !E.IsValid()||!E->IsActive()||!E->ComponentHasTag(HolderTag);});
    OwnedBursts.AddUnique(Effect);
}
void UHeliosSoulArmy::Clear()
{
    RecentDamageTarget.Reset();RecentDamageTime=-100.;PreferredTarget.Reset();ReadyAt=0;
    for(auto F:Flags)if(IsValid(F))F->Destroy();Flags.Reset();
    for(const auto& E:OwnedTrails)if(E.IsValid())E->Destroy();OwnedTrails.Reset();
    const FName HolderTag=SoulHolderTag(GetOwner());
    for(const auto& E:OwnedBursts)
        if(E.IsValid()&&!HolderTag.IsNone()&&E->ComponentHasTag(HolderTag))E->DeactivateImmediate();
    OwnedBursts.Reset();
}
void UHeliosSoulArmy::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* F)
{
    Super::TickComponent(Dt,Type,F);auto* P=Cast<APawn>(GetOwner());if(!P||HeliosWeaponHolderContext::IsDead(P)){Clear();return;}
    Flags.RemoveAll([](const auto& Item){return !IsValid(Item.Get())||Item->IsActorBeingDestroyed();});
    FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(SoulFocus),false,P);
    FVector Eye=HeliosWeaponHolderContext::ViewLocation(P);
    if(GetWorld()->LineTraceSingleByChannel(Hit,Eye,Eye+HeliosWeaponHolderContext::ViewRotation(P).Vector()*3000,ECC_Visibility,Q))
        if(auto* E=Cast<AHeliosEnemyActor>(Hit.GetActor()))PreferredTarget=E;
}
void UHeliosSoulArmy::EndPlay(EEndPlayReason::Type R){Clear();Super::EndPlay(R);}

AHeliosSoulBanner::AHeliosSoulBanner(){MeshAssetPath=TEXT("/Game/Helios/Weapons/SoulBanner/Xian/SM_SoulBanner");PrimaryActorTick.TickGroup=TG_PostPhysics;}
void AHeliosSoulBanner::ApplyDefinition(const UHeliosWeaponDefinition* D)
{
    Super::ApplyDefinition(D);D=ActiveDefinition;Settings=const_cast<UHeliosSoulBannerDefinition*>(Cast<UHeliosSoulBannerDefinition>(D));if(!Settings){Settings=NewObject<UHeliosSoulBannerDefinition>(this);ApplyWeaponDamageSettings(this,Settings);}
    Animations.Reset();for(auto& A:Settings->Swings)Animations.Add(A.LoadSynchronous());PlantPose=Settings->PlantAnimation.LoadSynchronous();
}
bool AHeliosSoulBanner::CanAct() const{auto* P=Cast<APawn>(GetOwner());return P&&!HeliosWeaponHolderContext::IsDead(P)&&!IsHidden()&&!GetWorld()->IsPaused();}
void AHeliosSoulBanner::StartFiring(){if(!CanAct()||State==2)return;bHeld=true;if(State==0)Swing();}
void AHeliosSoulBanner::Swing(){if(!Settings)return;if(IdleAge>.9f)Combo=0;State=1;Age=0;IdleAge=0;TrailAge=0;ActiveTrail=nullptr;HitActors.Reset();++AttackSerial;Sound(GetWorld(),GetActorLocation(),TEXT("SFX_SoulSwing"),.5f);}
void AHeliosSoulBanner::StartSecondaryFire()
{
    if(!CanAct()||State!=0){UE_LOG(LogTemp,Display,TEXT("SOUL_SECONDARY busy state=%d can_act=%d"),State,CanAct());return;}auto* A=UHeliosSoulArmy::Get(GetOwner());if(!A||A->Cooldown()>0)return;
    FVector G;TArray<FVector> S;if(!A->FindPlacement(G,S)){A->Message=TEXT("此处无法布阵，请移至开阔地面");A->MessageUntil=GetWorld()->GetTimeSeconds()+2;return;}
    State=2;Age=0;bHeld=false;Sound(GetWorld(),GetActorLocation(),TEXT("SFX_SoulCharge"),.45f);
}
void AHeliosSoulBanner::CancelReload(){State=0;bHeld=false;Age=0;ClothImpulse=0;if(IsValid(ActiveTrail))ActiveTrail->Destroy();ActiveTrail=nullptr;if(auto* P=Cast<APawn>(GetOwner()))if(HeliosWeaponHolderContext::Presentation(P))HeliosWeaponHolderContext::Presentation(P)->SetMeleePose(nullptr,0);}
void AHeliosSoulBanner::DeactivateWeapon(){CancelReload();Super::DeactivateWeapon();}
void AHeliosSoulBanner::Sweep(float From,float To)
{
    auto* P=Cast<APawn>(GetOwner());if(!P)return;
    const int32 Steps=FMath::Max(1,FMath::CeilToInt((To-From)/.035f));
    for(int32 I=0;I<=Steps;++I)
    {
        const float Phase=FMath::Lerp(From,To,float(I)/Steps);if(Phase<.22f||Phase>.65f)continue;
        const float T=(Phase-.22f)/.43f;
        FRotator Aim=HeliosWeaponHolderContext::ViewRotation(P);if(Combo<2)Aim.Yaw+=FMath::Lerp(Combo==0?-65.f:65.f,Combo==0?65.f:-65.f,T);else Aim.Pitch+=FMath::Lerp(45.f,-35.f,T);
        const FVector Eye=HeliosWeaponHolderContext::ViewLocation(P);const FVector End=Eye+Aim.Vector()*Settings->Range;
        FCollisionQueryParams Q(SCENE_QUERY_STAT(SoulMelee),false,P);Q.AddIgnoredActor(this);
        for(TActorIterator<AHeliosSoulSkeleton> S(GetWorld());S;++S)Q.AddIgnoredActor(*S);
        TArray<FHitResult> Hits;GetWorld()->SweepMultiByChannel(Hits,Eye,End,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(28),Q);
        for(const auto& H:Hits)
        {
            auto* E=Cast<AHeliosEnemyActor>(H.GetActor());if(!E||E->IsEnemyDead()||HitActors.Contains(E))continue;
            FHitResult Block;if(GetWorld()->LineTraceSingleByChannel(Block,Eye,H.ImpactPoint,ECC_Visibility,Q)&&Block.GetActor()!=E)continue;
            HitActors.Add(E);UGameplayStatics::ApplyPointDamage(E,Settings->ComboDamage.IsValidIndex(Combo)?Settings->ComboDamage[Combo]:100,Aim.Vector(),H,P->GetController(),this,UDamageType::StaticClass());
            UHeliosSoulArmy::Get(P)->PreferredTarget=E;AHeliosSoulEffect::Burst(this,H.ImpactPoint,1,.55f,H.ImpactNormal,1.f+Combo*.18f);
            Sound(GetWorld(),H.ImpactPoint,TEXT("SFX_SoulImpact"),.48f+Combo*.07f);
            if(HeliosWeaponHolderContext::Presentation(P))HeliosWeaponHolderContext::Presentation(P)->ApplyMeleeImpact(.55f);
        }
    }
}
void AHeliosSoulBanner::Tick(float Dt)
{
    Super::Tick(Dt);if(!CanAct()||!Settings)return;auto* P=Cast<APawn>(GetOwner());auto* Pres=HeliosWeaponHolderContext::Presentation(P);auto* A=UHeliosSoulArmy::Get(P);
    const bool Ghost=A->FlagCount()>0;
    if(!bArtMaterialsReady){bArtMaterialsReady=true;GhostMaterials.Reset();auto* Mesh=GetFirstPersonMesh();for(int32 I=0;I<Mesh->GetNumMaterials();++I)if(auto* M=Mesh->CreateDynamicMaterialInstance(I))GhostMaterials.Add(M);}
    bGhost=Ghost;for(auto M:GhostMaterials)if(M)M->SetScalarParameterValue(TEXT("Ghost"),Ghost?1.f:0.f);
    ClothImpulse=FMath::FInterpTo(ClothImpulse,State==1?FMath::Sin(FMath::Clamp(Age/.55f,0.f,1.f)*PI)*1.4f:0.f,Dt,7.f);
    for(auto M:GhostMaterials)if(M)M->SetScalarParameterValue(TEXT("Swing"),ClothImpulse);
    float Old=Age;Age+=Dt;IdleAge+=Dt;
    if(State==1)
    {
        float Duration=Settings->ComboSeconds.IsValidIndex(Combo)?Settings->ComboSeconds[Combo]:.55f;
        if(Pres)Pres->SetMeleePose(Animations.IsValidIndex(Combo)?Animations[Combo]:nullptr,Age/Duration);
        Sweep(Old/Duration,Age/Duration);TrailAge+=Dt;
        if(Age/Duration>.2f&&Age/Duration<.7f&&Pres){
            if(!IsValid(ActiveTrail))ActiveTrail=AHeliosSoulEffect::Trail(this,Combo==2?48.f:34.f);
            const FVector Tip=Pres->GetMeleeTip();
            if(ActiveTrail)ActiveTrail->Sample(Tip,(GetFirstPersonMesh()->GetComponentLocation()-Tip).GetSafeNormal());
        }
        if(Age>=Duration){State=0;IdleAge=0;Combo=(Combo+1)%3;if(Pres)Pres->SetMeleePose(nullptr,0);if(bHeld)Swing();}
    }
    else if(State==2)
    {
        if(Pres)Pres->SetMeleePose(PlantPose,Age/Settings->PlantSeconds);
        if(Age>=Settings->PlantSeconds){A->Plant(Settings);State=0;if(Pres){Pres->SetMeleePose(nullptr,0);Pres->ApplyMeleeImpact(.8f);}}
    }
}

AHeliosSoulFlag::AHeliosSoulFlag()
{
    PrimaryActorTick.bCanEverTick=true;Banner=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Flag"));SetRootComponent(Banner);Cosmetic(Banner);
    Circle=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SoulFormation"));Circle->SetupAttachment(Banner);Cosmetic(Circle);
    Aura=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RisingSoulSilk"));Aura->SetupAttachment(Banner);Cosmetic(Aura);
}
void AHeliosSoulFlag::Initialize(UHeliosSoulArmy* InArmy,const UHeliosSoulBannerDefinition* S,const TArray<FVector>& Slots)
{
    Army=InArmy;Remaining=S->FlagSeconds;Radius=S->CombatRadius;AttackPresentation=S->AttackPresentation;
    if(auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))Nav->RegisterNavigationInvoker(this,Radius+500,Radius+1500);
    Banner->SetStaticMesh(Soul<UStaticMesh>(TEXT("SM_SoulBannerWorld")));Banner->SetCastShadow(true);Circle->SetStaticMesh(Soul<UStaticMesh>(TEXT("SM_SB_Formation")));Circle->SetRelativeLocation(FVector(0,0,2));
    Aura->SetStaticMesh(Soul<UStaticMesh>(TEXT("SM_SX_Aura")));Aura->SetMaterial(0,Soul<UMaterialInterface>(TEXT("M_SX_Aura")));AuraMaterial=Aura->CreateDynamicMaterialInstance(0);
    for(int32 I=0;I<Banner->GetNumMaterials();++I)if(auto* M=Banner->CreateDynamicMaterialInstance(I))Materials.Add(M);
    for(int32 I=0;I<Circle->GetNumMaterials();++I)if(auto* M=Circle->CreateDynamicMaterialInstance(I))Materials.Add(M);
    for(int32 I=0;I<Slots.Num();++I){auto* C=GetWorld()->SpawnActor<AHeliosSoulSkeleton>(Slots[I],FRotator(0,GetActorRotation().Yaw,0),Spawn(Cast<APawn>(Army->GetOwner())));if(C){C->Initialize(this,I);Soldiers.Add(C);}}
    AHeliosSoulEffect::Burst(this,GetActorLocation()+FVector(0,0,8),0,1.4f);Sound(GetWorld(),GetActorLocation(),TEXT("SFX_SoulPlant"));
}
void AHeliosSoulFlag::Tick(float Dt)
{
    Super::Tick(Dt);Age+=Dt;Remaining-=Dt;if(Remaining<=0){Dismiss();return;}
    const float Rise=FMath::Clamp(Age*2,0.f,1.f);Circle->SetRelativeScale3D(FVector(Rise));
    for(auto M:Materials)if(M)M->SetScalarParameterValue(TEXT("Fade"),Rise*FMath::Clamp(Remaining/1.5f,0.f,1.f));
    const float Distance=ViewDistance(GetWorld(),GetActorLocation());Aura->SetVisibility(Distance<2400);
    if(Distance<2400){Aura->SetRelativeRotation(FRotator(0,Age*35,0));if(AuraMaterial)AuraMaterial->SetScalarParameterValue(TEXT("Opacity"),(Age<1.6f?.7f:.20f)*FMath::Clamp(Remaining/1.5f,0.f,1.f));}
}
void AHeliosSoulFlag::Dismiss(){if(IsActorBeingDestroyed())return;AHeliosSoulEffect::Burst(this,GetActorLocation()+FVector(0,0,70),2,1);Destroy();}
void AHeliosSoulFlag::EndPlay(EEndPlayReason::Type R){if(auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))Nav->UnregisterNavigationInvoker(this);for(auto S:Soldiers)if(IsValid(S))S->Destroy();Soldiers.Reset();Super::EndPlay(R);}

AHeliosSoulSkeleton::AHeliosSoulSkeleton()
{
    PrimaryActorTick.bCanEverTick=true;AutoPossessAI=EAutoPossessAI::PlacedInWorldOrSpawned;AIControllerClass=AAIController::StaticClass();
    Tags.Add(TEXT("Friendly"));Tags.Add(TEXT("HeliosSummon"));
    GetCapsuleComponent()->InitCapsuleSize(30,84);GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);GetCapsuleComponent()->SetCanEverAffectNavigation(false);
    GetMesh()->SetRelativeLocation(FVector(0,0,-84));GetMesh()->SetRelativeRotation(FRotator(0,-90,0));Cosmetic(GetMesh());GetMesh()->SetCastShadow(true);
    GetMesh()->bEnableUpdateRateOptimizations=true;GetCharacterMovement()->MaxWalkSpeed=420;GetCharacterMovement()->bOrientRotationToMovement=true;bUseControllerRotationYaw=false;
    // Town has explicit supported agents; use its existing Default nav data while retaining our collision body.
    GetCharacterMovement()->SetUpdateNavAgentWithOwnersCollisions(false);
    GetCharacterMovement()->NavAgentProps.AgentRadius=38.f;GetCharacterMovement()->NavAgentProps.AgentHeight=192.f;GetCharacterMovement()->NavAgentProps.AgentStepHeight=40.f;
    Blade=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SoulWeapon"));Blade->SetupAttachment(GetMesh());Cosmetic(Blade);
    Charge=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GatheredSoul"));Charge->SetupAttachment(Blade);Cosmetic(Charge);Charge->SetVisibility(false);
}
void AHeliosSoulSkeleton::Initialize(AHeliosSoulFlag* F,int32 R)
{
    Flag=F;Role=R;
    GetCharacterMovement()->MaxWalkSpeed = UHeliosCombatTempoSettings::Resolve(this,
        EHeliosTempoFile::Movement, TEXT("万魂幡召唤物"), TEXT("移动"),
        GetCharacterMovement()->MaxWalkSpeed);
    AttackInterval = UHeliosCombatTempoSettings::Resolve(this, EHeliosTempoFile::Attack,
        TEXT("宠物与召唤物"), R==2?TEXT("骷髅法师攻击间隔"):TEXT("骷髅战士攻击间隔"),
        R==2?1.8f:1.2f);
    Health=UCreatureHealthSettings::Resolve(this,R==2?ECreatureHealthType::SoulMage:ECreatureHealthType::SoulWarrior);
    Decision=.15f*R;
    GetMesh()->SetSkeletalMesh(Soul<USkeletalMesh>(R==2?TEXT("SK_SoulMage"):TEXT("SK_SoulWarrior")));
    // Match the 168 cm collision body and the ordinary enemies' 160-185 cm stature.
    // Include each role's headwear in the authored bounds; keep the feet at the mesh origin.
    if(auto* Model=GetMesh()->GetSkeletalMeshAsset())
        GetMesh()->SetRelativeScale3D(FVector(168.f/FMath::Max(1.f,Model->GetImportedBounds().BoxExtent.Z*2.f)));
    GetMesh()->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    for(const TCHAR* N:{TEXT("Idle"),TEXT("Run"),TEXT("Attack"),TEXT("Spawn"),TEXT("Death"),TEXT("Hit")})
        Animations.Add(FName(N),Soul<UAnimSequence>(*(FString(R==2?TEXT("A_SoulMage_"):TEXT("A_SoulWarrior_"))+N)));
    if(R!=2)Animations.Add(TEXT("Attack2"),Soul<UAnimSequence>(TEXT("A_SoulWarrior_Attack2")));
    Blade->SetStaticMesh(Soul<UStaticMesh>(R==2?TEXT("SM_SoulStaff"):TEXT("SM_SoulBlade")));
    Blade->AttachToComponent(GetMesh(),FAttachmentTransformRules::SnapToTargetIncludingScale,TEXT("handslot_r"));
    Blade->SetRelativeRotation(FRotator(180,0,0));
    for(int32 I=0;I<GetMesh()->GetNumMaterials();++I)if(auto* M=GetMesh()->CreateDynamicMaterialInstance(I))SoulMaterials.Add(M);
    if(R==2){Charge->SetStaticMesh(Soul<UStaticMesh>(TEXT("SM_SX_SoulSpear")));Charge->SetMaterial(0,Soul<UMaterialInterface>(TEXT("M_SB_Core")));ChargeMaterial=Charge->CreateDynamicMaterialInstance(0);Charge->SetRelativeLocation(F->AttackPresentation.StaffTip);}
    Pose(TEXT("Spawn"),false);AHeliosSoulEffect::Burst(this,GetActorLocation()-FVector(0,0,75),0,.55f);
}
void AHeliosSoulSkeleton::Pose(FName N,bool Loop){if(CurrentPose==N)return;if(auto A=Animations.FindRef(N)){GetMesh()->PlayAnimation(A,Loop);CurrentPose=N;}}
bool AHeliosSoulSkeleton::Visible(AActor* Other) const
{
    if(!IsValid(Other))return false;FCollisionQueryParams Q(SCENE_QUERY_STAT(SoulSight),false,this);Q.AddIgnoredActor(GetOwner());
    for(TActorIterator<AHeliosSoulSkeleton> S(GetWorld());S;++S)Q.AddIgnoredActor(*S);
    FHitResult H;return !GetWorld()->LineTraceSingleByChannel(H,GetActorLocation()+FVector(0,0,25),Other->GetActorLocation(),ECC_Visibility,Q)||H.GetActor()==Other;
}
void AHeliosSoulSkeleton::Decide()
{
    if(!Flag||!Flag->Army)return;AHeliosEnemyActor* Best=nullptr;double BestScore=1.e30;
    const float Radius=Role==2?Flag->Radius:FHeliosPetSettings().CombatRadius;
    const bool OutsideLeash=Role!=2&&FVector::DistSquared2D(Flag->GetActorLocation(),GetActorLocation())>FMath::Square(Radius);
    for(TActorIterator<AHeliosEnemyActor> E(GetWorld());E;++E)
    {
        if(OutsideLeash||E->IsEnemyDead()||FVector::DistSquared2D(Flag->GetActorLocation(),E->GetActorLocation())>FMath::Square(Radius))continue;
        const float D=FVector::DistSquared2D(GetActorLocation(),E->GetActorLocation());
        auto* EnemyAI=Cast<AShooterAIController>(E->GetController());
        const bool Threat=EnemyAI&&(EnemyAI->GetCurrentTarget()==GetOwner()||EnemyAI->GetCurrentTarget()==this);
        const bool Recent=Flag->Army->RecentDamageTarget.Get()==*E&&GetWorld()->GetTimeSeconds()-Flag->Army->RecentDamageTime<=5.;
        const double Score=Role==2?D+(Flag->Army->PreferredTarget.Get()==*E?0:1.e8f):HeliosMelee::TargetScore(Recent,Threat,D);
        if(Score>=BestScore)continue;
        auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),GetActorLocation(),E->GetActorLocation(),this);
        if(!Path||!Path->IsValid()||Path->IsPartial())continue;Best=*E;BestScore=Score;
    }
    Target=Best;
    if(Role!=2&&Best){TrackMeleeTarget(0.f);return;}
    if(Role!=2)bMoveRequested=false;
    if(auto* AI=Cast<AAIController>(GetController()))
    {
        const float D=Best?FVector::Dist2D(GetActorLocation(),Best->GetActorLocation()):0;
        if(Best&&(D>(Role==2?1100.f:135.f)||!Visible(Best)))AI->MoveToLocation(Best->GetActorLocation(),Role==2?650.f:65.f,true,true,true,false,nullptr,false);
        else if(Best)AI->StopMovement();
        else AI->MoveToLocation(Flag->GetActorLocation()+FVector(160,0,0).RotateAngleAxis(Role*120,FVector::UpVector),60,true,true,true,false,nullptr,false);
    }
}
bool AHeliosSoulSkeleton::CanMeleeHit(AHeliosEnemyActor* Enemy) const
{
    const FHeliosPetSettings Rules;
    return IsValid(Flag)&&Enemy&&!Enemy->IsEnemyDead()
        &&HeliosMelee::InStrike(this,Enemy,Flag->GetActorLocation(),Rules.CombatRadius,Rules.MeleeRange)&&Visible(Enemy);
}
void AHeliosSoulSkeleton::CancelMeleeAttack()
{
    ClearAttackVisuals();
    AttackAge=-1;bCommitted=true;Target.Reset();CurrentPose=NAME_None;Decision=0;bMoveRequested=false;
    if(auto* AI=Cast<AAIController>(GetController()))AI->StopMovement();
}
void AHeliosSoulSkeleton::TrackMeleeTarget(float Dt)
{
    auto* Enemy=Target.Get();auto* AI=Cast<AAIController>(GetController());if(!Enemy||!AI)return;
    const float Range=FHeliosPetSettings().MeleeRange;
    const bool Clear=Visible(Enemy);const FVector Goal=Enemy->GetActorLocation();
    const float Acceptance=Clear?FMath::Max(30.f,Range*.45f):30.f;
    if(FVector::Dist2D(Goal,GetActorLocation())<=Acceptance){AI->StopMovement();bMoveRequested=false;}
    else if(!bMoveRequested||FVector::Dist2D(LastGoal,Goal)>=50.f||AI->GetMoveStatus()!=EPathFollowingStatus::Moving)
    {LastGoal=Goal;bMoveRequested=AI->MoveToLocation(Goal,Acceptance,true,true,true,false,nullptr,false)!=EPathFollowingRequestResult::Failed;}
    const FVector Offset=Goal-GetActorLocation();
    if(Clear&&Offset.SizeSquared2D()<=FMath::Square(Range*1.5f)&&!Offset.IsNearlyZero())
        SetActorRotation(FMath::RInterpTo(GetActorRotation(),Offset.GetSafeNormal2D().Rotation(),Dt,16.f));
}
void AHeliosSoulSkeleton::Hit()
{
    if(!Target.IsValid()||Target->IsEnemyDead()||!Visible(Target.Get()))return;
    const float D=FVector::Dist2D(GetActorLocation(),Target->GetActorLocation());
    if(Role==2)
    {
        if(D>1500)return;const FVector Origin=GetActorLocation()+FVector(0,0,40)+GetActorForwardVector()*50;
        auto* B=GetWorld()->SpawnActor<AHeliosSoulBolt>(Origin,FRotator::ZeroRotator,Spawn(Cast<APawn>(GetOwner())));
        if(B){B->Caster=this;B->Velocity=(Target->GetActorLocation()-Origin).GetSafeNormal()*1500;}
        if(ViewDistance(GetWorld(),Origin)<2500){AHeliosSoulEffect::Burst(this,Charge->GetComponentLocation(),3,.65f,GetActorForwardVector());Sound(GetWorld(),Origin,TEXT("SFX_SoulCast"),.23f);}
    }
    else if(CanMeleeHit(Target.Get()))
    {
        auto* Player=Cast<APawn>(GetOwner());UGameplayStatics::ApplyDamage(Target.Get(),UCombatDamageSettings::Resolve(this,ECombatDamageType::SoulWarrior),Player?Player->GetController():nullptr,this,UDamageType::StaticClass());
        if(auto* AI=Cast<AShooterAIController>(Target->GetController()))AI->ReportAwarenessThreat(this,GetActorLocation(),true);
        const FVector Impact=Target->GetActorLocation()+FVector(0,0,25);
        AHeliosSoulEffect::Burst(this,Impact,1,.9f,(Impact-GetActorLocation()).GetSafeNormal());
        AHeliosSoulEffect::Strike(this,Blade->GetComponentLocation(),Impact,AttackVariant%2==0);
        if(ViewDistance(GetWorld(),Impact)<1800)Sound(GetWorld(),Impact,TEXT("SFX_SoulImpact"),.16f);++HitsDelivered;
    }
}
float AHeliosSoulSkeleton::TakeDamage(float Amount,const FDamageEvent&,AController* IC,AActor* Causer)
{
    if(!Alive()||Friendly(Causer)||Friendly(IC?IC->GetPawn():nullptr))return 0;
    const float Applied=FMath::Min(Health,FMath::Max(0.f,Amount));Health-=Applied;
    if(Health<=0)Die();else if(AttackAge<0)Pose(TEXT("Hit"),false);return Applied;
}
void AHeliosSoulSkeleton::Die()
{
    ClearAttackVisuals();
    Health=0;Tags.AddUnique(TEXT("Dead"));Target.Reset();GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if(auto* AI=Cast<AAIController>(GetController()))AI->StopMovement();GetCharacterMovement()->DisableMovement();Pose(TEXT("Death"),false);
    AHeliosSoulEffect::Burst(this,GetActorLocation(),2,.6f);SetLifeSpan(1.4f);
}
void AHeliosSoulSkeleton::Tick(float Dt)
{
    Super::Tick(Dt);if(!Alive()){for(auto M:SoulMaterials)if(M)M->SetScalarParameterValue(TEXT("Fade"),FMath::Clamp(GetLifeSpan()/1.4f,0.f,1.f));return;}if(!IsValid(Flag)){ClearAttackVisuals();Destroy();return;}Age+=Dt;Cooldown-=Dt;
    const float CosmeticDistance=ViewDistance(GetWorld(),GetActorLocation());const int32 DesiredLOD=CosmeticDistance<450?1:CosmeticDistance<900?2:3;
    if(DesiredLOD!=VisualLOD){VisualLOD=DesiredLOD;GetMesh()->SetForcedLOD(VisualLOD);GetMesh()->SetCastShadow(VisualLOD<3);}
    for(auto M:SoulMaterials)if(M)M->SetScalarParameterValue(TEXT("Fade"),FMath::Clamp((Age-Role*.18f)/.8f,0.f,1.f));
    const float SpawnDelay=Role*.18f;GetMesh()->SetRelativeLocation(FVector(0,0,-84-110*(1-FMath::Clamp((Age-SpawnDelay)/.9f,0.f,1.f))));
    if(Age<1.15f+SpawnDelay)return;
    if(Role!=2)
    {
        if(Target.IsValid()&&(Target->IsEnemyDead()||FVector::DistSquared2D(Flag->GetActorLocation(),Target->GetActorLocation())>FMath::Square(FHeliosPetSettings().CombatRadius)))CancelMeleeAttack();
        if(!Target.IsValid()&&AttackAge>=0)CancelMeleeAttack();
        if(AttackAge<0){Decision-=Dt;if(Decision<=0){Decision=.5f;Decide();}}
        if(Target.IsValid())TrackMeleeTarget(Dt);
    }
    else {Decision-=Dt;if(Decision<=0){Decision=.55f;Decide();}}
    if(AttackAge>=0)
    {
        AttackAge+=Dt;UpdateAttackVisuals();if(!bCommitted&&AttackAge>=.38f){bCommitted=true;Hit();}
        if(AttackAge>=.95f){ClearAttackVisuals();AttackAge=-1;CurrentPose=NAME_None;if(Role!=2)Decision=0;}return;
    }
    if(Target.IsValid()&&!Target->IsEnemyDead()&&(Role==2?(Visible(Target.Get())&&FVector::Dist2D(GetActorLocation(),Target->GetActorLocation())<=1500.f):CanMeleeHit(Target.Get())))
    {
        if(Role==2)SetActorRotation(FRotator(0,(Target->GetActorLocation()-GetActorLocation()).Rotation().Yaw,0));
        if(Cooldown<=0)
        {
            ClearAttackVisuals();AttackAge=0;Cooldown=AttackInterval;bCommitted=false;++AttackVariant;
            const auto& V=Flag->AttackPresentation;FName Variant=V.MageAttackVariant;
            if(Role!=2)Variant=V.MeleeAttackVariants.IsEmpty()?FName(TEXT("Attack")):V.MeleeAttackVariants[(AttackVariant-1)%V.MeleeAttackVariants.Num()];
            Pose(Animations.Contains(Variant)?Variant:FName(TEXT("Attack")),false);
            if(Role!=2&&ViewDistance(GetWorld(),GetActorLocation())<1600)Sound(GetWorld(),GetActorLocation(),TEXT("SFX_SoulBlade"),.14f);return;
        }
    }
    Pose(GetVelocity().Size2D()>15?TEXT("Run"):TEXT("Idle"));
}

void AHeliosSoulSkeleton::ClearAttackVisuals()
{
    if(IsValid(AttackTrail))AttackTrail->Destroy();AttackTrail=nullptr;
    if(Charge)Charge->SetVisibility(false);
}
void AHeliosSoulSkeleton::UpdateAttackVisuals()
{
    if(!IsValid(Flag))return;const auto& V=Flag->AttackPresentation;
    const float Distance=ViewDistance(GetWorld(),GetActorLocation());
    if(Role==2)
    {
        const float Grow=FMath::Clamp(AttackAge/.38f,0.f,1.f);
        Charge->SetVisibility(AttackAge<.38f&&Distance<3000);
        Charge->SetRelativeScale3D(FVector(1.f+Grow*2.6f,1.f+Grow*2.6f,.35f+Grow*.65f));
        Charge->SetRelativeRotation(FRotator(0,AttackAge*460,0));
        if(ChargeMaterial)ChargeMaterial->SetScalarParameterValue(TEXT("Opacity"),Grow*.85f);
    }
    else if(AttackAge>=V.TrailStartSeconds&&AttackAge<=V.TrailEndSeconds&&Distance<3500)
    {
        if(!IsValid(AttackTrail))AttackTrail=AHeliosSoulEffect::Trail(this,V.TrailWidth);
        if(AttackTrail)AttackTrail->RibbonLifetime=FMath::Clamp(V.RibbonLifetimeSeconds,.05f,.5f);
        const FVector Tip=Blade->GetComponentTransform().TransformPosition(V.BladeTip);
        const FVector Base=Blade->GetComponentTransform().TransformPosition(V.BladeBase);
        if(AttackTrail)AttackTrail->Sample(Tip,(Base-Tip).GetSafeNormal());
    }
}

AHeliosSoulEffect::AHeliosSoulEffect()
{
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;
    Ribbon=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("SoulRibbon"));SetRootComponent(Ribbon);Cosmetic(Ribbon);
}
UNiagaraComponent* AHeliosSoulEffect::Burst(AActor* Source,const FVector& P,int32 K,float S,const FVector& Direction,float Strength)
{
    AActor* Holder=SoulHolder(Source);UWorld* W=Source?Source->GetWorld():nullptr;
    if(!W||!Holder)return nullptr;auto* PC=UGameplayStatics::GetPlayerController(W,0);FVector Eye;FRotator R;if(PC)PC->GetPlayerViewPoint(Eye,R);
    if(PC&&FVector::DistSquared(Eye,P)>FMath::Square(4500.f))return nullptr;
    const float Distance=ViewDistance(W,P);
    const TCHAR* Name=K==0?TEXT("NS_SB_Birth"):K==2?TEXT("NS_SB_Dissolve"):K==3?TEXT("NS_SX_Cast"):Distance>2200?TEXT("NS_SX_ImpactFar"):TEXT("NS_SB_Impact");
    if(auto* FX=Soul<UNiagaraSystem>(Name))
    {
        auto* C=UNiagaraFunctionLibrary::SpawnSystemAtLocation(W,FX,P,Direction.Rotation(),FVector(S*Strength),true,true,ENCPoolMethod::AutoRelease);
        if(C)UHeliosSoulArmy::Get(Holder)->TrackBurst(C);
        return C;
    }
    return nullptr;
}
AHeliosSoulEffect* AHeliosSoulEffect::Trail(AActor* Owner,float Width)
{
    if(!IsValid(Owner))return nullptr;
    FActorSpawnParameters Params;Params.Owner=Owner;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    if(ViewDistance(Owner->GetWorld(),Owner->GetActorLocation())>4000)return nullptr;
    auto* A=Owner->GetWorld()->SpawnActor<AHeliosSoulEffect>(FVector::ZeroVector,FRotator::ZeroRotator,Params);
    if(A){A->Width=Width;if(auto* Holder=SoulHolder(Owner))UHeliosSoulArmy::Get(Holder)->TrackTrail(A);}return A;
}
void AHeliosSoulEffect::BeginPlay(){Super::BeginPlay();Ribbon->SetMaterial(0,Soul<UMaterialInterface>(TEXT("M_SB_Ribbon")));SetLifeSpan(.35f);}
void AHeliosSoulEffect::Sample(const FVector& Tip,const FVector& Inward)
{
    if(!Points.IsEmpty()&&FVector::DistSquared(Points.Last().Tip,Tip)<4)return;
    // A teleport must never stretch a ribbon across the map.
    if(!Points.IsEmpty()&&FVector::DistSquared(Points.Last().Tip,Tip)>FMath::Square(350.f))Points.Reset();
    Points.Add({Tip,Inward.GetSafeNormal(),Age});if(Points.Num()>32)Points.RemoveAt(0);
    SetLifeSpan(RibbonLifetime+.08f);
}
void AHeliosSoulEffect::Tick(float Dt)
{
    Super::Tick(Dt);Age+=Dt;
    if(bStrike){if(StrikeMaterial)StrikeMaterial->SetScalarParameterValue(TEXT("Opacity"),FMath::Square(FMath::Clamp(1-Age/.22f,0.f,1.f)));return;}
    Points.RemoveAll([this](const FTrailPoint& P){return Age-P.Time>RibbonLifetime;});
    if(Points.Num()<2){Ribbon->ClearAllMeshSections();return;}
    TArray<FVector> V,N;TArray<int32> Indices;TArray<FVector2D> UV;TArray<FLinearColor> Colors;TArray<FProcMeshTangent> Tangents;
    // Fixed topology lets the render thread update a small vertex buffer instead
    // of recreating a scene proxy for every soldier on every animation frame.
    const int32 Steps=36;
    for(int32 I=0;I<=Steps;++I)
    {
        const float Position=float(I)/Steps*(Points.Num()-1);
        const int32 J=FMath::Min(FMath::FloorToInt(Position),Points.Num()-2);const float T=Position-J;
        const auto& A=Points[J];const auto& B=Points[J+1];
        const FVector TangentA=(B.Tip-Points[FMath::Max(0,J-1)].Tip)*.5f;
        const FVector TangentB=(Points[FMath::Min(Points.Num()-1,J+2)].Tip-A.Tip)*.5f;
        const FVector Tip=FMath::CubicInterp(A.Tip,TangentA,B.Tip,TangentB,T);
        const FVector Inward=FMath::Lerp(A.Inward,B.Inward,T).GetSafeNormal();
        const float Alpha=FMath::Square(FMath::Clamp(1-(Age-FMath::Lerp(A.Time,B.Time,T))/RibbonLifetime,0.f,1.f));
        V.Add(Tip);V.Add(Tip+Inward*Width);N.Add(FVector::UpVector);N.Add(FVector::UpVector);
        UV.Add(FVector2D(float(I)/Steps,0));UV.Add(FVector2D(float(I)/Steps,1));
        Colors.Add(FLinearColor(1,1,1,Alpha));Colors.Add(FLinearColor(1,1,1,Alpha));
        if(I){int32 Index=I*2;Indices.Append({Index-2,Index-1,Index,Index,Index-1,Index+1});}
    }
    if(Ribbon->GetNumSections()>0&&Ribbon->GetProcMeshSection(0)->ProcVertexBuffer.Num()==V.Num())Ribbon->UpdateMeshSection_LinearColor(0,V,N,UV,Colors,Tangents);
    else Ribbon->CreateMeshSection_LinearColor(0,V,Indices,N,UV,Colors,Tangents,false);
}

void AHeliosSoulEffect::Strike(AActor* Owner,const FVector& From,const FVector& To,bool Vertical)
{
    if(!Owner||ViewDistance(Owner->GetWorld(),To)>3500)return;
    auto* A=Trail(Owner,30);if(!A)return;A->bStrike=true;A->SetLifeSpan(.24f);
    A->StrikeMaterial=A->Ribbon->CreateDynamicMaterialInstance(0);
    const FVector Forward=(To-From).GetSafeNormal();const FVector Side=Vertical?FVector::UpVector:FVector::CrossProduct(Forward,FVector::UpVector).GetSafeNormal();
    TArray<FVector> V,N;TArray<int32> Indices;TArray<FVector2D> UV;TArray<FLinearColor> Colors;TArray<FProcMeshTangent> Tangents;
    for(int32 I=0;I<=20;++I)
    {
        const float T=I/20.f;const float Bend=FMath::Sin(T*PI)*55.f;
        const FVector P=FMath::Lerp(From,To,T)+Side*Bend;
        V.Add(P);V.Add(P-Side*(8+FMath::Sin(T*PI)*40));N.Add(FVector::UpVector);N.Add(FVector::UpVector);
        UV.Add(FVector2D(T,0));UV.Add(FVector2D(T,1));Colors.Add(FLinearColor::White);Colors.Add(FLinearColor::White);
        if(I){int32 J=I*2;Indices.Append({J-2,J-1,J,J,J-1,J+1});}
    }
    A->Ribbon->CreateMeshSection_LinearColor(0,V,Indices,N,UV,Colors,Tangents,false);
}
AHeliosSoulBolt::AHeliosSoulBolt(){PrimaryActorTick.bCanEverTick=true;Visual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SoulBolt"));SetRootComponent(Visual);Cosmetic(Visual);Helix=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SoulSpiral"));Helix->SetupAttachment(Visual);Cosmetic(Helix);}
void AHeliosSoulBolt::BeginPlay(){Super::BeginPlay();SetLifeSpan(1.1f);Visual->SetStaticMesh(Soul<UStaticMesh>(TEXT("SM_SX_SoulSpear")));Visual->SetMaterial(0,Soul<UMaterialInterface>(TEXT("M_SB_Core")));Visual->SetWorldScale3D(FVector(1.6f,1.6f,1.4f));Wake=AHeliosSoulEffect::Trail(this,24);Helix->SetStaticMesh(Soul<UStaticMesh>(TEXT("SM_SX_Aura")));Helix->SetMaterial(0,Soul<UMaterialInterface>(TEXT("M_SX_Aura")));Helix->SetRelativeScale3D(FVector(.22f,.22f,.36f));Helix->SetRelativeLocation(FVector(0,0,-30));}
void AHeliosSoulBolt::Tick(float Dt)
{
    Super::Tick(Dt);if(!Caster.IsValid()||!Caster->Alive()){Destroy();return;}Age+=Dt;Helix->SetRelativeRotation(FRotator(0,Age*720,0));Helix->SetVisibility(ViewDistance(GetWorld(),GetActorLocation())<2200);
    const FVector From=GetActorLocation(),To=From+Velocity*Dt;FCollisionQueryParams Q(SCENE_QUERY_STAT(SoulBolt),false,this);Q.AddIgnoredActor(GetOwner());
    for(TActorIterator<AHeliosSoulSkeleton> S(GetWorld());S;++S)Q.AddIgnoredActor(*S);
    FHitResult H;if(GetWorld()->SweepSingleByChannel(H,From,To,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(8),Q))
    {
        if(auto* E=Cast<AHeliosEnemyActor>(H.GetActor())){auto* P=Cast<APawn>(GetOwner());UGameplayStatics::ApplyPointDamage(E,UCombatDamageSettings::Resolve(this,ECombatDamageType::SoulMage),Velocity.GetSafeNormal(),H,P?P->GetController():nullptr,Caster.Get(),UDamageType::StaticClass());++Caster->HitsDelivered;if(auto* AI=Cast<AShooterAIController>(E->GetController()))AI->ReportAwarenessThreat(Caster.Get(),Caster->GetActorLocation(),true);}
        AHeliosSoulEffect::Burst(this,H.ImpactPoint,1,1.15f,H.ImpactNormal);Destroy();return;
    }
    SetActorLocation(To);SetActorRotation(FRotationMatrix::MakeFromZ(Velocity).Rotator());
    if(IsValid(Wake)){FVector Eye;FRotator View;if(auto* PC=UGameplayStatics::GetPlayerController(GetWorld(),0))PC->GetPlayerViewPoint(Eye,View);Wake->Sample(To,FVector::CrossProduct(Velocity.GetSafeNormal(),(Eye-To).GetSafeNormal()).GetSafeNormal());}
}
