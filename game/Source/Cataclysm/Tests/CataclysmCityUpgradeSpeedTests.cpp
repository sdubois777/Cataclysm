// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmCityUpgradeMapping.h"
#include "Empire/CataclysmCityUpgrade.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Interface/CataclysmCityScreenLayout.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmCityUpgradePurchase.h"
#include "Player/CataclysmEmpireTree.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayLoopLog.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSavePartition.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Save/CataclysmSaveWriter.h"
#include "Tests/CataclysmTestWorld.h"
#include "Tests/CataclysmTestRun.h"

/**
 * The empire upgrade tree's three build nodes, issue #2832, ruled by the coordinating session on 2026-11-21:
 * Scaffolding (a city upgrade's price × 0.5 and its days × 2), Rapid Renovation (1 day fewer a point) and Rapid
 * Funding (a Rush on each offer, at 150% of the price after every discount, for 2 days fewer); the doubling first, then
 * the days taken off, then the floor of 1 day; and the days fixed at purchase.
 *
 * Written before the change. The price nodes of slice 6a are `Cataclysm.EmpireTreeGold.` and
 * `Cataclysm.CityUpgradePrice.`; the build times themselves are #2825's `Cataclysm.CityUpgradeBuild.`.
 */
namespace CataclysmCityUpgradeSpeedTest
{
	/** The upgrade every purchase here buys: built, the sheet's first row, 10,000 gold and 6 days at tier 1. */
	const TCHAR* const Row = TEXT("Architect_Increase_max_defense_by_20");

	const FName Scaffolding(TEXT("keystone_0000000z"));		// Architect keystone, 1 point
	const FName RapidRenovation(TEXT("basic_arch_t2g"));	// Architect, 3 points
	const FName RapidFunding(TEXT("keystone_0000003y"));	// Treasury keystone, 1 point
	const FName MunicipalBonds(TEXT("basic_0000003v"));		// Treasury, -3% a point
	const FName PublicWorks(TEXT("keystone_00000013"));		// Architect keystone, -20%
	const FName SovereignLoans(TEXT("basic_tres_t4f"));		// Treasury, 20% a point of a price may be owed

	/** What these nodes add up to, read from the effect table as the game reads it. */
	FCataclysmEmpireTreePlayerBonuses BonusesOf(const FCataclysmPassiveAllocation& Tree)
	{
		return UCataclysmEmpireTree::PlayerBonusesFor(UCataclysmEmpireTree::LoadNodeTable(),
													  UCataclysmEmpireTree::LoadEffectTable(), Tree);
	}

	/** A world with this project's game instance, a possessed player of one partition, and a run on the instance. */
	struct FSpeedScene
	{
		UWorld* World = nullptr;
		UCataclysmGameInstance* Instance = nullptr;
		ACataclysmPlayerState* State = nullptr;
		UCataclysmEmpireRun* Run = nullptr;
		FCataclysmCharacterPartition Partition;

		bool IsUsable() const { return World && Instance && State && Run && Run->Map && Run->Map->Cities.Num() > 1; }

		/** The mode's account, where the empire tree a character plays with is held. */
		UCataclysmAccountSave* Account() const { return Instance ? Instance->AccountFor(Partition) : nullptr; }
	};

	/** Waits for any write to the slot to finish, then deletes it when it exists. */
	void DeleteSlot(const FString& SlotName)
	{
		const double GiveUpAt = FPlatformTime::Seconds() + 2.0;
		while (FCataclysmSaveStorage::HasAWriteInFlight(SlotName) && FPlatformTime::Seconds() < GiveUpAt)
		{
			FPlatformProcess::Sleep(0.01f);
		}
		if (UGameplayStatics::DoesSaveGameExist(SlotName, UCataclysmSaveWriter::UserIndex))
		{
			UGameplayStatics::DeleteGameInSlot(SlotName, UCataclysmSaveWriter::UserIndex);
		}
	}

	/**
	 * The scene: a Standard offline partition, its account slot emptied first, a run begun at difficulty tier 1 on the
	 * game instance, and `Gold` banked. As `CataclysmCityUpgradePriceTests.cpp` builds its scene, without the writer.
	 */
	FSpeedScene MakeScene(int64 Gold)
	{
		FSpeedScene Out;
		Out.Partition.Lethality = ECataclysmLethality::Standard;
		Out.Partition.Population = ECataclysmPopulation::Offline;
		DeleteSlot(UCataclysmGameInstance::AccountSlotFor(Out.Partition));

		Out.World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Out.World)
		{
			return Out;
		}
		Out.Instance = NewObject<UCataclysmGameInstance>();
		Out.World->SetGameInstance(Out.Instance);

