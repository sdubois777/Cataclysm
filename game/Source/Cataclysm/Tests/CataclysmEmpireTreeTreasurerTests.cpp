// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPassiveTree.h"
#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/DataTable.h"
#include "Player/CataclysmEmpireTree.h"
#include "Tests/CataclysmTestRun.h"

/**
 * The empire upgrade tree's slice 7: the four Treasury nodes that read a Treasurer city upgrade. Issue #2741, the
 * coordinating session's R1 to R4 of 2026-11-20, on readings T1 to T4, ruled as proposed on 2026-11-21. A
 * Treasurer city is a standing city holding at least one upgrade of the City Upgrades sheet's Treasurer branch (T1, T3):
 *
 *   Commercial Hubs    +20% gold a kill drops while the dungeon's city is a Treasurer city, an increase (T1)
 *   Economic Zones     5% "more" a point on the gold a Treasurer city generates: a kill's piles and the Salvage Rights
 *                      payout of a dungeon standing on one, all of Trade Routes' gold, and the Treasurer cities' share of
 *                      Pax Imperialis and Tax Collection (T2)
 *   Trade Routes       every 20th day, 50 tier 1 gold a point for each Treasurer city (T3)
 *   The Golden Touch   +10% gold and crafting materials a kill drops for each Treasurer city, an increase (T3, T4)
 *
 * `Cataclysm.EmpireTreeTreasurer.` IS A NEW GROUP and selects these and nothing else; `Cataclysm.EmpireTreeTreasury.`,
 * slice 6d's, is another.
 *
 * WRITTEN WITHOUT A BUILD. Phase 1 of the slice: every figure below is reasoned from the code, and the run that proves
 * them is registered in `docs/DECISIONS.md` before it happens.
 */
namespace CataclysmEmpireTreeTreasurerTest
{
	const FName CommercialHubs(TEXT("keystone_0000003x"));	// +20%, 1 point
	const FName EconomicZones(TEXT("basic_tres_t2d"));		// 5% more a point, 10 points
	const FName TradeRoutes(TEXT("basic_tres_t1g"));		// 50 gold a point every 20 days, 5 points
	const FName GoldenTouch(TEXT("capstone_0000004b"));		// +10% a city, 1 point

	const FName TreasurerGold(TEXT("Treasurer_Dungeons_here_drop_25_more_gold"));
	const FName TreasurerLoot(TEXT("Treasurer_Dungeons_here_drop_25_more_loot"));
	const FName ArchitectDefence(TEXT("Architect_Increase_max_defense_by_20"));

	/** A run begun on this seed, its first day spent and its board emptied, as the earlier slices' tests leave one. */
	UCataclysmEmpireRun* QuietTreasurerRun(int32 Seed)
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		const CataclysmTestRun::FBuiltAsWritten BuiltAsWritten1; // Issue #2928: the Cataclysms this run faces are named.
		Run->Begin(Seed);
		Run->AdvanceDay();
		Run->Dungeons.Empty();
		Run->Clock->Timers.Empty();
		return Run;
	}

	FCataclysmCityUpgrade Bought(FName RowName)
	{
		FCataclysmCityUpgrade Upgrade;
		Upgrade.RowName = RowName;
		return Upgrade;
	}

	/** A dungeon on this city, not on the board, for asking the state of. */
	FCataclysmDungeon DungeonOn(int32 CityId)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = 9901;
		Dungeon.Type = ECataclysmDungeonType::Basic;
		Dungeon.CityId = CityId;
		Dungeon.CityTier = ECataclysmCityTier::Outpost;
		Dungeon.Floors = 10;
		Dungeon.Bosses = 1;
		return Dungeon;
	}

	/** The four at full points, as the table gives them. */
	FCataclysmEmpireTreePlayerBonuses AllFour()
	{
		FCataclysmEmpireTreePlayerBonuses Bonuses;
		Bonuses.GoldIncreasedOnTreasurerCity = 0.20f;
		Bonuses.GoldMoreFromTreasurerCities = 0.50f;
		Bonuses.GoldEveryTwentyDaysPerTreasurerCity = 250.0f;
		Bonuses.GoldAndMaterialsPerTreasurerCity = 0.10f;
		return Bonuses;
	}
}

