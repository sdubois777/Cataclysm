// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmSurge.h"

/**
 * Tests for the empire map, issue #1081.
 *
 * WHAT THESE PIN AND WHAT `tools/tests/test_empire_map_port.py` PINS. These
 * check that the map behaves: that the lattice has the shape the design
 * document describes, that a lane opens when a city falls and seals when it is
 * retaken, that the loss condition fires exactly when a Sanctuary is lost. The
 * Python test checks something these cannot -- that the per-tier defence and
 * population still match `sim/cataclysm_sim/config.py`, which is where they came
 * from. A test written against a constant cannot notice that the constant is
 * wrong, so both are needed. `CataclysmDayClockTests.cpp` beside this is in the
 * same arrangement.
 *
 * WHERE THE EXPECTED FIGURES COME FROM. They were read out of
 * `sim/cataclysm_sim/world.py` by running it, not worked out here. City 12 is
 * the Pillar in both, because both walk the lattice row by row and hand out
 * identifiers in that order.
 */

namespace CataclysmEmpireMapTest
{
	UCataclysmEmpireMap* MakeMap()
	{
		UCataclysmEmpireMap* Map = NewObject<UCataclysmEmpireMap>();
		Map->Build();
		return Map;
	}

	int32 CountAtTier(const UCataclysmEmpireMap& Map, ECataclysmCityTier Tier)
	{
		int32 Found = 0;
		for (const FCataclysmCity& City : Map.Cities)
		{
			if (City.Tier == Tier)
			{
				++Found;
			}
		}
		return Found;
	}
}

