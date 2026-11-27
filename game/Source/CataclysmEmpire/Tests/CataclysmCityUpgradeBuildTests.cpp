// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmCityUpgrade.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Tests/CataclysmTestRun.h"

/**
 * A city upgrade takes days to build. Issue #2825: the owner's reversal of 2026-11-21 ("They need build times.
 * Probably based on their tier.") and the coordinating session's rulings the same day -- 6, 12 and 24 days by the
 * upgrade's tier; the slot taken and nothing done until it is finished; a Siege or a fall pausing it; one build at a
 * time per city.
 *
 * THE PILLAR FOR EVERY TIMED CHECK, for `Cataclysm.CityUpgrade.`'s reason: no surge ever targets it while it is sealed,
 * so a dungeon cannot land on the city under test and pause or bite it. Each test asserts that afterwards. The fall is
 * the exception -- the Pillar falling ends a run -- and uses a city with nothing standing on it, asserted the same way.
 *
 * WHAT THE BUILD DAYS ARE PASSED AS. `UCataclysmEmpireRun::BuyCityUpgrade` takes them; the game passes
 * `UCataclysmCityUpgradePurchase::BuildDaysIn`, which `Cataclysm.CityScreen.` and the Python checks cover. These pass
 * the tier's figure, `UCataclysmCityUpgradeRules::BuildDaysFor(1)`, so a change to it moves these too.
 */

namespace CataclysmCityUpgradeBuildTest
{
	UCataclysmEmpireRun* MakeRun()
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		const CataclysmTestRun::FBuiltAsWritten BuiltAsWritten1; // Issue #2928: the Cataclysms this run faces are named.
		Run->Begin(/* InSeed */ 12345, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
		return Run;
	}

	FCataclysmCityUpgrade Make(ECataclysmCityUpgradeEffect Effect, float Value, FName RowName,
							   float IntervalDays = 0.0f)
	{
		FCataclysmCityUpgrade Upgrade;
		Upgrade.RowName = RowName;
		Upgrade.Effect = Effect;
		Upgrade.Value = Value;
		Upgrade.IntervalDays = IntervalDays;
		return Upgrade;
	}

	/** A Siege on a city with a long timer, placed as `Cataclysm.CityUpgrade.`'s `PlaceDungeon` places a dungeon. */
	void PlaceSiege(UCataclysmEmpireRun& Run, int32 CityId, int32 DungeonId)
	{
		FCataclysmDungeon Dungeon;
		Dungeon.DungeonId = DungeonId;
		Dungeon.CityId = CityId;
		Dungeon.Floors = 10;
		Dungeon.ResolveDays = 500.0f;
		Dungeon.SubType = ECataclysmDungeonSubType::Siege;
		Run.Dungeons.Add(Dungeon);
		Run.Clock->AddDungeon(DungeonId, Dungeon.Floors);
		Run.Clock->SetResolveDays(DungeonId, Dungeon.ResolveDays);
	}