/**
 * The four add up through the generated tables at full points: Commercial Hubs 0.20 at 1 point, Economic Zones 0.05 at
 * 10, 0.50; Trade Routes 50 at 5, 250; The Golden Touch 0.10 at 1. CONTROL: an empty tree gives none of them. Prints
 * "TREASURER TABLE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeTreasurerTableTest,
	"Cataclysm.EmpireTreeTreasurer.TheFourNodesAddUpFromTheirRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeTreasurerTableTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeTreasurerTest;

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const UDataTable* Effects = UCataclysmEmpireTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the node table"), Nodes) || !TestNotNull(TEXT("set-up: the effect table"), Effects))
	{
		return false;
	}
	FCataclysmPassiveAllocation Tree;
	Tree.Add(CommercialHubs, 1);
	Tree.Add(EconomicZones, 10);
	Tree.Add(TradeRoutes, 5);
	Tree.Add(GoldenTouch, 1);
	const FCataclysmEmpireTreePlayerBonuses Player = UCataclysmEmpireTree::PlayerBonusesFor(Nodes, Effects, Tree);
	const FCataclysmEmpireTreePlayerBonuses None =
		UCataclysmEmpireTree::PlayerBonusesFor(Nodes, Effects, FCataclysmPassiveAllocation());
	AddInfo(FString::Printf(TEXT("TREASURER TABLE: hubs %.3f, zones %.3f, routes %.1f, touch %.3f; empty %.3f %.3f %.1f "
								 "%.3f"),
							Player.GoldIncreasedOnTreasurerCity, Player.GoldMoreFromTreasurerCities,
							Player.GoldEveryTwentyDaysPerTreasurerCity, Player.GoldAndMaterialsPerTreasurerCity,
							None.GoldIncreasedOnTreasurerCity, None.GoldMoreFromTreasurerCities,
							None.GoldEveryTwentyDaysPerTreasurerCity, None.GoldAndMaterialsPerTreasurerCity));

	TestEqual(TEXT("Commercial Hubs: 0.20"), Player.GoldIncreasedOnTreasurerCity, 0.20f, 1.0e-5f);
	TestEqual(TEXT("Economic Zones: 0.50"), Player.GoldMoreFromTreasurerCities, 0.50f, 1.0e-5f);
	TestEqual(TEXT("Trade Routes: 250"), Player.GoldEveryTwentyDaysPerTreasurerCity, 250.0f, 1.0e-3f);
	TestEqual(TEXT("The Golden Touch: 0.10"), Player.GoldAndMaterialsPerTreasurerCity, 0.10f, 1.0e-5f);
	TestTrue(TEXT("CONTROL: an empty tree gives none of them"),
			 None.GoldIncreasedOnTreasurerCity == 0.0f && None.GoldMoreFromTreasurerCities == 0.0f
				 && None.GoldEveryTwentyDaysPerTreasurerCity == 0.0f && None.GoldAndMaterialsPerTreasurerCity == 0.0f);
	return true;
}

/**
 * A Treasurer city stands and holds at least one Treasurer upgrade (T1, T3). Of four cities: the first holds a Treasurer
 * and an Architect upgrade, and counts; the second an Architect one, and does not; the third a Treasurer one and has
 * fallen, and does not; the fourth two Treasurer ones, and counts once. So the run counts 2, and the citizens of the
 * first and the fourth; a dungeon on the first is on a Treasurer city, on the second or the fallen third it is not.
 * CONTROL: with no upgrade anywhere, no city counts. Prints "TREASURER CITIES".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeTreasurerCitiesTest,
	"Cataclysm.EmpireTreeTreasurer.TreasurerCitiesAreStandingCitiesHoldingATreasurerUpgrade",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeTreasurerCitiesTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeTreasurerTest;

	UCataclysmEmpireRun* Run = QuietTreasurerRun(2741);
	if (!TestTrue(TEXT("set-up: at least four cities"), Run->Map != nullptr && Run->Map->Cities.Num() >= 4))
	{
		return false;
	}
	TArray<FCataclysmCity>& Cities = Run->Map->Cities;
	for (FCataclysmCity& City : Cities)
	{
		City.Upgrades.Reset();
		City.bFallen = false;
	}
	const FCataclysmEmpireTreeDungeonState Before = UCataclysmEmpireTree::EmpireStateFor(*Run);

	Cities[0].Upgrades = { Bought(TreasurerGold), Bought(ArchitectDefence) };
	Cities[1].Upgrades = { Bought(ArchitectDefence) };
	Cities[2].Upgrades = { Bought(TreasurerGold) };
	Cities[2].bFallen = true;
	Cities[3].Upgrades = { Bought(TreasurerGold), Bought(TreasurerLoot) };
	const float Citizens = Run->Map->PopulationOf(Cities[0]) + Run->Map->PopulationOf(Cities[3]);

	const FCataclysmEmpireTreeDungeonState After = UCataclysmEmpireTree::EmpireStateFor(*Run);
	const bool bOnFirst = UCataclysmEmpireTree::DungeonStateFor(*Run, DungeonOn(Cities[0].CityId), {}, 1)
		.bDungeonCityIsTreasurer;
	const bool bOnSecond = UCataclysmEmpireTree::DungeonStateFor(*Run, DungeonOn(Cities[1].CityId), {}, 1)
		.bDungeonCityIsTreasurer;
	const bool bOnFallen = UCataclysmEmpireTree::DungeonStateFor(*Run, DungeonOn(Cities[2].CityId), {}, 1)
		.bDungeonCityIsTreasurer;
	AddInfo(FString::Printf(TEXT("TREASURER CITIES: before %d; after %d with %.0f citizens of %.0f wanted; on the first "
								 "%d, the second %d, the fallen third %d"),
							Before.TreasurerCities, After.TreasurerCities, After.TreasurerCitizens, Citizens,
							bOnFirst ? 1 : 0, bOnSecond ? 1 : 0, bOnFallen ? 1 : 0));

	TestEqual(TEXT("CONTROL: no upgrade anywhere, no Treasurer city"), Before.TreasurerCities, 0);
	TestEqual(TEXT("the first and the fourth, each once"), After.TreasurerCities, 2);
	TestTrue(TEXT("set-up: those two have citizens"), Citizens > 0.0f);
	TestEqual(TEXT("and their citizens are counted"), After.TreasurerCitizens, Citizens, 1.0f);
	TestTrue(TEXT("a dungeon on the first is on a Treasurer city"), bOnFirst);
	TestFalse(TEXT("one on the second, which holds only an Architect upgrade, is not"), bOnSecond);
	TestFalse(TEXT("one on the fallen third is not"), bOnFallen);
	return true;
}

/**
 * A kill's gold (T1 to T3) with the four at full points and three Treasurer cities: on a Treasurer city, (1 + 0.30 +
 * 0.20) x (1 + 0.50) = 2.25; on any other, The Golden Touch alone, 1.30; with Coinage's 0.50 beside it on a Treasurer
 * city, (1 + 0.50 + 0.30 + 0.20) x 1.50 = 3.00. Its crafting materials (T4): 1.30 at three cities, 1 at none, and the
 * drop rules still roll nothing. The drop rules carry the same gold figure. CONTROL: no node is 1 for both. Prints
 * "TREASURER KILL".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeTreasurerKillTest,
	"Cataclysm.EmpireTreeTreasurer.AKillsGoldAndMaterialsTakeTheFourWhereItsDungeonStands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeTreasurerKillTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeTreasurerTest;

	const FCataclysmEmpireTreePlayerBonuses Bonuses = AllFour();
	FCataclysmEmpireTreeDungeonState OnOne;
	OnOne.TreasurerCities = 3;
	OnOne.bDungeonCityIsTreasurer = true;
	FCataclysmEmpireTreeDungeonState Elsewhere = OnOne;
	Elsewhere.bDungeonCityIsTreasurer = false;
	FCataclysmEmpireTreeDungeonState NoCity;
	FCataclysmEmpireTreePlayerBonuses WithCoinage = Bonuses;
	WithCoinage.GoldIncreased = 0.50f;

	const float Here = UCataclysmEmpireTree::GoldMultiplierFor(Bonuses, OnOne);
	const float There = UCataclysmEmpireTree::GoldMultiplierFor(Bonuses, Elsewhere);
	const float Coined = UCataclysmEmpireTree::GoldMultiplierFor(WithCoinage, OnOne);
	const float NoNode = UCataclysmEmpireTree::GoldMultiplierFor(FCataclysmEmpireTreePlayerBonuses(), OnOne);
	const FCataclysmEmpireTreeDropRules Rules = UCataclysmEmpireTree::DropRulesFor(Bonuses, OnOne);
	const FCataclysmEmpireTreeDropRules NoCityRules = UCataclysmEmpireTree::DropRulesFor(Bonuses, NoCity);
	const FCataclysmEmpireTreeDropRules NoNodeRules =
		UCataclysmEmpireTree::DropRulesFor(FCataclysmEmpireTreePlayerBonuses(), OnOne);
	AddInfo(FString::Printf(TEXT("TREASURER KILL: on a Treasurer city %.4f, elsewhere %.4f, with Coinage %.4f, no node "
								 "%.4f; materials %.4f, at no city %.4f, no node %.4f; the rules' gold %.4f; rolls %d"),
							Here, There, Coined, NoNode, Rules.MaterialsMultiplier, NoCityRules.MaterialsMultiplier,
							NoNodeRules.MaterialsMultiplier, Rules.GoldMultiplier, Rules.RollsAnything() ? 1 : 0));

	TestEqual(TEXT("on a Treasurer city: 1.50 x 1.50 = 2.25"), Here, 2.25f, 1.0e-4f);
	TestEqual(TEXT("elsewhere, The Golden Touch alone: 1.30"), There, 1.30f, 1.0e-4f);
	TestEqual(TEXT("with Coinage on a Treasurer city: 2.00 x 1.50 = 3.00"), Coined, 3.00f, 1.0e-4f);
	TestEqual(TEXT("CONTROL: no node, 1"), NoNode, 1.0f, 1.0e-6f);
	TestEqual(TEXT("materials at three Treasurer cities: 1.30"), Rules.MaterialsMultiplier, 1.30f, 1.0e-4f);
	TestEqual(TEXT("materials at none: 1"), NoCityRules.MaterialsMultiplier, 1.0f, 1.0e-6f);
	TestEqual(TEXT("CONTROL: materials with no node, 1"), NoNodeRules.MaterialsMultiplier, 1.0f, 1.0e-6f);
	TestEqual(TEXT("the drop rules carry the kill's gold figure"), Rules.GoldMultiplier, Here, 1.0e-6f);
	TestFalse(TEXT("and roll nothing more"), Rules.RollsAnything());
	return true;
}

/**
 * The Salvage Rights payout of a 10 floor dungeon at 50 gold a floor, 500 (T2): with Economic Zones' 0.50 on a
 * Treasurer city, 750; on any other, 500; Commercial Hubs and The Golden Touch raise no payout, so with them alone it is
 * 500. Cleanup Crew's tenth of 1,000 left lying is 100 in every case, multiplied by nothing (B3). Prints "TREASURER
 * PAYOUT".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeTreasurerPayoutTest,
	"Cataclysm.EmpireTreeTreasurer.TheSalvagePayoutTakesEconomicZonesOnATreasurerCityAndCleanupNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeTreasurerPayoutTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeTreasurerTest;

	FCataclysmEmpireTreePlayerBonuses Bonuses = AllFour();
	Bonuses.GoldPerFloorAtClear = 50.0f;
	Bonuses.UnpickedGoldShareAtClear = 0.10f;
	FCataclysmEmpireTreePlayerBonuses NoZones = Bonuses;
	NoZones.GoldMoreFromTreasurerCities = 0.0f;
	FCataclysmEmpireTreeDungeonState OnOne;
	OnOne.TreasurerCities = 3;
	OnOne.bDungeonCityIsTreasurer = true;
	FCataclysmEmpireTreeDungeonState Elsewhere = OnOne;
	Elsewhere.bDungeonCityIsTreasurer = false;

	const FCataclysmEmpireTreeClearPay Here = UCataclysmEmpireTree::ClearPayFor(Bonuses, OnOne, 10, 1.0, 1000, 0);
	const FCataclysmEmpireTreeClearPay There = UCataclysmEmpireTree::ClearPayFor(Bonuses, Elsewhere, 10, 1.0, 1000, 0);
	const FCataclysmEmpireTreeClearPay Others = UCataclysmEmpireTree::ClearPayFor(NoZones, OnOne, 10, 1.0, 1000, 0);
	AddInfo(FString::Printf(TEXT("TREASURER PAYOUT: salvage on a Treasurer city %lld, elsewhere %lld, without Economic "
								 "Zones %lld; cleanup %lld, %lld, %lld"),
							Here.Salvage, There.Salvage, Others.Salvage, Here.Cleanup, There.Cleanup, Others.Cleanup));

	TestEqual(TEXT("on a Treasurer city: 500 x 1.50 = 750"), Here.Salvage, static_cast<int64>(750));
	TestEqual(TEXT("elsewhere: 500"), There.Salvage, static_cast<int64>(500));
	TestEqual(TEXT("Commercial Hubs and The Golden Touch raise no payout: 500"), Others.Salvage, static_cast<int64>(500));
	TestEqual(TEXT("Cleanup Crew on a Treasurer city: 100"), Here.Cleanup, static_cast<int64>(100));
	TestEqual(TEXT("and elsewhere: 100"), There.Cleanup, static_cast<int64>(100));
	return true;
}

/**
 * Trade Routes at full points, 250 a Treasurer city, with three (T3): day 20 and day 40 pay 750; day 10, day 21 and day
 * 0 nothing; at tier 2, scale 3, 2,250; with Economic Zones' 0.50, all of it, 1,125; raised by Untouchable Wealth's
 * 0.02 for each of 20 standing cities, 0.40, 1,050; with both, 1,575. With no Treasurer city it pays nothing, and it is
 * part of the day's total. CONTROL: no node pays nothing. Prints "TREASURER ROUTES".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeTreasurerRoutesTest,
	"Cataclysm.EmpireTreeTreasurer.TradeRoutesPaysEveryTwentiethDayForEachTreasurerCity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeTreasurerRoutesTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeTreasurerTest;

	FCataclysmEmpireTreePlayerBonuses Routes;
	Routes.GoldEveryTwentyDaysPerTreasurerCity = 250.0f;
	FCataclysmEmpireTreePlayerBonuses Zoned = Routes;
	Zoned.GoldMoreFromTreasurerCities = 0.50f;
	FCataclysmEmpireTreePlayerBonuses Wealthy = Routes;
	Wealthy.GoldPerStandingCity = 0.02f;
	FCataclysmEmpireTreePlayerBonuses Both = Zoned;
	Both.GoldPerStandingCity = 0.02f;
	FCataclysmEmpireTreeDungeonState Three;
	Three.TreasurerCities = 3;
	FCataclysmEmpireTreeDungeonState Standing = Three;
	Standing.StandingCities = 20;
	FCataclysmEmpireTreeDungeonState NoTreasurerCity;

	auto Trade = [](const FCataclysmEmpireTreePlayerBonuses& Bonuses, const FCataclysmEmpireTreeDungeonState& State,
					int32 Day, double Scale)
	{
		return UCataclysmEmpireTree::DailyIncomeFor(Bonuses, State, Day, 0, Scale);
	};
	const FCataclysmEmpireTreeDailyIncome Twentieth = Trade(Routes, Three, 20, 1.0);
	const int64 Fortieth = Trade(Routes, Three, 40, 1.0).Trade;
	const int64 Tenth = Trade(Routes, Three, 10, 1.0).Trade;
	const int64 TwentyFirst = Trade(Routes, Three, 21, 1.0).Trade;
	const int64 DayNought = Trade(Routes, Three, 0, 1.0).Trade;
	const int64 TierTwo = Trade(Routes, Three, 20, 3.0).Trade;
	const int64 WithZones = Trade(Zoned, Three, 20, 1.0).Trade;
	const int64 WithWealth = Trade(Wealthy, Standing, 20, 1.0).Trade;
	const int64 WithBoth = Trade(Both, Standing, 20, 1.0).Trade;
	const int64 NoCity = Trade(Routes, NoTreasurerCity, 20, 1.0).Trade;
	const int64 NoNode = Trade(FCataclysmEmpireTreePlayerBonuses(), Three, 20, 1.0).Total();
	AddInfo(FString::Printf(TEXT("TREASURER ROUTES: day 20 %lld (total %lld), day 40 %lld, day 10 %lld, day 21 %lld, day "
								 "0 %lld; tier 2 %lld; zones %lld; wealth %lld; both %lld; no city %lld; no node %lld"),
							Twentieth.Trade, Twentieth.Total(), Fortieth, Tenth, TwentyFirst, DayNought, TierTwo,
							WithZones, WithWealth, WithBoth, NoCity, NoNode));

	TestEqual(TEXT("day 20: 3 x 250 = 750"), Twentieth.Trade, static_cast<int64>(750));
	TestEqual(TEXT("and it is the day's total"), Twentieth.Total(), static_cast<int64>(750));
	TestEqual(TEXT("day 40: 750"), Fortieth, static_cast<int64>(750));
	TestEqual(TEXT("nothing on day 10"), Tenth, static_cast<int64>(0));
	TestEqual(TEXT("nothing on day 21"), TwentyFirst, static_cast<int64>(0));
	TestEqual(TEXT("nothing on day 0"), DayNought, static_cast<int64>(0));
	TestEqual(TEXT("tier 2: 2,250"), TierTwo, static_cast<int64>(2250));
	TestEqual(TEXT("with Economic Zones, all of it: 1,125"), WithZones, static_cast<int64>(1125));
	TestEqual(TEXT("raised by Untouchable Wealth's 0.40: 1,050"), WithWealth, static_cast<int64>(1050));
	TestEqual(TEXT("with both: 1,575"), WithBoth, static_cast<int64>(1575));
	TestEqual(TEXT("no Treasurer city: nothing"), NoCity, static_cast<int64>(0));
	TestEqual(TEXT("CONTROL: no node, nothing"), NoNode, static_cast<int64>(0));
	return true;
}

/**
 * Economic Zones raises Pax Imperialis and Tax Collection in the share of the citizens who live in Treasurer cities
 * (T2): of 500,000 citizens, 100,000 in Treasurer cities, a fifth, so 0.50 "more" is 1.10. Pax at 5 a day for each of
 * the 500 thousands, 2,500, becomes 2,750; Tax at 0.16 on day 10, 80,000, becomes 88,000. Wealth Interest's 0.005 of
 * 100,000 held on day 20, 500, stays 500: the gold held is tied to no city. With no citizen in a Treasurer city, Pax is
 * 2,500. CONTROL: without Economic Zones, Pax is 2,500. Prints "TREASURER SHARE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeTreasurerShareTest,
	"Cataclysm.EmpireTreeTreasurer.EconomicZonesRaisesPaxAndTaxByTheTreasurerCitizensShare",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeTreasurerShareTest::RunTest(const FString&)
{
	FCataclysmEmpireTreePlayerBonuses Bonuses;
	Bonuses.GoldMoreFromTreasurerCities = 0.50f;
	Bonuses.GoldPerThousandCitizensDaily = 5.0f;
	Bonuses.GoldShareOfPopulationEveryTenDays = 0.16f;
	Bonuses.GoldInterestEveryTwentyDays = 0.005f;
	Bonuses.GoldInterestCap = 5000.0f;
	FCataclysmEmpireTreePlayerBonuses NoZones = Bonuses;
	NoZones.GoldMoreFromTreasurerCities = 0.0f;
	FCataclysmEmpireTreeDungeonState State;
	State.Citizens = 500000.0f;
	State.TreasurerCitizens = 100000.0f;
	State.TreasurerCities = 1;
	FCataclysmEmpireTreeDungeonState NoTreasurer = State;
	NoTreasurer.TreasurerCitizens = 0.0f;
	NoTreasurer.TreasurerCities = 0;

	const FCataclysmEmpireTreeDailyIncome Tenth = UCataclysmEmpireTree::DailyIncomeFor(Bonuses, State, 10, 100000, 1.0);
	const FCataclysmEmpireTreeDailyIncome Twentieth =
		UCataclysmEmpireTree::DailyIncomeFor(Bonuses, State, 20, 100000, 1.0);
	const FCataclysmEmpireTreeDailyIncome Outside =
		UCataclysmEmpireTree::DailyIncomeFor(Bonuses, NoTreasurer, 10, 100000, 1.0);
	const FCataclysmEmpireTreeDailyIncome Without =
		UCataclysmEmpireTree::DailyIncomeFor(NoZones, State, 10, 100000, 1.0);
	AddInfo(FString::Printf(TEXT("TREASURER SHARE: day 10 pax %lld tax %lld; day 20 interest %lld; no Treasurer citizen "
								 "pax %lld; without Economic Zones pax %lld"),
							Tenth.Pax, Tenth.Tax, Twentieth.Interest, Outside.Pax, Without.Pax));

	TestEqual(TEXT("Pax: 2,500 x 1.10 = 2,750"), Tenth.Pax, static_cast<int64>(2750));
	TestEqual(TEXT("Tax: 80,000 x 1.10 = 88,000"), Tenth.Tax, static_cast<int64>(88000));
	TestEqual(TEXT("Wealth Interest is not raised: 500"), Twentieth.Interest, static_cast<int64>(500));
	TestEqual(TEXT("no citizen in a Treasurer city: Pax 2,500"), Outside.Pax, static_cast<int64>(2500));
	TestEqual(TEXT("CONTROL: without Economic Zones, Pax 2,500"), Without.Pax, static_cast<int64>(2500));
	return true;
}

/**
 * The citizen share Economic Zones raises Pax and Tax by is counted from each Treasurer city's own population, not from
 * how many there are (T2, the coordinating session's ruling of 2026-11-21 asked for this test). Every city is emptied
 * but two: the first holds 1,000 citizens and the second 3,000, so the run holds 4,000. Tax Collection at 0.16 on day 10
 * pays 640 without Economic Zones; with its 0.50 "more": the second alone a Treasurer city, a share of 3/4, 640 x 1.375
 * = 880; the first alone, 1/4, 640 x 1.125 = 720; both, the whole, 640 x 1.50 = 960. A share by the count of cities
 * would give the first and the second alone the same figure. CONTROL: without Economic Zones, 640. Prints "TREASURER
 * TWO CITIES".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeTreasurerTwoCitiesTest,
	"Cataclysm.EmpireTreeTreasurer.TheCitizenShareIsEachTreasurerCitysOwnPopulation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeTreasurerTwoCitiesTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeTreasurerTest;

	UCataclysmEmpireRun* Run = QuietTreasurerRun(2741);
	if (!TestTrue(TEXT("set-up: at least two cities"), Run->Map != nullptr && Run->Map->Cities.Num() >= 2))
	{
		return false;
	}
	TArray<FCataclysmCity>& Cities = Run->Map->Cities;
	for (FCataclysmCity& City : Cities)
	{
		City.Upgrades.Reset();
		City.bFallen = false;
		City.Population = 0.0f;
	}
	Cities[0].Population = 1000.0f;
	Cities[1].Population = 3000.0f;

	FCataclysmEmpireTreePlayerBonuses Bonuses;
	Bonuses.GoldMoreFromTreasurerCities = 0.50f;
	Bonuses.GoldShareOfPopulationEveryTenDays = 0.16f;
	FCataclysmEmpireTreePlayerBonuses NoZones = Bonuses;
	NoZones.GoldMoreFromTreasurerCities = 0.0f;
	auto TaxNow = [Run](const FCataclysmEmpireTreePlayerBonuses& Paying)
	{
		return UCataclysmEmpireTree::DailyIncomeFor(Paying, UCataclysmEmpireTree::EmpireStateFor(*Run), 10, 0, 1.0).Tax;
	};

	Cities[1].Upgrades = { Bought(TreasurerGold) };
	const FCataclysmEmpireTreeDungeonState SecondState = UCataclysmEmpireTree::EmpireStateFor(*Run);
	const int64 Second = TaxNow(Bonuses);
	Cities[1].Upgrades.Reset();
	Cities[0].Upgrades = { Bought(TreasurerGold) };
	const int64 First = TaxNow(Bonuses);
	Cities[1].Upgrades = { Bought(TreasurerLoot) };
	const FCataclysmEmpireTreeDungeonState BothState = UCataclysmEmpireTree::EmpireStateFor(*Run);
	const int64 Both = TaxNow(Bonuses);
	const int64 Without = TaxNow(NoZones);
	AddInfo(FString::Printf(TEXT("TREASURER TWO CITIES: citizens %.0f; the second alone %.0f of them, tax %lld; the first "
								 "alone %lld; both %d cities, %.0f citizens, tax %lld; without Economic Zones %lld"),
							SecondState.Citizens, SecondState.TreasurerCitizens, Second, First, BothState.TreasurerCities,
							BothState.TreasurerCitizens, Both, Without));

	TestEqual(TEXT("set-up: the run holds 4,000 citizens"), SecondState.Citizens, 4000.0f, 0.5f);
	TestEqual(TEXT("the second alone: 3,000 of them in a Treasurer city"), SecondState.TreasurerCitizens, 3000.0f, 0.5f);
	TestEqual(TEXT("the second alone, 3/4: 880"), Second, static_cast<int64>(880));
	TestEqual(TEXT("the first alone, 1/4: 720"), First, static_cast<int64>(720));
	TestEqual(TEXT("both, two cities and 4,000 citizens"), BothState.TreasurerCities, 2);
	TestEqual(TEXT("both, the whole: 960"), Both, static_cast<int64>(960));
	TestEqual(TEXT("CONTROL: without Economic Zones, 640"), Without, static_cast<int64>(640));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
