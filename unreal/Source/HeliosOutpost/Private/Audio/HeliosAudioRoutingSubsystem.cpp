#include "Audio/HeliosAudioRoutingSubsystem.h"
#include "Audio/HeliosAudioSettingsSubsystem.h"
#include "ActiveSound.h"
#include "Engine/World.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Pets/HeliosPetDog.h"
#include "Enemies/HeliosEnemyActor.h"
#include "EngineUtils.h"
#include "Misc/ScopeLock.h"
#include "AudioDevice.h"
namespace
{
FCriticalSection RoutingLock;
struct FRouting {TArray<USoundClass*> Classes;TSet<uint32> PetOwners,EnemyOwners;};
TMap<uint32,FRouting> Routes;
TMap<uint32,FRouting> DeviceRoutes;
}
void UHeliosAudioRoutingSubsystem::Publish(UWorld* W,const TArray<TObjectPtr<USoundClass>>& Classes)
{
    if(!W)return;FRouting R;for(auto& C:Classes)R.Classes.Add(C);for(TActorIterator<AHeliosPetCompanion> I(W);I;++I)R.PetOwners.Add(I->GetUniqueID());for(TActorIterator<AHeliosEnemyActor> I(W);I;++I)R.EnemyOwners.Add(I->GetUniqueID());
    FScopeLock Lock(&RoutingLock);if(auto* D=W->GetAudioDeviceRaw())DeviceRoutes.Add(D->DeviceID,R);Routes.Add(W->GetUniqueID(),MoveTemp(R));
}
void UHeliosAudioRoutingSubsystem::Forget(const TArray<TObjectPtr<USoundClass>>& Classes)
{FScopeLock Lock(&RoutingLock);for(auto I=Routes.CreateIterator();I;++I)if(!I->Value.Classes.IsEmpty()&&Classes.Contains(I->Value.Classes[0]))I.RemoveCurrent();for(auto I=DeviceRoutes.CreateIterator();I;++I)if(!I->Value.Classes.IsEmpty()&&Classes.Contains(I->Value.Classes[0]))I.RemoveCurrent();}
void UHeliosAudioRoutingSubsystem::NotifyActiveSoundCreated(FActiveSound& A)
{
    // Component playback in UE 5.8 does not call NotifyActiveSoundOwner. This
    // audio-thread hook uses only an immutable routing snapshot published on GT.
    FScopeLock Lock(&RoutingLock);auto* R=Routes.Find(A.GetWorldID());if(!R&&A.GetWorldID()==0)R=DeviceRoutes.Find(GetAudioDeviceId());if(!R||R->Classes.Num()!=int32(EHeliosAudioBus::Count))return;
    if(A.GetSound()&&A.GetSound()->IsLooping())A.bShouldRemainActiveIfDropped=true;
    if(R->Classes.Contains(A.GetSoundClass()))return;
    EHeliosAudioBus B=EHeliosAudioBus::Environment;
    if(R->PetOwners.Contains(A.GetOwnerID()))B=EHeliosAudioBus::Companion;
    else if(R->EnemyOwners.Contains(A.GetOwnerID()))B=EHeliosAudioBus::Environment;
    else if(auto* S=A.GetSound())
    {
        const FString P=S->GetPathName(); // active sound owns this immutable loaded asset
        if(P.Contains(TEXT("/VO_")))B=EHeliosAudioBus::CharacterVoice;
        else if(P.Contains(TEXT("SFX_Hurt"))||P.Contains(TEXT("SFX_Death")))B=EHeliosAudioBus::PlayerHurt;
        else if(P.Contains(TEXT("/KillFeedback/")))B=EHeliosAudioBus::KillFeedback;
        else if(P.Contains(TEXT("UI_"))||P.Contains(TEXT("/UI/"))||P.StartsWith(TEXT("/Engine/")))B=EHeliosAudioBus::Interface;
        else if(P.Contains(TEXT("Pickup"))||P.Contains(TEXT("Footstep"))||P.Contains(TEXT("Step_"))||P.Contains(TEXT("Ambien")))B=EHeliosAudioBus::Environment;
        else if(P.Contains(TEXT("/Weapons/"))||P.Contains(TEXT("/CFM"))||P.Contains(TEXT("SFX_PlayerDamage07"))||P.Contains(TEXT("SFX_Fire"))||P.Contains(TEXT("SFX_Reload")))B=EHeliosAudioBus::Weapon;
    }
    A.SetSoundClass(R->Classes[int32(B)]);
}
