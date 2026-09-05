// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmCityUpgrade.h"
#include "Empire/CataclysmEmpireMap.h"
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

	// A BIGGER CITY TAKES A SMALLER SHARE OF ITSELF FROM EACH BITE, and that is
	// not a typing error: a Sanctuary has eight times an Outpost's defence and
	// loses 8% of it rather than 10%, so it takes far longer to kill.
	TestTrue(TEXT("a Sanctuary loses a smaller share per bite than an Outpost"),
			 Sanctuary.DefenceBite < Outpost.DefenceBite);

	// ONLY BASIC DUNGEONS ARE BUILT.
	TestTrue(TEXT("a basic dungeon has a spec"), Outpost.IsBuilt());
	TestFalse(TEXT("a quest dungeon does not"),
			  UCataclysmSurgeScheduler::SpecFor(
				  ECataclysmDungeonType::Quest,
				  ECataclysmCityTier::Outpost).IsBuilt());
	TestFalse(TEXT("nor a fallen city"),
			  UCataclysmSurgeScheduler::SpecFor(
				  ECataclysmDungeonType::FallenCity,
				  ECataclysmCityTier::Bulwark).IsBuilt());
	TestFalse(TEXT("nor the Cataclysm boss dungeon"),
			  UCataclysmSurgeScheduler::SpecFor(
				  ECataclysmDungeonType::Cataclysm,
				  ECataclysmCityTier::Pillar).IsBuilt());

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
	// whatever either was. These are the eight numbers in
	// `config.SUBTYPE_SPAWN_WEIGHTS`.
	TestEqual(TEXT("no sub-type at all is weighted 34"),
			  UCataclysmSurgeScheduler::SpawnWeightFor(
				  ECataclysmDungeonSubType::None), 34.0f, 0.0001f);

	TestEqual(TEXT("Timed is 12"),
			  UCataclysmSurgeScheduler::SpawnWeightFor(
				  ECataclysmDungeonSubType::Timed), 12.0f, 0.0001f);

	TestEqual(TEXT("Horde is 12"),
			  UCataclysmSurgeScheduler::SpawnWeightFor(
				  ECataclysmDungeonSubType::Horde), 12.0f, 0.0001f);

	TestEqual(TEXT("Siege is 10"),
			  UCataclysmSurgeScheduler::SpawnWeightFor(
				  ECataclysmDungeonSubType::Siege), 10.0f, 0.0001f);

	TestEqual(TEXT("Cow Level is 4"),
			  UCataclysmSurgeScheduler::SpawnWeightFor(
				  ECataclysmDungeonSubType::CowLevel), 4.0f, 0.0001f);

	TestEqual(TEXT("Elite is 10"),
			  UCataclysmSurgeScheduler::SpawnWeightFor(
				  ECataclysmDungeonSubType::Elite), 10.0f, 0.0001f);

	TestEqual(TEXT("Volatile is 10"),
			  UCataclysmSurgeScheduler::SpawnWeightFor(
				  ECataclysmDungeonSubType::Volatile), 10.0f, 0.0001f);

	TestEqual(TEXT("Sacrificial is 8"),
			  UCataclysmSurgeScheduler::SpawnWeightFor(
				  ECataclysmDungeonSubType::Sacrificial), 8.0f, 0.0001f);

	// AND NOTHING IS LEFT WITHOUT ONE. Summing what the enum holds rather than
	// the eight literals above means a sub-type added to the enum and forgotten
	// makes this fail, instead of quietly never spawning.
	float Total = 0.0f;
	for (uint8 Value = 0;
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

	TestEqual(TEXT("and the eight of them add up to 100"), Total, 100.0f,
			  0.0001f);

	// A DUNGEON THAT DOES SOMETHING UNUSUAL SHOULD BE WORTH NOTICING. Plain is
	// the commonest outcome by a wide margin, and Cow Level the rarest, which is
	// what "ridiculous amounts of loot" has to be paid for with.
	TestTrue(TEXT("plain is commoner than any single sub-type"),
			 UCataclysmSurgeScheduler::SpawnWeightFor(
				 ECataclysmDungeonSubType::None) >
			 UCataclysmSurgeScheduler::SpawnWeightFor(
				 ECataclysmDungeonSubType::Timed));

	TestTrue(TEXT("and Cow Level is the rarest of them"),
			 UCataclysmSurgeScheduler::SpawnWeightFor(
				 ECataclysmDungeonSubType::CowLevel) <
			 UCataclysmSurgeScheduler::SpawnWeightFor(
				 ECataclysmDungeonSubType::Sacrificial));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSurgeSubTypeRollTest,
	"Cataclysm.Surge.RollingManySubTypesGivesTheDesignedSpread",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSurgeSubTypeRollTest::RunTest(const FString& Parameters)
{
	// ENOUGH ROLLS THAT THE RAREST ONE IS NOT A COINCIDENCE. Cow Level is 4 in
	// 100, so 20,000 rolls should give about 800 of them; a tolerance of two
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

	for (uint8 Value = 0;
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

	// AND THE SPREAD IS NOT MERELY CLOSE TO THE WEIGHTS, IT IS ORDERED BY THEM.
	// Eight shares all within two points of 12.5 would pass the check above and
	// mean the weights were being ignored.
	TestTrue(TEXT("plain came up more often than Timed"),
			 Counts.FindRef(ECataclysmDungeonSubType::None) >
			 Counts.FindRef(ECataclysmDungeonSubType::Timed));

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

	// A SEED THAT ROLLS A COW LEVEL, FOUND BY LOOKING. Cow Level is 4 in 100, so
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
	// wave that only ever produced plain dungeons would pass every other test in
	// this file.
	TestEqual(TEXT("all eight sub-types turned up across the waves"),
			  Seen.Num(), 8);

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
	int32 Dungeons = 0;

	for (int32 Wave = 0; Wave < 60; ++Wave)
	{
		for (const FCataclysmDungeon& Dungeon :
			 Scheduler->RollWave(*Map, 1, Dungeons, Free))
		{
			++Dungeons;
			if (Dungeon.SubType == ECataclysmDungeonSubType::Siege)
			{
				++SiegesWhenAllowed;
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
	int32 BarredDungeons = 0;

	for (int32 Wave = 0; Wave < 60; ++Wave)
	{
		for (const FCataclysmDungeon& Dungeon :
			 Scheduler->RollWave(*Map, 1, BarredDungeons, Barred, TArray<int32>(),
								 AllBesieged))
		{
			++BarredDungeons;
			if (Dungeon.SubType == ECataclysmDungeonSubType::Siege)
			{
				++SiegesWhenBarred;
			}
		}
	}

	TestEqual(TEXT("and none at all when every city already has one"),
			  SiegesWhenBarred, 0);

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

#endif // WITH_AUTOMATION_TESTS
