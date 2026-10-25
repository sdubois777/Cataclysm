// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Empire/CataclysmDungeonKind.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
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

	/**
	 * The first dungeon that costs the ordinary one day a floor, or -1.
	 *
	 * **WHY A SEARCH RATHER THAN `Dungeons[0]`.** A Cow Level costs two days a
	 * floor deliberately, and three of the tests below are about the starting
	 * rate of one floor, one day. They took the first dungeon on the map until
	 * the sub-type distribution changed on 2026-09-06 -- Cow Level went from 4
	 * in 100 to 7, and every dungeon now gets a sub-type -- at which point the
	 * first dungeon at seed 1 became a Cow Level and all three failed with the
	 * exact doubling they exist to leave to `AWalkedCowLevelCostsTwiceTheDays`.
	 *
	 * CHOOSING BY WHAT IT COSTS RATHER THAN BY SEED. A seed that happens to
	 * produce an ordinary dungeon would work until the next distribution change
	 * and then fail the same way; asking each dungeon what its walk costs cannot.
	 */
	int32 FirstOrdinaryDungeon(const UCataclysmEmpireRun& Run)
	{
		for (int32 Index = 0; Index < Run.Dungeons.Num(); ++Index)
		{
			if (Run.Dungeons[Index].SubType
				!= ECataclysmDungeonSubType::CowLevel)
			{
				return Index;
			}
		}
		return INDEX_NONE;
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

	// AN ORDINARY DUNGEON, because this test is about the starting rate. A Cow
	// Level costs two days a floor and has its own test for it.
	const int32 Ordinary = FirstOrdinaryDungeon(*Bound.Run);
	if (!TestTrue(TEXT("the wave holds a dungeon at the ordinary walk rate"),
				  Ordinary != INDEX_NONE))
	{
		return false;
	}

	const int32 DungeonId = Bound.Run->Dungeons[Ordinary].DungeonId;
	const int32 Floors = Bound.Run->Dungeons[Ordinary].Floors;
	const int32 DayBefore = Bound.Run->Day();

	// ENTERING IS A FLOOR. The player is standing on floor 1 and a floor costs a
	// day to begin with, which is what makes walking N floors cost N days in an
	// uninvested run like this one.
	if (!TestTrue(TEXT("the dungeon is entered"),
				  Bound.Mode->EnterEmpireDungeon(DungeonId)))
	{
		return false;
	}

	// OWED, NOT RUN, since 2026-10-24: nothing resolves while the player is
	// inside a dungeon, by the owner's decision, so the run's day stands still
	// and the walk's days are counted until the player leaves.
	TestEqual(TEXT("arriving on floor 1 owed a day"), Bound.Run->DaysOwed(), 1);
	TestEqual(TEXT("and the run's day did not move"), Bound.Run->Day(), DayBefore);
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
		const int32 Before = Bound.Run->DaysOwed();

		TestTrue(FString::Printf(TEXT("descent %d works"), Down),
				 Bound.Mode->GoDownOneFloor());
		TestEqual(FString::Printf(TEXT("descent %d owed exactly one day"), Down),
				  Bound.Run->DaysOwed(), Before + 1);
		TestEqual(FString::Printf(TEXT("and descent %d did not move the run's day"), Down),
				  Bound.Run->Day(), DayBefore);
	}

	// N FLOORS COST N DAYS AT THE STARTING RATE, which is what this run has: no
	// city here bought the upgrade that shortens a walk, and no empire tree
	// exists yet. A city that had bought one would make the same dungeon cost
	// fewer days WITHOUT changing its floor count, its reward or its timer --
	// that is `FCataclysmDungeon::WalkDays`, and it has its own tests.
	TestEqual(FString::Printf(
		TEXT("walking %d floors of a %d floor dungeon owed %d days"),
		Descents + 1, Floors, Descents + 1),
		Bound.Run->DaysOwed(), Descents + 1);

	TestEqual(TEXT("and the floor being walked is the last one"),
			  Bound.Mode->FloorNumber, Floors);

	// AND LEAVING RUNS THEM. The days the walk owed are the days that pass.
	Bound.Mode->LeaveEmpireDungeon();
	TestEqual(FString::Printf(
		TEXT("after leaving, walking %d floors cost %d days"), Descents + 1, Descents + 1),
		Bound.Run->Day(), DayBefore + Descents + 1);
	TestEqual(TEXT("and nothing is owed any more"), Bound.Run->DaysOwed(), 0);

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

	// AN ORDINARY DUNGEON TO WALK, so that five floors down really is five days.
	// A Cow Level would spend ten and the check below would fail for a reason
	// that has nothing to do with which timer stops.
	const int32 Ordinary = FirstOrdinaryDungeon(*Bound.Run);
	if (!TestTrue(TEXT("the wave holds a dungeon at the ordinary walk rate"),
				  Ordinary != INDEX_NONE))
	{
		return false;
	}

	const int32 Walked = Bound.Run->Dungeons[Ordinary].DungeonId;
	const int32 Other =
		Bound.Run->Dungeons[Ordinary == 0 ? 1 : 0].DungeonId;

	if (!TestTrue(TEXT("the other dungeon is a different one"),
				  Other != Walked))
	{
		return false;
	}

	Bound.Mode->EnterEmpireDungeon(Walked);

	const float WalkedBefore = Bound.Run->Clock->DaysUntilResolveFor(Walked);
	const float OtherBefore = Bound.Run->Clock->DaysUntilResolveFor(Other);

	// THE DAY ARRIVING COST IS OWED TOO, since 2026-10-24, and it runs with the
	// descents' days when the player leaves. Read rather than assumed.
	const int32 OwedOnArriving = Bound.Run->DaysOwed();

	// FIVE FLOORS DOWN, WHICH IS FIVE DAYS.
	for (int32 Down = 0; Down < 5; ++Down)
	{
		Bound.Mode->GoDownOneFloor();
	}

	// LEAVING RUNS THE WALK'S DAYS, since 2026-10-24: nothing resolves and no
	// timer moves while the player is inside, by the owner's decision. So both
	// timers are read after it.
	Bound.Mode->LeaveEmpireDungeon();

	// THE ONE BEING WALKED DOES NOT COUNT DOWN. Its residents are busy fighting
	// the player rather than marching on the city, so entering is a guaranteed
	// save rather than a gamble. See `UCataclysmDayClock::bTimerTicksWhileRunning`.
	// It stays paused through the walk's days, which run before the clock is
	// told the player has left, ruled by the coordinating session on 2026-10-24.
	TestEqual(TEXT("the timer of the dungeon being walked has not moved"),
			  Bound.Run->Clock->DaysUntilResolveFor(Walked), WalkedBefore, 0.001f);

	// AND EVERY OTHER ONE DOES. That is what walking a dungeon costs: five days
	// in here is five days every other timer advanced without you, and the day
	// arriving cost is a sixth.
	TestEqual(FString::Printf(TEXT("and another dungeon's timer lost the five descents' days and the %d of arriving"),
							  OwedOnArriving),
			  Bound.Run->Clock->DaysUntilResolveFor(Other), OtherBefore - 5.0f - OwedOnArriving,
			  0.001f);

	// LEAVING STARTS IT AGAIN, and the dungeon is still standing.
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

	// AN ORDINARY DUNGEON. A Cow Level takes twice as long to walk, which gives
	// a later surge time to land while the player is underground and makes the
	// count of what is standing afterwards say nothing.
	const int32 Ordinary = FirstOrdinaryDungeon(*Bound.Run);
	if (!TestTrue(TEXT("the wave holds a dungeon at the ordinary walk rate"),
				  Ordinary != INDEX_NONE))
	{
		return false;
	}

	const FCataclysmDungeon First = Bound.Run->Dungeons[Ordinary];
	const int32 DungeonId = First.DungeonId;
	const int32 CityId = First.CityId;
	const int32 Floors = First.Floors;

	// WHICH DUNGEONS WERE STANDING, NOT HOW MANY. A surge that lands while the
	// player is underground adds one, and a count would then fail for a reason
	// that has nothing to do with clearing.
	TSet<int32> StandingBefore;
	TMap<int32, int32> CityBefore;
	TSet<int32> QuestBefore;
	for (const FCataclysmDungeon& Dungeon : Bound.Run->Dungeons)
	{
		StandingBefore.Add(Dungeon.DungeonId);
		CityBefore.Add(Dungeon.DungeonId, Dungeon.CityId);
		if (Dungeon.Type == ECataclysmDungeonType::Quest)
		{
			QuestBefore.Add(Dungeon.DungeonId);
		}
	}

	const int32 ReportsBeforeTheWalk = Bound.Mode->DaysSpentWalking.Num();

	Bound.Mode->EnterEmpireDungeon(DungeonId);

	// THE RECORD OF THE WALK'S DAYS IS COMPLETE, or the ruling below reads a partial one. Since 2026-10-24 the walk's
	// days run when the player leaves, and `LeaveEmpireDungeon` writes their reports to the record then; the day
	// arriving cost is among them.
	const int32 DayBeforeTheDescents = Bound.Run->Day();
	const int32 ReportsBeforeTheDescents = Bound.Mode->DaysSpentWalking.Num();

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
	const int32 OwedAtTheBottom = Bound.Run->DaysOwed();

	TestEqual(TEXT("no day ran during the descents"), DayAtTheBottom, DayBeforeTheDescents);
	TestEqual(TEXT("and none was written to the walk's record"),
			  Bound.Mode->DaysSpentWalking.Num() - ReportsBeforeTheDescents, 0);

	// AND THE NEXT DESCENT IS BEATING IT, NOT FINDING ANOTHER FLOOR.
	TestFalse(TEXT("there is no floor below the last one"),
			  Bound.Mode->GoDownOneFloor());

	// BEATING IT RUNS THE DAYS THE WALK OWED, after the clear is credited, and no further day.
	TestEqual(TEXT("so the days the walk owed ran and no further day was spent"),
			  Bound.Run->Day(), DayAtTheBottom + OwedAtTheBottom);
	TestEqual(TEXT("the walk's record holds one day report for each day the walk owed"),
			  Bound.Mode->DaysSpentWalking.Num() - ReportsBeforeTheDescents, OwedAtTheBottom);
	TestEqual(TEXT("and no deeper floor was built"),
			  Bound.Mode->FloorNumber, Floors);

	// THE DUNGEON IS GONE FROM BOTH LISTS.
	TestNull(TEXT("the dungeon is off the map"),
			 Bound.Run->FindDungeon(DungeonId));
	TestEqual(TEXT("and off the clock"),
			  Bound.Run->Clock->DaysUntilResolveFor(DungeonId), -1.0f);
	// AND IT IS THE ONLY ONE THAT WENT. Every other dungeon that was standing
	// still is, which a count could not tell from one arriving as another left.
	//
	// UNLESS THE WALK'S OWN DAY REPORTS SAY IT LEFT BY ITS OWN HIT. Since 2026-10-13 an ordinary dungeon whose timer
	// runs out hits its city once and leaves the map, so a long walk takes dungeons off it that the walk never
	// touched. Ruled by the coordinating session on 2026-10-21: such a dungeon counts as left alone ONLY IF a report
	// of a day the walk spent names it -- its timer in `Resolved`, its city in `HitAndLeft`, and it not in
	// `Absorbed`. Any other dungeon missing still fails "dungeon N was left alone", and the message says what the
	// reports give for it.
	//
	// OR THAT IT WENT WITH ITS CITY'S FALL. Ruled by the coordinating session on 2026-10-22, after the second window
	// found dungeons 6 and 7 absorbed when city 8 fell on day 25: a dungeon also counts as left alone when a report
	// of a day the walk spent lists it in `Absorbed` and its pre-walk city in `Fallen`. An absorption whose city is
	// not in that report's `Fallen` still fails.
	for (const int32 Id : StandingBefore)
	{
		if (Id == DungeonId)
		{
			continue;
		}

		if (Bound.Run->FindDungeon(Id) != nullptr)
		{
			TestNotNull(*FString::Printf(
							TEXT("dungeon %d was left alone"), Id),
						Bound.Run->FindDungeon(Id));
			continue;
		}

		const int32 ItsCity = CityBefore.FindRef(Id);
		bool bLeftByItsHitOrItsCitysFall = false;
		TArray<FString> Reasons;
		for (int32 Index = ReportsBeforeTheWalk; Index < Bound.Mode->DaysSpentWalking.Num(); ++Index)
		{
			const FCataclysmDayReport& Report = Bound.Mode->DaysSpentWalking[Index];
			const bool bResolved = Report.Resolved.Contains(Id);
			const bool bAbsorbed = Report.Absorbed.Contains(Id);
			if (!bResolved && !bAbsorbed)
			{
				continue;
			}

			if (bAbsorbed && !Report.Fallen.Contains(ItsCity))
			{
				Reasons.Add(FString::Printf(
					TEXT("day %d: absorbed but city %d is not in Fallen"), Report.Day, ItsCity));
			}
			else if (bResolved && bAbsorbed)
			{
				bLeftByItsHitOrItsCitysFall = true;
				Reasons.Add(FString::Printf(
					TEXT("day %d: timer ran out and it was absorbed when city %d fell"), Report.Day, ItsCity));
			}
			else if (bAbsorbed)
			{
				bLeftByItsHitOrItsCitysFall = true;
				Reasons.Add(FString::Printf(TEXT("day %d: absorbed when city %d fell"), Report.Day, ItsCity));
			}
			else if (Report.HitAndLeft.Contains(ItsCity))
			{
				bLeftByItsHitOrItsCitysFall = true;
				Reasons.Add(FString::Printf(
					TEXT("day %d: timer ran out, hit city %d and left"), Report.Day, ItsCity));
			}
			else
			{
				Reasons.Add(FString::Printf(
					TEXT("day %d: timer ran out but city %d is not in HitAndLeft"), Report.Day, ItsCity));
			}
		}

		FString Reason = Reasons.Num() > 0 ? FString::Join(Reasons, TEXT("; ")) : FString(TEXT("no report names it"));
		if (QuestBefore.Contains(Id))
		{
			Reason += TEXT(" (a Quest dungeon)");
		}

		TestTrue(*FString::Printf(
					 TEXT("dungeon %d was left alone (missing after the walk: %s)"), Id, *Reason),
				 bLeftByItsHitOrItsCitysFall);
	}
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
	"Cataclysm.DungeonMode.ClearingAWholeWaveCostsWhatItsDungeonsCostToWalk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDungeonWholeWaveTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDungeonCostsDaysTest;

	// THE POINT OF THE WHOLE JOIN, MEASURED END TO END. A player who walks a
	// wave spends its floors in days, and that is the decision the strategy
	// layer exists to pose: these dungeons, or those cities.
	//
	// WHAT THIS TEST DELIBERATELY DOES NOT CLAIM. An earlier version of it
	// asserted that a city falls while the player is underground. This one
	// asserts neither that nor its opposite. Since 2026-10-13 an ordinary
	// dungeon hits its city when its timer runs out and leaves the map. An
	// Outpost dungeon's timer is about 19 to 39 days, and walking the opening
	// wave's twelve dungeons, an ordinary one 8 to 15 floors and a Quest one
	// 20 to 30 on an Outpost, costs about 138 days if all twelve are ordinary
	// at the typical 11.5 floors, and more for each Quest one, against about
	// 44 for the four ordinary dungeons of the wave of five before
	// 2026-10-20. So a dungeon of the wave can be gone before the player
	// reaches it, and a city can fall while the player is inside one of its
	// dungeons. The
	// test counts each case rather than asserting any of them: claiming one
	// would have been a test that passed only on the seeds where it happened.
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

	// THE WAVE ITSELF, BY IDENTIFIER. Clearing it takes weeks and a later surge
	// lands during them, so "keep going until the map is empty" clears more than
	// the wave and the arithmetic below would be about a different set of
	// dungeons than the one it names.
	TArray<int32> Wave;
	for (const FCataclysmDungeon& Dungeon : Bound.Run->Dungeons)
	{
		Wave.Add(Dungeon.DungeonId);
	}

	const int32 WaveSize = Wave.Num();

	int32 FloorsWalked = 0;
	int32 Cleared = 0;

	// TWO WAYS A DUNGEON OF THE WAVE IS LOST, COUNTED APART. Ruled by the
	// coordinating session under the owner's delegation, 2026-10-15.
	//
	// GONE BEFORE ITS TURN: it was not on the map when its turn came, so it
	// was never walked. Since 2026-10-13 that counts an ordinary dungeon that
	// hit its city and left, as well as one a city's fall absorbed.
	//
	// ABSORBED WHILE WALKED: it was on the map when it was entered, and the
	// run no longer had it when its walk ended, because its city fell while
	// the player was inside. Its walk still goes to its bottom and costs its
	// floors at the rate recorded on entry, and its clear counts for nothing.
	// Issue #2414. Its own timer does not run while it is walked, so it cannot
	// hit its city and leave in that time.
	int32 GoneBeforeItsTurn = 0;
	int32 AbsorbedWhileWalked = 0;

	//: What the dungeons actually walked cost, added up as they are reached.
	//
	// **NOT WORKED OUT UP FRONT, BECAUSE A DUNGEON CAN GO BEFORE ITS TURN.**
	// Clearing the wave takes weeks, and a city that falls in the meantime
	// absorbs every dungeon standing on it -- including ones the player has not
	// reached. The first version of this test asked for a total before it
	// started and failed when dungeon 3 was no longer there to walk.
	int32 DaysWalkingCost = 0;
	int32 FloorsToWalk = 0;

	// ENTER, WALK TO THE BOTTOM, BEAT IT, TAKE THE NEXT.
	for (const int32 DungeonId : Wave)
	{
		const FCataclysmDungeon* Standing = Bound.Run->FindDungeon(DungeonId);
		if (Standing == nullptr)
		{
			// GONE BEFORE ITS TURN: it hit its city and left, or its city fell
			// and absorbed it. It is off the map either way, which is what the
			// check after this loop asks.
			++GoneBeforeItsTurn;
			continue;
		}

		const int32 Floors = Standing->Floors;

		// WHAT THIS ONE COSTS TO WALK, WHICH IS NOT ALWAYS ITS FLOOR COUNT. One
		// floor costs one day as a starting rate; a Cow Level costs two, and
		// this wave may hold one now that every dungeon has a sub-type.
		DaysWalkingCost += FMath::RoundToInt(Standing->WalkDays);
		FloorsToWalk += Floors;

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

		// BOUNDED BY THE DEPTH READ BEFORE ENTRY, as
		// `ReachingTheBottomTakesTheDungeonOffTheMap` walks. A dungeon of
		// `Floors` floors needs `Floors - 1` descents, so its last floor is
		// found on pass `Floors` at the latest, and the bound ends the walk
		// only when the game never says the last floor was reached. Before
		// issue #2414 a walk whose city fell had no last floor, and the loop
		// that stood here walked until the editor ran out of memory.
		bool bReachedTheLastFloor = false;
		for (int32 Descent = 1; Descent <= Floors; ++Descent)
		{
			if (Bound.Mode->IsOnTheLastFloor())
			{
				bReachedTheLastFloor = true;
				break;
			}
			if (!Bound.Mode->GoDownOneFloor())
			{
				break;
			}
			++FloorsWalked;
		}

		TestTrue(FString::Printf(
			TEXT("the walk of dungeon %d stopped on its last floor and not at the bound of %d passes"),
			DungeonId, Floors),
			bReachedTheLastFloor);

		TestEqual(FString::Printf(
			TEXT("dungeon %d was walked to its bottom floor %d"),
			DungeonId, Floors),
			Bound.Mode->FloorNumber, Floors);

		// ASKED AT THE END OF THE WALK, BEFORE THE DESCENT THAT BEATS IT:
		// whether the run still has it. A dungeon whose city fell while the
		// player was inside is gone by now, and beating it counts for nothing.
		const bool bAbsorbedWhileWalked = Bound.Run->FindDungeon(DungeonId) == nullptr;

		// THE DESCENT PAST THE LAST FLOOR IS BEATING IT.
		Bound.Mode->GoDownOneFloor();
		if (bAbsorbedWhileWalked)
		{
			++AbsorbedWhileWalked;
		}
		else
		{
			++Cleared;
		}

		TestNull(FString::Printf(TEXT("dungeon %d is off the map"), DungeonId),
				 Bound.Run->FindDungeon(DungeonId));
	}

	AddInfo(FString::Printf(
		TEXT("a wave of %d: %d cleared, %d gone before their turn (hit their "
			 "city and left, or absorbed by its fall), %d absorbed while "
			 "walked; %d floors walked over %d days"),
		WaveSize, Cleared, GoneBeforeItsTurn, AbsorbedWhileWalked, FloorsWalked,
		DaysWalkingCost));

	// EVERY DUNGEON OF THE WAVE IS OFF THE MAP, one way or another. It was
	// cleared, gone before its turn, or absorbed while walked, and the counts
	// above say which happened to each.
	TestEqual(TEXT("every dungeon of the wave was accounted for: cleared, gone before its turn, or absorbed while walked"),
			  Cleared + GoneBeforeItsTurn + AbsorbedWhileWalked, WaveSize);

	for (const int32 DungeonId : Wave)
	{
		TestNull(*FString::Printf(TEXT("dungeon %d is off the map"), DungeonId),
				 Bound.Run->FindDungeon(DungeonId));
	}

	// AND AT LEAST ONE OF IT WAS WALKED. A run where every dungeon vanished
	// before the player reached it would satisfy the line above while
	// measuring nothing about walking. The walks are not compared with the
	// dungeons gone before their turn: since 2026-10-13 an ordinary dungeon can
	// be gone because it hit its city and left, which walking cannot prevent.
	// Ruled by the coordinating session, 2026-10-15.
	TestTrue(TEXT("at least one dungeon of the wave was walked"),
			 Cleared + AbsorbedWhileWalked >= 1);

	// AND THE DAY MOVED BY EXACTLY WHAT THOSE WALKS COST, because nothing in
	// this run has shortened one. One floor costs one day as a starting rate,
	// not as an invariant: a Cow Level costs two days a floor, so the days spent
	// are the sum of each dungeon's own walk cost rather than the floor count.
	// Depth and reward are the same axis; depth and time are not, once a player
	// has invested or a sub-type has intervened.
	//
	// A DUNGEON ABSORBED WHILE WALKED IS IN THAT SUM. Its walk cost its floors
	// at the rate recorded on entry, issue #2414, so it is counted among the
	// dungeons walked although its clear was not.
	TestEqual(FString::Printf(
		TEXT("walking %d floors across %d dungeons cost %d days"),
		FloorsWalked, Cleared + AbsorbedWhileWalked, DaysWalkingCost),
		Bound.Run->Day(), StartedOnDay + DaysWalkingCost);

	// AND EVERY FLOOR OF THOSE DUNGEONS WAS ACTUALLY WALKED. A run whose day
	// cost was right while half its floors went unvisited would satisfy the line
	// above.
	TestEqual(TEXT("and the floors walked are those dungeons' floors"),
			  FloorsWalked, FloorsToWalk);

	// A WAVE IS WEEKS OF WORK, not an afternoon. Outpost dungeons are 8 to 15
	// floors each, which is what makes ignoring one a real decision rather than
	// an obvious mistake. A dungeon absorbed while walked was walked to its
	// bottom too, so it is counted with the ones cleared.
	TestTrue(FString::Printf(
		TEXT("walking what was left of the wave took %d floors"), FloorsWalked),
		FloorsWalked >= (Cleared + AbsorbedWhileWalked) * 8);

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
	// hoped for. Cow Level is 7 in 100, so most seeds are not one, and a run is
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

	// THE DAYS THE WALK OWES COUNT, since 2026-10-24: they run when the player
	// leaves, and this walk stops on the last floor without leaving.
	TestEqual(TEXT("walking the whole Cow Level cost two days a floor"),
			  Bound.Run->Day() + Bound.Run->DaysOwed() - DayBefore, Floors * 2);

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
			  Plain.Run->Day() + Plain.Run->DaysOwed() - PlainDayBefore, PlainFloors);

	return true;
}

