#include "World/HeliosMapTravelSubsystem.h"
#include "World/HeliosMapDefinition.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "Player/HeliosArenaCharacter.h"
#include "Town/HeliosTownSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Engine/World.h"

bool UHeliosMapTravelSubsystem::RequestTravel(FName MapId, FName WeaponId)
{
    FHeliosLoadout Loadout=PendingLoadout;
    if(UWorld* World=GetWorld())
    {
        if(const auto* Player=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(World,0)))Loadout=Player->GetLoadout();
        else if(const auto* Director=AHeliosAdventureDirector::Find(World))Loadout=Director->GetSelectedLoadout();
    }
    Loadout.PrimaryWeaponId=WeaponId;
    return RequestTravelWithLoadout(MapId,Loadout);
}

bool UHeliosMapTravelSubsystem::RequestTravelWithLoadout(FName MapId, const FHeliosLoadout& Loadout)
{
    FString Error;
    if (!FHeliosLoadout::Validate(Loadout,Error) || Loadout.CharacterId==TEXT("sword_dog")) return false;
    const auto* Definition=FHeliosMapRegistry::Find(MapId);
    if(!Definition || !GetWorld())return false;
    const FString Package=Definition->Level.ToSoftObjectPath().GetLongPackageName();
    if(!FPackageName::DoesPackageExist(Package)) return false;
    if(auto* Town = GetWorld()->GetSubsystem<UHeliosTownSubsystem>())
        if(Town->IsTownWorld()) Town->SaveProgress();
    PendingLoadout = FHeliosLoadout::ResolveLegacyVariant(Loadout,GetGameInstance());
    PendingWeaponId = PendingLoadout.PrimaryWeaponId;
    DestinationMap = Definition->AssetMapName();
    bPendingStart = true;
    UGameplayStatics::SetGamePaused(this, false);
    UGameplayStatics::OpenLevel(this, FName(*Package));
    return true;
}
