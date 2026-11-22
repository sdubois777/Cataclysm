// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/CataclysmCityUpgradeMapping.h"
#include "Empire/CataclysmCityUpgrade.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Interface/CataclysmCityScreenLayout.h"
#include "Player/CataclysmCityUpgradePurchase.h"
#include "Save/CataclysmSaveApply.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "UObject/Package.h"

/**
 * Raising a bought city upgrade, as the game module offers, prices and saves it. Issue #1265, the coordinating
 * session's rulings of 2026-11-22. READ FROM THE GENERATED TABLE, `DT_CityUpgrades`: the rows' tier 2 and 3 figures and
 * their Tier 2 and Tier 3 Gold are what a raise reads.
 */

namespace CataclysmCityUpgradeRaiseScreenTest
{
	const FName RemoveRow(TEXT("Architect_Remove_25_of_dungeons_on_this_city"));
	const FName WallsRow(TEXT("Architect_Increase_max_defense_by_20"));
	const FName CleanseRow(TEXT("Unbranched_Cleanse_every_player_city_of_half_of_the"));
	constexpr int64 PlentyOfGold = 100000000;

	UCataclysmEmpireRun* MakeRun(int32 DifficultyTier = 1)
	{
		UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
		Run->Begin(/* InSeed */ 1265, ECataclysmSurgeMode::Static, /* LethalityRung */ 0, DifficultyTier);
		return Run;
	}

	const FCataclysmCityUpgradeOffer* OfferFor(const TArray<FCataclysmCityUpgradeOffer>& Offers, FName RowName)
	{
		return Offers.FindByPredicate([RowName](const FCataclysmCityUpgradeOffer& Offer) { return Offer.RowName == RowName; });
	}

