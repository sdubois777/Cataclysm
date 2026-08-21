// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AI/NavigationSystemBase.h"
#include "CataclysmLevelAuthoring.h"
#include "Dungeon/CataclysmDungeonFloor.h"
#include "Dungeon/CataclysmFloorGenerator.h"
#include "Dungeon/CataclysmFloorPlan.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Whether a character can actually walk a generated dungeon floor.
 *
 * WHY THIS IS THE TEST THAT MATTERS. `CataclysmFloorGeneratorTests.cpp` proves
 * things about a grid of cells: the exit is reachable, there is no single-file
 * stretch longer than three cells. `CataclysmDungeonFloorTests.cpp` proves the
 * blocks are placed on those cells. **Neither of them proves anything can move.**
 * Movement in this game goes through Unreal's navigation mesh -- click-to-move
 * calls `SimpleMoveToLocation`, and every enemy paths -- and that mesh is built
 * by a system with its own rules about what counts as ground. A floor that is
 * correct in every way above and has no navigation mesh over it is a floor
 * nothing can cross, and nothing else here would notice.
 *
 * IT FOUND A REAL FAULT ON THE FIRST RUN. `bCanEverAffectNavigation` is set to
 * false in `UActorComponent`'s constructor and again in `UPrimitiveComponent`'s,
 * and no mesh component turns it back on, so a mesh created in C++ is invisible
 * to the navigation system however solid it is. The dungeon floor could never
 * have had a navigation mesh over it. The first run of this test reported the
 * navigable world bounds as zero-sized while the floor's own extent was 8,000 by
 * 8,000 centimetres.
 *
 * WHY IT LIVES IN THE EDITOR MODULE. A navigation mesh is only built inside a
 * bounds volume, and a bounds volume takes its size from a brush built by
 * `UCubeBuilder`, which is editor-only. That is the whole reason
 * `UCataclysmLevelAuthoring` exists -- it is what gives `L_Sandbox` its
 * navigation bounds -- and this uses the same three steps the sandbox's own
 * generator uses. Putting the test here rather than in the `Cataclysm` module is
 * what lets it build a real volume instead of approximating one.
 *
 * TWO OTHER ROUTES WERE TRIED AND BOTH FAILED, recorded so they are not tried
 * again. `UNavigationSystemV1::bWholeWorldNavigable`, which asks the system to
 * work the bounds out from the geometry, left the bounds zero-sized; the engine's
 * own comment beside that flag reads "currently broken". And
 * `AddNavigationBoundsUpdateRequest`, which would register bounds directly, is
 * protected.
 */

namespace CataclysmDungeonNavTest
{
	/** A floor in a world, with a navigation mesh built over it. */
	struct FNavigableFloor
	{
		UWorld* World = nullptr;
		ACataclysmDungeonFloor* Floor = nullptr;
		UNavigationSystemV1* Navigation = nullptr;
		ANavigationData* NavData = nullptr;

		/** Empty when everything worked, otherwise what stopped it. */
		FString Trouble;

		bool IsReady() const
		{
			return World && Floor && Navigation && NavData && Trouble.IsEmpty();
		}
	};

	/** How much wider than the floor the navigation bounds are made. */
	constexpr double BoundsMarginCm = 800.0;

	/** How tall the navigation bounds are. Walls are 400 cm. */
	constexpr double BoundsHeightCm = 1600.0;

	FNavigableFloor Build(int32 Seed, ECataclysmFloorLayout Layout)
	{
		FNavigableFloor Out;

		Out.World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Out.World)
		{
			Out.Trouble = TEXT("no test world could be created");
			return Out;
		}

		Out.Floor = Out.World->SpawnActor<ACataclysmDungeonFloor>(
			FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Out.Floor)
		{
			Out.Trouble = TEXT("the floor actor did not spawn");
			return Out;
		}

