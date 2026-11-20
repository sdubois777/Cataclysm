// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmCityUpgradeMapping.h"
#include "Data/CataclysmDataRows.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Interface/CataclysmCityScreenLayout.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmCityUpgradePurchase.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayLoopLog.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSavePartition.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Save/CataclysmSaveTriggers.h"
#include "Save/CataclysmSaveWriter.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * City upgrades cost gold. Issue #2742's second slice: the owner's ruling of 2026-11-18 that city upgrades are the
 * first thing gold buys, priced by the coordinating session's G4 (10,000 gold at upgrade tier 1 and difficulty tier 1,
 * scaled to the run's tier by the Gold sheet's pile column).
 *
 * WHAT IS CHECKED HERE: the price by tier and the table's three price columns, a purchase taking the price from the
 * lethality mode's account and saving it, a Solo Self-Found character paying from its own, a purchase refused for too
 * little gold taking nothing, and every word the city screen and the play loop log show about it. The empire layer's
 * own check of the price against the gold held is `Cataclysm.CityUpgrade.ThePriceIsCheckedLastAndAgainstTheGoldHeld`;
 * the generator's columns are `tools/tests/test_city_upgrades_cost_gold.py`'s.
 *
 * THE ACCOUNT SLOT IS THE TEST'S OWN, as in `Cataclysm.Gold.`: while an automation test runs,
 * `UCataclysmGameInstance::AccountSlotFor` ends it in `_AutomationTest`. Each scene deletes it first.
 */
namespace CataclysmCityUpgradePriceTest
{
	/** The upgrade every purchase here buys: built, and the sheet's first row. */
	const TCHAR* const Row = TEXT("Architect_Increase_max_defense_by_20");

	/** What a priced row costs at difficulty tiers 1 to 8: 10,000 times 1, 3, 6, 10, 14, 19, 24 and 30. */
	const int64 PriceByTier[8] = { 10000, 30000, 60000, 100000, 140000, 190000, 240000, 300000 };

	/** A world with this project's game instance, a possessed player of one partition, and a run on the instance. */
	struct FPriceScene
	{
		UWorld* World = nullptr;
		UCataclysmGameInstance* Instance = nullptr;
		ACataclysmPlayerState* State = nullptr;
		UCataclysmEmpireRun* Run = nullptr;
		FCataclysmCharacterPartition Partition;

		bool IsUsable() const { return World && Instance && State && Run && Run->Map; }
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

	/** Waits for any write to the slot to finish, giving up after two seconds. */
	void WaitForTheSlot(const FString& SlotName)
	{
		const double GiveUpAt = FPlatformTime::Seconds() + 2.0;
		while (FCataclysmSaveStorage::HasAWriteInFlight(SlotName) && FPlatformTime::Seconds() < GiveUpAt)
		{
			FPlatformProcess::Sleep(0.01f);
		}
	}

	/**
	 * The scene: a Standard partition, offline, Solo Self-Found or not, its lethality mode's account slot emptied first,
	 * and a run begun at difficulty tier 1 set on the game instance. `bWriting` begins the save writer, as play does.
	 */
	FPriceScene MakeScene(bool bSoloSelfFound = false, bool bWriting = false)
	{
		FPriceScene Out;
		Out.Partition.Lethality = ECataclysmLethality::Standard;
		Out.Partition.Population = ECataclysmPopulation::Offline;
		Out.Partition.bSoloSelfFound = bSoloSelfFound;

		// THE MODE'S ACCOUNT SLOT, emptied even for a Solo Self-Found scene, whose test reads it to show it untouched.
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
			Out.State->SetIdentity(TEXT("Buyer"), Out.Partition);
		}

		Out.Run = NewObject<UCataclysmEmpireRun>(Out.Instance);
		Out.Run->Begin(/* InSeed */ 2742, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
		Out.Instance->EmpireRun = Out.Run;

		if (bWriting)
		{
			if (UCataclysmSaveWriter* Writer = UCataclysmSaveWriter::In(Out.World))
			{
				Writer->BeginRun(FGuid::NewGuid(), FGuid::NewGuid(), FName(TEXT("Sandbox")), 1);
			}
		}
		return Out;
	}

