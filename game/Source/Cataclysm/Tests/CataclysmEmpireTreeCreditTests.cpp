// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmCityUpgradeMapping.h"
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
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSavePartition.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Save/CataclysmSaveWriter.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The empire upgrade tree's slice 6e: Global Credit and Sovereign Loans, the gold debt. Issue #2741, the owner's ruling
 * of 2026-11-20 relayed by the coordinating session, "no gold debt unless there's an option for it in the empire tree",
 * and its readings D1 to D7 of that day:
 *
 *   D1  the debt is two numbers beside the balance, saved; the balance never goes below nought
 *   D2  a price the balance cannot cover may be short by 5% a point of it (Global Credit) and 20% a point (Sovereign
 *       Loans), rounded down; the whole balance is taken and the rest owed; only while nothing is owed
 *   D3  gold picked up from the floor pays the debt down first; the day's income and a clear's payout do not
 *   D4  with Sovereign Loans, a tenth of the debt, rounded up, is taken from the balance each day, partly if short
 *   D6  the balance shows the debt, an offer on credit says so, and a refusal names a debt that stops the credit
 *
 * `Cataclysm.EmpireTreeCredit.` IS A NEW GROUP and selects these and nothing else.
 *
 * THE ACCOUNT SLOT IS THE TEST'S OWN, as in `Cataclysm.CityUpgradePrice.`: while an automation test runs,
 * `UCataclysmGameInstance::AccountSlotFor` ends it in `_AutomationTest`. Each scene deletes it first, and nothing here
 * begins the save writer, so nothing is written.
 *
 * WRITTEN WITHOUT A BUILD. Phase 1 of the slice: every figure below is reasoned from the code, and the run that proves
 * them is registered in `docs/DECISIONS.md` before it happens.
 */
namespace CataclysmEmpireTreeCreditTest
{
	const FName GlobalCredit(TEXT("basic_0000003w"));	// 5% a point of the price may be owed, 5 points
	const FName SovereignLoans(TEXT("basic_tres_t4f"));	// 20% a point, repaid by a tenth a day, 5 points

	/** The upgrade every purchase here buys: built, and the sheet's first row, 10,000 gold at tier 1. */
	const TCHAR* const Row = TEXT("Architect_Increase_max_defense_by_20");

	/** A second built upgrade at the same price, for a second offer on the same city. */
	const TCHAR* const SecondRow = TEXT("Architect_Increase_max_population_by_20");

	/** A world with this project's game instance, a possessed player of one partition, and a run on the instance. */
	struct FCreditScene
	{
		UWorld* World = nullptr;
		UCataclysmGameInstance* Instance = nullptr;
		ACataclysmPlayerState* State = nullptr;
		UCataclysmEmpireRun* Run = nullptr;
		FCataclysmCharacterPartition Partition;

		bool IsUsable() const { return World && Instance && State && Run && Run->Map; }

		/** The mode's account record the scene's character banks into; null for a Solo Self-Found scene. */
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

	/** The scene: a Standard partition, offline, Solo Self-Found or not, its mode's account slot emptied first, and a
	 *  run begun at difficulty tier 1 set on the game instance. */
	FCreditScene MakeScene(bool bSoloSelfFound = false)
	{
		FCreditScene Out;
		Out.Partition.Lethality = ECataclysmLethality::Standard;
		Out.Partition.Population = ECataclysmPopulation::Offline;
		Out.Partition.bSoloSelfFound = bSoloSelfFound;

		FCataclysmCharacterPartition Mode = Out.Partition;
		Mode.bSoloSelfFound = false;
		DeleteSlot(UCataclysmGameInstance::AccountSlotFor(Mode));

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
			Out.State->SetIdentity(TEXT("Borrower"), Out.Partition);
		}

		Out.Run = NewObject<UCataclysmEmpireRun>(Out.Instance);
		// THE PRICE TESTS' SEED, whose city 0 they buy on: no Siege dungeon stands on it to refuse the offer.
		Out.Run->Begin(/* InSeed */ 2742, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
		Out.Instance->EmpireRun = Out.Run;
		return Out;
	}

	/** The balance, read through the one function every screen reads, or -1 for none. */
	int64 Held(const UObject* WorldContext)
	{
		int64 Gold = 0;
		return UCataclysmGameInstance::GoldHeldBy(WorldContext, Gold) ? Gold : -1;
	}

	/** The debt, or -1 for no balance. */
	int64 Owed(const UObject* WorldContext, int64* OutEachDay = nullptr)
	{
		int64 Debt = 0;
		int64 EachDay = 0;
		const bool bRead = UCataclysmGameInstance::GoldOwedBy(WorldContext, Debt, EachDay);
		if (OutEachDay != nullptr)
		{
			*OutEachDay = EachDay;
		}
		return bRead ? Debt : -1;
	}