		Out.State = Out.World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = Out.World->SpawnActor<APlayerController>();
		ACataclysmPlayerCharacter* Character =
			Out.World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Out.State && Controller && Character)
		{
			Controller->SetPlayerState(Out.State);
			Controller->Possess(Character);
			Out.State->SetIdentity(TEXT("Builder"), Out.Partition);
		}

		Out.Run = NewObject<UCataclysmEmpireRun>(Out.Instance);
		const CataclysmTestRun::FBuiltAsWritten BuiltAsWritten1; // Issue #2928: the Cataclysms this run faces are named.
		Out.Run->Begin(/* InSeed */ 2832, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
		Out.Instance->EmpireRun = Out.Run;
		Out.Instance->BankGold(Out.Partition, Gold);
		return Out;
	}

	/** The gold the scene's character holds, or -1 for none. */
	int64 Held(const UObject* WorldContext)
	{
		int64 Gold = 0;
		return UCataclysmGameInstance::GoldHeldBy(WorldContext, Gold) ? Gold : -1;
	}

	/** The offer for `Row` on `CityId`, or null. */
	const FCataclysmCityUpgradeOffer* OfferFor(const TArray<FCataclysmCityUpgradeOffer>& Offers)
	{
		const FName RowName(Row);
		return Offers.FindByPredicate(
			[&RowName](const FCataclysmCityUpgradeOffer& One) { return One.RowName == RowName; });
	}
}

// ---------------------------------------------------------------------------
// The price and the days, from the nodes alone
// ---------------------------------------------------------------------------

/**
 * Scaffolding, read from the effect table: half the price, a multiplier of its own after slice 6a's reductions are
 * held at their 75% cap, and twice the days. CONTROLS: no node leaves the price factor at exactly 1 and the 6 days at 6.
 * Prints "SCAFFOLDING".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeSpeedScaffoldingTest,
	"Cataclysm.CityUpgradeSpeed.ScaffoldingHalvesThePriceAfterTheReductionsAndDoublesTheDays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradeSpeedScaffoldingTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradeSpeedTest;

	UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
	const CataclysmTestRun::FBuiltAsWritten BuiltAsWritten2; // Issue #2928: the Cataclysms this run faces are named.
	Run->Begin(/* InSeed */ 2832, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
	if (!TestTrue(TEXT("set-up: a run with a map"), Run->Map != nullptr && Run->Map->Cities.Num() > 1))
	{
		return false;
	}
	UCataclysmEmpireMap* Map = Run->Map;
	FCataclysmCity& Holding = Map->Cities[0];
	Holding.Upgrades.AddDefaulted(1);

	FCataclysmPassiveAllocation Tree;
	Tree.Add(Scaffolding, 1);
	const FCataclysmEmpireTreePlayerBonuses Scaffolded = BonusesOf(Tree);

	FCataclysmPassiveAllocation Every;
	Every.Add(Scaffolding, 1);
	Every.Add(MunicipalBonds, 15);
	Every.Add(PublicWorks, 1);
	const FCataclysmEmpireTreePlayerBonuses All = BonusesOf(Every);
	const FCataclysmEmpireTreePlayerBonuses None;

	const float Alone = UCataclysmEmpireTree::UpgradePriceFactorFor(Scaffolded, Map, Holding.CityId);
	const float WithTheRest = UCataclysmEmpireTree::UpgradePriceFactorFor(All, Map, Holding.CityId);
	const float NoNode = UCataclysmEmpireTree::UpgradePriceFactorFor(None, Map, Holding.CityId);
	const int32 Days = UCataclysmEmpireTree::UpgradeBuildDaysFor(Scaffolded, 6, /*bRush=*/false);
	const int32 NoNodeDays = UCataclysmEmpireTree::UpgradeBuildDaysFor(None, 6, /*bRush=*/false);
	AddInfo(FString::Printf(TEXT("SCAFFOLDING: less %.2f, days x%.2f; factor alone %.4f, with Bonds and Public Works "
								 "%.4f, none %.4f; 6 days become %d, none %d"),
							Scaffolded.UpgradePriceLess, Scaffolded.UpgradeBuildDaysMultiplied, Alone, WithTheRest,
							NoNode, Days, NoNodeDays));

	TestEqual(TEXT("its row: 50% less"), Scaffolded.UpgradePriceLess, 0.5f, 1.0e-4f);
	TestEqual(TEXT("its row: the days times 2"), Scaffolded.UpgradeBuildDaysMultiplied, 2.0f, 1.0e-4f);
	TestEqual(TEXT("**alone it halves the price**"), Alone, 0.5f, 1.0e-4f);
	TestEqual(TEXT("**after Bonds and Public Works, 0.65 taken off, it halves what is left: 0.175**"), WithTheRest,
			  0.175f, 1.0e-4f);
	TestEqual(TEXT("CONTROL: no node, exactly 1"), NoNode, 1.0f);
	TestEqual(TEXT("**6 days become 12**"), Days, 12);
	TestEqual(TEXT("CONTROL: no node, 6"), NoNodeDays, 6);
	return true;
}