	/** The gold the scene's character holds, read through the one function every screen reads, or -1 for none. */
	int64 Held(const UObject* WorldContext)
	{
		int64 Gold = 0;
		return UCataclysmGameInstance::GoldHeldBy(WorldContext, Gold) ? Gold : -1;
	}
}

// ---------------------------------------------------------------------------
// The price
// ---------------------------------------------------------------------------

/**
 * A priced row costs 10,000 gold at difficulty tier 1 and the Gold sheet's pile scale above it: 30,000 at tier 2 up to
 * 300,000 at tier 8, a tier outside 1 to 8 held inside them. All 23 rows with a tier ladder carry 10,000, 20,000 and
 * 40,000 in the table; the unbranched last resort and an unknown row cost nothing. The run's own tier picks the price.
 * Prints "PRICE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradePriceByTierTest,
	"Cataclysm.CityUpgradePrice.ThePriceIsTheRowsTierOneGoldScaledByThePileColumn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradePriceByTierTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradePriceTest;

	const UDataTable* Table = UCataclysmCityUpgradeMapping::LoadGeneratedTable();
	if (!TestNotNull(TEXT("set-up: DT_CityUpgrades loads"), Table))
	{
		return false;
	}

	int32 Priced = 0;
	int32 Unpriced = 0;
	int32 Wrong = 0;
	FString FirstWrong;
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FCataclysmCityUpgradeRow* Upgrade = reinterpret_cast<const FCataclysmCityUpgradeRow*>(Pair.Value);
		if (Upgrade->Tier1Gold == 10000 && Upgrade->Tier2Gold == 20000 && Upgrade->Tier3Gold == 40000)
		{
			++Priced;
		}
		else if (Upgrade->Tier1Gold == 0 && Upgrade->Tier2Gold == 0 && Upgrade->Tier3Gold == 0 && !Upgrade->BranchUndecided)
		{
			++Wrong;
			FirstWrong = Pair.Key.ToString();
		}
		else if (Upgrade->Tier1Gold == 0 && Upgrade->Tier2Gold == 0 && Upgrade->Tier3Gold == 0)
		{
			++Unpriced;
		}
		else
		{
			++Wrong;
			FirstWrong = Pair.Key.ToString();
		}
	}

	TArray<int64> ByTier;
	for (int32 Tier = 1; Tier <= 8; ++Tier)
	{
		ByTier.Add(UCataclysmCityUpgradeMapping::PriceFor(FName(Row), Tier));
	}
	const int64 BelowOne = UCataclysmCityUpgradeMapping::PriceFor(FName(Row), 0);
	const int64 AboveEight = UCataclysmCityUpgradeMapping::PriceFor(FName(Row), 99);
	const int64 NoSuchRow = UCataclysmCityUpgradeMapping::PriceFor(FName(TEXT("Not_A_City_Upgrade")), 1);

	UCataclysmEmpireRun* Run = NewObject<UCataclysmEmpireRun>();
	Run->Begin(/* InSeed */ 2742, ECataclysmSurgeMode::Static, /* LethalityRung */ 0);
	Run->DifficultyTier = 3;
	const int64 InTheRun = UCataclysmCityUpgradePurchase::PriceIn(nullptr, Run, FName(Row), 0);
	const int64 NoRun = UCataclysmCityUpgradePurchase::PriceIn(nullptr, nullptr, FName(Row), 0);

	TArray<FString> Each;
	for (const int64 Price : ByTier)
	{
		Each.Add(FString::Printf(TEXT("%lld"), Price));
	}
	AddInfo(FString::Printf(TEXT("PRICE: %d rows priced 10,000/20,000/40,000, %d unpriced, %d otherwise%s; tiers 1-8 %s; "
								 "tier 0 %lld, tier 99 %lld, no such row %lld; a tier 3 run %lld, no run %lld; "
								 "PriceAt 10,000 x 30/10 = %lld"),
							Priced, Unpriced, Wrong, FirstWrong.IsEmpty() ? TEXT("") : *(TEXT(" (") + FirstWrong + TEXT(")")),
							*FString::Join(Each, TEXT(" ")), BelowOne, AboveEight, NoSuchRow, InTheRun, NoRun,
							UCataclysmCityUpgradeMapping::PriceAt(10000, 30.0f, 10.0f)));

	TestEqual(TEXT("23 rows are priced 10,000, 20,000 and 40,000"), Priced, 23);
	TestEqual(TEXT("one, the unbranched last resort, has no price"), Unpriced, 1);
	TestEqual(TEXT("and no row is priced otherwise"), Wrong, 0);
	for (int32 Index = 0; Index < 8; ++Index)
	{
		TestEqual(FString::Printf(TEXT("the price at tier %d"), Index + 1), ByTier[Index], PriceByTier[Index]);
	}
	TestEqual(TEXT("a tier below 1 is priced as tier 1"), BelowOne, PriceByTier[0]);
	TestEqual(TEXT("a tier above 8 as tier 8"), AboveEight, PriceByTier[7]);
	TestEqual(TEXT("a row the table does not hold costs nothing"), NoSuchRow, static_cast<int64>(0));
	TestEqual(TEXT("a run's price is at its own tier"), InTheRun, PriceByTier[2]);
	TestEqual(TEXT("and with no run, at tier 1"), NoRun, PriceByTier[0]);
	TestEqual(TEXT("the arithmetic: 10,000 x 30 / 10"), UCataclysmCityUpgradeMapping::PriceAt(10000, 30.0f, 10.0f),
			  static_cast<int64>(30000));
	TestEqual(TEXT("a price of nought stays nought"), UCataclysmCityUpgradeMapping::PriceAt(0, 30.0f, 10.0f),
			  static_cast<int64>(0));
	return true;
}

