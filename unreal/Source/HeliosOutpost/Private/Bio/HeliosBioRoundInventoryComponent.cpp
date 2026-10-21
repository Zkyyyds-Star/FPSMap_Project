#include "Bio/HeliosBioRoundInventoryComponent.h"
#include "Bio/HeliosBioSupplyWeaponDefinition.h"
#include "Bio/HeliosBioHotelGameMode.h"
#include "Bio/HeliosBioPlayer.h"
#include "Bio/HeliosBioNPC.h"
#include "Bio/HeliosBioFourSlashComponent.h"
#include "Player/HeliosCharacterDefinition.h"
#include "Player/HeliosCharacterActionProfile.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Weapons/HeliosCombatWeapons.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "ShooterWeapon.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

UHeliosBioRoundInventoryComponent::UHeliosBioRoundInventoryComponent()
{PrimaryComponentTick.bCanEverTick=false;Slots.SetNum(5);}
bool UHeliosBioRoundInventoryComponent::IsEnabled() const
{const auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());return Mode&&Mode->bFloatingProfile;}
bool UHeliosBioRoundInventoryComponent::CanOperate() const
{
    auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());if(!Mode||!IsEnabled()||GetWorld()->IsPaused()||Mode->bHunterChooserOpen||Mode->Model.GetPhase()==EHeliosBioPhase::RoundOver)return false;
    const auto* P=Mode->Model.FindParticipant(Mode->ParticipantId(GetOwner()));
    if(!P||!P->bAlive||P->Team!=EHeliosBioTeam::Human||P->FrozenSeconds>0)return false;
    if(const auto* Player=Cast<AHeliosBioPlayer>(GetOwner()))return !Player->GetFourSlash()->IsBusy();
    if(const auto* NPC=Cast<AHeliosBioNPC>(GetOwner()))return !NPC->IsHumanInfectionTransition()&&!NPC->GetFourSlash()->IsBusy();
    return false;
}
void UHeliosBioRoundInventoryComponent::RegisterBaseline(AShooterWeapon* Weapon,const FHeliosLoadout& Loadout,int32 Reserve,int32 Magazine,float Reload)
{
    if(!IsEnabled()||!IsValid(Weapon))return;
    auto& Baseline=Slots[0];Baseline.Weapon=Weapon;Baseline.Loadout=Loadout;Baseline.ReserveAmmo=FMath::Max(0,Reserve);
    Baseline.MagazineSize=Magazine;Baseline.ReloadSeconds=Reload;ActiveSlot=0;
}
bool UHeliosBioRoundInventoryComponent::HasSlot(int32 Slot) const
{return Slots.IsValidIndex(Slot)&&IsValid(Slots[Slot].Weapon);}
AShooterWeapon* UHeliosBioRoundInventoryComponent::GetWeapon(int32 Slot) const
{return HasSlot(Slot)?Slots[Slot].Weapon.Get():nullptr;}
FName UHeliosBioRoundInventoryComponent::GetWeaponId(int32 Slot) const
{return HasSlot(Slot)?Slots[Slot].Loadout.PrimaryWeaponId:NAME_None;}
int32 UHeliosBioRoundInventoryComponent::GetReserve(int32 Slot) const
{
    if(!HasSlot(Slot))return 0;
    if(Slot==ActiveSlot){if(const auto* P=Cast<AHeliosBioPlayer>(GetOwner()))return P->ReserveAmmo;if(const auto* N=Cast<AHeliosBioNPC>(GetOwner()))return N->ReserveAmmo;}
    return Slots[Slot].ReserveAmmo;
}
void UHeliosBioRoundInventoryComponent::CaptureActive()
{
    if(!HasSlot(ActiveSlot))return;
    if(const auto* P=Cast<AHeliosBioPlayer>(GetOwner())){if(P->Weapon==Slots[ActiveSlot].Weapon)Slots[ActiveSlot].ReserveAmmo=P->ReserveAmmo;}
    else if(const auto* N=Cast<AHeliosBioNPC>(GetOwner())){if(N->Weapon==Slots[ActiveSlot].Weapon)Slots[ActiveSlot].ReserveAmmo=N->ReserveAmmo;}
}
bool UHeliosBioRoundInventoryComponent::Preflight(const FHeliosLoadout& Loadout,bool Throwable,FString* Error,const UHeliosBioSupplyWeaponDefinition* SupplyDefinition) const
{
    const auto Reject=[&](const TCHAR* Why){if(Error)*Error=Why;return false;};
    const auto* Character=FHeliosCharacterRegistry::FindCharacter(Loadout.CharacterId);
    const auto* Form=Character?Character->FindForm(Loadout.CharacterVariantId):nullptr;
    auto* Profile=Form?Form->ActionProfile.LoadSynchronous():nullptr;
    if(!Profile||Profile->CharacterId!=Loadout.CharacterId||Profile->CharacterVariantId!=Loadout.CharacterVariantId)return Reject(TEXT("character_profile_unavailable"));
    const auto* Pair=Profile->FindWeapon(Loadout.PrimaryWeaponId);
    if(!Pair||!Pair->bGeneratedAndValidated)return Reject(TEXT("source_weapon_character_pair_unverified"));
    // Existing human weapons use their original clip names on the FP driver.
    // Keep that proven baseline contract; new BIO loot pairs expose canonical
    // action aliases in both views and pass the stricter acquisition gate below.
    const bool SupplyWeapon=Loadout.PrimaryWeaponId.ToString().StartsWith(TEXT("cfm_bio_supply_"));
    for(int32 View=0;View<2;++View)
    {
        auto* Mesh=(View?Pair->ThirdPersonMesh:Pair->FirstPersonMesh).LoadSynchronous();
        if(!Mesh||!Mesh->GetSkeleton())return Reject(TEXT("source_pair_mesh_missing"));
        const auto& Clips=View?Pair->ThirdPersonClips:Pair->FirstPersonClips;
        TArray<FName> Required;
        if(!SupplyWeapon){if(View)Required.Add(TEXT("Idle"));}
        else Required={TEXT("Idle"),TEXT("Move"),TEXT("Fire"),TEXT("Equip")};
        if(SupplyWeapon&&View&&SupplyDefinition&&SupplyDefinition->ThirdPersonHoldIdleActions.Contains(TEXT("Equip")))Required.Remove(TEXT("Equip"));
        if(SupplyWeapon){if(!Throwable)Required.Add(TEXT("ChangeClip"));if(View)Required.Add(TEXT("Down"));}
        for(FName Action:Required)
        {
            const auto* Ref=Clips.Find(Action);auto* Clip=Ref?Ref->LoadSynchronous():nullptr;
            if(!Clip||Clip->GetSkeleton()!=Mesh->GetSkeleton()||!FMath::IsFinite(Clip->GetPlayLength())||Clip->GetPlayLength()<=0)return Reject(TEXT("native_pair_action_missing_or_incompatible"));
        }
    }
    return true;
}
bool UHeliosBioRoundInventoryComponent::CanGrant(const UHeliosBioSupplyWeaponDefinition* Definition,FString* Error) const
{
    if(!CanOperate()||!Definition||!Definition->IsReady()||!HasSlot(0)){if(Error)*Error=TEXT("source_or_gameplay_gate_closed");return false;}
    FHeliosLoadout Target=Slots[0].Loadout;Target.PrimaryWeaponId=Definition->WeaponId;
    return Preflight(Target,Definition->bThrowable,Error,Definition);
}
bool UHeliosBioRoundInventoryComponent::CommitSlot(int32 Slot)
{
    auto& Entry=Slots[Slot];auto* Next=Entry.Weapon.Get();if(!IsValid(Next))return false;
    // Configure validates both rigs before changing state; Preflight additionally
    // validates every required native clip. Old instances remain available for rollback.
    auto* Player=Cast<AHeliosBioPlayer>(GetOwner());auto* NPC=Cast<AHeliosBioNPC>(GetOwner());
    auto* Presentation=Player?Player->HumanPresentation.Get():NPC?NPC->HumanPresentation.Get():nullptr;
    if(!Presentation)return false;
    const int32 Previous=ActiveSlot;
    CaptureActive();
    if(Player)Player->StopBioFire();else if(NPC->Weapon){NPC->Weapon->StopFiring();NPC->Weapon->StopSecondaryFire();}
    const bool Applied=Player?Presentation->ApplyLoadout(Player->FirstPersonCamera,Entry.Loadout,Next):Presentation->ApplyLoadout(NPC->GetMesh(),Entry.Loadout);
    if(!Applied)
    {
        if(HasSlot(Previous))
        {const auto& Old=Slots[Previous];if(Player)Presentation->ApplyLoadout(Player->FirstPersonCamera,Old.Loadout,Old.Weapon);else Presentation->ApplyLoadout(NPC->GetMesh(),Old.Loadout);}
        return false;
    }
    for(auto& Other:Slots)if(IsValid(Other.Weapon)&&Other.Weapon!=Next){Other.Weapon->CancelReload();Other.Weapon->DeactivateWeapon();}
    Next->CancelReload();
    if(Player)
    {
        Player->ReloadRemaining=0;Player->bReloadCommit=false;Player->Loadout=Entry.Loadout;
        Player->ReserveAmmo=Entry.ReserveAmmo;Player->MagazineSize=Entry.MagazineSize;Player->ReloadDuration=Entry.ReloadSeconds;
        Player->LastAmmo=Next->GetBulletCount();Player->Weapon=Next;Next->ActivateWeapon(TEXT("Player"));
    }
    else
    {
        NPC->ReloadRemaining=0;NPC->bReloadCommit=false;NPC->HumanLoadout=Entry.Loadout;
        NPC->ReserveAmmo=Entry.ReserveAmmo;NPC->MagazineSize=Entry.MagazineSize;NPC->ReloadDuration=Entry.ReloadSeconds;
        NPC->LastAmmo=Next->GetBulletCount();NPC->Weapon=Next;Next->ActivateWeapon(TEXT("Friendly"));
        NPC->HumanPresentation->SetComponentTickEnabled(true);NPC->RestoreHumanMeshTransform();
    }
    ActiveSlot=Slot;Presentation->SetVisible(true);
    UE_LOG(LogTemp,Display,TEXT("BIO_INVENTORY_SELECT participant=%d slot=%d weapon=%s clip=%d reserve=%d"),
        AHeliosBioHotelGameMode::Find(GetWorld())->ParticipantId(GetOwner()),Slot,*Entry.Loadout.PrimaryWeaponId.ToString(),Next->GetBulletCount(),Entry.ReserveAmmo);
    return true;
}
bool UHeliosBioRoundInventoryComponent::Select(int32 Slot,bool Force)
{
    if(!CanOperate()||!HasSlot(Slot))return false;
    if(Slot==ActiveSlot&&!Force)return true;
    const auto& Entry=Slots[Slot];
    if(!Preflight(Entry.Loadout,Entry.Definition&&Entry.Definition->bThrowable,nullptr,Entry.Definition))return false;
    return CommitSlot(Slot);
}
bool UHeliosBioRoundInventoryComponent::Cycle(int32 Direction)
{
    if(Direction==0||!CanOperate())return false;
    for(int32 Offset=1;Offset<5;++Offset)
    {const int32 Next=(ActiveSlot+(Direction>0?Offset:5-Offset))%5;if(HasSlot(Next))return Select(Next);}
    return false;
}
bool UHeliosBioRoundInventoryComponent::TryGrant(UHeliosBioSupplyWeaponDefinition* Definition)
{
    FString Error;if(!CanGrant(Definition,&Error))
    {UE_LOG(LogTemp,Warning,TEXT("BIO_INVENTORY_GRANT_REJECTED item=%d reason=%s"),Definition?Definition->OriginalItemId:0,*Error);return false;}
    const int32 Slot=UHeliosBioSupplyWeaponDefinition::SlotForItem(Definition->OriginalItemId);
    CaptureActive();
    if(HasSlot(Slot))
    {
        auto& Existing=Slots[Slot];Existing.ReserveAmmo=FMath::Min(Definition->MaximumReserveAmmo,Existing.ReserveAmmo+Definition->PickupReserveAmmo);
        if(Slot==ActiveSlot){if(auto* P=Cast<AHeliosBioPlayer>(GetOwner()))P->ReserveAmmo=Existing.ReserveAmmo;else if(auto* N=Cast<AHeliosBioNPC>(GetOwner()))N->ReserveAmmo=Existing.ReserveAmmo;}
        return true;
    }
    FHeliosBioRoundWeaponSlot Prepared;Prepared.Definition=Definition;Prepared.Loadout=Slots[0].Loadout;Prepared.Loadout.PrimaryWeaponId=Definition->WeaponId;
    Prepared.ReserveAmmo=Definition->PickupReserveAmmo;Prepared.MagazineSize=Definition->MagazineSize;Prepared.ReloadSeconds=Definition->ReloadSeconds;
    auto* Player=Cast<AHeliosBioPlayer>(GetOwner());auto* NPC=Cast<AHeliosBioNPC>(GetOwner());auto* Pawn=Cast<APawn>(GetOwner());
    auto* Next=GetWorld()->SpawnActorDeferred<AShooterWeapon>(Definition->WeaponClass,GetOwner()->GetActorTransform(),GetOwner(),Pawn,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Next)return false;
    if(auto* Combat=Cast<AHeliosCombatWeapon>(Next))Combat->ApplyDefinition(Definition);
    // ShooterWeapon::BeginPlay calls its owner. Preparing an unequipped instance
    // must not rebind the current visible presentation or mutate owner ammo.
    if(Player)Player->bPreparingInventoryWeapon=true;else NPC->bPreparingInventoryWeapon=true;
    UGameplayStatics::FinishSpawningActor(Next,GetOwner()->GetActorTransform());
    if(IsValid(Next))Next->ConfigureMagazine(Prepared.MagazineSize);
    if(Player)Player->bPreparingInventoryWeapon=false;else NPC->bPreparingInventoryWeapon=false;
    if(!IsValid(Next))return false;
    Next->SetActorHiddenInGame(true);Prepared.Weapon=Next;
    Slots[Slot]=Prepared;
    if(!CommitSlot(Slot)){Slots[Slot]=FHeliosBioRoundWeaponSlot{};Next->Destroy();return false;}
    return true;
}
void UHeliosBioRoundInventoryComponent::Suspend()
{
    if(!IsEnabled())return;CaptureActive();
    for(auto& Entry:Slots)if(IsValid(Entry.Weapon)){Entry.Weapon->StopSecondaryFire();Entry.Weapon->CancelReload();Entry.Weapon->DeactivateWeapon();}
}
bool UHeliosBioRoundInventoryComponent::Restore()
{return Select(ActiveSlot,true);}
void UHeliosBioRoundInventoryComponent::ResetRound(bool DestroyBaseline)
{
    const bool OwnedBaseline=HasSlot(0);
    CaptureActive();
    for(int32 Slot=DestroyBaseline?0:1;Slot<Slots.Num();++Slot)
    {if(IsValid(Slots[Slot].Weapon))Slots[Slot].Weapon->Destroy();Slots[Slot]=FHeliosBioRoundWeaponSlot{};}
    ActiveSlot=0;
    if(DestroyBaseline)
    {
        if(OwnedBaseline){if(auto* P=Cast<AHeliosBioPlayer>(GetOwner()))P->Weapon=nullptr;else if(auto* N=Cast<AHeliosBioNPC>(GetOwner()))N->Weapon=nullptr;}
        return;
    }
    if(HasSlot(0))
    {
        Slots[0].ReserveAmmo=600;
        if(auto* P=Cast<AHeliosBioPlayer>(GetOwner())){P->ReserveAmmo=600;P->bPreparingInventoryWeapon=true;Slots[0].Weapon->ConfigureMagazine(Slots[0].MagazineSize);P->bPreparingInventoryWeapon=false;}
        else if(auto* N=Cast<AHeliosBioNPC>(GetOwner())){N->ReserveAmmo=600;N->bPreparingInventoryWeapon=true;Slots[0].Weapon->ConfigureMagazine(Slots[0].MagazineSize);N->bPreparingInventoryWeapon=false;}
        CommitSlot(0);
    }
}
void UHeliosBioRoundInventoryComponent::AddBaselineAmmo(int32 Amount)
{
    if(!HasSlot(0))return;CaptureActive();Slots[0].ReserveAmmo=FMath::Min(1800,Slots[0].ReserveAmmo+FMath::Max(0,Amount));
    if(ActiveSlot==0){if(auto* P=Cast<AHeliosBioPlayer>(GetOwner()))P->ReserveAmmo=Slots[0].ReserveAmmo;else if(auto* N=Cast<AHeliosBioNPC>(GetOwner()))N->ReserveAmmo=Slots[0].ReserveAmmo;}
}
FString UHeliosBioRoundInventoryComponent::Describe() const
{
    FString Text;
    for(int32 Slot=0;Slot<5;++Slot)if(HasSlot(Slot))
    {
        if(!Text.IsEmpty())Text+=TEXT("  ·  ");
        Text+=FString::Printf(TEXT("%s%d %s %d/%d%s"),Slot==ActiveSlot?TEXT("["):TEXT(""),Slot+1,*UHeliosBioSupplyWeaponDefinition::LabelForSlot(Slot),Slots[Slot].Weapon->GetBulletCount(),GetReserve(Slot),Slot==ActiveSlot?TEXT("]"):TEXT(""));
    }
    return Text;
}
void UHeliosBioRoundInventoryComponent::EndPlay(const EEndPlayReason::Type Reason)
{ResetRound(true);Super::EndPlay(Reason);}
