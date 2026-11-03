// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Interface/CataclysmInventoryScreen.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmWearing.h"

/**
 * Tests for drawing a held item under the cursor and its slot as lifted. Issue #878.
 *
 * WHAT IS COVERED. The four judgements the drawing rests on, each a static function on
 * UCataclysmInventoryScreen over plain values or an inventory: which slot is drawn on the
 * cursor (and that nothing is when nothing is held), how each carried cell looks -- empty,
 * full or lifted -- as an item is picked up, put down and let go of, what a lifted cell's
 * frame and text become, and where the drawn item goes for a cursor position.
 *
 * WHAT IS NOT COVERED, said plainly: the drawing. That UCataclysmInventoryWidget really
 * paints the cell under the cursor, that it follows the mouse, that the lifted cell looks
 * lifted and that the cell under a held item still shows its tool tip all need a renderer,
 * and the automation command in tools/unreal_build.py passes -nullrhi (issue #559).
 *
 * NO TABLE IS LOADED. Every inventory here is a fresh component holding items by row key,
 * and whether a cell is empty, full or lifted never asks what the row says.
 */
namespace CataclysmHeldItemTest
{
	using FScreen = UCataclysmInventoryScreen;

	const TCHAR* HeldHelmBase = TEXT("Head_Helm");
	const TCHAR* HeldBootsBase = TEXT("Boots_Sabatons");
	const TCHAR* HeldMoteMaterial = TEXT("Material_Corrupted_Mote");

	/** A gear item of a base, with nothing else on it but an upgrade level to tell it apart. */
	FCataclysmItem ItemOfBase(const TCHAR* Base, int32 GearLevel = 0)
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);
		Item.GearLevel = GearLevel;
		return Item;
	}

	/** How many of the 48 cells of a bag look lifted. */
	int32 LiftedCellsIn(const UCataclysmInventoryComponent* Inventory)
	{
		int32 Lifted = 0;
		for (int32 Index = 0; Index < UCataclysmInventoryComponent::SlotCount; ++Index)
		{
			if (FScreen::CellLookFor(Inventory, Index) == ECataclysmCellLook::Lifted)
			{
				++Lifted;
			}
		}
		return Lifted;
	}

	/**
	 * A fresh bag holding a helm, a pair of boots and a stack of five motes.
	 *
	 * A FRESH COMPONENT HAS 48 EMPTY SLOTS AND HOLDS NOTHING: its constructor sets the 48,
	 * and `Held` starts at INDEX_NONE. AddItem and AddMaterial take the lowest free slot,
	 * so the three land in 0, 1 and 2 -- which each test asserts as set-up rather than
	 * assuming.
	 */
	struct FHeldItemBag
	{
		FHeldItemBag()
			: Inventory(NewObject<UCataclysmInventoryComponent>(GetTransientPackage()))
		{
			Helm = Inventory->AddItem(ItemOfBase(HeldHelmBase));
			Boots = Inventory->AddItem(ItemOfBase(HeldBootsBase));
			Mote = Inventory->AddMaterial(FName(HeldMoteMaterial), 5);
		}

		/** Whether the three landed where a fresh bag puts them, and nothing is held. */
		bool IsAsExpected() const
		{
			return Helm == 0 && Boots == 1 && Mote == 2 && !Inventory->IsHolding()
				&& UCataclysmInventoryComponent::SlotIsEmpty(Inventory->GetSlots()[3]);
		}

		/** How many of the 48 cells look lifted. */
		int32 LiftedCells() const { return LiftedCellsIn(Inventory); }

		UCataclysmInventoryComponent* Inventory = nullptr;
		int32 Helm = INDEX_NONE;
		int32 Boots = INDEX_NONE;
		int32 Mote = INDEX_NONE;
	};

	/** The first empty slot in a fresh FHeldItemBag. */
	constexpr int32 HeldEmptySlot = 3;
}

// ---------------------------------------------------------------------------
// What is drawn on the cursor
// ---------------------------------------------------------------------------