// ---------------------------------------------------------------------------
// Paying
// ---------------------------------------------------------------------------

/**
 * Buying takes the price from the lethality mode's account and saves it: 25,000 held buys a 10,000 upgrade and leaves
 * 15,000, the city holds the upgrade, the writer is told `CityUpgradeBought` last, the purchase starts writes, and the
 * account record read back from its slot holds 15,000. Prints "BOUGHT".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradePriceBoughtTest,
	"Cataclysm.CityUpgradePrice.BuyingTakesThePriceFromTheModesAccountAndSavesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradePriceBoughtTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradePriceTest;

	FPriceScene Scene = MakeScene(/*bSoloSelfFound=*/false, /*bWriting=*/true);
	UCataclysmSaveWriter* Writer = Scene.World ? UCataclysmSaveWriter::In(Scene.World) : nullptr;
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable())
		|| !TestTrue(TEXT("set-up: and its save writer is writing"), Writer != nullptr && Writer->IsWriting()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FString Slot = UCataclysmGameInstance::AccountSlotFor(Scene.Partition);
	Scene.Instance->BankGold(Scene.Partition, 25000);
	const int64 Before = Held(Scene.World);

	const int32 WritesBefore = Writer->WritesStarted();
	const ECataclysmCityUpgradeResult Result =
		UCataclysmCityUpgradePurchase::Buy(Scene.World, Scene.Run, 0, FName(Row));
	const int32 Writes = Writer->WritesStarted() - WritesBefore;
	const ECataclysmSaveTrigger Last = Writer->LastTrigger();
	const int64 After = Held(Scene.World);
	const bool bHeld = Scene.Run->Map->Find(0) != nullptr && Scene.Run->Map->Find(0)->HasUpgrade(FName(Row));
	WaitForTheSlot(Slot);

	ECataclysmSaveLoadResult ReadResult = ECataclysmSaveLoadResult::SlotIsEmpty;
	FString Message;
	const UCataclysmAccountSave* Read = Cast<UCataclysmAccountSave>(FCataclysmSaveStorage::ReadFromSlot(
		Slot, UCataclysmSaveWriter::UserIndex, UCataclysmAccountSave::StaticClass(), GetTransientPackage(), ReadResult,
		Message));

	AddInfo(FString::Printf(TEXT("BOUGHT: %s with %lld held, %lld after; the city holds it %s; last trigger '%s'; %d "
								 "write(s) started; '%s' read back %s holding %lld"),
							*UCataclysmCityUpgradeRules::ResultText(Result), Before, After,
							bHeld ? TEXT("yes") : TEXT("NO"), UCataclysmSaveTriggers::NameOf(Last), Writes, *Slot,
							Read != nullptr ? TEXT("a record") : TEXT("nothing"), Read != nullptr ? Read->Gold : -1));

	TestEqual(TEXT("set-up: 25,000 held"), Before, static_cast<int64>(25000));
	TestEqual(TEXT("it is bought"), static_cast<int32>(Result), static_cast<int32>(ECataclysmCityUpgradeResult::Bought));
	TestEqual(TEXT("for 10,000, leaving 15,000"), After, static_cast<int64>(15000));
	TestTrue(TEXT("and the city holds it"), bHeld);
	TestEqual(TEXT("the writer was told a city upgrade was bought, last"), static_cast<int32>(Last),
			  static_cast<int32>(ECataclysmSaveTrigger::CityUpgradeBought));
	TestTrue(TEXT("the purchase started writes: the account record's, and the run and character records'"), Writes >= 2);
	if (!TestNotNull(TEXT("the account record reads back from its slot"), Read))
	{
		return false;
	}
	TestEqual(TEXT("holding the 15,000 left"), Read->Gold, static_cast<int64>(15000));
	return true;
}

