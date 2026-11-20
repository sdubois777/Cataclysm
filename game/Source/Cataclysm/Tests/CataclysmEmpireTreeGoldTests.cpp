// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Algo/Accumulate.h"
#include "Character/CataclysmPassiveTree.h"
#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "EngineUtils.h"
#include "Items/CataclysmDroppedItem.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmEmpireTree.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The empire upgrade tree's slice 6a: the gold a kill drops and a city upgrade's price. Issue #2741, the coordinating
 * session's rulings G1 to G3, P1 and P2 of 2026-11-20.
 *
 *   Coinage                +5% gold a point; Minting Rights +3% a point: one increase (G1)
 *   The Midas Touch        "50% more": its own multiplier after the increase (G1)
 *   Untouchable Wealth     +2% a point for each city not fallen (G3)
 *   Wartime Economy        +10% a point for each active Cataclysm type (G3)
 *   Profiteering           +5% a point for each fallen city not erased (G3)
 *   Prosperity Index       +1% a point for each whole 10,000 citizens of the standing cities (G3)
 *   Fiscal Policy          +3% a point for each whole 5 upgrades the standing cities hold (G3)
 *   Municipal Bonds        -3% a point on every city upgrade's price; Public Works -20% (P1)
 *   War Funding            -2% a point on an upgrade bought on a city holding none (P2)
 *
 * The price reductions add and are held at 75% (P1, a judgement). PRICE CONTROLS IS SLICE 6B'S: its P3 reading, "below
 * its maximum now", was withdrawn on 2026-11-20, because a maximum the tree raises leaves every full city below it. The gold multiplier is applied to each pile's
 * amount after the pile's own draws, so it draws nothing. `Cataclysm.EmpireTreeGold.` IS A NEW GROUP and selects
 * these and nothing else; the screen-and-purchase test is in `Cataclysm.CityUpgradePrice.`, beside its scene.
 *
 * WRITTEN WITHOUT A BUILD. Phase 1 of the slice: every figure below is reasoned from the code, and the run that proves
 * them is registered in `docs/DECISIONS.md` before it happens.
 */
namespace CataclysmEmpireTreeGoldTest
{
	const FName Coinage(TEXT("basic_0000003q"));				// +5% a point, 10 points
	const FName MintingRights(TEXT("basic_tres_t1f"));			// +3% a point, 8 points
	const FName UntouchableWealth(TEXT("basic_tres_t4c"));		// +2% a point per city not fallen, 10 points
	const FName WartimeEconomy(TEXT("basic_tres_t3d"));			// +10% a point per active type, 10 points
	const FName Profiteering(TEXT("basic_tres_t3e"));			// +5% a point per fallen city, 10 points
	const FName ProsperityIndex(TEXT("basic_tres_t4g"));		// +1% a point per whole 10,000 citizens, 10 points
	const FName FiscalPolicy(TEXT("basic_tres_t2g"));			// +3% a point per whole 5 upgrades, 8 points
	const FName MunicipalBonds(TEXT("basic_0000003v"));			// -3% a point, 15 points
	const FName PublicWorks(TEXT("keystone_00000013"));			// -20%, a keystone
	const FName WarFunding(TEXT("basic_0000003s"));				// -2% a point, a city's first upgrade, 10 points
	const FName Vanguard(TEXT("capstone_0000000o"));			// 100: option 3, The Midas Touch, 50% more

	/** Every one of the eleven at its full points, The Midas Touch chosen. */
	FCataclysmPassiveAllocation EveryGoldNode()
	{
		FCataclysmPassiveAllocation Tree;
		Tree.Add(Coinage, 10);
		Tree.Add(MintingRights, 8);
		Tree.Add(UntouchableWealth, 10);
		Tree.Add(WartimeEconomy, 10);
		Tree.Add(Profiteering, 10);
		Tree.Add(ProsperityIndex, 10);
		Tree.Add(FiscalPolicy, 8);
		Tree.Add(MunicipalBonds, 15);
		Tree.Add(PublicWorks, 1);
		Tree.Add(WarFunding, 10);
		Tree.Add(Vanguard, 1);
		Tree.SetChosenOption(Vanguard, 3);
		return Tree;
	}

