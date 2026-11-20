// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPassiveTree.h"
#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Player/CataclysmEmpireTree.h"

/**
 * The empire upgrade tree's slice 6c: the gold held and the day's income. Issue #2741, the coordinating session's B6 to
 * B8 of 2026-11-20, every gold figure a tier 1 one scaled to the tier (R10):
 *
 *   The Gilded Path    +1% magic find a kill for each whole 10,000 gold held -- uncapped, because its text names none
 *   Venture Capital    +1% a point for each whole 50,000, to its text's 20%
 *   Imperial Reserve   every city's damage -1% a point for each whole 100,000, compounded, to its text's 25% (B7)
 *   Pax Imperialis     5 gold every day for each whole 1,000 citizens of the standing cities (B8)
 *   Wealth Interest    every 20th day, 0.5% a point of the gold held, to 5,000 a payment (B8)
 *
 * The income is raised by the "all sources" increases G2 sends to it. Tax Collection and Treasury Guard, ruled by the
 * owner after this slice was written, are slice 6d's (`Cataclysm.EmpireTreeTreasury.`).
 * `Cataclysm.EmpireTreeGoldHeld.` IS A NEW GROUP and selects these and nothing else.
 *
 * WRITTEN WITHOUT A BUILD. Phase 1 of the slice: every figure below is reasoned from the code, and the run that proves
 * them is registered in `docs/DECISIONS.md` before it happens.
 */
namespace CataclysmEmpireTreeGoldHeldTest
{
	const FName GildedPath(TEXT("keystone_00000048"));		// +1% per 10,000, a keystone
	const FName VentureCapital(TEXT("basic_tres_t3f"));		// +1% a point per 50,000, cap 20%, 10 points
	const FName ImperialReserve(TEXT("basic_00000045"));	// -1% a point per 100,000, cap 25%, 25 points
	const FName PaxImperialis(TEXT("keystone_0000004a"));	// 5 a day per 1,000 citizens, a keystone
	const FName WealthInterest(TEXT("basic_00000041"));		// 0.5% a point every 20 days, cap 5,000, 10 points

	/** A run begun on this seed, its first day spent and its board emptied, as the earlier slices' tests leave one. */
	UCataclysmEmpireRun* QuietHeldRun(int32 Seed)
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->Begin(Seed);
		Run->AdvanceDay();
		Run->Dungeons.Empty();
		Run->Clock->Timers.Empty();
		return Run;
	}

	/** The gold held, at a tier's scale, every other figure nought. */
	FCataclysmEmpireTreeDungeonState Holding(int64 Gold, double Scale)
	{
		FCataclysmEmpireTreeDungeonState State;
		State.GoldHeld = Gold;
		State.GoldScale = Scale;
		return State;
	}
}

