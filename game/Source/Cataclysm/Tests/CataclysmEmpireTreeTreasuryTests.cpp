// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPassiveTree.h"
#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Player/CataclysmEmpireTree.h"
#include "Tests/CataclysmTestRun.h"

/**
 * The empire upgrade tree's slice 6d: Treasury Guard and Tax Collection. Issue #2741, the owner's rulings of
 * 2026-11-20 relayed by the coordinating session ("1. to your cities"; "we can adjust the gold prices and all of that
 * later"), every gold figure a tier 1 one scaled to the tier (R10):
 *
 *   Treasury Guard   every city's damage -1% a point for each whole 50,000 gold held, compounded, to its text's 20%,
 *                    held apart from Imperial Reserve's 25% and multiplied with it (D9)
 *   Tax Collection   every 10th day, 2% a point of the standing cities' citizens as gold, raised as Pax Imperialis is
 *                    (D8)
 *
 * `Cataclysm.EmpireTreeTreasury.` IS A NEW GROUP and selects these and nothing else.
 *
 * WRITTEN WITHOUT A BUILD. Phase 1 of the slice: every figure below is reasoned from the code, and the run that proves
 * them is registered in `docs/DECISIONS.md` before it happens.
 */
namespace CataclysmEmpireTreeTreasuryTest
{
	const FName TreasuryGuard(TEXT("basic_tres_t4e"));	// -1% a point per 50,000, cap 20%, 10 points
	const FName TaxCollection(TEXT("basic_tres_t1d"));	// 2% a point of the citizens every 10 days, 8 points

	/** A run begun on this seed, its first day spent and its board emptied, as the earlier slices' tests leave one. */
	UCataclysmEmpireRun* QuietTreasuryRun(int32 Seed)
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		const CataclysmTestRun::FBuiltAsWritten BuiltAsWritten1; // Issue #2928: the Cataclysms this run faces are named.
		Run->Begin(Seed);
		Run->AdvanceDay();
		Run->Dungeons.Empty();
		Run->Clock->Timers.Empty();
		return Run;
	}

	/** Treasury Guard at full points, and nothing else. */
	FCataclysmEmpireTreeBonuses Guard()
	{
		FCataclysmEmpireTreeBonuses Bonuses;
		Bonuses.ReductionPerHeldGoldStep = 0.01f;
		Bonuses.HeldGoldStepPoints = 10;
		Bonuses.HeldGoldReductionCap = 0.20f;
		return Bonuses;
	}
}

