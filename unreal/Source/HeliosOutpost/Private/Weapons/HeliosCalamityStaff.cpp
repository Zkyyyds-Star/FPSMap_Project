#include "Weapons/HeliosCalamityStaff.h"
#include "Audio/HeliosAudio.h"
#include "Weapons/HeliosCalamityWind.h"
#include "Weapons/HeliosWeaponDamageSettings.h"
#include "Combat/CombatDamageSettings.h"
#include "Weapons/HeliosFirstPersonPresentationComponent.h"
#include "Weapons/HeliosWeaponVFX.h"
#include "Player/HeliosArenaCharacter.h"
#include "Weapons/HeliosWeaponHolderContext.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Boss/HeliosEnemyBoss.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "UObject/UObjectIterator.h"
#include "Sound/SoundBase.h"

namespace {
template<class T> T* Art(const TCHAR* Name) { return LoadObject<T>(nullptr,*(FString(TEXT("/Game/Helios/Weapons/Calamity/"))+Name)); }
const TCHAR* SpellName(EHeliosCalamitySpell S) { static const TCHAR* Names[]={TEXT("地震"),TEXT("火球"),TEXT("龙卷风"),TEXT("毒药瓶")};return Names[static_cast<int32>(S)]; }
FLinearColor SpellColor(EHeliosCalamitySpell S) { const FLinearColor C[]={FLinearColor(1,.46f,.06f),FLinearColor(1,.075f,.008f),FLinearColor(.05f,.65f,1),FLinearColor(.22f,1,.025f)};return C[static_cast<int32>(S)]; }
bool Eligible(const AHeliosEnemyActor* E) { return IsValid(E)&&!E->IsEnemyDead()&&!E->IsActorBeingDestroyed()&&!E->ActorHasTag(TEXT("Friendly")); }
FActorSpawnParameters Params(AActor* Owner) { FActorSpawnParameters P;P.Owner=Owner;P.Instigator=Cast<APawn>(Owner);P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;return P; }
void PlayCalamitySound(const UObject* C,const FVector& P,const TCHAR* N,float V=.65f) { if(auto* S=Art<USoundBase>(N))HeliosAudio::PlaySoundAtLocation(C,S,P,V); }
void Burst(UHeliosCalamityState* State,const FVector& P,const TCHAR* N,float S=1)
{
    if(!State||!State->GetWorld())return;
    const auto* Player=Cast<AHeliosArenaCharacter>(State->GetOwner());
    if(Player&&FVector::DistSquared(Player->GetActorLocation(),P)>FMath::Square(10000.f))return;
    if(auto* C=UHeliosWeaponVFX::SpawnOneShot(State,Art<UNiagaraSystem>(N),P,FRotator::ZeroRotator,FVector(S),2.5f))
    { C->ComponentTags.AddUnique(TEXT("Calamity"));C->ComponentTags.AddUnique(FName(*State->GetOwner()->GetName())); }
}
UStaticMeshComponent* Part(AActor* A,const TCHAR* Mesh,const TCHAR* Material=nullptr)
{
    auto* M=NewObject<UStaticMeshComponent>(A);M->SetupAttachment(A->GetRootComponent());M->SetStaticMesh(Art<UStaticMesh>(Mesh));
    if(Material)M->SetMaterial(0,Art<UMaterialInterface>(Material));
    M->SetCollisionEnabled(ECollisionEnabled::NoCollision);M->SetGenerateOverlapEvents(false);M->SetCanEverAffectNavigation(false);
    M->SetCastShadow(false);M->SetCullDistance(11000);M->RegisterComponent();return M;
}
void Dynamic(UStaticMeshComponent* M,TArray<TObjectPtr<UMaterialInstanceDynamic>>& Out,const FLinearColor& Color)
{
    for(int32 I=0;I<M->GetNumMaterials();++I)if(auto* D=M->CreateDynamicMaterialInstance(I))
    { D->SetVectorParameterValue(TEXT("SpellColor"),Color);Out.Add(D); }
}
}

