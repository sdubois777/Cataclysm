// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Interface/CataclysmEmpireMapLayout.h"
#include "Interface/CataclysmInventoryScreen.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayLoopLog.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveApply.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveMigration.h"
#include "Save/CataclysmSavePartition.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Save/CataclysmSaveWriter.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Gold: creatures drop it, the account holds it once per lethality mode, and the empire map shows it. Issue #2742,
 * slice 1, ruled by the owner on 2026-11-18 ("Creature kills") and by the coordinating session the same day (G1 to G6).
 *
 * WHAT IS CHECKED HERE: the pile's size by tier and floor, the piles' count by rung, which worlds drop gold at all,
 * a kill's gold on the floor, picking it up into the balance, the balance's save and the piles' save, the two
 * migration steps, and every line of words a player or the play loop log reads. The C++ the Python suite reads is
 * checked by `tools/tests/test_gold_drops_and_the_balance.py`.
 *
 * THE SLOT IS THE TEST'S OWN. While an automation test runs, `UCataclysmGameInstance::AccountSlotFor` ends an account
 * slot in `_AutomationTest`, so nothing here touches the player's own balance; each scene deletes that slot first, and
 * `FCataclysmTestSaves` deletes it again when the test ends.
 *
 * A WORLD BUILT BY A TEST HAS NO GAME MODE, so the difficulty tier is 1 and the floor is 1 there: every pile a kill or
 * `SpawnGoldFor` puts down in these tests holds 8 to 12 gold.
 */
namespace CataclysmGoldTest
{
	/** A world with this project's game instance and a possessed player of one partition. */
	struct FGoldScene
	{
		UWorld* World = nullptr;
		UCataclysmGameInstance* Instance = nullptr;
		ACataclysmPlayerState* State = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		FCataclysmCharacterPartition Partition;

		bool IsUsable() const { return World && Instance && State && Character; }
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
	 * The scene: a partition of the lethality mode given, offline, the test's account slot emptied first. `bWriting`
	 * begins the save writer, as play does, so `ECataclysmSaveTrigger::AccountChanged` writes the balance.
	 */
	FGoldScene MakeScene(ECataclysmLethality Lethality = ECataclysmLethality::Standard, bool bSoloSelfFound = false,
						 bool bWriting = false)
	{
		FGoldScene Out;
		Out.Partition.Lethality = Lethality;
		Out.Partition.Population = ECataclysmPopulation::Offline;
		Out.Partition.bSoloSelfFound = bSoloSelfFound;

		const FString Slot = UCataclysmGameInstance::AccountSlotFor(Out.Partition);
		if (!Slot.IsEmpty())
		{
			DeleteSlot(Slot);
		}

		Out.World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Out.World)
		{
			return Out;
		}

		Out.Instance = NewObject<UCataclysmGameInstance>();
		Out.World->SetGameInstance(Out.Instance);

		Out.State = Out.World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = Out.World->SpawnActor<APlayerController>();
		Out.Character = Out.World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Out.State && Controller && Out.Character)
		{
			Controller->SetPlayerState(Out.State);
			Controller->Possess(Out.Character);
			Out.State->SetIdentity(TEXT("Goldfinder"), Out.Partition);
		}

		if (bWriting)
		{
			if (UCataclysmSaveWriter* Writer = UCataclysmSaveWriter::In(Out.World))
			{
				Writer->BeginRun(FGuid::NewGuid(), FGuid::NewGuid(), FName(TEXT("Sandbox")), 1);
			}
		}
		return Out;
	}

	/** The scene's balance, or -1 when there is no account record for it. */
	int64 Balance(const FGoldScene& Scene)
	{
		const UCataclysmAccountSave* Account = Scene.Instance->AccountFor(Scene.Partition);
		return Account != nullptr ? Account->Gold : -1;
	}

	/** The balance the scene's character has, read through the one function every screen reads, or -1 for none. */
	int64 Held(const FGoldScene& Scene)
	{
		int64 Gold = 0;
		return UCataclysmGameInstance::GoldHeldBy(Scene.World, Gold) ? Gold : -1;
	}

	/** Every gold pile lying in the world. */
	TArray<ACataclysmDroppedItem*> PilesIn(UWorld* World)
	{
		TArray<ACataclysmDroppedItem*> Out;
		for (TActorIterator<ACataclysmDroppedItem> It(World); It; ++It)
		{
			if (IsValid(*It) && It->IsGold())
			{
				Out.Add(*It);
			}
		}
		return Out;
	}

	/** Kills `Kills` creatures of this rung in the world, each with its own corpse, and answers where each died. */
	TArray<FVector> Kill(UWorld* World, int32 RarityStep, int32 Kills)
	{
		TArray<FVector> Corpses;
		for (int32 Index = 0; Index < Kills; ++Index)
		{
			ACataclysmEnemyCharacter* Victim = World->SpawnActor<ACataclysmEnemyCharacter>(
				ACataclysmEnemyCharacter::StaticClass(), FVector(2000.0f + 600.0f * static_cast<float>(Index), 0.0f, 0.0f),
				FRotator::ZeroRotator);
			if (Victim != nullptr)
			{
				Victim->SetRarityStep(RarityStep);
				Corpses.Add(Victim->GetActorLocation());
				Victim->HandleDeath();
			}
		}
		return Corpses;
	}

	/** The rung a Cataclysm Boss is: twelve piles expected, so no pile at all is about six chances in a million. */
	constexpr int32 CataclysmBossStep = 5;
}

// ---------------------------------------------------------------------------
// How much a pile holds, and how many a kill drops
// ---------------------------------------------------------------------------

