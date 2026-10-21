#include "Weapons/HeliosLightstickWeapon.h"
#include "Audio/HeliosAudio.h"
#include "Weapons/HeliosWeaponDamageSettings.h"
#include "Weapons/HeliosLightstickWave.h"
#include "Boss/HeliosBossEffectsComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Player/HeliosArenaCharacter.h"
#include "Weapons/HeliosWeaponHolderContext.h"
#include "Weapons/HeliosFirstPersonPresentationComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Animation/AnimSequence.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundBase.h"

namespace {
template<class T> T* Art(const TCHAR* N) {return LoadObject<T>(nullptr,*(FString(TEXT("/Game/Helios/Weapons/Lightstick/"))+N));}
void Cosmetic(UStaticMeshComponent* C) {C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetGenerateOverlapEvents(false);C->SetCastShadow(false);C->SetCanEverAffectNavigation(false);}
const FLinearColor Cyan(.08f,.8f,1.8f), Violet(.62f,.12f,1.5f), Gold(2.f,1.25f,.42f);
}
UHeliosLightstickDefinition::UHeliosLightstickDefinition(){MagazineSize=1;ReloadSeconds=0;Range=500;Damage=105;Recoil=0;}
AHeliosLightstickWeapon::AHeliosLightstickWeapon()
{
    MeshAssetPath=TEXT("/Game/Helios/Weapons/Lightstick/SM_Lightstick");
    PrimaryActorTick.TickGroup=TG_PostPhysics;
    Pulse=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Encore pulse"));Pulse->SetupAttachment(RootComponent);Cosmetic(Pulse);Pulse->SetVisibility(false);
    Lamp=CreateDefaultSubobject<UPointLightComponent>(TEXT("Core fill"));Lamp->SetupAttachment(GetFirstPersonMesh(),TEXT("Muzzle"));Lamp->SetIntensity(0);Lamp->SetAttenuationRadius(100);Lamp->SetCastShadows(false);
    HitLight=CreateDefaultSubobject<UPointLightComponent>(TEXT("Impact fill"));HitLight->SetupAttachment(RootComponent);HitLight->SetIntensity(0);HitLight->SetAttenuationRadius(190);HitLight->SetCastShadows(false);
}
void AHeliosLightstickWeapon::BeginPlay()
{
    Super::BeginPlay();
    Pulse->SetStaticMesh(Art<UStaticMesh>(TEXT("SM_LightstickRing")));
    PulseMaterial=UMaterialInstanceDynamic::Create(Art<UMaterialInterface>(TEXT("M_LS_FX")),this);Pulse->SetMaterial(0,PulseMaterial);
    UStaticMesh* Cylinder=Art<UStaticMesh>(TEXT("SM_LightstickStreak"));
    for(int32 I=0;I<120;++I)
    {
        auto* C=NewObject<UStaticMeshComponent>(this);C->SetStaticMesh(Cylinder);Cosmetic(C);C->RegisterComponent();C->SetVisibility(false);
        auto* M=UMaterialInstanceDynamic::Create(Art<UMaterialInterface>(TEXT("M_LS_Trail")),this);C->SetMaterial(0,M);
        Streaks.Add(C);StreakMaterials.Add(M);StreakAges.Add(10);StreakLives.Add(1);
    }
}
void AHeliosLightstickWeapon::ApplyDefinition(const UHeliosWeaponDefinition* D)
{
    Super::ApplyDefinition(D);D=ActiveDefinition;Settings=const_cast<UHeliosLightstickDefinition*>(Cast<UHeliosLightstickDefinition>(D));
    if(!Settings){Settings=NewObject<UHeliosLightstickDefinition>(this);ApplyWeaponDamageSettings(this,Settings);}
    Swings.Reset();for(const auto& A:Settings->SwingAnimations)Swings.Add(A.LoadSynchronous());
    ChargePose=Settings->ChargeAnimation.LoadSynchronous();HeavyPose=Settings->HeavyAnimation.LoadSynchronous();
}
bool AHeliosLightstickWeapon::CanAttack() const
{
    auto* P=Cast<APawn>(GetOwner());return P&&!HeliosWeaponHolderContext::IsDead(P)&&!IsHidden()&&GetWorld()&&!GetWorld()->IsPaused();
}
void AHeliosLightstickWeapon::Sound(const TCHAR* N,float Volume){PlayWeaponSound(Art<USoundBase>(N),Volume);}
float AHeliosLightstickWeapon::GetCharge() const {return State==1&&Settings?FMath::Clamp(Age/FMath::Max(.01f,Settings->ChargeSeconds),0.f,1.f):0.f;}
void AHeliosLightstickWeapon::StartFiring()
{
    if(!CanAttack()||State==1)return;bSuspended=false;bHeld=true;if(State==0)BeginSwing(false);
}
void AHeliosLightstickWeapon::StopFiring(){bHeld=false;}
void AHeliosLightstickWeapon::StartSecondaryFire()
{
    if(!CanAttack()||State!=0)return;bSuspended=false;bHeld=false;State=1;Age=0;
    ChargeAudio=HeliosAudio::SpawnSoundAttached(Art<USoundBase>(TEXT("SFX_LightstickCharge")),GetFirstPersonMesh(),NAME_None,FVector::ZeroVector,EAttachLocation::KeepRelativeOffset,true,.7f);
}
void AHeliosLightstickWeapon::StopSecondaryFire()
{
    if(State!=1||!CanAttack()||bSuspended)return;
    ReleasedCharge=GetCharge();if(auto* P=Cast<APawn>(GetOwner()))ReleaseAim=HeliosWeaponHolderContext::ViewRotation(P);
    if(ChargeAudio)ChargeAudio->Stop();BeginSwing(true);
}
void AHeliosLightstickWeapon::SuspendInput(){bHeld=false;bSuspended=true;if(ChargeAudio)ChargeAudio->SetPaused(true);}
void AHeliosLightstickWeapon::BeginSwing(bool Heavy)
{
    if(!Settings)return;
    if(!Heavy&&IdleAge>Settings->ComboResetSeconds)Combo=0;
    State=Heavy?3:2;Age=0;IdleAge=0;HitActors.Reset();bHaveHead=false;bWaveReleased=false;++AttackSerial;
    AttackDuration=Heavy?.3f+Settings->HeavyRecoverySeconds:(Settings->ComboSeconds.IsValidIndex(Combo)?Settings->ComboSeconds[Combo]:.5f);
    Sound(Heavy?TEXT("SFX_LightstickHeavy"):Combo==0?TEXT("SFX_LightstickSwing1"):Combo==1?TEXT("SFX_LightstickSwing2"):TEXT("SFX_LightstickSwing3"));
}
void AHeliosLightstickWeapon::ReleaseWave()
{
    if(bWaveReleased||!CanAttack())return;bWaveReleased=true;++WavesReleased;
    auto* P=Cast<APawn>(GetOwner());const FVector Eye=HeliosWeaponHolderContext::ViewLocation(P);
    FActorSpawnParameters Params;Params.Owner=this;Params.Instigator=P;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Wave=GetWorld()->SpawnActor<AHeliosLightstickWave>(Eye,FRotator(0,ReleaseAim.Yaw,0),Params);
    if(Wave){Wave->Initialize(Settings,ReleasedCharge,Eye);OwnedWaves.RemoveAll([](const auto& W){return !W.IsValid();});OwnedWaves.Add(Wave);}
    if(auto* Pres=HeliosWeaponHolderContext::Presentation(P))Pres->ApplyMeleeImpact(2.4f);
    if(auto* PC=Cast<APlayerController>(P->GetController()))if(PC->PlayerCameraManager)PC->PlayerCameraManager->StartCameraShake(UHeliosBossImpactShake::StaticClass(),2.f);
}
int32 AHeliosLightstickWeapon::GetOwnedWaveCount() const {int32 N=0;for(const auto& W:OwnedWaves)if(W.IsValid())++N;return N;}
void AHeliosLightstickWeapon::ClearWaves(){for(auto& W:OwnedWaves)if(W.IsValid())W->Destroy();OwnedWaves.Reset();}
void AHeliosLightstickWeapon::TraceBlade(const FVector& A,const FVector& B,float Damage)
{
    auto* P=Cast<APawn>(GetOwner());if(!P)return;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(EncoreMelee),false,P);Q.AddIgnoredActor(this);
    // Actors already damaged are ignored by the sweep, but world geometry always blocks it.
    for(const auto& Target:HitActors)if(Target.IsValid())Q.AddIgnoredActor(Target.Get());
    TArray<FHitResult> Hits;GetWorld()->SweepMultiByChannel(Hits,A,B,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(Settings->SweepRadius),Q);
    for(const auto& H:Hits)
    {
        AActor* Target=H.GetActor();if(!Target||HitActors.Contains(Target))continue;
        FHitResult Obstruction;FCollisionQueryParams Sight(SCENE_QUERY_STAT(EncoreOcclusion),false,P);Sight.AddIgnoredActor(this);
        const FVector Eye=HeliosWeaponHolderContext::ViewLocation(P);
        if(FVector::DistSquared(Eye,H.ImpactPoint)>FMath::Square(Settings->Range))continue;
        if(GetWorld()->LineTraceSingleByChannel(Obstruction,Eye,H.ImpactPoint,ECC_Visibility,Sight)&&Obstruction.GetActor()!=Target)continue;
        HitActors.Add(Target);Impact(H);
        if(Target->CanBeDamaged())UGameplayStatics::ApplyPointDamage(Target,Damage,(B-A).GetSafeNormal(),H,P->GetController(),this,UDamageType::StaticClass());
    }
}
void AHeliosLightstickWeapon::EvaluateAttack(float Old,float Now)
{
    auto* P=Cast<APawn>(GetOwner());auto* Pres=P?HeliosWeaponHolderContext::Presentation(P):nullptr;if(!Pres)return;
    UAnimSequence* Anim=State==3?HeavyPose.Get():(Swings.IsValidIndex(Combo)?Swings[Combo].Get():nullptr);
    const float WindowStart=State==3?.14f:AttackDuration*.22f,WindowEnd=State==3?.30f:AttackDuration*.60f;
    const float Damage=Settings->ComboDamage.IsValidIndex(Combo)?Settings->ComboDamage[Combo]:105.f;
    const float Reach=Settings->Range;
    if(State==3&&Now>=.16f)ReleaseWave();
    const int32 Steps=FMath::Clamp(FMath::CeilToInt((Now-Old)*120),1,32);
    for(int32 I=0;I<=Steps;++I)
    {
        const float T=FMath::Lerp(Old,Now,float(I)/Steps);
        const float Phase=State==3?(T<=.3f?T/.3f*.35f:.35f+(T-.3f)/FMath::Max(.01f,Settings->HeavyRecoverySeconds)*.65f):T/AttackDuration;
        Pres->SetMeleePose(Anim,Phase);
        const FVector Head=Pres->GetMeleeTip();
        if(T>=WindowStart&&T<=WindowEnd)
        {
            const FVector Eye=HeliosWeaponHolderContext::ViewLocation(P);
            const FVector Direction=(Head-Eye).GetSafeNormal();
            const FVector End=Eye+Direction*Reach;
            if(State==2)TraceBlade(Eye+Direction*30,End,Damage);
            if(bHaveHead)
            {
                const FVector Prev=Eye+(LastHead-Eye).GetSafeNormal()*Reach;if(State==2)TraceBlade(Prev,End,Damage);
                if(FVector::DistSquared(Head,LastHead)>.2f)
                {
                    EmitSegment(LastHead,Head,.75f,State==3&&ReleasedCharge>=.98f?Gold:FLinearColor(.9f,1.5f,2.2f),.11f);
                    EmitSegment(LastHead-FVector(0,0,2),Head-FVector(0,0,2),2.1f,Violet,.15f);
                    EmitSegment(LastHead,Head,3.4f,Cyan*.4f,.10f);
                }
            }
        }
        LastHead=Head;bHaveHead=true;
    }
}
void AHeliosLightstickWeapon::EmitSegment(const FVector& A,const FVector& B,float Width,const FLinearColor& C,float Life)
{
    if(Streaks.IsEmpty())return;int32 I=NextStreak++%Streaks.Num();auto* S=Streaks[I].Get();
    const auto* P=Cast<APawn>(GetOwner());const FVector Eye=P?HeliosWeaponHolderContext::ViewLocation(P):A+FVector(0,0,100);
    const FVector Across=FVector::CrossProduct(B-A,Eye-(A+B)*.5f).GetSafeNormal();
    S->SetWorldLocationAndRotation((A+B)*.5f,FRotationMatrix::MakeFromZX(B-A,Across).Rotator());
    // Overlapping soft ends with a stable view-facing basis eliminate cap gaps and twisting.
    S->SetWorldScale3D(FVector(Width/50,Width/50,(FVector::Dist(A,B)*1.5f+.25f)/100));
    StreakMaterials[I]->SetVectorParameterValue(TEXT("GlowColor"),C);StreakMaterials[I]->SetScalarParameterValue(TEXT("Opacity"),.65f);
    S->SetVisibility(true);StreakAges[I]=0;StreakLives[I]=Life;
}
void AHeliosLightstickWeapon::Impact(const FHitResult& H)
{
    Sound(TEXT("SFX_LightstickImpact"),.55f);HitAge=0;HitLight->SetWorldLocation(H.ImpactPoint+H.ImpactNormal*12);HitLight->SetLightColor(State==3?Gold:Cyan);
    const FVector Cut=FVector::CrossProduct(FVector(H.ImpactNormal),FVector::UpVector);
    PlaySlashImpact(H,Cut,Cast<APawn>(H.GetActor())==nullptr,State==3?ReleasedCharge:0);
    if(auto* P=Cast<APawn>(GetOwner()))
    {
        if(auto* Pres=HeliosWeaponHolderContext::Presentation(P))Pres->ApplyMeleeImpact(Combo==2?2.2f:1.4f);
        if(auto* PC=Cast<APlayerController>(P->GetController()))if(PC->PlayerCameraManager)PC->PlayerCameraManager->StartCameraShake(UHeliosBossImpactShake::StaticClass(),Combo==2?2.2f:1.4f);
    }
}
void AHeliosLightstickWeapon::UpdateArt(float Dt)
{
    UpdateSlashEffects(Dt);
    auto* Mesh=GetFirstPersonMesh();
    if(LampMaterials.IsEmpty()&&HasExternalPresentation())
    {
        for(int32 I=0;I<Mesh->GetNumMaterials();++I)
        {
            const FString N=Mesh->GetMaterial(I)?Mesh->GetMaterial(I)->GetName():TEXT("");
            if(N.Contains(TEXT("Segment"))||N.Contains(TEXT("Core")))LampMaterials.Add(Mesh->CreateDynamicMaterialInstance(I));
        }
    }
    const float Charge=GetCharge();
    for(int32 I=0;I<LampMaterials.Num();++I)
    {
        auto* M=LampMaterials[I].Get();if(!M)continue;
        M->SetVectorParameterValue(TEXT("GlowColor"),Charge>=.98f?Gold:Cyan);
        M->SetScalarParameterValue(TEXT("Energy"),State==1?(Charge*6>=I?2.1f:.20f):.8f+.12f*FMath::Sin(Clock*3+I));
    }
    Lamp->SetLightColor(Charge>=.98f?Gold:Cyan);Lamp->SetIntensity(State==1?60+Charge*160:0);
    HitAge+=Dt;HitLight->SetIntensity(HitAge<.12f?480*(1-HitAge/.12f):0);
    PulseAge+=Dt;if(PulseAge<.5f){Pulse->SetWorldScale3D(FVector(.3f+PulseAge*4));if(PulseMaterial){PulseMaterial->SetVectorParameterValue(TEXT("GlowColor"),Gold);PulseMaterial->SetScalarParameterValue(TEXT("Opacity"),.45f*(1-PulseAge/.5f));}}else Pulse->SetVisibility(false);
    for(int32 I=0;I<Streaks.Num();++I)
    {
        StreakAges[I]+=Dt;float A=StreakAges[I]/StreakLives[I];Streaks[I]->SetVisibility(A<1);
        if(A<1)StreakMaterials[I]->SetScalarParameterValue(TEXT("Opacity"),.6f*(1-A)*(1-A));
    }
    if(State==1&&Charge>.05f)
    {
        FVector H=Mesh->GetSocketLocation(TEXT("Muzzle"));FVector C=Mesh->GetComponentLocation();
        for(int32 I=0;I<3;++I)
        {
            float A=Clock*14+I*2.094f;FVector Side=FVector(0,FMath::Cos(A),FMath::Sin(A))*6;
            FVector Mid=FMath::Lerp(C,H,.55f)+Side;
            EmitSegment(C+Side,Mid,.15f,Violet,.06f);EmitSegment(Mid,H,.20f,Charge>=.98f?Gold:Cyan,.06f);
        }
    }
}
void AHeliosLightstickWeapon::Tick(float Dt)
{
    Super::Tick(Dt);auto* P=Cast<APawn>(GetOwner());if(!P||HeliosWeaponHolderContext::IsDead(P))return;
    if(bSuspended){CancelAttack();bSuspended=false;}
    Clock+=Dt;UpdateArt(Dt);
    if(State==1){Age+=Dt;if(auto* Pres=HeliosWeaponHolderContext::Presentation(P))Pres->SetMeleePose(ChargePose,GetCharge());return;}
    float Remaining=Dt;
    // Carry the unused frame time into the next swing: cadence is independent of frame rate.
    while(IsAttacking()&&Remaining>SMALL_NUMBER)
    {
        const float Step=FMath::Min(Remaining,FMath::Max(.0001f,AttackDuration-Age));
        const float Old=Age;Age=FMath::Min(Age+Step,AttackDuration);Remaining-=Step;EvaluateAttack(Old,Age);
        if(Age>=AttackDuration-SMALL_NUMBER)
        {
            const bool WasHeavy=State==3;State=0;IdleAge=0;Combo=WasHeavy?0:(Combo+1)%3;
            if(auto* Pres=HeliosWeaponHolderContext::Presentation(P))Pres->SetMeleePose(nullptr,0);
            if(bHeld)BeginSwing(false);
        }
    }
    if(State==0)IdleAge+=Remaining;
}
void AHeliosLightstickWeapon::CancelAttack()
{
    State=0;Age=0;IdleAge=10;Combo=0;bHeld=false;bHaveHead=false;HitActors.Reset();
    if(ChargeAudio)ChargeAudio->Stop();ChargeAudio=nullptr;
    if(auto* P=Cast<APawn>(GetOwner()))if(auto* Pres=HeliosWeaponHolderContext::Presentation(P))Pres->SetMeleePose(nullptr,0);
    for(int32 I=0;I<Streaks.Num();++I){StreakAges[I]=10;Streaks[I]->SetVisibility(false);}
    PulseAge=HitAge=10;Pulse->SetVisibility(false);Lamp->SetIntensity(0);HitLight->SetIntensity(0);
}
void AHeliosLightstickWeapon::DeactivateWeapon(){CancelAttack();ClearWaves();ClearSlashEffects();Super::DeactivateWeapon();}
void AHeliosLightstickWeapon::EndPlay(EEndPlayReason::Type R){CancelAttack();ClearWaves();ClearSlashEffects();Super::EndPlay(R);}
