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


/**
 * An empty weapon hand says what it costs; an empty boot slot does not.
 *
 * WHY THE TWO DIFFER. Every other gear slot is worth nothing while empty and
 * obviously so. A weapon hand is not: `UCataclysmItemModifiers::BlendedWeaponDamage`
 * SUMS the attack damage of both weapons, so a character holding one one-handed
 * weapon deals a little over half the base damage of the same character holding
 * two.
 *
 * AND IT IS EASY TO END UP THERE WITHOUT ASKING.
 * `UCataclysmEquipmentComponent::EquipInto` takes a two-handed weapon off when a
 * one-handed one is put into either hand, leaving the other hand empty. Issue
 * #1184 reported exactly that: a hand emptied and 40% of the damage gone, with
 * nothing on screen saying so.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGearPanelSaysAnEmptyHandCosts,
	"Cataclysm.GearPanel.AnEmptyWeaponHandSaysWhatItCostsAndOtherEmptySlotsDoNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGearPanelSaysAnEmptyHandCosts::RunTest(const FString& Parameters)
{
	using namespace CataclysmGearPanelTest;

	UCataclysmEquipmentComponent* Equipment = MakeEquipment();
	if (!TestNotNull(TEXT("equipment"), Equipment))
	{
		return false;
	}

	// NOTHING WORN AT ALL. Both hands are empty and that is its own sentence,
	// because being unarmed is a different problem from having one hand free.
	for (const ECataclysmGearSlot Hand : {ECataclysmGearSlot::Weapon1,
										  ECataclysmGearSlot::Weapon2})
	{
		TestEqual(TEXT("with nothing worn, a hand says both are empty"),
			UCataclysmGearPanel::EmptyWeaponHandNote(Hand, Equipment),
			FString(TEXT("Both hands are empty.")));
	}

	// NO OTHER EMPTY SLOT SAYS ANYTHING. An empty boot slot is worth nothing and
	// the player can see that; adding a note to all nineteen would be noise.
	for (const ECataclysmGearSlot Slot : UCataclysmGearSlots::AllSlots())
	{
		if (UCataclysmGearSlots::IsWeaponSlot(Slot))
		{
			continue;
		}
		TestTrue(*FString::Printf(
			TEXT("the %s slot says nothing about being empty"),
			*UCataclysmGearSlots::DisplayName(Slot)),
			UCataclysmGearPanel::EmptyWeaponHandNote(Slot, Equipment).IsEmpty());
	}

	// ONE ONE-HANDED WEAPON WORN. The other hand now has something to say, and
	// the hand holding the weapon does not.
	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Where = ECataclysmGearSlot::Count;
	Equipment->Equip(Of(TEXT("Weapon_Sword")), Removed, AlsoRemoved, Where);
	TestEqual(TEXT("the sword went into a weapon slot"),
		static_cast<int32>(UCataclysmGearSlots::IsWeaponSlot(Where)), 1);

	const ECataclysmGearSlot Other =
		Where == ECataclysmGearSlot::Weapon1 ? ECataclysmGearSlot::Weapon2
											 : ECataclysmGearSlot::Weapon1;

	TestTrue(TEXT("the hand holding the sword says nothing"),
		UCataclysmGearPanel::EmptyWeaponHandNote(Where, Equipment).IsEmpty());

	const FString Note =
		UCataclysmGearPanel::EmptyWeaponHandNote(Other, Equipment);
	TestFalse(TEXT("the free hand says something"), Note.IsEmpty());
	TestTrue(TEXT("and it says the damage of a second weapon adds"),
		Note.Contains(TEXT("adds its damage")));

	// A SHIELD IS NAMED, because it looks like an answer and is not: it supplies
	// no attack damage, so Axe and Shield deals what Axe alone deals.
	TestTrue(TEXT("and it warns that a shield adds no damage"),
		Note.Contains(TEXT("shield")));

	// A TWO-HANDED WEAPON WORN. The second hand is not empty, it is held, and
	// SlotIsBlocked already says that. Two different sentences about the same
	// cell would contradict each other.
	UCataclysmEquipmentComponent* TwoHanded = MakeEquipment();
	TwoHanded->Equip(Of(TEXT("Weapon_Greatsword")), Removed, AlsoRemoved, Where);
	TestTrue(TEXT("a two-handed weapon holds both hands"),
		TwoHanded->TwoHandedOccupiesBothWeaponSlots());
	TestTrue(TEXT("so the blocked second hand adds no empty-hand note"),
		UCataclysmGearPanel::EmptyWeaponHandNote(
			ECataclysmGearSlot::Weapon2, TwoHanded).IsEmpty());

	// AND NO EQUIPMENT AT ALL IS NOT A CRASH.
	TestTrue(TEXT("no equipment component says nothing"),
		UCataclysmGearPanel::EmptyWeaponHandNote(
			ECataclysmGearSlot::Weapon1, nullptr).IsEmpty());

	return true;
}

/**
 * A one-handed weapon worn over a two-handed one leaves exactly ONE hand filled.
 *
 * THIS STATES WHAT THE GAME DOES RATHER THAN WHAT IT SHOULD DO, which is what
 * issue #1184 asked for: "a test that a one-handed weapon equipped over a
 * two-handed one leaves exactly one weapon slot filled, which states the current
 * behaviour so a later change to it is deliberate."
 *
 * The behaviour is deliberate -- `EquipInto` records that refusing would leave
 * the player unable to change weapon without an explicit unequip they have no
 * reason to know about -- but it costs a large amount of damage silently, so the
 * gear panel now says so and this pins the shape that note describes.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOneHanderOverTwoLeavesAHandEmpty,
	"Cataclysm.GearPanel.AOneHandedWeaponOverATwoHandedOneLeavesExactlyOneHandFilled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOneHanderOverTwoLeavesAHandEmpty::RunTest(const FString& Parameters)
{
	using namespace CataclysmGearPanelTest;

	UCataclysmEquipmentComponent* Equipment = MakeEquipment();
	if (!TestNotNull(TEXT("equipment"), Equipment))
	{
		return false;
	}

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	ECataclysmGearSlot Where = ECataclysmGearSlot::Count;

	Equipment->Equip(Of(TEXT("Weapon_Greatsword")), Removed, AlsoRemoved, Where);
	TestTrue(TEXT("the two-handed weapon holds both hands"),
		Equipment->TwoHandedOccupiesBothWeaponSlots());

	// THE SWAP. One item goes on and the two-handed weapon comes off.
	Equipment->Equip(Of(TEXT("Weapon_Sword")), Removed, AlsoRemoved, Where);

	int32 HandsFilled = 0;
	for (const ECataclysmGearSlot Slot : UCataclysmGearSlots::AllSlots())
	{
		if (UCataclysmGearSlots::IsWeaponSlot(Slot)
			&& Equipment->EquippedAt(Slot) != nullptr)
		{
			++HandsFilled;
		}
	}

	TestEqual(TEXT("exactly one hand is filled after the swap"), HandsFilled, 1);
	TestFalse(TEXT("and no two-handed weapon is held any more"),
		Equipment->TwoHandedOccupiesBothWeaponSlots());

	// AND THE FREE HAND SAYS SO, which is the whole point of pinning this.
	const ECataclysmGearSlot Free =
		Equipment->EquippedAt(ECataclysmGearSlot::Weapon1) != nullptr
			? ECataclysmGearSlot::Weapon2 : ECataclysmGearSlot::Weapon1;
	TestFalse(TEXT("the hand emptied by the swap explains itself"),
		UCataclysmGearPanel::EmptyWeaponHandNote(Free, Equipment).IsEmpty());

	return true;
}

#endif // WITH_AUTOMATION_TESTS
