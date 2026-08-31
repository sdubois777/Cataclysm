// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "DayClock/CataclysmDayClock.h"
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

#endif // WITH_AUTOMATION_TESTS
