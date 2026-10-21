#pragma once
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
class UAudioComponent; class USoundBase; class USoundAttenuation; class USoundConcurrency; class USceneComponent;
namespace HeliosAudio
{
HELIOSOUTPOST_API UAudioComponent* CreateSound2D(const UObject*,USoundBase*,float=1,float=1,float=0,USoundConcurrency* =nullptr,bool=false,bool=true,bool WorldOwned=false);
HELIOSOUTPOST_API UAudioComponent* SpawnSound2D(const UObject*,USoundBase*,float=1,float=1,float=0,USoundConcurrency* =nullptr,bool=false,bool=true,bool WorldOwned=false);
HELIOSOUTPOST_API void PlaySound2D(const UObject*,USoundBase*,float=1,float=1,float=0,USoundConcurrency* =nullptr,const AActor* =nullptr,bool=true);
HELIOSOUTPOST_API UAudioComponent* SpawnSoundAtLocation(const UObject*,USoundBase*,FVector,FRotator=FRotator::ZeroRotator,float=1,float=1,float=0,USoundAttenuation* =nullptr,USoundConcurrency* =nullptr,bool=true,bool WorldOwned=false);
HELIOSOUTPOST_API void PlaySoundAtLocation(const UObject*,USoundBase*,FVector,float=1,float=1,float=0,USoundAttenuation* =nullptr,USoundConcurrency* =nullptr,const AActor* =nullptr);
HELIOSOUTPOST_API UAudioComponent* SpawnSoundAttached(USoundBase*,USceneComponent*,FName=NAME_None,FVector=FVector::ZeroVector,EAttachLocation::Type=EAttachLocation::KeepRelativeOffset,bool=false,float=1,float=1,float=0,USoundAttenuation* =nullptr,USoundConcurrency* =nullptr,bool=true);
HELIOSOUTPOST_API UAudioComponent* SpawnSoundAttached(USoundBase*,USceneComponent*,FName,FVector,FRotator,EAttachLocation::Type,bool,float=1,float=1,float=0,USoundAttenuation* =nullptr,USoundConcurrency* =nullptr,bool=true);
}
