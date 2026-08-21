// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmGatekeeperCharacter.h"
#include "Character/CataclysmImpCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Dungeon/CataclysmDungeonFloor.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmFloorGenerator.h"
#include "Dungeon/CataclysmFloorPopulation.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
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

// ---------------------------------------------------------------------------
// Looking at another floor without rebuilding
// ---------------------------------------------------------------------------

namespace CataclysmDungeonModeTest
{
	/**
	 * Sets a console variable for as long as it is in scope, and puts it back.
	 *
	 * A TEST THAT LEFT ONE SET WOULD CHANGE EVERY TEST AFTER IT. Console
	 * variables are global and the automation suite runs in one process, so
	 * `Cataclysm.DungeonLayout` left at 1 would quietly carve caverns for the
	 * five tests above and they would still pass, which is worse than failing.
	 *
	 * SET AT THE CONSOLE'S OWN PRIORITY, the same as
	 * `CataclysmTestWorld::FScopedCritRoll` and for the reason recorded there:
	 * Unreal remembers who set a console variable and silently discards a write
	 * from code once the command line has set one.
	 */
	struct FScopedConsoleInt
	{
		FScopedConsoleInt(const TCHAR* Name, int32 Value)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(Name);
			if (Variable)
			{
				Previous = Variable->GetInt();
				Variable->Set(Value, ECVF_SetByConsole);
			}
		}

		~FScopedConsoleInt()
		{
			if (Variable)
			{
				Variable->Set(Previous, ECVF_SetByConsole);
			}
		}

