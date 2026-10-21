#pragma once

#include "CoreMinimal.h"

// Standalone tuning, not a claim about the live mobile game's numeric balance.
enum class EHeliosBioTeam : uint8 { Human, Zombie, GhostHunter, UltimateHunter };
enum class EHeliosBioPhase : uint8 { Preparation, Active, RoundOver };
enum class EHeliosBioWinner : uint8 { None, Humans, Zombies };
enum class EHeliosBioSupplyKind : uint8 { Normal, BlueHunterUpgrade, RedTerminatorEvolution };
enum class EHeliosBioZombieForm : uint8 { Normal, Terminator, Abyss };
enum class EHeliosBioSerumResult : uint8 { Rejected, CuredZombie, HealedHunter };

/** Optional map-owned view configuration; never changes NPC combat eye height. */
struct FHeliosBioPlayerCameraProfile
{
    bool bEnabled = false; // Other maps retain their existing camera path.
    float StandingEyeHeight = 90.f; // From capsule bottom, not the authored Pawn root.
    float CrouchedEyeHeight = 60.f;
    float CrouchedCapsuleHalfHeight = 40.f;
    FVector2D HorizontalOffset = FVector2D::ZeroVector;
    float WorldVerticalFOV = 70.f;
    float FirstPersonVerticalFOV = 33.f;
    float FirstPersonScale = 1.f;
};

struct FHeliosBioConfig
{
    float PreparationSeconds = 20.f;
    float RoundSeconds = 180.f;
    int32 ParticipantCount = 32;
    int32 InitialMotherCount = 2;
    bool bKeepLocalPlayerHuman = true; // Optional offline opening preference; map defaults to inclusive random mothers.
    int32 HunterThreshold = 2;
    bool bUseCurrentVersionRoles = false; // Optional Wukong/WhiteBone profile, never a substitute for blue-box legacy Ultimate.
    float HumanHealth = 100.f;
    float ZombieHealth = 2500.f;
    float MotherHealth = 4000.f;
    float GhostHunterHealth = 4000.f;
    float UltimateHunterHealth = 8000.f;
    float RespawnSeconds = 5.f;
    float SupplyIntervalSeconds = 30.f;
    float SupplyHeal = 50.f;
    int32 BlueSuppliesRequired = 1; // Configurable offline approximation; ordinary supplies never advance this count.
    bool bRequireHunterSelection = true;
    TArray<FName> AllowedHunterRoles; // Integration supplies only roles with verified, available source meshes; no implicit fallback.
    bool bZombieNormalSupplyEnabled = true; // Offline adaptation of normal-supply evolution; exact counts are unverified.
    int32 ZombieSuppliesPerEvolution = 2;
    int32 MaximumZombieEvolution = 2;
    bool bMothersAreTerminators = false; // Integration must explicitly identify the source role; mother alone is not proof.
    bool bAbyssSourceAvailable = false;
    FName AbyssRoleId = NAME_None; // Must identify an available real Abyss mesh; never reuse the Terminator as a substitute.
    float AbyssHealth = 8000.f; // Offline tuning, not a verified original value.
    int32 InfectionsPerEvolution = 2;
    float EvolutionHealthBonus = 500.f;
    float SkillDurationSeconds = 4.f;
    float SkillCooldownSeconds = 15.f;
    float HunterSkillDamageMultiplier = 1.75f;
    int32 NormalSupplyCount = 3;
    int32 BlueSupplyCount = 2;
    int32 RedSupplyCount = 0; // Floating profile explicitly enables the original red box.
    float HumanDamageMultiplier = 6.f;
    float ZombieMeleeDamage = 200.f; // Explicit offline damage; humans still use touch infection.
    float MovementCapsuleRadius = 34.f;
    float MovementCapsuleHalfHeight = 96.f;
    float MovementStepHeight = 40.f;
    float MovementSlopeDegrees = 45.f;
    float MovementJumpZ = 0.f; // Zero preserves the installed character's value.
    float EyeHeight = 160.f; // Independent NPC combat/legacy-player eye; map PlayerCamera never overwrites it.
    FHeliosBioPlayerCameraProfile PlayerCamera;
};

