// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A summoner's minion attack speed reaching what it commands. Issue #898.
 *
 * WHAT WAS WRONG. `game/Data/Affixes.csv` grants `minion_attack_speed` on five
 * gear slots, worth 15 per cent at tier 7 and gear level +10 -- the best a player
 * can find -- and no gameplay attribute for any minion stat exists
 * anywhere in the project. The two passes that fill a character's stat line are
 * driven by the stat-to-attribute map, so the stat was resolved by nothing and
 * reached nothing. **A player who found the affix got no benefit and no message.**
 *
 * WHY ATTACK SPEED IS THE FIRST OF THE THREE MINION STATS TO BE BUILT, and it is
 * not that a route existed. It is that the place the answer is USED is shared:
 * `ACataclysmEnemyController` drives a summoned minion and a subjugated enemy
 * alike, and its own comment calls the interval "the one place a minion and a
 * subjugated enemy share: they are different classes with different overrides and
 * one controller". Damage has no such place -- a minion deals a figure from its
 * type row with its summoner as instigator, and a thrall deals its own attack
 * damage with itself as instigator -- so damage needs its own work.
 *
 * **THE FIVE TESTS BELOW DIVIDE THREE WAYS.**
 *
 * TWO ARE THE POINT: a summoned minion and a subjugated enemy each swing faster.
 * That one change reaches both kinds of commanded creature is the whole argument
 * for doing attack speed first, so it is proved on both rather than on a minion
 * and assumed for the other.
 *
 * ONE IS ABOUT WHAT DOES NOT MOVE, because the figure is applied in the
 * controller every creature in the game shares. A creature following nobody must
 * be untouched, and it is measured in a world where another character DOES carry
 * the increase, so that case can fail rather than passing by default.
 *
 * ONE PINS THE TRAP -- that asking this stat for its VALUE rather than its
 * increases returns zero, silently -- AND ONE EXERCISES THE STEP THE OTHER FOUR
 * SUPPLY FOR THEMSELVES, which is gear putting the stat into a character's stat
 * line at all. Without that last one the other four would every one of them pass
 * in a build where no gear could reach a character.
 */
namespace CataclysmMinionAttackSpeedTest
{
	/** Centimetres in a metre, so a case can place a character in metres. */
	constexpr float M = 100.0f;

	/** What the test grants. Not the affix's top roll of 15, so a reading of 15
	 *  here could only have come from the data rather than from this line. */
	constexpr float IncreasePercent = 25.0f;

	/**
	 * The interval scale that increase should produce.
	 *
	 * A SHORTER INTERVAL, NOT A SMALLER ONE BY THE SAME PERCENTAGE, which is the
	 * arithmetic `AttackIntervalScaleFor`'s own comment insists on: 25% more
	 * swings in the same time is an interval of 1 / 1.25 = 0.8, not 0.75. The two
	 * differ and only the first means what the affix says.
	 *
	 * COMPUTED HERE RATHER THAN ASKED OF THE CODE. A test that asks the thing
	 * under test for the expected answer agrees with it however wrong it is.
	 */
	constexpr float ExpectedScale = 1.0f / (1.0f + IncreasePercent / 100.0f);

	static FGenericTeamId PlayersSide()
	{
		return UCataclysmTeams::IdFor(ECataclysmTeam::Players);
	}

	static FGenericTeamId MonstersSide()
	{
		return UCataclysmTeams::IdFor(ECataclysmTeam::Monsters);
	}

