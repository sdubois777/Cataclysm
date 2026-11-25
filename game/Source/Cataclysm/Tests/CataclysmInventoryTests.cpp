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
	for (const FCataclysmCarriedSlot& Stored : Inventory->GetSlots())
	{
		bRefusedIsStored = bRefusedIsStored || Stored.Item.Base == Refused.Base;
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
	Baseless.Sockets.SetNum(3);
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
	Item.Sockets.SetNum(2);
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
	TestEqual(TEXT("the socket count survived"), Stored->Sockets.Num(), 2);
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


// ---------------------------------------------------------------------------
// Crafting materials, which stack
// ---------------------------------------------------------------------------

/**
 * Every crafting material of one kind is one stack however many are held, and no
 * stack takes a gear slot.
 *
 * WHY IT MATTERS. A Cataclysm Boss averages 24 materials against a 48-slot bag,
 * so without stacking one kill could fill half of it with dust and leave no room
 * for the gear the same kill dropped. The project owner decided on 2026-08-19
 * that all crafting materials stack (issue #717), and on 2026-11-22 that they
 * live in a materials tab of their own, one stack per material with no limit,
 * so the 48 slots hold gear only (issue #2849).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMaterialsStackTest,
	"Cataclysm.Inventory.CraftingMaterialsStackIntoOneSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmMaterialsStackTest::RunTest(const FString&)
{
	using namespace CataclysmInventoryTest;

	UCataclysmInventoryComponent* Inventory = MakeInventory();
	if (!TestNotNull(TEXT("inventory"), Inventory))
	{
		return false;
	}

	const FName Mote(TEXT("Material_Corrupted_Mote"));
	const FName Ash(TEXT("Material_Whispering_Ash"));

	TestEqual(TEXT("nothing is carried to begin with"),
		Inventory->CountOfMaterial(Mote), 0);
	TestEqual(TEXT("and the materials tab holds no stack"),
		Inventory->GetMaterialStacks().Num(), 0);

	TestTrue(TEXT("the first one is taken"), Inventory->AddMaterial(Mote, 1));
	TestEqual(TEXT("and one is carried"), Inventory->CountOfMaterial(Mote), 1);
	TestEqual(TEXT("in one stack"), Inventory->GetMaterialStacks().Num(), 1);
	TestEqual(TEXT("and no gear slot is used"), Inventory->NumItems(), 0);

	// TWENTY-THREE MORE, WHICH IS WHAT A CATACLYSM BOSS AVERAGES. Still one stack.
	for (int32 More = 0; More < 23; ++More)
	{
		TestTrue(TEXT("every one after the first joins the stack"),
			Inventory->AddMaterial(Mote, 1));
	}

	TestEqual(TEXT("twenty-four are carried"),
		Inventory->CountOfMaterial(Mote), 24);
	TestEqual(TEXT("in one stack"), Inventory->GetMaterialStacks().Num(), 1);
	TestEqual(TEXT("so all forty-eight gear slots are still free"),
		Inventory->NumFreeSlots(), 48);

	// A DIFFERENT MATERIAL IS A DIFFERENT STACK. Four materials share tier 1 and
	// a Corrupted Mote is not a Whispering Ash; stacking by tier would merge
	// them, which is why the stack key is the material's own name.
	TestTrue(TEXT("another material is taken"), Inventory->AddMaterial(Ash, 5));
	TestEqual(TEXT("five of it are carried"),
		Inventory->CountOfMaterial(Ash), 5);
	TestEqual(TEXT("and the first stack is untouched"),
		Inventory->CountOfMaterial(Mote), 24);
	TestEqual(TEXT("two stacks are held"), Inventory->GetMaterialStacks().Num(), 2);
	TestEqual(TEXT("and still no gear slot is used"), Inventory->NumItems(), 0);

	// SEVERAL AT ONCE, which is what picking up a whole stack would be.
	TestTrue(TEXT("adding several at once joins the same stack"),
		Inventory->AddMaterial(Mote, 10));
	TestEqual(TEXT("thirty-four are carried"),
		Inventory->CountOfMaterial(Mote), 34);
	TestEqual(TEXT("still in two stacks"), Inventory->GetMaterialStacks().Num(), 2);

	// NOTHING IS NOT A DROP.
	TestFalse(TEXT("no material cannot be added"),
		Inventory->AddMaterial(NAME_None, 5));
	TestFalse(TEXT("nor can none of one"),
		Inventory->AddMaterial(Mote, 0));
	TestFalse(TEXT("nor a negative number"),
		Inventory->AddMaterial(Mote, -3));
	TestEqual(TEXT("and none of that changed the stack"),
		Inventory->CountOfMaterial(Mote), 34);

	// A STACK IS NOT IN A GEAR SLOT, so emptying a slot cannot take it (#2849). Until then the stack sat in slot 0 and
	// putting that slot down took the whole stack.
	TestFalse(TEXT("gear slot 0 holds nothing to empty"),
		Inventory->RemoveItemAt(0));
	TestEqual(TEXT("and the stack is all still carried"),
		Inventory->CountOfMaterial(Mote), 34);
	TestEqual(TEXT("as is the other material"),
		Inventory->CountOfMaterial(Ash), 5);

	return true;
}

/**
 * Gear does not stack, and a material stack is not a gear slot.
 *
 * BOTH HALVES MATTER. Two items of one base carry different affixes, upgrade
 * levels, sockets and residue, so merging them would silently destroy one; and a
 * stack of materials must not answer as though it held gear, or the inventory
 * screen would draw a stack of dust as a sword. Since issue #2849 the stack is in
 * the materials tab and takes no gear slot at all.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGearDoesNotStackTest,
	"Cataclysm.Inventory.GearDoesNotStackAndAMaterialSlotHoldsNoItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGearDoesNotStackTest::RunTest(const FString&)
{
	using namespace CataclysmInventoryTest;

	UCataclysmInventoryComponent* Inventory = MakeInventory();
	if (!TestNotNull(TEXT("inventory"), Inventory))
	{
		return false;
	}

	// THE SAME BASE THREE TIMES TAKES THREE SLOTS.
	FCataclysmItem Sword;
	Sword.Base = FName(TEXT("Greataxe"));

	TestEqual(TEXT("the first goes into slot 0"), Inventory->AddItem(Sword), 0);
	TestEqual(TEXT("the second into slot 1"), Inventory->AddItem(Sword), 1);
	TestEqual(TEXT("the third into slot 2"), Inventory->AddItem(Sword), 2);
	TestEqual(TEXT("three slots are used"), Inventory->NumItems(), 3);

	// A MATERIAL STACK HOLDS NO ITEM, AND TAKES NO GEAR SLOT.
	const FName Mote(TEXT("Material_Corrupted_Mote"));
	if (!TestTrue(TEXT("the material is taken"), Inventory->AddMaterial(Mote, 7))
		|| !TestEqual(TEXT("into one stack"), Inventory->GetMaterialStacks().Num(), 1))
	{
		return false;
	}

	const FCataclysmCarriedSlot& Stack = Inventory->GetMaterialStacks()[0];
	TestTrue(TEXT("the stack holds no item"), Stack.Item.Base.IsNone());
	TestEqual(TEXT("and carries the material"), Stack.Material, Mote);
	TestEqual(TEXT("and its quantity"), Stack.Quantity, 7);
	TestEqual(TEXT("still three gear slots are used"), Inventory->NumItems(), 3);
	TestNull(TEXT("the next gear slot reads as no gear"), Inventory->ItemAt(3));
	TestTrue(TEXT("and does not hold the material"), Inventory->GetSlots()[3].Material.IsNone());

	// A GEAR SLOT HOLDS NO MATERIAL.
	TestTrue(TEXT("no material is stacked in a gear slot"),
		Inventory->GetSlots()[0].Material.IsNone());
	TestEqual(TEXT("and it has no quantity"),
		Inventory->GetSlots()[0].Quantity, 0);
	TestNotNull(TEXT("but it does hold an item"), Inventory->ItemAt(0));

	return true;
}

/**
 * A full gear tab still takes more of a material it carries, and a material it
 * does not carry as well: the materials tab has no limit and never refuses.
 *
 * UNTIL ISSUE #2849 A NEW MATERIAL WAS REFUSED HERE, because a stack took one of
 * the 48 slots. The owner ruled on 2026-11-22 that materials and gems have a tab
 * each, one stack per kind with no limit. Gear is still refused when the 48 are
 * full.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmFullBagAndMaterialsTest,
	"Cataclysm.Inventory.AFullBagStillTakesMoreOfAMaterialItCarries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmFullBagAndMaterialsTest::RunTest(const FString&)
{
	using namespace CataclysmInventoryTest;

	UCataclysmInventoryComponent* Inventory = MakeInventory();
	if (!TestNotNull(TEXT("inventory"), Inventory))
	{
		return false;
	}

	const FName Mote(TEXT("Material_Corrupted_Mote"));
	const FName Ash(TEXT("Material_Whispering_Ash"));

	// ONE MATERIAL, AND FORTY-EIGHT PIECES OF GEAR FILL THE GEAR TAB: the material takes none of the 48.
	TestTrue(TEXT("the material is taken"), Inventory->AddMaterial(Mote, 2));
	for (int32 N = 0; N < 47; ++N)
	{
		Inventory->AddItem(ItemNumber(N));
	}
	TestFalse(TEXT("one material and 47 pieces of gear do not fill it"), Inventory->IsFull());
	Inventory->AddItem(ItemNumber(47));
	if (!TestTrue(TEXT("the gear tab is full"), Inventory->IsFull())
		|| !TestEqual(TEXT("with 48 pieces of gear"), Inventory->NumItems(), 48))
	{
		return false;
	}

	// MORE OF WHAT IS ALREADY CARRIED STILL FITS.
	TestTrue(TEXT("more of the carried material joins its stack"),
		Inventory->AddMaterial(Mote, 100));
	TestEqual(TEXT("and the stack grew"),
		Inventory->CountOfMaterial(Mote), 102);
	TestTrue(TEXT("and the gear tab is still full"), Inventory->IsFull());

	// AND SO DOES A MATERIAL IT IS NOT CARRYING, since #2849.
	TestTrue(TEXT("a new material is taken"),
		Inventory->AddMaterial(Ash, 1));
	TestEqual(TEXT("and one of it is carried"),
		Inventory->CountOfMaterial(Ash), 1);
	TestEqual(TEXT("in a second stack"), Inventory->GetMaterialStacks().Num(), 2);
	TestEqual(TEXT("and the gear tab still holds its 48"), Inventory->NumItems(), 48);

	// AND SO IS A PIECE OF GEAR.
	TestEqual(TEXT("gear is refused too"),
		Inventory->AddItem(ItemNumber(999)), INDEX_NONE);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmInventoryCountsItsChanges,
	"Cataclysm.Inventory.EveryChangeToWhatIsCarriedRaisesTheChangeCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmInventoryCountsItsChanges::RunTest(const FString& Parameters)
{
	using namespace CataclysmInventoryTest;

	// WHAT THE COUNT IS FOR. UCataclysmInventoryWidget refreshes from NativeTick
	// and walks all 48 cells, and an item's tool tip is a dozen table lookups and
	// a string join. Rebuilding 48 of those every frame would be a great deal of
	// work for something that changes when the player picks something up, so the
	// widget rebuilds them only when this moves. Issue #733.
	//
	// A COUNT THAT MISSES A CHANGE IS THE FAILURE THAT MATTERS, because the
	// player would be shown a tool tip for an item they no longer have. A count
	// that rises when nothing changed only costs one frame's work, which is why
	// the refusal case below is checked but is the less serious of the two.
	UCataclysmInventoryComponent* Inventory = MakeInventory();

	const int32 Start = Inventory->ChangeCount();

	Inventory->AddItem(ItemNumber(1));
	const int32 AfterAdd = Inventory->ChangeCount();
	TestTrue(TEXT("putting an item in raises the count"), AfterAdd > Start);

	Inventory->AddMaterial(FName(TEXT("Material_Aetherial_Shard")), 3);
	const int32 AfterMaterial = Inventory->ChangeCount();
	TestTrue(TEXT("putting a material in raises it"), AfterMaterial > AfterAdd);

	// A SECOND OF THE SAME MATERIAL JOINS THE STACK RATHER THAN TAKING A SLOT,
	// and the stack getting bigger is still a change: the tool tip states how
	// many are carried.
	Inventory->AddMaterial(FName(TEXT("Material_Aetherial_Shard")), 2);
	const int32 AfterStack = Inventory->ChangeCount();
	TestTrue(TEXT("adding to an existing stack raises it"),
		AfterStack > AfterMaterial);

	Inventory->RemoveItemAt(0);
	const int32 AfterRemove = Inventory->ChangeCount();
	TestTrue(TEXT("taking something out raises it"), AfterRemove > AfterStack);

	// A REFUSED CHANGE IS NOT A CHANGE. Removing from an empty slot answers
	// false and touches nothing.
	Inventory->RemoveItemAt(0);
	TestEqual(TEXT("removing from an empty slot does not raise it"),
		Inventory->ChangeCount(), AfterRemove);

	// An item with no base is not an item, so it is refused.
	Inventory->AddItem(FCataclysmItem());
	TestEqual(TEXT("a refused add does not raise it"),
		Inventory->ChangeCount(), AfterRemove);

	Inventory->RemoveEverything();
	TestTrue(TEXT("emptying it raises it"),
		Inventory->ChangeCount() > AfterRemove);

	return true;
}

/**
 * Taking a material out: that many from its stack, or none when fewer are carried; a stack taken to nothing leaves the
 * materials tab (issue #2849; until then it emptied its slot). Issues #1820 and #41, for Forced Tithes, whose altar
 * takes materials as a price.
 *
 * ALL OR NOTHING IS THE POINT. A price is paid whole or not at all, so a request for more than is carried must leave
 * the stack exactly as it was.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRemoveMaterialTest,
	"Cataclysm.Inventory.RemovingAMaterialTakesThatManyOrNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRemoveMaterialTest::RunTest(const FString&)
{
	using namespace CataclysmInventoryTest;

	UCataclysmInventoryComponent* Inventory = MakeInventory();
	if (!TestNotNull(TEXT("inventory"), Inventory))
	{
		return false;
	}
	const FName Mote(TEXT("Material_Corrupted_Mote"));
	const FName Ash(TEXT("Material_Whispering_Ash"));
	Inventory->AddMaterial(Mote, 12);
	Inventory->AddMaterial(Ash, 3);

	const int32 Before = Inventory->ChangeCount();
	TestTrue(TEXT("five of twelve can be taken"), Inventory->RemoveMaterial(Mote, 5));
	TestEqual(TEXT("seven are left"), Inventory->CountOfMaterial(Mote), 7);
	TestTrue(TEXT("in the same stack, still the first"), Inventory->GetMaterialStacks().Num() == 2
		&& Inventory->GetMaterialStacks()[0].Material == Mote && Inventory->GetMaterialStacks()[0].Quantity == 7);
	TestTrue(TEXT("and it is a change"), Inventory->ChangeCount() > Before);

	// FEWER CARRIED THAN ASKED: NONE IS TAKEN.
	const int32 AfterTaking = Inventory->ChangeCount();
	TestFalse(TEXT("five of three cannot be taken"), Inventory->RemoveMaterial(Ash, 5));
	TestEqual(TEXT("and all three are still carried"), Inventory->CountOfMaterial(Ash), 3);
	TestEqual(TEXT("which is not a change"), Inventory->ChangeCount(), AfterTaking);

	// NOTHING TO TAKE.
	TestFalse(TEXT("a material not carried cannot be taken"),
			  Inventory->RemoveMaterial(FName(TEXT("Material_Aetherial_Shard")), 1));
	TestFalse(TEXT("nor none of one"), Inventory->RemoveMaterial(Mote, 0));
	TestFalse(TEXT("nor a negative number"), Inventory->RemoveMaterial(Mote, -2));
	TestEqual(TEXT("and none of that changed the stack"), Inventory->CountOfMaterial(Mote), 7);

	// THE WHOLE STACK: IT LEAVES THE MATERIALS TAB.
	TestTrue(TEXT("all seven can be taken"), Inventory->RemoveMaterial(Mote, 7));
	TestEqual(TEXT("none is carried"), Inventory->CountOfMaterial(Mote), 0);
	TestTrue(TEXT("and only the other stack is left"), Inventory->GetMaterialStacks().Num() == 1
		&& Inventory->GetMaterialStacks()[0].Material == Ash && Inventory->GetMaterialStacks()[0].Quantity == 3);
	TestEqual(TEXT("and no gear slot was ever used"), Inventory->NumItems(), 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
