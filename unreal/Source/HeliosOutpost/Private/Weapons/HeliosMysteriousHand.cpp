#include "Weapons/HeliosMysteriousHand.h"
#include "Audio/HeliosAudio.h"
#include "Weapons/HeliosHandAnimInstance.h"
#include "Weapons/HeliosWeaponVFX.h"
#include "Weapons/HeliosHandImpact.h"
#include "Weapons/HeliosWeaponHolderContext.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Player/HeliosArenaCharacter.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "GameFramework/PlayerController.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace MH
{
const TCHAR* Base=TEXT("/Game/Helios/Weapons/MysteriousHand/");
template<class T> T* Asset(const TCHAR* Name) { return LoadObject<T>(nullptr,*(FString(Base)+Name)); }
UNiagaraComponent* FX(UObject* Context,const TCHAR* Name,const FVector& P,const FRotator& R=FRotator::ZeroRotator,float Life=2)
{ auto* C=UHeliosWeaponVFX::SpawnOneShot(Context,Asset<UNiagaraSystem>(Name),P,FRotator::ZeroRotator,FVector::OneVector,Life);if(C){C->SetVariableVec3(TEXT("User.Direction"),R.Vector());C->ReinitializeSystem();}return C; }
void Sound(UObject* Context,const TCHAR* Name,const FVector& P,float Volume=1)
{ if(auto* S=Asset<USoundBase>(Name))HeliosAudio::PlaySoundAtLocation(Context,S,P,Volume); }
bool Alive(const AHeliosEnemyActor* E) { return IsValid(E)&&!E->IsEnemyDead(); }
}

UHeliosMysteriousHandDefinition::UHeliosMysteriousHandDefinition()
{
    WeaponId=TEXT("mysterious_hand");DisplayName=FText::FromString(TEXT("神秘人"));
    Description=FText::FromString(TEXT("中指挑衅，食指点名。左键单击自动锁定准星附近敌人；右键辅助选中 · 定点轰炸，按下时固定敌人脚下落点。无需弹药。"));
    WeaponClass=AHeliosMysteriousHand::StaticClass();MagazineSize=1;ReloadSeconds=0;
    Damage=MinimumDamage=60;Range=6000;RefireSeconds=.6f;Recoil=.1f;
    AttachmentLocation=FVector(45,19,-23);AttachmentRotation=FRotator(0,90,0);AttachmentScale=FVector(1);
    SkeletalWeaponMesh=TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(FString(MH::Base)+TEXT("SK_MH_RightHand.SK_MH_RightHand")));
    PreviewTexture=TSoftObjectPtr<UTexture2D>(FSoftObjectPath(FString(MH::Base)+TEXT("T_MH_Preview.T_MH_Preview")));
    IdleAnimation=TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(FString(MH::Base)+TEXT("AN_MH_Idle.AN_MH_Idle")));
    PointAnimation=TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(FString(MH::Base)+TEXT("AN_MH_Point.AN_MH_Point")));
    BooAnimation=TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(FString(MH::Base)+TEXT("AN_MH_Boo.AN_MH_Boo")));
}

