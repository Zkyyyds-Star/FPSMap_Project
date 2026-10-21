#include "Weapons/HeliosFighterWeapon.h"
#include "Player/HeliosArenaCharacter.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/PlayerController.h"
#include "World/HeliosZone.h"
#include "World/HeliosMapDefinition.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "EngineUtils.h"

void UHeliosFighterMovement::PhysFlying(float Dt,int32 Iterations)
{
    auto* P=Cast<AHeliosCharacter>(CharacterOwner);
    if(P && P->FighterFlight && P->FighterFlight->bActive) P->FighterFlight->Simulate(Dt,this);
    else Super::PhysFlying(Dt,Iterations);
}

bool UHeliosFighterFlightComponent::FindDeployment(FVector& Center,float& Ground) const
{
    const auto* W=Cast<AHeliosFighterWeapon>(GetOwner());
    const auto* P=W?W->Player():nullptr;if(!P||!GetWorld())return false;
    const FVector Base=P->GetGameplayReference();
    FCollisionQueryParams Q(SCENE_QUERY_STAT(FighterDeploy),false,P);Q.AddIgnoredActor(W);
    FCollisionObjectQueryParams O;O.AddObjectTypesToQuery(ECC_WorldStatic);O.AddObjectTypesToQuery(ECC_WorldDynamic);
    for(int Ring=0;Ring<=6;++Ring)for(int I=0;I<(Ring?16:1);++I)
    {
        const float A=I*UE_TWO_PI/16;const FVector At=Base+FVector(FMath::Cos(A),FMath::Sin(A),0)*(Ring*400.f);
        FHitResult H;
        if(!GetWorld()->LineTraceSingleByObjectType(H,At+FVector(0,0,350),At-FVector(0,0,1200),O,Q)||H.ImpactNormal.Z<.65f)continue;
        const FVector Candidate=H.ImpactPoint+FVector(0,0,220);
        if(GetWorld()->OverlapBlockingTestByChannel(Candidate,P->GetActorQuat(),ECC_Pawn,FCollisionShape::MakeBox(HullExtent()+FVector(15)),Q))continue;
        Center=Candidate;Ground=H.ImpactPoint.Z;return true;
    }
    return false;
}

