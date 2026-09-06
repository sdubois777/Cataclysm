// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Empire/CataclysmEmpireRun.h"

/**
 * Tests for the empire's day loop, issue #1084.
 *
 * WHAT THESE PIN THAT THE OTHER THREE FILES CANNOT.
 * `CataclysmDayClockTests.cpp`, `CataclysmEmpireMapTests.cpp` and
 * `CataclysmSurgeTests.cpp` each check one piece in isolation, and all three
 * passed while nothing joined them -- so an empire that never lost a city would
 * have looked exactly as healthy. These are the tests that can only be written
 * once something owns all three: that a wave lands, that its timers run out,
 * that a city takes the damage, that it falls, and that the fall opens the way
 * in.
 *
 * WHY SO MANY OF THEM MEASURE RATHER THAN ASSERT A FIGURE. A run is
 * deterministic from its seed but not predictable by hand: the wave sizes are
 * fixed, the cities they land on are not. So these check the rules that must
 * hold whatever the roll was -- a bite is exactly the share the dungeon's spec
 * says, an unattended empire always falls, two runs from one seed match step for
 * step -- rather than a day number that would only ever be true of one seed.
 */

namespace CataclysmEmpireRunTest
{
	UCataclysmEmpireRun* MakeRun(int32 Seed = 1,
								 ECataclysmSurgeMode Mode = ECataclysmSurgeMode::Static,
								 int32 LethalityRung = 0)
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->Begin(Seed, Mode, LethalityRung);
		return Run;
	}

	/** Every city's defence, indexed by city identifier. */
	TArray<float> DefenceSnapshot(const UCataclysmEmpireRun& Run)
	{
		TArray<float> Defence;
		for (const FCataclysmCity& City : Run.Map->Cities)
		{
			Defence.Add(City.Defence);
		}
		return Defence;
	}
}

// ---------------------------------------------------------------------------
// Starting
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunBeginTest,
	"Cataclysm.EmpireRun.ARunBeginsWithAnIntactEmpireAndASurgeDue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunBeginTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	UCataclysmEmpireRun* Run = MakeRun();

	if (!TestNotNull(TEXT("the run has a map"), Run->Map.Get())
		|| !TestNotNull(TEXT("a clock"), Run->Clock.Get())
		|| !TestNotNull(TEXT("and a surge schedule"), Run->Surges.Get()))
	{
		return false;
	}

	TestEqual(TEXT("the empire is whole"), Run->Map->FallenCityCount(), 0);
	TestEqual(TEXT("twenty-five cities stand"), Run->Map->Cities.Num(), 25);
	TestEqual(TEXT("three cities from defeat"), Run->Map->DistanceToDefeat(), 3);
	TestFalse(TEXT("and the run is not lost"), Run->IsLost());

	TestEqual(TEXT("no day has passed"), Run->Day(), 0);
	TestEqual(TEXT("nothing is standing on the map"), Run->DungeonCount(), 0);
	TestEqual(TEXT("and the clock is counting nothing down"),
			  Run->Clock->Timers.Num(), 0);

	// "A SURGE IS TRIGGERED AT RUN START", so the first day advanced brings one.
	TestEqual(TEXT("no surge has fired yet"), Run->Surges->SurgesFired, 0);

	const FCataclysmDayReport First = Run->AdvanceDay();

	TestEqual(TEXT("the first day is day 1"), First.Day, 1);
	TestTrue(TEXT("and it surged"), First.bSurged);
	TestEqual(TEXT("bringing four dungeons"), First.Spawned.Num(), 4);
	TestEqual(TEXT("which are now standing"), Run->DungeonCount(), 4);
	TestEqual(TEXT("and the clock is counting all four down"),
			  Run->Clock->Timers.Num(), 4);

	// AND THEY ALL LANDED ON THE FRONTIER, which on day 1 is the twelve rim
	// Outposts.
	for (const int32 DungeonId : First.Spawned)
	{
		const FCataclysmDungeon* Dungeon = Run->FindDungeon(DungeonId);
		if (!TestNotNull(TEXT("a spawned dungeon is on the map"), Dungeon))
		{
			return false;
		}

		TestTrue(FString::Printf(
			TEXT("dungeon %d landed on an Outpost"), DungeonId),
			Dungeon->CityTier == ECataclysmCityTier::Outpost);
		TestEqual(FString::Printf(TEXT("dungeon %d arrived on day 1"), DungeonId),
				  Dungeon->SpawnedDay, 1);
	}

	// A DUNGEON THAT ARRIVED TODAY LOSES A DAY TODAY, which is what the model
	// does: the surge lands and then every timer moves.
	const FCataclysmDungeon* Any = Run->FindDungeon(First.Spawned[0]);
	TestEqual(TEXT("and it has already lost a day off its timer"),
			  Run->Clock->DaysUntilResolveFor(Any->DungeonId),
			  Any->ResolveDays - 1.0f, 0.001f);

	// THE NEXT SURGE IS 120 DAYS OFF, not immediately again.
	TestFalse(TEXT("no surge is due tomorrow"), Run->Surges->IsDue(2));
	TestEqual(TEXT("the next is due on day 121"),
			  Run->Surges->NextSurgeDay, 121.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunDayNumberTest,
	"Cataclysm.EmpireRun.TheReportedDayIsAlwaysTheClocksDay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunDayNumberTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	UCataclysmEmpireRun* Run = MakeRun();

	// THIS IS NOT A TRIVIAL CHECK. `AdvanceDay` has to know today's number
	// BEFORE it asks the clock to move, because a surge lands between the day
	// advancing and the timers moving -- so it computes the clock's day plus
	// one. If the clock ever advanced by anything other than exactly one, that
	// would be silently wrong and every other test here would still pass: the
	// wrong day number only shows up in a surge landing on the wrong day.
	int32 SurgeDays = 0;
	bool bSurgedOnDayOne = false;

	for (int32 Expected = 1; Expected <= 200; ++Expected)
	{
		const FCataclysmDayReport Report = Run->AdvanceDay();

		TestEqual(FString::Printf(TEXT("day %d reports itself"), Expected),
				  Report.Day, Expected);
		TestEqual(FString::Printf(TEXT("and the clock agrees on day %d"),
								  Expected),
				  Run->Clock->Day, Expected);

		if (!Report.bSurged)
		{
			continue;
		}

		++SurgeDays;
		bSurgedOnDayOne = bSurgedOnDayOne || Expected == 1;

		// AND THE SCHEDULE WAS SET FROM TODAY'S NUMBER. This is what the day
		// number being right actually buys: a surge that fired believing it was
		// a different day would push the next one to the wrong day, and nothing
		// else in this file would notice.
		//
		// MEASURED AGAINST THE DAY IT FIRED, NOT AGAINST A FIXED DAY. A city
		// falling fires a surge of its own and that resets the countdown too, so
		// "the next one is on day 121" is only true of a run in which nothing
		// was lost -- and this one loses two cities inside two hundred days.
		TestEqual(FString::Printf(
			TEXT("the surge on day %d puts the next one 120 days later"),
			Expected),
			Run->Surges->NextSurgeDay,
			static_cast<float>(Expected) + UCataclysmSurgeScheduler::IntervalDays,
			0.001f);
	}

	// "A SURGE IS TRIGGERED AT RUN START."
	TestTrue(TEXT("a surge fired on the first day"), bSurgedOnDayOne);
	TestTrue(FString::Printf(
		TEXT("%d days of the two hundred surged"), SurgeDays),
		SurgeDays >= 2);

	return true;
}

