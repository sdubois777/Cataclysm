// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/CataclysmCityUpgradeMapping.h"
#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Interface/CataclysmCityScreenLayout.h"
#include "Player/CataclysmCityUpgradePurchase.h"
#include "Player/CataclysmPlayLoopLog.h"
#include "Tests/CataclysmTestRun.h"

/**
 * What the screens and the play log say about a city upgrade being built. Issue #2825, ruled 2026-11-21: "every
 * system needs a basic UI". The empire layer's `Cataclysm.CityUpgradeBuild.` tests what a build does; these test the
 * words, which the automation command's `-nullrhi` cannot draw, so the layout functions are read directly.
 *
 * THE PILLAR, as there: no surge ever targets it while it is sealed.
 */

namespace CataclysmCityUpgradeBuildScreenTest
{
	const TCHAR* const Row = TEXT("Architect_Increase_max_defense_by_20");

	UCataclysmEmpireRun* MakeRun()
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		const CataclysmTestRun::FBuiltAsWritten BuiltAsWritten1; // Issue #2928: the Cataclysms this run faces are named.
		Run->Begin(/* InSeed */ 12345, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
		return Run;
	}

	/** Bought as the game buys it, with the days `BuildDaysIn` gives, and free, as the empire layer's tests buy. */
	ECataclysmCityUpgradeResult BuyWithItsDays(UCataclysmEmpireRun& Run, int32 CityId, FName RowName)
	{
		return Run.BuyCityUpgrade(CityId, UCataclysmCityUpgradeMapping::MakeFromTable(RowName), 0, 0,
								  UCataclysmCityUpgradePurchase::BuildDaysIn(nullptr, &Run, RowName, CityId));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeBuildScreenListedTest,
	"Cataclysm.CityScreen.ABuildUnderWayIsListedAndTheOtherOffersWaitForIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * With "Increase max defense by 20%" being built on the Pillar: the city's list ends with it, its days left and its
 * ready day; its slot is counted; every other upgrade the game has built is a greyed offer saying a city builds one at
 * a time; a city building nothing shows the price and the days on each button; and a Siege on the Pillar turns the
 * line's ready day into the pause and its reason. Prints "BUILD LISTED".
 */
bool FCataclysmCityUpgradeBuildScreenListedTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradeBuildScreenTest;

	UCataclysmEmpireRun* Run = MakeRun();
	const int32 PillarId = Run->Map->PillarId;
	const FName RowName(Row);
	const FString Effect = UCataclysmCityUpgradeMapping::EffectTextFor(RowName);
	if (!TestFalse(TEXT("set-up: the DataTable names the upgrade"), Effect.IsEmpty()))
	{
		return false;
	}

	const ECataclysmCityUpgradeResult Result = BuyWithItsDays(*Run, PillarId, RowName);
	const TArray<FString> Held = UCataclysmCityScreenLayout::HeldLinesFor(Run, PillarId);
	const FString Line = UCataclysmCityScreenLayout::BuildLineFor(Run, PillarId);
	const FString Slots = UCataclysmCityScreenLayout::SlotsTextFor(Run, PillarId);
	const FString Waiting = UCataclysmCityUpgradeRules::ResultText(ECataclysmCityUpgradeResult::BuildUnderWay);

	// EVERY OTHER BUILT UPGRADE ON THE PILLAR WAITS FOR THE BUILD, with gold enough that only the build stops it.
	int32 Built = 0;
	int32 SaidWaiting = 0;
	FString WaitingButton;
	for (const FCataclysmCityUpgradeOffer& Offer :
		 UCataclysmCityScreenLayout::OffersFor(nullptr, Run, PillarId, 100000000))
	{
		// THE CLEANSE IS SET ASIDE: below difficulty tier 3 it says its tier, which is checked before the build
		// (issue #2838).
		if (Offer.RowName == RowName || !UCataclysmCityUpgradeRules::IsBuilt(Offer.EffectKind)
			|| Offer.EffectKind == ECataclysmCityUpgradeEffect::CleanseEveryCity)
		{
			continue;
		}
		++Built;
		if (Offer.bWaitsOnABuild && !Offer.bCanBuy && Offer.Refusal == Waiting)
		{
			++SaidWaiting;
			WaitingButton = UCataclysmCityScreenLayout::ButtonTextFor(Offer);
		}
	}

	// A CITY BUILDING NOTHING: the price and the days on the button.
	int32 OtherId = INDEX_NONE;
	for (const FCataclysmCity& City : Run->Map->Cities)
	{
		if (City.CityId != PillarId && !City.bFallen && !Run->IsBesieged(City.CityId))
		{
			OtherId = City.CityId;
			break;
		}
	}
	FString OtherButton;
	int32 OtherDays = 0;
	for (const FCataclysmCityUpgradeOffer& Offer :
		 UCataclysmCityScreenLayout::OffersFor(nullptr, Run, OtherId, 100000000))
	{
		if (Offer.RowName == RowName)
		{
			OtherButton = UCataclysmCityScreenLayout::ButtonTextFor(Offer);
			OtherDays = Offer.BuildDays;
		}
	}

	// A SIEGE ON THE PILLAR PAUSES IT, and the line says so.
	FCataclysmDungeon Siege;
	Siege.DungeonId = 9001;
	Siege.CityId = PillarId;
	Siege.Floors = 10;
	Siege.ResolveDays = 500.0f;
	Siege.SubType = ECataclysmDungeonSubType::Siege;
	Run->Dungeons.Add(Siege);
	Run->Clock->AddDungeon(Siege.DungeonId, Siege.Floors);
	Run->Clock->SetResolveDays(Siege.DungeonId, Siege.ResolveDays);
	const FString PausedLine = UCataclysmCityScreenLayout::BuildLineFor(Run, PillarId);

	const FString Expected = FString::Printf(TEXT("%s  --  being built, 6 days left, ready on day %d"), *Effect,
											 Run->Day() + 6);
	AddInfo(FString::Printf(TEXT("BUILD LISTED: %s; held %d line(s), the last \"%s\"; \"%s\"; %d of %d built upgrades "
								 "wait, e.g. \"%s\"; city %d's button \"%s\" (%d days); under a Siege \"%s\""),
							*UCataclysmCityUpgradeRules::ResultText(Result), Held.Num(),
							Held.IsEmpty() ? TEXT("") : *Held.Last(), *Slots, SaidWaiting, Built, *WaitingButton,
							OtherId, *OtherButton, OtherDays, *PausedLine));

	TestEqual(TEXT("it is bought"), static_cast<int32>(Result), static_cast<int32>(ECataclysmCityUpgradeResult::Bought));
	TestEqual(TEXT("the build line names it, its days left and its ready day"), Line, Expected);
	TestEqual(TEXT("the city's list holds only the build"), Held.Num(), 1);
	TestTrue(TEXT("and ends with it"), !Held.IsEmpty() && Held.Last() == Line);
	TestEqual(TEXT("its slot is counted"), Slots, FString(TEXT("1 of 3 upgrade slots filled.")));
	TestTrue(TEXT("set-up: other built upgrades to offer"), Built > 0);
	TestEqual(TEXT("every other built upgrade waits for the build"), SaidWaiting, Built);
	TestTrue(TEXT("its greyed button says why"), WaitingButton.EndsWith(TEXT("  --  ") + Waiting));
	TestNotEqual(TEXT("set-up: a city building nothing"), OtherId, static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("an offer there carries tier 1's days"), OtherDays, 6);
	TestTrue(TEXT("and its button the price and the days"), OtherButton.EndsWith(TEXT("  --  10,000 gold, 6 days to build")));
	TestEqual(TEXT("under a Siege the line says it is paused and why"), PausedLine,
			  FString::Printf(TEXT("%s  --  being built, 6 days left, paused while a Siege dungeon stands on this city"),
							  *Effect));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeBuildScreenLogTest,
	"Cataclysm.CityScreen.ThePlayLogSaysWhenABuildWillBeReadyAndWhenItWasFinished",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The purchase line ends with the ready day for a build and with nothing for an upgrade finished at once; the capital's
 * account of the days away names the city whose upgrade was finished, and says nothing of upgrades when none was.
 * Prints "BUILD LOGGED".
 */
bool FCataclysmCityUpgradeBuildScreenLogTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradeBuildScreenTest;

	UCataclysmEmpireRun* Run = MakeRun();
	const int32 PillarId = Run->Map->PillarId;
	const FString Pillar = Run->Map->Find(PillarId)->Name;

	const FString Building = FCataclysmPlayLoopLog::UpgradeLine(TEXT("Increase max defense by 20%"), Run, PillarId,
																10000, 15000, 57);
	const FString AtOnce = FCataclysmPlayLoopLog::UpgradeLine(TEXT("Increase max defense by 20%"), Run, PillarId, 10000,
															  15000);

	FCataclysmDayReport Finished;
	Finished.Day = 6;
	Finished.UpgradesFinishedOn.Add(PillarId);
	FCataclysmDayReport Quiet;
	Quiet.Day = 7;
	const FString Away = FCataclysmPlayLoopLog::AwayAccount(5, 7, { Finished, Quiet }, {}, Run);
	const FString AwayQuiet = FCataclysmPlayLoopLog::AwayAccount(6, 7, { Quiet }, {}, Run);

	AddInfo(FString::Printf(TEXT("BUILD LOGGED: \"%s\"; \"%s\"; \"%s\"; \"%s\""), *Building, *AtOnce, *Away, *AwayQuiet));

	TestEqual(TEXT("a build's purchase line ends with its ready day"), Building,
			  FString::Printf(TEXT("bought 'Increase max defense by 20%%' on %s for 10,000 gold; 15,000 gold held; "
								   "being built, ready on day 57"), *Pillar));
	TestEqual(TEXT("one finished at once says nothing more"), AtOnce,
			  FString::Printf(TEXT("bought 'Increase max defense by 20%%' on %s for 10,000 gold; 15,000 gold held"),
							  *Pillar));
	TestTrue(TEXT("the days away name the city whose upgrade was finished"),
			 Away.EndsWith(FString::Printf(TEXT("; city upgrades finished: %s"), *Pillar)));
	TestFalse(TEXT("and days that finished none say nothing of upgrades"), AwayQuiet.Contains(TEXT("upgrades finished")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