/**
 * A purchase reaches the run record on disk even in a frame whose run record was already written. An item's trigger
 * writes the run record first; a clock write in the same frame is then skipped, which shows the once-a-frame rule is
 * in force; and the purchase's write is not skipped, so the run record read back from its slot holds the city's
 * upgrade. The coordinating session asked for this on 2026-11-18: written apart from the account's gold, a skipped
 * run record would keep the gold spent and lose the upgrade. Prints "SAME FRAME".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradePriceSameFrameTest,
	"Cataclysm.CityUpgradePrice.APurchaseInAFrameAlreadyWrittenStillReachesTheRunRecord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradePriceSameFrameTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradePriceTest;

	FPriceScene Scene = MakeScene(/*bSoloSelfFound=*/false, /*bWriting=*/true);
	UCataclysmSaveWriter* Writer = Scene.World ? UCataclysmSaveWriter::In(Scene.World) : nullptr;
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable())
		|| !TestTrue(TEXT("set-up: and its save writer is writing"), Writer != nullptr && Writer->IsWriting()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FString RunSlot = Writer->RunSlotName();
	const FString CharacterSlot = Writer->CharacterSlotName();
	ON_SCOPE_EXIT { DeleteSlot(RunSlot); DeleteSlot(CharacterSlot); };

	Scene.Instance->BankGold(Scene.Partition, 25000);

	// THE RUN RECORD WRITTEN FIRST THIS FRAME, by an item's trigger, which writes it. A test's frame does not end.
	const int32 Start = Writer->WritesStarted();
	Writer->NoteTrigger(ECataclysmSaveTrigger::InventoryChanged);
	const int32 AfterItem = Writer->WritesStarted();
	// CONTROL: THE ONCE-A-FRAME RULE IS IN FORCE. A clock write in the same frame is skipped.
	Writer->NoteTrigger(ECataclysmSaveTrigger::Clock);
	const int32 AfterClock = Writer->WritesStarted();

	const ECataclysmCityUpgradeResult Result =
		UCataclysmCityUpgradePurchase::Buy(Scene.World, Scene.Run, 0, FName(Row));
	const int32 AfterPurchase = Writer->WritesStarted();
	WaitForTheSlot(RunSlot);

	ECataclysmSaveLoadResult ReadResult = ECataclysmSaveLoadResult::SlotIsEmpty;
	FString Message;
	const UCataclysmRunSave* Read = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::ReadFromSlot(
		RunSlot, UCataclysmSaveWriter::UserIndex, UCataclysmRunSave::StaticClass(), GetTransientPackage(), ReadResult,
		Message));
	bool bOnDisk = false;
	if (Read != nullptr)
	{
		for (const FCataclysmCity& City : Read->Cities)
		{
			bOnDisk |= City.CityId == 0 && City.HasUpgrade(FName(Row));
		}
	}

	AddInfo(FString::Printf(TEXT("SAME FRAME: the item's trigger started %d write(s), the clock after it %d; the purchase "
								 "%s started %d; '%s' read back %s, city 0 holding the upgrade %s"),
							AfterItem - Start, AfterClock - AfterItem, *UCataclysmCityUpgradeRules::ResultText(Result),
							AfterPurchase - AfterClock, *RunSlot, Read != nullptr ? TEXT("a record") : TEXT("nothing"),
							bOnDisk ? TEXT("yes") : TEXT("NO")));

	TestTrue(TEXT("set-up: the item's trigger wrote the run record this frame"), AfterItem > Start);
	TestEqual(TEXT("CONTROL: a clock write in the same frame is skipped"), AfterClock, AfterItem);
	TestEqual(TEXT("set-up: the purchase goes through"), static_cast<int32>(Result),
			  static_cast<int32>(ECataclysmCityUpgradeResult::Bought));
	TestTrue(TEXT("the purchase starts writes in the same frame"), AfterPurchase > AfterClock);
	if (!TestNotNull(TEXT("the run record reads back from its slot"), Read))
	{
		return false;
	}
	TestTrue(TEXT("and its city 0 holds the upgrade bought"), bOnDisk);
	return true;
}