AHeliosMysteriousHand::AHeliosMysteriousHand()
{
    PrimaryActorTick.bCanEverTick=true;bFullAuto=false;MagazineSize=1;
    FirstPersonModel->SetVisibility(false);ThirdPersonModel->SetVisibility(false);
    GetFirstPersonMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetFirstPersonMesh()->SetCastShadow(false);GetFirstPersonMesh()->SetOnlyOwnerSee(true);
    GetFirstPersonMesh()->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
    GetFirstPersonMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
}
void AHeliosMysteriousHand::ApplyDefinition(const UHeliosWeaponDefinition* D)
{
    Super::ApplyDefinition(D);Settings=Cast<UHeliosMysteriousHandDefinition>(ActiveDefinition);
    if(!Settings)return;
    GetFirstPersonMesh()->SetSkeletalMesh(Settings->SkeletalWeaponMesh.LoadSynchronous());
    IdleAnim=Settings->IdleAnimation.LoadSynchronous();PointAnim=Settings->PointAnimation.LoadSynchronous();BooAnim=Settings->BooAnimation.LoadSynchronous();
    GetFirstPersonMesh()->SetAnimInstanceClass(UHeliosHandAnimInstance::StaticClass());
    GetFirstPersonMesh()->AddTickPrerequisiteActor(this);
    UpdateHandPose(true);
    FirstPersonModel->SetVisibility(false);ThirdPersonModel->SetVisibility(false);
}
void AHeliosMysteriousHand::ActivateWeapon(const FName& Tag)
{ Super::ActivateWeapon(Tag);bActive=true;CancelGesture();FirstPersonModel->SetVisibility(false); }
void AHeliosMysteriousHand::DeactivateWeapon()
{ bActive=false;CancelGesture();Super::DeactivateWeapon(); }
void AHeliosMysteriousHand::EndPlay(const EEndPlayReason::Type R)
{ CancelGesture();Super::EndPlay(R); }
void AHeliosMysteriousHand::CancelReload() { CancelGesture(); }
void AHeliosMysteriousHand::CancelGesture()
{
    Gesture=Buffered=0;bCommitted=false;bPrimaryDown=bSecondaryDown=false;
    PreviewEnemy.Reset();NextPreviewTime=0;
    if(Marker){UHeliosWeaponVFX::ReleaseTracked(Marker);Marker=nullptr;}
    if(Settings)GetFirstPersonMesh()->SetRelativeLocation(Settings->AttachmentLocation);
    UpdateHandPose();
}
bool AHeliosMysteriousHand::View(FVector& S,FVector& D) const
{
    FRotator R;if(!HeliosWeaponHolderContext::View(GetOwner(),S,R))return false;
    D=R.Vector();return true;
}
void AHeliosMysteriousHand::StartFiring()
{ if(!bPrimaryDown){bPrimaryDown=true;BeginGesture(1);} }
void AHeliosMysteriousHand::StartSecondaryFire()
{ if(!bSecondaryDown){bSecondaryDown=true;BeginGesture(2);} }
bool AHeliosMysteriousHand::BeginGesture(uint8 Requested,bool bUseBufferedStrike)
{
    if(!bActive||!Settings||!GetWorld()||GetWorld()->IsPaused())return false;
    if(HeliosWeaponHolderContext::IsDead(GetOwner()))return false;
    const double Now=GetWorld()->GetTimeSeconds();
    if(Gesture)
    {
        const float Duration=Gesture==1?Settings->RefireSeconds:1.f;
        if(Now-GestureStart>=Duration-.12f && Buffered==0)
        {
            if(Requested==2&&!ResolveStrike(BufferedStrikePoint,BufferedStrikeNormal,BufferedStrikeSpawn))
            {InvalidUntil=Now+.6;MH::Sound(this,TEXT("SFX_MH_Invalid"),GetActorLocation(),.45f);return false;}
            Buffered=Requested;
        }
        return false;
    }
    if(Requested==2)
    {
        if(bUseBufferedStrike){StrikePoint=BufferedStrikePoint;StrikeNormal=BufferedStrikeNormal;StrikeSpawn=BufferedStrikeSpawn;}
        else if(!ResolveStrike(StrikePoint,StrikeNormal,StrikeSpawn))
        {InvalidUntil=Now+.6;MH::Sound(this,TEXT("SFX_MH_Invalid"),GetActorLocation(),.45f);return false;}
        InvalidUntil=0;
        if(Marker){UHeliosWeaponVFX::ReleaseTracked(Marker);Marker=nullptr;}
        Marker=MH::FX(this,TEXT("NS_MH_Marker"),StrikePoint+StrikeNormal*2,StrikeNormal.Rotation(),1.1f);
    }
    Gesture=Requested;GestureStart=Now;bCommitted=false;
    PreviewEnemy.Reset();NextPreviewTime=0;
    UpdateHandPose(true);
    return true;
}
void AHeliosMysteriousHand::Tick(float Dt)
{
    Super::Tick(Dt);if(!bActive||!Settings)return;
    if(HeliosWeaponHolderContext::IsDead(GetOwner())){CancelGesture();return;}
    const double Now=GetWorld()->GetTimeSeconds();
    if(!Gesture && !GetWorld()->IsPaused() && Now>=NextPreviewTime)
    {
        NextPreviewTime=Now+.05;
        FVector Point,Normal,Spawn;AHeliosEnemyActor* Candidate=nullptr;
        ResolveStrike(Point,Normal,Spawn,&Candidate);
        PreviewEnemy=Candidate;
        if(Candidate)PreviewPosition=Candidate->GetActorLocation();
    }
    if(Gesture)
    {
        const float Age=Now-GestureStart;const float Duration=Gesture==1?Settings->RefireSeconds:1.f;
        UpdateHandPose();
        const float CommitTime=Gesture==1?Duration/3.f:.45f;
        if(!bCommitted&&Age>=CommitTime)
        {bCommitted=true;if(Gesture==1)CommitLaser();else CommitMissile();}
        if(Age>=Duration)
        {
            const uint8 Next=Buffered;Buffered=0;Gesture=0;
            // Resolve the queued state before updating the visual pose: no one-frame idle flash.
            if(!Next || !BeginGesture(Next,Next==2))UpdateHandPose();
        }
    }
    else
    {
        IdleTime+=Dt;
        UpdateHandPose();
    }
}
void AHeliosMysteriousHand::UpdateHandPose(bool bRestart)
{
    auto* Anim=Cast<UHeliosHandAnimInstance>(GetFirstPersonMesh()->GetAnimInstance());
    if(!Anim)return;
    if(!Gesture){Anim->SetTimelinePose(IdleAnim,IdleTime,true,bRestart);return;}
    const float Age=GetWorld()?float(GetWorld()->GetTimeSeconds()-GestureStart):0.f;
    const float Seconds=Gesture==1?Age*.6f/FMath::Max(.2f,Settings->RefireSeconds):Age;
    Anim->SetTimelinePose(Gesture==1?PointAnim:BooAnim,Seconds,false,bRestart);
}
float AHeliosMysteriousHand::GetMissileCooldownRemaining() const
{return 0.f;}
FString AHeliosMysteriousHand::GetHandStatus() const
{
    if(GetWorld()&&GetWorld()->GetTimeSeconds()<InvalidUntil)return TEXT("此处无法召唤导弹");
    if(Gesture==2)return TEXT("落点已固定 · 定点轰炸");
    if(Gesture)return TEXT("无限点射 · 施放中");
    return Settings&&Settings->bMissileAimAssist?TEXT("辅助选中 · 定点轰炸 · 半径10米"):TEXT("无限点射 · 定点轰炸 · 半径10米");
}
void AHeliosMysteriousHand::CommitLaser()
{
    FVector Start,Dir;if(!View(Start,Dir))return;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(MHLaser),true,GetOwner());Q.AddIgnoredActor(this);
    FVector End=Start+Dir*Settings->Range;float BestDot=FMath::Cos(FMath::DegreesToRadians(Settings->AimHalfAngle));float BestDist=Settings->Range+1;
    for(TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It)
    {
        if(!MH::Alive(*It))continue;
        const FVector P=It->GetActorLocation()+FVector(0,0,25);const FVector V=P-Start;const float Dist=V.Size();
        if(Dist>KINDA_SMALL_NUMBER&&Dist<=Settings->Range)
        {
            const float Dot=FVector::DotProduct(V/Dist,Dir);
            if(Dot<BestDot || (FMath::IsNearlyEqual(Dot,BestDot,.00001f)&&Dist>=BestDist))continue;
            FHitResult H;GetWorld()->LineTraceSingleByChannel(H,Start,P,ECC_Visibility,Q);
            if(H.bBlockingHit&&H.GetActor()!=*It)continue;
            BestDot=Dot;BestDist=Dist;End=P;
        }
    }
    FHitResult CameraHit;GetWorld()->LineTraceSingleByChannel(CameraHit,Start,End+(End-Start).GetSafeNormal()*3,ECC_Visibility,Q);
    if(CameraHit.bBlockingHit)End=CameraHit.ImpactPoint;
    auto* Hand=GetFirstPersonMesh();Hand->TickAnimation(0,false);Hand->RefreshBoneTransforms();
    FVector Tip=Hand->GetSocketLocation(TEXT("IndexTip"));
    if(auto* CharacterHands=GetOwner()->FindComponentByClass<UHeliosCharacterPresentationComponent>();CharacterHands&&CharacterHands->IsReady())
        Tip=CharacterHands->GetAnchorTransform(TEXT("IndexTip")).GetLocation();
    // A viewmodel can penetrate a nearby wall: clamp origin before casting the actual damaging ray.
    FHitResult Near;GetWorld()->LineTraceSingleByChannel(Near,Start,Tip,ECC_Visibility,Q);
    if(Near.bBlockingHit)Tip=Near.ImpactPoint+Near.ImpactNormal*2;
    FHitResult Hit;GetWorld()->LineTraceSingleByChannel(Hit,Tip,End+(End-Tip).GetSafeNormal()*3,ECC_Visibility,Q);
    if(Hit.bBlockingHit)End=Hit.ImpactPoint;
    if(auto* E=Cast<AHeliosEnemyActor>(Hit.GetActor());MH::Alive(E))
        UGameplayStatics::ApplyPointDamage(E,Settings->Damage,(End-Tip).GetSafeNormal(),Hit,GetInstigatorController(),this,UDamageType::StaticClass());
    auto* Beam=MH::FX(this,TEXT("NS_MH_Beam"),Tip,FRotator::ZeroRotator,.3f);
    if(Beam)
    {
        Beam->SetWorldLocation((Tip+End)*.5f);
        Beam->SetWorldRotation(FRotator::ZeroRotator);Beam->SetWorldScale3D(FVector::OneVector);
        Beam->SetVariableVec3(TEXT("User.Direction"),(End-Tip).GetSafeNormal());
        Beam->SetVariableVec3(TEXT("User.CoreScale"),FVector(.015f,.015f,FVector::Dist(Tip,End)/100.f));
        Beam->SetVariableVec3(TEXT("User.HaloScale"),FVector(.06f,.06f,FVector::Dist(Tip,End)/100.f));
        Beam->ReinitializeSystem();
    }
    MH::FX(this,TEXT("NS_MH_FingerFlash"),Tip,(End-Tip).Rotation(),.25f);
    if(Hit.bBlockingHit)MH::FX(this,TEXT("NS_MH_LaserHit"),End,Hit.ImpactNormal.Rotation(),.5f);
    MH::Sound(this,TEXT("SFX_MH_Laser"),Tip,.8f);++LaserCount;OnShotPresented.Broadcast();
}
bool AHeliosMysteriousHand::ResolveStrike(FVector& Point,FVector& Normal,FVector& Spawn,AHeliosEnemyActor** SelectedEnemy) const
{
    if(SelectedEnemy)*SelectedEnemy=nullptr;
    FVector S,D;if(!Settings||!GetWorld()||!View(S,D))return false;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(MHStrike),false,GetOwner());Q.AddIgnoredActor(this);
    FHitResult AimHit;
    const bool bAimHit=GetWorld()->LineTraceSingleByChannel(AimHit,S,S+D*Settings->MissileRange,ECC_Visibility,Q);
    auto AcceptSurface=[&](const FHitResult& Hit)
    {
        // Preserve high-altitude descent and the visible side of a wall; roofs still intercept the missile.
        const FVector Origin=Hit.ImpactPoint+Hit.ImpactNormal*14+FVector(0,0,Settings->SummonHeight);
        if(GetWorld()->OverlapBlockingTestByChannel(Origin,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(13),Q))return false;
        Point=Hit.ImpactPoint;Normal=Hit.ImpactNormal;Spawn=Origin;return true;
    };
    if(Settings->bMissileAimAssist)
    {
        struct FCandidate { AHeliosEnemyActor* Enemy; int64 Alignment; double Distance; bool bDirect; };
        TArray<FCandidate> Candidates;
        const double MinDot=FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(Settings->MissileAimHalfAngle,0.f,45.f)));
        for(TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It)
        {
            if(!MH::Alive(*It))continue;
            const FVector ToEnemy=It->GetActorLocation()-S;
            const double Distance=ToEnemy.Size();
            if(Distance<=KINDA_SMALL_NUMBER||Distance>Settings->MissileRange)continue;
            const bool bDirect=bAimHit&&AimHit.GetActor()==*It;
            const double Dot=FVector::DotProduct(ToEnemy/Distance,D);
            if(!bDirect&&Dot<MinDot)continue;
            FHitResult Sight;
            if(!bDirect&&GetWorld()->LineTraceSingleByChannel(Sight,S,It->GetActorLocation(),ECC_Visibility,Q)&&Sight.GetActor()!=*It)continue;
            // Quantize only the ranking, not eligibility. Equal angles must not beat distance due to
            // normalization roundoff; integer keys also preserve strict ordering for the sort.
            Candidates.Add({*It,FMath::RoundToInt64(Dot*100000000.0),Distance,bDirect});
        }
        Candidates.Sort([](const FCandidate& A,const FCandidate& B)
        {
            if(A.bDirect!=B.bDirect)return A.bDirect;
            if(A.Alignment!=B.Alignment)return A.Alignment>B.Alignment;
            if(A.Distance!=B.Distance)return A.Distance<B.Distance;
            return A.Enemy->GetUniqueID()<B.Enemy->GetUniqueID();
        });
        for(const FCandidate& Candidate:Candidates)
        {
            const auto* Capsule=Candidate.Enemy->GetCapsuleComponent();
            const FVector Feet=Capsule->GetComponentLocation()-FVector(0,0,Capsule->GetScaledCapsuleHalfHeight());
            FCollisionQueryParams GroundQuery=Q;GroundQuery.AddIgnoredActor(Candidate.Enemy);
            FHitResult Ground;
            if(!GetWorld()->LineTraceSingleByChannel(Ground,Feet+FVector(0,0,2),Feet-FVector(0,0,300),ECC_Visibility,GroundQuery))continue;
            if(Ground.bStartPenetrating||Ground.ImpactNormal.Z<.5f||Cast<APawn>(Ground.GetActor()))continue;
            if(!AcceptSurface(Ground))continue;
            if(SelectedEnemy)*SelectedEnemy=Candidate.Enemy;
            return true;
        }
    }
    return bAimHit&&AcceptSurface(AimHit);
}
bool AHeliosMysteriousHand::CommitMissile()
{
    if(Marker){UHeliosWeaponVFX::ReleaseTracked(Marker);Marker=nullptr;}
    // Revalidate stored world-space path, without re-reading aim or the former target actor.
    FCollisionQueryParams Q(SCENE_QUERY_STAT(MHSpawn),false,GetOwner());Q.AddIgnoredActor(this);
    if(GetWorld()->OverlapBlockingTestByChannel(StrikeSpawn,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(13),Q))
    {InvalidUntil=GetWorld()->GetTimeSeconds()+.6;return false;}
    FActorSpawnParameters P;P.Owner=GetOwner();P.Instigator=GetInstigator();P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Missile=GetWorld()->SpawnActor<AHeliosHandMissile>(StrikeSpawn,FRotator::ZeroRotator,P);
    if(!Missile)return false;
    Missile->Launch(StrikePoint,StrikeNormal,Settings);++MissileCount;
    MH::FX(this,TEXT("NS_MH_Portal"),StrikeSpawn,FRotator(90,0,0),.8f);
    FVector Thumb=GetFirstPersonMesh()->GetSocketLocation(TEXT("ThumbTip"));
    if(auto* CharacterHands=GetOwner()->FindComponentByClass<UHeliosCharacterPresentationComponent>();CharacterHands&&CharacterHands->IsReady())
        Thumb=CharacterHands->GetAnchorTransform(TEXT("ThumbTip")).GetLocation();
    MH::Sound(this,TEXT("SFX_MH_Boo"),Thumb,.85f);
    return true;
}
void AHeliosMysteriousHand::DrawHandHUD(UCanvas* C) const
{
    if(!C||!bActive||!GetWorld())return;
    if(HeliosWeaponHolderContext::IsDead(GetOwner()))return;
    const float X=C->ClipX*.5f+46,Y=C->ClipY*.5f+38;
    const FLinearColor Color=GetWorld()->GetTimeSeconds()<InvalidUntil?FLinearColor(1,.12f,.08f):Gesture?FLinearColor(.65f,.3f,1):FLinearColor(1,.65f,.2f);
    // Small outlined down-thumb silhouette, independent of font glyph availability.
    const FVector2D P[]={{-7,-6},{5,-6},{7,-3},{7,3},{2,3},{1,9},{-2,9},{-3,1},{-7,1},{-7,-6}};
    for(int32 I=1;I<UE_ARRAY_COUNT(P);++I)C->K2_DrawLine(FVector2D(X,Y)+P[I-1],FVector2D(X,Y)+P[I],1.5f,Color);
    const bool bFixed=Gesture==2;
    if(!bFixed&&(Gesture||!Settings||!Settings->bMissileAimAssist||!MH::Alive(PreviewEnemy.Get())))return;
    const FVector WorldPoint=bFixed?StrikePoint+StrikeNormal*4:PreviewPosition;
    FVector ViewStart,ViewDirection;
    if(!View(ViewStart,ViewDirection)||FVector::DotProduct(WorldPoint-ViewStart,ViewDirection)<=0)return;
    const FVector Screen=C->Project(WorldPoint);
    if(Screen.X<0||Screen.X>C->ClipX||Screen.Y<0||Screen.Y>C->ClipY)return;
    const FVector2D Center(Screen.X,Screen.Y);
    const FLinearColor TargetColor=bFixed?FLinearColor(.75f,.4f,1):FLinearColor(1,.72f,.2f);
    const float Radius=bFixed?10.f:18.f;
    for(float SX:{-1.f,1.f})for(float SY:{-1.f,1.f})
    {
        const FVector2D Corner=Center+FVector2D(SX*Radius,SY*Radius);
        C->K2_DrawLine(Corner,Corner-FVector2D(SX*7,0),2,TargetColor);
        C->K2_DrawLine(Corner,Corner-FVector2D(0,SY*7),2,TargetColor);
    }
}