/**
 * The order ruled: Scaffolding's doubling first, then Rapid Renovation's days and a rush's 2 taken off, then the floor
 * of a day. Rapid Renovation at 1, 2 and 3 points takes 1, 2 and 3 days off 6; with Scaffolding, 3 points leave 9, not
 * the 6 that taking off first would give; 3 points and a rush leave 1; a 2 day build with everything stops at 1. A
 * rush with Rapid Funding not held takes nothing off. An upgrade finished at once stays at 0. Prints "BUILD DAYS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeSpeedOrderTest,
	"Cataclysm.CityUpgradeSpeed.TheDaysAreDoubledThenShortenedThenHeldAtOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradeSpeedOrderTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradeSpeedTest;

	auto Points = [](int32 Renovation, bool bScaffolding, bool bFunding)
	{
		FCataclysmPassiveAllocation Tree;
		if (Renovation > 0)
		{
			Tree.Add(RapidRenovation, Renovation);
		}
		if (bScaffolding)
		{
			Tree.Add(Scaffolding, 1);
		}
		if (bFunding)
		{
			Tree.Add(RapidFunding, 1);
		}
		return BonusesOf(Tree);
	};

	const int32 One = UCataclysmEmpireTree::UpgradeBuildDaysFor(Points(1, false, false), 6, false);
	const int32 Two = UCataclysmEmpireTree::UpgradeBuildDaysFor(Points(2, false, false), 6, false);
	const int32 Three = UCataclysmEmpireTree::UpgradeBuildDaysFor(Points(3, false, false), 6, false);
	const int32 DoubledFirst = UCataclysmEmpireTree::UpgradeBuildDaysFor(Points(3, true, false), 6, false);
	const int32 Rushed = UCataclysmEmpireTree::UpgradeBuildDaysFor(Points(3, false, true), 6, true);
	const int32 RushedAlone = UCataclysmEmpireTree::UpgradeBuildDaysFor(Points(0, false, true), 6, true);
	const int32 Floor = UCataclysmEmpireTree::UpgradeBuildDaysFor(Points(3, false, true), 2, true);
	const int32 RushNotHeld = UCataclysmEmpireTree::UpgradeBuildDaysFor(Points(0, false, false), 6, true);
	const int32 AtOnce = UCataclysmEmpireTree::UpgradeBuildDaysFor(Points(3, true, true), 0, true);
	AddInfo(FString::Printf(TEXT("BUILD DAYS: Rapid Renovation 1/2/3 points %d/%d/%d; with Scaffolding %d; 3 and a rush %d; "
								 "a rush alone %d; 2 days with everything %d; a rush not held %d; at once %d"),
							One, Two, Three, DoubledFirst, Rushed, RushedAlone, Floor, RushNotHeld, AtOnce));

	TestEqual(TEXT("Rapid Renovation, 1 point: 5"), One, 5);
	TestEqual(TEXT("2 points: 4"), Two, 4);
	TestEqual(TEXT("3 points: 3"), Three, 3);
	TestEqual(TEXT("**with Scaffolding, doubled first: 6 x 2 - 3 = 9, not (6 - 3) x 2 = 6**"), DoubledFirst, 9);
	TestEqual(TEXT("3 points and a rush: 6 - 3 - 2 = 1"), Rushed, 1);
	TestEqual(TEXT("a rush alone: 4"), RushedAlone, 4);
	TestEqual(TEXT("**a 2 day build with 5 days off stops at 1**"), Floor, 1);
	TestEqual(TEXT("CONTROL: a rush with Rapid Funding not held takes nothing off"), RushNotHeld, 6);
	TestEqual(TEXT("an upgrade finished at once stays at 0"), AtOnce, 0);
	return true;
}

// ---------------------------------------------------------------------------
// The Rush
// ---------------------------------------------------------------------------

/**
 * With Rapid Funding and Municipal Bonds' 10 points held: the offer's price is 7,000 and its Rush 10,500 for 4 days;
 * buying the Rush takes 10,500 and puts the upgrade on the city's build list with 4 days left, ready 4 days from now.
 * Prints "RUSH".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeSpeedRushTest,
	"Cataclysm.CityUpgradeSpeed.ARushCostsHalfAsMuchAgainForTwoDaysFewer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradeSpeedRushTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradeSpeedTest;

	FSpeedScene Scene = MakeScene(25000);
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable() && Scene.Account() != nullptr))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	Scene.Account()->EmpireTree.Add(RapidFunding, 1);
	Scene.Account()->EmpireTree.Add(MunicipalBonds, 10);

	const TArray<FCataclysmCityUpgradeOffer> OfferList =
		UCataclysmCityScreenLayout::OffersFor(Scene.World, Scene.Run, 0, Held(Scene.World));
	const FCataclysmCityUpgradeOffer* Offer = OfferFor(OfferList);
	if (!TestNotNull(TEXT("set-up: the offer"), Offer))
	{
		return false;
	}
	const int64 Before = Held(Scene.World);
	const int32 Today = Scene.Run->Clock ? Scene.Run->Clock->Day : -1;
	const ECataclysmCityUpgradeResult Result =
		UCataclysmCityUpgradePurchase::Buy(Scene.World, Scene.Run, 0, FName(Row), /*bRush=*/true);
	const int64 After = Held(Scene.World);
	const FCataclysmCity* City = Scene.Run->Map->Find(0);
	const int32 DaysLeft = City && City->Building.Num() == 1 ? City->Building[0].BuildDaysLeft : -1;
	AddInfo(FString::Printf(TEXT("RUSH: offered %d, price %lld, rush %lld for %d days (can %d); %s, %lld to %lld; %d days "
								 "left, ready on day %d, today %d"),
							Offer->bRushOffered, Offer->Price, Offer->RushPrice, Offer->RushDays, Offer->bCanRush,
							*UCataclysmCityUpgradeRules::ResultText(Result), Before, After, DaysLeft,
							Scene.Run->BuildReadyDay(0), Today));

	TestTrue(TEXT("the offer has a Rush"), Offer->bRushOffered);
	TestEqual(TEXT("set-up: its price after Bonds' 30% is 7,000"), Offer->Price, static_cast<int64>(7000));
	TestEqual(TEXT("**its Rush is 150% of that: 10,500**"), Offer->RushPrice, static_cast<int64>(10500));
	TestEqual(TEXT("**for 4 days, 2 fewer than 6**"), Offer->RushDays, 4);
	TestTrue(TEXT("and can be bought with 25,000 held"), Offer->bCanRush);
	TestEqual(TEXT("UCataclysmCityUpgradePurchase::RushPriceIn answers the same"),
			  UCataclysmCityUpgradePurchase::RushPriceIn(Scene.World, Scene.Run, FName(Row), 1), Offer->RushPrice);
	TestTrue(TEXT("the rush is bought"), Result == ECataclysmCityUpgradeResult::Bought);
	TestEqual(TEXT("**the balance pays 10,500**"), Before - After, static_cast<int64>(10500));
	TestEqual(TEXT("**it is built in 4 days**"), DaysLeft, 4);
	TestEqual(TEXT("ready 4 days from now"), Scene.Run->BuildReadyDay(0), Today + 4);
	return true;
}