/**
 * A Solo Self-Found character pays from its own gold, as it earns into its own: 12,000 of its own buys a 10,000
 * upgrade and leaves 2,000, and its lethality mode's account is untouched. Prints "OWN".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradePriceOwnTest,
	"Cataclysm.CityUpgradePrice.ASoloSelfFoundCharacterPaysFromItsOwnGold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradePriceOwnTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradePriceTest;

	FPriceScene Scene = MakeScene(/*bSoloSelfFound=*/true);
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FCataclysmCharacterPartition Mode = Scene.Partition;
	Mode.bSoloSelfFound = false;
	Scene.State->SetOwnGold(12000);

	const ECataclysmCityUpgradeResult Result =
		UCataclysmCityUpgradePurchase::Buy(Scene.World, Scene.Run, 0, FName(Row));
	const UCataclysmAccountSave* ModesAccount = Scene.Instance->AccountFor(Mode);

	AddInfo(FString::Printf(TEXT("OWN: %s; its own %lld; the mode's account %lld"),
							*UCataclysmCityUpgradeRules::ResultText(Result), Scene.State->GetOwnGold(),
							ModesAccount != nullptr ? ModesAccount->Gold : -1));

	TestEqual(TEXT("it is bought"), static_cast<int32>(Result), static_cast<int32>(ECataclysmCityUpgradeResult::Bought));
	TestEqual(TEXT("from its own 12,000, leaving 2,000"), Scene.State->GetOwnGold(), static_cast<int64>(2000));
	TestTrue(TEXT("and its lethality mode's account holds none, untouched"),
			 ModesAccount != nullptr && ModesAccount->Gold == 0);
	return true;
}

