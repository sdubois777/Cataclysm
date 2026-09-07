// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmDungeonModifierTable.h"
#include "Dungeon/CataclysmFloorBrief.h"
#include "Dungeon/CataclysmFloorGenerator.h"
#include "Dungeon/CataclysmFloorPlan.h"
#include "Dungeon/CataclysmFloorPopulation.h"
#include "Empire/CataclysmDungeonModifier.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmRoster.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A dungeon deciding what its floors hold. Issue #41.
 *
 * WHAT WAS MISSING. Nothing that generated a floor could name the dungeon it
 * belonged to, so no rule that depends on the dungeon could be written anywhere
 * in the floor pipeline. Every dungeon a surge lands carries one of seven
 * sub-types -- the project owner ruled on 2026-09-05 that none is plain -- and
 * not one of them changed a floor. Three of them are more than half of every
 * dungeon in the game: Horde at 19.7 in 100, Elite and Volatile at 16.4 each.
 *
 * WHAT THESE TESTS COVER. `FCataclysmDungeonFloorRules` is the seam and the
 * first half of this file is the rules themselves, swept over floors of dungeons
 * built by hand. The second half is the two places the answer is spent: the
 * population pass, which stands a boss on a floor's exit and gathers a wave, and
 * the game mode, which is where a floor's modifiers reach the enemy score model.
 *
 * ONE PREFIX FOR THE WHOLE FEATURE, so that
 * `python tools/unreal_build.py tests --prefix Cataclysm.FloorBrief` runs all of
 * it. `Cataclysm.DungeonModifiers` is the same arrangement across two files and
 * says so.
 *
 * WHY A DUNGEON BUILT BY HAND AND NOT ONE OFF THE MAP. Every rule here is a
 * function over a plain struct, so a test can ask "what does a Volatile dungeon
 * of eight floors at tier 4 do on floor 5" without a surge, a city or a clock.
 * The two tests that do build a run are the ones checking that the answer is
 * actually spent, which a rule test cannot see.
 */

namespace CataclysmFloorBriefTest
{
	/** How many floors each sweep walks. Deeper than any Basic dungeon. */
	constexpr int32 SweepFloors = 20;

	/** How many dungeon seeds each sweep tries. */
	constexpr int32 SweepSeeds = 40;

	/** One modifier, built by hand, with a distinguishable danger score. */
	FCataclysmDungeonModifier Make(const TCHAR* Key, ECataclysmType Cataclysm,
								   float Danger)
	{
		FCataclysmDungeonModifier Modifier;
		Modifier.RowKey = FName(Key);
		Modifier.ModifierName = FName(Key);
		Modifier.Cataclysm = Cataclysm;
		Modifier.Danger = Danger;
		return Modifier;
	}

	/**
	 * A pool of twenty modifiers, all of one Cataclysm, with distinct dangers.
	 *
	 * TWENTY, so a tier 4 dungeon drawing four of them has enough room that two
	 * floors drawing the same four would be a real coincidence rather than the
	 * only thing the pool could produce. Distinct dangers, so that two different
	 * draws are two different scores and a test can tell them apart by the
	 * number alone.
	 */
	TArray<FCataclysmDungeonModifier> Pool(int32 HowMany = 20)
	{
		TArray<FCataclysmDungeonModifier> Out;
		for (int32 Index = 0; Index < HowMany; ++Index)
		{
			Out.Add(Make(*FString::Printf(TEXT("War_Modifier_%02d"), Index),
						 ECataclysmType::War,
						 static_cast<float>(Index + 1)));
		}
		return Out;
	}

	/** A dungeon of this sub-type, at tier 4, with a pool and four modifiers. */
	FCataclysmDungeonIdentity Dungeon(ECataclysmDungeonSubType SubType,
									  int32 DungeonSeed = 7,
									  int32 TotalFloors = SweepFloors)
	{
		FCataclysmDungeonIdentity Out;
		Out.DungeonSeed = DungeonSeed;
		Out.TotalFloors = TotalFloors;
		Out.SubType = SubType;
		Out.DifficultyTier = 4;
		Out.ModifierPool = Pool();

		// WHAT THE DUNGEON ITSELF DREW, which is what an ordinary dungeon's
		// floors all carry. Four of the twenty, because tier 4 draws four.
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Out.Modifiers.Add(Out.ModifierPool[Index].RowKey);
			Out.ModifierScore += Out.ModifierPool[Index].Danger;
		}
		return Out;
	}

	/** One floor plan, so a population can be asked for without a game mode. */
	FCataclysmFloorPlan Floor(int32 DungeonSeed, int32 FloorNumber,
							  ECataclysmFloorLayout Layout)
	{
		FCataclysmFloorRequest Request;
		Request.DungeonSeed = DungeonSeed;
		Request.FloorNumber = FloorNumber;
		Request.Layout = Layout;
		return FCataclysmFloorGenerator::Generate(Request);
	}

	/** A list of row keys, sorted into one string, so two lists can be compared. */
	FString SortedKeys(const TArray<FName>& Modifiers)
	{
		TArray<FString> Keys;
		for (const FName Key : Modifiers)
		{
			Keys.Add(Key.ToString());
		}
		Keys.Sort();
		return FString::Join(Keys, TEXT(","));
	}

	/** The modifier row keys of a floor, sorted, so two floors can be compared. */
	FString KeysOf(const FCataclysmFloorBrief& Brief)
	{
		return SortedKeys(Brief.Modifiers);
	}

	/** How far apart two cells are in a straight line, in cells. */
	float StraightLineCells(FIntPoint A, FIntPoint B)
	{
		return FVector2D(static_cast<float>(A.X - B.X),
						 static_cast<float>(A.Y - B.Y)).Size();
	}

	/** The mean straight-line distance of every creature from a cell. */
	float MeanDistanceFrom(const FCataclysmFloorPopulation& Population,
						   FIntPoint From)
	{
		if (Population.Enemies.Num() == 0)
		{
			return 0.0f;
		}
		float Total = 0.0f;
		for (const FCataclysmEnemyPlacement& Placement : Population.Enemies)
		{
			Total += StraightLineCells(Placement.Cell, From);
		}
		return Total / static_cast<float>(Population.Enemies.Num());
	}

	/**
	 * The closest two group middles stand, in cells WALKED, or -1 for fewer than
	 * two groups.
	 *
	 * **WALKED AND NOT IN A STRAIGHT LINE, WHICH IS NOT THE SAME CLAIM.**
	 * `LeastCellsBetweenPacks` is a rule about walking distance, and two cells
	 * either side of a wall are a metre apart and a long way to walk. Measuring
	 * this in a straight line would report an ordinary floor as breaking its own
	 * rule whenever two groups stood across a wall from each other.
	 *
	 * A BREADTH-FIRST SEARCH PER GROUP, which is why the test that uses it
	 * sweeps fewer floors than the rest of this file.
	 */
	int32 ClosestTwoPackSitesWalked(const FCataclysmFloorPlan& Plan,
									const FCataclysmFloorPopulation& Population)
	{
		int32 Closest = -1;
		for (int32 A = 0; A < Population.PackSites.Num(); ++A)
		{
			const TArray<int32> FromA =
				CataclysmFloorDistancesFrom(Plan, Population.PackSites[A]);

			for (int32 B = 0; B < Population.PackSites.Num(); ++B)
			{
				if (A == B)
				{
					continue;
				}
				const int32 Index = Plan.IndexOf(Population.PackSites[B]);
				if (Index == INDEX_NONE || FromA[Index] == INDEX_NONE)
				{
					continue;
				}
				if (Closest < 0 || FromA[Index] < Closest)
				{
					Closest = FromA[Index];
				}
			}
		}
		return Closest;
	}
}

