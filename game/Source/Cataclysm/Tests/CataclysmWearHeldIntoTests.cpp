// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Interface/CataclysmInventoryScreen.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmWearing.h"

/**
 * Tests for pressing a held item onto a gear slot, which wears it in that slot. Issue #837, slice 3.
 *
 * THE OWNER'S RULING, 2026-11-24, issue #837 comment 7150408605, "Approve all three (Recommended)": Q5, pressing a
 * held item onto a gear slot wears it in that slot, what was there going into the bag, so a weapon is swapped into
 * the hand the player chooses in one press and never through an empty hand; Q6, the rule that the last weapon cannot
 * be taken off stays, its refusal is shown on the inventory screen, and its sentence names the new press.
 *
 * EVERY TEST COUNTS WHAT THE CHARACTER HAS BEFORE AND AFTER, as the wearing tests beside these do and for their
 * reason: a step that loses an item leaves no other trace.
 *
 * NO WIDGET, NO ACTOR. The press itself, a point on a painted screen, is the controller's and is read out of its source
 * by `tools/tests/test_a_held_item_pressed_onto_a_gear_slot_is_worn_there.py`; what the screen shows is decided by
 * `UCataclysmInventoryScreen`'s plain functions, which are tested here.
 */
namespace CataclysmWearHeldIntoTest
{
	const TCHAR* HelmBase = TEXT("Head_Helm");
	const TCHAR* BootsBase = TEXT("Boots_Sabatons");
	const TCHAR* OneHandedBase = TEXT("Weapon_Sword");
	const TCHAR* OtherOneHandedBase = TEXT("Weapon_Dagger");
	const TCHAR* TwoHandedBase = TEXT("Weapon_Greatsword");
	const TCHAR* RingBase = TEXT("Ring_Band");

	/** A character's two components, with no actor, as the wearing tests build them. */
	struct FCarriedAndWorn
	{
		FCarriedAndWorn()
			: Inventory(NewObject<UCataclysmInventoryComponent>(GetTransientPackage()))
			, Equipment(NewObject<UCataclysmEquipmentComponent>(GetTransientPackage()))
		{
		}

		int32 Everything() const { return Inventory->NumItems() + Equipment->NumEquipped(); }

		/** Put an item on in a named slot, as a character already wearing it. */
		void WearIn(const FCataclysmItem& Item, ECataclysmGearSlot Slot)
		{
			FCataclysmItem First;
			FCataclysmItem Second;
			Equipment->EquipInto(Item, Slot, First, Second);
		}

		/** Carry an item and hold it on the cursor; the cell it is in. */
		int32 Hold(const FCataclysmItem& Item)
		{
			const int32 Cell = Inventory->AddItem(Item);
			Inventory->HoldSlot(Cell);
			return Cell;
		}

		/** The upgrade level of what is worn in a slot, or -1 when nothing is. */
		int32 LevelIn(ECataclysmGearSlot Slot) const
		{
			const FCataclysmItem* Worn = Equipment->EquippedAt(Slot);
			return Worn ? Worn->GearLevel : -1;
		}

		/** Whether the bag holds an item of this base at this upgrade level. */
		bool Carries(const TCHAR* Base, int32 GearLevel) const
		{
			for (int32 Cell = 0; Cell < UCataclysmInventoryComponent::SlotCount; ++Cell)
			{
				const FCataclysmItem* Item = Inventory->ItemAt(Cell);
				if (Item && Item->Base == FName(Base) && Item->GearLevel == GearLevel)
				{
					return true;
				}
			}
			return false;
		}

		UCataclysmInventoryComponent* Inventory = nullptr;
		UCataclysmEquipmentComponent* Equipment = nullptr;
	};