bool UHeliosFighterFlightComponent::Deploy()
{
    if(bActive)return true;
    auto* W=Cast<AHeliosFighterWeapon>(GetOwner());auto* P=W?W->Player():nullptr;
    FVector Center;if(!P||!W->Settings||!FindDeployment(Center,GroundZ))return false;
    auto* Move=P->GetCharacterMovement();auto* Cap=P->GetCapsuleComponent();
    SavedRadius=Cap->GetUnscaledCapsuleRadius();SavedHalfHeight=Cap->GetUnscaledCapsuleHalfHeight();
    SavedCamera=P->FirstPersonCamera->GetRelativeLocation();SavedFOV=P->FirstPersonCamera->FieldOfView;
    SavedMode=Move->MovementMode;bSavedNav=Move->ShouldUpdateNavAgentWithOwnersCollision();
    Move->SetUpdateNavAgentWithOwnersCollisions(false);
    Cap->SetCapsuleSize(80,80,false);P->SetActorLocation(Center,false,nullptr,ETeleportType::TeleportPhysics);
    P->FirstPersonCamera->SetRelativeLocation(FVector(12,0,42));P->FirstPersonCamera->SetFieldOfView(90);
    if(auto* PC=Cast<APlayerController>(P->GetController()))if(auto* C=PC->PlayerCameraManager.Get())
    {SavedPitchMin=C->ViewPitchMin;SavedPitchMax=C->ViewPitchMax;C->ViewPitchMin=-75;C->ViewPitchMax=75;}
    FlightBounds=FBox(EForceInit::ForceInit);
    for(TActorIterator<AHeliosZone> Z(GetWorld());Z;++Z)if(Z->Bounds)FlightBounds+=Z->Bounds->Bounds.GetBox();
    if(!FlightBounds.IsValid)FlightBounds=FBox(Center-FVector(12000,12000,0),Center+FVector(12000,12000,0));
    FlightBounds.Min.X+=300;FlightBounds.Min.Y+=300;FlightBounds.Max.X-=300;FlightBounds.Max.Y-=300;
    // Large streamed maps use authored complete extents, independent of loaded cells.
    if(const auto* Map=FHeliosMapRegistry::ForWorld(GetWorld()))
    {
        FConfigFile Config;Config.Read(FPaths::ProjectContentDir()/TEXT("Settings/战斗数值/战斗机.ini"));
        const FString Section=TEXT("Bounds.")+Map->AssetMapName().ToString();float MinX,MinY,MaxX,MaxY;
        if(Config.GetFloat(*Section,TEXT("MinX"),MinX)&&Config.GetFloat(*Section,TEXT("MaxX"),MaxX)&&Config.GetFloat(*Section,TEXT("MinY"),MinY)&&Config.GetFloat(*Section,TEXT("MaxY"),MaxY)
            &&FMath::IsFinite(MinX)&&FMath::IsFinite(MinY)&&FMath::IsFinite(MaxX)&&FMath::IsFinite(MaxY)&&MinX<MaxX&&MinY<MaxY)
        {FlightBounds.Min.X=MinX;FlightBounds.Min.Y=MinY;FlightBounds.Max.X=MaxX;FlightBounds.Max.Y=MaxY;}
    }
    // Always include the verified deployment position, even if it is at a zone seam.
    FlightBounds+=Center-FVector(350,350,0);FlightBounds+=Center+FVector(350,350,0);
    FlightBounds.Min.Z=GroundZ-1000;FlightBounds.Max.Z=GroundZ+W->Settings->Ceiling;
    HullRotation=P->GetActorRotation();HullRotation.Pitch=HullRotation.Roll=0;
    ClearInput();bActive=true;P->FighterFlight=this;Move->StopMovementImmediately();Move->SetMovementMode(MOVE_Flying);
    return true;
}

