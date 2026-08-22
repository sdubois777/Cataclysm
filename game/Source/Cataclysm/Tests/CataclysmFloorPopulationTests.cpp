// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmCorruptedSentinelCharacter.h"
#include "Character/CataclysmSuccubusCharacter.h"
#include "Dungeon/CataclysmFloorGenerator.h"
#include "Dungeon/CataclysmFloorPlan.h"
#include "Dungeon/CataclysmFloorPopulation.h"

/**
 * Tests for deciding which creature stands on which cell of a dungeon floor,
 * issue #40.
 *
 * WHAT THEY REACH, AND WHY IT IS THIS AND NOT SPAWNED CHARACTERS. The populator
 * produces a list of cells and creature names and spawns nothing, for the same
 * stated reason `FCataclysmFloorPlan` produces a grid and builds no geometry:
 * the automation tests run with `-nullrhi`. A list can be swept over a thousand
 * seeds of three layout families in well under a second. Sixty spawned
 * characters with ability systems cannot be, and could not be asserted on if
 * they were. `CataclysmDungeonGameModeTests.cpp` covers the spawning half.
 *
 * THE LIMITS BELOW WERE MEASURED, NOT CHOSEN.
 * `Cataclysm.DungeonEnemies.MeasureWhatAFloorIsPopulatedWith` logs what a
 * thousand seeds of each layout actually produce, and every limit here sits
 * outside the worst value that sweep found. Re-run it after changing a constant
 * in `FCataclysmFloorPopulator` rather than moving a limit to suit.
 */

namespace CataclysmPopulationTest
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

	/**
	 * How many seeds each assertion sweeps over, and how many the measurement
	 * does.
	 *
	 * THE MEASUREMENT IS WIDER ON PURPOSE, and the floor generator's own tests
	 * record why in more detail: a cavern setting that collapsed some floors to
	 * 8% walkable was found by the thousand-seed sweep and not by the
	 * hundred-and-twenty-seed one. A limit set from too few seeds is a limit that
	 * fails on the 121st.
	 */
	constexpr int32 SweepSeeds = 120;
	constexpr int32 MeasureSeeds = 1000;

	FCataclysmFloorPlan Floor(int32 DungeonSeed, int32 FloorNumber,
							  ECataclysmFloorLayout Layout)
	{
		FCataclysmFloorRequest Request;
		Request.DungeonSeed = DungeonSeed;
		Request.FloorNumber = FloorNumber;
		Request.Layout = Layout;
		return FCataclysmFloorGenerator::Generate(Request);
	}

	/** Whether two populations are the same creatures in the same places. */
	bool Same(const FCataclysmFloorPopulation& A, const FCataclysmFloorPopulation& B)
	{
		if (A.Enemies.Num() != B.Enemies.Num() || A.PackCount != B.PackCount)
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Enemies.Num(); ++Index)
		{
			if (A.Enemies[Index].Cell != B.Enemies[Index].Cell
				|| A.Enemies[Index].Creature != B.Enemies[Index].Creature
				|| A.Enemies[Index].Pack != B.Enemies[Index].Pack)
			{
				return false;
			}
		}
		return true;
	}

	/** How far apart two cells are in a straight line, in cells. */
	float StraightLineCells(FIntPoint A, FIntPoint B)
	{
		return FVector2D(static_cast<float>(A.X - B.X),
						 static_cast<float>(A.Y - B.Y)).Size();
	}
}

