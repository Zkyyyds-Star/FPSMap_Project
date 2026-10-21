#pragma once

#include "CoreMinimal.h"
#include "Core/HeliosGameMode.h"
#include "Bio/HeliosBioRules.h"
#include "Player/HeliosLoadout.h"
#include "HeliosBioHotelGameMode.generated.h"

class AHeliosBioPlayer;
class AHeliosBioNPC;
class AHeliosBioSupply;
class AHeliosBioClone;
class AHeliosBioMovingPlatform;
class UHeliosBioRoleDefinition;
class AHeliosBioTurret;
struct FHeliosBioHunterOption
{FName Id;FString Name;FString Description;bool bAvailable=false;};

/** Owns a separate offline match; no adventure director or persistent progress. */
UCLASS()
class HELIOSOUTPOST_API AHeliosBioHotelGameMode : public AHeliosGameMode
{
    GENERATED_BODY()
public:
    AHeliosBioHotelGameMode();
    virtual void StartPlay() override;
    virtual void Tick(float Dt) override;
    static AHeliosBioHotelGameMode* Find(UWorld* World);
    FHeliosBioRules Model;
    UPROPERTY(Transient) TObjectPtr<AHeliosBioPlayer> Player;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosBioNPC>> Bots;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosBioSupply>> Supplies;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosBioClone>> Clones;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosBioMovingPlatform>> MovingPlatforms;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosBioTurret>> Turrets;
    bool bHunterChooserOpen=false;
    FHeliosLoadout SelectedLoadout;
    int32 RoundSerial = 0;
    int32 BaseRoundSeed = 1337, CurrentRoundSeed = 1337;
    bool bFloatingProfile = false;
    int32 HumanWins = 0, ZombieWins = 0;
    int32 TotalInfections = 0, TotalDamageEvents = 0, TotalSupplies = 0;
    FString Notice;
    float NoticeSeconds = 0.f;
    void RestartBioRound();
    bool DamageParticipant(int32 VictimId,int32 AttackerId,float Damage,bool HunterMelee=false);
    bool TransformParticipant(int32 Id);
    bool PickupSupply(int32 Id,AHeliosBioSupply* Supply);
    bool UseParticipantSkill(int32 Id);
    bool IsParticipantSkillImplemented(int32 Id) const;
    FString ParticipantSkillDescription(int32 Id) const;
    bool UseParticipantSecondarySkill(int32 Id);
    bool GetParticipantSerumMuzzle(int32 Id,FVector& OutWorldPosition) const;
    bool FireParticipantSerum(int32 Id,const FVector& ActualMuzzle,const FVector& Direction);
    bool FireParticipantRoleWeapon(int32 Id,const FVector& Start,const FVector& Direction);
    bool FireParticipantSwordWave(int32 Id,const FVector& Start,const FVector& Direction,bool Heavy);
    TArray<FHeliosBioHunterOption> HunterOptions() const;
    bool SelectHunter(int32 Id,FName SelectedRole);
    void OpenHunterChooser();
    void CloseHunterChooser();
    // Standalone values where no source numeric config was verified.
    UPROPERTY(EditAnywhere) float UltimateSkillRadius = 600.f;
    UPROPERTY(EditAnywhere) float UltimateSkillDamage = 1800.f;
    UPROPERTY(EditAnywhere) float UltimateSkillKnockback = 700.f;
    UPROPERTY(EditAnywhere) float CloneLifetime = 10.f;
    int32 TotalAreaSkillHits = 0, TotalCloneSpawns = 0;
    int32 TotalSerumShots=0,TotalSerumCures=0,TotalSerumHeals=0,TotalSerumRejected=0,TotalSerumBlocked=0,TotalSerumCancelled=0;
    FString LastSerumDiagnostic;
    AActor* ParticipantActor(int32 Id) const;
    int32 ParticipantId(const AActor* Actor) const;
    bool AreOpponents(int32 A,int32 B) const;
    FVector SpawnLocation(int32 Id) const;
    FRotator SpawnRotation(int32 Id) const;
    void ShowNotice(const FString& Text,float Seconds=3.f);
    void PauseBio();
    void ReturnToMapMenu();
private:
    friend class AHeliosBioIterationQA;
    TArray<FVector> SpawnPoints,SupplyPoints;
    TArray<FRotator> SpawnRotations;
    int32 LastSupplyWave = -1;
    TArray<int32> SupplyWeaponBag;
    int32 SupplyWeaponBagCursor=0,SupplyWeaponBagCycle=0;
    int32 DrawSupplyWeaponSlot();
    int32 LastCountdown = -1;
    EHeliosBioPhase PreviousPhase = EHeliosBioPhase::RoundOver;
    float RoundOverAge = 0;
    void RefreshSupplyWave();
    void LoadSourcePoints();
    void PerformUltimateStrike(int32 Id,int32 ExpectedRound);
    void LoadOriginalMovingPlatforms();
    void RefreshHunterPool();
    void LoadRuntimeProfile();
    void LoadOriginalStaticJumpLinks();
    void PerformRoleSkill(int32 Id,int32 ExpectedRound,UHeliosBioRoleDefinition* Art);
    TArray<FName> ProfileHunterRoles;
    TMap<int32,double> NextRoleWeaponShot;
};
