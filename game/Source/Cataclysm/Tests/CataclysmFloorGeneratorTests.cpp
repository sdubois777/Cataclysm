// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Dungeon/CataclysmFloorGenerator.h"
#include "Dungeon/CataclysmFloorPlan.h"
#include "HAL/PlatformTime.h"
#include "Math/RandomStream.h"

/**
 * Tests for the procedural floor generator, issue #40.
 *
 * WHAT THESE PIN, AND WHY IT IS WORTH PINNING NOW. The generator produces a grid
 * of walkable cells and nothing else -- no meshes, no actors, no map. That is
 * deliberate: the automation tests run with `-nullrhi`, so a grid can be checked
 * and a room full of art cannot. Every property here is cheap to hold today and
 * expensive to retrofit once floors are built out of assets.
 *
 * The navigation limits come from the project owner's constraint on 2026-08-21 --
 * "I mostly just want to avoid tiny tedious rooms and hallways that make it
 * annoying for the player to navigate" -- and from what shipped games in the
 * genre had to correct. Diablo 4 patched its dungeons to remove backtracking down
 * hallways to side rooms; Path of Exile's players avoid maze layouts. The sources
 * are recorded on issue #34.
 *
 * THE LIMITS BELOW WERE MEASURED, NOT CHOSEN.
 * `Cataclysm.Dungeon.MeasureWhatEachLayoutProduces` logs what each layout
 * actually produces over a thousand seeds, and every limit here sits outside the
 * worst value that sweep found. What it printed on 2026-08-21:
 *
 *              walkable   single file   longest single   dead    walk to
 *              share      share         file run         ends    the stairs
 *     Halls    >= 0.466   <= 0.0847     <= 1 cell         0      58..133 cells
 *     Caverns  >= 0.263   <= 0.1006     <= 3 cells        0      42..105 cells
 *     Arena    >= 0.521   <= 0.0700     <= 2 cells        0      42..56 cells
 *
 * No layout needed a second attempt and no walkable cell was ever unreachable.
 * Re-run the measurement after changing a generation constant rather than moving
 * a limit to suit.
 */

namespace CataclysmFloorTest
{
	/** Every layout a floor can be carved by, for a sweep that cannot miss one. */
	TArray<ECataclysmFloorLayout> EveryLayout()
	{
		TArray<ECataclysmFloorLayout> Out;
		for (uint8 Index = 0; Index < static_cast<uint8>(ECataclysmFloorLayout::Count); ++Index)
		{
			Out.Add(static_cast<ECataclysmFloorLayout>(Index));
		}
		return Out;
	}

	/** How many seeds each assertion sweeps over. */
	constexpr int32 SweepSeeds = 120;

	/**
	 * How many seeds the measurement sweeps over.
	 *
	 * Wider than the assertions, because the limits are set from what it finds
	 * and a limit set from too few seeds is a limit that fails on the 121st.
	 * A floor costs a third of a millisecond at worst, so this is about a second.
	 */
	constexpr int32 MeasureSeeds = 1000;

	FCataclysmFloorPlan Build(int32 DungeonSeed, int32 FloorNumber,
							  ECataclysmFloorLayout Layout)
	{
		FCataclysmFloorRequest Request;
		Request.DungeonSeed = DungeonSeed;
		Request.FloorNumber = FloorNumber;
		Request.Layout = Layout;
		return FCataclysmFloorGenerator::Generate(Request);
	}

	/** Whether two plans are the same floor, cell for cell. */
	bool SameFloor(const FCataclysmFloorPlan& A, const FCataclysmFloorPlan& B)
	{
		return A.Width == B.Width && A.Height == B.Height
			&& A.Cells == B.Cells
			&& A.Entrance == B.Entrance && A.Exit == B.Exit;
	}
}

