// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyRarity.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Data/CataclysmDataRows.h"
#include "Dungeon/CataclysmFloorGenerator.h"
#include "GameFramework/Character.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Engine/DataTable.h"

/**
 * Tests for which rarity a spawned enemy is. Issue #508.
 *
 * WHAT IS COVERED. That the weights reaching the engine are the ones the design
 * states, that a draw follows them, that a Cataclysm Boss is never drawn, and
 * that a missing or empty table answers Common rather than something worse.
 * Since issue #849, also how big a creature at each rarity is, and since #885
 * that the biggest of them still fits the narrowest corridor a floor can hold.
 *
 * WHAT IS NOT. That the game mode calls any of this, and that a play session
 * therefore shows a spread of rarities. Spawning a creature needs a world, and
 * `Cataclysm.Sandbox.ACreatureSpawnsAtTheRarityItWasConfiguredWith` covers the
 * spawner writing what it was told. Whether the mix reads well over a real
 * session is a judgement only playing settles, and so is whether a creature at
 * a given size looks right: the automation command runs with `-nullrhi` and
 * nothing here can watch a creature draw. Issue #559.
 */
namespace CataclysmEnemyRarityTest
{
	using FRarity = UCataclysmEnemyRarity;

	/** How many draws each distribution check takes. */
	constexpr int32 Draws = 20000;

	/** The steps, in the order `sim/cataclysm_sim/enemy_stats.py` lists them. */
	constexpr int32 CommonStep = 0;
	constexpr int32 EliteStep = 1;
	constexpr int32 LegendaryStep = 2;
	constexpr int32 HeraldStep = 3;
	constexpr int32 BossStep = 4;
	constexpr int32 CataclysmBossStep = 5;
}

// ---------------------------------------------------------------------------
// The weights that reach the engine
// ---------------------------------------------------------------------------