// ---------------------------------------------------------------------------
// What a dungeon costs a city
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunBiteTest,
	"Cataclysm.EmpireRun.ADungeonResolvingTakesExactlyItsPointsOfDefence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunBiteTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	UCataclysmEmpireRun* Run = MakeRun();

	int32 BitesChecked = 0;
	int32 BesiegedBitesChecked = 0;

	// TWO HUNDRED DAYS, WATCHING EVERY RESOLVE. Which dungeon resolves on which
	// day depends on the roll; what must hold whatever the roll was is that the
	// defence lost is exactly the dungeon's own damage in POINTS, scaled by how
	// deep it is. The city's own maximum is deliberately not in that expression
	// -- issue #1331 -- and this test was renamed when it left.
	for (int32 Days = 0; Days < 200; ++Days)
	{
		const TArray<float> Before = DefenceSnapshot(*Run);

		// Read the dungeons before the day, because a city falling absorbs them.
		TMap<int32, FCataclysmDungeon> Standing;
		for (const FCataclysmDungeon& Dungeon : Run->Dungeons)
		{
			Standing.Add(Dungeon.DungeonId, Dungeon);
		}

		const FCataclysmDayReport Report = Run->AdvanceDay();

		// Gather what each city was bitten for today, so several dungeons
		// resolving on one city are added up rather than compared one at a time.
		TMap<int32, float> Expected;
		for (const int32 DungeonId : Report.Resolved)
		{
			const FCataclysmDungeon* Dungeon = Standing.Find(DungeonId);
			if (Dungeon == nullptr)
			{
				continue;
			}

			Expected.FindOrAdd(Dungeon->CityId) +=
				Dungeon->DefenceDamage * Dungeon->BiteScale();
		}

		for (const TPair<int32, float>& Pair : Expected)
		{
			const FCataclysmCity* City = Run->Map->Find(Pair.Key);
			if (City == nullptr)
			{
				continue;
			}

			if (City->bFallen)
			{
				// It fell today. Its defence is zeroed by the fall rather than
				// left at whatever the bite reduced it to, so the arithmetic
				// below does not apply. The fall itself is a separate test.
				continue;
			}

			const float Lost = Before[Pair.Key] - City->Defence;

			// THE CITY'S MAXIMUM IS NOT IN THIS EXPRESSION, and that absence is
			// the whole of issue #1331. It used to read
			// `City->MaxDefence * Pair.Value`.
			const float Points = Pair.Value;

			// A BESIEGED CITY LOSES MORE THAN THE RESOLVE, AND THAT IS CORRECT.
			// A Siege takes its own share of its host every day whether or not
			// anything resolved, so on a day that also carries a resolve the
			// city loses both. This test is about what a RESOLVE takes, so a
			// besieged city is checked for the weaker property instead: it lost
			// at least the resolve's points, and strictly more than them. How
			// much more is pinned by the Siege's own tests further down this
			// file, and writing that arithmetic out again here would only
			// duplicate the code under test.
			if (Report.Besieged.Contains(Pair.Key))
			{
				TestTrue(FString::Printf(
					TEXT("on day %d, besieged %s lost %.2f defence, more than "
						 "the %.2f its resolves alone would take"),
					Report.Day, *City->Name, Lost, Points),
					Lost > Points);

				++BesiegedBitesChecked;
				continue;
			}

			TestEqual(FString::Printf(
				TEXT("on day %d, %s lost %.2f defence, which is its dungeons' "
					 "damage in points"),
				Report.Day, *City->Name, Lost),
				Lost, Points, 0.01f);

			++BitesChecked;
		}
	}

	// AND SOMETHING ACTUALLY HAPPENED. Without this the loop above would pass
	// on a run where no dungeon ever resolved, which is the failure it exists to
	// catch.
	TestTrue(FString::Printf(
		TEXT("%d bites were checked over two hundred days"), BitesChecked),
		BitesChecked > 5);

	// THE SECOND CONTROL, ADDED WITH THE SIEGE. If every resolve in two hundred
	// days happened to land on a besieged city, the exact check above would have
	// been skipped every time and the count above would be the thing that
	// noticed. This says plainly how the two hundred days split, so a future
	// change that quietly moved every resolve into the besieged branch shows up
	// as a number rather than as silence.
	AddInfo(FString::Printf(
		TEXT("%d resolves on unbesieged cities, %d on besieged ones"),
		BitesChecked, BesiegedBitesChecked));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunPersistTest,
	"Cataclysm.EmpireRun.AnIgnoredDungeonBitesTheSameCityAgainAndAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunPersistTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	UCataclysmEmpireRun* Run = MakeRun();

	TMap<int32, int32> TimesResolved;

	for (int32 Days = 0; Days < 400; ++Days)
	{
		const FCataclysmDayReport Report = Run->AdvanceDay();

		for (const int32 DungeonId : Report.Resolved)
		{
			++TimesResolved.FindOrAdd(DungeonId);
		}
	}

	// A DUNGEON THAT RESOLVES DOES NOT GO AWAY. This is what lets an ignored
	// city actually die rather than being nibbled once and left alone, and it is
	// the reason `bDungeonPersistsAfterResolve` exists.
	int32 RepeatOffenders = 0;
	for (const TPair<int32, int32>& Pair : TimesResolved)
	{
		if (Pair.Value > 1)
		{
			++RepeatOffenders;
		}
	}

	TestTrue(FString::Printf(
		TEXT("%d of %d dungeons resolved more than once in four hundred days"),
		RepeatOffenders, TimesResolved.Num()),
		RepeatOffenders > 0);

	// AND CITIES ARE ACTUALLY DYING BY NOW.
	TestTrue(FString::Printf(
		TEXT("%d cities have been lost in four hundred unattended days"),
		Run->Map->FallenCityCount()),
		Run->Map->FallenCityCount() > 0);

	return true;
}