		IConsoleVariable* Variable = nullptr;
		int32 Previous = 0;
	};

	/**
	 * The same for a console variable holding a number with a fraction.
	 *
	 * A SEPARATE TYPE RATHER THAN A TEMPLATE. `IConsoleVariable` has a separate
	 * `GetFloat` and `GetInt`, and `GetInt` on a float variable truncates, so a
	 * scope guard that read the wrong one would restore 0.25 as 0 and silently
	 * empty every floor in every test after it.
	 */
	struct FScopedConsoleFloat
	{
		FScopedConsoleFloat(const TCHAR* Name, float Value)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(Name);
			if (Variable)
			{
				Previous = Variable->GetFloat();
				Variable->Set(Value, ECVF_SetByConsole);
			}
		}

		~FScopedConsoleFloat()
		{
			if (Variable)
			{
				Variable->Set(Previous, ECVF_SetByConsole);
			}
		}

		IConsoleVariable* Variable = nullptr;
		float Previous = 0.0f;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModeSeedControlTest,
	"Cataclysm.DungeonMode.TheConsoleCanAskForAnotherDungeon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModeSeedControlTest::RunTest(const FString& Parameters)
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

	Mode->DungeonSeed = 5;

	// Zero leaves the game mode's own setting alone, which is what a person who
	// has never typed the command gets.
	{
		FScopedConsoleInt Untouched(TEXT("Cataclysm.DungeonSeed"), 0);
		TestEqual(TEXT("with the console variable at zero, the setting decides"),
				  Mode->ChooseSeed(), 5);
	}

	// A number above zero is that dungeon.
	{
		FScopedConsoleInt Asked(TEXT("Cataclysm.DungeonSeed"), 91);
		TestEqual(TEXT("a number above zero is the dungeon walked"),
				  Mode->ChooseSeed(), 91);
	}

	// Minus one rolls a new one. The entropy is passed in rather than read from
	// the clock, because a test that rolled twice and expected two different
	// numbers would be a test that usually passes.
	{
		FScopedConsoleInt Rolling(TEXT("Cataclysm.DungeonSeed"), -1);

		const int32 First = Mode->ChooseSeed(/*Entropy=*/123456789);
		const int32 Second = Mode->ChooseSeed(/*Entropy=*/987654321);

		TestNotEqual(TEXT("two different moments give two different dungeons"),
					 First, Second);
		TestTrue(TEXT("and both are seeds the generator will accept"),
				 First > 0 && Second > 0);

		// AND IT DOES NOT SIMPLY RETURN THE SETTING, which is how this would
		// look if the -1 case were never reached.
		TestNotEqual(TEXT("a rolled seed is not the game mode's own setting"),
					 First, Mode->DungeonSeed);

		// The same moment gives the same dungeon, because the generator is
		// deterministic and only the choice of seed is rolled.
		TestEqual(TEXT("the same moment gives the same dungeon"),
				  Mode->ChooseSeed(/*Entropy=*/123456789), First);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModeFloorControlTest,
	"Cataclysm.DungeonMode.TheConsoleCanAskForAnotherFloorOfTheSameDungeon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModeFloorControlTest::RunTest(const FString& Parameters)
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

	Mode->FloorNumber = 3;

	{
		FScopedConsoleInt Untouched(TEXT("Cataclysm.DungeonFloor"), 0);
		TestEqual(TEXT("with the console variable at zero, the setting decides"),
				  Mode->ChooseFloorNumber(), 3);
	}

	{
		FScopedConsoleInt Asked(TEXT("Cataclysm.DungeonFloor"), 40);
		TestEqual(TEXT("a number above zero is the floor walked"),
				  Mode->ChooseFloorNumber(), 40);

		// AND IT REACHES THE FLOOR THAT IS BUILT, not only this answer. A
		// console variable nothing reads is a control that does nothing.
		ACataclysmDungeonFloor* Floor = Mode->BuildFloor();
		if (!TestNotNull(TEXT("it built a floor"), Floor))
		{
			return false;
		}

		FCataclysmFloorRequest Request;
		Request.DungeonSeed = Mode->DungeonSeed;
		Request.FloorNumber = 40;
		Request.Layout = Mode->Layout;

		TestTrue(TEXT("and the floor built is floor 40"),
				 Floor->GetPlan().Cells
					 == FCataclysmFloorGenerator::Generate(Request).Cells);
	}

	// Floors are counted from 1, so a negative asked for at the console cannot
	// produce floor zero, which would mean nobody is in a dungeon.
	{
		FScopedConsoleInt Nonsense(TEXT("Cataclysm.DungeonFloor"), -7);
		TestTrue(TEXT("a floor number is never below 1, whatever is typed"),
				 Mode->ChooseFloorNumber() >= 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModeLayoutControlTest,
	"Cataclysm.DungeonMode.TheConsoleCanAskForAnotherLayoutFamily",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModeLayoutControlTest::RunTest(const FString& Parameters)
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

	Mode->Layout = ECataclysmFloorLayout::Halls;

	// MINUS ONE MEANS "USE THE SETTING" HERE AND ZERO DOES NOT, unlike the other
	// two controls, because zero is a real answer: it is the Halls family.
	{
		FScopedConsoleInt Untouched(TEXT("Cataclysm.DungeonLayout"), -1);
		TestEqual(TEXT("with the console variable at minus one, the setting decides"),
				  Mode->ChooseLayout(), ECataclysmFloorLayout::Halls);
	}

	// Every family can be asked for by number, including the first one.
	for (uint8 Which = 0; Which < static_cast<uint8>(ECataclysmFloorLayout::Count); ++Which)
	{
		FScopedConsoleInt Asked(TEXT("Cataclysm.DungeonLayout"), Which);
		TestEqual(FString::Printf(TEXT("layout %d is %s"), Which,
				  CataclysmFloorLayoutName(static_cast<ECataclysmFloorLayout>(Which))),
				  Mode->ChooseLayout(), static_cast<ECataclysmFloorLayout>(Which));
	}

	// AND IT REACHES THE FLOOR THAT IS BUILT. Asking for caverns and getting
	// halls is the failure this rules out.
	{
		FScopedConsoleInt Caverns(TEXT("Cataclysm.DungeonLayout"),
			static_cast<int32>(ECataclysmFloorLayout::Caverns));

		ACataclysmDungeonFloor* Floor = Mode->BuildFloor();
		if (!TestNotNull(TEXT("it built a floor"), Floor))
		{
			return false;
		}
		TestEqual(TEXT("asking for caverns builds a cavern"),
				  Floor->GetPlan().Layout, ECataclysmFloorLayout::Caverns);
		TestNotEqual(TEXT("which is not the family the setting asks for"),
					 Floor->GetPlan().Layout, Mode->Layout);
	}

	// A number nobody should type is refused rather than cast. Casting 40 to the
	// enum would carve nothing and leave the player standing in the void.
	{
		FScopedConsoleInt Nonsense(TEXT("Cataclysm.DungeonLayout"), 40);
		TestEqual(TEXT("a layout number out of range falls back to the setting"),
				  Mode->ChooseLayout(), ECataclysmFloorLayout::Halls);
	}
	{
		FScopedConsoleInt Nonsense(TEXT("Cataclysm.DungeonLayout"),
			static_cast<int32>(ECataclysmFloorLayout::Count));
		TestEqual(TEXT("and the count itself is not a layout"),
				  Mode->ChooseLayout(), ECataclysmFloorLayout::Halls);
	}

	return true;
}

// ---------------------------------------------------------------------------
// Creatures on the floor
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModeClassForTest,
	"Cataclysm.DungeonMode.EveryCreatureThePlannerCanNameHasACharacterClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModeClassForTest::RunTest(const FString& Parameters)
{
	// THE FAULT THIS RULES OUT IS SILENT AND SURVIVES EVERY OTHER TEST. The
	// populator names creatures with a plain enum so it can be swept headlessly;
	// the game mode maps each name to a class. A creature added to the enum and
	// forgotten in that map is a creature the populator places and nothing
	// spawns, so the floor is quietly short of creatures and every count in
	// every log line still looks reasonable.
	for (uint8 Which = 0; Which < static_cast<uint8>(ECataclysmDungeonCreature::Count); ++Which)
	{
		const ECataclysmDungeonCreature Creature =
			static_cast<ECataclysmDungeonCreature>(Which);

		TestNotNull(FString::Printf(TEXT("a %s can be spawned"),
					CataclysmDungeonCreatureName(Creature)),
					ACataclysmDungeonGameMode::ClassFor(Creature).Get());
	}

	// AND NO ORDINARY FLOOR CARRIES THE BOSS. The design places a Gatekeeper at
	// the end of a dungeon, one per dungeon. Nothing holds a floor count yet --
	// that is issue #41 -- so the way to keep the promise today is that no
	// creature the populator can name maps to it.
	for (uint8 Which = 0; Which < static_cast<uint8>(ECataclysmDungeonCreature::Count); ++Which)
	{
		const ECataclysmDungeonCreature Creature =
			static_cast<ECataclysmDungeonCreature>(Which);

		TestNotEqual(FString::Printf(
			TEXT("a %s is not the boss"), CataclysmDungeonCreatureName(Creature)),
			ACataclysmDungeonGameMode::ClassFor(Creature).Get(),
			static_cast<UClass*>(ACataclysmGatekeeperCharacter::StaticClass()));
	}

	// THE CONTROL. Without it the loop above passes on a `ClassFor` that returns
	// null for everything, which is exactly the failure it is written against.
	TestEqual(TEXT("and the map really is a map: an Imp gives the Imp class"),
		ACataclysmDungeonGameMode::ClassFor(ECataclysmDungeonCreature::Imp).Get(),
		static_cast<UClass*>(ACataclysmImpCharacter::StaticClass()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModePopulatesTest,
	"Cataclysm.DungeonMode.ItPutsCreaturesOnTheFloorAndNotInsideIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModePopulatesTest::RunTest(const FString& Parameters)
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

	// POPULATING BEFORE THERE IS A FLOOR PLACES NOTHING AND SAYS SO. A silent
	// success here would put sixty creatures at the world origin.
	TestEqual(TEXT("nothing can be placed before a floor has been built"),
			  Mode->PopulateFloor(), 0);

	ACataclysmDungeonFloor* Floor = Mode->BuildFloor();
	if (!TestNotNull(TEXT("it built a floor"), Floor))
	{
		return false;
	}

	// BUILDING A FLOOR DOES NOT POPULATE IT. The two are separate calls so that a
	// test wanting only geometry does not pay for spawned characters, and so that
	// walking an empty floor is one console command.
	TestEqual(TEXT("building a floor puts nothing on it by itself"),
			  Mode->FloorEnemies.Num(), 0);

	const int32 Spawned = Mode->PopulateFloor();
	TestTrue(FString::Printf(TEXT("populating it spawned creatures: %d"), Spawned),
			 Spawned > 0);
	TestEqual(TEXT("and the game mode remembers all of them"),
			  Mode->FloorEnemies.Num(), Spawned);

	int32 InRock = 0;
	int32 SunkIntoTheGround = 0;
	int32 Bosses = 0;

	// HOW FAR THE BOTTOM OF A CAPSULE ENDS UP FROM THE WALKING SURFACE, at both
	// ends. A count on its own says the height is wrong and nothing about
	// whether it is a centimetre or a metre, and the two mean different faults.
	float LowestOffset = MAX_flt;
	float HighestOffset = -MAX_flt;

	for (ACataclysmEnemyCharacter* Enemy : Mode->FloorEnemies)
	{
		if (!IsValid(Enemy))
		{
			continue;
		}

		if (Enemy->IsA(ACataclysmGatekeeperCharacter::StaticClass()))
		{
			++Bosses;
		}

		const FVector Where = Enemy->GetActorLocation();
		const FIntPoint Cell = Floor->CellOfWorld(Where);

		if (!Floor->GetPlan().IsFloor(Cell))
		{
			++InRock;
			continue;
		}

		// ITS CAPSULE'S BOTTOM RESTS ON THE WALKING SURFACE. A character is a
		// capsule whose origin is its middle, so putting that origin on the
		// surface buries the lower half. The six creatures placed here are
		// between 87.95 and 114 cm in half height and none of them is the base
		// enemy's 80, so a single shared correction would be wrong for all six.
		const UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
		const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.0f;
		const float Surface = Floor->WorldOfCell(Cell).Z;
		const float Offset = Where.Z - HalfHeight - Surface;

		LowestOffset = FMath::Min(LowestOffset, Offset);
		HighestOffset = FMath::Max(HighestOffset, Offset);

		// A BAND RATHER THAN A SINGLE HEIGHT, AND THE REASON IS THE ENGINE'S AND
		// NOT THIS PROJECT'S. A creature is spawned with its capsule bottom
		// exactly on the surface, and Unreal's character movement then settles it
		// a little way clear of the ground: `MAX_FLOOR_DIST` is 2.4 cm and every
		// creature measured on 2026-08-21 ended up at exactly that. Asserting an
		// exact height failed on 74 of 105 creatures for that reason alone.
		//
		// WHAT THE BAND STILL CATCHES is the fault worth catching: a creature
		// buried in the floor, which is a negative offset, and one hanging in the
		// air, which is a large positive one. Both are what a wrong capsule half
		// height produces, and the six creatures here differ by 26 cm in half
		// height so a shared constant would be wrong by far more than this band.
		constexpr float DeepestAllowedCm = -0.5f;
		constexpr float HighestAllowedCm = 5.0f;

		if (Offset < DeepestAllowedCm || Offset > HighestAllowedCm)
		{
			++SunkIntoTheGround;
		}
	}

	TestEqual(TEXT("no creature stands in solid rock"), InRock, 0);
	TestEqual(FString::Printf(
		TEXT("every creature rests on the walking surface rather than in it or "
			 "above it; the capsule bottoms sat between %.2f and %.2f cm from "
			 "the surface"), LowestOffset, HighestOffset),
		SunkIntoTheGround, 0);
	TestEqual(TEXT("and no boss is on an ordinary floor"), Bosses, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModeClearsOldCreaturesTest,
	"Cataclysm.DungeonMode.GoingToAnotherFloorRemovesTheLastFloorsCreatures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModeClearsOldCreaturesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonModeTest;

	// THE SHAPE TAKING THE STAIRS WILL USE, and the fault it must not have.
	// `BuildFloor` replaces the floor inside the same actor, so creatures from
	// the floor before would be left standing in mid-air over the new one, or
	// inside its rock, still hunting the player. Nothing at run time reports it.
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

	// A THIN FLOOR ON PURPOSE. This test is about removal and not about density,
	// and spawning two full floors' worth of characters to prove it would be
	// slower for no extra evidence.
	Mode->EnemyScale = 0.1f;
	Mode->FloorNumber = 1;

	if (!TestNotNull(TEXT("it built the first floor"), Mode->BuildFloor()))
	{
		return false;
	}
	if (!TestTrue(TEXT("and put creatures on it"), Mode->PopulateFloor() > 0))
	{
		return false;
	}

	TArray<TWeakObjectPtr<ACataclysmEnemyCharacter>> FromTheFirstFloor;
	for (ACataclysmEnemyCharacter* Enemy : Mode->FloorEnemies)
	{
		FromTheFirstFloor.Add(Enemy);
	}

	Mode->FloorNumber = 2;
	if (!TestNotNull(TEXT("it built the second floor"), Mode->BuildFloor()))
	{
		return false;
	}
	const int32 OnTheSecond = Mode->PopulateFloor();
	TestTrue(TEXT("and put creatures on that one too"), OnTheSecond > 0);

	int32 Survivors = 0;
	for (const TWeakObjectPtr<ACataclysmEnemyCharacter>& Old : FromTheFirstFloor)
	{
		if (Old.IsValid() && !Old->IsActorBeingDestroyed())
		{
			++Survivors;
		}
	}

	TestEqual(FString::Printf(
		TEXT("none of the first floor's %d creatures is still in the world"),
		FromTheFirstFloor.Num()), Survivors, 0);
	TestEqual(TEXT("and the game mode's list holds only the new floor's"),
			  Mode->FloorEnemies.Num(), OnTheSecond);

	// AND CLEARING BY HAND EMPTIES IT, which is what an empty floor needs.
	Mode->ClearFloorEnemies();
	TestEqual(TEXT("clearing removes every creature from the list"),
			  Mode->FloorEnemies.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonModeEnemyScaleTest,
	"Cataclysm.DungeonMode.TheConsoleCanAskForMoreCreaturesOrForNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonModeEnemyScaleTest::RunTest(const FString& Parameters)
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

	Mode->EnemyScale = 0.25f;

	// BELOW ZERO MEANS "USE THE SETTING" HERE AND ZERO DOES NOT, unlike the seed
	// and floor controls, because zero is a real answer: it is a floor with
	// nothing on it.
	{
		FScopedConsoleFloat Untouched(TEXT("Cataclysm.DungeonEnemyScale"), -1.0f);
		TestEqual(TEXT("below zero, the game mode's own setting decides"),
				  Mode->ChooseEnemyScale(), 0.25f);
	}
	{
		FScopedConsoleFloat Asked(TEXT("Cataclysm.DungeonEnemyScale"), 2.0f);
		TestEqual(TEXT("a number at or above zero is the density used"),
				  Mode->ChooseEnemyScale(), 2.0f);
	}

	// A NEGATIVE SETTING IS NOT A NEGATIVE NUMBER OF CREATURES. The property
	// carries a clamp in the editor; a default saved before that clamp existed
	// would not be re-clamped, and a negative reaching the populator would ask
	// for a negative count.
	{
		FScopedConsoleFloat Untouched(TEXT("Cataclysm.DungeonEnemyScale"), -1.0f);
		Mode->EnemyScale = -5.0f;
		TestEqual(TEXT("a negative setting is read as an empty floor"),
				  Mode->ChooseEnemyScale(), 0.0f);
		Mode->EnemyScale = 0.25f;
	}

	// AND IT REACHES THE FLOOR THAT IS BUILT. A console variable nothing reads is
	// a control that does nothing, which is the failure the layout control's own
	// test was written against.
	if (!TestNotNull(TEXT("it built a floor"), Mode->BuildFloor()))
	{
		return false;
	}

	int32 WithNone = 0;
	{
		FScopedConsoleFloat Empty(TEXT("Cataclysm.DungeonEnemyScale"), 0.0f);
		WithNone = Mode->PopulateFloor();
	}

	int32 WithSome = 0;
	{
		FScopedConsoleFloat Some(TEXT("Cataclysm.DungeonEnemyScale"), 0.1f);
		WithSome = Mode->PopulateFloor();
	}

	int32 WithMore = 0;
	{
		FScopedConsoleFloat More(TEXT("Cataclysm.DungeonEnemyScale"), 0.4f);
		WithMore = Mode->PopulateFloor();
	}

	TestEqual(TEXT("a scale of zero leaves the floor empty"), WithNone, 0);
	TestTrue(FString::Printf(TEXT("a small scale puts some on it: %d"), WithSome),
			 WithSome > 0);
	TestTrue(FString::Printf(
		TEXT("and a larger scale puts more on it: %d against %d"),
		WithMore, WithSome), WithMore > WithSome);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
