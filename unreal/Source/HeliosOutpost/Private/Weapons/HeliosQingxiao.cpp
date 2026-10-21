#include "Weapons/HeliosQingxiao.h"
#include "Audio/HeliosAudio.h"
#include "Weapons/HeliosWeaponDamageSettings.h"
#include "Weapons/HeliosFirstPersonPresentationComponent.h"
#include "Weapons/HeliosWeaponVFX.h"
#include "Player/HeliosArenaCharacter.h"
#include "Weapons/HeliosWeaponHolderContext.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraSystem.h"
#include "ProceduralMeshComponent.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundConcurrency.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
FCollisionQueryParams SwordWallQuery(UWorld* World)
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(QXSwordWall),false);
    // Characters do not intercept swords assigned to another target.
    for(TActorIterator<APawn> It(World);It;++It)Query.AddIgnoredActor(*It);
    return Query;
}
bool SweepSwordWall(UWorld* World,FHitResult& Hit,const FVector& Start,const FVector& End,const FCollisionQueryParams& Query)
{
    return World->SweepSingleByChannel(Hit,Start,End,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(18.f),Query);
}
bool SwordPathClear(UWorld* World,const FVector& Start,const AHeliosEnemyActor* Enemy,const FCollisionQueryParams& Query)
{
    const FVector Goal=Enemy->GetActorLocation()+FVector(0,0,20);
    FHitResult Wall,TargetHit;
    if(!SweepSwordWall(World,Wall,Start,Goal,Query))return true;
    const bool bTargetHit=Enemy->GetCapsuleComponent()->SweepComponent(TargetHit,Start,Goal,FQuat::Identity,FCollisionShape::MakeSphere(18.f));
    return bTargetHit&&TargetHit.Time<Wall.Time;
}
template<class T> T* QX(const TCHAR* Name)
{ return LoadObject<T>(nullptr,*(FString(TEXT("/Game/Helios/Weapons/Qingxiao/"))+Name)); }
void Cosmetic(UPrimitiveComponent* C)
{ C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetGenerateOverlapEvents(false);C->SetCanEverAffectNavigation(false);C->SetCastShadow(false); }
void Sound(UWorld* W,const FVector& P,const TCHAR* Name,float Volume=.45f)
{
    const bool Rend=FString(Name).StartsWith(TEXT("SFX_QX_Rend"));
    if(auto* S=QX<USoundBase>(Name)) HeliosAudio::PlaySoundAtLocation(W,S,P,Volume,1.f,0.f,
        QX<USoundAttenuation>(Rend?TEXT("ATT_QX_Rend"):TEXT("ATT_QX")),QX<USoundConcurrency>(Rend?TEXT("SC_QX_Rend"):TEXT("SC_QX_Action")));
    if(FParse::Param(FCommandLine::Get(),TEXT("QXCapture")))
        UE_LOG(LogTemp,Display,TEXT("QX_AUDIO %s t=%.6f volume=%.3f"),Name,W->GetTimeSeconds(),Volume);
}
void Burst(UWorld* W,const FVector& P,const TCHAR* Name,float Scale=1)
{ UHeliosWeaponVFX::SpawnOneShot(W,QX<UNiagaraSystem>(Name),P,FRotator(-90,0,0),FVector(Scale),.35f); }
AHeliosQingxiaoBurst* Shatter(UWorld* W,AActor* Owner,const FVector& Point,const FVector& Normal,const FVector& Direction,EQingxiaoBurst Kind,float Scale=1,int32 Seed=0,AActor* Follow=nullptr)
{
    FActorSpawnParameters Params;Params.Owner=Owner;
    auto* FX=W->SpawnActor<AHeliosQingxiaoBurst>(Params);
    if(FX){FX->Follow=Follow;FX->Initialize(Point,Normal,Direction,Kind,Scale,Seed);}
    return FX;
}
}

