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
	//
	// THE OPENING WAVE IS THE ONE EXCEPTION, since 2026-10-20: the first surge
	// of a run brings `OpeningDungeonsPerSurge`, 12, and every later one
	// `DungeonsPerSurge`, 8. Until then every wave here was 5. "The same wave
	// for ever" is the 8 from index 1 on.
	TestEqual(TEXT("the opening surge, at index 0, brings twelve dungeons"),
			  CountAtIndex(Scheduler, 0), 12);
	TestEqual(TEXT("and the gap after it is 90 days"),
			  GapAtIndex(Scheduler, 0), 90.0f, 0.001f);

	for (const int32 Index : { 1, 5, 20, 100 })
	{
		TestEqual(FString::Printf(
			TEXT("surge %d still brings eight dungeons"), Index),
			CountAtIndex(Scheduler, Index), 8);
		TestEqual(FString::Printf(
			TEXT("and the gap after surge %d is still 90 days"), Index),
			GapAtIndex(Scheduler, Index), 90.0f, 0.001f);
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

	// HAND-WORKED FROM `Simulation.surge_gap`: 90 x 0.88 to the power of the
	// surge index, floored at 25.
	TestEqual(TEXT("the first gap is the full 90 days"),
			  GapAtIndex(Scheduler, 0), 90.0f, 0.001f);
	TestEqual(TEXT("the second is 79.2"),
			  GapAtIndex(Scheduler, 1), 79.2f, 0.01f);
	TestEqual(TEXT("the third is 69.696"),
			  GapAtIndex(Scheduler, 2), 69.696f, 0.01f);
	TestEqual(TEXT("the fourth is 61.3325"),
			  GapAtIndex(Scheduler, 3), 61.3325f, 0.01f);

	// AND IT STOPS. Without a floor an accelerating run ends in a surge every
	// day, which is arithmetic running away rather than difficulty.
	//
	// AT 90 DAYS THE FLOOR IS FIRST REACHED AT INDEX 11: 90 x 0.88 to the 11th
	// is 22.06. At 120 days it was first reached at index 13.
	TestEqual(TEXT("by the eleventh it is 25.07, just above the floor"),
			  GapAtIndex(Scheduler, 10), 25.0651f, 0.01f);
	TestEqual(TEXT("the twelfth is the floor itself"),
			  GapAtIndex(Scheduler, 11), 25.0f, 0.001f);
	TestEqual(TEXT("the fourteenth is the floor itself"),
			  GapAtIndex(Scheduler, 13), 25.0f, 0.001f);
	TestEqual(TEXT("and it never goes below it"),
			  GapAtIndex(Scheduler, 50), 25.0f, 0.001f);
	TestEqual(TEXT("nor after a hundred surges"),
			  GapAtIndex(Scheduler, 100), 25.0f, 0.001f);

	// THE WAVE ITSELF DOES NOT GROW IN THIS MODE. That is the whole difference
	// between accelerating and swelling, and a test that only checked the gap
	// would pass if the two modes had been swapped.
	//
	// THE OPENING WAVE OF 12 AND THEN 8, as in a static run, since 2026-10-20.
	// A swelling wave would be 8 + 0.5 x 20 = 18, capped at 14, after twenty.
	TestEqual(TEXT("an accelerating surge opens with twelve dungeons"),
			  CountAtIndex(Scheduler, 0), 12);
	TestEqual(TEXT("then brings eight"),
			  CountAtIndex(Scheduler, 1), 8);
	TestEqual(TEXT("and still eight after twenty surges"),
			  CountAtIndex(Scheduler, 20), 8);

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
	// than every one. The opening wave is 12 + 0.5 x 0 = 12, and from index 1
	// the growth is added to 8: 8.5, 9, 9.5, 10 as a real number. So a
	// swelling run's second wave is SMALLER than its first, since 2026-10-20,
	// as the model's is. Until then the ladder was 5, 5, 6, 6, 7.
	TestEqual(TEXT("the first wave is the opening twelve"),
			  CountAtIndex(Scheduler, 0), 12);
	TestEqual(TEXT("the second is eight"), CountAtIndex(Scheduler, 1), 8);
	TestEqual(TEXT("the third is nine"), CountAtIndex(Scheduler, 2), 9);
	TestEqual(TEXT("the fourth is still nine"), CountAtIndex(Scheduler, 3), 9);
	TestEqual(TEXT("the fifth is ten"), CountAtIndex(Scheduler, 4), 10);

	// AND IT STOPS AT FOURTEEN. From 8 the ceiling is first reached at index
	// 12: 8 + 0.5 x 12 is 14. From 5 it was first reached at index 18, and
	// from 4 at index 20. The pair at 11 and 12 is the one that says where.
	TestEqual(TEXT("by the twelfth it is thirteen"),
			  CountAtIndex(Scheduler, 11), 13);
	TestEqual(TEXT("the thirteenth is the ceiling"),
			  CountAtIndex(Scheduler, 12), 14);
	TestEqual(TEXT("the eighteenth is the ceiling, and was thirteen at 5 a wave"),
			  CountAtIndex(Scheduler, 17), 14);
	TestEqual(TEXT("the nineteenth is still the ceiling"),
			  CountAtIndex(Scheduler, 18), 14);
	TestEqual(TEXT("the twenty-first is the ceiling"),
			  CountAtIndex(Scheduler, 20), 14);
	TestEqual(TEXT("and it never goes above it"),
			  CountAtIndex(Scheduler, 21), 14);
	TestEqual(TEXT("nor after a hundred surges"),
			  CountAtIndex(Scheduler, 100), 14);

	// THE GAP DOES NOT SHORTEN IN THIS MODE.
	TestEqual(TEXT("a swelling surge still comes every 90 days"),
			  GapAtIndex(Scheduler, 20), 90.0f, 0.001f);

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
			  GapAtIndex(Scheduler, 1), 79.2f, 0.01f);
	TestEqual(TEXT("down to the same floor"),
			  GapAtIndex(Scheduler, 13), 25.0f, 0.001f);
	TestEqual(TEXT("and the wave grows like a swelling one"),
			  CountAtIndex(Scheduler, 4), 10);
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

	// THE OPENING WAVE, since the three schedulers have fired nothing.
	// FIFTEEN IS A QUARTER MORE THAN TWELVE: trunc(12 x 1.25) is 15, and a
	// later wave's trunc(8 x 1.25) is 10, a quarter more again. At 5 a wave,
	// from 2026-10-13 to 2026-10-20, it was trunc(6.25) = 6, a fifth more;
	// at a wave of four it was trunc(5.0) = 5. The design document says "25%
	// more dungeons".
	TestEqual(TEXT("Standard opens with twelve"),
			  Standard->DungeonsInNextSurge(), 12);
	TestEqual(TEXT("Hardcore opens with twelve as well"),
			  Hardcore->DungeonsInNextSurge(), 12);
	TestEqual(TEXT("and Heretic opens with fifteen"),
			  Heretic->DungeonsInNextSurge(), 15);

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
	TestEqual(TEXT("the next is due on day 90"),
			  Scheduler->NextSurgeDay, 90.0f, 0.001f);

	TestFalse(TEXT("nothing is due on day 1"), Scheduler->IsDue(1));
	TestEqual(TEXT("with 89 days to wait"),
			  Scheduler->DaysUntilNextSurge(1), 89.0f, 0.001f);
	TestFalse(TEXT("nor on day 89"), Scheduler->IsDue(89));
	TestTrue(TEXT("and it is due on day 90"), Scheduler->IsDue(90));

	// A LATE CALLER IS NOT PUNISHED FOR BEING LATE. The next surge is set from
	// the day it actually fired, not from the day it was due, so a run that
	// skipped several days at once does not immediately owe several surges.
	Scheduler->RecordSurge(130);
	TestEqual(TEXT("firing late puts the next one 90 days after the firing"),
			  Scheduler->NextSurgeDay, 220.0f, 0.001f);

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

	// FIVE HUNDRED ROLLS, NOT A WAVE'S WORTH. A wave of eight or twelve could
	// miss a sealed city by luck; the point is that it cannot reach one at all.
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

	// THE OPENING WAVE, because a new scheduler is at `SurgeIndex` 0: 12 since
	// 2026-10-20, and 5 until then. The bare map has no dungeon caps, so
	// every target `PickTargets` rolls becomes a dungeon.
	if (!TestEqual(TEXT("a static opening wave is twelve dungeons"),
				   WaveOne.Num(), 12))
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
	// Each was first the fraction it replaced multiplied by that tier's base
	// maximum: 10% of 1,000, 9% of 3,000, 8% of 8,000 and 6% of 20,000, which
	// is 100, 270, 640 and 1,200 defence and 250, 1,000, 2,400 and 4,500
	// people. SINCE 2026-10-20 EACH IS 0.7 OF THAT, as the model's has been
	// since 2026-10-16. Written here as literals rather than as either
	// product, because a test that recomputed the product would agree with
	// the code by construction.
	TestEqual(TEXT("an Outpost dungeon takes 70 defence points"),
			  Outpost.DefenceDamage, 70.0f);
	TestEqual(TEXT("and 175 people"), Outpost.PopulationDamage, 175.0f);
	TestEqual(TEXT("a Bulwark dungeon takes 189"), Bulwark.DefenceDamage, 189.0f);
	TestEqual(TEXT("and 700 people"), Bulwark.PopulationDamage, 700.0f);
	TestEqual(TEXT("a Sanctuary dungeon takes 448"), Sanctuary.DefenceDamage,
			  448.0f);
	TestEqual(TEXT("and 1,680 people"), Sanctuary.PopulationDamage, 1680.0f);
	TestEqual(TEXT("and a Pillar dungeon takes 840"), Pillar.DefenceDamage,
			  840.0f);
	TestEqual(TEXT("and 3,150 people"), Pillar.PopulationDamage, 3150.0f);

	// A BIGGER CITY TAKES MORE POINTS AND A SMALLER SHARE OF ITSELF, and both
	// halves are asserted because each on its own would be satisfied by the
	// wrong table. Before issue #1331 the first half was false -- a Sanctuary
	// carried 0.08 against an Outpost's 0.10 -- so a test written against the
	// old field would now fail rather than silently reverse. Multiplying every
	// row by 0.7 moves neither half: 448 against 70 points, and 5.6% against
	// 7%.
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

	// EVERY NEW KIND'S OWN ROW CARRIES NO DAMAGE, asserted rather than assumed.
	// A Fallen City and a Cataclysm stand on a city whose damage is already
	// done. A Quest dungeon's timer does hit its host since 2026-10-13, and
	// that hit is the ORDINARY row of the host's tier, not this row.
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
	TestEqual(TEXT("Timed is 19.7"),
			  UCataclysmSurgeScheduler::SpawnWeightFor(
				  ECataclysmDungeonSubType::Timed), 19.7f, 0.0001f);

	TestEqual(TEXT("Horde is 19.7"),
			  UCataclysmSurgeScheduler::SpawnWeightFor(
				  ECataclysmDungeonSubType::Horde), 19.7f, 0.0001f);

	// SEVEN AND A HALF SINCE 2026-09-06, HALVED FROM 15 ON ISSUE #1349. The
	// slack it gave up went to the others, which is why none of them is a round
	// number any more.
	TestEqual(TEXT("Siege is 7.5"),
			  UCataclysmSurgeScheduler::SpawnWeightFor(
				  ECataclysmDungeonSubType::Siege), 7.5f, 0.0001f);

	// AND THE COW LEVEL IS BACK AT THE 7 ISSUE #1293 DECIDED. Sharing the
	// Siege's slack across all six had carried it to 7.6, above the Siege,
	// which made the Siege the rarest sub-type in the game as arithmetic rather
	// than by anyone's choice. Issue #1369: the owner delegated it with one
	// constraint, "Your call, but the cow level should be pretty rare".
	TestEqual(TEXT("Cow Level is 7"),
			  UCataclysmSurgeScheduler::SpawnWeightFor(
				  ECataclysmDungeonSubType::CowLevel), 7.0f, 0.0001f);

	TestEqual(TEXT("Elite is 16.4"),
			  UCataclysmSurgeScheduler::SpawnWeightFor(
				  ECataclysmDungeonSubType::Elite), 16.4f, 0.0001f);

	TestEqual(TEXT("Volatile is 16.4"),
			  UCataclysmSurgeScheduler::SpawnWeightFor(
				  ECataclysmDungeonSubType::Volatile), 16.4f, 0.0001f);

	// SACRIFICIAL CARRIES THE ROUNDING REMAINDER, which is why it is 13.3 and
	// not the 13.15 an exact rescale of its 12 would give: the seven have to
	// total exactly 100.
	TestEqual(TEXT("Sacrificial is 13.3"),
			  UCataclysmSurgeScheduler::SpawnWeightFor(
				  ECataclysmDungeonSubType::Sacrificial), 13.3f, 0.0001f);

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
	//
	// AGAINST EVERY OTHER SUB-TYPE AND NOT MERELY AGAINST SACRIFICIAL. Until
	// issue #1369 this compared the Cow Level with Sacrificial alone, so it went
	// on passing through the whole period when the Cow Level was 7.6 and the
	// Siege 7.5 -- the one stretch where the sentence above it was false. A
	// three-rung ladder cannot see a reversal at the bottom of the table, and
	// this is what it costs to write one.
	for (uint8 Value = static_cast<uint8>(ECataclysmDungeonSubType::Timed);
		 Value <= static_cast<uint8>(ECataclysmDungeonSubType::Sacrificial);
		 ++Value)
	{
		const ECataclysmDungeonSubType SubType =
			static_cast<ECataclysmDungeonSubType>(Value);

		if (SubType == ECataclysmDungeonSubType::CowLevel)
		{
			continue;
		}

		// STRICTLY RARER, NOT MERELY NOT-COMMONER. A tie would leave the design
		// sentence above half true and is the shape #1369 was raised about.
		TestTrue(*FString::Printf(
					 TEXT("Cow Level is strictly rarer than sub-type %d"),
					 Value),
				 UCataclysmSurgeScheduler::SpawnWeightFor(
					 ECataclysmDungeonSubType::CowLevel) <
				 UCataclysmSurgeScheduler::SpawnWeightFor(SubType));
	}

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

	// **AND WHEN THE DUNGEON'S KIND BARS A SUB-TYPE, which is the third path.**
	// A Cataclysm may not be a Cow Level -- issue #1333 -- and the bar is
	// applied by shortening the line the draw is read against rather than by
	// refusing afterwards, precisely so that it costs nothing extra. Rolling
	// again for a barred sub-type would be the obvious implementation and would
	// break the promise this test exists for.
	for (const int32 Seed : { 1, 7, 99, 5150, 20260906 })
	{
		FRandomStream Rolled(Seed);
		FRandomStream Drawn(Seed);

		UCataclysmSurgeScheduler::RollSubType(
			Rolled, /*bSiegeAllowed=*/true,
			ECataclysmDungeonType::Cataclysm);
		Drawn.FRand();

		TestEqual(*FString::Printf(
					  TEXT("at seed %d a Cataclysm's roll cost one draw"),
					  Seed),
				  Rolled.GetCurrentSeed(), Drawn.GetCurrentSeed());
	}

	// **BOTH REFUSALS AT ONCE, which is the only path that stacks them.** A
	// Cataclysm on a city that already holds a Siege has one sub-type off the
	// line before the draw and another refused after it, and the re-read has to
	// map onto the five that are left. Seeds chosen by what they draw, for the
	// same reason as above.
	TArray<int32> SeedsThatDrawASiegeOnACataclysm;
	for (int32 Seed = 1;
		 Seed <= 1000 && SeedsThatDrawASiegeOnACataclysm.Num() < 5;
		 ++Seed)
	{
		FRandomStream Trial(Seed);
		if (UCataclysmSurgeScheduler::RollSubType(
				Trial, /*bSiegeAllowed=*/true,
				ECataclysmDungeonType::Cataclysm)
			== ECataclysmDungeonSubType::Siege)
		{
			SeedsThatDrawASiegeOnACataclysm.Add(Seed);
		}
	}

	// THE CONTROL, again. Without seeds that reach both refusals the checks
	// below are the loop above written a third time.
	if (!TestEqual(TEXT("seeds whose Cataclysm draw lands on Siege were found"),
				   SeedsThatDrawASiegeOnACataclysm.Num(), 5))
	{
		return false;
	}

	for (const int32 Seed : SeedsThatDrawASiegeOnACataclysm)
	{
		FRandomStream Rolled(Seed);
		FRandomStream Drawn(Seed);

		const ECataclysmDungeonSubType Got =
			UCataclysmSurgeScheduler::RollSubType(
				Rolled, /*bSiegeAllowed=*/false,
				ECataclysmDungeonType::Cataclysm);
		Drawn.FRand();

		TestNotEqual(*FString::Printf(
						 TEXT("at seed %d the Cataclysm's Siege was refused"),
						 Seed),
					 static_cast<uint8>(Got),
					 static_cast<uint8>(ECataclysmDungeonSubType::Siege));

		// AND IT DID NOT LAND ON THE OTHER BARRED ONE. The re-read maps onto a
		// line the Cow Level is off as well; mapping onto the six the ordinary
		// refusal uses would put it back in play exactly where the ruling says
		// it must not be.
		TestNotEqual(*FString::Printf(
						 TEXT("nor did it become a Cow Level at seed %d"),
						 Seed),
					 static_cast<uint8>(Got),
					 static_cast<uint8>(ECataclysmDungeonSubType::CowLevel));

		TestEqual(*FString::Printf(
					  TEXT("and at seed %d both refusals cost one draw"), Seed),
				  Rolled.GetCurrentSeed(), Drawn.GetCurrentSeed());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeCataclysmNoCowLevelTest,
	"Cataclysm.Surge.ACataclysmDungeonNeverRollsACowLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeCataclysmNoCowLevelTest::RunTest(const FString& Parameters)
{
	// **THE RULING.** The project owner, 2026-09-06, verbatim: "Last stand is a
	// cataclysm dungeon and should not be allowed to roll as a cow level sub
	// type." Asked how far to take it they answered "Only the one you ruled".
	// Issue #1333.
	//
	// WHY. A Cow Level's time "is doubled and cannot be reduced", and the
	// model's Last Stand adds floor bonuses after the dungeon is built and
	// worked its walk out again without the sub-type, losing the doubling.
	// Offered the repair, the owner removed the situation instead.
	//
	// ENOUGH ROLLS THAT AN ABSENCE IS A RULE. Cow Level is 7 in 100, so twenty
	// thousand rolls would carry about 1,400 of them.
	constexpr int32 Rolls = 20000;

	{
		FRandomStream Stream(20260906);
		TMap<ECataclysmDungeonSubType, int32> Counts;

		for (int32 Roll = 0; Roll < Rolls; ++Roll)
		{
			++Counts.FindOrAdd(UCataclysmSurgeScheduler::RollSubType(
				Stream, /*bSiegeAllowed=*/true,
				ECataclysmDungeonType::Cataclysm));
		}

		TestEqual(TEXT("no Cataclysm dungeon rolled a Cow Level"),
				  Counts.FindRef(ECataclysmDungeonSubType::CowLevel), 0);

		// AND IT STILL GOT A SUB-TYPE. Every dungeon has one since the owner's
		// ruling of 2026-09-05; a barred roll is redistributed, never dropped.
		TestEqual(TEXT("nor did one come out with no sub-type at all"),
				  Counts.FindRef(ECataclysmDungeonSubType::None), 0);

		// **THE SEVENTH SUB-TYPE'S WEIGHT IS SPREAD OVER THE OTHER SIX IN
		// PROPORTION, not dropped.** Dropping it would leave the six at their
		// old shares of 100 and 7 rolls in every 100 with nothing to return.
		//
		// IT WAS 92.4 FOR ONE DAY. Halving the Siege weight on issue #1349 gave
		// the other six the slack in proportion, which carried the Cow Level
		// from 7 to 7.6; issue #1369 put the Cow Level back to the 7 that #1293
		// decided, so the shortened line is 93 again.
		const float Total = UCataclysmSurgeScheduler::TotalSpawnWeight(
			ECataclysmDungeonSubType::CowLevel);

		TestEqual(TEXT("the shortened line is 93 points wide"), Total, 93.0f,
				  0.0001f);

		for (uint8 Value = static_cast<uint8>(ECataclysmDungeonSubType::Timed);
			 Value <= static_cast<uint8>(ECataclysmDungeonSubType::Sacrificial);
			 ++Value)
		{
			const ECataclysmDungeonSubType SubType =
				static_cast<ECataclysmDungeonSubType>(Value);

			if (SubType == ECataclysmDungeonSubType::CowLevel)
			{
				continue;
			}

			const float Share = 100.0f * Counts.FindRef(SubType) / Rolls;
			const float Wanted =
				100.0f * UCataclysmSurgeScheduler::SpawnWeightFor(SubType)
				/ Total;

			TestTrue(*FString::Printf(
						 TEXT("sub-type %d was rolled at all"), Value),
					 Counts.FindRef(SubType) > 0);

			TestEqual(*FString::Printf(
						  TEXT("sub-type %d took %.2f%% of a Cataclysm's rolls, "
							   "wanted %.2f%%"),
						  Value, Share, Wanted),
					  Share, Wanted, 2.0f);

			// AND EVERY SHARE IS ABOVE WHAT IT HAS ON THE FULL LINE, which is
			// the difference between spreading the seven points and dropping
			// them. The gap is smallest for Sacrificial at 12.90% against
			// 12.00%, so the tolerance above cannot hide it.
			TestTrue(*FString::Printf(
						 TEXT("sub-type %d gained from the redistribution"),
						 Value),
					 Wanted > UCataclysmSurgeScheduler::SpawnWeightFor(SubType));
		}
	}

	// **THE CONTROL, AND WITHOUT IT NONE OF THE ABOVE MEANS ANYTHING.** A
	// `RollSubType` that had lost Cow Level from its weights entirely, or a
	// `SpawnWeightFor` returning zero for it, would satisfy every check above
	// and would be a different bug. The same stream, the same count of rolls,
	// asked for an ordinary dungeon.
	{
		FRandomStream Stream(20260906);
		int32 CowLevels = 0;

		for (int32 Roll = 0; Roll < Rolls; ++Roll)
		{
			if (UCataclysmSurgeScheduler::RollSubType(
					Stream, /*bSiegeAllowed=*/true,
					ECataclysmDungeonType::Basic)
				== ECataclysmDungeonSubType::CowLevel)
			{
				++CowLevels;
			}
		}

		TestTrue(TEXT("an ordinary dungeon still rolls Cow Levels"),
				 CowLevels > 0);
	}

	// **ONLY THE ONE PAIR THE OWNER RULED.** Twenty-seven of the twenty-eight
	// stay legal, and this is where that is held: a session barring a second
	// pair by inference has to break a test that quotes the scope back.
	for (const ECataclysmDungeonType Kind : { ECataclysmDungeonType::Basic,
											  ECataclysmDungeonType::Quest,
											  ECataclysmDungeonType::FallenCity })
	{
		FRandomStream Stream(5150);
		TSet<ECataclysmDungeonSubType> Seen;

		for (int32 Roll = 0; Roll < Rolls; ++Roll)
		{
			Seen.Add(UCataclysmSurgeScheduler::RollSubType(
				Stream, /*bSiegeAllowed=*/true, Kind));
		}

		for (uint8 Value = static_cast<uint8>(ECataclysmDungeonSubType::Timed);
			 Value <= static_cast<uint8>(ECataclysmDungeonSubType::Sacrificial);
			 ++Value)
		{
			TestTrue(*FString::Printf(
						 TEXT("dungeon kind %d can still roll sub-type %d"),
						 static_cast<uint8>(Kind), Value),
					 Seen.Contains(
						 static_cast<ECataclysmDungeonSubType>(Value)));
		}
	}

	// AND THE RULE ITSELF, ASKED DIRECTLY. `BarredSubTypeOn` is what the roll
	// consults, and `None` is how it says "nothing is barred".
	TestEqual(TEXT("a Cataclysm is barred from Cow Level"),
			  static_cast<uint8>(UCataclysmSurgeScheduler::BarredSubTypeOn(
				  ECataclysmDungeonType::Cataclysm)),
			  static_cast<uint8>(ECataclysmDungeonSubType::CowLevel));

	for (const ECataclysmDungeonType Kind : { ECataclysmDungeonType::Basic,
											  ECataclysmDungeonType::Quest,
											  ECataclysmDungeonType::FallenCity })
	{
		TestEqual(*FString::Printf(
					  TEXT("dungeon kind %d is barred from nothing"),
					  static_cast<uint8>(Kind)),
				  static_cast<uint8>(
					  UCataclysmSurgeScheduler::BarredSubTypeOn(Kind)),
				  static_cast<uint8>(ECataclysmDungeonSubType::None));
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
 * The four KINDS have readable names too, and an ordinary one has none.
 *
 * `UCataclysmEmpireRun::Describe` NAMES THE KIND BESIDE THE SUB-TYPE since issue
 * #1324 slice 5, so a person reading a run can see which standing dungeon is the
 * Quest dungeon that would earn them an objective. Before that the kind was
 * invisible and the three non-Basic kinds read exactly like ordinary ones.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeKindNameTest,
	"Cataclysm.Surge.EveryKindHasAReadableNameAndAnOrdinaryOneHasNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeKindNameTest::RunTest(const FString& Parameters)
{
	// `TestEqualSensitive` FOR THE SAME REASON AS ABOVE, and hand-written
	// spellings for the same reason: `FallenCity` is two words when a person
	// reads it and one identifier when the compiler does.
	TestEqualSensitive(TEXT("Quest"), UCataclysmSurgeScheduler::KindName(
		ECataclysmDungeonType::Quest), FString(TEXT("Quest")));

	TestEqualSensitive(TEXT("Fallen City is two words"),
		UCataclysmSurgeScheduler::KindName(
			ECataclysmDungeonType::FallenCity), FString(TEXT("Fallen City")));

	TestEqualSensitive(TEXT("Cataclysm"), UCataclysmSurgeScheduler::KindName(
		ECataclysmDungeonType::Cataclysm), FString(TEXT("Cataclysm")));

	// AN ORDINARY DUNGEON SAYS NOTHING, matching `None` above. Most dungeons
	// are Basic and "dungeon 3 (Basic) on Outpost (1,2)" would put a word on
	// every line to distinguish the common case from itself.
	TestTrue(TEXT("and an ordinary dungeon has no kind name"),
			 UCataclysmSurgeScheduler::KindName(
				 ECataclysmDungeonType::Basic).IsEmpty());

	// AND THE THREE THAT DO HAVE ONE ALL DIFFER. A switch that fell through
	// would answer the same word for two kinds and every assertion above could
	// still pass if that word happened to be right for one of them.
	const FString QuestName =
		UCataclysmSurgeScheduler::KindName(ECataclysmDungeonType::Quest);
	const FString FallenName =
		UCataclysmSurgeScheduler::KindName(ECataclysmDungeonType::FallenCity);
	const FString BossName =
		UCataclysmSurgeScheduler::KindName(ECataclysmDungeonType::Cataclysm);

	// `Equals` WITH `CaseSensitive` RATHER THAN `!=`, because FString's own
	// comparison ignores case and would call "Quest" and "quest" the same word.
	TestTrue(TEXT("the three named kinds are three different words"),
			 !QuestName.Equals(FallenName, ESearchCase::CaseSensitive)
				 && !FallenName.Equals(BossName, ESearchCase::CaseSensitive)
				 && !QuestName.Equals(BossName, ESearchCase::CaseSensitive));

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
	// FORTY CAMPAIGNS RATHER THAN ONE. A single 600 day campaign makes too few
	// dungeons to compare a 7-in-100 sub-type against an exception that arose
	// a few times in a hundred. Pooling also stops the answer being a fact
	// about one seed.
	//
	// IT WAS TWENTY UNTIL 2026-10-13. The floor of 1,000 below was kept and
	// this was raised from 20 to 40, a labelled judgement by the coordinating
	// session under the owner's delegation, 2026-10-13. Issue #2380.
	//
	// WHAT WAS MEASURED, AND WHEN. Every figure here is at TWENTY campaigns.
	//
	//   - Recorded in the entry of 2026-09-06 in `docs/DECISIONS.md`, with
	//     every city's fall firing a surge and before a refused Siege was
	//     spread across the others: 10,914 days in all, 1,925 dungeons made.
	//   - This test's own log line in the Unreal run of 2026-10-12 before the
	//     change for issue #2380, with every fall still firing a surge:
	//     "20 campaigns, 12000 days in all, 1580 dungeons made: 0 with no
	//     sub-type (0.0%), 94 Cow Levels, 116 Sieges".
	//   - The same line in the Unreal run of 2026-10-12 with that change in,
	//     so that a fall fires a surge only when the last one is at least 25
	//     days behind: "20 campaigns, 12000 days in all, 687 dungeons made: 0
	//     with no sub-type (0.0%), 44 Cow Levels, 60 Sieges". That is 34 a
	//     campaign and under the floor, which is why twenty stopped being
	//     enough.
	//
	// MEASURED AT 40 CAMPAIGNS on 2026-10-13, in the Unreal window at 2604b2d3, with the
	// fall surge's least interval in: "40 campaigns, 24000 days in all, 1488 dungeons
	// made: 0 with no sub-type (0.0%), 108 Cow Levels, 125 Sieges". This comment said
	// the figure had not been measured until that run.
	constexpr int32 Campaigns = 40;
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
	// across the other six. That was at twenty campaigns with every city's
	// fall firing a surge; see the comment on `Campaigns` above.
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
	const float Left = 100.0f - 7.5f;

	const TPair<ECataclysmDungeonSubType, float> Expected[] = {
		{ ECataclysmDungeonSubType::Timed,		 19.7f },
		{ ECataclysmDungeonSubType::Horde,		 19.7f },
		{ ECataclysmDungeonSubType::CowLevel,	  7.0f },
		{ ECataclysmDungeonSubType::Elite,		 16.4f },
		{ ECataclysmDungeonSubType::Volatile,	 16.4f },
		{ ECataclysmDungeonSubType::Sacrificial, 13.3f },
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

// ---------------------------------------------------------------------------
// Quest dungeons -- issue #1324 slice 3
// ---------------------------------------------------------------------------

/**
 * A rolled kind is `Basic` or `Quest`, at the stated share, and never anything
 * else.
 *
 * THE SHARE IS CHECKED AGAINST THE CONSTANT AND NOT AGAINST A WRITTEN-DOWN
 * PERCENTAGE, because `QuestChance` is a port of the model's number and issue
 * #1357 is expected to replace it with a rule keyed on which Cataclysm is
 * running. A test naming 12% would fail on the day that lands and say nothing
 * useful; this one keeps measuring whatever the constant says.
 *
 * THE TOLERANCE IS WORKED OUT RATHER THAN GUESSED. Twenty thousand draws of a
 * 12-in-100 event have a standard deviation of about 0.23 percentage points, so
 * a band of two points is more than eight deviations wide. It fails on a rate
 * that is actually wrong and not on an unlucky seed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeQuestChanceTest,
	"Cataclysm.Surge.ARolledKindIsBasicOrQuestAtTheStatedChance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeQuestChanceTest::RunTest(const FString& Parameters)
{
	constexpr int32 Draws = 20000;

	FRandomStream Stream(20260906);

	TMap<ECataclysmDungeonType, int32> Rolled;
	for (int32 Draw = 0; Draw < Draws; ++Draw)
	{
		++Rolled.FindOrAdd(UCataclysmSurgeScheduler::RollKind(Stream));
	}

	const int32 Quests = Rolled.FindRef(ECataclysmDungeonType::Quest);
	const int32 Basics = Rolled.FindRef(ECataclysmDungeonType::Basic);

	AddInfo(FString::Printf(
		TEXT("%d draws: %d Quest (%.2f%%), %d Basic, %d kinds seen"),
		Draws, Quests, 100.0f * Quests / Draws, Basics, Rolled.Num()));

	// NOTHING BUT THOSE TWO. A Fallen City is what a city became rather than
	// something a surge rolled, and nothing builds a Cataclysm until slice 6.
	TestEqual(TEXT("only two kinds are ever rolled"), Rolled.Num(), 2);
	TestEqual(TEXT("and every draw was one of them"), Quests + Basics, Draws);

	const float Wanted = UCataclysmSurgeScheduler::QuestChance;
	const float Seen = static_cast<float>(Quests) / Draws;

	TestTrue(FString::Printf(
		TEXT("%.2f%% of draws were Quest, against the stated %.2f%%"),
		100.0f * Seen, 100.0f * Wanted),
		FMath::Abs(Seen - Wanted) < 0.02f);

	// AND BOTH ACTUALLY HAPPEN. A constant of zero or one would satisfy the
	// band above only by moving it, but a test that never saw one of the two
	// kinds would still read as a pass if the band were ever widened.
	TestTrue(TEXT("Quest dungeons are rolled at all"), Quests > 0);
	TestTrue(TEXT("and so are Basic ones"), Basics > 0);

	return true;
}

/**
 * A Quest dungeon is built to the Quest row of the spec, and its timer comes
 * from its depth as an ordinary dungeon's does.
 *
 * WHAT IT SHARES WITH A BASIC DUNGEON IS AS MUCH THE POINT AS WHAT IT DOES NOT.
 * One boss on the final floor, a rolled sub-type, a walk of a day a floor, and
 * since 2026-10-19 a timer drawn from the depth with the same jitter, are all
 * the ordinary rules; only the floor range differs.
 *
 * **THIS WAS `...WithAFlatTimer` UNTIL 2026-10-19**, and asserted the timer was
 * exactly 25 days. The owner ruled on 2026-10-16 that a Quest dungeon's timer
 * follows its depth; `docs/DECISIONS.md`, 2026-10-19, has the change.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeQuestDungeonTest,
	"Cataclysm.Surge.AQuestDungeonIsBuiltToItsOwnSpecWithATimerFromItsDepth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeQuestDungeonTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	UCataclysmSurgeScheduler* Scheduler = MakeScheduler(ECataclysmSurgeMode::Static);
	UCataclysmEmpireMap* Map = MakeMap();
	FRandomStream Stream(777);

	const ECataclysmCityTier Tiers[] = {
		ECataclysmCityTier::Outpost,
		ECataclysmCityTier::Bulwark,
		ECataclysmCityTier::Sanctuary,
	};

	for (const ECataclysmCityTier Tier : Tiers)
	{
		const FCataclysmCity* City = Map->Cities.FindByPredicate(
			[Tier](const FCataclysmCity& Candidate)
			{
				return Candidate.Tier == Tier;
			});

		if (!TestNotNull(TEXT("a city of that tier exists"), City))
		{
			return false;
		}

		const FCataclysmDungeonSpec Quest =
			UCataclysmSurgeScheduler::SpecFor(ECataclysmDungeonType::Quest, Tier);

		const FCataclysmDungeonSpec Basic =
			UCataclysmSurgeScheduler::SpecFor(ECataclysmDungeonType::Basic, Tier);

		// A QUEST DUNGEON IS THE DEEPER OF THE TWO AT BOTH ENDS, at every tier,
		// which is what the depth check below is really reading.
		//
		// **DEPTH ALONE CANNOT TELL THE KINDS APART EVERYWHERE, AND THIS TEST
		// WAS WRITTEN AS THOUGH IT COULD.** The first version of it asserted
		// that a Quest dungeon starts deeper than a Basic one ENDS, and that is
		// false at a Sanctuary: Quest is 30 to 50 there and Basic is 25 to 40,
		// so the two overlap between 30 and 40. The claim holds at an Outpost
		// (20-30 against 8-15) and a Bulwark (30-45 against 15-25) and nowhere
		// else. That is why the assertions inside the loop read the KIND and the
		// TIMER as well as the depth: at a Sanctuary the depth is not on its own
		// evidence of anything.
		if (!TestTrue(FString::Printf(
				TEXT("a %s Quest dungeon is deeper at both ends than a Basic "
					 "one (%d-%d against %d-%d)"),
				*UCataclysmEmpireMap::TierName(Tier), Quest.LeastFloors,
				Quest.MostFloors, Basic.LeastFloors, Basic.MostFloors),
				Quest.LeastFloors > Basic.LeastFloors
				&& Quest.MostFloors > Basic.MostFloors))
		{
			return false;
		}

		TSet<int32> Depths;

		// THE TIMERS, IN HUNDREDTHS OF A DAY, overall and by floor count. Rounded
		// as `Cataclysm.Surge.ADungeonsTimerComesFromItsDepthAndThenVaries`
		// rounds them, so two floats that differ only in their last bit are
		// one value.
		TSet<int32> Timers;
		TMap<int32, TSet<int32>> TimersAtDepth;
		TMap<int32, int32> RollsAtDepth;

		for (int32 Roll = 0; Roll < 200; ++Roll)
		{
			const FCataclysmDungeon Made = Scheduler->MakeDungeon(
				Roll, *City, 3, Stream, /* bSiegeAllowed */ true,
				ECataclysmDungeonType::Quest);

			Depths.Add(Made.Floors);

			TestEqual(TEXT("it is a Quest dungeon"), Made.Type,
					  ECataclysmDungeonType::Quest);

			TestTrue(FString::Printf(
				TEXT("a %s Quest dungeon of %d floors is inside %d to %d"),
				*UCataclysmEmpireMap::TierName(Tier), Made.Floors,
				Quest.LeastFloors, Quest.MostFloors),
				Made.Floors >= Quest.LeastFloors
				&& Made.Floors <= Quest.MostFloors);

			// A TIMER FROM ITS DEPTH, PLUS OR MINUS THE JITTER, the same band
			// an ordinary dungeon's timer lies in. Until 2026-10-19 this asserted
			// a flat 25 days. The timer has hit the host city when it runs out
			// since 2026-10-13, so a deeper Quest dungeon now bites on a slower
			// schedule, which is the trade every other dungeon makes.
			const float Base = UCataclysmDayClock::ResolveDaysFor(Made.Floors);
			const float Least =
				Base * (1.0f - UCataclysmSurgeScheduler::ResolveJitter);
			const float Most =
				Base * (1.0f + UCataclysmSurgeScheduler::ResolveJitter);

			TestTrue(FString::Printf(
				TEXT("a %s Quest dungeon of %d floors has a timer of %.2f days, "
					 "within the jitter of %.2f"),
				*UCataclysmEmpireMap::TierName(Tier), Made.Floors,
				Made.ResolveDays, Base),
				Made.ResolveDays >= Least - 0.001f
				&& Made.ResolveDays <= Most + 0.001f);

			const int32 Hundredths = FMath::RoundToInt(Made.ResolveDays * 100.0f);
			Timers.Add(Hundredths);
			TimersAtDepth.FindOrAdd(Made.Floors).Add(Hundredths);
			++RollsAtDepth.FindOrAdd(Made.Floors);

			// AND ITS OWN ROW CARRIES NO DAMAGE. What its timer takes from the
			// host is the ordinary row of the host's tier, which is
			// `Cataclysm.EmpireRun.AQuestDungeonsTimerTakesTheOrdinaryRowOfTheCityItStandsOn`.
			TestEqual(TEXT("it takes no defence"), Made.DefenceDamage, 0.0f);
			TestEqual(TEXT("and no population"), Made.PopulationDamage, 0.0f);
			TestFalse(TEXT("so it is not a kind that detonates"),
					  Made.Resolves());

			// THE ORDINARY RULES STILL APPLY. One boss on the final floor is
			// the design's universal rule and a Quest dungeon is not the stated
			// exception; a Fallen City is.
			TestEqual(TEXT("and it holds the one boss every dungeon holds"),
					  Made.Bosses, 1);

			TestTrue(TEXT("and costs days to walk"), Made.WalkDays >= 1.0f);
		}

		TestTrue(FString::Printf(
			TEXT("%s Quest dungeons vary in depth: %d different ones in 200 "
				 "rolls"), *UCataclysmEmpireMap::TierName(Tier), Depths.Num()),
			Depths.Num() > 1);

		// THE TIMERS VARY ACROSS THE ROLLS. A flat timer gives one value here,
		// which is what this test asserted until 2026-10-19.
		TestTrue(FString::Printf(
			TEXT("%s Quest dungeon timers vary: %d different ones in 200 rolls"),
			*UCataclysmEmpireMap::TierName(Tier), Timers.Num()),
			Timers.Num() > 1);

		// AND THEY VARY AT ONE DEPTH, WHICH ONLY THE JITTER DOES. The check
		// above passes for a timer that is the depth's figure with no jitter,
		// because the depths vary; two rolls of the same floor count would then
		// share one timer. So this groups the rolls by floor count and asks for
		// a floor count whose rolls carry more than one timer. Without the
		// jitter every group holds one value and this fails.
		//
		// THE CONTROL FIRST: the grouping must have found a floor count rolled
		// at least twice, or the jitter check below has nothing to compare and
		// fails for a reason that says nothing about the jitter.
		int32 MostRollsAtOneDepth = 0;
		for (const TPair<int32, int32>& Pair : RollsAtDepth)
		{
			MostRollsAtOneDepth = FMath::Max(MostRollsAtOneDepth, Pair.Value);
		}

		int32 MostTimersAtOneDepth = 0;
		for (const TPair<int32, TSet<int32>>& Pair : TimersAtDepth)
		{
			MostTimersAtOneDepth =
				FMath::Max(MostTimersAtOneDepth, Pair.Value.Num());
		}

		TestTrue(FString::Printf(
			TEXT("CONTROL: some %s floor count was rolled at least twice (at "
				 "most %d times)"),
			*UCataclysmEmpireMap::TierName(Tier), MostRollsAtOneDepth),
			MostRollsAtOneDepth >= 2);

		TestTrue(FString::Printf(
			TEXT("%s Quest dungeons of the same floor count carry different "
				 "timers: at most %d at one depth"),
			*UCataclysmEmpireMap::TierName(Tier), MostTimersAtOneDepth),
			MostTimersAtOneDepth >= 2);
	}

	// A BASIC DUNGEON IS UNCHANGED BY ALL OF THIS, checked here rather than left
	// to the older tests, because the kind became a parameter of the same
	// function and a mistake in the default would make every dungeon a Quest.
	{
		const FCataclysmCity* Outpost = Map->Find(0);
		if (!TestNotNull(TEXT("Outpost 0 exists"), Outpost))
		{
			return false;
		}

		const FCataclysmDungeon Made =
			Scheduler->MakeDungeon(1, *Outpost, 3, Stream);

		TestEqual(TEXT("asked for nothing in particular, it is Basic"),
				  Made.Type, ECataclysmDungeonType::Basic);
		TestTrue(TEXT("and it is a kind that detonates"), Made.Resolves());
		// THE SAME BAND THE QUEST DUNGEONS ABOVE LIE IN. Until 2026-10-19 this
		// asserted only that the timer was not the flat Quest figure of 25
		// days; with that figure gone, the band is what says the timer came
		// from the depth.
		const float Base = UCataclysmDayClock::ResolveDaysFor(Made.Floors);
		const float Least =
			Base * (1.0f - UCataclysmSurgeScheduler::ResolveJitter);
		const float Most =
			Base * (1.0f + UCataclysmSurgeScheduler::ResolveJitter);

		TestTrue(FString::Printf(
			TEXT("and its timer of %.2f days came from its %d floors: within "
				 "the jitter of %.2f"), Made.ResolveDays, Made.Floors, Base),
			Made.ResolveDays >= Least - 0.001f
			&& Made.ResolveDays <= Most + 0.001f);
	}

	return true;
}

/**
 * A CAMPAIGN ACTUALLY PUTS QUEST DUNGEONS ON THE MAP.
 *
 * WHY THIS IS SEPARATE FROM THE TWO ABOVE. One of them calls `RollKind` and the
 * other calls `MakeDungeon` with a kind handed to it; neither would notice if
 * `RollWave` never asked for one, which is exactly the gap that let slice 1's
 * `SpecFor` answer for four kinds while nothing created any of them. This drives
 * `UCataclysmEmpireRun` from `Begin` and reads what is standing on the map, so
 * the whole path from a surge firing to a dungeon existing is covered.
 *
 * IT COUNTS DUNGEONS AS THEY ARRIVE rather than censusing the board at the end,
 * for the reason `Cataclysm.Surge.NoDungeonACampaignMakesComesOutWithoutASubType`
 * gives above: a city falling absorbs everything standing on it, so a census
 * under-counts whatever was on the cities that died.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeQuestCampaignTest,
	"Cataclysm.Surge.ACampaignActuallyLandsQuestDungeons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeQuestCampaignTest::RunTest(const FString& Parameters)
{
	constexpr int32 Campaigns = 20;
	constexpr int32 MostDays = 600;

	int32 Quests = 0;
	int32 Rolled = 0;
	int32 Days = 0;
	int32 CampaignsWithOne = 0;

	for (int32 Seed = 1; Seed <= Campaigns; ++Seed)
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->Begin(Seed);

		if (!TestNotNull(TEXT("the run has a map"), Run->Map.Get()))
		{
			return false;
		}

		TSet<int32> Counted;
		int32 Here = 0;

		auto RecordWhatIsNew = [&](const UCataclysmEmpireRun& Live)
		{
			for (const FCataclysmDungeon& Dungeon : Live.Dungeons)
			{
				// A FALLEN CITY IS NOT A DUNGEON A SURGE ROLLED, so counting it
				// would dilute the share this test is measuring with dungeons
				// the roll never saw. Issue #1324 slice 2 is what creates them.
				if (Dungeon.Type == ECataclysmDungeonType::FallenCity)
				{
					continue;
				}

				bool bAlready = false;
				Counted.Add(Dungeon.DungeonId, &bAlready);
				if (bAlready)
				{
					continue;
				}

				++Rolled;

				// NOTHING BUT THE TWO KINDS A SURGE ROLLS EVER REACHES THE MAP.
				if (Dungeon.Type != ECataclysmDungeonType::Basic
					&& Dungeon.Type != ECataclysmDungeonType::Quest)
				{
					AddError(FString::Printf(
						TEXT("a surge landed dungeon %d of kind %d, which is "
							 "neither Basic nor Quest"),
						Dungeon.DungeonId,
						static_cast<int32>(Dungeon.Type)));
					continue;
				}

				if (Dungeon.Type == ECataclysmDungeonType::Quest)
				{
					++Quests;
					++Here;

					// AND IT ARRIVED WITH ITS OWN SPEC, not a Basic one's. The
					// wave is what builds it, so this is where a wave passing
					// the wrong kind through to `MakeDungeon` would show.
					const FCataclysmDungeonSpec Spec =
						UCataclysmSurgeScheduler::SpecFor(
							ECataclysmDungeonType::Quest, Dungeon.CityTier);

					TestTrue(FString::Printf(
						TEXT("landed Quest dungeon %d is %d floors, inside %d "
							 "to %d"), Dungeon.DungeonId, Dungeon.Floors,
						Spec.LeastFloors, Spec.MostFloors),
						Dungeon.Floors >= Spec.LeastFloors
						&& Dungeon.Floors <= Spec.MostFloors);
				}
			}
		};

		// THE FIRST WAVE FIRES AT RUN START, so the board is read before any day
		// passes as well as after each one.
		RecordWhatIsNew(*Run);

		int32 Advanced = 0;
		while (Advanced < MostDays && Run->Map->ExposedCities().Num() >= 2)
		{
			Run->AdvanceDay();
			++Advanced;
			RecordWhatIsNew(*Run);
		}

		Days += Advanced;
		CampaignsWithOne += Here > 0 ? 1 : 0;
	}

	AddInfo(FString::Printf(
		TEXT("%d campaigns, %d days in all, %d dungeons rolled: %d were Quest "
			 "(%.1f%%), and %d of %d campaigns saw at least one"),
		Campaigns, Days, Rolled, Quests,
		Rolled > 0 ? 100.0f * Quests / Rolled : 0.0f, CampaignsWithOne,
		Campaigns));

	// THE SAMPLE HAS TO BE BIG ENOUGH FOR THE SHARE TO MEAN ANYTHING. A handful
	// of dungeons would satisfy the band below whatever the roll did.
	if (!TestTrue(FString::Printf(
			TEXT("%d dungeons is enough to measure a 12-in-100 share"), Rolled),
			Rolled >= 500))
	{
		return false;
	}

	// THE ONE THING THIS EXISTS TO PROVE.
	TestTrue(TEXT("a campaign actually lands Quest dungeons"), Quests > 0);

	// AND AT ROUGHLY THE RATE THE CONSTANT SAYS, so that a wave that landed one
	// Quest dungeon by accident would not pass. The band is wide because a
	// campaign's dungeon count is not fixed; the point is the order of
	// magnitude, and the exact share is measured by
	// `Cataclysm.Surge.ARolledKindIsBasicOrQuestAtTheStatedChance` above.
	const float Wanted = UCataclysmSurgeScheduler::QuestChance;
	const float Seen = static_cast<float>(Quests) / Rolled;

	TestTrue(FString::Printf(
		TEXT("%.1f%% of dungeons landed were Quest, against the stated %.1f%%"),
		100.0f * Seen, 100.0f * Wanted),
		Seen > Wanted * 0.5f && Seen < Wanted * 1.5f);

	return true;
}

// ---------------------------------------------------------------------------
// Where a Quest dungeon may move -- issue #1324 slice 4
// ---------------------------------------------------------------------------

/**
 * `PickRelocation` answers an adjacent, exposed, non-Pillar city, or nothing.
 *
 * `docs/Cataclysm_GDD_v2.md` section VIII: a Quest dungeon "does not resolve --
 * refreshes and may move to adjacent city". The project owner ruled on
 * 2026-09-06, verbatim "Adjacent, and fix the simulation", so adjacency is the
 * rule and the model's move-anywhere was the defect.
 *
 * IT ASKS EVERY CITY ON EVERY MAP STATE IT BUILDS, rather than one worked
 * example. The rule is a filter and a filter is only as good as the cases it
 * was asked about; a single Outpost with one open neighbour would be satisfied
 * by a function that answered "the first city in the list".
 *
 * WHAT IT DOES NOT CHECK. That anything calls it. That is
 * `Cataclysm.EmpireRun.AQuestDungeonMovesToAnAdjacentCity`, which drives real
 * campaigns -- and it is a separate test because a rule that is right and
 * unreachable is exactly the state `UCataclysmEmpireMap::Retake` was in before
 * slice 2 found it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeRelocationTargetTest,
	"Cataclysm.Surge.AQuestDungeonRelocatesOnlyToAnAdjacentExposedCity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeRelocationTargetTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	FRandomStream Stream(7);

	int32 Answered = 0;
	int32 Refused = 0;

	// THREE MAP STATES, AND THE SECOND AND THIRD ARE WHY THIS IS A LOOP. An
	// intact map has only its rim exposed, so it can never exercise the inward
	// links; felling cities is what opens them.
	for (int32 Fell = 0; Fell <= 2; ++Fell)
	{
		UCataclysmEmpireMap* Map = MakeMap();

		// FELL FROM THE RIM INWARD, which is the only order the lane rule
		// allows: a Bulwark cannot fall while every Outpost shielding it
		// stands, because nothing can reach it to hurt it.
		for (int32 Ring = 3; Ring > 3 - Fell; --Ring)
		{
			for (FCataclysmCity& City : Map->Cities)
			{
				if (FMath::Abs(City.R) + FMath::Abs(City.C) == Ring)
				{
					City.bFallen = true;
					City.Defence = 0.0f;
				}
			}
		}

		for (const FCataclysmCity& City : Map->Cities)
		{
			const int32 Target =
				UCataclysmSurgeScheduler::PickRelocation(*Map, City, Stream);

			if (Target == INDEX_NONE)
			{
				++Refused;

				// AND IT REFUSED FOR THE RIGHT REASON. Nothing adjacent was
				// available; the assertion is what stops a `PickRelocation`
				// that gave up early from reading as correct.
				for (const int32 Neighbour :
					 UCataclysmSurgeScheduler::AdjacentCities(City))
				{
					TestFalse(FString::Printf(
						TEXT("%s was told it had nowhere to go while its "
							 "neighbour %d was exposed"), *City.Name,
						Neighbour),
						Map->IsExposed(Neighbour)
							&& Neighbour != Map->PillarId);
				}

				continue;
			}

			++Answered;

			TestTrue(FString::Printf(
				TEXT("%s relocates to a city adjacent to it"), *City.Name),
				UCataclysmSurgeScheduler::AdjacentCities(City)
					.Contains(Target));

			TestTrue(FString::Printf(
				TEXT("%s relocates to an exposed city"), *City.Name),
				Map->IsExposed(Target));

			// NEVER THE PILLAR. A dungeon reaching the Pillar is the Last
			// Stand, issue #43, and not a quest dungeon wandering onto it.
			TestNotEqual(FString::Printf(
				TEXT("%s does not relocate onto the Pillar"), *City.Name),
				Target, Map->PillarId);

			// AND NEVER ONTO ITSELF, which would make "moved" and "stayed"
			// indistinguishable to every caller.
			TestNotEqual(FString::Printf(
				TEXT("%s does not relocate onto itself"), *City.Name),
				Target, City.CityId);
		}
	}

	AddInfo(FString::Printf(
		TEXT("%d cities were given a relocation target and %d had none"),
		Answered, Refused));

	// BOTH OUTCOMES HAVE TO HAVE HAPPENED. A run where every city was refused
	// would pass every assertion above without ever testing the answer, and one
	// where none was refused would never test the empty case -- which is the
	// design's "MAY move".
	TestTrue(TEXT("some cities had an adjacent exposed city to move to"),
			 Answered > 0);
	TestTrue(TEXT("and some had none, which is the design's \"may\" move"),
			 Refused > 0);

	return true;
}

/**
 * A Quest dungeon on the rim can drift along the rim, and that is a decision.
 *
 * **THE DESIGN NEVER DEFINES ADJACENCY AND THIS MAP DEFINES IT TWICE.**
 * `UCataclysmEmpireMap`'s class comment describes lanes as the orthogonal
 * steps; `FCataclysmCity::Perimeter` describes the rim's curved edges as
 * existing "for adjacency effects". Relocation counts both -- see
 * `UCataclysmSurgeScheduler::AdjacentCities` and `docs/DECISIONS.md`.
 *
 * WHY IT NEEDS ITS OWN TEST. On an intact map a rim Outpost's only orthogonal
 * neighbour is one ring in, and that city is sealed precisely because the
 * Outpost is still standing. So without the perimeter links a Quest dungeon
 * landing on the rim -- where every dungeon lands at the start of a run --
 * could never move at all, and the test above would still pass: it would simply
 * count more refusals. This is the case that separates the two readings.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeRimRelocationTest,
	"Cataclysm.Surge.AQuestDungeonOnAnIntactRimDriftsAlongIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeRimRelocationTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	UCataclysmEmpireMap* Map = MakeMap();

	FRandomStream Stream(11);

	int32 RimCities = 0;
	int32 Moved = 0;

	for (const FCataclysmCity& City : Map->Cities)
	{
		if (FMath::Abs(City.R) + FMath::Abs(City.C) != 3)
		{
			continue;
		}

		++RimCities;

		// AN INTACT MAP, so nothing but the rim is exposed. Anything this
		// answers is a perimeter link by construction.
		const int32 Target =
			UCataclysmSurgeScheduler::PickRelocation(*Map, City, Stream);

		if (Target == INDEX_NONE)
		{
			continue;
		}

		++Moved;

		TestTrue(FString::Printf(
			TEXT("%s drifts to a rim Outpost it is linked to along the "
				 "perimeter"), *City.Name),
			City.Perimeter.Contains(Target));
	}

	AddInfo(FString::Printf(
		TEXT("%d of %d rim Outposts could move on an intact map"),
		Moved, RimCities));

	TestEqual(TEXT("the map still has twelve rim Outposts"), RimCities, 12);

	// EVERY ONE OF THEM, and the strong form is deliberate. Each rim Outpost
	// has at least one perimeter link, so if any could not move the perimeter
	// is not being read.
	TestEqual(TEXT("every rim Outpost can move along the rim"),
			  Moved, RimCities);

	return true;
}

/**
 * `PickRelocation` refuses a besieged city to a dungeon carrying a Siege, and
 * to nothing else.
 *
 * **THE OWNER'S RULING OF 2026-09-06, VERBATIM: "Check the limit on arrival
 * too".** Issue #1371. The design's Siege row says "Max 1 per city";
 * `RollSubType` enforced it at spawn and nothing enforced it on a move.
 *
 * WHY IT ASKS EVERY CITY. The rule is a filter, and a filter is only as good as
 * the cases it was asked about. Marking one city besieged and checking one
 * neighbour would be satisfied by a function that answered "not that one".
 *
 * **THE THREE READINGS THIS SEPARATES.** Every assertion below fails under one
 * of them and they are the three ways to get it wrong: refusing nothing, which
 * is the state before the ruling; refusing the besieged city to every dungeon,
 * which is a much larger rule than "max 1 Siege per city"; and refusing the move
 * outright rather than the destination, which loses the owner's "pick another
 * adjacent city".
 *
 * WHAT IT DOES NOT CHECK. That anything passes the counts in. That is
 * `Cataclysm.EmpireRun.ARelocatingSiegeRefusesACityThatAlreadyHasOne`, which
 * builds the situation on a real run and advances a day, because a filter that
 * is right and unreachable is exactly the state `UCataclysmEmpireMap::Retake`
 * was in before slice 2 of issue #1324 found it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeSiegeRelocationTest,
	"Cataclysm.Surge.ARelocatingSiegeIsRefusedACityThatAlreadyHasOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeSiegeRelocationTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	UCataclysmEmpireMap* Map = MakeMap();
	FRandomStream Stream(23);

	// EVERY CITY BESIEGED, which is the state that makes the refusal visible
	// everywhere at once rather than on one hand-picked pair.
	TArray<int32> AllBesieged;
	AllBesieged.Init(UCataclysmSurgeScheduler::SiegesPerCity,
					 Map->Cities.Num());

	int32 AskedWithTargets = 0;
	int32 RefusedCarryingASiege = 0;
	int32 AnsweredCarryingNothing = 0;

	for (const FCataclysmCity& City : Map->Cities)
	{
		// WHAT THE ORDINARY RULE OFFERS, WITH NO SIEGE INFORMATION AT ALL.
		// **THE CONTROL ON EVERYTHING BELOW**: a city with no legal target
		// under the ordinary filter would be refused whatever the Siege rule
		// did, and counting it would make the refusal look like the new rule
		// working when it is the old one.
		const int32 Ordinary =
			UCataclysmSurgeScheduler::PickRelocation(*Map, City, Stream);

		if (Ordinary == INDEX_NONE)
		{
			continue;
		}

		++AskedWithTargets;

		// **CARRYING A SIEGE, WITH EVERY CITY ALREADY HOLDING ONE.** There is
		// nowhere legal to go, so it must answer nothing rather than pick the
		// least bad option.
		const int32 Carrying = UCataclysmSurgeScheduler::PickRelocation(
			*Map, City, Stream, /* bCarriesSiege */ true, AllBesieged);

		if (Carrying == INDEX_NONE)
		{
			++RefusedCarryingASiege;
		}
		else
		{
			AddError(FString::Printf(
				TEXT("%s was offered city %d for a dungeon carrying a Siege "
					 "while every city already holds one. The owner ruled on "
					 "2026-09-06, verbatim \"Check the limit on arrival too\"; "
					 "issue #1371"),
				*City.Name, Carrying));
		}

		// **AND CARRYING NOTHING, ON THE SAME BESIEGED MAP.** The cap is "max 1
		// Siege per city" and not "one dungeon per besieged city", so this must
		// still be answered. It is the assertion that fails if the check is
		// written against the destination instead of against the mover.
		const int32 Plain = UCataclysmSurgeScheduler::PickRelocation(
			*Map, City, Stream, /* bCarriesSiege */ false, AllBesieged);

		if (Plain != INDEX_NONE)
		{
			++AnsweredCarryingNothing;
		}
		else
		{
			AddError(FString::Printf(
				TEXT("%s was told it had nowhere to go although the dungeon "
					 "carries no Siege. The cap is per Siege, not per dungeon"),
				*City.Name));
		}
	}

	// **ONE NEIGHBOUR BESIEGED AND THE REST FREE, WHICH IS THE CASE THE RULING
	// IS ACTUALLY ABOUT.** The map-wide sweep above proves the refusal; this
	// proves it redirects rather than cancels, which is the half of the owner's
	// words -- "pick another adjacent city" -- that a blanket refusal satisfies
	// while getting wrong.
	int32 Redirects = 0;

	for (const FCataclysmCity& City : Map->Cities)
	{
		const TArray<int32> Around =
			UCataclysmSurgeScheduler::AdjacentCities(City);

		int32 Barred = INDEX_NONE;
		int32 Legal = 0;

		for (const int32 Neighbour : Around)
		{
			if (Neighbour == Map->PillarId || !Map->IsExposed(Neighbour))
			{
				continue;
			}

			++Legal;

			if (Barred == INDEX_NONE)
			{
				Barred = Neighbour;
			}
		}

		if (Legal < 2)
		{
			// ONE LEGAL NEIGHBOUR CANNOT SHOW A REDIRECT. Barring it leaves
			// nowhere to go, which is the case above rather than this one.
			continue;
		}

		TArray<int32> OneBesieged;
		OneBesieged.AddZeroed(Map->Cities.Num());
		OneBesieged[Barred] = UCataclysmSurgeScheduler::SiegesPerCity;

		for (int32 Draw = 0; Draw < 40; ++Draw)
		{
			const int32 Target = UCataclysmSurgeScheduler::PickRelocation(
				*Map, City, Stream, /* bCarriesSiege */ true, OneBesieged);

			TestNotEqual(FString::Printf(
				TEXT("%s does not send a Siege onto the besieged city %d"),
				*City.Name, Barred),
				Target, Barred);

			if (Target != INDEX_NONE && Target != Barred)
			{
				++Redirects;
			}
		}
	}

	AddInfo(FString::Printf(
		TEXT("%d cities had somewhere to go under the ordinary rule; all of "
			 "them refused a Siege on a fully besieged map (%d) and all of "
			 "them still answered a dungeon carrying nothing (%d). %d draws "
			 "were redirected past a single besieged neighbour"),
		AskedWithTargets, RefusedCarryingASiege, AnsweredCarryingNothing,
		Redirects));

	// THE EVIDENCE HAS TO EXIST BEFORE IT CAN BE BELIEVED. A map on which no
	// city had anywhere to go would pass every assertion above by reaching none
	// of them.
	TestTrue(TEXT("some cities had a relocation target under the ordinary "
				  "rule"),
			 AskedWithTargets > 0);
	TestEqual(TEXT("and every one of them refused a Siege on a besieged map"),
			  RefusedCarryingASiege, AskedWithTargets);
	TestEqual(TEXT("and every one of them still answered a dungeon carrying "
				   "nothing"),
			  AnsweredCarryingNothing, AskedWithTargets);
	TestTrue(TEXT("and a Siege was redirected past a besieged neighbour rather "
				  "than stopped"),
			 Redirects > 0);

	return true;
}