// ---------------------------------------------------------------------------
// A city falling
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunFallTest,
	"Cataclysm.EmpireRun.ACityFallingOpensALaneFiresASurgeAndAbsorbsItsDungeons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunFallTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	UCataclysmEmpireRun* Run = MakeRun();

	int32 FallsSeen = 0;

	for (int32 Days = 0; Days < 600 && FallsSeen < 3; ++Days)
	{
		// What was standing where, before the day.
		TMap<int32, TArray<int32>> StandingOn;
		for (const FCataclysmDungeon& Dungeon : Run->Dungeons)
		{
			StandingOn.FindOrAdd(Dungeon.CityId).Add(Dungeon.DungeonId);
		}

		const int32 SurgesBefore = Run->Surges->SurgesFired;

		const FCataclysmDayReport Report = Run->AdvanceDay();

		if (Report.Fallen.IsEmpty())
		{
			continue;
		}

		++FallsSeen;

		for (const int32 CityId : Report.Fallen)
		{
			const FCataclysmCity* City = Run->Map->Find(CityId);
			if (!TestNotNull(TEXT("a fallen city exists"), City))
			{
				return false;
			}

			TestTrue(FString::Printf(TEXT("%s has fallen"), *City->Name),
					 City->bFallen);
			TestEqual(FString::Printf(
				TEXT("%s has nothing left defending it"), *City->Name),
				City->Defence, 0.0f);

			// THE LANE BEHIND IT IS OPEN. Every city it was shielding can now be
			// attacked, which is the whole consequence of losing one.
			for (const int32 Behind : City->Inward)
			{
				const FCataclysmCity* Sealed = Run->Map->Find(Behind);
				if (Sealed != nullptr && !Sealed->bFallen)
				{
					TestTrue(FString::Printf(
						TEXT("%s behind it is now exposed"), *Sealed->Name),
						Run->Map->IsExposed(Behind));
				}
			}

			// AND EVERY DUNGEON THAT WAS ON IT IS GONE, REPLACED BY THE ONE
			// THE CITY BECAME. Until issue #1324 this asserted that nothing at
			// all was left standing, which was true while a fall only removed
			// dungeons. A fallen city is now a Dungeon City standing on itself,
			// so the count is one rather than zero -- and it is checked to BE
			// that dungeon rather than merely counted, because a count of one
			// would also pass if an absorbed dungeon had been left behind.
			const TArray<int32> Standing = Run->DungeonsOn(CityId);

			if (TestEqual(FString::Printf(
					TEXT("only the dungeon %s became stands on it"),
					*City->Name),
					Standing.Num(), 1))
			{
				const FCataclysmDungeon* Left = Run->FindDungeon(Standing[0]);
				if (TestNotNull(TEXT("and it is on the map"), Left))
				{
					TestEqual(FString::Printf(
						TEXT("and %s became a Fallen City"), *City->Name),
						Left->Type, ECataclysmDungeonType::FallenCity);
				}
			}

			if (const TArray<int32>* WasStanding = StandingOn.Find(CityId))
			{
				for (const int32 DungeonId : *WasStanding)
				{
					TestTrue(FString::Printf(
						TEXT("dungeon %d was recorded as absorbed"), DungeonId),
						Report.Absorbed.Contains(DungeonId));
					TestNull(FString::Printf(
						TEXT("and dungeon %d is off the map"), DungeonId),
						Run->FindDungeon(DungeonId));
					TestEqual(FString::Printf(
						TEXT("and dungeon %d is off the clock"), DungeonId),
						Run->Clock->DaysUntilResolveFor(DungeonId), -1.0f);
				}
			}
		}

		// A CITY FALLING FIRES A SURGE OF ITS OWN, one per city lost.
		TestTrue(TEXT("the day it fell also surged"), Report.bSurged);
		TestTrue(FString::Printf(
			TEXT("at least one surge fired on the day %d cities fell"),
			Report.Fallen.Num()),
			Run->Surges->SurgesFired >= SurgesBefore + Report.Fallen.Num());
	}

	TestTrue(FString::Printf(
		TEXT("%d cities fell within six hundred unattended days"), FallsSeen),
		FallsSeen >= 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunLostTest,
	"Cataclysm.EmpireRun.AnUnattendedEmpireAlwaysLosesThePathToThePillar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunLostTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	// THE WHOLE POINT OF THE LAYER, AND THE ONE THING NONE OF THE THREE PIECES
	// COULD SHOW ON ITS OWN. Nobody clears a dungeon, so the Cataclysm walks in.
	// If this ever stops happening the empire game is decoration.
	//
	// FIVE SEEDS, NOT ONE. A single seed could lose by luck; the claim is that
	// there is no run in which ignoring the empire is survivable.
	for (int32 Seed = 1; Seed <= 5; ++Seed)
	{
		UCataclysmEmpireRun* Run = MakeRun(Seed);

		int32 LostOn = INDEX_NONE;

		for (int32 Days = 0; Days < 2000; ++Days)
		{
			const FCataclysmDayReport Report = Run->AdvanceDay();

			if (Report.bPillarExposed)
			{
				LostOn = Report.Day;
				break;
			}
		}

		if (!TestTrue(FString::Printf(
			TEXT("seed %d loses the path to the Pillar within 2000 days"), Seed),
			LostOn != INDEX_NONE))
		{
			continue;
		}

		TestTrue(TEXT("the run is lost"), Run->IsLost());
		TestEqual(TEXT("no cities stand between the Cataclysm and the Pillar"),
				  Run->Map->DistanceToDefeat(), 0);

		// AND IT GOT THERE BY LOSING A LANE, NOT BY LOSING EVERYTHING. A run
		// that had to lose all 24 cities first would mean the lane rule was
		// doing nothing.
		TestTrue(FString::Printf(
			TEXT("seed %d lost on day %d having lost %d of 24 cities"),
			Seed, LostOn, Run->Map->FallenCityCount()),
			Run->Map->FallenCityCount() < 24);

		// A SANCTUARY IS WHAT OPENS THE PILLAR. Nothing else can.
		bool bASanctuaryFell = false;
		for (const FCataclysmCity& City : Run->Map->Cities)
		{
			bASanctuaryFell = bASanctuaryFell
				|| (City.bFallen && City.Tier == ECataclysmCityTier::Sanctuary);
		}

		TestTrue(TEXT("and a Sanctuary is what fell to open it"), bASanctuaryFell);
	}

	return true;
}

// ---------------------------------------------------------------------------
// The run is one thing, and it is reproducible
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunSeedTest,
	"Cataclysm.EmpireRun.TheSameSeedGivesTheSameRunAndADifferentOneDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunSeedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	UCataclysmEmpireRun* First = MakeRun(11);
	UCataclysmEmpireRun* Again = MakeRun(11);
	UCataclysmEmpireRun* Other = MakeRun(12);

	for (int32 Days = 0; Days < 300; ++Days)
	{
		const FCataclysmDayReport A = First->AdvanceDay();
		const FCataclysmDayReport B = Again->AdvanceDay();

		TestEqual(FString::Printf(TEXT("day %d spawns the same count"), A.Day),
				  B.Spawned.Num(), A.Spawned.Num());
		TestEqual(FString::Printf(TEXT("day %d resolves the same count"), A.Day),
				  B.Resolved.Num(), A.Resolved.Num());
		TestEqual(FString::Printf(TEXT("day %d loses the same cities"), A.Day),
				  B.Fallen.Num(), A.Fallen.Num());

		for (int32 Index = 0; Index < A.Fallen.Num(); ++Index)
		{
			TestEqual(TEXT("and the same city by number"),
					  B.Fallen[Index], A.Fallen[Index]);
		}

		Other->AdvanceDay();
	}

	TestEqual(TEXT("after three hundred days both runs stand where the other "
				   "does"),
			  Again->Map->FallenCityCount(), First->Map->FallenCityCount());
	TestEqual(TEXT("with the same number of dungeons on the map"),
			  Again->DungeonCount(), First->DungeonCount());
	TestEqual(TEXT("and the same distance from defeat"),
			  Again->Map->DistanceToDefeat(), First->Map->DistanceToDefeat());

	// AND THE MAPS THEMSELVES ARE THE SAME PICTURE.
	TestEqual(TEXT("the two empires look identical"),
			  Again->Map->Render(), First->Map->Render());

	// A DIFFERENT SEED IS A DIFFERENT RUN. Without this the checks above would
	// pass on a run that ignored its seed entirely.
	TestNotEqual(TEXT("a different seed gives a different empire"),
				 Other->Map->Render(), First->Map->Render());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunManyDaysTest,
	"Cataclysm.EmpireRun.AdvancingManyDaysAtOnceIsTheSameAsOneAtATime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunManyDaysTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	// WHAT THIS GUARDS AGAINST IS A CALLER SKIPPING TIME. Almost nothing in this
	// game costs one day -- walking a 40-floor dungeon costs 40, dying costs 5 to
	// 15 -- so a caller that added 40 to the day would skip 40 days of surges,
	// resolves and city falls.
	UCataclysmEmpireRun* OneAtATime = MakeRun(21);
	UCataclysmEmpireRun* AllAtOnce = MakeRun(21);

	for (int32 Days = 0; Days < 250; ++Days)
	{
		OneAtATime->AdvanceDay();
	}

	const TArray<FCataclysmDayReport> Reports = AllAtOnce->AdvanceDays(250);

	if (!TestEqual(TEXT("two hundred and fifty days give two hundred and fifty "
					   "reports"), Reports.Num(), 250))
	{
		return false;
	}

	TestEqual(TEXT("the last report is day 250"), Reports.Last().Day, 250);
	TestEqual(TEXT("both runs are on day 250"),
			  AllAtOnce->Day(), OneAtATime->Day());
	TestEqual(TEXT("both empires look the same"),
			  AllAtOnce->Map->Render(), OneAtATime->Map->Render());
	TestEqual(TEXT("with the same dungeons standing"),
			  AllAtOnce->DungeonCount(), OneAtATime->DungeonCount());
	TestEqual(TEXT("and the same number of surges behind them"),
			  AllAtOnce->Surges->SurgesFired, OneAtATime->Surges->SurgesFired);

	// AND ZERO OR FEWER DAYS PASSES NO TIME AT ALL.
	const int32 DayBefore = AllAtOnce->Day();
	TestEqual(TEXT("no days gives no reports"), AllAtOnce->AdvanceDays(0).Num(), 0);
	TestEqual(TEXT("a negative number gives none either"),
			  AllAtOnce->AdvanceDays(-5).Num(), 0);
	TestEqual(TEXT("and the day has not moved"), AllAtOnce->Day(), DayBefore);

	return true;
}

