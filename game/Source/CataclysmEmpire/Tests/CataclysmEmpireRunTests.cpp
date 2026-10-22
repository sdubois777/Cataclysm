// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Empire/CataclysmCityUpgrade.h"
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
	// THE OPENING WAVE, `OpeningDungeonsPerSurge`: 12 since 2026-10-20, and
	// 5 until then, when the first wave was the size of every other.
	TestEqual(TEXT("bringing twelve dungeons"), First.Spawned.Num(), 12);
	TestEqual(TEXT("which are now standing"), Run->DungeonCount(), 12);
	TestEqual(TEXT("and the clock is counting all twelve down"),
			  Run->Clock->Timers.Num(), 12);

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

	// THE NEXT SURGE IS 90 DAYS OFF, not immediately again.
	TestFalse(TEXT("no surge is due tomorrow"), Run->Surges->IsDue(2));
	TestEqual(TEXT("the next is due on day 91"),
			  Run->Surges->NextSurgeDay, 91.0f, 0.001f);

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
		// "the next one is on day 91" is only true of a run in which nothing
		// was lost -- and this one loses two cities inside two hundred days.
		TestEqual(FString::Printf(
			TEXT("the surge on day %d puts the next one 90 days later"),
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

		// AND WHAT EACH QUEST TIMER HIT ITS HOST FOR. Since 2026-10-13 a Quest
		// dungeon's timer hits a standing host with the ORDINARY row of the
		// host's tier at a scale of one -- `SpecFor(Basic, Host->Tier)` in
		// `UCataclysmEmpireRun::ResolveDungeon` -- and not with its own row,
		// which is nought. The Quest dungeon's id is in `Resolved`, so the loop
		// above gives its city an entry of nought; this adds the hit to the
		// city it actually landed on. `QuestHits` holds one city id per hit, so
		// a city hit by two Quest timers is added twice, as the code takes it
		// twice. No city in this run has bought a resistance, so the row is
		// what `Map->Damage` takes, the same as for the ordinary bite above.
		// A host that fell is skipped below with every other fallen city.
		//
		// THIS WAS A FAULT IN THE TEST FROM 2026-10-13 UNTIL 2026-10-21. It
		// printed, on day 46, "Outpost (-1,-2) lost 70.00 defence, which is its
		// dungeons' damage in points" against an expected nought. It was
		// exposed by the larger waves of 2026-10-20, which moved the run's
		// random stream onto days with a Quest hit. Ruled by the coordinating
		// session, 2026-10-21; `docs/DECISIONS.md`, the entry of 2026-10-20.
		for (const int32 HostId : Report.QuestHits)
		{
			const FCataclysmCity* Host = Run->Map->Find(HostId);
			if (Host == nullptr)
			{
				continue;
			}

			Expected.FindOrAdd(HostId) += UCataclysmSurgeScheduler::SpecFor(
				ECataclysmDungeonType::Basic, Host->Tier).DefenceDamage;
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

/**
 * A city falling opens the lane behind it, absorbs its dungeons, and may fire
 * a surge.
 *
 * AN UNATTENDED RUN FROM SEED 1, watched until a city has fallen on three
 * separate days or 600 days have passed.
 *
 * EVERY FALL OPENS THE LANE AND ABSORBS THE ORDINARY DUNGEONS. Those are
 * asserted on every city that falls. A QUEST DUNGEON IS NOT ABSORBED, since
 * 2026-10-21: it moves, or with nowhere to go stays, and what is asserted of
 * it is that it is still on the map and on the clock and was not counted.
 *
 * NOT EVERY FALL FIRES A SURGE. Since issue #2380 a fall fires one only when
 * the last surge is at least 25 days behind, which is what
 * `UCataclysmSurgeScheduler::IsDueOnCityFall` answers. This test asks that
 * before each day and asserts the surge only on a fall day where the answer
 * was yes. Until 2026-10-13 it was named
 * `ACityFallingOpensALaneFiresASurgeAndAbsorbsItsDungeons` and asserted a
 * surge on every fall day.
 *
 * THE REFUSED CASE IS HELD BY ANOTHER TEST,
 * `Cataclysm.EmpireRun.ACityFallingInsideTheLeastIntervalStillFallsAndBringsNoWave`.
 * This one asserts nothing about the surge on a day the answer was no.
 *
 * WHERE EVERYTHING STANDS: nowhere. No actor and no world; a run is an object.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunFallMaySurgeTest,
	"Cataclysm.EmpireRun.ACityFallingOpensALaneAbsorbsItsDungeonsAndMayFireASurge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunFallMaySurgeTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	UCataclysmEmpireRun* Run = MakeRun();

	int32 FallsSeen = 0;

	// HOW MANY OF THOSE DAYS HAD A FALL'S SURGE DUE AND DID SURGE. Counted for
	// the closing assertion, so fall days that were all inside the least
	// interval do not pass this test having checked no surge at all.
	int32 FallDaysDueThatSurged = 0;

	// WATCHED UNTIL THREE FALLS HAVE BEEN SEEN AND ONE OF THEM HAD A FALL'S
	// SURGE DUE AND SURGED, still bounded at six hundred days. Until 2026-10-21
	// the loop stopped at the third fall, `FallsSeen < 3`, and with the larger
	// waves of 2026-10-20 this seed's first three falls all came within the
	// least interval of a surge, so it printed "0 of the 3 days a city fell had
	// a fall's surge due and surged". Ruled by the coordinating session,
	// 2026-10-21, with every assertion unchanged; `docs/DECISIONS.md`, the
	// entry of 2026-10-20.
	for (int32 Days = 0;
		 Days < 600 && (FallsSeen < 3 || FallDaysDueThatSurged < 1);
		 ++Days)
	{
		// What was standing where, before the day, and which were Quest
		// dungeons.
		TMap<int32, TArray<int32>> StandingOn;
		TSet<int32> QuestDungeons;
		for (const FCataclysmDungeon& Dungeon : Run->Dungeons)
		{
			StandingOn.FindOrAdd(Dungeon.CityId).Add(Dungeon.DungeonId);

			if (Dungeon.Type == ECataclysmDungeonType::Quest)
			{
				QuestDungeons.Add(Dungeon.DungeonId);
			}
		}

		const int32 SurgesBefore = Run->Surges->SurgesFired;

		// WHETHER A CITY FALLING TODAY WOULD FIRE A SURGE, ASKED BEFORE THE DAY.
		// `AdvanceDay` reports the clock's day plus one, and `CityFell` asks the
		// scheduler about the day on the report, so that is the day asked about
		// here. Asked before and not after, because the day's own surges move
		// what the interval is measured from.
		const int32 DayAboutToBegin = Run->Day() + 1;
		const bool bAFallsSurgeIsDue =
			Run->Surges->IsDueOnCityFall(DayAboutToBegin);

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

			// AND EVERY ORDINARY DUNGEON THAT WAS ON IT IS GONE, REPLACED BY THE
			// ONE THE CITY BECAME. Until issue #1324 this asserted that nothing
			// at all was left standing, which was true while a fall only
			// removed dungeons. A fallen city is now a Dungeon City standing on
			// itself, so the count is one rather than zero -- and it is checked
			// to BE that dungeon rather than merely counted, because a count of
			// one would also pass if an absorbed dungeon had been left behind.
			//
			// **AND A QUEST DUNGEON WITH NOWHERE TO GO MAY STAND THERE TOO**,
			// since 2026-10-21: a fall moves a Quest dungeon to the nearest city
			// that will take it, and one that finds none stays on the ruin.
			// Until then this asserted that exactly one dungeon stood there. It
			// now asserts exactly one Dungeon City and nothing else but Quest
			// dungeons.
			const TArray<int32> Standing = Run->DungeonsOn(CityId);

			int32 DungeonCitiesHere = 0;
			int32 NeitherHere = 0;

			for (const int32 StandingId : Standing)
			{
				const FCataclysmDungeon* Left = Run->FindDungeon(StandingId);
				if (!TestNotNull(TEXT("and it is on the map"), Left))
				{
					continue;
				}

				if (Left->Type == ECataclysmDungeonType::FallenCity)
				{
					++DungeonCitiesHere;
				}
				else if (Left->Type != ECataclysmDungeonType::Quest)
				{
					++NeitherHere;
				}
			}

			TestEqual(FString::Printf(
				TEXT("%s became one Fallen City standing on it"), *City->Name),
				DungeonCitiesHere, 1);
			TestEqual(FString::Printf(
				TEXT("and nothing but a Quest dungeon with nowhere to go stands on %s beside it"),
				*City->Name),
				NeitherHere, 0);

			if (const TArray<int32>* WasStanding = StandingOn.Find(CityId))
			{
				for (const int32 DungeonId : *WasStanding)
				{
					// A QUEST DUNGEON IS NOT ABSORBED, since 2026-10-21. It was
					// moved -- by this fall, or by its own timer earlier in the
					// day -- and every move is in `Relocated`; or it stayed on
					// the fallen city because nothing would take it. Either way
					// it is still on the map and on the clock.
					if (QuestDungeons.Contains(DungeonId))
					{
						TestFalse(FString::Printf(
							TEXT("quest dungeon %d was not recorded as absorbed"),
							DungeonId),
							Report.Absorbed.Contains(DungeonId));

						const FCataclysmDungeon* Quest = Run->FindDungeon(DungeonId);
						if (TestNotNull(FString::Printf(
								TEXT("and quest dungeon %d is still on the map"),
								DungeonId), Quest))
						{
							TestTrue(FString::Printf(
								TEXT("and quest dungeon %d either stayed on %s or is in the day's moves"),
								DungeonId, *City->Name),
								Quest->CityId == CityId
									|| Report.Relocated.Contains(DungeonId));
						}

						TestNotEqual(FString::Printf(
							TEXT("and quest dungeon %d is still on the clock"),
							DungeonId),
							Run->Clock->DaysUntilResolveFor(DungeonId), -1.0f);

						continue;
					}

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

		// A CITY FALLING FIRES A SURGE OF ITS OWN ONLY WHEN ONE IS DUE: the last
		// surge has to be at least 25 days behind. Issue #2380. Until then this
		// comment said every fall fired one, one per city lost, and the two
		// assertions below ran on every fall day.
		//
		// ASSERTED ONLY WHEN THE SCHEDULER SAID YES BEFORE THE DAY. A labelled
		// judgement by the coordinating session under the owner's delegation,
		// 2026-10-13. A yes before the day stays a yes for the day's first fall
		// unless a surge fires earlier in the same day, and the only one asked
		// earlier is the clock's, at the top of `AdvanceDay`. In both cases the
		// day surged and the count rose by at least one.
		//
		// WHEN THE ANSWER WAS NO AND THE DAY IS AFTER DAY 1, THE DAY DID NOT
		// SURGE AT ALL. `bSurged` is one flag for every cause, so this rests on
		// no other cause being able to fire then: every surge moves the clock's
		// next day on by `GapAfterThisSurge`, which is never below
		// `LeastIntervalDays`, and the empty-board surge asks for the same
		// interval, so inside the interval neither is due. Day 1 is the one
		// exception, where the opening wave fires with no surge behind it, and
		// nothing is asserted there. Ruled by the coordinating session on
		// 2026-10-13, after this test's first version asserted nothing on a no.
		// The refused case is also held, on a fall made by hand, by
		// `Cataclysm.EmpireRun.ACityFallingInsideTheLeastIntervalStillFallsAndBringsNoWave`.
		//
		// THE COUNT ASSERTS ONE AND NOT ONE PER CITY. Two cities can fall on one
		// day. The first fall's surge is recorded as today's, so the second fall
		// is nought days behind a surge and fires none. The question before the
		// day is therefore about the day's first fall only, and one surge is all
		// a day with any number of falls is certain to bring.
		// `Cataclysm.EmpireRun.TwoCitiesFallingOnOneDayBringOneWave` holds that.
		if (bAFallsSurgeIsDue)
		{
			TestTrue(TEXT("the day a city fell with a fall's surge due also surged"),
					 Report.bSurged);
			TestTrue(FString::Printf(
				TEXT("at least one surge fired on the day %d cities fell with a fall's surge due"),
				Report.Fallen.Num()),
				Run->Surges->SurgesFired >= SurgesBefore + 1);

			if (Report.bSurged)
			{
				++FallDaysDueThatSurged;
			}
		}
		else if (DayAboutToBegin > 1)
		{
			TestFalse(FString::Printf(
				TEXT("day %d, a day a city fell with no fall's surge due, did not surge"),
				DayAboutToBegin),
				Report.bSurged);
		}
	}

	TestTrue(FString::Printf(
		TEXT("%d cities fell within six hundred unattended days"), FallsSeen),
		FallsSeen >= 1);

	// AND AT LEAST ONE OF THE DAYS WATCHED HAD A SURGE DUE AND SURGED. The loop
	// above keeps watching past the third fall until one has, so this fails
	// only when six hundred days bring no such fall at all. Without it, a seed
	// whose fall days were all inside the least interval would pass with the
	// surge never looked at.
	TestTrue(FString::Printf(
		TEXT("%d of the %d days a city fell had a fall's surge due and surged"),
		FallDaysDueThatSurged, FallsSeen),
		FallDaysDueThatSurged >= 1);

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

	// THE OPENING WAVE OF 12, since 2026-10-20; 5 until then.
	if (!TestEqual(TEXT("twelve dungeons are standing"), Run->DungeonCount(), 12))
	{
		return false;
	}

	const int32 DungeonId = First.Spawned[0];
	const FCataclysmDungeon* Dungeon = Run->FindDungeon(DungeonId);
	if (!TestNotNull(TEXT("the first one is on the map"), Dungeon))
	{
		return false;
	}

	// NOTHING IS COUNTED BEFORE ANYTHING IS BEATEN. Issue #1324 slice 5. A
	// counter that started at something would make every figure below wrong by
	// a constant and look right.
	TestEqual(TEXT("no dungeons are cleared yet"), Run->DungeonsCleared, 0);
	TestEqual(TEXT("nor ordinary ones"), Run->BasicDungeonsCleared, 0);
	TestEqual(TEXT("nor quest objectives"), Run->QuestObjectives, 0);

	const ECataclysmDungeonType Kind = Dungeon->Type;

	TestTrue(TEXT("it is cleared"), Run->ClearDungeon(DungeonId));

	// BOTH LISTS, WHICH IS THE WHOLE REASON THIS IS A METHOD RATHER THAN A
	// CALLER REACHING INTO TWO ARRAYS. A dungeon left on the clock would keep
	// biting a city that no longer has a dungeon on it.
	TestNull(TEXT("it is off the map"), Run->FindDungeon(DungeonId));
	TestEqual(TEXT("and off the clock"),
			  Run->Clock->DaysUntilResolveFor(DungeonId), -1.0f);
	TestEqual(TEXT("eleven are left"), Run->DungeonCount(), 11);
	TestEqual(TEXT("and the clock is counting eleven down"),
			  Run->Clock->Timers.Num(), 11);

	// AND THE RUN COUNTED IT. Which of the two other counters moved depends on
	// what a surge rolled, so the kind is read off the dungeon rather than
	// assumed: this test's seed landed a Basic dungeon first at waves of 5,
	// and a change to the roll must not turn that into a silent failure of
	// the wrong assertion. The opening wave of 12, since 2026-10-20, is such
	// a change: its seven extra target draws come before the first dungeon's
	// kind is drawn, and which kind that now is was not worked out here.
	TestEqual(TEXT("the run counted one dungeon cleared"),
			  Run->DungeonsCleared, 1);
	TestEqual(TEXT("and counted it as ordinary exactly when it was"),
			  Run->BasicDungeonsCleared,
			  Kind == ECataclysmDungeonType::Basic ? 1 : 0);
	TestEqual(TEXT("and as a quest objective exactly when it was"),
			  Run->QuestObjectives,
			  Kind == ECataclysmDungeonType::Quest ? 1 : 0);

	TestFalse(TEXT("clearing it twice does nothing"), Run->ClearDungeon(DungeonId));
	TestFalse(TEXT("nor does clearing one that never existed"),
			  Run->ClearDungeon(9999));

	// AND NEITHER OF THOSE IS COUNTED. A refused clear that still raised a
	// tally would let a caller earn quest objectives by asking twice.
	TestEqual(TEXT("a refused clear counts nothing"), Run->DungeonsCleared, 1);
	TestEqual(TEXT("nor as an ordinary one"), Run->BasicDungeonsCleared,
			  Kind == ECataclysmDungeonType::Basic ? 1 : 0);
	TestEqual(TEXT("nor as an objective"), Run->QuestObjectives,
			  Kind == ECataclysmDungeonType::Quest ? 1 : 0);

	// AND IT NEVER RESOLVES AGAIN. Two hundred days is long enough for its timer
	// to have run out several times over.
	//
	// MEASURED AS "THAT DUNGEON NEVER RESOLVES" RATHER THAN "THAT CITY LOSES NO
	// DEFENCE", because the surge on day 91 puts more dungeons on the map and
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

	// A SEED WHOSE FIRST WAVE CONTAINS A SIEGE, found by looking. Most seeds
	// have one, but not all: the first wave is 12 dungeons since 2026-10-20,
	// and this comment said "four" and "10 in 100" from earlier still.
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
	// per day, plus 2.5 points for every day the Siege has already stood. The
	// day measured is the day after the wave landed, so exactly one day's growth
	// is in it. Written out from the constants rather than as 0.01 and 2.5, so a
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
	// defence takes 70 days of a growing Siege to empty, which leaves room to
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

	// TWO AND A HALF, WRITTEN OUT, BECAUSE THE DESIGN DOCUMENT SAYS 2.5. Every
	// other assertion in this test compares against
	// `UCataclysmEmpireRun::SiegeDamageGrowthPerDay`, and a test whose expected
	// value is read out of the same constant the code reads cannot notice that
	// the constant is wrong: setting it to zero would make this test expect no
	// growth, find no growth, and pass.
	//
	// FOUND BY BREAKING IT. A `prove_cpp_guard` run on 2026-09-05 set that
	// constant to zero and all fifteen tests in this file still passed. This
	// line and the strict comparison below are what that run was missing.
	//
	// IT WAS 10 UNTIL 2026-09-06, when the owner cut the growth on issue #1349.
	// This literal is the one that has to be edited by hand when it moves, and
	// that is the whole point of it being a literal.
	TestEqual(TEXT("the design's two and a half points a day"),
			  UCataclysmEmpireRun::SiegeDamageGrowthPerDay, 2.5f, 0.0001f);

	// EACH DAY TAKES EXACTLY THAT MUCH MORE THAN THE ONE BEFORE IT. Checked as
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
					  TEXT("day %d took one growth step more defence than day "
						   "%d (%.1f against %.1f)"),
					  Step + 1, Step, Taken[Step], Taken[Step - 1]),
				  Taken[Step] - Taken[Step - 1],
				  UCataclysmEmpireRun::SiegeDamageGrowthPerDay, 0.01f);
	}

	// AND THE FIRST OF THEM IS THE FLAT SHARE PLUS ONE DAY'S GROWTH, which is
	// what pins where the counting starts. A Siege that grew from the day before
	// it arrived would take one growth step more on every one of these days and
	// the differences above would not notice.
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
		//
		// **QUEST DUNGEONS ARE NOT COUNTED**, since 2026-10-21. A fall moves a
		// Quest dungeon rather than absorbing it, and the Dungeon City counts
		// only what it absorbed. Until then every dungeon standing was counted.
		TMap<int32, int32> StandingOn;
		for (const FCataclysmDungeon& Dungeon : Run->Dungeons)
		{
			if (Dungeon.Type != ECataclysmDungeonType::Quest)
			{
				++StandingOn.FindOrAdd(Dungeon.CityId);
			}
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

			// ONE DUNGEON CITY STANDS ON IT, AND NOTHING ELSE BUT A QUEST
			// DUNGEON THAT FOUND NOWHERE TO GO. Until 2026-10-21 this asserted
			// that exactly one dungeon stood there; a fall now leaves a Quest
			// dungeon on the ruin when no city anywhere will take it.
			const TArray<int32> Standing = Run->DungeonsOn(CityId);

			const FCataclysmDungeon* Left = nullptr;
			int32 DungeonCitiesHere = 0;
			int32 NeitherHere = 0;

			for (const int32 StandingId : Standing)
			{
				const FCataclysmDungeon* There = Run->FindDungeon(StandingId);
				if (There == nullptr)
				{
					continue;
				}

				if (There->Type == ECataclysmDungeonType::FallenCity)
				{
					Left = There;
					++DungeonCitiesHere;
				}
				else if (There->Type != ECataclysmDungeonType::Quest)
				{
					++NeitherHere;
				}
			}

			if (!TestEqual(TEXT("one Dungeon City stands on it"), DungeonCitiesHere, 1)
				|| !TestEqual(TEXT("and nothing but a Quest dungeon with nowhere to go stands beside it"),
							  NeitherHere, 0)
				|| !TestNotNull(TEXT("and it is on the map"), Left))
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

			// ONE BOSS PER DUNGEON THAT WAS STANDING AND WAS ABSORBED. The
			// design's one stated exception to a boss on the final floor and
			// nowhere else. AT LEAST ONE, since 2026-10-21: a city whose own
			// Quest dungeon's hit felled it with nothing else standing absorbs
			// none and still holds one boss, ruled by the coordinating session
			// that day. Until then this read `WasCarrying` alone, which was
			// never nought while every dungeon on the city was absorbed.
			TestEqual(TEXT("and one boss per dungeon that was standing and was absorbed, and at least one"),
					  Left->Bosses, FMath::Max(1, WasCarrying));

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

// ---------------------------------------------------------------------------
// Quest dungeons -- issue #1324 slice 3
// ---------------------------------------------------------------------------

/**
 * A Quest dungeon's timer runs out, its host city takes an ordinary dungeon's
 * hit for that city's tier, and the timer is full again.
 *
 * UNTIL 2026-10-13 THIS TEST HELD THE OPPOSITE, under the name
 * `AQuestDungeonRefreshesInsteadOfBitingItsCity`: the city paid nothing. The
 * owner, 2026-10-08, verbatim: "Quest dungeons will do their city/population
 * damage, and either refresh their timer and stay on that city, or potentially
 * move to an adjacent city." `docs/DECISIONS.md`, 2026-10-13.
 *
 * WHAT THE CITY PAYS IS READ FROM THE TABLE AND NOT TYPED: the ordinary row of
 * `SpecFor` for the tier of the city the dungeon stood on that morning.
 *
 * THE PARAGRAPH AT THE END, ABOUT TWO BREAKS, DESCRIBES THE OLD RULE. With the
 * hit built it no longer describes what this test guards. It is kept because
 * it records a proof that was run.
 *
 * IT DRIVES REAL CAMPAIGNS RATHER THAN BUILDING A DUNGEON BY HAND, because the
 * thing that could go wrong is `UCataclysmEmpireRun::ResolveDungeon` taking the
 * wrong amount, and that is reached only through the day loop. A
 * hand-built dungeon inserted into the run's arrays would also have to be
 * inserted into the clock's, which is the corruption `DungeonsAgreeWithTimers`
 * exists to catch.
 *
 * THE CITY IS WATCHED ACROSS THE ONE DAY THE TIMER FIRES, not from the start of
 * the run. Other dungeons stand on the same city and bite it on their own
 * schedules, and a Siege bites it every day, so comparing the city's defence
 * against what it was fifty days ago would prove nothing. What is measured is
 * the day a Quest dungeon appears in `Report.Resolved` while nothing else that
 * could have hurt that city did.
 *
 * **WHAT THIS GUARDS IS AN OUTCOME WITH A SPARE, AND PROVING IT FIRES TOOK TWO
 * BREAKS RATHER THAN ONE.** Two independent things make a Quest dungeon
 * harmless today: `FCataclysmDungeon::Resolves` answers false for one, and every
 * Quest row of `SpecFor` carries zero damage. Break either alone and the other
 * still holds the line, so a single-edit `prove_cpp_guard` run reports this test
 * as noticing nothing -- which would read as a worthless guard when it is
 * actually a guarded outcome with a redundancy. Removing both together --
 * `Resolves` made to answer true for a Quest, and `MakeDungeon` made to read the
 * Basic row of the spec -- is what makes the claim false, and this test is the
 * only one of the nineteen under `Cataclysm.EmpireRun` that fails when it is.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunQuestRefreshTest,
	"Cataclysm.EmpireRun.AQuestDungeonHitsItsCityAndThenRefreshes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunQuestRefreshTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	constexpr int32 Campaigns = 20;
	constexpr int32 MostDays = 600;

	int32 QuestsSeen = 0;
	int32 TimersChecked = 0;
	int32 CleanDaysChecked = 0;
	int32 MovedByAFall = 0;

	for (int32 Seed = 1; Seed <= Campaigns; ++Seed)
	{
		UCataclysmEmpireRun* Run = MakeRun(Seed);

		if (!TestNotNull(TEXT("the run has a map"), Run->Map.Get()))
		{
			return false;
		}

		// EVERY QUEST DUNGEON THAT ARRIVED, counted once by identifier, and
		// identifiers are handed out in order and never reused. A census of the
		// board at the end would miss the ones the player clears, and until
		// 2026-10-21 the ones a city's fall absorbed.
		TSet<int32> QuestsHere;

		auto CountQuests = [&QuestsHere](const UCataclysmEmpireRun& Live)
		{
			for (const FCataclysmDungeon& Dungeon : Live.Dungeons)
			{
				if (Dungeon.Type == ECataclysmDungeonType::Quest)
				{
					QuestsHere.Add(Dungeon.DungeonId);
				}
			}
		};

		// THE FIRST WAVE FIRES AT RUN START, before any day has passed.
		CountQuests(*Run);

		int32 Advanced = 0;
		while (Advanced < MostDays && Run->Map->ExposedCities().Num() >= 2)
		{
			// READ BEFORE THE DAY. A city that falls today is gone from the
			// comparison afterwards, and a dungeon absorbed with it is gone
			// from `Dungeons`.
			TMap<int32, float> DefenceWas;
			TMap<int32, float> PopulationWas;
			TSet<int32> FallenWas;
			for (const FCataclysmCity& City : Run->Map->Cities)
			{
				DefenceWas.Add(City.CityId, City.Defence);
				PopulationWas.Add(City.CityId, City.Population);
				if (City.bFallen)
				{
					FallenWas.Add(City.CityId);
				}
			}

			// WHICH CITIES COULD BE HURT BY SOMETHING THAT IS NOT A QUEST. Every
			// city carrying a dungeon that detonates, or a Siege, is excluded
			// below; what is left is a city whose only assailant today was the
			// Quest dungeon whose timer fired.
			TMap<int32, ECataclysmDungeonType> KindOf;
			TMap<int32, int32> CityOf;
			TSet<int32> CitiesWithABiter;
			for (const FCataclysmDungeon& Dungeon : Run->Dungeons)
			{
				KindOf.Add(Dungeon.DungeonId, Dungeon.Type);
				CityOf.Add(Dungeon.DungeonId, Dungeon.CityId);

				if (Dungeon.Resolves()
					|| Dungeon.SubType == ECataclysmDungeonSubType::Siege)
				{
					CitiesWithABiter.Add(Dungeon.CityId);
				}
			}

			const FCataclysmDayReport Report = Run->AdvanceDay();
			++Advanced;

			for (const int32 DungeonId : Report.Resolved)
			{
				if (KindOf.FindRef(DungeonId)
					!= ECataclysmDungeonType::Quest)
				{
					continue;
				}

				++TimersChecked;

				// ITS TIMER IS FULL AGAIN. The clock refreshes every timer that
				// runs out, and this is the half of "refreshes" the design names
				// -- so a Quest dungeon comes due again rather than going quiet.
				const FCataclysmDungeon* Still = Run->FindDungeon(DungeonId);

				if (Still == nullptr)
				{
					// A FALL NO LONGER TAKES A QUEST DUNGEON, since 2026-10-21:
					// it moves to the nearest city that will take it, or stays
					// on the ruin, so nothing in a day takes one off the map.
					// Until then this branch counted the ones whose city fell
					// underneath them -- measured at 5 of 832 timers over twenty
					// campaigns -- and asserted they were in `Absorbed`.
					AddError(FString::Printf(
						TEXT("quest dungeon %d whose timer ran out is gone from "
							 "the map. A fall moves a Quest dungeon rather than "
							 "absorbing it"), DungeonId));

					continue;
				}

				// MOVED BY A FALL TODAY, which is what the branch above used to
				// count as absorbed. Not absorbed, and in `Relocated`.
				const bool bMovedByAFall = Report.MovedByAFall.Contains(DungeonId);
				if (bMovedByAFall)
				{
					++MovedByAFall;

					TestFalse(FString::Printf(
						TEXT("quest dungeon %d a fall moved is not named as "
							 "absorbed"), DungeonId),
						Report.Absorbed.Contains(DungeonId));
					TestTrue(FString::Printf(
						TEXT("and quest dungeon %d a fall moved is in the day's "
							 "moves"), DungeonId),
						Report.Relocated.Contains(DungeonId));
				}

				const FCataclysmDungeonTimer* Timer =
					Run->Clock->FindTimer(DungeonId);

				if (TestNotNull(TEXT("and it still has a timer"), Timer))
				{
					TestEqual(FString::Printf(
						TEXT("quest dungeon %d refreshed to its full %.1f "
							 "days"), DungeonId, Timer->ResolveDays),
						Timer->DaysUntilResolve, Timer->ResolveDays, 0.001f);
				}

				// AND THE REPORT AGREES ABOUT WHETHER IT MOVED. Slice 4 of
				// issue #1324 made it move, so this used to assert it had NOT
				// -- with a note saying it should fail when the move landed,
				// which it did. What replaces it is the weaker claim this test
				// is entitled to make: `Relocated` and the dungeon's own city
				// say the same thing. Where it may move to is
				// `Cataclysm.EmpireRun.AQuestDungeonMovesToAnAdjacentCity`.
				//
				// NOT ASKED OF ONE A FALL MOVED, since 2026-10-21. A dungeon
				// that moved by its timer onto a city that then fell can be
				// moved back by the fall to the city it stood on this morning,
				// which the report rightly lists as two moves.
				const bool bMoved =
					Still->CityId != CityOf.FindRef(DungeonId);

				if (!bMovedByAFall)
				{
					TestEqual(FString::Printf(
						TEXT("the report says quest dungeon %d moved exactly when "
							 "it did"), DungeonId),
						Report.Relocated.Contains(DungeonId), bMoved);
				}

				// THE CITY IT WAS ON THIS MORNING, AND NOT THE ONE IT IS ON
				// NOW. What this test measures is that its host paid nothing
				// for the timer running out, and after a move the host is the
				// city it left. Reading the new one would compare a city the
				// dungeon only just arrived at against a snapshot taken while
				// it was somewhere else, which happens to be a fair comparison
				// and is not the claim being made.
				const int32 CityId = CityOf.FindRef(DungeonId);

				if (CitiesWithABiter.Contains(CityId)
					|| Report.Fallen.Contains(CityId)
					|| Report.Besieged.Contains(CityId)
					|| FallenWas.Contains(CityId)
					|| Report.MovedByAFallTo.Contains(CityId))
				{
					// SOMETHING ELSE COULD HAVE HURT THIS CITY TODAY, so it
					// says nothing about the quest dungeon. Skipped rather than
					// asserted loosely. `Besieged` covers a Siege that landed
					// today, which the morning's list cannot. A city that fell
					// today is skipped because its defence stops at the fall,
					// and one that had fallen before today is hit by nothing.
					//
					// AND A CITY A FALL MOVED A QUEST DUNGEON ONTO TODAY, since
					// 2026-10-21. That dungeon's timer may run out later the
					// same day and hit it, and it was not on the city this
					// morning, so the count below would not expect its hit.
					continue;
				}

				const FCataclysmCity* City = Run->Map->Find(CityId);
				if (City == nullptr)
				{
					continue;
				}

				++CleanDaysChecked;

				// HOW MANY QUEST TIMERS RAN OUT ON THIS CITY TODAY, counted
				// from the morning's positions and not from the report's own
				// `QuestHits`, so a hit the code failed to make is not also
				// missing from what is expected. Two on one city is two hits.
				int32 QuestTimersHere = 0;
				for (const int32 OtherId : Report.Resolved)
				{
					if (KindOf.FindRef(OtherId) == ECataclysmDungeonType::Quest
						&& CityOf.FindRef(OtherId) == CityId)
					{
						++QuestTimersHere;
					}
				}

				// THE ORDINARY ROW OF THE HOST'S TIER, READ FROM THE TABLE.
				// Population stops at nought, so a city with fewer people
				// left than the row takes loses what it had.
				const FCataclysmDungeonSpec OrdinaryRow =
					UCataclysmSurgeScheduler::SpecFor(
						ECataclysmDungeonType::Basic, City->Tier);
				const float DefenceExpected =
					OrdinaryRow.DefenceDamage * QuestTimersHere;
				const float PopulationExpected = FMath::Min(
					PopulationWas.FindRef(CityId),
					OrdinaryRow.PopulationDamage * QuestTimersHere);

				TestEqual(FString::Printf(
					TEXT("%s lost the defence an ordinary dungeon's hit takes "
						 "from a city of its tier, for each quest timer that "
						 "ran out on it"), *City->Name),
					DefenceWas.FindRef(CityId) - City->Defence,
					DefenceExpected, 0.01f);
				TestEqual(FString::Printf(
					TEXT("%s lost that hit's population as well"), *City->Name),
					PopulationWas.FindRef(CityId) - City->Population,
					PopulationExpected, 0.01f);
			}

			CountQuests(*Run);
		}

		QuestsSeen += QuestsHere.Num();
	}

	AddInfo(FString::Printf(
		TEXT("%d campaigns: %d quest dungeons arrived, %d of their timers ran "
			 "out, %d of those on a day nothing else could have hurt the city, "
			 "and %d were moved by the city falling underneath them"),
		Campaigns, QuestsSeen, TimersChecked, CleanDaysChecked,
		MovedByAFall));

	// THE EVIDENCE HAS TO EXIST BEFORE IT CAN BE BELIEVED. A run that never
	// landed a quest dungeon, or never let one of their timers run out, would
	// pass every assertion above by never reaching one.
	TestTrue(TEXT("quest dungeons arrived at all"), QuestsSeen > 0);
	TestTrue(TEXT("and their timers actually ran out"), TimersChecked > 0);
	TestTrue(TEXT("and some did so on a day nothing else hurt the city"),
			 CleanDaysChecked > 0);

	// SET-UP FOR THE TWO AMOUNTS ABOVE: the ordinary row of an Outpost takes
	// something. A row of nought would make "lost the row" true of a city that
	// lost nothing.
	const FCataclysmDungeonSpec OutpostRow = UCataclysmSurgeScheduler::SpecFor(
		ECataclysmDungeonType::Basic, ECataclysmCityTier::Outpost);
	TestTrue(TEXT("set-up: the ordinary row of an Outpost takes defence and population"),
			 OutpostRow.DefenceDamage > 0.0f && OutpostRow.PopulationDamage > 0.0f);

	return true;
}

// ---------------------------------------------------------------------------
// Quest dungeons relocating -- issue #1324 slice 4
// ---------------------------------------------------------------------------

/**
 * A Quest dungeon whose timer runs out picks up and moves to an adjacent city.
 *
 * `docs/Cataclysm_GDD_v2.md` section VIII: a Quest dungeon "does not resolve --
 * refreshes and **may move to adjacent city**". The project owner ruled on
 * 2026-09-06, verbatim "Adjacent, and fix the simulation", so adjacency is the
 * rule and the simulation moving it anywhere on the map was the defect.
 *
 * WHY THIS DRIVES CAMPAIGNS RATHER THAN CALLING `PickRelocation`. That function
 * is checked directly by
 * `Cataclysm.Surge.AQuestDungeonRelocatesOnlyToAnAdjacentExposedCity`, over
 * every city of three map states. What that test cannot see is whether anything
 * CALLS it. `UCataclysmEmpireMap::Retake` was correct, complete and unreachable
 * for a whole slice, and only a test that ran the day loop would have found it.
 *
 * WHAT IS MEASURED, PER MOVE. Where the dungeon stood at the start of the day
 * is recorded before `AdvanceDay`, because a move is only visible against the
 * position it moved from; the day's `Relocated` list is compared against what
 * actually changed; and the destination is checked against the adjacency of the
 * city it left, read off the map rather than recomputed here.
 *
 * **A DUNGEON THAT STAYED IS NOT A FAILURE AND THERE ARE NOW TWO REASONS FOR
 * IT.** It was hemmed in -- every neighbour sealed, fallen, the Pillar, or
 * already besieged if it carries a Siege itself -- or it had somewhere to go and
 * the coin said no. The project owner ruled the coin on 2026-09-06, verbatim "A
 * chance each time", and then chose the number, verbatim "0.5";
 * `UCataclysmSurgeScheduler::QuestMoveChance` carries it. Issue #1324.
 *
 * **ALL THREE OUTCOMES ARE ASSERTED TO HAVE HAPPENED**, and the take-up rate is
 * checked against the constant over the timers that had a choice. A run that
 * only ever moved them would never exercise the empty case; a run that never
 * moved one would prove nothing at all; and a run where every stay was a hemmed
 * in dungeon would pass unchanged against a build with no chance in it, which is
 * the state this test was in until 2026-09-06.
 *
 * **AND WHAT IT CARRIES WITH IT IS CHECKED, NOT ONLY WHERE IT GOES.** The owner
 * ruled in the same breath, verbatim "Keeps everything, fix the size": the floor
 * count, the resolve timer and the sub-type survive a move, and `CityTier` --
 * which is the tier the DEPTH was rolled from and not the host's tier -- does
 * not move either. That last one is a fix; this test asserted its opposite until
 * 2026-09-06. `CrossedATierBoundary` is the control that keeps it meaningful.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunQuestMovesTest,
	"Cataclysm.EmpireRun.AQuestDungeonMovesToAnAdjacentCity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunQuestMovesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	// FORTY CAMPAIGNS, AND IT WAS TWENTY UNTIL 2026-10-19. Since then a Quest
	// dungeon's timer comes from its depth, as an ordinary dungeon's does,
	// which on an Outpost is about twice the old flat 25 days -- so about half
	// as many quest timers run out in a campaign, and the rate below needs more
	// than 200 of them that had a choice. The campaigns were doubled rather
	// than the 200 lowered, so the four-sigma band keeps the width it had;
	// ruled by the coordinating session, 2026-10-19.
	constexpr int32 Campaigns = 40;
	constexpr int32 MostDays = 600;

	int32 TimersFired = 0;
	int32 Moves = 0;
	int32 StayedPut = 0;
	int32 AlongTheRim = 0;

	// QUEST DUNGEONS WHOSE TIMER RAN OUT ON A DAY A FALL ALSO MOVED THEM, since
	// 2026-10-21. A fall's move is not the timer's -- it goes as far as it
	// must, and may be followed by the timer's own move the same day -- so
	// these are counted apart and left out of every check below, which are
	// about the timer's move. Until then a fall absorbed them, and two
	// counters here held the ones absorbed and the ones absorbed after moving.
	int32 MovedByAFall = 0;

	// **THE TWO REASONS A QUEST DUNGEON STAYS, COUNTED APART.** It was hemmed
	// in -- every neighbour sealed, fallen, the Pillar, or already besieged if
	// it carries a Siege -- or it had somewhere to go and the coin said no. The
	// project owner ruled the coin on 2026-09-06, verbatim "A chance each time",
	// and chose 0.5. Before that ruling the second of these was IMPOSSIBLE and
	// this test raised an error for it; both are now asserted to happen, because
	// a sample showing only one leaves half the rule untested.
	int32 StayedHemmedIn = 0;
	int32 Declined = 0;

	// MOVES THAT LANDED ON A CITY OF A DIFFERENT TIER. The control on the
	// "kept the tier its depth was rolled from" assertion: between two cities
	// of one tier that assertion holds under either rule.
	int32 CrossedATierBoundary = 0;

	// HOW OFTEN THE STAYED-PUT CHECK HAD TO EXEMPT A NEIGHBOUR because the
	// day's own falls opened it after the decision was taken. Counted so the
	// exemption cannot quietly grow until the check means nothing; it is
	// reported below beside the rest.
	int32 OpenedByTheDaysOwnFalls = 0;

	for (int32 Seed = 1; Seed <= Campaigns; ++Seed)
	{
		UCataclysmEmpireRun* Run = MakeRun(Seed);

		if (!TestNotNull(TEXT("the run has a map"), Run->Map.Get()))
		{
			return false;
		}

		int32 Advanced = 0;
		while (Advanced < MostDays && Run->Map->ExposedCities().Num() >= 2)
		{
			// WHERE EVERY QUEST DUNGEON STOOD THIS MORNING. Read before the
			// day, because the whole measurement is a comparison against it.
			//
			// AND WHAT IT LOOKED LIKE, kept whole rather than field by field.
			// The project owner ruled on 2026-09-06, verbatim "Keeps
			// everything, fix the size", so the claim being checked is about
			// the dungeon and not about one property of it; a copy taken here
			// lets a later field be added to the comparison without another
			// map. See `WhatMovedThatShouldNotHave` below.
			TMap<int32, int32> StoodOn;
			TMap<int32, FCataclysmDungeon> WasLike;
			for (const FCataclysmDungeon& Dungeon : Run->Dungeons)
			{
				if (Dungeon.Type == ECataclysmDungeonType::Quest)
				{
					StoodOn.Add(Dungeon.DungeonId, Dungeon.CityId);
					WasLike.Add(Dungeon.DungeonId, Dungeon);
				}
			}

			const FCataclysmDayReport Report = Run->AdvanceDay();
			++Advanced;

			// NOTHING BUT A QUEST DUNGEON MAY BE IN `Relocated`, and this is
			// the assertion that would catch a Fallen City drifting off the
			// ruin it is standing on.
			for (const int32 DungeonId : Report.Relocated)
			{
				// A FALL'S MOVE, since 2026-10-21. It need not be on a day its
				// timer ran out, and the dungeon may have arrived today, so
				// what is asked of it is only that it is a Quest dungeon.
				if (Report.MovedByAFall.Contains(DungeonId))
				{
					const FCataclysmDungeon* Moved = Run->FindDungeon(DungeonId);
					TestTrue(FString::Printf(
						TEXT("dungeon %d a fall moved is a quest dungeon"),
						DungeonId),
						Moved != nullptr
							&& Moved->Type == ECataclysmDungeonType::Quest);
					continue;
				}

				TestTrue(FString::Printf(
					TEXT("dungeon %d that moved was a quest dungeon this "
						 "morning"), DungeonId),
					StoodOn.Contains(DungeonId));

				TestTrue(FString::Printf(
					TEXT("dungeon %d moved on a day its timer ran out"),
					DungeonId),
					Report.Resolved.Contains(DungeonId));
			}

			for (const TPair<int32, int32>& Was : StoodOn)
			{
				const int32 DungeonId = Was.Key;
				const int32 From = Was.Value;

				if (!Report.Resolved.Contains(DungeonId))
				{
					// ITS TIMER DID NOT RUN OUT, so it had no reason to move
					// and must not have. A quest dungeon that wandered on an
					// ordinary day would be a different bug from any this test
					// is named for, and it would otherwise go unseen.
					//
					// A FALL MAY STILL HAVE MOVED IT, since 2026-10-21, and that
					// move is not the timer's.
					const FCataclysmDungeon* Idle = Run->FindDungeon(DungeonId);

					if (Idle != nullptr
						&& !Report.MovedByAFall.Contains(DungeonId))
					{
						TestEqual(FString::Printf(
							TEXT("quest dungeon %d stayed put on a day its "
								 "timer did not fire"), DungeonId),
							Idle->CityId, From);
					}

					continue;
				}

				++TimersFired;

				const FCataclysmDungeon* Still = Run->FindDungeon(DungeonId);

				if (Still == nullptr)
				{
					// A FALL NO LONGER TAKES A QUEST DUNGEON, since 2026-10-21,
					// so nothing in a day takes one off the map. Until then a
					// city that fell underneath one absorbed it -- the case
					// `AQuestDungeonRefreshesInsteadOfBitingItsCity` found and
					// measured at 5 of 832 timers -- and this branch checked
					// that the city that took it was the one it stood on or one
					// adjacent to it.
					AddError(FString::Printf(
						TEXT("quest dungeon %d whose timer ran out is gone from "
							 "the map. A fall moves a Quest dungeon rather than "
							 "absorbing it"), DungeonId));

					continue;
				}

				// A FALL MOVED IT TODAY, before or after its timer ran out.
				// That move goes as far as it must and is not the timer's, and
				// one a fall moved before its timer ran out may also have taken
				// the timer's own move -- the coordinating session ruled on
				// 2026-10-21 that both stand. So it is counted and left out of
				// the timer's checks below, which compare against where it
				// stood this morning.
				if (Report.MovedByAFall.Contains(DungeonId))
				{
					++MovedByAFall;
					continue;
				}

				if (Still->CityId == From)
				{
					++StayedPut;

					TestFalse(FString::Printf(
						TEXT("quest dungeon %d stayed and is not in the day's "
							 "relocated list"), DungeonId),
						Report.Relocated.Contains(DungeonId));

					// AND WHY IT STAYED, WHICH IS NOW TWO ANSWERS RATHER THAN
					// ONE. Every neighbour sealed, fallen, the Pillar or --
					// for a dungeon carrying a Siege -- already besieged; or a
					// legal neighbour and a coin that said no. **THIS BLOCK
					// USED TO RAISE AN ERROR FOR THE SECOND ONE**, because
					// before the owner's ruling of 2026-09-06, verbatim "A
					// chance each time", it could not happen. It counts them
					// apart instead, and the checks after the loop refuse to
					// pass unless BOTH occurred -- so the whole test is still
					// not satisfied by a `RelocateQuestDungeon` that never did
					// anything, which was this block's original job.
					//
					// **READ AT THE END OF THE DAY, WHILE THE DECISION WAS MADE
					// PART WAY THROUGH IT.** That is the in-day ordering the
					// absorbed branch above documented until 2026-10-21, when a
					// fall stopped taking Quest dungeons: a day resolves its
					// timers in order, so a city can fall AFTER
					// this dungeon declined to move and open a neighbour that
					// was sealed when `PickRelocation` looked at it. Reading
					// exposure afterwards then reports an open neighbour that
					// was not open at the time.
					//
					// SO THE EXEMPTION IS EXACTLY THE CITIES THE DAY'S OWN
					// FALLS COULD HAVE OPENED, AND NOTHING ELSE.
					// `UCataclysmEmpireMap::IsExposed` answers true only for a
					// rim city or one whose `Outward` shield has fallen, and
					// `Retake` -- the only thing that un-falls a city -- is
					// reached from clearing a Fallen City dungeon and never from
					// `AdvanceDay`. So within one day exposure can only be
					// turned ON, only by a fall, and only for a city one of
					// whose own shields fell. A neighbour open now whose
					// shields all still stand was open then too, and that is a
					// real relocation defect rather than an ordering artefact.
					//
					// FOUND BY THE SIEGE RETUNE OF 2026-09-06, issue #1349,
					// which moved when cities fall and so changed the order
					// inside a day. The assertion was too strong before that and
					// passed on the seed it was written against.
					const FCataclysmCity* Host = Run->Map->Find(From);
					bool bHadSomewhereToGo = false;

					if (Host != nullptr)
					{
						for (const int32 Neighbour :
							 UCataclysmSurgeScheduler::AdjacentCities(*Host))
						{
							if (Neighbour == Run->Map->PillarId
								|| !Run->Map->IsExposed(Neighbour))
							{
								continue;
							}

							// AND A BESIEGED ONE IS NOT A TARGET FOR A DUNGEON
							// CARRYING A SIEGE. The owner ruled it on
							// 2026-09-06, verbatim "Check the limit on arrival
							// too"; issue #1371. Counting such a neighbour as
							// somewhere to go would report the refusal as a
							// coin failure and hide the rule under the chance.
							//
							// READ AT THE END OF THE DAY, like everything else
							// in this block, and a Siege standing now stood
							// then: nothing in `AdvanceDay` creates one.
							if (Still->SubType
									== ECataclysmDungeonSubType::Siege
								&& Run->IsBesieged(Neighbour))
							{
								continue;
							}

							// COULD THE DAY'S OWN FALLS HAVE OPENED IT? Only a
							// fall among this neighbour's own outward shields
							// can have.
							const FCataclysmCity* Open =
								Run->Map->Find(Neighbour);
							bool bOpenedToday = false;

							if (Open != nullptr)
							{
								for (const int32 Shield : Open->Outward)
								{
									bOpenedToday = bOpenedToday
										|| Report.Fallen.Contains(Shield);
								}
							}

							if (bOpenedToday)
							{
								++OpenedByTheDaysOwnFalls;
								continue;
							}

							// IT WAS OPEN WHEN `PickRelocation` LOOKED, so the
							// dungeon had a legal destination and declined it.
							bHadSomewhereToGo = true;
						}
					}

					if (bHadSomewhereToGo)
					{
						++Declined;
					}
					else
					{
						++StayedHemmedIn;
					}

					continue;
				}

				++Moves;

				TestTrue(FString::Printf(
					TEXT("quest dungeon %d appears in the day's relocated "
						 "list"), DungeonId),
					Report.Relocated.Contains(DungeonId));

				const FCataclysmCity* Left = Run->Map->Find(From);

				if (!TestNotNull(TEXT("the city it left is on the map"), Left))
				{
					continue;
				}

				// **THE RULE.** One counter-example is a failure; this is not a
				// rate.
				TestTrue(FString::Printf(
					TEXT("quest dungeon %d moved from %s to a city adjacent to "
						 "it"), DungeonId, *Left->Name),
					UCataclysmSurgeScheduler::AdjacentCities(*Left)
						.Contains(Still->CityId));

				const FCataclysmCity* Arrived =
					Run->Map->Find(Still->CityId);

				if (Arrived != nullptr)
				{
					TestTrue(FString::Printf(
						TEXT("and %s was exposed when it arrived"),
						*Arrived->Name),
						Run->Map->IsExposed(Arrived->CityId)
							|| Arrived->bFallen);

					// **AND IT KEPT EVERYTHING ELSE.** The owner ruled on
					// 2026-09-06, verbatim "Keeps everything, fix the size":
					// floor count, resolve timer and sub-type all survive the
					// move, and `docs/Cataclysm_GDD_v2.md` section VIII now
					// states it. Checked here rather than trusted, because
					// nothing else would notice a move that quietly rerolled
					// the depth.
					const FCataclysmDungeon& Before = WasLike[DungeonId];

					TestEqual(FString::Printf(
						TEXT("quest dungeon %d kept its floor count"),
						DungeonId), Still->Floors, Before.Floors);

					TestEqual(FString::Printf(
						TEXT("quest dungeon %d kept its resolve timer"),
						DungeonId), Still->ResolveDays, Before.ResolveDays);

					TestEqual(FString::Printf(
						TEXT("quest dungeon %d kept its sub-type"), DungeonId),
						static_cast<int32>(Still->SubType),
						static_cast<int32>(Before.SubType));

					// **AND `CityTier` DID NOT MOVE WITH IT.** THIS ASSERTION
					// USED TO BE ITS OWN OPPOSITE: it read "quest dungeon %d
					// carries the tier of the city it moved to", comparing
					// `Still->CityTier` against `Arrived->Tier`, which is the
					// defect the owner's "fix the size" names.
					//
					// `CityTier` is the tier the DEPTH was rolled from -- its
					// own comment on `FCataclysmDungeon` says so -- and
					// `BiteScale` divides `Floors` by the midpoint of
					// `SpecFor(Type, CityTier)`. `Floors` does not move, so
					// moving the tier made the two halves of that division name
					// different specification rows.
					//
					// IT IS UNREACHABLE TODAY AND THAT IS WHY IT NEEDS A TEST.
					// A Quest dungeon has zero city damage and answers false to
					// `Resolves`, so nothing ever calls `BiteScale` on one. The
					// wrong number would have appeared, silently and with no
					// test failing, on the day any non-Basic kind was given
					// city damage. `TestWhatARelocatedDungeonKeeps` in
					// `sim/tests/test_quest_relocation_is_adjacent.py` is the
					// model's half of this.
					TestEqual(FString::Printf(
						TEXT("quest dungeon %d kept the tier its depth was "
							 "rolled from"), DungeonId),
						static_cast<int32>(Still->CityTier),
						static_cast<int32>(Before.CityTier));

					// SO ITS BITE SCALE IS UNCHANGED BY THE MOVE, which is the
					// consequence that matters and the only one a reader of
					// `BiteScale` would look for. Asserted separately because
					// the two fields agreeing is the mechanism and this is the
					// outcome; a future third input to the scale would break
					// this and not the assertions above.
					TestEqual(FString::Printf(
						TEXT("quest dungeon %d bites at the same scale after "
							 "moving"), DungeonId),
						Still->BiteScale(), Before.BiteScale());

					// **THE CONTROL ON ALL FIVE.** A move between two cities of
					// the SAME tier satisfies every assertion above whichever
					// rule is in force, so a run whose moves never crossed a
					// tier boundary would prove nothing at all. This counts the
					// ones that did and the check below refuses to pass on
					// zero.
					if (Arrived->Tier != Left->Tier)
					{
						++CrossedATierBoundary;
					}

					if (Left->Perimeter.Contains(Still->CityId))
					{
						++AlongTheRim;
					}
				}

				TestNotEqual(FString::Printf(
					TEXT("quest dungeon %d did not move onto the Pillar"),
					DungeonId),
					Still->CityId, Run->Map->PillarId);
			}
		}
	}

	AddInfo(FString::Printf(
		TEXT("%d campaigns: %d quest timers ran out, %d moved, %d stayed -- %d "
			 "with nowhere adjacent to go and %d declining a move they could "
			 "have taken. %d of the moves were along the rim's perimeter, "
			 "%d of the moves crossed a tier boundary, and %d were moved by "
			 "a city falling underneath them the same day and left out. %d "
			 "neighbours were open by nightfall having been sealed when the "
			 "dungeon declined to move, opened by the same day's falls"),
		Campaigns, TimersFired, Moves, StayedPut, StayedHemmedIn, Declined,
		AlongTheRim, CrossedATierBoundary, MovedByAFall,
		OpenedByTheDaysOwnFalls));

	// THE EVIDENCE HAS TO EXIST BEFORE IT CAN BE BELIEVED.
	TestTrue(TEXT("quest dungeon timers actually ran out"), TimersFired > 0);
	TestTrue(TEXT("and quest dungeons actually moved"), Moves > 0);

	// BOTH OUTCOMES, and the second is the design's "MAY move". A run in which
	// every quest dungeon always had somewhere to go would never exercise the
	// empty case, which is exactly the state the old move-anywhere rule was in:
	// some exposed city always existed.
	TestTrue(TEXT("and some had nowhere adjacent to go and stayed"),
			 StayedHemmedIn > 0);

	// **AND THE OTHER HALF OF THE "MAY", WHICH IS THE RULING.** Before
	// 2026-09-06 this was zero by construction and the loop above raised an
	// error for it. The owner ruled, verbatim "A chance each time", and chose
	// 0.5; `UCataclysmSurgeScheduler::QuestMoveChance` carries the number.
	TestTrue(TEXT("and some declined a move they could have taken, which is "
				  "the owner's \"a chance each time\""),
			 Declined > 0);

	// **THE RATE ITSELF, AND THE BAND IS COMPUTED RATHER THAN CHOSEN.** Take-up
	// is a binomial proportion over the timers that had a choice, so its
	// standard deviation is sqrt(p(1-p)/n) and four of those either side is the
	// band. Writing it this way is what stops the tolerance being widened until
	// whatever the code does passes; it still fails by tens of standard
	// deviations at the two settings that matter, because a chance of 0 gives a
	// take-up of 0% and a chance of 1 gives 100%.
	const int32 Chose = Moves + Declined;

	if (TestTrue(TEXT("enough quest timers had a choice to measure the rate"),
				 Chose > 200))
	{
		const float Wanted = UCataclysmSurgeScheduler::QuestMoveChance;
		const float Spread =
			4.0f * FMath::Sqrt(Wanted * (1.0f - Wanted) / Chose);
		const float Rate = static_cast<float>(Moves) / Chose;

		TestTrue(FString::Printf(
			TEXT("a quest dungeon with somewhere to go took it %.1f%% of the "
				 "time over %d timers, against the configured %.0f%% and a "
				 "four-sigma band of %.1f%% to %.1f%%"),
			100.0f * Rate, Chose, 100.0f * Wanted,
			100.0f * (Wanted - Spread), 100.0f * (Wanted + Spread)),
			FMath::Abs(Rate - Wanted) <= Spread);
	}

	// AND THE PERIMETER LINKS ARE ACTUALLY BEING USED. If this were zero the
	// adjacency decision recorded in docs/DECISIONS.md would be doing nothing
	// in play, and the whole argument for it would be untested.
	TestTrue(TEXT("and some moves were along the rim's perimeter links"),
			 AlongTheRim > 0);

	// **AND SOME MOVE CROSSED A TIER BOUNDARY.** Without this the assertion
	// that a relocated dungeon keeps the tier its depth was rolled from is
	// satisfied by every move being between two Outposts, and it would pass
	// unchanged against the defect it exists to catch.
	TestTrue(TEXT("and some moves crossed a tier boundary, so the tier check "
				  "is not vacuous"),
			 CrossedATierBoundary > 0);

	return true;
}

// ---------------------------------------------------------------------------
// Counting what the player achieved -- issue #1324 slice 5
// ---------------------------------------------------------------------------

/**
 * A campaign played to the end, with every clear counted twice: once by the run
 * and once here.
 *
 * WHAT THIS EXISTS TO CATCH. `UCataclysmEmpireRun` now carries three tallies of
 * what the player beat, and the win condition slice 6 builds reads one of them.
 * A tally that counted the wrong thing would be invisible: there is nothing to
 * compare it against in play, and it is a plain integer that no other rule
 * disagrees with.
 *
 * SO IT IS COUNTED INDEPENDENTLY HERE AND THE TWO ARE COMPARED EVERY DAY. This
 * test walks a campaign, clears one dungeon a day, keeps its own three numbers
 * from the kind it read off the dungeon BEFORE clearing it, and asserts the run
 * agrees at the end of every day rather than only at the end of the run -- so a
 * failure names the day and the campaign rather than a total.
 *
 * **THE HARD PART IS WHAT MUST *NOT* BE COUNTED**, and there are three:
 *
 *   - A dungeon a falling city ABSORBED. Nobody walked it. It goes through
 *     `RemoveDungeon`, which is the whole reason that function is separate from
 *     `ClearDungeon`, and this asserts the run's total never moved on a day
 *     whose only losses were absorptions.
 *   - A QUEST dungeon cleared must not deepen the Cataclysm boss dungeon.
 *     `docs/Cataclysm_GDD_v2.md` section VIII: "Quest dungeons and retaken
 *     Dungeon Cities do not" add a floor, so pursuing the win condition never
 *     makes the final fight harder.
 *   - A FALLEN CITY retaken must not either, by the same sentence, and must not
 *     be a quest objective. It is still a dungeon cleared.
 *
 * ALL THREE KINDS HAVE TO ACTUALLY HAPPEN, and the counts are reported. A run
 * that never cleared a Quest dungeon would satisfy every objective assertion
 * above by never reaching one, which is the failure mode this issue has already
 * recorded twice.
 *
 * **THE FIRST VERSION OF THIS TEST FAILED ON EXACTLY THOSE CONTROLS AND WAS
 * RIGHT TO.** It cleared one dungeon a day, which is a player nothing can hurt:
 * 20 campaigns of 600 days, 400 dungeons cleared, and **not one city fell** --
 * so no Fallen City dungeon was ever built, nothing was ever absorbed, and two
 * of the three rules this test exists for were being asserted against evidence
 * that did not exist. It now spends the days a walk costs, which is what the
 * game charges. See the walk loop below.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunCountsProgressTest,
	"Cataclysm.EmpireRun.ClearingADungeonCountsItAndOnlyTheRightKindsCountTwice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunCountsProgressTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	constexpr int32 Campaigns = 20;

	// LONGER THAN THE OTHER CAMPAIGN TESTS IN THIS FILE, because this one is
	// the only one in which the player spends days. A dungeon takes its floor
	// count in days to walk, so six hundred days buys about twenty clears a
	// campaign and the later ones -- the retaken cities -- come late.
	constexpr int32 MostDays = 900;

	int32 ClearedInAll = 0;
	int32 OrdinaryInAll = 0;
	int32 ObjectivesInAll = 0;
	int32 FallenCitiesRetaken = 0;
	int32 AbsorbedInAll = 0;
	int32 DaysWhereOnlyAnAbsorptionRemovedOne = 0;

	for (int32 Seed = 1; Seed <= Campaigns; ++Seed)
	{
		UCataclysmEmpireRun* Run = MakeRun(Seed);

		if (!TestNotNull(TEXT("the run has a map"), Run->Map.Get()))
		{
			return false;
		}

		// A FRESH RUN HAS ACHIEVED NOTHING.
		if (!TestEqual(TEXT("a fresh run has cleared nothing"),
					   Run->DungeonsCleared, 0))
		{
			return false;
		}

		int32 Cleared = 0;
		int32 Ordinary = 0;
		int32 Objectives = 0;

		// ONE DAY OF THE EMPIRE'S CLOCK. Returns the day's report and records
		// what a falling city took with it, which must never be counted as
		// something the player beat.
		auto PassADay = [&]() -> FCataclysmDayReport
		{
			const int32 ClearedBefore = Run->DungeonsCleared;

			const FCataclysmDayReport Report = Run->AdvanceDay();

			if (!Report.Absorbed.IsEmpty())
			{
				AbsorbedInAll += Report.Absorbed.Num();
				++DaysWhereOnlyAnAbsorptionRemovedOne;

				TestEqual(FString::Printf(
					TEXT("seed %d day %d: %d dungeons were absorbed by a "
						 "falling city and none of them was counted as "
						 "cleared"), Seed, Report.Day, Report.Absorbed.Num()),
					Run->DungeonsCleared, ClearedBefore);
			}

			return Report;
		};

		int32 Advanced = 0;
		while (Advanced < MostDays && Run->Map->ExposedCities().Num() >= 2)
		{
			// WHICH DUNGEON THE PLAYER GOES TO. A Quest dungeon first, then a
			// Fallen City, then whatever is oldest.
			//
			// THIS IS THE TEST'S POLICY AND NOT A RULE OF THE GAME, and it is
			// this order so that all three kinds are actually beaten. A player
			// who only ever took the first dungeon on the list would leave the
			// quest objectives and the retaken cities to chance, and every
			// assertion about them would be satisfied by never reaching one.
			const FCataclysmDungeon* Target = nullptr;
			for (const ECataclysmDungeonType Wanted :
					{ ECataclysmDungeonType::Quest,
					  ECataclysmDungeonType::FallenCity })
			{
				for (const FCataclysmDungeon& Standing : Run->Dungeons)
				{
					if (Standing.Type == Wanted)
					{
						Target = &Standing;
						break;
					}
				}

				if (Target != nullptr)
				{
					break;
				}
			}

			if (Target == nullptr && !Run->Dungeons.IsEmpty())
			{
				Target = &Run->Dungeons[0];
			}

			if (Target == nullptr)
			{
				// NOTHING TO WALK. The day passes anyway. Since issue #1414
				// an empty board brings the next wave on the first day the
				// last surge is 25 days behind, so this player is idle for
				// at most that long; before it the wait was for the clock.
				PassADay();
				++Advanced;
				continue;
			}

			// READ BEFORE ANY TIME PASSES. `Target` points into `Dungeons` and
			// a day can add to that array, move it, or take this dungeon out of
			// it altogether.
			const int32 TargetId = Target->DungeonId;
			const ECataclysmDungeonType Kind = Target->Type;
			const int32 WalkDays =
				FMath::Max(1, FMath::CeilToInt(Target->WalkDays));

			// **WALKING A DUNGEON COSTS DAYS, AND THAT IS WHY THIS TEST DRIVES
			// THE CLOCK RATHER THAN CLEARING ONE A DAY.** The first version of
			// this test cleared a dungeon every day, which is a player nothing
			// can hurt: over twenty campaigns of six hundred days **not one
			// city fell**, so no Fallen City dungeon was ever created, nothing
			// was ever absorbed, and the two rules about those were asserted
			// against evidence that did not exist. It failed on exactly those
			// two controls, which is what they are for.
			for (int32 Spent = 0; Spent < WalkDays && Advanced < MostDays;
				 ++Spent)
			{
				PassADay();
				++Advanced;
			}

			if (Run->FindDungeon(TargetId) == nullptr)
			{
				// ITS CITY FELL WHILE THE PLAYER WAS INSIDE. The dungeon was
				// absorbed and there is nothing left to beat, which is a real
				// outcome rather than a case carved out to make this pass. Only
				// an ordinary dungeon, since 2026-10-21: a fall moves a Quest
				// dungeon, which is still here to be beaten.
				continue;
			}

			if (!Run->ClearDungeon(TargetId))
			{
				continue;
			}

			++Cleared;

			if (Kind == ECataclysmDungeonType::Basic)
			{
				++Ordinary;
			}
			else if (Kind == ECataclysmDungeonType::Quest)
			{
				++Objectives;
			}
			else if (Kind == ECataclysmDungeonType::FallenCity)
			{
				++FallenCitiesRetaken;
			}

			// AFTER EVERY CLEAR, NOT ONLY AT THE END OF THE CAMPAIGN. A total
			// compared once names neither the campaign nor the day it went
			// wrong.
			if (!TestEqual(FString::Printf(
					TEXT("seed %d day %d: the run counted the same dungeons "
						 "cleared as this test did"), Seed, Run->Day()),
					Run->DungeonsCleared, Cleared))
			{
				return false;
			}

			if (!TestEqual(FString::Printf(
					TEXT("seed %d day %d: and the same ordinary ones"),
					Seed, Run->Day()),
					Run->BasicDungeonsCleared, Ordinary))
			{
				return false;
			}

			if (!TestEqual(FString::Printf(
					TEXT("seed %d day %d: and the same quest objectives"),
					Seed, Run->Day()),
					Run->QuestObjectives, Objectives))
			{
				return false;
			}

			// AND THE ONE THAT WAS WALKED REALLY LEFT THE BOARD. Without this
			// the comparison above could be satisfied by a counter that moved
			// while the dungeon stayed where it was.
			TestNull(*FString::Printf(
				TEXT("seed %d day %d: dungeon %d really left the board"),
				Seed, Run->Day(), TargetId),
				Run->FindDungeon(TargetId));
		}

		ClearedInAll += Cleared;
		OrdinaryInAll += Ordinary;
		ObjectivesInAll += Objectives;

		// THE THREE ARE NOT INDEPENDENT AND THE RELATIONSHIP IS THE RULE. Every
		// ordinary dungeon and every quest objective is also a dungeon cleared,
		// and a Fallen City is a dungeon cleared that is neither.
		TestTrue(FString::Printf(
			TEXT("seed %d: ordinary and quest clears never exceed the total"),
			Seed),
			Run->BasicDungeonsCleared + Run->QuestObjectives
				<= Run->DungeonsCleared);

		// AND A SECOND `Begin` IS A FRESH CAMPAIGN. Carrying one run's quest
		// objectives into the next would hand slice 6's win condition a head
		// start that no player earned.
		Run->Begin(Seed, ECataclysmSurgeMode::Static);

		TestEqual(TEXT("beginning again forgets the dungeons cleared"),
				  Run->DungeonsCleared, 0);
		TestEqual(TEXT("and the ordinary ones"), Run->BasicDungeonsCleared, 0);
		TestEqual(TEXT("and the quest objectives"), Run->QuestObjectives, 0);
		TestEqual(TEXT("and the resolves that cost a city"),
				  Run->DungeonsDetonated, 0);
	}

	AddInfo(FString::Printf(
		TEXT("%d campaigns: %d dungeons cleared, of which %d ordinary, %d "
			 "quest objectives and %d retaken fallen cities. %d dungeons were "
			 "absorbed by a falling city on %d days and none was counted"),
		Campaigns, ClearedInAll, OrdinaryInAll, ObjectivesInAll,
		FallenCitiesRetaken, AbsorbedInAll,
		DaysWhereOnlyAnAbsorptionRemovedOne));

	// THE EVIDENCE HAS TO EXIST BEFORE IT CAN BE BELIEVED. Each of these makes
	// one of the assertions above non-vacuous.
	TestTrue(TEXT("dungeons were actually cleared"), ClearedInAll > 0);
	TestTrue(TEXT("and ordinary ones among them"), OrdinaryInAll > 0);
	TestTrue(TEXT("and quest objectives were actually earned"),
			 ObjectivesInAll > 0);
	TestTrue(TEXT("and a fallen city was actually retaken, so the rule that a "
				  "retake is not an ordinary clear was actually exercised"),
			 FallenCitiesRetaken > 0);
	TestTrue(TEXT("and dungeons were actually absorbed by a falling city"),
			 AbsorbedInAll > 0);

	// AND THE THREE COUNTERS ARE NOT THE SAME NUMBER. If every clear were a
	// Basic one the separation the design asks for would be untested, and a
	// build that counted every kind as ordinary would pass everything above.
	TestTrue(TEXT("the ordinary count is strictly below the total, so the "
				  "three tallies are genuinely different numbers"),
			 OrdinaryInAll < ClearedInAll);

	return true;
}

/**
 * How often a dungeon's timer running out actually cost the empire something.
 *
 * `FCataclysmDayReport::Resolved` IS NOT THAT NUMBER and its own comment said
 * so: since slice 3 of issue #1324 a Quest dungeon's timer running out costs
 * its city nothing, and a Fallen City and a Cataclysm stand on a city whose
 * damage is already done. `UCataclysmEmpireRun::DungeonsDetonated` is the count
 * a caller asking "how often was the empire hurt" needs, and this is what makes
 * it more than a second name for the length of that list.
 *
 * THE TWO ASSERTIONS THAT MATTER ARE AN INEQUALITY AND AN EQUALITY.
 *
 *   - **Never more than the timers that could detonate.** A day cannot hurt the
 *     empire more times than it ran timers out on kinds that bite.
 *   - **Exactly that many on a day where no city fell.** A city falling
 *     mid-day changes which hosts are alive part way through, so those days are
 *     excluded rather than approximated -- and there are plenty left.
 *
 * AND IT HAS TO BE STRICTLY NARROWER THAN `Resolved` SOMEWHERE, or a counter
 * that simply added one per entry in that list would pass both. The gap is
 * reported.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunDetonationCountTest,
	"Cataclysm.EmpireRun.OnlyAResolveThatCostACitySomethingIsCounted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunDetonationCountTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	constexpr int32 Campaigns = 20;
	constexpr int32 MostDays = 600;

	int32 ResolvedInAll = 0;
	int32 DetonationsInAll = 0;
	int32 QuietResolves = 0;
	int32 QuietDaysChecked = 0;
	int32 ExactDaysChecked = 0;

	for (int32 Seed = 1; Seed <= Campaigns; ++Seed)
	{
		UCataclysmEmpireRun* Run = MakeRun(Seed);

		if (!TestNotNull(TEXT("the run has a map"), Run->Map.Get()))
		{
			return false;
		}

		int32 Advanced = 0;
		while (Advanced < MostDays && Run->Map->ExposedCities().Num() >= 2)
		{
			// READ BEFORE THE DAY. A dungeon that is absorbed is gone from
			// `Dungeons` afterwards, and a city that falls is marked afterwards.
			//
			// `Resolves()` AND NOT THE KIND, so this test asks the same
			// question the empire run asks rather than repeating its answer.
			TMap<int32, bool> BitesOf;
			TMap<int32, bool> HostWasAlive;
			for (const FCataclysmDungeon& Standing : Run->Dungeons)
			{
				BitesOf.Add(Standing.DungeonId, Standing.Resolves());

				const FCataclysmCity* Host = Run->Map->Find(Standing.CityId);
				HostWasAlive.Add(Standing.DungeonId,
								 Host != nullptr && !Host->bFallen);
			}

			const int32 DetonatedBefore = Run->DungeonsDetonated;

			const FCataclysmDayReport Report = Run->AdvanceDay();
			++Advanced;

			const int32 DetonatedToday =
				Run->DungeonsDetonated - DetonatedBefore;

			ResolvedInAll += Report.Resolved.Num();
			DetonationsInAll += DetonatedToday;

			// HOW MANY OF TODAY'S TIMERS BELONGED TO A KIND THAT BITES AT ALL.
			// `FCataclysmDungeon::Resolves` is the rule and it is read off the
			// kind, which is what the empire run reads too.
			int32 CouldBite = 0;
			int32 CouldBiteALivingCity = 0;
			int32 UnknownResolves = 0;
			for (const int32 DungeonId : Report.Resolved)
			{
				const bool* Bites = BitesOf.Find(DungeonId);

				if (Bites == nullptr)
				{
					// A TIMER THAT RAN OUT FOR A DUNGEON THAT WAS NOT STANDING
					// THIS MORNING. Nothing should be able to arrive and run
					// out on the same day, so this is not expected -- but a day
					// containing one says nothing about the count either way,
					// and guessing would be worse than skipping it.
					++UnknownResolves;
					continue;
				}

				if (!*Bites)
				{
					++QuietResolves;
					continue;
				}

				++CouldBite;

				if (HostWasAlive.FindRef(DungeonId))
				{
					++CouldBiteALivingCity;
				}
			}

			if (!TestTrue(FString::Printf(
					TEXT("seed %d day %d: %d detonations against %d timers on "
						 "a kind that bites"),
					Seed, Report.Day, DetonatedToday, CouldBite),
					DetonatedToday <= CouldBite))
			{
				return false;
			}

			// A DAY WHOSE TIMERS ALL BELONGED TO A KIND THAT IS NOT ORDINARY
			// MUST NOT HAVE MOVED `DungeonsDetonated`. This is the assertion a
			// counter that just followed `Resolved` would fail. THE EMPIRE MAY
			// STILL HAVE PAID THAT DAY, since 2026-10-13: a Quest dungeon's
			// timer hits its host, and that is counted in `QuestHits` and not
			// here. The label used to say "so the empire paid nothing".
			if (CouldBite == 0 && UnknownResolves == 0
				&& !Report.Resolved.IsEmpty())
			{
				++QuietDaysChecked;

				TestEqual(FString::Printf(
					TEXT("seed %d day %d: %d timers ran out and none was an "
						 "ordinary dungeon's, so the count of ordinary "
						 "dungeons that hit a city did not move"),
					Seed, Report.Day, Report.Resolved.Num()),
					DetonatedToday, 0);
			}

			// AND ON A DAY WHERE NOTHING FELL, THE COUNT IS EXACT. A fall
			// part way through a day changes which hosts are alive after this
			// snapshot was taken, so those days are skipped rather than
			// guessed at.
			if (Report.Fallen.IsEmpty() && UnknownResolves == 0
				&& !Report.Resolved.IsEmpty())
			{
				++ExactDaysChecked;

				TestEqual(FString::Printf(
					TEXT("seed %d day %d: every biting timer on a living city "
						 "cost that city something"), Seed, Report.Day),
					DetonatedToday, CouldBiteALivingCity);
			}
		}
	}

	AddInfo(FString::Printf(
		TEXT("%d campaigns: %d timers ran out, %d of them cost a city "
			 "something, and %d belonged to a kind that never bites. Checked "
			 "exactly on %d days where nothing fell and %d days where every "
			 "timer was a quiet one"),
		Campaigns, ResolvedInAll, DetonationsInAll, QuietResolves,
		ExactDaysChecked, QuietDaysChecked));

	TestTrue(TEXT("timers actually ran out"), ResolvedInAll > 0);
	TestTrue(TEXT("and the empire actually paid for some of them"),
			 DetonationsInAll > 0);

	// **AND THE TWO NUMBERS ARE NOT THE SAME NUMBER.** Without this a counter
	// raised once per entry in `Resolved` would satisfy everything above, which
	// is exactly the arithmetic the model uses and issue #1373 records.
	TestTrue(TEXT("and strictly fewer resolves cost a city than ran out, so "
				  "this is not a second name for the report's list"),
			 DetonationsInAll < ResolvedInAll);

	TestTrue(TEXT("and days on which every timer was a quiet one were actually "
				  "reached"), QuietDaysChecked > 0);
	TestTrue(TEXT("and the exact comparison was actually made"),
			 ExactDaysChecked > 0);

	return true;
}

/**
 * `Describe` says what the player has achieved, and which dungeon is which kind.
 *
 * WHY THIS IS WORTH A TEST. `Describe` is what `Cataclysm.ShowEmpire` and
 * `Cataclysm.EmpireAdvance` print, so it is the only way anybody outside a
 * screen looks at a run -- and until issue #1324 slice 5 it had no test at all
 * while three sessions had edited it. It also said nothing about a dungeon's
 * KIND, so a Quest dungeon and an ordinary one read identically, which is
 * useless beside a count of quest objectives.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunDescribesProgressTest,
	"Cataclysm.EmpireRun.DescribeSaysWhatWasClearedAndWhichDungeonIsAQuest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunDescribesProgressTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	UCataclysmEmpireRun* Fresh = NewObject<UCataclysmEmpireRun>();

	TestEqual(TEXT("a run that has not begun says so"), Fresh->Describe(),
			  FString(TEXT("The run has not begun.")));

	// A CAMPAIGN WALKED UNTIL A QUEST DUNGEON IS STANDING. Which day that is
	// depends on the roll, so it is searched for rather than assumed; a surge
	// rolls one about an eighth of the time, and the first wave brings 12
	// since 2026-10-20.
	UCataclysmEmpireRun* Run = MakeRun(1);

	int32 QuestId = INDEX_NONE;
	int32 Advanced = 0;
	while (QuestId == INDEX_NONE && Advanced < 200)
	{
		for (const FCataclysmDungeon& Standing : Run->Dungeons)
		{
			if (Standing.Type == ECataclysmDungeonType::Quest)
			{
				QuestId = Standing.DungeonId;
				break;
			}
		}

		if (QuestId != INDEX_NONE)
		{
			break;
		}

		Run->AdvanceDay();
		++Advanced;
	}

	if (!TestTrue(TEXT("a quest dungeon was found to describe"),
				  QuestId != INDEX_NONE))
	{
		return false;
	}

	const FString WithAQuest = Run->Describe();

	TestTrue(TEXT("the quest dungeon is named as one"),
			 WithAQuest.Contains(TEXT("(Quest"), ESearchCase::CaseSensitive));

	// AND NOT THE ORDINARY ONES. "Basic" must never appear, or every line
	// carries a word that distinguishes the common case from itself.
	TestFalse(TEXT("and an ordinary dungeon is not labelled Basic"),
			  WithAQuest.Contains(TEXT("Basic"), ESearchCase::CaseSensitive));

	// NOTHING CLEARED YET SAYS SO, WITH THE NUMBERS. This is the line a person
	// running `Cataclysm.ShowEmpire` reads.
	TestTrue(TEXT("it reports nothing cleared before anything is"),
			 WithAQuest.Contains(
				 TEXT("0 dungeons cleared, 0 of them ordinary. 0 quest "
					  "objectives earned."), ESearchCase::CaseSensitive));

	TestTrue(TEXT("and it clears"), Run->ClearDungeon(QuestId));

	const FString AfterAClear = Run->Describe();

	TestTrue(TEXT("and the objective is reported"),
			 AfterAClear.Contains(
				 TEXT("1 dungeons cleared, 0 of them ordinary. 1 quest "
					  "objectives earned."), ESearchCase::CaseSensitive));

	// AND THE DUNGEON IS GONE FROM THE LISTING, so the line above is describing
	// a board that really changed rather than a counter that moved on its own.
	TestFalse(TEXT("the cleared dungeon is no longer listed"),
			  AfterAClear.Contains(
				  *FString::Printf(TEXT("dungeon %d "), QuestId),
				  ESearchCase::CaseSensitive));

	return true;
}

// ---------------------------------------------------------------------------
// A relocating Siege and the one-per-city cap -- issue #1371
// ---------------------------------------------------------------------------

namespace CataclysmEmpireRunTest
{
	/**
	 * A run with the opening surge cleared away and nothing standing.
	 *
	 * WHY A DAY IS SPENT RATHER THAN THE SCHEDULE BEING REACHED INTO. "A surge
	 * is triggered at run start", so day 1 lands a wave whatever else is done.
	 * Spending the day and then emptying both lists leaves the map intact -- no
	 * city falls in one day -- and the clock's next surge 90 days off.
	 *
	 * THE BOARD IT LEAVES IS EMPTY AND THE LAST SURGE WAS DAY 1, so since issue
	 * #1414 a run left with nothing standing is sent a wave on day 26. Every
	 * test written before that issue puts a dungeon down and stops by day 6;
	 * the empty-board tests at the end of this file use the day 26 on purpose.
	 */
	UCataclysmEmpireRun* MakeEmptyRun(int32 Seed)
	{
		UCataclysmEmpireRun* Run = MakeRun(Seed);
		Run->AdvanceDay();
		Run->Dungeons.Empty();
		Run->Clock->Timers.Empty();
		return Run;
	}

	/** Puts a dungeon on a city with a timer, exactly as a surge would. */
	void PlaceDungeon(UCataclysmEmpireRun& Run, int32 DungeonId,
					  ECataclysmDungeonType Type,
					  ECataclysmDungeonSubType SubType,
					  const FCataclysmCity& City, float ResolveDays)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = DungeonId;
		Dungeon.Type = Type;
		Dungeon.SubType = SubType;
		Dungeon.CityId = City.CityId;
		Dungeon.CityTier = City.Tier;
		Dungeon.Floors = 20;
		Dungeon.ResolveDays = ResolveDays;
		Dungeon.SpawnedDay = Run.Day();

		Run.Dungeons.Add(Dungeon);

		// THE CLOCK IS WHAT MAKES THE TIMER FIRE, and `AdvanceDay` reads the
		// clock rather than the dungeon. A dungeon added to one and not the
		// other never resolves, which would make every test below vacuous.
		Run.Clock->AddDungeon(DungeonId, Dungeon.Floors);
		Run.Clock->SetResolveDays(DungeonId, ResolveDays);
	}

	/** A rim Outpost with exactly two perimeter links, and those two links.
	 *
	 * ON AN INTACT MAP A RIM OUTPOST'S EXPOSED NEIGHBOURS ARE ITS PERIMETER
	 * LINKS AND NOTHING ELSE: it has no cities further out, and the ring inside
	 * it is sealed precisely because the rim still stands. So a scenario built
	 * on one has a known target set of exactly two cities, and a dungeon that
	 * ends up anywhere else is a defect rather than an option nobody thought
	 * about.
	 */
	bool FindRimPair(const UCataclysmEmpireRun& Run, int32& OutHome,
					 int32& OutFirst, int32& OutSecond)
	{
		for (const FCataclysmCity& City : Run.Map->Cities)
		{
			if (City.Ring() == 3 && City.Perimeter.Num() == 2)
			{
				OutHome = City.CityId;
				OutFirst = City.Perimeter[0];
				OutSecond = City.Perimeter[1];
				return true;
			}
		}

		return false;
	}
}

/**
 * A Quest dungeon carrying a Siege refuses a city that already has one.
 *
 * **THE OWNER'S RULING OF 2026-09-06, VERBATIM: "Check the limit on arrival
 * too".** Issue #1371. The design's Siege row says "Max 1 per city".
 * `UCataclysmSurgeScheduler::RollSubType` enforced that when a dungeon was
 * created and nothing enforced it when one moved, so a Quest dungeon that had
 * rolled Siege could walk onto a besieged city and the city would take the daily
 * bite twice. Two alternatives were offered and rejected: letting two Sieges
 * share a city, and barring a Quest dungeon from carrying a Siege at all.
 *
 * **THE SITUATION IS BUILT BY HAND, AND THAT IS THE WHOLE POINT OF THIS TEST.**
 * Issue #1371 measured it over 40 campaigns of the model: **2** Siege-carrying
 * Quest dungeons moved and **0** landings would have broken the cap. So a test
 * that ran campaigns and found no violation would pass, unchanged, against a
 * build with no check in it -- it would be reporting the rarity of the situation
 * and calling it a rule. Everything below places the dungeons itself.
 *
 * WHAT EACH PART DOES.
 *
 *   1. `Redirected` -- a besieged neighbour and a free one. The only legal
 *      answer is the free one, and it has to actually be reached: refusing the
 *      move altogether would satisfy "it did not land on the besieged city"
 *      while breaking the owner's "pick another adjacent city".
 *   2. `Stayed` -- the same scenario when the coin says no. Counted rather than
 *      forbidden, because `QuestMoveChance` is 0.5 and about half of these do
 *      exactly that; both are asserted to happen so neither can be the whole
 *      sample.
 *   3. Every neighbour besieged -- the owner's other outcome, "or stay where it
 *      is if there is none". Asserted on every seed, not as a rate.
 *   4. **A dungeon carrying no Siege is not refused.** The control that keeps
 *      the rule narrow: the cap is "max 1 Siege per city", not "one dungeon per
 *      besieged city", and a check written against the destination alone would
 *      pass part 1 and fail this.
 *   5. **The besieged city is a legal target when nothing carries a Siege.**
 *      The control that makes the refusal mean something: if that city were
 *      sealed or not adjacent, the ordinary filter would already exclude it and
 *      parts 1 to 3 would pass with no Siege rule present at all.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunSiegeArrivalTest,
	"Cataclysm.EmpireRun.ARelocatingSiegeRefusesACityThatAlreadyHasOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunSiegeArrivalTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	constexpr int32 Seeds = 60;
	constexpr int32 Wanderer = 9000;
	constexpr int32 Sitting = 9001;
	constexpr int32 SecondSitting = 9002;

	// LONG ENOUGH THAT THE SITTING SIEGE NEVER RESOLVES while the wandering
	// dungeon's one-day timer does. A Siege whose own timer fired would move
	// nothing -- a Basic dungeon bites -- but it would take draws off the
	// stream and muddle what the run did.
	constexpr float Never = 900.0f;

	int32 Redirected = 0;
	int32 Stayed = 0;
	int32 LandedOnTheBesiegedCity = 0;
	int32 PlainDungeonReachedTheBesiegedCity = 0;

	for (int32 Seed = 1; Seed <= Seeds; ++Seed)
	{
		// -- 1 and 2: one neighbour besieged, one free ----------------------
		{
			UCataclysmEmpireRun* Run = MakeEmptyRun(Seed);

			int32 Home = INDEX_NONE;
			int32 Blocked = INDEX_NONE;
			int32 Free = INDEX_NONE;

			if (!TestTrue(TEXT("the map has a rim Outpost with two perimeter "
							   "links"),
						  FindRimPair(*Run, Home, Blocked, Free)))
			{
				return false;
			}

			PlaceDungeon(*Run, Sitting, ECataclysmDungeonType::Basic,
						 ECataclysmDungeonSubType::Siege,
						 *Run->Map->Find(Blocked), Never);
			PlaceDungeon(*Run, Wanderer, ECataclysmDungeonType::Quest,
						 ECataclysmDungeonSubType::Siege,
						 *Run->Map->Find(Home), 1.0f);

			// THE CONTROL, AND IT IS CHECKED BEFORE THE DAY RATHER THAN
			// ASSUMED. `PickRelocation` asked with no Siege information offers
			// both neighbours; if it did not, the refusal below would be the
			// ordinary adjacency filter doing the work and this test would
			// prove nothing about issue #1371.
			{
				FRandomStream Probe(Seed);
				TSet<int32> Offered;
				for (int32 Draw = 0; Draw < 40; ++Draw)
				{
					Offered.Add(UCataclysmSurgeScheduler::PickRelocation(
						*Run->Map, *Run->Map->Find(Home), Probe));
				}

				TestTrue(TEXT("the besieged city is an ordinary legal target "
							  "when nothing carries a Siege"),
						 Offered.Contains(Blocked));
				TestTrue(TEXT("and so is the free one"),
						 Offered.Contains(Free));
			}

			const FCataclysmDayReport Report = Run->AdvanceDay();

			if (!TestTrue(TEXT("the wandering dungeon's timer ran out"),
						  Report.Resolved.Contains(Wanderer)))
			{
				return false;
			}

			const FCataclysmDungeon* Moved = Run->FindDungeon(Wanderer);

			if (!TestNotNull(TEXT("and it is still standing"), Moved))
			{
				return false;
			}

			if (Moved->CityId == Blocked)
			{
				++LandedOnTheBesiegedCity;

				AddError(FString::Printf(
					TEXT("seed %d: a Quest dungeon carrying a Siege moved onto "
						 "%s, which already holds one. The owner ruled on "
						 "2026-09-06, verbatim \"Check the limit on arrival "
						 "too\"; issue #1371"),
					Seed, *Run->Map->Find(Blocked)->Name));
			}
			else if (Moved->CityId == Free)
			{
				++Redirected;
			}
			else if (Moved->CityId == Home)
			{
				++Stayed;
			}
			else
			{
				AddError(FString::Printf(
					TEXT("seed %d: the dungeon reached city %d, which is "
						 "neither of its two rim neighbours nor the city it "
						 "stood on"),
					Seed, Moved->CityId));
			}

			// AND THE CAP HELD ON EVERY CITY, read off the run rather than
			// inferred from where this one dungeon went.
			for (const FCataclysmCity& City : Run->Map->Cities)
			{
				int32 Sieges = 0;
				for (const FCataclysmDungeon& Standing : Run->Dungeons)
				{
					if (Standing.SubType == ECataclysmDungeonSubType::Siege
						&& Standing.CityId == City.CityId)
					{
						++Sieges;
					}
				}

				TestTrue(FString::Printf(
					TEXT("%s holds no more than %d Siege"), *City.Name,
					UCataclysmSurgeScheduler::SiegesPerCity),
					Sieges <= UCataclysmSurgeScheduler::SiegesPerCity);
			}
		}

		// -- 3: every neighbour besieged, so it stays ------------------------
		{
			UCataclysmEmpireRun* Run = MakeEmptyRun(Seed);

			int32 Home = INDEX_NONE;
			int32 First = INDEX_NONE;
			int32 Second = INDEX_NONE;
			FindRimPair(*Run, Home, First, Second);

			PlaceDungeon(*Run, Sitting, ECataclysmDungeonType::Basic,
						 ECataclysmDungeonSubType::Siege,
						 *Run->Map->Find(First), Never);
			PlaceDungeon(*Run, SecondSitting, ECataclysmDungeonType::Basic,
						 ECataclysmDungeonSubType::Siege,
						 *Run->Map->Find(Second), Never);
			PlaceDungeon(*Run, Wanderer, ECataclysmDungeonType::Quest,
						 ECataclysmDungeonSubType::Siege,
						 *Run->Map->Find(Home), 1.0f);

			const FCataclysmDayReport Report = Run->AdvanceDay();
			const FCataclysmDungeon* Moved = Run->FindDungeon(Wanderer);

			if (Moved != nullptr)
			{
				TestEqual(FString::Printf(
					TEXT("seed %d: every neighbour is besieged, so the dungeon "
						 "stays where it is"), Seed),
					Moved->CityId, Home);

				TestFalse(FString::Printf(
					TEXT("seed %d: and it is not in the day's relocated list"),
					Seed),
					Report.Relocated.Contains(Wanderer));
			}
		}

		// -- 4: a dungeon carrying no Siege is not refused -------------------
		{
			UCataclysmEmpireRun* Run = MakeEmptyRun(Seed);

			int32 Home = INDEX_NONE;
			int32 Blocked = INDEX_NONE;
			int32 Free = INDEX_NONE;
			FindRimPair(*Run, Home, Blocked, Free);

			PlaceDungeon(*Run, Sitting, ECataclysmDungeonType::Basic,
						 ECataclysmDungeonSubType::Siege,
						 *Run->Map->Find(Blocked), Never);
			PlaceDungeon(*Run, Wanderer, ECataclysmDungeonType::Quest,
						 ECataclysmDungeonSubType::Horde,
						 *Run->Map->Find(Home), 1.0f);

			Run->AdvanceDay();

			const FCataclysmDungeon* Moved = Run->FindDungeon(Wanderer);

			if (Moved != nullptr && Moved->CityId == Blocked)
			{
				++PlainDungeonReachedTheBesiegedCity;
			}
		}
	}

	AddInfo(FString::Printf(
		TEXT("%d seeds: a Siege-carrying Quest dungeon was redirected to the "
			 "free neighbour %d times, stayed %d times because the coin said "
			 "no, and landed on the besieged city %d times. A Horde-carrying "
			 "one reached the besieged city %d times"),
		Seeds, Redirected, Stayed, LandedOnTheBesiegedCity,
		PlainDungeonReachedTheBesiegedCity));

	// **THE RULE.** One counter-example is a failure; this is not a rate.
	TestEqual(TEXT("no Siege-carrying dungeon ever landed on a besieged city"),
			  LandedOnTheBesiegedCity, 0);

	// **AND IT WAS REDIRECTED RATHER THAN STOPPED.** The owner ruled it must
	// "pick another adjacent city"; a check that simply cancelled the move
	// would pass the assertion above and fail the ruling.
	TestTrue(TEXT("and it reached the free neighbour instead, at least once"),
			 Redirected > 0);

	// BOTH OUTCOMES OF THE COIN OCCURRED, so neither half of the sample is the
	// whole of it. `QuestMoveChance` is 0.5 and these are 60 independent seeds.
	TestTrue(TEXT("and some of the seeds declined the move, which is the "
				  "chance rather than the cap"),
			 Stayed > 0);

	// **THE CONTROL THAT KEEPS THE RULE NARROW.** Without this, a
	// `PickRelocation` that refused every besieged city to every dungeon would
	// pass everything above -- and that is a different, much larger rule than
	// the one the owner ruled.
	TestTrue(TEXT("a dungeon carrying no Siege still reached the besieged "
				  "city"),
			 PlainDungeonReachedTheBesiegedCity > 0);

	return true;
}

// ---------------------------------------------------------------------------
// A death inside a dungeon -- issue #41
// ---------------------------------------------------------------------------
//
// `ResolveDungeonOnDeath` and `DeathDayCost` on their own: a run, a map and a
// clock, and no game mode, no world and no character. What the game does with
// them is `ACataclysmDungeonGameMode::EndTheDungeonForADeath`, tested in the
// `Cataclysm` module's `CataclysmDungeonCostsDaysTests.cpp`.

namespace CataclysmEmpireRunTest
{
	/** The first Outpost of the map that has not fallen, or `INDEX_NONE`. */
	int32 FirstStandingOutpost(const UCataclysmEmpireRun& Run)
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
	 * Gives a dungeon `PlaceDungeon` put down the points it takes when it
	 * resolves, and writes how many days its timer has left.
	 *
	 * `PlaceDungeon` LEAVES THE DAMAGE AT NOUGHT, which the relocation tests it
	 * was written for do not read. A death's resolve is measured by what the
	 * city pays, so these tests give every kind some, the kinds that never
	 * resolve included: if one of those were resolved by mistake it would show.
	 */
	void GiveDamageAndDaysLeft(UCataclysmEmpireRun& Run, int32 DungeonId,
							   float DefencePoints, float PopulationPoints,
							   float DaysLeft)
	{
		for (FCataclysmDungeon& Standing : Run.Dungeons)
		{
			if (Standing.DungeonId == DungeonId)
			{
				Standing.DefenceDamage = DefencePoints;
				Standing.PopulationDamage = PopulationPoints;
			}
		}

		for (FCataclysmDungeonTimer& Counting : Run.Clock->Timers)
		{
			if (Counting.DungeonId == DungeonId)
			{
				Counting.DaysUntilResolve = DaysLeft;
			}
		}
	}

	/** Every city's population, indexed by city identifier. */
	TArray<float> PopulationSnapshot(const UCataclysmEmpireRun& Run)
	{
		TArray<float> Population;
		for (const FCataclysmCity& City : Run.Map->Cities)
		{
			Population.Add(City.Population);
		}
		return Population;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunDeathResolvesTest,
	"Cataclysm.EmpireRun.ADeathMakesAnOrdinaryDungeonHitItsCityOnceAndLeaveTheMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunDeathResolvesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	const int32 DiedIn = 9001;

	// A FULL TIMER OF THIRTY DAYS WITH TWO LEFT, on an ordinary dungeon that
	// takes 100 points of defence and 40 of population before scaling.
	const float FullTimer = 30.0f;
	const float DaysLeft = 2.0f;

	// -- THE CONTROL: no death, and five days pass -------------------------
	//
	// The two days run out and the timer resolves the dungeon. This is what the
	// last check below is measured against: without it, "no second resolve in
	// five days" would be true of a dungeon whose timer could not have run out.
	// Since 2026-10-13 that dungeon then leaves the map, so one is also the
	// most it can ever be.
	{
		UCataclysmEmpireRun* Run = MakeEmptyRun(1);
		const int32 CityId = FirstStandingOutpost(*Run);
		if (!TestTrue(TEXT("set-up: the control's map has an Outpost"),
					  CityId != INDEX_NONE))
		{
			return false;
		}

		PlaceDungeon(*Run, DiedIn, ECataclysmDungeonType::Basic,
					 ECataclysmDungeonSubType::None, *Run->Map->Find(CityId),
					 FullTimer);
		GiveDamageAndDaysLeft(*Run, DiedIn, 100.0f, 40.0f, DaysLeft);

		Run->AdvanceDays(5);

		TestEqual(TEXT("CONTROL: with no death the two days run out and the timer resolves it once"),
				  Run->DungeonsDetonated, 1);
	}

	// -- THE DEATH --------------------------------------------------------
	UCataclysmEmpireRun* Run = MakeEmptyRun(1);
	const int32 CityId = FirstStandingOutpost(*Run);
	if (!TestTrue(TEXT("set-up: the map has an Outpost"), CityId != INDEX_NONE))
	{
		return false;
	}

	PlaceDungeon(*Run, DiedIn, ECataclysmDungeonType::Basic,
				 ECataclysmDungeonSubType::None, *Run->Map->Find(CityId), FullTimer);
	GiveDamageAndDaysLeft(*Run, DiedIn, 100.0f, 40.0f, DaysLeft);

	// THE PLAYER IS INSIDE IT, which is what the game mode tells the clock.
	Run->Clock->EnterDungeon(DiedIn);

	const FCataclysmDungeon* Placed = Run->FindDungeon(DiedIn);
	const FCataclysmCity* City = Run->Map->Find(CityId);
	if (!TestNotNull(TEXT("set-up: the dungeon is on the map"), Placed)
		|| !TestNotNull(TEXT("set-up: and so is its city"), City))
	{
		return false;
	}

	// WHAT ONE RESOLVE TAKES, worked out the way `ResolveDungeon` does.
	const float DefenceBite = Placed->DefenceDamage * Placed->BiteScale();
	const float PopulationBite = Placed->PopulationDamage * Placed->BiteScale();

	const float DefenceBefore = City->Defence;
	const float PopulationBefore = City->Population;
	const int32 DayBefore = Run->Day();

	if (!TestTrue(TEXT("set-up: one resolve is a real amount the city can pay twice"),
				  DefenceBite > 1.0f && PopulationBite > 1.0f
					  && DefenceBefore > 2.0f * DefenceBite
					  && PopulationBefore > 2.0f * PopulationBite)
		|| !TestEqual(TEXT("set-up: before the death no resolve has cost a city"),
					  Run->DungeonsDetonated, 0))
	{
		return false;
	}

	const FCataclysmDayReport Report = Run->ResolveDungeonOnDeath(DiedIn);

	// THE CONSEQUENCE LANDED ON ITS CITY, ONCE.
	TestEqual(TEXT("the city lost one resolve's worth of defence"),
			  DefenceBefore - City->Defence, DefenceBite, 0.01f);
	TestEqual(TEXT("and one resolve's worth of population"),
			  PopulationBefore - City->Population, PopulationBite, 0.01f);
	TestEqual(TEXT("one resolve has cost a city"), Run->DungeonsDetonated, 1);

	// THE REPORT SAYS SO, AND NO DAY PASSED.
	TestEqual(TEXT("the report names one dungeon as resolved"),
			  Report.Resolved.Num(), 1);
	TestTrue(TEXT("and it is this one"), Report.Resolved.Contains(DiedIn));
	TestEqual(TEXT("no city fell"), Report.Fallen.Num(), 0);
	TestEqual(TEXT("the report carries the day it already was"),
			  Report.Day, DayBefore);
	TestEqual(TEXT("and the run's day did not move"), Run->Day(), DayBefore);

	// IT HIT ITS CITY, SO IT IS OFF THE MAP AND OFF THE CLOCK. Until 2026-10-13
	// this test held the opposite: the dungeon stayed with its timer back at
	// thirty days. The owner's rule of 2026-10-08, verbatim: "Ordinary dungeons
	// will do their city/population damage, and then despawn."
	TestNull(TEXT("the dungeon has no timer"), Run->Clock->FindTimer(DiedIn));
	TestNull(TEXT("the dungeon is off the map"), Run->FindDungeon(DiedIn));

	// THE COUNT OF THAT DUNGEON'S RESOLVES IS NOT KEPT ANYWHERE NOW. It lived on
	// the dungeon's timer, `FCataclysmDungeonTimer::TimesResolved`, and the
	// timer left with the dungeon. This was the one dungeon on the board, so
	// the clock holds no timer at all. The run's own count, one, is asserted
	// above.
	TestEqual(TEXT("the clock keeps no count of that dungeon's resolves: no timer is left on it"),
			  Run->Clock->Timers.Num(), 0);

	// THE REPORT NAMES THE CITY IT HIT AND LEFT, AND NOTHING AS ABSORBED.
	TestEqual(TEXT("the report names one city as hit by a dungeon that left"),
			  Report.HitAndLeft.Num(), 1);
	TestTrue(TEXT("and it is this dungeon's city"),
			 Report.HitAndLeft.Contains(CityId));

	// THE CLOCK NO LONGER HOLDS THE PLAYER INSIDE IT. Removing the dungeon tells
	// the clock so; the caller still leaves, which then changes nothing here.
	TestEqual(TEXT("the clock does not hold the player inside it"),
			  Run->Clock->CurrentDungeonId, static_cast<int32>(INDEX_NONE));

	// AND NOTHING MORE LANDS. The caller leaves, and five days pass, as a
	// Standard death charges. The board is empty for them; the last surge was
	// day 1, so no empty-board wave is due before day 26.
	Run->Clock->LeaveDungeon();
	Run->AdvanceDays(5);

	TestEqual(TEXT("five days later still only the one resolve has cost a city"),
			  Run->DungeonsDetonated, 1);
	TestEqual(TEXT("and the city has lost nothing more"),
			  DefenceBefore - City->Defence, DefenceBite, 0.01f);
	TestEqual(TEXT("five days later no timer counts down for it"),
			  Run->Clock->DaysUntilResolveFor(DiedIn), -1.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunDeathSparesOtherKindsTest,
	"Cataclysm.EmpireRun.ADeathResolvesNothingInAQuestDungeonADungeonCityOrADungeonThatIsNotThere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunDeathSparesOtherKindsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	const int32 DiedIn = 9001;

	// EACH KIND THAT DOES NOT RESOLVE, ON A STANDING CITY, CARRYING DAMAGE. A
	// Quest dungeon, a Dungeon City and the Cataclysm's own dungeon carry none
	// in the game; here each is given 100 and 40 so a mistaken resolve shows.
	//
	// THREE DAYS LEFT ON A TIMER OF TWENTY-FIVE, which was a Quest dungeon's
	// flat timer until 2026-10-19; since then the game draws a Quest timer from
	// the depth, and twenty-five here is only a figure longer than the days
	// left. Since 2026-10-13 that timer hits the host city when it runs out;
	// it does so in both passes alike, so the comparison below is unchanged.
	// Each kind is then run on for five days twice, once
	// after the call and once without it, and the two must agree: the call
	// moved no timer and drew nothing from the run's chance, so the move a
	// Quest dungeon makes when its clock runs out comes out the same.
	const ECataclysmDungeonType Kinds[3] = {
		ECataclysmDungeonType::Quest,
		ECataclysmDungeonType::FallenCity,
		ECataclysmDungeonType::Cataclysm};
	const TCHAR* const KindNames[3] = {
		TEXT("Quest dungeon"), TEXT("Dungeon City"), TEXT("Cataclysm dungeon")};

	for (int32 Index = 0; Index < 3; ++Index)
	{
		const TCHAR* const Kind = KindNames[Index];

		float TimerAfterTheDays[2] = {0.0f, 0.0f};
		int32 CityAfterTheDays[2] = {INDEX_NONE, INDEX_NONE};
		int32 ResolvesAfterTheDays[2] = {-1, -1};

		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			const bool bThePlayerDied = Pass == 0;

			UCataclysmEmpireRun* Run = MakeEmptyRun(1);
			const int32 CityId = FirstStandingOutpost(*Run);
			if (!TestTrue(TEXT("set-up: the map has an Outpost"),
						  CityId != INDEX_NONE))
			{
				return false;
			}

			PlaceDungeon(*Run, DiedIn, Kinds[Index], ECataclysmDungeonSubType::None,
						 *Run->Map->Find(CityId), 25.0f);
			GiveDamageAndDaysLeft(*Run, DiedIn, 100.0f, 40.0f, 3.0f);

			if (bThePlayerDied)
			{
				const TArray<float> DefenceBefore = DefenceSnapshot(*Run);
				const TArray<float> PopulationBefore = PopulationSnapshot(*Run);
				const int32 DayBefore = Run->Day();

				const FCataclysmDayReport Report = Run->ResolveDungeonOnDeath(DiedIn);

				TestEqual(FString::Printf(TEXT("%s: the report names nothing as resolved"), Kind),
						  Report.Resolved.Num(), 0);
				TestEqual(FString::Printf(TEXT("%s: and nothing as moved"), Kind),
						  Report.Relocated.Num(), 0);
				TestEqual(FString::Printf(TEXT("%s: no resolve cost a city"), Kind),
						  Run->DungeonsDetonated, 0);
				TestTrue(FString::Printf(TEXT("%s: no city's defence changed"), Kind),
						 DefenceSnapshot(*Run) == DefenceBefore);
				TestTrue(FString::Printf(TEXT("%s: nor any city's population"), Kind),
						 PopulationSnapshot(*Run) == PopulationBefore);
				TestEqual(FString::Printf(TEXT("%s: its timer still has its three days"), Kind),
						  Run->Clock->DaysUntilResolveFor(DiedIn), 3.0f, 0.001f);
				TestEqual(FString::Printf(TEXT("%s: no day passed"), Kind),
						  Run->Day(), DayBefore);

				const FCataclysmDungeon* Still = Run->FindDungeon(DiedIn);
				if (!TestNotNull(*FString::Printf(TEXT("%s: it is still on the map"), Kind),
								 Still))
				{
					return false;
				}
				TestEqual(FString::Printf(TEXT("%s: on the city it was on"), Kind),
						  Still->CityId, CityId);
			}

			Run->AdvanceDays(5);

			const FCataclysmDungeon* Later = Run->FindDungeon(DiedIn);
			const FCataclysmDungeonTimer* Counting = Run->Clock->FindTimer(DiedIn);
			TimerAfterTheDays[Pass] = Run->Clock->DaysUntilResolveFor(DiedIn);
			CityAfterTheDays[Pass] = Later ? Later->CityId : INDEX_NONE;
			ResolvesAfterTheDays[Pass] = Counting ? Counting->TimesResolved : -1;
		}

		// THE CONTROL IS THE SECOND PASS, the same five days with no death.
		TestEqual(FString::Printf(
					  TEXT("CONTROL, %s: with no death its clock ran out on the third day and has run two since"),
					  Kind),
				  TimerAfterTheDays[1], 23.0f, 0.001f);
		TestEqual(FString::Printf(
					  TEXT("%s: five days after the death its timer is what the days alone make it"),
					  Kind),
				  TimerAfterTheDays[0], TimerAfterTheDays[1], 0.001f);
		TestEqual(FString::Printf(
					  TEXT("%s: it stands on the city the days alone leave it on"), Kind),
				  CityAfterTheDays[0], CityAfterTheDays[1]);
		TestEqual(FString::Printf(
					  TEXT("%s: its timer has run out as often as the days alone make it"),
					  Kind),
				  ResolvesAfterTheDays[0], ResolvesAfterTheDays[1]);
	}

	// A DUNGEON THAT IS NOT THERE, AND A RUN THAT HAS NOT BEGUN. Neither is an
	// error and neither does anything.
	{
		UCataclysmEmpireRun* Run = MakeEmptyRun(1);
		const TArray<float> DefenceBefore = DefenceSnapshot(*Run);

		const FCataclysmDayReport Report = Run->ResolveDungeonOnDeath(DiedIn);

		TestEqual(TEXT("no such dungeon: the report names nothing as resolved"),
				  Report.Resolved.Num(), 0);
		TestEqual(TEXT("no such dungeon: no resolve cost a city"),
				  Run->DungeonsDetonated, 0);
		TestTrue(TEXT("no such dungeon: no city's defence changed"),
				 DefenceSnapshot(*Run) == DefenceBefore);

		UCataclysmEmpireRun* NotBegun = NewObject<UCataclysmEmpireRun>();
		const FCataclysmDayReport Nothing = NotBegun->ResolveDungeonOnDeath(DiedIn);
		TestEqual(TEXT("a run that has not begun: the report names nothing as resolved"),
				  Nothing.Resolved.Num(), 0);
		TestEqual(TEXT("a run that has not begun: and carries no day"), Nothing.Day, 0);
	}

	// THE CONTROL THAT THE DAMAGE WAS REAL. The same dungeon as an ordinary one
	// does cost its city, so the three kinds above were spared by their kind.
	{
		UCataclysmEmpireRun* Run = MakeEmptyRun(1);
		const int32 CityId = FirstStandingOutpost(*Run);
		if (!TestTrue(TEXT("set-up: the control's map has an Outpost"),
					  CityId != INDEX_NONE))
		{
			return false;
		}

		PlaceDungeon(*Run, DiedIn, ECataclysmDungeonType::Basic,
					 ECataclysmDungeonSubType::None, *Run->Map->Find(CityId), 25.0f);
		GiveDamageAndDaysLeft(*Run, DiedIn, 100.0f, 40.0f, 3.0f);

		const float DefenceBefore = Run->Map->Find(CityId)->Defence;
		Run->ResolveDungeonOnDeath(DiedIn);

		TestTrue(FString::Printf(
					 TEXT("CONTROL: an ordinary dungeon carrying the same damage took %.1f defence"),
					 DefenceBefore - Run->Map->Find(CityId)->Defence),
				 DefenceBefore - Run->Map->Find(CityId)->Defence > 100.0f);
		TestEqual(TEXT("CONTROL: and one resolve cost a city"),
				  Run->DungeonsDetonated, 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunDeathFellsTheCityTest,
	"Cataclysm.EmpireRun.ADeathWhoseResolveFellsTheCityAbsorbsTheDungeonAndLeavesNoTimerBehind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunDeathFellsTheCityTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	const int32 DiedIn = 9001;
	const int32 Beside = 9002;

	UCataclysmEmpireRun* Run = MakeEmptyRun(1);
	const int32 CityId = FirstStandingOutpost(*Run);
	FCataclysmCity* Weakened =
		CityId != INDEX_NONE ? Run->Map->FindMutable(CityId) : nullptr;
	if (!TestNotNull(TEXT("set-up: the map has an Outpost"), Weakened))
	{
		return false;
	}

	// TEN POINTS OF DEFENCE LEFT, and two ordinary dungeons on the city that
	// each take far more than ten. The player is inside the first.
	Weakened->Defence = 10.0f;

	PlaceDungeon(*Run, DiedIn, ECataclysmDungeonType::Basic,
				 ECataclysmDungeonSubType::None, *Weakened, 30.0f);
	PlaceDungeon(*Run, Beside, ECataclysmDungeonType::Basic,
				 ECataclysmDungeonSubType::None, *Weakened, 30.0f);
	GiveDamageAndDaysLeft(*Run, DiedIn, 100.0f, 40.0f, 30.0f);
	GiveDamageAndDaysLeft(*Run, Beside, 100.0f, 40.0f, 30.0f);
	Run->Clock->EnterDungeon(DiedIn);

	const int32 FallenBefore = Run->Map->FallenCityCount();
	const int32 DayBefore = Run->Day();

	// THE CONTROL: before the death the city stands and so do both dungeons.
	if (!TestFalse(TEXT("CONTROL: before the death the city has not fallen"),
				   Weakened->bFallen)
		|| !TestNotNull(TEXT("CONTROL: and the dungeon the player is in is on the map"),
						Run->FindDungeon(DiedIn))
		|| !TestNotNull(TEXT("CONTROL: and so is the one beside it"),
						Run->FindDungeon(Beside)))
	{
		return false;
	}

	// ASKED BEFORE THE FALL, because the fall's own surge would move the day it is measured from. Until issue
	// #2380 this test compared the report with the bare flag `bSurgeOnCityFall`; a fall's surge now also waits
	// for the least interval, and this death is on day 1, the day of the opening wave, so none is due. Ruled by
	// the coordinating session on 2026-10-12. THE CASE WHERE THE ANSWER IS YES is
	// `Cataclysm.EmpireRun.ACityFallingTwentyFiveDaysAfterTheLastSurgeBringsOneWave`.
	const bool bAFallsSurgeWasDue = Run->Surges->IsDueOnCityFall(Run->Day());
	TestFalse(TEXT("set-up: on the day of the opening wave a fall's surge is not due"), bAFallsSurgeWasDue);
	const FCataclysmDayReport Report = Run->ResolveDungeonOnDeath(DiedIn);

	// THE CITY FELL TO THE RESOLVE.
	TestTrue(TEXT("the city fell"), Run->Map->Find(CityId)->bFallen);
	TestEqual(TEXT("and it is the one city that fell"),
			  Run->Map->FallenCityCount(), FallenBefore + 1);
	TestTrue(TEXT("the report names the city as fallen"),
			 Report.Fallen.Contains(CityId));
	TestEqual(TEXT("one resolve cost a city"), Run->DungeonsDetonated, 1);
	TestEqual(TEXT("no day passed"), Run->Day(), DayBefore);

	// AND TOOK BOTH DUNGEONS WITH IT.
	TestTrue(TEXT("the report names the dungeon the player died in as absorbed"),
			 Report.Absorbed.Contains(DiedIn));
	TestTrue(TEXT("and the one beside it"), Report.Absorbed.Contains(Beside));
	TestNull(TEXT("the dungeon the player died in is off the map"),
			 Run->FindDungeon(DiedIn));
	TestNull(TEXT("and so is the one beside it"), Run->FindDungeon(Beside));

	// NOTHING WAS DONE TO THE DUNGEON THAT IS GONE. No timer was put back for
	// it, the clock holds nobody inside it, and the two lists still agree.
	TestNull(TEXT("no timer counts down for the dungeon that is gone"),
			 Run->Clock->FindTimer(DiedIn));
	TestEqual(TEXT("the clock holds nobody inside a dungeon"),
			  Run->Clock->CurrentDungeonId, INDEX_NONE);

	FString WhyNot;
	const bool bListsAgree = UCataclysmEmpireRun::DungeonsAgreeWithTimers(
		Run->Dungeons, Run->Clock->Timers, WhyNot);
	TestTrue(FString::Printf(
				 TEXT("every dungeon still has one timer and every timer a dungeon: %s"),
				 *WhyNot),
			 bListsAgree);

	// AND WHAT A CITY FALLING DOES STILL HAPPENED: it became a Dungeon City, and
	// its fall fired a surge exactly when the scheduler said a fall's surge was due.
	bool bDungeonCityStands = false;
	for (const FCataclysmDungeon& Standing : Run->Dungeons)
	{
		bDungeonCityStands = bDungeonCityStands
			|| (Standing.Type == ECataclysmDungeonType::FallenCity
				&& Standing.CityId == CityId);
	}
	TestTrue(TEXT("a Dungeon City stands where the city fell"), bDungeonCityStands);
	TestTrue(TEXT("the report says a surge fired exactly when the scheduler said a fall's was due"),
			 Report.bSurged == bAFallsSurgeWasDue);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunDeathDayCostTest,
	"Cataclysm.EmpireRun.TheDaysADeathCostsAreTheRunsLethalityModes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunDeathDayCostTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	// THE DESIGNED FIGURES, WRITTEN OUT: `docs/Cataclysm_GDD_v2.md` section II,
	// 5 days in Standard, 10 in Hardcore, 15 in Heretic. Each run is the control
	// for the other two: one answer for every rung would fail two of the three.
	const int32 DesignedCost[3] = {5, 10, 15};
	const TCHAR* const ModeName[3] =
		{TEXT("Standard"), TEXT("Hardcore"), TEXT("Heretic")};

	for (int32 Rung = 0; Rung < 3; ++Rung)
	{
		const UCataclysmEmpireRun* Run =
			MakeRun(1, ECataclysmSurgeMode::Static, Rung);

		TestEqual(FString::Printf(TEXT("a %s run charges %d days for a death"),
								  ModeName[Rung], DesignedCost[Rung]),
				  Run->DeathDayCost(), DesignedCost[Rung]);
	}

	// A RUN THAT HAS NOT BEGUN HAS NO MODE, and answers the gentlest.
	const UCataclysmEmpireRun* NotBegun = NewObject<UCataclysmEmpireRun>();
	TestEqual(TEXT("a run that has not begun answers Standard's 5"),
			  NotBegun->DeathDayCost(), 5);

	return true;
}

// ---------------------------------------------------------------------------
// A surge when the board empties -- issue #1414
// ---------------------------------------------------------------------------

namespace CataclysmEmpireRunTest
{
	/** A timer none of the empty-board tests lives long enough to see run out. */
	const float EmptyBoardNeverDays = 10000.0f;

	/**
	 * Advances a run a day at a time until its clock reads `Day`.
	 *
	 * @return how many of those days reported a surge of any cause.
	 */
	int32 SurgesWhileAdvancingTo(UCataclysmEmpireRun& Run, int32 Day)
	{
		int32 Surged = 0;

		while (Run.Day() < Day)
		{
			if (Run.AdvanceDay().bSurged)
			{
				++Surged;
			}
		}

		return Surged;
	}

	/** Clears every dungeon standing, as a player who beat them all would. */
	void ClearTheWholeBoard(UCataclysmEmpireRun& Run)
	{
		while (Run.DungeonCount() > 0)
		{
			if (!Run.ClearDungeon(Run.Dungeons[0].DungeonId))
			{
				return;
			}
		}
	}
}

/**
 * The scheduler's own answer, with no run and no map.
 *
 * `IsDueOnEmptyBoard` is three conditions: the rule is on, nothing stands, and
 * the last surge is at least `LeastIntervalDays` (25) behind. Each row below
 * differs from a row beside it in exactly one of the last two, so each is the
 * control for its neighbour. The flag is a constant and cannot be turned off
 * from a test; `tools/tests/test_surge_port.py` holds it equal to the model's.
 *
 * NO ACTOR AND NO WORLD. Every figure is a day number written by hand.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunEmptyBoardIsDueTest,
	"Cataclysm.EmpireRun.TheSchedulerAnswersAnEmptyBoardOnlyOnceTheLeastIntervalHasPassed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunEmptyBoardIsDueTest::RunTest(const FString& Parameters)
{
	UCataclysmSurgeScheduler* Scheduler = NewObject<UCataclysmSurgeScheduler>();

	// BEFORE ANY SURGE THE START OF THE RUN IS DAY 0, which is what
	// `Simulation.days_since_last_surge` answers while its log is empty.
	TestEqual(TEXT("before any surge, day 24 is 24 days since the last one"),
			  Scheduler->DaysSinceLastSurge(24), 24.0f, 0.001f);
	TestFalse(TEXT("day 24 with nothing standing and no surge yet is not due"),
			  Scheduler->IsDueOnEmptyBoard(24, 0));
	TestTrue(TEXT("day 25 with nothing standing and no surge yet is due"),
			 Scheduler->IsDueOnEmptyBoard(25, 0));
	TestFalse(TEXT("day 25 with one dungeon standing is not due"),
			  Scheduler->IsDueOnEmptyBoard(25, 1));

	// A SURGE ON DAY 100 MOVES WHAT THE INTERVAL IS MEASURED FROM.
	Scheduler->RecordSurge(100);

	TestEqual(TEXT("a surge recorded on day 100 is the last surge's day"),
			  Scheduler->LastSurgeDay, 100);
	TestEqual(TEXT("day 124 is 24 days since it"),
			  Scheduler->DaysSinceLastSurge(124), 24.0f, 0.001f);
	TestFalse(TEXT("day 124 with nothing standing is not due"),
			  Scheduler->IsDueOnEmptyBoard(124, 0));
	TestTrue(TEXT("day 125 with nothing standing is due"),
			 Scheduler->IsDueOnEmptyBoard(125, 0));
	TestFalse(TEXT("day 1000 with three dungeons standing is not due"),
			  Scheduler->IsDueOnEmptyBoard(1000, 3));

	// AND A SURGE A CITY'S FALL FIRED MOVES IT TOO. The model logs every surge
	// in one list whatever caused it.
	Scheduler->RecordSurge(110, /* bFromCityFall */ true);

	TestFalse(TEXT("after a fall's surge on day 110, day 134 is not due"),
			  Scheduler->IsDueOnEmptyBoard(134, 0));
	TestTrue(TEXT("and day 135 is due"),
			 Scheduler->IsDueOnEmptyBoard(135, 0));

	return true;
}

/**
 * The player clears the last dungeon and the next wave lands the next day.
 *
 * TWO RUNS FROM ONE SEED, ALIKE UNTIL DAY 30. Each has the opening wave of day
 * 1 taken off and one hand-placed ordinary dungeon on the first Outpost, with a
 * timer that never runs out. On day 30 one run clears that dungeon and the
 * other does not. Nothing else differs, so the other run is the control.
 *
 * THE DAYS, BY HAND. The last surge was day 1 and the clock's next is day 91.
 * A clear made after day 30 is seen at the end of day 31, which is 30 days
 * after the last surge and past the least interval of 25, so the wave lands on
 * day 31 -- 60 days before the clock would have sent it.
 *
 * NO ACTOR IS SPAWNED. A run is an object with no world.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunEmptyBoardSurgeTest,
	"Cataclysm.EmpireRun.ClearingTheLastDungeonBringsTheNextSurgeTheNextDay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunEmptyBoardSurgeTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	const int32 LastOne = 9200;

	UCataclysmEmpireRun* Cleared = MakeEmptyRun(1);
	UCataclysmEmpireRun* Standing = MakeEmptyRun(1);

	for (UCataclysmEmpireRun* Run : { Cleared, Standing })
	{
		const int32 CityId = FirstStandingOutpost(*Run);
		if (!TestTrue(TEXT("set-up: the map has an Outpost standing"),
					  CityId != INDEX_NONE))
		{
			return false;
		}

		PlaceDungeon(*Run, LastOne, ECataclysmDungeonType::Basic,
					 ECataclysmDungeonSubType::None, *Run->Map->Find(CityId),
					 EmptyBoardNeverDays);

		TestEqual(TEXT("set-up: the opening wave fired on day 1"),
				  Run->Surges->LastSurgeDay, 1);
		TestEqual(TEXT("set-up: the clock's next surge is day 91"),
				  Run->Surges->NextSurgeDay, 91.0f, 0.001f);
		TestEqual(TEXT("set-up: no surge on days 2 to 30 while one dungeon stands"),
				  SurgesWhileAdvancingTo(*Run, 30), 0);
		TestEqual(TEXT("set-up: one dungeon stands on day 30"),
				  Run->DungeonCount(), 1);
	}

	if (!TestTrue(TEXT("the player clears the last dungeon on day 30"),
				  Cleared->ClearDungeon(LastOne)))
	{
		return false;
	}

	TestEqual(TEXT("set-up: the board is empty after the clear"),
			  Cleared->DungeonCount(), 0);
	TestEqual(TEXT("the clear itself fires nothing"),
			  Cleared->Surges->SurgesFired, 1);

	const FCataclysmDayReport After = Cleared->AdvanceDay();
	const FCataclysmDayReport Control = Standing->AdvanceDay();

	TestEqual(TEXT("the day after the clear is day 31"), After.Day, 31);
	TestTrue(TEXT("day 31 surged on the emptied board"), After.bSurged);
	TestTrue(TEXT("and the report says the empty board caused it"),
			 After.bSurgedOnEmptyBoard);
	// THE RUN'S SECOND WAVE, AT `SurgeIndex` 1, SO `DungeonsPerSurge`: 8 since
	// 2026-10-20, and 5 until then. The board was empty, so all of it stands.
	TestEqual(TEXT("the wave brought eight dungeons"), After.Spawned.Num(), 8);
	TestEqual(TEXT("eight dungeons stand after it"), Cleared->DungeonCount(), 8);
	TestEqual(TEXT("the run counts one surge from an empty board"),
			  Cleared->SurgesFromEmptyBoard, 1);
	TestEqual(TEXT("and two surges in all"), Cleared->Surges->SurgesFired, 2);

	TestFalse(TEXT("CONTROL: with the dungeon still standing day 31 does not surge"),
			  Control.bSurged);
	TestFalse(TEXT("CONTROL: nor report an empty board"),
			  Control.bSurgedOnEmptyBoard);
	TestEqual(TEXT("CONTROL: nothing arrived"), Control.Spawned.Num(), 0);
	TestEqual(TEXT("CONTROL: one dungeon stands"), Standing->DungeonCount(), 1);
	TestEqual(TEXT("CONTROL: no surge is counted from an empty board"),
			  Standing->SurgesFromEmptyBoard, 0);
	TestEqual(TEXT("CONTROL: one surge in all"),
			  Standing->Surges->SurgesFired, 1);

	// A WAVE THE EMPTY BOARD BRINGS DOES NOT LOSE A DAY ON THE DAY IT LANDS.
	// The model asks below its timer loop, so its dungeons are first counted
	// down the day after. A wave the clock sends loses a day at once, which
	// `Cataclysm.EmpireRun.ARunBeginsWithAnIntactEmpireAndASurgeDue` holds.
	for (const int32 DungeonId : After.Spawned)
	{
		const FCataclysmDungeon* Dungeon = Cleared->FindDungeon(DungeonId);
		if (!TestNotNull(TEXT("a dungeon of the wave is on the map"), Dungeon))
		{
			return false;
		}

		TestEqual(FString::Printf(TEXT("dungeon %d arrived on day 31"), DungeonId),
				  Dungeon->SpawnedDay, 31);
		TestEqual(FString::Printf(
					  TEXT("dungeon %d has its whole timer left on day 31"),
					  DungeonId),
				  Cleared->Clock->DaysUntilResolveFor(DungeonId),
				  Dungeon->ResolveDays, 0.001f);
	}

	return true;
}

/**
 * A board emptied inside the least interval waits for it, and is not forgotten.
 *
 * THE DAYS, BY HAND. The opening wave fires on day 1 and is taken off the same
 * day, so the board is empty from day 1 and the last surge is day 1. Days 2 to
 * 25 are 1 to 24 days after it, inside the least interval of 25. Day 26 is the
 * 25th. The board emptied once, on day 1, and nothing empties it again, so a
 * rule asked only at the moment of emptying would never fire.
 *
 * THE CONTROL IS THE 24 DAYS THEMSELVES: the same empty board, one day short.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunEmptyBoardWaitsTest,
	"Cataclysm.EmpireRun.ABoardEmptiedInsideTheLeastIntervalWaitsUntilItHasPassed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunEmptyBoardWaitsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	UCataclysmEmpireRun* Run = MakeEmptyRun(1);

	TestEqual(TEXT("set-up: the last surge was day 1"),
			  Run->Surges->LastSurgeDay, 1);
	TestEqual(TEXT("set-up: nothing stands on day 1"), Run->DungeonCount(), 0);

	TestEqual(TEXT("no surge on any of days 2 to 25, the 24 days inside the least interval"),
			  SurgesWhileAdvancingTo(*Run, 25), 0);
	TestEqual(TEXT("set-up: the clock reads day 25"), Run->Day(), 25);
	TestEqual(TEXT("and the board was still empty on day 25"),
			  Run->DungeonCount(), 0);
	TestEqual(TEXT("and no surge is counted from an empty board yet"),
			  Run->SurgesFromEmptyBoard, 0);

	const FCataclysmDayReport Report = Run->AdvanceDay();

	TestEqual(TEXT("the next day is day 26"), Report.Day, 26);
	TestTrue(TEXT("day 26, the 25th after the last surge, surges on the empty board"),
			 Report.bSurgedOnEmptyBoard);
	// THE RUN'S SECOND WAVE, so 8 since 2026-10-20 and 5 until then.
	TestEqual(TEXT("bringing eight dungeons"), Report.Spawned.Num(), 8);
	TestEqual(TEXT("and the run counts one surge from an empty board"),
			  Run->SurgesFromEmptyBoard, 1);

	return true;
}

/**
 * The clock after an empty-board surge, and a whole run against days written by
 * hand.
 *
 * `Simulation.trigger_surge` sets `next_surge_day` to today plus the gap for
 * every surge whatever caused it, and an empty-board surge advances the
 * escalation counter as the clock's does. So one wave replaces one wave.
 *
 * THE RUN, BY HAND, static surges of 90 days and a least interval of 25:
 *
 *   day 1    the opening wave, from the clock. Taken off at once. Clock: 91.
 *   day 26   25 days later the empty board brings a wave. Clock moves to 116.
 *            The player clears all of it the same day.
 *   day 51   25 days later again. Clock moves to 141. The player clears all of
 *            it and one Quest dungeon that never moves is put down.
 *   day 141  the clock's surge. Nothing came on days 52 to 140, though the
 *            least interval passed on day 76, because a dungeon stood. Clock
 *            moves to 231.
 *
 * WHY THE WAVES ARE CLEARED THE DAY THEY LAND. A rolled wave left standing
 * resolves and can fell a city, and a city's fall fires a surge of its own on a
 * day no hand can write down in advance.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunEmptyBoardClockTest,
	"Cataclysm.EmpireRun.AnEmptyBoardSurgeMovesTheClockOnAndTheClockStillComes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunEmptyBoardClockTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	UCataclysmEmpireRun* Run = MakeEmptyRun(1);

	TestEqual(TEXT("set-up: before any empty-board surge the clock's next is day 91"),
			  Run->Surges->NextSurgeDay, 91.0f, 0.001f);
	TestEqual(TEXT("set-up: one surge has advanced the escalation counter"),
			  Run->Surges->SurgeIndex, 1);

	// DAY 26.
	TestEqual(TEXT("one surge on days 2 to 26"),
			  SurgesWhileAdvancingTo(*Run, 26), 1);
	TestEqual(TEXT("and it was the empty board's, on day 26"),
			  Run->Surges->LastSurgeDay, 26);
	TestEqual(TEXT("the clock's next surge moved from day 91 to day 116"),
			  Run->Surges->NextSurgeDay, 116.0f, 0.001f);
	TestEqual(TEXT("the escalation counter advanced to 2"),
			  Run->Surges->SurgeIndex, 2);
	TestEqual(TEXT("one surge from an empty board so far"),
			  Run->SurgesFromEmptyBoard, 1);

	ClearTheWholeBoard(*Run);
	TestEqual(TEXT("set-up: the player cleared the whole wave on day 26"),
			  Run->DungeonCount(), 0);

	// DAYS 27 TO 50 ARE INSIDE THE LEAST INTERVAL AGAIN, AND DAY 51 IS NOT.
	TestEqual(TEXT("no surge on days 27 to 50"),
			  SurgesWhileAdvancingTo(*Run, 50), 0);

	const FCataclysmDayReport Second = Run->AdvanceDay();

	TestEqual(TEXT("the next day is day 51"), Second.Day, 51);
	TestTrue(TEXT("day 51 surges on the empty board, 25 days after day 26"),
			 Second.bSurgedOnEmptyBoard);
	TestEqual(TEXT("the clock's next surge moved to day 141"),
			  Run->Surges->NextSurgeDay, 141.0f, 0.001f);
	TestEqual(TEXT("two surges from an empty board"),
			  Run->SurgesFromEmptyBoard, 2);

	// ONE QUEST DUNGEON THAT NEVER LEAVES, AND NOTHING ELSE.
	ClearTheWholeBoard(*Run);

	const int32 CityId = FirstStandingOutpost(*Run);
	if (!TestTrue(TEXT("set-up: the map has an Outpost standing on day 51"),
				  CityId != INDEX_NONE))
	{
		return false;
	}

	PlaceDungeon(*Run, 9300, ECataclysmDungeonType::Quest,
				 ECataclysmDungeonSubType::None, *Run->Map->Find(CityId),
				 EmptyBoardNeverDays);

	TestEqual(TEXT("no surge on days 52 to 140 while the Quest dungeon stands"),
			  SurgesWhileAdvancingTo(*Run, 140), 0);
	TestEqual(TEXT("set-up: the Quest dungeon is the one thing standing on day 140"),
			  Run->DungeonCount(), 1);

	const FCataclysmDayReport Scheduled = Run->AdvanceDay();

	TestEqual(TEXT("the next day is day 141"), Scheduled.Day, 141);
	TestTrue(TEXT("day 141 surges, from the clock"), Scheduled.bSurged);
	TestFalse(TEXT("and not from an empty board"),
			  Scheduled.bSurgedOnEmptyBoard);
	// THE RUN'S FOURTH WAVE, AT `SurgeIndex` 3 IN A STATIC RUN, so 8 since
	// 2026-10-20 and 5 until then.
	TestEqual(TEXT("the clock's wave brought eight dungeons"),
			  Scheduled.Spawned.Num(), 8);
	TestEqual(TEXT("still two surges from an empty board"),
			  Run->SurgesFromEmptyBoard, 2);
	TestEqual(TEXT("four surges in all: days 1, 26, 51 and 141"),
			  Run->Surges->SurgesFired, 4);
	TestEqual(TEXT("and the clock's next surge is day 231"),
			  Run->Surges->NextSurgeDay, 231.0f, 0.001f);

	return true;
}

/**
 * Every kind of dungeon keeps the board from being empty.
 *
 * `Simulation.board_is_empty` is `not self.dungeons` and reads no kind, and
 * `test_every_kind_of_dungeon_counts_as_something_on_the_board` in
 * `sim/tests/test_surge_when_the_board_empties.py` holds it for all four. A
 * Quest dungeon and a Fallen City never leave the board on their own, so a
 * player who ignores one gets the clock's surges and no others.
 *
 * FIVE RUNS FROM ONE SEED TO DAY 40. Four have one dungeon of one kind on the
 * first Outpost with a timer that never runs out; the fifth has nothing and is
 * the control. The last surge was day 1, so day 26 is where an empty board
 * fires.
 *
 * DAY 40 AND NO LATER, for the control's sake. Its wave of day 26 stays
 * standing, and by day 40 it is 14 days old: the shortest timer a rolled
 * dungeon can have is over 19 days and a Siege takes 25 days to fell an
 * Outpost, so no city can have fallen and fired a surge of its own.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunEmptyBoardKindsTest,
	"Cataclysm.EmpireRun.EveryKindOfDungeonKeepsTheBoardFromBeingEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunEmptyBoardKindsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	const ECataclysmDungeonType Kinds[4] = {
		ECataclysmDungeonType::Basic, ECataclysmDungeonType::Quest,
		ECataclysmDungeonType::FallenCity, ECataclysmDungeonType::Cataclysm };
	const TCHAR* const KindNames[4] = {
		TEXT("an ordinary dungeon"), TEXT("a Quest dungeon"),
		TEXT("a Fallen City"), TEXT("a Cataclysm dungeon") };

	for (int32 Index = 0; Index < 4; ++Index)
	{
		UCataclysmEmpireRun* Run = MakeEmptyRun(1);

		const int32 CityId = FirstStandingOutpost(*Run);
		if (!TestTrue(TEXT("set-up: the map has an Outpost standing"),
					  CityId != INDEX_NONE))
		{
			return false;
		}

		PlaceDungeon(*Run, 9400, Kinds[Index], ECataclysmDungeonSubType::None,
					 *Run->Map->Find(CityId), EmptyBoardNeverDays);

		TestEqual(FString::Printf(
					  TEXT("%s standing: no surge on days 2 to 40"),
					  KindNames[Index]),
				  SurgesWhileAdvancingTo(*Run, 40), 0);
		TestEqual(FString::Printf(
					  TEXT("%s standing: no surge counted from an empty board"),
					  KindNames[Index]),
				  Run->SurgesFromEmptyBoard, 0);
		TestEqual(FString::Printf(
					  TEXT("%s standing: it is still the one thing on the board"),
					  KindNames[Index]),
				  Run->DungeonCount(), 1);
	}

	UCataclysmEmpireRun* Empty = MakeEmptyRun(1);

	TestEqual(TEXT("CONTROL: with nothing standing, one surge on days 2 to 40"),
			  SurgesWhileAdvancingTo(*Empty, 40), 1);
	TestEqual(TEXT("CONTROL: and it was the empty board's"),
			  Empty->SurgesFromEmptyBoard, 1);
	TestEqual(TEXT("CONTROL: on day 26"), Empty->Surges->LastSurgeDay, 26);

	return true;
}

/**
 * Once the Pillar can be reached an empty board fires nothing.
 *
 * WHY. In the model a reachable Pillar opens the Last Stand, which is a dungeon
 * standing on the board, so its board is never empty from that day and its
 * rule never fires. The Last Stand is not built in the game (issue #43), so
 * `AdvanceDay` asks the condition directly. That is a judgement by the writing
 * session and `docs/DECISIONS.md` says so.
 *
 * TWO RUNS FROM ONE SEED TO DAY 40, both with nothing standing. In one,
 * Sanctuaries are felled by hand through the map until the Pillar is exposed;
 * felling through the map and not the run leaves no Fallen City dungeon and
 * fires no surge, so the board stays empty. The other is the control.
 *
 * THE RIM STILL STANDS, so a wave would have had somewhere to land; that is
 * asserted as set-up, or "no surge" could mean "no target".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunEmptyBoardPillarTest,
	"Cataclysm.EmpireRun.AnEmptyBoardFiresNothingOnceThePillarCanBeReached",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunEmptyBoardPillarTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	UCataclysmEmpireRun* Lost = MakeEmptyRun(1);
	UCataclysmEmpireRun* Intact = MakeEmptyRun(1);

	TArray<int32> Sanctuaries;
	for (const FCataclysmCity& City : Lost->Map->Cities)
	{
		if (City.Tier == ECataclysmCityTier::Sanctuary)
		{
			Sanctuaries.Add(City.CityId);
		}
	}

	for (const int32 CityId : Sanctuaries)
	{
		if (!Lost->Map->IsPillarExposed())
		{
			Lost->Map->Fall(CityId);
		}
	}

	if (!TestTrue(TEXT("set-up: felling Sanctuaries exposed the Pillar"),
				  Lost->Map->IsPillarExposed()))
	{
		return false;
	}

	TestTrue(TEXT("set-up: a wave would still have a city to land on"),
			 Lost->Map->ExposedCities().Num() > 0);
	TestEqual(TEXT("set-up: nothing stands on the lost run's board"),
			  Lost->DungeonCount(), 0);
	TestFalse(TEXT("set-up: the control's Pillar is not exposed"),
			  Intact->Map->IsPillarExposed());

	TestEqual(TEXT("with the Pillar exposed, no surge on days 2 to 40"),
			  SurgesWhileAdvancingTo(*Lost, 40), 0);
	TestEqual(TEXT("and no surge counted from an empty board"),
			  Lost->SurgesFromEmptyBoard, 0);
	TestEqual(TEXT("and the board is still empty on day 40"),
			  Lost->DungeonCount(), 0);

	TestEqual(TEXT("CONTROL: with the Pillar shielded, one surge on days 2 to 40"),
			  SurgesWhileAdvancingTo(*Intact, 40), 1);
	TestEqual(TEXT("CONTROL: and it was the empty board's"),
			  Intact->SurgesFromEmptyBoard, 1);

	return true;
}

// ---------------------------------------------------------------------------
// A city's fall and the least interval -- issue #2380
// ---------------------------------------------------------------------------
//
// NO ACTOR IS SPAWNED IN ANY TEST BELOW. A run is an object with no world, so
// nothing stands anywhere and nothing waits: every day is a call.
//
// EVERY FALL IS MADE BY HAND, on a run whose opening wave has been taken off.
// A campaign's falls come on days no hand can write down, and the whole of
// this rule is a count of days.

namespace CataclysmEmpireRunTest
{
	/**
	 * Points of defence five times an Outpost's whole 1000, so a dungeon
	 * carrying them fells a full Outpost when it resolves whatever `BiteScale`
	 * makes of them. `Cataclysm.CityUpgrade.ClearingADungeonRepairsTheCityBut
	 * AbsorbingOneDoesNot` fells its city with the same figure.
	 */
	const float FallSurgeFellingPoints = 5000.0f;

	/**
	 * The lowest dungeon number these tests place by hand. A surge numbers its
	 * dungeons upwards from nought and no run here lands more than a few dozen
	 * -- the opening wave of 12 and a wave of 8 or two after it, since
	 * 2026-10-20 -- so a dungeon numbered below this that is not a Dungeon
	 * City was rolled by a surge.
	 */
	const int32 FallSurgeFirstHandId = 9500;

	/** The first `Count` Outposts that have not fallen, in the map's order. */
	TArray<int32> StandingOutposts(const UCataclysmEmpireRun& Run, int32 Count)
	{
		TArray<int32> Found;

		for (const FCataclysmCity& City : Run.Map->Cities)
		{
			if (Found.Num() < Count
				&& City.Tier == ECataclysmCityTier::Outpost && !City.bFallen)
			{
				Found.Add(City.CityId);
			}
		}

		return Found;
	}

	/**
	 * Puts two ordinary dungeons on one city, neither with a timer that runs
	 * out on its own.
	 *
	 * THE FELLER carries `FallSurgeFellingPoints`, so the city falls on the day
	 * it resolves; `ResolveOnTheNextDay` chooses that day. THE BYSTANDER takes
	 * nothing. It is there so the city falls carrying two dungeons and not
	 * one, which is what its Dungeon City's boss count is read against.
	 *
	 * NOTHING BITES UNTIL THE FELLER RESOLVES. Neither is a Siege, and an
	 * ordinary dungeon costs its city nothing until its timer runs out.
	 */
	void PlaceAFellerAndABystander(UCataclysmEmpireRun& Run, int32 CityId,
								   int32 FellerId, int32 BystanderId)
	{
		const FCataclysmCity* Host = Run.Map->Find(CityId);
		if (Host == nullptr)
		{
			return;
		}

		PlaceDungeon(Run, FellerId, ECataclysmDungeonType::Basic,
					 ECataclysmDungeonSubType::None, *Host, EmptyBoardNeverDays);
		PlaceDungeon(Run, BystanderId, ECataclysmDungeonType::Basic,
					 ECataclysmDungeonSubType::None, *Host, EmptyBoardNeverDays);

		GiveDamageAndDaysLeft(Run, FellerId, FallSurgeFellingPoints, 0.0f,
							  EmptyBoardNeverDays);
	}

	/** Leaves one day on a dungeon's timer, so the next day advanced resolves it. */
	void ResolveOnTheNextDay(UCataclysmEmpireRun& Run, int32 DungeonId)
	{
		for (FCataclysmDungeonTimer& Counting : Run.Clock->Timers)
		{
			if (Counting.DungeonId == DungeonId)
			{
				Counting.DaysUntilResolve = 1.0f;
			}
		}
	}

	/** The Dungeon City standing on a city, or null when none does. */
	const FCataclysmDungeon* DungeonCityOn(const UCataclysmEmpireRun& Run,
										   int32 CityId)
	{
		for (const FCataclysmDungeon& Standing : Run.Dungeons)
		{
			if (Standing.Type == ECataclysmDungeonType::FallenCity
				&& Standing.CityId == CityId)
			{
				return &Standing;
			}
		}

		return nullptr;
	}

	/**
	 * Clears every dungeon a surge rolled, as a player who beat the wave the
	 * day it landed would, and answers how many that was.
	 *
	 * A DUNGEON CITY AND A HAND-PLACED DUNGEON ARE LEFT. Clearing a Dungeon
	 * City retakes its city, which is not wanted here.
	 *
	 * WHY A WAVE IS CLEARED AT ALL. A rolled wave left standing can hold a
	 * Siege, which bites every day, and its timers run out from about day 19.
	 * Either can fell a city on a day no hand can write down.
	 */
	int32 ClearWhatASurgeRolled(UCataclysmEmpireRun& Run)
	{
		TArray<int32> Rolled;

		for (const FCataclysmDungeon& Standing : Run.Dungeons)
		{
			if (Standing.Type != ECataclysmDungeonType::FallenCity
				&& Standing.DungeonId < FallSurgeFirstHandId)
			{
				Rolled.Add(Standing.DungeonId);
			}
		}

		int32 Cleared = 0;

		for (const int32 DungeonId : Rolled)
		{
			Cleared += Run.ClearDungeon(DungeonId) ? 1 : 0;
		}

		return Cleared;
	}

	/** What one hand-made fall left, read straight after the day it fell on. */
	struct FFallSurgeOutcome
	{
		FCataclysmDayReport Report;
		int32 CityId = INDEX_NONE;
		int32 FellerId = INDEX_NONE;
		int32 BystanderId = INDEX_NONE;
		int32 SurgesFiredBefore = 0;
		int32 SurgesFiredAfter = 0;
		int32 SurgeIndexBefore = 0;
		int32 SurgeIndexAfter = 0;
		int32 LastSurgeDayAfter = 0;
		float NextSurgeDayAfter = 0.0f;
		int32 DungeonsStandingAfter = 0;
		int32 DungeonCityBosses = INDEX_NONE;
		float FellerTimerAfter = 0.0f;
		float BystanderTimerAfter = 0.0f;
		bool bCityFell = false;
		bool bFellerStands = true;
		bool bBystanderStands = true;
		bool bListsAgree = false;
		int32 SurgesInTheFourDaysAfter = 0;
	};

	/**
	 * One run from seed 1 in which the first Outpost falls on `FallDay`.
	 *
	 * THE DAYS, BY HAND. The opening wave fires on day 1 and is taken off the
	 * same day, so the last surge is day 1 and the clock's next is day 91. Two
	 * dungeons are put on the first Outpost on day 1. On days 2 to
	 * `FallDay - 1` nothing resolves and nothing bites; the board is not empty,
	 * so the empty-board rule fires nothing. That is asserted as set-up. The
	 * feller is then given one day and the next day advanced is `FallDay`: it
	 * resolves, and the city falls `FallDay - 1` days after the last surge.
	 *
	 * AND FOUR DAYS MORE, counted, for the test that a refused fall is not
	 * tried again. In four days no city can fall: a wave that did land is at
	 * most four days old.
	 *
	 * @return false when the set-up did not hold, having said so.
	 */
	bool FellTheFirstOutpostOn(FAutomationTestBase& Test, int32 FallDay,
							   FFallSurgeOutcome& Out)
	{
		Out.FellerId = FallSurgeFirstHandId;
		Out.BystanderId = FallSurgeFirstHandId + 1;

		UCataclysmEmpireRun* Run = MakeEmptyRun(1);

		const TArray<int32> Outposts = StandingOutposts(*Run, 1);
		if (!Test.TestEqual(TEXT("set-up: the map has an Outpost standing"),
							Outposts.Num(), 1))
		{
			return false;
		}

		Out.CityId = Outposts[0];
		PlaceAFellerAndABystander(*Run, Out.CityId, Out.FellerId,
								  Out.BystanderId);

		if (!Test.TestEqual(TEXT("set-up: the opening wave fired on day 1"),
							Run->Surges->LastSurgeDay, 1)
			|| !Test.TestEqual(FString::Printf(
								   TEXT("set-up: no surge on days 2 to %d"),
								   FallDay - 1),
							   SurgesWhileAdvancingTo(*Run, FallDay - 1), 0)
			|| !Test.TestEqual(FString::Printf(
								   TEXT("set-up: on day %d the city carries its two dungeons"),
								   FallDay - 1),
							   Run->DungeonsOn(Out.CityId).Num(), 2)
			|| !Test.TestFalse(FString::Printf(
								   TEXT("set-up: and on day %d it has not fallen"),
								   FallDay - 1),
							   Run->Map->Find(Out.CityId)->bFallen))
		{
			return false;
		}

		Out.SurgesFiredBefore = Run->Surges->SurgesFired;
		Out.SurgeIndexBefore = Run->Surges->SurgeIndex;

		ResolveOnTheNextDay(*Run, Out.FellerId);
		Out.Report = Run->AdvanceDay();

		Out.SurgesFiredAfter = Run->Surges->SurgesFired;
		Out.SurgeIndexAfter = Run->Surges->SurgeIndex;
		Out.LastSurgeDayAfter = Run->Surges->LastSurgeDay;
		Out.NextSurgeDayAfter = Run->Surges->NextSurgeDay;
		Out.DungeonsStandingAfter = Run->DungeonCount();

		const FCataclysmCity* Fell = Run->Map->Find(Out.CityId);
		Out.bCityFell = Fell != nullptr && Fell->bFallen;

		const FCataclysmDungeon* Became = DungeonCityOn(*Run, Out.CityId);
		Out.DungeonCityBosses = Became != nullptr ? Became->Bosses : INDEX_NONE;

		Out.bFellerStands = Run->FindDungeon(Out.FellerId) != nullptr;
		Out.bBystanderStands = Run->FindDungeon(Out.BystanderId) != nullptr;
		Out.FellerTimerAfter = Run->Clock->DaysUntilResolveFor(Out.FellerId);
		Out.BystanderTimerAfter =
			Run->Clock->DaysUntilResolveFor(Out.BystanderId);

		FString WhyNot;
		Out.bListsAgree = UCataclysmEmpireRun::DungeonsAgreeWithTimers(
			Run->Dungeons, Run->Clock->Timers, WhyNot);

		Out.SurgesInTheFourDaysAfter =
			SurgesWhileAdvancingTo(*Run, FallDay + 4);

		return true;
	}
}

