// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for walking a dungeon costing the empire days, issue #1092.
 *
 * WHAT THESE COVER THAT NOTHING ELSE COULD. `CataclysmDungeonGameModeTests.cpp`
 * checks that a floor gets built and that the player is stood on it;
 * `CataclysmEmpireRunTests.cpp` checks that a day moves timers and takes cities.
 * Both passed while the two knew nothing about each other, so a player could
 * walk down forty floors and the empire's day would not move.
 *
 * THE RUN IS HANDED OVER RATHER THAN FOUND. A world built by
 * `UWorld::CreateWorld` has no game instance, so `UCataclysmGameInstance` cannot
 * be asked for a run; `SetEmpireRunForTests` is the seam, and it is the same one
 * `UCataclysmEmpireMapWidget` has for the same reason.
 *
 * WHAT IS NOT COVERED. That the stairs call `GoDownOneFloor` -- that needs a
 * player walking onto a trigger, which `-nullrhi` cannot produce -- and anything
 * about how a floor looks.
 */

namespace CataclysmDungeonCostsDaysTest
{
	/** A run with a wave already on the map, and the mode bound to nothing yet. */
	struct FBound
	{
		UWorld* World = nullptr;
		ACataclysmDungeonGameMode* Mode = nullptr;
		UCataclysmEmpireRun* Run = nullptr;
	};

	FBound Make(int32 Seed = 1)
	{
		FBound Out;

		Out.World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Out.World)
		{
			return Out;
		}

		Out.Mode = Out.World->SpawnActor<ACataclysmDungeonGameMode>();
		if (!Out.Mode)
		{
			return Out;
		}

		Out.Run = NewObject<UCataclysmEmpireRun>();
		Out.Run->Begin(Seed);

		// ONE DAY, WHICH IS THE FIRST SURGE. A run begins with an intact empire
		// and nothing standing on it, so without this there would be no dungeon
		// to walk.
		Out.Run->AdvanceDay();

		Out.Mode->SetEmpireRunForTests(Out.Run);

		return Out;
	}
}

// ---------------------------------------------------------------------------
// What a floor costs
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonCostsDaysTest,
	"Cataclysm.DungeonMode.WalkingDownAFloorSpendsADayOfTheEmpire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonCostsDaysTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonCostsDaysTest;

	FBound Bound = Make();
	if (!TestNotNull(TEXT("a test world was created"), Bound.World))
	{
		return false;
	}
	ON_SCOPE_EXIT { Bound.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Bound.Mode)
		|| !TestTrue(TEXT("and the first surge put dungeons on the map"),
					 Bound.Run->DungeonCount() > 0))
	{
		return false;
	}

	const int32 DungeonId = Bound.Run->Dungeons[0].DungeonId;
	const int32 Floors = Bound.Run->Dungeons[0].Floors;
	const int32 DayBefore = Bound.Run->Day();

	// ENTERING IS A FLOOR. The player is standing on floor 1 and a floor costs a
	// day, which is what makes walking N floors cost N days.
	if (!TestTrue(TEXT("the dungeon is entered"),
				  Bound.Mode->EnterEmpireDungeon(DungeonId)))
	{
		return false;
	}

	TestEqual(TEXT("arriving on floor 1 spent a day"),
			  Bound.Run->Day(), DayBefore + 1);
	TestEqual(TEXT("and the game mode is walking that dungeon"),
			  Bound.Mode->EmpireDungeonId, DungeonId);
	TestEqual(TEXT("as deep as the dungeon actually is"),
			  Bound.Mode->EmpireDungeonFloors(), Floors);
	TestEqual(TEXT("which is what the floor count now says"),
			  Bound.Mode->TotalFloors, Floors);

	// AND EACH DESCENT IS ANOTHER. Down to the last floor but one, so the
	// dungeon is not cleared part way through and the arithmetic stays simple.
	const int32 Descents = FMath::Max(1, Floors - 1);

	for (int32 Down = 1; Down <= Descents; ++Down)
	{
		const int32 Before = Bound.Run->Day();

		TestTrue(FString::Printf(TEXT("descent %d works"), Down),
				 Bound.Mode->GoDownOneFloor());
		TestEqual(FString::Printf(TEXT("descent %d spent exactly one day"), Down),
				  Bound.Run->Day(), Before + 1);
	}

	// N FLOORS COST N DAYS AT THE STARTING RATE, which is what this run has: no
	// city here bought the upgrade that shortens a walk, and no empire tree
	// exists yet. A city that had bought one would make the same dungeon cost
	// fewer days WITHOUT changing its floor count, its reward or its timer --
	// that is `FCataclysmDungeon::WalkDays`, and it has its own tests.
	TestEqual(FString::Printf(
		TEXT("walking %d floors of a %d floor dungeon cost %d days"),
		Descents + 1, Floors, Descents + 1),
		Bound.Run->Day(), DayBefore + Descents + 1);

	TestEqual(TEXT("and the floor being walked is the last one"),
			  Bound.Mode->FloorNumber, Floors);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonNoRunCostsNothingTest,
	"Cataclysm.DungeonMode.WithNoEmpireRunAFloorStillCostsNothingAndTheStairsGoOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonNoRunCostsNothingTest::RunTest(const FString& Parameters)
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Mode = World->SpawnActor<ACataclysmDungeonGameMode>();
	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Mode))
	{
		return false;
	}

	// PRESSING PLAY IN `L_Dungeon` WITH NO EMPIRE MUST STILL WORK. It is how a
	// person looks at a floor, and it is what every other test in
	// `CataclysmDungeonGameModeTests.cpp` does.
	TestEqual(TEXT("no dungeon of the empire is being walked"),
			  Mode->EmpireDungeonId, INDEX_NONE);
	TestEqual(TEXT("so there is no depth to report"),
			  Mode->EmpireDungeonFloors(), 0);

	// AND THE STAIRS DESCEND FOR EVER. A floor built from the settings has no
	// bottom -- that was true before this change and has to stay true, because
	// the sandbox has no dungeon to run out of.
	TestFalse(TEXT("no floor is the last floor"), Mode->IsOnTheLastFloor());

	Mode->GoToFloor(1);
	for (int32 Down = 1; Down <= 3; ++Down)
	{
		TestTrue(FString::Printf(TEXT("descent %d works with no empire"), Down),
				 Mode->GoDownOneFloor());
		TestFalse(TEXT("and still no floor is the last"), Mode->IsOnTheLastFloor());
	}

	TestEqual(TEXT("three descents reached floor 4"), Mode->FloorNumber, 4);
	TestEqual(TEXT("and all three were counted"), Mode->FloorsDescended, 3);

	// NOTHING TO ENTER, LEAVE OR CLEAR, and none of it falls over.
	TestFalse(TEXT("no dungeon can be entered"), Mode->EnterEmpireDungeon(0));
	TestFalse(TEXT("nor cleared"), Mode->ClearEmpireDungeon());
	Mode->LeaveEmpireDungeon();

	return true;
}