// ---------------------------------------------------------------------------
// Where a Quest dungeon goes when its city falls -- 2026-10-21
// ---------------------------------------------------------------------------
//
// The owner, 2026-10-16, verbatim: "Quest dungeons don't go away when a city
// falls. They just move to an adjacent city. So eventually they should be
// hitting something." `UCataclysmSurgeScheduler::PickRefuge` answers where:
// an adjacent city that qualifies first, else the nearest one counted in
// lanes, one draw among the ties, nothing and no draw when none qualifies.
// These ask it on bare maps; `Cataclysm.EmpireRun.AFallMovesItsQuestDungeon
// AndItsDungeonCityCountsOnlyWhatItAbsorbed` asks whether a fall calls it.

namespace CataclysmRefugeTest
{
	/** Marks a city fallen, as `UCataclysmEmpireMap::Fall` would. */
	void Fell(UCataclysmEmpireMap& Map, int32 CityId)
	{
		if (FCataclysmCity* City = Map.FindMutable(CityId))
		{
			City->bFallen = true;
			City->Defence = 0.0f;
		}
	}

	/** A rim Outpost with exactly two perimeter links, or `INDEX_NONE`. */
	int32 RimOutpostWithTwoLinks(const UCataclysmEmpireMap& Map)
	{
		for (const FCataclysmCity& City : Map.Cities)
		{
			if (City.Ring() == 3 && City.Perimeter.Num() == 2)
			{
				return City.CityId;
			}
		}

		return INDEX_NONE;
	}