// ---------------------------------------------------------------------------
// The measurement the limits below are set from
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPopulationMeasureTest,
	"Cataclysm.DungeonEnemies.MeasureWhatAFloorIsPopulatedWith",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPopulationMeasureTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmPopulationTest;

	// NOT AN ASSERTION TEST. It reports what the populator does so the limits in
	// the tests below can be set from measured numbers rather than guessed ones.
	// It fails only if a floor comes out with nothing on it at all, which is the
	// one outcome no reading of the design allows.
	for (const ECataclysmFloorLayout Layout : EveryLayout())
	{
		int32 FewestCreatures = MAX_int32;
		int32 MostCreatures = 0;
		int32 FewestPacks = MAX_int32;
		int32 MostPacks = 0;
		int32 EmptyFloors = 0;
		float WorstFilled = 1.0f;
		int32 FewestKinds = MAX_int32;

		// How close two creatures from DIFFERENT groups ever come. The spacing
		// rule is about the middles of groups, so this is the honest number for
		// how separate two groups actually are.
		float ClosestAcrossPacks = MAX_flt;

		// How close a creature ever comes to where the player arrives.
		int32 NearestToEntrance = MAX_int32;

		for (int32 Seed = 1; Seed <= MeasureSeeds; ++Seed)
		{
			const FCataclysmFloorPlan Plan = Floor(Seed, 1, Layout);
			if (!Plan.IsBuilt())
			{
				continue;
			}

			const FCataclysmFloorPopulation Population =
				FCataclysmFloorPopulator::Populate(Plan);

			if (Population.Enemies.Num() == 0)
			{
				++EmptyFloors;
				continue;
			}

			FewestCreatures = FMath::Min(FewestCreatures, Population.Enemies.Num());
			MostCreatures = FMath::Max(MostCreatures, Population.Enemies.Num());
			FewestPacks = FMath::Min(FewestPacks, Population.PackCount);
			MostPacks = FMath::Max(MostPacks, Population.PackCount);
			FewestKinds = FMath::Min(FewestKinds, Population.KindsPresent());

			if (Population.Wanted > 0)
			{
				WorstFilled = FMath::Min(WorstFilled,
					static_cast<float>(Population.Enemies.Num())
						/ static_cast<float>(Population.Wanted));
			}

			const TArray<int32> FromEntrance =
				CataclysmFloorDistancesFrom(Plan, Plan.Entrance);
			for (const FCataclysmEnemyPlacement& Enemy : Population.Enemies)
			{
				const int32 Index = Plan.IndexOf(Enemy.Cell);
				if (FromEntrance.IsValidIndex(Index) && FromEntrance[Index] != INDEX_NONE)
				{
					NearestToEntrance = FMath::Min(NearestToEntrance, FromEntrance[Index]);
				}
			}

			for (int32 A = 0; A < Population.Enemies.Num(); ++A)
			{
				for (int32 B = A + 1; B < Population.Enemies.Num(); ++B)
				{
					if (Population.Enemies[A].Pack == Population.Enemies[B].Pack)
					{
						continue;
					}
					ClosestAcrossPacks = FMath::Min(ClosestAcrossPacks,
						StraightLineCells(Population.Enemies[A].Cell,
										  Population.Enemies[B].Cell));
				}
			}
		}

		UE_LOG(LogTemp, Display,
			TEXT("CataclysmPopulationMeasure %s: creatures %d..%d groups %d..%d ")
			TEXT("kinds>=%d filled>=%.3f nearest-to-entrance>=%d ")
			TEXT("closest-across-groups>=%.2f cells empty=%d"),
			CataclysmFloorLayoutName(Layout), FewestCreatures, MostCreatures,
			FewestPacks, MostPacks, FewestKinds, WorstFilled, NearestToEntrance,
			ClosestAcrossPacks, EmptyFloors);

		TestEqual(FString::Printf(
			TEXT("%s puts creatures on every floor it builds"),
			CataclysmFloorLayoutName(Layout)), EmptyFloors, 0);
	}

	return true;
}