// ---------------------------------------------------------------------------
// What stops, and what does not
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonTimerStopsTest,
	"Cataclysm.DungeonMode.TheDungeonBeingWalkedIsTheOneTimerThatStops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonTimerStopsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonCostsDaysTest;

	FBound Bound = Make();
	if (!TestNotNull(TEXT("a test world was created"), Bound.World))
	{
		return false;
	}
	ON_SCOPE_EXIT { Bound.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Bound.Mode)
		|| !TestTrue(TEXT("more than one dungeon is standing"),
					 Bound.Run->DungeonCount() > 1))
	{
		return false;
	}

	const int32 Walked = Bound.Run->Dungeons[0].DungeonId;
	const int32 Other = Bound.Run->Dungeons[1].DungeonId;

	Bound.Mode->EnterEmpireDungeon(Walked);

	const float WalkedBefore = Bound.Run->Clock->DaysUntilResolveFor(Walked);
	const float OtherBefore = Bound.Run->Clock->DaysUntilResolveFor(Other);

	// FIVE FLOORS DOWN, WHICH IS FIVE DAYS.
	for (int32 Down = 0; Down < 5; ++Down)
	{
		Bound.Mode->GoDownOneFloor();
	}

	// THE ONE BEING WALKED DOES NOT COUNT DOWN. Its residents are busy fighting
	// the player rather than marching on the city, so entering is a guaranteed
	// save rather than a gamble. See `UCataclysmDayClock::bTimerTicksWhileRunning`.
	TestEqual(TEXT("the timer of the dungeon being walked has not moved"),
			  Bound.Run->Clock->DaysUntilResolveFor(Walked), WalkedBefore, 0.001f);

	// AND EVERY OTHER ONE DOES. That is what walking a dungeon costs: five days
	// in here is five days every other timer advanced without you.
	TestEqual(TEXT("and another dungeon's timer lost five days"),
			  Bound.Run->Clock->DaysUntilResolveFor(Other), OtherBefore - 5.0f,
			  0.001f);

	// LEAVING STARTS IT AGAIN, and the dungeon is still standing.
	Bound.Mode->LeaveEmpireDungeon();

	const float WalkedOnLeaving = Bound.Run->Clock->DaysUntilResolveFor(Walked);
	Bound.Run->AdvanceDay();

	TestEqual(TEXT("once left, its timer counts again"),
			  Bound.Run->Clock->DaysUntilResolveFor(Walked),
			  WalkedOnLeaving - 1.0f, 0.001f);
	TestNotNull(TEXT("and it is still standing on the map"),
				Bound.Run->FindDungeon(Walked));
	TestEqual(TEXT("with no dungeon being walked"),
			  Bound.Mode->EmpireDungeonId, INDEX_NONE);

	return true;
}