	const FCataclysmCityUpgrade* Held(const UCataclysmEmpireRun& Run, int32 CityId, FName RowName)
	{
		const FCataclysmCity* City = Run.Map->Find(CityId);
		return City != nullptr
			? City->Upgrades.FindByPredicate([RowName](const FCataclysmCityUpgrade& One) { return One.RowName == RowName; })
			: nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeRaiseOfferTest,
	"Cataclysm.CityUpgradeRaiseScreen.AHeldUpgradesRaiseSaysWhatItDoesItsPriceAndItsDays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Remove 25% of dungeons on this city" held on the Pillar at difficulty tier 1: its offer is a raise to tier 2 that
 * "removes 50% of this city's dungeons when built", for its Tier 2 Gold, 20,000, and 12 days, the button "Raise to tier
 * 2: removes 50% of this city's dungeons when built  --  20,000 gold, 12 days to build". Raised to tier 3 it is greyed,
 * "Raise  --  Already at tier 3.". An upgrade the city does not hold has no raise. Prints "RAISE OFFER".
 */
bool FCataclysmCityUpgradeRaiseOfferTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradeRaiseScreenTest;

	UCataclysmEmpireRun* Run = MakeRun();
	const int32 PillarId = Run->Map->PillarId;
	const ECataclysmCityUpgradeResult Bought =
		Run->BuyCityUpgrade(PillarId, UCataclysmCityUpgradeMapping::MakeFromTable(RemoveRow));
	const TArray<FCataclysmCityUpgradeOffer> Offers = UCataclysmCityScreenLayout::OffersFor(nullptr, Run, PillarId, PlentyOfGold);
	const FCataclysmCityUpgradeOffer* Remove = OfferFor(Offers, RemoveRow);
	const FCataclysmCityUpgradeOffer* Walls = OfferFor(Offers, WallsRow);
	if (!TestEqual(TEXT("set-up: bought"), Bought, ECataclysmCityUpgradeResult::Bought)
		|| !TestNotNull(TEXT("its offer is shown"), Remove) || !TestNotNull(TEXT("and another's"), Walls))
	{
		return false;
	}
	const FString Button = UCataclysmCityScreenLayout::RaiseButtonTextFor(*Remove);

	Run->RaiseCityUpgrade(PillarId, UCataclysmCityUpgradeMapping::MakeAtTier(RemoveRow, 2));
	Run->RaiseCityUpgrade(PillarId, UCataclysmCityUpgradeMapping::MakeAtTier(RemoveRow, 3));
	const TArray<FCataclysmCityUpgradeOffer> AtTheTop = UCataclysmCityScreenLayout::OffersFor(nullptr, Run, PillarId, PlentyOfGold);
	const FCataclysmCityUpgradeOffer* Top = OfferFor(AtTheTop, RemoveRow);
	const FString TopButton = Top ? UCataclysmCityScreenLayout::RaiseButtonTextFor(*Top) : FString();

	AddInfo(FString::Printf(TEXT("RAISE OFFER: \"%s\"; tier %d, %lld gold, %d days, can %d; at the top \"%s\"; walls "
								 "offered a raise %d"),
							*Button, Remove->RaiseTier, Remove->RaisePrice, Remove->RaiseDays, Remove->bCanRaise ? 1 : 0,
							*TopButton, Walls->bRaiseOffered ? 1 : 0));

	TestTrue(TEXT("a held upgrade is offered a raise"), Remove->bRaiseOffered);
	TestEqual(TEXT("to tier 2"), Remove->RaiseTier, 2);
	TestEqual(TEXT("saying what it does when built"), Remove->RaiseWhat,
			  FString(TEXT("removes 50% of this city's dungeons when built")));
	TestEqual(TEXT("for its Tier 2 Gold"), Remove->RaisePrice, static_cast<int64>(20000));
	TestEqual(TEXT("and 12 days"), Remove->RaiseDays, 12);
	TestTrue(TEXT("which can be paid"), Remove->bCanRaise);
	TestEqual(TEXT("its button"), Button,
			  FString(TEXT("Raise to tier 2: removes 50% of this city's dungeons when built  --  20,000 gold, 12 days to build")));
	TestEqual(TEXT("at tier 3 it says so"), TopButton, FString(TEXT("Raise  --  Already at tier 3.")));
	TestTrue(TEXT("and cannot be pressed"), Top != nullptr && !Top->bCanRaise);
	TestFalse(TEXT("CONTROL: an upgrade not held is offered no raise"), Walls->bRaiseOffered);
	TestEqual(TEXT("RaiseWhatFor says a raised maximum"),
			  UCataclysmCityScreenLayout::RaiseWhatFor(ECataclysmCityUpgradeEffect::MaxDefence, 0.3f, 0.0f),
			  FString(TEXT("raises max defence by 30%")));
	TestEqual(TEXT("and an interval"),
			  UCataclysmCityScreenLayout::RaiseWhatFor(ECataclysmCityUpgradeEffect::HealDefenceEvery, 0.1f, 10.0f),
			  FString(TEXT("every 10 days this city's defences heal 10%")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeRaisePurchaseTest,
	"Cataclysm.CityUpgradeRaiseScreen.ThePurchaseRefusesARaiseItCannotMakeAndTakesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `UCataclysmCityUpgradePurchase::Raise` refuses a row the city does not hold, the cleanse (which has no tier 2), an
 * upgrade at tier 3, and with no balance to pay from, a raise whose price is above nought; each leaves the upgrade at
 * the tier it was. Its price at tier 2 is the row's Tier 2 Gold and its days 12, and at difficulty tier 2 the price is
 * scaled as a purchase's is. Prints "RAISE PURCHASE".
 */
bool FCataclysmCityUpgradeRaisePurchaseTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradeRaiseScreenTest;

	UCataclysmEmpireRun* Run = MakeRun(3);
	const int32 PillarId = Run->Map->PillarId;
	const ECataclysmCityUpgradeResult NotHeld = UCataclysmCityUpgradePurchase::Raise(nullptr, Run, PillarId, WallsRow);
	Run->BuyCityUpgrade(PillarId, UCataclysmCityUpgradeMapping::MakeFromTable(WallsRow));
	const ECataclysmCityUpgradeResult NoBalance = UCataclysmCityUpgradePurchase::Raise(nullptr, Run, PillarId, WallsRow);
	const int32 TierAfterNoBalance = Held(*Run, PillarId, WallsRow) ? Held(*Run, PillarId, WallsRow)->Tier : -1;
	Run->BuyCityUpgrade(PillarId, UCataclysmCityUpgradeMapping::MakeFromTable(CleanseRow));
	const ECataclysmCityUpgradeResult Cleanse = UCataclysmCityUpgradePurchase::Raise(nullptr, Run, PillarId, CleanseRow);
	Run->RaiseCityUpgrade(PillarId, UCataclysmCityUpgradeMapping::MakeAtTier(WallsRow, 2));
	Run->RaiseCityUpgrade(PillarId, UCataclysmCityUpgradeMapping::MakeAtTier(WallsRow, 3));
	const ECataclysmCityUpgradeResult AtTop = UCataclysmCityUpgradePurchase::Raise(nullptr, Run, PillarId, WallsRow);

	UCataclysmEmpireRun* TierOne = MakeRun(1);
	UCataclysmEmpireRun* TierTwo = MakeRun(2);
	const int64 PriceAtOne = UCataclysmCityUpgradePurchase::PriceIn(nullptr, TierOne, WallsRow, 0, 2);
	const int64 PriceAtTwo = UCataclysmCityUpgradePurchase::PriceIn(nullptr, TierTwo, WallsRow, 0, 2);
	const int64 TierThreeAtOne = UCataclysmCityUpgradePurchase::PriceIn(nullptr, TierOne, WallsRow, 0, 3);
	const int32 DaysTwo = UCataclysmCityUpgradePurchase::BuildDaysIn(nullptr, TierOne, WallsRow, 0, false, 2);
	const int32 DaysThree = UCataclysmCityUpgradePurchase::BuildDaysIn(nullptr, TierOne, WallsRow, 0, false, 3);
	const auto Say = [](ECataclysmCityUpgradeResult Result) { return UCataclysmCityUpgradeRules::ResultText(Result); };

	AddInfo(FString::Printf(TEXT("RAISE PURCHASE: not held \"%s\"; no balance \"%s\", tier %d; cleanse \"%s\"; at the top "
								 "\"%s\"; tier 2 %lld at difficulty 1, %lld at 2; tier 3 %lld; days %d and %d"),
							*Say(NotHeld), *Say(NoBalance), TierAfterNoBalance, *Say(Cleanse), *Say(AtTop), PriceAtOne,
							PriceAtTwo, TierThreeAtOne, DaysTwo, DaysThree));

	TestEqual(TEXT("a row not held"), NotHeld, ECataclysmCityUpgradeResult::NotHeld);
	TestEqual(TEXT("no balance to pay from"), NoBalance, ECataclysmCityUpgradeResult::NotEnoughGold);
	TestEqual(TEXT("and the upgrade stays at tier 1"), TierAfterNoBalance, 1);
	TestEqual(TEXT("the cleanse has no tier to raise to"), Cleanse, ECataclysmCityUpgradeResult::NoTierToRaiseTo);
	TestEqual(TEXT("at tier 3"), AtTop, ECataclysmCityUpgradeResult::AtTheTopTier);
	TestEqual(TEXT("tier 2 costs its Tier 2 Gold"), PriceAtOne, static_cast<int64>(20000));
	TestEqual(TEXT("scaled at difficulty tier 2 as a purchase is, three times"), PriceAtTwo, static_cast<int64>(60000));
	TestEqual(TEXT("tier 3 costs its Tier 3 Gold"), TierThreeAtOne, static_cast<int64>(40000));
	TestEqual(TEXT("tier 2 takes 12 days"), DaysTwo, 12);
	TestEqual(TEXT("tier 3 takes 24"), DaysThree, 24);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeRaiseSaveTest,
	"Cataclysm.CityUpgradeRaiseScreen.ARaiseUnderWayIsSavedAndFinishedAfterALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Increase max defense by 20%" held on the Pillar and raised to tier 2 with 12 days to build, two days in: saved and
 * loaded, the upgrade is held at tier 1 and the raise is being built at tier 2 with 10 days left; ten more days finish
 * it, at tier 2 in one slot. Prints "RAISE SAVED".
 */
bool FCataclysmCityUpgradeRaiseSaveTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradeRaiseScreenTest;

	UCataclysmEmpireRun* Run = MakeRun();
	const int32 PillarId = Run->Map->PillarId;
	Run->BuyCityUpgrade(PillarId, UCataclysmCityUpgradeMapping::MakeFromTable(WallsRow));
	const ECataclysmCityUpgradeResult Raised = Run->RaiseCityUpgrade(
		PillarId, UCataclysmCityUpgradeMapping::MakeAtTier(WallsRow, 2), 0, 0, UCataclysmCityUpgradeRules::BuildDaysFor(2));
	Run->AdvanceDays(2);

	UCataclysmRunSave* Record = NewObject<UCataclysmRunSave>();
	Record->SchemaVersion = UCataclysmRunSave::SchemaVersionNow;
	FString Why;
	FCataclysmSaveGather::RunClockFrom(*Run, *Record);
	FCataclysmSaveGather::CitiesFrom(*Run, *Record);
	FCataclysmSaveGather::SurgeScheduleFrom(*Run, *Record);
	FCataclysmSaveGather::ProgressFrom(*Run, *Record);
	FCataclysmSaveGather::IdentityFrom(*Run, *Record);
	FString Json;
	const bool bWritten = FCataclysmSaveGather::DungeonsFrom(*Run, *Record, Why) && FCataclysmSaveStorage::ToJson(Record, Json, Why);
	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	const UCataclysmRunSave* Read = bWritten ? Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
		Json, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, Why)) : nullptr;
	UCataclysmEmpireRun* Loaded = NewObject<UCataclysmEmpireRun>();
	const bool bLoaded = Read != nullptr && FCataclysmSaveApply::RunInto(*Read, *Loaded, Why);
	if (!TestTrue(TEXT("set-up: saved and loaded"), bLoaded))
	{
		AddError(Why);
		return false;
	}

	const FCataclysmCityUpgrade* Build = Loaded->BuildOn(PillarId);
	const int32 BuildTier = Build ? Build->Tier : -1;
	const int32 DaysLeft = Build ? Build->BuildDaysLeft : -1;
	const int32 HeldTier = Held(*Loaded, PillarId, WallsRow) ? Held(*Loaded, PillarId, WallsRow)->Tier : -1;
	Loaded->AdvanceDays(10);
	const FCataclysmCity* Pillar = Loaded->Map->Find(PillarId);
	const int32 FinishedTier = Held(*Loaded, PillarId, WallsRow) ? Held(*Loaded, PillarId, WallsRow)->Tier : -1;

	AddInfo(FString::Printf(TEXT("RAISE SAVED: raise \"%s\"; loaded, held at tier %d, building tier %d with %d days left; "
								 "ten days later tier %d, %d held, %d building"),
							*UCataclysmCityUpgradeRules::ResultText(Raised), HeldTier, BuildTier, DaysLeft, FinishedTier,
							Pillar ? Pillar->Upgrades.Num() : -1, Pillar ? Pillar->Building.Num() : -1));

	TestEqual(TEXT("set-up: the raise is accepted"), Raised, ECataclysmCityUpgradeResult::Bought);
	TestEqual(TEXT("the loaded city holds it at tier 1"), HeldTier, 1);
	TestEqual(TEXT("and is building tier 2"), BuildTier, 2);
	TestEqual(TEXT("with 10 days left"), DaysLeft, 10);
	TestEqual(TEXT("ten more days finish it at tier 2"), FinishedTier, 2);
	TestTrue(TEXT("in one slot, with nothing building"), Pillar && Pillar->Upgrades.Num() == 1 && Pillar->Building.IsEmpty());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