// ---------------------------------------------------------------------------
// Where creatures may stand
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPopulationOnTheFloorTest,
	"Cataclysm.DungeonEnemies.EveryCreatureStandsOnGroundThePlayerCanReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPopulationOnTheFloorTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmPopulationTest;

	// TWO FAULTS AT ONCE, and both are silent. A creature on a solid cell is
	// standing inside rock, which nothing at run time reports. A creature on a
	// walkable cell the player cannot walk to is a creature that never enters the
	// game, and a floor could be half full of them while looking populated in
	// every log line.
	int32 InRock = 0;
	int32 Unreachable = 0;
	int32 Checked = 0;

	for (const ECataclysmFloorLayout Layout : EveryLayout())
	{
		for (int32 Seed = 1; Seed <= SweepSeeds; ++Seed)
		{
			const FCataclysmFloorPlan Plan = Floor(Seed, 1, Layout);
			if (!Plan.IsBuilt())
			{
				continue;
			}

			const FCataclysmFloorPopulation Population =
				FCataclysmFloorPopulator::Populate(Plan);
			const TArray<int32> FromEntrance =
				CataclysmFloorDistancesFrom(Plan, Plan.Entrance);

			for (const FCataclysmEnemyPlacement& Enemy : Population.Enemies)
			{
				++Checked;

				if (!Plan.IsFloor(Enemy.Cell))
				{
					++InRock;
					continue;
				}

				const int32 Index = Plan.IndexOf(Enemy.Cell);
				if (!FromEntrance.IsValidIndex(Index)
					|| FromEntrance[Index] == INDEX_NONE)
				{
					++Unreachable;
				}
			}
		}
	}

	TestTrue(TEXT("the sweep actually placed creatures to check"), Checked > 0);
	TestEqual(FString::Printf(
		TEXT("no creature stands in solid rock, out of %d placed"), Checked),
		InRock, 0);
	TestEqual(TEXT("and every one can be walked to from where the player arrives"),
		Unreachable, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPopulationEntranceTest,
	"Cataclysm.DungeonEnemies.NothingIsWaitingWhereThePlayerArrives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPopulationEntranceTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmPopulationTest;

	// A PLAYER WHO ARRIVES ALREADY BEING WALKED AT HAS NOT ARRIVED. The furthest
	// any of these creatures notices a target is 14 metres, which is three and a
	// half cells, so the keep-out is set at twice that. It is measured by walking
	// rather than by straight line: two cells either side of a wall are two
	// metres apart and a long way to walk.
	int32 TooClose = 0;
	int32 Nearest = MAX_int32;
	int32 Checked = 0;

	for (const ECataclysmFloorLayout Layout : EveryLayout())
	{
		for (int32 Seed = 1; Seed <= SweepSeeds; ++Seed)
		{
			const FCataclysmFloorPlan Plan = Floor(Seed, 1, Layout);
			if (!Plan.IsBuilt())
			{
				continue;
			}

			const FCataclysmFloorPopulation Population =
				FCataclysmFloorPopulator::Populate(Plan);
			const TArray<int32> FromEntrance =
				CataclysmFloorDistancesFrom(Plan, Plan.Entrance);

			for (const FCataclysmEnemyPlacement& Enemy : Population.Enemies)
			{
				++Checked;

				const int32 Index = Plan.IndexOf(Enemy.Cell);
				const int32 Away = FromEntrance.IsValidIndex(Index)
					? FromEntrance[Index] : INDEX_NONE;

				if (Away == INDEX_NONE
					|| Away < FCataclysmFloorPopulator::LeastCellsFromEntrance)
				{
					++TooClose;
				}
				if (Away != INDEX_NONE)
				{
					Nearest = FMath::Min(Nearest, Away);
				}
			}
		}
	}

	TestTrue(TEXT("the sweep actually placed creatures to check"), Checked > 0);
	TestEqual(FString::Printf(
		TEXT("no creature stands within %d cells of the entrance; the nearest "
			 "of %d placed was %d cells away"),
		FCataclysmFloorPopulator::LeastCellsFromEntrance, Checked, Nearest),
		TooClose, 0);

	// AND THE KEEP-OUT IS LARGE ENOUGH TO BE WORTH HAVING. The check above
	// compares the placement against the same constant the placement used, so it
	// says the code keeps its own promise and says nothing about whether the
	// promise is any good: setting the constant to zero would satisfy it. This
	// asks the separate question, against the creatures rather than against
	// itself.
	//
	// 1,400 CM IS THE CORRUPTED SENTINEL'S, which is the furthest any creature
	// placed on a floor notices a target from. The constant is set to twice it;
	// this fails if it ever drops below once.
	const float KeepOutCm = FCataclysmFloorPopulator::LeastCellsFromEntrance
		* FCataclysmFloorGenerator::CellSizeCm;

	TestTrue(FString::Printf(
		TEXT("the keep-out of %.0f cm is further than the %.0f cm a Corrupted "
			 "Sentinel notices a target from, so a player who has just arrived "
			 "is not already being shot at"),
		KeepOutCm, ACataclysmCorruptedSentinelCharacter::SentinelNoticeRadiusCm),
		KeepOutCm >= ACataclysmCorruptedSentinelCharacter::SentinelNoticeRadiusCm);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPopulationOneToACellTest,
	"Cataclysm.DungeonEnemies.NoTwoCreaturesStandOnTheSameCell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPopulationOneToACellTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmPopulationTest;

	// TWO CREATURES ON ONE CELL ARE TWO CREATURES INSIDE EACH OTHER. Spawning
	// uses `AlwaysSpawn`, so the engine would not separate them and would not
	// complain; they would push apart on the first frame of movement, which looks
	// like a bug and hides how many there really are.
	int32 Shared = 0;
	int32 Checked = 0;

	for (const ECataclysmFloorLayout Layout : EveryLayout())
	{
		for (int32 Seed = 1; Seed <= SweepSeeds; ++Seed)
		{
			const FCataclysmFloorPlan Plan = Floor(Seed, 1, Layout);
			if (!Plan.IsBuilt())
			{
				continue;
			}

			const FCataclysmFloorPopulation Population =
				FCataclysmFloorPopulator::Populate(Plan);

			TSet<FIntPoint> Seen;
			for (const FCataclysmEnemyPlacement& Enemy : Population.Enemies)
			{
				++Checked;
				bool bAlreadyThere = false;
				Seen.Add(Enemy.Cell, &bAlreadyThere);
				if (bAlreadyThere)
				{
					++Shared;
				}
			}
		}
	}

	TestTrue(TEXT("the sweep actually placed creatures to check"), Checked > 0);
	TestEqual(FString::Printf(TEXT("no cell holds two creatures, out of %d placed"),
			  Checked), Shared, 0);

	return true;
}

