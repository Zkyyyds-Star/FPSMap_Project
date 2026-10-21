#include "Audio/HeliosAudio.h"
#include "Audio/HeliosAudioSettingsSubsystem.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"
namespace HeliosAudio
{
static void Route(UAudioComponent* C,const UObject* O,USoundBase* S){if(auto* A=UHeliosAudioSettingsSubsystem::Get(O))A->Route(C,O,S);}
UAudioComponent* CreateSound2D(const UObject* O,USoundBase* S,float V,float P,float T,USoundConcurrency* N,bool Persist,bool Auto,bool WorldOwned)
{const UObject* PlaybackContext=WorldOwned&&O?O->GetWorld():O;auto* C=UGameplayStatics::CreateSound2D(PlaybackContext,S,V,P,T,N,Persist,Auto);Route(C,O,S);return C;}
UAudioComponent* SpawnSound2D(const UObject* O,USoundBase* S,float V,float P,float T,USoundConcurrency* N,bool Persist,bool Auto,bool WorldOwned)
{auto* C=CreateSound2D(O,S,V,P,T,N,Persist,Auto,WorldOwned);if(C)C->Play(T);return C;}
void PlaySound2D(const UObject* O,USoundBase* S,float V,float P,float T,USoundConcurrency* N,const AActor* Owner,bool UI)
{auto* C=CreateSound2D(O,S,V,P,T,N,false,true);if(C){C->bIsUISound=UI;C->Play(T);}}
UAudioComponent* SpawnSoundAtLocation(const UObject* O,USoundBase* S,FVector L,FRotator R,float V,float P,float T,USoundAttenuation* A,USoundConcurrency* N,bool Auto,bool WorldOwned)
{
    auto* W=GEngine&&O?GEngine->GetWorldFromContextObject(O,EGetWorldErrorMode::ReturnNull):nullptr;
    if(!S||!W||!GEngine->UseSound()||!W->bAllowAudioPlayback||W->IsNetMode(NM_DedicatedServer))return nullptr;
    auto* Owner=Cast<AActor>(const_cast<UObject*>(O));if(auto* AC=Cast<UActorComponent>(const_cast<UObject*>(O)))Owner=AC->GetOwner();if(WorldOwned)Owner=nullptr;
    FAudioDevice::FCreateComponentParams Params(W,Owner);Params.SetLocation(L);Params.AttenuationSettings=A;if(N)Params.ConcurrencySet.Add(N);
    auto* C=FAudioDevice::CreateComponent(S,Params);if(C){C->SetWorldLocationAndRotation(L,R);C->SetVolumeMultiplier(V);C->SetPitchMultiplier(P);C->bAllowSpatialization=Params.ShouldUseAttenuation();C->bIsUISound=!W->IsGameWorld();C->bAutoDestroy=Auto;C->SubtitlePriority=S->GetSubtitlePriority();C->bStopWhenOwnerDestroyed=false;Route(C,O,S);C->Play(T);}return C;
}
void PlaySoundAtLocation(const UObject* O,USoundBase* S,FVector L,float V,float P,float T,USoundAttenuation* A,USoundConcurrency* N,const AActor* Owner)
{SpawnSoundAtLocation(O,S,L,FRotator::ZeroRotator,V,P,T,A,N,true);}
UAudioComponent* SpawnSoundAttached(USoundBase* S,USceneComponent* Attach,FName Socket,FVector L,EAttachLocation::Type Type,bool Stop,float V,float P,float T,USoundAttenuation* A,USoundConcurrency* N,bool Auto)
{return SpawnSoundAttached(S,Attach,Socket,L,FRotator::ZeroRotator,Type,Stop,V,P,T,A,N,Auto);}
UAudioComponent* SpawnSoundAttached(USoundBase* S,USceneComponent* Attach,FName Socket,FVector L,FRotator R,EAttachLocation::Type Type,bool Stop,float V,float P,float T,USoundAttenuation* A,USoundConcurrency* N,bool Auto)
{
    if(!S||!Attach)return nullptr;auto* W=Attach->GetWorld();if(!W||!W->bAllowAudioPlayback||W->IsNetMode(NM_DedicatedServer))return nullptr;
    FVector Test=L;if(Type==EAttachLocation::KeepRelativeOffset)Test=Attach->GetSocketTransform(Socket).TransformPosition(L);else if(Type==EAttachLocation::SnapToTarget||Type==EAttachLocation::SnapToTargetIncludingScale)Test=Attach->GetSocketLocation(Socket);
    FAudioDevice::FCreateComponentParams Params(W,Attach->GetOwner());Params.SetLocation(Test);Params.bStopWhenOwnerDestroyed=Stop;Params.AttenuationSettings=A;if(N)Params.ConcurrencySet.Add(N);
    auto* C=FAudioDevice::CreateComponent(S,Params);if(C){if(Type==EAttachLocation::SnapToTarget||Type==EAttachLocation::SnapToTargetIncludingScale)C->AttachToComponent(Attach,FAttachmentTransformRules::SnapToTargetNotIncludingScale,Socket);else{C->AttachToComponent(Attach,FAttachmentTransformRules::KeepRelativeTransform,Socket);if(Type==EAttachLocation::KeepWorldPosition)C->SetWorldLocationAndRotation(L,R);else C->SetRelativeLocationAndRotation(L,R);}C->SetVolumeMultiplier(V);C->SetPitchMultiplier(P);C->bAllowSpatialization=Params.ShouldUseAttenuation();C->bIsUISound=!W->IsGameWorld();C->bAutoDestroy=Auto;C->SubtitlePriority=S->GetSubtitlePriority();Route(C,Attach,S);C->Play(T);}return C;
}
}