// ---------------------------------------------------------------------------
// The bottom
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonBottomTest,
	"Cataclysm.DungeonMode.ReachingTheBottomTakesTheDungeonOffTheMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonBottomTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonCostsDaysTest;

	FBound Bound = Make();
	if (!TestNotNull(TEXT("a test world was created"), Bound.World))
	{
		return false;
	}
	ON_SCOPE_EXIT { Bound.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Bound.Mode)
		|| !TestTrue(TEXT("a dungeon is standing"), Bound.Run->DungeonCount() > 0))
	{
		return false;
	}

	const FCataclysmDungeon First = Bound.Run->Dungeons[0];
	const int32 DungeonId = First.DungeonId;
	const int32 CityId = First.CityId;
	const int32 Floors = First.Floors;
	const int32 StandingBefore = Bound.Run->DungeonCount();

	Bound.Mode->EnterEmpireDungeon(DungeonId);

	// DOWN TO THE LAST FLOOR. Not past it: the descent that would go past it is
	// the one that clears the dungeon, and it is taken on its own below.
	for (int32 Down = 1; Down < Floors; ++Down)
	{
		TestFalse(FString::Printf(
			TEXT("floor %d of %d is not the last"), Down, Floors),
			Bound.Mode->IsOnTheLastFloor());
		Bound.Mode->GoDownOneFloor();
	}

	TestEqual(TEXT("the floor being walked is the bottom one"),
			  Bound.Mode->FloorNumber, Floors);
	TestTrue(TEXT("and it is the last floor"), Bound.Mode->IsOnTheLastFloor());

	const int32 DayAtTheBottom = Bound.Run->Day();

	// AND THE NEXT DESCENT IS BEATING IT, NOT FINDING ANOTHER FLOOR.
	TestFalse(TEXT("there is no floor below the last one"),
			  Bound.Mode->GoDownOneFloor());

	TestEqual(TEXT("so no further day was spent"),
			  Bound.Run->Day(), DayAtTheBottom);
	TestEqual(TEXT("and no deeper floor was built"),
			  Bound.Mode->FloorNumber, Floors);

	// THE DUNGEON IS GONE FROM BOTH LISTS.
	TestNull(TEXT("the dungeon is off the map"),
			 Bound.Run->FindDungeon(DungeonId));
	TestEqual(TEXT("and off the clock"),
			  Bound.Run->Clock->DaysUntilResolveFor(DungeonId), -1.0f);
	TestEqual(TEXT("one fewer is standing"),
			  Bound.Run->DungeonCount(), StandingBefore - 1);
	TestEqual(TEXT("and nothing is being walked"),
			  Bound.Mode->EmpireDungeonId, INDEX_NONE);

	// AND ITS HOST CITY IS NEVER BITTEN BY IT AGAIN. That is the whole reward
	// for walking it: 400 days is long enough for its timer to have run out
	// several times over.
	bool bResolvedAgain = false;
	for (const FCataclysmDayReport& Report : Bound.Run->AdvanceDays(400))
	{
		bResolvedAgain = bResolvedAgain || Report.Resolved.Contains(DungeonId);
	}

	TestFalse(TEXT("the cleared dungeon never resolves again"), bResolvedAgain);

	// THE CITY IS NOT SAVED BY IT, ONLY SPARED THAT ONE DUNGEON. Anything else
	// standing there keeps biting, which is why this checks the dungeon rather
	// than the city's defence.
	TestNotNull(TEXT("its host city still exists"), Bound.Run->Map->Find(CityId));

	return true;
}

