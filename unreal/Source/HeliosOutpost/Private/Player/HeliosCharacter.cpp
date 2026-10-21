#include "Player/HeliosCharacter.h"
#include "World/HeliosMapDefinition.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerStart.h"
#include "Weapons/HeliosFighterWeapon.h"
#include "Control/HeliosControlSubsystem.h"
#include "Combat/HeliosCombatTempoSettings.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"

AHeliosCharacter::AHeliosCharacter(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer.SetDefaultSubobjectClass<UHeliosFighterMovement>(ACharacter::CharacterMovementComponentName))
{
    PrimaryActorTick.bCanEverTick = true;

    GetCapsuleComponent()->InitCapsuleSize(34.0f, 96.0f);

    FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
    FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
    FirstPersonCamera->SetRelativeLocation(FVector(-10.0f, 0.0f, 64.0f));
    FirstPersonCamera->bUsePawnControlRotation = true;
    FirstPersonCamera->FieldOfView = 90.0f;

    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw = true;
    bUseControllerRotationRoll = false;

    GetCharacterMovement()->bOrientRotationToMovement = false;
    GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
    GetCharacterMovement()->BrakingDecelerationWalking = 2048.0f;
    GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;
    GetCharacterMovement()->AirControl = 0.5f;
    GetCharacterMovement()->JumpZVelocity = 500.0f;
}

void AHeliosCharacter::GetActorEyesViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
    if (FirstPersonCamera)
    {
        OutLocation = FirstPersonCamera->GetComponentLocation();
        OutRotation = FirstPersonCamera->GetComponentRotation();
        return;
    }
    Super::GetActorEyesViewPoint(OutLocation, OutRotation);
}

void AHeliosCharacter::BeginPlay()
{
    Super::BeginPlay();
    WalkSpeed = UHeliosCombatTempoSettings::Resolve(this, EHeliosTempoFile::Movement,
        TEXT("玩家"), TEXT("步行"), WalkSpeed);
    SprintSpeed = UHeliosCombatTempoSettings::Resolve(this, EHeliosTempoFile::Movement,
        TEXT("玩家"), TEXT("冲刺"), SprintSpeed);
    SafeSpawnTransform = GetActorTransform();
    if(ResolveUrbanSafeSpawn(SafeSpawnTransform))SetActorTransform(SafeSpawnTransform,false,nullptr,ETeleportType::TeleportPhysics);
    ApplySprint(false);
}

void AHeliosCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (GetActorLocation().Z < ResetBelowZ)
    {
        ResetToSafeSpawn();
    }
}

void AHeliosCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &AHeliosCharacter::MoveForward);
    PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &AHeliosCharacter::MoveRight);
    PlayerInputComponent->BindAxis(TEXT("Turn"), this, &AHeliosCharacter::Turn);
    PlayerInputComponent->BindAxis(TEXT("LookUp"), this, &AHeliosCharacter::LookUp);
    PlayerInputComponent->BindAction(TEXT("Jump"), IE_Pressed, this, &AHeliosCharacter::StartJump);
    PlayerInputComponent->BindAction(TEXT("Jump"), IE_Released, this, &AHeliosCharacter::EndJump);
    PlayerInputComponent->BindAction(TEXT("Sprint"), IE_Pressed, this, &AHeliosCharacter::ToggleSprint);
    PlayerInputComponent->BindAction(TEXT("QuitPlay"), IE_Pressed, this, &AHeliosCharacter::QuitPlay);
    PlayerInputComponent->BindKey(EKeys::LeftControl,IE_Pressed,this,&AHeliosCharacter::FighterDescend);
    PlayerInputComponent->BindKey(EKeys::LeftControl,IE_Released,this,&AHeliosCharacter::FighterStopDescend);
}

void AHeliosCharacter::MoveForward(float Value)
{
    if(FighterFlight){FighterFlight->ForwardRightUp.X=Value;if(Value!=0)NotifyManualInput();return;}
    if (!FMath::IsNearlyZero(Value))
    {
        NotifyManualInput();
        AddMovementInput(GetActorForwardVector(), Value);
    }
}

void AHeliosCharacter::MoveRight(float Value)
{
    if(FighterFlight){FighterFlight->ForwardRightUp.Y=Value;if(Value!=0)NotifyManualInput();return;}
    if (!FMath::IsNearlyZero(Value))
    {
        NotifyManualInput();
        AddMovementInput(GetActorRightVector(), Value);
    }
}

void AHeliosCharacter::Turn(float Value)
{
    if (!FMath::IsNearlyZero(Value))
    {
        NotifyManualInput();
        AddControllerYawInput(Value);
    }
}

void AHeliosCharacter::LookUp(float Value)
{
    if (!FMath::IsNearlyZero(Value))
    {
        NotifyManualInput();
        AddControllerPitchInput(Value);
    }
}

void AHeliosCharacter::StartJump()
{
    NotifyManualInput();
    if(FighterFlight){FighterFlight->SetVerticalKey(true,true);return;}
    if (!bMechMode) Jump();
}