// ---------------------------------------------------------------------------
// The seam itself: an ordinary dungeon is unchanged, and every answer is
// reproducible from the dungeon's seed and the floor number
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorBriefOrdinaryTest,
	"Cataclysm.FloorBrief.AnOrdinaryDungeonsFloorsAreAllTheSame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorBriefOrdinaryTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorBriefTest;

	// **THE CONTROL FOR EVERY OTHER TEST IN THIS FILE.** Each rule below is a
	// claim that something differs from an ordinary dungeon, and a claim like
	// that is worth nothing unless an ordinary dungeon really is unchanged.
	// Four of the seven sub-types have no rule in this seam at all and must come
	// out identical.
	const TArray<ECataclysmDungeonSubType> Unchanged = {
		ECataclysmDungeonSubType::None,
		ECataclysmDungeonSubType::Timed,
		ECataclysmDungeonSubType::Siege,
		ECataclysmDungeonSubType::CowLevel,
		ECataclysmDungeonSubType::Sacrificial };

	for (const ECataclysmDungeonSubType SubType : Unchanged)
	{
		FCataclysmDungeonIdentity Ordinary = Dungeon(SubType);
		Ordinary.Layout = ECataclysmFloorLayout::Caverns;

		const FString Expected = SortedKeys(Ordinary.Modifiers);

		for (int32 Floor = 1; Floor <= SweepFloors; ++Floor)
		{
			const FCataclysmFloorBrief Brief =
				FCataclysmDungeonFloorRules::BriefFor(Ordinary, Floor);

			TestEqual(TEXT("an ordinary dungeon's floor is carved the way the "
						   "dungeon says"),
					  static_cast<int32>(Brief.Layout),
					  static_cast<int32>(ECataclysmFloorLayout::Caverns));

			TestEqual(TEXT("and carries the dungeon's own modifiers"),
					  KeysOf(Brief), Expected);

			TestEqual(TEXT("and their score"),
					  Brief.ModifierScore, Ordinary.ModifierScore);

			TestFalse(TEXT("and its creatures are not one wave"), Brief.bOneWave);

			// A BOSS ON THE LAST FLOOR AND NOWHERE ELSE. The design's universal
			// rule, `docs/Cataclysm_GDD_v2.md` section VIII: "Every dungeon has
			// a boss on the final floor."
			TestEqual(FString::Printf(
						  TEXT("floor %d of %d ends with a boss only if it is "
							   "the last one"), Floor, SweepFloors),
					  Brief.bBossAtTheExit, Floor == SweepFloors);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorBriefRepeatableTest,
	"Cataclysm.FloorBrief.TheSameFloorOfTheSameDungeonIsAlwaysTheSame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorBriefRepeatableTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorBriefTest;

	// ISSUE #40 REQUIRES THIS OF EVERYTHING ABOUT A FLOOR, twice over: a dungeon
	// must look the same when the player leaves and returns, and a fault must be
	// reproducible from the seed. A per-floor modifier draw is a new thing that
	// could break it, because it is the first draw in the floor pipeline that is
	// not taken from the floor's own carve seed.
	for (int32 Seed = 1; Seed <= SweepSeeds; ++Seed)
	{
		const FCataclysmDungeonIdentity Volatile =
			Dungeon(ECataclysmDungeonSubType::Volatile, Seed);

		for (int32 Floor = 1; Floor <= SweepFloors; ++Floor)
		{
			const FCataclysmFloorBrief First =
				FCataclysmDungeonFloorRules::BriefFor(Volatile, Floor);
			const FCataclysmFloorBrief Second =
				FCataclysmDungeonFloorRules::BriefFor(Volatile, Floor);

			if (KeysOf(First) != KeysOf(Second)
				|| First.ModifierScore != Second.ModifierScore)
			{
				AddError(FString::Printf(
					TEXT("floor %d of Volatile dungeon %d came out differently "
						 "the second time it was asked for: %s then %s"),
					Floor, Seed, *KeysOf(First), *KeysOf(Second)));
				return false;
			}
		}
	}

	// AND A FLOOR NUMBER BELOW ONE IS FLOOR ONE, which is what
	// `ACataclysmDungeonGameMode::GoToFloor` clamps to and what a save holding a
	// zero would produce.
	const FCataclysmDungeonIdentity Any =
		Dungeon(ECataclysmDungeonSubType::Volatile);
	TestEqual(TEXT("floor 0 is floor 1"),
			  KeysOf(FCataclysmDungeonFloorRules::BriefFor(Any, 0)),
			  KeysOf(FCataclysmDungeonFloorRules::BriefFor(Any, 1)));
	TestEqual(TEXT("and so is floor -3"),
			  KeysOf(FCataclysmDungeonFloorRules::BriefFor(Any, -3)),
			  KeysOf(FCataclysmDungeonFloorRules::BriefFor(Any, 1)));

	return true;
}

// ---------------------------------------------------------------------------
// Volatile: "Dungeon modifiers change every floor."
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorBriefVolatileTest,
	"Cataclysm.FloorBrief.AVolatileDungeonsModifiersChangeEveryFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorBriefVolatileTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorBriefTest;

	// `docs/Cataclysm_GDD_v2.md`, the Dungeon Sub-Types table: "Volatile |
	// Dungeon modifiers change every floor."
	//
	// **MEASURED AS HOW MANY DISTINCT SETS TWENTY FLOORS PRODUCE**, not as
	// "floor 2 differs from floor 1". Two consecutive floors could differ by
	// chance under a rule that only re-drew once, and a rule that re-drew every
	// floor from a stream that did not advance would give one set for all
	// twenty. Counting the distinct sets separates those.
	int32 WorstDistinct = SweepFloors + 1;

	for (int32 Seed = 1; Seed <= SweepSeeds; ++Seed)
	{
		const FCataclysmDungeonIdentity Changing =
			Dungeon(ECataclysmDungeonSubType::Volatile, Seed);
		const FCataclysmDungeonIdentity Steady =
			Dungeon(ECataclysmDungeonSubType::None, Seed);

		TSet<FString> ChangingSets;
		TSet<FString> SteadySets;

		for (int32 Floor = 1; Floor <= SweepFloors; ++Floor)
		{
			ChangingSets.Add(
				KeysOf(FCataclysmDungeonFloorRules::BriefFor(Changing, Floor)));
			SteadySets.Add(
				KeysOf(FCataclysmDungeonFloorRules::BriefFor(Steady, Floor)));
		}

		WorstDistinct = FMath::Min(WorstDistinct, ChangingSets.Num());

		// THE CONTROL, IN THE SAME LOOP AND ON THE SAME SEED. An ordinary
		// dungeon of the same depth with the same pool must give exactly one
		// set, or "changes every floor" is measuring something the seam does to
		// every dungeon rather than something Volatile does.
		if (SteadySets.Num() != 1)
		{
			AddError(FString::Printf(
				TEXT("dungeon %d has no sub-type and still carried %d different "
					 "modifier sets over %d floors"),
				Seed, SteadySets.Num(), SweepFloors));
			return false;
		}
	}

	// **THE LIMIT IS WELL BELOW WHAT A CORRECT DRAW GIVES AND WELL ABOVE ONE.**
	// Four modifiers out of twenty is 4,845 possible sets, so twenty floors
	// repeating a set at all is unlikely and repeating enough to fall below ten
	// is not something a working re-draw does. One would be a re-draw that never
	// advanced its stream, which is the failure worth catching.
	TestTrue(FString::Printf(
				 TEXT("the tightest of %d Volatile dungeons still carried %d "
					  "different modifier sets over %d floors, which is more "
					  "than 10"),
				 SweepSeeds, WorstDistinct, SweepFloors),
			 WorstDistinct > 10);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorBriefVolatileCountTest,
	"Cataclysm.FloorBrief.AVolatileFloorCarriesTheModifiersItsTierIsWorth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorBriefVolatileCountTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorBriefTest;

	// **CHANGING IS NOT THE SAME AS CARRYING MORE.** A Volatile dungeon draws a
	// different set on every floor and the set is the same size a plain dungeon
	// of that tier carries -- `UCataclysmDungeonModifierRules::CountFor` is the
	// one rule for how many, and this seam does not get a second opinion.
	for (int32 Tier = 1; Tier <= 8; ++Tier)
	{
		FCataclysmDungeonIdentity Changing =
			Dungeon(ECataclysmDungeonSubType::Volatile);
		Changing.DifficultyTier = Tier;

		const int32 Expected = UCataclysmDungeonModifierRules::CountFor(
			Tier, ECataclysmDungeonSubType::Volatile);

		for (int32 Floor = 1; Floor <= SweepFloors; ++Floor)
		{
			const FCataclysmFloorBrief Brief =
				FCataclysmDungeonFloorRules::BriefFor(Changing, Floor);

			TestEqual(FString::Printf(
						  TEXT("tier %d floor %d carries %d modifiers"),
						  Tier, Floor, Expected),
					  Brief.Modifiers.Num(), Expected);

			// AND THE SCORE IS THE DANGER OF THE ONES IT ACTUALLY CARRIES, not
			// the dungeon's. This is the number the enemy score model adds to
			// every creature on the floor, so getting it from the wrong list is
			// the failure that would make the whole rule cosmetic.
			float Danger = 0.0f;
			for (const FName Key : Brief.Modifiers)
			{
				for (const FCataclysmDungeonModifier& Modifier : Changing.ModifierPool)
				{
					if (Modifier.RowKey == Key)
					{
						Danger += Modifier.Danger;
					}
				}
			}
			TestEqual(FString::Printf(
						  TEXT("tier %d floor %d is worth the danger of its own "
							   "modifiers"), Tier, Floor),
					  Brief.ModifierScore, Danger);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorBriefVolatileNoPoolTest,
	"Cataclysm.FloorBrief.AVolatileDungeonWithNothingToDrawFromKeepsItsOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorBriefVolatileNoPoolTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorBriefTest;

	// A RUN WHOSE MODIFIER POOL WAS NEVER FILLED IS EVERY HEADLESS TEST, because
	// filling it needs the modifier DataTable. A Volatile dungeon there must
	// keep the modifiers the dungeon carries rather than losing them: an empty
	// floor is a worse answer than an unchanged one, and it would quietly make
	// every Volatile dungeon easier than a plain one.
	FCataclysmDungeonIdentity NoPool =
		Dungeon(ECataclysmDungeonSubType::Volatile);
	NoPool.ModifierPool.Reset();

	for (int32 Floor = 1; Floor <= SweepFloors; ++Floor)
	{
		const FCataclysmFloorBrief Brief =
			FCataclysmDungeonFloorRules::BriefFor(NoPool, Floor);

		TestEqual(TEXT("with nothing to draw from it keeps the dungeon's "
					   "modifiers"),
				  Brief.Modifiers.Num(), NoPool.Modifiers.Num());
		TestEqual(TEXT("and their score"),
				  Brief.ModifierScore, NoPool.ModifierScore);
	}

	// AND A POOL SMALLER THAN THE COUNT GIVES FEWER RATHER THAN REPEATS, which
	// is what `UCataclysmDungeonModifierRules::Draw` promises and this seam must
	// not undo.
	FCataclysmDungeonIdentity Short =
		Dungeon(ECataclysmDungeonSubType::Volatile);
	Short.DifficultyTier = 8;
	Short.ModifierPool = Pool(3);

	const FCataclysmFloorBrief Brief =
		FCataclysmDungeonFloorRules::BriefFor(Short, 1);
	TestEqual(TEXT("a tier 8 dungeon with three modifiers to draw from carries "
				   "three"),
			  Brief.Modifiers.Num(), 3);

	TSet<FName> Distinct(Brief.Modifiers);
	TestEqual(TEXT("and none of them twice"),
			  Distinct.Num(), Brief.Modifiers.Num());

	return true;
}

// ---------------------------------------------------------------------------
// Elite: "Every floor ends with a boss fight."
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorBriefEliteTest,
	"Cataclysm.FloorBrief.EveryFloorOfAnEliteDungeonEndsWithABossFight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorBriefEliteTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorBriefTest;

	// `docs/Cataclysm_GDD_v2.md`, the Dungeon Sub-Types table: "Elite | Every
	// floor ends with a boss fight."
	const FCataclysmDungeonIdentity Elite =
		Dungeon(ECataclysmDungeonSubType::Elite);
	const FCataclysmDungeonIdentity Plain =
		Dungeon(ECataclysmDungeonSubType::None);

	int32 EliteBosses = 0;
	int32 PlainBosses = 0;

	for (int32 Floor = 1; Floor <= SweepFloors; ++Floor)
	{
		if (FCataclysmDungeonFloorRules::BriefFor(Elite, Floor).bBossAtTheExit)
		{
			++EliteBosses;
		}
		if (FCataclysmDungeonFloorRules::BriefFor(Plain, Floor).bBossAtTheExit)
		{
			++PlainBosses;
		}
	}

	TestEqual(TEXT("an Elite dungeon of 20 floors holds 20 boss fights"),
			  EliteBosses, SweepFloors);

	// THE CONTROL, AND IT IS WHAT MAKES THE LINE ABOVE MEAN ANYTHING. An
	// ordinary dungeon of the same depth holds one, on its last floor.
	TestEqual(TEXT("and an ordinary one of the same depth holds one"),
			  PlainBosses, 1);

	// **THE LAST FLOOR OF AN ELITE DUNGEON IS NOT AN EXCEPTION.** "Every floor"
	// includes the bottom, and the universal rule puts one there anyway, so the
	// two rules agree rather than cancelling.
	TestTrue(TEXT("an Elite dungeon's last floor has one too"),
			 FCataclysmDungeonFloorRules::BriefFor(Elite, SweepFloors)
				 .bBossAtTheExit);

	// A DUNGEON WITH NO BOTTOM HAS NO LAST FLOOR. Pressing Play in `L_Dungeon`
	// descends for ever, which `IsOnTheLastFloor` says the same thing about, so
	// there is nowhere for the universal rule to put a boss.
	FCataclysmDungeonIdentity Endless = Dungeon(ECataclysmDungeonSubType::None);
	Endless.TotalFloors = 1;
	for (int32 Floor = 1; Floor <= SweepFloors; ++Floor)
	{
		TestFalse(TEXT("a dungeon with no bottom has no boss floor"),
				  FCataclysmDungeonFloorRules::BriefFor(Endless, Floor)
					  .bBossAtTheExit);
	}

	// AND AN ELITE DUNGEON STILL DOES, because its rule does not ask where the
	// bottom is.
	FCataclysmDungeonIdentity EndlessElite =
		Dungeon(ECataclysmDungeonSubType::Elite);
	EndlessElite.TotalFloors = 1;
	TestTrue(TEXT("an Elite dungeon with no bottom still ends every floor with "
				  "a boss"),
			 FCataclysmDungeonFloorRules::BriefFor(EndlessElite, 5)
				 .bBossAtTheExit);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorBriefBossStandsTest,
	"Cataclysm.FloorBrief.TheBossStandsOnTheWayDownAndIsNotOneOfTheCrowd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorBriefBossStandsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorBriefTest;

	// **THE RULE REACHING THE FLOOR.** `bBossAtTheExit` being true is a claim
	// about a boolean; this is the claim that a Gatekeeper really is standing
	// there, and standing on the way down rather than somewhere on the floor.
	for (int32 Seed = 1; Seed <= SweepSeeds; ++Seed)
	{
		const FCataclysmFloorPlan Plan =
			Floor(Seed, 3, ECataclysmFloorLayout::Halls);

		FCataclysmFloorBrief Boss;
		Boss.bBossAtTheExit = true;

		const FCataclysmFloorPopulation WithBoss =
			FCataclysmFloorPopulator::Populate(Plan, 1.0f, Boss);
		const FCataclysmFloorPopulation Without =
			FCataclysmFloorPopulator::Populate(Plan, 1.0f);

		const int32 Bosses =
			WithBoss.HowMany(ECataclysmDungeonCreature::Gatekeeper);
		if (Bosses != 1)
		{
			AddError(FString::Printf(
				TEXT("floor %d of dungeon %d was asked for a boss and holds %d"),
				3, Seed, Bosses));
			return false;
		}

		// THE CONTROL. A floor that was not asked for one must not have one, or
		// the line above is measuring the populator rather than the rule.
		if (Without.HowMany(ECataclysmDungeonCreature::Gatekeeper) != 0)
		{
			AddError(FString::Printf(
				TEXT("floor %d of dungeon %d was not asked for a boss and has "
					 "one anyway"), 3, Seed));
			return false;
		}

		for (const FCataclysmEnemyPlacement& Placement : WithBoss.Enemies)
		{
			if (Placement.Creature != ECataclysmDungeonCreature::Gatekeeper)
			{
				continue;
			}

			if (Placement.Cell != Plan.Exit)
			{
				AddError(FString::Printf(
					TEXT("the boss on floor 3 of dungeon %d stands at (%d, %d) "
						 "and the way down is at (%d, %d)"),
					Seed, Placement.Cell.X, Placement.Cell.Y,
					Plan.Exit.X, Plan.Exit.Y));
				return false;
			}

			// **IT IS IN NO GROUP.** A boss is not one of the encounters the
			// floor is made of, so it carries no pack and `PackCount` does not
			// count it. Without this a floor's group count would rise by one on
			// every boss floor and the density measurements would drift.
			if (Placement.Pack != INDEX_NONE)
			{
				AddError(FString::Printf(
					TEXT("the boss on floor 3 of dungeon %d was put in group %d"),
					Seed, Placement.Pack));
				return false;
			}
		}

		// AND IT IS ONE CREATURE BEYOND WHAT THE DENSITY ASKED FOR, not one of
		// them. The density is a property of the floor's size and a boss does
		// not change it.
		if (WithBoss.Wanted != Without.Wanted)
		{
			AddError(FString::Printf(
				TEXT("a boss changed what the density asked for on floor 3 of "
					 "dungeon %d, from %d to %d"),
				Seed, Without.Wanted, WithBoss.Wanted));
			return false;
		}

		if (WithBoss.PackCount != Without.PackCount)
		{
			// NOT AN ERROR BY ITSELF -- the boss takes the exit cell, so a group
			// that would have stood there stands elsewhere and the count can
			// move by a little. A large move would mean the boss is being
			// counted as a group.
			if (FMath::Abs(WithBoss.PackCount - Without.PackCount) > 3)
			{
				AddError(FString::Printf(
					TEXT("a boss moved the group count on floor 3 of dungeon %d "
						 "from %d to %d, which is more than taking one cell can "
						 "explain"),
					Seed, Without.PackCount, WithBoss.PackCount));
				return false;
			}
		}
	}

	// AND A FLOOR THE DENSITY EMPTIED STILL HAS ITS BOSS. `Cataclysm.Dungeon
	// EnemyScale 0` is what somebody walking a floor to look at its shape uses,
	// and a boss floor with nothing on it but the boss is the right answer.
	const FCataclysmFloorPlan Empty =
		Floor(1, 3, ECataclysmFloorLayout::Halls);
	FCataclysmFloorBrief Boss;
	Boss.bBossAtTheExit = true;
	const FCataclysmFloorPopulation Bare =
		FCataclysmFloorPopulator::Populate(Empty, 0.0f, Boss);

	TestEqual(TEXT("an emptied boss floor holds exactly the boss"),
			  Bare.Enemies.Num(), 1);
	TestEqual(TEXT("and it is the Gatekeeper"),
			  Bare.HowMany(ECataclysmDungeonCreature::Gatekeeper), 1);

	return true;
}

// ---------------------------------------------------------------------------
// Horde: "Number of floors equals number of enemy waves."
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorBriefHordeLayoutTest,
	"Cataclysm.FloorBrief.EveryFloorOfAHordeDungeonIsOneOpenSpace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorBriefHordeLayoutTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorBriefTest;

	// `ECataclysmFloorLayout::Arena`'s own comment has said "One open space.
	// What a Horde floor is, and what a boss floor wants" since the layout
	// families were built, and nothing made it true until now.
	for (uint8 Which = 0;
		 Which < static_cast<uint8>(ECataclysmFloorLayout::Count); ++Which)
	{
		FCataclysmDungeonIdentity Waves =
			Dungeon(ECataclysmDungeonSubType::Horde);
		Waves.Layout = static_cast<ECataclysmFloorLayout>(Which);

		FCataclysmDungeonIdentity Plain =
			Dungeon(ECataclysmDungeonSubType::None);
		Plain.Layout = static_cast<ECataclysmFloorLayout>(Which);

		for (int32 Floor = 1; Floor <= SweepFloors; ++Floor)
		{
			TestEqual(FString::Printf(
						  TEXT("floor %d of a Horde dungeon set to layout %d is "
							   "an Arena anyway"), Floor, Which),
					  static_cast<int32>(
						  FCataclysmDungeonFloorRules::BriefFor(Waves, Floor).Layout),
					  static_cast<int32>(ECataclysmFloorLayout::Arena));

			// THE CONTROL. An ordinary dungeon keeps the layout it was given,
			// including Arena -- so the line above is not passing because every
			// dungeon is now an Arena.
			TestEqual(FString::Printf(
						  TEXT("and an ordinary dungeon set to layout %d keeps "
							   "it on floor %d"), Which, Floor),
					  static_cast<int32>(
						  FCataclysmDungeonFloorRules::BriefFor(Plain, Floor).Layout),
					  Which);
		}
	}

	// AND ITS FLOOR COUNT IS UNTOUCHED, which is the trap in the design
	// sentence. "Number of floors equals number of enemy waves" reads as though
	// a Horde dungeon could be one arena with twenty waves in it -- and
	// `CLAUDE.md` says depth and reward are the same axis, so collapsing twenty
	// floors into one would make the dungeon nineteen twentieths poorer and
	// nineteen twentieths cheaper to walk. Nothing in this seam touches the
	// count; every floor is a wave.
	const FCataclysmDungeonIdentity Waves =
		Dungeon(ECataclysmDungeonSubType::Horde);
	int32 WaveFloors = 0;
	for (int32 Floor = 1; Floor <= Waves.TotalFloors; ++Floor)
	{
		if (FCataclysmDungeonFloorRules::BriefFor(Waves, Floor).bOneWave)
		{
			++WaveFloors;
		}
	}
	TestEqual(TEXT("a Horde dungeon of 20 floors is 20 waves"),
			  WaveFloors, Waves.TotalFloors);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorBriefHordeCrowdTest,
	"Cataclysm.FloorBrief.AHordeFloorsCreaturesAreOneCrowdAndNotSeveralEncounters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorBriefHordeCrowdTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorBriefTest;

	// **WHAT A WAVE IS, MEASURED.** `LeastCellsBetweenPacks` exists so "a floor
	// reads as a series of encounters rather than as one crowd that arrives
	// together" -- its own words. A wave is that crowd. So the two things that
	// separate a wave floor from an ordinary one are that its groups stand
	// closer than that rule allows, and that its creatures are gathered around
	// one point instead of spread over the floor.
	//
	// **BOTH WERE PROVED BY BREAKING THEM, AND THE FIRST ATTEMPT WAS A NO-OP.**
	// Swapping the wave's `Occupied` check in `FCataclysmFloorPopulator::Populate`
	// for the ordinary floor's `Claimed` check changes nothing, because
	// `Claimed` is only FILLED on an ordinary floor -- so the break consults a
	// set that is always empty and every test still passes. It reads exactly
	// like a test that cannot fail. Turning the spacing rule back on for a wave
	// needs the fill AND the check, and done that way this test fails. The
	// other break, reversing the sort that orders candidate cells by distance
	// from the wave site, also fails it.
	int32 Measured = 0;
	float WorstWaveSpread = 0.0f;
	float BestOrdinarySpread = 1000.0f;

	// FEWER SEEDS THAN THE REST OF THIS FILE, because the closeness measurement
	// runs a breadth-first search from every group on the floor and an Arena
	// floor holds up to a hundred of them. Twelve floors of each kind is enough
	// to show a difference that is present on every one of them.
	constexpr int32 CrowdSeeds = 12;

	for (int32 Seed = 1; Seed <= CrowdSeeds; ++Seed)
	{
		const FCataclysmFloorPlan Plan =
			Floor(Seed, 2, ECataclysmFloorLayout::Arena);

		FCataclysmFloorBrief Wave;
		Wave.bOneWave = true;

		const FCataclysmFloorPopulation Crowd =
			FCataclysmFloorPopulator::Populate(Plan, 1.0f, Wave);
		const FCataclysmFloorPopulation Spread =
			FCataclysmFloorPopulator::Populate(Plan, 1.0f);

		if (Crowd.WaveSite == FIntPoint(-1, -1))
		{
			AddError(FString::Printf(
				TEXT("floor 2 of dungeon %d was asked for a wave and has no "
					 "wave site"), Seed));
			return false;
		}

		// THE ORDINARY FLOOR HAS NONE, which is what says the field is not being
		// filled in for every floor.
		if (Spread.WaveSite != FIntPoint(-1, -1))
		{
			AddError(FString::Printf(
				TEXT("floor 2 of dungeon %d was not asked for a wave and has a "
					 "wave site anyway"), Seed));
			return false;
		}

		// **THE GROUPS OF A WAVE STAND CLOSER THAN THE SPACING RULE ALLOWS.**
		// On an ordinary floor no two group middles are within
		// `LeastCellsBetweenPacks` cells walked, so none can be closer than that
		// in a straight line either.
		const int32 ClosestCrowd = ClosestTwoPackSitesWalked(Plan, Crowd);
		const int32 ClosestSpread = ClosestTwoPackSitesWalked(Plan, Spread);

		if (ClosestCrowd >= FCataclysmFloorPopulator::LeastCellsBetweenPacks)
		{
			AddError(FString::Printf(
				TEXT("the wave on floor 2 of dungeon %d has its two closest "
					 "groups %d cells apart, which is not closer than the %d an "
					 "ordinary floor keeps them"),
				Seed, ClosestCrowd,
				FCataclysmFloorPopulator::LeastCellsBetweenPacks));
			return false;
		}

		// THE CONTROL, ON THE SAME FLOOR PLAN. Without it the line above only
		// says a number is small; with it, it says the wave broke a rule the
		// same floor keeps when it is not a wave.
		if (ClosestSpread < FCataclysmFloorPopulator::LeastCellsBetweenPacks)
		{
			AddError(FString::Printf(
				TEXT("an ordinary floor 2 of dungeon %d put two groups %d cells "
					 "apart, inside its own %d cell rule"),
				Seed, ClosestSpread,
				FCataclysmFloorPopulator::LeastCellsBetweenPacks));
			return false;
		}

		++Measured;

		// **AND THE CROWD IS GATHERED AROUND ONE POINT.** Measured as the mean
		// distance of every creature from the wave site, against the same
		// measurement on the same floor populated the ordinary way. A wave that
		// merely dropped the spacing rule would still be scattered, because the
		// candidate cells are drawn from the whole floor; ordering them by
		// distance from the wave site is what gathers them.
		const float CrowdSpread = MeanDistanceFrom(Crowd, Crowd.WaveSite);
		const float SpreadSpread = MeanDistanceFrom(Spread, Crowd.WaveSite);

		WorstWaveSpread = FMath::Max(WorstWaveSpread, CrowdSpread);
		BestOrdinarySpread = FMath::Min(BestOrdinarySpread, SpreadSpread);

		if (CrowdSpread >= SpreadSpread)
		{
			AddError(FString::Printf(
				TEXT("floor 2 of dungeon %d: the wave's creatures average %.1f "
					 "cells from the wave site and the ordinary floor's average "
					 "%.1f, so the wave is not gathered"),
				Seed, CrowdSpread, SpreadSpread));
			return false;
		}
	}

	TestEqual(TEXT("every floor in the sweep was measured"),
			  Measured, CrowdSeeds);

	// THE TWO NUMBERS, SO THE MARGIN IS ON RECORD rather than only the
	// comparison. The check above is per floor and this says how far apart the
	// two populations are across the sweep.
	TestTrue(FString::Printf(
				 TEXT("the loosest wave averages %.1f cells from its site and "
					  "the tightest ordinary floor %.1f"),
				 WorstWaveSpread, BestOrdinarySpread),
			 WorstWaveSpread < BestOrdinarySpread);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorBriefWaveKeepsPromisesTest,
	"Cataclysm.FloorBrief.AWaveKeepsEveryPromiseAnOrdinaryFloorMakes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorBriefWaveKeepsPromisesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorBriefTest;

	// **TURNING A RULE OFF IS WHERE THE OTHER RULES GET BROKEN.** The wave drops
	// the spacing between groups and nothing else, and these are the four things
	// `Cataclysm.DungeonEnemies.*` holds true of every ordinary floor. A wave
	// floor has to keep all four, and the boss on top of them.
	for (int32 Seed = 1; Seed <= SweepSeeds; ++Seed)
	{
		const FCataclysmFloorPlan Plan =
			Floor(Seed, 4, ECataclysmFloorLayout::Arena);

		FCataclysmFloorBrief Wave;
		Wave.bOneWave = true;
		Wave.bBossAtTheExit = true;

		const FCataclysmFloorPopulation Crowd =
			FCataclysmFloorPopulator::Populate(Plan, 1.0f, Wave);

		const TArray<int32> FromEntrance =
			CataclysmFloorDistancesFrom(Plan, Plan.Entrance);

		TSet<FIntPoint> Seen;
		for (const FCataclysmEnemyPlacement& Placement : Crowd.Enemies)
		{
			if (!Plan.IsFloor(Placement.Cell))
			{
				AddError(FString::Printf(
					TEXT("a %s on floor 4 of dungeon %d stands inside rock at "
						 "(%d, %d)"),
					CataclysmDungeonCreatureName(Placement.Creature), Seed,
					Placement.Cell.X, Placement.Cell.Y));
				return false;
			}

			const int32 Index = Plan.IndexOf(Placement.Cell);
			if (FromEntrance[Index] == INDEX_NONE)
			{
				AddError(FString::Printf(
					TEXT("a %s on floor 4 of dungeon %d stands where the player "
						 "cannot walk to"),
					CataclysmDungeonCreatureName(Placement.Creature), Seed));
				return false;
			}

			// THE BOSS IS THE ONE EXCEPTION AND IT IS NOT AN EXCEPTION TO THIS.
			// It stands on the way down, which the floor generator puts a long
			// way from the entrance, so it clears the keep-out too. The check
			// below covers it rather than skipping it.
			if (FromEntrance[Index]
				< FCataclysmFloorPopulator::LeastCellsFromEntrance)
			{
				AddError(FString::Printf(
					TEXT("a %s on floor 4 of dungeon %d waits %d cells from "
						 "where the player arrives, inside the %d cell keep-out"),
					CataclysmDungeonCreatureName(Placement.Creature), Seed,
					FromEntrance[Index],
					FCataclysmFloorPopulator::LeastCellsFromEntrance));
				return false;
			}

			if (Seen.Contains(Placement.Cell))
			{
				AddError(FString::Printf(
					TEXT("two creatures stand on (%d, %d) on floor 4 of dungeon "
						 "%d"),
					Placement.Cell.X, Placement.Cell.Y, Seed));
				return false;
			}
			Seen.Add(Placement.Cell);
		}

		// AND THE WAVE IS ACTUALLY A FLOOR'S WORTH OF CREATURES. A gathering
		// rule that ran out of cells early would pass every check above by
		// placing almost nothing.
		const int32 Crowded =
			Crowd.Enemies.Num()
			- Crowd.HowMany(ECataclysmDungeonCreature::Gatekeeper);
		if (Crowded < Crowd.Wanted * 4 / 5)
		{
			AddError(FString::Printf(
				TEXT("the wave on floor 4 of dungeon %d holds %d creatures "
					 "where the density asked for %d"),
				Seed, Crowded, Crowd.Wanted));
			return false;
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// One of the 117 dungeon modifiers through the same seam
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorBriefUnstableTest,
	"Cataclysm.FloorBrief.UnstableDimensionsGivesEveryFloorOneMoreModifier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorBriefUnstableTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorBriefTest;

	// **THE PROOF THAT THE SEAM IS NOT ONLY FOR SUB-TYPES.**
	// `Chaos_Unstable_Dimensions` in `game/Data/DungeonModifiers.csv`: "Every
	// time you clear a floor, the very fabric of the dungeon warps. A new
	// 'reality' is imposed, granting a new, random modifier to all enemies on
	// the next floor."
	//
	// IT USES THE SAME FIELD VOLATILE USES AND DOES SOMETHING ELSE WITH IT. The
	// sub-type replaces a floor's modifiers; this adds one to them.
	FCataclysmDungeonIdentity Warping = Dungeon(ECataclysmDungeonSubType::None);
	Warping.ModifierPool.Add(
		Make(FCataclysmDungeonFloorRules::UnstableDimensionsKey,
			 ECataclysmType::Chaos, 10.0f));
	Warping.Modifiers.Add(
		FName(FCataclysmDungeonFloorRules::UnstableDimensionsKey));
	Warping.ModifierScore += 10.0f;

	// THE CONTROL IS THE SAME DUNGEON WITHOUT THAT ONE ROW, so the only thing
	// that differs between them is the modifier under test.
	const FCataclysmDungeonIdentity Steady =
		Dungeon(ECataclysmDungeonSubType::None);

	TSet<FString> Extras;

	for (int32 Floor = 1; Floor <= SweepFloors; ++Floor)
	{
		const FCataclysmFloorBrief Warped =
			FCataclysmDungeonFloorRules::BriefFor(Warping, Floor);
		const FCataclysmFloorBrief Plain =
			FCataclysmDungeonFloorRules::BriefFor(Steady, Floor);

		TestEqual(FString::Printf(
					  TEXT("floor %d carries one more than the dungeon drew"),
					  Floor),
				  Warped.Modifiers.Num(), Warping.Modifiers.Num() + 1);

		TestEqual(FString::Printf(
					  TEXT("and the same dungeon without it carries exactly what "
						   "it drew on floor %d"), Floor),
				  Plain.Modifiers.Num(), Steady.Modifiers.Num());

		// EVERY ONE THE DUNGEON DREW IS STILL THERE. It adds rather than
		// replaces, which is the whole difference from the Volatile sub-type.
		for (const FName Key : Warping.Modifiers)
		{
			TestTrue(FString::Printf(
						 TEXT("floor %d still carries %s"), Floor, *Key.ToString()),
					 Warped.Modifiers.Contains(Key));
		}

		// AND NEVER A SECOND COPY OF ONE THE FLOOR ALREADY HAS.
		TSet<FName> Distinct(Warped.Modifiers);
		TestEqual(FString::Printf(TEXT("floor %d carries no modifier twice"), Floor),
				  Distinct.Num(), Warped.Modifiers.Num());

		// AND THE SCORE FOLLOWS THE EXTRA ONE.
		float Danger = 0.0f;
		for (const FName Key : Warped.Modifiers)
		{
			for (const FCataclysmDungeonModifier& Modifier : Warping.ModifierPool)
			{
				if (Modifier.RowKey == Key)
				{
					Danger += Modifier.Danger;
				}
			}
		}
		TestEqual(FString::Printf(
					  TEXT("floor %d is worth the danger of all five"), Floor),
				  Warped.ModifierScore, Danger);

		for (const FName Key : Warped.Modifiers)
		{
			if (!Warping.Modifiers.Contains(Key))
			{
				Extras.Add(Key.ToString());
			}
		}
	}

	// **"A NEW RANDOM MODIFIER" MEANS THE EXTRA CHANGES.** One extra repeated on
	// all twenty floors would satisfy every check above and would not be what
	// the row says.
	TestTrue(FString::Printf(
				 TEXT("twenty floors drew %d different extra modifiers, which is "
					  "more than five"), Extras.Num()),
			 Extras.Num() > 5);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorBriefUnstableComposesTest,
	"Cataclysm.FloorBrief.AVolatileDungeonGetsTheExtraOnTheFloorsThatDrewIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorBriefUnstableComposesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorBriefTest;

	// **TWO RULES ON ONE FIELD, AND NEITHER IS WRITTEN IN TERMS OF THE OTHER.**
	// A Volatile dungeon re-draws its modifiers for every floor, so whether it
	// is carrying Unstable Dimensions is a per-floor fact. The extra has to
	// follow the re-draw rather than the dungeon.
	FCataclysmDungeonIdentity Changing =
		Dungeon(ECataclysmDungeonSubType::Volatile);
	Changing.ModifierPool.Add(
		Make(FCataclysmDungeonFloorRules::UnstableDimensionsKey,
			 ECataclysmType::Chaos, 10.0f));

	const FName Warp(FCataclysmDungeonFloorRules::UnstableDimensionsKey);
	const int32 Drawn = UCataclysmDungeonModifierRules::CountFor(
		Changing.DifficultyTier, ECataclysmDungeonSubType::Volatile);

	int32 FloorsWithTheWarp = 0;
	int32 FloorsWithout = 0;

	// A LONG SWEEP, because whether a re-draw lands one particular modifier out
	// of twenty-one is a coin that has to be flipped enough times for both
	// outcomes to appear.
	for (int32 Floor = 1; Floor <= 200; ++Floor)
	{
		const FCataclysmFloorBrief Brief =
			FCataclysmDungeonFloorRules::BriefFor(Changing, Floor);

		if (Brief.Modifiers.Contains(Warp))
		{
			++FloorsWithTheWarp;
			if (Brief.Modifiers.Num() != Drawn + 1)
			{
				AddError(FString::Printf(
					TEXT("floor %d re-drew Unstable Dimensions and carries %d "
						 "modifiers, not the %d that is the re-draw plus its "
						 "extra"),
					Floor, Brief.Modifiers.Num(), Drawn + 1));
				return false;
			}
		}
		else
		{
			++FloorsWithout;
			if (Brief.Modifiers.Num() != Drawn)
			{
				AddError(FString::Printf(
					TEXT("floor %d did not re-draw Unstable Dimensions and "
						 "carries %d modifiers, not the %d the re-draw asked "
						 "for"),
					Floor, Brief.Modifiers.Num(), Drawn));
				return false;
			}
		}
	}

	// **BOTH OUTCOMES HAVE TO HAPPEN OR THE CHECK ABOVE CHECKED ONE BRANCH.**
	// A sweep in which no floor ever drew it would pass every assertion while
	// proving only that the extra never fires.
	TestTrue(FString::Printf(
				 TEXT("%d of 200 floors re-drew Unstable Dimensions"),
				 FloorsWithTheWarp),
			 FloorsWithTheWarp > 0);
	TestTrue(FString::Printf(TEXT("and %d did not"), FloorsWithout),
			 FloorsWithout > 0);

	return true;
}

// ---------------------------------------------------------------------------
// The answer being spent: a floor's modifiers reaching the enemy score model
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorBriefReachesTheScoreTest,
	"Cataclysm.FloorBrief.WalkingDownAVolatileDungeonChangesWhatItsCreaturesAreWorth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorBriefReachesTheScoreTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorBriefTest;

	// **THE WHOLE ROUTE, AND THE ONLY TEST THAT WALKS IT.** The rules decide a
	// floor's modifiers; `BuildFloor` has to ask them; `RunModifierScore` has to
	// answer with the floor's rather than the dungeon's; and the enemy score
	// model reads that. Each has its own test and all four would pass while the
	// wiring between them did not exist, which is how the modifier score stayed
	// hard-zeroed for four months.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Mode =
		World->SpawnActor<ACataclysmDungeonGameMode>();
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode))
	{
		return false;
	}

	// SET ON THE GAME MODE DIRECTLY RATHER THAN THROUGH A RUN. `EnterEmpireDungeon`
	// filling these in from a real dungeon is the next test; this one is about
	// what the game mode does with them once they are there, and building a
	// surge to get a Volatile dungeon would be a sweep over seeds rather than a
	// test.
	Mode->DungeonSubType = ECataclysmDungeonSubType::Volatile;
	Mode->TotalFloors = SweepFloors;
	Mode->DungeonModifierPool = Pool();
	Mode->DungeonModifiers.Add(Mode->DungeonModifierPool[0].RowKey);
	Mode->DungeonModifierScore = Mode->DungeonModifierPool[0].Danger;

	TSet<float> Scores;
	for (int32 Floor = 1; Floor <= 8; ++Floor)
	{
		if (!TestTrue(FString::Printf(TEXT("floor %d was built"), Floor),
					  Mode->GoToFloor(Floor)))
		{
			return false;
		}

		TestEqual(FString::Printf(
					  TEXT("the game mode answers with floor %d's modifiers"),
					  Floor),
				  Mode->RunModifierScore(), Mode->FloorBrief.ModifierScore);

		TestEqual(TEXT("and the brief is for the floor being stood on"),
				  Mode->FloorBrief.FloorNumber, Floor);

		Scores.Add(Mode->RunModifierScore());
	}

	// **EIGHT FLOORS OF A VOLATILE DUNGEON ARE NOT ALL WORTH THE SAME.** This is
	// the player-visible consequence of the whole change: a creature on floor 3
	// of a Volatile dungeon is not worth what the same creature on floor 2 is.
	TestTrue(FString::Printf(
				 TEXT("eight floors of a Volatile dungeon carried %d different "
					  "modifier scores"), Scores.Num()),
			 Scores.Num() > 1);

	// AND THE DUNGEON'S OWN NUMBER WAS NOT OVERWRITTEN BY ANY OF THEM. The
	// per-floor rules re-draw from what the dungeon is, so a floor that wrote
	// its answer back would make floor 4 a re-draw of floor 3 rather than of the
	// dungeon.
	TestEqual(TEXT("the dungeon's own modifier score is untouched"),
			  Mode->DungeonModifierScore, Mode->DungeonModifierPool[0].Danger);

	// AND AN ORDINARY DUNGEON'S FLOORS ARE ALL WORTH THE SAME, which is the
	// control and is also the behaviour every dungeon had before this change.
	Mode->DungeonSubType = ECataclysmDungeonSubType::None;
	TSet<float> Steady;
	for (int32 Floor = 1; Floor <= 8; ++Floor)
	{
		Mode->GoToFloor(Floor);
		Steady.Add(Mode->RunModifierScore());
	}
	TestEqual(TEXT("eight floors of an ordinary dungeon are all worth the same"),
			  Steady.Num(), 1);
	TestEqual(TEXT("and that is what the dungeon drew"),
			  Steady.Array()[0], Mode->DungeonModifierScore);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorBriefEnteringCarriesThePoolTest,
	"Cataclysm.FloorBrief.EnteringADungeonCarriesTheModifiersItsFloorsMayDraw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorBriefEnteringCarriesThePoolTest::RunTest(const FString& Parameters)
{
	// **THE HALF THE TEST ABOVE CANNOT SEE.** A Volatile dungeon on the empire
	// map can only re-draw if the pool crossed the module line with it, and
	// nothing else in the project carries a pool onto the game mode.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Mode =
		World->SpawnActor<ACataclysmDungeonGameMode>();
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode))
	{
		return false;
	}

	// THE REAL TABLE, so this is the pool the game would really have.
	UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
	Run->ModifierPool = UCataclysmDungeonModifierTable::LoadPool();
	if (!TestTrue(TEXT("the modifier table loaded"), Run->ModifierPool.Num() > 0))
	{
		return false;
	}

	Run->Begin(/* Seed */ 1, ECataclysmSurgeMode::Static,
			   /* LethalityRung */ 0, /* DifficultyTier */ 4);
	Run->AdvanceDay();

	if (!TestTrue(TEXT("the first surge put a dungeon on the map"),
				  Run->Dungeons.Num() > 0))
	{
		return false;
	}

	Mode->SetEmpireRunForTests(Run);
	const FCataclysmDungeon Dungeon = Run->Dungeons[0];

	TestEqual(TEXT("before entering the game mode knows of no modifiers"),
			  Mode->DungeonModifiers.Num(), 0);
	TestEqual(TEXT("and of nothing its floors could draw"),
			  Mode->DungeonModifierPool.Num(), 0);

	if (!TestTrue(TEXT("the dungeon is entered"),
				  Mode->EnterEmpireDungeon(Dungeon.DungeonId)))
	{
		return false;
	}

	TestEqual(TEXT("entering carries which modifiers the dungeon drew"),
			  CataclysmFloorBriefTest::SortedKeys(Mode->DungeonModifiers),
			  CataclysmFloorBriefTest::SortedKeys(Dungeon.Modifiers));

	// **NARROWED TO THE CATACLYSMS THE RUN IS FACING, NOT THE WHOLE TABLE.** A
	// floor that could re-draw from all 117 rows would put a Celestial modifier
	// in a run that is not facing the Celestial Cataclysm.
	const TArray<FCataclysmDungeonModifier> Expected =
		UCataclysmDungeonModifierRules::PoolFor(Run->ModifierPool,
												Run->ActiveCataclysms);
	TestEqual(TEXT("and the modifiers its floors may draw from"),
			  Mode->DungeonModifierPool.Num(), Expected.Num());
	TestTrue(TEXT("which is fewer than the whole table"),
			 Expected.Num() < Run->ModifierPool.Num());
	TestTrue(TEXT("and is not empty"), Expected.Num() > 0);

	// AND LEAVING PUTS ALL OF IT BACK. A player who leaves the empire and walks
	// a plain floor must not still be fighting the last dungeon's modifiers, and
	// the next dungeon must not draw from the Cataclysms this one faced.
	Mode->LeaveEmpireDungeon();
	TestEqual(TEXT("leaving leaves the dungeon's modifiers behind"),
			  Mode->DungeonModifiers.Num(), 0);
	TestEqual(TEXT("and what its floors could have drawn"),
			  Mode->DungeonModifierPool.Num(), 0);
	TestEqual(TEXT("and the floor's own score"),
			  Mode->RunModifierScore(), 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorBriefRealDungeonTest,
	"Cataclysm.FloorBrief.ADungeonOffTheMapReallyGetsItsSubTypesRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorBriefRealDungeonTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorBriefTest;

	// **EVERY OTHER TEST IN THIS FILE BUILDS ITS DUNGEON BY HAND.** All of them
	// would pass while a dungeon that a surge actually landed on a city never
	// reached the rules at all, because the sub-type has to cross from
	// `FCataclysmDungeon` in the empire layer onto the game mode and then into
	// `DungeonIdentity`. This is the only test that walks a dungeon off the map.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Mode =
		World->SpawnActor<ACataclysmDungeonGameMode>();
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode))
	{
		return false;
	}

	UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
	Run->ModifierPool = UCataclysmDungeonModifierTable::LoadPool();
	if (!TestTrue(TEXT("the modifier table loaded"), Run->ModifierPool.Num() > 0))
	{
		return false;
	}

	Run->Begin(/* Seed */ 3, ECataclysmSurgeMode::Static,
			   /* LethalityRung */ 0, /* DifficultyTier */ 4);
	Run->AdvanceDay();

	if (!TestTrue(TEXT("the first surge put a dungeon on the map"),
				  Run->Dungeons.Num() > 0))
	{
		return false;
	}

	Mode->SetEmpireRunForTests(Run);

	// **THE SUB-TYPE IS SET ON THE DUNGEON RATHER THAN SEARCHED FOR.** Waiting
	// for a surge to roll each of the three would be a sweep over seeds, and
	// what is being checked is the route from a dungeon on the map to the rules,
	// which is the same route whatever the roll gave.
	const int32 DungeonId = Run->Dungeons[0].DungeonId;

	// FOUR FLOORS AND AN EMPTY DENSITY. This test is about what the brief says,
	// not about what stands on the floor, and it builds sixteen floors -- four
	// of each of four dungeons. At the designed density that is several thousand
	// spawned characters for nothing, and the crowd itself is measured by
	// `AHordeFloorsCreaturesAreOneCrowdAndNotSeveralEncounters` from a plan,
	// with no world at all.
	//
	// THE BOSS IS STILL PLACED AT A DENSITY OF ZERO, which is deliberate and is
	// asserted by `TheBossStandsOnTheWayDownAndIsNotOneOfTheCrowd`, so the boss
	// half of this test is not weakened by emptying the floor.
	const int32 Floors = 4;
	Run->Dungeons[0].Floors = Floors;
	Mode->EnemyScale = 0.0f;

	struct FCase
	{
		ECataclysmDungeonSubType SubType;
		const TCHAR* Name;
	};
	const TArray<FCase> Cases = {
		{ ECataclysmDungeonSubType::Horde, TEXT("Horde") },
		{ ECataclysmDungeonSubType::Elite, TEXT("Elite") },
		{ ECataclysmDungeonSubType::Volatile, TEXT("Volatile") },
		{ ECataclysmDungeonSubType::None, TEXT("no sub-type") } };

	for (const FCase& Case : Cases)
	{
		Run->Dungeons[0].SubType = Case.SubType;

		if (!TestTrue(FString::Printf(TEXT("a %s dungeon is entered"), Case.Name),
					  Mode->EnterEmpireDungeon(DungeonId)))
		{
			return false;
		}

		// THE SUB-TYPE CROSSED. Without this the three checks below could all
		// pass on a game mode that was still carrying the last case's sub-type.
		TestEqual(FString::Printf(TEXT("the %s dungeon's sub-type reached the "
									   "rules"), Case.Name),
				  static_cast<int32>(Mode->DungeonIdentity().SubType),
				  static_cast<int32>(Case.SubType));

		// AND THE FLOOR BEING STOOD ON IS THE ONE THE RULES DESCRIBE.
		TestEqual(FString::Printf(TEXT("and the %s dungeon's floor 1 brief is "
									   "the one the rules give"), Case.Name),
				  KeysOf(Mode->FloorBrief),
				  KeysOf(FCataclysmDungeonFloorRules::BriefFor(
							 Mode->DungeonIdentity(), 1)));

		TArray<FString> PerFloor;
		TArray<bool> Bosses;
		TArray<int32> Layouts;
		for (int32 Floor = 1; Floor <= Floors; ++Floor)
		{
			Mode->GoToFloor(Floor);
			PerFloor.Add(KeysOf(Mode->FloorBrief));
			Bosses.Add(Mode->FloorBrief.bBossAtTheExit);
			Layouts.Add(static_cast<int32>(Mode->FloorBrief.Layout));
		}

		int32 BossFloors = 0;
		for (const bool bBoss : Bosses)
		{
			BossFloors += bBoss ? 1 : 0;
		}

		int32 ArenaFloors = 0;
		for (const int32 Layout : Layouts)
		{
			ArenaFloors +=
				(Layout == static_cast<int32>(ECataclysmFloorLayout::Arena))
					? 1 : 0;
		}

		const int32 DistinctSets = TSet<FString>(PerFloor).Num();

		AddInfo(FString::Printf(
			TEXT("a %s dungeon of %d floors off the map: %d boss floors, %d "
				 "Arena floors, %d different modifier sets"),
			Case.Name, Floors, BossFloors, ArenaFloors, DistinctSets));

		switch (Case.SubType)
		{
		case ECataclysmDungeonSubType::Horde:
			TestEqual(TEXT("every floor of a Horde dungeon off the map is an "
						   "Arena"), ArenaFloors, Floors);
			TestEqual(TEXT("and it still has one boss, on its last floor"),
					  BossFloors, 1);
			break;

		case ECataclysmDungeonSubType::Elite:
			TestEqual(TEXT("every floor of an Elite dungeon off the map ends "
						   "with a boss"), BossFloors, Floors);
			break;

		case ECataclysmDungeonSubType::Volatile:
			TestTrue(FString::Printf(
						 TEXT("a Volatile dungeon off the map carried %d "
							  "different modifier sets over %d floors"),
						 DistinctSets, Floors),
					 DistinctSets > 1);
			break;

		default:
			// **THE CONTROL, AND IT IS THE SAME DUNGEON ON THE SAME MAP.** One
			// boss on the last floor, no Arena unless the setting says so, and
			// one modifier set for the whole dungeon.
			TestEqual(TEXT("a dungeon with no sub-type has one boss floor"),
					  BossFloors, 1);
			TestEqual(TEXT("and no floor is forced to an Arena"), ArenaFloors, 0);
			TestEqual(TEXT("and every floor carries the same modifiers"),
					  DistinctSets, 1);
			break;
		}

		Mode->LeaveEmpireDungeon();
	}

	return true;
}