/**
 * Without Rapid Funding: no offer has a Rush, and a rush asked of `Buy` is refused with `RushNotHeld`, the gold left
 * as it was and nothing put on the city's build list. CONTROL: the same purchase not rushed goes through, for 10,000
 * and 6 days.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeSpeedRushRefusedTest,
	"Cataclysm.CityUpgradeSpeed.ARushWithoutRapidFundingIsRefusedAndTakesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradeSpeedRushRefusedTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradeSpeedTest;

	FSpeedScene Scene = MakeScene(25000);
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const TArray<FCataclysmCityUpgradeOffer> OfferList =
		UCataclysmCityScreenLayout::OffersFor(Scene.World, Scene.Run, 0, Held(Scene.World));
	const FCataclysmCityUpgradeOffer* Offer = OfferFor(OfferList);
	if (!TestNotNull(TEXT("set-up: the offer"), Offer))
	{
		return false;
	}
	const int64 Before = Held(Scene.World);
	const ECataclysmCityUpgradeResult Rushed =
		UCataclysmCityUpgradePurchase::Buy(Scene.World, Scene.Run, 0, FName(Row), /*bRush=*/true);
	const int64 AfterTheRush = Held(Scene.World);
	const FCataclysmCity* City = Scene.Run->Map->Find(0);
	const int32 BuildingAfterTheRush = City ? City->Building.Num() : -1;

	const ECataclysmCityUpgradeResult Plain =
		UCataclysmCityUpgradePurchase::Buy(Scene.World, Scene.Run, 0, FName(Row));
	const int64 AfterThePlain = Held(Scene.World);

	TestFalse(TEXT("no offer has a Rush"), Offer->bRushOffered);
	TestEqual(TEXT("**a rush is refused: Rapid Funding is not held**"), static_cast<int32>(Rushed),
			  static_cast<int32>(ECataclysmCityUpgradeResult::RushNotHeld));
	TestEqual(TEXT("**nothing is taken**"), AfterTheRush, Before);
	TestEqual(TEXT("**nothing is being built**"), BuildingAfterTheRush, 0);
	TestFalse(TEXT("and the refusal says why"),
			  UCataclysmCityUpgradePurchase::RefusalText(Rushed, 0, Before).IsEmpty());
	TestTrue(TEXT("CONTROL: bought without a rush"), Plain == ECataclysmCityUpgradeResult::Bought);
	TestEqual(TEXT("CONTROL: for 10,000"), Before - AfterThePlain, static_cast<int64>(10000));
	TestEqual(TEXT("CONTROL: in 6 days"),
			  City && City->Building.Num() == 1 ? City->Building[0].BuildDaysLeft : -1, 6);
	return true;
}

