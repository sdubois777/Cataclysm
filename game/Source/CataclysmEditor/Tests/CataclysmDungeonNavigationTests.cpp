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
#include "AI/NavDataGenerator.h"
#include <atomic>

#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/IQueuedWork.h"
#include "Misc/QueuedThreadPool.h"
#include "Dungeon/CataclysmFloorObstacle.h"

// For a clicked walk on a real navigation mesh. Issues #1174 and #793.
#include "AIController.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"

// The crowd on a navigable floor. Issue #761. (AIController.h is above.)
#include "Character/CataclysmImpCharacter.h"
#include "Engine/World.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "Navigation/CrowdManager.h"
#include "UObject/UnrealType.h"

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

	/**
	 * `ChangeThePlan` is given the generated plan before the floor is built from it, and before any navigation
	 * system exists. A test that needs a floor the generator does not make by itself, a carved shortcut, changes the
	 * plan there, which is where the game mode changes it.
	 */
	FNavigableFloor Build(int32 Seed, ECataclysmFloorLayout Layout,
						  TFunctionRef<void(FCataclysmFloorPlan&)> ChangeThePlan = [](FCataclysmFloorPlan&) {})
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
		FCataclysmFloorPlan Plan = FCataclysmFloorGenerator::Generate(Request);
		ChangeThePlan(Plan);
		if (!Out.Floor->Build(Plan))
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

// ---------------------------------------------------------------------------
// The runtime floor obstacle on a real navigation mesh. Issues #1820 and #41. Method C as measured on 2026-10-02: the
// obstacle's NavArea_Null modifier, not its geometry, takes its cells off the mesh.
// ---------------------------------------------------------------------------

namespace CataclysmDungeonNavTest
{
	/** Whether the navigation system has nothing left to build. */
	bool TheNavigationMeshIsBuilt(FNavigableFloor& Setup)
	{
		const FNavDataGenerator* Generator = Setup.NavData->GetGenerator();
		return !Setup.Navigation->IsNavigationBuildInProgress()
			&& (!Generator || Generator->GetNumRemaningBuildTasks() == 0);
	}