UHeliosQingxiaoDefinition::UHeliosQingxiaoDefinition()
{ WeaponId=TEXT("qingxiao");DisplayName=FText::FromString(TEXT("青霄 · 五剑归元"));MagazineSize=1;ReloadSeconds=0;Damage=70;Range=500;Recoil=0;RefireSeconds=.32f; }
UHeliosQingxiaoFormation::UHeliosQingxiaoFormation()
{ PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickGroup=TG_PrePhysics; }
UHeliosQingxiaoFormation* UHeliosQingxiaoFormation::Get(AActor* Player,bool Create)
{
    if(!IsValid(Player))return nullptr;
    auto* F=Player->FindComponentByClass<UHeliosQingxiaoFormation>();
    if(!F&&Create){F=NewObject<UHeliosQingxiaoFormation>(Player);Player->AddInstanceComponent(F);F->RegisterComponent();}
    return F;
}
int32 UHeliosQingxiaoFormation::Remaining() const
{ int32 N=0;for(auto S:Swords)if(IsValid(S)&&!S->IsActorBeingDestroyed())++N;return N; }
FText UHeliosQingxiaoFormation::Status() const
{ return FText::FromString(bActive?FString::Printf(TEXT("御剑中 · %d / 5"),Remaining()):TEXT("剑阵就绪 · 五剑归元")); }
bool UHeliosQingxiaoFormation::Eligible(const AHeliosEnemyActor* E) const
{ return Settings&&IsValid(E)&&!E->IsActorBeingDestroyed()&&!E->IsEnemyDead()&&E->GetHealth()>0&&FVector::DistSquared(Origin,E->GetActorLocation())<=FMath::Square(Settings->SearchRadius); }
AHeliosEnemyActor* UHeliosQingxiaoFormation::SelectTarget(const AHeliosQingxiaoSword* Requester) const
{
    const FVector Start=IsValid(Requester)?Requester->GetActorLocation():ViewOrigin;
    const auto WallQuery=SwordWallQuery(GetWorld());
    AHeliosEnemyActor* Best=nullptr;bool bBestClear=false;int32 MinAssignments=MAX_int32;double BestDistance=MAX_dbl;
    for(TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It)
    {
        auto* E=*It;if(!Eligible(E))continue;
        const bool bClear=SwordPathClear(GetWorld(),Start,E,WallQuery);
        int32 Assigned=0;for(auto S:Swords)if(IsValid(S)&&S!=Requester&&!S->IsDissolving()&&S->Target==E)++Assigned;
        const double D=FVector::DistSquared(Origin,E->GetActorLocation());
        if(!Best||(bClear&&!bBestClear)||(bClear==bBestClear&&(Assigned<MinAssignments||(Assigned==MinAssignments&&(D<BestDistance||(D==BestDistance&&E->GetUniqueID()<Best->GetUniqueID()))))))
        {Best=E;bBestClear=bClear;MinAssignments=Assigned;BestDistance=D;}
    }
    return Best;
}
bool UHeliosQingxiaoFormation::Begin(const UHeliosQingxiaoDefinition* D)
{
    auto* P=Cast<APawn>(GetOwner());
    if(bActive||!D||!P||HeliosWeaponHolderContext::IsDead(P)||GetWorld()->IsPaused())return false;
    Clear(false);Settings=const_cast<UHeliosQingxiaoDefinition*>(D);Origin=P->GetActorLocation();ViewOrigin=HeliosWeaponHolderContext::ViewLocation(P);ViewRotation=HeliosWeaponHolderContext::ViewRotation(P);
    Age=0;bActive=true;++WaveSerial;
    const FVector Forward=ViewRotation.Vector(),Right=FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y),Up=FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Z);
    FHitResult Ceiling;FCollisionQueryParams Params(SCENE_QUERY_STAT(QXCeiling),false,P);
    bCompressed=GetWorld()->LineTraceSingleByChannel(Ceiling,ViewOrigin,ViewOrigin+FVector(0,0,220),ECC_Visibility,Params);
    const float Height=bCompressed?4.f:20.f,Spread=bCompressed?45.f:66.f;
    Seal=NewObject<UStaticMeshComponent>(P);Seal->SetStaticMesh(QX<UStaticMesh>(TEXT("SM_QX_Seal")));Cosmetic(Seal);Seal->RegisterComponent();
    Seal->SetWorldLocation(ViewOrigin+Forward*380+Up*(Height+40));
    Seal->SetWorldRotation(FRotationMatrix::MakeFromZ(-Forward).Rotator());Seal->SetWorldScale3D(FVector(bCompressed?.95f:1.18f));
    SealMaterial=Seal->CreateDynamicMaterialInstance(0,QX<UMaterialInterface>(TEXT("M_QX_Seal")));
    const auto WallQuery=SwordWallQuery(GetWorld());
    for(int32 I=0;I<5;++I)
    {
        const float X=float(I-2);
        FVector Pos=ViewOrigin+Forward*(340+FMath::Abs(X)*10)+Right*X*Spread+Up*(Height+(bCompressed?12.f:36.f)-12*FMath::Abs(X));
        FHitResult SpawnWall;
        const bool bBlocked=SweepSwordWall(GetWorld(),SpawnWall,ViewOrigin,Pos,WallQuery);
        if(bBlocked)Pos=SpawnWall.Location;
        FActorSpawnParameters Spawn;Spawn.Owner=P;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Sword=GetWorld()->SpawnActor<AHeliosQingxiaoSword>(Pos,FRotator::ZeroRotator,Spawn);
        if(!Sword){Clear();return false;}
        Swords.Add(Sword);Sword->Initialize(this,I,Pos);++SpawnedTotal;
        if(bBlocked)Sword->BlockAt(SpawnWall);
    }
    for(auto S:Swords)S->Target=SelectTarget(S);
    Sound(GetWorld(),ViewOrigin,TEXT("SFX_QX_Summon"),.7f);
    return true;
}
void UHeliosQingxiaoFormation::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Dt,Type,Function);if(!bActive)return;
    auto* P=Cast<APawn>(GetOwner());if(!P||HeliosWeaponHolderContext::IsDead(P)){Clear();return;}
    Age+=Dt;
    if(SealMaterial)SealMaterial->SetScalarParameterValue(TEXT("Opacity"),FMath::Min(1.f,Age/.16f)*FMath::Clamp(1.f-(Age-.6f)/.5f,0.f,1.f)*.62f);
    if(Seal)Seal->AddLocalRotation(FRotator(0,Dt*13,0));
    const float MaxAge=FMath::Max(.9f,Settings->MaxWaveSeconds),Fade=FMath::Clamp(Settings->DissolveSeconds,.05f,.8f);
    if(Age>=MaxAge-Fade)for(auto S:Swords)if(IsValid(S))S->Dissolve();
    if(Age>=MaxAge||Remaining()==0)Clear(false);
}
void UHeliosQingxiaoFormation::Clear(bool ClearImpacts)
{
    for(auto S:Swords)if(IsValid(S))S->Destroy();Swords.Reset();
    if(Seal){Seal->DestroyComponent();Seal=nullptr;}SealMaterial=nullptr;bActive=false;Age=0;
    if(ClearImpacts&&GetWorld())for(TActorIterator<AHeliosQingxiaoImpact> I(GetWorld());I;++I)if(I->GetOwner()==GetOwner())I->Destroy();
    if(ClearImpacts&&GetWorld())
    {
        for(TActorIterator<AHeliosQingxiaoBurst> I(GetWorld());I;++I)if(I->GetOwner()==GetOwner())I->Destroy();
        for(TActorIterator<AHeliosQingxiaoRibbon> I(GetWorld());I;++I)if(I->GetOwner()==GetOwner())I->Destroy();
    }
}
void UHeliosQingxiaoFormation::EndPlay(EEndPlayReason::Type Reason){Clear();Super::EndPlay(Reason);}

