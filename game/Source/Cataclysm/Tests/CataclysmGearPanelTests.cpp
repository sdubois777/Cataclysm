// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Interface/CataclysmGearPanel.h"
#include "Interface/CataclysmInventoryScreen.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"

/**
 * Tests for the panel of worn gear. Issue #831.
 *
 * WHAT CAN AND CANNOT BE CHECKED HERE. Nothing in this project can watch a
 * widget draw -- the automation command in `tools/unreal_build.py` passes
 * `-nullrhi`, which is issue #559 -- so the panel appearing at all is something
 * only a person can confirm. What is covered is every decision made outside the
 * widget, and there are three worth having:
 *
 *   **Nineteen slots and nineteen places.** A slot with no position lands off
 *   the panel, and a position two slots share puts one cell on top of another.
 *   Both look like a missing slot rather than like a fault.
 *
 *   **The screen leaves room for the panel's columns.** The cell size is worked
 *   out from the share of the viewport the panel gets, and it has to divide that
 *   by every column, not only the carried grid's twelve. Two constants say how
 *   many, in two files, and they must agree.
 *
 *   **An empty slot says what it is for.** Nineteen blank squares tell a player
 *   nothing about where a ring goes.
 */
namespace CataclysmGearPanelTest
{
	FCataclysmItem Of(const TCHAR* Base)
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);
		return Item;
	}

	UCataclysmEquipmentComponent* MakeEquipment()
	{
		return NewObject<UCataclysmEquipmentComponent>(GetTransientPackage());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGearPanelPlacesEverySlot,
	"Cataclysm.GearPanel.EverySlotHasAPlaceOfItsOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGearPanelPlacesEverySlot::RunTest(const FString& Parameters)
{
	// A SLOT WITH NO POSITION AND TWO SLOTS SHARING ONE FAIL THE SAME WAY ON
	// SCREEN: a cell the player cannot see. The first lands outside the grid and
	// the second is covered by whichever was added last, and neither says
	// anything.
	TMap<FIntPoint, ECataclysmGearSlot> Taken;

	for (const ECataclysmGearSlot Slot : UCataclysmGearSlots::AllSlots())
	{
		int32 Row = INDEX_NONE;
		int32 Column = INDEX_NONE;
		UCataclysmGearPanel::PlacementFor(Slot, Row, Column);

		const FString Name = UCataclysmGearSlots::DisplayName(Slot);

		if (Row < 0 || Column < 0)
		{
			AddError(FString::Printf(
				TEXT("%s has no place on the panel, so its cell would land "
					 "outside the grid and never be seen."), *Name));
			continue;
		}

		if (Column >= UCataclysmGearPanel::Columns)
		{
			AddError(FString::Printf(
				TEXT("%s sits in column %d and the panel is %d columns wide."),
				*Name, Column, UCataclysmGearPanel::Columns));
		}

		const FIntPoint Where(Column, Row);
		if (const ECataclysmGearSlot* Already = Taken.Find(Where))
		{
			AddError(FString::Printf(
				TEXT("%s and %s are both at row %d, column %d, so one covers "
					 "the other."), *Name,
				*UCataclysmGearSlots::DisplayName(*Already), Row, Column));
			continue;
		}
		Taken.Add(Where, Slot);
	}

	TestEqual(TEXT("every slot has a place"), Taken.Num(),
		UCataclysmGearSlots::AllSlots().Num());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGearPanelFitsOnTheScreen,
	"Cataclysm.GearPanel.TheScreenReservesRoomForEveryGearColumn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGearPanelFitsOnTheScreen::RunTest(const FString& Parameters)
{
	// TWO CONSTANTS IN TWO FILES SAY HOW WIDE THIS PANEL IS. The panel's own
	// column count decides where a cell goes; the screen's ColumnsBeside decides
	// how much width the cell size is worked out against. If the panel grew a
	// fourth column and the screen did not hear about it, every cell would stay
	// the size it was and the panel would be wider than the share of the
	// viewport it was given -- on a small window, off the edge of it.
	TestEqual(TEXT("the screen reserves exactly the panel's columns"),
		UCataclysmInventoryScreen::ColumnsBeside,
		UCataclysmGearPanel::Columns);

	// AND THE CELL SIZE REALLY DOES SHRINK when there is not enough width, which
	// is what proves the reservation is used rather than merely stated.
	const float Wide = UCataclysmInventoryScreen::CellSizeFor(2560.0f, 1440.0f);
	const float Narrow = UCataclysmInventoryScreen::CellSizeFor(800.0f, 1440.0f);
	TestTrue(FString::Printf(
		TEXT("a narrow window gives a smaller cell: %.1f against %.1f"),
		Narrow, Wide), Narrow < Wide);

	// A CELL IS NEVER ZERO OR NEGATIVE, which Slate treats as an error rather
	// than as a very small box.
	TestTrue(TEXT("even an absurd window gives a positive cell"),
		UCataclysmInventoryScreen::CellSizeFor(1.0f, 1.0f) > 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGearPanelLabels,
	"Cataclysm.GearPanel.AnEmptySlotSaysWhatItIsFor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGearPanelLabels::RunTest(const FString& Parameters)
{
	using namespace CataclysmGearPanelTest;

	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();

	// NINETEEN BLANK SQUARES WOULD SAY NOTHING. The carried grid can leave an
	// empty cell blank because all 48 of its cells accept anything; here the
	// whole point of a cell is which one thing goes in it.
	for (const ECataclysmGearSlot Slot : UCataclysmGearSlots::AllSlots())
	{
		const FString Empty =
			UCataclysmGearPanel::LabelFor(Slot, nullptr, Bases);
		TestFalse(FString::Printf(TEXT("the empty %s slot says something"),
								  *UCataclysmGearSlots::DisplayName(Slot)),
			Empty.IsEmpty());
	}

	// WHEN SOMETHING IS WORN IT IS THE BASE'S NAME, the same as the carried grid
	// shows. A cell has room for one word; the whole name is in the tool tip.
	const FCataclysmItem Helm = Of(TEXT("Head_Helm"));
	const FString Worn =
		UCataclysmGearPanel::LabelFor(ECataclysmGearSlot::Head, &Helm, Bases);
	TestEqual(TEXT("a worn helm is named by its base"), Worn, FString(TEXT("Helm")));
	TestNotEqual(TEXT("which is not what the empty slot said"), Worn,
		UCataclysmGearPanel::LabelFor(ECataclysmGearSlot::Head, nullptr, Bases));

	TestFalse(TEXT("the heading says something"),
		UCataclysmGearPanel::HeaderTextFor(3, 19).IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGearPanelBlockedHand,
	"Cataclysm.GearPanel.TheSecondHandIsMarkedBlockedByATwoHandedWeapon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGearPanelBlockedHand::RunTest(const FString& Parameters)
{
	using namespace CataclysmGearPanelTest;

	// THE ONE CELL THAT IS EMPTY AND NOT FREE. A two-handed weapon is stored in
	// the first weapon slot alone, so its affixes are not counted twice, which
	// leaves the second reading as empty. A player would try to put something in
	// it.
	UCataclysmEquipmentComponent* Equipment = MakeEquipment();

	TestFalse(TEXT("an empty second hand is not blocked"),
		UCataclysmGearPanel::SlotIsBlocked(ECataclysmGearSlot::Weapon2, Equipment));

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	Equipment->Equip(Of(TEXT("Weapon_Greatsword")), Removed, AlsoRemoved, Went);

	TestTrue(TEXT("a two-handed weapon blocks the second hand"),
		UCataclysmGearPanel::SlotIsBlocked(ECataclysmGearSlot::Weapon2, Equipment));
	TestFalse(TEXT("and does not block the first, which holds it"),
		UCataclysmGearPanel::SlotIsBlocked(ECataclysmGearSlot::Weapon1, Equipment));

	// A ONE-HANDED WEAPON BLOCKS NOTHING.
	Equipment->UnequipEverything();
	Equipment->Equip(Of(TEXT("Weapon_Sword")), Removed, AlsoRemoved, Went);
	TestFalse(TEXT("a one-handed weapon leaves the other hand free"),
		UCataclysmGearPanel::SlotIsBlocked(ECataclysmGearSlot::Weapon2, Equipment));

	// AND NEITHER DOES A HELM, which is the case a check written as "is anything
	// worn" would get wrong.
	TestFalse(TEXT("nothing else is ever blocked"),
		UCataclysmGearPanel::SlotIsBlocked(ECataclysmGearSlot::Head, Equipment));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
