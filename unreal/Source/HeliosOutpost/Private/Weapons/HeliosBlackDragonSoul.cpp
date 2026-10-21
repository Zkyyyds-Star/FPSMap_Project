#include "Weapons/HeliosBlackDragonSoul.h"
#include "Weapons/HeliosHandAnimInstance.h"
#include "Weapons/HeliosWeaponHolderContext.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Weapons/HeliosImpactPresentationSubsystem.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Pets/HeliosPetDog.h"
#include "Audio/HeliosAudio.h"
#include "Animation/AnimSequence.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"

UHeliosBlackDragonSoulState::UHeliosBlackDragonSoulState()
{ PrimaryComponentTick.bCanEverTick=true; }
void UHeliosBlackDragonSoulState::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Dt,Type,Tick);
    if(HeliosWeaponHolderContext::IsDead(GetOwner()))Reset();
    else Refresh();
    WorldActors.RemoveAll([](const auto& A){return !A.IsValid();});
}
void UHeliosBlackDragonSoulState::EndPlay(const EEndPlayReason::Type Reason)
{ Reset(); Super::EndPlay(Reason); }
void UHeliosBlackDragonSoulState::TrackWorldActor(AActor* Actor)
{ if(IsValid(Actor))WorldActors.AddUnique(Actor); }

UHeliosBlackDragonSoulState* UHeliosBlackDragonSoulState::Get(AActor* Holder, bool Create)
{
    if (!Holder) return nullptr;
    auto* State = Holder->FindComponentByClass<UHeliosBlackDragonSoulState>();
    if (!State && Create)
    {
        State = NewObject<UHeliosBlackDragonSoulState>(Holder, TEXT("BlackDragonSoulChamber"));
        Holder->AddInstanceComponent(State); State->RegisterComponent();
    }
    return State;
}
void UHeliosBlackDragonSoulState::Reset()
{
    for(const auto& Actor:WorldActors)if(Actor.IsValid())Actor->Destroy();
    WorldActors.Reset(); Ammo=1; ReloadStart=ReloadEnd=-1;
    Explosions=EnemyHits=Knockbacks=0; LastExplosion=FVector::ZeroVector;
}
void UHeliosBlackDragonSoulState::Refresh()
{
    if (ReloadEnd>=0 && GetWorld() && GetWorld()->GetTimeSeconds()>=ReloadEnd)
    { Ammo=1; ReloadStart=ReloadEnd=-1; }
}
void UHeliosBlackDragonSoulState::StartReload(float Seconds,float Delay)
{
    Refresh(); if (Ammo>0 || ReloadEnd>=0 || !GetWorld()) return;
    ReloadStart=GetWorld()->GetTimeSeconds()+FMath::Max(0.f,Delay);
    ReloadEnd=ReloadStart+FMath::Max(.01f,Seconds);
}
float UHeliosBlackDragonSoulState::Remaining() const
{ return ReloadEnd>=0 && GetWorld() ? FMath::Max(0.f,float(ReloadEnd-GetWorld()->GetTimeSeconds())) : 0.f; }

