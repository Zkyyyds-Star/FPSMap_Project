#include "Weapons/HeliosFirstPersonPresentationComponent.h"
#include "Audio/HeliosAudio.h"
#include "Player/HeliosArenaCharacter.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Weapons/HeliosWeaponHolderContext.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Weapons/HeliosColaWeapon.h"
#include "Weapons/HeliosWeaponPresentationProfile.h"
#include "Weapons/HeliosWeaponVFX.h"
#include "Weapons/HeliosImpactPresentationSubsystem.h"
#include "ShooterWeapon.h"
#include "ShooterProjectile.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"

UHeliosFirstPersonPresentationComponent::UHeliosFirstPersonPresentationComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

bool UHeliosFirstPersonPresentationComponent::Equip(AHeliosArenaCharacter* Character, AShooterWeapon* InWeapon, const UHeliosWeaponDefinition* Definition)
{return EquipHolder(Character,Character?Character->FirstPersonCamera.Get():nullptr,InWeapon,Definition,false);}
bool UHeliosFirstPersonPresentationComponent::EquipCompanion(ACharacter* Character,USceneComponent* ViewAnchor,AShooterWeapon* InWeapon,const UHeliosWeaponDefinition* Definition)
{return EquipHolder(Character,ViewAnchor,InWeapon,Definition,true);}
bool UHeliosFirstPersonPresentationComponent::EquipHolder(ACharacter* Character,USceneComponent* ViewAnchor,AShooterWeapon* InWeapon,const UHeliosWeaponDefinition* Definition,bool bCompanion)
{
    Unequip();
    if (!Character || !ViewAnchor || !InWeapon || !Definition || FParse::Param(FCommandLine::Get(), TEXT("HeliosLegacyPresentation"))) return false;
    UHeliosWeaponPresentationProfile* NewProfile = Definition->PresentationProfile.LoadSynchronous();
    if (!NewProfile) return false;
    USkeletalMesh* HandsAsset = NewProfile->ArmsMesh.LoadSynchronous();
    USkeletalMesh* GunAsset = NewProfile->WeaponMesh.LoadSynchronous();
    UAnimSequence* IdleAsset = NewProfile->ArmsIdle.LoadSynchronous();
    UAnimSequence* ReloadAsset = NewProfile->ArmsReload.LoadSynchronous();
    if (!HandsAsset || !GunAsset || !HandsAsset->GetSkeleton() || !GunAsset->GetSkeleton() || !IdleAsset || !ReloadAsset) return false;
    Player = Character; HolderView=ViewAnchor; bCompanionSource=bCompanion; Weapon = InWeapon; Profile = NewProfile; WeaponId = Definition->WeaponId;
    if (auto* Impacts=GetWorld()->GetSubsystem<UHeliosImpactPresentationSubsystem>()) Impacts->Preload();
    Idle = IdleAsset; Reload = ReloadAsset;
    Fire = Profile->ArmsFire.LoadSynchronous(); Raise = Profile->ArmsEquip.LoadSynchronous();
    GunReload = Profile->WeaponReload.LoadSynchronous(); GunFire = Profile->WeaponFire.LoadSynchronous();
    MuzzleFX = Profile->MuzzleEffect.LoadSynchronous(); ShotSound = Profile->FireSound.LoadSynchronous();
    ImpactFX = Profile->ImpactEffect.LoadSynchronous();
    OutSound = Profile->MagazineOutSound.LoadSynchronous(); InSound = Profile->MagazineInSound.LoadSynchronous();
    ActionSound = Profile->ActionSound.LoadSynchronous();

    PoseRoot = NewObject<USceneComponent>(Character, TEXT("HeliosViewmodelRoot"));
    PoseRoot->SetupAttachment(ViewAnchor);
    PoseRoot->RegisterComponent();
    PoseRoot->SetRelativeLocationAndRotation(Profile->ViewLocation, Profile->ViewRotation);
    // Blender's FBX scene conversion reverses Y. One shared handedness correction
    // preserves the authored +Y-right grip/ejection contract for both meshes.
    PoseRoot->SetRelativeScale3D(FVector(1.f,-1.f,1.f));
    Arms = NewObject<USkeletalMeshComponent>(Character, TEXT("HeliosBareArms"));
    Arms->SetupAttachment(PoseRoot); Arms->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Arms->SetGenerateOverlapEvents(false); Arms->SetCanEverAffectNavigation(false);
    Arms->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
    Arms->SetOnlyOwnerSee(true); Arms->SetCastShadow(false);
    Arms->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Arms->SetSkeletalMesh(HandsAsset); Arms->RegisterComponent();
    Arms->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    Arms->SetAnimation(Idle); Arms->SetPosition(0, false); Arms->Stop();
    Arms->TickAnimation(0, false); Arms->RefreshBoneTransforms();
    Arms->AddTickPrerequisiteComponent(this);

    USkeletalMeshComponent* Gun = Weapon->GetFirstPersonMesh();
    Gun->SetSkeletalMesh(GunAsset); Gun->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    Gun->SetAnimation(nullptr); Gun->SetRelativeScale3D(FVector::OneVector);
    // The authored grip origin is shared. KeepWorld calculates the rest-pose socket correction once.
    Gun->AttachToComponent(PoseRoot, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
    Gun->SetRelativeTransform(FTransform::Identity);
    if (Arms->DoesSocketExist(Profile->GripSocket))
    {
        Gun->AttachToComponent(Arms, FAttachmentTransformRules::KeepWorldTransform, Profile->GripSocket);
        if(Profile->bAttachAtReferencePose)
        {
            const FReferenceSkeleton& Skeleton=HandsAsset->GetRefSkeleton();
            int32 Bone=Skeleton.FindBoneIndex(Profile->GripSocket);
            if(Bone!=INDEX_NONE)
            {
                FTransform Reference=FTransform::Identity;
                while(Bone!=INDEX_NONE){Reference=Reference*Skeleton.GetRefBonePose()[Bone];Bone=Skeleton.GetParentIndex(Bone);}
                Gun->SetRelativeTransform(Reference.Inverse());
            }
        }
    }
    Gun->SetVisibility(true); Gun->SetHiddenInGame(false);
    Gun->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Gun->AddTickPrerequisiteComponent(Arms);
    Weapon->SetExternalPresentation(true, Profile->MuzzleSocket);
    ShotHandle = Weapon->OnShotPresented.AddUObject(this, &UHeliosFirstPersonPresentationComponent::OnShot);
    ProjectileHandle = Weapon->OnProjectilePresented.AddUObject(this, &UHeliosFirstPersonPresentationComponent::OnProjectile);

    MuzzleLight = NewObject<UPointLightComponent>(Character, TEXT("HeliosMuzzleFill"));
    MuzzleLight->SetupAttachment(Gun, Profile->MuzzleSocket); MuzzleLight->SetIntensity(0);
    MuzzleLight->SetAttenuationRadius(140); MuzzleLight->SetLightColor(FLinearColor(1.0f, .68f, .30f));
    MuzzleLight->SetCastShadows(false); MuzzleLight->RegisterComponent();
    if (UStaticMesh* Shell = Profile->CasingMesh.LoadSynchronous())
    {
        UMaterialInterface* ShellMaterial = Profile->CasingMaterial.LoadSynchronous();
        for (int32 I=0; I<12; ++I)
        {
            UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Character);
            C->SetStaticMesh(Shell); if (ShellMaterial) C->SetMaterial(0, ShellMaterial);
            C->SetCollisionEnabled(ECollisionEnabled::NoCollision); C->SetCastShadow(false);
            C->SetWorldScale3D(WeaponId==TEXT("shotgun") ? FVector(.014f,.014f,.055f) : FVector(.008f,.008f,.024f));
            C->SetCanEverAffectNavigation(false); C->SetVisibility(false); C->RegisterComponent();
            Casings.Add(C); CasingMotion.AddDefaulted();
        }
    }
    Clock=0; EquipAge=0; ShotAge=100; ShotPresentationCount=0; NextCasing=0; SprintBlend=0;
    PreviousView=ViewAnchor->GetComponentRotation(); bReady=true;
    if(auto* Drink=Cast<AHeliosColaWeapon>(Weapon))Drink->RefreshDrinkPresentation();
    Pose(Raise ? Raise : Idle, nullptr, 0, false);
    return true;
}

