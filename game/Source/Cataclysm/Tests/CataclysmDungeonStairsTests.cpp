// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Dungeon/CataclysmDungeonFloor.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmDungeonStairs.h"
#include "Dungeon/CataclysmFloorGenerator.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for the way down to the next floor, issue #40.
 *
 * WHAT THEY REACH AND WHAT THEY DO NOT. The stairs notice the player by
 * comparing two positions rather than by an overlap, which is what makes the
 * whole floor change reachable from a headless test: `ArriveAt` takes a position,
 * so a test can hand it one. The one line these cannot reach is
 * `LookForThePlayer` finding the pawn through `GetFirstPlayerController`, because
 * an automation test has no player controller and cannot be given one --
 * `Tests/CataclysmTestWorld.h` records why. That line is the whole of what is
 * untested here.
 */

namespace CataclysmStairsTest
{
	ACataclysmDungeonGameMode* SpawnMode(UWorld* World)
	{
		return World ? World->SpawnActor<ACataclysmDungeonGameMode>() : nullptr;
	}

	/** A dungeon game mode standing on a built, populated floor with stairs. */
	ACataclysmDungeonGameMode* SpawnModeOnAFloor(UWorld* World, float EnemyScale)
	{
		ACataclysmDungeonGameMode* Mode = SpawnMode(World);
		if (!Mode)
		{
			return nullptr;
		}

		// A THIN FLOOR UNLESS A TEST ASKS OTHERWISE. These tests are about the
		// stairs, and spawning three floors' worth of characters to prove one
		// floor replaced another is slower for no extra evidence.
		Mode->EnemyScale = EnemyScale;
		return Mode->GoToFloor(1) ? Mode : nullptr;
	}
}