/**
 * A pile holds the Gold sheet's figure for its tier, plus 4% of it for each floor deeper: 10 at tier 1 on floor 1, 20
 * at tier 1 on floor 26, 300 at tier 8 on floor 1. A tier outside 1 to 8 is held inside them, and a floor below 1 is
 * floor 1. And a rolled pile is 0.8 to 1.2 of its mean, a whole number. Prints "PILE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGoldPileSizeTest,
	"Cataclysm.Gold.APileHoldsItsTiersFigurePlusFourPerCentAFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGoldPileSizeTest::RunTest(const FString&)
{
	const UDataTable* Gold = UCataclysmDropRoll::LoadGoldTable();
	if (!TestNotNull(TEXT("set-up: DT_Gold loads"), Gold))
	{
		return false;
	}

	const float TierOneFloorOne = UCataclysmDropRoll::GoldPileMean(Gold, 1, 1);
	const float TierOneFloorTwentySix = UCataclysmDropRoll::GoldPileMean(Gold, 1, 26);
	const float TierEightFloorOne = UCataclysmDropRoll::GoldPileMean(Gold, 8, 1);
	const float TierTwoFloorOne = UCataclysmDropRoll::GoldPileMean(Gold, 2, 1);
	const float BelowTheTiers = UCataclysmDropRoll::GoldPileMean(Gold, 0, 1);
	const float AboveTheTiers = UCataclysmDropRoll::GoldPileMean(Gold, 99, 1);
	const float NoDungeon = UCataclysmDropRoll::GoldPileMean(Gold, 1, 0);

	FRandomStream Stream(2742);
	int32 Lowest = MAX_int32;
	int32 Highest = 0;
	int64 Sum = 0;
	constexpr int32 Rolls = 4000;
	for (int32 Index = 0; Index < Rolls; ++Index)
	{
		const int32 Pile = UCataclysmDropRoll::RollGoldPile(Gold, 1, 1, Stream);
		Lowest = FMath::Min(Lowest, Pile);
		Highest = FMath::Max(Highest, Pile);
		Sum += Pile;
	}
	const double Average = static_cast<double>(Sum) / static_cast<double>(Rolls);

	AddInfo(FString::Printf(TEXT("PILE: tier 1 floor 1 %g, floor 26 %g; tier 2 %g, tier 8 %g; tier 0 %g, tier 99 %g, "
								 "floor 0 %g; %d rolls at tier 1 floor 1 from %d to %d, averaging %.3f"),
							TierOneFloorOne, TierOneFloorTwentySix, TierTwoFloorOne, TierEightFloorOne, BelowTheTiers,
							AboveTheTiers, NoDungeon, Rolls, Lowest, Highest, Average));

	TestEqual(TEXT("a pile on floor 1 of tier 1 holds 10"), TierOneFloorOne, 10.0f);
	TestEqual(TEXT("floor 26 holds twice that, 4% of it a floor deeper"), TierOneFloorTwentySix, 20.0f);
	TestEqual(TEXT("tier 2 holds 30, its scale of 3"), TierTwoFloorOne, 30.0f);
	TestEqual(TEXT("tier 8 holds 300, its scale of 30"), TierEightFloorOne, 300.0f);
	TestEqual(TEXT("a tier below 1 is tier 1"), BelowTheTiers, 10.0f);
	TestEqual(TEXT("a tier above 8 is tier 8"), AboveTheTiers, 300.0f);
	TestEqual(TEXT("floor 0, no dungeon, is floor 1"), NoDungeon, 10.0f);
	TestTrue(TEXT("a rolled pile is never below 0.8 of 10"), Lowest >= 8);
	TestTrue(TEXT("and never above 1.2 of it"), Highest <= 12);
	TestTrue(TEXT("CONTROL: both ends of the spread come up in 4000 rolls"), Lowest == 8 && Highest == 12);
	TestTrue(TEXT("and the rolls average the mean"), FMath::Abs(Average - 10.0) < 0.1);
	TestEqual(TEXT("a missing table rolls nothing"), UCataclysmDropRoll::RollGoldPile(nullptr, 1, 1, Stream), 0);
	return true;
}

/**
 * A kill's piles follow the gear ladder, the ruling's "as gear and materials drop today", and no loot quantity moves
 * them: the expected piles of every rung equal its expected gear at the baseline, and stay there when loot quantity is
 * quadrupled while the gear does not. Prints "LADDER".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGoldLadderTest,
	"Cataclysm.Gold.AKillsPilesFollowTheGearLadderAndNoLootQuantityMovesThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGoldLadderTest::RunTest(const FString&)
{
	const UDataTable* Drops = UCataclysmDropRoll::LoadEnemyDropTable();
	if (!TestNotNull(TEXT("set-up: DT_EnemyDrops loads"), Drops))
	{
		return false;
	}

	TArray<FString> Lines;
	int32 Rungs = 0;
	for (int32 Step = 0; Step < 6; ++Step)
	{
		const FName Rarity = UCataclysmDropSpawner::EnemyRarityForStep(Drops, Step);
		if (Rarity.IsNone())
		{
			continue;
		}
		++Rungs;
		const float Piles = UCataclysmDropRoll::ExpectedGoldDrops(Drops, Rarity);
		const float Gear = UCataclysmDropRoll::ExpectedGearDrops(Drops, Rarity, UCataclysmDropRoll::BaselineLootQuantity);
		const float GearFourTimes = UCataclysmDropRoll::ExpectedGearDrops(Drops, Rarity, 400.0f);
		Lines.Add(FString::Printf(TEXT("%s %g piles, %g gear, %g gear at 400%%"), *Rarity.ToString(), Piles, Gear,
								  GearFourTimes));
		TestEqual(*FString::Printf(TEXT("%s: its piles are its gear"), *Rarity.ToString()), Piles, Gear);
		TestTrue(*FString::Printf(TEXT("%s: CONTROL: loot quantity does move its gear"), *Rarity.ToString()),
				 GearFourTimes > Gear);
	}
	AddInfo(TEXT("LADDER: ") + FString::Join(Lines, TEXT("; ")));
	TestEqual(TEXT("CONTROL: all six rungs were read"), Rungs, 6);
	return true;
}

// ---------------------------------------------------------------------------
// Which kills drop gold
// ---------------------------------------------------------------------------

/**
 * Gold drops where the character playing has a balance: not in a world with no game instance of this project's, not
 * in one with nobody playing; yes for a Standard character and a Heretic one, and yes for a Solo Self-Found one, into
 * its own balance -- the owner's ruling of 2026-11-18, which reversed the coordinating session's G5. Prints "WHO".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGoldWhoTest,
	"Cataclysm.Gold.GoldDropsOnlyWhereTheCharacterPlayingHasABalance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGoldWhoTest::RunTest(const FString&)
{
	using namespace CataclysmGoldTest;

	UWorld* Bare = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	FGoldScene Standard = MakeScene(ECataclysmLethality::Standard);
	FGoldScene Heretic = MakeScene(ECataclysmLethality::Heretic);
	FGoldScene Solo = MakeScene(ECataclysmLethality::Standard, /*bSoloSelfFound=*/true);
	ON_SCOPE_EXIT
	{
		for (UWorld* World : { Bare, Standard.World, Heretic.World, Solo.World })
		{
			if (World)
			{
				World->DestroyWorld(/*bInformEngineOfWorld=*/false);
			}
		}
	};
	if (!TestNotNull(TEXT("set-up: a bare world"), Bare)
		|| !TestTrue(TEXT("set-up: the three scenes were built"),
					 Standard.IsUsable() && Heretic.IsUsable() && Solo.IsUsable()))
	{
		return false;
	}

	// A WORLD WITH THE GAME INSTANCE AND NOBODY PLAYING: the same scene's instance, in a world of its own.
	UWorld* Unplayed = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { if (Unplayed) { Unplayed->DestroyWorld(false); } };
	if (!TestNotNull(TEXT("set-up: a world nobody plays"), Unplayed))
	{
		return false;
	}
	Unplayed->SetGameInstance(NewObject<UCataclysmGameInstance>());

	const bool bBare = UCataclysmDropSpawner::GoldDropsIn(Bare);
	const bool bUnplayed = UCataclysmDropSpawner::GoldDropsIn(Unplayed);
	const bool bStandard = UCataclysmDropSpawner::GoldDropsIn(Standard.World);
	const bool bHeretic = UCataclysmDropSpawner::GoldDropsIn(Heretic.World);
	const bool bSolo = UCataclysmDropSpawner::GoldDropsIn(Solo.World);
	AddInfo(FString::Printf(TEXT("WHO: bare %d, nobody playing %d, Standard %d, Heretic %d, Solo Self-Found %d"),
							bBare, bUnplayed, bStandard, bHeretic, bSolo));

	TestFalse(TEXT("no gold in a world with no game instance of this project's"), bBare);
	TestFalse(TEXT("none in a world with nobody playing"), bUnplayed);
	TestTrue(TEXT("gold for a Standard character"), bStandard);
	TestTrue(TEXT("and for a Heretic one"), bHeretic);
	TestTrue(TEXT("and for a Solo Self-Found one, the owner's ruling of 2026-11-18"), bSolo);
	TestFalse(TEXT("and nothing for no world"), UCataclysmDropSpawner::GoldDropsIn(nullptr));
	return true;
}