// ---------------------------------------------------------------------------
// What a whole wave costs
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonWholeWaveTest,
	"Cataclysm.DungeonMode.ClearingAWholeWaveCostsItsFloorsInDays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonWholeWaveTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonCostsDaysTest;

	// THE POINT OF THE WHOLE JOIN, MEASURED END TO END. A player who walks a
	// wave spends its floors in days, and that is the decision the strategy
	// layer exists to pose: these dungeons, or those cities.
	//
	// WHAT THIS TEST DELIBERATELY DOES NOT CLAIM. An earlier version of it
	// asserted that a city falls while the player is underground, and that is
	// not true of the first wave: four Outpost dungeons are 8 to 15 floors each,
	// so clearing all of them costs about 44 days, and the earliest a city falls
	// is well past day 100. Claiming it would have been a test that passed only
	// on the seeds where something unusual happened.
	FBound Bound = Make();
	if (!TestNotNull(TEXT("a test world was created"), Bound.World))
	{
		return false;
	}
	ON_SCOPE_EXIT { Bound.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Bound.Mode)
		|| !TestTrue(TEXT("the first surge put dungeons on the map"),
					 Bound.Run->DungeonCount() > 0))
	{
		return false;
	}

	const int32 StartedOnDay = Bound.Run->Day();
	const int32 WaveSize = Bound.Run->DungeonCount();

	int32 FloorsWalked = 0;
	int32 Cleared = 0;

	// ENTER, WALK TO THE BOTTOM, BEAT IT, TAKE THE NEXT. The bound is a
	// backstop rather than the exit: the exit is running out of dungeons.
	for (int32 Guard = 0; Guard < 1000 && Bound.Run->DungeonCount() > 0; ++Guard)
	{
		const int32 DungeonId = Bound.Run->Dungeons[0].DungeonId;
		const int32 Floors = Bound.Run->Dungeons[0].Floors;

		if (!Bound.Mode->EnterEmpireDungeon(DungeonId))
		{
			break;
		}

		// ENTERING IS FLOOR 1, and it starts at the entrance whatever floor the
		// last dungeon was left on.
		TestEqual(FString::Printf(
			TEXT("entering dungeon %d starts at its entrance"), DungeonId),
			Bound.Mode->FloorNumber, 1);

		++FloorsWalked;

		while (!Bound.Mode->IsOnTheLastFloor())
		{
			if (!Bound.Mode->GoDownOneFloor())
			{
				break;
			}
			++FloorsWalked;
		}

		TestEqual(FString::Printf(
			TEXT("dungeon %d was walked to its bottom floor %d"),
			DungeonId, Floors),
			Bound.Mode->FloorNumber, Floors);

		// THE DESCENT PAST THE LAST FLOOR IS BEATING IT.
		Bound.Mode->GoDownOneFloor();
		++Cleared;

		TestNull(FString::Printf(TEXT("dungeon %d is off the map"), DungeonId),
				 Bound.Run->FindDungeon(DungeonId));
	}

	TestEqual(TEXT("every dungeon of the wave was cleared"), Cleared, WaveSize);
	TestEqual(TEXT("and nothing is left standing"), Bound.Run->DungeonCount(), 0);

	// AND THE DAY MOVED BY EXACTLY THE FLOORS WALKED, because nothing in this
	// run has shortened a walk. One floor costs one day as a starting rate, not
	// as an invariant; depth and reward are the same axis, depth and time are
	// not once a player has invested.
	TestEqual(FString::Printf(
		TEXT("walking %d floors across %d dungeons cost %d days"),
		FloorsWalked, Cleared, FloorsWalked),
		Bound.Run->Day(), StartedOnDay + FloorsWalked);

	// A WAVE IS WEEKS OF WORK, not an afternoon. Four Outpost dungeons of 8 to
	// 15 floors is 32 days at the very least, which is what makes ignoring one
	// a real decision rather than an obvious mistake.
	TestTrue(FString::Printf(
		TEXT("clearing the wave cost %d days"), FloorsWalked),
		FloorsWalked >= 4 * 8);

	return true;
}

