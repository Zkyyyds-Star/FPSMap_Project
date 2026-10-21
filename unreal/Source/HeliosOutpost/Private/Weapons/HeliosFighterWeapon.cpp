#include "Weapons/HeliosFighterWeapon.h"
#include "Player/HeliosArenaCharacter.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Pets/HeliosPetDog.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/DamageType.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "UnrealClient.h"

UHeliosFighterDefinition::UHeliosFighterDefinition()
{
    WeaponId=TEXT("fighter");DisplayName=FText::FromString(TEXT("隼影 VX-01 · 悬停战斗机"));
    Description=FText::FromString(TEXT("第一人称驾驶座舱，自由悬停飞行。左键双翼激光，右键锁定导弹。无限弹药，激光过热冷却。空格升高 / Ctrl下降 / Shift切换加速。"));
    WeaponClass=AHeliosFighterWeapon::StaticClass();MagazineSize=1;ReloadSeconds=0;Damage=MinimumDamage=20;Range=25000;RefireSeconds=.1f;Recoil=0;
    WeaponMesh=TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Helios/Weapons/Fighter/SM_VX_Hull.SM_VX_Hull")));
    PreviewTexture=TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Helios/Weapons/Fighter/T_Fighter.T_Fighter")));
}
void UHeliosFighterDefinition::LoadConfiguration(const UObject*)
{
    FConfigFile C;C.Read(FPaths::ProjectContentDir()/TEXT("Settings/战斗数值/战斗机.ini"));
    auto Read=[&](const TCHAR* Key,float& V,float Min,float Max){float N;if(C.GetFloat(TEXT("VX01"),Key,N)&&FMath::IsFinite(N))V=FMath::Clamp(N,Min,Max);};
#define VX_READ(Field,Min,Max) Read(TEXT(#Field),Field,Min,Max)
    VX_READ(Damage,0,100000);VX_READ(Range,100,100000);VX_READ(RefireSeconds,.02f,10);
    VX_READ(ForwardSpeed,100,10000);VX_READ(BoostSpeed,100,15000);VX_READ(StrafeSpeed,100,10000);VX_READ(VerticalSpeed,100,10000);VX_READ(Ceiling,1000,30000);
    VX_READ(HeatSeconds,.1f,60);VX_READ(CoolSeconds,.1f,60);VX_READ(DissipateSeconds,.1f,60);
    VX_READ(MissileCooldown,.1f,60);VX_READ(MissileDamage,0,100000);VX_READ(MissileMinimumDamage,0,100000);VX_READ(MissileRadius,1,5000);
    VX_READ(MissileSpeed,100,20000);VX_READ(MissileLife,.2f,20);VX_READ(TurnDegrees,1,360);VX_READ(LockRange,100,100000);VX_READ(LockAngle,1,30);VX_READ(LockSeconds,.01f,10);
    VX_READ(LaserCoreRadius,.2f,8);VX_READ(LaserHaloRadius,1,20);VX_READ(LaserVisualSeconds,.03f,.18f);
    VX_READ(TrailSpacing,100,500);VX_READ(EffectCullDistance,1000,100000);VX_READ(CockpitFeedback,0,.5f);
#undef VX_READ
    MissileMinimumDamage=FMath::Min(MissileDamage,MissileMinimumDamage);
}
AHeliosFighterWeapon::AHeliosFighterWeapon()
{
    Flight=CreateDefaultSubobject<UHeliosFighterFlightComponent>(TEXT("VX flight"));
    Art=CreateDefaultSubobject<UHeliosFighterPresentation>(TEXT("VX presentation"));
    PrimaryActorTick.bTickEvenWhenPaused=true;PrimaryActorTick.TickGroup=TG_PostPhysics;
    MagazineSize=1;
}
AHeliosArenaCharacter* AHeliosFighterWeapon::Player() const{return Cast<AHeliosArenaCharacter>(GetOwner());}
void AHeliosFighterWeapon::ApplyDefinition(const UHeliosWeaponDefinition* D)
{
    Super::ApplyDefinition(D);Settings=Cast<UHeliosFighterDefinition>(ActiveDefinition);
    if(!Settings)Settings=NewObject<UHeliosFighterDefinition>(this);
    Settings->LoadConfiguration(this);FirstPersonModel->SetStaticMesh(nullptr);ThirdPersonModel->SetStaticMesh(nullptr);Art->Initialize();
}
void AHeliosFighterWeapon::ActivateWeapon(const FName& Tag)
{
    Super::ActivateWeapon(Tag);Heat=CoolRemaining=MissileRemaining=ShotClock=0;
    TryResumeAfterArrival();
}
bool AHeliosFighterWeapon::TryResumeAfterArrival()
{
    CancelInput();bActive=Flight->Deploy();DeployAge=0;SetActorHiddenInGame(!bActive);
    GetFirstPersonMesh()->SetVisibility(false,true);GetThirdPersonMesh()->SetVisibility(false,true);
    if(bActive){Art->Update(0);Art->Sound(TEXT("Deploy"),.5f);}return bActive;
}
void AHeliosFighterWeapon::SuspendForArrival()
{bActive=false;CancelInput();ClearProjectiles();Art->Clear();Flight->Restore();SetActorHiddenInGame(true);}
void AHeliosFighterWeapon::DeactivateWeapon(){SuspendForArrival();Super::DeactivateWeapon();}
void AHeliosFighterWeapon::EndPlay(EEndPlayReason::Type R){SuspendForArrival();Super::EndPlay(R);}
void AHeliosFighterWeapon::CancelInput()
{bPrimaryHeld=bSecondaryHeld=false;ShotClock=0;LockCandidate.Reset();LockProgress=0;bLockSound=false;if(Flight)Flight->ClearInput();}
void AHeliosFighterWeapon::ClearProjectiles()
{
    if(!GetWorld())return;
    for(TActorIterator<AHeliosFighterMissile> M(GetWorld());M;++M)if(M->GetOwner()==this)M->Destroy();
    for(TActorIterator<AHeliosExplosionEffect> E(GetWorld());E;++E)if(E->GetOwner()==this)E->Destroy();
}
bool AHeliosFighterWeapon::CanAttack() const
{const auto* P=Player();return bActive&&Settings&&P&&!P->IsDead()&&P->IsFighterMode()&&!GetWorld()->IsPaused()&&DeployAge>=.6f;}
bool AHeliosFighterWeapon::ClearSight(const FVector& A,const FVector& B,const AActor* Target) const
{
    FCollisionQueryParams Q(SCENE_QUERY_STAT(FighterSight),false,Player());Q.AddIgnoredActor(this);FHitResult H;
    if(auto* Pets=GetWorld()->GetSubsystem<UHeliosPetWorld>())Pets->IgnoreAll(Q);
    return !GetWorld()->LineTraceSingleByChannel(H,A,B,ECC_Visibility,Q)||H.GetActor()==Target;
}
FVector AHeliosFighterWeapon::Muzzle(int32 Side) const
{
    const auto* P=Player();if(!P)return GetActorLocation();
    return P->GetActorLocation()+Flight->HullRotation.RotateVector(FVector(225,Side?163:-163,-22));
}
void AHeliosFighterWeapon::StartFiring()
{
    if(!CanAttack()||bPrimaryHeld)return;bPrimaryHeld=true;
    if(CoolRemaining<=0&&ShotClock<=0){FirePulse();ShotClock=Settings->RefireSeconds;}
}
void AHeliosFighterWeapon::FirePulse()
{
    auto* P=Player();if(!P)return;
    const FVector Eye=P->FirstPersonCamera->GetComponentLocation(),End=Eye+P->GetControlRotation().Vector()*Settings->Range;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(FighterLaser),false,P);Q.AddIgnoredActor(this);FHitResult Aim,Hit;
    if(auto* Pets=GetWorld()->GetSubsystem<UHeliosPetWorld>())Pets->IgnoreAll(Q);
    GetWorld()->LineTraceSingleByChannel(Aim,Eye,End,ECC_Visibility,Q);
    const FVector Target=Aim.bBlockingHit?Aim.ImpactPoint:End;const FVector Start=Muzzle(LaserCount%2);
    // A blocked eye-to-barrel path cannot move the shot to the far side of cover.
    if(!GetWorld()->LineTraceSingleByChannel(Hit,Eye,Start,ECC_Visibility,Q))
        GetWorld()->LineTraceSingleByChannel(Hit,Start,Target+(Target-Start).GetSafeNormal()*2,ECC_Visibility,Q);
    if(auto* Enemy=Cast<AHeliosEnemyActor>(Hit.GetActor());Enemy&&!Enemy->IsEnemyDead())
        UGameplayStatics::ApplyPointDamage(Enemy,Settings->Damage,(Target-Start).GetSafeNormal(),Hit,P->GetController(),this,UDamageType::StaticClass());
    Art->Pulse(Start,Hit.bBlockingHit?Hit.ImpactPoint:Target,Hit,LaserCount%2);++LaserCount;
}
void AHeliosFighterWeapon::UpdateLock(float Dt)
{
    auto* P=Player();const FVector Eye=P->FirstPersonCamera->GetComponentLocation(),Forward=P->GetControlRotation().Vector();
    AActor* Best=nullptr;float BestScore=-1;const float Threshold=FMath::Cos(FMath::DegreesToRadians(Settings->LockAngle));
    for(TActorIterator<AHeliosEnemyActor> E(GetWorld());E;++E)
    {
        if(E->IsEnemyDead())continue;const FVector Delta=E->GetActorLocation()-Eye;const float Distance=Delta.Size();
        if(Distance>Settings->LockRange||Distance<1)continue;const float Dot=FVector::DotProduct(Forward,Delta/Distance);
        if(Dot<Threshold||!ClearSight(Eye,E->GetActorLocation(),*E))continue;
        const float Score=Dot-Distance/Settings->LockRange*.002f+(LockCandidate.Get()==*E?.004f:0);
        if(Score>BestScore){Best=*E;BestScore=Score;}
    }
    if(Best!=LockCandidate.Get()){LockCandidate=Best;LockProgress=0;bLockSound=false;}
    if(Best){LockProgress=FMath::Min(Settings->LockSeconds,LockProgress+Dt);if(LockProgress>=Settings->LockSeconds&&!bLockSound){Art->Sound(TEXT("Lock"),.35f);bLockSound=true;}}
    else {LockProgress=0;bLockSound=false;}
}
void AHeliosFighterWeapon::StartSecondaryFire()
{
    if(bSecondaryHeld)return;bSecondaryHeld=true;
    if(!CanAttack()||MissileRemaining>0)return;auto* P=Player();
    const FVector Start=P->GetActorLocation()+Flight->HullRotation.RotateVector(FVector(90,MissileCount%2?170:-170,-70));
    if(!ClearSight(P->FirstPersonCamera->GetComponentLocation(),Start))return;
    FActorSpawnParameters S;S.Owner=this;S.Instigator=P;S.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* M=GetWorld()->SpawnActor<AHeliosFighterMissile>(Start,Flight->HullRotation,S);if(!M)return;
    AActor* Target=LockProgress>=Settings->LockSeconds?LockCandidate.Get():nullptr;
    M->Launch(Settings,Target,P->GetControlRotation().Vector());MissileRemaining=Settings->MissileCooldown;++MissileCount;Art->MissileLaunch(Start,P->GetControlRotation().Vector());
}
void AHeliosFighterWeapon::Tick(float Dt)
{
    Super::Tick(Dt);if(!bActive)return;
    // Offscreen QA deliberately has no focused OS window. Human sessions require focus.
    bool Focus=true;
    if(!FParse::Param(FCommandLine::Get(),TEXT("RenderOffscreen"))&&GEngine&&GEngine->GameViewport&&GEngine->GameViewport->Viewport)
        Focus=GEngine->GameViewport->Viewport->HasFocus();
    if(GetWorld()->IsPaused()||!Focus){CancelInput();Art->Clear();return;}
    DeployAge+=Dt;MissileRemaining=FMath::Max(0.f,MissileRemaining-Dt);
    if(!CanAttack()){Art->Update(Dt);return;}UpdateLock(Dt);
    float Remaining=FMath::Min(Dt,.25f);
    while(Remaining>UE_KINDA_SMALL_NUMBER)
    {
        const float Step=FMath::Min(Remaining,1.f/120);Remaining-=Step;
        ShotClock=FMath::Max(0.f,ShotClock-Step);
        if(CoolRemaining>0)
        {CoolRemaining=FMath::Max(0.f,CoolRemaining-Step);Heat=CoolRemaining/Settings->CoolSeconds;}
        else if(bPrimaryHeld)
        {
            if(ShotClock<=UE_KINDA_SMALL_NUMBER){FirePulse();ShotClock+=Settings->RefireSeconds;}
            Heat=FMath::Min(1.f,Heat+Step/Settings->HeatSeconds);
            if(Heat>=1.f-UE_KINDA_SMALL_NUMBER){Heat=1;CoolRemaining=Settings->CoolSeconds;Art->Sound(TEXT("Overheat"),.5f);}
        }
        else Heat=FMath::Max(0.f,Heat-Step/Settings->DissipateSeconds);
    }
    Art->Update(Dt);
}
FString AHeliosFighterWeapon::Status() const
{return FString::Printf(TEXT("激光 %d%% %s · 导弹 %s"),FMath::RoundToInt(Heat*100),CoolRemaining>0?TEXT("冷却"):TEXT("就绪"),MissileRemaining>0?*FString::Printf(TEXT("%.1fs"),MissileRemaining):TEXT("就绪"));}