/**
 * Killing creatures where a character is playing puts gold piles on the floor, named "N gold" in the gold colour with
 * the thinnest border, each holding 8 to 12 at tier 1 floor 1, and none on the spot the kill's lone item takes; the same
 * kills for a Solo Self-Found character put gold down too. Three Cataclysm Bosses each way. Prints "KILLS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGoldKillsTest,
	"Cataclysm.Gold.KillingCreaturesPutsNamedGoldPilesOnTheFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGoldKillsTest::RunTest(const FString&)
{
	using namespace CataclysmGoldTest;

	FGoldScene Scene = MakeScene();
	FGoldScene Solo = MakeScene(ECataclysmLethality::Standard, /*bSoloSelfFound=*/true);
	ON_SCOPE_EXIT
	{
		for (UWorld* World : { Scene.World, Solo.World })
		{
			if (World)
			{
				World->DestroyWorld(/*bInformEngineOfWorld=*/false);
			}
		}
	};
	if (!TestTrue(TEXT("set-up: the two scenes were built"), Scene.IsUsable() && Solo.IsUsable()))
	{
		return false;
	}

	TestEqual(TEXT("CONTROL: no gold lies on the floor before the kills"), PilesIn(Scene.World).Num(), 0);
	const TArray<FVector> Corpses = Kill(Scene.World, CataclysmBossStep, 3);
	Kill(Solo.World, CataclysmBossStep, 3);

	const TArray<ACataclysmDroppedItem*> Piles = PilesIn(Scene.World);
	int32 Named = 0;
	int32 InRange = 0;
	int32 Coloured = 0;
	int32 Thin = 0;
	int32 OnACorpse = 0;
	for (const ACataclysmDroppedItem* Pile : Piles)
	{
		Named += Pile->DisplayName == ACataclysmDroppedItem::GoldName(Pile->Gold) ? 1 : 0;
		InRange += Pile->Gold >= 8 && Pile->Gold <= 12 ? 1 : 0;
		Coloured += Pile->NameColour.Equals(ACataclysmDroppedItem::GoldNameColour) ? 1 : 0;
		Thin += UCataclysmDropPickup::NameBorderThicknessOf(*Pile) == UCataclysmDropPickup::ThinnestNameBorderPx ? 1 : 0;
		// WHERE A LONE ITEM LIES: on its corpse, which `ScatterOffset` gives a count of one. A pile's ring is 50 cm out
		// at the least.
		for (const FVector& Corpse : Corpses)
		{
			OnACorpse += FVector::Dist2D(Pile->GetActorLocation(), Corpse) < 25.0f ? 1 : 0;
		}
	}
	const int32 SoloPiles = PilesIn(Solo.World).Num();

	AddInfo(FString::Printf(TEXT("KILLS: %d piles from three Cataclysm Bosses; %d named, %d holding 8 to 12, %d in the gold "
								 "colour, %d with the thinnest border, %d on a corpse; Solo Self-Found: %d piles"),
							Piles.Num(), Named, InRange, Coloured, Thin, OnACorpse, SoloPiles));

	TestTrue(TEXT("killing three Cataclysm Bosses put gold on the floor"), Piles.Num() > 0);
	TestEqual(TEXT("each pile is named for its gold"), Named, Piles.Num());
	TestEqual(TEXT("each holds 8 to 12 at tier 1 on floor 1"), InRange, Piles.Num());
	TestEqual(TEXT("each is drawn in the gold colour"), Coloured, Piles.Num());
	TestEqual(TEXT("each has the thinnest border"), Thin, Piles.Num());
	TestEqual(TEXT("CONTROL: three corpses were placed"), Corpses.Num(), 3);
	TestEqual(TEXT("none lies where the kill's lone item would"), OnACorpse, 0);
	TestTrue(TEXT("a Solo Self-Found character's kills put gold down too"), SoloPiles > 0);
	return true;
}

// ---------------------------------------------------------------------------
// Picking it up
// ---------------------------------------------------------------------------