/**
 * The two add up through the generated tables at full points: Treasury Guard, a city figure, 0.01 a step at 10 points
 * to 0.20, in its own fields and not Imperial Reserve's; Tax Collection 0.02 at 8 points, 0.16. CONTROL: an empty tree
 * gives neither. Prints "TREASURY TABLE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeTreasuryTableTest,
	"Cataclysm.EmpireTreeTreasury.TheTwoNodesAddUpFromTheirRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeTreasuryTableTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeTreasuryTest;

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const UDataTable* Effects = UCataclysmEmpireTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the node table"), Nodes) || !TestNotNull(TEXT("set-up: the effect table"), Effects))
	{
		return false;
	}
	FCataclysmPassiveAllocation Tree;
	Tree.Add(TreasuryGuard, 10);
	Tree.Add(TaxCollection, 8);
	const FCataclysmEmpireTreePlayerBonuses Player = UCataclysmEmpireTree::PlayerBonusesFor(Nodes, Effects, Tree);
	const FCataclysmEmpireTreeBonuses City = UCataclysmEmpireTree::CityBonusesFor(Nodes, Effects, Tree);
	const FCataclysmEmpireTreePlayerBonuses NonePlayer =
		UCataclysmEmpireTree::PlayerBonusesFor(Nodes, Effects, FCataclysmPassiveAllocation());
	const FCataclysmEmpireTreeBonuses NoneCity =
		UCataclysmEmpireTree::CityBonusesFor(Nodes, Effects, FCataclysmPassiveAllocation());
	AddInfo(FString::Printf(TEXT("TREASURY TABLE: guard %.3f at %d points to %.2f (reserve's points %d); tax %.3f"),
							City.ReductionPerHeldGoldStep, City.HeldGoldStepPoints, City.HeldGoldReductionCap,
							City.GoldStepPoints, Player.GoldShareOfPopulationEveryTenDays));

	TestEqual(TEXT("Treasury Guard: 0.01 a step"), City.ReductionPerHeldGoldStep, 0.01f, 1.0e-5f);
	TestEqual(TEXT("at 10 points"), City.HeldGoldStepPoints, 10);
	TestEqual(TEXT("to 0.20"), City.HeldGoldReductionCap, 0.20f, 1.0e-5f);
	TestEqual(TEXT("and none of it in Imperial Reserve's fields"), City.GoldStepPoints, 0);
	TestEqual(TEXT("Tax Collection: 0.16"), Player.GoldShareOfPopulationEveryTenDays, 0.16f, 1.0e-5f);
	TestTrue(TEXT("CONTROL: an empty tree gives neither"),
			 NoneCity.HeldGoldStepPoints == 0 && NoneCity.ReductionPerHeldGoldStep == 0.0f
				 && NonePlayer.GoldShareOfPopulationEveryTenDays == 0.0f);
	return true;
}

/**
 * Treasury Guard reduces every city's damage from the gold held, in whole steps of 50,000 tier 1 gold, compounded and
 * held at its own 20%; with Imperial Reserve it multiplies, each held at its own cap (D9). At full points: 49,999 held
 * is no step, 1; 50,000 is one, 0.99^10 = 0.9044; 100,000 is two, 0.99^20 = 0.8179; 150,000 is three, 0.99^30 = 0.7397,
 * held at 0.80. With Imperial Reserve at 25 points, 100,000 is two steps of one and one of the other, 0.99^45 = 0.6362;
 * 1,000,000 is both at their caps, 0.80 x 0.75 = 0.60, which a run's city takes for defence and population alike.
 * CONTROL: no node is 1 at any gold. Prints "TREASURY GUARD".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeTreasuryGuardTest,
	"Cataclysm.EmpireTreeTreasury.TreasuryGuardCompoundsWithImperialReserveEachHeldAtItsOwnCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeTreasuryGuardTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeTreasuryTest;

	const FCataclysmEmpireTreeBonuses Alone = Guard();
	FCataclysmEmpireTreeBonuses Both = Guard();
	Both.ReductionPerGoldStep = 0.01f;
	Both.GoldStepPoints = 25;
	Both.GoldReductionCap = 0.25f;

	const float Under = Alone.GoldDamageFactorFor(49999.0);
	const float OneStep = Alone.GoldDamageFactorFor(50000.0);
	const float TwoSteps = Alone.GoldDamageFactorFor(100000.0);
	const float ThreeSteps = Alone.GoldDamageFactorFor(150000.0);
	const float BothAtTwo = Both.GoldDamageFactorFor(100000.0);
	const float BothAtCaps = Both.GoldDamageFactorFor(1000000.0);
	const float NoNode = FCataclysmEmpireTreeBonuses().GoldDamageFactorFor(1000000.0);

	UCataclysmEmpireRun* Run = QuietTreasuryRun(2741);
	if (!TestTrue(TEXT("set-up: a run with its cities"), Run->Map != nullptr && Run->Map->Cities.Num() > 0))
	{
		return false;
	}
	Run->SetTreeBonuses(Both);
	const int32 CityId = Run->Map->Cities[0].CityId;
	Run->GoldHeldAtTierOne = []() { return 1000000.0; };
	const FCataclysmTreeDamageFactors Taken = Run->TreeDamageFactorsFor(CityId, ECataclysmType::None);
	Run->GoldHeldAtTierOne = nullptr;
	AddInfo(FString::Printf(TEXT("TREASURY GUARD: under a step %.4f; one %.4f; two %.4f; three %.4f; with the reserve at "
								 "100,000 %.4f and at 1,000,000 %.4f; the run's city %.4f and %.4f; no node %.4f"),
							Under, OneStep, TwoSteps, ThreeSteps, BothAtTwo, BothAtCaps, Taken.Defence,
							Taken.Population, NoNode));

	TestEqual(TEXT("under a step: 1"), Under, 1.0f);
	TestEqual(TEXT("one step of 50,000: 0.99^10"), OneStep, FMath::Pow(0.99f, 10.0f), 1.0e-4f);
	TestEqual(TEXT("two steps: 0.99^20"), TwoSteps, FMath::Pow(0.99f, 20.0f), 1.0e-4f);
	TestEqual(TEXT("three steps, held at its 20%: 0.80"), ThreeSteps, 0.80f, 1.0e-4f);
	TestEqual(TEXT("with Imperial Reserve at 100,000: 0.99^20 x 0.99^25"), BothAtTwo, FMath::Pow(0.99f, 45.0f), 1.0e-4f);
	TestEqual(TEXT("both at their own caps: 0.80 x 0.75 = 0.60"), BothAtCaps, 0.60f, 1.0e-4f);
	TestEqual(TEXT("the run's city takes it for its defence"), Taken.Defence, 0.60f, 1.0e-4f);
	TestEqual(TEXT("and for its population"), Taken.Population, 0.60f, 1.0e-4f);
	TestEqual(TEXT("CONTROL: no node is 1"), NoNode, 1.0f);
	return true;
}

/**
 * Tax Collection pays on every 10th day (D8): at full points, 0.16 of 500,000 citizens is 80,000 on day 10 and day 20,
 * and nothing on day 7, day 15 or day 0; at tier 2, scale 3, 240,000; raised by Untouchable Wealth's 0.02 for each of
 * 20 standing cities, 0.40, it is 112,000; of 123,456 citizens it is 19,752.96, rounded to 19,753. With Pax Imperialis'
 * 5 a day for each of the 500 thousands, day 10 pays both, 2,500 + 80,000 = 82,500. No node: nothing. Prints
 * "TREASURY TAX".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeTreasuryTaxTest,
	"Cataclysm.EmpireTreeTreasury.TaxCollectionPaysEveryTenthDayFromTheStandingCitiesCitizens",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeTreasuryTaxTest::RunTest(const FString&)
{
	FCataclysmEmpireTreePlayerBonuses Bonuses;
	Bonuses.GoldShareOfPopulationEveryTenDays = 0.16f;
	FCataclysmEmpireTreeDungeonState State;
	State.Citizens = 500000.0f;

	const FCataclysmEmpireTreeDailyIncome Tenth = UCataclysmEmpireTree::DailyIncomeFor(Bonuses, State, 10, 0, 1.0);
	const FCataclysmEmpireTreeDailyIncome Twentieth = UCataclysmEmpireTree::DailyIncomeFor(Bonuses, State, 20, 0, 1.0);
	const FCataclysmEmpireTreeDailyIncome Seventh = UCataclysmEmpireTree::DailyIncomeFor(Bonuses, State, 7, 0, 1.0);
	const FCataclysmEmpireTreeDailyIncome Fifteenth = UCataclysmEmpireTree::DailyIncomeFor(Bonuses, State, 15, 0, 1.0);
	const FCataclysmEmpireTreeDailyIncome DayNought = UCataclysmEmpireTree::DailyIncomeFor(Bonuses, State, 0, 0, 1.0);
	const FCataclysmEmpireTreeDailyIncome TierTwo = UCataclysmEmpireTree::DailyIncomeFor(Bonuses, State, 10, 0, 3.0);
	FCataclysmEmpireTreePlayerBonuses Raised = Bonuses;
	Raised.GoldPerStandingCity = 0.02f;
	FCataclysmEmpireTreeDungeonState Standing = State;
	Standing.StandingCities = 20;
	const FCataclysmEmpireTreeDailyIncome WithWealth = UCataclysmEmpireTree::DailyIncomeFor(Raised, Standing, 10, 0, 1.0);
	FCataclysmEmpireTreeDungeonState Fewer;
	Fewer.Citizens = 123456.0f;
	const FCataclysmEmpireTreeDailyIncome Rounded = UCataclysmEmpireTree::DailyIncomeFor(Bonuses, Fewer, 10, 0, 1.0);
	FCataclysmEmpireTreePlayerBonuses WithPax = Bonuses;
	WithPax.GoldPerThousandCitizensDaily = 5.0f;
	const FCataclysmEmpireTreeDailyIncome Together = UCataclysmEmpireTree::DailyIncomeFor(WithPax, State, 10, 0, 1.0);
	const FCataclysmEmpireTreeDailyIncome NoNode =
		UCataclysmEmpireTree::DailyIncomeFor(FCataclysmEmpireTreePlayerBonuses(), State, 10, 0, 1.0);
	AddInfo(FString::Printf(TEXT("TREASURY TAX: day 10 %lld, day 20 %lld, day 7 %lld, day 15 %lld, day 0 %lld; tier 2 "
								 "%lld; raised %lld; of 123,456 citizens %lld; with Pax %lld + %lld = %lld; no node %lld"),
							Tenth.Tax, Twentieth.Tax, Seventh.Tax, Fifteenth.Tax, DayNought.Tax, TierTwo.Tax,
							WithWealth.Tax, Rounded.Tax, Together.Pax, Together.Tax, Together.Total(), NoNode.Total()));

	TestEqual(TEXT("day 10: 80,000"), Tenth.Tax, static_cast<int64>(80000));
	TestEqual(TEXT("day 20: 80,000"), Twentieth.Tax, static_cast<int64>(80000));
	TestEqual(TEXT("nothing on day 7"), Seventh.Tax, static_cast<int64>(0));
	TestEqual(TEXT("nothing on day 15"), Fifteenth.Tax, static_cast<int64>(0));
	TestEqual(TEXT("nothing on day 0"), DayNought.Tax, static_cast<int64>(0));
	TestEqual(TEXT("tier 2: 240,000"), TierTwo.Tax, static_cast<int64>(240000));
	TestEqual(TEXT("raised by Untouchable Wealth's 0.40: 112,000"), WithWealth.Tax, static_cast<int64>(112000));
	TestEqual(TEXT("of 123,456 citizens: 19,753"), Rounded.Tax, static_cast<int64>(19753));
	TestEqual(TEXT("with Pax Imperialis the day pays both: 82,500"), Together.Total(), static_cast<int64>(82500));
	TestEqual(TEXT("no node: nothing"), NoNode.Total(), static_cast<int64>(0));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
