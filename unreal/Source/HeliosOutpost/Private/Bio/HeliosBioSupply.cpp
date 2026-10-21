#include "Bio/HeliosBioSupply.h"
#include "Bio/HeliosBioHotelGameMode.h"
#include "Bio/HeliosBioSupplyWeaponDefinition.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Weapons/HeliosSSXEffect.h"

AHeliosBioSupply::AHeliosBioSupply()
{PrimaryActorTick.bCanEverTick=true;BoxMesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("OriginalBioSupply"));SetRootComponent(BoxMesh);BoxMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);BoxMesh->SetCanEverAffectNavigation(false);ContentPreview=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SupplyWeaponPreview"));ContentPreview->SetupAttachment(BoxMesh);ContentPreview->SetRelativeLocation(FVector(0,0,90));ContentPreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);ContentPreview->SetCanEverAffectNavigation(false);ContentPreview->SetCastShadow(false);Tags.Add(TEXT("BioSupplyBox"));}
void AHeliosBioSupply::BeginPlay(){Super::BeginPlay();ApplySourceAppearance();}
void AHeliosBioSupply::SetKind(EHeliosBioSupplyKind Kind){SupplyKind=Kind;if(HasActorBegunPlay())ApplySourceAppearance();}
void AHeliosBioSupply::SetWeaponContent(UHeliosBioSupplyWeaponDefinition* Definition)
{WeaponContent=SupplyKind==EHeliosBioSupplyKind::Normal?Definition:nullptr;ContentPreview->SetStaticMesh(WeaponContent?WeaponContent->SupplyPreviewMesh.LoadSynchronous():nullptr);}
bool AHeliosBioSupply::CanReachPickup(const AActor* Participant) const
{
    auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());
    if(!bAvailable||!Mode||!Participant||FVector::DistSquared(Participant->GetActorLocation(),GetActorLocation())>=22500)return false;
    // Keep legacy maps' placement behavior. Floating pickups additionally require
    // physical proximity on the same surface and actual world visibility.
    if(!Mode->bFloatingProfile)return true;
    const auto* Character=Cast<ACharacter>(Participant);if(!Character)return false;
    const FVector Feet=Character->GetActorLocation()-FVector(0,0,Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    FCollisionQueryParams Q(SCENE_QUERY_STAT(BioSupplyPickup),false,this);Q.AddIgnoredActor(Participant);
    for(const auto& P:Mode->Model.GetParticipants())if(auto* Other=Mode->ParticipantActor(P.Id))Q.AddIgnoredActor(Other);
    FHitResult Support;
    if(!GetWorld()->LineTraceSingleByChannel(Support,GetActorLocation()+FVector(0,0,35),GetActorLocation()-FVector(0,0,140),ECC_Visibility,Q))return false;
    if(FMath::Abs(Feet.Z-Support.ImpactPoint.Z)>65.f)return false;
    const FVector Start=Feet+FVector(0,0,FMath::Min(90.f,Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()*1.5f));
    FHitResult Obstruction;
    return !GetWorld()->LineTraceSingleByChannel(Obstruction,Start,GetActorLocation()+FVector(0,0,40),ECC_Visibility,Q);
}
void AHeliosBioSupply::ApplySourceAppearance()
{
    const bool Blue=SupplyKind==EHeliosBioSupplyKind::BlueHunterUpgrade;
    const bool Red=SupplyKind==EHeliosBioSupplyKind::RedTerminatorEvolution;
    const TCHAR* Path=Red?TEXT("/Game/Helios/CFMBioPyramid/Supply/SM_RedBioSupply.SM_RedBioSupply"):Blue?TEXT("/Game/Helios/CFMBioPyramid/Supply/SM_BlueBioSupply.SM_BlueBioSupply"):TEXT("/Game/Helios/CFMBioHotel/Supply/SM_BioSupply.SM_BioSupply");
    auto* Mesh=LoadObject<UStaticMesh>(nullptr,Path);BoxMesh->SetStaticMesh(Mesh);bHasOriginalMesh=Mesh!=nullptr;
    // Verified 410000035 -> RenLeiBox container -> Object205 -> YouLingBox blue material -> original texture.
    bBlueSupplySourceVerified=Blue&&Mesh!=nullptr;bRedSupplySourceVerified=Red&&Mesh!=nullptr;
    if(!Mesh)UE_LOG(LogTemp,Error,TEXT("BIOPYRAMID_ORIGINAL_SUPPLY_MISSING %s"),Path);
    RefreshSourceEffect();
}
void AHeliosBioSupply::RefreshSourceEffect()
{
    if(IsValid(BlueEffect))BlueEffect->Destroy();BlueEffect=nullptr;
    if(bAvailable&&bBlueSupplySourceVerified)
        BlueEffect=AHeliosSSXEffect::Spawn(this,TEXT("BioAvatar_RenLeiBox"),GetActorLocation(),FRotator(0,GetActorRotation().Yaw,0),false,1.f,TEXT("/Game/Helios/CFMBioPyramid/BlueSupplyEffects/DA_BlueSupplyEffects.DA_BlueSupplyEffects"));
}
void AHeliosBioSupply::SetAvailable(bool A){bAvailable=A;SetActorHiddenInGame(!A);RefreshSourceEffect();}
void AHeliosBioSupply::EndPlay(const EEndPlayReason::Type Reason){if(IsValid(BlueEffect))BlueEffect->Destroy();BlueEffect=nullptr;Super::EndPlay(Reason);}
void AHeliosBioSupply::Tick(float Dt)
{
    Super::Tick(Dt);if(!bAvailable||GetWorld()->IsPaused())return;
    CheckRemaining-=Dt;if(CheckRemaining>0)return;CheckRemaining=.12f;
    auto* M=AHeliosBioHotelGameMode::Find(GetWorld());if(!M)return;
    for(const auto& P:M->Model.GetParticipants())if(P.bAlive)
    {auto* A=M->ParticipantActor(P.Id);if(CanReachPickup(A)&&M->PickupSupply(P.Id,this))break;}
}