	FCataclysmEmpireTreePlayerBonuses GoldBonusesOf(const FCataclysmPassiveAllocation& Tree)
	{
		return UCataclysmEmpireTree::PlayerBonusesFor(UCataclysmEmpireTree::LoadNodeTable(),
													  UCataclysmEmpireTree::LoadEffectTable(), Tree);
	}

	/** A run begun on this seed, its first day spent and its board emptied, as the earlier slices' tests leave one. */
	UCataclysmEmpireRun* QuietGoldRun(int32 Seed)
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->Begin(Seed);
		Run->AdvanceDay();
		Run->Dungeons.Empty();
		Run->Clock->Timers.Empty();
		return Run;
	}

	/** Every gold pile's amount lying in the world, smallest first. */
	TArray<int32> PileAmountsIn(UWorld* World)
	{
		TArray<int32> Out;
		for (TActorIterator<ACataclysmDroppedItem> It(World); It; ++It)
		{
			if (IsValid(*It) && It->IsGold())
			{
				Out.Add(It->Gold);
			}
		}
		Out.Sort();
		return Out;
	}

	/** Takes every dropped thing off the floor, so the next roll is counted alone. */
	void ClearTheFloor(UWorld* World)
	{
		for (TActorIterator<ACataclysmDroppedItem> It(World); It; ++It)
		{
			It->Destroy();
		}
	}

	/** A Cataclysm Boss's rung: 12 piles expected, so a draw of none is a chance of about one in 160,000. */
	constexpr int32 CataclysmBossStep = 5;
}

// ---------------------------------------------------------------------------
// The rows
// ---------------------------------------------------------------------------