// ---------------------------------------------------------------------------
// What the dungeon carries into the run
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDungeonCarriesItsSubTypeTest,
	"Cataclysm.DungeonMode.WalkingADungeonCarriesItsSubTypeIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonCarriesItsSubTypeTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonCostsDaysTest;

	// A SEED WHOSE FIRST DUNGEON IS A COW LEVEL, found by looking rather than
	// hoped for. Cow Level is 4 in 100, so most seeds are not one, and a run is
	// built here without a world because that is much cheaper than building one
	// per seed.
	int32 CowSeed = INDEX_NONE;
	for (int32 Seed = 1; Seed <= 500 && CowSeed == INDEX_NONE; ++Seed)
	{
		UCataclysmEmpireRun* Trial = NewObject<UCataclysmEmpireRun>();
		Trial->Begin(Seed);
		Trial->AdvanceDay();

		if (Trial->DungeonCount() > 0 &&
			Trial->Dungeons[0].SubType == ECataclysmDungeonSubType::CowLevel)
		{
			CowSeed = Seed;
		}
	}

	if (!TestTrue(TEXT("a seed whose first dungeon is a Cow Level was found"),
				  CowSeed != INDEX_NONE))
	{
		return false;
	}

	FBound Bound = Make(CowSeed);
	if (!TestNotNull(TEXT("a test world was created"), Bound.World))
	{
		return false;
	}
	ON_SCOPE_EXIT { Bound.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	if (!TestNotNull(TEXT("the dungeon game mode spawned"), Bound.Mode)
		|| !TestTrue(TEXT("and the first surge put dungeons on the map"),
					 Bound.Run->DungeonCount() > 0))
	{
		return false;
	}

	const FCataclysmDungeon& Dungeon = Bound.Run->Dungeons[0];
	const int32 Floors = Dungeon.Floors;
	const int32 DayBefore = Bound.Run->Day();

	// THE CONTROL. The game mode starts every run with no sub-type, so a test
	// that only looked at the end could pass with nothing having been copied at
	// all.
	if (!TestEqual(TEXT("the game mode starts with no sub-type"),
				   static_cast<uint8>(Bound.Mode->RunDungeonSubType()),
				   static_cast<uint8>(ECataclysmDungeonSubType::None)))
	{
		return false;
	}

	if (!TestTrue(TEXT("the dungeon is entered"),
				  Bound.Mode->EnterEmpireDungeon(Dungeon.DungeonId)))
	{
		return false;
	}

	// WHAT THIS IS REALLY FOR. `UCataclysmEnemyScore::ScoreThisFloor` reads the
	// sub-type back off the game mode and adds its weight to every creature on
	// the floor. Before the surge scheduler rolled one, every dungeon in a real
	// run scored as though it had none.
	TestEqual(TEXT("the game mode is walking a Cow Level"),
			  static_cast<uint8>(Bound.Mode->RunDungeonSubType()),
			  static_cast<uint8>(ECataclysmDungeonSubType::CowLevel));

	// AND ITS FLOORS COST TWO DAYS EACH, all the way down, which is the one
	// sub-type rule the empire layer carries out. Arriving on floor 1 is one
	// floor and each descent is another, so walking the whole thing costs twice
	// the days its depth would otherwise cost.
	//
	// STOPPING ON THE LAST FLOOR RATHER THAN DESCENDING PAST IT, because
	// `GoDownOneFloor` on the last floor clears the dungeon instead of moving,
	// and a cleared dungeon leaves the day clock.
	for (int32 Floor = 2; Floor <= Floors; ++Floor)
	{
		Bound.Mode->GoDownOneFloor();
	}

	TestEqual(TEXT("walking the whole Cow Level cost two days a floor"),
			  Bound.Run->Day() - DayBefore, Floors * 2);

	// THE CONTROL FOR THE DAYS. A dungeon that is not a Cow Level, walked the
	// same way in the same kind of run, costs one day a floor. Without this the
	// figure above would pass if every dungeon cost two days a floor.
	int32 PlainSeed = INDEX_NONE;
	for (int32 Seed = 1; Seed <= 500 && PlainSeed == INDEX_NONE; ++Seed)
	{
		UCataclysmEmpireRun* Trial = NewObject<UCataclysmEmpireRun>();
		Trial->Begin(Seed);
		Trial->AdvanceDay();

		if (Trial->DungeonCount() > 0 &&
			Trial->Dungeons[0].SubType != ECataclysmDungeonSubType::CowLevel)
		{
			PlainSeed = Seed;
		}
	}

	if (!TestTrue(TEXT("a seed whose first dungeon is not a Cow Level was found"),
				  PlainSeed != INDEX_NONE))
	{
		return false;
	}

	FBound Plain = Make(PlainSeed);
	if (!TestNotNull(TEXT("a second test world was created"), Plain.World))
	{
		return false;
	}
	ON_SCOPE_EXIT { Plain.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const int32 PlainFloors = Plain.Run->Dungeons[0].Floors;
	const int32 PlainDayBefore = Plain.Run->Day();

	if (!TestTrue(TEXT("the plain dungeon is entered"),
				  Plain.Mode->EnterEmpireDungeon(
					  Plain.Run->Dungeons[0].DungeonId)))
	{
		return false;
	}

	for (int32 Floor = 2; Floor <= PlainFloors; ++Floor)
	{
		Plain.Mode->GoDownOneFloor();
	}

	TestEqual(TEXT("and a dungeon that is not a Cow Level costs one a floor"),
			  Plain.Run->Day() - PlainDayBefore, PlainFloors);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