	/** An item of a base, told apart by its upgrade level, with a Worn Residue. */
	FCataclysmItem Of(const TCHAR* Base, int32 GearLevel = 0, float Residue = 0.0f)
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);
		Item.GearLevel = GearLevel;
		Item.Residue = Residue;
		return Item;
	}

	/** Real flat affixes `game/Data/Affixes.csv` allows on a ring, the ones the ring comparison's tests use. */
	const TCHAR* const RingAffixes[] = {TEXT("Stat_Flat_maximum_health"), TEXT("Stat_Flat_maximum_mana"),
										TEXT("Stat_Flat_health_regeneration"), TEXT("Stat_Flat_mana_regeneration")};

	/**
	 * A ring of a rarity from Everyday to Masterful: one to four of those affixes and no enchantment, which is what those
	 * four rarities are (`UCataclysmItemValues::RarityOf`), told apart by its upgrade level.
	 */
	FCataclysmItem RingOfRarity(ECataclysmRarity Rarity, int32 GearLevel)
	{
		FCataclysmItem Item = Of(RingBase, GearLevel);
		const int32 Affixes = FMath::Min(UCataclysmItemValues::AffixSlotsFor(Rarity), 4);
		for (int32 Index = 0; Index < Affixes; ++Index)
		{
			FCataclysmRolledAffix Rolled;
			Rolled.Affix = FName(RingAffixes[Index]);
			Rolled.Tier = UCataclysmItemValues::MaxAffixTier;
			Rolled.Roll = 1.0f;
			Item.Affixes.Add(Rolled);
		}
		return Item;
	}

	/** Press what is held onto a slot, at tier 1, not yet confirmed. */
	ECataclysmWearResult Press(FCarriedAndWorn& Character, ECataclysmGearSlot Slot,
							   ECataclysmGearSlot& OutWent)
	{
		return UCataclysmWearing::WearHeldInto(Character.Inventory, Character.Equipment, Slot, OutWent,
											   /*bConfirmedPastThreshold=*/false, /*DifficultyTier=*/1);
	}
}

