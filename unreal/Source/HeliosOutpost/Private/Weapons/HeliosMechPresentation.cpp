#include "Weapons/HeliosMechWeapon.h"
#include "Player/HeliosArenaCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Camera/CameraComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace MTArt
{
template<class T>T* Load(const TCHAR* Name){return LoadObject<T>(nullptr,*(FString(TEXT("/Game/Helios/Weapons/Mech/"))+Name));}
float Ease(float T){T=FMath::Clamp(T,0.f,1.f);return T*T*(3-2*T);}
FVector Joint(const FVector& Root,FVector& End,const FVector& Pole,float Upper,float Lower)
{
    FVector Direction=(End-Root).GetSafeNormal();float Distance=FMath::Clamp(float(FVector::Distance(Root,End)),FMath::Abs(Upper-Lower)+1,Upper+Lower-1);
    End=Root+Direction*Distance;
    const float Along=(Upper*Upper-Lower*Lower+Distance*Distance)/(2*Distance);
    const FVector Bend=(Pole-Direction*FVector::DotProduct(Pole,Direction)).GetSafeNormal();
    return Root+Direction*Along+Bend*FMath::Sqrt(FMath::Max(0.f,Upper*Upper-Along*Along));
}
}
UStaticMeshComponent* UHeliosMechPresentationComponent::Part(const TCHAR* Mesh,const TCHAR* Material,bool Shadow)
{
    auto* C=NewObject<UStaticMeshComponent>(GetOwner());C->SetupAttachment(GetOwner()->GetRootComponent());C->SetAbsolute(true,true,true);
    C->SetStaticMesh(MTArt::Load<UStaticMesh>(Mesh));C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetGenerateOverlapEvents(false);C->SetCanEverAffectNavigation(false);C->SetCastShadow(Shadow);C->SetMobility(EComponentMobility::Movable);C->RegisterComponent();
    if(Material)C->SetMaterial(0,MTArt::Load<UMaterialInterface>(Material));return C;
}
void UHeliosMechPresentationComponent::Initialize()
{
    if(Armor.Num())return;
    const TCHAR* Names[]={TEXT("Torso"),TEXT("Hips"),TEXT("Head"),TEXT("Shoulder"),TEXT("Shoulder"),TEXT("UpperArm"),TEXT("UpperArm"),TEXT("Forearm"),TEXT("Forearm"),TEXT("Hand"),TEXT("Hand"),TEXT("Thigh"),TEXT("Thigh"),TEXT("Shin"),TEXT("Shin"),TEXT("Foot"),TEXT("Foot")};
    for(auto* N:Names)Armor.Add(Part(*(FString(TEXT("SM_MT_"))+N),nullptr,true));
    Armor.Add(Part(TEXT("SM_MT_ReactorDoor"),nullptr,true));Armor.Add(Part(TEXT("SM_MT_ReactorDoor"),nullptr,true));
    Armor.Add(Part(TEXT("SM_MT_Toe"),nullptr,true));Armor.Add(Part(TEXT("SM_MT_Toe"),nullptr,true));
    Armor.Add(Part(TEXT("SM_MT_Heel"),nullptr,true));Armor.Add(Part(TEXT("SM_MT_Heel"),nullptr,true));
    const int32 ViewIndices[]={11,13,15,19,21,5,7,9};
    for(int32 Side=0;Side<2;++Side)for(int32 Index:ViewIndices)
    {
        auto* Source=Armor[Index+Side].Get();Source->SetOwnerNoSee(true);Source->SetCastHiddenShadow(true);
        auto* C=Part(TEXT("SM_MT_Foot"));C->SetStaticMesh(Source->GetStaticMesh());C->SetOnlyOwnerSee(true);C->SetLightingChannels(true,true,false);C->SetVisibility(Index!=5);ViewArmor.Add(C);
    }
    // Keep the complete torso in the world and its shadows, while the cockpit view
    // uses the articulated arms/legs. Open reactor doors must not cross the lens.
    for(int32 Index:{0,1,2,3,4,17,18}){Armor[Index]->SetOwnerNoSee(true);Armor[Index]->SetCastHiddenShadow(true);}
    for(int32 I=0;I<2;++I)
    {
        auto* L=NewObject<UPointLightComponent>(GetOwner());L->SetupAttachment(GetOwner()->GetRootComponent());L->SetAbsolute(true,true,true);
        L->SetLightingChannels(false,true,false);L->SetCastShadows(false);L->SetAttenuationRadius(3500);L->SetSourceRadius(200);
        L->SetLightColor(I?FLinearColor(.48f,.62f,.8f):FLinearColor(1.f,.75f,.42f));L->RegisterComponent();ViewLights.Add(L);
    }
    Armor[2]->SetOwnerNoSee(true);Armor[2]->SetCastHiddenShadow(true);
    for(int32 I=0;I<6;++I){auto* C=Part(TEXT("SM_MT_Beam"),I==0?TEXT("M_MT_BeamCore"):I==5?TEXT("M_MT_Heat"):TEXT("M_MT_BeamFlame"));Beams.Add(C);BeamMaterials.Add(C->CreateDynamicMaterialInstance(0));C->SetVisibility(false);}
    for(int32 I=0;I<3;++I){auto* C=Part(TEXT("SM_MT_Surface"),TEXT("M_MT_Blast"));Plasma.Add(C);PlasmaMaterials.Add(C->CreateDynamicMaterialInstance(0));C->SetVisibility(false);}
    for(int32 I=0;I<9;++I){auto* C=Part(TEXT("SM_MT_Ring"),TEXT("M_MT_Energy"));Rings.Add(C);C->SetVisibility(false);}
    for(int32 I=0;I<80;++I)
    {
        const bool Dust=I>=56;auto* C=Part(Dust?TEXT("SM_MT_Surface"):TEXT("SM_MT_Spark"),Dust?TEXT("M_MT_Dust"):TEXT("M_MT_Energy"));C->SetVisibility(false);Motes.Add(C);MoteMaterials.Add(C->CreateDynamicMaterialInstance(0));
        MotePositions.Add(FVector::ZeroVector);MoteVelocities.Add(FVector::ZeroVector);MoteAges.Add(100);MoteLives.Add(0);MoteSizes.Add(1);DustFlags.Add(Dust);
    }
    for(int32 I=0;I<12;++I){auto* C=Part(TEXT("SM_MT_Surface"),I%2?TEXT("M_MT_Scorch"):TEXT("M_MT_Shock"));C->SetVisibility(false);Impacts.Add(C);ImpactMaterials.Add(C->CreateDynamicMaterialInstance(0));ImpactAges.Add(100);ImpactRadii.Add(100);}
    for(int32 I=0;I<8;++I){auto* C=Part(TEXT("SM_MT_Surface"),TEXT("M_MT_Shock"));C->SetVisibility(false);StepWaves.Add(C);StepMaterials.Add(C->CreateDynamicMaterialInstance(0));StepAges.Add(1);StepRadii.Add(300);}
    for(int32 I=0;I<2;++I)
    {
        auto* L=NewObject<UPointLightComponent>(GetOwner());L->SetupAttachment(GetOwner()->GetRootComponent());L->SetAbsolute(true,true,true);
        L->SetCastShadows(false);L->SetLightColor(FLinearColor(1,.28f,.035f));L->SetAttenuationRadius(800);L->SetIntensity(0);L->RegisterComponent();StepLights.Add(L);
    }
    ReactorLight=NewObject<UPointLightComponent>(GetOwner());ReactorLight->SetupAttachment(GetOwner()->GetRootComponent());ReactorLight->SetAbsolute(true,true,true);ReactorLight->SetCastShadows(false);ReactorLight->SetLightColor(FLinearColor(1,.23f,.035f));ReactorLight->SetAttenuationRadius(1600);ReactorLight->RegisterComponent();
    HitLight=NewObject<UPointLightComponent>(GetOwner());HitLight->SetupAttachment(GetOwner()->GetRootComponent());HitLight->SetAbsolute(true,true,true);HitLight->SetCastShadows(false);HitLight->SetLightColor(FLinearColor(1,.32f,.065f));HitLight->SetAttenuationRadius(2200);HitLight->RegisterComponent();Clear();
}
void UHeliosMechPresentationComponent::Pose(UStaticMeshComponent* C,const FVector& P,const FRotator& R,const FVector& S)
{C->SetWorldLocationAndRotation(P,R);C->SetWorldScale3D(S);}
void UHeliosMechPresentationComponent::Limb(UStaticMeshComponent* C,const FVector& A,const FVector& B,float Length)
{Pose(C,(A+B)*.5f,FRotationMatrix::MakeFromZX(A-B,FRotator(0,BodyYaw,0).Vector()).Rotator());}
void UHeliosMechPresentationComponent::Clear()
{
    for(auto& C:Beams)C->SetVisibility(false);for(auto& C:Rings)C->SetVisibility(false);for(auto& C:Motes)C->SetVisibility(false);for(auto& C:Impacts)C->SetVisibility(false);
    for(auto& C:Plasma)C->SetVisibility(false);
    for(auto& C:StepWaves)C->SetVisibility(false);for(auto& A:StepAges)A=1;
    for(auto& L:StepLights)L->SetIntensity(0);StepLightAges[0]=StepLightAges[1]=1;
    for(auto& L:ViewLights)L->SetIntensity(0);
    bGroundTracked=false;StepPhase=0;StepShakeAge=1;TravelSinceStep=550;SmoothedSpeed=0;bWasStomping=false;
    SwingProgress[0]=SwingProgress[1]=1;ContactAge[0]=ContactAge[1]=1;
    for(auto& A:MoteAges)A=100;for(auto& A:ImpactAges)A=100;
    if(ReactorLight)ReactorLight->SetIntensity(0);if(HitLight)HitLight->SetIntensity(0);BeamTail=0;bBeamOn=false;
}
void UHeliosMechPresentationComponent::Mote(const FVector& P,const FVector& V,float Life,float Size,bool Dust)
{
    const int32 I=Dust?56+(NextMote++%24):NextMote++%56;MotePositions[I]=P;MoteVelocities[I]=V;MoteAges[I]=0;MoteLives[I]=Life;MoteSizes[I]=Size;Motes[I]->SetVisibility(true);
}
void UHeliosMechPresentationComponent::Footfall(const FVector& Point,const FVector& Normal,bool Sprinting)
{
    // Walking is presentation only; no attack or damage function is called here.
    const int32 Slot=NextStep++%4;const float Radius=Sprinting?400.f:300.f;
    const FQuat Surface=FRotationMatrix::MakeFromZ(Normal).ToQuat();
    for(int32 K=0;K<2;++K)
    {
        const int32 I=Slot*2+K;StepAges[I]=0;StepRadii[I]=Radius;StepWaves[I]->SetVisibility(true);
        Pose(StepWaves[I],Point+Normal*(8+K*3),Surface.Rotator(),FVector(Radius*.35f));
    }
    for(int32 J=0;J<12;++J)
    {
        const float Angle=J*UE_TWO_PI/12;const FVector D=Surface.RotateVector(FVector(FMath::Cos(Angle),FMath::Sin(Angle),0));
        Mote(Point+D*110+Normal*15,D*(Sprinting?500:350)+Normal*120,.24f,5,false);
        if(J%3==0)Mote(Point+D*100+Normal*15,D*90+Normal*65,.75f,Sprinting?105:75,true);
    }
    const int32 Light=Slot%2;StepLights[Light]->SetWorldLocation(Point+Normal*70);StepLightAges[Light]=0;
    StepShakeAge=0;StepShakeStrength=Sprinting?5.5f:3.5f;++FootstepCount;
    if(auto* W=Cast<AHeliosMechWeapon>(GetOwner()))W->PlaySound(TEXT("Step"),Sprinting?.52f:.38f);
}
void UHeliosMechPresentationComponent::Burst(const FVector& At,const FVector& N,float Radius,bool Heavy)
{
    if(!Armor.Num())return;
    const int32 I=(NextImpact++%6)*2;
    for(int32 K=0;K<2;++K){const int32 J=I+K;ImpactAges[J]=0;ImpactRadii[J]=Radius;Impacts[J]->SetVisibility(true);Pose(Impacts[J],At+N*(K?3:9),FRotationMatrix::MakeFromZ(N).Rotator(),FVector(Radius));}
    const int32 Count=Heavy?36:7;
    for(int32 J=0;J<Count;++J)
    {
        const FVector D=(FMath::VRand()+N*.9f).GetSafeNormal();Mote(At+N*12,D*FMath::FRandRange(Heavy?450.f:160.f,Heavy?1600.f:650.f),FMath::FRandRange(.25f,1.0f),FMath::FRandRange(5.f,14.f),false);
    }
    if(Heavy)for(int32 J=0;J<12;++J){const float A=J*UE_TWO_PI/12;const FVector D(FMath::Cos(A),FMath::Sin(A),.18f);Mote(At+D*Radius*.3f,D*Radius*.55f,2.5f,Radius*.23f,true);}
}
void UHeliosMechPresentationComponent::SetBeam(const FVector& Start,const FVector& End,bool Hit,float Charge,bool Firing)
{
    if(!Beams.Num())return;BeamStart=Start;BeamEnd=End;bBeamHit=Hit;
    if(auto* W=Cast<AHeliosMechWeapon>(GetOwner()))ImpactNormal=W->BeamNormal;
    if(bBeamOn&&!Firing)BeamTail=.22f;bBeamOn=Firing;
    const FVector D=(End-Start).GetSafeNormal();const FRotator R=D.Rotation();
    for(int32 I=0;I<Rings.Num();++I)
    {
        const bool Visible=Charge>0&&(I<3||Firing);Rings[I]->SetVisibility(Visible);
        const float Travel=I<3?float(I)*85:320+FMath::Frac(Clock*1.6f+I*.16f)*FVector::Distance(Start,End);
        const float Size=I<3?(.8f+I*.38f)*(.35f+Charge*.65f):1.3f;
        Pose(Rings[I],Start+D*Travel,FRotationMatrix::MakeFromZ(D).Rotator(),FVector(Size));
    }
    if(ReactorLight){ReactorLight->SetWorldLocation(Start);ReactorLight->SetIntensity(Charge*100000);}
    if(HitLight){HitLight->SetWorldLocation(End-D*70);HitLight->SetIntensity(Firing&&Hit?180000:0);}
    if(Charge>0&&!Firing)
        for(int I=0;I<2;++I){const FVector Offset=FMath::VRand()*FMath::FRandRange(120.f,260.f);Mote(Start+Offset,-Offset*3,.25f,4+Charge*7,false);}
}
void UHeliosMechPresentationComponent::Update(float Dt)
{
    auto* W=Cast<AHeliosMechWeapon>(GetOwner());auto* P=W?W->Player():nullptr;if(!P||Armor.Num()<23)return;
    Clock+=Dt;const FVector Ground=P->GetGroundReference();
    if(!bGroundTracked)BodyYaw=P->GetActorRotation().Yaw;
    BodyYaw=FMath::FixedTurn(BodyYaw,P->GetActorRotation().Yaw,Dt*155);
    const FRotator Facing(0,BodyYaw,0);const FQuat Q=Facing.Quaternion();
    auto World=[&](const FVector& V){return Ground+Q.RotateVector(V);};
    const bool Stomp=W->State==EHeliosMechState::Stomp;
    const FVector Displacement=Ground-PreviousGround;const float Distance=Displacement.Size2D();
    const bool Grounded=P->GetCharacterMovement()->IsMovingOnGround()&&!GetWorld()->IsPaused();
    const bool Walking=bGroundTracked&&Dt>SMALL_NUMBER&&Distance<FMath::Max(250.f,P->GetVelocity().Size2D()*Dt*2+50)
        &&Grounded&&!Stomp&&W->DeployAge>=.8f;
    const float Speed=Walking?Distance/Dt:0;
    SmoothedSpeed=FMath::FInterpTo(SmoothedSpeed,Speed,Dt,7);
    const float Moving=FMath::Clamp(SmoothedSpeed/900.f,0.f,1.f);
    FCollisionQueryParams Trace(SCENE_QUERY_STAT(TitanPlantedFeet),false,P);Trace.AddIgnoredActor(W);
    FCollisionObjectQueryParams FloorTypes;FloorTypes.AddObjectTypesToQuery(ECC_WorldStatic);FloorTypes.AddObjectTypesToQuery(ECC_WorldDynamic);
    auto FloorAt=[&](FVector& Point,FVector& Normal)
    {
        FHitResult Hit;
        if(!GetWorld()->LineTraceSingleByObjectType(Hit,Point+FVector(0,0,350),Point-FVector(0,0,600),FloorTypes,Trace)||Hit.ImpactNormal.Z<.7f)return false;
        Point=Hit.ImpactPoint+Hit.ImpactNormal*12;Normal=Hit.ImpactNormal;return true;
    };
    if(!bGroundTracked||Distance>2000)
    {
        for(int32 I=0;I<2;++I){PlantedFeet[I]=World(FVector(60,I?218:-218,0));FootNormals[I]=FVector::UpVector;FloorAt(PlantedFeet[I],FootNormals[I]);Feet[I]=PlantedFeet[I];SwingProgress[I]=1;}
    }
    if(Stomp&&!bWasStomping)for(int32 I=0;I<2;++I){AttackFeet[I]=Feet[I];SwingProgress[I]=1;}
    if(!Stomp&&bWasStomping)for(int32 I=0;I<2;++I){PlantedFeet[I]=Feet[I];FloorAt(PlantedFeet[I],FootNormals[I]);SwingProgress[I]=1;}
    bWasStomping=Stomp;PreviousGround=Ground;bGroundTracked=true;StepShakeAge+=Dt;
    if(Walking&&Speed>30)TravelSinceStep+=Distance;
    const float SwingDuration=FMath::Lerp(.49f,.36f,FMath::Clamp((SmoothedSpeed-900)/500,0.f,1.f));
    if(Walking&&Speed>30&&TravelSinceStep>=600&&SwingProgress[0]>=1&&SwingProgress[1]>=1)
    {
        const int32 I=LeadFoot;FVector Target=World(FVector(30,I?218:-218,0))+Displacement.GetSafeNormal2D()*(Speed*SwingDuration+170);
        FVector Normal;
        if(FloorAt(Target,Normal)){SwingStarts[I]=Feet[I];SwingTargets[I]=Target;FootNormals[I]=Normal;SwingProgress[I]=0;LeadFoot=1-I;TravelSinceStep=0;}
    }
    const float T=Stomp?FMath::Clamp(W->Age/W->Settings->StompSeconds,0.f,1.f):0;
    const float AttackWeight=Stomp?MTArt::Ease(T/.34f)*(1-MTArt::Ease((T-.77f)/.23f)):0;
    const FVector StompOffset=Stomp?(W->StompPoint-Ground).GetClampedToMaxSize2D(400)*AttackWeight:FVector::ZeroVector;
    const float Compression=Stomp?70*FMath::Exp(-FMath::Abs(T-.61f)*22):0;
    const float Bob=Moving*FMath::Sin(StepPhase*2)*10;
    if(Walking)StepPhase+=UE_PI*Distance/600.f;
    const FVector BodyOffset=StompOffset+FVector(0,0,Bob-AttackWeight*180-Compression);
    Pose(Armor[0],World(FVector(0,0,1410))+BodyOffset,Facing+FRotator(-AttackWeight*5,0,Moving*FMath::Sin(StepPhase)*1.5f));
    Pose(Armor[1],World(FVector(-30,0,1020))+BodyOffset,Facing);Pose(Armor[2],World(FVector(20,0,1815))+BodyOffset,Facing);
    Muzzle=World(FVector(285,0,1424))+BodyOffset;
    const float Open=W->State==EHeliosMechState::Charging?MTArt::Ease(W->Age/W->Settings->ChargeSeconds):W->State==EHeliosMechState::Firing?1.f:W->Heat*.3f;
    for(int32 I=0;I<2;++I)Pose(Armor[17+I],World(FVector(259-Open*60,(I?1:-1)*(61+Open*140),1424))+BodyOffset,Facing+FRotator(0,(I?1:-1)*Open*65,0));
    for(int32 I=0;I<2;++I)
    {
        const float Side=I?1.f:-1.f;ContactAge[I]+=Dt;
        FVector Foot=PlantedFeet[I];
        if(!Stomp&&SwingProgress[I]<1)
        {
            SwingProgress[I]=FMath::Min(1.f,SwingProgress[I]+Dt/SwingDuration);const float A=SwingProgress[I];
            Foot=FMath::Lerp(SwingStarts[I],SwingTargets[I],MTArt::Ease(A));Foot.Z+=FMath::Sin(A*UE_PI)*(Speed>1100?195:145);
            if(A>=1){PlantedFeet[I]=SwingTargets[I];ContactAge[I]=0;if(Grounded&&Speed>30)Footfall(Foot-FootNormals[I]*12,FootNormals[I],Speed>1100);}
        }
        if(Stomp)
        {
            Foot=AttackFeet[I];
            if(I==W->SwingSide)
            {
                const FVector Rest=World(FVector(80,Side*218,0));
                Foot=FMath::Lerp(AttackFeet[I],W->StompPoint+FVector(0,0,12),MTArt::Ease(T/.57f));
                if(T<.57f)Foot.Z+=FMath::Sin(T/.57f*UE_PI)*600;
                if(T>.76f){const float A=MTArt::Ease((T-.76f)/.24f);Foot=FMath::Lerp(Foot,Rest,A);Foot.Z+=FMath::Sin(A*UE_PI)*170;}
            }
        }
        const float CompressionZ=ContactAge[I]<.28f?FMath::Sin(ContactAge[I]/.28f*UE_PI)*26:0;
        const FVector Hip=World(FVector(-25,Side*205,1020))+BodyOffset-FVector(0,0,CompressionZ*.45f);
        FVector Ankle=Foot+FVector(0,0,172-CompressionZ);
        const FVector Knee=MTArt::Joint(Hip,Ankle,Q.GetForwardVector(),500,530);
        Foot=Ankle-FVector(0,0,172-CompressionZ);
        const float ToeRoll=(!Stomp&&SwingProgress[I]<1)?FMath::Sin(SwingProgress[I]*UE_TWO_PI)*10:0;
        FRotator FootRotation=FRotationMatrix::MakeFromZX(FootNormals[I],Q.GetForwardVector()).Rotator();FootRotation.Pitch+=ToeRoll;
        Limb(Armor[11+I],Hip,Knee,500);Limb(Armor[13+I],Knee,Ankle,530);Pose(Armor[15+I],Foot,FootRotation);Feet[I]=Foot;
        const FQuat FootQ=FootRotation.Quaternion();Pose(Armor[19+I],Foot+FootQ.RotateVector(FVector(155,0,5)),FootRotation+FRotator(-ToeRoll*.65f,0,0));
        Pose(Armor[21+I],Foot+FootQ.RotateVector(FVector(-154,0,0)),FootRotation);
        const float Counter=FVector::DotProduct(Foot-Ground,Q.GetForwardVector())*.14f;
        const FVector Shoulder=World(FVector(0,Side*450,1550))+BodyOffset;
        FVector Wrist=World(FVector(130-Counter-AttackWeight*110,Side*(485+AttackWeight*80),680+Moving*12))+BodyOffset;
        const FVector Elbow=MTArt::Joint(Shoulder,Wrist,Q.RotateVector(FVector(-1,Side*.35f,0)),440,470);
        Pose(Armor[3+I],Shoulder,Facing+FRotator(-6,0,Side*4));Limb(Armor[5+I],Shoulder,Elbow,440);Limb(Armor[7+I],Elbow,Wrist,470);
        Pose(Armor[9+I],Wrist-FVector(0,0,60),Facing+FRotator(-8,0,Side*4));Hands[I]=Wrist;
    }
    float Tremor=0;if(Stomp&&T>.57f)Tremor=FMath::Sin(Clock*65)*FMath::Exp(-(T-.57f)*12)*8;
    if(W->State==EHeliosMechState::Firing)Tremor=FMath::Sin(Clock*52)*2;
    const float StepTremor=StepShakeAge<.28f?FMath::Sin(StepShakeAge*48)*FMath::Exp(-StepShakeAge*15)*StepShakeStrength:0;
    P->UpdateMechView(MTArt::Ease(W->DeployAge/.8f),Bob*.45f+Tremor+StepTremor);
    // A single owner-only layer shares all world poses. Looking down smoothly restores
    // exact world transforms, with no duplicate limbs and no change to player aim.
    const FTransform Camera=P->FirstPersonCamera->GetComponentTransform();
    for(int32 I=0;I<ViewLights.Num();++I){ViewLights[I]->SetWorldLocation(Camera.TransformPosition(FVector(160,I?760:-760,100)));ViewLights[I]->SetIntensity(I?190000.f:280000.f);}
    const float Framing=1-MTArt::Ease((-Camera.Rotator().Pitch-18)/35);
    const int32 ViewIndices[]={11,13,15,19,21,5,7,9};
    for(int32 I=0;I<2;++I)for(int32 J=0;J<8;++J)
    {
        auto* Source=Armor[ViewIndices[J]+I].Get();auto* View=ViewArmor[I*8+J].Get();
        const bool Leg=J<5;const float Size=Leg?.65f:.40f,Side=I?1.f:-1.f;
        FVector Local=Q.UnrotateVector(Source->GetComponentLocation()-Ground);Local.Y-=Side*(Leg?218:485);
        FVector Framed=FVector(Leg?1050:850,Side*(Leg?340:880),Leg?-1130:-800)+Local*Size;
        const FVector Location=FMath::Lerp(Source->GetComponentLocation(),Camera.TransformPosition(Framed),Framing);
        const FQuat Rotation=FQuat::Slerp(Source->GetComponentQuat(),Camera.GetRotation()*Q.Inverse()*Source->GetComponentQuat()*FQuat(FVector::UpVector,UE_PI),Framing);
        Pose(View,Location,Rotation.Rotator(),FVector(FMath::Lerp(1.f,Size,Framing)));
    }

    for(int32 I=0;I<StepWaves.Num();++I)
    {
        StepAges[I]+=Dt;const float A=StepAges[I]/.6f;
        if(A>=1){StepWaves[I]->SetVisibility(false);continue;}
        const float Radius=StepRadii[I]*(I%2?.28f+.54f*MTArt::Ease(A):.40f+.92f*MTArt::Ease(A));
        StepWaves[I]->SetWorldScale3D(FVector(Radius));
        if(StepMaterials[I])StepMaterials[I]->SetScalarParameterValue(TEXT("Opacity"),(1-A)*(I%2?.55f:1.f));
    }
    for(int32 I=0;I<StepLights.Num();++I){StepLightAges[I]+=Dt;StepLights[I]->SetIntensity(65000*FMath::Square(FMath::Max(0.f,1-StepLightAges[I]/.32f)));}
    const float BeamVisibility=bBeamOn?1.f:FMath::Clamp(BeamTail/.22f,0.f,1.f);BeamTail=FMath::Max(0.f,BeamTail-Dt);
    const FVector BD=(BeamEnd-BeamStart).GetSafeNormal();const float Length=FVector::Distance(BeamStart,BeamEnd);
    for(int32 I=0;I<Beams.Num();++I)
    {
        Beams[I]->SetVisibility(BeamVisibility>0);if(BeamVisibility<=0)continue;
        const float Radius=I==0?72.f:100.f+I*23.f;FRotator R=BD.Rotation();R.Roll=Clock*(I%2?-115:95)+I*67;
        Pose(Beams[I],BeamStart,R,FVector(Length,Radius,Radius));
        if(BeamMaterials[I]){BeamMaterials[I]->SetScalarParameterValue(TEXT("Opacity"),BeamVisibility*(I==0?1:.35f));BeamMaterials[I]->SetScalarParameterValue(TEXT("Seed"),I*1.73f);}
    }
    EmissionAge+=Dt;
    if(bBeamOn&&bBeamHit&&EmissionAge>.06f)
    {
        EmissionAge=0;
        for(int I=0;I<9;++I){const FVector V=(FMath::VRand()-BD*.9f).GetSafeNormal()*FMath::FRandRange(300.f,1300.f);Mote(BeamEnd+V*.025f,V,FMath::FRandRange(.2f,.65f),FMath::FRandRange(4.f,13.f),false);}
        Mote(BeamEnd-BD*35,FVector(0,0,180),1.2f,170,true);
        if(FMath::Fmod(Clock,.26f)<.07f)Burst(BeamEnd,ImpactNormal,470,false);
    }
    const FVector Eye=P->FirstPersonCamera->GetComponentLocation();
    for(int32 I=0;I<Plasma.Num();++I)
    {
        Plasma[I]->SetVisibility(bBeamOn&&bBeamHit);if(!bBeamOn||!bBeamHit)continue;
        const float Size=(I==0?210.f:I==1?360.f:480.f)*(1+.09f*FMath::Sin(Clock*19+I*2));
        const FVector Center=BeamEnd+ImpactNormal*(80+I*35);
        FRotator R=FRotationMatrix::MakeFromZ(Eye-Center).Rotator();
        Pose(Plasma[I],Center,R,FVector(Size));if(PlasmaMaterials[I])PlasmaMaterials[I]->SetScalarParameterValue(TEXT("Opacity"),I==0?1.f:I==1?.4f:.15f);
    }
    for(int32 I=0;I<Motes.Num();++I)
    {
        if(MoteAges[I]>=MoteLives[I]){Motes[I]->SetVisibility(false);continue;}MoteAges[I]+=Dt;const float A=FMath::Clamp(MoteAges[I]/MoteLives[I],0.f,1.f);
        MoteVelocities[I].Z-=Dt*(DustFlags[I]?25:680);MotePositions[I]+=MoteVelocities[I]*Dt;
        const float S=MoteSizes[I]*(DustFlags[I]?1+A*2:1-A*.5f);
        const FRotator R=DustFlags[I]?FRotationMatrix::MakeFromZ(Eye-MotePositions[I]).Rotator():MoteVelocities[I].Rotation();
        Pose(Motes[I],MotePositions[I],R,DustFlags[I]?FVector(S):FVector(S*5,S,S));
        if(MoteMaterials[I])MoteMaterials[I]->SetScalarParameterValue(TEXT("Opacity"),(1-A)*(DustFlags[I]?.32f:1));
    }
    for(int32 I=0;I<Impacts.Num();++I)
    {
        const float Life=I%2?5.f:.85f;ImpactAges[I]+=Dt;if(ImpactAges[I]>=Life){Impacts[I]->SetVisibility(false);continue;}
        const float A=ImpactAges[I]/Life;const float S=ImpactRadii[I]*(I%2?.65f:.2f+1.2f*MTArt::Ease(A));Impacts[I]->SetWorldScale3D(FVector(S));
        if(ImpactMaterials[I]){ImpactMaterials[I]->SetScalarParameterValue(TEXT("Opacity"),(1-A)*(I%2?.8f:1));ImpactMaterials[I]->SetScalarParameterValue(TEXT("Age"),A);}
    }
}
