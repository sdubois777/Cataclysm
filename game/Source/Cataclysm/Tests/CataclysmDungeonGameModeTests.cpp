// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Dungeon/CataclysmDungeonFloor.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmFloorGenerator.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for the game mode that puts a player on a generated dungeon floor.
 *
 * WHAT THEY CAN AND CANNOT REACH. `StartPlay` cannot be called from an automation
 * test: it wants a player controller and a pawn, and a test world has neither and
 * cannot be given them -- `Tests/CataclysmTestWorld.h` records why. So the two
 * steps it performs are public methods and these call them directly, which is the
 * same shape the sandbox's own tests use for its creature spawners and for the
 * same stated reason: a step left only inside `StartPlay` is a step nothing
 * covers.
 *
 * WHAT IS THEREFORE NOT COVERED, said plainly: that `StartPlay` calls them, and
 * in the right order. Two lines of it are unreachable from here.
 */

namespace CataclysmDungeonModeTest
{
	ACataclysmDungeonGameMode* SpawnMode(UWorld* World)
	{
		return World ? World->SpawnActor<ACataclysmDungeonGameMode>() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModeBuildsAFloorTest,
	"Cataclysm.DungeonMode.ItBuildsAFloorToStandOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModeBuildsAFloorTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModeTest;

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

	ACataclysmDungeonFloor* Floor = Mode->BuildFloor();
	if (!TestNotNull(TEXT("it built a floor"), Floor))
	{
		return false;
	}

	TestTrue(TEXT("the floor has ground to stand on and stairs to reach"),
			 Floor->IsBuilt());
	TestEqual(TEXT("and the game mode remembers it"),
			  Mode->CurrentFloor.Get(), Floor);

	// IT IS THE FLOOR THE SETTINGS ASK FOR, not merely a floor. Building the same
	// request straight from the generator has to give the same cells, or the
	// settings on the game mode do not decide anything.
	FCataclysmFloorRequest Request;
	Request.DungeonSeed = Mode->DungeonSeed;
	Request.FloorNumber = Mode->FloorNumber;
	Request.Layout = Mode->Layout;

	TestTrue(TEXT("it is the floor the game mode's own settings describe"),
			 Floor->GetPlan().Cells
				 == FCataclysmFloorGenerator::Generate(Request).Cells);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModeRebuildsTest,
	"Cataclysm.DungeonMode.ChangingTheFloorNumberBuildsThatFloorInPlace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModeRebuildsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModeTest;

	// THE SHAPE TAKING THE STAIRS WILL USE. Nothing advances the floor number
	// yet, but when something does it will set it and build again, and it must
	// get the next floor in the same actor rather than a second floor beside the
	// first.
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

	Mode->FloorNumber = 1;
	ACataclysmDungeonFloor* First = Mode->BuildFloor();
	if (!TestNotNull(TEXT("it built the first floor"), First))
	{
		return false;
	}
	const TArray<ECataclysmFloorCell> FirstCells = First->GetPlan().Cells;

	Mode->FloorNumber = 2;
	ACataclysmDungeonFloor* Second = Mode->BuildFloor();
	if (!TestNotNull(TEXT("it built the second floor"), Second))
	{
		return false;
	}

	TestEqual(TEXT("the second floor is built in the same actor"), Second, First);
	TestFalse(TEXT("and it is a different floor"),
			  Second->GetPlan().Cells == FirstCells);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModePlacesThePlayerTest,
	"Cataclysm.DungeonMode.ThePlayerIsStoodOnTheFloorAndNotInsideIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModePlacesThePlayerTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModeTest;

	// THE FAULT THIS RULES OUT IS SILENT AND LOOKS LIKE NOTHING. A character is a
	// capsule whose origin is its middle, so putting that origin on the walking
	// surface buries the lower half in the ground. What the player then sees is a
	// character standing in a hole, or falling through the world.
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

	ACataclysmPlayerCharacter* Player =
		World->SpawnActor<ACataclysmPlayerCharacter>(
			FVector(50000.0f, 50000.0f, 50000.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a player character spawned"), Player))
	{
		return false;
	}

	// PLACING BEFORE THERE IS A FLOOR FAILS AND SAYS SO. A silent success here
	// would put the player at the world origin on a floor that is not there.
	TestFalse(TEXT("a pawn cannot be placed before a floor has been built"),
			  Mode->PlaceAtEntrance(Player));

	ACataclysmDungeonFloor* Floor = Mode->BuildFloor();
	if (!TestNotNull(TEXT("it built a floor"), Floor))
	{
		return false;
	}

	TestTrue(TEXT("the player is placed once there is a floor"),
			 Mode->PlaceAtEntrance(Player));

	// It is at the entrance, in the two directions that decide which cell it is
	// standing on.
	const FVector Entrance = Floor->EntranceWorld();
	const FVector Standing = Player->GetActorLocation();

	TestTrue(FString::Printf(
		TEXT("the player stands on the cell the generator chose: at %s, the "
			 "entrance is %s"),
		*Standing.ToCompactString(), *Entrance.ToCompactString()),
		FVector::Dist2D(Standing, Entrance) < 1.0f);

	TestTrue(TEXT("and that cell is walkable"),
			 Floor->GetPlan().IsFloor(Floor->CellOfWorld(Standing)));

	// AND ABOVE THE SURFACE, NOT IN IT. Raised by the capsule's own half height,
	// read from the pawn rather than assumed, because the three designed classes
	// are not all the same size.
	const float HalfHeight =
		Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	TestTrue(FString::Printf(
		TEXT("the capsule's bottom rests on the surface: middle at %.1f, half "
			 "height %.1f, surface at %.1f"),
		Standing.Z, HalfHeight, Entrance.Z),
		FMath::IsNearlyEqual(Standing.Z - HalfHeight, Entrance.Z, 1.0f));

	TestTrue(TEXT("which means the player is above the floor, not inside it"),
			 Standing.Z > Entrance.Z);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModeNoSandboxCreaturesTest,
	"Cataclysm.DungeonMode.ItDoesNotPutTheSandboxsCreaturesInADungeon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModeNoSandboxCreaturesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModeTest;

	// Every sandbox spawner places its creatures at a fixed offset from the world
	// origin, which is where the sandbox's flat floor is. On a generated floor
	// the world origin is the middle of the grid and is as likely to be inside
	// solid rock as on the ground.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Dungeon = SpawnMode(World);
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Dungeon))
	{
		return false;
	}

	TestFalse(TEXT("a dungeon does not spawn the sandbox's creatures"),
			  Dungeon->bSpawnsSandboxCreatures);

	// THE CONTROL. Without this the check above passes on a flag nothing ever
	// sets to true, which would say nothing about the sandbox still working.
	ACataclysmGameMode* Sandbox = World->SpawnActor<ACataclysmGameMode>();
	if (!TestNotNull(TEXT("the sandbox game mode spawned"), Sandbox))
	{
		return false;
	}

	TestTrue(TEXT("and the sandbox still does"),
			 Sandbox->bSpawnsSandboxCreatures);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModeSaveRecordTest,
	"Cataclysm.DungeonMode.TheSaveRecordSaysWhichDungeonAndFloorNotSandbox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModeSaveRecordTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModeTest;

	// `FCataclysmSavedFloor` holds a dungeon name and a floor counted from 1. The
	// sandbox's game mode writes "Sandbox" and 1 because the sandbox is the only
	// level there is. A record that still said that while the player was three
	// floors into a dungeon would be a record of the wrong place.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Dungeon = SpawnMode(World);
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Dungeon))
	{
		return false;
	}

	Dungeon->DungeonName = FName(TEXT("TheDeep"));
	Dungeon->FloorNumber = 7;

	TestEqual(TEXT("the record names the dungeon"),
			  Dungeon->RunFloorName(), FName(TEXT("TheDeep")));
	TestEqual(TEXT("and the floor of it"), Dungeon->RunFloorNumber(), 7);

	// THE CONTROL, again: the sandbox still answers what it always did.
	ACataclysmGameMode* Sandbox = World->SpawnActor<ACataclysmGameMode>();
	if (!TestNotNull(TEXT("the sandbox game mode spawned"), Sandbox))
	{
		return false;
	}

	TestEqual(TEXT("the sandbox still records itself as Sandbox"),
			  Sandbox->RunFloorName(), FName(TEXT("Sandbox")));
	TestEqual(TEXT("on floor 1, because a floor of zero would say nobody is in "
				   "a dungeon"), Sandbox->RunFloorNumber(), 1);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