/**
 * Banking piles adds their gold to the lethality mode's balance in the account, takes them off the floor, and counts
 * them as this walk's; a pile clicked by hand is banked the same way and not carried; the character's own balance is
 * untouched. A Solo Self-Found character's pile goes into its own balance and leaves the account's untouched. With
 * nobody playing nothing is banked and the pile stays. A pile comes without a click within 15 m and is named at any
 * distance, as a material is. Prints "BANK".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGoldBankTest,
	"Cataclysm.Gold.PickingUpAPileAddsItToTheBalanceAndTakesItOffTheFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGoldBankTest::RunTest(const FString&)
{
	using namespace CataclysmGoldTest;

	FGoldScene Scene = MakeScene();
	FGoldScene Solo = MakeScene(ECataclysmLethality::Standard, /*bSoloSelfFound=*/true);
	ON_SCOPE_EXIT
	{
		for (UWorld* World : { Scene.World, Solo.World })
		{
			if (World)
			{
				World->DestroyWorld(/*bInformEngineOfWorld=*/false);
			}
		}
	};
	if (!TestTrue(TEXT("set-up: the two scenes were built"), Scene.IsUsable() && Solo.IsUsable()))
	{
		return false;
	}

	ACataclysmDroppedItem* ThirtySeven = UCataclysmDropSpawner::PutOnTheFloor(
		Scene.World, FVector(100.0f, 0.0f, 0.0f), FCataclysmItem(), NAME_None, 0, 37);
	ACataclysmDroppedItem* Five = UCataclysmDropSpawner::PutOnTheFloor(
		Scene.World, FVector(200.0f, 0.0f, 0.0f), FCataclysmItem(), NAME_None, 0, 5);
	ACataclysmDroppedItem* ByHand = UCataclysmDropSpawner::PutOnTheFloor(
		Scene.World, FVector(250.0f, 0.0f, 0.0f), FCataclysmItem(), NAME_None, 0, 1000);
	ACataclysmDroppedItem* Own = UCataclysmDropSpawner::PutOnTheFloor(
		Solo.World, FVector(100.0f, 0.0f, 0.0f), FCataclysmItem(), NAME_None, 0, 37);
	UWorld* Unplayed = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT
	{
		if (Unplayed)
		{
			Unplayed->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	ACataclysmDroppedItem* Unbanked = Unplayed ? UCataclysmDropSpawner::PutOnTheFloor(
		Unplayed, FVector(100.0f, 0.0f, 0.0f), FCataclysmItem(), NAME_None, 0, 37) : nullptr;
	UCataclysmInventoryComponent* Bag = Scene.Character->GetInventory();
	if (!TestTrue(TEXT("set-up: five piles and a bag"), ThirtySeven && Five && ByHand && Own && Unbanked && Bag))
	{
		return false;
	}
	FCataclysmCharacterPartition SolosMode = Solo.Partition;
	SolosMode.bSoloSelfFound = false;

	const int64 Before = Balance(Scene);
	const FString NameBefore = ThirtySeven->DisplayName;
	const int64 Banked = UCataclysmDropPickup::BankGold(Scene.World, { ThirtySeven, Five });
	const int64 AfterTwo = Balance(Scene);
	const bool bTwoGone = !IsValid(ThirtySeven) && !IsValid(Five);
	const int64 Walk = Scene.Instance->GoldPickedUpThisWalk;
	const int32 BagBefore = Bag->NumFreeSlots();
	const bool bTaken = UCataclysmDropPickup::TakeInto(Bag, ByHand, /*bByHand=*/true);
	const int64 AfterHand = Balance(Scene);
	const int32 BagAfter = Bag->NumFreeSlots();

	const int64 OrdinarysOwn = Scene.State->GetOwnGold();

	const int64 SoloBefore = Held(Solo);
	const int64 SoloBanked = UCataclysmDropPickup::BankGold(Solo.World, { Own });
	const int64 SoloAfter = Held(Solo);
	const bool bOwnGone = !IsValid(Own);
	const UCataclysmAccountSave* ModesAccount = Solo.Instance->AccountFor(SolosMode);
	const int64 ModesGold = ModesAccount != nullptr ? ModesAccount->Gold : -1;

	const int64 NobodyBanked = UCataclysmDropPickup::BankGold(Unplayed, { Unbanked });
	const bool bUnbankedStays = IsValid(Unbanked);

	const bool bComesNear = UCataclysmDropPickup::ComesAutomatically(true, FVector::ZeroVector, FVector(1400.0f, 0.0f, 0.0f));
	const bool bComesFar = UCataclysmDropPickup::ComesAutomatically(true, FVector::ZeroVector, FVector(1600.0f, 0.0f, 0.0f));
	ACataclysmDroppedItem* Distant = UCataclysmDropSpawner::PutOnTheFloor(
		Scene.World, FVector(5000.0f, 0.0f, 0.0f), FCataclysmItem(), NAME_None, 0, 9);
	TArray<ACataclysmDroppedItem*> Named;
	UCataclysmDropPickup::DropsToName(Scene.World, FVector::ZeroVector, Named);

	AddInfo(FString::Printf(TEXT("BANK: '%s' and 5 banked %lld, the balance %lld -> %lld, both gone %s, the walk %lld; by "
								 "hand %s, the balance %lld, the bag's free slots %d -> %d, its own gold %lld; Solo Self-Found "
								 "banked %lld, its own %lld -> %lld, the pile gone %s, its mode's account %lld; nobody playing "
								 "banked %lld, the pile stays %s; comes at 14 m %s, at 16 m %s; a pile 50 m off named %s"),
							*NameBefore, Banked, Before, AfterTwo, bTwoGone ? TEXT("yes") : TEXT("NO"), Walk,
							bTaken ? TEXT("taken") : TEXT("REFUSED"), AfterHand, BagBefore, BagAfter, OrdinarysOwn,
							SoloBanked, SoloBefore, SoloAfter, bOwnGone ? TEXT("yes") : TEXT("NO"), ModesGold, NobodyBanked,
							bUnbankedStays ? TEXT("yes") : TEXT("NO"), bComesNear ? TEXT("yes") : TEXT("NO"),
							bComesFar ? TEXT("YES") : TEXT("no"), Named.Contains(Distant) ? TEXT("yes") : TEXT("NO")));

	TestEqual(TEXT("CONTROL: the test's balance starts at nothing"), Before, static_cast<int64>(0));
	TestEqual(TEXT("CONTROL: a pile of 37 is named '37 gold'"), NameBefore, FString(TEXT("37 gold")));
	TestEqual(TEXT("banking a pile of 37 and one of 5 banks 42"), Banked, static_cast<int64>(42));
	TestEqual(TEXT("and the balance holds 42"), AfterTwo, static_cast<int64>(42));
	TestTrue(TEXT("and both piles are off the floor"), bTwoGone);
	TestEqual(TEXT("and the walk has picked up 42"), Walk, static_cast<int64>(42));
	TestTrue(TEXT("a pile clicked by hand is taken"), bTaken);
	TestEqual(TEXT("into the balance"), AfterHand, static_cast<int64>(1042));
	TestEqual(TEXT("and not into the bag"), BagAfter, BagBefore);
	TestEqual(TEXT("and the character's own balance is untouched"), OrdinarysOwn, static_cast<int64>(0));
	TestEqual(TEXT("CONTROL: a Solo Self-Found character starts with none"), SoloBefore, static_cast<int64>(0));
	TestEqual(TEXT("a Solo Self-Found character banks its pile of 37"), SoloBanked, static_cast<int64>(37));
	TestEqual(TEXT("into its own balance"), SoloAfter, static_cast<int64>(37));
	TestEqual(TEXT("which is the player state's"), Solo.State->GetOwnGold(), static_cast<int64>(37));
	TestTrue(TEXT("and the pile is off the floor"), bOwnGone);
	TestEqual(TEXT("and its lethality mode's account is untouched"), ModesGold, static_cast<int64>(0));
	TestEqual(TEXT("with nobody playing nothing is banked"), NobodyBanked, static_cast<int64>(0));
	TestTrue(TEXT("and the pile stays on the floor"), bUnbankedStays);
	TestTrue(TEXT("gold comes without a click within 15 m"), bComesNear);
	TestFalse(TEXT("and not from beyond"), bComesFar);
	TestTrue(TEXT("and is named at 50 m, past the 10 m gear is named within"), Named.Contains(Distant));
	return true;
}

// ---------------------------------------------------------------------------
// The balance and the piles are saved
// ---------------------------------------------------------------------------

/**
 * The balance survives a save: banking raises `AccountChanged`, the writer writes the account slot, and a new game
 * instance reads the same gold. And it is the lethality mode's own: a Hardcore account in the same session holds none
 * of it. Prints "ROUND TRIP".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGoldRoundTripTest,
	"Cataclysm.Gold.TheBalanceSurvivesASaveAndIsTheLethalityModesOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGoldRoundTripTest::RunTest(const FString&)
{
	using namespace CataclysmGoldTest;

	FGoldScene Scene = MakeScene(ECataclysmLethality::Standard, /*bSoloSelfFound=*/false, /*bWriting=*/true);
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

	FCataclysmCharacterPartition Hardcore = Scene.Partition;
	Hardcore.Lethality = ECataclysmLethality::Hardcore;
	const FString Slot = UCataclysmGameInstance::AccountSlotFor(Scene.Partition);
	const FString HardcoreSlot = UCataclysmGameInstance::AccountSlotFor(Hardcore);
	DeleteSlot(HardcoreSlot);

	ACataclysmDroppedItem* Pile = UCataclysmDropSpawner::PutOnTheFloor(
		Scene.World, FVector(100.0f, 0.0f, 0.0f), FCataclysmItem(), NAME_None, 0, 250);
	const int32 WritesBefore = Writer->WritesStarted();
	const int64 Banked = UCataclysmDropPickup::BankGold(Scene.World, { Pile });
	const int32 Writes = Writer->WritesStarted() - WritesBefore;
	WaitForTheSlot(Slot);

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::SlotIsEmpty;
	FString Message;
	const UCataclysmAccountSave* Read = Cast<UCataclysmAccountSave>(FCataclysmSaveStorage::ReadFromSlot(
		Slot, UCataclysmSaveWriter::UserIndex, UCataclysmAccountSave::StaticClass(), GetTransientPackage(), Result,
		Message));
	UCataclysmGameInstance* NextSession = NewObject<UCataclysmGameInstance>();
	const UCataclysmAccountSave* Reread = NextSession->AccountFor(Scene.Partition);
	const UCataclysmAccountSave* OtherMode = NextSession->AccountFor(Hardcore);

	AddInfo(FString::Printf(TEXT("ROUND TRIP: banked %lld with %d write(s) started; '%s' read back %s (%s) holding %lld; "
								 "a new game instance reads %lld; the Hardcore account '%s' holds %lld"),
							Banked, Writes, *Slot, Read != nullptr ? TEXT("a record") : TEXT("nothing"),
							FCataclysmSaveStorage::Describe(Result), Read != nullptr ? Read->Gold : -1,
							Reread != nullptr ? Reread->Gold : -1, *HardcoreSlot,
							OtherMode != nullptr ? OtherMode->Gold : -1));

	TestEqual(TEXT("CONTROL: the slot is the test's own, not the player's"),
			  Slot, UCataclysmSavePartition::AccountSlotName(Scene.Partition) + TEXT("_AutomationTest"));
	TestEqual(TEXT("set-up: 250 banked"), Banked, static_cast<int64>(250));
	TestTrue(TEXT("the banking started writes: the account record's, and the run and character records'"), Writes >= 2);
	if (!TestNotNull(TEXT("the account record reads back from its slot"), Read))
	{
		return false;
	}
	TestEqual(TEXT("at the current version"), Read->SchemaVersion, UCataclysmAccountSave::SchemaVersionNow);
	TestEqual(TEXT("holding the 250 gold"), Read->Gold, static_cast<int64>(250));
	TestTrue(TEXT("and a new game instance reads the same balance"), Reread != nullptr && Reread->Gold == 250);
	TestNotEqual(TEXT("CONTROL: the Hardcore account is another slot"), HardcoreSlot, Slot);
	TestTrue(TEXT("and holds none of the Standard gold"), OtherMode != nullptr && OtherMode->Gold == 0);
	return true;
}