UHeliosCalamityDefinition::UHeliosCalamityDefinition()
{
    WeaponId=TEXT("calamity");DisplayName=FText::FromString(TEXT("灾厄王权"));MagazineSize=1;ReloadSeconds=0;
    Range=6000;RefireSeconds=1.1f;Recoil=.18f;Damage=100;Spells.SetNum(4);
    Spells[0].TelegraphSeconds=.8f;Spells[0].DissipateSeconds=.6f;Spells[2].TelegraphSeconds=.9f;Spells[2].DissipateSeconds=.8f;
    Spells[0].Radius=800;Spells[0].Damage=100;Spells[0].Duration=2.3f;
    Spells[1].Damage=280;Spells[1].MinimumDamage=100;Spells[1].Duration=1.2f;
    Spells[2].Damage=60;Spells[2].Duration=4;
    Spells[3].Damage=45;Spells[3].Duration=6;
    Spells[1].FlightSpeedMultiplier=Spells[3].FlightSpeedMultiplier=2;
}
UHeliosCalamityState::UHeliosCalamityState() { PrimaryComponentTick.bCanEverTick=true; }
UHeliosCalamityState* UHeliosCalamityState::Get(AActor* Player,bool Create)
{
    if(!Player)return nullptr;auto* C=Player->FindComponentByClass<UHeliosCalamityState>();
    if(!C&&Create){C=NewObject<UHeliosCalamityState>(Player);C->RegisterComponent();}return C;
}
bool UHeliosCalamityState::Deployed() const {return IsValid(Tower)&&!Tower->IsActorBeingDestroyed();}
float UHeliosCalamityState::Cooldown() const {return GetWorld()?FMath::Max(0.f,ReadyAt-GetWorld()->GetTimeSeconds()):0;}
FText UHeliosCalamityState::Status() const
{
    const float T=GetWorld()->GetTimeSeconds();
    FString S=Deployed()?FString::Printf(TEXT("法杖化塔 · %.1f 秒后回归"),FMath::Max(0.f,Settings->TowerSeconds+.85f-Tower->Age)):
        FString::Printf(TEXT("%s · 施法 %s · 防御塔 %s"),SpellName(LastSpell),CastReadyAt>T?*FString::Printf(TEXT("%.1fs"),CastReadyAt-T):TEXT("就绪"),Cooldown()>0?*FString::Printf(TEXT("%.1fs"),Cooldown()):TEXT("就绪"));
    if(T<MessageUntil)S+=TEXT(" · ")+Message;return FText::FromString(S);
}
bool UHeliosCalamityState::CanHit(const FVector& From,AHeliosEnemyActor* E) const
{
    if(!Eligible(E))return false;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(CalamityVisibility),false,GetOwner());
    if(Tower)Q.AddIgnoredActor(Tower);
    // Other living bodies never shield an entire area effect; solid world geometry does.
    for(TActorIterator<APawn> P(GetWorld());P;++P)if(*P!=E)Q.AddIgnoredActor(*P);
    FHitResult Hit;return !GetWorld()->LineTraceSingleByChannel(Hit,From,E->GetActorLocation(),ECC_Visibility,Q)||Hit.GetActor()==E;
}
bool UHeliosCalamityState::FindPlacement(FVector& Point) const
{
    auto* P=Cast<APawn>(GetOwner());if(!P||!Settings||HeliosWeaponHolderContext::IsDead(P))return false;
    const FVector Eye=HeliosWeaponHolderContext::ViewLocation(P),Direction=HeliosWeaponHolderContext::ViewRotation(P).Vector();
    FCollisionQueryParams Q(SCENE_QUERY_STAT(CalamityPlacement),false,P);FHitResult Hit,Floor;
    const FVector End=Eye+Direction*Settings->PlacementRange;
    const bool HitSomething=GetWorld()->LineTraceSingleByChannel(Hit,Eye,End,ECC_Visibility,Q);
    FVector Candidate=HitSomething?Hit.ImpactPoint:End;
    if(HitSomething&&Hit.ImpactNormal.Z>.8f)Floor=Hit;
    else if(!GetWorld()->LineTraceSingleByChannel(Floor,Candidate+FVector(0,0,20),Candidate-FVector(0,0,700),ECC_Visibility,Q))return false;
    Point=Floor.ImpactPoint+FVector(0,0,2);
    if(Floor.ImpactNormal.Z<.82f||FVector::Dist(Eye,Point)>Settings->PlacementRange||FVector::Dist2D(P->GetActorLocation(),Point)<120)return false;
    if(GetWorld()->LineTraceSingleByChannel(Hit,Eye,Point+FVector(0,0,90),ECC_Visibility,Q))return false;
    return !GetWorld()->OverlapBlockingTestByChannel(Point+FVector(0,0,125),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(65,120),Q);
}
bool UHeliosCalamityState::Deploy()
{
    if(!Settings||Deployed()||Cooldown()>0||GetWorld()->IsPaused())return false;
    FVector Ground;if(!FindPlacement(Ground)){Message=TEXT("请瞄准12米内无遮挡的开阔地面");MessageUntil=GetWorld()->GetTimeSeconds()+2;return false;}
    Tower=GetWorld()->SpawnActor<AHeliosCalamityTower>(Ground,FRotator::ZeroRotator,Params(GetOwner()));if(!Tower)return false;
    Tower->Initialize(this);PlayCalamitySound(this,Ground,TEXT("SFX_CS_Deploy"));return true;
}
bool UHeliosCalamityState::CastSpell(EHeliosCalamitySpell Spell,const FVector& Origin,const FVector& Target,float Scale)
{
    if(!Settings||Settings->Spells.Num()!=4||GetWorld()->IsPaused())return false;
    Effects.RemoveAll([](const auto& E){return !IsValid(E.Get())||E->IsActorBeingDestroyed();});
    if(Effects.Num()>=16)return false;
    FVector Position=Origin,End=Target;
    if(Spell==EHeliosCalamitySpell::Earthquake||Spell==EHeliosCalamitySpell::Tornado)
    {
        FHitResult Floor;FCollisionQueryParams Q(SCENE_QUERY_STAT(CalamityGround),false,GetOwner());
        for(TActorIterator<APawn> P(GetWorld());P;++P)Q.AddIgnoredActor(*P);
        if(!GetWorld()->LineTraceSingleByChannel(Floor,End+FVector(0,0,80),End-FVector(0,0,900),ECC_Visibility,Q)||Floor.ImpactNormal.Z<.55f)
        {Message=TEXT("此法术需要可见的地面落点");MessageUntil=GetWorld()->GetTimeSeconds()+1.5f;return false;}
        Position=Floor.ImpactPoint+FVector(0,0,8);
        FHitResult Wall;if(GetWorld()->LineTraceSingleByChannel(Wall,Origin,Position+FVector(0,0,35),ECC_Visibility,Q))return false;
    }
    auto* E=GetWorld()->SpawnActor<AHeliosCalamityEffect>(Position,FRotator::ZeroRotator,Params(GetOwner()));if(!E)return false;
    E->Initialize(this,Spell,End,Scale);Effects.Add(E);LastSpell=Spell;++CastCount;
    Burst(this,Origin,TEXT("NS_CS_Cast"),.55f);return true;
}
void UHeliosCalamityState::DamageArea(const FVector& Center,EHeliosCalamitySpell Spell,float Scale,float Time)
{
    if(!Settings)return;const auto& S=Settings->Spells[static_cast<int32>(Spell)];auto* P=Cast<APawn>(GetOwner());
    for(TActorIterator<AHeliosEnemyActor> E(GetWorld());E;++E)
    {
        const float Distance=FVector::Dist(E->GetActorLocation(),Center);
        if(Distance>GetDamageRadius()||!CanHit(Center+FVector(0,0,40),*E))continue;
        float Amount=Spell==EHeliosCalamitySpell::Tornado?UCombatDamageSettings::Resolve(this,ECombatDamageType::Tornado):S.Damage*Scale*Time;
        if(Spell==EHeliosCalamitySpell::Fireball)Amount=FMath::Lerp(S.Damage,S.MinimumDamage,FMath::Clamp((Distance-120)/(S.Radius-120),0.f,1.f))*Scale;
        UGameplayStatics::ApplyDamage(*E,Amount,P?P->GetController():nullptr,GetOwner(),UDamageType::StaticClass());++HitCount;
    }
}
void UHeliosCalamityState::Clear()
{
    if(auto* P=Cast<APawn>(GetOwner())){if(auto* W=Cast<AHeliosCalamityStaff>(HeliosWeaponHolderContext::EquippedWeapon(P));W&&W->Channel)W->Channel->Cancel();if(bAppliedShake)if(auto* LocalPlayer=Cast<AHeliosArenaCharacter>(P))LocalPlayer->FirstPersonCamera->ClearAdditiveOffset();}
    bAppliedShake=false;ShakePower=0;for(auto A:PlayingSounds)if(IsValid(A))A->Stop();PlayingSounds.Reset();
    if(IsValid(Tower))Tower->Destroy();Tower=nullptr;
    for(auto E:Effects)if(IsValid(E))E->Destroy();Effects.Reset();ReadyAt=CastReadyAt=0;PoisonAccumulator=0;
    if(GetWorld()&&GetOwner())for(TObjectIterator<UNiagaraComponent> C;C;++C)
        if(C->GetWorld()==GetWorld()&&C->ComponentHasTag(TEXT("Calamity"))&&C->ComponentHasTag(FName(*GetOwner()->GetName())))UHeliosWeaponVFX::ReleaseTracked(*C);
}
void UHeliosCalamityState::EndPlay(EEndPlayReason::Type Reason){Clear();Super::EndPlay(Reason);}
void UHeliosCalamityState::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Dt,Type,Function);auto* P=Cast<APawn>(GetOwner());
    if(!P||HeliosWeaponHolderContext::IsDead(P)){Clear();return;}if(!Settings)return;
    Effects.RemoveAll([](const auto& E){return !IsValid(E.Get())||E->IsActorBeingDestroyed();});
    PlayingSounds.RemoveAll([](const auto& A){return !IsValid(A.Get())||!A->IsPlaying();});
    for(auto F:Effects)if(F->LoopAudio)
    {
        int32 Nearer=0;const float D=FVector::DistSquared(P->GetActorLocation(),F->GetActorLocation());
        for(auto Other:Effects)if(Other!=F&&Other->LoopAudio&&Other->Spell==F->Spell){const float OD=FVector::DistSquared(P->GetActorLocation(),Other->GetActorLocation());if(OD<D||(FMath::IsNearlyEqual(OD,D)&&Other->GetUniqueID()<F->GetUniqueID()))++Nearer;}
        F->SetLoopAudible(Nearer<2,Dt);
    }
    if(bAppliedShake){if(auto* LocalPlayer=Cast<AHeliosArenaCharacter>(P))LocalPlayer->FirstPersonCamera->ClearAdditiveOffset();bAppliedShake=false;}
    ShakePower=FMath::FInterpTo(ShakePower,0.f,Dt,9.f);
    if(ShakePower>.002f){const float T=GetWorld()->GetTimeSeconds();if(auto* LocalPlayer=Cast<AHeliosArenaCharacter>(P)){LocalPlayer->FirstPersonCamera->AddAdditiveOffset(FTransform(FVector(0,FMath::Sin(T*73)*ShakePower,FMath::Sin(T*91)*ShakePower*.6f)),0);bAppliedShake=true;}}
    PoisonAccumulator+=Dt;
    while(PoisonAccumulator>=.2f)
    {
        PoisonAccumulator-=.2f;
        for(TActorIterator<AHeliosEnemyActor> E(GetWorld());E;++E)
        {
            if(!Eligible(*E))continue;float MaxDamage=0;
            for(auto F:Effects)if(F->Spell==EHeliosCalamitySpell::Poison&&F->bField&&F->FieldAge<F->Duration&&
                FVector::DistSquared(E->GetActorLocation(),F->GetActorLocation())<=FMath::Square(GetDamageRadius())&&CanHit(F->GetActorLocation()+FVector(0,0,45),*E))
                MaxDamage=UCombatDamageSettings::Resolve(this,ECombatDamageType::Poison);
            if(MaxDamage>0){UGameplayStatics::ApplyDamage(*E,MaxDamage,P->GetController(),GetOwner(),UDamageType::StaticClass());++HitCount;}
        }
    }
}