// ---------------------------------------------------------------------------
// Groups
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPopulationGroupsTest,
	"Cataclysm.DungeonEnemies.CreaturesStandInGroupsSpreadOverTheFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPopulationGroupsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmPopulationTest;

	// THE FAULT THIS RULES OUT IS A FLOOR THAT LOOKS RIGHT IN EVERY OTHER TEST.
	// Sixty creatures each standing alone satisfies "on the floor", "not at the
	// entrance" and "one to a cell", and it is not what the design describes:
	// `docs/Cataclysm_GDD_v2.md` says of the Imp that "a single Common enemy is
	// not the threat. A pack is", and ten scattered Imps are ten harmless
	// creatures.
	//
	// AND THE OPPOSITE FAULT, which is one heap in a corner. The spacing rule is
	// about the middles of groups, so that is what is checked here.
	int32 ScatteredFloors = 0;
	int32 GroupsTooClose = 0;
	int32 MembersTooFarFromTheirOwnMiddle = 0;
	int32 Floors = 0;

	for (const ECataclysmFloorLayout Layout : EveryLayout())
	{
		for (int32 Seed = 1; Seed <= SweepSeeds; ++Seed)
		{
			const FCataclysmFloorPlan Plan = Floor(Seed, 1, Layout);
			if (!Plan.IsBuilt())
			{
				continue;
			}

			const FCataclysmFloorPopulation Population =
				FCataclysmFloorPopulator::Populate(Plan);
			if (Population.Enemies.Num() == 0)
			{
				continue;
			}
			++Floors;

			// More creatures than groups, or nothing stands with anything.
			if (Population.Enemies.Num() <= Population.PackCount)
			{
				++ScatteredFloors;
			}

			// Every group's middle is a real walk from every other's.
			for (int32 A = 0; A < Population.PackSites.Num(); ++A)
			{
				const TArray<int32> FromA =
					CataclysmFloorDistancesFrom(Plan, Population.PackSites[A]);

				for (int32 B = A + 1; B < Population.PackSites.Num(); ++B)
				{
					const int32 Index = Plan.IndexOf(Population.PackSites[B]);
					const int32 Away = FromA.IsValidIndex(Index)
						? FromA[Index] : INDEX_NONE;

					if (Away != INDEX_NONE
						&& Away < FCataclysmFloorPopulator::LeastCellsBetweenPacks)
					{
						++GroupsTooClose;
					}
				}
			}

			// And no creature has wandered away from its own group's middle.
			for (const FCataclysmEnemyPlacement& Enemy : Population.Enemies)
			{
				if (!Population.PackSites.IsValidIndex(Enemy.Pack))
				{
					++MembersTooFarFromTheirOwnMiddle;
					continue;
				}

				// Straight-line distance is never longer than the walking
				// distance the placement used, so this cannot fail for a
				// creature that was placed correctly, and it costs no search.
				if (StraightLineCells(Enemy.Cell, Population.PackSites[Enemy.Pack])
					> static_cast<float>(FCataclysmFloorPopulator::MostCellsFromPackSite))
				{
					++MembersTooFarFromTheirOwnMiddle;
				}
			}
		}
	}

	TestTrue(TEXT("the sweep actually populated floors to check"), Floors > 0);
	TestEqual(FString::Printf(
		TEXT("every one of %d floors has creatures standing together rather "
			 "than one to a group"), Floors), ScatteredFloors, 0);
	TestEqual(FString::Printf(
		TEXT("no two groups' middles are within %d cells of each other"),
		FCataclysmFloorPopulator::LeastCellsBetweenPacks), GroupsTooClose, 0);
	TestEqual(FString::Printf(
		TEXT("no creature stands more than %d cells from its own group's middle"),
		FCataclysmFloorPopulator::MostCellsFromPackSite),
		MembersTooFarFromTheirOwnMiddle, 0);

	// AND THE SPACING IS LARGE ENOUGH TO BE WORTH HAVING. The check above
	// compares the placement against the same constant the placement used, so it
	// would be satisfied by a constant of zero. This asks the separate question:
	// two groups have to be further apart than these creatures can see, or
	// walking into one pulls the next and the floor is one crowd.
	//
	// WHAT THIS DOES NOT BUY, said plainly. The rule is about the middles of
	// groups. Two middles four cells apart can each spread three cells, so
	// members of neighbouring groups do stand side by side: the measurement above
	// records the closest two creatures from different groups ever came over
	// three thousand floors as one cell, which is four metres.
	const float SpacingCm = FCataclysmFloorPopulator::LeastCellsBetweenPacks
		* FCataclysmFloorGenerator::CellSizeCm;

	TestTrue(FString::Printf(
		TEXT("groups stand %.0f cm apart, which is further than the %.0f cm a "
			 "Corrupted Sentinel notices a target from"),
		SpacingCm, ACataclysmCorruptedSentinelCharacter::SentinelNoticeRadiusCm),
		SpacingCm >= ACataclysmCorruptedSentinelCharacter::SentinelNoticeRadiusCm);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPopulationSuccubusTest,
	"Cataclysm.DungeonEnemies.ASuccubusAlwaysHasSomethingToMakeStronger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPopulationSuccubusTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmPopulationTest;

	// A SUCCUBUS ON ITS OWN IS A 150-HEALTH CREATURE AND NOTHING ELSE. Its aura
	// Dominion reaches 8 metres and makes every other creature inside it
	// stronger; it is the only thing in the game that does. One standing alone in
	// a room buffs nothing, and killing it first -- which the design says is the
	// correct play -- would be pointless.
	//
	// TWO CELLS BECAUSE THE AURA IS 8 METRES AND A CELL IS 4. Straight-line
	// distance, because the aura is a sphere in the world and not a walk.
	//
	// WORKED OUT HERE FROM THE TWO CONSTANTS RATHER THAN ASKED OF THE POPULATOR.
	// The populator does the same arithmetic to decide where to put the creature;
	// a test that called into it would be checking that a number equals itself.
	const float AuraCells = ACataclysmSuccubusCharacter::DominionRadiusCm
		/ FCataclysmFloorGenerator::CellSizeCm;

	int32 Alone = 0;
	int32 Succubi = 0;

	for (const ECataclysmFloorLayout Layout : EveryLayout())
	{
		for (int32 Seed = 1; Seed <= SweepSeeds; ++Seed)
		{
			const FCataclysmFloorPlan Plan = Floor(Seed, 1, Layout);
			if (!Plan.IsBuilt())
			{
				continue;
			}

			const FCataclysmFloorPopulation Population =
				FCataclysmFloorPopulator::Populate(Plan);

			for (const FCataclysmEnemyPlacement& Enemy : Population.Enemies)
			{
				if (Enemy.Creature != ECataclysmDungeonCreature::Succubus)
				{
					continue;
				}
				++Succubi;

				bool bHasCompany = false;
				for (const FCataclysmEnemyPlacement& Other : Population.Enemies)
				{
					if (&Other == &Enemy
						|| Other.Creature == ECataclysmDungeonCreature::Succubus)
					{
						continue;
					}
					if (StraightLineCells(Enemy.Cell, Other.Cell) <= AuraCells)
					{
						bHasCompany = true;
						break;
					}
				}

				if (!bHasCompany)
				{
					++Alone;
				}
			}
		}
	}

	// THE CONTROL. Without it this passes on a floor that contains no Succubus at
	// all, which is exactly what a broken escort rule would produce.
	TestTrue(TEXT("the sweep actually placed Succubi to check"), Succubi > 0);
	TestEqual(FString::Printf(
		TEXT("every one of %d Succubi has another creature inside its %.0f cell "
			 "aura"), Succubi, AuraCells), Alone, 0);

	return true;
}