	/**
	 * THE WAIT THESE TESTS USED UNTIL ISSUE #2222, kept so one test can show what was wrong with it: tick the
	 * navigation system until it is not building, at most `MostTicks`; whether it finished.
	 *
	 * A COUNT OF TICKS IS NOT AN AMOUNT OF TIME. The mesh's tiles are rebuilt by tasks on the engine's worker threads
	 * (`FRecastNavMeshGenerator` starts each with `StartBackgroundTask`), and a tick only collects the ones that
	 * have finished. This loop does not sleep, so its 600 ticks last as long as 600 calls take, and it fails
	 * whenever no worker finishes the tile in that time.
	 */
	bool TickOnlyForTheNavigationMesh(FNavigableFloor& Setup, int32 MostTicks = 600)
	{
		for (int32 Tick = 0; Tick < MostTicks; ++Tick)
		{
			Setup.Navigation->Tick(1.0f / 60.0f);
			if (Tick >= 2 && TheNavigationMeshIsBuilt(Setup))
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Tick the navigation system and BLOCK ON ITS TILE BUILDS until it is not building; whether it finished.
	 *
	 * `EnsureBuildCompletion` IS WHAT THE FIRST BUILD OF EVERY ONE OF THESE FLOORS ALREADY USES, through
	 * `UNavigationSystemV1::Build`. It waits for each running tile task, and runs one itself when no worker has
	 * started it, so it does not depend on a worker thread being free. Issue #2222.
	 *
	 * THE LIMIT IS SECONDS ON THE CLOCK, for the reason the tick-only wait above gives. WHEN IT DOES NOT FINISH IT
	 * SAYS WHAT IT SAW, so the next failure names its cause: the ticks run, the seconds, the tasks left, and whether
	 * a build was still in progress.
	 */
	bool WaitForTheNavigationMesh(FNavigableFloor& Setup, double MostSeconds = 60.0)
	{
		const double Began = FPlatformTime::Seconds();
		int32 Tick = 0;
		for (;; ++Tick)
		{
			Setup.Navigation->Tick(1.0f / 60.0f);
			Setup.NavData->EnsureBuildCompletion();
			if (Tick >= 2 && TheNavigationMeshIsBuilt(Setup))
			{
				// HOW LONG IT TOOK, EVERY TIME, so a run's log says whether the limit is generous or tight.
				UE_LOG(LogTemp, Display, TEXT("Navigation wait finished: %d ticks in %.4f s. Issue #2222."), Tick + 1,
					FPlatformTime::Seconds() - Began);
				return true;
			}
			if (FPlatformTime::Seconds() - Began > MostSeconds)
			{
				break;
			}
		}
		const FNavDataGenerator* Generator = Setup.NavData->GetGenerator();
		UE_LOG(LogTemp, Warning,
			TEXT("The navigation mesh did not finish building: %d ticks in %.3f s, %d build tasks left, build in "
				 "progress %d. Issue #2222."),
			Tick + 1, FPlatformTime::Seconds() - Began, Generator ? Generator->GetNumRemaningBuildTasks() : -1,
			Setup.Navigation->IsNavigationBuildInProgress() ? 1 : 0);
		return false;
	}

	/** One piece of work that holds a worker thread until it is let go. */
	class FHeldWorker : public IQueuedWork
	{
	public:
		FHeldWorker(FEvent* InLetGo, std::atomic<int32>* InStarted) : LetGo(InLetGo), Started(InStarted) {}

		virtual void DoThreadedWork() override
		{
			++(*Started);
			LetGo->Wait();
		}

		virtual void Abandon() override {}

	private:
		FEvent* LetGo;
		std::atomic<int32>* Started;
	};

	/**
	 * Every thread of the engine's pool held, until this goes out of scope. It is how the fault of issue #2222 is
	 * made to happen every time: with no worker free, a tile task that is started in the background is not begun.
	 */
	struct FEveryWorkerHeld
	{
		FEveryWorkerHeld()
		{
			LetGo = FPlatformProcess::GetSynchEventFromPool(/*bIsManualReset=*/true);
			const int32 Threads = GThreadPool ? GThreadPool->GetNumThreads() : 0;
			for (int32 Index = 0; Index < Threads; ++Index)
			{
				Held.Add(new FHeldWorker(LetGo, &Started));
				GThreadPool->AddQueuedWork(Held.Last());
			}
			// UNTIL EACH HAS BEGUN, at most five seconds: work that is only queued holds nothing.
			const double Began = FPlatformTime::Seconds();
			while (Started.load() < Threads && FPlatformTime::Seconds() - Began < 5.0)
			{
				FPlatformProcess::Sleep(0.001f);
			}
			bEveryOneHeld = Threads > 0 && Started.load() == Threads;
		}

		~FEveryWorkerHeld()
		{
			// WORK NO THREAD BEGAN IS TAKEN BACK, and the rest is let go and waited for, so nothing touches the
			// counter or the event after they are gone.
			int32 TakenBack = 0;
			for (FHeldWorker* Work : Held)
			{
				if (GThreadPool->RetractQueuedWork(Work))
				{
					++TakenBack;
				}
			}
			const int32 Begun = Held.Num() - TakenBack;
			const double Began = FPlatformTime::Seconds();
			while (Started.load() < Begun && FPlatformTime::Seconds() - Began < 5.0)
			{
				FPlatformProcess::Sleep(0.001f);
			}
			LetGo->Trigger();
			// A MOMENT FOR EACH TO LEAVE `Wait` before the work is deleted; the event goes back to its pool.
			FPlatformProcess::Sleep(0.05f);
			for (FHeldWorker* Work : Held)
			{
				delete Work;
			}
			FPlatformProcess::ReturnSynchEventToPool(LetGo);
		}

		FEvent* LetGo = nullptr;
		std::atomic<int32> Started{0};
		TArray<FHeldWorker*> Held;
		bool bEveryOneHeld = false;
	};

	/** A floor cell with every cell of a Side + 2 square around it walkable, or (-1, -1). */
	FIntPoint OpenSquareCorner(const FCataclysmFloorPlan& Plan, int32 Side)
	{
		for (int32 Index = 0; Index < Plan.Cells.Num(); ++Index)
		{
			const FIntPoint Corner = Plan.CellAt(Index);
			bool bOpen = true;
			for (int32 Y = -1; Y <= Side && bOpen; ++Y)
			{
				for (int32 X = -1; X <= Side && bOpen; ++X)
				{
					const FIntPoint Cell = Corner + FIntPoint(X, Y);
					bOpen = Plan.IsFloor(Cell) && Cell != Plan.Entrance && Cell != Plan.Exit;
				}
			}
			if (bOpen)
			{
				return Corner;
			}
		}
		return FIntPoint(-1, -1);
	}

	/** The length of a whole path between two points, and how close it comes to `Near`; -1 when there is none. */
	double PathAround(FNavigableFloor& Setup, const FVector& From, const FVector& To, const FVector& Near,
					  double& ClosestCm)
	{
		FPathFindingQuery Query(nullptr, *Setup.NavData, From, To);
		Query.SetAllowPartialPaths(false);
		const FPathFindingResult Result = Setup.Navigation->FindPathSync(Query);
		ClosestCm = -1.0;
		if (!Result.IsSuccessful() || !Result.Path.IsValid() || Result.Path->IsPartial())
		{
			return -1.0;
		}
		const TArray<FNavPathPoint>& Points = Result.Path->GetPathPoints();
		ClosestCm = 1.0e9;
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			const FVector A(Points[Index - 1].Location.X, Points[Index - 1].Location.Y, 0.0);
			const FVector B(Points[Index].Location.X, Points[Index].Location.Y, 0.0);
			ClosestCm = FMath::Min(ClosestCm, FMath::PointDistToSegment(FVector(Near.X, Near.Y, 0.0), A, B));
		}
		return Result.Path->GetLength();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmObstacleNavigationTest,
	"Cataclysm.DungeonFloor.ARuntimeObstacleTakesItsCellsOffTheNavigationMeshAndAPathGoesRound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmObstacleNavigationTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonNavTest;

	for (const ECataclysmObstacleKind Kind : {ECataclysmObstacleKind::Pillar, ECataclysmObstacleKind::Pit})
	{
		const int32 Side = Kind == ECataclysmObstacleKind::Pillar ? 1 : 2;
		const TCHAR* Name = Kind == ECataclysmObstacleKind::Pillar ? TEXT("pillar") : TEXT("pit");
		FNavigableFloor Setup = Build(3, ECataclysmFloorLayout::Halls);
		if (!TestTrue(FString::Printf(TEXT("a navigable floor was set up: %s"), *Setup.Trouble), Setup.IsReady()))
		{
			TearDown(Setup);
			return false;
		}
		const FIntPoint Corner = OpenSquareCorner(Setup.Floor->GetPlan(), Side);
		if (!TestTrue(FString::Printf(TEXT("set-up: an open square for a %s"), Name), Corner.X >= 0))
		{
			TearDown(Setup);
			return false;
		}
		TArray<FIntPoint> Cells;
		for (int32 Y = 0; Y < Side; ++Y)
		{
			for (int32 X = 0; X < Side; ++X)
			{
				Cells.Add(Corner + FIntPoint(X, Y));
			}
		}
		const FVector West = Setup.Floor->WorldOfCell(Corner - FIntPoint(1, 0));
		const FVector East = Setup.Floor->WorldOfCell(Corner + FIntPoint(Side, 0));
		FVector Middle = FVector::ZeroVector;
		for (const FIntPoint& Cell : Cells)
		{
			Middle += Setup.Floor->WorldOfCell(Cell);
		}
		Middle /= static_cast<double>(Cells.Num());
		double ClosestBefore = 0.0;
		const double Before = PathAround(Setup, West, East, Middle, ClosestBefore);

		ACataclysmFloorObstacle* Obstacle =
			ACataclysmFloorObstacle::Place(Setup.World, *Setup.Floor, Cells, Kind, NAME_None, NAME_None);
		if (!TestNotNull(FString::Printf(TEXT("a %s was placed"), Name), Obstacle))
		{
			TearDown(Setup);
			return false;
		}
		Obstacle->Raise();
		TestTrue(FString::Printf(TEXT("the %s's mesh rebuild finished"), Name), WaitForTheNavigationMesh(Setup));

		int32 StillOnTheMesh = 0;
		for (const FIntPoint& Cell : Cells)
		{
			FNavLocation Landed;
			StillOnTheMesh += Setup.Navigation->ProjectPointToNavigation(Setup.Floor->WorldOfCell(Cell), Landed,
																		  CloseEnough) ? 1 : 0;
		}
		TestEqual(FString::Printf(TEXT("no cell under the %s is on the navigation mesh"), Name), StillOnTheMesh, 0);

		double ClosestAfter = 0.0;
		const double After = PathAround(Setup, West, East, Middle, ClosestAfter);
		TestTrue(FString::Printf(TEXT("a %s: a path still goes from one side to the other"), Name), After > 0.0);
		TestTrue(FString::Printf(TEXT("a %s: and it is longer, %.0f cm against %.0f"), Name, After, Before),
				 After > Before + 100.0);
		TestTrue(FString::Printf(TEXT("a %s: and keeps %.0f cm from its middle, more than half its width"), Name,
								 ClosestAfter),
				 ClosestAfter > Obstacle->HalfWidthCm());
		TearDown(Setup);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmObstacleRemovedTest,
	"Cataclysm.DungeonFloor.ARemovedObstacleGivesItsCellsBackToTheNavigationMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmObstacleRemovedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonNavTest;

	FNavigableFloor Setup = Build(3, ECataclysmFloorLayout::Halls);
	if (!TestTrue(FString::Printf(TEXT("a navigable floor was set up: %s"), *Setup.Trouble), Setup.IsReady()))
	{
		TearDown(Setup);
		return false;
	}
	const FIntPoint Cell = OpenSquareCorner(Setup.Floor->GetPlan(), 1);
	if (!TestTrue(TEXT("set-up: an open cell"), Cell.X >= 0))
	{
		TearDown(Setup);
		return false;
	}
	ACataclysmFloorObstacle* Obstacle = ACataclysmFloorObstacle::Place(
		Setup.World, *Setup.Floor, {Cell}, ECataclysmObstacleKind::Pillar, NAME_None, NAME_None);
	if (!TestNotNull(TEXT("a pillar was placed"), Obstacle))
	{
		TearDown(Setup);
		return false;
	}
	Obstacle->Raise();
	WaitForTheNavigationMesh(Setup);
	FNavLocation Landed;
	TestFalse(TEXT("set-up: the cell is off the mesh"),
			  Setup.Navigation->ProjectPointToNavigation(Setup.Floor->WorldOfCell(Cell), Landed, CloseEnough));

	Obstacle->Destroy();
	TestTrue(TEXT("the rebuild after removing it finished"), WaitForTheNavigationMesh(Setup));
	TestTrue(TEXT("the cell is on the navigation mesh again"),
			 Setup.Navigation->ProjectPointToNavigation(Setup.Floor->WorldOfCell(Cell), Landed, CloseEnough));
	TearDown(Setup);
	return true;
}

// ---------------------------------------------------------------------------
// The wait itself. Issue #2222: four whole-suite runs in two days failed one of the tests above at "the mesh rebuild
// finished", and every one passed when run again alone.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNavigationWaitTest,
	"Cataclysm.DungeonFloor.TheNavigationWaitFinishesWhenNoWorkerThreadIsFree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmNavigationWaitTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonNavTest;

	FNavigableFloor Setup = Build(3, ECataclysmFloorLayout::Halls);
	if (!TestTrue(FString::Printf(TEXT("a navigable floor was set up: %s"), *Setup.Trouble), Setup.IsReady()))
	{
		TearDown(Setup);
		return false;
	}
	const FIntPoint Cell = OpenSquareCorner(Setup.Floor->GetPlan(), 1);
	if (!TestTrue(TEXT("set-up: an open cell"), Cell.X >= 0))
	{
		TearDown(Setup);
		return false;
	}
	FNavLocation Landed;
	const FVector Where = Setup.Floor->WorldOfCell(Cell);
	if (!TestTrue(TEXT("set-up: the cell is on the mesh before anything stands on it"),
				  Setup.Navigation->ProjectPointToNavigation(Where, Landed, CloseEnough)))
	{
		TearDown(Setup);
		return false;
	}

	{
		// EVERY WORKER IS BUSY, which is what a whole-suite run can do to these tests and a run of them alone does
		// not.
		FEveryWorkerHeld Workers;
		if (!TestTrue(TEXT("set-up: every worker thread is held"), Workers.bEveryOneHeld))
		{
			TearDown(Setup);
			return false;
		}
		ACataclysmFloorObstacle* Obstacle = ACataclysmFloorObstacle::Place(
			Setup.World, *Setup.Floor, {Cell}, ECataclysmObstacleKind::Pillar, NAME_None, NAME_None);
		if (!TestNotNull(TEXT("a pillar was placed"), Obstacle))
		{
			TearDown(Setup);
			return false;
		}
		Obstacle->Raise();

		// THE FAULT: 600 ticks pass and the tile is not rebuilt, because nothing was free to rebuild it.
		TestFalse(TEXT("control: counting 600 ticks does not finish the rebuild while no worker is free"),
				  TickOnlyForTheNavigationMesh(Setup));

		// THE WAIT: it finishes anyway, and the mesh is the changed one.
		TestTrue(TEXT("the wait finishes the rebuild while no worker is free"), WaitForTheNavigationMesh(Setup));
		TestFalse(TEXT("and the cell under the pillar is off the mesh"),
				  Setup.Navigation->ProjectPointToNavigation(Where, Landed, CloseEnough));
	}
	TearDown(Setup);
	return true;
}

// ---------------------------------------------------------------------------
// A gated shortcut on a real navigation mesh. Issues #1820 and #41.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShortcutNavigationTest,
	"Cataclysm.DungeonFloor.AShortcutsGateClosesItOnTheNavigationMeshAndOpeningItShortensThePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmShortcutNavigationTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonNavTest;

	// A SHORTCUT CARVED INTO THE PLAN BEFORE THE FLOOR IS BUILT FROM IT, which is the order the game mode uses for a
	// floor carrying a gate row.
	//
	// NOT BUILT A SECOND TIME. This test first built the plain floor, then carved, then built the same floor actor
	// again once the navigation system existed. On its first run, 2026-10-05, both waits for the mesh below answered
	// false while the path assertions passed, so those may have read a mesh that was not finished. The cause was not
	// proven; the second build is what no passing test beside this one does, and it is gone.
	FCataclysmFloorShortcut Shortcut;
	int32 ShortcutsFound = 0;
	FNavigableFloor Setup = Build(3, ECataclysmFloorLayout::Halls, [&Shortcut, &ShortcutsFound](FCataclysmFloorPlan& Plan)
	{
		FRandomStream Stream(3);
		const TArray<FCataclysmFloorShortcut> Found =
			FCataclysmFloorGenerator::FindShortcuts(Plan, Stream, 1, TSet<FIntPoint>());
		ShortcutsFound = Found.Num();
		if (ShortcutsFound == 1)
		{
			Shortcut = Found[0];
			FCataclysmFloorGenerator::CarveShortcut(Plan, Shortcut);
		}
	});
	if (!TestTrue(FString::Printf(TEXT("a navigable floor was set up: %s"), *Setup.Trouble), Setup.IsReady())
		|| !TestEqual(TEXT("set-up: a shortcut on this floor"), ShortcutsFound, 1))
	{
		TearDown(Setup);
		return false;
	}

	// THE GATE: two pillars, as `CloseTheGate` places them.
	TArray<ACataclysmFloorObstacle*> Pillars;
	for (const FIntPoint& Cell : Shortcut.Gate)
	{
		ACataclysmFloorObstacle* Pillar = ACataclysmFloorObstacle::Place(
			Setup.World, *Setup.Floor, {Cell}, ECataclysmObstacleKind::Pillar, NAME_None, NAME_None);
		if (TestNotNull(TEXT("a gate pillar was placed"), Pillar))
		{
			Pillar->Raise();
			Pillars.Add(Pillar);
		}
	}
	TestTrue(TEXT("the mesh rebuilt with the gate shut"), WaitForTheNavigationMesh(Setup));

	const FVector From = Setup.Floor->WorldOfCell(Shortcut.A);
	const FVector To = Setup.Floor->WorldOfCell(Shortcut.B);
	const FVector Gate = (Setup.Floor->WorldOfCell(Shortcut.Gate[0]) + Setup.Floor->WorldOfCell(Shortcut.Gate[1])) * 0.5;
	double Closest = 0.0;
	const double Shut = PathAround(Setup, From, To, Gate, Closest);
	TestTrue(TEXT("with the gate shut a path still joins the two ends, the long way"), Shut > 0.0);

	for (ACataclysmFloorObstacle* Pillar : Pillars)
	{
		Pillar->Destroy();
	}
	TestTrue(TEXT("the mesh rebuilt with the gate open"), WaitForTheNavigationMesh(Setup));
	const double Open = PathAround(Setup, From, To, Gate, Closest);
	TestTrue(TEXT("with the gate open a path joins them"), Open > 0.0);

	// AT LEAST HALF THE CELLS IT SAVES ON THE GRID, in centimetres: a navigation path cuts corners a cell walk cannot.
	const double Least = Shortcut.Saving * 0.5 * FCataclysmFloorGenerator::CellSizeCm;
	TestTrue(FString::Printf(TEXT("and it is shorter: %.0f cm against %.0f, by at least %.0f"), Open, Shut, Least),
			 Shut - Open >= Least);
	TearDown(Setup);
	return true;
}

// ---------------------------------------------------------------------------
// A clicked walk on a real navigation mesh. Issues #1174 and #793.
//
// THE ONE TEST THAT DRIVES CLICK-TO-MOVE. Issue #793 records that nothing did: a click goes through
// `UAIBlueprintHelperLibrary::SimpleMoveToLocation`, which needs a navigation mesh, and only this module can build one
// (see the comment at the top of this file). Issue #1174 is what the walk looked like once it worked: the character
// slid to the point playing its idle pose, because the path follower set its velocity and left its acceleration at
// zero, and `ABP_Unarmed` plays its run only while the acceleration is not zero.
//
// STEPPED BY HAND. A test world is never ticked, so each frame calls the path follower and then the movement
// component, the order a game frame runs them in (the controller, which owns the path follower, ticks before the pawn
// it possesses). A thirtieth of a second a frame.
// ---------------------------------------------------------------------------

namespace CataclysmDungeonNavTest
{
	/** What one clicked walk did. */
	struct FClickedWalkReading
	{
		/** Whether the order started a walk at all. */
		bool bOrdered = false;