// ---------------------------------------------------------------------------
// Dying ends the dungeon. Issue #41
// ---------------------------------------------------------------------------

/**
 * Tests for what a death of the player's character does to the dungeon of the
 * empire being walked.
 *
 * WHERE EVERYBODY STANDS, IN EVERY TEST BELOW. One character: the player, a
 * possessed player character, stood at the entrance of the floor being walked
 * by `PlaceAtEntrance`. The floor's own creatures are cleared away first, so
 * nothing else stands on the floor and no two characters share a spot.
 *
 * THE PLAYER IS A REAL ONE, a level 20 Ravager with its class lines, so no test
 * here asserts a figure of its own: health is compared with its own maximum.
 *
 * NOTHING HERE WAITS. A death is dealt by `ReduceHealthDirectly`, which no
 * roll touches, and the character is stood back up by calling `Revive`, because
 * a timer never fires in a test world.
 *
 * EVERY DUNGEON IS PUT ON THE MAP BY HAND, on a run whose opening wave has been
 * cleared away, so the only thing that can cost a city anything is the dungeon
 * the test placed, and its damage and its timer are the figures the test gave.
 *
 * NO TEST ASSERTS WHAT HAS HAPPENED BETWEEN THE DEATH AND STANDING BACK UP. The
 * dungeon is ended by `Revive`; each test reads the empire after it.
 */