UHeliosBlackDragonSoulDefinition::UHeliosBlackDragonSoulDefinition()
{
    WeaponId=TEXT("cfm_heilonghun"); DisplayName=FText::FromString(TEXT("黑龙魂"));
    Description=FText::FromString(TEXT("黑龙魂重炮。单枚炮弹命中后在8米范围爆炸，中心伤害1200、边缘600；打空自动换弹，R 换弹，支持后台装填，击退仍存活的普通敌人。"));
    WeaponClass=AHeliosBlackDragonSoulWeapon::StaticClass();
    MagazineSize=1; ReloadSeconds=3.4333339f; Damage=1200; MinimumDamage=600; PelletCount=1;
    Range=3000; FalloffStart=150; SpreadHalfAngle=0; ProjectileSpeed=6000; RefireSeconds=.85f; Recoil=1.6f;
    FireEffectName=TEXT("P_Gunfire_1P_SanDan_HeiLong");
    AttachmentLocation=FVector::ZeroVector; AttachmentRotation=FRotator(0,-90,0); AttachmentScale=FVector::OneVector;
    SkeletalWeaponMesh=TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Helios/CFMBlackDragonSoul/SK_BlackDragonSoul_FP")));
    PreviewTexture=TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Helios/CFMBlackDragonSoul/Textures/T_BlackDragonSoulPreview")));
}
AHeliosBlackDragonSoulWeapon::AHeliosBlackDragonSoulWeapon()
{
    bFullAuto=false; MagazineSize=1; SetExternalPresentation(true,TEXT("MuzzleFlashSocket"));
    GetFirstPersonMesh()->SetCastShadow(false); GetFirstPersonMesh()->SetBoundsScale(4);
    GetFirstPersonMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    GetFirstPersonMesh()->bEnableUpdateRateOptimizations=false;
}
void AHeliosBlackDragonSoulWeapon::ApplyDefinition(const UHeliosWeaponDefinition* Definition)
{
    Super::ApplyDefinition(Definition);
    if (!Definition) return;
    GetFirstPersonMesh()->SetSkeletalMesh(Definition->SkeletalWeaponMesh.LoadSynchronous());
    GetFirstPersonMesh()->SetAnimInstanceClass(UHeliosHandAnimInstance::StaticClass());
    if (const auto* D=Cast<UHeliosBlackDragonSoulDefinition>(ActiveDefinition))
    {
        for (const auto& Pair:D->Actions) Clips.Add(Pair.Key,Pair.Value.LoadSynchronous());
        for (const auto& Event:D->ActionSounds) Event.Sound.LoadSynchronous();
        EffectLibrary=LoadObject<UHeliosSSXEffectLibrary>(nullptr,TEXT("/Game/Helios/CFMBlackDragonSoul/Effects/DA_HLH_Effects"));
        if(EffectLibrary)EffectLibrary->GetParsedRecipe();
    }
    Chamber=UHeliosBlackDragonSoulState::Get(GetOwner());
}
void AHeliosBlackDragonSoulWeapon::ActivateWeapon(const FName& Tag)
{
    Super::ActivateWeapon(Tag); bActive=true;
    Chamber=UHeliosBlackDragonSoulState::Get(GetOwner()); Chamber->Refresh();
    CurrentBullets=Chamber->Ammo; WeaponOwner->UpdateWeaponHUD(CurrentBullets,MagazineSize);
    if (Chamber->ReloadEnd>=0 && GetWorld()->GetTimeSeconds()>=Chamber->ReloadStart)
        BeginAction(TEXT("ChangeClip"),FMath::Max(0.f,float(GetWorld()->GetTimeSeconds()-Chamber->ReloadStart)));
    else if(Chamber->ReloadEnd<0)BeginAction(TEXT("Equip"));
}
void AHeliosBlackDragonSoulWeapon::StopSounds()
{ for (const auto& Sound:Sounds) if (IsValid(Sound)) Sound->Stop(); Sounds.Reset(); }
void AHeliosBlackDragonSoulWeapon::DeactivateWeapon()
{ bActive=false; StopSounds(); Super::DeactivateWeapon(); }
void AHeliosBlackDragonSoulWeapon::EndPlay(const EEndPlayReason::Type Reason)
{ StopSounds(); for(const auto& E:Effects)if(IsValid(E))E->Destroy(); Effects.Reset(); Super::EndPlay(Reason); }
float AHeliosBlackDragonSoulWeapon::ReloadRemaining() const
{ return Chamber ? Chamber->Remaining() : 0; }
void AHeliosBlackDragonSoulWeapon::StartFiring()
{
    if (!bActive || HeliosWeaponHolderContext::IsDead(GetOwner()) || ReloadRemaining()>0 ||
        (!Action.IsNone() && Action!=TEXT("Fire"))) return;
    Super::StartFiring();
}
void AHeliosBlackDragonSoulWeapon::BeginReload(float Seconds)
{
    if (!Chamber) return; StopFiring(); Chamber->Ammo=CurrentBullets; Chamber->StartReload(Seconds);
    if (Chamber->ReloadEnd>=0 && GetWorld()->GetTimeSeconds()>=Chamber->ReloadStart)
        BeginAction(TEXT("ChangeClip"),FMath::Max(0.f,float(GetWorld()->GetTimeSeconds()-Chamber->ReloadStart)));
}
void AHeliosBlackDragonSoulWeapon::ReloadMagazine()
{ if(Chamber){Chamber->Refresh();if(Chamber->Remaining()>0)return;} Super::ReloadMagazine(); Action=NAME_None; }
void AHeliosBlackDragonSoulWeapon::CancelReload()
{
    // Cancellation hides playback. Chamber progress belongs to the holder and survives switches.
    StopSounds(); Action=NAME_None; Super::CancelReload();
}
void AHeliosBlackDragonSoulWeapon::BeginAction(FName Clip,float Age)
{
    Action=Clip; ActionAge=Age; PlayedEvents.Reset();
    if(const auto* D=Cast<UHeliosBlackDragonSoulDefinition>(ActiveDefinition))
        for(int32 I=0;I<D->ActionSounds.Num();++I)
            if(D->ActionSounds[I].Action==Clip && D->ActionSounds[I].Seconds<Age)PlayedEvents.Add(I);
    Pose();
}
void AHeliosBlackDragonSoulWeapon::Pose()
{
    const FName Clip=Action.IsNone()?(PawnOwner&&PawnOwner->GetVelocity().Size2D()>20?FName("Move"):FName("Idle")):Action;
    UAnimSequence* Sequence=Clips.FindRef(Clip); if(!Sequence)Sequence=Clips.FindRef(TEXT("Idle"));
    if(auto* A=Cast<UHeliosHandAnimInstance>(GetFirstPersonMesh()->GetAnimInstance()))
        A->SetTimelinePose(Sequence,Action.IsNone()?IdleAge:ActionAge,Action.IsNone());
}
void AHeliosBlackDragonSoulWeapon::Tick(float Dt)
{
    Super::Tick(Dt); if(!bActive||!Chamber||!GetWorld()||GetWorld()->IsPaused())return;
    if(HeliosWeaponHolderContext::IsDead(GetOwner())){Chamber->Reset();StopFiring();StopSounds();return;}
    IdleAge+=Dt; ActionAge+=Dt;
    Chamber->Refresh();
    if(CurrentBullets!=Chamber->Ammo){CurrentBullets=Chamber->Ammo;WeaponOwner->UpdateWeaponHUD(CurrentBullets,MagazineSize);}
    if(Chamber->ReloadEnd>=0 && GetWorld()->GetTimeSeconds()>=Chamber->ReloadStart)
    {
        const float Age=float(GetWorld()->GetTimeSeconds()-Chamber->ReloadStart);
        if(Action!=TEXT("ChangeClip"))BeginAction(TEXT("ChangeClip"),Age);
        ActionAge=Age;
    }
    if(const auto* D=Cast<UHeliosBlackDragonSoulDefinition>(ActiveDefinition))
        for(int32 I=0;I<D->ActionSounds.Num();++I)
        {
            const auto& Event=D->ActionSounds[I];
            if(Event.Action!=Action||PlayedEvents.Contains(I)||ActionAge<Event.Seconds)continue;
            PlayedEvents.Add(I);
            UE_LOG(LogTemp,Display,TEXT("HLH_SOUND action=%s age=%.3f event=%.3f"),*Event.Action.ToString(),ActionAge,Event.Seconds);
            if(auto* S=Event.Sound.LoadSynchronous())
                if(auto* C=HeliosWeaponHolderContext::IsCompanion(GetOwner())?
                    HeliosAudio::SpawnSoundAttached(S,GetOwner()->GetRootComponent(),NAME_None,FVector::ZeroVector,EAttachLocation::KeepRelativeOffset,true,.8f):
                    HeliosAudio::SpawnSound2D(this,S,.8f))Sounds.Add(C);
        }
    if(UAnimSequence* Sequence=Clips.FindRef(Action);Sequence&&ActionAge>=Sequence->GetPlayLength())Action=NAME_None;
    Sounds.RemoveAll([](const auto& S){return !IsValid(S)||!S->IsPlaying();});
    Effects.RemoveAll([](const auto& E){return !IsValid(E);}); Pose();
}
FVector AHeliosBlackDragonSoulWeapon::MuzzleLocation() const
{
    if(auto* P=GetOwner()->FindComponentByClass<UHeliosCharacterPresentationComponent>();P&&P->IsReady())return P->GetAnchorTransform(TEXT("Muzzle")).GetLocation();
    return GetFirstPersonMesh()->GetSocketLocation(TEXT("MuzzleFlashSocket"));
}
void AHeliosBlackDragonSoulWeapon::FireProjectile(const FVector& Target)
{
    if(!bActive||!Chamber||!PawnOwner||!WeaponOwner||!ActiveDefinition||CurrentBullets<=0||ReloadRemaining()>0)return;
    FVector View; FRotator Aim; PawnOwner->GetActorEyesViewPoint(View,Aim);
    const auto* D=Cast<UHeliosBlackDragonSoulDefinition>(ActiveDefinition); if(!D)return;
    const FVector Muzzle=MuzzleLocation();
    FCollisionQueryParams Q(SCENE_QUERY_STAT(BlackDragonSoulView),true,GetOwner()); Q.AddIgnoredActor(this);
    if(auto* Pets=GetWorld()->GetSubsystem<UHeliosPetWorld>())Pets->IgnoreAll(Q);
    Q.AddIgnoredActor(HeliosWeaponHolderContext::FriendlyPlayer(GetOwner()));
    for(TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It)
        if(It->ActorHasTag(TEXT("Friendly"))||It->IsEnemyDead())Q.AddIgnoredActor(*It);
    FHitResult ViewHit,NearWall;
    const FVector ViewEnd=View+Aim.Vector()*D->Range;
    GetWorld()->LineTraceSingleByChannel(ViewHit,View,ViewEnd,ECC_Visibility,Q);
    const FVector Direction=((ViewHit.bBlockingHit?ViewHit.ImpactPoint:ViewEnd)-Muzzle).GetSafeNormal();
    auto* Projectile=GetWorld()->SpawnActorDeferred<AHeliosBlackDragonSoulProjectile>(
        AHeliosBlackDragonSoulProjectile::StaticClass(),FTransform(Direction.Rotation(),Muzzle),GetOwner(),PawnOwner,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Projectile)return;
    Projectile->InitializeShot(D,Direction,this);
    UGameplayStatics::FinishSpawningActor(Projectile,Projectile->GetActorTransform());
    LastProjectile=Projectile;
    if(GetWorld()->LineTraceSingleByChannel(NearWall,View,Muzzle,ECC_Visibility,Q))
        Projectile->Explode(NearWall.ImpactPoint,NearWall.ImpactNormal);
    BeginAction(TEXT("Fire"));
    if(!D->FireEffectName.IsEmpty())
    {
        const bool FirstPerson=!HeliosWeaponHolderContext::IsCompanion(GetOwner());
        FString Name=D->FireEffectName;if(!FirstPerson)Name.ReplaceInline(TEXT("1P"),TEXT("3P"));
        if(auto* E=AHeliosSSXEffect::Spawn(this,Name,Muzzle,Aim,FirstPerson,1,TEXT("/Game/Helios/CFMBlackDragonSoul/Effects/DA_HLH_Effects")))Effects.Add(E);
    }
    WeaponOwner->AddWeaponRecoil(FiringRecoil); --CurrentBullets; Chamber->Ammo=CurrentBullets;
    const UAnimSequence* FireClip=Clips.FindRef(TEXT("Fire"));
    Chamber->StartReload(ActiveDefinition->ReloadSeconds,FireClip?FireClip->GetPlayLength():.2f);
    WeaponOwner->UpdateWeaponHUD(CurrentBullets,MagazineSize); bShotCommitted=true; ++ShotsFired;
}