/**
 * Too little gold is refused and nothing is taken: 9,999 against a 10,000 price says "Costs 10,000 gold; 1 more
 * needed.", the balance and the city stay as they were. A world with nobody playing has no balance and is refused the
 * same way. A fallen city says it has fallen, whatever gold is held. Prints "SHORT".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradePriceShortTest,
	"Cataclysm.CityUpgradePrice.TooLittleGoldIsRefusedAndNothingIsTaken",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradePriceShortTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradePriceTest;

	FPriceScene Scene = MakeScene();
	UWorld* Bare = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT
	{
		for (UWorld* World : { Scene.World, Bare })
		{
			if (World)
			{
				World->DestroyWorld(/*bInformEngineOfWorld=*/false);
			}
		}
	};
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable())
		|| !TestNotNull(TEXT("set-up: and a bare world"), Bare))
	{
		return false;
	}

	Scene.Instance->BankGold(Scene.Partition, 9999);
	const ECataclysmCityUpgradeResult Short =
		UCataclysmCityUpgradePurchase::Buy(Scene.World, Scene.Run, 0, FName(Row));
	const FString Words = UCataclysmCityUpgradePurchase::RefusalText(Short, 10000, 9999);
	const int64 After = Held(Scene.World);
	const int32 HeldUpgrades = Scene.Run->Map->Find(0) ? Scene.Run->Map->Find(0)->Upgrades.Num() : -1;

	const ECataclysmCityUpgradeResult Nobody = UCataclysmCityUpgradePurchase::Buy(Bare, Scene.Run, 0, FName(Row));

	Scene.Instance->BankGold(Scene.Partition, 1000000);
	Scene.Run->Map->Fall(1);
	const ECataclysmCityUpgradeResult Fallen =
		UCataclysmCityUpgradePurchase::Buy(Scene.World, Scene.Run, 1, FName(Row));

	AddInfo(FString::Printf(TEXT("SHORT: 9,999 against 10,000 %s, \"%s\", %lld after, %d upgrade(s) held; nobody "
								 "playing %s; a fallen city with 1,009,999 %s"),
							*UCataclysmCityUpgradeRules::ResultText(Short), *Words, After, HeldUpgrades,
							*UCataclysmCityUpgradeRules::ResultText(Nobody),
							*UCataclysmCityUpgradeRules::ResultText(Fallen)));

	TestEqual(TEXT("9,999 against 10,000 is refused for gold"), static_cast<int32>(Short),
			  static_cast<int32>(ECataclysmCityUpgradeResult::NotEnoughGold));
	TestEqual(TEXT("saying the price and what is missing"), Words, FString(TEXT("Costs 10,000 gold; 1 more needed.")));
	TestEqual(TEXT("nothing is taken"), After, static_cast<int64>(9999));
	TestEqual(TEXT("and the city holds nothing"), HeldUpgrades, 0);
	TestEqual(TEXT("a world with nobody playing has no balance and is refused for gold"), static_cast<int32>(Nobody),
			  static_cast<int32>(ECataclysmCityUpgradeResult::NotEnoughGold));
	TestEqual(TEXT("a fallen city says it has fallen, whatever gold is held"), static_cast<int32>(Fallen),
			  static_cast<int32>(ECataclysmCityUpgradeResult::CityHasFallen));
	return true;
}

// ---------------------------------------------------------------------------
// What the screens say
// ---------------------------------------------------------------------------