/**
 * Every one of the eleven at full points adds up as its row says: the increase 0.74 (Coinage's 0.50 and Minting Rights'
 * 0.24), the Midas Touch's 0.50 "more", the five counted figures 0.20, 1.00, 0.50, 0.10 and 0.24, and the price
 * reductions 0.65 (Municipal Bonds' 0.45 and Public Works' 0.20) and 0.20. Prints "GOLD TABLE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeGoldRowsTest,
	"Cataclysm.EmpireTreeGold.TheElevenNodesAddUpFromTheirRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeGoldRowsTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeGoldTest;

	if (!TestNotNull(TEXT("set-up: the node table"), UCataclysmEmpireTree::LoadNodeTable())
		|| !TestNotNull(TEXT("set-up: the effect table"), UCataclysmEmpireTree::LoadEffectTable()))
	{
		return false;
	}

	const FCataclysmEmpireTreePlayerBonuses All = GoldBonusesOf(EveryGoldNode());
	const FCataclysmEmpireTreePlayerBonuses None = GoldBonusesOf(FCataclysmPassiveAllocation());
	AddInfo(FString::Printf(TEXT("GOLD TABLE: increased %.2f, more %.2f; per city %.2f, per type %.2f, per fallen %.2f, "
								 "per 10,000 %.2f, per 5 upgrades %.2f; price %.2f, first %.2f"),
							All.GoldIncreased, All.GoldMore, All.GoldPerStandingCity, All.GoldPerActiveType,
							All.GoldPerFallenCity, All.GoldPerTenThousandCitizens, All.GoldPerFiveCityUpgrades,
							All.UpgradePriceReduced, All.FirstUpgradePriceReduced));

	TestEqual(TEXT("Coinage and Minting Rights: 0.74"), All.GoldIncreased, 0.74f, 1.0e-4f);
	TestEqual(TEXT("The Midas Touch: 0.50 more"), All.GoldMore, 0.50f, 1.0e-4f);
	TestEqual(TEXT("Untouchable Wealth: 0.20 a city"), All.GoldPerStandingCity, 0.20f, 1.0e-4f);
	TestEqual(TEXT("Wartime Economy: 1.00 a type"), All.GoldPerActiveType, 1.00f, 1.0e-4f);
	TestEqual(TEXT("Profiteering: 0.50 a fallen city"), All.GoldPerFallenCity, 0.50f, 1.0e-4f);
	TestEqual(TEXT("Prosperity Index: 0.10 a 10,000"), All.GoldPerTenThousandCitizens, 0.10f, 1.0e-4f);
	TestEqual(TEXT("Fiscal Policy: 0.24 a 5 upgrades"), All.GoldPerFiveCityUpgrades, 0.24f, 1.0e-4f);
	TestEqual(TEXT("Municipal Bonds and Public Works: 0.65"), All.UpgradePriceReduced, 0.65f, 1.0e-4f);
	TestEqual(TEXT("War Funding: 0.20"), All.FirstUpgradePriceReduced, 0.20f, 1.0e-4f);
	TestEqual(TEXT("and an empty tree gives no increase"), None.GoldIncreased, 0.0f);
	TestEqual(TEXT("no more"), None.GoldMore, 0.0f);
	TestEqual(TEXT("and no price reduction"), None.UpgradePriceReduced, 0.0f);
	return true;
}

// ---------------------------------------------------------------------------
// The gold a kill drops
// ---------------------------------------------------------------------------

/**
 * The multiplier is one plus the increase, times one plus the "more" (G1), the counts in whole steps (G3). With every
 * gold node, 20 standing cities, 3 active types, 4 fallen cities, 125,000 citizens (12 steps) and 13 upgrades (2 steps):
 * increased 0.74 + 4.00 + 3.00 + 2.00 + 1.20 + 0.48 = 11.42, so (1 + 11.42) x 1.5 = 18.63. 9,999 citizens and 4 upgrades
 * are no step. With no node it is exactly 1, and the drop rules carry the same figure and roll nothing. Prints
 * "GOLD MULTIPLIER".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeGoldMultiplierTest,
	"Cataclysm.EmpireTreeGold.TheGoldMultiplierIsTheIncreaseTimesTheMore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeGoldMultiplierTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeGoldTest;

	const FCataclysmEmpireTreePlayerBonuses All = GoldBonusesOf(EveryGoldNode());
	FCataclysmEmpireTreeDungeonState State;
	State.StandingCities = 20;
	State.ActiveTypes = 3;
	State.FallenCitiesNotErased = 4;
	State.Citizens = 125000.0f;
	State.StandingCityUpgrades = 13;

	FCataclysmEmpireTreeDungeonState BelowASteps = State;
	BelowASteps.Citizens = 9999.0f;
	BelowASteps.StandingCityUpgrades = 4;

	const float Full = UCataclysmEmpireTree::GoldMultiplierFor(All, State);
	const float Below = UCataclysmEmpireTree::GoldMultiplierFor(All, BelowASteps);
	const float NoNode = UCataclysmEmpireTree::GoldMultiplierFor(FCataclysmEmpireTreePlayerBonuses(), State);
	const FCataclysmEmpireTreeDropRules Rules = UCataclysmEmpireTree::DropRulesFor(All, State);
	const FCataclysmEmpireTreeDropRules NoRules =
		UCataclysmEmpireTree::DropRulesFor(FCataclysmEmpireTreePlayerBonuses(), State);
	AddInfo(FString::Printf(TEXT("GOLD MULTIPLIER: %.4f with every node; %.4f below a step; %.4f with none; the rules "
								 "carry %.4f and roll %s"),
							Full, Below, NoNode, Rules.GoldMultiplier, Rules.RollsAnything() ? TEXT("something") : TEXT("nothing")));

	TestEqual(TEXT("(1 + 11.42) x 1.5 = 18.63"), Full, 18.63f, 1.0e-3f);
	TestEqual(TEXT("9,999 citizens and 4 upgrades are no step: (1 + 9.74) x 1.5 = 16.11"), Below, 16.11f, 1.0e-3f);
	TestEqual(TEXT("no gold node: exactly 1"), NoNode, 1.0f);
	TestEqual(TEXT("the drop rules carry the same figure"), Rules.GoldMultiplier, Full, 1.0e-4f);
	TestEqual(TEXT("and with no node, exactly 1"), NoRules.GoldMultiplier, 1.0f);
	TestFalse(TEXT("the gold nodes alone roll nothing"), Rules.RollsAnything());
	return true;
}

/**
 * The counts are the run's cities (G3): of the 25, cities 1 to 3 fallen and city 3 erased, so 22 standing and 2 fallen
 * that can be reclaimed; 2 upgrades on city 0 and 3 on city 4 count, the one on fallen city 1 does not; and the
 * citizens are the map's own total. Prints "GOLD COUNTS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeGoldCountsTest,
	"Cataclysm.EmpireTreeGold.TheCountsAreTheRunsCities",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeGoldCountsTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeGoldTest;

	UCataclysmEmpireRun* Run = QuietGoldRun(2741);
	if (!TestTrue(TEXT("set-up: a run with its 25 cities"), Run->Map != nullptr && Run->Map->Cities.Num() == 25))
	{
		return false;
	}

	TArray<FCataclysmCity>& Cities = Run->Map->Cities;
	for (const int32 Index : { 1, 2, 3 })
	{
		Cities[Index].bFallen = true;
	}
	Cities[3].bErased = true;
	Cities[0].Upgrades.AddDefaulted(2);
	Cities[1].Upgrades.AddDefaulted(1);
	Cities[4].Upgrades.AddDefaulted(3);

	FCataclysmDungeon Dungeon;
	Dungeon.DungeonId = 1;
	Dungeon.Type = ECataclysmDungeonType::Basic;
	Dungeon.CityId = Cities[0].CityId;
	Dungeon.CityTier = Cities[0].Tier;
	Dungeon.Floors = 10;

	const FCataclysmEmpireTreeDungeonState State =
		UCataclysmEmpireTree::DungeonStateFor(*Run, Dungeon, TArray<FName>(), /*FloorNumber=*/1);
	const float Total = Run->Map->TotalPopulation();
	AddInfo(FString::Printf(TEXT("GOLD COUNTS: %d standing, %d fallen not erased, %.0f citizens (the map's %.0f), %d "
								 "upgrades"),
							State.StandingCities, State.FallenCitiesNotErased, State.Citizens, Total,
							State.StandingCityUpgrades));

	TestEqual(TEXT("22 standing"), State.StandingCities, 22);
	TestEqual(TEXT("2 fallen that can be reclaimed"), State.FallenCitiesNotErased, 2);
	TestEqual(TEXT("5 upgrades held by standing cities"), State.StandingCityUpgrades, 5);
	TestEqual(TEXT("the citizens are the map's total"), State.Citizens, Total, 1.0f);
	TestTrue(TEXT("and there are some"), State.Citizens > 0.0f);
	return true;
}

