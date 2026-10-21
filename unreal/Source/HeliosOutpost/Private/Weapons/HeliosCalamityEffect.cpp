#include "Weapons/HeliosCalamityStaff.h"
#include "Weapons/HeliosCalamityWind.h"
#include "Weapons/HeliosWeaponVFX.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Boss/HeliosEnemyBoss.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"

namespace {
template<class T>T* Art(const TCHAR* N){return LoadObject<T>(nullptr,*(FString(TEXT("/Game/Helios/Weapons/Calamity/"))+N));}
FLinearColor Color(EHeliosCalamitySpell S){const FLinearColor C[]={FLinearColor(1,.46f,.06f),FLinearColor(1,.075f,.008f),FLinearColor(.05f,.65f,1),FLinearColor(.22f,1,.025f)};return C[static_cast<int32>(S)];}
UStaticMeshComponent* Part(AHeliosCalamityEffect* E,const TCHAR* Mesh,const TCHAR* Mat=nullptr)
{
    auto* M=NewObject<UStaticMeshComponent>(E);M->SetupAttachment(E->GetRootComponent());M->SetStaticMesh(Art<UStaticMesh>(Mesh));if(Mat)M->SetMaterial(0,Art<UMaterialInterface>(Mat));
    M->SetCollisionEnabled(ECollisionEnabled::NoCollision);M->SetCastShadow(false);M->SetCanEverAffectNavigation(false);M->SetCullDistance(10000);M->RegisterComponent();E->Parts.Add(M);
    for(int32 I=0;I<M->GetNumMaterials();++I)if(auto* D=M->CreateDynamicMaterialInstance(I)){D->SetVectorParameterValue(TEXT("SpellColor"),Color(E->Spell));E->Materials.Add(D);}return M;
}
void Burst(AHeliosCalamityEffect* E,const TCHAR* N,float Scale,FVector Offset=FVector::ZeroVector)
{
    if(FVector::DistSquared(E->GetActorLocation(),E->GetOwner()->GetActorLocation())>FMath::Square(10000.f))return;
    if(auto* C=UHeliosWeaponVFX::SpawnOneShot(E,Art<UNiagaraSystem>(N),E->GetActorLocation()+Offset,FRotator::ZeroRotator,FVector(Scale*E->Radius/(E->Spell==EHeliosCalamitySpell::Earthquake?800.f:600.f),Scale*E->Radius/(E->Spell==EHeliosCalamitySpell::Earthquake?800.f:600.f),Scale),2.5f)){C->ComponentTags.AddUnique(TEXT("Calamity"));C->ComponentTags.AddUnique(FName(*E->GetOwner()->GetName()));}
}
}
AHeliosCalamityEffect::AHeliosCalamityEffect()
{
    PrimaryActorTick.bCanEverTick=true;RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Light=CreateDefaultSubobject<UPointLightComponent>(TEXT("SpellLight"));Light->SetupAttachment(RootComponent);Light->SetCastShadows(false);Light->SetIntensity(0);
}
void AHeliosCalamityEffect::Initialize(UHeliosCalamityState* S,EHeliosCalamitySpell Type,const FVector& Target,float Power)
{
    State=S;Spell=Type;Scale=Power;const auto& C=S->Settings->Spells[static_cast<int32>(Type)];Radius=S->GetDamageRadius();Duration=C.Duration;Telegraph=C.TelegraphSeconds;Tail=FMath::Max(.05f,C.DissipateSeconds);
    FlightSpeed=FMath::Clamp(C.FlightSpeedMultiplier,.1f,4.f);
    Light->SetLightColor(Color(Spell));Light->SetAttenuationRadius(900);
    if(Spell==EHeliosCalamitySpell::Earthquake||Spell==EHeliosCalamitySpell::Tornado)
    {bField=true;Phase=EHeliosCalamityPhase::Telegraph;BuildField();State->Sound(C.PreludeSound.LoadSynchronous(),RootComponent,FVector::ZeroVector,.8f);if(Telegraph<=0)ActivateField();}
    else
    {
        Part(this,Spell==EHeliosCalamitySpell::Poison?TEXT("SM_CS_Bottle"):TEXT("SM_CS_Core"));
        if(Spell==EHeliosCalamitySpell::Fireball){Parts[0]->SetRelativeScale3D(FVector(1.5f));Velocity=(Target-GetActorLocation()).GetSafeNormal()*2600;Light->SetIntensity(5000);StartLoop();}
        else{const float T=FMath::Clamp(FVector::Dist(Target,GetActorLocation())/1700.f,.3f,2.3f);Velocity=(Target-GetActorLocation())/T+FVector(0,0,490*T);Light->SetIntensity(350);}
        BuildFlightVisuals();
    }
}
void AHeliosCalamityEffect::StartLoop()
{
    if(LoopAudio)LoopAudio->Stop();LoopAudio=State->Sound(State->Settings->Spells[static_cast<int32>(Spell)].LoopSound.LoadSynchronous(),RootComponent,FVector::ZeroVector,0);LoopGain=0;
}
void AHeliosCalamityEffect::SetLoopAudible(bool Audible,float Dt)
{
    if(!IsValid(LoopAudio))return;
    const float Fade=Phase==EHeliosCalamityPhase::Dissipating?FMath::Clamp((Telegraph+Duration+Tail-FieldAge)/Tail,0.f,1.f):1;
    LoopGain=FMath::FInterpTo(LoopGain,Audible?.55f*Fade:0.f,Dt,8.f);LoopAudio->SetVolumeMultiplier(LoopGain*UHeliosCalamityState::SpellSoundGain);
}
void AHeliosCalamityEffect::EndPlay(EEndPlayReason::Type Reason){for(auto C:ImpactParticles)if(IsValid(C))UHeliosWeaponVFX::ReleaseTracked(C);ImpactParticles.Reset();if(LoopAudio)LoopAudio->Stop();if(IsValid(FlightParticles))UHeliosWeaponVFX::ReleaseTracked(FlightParticles);Super::EndPlay(Reason);}
void AHeliosCalamityEffect::BuildField()
{
    for(auto P:Parts)P->DestroyComponent();Parts.Reset();Materials.Reset();
    if(Spell==EHeliosCalamitySpell::Tornado&&State->Settings->WindPresentation&&State->Settings->WindPresentation->IsReady())
    {
        WindVisual=NewObject<UHeliosCalamityWindVisual>(this);WindVisual->SetupAttachment(RootComponent);WindVisual->RegisterComponent();
        WindVisual->Initialize(this,State->Settings->WindPresentation);return;
    }
    if(State->Settings->ImpactPresentation&&(Spell==EHeliosCalamitySpell::Fireball||Spell==EHeliosCalamitySpell::Poison)){BuildImpactPresentation();return;}
    if(Spell==EHeliosCalamitySpell::Fireball)
    {
        Part(this,TEXT("SM_CS_Core"),TEXT("M_CS_Rune"))->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));
        for(int32 I=0;I<3;++I)Part(this,TEXT("SM_CS_Ring"),TEXT("M_CS_Rune"))->SetRelativeRotation(FRotator(I*60,0,I*45));return;
    }
    Part(this,TEXT("SM_CS_Seal"),TEXT("M_CS_Rune"))->SetRelativeScale3D(FVector(Radius/100));
    if(Spell==EHeliosCalamitySpell::Earthquake)
    {
        Part(this,TEXT("SM_CS_Cracks"),TEXT("M_CS_Cracks"))->SetRelativeScale3D(FVector(Radius/94.5f,Radius/94.5f,1));
        for(int32 I=0;I<3;++I)Part(this,TEXT("SM_CS_Ring"),TEXT("M_CS_Rune"));
        for(int32 I=0;I<3;++I)Part(this,TEXT("SM_CS_DustRing"),TEXT("M_CS_Dust"));
    }
    else if(Spell==EHeliosCalamitySpell::Tornado)
    {
        Part(this,TEXT("SM_CS_Funnel"),TEXT("M_CS_Funnel"));Part(this,TEXT("SM_CS_Funnel"),TEXT("M_CS_Funnel"));
        Part(this,TEXT("SM_CS_Vortex"),TEXT("M_CS_Vortex"));Part(this,TEXT("SM_CS_DustRing"),TEXT("M_CS_Dust"));
    }
    else {
        Part(this,TEXT("SM_CS_Disc"),TEXT("M_CS_Poison"))->SetRelativeScale3D(FVector(Radius/100));
        for(int32 I=0;I<3;++I)Part(this,TEXT("SM_CS_Ring"),TEXT("M_CS_FlightGlow"));
        Debris=NewObject<UInstancedStaticMeshComponent>(this);Debris->SetupAttachment(RootComponent);Debris->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));Debris->SetMaterial(0,Art<UMaterialInterface>(TEXT("M_CS_PoisonBubble")));Debris->SetCollisionEnabled(ECollisionEnabled::NoCollision);Debris->SetCastShadow(false);Debris->SetCullDistance(6500);Debris->RegisterComponent();
        for(int32 I=0;I<24;++I)Debris->AddInstance(FTransform(FVector(0,0,-100)));
    }
    if(Spell==EHeliosCalamitySpell::Earthquake||Spell==EHeliosCalamitySpell::Tornado)
    {
        Debris=NewObject<UInstancedStaticMeshComponent>(this);Debris->SetupAttachment(RootComponent);Debris->SetStaticMesh(Art<UStaticMesh>(TEXT("SM_CS_Rock")));Debris->SetCollisionEnabled(ECollisionEnabled::NoCollision);Debris->SetCastShadow(false);Debris->SetCanEverAffectNavigation(false);Debris->SetCullDistance(9000);Debris->RegisterComponent();
        for(int32 I=0;I<32;++I)Debris->AddInstance(FTransform(FQuat::Identity,FVector(0,0,-100),FVector(.01f)));
    }
    for(auto M:Materials){M->SetVectorParameterValue(TEXT("EffectCenter"),GetActorLocation());M->SetScalarParameterValue(TEXT("EffectRadius"),Radius);M->SetScalarParameterValue(TEXT("Reveal"),0);M->SetScalarParameterValue(TEXT("Opacity"),0);}
}
void AHeliosCalamityEffect::ActivateField()
{
    if(State->Settings->ImpactPresentation&&(Spell==EHeliosCalamitySpell::Fireball||Spell==EHeliosCalamitySpell::Poison)){PlayImpactLayers();return;}
    Phase=EHeliosCalamityPhase::Active;const auto& C=State->Settings->Spells[static_cast<int32>(Spell)];
    if(Spell!=EHeliosCalamitySpell::Earthquake)State->Sound(C.ImpactSound.LoadSynchronous(),RootComponent,FVector::ZeroVector,Spell==EHeliosCalamitySpell::Fireball?.95f:.8f);
    if(Spell==EHeliosCalamitySpell::Fireball){Burst(this,TEXT("NS_CS_Fire"),2.6f);Burst(this,TEXT("NS_CS_FireEmbers"),1.5f);Burst(this,TEXT("NS_CS_FireSmoke"),1.5f);State->DamageArea(GetActorLocation(),Spell,Scale);if(LoopAudio)LoopAudio->Stop();LoopAudio=nullptr;}
    else {StartLoop();if(WindVisual)WindVisual->PlayActivation();if(Spell==EHeliosCalamitySpell::Poison){Burst(this,TEXT("NS_CS_PoisonSplash"),1.6f);Burst(this,TEXT("NS_CS_PoisonGlass"),1);Burst(this,TEXT("NS_CS_Poison"),2);}}
}
void AHeliosCalamityEffect::Impact(const FVector& P,const FVector& Normal,const FVector& Incoming,bool Ground)
{
    if(IsValid(FlightParticles)){UHeliosWeaponVFX::ReleaseTracked(FlightParticles);FlightParticles=nullptr;}
    bField=true;FieldAge=0;Telegraph=0;ImpactNormal=Normal.GetSafeNormal(UE_SMALL_NUMBER,FVector::UpVector);bImpactGround=Ground;
    SetActorLocation(P+ImpactNormal*3);FVector Along=FVector::VectorPlaneProject(Incoming,ImpactNormal).GetSafeNormal();if(Along.IsNearlyZero())Along=FVector::VectorPlaneProject(FVector::ForwardVector,ImpactNormal).GetSafeNormal();if(Along.IsNearlyZero())Along=FVector::RightVector;
    SetActorRotation(FRotationMatrix::MakeFromZX(ImpactNormal,Along).Rotator());BuildField();ActivateField();
}
void AHeliosCalamityEffect::Tick(float Dt)
{
    Super::Tick(Dt);if(!IsValid(State)||!State->Settings){Destroy();return;}Age+=Dt;
    if(!bField)
    {
        // Advance the original trajectory in virtual time. Analytic gravity keeps the
        // arc and landing position invariant at any multiplier and render frame rate.
        const int32 Steps=FMath::Max(1,FMath::CeilToInt(Dt*FlightSpeed*120));
        const float Step=Dt*FlightSpeed/Steps;
        for(int32 I=0;I<Steps;++I)
        {
            const FVector Start=GetActorLocation();const FVector Gravity(0,0,Spell==EHeliosCalamitySpell::Poison?-980:0);
            const FVector End=Start+Velocity*Step+Gravity*(.5f*Step*Step);Velocity+=Gravity*Step;
            FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(CalamityProjectile),false,GetOwner());Q.AddIgnoredActor(this);if(State->Tower)Q.AddIgnoredActor(State->Tower);
            if(GetWorld()->SweepSingleByChannel(Hit,Start,End,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(Spell==EHeliosCalamitySpell::Fireball?16:5),Q))
            {
                FVector Point=Hit.ImpactPoint,Normal=Hit.ImpactNormal;bool Ground=Normal.Z>.35f;
                if(Spell==EHeliosCalamitySpell::Poison&&!Ground){FHitResult Floor;Q.AddIgnoredActor(Hit.GetActor());if(GetWorld()->LineTraceSingleByChannel(Floor,Point+Normal*12,Point+Normal*12-FVector(0,0,600),ECC_Visibility,Q)&&Floor.ImpactNormal.Z>.35f){Point=Floor.ImpactPoint;Normal=Floor.ImpactNormal;Ground=true;}}
                Impact(Point,Normal,Velocity,Ground);return;
            }
            SetActorLocation(End);
        }
        UpdateFlightVisuals(Dt);
        if(Age*FlightSpeed>4||FVector::DistSquared(GetActorLocation(),GetOwner()->GetActorLocation())>FMath::Square(6500.f))Destroy();return;
    }
    if(Phase==EHeliosCalamityPhase::Flight){BuildField();ActivateField();}
    if(FlightTrail){FlightTrail->SetVisibility(FieldAge<.28f);if(auto* M=Cast<UMaterialInstanceDynamic>(FlightTrail->GetMaterial(0)))M->SetScalarParameterValue(TEXT("Opacity"),FMath::Max(0.f,1-FieldAge/.28f));}
    if(State->Settings->ImpactPresentation&&(Spell==EHeliosCalamitySpell::Fireball||Spell==EHeliosCalamitySpell::Poison)){TickImpactPresentation(Dt);return;}
    const float Previous=FieldAge;FieldAge+=Dt;
    const float Formation=Telegraph>0?FMath::Clamp(FieldAge/Telegraph,0.f,1.f):1;
    const float ActiveAge=FMath::Clamp(FieldAge-Telegraph,0.f,Duration);
    const float ActiveStep=ActiveAge-FMath::Clamp(Previous-Telegraph,0.f,Duration);
    if(Phase==EHeliosCalamityPhase::Telegraph&&FieldAge>=Telegraph)ActivateField();
    if(FieldAge>=Telegraph+Duration)
    {Phase=EHeliosCalamityPhase::Dissipating;if(!bTailPlayed){bTailPlayed=true;State->Sound(State->Settings->Spells[static_cast<int32>(Spell)].TailSound.LoadSynchronous(),RootComponent,FVector::ZeroVector,.7f);}}
    const float Fade=FMath::Clamp((Telegraph+Duration+Tail-FieldAge)/Tail,0.f,1.f);
    for(auto M:Materials){M->SetScalarParameterValue(TEXT("Opacity"),Fade*(.18f+.82f*Formation));M->SetScalarParameterValue(TEXT("Reveal"),Formation);M->SetScalarParameterValue(TEXT("EffectAge"),FieldAge);}
    Light->SetIntensity(Fade*(Spell==EHeliosCalamitySpell::Fireball?15000*FMath::Exp(-FieldAge*7):Spell==EHeliosCalamitySpell::Tornado?250:950)*Formation);
    if(Spell!=EHeliosCalamitySpell::Fireball&&!WindVisual)if(auto* Seal=Cast<UMaterialInstanceDynamic>(Parts[0]->GetMaterial(0)))Seal->SetScalarParameterValue(TEXT("Opacity"),Fade*Formation*.13f);
    if(Spell==EHeliosCalamitySpell::Earthquake)
    {
        while(FieldAge>=Telegraph&&Pulses<3&&ActiveAge>=.5f*Pulses)
        {State->DamageArea(GetActorLocation(),Spell,Scale);++Pulses;Burst(this,TEXT("NS_CS_HeavyDust"),2.3f);State->Sound(State->Settings->Spells[0].ImpactSound.LoadSynchronous(),RootComponent,FVector::ZeroVector,.85f);State->Shake(GetActorLocation(),.9f);}
        for(int32 I=0;I<3;++I){const float T=FieldAge-Telegraph-I*.5f;const bool Visible=T>=0&&T<1.1f;Parts[2+I]->SetVisibility(Visible);Parts[5+I]->SetVisibility(Visible);Parts[2+I]->SetRelativeScale3D(FVector(FMath::Max(.01f,FMath::Clamp(T/.8f,0.f,1.f)*Radius/100)));Parts[5+I]->SetRelativeScale3D(FVector(FMath::Max(.01f,FMath::Clamp(T/1.1f,0.f,1.f)*Radius/117),FMath::Max(.01f,FMath::Clamp(T/1.1f,0.f,1.f)*Radius/117),.5f+FMath::Max(0.f,T)*1.5f));}
        for(int32 I=0;I<32;++I){const float A=I*2.39996f,R=(120+(I%6)*103)*Radius/800.f,T=FMath::Max(0.f,FieldAge-Telegraph-.04f*(I%4));const float Lift=FMath::Sin(FMath::Clamp(T/1.9f,0.f,1.f)*PI)*(70+(I%5)*35);const float Tremor=Phase==EHeliosCalamityPhase::Telegraph?FMath::Sin(FieldAge*60+I)*Formation*2:0;const FVector P(FMath::Cos(A)*R,FMath::Sin(A)*R,-24+Lift+Tremor);Debris->UpdateInstanceTransform(I,FTransform(FRotator(I*17+T*16,I*31,I*13),P,FVector((.24f+(I%5)*.18f)*Fade)),false,I==31);}
        if(Phase==EHeliosCalamityPhase::Telegraph&&FieldAge>=FXAt){FXAt=FieldAge+.22f;Burst(this,TEXT("NS_CS_Earth"),.65f);}
    }
    else if(Spell==EHeliosCalamitySpell::Tornado)
    {
        while(PulseAt+.2f<=ActiveAge+.0001f){PulseAt+=.2f;State->DamageArea(GetActorLocation(),Spell,Scale,.2f);}
        const float Growth=FMath::SmoothStep(0.f,1.f,Formation)*Fade;
        if(WindVisual)WindVisual->Advance(Formation,Fade,ActiveAge);
        else {
        for(int32 I=1;I<=3;++I){const float S=(I==2?.77f:I==3?.88f:1.f)*FMath::Max(.025f,Growth);Parts[I]->SetRelativeScale3D(FVector(S*Radius/600.f,S*Radius/600.f,FMath::Max(.015f,Growth)));Parts[I]->SetRelativeRotation(FRotator(0,FieldAge*(I==2?-105:160)+I*120,0));}
        Parts[4]->SetRelativeScale3D(FVector((.27f+.73f*Growth)*Fade*Radius/117.f,(.27f+.73f*Growth)*Fade*Radius/117.f,.6f));Parts[4]->SetRelativeRotation(FRotator(0,-FieldAge*140,0));
        for(int32 I=0;I<32;++I){const float A=FieldAge*(3+Growth*2)+I*2.4f,R=(70+(I%6)*43)*(1.2f-Growth*.25f)*Radius/600.f;const float Z=(25+FMath::Fmod(FieldAge*120+I*43,680))*Growth;Debris->UpdateInstanceTransform(I,FTransform(FRotator(A*31,A*27,A*41),FVector(FMath::Cos(A)*R,FMath::Sin(A)*R,Z),FVector((.08f+(I%4)*.085f)*Fade)),false,I==31);}
        }
        if(ActiveStep>0)
        {
            State->Shake(GetActorLocation(),WindVisual?.035f:.07f);
            for(TActorIterator<AHeliosEnemyActor> E(GetWorld());E;++E)if(!E->IsEnemyDead()&&!Cast<AHeliosEnemyBoss>(*E)&&!E->ActorHasTag(TEXT("Friendly"))&&FVector::DistSquared(E->GetActorLocation(),GetActorLocation())<FMath::Square(State->Settings->Spells[static_cast<int32>(Spell)].Radius)&&State->CanHit(GetActorLocation()+FVector(0,0,45),*E))
            {FVector D=GetActorLocation()-E->GetActorLocation();D.Z=0;E->AddActorWorldOffset(D.GetSafeNormal()*FMath::Min(280*ActiveStep,FMath::Max(0.f,D.Size()-90)),true);}
        }
        if(!WindVisual&&FieldAge>=FXAt){FXAt=FieldAge+.35f;Burst(this,TEXT("NS_CS_Wind"),1.5f,FVector(0,0,100+250*Growth));}
    }
    else if(Spell==EHeliosCalamitySpell::Fireball)
    {
        Parts[0]->SetRelativeScale3D(FVector((1+FMath::Min(FieldAge/.35f,1.f)*4)*Radius/600.f,(1+FMath::Min(FieldAge/.35f,1.f)*4)*Radius/600.f,1+FMath::Min(FieldAge/.35f,1.f)*4));Parts[0]->SetVisibility(FieldAge<.35f);
        for(int32 I=1;I<Parts.Num();++I){Parts[I]->SetRelativeScale3D(FVector(FMath::Max(.01f,FMath::Min(FieldAge/.55f,1.f)*Radius/100)));Parts[I]->SetVisibility(FieldAge<.65f);}
        for(auto M:Materials)M->SetScalarParameterValue(TEXT("Opacity"),FMath::Clamp(1-FieldAge/.65f,0.f,1.f)*.45f);
    }
    else {
        for(int32 I=2;I<5;++I){float T=I==2?1.f:FMath::Fmod(FieldAge*.55f+(I-2)/3.f,1.f);Parts[I]->SetRelativeScale3D(FVector(FMath::Max(.01f,T*Radius/100)));Parts[I]->SetRelativeLocation(FVector(0,0,5+I));if(auto* M=Cast<UMaterialInstanceDynamic>(Parts[I]->GetMaterial(0)))M->SetScalarParameterValue(TEXT("Opacity"),(I==2?.65f:(1-T)*.28f)*Fade);}
        for(int32 I=0;I<24;++I){float T=FMath::Fmod(FieldAge*.65f+I*.137f,1.f),A=I*2.39996f,R=Radius*(.12f+.78f*((I%7)/7.f)),Size=(.035f+.025f*(I%4))*FMath::Sin(T*PI)*Fade;Debris->UpdateInstanceTransform(I,FTransform(FQuat::Identity,FVector(FMath::Cos(A)*R,FMath::Sin(A)*R,4+T*35),FVector(Size)),false,I==23);}
        if(FieldAge>=FXAt){FXAt=FieldAge+.45f;Burst(this,TEXT("NS_CS_Poison"),1.4f,FVector(0,0,18));}
    }
    if(FieldAge>=Telegraph+Duration+Tail)Destroy();
}