	/**
	 * The fewest lanes between two cities over every link, through any city,
	 * or `INDEX_NONE` when one cannot be reached. WRITTEN HERE, a plain
	 * breadth-first search, rather than read from the function under test.
	 */
	int32 LanesBetween(const UCataclysmEmpireMap& Map, int32 From, int32 To)
	{
		TArray<int32> Distance;
		Distance.Init(INDEX_NONE, Map.Cities.Num());
		if (!Distance.IsValidIndex(From) || !Distance.IsValidIndex(To))
		{
			return INDEX_NONE;
		}

		Distance[From] = 0;
		TArray<int32> Queue;
		Queue.Add(From);

		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			const FCataclysmCity* City = Map.Find(Queue[Head]);
			if (City == nullptr)
			{
				continue;
			}

			for (const int32 Next : UCataclysmSurgeScheduler::AdjacentCities(*City))
			{
				if (Distance.IsValidIndex(Next) && Distance[Next] == INDEX_NONE)
				{
					Distance[Next] = Distance[Queue[Head]] + 1;
					Queue.Add(Next);
				}
			}
		}

		return Distance[To];
	}

	/** Whether a city could take a Quest dungeon off a fallen one, by the
	 *  rule as this test states it: exposed, not the Pillar, and not already
	 *  holding a Siege when the dungeon carries one. */
	bool CouldTakeIt(const UCataclysmEmpireMap& Map, int32 CityId,
					 bool bCarriesSiege, const TArray<int32>& Sieges)
	{
		return CityId != Map.PillarId && Map.IsExposed(CityId)
			&& !(bCarriesSiege && Sieges.IsValidIndex(CityId)
				 && Sieges[CityId] >= UCataclysmSurgeScheduler::SiegesPerCity);
	}

	/** Every city that could take it, with its lanes from `From`. */
	TMap<int32, int32> EveryCityThatCouldTakeIt(
		const UCataclysmEmpireMap& Map, int32 From, bool bCarriesSiege,
		const TArray<int32>& Sieges)
	{
		TMap<int32, int32> Found;
		for (const FCataclysmCity& City : Map.Cities)
		{
			if (CouldTakeIt(Map, City.CityId, bCarriesSiege, Sieges))
			{
				Found.Add(City.CityId, LanesBetween(Map, From, City.CityId));
			}
		}
		return Found;
	}
}