void AHeliosCharacter::EndJump()
{
    if(FighterFlight){FighterFlight->SetVerticalKey(true,false);return;}
    StopJumping();
}

void AHeliosCharacter::ToggleSprint()
{
    NotifyManualInput();
    if(FighterFlight){FighterFlight->bBoost=!FighterFlight->bBoost;return;}
    bManualSprintEnabled = !bManualSprintEnabled;
    ApplySprint(bManualSprintEnabled);
}

void AHeliosCharacter::QuitPlay()
{
    UKismetSystemLibrary::QuitGame(this, Cast<APlayerController>(GetController()), EQuitPreference::Quit, false);
}

void AHeliosCharacter::ResetToSafeSpawn()
{
    auto* Fighter=FighterFlight?Cast<AHeliosFighterWeapon>(FighterFlight->GetOwner()):nullptr;
    if(Fighter)Fighter->SuspendForArrival();
    const bool RestoreMech=bMechMode;
    if(RestoreMech)ExitMechMode();
    NotifyManualInput();
    ClearMovementInput();
    FTransform Destination=SafeSpawnTransform;
    if(ResolveUrbanSafeSpawn(Destination))
    {SafeSpawnTransform=Destination;SetActorTransform(Destination,false,nullptr,ETeleportType::TeleportPhysics);}
    else UE_LOG(LogTemp,Warning,TEXT("URBAN_PLAYER_SPAWN no_safe_capsule_space; teleport deferred"));
    if(RestoreMech)EnterMechMode();
    if(Fighter)Fighter->TryResumeAfterArrival();
    if (GetCharacterMovement())
    {
        GetCharacterMovement()->StopMovementImmediately();
    }
}

bool AHeliosCharacter::ResolveUrbanSafeSpawn(FTransform& InOut) const
{
    const auto* Map=FHeliosMapRegistry::ForWorld(GetWorld());
    if(!Map||Map->Id!=TEXT("urban_challenge"))return true;
    if(auto* Start=UGameplayStatics::GetActorOfClass(this,APlayerStart::StaticClass()))InOut=Start->GetActorTransform();
    const auto* Capsule=GetCapsuleComponent();const float Radius=Capsule->GetScaledCapsuleRadius(),Height=Capsule->GetScaledCapsuleHalfHeight();
    FCollisionQueryParams Q(SCENE_QUERY_STAT(UrbanPlayerSafeSpawn),false,this);
    // Authored start first, then a small local ring inside the existing supply area.
    for(const FVector Offset:{FVector::ZeroVector,FVector(140,0,0),FVector(-140,0,0),FVector(0,140,0),FVector(0,-140,0),FVector(280,0,0),FVector(-280,0,0)})
    {
        const FVector Probe=InOut.GetLocation()+Offset;FHitResult Ground;
        if(!GetWorld()->LineTraceSingleByChannel(Ground,Probe+FVector(0,0,20),Probe-FVector(0,0,Height+150),ECC_WorldStatic,Q)||Ground.ImpactNormal.Z<.7f)continue;
        const FVector Center=Ground.ImpactPoint+FVector(0,0,Height+3);
        if(GetWorld()->OverlapBlockingTestByChannel(Center,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Radius+2,Height),Q))continue;
        InOut.SetLocation(Center);return true;
    }
    return false;
}

void AHeliosCharacter::SetSprintingFromAutomation(bool bSprint)
{
    ApplySprint(bSprint || bManualSprintEnabled);
}

void AHeliosCharacter::NotifyManualInput()
{
    if (UWorld* World = GetWorld())
    {
        if (UHeliosControlSubsystem* Control = World->GetSubsystem<UHeliosControlSubsystem>())
        {
            Control->NotifyManualInput(this);
        }
    }
}

void AHeliosCharacter::ApplySprint(bool bSprint)
{
    bAppliedSprint = bSprint;
    if (GetCharacterMovement())
    {
        GetCharacterMovement()->MaxWalkSpeed = (bSprint ? SprintSpeed : WalkSpeed) * EquipmentMovementMultiplier;
    }
}

void AHeliosCharacter::SetEquipmentMovementMultiplier(float Multiplier)
{
    EquipmentMovementMultiplier = FMath::IsFinite(Multiplier) ? FMath::Clamp(Multiplier, .1f, 3.f) : 1.f;
    ApplySprint(bAppliedSprint);
}

void AHeliosCharacter::ClearMovementInput()
{
    if(FighterFlight)FighterFlight->ClearInput();
    bManualSprintEnabled = false;
    ApplySprint(false);
    ConsumeMovementInputVector();
    StopJumping();
    if (GetCharacterMovement()) GetCharacterMovement()->StopMovementImmediately();
}

