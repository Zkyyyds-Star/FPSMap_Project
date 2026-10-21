#include "Player/HeliosGuanXiaoYuAbilityComponent.h"
#include "Player/HeliosArenaCharacter.h"
#include "Player/HeliosCharacterDefinition.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Pets/HeliosHumanoidPet.h"
#include "Weapons/HeliosFirstPersonPresentationComponent.h"
#include "Weapons/HeliosCalamityStaff.h"
#include "Weapons/HeliosSunShangXiang.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Weapons/HeliosZhongXiaoKui.h"
#include "Weapons/HeliosWangZheKuiThunder.h"
#include "Combat/CombatDamageSettings.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "ShooterWeapon.h"
#include "EngineUtils.h"

UHeliosGuanXiaoYuAbilityComponent::UHeliosGuanXiaoYuAbilityComponent()
{PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickGroup=TG_PostUpdateWork;}
void UHeliosGuanXiaoYuAbilityComponent::BeginPlay()
{
    Super::BeginPlay();FString Contents;FConfigFile Config;
    AddTickPrerequisiteActor(GetOwner());
    if(auto* Character=Cast<ACharacter>(GetOwner()))AddTickPrerequisiteComponent(Character->GetCharacterMovement());
    const FString File=UCombatDamageSettings::Directory()/TEXT("关小雨.ini");
    if(FFileHelper::LoadFileToString(Contents,*File))Config.ProcessInputFileContents(Contents,File);
    const TCHAR* Section=Cast<AHeliosHumanoidPet>(GetOwner())?TEXT("伙伴"):TEXT("玩家");
    auto Read=[&](const TCHAR* Key,float Default,float Min,float Max){FString Raw;Config.GetString(Section,Key,Raw);Raw.TrimStartAndEndInline();const double Value=FCString::Atod(*Raw);return Raw.IsNumeric()&&FMath::IsFinite(Value)&&Value>=Min&&Value<=Max?float(Value):Default;};
    LightDamage=Read(TEXT("轻击伤害"),60,0,100000);HeavyDamage=Read(TEXT("重击伤害"),120,0,100000);
    Range=Read(TEXT("攻击距离"),260,10,1000);SweepRadius=Read(TEXT("判定半径"),40,1,200);
    ShieldDuration=Read(TEXT("护盾持续秒"),2,.01f,60);ShieldCooldown=Read(TEXT("护盾冷却秒"),10,.01f,600);ShieldAngle=Read(TEXT("护盾正面角度"),120,1,180);
}
double UHeliosGuanXiaoYuAbilityComponent::Now()const{return GetWorld()?GetWorld()->GetTimeSeconds():0;}
AShooterWeapon* UHeliosGuanXiaoYuAbilityComponent::Primary()const
{if(auto* P=Cast<AHeliosArenaCharacter>(GetOwner()))return P->GetEquippedWeapon();if(auto* P=Cast<AHeliosHumanoidPet>(GetOwner()))return P->GetEquippedWeapon();return nullptr;}
UHeliosCharacterPresentationComponent* UHeliosGuanXiaoYuAbilityComponent::Presentation()const
{return GetOwner()?GetOwner()->FindComponentByClass<UHeliosCharacterPresentationComponent>():nullptr;}
bool UHeliosGuanXiaoYuAbilityComponent::CanUse()const
{
    if(!GetWorld()||GetWorld()->IsPaused()||!IsValid(Primary()))return false;
    FHeliosLoadout L;
    if(auto* P=Cast<AHeliosArenaCharacter>(GetOwner())){if(P->IsDead()||P->IsReloading())return false;L=P->GetLoadout();}
    else if(auto* Pet=Cast<AHeliosHumanoidPet>(GetOwner())){if(Pet->IsSleeping()||Pet->State==EHeliosPetState::Waking||Pet->IsReloading()||(Pet->CompanionOwner.IsValid()&&Pet->CompanionOwner->IsDead()))return false;L=Pet->GetLoadout();}
    else return false;
    if(L.CharacterId!=TEXT("cfm_guanxiaoyu")||L.PrimaryWeaponId==TEXT("mech")||L.PrimaryWeaponId==TEXT("fighter"))return false;
    if(auto* W=Cast<AHeliosZhongXiaoKuiWeapon>(Primary());W&&W->IsChangingForm())return false;
    if(auto* W=Cast<AHeliosWangZheKuiThunderWeapon>(Primary());W&&W->IsSwitchingMode())return false;
    if(auto* S=UHeliosCalamityState::Get(GetOwner(),false);S&&S->Deployed())return false;
    return true;
}
UAnimSequence* UHeliosGuanXiaoYuAbilityComponent::Clip(FName Name)const
{if(!Profile)return nullptr;const auto& Clips=bThirdPerson?Profile->ThirdPersonClips:Profile->FirstPersonClips;const auto* C=Clips.Find(Name);return C?C->LoadSynchronous():nullptr;}
bool UHeliosGuanXiaoYuAbilityComponent::Enter()
{
    if(!CanUse()){LastError=TEXT("当前状态无法切伞");return false;}
    bThirdPerson=Cast<AHeliosHumanoidPet>(GetOwner())!=nullptr;
    const auto L=bThirdPerson?CastChecked<AHeliosHumanoidPet>(GetOwner())->GetLoadout():CastChecked<AHeliosArenaCharacter>(GetOwner())->GetLoadout();
    const auto* D=FHeliosCharacterRegistry::FindCharacter(L.CharacterId);const auto* F=D?D->FindForm(L.CharacterVariantId):nullptr;
    auto* NewProfile=F?F->UmbrellaProfile.LoadSynchronous():nullptr;
    auto* Model=NewProfile?(bThirdPerson?NewProfile->ThirdPersonMesh:NewProfile->FirstPersonMesh).LoadSynchronous():nullptr;
    if(!Model||!Model->GetSkeleton()){LastError=TEXT("关小雨专属伞素材加载失败");return false;}
    const auto& Clips=bThirdPerson?NewProfile->ThirdPersonClips:NewProfile->FirstPersonClips;
    for(FName Name:{FName("Idle"),FName("Light"),FName("Heavy")})
    {const auto* Path=Clips.Find(Name);auto* A=Path?Path->LoadSynchronous():nullptr;if(!A||A->GetSkeleton()!=Model->GetSkeleton()||A->GetPlayLength()<=0){LastError=TEXT("关小雨专属伞动作不兼容");return false;}}
    Profile=NewProfile;Variant=F->VariantId;
    if(!VisibleMesh){VisibleMesh=NewObject<USkeletalMeshComponent>(GetOwner(),TEXT("GuanXiaoYuUmbrella"));VisibleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);VisibleMesh->SetGenerateOverlapEvents(false);VisibleMesh->SetCanEverAffectNavigation(false);VisibleMesh->RegisterComponent();}
    VisibleMesh->SetSkeletalMesh(Model);VisibleMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);VisibleMesh->SetOnlyOwnerSee(!bThirdPerson);
    VisibleMesh->SetFirstPersonPrimitiveType(bThirdPerson?EFirstPersonPrimitiveType::None:EFirstPersonPrimitiveType::FirstPerson);
    VisibleMesh->SetComponentTickEnabled(false);VisibleMesh->SetBoundsScale(4);
    VisibleMesh->bEnableUpdateRateOptimizations=false;
    VisibleMesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    bResetPoseHistory=true;
    if(bThirdPerson){auto* Body=CastChecked<ACharacter>(GetOwner())->GetMesh();VisibleMesh->AttachToComponent(Body->GetAttachParent(),FAttachmentTransformRules::KeepRelativeTransform);VisibleMesh->SetRelativeTransform(Body->GetRelativeTransform());}
    else{auto* Camera=GetOwner()->FindComponentByClass<UCameraComponent>();if(!Camera){LastError=TEXT("第一人称摄像机不可用");return false;}VisibleMesh->AttachToComponent(Camera,FAttachmentTransformRules::KeepRelativeTransform);VisibleMesh->SetRelativeTransform(Profile->FirstPersonTransform);}
    Primary()->StopFiring();Primary()->StopSecondaryFire();
    // Existing weapons use this cancellation hook for pending one-shot contacts,
    // throws and native animation timelines. CanUse already excludes reloads.
    Primary()->CancelReload();
    if(auto* P=Presentation())P->SetUmbrellaOverride(true);
    TArray<AActor*> Actors;Primary()->GetAttachedActors(Actors,true,true);Actors.Add(Primary());
    for(auto* A:Actors)if(IsValid(A)){HiddenActors.Add(A,A->IsHidden());A->SetActorHiddenInGame(true);}
    if(auto* P=GetOwner()->FindComponentByClass<UHeliosFirstPersonPresentationComponent>())
        if(auto* Driver=P->GetArmsMesh()){HiddenDriver=Driver;bDriverWasVisible=Driver->IsVisible();Driver->SetVisibility(false);}
    bUsingUmbrella=true;Action=NAME_None;bContactCommitted=true;VisibleMesh->SetVisibility(true);LastError.Empty();return true;
}
bool UHeliosGuanXiaoYuAbilityComponent::ToggleUmbrella()
{if(!GetWorld()||GetWorld()->IsPaused())return false;if(bUsingUmbrella){Cancel();return true;}return Enter();}
void UHeliosGuanXiaoYuAbilityComponent::Cancel(bool Restore)
{
    bLightHeld=false;Action=NAME_None;bContactCommitted=true;ShieldUntil=0;bUsingUmbrella=false;
    if(VisibleMesh)VisibleMesh->SetVisibility(false);ShowShield(false);
    for(auto Effect:Effects)if(IsValid(Effect.Get()))Effect->Destroy();Effects.Empty();
    if(Restore){if(auto* P=Presentation())P->SetUmbrellaOverride(false);for(const auto& Entry:HiddenActors)if(Entry.Key.IsValid())Entry.Key->SetActorHiddenInGame(Entry.Value);if(HiddenDriver)HiddenDriver->SetVisibility(bDriverWasVisible);}
    HiddenActors.Empty();HiddenDriver=nullptr;
}
bool UHeliosGuanXiaoYuAbilityComponent::StartLightAttack(){if(!bUsingUmbrella||!CanUse())return false;bLightHeld=true;return BeginAttack(false);}
bool UHeliosGuanXiaoYuAbilityComponent::StartHeavyAttack(){bLightHeld=false;return BeginAttack(true);}
bool UHeliosGuanXiaoYuAbilityComponent::BeginAttack(bool Heavy)
{
    if(!bUsingUmbrella||!CanUse()||Now()+UE_SMALL_NUMBER<NextAttack||!Action.IsNone())return false;
    Action=Heavy?FName("Heavy"):FName("Light");auto* Animation=Clip(Action);if(!Animation){Action=NAME_None;return false;}
    AttackStarted=Now();ActionDuration=Animation->GetPlayLength();NextAttack=AttackStarted+ActionDuration+(!Heavy&&!bThirdPerson?Profile->FirstPersonLightRecovery:0);
    ContactTime=FMath::Clamp(Heavy?(bThirdPerson?Profile->ThirdPersonHeavyContact:Profile->HeavyContact):(bThirdPerson?Profile->ThirdPersonLightContact:Profile->FirstPersonLightContact),0.f,ActionDuration);
    bContactCommitted=false;bAttackSoundPlayed=false;
    if(Heavy){++HeavyAttacks;if(Now()>=NextShield){ShieldUntil=Now()+ShieldDuration;NextShield=Now()+ShieldCooldown;ShowShield(true);}}
    if(auto* Effect=SpawnEffect(Heavy?Profile->HeavyEffect:Profile->LightEffect)){Effect->MatchTravelLifetime(ActionDuration);Effects.Add(Effect);}
    return true;
}
void UHeliosGuanXiaoYuAbilityComponent::Strike()
{
    ++Strikes;auto* Pawn=Cast<APawn>(GetOwner());FVector Start;FRotator Aim;Pawn->GetActorEyesViewPoint(Start,Aim);
    if(auto* P=Cast<AHeliosArenaCharacter>(Pawn))Aim=P->GetControlRotation();
    const float Damage=Action==TEXT("Heavy")?HeavyDamage:LightDamage;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(GuanXiaoYuStrike),false,GetOwner());
    for(TActorIterator<AHeliosPetCompanion> Pet(GetWorld());Pet;++Pet)Query.AddIgnoredActor(*Pet);
    TArray<FHitResult> Contacts;FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_Pawn);
    GetWorld()->SweepMultiByObjectType(Contacts,Start,Start+Aim.Vector()*Range,FQuat::Identity,Objects,FCollisionShape::MakeSphere(SweepRadius),Query);
    TSet<AActor*> Seen;
    for(const auto& Contact:Contacts)
    {
        auto* Enemy=Cast<AHeliosEnemyActor>(Contact.GetActor());
        if(!Enemy||Seen.Contains(Enemy)||Enemy->IsEnemyDead()||!Enemy->CanBeDamaged())continue;Seen.Add(Enemy);
        const FVector Offset=Enemy->GetActorLocation()-Start;
        if(Offset.Size()>Range||FVector::DotProduct(Offset,Aim.Vector())<=0)continue;
        FHitResult Wall;if(GetWorld()->LineTraceSingleByChannel(Wall,Start,Enemy->GetActorLocation(),ECC_Visibility,Query)&&Wall.GetActor()!=Enemy)continue;
        const float Before=Enemy->CurrentHP;
        if(UGameplayStatics::ApplyDamage(Enemy,Damage,Pawn->GetController(),GetOwner(),UDamageType::StaticClass())>0&&Enemy->CurrentHP<Before)
        {++Hits;if(Enemy->IsEnemyDead())if(auto* P=Presentation())P->PlayCharacterCue(TEXT("Kill"));}
    }
}
void UHeliosGuanXiaoYuAbilityComponent::Sample()
{
    if(!bUsingUmbrella||!VisibleMesh||PoseCommitFrame==GFrameCounter)return;
    const FName Pose=Action.IsNone()?(GetOwner()->GetVelocity().Size2D()>20&&Clip(TEXT("Move"))?FName("Move"):FName("Idle")):Action;
    auto* A=Clip(Pose);if(!A)return;
    float Age=Action.IsNone()?FMath::Fmod(float(Now()),A->GetPlayLength()):FMath::Clamp(float(Now()-AttackStarted),0.f,A->GetPlayLength());
    if(Action.IsNone()&&!bThirdPerson&&AttackRecoveryRemaining()>0)if(auto* Recover=Clip(TEXT("Recover"));Recover&&AttackRecoveryRemaining()<=Recover->GetPlayLength()){A=Recover;Age=Recover->GetPlayLength()-AttackRecoveryRemaining();}
    auto* Instance=VisibleMesh->GetSingleNodeInstance();if(!Instance)return;
    const FTransform CurrentTransform=VisibleMesh->GetComponentTransform();
    const float ContinuousTravel=GetOwner()->GetVelocity().Size()*GetWorld()->GetDeltaSeconds()*2.f;
    const bool Discontinuous=!bResetPoseHistory&&
        (FVector::Dist(CurrentTransform.GetLocation(),LastMeshTransform.GetLocation())>FMath::Max(100.f,ContinuousTravel)||
         CurrentTransform.GetRotation().AngularDistance(LastMeshTransform.GetRotation())>FMath::DegreesToRadians(120.f));
    VisibleMesh->HandleExistingParallelEvaluationTask(true,true);
    if(Instance->GetCurrentAsset()!=A)VisibleMesh->SetAnimation(A);
    VisibleMesh->SetPosition(Age,false);VisibleMesh->Stop();VisibleMesh->TickAnimation(0,false);VisibleMesh->RefreshBoneTransforms();
    VisibleMesh->UpdateChildTransforms();
    if(bResetPoseHistory||Discontinuous){VisibleMesh->ClearMotionVector();bResetPoseHistory=false;}
    LastMeshTransform=CurrentTransform;PoseCommitFrame=GFrameCounter;++PoseCommitCount;
}
float UHeliosGuanXiaoYuAbilityComponent::ShieldRemaining()const{return bUsingUmbrella?FMath::Max(0.f,float(ShieldUntil-Now())):0;}
float UHeliosGuanXiaoYuAbilityComponent::CooldownRemaining()const{return FMath::Max(0.f,float(NextShield-Now()));}
float UHeliosGuanXiaoYuAbilityComponent::AttackRecoveryRemaining()const{return FMath::Max(0.f,float(NextAttack-Now()));}
bool UHeliosGuanXiaoYuAbilityComponent::TryAbsorbDamage(float Amount,const FDamageEvent& Event,AController* Instigator,AActor* Causer)
{
    if(Amount<=0||!CanUse()||ShieldRemaining()<=0||Event.IsOfType(FRadialDamageEvent::ClassID))return false;
    auto* Enemy=Cast<AHeliosEnemyActor>(Causer);
    // Generic events from hazard actors are excluded even when enemy-owned.
    // A projectile's point event can resolve its firing pawn independently.
    if(!Enemy&&Event.IsOfType(FPointDamageEvent::ClassID)&&Instigator)Enemy=Cast<AHeliosEnemyActor>(Instigator->GetPawn());
    if(Causer&&(Causer->ActorHasTag(TEXT("DamageOverTime"))||Causer->ActorHasTag(TEXT("EnvironmentDamage"))))return false;
    // Unowned hazards/DOT/environment damage cannot spend or benefit from this shield.
    if(!Enemy||Enemy->IsEnemyDead())return false;
    const FVector To=(Enemy->GetActorLocation()-GetOwner()->GetActorLocation()).GetSafeNormal2D();
    const FVector Facing=bThirdPerson?GetOwner()->GetActorForwardVector():CastChecked<APawn>(GetOwner())->GetControlRotation().Vector();
    if(FVector::DotProduct(Facing.GetSafeNormal2D(),To)<FMath::Cos(FMath::DegreesToRadians(ShieldAngle*.5f)))return false;
    ShieldUntil=0;++Blocks;ShowShield(false);return true;
}
void UHeliosGuanXiaoYuAbilityComponent::ShowShield(bool Visible)
{
    if(!Visible){if(IsValid(ShieldEffect))ShieldEffect->Destroy();ShieldEffect=nullptr;return;}
    if(IsValid(ShieldEffect))return;
    ShieldEffect=SpawnEffect(bThirdPerson?Profile->ThirdPersonShieldEffect:Profile->FirstPersonShieldEffect);
    if(ShieldEffect){if(!bThirdPerson)ShieldEffect->AddActorWorldOffset(CastChecked<APawn>(GetOwner())->GetControlRotation().RotateVector(Profile->FirstPersonShieldOffset));ShieldEffect->MatchTravelLifetime(ShieldDuration);}
}
AHeliosSSXEffect* UHeliosGuanXiaoYuAbilityComponent::SpawnEffect(const FString& Name)
{
    if(!Profile||Profile->EffectLibrary.IsNull()||Name.IsEmpty()||!VisibleMesh)return nullptr;
    const FVector At=bThirdPerson?GetOwner()->GetActorLocation()+FVector(0,0,30):VisibleMesh->GetComponentLocation();
    const FRotator Aim=bThirdPerson?GetOwner()->GetActorRotation():CastChecked<APawn>(GetOwner())->GetControlRotation();
    auto* Effect=AHeliosSSXEffect::Spawn(GetOwner(),Name,At,Aim,!bThirdPerson,1,Profile->EffectLibrary.ToSoftObjectPath().ToString());
    if(Effect)Effect->AttachToComponent(bThirdPerson?GetOwner()->GetRootComponent():VisibleMesh->GetAttachParent(),FAttachmentTransformRules::KeepWorldTransform);
    return Effect;
}
void UHeliosGuanXiaoYuAbilityComponent::PlaySound(USoundBase* Sound)
{if(Sound)UGameplayStatics::PlaySoundAtLocation(this,Sound,GetOwner()->GetActorLocation());}
FString UHeliosGuanXiaoYuAbilityComponent::StatusText()const
{return FString::Printf(TEXT("T %s · 剑伞盾 %s"),bUsingUmbrella?TEXT("收伞 / 返回主武器"):TEXT("切换专属伞"),ShieldRemaining()>0?*FString::Printf(TEXT("生效 %.1fs"),ShieldRemaining()):CooldownRemaining()>0?*FString::Printf(TEXT("冷却 %.1fs"),CooldownRemaining()):TEXT("就绪"));}
void UHeliosGuanXiaoYuAbilityComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Fn)
{
    Super::TickComponent(Dt,Type,Fn);if(!GetWorld()||GetWorld()->IsPaused()||!bUsingUmbrella)return;
    if(!CanUse()){Cancel();return;}
    const auto L=bThirdPerson?CastChecked<AHeliosHumanoidPet>(GetOwner())->GetLoadout():CastChecked<AHeliosArenaCharacter>(GetOwner())->GetLoadout();
    if(L.CharacterVariantId!=Variant){Cancel();return;}
    if(!Action.IsNone())
    {
        const float Age=Now()-AttackStarted;
        const bool Heavy=Action==TEXT("Heavy");
        const float SoundTime=bThirdPerson?(Heavy?Profile->ThirdPersonHeavySoundTime:Profile->ThirdPersonLightSoundTime):(Heavy?Profile->FirstPersonHeavySoundTime:Profile->FirstPersonLightSoundTime);
        if(!bAttackSoundPlayed&&Age>=SoundTime){bAttackSoundPlayed=true;PlaySound((bThirdPerson?(Heavy?Profile->ThirdPersonHeavySound:Profile->ThirdPersonLightSound):(Heavy?Profile->HeavySound:Profile->LightSound)).LoadSynchronous());}
        if(!bContactCommitted&&Age+UE_SMALL_NUMBER>=ContactTime){bContactCommitted=true;Strike();}
        if(Age>=ActionDuration)Action=NAME_None;
    }
    if(ShieldRemaining()<=0)ShowShield(false);
    Effects.RemoveAll([](const auto& E){return !IsValid(E);});
    if(bLightHeld&&Action.IsNone())BeginAttack(false);
    Sample();
}
void UHeliosGuanXiaoYuAbilityComponent::EndPlay(const EEndPlayReason::Type Reason)
{Cancel();Super::EndPlay(Reason);}