	/** The run's answer as a number a TestEqual prints. */
	int32 Answer(ECataclysmCityUpgradeResult Result) { return static_cast<int32>(Result); }
}

/**
 * The two add up through the generated tables at full points, Global Credit 0.25 and Sovereign Loans 1.00, and the
 * credit on a price is their shares of it (D2): Global Credit's 0.05 of 10,000 is 500 and of 9,999 is 499, rounded
 * down; Sovereign Loans' 0.20 of 10,000 is 2,000; both at full points, 1.25, give 10,000 and never more than the price;
 * no node gives none. Only Sovereign Loans repays by day. Prints "CREDIT TABLE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeCreditTableTest,
	"Cataclysm.EmpireTreeCredit.TheTwoNodesAddUpAndTheCreditIsTheirSharesOfThePrice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeCreditTableTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeCreditTest;

	const UDataTable* Nodes = UCataclysmEmpireTree::LoadNodeTable();
	const UDataTable* Effects = UCataclysmEmpireTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the node table"), Nodes) || !TestNotNull(TEXT("set-up: the effect table"), Effects))
	{
		return false;
	}
	FCataclysmPassiveAllocation Tree;
	Tree.Add(GlobalCredit, 5);
	Tree.Add(SovereignLoans, 5);
	const FCataclysmEmpireTreePlayerBonuses Full = UCataclysmEmpireTree::PlayerBonusesFor(Nodes, Effects, Tree);

	FCataclysmEmpireTreePlayerBonuses Credit;
	Credit.UpgradeShortfallAllowed = 0.05f;
	FCataclysmEmpireTreePlayerBonuses Loans;
	Loans.UpgradeShortfallAllowedRepaidDaily = 0.20f;
	const FCataclysmEmpireTreePlayerBonuses None;
	const int64 CreditOn10000 = UCataclysmEmpireTree::UpgradeCreditFor(Credit, 10000);
	const int64 CreditOn9999 = UCataclysmEmpireTree::UpgradeCreditFor(Credit, 9999);
	const int64 LoansOn10000 = UCataclysmEmpireTree::UpgradeCreditFor(Loans, 10000);
	const int64 FullOn10000 = UCataclysmEmpireTree::UpgradeCreditFor(Full, 10000);
	const int64 NoneOn10000 = UCataclysmEmpireTree::UpgradeCreditFor(None, 10000);
	AddInfo(FString::Printf(TEXT("CREDIT TABLE: credit %.2f, loans %.2f; on 10,000 %lld, on 9,999 %lld; loans %lld; both "
								 "%lld; none %lld"),
							Full.UpgradeShortfallAllowed, Full.UpgradeShortfallAllowedRepaidDaily, CreditOn10000,
							CreditOn9999, LoansOn10000, FullOn10000, NoneOn10000));

	TestEqual(TEXT("Global Credit at 5 points: 0.25"), Full.UpgradeShortfallAllowed, 0.25f, 1.0e-5f);
	TestEqual(TEXT("Sovereign Loans at 5 points: 1.00"), Full.UpgradeShortfallAllowedRepaidDaily, 1.0f, 1.0e-5f);
	TestEqual(TEXT("0.05 of 10,000: 500"), CreditOn10000, static_cast<int64>(500));
	TestEqual(TEXT("0.05 of 9,999, rounded down: 499"), CreditOn9999, static_cast<int64>(499));
	TestEqual(TEXT("0.20 of 10,000: 2,000"), LoansOn10000, static_cast<int64>(2000));
	TestEqual(TEXT("both at full points, never more than the price: 10,000"), FullOn10000, static_cast<int64>(10000));
	TestEqual(TEXT("no node: none"), NoneOn10000, static_cast<int64>(0));
	TestTrue(TEXT("only Sovereign Loans repays by day"),
			 UCataclysmEmpireTree::RepaidDailyFor(Loans) && !UCataclysmEmpireTree::RepaidDailyFor(Credit));
	TestTrue(TEXT("either node gives credit, and no node none"),
			 UCataclysmEmpireTree::AllowsUpgradeCredit(Credit) && UCataclysmEmpireTree::AllowsUpgradeCredit(Loans)
				 && !UCataclysmEmpireTree::AllowsUpgradeCredit(None));
	return true;
}

/**
 * A purchase on credit takes the whole balance and owes the rest (D2): Global Credit at 2 points in the mode's account
 * gives 1,000 of credit on a 10,000 price, so with 9,000 held the upgrade is bought, the balance is 0, 1,000 is owed and
 * nothing is taken by day; the balance reads "0 gold, 1,000 owed" (D6). CONTROL, NO CREDIT NODE: 9,999 against 10,000
 * is refused as before, "Costs 10,000 gold; 1 more needed.", with nothing taken or owed; and short by more than the
 * credit -- 8,999 with Global Credit's 1,000 -- it is refused too, saying the 1 more that is needed beyond the credit.
 * Prints "CREDIT BUY".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeCreditBuyTest,
	"Cataclysm.EmpireTreeCredit.ACreditPurchaseTakesTheWholeBalanceAndOwesTheRest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeCreditBuyTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeCreditTest;

	FCreditScene Scene = MakeScene();
	ON_SCOPE_EXIT
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	UCataclysmAccountSave* Account = Scene.IsUsable() ? Scene.Account() : nullptr;
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable())
		|| !TestNotNull(TEXT("set-up: the mode's account"), Account))
	{
		return false;
	}
	const FName RowName(Row);

	// CONTROL: no credit node, 9,999 against 10,000.
	Scene.Instance->BankGold(Scene.Partition, 9999);
	const ECataclysmCityUpgradeResult Bare = UCataclysmCityUpgradePurchase::Buy(Scene.World, Scene.Run, 0, RowName);
	const FString BareWords =
		UCataclysmCityUpgradePurchase::RefusalTextIn(Scene.World, Bare, 10000, Held(Scene.World));
	const int64 BareHeld = Held(Scene.World);
	const int64 BareOwed = Owed(Scene.World);

	// SHORT BY MORE THAN THE CREDIT: 8,999 with Global Credit's 1,000.
	Account->Gold = 8999;
	Account->EmpireTree.Add(GlobalCredit, 2);
	const ECataclysmCityUpgradeResult TooShort = UCataclysmCityUpgradePurchase::Buy(Scene.World, Scene.Run, 0, RowName);
	const FString TooShortWords =
		UCataclysmCityUpgradePurchase::RefusalTextIn(Scene.World, TooShort, 10000, Held(Scene.World));
	const int64 TooShortHeld = Held(Scene.World);

	// ON CREDIT: 9,000 with the same 1,000.
	Account->Gold = 9000;
	const ECataclysmCityUpgradeResult OnCredit = UCataclysmCityUpgradePurchase::Buy(Scene.World, Scene.Run, 0, RowName);
	int64 EachDay = -1;
	const int64 After = Held(Scene.World);
	const int64 Debt = Owed(Scene.World, &EachDay);
	const FString Balance = UCataclysmGameInstance::GoldBalanceText(Scene.World);
	// BEING BUILT, since issue #2825: a purchase takes days, and the city holds it on its build list until then.
	const FCataclysmCity* City = Scene.Run->Map->Find(0);
	const bool bHeld = City != nullptr && City->IsBuilding(RowName);
	AddInfo(FString::Printf(TEXT("CREDIT BUY: bare %s \"%s\" %lld held %lld owed; too short %s \"%s\" %lld held; on credit "
								 "%s, %lld held, %lld owed, %lld a day, \"%s\", being built by the city %s"),
							*UCataclysmCityUpgradeRules::ResultText(Bare), *BareWords, BareHeld, BareOwed,
							*UCataclysmCityUpgradeRules::ResultText(TooShort), *TooShortWords, TooShortHeld,
							*UCataclysmCityUpgradeRules::ResultText(OnCredit), After, Debt, EachDay, *Balance,
							bHeld ? TEXT("yes") : TEXT("no")));

	TestEqual(TEXT("CONTROL: no credit node, 9,999 is refused for gold"), Answer(Bare),
			  Answer(ECataclysmCityUpgradeResult::NotEnoughGold));
	TestEqual(TEXT("saying what it said before"), BareWords, FString(TEXT("Costs 10,000 gold; 1 more needed.")));
	TestTrue(TEXT("with nothing taken or owed"), BareHeld == 9999 && BareOwed == 0);
	TestEqual(TEXT("short by more than the credit is refused"), Answer(TooShort),
			  Answer(ECataclysmCityUpgradeResult::NotEnoughGold));
	TestEqual(TEXT("saying the 1 more needed beyond the credit"), TooShortWords,
			  FString(TEXT("Costs 10,000 gold; 1 more needed.")));
	TestEqual(TEXT("with nothing taken"), TooShortHeld, static_cast<int64>(8999));
	TestEqual(TEXT("9,000 with 1,000 of credit buys it"), Answer(OnCredit), Answer(ECataclysmCityUpgradeResult::Bought));
	TestTrue(TEXT("and the city is building it"), bHeld);
	TestEqual(TEXT("the whole balance is taken"), After, static_cast<int64>(0));
	TestEqual(TEXT("and the rest owed: 1,000"), Debt, static_cast<int64>(1000));
	TestEqual(TEXT("nothing by day without Sovereign Loans"), EachDay, static_cast<int64>(0));
	TestEqual(TEXT("the balance says so"), Balance, FString(TEXT("0 gold, 1,000 owed")));
	return true;
}

/**
 * Nothing more on credit until the debt is paid, but a price the balance covers is bought as ever. With Global Credit
 * at 2 points and 500 owed, 9,500 held against 10,000 is refused, the credit withheld: "Costs 10,000 gold; 500 more
 * needed, and no more credit until the 500 owed is paid." (D6). With 20,000 held it is bought for 10,000 and the 500
 * stays owed. Prints "CREDIT OPEN".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeCreditOpenTest,
	"Cataclysm.EmpireTreeCredit.AnOpenDebtStopsMoreCreditButNotAFullPrice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeCreditOpenTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeCreditTest;

	FCreditScene Scene = MakeScene();
	ON_SCOPE_EXIT
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	UCataclysmAccountSave* Account = Scene.IsUsable() ? Scene.Account() : nullptr;
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable())
		|| !TestNotNull(TEXT("set-up: the mode's account"), Account))
	{
		return false;
	}
	const FName RowName(Row);
	Account->EmpireTree.Add(GlobalCredit, 2);
	Account->GoldOwed = 500;
	Account->Gold = 9500;

	const int64 Credit = UCataclysmCityUpgradePurchase::CreditIn(Scene.World, 10000);
	const ECataclysmCityUpgradeResult Withheld = UCataclysmCityUpgradePurchase::Buy(Scene.World, Scene.Run, 0, RowName);
	const FString Words = UCataclysmCityUpgradePurchase::RefusalTextIn(Scene.World, Withheld, 10000, Held(Scene.World));

	Account->Gold = 20000;
	const ECataclysmCityUpgradeResult Covered = UCataclysmCityUpgradePurchase::Buy(Scene.World, Scene.Run, 0, RowName);
	const int64 After = Held(Scene.World);
	const int64 StillOwed = Owed(Scene.World);
	AddInfo(FString::Printf(TEXT("CREDIT OPEN: credit while owing %lld; %s \"%s\"; with 20,000 %s, %lld after, %lld owed"),
							Credit, *UCataclysmCityUpgradeRules::ResultText(Withheld), *Words,
							*UCataclysmCityUpgradeRules::ResultText(Covered), After, StillOwed));

	TestEqual(TEXT("no credit while 500 is owed"), Credit, static_cast<int64>(0));
	TestEqual(TEXT("so 9,500 is refused for gold"), Answer(Withheld), Answer(ECataclysmCityUpgradeResult::NotEnoughGold));
	TestEqual(TEXT("naming the debt that stops the credit"), Words,
			  FString(TEXT("Costs 10,000 gold; 500 more needed, and no more credit until the 500 owed is paid.")));
	TestEqual(TEXT("a price the balance covers is bought"), Answer(Covered), Answer(ECataclysmCityUpgradeResult::Bought));
	TestEqual(TEXT("for its price"), After, static_cast<int64>(10000));
	TestEqual(TEXT("and the debt stays"), StillOwed, static_cast<int64>(500));
	return true;
}

/**
 * Gold picked up pays the debt down first, and the rest goes into the balance (D3): 1,000 owed at 100 a day, 600
 * picked up pays 600 and leaves 400 owed, the part a day kept; 1,000 more pays the 400, clears the part a day and puts
 * 600 into the balance. CONTROL: the day's income is not a pickup -- `AddGoldFor` puts 500 into the balance with 300
 * owed and leaves the 300. Prints "CREDIT PICKUP".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeCreditPickupTest,
	"Cataclysm.EmpireTreeCredit.GoldPickedUpPaysTheDebtFirstAndIncomeDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeCreditPickupTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeCreditTest;

	FCreditScene Scene = MakeScene();
	ON_SCOPE_EXIT
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	UCataclysmAccountSave* Account = Scene.IsUsable() ? Scene.Account() : nullptr;
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable())
		|| !TestNotNull(TEXT("set-up: the mode's account"), Account))
	{
		return false;
	}
	Account->Gold = 0;
	Account->GoldOwed = 1000;
	Account->GoldOwedEachDay = 100;

	bool bIntoTheCharacter = true;
	int64 FirstRepaid = -1;
	const bool bFirst = UCataclysmGameInstance::AddPickedUpGoldFor(Scene.World, 600, bIntoTheCharacter, FirstRepaid);
	const int64 OwedAfterFirst = Account->GoldOwed;
	const int64 PartAfterFirst = Account->GoldOwedEachDay;
	const int64 HeldAfterFirst = Account->Gold;
	int64 SecondRepaid = -1;
	UCataclysmGameInstance::AddPickedUpGoldFor(Scene.World, 1000, bIntoTheCharacter, SecondRepaid);
	const int64 OwedAfterSecond = Account->GoldOwed;
	const int64 PartAfterSecond = Account->GoldOwedEachDay;
	const int64 HeldAfterSecond = Account->Gold;

	Account->GoldOwed = 300;
	const int64 HeldBeforeIncome = Account->Gold;
	UCataclysmGameInstance::AddGoldFor(Scene.World, 500, bIntoTheCharacter);
	AddInfo(FString::Printf(TEXT("CREDIT PICKUP: 600 repaid %lld, %lld owed at %lld a day, %lld held; 1,000 repaid %lld, "
								 "%lld owed at %lld a day, %lld held; income 500 into %lld held, %lld owed"),
							FirstRepaid, OwedAfterFirst, PartAfterFirst, HeldAfterFirst, SecondRepaid, OwedAfterSecond,
							PartAfterSecond, HeldAfterSecond, Account->Gold - HeldBeforeIncome, Account->GoldOwed));

	TestTrue(TEXT("a pickup into the account is taken"), bFirst && !bIntoTheCharacter);
	TestEqual(TEXT("600 picked up pays 600"), FirstRepaid, static_cast<int64>(600));
	TestEqual(TEXT("leaving 400 owed"), OwedAfterFirst, static_cast<int64>(400));
	TestEqual(TEXT("the part a day kept"), PartAfterFirst, static_cast<int64>(100));
	TestEqual(TEXT("and nothing into the balance"), HeldAfterFirst, static_cast<int64>(0));
	TestEqual(TEXT("1,000 more pays the 400"), SecondRepaid, static_cast<int64>(400));
	TestEqual(TEXT("clearing the debt"), OwedAfterSecond, static_cast<int64>(0));
	TestEqual(TEXT("and its part a day"), PartAfterSecond, static_cast<int64>(0));
	TestEqual(TEXT("the rest into the balance: 600"), HeldAfterSecond, static_cast<int64>(600));
	TestEqual(TEXT("CONTROL: income goes into the balance whole"), Account->Gold - HeldBeforeIncome,
			  static_cast<int64>(500));
	TestEqual(TEXT("and pays no debt"), Account->GoldOwed, static_cast<int64>(300));
	return true;
}

/**
 * Sovereign Loans takes a tenth of the debt a day, rounded up (D4): at 1 point it gives 2,000 of credit on 10,000, so
 * 8,495 held leaves 1,505 owed at 151 a day. A day with nothing held pays nothing, with no penalty; with 10,000 held
 * nine days pay 1,359 and the tenth the 146 left, so the debt is cleared and 10,000 - 1,505 = 8,495 is held. A debt
 * made with Global Credit alone takes nothing a day. Prints "CREDIT DAYS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeCreditDaysTest,
	"Cataclysm.EmpireTreeCredit.SovereignLoansTakesATenthOfTheDebtEachDay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeCreditDaysTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeCreditTest;

	FCreditScene Scene = MakeScene();
	ON_SCOPE_EXIT
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	UCataclysmAccountSave* Account = Scene.IsUsable() ? Scene.Account() : nullptr;
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable())
		|| !TestNotNull(TEXT("set-up: the mode's account"), Account))
	{
		return false;
	}
	Account->EmpireTree.Add(SovereignLoans, 1);
	Account->Gold = 8495;
	const ECataclysmCityUpgradeResult Result =
		UCataclysmCityUpgradePurchase::Buy(Scene.World, Scene.Run, 0, FName(Row));
	int64 EachDay = -1;
	const int64 Debt = Owed(Scene.World, &EachDay);

	bool bFromTheCharacter = true;
	const int64 PaidWithNothing = UCataclysmGameInstance::RepayTheDaysPartFor(Scene.World, bFromTheCharacter);
	Account->Gold = 10000;
	int64 NineDays = 0;
	for (int32 Day = 0; Day < 9; ++Day)
	{
		NineDays += UCataclysmGameInstance::RepayTheDaysPartFor(Scene.World, bFromTheCharacter);
	}
	const int64 Tenth = UCataclysmGameInstance::RepayTheDaysPartFor(Scene.World, bFromTheCharacter);
	const int64 Eleventh = UCataclysmGameInstance::RepayTheDaysPartFor(Scene.World, bFromTheCharacter);

	// A DEBT MADE WITH GLOBAL CREDIT ALONE: nothing by day.
	Account->GoldOwed = 700;
	Account->GoldOwedEachDay = 0;
	const int64 CreditAlone = UCataclysmGameInstance::RepayTheDaysPartFor(Scene.World, bFromTheCharacter);
	AddInfo(FString::Printf(TEXT("CREDIT DAYS: %s, %lld owed at %lld a day; with nothing held %lld; nine days %lld, the "
								 "tenth %lld, the eleventh %lld; %lld held; Global Credit alone %lld"),
							*UCataclysmCityUpgradeRules::ResultText(Result), Debt, EachDay, PaidWithNothing, NineDays,
							Tenth, Eleventh, Account->Gold, CreditAlone));

	TestEqual(TEXT("8,495 with 2,000 of credit buys it"), Answer(Result), Answer(ECataclysmCityUpgradeResult::Bought));
	TestEqual(TEXT("owing 1,505"), Debt, static_cast<int64>(1505));
	TestEqual(TEXT("at a tenth a day, rounded up: 151"), EachDay, static_cast<int64>(151));
	TestEqual(TEXT("a day with nothing held pays nothing"), PaidWithNothing, static_cast<int64>(0));
	TestEqual(TEXT("nine days pay 1,359"), NineDays, static_cast<int64>(1359));
	TestEqual(TEXT("the tenth the 146 left"), Tenth, static_cast<int64>(146));
	TestEqual(TEXT("and then nothing"), Eleventh, static_cast<int64>(0));
	TestEqual(TEXT("leaving 8,495 held"), Account->Gold, static_cast<int64>(8495));
	TestEqual(TEXT("Global Credit alone takes nothing by day"), CreditAlone, static_cast<int64>(0));
	return true;
}

/**
 * A Solo Self-Found character borrows against its own gold, and its debt is its own (D1): Global Credit at 2 points in
 * its own tree, 9,000 of its own, a 10,000 upgrade bought; its own gold is 0 and its own debt 1,000, and the mode's
 * account is untouched, holding nothing and owing nothing. Prints "CREDIT OWN".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeCreditOwnTest,
	"Cataclysm.EmpireTreeCredit.ASoloSelfFoundCharacterOwesItsOwnGold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeCreditOwnTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeCreditTest;

	FCreditScene Scene = MakeScene(/*bSoloSelfFound=*/true);
	ON_SCOPE_EXIT
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		return false;
	}
	Scene.State->EditOwnEmpireTree().Add(GlobalCredit, 2);
	Scene.State->SetOwnGold(9000);
	const ECataclysmCityUpgradeResult Result =
		UCataclysmCityUpgradePurchase::Buy(Scene.World, Scene.Run, 0, FName(Row));

	FCataclysmCharacterPartition Mode = Scene.Partition;
	Mode.bSoloSelfFound = false;
	const UCataclysmAccountSave* ModeAccount = Scene.Instance->AccountFor(Mode);
	AddInfo(FString::Printf(TEXT("CREDIT OWN: %s; its own %lld held, %lld owed; the mode's account %lld held, %lld owed"),
							*UCataclysmCityUpgradeRules::ResultText(Result), Scene.State->GetOwnGold(),
							Scene.State->GetOwnGoldOwed(), ModeAccount ? ModeAccount->Gold : -1,
							ModeAccount ? ModeAccount->GoldOwed : -1));

	TestEqual(TEXT("bought on its own credit"), Answer(Result), Answer(ECataclysmCityUpgradeResult::Bought));
	TestEqual(TEXT("its own gold is taken"), Scene.State->GetOwnGold(), static_cast<int64>(0));
	TestEqual(TEXT("and its own debt is 1,000"), Scene.State->GetOwnGoldOwed(), static_cast<int64>(1000));
	TestTrue(TEXT("the mode's account is untouched"),
			 ModeAccount == nullptr || (ModeAccount->Gold == 0 && ModeAccount->GoldOwed == 0));
	return true;
}