/**
 * A Quest dungeon on a city that has just fallen goes to a neighbour when one
 * will take it, and the choice among them is one draw.
 *
 * THE CONTROL is a city that could take it and is not adjacent: it exists on
 * this map and is never answered, so "adjacent first" is not satisfied by a
 * map on which only neighbours qualify.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeRefugeNeighbourTest,
	"Cataclysm.Surge.AFallenCitysQuestDungeonTakesRefugeOnANeighbourFirst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeRefugeNeighbourTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;
	using namespace CataclysmRefugeTest;

	UCataclysmEmpireMap* Map = MakeMap();
	const int32 FallenId = RimOutpostWithTwoLinks(*Map);
	if (!TestTrue(TEXT("set-up: the map has a rim Outpost with two perimeter links"),
				  FallenId != INDEX_NONE))
	{
		return false;
	}

	Fell(*Map, FallenId);
	const FCataclysmCity& Fallen = *Map->Find(FallenId);
	const TArray<int32> NoSieges;

	// THE NEIGHBOURS THAT COULD TAKE IT, in the order `AdjacentCities` names
	// them, which is the order the one draw picks from.
	TArray<int32> Neighbours;
	for (const int32 Neighbour : UCataclysmSurgeScheduler::AdjacentCities(Fallen))
	{
		if (CouldTakeIt(*Map, Neighbour, false, NoSieges))
		{
			Neighbours.Add(Neighbour);
		}
	}

	int32 FartherOption = INDEX_NONE;
	for (const TPair<int32, int32>& Option :
		 EveryCityThatCouldTakeIt(*Map, FallenId, false, NoSieges))
	{
		if (Option.Value > 1)
		{
			FartherOption = Option.Key;
		}
	}

	if (!TestTrue(TEXT("set-up: at least two neighbours could take it, so the draw is a choice"),
				  Neighbours.Num() >= 2)
		|| !TestTrue(TEXT("set-up: CONTROL: a city that is not adjacent could take it too"),
					 FartherOption != INDEX_NONE))
	{
		return false;
	}

	FRandomStream Stream(31);
	TSet<int32> Answered;

	for (int32 Draw = 0; Draw < 200; ++Draw)
	{
		const int32 Refuge = UCataclysmSurgeScheduler::PickRefuge(*Map, Fallen, Stream);

		if (!Neighbours.Contains(Refuge))
		{
			AddError(FString::Printf(
				TEXT("draw %d sent the Quest dungeon to city %d, which is not a neighbour that could take it"),
				Draw, Refuge));
			continue;
		}

		Answered.Add(Refuge);
	}

	TestEqual(TEXT("every neighbour that could take it was answered"),
			  Answered.Num(), Neighbours.Num());
	TestFalse(TEXT("CONTROL: the city that is not adjacent was never answered"),
			  Answered.Contains(FartherOption));

	// ONE DRAW, AND IT PICKS FROM THE NEIGHBOURS IN `AdjacentCities` ORDER.
	// Two streams from one seed: one asked for the refuge, the other drawn
	// once by hand over the same count. They end on the same seed, and the
	// hand draw names the same city.
	FRandomStream Asked(5);
	FRandomStream ByHand(5);
	const int32 Refuge = UCataclysmSurgeScheduler::PickRefuge(*Map, Fallen, Asked);
	const int32 Index = ByHand.RandRange(0, Neighbours.Num() - 1);

	TestEqual(TEXT("asking took exactly one draw"),
			  Asked.GetCurrentSeed(), ByHand.GetCurrentSeed());
	TestEqual(TEXT("and that draw picked from the neighbours in their adjacency order"),
			  Refuge, Neighbours[Index]);

	return true;
}

/**
 * With no neighbour left to take it, a Quest dungeon goes to the nearest city
 * that will, counted in lanes.
 *
 * THE CONTROL is the timer's rule on the same map: `PickRelocation` answers
 * nothing, so the answer below comes from the outward search and from nothing
 * else. And a city farther out could take it too, so "nearest" is a choice.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeRefugeOutwardTest,
	"Cataclysm.Surge.AFallenCitysQuestDungeonWithNoNeighbourLeftGoesToTheNearestCityThatQualifies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeRefugeOutwardTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;
	using namespace CataclysmRefugeTest;

	UCataclysmEmpireMap* Map = MakeMap();
	const int32 FallenId = RimOutpostWithTwoLinks(*Map);
	if (!TestTrue(TEXT("set-up: the map has a rim Outpost with two perimeter links"),
				  FallenId != INDEX_NONE))
	{
		return false;
	}

	// THE CITY AND EVERY NEIGHBOUR OF IT FALL.
	Fell(*Map, FallenId);
	for (const int32 Neighbour :
		 UCataclysmSurgeScheduler::AdjacentCities(*Map->Find(FallenId)))
	{
		Fell(*Map, Neighbour);
	}

	const FCataclysmCity& Fallen = *Map->Find(FallenId);
	const TArray<int32> NoSieges;

	const TMap<int32, int32> Options =
		EveryCityThatCouldTakeIt(*Map, FallenId, false, NoSieges);

	int32 Nearest = INDEX_NONE;
	int32 Farthest = INDEX_NONE;
	for (const TPair<int32, int32>& Option : Options)
	{
		Nearest = Nearest == INDEX_NONE ? Option.Value : FMath::Min(Nearest, Option.Value);
		Farthest = FMath::Max(Farthest, Option.Value);
	}

	TArray<int32> NearestRing;
	for (const TPair<int32, int32>& Option : Options)
	{
		if (Option.Value == Nearest)
		{
			NearestRing.Add(Option.Key);
		}
	}

	FRandomStream Stream(43);

	if (!TestTrue(TEXT("set-up: the nearest city that could take it is two lanes away or more"),
				  Nearest >= 2)
		|| !TestTrue(TEXT("set-up: and one farther out could take it too"),
					 Farthest > Nearest)
		|| !TestEqual(TEXT("CONTROL: the timer's rule finds nowhere adjacent to go"),
					  UCataclysmSurgeScheduler::PickRelocation(*Map, Fallen, Stream),
					  static_cast<int32>(INDEX_NONE)))
	{
		return false;
	}

	TSet<int32> Answered;

	for (int32 Draw = 0; Draw < 200; ++Draw)
	{
		const int32 Refuge = UCataclysmSurgeScheduler::PickRefuge(*Map, Fallen, Stream);

		if (!NearestRing.Contains(Refuge))
		{
			AddError(FString::Printf(
				TEXT("draw %d sent the Quest dungeon to city %d, %d lanes away, where the nearest city that could take it is %d lanes away"),
				Draw, Refuge, LanesBetween(*Map, FallenId, Refuge), Nearest));
			continue;
		}

		Answered.Add(Refuge);
	}

	TestEqual(FString::Printf(
		TEXT("every city %d lanes away that could take it was answered"), Nearest),
		Answered.Num(), NearestRing.Num());

	// AND STILL ONE DRAW.
	FRandomStream Asked(9);
	FRandomStream ByHand(9);
	UCataclysmSurgeScheduler::PickRefuge(*Map, Fallen, Asked);
	ByHand.RandRange(0, NearestRing.Num() - 1);

	TestEqual(TEXT("the outward search took exactly one draw"),
			  Asked.GetCurrentSeed(), ByHand.GetCurrentSeed());

	return true;
}

/**
 * A Quest dungeon carrying a Siege is not sent to a city that already holds
 * one, and goes on past it to the nearest that does not.
 *
 * THE CONTROL is the same map and the same counts with a dungeon carrying
 * nothing: it goes to a neighbour, besieged or not.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeRefugeSiegeTest,
	"Cataclysm.Surge.AFallenCitysQuestDungeonCarryingASiegeIsRefusedABesiegedCity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeRefugeSiegeTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;
	using namespace CataclysmRefugeTest;

	UCataclysmEmpireMap* Map = MakeMap();
	const int32 FallenId = RimOutpostWithTwoLinks(*Map);
	if (!TestTrue(TEXT("set-up: the map has a rim Outpost with two perimeter links"),
				  FallenId != INDEX_NONE))
	{
		return false;
	}

	Fell(*Map, FallenId);
	const FCataclysmCity& Fallen = *Map->Find(FallenId);
	const TArray<int32> Around = UCataclysmSurgeScheduler::AdjacentCities(Fallen);

	// EVERY NEIGHBOUR ALREADY HOLDS A SIEGE.
	TArray<int32> Sieges;
	Sieges.AddZeroed(Map->Cities.Num());
	for (const int32 Neighbour : Around)
	{
		Sieges[Neighbour] = UCataclysmSurgeScheduler::SiegesPerCity;
	}

	int32 Nearest = INDEX_NONE;
	for (const TPair<int32, int32>& Option :
		 EveryCityThatCouldTakeIt(*Map, FallenId, true, Sieges))
	{
		Nearest = Nearest == INDEX_NONE ? Option.Value : FMath::Min(Nearest, Option.Value);
	}

	if (!TestTrue(TEXT("set-up: a city with no Siege could take it, two lanes away or more"),
				  Nearest >= 2))
	{
		return false;
	}

	FRandomStream Stream(53);

	for (int32 Draw = 0; Draw < 100; ++Draw)
	{
		const int32 Refuge = UCataclysmSurgeScheduler::PickRefuge(
			*Map, Fallen, Stream, /* bCarriesSiege */ true, Sieges);

		TestFalse(FString::Printf(
			TEXT("draw %d did not send a Siege to a neighbour already holding one"), Draw),
			Around.Contains(Refuge));
		TestTrue(FString::Printf(
			TEXT("draw %d sent it to a city that could take it"), Draw),
			CouldTakeIt(*Map, Refuge, true, Sieges));
		TestEqual(FString::Printf(
			TEXT("draw %d sent it to the nearest such city"), Draw),
			LanesBetween(*Map, FallenId, Refuge), Nearest);
	}

	// THE CONTROL: CARRYING NOTHING, THE BESIEGED NEIGHBOURS TAKE IT.
	for (int32 Draw = 0; Draw < 50; ++Draw)
	{
		const int32 Refuge = UCataclysmSurgeScheduler::PickRefuge(
			*Map, Fallen, Stream, /* bCarriesSiege */ false, Sieges);

		TestTrue(FString::Printf(
			TEXT("CONTROL: draw %d sent a dungeon carrying no Siege to a neighbour"), Draw),
			Around.Contains(Refuge));
	}

	return true;
}