/**
 * Nothing is drawn under the cursor unless something is held and its slot holds something.
 *
 * THE LAST CASE IS THE ONE THAT CAN GO WRONG IN PLAY. The hold is a slot number, and a slot
 * can be emptied without the hold being let go of: a stack of a material spent to nothing,
 * the bag emptied whole or restored. An empty frame following the mouse would then say the
 * player is holding something they are not. The test empties the slot directly. Wearing the
 * held item does not reach this state: it lets go (WearingTheHeldItemLetsGoOfIt, below).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHeldItemNothingDrawnTest,
	"Cataclysm.HeldItem.NothingIsDrawnOnTheCursorUnlessSomethingIsHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHeldItemNothingDrawnTest::RunTest(const FString&)
{
	using namespace CataclysmHeldItemTest;

	TestEqual(TEXT("with no inventory nothing is drawn"),
		FScreen::HeldSlotToDraw(nullptr), static_cast<int32>(INDEX_NONE));

	FHeldItemBag Bag;
	if (!TestTrue(TEXT("set-up: a fresh bag holds the helm, the boots and the motes in "
					   "slots 0, 1 and 2, the next slot empty and nothing held"),
			Bag.IsAsExpected()))
	{
		return false;
	}

	TestEqual(TEXT("carrying things and holding none, nothing is drawn"),
		FScreen::HeldSlotToDraw(Bag.Inventory), static_cast<int32>(INDEX_NONE));

	UCataclysmWearing::PickUpCarried(Bag.Inventory, Bag.Boots);
	TestEqual(TEXT("CONTROL: holding the boots draws the boots' slot"),
		FScreen::HeldSlotToDraw(Bag.Inventory), Bag.Boots);

	UCataclysmWearing::PutDownCarried(Bag.Inventory, Bag.Boots);
	TestEqual(TEXT("put back down where they came from, nothing is drawn"),
		FScreen::HeldSlotToDraw(Bag.Inventory), static_cast<int32>(INDEX_NONE));

	UCataclysmWearing::PickUpCarried(Bag.Inventory, Bag.Boots);
	UCataclysmWearing::ReleaseHeld(Bag.Inventory);
	TestEqual(TEXT("let go of, as closing the screen does, nothing is drawn"),
		FScreen::HeldSlotToDraw(Bag.Inventory), static_cast<int32>(INDEX_NONE));

	// A HOLD LEFT ON A SLOT THAT HAS BEEN EMPTIED.
	UCataclysmWearing::PickUpCarried(Bag.Inventory, Bag.Boots);
	Bag.Inventory->RemoveItemAt(Bag.Boots);
	if (!TestTrue(TEXT("set-up: the hold is still on the emptied slot"),
			Bag.Inventory->IsHolding() && Bag.Inventory->HeldSlot() == Bag.Boots
			&& UCataclysmInventoryComponent::SlotIsEmpty(
				   Bag.Inventory->GetSlots()[Bag.Boots])))
	{
		return false;
	}
	TestEqual(TEXT("a hold on a slot that has been emptied draws nothing"),
		FScreen::HeldSlotToDraw(Bag.Inventory), static_cast<int32>(INDEX_NONE));

	return true;
}

// ---------------------------------------------------------------------------
// How each cell looks
// ---------------------------------------------------------------------------

/**
 * Exactly the held slot looks lifted, and only while it is held.
 *
 * COUNTED OVER ALL 48 AS WELL AS ASKED OF THE ONE CELL, because a rule that lifted the
 * held slot and also some other would pass a test that only looked at the held one.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHeldItemLiftedLookTest,
	"Cataclysm.HeldItem.OnlyTheHeldSlotLooksLiftedAndOnlyWhileHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHeldItemLiftedLookTest::RunTest(const FString&)
{
	using namespace CataclysmHeldItemTest;

	FHeldItemBag Bag;
	if (!TestTrue(TEXT("set-up: a fresh bag holds the helm, the boots and the motes in "
					   "slots 0, 1 and 2, the next slot empty and nothing held"),
			Bag.IsAsExpected()))
	{
		return false;
	}

	// NOTHING HELD.
	TestTrue(TEXT("nothing held, a slot with gear looks full"),
		FScreen::CellLookFor(Bag.Inventory, Bag.Helm) == ECataclysmCellLook::Full);
	TestTrue(TEXT("nothing held, an empty slot looks empty"),
		FScreen::CellLookFor(Bag.Inventory, HeldEmptySlot) == ECataclysmCellLook::Empty);
	TestEqual(TEXT("nothing held, no cell looks lifted"), Bag.LiftedCells(), 0);

	// THE BOOTS ON THE CURSOR.
	UCataclysmWearing::PickUpCarried(Bag.Inventory, Bag.Boots);
	TestTrue(TEXT("the slot on the cursor looks lifted"),
		FScreen::CellLookFor(Bag.Inventory, Bag.Boots) == ECataclysmCellLook::Lifted);
	TestEqual(TEXT("exactly one cell looks lifted"), Bag.LiftedCells(), 1);
	TestTrue(TEXT("CONTROL: the helm beside it still looks full"),
		FScreen::CellLookFor(Bag.Inventory, Bag.Helm) == ECataclysmCellLook::Full);
	TestTrue(TEXT("CONTROL: the stack of motes still looks full"),
		FScreen::CellLookFor(Bag.Inventory, Bag.Mote) == ECataclysmCellLook::Full);
	TestTrue(TEXT("an empty slot still looks empty while something is held"),
		FScreen::CellLookFor(Bag.Inventory, HeldEmptySlot) == ECataclysmCellLook::Empty);

	// PUT DOWN IN THE EMPTY SLOT: the boots move and nothing is held.
	UCataclysmWearing::PutDownCarried(Bag.Inventory, HeldEmptySlot);
	TestTrue(TEXT("put down elsewhere, the boots' new slot looks full"),
		FScreen::CellLookFor(Bag.Inventory, HeldEmptySlot) == ECataclysmCellLook::Full);
	TestTrue(TEXT("and the slot they left looks empty"),
		FScreen::CellLookFor(Bag.Inventory, Bag.Boots) == ECataclysmCellLook::Empty);
	TestEqual(TEXT("and no cell looks lifted once it is put down"), Bag.LiftedCells(), 0);

	// PICKED UP AND LET GO OF, which is what closing the screen does.
	UCataclysmWearing::PickUpCarried(Bag.Inventory, Bag.Helm);
	TestTrue(TEXT("CONTROL: the helm looks lifted while it is held"),
		FScreen::CellLookFor(Bag.Inventory, Bag.Helm) == ECataclysmCellLook::Lifted);
	UCataclysmWearing::ReleaseHeld(Bag.Inventory);
	TestTrue(TEXT("let go of, the helm looks full again"),
		FScreen::CellLookFor(Bag.Inventory, Bag.Helm) == ECataclysmCellLook::Full);
	TestEqual(TEXT("and no cell looks lifted once it is let go of"), Bag.LiftedCells(), 0);

	// NOTHING TO ASK.
	TestTrue(TEXT("an index outside the bag looks empty"),
		FScreen::CellLookFor(Bag.Inventory, UCataclysmInventoryComponent::SlotCount)
			== ECataclysmCellLook::Empty);
	TestTrue(TEXT("with no inventory a cell looks empty"),
		FScreen::CellLookFor(nullptr, 0) == ECataclysmCellLook::Empty);

	return true;
}

/**
 * A lifted cell keeps its frame's hue, dimmed, and shows no name or count.
 *
 * THE FRAME IS KEPT so the rarity and the place the item came from can still be read, and a
 * lifted cell cannot be taken for an empty one; THE TEXT GOES because the name is on the
 * cursor now. Full and empty cells are drawn exactly as they were before issue #878.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHeldItemLiftedFrameTest,
	"Cataclysm.HeldItem.ALiftedCellKeepsItsFrameDimmedAndShowsNoText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHeldItemLiftedFrameTest::RunTest(const FString&)
{
	using namespace CataclysmHeldItemTest;

	TestTrue(TEXT("set-up: the lifted share is a dimming, between nought and one"),
		FScreen::LiftedFrameOpacity > 0.0f && FScreen::LiftedFrameOpacity < 1.0f);

	const FLinearColor Gold(1.0f, 0.8f, 0.0f, 1.0f);
	const FLinearColor Dimmed = FScreen::FrameColourFor(Gold, ECataclysmCellLook::Lifted);

	TestTrue(TEXT("a lifted frame keeps its hue"),
		FMath::IsNearlyEqual(Dimmed.R, Gold.R) && FMath::IsNearlyEqual(Dimmed.G, Gold.G)
		&& FMath::IsNearlyEqual(Dimmed.B, Gold.B));
	TestTrue(TEXT("a lifted frame is drawn at the lifted share of its opacity"),
		FMath::IsNearlyEqual(Dimmed.A, Gold.A * FScreen::LiftedFrameOpacity));

	TestTrue(TEXT("a full frame is drawn in the colour it was given"),
		FScreen::FrameColourFor(Gold, ECataclysmCellLook::Full).Equals(Gold));
	const FLinearColor EmptyGrey(0.35f, 0.39f, 0.44f, 1.0f);
	TestTrue(TEXT("an empty frame is drawn in the colour it was given"),
		FScreen::FrameColourFor(EmptyGrey, ECataclysmCellLook::Empty).Equals(EmptyGrey));

	TestEqual(TEXT("a lifted cell shows no name"),
		FScreen::TextShownFor(TEXT("Helm"), ECataclysmCellLook::Lifted), FString());
	TestEqual(TEXT("a lifted stack shows no count"),
		FScreen::TextShownFor(TEXT("34"), ECataclysmCellLook::Lifted), FString());
	TestEqual(TEXT("CONTROL: a full cell shows its name"),
		FScreen::TextShownFor(TEXT("Helm"), ECataclysmCellLook::Full), FString(TEXT("Helm")));
	TestEqual(TEXT("CONTROL: a full stack shows its count"),
		FScreen::TextShownFor(TEXT("34"), ECataclysmCellLook::Full), FString(TEXT("34")));

	return true;
}

// ---------------------------------------------------------------------------
// Where it is drawn
// ---------------------------------------------------------------------------

/**
 * The held item is drawn centred on the cursor, a cell's size across, and is not clamped.
 *
 * CENTRED, because a press puts the item down in the cell under the cursor, so an item
 * centred on it sits over the cell it will land in. NOT CLAMPED, because an item held back
 * on the screen while the cursor went on would stop marking the point a press acts on.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHeldItemPositionTest,
	"Cataclysm.HeldItem.TheHeldItemIsCentredOnTheCursor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHeldItemPositionTest::RunTest(const FString&)
{
	using namespace CataclysmHeldItemTest;

	const FVector2D Cursor(500.0, 300.0);

	const FVector2D Large = FScreen::HeldItemTopLeftFor(Cursor, 48.0f);
	TestTrue(TEXT("a 48 pixel cell under a cursor at 500, 300 starts at 476, 276"),
		Large.Equals(FVector2D(476.0, 276.0), 0.001));
	TestTrue(TEXT("so the cursor is the middle of the drawn cell"),
		(Large + FVector2D(24.0, 24.0)).Equals(Cursor, 0.001));

	const FVector2D Small = FScreen::HeldItemTopLeftFor(Cursor, 20.0f);
	TestTrue(TEXT("a 20 pixel cell under the same cursor starts at 490, 290"),
		Small.Equals(FVector2D(490.0, 290.0), 0.001));

	TestTrue(TEXT("at the screen's top left corner it is not pulled back on: -24, -24"),
		FScreen::HeldItemTopLeftFor(FVector2D::ZeroVector, 48.0f)
			.Equals(FVector2D(-24.0, -24.0), 0.001));

	TestTrue(TEXT("a negative cell size is treated as nought: the corner is the cursor"),
		FScreen::HeldItemTopLeftFor(Cursor, -10.0f).Equals(Cursor, 0.001));

	return true;
}

// ---------------------------------------------------------------------------
// Wearing what is held
// ---------------------------------------------------------------------------

/**
 * Wearing the held item lets go of it; wearing another item does not.
 *
 * RULED 2026-11-02 UNDER ISSUE #878. The hold is a slot number. A right click on the held item
 * wears it, and what comes off lands in the first free slot -- here the very slot the held
 * item left -- so a hold left in place would draw that piece on the pointer as though the
 * player had picked it up. `UCataclysmWearing::WearFromCarried` is what the right button calls
 * (`ACataclysmPlayerController::RightPressOnTheInventoryScreen`), so this calls it directly.
 *
 * THE CONTROL COMES FIRST: holding the boots and wearing a different helm keeps the boots on
 * the cursor, so a rule that let go on every wear would fail it.
 *
 * NO TABLE IS LOADED HERE EITHER, beyond what wearing loads for itself, as the
 * `Cataclysm.Wearing.` tests do: a helm goes on the head by its base.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmHeldItemWearingLetsGoTest,
	"Cataclysm.HeldItem.WearingTheHeldItemLetsGoOfIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmHeldItemWearingLetsGoTest::RunTest(const FString&)
{
	using namespace CataclysmHeldItemTest;

	// A HELM WORN, AND IN A FRESH BAG A SECOND HELM, THE BOOTS AND A THIRD HELM, in slots 0, 1
	// and 2. The upgrade levels tell the three helms apart.
	UCataclysmInventoryComponent* Inventory =
		NewObject<UCataclysmInventoryComponent>(GetTransientPackage());
	UCataclysmEquipmentComponent* Equipment =
		NewObject<UCataclysmEquipmentComponent>(GetTransientPackage());

	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	const ECataclysmWearResult FirstOn = UCataclysmWearing::WearFromCarried(
		Inventory, Equipment, Inventory->AddItem(ItemOfBase(HeldHelmBase, 1)), Went);
	const int32 SecondHelm = Inventory->AddItem(ItemOfBase(HeldHelmBase, 2));
	const int32 Boots = Inventory->AddItem(ItemOfBase(HeldBootsBase));
	const int32 ThirdHelm = Inventory->AddItem(ItemOfBase(HeldHelmBase, 3));
	if (!TestTrue(TEXT("set-up: the first helm is worn on the head, and the second helm, the boots "
					   "and the third helm are carried in slots 0, 1 and 2 with nothing held"),
			FirstOn == ECataclysmWearResult::Worn && Went == ECataclysmGearSlot::Head
			&& SecondHelm == 0 && Boots == 1 && ThirdHelm == 2 && !Inventory->IsHolding()))
	{
		return false;
	}

	// CONTROL: HOLDING THE BOOTS, THE THIRD HELM IS WORN WITH THE RIGHT BUTTON.
	UCataclysmWearing::PickUpCarried(Inventory, Boots);
	const ECataclysmWearResult OtherOn =
		UCataclysmWearing::WearFromCarried(Inventory, Equipment, ThirdHelm, Went);
	if (!TestTrue(TEXT("set-up: the third helm goes on over the first, which comes off into the "
					   "slot the third helm left"),
			OtherOn == ECataclysmWearResult::Swapped && Inventory->ItemAt(ThirdHelm)
			&& Inventory->ItemAt(ThirdHelm)->GearLevel == 1))
	{
		return false;
	}
	TestTrue(TEXT("CONTROL: wearing a different item keeps the boots on the cursor"),
		Inventory->IsHolding() && Inventory->HeldSlot() == Boots);
	TestEqual(TEXT("CONTROL: the boots are still drawn on the cursor"),
		FScreen::HeldSlotToDraw(Inventory), Boots);
	TestEqual(TEXT("CONTROL: and their cell is the one cell that looks lifted"),
		LiftedCellsIn(Inventory), 1);

	// THE RULING: THE SECOND HELM IS PICKED UP AND WORN WITH THE RIGHT BUTTON.
	UCataclysmWearing::ReleaseHeld(Inventory);
	UCataclysmWearing::PickUpCarried(Inventory, SecondHelm);
	if (!TestTrue(TEXT("set-up: the second helm is on the cursor"),
			Inventory->IsHolding() && Inventory->HeldSlot() == SecondHelm))
	{
		return false;
	}
	const ECataclysmWearResult HeldOn =
		UCataclysmWearing::WearFromCarried(Inventory, Equipment, SecondHelm, Went);
	if (!TestTrue(TEXT("set-up: the held helm goes on, and the third helm comes off into the very "
					   "slot the held one left"),
			HeldOn == ECataclysmWearResult::Swapped && Inventory->ItemAt(SecondHelm)
			&& Inventory->ItemAt(SecondHelm)->GearLevel == 3))
	{
		return false;
	}
	TestFalse(TEXT("wearing the held item lets go of it"), Inventory->IsHolding());
	TestEqual(TEXT("so nothing is drawn on the cursor, not the helm that came off"),
		FScreen::HeldSlotToDraw(Inventory), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("and no cell looks lifted"), LiftedCellsIn(Inventory), 0);
	TestTrue(TEXT("the slot the worn helm left, holding the one that came off, looks full"),
		FScreen::CellLookFor(Inventory, SecondHelm) == ECataclysmCellLook::Full);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
