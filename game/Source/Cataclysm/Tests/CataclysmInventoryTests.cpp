// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for what a character carries. Issue #714.
 *
 * WHAT THE INTERESTING CASE IS, AND IT IS NOT ADDING AN ITEM. The design's rule
 * is that the inventory is 48 slots and that an item which will not fit **stays
 * on the floor**: there is no way out of a dungeon partway through, so a full
 * inventory is a choice about what is worth carrying rather than a trip to town.
 * That makes the refusal the behaviour worth guarding, because a refusal that
 * silently succeeded would destroy the item and look like a successful pick-up.
 *
 * THE SIZE IS CHECKED FROM PYTHON AS WELL, by
 * `tools/tests/test_carried_inventory_is_forty_eight_slots.py`, which reads the
 * constants out of the header as text and compares them against the design
 * document. Continuous integration compiles no C++, so the tests in this file do
 * not run on a pull request and that one does.
 */
namespace CataclysmInventoryTest
{
	static UWorld* MakeWorldThatHasBegunPlay()
	{
		return CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	}

	/** A component with no actor. The store needs no world, so most of these
	 *  tests need no world either. */
	UCataclysmInventoryComponent* MakeInventory()
	{
		return NewObject<UCataclysmInventoryComponent>(GetTransientPackage());
	}

	/**
	 * An item that is distinguishable from every other one this file makes.
	 *
	 * THE BASE IS WHAT MAKES A SLOT NOT EMPTY, so it is what has to differ; the
	 * upgrade level rides along so a test can tell one stored item from another
	 * after they have been moved about.
	 */
	FCataclysmItem ItemNumber(int32 N)
	{
		FCataclysmItem Item;
		Item.Base = FName(*FString::Printf(TEXT("TestBase%d"), N));
		Item.GearLevel = N % 11;
		return Item;
	}
}

// ---------------------------------------------------------------------------
// The size, and what happens at the edge of it
// ---------------------------------------------------------------------------