/**
 * When no city but the Pillar is left exposed, a Quest dungeon on a city that
 * falls stays where it is, and no draw is taken.
 *
 * THE CONTROL is the same map with one rim Outpost standing again: then it
 * goes there, and exactly one draw is taken, as `rng.choice` of one city
 * takes one.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeRefugeNowhereTest,
	"Cataclysm.Surge.AFallenCitysQuestDungeonWithNowhereToGoStaysAndTakesNoDraw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeRefugeNowhereTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;
	using namespace CataclysmRefugeTest;

	UCataclysmEmpireMap* Map = MakeMap();
	const int32 FallenId = RimOutpostWithTwoLinks(*Map);
	if (!TestTrue(TEXT("set-up: the map has a rim Outpost with two perimeter links"),
				  FallenId != INDEX_NONE))
	{
		return false;
	}

	// EVERY CITY BUT THE PILLAR FALLS. The Pillar is then exposed, and still
	// refused.
	int32 OtherRimOutpost = INDEX_NONE;
	for (const FCataclysmCity& City : Map->Cities)
	{
		if (City.CityId != Map->PillarId)
		{
			Fell(*Map, City.CityId);
		}
		if (City.Ring() == 3 && City.CityId != FallenId
			&& !UCataclysmSurgeScheduler::AdjacentCities(*Map->Find(FallenId)).Contains(City.CityId))
		{
			OtherRimOutpost = City.CityId;
		}
	}

	const FCataclysmCity& Fallen = *Map->Find(FallenId);

	if (!TestTrue(TEXT("set-up: the Pillar is exposed"), Map->IsExposed(Map->PillarId))
		|| !TestTrue(TEXT("set-up: a rim Outpost not adjacent to the fallen one was found"),
					 OtherRimOutpost != INDEX_NONE))
	{
		return false;
	}

	FRandomStream Stream(17);
	const int32 SeedBefore = Stream.GetCurrentSeed();

	TestEqual(TEXT("nowhere takes it, so it stays"),
			  UCataclysmSurgeScheduler::PickRefuge(*Map, Fallen, Stream),
			  static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("and no draw was taken"), Stream.GetCurrentSeed(), SeedBefore);

	// THE CONTROL: ONE RIM OUTPOST STANDS AGAIN, FAR FROM THE FALLEN ONE.
	if (FCataclysmCity* Standing = Map->FindMutable(OtherRimOutpost))
	{
		Standing->bFallen = false;
		Standing->Defence = Standing->MaxDefence;
	}

	FRandomStream Asked(17);
	FRandomStream ByHand(17);
	TestEqual(TEXT("CONTROL: with one rim Outpost standing again, it goes there"),
			  UCataclysmSurgeScheduler::PickRefuge(*Map, Fallen, Asked), OtherRimOutpost);
	ByHand.RandRange(0, 0);
	TestEqual(TEXT("CONTROL: and exactly one draw was taken for a choice of one"),
			  Asked.GetCurrentSeed(), ByHand.GetCurrentSeed());

	// AND A SIEGE IS STILL REFUSED THE ONE CITY LEFT WHEN IT HOLDS ONE.
	TArray<int32> Sieges;
	Sieges.AddZeroed(Map->Cities.Num());
	Sieges[OtherRimOutpost] = UCataclysmSurgeScheduler::SiegesPerCity;

	FRandomStream Refused(17);
	TestEqual(TEXT("a dungeon carrying a Siege has nowhere to go when the one city left holds a Siege"),
			  UCataclysmSurgeScheduler::PickRefuge(*Map, Fallen, Refused, true, Sieges),
			  static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("and no draw was taken for it either"),
			  Refused.GetCurrentSeed(), SeedBefore);

	return true;
}

// ---------------------------------------------------------------------------
// Investment does not change the size of a dungeon's hit
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeFloorUpgradeHitTest,
	"Cataclysm.Surge.AFloorUpgradeDoesNotChangeWhatADungeonTakes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeFloorUpgradeHitTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	// THE RULE. A dungeon's hit is sized by the floors it was rolled with. The
	// two city upgrades that add and remove floors move the depth, the walk
	// and the resolve timer, and neither changes what the dungeon takes.
	// Rulings of 2026-10-13, `docs/DECISIONS.md`.
	//
	// HEADLESS: a scheduler and a map, no world and no actor.
	UCataclysmSurgeScheduler* Scheduler =
		MakeScheduler(ECataclysmSurgeMode::Static);
	UCataclysmEmpireMap* Map = MakeMap();

	const TArray<int32> Rim =
		CitiesAtTier(*Map, ECataclysmCityTier::Outpost, 5);

	if (!TestEqual(TEXT("set-up: five Outposts were found to compare"),
				   Rim.Num(), 5))
	{
		return false;
	}

	// THE FOUR COMBINATIONS, AND A FIFTH THAT THE CLAMP AT ONE FLOOR STOPS
	// SHORT: none, deeper by five, shallower by five, both (five more and two
	// fewer), and five hundred fewer.
	Give(*Map, Rim[1], ECataclysmCityUpgradeEffect::DungeonFloorsMore, 5.0f);
	Give(*Map, Rim[2], ECataclysmCityUpgradeEffect::DungeonFloorsFewer, 5.0f);
	Give(*Map, Rim[3], ECataclysmCityUpgradeEffect::DungeonFloorsMore, 5.0f);
	Give(*Map, Rim[3], ECataclysmCityUpgradeEffect::DungeonFloorsFewer, 2.0f);
	Give(*Map, Rim[4], ECataclysmCityUpgradeEffect::DungeonFloorsFewer, 500.0f);

	// FIVE STREAMS AT ONE SEED, so every dungeon is rolled from the same draws
	// and the only difference between them is the upgrade.
	FRandomStream A(9001), B(9001), C(9001), D(9001), E(9001);

	const FCataclysmDungeon Plain =
		Scheduler->MakeDungeon(0, *Map->Find(Rim[0]), 1, A);
	const FCataclysmDungeon Deeper =
		Scheduler->MakeDungeon(1, *Map->Find(Rim[1]), 1, B);
	const FCataclysmDungeon Shallower =
		Scheduler->MakeDungeon(2, *Map->Find(Rim[2]), 1, C);
	const FCataclysmDungeon Both =
		Scheduler->MakeDungeon(3, *Map->Find(Rim[3]), 1, D);
	const FCataclysmDungeon Floored =
		Scheduler->MakeDungeon(4, *Map->Find(Rim[4]), 1, E);

	// SET-UP. Near one floor, taking five off would be clamped and the
	// "shallower" case would be the clamped case twice.
	if (!TestTrue(TEXT("set-up: the plain dungeon is deep enough to shorten"),
				  Plain.Floors > 6))
	{
		return false;
	}

	// SET-UP: THE PLAIN DUNGEON HAS A HIT TO COMPARE WITH. Its scale is its
	// rolled floors over the midpoint of its own row, read from the table. A
	// scale of nought on all five would pass every equality below.
	const FCataclysmDungeonSpec Row =
		UCataclysmSurgeScheduler::SpecFor(Plain.Type, Plain.CityTier);
	const float Midpoint = (Row.LeastFloors + Row.MostFloors) / 2.0f;

	if (!TestTrue(TEXT("set-up: the plain dungeon's row has a floor range and "
					   "takes defence"),
				  Midpoint > 0.0f && Plain.DefenceDamage > 0.0f))
	{
		return false;
	}

	TestEqual(TEXT("the plain dungeon's scale is its floors over its row's "
				   "midpoint"),
			  Plain.BiteScale(), Plain.Floors / Midpoint, 0.0001f);
	TestEqual(TEXT("and no upgrade moved its floors"), Plain.UpgradeFloors, 0);

	// THE CONTROL: THE UPGRADES DID MOVE THE DEPTH, THE WALK AND THE TIMER. An
	// upgrade that did nothing at all would leave the hit alone too.
	TestEqual(TEXT("CONTROL: the deeper dungeon has five more floors"),
			  Deeper.Floors, Plain.Floors + 5);
	TestEqual(TEXT("CONTROL: the shallower one has five fewer"),
			  Shallower.Floors, Plain.Floors - 5);
	TestEqual(TEXT("CONTROL: the one holding both has three more"),
			  Both.Floors, Plain.Floors + 3);
	TestEqual(TEXT("CONTROL: five hundred fewer leaves one floor"),
			  Floored.Floors, 1);

	TestTrue(TEXT("CONTROL: the deeper dungeon takes longer to walk"),
			 Deeper.WalkDays > Plain.WalkDays);
	TestTrue(TEXT("CONTROL: the shallower one takes less"),
			 Shallower.WalkDays < Plain.WalkDays);
	TestTrue(TEXT("CONTROL: the deeper dungeon's timer is longer"),
			 Deeper.ResolveDays > Plain.ResolveDays);
	TestTrue(TEXT("CONTROL: the shallower one's is shorter"),
			 Shallower.ResolveDays < Plain.ResolveDays);

	// WHAT EACH DUNGEON STORES: the floors the upgrades moved, after the clamp.
	TestEqual(TEXT("the deeper dungeon stores five floors added"),
			  Deeper.UpgradeFloors, 5);
	TestEqual(TEXT("the shallower one stores five removed"),
			  Shallower.UpgradeFloors, -5);
	TestEqual(TEXT("the one holding both stores three added"),
			  Both.UpgradeFloors, 3);
	TestEqual(TEXT("the clamped one stores what was removed and not five "
				   "hundred"),
			  Floored.UpgradeFloors, 1 - Plain.Floors);

	// AND THE HIT IS THE PLAIN DUNGEON'S, FOR EVERY ONE OF THEM.
	TestEqual(TEXT("the deeper dungeon hits at the plain one's scale"),
			  Deeper.BiteScale(), Plain.BiteScale(), 0.0001f);
	TestEqual(TEXT("the shallower one hits at the plain one's scale"),
			  Shallower.BiteScale(), Plain.BiteScale(), 0.0001f);
	TestEqual(TEXT("the one holding both hits at the plain one's scale"),
			  Both.BiteScale(), Plain.BiteScale(), 0.0001f);
	TestEqual(TEXT("the one-floor dungeon hits at the plain one's scale"),
			  Floored.BiteScale(), Plain.BiteScale(), 0.0001f);

	return true;
}

// ---------------------------------------------------------------------------
// Each Cataclysm's Quest dungeons are one sub-type -- 2026-10-21
// ---------------------------------------------------------------------------

/**
 * `QuestSubTypeFor` names five pairs and answers `None` for the rest.
 *
 * THE OWNER, 2026-10-16, verbatim: "Each cataclysm's quest dungeons will be one
 * specific type." The model's `config.QUEST_SUBTYPE_BY_CATACLYSM` is the
 * original; `tools/tests/test_dungeon_subtype_port.py` compares the five pairs
 * with it by name. This is the function answering them, written out by hand so
 * that a pair changed in the switch is caught here as well as there.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeQuestSubTypeForTest,
	"Cataclysm.Surge.FiveCataclysmsNameTheirQuestSubTypeAndTheRestRoll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeQuestSubTypeForTest::RunTest(const FString& Parameters)
{
	// A LOCAL PAIRING RATHER THAN `TPair`, so the table below is plain
	// aggregate initialisation.
	struct FPairing
	{
		ECataclysmType Cataclysm;
		ECataclysmDungeonSubType SubType;
	};

	const FPairing Fixed[] = {
		{ ECataclysmType::Death,		ECataclysmDungeonSubType::Horde },
		{ ECataclysmType::Demonic,		ECataclysmDungeonSubType::Elite },
		{ ECataclysmType::Pestilence,	ECataclysmDungeonSubType::Timed },
		{ ECataclysmType::Famine,		ECataclysmDungeonSubType::Sacrificial },
		{ ECataclysmType::Chaos,		ECataclysmDungeonSubType::Volatile },
	};

	TSet<ECataclysmDungeonSubType> Answered;

	for (const FPairing& Pair : Fixed)
	{
		const ECataclysmDungeonSubType Got =
			UCataclysmSurgeScheduler::QuestSubTypeFor(Pair.Cataclysm);

		TestEqual(FString::Printf(
			TEXT("%s's Quest dungeons are %s"),
			*UCataclysmRoster::NameFor(Pair.Cataclysm).ToString(),
			*UCataclysmSurgeScheduler::SubTypeName(Pair.SubType)),
			static_cast<int32>(Got), static_cast<int32>(Pair.SubType));

		Answered.Add(Got);
	}

	// WAR, CELESTIAL AND THE VOID ROLL. War is ruled out of the game by the
	// owner, and Ascendant and Hollow are not sub-types the game has yet. `None`
	// is the Cataclysm nobody named.
	for (const ECataclysmType Rolls : { ECataclysmType::War,
										ECataclysmType::Celestial,
										ECataclysmType::Void,
										ECataclysmType::None })
	{
		TestEqual(FString::Printf(
			TEXT("Cataclysm %d answers None, so its Quest dungeons roll"),
			static_cast<int32>(Rolls)),
			static_cast<int32>(UCataclysmSurgeScheduler::QuestSubTypeFor(Rolls)),
			static_cast<int32>(ECataclysmDungeonSubType::None));
	}

	// THE CONTROL. Five different answers, none of them `None`: a function that
	// answered one sub-type for every Cataclysm, or nothing for any, would fail
	// one of these where it could pass a check of a single pair.
	TestEqual(TEXT("the five name five different sub-types"), Answered.Num(), 5);
	TestFalse(TEXT("and none of the five answers None"),
			  Answered.Contains(ECataclysmDungeonSubType::None));

	return true;
}

/**
 * A fixed Quest sub-type moves nothing else a wave rolls.
 *
 * **THE STREAM CHECK, AND HOW IT TURNS THE RULE OFF.** There is no flag. The
 * same seed is rolled twice against the same map: once with no Cataclysm
 * callback, so every dungeon carries `None` and nothing is fixed -- what a wave
 * was before this layer -- and once with a callback that names a Cataclysm for
 * each dungeon, drawn from a stream of its own over all eight, so five in eight
 * Quest dungeons take a fixed sub-type. Everything drawn from the wave's stream
 * must come out the same: the targets, the kinds, the depths and the timers.
 *
 * WHAT WOULD BREAK IT. `MakeDungeon` keeps the sub-type draw and discards its
 * answer for a fixed Quest dungeon. Skipping the draw instead would leave every
 * later dungeon in the wave one number behind, at a different depth on a
 * different city. `docs/DECISIONS.md`, 2026-10-21, registers that break.
 *
 * AND THE CATACLYSM HALF. The callback records what it answered; each dungeon
 * must carry the answer given for it, one per dungeon and in order, which is
 * what keeps the run's Cataclysm stream on the draws it took when the run
 * stamped the wave afterwards.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeQuestSubTypeStreamTest,
	"Cataclysm.Surge.AFixedQuestSubTypeMovesNothingElseAWaveRolls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeQuestSubTypeStreamTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	UCataclysmSurgeScheduler* Scheduler =
		MakeScheduler(ECataclysmSurgeMode::Static);
	UCataclysmEmpireMap* Map = MakeMap();

	const TArray<ECataclysmType>& All = UCataclysmRoster::All();

	int32 Compared = 0;
	int32 FixedQuests = 0;
	int32 Overridden = 0;
	int32 SiegesFreed = 0;

	for (int32 Seed = 1; Seed <= 60; ++Seed)
	{
		FRandomStream Plain(Seed);
		FRandomStream Fixing(Seed);
		FRandomStream Senders(Seed * 7919 + 13);

		TArray<ECataclysmType> Asked;
		auto Draw = [&Senders, &All, &Asked]()
		{
			const ECataclysmType Sender =
				All[Senders.RandRange(0, All.Num() - 1)];
			Asked.Add(Sender);
			return Sender;
		};

		const TArray<FCataclysmDungeon> Without =
			Scheduler->RollWave(*Map, 1, 0, Plain);
		const TArray<FCataclysmDungeon> With =
			Scheduler->RollWave(*Map, 1, 0, Fixing, TArray<int32>(),
								TArray<int32>(), Draw);

		if (!TestEqual(FString::Printf(
				TEXT("seed %d: both waves land as many dungeons"), Seed),
				With.Num(), Without.Num()))
		{
			return false;
		}

		TestEqual(FString::Printf(
			TEXT("seed %d: the callback was asked once per dungeon made"), Seed),
			Asked.Num(), With.Num());

		for (int32 Index = 0; Index < With.Num(); ++Index)
		{
			const FCataclysmDungeon& A = Without[Index];
			const FCataclysmDungeon& B = With[Index];

			TestEqual(TEXT("on the same city"), B.CityId, A.CityId);
			TestEqual(TEXT("of the same kind"),
					  static_cast<int32>(B.Type), static_cast<int32>(A.Type));
			TestEqual(TEXT("at the same depth"), B.Floors, A.Floors);
			TestEqual(TEXT("with the same timer"), B.ResolveDays, A.ResolveDays);

			TestEqual(TEXT("a wave with no callback stamps nobody"),
					  static_cast<int32>(A.Cataclysm),
					  static_cast<int32>(ECataclysmType::None));
			if (Asked.IsValidIndex(Index))
			{
				TestEqual(TEXT("and with one, each dungeon carries its own answer"),
						  static_cast<int32>(B.Cataclysm),
						  static_cast<int32>(Asked[Index]));
			}

			const ECataclysmDungeonSubType Wanted =
				UCataclysmSurgeScheduler::QuestSubTypeFor(B.Cataclysm);

			if (B.Type == ECataclysmDungeonType::Quest
				&& Wanted != ECataclysmDungeonSubType::None)
			{
				++FixedQuests;
				TestEqual(TEXT("a fixed Quest dungeon takes its sender's sub-type"),
						  static_cast<int32>(B.SubType),
						  static_cast<int32>(Wanted));
				if (A.SubType != B.SubType)
				{
					++Overridden;
				}
			}
			else if (B.SubType == ECataclysmDungeonSubType::Siege
					 && A.SubType != ECataclysmDungeonSubType::Siege)
			{
				// THE ONE DIFFERENCE THAT IS ALLOWED, AND IT IS NOT A DRAW. A
				// fixed Quest dungeon that rolled a Siege is no longer one, so a
				// later dungeon on the same city may now keep the Siege the
				// one-per-city cap refused it in the plain wave -- the same
				// draw, read without the re-read. Never the other way round.
				++SiegesFreed;
			}
			else
			{
				TestEqual(TEXT("anything else keeps the sub-type it rolled"),
						  static_cast<int32>(B.SubType),
						  static_cast<int32>(A.SubType));
			}

			++Compared;
		}
	}

	AddInfo(FString::Printf(
		TEXT("%d dungeons compared, %d fixed Quest dungeons, %d of them a "
			 "different sub-type from the one rolled; %d Sieges a fixed one "
			 "left room for"),
		Compared, FixedQuests, Overridden, SiegesFreed));

	// THE CONTROL. Unless some fixed Quest dungeon really came out different
	// from what it rolled, the two waves never differed and the comparison
	// above shows nothing about the draw being kept.
	TestTrue(TEXT("some fixed Quest dungeons were made"), FixedQuests > 0);
	TestTrue(TEXT("and some of them differ from what was rolled"),
			 Overridden > 0);

	return true;
}

/**
 * In a run, five Cataclysms' Quest dungeons are their one sub-type, and the
 * rest still roll.
 *
 * THE PATH THE DAY LOOP TAKES. A run at difficulty tier 8 faces all eight
 * Cataclysms, and `FireSurge` hands `RollWave` the run's own Cataclysm draw.
 * Every Quest dungeon a Death, Demonic, Pestilence, Famine or Chaos surge lands
 * must be its one sub-type; the expected sub-types are written out here rather
 * than read from `QuestSubTypeFor`, so a pair changed there fails this too.
 *
 * TWO CONTROLS, BOTH SEARCHED FOR AND ASSERTED. War's, Celestial's and the
 * Void's Quest dungeons still vary -- each shows two sub-types at least -- and
 * the five's ORDINARY dungeons still vary, so the rule is about Quest dungeons
 * and not about the sender. Campaigns are run until all of that has been seen,
 * up to a limit: Quest dungeons are 12 in 100, and one Cataclysm in eight
 * sends each, so a single campaign shows too few.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeQuestSubTypeRunTest,
	"Cataclysm.Surge.InARunFiveCataclysmsQuestDungeonsAreTheirOneSubType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeQuestSubTypeRunTest::RunTest(const FString& Parameters)
{
	constexpr int32 MostCampaigns = 80;
	constexpr int32 MostDays = 600;
	constexpr int32 EnoughOfEach = 3;

	TMap<ECataclysmType, ECataclysmDungeonSubType> Expected;
	Expected.Add(ECataclysmType::Death, ECataclysmDungeonSubType::Horde);
	Expected.Add(ECataclysmType::Demonic, ECataclysmDungeonSubType::Elite);
	Expected.Add(ECataclysmType::Pestilence, ECataclysmDungeonSubType::Timed);
	Expected.Add(ECataclysmType::Famine, ECataclysmDungeonSubType::Sacrificial);
	Expected.Add(ECataclysmType::Chaos, ECataclysmDungeonSubType::Volatile);

	const ECataclysmType Rolling[] = { ECataclysmType::War,
									   ECataclysmType::Celestial,
									   ECataclysmType::Void };

	TMap<ECataclysmType, int32> FixedSeen;
	TMap<ECataclysmType, TSet<ECataclysmDungeonSubType>> RolledSeen;
	TSet<ECataclysmDungeonSubType> OrdinaryFromTheFive;
	int32 Wrong = 0;
	int32 Campaigns = 0;

	auto Enough = [&]()
	{
		for (const TPair<ECataclysmType, ECataclysmDungeonSubType>& Pair : Expected)
		{
			if (FixedSeen.FindRef(Pair.Key) < EnoughOfEach)
			{
				return false;
			}
		}
		for (const ECataclysmType Cataclysm : Rolling)
		{
			const TSet<ECataclysmDungeonSubType>* Seen = RolledSeen.Find(Cataclysm);
			if (Seen == nullptr || Seen->Num() < 2)
			{
				return false;
			}
		}
		return OrdinaryFromTheFive.Num() >= 2;
	};

	for (int32 Seed = 1; Seed <= MostCampaigns && !Enough(); ++Seed)
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->Begin(Seed, ECataclysmSurgeMode::Static, /* LethalityRung */ 0,
				   /* InDifficultyTier */ 8);
		++Campaigns;

		if (!TestEqual(TEXT("a tier 8 run faces all eight"),
					   Run->ActiveCataclysms.Num(), 8))
		{
			return false;
		}

		TSet<int32> Counted;

		int32 Advanced = 0;
		while (Advanced < MostDays && Run->Map->ExposedCities().Num() >= 2)
		{
			Run->AdvanceDay();
			++Advanced;

			for (const FCataclysmDungeon& Dungeon : Run->Dungeons)
			{
				bool bAlready = false;
				Counted.Add(Dungeon.DungeonId, &bAlready);
				if (bAlready || Dungeon.Type == ECataclysmDungeonType::FallenCity)
				{
					continue;
				}

				const ECataclysmDungeonSubType* Fixed =
					Expected.Find(Dungeon.Cataclysm);

				if (Dungeon.Type == ECataclysmDungeonType::Quest)
				{
					if (Fixed != nullptr)
					{
						++FixedSeen.FindOrAdd(Dungeon.Cataclysm);
						if (Dungeon.SubType != *Fixed)
						{
							++Wrong;
							AddError(FString::Printf(
								TEXT("seed %d: dungeon %d, a %s Quest dungeon, "
									 "is %s and not %s"),
								Seed, Dungeon.DungeonId,
								*UCataclysmRoster::NameFor(Dungeon.Cataclysm).ToString(),
								*UCataclysmSurgeScheduler::SubTypeName(Dungeon.SubType),
								*UCataclysmSurgeScheduler::SubTypeName(*Fixed)));
						}
					}
					else
					{
						RolledSeen.FindOrAdd(Dungeon.Cataclysm).Add(Dungeon.SubType);
					}
				}
				else if (Fixed != nullptr)
				{
					OrdinaryFromTheFive.Add(Dungeon.SubType);
				}
			}
		}
	}

	AddInfo(FString::Printf(
		TEXT("%d campaigns: %d fixed Quest dungeons came out wrong"),
		Campaigns, Wrong));

	for (const TPair<ECataclysmType, ECataclysmDungeonSubType>& Pair : Expected)
	{
		TestTrue(FString::Printf(
			TEXT("at least %d %s Quest dungeons were landed (%d)"),
			EnoughOfEach, *UCataclysmRoster::NameFor(Pair.Key).ToString(),
			FixedSeen.FindRef(Pair.Key)),
			FixedSeen.FindRef(Pair.Key) >= EnoughOfEach);
	}

	for (const ECataclysmType Cataclysm : Rolling)
	{
		const TSet<ECataclysmDungeonSubType>* Seen = RolledSeen.Find(Cataclysm);
		TestTrue(FString::Printf(
			TEXT("CONTROL: %s's Quest dungeons still roll, two sub-types or more"),
			*UCataclysmRoster::NameFor(Cataclysm).ToString()),
			Seen != nullptr && Seen->Num() >= 2);
	}

	TestTrue(TEXT("CONTROL: the five's ordinary dungeons still roll"),
			 OrdinaryFromTheFive.Num() >= 2);

	TestEqual(TEXT("no fixed Quest dungeon came out another sub-type"), Wrong, 0);

	return true;
}