AHeliosQingxiaoImpact::AHeliosQingxiaoImpact()
{
    PrimaryActorTick.bCanEverTick=true;
    Mesh=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Crossed spirit tears"));SetRootComponent(Mesh);Cosmetic(Mesh);
    InitialLifeSpan=.55f;
}
void AHeliosQingxiaoImpact::Initialize(AHeliosEnemyActor* Enemy,const FVector& Eye,int32 Variation)
{
    Victim=Enemy;Seed=Variation;
    const FVector Facing=(Eye-Enemy->GetActorLocation()).GetSafeNormal(SMALL_NUMBER,FVector::ForwardVector);
    Offset=Facing*(Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius()+9)+FVector(0,0,12);
    Right=FVector::CrossProduct(FVector::UpVector,Facing).GetSafeNormal(SMALL_NUMBER,FVector::RightVector);
    Up=FVector::CrossProduct(Facing,Right).GetSafeNormal();SetActorLocation(Enemy->GetActorLocation()+Offset);
    Mesh->SetMaterial(0,QX<UMaterialInterface>(TEXT("M_QX_Rend")));
    Mesh->SetMaterial(1,QX<UMaterialInterface>(TEXT("M_QX_RendGlow")));
    Shatter(GetWorld(),GetOwner(),GetActorLocation(),Facing,Right,EQingxiaoBurst::LightHit,1.45f,Variation,Enemy);
    Tick(0);
}
void AHeliosQingxiaoImpact::Tick(float Dt)
{
    Super::Tick(Dt);Age+=Dt;if(Age>=.52f){Destroy();return;}
    if(Victim.IsValid())SetActorLocation(Victim->GetActorLocation()+Offset);
    struct FLayer{TArray<FVector> V,N;TArray<int32> T;TArray<FVector2D> UV;TArray<FLinearColor> C;TArray<FProcMeshTangent> Tangents;};
    FLayer Cuts,Glow;
    auto Pair=[&](FLayer& L,const FVector& Center,const FVector& Side,float Width,float U,float Alpha,int32 Step)
    {
        const int32 K=L.V.Num();L.V.Add(Center-Side*Width);L.V.Add(Center+Side*Width);
        L.UV.Add(FVector2D(U,0));L.UV.Add(FVector2D(U,1));
        L.N.Add(FVector::ForwardVector);L.N.Add(FVector::ForwardVector);
        L.C.Add(FLinearColor(1,1,1,Alpha));L.C.Add(FLinearColor(1,1,1,Alpha));
        if(Step)L.T.Append({K-2,K-1,K,K,K-1,K+1});
    };
    for(int32 Slash=0;Slash<3;++Slash)
    {
        const float LocalAge=Age-Slash*.025f;if(LocalAge<0)continue;
        const float Angle=FMath::DegreesToRadians((Slash==0?51.f:Slash==1?-57.f:7.f)+(Seed%3-1)*8.f);
        const FVector Axis=Right*FMath::Cos(Angle)+Up*FMath::Sin(Angle),Side=Right*(-FMath::Sin(Angle))+Up*FMath::Cos(Angle);
        const float Grow=FMath::Clamp((LocalAge+.015f)/.065f,0.f,1.f);
        const float Fade=FMath::Square(FMath::Clamp(1-(LocalAge-.11f)/.35f,0.f,1.f));
        for(int32 I=0;I<=20;++I)
        {
            const float U=I/20.f,T=U*2-1,Envelope=FMath::Pow(FMath::Max(0.f,FMath::Sin(U*PI)),.65f);
            const float Jag=(FMath::Sin(I*2.7f+Seed)+.4f*FMath::Sin(I*6.1f))*(Slash==2?1.5f:3.f)*Envelope;
            const FVector P=Axis*T*(Slash==2?80.f:127.f)*Grow+Side*Jag;
            Pair(Cuts,P,Side,(Slash==2?8.f:14.f)*Envelope*Grow,U,Fade,I);
        }
    }
    if(Age<.075f)
    {
        const FVector Axis=(Right+Up*.6f).GetSafeNormal(),Side=(Up-Right*.6f).GetSafeNormal();
        for(int32 I=0;I<3;++I){const float U=I*.5f;Pair(Glow,Axis*((U-.5f)*105),Side,I==1?19.f:.1f,U,.95f*(1-Age/.075f),I);}
    }
    if(Age<.30f)
    {
        const float Radius=18+112*FMath::Clamp(Age/.24f,0.f,1.f),Alpha=.50f*FMath::Square(1-Age/.30f);
        for(int32 I=0;I<=64;++I){const float A=I*2*PI/64;const FVector Radial=Right*FMath::Cos(A)+Up*FMath::Sin(A);Pair(Glow,Radial*Radius,Radial,2.2f,I/64.f,Alpha,I);}
        for(int32 Ray=0;Ray<18;++Ray)
        {
            const float A=Ray*2.39996f+Seed*.37f;const FVector Axis=Right*FMath::Cos(A)+Up*FMath::Sin(A),Side=Right*(-FMath::Sin(A))+Up*FMath::Cos(A);
            const float Travel=Age*(210+(Ray%5)*80),Length=11+(Ray%4)*8;
            for(int32 I=0;I<3;++I){const float U=I*.5f;Pair(Glow,Axis*(Travel+U*Length),Side,I==1?2.8f:.05f,U,(1-Age/.30f)*.85f,I);}
        }
    }
    int32 Section=0;for(FLayer* L:{&Cuts,&Glow})Mesh->CreateMeshSection_LinearColor(Section++,L->V,L->T,L->N,L->UV,L->C,L->Tangents,false);
}