/**
 * The five add up through the generated tables at full points: The Gilded Path 0.01, Venture Capital 0.10 to its 0.20,
 * Pax Imperialis 5, Wealth Interest 0.05 to 5,000; and Imperial Reserve, a city figure, 0.01 a step at 25 points to
 * 0.25. CONTROL: an empty tree gives none of them. Prints "GOLD HELD TABLE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeGoldHeldTableTest,
	"Cataclysm.EmpireTreeGoldHeld.TheFiveNodesAddUpFromTheirRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeGoldHeldTableTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeGoldHeldTest;

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const UDataTable* Effects = UCataclysmEmpireTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the node table"), Nodes) || !TestNotNull(TEXT("set-up: the effect table"), Effects))
	{
		return false;
	}
	FCataclysmPassiveAllocation Tree;
	Tree.Add(GildedPath, 1);
	Tree.Add(VentureCapital, 10);
	Tree.Add(ImperialReserve, 25);
	Tree.Add(PaxImperialis, 1);
	Tree.Add(WealthInterest, 10);
	const FCataclysmEmpireTreePlayerBonuses Player = UCataclysmEmpireTree::PlayerBonusesFor(Nodes, Effects, Tree);
	const FCataclysmEmpireTreeBonuses City = UCataclysmEmpireTree::CityBonusesFor(Nodes, Effects, Tree);
	const FCataclysmEmpireTreePlayerBonuses None =
		UCataclysmEmpireTree::PlayerBonusesFor(Nodes, Effects, FCataclysmPassiveAllocation());
	AddInfo(FString::Printf(TEXT("GOLD HELD TABLE: gilded %.3f, venture %.3f to %.2f, pax %.1f, interest %.4f to %.0f; "
								 "reserve %.3f at %d points to %.2f"),
							Player.MagicFindPerTenThousandGoldHeld, Player.MagicFindPerFiftyThousandGoldHeld,
							Player.MagicFindFromGoldCap, Player.GoldPerThousandCitizensDaily,
							Player.GoldInterestEveryTwentyDays, Player.GoldInterestCap, City.ReductionPerGoldStep,
							City.GoldStepPoints, City.GoldReductionCap));

	TestEqual(TEXT("The Gilded Path: 0.01"), Player.MagicFindPerTenThousandGoldHeld, 0.01f, 1.0e-5f);
	TestEqual(TEXT("Venture Capital: 0.10"), Player.MagicFindPerFiftyThousandGoldHeld, 0.10f, 1.0e-5f);
	TestEqual(TEXT("to its 0.20"), Player.MagicFindFromGoldCap, 0.20f, 1.0e-5f);
	TestEqual(TEXT("Pax Imperialis: 5"), Player.GoldPerThousandCitizensDaily, 5.0f, 1.0e-4f);
	TestEqual(TEXT("Wealth Interest: 0.05"), Player.GoldInterestEveryTwentyDays, 0.05f, 1.0e-5f);
	TestEqual(TEXT("to 5,000"), Player.GoldInterestCap, 5000.0f, 1.0e-2f);
	TestEqual(TEXT("Imperial Reserve: 0.01 a step"), City.ReductionPerGoldStep, 0.01f, 1.0e-5f);
	TestEqual(TEXT("at 25 points"), City.GoldStepPoints, 25);
	TestEqual(TEXT("to 0.25"), City.GoldReductionCap, 0.25f, 1.0e-5f);
	TestTrue(TEXT("CONTROL: an empty tree gives none"),
			 None.MagicFindPerTenThousandGoldHeld == 0.0f && None.MagicFindPerFiftyThousandGoldHeld == 0.0f
				 && None.GoldPerThousandCitizensDaily == 0.0f && None.GoldInterestEveryTwentyDays == 0.0f);
	return true;
}

/**
 * The gold held gives a kill magic find in whole steps of tier 1 gold (B6): with The Gilded Path and Venture Capital at
 * full points, 35,000 held at tier 1 is 3 steps of 10,000 and none of 50,000: 3 points; 120,000 is 12 and 2 steps, 12 +
 * 20 = 32 (Venture Capital at its 20); 1,000,000 is 100 + 20 = 120, The Gilded Path uncapped; 35,000 at tier 2, whose
 * scale is 3, is 11,666 tier 1 gold: 1 point. Nothing held: none. The drop rules carry the same figure. Prints
 * "GOLD HELD MAGIC FIND".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeGoldHeldMagicFindTest,
	"Cataclysm.EmpireTreeGoldHeld.TheGoldHeldGivesMagicFindInWholeStepsAtTheTiersScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeGoldHeldMagicFindTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeGoldHeldTest;

	FCataclysmEmpireTreePlayerBonuses Bonuses;
	Bonuses.MagicFindPerTenThousandGoldHeld = 0.01f;
	Bonuses.MagicFindPerFiftyThousandGoldHeld = 0.10f;
	Bonuses.MagicFindFromGoldCap = 0.20f;

	const float Few = UCataclysmEmpireTree::MagicFindFromGoldFor(Bonuses, Holding(35000, 1.0));
	const float Some = UCataclysmEmpireTree::MagicFindFromGoldFor(Bonuses, Holding(120000, 1.0));
	const float Plenty = UCataclysmEmpireTree::MagicFindFromGoldFor(Bonuses, Holding(1000000, 1.0));
	const float TierTwo = UCataclysmEmpireTree::MagicFindFromGoldFor(Bonuses, Holding(35000, 3.0));
	const float Nothing = UCataclysmEmpireTree::MagicFindFromGoldFor(Bonuses, Holding(0, 1.0));
	const FCataclysmEmpireTreeDropRules Rules = UCataclysmEmpireTree::DropRulesFor(Bonuses, Holding(120000, 1.0));
	AddInfo(FString::Printf(TEXT("GOLD HELD MAGIC FIND: 35,000 %.1f; 120,000 %.1f; 1,000,000 %.1f; 35,000 at tier 2 %.1f; "
								 "none %.1f; the rules carry %.1f"),
							Few, Some, Plenty, TierTwo, Nothing, Rules.MagicFindFromGold));

	TestEqual(TEXT("35,000: 3 points"), Few, 3.0f, 1.0e-3f);
	TestEqual(TEXT("120,000: 12 and Venture Capital's 20"), Some, 32.0f, 1.0e-3f);
	TestEqual(TEXT("1,000,000: 100, uncapped, and 20"), Plenty, 120.0f, 1.0e-3f);
	TestEqual(TEXT("35,000 at tier 2: 1 point"), TierTwo, 1.0f, 1.0e-3f);
	TestEqual(TEXT("nothing held: none"), Nothing, 0.0f);
	TestEqual(TEXT("the drop rules carry the same figure"), Rules.MagicFindFromGold, Some, 1.0e-3f);
	return true;
}

/**
 * Imperial Reserve reduces every city's damage by the gold held (B7): at 25 points, 100,000 tier 1 gold is one step,
 * 0.99^25 = 0.7778 kept; 250,000 is two, 0.99^50 = 0.605, held at its 25%, so 0.75; under a step, and with no function
 * set, 1. Prints "GOLD HELD RESERVE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeGoldHeldReserveTest,
	"Cataclysm.EmpireTreeGoldHeld.ImperialReserveReducesEveryCitysDamageByTheGoldHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeGoldHeldReserveTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeGoldHeldTest;

	UCataclysmEmpireRun* Run = QuietHeldRun(2741);
	if (!TestTrue(TEXT("set-up: a run with its cities"), Run->Map != nullptr && Run->Map->Cities.Num() > 0))
	{
		return false;
	}
	FCataclysmEmpireTreeBonuses Reserve;
	Reserve.ReductionPerGoldStep = 0.01f;
	Reserve.GoldStepPoints = 25;
	Reserve.GoldReductionCap = 0.25f;
	Run->SetTreeBonuses(Reserve);
	const int32 CityId = Run->Map->Cities[0].CityId;

	const float Unset = Run->TreeDamageFactorsFor(CityId, ECataclysmType::None).Defence;
	double Held = 99999.0;
	Run->GoldHeldAtTierOne = [&Held]() { return Held; };
	const float Under = Run->TreeDamageFactorsFor(CityId, ECataclysmType::None).Defence;
	Held = 100000.0;
	const float OneStep = Run->TreeDamageFactorsFor(CityId, ECataclysmType::None).Defence;
	const float OneStepPopulation = Run->TreeDamageFactorsFor(CityId, ECataclysmType::None).Population;
	Held = 250000.0;
	const float TwoSteps = Run->TreeDamageFactorsFor(CityId, ECataclysmType::None).Defence;
	Run->GoldHeldAtTierOne = nullptr;
	AddInfo(FString::Printf(TEXT("GOLD HELD RESERVE: unset %.4f; under a step %.4f; one step %.4f (population %.4f); two "
								 "steps %.4f"),
							Unset, Under, OneStep, OneStepPopulation, TwoSteps));

	TestEqual(TEXT("no function set: 1"), Unset, 1.0f);
	TestEqual(TEXT("under a step: 1"), Under, 1.0f);
	TestEqual(TEXT("one step: 0.99^25"), OneStep, FMath::Pow(0.99f, 25.0f), 1.0e-4f);
	TestEqual(TEXT("population alike"), OneStepPopulation, OneStep, 1.0e-6f);
	TestEqual(TEXT("two steps, held at 25%: 0.75"), TwoSteps, 0.75f, 1.0e-4f);
	return true;
}

/**
 * The day's income (B8): Pax Imperialis' 5 for each of 123 whole thousands of 123,456 citizens is 615 every day; Wealth
 * Interest's 0.05 of 10,000 held is 500 on day 20 and nothing on day 7 or day 0; of 1,000,000 held it is held at 5,000.
 * At tier 2, scale 3: Pax 1,845 and the cap 15,000. Raised by Untouchable Wealth's 0.02 for each of 20 standing cities,
 * 0.40: Pax 861. Prints "GOLD HELD INCOME".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeGoldHeldIncomeTest,
	"Cataclysm.EmpireTreeGoldHeld.TheDayPaysPaxEveryDayAndInterestEveryTwentiethDay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeGoldHeldIncomeTest::RunTest(const FString&)
{
	FCataclysmEmpireTreePlayerBonuses Bonuses;
	Bonuses.GoldPerThousandCitizensDaily = 5.0f;
	Bonuses.GoldInterestEveryTwentyDays = 0.05f;
	Bonuses.GoldInterestCap = 5000.0f;
	FCataclysmEmpireTreeDungeonState State;
	State.Citizens = 123456.0f;

	const FCataclysmEmpireTreeDailyIncome Seventh = UCataclysmEmpireTree::DailyIncomeFor(Bonuses, State, 7, 10000, 1.0);
	const FCataclysmEmpireTreeDailyIncome Twentieth = UCataclysmEmpireTree::DailyIncomeFor(Bonuses, State, 20, 10000, 1.0);
	const FCataclysmEmpireTreeDailyIncome DayNought = UCataclysmEmpireTree::DailyIncomeFor(Bonuses, State, 0, 10000, 1.0);
	const FCataclysmEmpireTreeDailyIncome Capped = UCataclysmEmpireTree::DailyIncomeFor(Bonuses, State, 40, 1000000, 1.0);
	const FCataclysmEmpireTreeDailyIncome TierTwo = UCataclysmEmpireTree::DailyIncomeFor(Bonuses, State, 20, 1000000, 3.0);
	FCataclysmEmpireTreePlayerBonuses Raised = Bonuses;
	Raised.GoldPerStandingCity = 0.02f;
	FCataclysmEmpireTreeDungeonState Standing = State;
	Standing.StandingCities = 20;
	const FCataclysmEmpireTreeDailyIncome WithWealth = UCataclysmEmpireTree::DailyIncomeFor(Raised, Standing, 7, 0, 1.0);
	const FCataclysmEmpireTreeDailyIncome NoNode =
		UCataclysmEmpireTree::DailyIncomeFor(FCataclysmEmpireTreePlayerBonuses(), State, 20, 1000000, 1.0);
	AddInfo(FString::Printf(TEXT("GOLD HELD INCOME: day 7 %lld + %lld; day 20 %lld + %lld; day 0 interest %lld; day 40 of "
								 "1,000,000 interest %lld; tier 2 %lld + %lld; raised Pax %lld; no node %lld"),
							Seventh.Pax, Seventh.Interest, Twentieth.Pax, Twentieth.Interest, DayNought.Interest,
							Capped.Interest, TierTwo.Pax, TierTwo.Interest, WithWealth.Pax, NoNode.Total()));

	TestEqual(TEXT("Pax Imperialis every day: 615"), Seventh.Pax, static_cast<int64>(615));
	TestEqual(TEXT("no interest on day 7"), Seventh.Interest, static_cast<int64>(0));
	TestEqual(TEXT("interest on day 20: 500"), Twentieth.Interest, static_cast<int64>(500));
	TestEqual(TEXT("and none on day 0"), DayNought.Interest, static_cast<int64>(0));
	TestEqual(TEXT("of 1,000,000, held at 5,000"), Capped.Interest, static_cast<int64>(5000));
	TestEqual(TEXT("tier 2: Pax 1,845"), TierTwo.Pax, static_cast<int64>(1845));
	TestEqual(TEXT("and the cap 15,000"), TierTwo.Interest, static_cast<int64>(15000));
	TestEqual(TEXT("raised by Untouchable Wealth's 0.40: 861"), WithWealth.Pax, static_cast<int64>(861));
	TestEqual(TEXT("no node: nothing"), NoNode.Total(), static_cast<int64>(0));
	return true;
}

/**
 * The run calls the day's hook once for every day that passes, with the day's number: three days advanced call it
 * three times, on the three days that follow. Unset, a day calls nothing. Prints "GOLD HELD DAYS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeGoldHeldDaysTest,
	"Cataclysm.EmpireTreeGoldHeld.TheRunCallsTheDaysHookOnceForEveryDay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeGoldHeldDaysTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeGoldHeldTest;

	UCataclysmEmpireRun* Run = QuietHeldRun(2741);
	const int32 Before = Run->Day();
	TArray<int32> Days;
	Run->OnDayPassed = [&Days](UCataclysmEmpireRun& DayRun, int32 Day) { Days.Add(Day); };
	Run->AdvanceDays(3);
	Run->OnDayPassed = nullptr;
	Run->AdvanceDay();
	AddInfo(FString::Printf(TEXT("GOLD HELD DAYS: from day %d, called %d times: %s"), Before, Days.Num(),
							*FString::JoinBy(Days, TEXT(", "), [](int32 Day) { return FString::FromInt(Day); })));

	TestEqual(TEXT("called three times"), Days.Num(), 3);
	TestTrue(TEXT("on the three days that follow"),
			 Days.Num() == 3 && Days[0] == Before + 1 && Days[1] == Before + 2 && Days[2] == Before + 3);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