AHeliosHandMissile::AHeliosHandMissile()
{
    PrimaryActorTick.bCanEverTick=true;Mesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Missile"));SetRootComponent(Mesh);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetCastShadow(true);
}
void AHeliosHandMissile::Launch(const FVector& Destination,const FVector& Normal,const UHeliosMysteriousHandDefinition* D)
{
    Origin=GetActorLocation();Target=Destination;TargetNormal=Normal;Duration=FMath::Max(.1f,D->MissileFlightSeconds);
    LastWakePoint=Origin;
    MaxDamage=D->MissileMaxDamage;MinDamage=D->MissileMinDamage;Radius=D->MissileRadius;
    Mesh->SetStaticMesh(MH::Asset<UStaticMesh>(TEXT("SM_MH_Missile")));
    SetActorRotation(FRotationMatrix::MakeFromZ((Target-Origin).GetSafeNormal()).Rotator());
    Trail=UHeliosWeaponVFX::SpawnAttachedTrail(MH::Asset<UNiagaraSystem>(TEXT("NS_MH_Trail")),Mesh);
    if(Trail){Trail->SetWorldRotation(FRotator::ZeroRotator);Trail->SetVariableVec3(TEXT("User.Direction"),FVector::UpVector);Trail->ReinitializeSystem();}
    FallAudio=HeliosAudio::SpawnSoundAttached(MH::Asset<USoundBase>(TEXT("SFX_MH_FallCinematic")),Mesh,NAME_None,FVector::ZeroVector,EAttachLocation::KeepRelativeOffset,true,.7f);
    SetLifeSpan(Duration+2);
}
void AHeliosHandMissile::Tick(float Dt)
{
    Super::Tick(Dt);if(bExploded)return;Age+=Dt;
    const float T=FMath::Clamp(Age/Duration,0.f,1.f);const FVector Next=FMath::Lerp(Origin,Target,T*T);
    FCollisionQueryParams Q(SCENE_QUERY_STAT(MHMissile),false,GetOwner());Q.AddIgnoredActor(this);FHitResult H;
    if(GetWorld()->SweepSingleByChannel(H,GetActorLocation(),Next,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(9),Q))
    {EmitWake(H.ImpactPoint);Explode(H.ImpactPoint,H.ImpactNormal);return;}
    WakeAge+=Dt;if(WakeAge>=.05f){EmitWake(Next);WakeAge=0;}
    SetActorLocation(Next);if(T>=1)Explode(Target,TargetNormal);
}
void AHeliosHandMissile::EmitWake(const FVector& End)
{
    const float Length=FVector::Dist(End,LastWakePoint);
    if(Length>12)
        UHeliosWeaponVFX::SpawnOneShot(this,MH::Asset<UNiagaraSystem>(TEXT("NS_MH_Wake")),(End+LastWakePoint)*.5f,FRotator::ZeroRotator,FVector(1,1,FMath::Max(.15f,Length/200.f)),1.5f);
    LastWakePoint=End;
}
void AHeliosHandMissile::Explode(const FVector& P,const FVector& N)
{
    if(bExploded)return;bExploded=true;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(MHBlast),false,GetOwner());Q.AddIgnoredActor(this);
    for(TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It)
    {
        if(!MH::Alive(*It))continue;
        const FVector Center=It->GetActorLocation();float Distance=FVector::Dist(P,Center);if(Distance>Radius)continue;
        FHitResult H;GetWorld()->LineTraceSingleByChannel(H,P+N*12,Center,ECC_Visibility,Q);
        if(H.bBlockingHit&&H.GetActor()!=*It)continue;
        UGameplayStatics::ApplyDamage(*It,FMath::Lerp(MaxDamage,MinDamage,FMath::Clamp(Distance/Radius,0.f,1.f)),GetInstigatorController(),this,UDamageType::StaticClass());
    }
    if(auto* Impact=GetWorld()->SpawnActor<AHeliosHandImpact>(P,FRotator::ZeroRotator))Impact->Initialize(N,Radius);
    Destroy();
}
void AHeliosHandMissile::EndPlay(const EEndPlayReason::Type R)
{
    if(FallAudio)FallAudio->Stop();
    if(Trail)
    {
        Trail->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
        Trail->Deactivate();
        TWeakObjectPtr<UNiagaraComponent> WeakTrail(Trail);FTimerHandle Handle;
        GetWorld()->GetTimerManager().SetTimer(Handle,[WeakTrail](){if(WeakTrail.IsValid())UHeliosWeaponVFX::ReleaseTracked(WeakTrail.Get());},1.3f,false);
    }
    Super::EndPlay(R);
}