AHeliosQingxiaoRibbon::AHeliosQingxiaoRibbon()
{
    PrimaryActorTick.bCanEverTick=true;Mesh=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Silken sword wake"));SetRootComponent(Mesh);Cosmetic(Mesh);
}
void AHeliosQingxiaoRibbon::Add(const FVector& Inner,const FVector& Outer)
{
    if(Inners.Num()>=64){Inners.RemoveAt(0);Outers.RemoveAt(0);Times.RemoveAt(0);}
    Inners.Add(Inner);Outers.Add(Outer);Times.Add(Clock);
}
void AHeliosQingxiaoRibbon::Tick(float Dt)
{
    Super::Tick(Dt);Clock+=Dt;
    while(Times.Num()&&Clock-Times[0]>FadeSeconds){Times.RemoveAt(0);Inners.RemoveAt(0);Outers.RemoveAt(0);}
    if(Inners.Num()<2){Mesh->ClearAllMeshSections();if(Times.IsEmpty()&&Clock>.1f)Destroy();return;}
    if(!Mesh->GetMaterial(0))
    {
        Mesh->SetMaterial(0,QX<UMaterialInterface>(bFlight?TEXT("M_QX_FlightRibbon"):TEXT("M_QX_Ribbon")));
        Mesh->SetMaterial(1,QX<UMaterialInterface>(TEXT("M_QX_BurstGlow")));
    }
    TArray<FVector> V,N;TArray<int32> Tri;TArray<FVector2D> UV;TArray<FLinearColor> Colors;TArray<FProcMeshTangent> Tangents;
    for(int32 I=0;I<Inners.Num();++I)
    {
        V.Add(Inners[I]);V.Add(Outers[I]);N.Add(FVector::UpVector);N.Add(FVector::UpVector);
        UV.Add(FVector2D(float(I)/(Inners.Num()-1),0));UV.Add(FVector2D(float(I)/(Inners.Num()-1),1));
        const float Alpha=FMath::Clamp(1-(Clock-Times[I])/FadeSeconds,0.f,1.f)*FMath::Clamp(MotionStrength,.25f,1.f);
        Colors.Add(FLinearColor(1,1,1,Alpha));Colors.Add(FLinearColor(1,1,1,Alpha));
        if(I){const int32 K=I*2;Tri.Append({K-2,K-1,K,K,K-1,K+1});}
    }
    Mesh->CreateMeshSection_LinearColor(0,V,Tri,N,UV,Colors,Tangents,false);
    // Bright cutting edge is locked to the blade; the broad body is subordinate.
    for(int32 I=0;I<Inners.Num();++I)
    {
        const FVector Across=Outers[I]-Inners[I];
        const FVector Center=bFlight?(Outers[I]+Inners[I])*.5:Outers[I]-Across*.04;
        const FVector Half=Across*(bFlight?.08f:.025f);
        V[I*2]=Center-Half;V[I*2+1]=Center+Half;
        const float Alpha=Colors[I*2].A;
        Colors[I*2]=Colors[I*2+1]=bFlight&&I%13==0?FLinearColor(1.f,.5f,.045f,Alpha):FLinearColor(.55f,.85f,1.f,Alpha);
    }
    Mesh->CreateMeshSection_LinearColor(1,V,Tri,N,UV,Colors,Tangents,false);
}

