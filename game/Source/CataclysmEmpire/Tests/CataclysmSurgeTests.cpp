// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmCityUpgrade.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"

/**
 * Tests for the surge scheduler, issue #1083.
 *
 * WHAT THESE PIN AND WHAT `tools/tests/test_surge_port.py` PINS. These check the
 * behaviour: that an accelerating run really does shorten its gap and stop at a
 * floor, that Heretic really does bring more dungeons even at the cap, that a
 * wave lands only on the frontier. The Python test checks that the constants
 * still match `sim/cataclysm_sim/config.py`. Neither can notice what the other
 * does. `CataclysmDayClockTests.cpp` and `CataclysmEmpireMapTests.cpp` beside
 * this are in the same arrangement.
 *
 * WHERE THE EXPECTED FIGURES COME FROM. They were read out of
 * `Simulation.surge_count` and `Simulation.surge_gap` by running them at the
 * surge indices below, not worked out here.
 */

namespace CataclysmSurgeTest
{
	UCataclysmSurgeScheduler* MakeScheduler(ECataclysmSurgeMode Mode,
											int32 LethalityRung = 0)
	{
		UCataclysmSurgeScheduler* Scheduler = NewObject<UCataclysmSurgeScheduler>();
		Scheduler->Mode = Mode;
		Scheduler->LethalityRung = LethalityRung;
		return Scheduler;
	}

	UCataclysmEmpireMap* MakeMap()
	{
		UCataclysmEmpireMap* Map = NewObject<UCataclysmEmpireMap>();
		Map->Build();
		return Map;
	}

	/** What a scheduler answers once it has fired this many surges. */
	int32 CountAtIndex(UCataclysmSurgeScheduler* Scheduler, int32 Index)
	{
		Scheduler->SurgeIndex = Index;
		return Scheduler->DungeonsInNextSurge();
	}

	float GapAtIndex(UCataclysmSurgeScheduler* Scheduler, int32 Index)
	{
		Scheduler->SurgeIndex = Index;
		return Scheduler->GapAfterThisSurge();
	}
}