// ---------------------------------------------------------------------------
// The measurement the limits below are set from
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorMeasureTest,
	"Cataclysm.Dungeon.MeasureWhatEachLayoutProduces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorMeasureTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorTest;

	// NOT AN ASSERTION TEST. It reports what the generator does so the limits in
	// the tests below can be set from measured numbers instead of guessed ones.
	// It fails only if a floor cannot be built at all, which every other test
	// here would also catch.
	for (const ECataclysmFloorLayout Layout : EveryLayout())
	{
		float WorstOpen = 1.0f;
		float WorstNarrow = 0.0f;
		int32 WorstNarrowRun = 0;
		int32 WorstDeadEnds = 0;
		int32 WorstUnreachable = 0;
		int32 ShortestPath = MAX_int32;
		int32 LongestPath = 0;
		int32 MostAttempts = 0;
		int32 Unbuilt = 0;

		for (int32 Seed = 1; Seed <= MeasureSeeds; ++Seed)
		{
			const FCataclysmFloorPlan Plan = Build(Seed, 1, Layout);
			if (!Plan.IsBuilt())
			{
				++Unbuilt;
				continue;
			}
			const FCataclysmFloorQuality Quality = CataclysmMeasureFloor(Plan);

			WorstOpen = FMath::Min(WorstOpen, Quality.OpenFraction);
			WorstNarrow = FMath::Max(WorstNarrow, Quality.NarrowFraction);
			WorstNarrowRun = FMath::Max(WorstNarrowRun, Quality.LongestNarrowRun);
			WorstDeadEnds = FMath::Max(WorstDeadEnds, Quality.DeadEnds);
			WorstUnreachable = FMath::Max(WorstUnreachable, Quality.UnreachableCells);
			ShortestPath = FMath::Min(ShortestPath, Quality.PathLength);
			LongestPath = FMath::Max(LongestPath, Quality.PathLength);
			MostAttempts = FMath::Max(MostAttempts, Plan.Attempts);
		}

		UE_LOG(LogTemp, Display,
			TEXT("CataclysmFloorMeasure %s: open>=%.3f narrow<=%.4f run<=%d ")
			TEXT("deadends<=%d unreachable<=%d path %d..%d attempts<=%d unbuilt=%d"),
			CataclysmFloorLayoutName(Layout), WorstOpen, WorstNarrow,
			WorstNarrowRun, WorstDeadEnds, WorstUnreachable, ShortestPath,
			LongestPath, MostAttempts, Unbuilt);

		TestEqual(FString::Printf(TEXT("%s builds a floor for every seed"),
				  CataclysmFloorLayoutName(Layout)), Unbuilt, 0);
	}

	return true;
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorSameSeedTest,
	"Cataclysm.Dungeon.TheSameSeedGivesTheSameFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorSameSeedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorTest;

	// Issue #40 requires this twice over: a dungeon must look the same when the
	// player leaves and returns, and a bug in a floor must be reproducible.
	for (const ECataclysmFloorLayout Layout : EveryLayout())
	{
		for (int32 Seed = 1; Seed <= SweepSeeds; ++Seed)
		{
			const FCataclysmFloorPlan First = Build(Seed, 3, Layout);
			const FCataclysmFloorPlan Second = Build(Seed, 3, Layout);
			if (!SameFloor(First, Second))
			{
				AddError(FString::Printf(
					TEXT("%s floor 3 of dungeon %d came out differently the "
						 "second time it was generated"),
					CataclysmFloorLayoutName(Layout), Seed));
				return false;
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorDifferentSeedTest,
	"Cataclysm.Dungeon.ADifferentSeedGivesADifferentFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorDifferentSeedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorTest;

	// THE CONTROL FOR THE TEST ABOVE. "The same seed gives the same floor" also
	// passes on a generator that ignores its seed entirely and returns one fixed
	// floor. This is what makes that test worth anything.
	for (const ECataclysmFloorLayout Layout : EveryLayout())
	{
		int32 Same = 0;
		for (int32 Seed = 1; Seed <= SweepSeeds; ++Seed)
		{
			const FCataclysmFloorPlan First = Build(Seed, 1, Layout);
			const FCataclysmFloorPlan Second = Build(Seed + 1, 1, Layout);
			Same += SameFloor(First, Second) ? 1 : 0;
		}
		TestEqual(FString::Printf(
			TEXT("%s: neighbouring dungeon seeds never give the same floor"),
			CataclysmFloorLayoutName(Layout)), Same, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorIndependenceTest,
	"Cataclysm.Dungeon.AFloorDoesNotDependOnTheFloorsBeforeIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorIndependenceTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorTest;

	// WHAT THIS IS REALLY GUARDING. Issue #40's third acceptance criterion is
	// that a 150-floor dungeon is affordable, and the answer is to build only the
	// floor the player is standing on. That only works while a floor can be built
	// without the floors before it. This fails the moment somebody caches state
	// between calls.
	for (const ECataclysmFloorLayout Layout : EveryLayout())
	{
		const FCataclysmFloorPlan Alone = Build(7777, 40, Layout);

		for (int32 Floor = 1; Floor < 40; ++Floor)
		{
			Build(7777, Floor, Layout);
		}
		const FCataclysmFloorPlan AfterTheOthers = Build(7777, 40, Layout);

		TestTrue(FString::Printf(
			TEXT("%s: floor 40 is the same whether or not floors 1 to 39 were "
				 "built first"), CataclysmFloorLayoutName(Layout)),
			SameFloor(Alone, AfterTheOthers));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorConsecutiveTest,
	"Cataclysm.Dungeon.ConsecutiveFloorsAreDifferentFloors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorConsecutiveTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorTest;

	// Adding the floor number to the dungeon's seed instead of mixing them would
	// make floor 2 of dungeon 100 the same floor as floor 1 of dungeon 101, and
	// would leave a dungeon's floors barely distinguishable from each other.
	for (const ECataclysmFloorLayout Layout : EveryLayout())
	{
		int32 Same = 0;
		for (int32 Floor = 1; Floor < 60; ++Floor)
		{
			if (SameFloor(Build(31, Floor, Layout), Build(31, Floor + 1, Layout)))
			{
				++Same;
			}
		}
		TestEqual(FString::Printf(
			TEXT("%s: no two consecutive floors of one dungeon are the same"),
			CataclysmFloorLayoutName(Layout)), Same, 0);
	}
	return true;
}

// ---------------------------------------------------------------------------
// The floor is playable
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorReachableTest,
	"Cataclysm.Dungeon.TheStairsCanAlwaysBeWalkedTo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorReachableTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorTest;

	// The one property a dungeon floor cannot ship without. Checked alongside
	// unreachable cells, which is the other half of the same fault: floor the
	// player can see across a wall and never stand on.
	for (const ECataclysmFloorLayout Layout : EveryLayout())
	{
		for (int32 Seed = 1; Seed <= SweepSeeds; ++Seed)
		{
			const FCataclysmFloorPlan Plan = Build(Seed, 1, Layout);
			const FCataclysmFloorQuality Quality = CataclysmMeasureFloor(Plan);

			if (!Quality.bExitReachable || Quality.UnreachableCells != 0)
			{
				AddError(FString::Printf(
					TEXT("%s dungeon %d floor 1: exit reachable %d, %d walkable "
						 "cells cannot be reached from the entrance"),
					CataclysmFloorLayoutName(Layout), Seed,
					Quality.bExitReachable ? 1 : 0, Quality.UnreachableCells));
				return false;
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorNotAMazeTest,
	"Cataclysm.Dungeon.NoFloorIsAMazeOfPassagesOneCellWide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorNotAMazeTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorTest;

	// The project owner's constraint, as a number. A cell in a passage one cell
	// wide has at most two walkable neighbours, because it can only be left the
	// way it was entered. A cell in a passage two cells wide has three. So a run
	// of narrow cells touching each other IS a stretch of single-file passage,
	// and its length is how far the player has to walk in single file.
	//
	// THE FRACTION WAS TRIED FIRST AND WAS TOO BLUNT TO BE WORTH ASSERTING ON.
	// Breaking `ConnectionWidth` to 1, which makes every corridor in a Halls
	// floor single file, moved the fraction only from 0.0847 to 0.1549 -- rooms
	// are most of a floor's cells either way -- and a limit set with any margin
	// above the honest value did not fire. Measured on 2026-08-21. The fraction
	// is still reported by the measurement, because it is worth reading; it is
	// the run length that is held.
	//
	// SIX, AGAINST A WORST MEASURED RUN OF THREE over a thousand seeds of each
	// layout. Three comes from a cavern; a room's four corners are narrow cells
	// but do not touch each other, so each is a run of one.
	constexpr int32 LongestSingleFileRun = 6;

	for (const ECataclysmFloorLayout Layout : EveryLayout())
	{
		for (int32 Seed = 1; Seed <= SweepSeeds; ++Seed)
		{
			const FCataclysmFloorPlan Plan = Build(Seed, 1, Layout);
			const FCataclysmFloorQuality Quality = CataclysmMeasureFloor(Plan);

			if (Quality.LongestNarrowRun > LongestSingleFileRun)
			{
				AddError(FString::Printf(
					TEXT("%s dungeon %d floor 1: a stretch of %d cells has to be "
						 "walked in single file, above the %d cell limit. %.1f%% "
						 "of its %d walkable cells are single file."),
					CataclysmFloorLayoutName(Layout), Seed,
					Quality.LongestNarrowRun, LongestSingleFileRun,
					Quality.NarrowFraction * 100.0f, Quality.FloorCells));
				return false;
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorOpenTest,
	"Cataclysm.Dungeon.AFloorIsMostlyOpenSpaceRatherThanRock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorOpenTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorTest;

	for (const ECataclysmFloorLayout Layout : EveryLayout())
	{
		for (int32 Seed = 1; Seed <= SweepSeeds; ++Seed)
		{
			const FCataclysmFloorPlan Plan = Build(Seed, 1, Layout);
			const FCataclysmFloorQuality Quality = CataclysmMeasureFloor(Plan);

			if (Quality.OpenFraction < FCataclysmFloorGenerator::MinOpenFraction)
			{
				AddError(FString::Printf(
					TEXT("%s dungeon %d floor 1: only %.1f%% of the grid is "
						 "walkable, below the %.1f%% the generator promises, and "
						 "it used %d of %d attempts"),
					CataclysmFloorLayoutName(Layout), Seed,
					Quality.OpenFraction * 100.0f,
					FCataclysmFloorGenerator::MinOpenFraction * 100.0f,
					Plan.Attempts, FCataclysmFloorGenerator::MaxAttempts));
				return false;
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorWalkTest,
	"Cataclysm.Dungeon.TheWalkToTheStairsIsWorthTaking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorWalkTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorTest;

	// The project owner set the pace on 2026-08-21: "Each floor should take the
	// player between 2-5 minutes to complete so long as they're being efficient
	// and don't get unlucky when searching for the stairs."
	//
	// THIS CHECKS THE WALK AND NOT THE MINUTES. Most of those minutes are spent
	// fighting, and no test here knows how long a fight takes. What it can hold
	// is that the shortest route from the entrance to the stairs is a real walk
	// across the floor rather than a few paces. Twenty-five cells is 100 metres
	// at the generator's four-metre cell, which is 25 seconds at the designed
	// default walking speed of 4 metres per second.
	//
	// The shortest measured was 42 cells, in the arena and in a cavern.
	constexpr int32 LeastCells = 25;

	// AND AN UPPER BOUND, because the budget runs out at the other end too. The
	// longest measured was 133 cells in the halls: 532 metres, over two minutes
	// of walking before a single fight. That is already at the top of what a
	// two-to-five minute floor can carry, so this is set 20% above it rather
	// than generously.
	constexpr int32 MostCells = 160;

	for (const ECataclysmFloorLayout Layout : EveryLayout())
	{
		for (int32 Seed = 1; Seed <= SweepSeeds; ++Seed)
		{
			const FCataclysmFloorPlan Plan = Build(Seed, 1, Layout);
			const FCataclysmFloorQuality Quality = CataclysmMeasureFloor(Plan);

			if (Quality.PathLength < LeastCells || Quality.PathLength > MostCells)
			{
				AddError(FString::Printf(
					TEXT("%s dungeon %d floor 1: the stairs are %d cells from the "
						 "entrance, outside the %d to %d cell range"),
					CataclysmFloorLayoutName(Layout), Seed,
					Quality.PathLength, LeastCells, MostCells));
				return false;
			}
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// A deep dungeon is affordable
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorDeepDungeonTest,
	"Cataclysm.Dungeon.ADeepDungeonGeneratesInTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorDeepDungeonTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorTest;

	// Issue #40's third acceptance criterion. Floor counts run past 150 and the
	// Cataclysm dungeon grows past that in the Last Stand.
	//
	// THE BUDGET IS FOR ALL 150 AT ONCE, WHICH IS THE PESSIMISTIC CASE. Nothing
	// makes the game build a floor before the player reaches it, and the test
	// above proves it does not have to. This is here so that a change which makes
	// one floor far slower is noticed.
	//
	// TWO SECONDS AGAINST A MEASURED WORST OF 0.045, which was the caverns. Halls
	// and the arena were near 0.011. The budget is loose on purpose because this
	// only ever runs on a developer's machine -- continuous integration does not
	// build the C++, which is issue #20 -- so it has to survive a slower one
	// without becoming a false alarm.
	constexpr double MostSecondsForAll = 2.0;
	constexpr int32 Floors = 150;

	for (const ECataclysmFloorLayout Layout : EveryLayout())
	{
		const double Started = FPlatformTime::Seconds();
		int32 Built = 0;
		for (int32 Floor = 1; Floor <= Floors; ++Floor)
		{
			Built += Build(9001, Floor, Layout).IsBuilt() ? 1 : 0;
		}
		const double Took = FPlatformTime::Seconds() - Started;

		UE_LOG(LogTemp, Display,
			TEXT("CataclysmFloorMeasure %s: %d floors in %.3f s (%.1f ms each)"),
			CataclysmFloorLayoutName(Layout), Floors, Took,
			(Took * 1000.0) / Floors);

		TestEqual(FString::Printf(TEXT("%s builds all %d floors"),
				  CataclysmFloorLayoutName(Layout), Floors), Built, Floors);

		TestTrue(FString::Printf(
			TEXT("%s builds %d floors in under %.1f s, took %.3f s"),
			CataclysmFloorLayoutName(Layout), Floors, MostSecondsForAll, Took),
			Took < MostSecondsForAll);
	}
	return true;
}

// ---------------------------------------------------------------------------
// The seed mixing itself
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorSeedTest,
	"Cataclysm.Dungeon.FloorSeedsDoNotCollideBetweenNeighbouringDungeons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorSeedTest::RunTest(const FString& Parameters)
{
	// THE FAULT THIS RULES OUT. `DungeonSeed + FloorNumber` would give floor 2 of
	// dungeon 100 and floor 1 of dungeon 101 the same seed, so two dungeons side
	// by side would share nearly every floor. Sixty dungeons of sixty floors is
	// 3,600 seeds; if the mixing works they are 3,600 different numbers.
	TSet<int32> Seen;
	int32 Made = 0;

	for (int32 Dungeon = 1; Dungeon <= 60; ++Dungeon)
	{
		for (int32 Floor = 1; Floor <= 60; ++Floor)
		{
			Seen.Add(FCataclysmFloorGenerator::SeedForFloor(Dungeon, Floor));
			++Made;
		}
	}

	TestEqual(TEXT("3,600 dungeon-and-floor pairs give 3,600 different seeds"),
			  Seen.Num(), Made);

	// Never negative, so it can be handed to FRandomStream without a sign to
	// reason about. Checked over inputs that overflow the mixing on purpose.
	bool bAllPositive = true;
	for (int32 Dungeon = -5000; Dungeon <= 5000; Dungeon += 37)
	{
		for (int32 Floor = 1; Floor <= 200; Floor += 7)
		{
			bAllPositive &= FCataclysmFloorGenerator::SeedForFloor(Dungeon, Floor) >= 0;
		}
	}
	TestTrue(TEXT("a floor seed is never negative, including for negative "
				  "dungeon seeds"), bAllPositive);

	return true;
}

// ---------------------------------------------------------------------------
// Floors differ from one another
// ---------------------------------------------------------------------------

namespace CataclysmFloorTest
{
	/**
	 * What the generator will roll one floor to be like.
	 *
	 * MIRRORS `Generate`'s FIRST ATTEMPT EXACTLY, and
	 * `Cataclysm.Dungeon.TheRolledShapeIsTheShapeTheFloorIsBuiltTo` proves it.
	 * Without that proof this whole file could be sweeping numbers nothing uses.
	 */
	FCataclysmFloorShape ShapeFor(int32 DungeonSeed, int32 FloorNumber)
	{
		FCataclysmFloorRequest Request;
		Request.DungeonSeed = DungeonSeed;
		Request.FloorNumber = FloorNumber;

		const int32 Seed =
			FCataclysmFloorGenerator::SeedForFloor(DungeonSeed, FloorNumber);
		FRandomStream Stream(FCataclysmFloorGenerator::SeedForFloor(Seed, 1));

		return FCataclysmFloorGenerator::RollShape(Stream, Request);
	}

	/** How many different values a run of numbers took, and the commonest share. */
	struct FSpread
	{
		int32 Distinct = 0;
		float CommonestShare = 1.0f;
		int32 Least = MAX_int32;
		int32 Most = MIN_int32;
	};

	FSpread SpreadOf(const TArray<int32>& Values)
	{
		FSpread Out;
		if (Values.Num() == 0)
		{
			return Out;
		}

		TMap<int32, int32> Counts;
		for (const int32 Value : Values)
		{
			++Counts.FindOrAdd(Value);
			Out.Least = FMath::Min(Out.Least, Value);
			Out.Most = FMath::Max(Out.Most, Value);
		}

		int32 Commonest = 0;
		for (const TPair<int32, int32>& Pair : Counts)
		{
			Commonest = FMath::Max(Commonest, Pair.Value);
		}

		Out.Distinct = Counts.Num();
		Out.CommonestShare =
			static_cast<float>(Commonest) / static_cast<float>(Values.Num());
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorShapeIsUsedTest,
	"Cataclysm.Dungeon.TheRolledShapeIsTheShapeTheFloorIsBuiltTo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorShapeIsUsedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorTest;

	// THE CONTROL FOR EVERY VARIETY TEST BELOW. They sweep what `RollShape`
	// returns, which would be a set of numbers nothing reads if the generator
	// ignored them. This checks the floor that comes out really is the size it
	// was rolled to be, and really has the corridor width it was rolled.
	int32 Checked = 0;
	for (int32 Seed = 1; Seed <= 60; ++Seed)
	{
		const FCataclysmFloorPlan Plan = Build(Seed, 1, ECataclysmFloorLayout::Halls);

		// Only floors that came out on the first attempt, because a re-roll rolls
		// a new shape and the helper above only mirrors the first.
		if (Plan.Attempts != 1)
		{
			continue;
		}
		++Checked;

		const FCataclysmFloorShape Rolled = ShapeFor(Seed, 1);

		TestEqual(FString::Printf(TEXT("dungeon %d: the floor is as wide as it "
									   "was rolled"), Seed),
				  Plan.Width, Rolled.Width);
		TestEqual(FString::Printf(TEXT("dungeon %d: and as deep"), Seed),
				  Plan.Height, Rolled.Height);
		TestEqual(FString::Printf(TEXT("dungeon %d: and the plan reports the "
									   "shape it was built to"), Seed),
				  Plan.Shape.MinLeafSide, Rolled.MinLeafSide);
		TestEqual(FString::Printf(TEXT("dungeon %d: including its corridor width"),
				  Seed), Plan.Shape.ConnectionWidth, Rolled.ConnectionWidth);
	}

	TestTrue(FString::Printf(TEXT("there were floors to check: %d"), Checked),
			 Checked > 40);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorVarietyTest,
	"Cataclysm.Dungeon.FloorsDifferInCharacterAndNotOnlyInArrangement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorVarietyTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorTest;

	// THE FAULT THIS EXISTS FOR, in the project owner's words on 2026-08-21:
	// "Nobody wants to play a 50 floor dungeon where every floor is the same
	// layout, or just some combination of 3 different layouts."
	//
	// Before this, every floor was 40 by 40 cells with corridors exactly two
	// cells wide and between ten and sixteen rectangular rooms. The arrangement
	// varied and nothing else did, and no test could tell -- every guarantee in
	// this file is about ONE floor being good, and a thousand identical good
	// floors satisfy all of them.
	//
	// SWEPT OVER ONE DUNGEON'S FLOORS, NOT OVER DUNGEONS. Floors 1 to 200 of
	// dungeon 1 is the case that matters: a player walks down through them one
	// after another. Variety across dungeons would not help them at all.
	constexpr int32 Floors = 200;

	TArray<int32> Widths, Heights, RoomSizes, CorridorWidths, Loops;
	for (int32 Floor = 1; Floor <= Floors; ++Floor)
	{
		const FCataclysmFloorShape Shape = ShapeFor(/*DungeonSeed=*/1, Floor);
		Widths.Add(Shape.Width);
		Heights.Add(Shape.Height);
		RoomSizes.Add(Shape.MinLeafSide);
		CorridorWidths.Add(Shape.ConnectionWidth);
		Loops.Add(Shape.ExtraConnections);
	}

	struct FKnob
	{
		const TCHAR* Name;
		const TArray<int32>* Values;
		int32 LeastDistinct;
		float MostCommonShare;
	};

	// EACH LIMIT IS WELL INSIDE WHAT THE RANGES ALLOW, so this fails when a knob
	// stops varying and not when a roll comes out lopsided. A knob rolled over
	// seventeen values that produced only three would be a knob barely varying.
	const FKnob Knobs[] = {
		{ TEXT("floor width"),   &Widths,         8, 0.30f },
		{ TEXT("floor depth"),   &Heights,        8, 0.30f },
		{ TEXT("room size"),     &RoomSizes,      5, 0.40f },
		{ TEXT("corridor width"), &CorridorWidths, 2, 0.85f },
		{ TEXT("loops"),         &Loops,          5, 0.40f },
	};

	for (const FKnob& Knob : Knobs)
	{
		const FSpread Spread = SpreadOf(*Knob.Values);

		UE_LOG(LogTemp, Display,
			TEXT("CataclysmFloorVariety %s: %d different values over %d floors, "
				 "%d..%d, commonest is %.0f%%"),
			Knob.Name, Spread.Distinct, Floors, Spread.Least, Spread.Most,
			Spread.CommonestShare * 100.0f);

		TestTrue(FString::Printf(
			TEXT("%s takes at least %d different values across %d floors of one "
				 "dungeon; it took %d (%d to %d)"),
			Knob.Name, Knob.LeastDistinct, Floors, Spread.Distinct,
			Spread.Least, Spread.Most),
			Spread.Distinct >= Knob.LeastDistinct);

		TestTrue(FString::Printf(
			TEXT("no single %s covers more than %.0f%% of floors; the commonest "
				 "covers %.0f%%"),
			Knob.Name, Knob.MostCommonShare * 100.0f,
			Spread.CommonestShare * 100.0f),
			Spread.CommonestShare <= Knob.MostCommonShare);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorPlayVarietyTest,
	"Cataclysm.Dungeon.WalkingDownADungeonIsNotTheSameWalkEveryTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorPlayVarietyTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorTest;

	// THE KNOBS VARYING IS NOT THE POINT; WHAT COMES OUT OF THEM IS. This builds
	// real floors and measures two things a player would notice: how far the
	// stairs are, and how much of the floor is open space. A generator whose
	// knobs varied but whose floors all played the same would pass the test above
	// and fail this one.
	constexpr int32 Floors = 60;

	for (const ECataclysmFloorLayout Layout : EveryLayout())
	{
		TArray<int32> Walks;
		TArray<int32> OpenPercents;

		for (int32 Floor = 1; Floor <= Floors; ++Floor)
		{
			const FCataclysmFloorPlan Plan = Build(/*DungeonSeed=*/1, Floor, Layout);
			const FCataclysmFloorQuality Quality = CataclysmMeasureFloor(Plan);
			Walks.Add(Quality.PathLength);
			OpenPercents.Add(FMath::RoundToInt(Quality.OpenFraction * 100.0f));
		}

		const FSpread Walk = SpreadOf(Walks);
		const FSpread Open = SpreadOf(OpenPercents);

		UE_LOG(LogTemp, Display,
			TEXT("CataclysmFloorVariety %s over %d floors: walk %d..%d cells "
				 "(%d different), open %d..%d%% (%d different)"),
			CataclysmFloorLayoutName(Layout), Floors, Walk.Least, Walk.Most,
			Walk.Distinct, Open.Least, Open.Most, Open.Distinct);

		// THE WALK IS THE ONE A PLAYER FEELS, AND IT IS JUDGED AS A RATIO.
		//
		// A limit counted in cells would have to be three different limits. An
		// arena is one open space by definition, so its walk is close to the
		// floor's diameter and is bounded by how big a floor may be; halls wander
		// and can be far longer. Measured over sixty floors of one dungeon: halls
		// ran 49 to 111 cells, caverns 57 to 89, arenas 30 to 60 -- ratios of
		// 2.27, 1.56 and 2.00.
		//
		// A ratio says the thing worth saying once, for all three: the longest
		// floor of a dungeon takes meaningfully longer to cross than the shortest.
		// 1.4 sits under all three with margin and well above 1.0, which is what a
		// generator with nothing varying would produce.
		constexpr float LeastWalkRatio = 1.4f;
		const float WalkRatio = (Walk.Least > 0)
			? static_cast<float>(Walk.Most) / static_cast<float>(Walk.Least)
			: 0.0f;

		TestTrue(FString::Printf(
			TEXT("%s: the longest floor of a dungeon is at least %.1f times the "
				 "walk of the shortest; it ran %d to %d cells, a ratio of %.2f"),
			CataclysmFloorLayoutName(Layout), LeastWalkRatio, Walk.Least,
			Walk.Most, WalkRatio),
			WalkRatio >= LeastWalkRatio);

		TestTrue(FString::Printf(
			TEXT("%s: how open a floor is varies by at least 10 points; it ran "
				 "%d%% to %d%%"),
			CataclysmFloorLayoutName(Layout), Open.Least, Open.Most),
			Open.Most - Open.Least >= 10);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
