#if WITH_DEV_AUTOMATION_TESTS
#include "Bio/HeliosBioRules.h"
#include "Misc/AutomationTest.h"

namespace
{
// Explicit pure-rule role qualification; does not bypass source-asset readiness in the game.
bool PrepareRuleHunter(FHeliosBioRules& Rules,FName Role,int32& Mother)
{
    Rules.Config.PreparationSeconds=0.f;Rules.Config.InitialMotherCount=1;Rules.Config.HunterThreshold=8;
    Rules.Config.AllowedHunterRoles.Add(Role);Rules.StartRound(8,1337);Mother=INDEX_NONE;
    for(const auto& P:Rules.GetParticipants())if(P.bMother)Mother=P.Id;
    return Mother!=INDEX_NONE&&Rules.Transform(0)&&Rules.PickupSupply(0,EHeliosBioSupplyKind::BlueHunterUpgrade)&&Rules.ConfirmHunterChoice(0,Role);
}
bool SameSerumStateExceptHealth(const FHeliosBioParticipant& A,const FHeliosBioParticipant& B)
{
    return A.Id==B.Id&&A.Team==B.Team&&A.MaxHealth==B.MaxHealth&&A.bAlive==B.bAlive&&A.bPermanentlyDead==B.bPermanentlyDead&&A.bMother==B.bMother&&A.bUseModernAvatar==B.bUseModernAvatar&&A.RespawnSeconds==B.RespawnSeconds&&
        A.Infections==B.Infections&&A.EvolutionInfections==B.EvolutionInfections&&A.EvolutionLevel==B.EvolutionLevel&&A.SuppliesCollected==B.SuppliesCollected&&A.BlueSuppliesCollected==B.BlueSuppliesCollected&&A.HunterRoleId==B.HunterRoleId&&A.bHunterChoicePending==B.bHunterChoicePending&&A.ZombieForm==B.ZombieForm&&A.ZombieRoleId==B.ZombieRoleId&&A.ZombieSuppliesCollected==B.ZombieSuppliesCollected&&
        A.SkillRemaining==B.SkillRemaining&&A.SkillCooldownRemaining==B.SkillCooldownRemaining&&A.FrozenSeconds==B.FrozenSeconds&&A.ShieldRemaining==B.ShieldRemaining&&A.ShieldReduction==B.ShieldReduction&&A.SkillSpeedMultiplier==B.SkillSpeedMultiplier&&A.SkillDamageMultiplier==B.SkillDamageMultiplier&&
        A.SecondarySkillRemaining==B.SecondarySkillRemaining&&A.SecondaryCooldownRemaining==B.SecondaryCooldownRemaining&&A.SecondarySkillDuration==B.SecondarySkillDuration&&A.SecondarySpeedMultiplier==B.SecondarySpeedMultiplier&&A.SecondaryDamageMultiplier==B.SecondaryDamageMultiplier&&A.SecondarySpeedRemaining==B.SecondarySpeedRemaining&&A.bGhostMaskActive==B.bGhostMaskActive&&A.bFirstAttackSwordWave==B.bFirstAttackSwordWave&&A.bHeavySwordWaveReady==B.bHeavySwordWaveReady&&
        A.WeaponEnergy==B.WeaponEnergy&&A.WeaponMaxEnergy==B.WeaponMaxEnergy&&A.WeaponRechargeDelay==B.WeaponRechargeDelay&&A.WeaponRechargeRate==B.WeaponRechargeRate&&A.Kills==B.Kills&&A.Deaths==B.Deaths&&A.DamageDealt==B.DamageDealt;
}
bool SerumRejectedWithoutMutation(FAutomationTestBase& Test,FHeliosBioRules& Rules,int32 Target,int32 Caster,const FString& Label)
{
    const auto Before=Rules.GetParticipants();const auto Phase=Rules.GetPhase();const auto Winner=Rules.GetWinner();const float Remaining=Rules.GetPhaseTimeRemaining();
    bool Passed=Test.TestTrue(Label+TEXT(" returns Rejected"),Rules.ApplyRescueSerum(Target,Caster)==EHeliosBioSerumResult::Rejected);
    Passed&=Test.TestTrue(Label+TEXT(" retains phase/winner/time"),Rules.GetPhase()==Phase&&Rules.GetWinner()==Winner&&Rules.GetPhaseTimeRemaining()==Remaining);
    for(const auto& P:Before){const auto* After=Rules.FindParticipant(P.Id);Passed&=Test.TestTrue(Label+FString::Printf(TEXT(" retains complete participant %d"),P.Id),After&&After->Health==P.Health&&SameSerumStateExceptHealth(P,*After));}
    return Passed;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosBioRulesLifecycleTest,
    "Helios.BioHotel.Rules.Lifecycle", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FHeliosBioRulesLifecycleTest::RunTest(const FString& Parameters)
{
    FHeliosBioRules R;
    R.StartRound();
    TestEqual(TEXT("32 participants"), R.GetParticipants().Num(), 32);
    TestFalse(TEXT("No preparation damage"), R.ApplyDamage(0, 1, 10.f));
    R.Tick(25.f);
    TestTrue(TEXT("Crosses preparation"), R.GetPhase() == EHeliosBioPhase::Active);
    TestEqual(TEXT("Overshoot consumes active time"), R.GetPhaseTimeRemaining(), 175.f);
    TestEqual(TEXT("Two mothers"), R.CountLivingHumanSide(), 30);
    TestTrue(TEXT("Local player starts human"), R.FindParticipant(0)->Team == EHeliosBioTeam::Human);
    R.Tick(175.f);
    TestTrue(TEXT("Survival wins"), R.GetWinner() == EHeliosBioWinner::Humans);
    R.StartRound();
    TestTrue(TEXT("Restart clears winner"), R.GetWinner() == EHeliosBioWinner::None);
    TestEqual(TEXT("Restart clears supply waves"), R.GetSupplyWave(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosBioRulesCombatTest,
    "Helios.BioHotel.Rules.InfectionHunterRespawn", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FHeliosBioRulesCombatTest::RunTest(const FString& Parameters)
{
    FHeliosBioRules R;
    R.Config.bRequireHunterSelection = false; // Legacy mechanic fixture, not the user-facing role selection flow.
    R.Config.bZombieNormalSupplyEnabled = false;
    R.Config.PreparationSeconds = 0.f;
    R.Config.InitialMotherCount = 1;
    R.StartRound(4);
    int32 Mother = INDEX_NONE;
    int32 Human = INDEX_NONE;
    for (const auto& P : R.GetParticipants())
    {
        if (P.bMother) Mother = P.Id;
        else if (P.Id != 0) Human = P.Id;
    }
    TestFalse(TEXT("Cannot transform with three humans"), R.Transform(0));
    TestTrue(TEXT("Zombie hit infects human"), R.ApplyDamage(Human, Mother, 1.f));
    TestTrue(TEXT("Two humans may transform"), R.Transform(0));
    TestFalse(TEXT("Hunter cannot be infected"), R.Infect(0, Mother));
    TestFalse(TEXT("Zombie cannot collect human supply"), R.PickupSupply(Mother));
    TestTrue(TEXT("Blue supply evolves hunter"), R.PickupSupply(0, EHeliosBioSupplyKind::BlueHunterUpgrade));
    TestTrue(TEXT("Ultimate form"), R.FindParticipant(0)->Team == EHeliosBioTeam::UltimateHunter);
    TestTrue(TEXT("Hunter skill activates"), R.ActivateSkill(0));
    TestFalse(TEXT("Skill cannot bypass cooldown"), R.ActivateSkill(0));
    R.ApplyDamage(Mother, 0, 100000.f, false);
    TestFalse(TEXT("Gun kill is temporary"), R.FindParticipant(Mother)->bPermanentlyDead);
    R.Tick(R.Config.RespawnSeconds);
    TestTrue(TEXT("Zombie respawns at full HP"), R.FindParticipant(Mother)->bAlive && R.FindParticipant(Mother)->Health == R.FindParticipant(Mother)->MaxHealth);
    R.ApplyDamage(Mother, 0, 100000.f, true);
    R.Tick(10.f);
    TestTrue(TEXT("Hunter melee prevents return"), R.FindParticipant(Mother)->bPermanentlyDead && !R.FindParticipant(Mother)->bAlive);
    R.ApplyDamage(Human, 0, 100000.f, true);
    TestTrue(TEXT("Eliminating all zombies wins"), R.GetWinner() == EHeliosBioWinner::Humans);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosBioRulesInfectionVictoryTest,
    "Helios.BioHotel.Rules.EvolutionAndInfectionVictory", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FHeliosBioRulesInfectionVictoryTest::RunTest(const FString& Parameters)
{
    FHeliosBioRules R;
    R.Config.PreparationSeconds = 0.f;
    R.Config.InitialMotherCount = 1;
    R.StartRound(5);
    int32 Mother = INDEX_NONE;
    for (const auto& P : R.GetParticipants()) if (P.bMother) Mother = P.Id;
    R.Tick(61.f);
    TestEqual(TEXT("Supply timing supports large ticks"), R.GetSupplyWave(), 2);
    for (int32 Id = 0; Id < 5; ++Id) if (Id != Mother) R.Infect(Id, Mother);
    TestEqual(TEXT("Infections upgrade mother"), R.FindParticipant(Mother)->EvolutionLevel, 2);
    TestTrue(TEXT("All infected wins for zombies"), R.GetWinner() == EHeliosBioWinner::Zombies);
    TestFalse(TEXT("Round over blocks skill"), R.ActivateSkill(0));
    TestFalse(TEXT("Bad id is rejected"), R.PickupSupply(-1));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosBioRulesZombieSkillTest,
    "Helios.BioHotel.Rules.ZombieSkillTimers", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FHeliosBioRulesZombieSkillTest::RunTest(const FString& Parameters)
{
    FHeliosBioRules R;
    R.Config.PreparationSeconds = 0.f;
    R.Config.InitialMotherCount = 1;
    R.StartRound(4);
    int32 Mother = INDEX_NONE;
    for (const auto& P : R.GetParticipants()) if (P.bMother) Mother = P.Id;
    TestFalse(TEXT("Human cannot activate a skill"), R.ActivateSkill(0));
    TestTrue(TEXT("Zombie activates its movement skill"), R.ActivateSkill(Mother));
    TestEqual(TEXT("Zombie receives configured duration"), R.FindParticipant(Mother)->SkillRemaining, R.Config.SkillDurationSeconds);
    TestFalse(TEXT("Zombie cannot bypass cooldown"), R.ActivateSkill(Mother));
    R.Tick(R.Config.SkillDurationSeconds);
    TestEqual(TEXT("Boost expires before cooldown"), R.FindParticipant(Mother)->SkillRemaining, 0.f);
    TestFalse(TEXT("Expired boost does not reset cooldown"), R.ActivateSkill(Mother));
    R.Tick(R.Config.SkillCooldownSeconds - R.Config.SkillDurationSeconds);
    TestTrue(TEXT("Zombie can activate after full cooldown"), R.ActivateSkill(Mother));
    R.ApplyDamage(Mother, 0, 100000.f);
    TestFalse(TEXT("Dead zombie cannot activate a skill"), R.ActivateSkill(Mother));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosBioRulesLocalMotherSelectionTest,
    "Helios.BioHotel.Rules.LocalMotherSelection", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FHeliosBioRulesLocalMotherSelectionTest::RunTest(const FString& Parameters)
{
    FHeliosBioRules Inclusive, Protected, Repeat;
    for (FHeliosBioRules* Rules : { &Inclusive, &Protected, &Repeat })
    {
        Rules->Config.ParticipantCount = 30;
        Rules->Config.InitialMotherCount = 2;
        Rules->Config.PreparationSeconds = 0.f;
    }
    Inclusive.Config.bKeepLocalPlayerHuman = false;
    Repeat.Config.bKeepLocalPlayerHuman = false;
    Protected.Config.bKeepLocalPlayerHuman = true;
    bool bSawLocalMother = false;
    for (int32 Seed = 0; Seed < 64; ++Seed)
    {
        Inclusive.StartRound(0, Seed);
        Protected.StartRound(0, Seed);
        Repeat.StartRound(0, Seed);
        int32 InclusiveMothers = 0, ProtectedMothers = 0;
        for (const auto& Participant : Inclusive.GetParticipants())
        {
            if (Participant.bMother) ++InclusiveMothers;
            const auto* Repeated = Repeat.FindParticipant(Participant.Id);
            TestTrue(FString::Printf(TEXT("Seed %d selection is reproducible for ID %d"), Seed, Participant.Id),
                Repeated && Participant.bMother == Repeated->bMother && Participant.Team == Repeated->Team);
        }
        for (const auto& Participant : Protected.GetParticipants()) if (Participant.bMother) ++ProtectedMothers;
        TestEqual(FString::Printf(TEXT("Seed %d inclusive roster has 30 participants"), Seed), Inclusive.GetParticipants().Num(), 30);
        TestEqual(FString::Printf(TEXT("Seed %d protected roster has 30 participants"), Seed), Protected.GetParticipants().Num(), 30);
        TestEqual(FString::Printf(TEXT("Seed %d inclusive roster has exactly two mothers"), Seed), InclusiveMothers, 2);
        TestEqual(FString::Printf(TEXT("Seed %d protected roster has exactly two mothers"), Seed), ProtectedMothers, 2);
        TestTrue(FString::Printf(TEXT("Seed %d protection keeps local player human"), Seed),
            !Protected.FindParticipant(0)->bMother && Protected.FindParticipant(0)->Team == EHeliosBioTeam::Human);
        bSawLocalMother |= Inclusive.FindParticipant(0)->bMother;
    }
    TestTrue(TEXT("Inclusive random selection can choose the local player as a mother within 64 reproducible seeds"), bSawLocalMother);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosBioRulesBlueSupplyTest,
    "Helios.BioHotel.Rules.BlueSupplyEligibility", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FHeliosBioRulesBlueSupplyTest::RunTest(const FString& Parameters)
{
    const auto Blue = EHeliosBioSupplyKind::BlueHunterUpgrade;
    FHeliosBioRules R;
    R.Config.bRequireHunterSelection = false; // Tests box eligibility independently of the chooser.
    R.Config.InitialMotherCount = 1;
    R.StartRound(3);
    TestFalse(TEXT("Preparation rejects blue supplies"), R.PickupSupply(0, Blue));
    R.Tick(R.Config.PreparationSeconds);
    int32 Mother = INDEX_NONE;
    for (const auto& P : R.GetParticipants()) if (P.bMother) Mother = P.Id;
    TestFalse(TEXT("Human leaves blue supply unconsumed"), R.PickupSupply(0, Blue));
    TestEqual(TEXT("Rejected human pickup does not count"), R.FindParticipant(0)->SuppliesCollected, 0);
    TestFalse(TEXT("Zombie leaves blue supply unconsumed"), R.PickupSupply(Mother, Blue));
    TestTrue(TEXT("Human can collect normal supply"), R.PickupSupply(0));
    TestTrue(TEXT("Last two may become Ghost"), R.Transform(0));
    TestTrue(TEXT("Ghost can collect normal supply"), R.PickupSupply(0));
    TestTrue(TEXT("Normal supply never upgrades Ghost"), R.FindParticipant(0)->Team == EHeliosBioTeam::GhostHunter);
    TestEqual(TEXT("Normal supplies do not advance blue progress"), R.FindParticipant(0)->BlueSuppliesCollected, 0);
    TestTrue(TEXT("Default single blue supply upgrades Ghost"), R.PickupSupply(0, Blue));
    TestTrue(TEXT("Blue reaches legacy Ultimate team"), R.FindParticipant(0)->Team == EHeliosBioTeam::UltimateHunter);
    TestFalse(TEXT("Ultimate cannot consume another upgrade box"), R.PickupSupply(0, Blue));
    TestEqual(TEXT("Only successful pickups count"), R.FindParticipant(0)->SuppliesCollected, 3);
    TestEqual(TEXT("One successful blue pickup"), R.FindParticipant(0)->BlueSuppliesCollected, 1);

    R.Config.PreparationSeconds = 0.f;
    R.Config.BlueSuppliesRequired = 2;
    R.StartRound(3);
    TestEqual(TEXT("Round restart clears blue progress"), R.FindParticipant(0)->BlueSuppliesCollected, 0);
    R.Transform(0);
    TestTrue(TEXT("First configured blue pickup succeeds"), R.PickupSupply(0, Blue));
    R.PickupSupply(0);
    TestTrue(TEXT("Normal pickup cannot finish configured blue progression"), R.FindParticipant(0)->Team == EHeliosBioTeam::GhostHunter);
    TestTrue(TEXT("Second blue pickup completes configured progression"), R.PickupSupply(0, Blue));
    for (const auto& P : R.GetParticipants()) if (P.bMother) Mother = P.Id;
    R.ApplyDamage(0, Mother, 100000.f);
    const int32 Collected = R.FindParticipant(0)->SuppliesCollected;
    TestFalse(TEXT("Dead hunter cannot consume blue"), R.PickupSupply(0, Blue));
    TestEqual(TEXT("Dead pickup does not count"), R.FindParticipant(0)->SuppliesCollected, Collected);
    R.Tick(R.Config.RoundSeconds);
    TestFalse(TEXT("Round over rejects blue supplies"), R.PickupSupply(0, Blue));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosBioRulesHunterChoiceTest,
    "Helios.BioHotel.Rules.HunterChoice", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FHeliosBioRulesHunterChoiceTest::RunTest(const FString& Parameters)
{
    FHeliosBioRules R;
    const FName VerifiedRole(TEXT("VerifiedFixtureRole"));
    R.Config.AllowedHunterRoles.Add(VerifiedRole); // Synthetic rule fixture; mesh validation belongs to integration.
    R.Config.InitialMotherCount = 1;
    R.Config.PreparationSeconds = 0.f;
    R.StartRound(3);
    TestFalse(TEXT("Human cannot choose without earning eligibility"), R.ConfirmHunterChoice(0, VerifiedRole));
    R.Transform(0);
    TestFalse(TEXT("Ghost cannot bypass blue supply"), R.ConfirmHunterChoice(0, VerifiedRole));
    TestFalse(TEXT("Direct legacy upgrade is disabled by default"), R.TransformUltimate(0));
    TestTrue(TEXT("Blue supply starts choice"), R.PickupSupply(0, EHeliosBioSupplyKind::BlueHunterUpgrade));
    TestTrue(TEXT("Pending choice keeps Ghost alive"), R.FindParticipant(0)->bHunterChoicePending && R.FindParticipant(0)->Team == EHeliosBioTeam::GhostHunter);
    TestFalse(TEXT("No implicit fallback role"), R.ConfirmHunterChoice(0, NAME_None));
    TestFalse(TEXT("Unknown role rejected"), R.ConfirmHunterChoice(0, FName(TEXT("UnverifiedRole"))));
    TestFalse(TEXT("Pending choice does not consume another blue box"), R.PickupSupply(0, EHeliosBioSupplyKind::BlueHunterUpgrade));
    TestEqual(TEXT("Pending repeat leaves successful count unchanged"), R.FindParticipant(0)->SuppliesCollected, 1);
    TestTrue(TEXT("Cancel closes choice without spending eligibility again"), R.CancelHunterChoice(0));
    TestTrue(TEXT("Cancelled chooser can be reopened"), R.FindParticipant(0)->bHunterChoicePending);
    TestTrue(TEXT("Allowed role confirms after cancel"), R.ConfirmHunterChoice(0, VerifiedRole));
    TestTrue(TEXT("Confirmation stores role and upgrades"), R.FindParticipant(0)->HunterRoleId == VerifiedRole && R.FindParticipant(0)->Team == EHeliosBioTeam::UltimateHunter && !R.FindParticipant(0)->bHunterChoicePending);
    TestEqual(TEXT("Confirmed health uses configured balance"), R.FindParticipant(0)->Health, R.Config.UltimateHunterHealth);
    TestFalse(TEXT("Repeated confirmation rejected"), R.ConfirmHunterChoice(0, VerifiedRole));
    R.StartRound(3);
    R.Transform(0);
    R.PickupSupply(0, EHeliosBioSupplyKind::BlueHunterUpgrade);
    int32 Mother = INDEX_NONE;
    for (const auto& P : R.GetParticipants()) if (P.bMother) Mother = P.Id;
    R.ApplyDamage(0, Mother, 100000.f);
    TestFalse(TEXT("Death clears pending choice"), R.FindParticipant(0)->bHunterChoicePending);
    TestTrue(TEXT("Death clears role"), R.FindParticipant(0)->HunterRoleId.IsNone());
    TestFalse(TEXT("Dead chooser cannot confirm"), R.ConfirmHunterChoice(0, VerifiedRole));
    R.StartRound(3);
    TestFalse(TEXT("New round clears pending choice"), R.FindParticipant(0)->bHunterChoicePending);
    TestEqual(TEXT("New round clears blue progress"), R.FindParticipant(0)->BlueSuppliesCollected, 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosBioRulesRedSupplyTest,
    "Helios.BioHotel.Rules.RedSupplySourceGate", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FHeliosBioRulesRedSupplyTest::RunTest(const FString& Parameters)
{
    FHeliosBioRules R;
    R.Config.PreparationSeconds = 0.f;
    R.Config.InitialMotherCount = 1;
    R.Config.bMothersAreTerminators = true; // Synthetic role fixture; actual source identity is integration's responsibility.
    R.Config.ZombieSuppliesPerEvolution = 1;
    R.Config.MaximumZombieEvolution = 2;
    R.StartRound(4);
    int32 Mother = INDEX_NONE;
    for (const auto& P : R.GetParticipants()) if (P.bMother) Mother = P.Id;
    const auto Red = EHeliosBioSupplyKind::RedTerminatorEvolution;
    TestFalse(TEXT("Human cannot consume red"), R.PickupSupply(0, Red));
    TestFalse(TEXT("Unevolved mother cannot consume red"), R.PickupSupply(Mother, Red));
    R.PickupSupply(Mother);
    R.PickupSupply(Mother);
    TestEqual(TEXT("Configured normal supplies reach maximum evolution"), R.FindParticipant(Mother)->EvolutionLevel, 2);
    TestFalse(TEXT("Missing real Abyss prevents consumption"), R.PickupSupply(Mother, Red));
    TestEqual(TEXT("Rejected red does not increment count"), R.FindParticipant(Mother)->SuppliesCollected, 2);
    R.Config.bAbyssSourceAvailable = true;
    TestFalse(TEXT("Availability without role identity is insufficient"), R.PickupSupply(Mother, Red));
    R.Config.AbyssRoleId = FName(TEXT("VerifiedAbyssFixture"));
    R.Infect(0, Mother);
    R.PickupSupply(0); R.PickupSupply(0);
    TestFalse(TEXT("Fully evolved ordinary zombie cannot consume mother-only red"), R.PickupSupply(0, Red));
    TestTrue(TEXT("Qualified source-gated mother evolves"), R.PickupSupply(Mother, Red));
    TestTrue(TEXT("Abyss remains zombie side with separate form and role"), R.FindParticipant(Mother)->Team == EHeliosBioTeam::Zombie && R.FindParticipant(Mother)->ZombieForm == EHeliosBioZombieForm::Abyss && R.FindParticipant(Mother)->ZombieRoleId == R.Config.AbyssRoleId);
    TestFalse(TEXT("Abyss cannot consume repeat red"), R.PickupSupply(Mother, Red));
    TestEqual(TEXT("Only successful red is counted"), R.FindParticipant(Mother)->SuppliesCollected, 3);
    R.StartRound(4);
    for (const auto& P : R.GetParticipants()) if (P.bMother) Mother = P.Id;
    R.Config.bMothersAreTerminators = false;
    R.StartRound(4);
    for (const auto& P : R.GetParticipants()) if (P.bMother) Mother = P.Id;
    R.PickupSupply(Mother); R.PickupSupply(Mother);
    TestFalse(TEXT("Generic mother is not automatically Terminator"), R.PickupSupply(Mother, Red));
    R.ApplyDamage(Mother, 0, 100000.f);
    TestFalse(TEXT("Dead zombie cannot collect normal or red"), R.PickupSupply(Mother) || R.PickupSupply(Mother, Red));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosBioRoleStatusTest,
    "Helios.BioHotel.Rules.RoleStatusAndReset", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FHeliosBioRoleStatusTest::RunTest(const FString& Parameters)
{
    FHeliosBioRules R;
    R.Config.PreparationSeconds = 0.f;
    R.Config.InitialMotherCount = 1;
    R.Config.HunterThreshold = 3;
    R.StartRound(4);
    int32 Mother = INDEX_NONE, Human = INDEX_NONE;
    for (const auto& P : R.GetParticipants()) if (P.bMother) Mother = P.Id; else if (P.Id != 0) Human = P.Id;
    TestTrue(TEXT("Hunter can transform"), R.Transform(0));
    TestTrue(TEXT("Role-specific shield activates"), R.ActivateSkill(0, 2.f, 6.f, 1.f, 1.f, .5f));
    const float Before = R.FindParticipant(0)->Health;
    TestTrue(TEXT("Enemy damage enters shield calculation"), R.ApplyDamage(0, Mother, 1000.f));
    TestEqual(TEXT("Shield absorbs configured fraction"), R.FindParticipant(0)->Health, Before - 500.f);
    TestTrue(TEXT("Freeze applies only to enemy"), R.FreezeParticipant(Mother, 0, 2.f));
    TestFalse(TEXT("Freeze rejects friendly participant"), R.FreezeParticipant(Human, 0, 2.f));
    TestFalse(TEXT("Frozen zombie cannot infect through damage"), R.ApplyDamage(Human, Mother, 1.f));
    TestFalse(TEXT("Frozen zombie cannot bypass damage via Infect"), R.Infect(Human, Mother));
    TestFalse(TEXT("Frozen zombie cannot activate skill"), R.ActivateSkill(Mother));
    R.Tick(2.f);
    TestEqual(TEXT("Freeze expires"), R.FindParticipant(Mother)->FrozenSeconds, 0.f);
    TestEqual(TEXT("Shield expires"), R.FindParticipant(0)->ShieldReduction, 0.f);
    TestFalse(TEXT("Shield expiry does not bypass role cooldown"), R.ActivateSkill(0));
    R.ApplyDamage(0, Mother, 1000.f);
    TestEqual(TEXT("Damage after expiry is unreduced"), R.FindParticipant(0)->Health, Before - 1500.f);
    R.ApplyDamage(Mother, 0, 100000.f, true);
    TestEqual(TEXT("Confirmed kill increments attacker score"), R.FindParticipant(0)->Kills, 1);
    TestEqual(TEXT("Death increments victim score"), R.FindParticipant(Mother)->Deaths, 1);
    R.StartRound(4);
    TestEqual(TEXT("Restart clears status"), R.FindParticipant(0)->ShieldRemaining + R.FindParticipant(0)->FrozenSeconds, 0.f);
    TestEqual(TEXT("Restart clears score"), R.FindParticipant(0)->Kills, 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosBioMechanicalEnergyTest,
    "Helios.BioHotel.Rules.MechanicalEnergy", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FHeliosBioMechanicalEnergyTest::RunTest(const FString& Parameters)
{
    FHeliosBioRules R;int32 Mother;
    if(!TestTrue(TEXT("Mechanical role earns blue-box qualification"),PrepareRuleHunter(R,TEXT("BioMechanicalHunter"),Mother)))return false;
    TestTrue(TEXT("A full shot budget consumes all energy"),R.ConsumeRoleWeaponEnergy(0,100.f,1.f,20.f,100.f));
    TestEqual(TEXT("Exhaustion reaches zero"),R.FindParticipant(0)->WeaponEnergy,0.f);
    TestFalse(TEXT("Empty energy blocks firing"),R.ConsumeRoleWeaponEnergy(0,1.f,1.f,20.f,100.f));
    // Binary-exact half-second fixtures verify the split without decimal float rounding hiding the result.
    R.Tick(.5f);TestEqual(TEXT("No recharge before delay"),R.FindParticipant(0)->WeaponEnergy,0.f);
    TestEqual(TEXT("Active tick consumes exactly half the pending recharge delay"),R.FindParticipant(0)->WeaponRechargeDelay,.5f);
    R.Tick(1.f);TestEqual(TEXT("A tick crossing the delay recharges only its remaining fraction"),R.FindParticipant(0)->WeaponEnergy,10.f);
    TestEqual(TEXT("Crossing tick consumes the pending recharge delay"),R.FindParticipant(0)->WeaponRechargeDelay,0.f);
    R.Tick(5.f);TestEqual(TEXT("Recovery stops at capacity"),R.FindParticipant(0)->WeaponEnergy,100.f);
    R.ConsumeRoleWeaponEnergy(0,10.f,2.f,20.f,100.f);R.Tick(1.f);
    TestEqual(TEXT("First recharge delay holds energy"),R.FindParticipant(0)->WeaponEnergy,90.f);
    R.ConsumeRoleWeaponEnergy(0,10.f,2.f,20.f,100.f);R.Tick(1.5f);
    TestEqual(TEXT("A new shot restarts the recharge delay"),R.FindParticipant(0)->WeaponEnergy,80.f);
    R.Tick(.75f);TestEqual(TEXT("Recovery resumes after the restarted delay"),R.FindParticipant(0)->WeaponEnergy,85.f);
    TestFalse(TEXT("A reduced capacity cannot pay an oversized shot using stale energy"),R.ConsumeRoleWeaponEnergy(0,20.f,1.f,20.f,10.f));
    TestEqual(TEXT("Rejected capacity/cost combination leaves energy unchanged"),R.FindParticipant(0)->WeaponEnergy,85.f);
    TestTrue(TEXT("A valid shot clamps available stock to its declared capacity"),R.ConsumeRoleWeaponEnergy(0,10.f,1.f,20.f,10.f));
    TestEqual(TEXT("Clamped shot cannot leave excess stock"),R.FindParticipant(0)->WeaponEnergy,0.f);R.Tick(2.f);
    TestEqual(TEXT("Declared small capacity controls recovery"),R.FindParticipant(0)->WeaponEnergy,10.f);
    TestFalse(TEXT("Zero cost cannot make free shots"),R.ConsumeRoleWeaponEnergy(0,0.f,1.f,20.f,100.f));
    TestFalse(TEXT("Negative recharge settings are rejected"),R.ConsumeRoleWeaponEnergy(0,1.f,-1.f,20.f,100.f));
    TestTrue(TEXT("Opponent freeze enters actual state"),R.FreezeParticipant(0,Mother,1.f));
    TestFalse(TEXT("Frozen mechanical hunter cannot consume energy"),R.ConsumeRoleWeaponEnergy(0,1.f,1.f,20.f,100.f));
    R.Tick(1.f);R.ApplyDamage(0,Mother,100000.f);
    TestFalse(TEXT("Dead mechanical hunter cannot fire"),R.ConsumeRoleWeaponEnergy(0,1.f,1.f,20.f,100.f));
    R.StartRound(8);TestEqual(TEXT("Restart resets energy"),R.FindParticipant(0)->WeaponEnergy,100.f);
    TestEqual(TEXT("Restart resets recharge delay"),R.FindParticipant(0)->WeaponRechargeDelay,0.f);
    TestFalse(TEXT("Ordinary human cannot use mechanical energy"),R.ConsumeRoleWeaponEnergy(0,1.f,1.f,20.f,100.f));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosBioGhostMaskTest,
    "Helios.BioHotel.Rules.GhostMaskSwordWave", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FHeliosBioGhostMaskTest::RunTest(const FString& Parameters)
{
    FHeliosBioRules R;int32 Mother;
    if(!TestTrue(TEXT("Zhong Xiao Kui earns its role"),PrepareRuleHunter(R,TEXT("BioZhongXiaoKuiHunter"),Mother)))return false;
    TArray<int32> Others;for(const auto& P:R.GetParticipants())if(P.Team==EHeliosBioTeam::Human&&P.Id!=0)Others.Add(P.Id);
    for(int32 Id:Others)R.Infect(Id,Mother);
    TestTrue(TEXT("F cooldown starts independently"),R.ActivateSkill(0,.1f,10.f,1.f,1.f,0.f));
    TestTrue(TEXT("G activates ghost mask"),R.ActivateSecondarySkill(0,8.f,15.f,1.4f,1.75f,true));
    TestTrue(TEXT("Ghost mask begins at configured speed"),FMath::IsNearlyEqual(R.FindParticipant(0)->MovementSkillMultiplier(),1.4f));
    TestTrue(TEXT("First attack earns exactly one sword wave"),R.ConsumeSwordWave(0,false));
    TestFalse(TEXT("Light attack cannot duplicate first wave"),R.ConsumeSwordWave(0,false));
    TestFalse(TEXT("Heavy attack requires a melee kill before another wave"),R.ConsumeSwordWave(0,true));
    TestFalse(TEXT("G cannot bypass its separate cooldown"),R.ActivateSecondarySkill(0,8.f,15.f,1.4f,1.75f,true));
    R.Tick(2.f);TestTrue(TEXT("Ghost mask speed decays proportionally with remaining time"),FMath::IsNearlyEqual(R.FindParticipant(0)->MovementSkillMultiplier(),1.3f));
    const float Before=R.FindParticipant(Mother)->Health;R.ApplyDamage(Mother,0,10.f,false);
    TestTrue(TEXT("Ghost mask applies real configured damage boost"),FMath::IsNearlyEqual(Before-R.FindParticipant(Mother)->Health,17.5f));
    R.ApplyDamage(Mother,0,100000.f,true);
    TestEqual(TEXT("Melee kill extends active mask"),R.FindParticipant(0)->SecondarySkillRemaining,8.f);
    TestTrue(TEXT("Extending mask cannot restart the initial speed boost"),FMath::IsNearlyEqual(R.FindParticipant(0)->MovementSkillMultiplier(),1.3f));
    TestEqual(TEXT("Kill reduces F cooldown"),R.FindParticipant(0)->SkillCooldownRemaining,6.f);
    TestEqual(TEXT("Kill reduces G cooldown independently"),R.FindParticipant(0)->SecondaryCooldownRemaining,11.f);
    TestTrue(TEXT("Melee kill refreshes one heavy-wave charge"),R.FindParticipant(0)->bHeavySwordWaveReady);
    TestFalse(TEXT("Refreshed heavy charge cannot be spent by a light attack"),R.ConsumeSwordWave(0,false));
    TestTrue(TEXT("Next heavy attack consumes the refreshed sword wave"),R.ConsumeSwordWave(0,true));
    TestFalse(TEXT("Heavy charge cannot be spent twice"),R.ConsumeSwordWave(0,true));
    if(Others.Num()){const float Remaining=R.FindParticipant(0)->SecondarySkillRemaining;R.ApplyDamage(Others[0],0,100000.f,false);
        TestEqual(TEXT("Non-melee kill cannot extend mask"),R.FindParticipant(0)->SecondarySkillRemaining,Remaining);TestFalse(TEXT("Non-melee kill cannot refresh heavy wave"),R.FindParticipant(0)->bHeavySwordWaveReady);}
    R.Tick(8.f);TestFalse(TEXT("Expired mask clears active flag"),R.FindParticipant(0)->bGhostMaskActive);
    TestFalse(TEXT("Expired mask cannot fire any sword wave"),R.ConsumeSwordWave(0,true));
    TestEqual(TEXT("Expired mask restores ordinary speed"),R.FindParticipant(0)->MovementSkillMultiplier(),1.f);
    TestFalse(TEXT("Mask expiry alone does not bypass remaining G cooldown"),R.ActivateSecondarySkill(0,8.f,15.f,1.4f,1.75f,true));
    R.Tick(1.f);TestTrue(TEXT("Ghost mask can reactivate after actual cooldown"),R.ActivateSecondarySkill(0,8.f,15.f,1.4f,1.75f,true));
    R.StartRound(8);TestFalse(TEXT("Restart clears mask and queued waves"),R.FindParticipant(0)->bGhostMaskActive||R.FindParticipant(0)->bFirstAttackSwordWave||R.FindParticipant(0)->bHeavySwordWaveReady);
    TestEqual(TEXT("Restart clears G timers"),R.FindParticipant(0)->SecondarySkillRemaining+R.FindParticipant(0)->SecondaryCooldownRemaining,0.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosBioArcaneDevolutionTest,
    "Helios.BioHotel.Rules.ArcaneDevolutionKeepsScores", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FHeliosBioArcaneDevolutionTest::RunTest(const FString& Parameters)
{
    FHeliosBioRules R;R.Config.PreparationSeconds=0.f;R.Config.InitialMotherCount=1;R.Config.HunterThreshold=8;R.Config.InfectionsPerEvolution=1;R.Config.MaximumZombieEvolution=2;R.StartRound(8);
    int32 Mother=INDEX_NONE;TArray<int32> Humans;for(const auto& P:R.GetParticipants())if(P.bMother)Mother=P.Id;else if(P.Id!=0)Humans.Add(P.Id);
    R.Transform(0);R.ApplyDamage(0,Mother,125.f);R.Infect(Humans[0],Mother);R.Infect(Humans[1],Mother);R.PickupSupply(Mother);R.PickupSupply(Mother);
    TestEqual(TEXT("Mother has historical infection score"),R.FindParticipant(Mother)->Infections,2);
    TestEqual(TEXT("Mother has independent current infection progress"),R.FindParticipant(Mother)->EvolutionInfections,2);
    TestEqual(TEXT("Infections and supplies reach maximum evolution"),R.FindParticipant(Mother)->EvolutionLevel,2);
    const float Dealt=R.FindParticipant(Mother)->DamageDealt;const int32 Supplies=R.FindParticipant(Mother)->SuppliesCollected;
    TestTrue(TEXT("Mother has nonzero historical damage to preserve"),Dealt>0);
    TestFalse(TEXT("Arcane cannot devolve a living target"),R.DevolveDefeatedZombie(Mother));
    R.ApplyDamage(Mother,0,100000.f,false);TestTrue(TEXT("Arcane lethal damage permits temporary zombie death"),!R.FindParticipant(Mother)->bAlive&&!R.FindParticipant(Mother)->bPermanentlyDead);
    TestTrue(TEXT("Defeated zombie can be devolved"),R.DevolveDefeatedZombie(Mother));
    TestEqual(TEXT("Devolution clears current level"),R.FindParticipant(Mother)->EvolutionLevel,0);
    TestEqual(TEXT("Devolution clears current infection progress"),R.FindParticipant(Mother)->EvolutionInfections,0);
    TestEqual(TEXT("Devolution clears current supply progress"),R.FindParticipant(Mother)->ZombieSuppliesCollected,0);
    TestEqual(TEXT("Devolution preserves historical infections"),R.FindParticipant(Mother)->Infections,2);
    TestEqual(TEXT("Devolution preserves historical supplies"),R.FindParticipant(Mother)->SuppliesCollected,Supplies);
    TestEqual(TEXT("Devolution preserves death score"),R.FindParticipant(Mother)->Deaths,1);
    TestEqual(TEXT("Devolution preserves historical dealt damage"),R.FindParticipant(Mother)->DamageDealt,Dealt);
    TestEqual(TEXT("Mother loses its evolution HP bonus"),R.FindParticipant(Mother)->MaxHealth,R.Config.MotherHealth);
    R.Tick(R.Config.RespawnSeconds);TestTrue(TEXT("Devolved zombie respawns with its reset maximum HP"),R.FindParticipant(Mother)->bAlive&&R.FindParticipant(Mother)->Health==R.Config.MotherHealth);
    R.Infect(Humans[2],Mother);TestEqual(TEXT("One new infection does not restore old progression"),R.FindParticipant(Mother)->EvolutionLevel,1);
    TestEqual(TEXT("New infection still advances historical score"),R.FindParticipant(Mother)->Infections,3);
    R.Transform(0);R.ApplyDamage(Mother,0,100000.f,true);
    TestFalse(TEXT("Arcane cannot downgrade a permanently eliminated zombie"),R.DevolveDefeatedZombie(Mother));
    R.StartRound(8);TestEqual(TEXT("Only a new round clears infection history"),R.FindParticipant(0)->Infections,0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosBioSecondaryLifeResetTest,
    "Helios.BioHotel.Rules.SecondaryDeathAndStatusGuards", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FHeliosBioSecondaryLifeResetTest::RunTest(const FString& Parameters)
{
    FHeliosBioRules R;int32 Mother;
    if(!TestTrue(TEXT("Role is earned before status test"),PrepareRuleHunter(R,TEXT("BioZhongXiaoKuiHunter"),Mother)))return false;
    TestFalse(TEXT("Negative G duration is invalid"),R.ActivateSecondarySkill(0,-1.f,15.f));
    TestFalse(TEXT("Negative G cooldown is invalid"),R.ActivateSecondarySkill(0,8.f,-1.f));
    TestTrue(TEXT("A frozen hunter is a real opposing target"),R.FreezeParticipant(0,Mother,1.f));
    TestFalse(TEXT("Freeze prevents ghost mask activation"),R.ActivateSecondarySkill(0,8.f,15.f,1.4f,1.75f,true));R.Tick(1.f);
    R.ActivateSkill(0,4.f,15.f,1.3f,1.2f,.5f);R.ActivateSecondarySkill(0,8.f,15.f,1.4f,1.75f,true);R.ApplyDamage(0,Mother,100000.f);
    const auto* Dead=R.FindParticipant(0);TestFalse(TEXT("Death clears mask and queued first/heavy wave"),Dead->bGhostMaskActive||Dead->bFirstAttackSwordWave||Dead->bHeavySwordWaveReady);
    TestEqual(TEXT("Death clears active shield and buffs"),Dead->SkillRemaining+Dead->ShieldRemaining+Dead->ShieldReduction+Dead->SecondarySkillRemaining,0.f);
    TestTrue(TEXT("Death preserves both cooldowns"),Dead->SkillCooldownRemaining>0&&Dead->SecondaryCooldownRemaining>0);
    TestFalse(TEXT("Dead participant cannot consume sword wave"),R.ConsumeSwordWave(0,false));TestFalse(TEXT("Dead participant cannot activate G"),R.ActivateSecondarySkill(0,8.f,15.f));
    R.StartRound(8);TestFalse(TEXT("Human cannot activate hunter G"),R.ActivateSecondarySkill(0,8.f,15.f));
    PrepareRuleHunter(R,TEXT("BioZhongXiaoKuiHunter"),Mother);TestTrue(TEXT("Zero-duration utility G can start a cooldown"),R.ActivateSecondarySkill(0,0.f,15.f,1.4f,1.75f,true));
    TestFalse(TEXT("Zero-duration effect creates no queued ghost-mask attacks"),R.FindParticipant(0)->bGhostMaskActive||R.FindParticipant(0)->bFirstAttackSwordWave);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosBioRescueSerumCureTest,
    "Helios.BioHotel.Rules.RescueSerum.CureZombie", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FHeliosBioRescueSerumCureTest::RunTest(const FString& Parameters)
{
    // Pure-rule numeric fixtures establish histories/statuses through real public
    // rules transitions; they do not qualify weapon assets, NPC loadouts or balance.
    for(auto Form:{EHeliosBioZombieForm::Normal,EHeliosBioZombieForm::Terminator,EHeliosBioZombieForm::Abyss})
    {
        FHeliosBioRules R;int32 Mother;R.Config.HumanHealth=137.f;R.Config.bMothersAreTerminators=Form!=EHeliosBioZombieForm::Normal;R.Config.bAbyssSourceAvailable=true;R.Config.AbyssRoleId=TEXT("BioAbyssTerminator");
        if(!TestTrue(TEXT("Serum caster earns actual blue-box Redeemer identity"),PrepareRuleHunter(R,TEXT("BioRedeemerHunter"),Mother)))return false;
        TArray<int32> Humans;for(const auto& P:R.GetParticipants())if(P.Team==EHeliosBioTeam::Human&&P.Id!=0)Humans.Add(P.Id);
        if(!TestTrue(TEXT("History fixture retains multiple other living humans"),Humans.Num()>=4))return false;
        TestTrue(TEXT("First real infection contributes historical score"),R.ApplyDamage(Humans[0],Mother,1.f));TestTrue(TEXT("Second real infection supplies another zombie"),R.ApplyDamage(Humans[1],Mother,1.f));
        TestTrue(TEXT("Historical victim becomes a real ghost hunter"),R.Transform(Humans[2]));TestTrue(TEXT("Mother really damages and eliminates that hunter"),R.ApplyDamage(Humans[2],Mother,100000.f));
        TestTrue(TEXT("Mother really dies temporarily before its later cure"),R.ApplyDamage(Mother,0,100000.f,false));
        // Advance exactly the ordinary pending respawn transition, never fake a
        // serum shot, cooldown readiness or state assignment with oversized Tick.
        R.Tick(R.Config.RespawnSeconds);TestTrue(TEXT("Historical mother has actually respawned alive"),R.FindParticipant(Mother)->bAlive);
        for(int32 I=0;I<4;++I)TestTrue(TEXT("Real zombie supply creates progression and supply history"),R.PickupSupply(Mother));
        if(Form==EHeliosBioZombieForm::Abyss)TestTrue(TEXT("Original mother/red-box permissions actually evolve Abyss"),R.PickupSupply(Mother,EHeliosBioSupplyKind::RedTerminatorEvolution));
        TestTrue(TEXT("Actual target form matches independent fixture branch"),R.FindParticipant(Mother)->ZombieForm==Form);
        TestTrue(TEXT("Living zombie activates an actual expiring primary status"),R.ActivateSkill(Mother,4.f,15.f,1.3f,1.2f,.5f));TestTrue(TEXT("Opponent really freezes the curable zombie"),R.FreezeParticipant(Mother,0,2.f));
        const auto Before=*R.FindParticipant(Mother),CasterBefore=*R.FindParticipant(0);TestTrue(TEXT("Histories are nonzero before cure"),Before.Infections>0&&Before.Kills>0&&Before.Deaths>0&&Before.DamageDealt>0&&Before.SuppliesCollected>0&&Before.EvolutionLevel>0);
        TestTrue(TEXT("Real serum API cures the live zombie"),R.ApplyRescueSerum(Mother,0)==EHeliosBioSerumResult::CuredZombie);const auto* P=R.FindParticipant(Mother);
        TestTrue(TEXT("Cure restores a live ordinary human at configured HP"),P->Team==EHeliosBioTeam::Human&&P->bAlive&&!P->bPermanentlyDead&&P->Health==137.f&&P->MaxHealth==137.f);
        TestTrue(TEXT("Cure clears mother, progression, roles and respawn eligibility"),!P->bMother&&P->EvolutionLevel==0&&P->EvolutionInfections==0&&P->ZombieSuppliesCollected==0&&P->ZombieForm==EHeliosBioZombieForm::Normal&&P->ZombieRoleId.IsNone()&&P->HunterRoleId.IsNone()&&!P->bHunterChoicePending&&P->RespawnSeconds==0);
        TestTrue(TEXT("Cure clears both skills, shield, freeze and queued attacks"),P->SkillRemaining==0&&P->SkillCooldownRemaining==0&&P->FrozenSeconds==0&&P->ShieldRemaining==0&&P->ShieldReduction==0&&P->SkillSpeedMultiplier==1&&P->SkillDamageMultiplier==1&&P->SecondarySkillRemaining==0&&P->SecondaryCooldownRemaining==0&&P->SecondarySkillDuration==0&&P->SecondarySpeedRemaining==0&&P->SecondarySpeedMultiplier==1&&P->SecondaryDamageMultiplier==1&&!P->bGhostMaskActive&&!P->bFirstAttackSwordWave&&!P->bHeavySwordWaveReady);
        TestTrue(TEXT("Cure preserves every historical score and supply counter"),P->Infections==Before.Infections&&P->Kills==Before.Kills&&P->Deaths==Before.Deaths&&P->DamageDealt==Before.DamageDealt&&P->SuppliesCollected==Before.SuppliesCollected&&P->BlueSuppliesCollected==Before.BlueSuppliesCollected);
        TestTrue(TEXT("Cure does not mutate caster or force round end while another zombie remains"),R.FindParticipant(0)->Health==CasterBefore.Health&&SameSerumStateExceptHealth(CasterBefore,*R.FindParticipant(0))&&R.GetPhase()==EHeliosBioPhase::Active);
        TestTrue(TEXT("Ordinary infected non-mother is also curable"),R.ApplyRescueSerum(Humans[1],0)==EHeliosBioSerumResult::CuredZombie);TestFalse(TEXT("Cured non-mother gains no mother identity"),R.FindParticipant(Humans[1])->bMother);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosBioRescueSerumHealTest,
    "Helios.BioHotel.Rules.RescueSerum.HealHuntersPreservesState", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FHeliosBioRescueSerumHealTest::RunTest(const FString& Parameters)
{
    for(FName Role:{FName(NAME_None),FName(TEXT("BioZhongXiaoKuiHunter")),FName(TEXT("BioMechanicalHunter"))})
    {
        FHeliosBioRules R;int32 Mother;R.Config.AllowedHunterRoles.Add(Role);
        if(!TestTrue(TEXT("Independent healer earns actual Redeemer role"),PrepareRuleHunter(R,TEXT("BioRedeemerHunter"),Mother)))return false;
        TArray<int32> Humans;for(const auto& P:R.GetParticipants())if(P.Team==EHeliosBioTeam::Human&&P.Id!=0)Humans.Add(P.Id);const int32 Target=Humans[0];
        TestTrue(TEXT("Healing target really becomes ghost hunter"),R.Transform(Target));TestTrue(TEXT("Target earns real pending blue choice"),R.PickupSupply(Target,EHeliosBioSupplyKind::BlueHunterUpgrade));
        if(!Role.IsNone())TestTrue(TEXT("Ultimate target selects its distinct real allowed role"),R.ConfirmHunterChoice(Target,Role));
        TestTrue(TEXT("Target has nonzero primary status and cooldown"),R.ActivateSkill(Target,4.f,15.f,1.2f,1.3f,.5f));
        TestTrue(TEXT("Target has independent real secondary status/cooldown"),R.ActivateSecondarySkill(Target,8.f,15.f,1.4f,1.75f,Role==TEXT("BioZhongXiaoKuiHunter")));
        if(Role==TEXT("BioZhongXiaoKuiHunter")){R.Infect(Humans[1],Mother);TestTrue(TEXT("Real mask melee kill queues heavy wave and changes historical score"),R.ApplyDamage(Humans[1],Target,100000.f,true));TestTrue(TEXT("Mask first and heavy waves remain genuinely queued"),R.FindParticipant(Target)->bFirstAttackSwordWave&&R.FindParticipant(Target)->bHeavySwordWaveReady);}
        if(Role==TEXT("BioMechanicalHunter"))TestTrue(TEXT("Mechanical target genuinely spends energy"),R.ConsumeRoleWeaponEnergy(Target,10.f,2.f,20.f,100.f));
        TestTrue(TEXT("Real zombie hit makes target HP lower than maximum"),R.ApplyDamage(Target,Mother,100.f));TestTrue(TEXT("Real freeze status also survives healing"),R.FreezeParticipant(Target,Mother,1.f));
        const auto Before=*R.FindParticipant(Target),CasterBefore=*R.FindParticipant(0);TestTrue(TEXT("Healing fixture has real missing HP and both active cooldowns"),Before.Health<Before.MaxHealth&&Before.SkillCooldownRemaining>0&&Before.SecondaryCooldownRemaining>0);
        TestTrue(TEXT("Serum returns the hunter healing branch"),R.ApplyRescueSerum(Target,0)==EHeliosBioSerumResult::HealedHunter);const auto& After=*R.FindParticipant(Target);
        TestEqual(TEXT("Hunter heals exactly to its own maximum HP"),After.Health,Before.MaxHealth);
        TestTrue(TEXT("Every other hunter field remains exactly equal by value"),SameSerumStateExceptHealth(Before,After));
        TestTrue(TEXT("Healing does not spend/reset caster state itself"),R.FindParticipant(0)->Health==CasterBefore.Health&&SameSerumStateExceptHealth(CasterBefore,*R.FindParticipant(0)));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosBioRescueSerumGuardsTest,
    "Helios.BioHotel.Rules.RescueSerum.PermissionPhaseAndDeathGuards", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FHeliosBioRescueSerumGuardsTest::RunTest(const FString& Parameters)
{
    FHeliosBioRules Base;int32 Mother;if(!TestTrue(TEXT("Guard baseline earns a living real Redeemer"),PrepareRuleHunter(Base,TEXT("BioRedeemerHunter"),Mother)))return false;
    TArray<int32> Humans;for(const auto& P:Base.GetParticipants())if(P.Team==EHeliosBioTeam::Human&&P.Id!=0)Humans.Add(P.Id);const int32 Human=Humans[0];
    SerumRejectedWithoutMutation(*this,Base,-1,0,TEXT("Invalid target"));SerumRejectedWithoutMutation(*this,Base,Mother,-1,TEXT("Invalid caster"));SerumRejectedWithoutMutation(*this,Base,999,0,TEXT("Missing target"));SerumRejectedWithoutMutation(*this,Base,Mother,999,TEXT("Missing caster"));
    SerumRejectedWithoutMutation(*this,Base,Human,0,TEXT("Ordinary human target"));SerumRejectedWithoutMutation(*this,Base,Human,Human,TEXT("Ordinary human self rescue"));SerumRejectedWithoutMutation(*this,Base,Mother,Human,TEXT("Human cannot administer serum"));SerumRejectedWithoutMutation(*this,Base,Human,Mother,TEXT("Zombie cannot administer serum"));
    {auto R=Base;R.ApplyDamage(0,Mother,10.f);SerumRejectedWithoutMutation(*this,R,0,0,TEXT("Wounded Redeemer cannot heal itself"));}
    {auto R=Base;R.Transform(Human);SerumRejectedWithoutMutation(*this,R,Mother,Human,TEXT("Unselected ghost has no Redeemer identity"));}
    {auto R=Base;R.Config.AllowedHunterRoles.Add(TEXT("BioMechanicalHunter"));R.Transform(Human);R.PickupSupply(Human,EHeliosBioSupplyKind::BlueHunterUpgrade);R.ConfirmHunterChoice(Human,TEXT("BioMechanicalHunter"));SerumRejectedWithoutMutation(*this,R,Mother,Human,TEXT("Different actual ultimate role cannot administer serum"));}
    {auto R=Base;R.FreezeParticipant(0,Mother,1.f);SerumRejectedWithoutMutation(*this,R,Mother,0,TEXT("Frozen real Redeemer cannot administer serum"));}
    {auto R=Base;R.ApplyDamage(0,Mother,100000.f);TestTrue(TEXT("Caster truly dies and retains its role for death pose"),!R.FindParticipant(0)->bAlive&&R.FindParticipant(0)->HunterRoleId==TEXT("BioRedeemerHunter"));SerumRejectedWithoutMutation(*this,R,Mother,0,TEXT("Dead qualified caster"));}
    {auto R=Base;R.ApplyDamage(Mother,0,100000.f,false);TestTrue(TEXT("Target has actual temporary zombie death"),!R.FindParticipant(Mother)->bAlive&&!R.FindParticipant(Mother)->bPermanentlyDead);SerumRejectedWithoutMutation(*this,R,Mother,0,TEXT("Temporary dead zombie cannot be resurrected by serum"));}
    {auto R=Base;R.Infect(Human,Mother);R.ApplyDamage(Mother,0,100000.f,true);TestTrue(TEXT("Permanently dead zombie fixture retains an active round"),R.GetPhase()==EHeliosBioPhase::Active&&R.FindParticipant(Mother)->bPermanentlyDead);SerumRejectedWithoutMutation(*this,R,Mother,0,TEXT("Permanently eliminated zombie"));}
    {auto R=Base;R.Transform(Human);R.ApplyDamage(Human,Mother,100000.f);SerumRejectedWithoutMutation(*this,R,Human,0,TEXT("Dead hunter cannot be healed/resurrected"));}
    {auto R=Base;R.Tick(R.GetPhaseTimeRemaining());TestTrue(TEXT("Normal survival deadline genuinely ends the round"),R.GetPhase()==EHeliosBioPhase::RoundOver);SerumRejectedWithoutMutation(*this,R,Mother,0,TEXT("Round-over serum"));}
    {FHeliosBioRules R;R.Config.PreparationSeconds=20.f;R.StartRound(8);SerumRejectedWithoutMutation(*this,R,1,0,TEXT("Preparation serum"));}
    return true;
}
#endif