namespace CataclysmDeathEndsDungeonTest
{
	/** The playtest switch, by the name a person types at the console. */
	const TCHAR* const PlaytestSwitchName =
		TEXT("Cataclysm.DeathKeepsThePlayerInTheDungeon");

	/**
	 * Sets the playtest switch for as long as it is in scope, and puts it back.
	 *
	 * AT THE CONSOLE'S OWN PRIORITY, for the reason `FScopedConsoleInt` in
	 * `CataclysmDungeonGameModeTests.cpp` gives. A copy named for this file,
	 * because that one lives in another file's namespace.
	 */
	struct FScopedDeathSwitch
	{
		explicit FScopedDeathSwitch(int32 Value)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(PlaytestSwitchName);
			if (Variable)
			{
				Before = Variable->GetInt();
				Variable->Set(Value, ECVF_SetByConsole);
			}
		}

		~FScopedDeathSwitch()
		{
			if (Variable)
			{
				Variable->Set(Before, ECVF_SetByConsole);
			}
		}

		IConsoleVariable* Variable = nullptr;
		int32 Before = 0;
	};

	/** A world, a possessed player, a begun dungeon game mode and an empire run. */
	struct FDeathScene
	{
		UWorld* World = nullptr;
		ACataclysmDungeonGameMode* Mode = nullptr;
		UCataclysmEmpireRun* Run = nullptr;
		ACataclysmPlayerCharacter* Player = nullptr;
		UAbilitySystemComponent* AbilitySystem = nullptr;

		bool IsUsable() const
		{
			return World && Mode && Run && Run->Map && Run->Clock && Player
				&& AbilitySystem;
		}
	};

	/**
	 * The scene every test below starts from.
	 *
	 * THE RUN HAS NOTHING STANDING ON IT. A day is spent, which lands the
	 * opening wave, and both lists are then emptied, the way `MakeEmptyRun` in
	 * the empire layer's own tests does and for its reason: the next surge is
	 * 90 days off, further than any test here goes.
	 *
	 * THE GAME MODE HAS BEGUN PLAY, so it listens for deaths as it does in the
	 * running game, and the run is handed to it through the test seam.
	 *
	 * @param LethalityRung 0 Standard, 1 Hardcore, 2 Heretic.
	 */
	FDeathScene MakeScene(int32 LethalityRung = 0)
	{
		FDeathScene Out;

		Out.World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Out.World)
		{
			return Out;
		}

		ACataclysmPlayerState* PlayerState =
			Out.World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = Out.World->SpawnActor<APlayerController>();
		Out.Player = Out.World->SpawnActor<ACataclysmPlayerCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
		if (!PlayerState || !Controller || !Out.Player)
		{
			return Out;
		}
		Controller->SetPlayerState(PlayerState);
		Controller->Possess(Out.Player);
		Out.AbilitySystem = Out.Player->GetAbilitySystemComponent();

		Out.Mode = Out.World->SpawnActor<ACataclysmDungeonGameMode>();
		if (!Out.Mode)
		{
			return Out;
		}
		Out.Mode->StartPlay();

		Out.Run = NewObject<UCataclysmEmpireRun>();
		Out.Run->Begin(1, ECataclysmSurgeMode::Static, LethalityRung);
		Out.Run->AdvanceDay();
		Out.Run->Dungeons.Empty();
		if (Out.Run->Clock)
		{
			Out.Run->Clock->Timers.Empty();
		}
		Out.Mode->SetEmpireRunForTests(Out.Run);

		return Out;
	}

	/** The first Outpost of the map that has not fallen, or `INDEX_NONE`. */
	int32 AnOutpostOf(const UCataclysmEmpireRun& Run)
	{
		for (const FCataclysmCity& City : Run.Map->Cities)
		{
			if (City.Tier == ECataclysmCityTier::Outpost && !City.bFallen)
			{
				return City.CityId;
			}
		}
		return INDEX_NONE;
	}

	/**
	 * Puts one dungeon on a city by hand, with its timer, and answers its number.
	 *
	 * BOTH LISTS, as a surge fills them: the dungeon on the run and its timer on
	 * the clock. `FullTimerDays` is what the timer refills to and `DaysLeft` is
	 * what it has left now, written straight onto the timer because the clock
	 * has no call that sets one without the other.
	 *
	 * THE DAMAGE IS THE CALLER'S, for every kind. A Quest dungeon and a Dungeon
	 * City carry none in the game; a test gives them some so that a resolve
	 * reaching them by mistake would show on the city.
	 */
	int32 PlaceByHand(UCataclysmEmpireRun& Run, ECataclysmDungeonType Type,
					  int32 CityId, int32 Floors, float FullTimerDays,
					  float DaysLeft, float DefencePoints, float PopulationPoints)
	{
		const FCataclysmCity* City = Run.Map->Find(CityId);
		if (City == nullptr || Run.Clock == nullptr)
		{
			return INDEX_NONE;
		}

		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = Run.NextDungeonId;
		Dungeon.Type = Type;
		Dungeon.SubType = ECataclysmDungeonSubType::None;
		Dungeon.CityId = CityId;
		Dungeon.CityTier = City->Tier;
		Dungeon.Floors = Floors;
		Dungeon.ResolveDays = FullTimerDays;
		Dungeon.SpawnedDay = Run.Day();
		Dungeon.DefenceDamage = DefencePoints;
		Dungeon.PopulationDamage = PopulationPoints;

		Run.NextDungeonId = Dungeon.DungeonId + 1;
		Run.Dungeons.Add(Dungeon);

		if (!Run.Clock->AddDungeon(Dungeon.DungeonId, Floors)
			|| !Run.Clock->SetResolveDays(Dungeon.DungeonId, FullTimerDays))
		{
			return INDEX_NONE;
		}

		for (FCataclysmDungeonTimer& Counting : Run.Clock->Timers)
		{
			if (Counting.DungeonId == Dungeon.DungeonId)
			{
				Counting.DaysUntilResolve = DaysLeft;
			}
		}

		return Dungeon.DungeonId;
	}

	/** Every city's defence added up, so a bite on any city shows. */
	float DefenceOfEveryCity(const UCataclysmEmpireRun& Run)
	{
		float Total = 0.0f;
		for (const FCataclysmCity& City : Run.Map->Cities)
		{
			Total += City.Defence;
		}
		return Total;
	}

	/** Every city's population added up. */
	float PopulationOfEveryCity(const UCataclysmEmpireRun& Run)
	{
		float Total = 0.0f;
		for (const FCataclysmCity& City : Run.Map->Cities)
		{
			Total += City.Population;
		}
		return Total;
	}

	/** How many times the clock says this dungeon has resolved, or -1. */
	int32 TimesResolvedOf(const UCataclysmEmpireRun& Run, int32 DungeonId)
	{
		const FCataclysmDungeonTimer* Counting =
			Run.Clock ? Run.Clock->FindTimer(DungeonId) : nullptr;
		return Counting ? Counting->TimesResolved : -1;
	}

	/**
	 * Enters the dungeon, clears its first floor of creatures and stands the
	 * player at that floor's entrance. Each step is asserted as set-up.
	 */
	bool EnterAndStand(FAutomationTestBase& Test, const FDeathScene& Scene,
					   int32 DungeonId)
	{
		if (!Test.TestTrue(TEXT("set-up: the dungeon was entered"),
						   Scene.Mode->EnterEmpireDungeon(DungeonId)))
		{
			return false;
		}

		Scene.Mode->ClearFloorEnemies();

		return Test.TestTrue(TEXT("set-up: the player stands at the floor's entrance"),
							 Scene.Mode->PlaceAtEntrance(Scene.Player))
			&& Test.TestEqual(TEXT("set-up: the game mode is walking that dungeon"),
							  Scene.Mode->EmpireDungeonId, DungeonId);
	}

	/** Kills the player the way the dungeon rule tests do. Asserted as set-up. */
	bool KillThePlayer(FAutomationTestBase& Test, const FDeathScene& Scene)
	{
		UCataclysmSkillEffects::ReduceHealthDirectly(Scene.Player, Scene.Player,
													 1000000.0f);
		return Test.TestTrue(TEXT("set-up: the player died"),
							 UCataclysmSkillEffects::IsDead(Scene.Player));
	}

	/**
	 * Stands the player back up by hand, which is what the respawn timer calls.
	 * A world built for a test is never ticked, so that timer never fires.
	 */
	bool StandBackUp(FAutomationTestBase& Test, const FDeathScene& Scene)
	{
		Scene.Player->Revive();
		return Test.TestFalse(TEXT("set-up: the player stood back up"),
							  UCataclysmSkillEffects::IsDead(Scene.Player));
	}

	/** What one case left behind it, for comparing a death with a control. */
	struct FAfterTheDays
	{
		float DefenceInside = 0.0f;
		float Defence = 0.0f;
		float PopulationInside = 0.0f;
		float Population = 0.0f;
		float TimerLeft = 0.0f;
		int32 CityOfTheDungeon = INDEX_NONE;
		int32 TimesResolved = 0;
		int32 Detonated = 0;
		int32 DaysPassed = 0;
		int32 BoundAfter = 0;
	};

	/**
	 * One case, in a world of its own: a dungeon of the given kind carrying 100
	 * points of defence damage and 50 of population damage, entered, and then
	 * either a death and standing back up, or the control.
	 *
	 * THE CONTROL IS THE SAME DAYS WITH NO DEATH: the dungeon is left by
	 * `LeaveEmpireDungeon` and the run is advanced by `DaysForTheControl`.
	 * Both cases draw the same numbers from the run's chance up to that point,
	 * because the run is begun from one seed and does the same things.
	 */
	bool RunOneCase(FAutomationTestBase& Test, ECataclysmDungeonType Type,
					float FullTimerDays, float DaysLeft, bool bThePlayerDies,
					int32 DaysForTheControl, FAfterTheDays& Out)
	{
		FDeathScene Scene = MakeScene(0);
		if (!Test.TestNotNull(TEXT("a test world was created"), Scene.World))
		{
			return false;
		}
		ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		if (!Test.TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
		{
			return false;
		}

		const int32 CityId = AnOutpostOf(*Scene.Run);
		const int32 DungeonId = PlaceByHand(*Scene.Run, Type, CityId, 20,
											FullTimerDays, DaysLeft, 100.0f, 50.0f);
		if (!Test.TestTrue(TEXT("set-up: a dungeon was placed on an Outpost"),
						   DungeonId != INDEX_NONE)
			|| !EnterAndStand(Test, Scene, DungeonId))
		{
			return false;
		}

		const int32 DayInside = Scene.Run->Day() + Scene.Run->DaysOwed(); // the day the walk has reached; its days run on leaving
		Out.DefenceInside = DefenceOfEveryCity(*Scene.Run);
		Out.PopulationInside = PopulationOfEveryCity(*Scene.Run);

		if (bThePlayerDies)
		{
			if (!KillThePlayer(Test, Scene) || !StandBackUp(Test, Scene))
			{
				return false;
			}
		}
		else
		{
			Scene.Mode->LeaveEmpireDungeon();
			Scene.Run->AdvanceDays(DaysForTheControl);
		}

		const FCataclysmDungeon* Standing = Scene.Run->FindDungeon(DungeonId);

		Out.Defence = DefenceOfEveryCity(*Scene.Run);
		Out.Population = PopulationOfEveryCity(*Scene.Run);
		Out.TimerLeft = Scene.Run->Clock->DaysUntilResolveFor(DungeonId);
		Out.CityOfTheDungeon = Standing ? Standing->CityId : INDEX_NONE;
		Out.TimesResolved = TimesResolvedOf(*Scene.Run, DungeonId);
		Out.Detonated = Scene.Run->DungeonsDetonated;
		Out.DaysPassed = Scene.Run->Day() - DayInside;
		Out.BoundAfter = Scene.Mode->EmpireDungeonId;

		return true;
	}
}