// ---------------------------------------------------------------------------
// A weapon into the hand pressed
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearHeldIntoAFilledHand,
	"Cataclysm.Wearing.AWeaponPressedOntoAFilledHandIsWornThereAndTheOtherGoesToTheBag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearHeldIntoAFilledHand::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearHeldIntoTest;

	FCarriedAndWorn Character;
	Character.WearIn(Of(OneHandedBase, 1), ECataclysmGearSlot::Weapon1);
	Character.WearIn(Of(OtherOneHandedBase, 2), ECataclysmGearSlot::Weapon2);
	Character.Hold(Of(OneHandedBase, 7));
	const int32 Before = Character.Everything();

	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	const ECataclysmWearResult Result = Press(Character, ECataclysmGearSlot::Weapon1, Went);

	TestEqual(TEXT("the held sword goes on over the worn one"), Result, ECataclysmWearResult::Swapped);
	TestEqual(TEXT("in the hand pressed"), Went, ECataclysmGearSlot::Weapon1);
	TestEqual(TEXT("the first hand holds the held sword"), Character.LevelIn(ECataclysmGearSlot::Weapon1), 7);
	TestEqual(TEXT("the other hand still holds the dagger"), Character.LevelIn(ECataclysmGearSlot::Weapon2), 2);
	TestTrue(TEXT("and the sword that was worn is in the bag"), Character.Carries(OneHandedBase, 1));
	TestFalse(TEXT("and the held one is not"), Character.Carries(OneHandedBase, 7));
	TestEqual(TEXT("nothing was lost"), Character.Everything(), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearHeldIntoTheOtherHand,
	"Cataclysm.Wearing.PressedOntoTheOtherHandItReplacesWeaponTwo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearHeldIntoTheOtherHand::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearHeldIntoTest;

	FCarriedAndWorn Pressed;
	Pressed.WearIn(Of(OneHandedBase, 1), ECataclysmGearSlot::Weapon1);
	Pressed.WearIn(Of(OtherOneHandedBase, 2), ECataclysmGearSlot::Weapon2);
	Pressed.Hold(Of(OneHandedBase, 7));
	const int32 Before = Pressed.Everything();

	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	TestEqual(TEXT("pressed onto the second hand it goes on"), Press(Pressed, ECataclysmGearSlot::Weapon2, Went),
			  ECataclysmWearResult::Swapped);
	TestEqual(TEXT("in the second hand"), Went, ECataclysmGearSlot::Weapon2);
	TestEqual(TEXT("the second hand holds it"), Pressed.LevelIn(ECataclysmGearSlot::Weapon2), 7);
	TestEqual(TEXT("and the first hand still holds its sword"), Pressed.LevelIn(ECataclysmGearSlot::Weapon1), 1);
	TestTrue(TEXT("and the dagger is in the bag"), Pressed.Carries(OtherOneHandedBase, 2));
	TestEqual(TEXT("nothing was lost"), Pressed.Everything(), Before);

	// CONTROL: the right press, which names no hand, replaces the first. So the hand above is the press's choice and
	// not where a weapon would have gone anyway.
	FCarriedAndWorn Right;
	Right.WearIn(Of(OneHandedBase, 1), ECataclysmGearSlot::Weapon1);
	Right.WearIn(Of(OtherOneHandedBase, 2), ECataclysmGearSlot::Weapon2);
	const int32 Cell = Right.Inventory->AddItem(Of(OneHandedBase, 7));
	UCataclysmWearing::WearFromCarried(Right.Inventory, Right.Equipment, Cell, Went, false, 1);
	TestEqual(TEXT("CONTROL: the right press puts it in the first hand"), Went, ECataclysmGearSlot::Weapon1);
	TestEqual(TEXT("CONTROL: and the dagger stays in the second"), Right.LevelIn(ECataclysmGearSlot::Weapon2), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearHeldIntoTheOnlyWeaponsHand,
	"Cataclysm.Wearing.PressedOntoTheOnlyWeaponsHandItSwapsWithoutAnEmptyHand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearHeldIntoTheOnlyWeaponsHand::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearHeldIntoTest;

	FCarriedAndWorn Character;
	Character.WearIn(Of(OneHandedBase, 1), ECataclysmGearSlot::Weapon1);
	Character.Hold(Of(OtherOneHandedBase, 5));
	const int32 Before = Character.Everything();

	// CONTROL: the only weapon cannot be taken off, the rule the owner kept (Q6).
	TestEqual(TEXT("CONTROL: the only weapon still cannot be taken off"),
			  UCataclysmWearing::TakeOffInto(Character.Inventory, Character.Equipment, ECataclysmGearSlot::Weapon1),
			  ECataclysmWearResult::TheLastWeapon);

	// EVERY ANNOUNCEMENT IS COUNTED, with the weapons worn when it was made: the pawn fills the skill keys from what
	// it hears, and a fill that saw no weapon would empty them.
	TArray<int32> WeaponsAtEachAnnouncement;
	UCataclysmEquipmentComponent* Equipment = Character.Equipment;
	const FDelegateHandle Listening = Equipment->EquipmentChanged.AddLambda([&WeaponsAtEachAnnouncement, Equipment]()
	{
		WeaponsAtEachAnnouncement.Add(
			(Equipment->SlotIsEmpty(ECataclysmGearSlot::Weapon1) ? 0 : 1)
			+ (Equipment->SlotIsEmpty(ECataclysmGearSlot::Weapon2) ? 0 : 1));
	});

	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	const ECataclysmWearResult Result = Press(Character, ECataclysmGearSlot::Weapon1, Went);
	Equipment->EquipmentChanged.Remove(Listening);

	TestEqual(TEXT("the held dagger goes on over the only weapon"), Result, ECataclysmWearResult::Swapped);
	TestEqual(TEXT("in the first hand"), Character.LevelIn(ECataclysmGearSlot::Weapon1), 5);
	TestTrue(TEXT("and the sword is in the bag"), Character.Carries(OneHandedBase, 1));
	TestEqual(TEXT("one announcement"), WeaponsAtEachAnnouncement.Num(), 1);
	TestTrue(TEXT("made with a weapon in hand"),
			 WeaponsAtEachAnnouncement.Num() == 1 && WeaponsAtEachAnnouncement[0] == 1);
	TestEqual(TEXT("nothing was lost"), Character.Everything(), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearHeldIntoAnEmptyHand,
	"Cataclysm.Wearing.PressedOntoAnEmptyHandItIsWornThere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearHeldIntoAnEmptyHand::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearHeldIntoTest;

	FCarriedAndWorn Character;
	Character.WearIn(Of(OneHandedBase, 1), ECataclysmGearSlot::Weapon1);
	Character.Hold(Of(OtherOneHandedBase, 5));
	const int32 Before = Character.Everything();

	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	TestEqual(TEXT("the held dagger goes into the empty hand"), Press(Character, ECataclysmGearSlot::Weapon2, Went),
			  ECataclysmWearResult::Worn);
	TestEqual(TEXT("the second hand"), Went, ECataclysmGearSlot::Weapon2);
	TestEqual(TEXT("which holds it"), Character.LevelIn(ECataclysmGearSlot::Weapon2), 5);
	TestEqual(TEXT("and the first hand still holds the sword"), Character.LevelIn(ECataclysmGearSlot::Weapon1), 1);
	TestEqual(TEXT("the bag is empty"), Character.Inventory->NumItems(), 0);
	TestEqual(TEXT("nothing was lost"), Character.Everything(), Before);
	return true;
}

// ---------------------------------------------------------------------------
// Two-handed weapons
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearHeldIntoTwoHanded,
	"Cataclysm.Wearing.ATwoHandedWeaponPressedOntoEitherHandTakesBoth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearHeldIntoTwoHanded::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearHeldIntoTest;

	for (const ECataclysmGearSlot Hand : {ECataclysmGearSlot::Weapon1, ECataclysmGearSlot::Weapon2})
	{
		const FString Which = UCataclysmGearSlots::DisplayName(Hand);
		FCarriedAndWorn Character;
		Character.WearIn(Of(OneHandedBase, 1), ECataclysmGearSlot::Weapon1);
		Character.WearIn(Of(OtherOneHandedBase, 2), ECataclysmGearSlot::Weapon2);
		Character.Hold(Of(TwoHandedBase, 9));
		const int32 Before = Character.Everything();

		ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
		TestEqual(FString::Printf(TEXT("%s: the greatsword goes on"), *Which), Press(Character, Hand, Went),
				  ECataclysmWearResult::Swapped);
		TestEqual(FString::Printf(TEXT("%s: and is in the first hand, where a two-handed weapon is kept"), *Which),
				  Went, ECataclysmGearSlot::Weapon1);
		TestEqual(FString::Printf(TEXT("%s: the first hand holds it"), *Which),
				  Character.LevelIn(ECataclysmGearSlot::Weapon1), 9);
		TestTrue(FString::Printf(TEXT("%s: the second hand is empty"), *Which),
				 Character.Equipment->SlotIsEmpty(ECataclysmGearSlot::Weapon2));
		TestTrue(FString::Printf(TEXT("%s: the sword is in the bag"), *Which), Character.Carries(OneHandedBase, 1));
		TestTrue(FString::Printf(TEXT("%s: and so is the dagger"), *Which), Character.Carries(OtherOneHandedBase, 2));
		TestEqual(FString::Printf(TEXT("%s: nothing was lost"), *Which), Character.Everything(), Before);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearHeldIntoNoRoom,
	"Cataclysm.Wearing.ATwoHandedWeaponWithNoRoomForTheSecondIsRefusedAndNothingMoves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearHeldIntoNoRoom::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearHeldIntoTest;

	// TWO WEAPONS COME OFF FOR ONE GOING ON, and the cell the greatsword leaves makes room for one. With every other
	// cell full there is nowhere for the second.
	for (const bool bOneCellFree : {false, true})
	{
		const TCHAR* Case = bOneCellFree ? TEXT("CONTROL, one cell free") : TEXT("a full bag");
		FCarriedAndWorn Character;
		Character.WearIn(Of(OneHandedBase, 1), ECataclysmGearSlot::Weapon1);
		Character.WearIn(Of(OtherOneHandedBase, 2), ECataclysmGearSlot::Weapon2);
		const int32 Cell = Character.Hold(Of(TwoHandedBase, 9));
		while (Character.Inventory->NumFreeSlots() > (bOneCellFree ? 1 : 0))
		{
			Character.Inventory->AddItem(Of(BootsBase));
		}
		const int32 Before = Character.Everything();

		ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
		const ECataclysmWearResult Result = Press(Character, ECataclysmGearSlot::Weapon2, Went);
		if (bOneCellFree)
		{
			TestEqual(FString::Printf(TEXT("%s: it goes on"), Case), Result, ECataclysmWearResult::Swapped);
		}
		else
		{
			TestEqual(FString::Printf(TEXT("%s: it is refused"), Case), Result, ECataclysmWearResult::NoRoomInTheBag);
			TestEqual(FString::Printf(TEXT("%s: the first hand still holds the sword"), Case),
					  Character.LevelIn(ECataclysmGearSlot::Weapon1), 1);
			TestEqual(FString::Printf(TEXT("%s: the second still holds the dagger"), Case),
					  Character.LevelIn(ECataclysmGearSlot::Weapon2), 2);
			TestEqual(FString::Printf(TEXT("%s: the greatsword is still held, in its cell"), Case),
					  Character.Inventory->HeldSlot(), Cell);
		}
		TestEqual(FString::Printf(TEXT("%s: nothing was lost"), Case), Character.Everything(), Before);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearHeldIntoATwoHandersHand,
	"Cataclysm.Wearing.AOneHanderPressedOntoATwoHandersHandTakesItOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearHeldIntoATwoHandersHand::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearHeldIntoTest;

	// THE GREATSWORD IS KEPT IN THE FIRST HAND AND FILLS BOTH, so either hand pressed takes it off, and the sword goes
	// in the hand pressed.
	for (const ECataclysmGearSlot Hand : {ECataclysmGearSlot::Weapon1, ECataclysmGearSlot::Weapon2})
	{
		const FString Which = UCataclysmGearSlots::DisplayName(Hand);
		FCarriedAndWorn Character;
		Character.WearIn(Of(TwoHandedBase, 9), ECataclysmGearSlot::Weapon1);
		Character.Hold(Of(OneHandedBase, 3));
		const int32 Before = Character.Everything();

		ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
		TestEqual(FString::Printf(TEXT("%s: the sword goes on"), *Which), Press(Character, Hand, Went),
				  ECataclysmWearResult::Swapped);
		TestEqual(FString::Printf(TEXT("%s: in the hand pressed"), *Which), Went, Hand);
		TestEqual(FString::Printf(TEXT("%s: which holds it"), *Which), Character.LevelIn(Hand), 3);
		TestEqual(FString::Printf(TEXT("%s: one weapon is worn"), *Which),
				  Character.Equipment->NumEquipped(), 1);
		TestTrue(FString::Printf(TEXT("%s: and the greatsword is in the bag"), *Which),
				 Character.Carries(TwoHandedBase, 9));
		TestEqual(FString::Printf(TEXT("%s: nothing was lost"), *Which), Character.Everything(), Before);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Other slots, and what is refused
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearHeldIntoTheWrongSlot,
	"Cataclysm.Wearing.AnItemPressedOntoASlotItCannotGoInIsRefusedAndStaysHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearHeldIntoTheWrongSlot::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearHeldIntoTest;

	// THE HELM IS IN THE SECOND CELL WITH THE FIRST FREE, so a helm taken out and put back would land in the first
	// cell: staying in its own cell is what shows it never moved.
	FCarriedAndWorn Character;
	const int32 Filler = Character.Inventory->AddItem(Of(BootsBase, 1));
	const int32 Cell = Character.Hold(Of(HelmBase, 4));
	Character.Inventory->RemoveItemAt(Filler);
	Character.Inventory->HoldSlot(Cell);
	const int32 Before = Character.Everything();

	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	TestEqual(TEXT("a helm pressed onto the boots' slot is refused"), Press(Character, ECataclysmGearSlot::Boots, Went),
			  ECataclysmWearResult::CannotBeWorn);
	TestEqual(TEXT("and onto a hand"), Press(Character, ECataclysmGearSlot::Weapon1, Went),
			  ECataclysmWearResult::CannotBeWorn);
	TestEqual(TEXT("nothing is worn"), Character.Equipment->NumEquipped(), 0);
	TestEqual(TEXT("the helm is still held"), Character.Inventory->HeldSlot(), Cell);
	const FCataclysmItem* Still = Character.Inventory->ItemAt(Cell);
	TestTrue(TEXT("in its own cell"), Still && Still->GearLevel == 4);
	TestEqual(TEXT("nothing was lost"), Character.Everything(), Before);

	// CONTROL: the same helm pressed onto the head goes on, so the refusals above were the slot's.
	TestEqual(TEXT("CONTROL: pressed onto the head it goes on"), Press(Character, ECataclysmGearSlot::Head, Went),
			  ECataclysmWearResult::Worn);
	TestEqual(TEXT("CONTROL: on the head"), Character.LevelIn(ECataclysmGearSlot::Head), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearHeldIntoRingFive,
	"Cataclysm.Wearing.ARingPressedOntoRingFiveGoesInRingFive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearHeldIntoRingFive::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearHeldIntoTest;

	FCarriedAndWorn Character;
	Character.WearIn(Of(RingBase, 1), ECataclysmGearSlot::Ring1);
	Character.Hold(Of(RingBase, 5));
	const int32 Before = Character.Everything();

	// CONTROL: where the right press would put it, read without wearing anything.
	const FCataclysmEquipPreview RightPress = Character.Equipment->PreviewEquip(Of(RingBase, 5));
	TestEqual(TEXT("CONTROL: the right press would put it in the first free ring slot"), RightPress.Slot,
			  ECataclysmGearSlot::Ring2);

	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	TestEqual(TEXT("pressed onto the fifth ring slot it goes on"), Press(Character, ECataclysmGearSlot::Ring5, Went),
			  ECataclysmWearResult::Worn);
	TestEqual(TEXT("in the fifth"), Went, ECataclysmGearSlot::Ring5);
	TestEqual(TEXT("which holds it"), Character.LevelIn(ECataclysmGearSlot::Ring5), 5);
	TestTrue(TEXT("and the second is still empty"), Character.Equipment->SlotIsEmpty(ECataclysmGearSlot::Ring2));
	TestEqual(TEXT("and the first still holds its ring"), Character.LevelIn(ECataclysmGearSlot::Ring1), 1);
	TestEqual(TEXT("nothing was lost"), Character.Everything(), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearHeldIntoOverridesTheRingEquipWouldReplace,
	"Cataclysm.Wearing.APressedRingSlotOverridesTheRingEquipWouldReplace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearHeldIntoOverridesTheRingEquipWouldReplace::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearHeldIntoTest;

	// EIGHT RINGS WORN, the owner's ring report's case: a Masterful ring in Ring 1, the one Quality ring in Ring 3, and
	// Everyday rings in the rest. With every ring slot full, `Equip` gives a Quality ring the Quality ring's slot
	// (`UCataclysmGearSlots::SlotToReplace`, fix/ring-comparison). A PRESS NAMES ITS OWN SLOT, and that rule is only
	// `Equip`'s: the coordinating session's ruling of 2026-11-24, "a pressed slot overrides SlotToReplace's choice".
	FCarriedAndWorn Character;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const ECataclysmRarity Rarity = Index == 0 ? ECataclysmRarity::Masterful
			: Index == 2 ? ECataclysmRarity::Quality : ECataclysmRarity::Everyday;
		Character.WearIn(RingOfRarity(Rarity, /*GearLevel=*/Index + 1),
						 static_cast<ECataclysmGearSlot>(static_cast<int32>(ECataclysmGearSlot::Ring1) + Index));
	}
	const FCataclysmItem Held = RingOfRarity(ECataclysmRarity::Quality, /*GearLevel=*/9);
	Character.Hold(Held);
	const int32 Before = Character.Everything();

	// CONTROL: what `Equip` would choose, read without wearing anything: the Quality ring's slot.
	TestEqual(TEXT("CONTROL: the right press would replace the Quality ring in Ring 3"),
			  Character.Equipment->PreviewEquip(Held).Slot, ECataclysmGearSlot::Ring3);

	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	TestEqual(TEXT("pressed onto Ring 1 it goes on"), Press(Character, ECataclysmGearSlot::Ring1, Went),
			  ECataclysmWearResult::Swapped);
	TestEqual(TEXT("in Ring 1"), Went, ECataclysmGearSlot::Ring1);
	TestEqual(TEXT("which holds it"), Character.LevelIn(ECataclysmGearSlot::Ring1), 9);
	TestEqual(TEXT("and Ring 3 still holds the Quality ring"), Character.LevelIn(ECataclysmGearSlot::Ring3), 3);
	TestTrue(TEXT("and the Masterful ring is in the bag"), Character.Carries(RingBase, 1));
	TestEqual(TEXT("nothing was lost"), Character.Everything(), Before);
	return true;
}

// ---------------------------------------------------------------------------
// The Consumption Threshold, and the hold
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearHeldIntoPastTheThreshold,
	"Cataclysm.Wearing.PressedPastTheConsumptionThresholdItAsksFirst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearHeldIntoPastTheThreshold::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearHeldIntoTest;

	// A helm of 1000 worn and boots of 1000 held: 2000 once both are on, past tier 1's 1950.
	FCarriedAndWorn Character;
	Character.WearIn(Of(HelmBase, 0, 1000.0f), ECataclysmGearSlot::Head);
	const FCataclysmItem Boots = Of(BootsBase, 6, 1000.0f);
	const int32 Cell = Character.Hold(Boots);
	const int32 Before = Character.Everything();

	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	TestEqual(TEXT("the press is asked about first"), Press(Character, ECataclysmGearSlot::Boots, Went),
			  ECataclysmWearResult::WouldCrossConsumptionThreshold);
	TestTrue(TEXT("and nothing is on the feet"), Character.Equipment->SlotIsEmpty(ECataclysmGearSlot::Boots));
	TestEqual(TEXT("the boots are still held"), Character.Inventory->HeldSlot(), Cell);
	TestEqual(TEXT("nothing was lost"), Character.Everything(), Before);

	// THE YES PUTS THEM IN THE SLOT PRESSED, which is the slot the question was about.
	TestEqual(TEXT("a yes puts them on"),
			  UCataclysmWearing::WearAfterConfirming(Character.Inventory, Character.Equipment, Cell, Boots, Went, 1,
													 ECataclysmGearSlot::Boots),
			  ECataclysmWearResult::Worn);
	TestEqual(TEXT("on the feet"), Character.LevelIn(ECataclysmGearSlot::Boots), 6);
	TestEqual(TEXT("the Worn Residue is then 2000"), Character.Equipment->WornResidue(), 2000.0f);
	TestEqual(TEXT("and still nothing was lost"), Character.Everything(), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearHeldIntoTheQuestionIsAboutTheSlot,
	"Cataclysm.Wearing.TheThresholdQuestionIsAboutTheSlotPressed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearHeldIntoTheQuestionIsAboutTheSlot::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearHeldIntoTest;

	// A RING OF 1500 WORN IN THE FIRST RING SLOT AND ONE OF 600 HELD. Into a free ring slot the total is 2100, past
	// tier 1's 1950; over the first ring it is 600.
	FCarriedAndWorn Character;
	Character.WearIn(Of(RingBase, 1, 1500.0f), ECataclysmGearSlot::Ring1);
	const int32 Cell = Character.Hold(Of(RingBase, 2, 600.0f));

	const FCataclysmThresholdCrossing OverTheFirst = UCataclysmWearing::CrossingFor(
		Character.Inventory, Character.Equipment, Cell, 1, ECataclysmGearSlot::Ring1);
	const FCataclysmThresholdCrossing AnywhereFree =
		UCataclysmWearing::CrossingFor(Character.Inventory, Character.Equipment, Cell, 1);
	TestEqual(TEXT("over the first ring the total would be 600"), OverTheFirst.After, 600.0f);
	TestFalse(TEXT("which crosses nothing"), OverTheFirst.Crosses());
	TestEqual(TEXT("CONTROL: in the first free ring slot it would be 2100"), AnywhereFree.After, 2100.0f);
	TestTrue(TEXT("CONTROL: which crosses"), AnywhereFree.Crosses());

	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	TestEqual(TEXT("so the press onto the first ring is not asked about"),
			  Press(Character, ECataclysmGearSlot::Ring1, Went), ECataclysmWearResult::Swapped);
	TestEqual(TEXT("and the Worn Residue is 600"), Character.Equipment->WornResidue(), 600.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearHeldIntoEndsTheHold,
	"Cataclysm.Wearing.WearingTheHeldItemEndsTheHold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearHeldIntoEndsTheHold::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearHeldIntoTest;

	// WHAT CAME OFF LANDS IN THE CELL THE HELD HELM LEFT, the first free one, so a hold left in place would put the old
	// helm on the cursor.
	FCarriedAndWorn Character;
	Character.WearIn(Of(HelmBase, 1), ECataclysmGearSlot::Head);
	const int32 Cell = Character.Hold(Of(HelmBase, 4));

	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	TestEqual(TEXT("the held helm goes on"), Press(Character, ECataclysmGearSlot::Head, Went),
			  ECataclysmWearResult::Swapped);
	const FCataclysmItem* Landed = Character.Inventory->ItemAt(Cell);
	TestTrue(TEXT("the old helm is in the cell the held one left"), Landed && Landed->GearLevel == 1);
	TestFalse(TEXT("and nothing is held"), Character.Inventory->IsHolding());

	// CONTROL: with nothing held there is nothing to wear, and it is said so.
	TestEqual(TEXT("CONTROL: a press with nothing held is refused"),
			  Press(Character, ECataclysmGearSlot::Head, Went), ECataclysmWearResult::NothingHeld);
	TestEqual(TEXT("CONTROL: and the new helm is still worn"), Character.LevelIn(ECataclysmGearSlot::Head), 4);
	return true;
}

// ---------------------------------------------------------------------------
// What the screen says
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearHeldIntoTheLastWeaponsWords,
	"Cataclysm.Wearing.TheLastWeaponRefusalNamesThePress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearHeldIntoTheLastWeaponsWords::RunTest(const FString& Parameters)
{
	// THE OWNER'S Q6, word for word: the plan's example sentence, approved with it.
	TestEqual(TEXT("the last weapon's refusal names the press that swaps it"),
			  UCataclysmWearing::Explain(ECataclysmWearResult::TheLastWeapon),
			  FString(TEXT("That is your only weapon, and you have to hold one. "
						   "Press another weapon onto this hand to swap them.")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearHeldIntoTheScreenSaysSo,
	"Cataclysm.Wearing.ARefusedPressIsShownOnTheScreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearHeldIntoTheScreenSaysSo::RunTest(const FString& Parameters)
{
	using FScreen = UCataclysmInventoryScreen;

	for (const ECataclysmWearResult Refused : {ECataclysmWearResult::TheLastWeapon, ECataclysmWearResult::NoRoomInTheBag,
											   ECataclysmWearResult::CannotBeWorn, ECataclysmWearResult::AlreadyHolding})
	{
		TestEqual(FString::Printf(TEXT("a refusal shows its sentence: %s"), *UCataclysmWearing::Explain(Refused)),
				  FScreen::PressLineFor(Refused), UCataclysmWearing::Explain(Refused));
	}

	// CONTROL: a press that did what was asked shows nothing, so the line is not simply every sentence.
	for (const ECataclysmWearResult Done : {ECataclysmWearResult::Worn, ECataclysmWearResult::Swapped,
											ECataclysmWearResult::TakenOff, ECataclysmWearResult::PickedUp,
											ECataclysmWearResult::PutDown, ECataclysmWearResult::Exchanged})
	{
		TestTrue(FString::Printf(TEXT("CONTROL: a press that did what was asked shows nothing: %s"),
								 *UCataclysmWearing::Explain(Done)),
				 FScreen::PressLineFor(Done).IsEmpty());
	}

	// SHOWN FOR ITS SECONDS AND THEN GONE, and an empty line never.
	const FString Line = FScreen::PressLineFor(ECataclysmWearResult::TheLastWeapon);
	TestTrue(TEXT("the line is shown when it is posted"), FScreen::PressLineIsShown(Line, 10.0, 10.0));
	TestTrue(TEXT("and just before its seconds are up"),
			 FScreen::PressLineIsShown(Line, 10.0, 10.0 + FScreen::PressLineSeconds - 0.01));
	TestFalse(TEXT("and not after"), FScreen::PressLineIsShown(Line, 10.0, 10.0 + FScreen::PressLineSeconds + 0.01));
	TestFalse(TEXT("an empty line is never shown"), FScreen::PressLineIsShown(FString(), 10.0, 10.0));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