	int32 DaysLeftOn(const UCataclysmEmpireRun& Run, int32 CityId)
	{
		const FCataclysmCityUpgrade* Build = Run.BuildOn(CityId);
		return Build != nullptr ? Build->BuildDaysLeft : -1;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeBuildActsWhenFinishedTest,
	"Cataclysm.CityUpgradeBuild.AnUpgradeDoesNothingUntilItsDaysHavePassed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A raised maximum bought with tier 1's 6 days takes its slot at once and raises nothing for five days; on the sixth it
 * is finished, raises the maximum, is stamped with that day and is named in that day's report. Prints "FINISHED".
 */
bool FCataclysmCityUpgradeBuildActsWhenFinishedTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradeBuildTest;

	UCataclysmEmpireRun* Run = MakeRun();
	const int32 CityId = Run->Map->PillarId;
	const int32 Days = UCataclysmCityUpgradeRules::BuildDaysFor(1);
	const float MaxBefore = Run->Map->Find(CityId)->MaxDefence;
	const int32 BoughtOn = Run->Day();

	const ECataclysmCityUpgradeResult Result = Run->BuyCityUpgrade(
		CityId, Make(ECataclysmCityUpgradeEffect::MaxDefence, 0.2f, TEXT("Test_Walls")), 0, 0, Days);
	const int32 FreeAfterPurchase = Run->Map->FreeUpgradeSlots(CityId);
	const int32 HeldAfterPurchase = Run->Map->Find(CityId)->Upgrades.Num();
	const int32 ReadyDay = Run->BuildReadyDay(CityId);

	Run->AdvanceDays(Days - 1);
	const float MaxTheDayBefore = Run->Map->Find(CityId)->MaxDefence;
	const int32 LeftTheDayBefore = DaysLeftOn(*Run, CityId);

	const FCataclysmDayReport Last = Run->AdvanceDay();
	const FCataclysmCity* City = Run->Map->Find(CityId);

	AddInfo(FString::Printf(TEXT("FINISHED: %s on day %d with %d days, ready on day %d; %d slot(s) free, %d finished; "
								 "the day before, %.0f of max %.0f and %d day(s) left; on day %d, max %.0f, %d finished, "
								 "stamped day %d, the report naming %d city(ies)"),
							*UCataclysmCityUpgradeRules::ResultText(Result), BoughtOn, Days, ReadyDay, FreeAfterPurchase,
							HeldAfterPurchase, MaxTheDayBefore, MaxBefore, LeftTheDayBefore, Last.Day,
							City->MaxDefence, City->Upgrades.Num(),
							City->Upgrades.Num() > 0 ? City->Upgrades[0].BoughtDay : -1,
							Last.UpgradesFinishedOn.Num()));

	TestEqual(TEXT("tier 1 takes 6 days, the derived figure"), Days, 6);
	TestEqual(TEXT("it is bought"), static_cast<int32>(Result), static_cast<int32>(ECataclysmCityUpgradeResult::Bought));
	TestEqual(TEXT("its slot is taken at once"), FreeAfterPurchase, UCataclysmCityUpgradeRules::SlotsNormally - 1);
	TestEqual(TEXT("and it is not finished"), HeldAfterPurchase, 0);
	TestEqual(TEXT("it will be ready on the purchase day plus its days"), ReadyDay, BoughtOn + Days);
	TestEqual(TEXT("the day before, the maximum is untouched"), MaxTheDayBefore, MaxBefore, 0.01f);
	TestEqual(TEXT("with one day left"), LeftTheDayBefore, 1);
	TestEqual(TEXT("on its last day the maximum is raised by a fifth"), City->MaxDefence, MaxBefore * 1.2f, 0.5f);
	TestEqual(TEXT("it is finished"), City->Upgrades.Num(), 1);
	TestTrue(TEXT("and no longer being built"), City->Building.IsEmpty());
	if (City->Upgrades.Num() == 1)
	{
		TestEqual(TEXT("stamped with the day it was finished, which Enduring Foundations reads"),
				  City->Upgrades[0].BoughtDay, Last.Day);
		TestEqual(TEXT("with no days left on it"), City->Upgrades[0].BuildDaysLeft, 0);
	}
	TestEqual(TEXT("that day's report names one city"), Last.UpgradesFinishedOn.Num(), 1);
	TestTrue(TEXT("the Pillar"), Last.UpgradesFinishedOn.Contains(CityId));
	TestEqual(TEXT("its slot stays taken"), Run->Map->FreeUpgradeSlots(CityId),
			  UCataclysmCityUpgradeRules::SlotsNormally - 1);

	TestEqual(TEXT("CONTROL: no dungeon ever stood on the Pillar"), Run->DungeonsOn(CityId).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeBuildOneAtATimeTest,
	"Cataclysm.CityUpgradeBuild.ACityBuildsOneUpgradeAtATime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * While one upgrade is being built a second on the same city is refused with `BuildUnderWay`, the one being built is
 * refused as already bought, and another city may build at the same time; once the first is finished the second is
 * sold. A purchase with no build days is finished at once, as the empire layer's other tests buy. Prints "ONE AT A
 * TIME".
 */
bool FCataclysmCityUpgradeBuildOneAtATimeTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradeBuildTest;

	UCataclysmEmpireRun* Run = MakeRun();
	const int32 CityId = Run->Map->PillarId;
	const int32 Days = UCataclysmCityUpgradeRules::BuildDaysFor(1);
	const FCataclysmCityUpgrade Walls = Make(ECataclysmCityUpgradeEffect::MaxDefence, 0.2f, TEXT("Test_Walls"));
	const FCataclysmCityUpgrade Homes = Make(ECataclysmCityUpgradeEffect::MaxPopulation, 0.2f, TEXT("Test_Homes"));

	Run->BuyCityUpgrade(CityId, Walls, 0, 0, Days);
	const ECataclysmCityUpgradeResult Second = Run->WouldBuyCityUpgrade(CityId, Homes);
	const ECataclysmCityUpgradeResult Same = Run->WouldBuyCityUpgrade(CityId, Walls);

	// ANOTHER CITY, AT THE SAME TIME: any number of cities may build at once.
	int32 OtherId = INDEX_NONE;
	for (const FCataclysmCity& City : Run->Map->Cities)
	{
		if (City.CityId != CityId && !City.bFallen && !Run->IsBesieged(City.CityId))
		{
			OtherId = City.CityId;
			break;
		}
	}
	const ECataclysmCityUpgradeResult Elsewhere =
		OtherId != INDEX_NONE ? Run->BuyCityUpgrade(OtherId, Homes, 0, 0, Days) : ECataclysmCityUpgradeResult::NoSuchCity;

	Run->AdvanceDays(Days);
	const ECataclysmCityUpgradeResult AfterwardsAsked = Run->WouldBuyCityUpgrade(CityId, Homes);

	// NOUGHT DAYS IS FINISHED AT ONCE.
	const ECataclysmCityUpgradeResult AtOnce = Run->BuyCityUpgrade(CityId, Homes);
	const FCataclysmCity* City = Run->Map->Find(CityId);

	AddInfo(FString::Printf(TEXT("ONE AT A TIME: a second while building %s; the same one %s; city %d at once %s; "
								 "after %d days the second %s; bought with no days %s, %d finished, %d building, \"%s\""),
							*UCataclysmCityUpgradeRules::ResultText(Second), *UCataclysmCityUpgradeRules::ResultText(Same),
							OtherId, *UCataclysmCityUpgradeRules::ResultText(Elsewhere), Days,
							*UCataclysmCityUpgradeRules::ResultText(AfterwardsAsked),
							*UCataclysmCityUpgradeRules::ResultText(AtOnce), City->Upgrades.Num(), City->Building.Num(),
							*UCataclysmCityUpgradeRules::ResultText(ECataclysmCityUpgradeResult::BuildUnderWay)));

	TestEqual(TEXT("a second upgrade on a city that is building is refused"), static_cast<int32>(Second),
			  static_cast<int32>(ECataclysmCityUpgradeResult::BuildUnderWay));
	TestEqual(TEXT("the one being built is already bought"), static_cast<int32>(Same),
			  static_cast<int32>(ECataclysmCityUpgradeResult::AlreadyBought));
	TestNotEqual(TEXT("set-up: another city to build on"), OtherId, static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("another city builds at the same time"), static_cast<int32>(Elsewhere),
			  static_cast<int32>(ECataclysmCityUpgradeResult::Bought));
	TestEqual(TEXT("once the first is finished the second is sold"), static_cast<int32>(AfterwardsAsked),
			  static_cast<int32>(ECataclysmCityUpgradeResult::Bought));
	TestEqual(TEXT("a purchase with no build days goes through"), static_cast<int32>(AtOnce),
			  static_cast<int32>(ECataclysmCityUpgradeResult::Bought));
	TestEqual(TEXT("and is finished at once, beside the first"), City->Upgrades.Num(), 2);
	TestTrue(TEXT("with nothing being built"), City->Building.IsEmpty());
	TestTrue(TEXT("the refusal has words of its own"),
			 UCataclysmCityUpgradeRules::ResultText(ECataclysmCityUpgradeResult::BuildUnderWay).Contains(TEXT("one at a time")));

	TestEqual(TEXT("CONTROL: no dungeon ever stood on the Pillar"), Run->DungeonsOn(CityId).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeBuildPausedTest,
	"Cataclysm.CityUpgradeBuild.ASiegeOrAFallPausesTheBuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A Siege on the city counts no day of its build, and the build goes on once the Siege is gone; a fallen city counts
 * none either, and goes on once it is retaken. While paused the build has no ready day. Prints "PAUSED".
 */
bool FCataclysmCityUpgradeBuildPausedTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradeBuildTest;

	UCataclysmEmpireRun* Run = MakeRun();
	const int32 Days = UCataclysmCityUpgradeRules::BuildDaysFor(1);
	const int32 PillarId = Run->Map->PillarId;
	const int32 SiegeId = 9001;

	// THE SIEGE, ON THE PILLAR: bought, then besieged, then three days pass.
	Run->BuyCityUpgrade(PillarId, Make(ECataclysmCityUpgradeEffect::MaxDefence, 0.2f, TEXT("Test_Walls")), 0, 0, Days);
	PlaceSiege(*Run, PillarId, SiegeId);
	const bool bSiegePaused = Run->IsBuildPaused(PillarId);
	const int32 SiegeReadyDay = Run->BuildReadyDay(PillarId);
	Run->AdvanceDays(3);
	const int32 LeftUnderTheSiege = DaysLeftOn(*Run, PillarId);
	// CLEARED, the public way a dungeon leaves the board; what the clear pays does not touch the build.
	Run->ClearDungeon(SiegeId);
	Run->AdvanceDay();
	const int32 LeftAfterTheSiege = DaysLeftOn(*Run, PillarId);

	// THE FALL, ON A CITY WITH NOTHING STANDING ON IT: bought, felled, three days pass, retaken, one day.
	int32 QuietId = INDEX_NONE;
	for (const FCataclysmCity& City : Run->Map->Cities)
	{
		if (City.CityId != PillarId && !City.bFallen && Run->DungeonsOn(City.CityId).IsEmpty())
		{
			QuietId = City.CityId;
			break;
		}
	}
	if (!TestNotEqual(TEXT("set-up: a city with nothing standing on it"), QuietId, static_cast<int32>(INDEX_NONE)))
	{
		return false;
	}
	Run->BuyCityUpgrade(QuietId, Make(ECataclysmCityUpgradeEffect::MaxPopulation, 0.2f, TEXT("Test_Homes")), 0, 0, Days);
	Run->Map->Fall(QuietId);
	const bool bFallPaused = Run->IsBuildPaused(QuietId);
	Run->AdvanceDays(3);
	const int32 LeftWhileFallen = DaysLeftOn(*Run, QuietId);
	Run->Map->Retake(QuietId);
	Run->AdvanceDay();
	const int32 LeftAfterTheRetake = DaysLeftOn(*Run, QuietId);
	const bool bQuietStayedQuiet = !Run->IsBesieged(QuietId);

	AddInfo(FString::Printf(TEXT("PAUSED: besieged %s, ready day %d, %d left after three days, %d after the Siege's day; "
								 "city %d fallen %s, %d left after three days, %d after the retake's day"),
							bSiegePaused ? TEXT("paused") : TEXT("NOT paused"), SiegeReadyDay, LeftUnderTheSiege,
							LeftAfterTheSiege, QuietId, bFallPaused ? TEXT("paused") : TEXT("NOT paused"),
							LeftWhileFallen, LeftAfterTheRetake));

	TestTrue(TEXT("a Siege pauses the build"), bSiegePaused);
	TestEqual(TEXT("and a paused build has no ready day"), SiegeReadyDay, static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("three days under the Siege count none"), LeftUnderTheSiege, Days);
	TestEqual(TEXT("the day after the Siege is gone counts one"), LeftAfterTheSiege, Days - 1);
	TestTrue(TEXT("a fall pauses the build"), bFallPaused);
	TestEqual(TEXT("three days fallen count none"), LeftWhileFallen, Days);
	TestEqual(TEXT("the day after the retake counts one"), LeftAfterTheRetake, Days - 1);
	TestTrue(TEXT("CONTROL: no Siege reached the retaken city"), bQuietStayedQuiet);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeBuildIntervalTest,
	"Cataclysm.CityUpgradeBuild.AnIntervalUpgradeFirstFiresAFullIntervalAfterItIsFinished",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A repair every 20 days bought with 6 days to build first fires 20 days after it is finished, on day 26, not 20 days
 * after it was bought. Prints "INTERVAL".
 */
bool FCataclysmCityUpgradeBuildIntervalTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradeBuildTest;

	UCataclysmEmpireRun* Run = MakeRun();
	const int32 CityId = Run->Map->PillarId;
	const int32 Days = UCataclysmCityUpgradeRules::BuildDaysFor(1);

	Run->Map->Bite(CityId, 0.5f, 0.5f);
	const float Damaged = Run->Map->Find(CityId)->Defence;
	Run->BuyCityUpgrade(CityId, Make(ECataclysmCityUpgradeEffect::HealDefenceEvery, 0.05f, TEXT("Test_Heal"), 20.0f),
						0, 0, Days);

	Run->AdvanceDays(Days);
	const FCataclysmCity* City = Run->Map->Find(CityId);
	const int32 NextTrigger = City->Upgrades.Num() == 1 ? City->Upgrades[0].NextTriggerDay : -1;

	Run->AdvanceDays(19);
	const float TheDayBefore = Run->Map->Find(CityId)->Defence;
	Run->AdvanceDay();
	const float OnTheDay = Run->Map->Find(CityId)->Defence;

	AddInfo(FString::Printf(TEXT("INTERVAL: finished on day %d with its next trigger on day %d; day %d %.1f, day %d %.1f "
								 "(damaged %.1f)"),
							Days, NextTrigger, Run->Day() - 1, TheDayBefore, Run->Day(), OnTheDay, Damaged));

	TestEqual(TEXT("its first trigger is 20 days after it is finished"), NextTrigger, Days + 20);
	TestEqual(TEXT("nothing is repaired on day 25"), TheDayBefore, Damaged, 0.01f);
	TestTrue(TEXT("and on day 26 it repairs"), OnTheDay > TheDayBefore + 0.01f);
	TestEqual(TEXT("CONTROL: no dungeon ever stood on the Pillar"), Run->DungeonsOn(CityId).Num(), 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
