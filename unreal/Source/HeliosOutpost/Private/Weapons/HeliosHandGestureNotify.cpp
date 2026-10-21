#include "Weapons/HeliosHandGestureNotify.h"
#include "Audio/HeliosAudio.h"
#include "Weapons/HeliosMysteriousHand.h"
#include "Weapons/HeliosWeaponVFX.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
void UHeliosHandGestureNotify::Notify(USkeletalMeshComponent* MeshComp,UAnimSequenceBase* Animation,const FAnimNotifyEventReference& Reference)
{
    Super::Notify(MeshComp,Animation,Reference);
    if(!MeshComp||!MeshComp->GetWorld()||!MeshComp->GetWorld()->IsGameWorld())return;
    auto* H=Cast<AHeliosMysteriousHand>(MeshComp->GetOwner());if(!H||H->IsHidden())return;
    const FVector At=MeshComp->GetSocketLocation(bBoo?TEXT("ThumbTip"):TEXT("IndexTip"));
    if(auto* S=LoadObject<USoundBase>(nullptr,bBoo?TEXT("/Game/Helios/Weapons/MysteriousHand/SFX_MH_Boo.SFX_MH_Boo"):TEXT("/Game/Helios/Weapons/MysteriousHand/SFX_MH_Laser.SFX_MH_Laser")))
        HeliosAudio::PlaySoundAtLocation(H,S,At,.12f,bBoo?1.5f:1.8f);
    if(!bBoo)UHeliosWeaponVFX::SpawnOneShot(H,LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Helios/Weapons/MysteriousHand/NS_MH_FingerFlash.NS_MH_FingerFlash")),At,FRotator::ZeroRotator,FVector(.3f),.15f);
}