/**
 * A rush on credit, since the empire tree's slice 6e merged first: Global Credit's and Sovereign Loans' share is of the
 * rush's own price, as `Buy` takes it, and the Rush button says what it leaves owed, as the offer's own button does.
 * With Rapid Funding and Sovereign Loans' 1 point (20%) and 13,000 held: the Rush is 15,000, 3,000 of it may be owed,
 * so it can be rushed, leaving 2,000 owed, "Rush  --  15,000 gold on credit, 2,000 owed, 4 days to build"; bought, the
 * balance is taken whole, 2,000 is owed and the build has 4 days left. With 11,000 offered, 14,000 is short of 15,000
 * and the button says 1,000 more is needed. CONTROL: the offer's own 10,000 is bought outright, nothing owed. Prints
 * "RUSH ON CREDIT".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeSpeedCreditTest,
	"Cataclysm.CityUpgradeSpeed.ARushMayBeBoughtOnCreditAndItsButtonSaysWhatIsOwed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradeSpeedCreditTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradeSpeedTest;

	FSpeedScene Scene = MakeScene(13000);
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable() && Scene.Account() != nullptr))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	Scene.Account()->EmpireTree.Add(RapidFunding, 1);
	Scene.Account()->EmpireTree.Add(SovereignLoans, 1);

	const TArray<FCataclysmCityUpgradeOffer> Offers =
		UCataclysmCityScreenLayout::OffersFor(Scene.World, Scene.Run, 0, Held(Scene.World));
	const TArray<FCataclysmCityUpgradeOffer> ShortOffers =
		UCataclysmCityScreenLayout::OffersFor(Scene.World, Scene.Run, 0, 11000);
	const FCataclysmCityUpgradeOffer* Offer = OfferFor(Offers);
	const FCataclysmCityUpgradeOffer* Short = OfferFor(ShortOffers);
	if (!TestNotNull(TEXT("set-up: the offer"), Offer) || !TestNotNull(TEXT("set-up: the offer with 11,000"), Short))
	{
		return false;
	}
	const FString Button = UCataclysmCityScreenLayout::RushButtonTextFor(*Offer);
	const FString ShortButton = UCataclysmCityScreenLayout::RushButtonTextFor(*Short);

	const ECataclysmCityUpgradeResult Result =
		UCataclysmCityUpgradePurchase::Buy(Scene.World, Scene.Run, 0, FName(Row), /*bRush=*/true);
	const int64 After = Held(Scene.World);
	const int64 OwedAfter = UCataclysmCityUpgradePurchase::GoldOwedIn(Scene.World);
	const FCataclysmCity* City = Scene.Run->Map->Find(0);
	const int32 DaysLeft = City && City->Building.Num() == 1 ? City->Building[0].BuildDaysLeft : -1;
	AddInfo(FString::Printf(TEXT("RUSH ON CREDIT: rush %lld (can %d, owed if rushed %lld), \"%s\"; with 11,000 \"%s\"; "
								 "price %lld owed if bought %lld; %s, %lld held and %lld owed after, %d days left"),
							Offer->RushPrice, Offer->bCanRush, Offer->RushOwedIfBought, *Button, *ShortButton,
							Offer->Price, Offer->OwedIfBought, *UCataclysmCityUpgradeRules::ResultText(Result), After,
							OwedAfter, DaysLeft));

	TestEqual(TEXT("set-up: the Rush is 15,000"), Offer->RushPrice, static_cast<int64>(15000));
	TestTrue(TEXT("**13,000 held and 3,000 of credit can rush it**"), Offer->bCanRush);
	TestEqual(TEXT("**leaving 2,000 owed**"), Offer->RushOwedIfBought, static_cast<int64>(2000));
	TestEqual(TEXT("**the Rush button says so**"), Button,
			  FString(TEXT("Rush  --  15,000 gold on credit, 2,000 owed, 4 days to build")));
	TestTrue(TEXT("with 11,000, 14,000 cannot rush it"), !Short->bCanRush && Short->bRushShortOfGold);
	TestEqual(TEXT("and its button says what more is needed beyond the credit"), ShortButton,
			  FString(TEXT("Rush  --  Costs 15,000 gold; 1,000 more needed.")));
	TestEqual(TEXT("CONTROL: the offer's own 10,000 is bought outright"), Offer->OwedIfBought, static_cast<int64>(0));
	TestTrue(TEXT("**the rush is bought on credit**"), Result == ECataclysmCityUpgradeResult::Bought);
	TestEqual(TEXT("**the whole balance is taken**"), After, static_cast<int64>(0));
	TestEqual(TEXT("**and 2,000 is owed**"), OwedAfter, static_cast<int64>(2000));
	TestEqual(TEXT("it is built in 4 days"), DaysLeft, 4);
	return true;
}