		/**
		 * Frames the path follower drove -- it was still following the path after its own tick -- on which the
		 * character was on the ground at more than `MovingCmPerSecond`, flat.
		 *
		 * ONLY FRAMES THE FOLLOWER DROVE, AND THE FIRST WRITING MISSED IT. On the frame a walk driven by acceleration
		 * finishes, the follower gives no input (`OnPathFinished` does not stop a walk driven by acceleration:
		 * `ShouldStopMovementOnPathFinished` is false for it), so the movement component has no acceleration while
		 * the character, still braking from a walking pace, moves at well over `MovingCmPerSecond`. Counted, that
		 * frame alone failed the assertion with the fix in place. It is the brake a released key gives, not the
		 * slide, and the slide is what is asked about. Found by reading `UPathFollowingComponent::OnPathFinished`
		 * on 2026-11-02, before any run.
		 */
		int32 MovingFrames = 0;

		/** Of those, the frames on which the movement component's acceleration was zero: what plays the idle pose. */
		int32 MovingWithNoAcceleration = 0;

		/** The most the character's facing turned away from the way it was stood, in degrees, over the walk. */
		double MostTurnedDegrees = 0.0;

		/** Whether the path follower had finished when the stepping stopped. */
		bool bFinished = false;

		/** Where the character stood at the end. */
		FVector EndedAt = FVector::ZeroVector;
	};

