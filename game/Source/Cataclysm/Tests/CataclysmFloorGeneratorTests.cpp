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

// ---------------------------------------------------------------------------
// Gated shortcuts. Issues #1820 and #41. Ruled 2026-10-04: a corridor two cells across and at most twelve long, carved
// between two walkable cells whose walk apart it shortens by ten or more, with a gate of two new cells.
// ---------------------------------------------------------------------------

namespace CataclysmGatedShortcutTest
{
	/** The Halls plan of one of the seeds the measurements of 2026-10-02 used. */
	FCataclysmFloorPlan HallsPlan(int32 Seed, int32 FloorNumber)
	{
		FCataclysmFloorRequest Request;
		Request.DungeonSeed = 1000 + Seed * 37;
		Request.FloorNumber = FloorNumber;
		Request.Layout = ECataclysmFloorLayout::Halls;
		return FCataclysmFloorGenerator::Generate(Request);
	}

	int32 WalkBetween(const FCataclysmFloorPlan& Plan, FIntPoint From, FIntPoint To)
	{
		return CataclysmFloorDistancesFrom(Plan, From)[Plan.IndexOf(To)];
	}

	int32 CellsCutOff(const FCataclysmFloorPlan& Plan)
	{
		const TArray<int32> Distance = CataclysmFloorDistancesFrom(Plan, Plan.Entrance);
		int32 CutOff = 0;
		for (int32 Index = 0; Index < Plan.Cells.Num(); ++Index)
		{
			CutOff += (Plan.Cells[Index] == ECataclysmFloorCell::Floor && Distance[Index] == INDEX_NONE) ? 1 : 0;
		}
		return CutOff;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGatedShortcutsAnyPairTest,
	"Cataclysm.FloorGenerator.AGatedShortcutSavesTenCellsAndItsGateClosesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGatedShortcutsAnyPairTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmGatedShortcutTest;

	// EVERY HALLS PLAN MEASURED HAD AT LEAST TWO SHORTCUTS SHARING NO CELL. Ten of them here.
	for (int32 Seed = 1; Seed <= 10; ++Seed)
	{
		const FCataclysmFloorPlan Plan = HallsPlan(Seed, 1);
		FRandomStream Stream(Seed);
		const TArray<FCataclysmFloorShortcut> Found =
			FCataclysmFloorGenerator::FindShortcuts(Plan, Stream, 4, TSet<FIntPoint>());
		if (!TestTrue(FString::Printf(TEXT("seed %d: at least two shortcuts (%d)"), Seed, Found.Num()), Found.Num() >= 2))
		{
			continue;
		}
		TSet<FIntPoint> Seen;
		for (const FCataclysmFloorShortcut& One : Found)
		{
			const int32 Before = WalkBetween(Plan, One.A, One.B);
			FCataclysmFloorPlan Carved = Plan;
			FCataclysmFloorGenerator::CarveShortcut(Carved, One);
			const int32 After = WalkBetween(Carved, One.A, One.B);
			TestEqual(FString::Printf(TEXT("seed %d: carving it saves what it said"), Seed), Before - After, One.Saving);
			TestTrue(FString::Printf(TEXT("seed %d: and that is ten cells or more (%d)"), Seed, One.Saving),
					 One.Saving >= FCataclysmFloorGenerator::ShortcutLeastSaving);
			TestTrue(TEXT("its ends are at most twelve cells apart"),
					 FMath::Abs(One.A.X - One.B.X) + FMath::Abs(One.A.Y - One.B.Y)
						 <= FCataclysmFloorGenerator::ShortcutMostLength);

			// EVERY NEW CELL WAS ROCK AND IS FLOOR ONCE CARVED, AND NO TWO SHORTCUTS SHARE ONE.
			for (const FIntPoint& Cell : One.NewCells)
			{
				TestFalse(TEXT("a new cell was rock"), Plan.IsFloor(Cell));
				TestTrue(TEXT("and is floor once carved"), Carved.IsFloor(Cell));
				TestFalse(TEXT("and belongs to no other shortcut"), Seen.Contains(Cell));
				Seen.Add(Cell);
			}

			// THE GATE: two new cells side by side, whose closing gives the first walk back and strands nothing.
			if (!TestEqual(TEXT("a gate is two cells"), One.Gate.Num(), 2))
			{
				continue;
			}
			TestTrue(TEXT("both of them new"), One.NewCells.Contains(One.Gate[0]) && One.NewCells.Contains(One.Gate[1]));
			TestEqual(TEXT("side by side"),
					  FMath::Abs(One.Gate[0].X - One.Gate[1].X) + FMath::Abs(One.Gate[0].Y - One.Gate[1].Y), 1);
			FCataclysmFloorPlan Closed = Carved;
			Closed.Cells[Closed.IndexOf(One.Gate[0])] = ECataclysmFloorCell::Solid;
			Closed.Cells[Closed.IndexOf(One.Gate[1])] = ECataclysmFloorCell::Solid;
			TestEqual(TEXT("closing the gate gives the first walk back"), WalkBetween(Closed, One.A, One.B), Before);
			TestEqual(TEXT("and strands no walkable cell"), CellsCutOff(Closed), 0);
		}

		// THE SAME STREAM GIVES THE SAME SHORTCUTS.
		FRandomStream Again(Seed);
		const TArray<FCataclysmFloorShortcut> Second =
			FCataclysmFloorGenerator::FindShortcuts(Plan, Again, 4, TSet<FIntPoint>());
		if (TestEqual(TEXT("as many the second time"), Second.Num(), Found.Num()))
		{
			for (int32 Index = 0; Index < Found.Num(); ++Index)
			{
				TestTrue(TEXT("and the same ones"), Second[Index].A == Found[Index].A && Second[Index].B == Found[Index].B
						 && Second[Index].Gate == Found[Index].Gate);
			}
		}
	}

	// AN ARENA HAS NONE, which is why the gate rows do nothing there.
	FCataclysmFloorRequest Arena;
	Arena.DungeonSeed = 1037;
	Arena.FloorNumber = 1;
	Arena.Layout = ECataclysmFloorLayout::Arena;
	FRandomStream Stream(1);
	TestEqual(TEXT("an arena has no shortcut"),
			  FCataclysmFloorGenerator::FindShortcuts(FCataclysmFloorGenerator::Generate(Arena), Stream, 4,
													 TSet<FIntPoint>()).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGatedShortcutBetweenTest,
	"Cataclysm.FloorGenerator.AShortcutBetweenTwoCellsShortensThatWalkOrIsRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGatedShortcutBetweenTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmGatedShortcutTest;

	// MEASURED 2026-10-02: this plan's entrance-to-exit walk can be shortened by 66 cells, and that one by none.
	const FCataclysmFloorPlan Winding = HallsPlan(5, 10);
	FCataclysmFloorShortcut Found;
	if (TestTrue(TEXT("the winding plan has a shortcut to the exit"),
				 FCataclysmFloorGenerator::FindShortcutBetween(Winding, Winding.Entrance, Winding.Exit, TSet<FIntPoint>(),
															   Found)))
	{
		const int32 Before = WalkBetween(Winding, Winding.Entrance, Winding.Exit);
		FCataclysmFloorPlan Carved = Winding;
		FCataclysmFloorGenerator::CarveShortcut(Carved, Found);
		TestEqual(TEXT("carving it saves what it said"), Before - WalkBetween(Carved, Carved.Entrance, Carved.Exit),
				  Found.Saving);
		TestTrue(FString::Printf(TEXT("ten cells or more (%d)"), Found.Saving),
				 Found.Saving >= FCataclysmFloorGenerator::ShortcutLeastSaving);

		// A CELL IT WOULD USE, GIVEN AS ONE TO AVOID, RULES THAT CORRIDOR OUT.
		FCataclysmFloorShortcut Other;
		const bool bAnother = FCataclysmFloorGenerator::FindShortcutBetween(
			Winding, Winding.Entrance, Winding.Exit, TSet<FIntPoint>(Found.NewCells), Other);
		if (bAnother)
		{
			for (const FIntPoint& Cell : Other.NewCells)
			{
				TestFalse(TEXT("another shortcut uses none of the avoided cells"), Found.NewCells.Contains(Cell));
			}
		}
	}

	const FCataclysmFloorPlan Straight = HallsPlan(1, 1);
	FCataclysmFloorShortcut None;
	TestFalse(TEXT("a plan whose walk is already as short as it can be has none"),
			  FCataclysmFloorGenerator::FindShortcutBetween(Straight, Straight.Entrance, Straight.Exit,
															TSet<FIntPoint>(), None));
	TestFalse(TEXT("and rock is no end for one"),
			  FCataclysmFloorGenerator::FindShortcutBetween(Straight, FIntPoint(-1, -1), Straight.Exit, TSet<FIntPoint>(),
															None));
	return true;
}

// ---------------------------------------------------------------------------
// Sections. Issues #1820 and #41. Ruled 2026-10-08: a Halls floor is divided into three sections, or failing that two,
// by closing lines of two to six cells, at most three lines and twelve cells a boundary, every area a tenth of the
// walkable cells or more. `FCataclysmFloorGenerator::FindSections` states the rules; `CheckEveryRule` below works each
// one out again from `CataclysmFloorDistancesFrom` on a copy of the plan with the boundary cells closed.
// ---------------------------------------------------------------------------

namespace CataclysmFloorSectionsTest
{
	/** One of the plans the measurements of 2026-10-02 and 2026-10-08 used, in the layout asked for. */
	FCataclysmFloorPlan PlanOf(int32 Seed, int32 FloorNumber, ECataclysmFloorLayout Layout)
	{
		FCataclysmFloorRequest Request;
		Request.DungeonSeed = 1000 + Seed * 37;
		Request.FloorNumber = FloorNumber;
		Request.Layout = Layout;
		return FCataclysmFloorGenerator::Generate(Request);
	}

	/** A plan of this size, all rock, for a test to carve by hand. Its layout is Halls, which is the default. */
	FCataclysmFloorPlan Rock(int32 Width, int32 Height)
	{
		FCataclysmFloorPlan Plan;
		Plan.Reset(Width, Height);
		return Plan;
	}

	/** Carves every cell from (FirstX, FirstY) to (LastX, LastY), both included. */
	void CarveBlock(FCataclysmFloorPlan& Plan, int32 FirstX, int32 FirstY, int32 LastX, int32 LastY)
	{
		for (int32 Y = FirstY; Y <= LastY; ++Y)
		{
			for (int32 X = FirstX; X <= LastX; ++X)
			{
				Plan.Carve(FIntPoint(X, Y));
			}
		}
	}

	/** A copy of the plan with these cells made rock. */
	FCataclysmFloorPlan WithClosed(const FCataclysmFloorPlan& Plan, const TArray<FIntPoint>& Cells)
	{
		FCataclysmFloorPlan Closed = Plan;
		for (const FIntPoint& Cell : Cells)
		{
			Closed.Fill(Cell);
		}
		return Closed;
	}

	/** Whether every one of these cells lies from (FirstX, FirstY) to (LastX, LastY), both included. */
	bool AllWithin(const TArray<FIntPoint>& Cells, int32 FirstX, int32 FirstY, int32 LastX, int32 LastY)
	{
		for (const FIntPoint& Cell : Cells)
		{
			if (Cell.X < FirstX || Cell.X > LastX || Cell.Y < FirstY || Cell.Y > LastY)
			{
				return false;
			}
		}
		return true;
	}

	/** Numbers written as "29/25/29", for a log line and a failure message. */
	FString Joined(const TArray<int32>& Numbers)
	{
		FString Out;
		for (int32 Index = 0; Index < Numbers.Num(); ++Index)
		{
			Out += (Index > 0 ? TEXT("/") : TEXT("")) + FString::FromInt(Numbers[Index]);
		}
		return Out.IsEmpty() ? FString(TEXT("none")) : Out;
	}

	/**
	 * The connected areas of a plan, worked out with `CataclysmFloorDistancesFrom` and nothing of the search's own:
	 * which area every walkable cell is in, and how many cells each area holds.
	 */
	TArray<int32> AreasOf(const FCataclysmFloorPlan& Plan, TArray<int32>& OutArea)
	{
		OutArea.Init(INDEX_NONE, Plan.Cells.Num());
		TArray<int32> Sizes;
		for (int32 Index = 0; Index < Plan.Cells.Num(); ++Index)
		{
			if (Plan.Cells[Index] != ECataclysmFloorCell::Floor || OutArea[Index] != INDEX_NONE)
			{
				continue;
			}
			const TArray<int32> Distance = CataclysmFloorDistancesFrom(Plan, Plan.CellAt(Index));
			int32 Size = 0;
			for (int32 Other = 0; Other < Distance.Num(); ++Other)
			{
				if (Distance[Other] != INDEX_NONE)
				{
					OutArea[Other] = Sizes.Num();
					++Size;
				}
			}
			Sizes.Add(Size);
		}
		return Sizes;
	}

	/**
	 * Asserts every rule of an answer that has sections, and returns how many cells each section holds.
	 *
	 * `Asked` is the plan `FindSections` was given. `CheckOn` is the plan the rules are worked out on: the same plan,
	 * or a copy with a shortcut's gate closed. A tenth is counted on `Asked`, as the rule says.
	 */
	TArray<int32> CheckEveryRule(FAutomationTestBase& Test, const FString& Label, const FCataclysmFloorPlan& Asked,
								 const FCataclysmFloorPlan& CheckOn, const FCataclysmFloorSections& Sections)
	{
		TArray<int32> SectionSizes;
		const int32 Count = Sections.SectionCount();
		if (!Test.TestTrue(Label + TEXT(": two sections or three"), Count == 2 || Count == 3))
		{
			return SectionSizes;
		}
		if (!Test.TestEqual(Label + TEXT(": a section number for every cell of the plan"), Sections.Section.Num(),
							Asked.Cells.Num()))
		{
			return SectionSizes;
		}
		const int32 LeastArea = (Asked.FloorCount() + FCataclysmFloorGenerator::SectionLeastAreaOneIn - 1)
			/ FCataclysmFloorGenerator::SectionLeastAreaOneIn;

		// THE BOUNDARIES THEMSELVES.
		FCataclysmFloorPlan AllClosed = CheckOn;
		int32 MostCells = 0;
		int32 OnAnEnd = 0;
		int32 NotWalkable = 0;
		int32 Numbered = 0;
		for (const TArray<FIntPoint>& Boundary : Sections.Boundaries)
		{
			MostCells = FMath::Max(MostCells, Boundary.Num());
			for (const FIntPoint& Cell : Boundary)
			{
				OnAnEnd += (Cell == Asked.Entrance || Cell == Asked.Exit) ? 1 : 0;
				NotWalkable += Asked.IsFloor(Cell) ? 0 : 1;
				Numbered += (Sections.SectionOf(Asked, Cell) != INDEX_NONE) ? 1 : 0;
				AllClosed.Fill(Cell);
			}
		}
		Test.TestTrue(FString::Printf(TEXT("%s: no boundary closes more than twelve cells (%d)"), *Label, MostCells),
					  MostCells <= FCataclysmFloorGenerator::SectionBoundaryMostCells);
		Test.TestEqual(Label + TEXT(": no boundary cell is the entrance or the exit"), OnAnEnd, 0);
		Test.TestEqual(Label + TEXT(": every boundary cell was walkable in the plan asked about"), NotWalkable, 0);
		Test.TestEqual(Label + TEXT(": no boundary cell carries a section number"), Numbered, 0);

		// THE AREAS LEFT WITH EVERY BOUNDARY CLOSED.
		TArray<int32> Area;
		const TArray<int32> Sizes = AreasOf(AllClosed, Area);
		if (!Test.TestEqual(Label + TEXT(": closing every boundary leaves as many areas as there are sections"),
							Sizes.Num(), Count))
		{
			return SectionSizes;
		}
		int32 Smallest = MAX_int32;
		for (const int32 Size : Sizes)
		{
			Smallest = FMath::Min(Smallest, Size);
		}
		Test.TestTrue(FString::Printf(TEXT("%s: every area holds a tenth of the walkable cells or more (%d against %d)"),
									  *Label, Smallest, LeastArea), Smallest >= LeastArea);
		const int32 EntranceArea = Area[AllClosed.IndexOf(Asked.Entrance)];
		const int32 ExitArea = Area[AllClosed.IndexOf(Asked.Exit)];
		if (!Test.TestTrue(Label + TEXT(": the entrance and the exit are in different areas"),
						   EntranceArea != INDEX_NONE && ExitArea != INDEX_NONE && EntranceArea != ExitArea))
		{
			return SectionSizes;
		}

		// EVERY WALKABLE CELL'S NUMBER IS THE AREA IT IS REALLY IN: 0 the entrance's, the last the exit's.
		SectionSizes.Init(0, Count);
		int32 WrongNumber = 0;
		int32 RockNumbered = 0;
		for (int32 Index = 0; Index < AllClosed.Cells.Num(); ++Index)
		{
			if (Asked.Cells[Index] != ECataclysmFloorCell::Floor && Sections.Section[Index] != INDEX_NONE)
			{
				++RockNumbered;
			}
			if (AllClosed.Cells[Index] != ECataclysmFloorCell::Floor)
			{
				continue;
			}
			const int32 Real = (Area[Index] == EntranceArea) ? 0 : (Area[Index] == ExitArea) ? Count - 1 : 1;
			WrongNumber += (Sections.Section[Index] != Real) ? 1 : 0;
			++SectionSizes[Real];
		}
		Test.TestEqual(Label + TEXT(": every walkable cell's section number is the area it is in"), WrongNumber, 0);
		Test.TestEqual(Label + TEXT(": no cell of rock carries a section number"), RockNumbered, 0);

		// EACH BOUNDARY ALONE PARTS THE ENTRANCE FROM THE EXIT, and leaves on the entrance's side exactly the sections
		// before it: boundary 0 lies between sections 0 and 1, boundary 1 between sections 1 and 2.
		for (int32 Which = 0; Which < Sections.Boundaries.Num(); ++Which)
		{
			const FCataclysmFloorPlan OneClosed = WithClosed(CheckOn, Sections.Boundaries[Which]);
			const TArray<int32> FromEntrance = CataclysmFloorDistancesFrom(OneClosed, Asked.Entrance);
			Test.TestTrue(FString::Printf(TEXT("%s: boundary %d alone parts the entrance from the exit"), *Label, Which),
						  FromEntrance[OneClosed.IndexOf(Asked.Exit)] == INDEX_NONE);
			int32 OutOfPlace = 0;
			for (int32 Index = 0; Index < OneClosed.Cells.Num(); ++Index)
			{
				if (OneClosed.Cells[Index] != ECataclysmFloorCell::Floor || Sections.Section[Index] == INDEX_NONE)
				{
					continue;
				}
				const bool bReached = FromEntrance[Index] != INDEX_NONE;
				const bool bBeforeIt = Sections.Section[Index] <= Which;
				OutOfPlace += (bReached != bBeforeIt) ? 1 : 0;
			}
			Test.TestEqual(FString::Printf(TEXT("%s: boundary %d alone leaves sections 0 to %d on the entrance's side "
												"and no other"), *Label, Which, Which), OutOfPlace, 0);
		}
		return SectionSizes;
	}

	/**
	 * Three rooms five by five in a row, joined by two corridors three cells long and two wide. 87 walkable cells.
	 * Rooms at X 1 to 5, 9 to 13 and 17 to 21, Y 1 to 5; corridors at X 6 to 8 and 14 to 16, Y 3 to 4.
	 */
	FCataclysmFloorPlan ThreeRooms()
	{
		FCataclysmFloorPlan Plan = Rock(23, 7);
		CarveBlock(Plan, 1, 1, 5, 5);
		CarveBlock(Plan, 6, 3, 8, 4);
		CarveBlock(Plan, 9, 1, 13, 5);
		CarveBlock(Plan, 14, 3, 16, 4);
		CarveBlock(Plan, 17, 1, 21, 5);
		Plan.Entrance = FIntPoint(1, 3);
		Plan.Exit = FIntPoint(21, 3);
		return Plan;
	}

	/**
	 * Two rooms seven by seven with a room three by three between them, each joined to it by a corridor one cell long
	 * and two wide. 111 walkable cells, so a tenth is 12, and the middle room holds 9.
	 */
	FCataclysmFloorPlan TinyMiddleRoom()
	{
		FCataclysmFloorPlan Plan = Rock(21, 9);
		CarveBlock(Plan, 1, 1, 7, 7);
		CarveBlock(Plan, 8, 4, 8, 5);
		CarveBlock(Plan, 9, 3, 11, 5);
		CarveBlock(Plan, 12, 4, 12, 5);
		CarveBlock(Plan, 13, 1, 19, 7);
		Plan.Entrance = FIntPoint(1, 4);
		Plan.Exit = FIntPoint(19, 4);
		return Plan;
	}

	/**
	 * A room ten by ten, a corridor one cell long and two wide, and a last room `LastSide` cells square that holds
	 * the exit. With a last room of 3 the plan has 111 walkable cells and the room 9, under a tenth; with 4 it has
	 * 118 and the room 16, over a tenth.
	 */
	FCataclysmFloorPlan BigRoomAndLastRoom(int32 LastSide)
	{
		FCataclysmFloorPlan Plan = Rock(18, 12);
		CarveBlock(Plan, 1, 1, 10, 10);
		CarveBlock(Plan, 11, 5, 11, 6);
		CarveBlock(Plan, 12, 4, 12 + LastSide - 1, 4 + LastSide - 1);
		Plan.Entrance = FIntPoint(1, 5);
		Plan.Exit = FIntPoint(12 + LastSide - 1, 5);
		return Plan;
	}

	/**
	 * Two rooms seven by seven joined by TWO corridors, each two cells long and two wide: one at Y 2 to 3 and one at
	 * Y 5 to 6, both at X 8 to 9. 106 walkable cells.
	 */
	FCataclysmFloorPlan TwoCorridors()
	{
		FCataclysmFloorPlan Plan = Rock(18, 9);
		CarveBlock(Plan, 1, 1, 7, 7);
		CarveBlock(Plan, 8, 2, 9, 3);
		CarveBlock(Plan, 8, 5, 9, 6);
		CarveBlock(Plan, 10, 1, 16, 7);
		Plan.Entrance = FIntPoint(1, 4);
		Plan.Exit = FIntPoint(16, 4);
		return Plan;
	}

	/**
	 * Two rooms seven by seven, at X 1 to 7 and 9 to 15, joined by one corridor one cell long and two wide at X 8,
	 * Y 4 to 5. 100 walkable cells. The corridor's two cells are the only line the plan has.
	 */
	FCataclysmFloorPlan TwoRooms(FIntPoint Entrance, FIntPoint Exit)
	{
		FCataclysmFloorPlan Plan = Rock(17, 9);
		CarveBlock(Plan, 1, 1, 7, 7);
		CarveBlock(Plan, 8, 4, 8, 5);
		CarveBlock(Plan, 9, 1, 15, 7);
		Plan.Entrance = Entrance;
		Plan.Exit = Exit;
		return Plan;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorSectionsTwentyPlansTest,
	"Cataclysm.FloorSections.OnTwentyHallsPlansTheSearchFindsThreeSectionsOnAtLeastEighteenAndThreeOrTwoOnAll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorSectionsTwentyPlansTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorSectionsTest;

	// THE TWENTY HALLS PLANS THE MEASUREMENT OF 2026-10-08 USED: seeds 1 to 10, floors 1 and 10. That measurement
	// tried far more offers than the search does, so whether the search reaches 18 was not known when this was
	// written. The time is logged and not asserted.
	int32 Plans = 0;
	int32 WithThree = 0;
	int32 WithTwoOrThree = 0;
	for (const int32 FloorNumber : {1, 10})
	{
		for (int32 Seed = 1; Seed <= 10; ++Seed)
		{
			const FCataclysmFloorPlan Plan = PlanOf(Seed, FloorNumber, ECataclysmFloorLayout::Halls);
			if (!TestTrue(FString::Printf(TEXT("set-up, seed %d floor %d: the plan is built"), Seed, FloorNumber),
						  Plan.IsBuilt()))
			{
				continue;
			}
			++Plans;

			const double Began = FPlatformTime::Seconds();
			const FCataclysmFloorSections Sections = FCataclysmFloorGenerator::FindSections(Plan);
			const double Milliseconds = (FPlatformTime::Seconds() - Began) * 1000.0;

			const int32 Count = Sections.SectionCount();
			WithThree += (Count == 3) ? 1 : 0;
			WithTwoOrThree += (Count == 2 || Count == 3) ? 1 : 0;

			TArray<int32> SectionSizes;
			if (Count > 0)
			{
				SectionSizes = CheckEveryRule(*this, FString::Printf(TEXT("seed %d floor %d"), Seed, FloorNumber), Plan,
											  Plan, Sections);
			}
			TArray<int32> ClosedCells;
			for (const TArray<FIntPoint>& Boundary : Sections.Boundaries)
			{
				ClosedCells.Add(Boundary.Num());
			}
			UE_LOG(LogTemp, Display,
				TEXT("FLOORSECTIONS seed=%d floor=%d dungeonseed=%d walkable=%d sections=%d cells=%s closed=%s ms=%.3f"),
				Seed, FloorNumber, 1000 + Seed * 37, Plan.FloorCount(), Count, *Joined(SectionSizes),
				*Joined(ClosedCells), Milliseconds);
		}
	}

	TestEqual(TEXT("set-up: twenty plans were asked about"), Plans, 20);
	TestTrue(FString::Printf(TEXT("three sections on at least 18 of the 20 plans (%d)"), WithThree), WithThree >= 18);
	TestEqual(TEXT("three sections or two on all 20 plans"), WithTwoOrThree, 20);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorSectionsSamePlanTest,
	"Cataclysm.FloorSections.TheSamePlanAskedTwiceGivesTheSameBoundariesAndTheSameSectionNumbers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorSectionsSamePlanTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorSectionsTest;

	int32 WithSections = 0;
	int32 Differed = 0;
	TArray<TArray<TArray<FIntPoint>>> Answers;
	for (const int32 FloorNumber : {1, 10})
	{
		for (int32 Seed = 1; Seed <= 10; ++Seed)
		{
			const FCataclysmFloorPlan Plan = PlanOf(Seed, FloorNumber, ECataclysmFloorLayout::Halls);
			const FCataclysmFloorSections First = FCataclysmFloorGenerator::FindSections(Plan);
			const FCataclysmFloorSections Second = FCataclysmFloorGenerator::FindSections(Plan);
			WithSections += (First.SectionCount() > 0) ? 1 : 0;
			Differed += (First.Boundaries != Second.Boundaries || First.Section != Second.Section) ? 1 : 0;
			Answers.Add(First.Boundaries);
		}
	}
	TestTrue(FString::Printf(TEXT("set-up: some of the twenty plans have sections to compare (%d)"), WithSections),
			 WithSections > 0);
	TestEqual(TEXT("no plan gave a different answer the second time"), Differed, 0);

	// THE CONTROL. A search that gave every plan one answer would pass the line above.
	int32 UnlikeTheFirst = 0;
	for (const TArray<TArray<FIntPoint>>& Answer : Answers)
	{
		UnlikeTheFirst += (Answer != Answers[0]) ? 1 : 0;
	}
	TestTrue(FString::Printf(TEXT("and different plans give different boundaries (%d unlike the first)"), UnlikeTheFirst),
			 UnlikeTheFirst > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorSectionsHallsOnlyTest,
	"Cataclysm.FloorSections.APlanThatIsNotHallsHasNoSections",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorSectionsHallsOnlyTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorSectionsTest;

	int32 Asked = 0;
	int32 WithSections = 0;
	FCataclysmFloorPlan HallsWithSections;
	bool bHaveOne = false;
	for (const int32 FloorNumber : {1, 10})
	{
		for (int32 Seed = 1; Seed <= 10; ++Seed)
		{
			for (const ECataclysmFloorLayout Layout : {ECataclysmFloorLayout::Caverns, ECataclysmFloorLayout::Arena})
			{
				const FCataclysmFloorPlan Plan = PlanOf(Seed, FloorNumber, Layout);
				if (!Plan.IsBuilt())
				{
					continue;
				}
				++Asked;
				const FCataclysmFloorSections Sections = FCataclysmFloorGenerator::FindSections(Plan);
				WithSections += (Sections.SectionCount() != 0 || !Sections.Boundaries.IsEmpty()
					|| !Sections.Section.IsEmpty()) ? 1 : 0;
			}
			if (!bHaveOne)
			{
				HallsWithSections = PlanOf(Seed, FloorNumber, ECataclysmFloorLayout::Halls);
				bHaveOne = FCataclysmFloorGenerator::FindSections(HallsWithSections).SectionCount() > 0;
			}
		}
	}
	TestEqual(TEXT("set-up: twenty Caverns plans and twenty Arena plans were built and asked about"), Asked, 40);
	TestEqual(TEXT("none of them has a section, a boundary or a section number"), WithSections, 0);

	// THE CONTROL: THE SAME CELLS, CALLED HALLS AND THEN CALLED CAVERNS. Only the layout differs between the two calls.
	if (TestTrue(TEXT("set-up: a Halls plan with sections was found"), bHaveOne))
	{
		TestTrue(TEXT("the Halls plan has sections"),
				 FCataclysmFloorGenerator::FindSections(HallsWithSections).SectionCount() >= 2);
		FCataclysmFloorPlan Relabelled = HallsWithSections;
		Relabelled.Layout = ECataclysmFloorLayout::Caverns;
		TestEqual(TEXT("the same cells called Caverns have none"),
				  FCataclysmFloorGenerator::FindSections(Relabelled).SectionCount(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorSectionsThreeRoomsTest,
	"Cataclysm.FloorSections.ThreeRoomsInARowGiveThreeSectionsWithTheBoundariesInTheCorridors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorSectionsThreeRoomsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorSectionsTest;

	// ROOMS AT X 1-5, 9-13 AND 17-21; CORRIDORS AT X 6-8 AND 14-16, Y 3-4. The entrance is at (1, 3) in the first
	// room and the exit at (21, 3) in the last.
	const FCataclysmFloorPlan Plan = ThreeRooms();
	TestEqual(TEXT("set-up: the plan has 87 walkable cells"), Plan.FloorCount(), 87);
	const FCataclysmFloorSections Sections = FCataclysmFloorGenerator::FindSections(Plan);
	if (!TestEqual(TEXT("three sections"), Sections.SectionCount(), 3))
	{
		return true;
	}
	const TArray<int32> SectionSizes = CheckEveryRule(*this, TEXT("three rooms"), Plan, Plan, Sections);
	TestEqual(TEXT("boundary 0 closes two cells"), Sections.Boundaries[0].Num(), 2);
	TestTrue(TEXT("both in the first corridor"), AllWithin(Sections.Boundaries[0], 6, 3, 8, 4));
	TestEqual(TEXT("boundary 1 closes two cells"), Sections.Boundaries[1].Num(), 2);
	TestTrue(TEXT("both in the second corridor"), AllWithin(Sections.Boundaries[1], 14, 3, 16, 4));
	if (TestEqual(TEXT("a size for each section"), SectionSizes.Num(), 3))
	{
		// EACH SECTION HOLDS A WHOLE ROOM OF 25 CELLS, and whatever of a corridor is on its side of a boundary.
		TestTrue(FString::Printf(TEXT("every section holds a room's 25 cells or more (%s)"), *Joined(SectionSizes)),
				 SectionSizes[0] >= 25 && SectionSizes[1] >= 25 && SectionSizes[2] >= 25);
		TestEqual(TEXT("and the three hold every walkable cell but the four closed"),
				  SectionSizes[0] + SectionSizes[1] + SectionSizes[2], 87 - 4);
	}
	TestEqual(TEXT("the first room's far corner is in section 0"), Sections.SectionOf(Plan, FIntPoint(5, 5)), 0);
	TestEqual(TEXT("the middle room's middle is in section 1"), Sections.SectionOf(Plan, FIntPoint(11, 3)), 1);
	TestEqual(TEXT("the last room's near corner is in section 2"), Sections.SectionOf(Plan, FIntPoint(17, 1)), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorSectionsTenthTest,
	"Cataclysm.FloorSections.ARoomUnderATenthOfTheCellsIsNeverASectionOfItsOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorSectionsTenthTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorSectionsTest;

	// A ROOM OF 9 CELLS BETWEEN TWO OF 49. A tenth of 111 cells, rounded up, is 12. The corridors either side of the
	// small room are where three sections would be divided, and the middle one would hold 9.
	const FCataclysmFloorPlan Middle = TinyMiddleRoom();
	TestEqual(TEXT("set-up: the plan has 111 walkable cells, so a tenth is 12 and the middle room's 9 is under it"),
			  Middle.FloorCount(), 111);
	const FCataclysmFloorSections MiddleSections = FCataclysmFloorGenerator::FindSections(Middle);
	TestTrue(FString::Printf(TEXT("a tiny middle room: two sections or none, never three (%d)"),
							 MiddleSections.SectionCount()),
			 MiddleSections.SectionCount() == 2 || MiddleSections.SectionCount() == 0);
	if (MiddleSections.SectionCount() > 0)
	{
		CheckEveryRule(*this, TEXT("a tiny middle room"), Middle, Middle, MiddleSections);
	}

	// A LAST ROOM OF 9 CELLS HOLDING THE EXIT, off a room of 100: the only place to divide leaves 9 on the exit's
	// side, so there are no sections.
	const FCataclysmFloorPlan Last = BigRoomAndLastRoom(3);
	TestEqual(TEXT("set-up: this plan has 111 walkable cells too"), Last.FloorCount(), 111);
	TestEqual(TEXT("a tiny last room: no sections"), FCataclysmFloorGenerator::FindSections(Last).SectionCount(), 0);

	// THE CONTROL: THE SAME PLAN WITH A LAST ROOM OF 16 CELLS, over a tenth of its 118, is divided at that corridor.
	const FCataclysmFloorPlan Larger = BigRoomAndLastRoom(4);
	const FCataclysmFloorSections LargerSections = FCataclysmFloorGenerator::FindSections(Larger);
	if (TestEqual(TEXT("a last room of 16 cells: two sections"), LargerSections.SectionCount(), 2))
	{
		const TArray<int32> SectionSizes = CheckEveryRule(*this, TEXT("a last room of 16"), Larger, Larger, LargerSections);
		TestTrue(TEXT("the boundary is the corridor's two cells"),
				 LargerSections.Boundaries[0].Num() == 2 && AllWithin(LargerSections.Boundaries[0], 11, 5, 11, 6));
		if (TestEqual(TEXT("a size for each section"), SectionSizes.Num(), 2))
		{
			TestEqual(TEXT("the exit's section is the last room's 16 cells"), SectionSizes[1], 16);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorSectionsTwoCorridorsTest,
	"Cataclysm.FloorSections.TwoRoomsJoinedByTwoCorridorsGiveOneBoundaryOfTwoLines",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorSectionsTwoCorridorsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorSectionsTest;

	// ROOMS AT X 1-7 AND 10-16. CORRIDORS AT X 8-9: ONE AT Y 2-3, ONE AT Y 5-6. No single line parts the rooms.
	const FCataclysmFloorPlan Plan = TwoCorridors();
	const FCataclysmFloorSections Sections = FCataclysmFloorGenerator::FindSections(Plan);
	if (!TestEqual(TEXT("two sections"), Sections.SectionCount(), 2))
	{
		return true;
	}
	CheckEveryRule(*this, TEXT("two corridors"), Plan, Plan, Sections);

	TArray<FIntPoint> InTheUpper;
	TArray<FIntPoint> InTheLower;
	for (const FIntPoint& Cell : Sections.Boundaries[0])
	{
		(Cell.Y <= 3 ? InTheUpper : InTheLower).Add(Cell);
	}
	TestEqual(TEXT("the boundary closes four cells"), Sections.Boundaries[0].Num(), 4);
	TestTrue(TEXT("two of them across the upper corridor"),
			 InTheUpper.Num() == 2 && AllWithin(InTheUpper, 8, 2, 9, 3) && InTheUpper[0].X == InTheUpper[1].X);
	TestTrue(TEXT("and two across the lower corridor"),
			 InTheLower.Num() == 2 && AllWithin(InTheLower, 8, 5, 9, 6) && InTheLower[0].X == InTheLower[1].X);

	// THE CONTROL: ONE OF THE TWO LINES ALONE PARTS NOTHING, so the boundary needed both.
	if (InTheUpper.Num() == 2 && InTheLower.Num() == 2)
	{
		TestTrue(TEXT("with only the upper line closed the exit is still reached"),
				 CataclysmFloorDistancesFrom(WithClosed(Plan, InTheUpper), Plan.Entrance)[Plan.IndexOf(Plan.Exit)]
					 != INDEX_NONE);
		TestTrue(TEXT("with only the lower line closed the exit is still reached"),
				 CataclysmFloorDistancesFrom(WithClosed(Plan, InTheLower), Plan.Entrance)[Plan.IndexOf(Plan.Exit)]
					 != INDEX_NONE);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorSectionsOneRoomTest,
	"Cataclysm.FloorSections.ASingleRoomHasNoSections",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorSectionsOneRoomTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorSectionsTest;

	// A ROOM SIX CELLS BY FOUR, AT X 1-6 AND Y 1-4. Its top row and its bottom row are lines, six cells with rock at
	// each end, and closing either or both leaves one area. The entrance is at (1, 2) and the exit at (6, 3).
	FCataclysmFloorPlan Room = Rock(8, 6);
	CarveBlock(Room, 1, 1, 6, 4);
	Room.Entrance = FIntPoint(1, 2);
	Room.Exit = FIntPoint(6, 3);
	const FCataclysmFloorSections Sections = FCataclysmFloorGenerator::FindSections(Room);
	TestEqual(TEXT("one room: no sections"), Sections.SectionCount(), 0);
	TestTrue(TEXT("no boundary and no section number"), Sections.Boundaries.IsEmpty() && Sections.Section.IsEmpty());

	// THE CONTROL: TWO ROOMS AND A CORRIDOR ARE DIVIDED, so "none" above is the room and not a search that finds nothing.
	TestEqual(TEXT("two rooms joined by a corridor: two sections"),
			  FCataclysmFloorGenerator::FindSections(TwoRooms(FIntPoint(1, 4), FIntPoint(15, 4))).SectionCount(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorSectionsExitOnTheLineTest,
	"Cataclysm.FloorSections.ALineThatHoldsTheExitIsNeverClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorSectionsExitOnTheLineTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorSectionsTest;

	// TWO ROOMS, AND THE ONLY LINE BETWEEN THEM IS THE CORRIDOR'S TWO CELLS AT X 8. With the exit on one of them, at
	// (8, 4), that line may not be closed and nothing else divides the plan.
	const FCataclysmFloorPlan OnTheLine = TwoRooms(FIntPoint(1, 4), FIntPoint(8, 4));
	const FCataclysmFloorSections None = FCataclysmFloorGenerator::FindSections(OnTheLine);
	TestEqual(TEXT("the exit on the only line: no sections"), None.SectionCount(), 0);

	// THE CONTROL: THE SAME CELLS WITH THE EXIT IN THE FAR ROOM, at (15, 4), are divided at that very line.
	const FCataclysmFloorPlan InTheRoom = TwoRooms(FIntPoint(1, 4), FIntPoint(15, 4));
	const FCataclysmFloorSections Two = FCataclysmFloorGenerator::FindSections(InTheRoom);
	if (TestEqual(TEXT("the exit in the far room: two sections"), Two.SectionCount(), 2))
	{
		CheckEveryRule(*this, TEXT("the exit in the far room"), InTheRoom, InTheRoom, Two);
		TestTrue(TEXT("the boundary is the corridor's two cells"),
				 Two.Boundaries[0].Num() == 2 && AllWithin(Two.Boundaries[0], 8, 4, 8, 5));
	}

	// AND THE SAME FOR THE ENTRANCE, on the corridor's other cell at (8, 5).
	TestEqual(TEXT("the entrance on the only line: no sections"),
			  FCataclysmFloorGenerator::FindSections(TwoRooms(FIntPoint(8, 5), FIntPoint(15, 4))).SectionCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorSectionsBothEndsOneSideTest,
	"Cataclysm.FloorSections.ALineThatDoesNotPartTheEntranceFromTheExitIsNoBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorSectionsBothEndsOneSideTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorSectionsTest;

	// TWO ROOMS OF 49 CELLS AND ONE LINE BETWEEN THEM, WITH THE ENTRANCE AT (1, 1) AND THE EXIT AT (7, 7), BOTH IN THE
	// FIRST ROOM. Closing the line leaves two areas, each far over a tenth, and does not part the two ends.
	const FCataclysmFloorPlan SameRoom = TwoRooms(FIntPoint(1, 1), FIntPoint(7, 7));
	TestTrue(TEXT("set-up: closing the corridor leaves the exit reached from the entrance"),
			 CataclysmFloorDistancesFrom(WithClosed(SameRoom, {FIntPoint(8, 4), FIntPoint(8, 5)}), SameRoom.Entrance)
				 [SameRoom.IndexOf(SameRoom.Exit)] != INDEX_NONE);
	TestEqual(TEXT("both ends in one room: no sections"),
			  FCataclysmFloorGenerator::FindSections(SameRoom).SectionCount(), 0);

	// THE CONTROL: THE SAME CELLS WITH THE EXIT IN THE OTHER ROOM, at (15, 7), are divided.
	TestEqual(TEXT("an end in each room: two sections"),
			  FCataclysmFloorGenerator::FindSections(TwoRooms(FIntPoint(1, 1), FIntPoint(15, 7))).SectionCount(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorSectionsShortcutTest,
	"Cataclysm.FloorSections.APlanWithACarvedShortcutIsDividedByTheRulesWithItsGateOpenAndWithItClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorSectionsShortcutTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorSectionsTest;

	// THE FIRST OF THE TEN HALLS PLANS OF FLOOR 1 THAT HAS A SHORTCUT, found as the shortcut tests above find theirs.
	FCataclysmFloorPlan Plan;
	FCataclysmFloorShortcut Shortcut;
	int32 FoundOnSeed = 0;
	for (int32 Seed = 1; Seed <= 10 && FoundOnSeed == 0; ++Seed)
	{
		Plan = PlanOf(Seed, 1, ECataclysmFloorLayout::Halls);
		FRandomStream Stream(Seed);
		const TArray<FCataclysmFloorShortcut> Found =
			FCataclysmFloorGenerator::FindShortcuts(Plan, Stream, 1, TSet<FIntPoint>());
		if (!Found.IsEmpty())
		{
			Shortcut = Found[0];
			FoundOnSeed = Seed;
		}
	}
	if (!TestTrue(TEXT("set-up: one of the ten plans has a shortcut"), FoundOnSeed != 0 && Shortcut.IsValid()))
	{
		return true;
	}

	const int32 WalkableBefore = Plan.FloorCount();
	FCataclysmFloorGenerator::CarveShortcut(Plan, Shortcut);
	TestTrue(TEXT("set-up: carving it made new cells walkable"), Plan.FloorCount() > WalkableBefore);
	TestTrue(TEXT("set-up: its two gate cells are walkable in the plan asked about"),
			 Plan.IsFloor(Shortcut.Gate[0]) && Plan.IsFloor(Shortcut.Gate[1]));

	const FCataclysmFloorSections Sections = FCataclysmFloorGenerator::FindSections(Plan);
	if (!TestTrue(FString::Printf(TEXT("seed %d with its shortcut carved has sections (%d)"), FoundOnSeed,
								  Sections.SectionCount()), Sections.SectionCount() >= 2))
	{
		return true;
	}

	// THE SEARCH READ THE PLAN WITH THE GATE OPEN. A floor begins with the gate shut, so the same division is checked
	// on a copy with the gate's two cells rock as well.
	CheckEveryRule(*this, TEXT("the gate open"), Plan, Plan, Sections);
	const FCataclysmFloorPlan GateClosed = WithClosed(Plan, Shortcut.Gate);
	TestEqual(TEXT("set-up: the copy with the gate closed has two walkable cells fewer"), GateClosed.FloorCount(),
			  Plan.FloorCount() - 2);
	CheckEveryRule(*this, TEXT("the gate closed"), Plan, GateClosed, Sections);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorSectionsTreatedAsOpenTest,
	"Cataclysm.FloorSections.AnObstacleIsAskedAboutWithAClosedBarriersCellsTreatedAsOpen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorSectionsTreatedAsOpenTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmFloorSectionsTest;

	// TWO ROOMS THREE BY THREE, AT X 1-3 AND X 5-7, Y 1-3, JOINED BY ONE CELL AT (4, 2), WITH A DEAD END TWO CELLS LONG
	// BELOW THE FIRST ROOM AT (2, 4) AND (2, 5). The entrance is at (1, 1), the exit at (7, 3), and the player stands
	// at (1, 2). `Open` is that plan. `Barred` is the same with the joining cell rock, which is a barrier standing.
	FCataclysmFloorPlan Open = Rock(9, 7);
	CarveBlock(Open, 1, 1, 3, 3);
	CarveBlock(Open, 4, 2, 4, 2);
	CarveBlock(Open, 5, 1, 7, 3);
	CarveBlock(Open, 2, 4, 2, 5);
	Open.Entrance = FIntPoint(1, 1);
	Open.Exit = FIntPoint(7, 3);
	const TArray<FIntPoint> Barrier = {FIntPoint(4, 2)};
	const FCataclysmFloorPlan Barred = WithClosed(Open, Barrier);
	const FIntPoint From(1, 2);
	const TSet<FIntPoint> Nothing;
	const TArray<FIntPoint> NoCells;

	TestTrue(TEXT("set-up: with the barrier standing the far room is out of reach of the player"),
			 CataclysmFloorDistancesFrom(Barred, From)[Barred.IndexOf(Barred.Exit)] == INDEX_NONE);

	// THE CONTROL FIRST: WITHOUT THE ARGUMENT EVERY OBSTACLE IS REFUSED, because the far room is already cut off.
	TestFalse(TEXT("without the argument a corner of the near room is refused"),
			  CataclysmFloorCanBlock(Barred, {FIntPoint(3, 3)}, From, Nothing));
	TestTrue(TEXT("with the barrier's cell treated as open the same corner is allowed"),
			 CataclysmFloorCanBlock(Barred, {FIntPoint(3, 3)}, From, Nothing, Barrier));
	TestTrue(TEXT("and so is the dead end's last cell, which strands nothing"),
			 CataclysmFloorCanBlock(Barred, {FIntPoint(2, 5)}, From, Nothing, Barrier));

	// WHAT WOULD STRAND A CELL EVEN WITH THE BARRIER OPEN IS STILL REFUSED.
	TestFalse(TEXT("the dead end's first cell is refused: it strands the last"),
			  CataclysmFloorCanBlock(Barred, {FIntPoint(2, 4)}, From, Nothing, Barrier));
	TestFalse(TEXT("the far room's doorway is refused: it strands the far room"),
			  CataclysmFloorCanBlock(Barred, {FIntPoint(5, 2)}, From, Nothing, Barrier));
	TestFalse(TEXT("the barrier's own cell is refused: it is rock in the plan"),
			  CataclysmFloorCanBlock(Barred, Barrier, From, Nothing, Barrier));

	// WITH NOTHING PASSED THE ANSWERS ARE WHAT THEY WERE, on the plan with no barrier.
	TestTrue(TEXT("no barrier, no argument: the corner is allowed"),
			 CataclysmFloorCanBlock(Open, {FIntPoint(3, 3)}, From, Nothing));
	TestTrue(TEXT("no barrier, an empty list: the corner is allowed"),
			 CataclysmFloorCanBlock(Open, {FIntPoint(3, 3)}, From, Nothing, NoCells));
	TestFalse(TEXT("no barrier, no argument: the joining cell is refused"),
			  CataclysmFloorCanBlock(Open, Barrier, From, Nothing));
	TestFalse(TEXT("no barrier, an empty list: the joining cell is refused"),
			  CataclysmFloorCanBlock(Open, Barrier, From, Nothing, NoCells));
	TestFalse(TEXT("no barrier, an empty list: the dead end's first cell is refused"),
			  CataclysmFloorCanBlock(Open, {FIntPoint(2, 4)}, From, Nothing, NoCells));
	TestTrue(TEXT("a cell that is already walkable, treated as open, changes nothing"),
			 CataclysmFloorCanBlock(Open, {FIntPoint(3, 3)}, From, Nothing, Barrier));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