void UHeliosFirstPersonPresentationComponent::Pose(UAnimSequence* A, UAnimSequence* G, float Alpha, bool bLoop)
{
    if (!Arms || !Weapon) return;
    auto SetPose = [Alpha,bLoop](USkeletalMeshComponent* Mesh, UAnimSequence* Animation)
    {
        if (!Mesh) return;
        UAnimSingleNodeInstance* Instance=Mesh->GetSingleNodeInstance();
        if (!Instance || Instance->GetCurrentAsset() != Animation) Mesh->SetAnimation(Animation);
        if (Animation)
        {
            const float Phase=bLoop ? FMath::Fmod(FMath::Max(0.f,Alpha),1.f) : FMath::Clamp(Alpha,0.f,1.f);
            Mesh->SetPosition(Phase*Animation->GetPlayLength(),false);
        }
        Mesh->Stop();
        // The presentation clock is authoritative. Evaluate this exact sample now,
        // including sockets, instead of waiting for a stopped single-node player's tick.
        Mesh->TickAnimation(0.f,false);
        Mesh->RefreshBoneTransforms();
    };
    SetPose(Arms,A); SetPose(Weapon->GetFirstPersonMesh(),G);
    if(bCompanionSource&&Player&&A)if(auto* Character=Player->FindComponentByClass<UHeliosCharacterPresentationComponent>())Character->PresentSourcePose(A,(bLoop?FMath::Fmod(FMath::Max(0.f,Alpha),1.f):FMath::Clamp(Alpha,0.f,1.f))*A->GetPlayLength(),bLoop);
}