// ---------------------------------------------------------------------------
// Clearing a dungeon
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunClearTest,
	"Cataclysm.EmpireRun.ClearingADungeonTakesItOffTheMapAndOffTheClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunClearTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	UCataclysmEmpireRun* Run = MakeRun();

	const FCataclysmDayReport First = Run->AdvanceDay();

	if (!TestEqual(TEXT("four dungeons are standing"), Run->DungeonCount(), 4))
	{
		return false;
	}

	const int32 DungeonId = First.Spawned[0];
	const FCataclysmDungeon* Dungeon = Run->FindDungeon(DungeonId);
	if (!TestNotNull(TEXT("the first one is on the map"), Dungeon))
	{
		return false;
	}

	TestTrue(TEXT("it is cleared"), Run->ClearDungeon(DungeonId));

	// BOTH LISTS, WHICH IS THE WHOLE REASON THIS IS A METHOD RATHER THAN A
	// CALLER REACHING INTO TWO ARRAYS. A dungeon left on the clock would keep
	// biting a city that no longer has a dungeon on it.
	TestNull(TEXT("it is off the map"), Run->FindDungeon(DungeonId));
	TestEqual(TEXT("and off the clock"),
			  Run->Clock->DaysUntilResolveFor(DungeonId), -1.0f);
	TestEqual(TEXT("three are left"), Run->DungeonCount(), 3);
	TestEqual(TEXT("and the clock is counting three down"),
			  Run->Clock->Timers.Num(), 3);

	TestFalse(TEXT("clearing it twice does nothing"), Run->ClearDungeon(DungeonId));
	TestFalse(TEXT("nor does clearing one that never existed"),
			  Run->ClearDungeon(9999));

	// AND IT NEVER RESOLVES AGAIN. Two hundred days is long enough for its timer
	// to have run out several times over.
	//
	// MEASURED AS "THAT DUNGEON NEVER RESOLVES" RATHER THAN "THAT CITY LOSES NO
	// DEFENCE", because the surge on day 121 puts more dungeons on the map and
	// one of them may well land on the same city. A test written the second way
	// would fail for a reason that has nothing to do with clearing.
	bool bResolvedAgain = false;
	for (const FCataclysmDayReport& Report : Run->AdvanceDays(200))
	{
		bResolvedAgain = bResolvedAgain || Report.Resolved.Contains(DungeonId);
	}

	TestFalse(TEXT("the cleared dungeon never resolves again"), bResolvedAgain);

	return true;
}

// ---------------------------------------------------------------------------
// Spending part of a day
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunPartialDayTest,
	"Cataclysm.EmpireRun.PartsOfADayAddUpToWholeDaysAndNoMore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunPartialDayTest::RunTest(const FString& Parameters)
{
	UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
	Run->Begin(/* InSeed */ 606, ECataclysmSurgeMode::Static);

	const int32 Started = Run->Day();

	// A FIFTY FLOOR DUNGEON COSTING TWO DAYS charges a twenty-fifth of a day a
	// floor. That is the case the whole feature exists for: an invested player
	// running a deep dungeon without paying its full time cost.
	const float PerFloor = 2.0f / 50.0f;

	// TWENTY-FOUR FLOORS IS UNDER A DAY, so no day passes at all.
	for (int32 Floor = 0; Floor < 24; ++Floor)
	{
		TestEqual(*FString::Printf(TEXT("floor %d passes no whole day"), Floor),
				  Run->SpendFloorTime(PerFloor).Num(), 0);
	}

	TestEqual(TEXT("after twenty-four floors the day has not moved"),
			  Run->Day(), Started);

	// THE TWENTY-FIFTH TIPS IT OVER, and the day that comes out is a real day:
	// its report is the same shape as one from `AdvanceDay`.
	const TArray<FCataclysmDayReport> Reports = Run->SpendFloorTime(PerFloor);

	if (!TestEqual(TEXT("the twenty-fifth floor spends one whole day"),
				   Reports.Num(), 1))
	{
		return false;
	}

	TestEqual(TEXT("and the day moved by exactly one"), Run->Day(),
			  Started + 1);
	TestEqual(TEXT("the report is for that day"), Reports[0].Day, Started + 1);

	// THE REST OF THE WALK COSTS THE SECOND DAY AND NOT A THIRD. Fifty floors at
	// a twenty-fifth of a day is two days, and a walk that quietly cost three
	// would be the upgrade failing to do what it says.
	int32 MoreDays = 0;
	for (int32 Floor = 25; Floor < 50; ++Floor)
	{
		MoreDays += Run->SpendFloorTime(PerFloor).Num();
	}

	TestEqual(TEXT("the whole fifty floor walk cost two days"), Run->Day(),
			  Started + 2);
	TestEqual(TEXT("one of which came from the second half"), MoreDays, 1);

	// A FLOOR COSTING MORE THAN A DAY SPENDS ALL OF IT AT ONCE, rather than
	// leaving the remainder to be paid by the next floor.
	const int32 BeforeBigStep = Run->Day();

	TestEqual(TEXT("a floor costing three days spends three"),
			  Run->SpendFloorTime(3.0f).Num(), 3);
	TestEqual(TEXT("and the day moved by three"), Run->Day(),
			  BeforeBigStep + 3);

	// NOTHING AND LESS THAN NOTHING PASS NO TIME. A caller with no rate should
	// pass zero, and a negative number can only be a mistake; winding the clock
	// back would give a player days they had already spent.
	const int32 BeforeNothing = Run->Day();

	TestEqual(TEXT("zero passes no time"), Run->SpendFloorTime(0.0f).Num(), 0);
	TestEqual(TEXT("and neither does a negative"),
			  Run->SpendFloorTime(-5.0f).Num(), 0);
	TestEqual(TEXT("so the day did not move"), Run->Day(), BeforeNothing);

	return true;
}

