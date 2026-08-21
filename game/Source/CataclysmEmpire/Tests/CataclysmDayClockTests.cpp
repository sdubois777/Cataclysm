// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "DayClock/CataclysmDayClock.h"

/**
 * Tests for the empire's day clock, issue #41.
 *
 * THE FIRST TESTS IN THE `CataclysmEmpire` MODULE, which until now held a build
 * file, an `IMPLEMENT_MODULE` and nothing else. They need no world, no actor and
 * no pawn, which is exactly what that module's build file asks for: "plain
 * arithmetic on plain structs", which "should stay testable without the combat,
 * rendering or input systems".
 *
 * WHAT THESE PIN AND WHAT `tools/tests/test_day_clock_port.py` PINS. These check
 * that the rules behave: that a day moves every timer but one, that a timer
 * running out reports a resolve exactly once, that the arithmetic lands on
 * hand-worked figures. The Python test checks something these cannot -- that the
 * constants still match `sim/cataclysm_sim/config.py`, which is where they came
 * from. A test written against a constant cannot notice that the constant is
 * wrong, so both are needed.
 */

namespace CataclysmDayClockTest
{
	UCataclysmDayClock* MakeClock()
	{
		return NewObject<UCataclysmDayClock>();
	}
}

// ---------------------------------------------------------------------------
// The arithmetic
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDayClockRunDaysTest,
	"Cataclysm.DayClock.AFloorCostsExactlyOneDay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDayClockRunDaysTest::RunTest(const FString& Parameters)
{
	// THE RULE THE WHOLE EMPIRE LAYER RESTS ON. Depth and time are the same axis,
	// so a dungeon can never be made cheaper without also being made poorer.
	// `CLAUDE.md` lists it among the rules that are easy to get wrong.
	//
	// HAND-WORKED FIGURES, NOT THE FORMULA WRITTEN OUT AGAIN. A test that
	// computed `Floors * DaysPerFloor` and compared it with `RunDaysFor` would
	// pass whatever `DaysPerFloor` was.
	TestEqual(TEXT("one floor is one day"), UCataclysmDayClock::RunDaysFor(1), 1);
	TestEqual(TEXT("twenty floors are twenty days"),
			  UCataclysmDayClock::RunDaysFor(20), 20);
	TestEqual(TEXT("a 60-floor metropolis dungeon is sixty days"),
			  UCataclysmDayClock::RunDaysFor(60), 60);
	TestEqual(TEXT("a 150-floor Cataclysm dungeon is a hundred and fifty days"),
			  UCataclysmDayClock::RunDaysFor(150), 150);

	// AND IT IS CLAMPED AT BOTH ENDS. A dungeon that took no days would be free,
	// and the empire layer's whole tension is that nothing is.
	TestEqual(TEXT("a dungeon of no floors still costs a day"),
			  UCataclysmDayClock::RunDaysFor(0), UCataclysmDayClock::LeastRunDays);
	TestEqual(TEXT("and so does a nonsensical negative one"),
			  UCataclysmDayClock::RunDaysFor(-10), UCataclysmDayClock::LeastRunDays);
	TestEqual(TEXT("a dungeon deeper than the clamp costs the clamp"),
			  UCataclysmDayClock::RunDaysFor(UCataclysmDayClock::MostRunDays + 50),
			  UCataclysmDayClock::MostRunDays);

	// THE CLAMP IS ABOVE ANY DUNGEON THE DESIGN DESCRIBES. Floor counts run to
	// over 150, and the Cataclysm boss dungeon grows by a floor for every dungeon
	// the player fails to clear, so the ceiling has to be well clear of the
	// deepest ordinary one rather than near it.
	TestTrue(FString::Printf(
		TEXT("the ceiling of %d days is well above a 150-floor dungeon"),
		UCataclysmDayClock::MostRunDays),
		UCataclysmDayClock::MostRunDays > 150);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDayClockResolveDaysTest,
	"Cataclysm.DayClock.ADeeperDungeonTakesLongerToResolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDayClockResolveDaysTest::RunTest(const FString& Parameters)
{
	// HAND-WORKED, for the reason above. 10 + floors * 1.6.
	TestEqual(TEXT("one floor resolves in 11.6 days"),
			  UCataclysmDayClock::ResolveDaysFor(1), 11.6f, 0.001f);
	TestEqual(TEXT("ten floors in 26 days"),
			  UCataclysmDayClock::ResolveDaysFor(10), 26.0f, 0.001f);
	TestEqual(TEXT("a 20-floor fallen village in 42 days"),
			  UCataclysmDayClock::ResolveDaysFor(20), 42.0f, 0.001f);
	TestEqual(TEXT("a 40-floor city in 74 days"),
			  UCataclysmDayClock::ResolveDaysFor(40), 74.0f, 0.001f);
	TestEqual(TEXT("a 60-floor metropolis in 106 days"),
			  UCataclysmDayClock::ResolveDaysFor(60), 106.0f, 0.001f);
	TestEqual(TEXT("a 150-floor Cataclysm dungeon in 250 days"),
			  UCataclysmDayClock::ResolveDaysFor(150), 250.0f, 0.001f);

	// **THE PROPERTY THE RATIO EXISTS FOR**, and it is worth checking rather than
	// trusting: every dungeon can be saved by somebody who goes straight there.
	// At a ratio of 1.0 the timer and the walk are the same length, so a dungeon
	// is exactly barely savable and nothing else can be. The design wants a
	// margin, and this is what that margin looks like at every depth the design
	// describes.
	for (int32 Floors = 1; Floors <= 150; ++Floors)
	{
		const float Resolve = UCataclysmDayClock::ResolveDaysFor(Floors);
		const int32 Walk = UCataclysmDayClock::RunDaysFor(Floors);

		if (!TestTrue(FString::Printf(
			TEXT("a %d-floor dungeon resolves in %.1f days and takes %d to walk, "
				 "so it can be saved by somebody who goes straight there"),
			Floors, Resolve, Walk), Resolve > Walk))
		{
			return false;
		}
	}

	// AND THE MARGIN GROWS WITH DEPTH RATHER THAN SHRINKING. That is what the
	// ratio being above 1 means, and it is the difference between a deep dungeon
	// being a commitment and a deep dungeon being impossible.
	const float ShallowMargin = UCataclysmDayClock::ResolveDaysFor(10)
		- UCataclysmDayClock::RunDaysFor(10);
	const float DeepMargin = UCataclysmDayClock::ResolveDaysFor(150)
		- UCataclysmDayClock::RunDaysFor(150);

	TestTrue(FString::Printf(
		TEXT("a deep dungeon leaves more slack than a shallow one: %.1f days "
			 "against %.1f"), DeepMargin, ShallowMargin),
		DeepMargin > ShallowMargin);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDayClockDeathCostTest,
	"Cataclysm.DayClock.DyingCostsMoreDaysInAHarsherMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDayClockDeathCostTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Standard costs 5 days"),
			  UCataclysmDayClock::DeathDayCostFor(0), 5);
	TestEqual(TEXT("Hardcore costs 10"),
			  UCataclysmDayClock::DeathDayCostFor(1), 10);
	TestEqual(TEXT("Heretic costs 15"),
			  UCataclysmDayClock::DeathDayCostFor(2), 15);

	// A MODE NOBODY CHOSE COSTS WHAT THE GENTLEST ONE COSTS. Falling back to the
	// harshest would punish a character for a number nobody typed.
	TestEqual(TEXT("a rung out of range costs what Standard costs"),
			  UCataclysmDayClock::DeathDayCostFor(40), 5);
	TestEqual(TEXT("and so does a negative one"),
			  UCataclysmDayClock::DeathDayCostFor(-1), 5);

	// AND DYING IS NEVER FREE. A mode whose death cost was zero would make the
	// design's own statement -- that dying costs days -- false for that mode.
	for (int32 Rung = 0; Rung <= 2; ++Rung)
	{
		TestTrue(FString::Printf(TEXT("mode %d costs days to die in"), Rung),
				 UCataclysmDayClock::DeathDayCostFor(Rung) > 0);
	}

	return true;
}