		FCataclysmFloorRequest Request;
		Request.DungeonSeed = Seed;
		Request.FloorNumber = 1;
		Request.Layout = Layout;
		if (!Out.Floor->Build(FCataclysmFloorGenerator::Generate(Request)))
		{
			Out.Trouble = TEXT("the floor did not build");
			return Out;
		}

		// THE BOUNDS VOLUME GOES IN BEFORE THE NAVIGATION SYSTEM, and the order is
		// not a preference. A navigation system spawns its navigation data while
		// it initialises, and only when there is somewhere to build -- meaning at
		// least one bounds volume already registered. Added afterwards, the volume
		// is there, the build reports success, and `GetDefaultNavDataInstance`
		// answers null because no navigation data was ever created. That is what
		// the first attempt got, and the failure said only "the navigation system
		// built no navigation data".
		//
		// It is also the order a real level uses: the volume is placed in the map
		// and the navigation system initialises afterwards.
		//
		// The bounds are centred on the walls rather than on the walking surface,
		// so the volume contains the geometry from the underside of a ground
		// block to the top of a wall with room to spare on both sides.
		const FVector Extent = Out.Floor->Extent();
		const FVector Origin = Out.Floor->GetActorLocation()
			+ FVector(0.0, 0.0, ACataclysmDungeonFloor::WallHeightCm * 0.5);
		const FVector Size(Extent.X * 2.0 + BoundsMarginCm,
						   Extent.Y * 2.0 + BoundsMarginCm,
						   BoundsHeightCm);

		ANavMeshBoundsVolume* Volume =
			UCataclysmLevelAuthoring::AddNavMeshBounds(Out.World, Origin, Size);
		if (!Volume)
		{
			Out.Trouble = TEXT("the navigation bounds volume could not be built");
			return Out;
		}

		// Checked, not assumed, exactly as the sandbox's generator checks it: a
		// volume whose brush failed to build reports a zero extent, covers
		// nothing, and produces an empty navigation mesh with no error anywhere.
		const FVector Built = UCataclysmLevelAuthoring::GetVolumeExtent(Volume);
		if (Built.X < Extent.X || Built.Y < Extent.Y)
		{
			Out.Trouble = FString::Printf(
				TEXT("the navigation bounds volume built to extent %s, which does "
					 "not cover a floor of extent %s"),
				*Built.ToCompactString(), *Extent.ToCompactString());
			return Out;
		}

		FNavigationSystem::AddNavigationSystemToWorld(
			*Out.World, FNavigationSystemRunMode::GameMode,
			/*NavigationSystemConfig=*/nullptr,
			/*bInitializeForWorld=*/true,
			/*bOverridePreviousNavSys=*/true);