UAudioComponent* UHeliosCalamityState::Sound(USoundBase* Asset,USceneComponent* Attach,const FVector& Location,float Volume)
{
    if(!Asset)return nullptr;
    Volume*=SpellSoundGain;
    UAudioComponent* A=Attach?HeliosAudio::SpawnSoundAttached(Asset,Attach,NAME_None,Location,FRotator::ZeroRotator,EAttachLocation::KeepRelativeOffset,true,Volume,1.f,0.f,nullptr,nullptr,true):HeliosAudio::SpawnSoundAtLocation(this,Asset,Location,FRotator::ZeroRotator,Volume);
    if(A){A->bIsUISound=false;PlayingSounds.Add(A);}return A;
}
void UHeliosCalamityState::Shake(const FVector& Center,float Strength)
{
    const float Attenuation=FMath::Clamp(1-FVector::Dist(GetOwner()->GetActorLocation(),Center)/1800.f,0.f,1.f);ShakePower=FMath::Max(ShakePower,Strength*Attenuation);
}

AHeliosCalamityStaff::AHeliosCalamityStaff(){MeshAssetPath=TEXT("/Game/Helios/Weapons/Calamity/SM_CS_Staff");PrimaryActorTick.TickGroup=TG_PostPhysics;}
void AHeliosCalamityStaff::ApplyDefinition(const UHeliosWeaponDefinition* D)
{
    Super::ApplyDefinition(D);D=ActiveDefinition;Settings=const_cast<UHeliosCalamityDefinition*>(Cast<UHeliosCalamityDefinition>(D));
    if(!Settings){Settings=NewObject<UHeliosCalamityDefinition>(this);ApplyWeaponDamageSettings(this,Settings);}
    UHeliosCalamityState::Get(GetOwner())->Settings=Settings;
    if(Settings->ImpactPresentation)Settings->ImpactPresentation->Prewarm();
    if(Settings->WindPresentation)Settings->WindPresentation->Prewarm();
}
void AHeliosCalamityStaff::StartFiring(){auto* P=Cast<APawn>(GetOwner());if(!P||HeliosWeaponHolderContext::IsDead(P)||GetWorld()->IsPaused())return;bHeld=true;CastOnce();}
void AHeliosCalamityStaff::CastOnce()
{
    auto* P=Cast<APawn>(GetOwner());auto* S=UHeliosCalamityState::Get(P);
    if(!P||!Settings||!S||S->Deployed()||GetWorld()->GetTimeSeconds()<S->CastReadyAt||(Channel&&Channel->bCharging)||S->Effects.Num()>=16)return;
    if(!Channel){Channel=NewObject<UHeliosCalamityChannel>(this);Channel->Initialize(S,GetFirstPersonMesh(),FVector(-8,0,-10),GetFirstPersonMesh()->GetSocketTransform(TEXT("Muzzle"),RTS_Component).GetLocation());}
    const auto Spell=static_cast<EHeliosCalamitySpell>(ForcedSpell>=0?FMath::Clamp(ForcedSpell,0,3):FMath::RandRange(0,3));
    S->LastSpell=Spell;S->CastReadyAt=GetWorld()->GetTimeSeconds()+Settings->RefireSeconds;Channel->Start(Spell);OnShotPresented.Broadcast();AnimationAge=0;
}
void AHeliosCalamityStaff::ReleaseSpell()
{
    auto* P=Cast<APawn>(GetOwner());auto* S=UHeliosCalamityState::Get(P);if(!P||!S||!Channel)return;
    const FVector Eye=HeliosWeaponHolderContext::ViewLocation(P),Direction=HeliosWeaponHolderContext::ViewRotation(P).Vector();
    FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(CalamityAim),false,P);Q.AddIgnoredActor(this);
    FVector Target=Eye+Direction*Settings->Range;if(GetWorld()->LineTraceSingleByChannel(Hit,Eye,Target,ECC_Visibility,Q))Target=Hit.ImpactPoint;
    FVector Origin=GetFirstPersonMesh()->GetSocketLocation(TEXT("Muzzle"));
    if(GetWorld()->LineTraceSingleByChannel(Hit,Eye,Origin,ECC_Visibility,Q))Origin=Hit.ImpactPoint-Direction*8;
    if(S->CastSpell(Channel->Spell,Origin,Target))Channel->Release();else Channel->Cancel();
}
void AHeliosCalamityStaff::StartSecondaryFire()
{
    auto* P=Cast<APawn>(GetOwner());if(!P||HeliosWeaponHolderContext::IsDead(P)||GetWorld()->IsPaused())return;
    bHeld=false;if(auto* S=UHeliosCalamityState::Get(P);S&&S->Deploy()){if(Channel)Channel->Cancel();AnimationAge=0;if(auto* Pres=HeliosWeaponHolderContext::Presentation(P))Pres->ApplyMeleeImpact(.5f);}
}
void AHeliosCalamityStaff::DeactivateWeapon(){bHeld=false;if(Channel)Channel->Cancel();Super::DeactivateWeapon();}
void AHeliosCalamityStaff::EndPlay(EEndPlayReason::Type Reason){if(Channel)Channel->Cancel();Super::EndPlay(Reason);}
void AHeliosCalamityStaff::Tick(float Dt)
{
    Super::Tick(Dt);auto* P=Cast<APawn>(GetOwner());if(!P||!Settings)return;
    auto* S=UHeliosCalamityState::Get(P);if(!S)return;AnimationAge+=Dt;
    if(Channel){Channel->Advance(Dt);if(Channel->IsReady()){if(S->Deployed()||HeliosWeaponHolderContext::IsDead(P))Channel->Cancel();else ReleaseSpell();}}
    const bool Deployed=S->Deployed();const bool Flying=Deployed&&S->Tower->Age<.85f;
    GetFirstPersonMesh()->SetVisibility(!Deployed);FirstPersonModel->SetVisibility(!Deployed&&!HasExternalPresentation());ThirdPersonModel->SetVisibility(!Deployed);
    if(auto* Pres=HeliosWeaponHolderContext::Presentation(P))
    {
        if(auto* Arms=Pres->GetArmsMesh())Arms->SetVisibility(!Deployed||Flying);
        if(Flying)Pres->SetMeleePose(Settings->DeployPose.LoadSynchronous(),S->Tower->Age/.85f);
        else if(bWasDeployed&&!Deployed){Pres->SetMeleePose(nullptr,0);Pres->ReplayEquip();PlayCalamitySound(this,P->GetActorLocation(),TEXT("SFX_CS_Return"),.45f);}
    }
    bWasDeployed=Deployed;
    if(GlowMaterials.IsEmpty()&&HasExternalPresentation())for(int32 I=0;I<GetFirstPersonMesh()->GetNumMaterials();++I)
        if(auto* D=GetFirstPersonMesh()->CreateDynamicMaterialInstance(I))GlowMaterials.Add(D);
    const FLinearColor Color=FMath::Lerp(SpellColor(S->LastSpell),FLinearColor(.4f,.055f,1),FMath::Clamp(AnimationAge/1.3f,0.f,1.f));
    for(auto M:GlowMaterials){M->SetVectorParameterValue(TEXT("SpellColor"),Color);M->SetScalarParameterValue(TEXT("Charge"),Channel?Channel->Power:0);M->SetScalarParameterValue(TEXT("FlowProgress"),Channel?FMath::Clamp(Channel->Age/Settings->CastWindupSeconds,0.f,1.f):1);M->SetVectorParameterValue(TEXT("FlowOrigin"),GetFirstPersonMesh()->GetComponentLocation());M->SetVectorParameterValue(TEXT("FlowAxis"),(GetFirstPersonMesh()->GetSocketLocation(TEXT("Muzzle"))-GetFirstPersonMesh()->GetComponentLocation()).GetSafeNormal());}
    if(bHeld&&!Deployed&&!HeliosWeaponHolderContext::IsDead(P)&&!IsHidden())CastOnce();
}