void UHeliosFighterFlightComponent::Restore()
{
    if(!bActive)return;auto* W=Cast<AHeliosFighterWeapon>(GetOwner());auto* P=W?W->Player():nullptr;
    bActive=false;ClearInput();if(!P)return;P->FighterFlight=nullptr;
    auto* M=P->GetCharacterMovement();M->StopMovementImmediately();M->SetUpdateNavAgentWithOwnersCollisions(bSavedNav);
    P->GetCapsuleComponent()->SetCapsuleSize(SavedRadius,SavedHalfHeight,false);
    P->FirstPersonCamera->SetRelativeLocation(SavedCamera);P->FirstPersonCamera->SetFieldOfView(SavedFOV);
    if(auto* PC=Cast<APlayerController>(P->GetController()))if(auto* C=PC->PlayerCameraManager.Get()){C->ViewPitchMin=SavedPitchMin;C->ViewPitchMax=SavedPitchMax;}
    M->SetMovementMode(P->IsDead()?MOVE_None:SavedMode.GetValue());
}
void UHeliosFighterFlightComponent::ClearInput(){ForwardRightUp=FVector::ZeroVector;bBoost=bUpHeld=bDownHeld=false;}
void UHeliosFighterFlightComponent::SetVerticalKey(bool Ascend,bool Held)
{if(Ascend)bUpHeld=Held;else bDownHeld=Held;ForwardRightUp.Z=(bUpHeld?1.f:0.f)-(bDownHeld?1.f:0.f);}
FVector UHeliosFighterFlightComponent::DesiredVelocity() const
{
    const auto* W=Cast<AHeliosFighterWeapon>(GetOwner());const auto* P=W?W->Player():nullptr;if(!P||!W->Settings)return FVector::ZeroVector;
    const auto* D=W->Settings.Get();const FVector I=ForwardRightUp.GetClampedToMaxSize(1);
    const FRotationMatrix R(P->GetControlRotation());
    FVector V=R.GetUnitAxis(EAxis::X)*I.X*(I.X>=0?(bBoost?D->BoostSpeed:D->ForwardSpeed):D->StrafeSpeed)
        +R.GetUnitAxis(EAxis::Y)*I.Y*D->StrafeSpeed+FVector::UpVector*I.Z*D->VerticalSpeed;
    // Pitch can align forward and vertical controls; also cap that combined direction.
    return V.GetClampedToMaxSize(I.X>0?(bBoost?D->BoostSpeed:D->ForwardSpeed):FMath::Max(D->StrafeSpeed,D->VerticalSpeed));
}
void UHeliosFighterFlightComponent::Simulate(float Dt,UCharacterMovementComponent* M)
{
    auto* W=Cast<AHeliosFighterWeapon>(GetOwner());auto* P=W?W->Player():nullptr;if(!P||!bActive)return;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(FighterHull),false,P);Q.AddIgnoredActor(W);
    FCollisionObjectQueryParams O;O.AddObjectTypesToQuery(ECC_WorldStatic);O.AddObjectTypesToQuery(ECC_WorldDynamic);
    FRotator Desired=P->GetControlRotation();Desired.Pitch=FMath::Clamp(FRotator::NormalizeAxis(Desired.Pitch),-75.f,75.f);Desired.Roll=0;
    if(!GetWorld()->OverlapBlockingTestByChannel(P->GetActorLocation(),Desired.Quaternion(),ECC_Pawn,FCollisionShape::MakeBox(HullExtent()),Q))HullRotation=Desired;
    else if(auto* C=P->GetController())C->SetControlRotation(HullRotation);
    bBoundary=false;float Remaining=FMath::Min(Dt,.25f);
    const FVector Target=DesiredVelocity();
    while(Remaining>UE_KINDA_SMALL_NUMBER)
    {
        const float Step=FMath::Min(Remaining,1.f/120);Remaining-=Step;
        M->Velocity=FMath::Lerp(M->Velocity,Target,1.f-FMath::Exp(-(Target.IsNearlyZero()?7.f:4.f)*Step));
        FVector Delta=M->Velocity*Step;const FVector Start=P->GetActorLocation();
        const FVector Bounded=(Start+Delta).BoundToBox(FlightBounds.Min,FlightBounds.Max);
        if(!Bounded.Equals(Start+Delta,.01f)){bBoundary=true;Delta=Bounded-Start;M->Velocity=Delta/Step;}
        FHitResult HullHit;
        if(GetWorld()->SweepSingleByChannel(HullHit,Start,Start+Delta,HullRotation.Quaternion(),ECC_Pawn,FCollisionShape::MakeBox(HullExtent()),Q))
        {
            const FVector Travel=Delta*FMath::Max(0.f,HullHit.Time-.002f);
            FHitResult CoreHit;M->SafeMoveUpdatedComponent(Travel,P->GetActorQuat(),true,CoreHit);
            FVector Slide=FVector::VectorPlaneProject(Delta-Travel,HullHit.Normal);
            const FVector SlideStart=P->GetActorLocation();FHitResult SlideHit;
            if(GetWorld()->SweepSingleByChannel(SlideHit,SlideStart,SlideStart+Slide,HullRotation.Quaternion(),ECC_Pawn,FCollisionShape::MakeBox(HullExtent()),Q))Slide*=FMath::Max(0.f,SlideHit.Time-.002f);
            M->SafeMoveUpdatedComponent(Slide,P->GetActorQuat(),true,CoreHit);
            M->Velocity=FVector::VectorPlaneProject(M->Velocity,HullHit.Normal)*.96f;
        }
        else {FHitResult H;M->SafeMoveUpdatedComponent(Delta,P->GetActorQuat(),true,H);if(H.IsValidBlockingHit())M->Velocity=FVector::VectorPlaneProject(M->Velocity,H.Normal);}
    }
    FHitResult Ground;const FVector At=P->GetActorLocation();
    Altitude=GetWorld()->LineTraceSingleByObjectType(Ground,At,At-FVector(0,0,20000),O,Q)?FMath::Max(0.f,float(At.Z-Ground.ImpactPoint.Z))/100.f:-1.f;
    M->UpdateComponentVelocity();P->ConsumeMovementInputVector();
}
