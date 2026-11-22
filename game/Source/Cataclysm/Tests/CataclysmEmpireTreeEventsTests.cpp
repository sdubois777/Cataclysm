// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPassiveTree.h"
#include "DayClock/CataclysmDayClock.h"
#include "Dom/JsonObject.h"
#include "Empire/CataclysmCityUpgrade.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/DataTable.h"
#include "Player/CataclysmEmpireTree.h"
#include "Save/CataclysmSaveApply.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveMigration.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "UObject/Package.h"

/**
 * The empire upgrade tree's rules that act at an event. Issue #2741 slice 3b: the seven nodes the coordinating session's
 * S2 of 2026-11-18 left to this slice, each reading a judgement of its Q1 to Q7 of 2026-11-19 --
 *
 *   The Architect's Legacy   a retaken city comes back at 75% of its maxima, not half
 *   Civil Order              a living Outpost regains 3% a point of what it holds, on run days 30, 60, ...
 *   The Mason's Guild        on run days 20, 40, ... the living Outpost or Bulwark with the lowest share of its
 *                            maximum defence regains 20% of it and 15% of its maximum population
 *   Global Vigilance         every city takes 1% a point less for each dungeon cleared since the last surge, to 15%
 *   Reclaimer's Resolve      every city takes 1% a point less for each city reclaimed in the run, to 15%
 *   Last Stand Protocols     the run's first ordinary resolve or Quest timer hit that would fell a Sanctuary leaves it at 1
 *   Enduring Foundations     a city upgrade held 10 days or more is 2% a point stronger, for its percentage effects
 *
 * -- and the run record's version 9, which saves what they count.
 *
 * `Cataclysm.EmpireTreeEvents.` IS A NEW GROUP and selects these and nothing else. A RUN MADE BY HAND, handed bonuses
 * through `UCataclysmEmpireRun::SetTreeBonuses`, the one door the game uses; the first test adds the seven nodes up
 * through the real `DT_EmpireTreeEffects` and `DT_EmpireTreeNodes`.
 */
namespace CataclysmEmpireTreeEventsTest
{
	// The tree file's identifiers, by the node names the table gives them.
	const FName CivilOrder(TEXT("basic_arch_t1f"));				// 3% of an Outpost's defence a point, 8 points
	const FName MasonsGuild(TEXT("keystone_00000014"));			// every 20 days, 20% and 15%
	const FName ArchitectsLegacy(TEXT("keystone_0000001g"));	// a retake at 75%
	const FName LastStandProtocols(TEXT("keystone_0000001f"));	// a Sanctuary survives at 1, once
	const FName EnduringFoundations(TEXT("basic_arch_t1e"));	// +2% an upgrade's effect a point after 10 days, 10 points
	const FName GlobalVigilance(TEXT("basic_00000015"));		// -1% a clear a point, reset by a surge, 15% cap, 10 points
	const FName ReclaimersResolve(TEXT("basic_arch_t4g"));		// -1% a reclaimed city a point, 10 points

	/** A run begun with these bonuses, its first day spent and its board emptied, as slice 3a's tests leave one. */
	UCataclysmEmpireRun* QuietRun(int32 Seed, const FCataclysmEmpireTreeBonuses& Bonuses = FCataclysmEmpireTreeBonuses())
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->SetTreeBonuses(Bonuses);
		Run->Begin(Seed);
		Run->AdvanceDay();
		Run->Dungeons.Empty();
		Run->Clock->Timers.Empty();
		return Run;
	}

	/** The standing cities of a tier, in id order. */
	TArray<FCataclysmCity*> AllOf(UCataclysmEmpireRun& Run, ECataclysmCityTier Tier)
	{
		TArray<FCataclysmCity*> Out;
		for (FCataclysmCity& City : Run.Map->Cities)
		{
			if (City.Tier == Tier && !City.bFallen)
			{
				Out.Add(&City);
			}
		}
		return Out;
	}

	/** The first standing city of a tier, or null. */
	FCataclysmCity* FirstOf(UCataclysmEmpireRun& Run, ECataclysmCityTier Tier)
	{
		const TArray<FCataclysmCity*> Cities = AllOf(Run, Tier);
		return Cities.Num() > 0 ? Cities[0] : nullptr;
	}

	/** Puts a dungeon on a city with a timer, as a surge would. */
	void Place(UCataclysmEmpireRun& Run, int32 DungeonId, ECataclysmDungeonType Type, ECataclysmDungeonSubType SubType,
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
		Dungeon.DefenceDamage = 100.0f;
		Dungeon.PopulationDamage = 400.0f;
		Run.Dungeons.Add(Dungeon);
		Run.Clock->AddDungeon(DungeonId, Dungeon.Floors);
		Run.Clock->SetResolveDays(DungeonId, ResolveDays);
	}

	/** An upgrade of one effect, bought on `BoughtDay`, as `UCataclysmCityUpgradeMapping::Make` fills one in. */
	FCataclysmCityUpgrade AnUpgrade(const TCHAR* RowName, ECataclysmCityUpgradeEffect Effect, float Value,
									int32 BoughtDay)
	{
		FCataclysmCityUpgrade Upgrade;
		Upgrade.RowName = FName(RowName);
		Upgrade.Effect = Effect;
		Upgrade.Value = Value;
		Upgrade.BoughtDay = BoughtDay;
		return Upgrade;
	}

	/** Global Vigilance and Reclaimer's Resolve, each at 10 points with their 15% caps. */
	FCataclysmEmpireTreeBonuses CountingBoth()
	{
		FCataclysmEmpireTreeBonuses Bonuses;
		Bonuses.ReductionPerClear = 0.01f;
		Bonuses.ClearPoints = 10;
		Bonuses.ClearReductionCap = 0.15f;
		Bonuses.ReductionPerReclaim = 0.01f;
		Bonuses.ReclaimPoints = 10;
		Bonuses.ReclaimReductionCap = 0.15f;
		return Bonuses;
	}

	/** A run file, read through the whole chain as a load reads it. */
	UCataclysmRunSave* ReadRunFixture(FAutomationTestBase& Test, const TCHAR* Name, FString& OutText)
	{
		FString Reason;
		if (!CataclysmSaveFixtures::Read(Name, OutText, Reason))
		{
			Test.AddError(Reason);
			return nullptr;
		}
		ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
		FString Message;
		UCataclysmRunSave* Read = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
			OutText, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, Message));
		if (Read == nullptr)
		{
			Test.AddError(FString::Printf(TEXT("%s would not load: %s -- %s"), Name,
										  FCataclysmSaveStorage::Describe(Result), *Message));
		}
		return Read;
	}

	/** Every city upgrade's bought day in a record, city by city. */
	TArray<int32> BoughtDaysIn(const UCataclysmRunSave& Record)
	{
		TArray<int32> Days;
		for (const FCataclysmCity& City : Record.Cities)
		{
			for (const FCataclysmCityUpgrade& Upgrade : City.Upgrades)
			{
				Days.Add(Upgrade.BoughtDay);
			}
		}
		return Days;
	}
}

// ---------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------