	/** A character on the given side, at a distance along X in metres. */
	static ACataclysmEnemyCharacter* SpawnOn(UWorld* World, float Metres,
											 const FGenericTeamId& Side)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(Metres * M, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(Side);
			Made->SetHealth(1000.0f);
			Made->SetAttackDamage(0.0f);
		}
		return Made;
	}

	/**
	 * Give a character the stat line a player would have after equipping gear
	 * that grants increased minion attack speed.
	 *
	 * THROUGH `SetStatInputs`, WHICH IS THE ROUTE THE REAL ONE TAKES.
	 * `UCataclysmPlayerClassStats::ApplyTo` fills that map from gear and
	 * passives and hands it across whole. Writing an attribute instead would
	 * test nothing: the stat HAS no attribute, which is the entire problem.
	 */
	static void GrantMinionAttackSpeed(AActor* Commander, float Percent)
	{
		UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(
				UCataclysmTargeting::AbilitySystemOf(Commander));
		if (!System)
		{
			return;
		}

		FCataclysmStatModifier Increase;
		Increase.Bucket = ECataclysmStatBucket::Increased;
		Increase.Source = ECataclysmModifierSource::GearAffix;
		Increase.Value = Percent;

		TMap<FName, FCataclysmStatInputs> Inputs;
		FCataclysmStatInputs& Line =
			Inputs.FindOrAdd(FName(TEXT("minion_attack_speed")));

		// A BASE OF NOTHING, DELIBERATELY, BECAUSE THAT IS THE REAL CASE. The
		// stat has no base and can have none: a minion's interval comes from its
		// own row in game/Data/MinionTypes.csv. A test that gave it a base would
		// pass against an implementation that reads the stat's VALUE, which
		// returns zero in the game.
		Line.Base = 0.0f;
		Line.Modifiers = {Increase};
		System->SetStatInputs(MoveTemp(Inputs));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionAttackSpeedReachesAMinionTest,
	"Cataclysm.MinionAttackSpeed.ASummonedMinionSwingsFasterForItsSummonersGear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The first half of the claim: it reaches a summoned minion.
 *
 * THE CONTROL IS IN THIS TEST RATHER THAN BESIDE IT. The same minion is measured
 * before the gear is granted and after, so a build where the scale is always 0.8
 * fails the first reading and never reaches the second.
 */
bool FCataclysmMinionAttackSpeedReachesAMinionTest::RunTest(const FString&)
{
	using namespace CataclysmMinionAttackSpeedTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Summoner = SpawnOn(World, 0.0f, PlayersSide());
	ACataclysmEnemyCharacter* Enemy = SpawnOn(World, 3.0f, MonstersSide());
	if (!TestNotNull(TEXT("a summoner"), Summoner)
		|| !TestNotNull(TEXT("an enemy"), Enemy))
	{
		return false;
	}

	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
		Summoner, FVector(1 * M, 0, 0), /*Lifetime=*/20.0f, /*bBurns=*/false,
		TEXT("Imp"));
	if (!TestNotNull(TEXT("an imp"), Imp))
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

	// BEFORE THE GEAR, AND THIS READING IS WHAT MAKES THE NEXT ONE MEAN
	// ANYTHING. A summoner wearing nothing must not hurry its minions.
	TestEqual(TEXT("with no such gear the interval is unchanged"),
			  UCataclysmCommand::AttackIntervalScaleFor(Imp, Enemy), 1.0f, 0.001f);

	GrantMinionAttackSpeed(Summoner, IncreasePercent);

	TestEqual(TEXT("and its summoner's increased minion attack speed shortens it"),
			  UCataclysmCommand::AttackIntervalScaleFor(Imp, Enemy),
			  ExpectedScale, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionAttackSpeedReachesAThrallTest,
	"Cataclysm.MinionAttackSpeed.ASubjugatedEnemySwingsFasterForItsCaptorsGear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The second half, and the reason attack speed was built before damage.
 *
 * A THRALL IS NOT AN `ACataclysmMinion`. It stays the enemy character it was,
 * which the decision of 2026-09-02, "an enemy can be taken rather than
 * summoned", records as deliberate: "Nothing is destroyed and nothing is
 * spawned", because replacing the creature with a minion "would have given
 * every thrall in the game the same three attacks". So this passing is not
 * implied by the minion test passing -- it is a different class reaching the
 * same answer through the one place the two share.
 */
bool FCataclysmMinionAttackSpeedReachesAThrallTest::RunTest(const FString&)
{
	using namespace CataclysmMinionAttackSpeedTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Captor = SpawnOn(World, 0.0f, PlayersSide());
	ACataclysmEnemyCharacter* Taken = SpawnOn(World, 3.0f, MonstersSide());
	ACataclysmEnemyCharacter* Enemy = SpawnOn(World, 6.0f, MonstersSide());
	if (!TestNotNull(TEXT("a captor"), Captor)
		|| !TestNotNull(TEXT("something to take"), Taken)
		|| !TestNotNull(TEXT("an enemy"), Enemy))
	{
		return false;
	}

	if (!TestTrue(TEXT("it can be taken"),
				  UCataclysmCommand::Subjugate(Captor, Taken)))
	{
		return false;
	}

	// AND IT IS A THRALL RATHER THAN A MINION, asserted rather than assumed,
	// because the whole value of this test is that it covers the other class.
	TestNull(TEXT("a thrall is not an ACataclysmMinion"),
			 Cast<ACataclysmMinion>(Taken));

	TestEqual(TEXT("with no such gear the interval is unchanged"),
			  UCataclysmCommand::AttackIntervalScaleFor(Taken, Enemy), 1.0f,
			  0.001f);

	GrantMinionAttackSpeed(Captor, IncreasePercent);

	TestEqual(TEXT("and its captor's increased minion attack speed shortens it"),
			  UCataclysmCommand::AttackIntervalScaleFor(Taken, Enemy),
			  ExpectedScale, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionAttackSpeedLeavesOthersAloneTest,
	"Cataclysm.MinionAttackSpeed.ACreatureFollowingNobodyIsUntouched",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * What did NOT move, which is the evidence that matters for a change applied in
 * the controller every creature in the game shares.
 *
 * THE FIXTURE IS BUILT SO THIS CAN FAIL. The wandering creature stands in a
 * world where another character DOES carry increased minion attack speed, so an
 * implementation that read the stat off the wrong character, or off any
 * character it could find, would shorten this one's interval too.
 */
bool FCataclysmMinionAttackSpeedLeavesOthersAloneTest::RunTest(const FString&)
{
	using namespace CataclysmMinionAttackSpeedTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Commander = SpawnOn(World, 0.0f, PlayersSide());
	ACataclysmEnemyCharacter* Wanderer = SpawnOn(World, 3.0f, MonstersSide());
	ACataclysmEnemyCharacter* Enemy = SpawnOn(World, 6.0f, PlayersSide());
	if (!TestNotNull(TEXT("a commander"), Commander)
		|| !TestNotNull(TEXT("a creature following nobody"), Wanderer)
		|| !TestNotNull(TEXT("something for it to hit"), Enemy))
	{
		return false;
	}

	GrantMinionAttackSpeed(Commander, IncreasePercent);

	TestNull(TEXT("the wandering creature follows nobody"),
			 UCataclysmCommand::CommanderOf(Wanderer));

	TestEqual(TEXT("so nothing shortens its interval"),
			  UCataclysmCommand::AttackIntervalScaleFor(Wanderer, Enemy), 1.0f,
			  0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionAttackSpeedNeedsTheStatTest,
	"Cataclysm.MinionAttackSpeed.AskingForTheStatsValueWouldReturnNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The trap this change exists to avoid, pinned so nobody undoes it.
 *
 * `minion_attack_speed` HAS NO BASE AND CAN HAVE NONE. The pipeline computes
 * `(base + flat) * (1 + increases)`, so asking for the stat's VALUE returns zero
 * however much gear the summoner wears -- silently, with every other test still
 * passing. That is why `IncreasesForStat` exists and why it returns the
 * increases rather than the value.
 *
 * SO THIS TEST ASSERTS THE WRONG ANSWER IS STILL WRONG. A later change that
 * "simplified" the accessor into `StatForSkill` would pass the three tests above
 * only if it also invented a base, and would fail this one.
 */
bool FCataclysmMinionAttackSpeedNeedsTheStatTest::RunTest(const FString&)
{
	using namespace CataclysmMinionAttackSpeedTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Commander = SpawnOn(World, 0.0f, PlayersSide());
	if (!TestNotNull(TEXT("a commander"), Commander))
	{
		return false;
	}

	GrantMinionAttackSpeed(Commander, IncreasePercent);

	UCataclysmAbilitySystemComponent* System =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Commander));
	if (!TestNotNull(TEXT("its ability system"), System))
	{
		return false;
	}

	const FName Stat(TEXT("minion_attack_speed"));

	// THE FIGURE THE CHANGE USES: the increases alone, as a fraction.
	TestEqual(TEXT("the increases are there and are a fraction"),
			  System->IncreasesForStat(Stat, FGameplayTagContainer()),
			  IncreasePercent / 100.0f, 0.001f);

	// AND THE FIGURE IT MUST NOT USE: the stat's value, which is nothing,
	// because an increase against a base of nothing is nothing.
	TestEqual(TEXT("while the stat's own value is zero, as it always will be"),
			  System->StatForSkill(Stat, FGameplayTagContainer(), 0.0f), 0.0f,
			  0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionAttackSpeedFromGearTest,
	"Cataclysm.MinionAttackSpeed.GearGrantingItReachesTheCharactersStatLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The step the four tests above SUPPLY rather than exercise, and without this
 * they prove nothing about the game.
 *
 * EVERY TEST ABOVE CALLS `SetStatInputs` ITSELF. That is the right fixture for
 * what each of them is about -- the accessor, and the interval -- but it means
 * they would all pass in a build where no gear could ever put
 * `minion_attack_speed` into a character's stat line, which is exactly the
 * state this change exists to end. A test that supplies the missing step proves
 * nothing about the step.
 *
 * SO THIS ONE GOES THROUGH `UCataclysmPlayerClassStats::ApplyTo`, the function a
 * real gear change calls, and asserts the stat arrives. Before this change it
 * did not: both passes in that function are driven by the stat-to-attribute
 * map, and no gameplay attribute for any minion stat exists.
 *
 * THE CONTROL IS A STAT THAT DOES HAVE AN ATTRIBUTE. `attack_damage` is granted
 * in the same call, so a build where `ApplyTo` recorded nothing at all fails
 * both readings rather than looking like a minion-specific fault.
 */
bool FCataclysmMinionAttackSpeedFromGearTest::RunTest(const FString&)
{
	using namespace CataclysmMinionAttackSpeedTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Wearer = SpawnOn(World, 0.0f, PlayersSide());
	if (!TestNotNull(TEXT("a character"), Wearer))
	{
		return false;
	}

	UCataclysmAbilitySystemComponent* System =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Wearer));
	if (!TestNotNull(TEXT("its ability system"), System))
	{
		return false;
	}

	FCataclysmStatModifier Increase;
	Increase.Bucket = ECataclysmStatBucket::Increased;
	Increase.Source = ECataclysmModifierSource::GearAffix;
	Increase.Value = IncreasePercent;

	// WHAT `UCataclysmItemModifiers::AccumulateInto` HANDS OVER when the gear is
	// worn: a map of stat name to the modifiers granting it. The affix row
	// `Stat_Increased_minion_attack_speed` produces exactly this shape.
	TMap<FName, TArray<FCataclysmStatModifier>> FromGear;
	FromGear.Add(FName(TEXT("minion_attack_speed")), {Increase});
	FromGear.Add(FName(TEXT("attack_damage")), {Increase});

	UCataclysmPlayerClassStats::ApplyTo(
		System, UCataclysmPlayerClassStats::LoadTable(),
		UCataclysmPlayerClassStats::ChosenClass(),
		UCataclysmPlayerClassStats::ChosenLevel(), &FromGear);

	// THE CONTROL FIRST: a stat that HAS an attribute arrived, so a reading of
	// nothing below is about minion stats rather than about ApplyTo doing
	// nothing at all.
	TestEqual(TEXT("a stat with an attribute reached the stat line"),
			  System->IncreasesForStat(FName(TEXT("attack_damage")),
									   FGameplayTagContainer()),
			  IncreasePercent / 100.0f, 0.001f);

	TestEqual(TEXT("and so did the minion stat, which has none"),
			  System->IncreasesForStat(FName(TEXT("minion_attack_speed")),
									   FGameplayTagContainer()),
			  IncreasePercent / 100.0f, 0.001f);

	return true;
}

#endif  // WITH_AUTOMATION_TESTS