// ---------------------------------------------------------------------------
// Cadence
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeStaticTest,
	"Cataclysm.Surge.AStaticSurgeBringsTheSameWaveForEver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeStaticTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	UCataclysmSurgeScheduler* Scheduler = MakeScheduler(ECataclysmSurgeMode::Static);

	// THE DEFAULT MODE, AND THE ONE THE OTHER THREE ARE MEASURED AGAINST. How
	// surges escalate is an open tuning question; this is what "no escalation"
	// has to mean for the sweep comparing them to say anything.
	for (const int32 Index : { 0, 1, 5, 20, 100 })
	{
		TestEqual(FString::Printf(
			TEXT("surge %d still brings four dungeons"), Index),
			CountAtIndex(Scheduler, Index), 4);
		TestEqual(FString::Printf(
			TEXT("and the gap after surge %d is still 120 days"), Index),
			GapAtIndex(Scheduler, Index), 120.0f, 0.001f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeAcceleratingTest,
	"Cataclysm.Surge.AnAcceleratingSurgeShortensTheGapDownToAFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeAcceleratingTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	UCataclysmSurgeScheduler* Scheduler =
		MakeScheduler(ECataclysmSurgeMode::Accelerating);

	// HAND-WORKED FROM `Simulation.surge_gap`: 120 x 0.88 to the power of the
	// surge index, floored at 25.
	TestEqual(TEXT("the first gap is the full 120 days"),
			  GapAtIndex(Scheduler, 0), 120.0f, 0.001f);
	TestEqual(TEXT("the second is 105.6"),
			  GapAtIndex(Scheduler, 1), 105.6f, 0.01f);
	TestEqual(TEXT("the third is 92.928"),
			  GapAtIndex(Scheduler, 2), 92.928f, 0.01f);
	TestEqual(TEXT("the fourth is 81.7766"),
			  GapAtIndex(Scheduler, 3), 81.7766f, 0.01f);

	// AND IT STOPS. Without a floor an accelerating run ends in a surge every
	// day, which is arithmetic running away rather than difficulty.
	TestEqual(TEXT("by the thirteenth it is 25.88, just above the floor"),
			  GapAtIndex(Scheduler, 12), 25.8805f, 0.01f);
	TestEqual(TEXT("the fourteenth is the floor itself"),
			  GapAtIndex(Scheduler, 13), 25.0f, 0.001f);
	TestEqual(TEXT("and it never goes below it"),
			  GapAtIndex(Scheduler, 50), 25.0f, 0.001f);
	TestEqual(TEXT("nor after a hundred surges"),
			  GapAtIndex(Scheduler, 100), 25.0f, 0.001f);

	// THE WAVE ITSELF DOES NOT GROW IN THIS MODE. That is the whole difference
	// between accelerating and swelling, and a test that only checked the gap
	// would pass if the two modes had been swapped.
	TestEqual(TEXT("an accelerating surge still brings four dungeons"),
			  CountAtIndex(Scheduler, 0), 4);
	TestEqual(TEXT("and still four after twenty surges"),
			  CountAtIndex(Scheduler, 20), 4);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeSwellingTest,
	"Cataclysm.Surge.ASwellingSurgeGrowsTheWaveUpToACeiling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeSwellingTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	UCataclysmSurgeScheduler* Scheduler =
		MakeScheduler(ECataclysmSurgeMode::Swelling);

	// HALF A DUNGEON PER SURGE, TRUNCATED, so it grows every other surge rather
	// than every one. 4, 4.5, 5, 5.5, 6 as a real number.
	TestEqual(TEXT("the first wave is four"), CountAtIndex(Scheduler, 0), 4);
	TestEqual(TEXT("the second is still four"), CountAtIndex(Scheduler, 1), 4);
	TestEqual(TEXT("the third is five"), CountAtIndex(Scheduler, 2), 5);
	TestEqual(TEXT("the fourth is still five"), CountAtIndex(Scheduler, 3), 5);
	TestEqual(TEXT("the fifth is six"), CountAtIndex(Scheduler, 4), 6);

	// AND IT STOPS AT FOURTEEN.
	TestEqual(TEXT("by the twentieth it is thirteen"),
			  CountAtIndex(Scheduler, 19), 13);
	TestEqual(TEXT("the twenty-first is the ceiling"),
			  CountAtIndex(Scheduler, 20), 14);
	TestEqual(TEXT("and it never goes above it"),
			  CountAtIndex(Scheduler, 21), 14);
	TestEqual(TEXT("nor after a hundred surges"),
			  CountAtIndex(Scheduler, 100), 14);

	// THE GAP DOES NOT SHORTEN IN THIS MODE.
	TestEqual(TEXT("a swelling surge still comes every 120 days"),
			  GapAtIndex(Scheduler, 20), 120.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeBothTest,
	"Cataclysm.Surge.BothEscalationsRunTogetherAndNeitherIsLost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeBothTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	UCataclysmSurgeScheduler* Scheduler = MakeScheduler(ECataclysmSurgeMode::Both);

	// THE SAME FIGURES AS EACH MODE ON ITS OWN. A fourth mode that quietly did
	// only one of the two would look like escalation and be half of it.
	TestEqual(TEXT("the gap shortens like an accelerating surge"),
			  GapAtIndex(Scheduler, 1), 105.6f, 0.01f);
	TestEqual(TEXT("down to the same floor"),
			  GapAtIndex(Scheduler, 13), 25.0f, 0.001f);
	TestEqual(TEXT("and the wave grows like a swelling one"),
			  CountAtIndex(Scheduler, 4), 6);
	TestEqual(TEXT("up to the same ceiling"),
			  CountAtIndex(Scheduler, 20), 14);

	// SO THE WORST IT EVER GETS IS FOURTEEN DUNGEONS EVERY TWENTY-FIVE DAYS.
	Scheduler->SurgeIndex = 100;
	TestEqual(TEXT("at its worst it is fourteen dungeons"),
			  Scheduler->DungeonsInNextSurge(), 14);
	TestEqual(TEXT("every twenty-five days"),
			  Scheduler->GapAfterThisSurge(), 25.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeHereticTest,
	"Cataclysm.Surge.HereticBringsAQuarterMoreDungeonsEvenAtTheCeiling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeHereticTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	// 0 STANDARD, 1 HARDCORE, 2 HERETIC. Only Heretic changes a wave; Hardcore
	// changes what dying costs and not what the Cataclysm sends.
	UCataclysmSurgeScheduler* Standard =
		MakeScheduler(ECataclysmSurgeMode::Static, 0);
	UCataclysmSurgeScheduler* Hardcore =
		MakeScheduler(ECataclysmSurgeMode::Static, 1);
	UCataclysmSurgeScheduler* Heretic =
		MakeScheduler(ECataclysmSurgeMode::Static, 2);

	TestEqual(TEXT("Standard brings four"), Standard->DungeonsInNextSurge(), 4);
	TestEqual(TEXT("Hardcore brings four as well"),
			  Hardcore->DungeonsInNextSurge(), 4);
	TestEqual(TEXT("and Heretic brings five"), Heretic->DungeonsInNextSurge(), 5);

	// THE ORDER IS THE WHOLE POINT, AND THIS IS THE ASSERTION THAT PROVES IT.
	// The ceiling is 14. Multiplying by 1.25 BEFORE the ceiling gives
	// min(17.5, 14) = 14, which is what Standard gets, so Heretic would stop
	// being harder at exactly the surge where the extra dungeons hurt most.
	// After the ceiling it gives 17.
	UCataclysmSurgeScheduler* SwellingStandard =
		MakeScheduler(ECataclysmSurgeMode::Swelling, 0);
	UCataclysmSurgeScheduler* SwellingHeretic =
		MakeScheduler(ECataclysmSurgeMode::Swelling, 2);

	SwellingStandard->SurgeIndex = 20;
	SwellingHeretic->SurgeIndex = 20;

	TestEqual(TEXT("a swelling Standard run tops out at fourteen"),
			  SwellingStandard->DungeonsInNextSurge(), 14);
	TestEqual(TEXT("and a swelling Heretic run at seventeen, not fourteen"),
			  SwellingHeretic->DungeonsInNextSurge(), 17);

	// AND THE MODE DOES NOT TOUCH THE GAP.
	TestEqual(TEXT("Heretic waves come no more often"),
			  Heretic->GapAfterThisSurge(), Standard->GapAfterThisSurge(), 0.001f);

	return true;
}

// ---------------------------------------------------------------------------
// The schedule
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeScheduleTest,
	"Cataclysm.Surge.ASurgeFiresAtRunStartAndThenOnTheClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeScheduleTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	UCataclysmSurgeScheduler* Scheduler = MakeScheduler(ECataclysmSurgeMode::Static);

	// "A SURGE IS TRIGGERED AT RUN START", so day 0 is already due.
	TestTrue(TEXT("a surge is due on day 0"), Scheduler->IsDue(0));
	TestEqual(TEXT("with no days left to wait"),
			  Scheduler->DaysUntilNextSurge(0), 0.0f, 0.001f);
	TestEqual(TEXT("and none have fired yet"), Scheduler->SurgesFired, 0);

	Scheduler->RecordSurge(0);

	TestEqual(TEXT("one has fired"), Scheduler->SurgesFired, 1);
	TestEqual(TEXT("and it counts towards escalation"), Scheduler->SurgeIndex, 1);
	TestEqual(TEXT("the next is due on day 120"),
			  Scheduler->NextSurgeDay, 120.0f, 0.001f);

	TestFalse(TEXT("nothing is due on day 1"), Scheduler->IsDue(1));
	TestEqual(TEXT("with 119 days to wait"),
			  Scheduler->DaysUntilNextSurge(1), 119.0f, 0.001f);
	TestFalse(TEXT("nor on day 119"), Scheduler->IsDue(119));
	TestTrue(TEXT("and it is due on day 120"), Scheduler->IsDue(120));

	// A LATE CALLER IS NOT PUNISHED FOR BEING LATE. The next surge is set from
	// the day it actually fired, not from the day it was due, so a run that
	// skipped several days at once does not immediately owe several surges.
	Scheduler->RecordSurge(130);
	TestEqual(TEXT("firing late puts the next one 120 days after the firing"),
			  Scheduler->NextSurgeDay, 250.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeCityFallTest,
	"Cataclysm.Surge.ACityFallingFiresASurgeAndPermanentlySpeedsTheRunUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeCityFallTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	// THE RULE IS RECORDED IN TWO CONSTANTS AND THIS IS WHAT MAKES THEM
	// LOAD-BEARING. A constant nothing reads can be changed with no effect and
	// no warning.
	TestTrue(TEXT("a city falling fires a surge"),
			 UCataclysmSurgeScheduler::bSurgeOnCityFall);
	TestTrue(TEXT("and that surge escalates like any other"),
			 UCataclysmSurgeScheduler::bCityFallAdvancesEscalation);

	UCataclysmSurgeScheduler* Scheduler =
		MakeScheduler(ECataclysmSurgeMode::Accelerating);

	Scheduler->RecordSurge(0);
	TestEqual(TEXT("the clock's own surge advances the counter"),
			  Scheduler->SurgeIndex, 1);

	Scheduler->RecordSurge(10, /* bFromCityFall */ true);

	TestEqual(TEXT("two waves have landed"), Scheduler->SurgesFired, 2);
	TestEqual(TEXT("and the fall advanced the counter too"),
			  Scheduler->SurgeIndex, 2);

	// SO LOSING A CITY DOES NOT ONLY COST THAT CITY. In an accelerating run it
	// permanently shortens every gap that follows, which is what makes a bad run
	// get worse rather than merely stay bad.
	TestTrue(TEXT("and the gaps after it are shorter than the first one was"),
			 Scheduler->GapAfterThisSurge()
			 < UCataclysmSurgeScheduler::IntervalDays);

	return true;
}

// ---------------------------------------------------------------------------
// The wave
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeFrontierTest,
	"Cataclysm.Surge.AWaveLandsOnlyOnTheExposedFrontierAndNeverOnThePillar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeFrontierTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	UCataclysmSurgeScheduler* Scheduler = MakeScheduler(ECataclysmSurgeMode::Static);
	UCataclysmEmpireMap* Map = MakeMap();

	FRandomStream Stream(12345);

	// FIVE HUNDRED ROLLS, NOT FIVE. A wave of four could miss a sealed city by
	// luck; the point is that it cannot reach one at all.
	const TArray<int32> Landed = Scheduler->PickTargets(*Map, 500, Stream);

	TestEqual(TEXT("five hundred dungeons were placed"), Landed.Num(), 500);

	TSet<int32> Hit;
	for (const int32 CityId : Landed)
	{
		Hit.Add(CityId);

		TestTrue(FString::Printf(
			TEXT("city %d was exposed when the wave landed on it"), CityId),
			Map->IsExposed(CityId));
	}

	TestFalse(TEXT("the Pillar was never hit"), Hit.Contains(Map->PillarId));
	TestEqual(TEXT("and every one of the twelve rim Outposts was"), Hit.Num(), 12);

	// ONCE A LANE OPENS, WHAT IS BEHIND IT BECOMES A TARGET. Outpost 0 falls and
	// Bulwark 2 behind it is now reachable; nothing else has changed.
	Map->Fall(0);

	const TArray<int32> After = Scheduler->PickTargets(*Map, 500, Stream);

	TSet<int32> HitAfter;
	for (const int32 CityId : After)
	{
		HitAfter.Add(CityId);
		TestTrue(FString::Printf(
			TEXT("city %d was exposed when the second wave landed on it"), CityId),
			Map->IsExposed(CityId));
	}

	TestTrue(TEXT("the Bulwark behind the breach is now hit"),
			 HitAfter.Contains(2));
	TestFalse(TEXT("the fallen Outpost is not hit again"),
			  HitAfter.Contains(0));
	TestFalse(TEXT("and a Bulwark elsewhere is still out of reach"),
			  HitAfter.Contains(5));

	// AND AN OUTPOST IS STILL PREFERRED TO THE BULWARK BEHIND IT. Weight 5
	// against weight 3, over eleven Outposts and one Bulwark, so the Bulwark
	// should be a small share rather than a twelfth.
	int32 BulwarkHits = 0;
	for (const int32 CityId : After)
	{
		if (CityId == 2)
		{
			++BulwarkHits;
		}
	}

	TestTrue(FString::Printf(
		TEXT("the Bulwark took %d of 500, which is a small share rather than "
			 "an even one"), BulwarkHits),
		BulwarkHits > 0 && BulwarkHits < 500 / 8);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeSeedTest,
	"Cataclysm.Surge.TheSameSeedGivesTheSameWave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeSeedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	UCataclysmSurgeScheduler* Scheduler = MakeScheduler(ECataclysmSurgeMode::Static);
	UCataclysmEmpireMap* Map = MakeMap();

	FRandomStream First(7);
	FRandomStream Again(7);
	FRandomStream Different(8);

	const TArray<FCataclysmDungeon> WaveOne = Scheduler->RollWave(*Map, 0, 0, First);
	const TArray<FCataclysmDungeon> WaveTwo = Scheduler->RollWave(*Map, 0, 0, Again);
	const TArray<FCataclysmDungeon> WaveElse =
		Scheduler->RollWave(*Map, 0, 0, Different);

	if (!TestEqual(TEXT("a static wave is four dungeons"), WaveOne.Num(), 4))
	{
		return false;
	}

	TestEqual(TEXT("and the same seed gives the same number"),
			  WaveTwo.Num(), WaveOne.Num());

	// EVERY FIELD, NOT JUST THE CITY. A reproducible wave that rolled different
	// depths would still be a different problem to solve.
	for (int32 Index = 0; Index < WaveOne.Num(); ++Index)
	{
		TestEqual(FString::Printf(TEXT("dungeon %d lands on the same city"), Index),
				  WaveTwo[Index].CityId, WaveOne[Index].CityId);
		TestEqual(FString::Printf(TEXT("dungeon %d is the same depth"), Index),
				  WaveTwo[Index].Floors, WaveOne[Index].Floors);
		TestEqual(FString::Printf(TEXT("dungeon %d has the same timer"), Index),
				  WaveTwo[Index].ResolveDays, WaveOne[Index].ResolveDays, 0.0001f);
		TestEqual(FString::Printf(TEXT("dungeon %d is numbered from the first"),
								  Index),
				  WaveOne[Index].DungeonId, Index);
	}

	// AND A DIFFERENT SEED GIVES A DIFFERENT WAVE. Without this the test above
	// would pass on a scheduler that ignored the stream entirely.
	bool bAnythingDiffers = false;
	for (int32 Index = 0; Index < WaveOne.Num() && Index < WaveElse.Num(); ++Index)
	{
		bAnythingDiffers = bAnythingDiffers
			|| WaveElse[Index].CityId != WaveOne[Index].CityId
			|| WaveElse[Index].Floors != WaveOne[Index].Floors;
	}

	TestTrue(TEXT("a different seed gives a different wave"), bAnythingDiffers);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeDungeonDepthTest,
	"Cataclysm.Surge.ABiggerCityBreedsADeeperDungeon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeDungeonDepthTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	// HAND-WORKED FROM `config.DUNGEON_SPECS`. The Python guard beside this
	// checks these against the simulation; these check the scheduler hands them
	// out and that a rolled dungeon stays inside them.
	const FCataclysmDungeonSpec Outpost = UCataclysmSurgeScheduler::SpecFor(
		ECataclysmDungeonType::Basic, ECataclysmCityTier::Outpost);
	const FCataclysmDungeonSpec Bulwark = UCataclysmSurgeScheduler::SpecFor(
		ECataclysmDungeonType::Basic, ECataclysmCityTier::Bulwark);
	const FCataclysmDungeonSpec Sanctuary = UCataclysmSurgeScheduler::SpecFor(
		ECataclysmDungeonType::Basic, ECataclysmCityTier::Sanctuary);
	const FCataclysmDungeonSpec Pillar = UCataclysmSurgeScheduler::SpecFor(
		ECataclysmDungeonType::Basic, ECataclysmCityTier::Pillar);

	TestEqual(TEXT("an Outpost dungeon is 8 to 15 floors"), Outpost.LeastFloors, 8);
	TestEqual(TEXT("at most"), Outpost.MostFloors, 15);
	TestEqual(TEXT("a Bulwark dungeon is 15 to 25"), Bulwark.LeastFloors, 15);
	TestEqual(TEXT("at most"), Bulwark.MostFloors, 25);
	TestEqual(TEXT("a Sanctuary dungeon is 25 to 40"), Sanctuary.LeastFloors, 25);
	TestEqual(TEXT("at most"), Sanctuary.MostFloors, 40);
	TestEqual(TEXT("and a Pillar dungeon is 40 to 60"), Pillar.LeastFloors, 40);
	TestEqual(TEXT("at most"), Pillar.MostFloors, 60);

	// THE DAMAGE IS POINTS, AND THE FOUR NUMBERS ARE WRITTEN OUT. Issue #1331.
	// Each is the fraction it replaced multiplied by that tier's base maximum:
	// 10% of 1,000, 9% of 3,000, 8% of 8,000 and 6% of 20,000. Written here as
	// literals rather than as that product, because a test that recomputed the
	// product would agree with the code by construction.
	TestEqual(TEXT("an Outpost dungeon takes 100 defence points"),
			  Outpost.DefenceDamage, 100.0f);
	TestEqual(TEXT("and 250 people"), Outpost.PopulationDamage, 250.0f);
	TestEqual(TEXT("a Bulwark dungeon takes 270"), Bulwark.DefenceDamage, 270.0f);
	TestEqual(TEXT("and 1,000 people"), Bulwark.PopulationDamage, 1000.0f);
	TestEqual(TEXT("a Sanctuary dungeon takes 640"), Sanctuary.DefenceDamage,
			  640.0f);
	TestEqual(TEXT("and 2,400 people"), Sanctuary.PopulationDamage, 2400.0f);
	TestEqual(TEXT("and a Pillar dungeon takes 1,200"), Pillar.DefenceDamage,
			  1200.0f);
	TestEqual(TEXT("and 4,500 people"), Pillar.PopulationDamage, 4500.0f);

	// A BIGGER CITY TAKES MORE POINTS AND A SMALLER SHARE OF ITSELF, and both
	// halves are asserted because each on its own would be satisfied by the
	// wrong table. Before issue #1331 the first half was false -- a Sanctuary
	// carried 0.08 against an Outpost's 0.10 -- so a test written against the
	// old field would now fail rather than silently reverse.
	TestTrue(TEXT("a Sanctuary dungeon takes more points than an Outpost one"),
			 Sanctuary.DefenceDamage > Outpost.DefenceDamage);

	const float OutpostShare =
		Outpost.DefenceDamage
		/ UCataclysmEmpireMap::MaxDefenceFor(ECataclysmCityTier::Outpost);

	const float SanctuaryShare =
		Sanctuary.DefenceDamage
		/ UCataclysmEmpireMap::MaxDefenceFor(ECataclysmCityTier::Sanctuary);

	TestTrue(*FString::Printf(
				 TEXT("a Sanctuary loses %.1f%% of itself per resolve and an "
					  "Outpost %.1f%%"),
				 SanctuaryShare * 100.0f, OutpostShare * 100.0f),
			 SanctuaryShare < OutpostShare);

	// ALL FOUR KINDS HAVE A SPEC NOW. Until issue #1324 the other three
	// answered a spec `IsBuilt` read as false, on the grounds that nothing
	// built them. Nothing builds them still -- `MakeDungeon` sets `Basic` on
	// every dungeon a surge lands -- but the numbers they would use are the
	// model's and have been settled all along, and the work that creates them
	// needs them answered first.
	TestTrue(TEXT("a basic dungeon has a spec"), Outpost.IsBuilt());
	TestTrue(TEXT("so does a quest dungeon"),
			 UCataclysmSurgeScheduler::SpecFor(
				 ECataclysmDungeonType::Quest,
				 ECataclysmCityTier::Outpost).IsBuilt());
	TestTrue(TEXT("and a fallen city"),
			 UCataclysmSurgeScheduler::SpecFor(
				 ECataclysmDungeonType::FallenCity,
				 ECataclysmCityTier::Bulwark).IsBuilt());
	TestTrue(TEXT("and the Cataclysm boss dungeon"),
			 UCataclysmSurgeScheduler::SpecFor(
				 ECataclysmDungeonType::Cataclysm,
				 ECataclysmCityTier::Pillar).IsBuilt());

	// AND THE CATACLYSM STILL HAS NO SPEC ANYWHERE BUT THE PILLAR. The model
	// holds one Cataclysm row and `TuningConfig.spec` raises when asked for
	// another, so answering a plausible number for an Outpost would be
	// inventing one. All three lesser tiers are checked, not just one.
	for (const ECataclysmCityTier Lesser : { ECataclysmCityTier::Outpost,
											 ECataclysmCityTier::Bulwark,
											 ECataclysmCityTier::Sanctuary })
	{
		TestFalse(TEXT("no Cataclysm dungeon below the Pillar"),
				  UCataclysmSurgeScheduler::SpecFor(
					  ECataclysmDungeonType::Cataclysm, Lesser).IsBuilt());
	}

	// EVERY NEW KIND BITES NOTHING, asserted rather than assumed. A Quest
	// dungeon never resolves; a Fallen City and a Cataclysm stand on a city
	// whose damage is already done. A non-zero here would mean a city taking
	// damage from a dungeon the design says takes none.
	for (const ECataclysmDungeonType Kind : { ECataclysmDungeonType::Quest,
											  ECataclysmDungeonType::FallenCity,
											  ECataclysmDungeonType::Cataclysm })
	{
		const FCataclysmDungeonSpec Bites =
			UCataclysmSurgeScheduler::SpecFor(Kind, ECataclysmCityTier::Pillar);

		TestEqual(TEXT("it takes no defence"), Bites.DefenceDamage, 0.0f);
		TestEqual(TEXT("and no population"), Bites.PopulationDamage, 0.0f);
	}

	// AND A DEEPER KIND IS DEEPER ON THE SAME CITY. The three ladders never
	// cross: on any one tier a Fallen City is deeper than a Quest, which is
	// deeper than a Basic, at both ends of the range. Checked on every tier
	// rather than on one, because the ranges OVERLAP -- a Sanctuary Basic is
	// 25 to 40 and a Sanctuary Quest is 30 to 50 -- so the stronger claim that
	// one range starts where another ends is false and must not be asserted.
	for (const ECataclysmCityTier Tier : { ECataclysmCityTier::Outpost,
										   ECataclysmCityTier::Bulwark,
										   ECataclysmCityTier::Sanctuary,
										   ECataclysmCityTier::Pillar })
	{
		const FCataclysmDungeonSpec Basic = UCataclysmSurgeScheduler::SpecFor(
			ECataclysmDungeonType::Basic, Tier);
		const FCataclysmDungeonSpec Quest = UCataclysmSurgeScheduler::SpecFor(
			ECataclysmDungeonType::Quest, Tier);
		const FCataclysmDungeonSpec Fallen = UCataclysmSurgeScheduler::SpecFor(
			ECataclysmDungeonType::FallenCity, Tier);

		TestTrue(TEXT("a quest dungeon is deeper than a basic one, both ends"),
				 Quest.LeastFloors > Basic.LeastFloors
				 && Quest.MostFloors > Basic.MostFloors);

		// AT LEAST AS DEEP AT THE SHALLOW END AND STRICTLY DEEPER AT THE DEEP
		// END. On an Outpost both start at 20, so the shallow end is not
		// strictly greater and asserting that it is would fail.
		TestTrue(TEXT("and a fallen city is deeper again"),
				 Fallen.LeastFloors >= Quest.LeastFloors
				 && Fallen.MostFloors > Quest.MostFloors);

		TestTrue(TEXT("and every range runs shallow to deep"),
				 Basic.LeastFloors <= Basic.MostFloors
				 && Quest.LeastFloors <= Quest.MostFloors
				 && Fallen.LeastFloors <= Fallen.MostFloors);
	}

	// AND A ROLLED DUNGEON STAYS INSIDE ITS RANGE. Two hundred rolls on each of
	// three tiers, checking both that nothing escapes and that the range is
	// actually used rather than one value being returned every time.
	UCataclysmSurgeScheduler* Scheduler = MakeScheduler(ECataclysmSurgeMode::Static);
	UCataclysmEmpireMap* Map = MakeMap();
	FRandomStream Stream(99);

	struct FTierCase
	{
		ECataclysmCityTier Tier;
		FCataclysmDungeonSpec Spec;
	};

	const FTierCase Cases[] = {
		{ ECataclysmCityTier::Outpost, Outpost },
		{ ECataclysmCityTier::Bulwark, Bulwark },
		{ ECataclysmCityTier::Sanctuary, Sanctuary },
	};

	for (const FTierCase& Case : Cases)
	{
		// Any city of that tier will do; the roll reads the tier, not the city.
		const FCataclysmCity* City = Map->Cities.FindByPredicate(
			[&Case](const FCataclysmCity& Candidate)
			{
				return Candidate.Tier == Case.Tier;
			});

		if (!TestNotNull(TEXT("a city of that tier exists"), City))
		{
			return false;
		}

		TSet<int32> Depths;
		for (int32 Roll = 0; Roll < 200; ++Roll)
		{
			const FCataclysmDungeon Dungeon =
				Scheduler->MakeDungeon(Roll, *City, 0, Stream);

			Depths.Add(Dungeon.Floors);

			TestTrue(FString::Printf(
				TEXT("a %s dungeon of %d floors is inside %d to %d"),
				*UCataclysmEmpireMap::TierName(Case.Tier), Dungeon.Floors,
				Case.Spec.LeastFloors, Case.Spec.MostFloors),
				Dungeon.Floors >= Case.Spec.LeastFloors
				&& Dungeon.Floors <= Case.Spec.MostFloors);
		}

		TestTrue(FString::Printf(
			TEXT("%s dungeons vary in depth: %d different ones in 200 rolls"),
			*UCataclysmEmpireMap::TierName(Case.Tier), Depths.Num()),
			Depths.Num() > 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeTimerTest,
	"Cataclysm.Surge.ADungeonsTimerComesFromItsDepthAndThenVaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeTimerTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	UCataclysmSurgeScheduler* Scheduler = MakeScheduler(ECataclysmSurgeMode::Static);
	UCataclysmEmpireMap* Map = MakeMap();
	FRandomStream Stream(4242);

	const FCataclysmCity* Outpost = Map->Find(0);
	if (!TestNotNull(TEXT("Outpost 0 exists"), Outpost))
	{
		return false;
	}

	TSet<int32> Timers;
	for (int32 Roll = 0; Roll < 200; ++Roll)
	{
		const FCataclysmDungeon Dungeon =
			Scheduler->MakeDungeon(Roll, *Outpost, 0, Stream);

		// THE FIGURE THE DEPTH GIVES, PLUS OR MINUS 15%. `ResolveDaysFor` is the
		// day clock's, so a dungeon's timer and the walk through it are set by
		// the same number.
		const float Base = UCataclysmDayClock::ResolveDaysFor(Dungeon.Floors);
		const float Least = Base * (1.0f - UCataclysmSurgeScheduler::ResolveJitter);
		const float Most = Base * (1.0f + UCataclysmSurgeScheduler::ResolveJitter);

		TestTrue(FString::Printf(
			TEXT("a %d-floor dungeon's timer of %.2f days is within 15%% of "
				 "%.2f"), Dungeon.Floors, Dungeon.ResolveDays, Base),
			Dungeon.ResolveDays >= Least - 0.001f
			&& Dungeon.ResolveDays <= Most + 0.001f);

		Timers.Add(FMath::RoundToInt(Dungeon.ResolveDays * 100.0f));
	}

	// AND THEY ARE NOT ALL THE SAME. Without the jitter every dungeon of a given
	// depth would come due on exactly the same day as every other, and triage
	// would be arithmetic rather than a judgement.
	TestTrue(FString::Printf(
		TEXT("200 rolls gave %d different timers"), Timers.Num()),
		Timers.Num() > 100);

	// A DEEPER DUNGEON IS ALWAYS SLOWER TO BITE THAN A SHALLOW ONE, even at the
	// extremes of the jitter. 15% is small enough that the ranges of an
	// 8-floor and a 40-floor dungeon cannot overlap.
	const float ShallowMost = UCataclysmDayClock::ResolveDaysFor(8)
		* (1.0f + UCataclysmSurgeScheduler::ResolveJitter);
	const float DeepLeast = UCataclysmDayClock::ResolveDaysFor(40)
		* (1.0f - UCataclysmSurgeScheduler::ResolveJitter);

	TestTrue(FString::Printf(
		TEXT("the slowest 8-floor timer, %.2f days, is still shorter than the "
			 "fastest 40-floor one, %.2f"), ShallowMost, DeepLeast),
		ShallowMost < DeepLeast);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeBiteScaleTest,
	"Cataclysm.Surge.ADeeperDungeonTakesABiggerBiteOutOfItsCity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeBiteScaleTest::RunTest(const FString& Parameters)
{
	// AN OUTPOST DUNGEON IS 8 TO 15 FLOORS, so a typical one is 11.5.
	FCataclysmDungeon Shallow;
	Shallow.CityTier = ECataclysmCityTier::Outpost;
	Shallow.Floors = 8;

	FCataclysmDungeon Typical;
	Typical.CityTier = ECataclysmCityTier::Outpost;
	Typical.Floors = 12;

	FCataclysmDungeon Deep;
	Deep.CityTier = ECataclysmCityTier::Outpost;
	Deep.Floors = 15;

	TestEqual(TEXT("the shallowest takes 8 over 11.5 of its type's bite"),
			  Shallow.BiteScale(), 8.0f / 11.5f, 0.001f);
	TestEqual(TEXT("a typical one takes about all of it"),
			  Typical.BiteScale(), 12.0f / 11.5f, 0.001f);
	TestEqual(TEXT("and the deepest takes 15 over 11.5"),
			  Deep.BiteScale(), 15.0f / 11.5f, 0.001f);

	TestTrue(TEXT("so the shallowest takes less than its type's full bite"),
			 Shallow.BiteScale() < 1.0f);
	TestTrue(TEXT("and the deepest takes more"), Deep.BiteScale() > 1.0f);

	// THE SCALE IS AGAINST ITS OWN TIER AND NOT AGAINST SOME ABSOLUTE DEPTH. A
	// 25-floor dungeon is shallow for a Sanctuary and deep for a Bulwark, so it
	// scales differently on each.
	FCataclysmDungeon OnABulwark;
	OnABulwark.CityTier = ECataclysmCityTier::Bulwark;
	OnABulwark.Floors = 25;

	FCataclysmDungeon OnASanctuary;
	OnASanctuary.CityTier = ECataclysmCityTier::Sanctuary;
	OnASanctuary.Floors = 25;

	TestTrue(TEXT("25 floors is deep for a Bulwark"),
			 OnABulwark.BiteScale() > 1.0f);
	TestTrue(TEXT("and shallow for a Sanctuary"),
			 OnASanctuary.BiteScale() < 1.0f);

	return true;
}

// ---------------------------------------------------------------------------
// What a city's own upgrades do to the dungeons it receives
// ---------------------------------------------------------------------------

namespace CataclysmSurgeTest
{
	/** Gives a city an upgrade, without going through a purchase. */
	void Give(UCataclysmEmpireMap& Map, int32 CityId,
			  ECataclysmCityUpgradeEffect Effect, float Value)
	{
		FCataclysmCityUpgrade Upgrade;
		Upgrade.RowName = FName(*UCataclysmCityUpgradeRules::EffectName(Effect));
		Upgrade.Effect = Effect;
		Upgrade.Value = Value;

		Map.AddUpgrade(CityId, Upgrade);
	}

	/** How many dungeons stand on each city, for `PickTargets`. */
	TArray<int32> NoDungeonsAnywhere(const UCataclysmEmpireMap& Map)
	{
		TArray<int32> Counts;
		Counts.AddZeroed(Map.Cities.Num());
		return Counts;
	}

	/**
	 * The first `Wanted` cities of one tier.
	 *
	 * FOUND RATHER THAN WRITTEN OUT. City identifiers are handed out in lattice
	 * scan order, row by row, so the rim is not cities 0 to 11 and picking a
	 * block of low identifiers gives a mixture of tiers. An earlier version of
	 * the test below assumed cities 0 to 4 were all Outposts; city 2 is a
	 * Bulwark.
	 */
	TArray<int32> CitiesAtTier(const UCataclysmEmpireMap& Map,
							   ECataclysmCityTier Tier, int32 Wanted)
	{
		TArray<int32> Found;

		for (const FCataclysmCity& City : Map.Cities)
		{
			if (City.Tier == Tier && Found.Num() < Wanted)
			{
				Found.Add(City.CityId);
			}
		}

		return Found;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeFloorUpgradeTest,
	"Cataclysm.Surge.AFloorUpgradeMovesTheDepthAndTheTimerTogether",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeFloorUpgradeTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	UCataclysmSurgeScheduler* Scheduler =
		MakeScheduler(ECataclysmSurgeMode::Static);
	UCataclysmEmpireMap* Map = MakeMap();

	// FIVE CITIES OF THE SAME TIER, so what differs between the dungeons rolled
	// for them is the upgrade and not the city. Found rather than written out:
	// city identifiers run in lattice scan order, so a block of low identifiers
	// is a mixture of tiers.
	const TArray<int32> Rim =
		CitiesAtTier(*Map, ECataclysmCityTier::Outpost, 5);

	if (!TestEqual(TEXT("five Outposts were found to compare"), Rim.Num(), 5))
	{
		return false;
	}

	Give(*Map, Rim[1], ECataclysmCityUpgradeEffect::DungeonFloorsMore, 5.0f);
	Give(*Map, Rim[2], ECataclysmCityUpgradeEffect::DungeonFloorsFewer, 5.0f);
	Give(*Map, Rim[3], ECataclysmCityUpgradeEffect::DungeonFloorsMore, 5.0f);
	Give(*Map, Rim[3], ECataclysmCityUpgradeEffect::DungeonFloorsFewer, 2.0f);

	// FOUR STREAMS AT THE SAME SEED, so every one of the four dungeons is rolled
	// from the same two draws and the only difference between them is the
	// upgrade.
	FRandomStream A(9001), B(9001), C(9001), D(9001);

	const FCataclysmDungeon Plain =
		Scheduler->MakeDungeon(0, *Map->Find(Rim[0]), 1, A);
	const FCataclysmDungeon Deeper =
		Scheduler->MakeDungeon(1, *Map->Find(Rim[1]), 1, B);
	const FCataclysmDungeon Shallower =
		Scheduler->MakeDungeon(2, *Map->Find(Rim[2]), 1, C);
	const FCataclysmDungeon Both =
		Scheduler->MakeDungeon(3, *Map->Find(Rim[3]), 1, D);

	// THE CONTROL. If the plain dungeon were already near the floor of one,
	// taking five off would be clamped and nothing below would mean anything.
	if (!TestTrue(TEXT("the plain dungeon is deep enough to shorten"),
				  Plain.Floors > 6))
	{
		return false;
	}

	TestEqual(TEXT("five more floors is five more floors"), Deeper.Floors,
			  Plain.Floors + 5);
	TestEqual(TEXT("five fewer is five fewer"), Shallower.Floors,
			  Plain.Floors - 5);
	TestEqual(TEXT("and holding both gives the difference"), Both.Floors,
			  Plain.Floors + 3);

	// AND THE TIMER FOLLOWED THE DEPTH. Depth and reward are the same axis, so a
	// deeper dungeon is worth more and slower to bite. Checked as a ratio, which
	// also proves the jitter draw was the same for both: a different jitter would
	// break the ratio even if the depth were right.
	const float PlainBase = UCataclysmDayClock::ResolveDaysFor(Plain.Floors);
	const float DeeperBase = UCataclysmDayClock::ResolveDaysFor(Deeper.Floors);

	TestEqual(TEXT("the deeper dungeon's timer grew in step with its depth"),
			  Deeper.ResolveDays / Plain.ResolveDays, DeeperBase / PlainBase,
			  0.0001f);

	TestTrue(TEXT("so a deeper dungeon takes longer to bite"),
			 Deeper.ResolveDays > Plain.ResolveDays);
	TestTrue(TEXT("and a shallower one bites sooner"),
			 Shallower.ResolveDays < Plain.ResolveDays);

	// A DUNGEON NEVER HAS FEWER THAN ONE FLOOR, which is what "to a minimum of 1"
	// in the sheet says and what would otherwise be an unwalkable dungeon.
	Give(*Map, Rim[4], ECataclysmCityUpgradeEffect::DungeonFloorsFewer, 500.0f);

	FRandomStream E(9001);
	const FCataclysmDungeon Floored =
		Scheduler->MakeDungeon(4, *Map->Find(Rim[4]), 1, E);

	TestEqual(TEXT("five hundred fewer floors still leaves one"),
			  Floored.Floors, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeWalkDaysTest,
	"Cataclysm.Surge.AQuickerDungeonKeepsItsFloorsItsRewardAndItsTimer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeWalkDaysTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	UCataclysmSurgeScheduler* Scheduler =
		MakeScheduler(ECataclysmSurgeMode::Static);
	UCataclysmEmpireMap* Map = MakeMap();

	const TArray<int32> Rim =
		CitiesAtTier(*Map, ECataclysmCityTier::Outpost, 2);

	if (!TestEqual(TEXT("two Outposts were found to compare"), Rim.Num(), 2))
	{
		return false;
	}

	Give(*Map, Rim[1], ECataclysmCityUpgradeEffect::DungeonWalkDaysFewer, 4.0f);

	FRandomStream A(5150), B(5150);

	const FCataclysmDungeon Plain =
		Scheduler->MakeDungeon(0, *Map->Find(Rim[0]), 1, A);
	const FCataclysmDungeon Quicker =
		Scheduler->MakeDungeon(1, *Map->Find(Rim[1]), 1, B);

	if (!TestTrue(TEXT("the plain dungeon is deep enough to shorten"),
				  Plain.Floors > 6))
	{
		return false;
	}

	// THE OTHER CONTROL. A Cow Level's walk is doubled and cannot be reduced, so
	// this seed rolling one would make both dungeons cost the same and every
	// figure below meaningless. Both are rolled from the same seed and so get the
	// same sub-type; checking one is checking both. If this ever fails, change
	// the seed rather than the assertions -- the doubling has its own test.
	if (!TestTrue(TEXT("the seed did not roll a Cow Level, whose walk is fixed"),
				  Plain.SubType != ECataclysmDungeonSubType::CowLevel))
	{
		return false;
	}

	// THIS IS THE WHOLE POINT OF THE UPGRADE. It buys time and gives up nothing:
	// the dungeon is the same depth, so it is worth the same and it bites on the
	// same schedule. `DungeonFloorsFewer` beside it buys the same speed by giving
	// up all three, which is what makes them different upgrades rather than one
	// upgrade written twice.
	TestEqual(TEXT("the floor count did not move"), Quicker.Floors,
			  Plain.Floors);

	TestEqual(TEXT("so the resolve timer did not move either"),
			  Quicker.ResolveDays, Plain.ResolveDays, 0.0001f);

	TestEqual(TEXT("and its bite is unchanged"), Quicker.BiteScale(),
			  Plain.BiteScale(), 0.0001f);

	// WHAT DID MOVE IS THE TIME TO WALK IT.
	TestEqual(TEXT("a plain dungeon costs one day a floor"), Plain.WalkDays,
			  Plain.Floors * UCataclysmDayClock::DaysPerFloor, 0.0001f);

	TestEqual(TEXT("and the upgraded one costs four days fewer"),
			  Quicker.WalkDays, Plain.WalkDays - 4.0f, 0.0001f);

	TestTrue(TEXT("so each of its floors costs less than a day"),
			 Quicker.WalkDaysPerFloor() < Plain.WalkDaysPerFloor());

	// A DUNGEON NEVER COSTS LESS THAN A DAY IN TOTAL, which the sheet states.
	// One that cost nothing would be free, and the empire layer's whole tension
	// is that nothing is.
	Give(*Map, Rim[0], ECataclysmCityUpgradeEffect::DungeonWalkDaysFewer,
		 500.0f);

	FRandomStream C(5150);
	const FCataclysmDungeon Floored =
		Scheduler->MakeDungeon(2, *Map->Find(Rim[0]), 1, C);

	TestEqual(TEXT("five hundred days off still leaves one"), Floored.WalkDays,
			  1.0f, 0.0001f);
	TestEqual(TEXT("and it is still as deep as it was"), Floored.Floors,
			  Plain.Floors);

	// A DUNGEON NOBODY SET A WALK COST ON COSTS THE ORDINARY DAY A FLOOR, which
	// is what a hand-built dungeon in a test and a save from before this field
	// existed both are.
	FCataclysmDungeon ByHand;
	ByHand.Floors = 12;

	TestEqual(TEXT("an unset walk cost is one day a floor"),
			  ByHand.WalkDaysPerFloor(), UCataclysmDayClock::DaysPerFloor,
			  0.0001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeDungeonCapTest,
	"Cataclysm.Surge.AFullCityStopsBeingATargetAndTheWaveLandsElsewhere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeDungeonCapTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	UCataclysmSurgeScheduler* Scheduler =
		MakeScheduler(ECataclysmSurgeMode::Static);
	UCataclysmEmpireMap* Map = MakeMap();

	// A CAP OF TWO ON CITY 0, WHICH ALREADY HOLDS TWO. It should never be picked
	// again while it stays full.
	Give(*Map, 0, ECataclysmCityUpgradeEffect::DungeonCap, 2.0f);

	TArray<int32> Counts = NoDungeonsAnywhere(*Map);
	Counts[0] = 2;

	FRandomStream Stream(4242);
	const TArray<int32> Landed = Scheduler->PickTargets(*Map, 40, Stream, Counts);

	// THE WAVE IS STILL THE SIZE IT WAS ASKED FOR. A dungeon that cannot land on
	// a full city lands on another rather than vanishing, which would let a
	// capped city absorb wave slots harmlessly.
	TestEqual(TEXT("all forty dungeons still landed"), Landed.Num(), 40);

	TestFalse(TEXT("and none of them on the full city"), Landed.Contains(0));

	// THE CONTROL. Without the cap the same forty rolls land on city 0 several
	// times, so its absence above is the cap and not luck.
	FRandomStream Same(4242);
	const TArray<int32> Uncapped = Scheduler->PickTargets(*Map, 40, Same);

	TestTrue(TEXT("without the cap that city is picked"), Uncapped.Contains(0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeCapWithinOneWaveTest,
	"Cataclysm.Surge.OneWaveCannotPushACityPastItsCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeCapWithinOneWaveTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	UCataclysmSurgeScheduler* Scheduler =
		MakeScheduler(ECataclysmSurgeMode::Static);
	UCataclysmEmpireMap* Map = MakeMap();

	// THE ROLL IS WITH REPLACEMENT, so a city one short of its cap can be chosen
	// twice in one wave. Counting down only once the wave was finished would let
	// it go over, and this is the case that catches that.
	Give(*Map, 0, ECataclysmCityUpgradeEffect::DungeonCap, 3.0f);

	TArray<int32> Counts = NoDungeonsAnywhere(*Map);
	Counts[0] = 2;

	FRandomStream Stream(77);
	const TArray<int32> Landed = Scheduler->PickTargets(*Map, 60, Stream, Counts);

	int32 OnTheCappedCity = 0;
	for (const int32 CityId : Landed)
	{
		if (CityId == 0)
		{
			++OnTheCappedCity;
		}
	}

	TestEqual(TEXT("exactly one more dungeon reaches the capped city"),
			  OnTheCappedCity, 1);

	TestEqual(TEXT("and the wave is still the size it was asked for"),
			  Landed.Num(), 60);

	// AND WHEN EVERY EXPOSED CITY IS FULL THE WAVE IS SHORT, which is the honest
	// answer rather than putting dungeons where the design forbids.
	//
	// A FRESH MAP, NOT THE ONE ABOVE. Two upgrades with one effect ADD UP, which
	// `FCataclysmCity::UpgradeValueFor` does on purpose, so giving the city that
	// already has a cap of 3 another cap of 1 would give it a cap of 4 and leave
	// it room rather than filling it. An earlier version of this test did
	// exactly that and three dungeons landed on it.
	UCataclysmEmpireMap* FullMap = MakeMap();
	TArray<int32> FullCounts = NoDungeonsAnywhere(*FullMap);

	for (const int32 CityId : FullMap->ExposedCities())
	{
		Give(*FullMap, CityId, ECataclysmCityUpgradeEffect::DungeonCap, 1.0f);
		FullCounts[CityId] = 1;
	}

	FRandomStream Full(77);
	const TArray<int32> Nowhere =
		Scheduler->PickTargets(*FullMap, 5, Full, FullCounts);

	TestEqual(TEXT("a wave with nowhere to land is empty"), Nowhere.Num(), 0);

	// THE CONTROL. The same map with nothing counted still takes a full wave, so
	// the emptiness above is the caps and not a broken map.
	FRandomStream Unlimited(77);

	TestEqual(TEXT("and the same map takes a full wave when nothing is counted"),
			  Scheduler->PickTargets(*FullMap, 5, Unlimited).Num(), 5);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeCapChangesNothingTest,
	"Cataclysm.Surge.CountingDungeonsChangesNoWaveWhenNoCityHasACap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeCapChangesNothingTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	UCataclysmSurgeScheduler* Scheduler =
		MakeScheduler(ECataclysmSurgeMode::Static);
	UCataclysmEmpireMap* Map = MakeMap();

	// NO CITY HAS BOUGHT A CAP, and several already hold dungeons. The counts
	// must then change nothing at all: a run that has nothing to do with this
	// upgrade has to roll exactly what it rolled before the upgrade existed.
	TArray<int32> Counts = NoDungeonsAnywhere(*Map);
	Counts[0] = 9;
	Counts[3] = 40;
	Counts[7] = 1;

	FRandomStream WithCounts(31337);
	FRandomStream WithoutCounts(31337);

	const TArray<int32> Told =
		Scheduler->PickTargets(*Map, 50, WithCounts, Counts);
	const TArray<int32> NotTold =
		Scheduler->PickTargets(*Map, 50, WithoutCounts);

	if (!TestEqual(TEXT("both waves are the same size"), Told.Num(),
				   NotTold.Num()))
	{
		return false;
	}

	TestEqual(TEXT("and fifty dungeons landed"), Told.Num(), 50);

	for (int32 Index = 0; Index < Told.Num(); ++Index)
	{
		TestEqual(*FString::Printf(TEXT("dungeon %d lands on the same city"),
								   Index),
				  Told[Index], NotTold[Index]);
	}

	// THE CONTROL. Two streams left at different states would make the
	// comparison above meaningless, so the draw counts are checked to match.
	TestEqual(TEXT("both streams took the same number of draws"),
			  WithCounts.GetCurrentSeed(), WithoutCounts.GetCurrentSeed());

	return true;
}

// ---------------------------------------------------------------------------
// What a dungeon does differently
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeSubTypeWeightsTest,
	"Cataclysm.Surge.EverySubTypeCarriesTheWeightTheDesignGivesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeSubTypeWeightsTest::RunTest(const FString& Parameters)
{
	// HAND-WORKED FIGURES, NOT THE CONSTANTS WRITTEN OUT AGAIN. A test that
	// compared `SpawnWeightFor(Timed)` with `SpawnWeightTimed` would pass
	// whatever either was. These are the numbers in
	// `config.SUBTYPE_SPAWN_WEIGHTS`.
	TestEqual(TEXT("Timed is 18"),
			  UCataclysmSurgeScheduler::SpawnWeightFor(
				  ECataclysmDungeonSubType::Timed), 18.0f, 0.0001f);

	TestEqual(TEXT("Horde is 18"),
			  UCataclysmSurgeScheduler::SpawnWeightFor(
				  ECataclysmDungeonSubType::Horde), 18.0f, 0.0001f);

	TestEqual(TEXT("Siege is 15"),
			  UCataclysmSurgeScheduler::SpawnWeightFor(
				  ECataclysmDungeonSubType::Siege), 15.0f, 0.0001f);

	TestEqual(TEXT("Cow Level is 7"),
			  UCataclysmSurgeScheduler::SpawnWeightFor(
				  ECataclysmDungeonSubType::CowLevel), 7.0f, 0.0001f);

	TestEqual(TEXT("Elite is 15"),
			  UCataclysmSurgeScheduler::SpawnWeightFor(
				  ECataclysmDungeonSubType::Elite), 15.0f, 0.0001f);

	TestEqual(TEXT("Volatile is 15"),
			  UCataclysmSurgeScheduler::SpawnWeightFor(
				  ECataclysmDungeonSubType::Volatile), 15.0f, 0.0001f);

	TestEqual(TEXT("Sacrificial is 12"),
			  UCataclysmSurgeScheduler::SpawnWeightFor(
				  ECataclysmDungeonSubType::Sacrificial), 12.0f, 0.0001f);

	// **AND NO SUB-TYPE AT ALL IS WEIGHTED NOTHING**, which is the change the
	// owner made on 2026-09-05: every dungeon a surge produces has a sub-type.
	// It used to be weighted 34, the commonest outcome of the eight.
	TestEqual(TEXT("no sub-type at all is weighted nothing"),
			  UCataclysmSurgeScheduler::SpawnWeightFor(
				  ECataclysmDungeonSubType::None), 0.0f, 0.0001f);

	// AND NOTHING ELSE IS LEFT WITHOUT ONE. Summing what the enum holds rather
	// than the literals above means a sub-type added to the enum and forgotten
	// makes this fail, instead of quietly never spawning.
	float Total = 0.0f;
	for (uint8 Value = static_cast<uint8>(ECataclysmDungeonSubType::Timed);
		 Value <= static_cast<uint8>(ECataclysmDungeonSubType::Sacrificial);
		 ++Value)
	{
		const ECataclysmDungeonSubType SubType =
			static_cast<ECataclysmDungeonSubType>(Value);

		TestTrue(*FString::Printf(
					 TEXT("sub-type %d has a weight above zero"), Value),
				 UCataclysmSurgeScheduler::SpawnWeightFor(SubType) > 0.0f);

		Total += UCataclysmSurgeScheduler::SpawnWeightFor(SubType);
	}

	TestEqual(TEXT("and they add up to 100"), Total, 100.0f, 0.0001f);

	// COW LEVEL IS THE RAREST, which is what "ridiculous amounts of loot" has to
	// be paid for with, and Timed the commonest.
	TestTrue(TEXT("Cow Level is rarer than Sacrificial"),
			 UCataclysmSurgeScheduler::SpawnWeightFor(
				 ECataclysmDungeonSubType::CowLevel) <
			 UCataclysmSurgeScheduler::SpawnWeightFor(
				 ECataclysmDungeonSubType::Sacrificial));

	TestTrue(TEXT("and Sacrificial rarer than Timed"),
			 UCataclysmSurgeScheduler::SpawnWeightFor(
				 ECataclysmDungeonSubType::Sacrificial) <
			 UCataclysmSurgeScheduler::SpawnWeightFor(
				 ECataclysmDungeonSubType::Timed));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeSubTypeRollTest,
	"Cataclysm.Surge.RollingManySubTypesGivesTheDesignedSpread",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeSubTypeRollTest::RunTest(const FString& Parameters)
{
	// ENOUGH ROLLS THAT THE RAREST ONE IS NOT A COINCIDENCE. Cow Level is 7 in
	// 100, so 20,000 rolls should give about 1,400 of them; a tolerance of two
	// percentage points is far wider than the spread at this many rolls and far
	// narrower than any weight being wrong.
	constexpr int32 Rolls = 20000;
	constexpr float Tolerance = 2.0f;

	FRandomStream Stream(20260905);

	TMap<ECataclysmDungeonSubType, int32> Counts;

	for (int32 Roll = 0; Roll < Rolls; ++Roll)
	{
		++Counts.FindOrAdd(UCataclysmSurgeScheduler::RollSubType(Stream));
	}

	for (uint8 Value = static_cast<uint8>(ECataclysmDungeonSubType::Timed);
		 Value <= static_cast<uint8>(ECataclysmDungeonSubType::Sacrificial);
		 ++Value)
	{
		const ECataclysmDungeonSubType SubType =
			static_cast<ECataclysmDungeonSubType>(Value);

		const int32 Seen = Counts.FindRef(SubType);
		const float Share = 100.0f * Seen / Rolls;
		const float Wanted = UCataclysmSurgeScheduler::SpawnWeightFor(SubType);

		// EVERY ONE OF THEM ACTUALLY CAME UP. A weighted roll that never returns
		// one of its options is the failure this is really looking for, and a
		// share of zero would not be caught by the tolerance alone if the weight
		// were also zero.
		TestTrue(*FString::Printf(TEXT("sub-type %d was rolled at all"), Value),
				 Seen > 0);

		TestEqual(*FString::Printf(
					  TEXT("sub-type %d came up %.1f%% of the time, wanted %.1f%%"),
					  Value, Share, Wanted),
				  Share, Wanted, Tolerance);
	}

	// **NO SUB-TYPE AT ALL NEVER CAME UP, NOT EVEN ONCE IN TWENTY THOUSAND.**
	// This is the whole of the owner's 2026-09-05 ruling stated as a
	// measurement, and it is the assertion that would catch a roll that walked
	// the enum from zero again. It used to be the commonest outcome of the
	// eight, at 34 in 100.
	TestEqual(TEXT("no dungeon came out without a sub-type"),
			  Counts.FindRef(ECataclysmDungeonSubType::None), 0);

	// AND THE SPREAD IS NOT MERELY CLOSE TO THE WEIGHTS, IT IS ORDERED BY THEM.
	// Seven shares all within two points of 14.3 would pass the check above and
	// mean the weights were being ignored.
	TestTrue(TEXT("Timed more often than Sacrificial"),
			 Counts.FindRef(ECataclysmDungeonSubType::Timed) >
			 Counts.FindRef(ECataclysmDungeonSubType::Sacrificial));

	TestTrue(TEXT("and Sacrificial more often than Cow Level"),
			 Counts.FindRef(ECataclysmDungeonSubType::Sacrificial) >
			 Counts.FindRef(ECataclysmDungeonSubType::CowLevel));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeSubTypeOneDrawTest,
	"Cataclysm.Surge.RollingASubTypeCostsExactlyOneDraw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeSubTypeOneDrawTest::RunTest(const FString& Parameters)
{
	// WHY THIS MATTERS AT ALL. Every dungeon in a wave is rolled from one
	// stream, one after another. A sub-type roll that sometimes took two draws
	// would make the depth of the second dungeon in a wave depend on what the
	// first one rolled, so a change to these weights would silently move every
	// other number in the run. `PickTargets` above makes the same promise for the
	// same reason.
	for (const int32 Seed : { 1, 7, 99, 5150, 20260905 })
	{
		FRandomStream Rolled(Seed);
		FRandomStream Drawn(Seed);

		UCataclysmSurgeScheduler::RollSubType(Rolled);
		Drawn.FRand();

		TestEqual(*FString::Printf(
					  TEXT("at seed %d the roll left the stream one draw on"),
					  Seed),
				  Rolled.GetCurrentSeed(), Drawn.GetCurrentSeed());
	}

	// **AND WHEN A SIEGE IS REFUSED, WHICH IS THE HARD HALF.** A refused Siege
	// is spread across the other six, and the obvious way to do that is to roll
	// again -- which is exactly what the promise above forbids. It re-reads the
	// draw it already made instead.
	//
	// **SEEDS CHOSEN BY WHAT THEY DRAW, NOT FIXED.** The five above are fixed
	// and none of them draws a Siege, so barring one leaves the ordinary path
	// and says nothing about the re-read. A guard proof that replaced the
	// re-read with `Stream.FRand()` was NOT caught by this test until the seeds
	// were picked this way; one other test caught it, and one test is not the
	// guard this is supposed to be.
	TArray<int32> SeedsThatDrawASiege;
	for (int32 Seed = 1;
		 Seed <= 1000 && SeedsThatDrawASiege.Num() < 5;
		 ++Seed)
	{
		FRandomStream Trial(Seed);
		if (UCataclysmSurgeScheduler::RollSubType(Trial)
			== ECataclysmDungeonSubType::Siege)
		{
			SeedsThatDrawASiege.Add(Seed);
		}
	}

	// THE CONTROL. Without seeds that reach the refusal, every check below is
	// the loop above written a second time.
	if (!TestEqual(TEXT("seeds whose draw lands on Siege were found"),
				   SeedsThatDrawASiege.Num(), 5))
	{
		return false;
	}

	for (const int32 Seed : SeedsThatDrawASiege)
	{
		FRandomStream Rolled(Seed);
		FRandomStream Drawn(Seed);

		const ECataclysmDungeonSubType Got =
			UCataclysmSurgeScheduler::RollSubType(Rolled,
												  /*bSiegeAllowed=*/false);
		Drawn.FRand();

		// THE REFUSAL REALLY HAPPENED AT THIS SEED, which is what makes the
		// draw count below a statement about the re-read.
		TestNotEqual(*FString::Printf(
						 TEXT("at seed %d the Siege was refused"), Seed),
					 static_cast<uint8>(Got),
					 static_cast<uint8>(ECataclysmDungeonSubType::Siege));

		TestEqual(*FString::Printf(
					  TEXT("and at seed %d it still cost exactly one draw"),
					  Seed),
				  Rolled.GetCurrentSeed(), Drawn.GetCurrentSeed());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeCowLevelWalkTest,
	"Cataclysm.Surge.ACowLevelTakesTwiceAsLongAndCannotBeHurried",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeCowLevelWalkTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	UCataclysmSurgeScheduler* Scheduler =
		MakeScheduler(ECataclysmSurgeMode::Static);
	UCataclysmEmpireMap* Map = MakeMap();

	const TArray<int32> Rim =
		CitiesAtTier(*Map, ECataclysmCityTier::Outpost, 2);

	if (!TestEqual(TEXT("two Outposts were found to compare"), Rim.Num(), 2))
	{
		return false;
	}

	// ONE OF THEM BOUGHT THE UPGRADE THAT SHORTENS A WALK. On an ordinary
	// dungeon it takes four days off. On this one it must take nothing off at
	// all, which is the half of the rule that is easy to lose.
	Give(*Map, Rim[1], ECataclysmCityUpgradeEffect::DungeonWalkDaysFewer, 4.0f);

	// A SEED THAT ROLLS A COW LEVEL, FOUND BY LOOKING. Cow Level is 7 in 100, so
	// most seeds do not, and a test that just hoped would be a test of the seed.
	int32 CowSeed = INDEX_NONE;
	for (int32 Seed = 1; Seed <= 5000 && CowSeed == INDEX_NONE; ++Seed)
	{
		FRandomStream Trial(Seed);
		const FCataclysmDungeon Candidate =
			Scheduler->MakeDungeon(0, *Map->Find(Rim[0]), 1, Trial);

		if (Candidate.SubType == ECataclysmDungeonSubType::CowLevel)
		{
			CowSeed = Seed;
		}
	}

	if (!TestTrue(TEXT("a seed rolling a Cow Level was found"),
				  CowSeed != INDEX_NONE))
	{
		return false;
	}

	FRandomStream A(CowSeed), B(CowSeed);

	const FCataclysmDungeon Plain =
		Scheduler->MakeDungeon(0, *Map->Find(Rim[0]), 1, A);
	const FCataclysmDungeon Hurried =
		Scheduler->MakeDungeon(1, *Map->Find(Rim[1]), 1, B);

	if (!TestEqual(TEXT("both are Cow Levels"),
				   static_cast<uint8>(Plain.SubType),
				   static_cast<uint8>(ECataclysmDungeonSubType::CowLevel)))
	{
		return false;
	}

	TestEqual(TEXT("and the upgraded city rolled one too"),
			  static_cast<uint8>(Hurried.SubType),
			  static_cast<uint8>(ECataclysmDungeonSubType::CowLevel));

	// TIME TO COMPLETE IS DOUBLED.
	TestEqual(TEXT("a Cow Level costs two days a floor"), Plain.WalkDays,
			  Plain.Floors * UCataclysmDayClock::DaysPerFloor * 2.0f, 0.0001f);

	TestEqual(TEXT("so each of its floors costs two days"),
			  Plain.WalkDaysPerFloor(), 2.0f, 0.0001f);

	// AND CANNOT BE REDUCED. The upgrade is worth four days on any other dungeon
	// this city receives, and nothing here.
	TestEqual(TEXT("four days off a Cow Level is no days off a Cow Level"),
			  Hurried.WalkDays, Plain.WalkDays, 0.0001f);

	// WHAT THE DOUBLING DOES NOT TOUCH. Depth and time come apart, so the reward
	// and the timer are the ones the depth earned. A Cow Level bites on exactly
	// the day it would have bitten as a plain dungeon.
	TestEqual(TEXT("its floor count is untouched"), Hurried.Floors,
			  Plain.Floors);

	// ITS TIMER IS THE ONE ITS DEPTH EARNED, jitter and all, and the doubling is
	// nowhere in it. Compared against the depth's own figure rather than against
	// the other dungeon, so a doubling that leaked into the timer would show up
	// as a ratio of about two rather than one.
	const float Earned = UCataclysmDayClock::ResolveDaysFor(Plain.Floors);

	TestTrue(TEXT("the timer is within the jitter of what its depth gives"),
			 FMath::Abs(Plain.ResolveDays / Earned - 1.0f) <=
				 UCataclysmSurgeScheduler::ResolveJitter + 0.0001f);

	TestEqual(TEXT("and the upgraded city's Cow Level has the same timer"),
			  Hurried.ResolveDays, Plain.ResolveDays, 0.0001f);

	// THE CONTROL FOR THE WHOLE TEST. If the upgrade did nothing to any dungeon,
	// every figure above would pass while proving nothing about Cow Level. So
	// the same upgrade on the same city is shown to work on a dungeon that is
	// not one.
	int32 PlainSeed = INDEX_NONE;
	for (int32 Seed = 1; Seed <= 5000 && PlainSeed == INDEX_NONE; ++Seed)
	{
		FRandomStream Trial(Seed);
		const FCataclysmDungeon Candidate =
			Scheduler->MakeDungeon(0, *Map->Find(Rim[0]), 1, Trial);

		if (Candidate.SubType != ECataclysmDungeonSubType::CowLevel &&
			Candidate.Floors > 6)
		{
			PlainSeed = Seed;
		}
	}

	if (!TestTrue(TEXT("a seed rolling something else was found"),
				  PlainSeed != INDEX_NONE))
	{
		return false;
	}

	FRandomStream C(PlainSeed), D(PlainSeed);

	const FCataclysmDungeon Ordinary =
		Scheduler->MakeDungeon(2, *Map->Find(Rim[0]), 1, C);
	const FCataclysmDungeon Shortened =
		Scheduler->MakeDungeon(3, *Map->Find(Rim[1]), 1, D);

	TestEqual(TEXT("the same upgrade takes four days off a dungeon that is not "
				   "a Cow Level"),
			  Shortened.WalkDays, Ordinary.WalkDays - 4.0f, 0.0001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeWaveSubTypesTest,
	"Cataclysm.Surge.AWaveRollsASubTypePerDungeon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeWaveSubTypesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	UCataclysmSurgeScheduler* Scheduler =
		MakeScheduler(ECataclysmSurgeMode::Static);
	UCataclysmEmpireMap* Map = MakeMap();

	// A WAVE ROLLED THE ORDINARY WAY, not one dungeon at a time, so this covers
	// the path the day loop actually takes.
	FRandomStream Stream(4242);
	Scheduler->SurgeIndex = 0;

	// A SECOND STREAM FOR THE SECOND PASS BELOW, seeded differently so the two
	// passes are not the same waves counted twice.
	FRandomStream Check(31337);

	TSet<ECataclysmDungeonSubType> Seen;
	int32 Dungeons = 0;

	for (int32 Wave = 0; Wave < 60; ++Wave)
	{
		for (const FCataclysmDungeon& Dungeon :
			 Scheduler->RollWave(*Map, 1, Dungeons, Stream))
		{
			Seen.Add(Dungeon.SubType);
			++Dungeons;

			// A COW LEVEL IN A REAL WAVE COSTS DOUBLE, wherever it was rolled.
			if (Dungeon.SubType == ECataclysmDungeonSubType::CowLevel)
			{
				TestEqual(TEXT("a Cow Level in a wave costs two days a floor"),
						  Dungeon.WalkDaysPerFloor(), 2.0f, 0.0001f);
			}
			else
			{
				TestEqual(TEXT("and anything else costs the ordinary one"),
						  Dungeon.WalkDaysPerFloor(),
						  UCataclysmDayClock::DaysPerFloor, 0.0001f);
			}
		}
	}

	if (!TestTrue(TEXT("the waves brought enough dungeons to see a spread"),
				  Dungeons >= 200))
	{
		return false;
	}

	// EVERY SUB-TYPE REACHES A REAL WAVE. `RollWave` calling `MakeDungeon` is
	// what puts a sub-type on a dungeon the day loop will actually see, and a
	// wave that only ever produced one kind would pass every other test in this
	// file.
	//
	// WALKED FROM THE ENUM RATHER THAN COUNTED. A count would say the right
	// number turned up without saying which, so a sub-type that never spawns
	// while another spawns twice would pass.
	for (uint8 Value = static_cast<uint8>(ECataclysmDungeonSubType::Timed);
		 Value <= static_cast<uint8>(ECataclysmDungeonSubType::Sacrificial);
		 ++Value)
	{
		const ECataclysmDungeonSubType SubType =
			static_cast<ECataclysmDungeonSubType>(Value);

		TestTrue(*FString::Printf(
					 TEXT("sub-type %d turned up in a wave"), Value),
				 Seen.Contains(SubType));
	}

	// **AND NOT ONE DUNGEON CAME OUT WITHOUT A SUB-TYPE.** That is the owner's
	// ruling of 2026-09-05 measured on the path the day loop takes rather than
	// on `RollSubType` alone.
	//
	// IT REALLY IS NONE, WHICH IT WAS NOT BEFORE 2026-09-06. A Siege refused by
	// the one-per-city cap used to be made plain, which left 1.6% of dungeons
	// carrying nothing; refusals are now spread across the other six sub-types.
	// A second pass is run here because a refusal needs a wave that lands two
	// dungeons on one city and rolls Siege for both, which the first 60 waves
	// happen not to do at every seed.
	int32 Plain = 0;
	int32 Rolled = 0;
	for (int32 Wave = 0; Wave < 60; ++Wave)
	{
		for (const FCataclysmDungeon& Dungeon :
			 Scheduler->RollWave(*Map, 1, 10000 + Wave * 100, Check))
		{
			++Rolled;
			if (Dungeon.SubType == ECataclysmDungeonSubType::None)
			{
				++Plain;
			}
		}
	}

	AddInfo(FString::Printf(
		TEXT("%d dungeons rolled across 60 further waves"), Rolled));

	TestEqual(TEXT("no dungeon in any wave came out without a sub-type"),
			  Plain, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeSiegeCapTest,
	"Cataclysm.Surge.AWaveWillNotPutASiegeOnACityThatHasOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeSiegeCapTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	UCataclysmSurgeScheduler* Scheduler =
		MakeScheduler(ECataclysmSurgeMode::Static);
	UCataclysmEmpireMap* Map = MakeMap();

	// THE CONTROL FIRST. Told that no city holds a Siege, enough waves produce
	// some. Without this the refusal below would pass on a run that simply never
	// rolled one.
	FRandomStream Free(31337);
	int32 SiegesWhenAllowed = 0;
	int32 PlainWhenAllowed = 0;
	int32 Dungeons = 0;
	TMap<ECataclysmDungeonSubType, int32> FreeCounts;

	for (int32 Wave = 0; Wave < 60; ++Wave)
	{
		for (const FCataclysmDungeon& Dungeon :
			 Scheduler->RollWave(*Map, 1, Dungeons, Free))
		{
			++Dungeons;
			++FreeCounts.FindOrAdd(Dungeon.SubType);
			if (Dungeon.SubType == ECataclysmDungeonSubType::Siege)
			{
				++SiegesWhenAllowed;
			}
			if (Dungeon.SubType == ECataclysmDungeonSubType::None)
			{
				++PlainWhenAllowed;
			}
		}
	}

	if (!TestTrue(TEXT("waves do produce Sieges when none is standing"),
				  SiegesWhenAllowed > 0))
	{
		return false;
	}

	// AND NOW THE SAME ROLLS, told that every city already holds one. Same seed,
	// same wave sizes, same targets, same draws -- the only difference is what
	// the caller said was already there.
	TArray<int32> AllBesieged;
	AllBesieged.Init(UCataclysmSurgeScheduler::SiegesPerCity,
					 Map->Cities.Num());

	FRandomStream Barred(31337);
	int32 SiegesWhenBarred = 0;
	int32 PlainWhenBarred = 0;
	int32 BarredDungeons = 0;
	TMap<ECataclysmDungeonSubType, int32> BarredCounts;

	for (int32 Wave = 0; Wave < 60; ++Wave)
	{
		for (const FCataclysmDungeon& Dungeon :
			 Scheduler->RollWave(*Map, 1, BarredDungeons, Barred, TArray<int32>(),
								 AllBesieged))
		{
			++BarredDungeons;
			++BarredCounts.FindOrAdd(Dungeon.SubType);
			if (Dungeon.SubType == ECataclysmDungeonSubType::Siege)
			{
				++SiegesWhenBarred;
			}
			if (Dungeon.SubType == ECataclysmDungeonSubType::None)
			{
				++PlainWhenBarred;
			}
		}
	}

	TestEqual(TEXT("and none at all when every city already has one"),
			  SiegesWhenBarred, 0);

	// **AND NOTHING CAME OUT PLAIN, IN EITHER RUN.** A refused Siege used to be
	// made a dungeon with no sub-type; since 2026-09-06 it is spread across the
	// other six instead, so the plain column is empty however many are refused.
	TestEqual(TEXT("nothing came out plain when every Siege was barred"),
			  PlainWhenBarred, 0);
	TestEqual(TEXT("nor when none was"), PlainWhenAllowed, 0);

	// **THE REFUSED SIEGES WENT TO THE OTHER SIX, AND TO NOTHING ELSE.** Both
	// runs draw the same numbers in the same order, so every dungeon whose draw
	// did not land on Siege comes out identical in the two. Each of the other
	// six can therefore only gain, and what they gain between them is exactly
	// the Sieges the unbarred run landed.
	int32 Gained = 0;
	for (uint8 Value = static_cast<uint8>(ECataclysmDungeonSubType::Timed);
		 Value <= static_cast<uint8>(ECataclysmDungeonSubType::Sacrificial);
		 ++Value)
	{
		const ECataclysmDungeonSubType SubType =
			static_cast<ECataclysmDungeonSubType>(Value);

		if (SubType == ECataclysmDungeonSubType::Siege)
		{
			continue;
		}

		const int32 WhenAllowed = FreeCounts.FindRef(SubType);
		const int32 WhenBarred = BarredCounts.FindRef(SubType);

		TestTrue(*FString::Printf(
					 TEXT("sub-type %d did not lose ground when Sieges were "
						  "barred: %d against %d"), Value, WhenBarred,
					 WhenAllowed),
				 WhenBarred >= WhenAllowed);

		Gained += WhenBarred - WhenAllowed;
	}

	TestEqual(TEXT("and between them they took every refused Siege"),
			  Gained, SiegesWhenAllowed);

	// AND THE SPREAD REACHED MORE THAN ONE OF THEM, which is what "spread it
	// across the others" means. A rule that sent every refusal to one sub-type
	// would satisfy every line above.
	int32 SharedBy = 0;
	for (uint8 Value = static_cast<uint8>(ECataclysmDungeonSubType::Timed);
		 Value <= static_cast<uint8>(ECataclysmDungeonSubType::Sacrificial);
		 ++Value)
	{
		const ECataclysmDungeonSubType SubType =
			static_cast<ECataclysmDungeonSubType>(Value);

		if (SubType != ECataclysmDungeonSubType::Siege
			&& BarredCounts.FindRef(SubType) > FreeCounts.FindRef(SubType))
		{
			++SharedBy;
		}
	}

	TestTrue(*FString::Printf(
				 TEXT("%d different sub-types took a share of the refusals"),
				 SharedBy),
			 SharedBy > 1);

	// THE WAVE SIZES DID NOT CHANGE, so the comparison above is between two
	// runs of the same length. A refusal that dropped dungeons rather than
	// making them plain would show up here.
	TestEqual(TEXT("the same number of dungeons landed either way"),
			  BarredDungeons, Dungeons);

	// AND THE REFUSAL COST NO EXTRA DRAW. Both streams took the same number, so
	// a city that bars a Siege does not shift what every later dungeon rolls.
	TestEqual(TEXT("both streams took the same number of draws"),
			  Barred.GetCurrentSeed(), Free.GetCurrentSeed());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeSiegeOncePerWaveTest,
	"Cataclysm.Surge.OneWaveNeverPutsTwoSiegesOnOneCity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeSiegeOncePerWaveTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	// WHY THIS IS A SEPARATE TEST. The caller passes in what was already
	// standing, and a wave that landed two dungeons on one city could still put
	// two Sieges there while honouring everything it was told. Targets are
	// rolled with replacement, so two on one city is ordinary rather than rare.
	UCataclysmSurgeScheduler* Scheduler =
		MakeScheduler(ECataclysmSurgeMode::Static);
	UCataclysmEmpireMap* Map = MakeMap();

	FRandomStream Stream(24680);
	int32 Dungeons = 0;
	int32 WavesWithTwoOnOneCity = 0;

	for (int32 Wave = 0; Wave < 400; ++Wave)
	{
		const TArray<FCataclysmDungeon> Landed =
			Scheduler->RollWave(*Map, 1, Dungeons, Stream);

		TMap<int32, int32> DungeonsOn;
		TMap<int32, int32> SiegesOn;

		for (const FCataclysmDungeon& Dungeon : Landed)
		{
			++Dungeons;
			++DungeonsOn.FindOrAdd(Dungeon.CityId);

			if (Dungeon.SubType == ECataclysmDungeonSubType::Siege)
			{
				++SiegesOn.FindOrAdd(Dungeon.CityId);
			}
		}

		for (const TPair<int32, int32>& Pair : DungeonsOn)
		{
			if (Pair.Value > 1)
			{
				++WavesWithTwoOnOneCity;
				break;
			}
		}

		for (const TPair<int32, int32>& Pair : SiegesOn)
		{
			if (Pair.Value > UCataclysmSurgeScheduler::SiegesPerCity)
			{
				AddError(FString::Printf(
					TEXT("wave %d put %d Sieges on city %d in one go"),
					Wave, Pair.Value, Pair.Key));
				return false;
			}
		}
	}

	// THE CONTROL. If no wave ever landed two dungeons on one city, the loop
	// above never exercised the rule and proves nothing.
	TestTrue(*FString::Printf(
				 TEXT("waves that put two dungeons on one city were seen (%d of "
					  "400)"), WavesWithTwoOnOneCity),
			 WavesWithTwoOnOneCity > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeSubTypeNameTest,
	"Cataclysm.Surge.EverySubTypeHasAReadableNameAndPlainHasNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeSubTypeNameTest::RunTest(const FString& Parameters)
{
	// `TestEqualSensitive` AND NOT `TestEqual`. The ordinary one compares two
	// FStrings case-insensitively, so it would accept "siege" for "Siege" and
	// say nothing about it.
	//
	// HAND-WRITTEN SPELLINGS, NOT THE IDENTIFIERS READ BACK. `CowLevel` is two
	// words when a person reads it, and a test that derived the string from the
	// enumerator would accept "CowLevel".
	TestEqualSensitive(TEXT("Timed"), UCataclysmSurgeScheduler::SubTypeName(
		ECataclysmDungeonSubType::Timed), FString(TEXT("Timed")));

	TestEqualSensitive(TEXT("Horde"), UCataclysmSurgeScheduler::SubTypeName(
		ECataclysmDungeonSubType::Horde), FString(TEXT("Horde")));

	TestEqualSensitive(TEXT("Siege"), UCataclysmSurgeScheduler::SubTypeName(
		ECataclysmDungeonSubType::Siege), FString(TEXT("Siege")));

	TestEqualSensitive(TEXT("Cow Level is two words"),
		UCataclysmSurgeScheduler::SubTypeName(
			ECataclysmDungeonSubType::CowLevel), FString(TEXT("Cow Level")));

	TestEqualSensitive(TEXT("Elite"), UCataclysmSurgeScheduler::SubTypeName(
		ECataclysmDungeonSubType::Elite), FString(TEXT("Elite")));

	TestEqualSensitive(TEXT("Volatile"), UCataclysmSurgeScheduler::SubTypeName(
		ECataclysmDungeonSubType::Volatile), FString(TEXT("Volatile")));

	TestEqualSensitive(TEXT("Sacrificial"), UCataclysmSurgeScheduler::SubTypeName(
		ECataclysmDungeonSubType::Sacrificial), FString(TEXT("Sacrificial")));

	// AN ORDINARY DUNGEON SAYS NOTHING, which is what lets a caller print the
	// name unconditionally without writing "(None)" beside most dungeons.
	TestTrue(TEXT("and a dungeon with no sub-type has no name"),
			 UCataclysmSurgeScheduler::SubTypeName(
				 ECataclysmDungeonSubType::None).IsEmpty());

	return true;
}

/**
 * No dungeon a campaign produces comes out without a sub-type.
 *
 * WHAT THE OWNER RULED, MEASURED ON WHOLE CAMPAIGNS. Every dungeon a surge makes
 * has a sub-type as of 2026-09-05, and the one-per-city Siege cap was the single
 * exception until 2026-09-06: a Siege rolled for a city that already held one
 * became a plain dungeon. **This test measured that exception at 1.6% of all
 * dungeons made**, which is why the owner was asked what should happen instead
 * and answered "Spread it across the others". A refused Siege is now spread
 * across the other six sub-types in proportion to their weights, so the answer
 * here is none at all.
 *
 * IT ASSERTS ZERO RATHER THAN A SMALL SHARE, deliberately. A test that still
 * allowed 1.6% would pass whether the redistribution worked or not, which is the
 * whole reason this one is worth keeping rather than deleting.
 *
 * WHY A CAMPAIGN AND NOT A LOOP OF ROLLS. The refusal only happens when a Siege
 * is rolled for a city that already has one, which needs cities that accumulate
 * Sieges over time. `ARefusedSiegeIsSpreadAcrossTheOthersInProportion` covers
 * the roll on its own; this covers the path the day loop takes.
 *
 * **IT COUNTS EVERY DUNGEON THE CAMPAIGN MADE, NOT WHAT IS LEFT STANDING**, and
 * the difference is not small. A city that falls absorbs every dungeon on it,
 * and a Siege takes 1% of its host city's defence and population every day it
 * stands -- so a Siege destroys the city it is standing on and is then removed
 * with it. Counting the board at the end of a 600 day campaign found 14 dungeons
 * standing and NOT ONE Siege among them, from a distribution that rolls Siege 15
 * times in 100. A census of the board is a census of the sub-types that do not
 * kill their host.
 *
 * SO IT RECORDS EACH DUNGEON AS IT ARRIVES, by watching for identifiers it has
 * not seen. They are handed out in order and never reused, so a dungeon that
 * appears and is later absorbed is still counted once.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgePlainIsAnExceptionTest,
	"Cataclysm.Surge.NoDungeonACampaignMakesComesOutWithoutASubType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgePlainIsAnExceptionTest::RunTest(const FString& Parameters)
{
	// TWENTY CAMPAIGNS RATHER THAN ONE. A single 600 day campaign makes about a
	// hundred dungeons, measured, which is too few to compare a 7-in-100
	// sub-type against an exception that arises a few times in a hundred.
	// Pooling also stops the answer being a fact about one seed.
	constexpr int32 Campaigns = 20;
	constexpr int32 MostDays = 600;

	TMap<ECataclysmDungeonSubType, int32> Made;
	int32 Total = 0;
	int32 Days = 0;

	for (int32 Seed = 1; Seed <= Campaigns; ++Seed)
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->Begin(Seed);

		if (!TestNotNull(TEXT("the run has a map"), Run->Map.Get()))
		{
			return false;
		}

		TSet<int32> Counted;

		auto RecordWhatIsNew = [&Made, &Counted, &Total](
								   const UCataclysmEmpireRun& Live)
		{
			for (const FCataclysmDungeon& Dungeon : Live.Dungeons)
			{
				// A FALLEN CITY IS NOT A DUNGEON A SURGE ROLLED, so it is not
				// one this rule is about. Issue #1324 slice 2 made a city that
				// falls become a dungeon standing on itself, and nothing rolls
				// a sub-type for it -- `MakeFallenCityDungeon` takes no random
				// stream at all, because its depth is determined by the siege
				// that took the city rather than drawn.
				//
				// WHETHER IT SHOULD HAVE ONE IS AN OPEN DESIGN QUESTION, issue
				// #1342, and not something this test should decide by
				// accident. Counting it here would have this test report the
				// removed no-sub-type outcome as though the roll had produced
				// it, which is the opposite of what it exists to catch.
				if (Dungeon.Type == ECataclysmDungeonType::FallenCity)
				{
					continue;
				}

				bool bAlready = false;
				Counted.Add(Dungeon.DungeonId, &bAlready);
				if (!bAlready)
				{
					++Made.FindOrAdd(Dungeon.SubType);
					++Total;
				}
			}
		};

		// THE FIRST WAVE FIRES AT RUN START, so the board is read before any day
		// passes as well as after each one.
		RecordWhatIsNew(*Run);

		// AS FAR AS THE EMPIRE SURVIVES, up to a limit. A fixed number of days
		// would be a guess: once the frontier is gone no wave lands anywhere and
		// the sample stops growing, so each run stops while there is still one.
		int32 Advanced = 0;
		while (Advanced < MostDays && Run->Map->ExposedCities().Num() >= 2)
		{
			Run->AdvanceDay();
			++Advanced;
			RecordWhatIsNew(*Run);
		}

		Days += Advanced;
	}

	const int32 Plain = Made.FindRef(ECataclysmDungeonSubType::None);
	const int32 Cows = Made.FindRef(ECataclysmDungeonSubType::CowLevel);

	AddInfo(FString::Printf(
		TEXT("%d campaigns, %d days in all, %d dungeons made: %d with no "
			 "sub-type (%.1f%%), %d Cow Levels, %d Sieges"),
		Campaigns, Days, Total, Plain,
		Total > 0 ? 100.0f * Plain / Total : 0.0f, Cows,
		Made.FindRef(ECataclysmDungeonSubType::Siege)));

	// THE SAMPLE IS BIG ENOUGH FOR THE COMPARISON TO MEAN SOMETHING. A handful
	// of dungeons would satisfy the comparison below whatever the roll did.
	if (!TestTrue(TEXT("enough dungeons were made to compare"), Total >= 1000))
	{
		return false;
	}

	// EVERY SUB-TYPE WAS MADE AT LEAST ONCE, so a zero on either side of the
	// comparison below is a real zero rather than a sample that never got there.
	for (uint8 Value = static_cast<uint8>(ECataclysmDungeonSubType::Timed);
		 Value <= static_cast<uint8>(ECataclysmDungeonSubType::Sacrificial);
		 ++Value)
	{
		const ECataclysmDungeonSubType SubType =
			static_cast<ECataclysmDungeonSubType>(Value);

		TestTrue(*FString::Printf(TEXT("sub-type %d was made at least once"),
								  Value),
				 Made.FindRef(SubType) > 0);
	}

	// **NONE AT ALL.** It was 31 of 1,925 before refused Sieges were spread
	// across the other six.
	TestEqual(TEXT("no dungeon came out without a sub-type"), Plain, 0);

	// AND COW LEVELS DID TURN UP, so the count above is a real zero from a
	// sample that reached the rarer sub-types rather than one that made
	// nothing.
	TestTrue(TEXT("Cow Levels were made"), Cows > 0);

	return true;
}

/**
 * A refused Siege is spread across the other six in proportion to their weights.
 *
 * THE OWNER'S RULING OF 2026-09-06, verbatim: "Spread it across the others".
 * This is the roll on its own; `NoDungeonACampaignMakesComesOutWithoutASubType`
 * is the same rule on the path the day loop takes.
 *
 * **WHAT MAKES THIS HARD IS THE DRAW COUNT, NOT THE SPREAD.** Rolling again
 * would be easy and is forbidden: every dungeon in a wave is rolled from one
 * stream in sequence, so a second draw here would change the depth of every
 * later dungeon in the wave. `RollSubType` re-reads the draw it already made
 * into the weight space the other six occupy, and
 * `RollingASubTypeCostsExactlyOneDraw` is what holds it to that.
 *
 * SO THIS CHECKS THE RE-READ IS EXACT rather than merely non-Siege. Any rule
 * that avoided Siege would pass a test that only asked for that -- always
 * answering Timed, for instance. Each of the six should take its own weight out
 * of the 85 that remain.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeRefusedSiegeSpreadTest,
	"Cataclysm.Surge.ARefusedSiegeIsSpreadAcrossTheOthersInProportion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeRefusedSiegeSpreadTest::RunTest(const FString& Parameters)
{
	// ENOUGH ROLLS THAT THE RAREST SHARE IS NOT A COINCIDENCE. Cow Level takes
	// 7 of the 85 left, which is 8.2%, so 40,000 rolls give about 3,300 of them.
	constexpr int32 Rolls = 40000;
	constexpr float Tolerance = 1.0f;

	FRandomStream Stream(20260906);
	TMap<ECataclysmDungeonSubType, int32> Counts;

	for (int32 Roll = 0; Roll < Rolls; ++Roll)
	{
		++Counts.FindOrAdd(
			UCataclysmSurgeScheduler::RollSubType(Stream,
												  /*bSiegeAllowed=*/false));
	}

	TestEqual(TEXT("not one Siege was returned"),
			  Counts.FindRef(ECataclysmDungeonSubType::Siege), 0);
	TestEqual(TEXT("and not one dungeon without a sub-type"),
			  Counts.FindRef(ECataclysmDungeonSubType::None), 0);

	// THE LINE THE OTHER SIX OCCUPY, worked out here rather than read from the
	// code, so a change to the totals shows up as a disagreement.
	const float Left = 100.0f - 15.0f;

	const TPair<ECataclysmDungeonSubType, float> Expected[] = {
		{ ECataclysmDungeonSubType::Timed,		 18.0f },
		{ ECataclysmDungeonSubType::Horde,		 18.0f },
		{ ECataclysmDungeonSubType::CowLevel,	  7.0f },
		{ ECataclysmDungeonSubType::Elite,		 15.0f },
		{ ECataclysmDungeonSubType::Volatile,	 15.0f },
		{ ECataclysmDungeonSubType::Sacrificial, 12.0f },
	};

	int32 Seen = 0;
	for (const TPair<ECataclysmDungeonSubType, float>& Pair : Expected)
	{
		const int32 Got = Counts.FindRef(Pair.Key);
		Seen += Got;

		const float Share = 100.0f * Got / Rolls;
		const float Wanted = 100.0f * Pair.Value / Left;

		TestEqual(*FString::Printf(
					  TEXT("sub-type %d took %.2f%% of the refusals, wanted "
						   "%.2f%%"),
					  static_cast<int32>(Pair.Key), Share, Wanted),
				  Share, Wanted, Tolerance);
	}

	// AND EVERY ROLL LANDED ON ONE OF THE SIX. A rule that answered something
	// outside the list would leave a gap the shares above could not show.
	TestEqual(TEXT("every roll landed on one of the six"), Seen, Rolls);

	return true;
}



/**
 * Issue #1324 slice 2. The dungeon a city becomes is as deep as the siege that
 * took it, and holds one boss for each dungeon that was standing.
 *
 * IT IS THE ONE DUNGEON WHOSE DEPTH IS NOT ROLLED, so this is checked by
 * calling the maker directly with a count rather than by driving a run.
 * `Cataclysm.EmpireRun.AFallenCityBecomesADungeonStandingOnItself` is the other
 * half: that a real fall actually calls this.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeFallenCityTest,
	"Cataclysm.Surge.AFallenCityIsAsDeepAsTheSiegeThatTookIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeFallenCityTest::RunTest(const FString& Parameters)
{
	FCataclysmCity City;
	City.CityId = 4;
	City.Tier = ECataclysmCityTier::Outpost;

	// A QUIET CITY GETS THE TIER'S MINIMUM. One dungeon standing is far below
	// the twenty floors the design floors an Outpost at.
	{
		const FCataclysmDungeon Made =
			UCataclysmSurgeScheduler::MakeFallenCityDungeon(9, City, 40, 1);

		TestEqual(TEXT("it is a Fallen City"), Made.Type,
				  ECataclysmDungeonType::FallenCity);
		TestEqual(TEXT("on the city that fell"), Made.CityId, 4);
		TestEqual(TEXT("a quiet Outpost floors at twenty"), Made.Floors, 20);
		TestEqual(TEXT("and holds one boss"), Made.Bosses, 1);
		TestEqual(TEXT("it takes no defence"), Made.DefenceDamage, 0.0f);
		TestEqual(TEXT("and no population"), Made.PopulationDamage, 0.0f);
		TestEqual(TEXT("and never resolves"), Made.ResolveDays,
				  UCataclysmSurgeScheduler::FallenCityResolveDays);
		TestEqual(TEXT("and has no sub-type"), Made.SubType,
				  ECataclysmDungeonSubType::None);
	}

	// A BESIEGED ONE IS DEEPER THAN THE MINIMUM, AND THE BOSSES FOLLOW THE
	// SIEGE RATHER THAN THE FLOORS. Twenty-five dungeons standing makes a
	// twenty-five floor dungeon with twenty-five bosses in it; twenty-one makes
	// a twenty-one floor one. The two numbers are the same count here and are
	// NOT the same rule -- at three dungeons the floors are twenty and the
	// bosses are three.
	{
		const FCataclysmDungeon Deep =
			UCataclysmSurgeScheduler::MakeFallenCityDungeon(9, City, 40, 25);

		TestEqual(TEXT("a besieged Outpost is as deep as its siege"),
				  Deep.Floors, 25);
		TestEqual(TEXT("and holds one boss each"), Deep.Bosses, 25);
	}

	{
		const FCataclysmDungeon Few =
			UCataclysmSurgeScheduler::MakeFallenCityDungeon(9, City, 40, 3);

		TestEqual(TEXT("three dungeons still floor the depth at twenty"),
				  Few.Floors, 20);
		TestEqual(TEXT("but the bosses are the three, not the twenty"),
				  Few.Bosses, 3);
	}

	// EVERY TIER FLOORS AT ITS OWN MINIMUM, which is the spec's shallow end and
	// the design's 20/40/60.
	for (const TPair<ECataclysmCityTier, int32>& Pair :
			TMap<ECataclysmCityTier, int32>{
				{ ECataclysmCityTier::Outpost, 20 },
				{ ECataclysmCityTier::Bulwark, 40 },
				{ ECataclysmCityTier::Sanctuary, 60 } })
	{
		FCataclysmCity Host;
		Host.CityId = 1;
		Host.Tier = Pair.Key;

		const FCataclysmDungeon Made =
			UCataclysmSurgeScheduler::MakeFallenCityDungeon(1, Host, 0, 1);

		TestEqual(TEXT("the tier's minimum is the design's"), Made.Floors,
				  Pair.Value);
	}

	// AND A COUNT OF ZERO STILL LEAVES A DUNGEON WITH A BOSS IN IT. It should
	// not arise -- a city falls because a dungeon standing on it resolved -- but
	// a floorless, bossless dungeon would be worse than the floor of one.
	{
		const FCataclysmDungeon None =
			UCataclysmSurgeScheduler::MakeFallenCityDungeon(9, City, 40, 0);

		TestEqual(TEXT("no siege still floors at twenty"), None.Floors, 20);
		TestEqual(TEXT("and still holds one boss"), None.Bosses, 1);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