AHeliosQingxiaoSword::AHeliosQingxiaoSword()
{
    PrimaryActorTick.bCanEverTick=true;
    Blade=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Qingxiao spirit blade"));SetRootComponent(Blade);Cosmetic(Blade);
    Halo=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Blade aura"));Halo->SetupAttachment(Blade);Cosmetic(Halo);
}
void AHeliosQingxiaoSword::Initialize(UHeliosQingxiaoFormation* F,int32 Slot,const FVector& Position)
{
    Formation=F;Index=Slot;Birth=Position;Blade->SetStaticMesh(QX<UStaticMesh>(TEXT("SM_Qingxiao")));
    for(int32 I=0;I<Blade->GetNumMaterials();++I)if(auto* M=Blade->CreateDynamicMaterialInstance(I))Materials.Add(M);
    Halo->SetStaticMesh(Blade->GetStaticMesh());Halo->SetRelativeScale3D(FVector(1.8f,1.25f,1.006f));
    AuraMaterial=UMaterialInstanceDynamic::Create(QX<UMaterialInterface>(TEXT("M_QX_Aura")),this);
    for(int32 I=0;I<Halo->GetNumMaterials();++I)Halo->SetMaterial(I,AuraMaterial);
    // Fan the tips inward; the blade's authored +Z axis points along the flight direction.
    FVector Dir=(F->ViewRotation.Vector()*(F->bCompressed?.95f:.28f)+FVector::UpVector*(F->bCompressed?.25f:.95f)+FRotationMatrix(F->ViewRotation).GetUnitAxis(EAxis::Y)*float(2-Index)*.15f).GetSafeNormal();
    SetActorRotation(FRotationMatrix::MakeFromZX(Dir,-F->ViewRotation.Vector()).Rotator());
    SetActorScale3D(FVector(F->bCompressed?.65f:.85f));
    for(auto M:Materials)M->SetScalarParameterValue(TEXT("Reveal"),0);
    Burst(GetWorld(),Birth,TEXT("NS_QX_Birth"),.45f);
}
void AHeliosQingxiaoSword::Dissolve(EQingxiaoSwordEnd Reason)
{
    if(IsDissolving())return;DissolveAge=0;EndReason=Reason;
    if(Reason==EQingxiaoSwordEnd::EnemyHit){Burst(GetWorld(),GetActorLocation(),TEXT("NS_QX_Impact"),1.6f);Blade->SetVisibility(false);}
    else if(Reason==EQingxiaoSwordEnd::Natural){Burst(GetWorld(),GetActorLocation(),TEXT("NS_QX_Dissolve"),.5f);Sound(GetWorld(),GetActorLocation(),TEXT("SFX_QX_Dissolve"),.12f);}
    else Blade->SetVisibility(false);
    Halo->SetVisibility(false);
}
void AHeliosQingxiaoSword::BlockAt(const FHitResult& Hit)
{
    if(IsDissolving())return;
    const FVector Normal=Hit.ImpactNormal.GetSafeNormal(SMALL_NUMBER,-GetActorUpVector());
    const FVector Point=Hit.bStartPenetrating?FVector(Hit.Location)+Normal*18.f:FVector(Hit.ImpactPoint);
    SetActorLocation(Hit.Location);
    Shatter(GetWorld(),GetOwner(),Point,Normal,GetActorUpVector(),EQingxiaoBurst::Wall,Formation&&Formation->Settings?Formation->Settings->WallBurstScale:1.f,Index);
    Sound(GetWorld(),Point,TEXT("SFX_QX_Wall"),.7f);
    if(Formation)++Formation->WallBurstsTotal;
    Dissolve(EQingxiaoSwordEnd::WorldBlocked);
}
void AHeliosQingxiaoSword::Tick(float Dt)
{
    Super::Tick(Dt);
    if(!IsValid(Formation)||!Formation->Busy()||!Formation->Settings){Destroy();return;}
    const auto* S=Formation->Settings.Get();
    if(IsDissolving())
    {
        DissolveAge+=Dt;const float P=FMath::Clamp(DissolveAge/FMath::Max(.05f,S->DissolveSeconds),0.f,1.f);
        for(auto M:Materials){M->SetScalarParameterValue(TEXT("Dissolve"),P);M->SetScalarParameterValue(TEXT("Reveal"),1);}
        if(P>=1)Destroy();return;
    }
    const float Launch=S->SummonSeconds+Index*S->LaunchInterval;
    const auto WallQuery=SwordWallQuery(GetWorld());
    if(Formation->Age<Launch)
    {
        const float Reveal=FMath::Clamp(Formation->Age/FMath::Max(.1f,S->SummonSeconds*.35f),0.f,1.f);
        for(auto M:Materials)M->SetScalarParameterValue(TEXT("Reveal"),Reveal);
        if(AuraMaterial){AuraMaterial->SetScalarParameterValue(TEXT("Charge"),FMath::Clamp(Formation->Age/Launch,0.f,1.f));AuraMaterial->SetScalarParameterValue(TEXT("Opacity"),.16f*Reveal);}
        const FVector Hover=Birth+FVector(0,0,FMath::Sin(Formation->Age*5+Index*.5f)*4);
        FHitResult Wall;
        if(SweepSwordWall(GetWorld(),Wall,GetActorLocation(),Hover,WallQuery)){BlockAt(Wall);}
        else SetActorLocation(Hover);
        return;
    }
    if(!bLaunched||!Formation->Eligible(Target.Get()))Target=Formation->SelectTarget(this);
    else if(!SwordPathClear(GetWorld(),GetActorLocation(),Target.Get(),WallQuery))
    {
        auto* Candidate=Formation->SelectTarget(this);
        if(Candidate&&SwordPathClear(GetWorld(),GetActorLocation(),Candidate,WallQuery))Target=Candidate;
    }
    if(!Target.IsValid()){Dissolve();return;}
    if(!bLaunched)
    {
        bLaunched=true;for(auto M:Materials)M->SetScalarParameterValue(TEXT("Reveal"),1);
        if(AuraMaterial){AuraMaterial->SetScalarParameterValue(TEXT("Charge"),1);AuraMaterial->SetScalarParameterValue(TEXT("Opacity"),.11f);}
        Burst(GetWorld(),GetActorLocation(),TEXT("NS_QX_Birth"),.55f);Sound(GetWorld(),GetActorLocation(),TEXT("SFX_QX_Flight"),.22f);
        Shatter(GetWorld(),GetOwner(),GetActorLocation(),Formation->ViewRotation.Vector(),FVector::UpVector,EQingxiaoBurst::Launch,.85f,Index);
        FActorSpawnParameters Spawn;Spawn.Owner=GetOwner();
        Ribbon=GetWorld()->SpawnActor<AHeliosQingxiaoRibbon>(Spawn);Ribbon->FadeSeconds=S->FlightTrailSeconds;Ribbon->bFlight=true;
    }
    if(!Formation->Eligible(Target.Get()))Target=Formation->SelectTarget(this);
    if(!Target.IsValid()){Dissolve();return;}
    // Substeps keep the curved path and swept hit test stable even during a slow frame.
    const int32 Steps=FMath::Clamp(FMath::CeilToInt(Dt/.012f),1,32);const float Step=Dt/Steps;
    for(int32 I=0;I<Steps&&!IsDissolving();++I)
    {
        if(!Formation->Eligible(Target.Get())){Target=Formation->SelectTarget(this);if(!Target.IsValid()){Dissolve();break;}}
        FlightAge+=Step;const FVector Start=GetActorLocation(),Goal=Target->GetActorLocation()+FVector(0,0,20);
        const FVector Toward=Goal-Start;const float Distance=Toward.Size();
        const FVector Lateral=FRotationMatrix(Formation->ViewRotation).GetUnitAxis(EAxis::Y);
        const float Bend=FMath::Clamp(Distance/700.f,0.f,1.f)*FMath::Exp(-FlightAge*3.f);
        const FVector Direction=(Toward.GetSafeNormal()+Lateral*(Index-2)*.20f*Bend+FVector::UpVector*.4f*Bend).GetSafeNormal();
        const float Speed=FMath::Max(500.f,S->SwordSpeed)*FMath::Lerp(.3f,1.f,FMath::Clamp(FlightAge/.20f,0.f,1.f));
        const FVector End=Start+Direction*FMath::Min(Speed*Step,Distance+20.f);
        FHitResult Hit;
        // Prefer clear targets, but fallback shots still stop at the first wall or assigned enemy.
        const bool bHit=Target->GetCapsuleComponent()->SweepComponent(Hit,Start,End,FQuat::Identity,FCollisionShape::MakeSphere(18.f));
        FHitResult Wall;
        const bool bWall=SweepSwordWall(GetWorld(),Wall,Start,End,WallQuery)&&(!bHit||Wall.Time<=Hit.Time);
        SetActorLocation(bWall?Wall.Location:bHit?Hit.Location:End);SetActorRotation(FRotationMatrix::MakeFromZX(Direction,FVector::UpVector).Rotator());
        if(IsValid(Ribbon))
        {
            const FVector V=(Formation->ViewOrigin-GetActorLocation()).GetSafeNormal();
            const FVector Side=FVector::CrossProduct(Direction,V).GetSafeNormal(SMALL_NUMBER,FVector::RightVector)*S->FlightTrailWidth;
            Ribbon->Add(GetActorLocation()-Side,GetActorLocation()+Side);
        }
        if(bWall){BlockAt(Wall);break;}
        if(bHit)
        {
            auto* Player=Cast<APawn>(GetOwner());++HitsDelivered;++Formation->HitsTotal;
            FActorSpawnParameters Spawn;Spawn.Owner=GetOwner();
            if(auto* Rend=GetWorld()->SpawnActor<AHeliosQingxiaoImpact>(Spawn))
            {Rend->Initialize(Target.Get(),Player?Player->GetPawnViewLocation():Formation->ViewOrigin,Index);++Formation->RendTotal;}
            const FString Cue=FString::Printf(TEXT("SFX_QX_Rend%d"),Index%3);
            Sound(GetWorld(),Hit.ImpactPoint,*Cue,.80f);
            const float HealthBefore=Target->CurrentHP;
            UGameplayStatics::ApplyPointDamage(Target.Get(),S->SwordDamage,Direction,Hit,Player?Player->GetController():nullptr,this,UDamageType::StaticClass());
            if(FParse::Param(FCommandLine::Get(),TEXT("QXSettings")))
                UE_LOG(LogTemp,Display,TEXT("QX_SETTINGS_HIT sword=%d configured=%.9g actual=%.9g"),Index,S->SwordDamage,HealthBefore-Target->CurrentHP);
            Dissolve(EQingxiaoSwordEnd::EnemyHit);
        }
    }
}
void AHeliosQingxiaoSword::EndPlay(EEndPlayReason::Type Reason)
{ if(IsValid(Ribbon)&&Reason!=EEndPlayReason::Destroyed)Ribbon->Destroy();Super::EndPlay(Reason); }