struct FHeliosBioParticipant
{
    int32 Id = INDEX_NONE;
    EHeliosBioTeam Team = EHeliosBioTeam::Human;
    float Health = 100.f;
    float MaxHealth = 100.f;
    bool bAlive = true;
    bool bPermanentlyDead = false;
    bool bMother = false;
    bool bUseModernAvatar = false;
    float RespawnSeconds = 0.f;
    int32 Infections = 0;
    int32 EvolutionInfections = 0; // Current evolution progress; historical infection score survives Arcane devolution.
    int32 EvolutionLevel = 0;
    int32 SuppliesCollected = 0;
    int32 BlueSuppliesCollected = 0;
    FName HunterRoleId = NAME_None;
    bool bHunterChoicePending = false;
    EHeliosBioZombieForm ZombieForm = EHeliosBioZombieForm::Normal;
    FName ZombieRoleId = NAME_None;
    int32 ZombieSuppliesCollected = 0;
    float SkillRemaining = 0.f;
    float SkillCooldownRemaining = 0.f;
    float FrozenSeconds = 0.f;
    float ShieldRemaining = 0.f;
    float ShieldReduction = 0.f;
    float SkillSpeedMultiplier = 1.f;
    float SkillDamageMultiplier = 1.f;
    float SecondarySkillRemaining = 0.f, SecondaryCooldownRemaining = 0.f;
    float SecondarySkillDuration = 0.f, SecondarySpeedMultiplier = 1.f, SecondaryDamageMultiplier = 1.f;
    float SecondarySpeedRemaining = 0.f; // Initial speed boost expires independently of extended ghost-mask lifetime.
    bool bGhostMaskActive = false, bFirstAttackSwordWave = false, bHeavySwordWaveReady = false;
    float WeaponEnergy = 100.f, WeaponMaxEnergy = 100.f, WeaponRechargeDelay = 0.f, WeaponRechargeRate = 20.f;
    int32 Kills = 0, Deaths = 0;
    float DamageDealt = 0.f;
    bool IsHumanSide() const { return Team != EHeliosBioTeam::Zombie; }
    bool IsHunter() const { return Team == EHeliosBioTeam::GhostHunter || Team == EHeliosBioTeam::UltimateHunter; }
    float MovementSkillMultiplier() const
    {return SkillSpeedMultiplier*(bGhostMaskActive&&SecondarySkillRemaining>0?1.f+(SecondarySpeedMultiplier-1.f)*FMath::Clamp(SecondarySpeedRemaining/FMath::Max(.01f,SecondarySkillDuration),0.f,1.f):1.f);}
};

// No world/actor ownership: integration polls participants for team, HP and respawn changes.
class HELIOSOUTPOST_API FHeliosBioRules
{
public:
    FHeliosBioConfig Config;
    void StartRound(int32 Count = 0, int32 Seed = 1337);
    void Tick(float DeltaSeconds);
    bool ApplyDamage(int32 VictimId, int32 AttackerId, float Damage, bool bHunterMelee = false);
    bool Infect(int32 VictimId, int32 AttackerId);
    bool CanTransform(int32 Id) const;
    bool Transform(int32 Id);
    bool TransformUltimate(int32 Id);
    bool PickupSupply(int32 Id, EHeliosBioSupplyKind Kind = EHeliosBioSupplyKind::Normal);
    bool ConfirmHunterChoice(int32 Id, FName RoleId);
    bool CancelHunterChoice(int32 Id); // Close the chooser while preserving eligibility to reopen it.
    bool ActivateSkill(int32 Id, float Duration = -1.f, float Cooldown = -1.f,
        float SpeedMultiplier = -1.f, float DamageMultiplier = -1.f,
        float ShieldReduction = 0.f);
    bool FreezeParticipant(int32 VictimId, int32 AttackerId, float Seconds);
    bool ActivateSecondarySkill(int32 Id,float Duration,float Cooldown,float SpeedMultiplier=1.f,float DamageMultiplier=1.f,bool GhostMask=false);
    bool ConsumeSwordWave(int32 Id,bool Heavy);
    bool DevolveDefeatedZombie(int32 Id);
    EHeliosBioSerumResult ApplyRescueSerum(int32 TargetId,int32 CasterId);
    bool ConsumeRoleWeaponEnergy(int32 Id,float Cost,float RechargeDelay,float RechargeRate,float Capacity);
    const FHeliosBioParticipant* FindParticipant(int32 Id) const;
    const TArray<FHeliosBioParticipant>& GetParticipants() const { return Participants; }
    EHeliosBioPhase GetPhase() const { return Phase; }
    EHeliosBioWinner GetWinner() const { return Winner; }
    float GetPhaseTimeRemaining() const { return PhaseTimeRemaining; }
    int32 GetSupplyWave() const { return SupplyWave; }
    int32 CountLivingHumanSide() const;
private:
    TArray<FHeliosBioParticipant> Participants;
    EHeliosBioPhase Phase = EHeliosBioPhase::RoundOver;
    EHeliosBioWinner Winner = EHeliosBioWinner::None;
    float PhaseTimeRemaining = 0.f;
    float SupplyElapsed = 0.f;
    int32 SupplyWave = 0;
    int32 RoundSeed = 1337;
    FHeliosBioParticipant* FindMutable(int32 Id);
    void BeginInfection();
    void CheckWinner();
    void SetTeam(FHeliosBioParticipant& Participant, EHeliosBioTeam Team, float Health);
    void RefreshZombieEvolution(FHeliosBioParticipant& Participant);
};
