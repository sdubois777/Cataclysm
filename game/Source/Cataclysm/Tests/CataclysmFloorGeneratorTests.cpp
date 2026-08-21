// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Dungeon/CataclysmFloorGenerator.h"
#include "Dungeon/CataclysmFloorPlan.h"
#include "HAL/PlatformTime.h"

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

#endif // WITH_AUTOMATION_TESTS