/**
 * A Solo Self-Found character's own gold survives its character record: the gather writes it, the JSON round trip keeps
 * it, and the restore puts it back on the player state. An ordinary character's record holds a gold of 0, its gold
 * being its lethality mode's. Prints "OWN GOLD".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGoldOwnRecordTest,
	"Cataclysm.Gold.ASoloSelfFoundCharactersGoldSurvivesItsCharacterRecord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGoldOwnRecordTest::RunTest(const FString&)
{
	using namespace CataclysmGoldTest;

	FGoldScene Solo = MakeScene(ECataclysmLethality::Hardcore, /*bSoloSelfFound=*/true);
	FGoldScene Ordinary = MakeScene(ECataclysmLethality::Hardcore);
	ON_SCOPE_EXIT
	{
		for (UWorld* World : { Solo.World, Ordinary.World })
		{
			if (World)
			{
				World->DestroyWorld(/*bInformEngineOfWorld=*/false);
			}
		}
	};
	if (!TestTrue(TEXT("set-up: the two scenes were built"), Solo.IsUsable() && Ordinary.IsUsable()))
	{
		return false;
	}

	ACataclysmDroppedItem* SoloPile = UCataclysmDropSpawner::PutOnTheFloor(
		Solo.World, FVector(100.0f, 0.0f, 0.0f), FCataclysmItem(), NAME_None, 0, 250);
	ACataclysmDroppedItem* OrdinaryPile = UCataclysmDropSpawner::PutOnTheFloor(
		Ordinary.World, FVector(100.0f, 0.0f, 0.0f), FCataclysmItem(), NAME_None, 0, 90);
	const int64 SoloBanked = UCataclysmDropPickup::BankGold(Solo.World, { SoloPile });
	const int64 OrdinaryBanked = UCataclysmDropPickup::BankGold(Ordinary.World, { OrdinaryPile });

	UCataclysmCharacterSave* SoloRecord = NewObject<UCataclysmCharacterSave>();
	UCataclysmCharacterSave* OrdinaryRecord = NewObject<UCataclysmCharacterSave>();
	const bool bGathered = FCataclysmSaveGather::CharacterFrom(*Solo.Character, *SoloRecord)
		&& FCataclysmSaveGather::CharacterFrom(*Ordinary.Character, *OrdinaryRecord);
	SoloRecord->SchemaVersion = UCataclysmCharacterSave::SchemaVersionNow;

	FString Json;
	FString Error;
	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	const bool bWritten = FCataclysmSaveStorage::ToJson(SoloRecord, Json, Error);
	const UCataclysmCharacterSave* ReadBack = bWritten ? Cast<UCataclysmCharacterSave>(FCataclysmSaveStorage::FromJson(
		Json, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(), Result, Message)) : nullptr;

	Solo.State->SetOwnGold(0);
	int32 SkillsRefused = 0;
	FString Why;
	const bool bRestored = ReadBack != nullptr
		&& FCataclysmSaveApply::CharacterInto(*ReadBack, *Solo.Character, SkillsRefused, Why);

	AddInfo(FString::Printf(TEXT("OWN GOLD: banked %lld and %lld; gathered %s, the Solo Self-Found record %lld, the "
								 "ordinary record %lld; written %s, read back %lld (%s); restored %s (%s), the state %lld"),
							SoloBanked, OrdinaryBanked, bGathered ? TEXT("yes") : TEXT("NO"), SoloRecord->Gold,
							OrdinaryRecord->Gold, bWritten ? TEXT("yes") : *Error, ReadBack ? ReadBack->Gold : -1,
							FCataclysmSaveStorage::Describe(Result), bRestored ? TEXT("yes") : TEXT("NO"), *Why,
							Solo.State->GetOwnGold()));

	TestEqual(TEXT("set-up: a pile of 250 banked into the Solo Self-Found character's own"), SoloBanked,
			  static_cast<int64>(250));
	TestEqual(TEXT("set-up: and one of 90 into the ordinary character's mode"), OrdinaryBanked, static_cast<int64>(90));
	TestTrue(TEXT("both records are gathered"), bGathered);
	TestEqual(TEXT("the Solo Self-Found record holds its 250"), SoloRecord->Gold, static_cast<int64>(250));
	TestEqual(TEXT("the ordinary record holds none: its gold is its mode's"), OrdinaryRecord->Gold, static_cast<int64>(0));
	TestTrue(TEXT("the record is written out"), bWritten);
	TestTrue(TEXT("and reads back holding 250"), ReadBack != nullptr && ReadBack->Gold == 250);
	TestTrue(TEXT("the restore succeeds"), bRestored);
	TestEqual(TEXT("and puts the 250 back on the player state"), Solo.State->GetOwnGold(), static_cast<int64>(250));
	return true;
}