// ---------------------------------------------------------------------------
// What a death costs in days
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathCostsTheModesDaysTest,
	"Cataclysm.DungeonMode.ADeathInAnEmpireDungeonCostsTheDaysOfItsLethalityMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDeathCostsTheModesDaysTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDeathEndsDungeonTest;

	// THE DESIGNED FIGURES, WRITTEN OUT. `docs/Cataclysm_GDD_v2.md` section II:
	// dying costs 5 days in Standard, 10 in Hardcore and 15 in Heretic. Not read
	// from `DeathDayCostFor`, which would compare the code with itself.
	const int32 DesignedCost[3] = {5, 10, 15};
	const TCHAR* const ModeName[3] =
		{TEXT("Standard"), TEXT("Hardcore"), TEXT("Heretic")};

	for (int32 Rung = 0; Rung < 3; ++Rung)
	{
		// THE DEATH. An ordinary dungeon twenty floors deep with a hundred days
		// on its timer and no damage, so the days are the only thing that moves.
		{
			FDeathScene Scene = MakeScene(Rung);
			if (!TestNotNull(TEXT("a test world was created"), Scene.World))
			{
				return false;
			}
			ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

			if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
			{
				return false;
			}

			const int32 DungeonId = PlaceByHand(
				*Scene.Run, ECataclysmDungeonType::Basic, AnOutpostOf(*Scene.Run),
				20, 100.0f, 100.0f, 0.0f, 0.0f);
			if (!TestTrue(TEXT("set-up: a dungeon was placed on an Outpost"),
						  DungeonId != INDEX_NONE)
				|| !EnterAndStand(*this, Scene, DungeonId))
			{
				return false;
			}

			const int32 DayInside = Scene.Run->Day() + Scene.Run->DaysOwed(); // the day the walk has reached; its days run on leaving

			if (!KillThePlayer(*this, Scene) || !StandBackUp(*this, Scene))
			{
				return false;
			}

			TestEqual(FString::Printf(
				TEXT("%s: a death in the dungeon cost %d days"),
				ModeName[Rung], DesignedCost[Rung]),
				Scene.Run->Day() - DayInside, DesignedCost[Rung]);
		}

		// THE CONTROL. The same run and the same dungeon, and nobody dies. The
		// player is "stood back up" anyway, which for a living character must do
		// nothing at all.
		{
			FDeathScene Scene = MakeScene(Rung);
			if (!TestNotNull(TEXT("a second test world was created"), Scene.World))
			{
				return false;
			}
			ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

			if (!TestTrue(TEXT("set-up: the control's scene was built"),
						  Scene.IsUsable()))
			{
				return false;
			}

			const int32 DungeonId = PlaceByHand(
				*Scene.Run, ECataclysmDungeonType::Basic, AnOutpostOf(*Scene.Run),
				20, 100.0f, 100.0f, 0.0f, 0.0f);
			if (!TestTrue(TEXT("set-up: the control's dungeon was placed"),
						  DungeonId != INDEX_NONE)
				|| !EnterAndStand(*this, Scene, DungeonId))
			{
				return false;
			}

			const int32 DayInside = Scene.Run->Day();

			Scene.Player->Revive();

			TestEqual(FString::Printf(
				TEXT("CONTROL, %s: with no death no day passed"), ModeName[Rung]),
				Scene.Run->Day() - DayInside, 0);
			TestEqual(FString::Printf(
				TEXT("CONTROL, %s: and the dungeon is still being walked"),
				ModeName[Rung]),
				Scene.Mode->EmpireDungeonId, DungeonId);
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// An ordinary dungeon resolves, once
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathResolvesOnceTest,
	"Cataclysm.DungeonMode.ADeathInAnOrdinaryDungeonHitsItsCityOnceAndTheDungeonLeavesTheMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDeathResolvesOnceTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDeathEndsDungeonTest;

	// A FULL TIMER OF FORTY DAYS WITH TWO LEFT. Two is fewer than the five days
	// a Standard death costs, so a death that resolved the dungeon and left it
	// on the map with its timer alone would resolve it a second time two days
	// into the charge. Since 2026-10-13 the dungeon leaves the map at the
	// death, so there is no timer for those days to run out.
	const float FullTimer = 40.0f;
	const float DaysLeft = 2.0f;

	// THE CONTROL: the same dungeon entered and left with nobody dying.
	{
		FDeathScene Scene = MakeScene(0);
		if (!TestNotNull(TEXT("a test world was created"), Scene.World))
		{
			return false;
		}
		ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		if (!TestTrue(TEXT("set-up: the control's scene was built"), Scene.IsUsable()))
		{
			return false;
		}

		const int32 DungeonId = PlaceByHand(
			*Scene.Run, ECataclysmDungeonType::Basic, AnOutpostOf(*Scene.Run), 20,
			FullTimer, DaysLeft, 100.0f, 50.0f);
		if (!TestTrue(TEXT("set-up: the control's dungeon was placed"),
					  DungeonId != INDEX_NONE)
			|| !EnterAndStand(*this, Scene, DungeonId))
		{
			return false;
		}

		const float DefenceInside = DefenceOfEveryCity(*Scene.Run);

		Scene.Mode->LeaveEmpireDungeon();

		TestEqual(TEXT("CONTROL: leaving without dying takes nothing from any city"),
				  DefenceOfEveryCity(*Scene.Run), DefenceInside, 0.01f);
		TestEqual(TEXT("CONTROL: and no resolve has cost a city"),
				  Scene.Run->DungeonsDetonated, 0);
		TestEqual(TEXT("CONTROL: and its timer still has the two days it had"),
				  Scene.Run->Clock->DaysUntilResolveFor(DungeonId), DaysLeft, 0.001f);
	}

	// THE DEATH.
	FDeathScene Scene = MakeScene(0);
	if (!TestNotNull(TEXT("a second test world was created"), Scene.World))
	{
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		return false;
	}

	const int32 CityId = AnOutpostOf(*Scene.Run);
	const int32 DungeonId = PlaceByHand(
		*Scene.Run, ECataclysmDungeonType::Basic, CityId, 20, FullTimer, DaysLeft,
		100.0f, 50.0f);
	const FCataclysmDungeon* Placed =
		DungeonId != INDEX_NONE ? Scene.Run->FindDungeon(DungeonId) : nullptr;
	if (!TestNotNull(TEXT("set-up: a dungeon was placed on an Outpost"), Placed))
	{
		return false;
	}

	// WHAT ONE RESOLVE OF THIS DUNGEON TAKES, worked out from the dungeon the
	// way `ResolveDungeon` does: its points scaled by how deep it is against a
	// typical dungeon of its kind on that tier of city.
	const float DefenceBite = Placed->DefenceDamage * Placed->BiteScale();
	const float PopulationBite = Placed->PopulationDamage * Placed->BiteScale();

	if (!EnterAndStand(*this, Scene, DungeonId))
	{
		return false;
	}

	const FCataclysmCity* City = Scene.Run->Map->Find(CityId);
	if (!TestNotNull(TEXT("set-up: the dungeon's city is on the map"), City))
	{
		return false;
	}

	const float DefenceInside = City->Defence;
	const float PopulationInside = City->Population;
	const int32 DayInside = Scene.Run->Day() + Scene.Run->DaysOwed(); // the day the walk has reached; its days run on leaving

	if (!TestTrue(TEXT("set-up: one resolve is a real amount the city can pay"),
				  DefenceBite > 1.0f && PopulationBite > 1.0f
					  && DefenceInside > 2.0f * DefenceBite
					  && PopulationInside > 2.0f * PopulationBite)
		|| !TestEqual(TEXT("set-up: before the death no resolve has cost a city"),
					  Scene.Run->DungeonsDetonated, 0)
		|| !TestEqual(TEXT("set-up: and the timer has the two days it was given"),
					  Scene.Run->Clock->DaysUntilResolveFor(DungeonId), DaysLeft,
					  0.001f))
	{
		return false;
	}

	if (!KillThePlayer(*this, Scene) || !StandBackUp(*this, Scene))
	{
		return false;
	}

	// THE CITY PAID FOR ONE RESOLVE AND NOT FOR TWO.
	TestEqual(TEXT("the city lost one resolve's worth of defence"),
			  DefenceInside - City->Defence, DefenceBite, 0.01f);
	TestEqual(TEXT("and one resolve's worth of population"),
			  PopulationInside - City->Population, PopulationBite, 0.01f);
	TestEqual(TEXT("one resolve has cost a city, and only one"),
			  Scene.Run->DungeonsDetonated, 1);
	// THE CLOCK KEEPS NO COUNT OF THAT DUNGEON'S RESOLVES NOW. The count lived
	// on the dungeon's timer and the timer left with the dungeon;
	// `TimesResolvedOf` answers -1 for a dungeon with no timer. The run's own
	// count, one, is asserted above.
	TestEqual(TEXT("the clock keeps no count of that dungeon's resolves: -1, which is what a dungeon with no timer reads"),
			  TimesResolvedOf(*Scene.Run, DungeonId), -1);

	// IT IS OFF THE MAP, as every ordinary dungeon is after it hits its city.
	TestNull(TEXT("the dungeon is off the map"),
			 Scene.Run->FindDungeon(DungeonId));

	// AND NO TIMER OF ITS RAN THROUGH THE DAYS. Left on the map with its two
	// days it would have run out on the second of them and hit the city again;
	// the count of one above is what shows it did not.
	TestEqual(TEXT("the death cost five days"), Scene.Run->Day() - DayInside, 5);
	TestEqual(TEXT("no timer counts down for it after the five days"),
			  Scene.Run->Clock->DaysUntilResolveFor(DungeonId), -1.0f, 0.001f);

	return true;
}

// ---------------------------------------------------------------------------
// A resolve that fells the city
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathFellsTheCityTest,
	"Cataclysm.DungeonMode.ADeathWhoseResolveFellsTheCityTouchesNoDungeonThatIsGone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDeathFellsTheCityTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDeathEndsDungeonTest;

	FDeathScene Scene = MakeScene(0);
	if (!TestNotNull(TEXT("a test world was created"), Scene.World))
	{
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		return false;
	}

	// A CITY WITH TEN POINTS OF DEFENCE LEFT AND TWO ORDINARY DUNGEONS ON IT,
	// each of which takes far more than ten when it resolves. The player walks
	// the first; the second is there to be absorbed with it.
	const int32 CityId = AnOutpostOf(*Scene.Run);
	FCataclysmCity* Weakened = Scene.Run->Map->FindMutable(CityId);
	if (!TestNotNull(TEXT("set-up: an Outpost is on the map"), Weakened))
	{
		return false;
	}
	Weakened->Defence = 10.0f;

	const int32 DungeonId = PlaceByHand(
		*Scene.Run, ECataclysmDungeonType::Basic, CityId, 20, 40.0f, 40.0f,
		100.0f, 50.0f);
	const int32 OtherId = PlaceByHand(
		*Scene.Run, ECataclysmDungeonType::Basic, CityId, 20, 40.0f, 40.0f,
		100.0f, 50.0f);
	if (!TestTrue(TEXT("set-up: two dungeons were placed on that city"),
				  DungeonId != INDEX_NONE && OtherId != INDEX_NONE
					  && DungeonId != OtherId)
		|| !EnterAndStand(*this, Scene, DungeonId))
	{
		return false;
	}

	const int32 FallenBefore = Scene.Run->Map->FallenCityCount();
	const int32 DayInside = Scene.Run->Day() + Scene.Run->DaysOwed(); // the day the walk has reached; its days run on leaving

	// THE CONTROL: before the death the city stands and both dungeons stand.
	if (!TestFalse(TEXT("CONTROL: before the death the city has not fallen"),
				   Scene.Run->Map->Find(CityId)->bFallen)
		|| !TestNotNull(TEXT("CONTROL: and the dungeon being walked is on the map"),
						Scene.Run->FindDungeon(DungeonId))
		|| !TestNotNull(TEXT("CONTROL: and so is the other one"),
						Scene.Run->FindDungeon(OtherId)))
	{
		return false;
	}

	if (!KillThePlayer(*this, Scene) || !StandBackUp(*this, Scene))
	{
		return false;
	}

	// THE RESOLVE FELLED THE CITY, and the city took its dungeons with it.
	TestTrue(TEXT("the death's resolve felled the city"),
			 Scene.Run->Map->Find(CityId)->bFallen);
	TestEqual(TEXT("and it is the one city that fell"),
			  Scene.Run->Map->FallenCityCount(), FallenBefore + 1);
	TestEqual(TEXT("one resolve cost a city"), Scene.Run->DungeonsDetonated, 1);
	TestNull(TEXT("the dungeon the player died in is off the map"),
			 Scene.Run->FindDungeon(DungeonId));
	TestNull(TEXT("and so is the other dungeon that stood on the city"),
			 Scene.Run->FindDungeon(OtherId));

	// AND NOTHING WAS DONE TO A DUNGEON THAT IS GONE. No timer was put back for
	// it, and the two lists the run keeps in step still agree.
	TestEqual(TEXT("no timer counts down for the dungeon that is gone"),
			  Scene.Run->Clock->DaysUntilResolveFor(DungeonId), -1.0f, 0.001f);

	FString WhyNot;
	const bool bListsAgree = UCataclysmEmpireRun::DungeonsAgreeWithTimers(
		Scene.Run->Dungeons, Scene.Run->Clock->Timers, WhyNot);
	TestTrue(FString::Printf(
		TEXT("every dungeon still has one timer and every timer a dungeon: %s"),
		*WhyNot), bListsAgree);

	// THE CITY BECAME A DUNGEON CITY, which is what a city falling does.
	bool bDungeonCityStands = false;
	for (const FCataclysmDungeon& Standing : Scene.Run->Dungeons)
	{
		bDungeonCityStands = bDungeonCityStands
			|| (Standing.Type == ECataclysmDungeonType::FallenCity
				&& Standing.CityId == CityId);
	}
	TestTrue(TEXT("a Dungeon City stands where the city fell"), bDungeonCityStands);

	// THE PLAYER IS OUT OF IT AND THE DAYS WERE STILL PAID.
	TestEqual(TEXT("the game mode is walking no dungeon"),
			  Scene.Mode->EmpireDungeonId, INDEX_NONE);
	TestEqual(TEXT("the clock holds nobody inside a dungeon"),
			  Scene.Run->Clock->CurrentDungeonId, INDEX_NONE);
	TestEqual(TEXT("and the death still cost five days"),
			  Scene.Run->Day() - DayInside, 5);

	return true;
}

