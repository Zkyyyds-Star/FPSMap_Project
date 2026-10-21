#include "Bio/HeliosBioRules.h"

const FHeliosBioParticipant* FHeliosBioRules::FindParticipant(int32 Id) const
{
    return Participants.IsValidIndex(Id) ? &Participants[Id] : nullptr;
}

EHeliosBioSerumResult FHeliosBioRules::ApplyRescueSerum(int32 TargetId,int32 CasterId)
{
    const auto* Caster=FindParticipant(CasterId);auto* Target=FindMutable(TargetId);
    if(Phase!=EHeliosBioPhase::Active||!Caster||!Caster->bAlive||Caster->FrozenSeconds>0||
        Caster->Team!=EHeliosBioTeam::UltimateHunter||Caster->HunterRoleId!=TEXT("BioRedeemerHunter")||
        !Target||!Target->bAlive||Target->bPermanentlyDead||TargetId==CasterId)return EHeliosBioSerumResult::Rejected;
    if(Target->Team==EHeliosBioTeam::Zombie)
    {
        // Team/presentation state is reset; historical score and the actor's
        // independent human loadout survive. Integration never relocates a cure.
        SetTeam(*Target,EHeliosBioTeam::Human,Config.HumanHealth);Target->bMother=false;
        return EHeliosBioSerumResult::CuredZombie;
    }
    if(Target->IsHunter())
    {
        Target->Health=Target->MaxHealth;
        return EHeliosBioSerumResult::HealedHunter;
    }
    return EHeliosBioSerumResult::Rejected;
}
FHeliosBioParticipant* FHeliosBioRules::FindMutable(int32 Id)
{
    return Participants.IsValidIndex(Id) ? &Participants[Id] : nullptr;
}
void FHeliosBioRules::SetTeam(FHeliosBioParticipant& P, EHeliosBioTeam Team, float Health)
{
    P.Team = Team;
    P.MaxHealth = FMath::Max(1.f, Health);
    P.Health = P.MaxHealth;
    P.bAlive = true;
    P.bPermanentlyDead = false;
    P.RespawnSeconds = 0.f;
    P.SkillRemaining = P.SkillCooldownRemaining = 0.f;
    P.FrozenSeconds = P.ShieldRemaining = P.ShieldReduction = 0.f;
    P.SkillSpeedMultiplier = P.SkillDamageMultiplier = 1.f;
    P.SecondarySkillRemaining = P.SecondaryCooldownRemaining = P.SecondarySkillDuration = 0.f;
    P.SecondarySpeedRemaining = 0.f;
    P.SecondarySpeedMultiplier = P.SecondaryDamageMultiplier = 1.f;
    P.bGhostMaskActive = P.bFirstAttackSwordWave = P.bHeavySwordWaveReady = false;
    P.WeaponEnergy=P.WeaponMaxEnergy=100.f;P.WeaponRechargeDelay=0.f;P.WeaponRechargeRate=20.f;
    P.HunterRoleId = NAME_None;
    P.bHunterChoicePending = false;
    P.ZombieForm = EHeliosBioZombieForm::Normal;
    P.ZombieRoleId = NAME_None;
    P.EvolutionLevel=P.EvolutionInfections=P.ZombieSuppliesCollected=0;
}
void FHeliosBioRules::StartRound(int32 Count, int32 Seed)
{
    Participants.Reset();
    const int32 Total = FMath::Clamp(Count > 0 ? Count : Config.ParticipantCount, 2, 64);
    for (int32 Id = 0; Id < Total; ++Id)
    {
        FHeliosBioParticipant P;
        P.Id = Id;
        P.bUseModernAvatar = Config.bUseCurrentVersionRoles;
        SetTeam(P, EHeliosBioTeam::Human, Config.HumanHealth);
        Participants.Add(P);
    }
    RoundSeed = Seed;
    Phase = EHeliosBioPhase::Preparation;
    Winner = EHeliosBioWinner::None;
    PhaseTimeRemaining = FMath::Max(0.f, Config.PreparationSeconds);
    SupplyElapsed = 0.f;
    SupplyWave = 0;
    if (PhaseTimeRemaining <= 0.f) BeginInfection();
}
void FHeliosBioRules::BeginInfection()
{
    Phase = EHeliosBioPhase::Active;
    PhaseTimeRemaining = FMath::Max(0.f, Config.RoundSeconds);
    TArray<int32> Candidates;
    // Optional tutorial/QA opening excludes local player; normal map includes all participants.
    for (int32 Id = Config.bKeepLocalPlayerHuman ? 1 : 0; Id < Participants.Num(); ++Id) Candidates.Add(Id);
    FRandomStream Random(RoundSeed);
    const int32 Mothers = FMath::Clamp(Config.InitialMotherCount, 1, Candidates.Num());
    for (int32 N = 0; N < Mothers; ++N)
    {
        const int32 Pick = Random.RandRange(0, Candidates.Num() - 1);
        FHeliosBioParticipant& P = Participants[Candidates[Pick]];
        SetTeam(P, EHeliosBioTeam::Zombie, Config.MotherHealth);
        P.bMother = true;
        if (Config.bMothersAreTerminators) P.ZombieForm = EHeliosBioZombieForm::Terminator;
        Candidates.RemoveAt(Pick);
    }
    CheckWinner();
}
int32 FHeliosBioRules::CountLivingHumanSide() const
{
    int32 Count = 0;
    for (const auto& P : Participants) if (P.bAlive && P.IsHumanSide()) ++Count;
    return Count;
}
void FHeliosBioRules::CheckWinner()
{
    if (Phase != EHeliosBioPhase::Active) return;
    bool bZombieCanReturn = false;
    for (const auto& P : Participants)
        if (P.Team == EHeliosBioTeam::Zombie && !P.bPermanentlyDead) bZombieCanReturn = true;
    if (CountLivingHumanSide() == 0) Winner = EHeliosBioWinner::Zombies;
    else if (!bZombieCanReturn || PhaseTimeRemaining <= 0.f) Winner = EHeliosBioWinner::Humans;
    if (Winner != EHeliosBioWinner::None) Phase = EHeliosBioPhase::RoundOver;
}
void FHeliosBioRules::Tick(float DeltaSeconds)
{
    if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f || Phase == EHeliosBioPhase::RoundOver) return;
    float Remaining = DeltaSeconds;
    if (Phase == EHeliosBioPhase::Preparation)
    {
        const float Step = FMath::Min(Remaining, PhaseTimeRemaining);
        PhaseTimeRemaining -= Step;
        Remaining -= Step;
        if (PhaseTimeRemaining <= 0.f) BeginInfection();
    }
    if (Phase != EHeliosBioPhase::Active) return;
    const float Step = FMath::Min(Remaining, PhaseTimeRemaining);
    PhaseTimeRemaining -= Step;
    for (auto& P : Participants)
    {
        P.SkillRemaining = FMath::Max(0.f, P.SkillRemaining - Step);
        P.SkillCooldownRemaining = FMath::Max(0.f, P.SkillCooldownRemaining - Step);
        P.FrozenSeconds = FMath::Max(0.f, P.FrozenSeconds - Step);
        P.ShieldRemaining = FMath::Max(0.f, P.ShieldRemaining - Step);
        P.SecondarySkillRemaining = FMath::Max(0.f,P.SecondarySkillRemaining-Step);
        P.SecondarySpeedRemaining = FMath::Max(0.f,P.SecondarySpeedRemaining-Step);
        P.SecondaryCooldownRemaining = FMath::Max(0.f,P.SecondaryCooldownRemaining-Step);
        const float RechargeTime=FMath::Max(0.f,Step-P.WeaponRechargeDelay);
        P.WeaponRechargeDelay=FMath::Max(0.f,P.WeaponRechargeDelay-Step);
        if(P.bAlive&&P.HunterRoleId==TEXT("BioMechanicalHunter"))P.WeaponEnergy=FMath::Min(P.WeaponMaxEnergy,P.WeaponEnergy+RechargeTime*P.WeaponRechargeRate);
        if(P.SecondarySkillRemaining<=0){P.bGhostMaskActive=P.bFirstAttackSwordWave=P.bHeavySwordWaveReady=false;P.SecondarySpeedMultiplier=P.SecondaryDamageMultiplier=1.f;}
        if (P.SkillRemaining <= 0.f) P.SkillSpeedMultiplier = P.SkillDamageMultiplier = 1.f;
        if (P.ShieldRemaining <= 0.f) P.ShieldReduction = 0.f;
        if (!P.bAlive && !P.bPermanentlyDead && P.Team == EHeliosBioTeam::Zombie)
        {
            P.RespawnSeconds = FMath::Max(0.f, P.RespawnSeconds - Step);
            if (P.RespawnSeconds <= 0.f) { P.bAlive = true; P.Health = P.MaxHealth; }
        }
    }
    if (Config.SupplyIntervalSeconds > 0.f)
    {
        SupplyElapsed += Step;
        const int32 Waves = FMath::FloorToInt(SupplyElapsed / Config.SupplyIntervalSeconds);
        SupplyWave += Waves;
        SupplyElapsed -= Waves * Config.SupplyIntervalSeconds;
    }
    CheckWinner();
}
bool FHeliosBioRules::Infect(int32 VictimId, int32 AttackerId)
{
    auto* V = FindMutable(VictimId);
    auto* A = FindMutable(AttackerId);
    if (Phase != EHeliosBioPhase::Active || !V || !A || !V->bAlive || !A->bAlive ||
        V->Team != EHeliosBioTeam::Human || A->Team != EHeliosBioTeam::Zombie || A->FrozenSeconds > 0.f) return false;
    SetTeam(*V, EHeliosBioTeam::Zombie, Config.ZombieHealth);
    ++A->Infections;
    ++A->EvolutionInfections;
    RefreshZombieEvolution(*A);
    CheckWinner();
    return true;
}
void FHeliosBioRules::RefreshZombieEvolution(FHeliosBioParticipant& P)
{
    const int32 Level = FMath::Min(FMath::Max(0, Config.MaximumZombieEvolution),
        P.EvolutionInfections / FMath::Max(1, Config.InfectionsPerEvolution) +
        P.ZombieSuppliesCollected / FMath::Max(1, Config.ZombieSuppliesPerEvolution));
    if (Level > P.EvolutionLevel)
    {
        const float Bonus = FMath::Max(0.f, Config.EvolutionHealthBonus) * (Level - P.EvolutionLevel);
        P.EvolutionLevel = Level;
        P.MaxHealth += Bonus;
        P.Health = FMath::Min(P.MaxHealth, P.Health + Bonus);
    }
}
bool FHeliosBioRules::ApplyDamage(int32 VictimId, int32 AttackerId, float Damage, bool bHunterMelee)
{
    auto* V = FindMutable(VictimId);
    auto* A = FindMutable(AttackerId);
    if (Phase != EHeliosBioPhase::Active || !V || !A || !V->bAlive || !A->bAlive ||
        V->IsHumanSide() == A->IsHumanSide() || !FMath::IsFinite(Damage) || Damage <= 0.f) return false;
    if (A->FrozenSeconds > 0.f) return false;
    if (A->Team == EHeliosBioTeam::Zombie && V->Team == EHeliosBioTeam::Human) return Infect(VictimId, AttackerId);
    if (A->SkillRemaining > 0.f) Damage *= FMath::Max(0.f, A->SkillDamageMultiplier);
    if (A->bGhostMaskActive && A->SecondarySkillRemaining > 0.f) Damage *= FMath::Max(0.f,A->SecondaryDamageMultiplier);
    if (V->ShieldRemaining > 0.f) Damage *= 1.f - FMath::Clamp(V->ShieldReduction, 0.f, .99f);
    A->DamageDealt += FMath::Min(V->Health, Damage);
    V->Health = FMath::Max(0.f, V->Health - Damage);
    if (V->Health <= 0.f)
    {
        V->bAlive = false;
        ++V->Deaths; ++A->Kills;
        if(A->HunterRoleId==TEXT("BioZhongXiaoKuiHunter"))
        {
            A->SkillCooldownRemaining=FMath::Max(0.f,A->SkillCooldownRemaining-2.f);
            A->SecondaryCooldownRemaining=FMath::Max(0.f,A->SecondaryCooldownRemaining-2.f);
            if(bHunterMelee&&A->bGhostMaskActive&&A->SecondarySkillRemaining>0)
            {A->SecondarySkillRemaining=FMath::Min(30.f,A->SecondarySkillRemaining+2.f);A->bHeavySwordWaveReady=true;}
        }
        V->bHunterChoicePending = false;
        // Retain the defeated avatar for its real death pose and spectator identity.
        // SetTeam/StartRound clear the role when the next life or round begins.
        // A defeated life cannot retain shields, ghost masks or queued sword waves across respawn.
        // Cooldowns remain intact and continue ticking, so dying cannot provide an immediate recast.
        V->SkillRemaining=V->ShieldRemaining=V->ShieldReduction=V->FrozenSeconds=V->SecondarySkillRemaining=0.f;
        V->SkillSpeedMultiplier=V->SkillDamageMultiplier=V->SecondarySpeedMultiplier=V->SecondaryDamageMultiplier=1.f;
        V->bGhostMaskActive=V->bFirstAttackSwordWave=V->bHeavySwordWaveReady=false;
        V->SecondarySpeedRemaining=0.f;
        V->bPermanentlyDead = V->IsHumanSide() || (bHunterMelee && A->IsHunter());
        V->RespawnSeconds = V->bPermanentlyDead ? 0.f : FMath::Max(0.f, Config.RespawnSeconds);
    }
    CheckWinner();
    return true;
}
bool FHeliosBioRules::CanTransform(int32 Id) const
{
    const auto* P = FindParticipant(Id);
    return Phase == EHeliosBioPhase::Active && P && P->bAlive && P->Team == EHeliosBioTeam::Human &&
        CountLivingHumanSide() <= FMath::Max(1, Config.HunterThreshold);
}
bool FHeliosBioRules::Transform(int32 Id)
{
    if (!CanTransform(Id)) return false;
    SetTeam(*FindMutable(Id), EHeliosBioTeam::GhostHunter, Config.GhostHunterHealth);
    return true;
}
bool FHeliosBioRules::TransformUltimate(int32 Id)
{
    auto* P = FindMutable(Id);
    if (Config.bRequireHunterSelection) return false; // Legacy fixture path only; normal play uses explicit confirmation.
    if (Phase != EHeliosBioPhase::Active || !P || !P->bAlive || P->Team != EHeliosBioTeam::GhostHunter) return false;
    SetTeam(*P, EHeliosBioTeam::UltimateHunter, Config.UltimateHunterHealth);
    return true;
}
bool FHeliosBioRules::PickupSupply(int32 Id, EHeliosBioSupplyKind Kind)
{
    auto* P = FindMutable(Id);
    if (Phase != EHeliosBioPhase::Active || !P || !P->bAlive) return false;
    if (Kind == EHeliosBioSupplyKind::RedTerminatorEvolution)
    {
        if (P->Team != EHeliosBioTeam::Zombie || !P->bMother ||
            P->ZombieForm != EHeliosBioZombieForm::Terminator ||
            P->EvolutionLevel < FMath::Max(0, Config.MaximumZombieEvolution) ||
            !Config.bAbyssSourceAvailable || Config.AbyssRoleId.IsNone()) return false;
        P->ZombieForm = EHeliosBioZombieForm::Abyss;
        P->ZombieRoleId = Config.AbyssRoleId;
        P->MaxHealth = FMath::Max(1.f, Config.AbyssHealth);
        P->Health = P->MaxHealth;
        ++P->SuppliesCollected;
        return true;
    }
    if (Kind == EHeliosBioSupplyKind::BlueHunterUpgrade)
    {
        if (P->Team != EHeliosBioTeam::GhostHunter || P->bHunterChoicePending) return false;
        const int32 Required = FMath::Max(1, Config.BlueSuppliesRequired);
        if (P->BlueSuppliesCollected >= Required - 1)
        {
            if (Config.bRequireHunterSelection) P->bHunterChoicePending = true;
            else if (!TransformUltimate(Id)) return false;
        }
        ++P->BlueSuppliesCollected;
        ++P->SuppliesCollected;
        return true;
    }
    if (Kind != EHeliosBioSupplyKind::Normal) return false;
    if (P->Team == EHeliosBioTeam::Zombie)
    {
        if (!Config.bZombieNormalSupplyEnabled) return false;
        ++P->ZombieSuppliesCollected;
        RefreshZombieEvolution(*P);
    }
    ++P->SuppliesCollected;
    P->Health = FMath::Min(P->MaxHealth, P->Health + FMath::Max(0.f, Config.SupplyHeal));
    return true;
}
bool FHeliosBioRules::ConfirmHunterChoice(int32 Id, FName RoleId)
{
    auto* P = FindMutable(Id);
    if (Phase != EHeliosBioPhase::Active || !P || !P->bAlive ||
        P->Team != EHeliosBioTeam::GhostHunter || !P->bHunterChoicePending ||
        RoleId.IsNone() || !Config.AllowedHunterRoles.Contains(RoleId)) return false;
    SetTeam(*P, EHeliosBioTeam::UltimateHunter, Config.UltimateHunterHealth);
    P->HunterRoleId = RoleId;
    return true;
}
bool FHeliosBioRules::CancelHunterChoice(int32 Id)
{
    auto* P = FindMutable(Id);
    if (Phase != EHeliosBioPhase::Active || !P || !P->bAlive ||
        P->Team != EHeliosBioTeam::GhostHunter || !P->bHunterChoicePending) return false;
    P->HunterRoleId = NAME_None;
    return true;
}
bool FHeliosBioRules::ActivateSkill(int32 Id, float Duration, float Cooldown,
    float SpeedMultiplier, float DamageMultiplier, float ShieldReduction)
{
    if (!FMath::IsFinite(Duration) || !FMath::IsFinite(Cooldown) || !FMath::IsFinite(SpeedMultiplier) ||
        !FMath::IsFinite(DamageMultiplier) || !FMath::IsFinite(ShieldReduction)) return false;
    auto* P = FindMutable(Id);
    // Zombie skill shares timers; the actor integration applies its speed boost.
    if (Phase != EHeliosBioPhase::Active || !P || !P->bAlive ||
        (!P->IsHunter() && P->Team != EHeliosBioTeam::Zombie) || P->SkillCooldownRemaining > 0.f || P->FrozenSeconds > 0.f) return false;
    P->SkillRemaining = FMath::Max(0.f, Duration < 0.f ? Config.SkillDurationSeconds : Duration);
    P->SkillCooldownRemaining = FMath::Max(P->SkillRemaining, Cooldown < 0.f ? Config.SkillCooldownSeconds : Cooldown);
    P->SkillSpeedMultiplier = FMath::Max(0.f, SpeedMultiplier < 0.f ? (P->Team == EHeliosBioTeam::Zombie ? 1.3f : 1.f) : SpeedMultiplier);
    P->SkillDamageMultiplier = FMath::Max(0.f, DamageMultiplier < 0.f ? (P->IsHunter() ? Config.HunterSkillDamageMultiplier : 1.f) : DamageMultiplier);
    P->ShieldReduction = FMath::Clamp(ShieldReduction, 0.f, .99f);
    P->ShieldRemaining = P->ShieldReduction > 0.f ? P->SkillRemaining : 0.f;
    return true;
}
bool FHeliosBioRules::FreezeParticipant(int32 VictimId, int32 AttackerId, float Seconds)
{
    auto* V = FindMutable(VictimId); const auto* A = FindParticipant(AttackerId);
    if (Phase != EHeliosBioPhase::Active || !V || !A || !V->bAlive || !A->bAlive ||
        V->IsHumanSide() == A->IsHumanSide() || !FMath::IsFinite(Seconds) || Seconds <= 0.f) return false;
    V->FrozenSeconds = FMath::Max(V->FrozenSeconds, Seconds);
    return true;
}
bool FHeliosBioRules::ActivateSecondarySkill(int32 Id,float Duration,float Cooldown,float SpeedMultiplier,float DamageMultiplier,bool GhostMask)
{
    auto* P=FindMutable(Id);
    if(!P||Phase!=EHeliosBioPhase::Active||!P->bAlive||!P->IsHunter()||P->FrozenSeconds>0||P->SecondaryCooldownRemaining>0||
       !FMath::IsFinite(Duration)||!FMath::IsFinite(Cooldown)||!FMath::IsFinite(SpeedMultiplier)||!FMath::IsFinite(DamageMultiplier)||Duration<0||Cooldown<0)return false;
    P->SecondarySkillRemaining=P->SecondarySkillDuration=Duration;P->SecondaryCooldownRemaining=FMath::Max(Duration,Cooldown);
    P->SecondarySpeedRemaining=Duration;
    P->SecondarySpeedMultiplier=FMath::Max(0.f,SpeedMultiplier);P->SecondaryDamageMultiplier=FMath::Max(0.f,DamageMultiplier);
    P->bGhostMaskActive=GhostMask&&Duration>0;P->bFirstAttackSwordWave=P->bGhostMaskActive;P->bHeavySwordWaveReady=false;return true;
}
bool FHeliosBioRules::ConsumeSwordWave(int32 Id,bool Heavy)
{
    auto* P=FindMutable(Id);if(!P||Phase!=EHeliosBioPhase::Active||!P->bAlive||P->FrozenSeconds>0||!P->bGhostMaskActive||P->SecondarySkillRemaining<=0)return false;
    if(!P->bFirstAttackSwordWave&&!(Heavy&&P->bHeavySwordWaveReady))return false;
    P->bFirstAttackSwordWave=false;P->bHeavySwordWaveReady=false;return true;
}
bool FHeliosBioRules::DevolveDefeatedZombie(int32 Id)
{
    auto* P=FindMutable(Id);if(!P||P->bAlive||P->bPermanentlyDead||P->Team!=EHeliosBioTeam::Zombie)return false;
    P->EvolutionLevel=P->EvolutionInfections=P->ZombieSuppliesCollected=0;
    P->MaxHealth=FMath::Max(1.f,P->ZombieForm==EHeliosBioZombieForm::Abyss?Config.AbyssHealth:P->bMother?Config.MotherHealth:Config.ZombieHealth);
    return true;
}
bool FHeliosBioRules::ConsumeRoleWeaponEnergy(int32 Id,float Cost,float RechargeDelay,float RechargeRate,float Capacity)
{
    auto* P=FindMutable(Id);
    if(!P||Phase!=EHeliosBioPhase::Active||!P->bAlive||P->FrozenSeconds>0||P->HunterRoleId!=TEXT("BioMechanicalHunter")||
        !FMath::IsFinite(Cost)||Cost<=0||!FMath::IsFinite(Capacity)||Capacity<=0||!FMath::IsFinite(RechargeDelay)||RechargeDelay<0||!FMath::IsFinite(RechargeRate)||RechargeRate<0)return false;
    const float Available=FMath::Clamp(P->WeaponEnergy,0.f,Capacity);if(Available<Cost)return false;
    P->WeaponMaxEnergy=Capacity;P->WeaponRechargeRate=RechargeRate;P->WeaponEnergy=Available-Cost;P->WeaponRechargeDelay=RechargeDelay;return true;
}