void UHeliosFirstPersonPresentationComponent::BeginReload(float Duration, bool bEmpty)
{
    if (!bReady) return;
    bReloading=true; bEmptyReload=bEmpty; ReloadAge=0; ReloadCues=0;
    ReloadDuration=FMath::Max(Duration,.01f); ShotAge=100; Kick=0;
    Pose(Reload,GunReload,0,false);
}

void UHeliosFirstPersonPresentationComponent::FinishReload()
{
    bReloading=false; ReloadAge=0; ReloadCues=0;
    if (bReady) Pose(Idle,nullptr,0,true);
}

void UHeliosFirstPersonPresentationComponent::ReplayEquip()
{
    if (!bReady) return;
    Cancel(false); EquipAge=0;
    Pose(Raise ? Raise : Idle,nullptr,0,false);
}

float UHeliosFirstPersonPresentationComponent::GetReloadProgress() const
{
    return bReloading ? FMath::Clamp(ReloadAge/ReloadDuration,0.f,1.f) : 0.f;
}

void UHeliosFirstPersonPresentationComponent::PlayCue(USoundBase* Sound,float Volume)
{
    if (!Sound || !Player) return;
    if (bCompanionSource) HeliosAudio::PlaySoundAtLocation(Player,Sound,Player->GetActorLocation(),Volume*.7f);
    else HeliosAudio::PlaySound2D(Player,Sound,Volume);
}