// ---------------------------------------------------------------------------
// How many
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPopulationDensityTest,
	"Cataclysm.DungeonEnemies.ABiggerFloorHoldsMoreCreatures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPopulationDensityTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmPopulationTest;

	// THE FAULT THIS RULES OUT IS A FIXED COUNT, and every other test in this
	// file passes with one. Floor size is rolled per floor between 32 and 48
	// cells on each axis, which is a factor of 2.25 in area, so a fixed count
	// makes the small floors crowded and the large ones empty with nothing saying
	// so. Diablo II's own `MonDen` column is a rate per tile for this reason.
	//
	// TWO FLOORS OF THE SAME LAYOUT, ONE THE SMALLEST ALLOWED AND ONE THE
	// LARGEST, rather than two seeds and a hope that one came out bigger.
	FCataclysmFloorRequest Small;
	Small.DungeonSeed = 4242;
	Small.FloorNumber = 1;
	Small.Layout = ECataclysmFloorLayout::Halls;
	Small.Width = FCataclysmFloorGenerator::LeastFloorSide;
	Small.Height = FCataclysmFloorGenerator::LeastFloorSide;

	FCataclysmFloorRequest Large = Small;
	Large.Width = FCataclysmFloorGenerator::MostFloorSide;
	Large.Height = FCataclysmFloorGenerator::MostFloorSide;

	const FCataclysmFloorPlan SmallPlan = FCataclysmFloorGenerator::Generate(Small);
	const FCataclysmFloorPlan LargePlan = FCataclysmFloorGenerator::Generate(Large);

	if (!TestTrue(TEXT("both floors were built"),
				  SmallPlan.IsBuilt() && LargePlan.IsBuilt()))
	{
		return false;
	}

	TestTrue(FString::Printf(
		TEXT("the large floor has more walkable cells: %d against %d"),
		LargePlan.FloorCount(), SmallPlan.FloorCount()),
		LargePlan.FloorCount() > SmallPlan.FloorCount());

	const FCataclysmFloorPopulation OnSmall =
		FCataclysmFloorPopulator::Populate(SmallPlan);
	const FCataclysmFloorPopulation OnLarge =
		FCataclysmFloorPopulator::Populate(LargePlan);

	TestTrue(FString::Printf(
		TEXT("and it holds more creatures: %d against %d"),
		OnLarge.Enemies.Num(), OnSmall.Enemies.Num()),
		OnLarge.Enemies.Num() > OnSmall.Enemies.Num());

	// AND THE COUNT ASKED FOR IS THE DENSITY TIMES THE WALKABLE CELLS, exactly,
	// so this is the rule and not a coincidence of two floors.
	TestEqual(TEXT("the small floor asks for its own area times the density"),
		OnSmall.Wanted,
		FMath::RoundToInt(static_cast<float>(SmallPlan.FloorCount())
			* FCataclysmFloorPopulator::EnemiesPerWalkableCell));

	// AND THE DENSITY IS NEARLY ALWAYS REACHED, WHICH IS A WEAKER STATEMENT THAN
	// IT USED TO MAKE AND THE CHANGE IS DELIBERATE. Placement can fall short: it
	// stops looking once the count is met, a group that finds too few free cells
	// is placed smaller, and once every candidate cell is claimed by a group
	// already standing there is nowhere left to put another. A floor that quietly
	// delivered half of what its density asked for would look right in every
	// other test here, so something has to watch it.
	//
	// AT THE OLD DENSITY OF 0.08 EVERY FLOOR REACHED 1.000 OF WHAT IT ASKED FOR
	// and this test asserted exactly that. At 0.24, chosen by the project owner
	// in issue #809, ten floors in 360 fall short, and the reason is geometry
	// rather than a fault: groups must stand `LeastCellsBetweenPacks` apart and
	// a group's members within `MostCellsFromPackSite` of its middle, and on the
	// tightest floors those two rules together cannot fit three times as many
	// creatures. Measured over 1,000 seeds of each layout, the worst floor
	// reached 0.837 of what it asked for -- 0.971 on Halls, 0.880 on Caverns,
	// 0.837 on Arena.
	//
	// SO TWO NUMBERS ARE ASSERTED RATHER THAN ONE, because a single loose floor
	// would let a real collapse through. The worst single floor catches a floor
	// that gave up; the average across the sweep catches placement getting worse
	// everywhere at once, which a per-floor limit set below the worst case would
	// not notice.
	int32 ShortFloors = 0;
	int32 Floors = 0;
	float WorstFilled = 1.0f;
	double TotalFilled = 0.0;

	for (const ECataclysmFloorLayout Layout : EveryLayout())
	{
		for (int32 Seed = 1; Seed <= SweepSeeds; ++Seed)
		{
			const FCataclysmFloorPlan Plan = Floor(Seed, 1, Layout);
			if (!Plan.IsBuilt())
			{
				continue;
			}
			++Floors;

			const FCataclysmFloorPopulation Population =
				FCataclysmFloorPopulator::Populate(Plan);
			if (Population.Enemies.Num() < Population.Wanted)
			{
				++ShortFloors;
			}

			// A FLOOR THAT ASKED FOR NOTHING COUNTS AS FULL. Dividing by
			// `Wanted` when it is zero is the one way this measurement can go
			// wrong, and it would go wrong silently.
			const float Filled = (Population.Wanted > 0)
				? static_cast<float>(Population.Enemies.Num())
					/ static_cast<float>(Population.Wanted)
				: 1.0f;

			WorstFilled = FMath::Min(WorstFilled, Filled);
			TotalFilled += Filled;
		}
	}

	TestTrue(TEXT("the sweep actually populated floors to check"), Floors > 0);

	const float MeanFilled = (Floors > 0)
		? static_cast<float>(TotalFilled / Floors) : 0.0f;

	UE_LOG(LogTemp, Display,
		TEXT("CataclysmPopulationFill: %d floors, %d short, worst %.3f, mean %.4f"),
		Floors, ShortFloors, WorstFilled, MeanFilled);

	TestTrue(FString::Printf(
		TEXT("the worst of %d floors still holds three quarters of the number "
			 "its density asked for: %.3f"), Floors, WorstFilled),
		WorstFilled >= 0.75f);

	TestTrue(FString::Printf(
		TEXT("and across all %d floors the average is within one percent of "
			 "what was asked for: %.4f"), Floors, MeanFilled),
		MeanFilled >= 0.99f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPopulationScaleTest,
	"Cataclysm.DungeonEnemies.TheDensityCanBeTurnedUpAndDownAndOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPopulationScaleTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmPopulationTest;

	// WHAT `Cataclysm.DungeonEnemyScale` REACHES. How many creatures a floor
	// should hold is the one number in this feature the design document does not
	// answer, so being able to walk the same floor with twice as many, and with
	// none, is how it gets judged.
	const FCataclysmFloorPlan Plan = Floor(77, 1, ECataclysmFloorLayout::Halls);
	if (!TestTrue(TEXT("a floor was built"), Plan.IsBuilt()))
	{
		return false;
	}

	const FCataclysmFloorPopulation None =
		FCataclysmFloorPopulator::Populate(Plan, 0.0f);
	const FCataclysmFloorPopulation Half =
		FCataclysmFloorPopulator::Populate(Plan, 0.5f);
	const FCataclysmFloorPopulation Designed =
		FCataclysmFloorPopulator::Populate(Plan, 1.0f);

	TestEqual(TEXT("a scale of zero empties the floor"), None.Enemies.Num(), 0);
	TestEqual(TEXT("and leaves no groups on it"), None.PackCount, 0);

	TestTrue(FString::Printf(
		TEXT("half the density is fewer creatures: %d against %d"),
		Half.Enemies.Num(), Designed.Enemies.Num()),
		Half.Enemies.Num() < Designed.Enemies.Num());
	TestTrue(TEXT("and still more than none"), Half.Enemies.Num() > 0);

	// A NEGATIVE SCALE IS NOT A NEGATIVE COUNT. The console variable takes one to
	// mean "use the game mode's setting" and the game mode clamps, but a caller
	// that passed one through must not make the populator ask for -60 creatures.
	const FCataclysmFloorPopulation Nonsense =
		FCataclysmFloorPopulator::Populate(Plan, -3.0f);
	TestEqual(TEXT("a negative scale empties the floor rather than breaking it"),
		Nonsense.Enemies.Num(), 0);
	TestEqual(TEXT("and asks for no creatures"), Nonsense.Wanted, 0);

	// AN UNBUILT FLOOR HOLDS NOTHING, rather than failing. That is what lets a
	// caller populate without first asking whether there is a floor.
	const FCataclysmFloorPopulation OnNothing =
		FCataclysmFloorPopulator::Populate(FCataclysmFloorPlan());
	TestEqual(TEXT("a floor with nothing to stand on holds nothing"),
		OnNothing.Enemies.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPopulationVarietyTest,
	"Cataclysm.DungeonEnemies.AFloorHoldsMoreThanOneKindOfCreature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPopulationVarietyTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmPopulationTest;

	// A FLOOR OF SIXTY IMPS PASSES EVERY OTHER TEST IN THIS FILE. They would be
	// on the floor, in groups, spread out and reachable, and the floor would be
	// one fight repeated. This is the test that can tell.
	int32 WorstKinds = MAX_int32;
	int32 Floors = 0;

	// AND EVERY KIND THE POPULATOR CAN DRAW ACTUALLY APPEARS, over the sweep. A
	// pack kind with a weight of zero, or one left out of the draw, would
	// otherwise be invisible.
	TSet<ECataclysmDungeonCreature> EverSeen;

	for (const ECataclysmFloorLayout Layout : EveryLayout())
	{
		for (int32 Seed = 1; Seed <= SweepSeeds; ++Seed)
		{
			const FCataclysmFloorPlan Plan = Floor(Seed, 1, Layout);
			if (!Plan.IsBuilt())
			{
				continue;
			}

			const FCataclysmFloorPopulation Population =
				FCataclysmFloorPopulator::Populate(Plan);
			if (Population.Enemies.Num() == 0)
			{
				continue;
			}
			++Floors;

			WorstKinds = FMath::Min(WorstKinds, Population.KindsPresent());
			for (const FCataclysmEnemyPlacement& Enemy : Population.Enemies)
			{
				EverSeen.Add(Enemy.Creature);
			}
		}
	}

	TestTrue(TEXT("the sweep actually populated floors to check"), Floors > 0);
	TestTrue(FString::Printf(
		TEXT("the least varied of %d floors still held %d kinds of creature"),
		Floors, WorstKinds), WorstKinds >= 2);

	for (uint8 Which = 0; Which < static_cast<uint8>(ECataclysmDungeonCreature::Count); ++Which)
	{
		const ECataclysmDungeonCreature Creature =
			static_cast<ECataclysmDungeonCreature>(Which);
		TestTrue(FString::Printf(TEXT("a %s is placed somewhere in the sweep"),
				 CataclysmDungeonCreatureName(Creature)),
				 EverSeen.Contains(Creature));
	}

	return true;
}

// ---------------------------------------------------------------------------
// The same floor twice
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPopulationSameSeedTest,
	"Cataclysm.DungeonEnemies.TheSameSeedPutsTheSameCreaturesInTheSamePlaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPopulationSameSeedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmPopulationTest;

	// THE SAME REQUIREMENT THE LAYOUT CARRIES, for the same two reasons issue #40
	// gives: a dungeon must hold the same creatures when the player leaves and
	// returns, and a floor that goes wrong has to be reproducible from its seed.
	int32 Differed = 0;
	int32 Compared = 0;

	for (const ECataclysmFloorLayout Layout : EveryLayout())
	{
		for (int32 Seed = 1; Seed <= SweepSeeds; ++Seed)
		{
			const FCataclysmFloorPlan Plan = Floor(Seed, 1, Layout);
			if (!Plan.IsBuilt())
			{
				continue;
			}

			++Compared;
			if (!Same(FCataclysmFloorPopulator::Populate(Plan),
					  FCataclysmFloorPopulator::Populate(Plan)))
			{
				++Differed;
			}
		}
	}

	TestTrue(TEXT("the sweep actually compared floors"), Compared > 0);
	TestEqual(FString::Printf(
		TEXT("populating the same floor twice gives the same creatures, over "
			 "%d floors"), Compared), Differed, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPopulationDifferentFloorTest,
	"Cataclysm.DungeonEnemies.ADifferentFloorHoldsDifferentCreatures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPopulationDifferentFloorTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmPopulationTest;

	// THE CONTROL FOR THE TEST ABOVE. A populator that returned the same list
	// whatever it was handed would satisfy "the same seed gives the same
	// creatures" perfectly, and would mean every floor of a fifty-floor dungeon
	// held the same fight in the same corners.
	int32 Identical = 0;
	int32 Compared = 0;

	for (int32 FloorNumber = 1; FloorNumber < SweepSeeds; ++FloorNumber)
	{
		const FCataclysmFloorPlan First =
			Floor(31337, FloorNumber, ECataclysmFloorLayout::Halls);
		const FCataclysmFloorPlan Next =
			Floor(31337, FloorNumber + 1, ECataclysmFloorLayout::Halls);

		if (!First.IsBuilt() || !Next.IsBuilt())
		{
			continue;
		}

		++Compared;
		if (Same(FCataclysmFloorPopulator::Populate(First),
				 FCataclysmFloorPopulator::Populate(Next)))
		{
			++Identical;
		}
	}

	TestTrue(TEXT("the sweep actually compared floors"), Compared > 0);
	TestEqual(FString::Printf(
		TEXT("walking down %d floors of one dungeon never meets the same "
			 "creatures in the same places twice in a row"), Compared),
		Identical, 0);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