/**
 * It holds 48 and refuses the 49th, leaving that item with its caller.
 *
 * THE REFUSAL IS THE POINT. `AddItem` answering INDEX_NONE is what lets the drop
 * stay on the floor. If it ever answered a slot number here, the 49th item would
 * overwrite something a player had chosen to carry.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInventoryHoldsFortyEightTest,
	"Cataclysm.Inventory.ItHoldsFortyEightAndRefusesTheFortyNinth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmInventoryHoldsFortyEightTest::RunTest(const FString&)
{
	using namespace CataclysmInventoryTest;

	UCataclysmInventoryComponent* Inventory = MakeInventory();
	if (!TestNotNull(TEXT("inventory"), Inventory))
	{
		return false;
	}

	TestEqual(TEXT("the design's 48 slots"),
		UCataclysmInventoryComponent::SlotCount, 48);
	TestEqual(TEXT("four rows"), UCataclysmInventoryComponent::Rows, 4);
	TestEqual(TEXT("of twelve"), UCataclysmInventoryComponent::Columns, 12);

	// EVERY SLOT EXISTS FROM THE START, empty. A store that grew as items
	// arrived would make slot 30 unaddressable until 30 items had been carried.
	TestEqual(TEXT("48 slots exist before anything is carried"),
		Inventory->GetSlots().Num(), 48);
	TestEqual(TEXT("and none of them holds anything"), Inventory->NumItems(), 0);
	TestEqual(TEXT("so all 48 are free"), Inventory->NumFreeSlots(), 48);
	TestFalse(TEXT("an empty inventory is not full"), Inventory->IsFull());
	TestEqual(TEXT("the first free slot is the first slot"),
		Inventory->FirstFreeSlot(), 0);

	// FILLED IN ORDER, so the slot each item lands in is predictable.
	for (int32 N = 0; N < 48; ++N)
	{
		TestEqual(FString::Printf(TEXT("item %d goes into slot %d"), N, N),
			Inventory->AddItem(ItemNumber(N)), N);
	}

	TestEqual(TEXT("48 items are carried"), Inventory->NumItems(), 48);
	TestEqual(TEXT("no slot is free"), Inventory->NumFreeSlots(), 0);
	TestTrue(TEXT("the inventory is full"), Inventory->IsFull());
	TestEqual(TEXT("there is no first free slot"),
		Inventory->FirstFreeSlot(), INDEX_NONE);

	// THE 49TH. Refused, and nothing about the inventory changed.
	const FCataclysmItem Refused = ItemNumber(1000);
	TestEqual(TEXT("the 49th item is refused"),
		Inventory->AddItem(Refused), INDEX_NONE);
	TestEqual(TEXT("and the inventory still holds 48"),
		Inventory->NumItems(), 48);
	TestEqual(TEXT("and is still 48 slots long"),
		Inventory->GetSlots().Num(), 48);

	// AND THE REFUSED ITEM IS NOWHERE IN IT. Counting to 48 would still pass if
	// the new item had replaced one that was already carried.
	bool bRefusedIsStored = false;
	for (const FCataclysmItem& Stored : Inventory->GetSlots())
	{
		bRefusedIsStored = bRefusedIsStored || Stored.Base == Refused.Base;
	}
	TestFalse(TEXT("the refused item did not overwrite a carried one"),
		bRefusedIsStored);

	// AND EVERY ITEM THAT WAS ACCEPTED IS STILL THE ONE THAT WENT IN.
	for (int32 N = 0; N < 48; ++N)
	{
		const FCataclysmItem* Stored = Inventory->ItemAt(N);
		if (!TestNotNull(FString::Printf(TEXT("slot %d holds something"), N), Stored))
		{
			return false;
		}
		TestEqual(FString::Printf(TEXT("slot %d holds item %d"), N, N),
			Stored->Base, ItemNumber(N).Base);
	}

	return true;
}

/**
 * A gap left by a removal is refilled before the end of the grid is used.
 *
 * WHY THE LOWEST FREE SLOT RATHER THAN THE NEXT ONE ALONG. A cursor that only
 * moved forward would leave a full inventory permanently full: dropping one item
 * would free slot 5 and the next pick-up would still find nothing after slot 47.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInventoryRefillsGapsTest,
	"Cataclysm.Inventory.AGapIsRefilledBeforeTheEndOfTheGrid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmInventoryRefillsGapsTest::RunTest(const FString&)
{
	using namespace CataclysmInventoryTest;

	UCataclysmInventoryComponent* Inventory = MakeInventory();
	if (!TestNotNull(TEXT("inventory"), Inventory))
	{
		return false;
	}

	for (int32 N = 0; N < 48; ++N)
	{
		Inventory->AddItem(ItemNumber(N));
	}
	TestTrue(TEXT("full before anything is removed"), Inventory->IsFull());

	TestTrue(TEXT("slot 5 gave something up"), Inventory->RemoveItemAt(5));
	TestEqual(TEXT("47 items are left"), Inventory->NumItems(), 47);
	TestFalse(TEXT("and it is no longer full"), Inventory->IsFull());
	TestEqual(TEXT("the gap is the first free slot"),
		Inventory->FirstFreeSlot(), 5);
	TestNull(TEXT("the emptied slot holds nothing"), Inventory->ItemAt(5));

	// THE ITEMS AFTER THE GAP DID NOT MOVE. Removing by shortening the array
	// would have slid item 6 into slot 5 and every later item down one, which a
	// player looking at a grid would see as their inventory rearranging itself.
	const FCataclysmItem* AfterTheGap = Inventory->ItemAt(6);
	if (!TestNotNull(TEXT("slot 6 still holds something"), AfterTheGap))
	{
		return false;
	}
	TestEqual(TEXT("slot 6 still holds item 6"),
		AfterTheGap->Base, ItemNumber(6).Base);
	TestEqual(TEXT("and the store is still 48 long"),
		Inventory->GetSlots().Num(), 48);

	TestEqual(TEXT("the next item goes into the gap"),
		Inventory->AddItem(ItemNumber(500)), 5);
	TestTrue(TEXT("and the inventory is full again"), Inventory->IsFull());

	return true;
}

// ---------------------------------------------------------------------------
// What is refused, and what does not crash
// ---------------------------------------------------------------------------

/**
 * An item with no base is refused, because storing one would fill a slot that
 * every count would go on reading as free.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInventoryRefusesANonItemTest,
	"Cataclysm.Inventory.AnItemWithNoBaseIsRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmInventoryRefusesANonItemTest::RunTest(const FString&)
{
	using namespace CataclysmInventoryTest;

	UCataclysmInventoryComponent* Inventory = MakeInventory();
	if (!TestNotNull(TEXT("inventory"), Inventory))
	{
		return false;
	}

	TestTrue(TEXT("a default item reads as an empty slot"),
		UCataclysmInventoryComponent::SlotIsEmpty(FCataclysmItem()));
	TestFalse(TEXT("and one with a base does not"),
		UCataclysmInventoryComponent::SlotIsEmpty(ItemNumber(1)));

	TestEqual(TEXT("an item with no base is refused"),
		Inventory->AddItem(FCataclysmItem()), INDEX_NONE);
	TestEqual(TEXT("and nothing is carried"), Inventory->NumItems(), 0);
	TestEqual(TEXT("and all 48 slots are still free"),
		Inventory->NumFreeSlots(), 48);

	// AN ITEM CARRYING EVERYTHING BUT A BASE IS STILL NOT AN ITEM. Affixes and
	// sockets do not make one, because nothing can name or value a piece whose
	// base is unknown.
	FCataclysmItem Baseless;
	Baseless.GearLevel = 10;
	Baseless.Sockets = 3;
	Baseless.Residue = 400.0f;
	TestEqual(TEXT("an item with affixes but no base is refused too"),
		Inventory->AddItem(Baseless), INDEX_NONE);
	TestEqual(TEXT("and still nothing is carried"), Inventory->NumItems(), 0);

	return true;
}

/**
 * A slot number that is not a slot is refused rather than read.
 *
 * `Slots.IsValidIndex` is what does this. Written as a test because the negative
 * case is the one an out-by-one in a future interface would hit, and reading
 * past the end of the array would not fail loudly.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInventoryRejectsBadSlotsTest,
	"Cataclysm.Inventory.ASlotThatIsNotASlotIsRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmInventoryRejectsBadSlotsTest::RunTest(const FString&)
{
	using namespace CataclysmInventoryTest;

	UCataclysmInventoryComponent* Inventory = MakeInventory();
	if (!TestNotNull(TEXT("inventory"), Inventory))
	{
		return false;
	}

	Inventory->AddItem(ItemNumber(1));

	const int32 NotSlots[] = { -1, -100, 48, 49, 1000 };
	for (int32 Slot : NotSlots)
	{
		TestNull(FString::Printf(TEXT("slot %d cannot be read"), Slot),
			Inventory->ItemAt(Slot));
		TestFalse(FString::Printf(TEXT("slot %d cannot be emptied"), Slot),
			Inventory->RemoveItemAt(Slot));
	}

	// AN EMPTY SLOT IS A REAL SLOT AND STILL ANSWERS NO. Otherwise a caller
	// could not tell "I took something out" from "there was nothing there".
	TestFalse(TEXT("emptying an already empty slot answers no"),
		Inventory->RemoveItemAt(20));
	TestNull(TEXT("and reading it gives nothing"), Inventory->ItemAt(20));

	TestEqual(TEXT("the one real item survived all of that"),
		Inventory->NumItems(), 1);

	return true;
}

// ---------------------------------------------------------------------------
// What comes out is what went in
// ---------------------------------------------------------------------------

/**
 * A whole item survives being carried: its upgrade level, affixes, sockets and
 * residue all come back out.
 *
 * WHY THIS IS WORTH ASSERTING. The slot stores a copy. A store that kept only
 * the base would pass every count in this file and lose everything that makes
 * one piece different from another.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInventoryKeepsTheWholeItemTest,
	"Cataclysm.Inventory.AnItemComesBackOutTheWayItWentIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmInventoryKeepsTheWholeItemTest::RunTest(const FString&)
{
	using namespace CataclysmInventoryTest;

	UCataclysmInventoryComponent* Inventory = MakeInventory();
	if (!TestNotNull(TEXT("inventory"), Inventory))
	{
		return false;
	}

	FCataclysmRolledAffix Affix;
	Affix.Affix = FName(TEXT("IncreasedHealth"));
	Affix.Tier = 6;
	Affix.Roll = 0.75f;
	Affix.DamageTypes = { FName(TEXT("Demonic")) };

	FCataclysmItem Item;
	Item.Base = FName(TEXT("Greataxe"));
	Item.GearLevel = 7;
	Item.Affixes = { Affix };
	Item.EnchantmentCount = 1;
	Item.Sockets = 2;
	Item.Residue = 312.5f;

	const int32 Slot = Inventory->AddItem(Item);
	if (!TestEqual(TEXT("it went into the first slot"), Slot, 0))
	{
		return false;
	}

	const FCataclysmItem* Stored = Inventory->ItemAt(Slot);
	if (!TestNotNull(TEXT("and it is there"), Stored))
	{
		return false;
	}

	TestEqual(TEXT("the base survived"), Stored->Base, Item.Base);
	TestEqual(TEXT("the upgrade level survived"), Stored->GearLevel, 7);
	TestEqual(TEXT("the enchantment count survived"), Stored->EnchantmentCount, 1);
	TestEqual(TEXT("the socket count survived"), Stored->Sockets, 2);
	TestEqual(TEXT("the residue survived"), Stored->Residue, 312.5f);

	if (!TestEqual(TEXT("the affix survived"), Stored->Affixes.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("which affix it was"), Stored->Affixes[0].Affix, Affix.Affix);
	TestEqual(TEXT("its tier"), Stored->Affixes[0].Tier, 6);
	TestEqual(TEXT("where in the band it rolled"), Stored->Affixes[0].Roll, 0.75f);
	TestEqual(TEXT("and which damage type it covers"),
		Stored->Affixes[0].DamageTypes.Num(), 1);

	// EMPTYING EVERYTHING LEAVES 48 EMPTY SLOTS, not zero slots.
	Inventory->AddItem(ItemNumber(2));
	Inventory->RemoveEverything();
	TestEqual(TEXT("nothing is carried"), Inventory->NumItems(), 0);
	TestEqual(TEXT("and the grid is still 48 slots"),
		Inventory->GetSlots().Num(), 48);

	return true;
}

// ---------------------------------------------------------------------------
// The player actually carries one
// ---------------------------------------------------------------------------

/**
 * The player character has an inventory, empty, with all 48 slots.
 *
 * WHY THIS EXISTS SEPARATELY FROM EVERYTHING ABOVE. Every other test in this
 * file builds the component directly, so all of them would still pass if nothing
 * in the game ever created one. That is exactly the failure issue #169 recorded
 * for the weapon slots: the component worked and no pawn had one, so a play
 * session had no ability filled at all.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlayerCarriesAnInventoryTest,
	"Cataclysm.Inventory.ThePlayerCharacterCarriesOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayerCarriesAnInventoryTest::RunTest(const FString&)
{
	using namespace CataclysmInventoryTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("player character spawned"), Character))
	{
		return false;
	}

	UCataclysmInventoryComponent* Inventory =
		Character->FindComponentByClass<UCataclysmInventoryComponent>();
	if (!TestNotNull(TEXT("the player character has an inventory"), Inventory))
	{
		return false;
	}

	TestEqual(TEXT("with 48 slots"), Inventory->GetSlots().Num(), 48);
	TestEqual(TEXT("carrying nothing"), Inventory->NumItems(), 0);

	// AND IT WORKS ON THE PAWN, not only in isolation.
	TestEqual(TEXT("an item can be put into it"),
		Inventory->AddItem(ItemNumber(1)), 0);
	TestEqual(TEXT("and is then carried"), Inventory->NumItems(), 1);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