/**
 * The scheduler's own answer about a city's fall, with no run and no map.
 *
 * `IsDueOnCityFall` is two conditions: the rule is on, and the last surge is at
 * least `LeastIntervalDays` (25) behind. Each row below differs from the row
 * beside it by one day, so each is the control for its neighbour.
 *
 * THE FLAG IS A CONSTANT and cannot be turned off from a test. Its control is
 * the row at 1000 days: that far past the interval the flag is the only thing
 * that can refuse, so the answer there has to be the flag's own value.
 * `tools/tests/test_surge_port.py` holds the flag equal to the model's.
 *
 * NO ACTOR AND NO WORLD. Every figure is a day number written by hand.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunFallSurgeIsDueTest,
	"Cataclysm.EmpireRun.TheSchedulerAnswersACitysFallOnlyOnceTheLeastIntervalHasPassed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunFallSurgeIsDueTest::RunTest(const FString& Parameters)
{
	UCataclysmSurgeScheduler* Scheduler = NewObject<UCataclysmSurgeScheduler>();

	// BEFORE ANY SURGE THE START OF THE RUN IS DAY 0.
	TestEqual(TEXT("before any surge, day 24 is 24 days since the last one"),
			  Scheduler->DaysSinceLastSurge(24), 24.0f, 0.001f);
	TestFalse(TEXT("a city falling on day 24 with no surge yet fires none"),
			  Scheduler->IsDueOnCityFall(24));
	TestTrue(TEXT("a city falling on day 25 with no surge yet fires one"),
			 Scheduler->IsDueOnCityFall(25));

	// THE FLAG'S CONTROL. See the comment above this test.
	TestTrue(TEXT("a city falling 1000 days after the last surge fires one exactly when the flag says a fall does"),
			 Scheduler->IsDueOnCityFall(1000)
				 == UCataclysmSurgeScheduler::bSurgeOnCityFall);

	// A FALL DOES NOT ASK WHAT STANDS ON THE BOARD. The empty-board rule on
	// the same day, with one dungeon standing, is the control.
	TestFalse(TEXT("CONTROL: the empty-board rule on day 25 with one dungeon standing is not due"),
			  Scheduler->IsDueOnEmptyBoard(25, 1));

	// A SURGE ON DAY 100 MOVES WHAT THE INTERVAL IS MEASURED FROM.
	Scheduler->RecordSurge(100);

	TestFalse(TEXT("a second city falling on day 100, the day of a surge, fires none"),
			  Scheduler->IsDueOnCityFall(100));
	TestFalse(TEXT("after a surge on day 100, a fall on day 124 fires none"),
			  Scheduler->IsDueOnCityFall(124));
	TestTrue(TEXT("and a fall on day 125 fires one"),
			 Scheduler->IsDueOnCityFall(125));

	// AND A SURGE A FALL FIRED MOVES IT TOO.
	Scheduler->RecordSurge(110, /* bFromCityFall */ true);

	TestFalse(TEXT("after a fall's surge on day 110, a fall on day 134 fires none"),
			  Scheduler->IsDueOnCityFall(134));
	TestTrue(TEXT("and a fall on day 135 fires one"),
			 Scheduler->IsDueOnCityFall(135));

	return true;
}