// ---------------------------------------------------------------------------
// A Quest dungeon and a Dungeon City
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathResolvesNoQuestDungeonTest,
	"Cataclysm.DungeonMode.ADeathInAQuestDungeonOrADungeonCityCostsTheDaysAndResolvesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDeathResolvesNoQuestDungeonTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDeathEndsDungeonTest;

	// EVERY CASE CARRIES THE SAME DAMAGE, 100 points of defence and 50 of
	// population, on a city that is standing. So if a death resolved a Quest
	// dungeon or a Dungeon City by mistake, the city would show it.

	// -- A Quest dungeon ---------------------------------------------------
	//
	// ITS TIMER IS 25 DAYS, WITH 12 LEFT. TWELVE IS MORE THAN THE FIVE DAYS
	// CHARGED, ON PURPOSE. Since 2026-10-13 a Quest dungeon's timer hits its
	// host city when it runs out. This test is about the death, and its control
	// says the days alone take nothing from any city; a timer that ran out
	// inside the five days would take an ordinary hit from the host in both
	// passes and make that control false. Until that date the timer had 3
	// days left and ran out on the third. With 12 it runs the five days and
	// reads 7. A death that touched the timer would leave some other figure.
	//
	// THE 25 IS SET HERE AND NOT READ FROM THE GAME. The test needs a Quest
	// timer with 12 days left, because it runs five; any timer longer than that
	// serves. Until 2026-10-19 this read the game's flat Quest timer, which was
	// 25 days; since then a Quest dungeon's timer comes from its depth, as an
	// ordinary dungeon's does, so there is no one figure to read.
	const float QuestTimer = 25.0f;
	const float QuestDaysLeft = 12.0f;

	FAfterTheDays QuestDeath;
	FAfterTheDays QuestControl;
	if (!RunOneCase(*this, ECataclysmDungeonType::Quest, QuestTimer,
					QuestDaysLeft, /*bThePlayerDies=*/true, 0, QuestDeath)
		|| !RunOneCase(*this, ECataclysmDungeonType::Quest, QuestTimer,
					   QuestDaysLeft, /*bThePlayerDies=*/false, 5, QuestControl))
	{
		return false;
	}

	if (!TestEqual(TEXT("CONTROL, Quest: five days passed with no death"),
				   QuestControl.DaysPassed, 5)
		|| !TestEqual(TEXT("CONTROL, Quest: its timer ran the five days and did not run out"),
					  QuestControl.TimerLeft, QuestDaysLeft - 5.0f, 0.001f)
		|| !TestEqual(TEXT("CONTROL, Quest: the days alone took nothing from any city"),
					  QuestControl.Defence, QuestControl.DefenceInside, 0.01f))
	{
		return false;
	}

	TestEqual(TEXT("Quest: the death cost five days"), QuestDeath.DaysPassed, 5);
	TestEqual(TEXT("Quest: no resolve cost a city"), QuestDeath.Detonated, 0);
	TestEqual(TEXT("Quest: every city's defence is what the days alone make it"),
			  QuestDeath.Defence, QuestControl.Defence, 0.01f);
	TestEqual(TEXT("Quest: and every city's population"),
			  QuestDeath.Population, QuestControl.Population, 0.01f);
	TestEqual(TEXT("Quest: its timer is what the days alone make it"),
			  QuestDeath.TimerLeft, QuestControl.TimerLeft, 0.001f);
	TestEqual(TEXT("Quest: it has run out as many times as the days alone make it"),
			  QuestDeath.TimesResolved, QuestControl.TimesResolved);
	TestEqual(TEXT("Quest: it stands on the city the days alone leave it on"),
			  QuestDeath.CityOfTheDungeon, QuestControl.CityOfTheDungeon);
	TestEqual(TEXT("Quest: the player is out of the dungeon"),
			  QuestDeath.BoundAfter, INDEX_NONE);

	// -- A Dungeon City ----------------------------------------------------
	//
	// A timer of 999 days with 500 left, so nothing runs out in five days.
	const float CityTimer = UCataclysmSurgeScheduler::FallenCityResolveDays;

	FAfterTheDays CityDeath;
	FAfterTheDays CityControl;
	if (!RunOneCase(*this, ECataclysmDungeonType::FallenCity, CityTimer, 500.0f,
					/*bThePlayerDies=*/true, 0, CityDeath)
		|| !RunOneCase(*this, ECataclysmDungeonType::FallenCity, CityTimer, 500.0f,
					   /*bThePlayerDies=*/false, 5, CityControl))
	{
		return false;
	}

	if (!TestEqual(TEXT("CONTROL, Dungeon City: five days passed with no death"),
				   CityControl.DaysPassed, 5)
		|| !TestEqual(TEXT("CONTROL, Dungeon City: its timer ran the five days"),
					  CityControl.TimerLeft, 495.0f, 0.001f))
	{
		return false;
	}

	TestEqual(TEXT("Dungeon City: the death cost five days"),
			  CityDeath.DaysPassed, 5);
	TestEqual(TEXT("Dungeon City: no resolve cost a city"), CityDeath.Detonated, 0);
	TestEqual(TEXT("Dungeon City: every city's defence is what the days alone make it"),
			  CityDeath.Defence, CityControl.Defence, 0.01f);
	TestEqual(TEXT("Dungeon City: and every city's population"),
			  CityDeath.Population, CityControl.Population, 0.01f);
	TestEqual(TEXT("Dungeon City: its timer is what the days alone make it"),
			  CityDeath.TimerLeft, CityControl.TimerLeft, 0.001f);
	TestEqual(TEXT("Dungeon City: the player is out of the dungeon"),
			  CityDeath.BoundAfter, INDEX_NONE);

	// -- THE CONTROL THAT THE DAMAGE WAS REAL -------------------------------
	//
	// The same dungeon as an ordinary one. A death in it does take the city's
	// defence, so the two kinds above were spared by their kind and not by a
	// dungeon that could not have hurt anything.
	FAfterTheDays OrdinaryDeath;
	if (!RunOneCase(*this, ECataclysmDungeonType::Basic, 40.0f, 40.0f,
					/*bThePlayerDies=*/true, 0, OrdinaryDeath))
	{
		return false;
	}

	TestTrue(FString::Printf(
		TEXT("CONTROL: an ordinary dungeon carrying the same damage took %.1f defence at the death"),
		OrdinaryDeath.DefenceInside - OrdinaryDeath.Defence),
		OrdinaryDeath.DefenceInside - OrdinaryDeath.Defence > 100.0f);
	TestEqual(TEXT("CONTROL: and one resolve cost a city"),
			  OrdinaryDeath.Detonated, 1);

	return true;
}