// ---------------------------------------------------------------------------
// The modifier this seam names is a real row of the real table
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorBriefUnstableIsARowTest,
	"Cataclysm.FloorBrief.TheModifierThatChangesAFloorIsARealRowOfTheTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorBriefUnstableIsARowTest::RunTest(const FString& Parameters)
{
	// **A ROW KEY WRITTEN IN C++ IS A STRING UNTIL SOMETHING CHECKS IT.** If the
	// design workbook is re-exported with this modifier renamed, every test in
	// this file still passes -- they build their own pool -- and the rule
	// quietly stops firing in the real game. This is what notices.
	const TArray<FCataclysmDungeonModifier> Pool =
		UCataclysmDungeonModifierTable::LoadPool();

	if (!TestTrue(TEXT("the modifier table loaded"), Pool.Num() > 0))
	{
		return false;
	}

	bool bFound = false;
	for (const FCataclysmDungeonModifier& Modifier : Pool)
	{
		if (Modifier.RowKey
			== FName(FCataclysmDungeonFloorRules::UnstableDimensionsKey))
		{
			bFound = true;

			// AND IT IS A CHAOS MODIFIER, so a run not facing Chaos never sees
			// it. That is what stops it being a rule on every dungeon.
			TestEqual(TEXT("Unstable Dimensions belongs to Chaos"),
					  static_cast<int32>(Modifier.Cataclysm),
					  static_cast<int32>(ECataclysmType::Chaos));
		}
	}

	TestTrue(FString::Printf(
				 TEXT("%s is a row of the dungeon modifier table"),
				 FCataclysmDungeonFloorRules::UnstableDimensionsKey),
			 bFound);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
