// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Components/InstancedStaticMeshComponent.h"
#include "Dungeon/CataclysmDungeonFloor.h"
#include "Dungeon/CataclysmFloorGenerator.h"
#include "Dungeon/CataclysmFloorPlan.h"
#include "Engine/StaticMesh.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for the geometry a dungeon floor is built out of.
 *
 * WHAT THESE COVER AND WHAT THEY CANNOT. Every one of them asks where a block
 * is, not what it looks like. The automation harness runs with `-nullrhi`, so
 * nothing here has been rendered and none of these would notice a floor that is
 * the wrong colour, unlit, or invisible. What they do notice is a floor whose
 * walkable surface is not the same set of cells the generator's tests measured,
 * which is the fault that would make every guarantee in
 * `CataclysmFloorGeneratorTests.cpp` stop meaning anything.
 *
 * They also do not cover navigation. Whether a character can path across this
 * geometry is the next piece of work and needs a running editor to answer.
 */

namespace CataclysmDungeonFloorTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	FCataclysmFloorPlan PlanFor(int32 Seed, ECataclysmFloorLayout Layout)
	{
		FCataclysmFloorRequest Request;
		Request.DungeonSeed = Seed;
		Request.FloorNumber = 1;
		Request.Layout = Layout;
		return FCataclysmFloorGenerator::Generate(Request);
	}

	/** Every layout, so a sweep cannot miss one. */
	TArray<ECataclysmFloorLayout> EveryLayout()
	{
		TArray<ECataclysmFloorLayout> Out;
		for (uint8 Index = 0; Index < static_cast<uint8>(ECataclysmFloorLayout::Count); ++Index)
		{
			Out.Add(static_cast<ECataclysmFloorLayout>(Index));
		}
		return Out;
	}

	ACataclysmDungeonFloor* SpawnFloor(UWorld* World)
	{
		return World ? World->SpawnActor<ACataclysmDungeonFloor>(
			FVector::ZeroVector, FRotator::ZeroRotator) : nullptr;
	}

	/** How many seeds each sweep covers. Building geometry is not free. */
	constexpr int32 SweepSeeds = 12;
}