// ---------------------------------------------------------------------------
// The marker itself
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStairsReachTest,
	"Cataclysm.DungeonStairs.TheyAreTakenByStandingOnThemAndNotFromAcrossTheRoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStairsReachTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStairsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonStairs* Stairs =
		World->SpawnActor<ACataclysmDungeonStairs>();
	if (!TestNotNull(TEXT("the stairs spawned"), Stairs))
	{
		return false;
	}

	const FVector Exit(1000.0f, 2000.0f, 50.0f);
	Stairs->PlaceAt(Exit);

	TestTrue(TEXT("standing on them is close enough"),
			 Stairs->IsWithinReach(Exit));

	// THE REACH IS HALF A CELL, so the next cell along is not close enough. A
	// reach that covered the neighbouring cells would take the player down a
	// floor as they walked past the stairs on their way somewhere else.
	const float Cell = FCataclysmFloorGenerator::CellSizeCm;
	TestFalse(FString::Printf(
		TEXT("the next cell along, %.0f cm away, is not"), Cell),
		Stairs->IsWithinReach(Exit + FVector(Cell, 0.0f, 0.0f)));

	TestTrue(TEXT("just inside the reach is close enough"),
			 Stairs->IsWithinReach(
				 Exit + FVector(ACataclysmDungeonStairs::ReachCm - 1.0f, 0.0f, 0.0f)));
	TestFalse(TEXT("and just outside it is not"),
			  Stairs->IsWithinReach(
				  Exit + FVector(ACataclysmDungeonStairs::ReachCm + 1.0f, 0.0f, 0.0f)));

	// HEIGHT IS IGNORED, because the marker is a platform the player may be
	// standing on top of. A distance that counted height would ask the player to
	// be at the marker's own height as well as its position, and standing on the
	// top step would then not count as arriving.
	TestTrue(FString::Printf(
		TEXT("standing on the top step, %.0f cm up, still counts"),
		ACataclysmDungeonStairs::TierCount * ACataclysmDungeonStairs::TierRiseCm),
		Stairs->IsWithinReach(Exit + FVector(0.0f, 0.0f,
			ACataclysmDungeonStairs::TierCount * ACataclysmDungeonStairs::TierRiseCm)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStairsShapeTest,
	"Cataclysm.DungeonStairs.TheyAreBuiltAsStepsAPlayerCanWalkUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStairsShapeTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStairsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonStairs* Stairs =
		World->SpawnActor<ACataclysmDungeonStairs>();
	if (!TestNotNull(TEXT("the stairs spawned"), Stairs))
	{
		return false;
	}

	TestEqual(TEXT("nothing is drawn before they are placed"),
			  Stairs->StepBlockCount(), 0);

	Stairs->PlaceAt(FVector(500.0f, 500.0f, 0.0f));
	TestEqual(TEXT("placing them builds one block per step"),
			  Stairs->StepBlockCount(), ACataclysmDungeonStairs::TierCount);

	// PLACING THEM AGAIN MOVES THEM RATHER THAN DRAWING A SECOND SET. Going down
	// a floor moves this actor, so a marker that added its blocks again would
	// leave a taller and taller pile after every flight.
	Stairs->PlaceAt(FVector(9000.0f, 9000.0f, 0.0f));
	TestEqual(TEXT("placing them a second time still draws one block per step"),
			  Stairs->StepBlockCount(), ACataclysmDungeonStairs::TierCount);
	TestTrue(TEXT("and they are where they were put"),
			 FVector::Dist(Stairs->GetActorLocation(),
						   FVector(9000.0f, 9000.0f, 0.0f)) < 1.0f);

	// EVERY STEP IS ONE A CHARACTER CAN WALK UP. Unreal's default
	// `MaxStepHeight` is 45 cm; a step taller than that would stop the player at
	// the bottom of a marker they are supposed to be able to stand on, and would
	// also be an obstacle the navigation mesh does not know about.
	constexpr float MostACharacterStepsOverCm = 45.0f;
	TestTrue(FString::Printf(
		TEXT("each step rises %.0f cm, which a character can walk up"),
		ACataclysmDungeonStairs::TierRiseCm),
		ACataclysmDungeonStairs::TierRiseCm < MostACharacterStepsOverCm);

	// AND THE MARKER FITS THE CELL IT MARKS. A marker wider than a cell would
	// stand in the cells beside the exit, which may be solid rock.
	TestTrue(FString::Printf(
		TEXT("the widest step is %.0f cm, within a cell's %.0f"),
		ACataclysmDungeonStairs::WidestTierCm,
		FCataclysmFloorGenerator::CellSizeCm),
		ACataclysmDungeonStairs::WidestTierCm
			<= FCataclysmFloorGenerator::CellSizeCm);

	// IT MUST NOT CHANGE THE NAVIGATION MESH. The floor's own tests measure what
	// can be walked on against the floor plan, and the exit is the one cell the
	// player has to be able to reach.
	if (const UInstancedStaticMeshComponent* Blocks = Stairs->Steps)
	{
		TestFalse(TEXT("the steps are invisible to navigation"),
				  Blocks->CanEverAffectNavigation());
	}

	return true;
}

// ---------------------------------------------------------------------------
// Taking them
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStairsAdvanceTest,
	"Cataclysm.DungeonStairs.WalkingIntoThemBuildsTheNextFloorAndMovesThePlayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStairsAdvanceTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStairsTest;

	// THE WHOLE POINT OF THE FEATURE, IN ONE TEST. Before this, `FloorNumber` was
	// a setting: a person could look at floor 7 by typing a console command and
	// there was no way to walk to it.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Mode = SpawnModeOnAFloor(World, /*EnemyScale=*/0.1f);
	if (!TestNotNull(TEXT("a game mode is standing on a floor"), Mode))
	{
		return false;
	}
	if (!TestNotNull(TEXT("and the stairs were placed on it"), Mode->Stairs.Get()))
	{
		return false;
	}

	TestEqual(TEXT("it starts on floor 1"), Mode->FloorNumber, 1);
	TestEqual(TEXT("having gone down no floors"), Mode->FloorsDescended, 0);

	// THE STAIRS STAND AT THE FLOOR'S EXIT, which is the cell the generator chose
	// and not a cell this test picked.
	const FVector FirstExit = Mode->CurrentFloor->ExitWorld();
	TestTrue(TEXT("the stairs stand at the floor's exit"),
			 FVector::Dist2D(Mode->Stairs->GetActorLocation(), FirstExit) < 1.0f);

	const TArray<ECataclysmFloorCell> FirstCells = Mode->CurrentFloor->GetPlan().Cells;

	// AND WALKING SOMEWHERE ELSE DOES NOTHING. Without this the test below passes
	// on stairs that fire wherever the player is, which would send the player
	// down a floor the moment play began.
	const FVector FarAway = FirstExit + FVector(50000.0f, 50000.0f, 0.0f);
	TestFalse(TEXT("standing across the floor does not take the stairs"),
			  Mode->Stairs->ArriveAt(FarAway));
	TestEqual(TEXT("so the floor is unchanged"), Mode->FloorNumber, 1);

	// WALKING INTO THEM DOES.
	TestTrue(TEXT("standing on them takes them"),
			 Mode->Stairs->ArriveAt(FirstExit));

	TestEqual(TEXT("the player is now on floor 2"), Mode->FloorNumber, 2);
	TestEqual(TEXT("having gone down one floor"), Mode->FloorsDescended, 1);

	if (!TestNotNull(TEXT("and there is a floor to stand on"),
					 Mode->CurrentFloor.Get()))
	{
		return false;
	}

	TestFalse(TEXT("it is a different floor"),
			  Mode->CurrentFloor->GetPlan().Cells == FirstCells);
	TestTrue(TEXT("and it is a floor with ground and stairs on it"),
			 Mode->CurrentFloor->IsBuilt());

	// THE STAIRS CAME WITH THE PLAYER, to the new floor's exit. A marker left at
	// the old floor's exit would be standing in whatever the new floor has there,
	// which is as likely to be solid rock as walkable ground.
	const FVector SecondExit = Mode->CurrentFloor->ExitWorld();
	TestTrue(TEXT("the stairs moved to the new floor's exit"),
			 FVector::Dist2D(Mode->Stairs->GetActorLocation(), SecondExit) < 1.0f);

	// AND THE NEW FLOOR HAS ITS OWN CREATURES.
	TestTrue(FString::Printf(
		TEXT("the new floor has creatures on it: %d"), Mode->FloorEnemies.Num()),
		Mode->FloorEnemies.Num() > 0);

	// GOING DOWN AGAIN WORKS, which is what rules out a first flight that happens
	// to work and a second that finds the stairs no longer watching.
	TestTrue(TEXT("the stairs on the second floor can be taken too"),
			 Mode->Stairs->ArriveAt(SecondExit));
	TestEqual(TEXT("which reaches floor 3"), Mode->FloorNumber, 3);
	TestEqual(TEXT("having gone down two floors"), Mode->FloorsDescended, 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStairsPlayerMovedTest,
	"Cataclysm.DungeonStairs.ThePlayerArrivesAtTheNewFloorsEntrance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStairsPlayerMovedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStairsTest;

	// THE FAULT THIS RULES OUT LEAVES THE PLAYER IN THE VOID. The floor is
	// replaced inside the same actor, so a player who is not moved is standing
	// wherever the old floor's exit was, which on the new floor is as likely to
	// be solid rock, or nothing at all, as walkable ground.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Mode = SpawnModeOnAFloor(World, /*EnemyScale=*/0.0f);
	if (!TestNotNull(TEXT("a game mode is standing on a floor"), Mode))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player =
		World->SpawnActor<ACataclysmPlayerCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a player character spawned"), Player))
	{
		return false;
	}

	// STOOD AT THE FIRST FLOOR'S ENTRANCE, the way beginning play does it.
	if (!TestTrue(TEXT("the player is placed on the first floor"),
				  Mode->PlaceAtEntrance(Player)))
	{
		return false;
	}
	const FVector FirstEntrance = Player->GetActorLocation();

	// THE PAWN IS HANDED TO THE FLOOR CHANGE RATHER THAN LOOKED UP. In play,
	// `GoToFloor` finds it through `GetFirstPlayerController`, and an automation
	// test world has no player controller. Passing it here is what makes the step
	// reachable at all: without it, the most player-visible thing about the
	// stairs would be the one thing no test could see.
	//
	// NOTHING MOVES THE PLAYER IN THIS TEST BUT THE FLOOR CHANGE ITSELF. That is
	// the point: where the player ends up below is where taking the stairs put
	// them.
	if (!TestTrue(TEXT("the player goes down a floor"),
				  Mode->GoDownOneFloor(Player)))
	{
		return false;
	}

	const FVector SecondEntrance = Player->GetActorLocation();
	const FVector Entrance = Mode->CurrentFloor->EntranceWorld();

	TestTrue(FString::Printf(
		TEXT("the player stands at the new floor's entrance: at %s, the "
			 "entrance is %s"),
		*SecondEntrance.ToCompactString(), *Entrance.ToCompactString()),
		FVector::Dist2D(SecondEntrance, Entrance) < 1.0f);

	TestTrue(TEXT("which is walkable ground on the new floor"),
			 Mode->CurrentFloor->GetPlan().IsFloor(
				 Mode->CurrentFloor->CellOfWorld(SecondEntrance)));

	// AND IT IS NOT WHERE THEY WERE. Two floors whose entrances happened to be the
	// same cell would make this test pass while nothing moved, so it is worth
	// saying that they are not.
	TestFalse(TEXT("and it is not where they were standing"),
			  FVector::Dist2D(SecondEntrance, FirstEntrance) < 1.0f);

	// THEY ARE ON THE SURFACE RATHER THAN IN IT, the same check beginning play
	// makes, because being moved to the right cell at the wrong height is a fall
	// through the world.
	const float HalfHeight =
		Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	TestTrue(FString::Printf(
		TEXT("the capsule's bottom rests on the surface: middle at %.1f, half "
			 "height %.1f, surface at %.1f"),
		SecondEntrance.Z, HalfHeight, Entrance.Z),
		FMath::IsNearlyEqual(SecondEntrance.Z - HalfHeight, Entrance.Z, 1.0f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStairsWatchTest,
	"Cataclysm.DungeonStairs.TheyStopWatchingWhileTheFloorIsBeingReplaced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStairsWatchTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStairsTest;

	// THE FAULT THIS RULES OUT IS A FLIGHT OF STAIRS THAT GOES DOWN TWICE. The
	// floor is rebuilt from inside the broadcast the stairs make, so a look
	// arriving part way through would take the stairs again on a floor that is
	// half replaced.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Mode = SpawnModeOnAFloor(World, /*EnemyScale=*/0.0f);
	if (!TestNotNull(TEXT("a game mode is standing on a floor"), Mode))
	{
		return false;
	}
	if (!TestNotNull(TEXT("and the stairs were placed on it"), Mode->Stairs.Get()))
	{
		return false;
	}

	TestTrue(TEXT("the stairs watch for the player once placed"),
			 Mode->Stairs->IsWatching());

	// TAKING THEM STOPS THE WATCH AND PLACING THEM AGAIN RESTARTS IT, which is
	// what makes the second flight work as well as the first.
	TestTrue(TEXT("the stairs are taken"),
			 Mode->Stairs->ArriveAt(Mode->Stairs->GetActorLocation()));
	TestTrue(TEXT("and they are watching again on the new floor"),
			 Mode->Stairs->IsWatching());

	Mode->Stairs->StopWatching();
	TestFalse(TEXT("stopping the watch stops it"), Mode->Stairs->IsWatching());

	// AND A STOPPED WATCH DOES NOT STOP THE STAIRS BEING TAKEN BY HAND. The watch
	// is how the player is noticed, not what decides whether the stairs work.
	const int32 Before = Mode->FloorNumber;
	TestTrue(TEXT("they can still be taken while not watching"),
			 Mode->Stairs->ArriveAt(Mode->Stairs->GetActorLocation()));
	TestEqual(TEXT("and the floor advanced"), Mode->FloorNumber, Before + 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStairsConsoleFloorTest,
	"Cataclysm.DungeonStairs.TheyWorkFromAFloorAskedForAtTheConsole",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStairsConsoleFloorTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStairsTest;

	// THE FAULT THIS RULES OUT MAKES THE STAIRS LOOK BROKEN AND IS NOT IN THEM.
	// `Cataclysm.DungeonFloor 5` wins over the game mode's own setting every time
	// a floor is built, so without the override following the floor being walked,
	// taking the stairs from floor 5 would set the setting to 6 and build floor 5
	// again -- the same floor, the same creatures, the same stairs.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	IConsoleVariable* FloorVariable =
		IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.DungeonFloor"));
	if (!TestNotNull(TEXT("the floor console variable exists"), FloorVariable))
	{
		return false;
	}

	const int32 Previous = FloorVariable->GetInt();
	ON_SCOPE_EXIT { FloorVariable->Set(Previous, ECVF_SetByConsole); };

	FloorVariable->Set(5, ECVF_SetByConsole);

	ACataclysmDungeonGameMode* Mode = SpawnMode(World);
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode))
	{
		return false;
	}
	Mode->EnemyScale = 0.0f;

	if (!TestTrue(TEXT("it stands on the floor the console asked for"),
				  Mode->GoToFloor(Mode->ChooseFloorNumber())))
	{
		return false;
	}
	TestEqual(TEXT("which is floor 5"), Mode->ChooseFloorNumber(), 5);

	const TArray<ECataclysmFloorCell> FifthCells = Mode->CurrentFloor->GetPlan().Cells;

	if (!TestTrue(TEXT("the player goes down a floor"), Mode->GoDownOneFloor()))
	{
		return false;
	}

	TestEqual(TEXT("the floor being walked is now 6"), Mode->ChooseFloorNumber(), 6);
	TestEqual(TEXT("and the console variable followed it"),
			  FloorVariable->GetInt(), 6);
	TestFalse(TEXT("and the floor built is a different floor"),
			  Mode->CurrentFloor->GetPlan().Cells == FifthCells);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStairsFloorNumberTest,
	"Cataclysm.DungeonStairs.AFloorNumberIsNeverBelowOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStairsFloorNumberTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStairsTest;

	// FLOORS ARE COUNTED FROM 1 AND ZERO MEANS SOMETHING ELSE. `FCataclysmSavedFloor`
	// reads a floor of zero as "nobody is in a dungeon", so a game mode that
	// reached floor 0 would write a record saying the fight is not happening.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Mode = SpawnMode(World);
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode))
	{
		return false;
	}
	Mode->EnemyScale = 0.0f;

	TestTrue(TEXT("going to floor zero builds a floor anyway"),
			 Mode->GoToFloor(0));
	TestEqual(TEXT("and it is floor 1"), Mode->FloorNumber, 1);

	TestTrue(TEXT("going to a negative floor builds a floor anyway"),
			 Mode->GoToFloor(-40));
	TestEqual(TEXT("and it is floor 1"), Mode->FloorNumber, 1);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