/**
 * The five that spawn sum to one, and a Cataclysm Boss carries none.
 *
 * THE NUMBERS ARE WRITTEN HERE RATHER THAN READ FROM THE TABLE UNDER TEST, so a
 * table that lost a value fails instead of agreeing with itself. They come from
 * the Dungeon Score Formula section of `docs/Cataclysm_GDD_v2.md`, which states
 * all five and says outright: "The five weights are how common each rarity is,
 * and they sum to 1. Cataclysm Boss is absent because it does not appear on an
 * ordinary floor."
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSpawnWeightsTest,
	"Cataclysm.EnemyRarity.TheSpawnWeightsAreTheDesignedMix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSpawnWeightsTest::RunTest(const FString&)
{
	using namespace CataclysmEnemyRarityTest;

	const UDataTable* Table = FRarity::LoadEnemyRarityTable();
	if (!Table)
	{
		AddError(TEXT("DT_EnemyRarities does not exist. Run "
					  "tools/generate_datatable_assets.py."));
		return false;
	}

	struct FCase
	{
		int32 Step;
		const TCHAR* Rarity;
		float Weight;
	};

	const FCase Cases[] = {
		{ CommonStep,        TEXT("Common"),         0.60f },
		{ EliteStep,         TEXT("Elite"),          0.20f },
		{ LegendaryStep,     TEXT("Legendary"),      0.15f },
		{ HeraldStep,        TEXT("Herald"),         0.04f },
		{ BossStep,          TEXT("Boss"),           0.01f },
		{ CataclysmBossStep, TEXT("Cataclysm Boss"), 0.00f },
	};

	float Total = 0.0f;
	for (const FCase& Case : Cases)
	{
		const float Weight = FRarity::SpawnWeightForStep(Table, Case.Step);
		TestEqual(FString::Printf(TEXT("%s carries its designed weight"),
								  Case.Rarity), Weight, Case.Weight);
		Total += Weight;
	}

	TestEqual(TEXT("and the six together are one floor's worth"), Total, 1.0f);

	// THE FIVE THAT SPAWN ARE THE FIVE THE DESIGN NAMES, in order and with no
	// sixth. A Cataclysm Boss appearing here would be a second one met on an
	// ordinary floor.
	TArray<int32> Steps;
	FRarity::SpawnableSteps(Table, Steps);

	TestEqual(TEXT("five rarities can be drawn"), Steps.Num(), 5);
	if (Steps.Num() == 5)
	{
		TestEqual(TEXT("and they are the bottom five rungs, lowest first"),
			Steps, TArray<int32>({ CommonStep, EliteStep, LegendaryStep,
								   HeraldStep, BossStep }));
	}
	TestFalse(TEXT("a Cataclysm Boss is not among them"),
		Steps.Contains(CataclysmBossStep));

	// A STEP THE TABLE DOES NOT HOLD CARRIES NO WEIGHT, and neither does no
	// table at all.
	TestEqual(TEXT("a step off the ladder carries no weight"),
		FRarity::SpawnWeightForStep(Table, 99), 0.0f);
	TestEqual(TEXT("a negative step carries none either"),
		FRarity::SpawnWeightForStep(Table, -1), 0.0f);
	TestEqual(TEXT("and no table carries none"),
		FRarity::SpawnWeightForStep(nullptr, CommonStep), 0.0f);

	// EVERY STEP HAS A NAME A PERSON CAN READ. The log is the only place an
	// enemy's rarity appears at all, so a step with no name there leaves a line
	// that says nothing. Issue #740 is the screen work that would replace it.
	for (const FCase& Case : Cases)
	{
		TestEqual(FString::Printf(TEXT("step %d is called %s"), Case.Step,
								  Case.Rarity),
			FRarity::RarityNameForStep(Table, Case.Step), FString(Case.Rarity));
	}

	TestEqual(TEXT("a step off the ladder has no name"),
		FRarity::RarityNameForStep(Table, 99), FString());
	TestEqual(TEXT("and no table gives no name"),
		FRarity::RarityNameForStep(nullptr, CommonStep), FString());

	return true;
}

// ---------------------------------------------------------------------------
// Drawing one
// ---------------------------------------------------------------------------

/**
 * A draw follows the weights, and never produces a Cataclysm Boss.
 *
 * OVER TWENTY THOUSAND DRAWS, WITH A TOLERANCE WORKED OUT RATHER THAN GUESSED.
 * The standard error on a share p over n draws is sqrt(p(1-p)/n), which for the
 * commonest rung is 0.0035 and for the rarest is 0.0007. A tolerance of 0.02 is
 * more than five standard errors on every rung, so a correct draw fails this
 * about never, while a rung whose weight was dropped or doubled misses by far
 * more than that: Boss at 0.01 doubled to 0.02 is fifteen standard errors out.
 *
 * A FIXED SEED, so a failure can be reproduced. The draw itself is what is under
 * test, not the seed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRarityDrawTest,
	"Cataclysm.EnemyRarity.ADrawFollowsTheWeights",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRarityDrawTest::RunTest(const FString&)
{
	using namespace CataclysmEnemyRarityTest;

	const UDataTable* Table = FRarity::LoadEnemyRarityTable();
	if (!Table)
	{
		AddError(TEXT("DT_EnemyRarities does not exist. Run "
					  "tools/generate_datatable_assets.py."));
		return false;
	}

	TMap<int32, int32> Drawn;
	FRandomStream Stream(20260819);
	for (int32 Draw = 0; Draw < Draws; ++Draw)
	{
		++Drawn.FindOrAdd(FRarity::RollRarityStep(Table, Stream));
	}

	for (int32 Step = CommonStep; Step <= CataclysmBossStep; ++Step)
	{
		const float Expected = FRarity::SpawnWeightForStep(Table, Step);
		const float Share = static_cast<float>(Drawn.FindRef(Step))
			/ static_cast<float>(Draws);

		TestEqual(FString::Printf(
			TEXT("step %d came up about as often as its weight says"), Step),
			Share, Expected, 0.02f);
	}

	// NEVER A CATACLYSM BOSS, AND THIS IS EXACT RATHER THAN WITHIN A TOLERANCE.
	// Its weight is zero, so one appearing at all is a fault rather than an
	// unlucky sample.
	TestEqual(TEXT("a Cataclysm Boss was never drawn"),
		Drawn.FindRef(CataclysmBossStep), 0);

	// AND EVERY OTHER RUNG WAS DRAWN AT LEAST ONCE, or a draw that always
	// answered Common would pass the tolerances above for four of the five.
	for (int32 Step = CommonStep; Step <= BossStep; ++Step)
	{
		TestTrue(FString::Printf(TEXT("step %d was drawn at least once"), Step),
			Drawn.FindRef(Step) > 0);
	}

	// A BOSS IS RARE BUT REACHABLE, which is the whole reason the weights are
	// not flat. One in a hundred over twenty thousand draws is about 200.
	TestTrue(TEXT("a Boss is rarer than a Common"),
		Drawn.FindRef(BossStep) < Drawn.FindRef(CommonStep));

	return true;
}

/**
 * A draw with nothing to draw from answers Common.
 *
 * COMMON IS THE RIGHT ANSWER TO "SOMETHING WENT WRONG". It is what every
 * creature spawned as before anything drew a rarity at all, and it is the rung
 * that gives the least, so a missing table cannot quietly make the game more
 * generous than it should be.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRarityDrawWithoutATableTest,
	"Cataclysm.EnemyRarity.ADrawWithNothingToDrawFromIsCommon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRarityDrawWithoutATableTest::RunTest(const FString&)
{
	using namespace CataclysmEnemyRarityTest;

	FRandomStream Stream(1);
	TestEqual(TEXT("no table draws Common"),
		FRarity::RollRarityStep(nullptr, Stream), CommonStep);

	TArray<int32> Steps;
	FRarity::SpawnableSteps(nullptr, Steps);
	TestEqual(TEXT("and offers nothing to draw from"), Steps.Num(), 0);

	// AN EMPTY TABLE IS A DIFFERENT FAILURE FROM A MISSING ONE and reaches a
	// different branch: the asset loaded and holds no rows.
	UDataTable* Empty = NewObject<UDataTable>();
	Empty->RowStruct = FCataclysmEnemyRarityRow::StaticStruct();

	TestEqual(TEXT("an empty table draws Common too"),
		FRarity::RollRarityStep(Empty, Stream), CommonStep);

	FRarity::SpawnableSteps(Empty, Steps);
	TestEqual(TEXT("and offers nothing either"), Steps.Num(), 0);

	// AND THE ANSWER IS NOT A BOSS, which is the consequence that would matter:
	// a boss cannot be stunned at all.
	TestTrue(TEXT("Common is below the first boss rung"),
		CommonStep < ACataclysmEnemyCharacter::FirstBossRarityStep);

	return true;
}

/**
 * The rungs come back in order, whatever order the table holds them in.
 *
 * WHY THIS NEEDS A TABLE BUILT BY HAND. A DataTable is a map, so its rows come
 * back in whatever order the container happens to hold them. The generated one
 * happens to come back already sorted, so deleting the sort in
 * `SpawnableSteps` changes nothing any test over that table can see -- which was
 * measured, by deleting it and watching all three tests pass. This builds a
 * table whose rows are added highest rung first, which is the case the sort
 * exists for.
 *
 * WHAT GOES WRONG WITHOUT IT. The draw walks the rungs and accumulates their
 * weights until it passes the number it drew. Walked in a different order, the
 * same seed picks a different rung, so two runs of the same build could disagree
 * about what spawned with nothing having changed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRaritySortedTest,
	"Cataclysm.EnemyRarity.TheRungsComeBackInOrderWhateverOrderTheyWereAdded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRaritySortedTest::RunTest(const FString&)
{
	using namespace CataclysmEnemyRarityTest;

	UDataTable* Shuffled = NewObject<UDataTable>();
	Shuffled->RowStruct = FCataclysmEnemyRarityRow::StaticStruct();

	// HIGHEST RUNG FIRST, which is the reverse of what the generated table gives
	// and of what the draw needs.
	const int32 AddedInThisOrder[] = { BossStep, HeraldStep, CommonStep,
									   LegendaryStep, EliteStep };
	for (const int32 Step : AddedInThisOrder)
	{
		FCataclysmEnemyRarityRow Row;
		Row.RarityName = FString::Printf(TEXT("Rung%d"), Step);
		Row.Step = Step;
		Row.SpawnWeight = 0.2f;
		Shuffled->AddRow(FName(*Row.RarityName), Row);
	}

	TArray<int32> Steps;
	FRarity::SpawnableSteps(Shuffled, Steps);

	TestEqual(TEXT("all five rungs came back"), Steps.Num(), 5);
	TestEqual(TEXT("and they came back lowest first"),
		Steps, TArray<int32>({ CommonStep, EliteStep, LegendaryStep, HeraldStep,
							   BossStep }));

	// AND A ROW WITH NO WEIGHT IS STILL LEFT OUT, whatever order it was added in.
	FCataclysmEnemyRarityRow Unspawnable;
	Unspawnable.RarityName = TEXT("Placed");
	Unspawnable.Step = CataclysmBossStep;
	Unspawnable.SpawnWeight = 0.0f;
	Shuffled->AddRow(TEXT("Placed"), Unspawnable);

	FRarity::SpawnableSteps(Shuffled, Steps);
	TestEqual(TEXT("a rung with no weight is still not among them"),
		Steps.Num(), 5);
	TestFalse(TEXT("and it is the one that was left out"),
		Steps.Contains(CataclysmBossStep));

	return true;
}

// ---------------------------------------------------------------------------
// Saying it over the creature's head
// ---------------------------------------------------------------------------

/**
 * A rarity is said over a living enemy above Common, hurt or not. Issue #740.
 *
 * THE "HURT OR NOT" IS THE WHOLE POINT AND IT IS WHY THIS IS NOT THE BAR'S
 * RULE. `ShouldShowBarFor` deliberately shows nothing over an undamaged
 * creature, which is right for a health bar and wrong for a rarity: the design
 * says a boss cannot be stunned at all, and that is worth nothing to a player
 * who finds out by spending the stun. Path of Exile's own forum carries the
 * complaint this avoids.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRarityNameShownTest,
	"Cataclysm.EnemyRarity.TheRarityIsSaidBeforeTheFightRatherThanDuringIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRarityNameShownTest::RunTest(const FString&)
{
	using namespace CataclysmEnemyRarityTest;
	using FOverlay = UCataclysmCombatOverlay;

	// UNTOUCHED AND ABOVE COMMON IS THE CASE THAT MATTERS. A health bar would
	// show nothing here; a rarity has to.
	TestTrue(TEXT("an untouched Elite is marked"),
		FOverlay::ShouldShowRarityNameFor(EliteStep, 100.0f, 100.0f));
	TestFalse(TEXT("and a health bar over the same creature is not drawn"),
		FOverlay::ShouldShowBarFor(100.0f, 100.0f));

	// EVERY RUNG ABOVE COMMON, so a ladder that grew a rung is not silently
	// left unmarked.
	for (int32 Step = FOverlay::LowestMarkedRarityStep;
		 Step <= CataclysmBossStep; ++Step)
	{
		TestTrue(FString::Printf(TEXT("step %d is marked"), Step),
			FOverlay::ShouldShowRarityNameFor(Step, 100.0f, 100.0f));
	}

	// A COMMON IS NOT MARKED, at full health or hurt. It is 60% of what spawns
	// and a word over every one of them is a word over most of the screen.
	TestFalse(TEXT("an untouched Common is not marked"),
		FOverlay::ShouldShowRarityNameFor(CommonStep, 100.0f, 100.0f));
	TestFalse(TEXT("and neither is a hurt one"),
		FOverlay::ShouldShowRarityNameFor(CommonStep, 40.0f, 100.0f));

	// A HURT ONE ABOVE COMMON IS STILL MARKED, or the word would vanish the
	// moment the fight started.
	TestTrue(TEXT("a hurt Boss is still marked"),
		FOverlay::ShouldShowRarityNameFor(BossStep, 1.0f, 100.0f));

	// NOTHING OVER A CORPSE, the same rule the bar follows and for the same
	// reason: an enemy destroys itself on the tick after it dies.
	TestFalse(TEXT("a dead Boss is not marked"),
		FOverlay::ShouldShowRarityNameFor(BossStep, 0.0f, 100.0f));
	TestFalse(TEXT("and neither is one past zero"),
		FOverlay::ShouldShowRarityNameFor(BossStep, -5.0f, 100.0f));

	// A CREATURE WITH NO HEALTH POOL IS NOT A CREATURE YET. Its ability system
	// arrives some frames after the actor on a client.
	TestFalse(TEXT("a creature with no maximum health is not marked"),
		FOverlay::ShouldShowRarityNameFor(BossStep, 0.0f, 0.0f));

	// A STEP BELOW THE LADDER IS NOT MARKED EITHER, which is what the sandbox
	// setting holds before a rarity is drawn for it.
	TestFalse(TEXT("a step below Common is not marked"),
		FOverlay::ShouldShowRarityNameFor(UCataclysmEnemyRarity::RollTheRarity,
										  100.0f, 100.0f));

	// AND COMMON IS REALLY THE ONLY RUNG LEFT BARE, or the constant could drift
	// upward and quietly stop marking Elites.
	TestEqual(TEXT("the lowest marked rung is the one above Common"),
		FOverlay::LowestMarkedRarityStep, CommonStep + 1);

	return true;
}


// ---------------------------------------------------------------------------
// How big a creature is at each rarity. Issue #849.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRarityBodyScaleTest,
	"Cataclysm.EnemyRarity.ARarerCreatureIsPhysicallyBigger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRarityBodyScaleTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnemyRarityTest;

	const UDataTable* Table = FRarity::LoadEnemyRarityTable();
	if (!TestNotNull(TEXT("the enemy rarity table loads"), Table))
	{
		return false;
	}

	// A COMMON CREATURE IS ITS OWN SIZE. Every other figure is a multiple of
	// this one, so a Common that was not exactly one would resize the whole game.
	TestEqual(TEXT("a Common creature is its own size"),
		FRarity::BodyScaleForStep(Table, 0), 1.0f);

	// ONE RATIO, THE SAME AT EVERY RUNG, AND IT IS READ FROM THE DATA RATHER
	// THAN WRITTEN HERE. Issue #885. This used to assert `Below * 1.5f`, so
	// when the project owner changed the step from 50% to 20% on 2026-08-23
	// the test had to be edited to match the data it was meant to be checking
	// -- which is not a check at all. The ratio now comes from the first pair
	// and every later rung is held against it.
	//
	// WHAT THAT STILL CATCHES: a ladder that is not geometric. One rung
	// generated wrong, a linear ladder, or a value that stopped compounding
	// all fail here. What it deliberately does NOT claim is what the ratio
	// should be. That belongs to one file --
	// `BODY_SCALE_PER_STEP` in `sim/cataclysm_sim/enemy_stats.py` -- and
	// `tools/tests/test_rarity_scaling_matches_the_model.py` holds the
	// generated data against it, so a second copy of the figure here would be
	// a second answer to a settled question.
	const float Common = FRarity::BodyScaleForStep(Table, 0);
	const float Ratio = FRarity::BodyScaleForStep(Table, 1) / Common;

	// A RARER CREATURE IS BIGGER, WHICH IS THE WHOLE FEATURE. A ratio of one
	// or less is a ladder that does not climb, and every per-rung check below
	// would still pass on one.
	TestTrue(FString::Printf(
			TEXT("each rung is bigger than the one below it (ratio %.4f)"),
			Ratio),
		Ratio > 1.0f);

	for (int32 Step = 2; Step <= 5; ++Step)
	{
		const float Below = FRarity::BodyScaleForStep(Table, Step - 1);
		const float Here = FRarity::BodyScaleForStep(Table, Step);
		TestTrue(FString::Printf(
				TEXT("step %d climbs by the same ratio as every other step "
					 "(%.4f, %.4f, ratio %.4f)"),
				Step, Below, Here, Ratio),
			FMath::IsNearlyEqual(Here, Below * Ratio, 0.0001f));
	}

	// AND THE BIGGEST CREATURE IN THE GAME FITS THE NARROWEST CORRIDOR BESIDE
	// A PLAYER. This is what issue #885 changed and it is why the compounding
	// is safe now: at 50% a step a Cataclysm Boss was 729 cm and did not fit
	// at all, and what kept a player out of that passage was its spawn weight
	// of zero. At 20% it is 239 cm and fits with room to spare, so the size
	// rule no longer leans on the weight.
	//
	// MEASURED RATHER THAN WRITTEN DOWN. The two bodies come from the class
	// default objects' own capsules and the corridor from the floor
	// generator's own constants, so resizing a creature, the player, or a
	// cell moves this check with them.
	//
	// IT FAILS ON ITS OWN, WHICH THE PYTHON GUARD BESIDE IT CANNOT DO HERE.
	// `test_anything_too_wide_for_a_corridor_never_spawns_in_one` only asks
	// that anything too wide have a spawn weight of zero, and a Cataclysm
	// Boss has one either way. Putting the step back to 50% passes there and
	// fails here.
	const auto UnscaledRadiusOf = [](const UClass* Class) -> float
	{
		const ACharacter* Default =
			Class ? Class->GetDefaultObject<ACharacter>() : nullptr;
		const UCapsuleComponent* Capsule =
			Default ? Default->GetCapsuleComponent() : nullptr;
		return Capsule ? Capsule->GetUnscaledCapsuleRadius() : 0.0f;
	};

	const float CreatureWide =
		UnscaledRadiusOf(ACataclysmEnemyCharacter::StaticClass()) * 2.0f;
	const float PlayerWide =
		UnscaledRadiusOf(ACataclysmPlayerCharacter::StaticClass()) * 2.0f;
	const float CorridorWide = FCataclysmFloorGenerator::CellSizeCm
		* static_cast<float>(FCataclysmFloorGenerator::LeastConnectionWidth);

	// A ZERO HERE WOULD MAKE THE CHECK BELOW PASS ON NOTHING, so the
	// measurements are confirmed before they are used.
	TestTrue(TEXT("a creature's body was measured"), CreatureWide > 0.0f);
	TestTrue(TEXT("the player's body was measured"), PlayerWide > 0.0f);
	TestTrue(TEXT("a corridor has a width"), CorridorWide > 0.0f);

	const float BiggestWide =
		CreatureWide * FRarity::BodyScaleForStep(Table, CataclysmBossStep);

	TestTrue(FString::Printf(
			TEXT("the biggest creature is %.0f cm and a player %.0f cm can "
				 "still get past it in a %.0f cm corridor"),
			BiggestWide, PlayerWide, CorridorWide),
		BiggestWide + PlayerWide <= CorridorWide);

	// A MISSING TABLE LEAVES A CREATURE ITS OWN SIZE rather than scaling it to
	// nothing, which is what a zero default would do.
	TestEqual(TEXT("no table means no scaling"),
		FRarity::BodyScaleForStep(nullptr, 3), 1.0f);

	// AND SO DOES A STEP THE TABLE DOES NOT HOLD. The ladder is 0 to 5.
	TestEqual(TEXT("a step off the end of the ladder means no scaling"),
		FRarity::BodyScaleForStep(Table, 99), 1.0f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