// ---------------------------------------------------------------------------
// With no dungeon of the empire bound
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathWithNothingBoundTest,
	"Cataclysm.DungeonMode.ADeathWithNoEmpireDungeonBoundStandsThePlayerBackUpInTheLevelAndCostsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDeathWithNothingBoundTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDeathEndsDungeonTest;

	FDeathScene Scene = MakeScene(0);
	if (!TestNotNull(TEXT("a test world was created"), Scene.World))
	{
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		return false;
	}

	// A DUNGEON STANDS ON THE MAP AND NOBODY HAS ENTERED IT. The run is there
	// and so is something a mistaken resolve could reach; the game mode is
	// bound to nothing, which is pressing Play in the dungeon level.
	const int32 CityId = AnOutpostOf(*Scene.Run);
	const int32 NotEntered = PlaceByHand(
		*Scene.Run, ECataclysmDungeonType::Basic, CityId, 20, 40.0f, 40.0f,
		100.0f, 50.0f);
	if (!TestTrue(TEXT("set-up: a dungeon stands on the map"), NotEntered != INDEX_NONE)
		|| !TestEqual(TEXT("set-up: the game mode is walking no dungeon"),
					  Scene.Mode->EmpireDungeonId, INDEX_NONE)
		|| !TestTrue(TEXT("set-up: floor 3 of the level was reached"),
					 Scene.Mode->GoToFloor(3)))
	{
		return false;
	}

	Scene.Mode->ClearFloorEnemies();
	if (!TestTrue(TEXT("set-up: the player stands at the floor's entrance"),
				  Scene.Mode->PlaceAtEntrance(Scene.Player)))
	{
		return false;
	}

	// THE PLAYER WALKS THIRTY METRES OFF AND DIES THERE, so standing back up at
	// the entrance is a move and not where it already was.
	const FVector Entrance = Scene.Player->GetActorLocation();
	const FVector Away = Entrance + FVector(3000.0f, 3000.0f, 0.0f);
	Scene.Player->SetActorLocation(Away);

	const int32 DayBefore = Scene.Run->Day();
	const float DefenceBefore = DefenceOfEveryCity(*Scene.Run);

	if (!KillThePlayer(*this, Scene) || !StandBackUp(*this, Scene))
	{
		return false;
	}

	TestEqual(TEXT("no day passed"), Scene.Run->Day(), DayBefore);
	TestEqual(TEXT("no resolve cost a city"), Scene.Run->DungeonsDetonated, 0);
	TestEqual(TEXT("and no city lost any defence"),
			  DefenceOfEveryCity(*Scene.Run), DefenceBefore, 0.01f);
	TestEqual(TEXT("the dungeon nobody entered still has its forty days"),
			  Scene.Run->Clock->DaysUntilResolveFor(NotEntered), 40.0f, 0.001f);
	TestEqual(TEXT("the floor being walked is still floor 3"),
			  Scene.Mode->FloorNumber, 3);

	const FVector StandingAgain = Scene.Player->GetActorLocation();
	TestTrue(FString::Printf(
		TEXT("the player stands back up at the floor's entrance: at %s, the entrance is %s"),
		*StandingAgain.ToCompactString(), *Entrance.ToCompactString()),
		FVector::Dist2D(StandingAgain, Entrance) < 1.0f);
	TestTrue(TEXT("and not where it died"),
			 FVector::Dist2D(StandingAgain, Away) > 100.0f);

	const float Health = Scene.AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetHealthAttribute());
	const float MaxHealth = Scene.AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
	TestTrue(TEXT("it has health again"), Health > 0.0f);
	TestEqual(TEXT("all of it"), Health, MaxHealth, 0.01f);

	return true;
}

// ---------------------------------------------------------------------------
// The playtest switch
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathPlaytestSwitchTest,
	"Cataclysm.DungeonMode.ThePlaytestSwitchKeepsADeadPlayerInTheDungeonAndWithoutItTheDeathEndsTheWalk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDeathPlaytestSwitchTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDeathEndsDungeonTest;

	FDeathScene Scene = MakeScene(0);
	if (!TestNotNull(TEXT("a test world was created"), Scene.World))
	{
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		return false;
	}

	const int32 CityId = AnOutpostOf(*Scene.Run);
	const int32 DungeonId = PlaceByHand(
		*Scene.Run, ECataclysmDungeonType::Basic, CityId, 20, 40.0f, 40.0f,
		100.0f, 50.0f);
	if (!TestTrue(TEXT("set-up: a dungeon was placed on an Outpost"),
				  DungeonId != INDEX_NONE)
		|| !EnterAndStand(*this, Scene, DungeonId))
	{
		return false;
	}

	const FCataclysmCity* City = Scene.Run->Map->Find(CityId);
	if (!TestNotNull(TEXT("set-up: the dungeon's city is on the map"), City))
	{
		return false;
	}

	const float DefenceInside = City->Defence;
	const int32 DayInside = Scene.Run->Day();
	// THE DAYS THE WALK OWES, since 2026-10-24. The switch-set half below never leaves, so the run's own day stays
	// where it is; the default half leaves, which runs these before the death's own days.
	const int32 OwedInside = Scene.Run->DaysOwed();

	// -- THE SWITCH SET: the death is what it was before issue #41 ----------
	{
		FScopedDeathSwitch KeepsThePlayer(1);
		if (!TestNotNull(TEXT("set-up: the playtest switch is a console variable"),
						 KeepsThePlayer.Variable)
			|| !TestEqual(TEXT("set-up: with nothing having set it the switch read 0, the designed behaviour"),
						  KeepsThePlayer.Before, 0))
		{
			return false;
		}

		if (!KillThePlayer(*this, Scene) || !StandBackUp(*this, Scene))
		{
			return false;
		}

		TestEqual(TEXT("switch set: no day passed"), Scene.Run->Day(), DayInside);
		TestEqual(TEXT("switch set: the dungeon is still being walked"),
				  Scene.Mode->EmpireDungeonId, DungeonId);
		TestEqual(TEXT("switch set: and the clock still holds the player inside it"),
				  Scene.Run->Clock->CurrentDungeonId, DungeonId);
		TestEqual(TEXT("switch set: no resolve cost a city"),
				  Scene.Run->DungeonsDetonated, 0);
		TestEqual(TEXT("switch set: the city kept its defence"),
				  City->Defence, DefenceInside, 0.01f);
		TestEqual(TEXT("switch set: the dungeon's timer still has its forty days"),
				  Scene.Run->Clock->DaysUntilResolveFor(DungeonId), 40.0f, 0.001f);
	}

	// -- THE SWITCH BACK AT ITS DEFAULT: the same player dies again ---------
	//
	// In the same dungeon, which the first death left bound. This half is what
	// the half above is the control for, and the other way round.
	if (!KillThePlayer(*this, Scene) || !StandBackUp(*this, Scene))
	{
		return false;
	}

	TestEqual(TEXT("default: the death cost five days"),
			  Scene.Run->Day() - (DayInside + OwedInside), 5);
	TestEqual(TEXT("default: the game mode is walking no dungeon"),
			  Scene.Mode->EmpireDungeonId, INDEX_NONE);
	TestEqual(TEXT("default: the clock holds nobody inside a dungeon"),
			  Scene.Run->Clock->CurrentDungeonId, INDEX_NONE);
	TestEqual(TEXT("default: one resolve cost a city"),
			  Scene.Run->DungeonsDetonated, 1);
	TestTrue(FString::Printf(
		TEXT("default: the city lost defence to the resolve, %.1f of it"),
		DefenceInside - City->Defence),
		DefenceInside - City->Defence > 100.0f);

	return true;
}