// ---------------------------------------------------------------------------
// The surface matches the plan
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorGroundIsTheWalkableCellsTest,
	"Cataclysm.DungeonFloor.TheGroundIsExactlyTheCellsTheGeneratorSaidAreWalkable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorGroundIsTheWalkableCellsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonFloorTest;

	// THE ONE THAT MATTERS MOST. The generator's tests prove things about a set
	// of cells -- the exit is reachable, there is no single-file stretch longer
	// than three cells. Every one of those claims transfers to the game only if
	// the ground a player can stand on is that same set. A large plane under
	// everything would put walkable surface outside the walls and quietly break
	// the lot.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}

	for (const ECataclysmFloorLayout Layout : EveryLayout())
	{
		for (int32 Seed = 1; Seed <= SweepSeeds; ++Seed)
		{
			const FCataclysmFloorPlan Plan = PlanFor(Seed, Layout);
			ACataclysmDungeonFloor* Floor = SpawnFloor(World);
			if (!TestNotNull(TEXT("the floor actor spawned"), Floor))
			{
				World->DestroyWorld(false);
				return false;
			}

			TestTrue(FString::Printf(TEXT("%s seed %d builds"),
					 CataclysmFloorLayoutName(Layout), Seed), Floor->Build(Plan));

			// One ground block per walkable cell, and no more.
			TestEqual(FString::Printf(
				TEXT("%s seed %d: ground blocks equal walkable cells"),
				CataclysmFloorLayoutName(Layout), Seed),
				Floor->GroundBlockCount(), Plan.FloorCount());

			// And each of them is centred on a cell that really is walkable.
			// The count alone would pass on geometry placed at random.
			int32 NotOnWalkableGround = 0;
			for (int32 Index = 0; Index < Floor->GroundBlockCount(); ++Index)
			{
				FTransform Block;
				Floor->Ground->GetInstanceTransform(Index, Block, /*bWorldSpace=*/true);
				const FIntPoint Cell = Floor->CellOfWorld(Block.GetLocation());
				if (!Plan.IsFloor(Cell))
				{
					++NotOnWalkableGround;
				}
			}
			TestEqual(FString::Printf(
				TEXT("%s seed %d: every ground block is on a walkable cell"),
				CataclysmFloorLayoutName(Layout), Seed), NotOnWalkableGround, 0);

			Floor->Destroy();
		}
	}

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorWallsEncloseTest,
	"Cataclysm.DungeonFloor.EveryWalkableCellIsWalledOffFromTheRock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorWallsEncloseTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonFloorTest;

	// A WALL IS A FACE, NOT A CELL. One piece stands on each side where walkable
	// ground meets rock, plus one in each corner where two of those meet at a
	// right angle and leave a hole. Nothing is built against rock nobody can see.
	//
	// This checks both directions: no piece stands where one is not needed, and
	// none is missing where one is.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}

	int32 TotalWalls = 0;
	int32 TotalGround = 0;

	for (const ECataclysmFloorLayout Layout : EveryLayout())
	{
		for (int32 Seed = 1; Seed <= SweepSeeds; ++Seed)
		{
			const FCataclysmFloorPlan Plan = PlanFor(Seed, Layout);
			ACataclysmDungeonFloor* Floor = SpawnFloor(World);
			if (!TestNotNull(TEXT("the floor actor spawned"), Floor))
			{
				World->DestroyWorld(false);
				return false;
			}
			Floor->Build(Plan);

			const int32 Wanted = ACataclysmDungeonFloor::WallPiecesFor(Plan);

			TestEqual(FString::Printf(
				TEXT("%s seed %d: a wall piece stands on every side where ground "
					 "meets rock, and in every corner, and nowhere else"),
				CataclysmFloorLayoutName(Layout), Seed),
				Floor->WallBlockCount(), Wanted);

			// EVERY PIECE STANDS AGAINST GROUND. A floor with no walkable cells
			// would need none, and a floor with some needs at least four -- the
			// grid's border is always rock, so the walkable area always ends
			// somewhere.
			TestTrue(FString::Printf(
				TEXT("%s seed %d: a floor with %d walkable cells has walls: %d"),
				CataclysmFloorLayoutName(Layout), Seed, Plan.FloorCount(),
				Floor->WallBlockCount()),
				Floor->WallBlockCount() >= 4);

			TotalWalls += Floor->WallBlockCount();
			TotalGround += Plan.FloorCount();

			Floor->Destroy();
		}
	}

	// AND THE WALL IS A SKIN, NOT A SOLID. Building walls as faces is only worth
	// anything if there are far fewer of them than there is ground: a floor whose
	// wall pieces outnumbered its walkable cells would be one where the walls
	// cost more than the room they enclose.
	//
	// ASKED OF THE WHOLE SWEEP RATHER THAN OF EACH FLOOR, because a small
	// enough floor can legitimately be almost all edge.
	TestTrue(FString::Printf(
		TEXT("across the sweep, %d wall pieces enclose %d walkable cells, which "
			 "is %.0f%% and the limit is 60%%"),
		TotalWalls, TotalGround,
		(TotalGround > 0) ? 100.0f * TotalWalls / TotalGround : 100.0f),
		TotalGround > 0 && TotalWalls <= TotalGround * 6 / 10);

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorNeedsWallRuleTest,
	"Cataclysm.DungeonFloor.AWallIsBuiltOnEverySideAndInEveryCorner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorNeedsWallRuleTest::RunTest(const FString& Parameters)
{
	// THE CONTROL FOR THE TEST ABOVE, which compares the built walls against
	// this same rule and would agree with it however wrong the rule was. Worked
	// out by hand here instead.
	//
	// ONE WALKABLE CELL ALONE ON A 5 BY 5 GRID needs eight pieces: four sides,
	// and four corners. A rule that skipped the corners would leave a hole you
	// can see through at every corner of every room, because each side piece
	// spans only its own cell's width.
	FCataclysmFloorPlan Alone;
	Alone.Reset(5, 5);
	Alone.Carve(FIntPoint(2, 2));

	TestEqual(TEXT("a lone walkable cell needs four sides and four corners"),
			  ACataclysmDungeonFloor::WallPiecesFor(Alone), 8);

	// TWO CELLS SIDE BY SIDE need six sides, not eight: the side they share is
	// ground on both sides and gets nothing. And four corners, not eight: on the
	// side each cell shares with the other there is no corner to fill, because
	// that side is ground.
	//
	// TWELVE WAS WRITTEN HERE FIRST AND THE CODE SAID TEN. The code was right.
	// That is what a hand-worked control is for; it is only worth having if it is
	// worked out independently, and independent means it can be the one that is
	// wrong.
	FCataclysmFloorPlan Pair;
	Pair.Reset(5, 5);
	Pair.Carve(FIntPoint(2, 2));
	Pair.Carve(FIntPoint(3, 2));

	TestEqual(TEXT("two cells side by side need six sides and four corners"),
			  ACataclysmDungeonFloor::WallPiecesFor(Pair), 10);

	// TWO CELLS TOUCHING ONLY AT A CORNER still get a piece in the corner
	// between them. Nothing can walk that diagonal, and without the piece there
	// is a two-metre hole the navigation mesh would route a character through.
	// Each cell needs four sides and four corners: sixteen.
	FCataclysmFloorPlan Diagonal;
	Diagonal.Reset(5, 5);
	Diagonal.Carve(FIntPoint(1, 1));
	Diagonal.Carve(FIntPoint(2, 2));

	TestEqual(TEXT("two cells touching only at a corner are still walled apart"),
			  ACataclysmDungeonFloor::WallPiecesFor(Diagonal), 16);

	// AN EMPTY PLAN NEEDS NOTHING, which is the case a rule that counted solid
	// cells rather than faces would get wrong: it would want a wall on all 25.
	FCataclysmFloorPlan Empty;
	Empty.Reset(5, 5);

	TestEqual(TEXT("a plan with no walkable cells needs no walls"),
			  ACataclysmDungeonFloor::WallPiecesFor(Empty), 0);

	return true;
}