	/** One frame of the walk, in seconds. */
	constexpr float ClickedWalkStepSeconds = 1.0f / 30.0f;

	/** Slower than this, flat, is not counted as moving: the first frame of a start and the last of a stop. */
	constexpr double MovingCmPerSecond = 50.0;

	/**
	 * How near the stairs the walk to them has to end, flat.
	 *
	 * WHERE IT IS EXPECTED TO END, READ AND NOT RUN: about half a metre short. The follower calls a walk finished
	 * within its acceptance radius, a tenth of the agent's radius, plus 1.1 times that radius
	 * (`UPathFollowingComponent::MinAgentRadiusPct`), about 50 cm for the player's 42 cm capsule; the last frame
	 * carries it a little nearer as it brakes. The stairs' cell middle is on the navigation mesh (the halls floor's
	 * cells are four metres wide), so the path ends on the stairs. A metre and a half leaves room for that and
	 * still fails a walk that stopped in the wrong room or never set off.
	 */
	constexpr double ArrivedWithinCm = 150.0;

	/** How far a clicked walk must turn the character, which was stood facing directly away from its first leg. */
	constexpr double TurnedAtLeastDegrees = 90.0;

	/**
	 * Stand the character at `Start` facing `Yaw`, order a walk to `Goal` the way a click does, and step it for at
	 * most `MostFrames` frames or until the walk finishes.
	 *
	 * THE ORDER IS THE GAME'S OWN CALL, `SimpleMoveToLocation`, which is what
	 * `ACataclysmPlayerController::Input_MoveToCursorReleased` calls for a click on the floor.
	 */
	FClickedWalkReading WalkAClickedPath(AAIController& Controller, ACataclysmPlayerCharacter& Character,
										 const FVector& Start, float Yaw, const FVector& Goal, int32 MostFrames)
	{
		FClickedWalkReading Reading;
		UCharacterMovementComponent* Movement = Character.GetCharacterMovement();
		UPathFollowingComponent* Follower = Controller.GetPathFollowingComponent();
		UCapsuleComponent* Capsule = Character.GetCapsuleComponent();
		UWorld* World = Character.GetWorld();
		if (!Movement || !Follower || !Capsule || !World)
		{
			return Reading;
		}

		// STOOD ON THE GROUND, whose top is the floor actor's height (`ACataclysmDungeonFloor::GroundThicknessCm`),
		// two centimetres up so the capsule starts clear of it.
		Controller.StopMovement();
		const FRotator Stood(0.0, Yaw, 0.0);
		Character.SetActorLocationAndRotation(
			Start + FVector(0.0, 0.0, Capsule->GetScaledCapsuleHalfHeight() + 2.0), Stood, /*bSweep=*/false, nullptr,
			ETeleportType::TeleportPhysics);
		Movement->StopMovementImmediately();
		const FVector StoodFacing = Stood.Vector().GetSafeNormal2D();

		UAIBlueprintHelperLibrary::SimpleMoveToLocation(&Controller, Goal);
		Reading.bOrdered = Follower->GetStatus() == EPathFollowingStatus::Moving;
		if (!Reading.bOrdered)
		{
			return Reading;
		}

		for (int32 Frame = 0; Frame < MostFrames; ++Frame)
		{
			// THE WORLD'S CLOCK TOO, because the path follower's block detection samples by it.
			World->TimeSeconds += ClickedWalkStepSeconds;
			Follower->TickComponent(ClickedWalkStepSeconds, LEVELTICK_All, nullptr);
			const bool bDriven = Follower->GetStatus() == EPathFollowingStatus::Moving;
			Movement->TickComponent(ClickedWalkStepSeconds, LEVELTICK_All, nullptr);

			const FVector Travel(Movement->Velocity.X, Movement->Velocity.Y, 0.0);
			if (bDriven && Movement->IsMovingOnGround() && Travel.Size() > MovingCmPerSecond)
			{
				++Reading.MovingFrames;
				if (Movement->GetCurrentAcceleration().IsNearlyZero())
				{
					++Reading.MovingWithNoAcceleration;
				}
			}

			const FVector Facing = Character.GetActorForwardVector().GetSafeNormal2D();
			const double Cosine = FMath::Clamp(FVector::DotProduct(Facing, StoodFacing), -1.0, 1.0);
			Reading.MostTurnedDegrees = FMath::Max(Reading.MostTurnedDegrees, FMath::RadiansToDegrees(FMath::Acos(Cosine)));

			if (Follower->GetStatus() == EPathFollowingStatus::Idle)
			{
				Reading.bFinished = true;
				break;
			}
		}

		Reading.EndedAt = Character.GetActorLocation();
		return Reading;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmClickedWalkRunsTest,
	"Cataclysm.ClickToMove.AClickedWalkMovesTheCharacterByAccelerationSoItRunsAndStillTurnsAndArrives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmClickedWalkRunsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonNavTest;

	// THE FLOOR MOST TESTS IN THIS FILE STAND ON, seed 3 in halls. That a path joins its arrival point to its stairs
	// is asserted below as set-up: `ACharacterCanPathFromTheArrivalPointToTheStairs` walks seeds 21 onward, not this
	// one, which the first writing of this comment claimed.
	FNavigableFloor Setup = Build(3, ECataclysmFloorLayout::Halls);
	if (!TestTrue(FString::Printf(TEXT("a navigable floor was set up: %s"), *Setup.Trouble), Setup.IsReady()))
	{
		TearDown(Setup);
		return false;
	}

	// THE WALK A CLICK ON THE STAIRS WOULD ORDER, asked first so the character can be stood facing directly away
	// from its first leg. Turning to face it is then a real turn, and a character that never turned would be caught.
	const FVector Start = Setup.Floor->EntranceWorld();
	const FVector Goal = Setup.Floor->ExitWorld();
	FPathFindingQuery Query(nullptr, *Setup.NavData, Start, Goal);
	Query.SetAllowPartialPaths(false);
	const FPathFindingResult Path = Setup.Navigation->FindPathSync(Query);
	if (!TestTrue(TEXT("set-up: a path joins the arrival point to the stairs"),
				  Path.IsSuccessful() && Path.Path.IsValid() && Path.Path->GetPathPoints().Num() >= 2))
	{
		TearDown(Setup);
		return false;
	}
	const TArray<FNavPathPoint>& Points = Path.Path->GetPathPoints();
	const FVector FirstLeg(Points[1].Location.X - Points[0].Location.X, Points[1].Location.Y - Points[0].Location.Y,
						   0.0);
	const float AwayYaw = static_cast<float>((-FirstLeg).Rotation().Yaw);
	const double PathCm = Path.Path->GetLength();

	// THE PLAYER CHARACTER, POSSESSED BY AN AI CONTROLLER AND NOT BY THE PLAYER'S OWN, AND THAT IS FORCED.
	// `APlayerController::IsLocalController` answers false in a world with no net driver to a controller with no
	// local player, which every test world's is, and `UCharacterMovementComponent::TickComponent` performs a move
	// only for a character that is locally controlled -- so under `ACataclysmPlayerController` the character would
	// stand still and every assertion below would be about nothing. `AController::IsLocalController` answers true in
	// a world with no network. Read in this engine's source on 2026-11-02, not run.
	//
	// IT CHANGES NOTHING ASKED HERE. The setting under test is on the player character's movement component, and the
	// path follower asks that component, whichever controller owns the follower. The controller's own frame,
	// `ACataclysmPlayerController::PostProcessInput`, is reached by no automation test either way.
	//
	// NO PLAYER STATE, so the character walks at the speed its constructor writes,
	// `ACataclysmPlayerCharacter::DefaultWalkSpeedCmPerSecond`: `InitAbilityActorInfo` returns at once without one.
	AAIController* Controller = Setup.World->SpawnActor<AAIController>();
	ACataclysmPlayerCharacter* Character = Setup.World->SpawnActor<ACataclysmPlayerCharacter>(
		Start + FVector(0.0, 0.0, 200.0), FRotator(0.0, AwayYaw, 0.0));
	if (!TestTrue(TEXT("set-up: a controller and the player character were spawned"), Controller && Character))
	{
		TearDown(Setup);
		return false;
	}
	Controller->Possess(Character);

	// AT MOST TWICE THE TIME THE PATH TAKES AT 200 CM A SECOND, half a walking pace, and three seconds more.
	const int32 MostFrames = FMath::Clamp(
		FMath::CeilToInt32((PathCm / 200.0 + 3.0) / ClickedWalkStepSeconds), 90, 9000);

	const FClickedWalkReading Clicked = WalkAClickedPath(*Controller, *Character, Start, AwayYaw, Goal, MostFrames);
	AddInfo(FString::Printf(
		TEXT("Clicked walk: path %.0f cm, %d moving frames, %d of them with no acceleration, turned at most %.0f "
			 "degrees, finished %d, ended %.0f cm from the stairs. Issue #1174."),
		PathCm, Clicked.MovingFrames, Clicked.MovingWithNoAcceleration, Clicked.MostTurnedDegrees,
		Clicked.bFinished ? 1 : 0, FVector::Dist2D(Clicked.EndedAt, Goal)));

	if (!TestTrue(TEXT("set-up: the click ordered a walk"), Clicked.bOrdered)
		|| !TestTrue(TEXT("set-up: the character walked, for at least a second of frames"), Clicked.MovingFrames >= 30))
	{
		TearDown(Setup);
		return false;
	}

	// THE FAULT, ASKED THE WAY THE ANIMATION ASKS IT. `ABP_Unarmed` plays its run only while the acceleration is not
	// zero, so every moving frame with none is a frame of sliding.
	TestEqual(TEXT("a clicked walk: frames moving with no acceleration, which play the idle pose"),
			  Clicked.MovingWithNoAcceleration, 0);

	// AND THE TURN, WHICH WAS NEVER THE FAULT AND IS KEPT. Issue #1174 guessed that a clicked walk also failed to turn,
	// because `bOrientRotationToMovement` turns toward the acceleration. It does not stop there:
	// `UCharacterMovementComponent::ComputeOrientToMovementRotation` falls back to the path follower's requested
	// velocity when the acceleration is zero, so the character turned all along. The CONTROL below measures that.
	// Asserted here so that driving the walk by acceleration is seen not to have lost the turn.
	TestTrue(TEXT("a clicked walk turns the character, stood facing away, toward its path"),
			 Clicked.MostTurnedDegrees >= TurnedAtLeastDegrees);

	// AND IT GETS THERE, which is issue #793's question: nothing tested that a click moves the character at all.
	TestTrue(TEXT("the clicked walk to the stairs finishes"), Clicked.bFinished);
	TestTrue(TEXT("and ends within a metre and a half of them"),
			 FVector::Dist2D(Clicked.EndedAt, Goal) <= ArrivedWithinCm);

	// THE CONTROL: THE SAME WALK DRIVEN BY VELOCITY, the engine's default and the game before issue #1174. It has to
	// move with no acceleration on every moving frame, or the assertions above could not have failed for the reason
	// they exist: this is the measurement of the fault, not a reading of the engine.
	Character->GetCharacterMovement()->GetNavMovementProperties()->bUseAccelerationForPaths = false;
	const FClickedWalkReading Slid = WalkAClickedPath(*Controller, *Character, Start, AwayYaw, Goal, /*MostFrames=*/45);
	AddInfo(FString::Printf(TEXT("Control walk: %d moving frames, %d of them with no acceleration, turned at most %.0f "
								 "degrees."),
							Slid.MovingFrames, Slid.MovingWithNoAcceleration, Slid.MostTurnedDegrees));
	if (TestTrue(TEXT("CONTROL: the same walk driven by velocity moves too"), Slid.bOrdered && Slid.MovingFrames > 0))
	{
		TestEqual(TEXT("CONTROL: and every frame it moves, it moves with no acceleration"),
				  Slid.MovingWithNoAcceleration, Slid.MovingFrames);
		TestTrue(TEXT("CONTROL: and it turns toward its path all the same, so the turn was never the fault"),
				 Slid.MostTurnedDegrees >= TurnedAtLeastDegrees);
	}

	TearDown(Setup);
	return true;
}

// ---------------------------------------------------------------------------
// The Detour crowd on a real navigation mesh. Issue #761: every creature walks as a crowd agent. The tests in
// `CataclysmCrowdTests.cpp`, group `Cataclysm.Crowd.`, check the configuration in worlds with no navigation mesh;
// this is the one place a crowd actually holds creatures.
// ---------------------------------------------------------------------------

namespace CataclysmDungeonNavTest
{
	/**
	 * How many Imps are put on the floor. MORE THAN THE ENGINE'S OWN CROWD OF 50 (`UCrowdManager`'s constructor), so
	 * a crowd left at the engine's size holds fifty of them and this test says so; the project sets 1024 in
	 * `game/Config/DefaultEngine.ini`.
	 */
	constexpr int32 CrowdImps = 60;