void AHeliosCalamityEffect::BuildFlightVisuals()
{
    const bool Fire=Spell==EHeliosCalamitySpell::Fireball;
    if(Fire){Parts[0]->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));Parts[0]->SetMaterial(0,Art<UMaterialInterface>(TEXT("M_CS_FireCore")));Parts[0]->SetRelativeScale3D(FVector(.32f));}
    else for(int32 I=0;I<Parts[0]->GetNumMaterials();++I)if(auto* M=Cast<UMaterialInstanceDynamic>(Parts[0]->GetMaterial(I)))M->SetScalarParameterValue(TEXT("Charge"),.7f);
    for(int32 I=0;I<2;++I){auto* Ring=Part(this,TEXT("SM_CS_Ring"),TEXT("M_CS_FlightGlow"));Ring->SetRelativeScale3D(FVector(Fire?.23f:.12f));}
    FlightTrail=NewObject<UInstancedStaticMeshComponent>(this);FlightTrail->SetupAttachment(RootComponent);FlightTrail->SetAbsolute(true,true,true);FlightTrail->SetWorldTransform(FTransform::Identity);
    FlightTrail->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));FlightTrail->SetMaterial(0,Art<UMaterialInterface>(TEXT("M_CS_FlightGlow")));FlightTrail->SetCollisionEnabled(ECollisionEnabled::NoCollision);FlightTrail->SetCastShadow(false);FlightTrail->SetCanEverAffectNavigation(false);FlightTrail->SetCullDistance(7000);FlightTrail->RegisterComponent();
    if(auto* M=FlightTrail->CreateDynamicMaterialInstance(0))M->SetVectorParameterValue(TEXT("SpellColor"),Color(Spell));
    for(int32 I=0;I<72;++I)FlightTrail->AddInstance(FTransform(FQuat::Identity,GetActorLocation(),FVector(0)));
    TrailPoints.Add(GetActorLocation());
    FlightParticles=UHeliosWeaponVFX::SpawnAttachedTrail(Art<UNiagaraSystem>(Fire?TEXT("NS_CS_FireStream"):TEXT("NS_CS_PoisonStream")),RootComponent,NAME_None,FVector::ZeroVector,FRotator::ZeroRotator);
    if(FlightParticles){FlightParticles->ComponentTags.AddUnique(TEXT("Calamity"));FlightParticles->ComponentTags.AddUnique(FName(*GetOwner()->GetName()));}
}
void AHeliosCalamityEffect::UpdateFlightVisuals(float Dt)
{
    const bool Fire=Spell==EHeliosCalamitySpell::Fireball;
    SetActorRotation(Velocity.Rotation());Parts[0]->AddLocalRotation(FRotator(Dt*160,Dt*90,0));
    for(int32 I=1;I<3;++I)Parts[I]->SetRelativeRotation(FRotator(65+I*40,Age*420*(I==1?1:-1),Age*180));
    TrailPoints.Insert(GetActorLocation(),0);if(TrailPoints.Num()>25)TrailPoints.SetNum(25);
    FVector Side,Up;Velocity.GetSafeNormal().FindBestAxisVectors(Side,Up);
    for(int32 I=0;I<72;++I)
    {
        int32 Segment=I/3,Strand=I%3;FTransform Transform(FQuat::Identity,GetActorLocation(),FVector(0));
        if(Segment+1<TrailPoints.Num()){
            const float Fade=1-Segment/24.f,TrailWidth=(Fire?6.f:3.f)*Fade;
            auto Point=[&](int32 J){float A=Age*20+J*.55f+Strand*PI;return TrailPoints[J]+(Strand==0?FVector::ZeroVector:(Side*FMath::Cos(A)+Up*FMath::Sin(A))*(Fire?20.f:11.f)*(1-J/25.f));};
            const FVector A=Point(Segment),B=Point(Segment+1),Delta=B-A;
            Transform=FTransform(FQuat::FindBetweenNormals(FVector::UpVector,Delta.GetSafeNormal()),(A+B)*.5f,FVector(TrailWidth/50*(Strand==0?1:.23f),TrailWidth/50*(Strand==0?1:.23f),(Delta.Size()+TrailWidth)/100));
        }
        FlightTrail->UpdateInstanceTransform(I,Transform,true,I==71);
    }
}