// ---------------------------------------------------------------------------
// The shape
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireMapShapeTest,
	"Cataclysm.EmpireMap.TheEmpireIsTwentyFiveCitiesInFourRings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireMapShapeTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireMapTest;

	UCataclysmEmpireMap* Map = MakeMap();

	// THE DESIGN DOCUMENT'S COUNTS, section IX. They are a property of the
	// geometry -- ring N holds 4N cells -- rather than a separate decision, so a
	// wrong count means the lattice itself is wrong.
	TestEqual(TEXT("the empire holds twenty-five cities"), Map->Cities.Num(), 25);
	TestEqual(TEXT("twelve Outposts"),
			  CountAtTier(*Map, ECataclysmCityTier::Outpost), 12);
	TestEqual(TEXT("eight Bulwarks"),
			  CountAtTier(*Map, ECataclysmCityTier::Bulwark), 8);
	TestEqual(TEXT("four Sanctuaries"),
			  CountAtTier(*Map, ECataclysmCityTier::Sanctuary), 4);
	TestEqual(TEXT("one Pillar"),
			  CountAtTier(*Map, ECataclysmCityTier::Pillar), 1);

	// AND THE PILLAR IS AT THE CENTRE. City 12 in the simulation too, because
	// both walk the lattice in the same order.
	TestEqual(TEXT("the Pillar is city 12"), Map->PillarId, 12);

	const FCataclysmCity* Pillar = Map->Find(Map->PillarId);
	if (!TestNotNull(TEXT("the Pillar exists"), Pillar))
	{
		return false;
	}

	TestEqual(TEXT("the Pillar sits at the origin"), Pillar->R, 0);
	TestEqual(TEXT("and at column zero"), Pillar->C, 0);
	TestEqual(TEXT("which is ring 0"), Pillar->Ring(), 0);

	// EVERY CITY IS INSIDE THE BALL AND ITS IDENTIFIER IS ITS INDEX. The second
	// half is what lets `FallCost` return a plain array indexed by identifier.
	for (int32 Index = 0; Index < Map->Cities.Num(); ++Index)
	{
		const FCataclysmCity& City = Map->Cities[Index];

		TestEqual(FString::Printf(TEXT("city %d knows its own identifier"), Index),
				  City.CityId, Index);
		TestTrue(FString::Printf(
			TEXT("city %d at (%d,%d) is inside the ball of radius 3"),
			Index, City.R, City.C), City.Ring() <= UCataclysmEmpireMap::Radius);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireMapTierRingTest,
	"Cataclysm.EmpireMap.ATiersRingIsAlsoItsDistanceFromThePillar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireMapTierRingTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireMapTest;

	// HAND-WORKED, NOT THE FORMULA WRITTEN OUT AGAIN. A test that computed
	// `Radius - Ring` and compared it with `TierForRing` would pass whatever
	// `Radius` was.
	TestTrue(TEXT("ring 0 is the Pillar"),
			 UCataclysmEmpireMap::TierForRing(0) == ECataclysmCityTier::Pillar);
	TestTrue(TEXT("ring 1 is a Sanctuary"),
			 UCataclysmEmpireMap::TierForRing(1) == ECataclysmCityTier::Sanctuary);
	TestTrue(TEXT("ring 2 is a Bulwark"),
			 UCataclysmEmpireMap::TierForRing(2) == ECataclysmCityTier::Bulwark);
	TestTrue(TEXT("ring 3 is an Outpost"),
			 UCataclysmEmpireMap::TierForRing(3) == ECataclysmCityTier::Outpost);

	TestEqual(TEXT("the Pillar is nought rings from itself"),
			  UCataclysmEmpireMap::RingForTier(ECataclysmCityTier::Pillar), 0);
	TestEqual(TEXT("a Sanctuary is one ring out"),
			  UCataclysmEmpireMap::RingForTier(ECataclysmCityTier::Sanctuary), 1);
	TestEqual(TEXT("a Bulwark is two"),
			  UCataclysmEmpireMap::RingForTier(ECataclysmCityTier::Bulwark), 2);
	TestEqual(TEXT("an Outpost is three"),
			  UCataclysmEmpireMap::RingForTier(ECataclysmCityTier::Outpost), 3);

	// RING N HOLDS 4N CELLS, which is where 12/8/4/1 comes from.
	TestEqual(TEXT("ring 0 holds the Pillar alone"),
			  UCataclysmEmpireMap::CityCountForRing(0), 1);
	TestEqual(TEXT("ring 1 holds four"),
			  UCataclysmEmpireMap::CityCountForRing(1), 4);
	TestEqual(TEXT("ring 2 holds eight"),
			  UCataclysmEmpireMap::CityCountForRing(2), 8);
	TestEqual(TEXT("ring 3 holds twelve"),
			  UCataclysmEmpireMap::CityCountForRing(3), 12);
	TestEqual(TEXT("there is no ring 4"),
			  UCataclysmEmpireMap::CityCountForRing(4), 0);

	// AND THE MAP AGREES WITH THE ARITHMETIC. Both halves are needed: the lines
	// above would pass if `Build` laid out a different lattice entirely.
	UCataclysmEmpireMap* Map = MakeMap();

	for (int32 Ring = 0; Ring <= UCataclysmEmpireMap::Radius; ++Ring)
	{
		int32 Found = 0;
		for (const FCataclysmCity& City : Map->Cities)
		{
			if (City.Ring() == Ring)
			{
				++Found;
				TestTrue(FString::Printf(
					TEXT("the city at (%d,%d) in ring %d is a %s"),
					City.R, City.C, Ring,
					*UCataclysmEmpireMap::TierName(City.Tier)),
					City.Tier == UCataclysmEmpireMap::TierForRing(Ring));
			}
		}

		TestEqual(FString::Printf(TEXT("ring %d holds what the arithmetic says"),
								  Ring),
				  Found, UCataclysmEmpireMap::CityCountForRing(Ring));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireMapLaneShapeTest,
	"Cataclysm.EmpireMap.EveryStepBetweenNeighboursChangesTheRingByOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireMapLaneShapeTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireMapTest;

	UCataclysmEmpireMap* Map = MakeMap();

	// THIS IS WHAT MAKES THE FRONTIER RULE EXACT. If a step could leave the ring
	// unchanged, a city could be shielded by one no further out than itself and
	// "the cities shielding this one" would stop meaning anything.
	for (const FCataclysmCity& City : Map->Cities)
	{
		for (const int32 Outward : City.Outward)
		{
			const FCataclysmCity* Other = Map->Find(Outward);
			if (!TestNotNull(TEXT("an outward neighbour exists"), Other))
			{
				return false;
			}

			TestEqual(FString::Printf(
				TEXT("city %d's outward neighbour %d is one ring further out"),
				City.CityId, Outward), Other->Ring(), City.Ring() + 1);

			// AND THE LINK RUNS BOTH WAYS. A one-sided lane would let a city be
			// shielded by one that did not know it was shielding.
			TestTrue(FString::Printf(
				TEXT("and city %d shields city %d in return"),
				Outward, City.CityId), Other->Inward.Contains(City.CityId));
		}

		for (const int32 Inward : City.Inward)
		{
			const FCataclysmCity* Other = Map->Find(Inward);
			if (!TestNotNull(TEXT("an inward neighbour exists"), Other))
			{
				return false;
			}

			TestEqual(FString::Printf(
				TEXT("city %d's inward neighbour %d is one ring further in"),
				City.CityId, Inward), Other->Ring(), City.Ring() - 1);
		}

		// THE RIM HAS NOTHING OUTSIDE IT, which is why it is always exposed.
		if (City.Ring() == UCataclysmEmpireMap::Radius)
		{
			TestEqual(FString::Printf(
				TEXT("rim city %d has nothing shielding it"), City.CityId),
				City.Outward.Num(), 0);
			TestEqual(FString::Printf(
				TEXT("and rim city %d has two neighbours along the rim"),
				City.CityId), City.Perimeter.Num(), 2);
		}
		else
		{
			// A PERIMETER LINK IS A RIM THING ONLY. Inside the rim those links
			// would be same-ring lanes, which the exposure rule has no meaning
			// for.
			TestEqual(FString::Printf(
				TEXT("inner city %d has no perimeter links"), City.CityId),
				City.Perimeter.Num(), 0);
		}
	}

	// THE PILLAR IS SHIELDED BY THE FOUR SANCTUARIES AND SHIELDS NOTHING.
	const FCataclysmCity* Pillar = Map->Find(Map->PillarId);
	if (!TestNotNull(TEXT("the Pillar exists"), Pillar))
	{
		return false;
	}

	TestEqual(TEXT("four Sanctuaries shield the Pillar"), Pillar->Outward.Num(), 4);
	TestEqual(TEXT("and the Pillar shields nothing"), Pillar->Inward.Num(), 0);

	return true;
}

// ---------------------------------------------------------------------------
// Exposure
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireMapExposureTest,
	"Cataclysm.EmpireMap.OnlyTheRimIsExposedToStartWith",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireMapExposureTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireMapTest;

	UCataclysmEmpireMap* Map = MakeMap();

	const TArray<int32> Exposed = Map->ExposedCities();

	// TWELVE, AND EXACTLY THE TWELVE OUTPOSTS. A surge at the start of a run has
	// nowhere else to land, which is what makes the empire a frontier rather
	// than a scatter of independent targets.
	TestEqual(TEXT("twelve cities are exposed at the start"), Exposed.Num(), 12);

	for (const FCataclysmCity& City : Map->Cities)
	{
		const bool bOnTheRim = City.Ring() == UCataclysmEmpireMap::Radius;

		TestEqual(FString::Printf(
			TEXT("city %d at ring %d exposed"), City.CityId, City.Ring()),
			Map->IsExposed(City.CityId), bOnTheRim);
	}

	TestFalse(TEXT("and the Pillar is not among them"), Map->IsPillarExposed());
	TestEqual(TEXT("no lane is open into a sealed city"), Map->OpenLanes(), 0);
	TestEqual(TEXT("and nothing is breached"), Map->BreachDepth(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireMapLaneOpensTest,
	"Cataclysm.EmpireMap.ACityFallingOpensALaneAndRetakingItSealsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireMapLaneOpensTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireMapTest;

	UCataclysmEmpireMap* Map = MakeMap();

	// THE LANE WORKED OUT BY HAND: Outpost 0 at (-3,0) is the only rim city
	// shielding nothing else, and Bulwark 2 at (-2,0) is shielded by Outposts 0,
	// 1 and 3.
	TestFalse(TEXT("Bulwark 2 starts sealed"), Map->IsExposed(2));

	TestTrue(TEXT("Outpost 0 falls"), Map->Fall(0));

	TestTrue(TEXT("and Bulwark 2 behind it is now exposed"), Map->IsExposed(2));
	TestFalse(TEXT("while the fallen Outpost itself is not a target"),
			  Map->IsExposed(0));
	TestEqual(TEXT("one lane is open"), Map->OpenLanes(), 1);
	TestEqual(TEXT("and the breach reaches one ring in"), Map->BreachDepth(), 1);

	// THE OTHER TWO CITIES BEHIND THAT OUTPOST ARE UNAFFECTED. Outpost 0 shields
	// only Bulwark 2; nothing else should have opened.
	TestFalse(TEXT("Bulwark 5 elsewhere on the map is still sealed"),
			  Map->IsExposed(5));
	TestFalse(TEXT("and so is Sanctuary 6"), Map->IsExposed(6));

	// AND RETAKING IT CLOSES THE LANE AGAIN. This is the whole point of a Dungeon
	// City being retakeable rather than a permanent loss.
	TestTrue(TEXT("Outpost 0 is retaken"), Map->Retake(0));

	TestTrue(TEXT("it is a target again"), Map->IsExposed(0));
	TestFalse(TEXT("and Bulwark 2 is sealed again"), Map->IsExposed(2));
	TestEqual(TEXT("no lane is open"), Map->OpenLanes(), 0);
	TestEqual(TEXT("and nothing is breached"), Map->BreachDepth(), 0);

	// A CITY CANNOT FALL TWICE OR BE RETAKEN WHILE IT STANDS.
	TestFalse(TEXT("retaking a city that never fell does nothing"),
			  Map->Retake(0));
	TestTrue(TEXT("it falls again"), Map->Fall(0));
	TestFalse(TEXT("but not twice"), Map->Fall(0));

	return true;
}

// ---------------------------------------------------------------------------
// The loss condition
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireMapDefeatTest,
	"Cataclysm.EmpireMap.TheRunIsLostWhenASanctuaryFalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireMapDefeatTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireMapTest;

	UCataclysmEmpireMap* Map = MakeMap();

	// ONE LANE, WALKED IN: Outpost 0, then the Bulwark behind it, then the
	// Sanctuary behind that. Three cities, and the run is over.
	TestEqual(TEXT("an intact empire is three cities from defeat"),
			  Map->DistanceToDefeat(), 3);
	TestFalse(TEXT("and the Pillar cannot be reached"), Map->IsPillarExposed());

	Map->Fall(0);
	TestEqual(TEXT("losing the Outpost leaves two"), Map->DistanceToDefeat(), 2);
	TestFalse(TEXT("the Pillar still cannot be reached"), Map->IsPillarExposed());

	Map->Fall(2);
	TestEqual(TEXT("losing the Bulwark behind it leaves one"),
			  Map->DistanceToDefeat(), 1);
	TestFalse(TEXT("and still not"), Map->IsPillarExposed());

	Map->Fall(6);
	TestEqual(TEXT("losing the Sanctuary behind that leaves none"),
			  Map->DistanceToDefeat(), 0);

	// THE LOSS CONDITION. "The run is lost when a clear path to the capital is
	// opened", and this is what a clear path is in lattice terms.
	TestTrue(TEXT("the Cataclysm can reach the Pillar"), Map->IsPillarExposed());
	TestEqual(TEXT("the breach reaches the Sanctuary ring"), Map->BreachDepth(), 3);

	// THE TWO ANSWERS ARE THE SAME QUESTION. A caller may check either.
	TestEqual(TEXT("no distance left is the same fact as the Pillar exposed"),
			  Map->DistanceToDefeat() == 0, Map->IsPillarExposed());

	// AND RETAKING THE SANCTUARY UNDOES IT. Nothing about defeat is one-way
	// until the Last Stand is fought, which is issue #43 and not here.
	TestTrue(TEXT("the Sanctuary is retaken"), Map->Retake(6));
	TestFalse(TEXT("the path closes"), Map->IsPillarExposed());
	TestEqual(TEXT("and the empire is one city from defeat again"),
			  Map->DistanceToDefeat(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireMapScatterTest,
	"Cataclysm.EmpireMap.TwelveCitiesLostInAScatterCostLessThanTwoInALine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireMapScatterTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireMapTest;

	// THE POINT OF THE WHOLE LATTICE, AND THE REASON THE STRATEGY LAYER IS A
	// TRIAGE PROBLEM RATHER THAN A COUNTING ONE. If losing cities were simply
	// bad, the player would defend whatever was cheapest. Because a lane is what
	// kills you, they have to defend the right ones.
	UCataclysmEmpireMap* Scattered = MakeMap();

	int32 Lost = 0;
	for (int32 CityId = 0; CityId < Scattered->Cities.Num(); ++CityId)
	{
		if (Scattered->Cities[CityId].Ring() == UCataclysmEmpireMap::Radius)
		{
			Scattered->Fall(CityId);
			++Lost;
		}
	}

	TestEqual(TEXT("every Outpost on the rim is lost"), Lost, 12);
	TestEqual(TEXT("and the empire is still two cities from defeat"),
			  Scattered->DistanceToDefeat(), 2);
	TestFalse(TEXT("the Pillar cannot be reached"),
			  Scattered->IsPillarExposed());
	TestEqual(TEXT("but every Bulwark is now a target"),
			  Scattered->OpenLanes(), 8);

	UCataclysmEmpireMap* Lane = MakeMap();
	Lane->Fall(0);
	Lane->Fall(2);

	TestEqual(TEXT("two cities lost in a line leave one"),
			  Lane->DistanceToDefeat(), 1);

	TestTrue(TEXT("so twelve scattered losses are further from defeat than two "
				  "in a line"),
			 Scattered->DistanceToDefeat() > Lane->DistanceToDefeat());
	TestEqual(TEXT("although six times as many cities are gone"),
			  Scattered->FallenCityCount(), 6 * Lane->FallenCityCount());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireMapCriticalityTest,
	"Cataclysm.EmpireMap.LaneCriticalityScoresACityByWhatItsLossWouldOpen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireMapCriticalityTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireMapTest;

	// THIS TEST DOES NOT TOUCH THE EXPOSURE RULE, AND THAT IS NOT AN OVERSIGHT.
	// `LaneCriticality` is built on `FallCost`, which reads `bFallen` directly
	// and never calls `IsExposed`. The two are independent readings of the same
	// graph: exposure asks what can be attacked TODAY, fall cost asks how long a
	// chain of falls would have to be, and the chain establishes its own
	// reachability because it bottoms out at the rim, which is always exposed.
	//
	// IT WAS MEASURED RATHER THAN ASSUMED. Breaking `IsExposed` so that a fallen
	// city opens no lane failed four of the twelve tests in this file and left
	// this one passing, which is what said the two calculations are separate.
	UCataclysmEmpireMap* Map = MakeMap();

	const TArray<int32> Criticality = Map->LaneCriticality();

	if (!TestEqual(TEXT("one score per city"), Criticality.Num(), 25))
	{
		return false;
	}

	// ON AN INTACT EMPIRE THE SCORE IS THE RING READ BACKWARDS: losing a
	// Sanctuary would put you three closer to defeat, a Bulwark two, an Outpost
	// one. It stops being that simple the moment anything falls, which is the
	// case below.
	for (const FCataclysmCity& City : Map->Cities)
	{
		if (City.CityId == Map->PillarId)
		{
			TestEqual(TEXT("the Pillar scores nothing; it is the thing being "
						   "defended"),
					  Criticality[City.CityId], 0);
			continue;
		}

		const int32 Expected = UCataclysmEmpireMap::Radius + 1 - City.Ring();
		TestEqual(FString::Printf(TEXT("%s %d scores %d"),
								  *UCataclysmEmpireMap::TierName(City.Tier),
								  City.CityId, Expected),
				  Criticality[City.CityId], Expected);
	}

	// ONCE A LANE IS PART OPEN, THE ELEVEN OUTPOSTS OFF IT STOP MATTERING
	// ALTOGETHER. Outpost 0 has fallen, so the empire stands two cities from
	// defeat and only the two behind the breach can shorten that.
	//
	// THE SCORE IS MEASURED AGAINST WHERE THE EMPIRE STANDS NOW, NOT AGAINST AN
	// INTACT ONE. Bulwark 2 scored 2 a moment ago and scores 1 here, because the
	// distance it is being subtracted from has itself dropped from 3 to 2.
	// Reading it as an absolute worth is the mistake this line exists to catch.
	Map->Fall(0);

	TestEqual(TEXT("the empire now stands two cities from defeat"),
			  Map->DistanceToDefeat(), 2);

	const TArray<int32> After = Map->LaneCriticality();

	TestEqual(TEXT("the Bulwark behind the breach would put you one closer"),
			  After[2], 1);
	TestEqual(TEXT("and the Sanctuary behind that one, two -- the rest of the "
				   "way"),
			  After[6], 2);
	TestEqual(TEXT("the fallen Outpost is worth nothing; it is already gone"),
			  After[0], 0);
	TestEqual(TEXT("an Outpost on the far side of the map is worth nothing"),
			  After[24], 0);

	// THE ELEVEN OUTPOSTS STILL STANDING ARE ALL WORTH NOTHING NOW, and that is
	// the triage instruction the strategy layer is built to give: a surge that
	// lands on one of them can be ignored while a surge on Bulwark 2 cannot.
	int32 WorthlessOutposts = 0;
	for (const FCataclysmCity& City : Map->Cities)
	{
		if (City.Tier == ECataclysmCityTier::Outpost && City.IsAlive()
			&& After[City.CityId] == 0)
		{
			++WorthlessOutposts;
		}
	}

	TestEqual(TEXT("every Outpost still standing is off the shortest lane"),
			  WorthlessOutposts, 11);

	return true;
}

// ---------------------------------------------------------------------------
// What a city is worth, and what takes it away
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireMapCityStatsTest,
	"Cataclysm.EmpireMap.ABiggerCityIsWorthMoreAndTakesMoreToBreak",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireMapCityStatsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireMapTest;

	UCataclysmEmpireMap* Map = MakeMap();

	// HAND-WORKED FROM `config.TIER_STATS`. The Python guard beside this checks
	// these against the simulation; these check the map actually hands them out.
	TestEqual(TEXT("an Outpost has 1,000 defence"),
			  UCataclysmEmpireMap::MaxDefenceFor(ECataclysmCityTier::Outpost),
			  1000.0f);
	TestEqual(TEXT("a Bulwark has 3,000"),
			  UCataclysmEmpireMap::MaxDefenceFor(ECataclysmCityTier::Bulwark),
			  3000.0f);
	TestEqual(TEXT("a Sanctuary has 8,000"),
			  UCataclysmEmpireMap::MaxDefenceFor(ECataclysmCityTier::Sanctuary),
			  8000.0f);
	TestEqual(TEXT("and the Pillar has 20,000"),
			  UCataclysmEmpireMap::MaxDefenceFor(ECataclysmCityTier::Pillar),
			  20000.0f);

	// A CITY STARTS FULL.
	for (const FCataclysmCity& City : Map->Cities)
	{
		TestEqual(FString::Printf(TEXT("city %d starts at full defence"),
								  City.CityId),
				  City.Defence, City.MaxDefence);
		TestEqual(FString::Printf(TEXT("city %d starts at full population"),
								  City.CityId),
				  City.Population, City.MaxPopulation);
		TestTrue(FString::Printf(TEXT("city %d is standing"), City.CityId),
				 City.IsAlive());
		TestEqual(FString::Printf(TEXT("city %d gets the defence its tier gets"),
								  City.CityId),
				  City.MaxDefence,
				  UCataclysmEmpireMap::MaxDefenceFor(City.Tier));
	}

	// 12 x 5,000 + 8 x 20,000 + 4 x 60,000 + 150,000.
	TestEqual(TEXT("the empire holds 610,000 people"),
			  Map->TotalMaxPopulation(), 610000.0f);
	TestEqual(TEXT("and none of them have been lost yet"),
			  Map->TotalPopulation(), 610000.0f);
	TestEqual(TEXT("no city has fallen"), Map->FallenCityCount(), 0);
	TestEqual(TEXT("and twenty-four cities stand beside the Pillar"),
			  Map->AliveCities().Num(), 24);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireMapBiteTest,
	"Cataclysm.EmpireMap.ABiteTakesAShareOfTheMaximumAndZeroDefenceIsAFall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireMapBiteTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireMapTest;

	UCataclysmEmpireMap* Map = MakeMap();

	// OUTPOST 0: 1,000 defence and 5,000 people. A tenth of its defence and a
	// twentieth of its people is what a basic dungeon on an Outpost takes,
	// before any scaling.
	TestFalse(TEXT("one bite does not take it"), Map->Bite(0, 0.10f, 0.05f));

	const FCataclysmCity* City = Map->Find(0);
	if (!TestNotNull(TEXT("Outpost 0 exists"), City))
	{
		return false;
	}

	TestEqual(TEXT("a tenth of 1,000 defence is gone"), City->Defence, 900.0f);
	TestEqual(TEXT("and a twentieth of 5,000 people"), City->Population, 4750.0f);
	TestEqual(TEXT("which is nine tenths of its defence left"),
			  City->DefenceFraction(), 0.9f, 0.0001f);

	// THE SHARE IS OF THE MAXIMUM, NOT OF WHAT IS LEFT. That is what makes an
	// ignored city die on a schedule: ten bites, not an approach to zero that
	// never arrives.
	for (int32 Bites = 2; Bites <= 9; ++Bites)
	{
		TestFalse(FString::Printf(TEXT("bite %d does not take it"), Bites),
				  Map->Bite(0, 0.10f, 0.05f));
	}

	TestEqual(TEXT("nine bites later a tenth of its defence is left"),
			  Map->Find(0)->Defence, 100.0f, 0.01f);

	TestTrue(TEXT("the tenth bite takes it"), Map->Bite(0, 0.10f, 0.05f));
	TestTrue(TEXT("the Outpost has fallen"), Map->Find(0)->bFallen);
	TestEqual(TEXT("with nothing left to defend it"), Map->Find(0)->Defence, 0.0f);

	// A FALLEN CITY IS NOT BITTEN AGAIN. There is nothing left there to take, and
	// a dungeon that resolves on one refreshes its timer instead.
	const float PopulationWhenItFell = Map->Find(0)->Population;
	TestFalse(TEXT("biting a fallen city does nothing"),
			  Map->Bite(0, 0.50f, 0.50f));
	TestEqual(TEXT("and takes nobody"), Map->Find(0)->Population,
			  PopulationWhenItFell);

	// POPULATION NEVER GOES NEGATIVE, however large the bite.
	TestTrue(TEXT("a huge bite takes Outpost 1 outright"),
			 Map->Bite(1, 5.0f, 5.0f));
	TestEqual(TEXT("and leaves nobody rather than fewer than nobody"),
			  Map->Find(1)->Population, 0.0f);

	// AND A RETAKEN CITY COMES BACK WITH HALF, NOT ALL. Retaking is a repair,
	// not an undo.
	TestTrue(TEXT("Outpost 0 is retaken"), Map->Retake(0));
	TestEqual(TEXT("with half its defence"), Map->Find(0)->Defence, 500.0f);
	TestEqual(TEXT("and half its people"), Map->Find(0)->Population, 2500.0f);

	return true;
}

/**
 * Damage is a number of points, and the city's ceiling is not a factor in it.
 *
 * THE PROPERTY ISSUE #1331 EXISTS FOR, ASSERTED AS A PROPERTY RATHER THAN AS A
 * NUMBER. `Damage` taking a fixed 100 points off an Outpost is not by itself
 * evidence of anything -- the old code would also have removed 100 points if it
 * had been handed 0.1. What discriminates the two is what happens when the
 * city's maximum MOVES: points do not follow it, a share does.
 *
 * BOTH DIRECTIONS ARE CHECKED. A city whose ceiling was raised loses the same
 * points, and the same call through `Bite` loses proportionally more. Without
 * the second half this would pass on an implementation that had quietly stopped
 * taking damage at all.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireMapDamageIsPointsTest,
	"Cataclysm.EmpireMap.DamageIsPointsAndTheCitysCeilingIsNotAFactorInIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireMapDamageIsPointsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireMapTest;

	UCataclysmEmpireMap* Map = MakeMap();

	// TWO OUTPOSTS OF THE SAME TIER, so the comparison is of the ceiling and not
	// of the cities. Both start at 1,000 defence and 5,000 people.
	const FCataclysmCity* Plain = Map->Find(0);
	const FCataclysmCity* Raised = Map->Find(1);

	if (!TestNotNull(TEXT("Outpost 0 exists"), Plain)
		|| !TestNotNull(TEXT("Outpost 1 exists"), Raised))
	{
		return false;
	}

	if (!TestEqual(TEXT("the two cities are the same tier"),
				   static_cast<int32>(Plain->Tier),
				   static_cast<int32>(Raised->Tier)))
	{
		return false;
	}

	// THE UPGRADE THE GAME SHIPS: `Architect_Increase_max_defense_by_20`, at the
	// value `game/Data/CityUpgrades.csv` gives it.
	FCataclysmCityUpgrade Bigger;
	Bigger.RowName = FName(TEXT("Architect_Increase_max_defense_by_20"));
	Bigger.Effect = ECataclysmCityUpgradeEffect::MaxDefence;
	Bigger.Value = 0.2f;

	FCataclysmCityUpgrade MorePeople;
	MorePeople.RowName = FName(TEXT("Architect_Increase_max_population_by_20"));
	MorePeople.Effect = ECataclysmCityUpgradeEffect::MaxPopulation;
	MorePeople.Value = 0.2f;

	TestTrue(TEXT("Outpost 1 buys the bigger ceiling"), Map->AddUpgrade(1, Bigger));
	TestTrue(TEXT("and the bigger population"),
			 Map->AddUpgrade(1, MorePeople));

	TestEqual(TEXT("its ceiling really did move"), Raised->MaxDefence, 1200.0f,
			  0.01f);
	TestEqual(TEXT("and it is still full"), Raised->Defence, 1200.0f, 0.01f);

	// A HUNDRED POINTS EACH, which is what a basic dungeon on an Outpost takes.
	TestFalse(TEXT("the plain city survives it"), Map->Damage(0, 100.0f, 250.0f));
	TestFalse(TEXT("so does the raised one"), Map->Damage(1, 100.0f, 250.0f));

	TestEqual(TEXT("the plain city lost exactly a hundred points"),
			  Plain->MaxDefence - Plain->Defence, 100.0f, 0.01f);

	TestEqual(TEXT("and the raised one lost exactly a hundred too, although its "
				   "ceiling is a fifth higher"),
			  Raised->MaxDefence - Raised->Defence, 100.0f, 0.01f);

	TestEqual(TEXT("population likewise, in people"),
			  Plain->MaxPopulation - Plain->Population, 250.0f, 0.01f);
	TestEqual(TEXT("and the raised city loses the same number of people"),
			  Raised->MaxPopulation - Raised->Population, 250.0f, 0.01f);

	// AND THE SHARE PATH STILL BEHAVES LIKE A SHARE, which is the control. The
	// same 10% takes 100 off the plain city and 120 off the raised one, which is
	// exactly the behaviour that made the upgrade worthless.
	Map->Bite(0, 0.10f, 0.0f);
	Map->Bite(1, 0.10f, 0.0f);

	TestEqual(TEXT("a tenth of the plain city's maximum is 100"),
			  Plain->MaxDefence - Plain->Defence, 200.0f, 0.01f);
	TestEqual(TEXT("a tenth of the raised city's maximum is 120"),
			  Raised->MaxDefence - Raised->Defence, 220.0f, 0.01f);

	// ZERO DEFENCE IS STILL A FALL, THROUGH THE POINTS PATH TOO.
	TestTrue(TEXT("enough points take the city outright"),
			 Map->Damage(0, 5000.0f, 50000.0f));
	TestTrue(TEXT("and it has fallen"), Map->Find(0)->bFallen);
	TestEqual(TEXT("leaving nobody rather than fewer than nobody"),
			  Map->Find(0)->Population, 0.0f);

	// A FALLEN CITY IS NOT DAMAGED AGAIN.
	TestFalse(TEXT("damaging a fallen city does nothing"),
			  Map->Damage(0, 100.0f, 100.0f));

	return true;
}

/**
 * The two city upgrades that raise a ceiling now buy resolves, at every tier.
 *
 * THIS IS THE TEST ISSUE #1331 WAS FILED FOR. Its table said the +20% upgrades
 * bought ZERO extra resolves for a full city at all four tiers, because the
 * damage was a share of the same pool it came out of. The numbers below are the
 * same arithmetic with flat damage, and none of them is zero.
 *
 * THE DAMAGE FIGURES COME FROM `UCataclysmSurgeScheduler::SpecFor` rather than
 * being written out here, so this cannot pass by agreeing with a constant it
 * copied. The RESOLVE COUNTS are written out, because a count computed from the
 * same expression the code uses would pass whatever the code did.
 *
 * A TYPICAL-DEPTH DUNGEON, so `BiteScale` is one and the depth axis is out of
 * the way; how depth scales damage is `FCataclysmSurgeBiteScaleTest`.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireMapCeilingBuysResolvesTest,
	"Cataclysm.EmpireMap.RaisingACitysCeilingBuysMoreResolvesAtEveryTier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireMapCeilingBuysResolvesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireMapTest;

	// HOW MANY RESOLVES A CITY OF THIS TIER SURVIVES, driven through the map
	// rather than computed, so it measures what the game does.
	auto ResolvesToFall =
		[this](ECataclysmCityTier Tier, bool bUpgraded) -> int32
		{
			UCataclysmEmpireMap* Map = MakeMap();

			int32 CityId = INDEX_NONE;
			for (const FCataclysmCity& Candidate : Map->Cities)
			{
				if (Candidate.Tier == Tier)
				{
					CityId = Candidate.CityId;
					break;
				}
			}

			if (CityId == INDEX_NONE)
			{
				AddError(TEXT("no city of that tier is on the map"));
				return -1;
			}

			if (bUpgraded)
			{
				FCataclysmCityUpgrade Bigger;
				Bigger.RowName =
					FName(TEXT("Architect_Increase_max_defense_by_20"));
				Bigger.Effect = ECataclysmCityUpgradeEffect::MaxDefence;
				Bigger.Value = 0.2f;
				Map->AddUpgrade(CityId, Bigger);
			}

			const FCataclysmDungeonSpec Spec = UCataclysmSurgeScheduler::SpecFor(
				ECataclysmDungeonType::Basic, Tier);

			for (int32 Resolves = 1; Resolves <= 200; ++Resolves)
			{
				if (Map->Damage(CityId, Spec.DefenceDamage,
								Spec.PopulationDamage))
				{
					return Resolves;
				}
			}

			AddError(TEXT("the city stood through two hundred resolves"));
			return -1;
		};

	// tier, resolves without the upgrade, resolves with it. WRITTEN OUT BY HAND
	// from the ceilings and the damage the design gives each tier:
	//
	//   Outpost    1,000 / 100   = 10, and 1,200 / 100   = 12
	//   Bulwark    3,000 / 270   = 12, and 3,600 / 270   = 14
	//   Sanctuary  8,000 / 640   = 13, and 9,600 / 640   = 15
	//   Pillar    20,000 / 1,200 = 17, and 24,000 / 1,200 = 20
	struct FCase
	{
		ECataclysmCityTier Tier;
		const TCHAR* Name;
		int32 Plain;
		int32 Upgraded;
	};

	const TArray<FCase> Cases = {
		{ ECataclysmCityTier::Outpost,   TEXT("Outpost"),   10, 12 },
		{ ECataclysmCityTier::Bulwark,   TEXT("Bulwark"),   12, 14 },
		{ ECataclysmCityTier::Sanctuary, TEXT("Sanctuary"), 13, 15 },
		{ ECataclysmCityTier::Pillar,    TEXT("Pillar"),    17, 20 },
	};

	for (const FCase& Row : Cases)
	{
		const int32 Plain = ResolvesToFall(Row.Tier, false);
		const int32 Upgraded = ResolvesToFall(Row.Tier, true);

		TestEqual(*FString::Printf(
					  TEXT("a %s survives %d resolves"), Row.Name, Row.Plain),
				  Plain, Row.Plain);

		TestEqual(*FString::Printf(
					  TEXT("and a %s survives %d with the ceiling upgrade"),
					  Row.Name, Row.Upgraded),
				  Upgraded, Row.Upgraded);

		// THE CLAIM THE ISSUE MADE, SAID PLAINLY. Before #1331 every one of
		// these differences was zero.
		TestTrue(*FString::Printf(
					 TEXT("a %s gains %d resolves from +20%% maximum defence, "
						  "and gained 0 before issue #1331"),
					 Row.Name, Upgraded - Plain),
				 Upgraded > Plain);
	}

	// WHAT THIS DOES NOT CLAIM. A Pillar holds twenty times an Outpost's defence
	// and still lasts 17 resolves against 10, because a Pillar dungeon takes
	// twelve times an Outpost dungeon's points. That ladder is a separate
	// question from this defect, and `docs/DECISIONS.md` records that those
	// numbers were measured and deliberately left where they were.

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireMapErasedTest,
	"Cataclysm.EmpireMap.AnErasedCityCannotBeRetaken",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireMapErasedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireMapTest;

	UCataclysmEmpireMap* Map = MakeMap();

	// NOTHING IN THE GAME MARKS A CITY DOOMED YET -- that is the Void, one of the
	// seven Cataclysms that do not exist, issue #53. The rule is carried because
	// `Retake` has to obey it and because a lane a doomed city was sealing can
	// never be closed again, which changes what the map means.
	FCataclysmCity* Doomed = Map->FindMutable(3);
	if (!TestNotNull(TEXT("Outpost 3 exists"), Doomed))
	{
		return false;
	}

	Doomed->bDoomed = true;

	TestTrue(TEXT("it falls like any other"), Map->Fall(3));
	TestTrue(TEXT("and is erased rather than merely lost"), Map->Find(3)->bErased);
	TestFalse(TEXT("so it cannot be retaken"), Map->Retake(3));
	TestTrue(TEXT("and it stays fallen"), Map->Find(3)->bFallen);

	// THE LANE IT WAS SEALING STAYS OPEN FOR THE REST OF THE RUN.
	TestTrue(TEXT("the Bulwark behind it is exposed"), Map->IsExposed(2));

	// AN ORDINARY CITY BESIDE IT IS UNAFFECTED.
	TestTrue(TEXT("Outpost 0 falls"), Map->Fall(0));
	TestFalse(TEXT("and is not erased"), Map->Find(0)->bErased);
	TestTrue(TEXT("so it can be retaken"), Map->Retake(0));

	return true;
}

// ---------------------------------------------------------------------------
// Seeing it
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireMapRenderTest,
	"Cataclysm.EmpireMap.TheMapDrawsTheSameDiamondTheSimulationDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireMapRenderTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEmpireMapTest;

	UCataclysmEmpireMap* Map = MakeMap();

	// CHARACTER FOR CHARACTER WHAT `Empire.render` PRINTS for an intact empire.
	// It was copied out of a run of the simulation rather than typed from the
	// header's sketch, which is the only way it is worth anything: a rendering
	// that agrees with a picture drawn in a comment proves only that both were
	// written by the same hand.
	const FString Intact =
		TEXT("    O!\n")
		TEXT("   O! B. O!\n")
		TEXT("  O! B. S. B. O!\n")
		TEXT(" O! B. S. P. S. B. O!\n")
		TEXT("  O! B. S. B. O!\n")
		TEXT("   O! B. O!\n")
		TEXT("    O!");

	TestEqual(TEXT("an intact empire draws as twelve exposed Outposts around a "
				   "sealed core"),
			  Map->Render(), Intact);

	// AND A BREACH SHOWS AS ONE. Outpost 0 at (-3,0) is the top of the diamond,
	// and the Bulwark behind it changes from sealed to exposed.
	Map->Fall(0);

	const FString Breached =
		TEXT("    Ox\n")
		TEXT("   O! B! O!\n")
		TEXT("  O! B. S. B. O!\n")
		TEXT(" O! B. S. P. S. B. O!\n")
		TEXT("  O! B. S. B. O!\n")
		TEXT("   O! B. O!\n")
		TEXT("    O!");

	TestEqual(TEXT("a fallen Outpost draws as an x and opens the Bulwark behind "
				   "it"),
			  Map->Render(), Breached);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