		Out.Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Out.World);
		if (!Out.Navigation)
		{
			Out.Trouble = TEXT("no navigation system was created for the world");
			return Out;
		}

		// Synchronous: `BuildNavigation` releases the build locks and calls
		// `Build`, which rebuilds every navigation data and waits for completion.
		// There is nothing to tick afterwards.
		if (!UCataclysmLevelAuthoring::BuildNavigation(Out.World))
		{
			Out.Trouble = TEXT("the navigation build did not run");
			return Out;
		}

		Out.NavData = Out.Navigation->GetDefaultNavDataInstance(
			FNavigationSystem::DontCreate);
		if (!Out.NavData)
		{
			Out.Trouble = TEXT("the navigation system built no navigation data");
			return Out;
		}

		return Out;
	}

	void TearDown(FNavigableFloor& Setup)
	{
		if (Setup.World)
		{
			Setup.World->DestroyWorld(false);
			Setup.World = nullptr;
		}
	}

	/** How far off a point may be and still count as on the navigation mesh. */
	const FVector CloseEnough(150.0, 150.0, 300.0);
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorHasANavigationMeshTest,
	"Cataclysm.DungeonFloor.AGeneratedFloorGetsANavigationMeshOverIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorHasANavigationMeshTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonNavTest;

	FNavigableFloor Setup = Build(3, ECataclysmFloorLayout::Halls);
	if (!TestTrue(FString::Printf(TEXT("a navigable floor was set up: %s"),
				  *Setup.Trouble), Setup.IsReady()))
	{
		TearDown(Setup);
		return false;
	}

	const FCataclysmFloorPlan& Plan = Setup.Floor->GetPlan();

	FNavLocation Where;
	TestTrue(TEXT("where the player arrives is on the navigation mesh"),
		Setup.Navigation->ProjectPointToNavigation(
			Setup.Floor->EntranceWorld(), Where, CloseEnough));

	TestTrue(TEXT("the stairs are on the navigation mesh"),
		Setup.Navigation->ProjectPointToNavigation(
			Setup.Floor->ExitWorld(), Where, CloseEnough));

	// And the floor generally, not only its two named cells. The count of misses
	// is reported rather than stopping at the first, because "a few tiles are
	// missing" and "there is no navigation mesh" are different faults and only
	// the number tells them apart.
	int32 Walkable = 0;
	int32 Missed = 0;
	for (int32 Index = 0; Index < Plan.Cells.Num(); ++Index)
	{
		const FIntPoint Cell = Plan.CellAt(Index);
		if (!Plan.IsFloor(Cell))
		{
			continue;
		}
		++Walkable;
		FNavLocation Landed;
		if (!Setup.Navigation->ProjectPointToNavigation(
				Setup.Floor->WorldOfCell(Cell), Landed, CloseEnough))
		{
			++Missed;
		}
	}

	// ZERO IS NOT DEMANDED AND SHOULD NOT BE. Recast insets the navigation mesh
	// from the edge of the walking surface by the agent's radius, so the middle
	// of a cell hard against a wall can legitimately sit just outside it. What
	// this rules out is the fault that matters: no navigation mesh, or one over
	// a fraction of the floor.
	const float MissedShare = (Walkable > 0)
		? static_cast<float>(Missed) / static_cast<float>(Walkable) : 1.0f;

	TestTrue(FString::Printf(
		TEXT("%d of %d walkable cells are on the navigation mesh; %d are not, "
			 "which is %.1f%% and the limit is 15%%"),
		Walkable - Missed, Walkable, Missed, MissedShare * 100.0f),
		MissedShare <= 0.15f);

	TearDown(Setup);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorCanBeWalkedTest,
	"Cataclysm.DungeonFloor.ACharacterCanPathFromTheArrivalPointToTheStairs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorCanBeWalkedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonNavTest;

	// THE QUESTION THE WHOLE DUNGEON RESTS ON. The generator promises the stairs
	// are reachable from where the player arrives, and proves it over a grid of
	// cells. This asks the navigation system the same question about the geometry
	// that grid turned into, and the two can disagree: a corridor is two cells,
	// which is eight metres, but Recast insets the navigation mesh at every edge
	// by the agent's radius, so a narrow enough passage has surface on it that a
	// character cannot use.
	for (uint8 Which = 0; Which < static_cast<uint8>(ECataclysmFloorLayout::Count); ++Which)
	{
		const ECataclysmFloorLayout Layout = static_cast<ECataclysmFloorLayout>(Which);

		FNavigableFloor Setup = Build(21 + Which, Layout);
		if (!TestTrue(FString::Printf(TEXT("%s: a navigable floor was set up: %s"),
					  CataclysmFloorLayoutName(Layout), *Setup.Trouble),
					  Setup.IsReady()))
		{
			TearDown(Setup);
			return false;
		}

		FPathFindingQuery Query(nullptr, *Setup.NavData,
								Setup.Floor->EntranceWorld(),
								Setup.Floor->ExitWorld());

		// PARTIAL PATHS REFUSED, AND THE DEFAULT IS TO ALLOW THEM. A partial path
		// gets as close as it can, gives up, and reports success. Left alone this
		// test would pass on a floor cut in half.
		Query.SetAllowPartialPaths(false);

		const FPathFindingResult Result = Setup.Navigation->FindPathSync(Query);

		TestTrue(FString::Printf(
			TEXT("%s: a path exists from where the player arrives to the stairs"),
			CataclysmFloorLayoutName(Layout)), Result.IsSuccessful());

		if (Result.IsSuccessful() && Result.Path.IsValid())
		{
			TestFalse(FString::Printf(TEXT("%s: and it is not a partial path"),
					  CataclysmFloorLayoutName(Layout)), Result.Path->IsPartial());
		}

		TearDown(Setup);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorRockIsNotWalkableTest,
	"Cataclysm.DungeonFloor.TheRockBetweenTheRoomsIsNotWalkable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFloorRockIsNotWalkableTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonNavTest;

	// THE CONTROL FOR THE TWO TESTS ABOVE. Both would pass on a navigation mesh
	// that covered the whole grid regardless of what was carved: every cell would
	// project onto it and every path would be found, on a floor with no walls at
	// all. This asks the opposite question.
	//
	// IT SAMPLES BURIED ROCK -- a solid cell with no walkable cell within two
	// cells of it. Rock that faces the floor is right beside walkable surface,
	// and asking whether a point four metres from the navigation mesh is on it is
	// a question about the search extent rather than about the floor.
	FNavigableFloor Setup = Build(3, ECataclysmFloorLayout::Halls);
	if (!TestTrue(FString::Printf(TEXT("a navigable floor was set up: %s"),
				  *Setup.Trouble), Setup.IsReady()))
	{
		TearDown(Setup);
		return false;
	}

	const FCataclysmFloorPlan& Plan = Setup.Floor->GetPlan();

	// FIRST, THAT THERE IS A NAVIGATION MESH AT ALL, because without this line
	// the rest of this test passes when everything is broken. Measured: with the
	// ground made invisible to navigation, the two tests above fail and this one
	// passed, because "no buried rock is on the navigation mesh" is true when
	// there is no navigation mesh. A control that survives the fault it is a
	// control for is not one.
	FNavLocation Where;
	if (!TestTrue(TEXT("there is a navigation mesh to be off, so this test can "
					   "mean something"),
		Setup.Navigation->ProjectPointToNavigation(
			Setup.Floor->EntranceWorld(), Where, CloseEnough)))
	{
		TearDown(Setup);
		return false;
	}

	int32 Buried = 0;
	int32 WronglyWalkable = 0;

	for (int32 Index = 0; Index < Plan.Cells.Num(); ++Index)
	{
		const FIntPoint Cell = Plan.CellAt(Index);
		if (Plan.IsFloor(Cell))
		{
			continue;
		}

		bool bNearTheFloor = false;
		for (int32 OffsetY = -2; OffsetY <= 2 && !bNearTheFloor; ++OffsetY)
		{
			for (int32 OffsetX = -2; OffsetX <= 2 && !bNearTheFloor; ++OffsetX)
			{
				bNearTheFloor = Plan.IsFloor(Cell + FIntPoint(OffsetX, OffsetY));
			}
		}
		if (bNearTheFloor)
		{
			continue;
		}

		++Buried;
		FNavLocation Landed;
		if (Setup.Navigation->ProjectPointToNavigation(
				Setup.Floor->WorldOfCell(Cell), Landed, CloseEnough))
		{
			++WronglyWalkable;
		}
	}

	// The sample has to be a real one, or the check below passes on nothing.
	TestTrue(FString::Printf(TEXT("the floor has buried rock to sample: %d cells"),
			 Buried), Buried > 50);

	TestEqual(FString::Printf(
		TEXT("no buried rock is on the navigation mesh; %d of %d cells are"),
		WronglyWalkable, Buried), WronglyWalkable, 0);

	TearDown(Setup);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