// ---------------------------------------------------------------------------
// The Void's stacks: which dungeon adds them, how many, and its aim -- 2026-10-22
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeOnlyVoidOrdinaryAddsStacksTest,
	"Cataclysm.Surge.OnlyAnOrdinaryDungeonTheVoidSentAddsStacks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeOnlyVoidOrdinaryAddsStacksTest::RunTest(const FString& Parameters)
{
	FCataclysmDungeon Dungeon;
	Dungeon.Type = ECataclysmDungeonType::Basic;
	Dungeon.Cataclysm = ECataclysmType::Void;

	TestTrue(TEXT("an ordinary dungeon the Void sent adds stacks"),
			 Dungeon.AddsVoidStacks());

	// THE VOID'S OTHER KINDS DO NOT. Its Quest dungeon's hit is the ordinary
	// Quest hit until the Hollow is built; a Fallen City and a Cataclysm
	// dungeon are not its timer's to add.
	for (const ECataclysmDungeonType Kind : { ECataclysmDungeonType::Quest,
											  ECataclysmDungeonType::FallenCity,
											  ECataclysmDungeonType::Cataclysm })
	{
		Dungeon.Type = Kind;
		TestFalse(FString::Printf(TEXT("a %s dungeon the Void sent does not"),
								  *UCataclysmSurgeScheduler::KindName(Kind)),
				  Dungeon.AddsVoidStacks());
	}

	// THE CONTROL: EVERY OTHER SENDER'S ORDINARY DUNGEON DOES NOT, nobody's
	// included.
	Dungeon.Type = ECataclysmDungeonType::Basic;

	TArray<ECataclysmType> Others = UCataclysmRoster::All();
	Others.Remove(ECataclysmType::Void);
	Others.Add(ECataclysmType::None);

	if (!TestEqual(TEXT("set-up: seven other Cataclysms and nobody"), Others.Num(), 8))
	{
		return false;
	}

	for (const ECataclysmType Sender : Others)
	{
		Dungeon.Cataclysm = Sender;
		TestFalse(FString::Printf(TEXT("CONTROL: an ordinary dungeon %s sent does not"),
								  *UCataclysmRoster::NameFor(Sender).ToString()),
				  Dungeon.AddsVoidStacks());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeVoidStacksByThirdTest,
	"Cataclysm.Surge.AVoidDungeonAddsOneTwoOrThreeStacksByTheThirdOfItsRowsDepth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeVoidStacksByThirdTest::RunTest(const FString& Parameters)
{
	auto StacksAt = [](ECataclysmCityTier Tier, int32 Floors, int32 UpgradeFloors)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.Type = ECataclysmDungeonType::Basic;
		Dungeon.Cataclysm = ECataclysmType::Void;
		Dungeon.CityTier = Tier;
		Dungeon.Floors = Floors;
		Dungeon.UpgradeFloors = UpgradeFloors;
		return Dungeon.VoidStacksItAdds();
	};

	// THE TWO ROWS THE FIGURES BELOW ARE WORKED FROM. If a range moves, the
	// boundaries move with it and this says so rather than failing on a figure.
	const FCataclysmDungeonSpec Outpost = UCataclysmSurgeScheduler::SpecFor(
		ECataclysmDungeonType::Basic, ECataclysmCityTier::Outpost);
	const FCataclysmDungeonSpec Bulwark = UCataclysmSurgeScheduler::SpecFor(
		ECataclysmDungeonType::Basic, ECataclysmCityTier::Bulwark);

	if (!TestEqual(TEXT("set-up: an ordinary Outpost dungeon is 8 floors at least"), Outpost.LeastFloors, 8)
		|| !TestEqual(TEXT("set-up: and 15 at most"), Outpost.MostFloors, 15)
		|| !TestEqual(TEXT("set-up: an ordinary Bulwark dungeon is 15 at least"), Bulwark.LeastFloors, 15)
		|| !TestEqual(TEXT("set-up: and 25 at most"), Bulwark.MostFloors, 25))
	{
		return false;
	}

	struct FCase
	{
		ECataclysmCityTier Tier;
		int32 Floors;
		int32 Expected;
		const TCHAR* What;
	};

	// `1 + clamp(int(3 x (natural - least) / (most - least)), 0, 2)`, worked
	// out by hand for each line.
	const FCase Cases[] = {
		{ ECataclysmCityTier::Outpost, 8, 1, TEXT("the shallowest Outpost dungeon, 8 floors") },
		{ ECataclysmCityTier::Outpost, 10, 1, TEXT("10 floors, the top of the first third (6 / 7 is 0)") },
		{ ECataclysmCityTier::Outpost, 11, 2, TEXT("11 floors, the start of the second (9 / 7 is 1)") },
		{ ECataclysmCityTier::Outpost, 12, 2, TEXT("12 floors, the top of the second (12 / 7 is 1)") },
		{ ECataclysmCityTier::Outpost, 13, 3, TEXT("13 floors, the start of the third (15 / 7 is 2)") },
		{ ECataclysmCityTier::Outpost, 15, 3, TEXT("the deepest Outpost dungeon, 15 floors (21 / 7 is 3, held at 2)") },
		{ ECataclysmCityTier::Outpost, 5, 1, TEXT("5 floors, below the row, held at the first") },
		{ ECataclysmCityTier::Outpost, 30, 3, TEXT("30 floors, above the row, held at the third") },
		{ ECataclysmCityTier::Bulwark, 15, 1, TEXT("the shallowest Bulwark dungeon, 15 floors") },
		{ ECataclysmCityTier::Bulwark, 18, 1, TEXT("18 floors, the top of the first third (9 / 10 is 0)") },
		{ ECataclysmCityTier::Bulwark, 19, 2, TEXT("19 floors, the start of the second (12 / 10 is 1)") },
		{ ECataclysmCityTier::Bulwark, 21, 2, TEXT("21 floors, the top of the second (18 / 10 is 1)") },
		{ ECataclysmCityTier::Bulwark, 22, 3, TEXT("22 floors, the start of the third (21 / 10 is 2)") },
		{ ECataclysmCityTier::Bulwark, 25, 3, TEXT("the deepest Bulwark dungeon, 25 floors") },
	};

	for (const FCase& Case : Cases)
	{
		TestEqual(FString::Printf(TEXT("%s adds %d"), Case.What, Case.Expected),
				  StacksAt(Case.Tier, Case.Floors, 0), Case.Expected);
	}

	// A CITY'S FLOOR UPGRADES MOVE NO STACKS. Each line has a control: the same
	// floors with no upgrade counted come out in another third, so the upgrade
	// floors really are what is left out.
	TestEqual(TEXT("16 floors of which an upgrade added 5 are 11 natural and add 2"),
			  StacksAt(ECataclysmCityTier::Outpost, 16, 5), 2);
	TestEqual(TEXT("CONTROL: 16 floors all natural add 3"),
			  StacksAt(ECataclysmCityTier::Outpost, 16, 0), 3);
	TestEqual(TEXT("8 floors after an upgrade took 5 are 13 natural and add 3"),
			  StacksAt(ECataclysmCityTier::Outpost, 8, -5), 3);
	TestEqual(TEXT("CONTROL: 8 floors all natural add 1"),
			  StacksAt(ECataclysmCityTier::Outpost, 8, 0), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeVoidWeightTest,
	"Cataclysm.Surge.TheVoidsWeightOnACityRisesWithItsStacksToTwiceAtTen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeVoidWeightTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	UCataclysmEmpireMap* Map = MakeMap();

	// ONE CITY OF EACH TIER, found rather than written out.
	TArray<int32> OneOfEach;
	for (const ECataclysmCityTier Tier : { ECataclysmCityTier::Outpost,
										   ECataclysmCityTier::Bulwark,
										   ECataclysmCityTier::Sanctuary,
										   ECataclysmCityTier::Pillar })
	{
		OneOfEach.Append(CitiesAtTier(*Map, Tier, 1));
	}

	if (!TestEqual(TEXT("set-up: one city of each of the four tiers"), OneOfEach.Num(), 4))
	{
		return false;
	}

	const ECataclysmType Others[] = { ECataclysmType::None, ECataclysmType::Death,
									  ECataclysmType::Chaos, ECataclysmType::Celestial };

	for (const int32 CityId : OneOfEach)
	{
		FCataclysmCity City = *Map->Find(CityId);
		const float Base = UCataclysmSurgeScheduler::TargetWeightFor(City.Tier);
		const FString Tier = UCataclysmEmpireMap::TierName(City.Tier);

		auto VoidAt = [&City](int32 Stacks)
		{
			City.VoidStacks = Stacks;
			return UCataclysmSurgeScheduler::TargetWeightForSender(City, ECataclysmType::Void);
		};

		// EXACT, AND NOT WITHIN A TOLERANCE: a Void slot on a city with no stacks
		// must weigh the very float an ordinary slot does, or a wave against an
		// empire with no stacks would land somewhere else.
		TestTrue(FString::Printf(TEXT("the Void wants a %s with no stacks exactly as much as anybody (%f)"),
								 *Tier, Base),
				 VoidAt(0) == Base);
		TestTrue(FString::Printf(TEXT("half as much again at five stacks (%f)"), VoidAt(5)),
				 VoidAt(5) == Base * 1.5f);
		TestTrue(FString::Printf(TEXT("twice as much at ten (%f)"), VoidAt(10)),
				 VoidAt(10) == Base * 2.0f);
		TestTrue(FString::Printf(TEXT("and no more than twice at fifteen (%f)"), VoidAt(15)),
				 VoidAt(15) == Base * 2.0f);

		// THE CONTROL: ANY OTHER SENDER IGNORES THE STACKS.
		City.VoidStacks = 10;
		for (const ECataclysmType Sender : Others)
		{
			TestTrue(FString::Printf(TEXT("CONTROL: %s wants a %s at ten stacks only by its tier"),
									 *UCataclysmRoster::NameFor(Sender).ToString(), *Tier),
					 UCataclysmSurgeScheduler::TargetWeightForSender(City, Sender) == Base);
		}
	}

	// AND THE MULTIPLICATION IS VISIBLE: an Outpost's tier weight is not nought.
	TestTrue(TEXT("CONTROL: an Outpost's tier weight is more than nought"),
			 UCataclysmSurgeScheduler::TargetWeightFor(ECataclysmCityTier::Outpost) > 0.0f);

	return true;
}

/**
 * A wave the Void sends lands where a wave nobody sends does, while no city
 * carries a stack.
 *
 * THE STREAM CHECK FOR THE VOID'S AIM, ruled by the coordinating session on
 * 2026-10-22. The same seed is rolled twice against the same map with no
 * stacks on it, once with every slot sent by nobody and once with every slot
 * sent by the Void. Everything drawn from the wave's stream must come out the
 * same: the cities, the kinds, the depths, the timers and the sub-types. Then
 * the same on a map where every rim city has a dungeon cap of two, so cities
 * fill and leave the candidates part way through, which is where a total
 * worked out afresh could part from the running one.
 *
 * WHAT WOULD BREAK IT: a Void slot summing its own total when no city carries
 * a stack, or a sender drawn from the wave's stream rather than its own.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeVoidWaveWithoutStacksTest,
	"Cataclysm.Surge.AWaveTheVoidSendsLandsWhereANobodysWaveDoesWhileNoCityCarriesStacks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeVoidWaveWithoutStacksTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	UCataclysmSurgeScheduler* Scheduler = MakeScheduler(ECataclysmSurgeMode::Static);
	UCataclysmEmpireMap* Map = MakeMap();

	int32 Compared = 0;

	for (int32 Seed = 1; Seed <= 60; ++Seed)
	{
		FRandomStream Plain(Seed);
		FRandomStream Voided(Seed);

		int32 VoidAsked = 0;
		auto SentByNobody = []() { return ECataclysmType::None; };
		auto SentByTheVoid = [&VoidAsked]()
		{
			++VoidAsked;
			return ECataclysmType::Void;
		};

		const TArray<FCataclysmDungeon> Without =
			Scheduler->RollWave(*Map, 1, 0, Plain, TArray<int32>(), TArray<int32>(),
								SentByNobody);
		const TArray<FCataclysmDungeon> With =
			Scheduler->RollWave(*Map, 1, 0, Voided, TArray<int32>(), TArray<int32>(),
								SentByTheVoid);

		if (!TestEqual(FString::Printf(TEXT("seed %d: both waves land as many dungeons"), Seed),
					   With.Num(), Without.Num()))
		{
			return false;
		}

		TestEqual(FString::Printf(TEXT("seed %d: the Void was asked once per dungeon"), Seed),
				  VoidAsked, With.Num());

		for (int32 Index = 0; Index < With.Num(); ++Index)
		{
			const FCataclysmDungeon& A = Without[Index];
			const FCataclysmDungeon& B = With[Index];

			TestEqual(TEXT("on the same city"), B.CityId, A.CityId);
			TestEqual(TEXT("of the same kind"),
					  static_cast<int32>(B.Type), static_cast<int32>(A.Type));
			TestEqual(TEXT("at the same depth"), B.Floors, A.Floors);
			TestEqual(TEXT("with the same timer"), B.ResolveDays, A.ResolveDays);
			TestEqual(TEXT("and the same sub-type"),
					  static_cast<int32>(B.SubType), static_cast<int32>(A.SubType));
			TestEqual(TEXT("each Void dungeon carries the Void"),
					  static_cast<int32>(B.Cataclysm), static_cast<int32>(ECataclysmType::Void));
			TestEqual(TEXT("and each of the other carries nobody"),
					  static_cast<int32>(A.Cataclysm), static_cast<int32>(ECataclysmType::None));
			++Compared;
		}

		TestEqual(FString::Printf(TEXT("seed %d: and both streams end in the same place"), Seed),
				  Voided.GetCurrentSeed(), Plain.GetCurrentSeed());
	}

	// CITIES THAT FILL. Every exposed city takes two at most, so a wave of 30
	// fills all twelve and stops at 24.
	UCataclysmEmpireMap* Capped = MakeMap();
	for (const int32 CityId : Capped->ExposedCities())
	{
		Give(*Capped, CityId, ECataclysmCityUpgradeEffect::DungeonCap, 2.0f);
	}

	int32 Filled = 0;

	for (int32 Seed = 1; Seed <= 30; ++Seed)
	{
		FRandomStream Plain(Seed);
		FRandomStream Voided(Seed);
		TArray<ECataclysmType> Senders;

		const TArray<int32> Without = Scheduler->PickTargets(
			*Capped, 30, Plain, NoDungeonsAnywhere(*Capped),
			[]() { return ECataclysmType::None; });
		const TArray<int32> With = Scheduler->PickTargets(
			*Capped, 30, Voided, NoDungeonsAnywhere(*Capped),
			[]() { return ECataclysmType::Void; }, &Senders);

		TestTrue(FString::Printf(TEXT("seed %d: a capped wave the Void sends lands on the same cities in the same order"), Seed),
				 With == Without);
		TestEqual(FString::Printf(TEXT("seed %d: with one sender for each"), Seed),
				  Senders.Num(), With.Num());

		Filled += With.Num() == 24 ? 1 : 0;
	}

	AddInfo(FString::Printf(TEXT("%d dungeons compared; %d of 30 capped waves filled every city"),
							Compared, Filled));

	// THE CONTROLS. Something was compared, and the capped waves really did fill
	// their cities, which is the case the second half is there for.
	TestTrue(TEXT("CONTROL: dungeons were compared"), Compared > 0);
	TestEqual(TEXT("CONTROL: every capped wave filled all twelve cities and stopped at 24"),
			  Filled, 30);

	return true;
}

/**
 * A slot the Void sends lands on a city carrying stacks more often; the
 * senders come back one per city landed on, in order.
 *
 * THE PULL REACHES THE WAVE. `TheVoidsWeightOnACityRisesWithItsStacksToTwiceAtTen`
 * checks the weight; this checks that `PickTargets` uses it. On an intact map
 * the twelve rim Outposts are the frontier, each weighing 5. One carries ten
 * stacks, so a Void slot lands on it 10 times in 65 and a slot nobody sends 5
 * times in 60. Expected landings in 2,400 slots: about 369 and about 200.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeVoidPullTest,
	"Cataclysm.Surge.AVoidSlotLandsOnACityCarryingStacksMoreOftenAndEachSlotKeepsItsSender",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeVoidPullTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSurgeTest;

	UCataclysmSurgeScheduler* Scheduler = MakeScheduler(ECataclysmSurgeMode::Static);
	UCataclysmEmpireMap* Map = MakeMap();

	const TArray<int32> Rim = CitiesAtTier(*Map, ECataclysmCityTier::Outpost, 1);
	if (!TestEqual(TEXT("set-up: an Outpost was found"), Rim.Num(), 1)
		|| !TestEqual(TEXT("set-up: the frontier is the twelve rim Outposts"),
					  Map->ExposedCities().Num(), 12))
	{
		return false;
	}

	const int32 Stacked = Rim[0];
	Map->FindMutable(Stacked)->VoidStacks = 10;

	constexpr int32 Slots = 2400;

	FRandomStream VoidStream(31);
	FRandomStream NobodyStream(31);
	TArray<ECataclysmType> VoidSenders;

	const TArray<int32> ByTheVoid = Scheduler->PickTargets(
		*Map, Slots, VoidStream, TArray<int32>(),
		[]() { return ECataclysmType::Void; }, &VoidSenders);
	const TArray<int32> ByNobody = Scheduler->PickTargets(
		*Map, Slots, NobodyStream, TArray<int32>(),
		[]() { return ECataclysmType::None; });

	const int32 VoidOn = ByTheVoid.FilterByPredicate(
		[Stacked](int32 CityId) { return CityId == Stacked; }).Num();
	const int32 NobodyOn = ByNobody.FilterByPredicate(
		[Stacked](int32 CityId) { return CityId == Stacked; }).Num();

	AddInfo(FString::Printf(TEXT("of %d slots the Void landed %d on the city at ten stacks, nobody %d"),
							Slots, VoidOn, NobodyOn));

	TestEqual(TEXT("every Void slot landed"), ByTheVoid.Num(), Slots);
	TestTrue(TEXT("the Void lands on the stacked city more than half as often again"),
			 VoidOn * 2 > NobodyOn * 3);
	TestTrue(TEXT("and less than two and a half times as often"),
			 VoidOn * 2 < NobodyOn * 5);

	// ONE SENDER PER CITY LANDED ON, IN ORDER.
	if (!TestEqual(TEXT("one sender came back for each city landed on"),
				   VoidSenders.Num(), ByTheVoid.Num()))
	{
		return false;
	}

	TestEqual(TEXT("and every one is the Void"),
			  VoidSenders.FilterByPredicate(
				  [](ECataclysmType Sender) { return Sender == ECataclysmType::Void; }).Num(),
			  Slots);

	int32 Asked = 0;
	TArray<ECataclysmType> Mixed;
	FRandomStream MixedStream(31);
	const TArray<int32> Alternating = Scheduler->PickTargets(
		*Map, 9, MixedStream, TArray<int32>(),
		[&Asked]()
		{
			return (Asked++ % 3 == 0) ? ECataclysmType::Death : ECataclysmType::Famine;
		},
		&Mixed);

	TestEqual(TEXT("a mixed wave of nine was asked nine times"), Asked, 9);
	if (TestEqual(TEXT("and returned nine senders"), Mixed.Num(), 9))
	{
		for (int32 Index = 0; Index < Mixed.Num(); ++Index)
		{
			TestEqual(FString::Printf(TEXT("slot %d carries the answer given for it"), Index),
					  static_cast<int32>(Mixed[Index]),
					  static_cast<int32>(Index % 3 == 0 ? ECataclysmType::Death
														: ECataclysmType::Famine));
		}
	}

	TestEqual(TEXT("CONTROL: and landed nine"), Alternating.Num(), 9);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