/**
 * A city that falls inside the least interval still falls, and brings no wave.
 *
 * TWO RUNS FROM ONE SEED, A DAY APART. In one the first Outpost falls on day
 * 25, which is 24 days after the opening wave of day 1. In the other it falls
 * on day 26, the 25th day. Nothing else differs, so the second is the control:
 * it shows the same fall does bring a wave one day later.
 *
 * WHAT THE FALL STILL DOES. The city is down, the two dungeons it carried are
 * absorbed and off the clock, and a Dungeon City with two bosses stands on it.
 * Both runs are asserted to agree on all of that.
 *
 * A REFUSED FALL IS NOT TRIED AGAIN. Day 26 is the 25th day after the last
 * surge and the city fell the day before. Nothing fires on days 26 to 29.
 *
 * WHERE EVERYTHING STANDS: nowhere. See the note at the head of this section.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunFallSurgeInsideTest,
	"Cataclysm.EmpireRun.ACityFallingInsideTheLeastIntervalStillFallsAndBringsNoWave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunFallSurgeInsideTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	FFallSurgeOutcome Inside;
	FFallSurgeOutcome AtTheInterval;

	if (!FellTheFirstOutpostOn(*this, 25, Inside)
		|| !FellTheFirstOutpostOn(*this, 26, AtTheInterval))
	{
		return false;
	}

	// THE FALL ON DAY 25 FIRED NOTHING.
	TestEqual(TEXT("the city fell on day 25"), Inside.Report.Day, 25);
	TestFalse(TEXT("day 25, 24 days after the last surge, did not surge"),
			  Inside.Report.bSurged);
	TestEqual(TEXT("no surge was recorded for that fall"),
			  Inside.SurgesFiredAfter, Inside.SurgesFiredBefore);
	TestEqual(TEXT("the escalation counter did not move"),
			  Inside.SurgeIndexAfter, Inside.SurgeIndexBefore);
	TestEqual(TEXT("the last surge is still day 1"),
			  Inside.LastSurgeDayAfter, 1);
	TestEqual(TEXT("the clock's next surge is still day 91"),
			  Inside.NextSurgeDayAfter, 91.0f, 0.001f);
	TestEqual(TEXT("one dungeon arrived on day 25, the Dungeon City"),
			  Inside.Report.Spawned.Num(), 1);
	TestEqual(TEXT("and it is the one dungeon standing"),
			  Inside.DungeonsStandingAfter, 1);

	// AND THE CITY STILL FELL, WITH EVERYTHING A FALL DOES.
	TestTrue(TEXT("the city has fallen"), Inside.bCityFell);
	TestEqual(TEXT("the report names one fallen city"),
			  Inside.Report.Fallen.Num(), 1);
	TestTrue(TEXT("and it is this one"),
			 Inside.Report.Fallen.Contains(Inside.CityId));
	TestEqual(TEXT("a Dungeon City stands on it with one boss for each of the two dungeons it carried"),
			  Inside.DungeonCityBosses, 2);
	TestEqual(TEXT("the report names two dungeons as absorbed"),
			  Inside.Report.Absorbed.Num(), 2);
	TestTrue(TEXT("the one that felled the city"),
			 Inside.Report.Absorbed.Contains(Inside.FellerId));
	TestTrue(TEXT("and the one beside it"),
			 Inside.Report.Absorbed.Contains(Inside.BystanderId));
	TestFalse(TEXT("the dungeon that felled the city is off the map"),
			  Inside.bFellerStands);
	TestFalse(TEXT("and so is the one beside it"), Inside.bBystanderStands);
	TestEqual(TEXT("no timer counts down for the dungeon that felled it"),
			  Inside.FellerTimerAfter, -1.0f, 0.001f);
	TestEqual(TEXT("nor for the one beside it"),
			  Inside.BystanderTimerAfter, -1.0f, 0.001f);
	TestTrue(TEXT("every dungeon still has one timer and every timer a dungeon"),
			 Inside.bListsAgree);

	// AND IT IS NOT TRIED AGAIN ONCE THE INTERVAL HAS PASSED.
	TestEqual(TEXT("no surge on days 26 to 29, though day 26 is the 25th after the last surge"),
			  Inside.SurgesInTheFourDaysAfter, 0);

	// THE CONTROL: THE SAME FALL ONE DAY LATER.
	TestEqual(TEXT("CONTROL: the city fell on day 26"),
			  AtTheInterval.Report.Day, 26);
	TestTrue(TEXT("CONTROL: day 26, 25 days after the last surge, surged"),
			 AtTheInterval.Report.bSurged);
	TestEqual(TEXT("CONTROL: one surge was recorded for that fall"),
			  AtTheInterval.SurgesFiredAfter,
			  AtTheInterval.SurgesFiredBefore + 1);
	// THE RUN'S SECOND WAVE, so 8 since 2026-10-20 and 5 until then.
	TestEqual(TEXT("CONTROL: nine dungeons arrived on day 26, a wave of eight and the Dungeon City"),
			  AtTheInterval.Report.Spawned.Num(), 9);
	TestTrue(TEXT("CONTROL: the city has fallen there too"),
			 AtTheInterval.bCityFell);
	TestEqual(TEXT("CONTROL: and its Dungeon City has the same two bosses"),
			  AtTheInterval.DungeonCityBosses, Inside.DungeonCityBosses);
	TestEqual(TEXT("CONTROL: and it absorbed the same two dungeons"),
			  AtTheInterval.Report.Absorbed.Num(),
			  Inside.Report.Absorbed.Num());

	return true;
}

/**
 * A city that falls 25 days after the last surge brings one wave.
 *
 * THE SAME TWO RUNS AS THE TEST ABOVE, READ FROM THE OTHER SIDE. The fall on
 * day 26 is the 25th day after the opening wave and fires; the fall on day 25
 * is the control.
 *
 * ONE WAVE, AS A FALL'S SURGE. It moves the clock's next surge on from the day
 * it fired, and it advances the escalation counter exactly when
 * `bCityFallAdvancesEscalation` says a fall's surge does. That flag is true
 * today, so the counter is the only thing that could tell a fall's surge from
 * the clock's, and while the flag is true it cannot. The report's
 * `bSurgedOnEmptyBoard` is read as well: the board is not empty on day 26, and
 * this wave must not be counted as the empty board's.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunFallSurgeAtIntervalTest,
	"Cataclysm.EmpireRun.ACityFallingTwentyFiveDaysAfterTheLastSurgeBringsOneWave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunFallSurgeAtIntervalTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	FFallSurgeOutcome Inside;
	FFallSurgeOutcome AtTheInterval;

	if (!FellTheFirstOutpostOn(*this, 25, Inside)
		|| !FellTheFirstOutpostOn(*this, 26, AtTheInterval))
	{
		return false;
	}

	const int32 EscalatedBy =
		UCataclysmSurgeScheduler::bCityFallAdvancesEscalation ? 1 : 0;

	TestEqual(TEXT("the city fell on day 26"), AtTheInterval.Report.Day, 26);
	TestTrue(TEXT("the city has fallen"), AtTheInterval.bCityFell);
	TestTrue(TEXT("the report names it as fallen"),
			 AtTheInterval.Report.Fallen.Contains(AtTheInterval.CityId));
	TestTrue(TEXT("day 26, 25 days after the last surge, surged"),
			 AtTheInterval.Report.bSurged);
	TestFalse(TEXT("and not as an empty board's surge"),
			  AtTheInterval.Report.bSurgedOnEmptyBoard);
	TestEqual(TEXT("the fall fired one surge and not two"),
			  AtTheInterval.SurgesFiredAfter - AtTheInterval.SurgesFiredBefore,
			  1);
	TestEqual(TEXT("the escalation counter advanced exactly as the flag says a fall's surge does"),
			  AtTheInterval.SurgeIndexAfter - AtTheInterval.SurgeIndexBefore,
			  EscalatedBy);
	TestEqual(TEXT("the last surge is now day 26"),
			  AtTheInterval.LastSurgeDayAfter, 26);
	TestEqual(TEXT("the clock's next surge moved from day 91 to day 116"),
			  AtTheInterval.NextSurgeDayAfter, 116.0f, 0.001f);
	// THE RUN'S SECOND WAVE, at `SurgeIndex` 1, so 8 since 2026-10-20 and 5
	// until then. The wave lands after the fall, so none of it lands on the
	// fallen city and all of it stands beside the Dungeon City.
	TestEqual(TEXT("nine dungeons arrived on day 26, a wave of eight and the Dungeon City"),
			  AtTheInterval.Report.Spawned.Num(), 9);
	TestEqual(TEXT("and nine stand after it"),
			  AtTheInterval.DungeonsStandingAfter, 9);
	TestEqual(TEXT("the Dungeon City has one boss for each of the two dungeons the city carried"),
			  AtTheInterval.DungeonCityBosses, 2);
	TestEqual(TEXT("no further surge on days 27 to 30"),
			  AtTheInterval.SurgesInTheFourDaysAfter, 0);

	// THE CONTROL: THE SAME FALL ONE DAY SOONER.
	TestEqual(TEXT("CONTROL: the city fell on day 25"), Inside.Report.Day, 25);
	TestFalse(TEXT("CONTROL: day 25, 24 days after the last surge, did not surge"),
			  Inside.Report.bSurged);
	TestEqual(TEXT("CONTROL: no surge was recorded for that fall"),
			  Inside.SurgesFiredAfter - Inside.SurgesFiredBefore, 0);
	TestEqual(TEXT("CONTROL: the escalation counter did not move"),
			  Inside.SurgeIndexAfter - Inside.SurgeIndexBefore, 0);
	TestEqual(TEXT("CONTROL: the clock's next surge stayed at day 91"),
			  Inside.NextSurgeDayAfter, 91.0f, 0.001f);
	TestEqual(TEXT("CONTROL: one dungeon arrived on day 25, the Dungeon City"),
			  Inside.Report.Spawned.Num(), 1);

	return true;
}

/**
 * Two cities falling on one day bring one wave, not two.
 *
 * TWO RUNS FROM ONE SEED, ALIKE UNTIL DAY 25. Each has the opening wave taken
 * off and a feller and a bystander on each of the first two Outposts.
 *
 *   SameDay    both fellers resolve on day 26. The first city falls 25 days
 *              after the last surge and its fall fires a wave, which is
 *              recorded as day 26. The second falls nought days after that
 *              and fires nothing.
 *   DaysApart  the control. The first city falls on day 26 and fires a wave,
 *              which the player clears that day. The second falls on day 51,
 *              25 days after it, and fires a second.
 *
 * So the same two falls bring one wave on one day and two waves 25 days apart.
 *
 * WHICH CITY FALLS FIRST ON DAY 26 is the order the timers were added in: the
 * first Outpost's feller, then the second's. The assertions do not depend on
 * it; either order gives one wave.
 *
 * A DUNGEON OF THE WAVE MAY LAND ON THE SECOND CITY before it falls, and is
 * then absorbed with it. So the wave is counted from the report's `Spawned`,
 * which lists what arrived, and not from what is left standing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunFallSurgeTwoCitiesTest,
	"Cataclysm.EmpireRun.TwoCitiesFallingOnOneDayBringOneWave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunFallSurgeTwoCitiesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	const int32 FirstFeller = FallSurgeFirstHandId + 10;
	const int32 FirstBystander = FallSurgeFirstHandId + 11;
	const int32 SecondFeller = FallSurgeFirstHandId + 12;
	const int32 SecondBystander = FallSurgeFirstHandId + 13;

	const int32 EscalatedBy =
		UCataclysmSurgeScheduler::bCityFallAdvancesEscalation ? 1 : 0;

	UCataclysmEmpireRun* SameDay = MakeEmptyRun(1);
	UCataclysmEmpireRun* DaysApart = MakeEmptyRun(1);

	int32 FirstCity = INDEX_NONE;
	int32 SecondCity = INDEX_NONE;

	for (UCataclysmEmpireRun* Run : { SameDay, DaysApart })
	{
		const TArray<int32> Outposts = StandingOutposts(*Run, 2);
		if (!TestEqual(TEXT("set-up: the map has two Outposts standing"),
					   Outposts.Num(), 2))
		{
			return false;
		}

		FirstCity = Outposts[0];
		SecondCity = Outposts[1];

		PlaceAFellerAndABystander(*Run, FirstCity, FirstFeller, FirstBystander);
		PlaceAFellerAndABystander(*Run, SecondCity, SecondFeller,
								  SecondBystander);

		if (!TestEqual(TEXT("set-up: the opening wave fired on day 1"),
					   Run->Surges->LastSurgeDay, 1)
			|| !TestEqual(TEXT("set-up: no surge on days 2 to 25"),
						  SurgesWhileAdvancingTo(*Run, 25), 0)
			|| !TestEqual(TEXT("set-up: one surge has fired by day 25"),
						  Run->Surges->SurgesFired, 1)
			|| !TestEqual(TEXT("set-up: the four hand-placed dungeons stand on day 25"),
						  Run->DungeonCount(), 4))
		{
			return false;
		}
	}

	const int32 IndexOnDay25 = SameDay->Surges->SurgeIndex;

	// -- BOTH CITIES FALL ON DAY 26 ----------------------------------------
	ResolveOnTheNextDay(*SameDay, FirstFeller);
	ResolveOnTheNextDay(*SameDay, SecondFeller);

	const FCataclysmDayReport Both = SameDay->AdvanceDay();

	TestEqual(TEXT("the day both fellers resolved is day 26"), Both.Day, 26);
	TestEqual(TEXT("two cities fell on day 26"), Both.Fallen.Num(), 2);
	TestTrue(TEXT("the first Outpost is one of them"),
			 Both.Fallen.Contains(FirstCity));
	TestTrue(TEXT("and the second Outpost is the other"),
			 Both.Fallen.Contains(SecondCity));
	TestNotNull(TEXT("a Dungeon City stands on the first"),
				DungeonCityOn(*SameDay, FirstCity));
	TestNotNull(TEXT("and one on the second"),
				DungeonCityOn(*SameDay, SecondCity));
	TestTrue(TEXT("day 26 surged"), Both.bSurged);
	TestEqual(TEXT("two cities falling on day 26 fired one surge and not two"),
			  SameDay->Surges->SurgesFired, 2);
	TestEqual(TEXT("the escalation counter advanced for one fall's surge and not two"),
			  SameDay->Surges->SurgeIndex - IndexOnDay25, EscalatedBy);
	// THE RUN'S SECOND WAVE, so 8 since 2026-10-20 and 5 until then.
	TestEqual(TEXT("ten dungeons arrived on day 26: one wave of eight and the two Dungeon Cities"),
			  Both.Spawned.Num(), 10);
	TestEqual(TEXT("the last surge is day 26"),
			  SameDay->Surges->LastSurgeDay, 26);
	TestEqual(TEXT("and the clock's next surge is day 116"),
			  SameDay->Surges->NextSurgeDay, 116.0f, 0.001f);

	// -- THE CONTROL: THE SAME TWO FALLS, 25 DAYS APART --------------------
	ResolveOnTheNextDay(*DaysApart, FirstFeller);

	const FCataclysmDayReport FirstFall = DaysApart->AdvanceDay();

	if (!TestEqual(TEXT("CONTROL: one city fell on day 26"),
				   FirstFall.Fallen.Num(), 1)
		|| !TestEqual(TEXT("CONTROL: and its fall fired a surge, the second of the run"),
					  DaysApart->Surges->SurgesFired, 2)
		|| !TestEqual(TEXT("CONTROL: set-up: the player cleared the eight dungeons of that wave on day 26"),
					  ClearWhatASurgeRolled(*DaysApart), 8)
		|| !TestEqual(TEXT("CONTROL: set-up: no surge on days 27 to 50"),
					  SurgesWhileAdvancingTo(*DaysApart, 50), 0))
	{
		return false;
	}

	ResolveOnTheNextDay(*DaysApart, SecondFeller);

	const FCataclysmDayReport SecondFall = DaysApart->AdvanceDay();

	TestEqual(TEXT("CONTROL: the second city fell on day 51"),
			  SecondFall.Day, 51);
	TestEqual(TEXT("CONTROL: one city fell that day"),
			  SecondFall.Fallen.Num(), 1);
	TestTrue(TEXT("CONTROL: and it is the second Outpost"),
			 SecondFall.Fallen.Contains(SecondCity));
	TestTrue(TEXT("CONTROL: day 51, 25 days after the surge of day 26, surged"),
			 SecondFall.bSurged);
	TestEqual(TEXT("CONTROL: the same two falls 25 days apart fired two surges, three in all"),
			  DaysApart->Surges->SurgesFired, 3);
	// THE RUN'S THIRD WAVE, at `SurgeIndex` 2 in a static run, so 8 since
	// 2026-10-20 and 5 until then.
	TestEqual(TEXT("CONTROL: nine dungeons arrived on day 51, a wave of eight and the Dungeon City"),
			  SecondFall.Spawned.Num(), 9);

	return true;
}

// ---------------------------------------------------------------------------
// An ordinary dungeon leaves after it hits, and a Quest dungeon's timer hits
// ---------------------------------------------------------------------------
//
// The two rules of 2026-10-13, `docs/DECISIONS.md`. The owner, 2026-10-08,
// verbatim: "Ordinary dungeons will do their city/population damage, and then
// despawn. Quest dungeons will do their city/population damage, and either
// refresh their timer and stay on that city, or potentially move to an adjacent
// city."
//
// EVERY TEST BELOW IS HEADLESS: a run, a map and a clock. No world and no
// actor, so nothing stands anywhere.
//
// EVERY ONE STARTS FROM `MakeEmptyRun`, so the last surge was day 1, the day
// advanced is day 2 and no city's fall or empty board can bring a wave: the
// least interval of 25 days has not passed.

namespace CataclysmEmpireRunTest
{
	/** The first Bulwark of the map that has not fallen, or `INDEX_NONE`. */
	int32 FirstStandingBulwark(const UCataclysmEmpireRun& Run)
	{
		for (const FCataclysmCity& City : Run.Map->Cities)
		{
			if (City.Tier == ECataclysmCityTier::Bulwark && !City.bFallen)
			{
				return City.CityId;
			}
		}
		return INDEX_NONE;
	}

	/** Every city's defence added up. */
	float DefenceOfTheWholeMap(const UCataclysmEmpireRun& Run)
	{
		float Total = 0.0f;
		for (const FCataclysmCity& City : Run.Map->Cities)
		{
			Total += City.Defence;
		}
		return Total;
	}

	/** Every city's population added up. */
	float PopulationOfTheWholeMap(const UCataclysmEmpireRun& Run)
	{
		float Total = 0.0f;
		for (const FCataclysmCity& City : Run.Map->Cities)
		{
			Total += City.Population;
		}
		return Total;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunHitsOnceAndLeavesTest,
	"Cataclysm.EmpireRun.AnOrdinaryDungeonHitsItsCityOnceAndIsGone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunHitsOnceAndLeavesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	const int32 OrdinaryId = 9701;
	const int32 QuestId = 9702;
	const int32 DungeonCityId = 9703;
	const float FullTimer = 30.0f;

	UCataclysmEmpireRun* Run = MakeEmptyRun(1);

	// THREE OUTPOSTS, ONE DUNGEON ON EACH, EVERY TIMER WITH ONE DAY LEFT. The
	// ordinary dungeon is the case. The Quest dungeon and the dungeon of the
	// Fallen City kind are the controls: their timers run out on the same day
	// and they stay. The Fallen City kind is put on a standing city by hand so
	// that a mistaken hit from it would show on that city.
	const TArray<int32> Outposts = StandingOutposts(*Run, 3);
	if (!TestEqual(TEXT("set-up: the map has three Outposts standing"),
				   Outposts.Num(), 3))
	{
		return false;
	}

	PlaceDungeon(*Run, OrdinaryId, ECataclysmDungeonType::Basic,
				 ECataclysmDungeonSubType::None, *Run->Map->Find(Outposts[0]),
				 FullTimer);
	GiveDamageAndDaysLeft(*Run, OrdinaryId, 100.0f, 40.0f, 1.0f);

	PlaceDungeon(*Run, QuestId, ECataclysmDungeonType::Quest,
				 ECataclysmDungeonSubType::None, *Run->Map->Find(Outposts[1]),
				 FullTimer);
	GiveDamageAndDaysLeft(*Run, QuestId, 0.0f, 0.0f, 1.0f);

	PlaceDungeon(*Run, DungeonCityId, ECataclysmDungeonType::FallenCity,
				 ECataclysmDungeonSubType::None, *Run->Map->Find(Outposts[2]),
				 FullTimer);
	GiveDamageAndDaysLeft(*Run, DungeonCityId, 100.0f, 40.0f, 1.0f);

	const FCataclysmDungeon* Placed = Run->FindDungeon(OrdinaryId);
	const FCataclysmCity* HitCity = Run->Map->Find(Outposts[0]);
	const FCataclysmCity* DungeonCityHost = Run->Map->Find(Outposts[2]);
	if (!TestNotNull(TEXT("set-up: the ordinary dungeon is on the map"), Placed)
		|| !TestNotNull(TEXT("set-up: and so is its city"), HitCity)
		|| !TestNotNull(TEXT("set-up: and the third Outpost"), DungeonCityHost))
	{
		return false;
	}

	// WHAT ONE HIT TAKES, worked out the way `ResolveDungeon` does.
	const float OneHit = Placed->DefenceDamage * Placed->BiteScale();
	const float DefenceBefore = HitCity->Defence;
	const float ThirdDefenceBefore = DungeonCityHost->Defence;

	if (!TestTrue(TEXT("set-up: one hit is a real amount the city can pay several times"),
				  OneHit > 1.0f && DefenceBefore > 3.0f * OneHit)
		|| !TestEqual(TEXT("set-up: no ordinary dungeon has hit a city yet"),
					  Run->DungeonsDetonated, 0))
	{
		return false;
	}

	const FCataclysmDayReport Report = Run->AdvanceDay();

	TestEqual(TEXT("set-up: the day advanced is day 2"), Report.Day, 2);
	TestFalse(TEXT("set-up: and it brought no wave"), Report.bSurged);
	TestTrue(TEXT("the ordinary dungeon's timer ran out"),
			 Report.Resolved.Contains(OrdinaryId));

	// IT HIT, ONCE.
	TestEqual(TEXT("its city lost one hit's defence"),
			  DefenceBefore - HitCity->Defence, OneHit, 0.01f);
	TestEqual(TEXT("one ordinary dungeon has hit a city"),
			  Run->DungeonsDetonated, 1);

	// AND IT IS GONE.
	TestNull(TEXT("the ordinary dungeon is off the map"),
			 Run->FindDungeon(OrdinaryId));
	TestNull(TEXT("and off the clock"), Run->Clock->FindTimer(OrdinaryId));
	TestEqual(TEXT("the report names one city as hit by a dungeon that left"),
			  Report.HitAndLeft.Num(), 1);
	TestTrue(TEXT("and it is that dungeon's city"),
			 Report.HitAndLeft.Contains(Outposts[0]));

	// IT LEFT; NOBODY BEAT IT AND NO CITY ABSORBED IT.
	TestFalse(TEXT("the report does not name it as absorbed"),
			  Report.Absorbed.Contains(OrdinaryId));
	TestEqual(TEXT("no city fell"), Report.Fallen.Num(), 0);
	TestEqual(TEXT("no dungeon is counted as cleared"), Run->DungeonsCleared, 0);
	TestEqual(TEXT("nor an ordinary one"), Run->BasicDungeonsCleared, 0);

	// THE CONTROLS: THE SAME DAY, THE SAME ONE DAY LEFT, AND THEY STAY.
	TestTrue(TEXT("CONTROL: the Quest dungeon's timer ran out the same day"),
			 Report.Resolved.Contains(QuestId));
	TestNotNull(TEXT("CONTROL: the Quest dungeon is still on the map"),
				Run->FindDungeon(QuestId));
	TestEqual(TEXT("CONTROL: and its timer is full again"),
			  Run->Clock->DaysUntilResolveFor(QuestId), FullTimer, 0.001f);
	TestTrue(TEXT("CONTROL: the Dungeon City's timer ran out the same day"),
			 Report.Resolved.Contains(DungeonCityId));
	TestNotNull(TEXT("CONTROL: the Dungeon City is still on the map"),
				Run->FindDungeon(DungeonCityId));
	TestEqual(TEXT("CONTROL: and its timer is full again"),
			  Run->Clock->DaysUntilResolveFor(DungeonCityId), FullTimer, 0.001f);
	TestEqual(TEXT("CONTROL: the Dungeon City took nothing from the city it was put on"),
			  DungeonCityHost->Defence, ThirdDefenceBefore, 0.001f);
	TestEqual(TEXT("CONTROL: two dungeons stand where three did"),
			  Run->DungeonCount(), 2);

	// AND NOTHING MORE LANDS ON THAT CITY. Five more days; the two timers that
	// were refilled have thirty days and cannot run out in them.
	Run->AdvanceDays(5);

	TestEqual(TEXT("five days later the city has lost nothing more"),
			  DefenceBefore - HitCity->Defence, OneHit, 0.01f);
	TestEqual(TEXT("and still one ordinary dungeon has hit a city"),
			  Run->DungeonsDetonated, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunFellerIsAbsorbedTest,
	"Cataclysm.EmpireRun.TheDungeonWhoseHitFellsItsCityIsAbsorbedAndNotReportedAsLeft",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunFellerIsAbsorbedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	const int32 FellerId = 9711;
	const int32 BystanderId = 9712;

	// TWO RUNS FROM ONE SEED. In each the first Outpost carries two ordinary
	// dungeons and one of them resolves on day 2. In `Felled` its hit is 5,000
	// points, five times an Outpost's whole defence. In `Stood`, the control,
	// the same dungeon takes 100 before scaling and the city stands.
	UCataclysmEmpireRun* Felled = MakeEmptyRun(1);
	UCataclysmEmpireRun* Stood = MakeEmptyRun(1);

	const int32 CityId = FirstStandingOutpost(*Felled);
	if (!TestTrue(TEXT("set-up: the map has an Outpost standing"),
				  CityId != INDEX_NONE)
		|| !TestEqual(TEXT("set-up: and it is the same Outpost in the control"),
					  FirstStandingOutpost(*Stood), CityId))
	{
		return false;
	}

	PlaceAFellerAndABystander(*Felled, CityId, FellerId, BystanderId);
	ResolveOnTheNextDay(*Felled, FellerId);

	PlaceAFellerAndABystander(*Stood, CityId, FellerId, BystanderId);
	GiveDamageAndDaysLeft(*Stood, FellerId, 100.0f, 0.0f, 1.0f);

	const FCataclysmDayReport FellReport = Felled->AdvanceDay();
	const FCataclysmDayReport StoodReport = Stood->AdvanceDay();

	// THE FALL ABSORBED IT.
	TestTrue(TEXT("the city fell"), FellReport.Fallen.Contains(CityId));
	TestTrue(TEXT("the dungeon that felled it is named as absorbed"),
			 FellReport.Absorbed.Contains(FellerId));
	TestTrue(TEXT("and so is the one beside it"),
			 FellReport.Absorbed.Contains(BystanderId));
	TestNull(TEXT("the dungeon that felled it is off the map"),
			 Felled->FindDungeon(FellerId));
	TestEqual(TEXT("the report names no city as hit by a dungeon that left"),
			  FellReport.HitAndLeft.Num(), 0);
	TestEqual(TEXT("and the run keeps no last hit for the empire screen"),
			  Felled->LastHitDay, 0);
	TestEqual(TEXT("its hit is still counted as one ordinary dungeon hitting a city"),
			  Felled->DungeonsDetonated, 1);
	TestNotNull(TEXT("a Dungeon City stands on the fallen city"),
				DungeonCityOn(*Felled, CityId));

	// THE CONTROL: THE SAME DUNGEON WITH A HIT THE CITY SURVIVES LEAVES.
	TestEqual(TEXT("CONTROL: no city fell"), StoodReport.Fallen.Num(), 0);
	TestEqual(TEXT("CONTROL: the report names one city as hit by a dungeon that left"),
			  StoodReport.HitAndLeft.Num(), 1);
	TestTrue(TEXT("CONTROL: and it is this city"),
			 StoodReport.HitAndLeft.Contains(CityId));
	TestFalse(TEXT("CONTROL: the dungeon is not named as absorbed"),
			  StoodReport.Absorbed.Contains(FellerId));
	TestNull(TEXT("CONTROL: it is off the map all the same"),
			 Stood->FindDungeon(FellerId));
	TestNotNull(TEXT("CONTROL: the dungeon beside it still stands"),
				Stood->FindDungeon(BystanderId));
	TestEqual(TEXT("CONTROL: and the run keeps day 2 as the last hit"),
			  Stood->LastHitDay, 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunQuestHitRowTest,
	"Cataclysm.EmpireRun.AQuestDungeonsTimerTakesTheOrdinaryRowOfTheCityItStandsOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunQuestHitRowTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	const int32 OnTheBulwarkId = 9721;
	const int32 OnTheOutpostId = 9722;

	UCataclysmEmpireRun* Run = MakeEmptyRun(1);

	const int32 OutpostId = FirstStandingOutpost(*Run);
	const int32 BulwarkId = FirstStandingBulwark(*Run);
	if (!TestTrue(TEXT("set-up: the map has an Outpost standing"),
				  OutpostId != INDEX_NONE)
		|| !TestTrue(TEXT("set-up: and a Bulwark"), BulwarkId != INDEX_NONE))
	{
		return false;
	}

	// THE TWO ROWS, READ FROM THE TABLE AND NOT TYPED.
	const FCataclysmDungeonSpec OutpostRow = UCataclysmSurgeScheduler::SpecFor(
		ECataclysmDungeonType::Basic, ECataclysmCityTier::Outpost);
	const FCataclysmDungeonSpec BulwarkRow = UCataclysmSurgeScheduler::SpecFor(
		ECataclysmDungeonType::Basic, ECataclysmCityTier::Bulwark);

	// TWO QUEST DUNGEONS, BOTH ROLLED ON AN OUTPOST. `PlaceDungeon` writes the
	// tier of the city it is given into `CityTier`, so both carry Outpost
	// there. One is then moved by hand to stand on a Bulwark, as a relocation
	// leaves a dungeon: `CityId` moves and `CityTier` does not. The other stays
	// on the Outpost and is the control.
	PlaceDungeon(*Run, OnTheBulwarkId, ECataclysmDungeonType::Quest,
				 ECataclysmDungeonSubType::None, *Run->Map->Find(OutpostId), 25.0f);
	PlaceDungeon(*Run, OnTheOutpostId, ECataclysmDungeonType::Quest,
				 ECataclysmDungeonSubType::None, *Run->Map->Find(OutpostId), 25.0f);

	for (FCataclysmDungeon& Standing : Run->Dungeons)
	{
		if (Standing.DungeonId == OnTheBulwarkId)
		{
			Standing.CityId = BulwarkId;
		}
	}

	ResolveOnTheNextDay(*Run, OnTheBulwarkId);
	ResolveOnTheNextDay(*Run, OnTheOutpostId);

	const FCataclysmDungeon* Moved = Run->FindDungeon(OnTheBulwarkId);
	const FCataclysmCity* Outpost = Run->Map->Find(OutpostId);
	const FCataclysmCity* Bulwark = Run->Map->Find(BulwarkId);
	if (!TestNotNull(TEXT("set-up: the moved Quest dungeon is on the map"), Moved)
		|| !TestNotNull(TEXT("set-up: the Outpost is on the map"), Outpost)
		|| !TestNotNull(TEXT("set-up: the Bulwark is on the map"), Bulwark))
	{
		return false;
	}

	const float OutpostDefenceBefore = Outpost->Defence;
	const float OutpostPopulationBefore = Outpost->Population;
	const float BulwarkDefenceBefore = Bulwark->Defence;
	const float BulwarkPopulationBefore = Bulwark->Population;

	if (!TestEqual(TEXT("set-up: the moved dungeon stands on the Bulwark"),
				   Moved->CityId, BulwarkId)
		|| !TestTrue(TEXT("set-up: and still carries the Outpost tier its depth was rolled on"),
					 Moved->CityTier == ECataclysmCityTier::Outpost)
		|| !TestTrue(TEXT("set-up: the two ordinary rows take something and differ in defence and in population"),
					 OutpostRow.DefenceDamage > 0.0f
						 && OutpostRow.PopulationDamage > 0.0f
						 && BulwarkRow.DefenceDamage != OutpostRow.DefenceDamage
						 && BulwarkRow.PopulationDamage != OutpostRow.PopulationDamage)
		|| !TestTrue(TEXT("set-up: each city can pay its row and stand"),
					 OutpostDefenceBefore > OutpostRow.DefenceDamage
						 && OutpostPopulationBefore > OutpostRow.PopulationDamage
						 && BulwarkDefenceBefore > BulwarkRow.DefenceDamage
						 && BulwarkPopulationBefore > BulwarkRow.PopulationDamage)
		|| !TestTrue(TEXT("set-up: the Quest dungeon's own row carries no damage"),
					 Moved->DefenceDamage == 0.0f && Moved->PopulationDamage == 0.0f))
	{
		return false;
	}

	const FCataclysmDayReport Report = Run->AdvanceDay();

	TestTrue(TEXT("set-up: both Quest timers ran out on day 2"),
			 Report.Resolved.Contains(OnTheBulwarkId)
				 && Report.Resolved.Contains(OnTheOutpostId));
	TestEqual(TEXT("set-up: no city fell"), Report.Fallen.Num(), 0);

	// THE BULWARK PAID THE BULWARK'S ROW, THOUGH THE DUNGEON WAS ROLLED ON AN
	// OUTPOST, AND AT A SCALE OF ONE WHATEVER THE DUNGEON'S TWENTY FLOORS.
	TestEqual(TEXT("the Bulwark lost exactly the defence of the ordinary row of a Bulwark"),
			  BulwarkDefenceBefore - Bulwark->Defence,
			  BulwarkRow.DefenceDamage, 0.01f);
	TestEqual(TEXT("and exactly that row's population"),
			  BulwarkPopulationBefore - Bulwark->Population,
			  BulwarkRow.PopulationDamage, 0.01f);

	// THE CONTROL: THE SAME KIND OF DUNGEON ON AN OUTPOST TAKES THE OUTPOST'S.
	TestEqual(TEXT("CONTROL: the Outpost lost exactly the defence of the ordinary row of an Outpost"),
			  OutpostDefenceBefore - Outpost->Defence,
			  OutpostRow.DefenceDamage, 0.01f);
	TestEqual(TEXT("CONTROL: and exactly that row's population"),
			  OutpostPopulationBefore - Outpost->Population,
			  OutpostRow.PopulationDamage, 0.01f);

	// AND EACH DUNGEON IS STILL ON THE MAP WITH A FULL TIMER. Where each stands
	// now is not asserted: a Quest dungeon may move after it hits.
	TestNotNull(TEXT("the Quest dungeon that hit the Bulwark is still on the map"),
				Run->FindDungeon(OnTheBulwarkId));
	TestEqual(TEXT("and its timer is full again"),
			  Run->Clock->DaysUntilResolveFor(OnTheBulwarkId), 25.0f, 0.001f);
	TestNotNull(TEXT("CONTROL: and so is the one that hit the Outpost"),
				Run->FindDungeon(OnTheOutpostId));

	return true;
}

/**
 * A Quest dungeon whose own hit fells its city moves to a neighbour, with its
 * timer full, and the dungeon beside it is absorbed.
 *
 * UNTIL 2026-10-21 THIS TEST HELD THE OPPOSITE, under the name
 * `AQuestHitThatFellsItsCityAbsorbsTheQuestDungeonWithNoRefreshAndNoMove`: the
 * fall absorbed the Quest dungeon, so it had no timer and was not in
 * `Relocated`. The owner, 2026-10-16, verbatim: "Quest dungeons don't go away
 * when a city falls. They just move to an adjacent city." The simulation's
 * `_resolve` refreshes such a dungeon and gives it no second move that day,
 * and that is what is asserted here: it is in `Relocated` once.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunQuestHitFellsTest,
	"Cataclysm.EmpireRun.AQuestHitThatFellsItsCityMovesTheQuestDungeonWithItsTimerFull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunQuestHitFellsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	const int32 QuestId = 9731;
	const int32 BystanderId = 9732;

	// TWO RUNS FROM ONE SEED. In each a Quest dungeon with one day left stands
	// on the first Outpost beside an ordinary dungeon whose timer never runs
	// out. In `Felled` the Outpost is first taken down to half of what an
	// ordinary hit takes, so the Quest dungeon's hit fells it. In `Stood`, the
	// control, the Outpost is at full defence.
	UCataclysmEmpireRun* Felled = MakeEmptyRun(1);
	UCataclysmEmpireRun* Stood = MakeEmptyRun(1);

	const int32 CityId = FirstStandingOutpost(*Felled);
	if (!TestTrue(TEXT("set-up: the map has an Outpost standing"),
				  CityId != INDEX_NONE)
		|| !TestEqual(TEXT("set-up: and it is the same Outpost in the control"),
					  FirstStandingOutpost(*Stood), CityId))
	{
		return false;
	}

	const FCataclysmDungeonSpec OutpostRow = UCataclysmSurgeScheduler::SpecFor(
		ECataclysmDungeonType::Basic, ECataclysmCityTier::Outpost);
	const float FullDefence = Felled->Map->Find(CityId)->Defence;

	if (!TestTrue(TEXT("set-up: an ordinary hit takes less than an Outpost's whole defence"),
				  OutpostRow.DefenceDamage > 1.0f
					  && FullDefence > 2.0f * OutpostRow.DefenceDamage))
	{
		return false;
	}

	for (UCataclysmEmpireRun* Run : { Felled, Stood })
	{
		PlaceDungeon(*Run, QuestId, ECataclysmDungeonType::Quest,
					 ECataclysmDungeonSubType::None, *Run->Map->Find(CityId),
					 25.0f);
		PlaceDungeon(*Run, BystanderId, ECataclysmDungeonType::Basic,
					 ECataclysmDungeonSubType::None, *Run->Map->Find(CityId),
					 EmptyBoardNeverDays);
		ResolveOnTheNextDay(*Run, QuestId);
	}

	// HALF AN ORDINARY HIT IS LEFT ON THE ONE CITY.
	const bool bFellEarly = Felled->Map->Damage(
		CityId, FullDefence - 0.5f * OutpostRow.DefenceDamage, 0.0f);
	if (!TestFalse(TEXT("set-up: taking the Outpost down did not fell it"),
				   bFellEarly)
		|| !TestEqual(TEXT("set-up: it has half an ordinary hit's defence left"),
					  Felled->Map->Find(CityId)->Defence,
					  0.5f * OutpostRow.DefenceDamage, 0.01f))
	{
		return false;
	}

	const FCataclysmDayReport FellReport = Felled->AdvanceDay();
	const FCataclysmDayReport StoodReport = Stood->AdvanceDay();

	// THE HIT FELLED THE CITY AND THE FALL MOVED THE QUEST DUNGEON. The
	// dungeon beside it, an ordinary one, was absorbed.
	TestTrue(TEXT("the Quest dungeon's timer ran out"),
			 FellReport.Resolved.Contains(QuestId));
	TestTrue(TEXT("the city fell to its hit"),
			 FellReport.Fallen.Contains(CityId));
	TestFalse(TEXT("the Quest dungeon is not named as absorbed"),
			  FellReport.Absorbed.Contains(QuestId));
	TestTrue(TEXT("and the dungeon beside it is"),
			 FellReport.Absorbed.Contains(BystanderId));

	const FCataclysmDungeon* Moved = Felled->FindDungeon(QuestId);
	if (TestNotNull(TEXT("the Quest dungeon is still on the map"), Moved))
	{
		const FCataclysmCity* Fallen = Felled->Map->Find(CityId);

		TestNotEqual(TEXT("and no longer on the city that fell"),
					 Moved->CityId, CityId);
		TestTrue(TEXT("it moved to a neighbour of the city that fell, which on an intact map always has one to take it"),
				 Fallen != nullptr
					 && UCataclysmSurgeScheduler::AdjacentCities(*Fallen)
							.Contains(Moved->CityId));
		TestTrue(TEXT("and that neighbour is exposed and standing"),
				 Felled->Map->IsExposed(Moved->CityId));
	}

	TestNotNull(TEXT("its timer is still on the clock"),
				Felled->Clock->FindTimer(QuestId));
	TestEqual(TEXT("and full: the clock refilled it when it ran out"),
			  Felled->Clock->DaysUntilResolveFor(QuestId), 25.0f, 0.001f);
	TestEqual(TEXT("the report names it as moved once, by the fall, and not by its timer as well"),
			  FellReport.Relocated.FilterByPredicate(
				  [QuestId](int32 Id) { return Id == QuestId; }).Num(), 1);
	TestTrue(TEXT("and names that move as a fall's"),
			 FellReport.MovedByAFall.Contains(QuestId));

	const FCataclysmDungeon* DungeonCity = DungeonCityOn(*Felled, CityId);
	if (TestNotNull(TEXT("a Dungeon City stands on the fallen city"), DungeonCity))
	{
		TestEqual(TEXT("and it holds one boss, for the one dungeon it absorbed"),
				  DungeonCity->Bosses, 1);
	}
	TestEqual(TEXT("the hit is counted as one Quest hit"), Felled->QuestHits, 1);

	// THE CONTROL: AT FULL DEFENCE THE SAME TIMER LEAVES THE DUNGEON STANDING.
	TestTrue(TEXT("CONTROL: the Quest dungeon's timer ran out"),
			 StoodReport.Resolved.Contains(QuestId));
	TestEqual(TEXT("CONTROL: no city fell"), StoodReport.Fallen.Num(), 0);
	TestEqual(TEXT("CONTROL: the Outpost lost one ordinary hit's defence"),
			  FullDefence - Stood->Map->Find(CityId)->Defence,
			  OutpostRow.DefenceDamage, 0.01f);
	TestNotNull(TEXT("CONTROL: the Quest dungeon is still on the map"),
				Stood->FindDungeon(QuestId));
	TestEqual(TEXT("CONTROL: and its timer is full again"),
			  Stood->Clock->DaysUntilResolveFor(QuestId), 25.0f, 0.001f);
	TestFalse(TEXT("CONTROL: and nothing was absorbed"),
			  StoodReport.Absorbed.Contains(QuestId));
	TestFalse(TEXT("CONTROL: and no fall moved it"),
			  StoodReport.MovedByAFall.Contains(QuestId));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunQuestOnAFallenCityTest,
	"Cataclysm.EmpireRun.AQuestDungeonOnAFallenCityHitsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunQuestOnAFallenCityTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	const int32 QuestId = 9741;

	// TWO RUNS FROM ONE SEED. In `OnAFallen` the first Outpost is felled through
	// the map before anything stands on it, and a Quest dungeon with one day
	// left is then put on it by hand. In `OnAStanding`, the control, the same
	// dungeon is put on the same Outpost standing.
	UCataclysmEmpireRun* OnAFallen = MakeEmptyRun(1);
	UCataclysmEmpireRun* OnAStanding = MakeEmptyRun(1);

	const int32 CityId = FirstStandingOutpost(*OnAFallen);
	if (!TestTrue(TEXT("set-up: the map has an Outpost standing"),
				  CityId != INDEX_NONE)
		|| !TestEqual(TEXT("set-up: and it is the same Outpost in the control"),
					  FirstStandingOutpost(*OnAStanding), CityId))
	{
		return false;
	}

	const bool bFell = OnAFallen->Map->Damage(CityId, 100000.0f, 0.0f);
	if (!TestTrue(TEXT("set-up: the Outpost was felled through the map"), bFell)
		|| !TestTrue(TEXT("set-up: and reads as fallen"),
					 OnAFallen->Map->Find(CityId)->bFallen))
	{
		return false;
	}

	for (UCataclysmEmpireRun* Run : { OnAFallen, OnAStanding })
	{
		PlaceDungeon(*Run, QuestId, ECataclysmDungeonType::Quest,
					 ECataclysmDungeonSubType::None, *Run->Map->Find(CityId),
					 25.0f);
		ResolveOnTheNextDay(*Run, QuestId);
	}

	const float DefenceBefore = DefenceOfTheWholeMap(*OnAFallen);
	const float PopulationBefore = PopulationOfTheWholeMap(*OnAFallen);
	const float ControlDefenceBefore = DefenceOfTheWholeMap(*OnAStanding);

	const FCataclysmDayReport Report = OnAFallen->AdvanceDay();
	const FCataclysmDayReport ControlReport = OnAStanding->AdvanceDay();

	TestTrue(TEXT("the Quest dungeon's timer ran out on the fallen city"),
			 Report.Resolved.Contains(QuestId));
	TestEqual(TEXT("the report names no city as hit by a Quest timer"),
			  Report.QuestHits.Num(), 0);
	TestEqual(TEXT("no Quest hit is counted"), OnAFallen->QuestHits, 0);
	TestEqual(TEXT("no city anywhere lost defence that day"),
			  DefenceOfTheWholeMap(*OnAFallen), DefenceBefore, 0.01f);
	TestEqual(TEXT("nor population"),
			  PopulationOfTheWholeMap(*OnAFallen), PopulationBefore, 0.01f);
	TestNotNull(TEXT("the Quest dungeon is still on the map"),
				OnAFallen->FindDungeon(QuestId));

	// THE CONTROL: ON THE SAME CITY STANDING, THE SAME TIMER HITS.
	TestEqual(TEXT("CONTROL: on the standing Outpost the report names one city as hit by a Quest timer"),
			  ControlReport.QuestHits.Num(), 1);
	TestEqual(TEXT("CONTROL: and one Quest hit is counted"),
			  OnAStanding->QuestHits, 1);
	TestTrue(TEXT("CONTROL: and the map lost defence that day"),
			 ControlDefenceBefore - DefenceOfTheWholeMap(*OnAStanding) > 1.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunQuestHitsCountedApartTest,
	"Cataclysm.EmpireRun.AQuestHitIsCountedInQuestHitsAndNotInDungeonsDetonated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunQuestHitsCountedApartTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	const int32 QuestId = 9751;
	const int32 OrdinaryId = 9752;

	UCataclysmEmpireRun* Run = MakeEmptyRun(1);

	// A QUEST DUNGEON ON ONE OUTPOST WITH ONE DAY LEFT, AND AN ORDINARY ONE ON
	// ANOTHER WITH TWO. So day 2 holds a Quest hit alone and day 3 an ordinary
	// hit alone, and the ordinary dungeon is the control on both counters.
	const TArray<int32> Outposts = StandingOutposts(*Run, 2);
	if (!TestEqual(TEXT("set-up: the map has two Outposts standing"),
				   Outposts.Num(), 2))
	{
		return false;
	}

	PlaceDungeon(*Run, QuestId, ECataclysmDungeonType::Quest,
				 ECataclysmDungeonSubType::None, *Run->Map->Find(Outposts[0]),
				 25.0f);
	ResolveOnTheNextDay(*Run, QuestId);

	PlaceDungeon(*Run, OrdinaryId, ECataclysmDungeonType::Basic,
				 ECataclysmDungeonSubType::None, *Run->Map->Find(Outposts[1]),
				 30.0f);
	GiveDamageAndDaysLeft(*Run, OrdinaryId, 100.0f, 40.0f, 2.0f);

	if (!TestEqual(TEXT("set-up: no Quest hit is counted before the day"),
				   Run->QuestHits, 0)
		|| !TestEqual(TEXT("set-up: nor an ordinary dungeon's"),
					  Run->DungeonsDetonated, 0))
	{
		return false;
	}

	const float FirstDefenceBefore = Run->Map->Find(Outposts[0])->Defence;

	const FCataclysmDayReport QuestDay = Run->AdvanceDay();

	TestTrue(TEXT("set-up: the Quest hit took defence from its city"),
			 FirstDefenceBefore - Run->Map->Find(Outposts[0])->Defence > 1.0f);
	TestEqual(TEXT("the Quest hit is counted in QuestHits"), Run->QuestHits, 1);
	TestEqual(TEXT("and not in DungeonsDetonated"), Run->DungeonsDetonated, 0);
	TestEqual(TEXT("the day's report names one city as hit by a Quest timer"),
			  QuestDay.QuestHits.Num(), 1);
	TestTrue(TEXT("and it is the Quest dungeon's city"),
			 QuestDay.QuestHits.Contains(Outposts[0]));
	TestEqual(TEXT("and names none as hit by a dungeon that left"),
			  QuestDay.HitAndLeft.Num(), 0);
	TestEqual(TEXT("the run keeps day 2 as the last hit"), Run->LastHitDay, 2);
	TestTrue(TEXT("and keeps it as a Quest timer's"), Run->bLastHitWasAQuestTimer);

	// THE CONTROL: AN ORDINARY DUNGEON'S HIT MOVES THE OTHER COUNTER.
	const FCataclysmDayReport OrdinaryDay = Run->AdvanceDay();

	TestEqual(TEXT("CONTROL: the ordinary hit is counted in DungeonsDetonated"),
			  Run->DungeonsDetonated, 1);
	TestEqual(TEXT("CONTROL: and QuestHits did not move"), Run->QuestHits, 1);
	TestEqual(TEXT("CONTROL: that day's report names no city as hit by a Quest timer"),
			  OrdinaryDay.QuestHits.Num(), 0);
	TestTrue(TEXT("CONTROL: and names the ordinary dungeon's city as hit by a dungeon that left"),
			 OrdinaryDay.HitAndLeft.Contains(Outposts[1]));
	TestEqual(TEXT("CONTROL: the run keeps day 3 as the last hit"),
			  Run->LastHitDay, 3);
	TestFalse(TEXT("CONTROL: and no longer as a Quest timer's"),
			  Run->bLastHitWasAQuestTimer);

	// AND `Describe` SAYS BOTH COUNTS.
	TestTrue(TEXT("Describe says one ordinary dungeon has hit a city and one Quest timer has"),
			 Run->Describe().Contains(
				 TEXT("1 ordinary dungeons have hit a city. 1 Quest dungeon "
					  "timers have hit one.")));

	return true;
}

/**
 * A Quest dungeon's hit on a city that bought the two resistances takes the reduced amount. Written 2026-10-16.
 *
 * WHERE THE REDUCTION IS. Not in `ResolveDungeon`, which hands the Outpost's ordinary row to `Map->Damage` whole,
 * but inside `UCataclysmEmpireMap::Damage`, which takes `ResistDefenceLoss` off the defence and
 * `ResistPopulationLoss` off the population. So a Quest hit is resisted as an ordinary dungeon's is, and this pins
 * it for the Quest hit, which came to exist on 2026-10-13.
 *
 * TWO RUNS FROM ONE SEED. In `Resisting` the first Outpost buys both resistances at a quarter each, the way
 * `Cataclysm.CityUpgrade.AResistantCityLosesLessToTheSameDungeon` buys them; in `Plain`, the control, it buys
 * nothing. In each a Quest dungeon with one day left stands on that Outpost.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunQuestHitResistedTest,
	"Cataclysm.EmpireRun.AQuestHitOnAResistingCityTakesTheReducedAmount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunQuestHitResistedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	const int32 QuestId = 9761;
	const float Quarter = 0.25f;

	UCataclysmEmpireRun* Resisting = MakeEmptyRun(1);
	UCataclysmEmpireRun* Plain = MakeEmptyRun(1);

	const int32 CityId = FirstStandingOutpost(*Resisting);
	if (!TestTrue(TEXT("set-up: the map has an Outpost standing"),
				  CityId != INDEX_NONE)
		|| !TestEqual(TEXT("set-up: and it is the same Outpost in the control"),
					  FirstStandingOutpost(*Plain), CityId))
	{
		return false;
	}

	FCataclysmCityUpgrade ResistDefence;
	ResistDefence.RowName = TEXT("ResistDef");
	ResistDefence.Effect = ECataclysmCityUpgradeEffect::ResistDefenceLoss;
	ResistDefence.Value = Quarter;

	FCataclysmCityUpgrade ResistPopulation;
	ResistPopulation.RowName = TEXT("ResistPop");
	ResistPopulation.Effect = ECataclysmCityUpgradeEffect::ResistPopulationLoss;
	ResistPopulation.Value = Quarter;

	if (!TestTrue(TEXT("set-up: the city bought resistance to defence loss"),
				  Resisting->BuyCityUpgrade(CityId, ResistDefence) == ECataclysmCityUpgradeResult::Bought)
		|| !TestTrue(TEXT("set-up: and to population loss"),
					 Resisting->BuyCityUpgrade(CityId, ResistPopulation) == ECataclysmCityUpgradeResult::Bought))
	{
		return false;
	}

	const FCataclysmCity* Resistant = Resisting->Map->Find(CityId);
	const FCataclysmCity* Unresisting = Plain->Map->Find(CityId);

	// THE ROW, READ FROM THE TABLE AND NOT TYPED.
	const FCataclysmDungeonSpec OutpostRow = UCataclysmSurgeScheduler::SpecFor(
		ECataclysmDungeonType::Basic, ECataclysmCityTier::Outpost);

	if (!TestTrue(TEXT("set-up: the city holds a quarter against each loss"),
				  FMath::IsNearlyEqual(Resistant->UpgradeValueFor(ECataclysmCityUpgradeEffect::ResistDefenceLoss),
									   Quarter)
					  && FMath::IsNearlyEqual(
						  Resistant->UpgradeValueFor(ECataclysmCityUpgradeEffect::ResistPopulationLoss), Quarter))
		|| !TestTrue(TEXT("set-up: the control's city holds nothing against either"),
					 Unresisting->UpgradeValueFor(ECataclysmCityUpgradeEffect::ResistDefenceLoss) == 0.0f
						 && Unresisting->UpgradeValueFor(ECataclysmCityUpgradeEffect::ResistPopulationLoss) == 0.0f)
		|| !TestTrue(TEXT("set-up: the ordinary row takes something and each city can pay it and stand"),
					 OutpostRow.DefenceDamage > 1.0f && OutpostRow.PopulationDamage > 1.0f
						 && Resistant->Defence > OutpostRow.DefenceDamage
						 && Unresisting->Defence > OutpostRow.DefenceDamage
						 && Resistant->Population > OutpostRow.PopulationDamage
						 && Unresisting->Population > OutpostRow.PopulationDamage))
	{
		return false;
	}

	for (UCataclysmEmpireRun* Run : { Resisting, Plain })
	{
		PlaceDungeon(*Run, QuestId, ECataclysmDungeonType::Quest,
					 ECataclysmDungeonSubType::None, *Run->Map->Find(CityId), 25.0f);
		ResolveOnTheNextDay(*Run, QuestId);
	}

	const float DefenceBefore = Resistant->Defence;
	const float PopulationBefore = Resistant->Population;
	const float ControlDefenceBefore = Unresisting->Defence;
	const float ControlPopulationBefore = Unresisting->Population;

	const FCataclysmDayReport Report = Resisting->AdvanceDay();
	const FCataclysmDayReport ControlReport = Plain->AdvanceDay();

	if (!TestTrue(TEXT("set-up: the Quest timer ran out on the resisting city"),
				  Report.Resolved.Contains(QuestId))
		|| !TestTrue(TEXT("set-up: and on the control's"), ControlReport.Resolved.Contains(QuestId)))
	{
		return false;
	}

	TestEqual(TEXT("no city fell"), Report.Fallen.Num(), 0);
	TestEqual(TEXT("the resisting city lost three quarters of the ordinary row's defence"),
			  DefenceBefore - Resistant->Defence, OutpostRow.DefenceDamage * (1.0f - Quarter), 0.01f);
	TestEqual(TEXT("and three quarters of its population"),
			  PopulationBefore - Resistant->Population, OutpostRow.PopulationDamage * (1.0f - Quarter), 0.01f);
	TestEqual(TEXT("one Quest hit is counted"), Resisting->QuestHits, 1);

	// THE CONTROL: THE SAME HIT ON THE SAME OUTPOST WITH NOTHING BOUGHT TAKES THE WHOLE ROW.
	TestEqual(TEXT("CONTROL: no city fell"), ControlReport.Fallen.Num(), 0);
	TestEqual(TEXT("CONTROL: the plain city lost the ordinary row's whole defence"),
			  ControlDefenceBefore - Unresisting->Defence, OutpostRow.DefenceDamage, 0.01f);
	TestEqual(TEXT("CONTROL: and its whole population"),
			  ControlPopulationBefore - Unresisting->Population, OutpostRow.PopulationDamage, 0.01f);
	TestEqual(TEXT("CONTROL: one Quest hit is counted"), Plain->QuestHits, 1);

	TestTrue(TEXT("the resisting city lost less defence than the control"),
			 DefenceBefore - Resistant->Defence < ControlDefenceBefore - Unresisting->Defence - 0.01f);

	return true;
}

/**
 * An ordinary dungeon whose timer runs out on a city that has already fallen hits nothing and stays. Written
 * 2026-10-16; the twin of `Cataclysm.EmpireRun.AQuestDungeonOnAFallenCityHitsNothing`.
 *
 * PLAY CANNOT REACH THIS STATE TODAY. A city falls only through `Map->Damage`; every call that can fell a city goes
 * on to `CityFell`, which removes every dungeon standing on it; a surge lands only on exposed cities, which are
 * standing ones; and a Quest dungeon's relocation skips cities that are not exposed. So no ordinary dungeon is ever
 * left on a fallen city. This test pins the guard in `ResolveDungeon` that says "THE DUNGEON STAYS: it did not hit"
 * for a future path that could, such as loading a save.
 *
 * TWO RUNS FROM ONE SEED. In `OnAFallen` the first Outpost is felled through the map before anything stands on it,
 * so no `CityFell` runs and nothing removes what is put there next; an ordinary dungeon with one day left is then
 * put on it by hand. In `OnAStanding`, the control, the same dungeon is put on the same Outpost standing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunOrdinaryOnAFallenCityTest,
	"Cataclysm.EmpireRun.AnOrdinaryDungeonOnAFallenCityHitsNothingAndStays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunOrdinaryOnAFallenCityTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	const int32 OrdinaryId = 9771;
	const float FullTimer = 30.0f;

	UCataclysmEmpireRun* OnAFallen = MakeEmptyRun(1);
	UCataclysmEmpireRun* OnAStanding = MakeEmptyRun(1);

	const int32 CityId = FirstStandingOutpost(*OnAFallen);
	if (!TestTrue(TEXT("set-up: the map has an Outpost standing"),
				  CityId != INDEX_NONE)
		|| !TestEqual(TEXT("set-up: and it is the same Outpost in the control"),
					  FirstStandingOutpost(*OnAStanding), CityId))
	{
		return false;
	}

	const bool bFell = OnAFallen->Map->Damage(CityId, 100000.0f, 0.0f);
	if (!TestTrue(TEXT("set-up: the Outpost was felled through the map"), bFell)
		|| !TestTrue(TEXT("set-up: and reads as fallen"),
					 OnAFallen->Map->Find(CityId)->bFallen))
	{
		return false;
	}

	for (UCataclysmEmpireRun* Run : { OnAFallen, OnAStanding })
	{
		PlaceDungeon(*Run, OrdinaryId, ECataclysmDungeonType::Basic,
					 ECataclysmDungeonSubType::None, *Run->Map->Find(CityId),
					 FullTimer);
		GiveDamageAndDaysLeft(*Run, OrdinaryId, 100.0f, 40.0f, 1.0f);
	}

	const FCataclysmCity* Fallen = OnAFallen->Map->Find(CityId);
	const float DefenceBefore = Fallen->Defence;
	const float PopulationBefore = Fallen->Population;
	const int32 DetonatedBefore = OnAFallen->DungeonsDetonated;
	const int32 LastHitDayBefore = OnAFallen->LastHitDay;
	const int32 ControlDetonatedBefore = OnAStanding->DungeonsDetonated;

	const FCataclysmDayReport Report = OnAFallen->AdvanceDay();
	const FCataclysmDayReport ControlReport = OnAStanding->AdvanceDay();

	TestTrue(TEXT("the ordinary dungeon's timer ran out on the fallen city"),
			 Report.Resolved.Contains(OrdinaryId));
	TestEqual(TEXT("it is not counted as a dungeon hitting a city"),
			  OnAFallen->DungeonsDetonated, DetonatedBefore);
	TestFalse(TEXT("the report does not name the city as hit by a dungeon that left"),
			  Report.HitAndLeft.Contains(CityId));
	TestEqual(TEXT("the run keeps no new last hit"), OnAFallen->LastHitDay, LastHitDayBefore);
	TestEqual(TEXT("the fallen city's defence is unchanged"), Fallen->Defence, DefenceBefore, 0.01f);
	TestEqual(TEXT("nor its population"), Fallen->Population, PopulationBefore, 0.01f);
	TestNotNull(TEXT("the ordinary dungeon is still on the map"), OnAFallen->FindDungeon(OrdinaryId));
	TestEqual(TEXT("and its timer is full again"),
			  OnAFallen->Clock->DaysUntilResolveFor(OrdinaryId), FullTimer, 0.001f);

	// THE CONTROL: ON THE SAME CITY STANDING, THE SAME TIMER HITS AND THE DUNGEON LEAVES.
	TestTrue(TEXT("CONTROL: on the standing Outpost the timer ran out"),
			 ControlReport.Resolved.Contains(OrdinaryId));
	TestEqual(TEXT("CONTROL: one ordinary dungeon is counted as hitting a city"),
			  OnAStanding->DungeonsDetonated, ControlDetonatedBefore + 1);
	TestTrue(TEXT("CONTROL: the report names that city as hit by a dungeon that left"),
			 ControlReport.HitAndLeft.Contains(CityId));
	TestNull(TEXT("CONTROL: and the dungeon is off the map"), OnAStanding->FindDungeon(OrdinaryId));

	return true;
}

// ---------------------------------------------------------------------------
// A Quest dungeon whose city falls moves -- 2026-10-21
// ---------------------------------------------------------------------------
//
// The owner, 2026-10-16, verbatim: "Quest dungeons don't go away when a city
// falls. They just move to an adjacent city. So eventually they should be
// hitting something." The simulation took it that day; `CityFell` does it in
// the game since 2026-10-21, through `UCataclysmSurgeScheduler::PickRefuge`,
// whose own tests are in `CataclysmSurgeTests.cpp`. These ask whether the run
// calls it, and what the run does around it.

namespace CataclysmEmpireRunTest
{
	/** How many times a dungeon is named in one of a report's lists. */
	int32 TimesNamed(const TArray<int32>& List, int32 DungeonId)
	{
		int32 Times = 0;
		for (const int32 Named : List)
		{
			if (Named == DungeonId)
			{
				++Times;
			}
		}
		return Times;
	}
}

/**
 * A city that falls moves the Quest dungeon standing on it to a neighbour,
 * keeping everything it carries, and its Dungeon City counts only the
 * ordinary dungeons it absorbed.
 *
 * TWO RUNS FROM ONE SEED. In each the first Outpost carries a feller and a
 * bystander, two ordinary dungeons, and the feller's hit fells it on day 2.
 * In `Moved` a Quest dungeon carrying a Horde stands there too, with twenty
 * of its twenty-five days left. In `Control` a third ordinary dungeon stands
 * in its place.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunFallMovesQuestTest,
	"Cataclysm.EmpireRun.AFallMovesItsQuestDungeonAndItsDungeonCityCountsOnlyWhatItAbsorbed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunFallMovesQuestTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	const int32 FellerId = 9781;
	const int32 BystanderId = 9782;
	const int32 ThirdId = 9783;
	const float QuestTimer = 25.0f;
	const float QuestDaysLeft = 20.0f;

	UCataclysmEmpireRun* Moved = MakeEmptyRun(1);
	UCataclysmEmpireRun* Control = MakeEmptyRun(1);

	const int32 CityId = FirstStandingOutpost(*Moved);
	if (!TestTrue(TEXT("set-up: the map has an Outpost standing"),
				  CityId != INDEX_NONE)
		|| !TestEqual(TEXT("set-up: and it is the same Outpost in the control"),
					  FirstStandingOutpost(*Control), CityId))
	{
		return false;
	}

	for (UCataclysmEmpireRun* Run : { Moved, Control })
	{
		PlaceAFellerAndABystander(*Run, CityId, FellerId, BystanderId);
		ResolveOnTheNextDay(*Run, FellerId);
	}

	PlaceDungeon(*Moved, ThirdId, ECataclysmDungeonType::Quest,
				 ECataclysmDungeonSubType::Horde, *Moved->Map->Find(CityId),
				 QuestTimer);
	GiveDamageAndDaysLeft(*Moved, ThirdId, 0.0f, 0.0f, QuestDaysLeft);

	PlaceDungeon(*Control, ThirdId, ECataclysmDungeonType::Basic,
				 ECataclysmDungeonSubType::None, *Control->Map->Find(CityId),
				 EmptyBoardNeverDays);

	const FCataclysmDayReport Report = Moved->AdvanceDay();
	const FCataclysmDayReport ControlReport = Control->AdvanceDay();

	if (!TestEqual(TEXT("set-up: the day advanced is day 2"), Report.Day, 2)
		|| !TestTrue(TEXT("set-up: the Outpost fell"), Report.Fallen.Contains(CityId))
		|| !TestTrue(TEXT("set-up: and in the control"), ControlReport.Fallen.Contains(CityId))
		|| !TestFalse(TEXT("set-up: the Quest dungeon's own timer did not run out"),
					  Report.Resolved.Contains(ThirdId)))
	{
		return false;
	}

	// NOT ABSORBED.
	TestFalse(TEXT("the Quest dungeon is not named as absorbed"),
			  Report.Absorbed.Contains(ThirdId));
	TestEqual(TEXT("the two ordinary dungeons are, and nothing else"),
			  Report.Absorbed.Num(), 2);
	TestTrue(TEXT("the feller and the bystander are the two"),
			 Report.Absorbed.Contains(FellerId) && Report.Absorbed.Contains(BystanderId));

	// MOVED, TO A NEIGHBOUR THAT WILL TAKE IT. On an intact map a rim
	// Outpost's perimeter links stand and are exposed, so a neighbour always
	// qualifies and the outward search is not reached.
	const FCataclysmDungeon* Quest = Moved->FindDungeon(ThirdId);
	const FCataclysmCity* Fallen = Moved->Map->Find(CityId);
	if (!TestNotNull(TEXT("the Quest dungeon is still on the map"), Quest)
		|| !TestNotNull(TEXT("set-up: the fallen city is on the map"), Fallen))
	{
		return false;
	}

	const int32 Refuge = Quest->CityId;

	TestNotEqual(TEXT("it is no longer on the city that fell"), Refuge, CityId);
	TestTrue(TEXT("it is on a city adjacent to the one that fell"),
			 UCataclysmSurgeScheduler::AdjacentCities(*Fallen).Contains(Refuge));
	TestTrue(TEXT("and that city is exposed and standing"),
			 Moved->Map->IsExposed(Refuge));
	TestNotEqual(TEXT("and is not the Pillar"), Refuge, Moved->Map->PillarId);

	// NAMED AS A FALL'S MOVE, ONCE, WITH ITS TWO CITIES.
	TestEqual(TEXT("the report names it in Relocated once"),
			  TimesNamed(Report.Relocated, ThirdId), 1);
	if (TestEqual(TEXT("and one move by a fall today"), Report.MovedByAFall.Num(), 1)
		&& TestEqual(TEXT("with one city it left"), Report.MovedByAFallFrom.Num(), 1)
		&& TestEqual(TEXT("and one city it went to"), Report.MovedByAFallTo.Num(), 1))
	{
		TestEqual(TEXT("the move is the Quest dungeon's"), Report.MovedByAFall[0], ThirdId);
		TestEqual(TEXT("it left the city that fell"), Report.MovedByAFallFrom[0], CityId);
		TestEqual(TEXT("and went to the city it stands on"), Report.MovedByAFallTo[0], Refuge);
	}

	// IT KEPT EVERYTHING ELSE: floors, sub-type, the tier its depth was rolled
	// on, its timer's length, and what was left of its timer.
	TestEqual(TEXT("it kept its floors"), Quest->Floors, 20);
	TestTrue(TEXT("and its sub-type"),
			 Quest->SubType == ECataclysmDungeonSubType::Horde);
	TestTrue(TEXT("and the tier its depth was rolled on"),
			 Quest->CityTier == ECataclysmCityTier::Outpost);
	TestEqual(TEXT("and its timer's length"), Quest->ResolveDays, QuestTimer, 0.001f);
	TestEqual(TEXT("and what was left of its timer, one day less for the day that passed and not refilled"),
			  Moved->Clock->DaysUntilResolveFor(ThirdId), QuestDaysLeft - 1.0f, 0.001f);

	// THE DUNGEON CITY COUNTS THE TWO IT ABSORBED.
	const FCataclysmDungeonSpec Spec = UCataclysmSurgeScheduler::SpecFor(
		ECataclysmDungeonType::FallenCity, ECataclysmCityTier::Outpost);
	const FCataclysmDungeon* DungeonCity = DungeonCityOn(*Moved, CityId);
	if (TestNotNull(TEXT("a Dungeon City stands on the fallen city"), DungeonCity))
	{
		TestEqual(TEXT("it holds two bosses, for the two dungeons it absorbed"),
				  DungeonCity->Bosses, 2);
		TestEqual(TEXT("and is as deep as its tier's least"),
				  DungeonCity->Floors, FMath::Max(2, Spec.LeastFloors));
	}

	// THE EMPIRE SCREEN'S RECORD OF THE MOVE.
	TestEqual(TEXT("the run keeps day 2 as the last move"), Moved->LastMoveDay, 2);
	TestEqual(TEXT("from the city that fell"), Moved->LastMoveFromCityId, CityId);
	TestEqual(TEXT("to the city it went to"), Moved->LastMoveToCityId, Refuge);

	FString Why;
	TestTrue(TEXT("the dungeons and the timers still agree"),
			 UCataclysmEmpireRun::DungeonsAgreeWithTimers(
				 Moved->Dungeons, Moved->Clock->Timers, Why));

	// THE CONTROL: A THIRD ORDINARY DUNGEON IN ITS PLACE IS ABSORBED AND
	// COUNTED, so the two bosses above are the Quest dungeon left out and not
	// a count that never reached three.
	TestEqual(TEXT("CONTROL: the report names three dungeons as absorbed"),
			  ControlReport.Absorbed.Num(), 3);
	TestTrue(TEXT("CONTROL: the third among them"),
			 ControlReport.Absorbed.Contains(ThirdId));
	TestEqual(TEXT("CONTROL: and no fall moved anything"),
			  ControlReport.MovedByAFall.Num(), 0);
	TestEqual(TEXT("CONTROL: nor did anything else"), ControlReport.Relocated.Num(), 0);
	const FCataclysmDungeon* ControlCity = DungeonCityOn(*Control, CityId);
	if (TestNotNull(TEXT("CONTROL: a Dungeon City stands on the fallen city"), ControlCity))
	{
		TestEqual(TEXT("CONTROL: it holds three bosses"), ControlCity->Bosses, 3);
	}
	TestEqual(TEXT("CONTROL: the run keeps no last move"), Control->LastMoveDay, 0);

	return true;
}

/**
 * A Quest dungeon left on a city that has fallen moves by its timer from
 * there. Ruled by the coordinating session on 2026-10-21.
 *
 * THE STATE IS BUILT BY HAND. In play a fall leaves a Quest dungeon on the
 * ruin only when no exposed city but the Pillar is left anywhere. Here the
 * first Outpost is felled through the map, so no `CityFell` runs, and two
 * Quest dungeons are put on it: one whose timer runs out the next day, and
 * the control, whose timer does not.
 *
 * TWENTY SEEDS, BECAUSE THE TIMER'S MOVE IS A COIN. Each seed's run has drawn
 * a different opening wave, so the coin is a different draw in each. Before
 * 2026-10-21 `RelocateQuestDungeon` returned at once for a fallen city and no
 * seed could move it; with a chance of one half, twenty seeds all declining
 * is about one in a million.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunQuestLeftOnARuinTest,
	"Cataclysm.EmpireRun.AQuestDungeonLeftOnAFallenCityLaterMovesByItsTimer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunQuestLeftOnARuinTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	const int32 DueId = 9791;
	const int32 NotDueId = 9792;
	constexpr int32 Seeds = 20;

	int32 Moves = 0;
	int32 Stays = 0;

	for (int32 Seed = 1; Seed <= Seeds; ++Seed)
	{
		UCataclysmEmpireRun* Run = MakeEmptyRun(Seed);

		const int32 CityId = FirstStandingOutpost(*Run);
		if (!TestTrue(TEXT("set-up: the map has an Outpost standing"),
					  CityId != INDEX_NONE)
			|| !TestTrue(TEXT("set-up: the Outpost was felled through the map"),
						 Run->Map->Damage(CityId, 100000.0f, 0.0f)))
		{
			return false;
		}

		const FCataclysmCity* Ruin = Run->Map->Find(CityId);
		PlaceDungeon(*Run, DueId, ECataclysmDungeonType::Quest,
					 ECataclysmDungeonSubType::None, *Ruin, 25.0f);
		PlaceDungeon(*Run, NotDueId, ECataclysmDungeonType::Quest,
					 ECataclysmDungeonSubType::None, *Ruin, 25.0f);
		ResolveOnTheNextDay(*Run, DueId);
		GiveDamageAndDaysLeft(*Run, NotDueId, 0.0f, 0.0f, 5.0f);

		const FCataclysmDayReport Report = Run->AdvanceDay();

		if (!TestTrue(FString::Printf(
				TEXT("set-up: seed %d: the due Quest dungeon's timer ran out"), Seed),
				Report.Resolved.Contains(DueId)))
		{
			return false;
		}

		TestEqual(FString::Printf(
			TEXT("seed %d: a Quest timer on a fallen city hits nothing"), Seed),
			Report.QuestHits.Num(), 0);
		TestFalse(FString::Printf(
			TEXT("seed %d: and no fall moved it today"), Seed),
			Report.MovedByAFall.Contains(DueId));

		const FCataclysmDungeon* Due = Run->FindDungeon(DueId);
		if (!TestNotNull(TEXT("the due Quest dungeon is still on the map"), Due))
		{
			return false;
		}

		if (Due->CityId == CityId)
		{
			++Stays;
			TestFalse(FString::Printf(
				TEXT("seed %d: it stayed and is not named as moved"), Seed),
				Report.Relocated.Contains(DueId));
		}
		else
		{
			++Moves;
			TestTrue(FString::Printf(
				TEXT("seed %d: it moved and is named as moved"), Seed),
				Report.Relocated.Contains(DueId));
			TestTrue(FString::Printf(
				TEXT("seed %d: to a city adjacent to the ruin"), Seed),
				UCataclysmSurgeScheduler::AdjacentCities(*Ruin).Contains(Due->CityId));
			TestTrue(FString::Printf(
				TEXT("seed %d: that is exposed and standing"), Seed),
				Run->Map->IsExposed(Due->CityId));
		}

		// THE CONTROL: THE SAME RUIN, A TIMER THAT DID NOT RUN OUT, NO MOVE.
		const FCataclysmDungeon* NotDue = Run->FindDungeon(NotDueId);
		if (TestNotNull(TEXT("CONTROL: the Quest dungeon not due is on the map"), NotDue))
		{
			TestEqual(FString::Printf(
				TEXT("CONTROL: seed %d: the one whose timer did not run out is still on the ruin"), Seed),
				NotDue->CityId, CityId);
		}
		TestFalse(FString::Printf(
			TEXT("CONTROL: seed %d: and is not named as moved"), Seed),
			Report.Relocated.Contains(NotDueId));
	}

	AddInfo(FString::Printf(
		TEXT("%d seeds: the Quest dungeon on the ruin moved by its timer in %d and stayed in %d"),
		Seeds, Moves, Stays));

	TestTrue(TEXT("a Quest dungeon on a fallen city moved by its timer in some seed"),
			 Moves > 0);

	return true;
}

/**
 * A Quest dungeon a fall moved can take its own timer's move later the same
 * day. Ruled by the coordinating session on 2026-10-21: both moves stand.
 *
 * THE ORDER IS THE CLOCK'S. Its timers run out in the order the dungeons were
 * put on it, so the feller, put down first, fells the city before the Quest
 * dungeon's timer is reached, and that timer then runs out on the city the
 * fall moved it to: it hits that city, and may move on.
 *
 * TWENTY SEEDS, because the timer's move is a coin; the second move is
 * asserted to happen in some seed and never to be other than adjacent.
 *
 * THE CONTROL is a second Quest dungeon on the same city whose timer does not
 * run out: the fall moves it once and nothing moves it again.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunTwoMovesOneDayTest,
	"Cataclysm.EmpireRun.AQuestDungeonAFallMovedMayTakeItsOwnTimersMoveTheSameDay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunTwoMovesOneDayTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	const int32 FellerId = 9801;
	const int32 BystanderId = 9802;
	const int32 DueId = 9803;
	const int32 NotDueId = 9804;
	constexpr int32 Seeds = 20;

	int32 TwoMoves = 0;
	int32 OneMove = 0;

	for (int32 Seed = 1; Seed <= Seeds; ++Seed)
	{
		UCataclysmEmpireRun* Run = MakeEmptyRun(Seed);

		const int32 CityId = FirstStandingOutpost(*Run);
		if (!TestTrue(TEXT("set-up: the map has an Outpost standing"),
					  CityId != INDEX_NONE))
		{
			return false;
		}

		PlaceAFellerAndABystander(*Run, CityId, FellerId, BystanderId);
		ResolveOnTheNextDay(*Run, FellerId);

		PlaceDungeon(*Run, DueId, ECataclysmDungeonType::Quest,
					 ECataclysmDungeonSubType::None, *Run->Map->Find(CityId), 25.0f);
		ResolveOnTheNextDay(*Run, DueId);

		PlaceDungeon(*Run, NotDueId, ECataclysmDungeonType::Quest,
					 ECataclysmDungeonSubType::None, *Run->Map->Find(CityId), 25.0f);
		GiveDamageAndDaysLeft(*Run, NotDueId, 0.0f, 0.0f, 5.0f);

		const FCataclysmDayReport Report = Run->AdvanceDay();

		const int32 FellerAt = Report.Resolved.IndexOfByKey(FellerId);
		const int32 DueAt = Report.Resolved.IndexOfByKey(DueId);
		const int32 DueMove = Report.MovedByAFall.IndexOfByKey(DueId);
		const int32 NotDueMove = Report.MovedByAFall.IndexOfByKey(NotDueId);

		if (!TestTrue(FString::Printf(TEXT("set-up: seed %d: the Outpost fell"), Seed),
					  Report.Fallen.Contains(CityId))
			|| !TestTrue(FString::Printf(
					TEXT("set-up: seed %d: the feller's timer ran out before the Quest dungeon's"), Seed),
					FellerAt != INDEX_NONE && DueAt != INDEX_NONE && FellerAt < DueAt)
			|| !TestTrue(FString::Printf(
					TEXT("set-up: seed %d: the fall moved both Quest dungeons"), Seed),
					Report.MovedByAFallTo.IsValidIndex(DueMove)
						&& Report.MovedByAFallTo.IsValidIndex(NotDueMove)))
		{
			return false;
		}

		const int32 Refuge = Report.MovedByAFallTo[DueMove];

		// ITS TIMER RAN OUT ON THE CITY THE FALL MOVED IT TO, AND HIT IT.
		TestTrue(FString::Printf(
			TEXT("seed %d: the Quest dungeon's timer hit the city the fall moved it to"), Seed),
			Report.QuestHits.Contains(Refuge));

		const FCataclysmDungeon* Due = Run->FindDungeon(DueId);
		if (!TestNotNull(TEXT("the due Quest dungeon is on the map"), Due))
		{
			return false;
		}

		const int32 Times = TimesNamed(Report.Relocated, DueId);

		if (Times == 2)
		{
			++TwoMoves;
			TestNotEqual(FString::Printf(
				TEXT("seed %d: moved twice, it is no longer where the fall put it"), Seed),
				Due->CityId, Refuge);
			TestTrue(FString::Printf(
				TEXT("seed %d: and its second move was to a city adjacent to where the fall put it"), Seed),
				UCataclysmSurgeScheduler::AdjacentCities(*Run->Map->Find(Refuge))
					.Contains(Due->CityId));
		}
		else
		{
			++OneMove;
			TestEqual(FString::Printf(
				TEXT("seed %d: moved once or twice, and here once"), Seed), Times, 1);
			TestEqual(FString::Printf(
				TEXT("seed %d: moved once, it is where the fall put it"), Seed),
				Due->CityId, Refuge);
		}

		// THE CONTROL: MOVED BY THE FALL, AND NOTHING MORE.
		const FCataclysmDungeon* NotDue = Run->FindDungeon(NotDueId);
		TestEqual(FString::Printf(
			TEXT("CONTROL: seed %d: the Quest dungeon whose timer did not run out moved once"), Seed),
			TimesNamed(Report.Relocated, NotDueId), 1);
		if (TestNotNull(TEXT("CONTROL: it is on the map"), NotDue))
		{
			TestEqual(FString::Printf(
				TEXT("CONTROL: seed %d: and is where the fall put it"), Seed),
				NotDue->CityId, Report.MovedByAFallTo[NotDueMove]);
		}
	}

	AddInfo(FString::Printf(
		TEXT("%d seeds: the Quest dungeon a fall moved took its timer's move as well in %d and did not in %d"),
		Seeds, TwoMoves, OneMove));

	TestTrue(TEXT("a Quest dungeon moved twice in one day, by a fall and by its own timer, in some seed"),
			 TwoMoves > 0);

	return true;
}

/**
 * The Quest dungeon the player is walking moves when its city falls, the
 * walk goes on, and reaching its bottom still clears it. Ruled by the
 * coordinating session on 2026-10-21.
 *
 * WHAT THE GAME MODE READS IS WHAT THIS ASKS. `ACataclysmDungeonGameMode`
 * finds the walked dungeon by its number through `FindDungeon` and clears it
 * through `ClearDungeon`, and the clock's `CurrentDungeonId` is what stops its
 * timer while it is walked. The dungeon's number and those three are all this
 * module holds, so they are asked here, headless.
 *
 * THE CONTROL is the recorded difference from the model, issue #2399: an
 * ordinary dungeon being walked on the same city is still absorbed, the clock
 * forgets it, and its clear counts for nothing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunWalkedQuestMovesTest,
	"Cataclysm.EmpireRun.AWalkedQuestDungeonWhoseCityFallsMovesAndItsClearStillCounts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunWalkedQuestMovesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	const int32 FellerId = 9821;
	const int32 BystanderId = 9822;
	const int32 WalkedId = 9823;

	UCataclysmEmpireRun* Walked = MakeEmptyRun(1);
	UCataclysmEmpireRun* Control = MakeEmptyRun(1);

	const int32 CityId = FirstStandingOutpost(*Walked);
	if (!TestTrue(TEXT("set-up: the map has an Outpost standing"),
				  CityId != INDEX_NONE)
		|| !TestEqual(TEXT("set-up: and it is the same Outpost in the control"),
					  FirstStandingOutpost(*Control), CityId))
	{
		return false;
	}

	for (UCataclysmEmpireRun* Run : { Walked, Control })
	{
		PlaceAFellerAndABystander(*Run, CityId, FellerId, BystanderId);
		ResolveOnTheNextDay(*Run, FellerId);
	}

	// THE PLAYER IS INSIDE THE QUEST DUNGEON IN `Walked`, AND INSIDE THE
	// ORDINARY BYSTANDER IN `Control`.
	PlaceDungeon(*Walked, WalkedId, ECataclysmDungeonType::Quest,
				 ECataclysmDungeonSubType::None, *Walked->Map->Find(CityId), 25.0f);
	Walked->Clock->EnterDungeon(WalkedId);
	Control->Clock->EnterDungeon(BystanderId);

	const FCataclysmDayReport Report = Walked->AdvanceDay();
	const FCataclysmDayReport ControlReport = Control->AdvanceDay();

	if (!TestTrue(TEXT("set-up: the Outpost fell"), Report.Fallen.Contains(CityId))
		|| !TestTrue(TEXT("set-up: and in the control"), ControlReport.Fallen.Contains(CityId)))
	{
		return false;
	}

	// IT MOVED.
	TestTrue(TEXT("the fall moved the walked Quest dungeon"),
			 Report.MovedByAFall.Contains(WalkedId));
	const FCataclysmDungeon* Dungeon = Walked->FindDungeon(WalkedId);
	if (TestNotNull(TEXT("the run still has the walked Quest dungeon, so the game mode still finds it by its number"),
					Dungeon))
	{
		TestNotEqual(TEXT("and it is no longer on the city that fell"),
					 Dungeon->CityId, CityId);
	}

	// AND THE WALK GOES ON.
	TestEqual(TEXT("the clock still has the player inside it"),
			  Walked->Clock->CurrentDungeonId, WalkedId);
	TestNotNull(TEXT("and its timer is still on the clock"),
				Walked->Clock->FindTimer(WalkedId));

	// REACHING ITS BOTTOM CLEARS IT, AND IT COUNTS.
	const int32 ObjectivesBefore = Walked->QuestObjectives;
	TestTrue(TEXT("clearing it succeeds"), Walked->ClearDungeon(WalkedId));
	TestEqual(TEXT("and earns one quest objective"),
			  Walked->QuestObjectives, ObjectivesBefore + 1);
	TestEqual(TEXT("and the player has left it"),
			  Walked->Clock->CurrentDungeonId, static_cast<int32>(INDEX_NONE));

	// THE CONTROL: AN ORDINARY WALKED DUNGEON IS STILL TAKEN WITH ITS CITY.
	TestTrue(TEXT("CONTROL: the walked ordinary dungeon is absorbed"),
			 ControlReport.Absorbed.Contains(BystanderId));
	TestNull(TEXT("CONTROL: the run no longer has it"),
			 Control->FindDungeon(BystanderId));
	TestEqual(TEXT("CONTROL: the clock no longer has the player inside anything"),
			  Control->Clock->CurrentDungeonId, static_cast<int32>(INDEX_NONE));
	TestFalse(TEXT("CONTROL: and reaching its bottom clears nothing"),
			  Control->ClearDungeon(BystanderId));
	TestEqual(TEXT("CONTROL: so nothing is counted as cleared"),
			  Control->DungeonsCleared, 0);

	return true;
}

// ---------------------------------------------------------------------------
// An ordinary Void dungeon adds Void stacks instead of damage -- 2026-10-22
// ---------------------------------------------------------------------------
//
// The owner, 2026-10-16, verbatim: "All void dungeons when they resolve add 1-3
// void stacks to a city instead of doing damage to the city or population and
// at 10 stacks the city is erased." Ruled by the coordinating session the same
// day: the stacks go by the third of the row's depth, a Void Siege's daily bite
// stays damage, and a city that falls with fewer than ten can be retaken.
//
// EVERY TEST BELOW STARTS FROM `MakeEmptyRun` AND STOPS BY DAY 2, so no wave
// lands while they run. Each has a control: the same dungeon sent by nobody.

namespace CataclysmEmpireRunTest
{
	/**
	 * Names who sent a dungeon `PlaceDungeon` put down and, when `Floors` is
	 * more than nought, how deep it is, with no floors added by an upgrade.
	 */
	void SendFrom(UCataclysmEmpireRun& Run, int32 DungeonId, ECataclysmType Sender,
				  int32 Floors)
	{
		for (FCataclysmDungeon& Standing : Run.Dungeons)
		{
			if (Standing.DungeonId == DungeonId)
			{
				Standing.Cataclysm = Sender;
				if (Floors > 0)
				{
					Standing.Floors = Floors;
					Standing.UpgradeFloors = 0;
				}
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunVoidAddsStacksTest,
	"Cataclysm.EmpireRun.AnOrdinaryVoidDungeonAddsItsStacksInsteadOfDamageAndStillLeaves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunVoidAddsStacksTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	const int32 VoidId = 9901;

	// TWO RUNS FROM ONE SEED, THE SAME DUNGEON ON THE SAME OUTPOST, 11 FLOORS
	// DEEP, ITS TIMER ONE DAY FROM RUNNING OUT. In `Run` the Void sent it; in
	// `Control` nobody did.
	UCataclysmEmpireRun* Run = MakeEmptyRun(1);
	UCataclysmEmpireRun* Control = MakeEmptyRun(1);

	const int32 CityId = FirstStandingOutpost(*Run);
	if (!TestTrue(TEXT("set-up: the map has an Outpost standing"), CityId != INDEX_NONE)
		|| !TestEqual(TEXT("set-up: and it is the same Outpost in the control"),
					  FirstStandingOutpost(*Control), CityId))
	{
		return false;
	}

	for (UCataclysmEmpireRun* Each : { Run, Control })
	{
		PlaceDungeon(*Each, VoidId, ECataclysmDungeonType::Basic,
					 ECataclysmDungeonSubType::None, *Each->Map->Find(CityId), 30.0f);
		GiveDamageAndDaysLeft(*Each, VoidId, 100.0f, 40.0f, 1.0f);
	}

	SendFrom(*Run, VoidId, ECataclysmType::Void, 11);
	SendFrom(*Control, VoidId, ECataclysmType::None, 11);

	const FCataclysmDungeon* Placed = Run->FindDungeon(VoidId);
	const FCataclysmDungeon* ControlPlaced = Control->FindDungeon(VoidId);
	const FCataclysmCity* City = Run->Map->Find(CityId);
	const FCataclysmCity* ControlCity = Control->Map->Find(CityId);
	if (!TestNotNull(TEXT("set-up: the Void dungeon is on the map"), Placed)
		|| !TestNotNull(TEXT("set-up: and the control's"), ControlPlaced)
		|| !TestNotNull(TEXT("set-up: and the city"), City)
		|| !TestNotNull(TEXT("set-up: and the control's city"), ControlCity)
		|| !TestEqual(TEXT("set-up: an 11 floor Outpost dungeon adds 2"),
					  Placed->VoidStacksItAdds(), 2))
	{
		return false;
	}

	const float DefenceBefore = City->Defence;
	const float PopulationBefore = City->Population;
	const float ControlDefenceBefore = ControlCity->Defence;
	const float ControlHit = ControlPlaced->DefenceDamage * ControlPlaced->BiteScale();

	const FCataclysmDayReport Report = Run->AdvanceDay();
	const FCataclysmDayReport ControlReport = Control->AdvanceDay();

	TestTrue(TEXT("the Void dungeon's timer ran out"), Report.Resolved.Contains(VoidId));

	// STACKS, AND NOTHING ELSE.
	TestEqual(TEXT("its city carries the 2 stacks it adds"), City->VoidStacks, 2);
	TestTrue(TEXT("and lost no defence"), City->Defence == DefenceBefore);
	TestTrue(TEXT("and no people"), City->Population == PopulationBefore);
	TestFalse(TEXT("and stands"), City->bFallen);

	// AND OTHERWISE EXACTLY AN ORDINARY DUNGEON THAT HIT.
	TestEqual(TEXT("it counts as one ordinary dungeon hitting a city"),
			  Run->DungeonsDetonated, 1);
	TestNull(TEXT("it is off the map"), Run->FindDungeon(VoidId));
	TestNull(TEXT("and off the clock"), Run->Clock->FindTimer(VoidId));
	TestEqual(TEXT("the report names one city as hit by a dungeon that left"),
			  Report.HitAndLeft.Num(), 1);
	TestTrue(TEXT("and it is this city"), Report.HitAndLeft.Contains(CityId));
	TestEqual(TEXT("no city fell"), Report.Fallen.Num(), 0);
	TestEqual(TEXT("and the run keeps day 2 as the last hit"), Run->LastHitDay, 2);

	// THE CONTROL: THE SAME DUNGEON SENT BY NOBODY TAKES ITS POINTS.
	TestEqual(TEXT("CONTROL: the city carries no stacks"), ControlCity->VoidStacks, 0);
	TestEqual(TEXT("CONTROL: and lost one hit's defence"),
			  ControlDefenceBefore - ControlCity->Defence, ControlHit, 0.01f);
	TestTrue(TEXT("CONTROL: which is a real amount"), ControlHit > 1.0f);
	TestEqual(TEXT("CONTROL: it counts as one hit too"), Control->DungeonsDetonated, 1);
	TestTrue(TEXT("CONTROL: and left"), ControlReport.HitAndLeft.Contains(CityId));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunVoidThirdsTest,
	"Cataclysm.EmpireRun.AShallowAMiddleAndADeepVoidDungeonAddOneTwoAndThreeStacks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunVoidThirdsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	UCataclysmEmpireRun* Run = MakeEmptyRun(1);

	const TArray<int32> Outposts = StandingOutposts(*Run, 3);
	const FCataclysmDungeonSpec Row = UCataclysmSurgeScheduler::SpecFor(
		ECataclysmDungeonType::Basic, ECataclysmCityTier::Outpost);

	if (!TestEqual(TEXT("set-up: the map has three Outposts standing"), Outposts.Num(), 3)
		|| !TestEqual(TEXT("set-up: an ordinary Outpost dungeon is 8 floors at least"), Row.LeastFloors, 8)
		|| !TestEqual(TEXT("set-up: and 15 at most"), Row.MostFloors, 15))
	{
		return false;
	}

	// THE SHALLOWEST, ONE IN THE MIDDLE THIRD, AND THE DEEPEST.
	const int32 Floors[] = { 8, 12, 15 };
	const int32 Expected[] = { 1, 2, 3 };

	for (int32 Index = 0; Index < 3; ++Index)
	{
		const int32 DungeonId = 9911 + Index;
		PlaceDungeon(*Run, DungeonId, ECataclysmDungeonType::Basic,
					 ECataclysmDungeonSubType::None, *Run->Map->Find(Outposts[Index]), 30.0f);
		GiveDamageAndDaysLeft(*Run, DungeonId, 100.0f, 40.0f, 1.0f);
		SendFrom(*Run, DungeonId, ECataclysmType::Void, Floors[Index]);
	}

	const FCataclysmDayReport Report = Run->AdvanceDay();

	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FCataclysmCity* City = Run->Map->Find(Outposts[Index]);
		if (!TestNotNull(TEXT("the city is on the map"), City))
		{
			return false;
		}

		TestEqual(FString::Printf(TEXT("a %d floor Void dungeon added %d"), Floors[Index],
								  Expected[Index]),
				  City->VoidStacks, Expected[Index]);
		TestTrue(FString::Printf(TEXT("and the city it hit, %d, stands at full defence"),
								 Outposts[Index]),
				 !City->bFallen && City->Defence == City->MaxDefence);
	}

	TestEqual(TEXT("all three count as hits"), Run->DungeonsDetonated, 3);
	TestEqual(TEXT("and all three left"), Report.HitAndLeft.Num(), 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunVoidErasesTest,
	"Cataclysm.EmpireRun.AVoidDungeonThatBringsItsCityToTenStacksErasesItAndTheFallAbsorbsTheRest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunVoidErasesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	const int32 VoidId = 9921;
	const int32 BystanderId = 9922;

	// TWO RUNS FROM ONE SEED. The same Outpost carries a 15 floor Void dungeon,
	// which adds 3, and an ordinary one beside it that never resolves. In
	// `Erased` the city already carries 7, so the hit brings it to exactly ten;
	// in `Stood`, the control, it carries 6 and reaches nine.
	UCataclysmEmpireRun* Erased = MakeEmptyRun(1);
	UCataclysmEmpireRun* Stood = MakeEmptyRun(1);

	const int32 CityId = FirstStandingOutpost(*Erased);
	if (!TestTrue(TEXT("set-up: the map has an Outpost standing"), CityId != INDEX_NONE)
		|| !TestEqual(TEXT("set-up: and it is the same Outpost in the control"),
					  FirstStandingOutpost(*Stood), CityId))
	{
		return false;
	}

	for (UCataclysmEmpireRun* Each : { Erased, Stood })
	{
		const FCataclysmCity& Host = *Each->Map->Find(CityId);
		PlaceDungeon(*Each, VoidId, ECataclysmDungeonType::Basic,
					 ECataclysmDungeonSubType::None, Host, EmptyBoardNeverDays);
		GiveDamageAndDaysLeft(*Each, VoidId, 100.0f, 40.0f, 1.0f);
		SendFrom(*Each, VoidId, ECataclysmType::Void, 15);

		PlaceDungeon(*Each, BystanderId, ECataclysmDungeonType::Basic,
					 ECataclysmDungeonSubType::None, Host, EmptyBoardNeverDays);
		GiveDamageAndDaysLeft(*Each, BystanderId, 100.0f, 40.0f, EmptyBoardNeverDays);
	}

	Erased->Map->FindMutable(CityId)->VoidStacks = 7;
	Stood->Map->FindMutable(CityId)->VoidStacks = 6;

	const FCataclysmDayReport Report = Erased->AdvanceDay();
	const FCataclysmDayReport StoodReport = Stood->AdvanceDay();

	const FCataclysmCity* City = Erased->Map->Find(CityId);
	const FCataclysmCity* StoodCity = Stood->Map->Find(CityId);
	if (!TestNotNull(TEXT("the city is still on the map"), City)
		|| !TestNotNull(TEXT("and the control's"), StoodCity))
	{
		return false;
	}

	// TEN STACKS: FALLEN AND ERASED.
	TestTrue(TEXT("the city fell"), Report.Fallen.Contains(CityId));
	TestEqual(TEXT("carrying ten stacks"), City->VoidStacks, 10);
	TestTrue(TEXT("and it is erased"), City->bErased);
	TestFalse(TEXT("so it cannot be retaken"), Erased->Map->Retake(CityId));

	// THE FALL ABSORBED BOTH DUNGEONS, the Void one that felled it included, and
	// left no Dungeon City on a city there is nothing left of.
	TestTrue(TEXT("the Void dungeon that erased it is named as absorbed"),
			 Report.Absorbed.Contains(VoidId));
	TestTrue(TEXT("and so is the ordinary one beside it"),
			 Report.Absorbed.Contains(BystanderId));
	TestNull(TEXT("the Void dungeon is off the map"), Erased->FindDungeon(VoidId));
	TestNull(TEXT("and so is the one beside it"), Erased->FindDungeon(BystanderId));
	TestEqual(TEXT("the report names no city as hit by a dungeon that left"),
			  Report.HitAndLeft.Num(), 0);
	TestEqual(TEXT("its hit is still counted as one ordinary dungeon hitting a city"),
			  Erased->DungeonsDetonated, 1);
	TestNull(TEXT("no Dungeon City stands on an erased city"), DungeonCityOn(*Erased, CityId));
	TestEqual(TEXT("so nothing is left on the map"), Erased->DungeonCount(), 0);

	// THE CONTROL: NINE STACKS, AND THE CITY STANDS.
	TestEqual(TEXT("CONTROL: no city fell"), StoodReport.Fallen.Num(), 0);
	TestEqual(TEXT("CONTROL: the city carries nine"), StoodCity->VoidStacks, 9);
	TestFalse(TEXT("CONTROL: and is not erased"), StoodCity->bErased);
	TestTrue(TEXT("CONTROL: the Void dungeon hit and left"),
			 StoodReport.HitAndLeft.Contains(CityId));
	TestNotNull(TEXT("CONTROL: the dungeon beside it still stands"),
				Stood->FindDungeon(BystanderId));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireRunVoidSiegeBitesTest,
	"Cataclysm.EmpireRun.AVoidSiegesDailyBiteStillTakesDefenceAndAddsNoStacks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireRunVoidSiegeBitesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireRunTest;

	const int32 SiegeId = 9931;

	// TWO RUNS FROM ONE SEED, A SIEGE ON THE SAME OUTPOST WHOSE TIMER CANNOT RUN
	// OUT. In `Run` the Void sent it; in `Control` nobody did. Ruled by the
	// coordinating session on 2026-10-16: a Void Siege's daily bite stays damage.
	UCataclysmEmpireRun* Run = MakeEmptyRun(1);
	UCataclysmEmpireRun* Control = MakeEmptyRun(1);

	const int32 CityId = FirstStandingOutpost(*Run);
	if (!TestTrue(TEXT("set-up: the map has an Outpost standing"), CityId != INDEX_NONE)
		|| !TestEqual(TEXT("set-up: and it is the same Outpost in the control"),
					  FirstStandingOutpost(*Control), CityId))
	{
		return false;
	}

	for (UCataclysmEmpireRun* Each : { Run, Control })
	{
		PlaceDungeon(*Each, SiegeId, ECataclysmDungeonType::Basic,
					 ECataclysmDungeonSubType::Siege, *Each->Map->Find(CityId),
					 EmptyBoardNeverDays);
		GiveDamageAndDaysLeft(*Each, SiegeId, 100.0f, 40.0f, EmptyBoardNeverDays);
	}

	SendFrom(*Run, SiegeId, ECataclysmType::Void, 0);

	const FCataclysmCity* City = Run->Map->Find(CityId);
	const FCataclysmCity* ControlCity = Control->Map->Find(CityId);
	if (!TestNotNull(TEXT("set-up: the city is on the map"), City)
		|| !TestNotNull(TEXT("set-up: and the control's"), ControlCity))
	{
		return false;
	}

	const float DefenceBefore = City->Defence;
	const float PopulationBefore = City->Population;
	const float ControlDefenceBefore = ControlCity->Defence;
	const float ControlPopulationBefore = ControlCity->Population;

	const FCataclysmDayReport Report = Run->AdvanceDay();
	Control->AdvanceDay();

	TestEqual(TEXT("set-up: no timer ran out"), Report.Resolved.Num(), 0);
	TestTrue(TEXT("the Void Siege bit its city"), Report.Besieged.Contains(CityId));
	TestTrue(TEXT("taking defence"), City->Defence < DefenceBefore);
	TestEqual(TEXT("and adding no stacks"), City->VoidStacks, 0);

	// THE CONTROL: EXACTLY WHAT A SIEGE NOBODY SENT TAKES.
	TestEqual(TEXT("CONTROL: the same defence a Siege nobody sent takes"),
			  DefenceBefore - City->Defence, ControlDefenceBefore - ControlCity->Defence,
			  0.0001f);
	TestEqual(TEXT("CONTROL: and the same people"),
			  PopulationBefore - City->Population,
			  ControlPopulationBefore - ControlCity->Population, 0.0001f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