/**
 * The city screen shows each price, the balance and what is missing. With 25,000 held every built upgrade can be
 * bought, each priced 10,000, its button "<sentence>  --  10,000 gold"; with 5,000 held each is short of gold, its
 * button saying "Costs 10,000 gold; 5,000 more needed."; at difficulty tier 2 each costs 30,000. The slot line ends with
 * the balance where there is one. And the play loop line names what was bought, where, for how much and what is left.
 * Prints "SCREEN".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradePriceScreenTest,
	"Cataclysm.CityUpgradePrice.TheCityScreenShowsEachPriceTheBalanceAndWhatIsMissing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradePriceScreenTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradePriceTest;

	FPriceScene Scene = MakeScene();
	UWorld* Bare = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT
	{
		for (UWorld* World : { Scene.World, Bare })
		{
			if (World)
			{
				World->DestroyWorld(/*bInformEngineOfWorld=*/false);
			}
		}
	};
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable())
		|| !TestNotNull(TEXT("set-up: and a bare world"), Bare))
	{
		return false;
	}

	const TArray<FCataclysmCityUpgradeOffer> Rich = UCataclysmCityScreenLayout::OffersFor(Scene.World, Scene.Run, 0, 25000);
	const TArray<FCataclysmCityUpgradeOffer> Poor = UCataclysmCityScreenLayout::OffersFor(Scene.World, Scene.Run, 0, 5000);
	Scene.Run->DifficultyTier = 2;
	const TArray<FCataclysmCityUpgradeOffer> TierTwo = UCataclysmCityScreenLayout::OffersFor(Scene.World, Scene.Run, 0, 25000);
	Scene.Run->DifficultyTier = 1;
	if (!TestEqual(TEXT("set-up: every upgrade is offered"), Rich.Num(), 24) || Poor.Num() != 24 || TierTwo.Num() != 24)
	{
		return false;
	}

	int32 Buyable = 0;
	int32 Short = 0;
	int32 ShortAtTierTwo = 0;
	bool bEveryRichPriceRight = true;
	bool bEveryPoorRefusalRight = true;
	FString RichButton;
	FString PoorButton;
	FString NotBuiltLine;
	for (int32 Index = 0; Index < Rich.Num(); ++Index)
	{
		if (Rich[Index].bCanBuy)
		{
			++Buyable;
			bEveryRichPriceRight &= Rich[Index].Price == 10000;
			if (RichButton.IsEmpty())
			{
				RichButton = UCataclysmCityScreenLayout::ButtonTextFor(Rich[Index]);
			}
		}
		else if (NotBuiltLine.IsEmpty())
		{
			NotBuiltLine = UCataclysmCityScreenLayout::NotBuiltLineFor(Rich[Index]);
		}
		if (Poor[Index].bShortOfGold)
		{
			++Short;
			bEveryPoorRefusalRight &= Poor[Index].Refusal == TEXT("Costs 10,000 gold; 5,000 more needed.");
			if (PoorButton.IsEmpty())
			{
				PoorButton = UCataclysmCityScreenLayout::ButtonTextFor(Poor[Index]);
			}
		}
		ShortAtTierTwo += TierTwo[Index].bShortOfGold && TierTwo[Index].Price == 30000 ? 1 : 0;
	}

	Scene.Instance->BankGold(Scene.Partition, 25000);
	const FString SlotsWithGold = UCataclysmCityScreenLayout::SlotsAndGoldTextFor(Scene.World, Scene.Run, 0);
	const FString SlotsWithout = UCataclysmCityScreenLayout::SlotsAndGoldTextFor(Bare, Scene.Run, 0);
	const FString CityName = Scene.Run->Map->Find(0) ? Scene.Run->Map->Find(0)->Name : FString();
	const FString Line = FCataclysmPlayLoopLog::UpgradeLine(TEXT("Increase max defense by 20%"), Scene.Run, 0, 10000,
															15000);
	const FString NoBalanceLine = FCataclysmPlayLoopLog::UpgradeLine(TEXT("Increase max defense by 20%"), Scene.Run, 0,
																	 10000, -1);

	AddInfo(FString::Printf(TEXT("SCREEN: with 25,000 %d buyable, every one 10,000 %s, button \"%s\"; with 5,000 %d "
								 "short, refusals right %s, button \"%s\"; at tier 2 %d short at 30,000; not built \"%s\"; "
								 "slots \"%s\" / without a balance \"%s\"; line \"%s\" / \"%s\""),
							Buyable, bEveryRichPriceRight ? TEXT("yes") : TEXT("NO"), *RichButton, Short,
							bEveryPoorRefusalRight ? TEXT("yes") : TEXT("NO"), *PoorButton, ShortAtTierTwo,
							*NotBuiltLine, *SlotsWithGold, *SlotsWithout, *Line, *NoBalanceLine));

	TestEqual(TEXT("with 25,000 every built upgrade can be bought"), Buyable,
			  UCataclysmCityUpgradeRules::BuiltEffectCount());
	TestTrue(TEXT("each priced 10,000"), bEveryRichPriceRight);
	TestTrue(TEXT("its button says the price"), RichButton.EndsWith(TEXT("  --  10,000 gold")));
	TestEqual(TEXT("with 5,000 every one is short of gold"), Short, UCataclysmCityUpgradeRules::BuiltEffectCount());
	TestTrue(TEXT("each saying the price and what is missing"), bEveryPoorRefusalRight);
	TestTrue(TEXT("on its button"), PoorButton.EndsWith(TEXT("  --  Costs 10,000 gold; 5,000 more needed.")));
	TestEqual(TEXT("at tier 2 each costs 30,000, which 25,000 does not reach"), ShortAtTierTwo,
			  UCataclysmCityUpgradeRules::BuiltEffectCount());
	TestFalse(TEXT("set-up: a line for an upgrade that is not built was read"), NotBuiltLine.IsEmpty());
	TestFalse(TEXT("an upgrade that is not built shows no price"), NotBuiltLine.Contains(TEXT("10,000")));
	TestEqual(TEXT("the slot line ends with the balance"), SlotsWithGold,
			  FString(TEXT("0 of 3 upgrade slots filled.   25,000 gold")));
	TestEqual(TEXT("and has none where there is no balance"), SlotsWithout, FString(TEXT("0 of 3 upgrade slots filled.")));
	TestEqual(TEXT("the play loop line"), Line,
			  FString::Printf(TEXT("bought 'Increase max defense by 20%%' on %s for 10,000 gold; 15,000 gold held"),
							  *CityName));
	TestEqual(TEXT("and with no balance to name"), NoBalanceLine,
			  FString::Printf(TEXT("bought 'Increase max defense by 20%%' on %s for 10,000 gold; no gold balance to "
								   "hold it"), *CityName));
	return true;
}