AHeliosCalamityTower::AHeliosCalamityTower()
{
    PrimaryActorTick.bCanEverTick=true;RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Light=CreateDefaultSubobject<UPointLightComponent>(TEXT("CoreLight"));Light->SetupAttachment(RootComponent);Light->SetRelativeLocation(FVector(0,0,200));Light->SetIntensity(1800);Light->SetAttenuationRadius(500);Light->SetCastShadows(false);
    Tags.Add(TEXT("HeliosSummon"));
}
void AHeliosCalamityTower::Initialize(UHeliosCalamityState* S)
{
    State=S;Ground=GetActorLocation();auto* P=Cast<APawn>(GetOwner());LaunchPoint=P?HeliosWeaponHolderContext::ViewLocation(P)+HeliosWeaponHolderContext::ViewRotation(P).Vector()*65:Ground+FVector(0,0,180);
    for(const TCHAR* N:{TEXT("SM_CS_Shaft"),TEXT("SM_CS_Crown"),TEXT("SM_CS_Core"),TEXT("SM_CS_Base"),TEXT("SM_CS_Seal")})Parts.Add(Part(this,N));
    for(int32 I=0;I<4;++I)Parts.Add(Part(this,TEXT("SM_CS_Petal")));
    for(auto M:Parts)Dynamic(M,Materials,FLinearColor(.4f,.055f,1));
    Channel=NewObject<UHeliosCalamityChannel>(this);Channel->Initialize(S,RootComponent,FVector(0,0,75),FVector(0,0,210));
    Burst(State,Ground,TEXT("NS_CS_Cast"),2);
}
void AHeliosCalamityTower::EndPlay(EEndPlayReason::Type Reason){if(Channel)Channel->Cancel();Super::EndPlay(Reason);}
void AHeliosCalamityTower::Tick(float Dt)
{
    Super::Tick(Dt);if(!IsValid(State)||!State->Settings){Destroy();return;}Age+=Dt;
    const float Lifetime=State->Settings->TowerSeconds+.85f;
    float Spread=FMath::SmoothStep(0.f,1.f,FMath::Clamp(Age/.85f,0.f,1.f));
    if(Age>Lifetime-.65f)
    {
        Spread=FMath::Clamp((Lifetime-Age)/.65f,0.f,1.f);
        if(auto* P=Cast<APawn>(GetOwner()))LaunchPoint=HeliosWeaponHolderContext::ViewLocation(P)+HeliosWeaponHolderContext::ViewRotation(P).Vector()*65;
    }
    FVector At=FMath::Lerp(LaunchPoint,Ground,Spread);if(Age>=.85f&&Age<Lifetime-.65f)At=Ground;SetActorLocation(At);
    Parts[0]->SetRelativeLocation(FVector(0,0,65));Parts[1]->SetRelativeLocation(FVector(0,0,65+Spread*15));
    Parts[2]->SetRelativeLocation(FVector(0,0,170+Spread*40+FMath::Sin(Age*2)*5));Parts[2]->SetRelativeRotation(FRotator(0,Age*38,0));
    Parts[3]->SetRelativeScale3D(FVector(FMath::Max(.02f,Spread)));Parts[4]->SetRelativeScale3D(FVector(1.25f*FMath::Max(.02f,Spread)));Parts[4]->SetRelativeLocation(FVector(0,0,4));Parts[4]->SetRelativeRotation(FRotator(0,-Age*15,0));
    for(int32 I=5;I<9;++I){const float A=(I-5)*PI/2+Age*.2f;Parts[I]->SetRelativeLocation(FVector(FMath::Cos(A),FMath::Sin(A),0)*Spread*38+FVector(0,0,65));Parts[I]->SetRelativeRotation(FRotator(0,FMath::RadiansToDegrees(A),Spread*25));}
    for(auto M:Materials)M->SetScalarParameterValue(TEXT("Opacity"),FMath::Min(1.f,Spread*2));
    if(Channel){Channel->TipPoint=Parts[2]->GetRelativeLocation();Channel->Advance(Dt);}
    for(auto M:Materials){M->SetScalarParameterValue(TEXT("Charge"),Channel?Channel->Power:0);M->SetScalarParameterValue(TEXT("FlowProgress"),Channel?FMath::Clamp(Channel->Age/State->Settings->CastWindupSeconds,0.f,1.f):1);M->SetVectorParameterValue(TEXT("FlowOrigin"),Ground+FVector(0,0,75));M->SetVectorParameterValue(TEXT("FlowAxis"),FVector::UpVector);}
    if(Age>Lifetime-.65f&&Channel)Channel->Cancel();
    if(Age>=Lifetime)
    {
        // Returning the staff allows immediate redeployment, including with older data assets.
        State->ReadyAt=0;State->Tower=nullptr;Burst(State,Ground+FVector(0,0,180),TEXT("NS_CS_Cast"),2);Destroy();return;
    }
    const FVector Origin=Ground+FVector(0,0,225);
    if(Channel&&Channel->IsReady())
    {
        if(PendingTarget.IsValid()&&FVector::DistSquared(PendingTarget->GetActorLocation(),Ground)<=FMath::Square(State->Settings->TowerRange)&&State->CanHit(Origin,PendingTarget.Get())&&State->CastSpell(Channel->Spell,Origin,PendingTarget->GetActorLocation(),State->Settings->TowerDamageScale)){Channel->Release();++State->TowerShots;}
        else Channel->Cancel();PendingTarget.Reset();
    }
    if(Age<.85f||Age>Lifetime-.65f-State->Settings->CastWindupSeconds||Age<NextShot||(Channel&&Channel->bCharging))return;
    NextShot=Age+.2f;AHeliosEnemyActor* Best=nullptr;float Distance=FMath::Square(State->Settings->TowerRange);
    for(TActorIterator<AHeliosEnemyActor> E(GetWorld());E;++E){const float D=FVector::DistSquared(E->GetActorLocation(),Ground);if(D<=Distance&&State->CanHit(Origin,*E)){Best=*E;Distance=D;}}
    if(!Best)return;
    const auto Spell=static_cast<EHeliosCalamitySpell>(FMath::RandRange(0,3));PendingTarget=Best;Channel->Start(Spell);NextShot=Age+State->Settings->TowerInterval;
    Light->SetLightColor(SpellColor(Spell));for(auto M:Materials)M->SetVectorParameterValue(TEXT("SpellColor"),SpellColor(Spell));
}