// ---------------------------------------------------------------------------
// The clock
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDayClockTicksTest,
	"Cataclysm.DayClock.ADayMovesEveryTimer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDayClockTicksTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDayClockTest;

	UCataclysmDayClock* Clock = MakeClock();
	if (!TestNotNull(TEXT("a day clock was made"), Clock))
	{
		return false;
	}

	TestEqual(TEXT("a new clock is on day zero"), Clock->Day, 0);
	TestEqual(TEXT("with nothing counting down"), Clock->Timers.Num(), 0);

	TestTrue(TEXT("a dungeon can be added"), Clock->AddDungeon(7, /*Floors=*/20));
	TestTrue(TEXT("and a second one"), Clock->AddDungeon(8, /*Floors=*/40));

	// THE SAME DUNGEON TWICE IS REFUSED. Two timers on one dungeon would bite the
	// same city twice for one dungeon being ignored once.
	TestFalse(TEXT("the same dungeon cannot be added twice"),
			  Clock->AddDungeon(7, /*Floors=*/20));
	TestEqual(TEXT("so the clock still holds two"), Clock->Timers.Num(), 2);

	// EACH STARTS ON ITS OWN DEPTH'S TIMER.
	TestEqual(TEXT("the 20-floor dungeon starts at 42 days"),
			  Clock->DaysUntilResolveFor(7), 42.0f, 0.001f);
	TestEqual(TEXT("the 40-floor one at 74"),
			  Clock->DaysUntilResolveFor(8), 74.0f, 0.001f);

	const TArray<int32> Resolved = Clock->AdvanceDay();

	TestEqual(TEXT("a day passes"), Clock->Day, 1);
	TestEqual(TEXT("and nothing resolves on the first day"), Resolved.Num(), 0);
	TestEqual(TEXT("the first timer moved by one day"),
			  Clock->DaysUntilResolveFor(7), 41.0f, 0.001f);
	TestEqual(TEXT("and so did the second"),
			  Clock->DaysUntilResolveFor(8), 73.0f, 0.001f);

	// A DUNGEON THE CLOCK DOES NOT KNOW ANSWERS -1 rather than 0, because 0 is a
	// real answer: it is a timer that has just run out.
	TestEqual(TEXT("a dungeon the clock has never heard of answers -1"),
			  Clock->DaysUntilResolveFor(999), -1.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDayClockPauseTest,
	"Cataclysm.DayClock.TheDungeonThePlayerIsInsideDoesNotTick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDayClockPauseTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDayClockTest;

	// **THE RULE THAT MAKES ENTERING A DUNGEON A DECISION RATHER THAN A GAMBLE.**
	// Its residents are busy fighting the player rather than marching on the
	// city, so the dungeon being walked is a guaranteed save. What it costs is
	// pure opportunity: thirty days in here is thirty days every other timer
	// advances without you.
	UCataclysmDayClock* Clock = MakeClock();
	if (!TestNotNull(TEXT("a day clock was made"), Clock))
	{
		return false;
	}

	Clock->AddDungeon(1, /*Floors=*/10);
	Clock->AddDungeon(2, /*Floors=*/10);
	Clock->AddDungeon(3, /*Floors=*/10);

	const float Started = Clock->DaysUntilResolveFor(1);

	// THE CONTROL FIRST: with the player nowhere, all three move together.
	Clock->AdvanceDays(5);

	TestEqual(TEXT("outside a dungeon, the first timer moved five days"),
			  Clock->DaysUntilResolveFor(1), Started - 5.0f, 0.001f);
	TestEqual(TEXT("and so did the second"),
			  Clock->DaysUntilResolveFor(2), Started - 5.0f, 0.001f);
	TestEqual(TEXT("and the third"),
			  Clock->DaysUntilResolveFor(3), Started - 5.0f, 0.001f);

	// NOW THE PLAYER WALKS INTO THE SECOND ONE.
	Clock->EnterDungeon(2);
	Clock->AdvanceDays(10);

	TestEqual(TEXT("ten more days pass on the first"),
			  Clock->DaysUntilResolveFor(1), Started - 15.0f, 0.001f);
	TestEqual(TEXT("and on the third"),
			  Clock->DaysUntilResolveFor(3), Started - 15.0f, 0.001f);
	TestEqual(TEXT("but the one the player is standing in has not moved since"),
			  Clock->DaysUntilResolveFor(2), Started - 5.0f, 0.001f);

	// AND THE DAY ITSELF PASSED ANYWAY. Standing in a dungeon costs days; it is
	// only that one dungeon's timer that is paused, not the world.
	TestEqual(TEXT("fifteen days have passed on the clock"), Clock->Day, 15);

	// LEAVING STARTS IT AGAIN.
	Clock->LeaveDungeon();
	Clock->AdvanceDay();

	TestEqual(TEXT("once the player leaves, that timer moves again"),
			  Clock->DaysUntilResolveFor(2), Started - 6.0f, 0.001f);
	TestEqual(TEXT("and the others carried on throughout"),
			  Clock->DaysUntilResolveFor(1), Started - 16.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDayClockResolveTest,
	"Cataclysm.DayClock.ATimerRunningOutResolvesOnceAndStartsAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDayClockResolveTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDayClockTest;

	UCataclysmDayClock* Clock = MakeClock();
	if (!TestNotNull(TEXT("a day clock was made"), Clock))
	{
		return false;
	}

	// A 10-FLOOR DUNGEON RESOLVES IN 26 DAYS.
	Clock->AddDungeon(1, /*Floors=*/10);
	const int32 Timer = 26;

	// NOTHING ON THE WAY THERE. A timer that reported a resolve early would take
	// a bite out of a city that had days left.
	const TArray<int32> Early = Clock->AdvanceDays(Timer - 1);
	TestEqual(FString::Printf(
		TEXT("nothing resolves in the first %d days"), Timer - 1),
		Early.Num(), 0);
	TestEqual(TEXT("with one day left on the timer"),
			  Clock->DaysUntilResolveFor(1), 1.0f, 0.001f);

	// AND EXACTLY ONE ON THE DAY IT RUNS OUT.
	const TArray<int32> OnTheDay = Clock->AdvanceDay();
	TestEqual(TEXT("it resolves on the day the timer runs out"),
			  OnTheDay.Num(), 1);
	if (OnTheDay.Num() == 1)
	{
		TestEqual(TEXT("and it is the right dungeon"), OnTheDay[0], 1);
	}

	// **IT DOES NOT VANISH, AND IT DOES NOT RESOLVE AGAIN TOMORROW.** A dungeon
	// that stayed at zero would bite its city every single day after the first,
	// which is the fault this checks for. Its timer goes back to full.
	TestEqual(TEXT("its timer is full again"),
			  Clock->DaysUntilResolveFor(1), 26.0f, 0.001f);
	TestEqual(TEXT("and it is still on the clock"), Clock->Timers.Num(), 1);

	const TArray<int32> DayAfter = Clock->AdvanceDay();
	TestEqual(TEXT("it does not resolve again the next day"), DayAfter.Num(), 0);

	// AND IT BITES AGAIN WHEN THE NEW TIMER RUNS OUT, which is what lets an
	// ignored city actually die rather than being hurt once and left alone.
	const TArray<int32> SecondTime = Clock->AdvanceDays(Timer);
	TestEqual(TEXT("it resolves a second time a full timer later"),
			  SecondTime.Num(), 1);

	if (const FCataclysmDungeonTimer* Found = Clock->FindTimer(1))
	{
		TestEqual(TEXT("and the dungeon has counted both"),
				  Found->TimesResolved, 2);
	}
	else
	{
		AddError(TEXT("the dungeon left the clock after resolving"));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDayClockManyDungeonsTest,
	"Cataclysm.DayClock.SeveralDungeonsCanRunOutOnTheSameDay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDayClockManyDungeonsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDayClockTest;

	// THE SITUATION THE WHOLE STRATEGY LAYER IS ABOUT: more dungeons running out
	// than the player can reach. A clock that reported only the first would hide
	// exactly the moment the game is built around.
	UCataclysmDayClock* Clock = MakeClock();
	if (!TestNotNull(TEXT("a day clock was made"), Clock))
	{
		return false;
	}

	Clock->AddDungeon(10, /*Floors=*/5);
	Clock->AddDungeon(11, /*Floors=*/5);
	Clock->AddDungeon(12, /*Floors=*/5);

	// 10 + 5 * 1.6 is 18 days.
	const TArray<int32> Resolved = Clock->AdvanceDays(18);

	TestEqual(TEXT("all three resolve on the same day"), Resolved.Num(), 3);
	TestTrue(TEXT("and every one of them is named"),
			 Resolved.Contains(10) && Resolved.Contains(11) && Resolved.Contains(12));

	// A DEEPER ONE ADDED ALONGSIDE OUTLASTS THEM, which is the shape of the
	// decision: the shallow dungeons bite first and the deep one is the
	// commitment.
	Clock->AddDungeon(13, /*Floors=*/60);
	const TArray<int32> Next = Clock->AdvanceDays(18);

	TestEqual(TEXT("the three shallow ones resolve again"), Next.Num(), 3);
	TestFalse(TEXT("and the deep one has not yet"), Next.Contains(13));
	TestTrue(FString::Printf(
		TEXT("it still has %.1f days left"), Clock->DaysUntilResolveFor(13)),
		Clock->DaysUntilResolveFor(13) > 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDayClockAdvanceDaysTest,
	"Cataclysm.DayClock.PassingManyDaysAtOnceDoesNotSkipTheResolvesBetween",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDayClockAdvanceDaysTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDayClockTest;

	// THE FAULT THIS RULES OUT IS SILENT AND WOULD MAKE THE GAME EASIER THAN IT
	// IS. Almost nothing costs one day: walking a 40-floor dungeon costs 40, and
	// dying costs 5 to 15. A caller that added 40 to the day and then looked
	// would find each timer 40 lower, and would have missed every resolve on the
	// way -- so a city that should have been bitten twice would not be bitten at
	// all.
	UCataclysmDayClock* Clock = MakeClock();
	if (!TestNotNull(TEXT("a day clock was made"), Clock))
	{
		return false;
	}

	// A 5-floor dungeon resolves every 18 days. Sixty days holds three of them.
	Clock->AddDungeon(1, /*Floors=*/5);

	const TArray<int32> Resolved = Clock->AdvanceDays(60);

	TestEqual(TEXT("sixty days pass"), Clock->Day, 60);
	TestEqual(TEXT("and three resolves happen along the way"), Resolved.Num(), 3);

	if (const FCataclysmDungeonTimer* Found = Clock->FindTimer(1))
	{
		TestEqual(TEXT("the dungeon counted all three"), Found->TimesResolved, 3);
	}
	else
	{
		AddError(TEXT("the dungeon left the clock"));
	}

	// IT AGREES WITH THE SAME DAYS PASSED ONE AT A TIME. That is the check that
	// makes this more than a restatement of the loop.
	UCataclysmDayClock* OneAtATime = MakeClock();
	if (!TestNotNull(TEXT("a second day clock was made"), OneAtATime))
	{
		return false;
	}
	OneAtATime->AddDungeon(1, /*Floors=*/5);

	int32 Separately = 0;
	for (int32 Passed = 0; Passed < 60; ++Passed)
	{
		Separately += OneAtATime->AdvanceDay().Num();
	}

	TestEqual(TEXT("passing sixty days at once matches sixty single days"),
			  Resolved.Num(), Separately);
	TestEqual(TEXT("and leaves the same timer"),
			  Clock->DaysUntilResolveFor(1),
			  OneAtATime->DaysUntilResolveFor(1), 0.001f);

	// PASSING NO DAYS PASSES NO DAYS.
	const int32 Before = Clock->Day;
	TestEqual(TEXT("passing zero days resolves nothing"),
			  Clock->AdvanceDays(0).Num(), 0);
	TestEqual(TEXT("and moves the clock not at all"), Clock->Day, Before);
	TestEqual(TEXT("and neither does a negative number of days"),
			  Clock->AdvanceDays(-5).Num(), 0);
	TestEqual(TEXT("which also leaves the clock alone"), Clock->Day, Before);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