/**
 * The city screen offers a short upgrade on credit and says what it leaves owed (D6): Global Credit at 2 points, 9,000
 * held, city 0's offer of the row is buyable, owing 1,000, its button "<sentence>  --  10,000 gold on credit, 1,000
 * owed, 6 days to build" (the days since issue #2825). Bought, the balance reads "0 gold, 1,000 owed"; with 9,000
 * banked again, another standing city's offer of a second upgrade at the same price is short of gold, its button
 * "<sentence>  --  Costs 10,000 gold; 1,000 more needed, and no more credit until the 1,000 owed is paid." ANOTHER
 * CITY since issue #2825: city 0 is building the first, and a city builds one upgrade at a time.
 * Prints "CREDIT SCREEN".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeCreditScreenTest,
	"Cataclysm.EmpireTreeCredit.TheCityScreenSaysWhatCreditLeavesOwedAndWhatADebtStops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeCreditScreenTest::RunTest(const FString&)
{
	using namespace CataclysmEmpireTreeCreditTest;

	FCreditScene Scene = MakeScene();
	ON_SCOPE_EXIT
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	UCataclysmAccountSave* Account = Scene.IsUsable() ? Scene.Account() : nullptr;
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable())
		|| !TestNotNull(TEXT("set-up: the mode's account"), Account))
	{
		return false;
	}
	const FName RowName(Row);
	Account->EmpireTree.Add(GlobalCredit, 2);
	Account->Gold = 9000;

	auto OfferOn = [&Scene](int32 CityId, FName Wanted) -> TOptional<FCataclysmCityUpgradeOffer>
	{
		const TArray<FCataclysmCityUpgradeOffer> Offers =
			UCataclysmCityScreenLayout::OffersFor(Scene.World, Scene.Run, CityId, Held(Scene.World));
		const FCataclysmCityUpgradeOffer* Found = Offers.FindByPredicate(
			[&Wanted](const FCataclysmCityUpgradeOffer& One) { return One.RowName == Wanted; });
		return Found != nullptr ? TOptional<FCataclysmCityUpgradeOffer>(*Found) : TOptional<FCataclysmCityUpgradeOffer>();
	};

	const TOptional<FCataclysmCityUpgradeOffer> OnCredit = OfferOn(0, RowName);
	if (!TestTrue(TEXT("set-up: city 0 offers the row"), OnCredit.IsSet()))
	{
		return false;
	}
	const FString CreditButton = UCataclysmCityScreenLayout::ButtonTextFor(OnCredit.GetValue());
	UCataclysmCityUpgradePurchase::Buy(Scene.World, Scene.Run, 0, RowName);
	const FString Balance = UCataclysmGameInstance::GoldBalanceText(Scene.World);
	Account->Gold = 9000;

	// ANOTHER STANDING CITY, since issue #2825: city 0 is building the first upgrade, one at a time.
	int32 OtherCity = INDEX_NONE;
	for (const FCataclysmCity& City : Scene.Run->Map->Cities)
	{
		if (City.CityId != 0 && !City.bFallen && !Scene.Run->IsBesieged(City.CityId))
		{
			OtherCity = City.CityId;
			break;
		}
	}
	if (!TestTrue(TEXT("set-up: another city stands unbesieged"), OtherCity != INDEX_NONE))
	{
		return false;
	}
	const TOptional<FCataclysmCityUpgradeOffer> Stopped = OfferOn(OtherCity, FName(SecondRow));
	if (!TestTrue(TEXT("set-up: the other city offers the second row"), Stopped.IsSet()))
	{
		return false;
	}
	const FString StoppedButton = UCataclysmCityScreenLayout::ButtonTextFor(Stopped.GetValue());
	AddInfo(FString::Printf(TEXT("CREDIT SCREEN: \"%s\" (can buy %s, owing %lld); \"%s\"; \"%s\" (short %s)"),
							*CreditButton, OnCredit->bCanBuy ? TEXT("yes") : TEXT("no"), OnCredit->OwedIfBought,
							*Balance, *StoppedButton, Stopped->bShortOfGold ? TEXT("yes") : TEXT("no")));

	const FString Sentence = OnCredit->Effect.IsEmpty() ? RowName.ToString() : OnCredit->Effect;
	const FString SecondSentence = Stopped->Effect.IsEmpty() ? FString(SecondRow) : Stopped->Effect;
	TestTrue(TEXT("the offer is buyable on credit"), OnCredit->bCanBuy && !OnCredit->bShortOfGold);
	TestEqual(TEXT("owing 1,000 if bought"), OnCredit->OwedIfBought, static_cast<int64>(1000));
	TestEqual(TEXT("its button says so"), CreditButton,
			  FString::Printf(TEXT("%s  --  10,000 gold on credit, 1,000 owed, 6 days to build"), *Sentence));
	TestEqual(TEXT("bought, the balance shows the debt"), Balance, FString(TEXT("0 gold, 1,000 owed")));
	TestTrue(TEXT("the next offer is short of gold"), Stopped->bShortOfGold && !Stopped->bCanBuy);
	TestEqual(TEXT("its button names the debt that stops the credit"), StoppedButton,
			  FString::Printf(TEXT("%s  --  Costs 10,000 gold; 1,000 more needed, and no more credit until the 1,000 "
								   "owed is paid."),
							  *SecondSentence));
	return true;
}

/**
 * The files from before the debt migrate owing nothing (D1), and the committed files read the debt they state:
 * `Account_v4.json` migrates to the current version (5 when this was written) with 0 owed and 0 a day, keeping its
 * 1234567 gold and 37.625 points; `Character_v10.json` to the current version (11 then) with 0 and 0, keeping its 4321
 * gold and two skills' experience. The committed files, `Account_v8.json` (`Account_v7.json` until #47 slice F6a)
 * and `Character_v17.json` since issue #837 slice 1
 * (`Character_v16.json` from issue #2849, `Character_v15.json` from issue #2848, `Character_v14.json` from issue #2741 slice 9, `Character_v13.json` from issue #46, `Character_v12.json` from issue #2741 slice 8), are read, not migrated, with 2500 owed at 250 a day and 1500 at 150. Prints "CREDIT FILES".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEmpireTreeCreditFilesTest,
	"Cataclysm.EmpireTreeCredit.ThePreviousFilesMigrateOwingNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEmpireTreeCreditFilesTest::RunTest(const FString&)
{
	FString OldAccountText;
	FString OldCharacterText;
	FString AccountText;
	FString CharacterText;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Account_v4.json"), OldAccountText, Reason)
		|| !CataclysmSaveFixtures::Read(TEXT("Character_v10.json"), OldCharacterText, Reason)
		|| !CataclysmSaveFixtures::Read(TEXT("Account_v8.json"), AccountText, Reason)
		|| !CataclysmSaveFixtures::Read(TEXT("Character_v17.json"), CharacterText, Reason))
	{
		AddError(Reason);
		return false;
	}

	ECataclysmSaveLoadResult OldAccountResult = ECataclysmSaveLoadResult::NotValidJson;
	ECataclysmSaveLoadResult OldCharacterResult = ECataclysmSaveLoadResult::NotValidJson;
	ECataclysmSaveLoadResult AccountResult = ECataclysmSaveLoadResult::NotValidJson;
	ECataclysmSaveLoadResult CharacterResult = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	const UCataclysmAccountSave* OldAccount = Cast<UCataclysmAccountSave>(FCataclysmSaveStorage::FromJson(
		OldAccountText, UCataclysmAccountSave::StaticClass(), GetTransientPackage(), OldAccountResult, Message));
	const UCataclysmCharacterSave* OldCharacter = Cast<UCataclysmCharacterSave>(FCataclysmSaveStorage::FromJson(
		OldCharacterText, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(), OldCharacterResult, Message));
	const UCataclysmAccountSave* Account = Cast<UCataclysmAccountSave>(FCataclysmSaveStorage::FromJson(
		AccountText, UCataclysmAccountSave::StaticClass(), GetTransientPackage(), AccountResult, Message));
	const UCataclysmCharacterSave* Character = Cast<UCataclysmCharacterSave>(FCataclysmSaveStorage::FromJson(
		CharacterText, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(), CharacterResult, Message));
	if (!TestNotNull(TEXT("Account_v4.json loads"), OldAccount)
		|| !TestNotNull(TEXT("Character_v10.json loads"), OldCharacter)
		|| !TestNotNull(TEXT("Account_v8.json loads"), Account)
		|| !TestNotNull(TEXT("Character_v17.json loads"), Character))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("CREDIT FILES: account v4 %s at %d, %lld owed at %lld, %lld gold, %.3f points; character "
								 "v10 %s at %d, %lld owed at %lld, %lld gold, %d skills; account v6 %s, %lld at %lld; "
								 "character v12 %s, %lld at %lld"),
							FCataclysmSaveStorage::Describe(OldAccountResult), OldAccount->SchemaVersion,
							OldAccount->GoldOwed, OldAccount->GoldOwedEachDay, OldAccount->Gold,
							OldAccount->EmpireUpgradePoints, FCataclysmSaveStorage::Describe(OldCharacterResult),
							OldCharacter->SchemaVersion, OldCharacter->GoldOwed, OldCharacter->GoldOwedEachDay,
							OldCharacter->Gold, OldCharacter->SkillExperience.Num(),
							FCataclysmSaveStorage::Describe(AccountResult), Account->GoldOwed, Account->GoldOwedEachDay,
							FCataclysmSaveStorage::Describe(CharacterResult), Character->GoldOwed,
							Character->GoldOwedEachDay));

	TestEqual(TEXT("Account_v4.json is migrated"), static_cast<int32>(OldAccountResult),
			  static_cast<int32>(ECataclysmSaveLoadResult::Migrated));
	TestEqual(TEXT("to the current version"), OldAccount->SchemaVersion, UCataclysmAccountSave::SchemaVersionNow);
	TestTrue(TEXT("owing nothing"), OldAccount->GoldOwed == 0 && OldAccount->GoldOwedEachDay == 0);
	TestTrue(TEXT("keeping its gold and points"),
			 OldAccount->Gold == 1234567 && FMath::IsNearlyEqual(OldAccount->EmpireUpgradePoints, 37.625));
	TestEqual(TEXT("Character_v10.json is migrated"), static_cast<int32>(OldCharacterResult),
			  static_cast<int32>(ECataclysmSaveLoadResult::Migrated));
	TestEqual(TEXT("to the current version"), OldCharacter->SchemaVersion, UCataclysmCharacterSave::SchemaVersionNow);
	TestTrue(TEXT("owing nothing"), OldCharacter->GoldOwed == 0 && OldCharacter->GoldOwedEachDay == 0);
	TestTrue(TEXT("keeping its gold and skills"), OldCharacter->Gold == 4321 && OldCharacter->SkillExperience.Num() == 2);
	TestEqual(TEXT("Account_v8.json is read rather than migrated"), static_cast<int32>(AccountResult),
			  static_cast<int32>(ECataclysmSaveLoadResult::Loaded));
	TestTrue(TEXT("owing 2500 at 250 a day"), Account->GoldOwed == 2500 && Account->GoldOwedEachDay == 250);
	TestEqual(TEXT("Character_v17.json is read rather than migrated"), static_cast<int32>(CharacterResult),
			  static_cast<int32>(ECataclysmSaveLoadResult::Loaded));
	TestTrue(TEXT("owing 1500 at 150 a day"), Character->GoldOwed == 1500 && Character->GoldOwedEachDay == 150);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