// ---------------------------------------------------------------------------
// Where things are
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorCoordinatesRoundTripTest,
	"Cataclysm.DungeonFloor.ACellAndItsWorldPositionAgreeBothWays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorCoordinatesRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonFloorTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}

	ACataclysmDungeonFloor* Floor = SpawnFloor(World);
	if (!TestNotNull(TEXT("the floor actor spawned"), Floor))
	{
		World->DestroyWorld(false);
		return false;
	}

	// AT AN OFFSET, NOT AT THE ORIGIN. Both conversions add and subtract the
	// actor's own location, and at the origin a version that forgot to would
	// pass.
	Floor->SetActorLocation(FVector(1234.0f, -5678.0f, 250.0f));
	Floor->Build(PlanFor(4242, ECataclysmFloorLayout::Halls));

	const FCataclysmFloorPlan& Plan = Floor->GetPlan();

	int32 Wrong = 0;
	for (int32 Index = 0; Index < Plan.Cells.Num(); ++Index)
	{
		const FIntPoint Cell = Plan.CellAt(Index);
		if (Floor->CellOfWorld(Floor->WorldOfCell(Cell)) != Cell)
		{
			++Wrong;
		}
	}
	TestEqual(TEXT("every cell survives being turned into a world position and "
				   "back"), Wrong, 0);

	// Neighbouring cells are one cell apart in the world, which pins the scale.
	// A round trip alone would pass at any cell size, including zero.
	const float Apart = FVector::Dist2D(Floor->WorldOfCell(FIntPoint(3, 3)),
										Floor->WorldOfCell(FIntPoint(4, 3)));
	TestTrue(FString::Printf(TEXT("neighbouring cells are %.0f cm apart, and the "
								  "generator's cell is %.0f cm"),
			 Apart, FCataclysmFloorGenerator::CellSizeCm),
		FMath::IsNearlyEqual(Apart, FCataclysmFloorGenerator::CellSizeCm, 0.5f));

	// The middle cell of an odd-sized floor sits on the actor itself, which is
	// what "the floor is centred on the actor" means.
	if (Plan.Width % 2 == 1 && Plan.Height % 2 == 1)
	{
		const FVector Middle = Floor->WorldOfCell(
			FIntPoint(Plan.Width / 2, Plan.Height / 2));
		TestTrue(TEXT("the middle cell of an odd-sized floor is at the actor"),
			Middle.Equals(Floor->GetActorLocation(), 0.5f));
	}

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorEndsAreOnGroundTest,
	"Cataclysm.DungeonFloor.TheArrivalPointAndTheStairsAreBothOnWalkableGround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorEndsAreOnGroundTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonFloorTest;

	// A player put down on solid rock, or stairs buried in a wall, is the way
	// this breaks. The generator promises both are walkable cells; this asks
	// whether the world positions they turn into still land on them.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}

	for (const ECataclysmFloorLayout Layout : EveryLayout())
	{
		for (int32 Seed = 1; Seed <= SweepSeeds; ++Seed)
		{
			ACataclysmDungeonFloor* Floor = SpawnFloor(World);
			if (!TestNotNull(TEXT("the floor actor spawned"), Floor))
			{
				World->DestroyWorld(false);
				return false;
			}
			Floor->SetActorLocation(FVector(500.0f, -500.0f, 0.0f));
			Floor->Build(PlanFor(Seed, Layout));

			const FCataclysmFloorPlan& Plan = Floor->GetPlan();

			TestTrue(FString::Printf(
				TEXT("%s seed %d: the arrival point is on walkable ground"),
				CataclysmFloorLayoutName(Layout), Seed),
				Plan.IsFloor(Floor->CellOfWorld(Floor->EntranceWorld())));

			TestTrue(FString::Printf(
				TEXT("%s seed %d: the stairs are on walkable ground"),
				CataclysmFloorLayoutName(Layout), Seed),
				Plan.IsFloor(Floor->CellOfWorld(Floor->ExitWorld())));

			TestTrue(FString::Printf(
				TEXT("%s seed %d: the stairs are not where the player arrives"),
				CataclysmFloorLayoutName(Layout), Seed),
				Floor->EntranceWorld() != Floor->ExitWorld());

			Floor->Destroy();
		}
	}

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorStandsOnTopTest,
	"Cataclysm.DungeonFloor.AGroundBlocksTopSurfaceIsWhereTheCharacterStands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorStandsOnTopTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonFloorTest;

	// A ground block is sunk by half its thickness so its top surface lands at
	// the actor's own height. Get this wrong and a character placed at
	// `WorldOfCell` is either standing inside the floor or hovering over it.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}

	ACataclysmDungeonFloor* Floor = SpawnFloor(World);
	if (!TestNotNull(TEXT("the floor actor spawned"), Floor))
	{
		World->DestroyWorld(false);
		return false;
	}
	Floor->SetActorLocation(FVector(0.0f, 0.0f, 800.0f));
	Floor->Build(PlanFor(7, ECataclysmFloorLayout::Caverns));

	if (!TestTrue(TEXT("the floor built"), Floor->IsBuilt()))
	{
		World->DestroyWorld(false);
		return false;
	}

	FTransform Block;
	Floor->Ground->GetInstanceTransform(0, Block, /*bWorldSpace=*/true);

	const float HalfThickness =
		ACataclysmDungeonFloor::GroundThicknessCm * 0.5f;
	const float TopSurface = Block.GetLocation().Z + HalfThickness;

	TestTrue(FString::Printf(
		TEXT("a ground block's top surface is at %.1f and the actor is at %.1f"),
		TopSurface, Floor->GetActorLocation().Z),
		FMath::IsNearlyEqual(TopSurface, Floor->GetActorLocation().Z, 0.5f));

	// And a wall block sits on that surface rather than in it.
	if (Floor->WallBlockCount() > 0)
	{
		FTransform Wall;
		Floor->Walls->GetInstanceTransform(0, Wall, /*bWorldSpace=*/true);
		const float WallBottom =
			Wall.GetLocation().Z - ACataclysmDungeonFloor::WallHeightCm * 0.5f;
		TestTrue(FString::Printf(
			TEXT("a wall block's bottom is at %.1f, the walking surface is at %.1f"),
			WallBottom, Floor->GetActorLocation().Z),
			FMath::IsNearlyEqual(WallBottom, Floor->GetActorLocation().Z, 0.5f));
	}

	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// Building again
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorRebuildTest,
	"Cataclysm.DungeonFloor.BuildingTwiceDoesNotDoubleTheGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorRebuildTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonFloorTest;

	// REBUILDING IS THE ORDINARY CASE. Taking the stairs down replaces the
	// floor, so this happens once per floor of a dungeon that can run past 150.
	// A build that added instead of replacing would leave the last floor's walls
	// standing inside the next one.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}

	ACataclysmDungeonFloor* Floor = SpawnFloor(World);
	if (!TestNotNull(TEXT("the floor actor spawned"), Floor))
	{
		World->DestroyWorld(false);
		return false;
	}

	const FCataclysmFloorPlan First = PlanFor(11, ECataclysmFloorLayout::Halls);
	Floor->Build(First);
	const int32 FirstGround = Floor->GroundBlockCount();
	const int32 FirstWalls = Floor->WallBlockCount();

	// The same floor again: identical counts, not doubled.
	Floor->Build(First);
	TestEqual(TEXT("building the same floor again leaves the ground count alone"),
			  Floor->GroundBlockCount(), FirstGround);
	TestEqual(TEXT("building the same floor again leaves the wall count alone"),
			  Floor->WallBlockCount(), FirstWalls);

	// A different floor: the counts become that floor's, not a sum.
	const FCataclysmFloorPlan Second = PlanFor(12, ECataclysmFloorLayout::Caverns);
	Floor->Build(Second);
	TestEqual(TEXT("building a different floor gives that floor's ground count"),
			  Floor->GroundBlockCount(), Second.FloorCount());
	TestTrue(TEXT("the plan the actor reports is the one it was last built from"),
			 Floor->GetPlan().Cells == Second.Cells);

	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// The block itself
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorHasAMeshTest,
	"Cataclysm.DungeonFloor.TheBlocksHaveAMeshAndCollide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorHasAMeshTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonFloorTest;

	// WITHOUT THIS EVERY OTHER TEST HERE STILL PASSES. An instanced mesh
	// component happily records transforms with no mesh set, so the counts and
	// the positions would all be right and the floor would be invisible and
	// walked straight through. Collision matters twice over: it is what stops a
	// character falling, and the navigation mesh is built from it.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}

	ACataclysmDungeonFloor* Floor = SpawnFloor(World);
	if (!TestNotNull(TEXT("the floor actor spawned"), Floor))
	{
		World->DestroyWorld(false);
		return false;
	}

	TestNotNull(TEXT("the ground component has a static mesh"),
				Floor->Ground->GetStaticMesh().Get());
	TestNotNull(TEXT("the wall component has a static mesh"),
				Floor->Walls->GetStaticMesh().Get());

	TestEqual(TEXT("the ground collides"),
			  Floor->Ground->GetCollisionEnabled(),
			  ECollisionEnabled::QueryAndPhysics);
	TestEqual(TEXT("the walls collide"),
			  Floor->Walls->GetCollisionEnabled(),
			  ECollisionEnabled::QueryAndPhysics);

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorExtentTest,
	"Cataclysm.DungeonFloor.TheStatedExtentContainsEveryBlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorExtentTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonFloorTest;

	// `Extent` is what a navigation bounds volume will be sized from in the next
	// piece of work. A volume that does not contain the whole floor produces a
	// navigation mesh with holes in it, and nothing reports that: enemies simply
	// stop pathing in the part that was left out.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}

	for (const ECataclysmFloorLayout Layout : EveryLayout())
	{
		ACataclysmDungeonFloor* Floor = SpawnFloor(World);
		if (!TestNotNull(TEXT("the floor actor spawned"), Floor))
		{
			World->DestroyWorld(false);
			return false;
		}
		Floor->SetActorLocation(FVector(-2000.0f, 3000.0f, 100.0f));
		Floor->Build(PlanFor(99, Layout));

		const FBox Bounds = FBox::BuildAABB(Floor->GetActorLocation(),
											Floor->Extent());

		int32 Outside = 0;
		for (int32 Index = 0; Index < Floor->GroundBlockCount(); ++Index)
		{
			FTransform Block;
			Floor->Ground->GetInstanceTransform(Index, Block, /*bWorldSpace=*/true);
			Outside += Bounds.IsInsideOrOn(Block.GetLocation()) ? 0 : 1;
		}
		for (int32 Index = 0; Index < Floor->WallBlockCount(); ++Index)
		{
			FTransform Block;
			Floor->Walls->GetInstanceTransform(Index, Block, /*bWorldSpace=*/true);
			Outside += Bounds.IsInsideOrOn(Block.GetLocation()) ? 0 : 1;
		}

		TestEqual(FString::Printf(
			TEXT("%s: every block's middle is inside the stated extent"),
			CataclysmFloorLayoutName(Layout)), Outside, 0);

		// The top of a wall is the highest thing on the floor and it has to be
		// inside too, which is the case the Z extent was got wrong on first.
		TestTrue(TEXT("the top of a wall is inside the stated extent"),
			Bounds.IsInsideOrOn(Floor->GetActorLocation()
								+ FVector(0.0f, 0.0f,
										  ACataclysmDungeonFloor::WallHeightCm)));

		Floor->Destroy();
	}

	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
