// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyRarity.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"

/**
 * Tests for which rarity a spawned enemy is. Issue #508.
 *
 * WHAT IS COVERED. That the weights reaching the engine are the ones the design
 * states, that a draw follows them, that a Cataclysm Boss is never drawn, and
 * that a missing or empty table answers Common rather than something worse.
 *
 * WHAT IS NOT. That the game mode calls any of this, and that a play session
 * therefore shows a spread of rarities. Spawning a creature needs a world, and
 * `Cataclysm.Sandbox.ACreatureSpawnsAtTheRarityItWasConfiguredWith` covers the
 * spawner writing what it was told. Whether the mix reads well over a real
 * session is a judgement only playing settles.
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

#endif // WITH_AUTOMATION_TESTS