void UHeliosFirstPersonPresentationComponent::OnShot()
{
    if (!bReady || bReloading || !IsValid(Weapon)) return;
    if (bThrowing) { ++ShotPresentationCount; PlayCue(ShotSound,.65f); return; }
    ++ShotPresentationCount; ShotAge=0; EquipAge=1;
    if(Profile->bPreserveProjectileVisual && Fire) Pose(Fire,GunFire,0,false);
    const bool bLaser=WeaponId==TEXT("laser");
    Kick=FMath::Min(Kick+Profile->VisualKick,bLaser ? .3f : 3.f);
    PlayCue(ShotSound,.65f);
    if (MuzzleFX && !bLaser)
    {
        if (UNiagaraComponent* FX=UHeliosWeaponVFX::SpawnOneShot(this,MuzzleFX,
            Weapon->GetFirstPersonMesh()->GetSocketLocation(Profile->MuzzleSocket),
            HolderView->GetComponentRotation(),FVector::OneVector,.7f))
        {
            FX->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
        }
    }
    if (MuzzleLight && !bLaser && Profile->bMuzzleLightEnabled) MuzzleLight->SetIntensity(1200);
    if (!bCompanionSource && (WeaponId==TEXT("rifle") || WeaponId==TEXT("shotgun"))) EjectCasing();
}

void UHeliosFirstPersonPresentationComponent::EjectCasing()
{
    if (Casings.IsEmpty() || !Player || !Weapon) return;
    const int32 I=NextCasing++%Casings.Num(); FCasingMotion& Motion=CasingMotion[I];
    Motion.Age=0; Motion.Position=Weapon->GetFirstPersonMesh()->GetSocketLocation(Profile->EjectSocket);
    const FTransform View=HolderView->GetComponentTransform();
    Motion.Velocity=View.TransformVectorNoScale(FVector(-20,125,90));
    Motion.Rotation=View.Rotator(); Motion.Spin=FVector(370,210,480);
    Casings[I]->SetWorldLocationAndRotation(Motion.Position,Motion.Rotation); Casings[I]->SetVisibility(true);
}

void UHeliosFirstPersonPresentationComponent::OnProjectile(AShooterProjectile* Projectile)
{
    if (!Projectile || !bReady || Profile->bPreserveProjectileVisual) return;
    Projectile->UseExternalPresentation(ImpactFX);
    const TWeakObjectPtr<UWorld> World(GetWorld());
    auto* Impacts=GetWorld()->GetSubsystem<UHeliosImpactPresentationSubsystem>();
    const uint64 Shot=Impacts ? Impacts->BeginShot() : 0;
    if(Impacts&&WeaponId==TEXT("rifle")) Impacts->TrackProjectile(Projectile);
    const FName Id=WeaponId;
    const TWeakObjectPtr<AActor> FlightProjectile(Projectile);
    Projectile->OnPresentationImpact.AddLambda([World,Shot,Id,FlightProjectile](const FHitResult& Hit)
    {
        if (World.IsValid()) if (auto* Feedback=World->GetSubsystem<UHeliosImpactPresentationSubsystem>())
        {
            Feedback->StopProjectile(FlightProjectile.Get(),Hit);
            Feedback->ReportImpact(Id,Shot,Hit);
        }
    });
}

void UHeliosFirstPersonPresentationComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(Dt,TickType,TickFunction);
    if (!bReady || !IsValid(Player) || !IsValid(Weapon) || HeliosWeaponHolderContext::IsDead(Player)) return;
    Clock+=Dt; ShotAge+=Dt; EquipAge+=Dt;
    if(Profile->bPreserveProjectileVisual) Weapon->GetFirstPersonMesh()->SetVisibility(Weapon->GetBulletCount()>0 || bReloading);
    Kick=FMath::FInterpTo(Kick,0.f,Dt,16.f);
    const float Speed=Player->GetVelocity().Size2D();
    SprintBlend=FMath::FInterpTo(SprintBlend,Speed>650 ? 1.f:0.f,Dt,7.f);
    const FRotator View=HolderView->GetComponentRotation();
    const FRotator ViewDelta=(View-PreviousView).GetNormalized(); PreviousView=View;
    const FVector2D TargetSway(FMath::Clamp(ViewDelta.Yaw,-3.f,3.f),FMath::Clamp(ViewDelta.Pitch,-3.f,3.f));
    Sway=FMath::Vector2DInterpTo(Sway,TargetSway,Dt,9.f);
    const float Move=FMath::Clamp(Speed/600.f,0.f,1.f);
    const float Bob=FMath::Sin(Clock*9.f)*Move;
    const float ReloadPhase=GetReloadProgress();
    const float ReloadTilt=bReloading && !Profile->bBakedReloadPose ? FMath::Sin(ReloadPhase*PI) : 0;
    const float RaiseBlend=1.f-FMath::Clamp(EquipAge/.32f,0.f,1.f);
    // Compact reloads are baked into the hands. Shoulder roots stay fixed while
    // the right hand carries the gun and the left hand follows its actual module.
    const FVector Location=Profile->ViewLocation+FVector(-Kick*.9f,Bob*.22f+ReloadTilt*2.f,-FMath::Abs(Bob)*.18f+FMath::Sin(Clock*1.6f)*.09f-SprintBlend*2.f-RaiseBlend*7.f+ReloadTilt*15.f);
    const FRotator Rotation=Profile->ViewRotation+FRotator(Kick*1.1f-SprintBlend*10.f-RaiseBlend*16.f+ReloadTilt*8.f,-Sway.X*.25f,ReloadTilt*-18.f+Bob*.22f+Sway.Y*.15f);
    PoseRoot->SetRelativeLocationAndRotation(Location,Rotation);
    if (bMeleePose) {}
    else if (bReloading)
    {
        ReloadAge=FMath::Min(ReloadAge+Dt,ReloadDuration);
        const float P=GetReloadProgress();
        if (P>=.23f && !(ReloadCues&1)) { ReloadCues|=1; PlayCue(OutSound); }
        if (P>=.8f && !(ReloadCues&2)) { ReloadCues|=2; PlayCue(InSound); }
        if (P>=.9f && bEmptyReload && !(ReloadCues&4)) { ReloadCues|=4; PlayCue(ActionSound); }
        Pose(Reload,GunReload,P,false);
    }
    else if (EquipAge<.32f && Raise) Pose(Raise,nullptr,EquipAge/.32f,false);
    else if (ShotAge<Profile->FireAnimationSeconds && Fire && WeaponId!=TEXT("laser")) Pose(Fire,GunFire,ShotAge/FMath::Max(.01f,Profile->FireAnimationSeconds),false);
    else Pose(Idle,nullptr,Clock/3.f,true);
    if (WeaponId==TEXT("cola"))
    {
        if (bThrowing && ShotAge>=Profile->FireAnimationSeconds) bThrowing=false;
        const bool bRecovering=bThrowing && bThrowReleased && ShotAge<.9f;
        Weapon->GetFirstPersonMesh()->SetVisibility(Weapon->GetBulletCount()>0 && !bRecovering);
    }
    if (MuzzleLight) MuzzleLight->SetIntensity(ShotAge<.04f && WeaponId!=TEXT("laser") && Profile->bMuzzleLightEnabled ? 1200.f*(1-ShotAge/.04f):0);
    for (int32 I=0; I<Casings.Num(); ++I)
    {
        FCasingMotion& M=CasingMotion[I]; if (M.Age>=1.4f) continue;
        M.Age+=Dt; M.Velocity.Z-=980.f*Dt; M.Position+=M.Velocity*Dt;
        M.Rotation+=FRotator(M.Spin.X,M.Spin.Y,M.Spin.Z)*Dt;
        Casings[I]->SetWorldLocationAndRotation(M.Position,M.Rotation);
        if (M.Age>=1.4f) Casings[I]->SetVisibility(false);
    }
}