AHeliosQingxiao::AHeliosQingxiao()
{ MeshAssetPath=TEXT("/Game/Helios/Weapons/Qingxiao/SM_Qingxiao");PrimaryActorTick.TickGroup=TG_PostPhysics; }
void AHeliosQingxiao::ApplyDefinition(const UHeliosWeaponDefinition* D)
{
    Super::ApplyDefinition(D);D=ActiveDefinition;Settings=const_cast<UHeliosQingxiaoDefinition*>(Cast<UHeliosQingxiaoDefinition>(D));
    if(!Settings){Settings=NewObject<UHeliosQingxiaoDefinition>(this);ApplyWeaponDamageSettings(this,Settings);}
    Slashes.Reset();for(auto& A:Settings->Slashes)Slashes.Add(A.LoadSynchronous());SummonAnimation=Settings->SummonPose.LoadSynchronous();
    // Warm the dedicated impact resources while equipping, before the first strike.
    QX<UMaterialInterface>(TEXT("M_QX_Rend"));QX<UMaterialInterface>(TEXT("M_QX_RendGlow"));
    for(const TCHAR* Cue:{TEXT("SFX_QX_Rend0"),TEXT("SFX_QX_Rend1"),TEXT("SFX_QX_Rend2")})QX<USoundBase>(Cue);
    QX<USoundAttenuation>(TEXT("ATT_QX_Rend"));QX<USoundConcurrency>(TEXT("SC_QX_Rend"));
    for(const TCHAR* Name:{TEXT("M_QX_BurstBody"),TEXT("M_QX_BurstGlow"),TEXT("M_QX_FlightRibbon")})QX<UMaterialInterface>(Name);
    QX<USoundBase>(TEXT("SFX_QX_Wall"));QX<USoundConcurrency>(TEXT("SC_QX_Action"));
    UHeliosQingxiaoFormation::Get(GetOwner());
}
bool AHeliosQingxiao::CanAct() const
{ const auto* P=Cast<APawn>(GetOwner());return P&&!HeliosWeaponHolderContext::IsDead(P)&&!IsHidden()&&!GetWorld()->IsPaused(); }
void AHeliosQingxiao::StartFiring()
{ if(!CanAct())return;const bool NewPress=!bHeld;bHeld=true;if(State==0)Slash();else if(NewPress)bQueuedSlash=true; }
void AHeliosQingxiao::Slash()
{
    State=1;Age=0;++SlashSerial;bQueuedSlash=false;HitActors.Reset();
    SlashPhase=HitStopRemaining=PhaseLag=0;bImpactFeedback=bSwingSound=false;
    FActorSpawnParameters Spawn;Spawn.Owner=GetOwner();
    Arc=GetWorld()->SpawnActor<AHeliosQingxiaoRibbon>(Spawn);Arc->FadeSeconds=Settings->LightTrailSeconds;
}
void AHeliosQingxiao::StartSecondaryFire()
{
    if(!CanAct()||bSecondaryDown)return;bSecondaryDown=true;
    auto* F=UHeliosQingxiaoFormation::Get(GetOwner());if(!F||F->Busy())return;
    if(State==1)bQueuedSummon=true;else if(State==0)Summon();
}
void AHeliosQingxiao::Summon()
{ if(auto* F=UHeliosQingxiaoFormation::Get(GetOwner());F&&F->Begin(Settings)){State=2;Age=0;bQueuedSummon=false;} }
void AHeliosQingxiao::CancelReload()
{
    State=0;Age=0;bHeld=bQueuedSlash=bQueuedSummon=bSecondaryDown=false;
    SlashPhase=HitStopRemaining=PhaseLag=0;bImpactFeedback=bSwingSound=false;
    if(IsValid(Arc))Arc->Destroy();Arc=nullptr;
    if(auto* P=Cast<APawn>(GetOwner()))if(HeliosWeaponHolderContext::Presentation(P))HeliosWeaponHolderContext::Presentation(P)->SetMeleePose(nullptr,0);
}
void AHeliosQingxiao::DeactivateWeapon(){CancelReload();Super::DeactivateWeapon();}
float AHeliosQingxiao::Sweep(float From,float To)
{
    auto* P=Cast<APawn>(GetOwner());if(!P||!HeliosWeaponHolderContext::Presentation(P))return To;
    auto* Pres=HeliosWeaponHolderContext::Presentation(P);auto* Anim=Slashes.IsValidIndex((SlashSerial-1)%2)?Slashes[(SlashSerial-1)%2].Get():nullptr;
    const int32 Steps=FMath::Max(1,FMath::CeilToInt((To-From)*Settings->SlashSeconds/.004f));
    for(int32 I=0;I<=Steps;++I)
    {
        const float Phase=FMath::Lerp(From,To,float(I)/Steps);Pres->SetMeleePose(Anim,Phase);
        if(Phase>=.18f&&!bSwingSound){Sound(GetWorld(),GetActorLocation(),TEXT("SFX_QX_Slash"),.7f);bSwingSound=true;}
        if(Phase<.18f||Phase>.72f)continue;
        GetFirstPersonMesh()->UpdateComponentToWorld();
        const FVector Eye=HeliosWeaponHolderContext::ViewLocation(P),Tip=Pres->GetMeleeTip();
        const FVector Grip=Pres->GetArmsMesh()->GetSocketLocation(TEXT("Grip_R"));
        if(IsValid(Arc))
        {
            const FVector Outer=Tip+(Tip-Grip)*.04f;
            const FVector Inner=FMath::Lerp(Grip,Tip,.65f);
            // Keep the bright outer edge on the blade; broaden only the light-slash ribbon.
            Arc->Add(Outer+(Inner-Outer)*1.4f,Outer);
            Arc->MotionStrength=Phase<.55f?1.f:.6f;
        }
        FCollisionQueryParams Query(SCENE_QUERY_STAT(QXSlash),false,P);Query.AddIgnoredActor(this);
        for(TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It)
        {
            auto* E=*It;if(E->IsEnemyDead()||HitActors.Contains(E)||FVector::DistSquared(Eye,E->GetActorLocation())>FMath::Square(Settings->Range+80))continue;
            // Sample the blade's length, not only its tip: a slash intersects with the cutting edge.
            FHitResult Hit;FVector Direction=FVector::ZeroVector;bool Contact=false;
            for(int32 Sample=0;Sample<6;++Sample)
            {
                const FVector Point=FMath::Lerp(Grip,Tip,.18f+Sample*.164f);
                Direction=(Point-Eye).GetSafeNormal();
                if(FVector::DotProduct(Direction,HeliosWeaponHolderContext::ViewRotation(P).Vector())<.2f)continue;
                if(E->GetCapsuleComponent()->SweepComponent(Hit,Eye,Eye+Direction*Settings->Range,FQuat::Identity,FCollisionShape::MakeSphere(20)))
                {Contact=true;break;}
            }
            if(!Contact)continue;
            FHitResult Wall;if(GetWorld()->LineTraceSingleByChannel(Wall,Eye,Hit.ImpactPoint,ECC_Visibility,Query)&&Wall.GetActor()!=E)continue;
            HitActors.Add(E);UGameplayStatics::ApplyPointDamage(E,Settings->Damage,Direction,Hit,P->GetController(),this,UDamageType::StaticClass());
            const FVector Cut=FRotationMatrix(HeliosWeaponHolderContext::ViewRotation(P)).GetUnitAxis(EAxis::Y)*((SlashSerial%2)?-1.f:1.f)+FVector(0,0,-.35f);
            Shatter(GetWorld(),GetOwner(),Hit.ImpactPoint,(Eye-Hit.ImpactPoint).GetSafeNormal(),Cut,EQingxiaoBurst::LightHit,1,SlashSerial,E);
            Sound(GetWorld(),Hit.ImpactPoint,TEXT("SFX_QX_Impact"),.65f);
            if(!bImpactFeedback)
            {
                bImpactFeedback=true;++LightFeedbackTotal;HitStopRemaining=FMath::Clamp(Settings->LightHitStopSeconds,0.f,.04f);
                Pres->ApplyMeleeImpact(.8f);
                if(auto* PC=Cast<APlayerController>(P->GetController());PC&&PC->PlayerCameraManager)
                    if(auto* Shake=PC->PlayerCameraManager->StartCameraShake(UHeliosQingxiaoShake::StaticClass(),Settings->LightShakeStrength))
                        if(auto* Pattern=Cast<UHeliosQingxiaoShakePattern>(Shake->GetRootShakePattern()))Pattern->Direction=(SlashSerial%2)?-1.f:1.f;
                return Phase;
            }
        }
    }
    return To;
}
void AHeliosQingxiao::Tick(float Dt)
{
    Super::Tick(Dt);if(!CanAct()||!Settings)return;
    auto* P=Cast<APawn>(GetOwner());auto* Pres=HeliosWeaponHolderContext::Presentation(P);Age+=Dt;
    if(State==1)
    {
        const float Duration=FMath::Max(.12f,Settings->SlashSeconds);
        const float Held=FMath::Min(Dt,HitStopRemaining);HitStopRemaining-=Held;PhaseLag+=Held;
        // Resume the same monotonic pose/sweep clock and repay the pause during recovery.
        const float Raw=FMath::Clamp((Age-PhaseLag)/Duration,0.f,1.f);
        float Desired=Raw;
        if(Raw>.72f)Desired=FMath::Lerp(.72f,1.f,FMath::Clamp((Age-(.72f*Duration+PhaseLag))/FMath::Max(.01f,.28f*Duration-PhaseLag),0.f,1.f));
        if(Age>=Duration)Desired=1;
        if(Desired>SlashPhase)
        {
            const float Reached=Sweep(SlashPhase,Desired);
            PhaseLag+=(Desired-Reached)*Duration;
            SlashPhase=Reached;
        }
        if(Age>=Duration)
        {State=0;if(Pres)Pres->SetMeleePose(nullptr,0);if(bQueuedSummon)Summon();else if(bHeld||bQueuedSlash)Slash();}
    }
    else if(State==2)
    {
        if(Pres)Pres->SetMeleePose(SummonAnimation,Age/FMath::Max(.1f,Settings->SummonSeconds));
        if(Age>=Settings->SummonSeconds){State=0;if(Pres)Pres->SetMeleePose(nullptr,0);if(bHeld||bQueuedSlash)Slash();}
    }
}