/**
 * A kill's piles hold the multiplied amount, and the multiplier draws nothing: from one seed, a Cataclysm Boss's kill at
 * 2.5 drops the same number of piles as at 1, each pile 2.5 times the one rolled at 1 rounded to whole gold, and leaves
 * the stream exactly where the roll at 1 leaves it. Prints "GOLD PILES".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeGoldPilesTest,
	"Cataclysm.EmpireTreeGold.AKillsGoldPilesAreMultipliedAfterTheirDraws",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeGoldPilesTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeGoldTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a test world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FRandomStream AtOne(2741);
	const int32 PlacedAtOne =
		UCataclysmDropSpawner::SpawnGoldFor(World, CataclysmBossStep, FVector::ZeroVector, AtOne, 1.0f);
	const TArray<int32> Rolled = PileAmountsIn(World);
	ClearTheFloor(World);

	FRandomStream AtTwoAndAHalf(2741);
	const int32 PlacedAtTwoAndAHalf =
		UCataclysmDropSpawner::SpawnGoldFor(World, CataclysmBossStep, FVector::ZeroVector, AtTwoAndAHalf, 2.5f);
	const TArray<int32> Multiplied = PileAmountsIn(World);

	FString Pairs;
	for (int32 Index = 0; Index < FMath::Min(Rolled.Num(), Multiplied.Num()); ++Index)
	{
		Pairs += FString::Printf(TEXT(" %d->%d"), Rolled[Index], Multiplied[Index]);
	}
	AddInfo(FString::Printf(TEXT("GOLD PILES: %d at 1 and %d at 2.5;%s; the streams end at %d and %d"), PlacedAtOne,
							PlacedAtTwoAndAHalf, *Pairs, AtOne.GetCurrentSeed(), AtTwoAndAHalf.GetCurrentSeed()));

	if (!TestTrue(TEXT("set-up: the kill dropped gold"), PlacedAtOne > 0 && Rolled.Num() == PlacedAtOne))
	{
		return false;
	}
	TestEqual(TEXT("as many piles at 2.5"), Multiplied.Num(), Rolled.Num());
	TestEqual(TEXT("and the multiplier draws nothing"), AtTwoAndAHalf.GetCurrentSeed(), AtOne.GetCurrentSeed());
	int32 Wrong = 0;
	for (int32 Index = 0; Index < FMath::Min(Rolled.Num(), Multiplied.Num()); ++Index)
	{
		const int32 Wanted = FMath::Max(1, FMath::RoundToInt32(static_cast<double>(Rolled[Index]) * 2.5));
		Wrong += Multiplied[Index] == Wanted ? 0 : 1;
	}
	TestEqual(TEXT("each pile is 2.5 times the one rolled, to whole gold"), Wrong, 0);
	return true;
}

/**
 * A kill with no gold node draws the same stream and drops the same gold as before slice 6a: the drop rules of an empty
 * tree carry a multiplier of exactly 1, and a roll at that multiplier and a roll that names none leave the same piles and
 * the stream at the same place. Prints "GOLD NONE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeGoldNoneTest,
	"Cataclysm.EmpireTreeGold.AKillWithNoGoldNodeDrawsTheSameStreamAndDropsTheSameGold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeGoldNoneTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeGoldTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a test world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmEmpireTreeDropRules NoRules = UCataclysmEmpireTree::DropRulesFor(
		FCataclysmEmpireTreePlayerBonuses(), FCataclysmEmpireTreeDungeonState());

	FRandomStream NamedNone(2741);
	UCataclysmDropSpawner::SpawnGoldFor(World, CataclysmBossStep, FVector::ZeroVector, NamedNone);
	const TArray<int32> Before = PileAmountsIn(World);
	ClearTheFloor(World);

	FRandomStream EmptyTree(2741);
	UCataclysmDropSpawner::SpawnGoldFor(World, CataclysmBossStep, FVector::ZeroVector, EmptyTree, NoRules.GoldMultiplier);
	const TArray<int32> After = PileAmountsIn(World);

	AddInfo(FString::Printf(TEXT("GOLD NONE: the empty tree's multiplier %.4f; %d piles holding %d named none, %d piles "
								 "holding %d with the empty tree; the streams end at %d and %d"),
							NoRules.GoldMultiplier, Before.Num(), Algo::Accumulate(Before, 0), After.Num(),
							Algo::Accumulate(After, 0), NamedNone.GetCurrentSeed(), EmptyTree.GetCurrentSeed()));

	TestEqual(TEXT("the empty tree's multiplier is exactly 1"), NoRules.GoldMultiplier, 1.0f);
	TestTrue(TEXT("set-up: the kill dropped gold"), Before.Num() > 0);
	TestEqual(TEXT("the same piles"), After, Before);
	TestEqual(TEXT("and the stream at the same place"), EmptyTree.GetCurrentSeed(), NamedNone.GetCurrentSeed());
	return true;
}

// ---------------------------------------------------------------------------
// A city upgrade's price
// ---------------------------------------------------------------------------

/**
 * The price factor on a city (P1, P2): 0.70 for a 0.30 reduction on a city that holds an upgrade; 0.50 with War
 * Funding's 0.20 on a city that holds none; every reduction at full points on a city that holds none, 0.85 in all, held
 * at 0.75, so a quarter; on one that holds an upgrade 0.65, below the cap, so 0.35; exactly 1 with no node, no map or no
 * such city. Prints "GOLD PRICE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeGoldPriceTest,
	"Cataclysm.EmpireTreeGold.ThePriceFallsByTheReductionsHeldAtThreeQuarters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeGoldPriceTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeGoldTest;

	UCataclysmEmpireRun* Run = QuietGoldRun(2741);
	if (!TestTrue(TEXT("set-up: a run with a map"), Run->Map != nullptr && Run->Map->Cities.Num() > 2))
	{
		return false;
	}
	UCataclysmEmpireMap* Map = Run->Map;
	FCataclysmCity& Holding = Map->Cities[0];
	FCataclysmCity& Bare = Map->Cities[1];
	Holding.Upgrades.AddDefaulted(1);
	Bare.Upgrades.Empty();

	FCataclysmEmpireTreePlayerBonuses Some;
	Some.UpgradePriceReduced = 0.30f;
	Some.FirstUpgradePriceReduced = 0.20f;
	const FCataclysmEmpireTreePlayerBonuses All = GoldBonusesOf(EveryGoldNode());

	const float OnHolding = UCataclysmEmpireTree::UpgradePriceFactorFor(Some, Map, Holding.CityId);
	const float OnBare = UCataclysmEmpireTree::UpgradePriceFactorFor(Some, Map, Bare.CityId);
	const float Capped = UCataclysmEmpireTree::UpgradePriceFactorFor(All, Map, Bare.CityId);
	const float BelowTheCap = UCataclysmEmpireTree::UpgradePriceFactorFor(All, Map, Holding.CityId);
	const float NoNode = UCataclysmEmpireTree::UpgradePriceFactorFor(FCataclysmEmpireTreePlayerBonuses(), Map, Bare.CityId);
	const float NoMap = UCataclysmEmpireTree::UpgradePriceFactorFor(All, nullptr, Bare.CityId);
	const float NoCity = UCataclysmEmpireTree::UpgradePriceFactorFor(All, Map, 9999);
	AddInfo(FString::Printf(TEXT("GOLD PRICE: holding %.2f, bare %.2f; every node %.2f bare and %.2f holding; none %.2f, "
								 "no map %.2f, no city %.2f"),
							OnHolding, OnBare, Capped, BelowTheCap, NoNode, NoMap, NoCity));

	TestEqual(TEXT("a city that holds an upgrade: 0.70"), OnHolding, 0.70f, 1.0e-4f);
	TestEqual(TEXT("War Funding on a city that holds none: 0.50"), OnBare, 0.50f, 1.0e-4f);
	TestEqual(TEXT("every node on a city holding none, 0.85, held at 0.75: a quarter"), Capped, 0.25f, 1.0e-4f);
	TestEqual(TEXT("every node on a city holding one, 0.65, below the cap: 0.35"), BelowTheCap, 0.35f, 1.0e-4f);
	TestEqual(TEXT("no price node: exactly 1"), NoNode, 1.0f);
	TestEqual(TEXT("no map: exactly 1"), NoMap, 1.0f);
	TestEqual(TEXT("no such city: exactly 1"), NoCity, 1.0f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