void UHeliosFirstPersonPresentationComponent::Cancel(bool bHide)
{
    if (!bCompanionSource) if (auto* Impacts=GetWorld()->GetSubsystem<UHeliosImpactPresentationSubsystem>()) Impacts->Clear();
    bMeleePose=false; bReloading=false; bThrowing=false; bThrowReleased=false; ReloadAge=0; ReloadCues=0; Kick=0; ShotAge=100; EquipAge=1;
    if (bReady) Pose(Idle,nullptr,0,true);
    if (MuzzleLight) MuzzleLight->SetIntensity(0);
    for (UStaticMeshComponent* C:Casings) if (C) C->SetVisibility(false);
    for (auto& M:CasingMotion) M.Age=10;
    if (Arms) Arms->SetVisibility(!bHide);
}

void UHeliosFirstPersonPresentationComponent::Unequip()
{
    if (GetWorld()) if (!bCompanionSource) if (auto* Impacts=GetWorld()->GetSubsystem<UHeliosImpactPresentationSubsystem>()) Impacts->Clear();
    if (IsValid(Weapon))
    {
        Weapon->OnShotPresented.Remove(ShotHandle);
        Weapon->OnProjectilePresented.Remove(ProjectileHandle);
        Weapon->SetExternalPresentation(false);
        if (Weapon->GetFirstPersonMesh()) Weapon->GetFirstPersonMesh()->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
    }
    bReady=false; bMeleePose=false; bReloading=false; bThrowing=false; bThrowReleased=false;
    for (UStaticMeshComponent* C:Casings) if (C) C->DestroyComponent();
    Casings.Empty(); CasingMotion.Empty();
    if (MuzzleLight) MuzzleLight->DestroyComponent(); MuzzleLight=nullptr;
    if (Arms) Arms->DestroyComponent(); Arms=nullptr;
    if (PoseRoot) PoseRoot->DestroyComponent(); PoseRoot=nullptr;
    Player=nullptr; HolderView=nullptr; Weapon=nullptr; Profile=nullptr;bCompanionSource=false;
    Idle=nullptr; Reload=nullptr; Fire=nullptr; Raise=nullptr; GunReload=nullptr; GunFire=nullptr;
    MuzzleFX=nullptr; ImpactFX=nullptr; ShotSound=nullptr; OutSound=nullptr; InSound=nullptr; ActionSound=nullptr;
}

void UHeliosFirstPersonPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    Unequip(); Super::EndPlay(Reason);
}

void UHeliosFirstPersonPresentationComponent::BeginThrow()
{
    if(!bReady || bReloading)return;
    bThrowing=true; bThrowReleased=false; ShotAge=0; EquipAge=1;
    Pose(Fire,GunFire,0,false);
}
void UHeliosFirstPersonPresentationComponent::CommitThrow()
{
    bThrowReleased=true;
    if(IsValid(Weapon))Weapon->GetFirstPersonMesh()->SetVisibility(false);
}
void UHeliosFirstPersonPresentationComponent::CancelThrow()
{
    if(!bThrowing)return;
    bThrowing=false; bThrowReleased=false; ShotAge=100;
    if(bReady && IsValid(Weapon))
    {
        Pose(Idle,nullptr,0,true);
        Weapon->GetFirstPersonMesh()->SetVisibility(Weapon->GetBulletCount()>0);
    }
}

void UHeliosFirstPersonPresentationComponent::SetMeleePose(UAnimSequence* Animation, float Progress)
{
    bMeleePose=Animation!=nullptr;
    if(bReady) Pose(Animation?Animation:Idle.Get(),nullptr,Progress,false);
}
FVector UHeliosFirstPersonPresentationComponent::GetMeleeTip() const
{
    return Weapon && Profile ? Weapon->GetFirstPersonMesh()->GetSocketLocation(Profile->MuzzleSocket) : FVector::ZeroVector;
}