/**
 * The seven nodes add up through the generated tables into what the run reads: The Architect's Legacy 0.75; Civil Order
 * at 8 points 0.24; The Mason's Guild 0.20 and 0.15; Global Vigilance and Reclaimer's Resolve each 1% a point at 10
 * points with a 15% cap; Last Stand Protocols 1; Enduring Foundations at 10 points 0.2. CONTROL: a tree holding none
 * holds none of them.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeEventsTableTest,
	"Cataclysm.EmpireTreeEvents.TheSevenNodesAddUpFromTheirRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeEventsTableTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeEventsTest;

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const UDataTable* Effects = UCataclysmEmpireTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: DT_EmpireTreeNodes"), Nodes) || !TestNotNull(TEXT("set-up: DT_EmpireTreeEffects"), Effects))
	{
		return false;
	}

	FCataclysmPassiveAllocation Tree;
	Tree.Add(CivilOrder, 8);
	Tree.Add(MasonsGuild, 1);
	Tree.Add(ArchitectsLegacy, 1);
	Tree.Add(LastStandProtocols, 1);
	Tree.Add(EnduringFoundations, 10);
	Tree.Add(GlobalVigilance, 10);
	Tree.Add(ReclaimersResolve, 10);
	const FCataclysmEmpireTreeBonuses Held = UCataclysmEmpireTree::CityBonusesFor(Nodes, Effects, Tree);
	const FCataclysmEmpireTreeBonuses None = UCataclysmEmpireTree::CityBonusesFor(Nodes, Effects,
																				  FCataclysmPassiveAllocation());

	AddInfo(FString::Printf(TEXT("SEVEN: retake %.2f, Civil Order %.2f, Guild %.2f/%.2f, Vigilance %.2f x%d cap %.2f, "
								 "Reclaimer %.2f x%d cap %.2f, Last Stand %.0f, Enduring %.2f"),
							Held.RetakeFraction, Held.OutpostRegeneration, Held.LowestCityDefenceRestored,
							Held.LowestCityPopulationRestored, Held.ReductionPerClear, Held.ClearPoints,
							Held.ClearReductionCap, Held.ReductionPerReclaim, Held.ReclaimPoints, Held.ReclaimReductionCap,
							Held.SanctuarySurvivesAt, Held.UpgradeStrengthAfterDays));
	TestEqual(TEXT("The Architect's Legacy: 75%"), Held.RetakeFraction, 0.75f, 0.0001f);
	TestEqual(TEXT("Civil Order at 8 points: 24%"), Held.OutpostRegeneration, 0.24f, 0.0001f);
	TestEqual(TEXT("The Mason's Guild: 20% of maximum defence"), Held.LowestCityDefenceRestored, 0.20f, 0.0001f);
	TestEqual(TEXT("and 15% of maximum population"), Held.LowestCityPopulationRestored, 0.15f, 0.0001f);
	TestEqual(TEXT("Global Vigilance: 1% a clear a point"), Held.ReductionPerClear, 0.01f, 0.0001f);
	TestEqual(TEXT("at 10 points"), Held.ClearPoints, 10);
	TestEqual(TEXT("to 15%"), Held.ClearReductionCap, 0.15f, 0.0001f);
	TestEqual(TEXT("Reclaimer's Resolve: 1% a city a point"), Held.ReductionPerReclaim, 0.01f, 0.0001f);
	TestEqual(TEXT("at 10 points"), Held.ReclaimPoints, 10);
	TestEqual(TEXT("to 15%"), Held.ReclaimReductionCap, 0.15f, 0.0001f);
	TestEqual(TEXT("Last Stand Protocols: at 1"), Held.SanctuarySurvivesAt, 1.0f, 0.0001f);
	TestEqual(TEXT("Enduring Foundations at 10 points: 20%"), Held.UpgradeStrengthAfterDays, 0.2f, 0.0001f);
	TestTrue(TEXT("CONTROL: a tree holding none holds none of them"),
			 None.RetakeFraction == 0.0f && None.OutpostRegeneration == 0.0f && None.LowestCityDefenceRestored == 0.0f
			 && None.ClearPoints == 0 && None.ReclaimPoints == 0 && None.SanctuarySurvivesAt == 0.0f
			 && None.UpgradeStrengthAfterDays == 0.0f);
	return true;
}

// ---------------------------------------------------------------------------
// The Architect's Legacy
// ---------------------------------------------------------------------------

/**
 * A retaken Outpost comes back at 75% of its maximum defence and population with The Architect's Legacy, and at half
 * without it: two runs on one seed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeEventsLegacyTest,
	"Cataclysm.EmpireTreeEvents.TheArchitectsLegacyRetakesACityAtThreeQuarters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeEventsLegacyTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeEventsTest;

	FCataclysmEmpireTreeBonuses Legacy;
	Legacy.RetakeFraction = 0.75f;
	UCataclysmEmpireRun* With = QuietRun(2741, Legacy);
	UCataclysmEmpireRun* Without = QuietRun(2741);

	float Defence[2] = { -1.0f, -1.0f };
	float Population[2] = { -1.0f, -1.0f };
	float MaxPopulation = 0.0f;
	UCataclysmEmpireRun* Runs[2] = { With, Without };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FCataclysmCity* Outpost = FirstOf(*Runs[Index], ECataclysmCityTier::Outpost);
		if (!TestNotNull(TEXT("set-up: an Outpost"), Outpost))
		{
			return false;
		}
		const int32 Id = Outpost->CityId;
		Runs[Index]->Map->Fall(Id);
		Runs[Index]->Map->Retake(Id);
		const FCataclysmCity* Back = Runs[Index]->Map->Find(Id);
		Defence[Index] = Back->Defence;
		Population[Index] = Back->Population;
		MaxPopulation = Runs[Index]->Map->EffectiveMaxPopulation(*Back);
	}

	AddInfo(FString::Printf(TEXT("LEGACY: retaken at %.0f defence and %.0f people with it, %.0f and %.0f without"),
							Defence[0], Population[0], Defence[1], Population[1]));
	TestEqual(TEXT("with it, 75% of 1000 defence"), Defence[0], 750.0f, 0.01f);
	TestEqual(TEXT("and 75% of its people"), Population[0], 0.75f * MaxPopulation, 0.5f);
	TestEqual(TEXT("CONTROL: without it, half"), Defence[1], 500.0f, 0.01f);
	TestEqual(TEXT("CONTROL: and half its people"), Population[1], 0.5f * MaxPopulation, 0.5f);
	return true;
}

// ---------------------------------------------------------------------------
// Civil Order and The Mason's Guild
// ---------------------------------------------------------------------------

/**
 * Civil Order at 8 points: on run day 30 every living Outpost regains 24% of what it holds, to its maximum, and on day
 * 29 nothing. An Outpost at 500 holds 620, one at 900 is full at 1000, a Bulwark and a fallen Outpost are untouched; on
 * day 60 the first holds 768.8.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeEventsCivilOrderTest,
	"Cataclysm.EmpireTreeEvents.CivilOrderRegainsAShareOfWhatEachOutpostHoldsEveryThirtyDays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeEventsCivilOrderTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeEventsTest;

	FCataclysmEmpireTreeBonuses Order;
	Order.OutpostRegeneration = 0.24f;
	UCataclysmEmpireRun* Run = QuietRun(2741, Order);
	const TArray<FCataclysmCity*> Outposts = AllOf(*Run, ECataclysmCityTier::Outpost);
	FCataclysmCity* Bulwark = FirstOf(*Run, ECataclysmCityTier::Bulwark);
	if (!TestTrue(TEXT("set-up: three Outposts and a Bulwark"), Outposts.Num() >= 3 && Bulwark != nullptr))
	{
		return false;
	}
	const int32 Low = Outposts[0]->CityId;
	const int32 High = Outposts[1]->CityId;
	const int32 Fallen = Outposts[2]->CityId;
	Outposts[0]->Defence = 500.0f;
	Outposts[1]->Defence = 900.0f;
	Bulwark->Defence = 500.0f;
	Run->Map->Fall(Fallen);

	Run->RunTreeIntervals(29);
	const float OnDay29 = Run->Map->Find(Low)->Defence;
	Run->RunTreeIntervals(30);
	const float OnDay30 = Run->Map->Find(Low)->Defence;
	const float FullOnDay30 = Run->Map->Find(High)->Defence;
	Run->RunTreeIntervals(60);
	const float OnDay60 = Run->Map->Find(Low)->Defence;

	AddInfo(FString::Printf(TEXT("CIVIL ORDER: 500 holds %.1f on day 29, %.1f on day 30, %.1f on day 60; 900 holds %.1f; "
								 "a Bulwark %.1f; the fallen Outpost %.1f"),
							OnDay29, OnDay30, OnDay60, FullOnDay30, Bulwark->Defence, Run->Map->Find(Fallen)->Defence));
	TestEqual(TEXT("nothing on day 29"), OnDay29, 500.0f, 0.01f);
	TestEqual(TEXT("24% of 500 on day 30"), OnDay30, 620.0f, 0.01f);
	TestEqual(TEXT("24% of 620 on day 60"), OnDay60, 768.8f, 0.01f);
	TestEqual(TEXT("one at 900 is held at its maximum"), FullOnDay30, 1000.0f, 0.01f);
	TestEqual(TEXT("a Bulwark is not an Outpost"), Bulwark->Defence, 500.0f, 0.01f);
	TestEqual(TEXT("a fallen Outpost regains nothing"), Run->Map->Find(Fallen)->Defence, 0.0f, 0.01f);
	return true;
}

/**
 * The Mason's Guild on run day 20: the living Outpost or Bulwark with the lowest share of its maximum defence regains
 * 20% of its maximum defence and 15% of its maximum population; nothing on day 19; a Sanctuary lower still is not
 * reached; on a tie the lower id; and when every one is full nothing changes.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeEventsGuildTest,
	"Cataclysm.EmpireTreeEvents.TheMasonsGuildRestoresTheMostDamagedOutpostOrBulwarkEveryTwentyDays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeEventsGuildTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeEventsTest;

	FCataclysmEmpireTreeBonuses Guild;
	Guild.LowestCityDefenceRestored = 0.20f;
	Guild.LowestCityPopulationRestored = 0.15f;
	UCataclysmEmpireRun* Run = QuietRun(2741, Guild);
	const TArray<FCataclysmCity*> Outposts = AllOf(*Run, ECataclysmCityTier::Outpost);
	const TArray<FCataclysmCity*> Bulwarks = AllOf(*Run, ECataclysmCityTier::Bulwark);
	FCataclysmCity* Sanctuary = FirstOf(*Run, ECataclysmCityTier::Sanctuary);
	if (!TestTrue(TEXT("set-up: two Outposts, two Bulwarks and a Sanctuary"),
				  Outposts.Num() >= 2 && Bulwarks.Num() >= 2 && Sanctuary != nullptr))
	{
		return false;
	}
	UCataclysmEmpireMap& Map = *Run->Map;
	FCataclysmCity& Outpost = *Outposts[0];
	FCataclysmCity& Bulwark = *Bulwarks[0];
	const float BulwarkMax = Map.EffectiveMaxDefence(Bulwark);
	const float BulwarkPeople = Map.EffectiveMaxPopulation(Bulwark);
	Outpost.Defence = 0.5f * Map.EffectiveMaxDefence(Outpost);
	Bulwark.Defence = 0.3f * BulwarkMax;
	Bulwark.Population = 0.5f * BulwarkPeople;
	Sanctuary->Defence = 0.1f * Map.EffectiveMaxDefence(*Sanctuary);

	Run->RunTreeIntervals(19);
	const float OnDay19 = Map.DefenceFractionOf(Bulwark);
	Run->RunTreeIntervals(20);
	const float BulwarkDefence = Map.DefenceFractionOf(Bulwark);
	const float BulwarkPopulation = Map.PopulationOf(Bulwark) / BulwarkPeople;
	const float OutpostAfter = Map.DefenceFractionOf(Outpost);
	const float SanctuaryAfter = Map.DefenceFractionOf(*Sanctuary);

	// A TIE, AT HALF EACH: the lower id is chosen. HALF, because half of a maximum over that maximum is exactly 0.5 in
	// floating point, so the two shares are equal and not two roundings of 0.4.
	Bulwark.Defence = BulwarkMax;
	FCataclysmCity& Second = *Bulwarks[1];
	Outpost.Defence = 0.5f * Map.EffectiveMaxDefence(Outpost);
	Second.Defence = 0.5f * Map.EffectiveMaxDefence(Second);
	FCataclysmCity& Lower = Outpost.CityId < Second.CityId ? Outpost : Second;
	FCataclysmCity& Higher = Outpost.CityId < Second.CityId ? Second : Outpost;
	Run->RunTreeIntervals(40);
	const float LowerAfter = Map.DefenceFractionOf(Lower);
	const float HigherAfter = Map.DefenceFractionOf(Higher);

	// EVERY ONE FULL: nothing changes.
	for (FCataclysmCity& City : Map.Cities)
	{
		City.Defence = Map.EffectiveMaxDefence(City);
		City.Population = Map.EffectiveMaxPopulation(City);
	}
	Bulwark.Population = 0.5f * BulwarkPeople;
	Run->RunTreeIntervals(60);
	const float PopulationWhenFull = Map.PopulationOf(Bulwark) / BulwarkPeople;

	AddInfo(FString::Printf(TEXT("GUILD: day 19 the Bulwark %.2f; day 20 it is %.2f defence and %.2f people, the Outpost "
								 "%.2f, the Sanctuary %.2f; a tie at 0.50 goes to city %d (%.2f) over %d (%.2f); every "
								 "city full leaves the Bulwark's people at %.2f"),
							OnDay19, BulwarkDefence, BulwarkPopulation, OutpostAfter, SanctuaryAfter, Lower.CityId,
							LowerAfter, Higher.CityId, HigherAfter, PopulationWhenFull));
	TestEqual(TEXT("nothing on day 19"), OnDay19, 0.3f, 0.0001f);
	TestEqual(TEXT("day 20: the Bulwark, the lowest share, regains 20% of its maximum defence"), BulwarkDefence, 0.5f,
			  0.0001f);
	TestEqual(TEXT("and 15% of its maximum population"), BulwarkPopulation, 0.65f, 0.0001f);
	TestEqual(TEXT("the Outpost, higher, regains nothing"), OutpostAfter, 0.5f, 0.0001f);
	TestEqual(TEXT("the Sanctuary, lower still, is not reached"), SanctuaryAfter, 0.1f, 0.0001f);
	TestEqual(TEXT("a tie goes to the lower id"), LowerAfter, 0.7f, 0.0001f);
	TestEqual(TEXT("and not to the higher"), HigherAfter, 0.5f, 0.0001f);
	TestEqual(TEXT("every city full: nothing changes, its people included"), PopulationWhenFull, 0.5f, 0.0001f);
	return true;
}

/**
 * The day loop runs the two repairs: a day spent from day 29 to day 30 gives a held Outpost Civil Order's share, as
 * `RunTreeIntervals` does when called by hand. A dungeon standing on a Bulwark with a long timer keeps the board from
 * being empty, so no surge lands.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeEventsDayLoopTest,
	"Cataclysm.EmpireTreeEvents.ADaySpentRunsTheTreesRepairs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeEventsDayLoopTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeEventsTest;

	FCataclysmEmpireTreeBonuses Order;
	Order.OutpostRegeneration = 0.24f;
	UCataclysmEmpireRun* Run = QuietRun(2741, Order);
	FCataclysmCity* Outpost = FirstOf(*Run, ECataclysmCityTier::Outpost);
	const FCataclysmCity* Bulwark = FirstOf(*Run, ECataclysmCityTier::Bulwark);
	if (!TestTrue(TEXT("set-up: an Outpost and a Bulwark"), Outpost != nullptr && Bulwark != nullptr))
	{
		return false;
	}
	Place(*Run, 9100, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, *Bulwark, 500.0f);
	Outpost->Defence = 500.0f;
	const int32 OutpostId = Outpost->CityId;
	Run->Clock->Day = 29;

	const FCataclysmDayReport Report = Run->AdvanceDay();
	const float After = Run->Map->Find(OutpostId)->Defence;

	AddInfo(FString::Printf(TEXT("DAY LOOP: day %d, surged %s; the Outpost at 500 holds %.1f"), Report.Day,
							Report.bSurged ? TEXT("yes") : TEXT("no"), After));
	TestEqual(TEXT("set-up: the day spent is day 30"), Report.Day, 30);
	TestFalse(TEXT("set-up: no surge landed"), Report.bSurged);
	TestEqual(TEXT("the day loop gave Civil Order's 24%"), After, 620.0f, 0.01f);
	return true;
}

// ---------------------------------------------------------------------------
// Global Vigilance and Reclaimer's Resolve
// ---------------------------------------------------------------------------

/**
 * Global Vigilance at 10 points: no clear, the whole hit; one clear, 0.99^10 of it; two, 0.99^20, held at the cap, 0.85;
 * a surge, the whole hit again. Every kind of clear counts, and the factor is on defence and population alike. CONTROL:
 * at 1 point one clear is 0.99.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeEventsVigilanceTest,
	"Cataclysm.EmpireTreeEvents.GlobalVigilanceCountsTheClearsSinceTheLastSurge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeEventsVigilanceTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeEventsTest;

	FCataclysmEmpireTreeBonuses Vigilance;
	Vigilance.ReductionPerClear = 0.01f;
	Vigilance.ClearPoints = 10;
	Vigilance.ClearReductionCap = 0.15f;
	UCataclysmEmpireRun* Run = QuietRun(2741, Vigilance);
	const FCataclysmCity* Outpost = FirstOf(*Run, ECataclysmCityTier::Outpost);
	const FCataclysmCity* Bulwark = FirstOf(*Run, ECataclysmCityTier::Bulwark);
	if (!TestTrue(TEXT("set-up: an Outpost and a Bulwark"), Outpost != nullptr && Bulwark != nullptr))
	{
		return false;
	}
	const int32 Target = Outpost->CityId;

	const FCataclysmTreeDamageFactors None = Run->TreeDamageFactorsFor(Target, ECataclysmType::None);
	Place(*Run, 9200, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, *Bulwark, 50.0f);
	Run->ClearDungeon(9200);
	const FCataclysmTreeDamageFactors One = Run->TreeDamageFactorsFor(Target, ECataclysmType::None);
	Place(*Run, 9201, ECataclysmDungeonType::Quest, ECataclysmDungeonSubType::None, *Bulwark, 50.0f);
	Run->ClearDungeon(9201);
	const int32 Counted = Run->ClearsSinceLastSurge;
	const FCataclysmTreeDamageFactors Two = Run->TreeDamageFactorsFor(Target, ECataclysmType::None);

	Run->Surges->NextSurgeDay = static_cast<float>(Run->Day() + 1);
	const FCataclysmDayReport Surged = Run->AdvanceDay();
	const FCataclysmTreeDamageFactors AfterTheSurge = Run->TreeDamageFactorsFor(Target, ECataclysmType::None);

	FCataclysmEmpireTreeBonuses OnePoint = Vigilance;
	OnePoint.ClearPoints = 1;
	UCataclysmEmpireRun* Control = QuietRun(2741, OnePoint);
	Place(*Control, 9202, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None,
		  *FirstOf(*Control, ECataclysmCityTier::Bulwark), 50.0f);
	Control->ClearDungeon(9202);
	const float OneAtOnePoint = Control->TreeDamageFactorsFor(Target, ECataclysmType::None).Defence;

	AddInfo(FString::Printf(TEXT("VIGILANCE: %.4f with no clear, %.4f after one, %.4f after two (%d counted; people "
								 "%.4f); a surge %s, then %.4f with %d counted; one clear at 1 point %.4f"),
							None.Defence, One.Defence, Two.Defence, Counted, Two.Population,
							Surged.bSurged ? TEXT("landed") : TEXT("DID NOT LAND"), AfterTheSurge.Defence,
							Run->ClearsSinceLastSurge, OneAtOnePoint));
	TestEqual(TEXT("no clear: the whole hit"), None.Defence, 1.0f, 0.0001f);
	TestEqual(TEXT("one clear at 10 points: 0.99^10"), One.Defence, FMath::Pow(0.99f, 10.0f), 0.0001f);
	TestEqual(TEXT("an ordinary and a Quest clear both count"), Counted, 2);
	TestEqual(TEXT("two: 0.99^20 is held at the cap, 0.85"), Two.Defence, 0.85f, 0.0001f);
	TestEqual(TEXT("and people alike"), Two.Population, 0.85f, 0.0001f);
	TestTrue(TEXT("set-up: a surge landed"), Surged.bSurged);
	TestEqual(TEXT("a surge resets the count"), Run->ClearsSinceLastSurge, 0);
	TestEqual(TEXT("and the whole hit is taken again"), AfterTheSurge.Defence, 1.0f, 0.0001f);
	TestEqual(TEXT("CONTROL: at 1 point one clear is 0.99"), OneAtOnePoint, 0.99f, 0.0001f);
	return true;
}

/**
 * Reclaimer's Resolve at 10 points: clearing a fallen Outpost's Dungeon City reclaims it and every city then takes
 * 0.99^10 of a hit, and a surge does not reset it. An erased city's Dungeon City reclaims nothing, and an ordinary clear
 * is not a reclaim. Five cities reclaimed are held at the 15% cap. With one clear since the last surge as well, Global
 * Vigilance's factor multiplies it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeEventsReclaimerTest,
	"Cataclysm.EmpireTreeEvents.ReclaimersResolveCountsTheCitiesReclaimedForTheRestOfTheRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeEventsReclaimerTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeEventsTest;

	FCataclysmEmpireTreeBonuses Reclaimer;
	Reclaimer.ReductionPerReclaim = 0.01f;
	Reclaimer.ReclaimPoints = 10;
	Reclaimer.ReclaimReductionCap = 0.15f;
	UCataclysmEmpireRun* Run = QuietRun(2741, Reclaimer);
	const TArray<FCataclysmCity*> Outposts = AllOf(*Run, ECataclysmCityTier::Outpost);
	const FCataclysmCity* Bulwark = FirstOf(*Run, ECataclysmCityTier::Bulwark);
	if (!TestTrue(TEXT("set-up: three Outposts and a Bulwark"), Outposts.Num() >= 3 && Bulwark != nullptr))
	{
		return false;
	}
	const int32 Target = Bulwark->CityId;
	const int32 Lost = Outposts[0]->CityId;
	const int32 Erased = Outposts[1]->CityId;

	Run->Map->Fall(Lost);
	Place(*Run, 9300, ECataclysmDungeonType::FallenCity, ECataclysmDungeonSubType::None, *Run->Map->Find(Lost), 50.0f);
	Run->ClearDungeon(9300);
	const bool bBack = !Run->Map->Find(Lost)->bFallen;
	const int32 AfterOne = Run->CitiesReclaimed;
	const float One = Run->TreeDamageFactorsFor(Target, ECataclysmType::None).Defence;

	Run->Map->Fall(Erased);
	Run->Map->FindMutable(Erased)->bErased = true;
	Place(*Run, 9301, ECataclysmDungeonType::FallenCity, ECataclysmDungeonSubType::None, *Run->Map->Find(Erased), 50.0f);
	Run->ClearDungeon(9301);
	Place(*Run, 9302, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, *Bulwark, 50.0f);
	Run->ClearDungeon(9302);
	const int32 AfterTheOthers = Run->CitiesReclaimed;

	Run->Surges->NextSurgeDay = static_cast<float>(Run->Day() + 1);
	const FCataclysmDayReport Surged = Run->AdvanceDay();
	const float AfterTheSurge = Run->TreeDamageFactorsFor(Target, ECataclysmType::None).Defence;

	FCataclysmEmpireTreeBonuses Both = CountingBoth();
	UCataclysmEmpireRun* Together = QuietRun(2741, Both);
	Together->ClearsSinceLastSurge = 1;
	Together->CitiesReclaimed = 1;
	const float BothOnce = Together->TreeDamageFactorsFor(Target, ECataclysmType::None).Defence;
	Together->ClearsSinceLastSurge = 0;
	Together->CitiesReclaimed = 5;
	const float FiveReclaimed = Together->TreeDamageFactorsFor(Target, ECataclysmType::None).Defence;

	AddInfo(FString::Printf(TEXT("RECLAIMER: retaken %s, %d reclaimed, %.4f; then %d after an erased city's and an "
								 "ordinary clear; a surge %s, %.4f; with one clear as well %.4f; five reclaimed %.4f"),
							bBack ? TEXT("yes") : TEXT("NO"), AfterOne, One, AfterTheOthers,
							Surged.bSurged ? TEXT("landed") : TEXT("DID NOT LAND"), AfterTheSurge, BothOnce,
							FiveReclaimed));
	TestTrue(TEXT("set-up: the Outpost was retaken"), bBack);
	TestEqual(TEXT("one city reclaimed"), AfterOne, 1);
	TestEqual(TEXT("every city takes 0.99^10"), One, FMath::Pow(0.99f, 10.0f), 0.0001f);
	TestEqual(TEXT("an erased city's and an ordinary clear reclaim nothing"), AfterTheOthers, 1);
	TestTrue(TEXT("set-up: a surge landed"), Surged.bSurged);
	TestEqual(TEXT("a surge does not reset it"), AfterTheSurge, FMath::Pow(0.99f, 10.0f), 0.0001f);
	TestEqual(TEXT("with Global Vigilance's one clear, the two multiply"), BothOnce, FMath::Pow(0.99f, 20.0f), 0.0001f);
	TestEqual(TEXT("five reclaimed are held at the cap, 0.85"), FiveReclaimed, 0.85f, 0.0001f);
	return true;
}

// ---------------------------------------------------------------------------
// Last Stand Protocols
// ---------------------------------------------------------------------------

/**
 * Last Stand Protocols: a Sanctuary at 10 defence, hit by an ordinary dungeon resolving, is left at 1 and stands, and
 * the run notes it spent; a second such hit fells it. CONTROL: without the node the first fells it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeEventsLastStandTest,
	"Cataclysm.EmpireTreeEvents.LastStandProtocolsHoldsTheFirstFatalResolveOnASanctuary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeEventsLastStandTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeEventsTest;

	FCataclysmEmpireTreeBonuses LastStand;
	LastStand.SanctuarySurvivesAt = 1.0f;
	UCataclysmEmpireRun* Run = QuietRun(2741, LastStand);
	UCataclysmEmpireRun* Without = QuietRun(2741);
	FCataclysmCity* Sanctuary = FirstOf(*Run, ECataclysmCityTier::Sanctuary);
	FCataclysmCity* Bare = FirstOf(*Without, ECataclysmCityTier::Sanctuary);
	if (!TestTrue(TEXT("set-up: a Sanctuary in each run"), Sanctuary != nullptr && Bare != nullptr))
	{
		return false;
	}
	const int32 Id = Sanctuary->CityId;
	Sanctuary->Defence = 10.0f;
	Bare->Defence = 10.0f;

	Place(*Run, 9400, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, *Sanctuary, 1.0f);
	Run->AdvanceDay();
	const FCataclysmCity* AfterFirst = Run->Map->Find(Id);
	const bool bStoodFirst = AfterFirst != nullptr && !AfterFirst->bFallen;
	const float HeldAt = AfterFirst != nullptr ? AfterFirst->Defence : -1.0f;
	const bool bSpent = Run->bSanctuaryLastStandSpent;

	Place(*Run, 9401, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, *Run->Map->Find(Id), 1.0f);
	Run->AdvanceDay();
	const bool bFellSecond = Run->Map->Find(Id)->bFallen;

	Place(*Without, 9402, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, *Bare, 1.0f);
	Without->AdvanceDay();
	const bool bBareFell = Without->Map->Find(Bare->CityId)->bFallen;

	AddInfo(FString::Printf(TEXT("LAST STAND: the first fatal resolve leaves it %s at %.1f, spent %s; the second %s; "
								 "without the node the first %s"),
							bStoodFirst ? TEXT("standing") : TEXT("FALLEN"), HeldAt, bSpent ? TEXT("yes") : TEXT("no"),
							bFellSecond ? TEXT("fells it") : TEXT("DOES NOT"),
							bBareFell ? TEXT("fells it") : TEXT("DOES NOT")));
	TestTrue(TEXT("the first fatal resolve leaves the Sanctuary standing"), bStoodFirst);
	TestEqual(TEXT("at 1"), HeldAt, 1.0f, 0.0001f);
	TestTrue(TEXT("and the run notes it spent"), bSpent);
	TestTrue(TEXT("once per run: the second fells it"), bFellSecond);
	TestTrue(TEXT("CONTROL: without the node the first fells it"), bBareFell);
	return true;
}

/**
 * Last Stand Protocols holds a Quest dungeon's timer hit, which is a dungeon's resolution; and does not hold a Siege's
 * daily share, which is not, or a fatal resolve on an Outpost, which is not a Sanctuary. Neither of the last two spends
 * it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeEventsLastStandKindsTest,
	"Cataclysm.EmpireTreeEvents.TheLastStandHoldsAQuestTimerHitAndNotASiegeOrAnOutpost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeEventsLastStandKindsTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeEventsTest;

	FCataclysmEmpireTreeBonuses LastStand;
	LastStand.SanctuarySurvivesAt = 1.0f;

	// A QUEST DUNGEON'S TIMER.
	UCataclysmEmpireRun* Quest = QuietRun(2741, LastStand);
	FCataclysmCity* QuestHost = FirstOf(*Quest, ECataclysmCityTier::Sanctuary);
	// A SIEGE'S DAILY SHARE.
	UCataclysmEmpireRun* Siege = QuietRun(2741, LastStand);
	FCataclysmCity* SiegeHost = FirstOf(*Siege, ECataclysmCityTier::Sanctuary);
	// AN OUTPOST.
	UCataclysmEmpireRun* Outpost = QuietRun(2741, LastStand);
	FCataclysmCity* OutpostHost = FirstOf(*Outpost, ECataclysmCityTier::Outpost);
	if (!TestTrue(TEXT("set-up: the three cities"), QuestHost != nullptr && SiegeHost != nullptr && OutpostHost != nullptr))
	{
		return false;
	}
	const int32 QuestId = QuestHost->CityId;
	const int32 SiegeId = SiegeHost->CityId;
	const int32 OutpostId = OutpostHost->CityId;
	QuestHost->Defence = 10.0f;
	SiegeHost->Defence = 10.0f;
	OutpostHost->Defence = 10.0f;

	Place(*Quest, 9500, ECataclysmDungeonType::Quest, ECataclysmDungeonSubType::None, *QuestHost, 1.0f);
	Quest->AdvanceDay();
	Place(*Siege, 9501, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::Siege, *SiegeHost, 10000.0f);
	Siege->AdvanceDay();
	Place(*Outpost, 9502, ECataclysmDungeonType::Basic, ECataclysmDungeonSubType::None, *OutpostHost, 1.0f);
	Outpost->AdvanceDay();

	const FCataclysmCity* QuestAfter = Quest->Map->Find(QuestId);
	AddInfo(FString::Printf(TEXT("LAST STAND KINDS: a Quest timer leaves the Sanctuary %s at %.1f, spent %s; a Siege %s it, "
								 "spent %s; an Outpost's resolve %s it, spent %s"),
							QuestAfter->bFallen ? TEXT("FALLEN") : TEXT("standing"), QuestAfter->Defence,
							Quest->bSanctuaryLastStandSpent ? TEXT("yes") : TEXT("no"),
							Siege->Map->Find(SiegeId)->bFallen ? TEXT("fells") : TEXT("DOES NOT FELL"),
							Siege->bSanctuaryLastStandSpent ? TEXT("YES") : TEXT("no"),
							Outpost->Map->Find(OutpostId)->bFallen ? TEXT("fells") : TEXT("DOES NOT FELL"),
							Outpost->bSanctuaryLastStandSpent ? TEXT("YES") : TEXT("no")));
	TestFalse(TEXT("a Quest dungeon's timer hit is held"), QuestAfter->bFallen);
	TestEqual(TEXT("at 1"), QuestAfter->Defence, 1.0f, 0.0001f);
	TestTrue(TEXT("and spends it"), Quest->bSanctuaryLastStandSpent);
	TestTrue(TEXT("a Siege's daily share still fells the Sanctuary"), Siege->Map->Find(SiegeId)->bFallen);
	TestFalse(TEXT("and does not spend it"), Siege->bSanctuaryLastStandSpent);
	TestTrue(TEXT("an Outpost's fatal resolve still fells it"), Outpost->Map->Find(OutpostId)->bFallen);
	TestFalse(TEXT("and does not spend it"), Outpost->bSanctuaryLastStandSpent);
	return true;
}

// ---------------------------------------------------------------------------
// Enduring Foundations
// ---------------------------------------------------------------------------

/**
 * Enduring Foundations at 10 points, a Bulwark holding three upgrades bought on day 1: a 25% resistance, a 20% raise to
 * maximum defence and a dungeon cap. On day 10 each is as bought; on day 11, held 10 days, the resistance is 30% and a
 * hit of 100 takes 70, and the maximum is the base's x1.24 and not x1.2 while what the city holds stays where it was
 * (S1). The dungeon cap, a whole number, is not reached.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeEventsEnduringTest,
	"Cataclysm.EmpireTreeEvents.EnduringFoundationsStrengthensAnUpgradeHeldTenDays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeEventsEnduringTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeEventsTest;

	FCataclysmEmpireTreeBonuses Enduring;
	Enduring.UpgradeStrengthAfterDays = 0.2f;
	UCataclysmEmpireRun* Run = QuietRun(2741, Enduring);
	FCataclysmCity* Bulwark = FirstOf(*Run, ECataclysmCityTier::Bulwark);
	if (!TestNotNull(TEXT("set-up: a Bulwark"), Bulwark))
	{
		return false;
	}
	const int32 Id = Bulwark->CityId;
	UCataclysmEmpireMap& Map = *Run->Map;
	const float Base = Map.EffectiveMaxDefence(*Bulwark);
	const int32 Bought = Run->Day();
	const bool bAdded = Map.AddUpgrade(Id, AnUpgrade(TEXT("Test_Resist"), ECataclysmCityUpgradeEffect::ResistDefenceLoss,
													 0.25f, Bought))
		&& Map.AddUpgrade(Id, AnUpgrade(TEXT("Test_Walls"), ECataclysmCityUpgradeEffect::MaxDefence, 0.2f, Bought))
		&& Map.AddUpgrade(Id, AnUpgrade(TEXT("Test_Cap"), ECataclysmCityUpgradeEffect::DungeonCap, 2.0f, Bought));
	if (!TestTrue(TEXT("set-up: the three upgrades were added"), bAdded))
	{
		return false;
	}
	const FCataclysmCity& City = *Map.Find(Id);

	Run->Clock->Day = Bought + 9;
	const float ResistOnDay9 = Map.UpgradeValueNowFor(City, ECataclysmCityUpgradeEffect::ResistDefenceLoss);
	const float MaxOnDay9 = Map.EffectiveMaxDefence(City);
	const float HeldOnDay9 = Map.DefenceOf(City);

	Run->Clock->Day = Bought + 10;
	const float ResistOnDay10 = Map.UpgradeValueNowFor(City, ECataclysmCityUpgradeEffect::ResistDefenceLoss);
	const float MaxOnDay10 = Map.EffectiveMaxDefence(City);
	const float HeldOnDay10 = Map.DefenceOf(City);
	const float CapOnDay10 = Map.UpgradeValueNowFor(City, ECataclysmCityUpgradeEffect::DungeonCap);
	Map.Damage(Id, 100.0f, 0.0f);
	const float AfterAHit = Map.DefenceOf(*Map.Find(Id));

	AddInfo(FString::Printf(TEXT("ENDURING: held 9 days, resist %.4f, maximum %.1f (base %.1f), holding %.1f; held 10, "
								 "resist %.4f, maximum %.1f, holding %.1f, cap %.1f; a hit of 100 leaves %.1f"),
							ResistOnDay9, MaxOnDay9, Base, HeldOnDay9, ResistOnDay10, MaxOnDay10, HeldOnDay10,
							CapOnDay10, AfterAHit));
	TestEqual(TEXT("held 9 days: the resistance as bought"), ResistOnDay9, 0.25f, 0.0001f);
	TestEqual(TEXT("and the maximum the base's x1.2"), MaxOnDay9, Base * 1.2f, 0.5f);
	TestEqual(TEXT("held 10 days: the resistance is 30%"), ResistOnDay10, 0.30f, 0.0001f);
	TestEqual(TEXT("the maximum is the base's x1.24"), MaxOnDay10, Base * 1.24f, 0.5f);
	TestEqual(TEXT("and what the city holds stays where it was (S1)"), HeldOnDay10, HeldOnDay9, 0.01f);
	TestEqual(TEXT("a hit of 100 takes 70"), AfterAHit, HeldOnDay10 - 70.0f, 0.01f);
	TestEqual(TEXT("the dungeon cap, a whole number, is as bought"), CapOnDay10, 2.0f, 0.0001f);
	return true;
}

/**
 * A city upgrade bought through the run records the day it was bought: the clock's day, 37.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeEventsBoughtDayTest,
	"Cataclysm.EmpireTreeEvents.ABoughtUpgradeRecordsTheDayItWasBought",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeEventsBoughtDayTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeEventsTest;

	UCataclysmEmpireRun* Run = QuietRun(2741);
	const FCataclysmCity* Bulwark = FirstOf(*Run, ECataclysmCityTier::Bulwark);
	if (!TestNotNull(TEXT("set-up: a Bulwark"), Bulwark))
	{
		return false;
	}
	const int32 Id = Bulwark->CityId;
	Run->Clock->Day = 37;
	const ECataclysmCityUpgradeResult Result = Run->BuyCityUpgrade(
		Id, AnUpgrade(TEXT("Test_Resist"), ECataclysmCityUpgradeEffect::ResistDefenceLoss, 0.25f, 0), 0, 0);
	const FCataclysmCity* After = Run->Map->Find(Id);
	const int32 Day = After != nullptr && After->Upgrades.Num() > 0 ? After->Upgrades.Last().BoughtDay : -1;

	AddInfo(FString::Printf(TEXT("BOUGHT DAY: result %d, recorded day %d"), static_cast<int32>(Result), Day));
	TestEqual(TEXT("set-up: it was bought"), static_cast<int32>(Result),
			  static_cast<int32>(ECataclysmCityUpgradeResult::Bought));
	TestEqual(TEXT("on day 37"), Day, 37);
	return true;
}

// ---------------------------------------------------------------------------
// The run record, version 9
// ---------------------------------------------------------------------------

/**
 * A saved run keeps what the tree's event rules count: 3 clears since the last surge, 2 cities reclaimed, the Last
 * Stand spent, and an upgrade bought on day 7, written out and read back into a fresh run.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeEventsSavedTest,
	"Cataclysm.EmpireTreeEvents.ASavedRunKeepsItsEventCountsAndBoughtDays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeEventsSavedTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeEventsTest;

	UCataclysmEmpireRun* Run = QuietRun(2741);
	const FCataclysmCity* Bulwark = FirstOf(*Run, ECataclysmCityTier::Bulwark);
	if (!TestNotNull(TEXT("set-up: a Bulwark"), Bulwark))
	{
		return false;
	}
	const int32 Id = Bulwark->CityId;
	Run->ClearsSinceLastSurge = 3;
	Run->CitiesReclaimed = 2;
	Run->bSanctuaryLastStandSpent = true;
	Run->Map->AddUpgrade(Id, AnUpgrade(TEXT("Test_Resist"), ECataclysmCityUpgradeEffect::ResistDefenceLoss, 0.25f, 7));

	UCataclysmRunSave* Record = NewObject<UCataclysmRunSave>();
	Record->SchemaVersion = UCataclysmRunSave::SchemaVersionNow;
	FString Why;
	FCataclysmSaveGather::RunClockFrom(*Run, *Record);
	FCataclysmSaveGather::CitiesFrom(*Run, *Record);
	FCataclysmSaveGather::SurgeScheduleFrom(*Run, *Record);
	FCataclysmSaveGather::ProgressFrom(*Run, *Record);
	FCataclysmSaveGather::IdentityFrom(*Run, *Record);
	FString Json;
	const bool bWritten = FCataclysmSaveGather::DungeonsFrom(*Run, *Record, Why)
		&& FCataclysmSaveStorage::ToJson(Record, Json, Why);
	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	UCataclysmRunSave* Read = bWritten ? Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
		Json, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, Why)) : nullptr;
	if (!TestNotNull(TEXT("set-up: the record was written and read back"), Read))
	{
		AddError(Why);
		return false;
	}
	UCataclysmEmpireRun* Loaded = NewObject<UCataclysmEmpireRun>();
	const bool bLoaded = FCataclysmSaveApply::RunInto(*Read, *Loaded, Why);
	const FCataclysmCity* LoadedCity = bLoaded ? Loaded->Map->Find(Id) : nullptr;
	const int32 Day = LoadedCity != nullptr && LoadedCity->Upgrades.Num() > 0 ? LoadedCity->Upgrades.Last().BoughtDay : -1;

	AddInfo(FString::Printf(TEXT("SAVED: the file holds \"ClearsSinceLastSurge\": %s; loaded %s with %d clears, %d "
								 "reclaimed, spent %s, the upgrade bought on day %d"),
							Json.Contains(TEXT("\"ClearsSinceLastSurge\": 3")) ? TEXT("3") : TEXT("NOT 3"),
							bLoaded ? TEXT("yes") : *Why, Loaded->ClearsSinceLastSurge, Loaded->CitiesReclaimed,
							Loaded->bSanctuaryLastStandSpent ? TEXT("yes") : TEXT("no"), Day));
	TestTrue(TEXT("set-up: it loaded"), bLoaded);
	TestTrue(TEXT("the file holds the count"), Json.Contains(TEXT("\"ClearsSinceLastSurge\": 3")));
	TestEqual(TEXT("3 clears since the last surge"), Loaded->ClearsSinceLastSurge, 3);
	TestEqual(TEXT("2 cities reclaimed"), Loaded->CitiesReclaimed, 2);
	TestTrue(TEXT("the Last Stand spent"), Loaded->bSanctuaryLastStandSpent);
	TestEqual(TEXT("the upgrade bought on day 7"), Day, 7);
	return true;
}

/**
 * `Run_v8.json`, written before the event counts, migrates to nothing counted -- 0, 0, false, and a `BoughtDay` of 0 on
 * both its upgrades -- with every other field kept. CONTROL: the committed run file, `Run_v16.json` since the run
 * record went to version 16 (issue #2838), `Run_v15.json` from version 15 (issue #2741 slice 9), `Run_v14.json` from version 14 (issue #46), `Run_v13.json` from version 13 (issue #2825), `Run_v12.json` from version 12 (issue #2741 slice 6b), `Run_v11.json` from version 11 (issue #2741 slice 5b), `Run_v10.json` from version 10 (issue #2725) and `Run_v9.json`
 * before, reads 3, 1, true, 101 and 64.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeEventsMigrationTest,
	"Cataclysm.EmpireTreeEvents.ARunFileFromVersion8MigratesWithNothingCounted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeEventsMigrationTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeEventsTest;

	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Run_v8.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}
	const TSharedPtr<FJsonObject> Stepped = CataclysmSaveFixtures::Parse(Text);
	if (!TestTrue(TEXT("set-up: Run_v8.json is a JSON object with no event counts"),
				  Stepped.IsValid() && !Stepped->HasField(TEXT("ClearsSinceLastSurge"))))
	{
		return false;
	}
	FString StepMessage;
	const ECataclysmSaveMigrationResult Stepping = FCataclysmSaveMigration::Migrate(
		GetDefault<UCataclysmRunSave>()->MigrationSteps(), UCataclysmRunSave::SchemaVersionNow, Stepped.ToSharedRef(),
		StepMessage);
	double Written = -1.0;
	const bool bWritten = Stepped->TryGetNumberField(TEXT("ClearsSinceLastSurge"), Written);

	FString ReadText;
	const UCataclysmRunSave* Read = ReadRunFixture(*this, TEXT("Run_v8.json"), ReadText);
	FString CurrentText;
	const UCataclysmRunSave* Current = ReadRunFixture(*this, TEXT("Run_v16.json"), CurrentText);
	if (Read == nullptr || Current == nullptr)
	{
		return false;
	}

	// EVERY OTHER FIELD KEPT: the version 8 file, at the current version with what the step writes, is what the record
	// writes back.
	const TSharedPtr<FJsonObject> Expected = CataclysmSaveFixtures::Parse(Text);
	Expected->SetNumberField(TEXT("SchemaVersion"), UCataclysmRunSave::SchemaVersionNow);
	CataclysmSaveFixtures::AddTheRunsVersion9Defaults(*Expected);
	// AND A `bIsVengefulWraith` OF FALSE ON EVERY SAVED CREATURE, which `Migrate_9_to_10` writes, because
	// the chain runs on to run record version 10 since 2026-11-19 (issue #2725).
	CataclysmSaveFixtures::AddTheRunsVersion10Defaults(*Expected);
	// AND THE CLEARS SINCE THE LAST DEATH `Migrate_10_to_11` WRITES, because the chain runs on to run record
	// version 11 since 2026-11-20 (issue #2741 slice 5b).
	CataclysmSaveFixtures::AddTheRunsVersion11Defaults(*Expected);
	CataclysmSaveFixtures::AddTheRunsVersion12Defaults(*Expected);
	CataclysmSaveFixtures::AddTheRunsVersion13Defaults(*Expected);
	CataclysmSaveFixtures::AddTheRunsVersion14Defaults(*Expected); // And 14, #46 G3 and G5: socket lists, no gem on the floor.
	CataclysmSaveFixtures::AddTheRunsVersion15Defaults(*Expected); // And issue #2741 slice 9's step.
	CataclysmSaveFixtures::AddTheRunsVersion16Defaults(*Expected); // And issue #2838's step.
	FString Rewritten;
	FString Error;
	FString Where;
	const bool bSame = FCataclysmSaveStorage::ToJson(Read, Rewritten, Error)
		&& CataclysmSaveFixtures::SameObject(Expected, CataclysmSaveFixtures::Parse(Rewritten), Where);
	const TArray<int32> Migrated = BoughtDaysIn(*Read);
	const TArray<int32> Committed = BoughtDaysIn(*Current);

	AddInfo(FString::Printf(TEXT("EVENTS MIGRATED: the step %d (%s); count written %s; the record at version %d with %d, "
								 "%d, %s and bought days %s; Run_v16.json %d, %d, %s and %s; the rest %s"),
							static_cast<int32>(Stepping), *StepMessage, bWritten ? TEXT("yes") : TEXT("NO"),
							Read->SchemaVersion, Read->ClearsSinceLastSurge, Read->CitiesReclaimed,
							Read->bSanctuaryLastStandSpent ? TEXT("true") : TEXT("false"),
							*FString::JoinBy(Migrated, TEXT(","), [](int32 Day) { return FString::FromInt(Day); }),
							Current->ClearsSinceLastSurge, Current->CitiesReclaimed,
							Current->bSanctuaryLastStandSpent ? TEXT("true") : TEXT("false"),
							*FString::JoinBy(Committed, TEXT(","), [](int32 Day) { return FString::FromInt(Day); }),
							bSame ? TEXT("kept") : *Where));
	TestEqual(TEXT("the chain migrates the version 8 file"), static_cast<int32>(Stepping),
			  static_cast<int32>(ECataclysmSaveMigrationResult::Migrated));
	TestTrue(TEXT("the step writes a count of 0"), bWritten && Written == 0.0);
	TestEqual(TEXT("it arrives at the current version"), Read->SchemaVersion, UCataclysmRunSave::SchemaVersionNow);
	TestEqual(TEXT("no clear since the last surge"), Read->ClearsSinceLastSurge, 0);
	TestEqual(TEXT("no city reclaimed"), Read->CitiesReclaimed, 0);
	TestFalse(TEXT("the Last Stand not spent"), Read->bSanctuaryLastStandSpent);
	TestTrue(TEXT("both upgrades bought on day 0"), Migrated == TArray<int32>({ 0, 0 }));
	TestTrue(TEXT("and every other field is kept"), bSame);
	TestEqual(TEXT("CONTROL: Run_v16.json's clears"), Current->ClearsSinceLastSurge, 3);
	TestEqual(TEXT("CONTROL: its reclaimed"), Current->CitiesReclaimed, 1);
	TestTrue(TEXT("CONTROL: its Last Stand spent"), Current->bSanctuaryLastStandSpent);
	TestTrue(TEXT("CONTROL: its bought days, 101 and 64"), Committed == TArray<int32>({ 101, 64 }));
	return true;
}

/**
 * A run begun again counts nothing: a run that had cleared, reclaimed and spent its Last Stand begins with none of it.
 * That is how "Resets every tier" is met, a run's difficulty tier being fixed (Q3).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeEventsBeginTest,
	"Cataclysm.EmpireTreeEvents.ANewRunBeginsWithNothingCounted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeEventsBeginTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeEventsTest;

	UCataclysmEmpireRun* Run = QuietRun(2741, CountingBoth());
	Run->ClearsSinceLastSurge = 4;
	Run->CitiesReclaimed = 2;
	Run->bSanctuaryLastStandSpent = true;
	const bool bCountedBefore = Run->ClearsSinceLastSurge == 4 && Run->CitiesReclaimed == 2 && Run->bSanctuaryLastStandSpent;
	Run->Begin(2742);

	AddInfo(FString::Printf(TEXT("BEGUN AGAIN: %d clears, %d reclaimed, spent %s"), Run->ClearsSinceLastSurge,
							Run->CitiesReclaimed, Run->bSanctuaryLastStandSpent ? TEXT("YES") : TEXT("no")));
	TestTrue(TEXT("set-up: the run had counted"), bCountedBefore);
	TestEqual(TEXT("no clear since a surge"), Run->ClearsSinceLastSurge, 0);
	TestEqual(TEXT("no city reclaimed"), Run->CitiesReclaimed, 0);
	TestFalse(TEXT("the Last Stand not spent"), Run->bSanctuaryLastStandSpent);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