// ---------------------------------------------------------------------------
// Where the player is afterwards
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathLeavesAsAClearDoesTest,
	"Cataclysm.DungeonMode.AfterADeathThePlayerIsOutOfTheDungeonAsAClearedOneLeavesThemAndStandsUpWhole",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDeathLeavesAsAClearDoesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDeathEndsDungeonTest;

	/** What the game mode and the clock say once a dungeon has been left. */
	struct FLeftBehind
	{
		int32 Bound = 0;
		int32 ClockInside = 0;
		int32 EmpireFloors = -1;
		int32 FloorNumber = 0;
		int32 FloorModifiers = -1;
		float ModifierScore = -1.0f;
		bool bLastFloor = true;
	};

	const auto Read = [](const FDeathScene& From)
	{
		FLeftBehind Out;
		Out.Bound = From.Mode->EmpireDungeonId;
		Out.ClockInside = From.Run->Clock->CurrentDungeonId;
		Out.EmpireFloors = From.Mode->EmpireDungeonFloors();
		Out.FloorNumber = From.Mode->FloorNumber;
		Out.FloorModifiers = From.Mode->FloorBrief.Modifiers.Num();
		Out.ModifierScore = From.Mode->DungeonModifierScore;
		Out.bLastFloor = From.Mode->IsOnTheLastFloor();
		return Out;
	};

	// BOTH SCENES WALK THE SAME DUNGEON TO ITS THIRD FLOOR: twenty floors deep,
	// no damage, a hundred days on its timer, and a modifier score of 30 put on
	// it by hand so that there is something of the dungeon's on the game mode
	// to see go.
	const auto WalkToFloorThree = [this](const FDeathScene& Scene, int32& OutDungeonId)
	{
		OutDungeonId = PlaceByHand(
			*Scene.Run, ECataclysmDungeonType::Basic, AnOutpostOf(*Scene.Run), 20,
			100.0f, 100.0f, 0.0f, 0.0f);
		for (FCataclysmDungeon& Standing : Scene.Run->Dungeons)
		{
			if (Standing.DungeonId == OutDungeonId)
			{
				Standing.ModifierScore = 30.0f;
			}
		}

		if (!TestTrue(TEXT("set-up: a dungeon was placed on an Outpost"),
					  OutDungeonId != INDEX_NONE)
			|| !EnterAndStand(*this, Scene, OutDungeonId)
			|| !TestTrue(TEXT("set-up: the first flight of stairs went down"),
						 Scene.Mode->GoDownOneFloor())
			|| !TestTrue(TEXT("set-up: and the second"), Scene.Mode->GoDownOneFloor()))
		{
			return false;
		}

		Scene.Mode->ClearFloorEnemies();

		return TestTrue(TEXT("set-up: the player stands at the third floor's entrance"),
						Scene.Mode->PlaceAtEntrance(Scene.Player))
			&& TestEqual(TEXT("CONTROL: inside, the game mode is walking the dungeon"),
						 Scene.Mode->EmpireDungeonId, OutDungeonId)
			&& TestEqual(TEXT("CONTROL: inside, it is on floor 3"),
						 Scene.Mode->FloorNumber, 3)
			&& TestEqual(TEXT("CONTROL: inside, it reads the dungeon's twenty floors"),
						 Scene.Mode->EmpireDungeonFloors(), 20)
			&& TestEqual(TEXT("CONTROL: inside, it carries the dungeon's modifier score of 30"),
						 Scene.Mode->DungeonModifierScore, 30.0f, 0.001f)
			&& TestEqual(TEXT("CONTROL: inside, the clock holds the player in that dungeon"),
						 Scene.Run->Clock->CurrentDungeonId, OutDungeonId);
	};

	// -- THE CONTROL: the dungeon is cleared ------------------------------
	//
	// By `ClearEmpireDungeon`, which is what the stairs of the last floor call.
	FLeftBehind AfterAClear;
	{
		FDeathScene Scene = MakeScene(0);
		if (!TestNotNull(TEXT("a test world was created"), Scene.World))
		{
			return false;
		}
		ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		int32 ClearedId = INDEX_NONE;
		if (!TestTrue(TEXT("set-up: the control's scene was built"), Scene.IsUsable())
			|| !WalkToFloorThree(Scene, ClearedId)
			|| !TestTrue(TEXT("set-up: the dungeon was cleared"),
						 Scene.Mode->ClearEmpireDungeon()))
		{
			return false;
		}

		AfterAClear = Read(Scene);
		TestNull(TEXT("CONTROL: a cleared dungeon is off the map"),
				 Scene.Run->FindDungeon(ClearedId));
	}

	// -- THE DEATH --------------------------------------------------------
	FDeathScene Scene = MakeScene(0);
	if (!TestNotNull(TEXT("a second test world was created"), Scene.World))
	{
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	int32 DungeonId = INDEX_NONE;
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable())
		|| !WalkToFloorThree(Scene, DungeonId))
	{
		return false;
	}

	// THE PLAYER WALKS THIRTY METRES FROM THE ENTRANCE AND DIES THERE.
	const FVector Entrance = Scene.Player->GetActorLocation();
	const FVector Away = Entrance + FVector(3000.0f, 3000.0f, 0.0f);
	Scene.Player->SetActorLocation(Away);

	if (!KillThePlayer(*this, Scene))
	{
		return false;
	}

	const FGameplayAttribute HealthAttribute =
		UCataclysmVitalAttributeSet::GetHealthAttribute();
	if (!TestEqual(TEXT("CONTROL: dead, the player has no health"),
				   Scene.AbilitySystem->GetNumericAttribute(HealthAttribute), 0.0f,
				   0.01f)
		|| !StandBackUp(*this, Scene))
	{
		return false;
	}

	const FLeftBehind AfterTheDeath = Read(Scene);

	// THE STATE A CLEARED DUNGEON LEAVES, FIELD BY FIELD.
	TestEqual(TEXT("the game mode is walking no dungeon"),
			  AfterTheDeath.Bound, INDEX_NONE);
	TestEqual(TEXT("which is what a cleared dungeon leaves"),
			  AfterTheDeath.Bound, AfterAClear.Bound);
	TestEqual(TEXT("the clock holds nobody inside a dungeon, as after a clear"),
			  AfterTheDeath.ClockInside, AfterAClear.ClockInside);
	TestEqual(TEXT("and that is nobody"), AfterTheDeath.ClockInside, INDEX_NONE);
	TestEqual(TEXT("the game mode reads no dungeon's depth, as after a clear"),
			  AfterTheDeath.EmpireFloors, AfterAClear.EmpireFloors);
	TestEqual(TEXT("and that is none"), AfterTheDeath.EmpireFloors, 0);
	TestEqual(TEXT("the dungeon's modifier score is off the game mode, as after a clear"),
			  AfterTheDeath.ModifierScore, AfterAClear.ModifierScore, 0.001f);
	TestEqual(TEXT("and that is nought"), AfterTheDeath.ModifierScore, 0.0f, 0.001f);
	TestEqual(TEXT("the floor carries as many modifiers as after a clear"),
			  AfterTheDeath.FloorModifiers, AfterAClear.FloorModifiers);
	TestTrue(TEXT("whether the floor is the last floor is as after a clear"),
			 AfterTheDeath.bLastFloor == AfterAClear.bLastFloor);
	TestFalse(TEXT("and it is not: with no dungeon bound there is no last floor"),
			  AfterTheDeath.bLastFloor);
	TestEqual(TEXT("the floor still standing is the one the player died on, as after a clear"),
			  AfterTheDeath.FloorNumber, AfterAClear.FloorNumber);
	TestEqual(TEXT("and that is floor 3"), AfterTheDeath.FloorNumber, 3);

	// AND THE DUNGEON IS OFF THE MAP, AS A CLEARED ONE IS. Until 2026-10-13
	// this was the one difference from a clear: the dungeon stayed. An
	// ordinary dungeon now leaves after it hits its city, and a death makes it
	// hit. It is not counted as cleared; that is the empire layer's test.
	TestNull(TEXT("like a cleared dungeon, this one is off the map"),
			 Scene.Run->FindDungeon(DungeonId));

	// AND THE PLAYER STANDS BACK UP WHOLE, at the entrance of that floor.
	const FVector StandingAgain = Scene.Player->GetActorLocation();
	TestTrue(FString::Printf(
		TEXT("the player stands back up at the floor's entrance: at %s, the entrance is %s"),
		*StandingAgain.ToCompactString(), *Entrance.ToCompactString()),
		FVector::Dist2D(StandingAgain, Entrance) < 1.0f);
	TestTrue(TEXT("and not where it died"),
			 FVector::Dist2D(StandingAgain, Away) > 100.0f);

	const float Health = Scene.AbilitySystem->GetNumericAttribute(HealthAttribute);
	const float MaxHealth = Scene.AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
	TestTrue(TEXT("it has health again"), Health > 0.0f);
	TestEqual(TEXT("all of its maximum"), Health, MaxHealth, 0.01f);

	if (const UCharacterMovementComponent* Movement =
			Scene.Player->GetCharacterMovement())
	{
		TestEqual(TEXT("and it can walk again"),
				  static_cast<int32>(Movement->MovementMode),
				  static_cast<int32>(MOVE_Walking));
	}

	return true;
}

// ---------------------------------------------------------------------------
// Heretic, in a shallow dungeon
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathHereticShallowTest,
	"Cataclysm.DungeonMode.OnHereticADeathInAThreeFloorDungeonHitsItsCityOnceAndNothingMoreLandsInTheDaysCharged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDeathHereticShallowTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDeathEndsDungeonTest;

	// THE FIGURES THE CASE RESTS ON, READ FROM THE CLOCK AND CHECKED. A full
	// timer is 10 days plus 1.6 a floor: 14.8 at three floors and 16.4 at four.
	// A Heretic death costs 15. Until 2026-10-13 a three floor dungeon, full
	// again at the death, ran out inside the days the death costs and resolved
	// a second time, and a four floor one did not; this test held that, under
	// the name `...ResolvesItAtTheDeathAndAgainFromTheDaysCharged`.
	//
	// WHAT IT HOLDS NOW: the dungeon hits its city once, at the death, and
	// leaves the map, so nothing more lands in the fifteen days at either
	// depth. Three floors is the case that used to hit twice, which is what
	// makes "once" a figure that could be wrong. No roll is on these timers;
	// the test writes them.
	const float TimerAtThree = UCataclysmDayClock::ResolveDaysFor(3);
	const float TimerAtFour = UCataclysmDayClock::ResolveDaysFor(4);
	constexpr int32 HereticRung = 2;

	if (!TestEqual(TEXT("set-up: a three floor dungeon's full timer is 14.8 days"),
				   TimerAtThree, 14.8f, 0.001f)
		|| !TestEqual(TEXT("set-up: a four floor dungeon's is 16.4"),
					  TimerAtFour, 16.4f, 0.001f)
		|| !TestEqual(TEXT("set-up: a Heretic death costs 15 days"),
					  UCataclysmDayClock::DeathDayCostFor(HereticRung), 15))
	{
		return false;
	}

	/** One Heretic death in a dungeon of this depth, and what it left. */
	struct FShallow
	{
		float DefenceLost = 0.0f;
		float OneBite = 0.0f;
		float TimerLeft = 0.0f;
		int32 Detonated = 0;
		int32 TimesResolved = 0;
		int32 DaysPassed = 0;
		bool bOnTheMapAfter = true;
	};

	const auto DieIn = [this](int32 Floors, float FullTimer, FShallow& Out)
	{
		FDeathScene Scene = MakeScene(HereticRung);
		if (!TestNotNull(TEXT("a test world was created"), Scene.World))
		{
			return false;
		}
		ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

		if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
		{
			return false;
		}

		const int32 CityId = AnOutpostOf(*Scene.Run);
		const int32 DungeonId = PlaceByHand(
			*Scene.Run, ECataclysmDungeonType::Basic, CityId, Floors, FullTimer,
			FullTimer, 20.0f, 10.0f);
		const FCataclysmDungeon* Placed =
			DungeonId != INDEX_NONE ? Scene.Run->FindDungeon(DungeonId) : nullptr;
		if (!TestNotNull(TEXT("set-up: a dungeon was placed on an Outpost"), Placed))
		{
			return false;
		}
		Out.OneBite = Placed->DefenceDamage * Placed->BiteScale();

		if (!EnterAndStand(*this, Scene, DungeonId))
		{
			return false;
		}

		const FCataclysmCity* City = Scene.Run->Map->Find(CityId);
		if (!TestNotNull(TEXT("set-up: the dungeon's city is on the map"), City))
		{
			return false;
		}
		const float DefenceInside = City->Defence;
		const int32 DayInside = Scene.Run->Day() + Scene.Run->DaysOwed(); // the day the walk has reached; its days run on leaving

		if (!KillThePlayer(*this, Scene) || !StandBackUp(*this, Scene))
		{
			return false;
		}

		Out.DefenceLost = DefenceInside - City->Defence;
		Out.TimerLeft = Scene.Run->Clock->DaysUntilResolveFor(DungeonId);
		Out.Detonated = Scene.Run->DungeonsDetonated;
		Out.TimesResolved = TimesResolvedOf(*Scene.Run, DungeonId);
		Out.DaysPassed = Scene.Run->Day() - DayInside;
		Out.bOnTheMapAfter = Scene.Run->FindDungeon(DungeonId) != nullptr;
		return true;
	};

	// -- FOUR FLOORS: one hit, as it always was ----------------------------
	//
	// THE FULL TIMER HERE IS LONGER THAN THE FIFTEEN DAYS, so this depth never
	// hit twice. It is beside the three floor case to show the two depths now
	// come out alike.
	FShallow AtFour;
	if (!DieIn(4, TimerAtFour, AtFour))
	{
		return false;
	}

	TestEqual(TEXT("four floors: the death cost 15 days"),
			  AtFour.DaysPassed, 15);
	TestEqual(TEXT("four floors: one resolve cost the city, at the death"),
			  AtFour.Detonated, 1);
	TestEqual(TEXT("four floors: the clock keeps no count for it: -1, which is what a dungeon with no timer reads"),
			  AtFour.TimesResolved, -1);
	TestTrue(TEXT("four floors: one resolve is a real amount"),
			 AtFour.OneBite > 1.0f);
	TestEqual(TEXT("four floors: the city lost one resolve's defence"),
			  AtFour.DefenceLost, AtFour.OneBite, 0.01f);
	TestEqual(TEXT("four floors: no timer counts down for it after the fifteen days"),
			  AtFour.TimerLeft, -1.0f, 0.001f);
	TestFalse(TEXT("four floors: the dungeon is off the map"),
			  AtFour.bOnTheMapAfter);

	// -- THREE FLOORS: one hit, where there used to be two ------------------
	FShallow AtThree;
	if (!DieIn(3, TimerAtThree, AtThree))
	{
		return false;
	}

	TestEqual(TEXT("three floors: the death cost 15 days"), AtThree.DaysPassed, 15);
	TestEqual(TEXT("three floors: one resolve cost the city, at the death, and none from the days charged"),
			  AtThree.Detonated, 1);
	TestEqual(TEXT("three floors: the clock keeps no count for it: -1, which is what a dungeon with no timer reads"),
			  AtThree.TimesResolved, -1);
	TestTrue(TEXT("three floors: one resolve is a real amount"),
			 AtThree.OneBite > 1.0f);
	TestEqual(TEXT("three floors: the city lost one resolve's defence and no more in the fifteen days"),
			  AtThree.DefenceLost, AtThree.OneBite, 0.01f);
	TestEqual(TEXT("three floors: no timer counts down for it after the fifteen days"),
			  AtThree.TimerLeft, -1.0f, 0.001f);
	TestFalse(TEXT("three floors: the dungeon is off the map"),
			  AtThree.bOnTheMapAfter);

	return true;
}

