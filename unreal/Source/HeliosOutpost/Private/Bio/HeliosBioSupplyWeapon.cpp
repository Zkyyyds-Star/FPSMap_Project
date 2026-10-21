#include "Bio/HeliosBioSupplyWeapon.h"
#include "Bio/HeliosBioSupplyWeaponDefinition.h"
#include "Bio/HeliosBioSupplyProjectileMovement.h"
#include "Bio/HeliosBioHotelGameMode.h"
#include "Bio/HeliosBioRoundInventoryComponent.h"
#include "QA/HeliosBioSupplyPresentationQA.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "ShooterWeaponHolder.h"

namespace
{
FName ResolveNativeSoundAction(const UHeliosBioSupplyWeaponDefinition* Definition,FName Action)
{
    if(Definition&&Definition->bThrowable&&Action==TEXT("ChangeClip"))
    {
        const auto* Reload=Definition->NativeFirstPersonClips.Find(TEXT("ChangeClip"));
        const auto* Equip=Definition->NativeFirstPersonClips.Find(TEXT("Equip"));
        if(Reload&&Equip&&!Equip->IsNull()&&*Reload==*Equip)return TEXT("Equip");
    }
    return Action;
}
void IgnoreBioFriendlies(AHeliosBioHotelGameMode* Mode,int32 Caster,FCollisionQueryParams& Query)
{
    for(const auto& P:Mode->Model.GetParticipants())if(!Mode->AreOpponents(Caster,P.Id))if(auto* Actor=Mode->ParticipantActor(P.Id))Query.AddIgnoredActor(Actor);
}
void SourceFX(AActor* Owner,const UHeliosBioSupplyWeaponDefinition* Definition,const FString& Name,const FVector& At,const FRotator& Rotation,bool FirstPerson=false)
{if(Definition&&!Name.IsEmpty()&&!Definition->EffectLibrary.IsEmpty())AHeliosSSXEffect::Spawn(Owner,Name,At,Rotation,FirstPerson,1.f,Definition->EffectLibrary);}
}
AHeliosBioSupplyWeapon::AHeliosBioSupplyWeapon(){bFullAuto=false;}
bool AHeliosBioSupplyWeapon::ShouldKeepThirdPersonIdle(UAnimSequence* Source) const
{
    if(!Source||!BioDefinition)return false;
    for(FName Action:BioDefinition->ThirdPersonHoldIdleActions)
        if(const auto* Clip=BioDefinition->NativeFirstPersonClips.Find(Action);Clip&&Clip->Get()==Source)return true;
    return false;
}
void AHeliosBioSupplyWeapon::ApplyDefinition(const UHeliosWeaponDefinition* Definition)
{
    Super::ApplyDefinition(Definition);BioDefinition=Cast<UHeliosBioSupplyWeaponDefinition>(ActiveDefinition);
    if(!BioDefinition)return;
    bFullAuto=BioDefinition->FireType==EHeliosBioSupplyFireType::Hitscan;
    GetFirstPersonMesh()->SetSkeletalMesh(BioDefinition->NativeFirstPersonMesh.LoadSynchronous());
    GetFirstPersonMesh()->SetRelativeTransform(BioDefinition->NativeFirstPersonTransform);
    GetFirstPersonMesh()->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    GetFirstPersonMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    SetExternalPresentation(true,BioDefinition->NativeMuzzleSocket);
    FirstPersonModel->SetVisibility(false);ThirdPersonModel->SetVisibility(false);
    SetNativePose(TEXT("Idle"),true);
}
void AHeliosBioSupplyWeapon::SetNativePose(FName Action,bool Loop,float Duration)
{
    if(!BioDefinition)return;
    const auto* Ref=BioDefinition->NativeFirstPersonClips.Find(Action);auto* Clip=Ref?Ref->LoadSynchronous():nullptr;
    if(!Clip||!GetFirstPersonMesh()->GetSkeletalMeshAsset()||Clip->GetSkeleton()!=GetFirstPersonMesh()->GetSkeletalMeshAsset()->GetSkeleton())return;
    if(Loop&&NativeClip==Clip&&NativeActionRemaining<=0)return;
    NativeClip=Clip;GetFirstPersonMesh()->PlayAnimation(Clip,Loop);
    GetFirstPersonMesh()->SetPlayRate(Duration>0?Clip->GetPlayLength()/Duration:1.f);
    NativeActionRemaining=Loop?0:Duration>0?Duration:Clip->GetPlayLength();
    NativeSoundAction=ResolveNativeSoundAction(BioDefinition,Action);NativeSoundAge=0;NativeSoundRate=Duration>0?Clip->GetPlayLength()/Duration:1.f;PlayedSourceCues.Reset();
    if(!Loop)TickSourceAudio(0);
}
bool AHeliosBioSupplyWeapon::HasSourceAudioCues(FName Action) const
{
    if(!BioDefinition)return false;const auto* Pawn=Cast<APawn>(GetOwner());const bool FP=Pawn&&Pawn->IsPlayerControlled();
    const FName SourceAction=ResolveNativeSoundAction(BioDefinition,Action);
    return BioDefinition->SourceAudioCues.ContainsByPredicate([&](const auto& Cue){return Cue.Action==SourceAction&&Cue.bFirstPerson==FP;});
}
void AHeliosBioSupplyWeapon::TickSourceAudio(float Dt)
{
    if(!BioDefinition||!GetOwner())return;NativeSoundAge+=Dt*NativeSoundRate;
    const auto* Pawn=Cast<APawn>(GetOwner());const bool FP=Pawn&&Pawn->IsPlayerControlled();
    for(int32 I=0;I<BioDefinition->SourceAudioCues.Num();++I)
    {
        const auto& Cue=BioDefinition->SourceAudioCues[I];
        if(Cue.Action!=NativeSoundAction||Cue.bFirstPerson!=FP||PlayedSourceCues.Contains(I)||Cue.NativeSeconds>NativeSoundAge)continue;
        PlayedSourceCues.Add(I);if(auto* Sound=Cue.Sound.LoadSynchronous())
        {UHeliosBioSupplyPresentationQA::RecordSoundRequest(this,Sound,NativeSoundAction,Cue.NativeSeconds,NativeSoundAge,FP,TEXT("original_clip_cue"));UGameplayStatics::PlaySoundAtLocation(this,Sound,GetOwner()->GetActorLocation());}
    }
}
void AHeliosBioSupplyWeapon::ActivateWeapon(const FName& Tag)
{
    Super::ActivateWeapon(Tag);bNativeEquipped=true;bNativeReload=false;
    bThrownPropHidden=BioDefinition&&BioDefinition->bThrowable&&CurrentBullets<=0;UpdateThrowableVisual();
    if(BioDefinition)GetFirstPersonMesh()->SetRelativeTransform(BioDefinition->NativeFirstPersonTransform);
    SetNativePose(TEXT("Equip"),false);PlaySourceActionSound(TEXT("Equip"));
}
void AHeliosBioSupplyWeapon::PlaySourceActionSound(FName Action)
{
    if(!BioDefinition||!GetOwner()||HasSourceAudioCues(Action))return;
    const auto* Pawn=Cast<APawn>(GetOwner());const bool FP=Pawn&&Pawn->IsPlayerControlled();
    const FName Key(*(FString(FP?TEXT("FP_"):TEXT("TP_"))+Action.ToString()));
    const auto* Ref=BioDefinition->SourceActionSounds.Find(Key);
    auto* Sound=Ref?Ref->LoadSynchronous():Action==TEXT("ChangeClip")?BioDefinition->ReloadSound.LoadSynchronous():nullptr;
    if(Sound){UHeliosBioSupplyPresentationQA::RecordSoundRequest(this,Sound,Action,-1,NativeSoundAge,FP,TEXT("explicit_local_action_fallback"));UGameplayStatics::PlaySoundAtLocation(this,Sound,GetOwner()->GetActorLocation());}
}
void AHeliosBioSupplyWeapon::DeactivateWeapon()
{CancelPendingThrow(TEXT("Deactivated"));bNativeEquipped=false;bNativeReload=false;NativeActionRemaining=0;NativeSoundAction=NAME_None;Super::DeactivateWeapon();}
bool AHeliosBioSupplyWeapon::CancelPendingThrow(const TCHAR* Reason)
{
    if(!bPendingThrow)return false;
    bPendingThrow=bThrowPreparing=false;ThrowReleaseRemaining=0;PendingThrowRound=INDEX_NONE;
    StopFiring();NativeActionRemaining=0;NativeSoundAction=NAME_None;PlayedSourceCues.Reset();
    bThrownPropHidden=BioDefinition&&BioDefinition->bThrowable&&CurrentBullets<=0;UpdateThrowableVisual();
    SetNativePose(TEXT("Idle"),true);
    UE_LOG(LogTemp,Display,TEXT("BIO_SUPPLY_THROW_CANCEL item=%d reason=%s clip=%d"),BioDefinition?BioDefinition->OriginalItemId:0,Reason,CurrentBullets);
    return true;
}
void AHeliosBioSupplyWeapon::UpdateThrowableVisual()
{
    if(!BioDefinition||!BioDefinition->bThrowable||!GetOwner())return;
    if(auto* Presentation=GetOwner()->FindComponentByClass<UHeliosCharacterPresentationComponent>())
        if(Presentation->GetPresentedWeaponId()==BioDefinition->WeaponId)
            if(auto* Gun=Presentation->GetWeaponMesh())Gun->SetVisibility(!bThrownPropHidden,true);
}
void AHeliosBioSupplyWeapon::Tick(float Dt)
{
    // Cancel before advancing source audio or the Prepare/Fire release clock.
    // A brief freeze or a new round cannot resume the old pending attack.
    if(bPendingThrow)
    {
        const auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());
        const auto* Participant=Mode?Mode->Model.FindParticipant(Mode->ParticipantId(GetOwner())):nullptr;
        if(!bNativeEquipped||!Participant||!Participant->bAlive||Participant->Team!=EHeliosBioTeam::Human||Participant->FrozenSeconds>0||
            Mode->RoundSerial!=PendingThrowRound||Mode->Model.GetPhase()!=EHeliosBioPhase::Active||Mode->bHunterChooserOpen)
            CancelPendingThrow(TEXT("OwnerOrRoundStateChanged"));
    }
    Super::Tick(Dt);if(!bNativeEquipped||!BioDefinition||GetWorld()->IsPaused())return;TickSourceAudio(Dt);
    if(bPendingThrow)
    {
        if(bThrowPreparing)
        {
            if(NativeActionRemaining<=Dt)
            {
                // The two motions are original. Automatically entering Fire
                // when Prepare ends is the explicitly documented offline policy.
                bThrowPreparing=false;SetNativePose(TEXT("Fire"),false);
                ThrowReleaseRemaining=FMath::Max(.01f,BioDefinition->ThrowReleaseSeconds);
            }
        }
        else ThrowReleaseRemaining-=Dt;
        if(!bThrowPreparing&&ThrowReleaseRemaining<=0)
        {
            bPendingThrow=false;bReleasingThrow=true;
            const int32 Before=CurrentBullets;bShotCommitted=false;
            // The actual animated carrier is sampled by GetNativeMuzzle at this
            // release, rather than launching from the idle pose or camera.
            FireProjectile(WeaponOwner?WeaponOwner->GetWeaponTargetLocation():PendingThrowTarget);
            if(bShotCommitted&&CurrentBullets<Before)OnShotPresented.Broadcast();
            bReleasingThrow=false;
        }
    }
    NativeActionRemaining=FMath::Max(0.f,NativeActionRemaining-Dt);
    UpdateThrowableVisual();
    if(NativeActionRemaining<=0&&!bNativeReload)
    {const auto* OwnerCharacter=Cast<ACharacter>(GetOwner());SetNativePose(OwnerCharacter&&OwnerCharacter->GetVelocity().Size2D()>10?TEXT("Move"):TEXT("Idle"),true);}
}
void AHeliosBioSupplyWeapon::BeginReload(float Seconds)
{CancelPendingThrow(TEXT("Reload"));bThrownPropHidden=false;UpdateThrowableVisual();Super::BeginReload(Seconds);bNativeReload=true;SetNativePose(TEXT("ChangeClip"),false,Seconds);PlaySourceActionSound(TEXT("ChangeClip"));}
void AHeliosBioSupplyWeapon::CancelReload()
{CancelPendingThrow(TEXT("CombatCancelled"));Super::CancelReload();bNativeReload=false;NativeActionRemaining=0;NativeSoundAction=NAME_None;bThrownPropHidden=BioDefinition&&BioDefinition->bThrowable&&CurrentBullets<=0;UpdateThrowableVisual();}
bool AHeliosBioSupplyWeapon::GetNativeMuzzle(FVector& Position) const
{
    if(!BioDefinition||!BioDefinition->bMuzzleBridgeVerified)return false;
    const auto* Pawn=Cast<APawn>(GetOwner());if(!Pawn)return false;
    if(Pawn->IsPlayerControlled())
    {
        auto* Mesh=GetFirstPersonMesh();if(!Mesh->DoesSocketExist(BioDefinition->NativeMuzzleSocket))return false;
        Position=Mesh->GetSocketLocation(BioDefinition->NativeMuzzleSocket);return !Position.ContainsNaN();
    }
    const auto* Presentation=GetOwner()->FindComponentByClass<UHeliosCharacterPresentationComponent>();
    auto* Body=Presentation?Presentation->GetArmsMesh():nullptr;
    if(!Body||!Presentation->GetWeaponMesh()||!Body->DoesSocketExist(BioDefinition->ThirdPersonMuzzleBone))return false;
    if(auto* Gun=Presentation->GetWeaponMesh();Presentation->GetPresentedWeaponId()==BioDefinition->WeaponId&&Gun->DoesSocketExist(BioDefinition->NativeMuzzleSocket))
    {Position=Gun->GetSocketLocation(BioDefinition->NativeMuzzleSocket);return !Position.ContainsNaN();}
    auto* BodyAsset=Body->GetSkeletalMeshAsset();if(!BodyAsset)return false;
    const auto& Reference=BodyAsset->GetRefSkeleton();const int32 Anchor=Reference.FindBoneIndex(BioDefinition->ThirdPersonMuzzleBone);
    if(Anchor==INDEX_NONE)return false;
    FTransform AnchorReference=FTransform::Identity;
    for(int32 Index=Anchor;Index!=INDEX_NONE;Index=Reference.GetParentIndex(Index))AnchorReference=AnchorReference*Reference.GetRefBonePose()[Index];
    const FVector ReferenceScale=AnchorReference.GetScale3D();if(ReferenceScale.ContainsNaN()||ReferenceScale.GetMin()<=SMALL_NUMBER)return false;
    // The measured original locator is already in centimetres. Compensate
    // only the saved FBX reference-unit scale; the actual animated/reference
    // scale ratio (including mirrored poses) and owner scale remain intact.
    const FVector BoneLocal=BioDefinition->ThirdPersonMuzzleRelativeToBone.GetTranslation()/ReferenceScale;
    Position=Body->GetSocketTransform(BioDefinition->ThirdPersonMuzzleBone).TransformPosition(BoneLocal);
    return !Position.ContainsNaN();
}
bool AHeliosBioSupplyWeapon::CanReachNativeTarget(const FVector& Target) const
{
    if(!BioDefinition||Target.ContainsNaN())return false;
    FVector Muzzle;if(!GetNativeMuzzle(Muzzle)||FVector::DistSquared(Muzzle,Target)>FMath::Square(BioDefinition->Range))return false;
    if(BioDefinition->FireType==EHeliosBioSupplyFireType::Hitscan)return true;
    FVector Velocity;return HeliosBioSupplyBallistics::SolveNativeLaunch(Muzzle,Target,BioDefinition->ProjectileSpeed,
        FMath::Max(BioDefinition->ProjectileSpeed,BioDefinition->MaximumProjectileSpeed),BioDefinition->ProjectileAcceleration,
        GetWorld()->GetGravityZ()*BioDefinition->ProjectileGravityScale,BioDefinition->ProjectileLifetime,Velocity);
}
void AHeliosBioSupplyWeapon::FireProjectile(const FVector& Target)
{
    auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());const int32 Caster=Mode?Mode->ParticipantId(GetOwner()):INDEX_NONE;
    const auto* P=Mode?Mode->Model.FindParticipant(Caster):nullptr;
    if(!bNativeEquipped||bNativeReload||CurrentBullets<=0||!BioDefinition||!BioDefinition->IsReady()||!P||!P->bAlive||P->Team!=EHeliosBioTeam::Human||P->FrozenSeconds>0||Mode->Model.GetPhase()!=EHeliosBioPhase::Active||Mode->bHunterChooserOpen||GetWorld()->IsPaused())return;
    FVector Muzzle;if(!GetNativeMuzzle(Muzzle))return;
    FVector Eye;FRotator Aim;PawnOwner->GetActorEyesViewPoint(Eye,Aim);
    FCollisionQueryParams Q(SCENE_QUERY_STAT(BioSupplyNativeShot),true,GetOwner());Q.AddIgnoredActor(this);IgnoreBioFriendlies(Mode,Caster,Q);
    FHitResult ViewHit,NearHit,Impact;
    const FVector End=Eye+(Target-Eye).GetSafeNormal()*FMath::Max(1.f,BioDefinition->Range);
    const bool ViewBlocked=GetWorld()->LineTraceSingleByChannel(ViewHit,Eye,End,ECC_Visibility,Q);
    const FVector TargetPoint=ViewBlocked?ViewHit.ImpactPoint:End;
    FVector Direction=(TargetPoint-Muzzle).GetSafeNormal(SMALL_NUMBER,Aim.Vector());
    const bool MuzzleBlocked=GetWorld()->SweepSingleByChannel(NearHit,Eye,Muzzle,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(3.f),Q);
    const bool Hitscan=BioDefinition->FireType==EHeliosBioSupplyFireType::Hitscan;
    if(!Hitscan&&!PawnOwner->IsPlayerControlled()&&!MuzzleBlocked)
    {
        // AI aims its real gravity-driven projectile using the same editable
        // launch speed/gravity as flight. Player aiming remains direct input.
        FVector Velocity;if(!HeliosBioSupplyBallistics::SolveNativeLaunch(Muzzle,TargetPoint,BioDefinition->ProjectileSpeed,
            FMath::Max(BioDefinition->ProjectileSpeed,BioDefinition->MaximumProjectileSpeed),BioDefinition->ProjectileAcceleration,
            GetWorld()->GetGravityZ()*BioDefinition->ProjectileGravityScale,BioDefinition->ProjectileLifetime,Velocity))return;
        Direction=Velocity.GetSafeNormal();
    }
    if(BioDefinition->bThrowable&&!bReleasingThrow)
    {
        if(bPendingThrow)return;
        bPendingThrow=true;PendingThrowTarget=Target;PendingThrowRound=Mode->RoundSerial;ThrowReleaseRemaining=FMath::Max(.01f,BioDefinition->ThrowReleaseSeconds);
        const auto* Ref=BioDefinition->NativeFirstPersonClips.Find(TEXT("Prepare"));auto* Prepare=Ref?Ref->LoadSynchronous():nullptr;
        bThrowPreparing=Prepare&&Prepare->GetPlayLength()>0;SetNativePose(bThrowPreparing?TEXT("Prepare"):TEXT("Fire"),false);
        if(bThrowPreparing)PlaySourceActionSound(TEXT("Prepare")); // TP fallback starts locally; FP retains its original timed cue.
        UE_LOG(LogTemp,Display,TEXT("BIO_SUPPLY_THROW_BEGIN item=%d participant=%d release_seconds=%.4f timing_scope=%s"),BioDefinition->OriginalItemId,Caster,ThrowReleaseRemaining,*BioDefinition->ThrowReleaseScope);return;
    }
    if(Hitscan)
    {
        if(MuzzleBlocked)Impact=NearHit;else GetWorld()->LineTraceSingleByChannel(Impact,Muzzle,TargetPoint+Direction*2.f,ECC_Visibility,Q);
        if(Impact.bBlockingHit)
        {
            LastNativeImpact=Impact;
            const int32 Victim=Mode->ParticipantId(Impact.GetActor());
            if(Mode->AreOpponents(Caster,Victim))
            {
                const float Distance=FVector::Dist(Muzzle,Impact.ImpactPoint);
                const float Falloff=FMath::Clamp((Distance-BioDefinition->FalloffStart)/FMath::Max(1.f,BioDefinition->Range-BioDefinition->FalloffStart),0.f,1.f);
                const float Damage=FMath::Lerp(BioDefinition->Damage,BioDefinition->MinimumDamage,Falloff);
                if(UGameplayStatics::ApplyPointDamage(Impact.GetActor(),Damage,Direction,Impact,PawnOwner->GetController(),this,UDamageType::StaticClass())>0)++NativeHits;
            }
            SourceFX(this,BioDefinition,BioDefinition->ImpactEffectName,Impact.ImpactPoint,Impact.ImpactNormal.Rotation());
        }
        if(!MuzzleBlocked&&!BioDefinition->NativeTracerMaterial.IsNull())
        {
            FActorSpawnParameters Params;Params.Owner=this;
            if(auto* Tracer=GetWorld()->SpawnActor<AHeliosBioSupplyTracer>(Params))
                if(!Tracer->Initialize(BioDefinition,Muzzle,Impact.bBlockingHit?Impact.ImpactPoint:TargetPoint,PawnOwner->IsPlayerControlled()))Tracer->Destroy();
        }
    }
    else
    {
        const FVector Spawn=MuzzleBlocked?NearHit.Location-Direction*4.f:Muzzle;
        const FTransform Transform(Direction.Rotation(),Spawn);
        auto* Projectile=GetWorld()->SpawnActorDeferred<AHeliosBioSupplyProjectile>(AHeliosBioSupplyProjectile::StaticClass(),Transform,this,PawnOwner,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Projectile)return;
        Projectile->Initialize(BioDefinition,Caster,Mode->RoundSerial,Direction);
        UGameplayStatics::FinishSpawningActor(Projectile,Transform);
    }
    bShotCommitted=true;--CurrentBullets;++NativeShots;WeaponOwner->UpdateWeaponHUD(CurrentBullets,MagazineSize);WeaponOwner->AddWeaponRecoil(FiringRecoil);
    if(BioDefinition->bThrowable){bThrownPropHidden=true;UpdateThrowableVisual();}
    if(!bReleasingThrow)SetNativePose(TEXT("Fire"),false);
    const bool FirstPerson=PawnOwner->IsPlayerControlled();
    auto* Sound=(!FirstPerson&&!BioDefinition->ThirdPersonFireSound.IsNull()?BioDefinition->ThirdPersonFireSound:BioDefinition->FireSound).LoadSynchronous();
    if(Sound&&!HasSourceAudioCues(TEXT("Fire"))){UHeliosBioSupplyPresentationQA::RecordSoundRequest(this,Sound,TEXT("Fire"),-1,NativeSoundAge,FirstPerson,TEXT("native_fire"));UGameplayStatics::PlaySoundAtLocation(this,Sound,Muzzle);}
    const FString& Flash=FirstPerson||BioDefinition->ThirdPersonMuzzleEffectName.IsEmpty()?BioDefinition->MuzzleEffectName:BioDefinition->ThirdPersonMuzzleEffectName;
    SourceFX(this,BioDefinition,Flash,Muzzle,Direction.Rotation(),FirstPerson);
    UE_LOG(LogTemp,Display,TEXT("BIO_SUPPLY_NATIVE_SHOT item=%d participant=%d serial=%d muzzle=%s obstruction=%s clip=%d"),BioDefinition->OriginalItemId,Caster,NativeShots,*Muzzle.ToCompactString(),*GetNameSafe(Hitscan?Impact.GetActor():NearHit.GetActor()),CurrentBullets);
}
AHeliosBioSupplyProjectile::AHeliosBioSupplyProjectile()
{
    PrimaryActorTick.bCanEverTick=true;
    Collision=CreateDefaultSubobject<USphereComponent>(TEXT("NativeProjectileCollision"));SetRootComponent(Collision);Collision->InitSphereRadius(3.f);Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));Collision->SetNotifyRigidBodyCollision(true);Collision->SetCanEverAffectNavigation(false);
    Collision->OnComponentHit.AddDynamic(this,&AHeliosBioSupplyProjectile::OnNativeHit);
    Visual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("OriginalProjectileVisual"));Visual->SetupAttachment(Collision);Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);Visual->SetCanEverAffectNavigation(false);
    Movement=CreateDefaultSubobject<UHeliosBioSupplyProjectileMovement>(TEXT("NativeProjectileMovement"));Movement->SetUpdatedComponent(Collision);Movement->bRotationFollowsVelocity=true;Movement->bSweepCollision=true;Movement->bInitialVelocityInLocalSpace=false;
    // Keep the explicit actor-before-movement lifetime check without the default
    // inverse MovementComponent owner prerequisite creating a tick cycle.
    Movement->bTickBeforeOwner=false;Movement->AddTickPrerequisiteActor(this);
}
void AHeliosBioSupplyProjectile::Initialize(UHeliosBioSupplyWeaponDefinition* Definition,int32 Participant,int32 Round,const FVector& Direction)
{
    BioDefinition=Definition;LaunchParticipant=Participant;LaunchRound=Round;BirthLocation=GetActorLocation();
    Movement->InitialSpeed=FMath::Max(1.f,Definition->ProjectileSpeed);Movement->MaxSpeed=FMath::Max(Movement->InitialSpeed,Definition->MaximumProjectileSpeed);Movement->Velocity=Direction.GetSafeNormal()*Movement->InitialSpeed;Movement->ProjectileGravityScale=Definition->ProjectileGravityScale;
    CastChecked<UHeliosBioSupplyProjectileMovement>(Movement)->ForwardAcceleration=Definition->ProjectileAcceleration;
    Visual->SetStaticMesh(Definition->NativeProjectileMesh.LoadSynchronous());
    Collision->IgnoreActorWhenMoving(GetOwner(),true);Collision->IgnoreActorWhenMoving(GetInstigator(),true);
    if(auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld()))for(const auto& P:Mode->Model.GetParticipants())if(!Mode->AreOpponents(Participant,P.Id))if(auto* Actor=Mode->ParticipantActor(P.Id))Collision->IgnoreActorWhenMoving(Actor,true);
    if(!Definition->TrailEffectName.IsEmpty()&&!Definition->EffectLibrary.IsEmpty())
    {
        Trail=AHeliosSSXEffect::Spawn(this,Definition->TrailEffectName,GetActorLocation(),Direction.Rotation(),false,1.f,Definition->EffectLibrary);
        if(Trail)
        {
            // Offline lifetime bridge: repeat the unchanged original preview
            // cycle until this real collision-driven projectile ends.
            Trail->KeepTravelingEmitterUntilOwnerEnds();
        }
    }
}
bool AHeliosBioSupplyProjectile::IsLaunchValid() const
{
    const auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());const auto* P=Mode?Mode->Model.FindParticipant(LaunchParticipant):nullptr;
    return BioDefinition&&Mode&&Mode->RoundSerial==LaunchRound&&Mode->Model.GetPhase()==EHeliosBioPhase::Active&&P&&P->bAlive&&P->Team==EHeliosBioTeam::Human;
}
void AHeliosBioSupplyProjectile::Tick(float Dt)
{
    Super::Tick(Dt);if(bDetonated)return;if(!IsLaunchValid()){Destroy();return;}Age+=Dt;
    if(Trail)Trail->SetActorLocationAndRotation(GetActorLocation(),GetActorRotation());
    if(Age>=BioDefinition->ProjectileLifetime||FVector::Dist(BirthLocation,GetActorLocation())>=BioDefinition->Range)Detonate(GetActorLocation(),FVector::UpVector);
}
void AHeliosBioSupplyProjectile::OnNativeHit(UPrimitiveComponent*,AActor*,UPrimitiveComponent*,FVector,const FHitResult& Hit)
{if(auto* Source=Cast<AHeliosBioSupplyWeapon>(GetOwner()))Source->LastNativeImpact=Hit;Detonate(Hit.ImpactPoint,Hit.ImpactNormal);}
void AHeliosBioSupplyProjectile::Detonate(const FVector& At,const FVector& Normal)
{
    if(bDetonated||!IsLaunchValid()){Destroy();return;}bDetonated=true;
    auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());const float Radius=FMath::Max(1.f,BioDefinition->ExplosionRadius);int32 Affected=0;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(BioSupplyExplosionLOS),true,this);Q.AddIgnoredActor(GetOwner());IgnoreBioFriendlies(Mode,LaunchParticipant,Q);
    for(const auto& P:Mode->Model.GetParticipants())
    {
        if(!P.bAlive||!Mode->AreOpponents(LaunchParticipant,P.Id))continue;
        auto* Actor=Mode->ParticipantActor(P.Id);if(!Actor)continue;
        const FVector Center=Actor->GetActorLocation();const float Distance=FVector::Dist(At,Center);if(Distance>Radius)continue;
        FHitResult Occlusion;if(GetWorld()->LineTraceSingleByChannel(Occlusion,At+Normal*5.f,Center,ECC_Visibility,Q)&&Occlusion.GetActor()!=Actor)continue;
        if(BioDefinition->FireType==EHeliosBioSupplyFireType::ConcussionProjectile)
        {if(P.Team==EHeliosBioTeam::Zombie&&Mode->Model.FreezeParticipant(P.Id,LaunchParticipant,BioDefinition->ConcussionSeconds))++Affected;}
        else
        {const float Damage=FMath::Lerp(BioDefinition->Damage,BioDefinition->MinimumDamage,FMath::Clamp(Distance/Radius,0.f,1.f));if(Mode->DamageParticipant(P.Id,LaunchParticipant,Damage))++Affected;}
    }
    // The actual projectile ends now. Its surviving weapon owns the independent
    // world-space source explosion so the original cycle can finish naturally.
    SourceFX(GetOwner(),BioDefinition,BioDefinition->ImpactEffectName,At,Normal.Rotation());
    if(auto* Source=Cast<AHeliosBioSupplyWeapon>(GetOwner()))Source->NativeHits+=Affected;
    if(auto* Sound=BioDefinition->ActionSound.LoadSynchronous())UGameplayStatics::PlaySoundAtLocation(this,Sound,At);
    UE_LOG(LogTemp,Display,TEXT("BIO_SUPPLY_PROJECTILE_IMPACT item=%d participant=%d affected=%d age=%.4f position=%s"),BioDefinition->OriginalItemId,LaunchParticipant,Affected,Age,*At.ToCompactString());Destroy();
}
void AHeliosBioSupplyProjectile::EndPlay(const EEndPlayReason::Type Reason)
{if(IsValid(Trail))Trail->Destroy();Super::EndPlay(Reason);}