FVector AHeliosCharacter::GetGroundReference() const
{return GetActorLocation()-FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight());}
FVector AHeliosCharacter::GetGameplayReference() const
{
    if(FighterFlight)
    {
        FHitResult H;FCollisionQueryParams Q(SCENE_QUERY_STAT(FighterGroundReference),false,this);
        if(GetWorld()->LineTraceSingleByChannel(H,GetActorLocation(),GetActorLocation()-FVector(0,0,20000),ECC_WorldStatic,Q))return H.ImpactPoint+FVector(0,0,96);
    }
    return bMechMode?GetGroundReference()+FVector(0,0,SavedHalfHeight):GetActorLocation();
}
void AHeliosCharacter::FighterDescend(){if(FighterFlight){NotifyManualInput();FighterFlight->SetVerticalKey(false,true);}}
void AHeliosCharacter::FighterStopDescend(){if(FighterFlight)FighterFlight->SetVerticalKey(false,false);}
bool AHeliosCharacter::FindMechDeployment(FVector& OutFeet) const
{
    if(!GetWorld())return false;
    const FVector Base=GetGroundReference();
    FCollisionQueryParams Q(SCENE_QUERY_STAT(TitanDeployment),false,this);
    TArray<AActor*> Attached;GetAttachedActors(Attached);Q.AddIgnoredActors(Attached);
    FCollisionObjectQueryParams O;O.AddObjectTypesToQuery(ECC_WorldStatic);O.AddObjectTypesToQuery(ECC_WorldDynamic);
    // Deterministic nearest-ring search; require walkable ground and full-height clearance.
    for(int32 Ring=0;Ring<=6;++Ring)for(int32 I=0;I<(Ring?24:1);++I)
    {
        const float Angle=I*UE_TWO_PI/24;const FVector At=Base+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*(Ring*500.f);
        FHitResult H;
        if(!GetWorld()->LineTraceSingleByObjectType(H,At+FVector(0,0,180),At-FVector(0,0,600),O,Q)||H.ImpactNormal.Z<.7f)continue;
        const FVector Feet=H.ImpactPoint+FVector(0,0,3);
        if(GetWorld()->OverlapBlockingTestByChannel(Feet+FVector(0,0,1000),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(250,1000),Q))continue;
        OutFeet=Feet;return true;
    }
    return false;
}
bool AHeliosCharacter::EnterMechMode()
{
    if(bMechMode)return true;FVector Feet;
    if(!FindMechDeployment(Feet)){MechDeploymentError=TEXT("附近没有足够空间部署20米机甲，请前往开阔地带。");return false;}
    MechDeploymentError.Empty();auto* Move=GetCharacterMovement();auto* Capsule=GetCapsuleComponent();
    SavedCamera=FirstPersonCamera->GetRelativeLocation();SavedFOV=FirstPersonCamera->FieldOfView;
    SavedRadius=Capsule->GetUnscaledCapsuleRadius();SavedHalfHeight=Capsule->GetUnscaledCapsuleHalfHeight();
    SavedWalk=WalkSpeed;SavedSprint=SprintSpeed;SavedJump=Move->JumpZVelocity;bSavedNavUpdate=Move->ShouldUpdateNavAgentWithOwnersCollision();
    Move->SetUpdateNavAgentWithOwnersCollisions(false);bMechMode=true;
    Capsule->SetCapsuleSize(250,1000,false);SetActorLocation(Feet+FVector(0,0,1000),false,nullptr,ETeleportType::TeleportPhysics);
    WalkSpeed=UHeliosCombatTempoSettings::Resolve(this,EHeliosTempoFile::Movement,TEXT("黑金巨神"),TEXT("步行"),900);
    SprintSpeed=UHeliosCombatTempoSettings::Resolve(this,EHeliosTempoFile::Movement,TEXT("黑金巨神"),TEXT("冲刺"),1400);
    Move->JumpZVelocity=0;StopJumping();bManualSprintEnabled=false;ApplySprint(false);FirstPersonCamera->SetFieldOfView(95);UpdateMechView(0,0);return true;
}
void AHeliosCharacter::ExitMechMode()
{
    if(!bMechMode)return;const FVector Feet=GetGroundReference();bMechMode=false;auto* Move=GetCharacterMovement();
    GetCapsuleComponent()->SetCapsuleSize(SavedRadius,SavedHalfHeight,false);
    SetActorLocation(Feet+FVector(0,0,SavedHalfHeight),false,nullptr,ETeleportType::TeleportPhysics);
    FirstPersonCamera->SetRelativeLocation(SavedCamera);FirstPersonCamera->SetFieldOfView(SavedFOV);
    WalkSpeed=SavedWalk;SprintSpeed=SavedSprint;Move->JumpZVelocity=SavedJump;Move->SetUpdateNavAgentWithOwnersCollisions(bSavedNavUpdate);bManualSprintEnabled=false;ApplySprint(false);
}
void AHeliosCharacter::UpdateMechView(float Alpha,float Bob)
{
    if(bMechMode)FirstPersonCamera->SetRelativeLocation(FVector(FMath::Lerp(float(SavedCamera.X),320.f,Alpha),0,FMath::Lerp(float(SavedCamera.Z+SavedHalfHeight)-1000.f,800.f,Alpha)+Bob));
}