/**
 * `Migrate_7_to_8` gives a character from before gold a balance of its own of 0: `Character_v7.json` through the chain
 * arrives at the current version with every other field kept. Prints "CHARACTER MIGRATED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGoldCharacterMigrationTest,
	"Cataclysm.Gold.ACharacterFileFromBeforeGoldMigratesHoldingNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGoldCharacterMigrationTest::RunTest(const FString&)
{
	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Character_v7.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}
	const TSharedPtr<FJsonObject> Stepped = CataclysmSaveFixtures::Parse(Text);
	if (!TestTrue(TEXT("set-up: Character_v7.json is a JSON object with no gold"),
				  Stepped.IsValid() && !Stepped->HasField(TEXT("Gold"))))
	{
		return false;
	}

	FString StepMessage;
	const ECataclysmSaveMigrationResult Stepping = FCataclysmSaveMigration::Migrate(
		GetDefault<UCataclysmCharacterSave>()->MigrationSteps(), UCataclysmCharacterSave::SchemaVersionNow,
		Stepped.ToSharedRef(), StepMessage);
	double SteppedGold = -1.0;
	const bool bWritten = Stepped->TryGetNumberField(TEXT("Gold"), SteppedGold);

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	UCataclysmCharacterSave* Read = Cast<UCataclysmCharacterSave>(FCataclysmSaveStorage::FromJson(
		Text, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(), Result, Message));
	if (Read == nullptr)
	{
		AddError(FString::Printf(TEXT("Character_v7.json would not load: %s -- %s"),
								 FCataclysmSaveStorage::Describe(Result), *Message));
		return false;
	}

	const TSharedPtr<FJsonObject> Expected = CataclysmSaveFixtures::Parse(Text);
	Expected->SetNumberField(TEXT("SchemaVersion"), UCataclysmCharacterSave::SchemaVersionNow);
	Expected->SetNumberField(TEXT("Gold"), 0);
	CataclysmSaveFixtures::AddTheCharactersVersion9Defaults(*Expected); // And 9, issue #2741 slice 1c: its own tree.
	CataclysmSaveFixtures::AddTheCharactersVersion10Defaults(*Expected); // And 10, issue #2764: no skill experience.
	CataclysmSaveFixtures::AddTheCharactersVersion11Defaults(*Expected); // And 11, #2741 slice 6e: no debt.
	CataclysmSaveFixtures::AddTheCharactersVersion12Defaults(*Expected); // And 12, #2741 slice 8: no extra option.
	CataclysmSaveFixtures::AddTheCharactersVersion13Defaults(*Expected); // And 13, #46 G2 and G3: socket lists, no gem.
	CataclysmSaveFixtures::AddTheCharactersVersion14Defaults(*Expected); // And issue #2741 slice 9's step.
	CataclysmSaveFixtures::AddTheCharactersVersion15Defaults(*Expected); // And issue #2848's step: where each item arrived.
	CataclysmSaveFixtures::AddTheCharactersVersion16Defaults(*Expected); // And 16, issue #2849: stacks to their tabs.
	CataclysmSaveFixtures::AddTheCharactersVersion17Defaults(*Expected); // And 17, issue #837 slice 1: no remembered defaults.
	FString Rewritten;
	FString Error;
	const bool bWroteBack = FCataclysmSaveStorage::ToJson(Read, Rewritten, Error);
	FString Where;
	const bool bSame = bWroteBack
		&& CataclysmSaveFixtures::SameObject(Expected, CataclysmSaveFixtures::Parse(Rewritten), Where);

	AddInfo(FString::Printf(TEXT("CHARACTER MIGRATED: the step %d (%s), wrote the gold %s as %g; the record at version %d "
								 "holding %lld; the rest %s"),
							static_cast<int32>(Stepping), *StepMessage, bWritten ? TEXT("yes") : TEXT("NO"),
							SteppedGold, Read->SchemaVersion, Read->Gold, bSame ? TEXT("kept") : *Where));

	TestEqual(TEXT("the chain migrates the version 7 file"), static_cast<int32>(Stepping),
			  static_cast<int32>(ECataclysmSaveMigrationResult::Migrated));
	TestTrue(TEXT("the step writes a gold of 0"), bWritten && SteppedGold == 0.0);
	TestEqual(TEXT("it arrives at the current version"), Read->SchemaVersion, UCataclysmCharacterSave::SchemaVersionNow);
	TestEqual(TEXT("holding no gold of its own"), Read->Gold, static_cast<int64>(0));
	TestTrue(TEXT("and every other field is kept"), bSame);
	return true;
}

/**
 * A gold pile lying on the floor is saved with its amount and restored as a gold pile, named again on the way in. A
 * piece of gear beside it is saved with a gold of 0. Prints "GROUND".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGoldGroundTest,
	"Cataclysm.Gold.APileOnTheFloorIsSavedAndRestoredWithItsGold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGoldGroundTest::RunTest(const FString&)
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDroppedItem* Pile = UCataclysmDropSpawner::PutOnTheFloor(
		World, FVector(300.0f, 100.0f, 0.0f), FCataclysmItem(), NAME_None, 0, 1234);
	if (!TestNotNull(TEXT("set-up: a pile"), Pile))
	{
		return false;
	}
	const FCataclysmSavedGroundItem Saved = FCataclysmSaveGather::GroundItemFrom(*Pile);
	Pile->Destroy();
	const ACataclysmDroppedItem* Restored = FCataclysmSaveApply::GroundItemInto(*World, Saved);

	AddInfo(FString::Printf(TEXT("GROUND: saved %d gold at (%g, %g); restored %s, %d gold, named '%s', a material %s"),
							Saved.Gold, Saved.Location.X, Saved.Location.Y, Restored ? TEXT("a drop") : TEXT("NOTHING"),
							Restored ? Restored->Gold : -1, Restored ? *Restored->DisplayName : TEXT(""),
							Restored && Restored->IsMaterial() ? TEXT("YES") : TEXT("no")));

	TestEqual(TEXT("the record holds the pile's 1234 gold"), Saved.Gold, 1234);
	TestFalse(TEXT("and no material"), Saved.IsMaterial());
	if (!TestNotNull(TEXT("the pile comes back"), Restored))
	{
		return false;
	}
	TestTrue(TEXT("as a gold pile"), Restored->IsGold());
	TestEqual(TEXT("holding 1234"), Restored->Gold, 1234);
	TestEqual(TEXT("named again on the way in, with a comma"), Restored->DisplayName, FString(TEXT("1,234 gold")));
	TestFalse(TEXT("and not a material"), Restored->IsMaterial());
	return true;
}

/**
 * `Migrate_6_to_7` writes a gold of 0 on every ground item of a run file from before gold: `Run_v6.json` through the
 * chain arrives at the current version with every other field kept. Prints "RUN MIGRATED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGoldRunMigrationTest,
	"Cataclysm.Gold.ARunFileFromBeforeGoldMigratesWithNoGoldOnItsFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGoldRunMigrationTest::RunTest(const FString&)
{
	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Run_v6.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}
	const TSharedPtr<FJsonObject> Stepped = CataclysmSaveFixtures::Parse(Text);
	const TSharedPtr<FJsonObject>* Floor = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (!TestTrue(TEXT("set-up: Run_v6.json is a JSON object with ground items"),
				  Stepped.IsValid() && Stepped->TryGetObjectField(TEXT("Floor"), Floor) && Floor
					  && (*Floor)->TryGetArrayField(TEXT("GroundItems"), Items) && Items && Items->Num() > 0))
	{
		return false;
	}

	FString StepMessage;
	const ECataclysmSaveMigrationResult Stepping = FCataclysmSaveMigration::Migrate(
		GetDefault<UCataclysmRunSave>()->MigrationSteps(), UCataclysmRunSave::SchemaVersionNow, Stepped.ToSharedRef(),
		StepMessage);
	int32 Zeroes = 0;
	for (const TSharedPtr<FJsonValue>& Entry : *Items)
	{
		const TSharedPtr<FJsonObject>* Item = nullptr;
		double Gold = -1.0;
		Zeroes += Entry->TryGetObject(Item) && Item && (*Item)->TryGetNumberField(TEXT("Gold"), Gold) && Gold == 0.0
			? 1 : 0;
	}

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	UCataclysmRunSave* Read = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
		Text, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, Message));
	if (Read == nullptr)
	{
		AddError(FString::Printf(TEXT("Run_v6.json would not load: %s -- %s"), FCataclysmSaveStorage::Describe(Result),
								 *Message));
		return false;
	}

	const TSharedPtr<FJsonObject> Expected = CataclysmSaveFixtures::Parse(Text);
	Expected->SetNumberField(TEXT("SchemaVersion"), UCataclysmRunSave::SchemaVersionNow);
	CataclysmSaveFixtures::AddTheRunsVersion7Defaults(*Expected);
	// AND THE WALK SEED MASK OF 0 `Migrate_7_to_8` WRITES, because the chain runs on to version 8 since 2026-11-18
	// (issue #2749, B10).
	CataclysmSaveFixtures::AddTheRunsVersion8Defaults(*Expected);
	// AND THE TREE'S EVENT COUNTS AT NOTHING AND A BOUGHT DAY OF 0 ON EVERY UPGRADE, which `Migrate_8_to_9` writes,
	// because the chain runs on to version 9 since 2026-11-19 (issue #2741 slice 3b).
	CataclysmSaveFixtures::AddTheRunsVersion9Defaults(*Expected);
	// AND A `bIsVengefulWraith` OF FALSE ON EVERY SAVED CREATURE, which `Migrate_9_to_10` writes, because
	// the chain runs on to run record version 10 since 2026-11-19 (issue #2725).
	CataclysmSaveFixtures::AddTheRunsVersion10Defaults(*Expected);
	// AND THE CLEARS SINCE THE LAST DEATH `Migrate_10_to_11` WRITES, because the chain runs on to run record
	// version 11 since 2026-11-20 (issue #2741 slice 5b).
	CataclysmSaveFixtures::AddTheRunsVersion11Defaults(*Expected);
	CataclysmSaveFixtures::AddTheRunsVersion12Defaults(*Expected);
	CataclysmSaveFixtures::AddTheRunsVersion13Defaults(*Expected);
	CataclysmSaveFixtures::AddTheRunsVersion14Defaults(*Expected); // And 14, #46 G3 and G5: socket lists, no gem on the floor.
	CataclysmSaveFixtures::AddTheRunsVersion15Defaults(*Expected); // And issue #2741 slice 9's step.
	CataclysmSaveFixtures::AddTheRunsVersion16Defaults(*Expected); // And issue #2838's step.
	CataclysmSaveFixtures::AddTheRunsVersion17Defaults(*Expected); // And issue #2840's step.
	CataclysmSaveFixtures::AddTheRunsVersion18Defaults(*Expected); // And issue #2848's step: where each item arrived.
	FString Rewritten;
	FString Error;
	const bool bWroteBack = FCataclysmSaveStorage::ToJson(Read, Rewritten, Error);
	FString Where;
	const bool bSame = bWroteBack
		&& CataclysmSaveFixtures::SameObject(Expected, CataclysmSaveFixtures::Parse(Rewritten), Where);

	AddInfo(FString::Printf(TEXT("RUN MIGRATED: the step %d (%s); a gold of 0 on %d of %d ground items; the record at "
								 "version %d with %d ground items; the rest %s"),
							static_cast<int32>(Stepping), *StepMessage, Zeroes, Items->Num(), Read->SchemaVersion,
							Read->Floor.GroundItems.Num(), bSame ? TEXT("kept") : *Where));

	TestEqual(TEXT("the chain migrates the version 6 file"), static_cast<int32>(Stepping),
			  static_cast<int32>(ECataclysmSaveMigrationResult::Migrated));
	TestEqual(TEXT("the step writes a gold of 0 on every ground item"), Zeroes, Items->Num());
	TestEqual(TEXT("it arrives at the current version"), Read->SchemaVersion, UCataclysmRunSave::SchemaVersionNow);
	TestTrue(TEXT("and every other field is kept"), bSame);
	return true;
}

/**
 * `Migrate_2_to_3` writes a balance of 0 into an account written at 2, keeps a number already there, and refuses one
 * that is not a number: run on the committed version 3 file put back to version 2, without its gold, with a number and
 * with a word. And `Account_v2.json`, the file from before gold, reads through the whole chain holding 0 with every
 * other field kept. Prints "ACCOUNT MIGRATED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGoldAccountMigrationTest,
	"Cataclysm.Gold.AnAccountFromBeforeGoldMigratesHoldingNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGoldAccountMigrationTest::RunTest(const FString&)
{
	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Account_v3.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}

	// THREE COPIES AT VERSION 2: with no gold, with a number, and with a word where the number goes.
	const TSharedPtr<FJsonObject> NoGold = CataclysmSaveFixtures::Parse(Text);
	const TSharedPtr<FJsonObject> Held = CataclysmSaveFixtures::Parse(Text);
	const TSharedPtr<FJsonObject> Word = CataclysmSaveFixtures::Parse(Text);
	if (!TestTrue(TEXT("set-up: Account_v3.json is a JSON object"), NoGold.IsValid() && Held.IsValid() && Word.IsValid()))
	{
		return false;
	}
	for (const TSharedPtr<FJsonObject>& Copy : { NoGold, Held, Word })
	{
		Copy->SetNumberField(TEXT("SchemaVersion"), 2);
	}
	NoGold->RemoveField(TEXT("Gold"));
	Held->SetNumberField(TEXT("Gold"), 77);
	Word->SetStringField(TEXT("Gold"), TEXT("plenty"));

	const TArrayView<const FCataclysmSaveMigrationStep> Steps = GetDefault<UCataclysmAccountSave>()->MigrationSteps();
	FString NoGoldMessage;
	FString HeldMessage;
	FString WordMessage;
	const ECataclysmSaveMigrationResult NoGoldResult = FCataclysmSaveMigration::Migrate(
		Steps, UCataclysmAccountSave::SchemaVersionNow, NoGold.ToSharedRef(), NoGoldMessage);
	const ECataclysmSaveMigrationResult HeldResult = FCataclysmSaveMigration::Migrate(
		Steps, UCataclysmAccountSave::SchemaVersionNow, Held.ToSharedRef(), HeldMessage);
	const ECataclysmSaveMigrationResult WordResult = FCataclysmSaveMigration::Migrate(
		Steps, UCataclysmAccountSave::SchemaVersionNow, Word.ToSharedRef(), WordMessage);
	double NoGoldHeld = -1.0;
	double HeldGold = -1.0;
	NoGold->TryGetNumberField(TEXT("Gold"), NoGoldHeld);
	Held->TryGetNumberField(TEXT("Gold"), HeldGold);

	AddInfo(FString::Printf(TEXT("ACCOUNT MIGRATED: no gold %d (%s) -> %g; 77 %d (%s) -> %g; a word %d (%s)"),
							static_cast<int32>(NoGoldResult), *NoGoldMessage, NoGoldHeld, static_cast<int32>(HeldResult),
							*HeldMessage, HeldGold, static_cast<int32>(WordResult), *WordMessage));

	TestEqual(TEXT("an account at 2 with no gold migrates"), static_cast<int32>(NoGoldResult),
			  static_cast<int32>(ECataclysmSaveMigrationResult::Migrated));
	TestEqual(TEXT("holding a balance of 0, written out"), NoGoldHeld, 0.0);
	TestEqual(TEXT("one already holding 77 keeps it"), HeldGold, 77.0);
	TestNotEqual(TEXT("and one holding a word is refused"), static_cast<int32>(WordResult),
				 static_cast<int32>(ECataclysmSaveMigrationResult::Migrated));

	// THE FILE FROM BEFORE GOLD, through the whole chain, compared with what the record writes back.
	FString Before;
	if (!CataclysmSaveFixtures::Read(TEXT("Account_v2.json"), Before, Reason))
	{
		AddError(Reason);
		return false;
	}
	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	const UCataclysmAccountSave* Read = Cast<UCataclysmAccountSave>(FCataclysmSaveStorage::FromJson(
		Before, UCataclysmAccountSave::StaticClass(), GetTransientPackage(), Result, Message));
	if (Read == nullptr)
	{
		AddError(FString::Printf(TEXT("Account_v2.json would not load: %s -- %s"), FCataclysmSaveStorage::Describe(Result),
								 *Message));
		return false;
	}
	const TSharedPtr<FJsonObject> Expected = CataclysmSaveFixtures::Parse(Before);
	Expected->SetNumberField(TEXT("SchemaVersion"), UCataclysmAccountSave::SchemaVersionNow);
	Expected->SetNumberField(TEXT("Gold"), 0);
	// AND THE EMPTY TREE `Migrate_3_to_4` WRITES: the chain runs on to version 4 since 2026-11-18 (issue #2741).
	CataclysmSaveFixtures::AddTheAccountsVersion4Defaults(*Expected);
	CataclysmSaveFixtures::AddTheAccountsVersion5Defaults(*Expected); // And 5, #2741 slice 6e: no debt.
	CataclysmSaveFixtures::AddTheAccountsVersion6Defaults(*Expected); // And 6, #2741 slice 8: no extra option.
	CataclysmSaveFixtures::AddTheAccountsVersion7Defaults(*Expected); // And issue #2741 slice 9's step.
	FString Rewritten;
	FString Error;
	const bool bWroteBack = FCataclysmSaveStorage::ToJson(Read, Rewritten, Error);
	FString Where;
	const bool bSame = bWroteBack
		&& CataclysmSaveFixtures::SameObject(Expected, CataclysmSaveFixtures::Parse(Rewritten), Where);
	AddInfo(FString::Printf(TEXT("ACCOUNT MIGRATED: Account_v2.json %s, at version %d holding %lld; the rest %s"),
							FCataclysmSaveStorage::Describe(Result), Read->SchemaVersion, Read->Gold,
							bSame ? TEXT("kept") : *Where));
	TestEqual(TEXT("Account_v2.json is migrated"), static_cast<int32>(Result),
			  static_cast<int32>(ECataclysmSaveLoadResult::Migrated));
	TestEqual(TEXT("to the current version"), Read->SchemaVersion, UCataclysmAccountSave::SchemaVersionNow);
	TestEqual(TEXT("holding no gold"), Read->Gold, static_cast<int64>(0));
	TestTrue(TEXT("keeping its 37.625 empire points"), Read->EmpireUpgradePoints == 37.625);
	TestTrue(TEXT("and every other field"), bSame);
	return true;
}

/**
 * The committed files hold their gold: `Account_v7.json` reads 1234567 into the balance and is read rather than
 * migrated, `Character_v17.json` reads 4321 into the character's own, and `Run_v18.json` restores a pile of 37 beside its
 * piece of gear. Prints "FILES". The account file was `Account_v3.json` until the account record went to version 4 on
 * 2026-11-18 (issue #2741 slice 1b), `Account_v4.json` until it went to version 5 on 2026-11-20 (issue #2741 slice
 * 6e), `Account_v5.json` until it went to version 6 on 2026-11-21 (issue #2741 slice 8), and `Account_v6.json` until it
 * went to version 7 on 2026-11-22 (issue #2741 slice 9); each holds the same gold. The character file was `Character_v8.json` until the character record went to version 9 the same day (issue
 * #2741 slice 1c); `Character_v9.json`, since 2026-11-19 `Character_v10.json` (issue #2764), and
 * from 2026-11-20 `Character_v11.json` (issue #2741 slice 6e), from 2026-11-21 `Character_v12.json` (issue #2741
 * slice 8), from 2026-11-22 `Character_v13.json` (issue #46), from
 * the same day `Character_v14.json` (issue #2741 slice 9), from 2026-11-24 `Character_v15.json` (issue #2848), and
 * from the same day `Character_v16.json` (issue #2849), and since the same day `Character_v17.json` (issue #837 slice
 * 1), hold the same.
 * The run file was `Run_v7.json` until the run record went to version 8 the same day (issue #2749, B10), and
 * `Run_v8.json` until it went to version 9 on 2026-11-19 (issue #2741 slice 3b), and `Run_v9.json` until it went to
 * version 10 the same day (issue #2725), and `Run_v10.json` until it went to version 11 on 2026-11-20 (issue #2741
 * slice 5b), and `Run_v11.json` until it went to version 12 the same day (issue #2741 slice 6b), and `Run_v12.json` until it
 * went to version 13 on 2026-11-21 (issue #2825), and `Run_v13.json` until it went to version 14 on 2026-11-22 (issue
 * #46), and `Run_v14.json` until it went to version 15 the same day (issue #2741 slice 9), and `Run_v15.json` until it
 * went to version 16 the same day (issue #2838), and `Run_v16.json` until it went to version 17 the same day (issue
 * #2840), each with the same pile.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGoldCommittedFilesTest,
	"Cataclysm.Gold.TheCommittedFilesReadIntoTheGoldTheyState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGoldCommittedFilesTest::RunTest(const FString&)
{
	FString AccountText;
	FString RunText;
	FString CharacterText;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Account_v7.json"), AccountText, Reason)
		|| !CataclysmSaveFixtures::Read(TEXT("Run_v18.json"), RunText, Reason)
		|| !CataclysmSaveFixtures::Read(TEXT("Character_v17.json"), CharacterText, Reason))
	{
		AddError(Reason);
		return false;
	}

	ECataclysmSaveLoadResult AccountResult = ECataclysmSaveLoadResult::NotValidJson;
	ECataclysmSaveLoadResult RunResult = ECataclysmSaveLoadResult::NotValidJson;
	ECataclysmSaveLoadResult CharacterResult = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	const UCataclysmAccountSave* Account = Cast<UCataclysmAccountSave>(FCataclysmSaveStorage::FromJson(
		AccountText, UCataclysmAccountSave::StaticClass(), GetTransientPackage(), AccountResult, Message));
	const UCataclysmRunSave* Run = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
		RunText, UCataclysmRunSave::StaticClass(), GetTransientPackage(), RunResult, Message));
	const UCataclysmCharacterSave* Character = Cast<UCataclysmCharacterSave>(FCataclysmSaveStorage::FromJson(
		CharacterText, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(), CharacterResult, Message));
	if (!TestNotNull(TEXT("Account_v7.json loads"), Account) || !TestNotNull(TEXT("Run_v18.json loads"), Run)
		|| !TestNotNull(TEXT("Character_v17.json loads"), Character))
	{
		return false;
	}

	TArray<int32> Golds;
	for (const FCataclysmSavedGroundItem& Item : Run->Floor.GroundItems)
	{
		Golds.Add(Item.Gold);
	}
	AddInfo(FString::Printf(TEXT("FILES: the account %s, %lld gold; the character %s, %lld gold; the run %s, ground "
								 "items holding gold %s"),
							FCataclysmSaveStorage::Describe(AccountResult), Account->Gold,
							FCataclysmSaveStorage::Describe(CharacterResult), Character->Gold,
							FCataclysmSaveStorage::Describe(RunResult),
							*FString::JoinBy(Golds, TEXT(", "), [](int32 Gold) { return FString::FromInt(Gold); })));

	TestEqual(TEXT("the account file is read rather than migrated"), static_cast<int32>(AccountResult),
			  static_cast<int32>(ECataclysmSaveLoadResult::Loaded));
	TestEqual(TEXT("holding 1234567 gold"), Account->Gold, static_cast<int64>(1234567));
	TestEqual(TEXT("the character file is read rather than migrated"), static_cast<int32>(CharacterResult),
			  static_cast<int32>(ECataclysmSaveLoadResult::Loaded));
	TestEqual(TEXT("holding 4321 gold of its own"), Character->Gold, static_cast<int64>(4321));
	TestEqual(TEXT("the run file is read rather than migrated"), static_cast<int32>(RunResult),
			  static_cast<int32>(ECataclysmSaveLoadResult::Loaded));
	TestEqual(TEXT("its floor holds two ground items"), Golds.Num(), 2);
	TestTrue(TEXT("the piece of gear with no gold, and the pile with 37"), Golds == TArray<int32>({ 0, 37 }));
	return true;
}

// ---------------------------------------------------------------------------
// What a player and the log read
// ---------------------------------------------------------------------------

/**
 * The words: a pile's name with a comma every three digits; the map's balance line, which is the balance the character
 * has -- its lethality mode's, or a Solo Self-Found character's own -- and empty with nobody playing; the inventory
 * header with the balance; and the play loop's line at a walk's end. Prints "WORDS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGoldWordsTest,
	"Cataclysm.Gold.TheNamesTheMapLineTheInventoryAndThePlayLoopLineSayTheGold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGoldWordsTest::RunTest(const FString&)
{
	using namespace CataclysmGoldTest;
	using Layout = UCataclysmEmpireMapLayout;

	FGoldScene Scene = MakeScene();
	FGoldScene Solo = MakeScene(ECataclysmLethality::Standard, /*bSoloSelfFound=*/true);
	ON_SCOPE_EXIT
	{
		for (UWorld* World : { Scene.World, Solo.World })
		{
			if (World)
			{
				World->DestroyWorld(/*bInformEngineOfWorld=*/false);
			}
		}
	};
	if (!TestTrue(TEXT("set-up: the two scenes were built"), Scene.IsUsable() && Solo.IsUsable()))
	{
		return false;
	}

	const FString MapBefore = Layout::GoldLineIn(Scene.World);
	ACataclysmDroppedItem* Pile = UCataclysmDropSpawner::PutOnTheFloor(
		Scene.World, FVector(100.0f, 0.0f, 0.0f), FCataclysmItem(), NAME_None, 0, 1234);
	UCataclysmDropPickup::BankGold(Scene.World, { Pile });
	const FString MapAfter = Layout::GoldLineIn(Scene.World);
	ACataclysmDroppedItem* SoloPile = UCataclysmDropSpawner::PutOnTheFloor(
		Solo.World, FVector(100.0f, 0.0f, 0.0f), FCataclysmItem(), NAME_None, 0, 50);
	UCataclysmDropPickup::BankGold(Solo.World, { SoloPile });
	const FString MapSolo = Layout::GoldLineIn(Solo.World);
	const FString MapNoWorld = Layout::GoldLineIn(nullptr);
	const FString LoopLine = FCataclysmPlayLoopLog::GoldLineIn(Scene.World, 3);
	const FString LoopSolo = FCataclysmPlayLoopLog::GoldLineIn(Solo.World, 7);

	UWorld* Unplayed = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT
	{
		if (Unplayed)
		{
			Unplayed->DestroyWorld(/*bInformEngineOfWorld=*/false);
		}
	};
	if (!TestNotNull(TEXT("set-up: a world nobody plays"), Unplayed))
	{
		return false;
	}
	Unplayed->SetGameInstance(NewObject<UCataclysmGameInstance>());
	const FString MapUnplayed = Layout::GoldLineIn(Unplayed);
	const FString LoopUnplayed = FCataclysmPlayLoopLog::GoldLineIn(Unplayed, 9);
	const FString Header = UCataclysmInventoryScreen::HeaderTextFor(12, 48, FString(),
																	UCataclysmGameInstance::GoldBalanceText(Scene.World));
	const FString HeaderHolding = UCataclysmInventoryScreen::HeaderTextFor(12, 48, TEXT("Quality Helm"),
																		   TEXT("1,234 gold"));

	AddInfo(FString::Printf(TEXT("WORDS: map '%s' -> '%s'; Solo Self-Found '%s'; no world '%s'; nobody playing '%s'; "
								 "loop '%s'; Solo loop '%s'; nobody's loop '%s'; header '%s'; holding '%s'"),
							*MapBefore, *MapAfter, *MapSolo, *MapNoWorld, *MapUnplayed, *LoopLine, *LoopSolo,
							*LoopUnplayed, *Header, *HeaderHolding));

	TestEqual(TEXT("0"), ACataclysmDroppedItem::GoldAmountText(0), FString(TEXT("0")));
	TestEqual(TEXT("999"), ACataclysmDroppedItem::GoldAmountText(999), FString(TEXT("999")));
	TestEqual(TEXT("1,000"), ACataclysmDroppedItem::GoldAmountText(1000), FString(TEXT("1,000")));
	TestEqual(TEXT("1,234,567"), ACataclysmDroppedItem::GoldAmountText(1234567), FString(TEXT("1,234,567")));
	TestEqual(TEXT("past 32 bits"), ACataclysmDroppedItem::GoldAmountText(3000000000LL),
			  FString(TEXT("3,000,000,000")));
	TestEqual(TEXT("-1,234"), ACataclysmDroppedItem::GoldAmountText(-1234), FString(TEXT("-1,234")));
	TestEqual(TEXT("a pile's name"), ACataclysmDroppedItem::GoldName(12), FString(TEXT("12 gold")));
	TestEqual(TEXT("the map line before any gold"), MapBefore, FString(TEXT("0 gold")));
	TestEqual(TEXT("and after a pile of 1234"), MapAfter, FString(TEXT("1,234 gold")));
	TestEqual(TEXT("a Solo Self-Found character's map line is its own balance"), MapSolo, FString(TEXT("50 gold")));
	TestEqual(TEXT("no map line with no world"), MapNoWorld, FString());
	TestEqual(TEXT("nor with nobody playing"), MapUnplayed, FString());
	TestEqual(TEXT("the play loop line"), LoopLine, FString(TEXT("dungeon 3's walk picked up 1,234 gold; 1,234 gold held")));
	TestEqual(TEXT("and a Solo Self-Found one's"), LoopSolo,
			  FString(TEXT("dungeon 7's walk picked up 50 gold; 50 gold held")));
	TestEqual(TEXT("and with nobody playing"), LoopUnplayed,
			  FString(TEXT("dungeon 9's walk picked up 0 gold; no gold balance to hold it")));
	TestEqual(TEXT("the inventory header shows the balance"), Header, FString(TEXT("Carried    12 / 48        1,234 gold")));
	TestEqual(TEXT("before what is held"), HeaderHolding,
			  FString(TEXT("Carried    12 / 48        1,234 gold        holding Quality Helm")));
	TestEqual(TEXT("CONTROL: with no balance it is the header it was"), UCataclysmInventoryScreen::HeaderTextFor(12, 48),
			  FString(TEXT("Carried    12 / 48")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