// ---------------------------------------------------------------------------
// The empire upgrade tree's price nodes, issue #2741 slice 6a
// ---------------------------------------------------------------------------

/**
 * THE PRICE SHOWN IS THE PRICE PAID, with the tree's price nodes bought (P1, P2): Municipal Bonds at 10 points (a 0.30
 * reduction) and War Funding at 10 (0.20, on a city holding no upgrade) in the mode's account. City 0 holds none, so the
 * city screen offers the row at 10,000 x 0.50 = 5,000; `PriceIn` answers the same; buying it takes 5,000 from the
 * balance; and the next built upgrade on the same city, which now holds one, costs 10,000 x 0.70 = 7,000. Prints
 * "TREE PRICE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCityUpgradePriceTreeTest,
	"Cataclysm.CityUpgradePrice.TheTreesPriceNodesMakeTheScreensPriceThePricePaid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCityUpgradePriceTreeTest::RunTest(const FString&)
{
	using namespace CataclysmCityUpgradePriceTest;

	FPriceScene Scene = MakeScene();
	if (!TestTrue(TEXT("set-up: the scene was built"), Scene.IsUsable()))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	Scene.Instance->BankGold(Scene.Partition, 25000);
	UCataclysmAccountSave* Account = Scene.Instance->AccountFor(Scene.Partition);
	if (!TestNotNull(TEXT("set-up: the mode's account"), Account))
	{
		return false;
	}
	Account->EmpireTree.Add(FName(TEXT("basic_0000003v")), 10);		// Municipal Bonds, -3% a point
	Account->EmpireTree.Add(FName(TEXT("basic_0000003s")), 10);		// War Funding, -2% a point on a city's first

	const FName RowName(Row);
	const TArray<FCataclysmCityUpgradeOffer> Offers =
		UCataclysmCityScreenLayout::OffersFor(Scene.World, Scene.Run, 0, Held(Scene.World));
	const FCataclysmCityUpgradeOffer* Offer = Offers.FindByPredicate(
		[&RowName](const FCataclysmCityUpgradeOffer& One) { return One.RowName == RowName; });
	const int64 Shown = Offer != nullptr ? Offer->Price : -1;
	const int64 Asked = UCataclysmCityUpgradePurchase::PriceIn(Scene.World, Scene.Run, RowName, 0);
	const int64 Before = Held(Scene.World);
	const ECataclysmCityUpgradeResult Result = UCataclysmCityUpgradePurchase::Buy(Scene.World, Scene.Run, 0, RowName);
	const int64 After = Held(Scene.World);

	// THE NEXT ONE ON THE SAME CITY: the first other upgrade the game has built.
	FName NextRow;
	for (const FName Name : UCataclysmCityUpgradeMapping::AllRowNames())
	{
		if (Name != RowName && UCataclysmCityUpgradeRules::IsBuilt(UCataclysmCityUpgradeMapping::MakeFromTable(Name).Effect))
		{
			NextRow = Name;
			break;
		}
	}
	const int64 Next = UCataclysmCityUpgradePurchase::PriceIn(Scene.World, Scene.Run, NextRow, 0);
	AddInfo(FString::Printf(TEXT("TREE PRICE: shown %lld, asked %lld; %s with %lld held, %lld after; the next (%s) %lld"),
							Shown, Asked, *UCataclysmCityUpgradeRules::ResultText(Result), Before, After,
							*NextRow.ToString(), Next));

	TestEqual(TEXT("the screen offers it at 5,000"), Shown, static_cast<int64>(5000));
	TestEqual(TEXT("PriceIn answers the same"), Asked, Shown);
	TestTrue(TEXT("it is bought"), Result == ECataclysmCityUpgradeResult::Bought);
	TestEqual(TEXT("and the balance pays exactly the price shown"), Before - After, Shown);
	TestFalse(TEXT("set-up: another built upgrade to price"), NextRow.IsNone());
	TestEqual(TEXT("the next on the same city, which now holds one, is 7,000"), Next, static_cast<int64>(7000));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
