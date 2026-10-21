#include "Weapons/HeliosMechWeapon.h"
#include "Audio/HeliosAudio.h"
#include "Player/HeliosArenaCharacter.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/PlayerController.h"
#include "Sound/SoundBase.h"
#include "CanvasItem.h"

UHeliosMechDefinition::UHeliosMechDefinition()
{
    WeaponId=TEXT("mech");DisplayName=FText::FromString(TEXT("黑金巨神 · 泰坦机甲"));
    Description=FText::FromString(TEXT("20米重装巨神。按住左键左右脚交替重踏；按住右键蓄能并持续扫射毁灭激光。无限近战，激光过热后冷却。"));
    WeaponClass=AHeliosMechWeapon::StaticClass();MagazineSize=1;ReloadSeconds=0;
    Damage=MinimumDamage=1000;Range=18000;RefireSeconds=1.4f;PulseSeconds=.1f;Recoil=0;
    WeaponMesh=TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Helios/Weapons/Mech/SM_MT_Torso.SM_MT_Torso")));
    PreviewTexture=TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Helios/Weapons/Mech/T_Mech.T_Mech")));
}
AHeliosMechWeapon::AHeliosMechWeapon()
{
    MechArt=CreateDefaultSubobject<UHeliosMechPresentationComponent>(TEXT("Titan armor and reactor"));
    MagazineSize=1;FirstPersonModel->SetVisibility(false);ThirdPersonModel->SetVisibility(false);
}
AHeliosArenaCharacter* AHeliosMechWeapon::Player() const { return Cast<AHeliosArenaCharacter>(GetOwner()); }
void AHeliosMechWeapon::ApplyDefinition(const UHeliosWeaponDefinition* D)
{
    Super::ApplyDefinition(D);Settings=Cast<UHeliosMechDefinition>(ActiveDefinition);
    if(!Settings)Settings=NewObject<UHeliosMechDefinition>(this);
    FirstPersonModel->SetStaticMesh(nullptr);ThirdPersonModel->SetStaticMesh(nullptr);
    MechArt->Initialize();
}
void AHeliosMechWeapon::PlaySound(const TCHAR* Name,float Volume) const
{
    if(auto* S=LoadObject<USoundBase>(nullptr,*(FString(TEXT("/Game/Helios/Weapons/Mech/SFX_MT_"))+Name)))
        HeliosAudio::PlaySound2D(this,S,Volume);
}
void AHeliosMechWeapon::ActivateWeapon(const FName& Tag)
{
    Super::ActivateWeapon(Tag);bActive=false;Heat=0;CoolRemaining=0;CancelAttack();
    TryResumeAfterArrival();
}
void AHeliosMechWeapon::SuspendForArrival()
{
    bActive=false;CancelAttack();SetActorHiddenInGame(true);
    if(auto* P=Player())P->ExitMechMode();
}
bool AHeliosMechWeapon::TryResumeAfterArrival()
{
    bActive=false;CancelAttack();
    if(auto* P=Player())bActive=P->EnterMechMode();
    SetActorHiddenInGame(!bActive);
    GetFirstPersonMesh()->SetVisibility(false,true);GetThirdPersonMesh()->SetVisibility(false,true);
    DeployAge=0;
    if(bActive){MechArt->Update(0);PlaySound(TEXT("Deploy"),.7f);}
    return bActive;
}
void AHeliosMechWeapon::DeactivateWeapon()
{
    bActive=false;CancelAttack();MechArt->Clear();if(auto* P=Player())P->ExitMechMode();Super::DeactivateWeapon();
}
void AHeliosMechWeapon::EndPlay(EEndPlayReason::Type Reason)
{
    bActive=false;CancelAttack();MechArt->Clear();if(auto* P=Player())P->ExitMechMode();Super::EndPlay(Reason);
}
bool AHeliosMechWeapon::CanAttack() const
{ const auto* P=Player();return bActive&&Settings&&P&&!P->IsDead()&&P->IsMechMode()&&GetWorld()&&!GetWorld()->IsPaused()&&DeployAge>=.8f; }
bool AHeliosMechWeapon::WantsStomp(FVector* Point) const
{
    const auto* P=Player();if(!P||!P->IsMechMode())return false;
    const FVector Ground=P->GetGroundReference(),S=P->FirstPersonCamera->GetComponentLocation();
    const FVector D=P->FirstPersonCamera->GetForwardVector();
    FHitResult H;FCollisionQueryParams Q(SCENE_QUERY_STAT(TitanStompAim),false,P);Q.AddIgnoredActor(this);
    FCollisionObjectQueryParams Types;Types.AddObjectTypesToQuery(ECC_WorldStatic);Types.AddObjectTypesToQuery(ECC_WorldDynamic);
    FVector Candidate=Ground+D.GetSafeNormal2D()*600;
    if(GetWorld()->LineTraceSingleByObjectType(H,S,S+D*6000,Types,Q)&&H.ImpactNormal.Z>=.7f&&FVector::Dist2D(Ground,H.ImpactPoint)<=1200)
        Candidate=H.ImpactPoint;
    if(!GetWorld()->LineTraceSingleByObjectType(H,Candidate+FVector(0,0,300),Candidate-FVector(0,0,600),Types,Q)||H.ImpactNormal.Z<.7f)return false;
    Candidate=H.ImpactPoint;
    // Foot-width clearance prevents a valid floor behind a wall from accepting a stomp.
    FHitResult Wall;
    if(GetWorld()->SweepSingleByObjectType(Wall,Ground+FVector(0,0,180),Candidate+FVector(0,0,180),FQuat::Identity,Types,FCollisionShape::MakeSphere(110),Q))return false;
    if(FMath::Abs(Candidate.Z-Ground.Z)>260)return false;
    if(Point)*Point=Candidate;return true;
}
void AHeliosMechWeapon::StartFiring(){if(CanAttack()){bPrimaryHeld=true;if(State==EHeliosMechState::Idle||State==EHeliosMechState::Cooling)BeginMelee();}}
void AHeliosMechWeapon::StartSecondaryFire(){if(CanAttack()){bSecondaryHeld=true;if(State==EHeliosMechState::Idle&&CoolRemaining<=0)BeginCharge();}}
void AHeliosMechWeapon::StopSecondaryFire(){bSecondaryHeld=false;if(State==EHeliosMechState::Charging||State==EHeliosMechState::Firing)FinishLaser(false);}
void AHeliosMechWeapon::CancelAttack()
{
    bPrimaryHeld=bSecondaryHeld=false;bHavePreviousHand=false;HitEnemies.Reset();
    if(LaserAudio){LaserAudio->Stop();LaserAudio->DestroyComponent();LaserAudio=nullptr;}
    State=CoolRemaining>0?EHeliosMechState::Cooling:EHeliosMechState::Idle;Age=PulseAge=0;
    if(MechArt)MechArt->Clear();
}
void AHeliosMechWeapon::BeginMelee()
{
    if(!CanAttack()||(State!=EHeliosMechState::Idle&&State!=EHeliosMechState::Cooling))return;
    if(GetWorld()->GetTimeSeconds()<InvalidStompUntil)return;
    if(!WantsStomp(&StompPoint)){InvalidStompUntil=GetWorld()->GetTimeSeconds()+.5f;return;}
    State=EHeliosMechState::Stomp;
    Age=0;bStompCommitted=bHavePreviousHand=false;HitEnemies.Reset();SwingSide=1-SwingSide;

}
void AHeliosMechWeapon::BeginCharge()
{
    if(!CanAttack()||CoolRemaining>0||State!=EHeliosMechState::Idle)return;
    State=EHeliosMechState::Charging;Age=0;PulseAge=0;
    if(auto* S=LoadObject<USoundBase>(nullptr,TEXT("/Game/Helios/Weapons/Mech/SFX_MT_Charge")))LaserAudio=HeliosAudio::SpawnSound2D(this,S,.55f);
}
void AHeliosMechWeapon::FinishLaser(bool Overheated)
{
    if(LaserAudio){LaserAudio->Stop();LaserAudio->DestroyComponent();LaserAudio=nullptr;}
    if(Overheated){Heat=1;CoolRemaining=Settings->CooldownSeconds;bSecondaryHeld=false;}
    if(State==EHeliosMechState::Firing)PlaySound(TEXT("Vent"),.6f);
    State=CoolRemaining>0?EHeliosMechState::Cooling:EHeliosMechState::Idle;Age=0;
    MechArt->SetBeam(BeamOrigin,BeamTarget,false,0,false);
}
bool AHeliosMechWeapon::Unobstructed(const FVector& A,const FVector& B,const AActor* Target) const
{
    FHitResult H;FCollisionQueryParams Q(SCENE_QUERY_STAT(TitanCover),false,GetOwner());Q.AddIgnoredActor(this);Q.AddIgnoredActor(Target);
    FCollisionObjectQueryParams O;O.AddObjectTypesToQuery(ECC_WorldStatic);O.AddObjectTypesToQuery(ECC_WorldDynamic);
    return !GetWorld()->LineTraceSingleByObjectType(H,A,B,O,Q);
}
void AHeliosMechWeapon::DamageStomp()
{
    auto* P=Player();if(!P)return;
    for(TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It)
    {
        auto* E=*It;if(E->IsEnemyDead()||FVector::DistSquared(E->GetActorLocation(),StompPoint)>FMath::Square(Settings->StompRadius))continue;
        if(!Unobstructed(StompPoint+FVector(0,0,45),E->GetActorLocation(),E))continue;
        UGameplayStatics::ApplyDamage(E,Settings->StompDamage,P->GetController(),this,UDamageType::StaticClass());
        E->LaunchCharacter((E->GetActorLocation()-StompPoint).GetSafeNormal2D()*650+FVector(0,0,360),true,true);
    }
    MechArt->Burst(StompPoint,FVector::UpVector,Settings->StompRadius,true);PlaySound(TEXT("Stomp"),.85f);
}
void AHeliosMechWeapon::TraceBeam()
{
    auto* P=Player();if(!P)return;
    const FVector View=P->FirstPersonCamera->GetComponentLocation(),D=P->FirstPersonCamera->GetForwardVector();
    FCollisionQueryParams Q(SCENE_QUERY_STAT(TitanBeam),false,P);Q.AddIgnoredActor(this);
    FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_WorldStatic);Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    FHitResult Aim;FVector Target=View+D*Settings->Range;
    if(GetWorld()->LineTraceSingleByObjectType(Aim,View,Target,Objects,Q))Target=Aim.ImpactPoint;
    BeamOrigin=MechArt->Muzzle;const FVector Direction=(Target-BeamOrigin).GetSafeNormal();
    BeamTarget=BeamOrigin+Direction*FMath::Min(Settings->Range,float(FVector::Distance(BeamOrigin,Target)));
    FHitResult H;bBeamHasHit=GetWorld()->SweepSingleByObjectType(H,BeamOrigin,BeamTarget,FQuat::Identity,Objects,FCollisionShape::MakeSphere(8),Q);
    if(bBeamHasHit){BeamTarget=H.ImpactPoint;BeamNormal=H.ImpactNormal;}
    else BeamNormal=-Direction;
}
void AHeliosMechWeapon::DamageBeam()
{
    auto* P=Player();if(!P)return;
    for(TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It)
    {
        auto* E=*It;if(E->IsEnemyDead())continue;
        auto* C=E->GetCapsuleComponent();const float H=C?C->GetScaledCapsuleHalfHeight():80;
        FVector A,B;FMath::SegmentDistToSegmentSafe(BeamOrigin,BeamTarget,E->GetActorLocation()-FVector(0,0,H*.7f),E->GetActorLocation()+FVector(0,0,H*.7f),A,B);
        if(FVector::DistSquared(A,B)>FMath::Square(Settings->BeamRadius+(C?C->GetScaledCapsuleRadius():40)))continue;
        if(!Unobstructed(BeamOrigin,E->GetActorLocation(),E))continue;
        UGameplayStatics::ApplyDamage(E,Settings->BeamDamage,P->GetController(),this,UDamageType::StaticClass());
        MechArt->Burst(B,-(BeamTarget-BeamOrigin).GetSafeNormal(),100,false);
    }
}
void AHeliosMechWeapon::Tick(float Dt)
{
    Super::Tick(Dt);if(!bActive||!Settings||!Player())return;
    if(Player()->IsDead()){CancelAttack();return;}DeployAge+=Dt;
    if(CoolRemaining>0){CoolRemaining=FMath::Max(0.f,CoolRemaining-Dt);Heat=CoolRemaining/Settings->CooldownSeconds;}
    else if(State!=EHeliosMechState::Firing)Heat=FMath::Max(0.f,Heat-Dt/Settings->CooldownSeconds);
    Age+=Dt;MechArt->Update(Dt);
    if(State==EHeliosMechState::Stomp)
    {
        if(!bStompCommitted&&Age>=Settings->StompSeconds*.57f){bStompCommitted=true;DamageStomp();}
        if(Age>=Settings->StompSeconds){State=CoolRemaining>0?EHeliosMechState::Cooling:EHeliosMechState::Idle;Age=0;}
    }
    else if(State==EHeliosMechState::Charging||State==EHeliosMechState::Firing)
    {
        TraceBeam();
        if(State==EHeliosMechState::Charging&&Age>=Settings->ChargeSeconds)
        {
            State=EHeliosMechState::Firing;Age=0;PulseAge=Settings->PulseSeconds;
            if(LaserAudio){LaserAudio->Stop();LaserAudio->DestroyComponent();LaserAudio=nullptr;}
            if(auto* S=LoadObject<USoundBase>(nullptr,TEXT("/Game/Helios/Weapons/Mech/SFX_MT_BeamLoop")))LaserAudio=HeliosAudio::SpawnSound2D(this,S,.55f);
        }
        const bool Firing=State==EHeliosMechState::Firing;
        MechArt->SetBeam(BeamOrigin,BeamTarget,bBeamHasHit,Firing?1:Age/Settings->ChargeSeconds,Firing);
        if(Firing)
        {
            Heat=FMath::Min(1.f,Heat+Dt/Settings->MaximumBeamSeconds);PulseAge+=Dt;
            // Frame-rate independent pulses, with bounded recovery after a long stall.
            int32 Pulses=0;while(PulseAge>=Settings->PulseSeconds&&Pulses++<8){PulseAge-=Settings->PulseSeconds;DamageBeam();}
            if(Heat>=1)FinishLaser(true);
        }
    }
    else
    {
        if(State==EHeliosMechState::Cooling&&CoolRemaining<=0)State=EHeliosMechState::Idle;
        if(bSecondaryHeld&&State==EHeliosMechState::Idle)BeginCharge();
        else if(bPrimaryHeld)BeginMelee();
    }
}
FString AHeliosMechWeapon::Status() const
{
    const TCHAR* Mode=State==EHeliosMechState::Charging?TEXT("反应炉蓄能"):State==EHeliosMechState::Firing?TEXT("毁灭激光"):CoolRemaining>0?TEXT("过热散热"):GetWorld()->GetTimeSeconds()<InvalidStompUntil?TEXT("无法踩踏 · 前方无安全落点"):TEXT("机械重踏");
    return FString::Printf(TEXT("%s · 热量 %02d%% · 20m"),Mode,FMath::RoundToInt(Heat*100));
}
void AHeliosMechWeapon::DrawMechHUD(UCanvas* C)
{
    if(!C||!bActive)return;const float X=C->ClipX*.5f,Y=C->ClipY*.5f,Scale=C->ClipY/1080.f;
    const FLinearColor Gold(1,.58f,.16f,.65f),Dim(.4f,.55f,.62f,.35f);
    auto Line=[C](float AX,float AY,float BX,float BY,FLinearColor Color,float Width=1.f){FCanvasLineItem I(FVector2D(AX,AY),FVector2D(BX,BY));I.SetColor(Color);I.LineThickness=Width;C->DrawItem(I);};
    for(int32 S:{-1,1}){Line(X+S*38*Scale,Y-22*Scale,X+S*38*Scale,Y+22*Scale,Gold,2);Line(X+S*38*Scale,Y-22*Scale,X+S*24*Scale,Y-22*Scale,Gold);Line(X+S*38*Scale,Y+22*Scale,X+S*24*Scale,Y+22*Scale,Gold);}
    for(int32 I=0;I<30;++I){const float LX=X-150*Scale+I*10*Scale;Line(LX,Y+88*Scale,LX+6*Scale,Y+88*Scale,I<float(30)*Heat?Gold:Dim,3*Scale);}
    const FString Text=State==EHeliosMechState::Charging?FString::Printf(TEXT("REACTOR CHARGE  %02d%%"),FMath::RoundToInt(Age/Settings->ChargeSeconds*100)):State==EHeliosMechState::Firing?TEXT("ANNIHILATION // ACTIVE"):CoolRemaining>0?TEXT("REACTOR VENTING"):GetWorld()->GetTimeSeconds()<InvalidStompUntil?TEXT("STOMP BLOCKED / NO FOOTING"):TEXT("LMB / GROUND BREAKER");
    FCanvasTextItem Label(FVector2D(X-146*Scale,Y+104*Scale),FText::FromString(Text),GEngine->GetSmallFont(),Gold);Label.Scale=FVector2D(Scale);Label.EnableShadow(FLinearColor::Black);C->DrawItem(Label);
    FCanvasTextItem Tag(FVector2D(56*Scale,C->ClipY*.25f),FText::FromString(TEXT("T I T A N   /   2 0\nHEAVY ASSAULT FRAME\nREACTOR ONLINE")),GEngine->GetSmallFont(),Dim);Tag.Scale=FVector2D(Scale);C->DrawItem(Tag);
}