// ---------------------------------------------------------------------------
// A Siege costs its host something every day
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunSiegeBitesDailyTest,
	"Cataclysm.EmpireRun.ASiegeTakesItsShareEveryDayAndOtherDungeonsTakeNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunSiegeBitesDailyTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	// A SEED WHOSE FIRST WAVE CONTAINS A SIEGE, found by looking. Siege is 10 in
	// 100 and a wave is four dungeons, so most seeds have one, but not all.
	int32 SiegeSeed = INDEX_NONE;
	for (int32 Seed = 1; Seed <= 200 && SiegeSeed == INDEX_NONE; ++Seed)
	{
		UCataclysmEmpireRun* Trial = MakeRun(Seed);
		Trial->AdvanceDay();

		for (const FCataclysmDungeon& Dungeon : Trial->Dungeons)
		{
			if (Dungeon.SubType == ECataclysmDungeonSubType::Siege)
			{
				SiegeSeed = Seed;
				break;
			}
		}
	}

	if (!TestTrue(TEXT("a seed whose first wave holds a Siege was found"),
				  SiegeSeed != INDEX_NONE))
	{
		return false;
	}

	UCataclysmEmpireRun* Run = MakeRun(SiegeSeed);
	Run->AdvanceDay();

	// WHICH CITIES ARE BESIEGED, AND WHICH HOLD ONLY ORDINARY DUNGEONS. The
	// second list is the control: without it, a bite applied to every city with
	// any dungeon on it would pass every figure below.
	TSet<int32> Besieged;
	TSet<int32> Ordinary;

	for (const FCataclysmDungeon& Dungeon : Run->Dungeons)
	{
		if (Dungeon.SubType == ECataclysmDungeonSubType::Siege)
		{
			Besieged.Add(Dungeon.CityId);
		}
		else
		{
			Ordinary.Add(Dungeon.CityId);
		}
	}

	Ordinary = Ordinary.Difference(Besieged);

	if (!TestTrue(TEXT("at least one city is besieged"), Besieged.Num() > 0))
	{
		return false;
	}

	// A DAY WITH NO RESOLVES IN IT, so the only thing that can move a city's
	// defence is the Siege. Resolve timers are at least ten days plus the depth,
	// so the day after the first wave is safely clear of them.
	TMap<int32, float> DefenceBefore;
	TMap<int32, float> PopulationBefore;
	TMap<int32, float> MaxDefence;
	TMap<int32, float> MaxPopulation;

	for (const FCataclysmCity& City : Run->Map->Cities)
	{
		DefenceBefore.Add(City.CityId, City.Defence);
		PopulationBefore.Add(City.CityId, City.Population);
		MaxDefence.Add(City.CityId, City.MaxDefence);
		MaxPopulation.Add(City.CityId, City.MaxPopulation);
	}

	const FCataclysmDayReport Report = Run->AdvanceDay();

	if (!TestEqual(TEXT("no dungeon resolved on the day measured"),
				   Report.Resolved.Num(), 0))
	{
		return false;
	}

	// THE FIGURE THE DESIGN DOCUMENT STATES: one per cent of the city's maximum
	// per day, plus ten points for every day the Siege has already stood. The
	// day measured is the day after the wave landed, so exactly one day's growth
	// is in it. Written out from the constants rather than as 0.01 and 10, so a
	// change to either fails this rather than silently passing.
	const float Grown = UCataclysmEmpireRun::SiegeDamageGrowthPerDay * 1.0f;

	for (const int32 CityId : Besieged)
	{
		const FCataclysmCity* City = Run->Map->Find(CityId);
		if (!TestNotNull(TEXT("the besieged city is still on the map"), City))
		{
			continue;
		}

		TestEqual(*FString::Printf(
					  TEXT("city %d lost 1%% of its maximum defence plus a "
						   "day's growth"), CityId),
				  DefenceBefore[CityId] - City->Defence,
				  MaxDefence[CityId]
					  * UCataclysmEmpireRun::SiegeDefenceBitePerDay + Grown,
				  0.01f);

		TestEqual(*FString::Printf(
					  TEXT("city %d lost 1%% of its maximum population plus a "
						   "day's growth"), CityId),
				  PopulationBefore[CityId] - City->Population,
				  MaxPopulation[CityId]
					  * UCataclysmEmpireRun::SiegePopulationBitePerDay + Grown,
				  0.01f);

		TestTrue(*FString::Printf(TEXT("and the day's report names city %d"),
								  CityId),
				 Report.Besieged.Contains(CityId));
	}

	// THE CONTROL. A city carrying only ordinary dungeons loses nothing at all
	// on a day when nothing resolves. Every other dungeon is free until its
	// timer runs out, and that is what makes a Siege the one you cannot leave.
	for (const int32 CityId : Ordinary)
	{
		const FCataclysmCity* City = Run->Map->Find(CityId);
		if (City == nullptr)
		{
			continue;
		}

		TestEqual(*FString::Printf(
					  TEXT("city %d holds only ordinary dungeons and lost no "
						   "defence"), CityId),
				  City->Defence, DefenceBefore[CityId], 0.0001f);

		TestFalse(*FString::Printf(
					  TEXT("so the report does not name city %d"), CityId),
				  Report.Besieged.Contains(CityId));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunSiegeAloneFellsACityTest,
	"Cataclysm.EmpireRun.ASiegesFlatShareIsOfTheMaximumAndNotTheRemainder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunSiegeAloneFellsACityTest::RunTest(
	const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	// WHY THIS TEST AND NOT ONLY THE ONE ABOVE. One per cent of what a city HAS
	// LEFT would never reach zero, and a test measuring a single day cannot tell
	// the two readings apart -- the first day's bite is the same either way.
	// This is the test that can: a hundred bites of one per cent of the maximum
	// empty a city, and a hundred bites of one per cent of the remainder leave
	// it at about 37% of where it started.
	UCataclysmEmpireRun* Run = MakeRun(1);

	UCataclysmEmpireMap* Map = Run->Map;
	if (!TestNotNull(TEXT("the run has a map"), Map))
	{
		return false;
	}

	FCataclysmCity* City = nullptr;
	for (FCataclysmCity& Candidate : Map->Cities)
	{
		if (Candidate.Tier == ECataclysmCityTier::Outpost)
		{
			City = &Candidate;
			break;
		}
	}

	if (!TestNotNull(TEXT("an Outpost was found"), City))
	{
		return false;
	}

	const int32 CityId = City->CityId;
	const float StartingDefence = City->Defence;

	// BITTEN DIRECTLY, ONE DAY AT A TIME, rather than by standing a dungeon on
	// it and advancing a hundred days. Advancing days would fire surges, land
	// more dungeons and resolve timers, and the figure at the end would be the
	// sum of all of it rather than the Siege's own arithmetic.
	int32 DaysToEmpty = 0;
	for (int32 Day = 1; Day <= 200; ++Day)
	{
		const bool bFell = Map->Bite(
			CityId, UCataclysmEmpireRun::SiegeDefenceBitePerDay,
			UCataclysmEmpireRun::SiegePopulationBitePerDay);

		if (bFell)
		{
			DaysToEmpty = Day;
			break;
		}
	}

	// A RANGE AND NOT AN EXACT DAY, because a hundred subtractions of one per
	// cent of the maximum need not land exactly on zero in floating point, and a
	// test that demanded day 100 would be testing the rounding rather than the
	// rule. What discriminates the two readings is that it ENDS: one per cent of
	// the remainder would leave the city at about 37% after a hundred days and
	// above zero for ever, so any finite answer here rules that reading out.
	TestTrue(*FString::Printf(
				 TEXT("a Siege alone empties an untouched city, and did so on "
					  "day %d"), DaysToEmpty),
			 DaysToEmpty >= 100 && DaysToEmpty <= 101);

	TestTrue(TEXT("the city really did start full"),
			 StartingDefence > 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunOneSiegePerCityTest,
	"Cataclysm.EmpireRun.ACityNeverHoldsTwoSieges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunOneSiegePerCityTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	// MANY RUNS, EACH WALKED FOR A LONG TIME. A single run rarely puts two
	// dungeons of any kind on one city early on; the rule only comes under
	// pressure once waves have been landing for a while, and the dungeon cap is
	// what stops it being impossible.
	int32 CitiesSeenWithASiege = 0;

	for (int32 Seed = 1; Seed <= 8; ++Seed)
	{
		UCataclysmEmpireRun* Run = MakeRun(Seed);

		for (int32 Day = 0; Day < 500; ++Day)
		{
			Run->AdvanceDay();

			TMap<int32, int32> SiegesOn;

			for (const FCataclysmDungeon& Dungeon : Run->Dungeons)
			{
				if (Dungeon.SubType == ECataclysmDungeonSubType::Siege)
				{
					++SiegesOn.FindOrAdd(Dungeon.CityId);
				}
			}

			for (const TPair<int32, int32>& Pair : SiegesOn)
			{
				++CitiesSeenWithASiege;

				if (Pair.Value > UCataclysmSurgeScheduler::SiegesPerCity)
				{
					AddError(FString::Printf(
						TEXT("seed %d day %d: city %d holds %d Sieges, and the "
							 "design allows %d"),
						Seed, Day, Pair.Key, Pair.Value,
						UCataclysmSurgeScheduler::SiegesPerCity));
					return false;
				}
			}
		}
	}

	// THE CONTROL. If no city ever held a Siege at all, the loop above would
	// have found no violation and proved nothing.
	TestTrue(TEXT("cities carrying a Siege were actually seen"),
			 CitiesSeenWithASiege > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunSiegeGrowsTest,
	"Cataclysm.EmpireRun.ASiegeHurtsMoreEachDayItIsLeftStanding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunSiegeGrowsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	// BUILT BY HAND RATHER THAN ROLLED. A run walked for many days would also be
	// taking resolve bites, landing new waves and losing cities, and the figure
	// at the end would be the sum of all of it. This stands one Siege on one
	// city and moves the clock, so the only thing that can change the damage is
	// the day count.
	UCataclysmEmpireRun* Run = MakeRun(1);

	UCataclysmEmpireMap* Map = Run->Map;
	if (!TestNotNull(TEXT("the run has a map"), Map))
	{
		return false;
	}

	// THE PILLAR, WHICH IS THE DEEPEST POCKET ON THE MAP. Twenty thousand
	// defence takes 47 days of a growing Siege to empty, which leaves room to
	// measure several days without the city falling part way through.
	int32 CityId = INDEX_NONE;
	float MaxDefence = 0.0f;

	for (const FCataclysmCity& City : Map->Cities)
	{
		if (City.Tier == ECataclysmCityTier::Pillar)
		{
			CityId = City.CityId;
			MaxDefence = City.MaxDefence;
			break;
		}
	}

	if (!TestTrue(TEXT("the Pillar was found"), CityId != INDEX_NONE))
	{
		return false;
	}

	// EVERY OTHER DUNGEON REMOVED, so nothing else can bite anything. The wave
	// that lands on day one is not wanted here.
	Run->AdvanceDay();

	while (Run->DungeonCount() > 0)
	{
		Run->ClearDungeon(Run->Dungeons[0].DungeonId);
	}

	// ONE SIEGE, STANDING FROM TODAY. Its resolve timer is set far enough out
	// that it cannot run out during the days measured, so the only damage the
	// city takes is the daily one.
	FCataclysmDungeon Siege;
	Siege.DungeonId = 9000;
	Siege.SubType = ECataclysmDungeonSubType::Siege;
	Siege.CityId = CityId;
	Siege.Floors = 10;
	Siege.SpawnedDay = Run->Day();
	Siege.ResolveDays = 10000.0f;

	Run->Dungeons.Add(Siege);
	Run->Clock->AddDungeon(Siege.DungeonId, Siege.Floors);
	Run->Clock->SetResolveDays(Siege.DungeonId, Siege.ResolveDays);

	// FIVE DAYS, EACH MEASURED ON ITS OWN. The first day after it arrived is one
	// day's growth, and each later day is one more.
	TArray<float> Taken;

	for (int32 Step = 0; Step < 5; ++Step)
	{
		const FCataclysmCity* Before = Map->Find(CityId);
		const float DefenceBefore = Before ? Before->Defence : 0.0f;

		const FCataclysmDayReport Report = Run->AdvanceDay();

		if (!TestTrue(TEXT("nothing resolved on the day measured"),
					  Report.Resolved.IsEmpty()))
		{
			return false;
		}

		const FCataclysmCity* After = Map->Find(CityId);
		Taken.Add(DefenceBefore - (After ? After->Defence : 0.0f));
	}

	// TEN, WRITTEN OUT, BECAUSE THE DESIGN DOCUMENT SAYS TEN. Every other
	// assertion in this test compares against
	// `UCataclysmEmpireRun::SiegeDamageGrowthPerDay`, and a test whose expected
	// value is read out of the same constant the code reads cannot notice that
	// the constant is wrong: setting it to zero would make this test expect no
	// growth, find no growth, and pass.
	//
	// FOUND BY BREAKING IT. A `prove_cpp_guard` run on 2026-09-05 set that
	// constant to zero and all fifteen tests in this file still passed. This
	// line and the strict comparison below are what that run was missing.
	TestEqual(TEXT("the design's ten points a day"),
			  UCataclysmEmpireRun::SiegeDamageGrowthPerDay, 10.0f, 0.0001f);

	// EACH DAY TAKES EXACTLY TEN MORE POINTS THAN THE ONE BEFORE IT. Checked as
	// the difference between consecutive days rather than against an absolute
	// figure, so it does not depend on which day the Siege happened to arrive.
	for (int32 Step = 1; Step < Taken.Num(); ++Step)
	{
		// STRICTLY MORE, CHECKED SEPARATELY FROM HOW MUCH MORE. This one holds
		// whatever the constant is set to, so it fails for a Siege that stopped
		// growing even if somebody changed the number the line above pins.
		TestTrue(*FString::Printf(
					 TEXT("day %d took strictly more than day %d (%.1f against "
						  "%.1f)"),
					 Step + 1, Step, Taken[Step], Taken[Step - 1]),
				 Taken[Step] > Taken[Step - 1]);

		TestEqual(*FString::Printf(
					  TEXT("day %d took ten more points of defence than day %d "
						   "(%.1f against %.1f)"),
					  Step + 1, Step, Taken[Step], Taken[Step - 1]),
				  Taken[Step] - Taken[Step - 1],
				  UCataclysmEmpireRun::SiegeDamageGrowthPerDay, 0.01f);
	}

	// AND THE FIRST OF THEM IS THE FLAT SHARE PLUS ONE DAY'S GROWTH, which is
	// what pins where the counting starts. A Siege that grew from the day before
	// it arrived would take ten points more on every one of these days and the
	// differences above would not notice.
	TestEqual(TEXT("the first day after it arrived is the flat share plus one "
				   "day's growth"),
			  Taken[0],
			  MaxDefence * UCataclysmEmpireRun::SiegeDefenceBitePerDay
				  + UCataclysmEmpireRun::SiegeDamageGrowthPerDay,
			  0.01f);

	return true;
}

/**
 * A Siege still takes a SHARE of the city's maximum, and that is a ruling.
 *
 * THE ONE THING ISSUE #1331 DELIBERATELY DID NOT CHANGE. Every dungeon resolve
 * became a number of points, because a share of the maximum divided out of how
 * long a city survived and made every city-health upgrade worthless. The project
 * owner was asked whether the Siege should follow and answered on 2026-09-05,
 * verbatim: "Keep it as a deliberate exception (Recommended)" -- a siege does
 * not care how thick your walls are.
 *
 * SO THIS TEST ASSERTS THE COST OF THAT RULING RATHER THAN ONLY EXPLAINING IT.
 * A city that bought `Architect_Increase_max_defense_by_20` loses a fifth MORE
 * defence to a Siege than one that did not, which reads exactly like the defect
 * #1331 was filed about and is the owner's decision.
 *
 * WITH THE CONTROL BESIDE IT: the same two cities lose identical points to an
 * ordinary dungeon resolving. Without the control this test would pass on an
 * implementation where nothing had changed at all.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunSiegeIgnoresCityHealthTest,
	"Cataclysm.EmpireRun.CityHealthDoesNotProtectAgainstASiegeAndThatIsTheRuling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunSiegeIgnoresCityHealthTest::RunTest(
	const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	UCataclysmEmpireRun* Run = MakeRun(1);

	UCataclysmEmpireMap* Map = Run->Map;
	if (!TestNotNull(TEXT("the run has a map"), Map))
	{
		return false;
	}

	// TWO OUTPOSTS, so the comparison is of the ceiling and not of the tier.
	TArray<int32> Outposts;
	for (const FCataclysmCity& City : Map->Cities)
	{
		if (City.Tier == ECataclysmCityTier::Outpost && Outposts.Num() < 2)
		{
			Outposts.Add(City.CityId);
		}
	}

	if (!TestEqual(TEXT("two Outposts were found"), Outposts.Num(), 2))
	{
		return false;
	}

	const int32 Plain = Outposts[0];
	const int32 Raised = Outposts[1];

	FCataclysmCityUpgrade Bigger;
	Bigger.RowName = FName(TEXT("Architect_Increase_max_defense_by_20"));
	Bigger.Effect = ECataclysmCityUpgradeEffect::MaxDefence;
	Bigger.Value = 0.2f;

	TestTrue(TEXT("the second Outpost buys a fifth more maximum defence"),
			 Map->AddUpgrade(Raised, Bigger));

	// EVERY ROLLED DUNGEON REMOVED, so nothing but the two hand-built Sieges can
	// touch either city. The wave that lands on day one is not wanted here.
	Run->AdvanceDay();

	while (Run->DungeonCount() > 0)
	{
		Run->ClearDungeon(Run->Dungeons[0].DungeonId);
	}

	// ONE SIEGE ON EACH, ARRIVING ON THE SAME DAY, so both carry exactly the
	// same growth and the only thing that can differ is the flat share.
	int32 NextId = 9100;
	for (const int32 CityId : { Plain, Raised })
	{
		FCataclysmDungeon Siege;
		Siege.DungeonId = NextId++;
		Siege.SubType = ECataclysmDungeonSubType::Siege;
		Siege.CityId = CityId;
		Siege.Floors = 10;
		Siege.SpawnedDay = Run->Day();
		Siege.ResolveDays = 10000.0f;

		Run->Dungeons.Add(Siege);
		Run->Clock->AddDungeon(Siege.DungeonId, Siege.Floors);
		Run->Clock->SetResolveDays(Siege.DungeonId, Siege.ResolveDays);
	}

	const float PlainBefore = Map->Find(Plain)->Defence;
	const float RaisedBefore = Map->Find(Raised)->Defence;
	const float PlainMax = Map->Find(Plain)->MaxDefence;
	const float RaisedMax = Map->Find(Raised)->MaxDefence;

	if (!TestEqual(TEXT("the raised city really is a fifth bigger"), RaisedMax,
				   PlainMax * 1.2f, 0.01f))
	{
		return false;
	}

	const FCataclysmDayReport Report = Run->AdvanceDay();

	if (!TestTrue(TEXT("nothing resolved on the day measured"),
				  Report.Resolved.IsEmpty()))
	{
		return false;
	}

	const float PlainLost = PlainBefore - Map->Find(Plain)->Defence;
	const float RaisedLost = RaisedBefore - Map->Find(Raised)->Defence;

	// ONE DAY'S GROWTH IN EACH, since both arrived on the same day.
	const float Grown = UCataclysmEmpireRun::SiegeDamageGrowthPerDay;

	TestEqual(TEXT("the plain city loses 1% of its own maximum plus the growth"),
			  PlainLost,
			  PlainMax * UCataclysmEmpireRun::SiegeDefenceBitePerDay + Grown,
			  0.01f);

	TestEqual(TEXT("and the raised city loses 1% of ITS larger maximum, which "
				   "is more"),
			  RaisedLost,
			  RaisedMax * UCataclysmEmpireRun::SiegeDefenceBitePerDay + Grown,
			  0.01f);

	// THE RULING, STATED AS A STRICT INEQUALITY. Buying city health makes a
	// Siege hurt MORE in absolute terms, not less. If this line ever fails, the
	// Siege has stopped being the exception and `docs/DECISIONS.md`, the owner's
	// 2026-09-05 ruling and `UCataclysmEmpireRun::ApplySiegeDamage` all need
	// revisiting -- it is not a test to relax.
	TestTrue(*FString::Printf(
				 TEXT("a Siege takes %.1f from the city with the bigger ceiling "
					  "and %.1f from the plain one"),
				 RaisedLost, PlainLost),
			 RaisedLost > PlainLost + 0.01f);

	// THE CONTROL: THE SAME TWO CITIES, HIT BY AN ORDINARY RESOLVE, LOSE THE
	// SAME POINTS. This is what changed in #1331, and without it the assertions
	// above would pass on the code as it was before.
	const FCataclysmDungeonSpec Spec = UCataclysmSurgeScheduler::SpecFor(
		ECataclysmDungeonType::Basic, ECataclysmCityTier::Outpost);

	const float PlainBeforeResolve = Map->Find(Plain)->Defence;
	const float RaisedBeforeResolve = Map->Find(Raised)->Defence;

	Map->Damage(Plain, Spec.DefenceDamage, 0.0f);
	Map->Damage(Raised, Spec.DefenceDamage, 0.0f);

	TestEqual(TEXT("an ordinary resolve takes the same points from both"),
			  PlainBeforeResolve - Map->Find(Plain)->Defence,
			  RaisedBeforeResolve - Map->Find(Raised)->Defence, 0.01f);

	TestEqual(TEXT("which is the dungeon spec's own number"),
			  PlainBeforeResolve - Map->Find(Plain)->Defence,
			  Spec.DefenceDamage, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunBesiegedCityCannotBuyTest,
	"Cataclysm.EmpireRun.ABesiegedCityCannotBuyAnUpgradeButKeepsTheOnesItHas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunBesiegedCityCannotBuyTest::RunTest(
	const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	UCataclysmEmpireRun* Run = MakeRun(1);

	UCataclysmEmpireMap* Map = Run->Map;
	if (!TestNotNull(TEXT("the run has a map"), Map))
	{
		return false;
	}

	int32 CityId = INDEX_NONE;
	for (const FCataclysmCity& City : Map->Cities)
	{
		if (City.Tier == ECataclysmCityTier::Outpost)
		{
			CityId = City.CityId;
			break;
		}
	}

	if (!TestTrue(TEXT("an Outpost was found"), CityId != INDEX_NONE))
	{
		return false;
	}

	FCataclysmCityUpgrade Upgrade;
	Upgrade.RowName = FName(TEXT("Architect_This_city_has_25_more_defense"));
	Upgrade.Effect = ECataclysmCityUpgradeEffect::MaxDefence;
	Upgrade.Value = 0.25f;

	// THE CONTROL FIRST. With no Siege standing, this city can buy it. Without
	// this the refusal below could be any of eight other reasons.
	if (!TestEqual(TEXT("an unbesieged city can buy the upgrade"),
				   static_cast<uint8>(
					   Run->WouldBuyCityUpgrade(CityId, Upgrade)),
				   static_cast<uint8>(ECataclysmCityUpgradeResult::Bought)))
	{
		return false;
	}

	// AND IT REALLY BOUGHT IT, so there is an existing upgrade to check keeps
	// working while the city is besieged.
	FCataclysmCityUpgrade Bought;
	Bought.RowName = FName(TEXT("Architect_This_city_repairs_5_defense_every_7_d"));
	Bought.Effect = ECataclysmCityUpgradeEffect::ResistDefenceLoss;
	Bought.Value = 0.25f;

	TestEqual(TEXT("and a second one goes through as well"),
			  static_cast<uint8>(Run->BuyCityUpgrade(CityId, Bought)),
			  static_cast<uint8>(ECataclysmCityUpgradeResult::Bought));

	const FCataclysmCity* City = Map->Find(CityId);
	if (!TestNotNull(TEXT("the city is still there"), City))
	{
		return false;
	}

	const float ResistedBefore =
		City->UpgradeValueFor(ECataclysmCityUpgradeEffect::ResistDefenceLoss);

	TestTrue(TEXT("the bought resistance is doing something"),
			 ResistedBefore > 0.0f);

	// NOW A SIEGE STANDS ON IT.
	FCataclysmDungeon Siege;
	Siege.DungeonId = 9100;
	Siege.SubType = ECataclysmDungeonSubType::Siege;
	Siege.CityId = CityId;
	Siege.Floors = 10;
	Siege.SpawnedDay = Run->Day();
	Siege.ResolveDays = 10000.0f;

	Run->Dungeons.Add(Siege);

	TestTrue(TEXT("the city reports itself besieged"), Run->IsBesieged(CityId));

	TestEqual(TEXT("and it can no longer buy an upgrade"),
			  static_cast<uint8>(Run->WouldBuyCityUpgrade(CityId, Upgrade)),
			  static_cast<uint8>(
				  ECataclysmCityUpgradeResult::CityIsBesieged));

	TestEqual(TEXT("nor actually buy one"),
			  static_cast<uint8>(Run->BuyCityUpgrade(CityId, Upgrade)),
			  static_cast<uint8>(
				  ECataclysmCityUpgradeResult::CityIsBesieged));

	// WHAT IT ALREADY BOUGHT STILL WORKS, which is the half of the design's
	// "pauses city upgrades" that a Siege does NOT do. The owner's answer stops
	// buying and stops building; it does not switch off what a city already has.
	const FCataclysmCity* Besieged = Map->Find(CityId);
	if (!TestNotNull(TEXT("the besieged city is still there"), Besieged))
	{
		return false;
	}

	TestEqual(TEXT("the resistance it bought before the Siege still applies"),
			  Besieged->UpgradeValueFor(
				  ECataclysmCityUpgradeEffect::ResistDefenceLoss),
			  ResistedBefore, 0.0001f);

	// AND ANOTHER CITY IS UNAFFECTED. The refusal is about this city, not about
	// there being a Siege anywhere on the map.
	int32 OtherId = INDEX_NONE;
	for (const FCataclysmCity& Other : Map->Cities)
	{
		if (Other.CityId != CityId
			&& Other.Tier == ECataclysmCityTier::Outpost)
		{
			OtherId = Other.CityId;
			break;
		}
	}

	if (TestTrue(TEXT("a second Outpost was found"), OtherId != INDEX_NONE))
	{
		TestFalse(TEXT("the other city is not besieged"),
				  Run->IsBesieged(OtherId));

		TestEqual(TEXT("and it can still buy"),
				  static_cast<uint8>(
					  Run->WouldBuyCityUpgrade(OtherId, Upgrade)),
				  static_cast<uint8>(ECataclysmCityUpgradeResult::Bought));
	}

	return true;
}


// ---------------------------------------------------------------------------
// What a city becomes when it falls
// ---------------------------------------------------------------------------

/**
 * Issue #1324 slice 2. A city that falls becomes a dungeon standing on itself.
 *
 * WHY IT DRIVES A REAL RUN RATHER THAN BUILDING THE STATE BY HAND. The
 * interesting quantity is how many dungeons the city was carrying when it fell,
 * and that is produced by surges landing over many days. A hand-built city with
 * three dungeons pushed onto it would test the maker, which
 * `Cataclysm.Surge.AFallenCityIsAsDeepAsTheSiegeThatTookIt` already does; this
 * tests that the run wires it up.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunFallenCityTest,
	"Cataclysm.EmpireRun.AFallenCityBecomesADungeonStandingOnItself",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunFallenCityTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	UCataclysmEmpireRun* Run = MakeRun();

	int32 FallsChecked = 0;

	for (int32 Days = 0; Days < 600 && FallsChecked < 3; ++Days)
	{
		// HOW MANY WERE STANDING, READ BEFORE THE DAY. The fall absorbs them,
		// so afterwards the only record is the report.
		TMap<int32, int32> StandingOn;
		for (const FCataclysmDungeon& Dungeon : Run->Dungeons)
		{
			++StandingOn.FindOrAdd(Dungeon.CityId);
		}

		const FCataclysmDayReport Report = Run->AdvanceDay();

		for (const int32 CityId : Report.Fallen)
		{
			const FCataclysmCity* City = Run->Map->Find(CityId);
			if (City == nullptr)
			{
				continue;
			}

			++FallsChecked;

			const int32 WasCarrying = StandingOn.FindRef(CityId);

			const TArray<int32> Standing = Run->DungeonsOn(CityId);
			if (!TestEqual(TEXT("one dungeon stands on it"), Standing.Num(), 1))
			{
				return false;
			}

			const FCataclysmDungeon* Left = Run->FindDungeon(Standing[0]);
			if (!TestNotNull(TEXT("and it is on the map"), Left))
			{
				return false;
			}

			TestEqual(TEXT("it is a Fallen City"), Left->Type,
					  ECataclysmDungeonType::FallenCity);

			// AS DEEP AS THE SIEGE THAT TOOK THE CITY, OR THE TIER'S MINIMUM.
			// The design's rule, and the minimum is the spec's shallow end.
			const FCataclysmDungeonSpec Spec =
				UCataclysmSurgeScheduler::SpecFor(
					ECataclysmDungeonType::FallenCity, City->Tier);

			TestEqual(FString::Printf(
				TEXT("%s fell carrying %d, so its dungeon is %d deep"),
				*City->Name, WasCarrying,
				FMath::Max(WasCarrying, Spec.LeastFloors)),
				Left->Floors, FMath::Max(WasCarrying, Spec.LeastFloors));

			// ONE BOSS PER DUNGEON THAT WAS STANDING. The design's one stated
			// exception to a boss on the final floor and nowhere else.
			TestEqual(TEXT("and one boss per dungeon that was standing"),
					  Left->Bosses, WasCarrying);

			// AND IT TAKES NOTHING MORE FROM THE CITY, EVER.
			TestEqual(TEXT("it takes no defence"), Left->DefenceDamage, 0.0f);
			TestEqual(TEXT("and no population"), Left->PopulationDamage, 0.0f);

			// ITS TIMER IS PAST THE END OF ANY RUN, so it never appears in a
			// day report as having resolved.
			TestTrue(FString::Printf(
				TEXT("its timer is %.0f days, past any run"), Left->ResolveDays),
				Left->ResolveDays >= 999.0f);

			// AND THE TWO LISTS AGREE. Adding a dungeon without its timer is
			// the corruption `DungeonsAgreeWithTimers` exists to catch.
			FString Why;
			TestTrue(TEXT("the dungeons and the timers still agree"),
					 UCataclysmEmpireRun::DungeonsAgreeWithTimers(
						 Run->Dungeons, Run->Clock->Timers, Why));
		}
	}

	TestTrue(FString::Printf(TEXT("%d cities fell to check"), FallsChecked),
			 FallsChecked > 0);

	return true;
}

/**
 * Issue #1324 slice 2. Beating the dungeon a city became takes the city back.
 *
 * UNTIL THIS, `UCataclysmEmpireMap::Retake` HAD NO CALLER AT ALL. Retaking a
 * city was implemented and unreachable: nothing in the game could restore one.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunRetakeTest,
	"Cataclysm.EmpireRun.ClearingAFallenCityTakesTheCityBackAtHalf",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunRetakeTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	UCataclysmEmpireRun* Run = MakeRun();

	int32 Retaken = 0;

	for (int32 Days = 0; Days < 600 && Retaken < 2; ++Days)
	{
		const FCataclysmDayReport Report = Run->AdvanceDay();

		for (const int32 CityId : Report.Fallen)
		{
			const FCataclysmCity* Before = Run->Map->Find(CityId);
			if (Before == nullptr || !Before->bFallen)
			{
				continue;
			}

			const TArray<int32> Standing = Run->DungeonsOn(CityId);
			if (Standing.Num() != 1)
			{
				continue;
			}

			const float MaxDefence = Before->MaxDefence;
			const float MaxPopulation = Before->MaxPopulation;

			if (!TestTrue(TEXT("clearing it succeeds"),
						  Run->ClearDungeon(Standing[0])))
			{
				return false;
			}

			++Retaken;

			const FCataclysmCity* After = Run->Map->Find(CityId);
			if (!TestNotNull(TEXT("the city is still there"), After))
			{
				return false;
			}

			TestFalse(TEXT("it has been retaken"), After->bFallen);

			// HALF ITS MAXIMUM, NOT ALL OF IT. Decided by the project owner on
			// 2026-09-06, verbatim "Half its maximum, upgrades intact, can fall
			// again". `docs/Cataclysm_GDD_v2.md` section VIII now says so; before
			// issue #1324 the figure was inferable only from the Tier 4 keystone
			// that improves on it.
			//
			// AT LEAST HALF RATHER THAN EXACTLY HALF, because a city that bought
			// `RestoreDefenceOnClear` is repaired again by the same clear, on
			// top of the half. Asserting equality would fail for that city only,
			// which is a flake nobody would attribute to an upgrade.
			TestTrue(FString::Printf(
				TEXT("%s came back with %.0f of %.0f defence"),
				*After->Name, After->Defence, MaxDefence),
				After->Defence >= MaxDefence * 0.5f - 0.01f);

			TestEqual(FString::Printf(
				TEXT("%s came back with half its people"), *After->Name),
				After->Population, MaxPopulation * 0.5f, 0.01f);

			TestTrue(TEXT("and it is not restored to full"),
					 After->Defence < MaxDefence);

			// THE DUNGEON IS GONE WITH IT.
			TestEqual(TEXT("nothing stands on it any more"),
					  Run->DungeonsOn(CityId).Num(), 0);

			// AND IT CAN FALL AGAIN. The owner's answer says so in as many
			// words, and it follows from the flag being cleared rather than the
			// city being marked safe.
			TestTrue(TEXT("it is alive again"), After->IsAlive());
		}
	}

	TestTrue(FString::Printf(TEXT("%d cities were retaken"), Retaken),
			 Retaken > 0);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