// ---------------------------------------------------------------------------
// A walk's days run when the player leaves, 2026-10-24
// ---------------------------------------------------------------------------

/**
 * A clear is credited before the walk's days run, and a city another dungeon fells in those days still falls.
 *
 * THE OWNER'S TWO ANSWERS OF 2026-10-24: nothing resolves until the player leaves ("all of the resolutions and
 * day/timer updates happen once a player leaves a dungeon"), and "Clear counts, city still falls". So the city
 * stands for the whole walk although another dungeon on it has one day left; the walk is cleared and credited;
 * and then the walk's days run and that dungeon fells the city.
 *
 * THE CONTROL is the same walk with the other dungeon's timer at forty days: the clear is credited the same way and
 * no city falls, so the fall in the first half is the other dungeon's, inside the walk's days.
 */
namespace CataclysmDaysRunOnLeavingTest
{
	struct FWalkedToTheBottom
	{
		bool bCityStoodAtTheBottom = false;
		bool bCityFell = false;
		int32 OwedAtTheBottom = 0;
		int32 DaysPassed = 0;
		int32 ClearedAdded = 0;
		int32 BasicClearedAdded = 0;
		bool bWalkedOffTheMap = false;
	};

	bool WalkAndClear(FAutomationTestBase& Test, float OtherDaysLeft, FWalkedToTheBottom& Out)
	{
		using namespace CataclysmDeathEndsDungeonTest;

		FDeathScene Scene = MakeScene(0);
		if (!Test.TestNotNull(TEXT("a test world was created"), Scene.World))
		{
			return false;
		}
		ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		if (!Test.TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
		{
			return false;
		}

		// A CITY WITH TEN POINTS OF DEFENCE, the dungeon walked on it, and another whose hit is far more than any
		// defence the clear could restore.
		const int32 CityId = AnOutpostOf(*Scene.Run);
		FCataclysmCity* City = Scene.Run->Map->FindMutable(CityId);
		if (!Test.TestNotNull(TEXT("set-up: an Outpost is on the map"), City))
		{
			return false;
		}
		City->Defence = 10.0f;

		const int32 Walked = PlaceByHand(*Scene.Run, ECataclysmDungeonType::Basic, CityId, 3, 40.0f, 40.0f,
										 100.0f, 50.0f);
		const int32 Other = PlaceByHand(*Scene.Run, ECataclysmDungeonType::Basic, CityId, 20, 40.0f, OtherDaysLeft,
										100000.0f, 100000.0f);
		if (!Test.TestTrue(TEXT("set-up: two dungeons were placed on that city"),
						   Walked != INDEX_NONE && Other != INDEX_NONE && Walked != Other)
			|| !EnterAndStand(Test, Scene, Walked))
		{
			return false;
		}

		const int32 DayOnEntering = Scene.Run->Day();
		const int32 ClearedBefore = Scene.Run->DungeonsCleared;
		const int32 BasicClearedBefore = Scene.Run->BasicDungeonsCleared;

		// DOWN TO THE BOTTOM, floor 3 of 3.
		for (int32 Down = 0; Down < 2; ++Down)
		{
			if (!Test.TestTrue(FString::Printf(TEXT("set-up: descent %d was made"), Down + 1),
							   Scene.Mode->GoDownOneFloor()))
			{
				return false;
			}
		}
		if (!Test.TestTrue(TEXT("set-up: the player is on the last floor"), Scene.Mode->IsOnTheLastFloor()))
		{
			return false;
		}

		Out.bCityStoodAtTheBottom = !Scene.Run->Map->Find(CityId)->bFallen;
		Out.OwedAtTheBottom = Scene.Run->DaysOwed();

		// THE DESCENT PAST THE LAST FLOOR CLEARS IT, and then the walk's days run.
		Scene.Mode->GoDownOneFloor();

		Out.bCityFell = Scene.Run->Map->Find(CityId)->bFallen;
		Out.DaysPassed = Scene.Run->Day() - DayOnEntering;
		Out.ClearedAdded = Scene.Run->DungeonsCleared - ClearedBefore;
		Out.BasicClearedAdded = Scene.Run->BasicDungeonsCleared - BasicClearedBefore;
		Out.bWalkedOffTheMap = Scene.Run->FindDungeon(Walked) == nullptr;
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmClearCountsCityStillFallsTest,
	"Cataclysm.DungeonMode.AClearIsCreditedBeforeTheWalksDaysRunAndACityFelledInThemStillFalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmClearCountsCityStillFallsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDaysRunOnLeavingTest;

	// THE CONTROL: the other dungeon has forty days, more than the walk's three.
	FWalkedToTheBottom Control;
	if (!WalkAndClear(*this, 40.0f, Control))
	{
		return false;
	}
	TestTrue(TEXT("CONTROL: the city stood at the bottom"), Control.bCityStoodAtTheBottom);
	TestEqual(TEXT("CONTROL: three floors owed three days"), Control.OwedAtTheBottom, 3);
	TestEqual(TEXT("CONTROL: and leaving ran the three"), Control.DaysPassed, 3);
	TestEqual(TEXT("CONTROL: the clear was counted"), Control.ClearedAdded, 1);
	TestEqual(TEXT("CONTROL: as an ordinary dungeon's"), Control.BasicClearedAdded, 1);
	TestTrue(TEXT("CONTROL: the walked dungeon is off the map"), Control.bWalkedOffTheMap);
	TestFalse(TEXT("CONTROL: and no city fell"), Control.bCityFell);

	// THE OTHER DUNGEON HAS ONE DAY LEFT, inside the walk's three.
	FWalkedToTheBottom Fell;
	if (!WalkAndClear(*this, 1.0f, Fell))
	{
		return false;
	}
	TestTrue(TEXT("the city still stood at the bottom: nothing resolves while the player is inside"),
			 Fell.bCityStoodAtTheBottom);
	TestEqual(TEXT("three floors owed three days"), Fell.OwedAtTheBottom, 3);
	TestEqual(TEXT("the clear was counted"), Fell.ClearedAdded, 1);
	TestEqual(TEXT("as an ordinary dungeon's"), Fell.BasicClearedAdded, 1);
	TestTrue(TEXT("the walked dungeon is off the map"), Fell.bWalkedOffTheMap);
	TestTrue(TEXT("and the city fell in the walk's days, to the other dungeon"), Fell.bCityFell);
	TestEqual(TEXT("the walk's three days ran"), Fell.DaysPassed, 3);

	return true;
}

/**
 * Entering a dungeon while another is walked leaves the first, which runs the days its walk owes.
 *
 * RULED BY THE COORDINATING SESSION ON 2026-10-24: otherwise one walk's days would be carried into the next. The days
 * run with the first dungeon still the one the clock names, so its timer stays paused through them and the second's
 * moves; the second is entered after, and owes its own arrival.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnteringLeavesTheWalkFirstTest,
	"Cataclysm.DungeonMode.EnteringADungeonWhileAnotherIsWalkedLeavesItAndRunsTheDaysItOwes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnteringLeavesTheWalkFirstTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDeathEndsDungeonTest;

	FDeathScene Scene = MakeScene(0);
	if (!TestNotNull(TEXT("a test world was created"), Scene.World))
	{
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		return false;
	}

	const int32 CityId = AnOutpostOf(*Scene.Run);
	const int32 First = PlaceByHand(*Scene.Run, ECataclysmDungeonType::Basic, CityId, 10, 40.0f, 40.0f, 0.0f, 0.0f);
	const int32 Second = PlaceByHand(*Scene.Run, ECataclysmDungeonType::Basic, CityId, 10, 40.0f, 40.0f, 0.0f, 0.0f);
	if (!TestTrue(TEXT("set-up: two dungeons were placed"), First != INDEX_NONE && Second != INDEX_NONE)
		|| !EnterAndStand(*this, Scene, First)
		|| !TestTrue(TEXT("set-up: a descent was made"), Scene.Mode->GoDownOneFloor()))
	{
		return false;
	}

	const int32 Day = Scene.Run->Day();
	if (!TestEqual(TEXT("set-up: arriving and one descent owe two days"), Scene.Run->DaysOwed(), 2)
		|| !TestTrue(TEXT("CONTROL: a walk is under way"), Scene.Run->IsAWalkUnderWay()))
	{
		return false;
	}

	if (!TestTrue(TEXT("the second dungeon is entered"), Scene.Mode->EnterEmpireDungeon(Second)))
	{
		return false;
	}

	TestEqual(TEXT("the first walk's two days ran"), Scene.Run->Day(), Day + 2);
	TestEqual(TEXT("and the second walk owes only its own arrival"), Scene.Run->DaysOwed(), 1);
	TestEqual(TEXT("the game mode is walking the second"), Scene.Mode->EmpireDungeonId, Second);
	TestEqual(TEXT("and so is the clock"), Scene.Run->Clock->CurrentDungeonId, Second);
	TestNotNull(TEXT("the first is still on the map"), Scene.Run->FindDungeon(First));
	TestEqual(TEXT("its timer stayed paused through its own walk's days"),
			  Scene.Run->Clock->DaysUntilResolveFor(First), 40.0f, 0.001f);
	TestEqual(TEXT("while the second's ran through them"),
			  Scene.Run->Clock->DaysUntilResolveFor(Second), 38.0f, 0.001f);

	// AND LEAVING ENDS THE WALK: nothing is under way, so `Cataclysm.EmpireAdvance` may pass days again.
	Scene.Mode->LeaveEmpireDungeon();
	TestFalse(TEXT("after leaving no walk is under way"), Scene.Run->IsAWalkUnderWay());
	TestEqual(TEXT("and nothing is owed"), Scene.Run->DaysOwed(), 0);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