/**
 * The days are fixed at purchase: Rapid Renovation's 3 points taken while a build stands leave it at its 6 days, and
 * the next offer, on another city, is 3 days. Prints "FIXED AT PURCHASE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeSpeedFixedTest,
	"Cataclysm.CityUpgradeSpeed.ANodeTakenDuringABuildLeavesItsDays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradeSpeedFixedTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradeSpeedTest;

	FSpeedScene Scene = MakeScene(25000);
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable() && Scene.Account() != nullptr))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const ECataclysmCityUpgradeResult Result = UCataclysmCityUpgradePurchase::Buy(Scene.World, Scene.Run, 0, FName(Row));
	const int32 ReadyBefore = Scene.Run->BuildReadyDay(0);
	Scene.Account()->EmpireTree.Add(RapidRenovation, 3);
	const FCataclysmCity* City = Scene.Run->Map->Find(0);
	const int32 DaysLeft = City && City->Building.Num() == 1 ? City->Building[0].BuildDaysLeft : -1;
	const int32 OtherCity = Scene.Run->Map->Cities[1].CityId;
	const int32 NextDays = UCataclysmCityUpgradePurchase::BuildDaysIn(Scene.World, Scene.Run, FName(Row), OtherCity);
	AddInfo(FString::Printf(TEXT("FIXED AT PURCHASE: %s; %d days left after Rapid Renovation's 3 points, ready on day %d "
								 "then %d; the next offer %d days"),
							*UCataclysmCityUpgradeRules::ResultText(Result), DaysLeft, ReadyBefore,
							Scene.Run->BuildReadyDay(0), NextDays));

	TestTrue(TEXT("set-up: bought"), Result == ECataclysmCityUpgradeResult::Bought);
	TestEqual(TEXT("**the build under way keeps its 6 days**"), DaysLeft, 6);
	TestEqual(TEXT("and its ready day"), Scene.Run->BuildReadyDay(0), ReadyBefore);
	TestEqual(TEXT("**the next purchase takes 3**"), NextDays, 3);
	return true;
}

/**
 * The Rush button's words: "Rush  --  10,500 gold, 4 days to build"; and, with too little gold for the Rush and enough
 * for the plain purchase, greyed, saying how much more: "Rush  --  Costs 15,000 gold; 3,000 more needed."
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeSpeedButtonTest,
	"Cataclysm.CityUpgradeSpeed.TheRushButtonSaysItsPriceAndDaysOrWhatIsMissing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradeSpeedButtonTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradeSpeedTest;

	FCataclysmCityUpgradeOffer Offer;
	Offer.RowName = FName(Row);
	Offer.bRushOffered = true;
	Offer.bCanRush = true;
	Offer.RushPrice = 10500;
	Offer.RushDays = 4;
	TestEqual(TEXT("**a Rush that can be bought**"), UCataclysmCityScreenLayout::RushButtonTextFor(Offer),
			  FString(TEXT("Rush  --  10,500 gold, 4 days to build")));

	FSpeedScene Scene = MakeScene(12000);
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable() && Scene.Account() != nullptr))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	Scene.Account()->EmpireTree.Add(RapidFunding, 1);
	const TArray<FCataclysmCityUpgradeOffer> OfferList =
		UCataclysmCityScreenLayout::OffersFor(Scene.World, Scene.Run, 0, Held(Scene.World));
	const FCataclysmCityUpgradeOffer* Short = OfferFor(OfferList);
	if (!TestNotNull(TEXT("set-up: the offer"), Short))
	{
		return false;
	}
	const FString Text = UCataclysmCityScreenLayout::RushButtonTextFor(*Short);
	AddInfo(FString::Printf(TEXT("RUSH BUTTON: '%s'; can buy %d, can rush %d, short %d"), *Text, Short->bCanBuy,
							Short->bCanRush, Short->bRushShortOfGold));

	TestTrue(TEXT("set-up: 12,000 buys it plainly"), Short->bCanBuy);
	TestFalse(TEXT("but not the 15,000 Rush"), Short->bCanRush);
	TestTrue(TEXT("and gold is all that stops it"), Short->bRushShortOfGold);
	TestEqual(TEXT("**greyed, saying how much more**"), Text,
			  FString(TEXT("Rush  --  Costs 15,000 gold; 3,000 more needed.")));
	return true;
}

/**
 * The play log says a rushed purchase was rushed, before its ready day; a purchase not rushed says nothing of it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradeSpeedLogTest,
	"Cataclysm.CityUpgradeSpeed.ThePlayLogSaysARushWasRushed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradeSpeedLogTest::RunTest(const FString&)
{
	const FString Rushed = FCataclysmPlayLoopLog::UpgradeLine(TEXT("Increase max defense by 20%"), nullptr, 0, 15000,
															  10000, 57, /*bRushed=*/true);
	const FString Plain = FCataclysmPlayLoopLog::UpgradeLine(TEXT("Increase max defense by 20%"), nullptr, 0, 10000,
															 15000, 59);
	AddInfo(FString::Printf(TEXT("RUSH LOG: '%s' / '%s'"), *Rushed, *Plain));

	TestTrue(TEXT("**a rushed purchase says so, before its ready day**"),
			 Rushed.EndsWith(TEXT("; rushed; being built, ready on day 57")));
	TestFalse(TEXT("CONTROL: a plain one does not"), Plain.Contains(TEXT("rushed")));
	TestTrue(TEXT("CONTROL: and still gives its ready day"), Plain.EndsWith(TEXT("; being built, ready on day 59")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