	/** The crowd size ruled for issue #761, as `Cataclysm.Crowd.` copies it. */
	constexpr int32 RuledCrowdSize = 1024;

	/** The crowd-following component that walks this pawn, or null when its brain walks it any other way. */
	UCrowdFollowingComponent* CrowdFollowingOf(const APawn* Pawn)
	{
		const AAIController* Brain = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
		return Brain ? Cast<UCrowdFollowingComponent>(Brain->GetPathFollowingComponent()) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFloorCrowdHoldsSixtyImpsTest,
	"Cataclysm.FloorCrowd.SixtyImpsOnANavigableFloorAreEachHeldByTheCrowd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Sixty Imps put on a generated floor with a navigation mesh over it are each an agent the Detour crowd simulates,
 * each the radius of its own 30 cm body.
 *
 * WHAT THIS ADDS TO `Cataclysm.Crowd.` That a crowd manager on a real navigation mesh actually takes every creature
 * in: a slot in its Detour crowd is what `UCrowdManager::IsAgentValid` answers, and the manager moves only agents it
 * holds. It also shows `ACataclysmEnemyController`'s fallback, which takes a creature the crowd refused back out of
 * crowd mode, leaves alone every creature the crowd did take.
 *
 * WHAT IT DOES NOT DO: MOVE THEM. Nothing here ticks the world, so whether a pack rings the player is not measured.
 * See `docs/DECISIONS.md`.
 *
 * WHERE THEY STAND. An eight by eight grid, less four, 150 cm apart, inside an open square of two by two cells and the
 * ring of cells around it -- 16 metres of walkable floor -- which `OpenSquareCorner` finds on this seed for the pit
 * test above. 150 cm apart, each Imp's 60 cm body clears the next by 90.
 */
bool FCataclysmFloorCrowdHoldsSixtyImpsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonNavTest;

	FNavigableFloor Setup = Build(3, ECataclysmFloorLayout::Halls);
	if (!TestTrue(FString::Printf(TEXT("a navigable floor was set up: %s"), *Setup.Trouble), Setup.IsReady()))
	{
		TearDown(Setup);
		return false;
	}
	const FIntPoint Corner = OpenSquareCorner(Setup.Floor->GetPlan(), 2);
	UCrowdManager* Manager = UCrowdManager::GetCurrent(Setup.World);
	if (!TestTrue(TEXT("set-up: an open square for the Imps"), Corner.X >= 0)
		|| !TestNotNull(TEXT("set-up: the floor's navigation system has a crowd manager"), Manager))
	{
		TearDown(Setup);
		return false;
	}

	// THE LIVE MANAGER'S SIZE, read through reflection because the property is protected.
	const FIntProperty* Property = FindFProperty<FIntProperty>(UCrowdManager::StaticClass(), TEXT("MaxAgents"));
	if (TestNotNull(TEXT("set-up: the crowd manager has a whole-number property called MaxAgents"), Property))
	{
		TestEqual(TEXT("the crowd on the floor holds the ruled 1024 creatures"),
				  Property->GetPropertyValue_InContainer(Manager), RuledCrowdSize);
	}

	const FVector Origin = Setup.Floor->WorldOfCell(Corner);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	TArray<ACataclysmImpCharacter*> Imps;
	for (int32 Index = 0; Index < CrowdImps; ++Index)
	{
		const FVector Where = Origin + FVector(-300.0 + 150.0 * (Index % 8), -300.0 + 150.0 * (Index / 8),
											   ACataclysmImpCharacter::ImpCapsuleHalfHeight + 5.0);
		if (ACataclysmImpCharacter* Imp = Setup.World->SpawnActor<ACataclysmImpCharacter>(
				ACataclysmImpCharacter::StaticClass(), Where, FRotator::ZeroRotator, Params))
		{
			Imps.Add(Imp);
		}
	}
	if (!TestEqual(TEXT("set-up: sixty Imps spawned on the floor"), Imps.Num(), CrowdImps))
	{
		TearDown(Setup);
		return false;
	}

	int32 Held = 0;
	int32 RightSize = 0;
	for (const ACataclysmImpCharacter* Imp : Imps)
	{
		UCrowdFollowingComponent* Crowd = CrowdFollowingOf(Imp);
		if (!Crowd)
		{
			continue;
		}
		Held += Crowd->IsCrowdSimulationEnabled() && Manager->IsAgentValid(Crowd) ? 1 : 0;

		float Radius = 0.0f;
		float HalfHeight = 0.0f;
		Crowd->GetCrowdAgentCollisions(Radius, HalfHeight);
		RightSize += FMath::IsNearlyEqual(Radius, ACataclysmImpCharacter::ImpCapsuleRadius, 0.01f) ? 1 : 0;
	}

	TestEqual(TEXT("every one of sixty Imps is an agent the crowd simulates"), Held, CrowdImps);
	TestEqual(TEXT("and each one's crowd radius is its own 30 cm body"), RightSize, CrowdImps);
	TestNotNull(TEXT("and the crowd walks on the floor's navigation mesh"), Manager->GetNavData());

	TearDown(Setup);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
