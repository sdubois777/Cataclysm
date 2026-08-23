// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmWearing.h"

/**
 * Tests for moving an item between the bag and what is worn. Issues #828, #831.
 *
 * **THE ONE THING WORTH GUARDING IS THAT NOTHING IS EVER DESTROYED**, and it is
 * the reason this rule is a class of its own rather than living inside the
 * console command that first needed it. An item is either worn or carried; a
 * step that drops one leaves no trace, throws no error and fails no other test.
 * The player simply does not have a helmet any more.
 *
 * So every test below counts what the character has before and after, and the
 * count has to match. That is a stronger check than looking at the slot the
 * change was aimed at, because it also catches an item lost from somewhere else.
 *
 * THE INTERESTING CASE IS A TWO-HANDED WEAPON ON A FULL BAG. One item goes on
 * and two come off, so the slot the weapon vacated is not enough room. That is
 * the only case that can be refused for want of space, and a refusal has to
 * change nothing at all rather than half-happen.
 *
 * NOTHING HERE NEEDS A WIDGET OR A RENDERER, which is the point of the rule
 * living outside the screen: the automation command in `tools/unreal_build.py`
 * passes `-nullrhi` and no test in this project can watch a widget draw.
 */
namespace CataclysmWearingTest
{
	const TCHAR* HelmBase = TEXT("Head_Helm");
	const TCHAR* BootsBase = TEXT("Boots_Sabatons");
	const TCHAR* OneHandedBase = TEXT("Weapon_Sword");
	const TCHAR* OtherOneHandedBase = TEXT("Weapon_Dagger");
	const TCHAR* TwoHandedBase = TEXT("Weapon_Greatsword");

	/** A character's two components, with no actor. Neither needs a world. */
	struct FCarriedAndWorn
	{
		FCarriedAndWorn()
			: Inventory(NewObject<UCataclysmInventoryComponent>(GetTransientPackage()))
			, Equipment(NewObject<UCataclysmEquipmentComponent>(GetTransientPackage()))
		{
		}

		/**
		 * How many items the character has anywhere.
		 *
		 * WORN PLUS CARRIED, and it is the number every test here checks. A
		 * change that loses an item shows up as this falling, whichever end it
		 * was lost from.
		 */
		int32 Everything() const
		{
			return Inventory->NumItems() + Equipment->NumEquipped();
		}

		UCataclysmInventoryComponent* Inventory = nullptr;
		UCataclysmEquipmentComponent* Equipment = nullptr;
	};

	FCataclysmItem Of(const TCHAR* Base, int32 GearLevel = 0)
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);
		Item.GearLevel = GearLevel;
		return Item;
	}

	/** Fills every free carried slot, so the bag is exactly full. */
	void FillTheBag(FCarriedAndWorn& Character)
	{
		while (!Character.Inventory->IsFull())
		{
			Character.Inventory->AddItem(Of(BootsBase));
		}
	}
}

// ---------------------------------------------------------------------------
// Putting something on
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearingMovesFromBagToBody,
	"Cataclysm.Wearing.AnItemLeavesTheBagAndIsWorn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearingMovesFromBagToBody::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearingTest;

	FCarriedAndWorn Character;
	const int32 Slot = Character.Inventory->AddItem(Of(HelmBase, 4));
	const int32 Before = Character.Everything();

	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	const ECataclysmWearResult Result = UCataclysmWearing::WearFromCarried(
		Character.Inventory, Character.Equipment, Slot, Went);

	TestTrue(TEXT("the helm goes on"), Result == ECataclysmWearResult::Worn);
	TestTrue(TEXT("and it went on the head"), Went == ECataclysmGearSlot::Head);
	TestEqual(TEXT("the character still has exactly one item"),
		Character.Everything(), Before);
	TestEqual(TEXT("the bag is empty"), Character.Inventory->NumItems(), 0);

	const FCataclysmItem* Worn =
		Character.Equipment->EquippedAt(ECataclysmGearSlot::Head);
	if (TestNotNull(TEXT("the head slot holds something"), Worn))
	{
		TestEqual(TEXT("and it is the same helm, upgrade level and all"),
			Worn->GearLevel, 4);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearingSwapsIntoTheBag,
	"Cataclysm.Wearing.WhatComesOffGoesIntoTheBagEvenWhenItWasFull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearingSwapsIntoTheBag::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearingTest;

	FCarriedAndWorn Character;

	// Wear one helm, then fill the bag completely and try to swap it for
	// another. A ONE-FOR-ONE SWAP ALWAYS FITS, because the slot the new item
	// came out of is free by the time the old one needs somewhere to go. A rule
	// that simply refused on a full bag would refuse this, which is the common
	// case rather than the awkward one.
	const int32 First = Character.Inventory->AddItem(Of(HelmBase, 1));
	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	UCataclysmWearing::WearFromCarried(Character.Inventory, Character.Equipment,
									   First, Went);

	Character.Inventory->AddItem(Of(HelmBase, 9));
	FillTheBag(Character);
	TestTrue(TEXT("the bag is full"), Character.Inventory->IsFull());

	const int32 Before = Character.Everything();
	const int32 Second = 0;   // the second helm went into the first free slot

	const ECataclysmWearResult Result = UCataclysmWearing::WearFromCarried(
		Character.Inventory, Character.Equipment, Second, Went);

	TestTrue(TEXT("the swap happens even with a full bag"),
		Result == ECataclysmWearResult::Swapped);
	TestEqual(TEXT("and nothing was destroyed"),
		Character.Everything(), Before);

	const FCataclysmItem* Worn =
		Character.Equipment->EquippedAt(ECataclysmGearSlot::Head);
	if (TestNotNull(TEXT("a helm is worn"), Worn))
	{
		TestEqual(TEXT("and it is the new one"), Worn->GearLevel, 9);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearingRefusesRatherThanLosing,
	"Cataclysm.Wearing.ATwoHandedWeaponOnAFullBagIsRefusedAndNothingMoves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearingRefusesRatherThanLosing::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearingTest;

	// THE ONLY CASE THAT CAN BE REFUSED FOR WANT OF ROOM. One item goes on and
	// two come off, so the slot the weapon vacated is one short. Without this
	// check the second weapon would have nowhere to go and would be destroyed,
	// and nothing else in the project would notice.
	FCarriedAndWorn Character;

	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	const int32 Sword = Character.Inventory->AddItem(Of(OneHandedBase));
	UCataclysmWearing::WearFromCarried(Character.Inventory, Character.Equipment,
									   Sword, Went);
	const int32 Dagger = Character.Inventory->AddItem(Of(OtherOneHandedBase));
	UCataclysmWearing::WearFromCarried(Character.Inventory, Character.Equipment,
									   Dagger, Went);
	TestEqual(TEXT("both hands are full"), Character.Equipment->NumEquipped(), 2);

	const int32 Greatsword = Character.Inventory->AddItem(Of(TwoHandedBase));
	FillTheBag(Character);

	const int32 Before = Character.Everything();
	const int32 WornBefore = Character.Equipment->NumEquipped();

	const ECataclysmWearResult Result = UCataclysmWearing::WearFromCarried(
		Character.Inventory, Character.Equipment, Greatsword, Went);

	TestTrue(TEXT("it is refused"),
		Result == ECataclysmWearResult::NoRoomInTheBag);
	TestEqual(TEXT("and nothing was destroyed"),
		Character.Everything(), Before);
	TestEqual(TEXT("and nothing was even moved"),
		Character.Equipment->NumEquipped(), WornBefore);

	const FCataclysmItem* StillCarried = Character.Inventory->ItemAt(Greatsword);
	if (TestNotNull(TEXT("the greatsword is still in the bag"), StillCarried))
	{
		TestEqual(TEXT("in the slot it was in"),
			StillCarried->Base, FName(TwoHandedBase));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearingTwoHandedWithRoom,
	"Cataclysm.Wearing.ATwoHandedWeaponWithRoomHandsBackBothWeapons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearingTwoHandedWithRoom::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearingTest;

	FCarriedAndWorn Character;

	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	const int32 Sword = Character.Inventory->AddItem(Of(OneHandedBase));
	UCataclysmWearing::WearFromCarried(Character.Inventory, Character.Equipment,
									   Sword, Went);
	const int32 Dagger = Character.Inventory->AddItem(Of(OtherOneHandedBase));
	UCataclysmWearing::WearFromCarried(Character.Inventory, Character.Equipment,
									   Dagger, Went);

	const int32 Greatsword = Character.Inventory->AddItem(Of(TwoHandedBase));
	const int32 Before = Character.Everything();

	const ECataclysmWearResult Result = UCataclysmWearing::WearFromCarried(
		Character.Inventory, Character.Equipment, Greatsword, Went);

	TestTrue(TEXT("the greatsword goes on"),
		Result == ECataclysmWearResult::Swapped);
	TestEqual(TEXT("and nothing was destroyed"),
		Character.Everything(), Before);
	TestEqual(TEXT("one weapon is worn"), Character.Equipment->NumEquipped(), 1);
	TestEqual(TEXT("and both the others are carried"),
		Character.Inventory->NumItems(), 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearingRefusesWhatCannotBeWorn,
	"Cataclysm.Wearing.SomethingThatFitsNoSlotStaysInTheBag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearingRefusesWhatCannotBeWorn::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearingTest;

	FCarriedAndWorn Character;

	// AN ITEM WHOSE BASE IS NOT IN THE TABLE CANNOT BE JUDGED, so it is refused
	// -- and the point of the test is that it goes back in the bag rather than
	// being taken out and dropped on the floor of a function.
	const int32 Slot = Character.Inventory->AddItem(Of(TEXT("NotARealBase")));
	const int32 Before = Character.Everything();

	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	const ECataclysmWearResult Result = UCataclysmWearing::WearFromCarried(
		Character.Inventory, Character.Equipment, Slot, Went);

	TestTrue(TEXT("it cannot be worn"),
		Result == ECataclysmWearResult::CannotBeWorn);
	TestEqual(TEXT("and it was not destroyed"),
		Character.Everything(), Before);
	TestEqual(TEXT("it is still carried"), Character.Inventory->NumItems(), 1);
	TestEqual(TEXT("and nothing is worn"), Character.Equipment->NumEquipped(), 0);

	// AN EMPTY SLOT AND A MATERIAL BOTH ANSWER "NOTHING TO WEAR". ItemAt gives
	// nothing for a material rather than an item with no base, so both arrive at
	// the same answer and neither disturbs anything.
	Character.Inventory->AddMaterial(FName(TEXT("Material_Aetherial_Shard")), 3);
	const int32 WithMaterial = Character.Everything();

	TestTrue(TEXT("an empty slot holds nothing to wear"),
		UCataclysmWearing::WearFromCarried(Character.Inventory,
										   Character.Equipment, 40, Went)
			== ECataclysmWearResult::NothingToWear);
	TestTrue(TEXT("and neither does a stack of materials"),
		UCataclysmWearing::WearFromCarried(Character.Inventory,
										   Character.Equipment, 1, Went)
			== ECataclysmWearResult::NothingToWear);
	TestEqual(TEXT("and neither disturbed anything"),
		Character.Everything(), WithMaterial);

	return true;
}

// ---------------------------------------------------------------------------
// Taking something off
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearingTakesOffIntoTheBag,
	"Cataclysm.Wearing.TakingSomethingOffPutsItInTheBag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearingTakesOffIntoTheBag::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearingTest;

	FCarriedAndWorn Character;

	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	const int32 Slot = Character.Inventory->AddItem(Of(HelmBase, 3));
	UCataclysmWearing::WearFromCarried(Character.Inventory, Character.Equipment,
									   Slot, Went);
	const int32 Before = Character.Everything();

	const ECataclysmWearResult Result = UCataclysmWearing::TakeOffInto(
		Character.Inventory, Character.Equipment, ECataclysmGearSlot::Head);

	TestTrue(TEXT("it comes off"), Result == ECataclysmWearResult::TakenOff);
	TestEqual(TEXT("and nothing was destroyed"),
		Character.Everything(), Before);
	TestEqual(TEXT("it is carried"), Character.Inventory->NumItems(), 1);
	TestEqual(TEXT("and nothing is worn"), Character.Equipment->NumEquipped(), 0);

	// AN EMPTY SLOT HAS NOTHING TO TAKE OFF, and answers so rather than putting
	// an item with no base in the bag.
	TestTrue(TEXT("an empty gear slot has nothing to take off"),
		UCataclysmWearing::TakeOffInto(Character.Inventory, Character.Equipment,
									   ECataclysmGearSlot::Head)
			== ECataclysmWearResult::NothingWorn);
	TestEqual(TEXT("and asking did not add a phantom to the bag"),
		Character.Inventory->NumItems(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearingKeepsItOnWhenTheBagIsFull,
	"Cataclysm.Wearing.TakingSomethingOffIsRefusedWhenTheBagIsFull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearingKeepsItOnWhenTheBagIsFull::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearingTest;

	// THERE IS NO FLOOR TO DROP IT ON FROM A SCREEN. Taking the piece off first
	// and then finding nowhere to put it would leave it in a local variable
	// about to go out of scope, so the room is asked for before anything moves.
	FCarriedAndWorn Character;

	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	const int32 Slot = Character.Inventory->AddItem(Of(HelmBase));
	UCataclysmWearing::WearFromCarried(Character.Inventory, Character.Equipment,
									   Slot, Went);
	FillTheBag(Character);

	const int32 Before = Character.Everything();

	const ECataclysmWearResult Result = UCataclysmWearing::TakeOffInto(
		Character.Inventory, Character.Equipment, ECataclysmGearSlot::Head);

	TestTrue(TEXT("it is refused"),
		Result == ECataclysmWearResult::NoRoomInTheBag);
	TestEqual(TEXT("and nothing was destroyed"),
		Character.Everything(), Before);
	TestNotNull(TEXT("the helm is still worn"),
		Character.Equipment->EquippedAt(ECataclysmGearSlot::Head));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearingExplainsItself,
	"Cataclysm.Wearing.EveryOutcomeCanBeExplained",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearingExplainsItself::RunTest(const FString& Parameters)
{
	// THE CONSOLE COMMAND AND THE SCREEN BOTH SHOW THIS, so an outcome added
	// later with no sentence behind it would print nothing at all and read as
	// the command having done nothing.
	const UEnum* Enum = StaticEnum<ECataclysmWearResult>();
	if (!TestNotNull(TEXT("the outcome enum exists"), Enum))
	{
		return false;
	}

	// NumEnums includes the hidden _MAX entry the engine appends, so the last
	// one is skipped.
	for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
	{
		const ECataclysmWearResult Result =
			static_cast<ECataclysmWearResult>(Enum->GetValueByIndex(Index));
		TestFalse(FString::Printf(TEXT("%s has a sentence"),
								  *Enum->GetNameStringByIndex(Index)),
			UCataclysmWearing::Explain(Result).IsEmpty());
	}

	return true;
}

// ---------------------------------------------------------------------------
// Keeping hold of a weapon. Issues #840 and #841
// ---------------------------------------------------------------------------

/**
 * A CHARACTER MUST KEEP A WEAPON ON, AND ISSUE #841 IS WHERE THAT IS DECIDED.
 * A character holding nothing has no weapon skills, because the six ability
 * slots come from the worn weapon's type.
 *
 * THIS COMMENT USED TO CALL THE RULE TEMPORARY AND SAY IT WAS WAITING ON FOUR
 * WEAPON TYPES to be given Demonic skills. That was wrong, and corrected on
 * 2026-08-23: the design gives each damage type its own set of weapons, Demonic
 * covers ten deliberately, and those four were never going to be among them. The
 * rule stays or goes on its own merits.
 *
 * THE RULE IS ABOUT REMOVING, NOT ABOUT WEARING. Swapping one weapon for
 * another goes through WearFromCarried and must stay unaffected, which is what
 * the last test here checks. A refusal that leaked into swapping would stop the
 * player ever changing weapon.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearingKeepsTheLastWeaponOn,
	"Cataclysm.Wearing.TheOnlyWeaponCannotBeTakenOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearingKeepsTheLastWeaponOn::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearingTest;

	FCarriedAndWorn Character;

	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	const int32 Slot = Character.Inventory->AddItem(Of(OneHandedBase));
	UCataclysmWearing::WearFromCarried(Character.Inventory, Character.Equipment,
									   Slot, Went);
	const int32 Before = Character.Everything();
	const int32 CarriedBefore = Character.Inventory->NumItems();

	const ECataclysmWearResult Result = UCataclysmWearing::TakeOffInto(
		Character.Inventory, Character.Equipment, Went);

	TestTrue(TEXT("taking off the only weapon is refused"),
		Result == ECataclysmWearResult::TheLastWeapon);
	TestEqual(TEXT("and nothing was destroyed"),
		Character.Everything(), Before);
	TestEqual(TEXT("nothing moved into the bag either"),
		Character.Inventory->NumItems(), CarriedBefore);
	TestNotNull(TEXT("the weapon is still worn"),
		Character.Equipment->EquippedAt(Went));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearingTakesOffOneOfTwoWeapons,
	"Cataclysm.Wearing.OneOfTwoWeaponsComesOffAndThenTheSecondWillNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearingTakesOffOneOfTwoWeapons::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearingTest;

	FCarriedAndWorn Character;

	ECataclysmGearSlot First = ECataclysmGearSlot::Count;
	ECataclysmGearSlot Second = ECataclysmGearSlot::Count;
	UCataclysmWearing::WearFromCarried(Character.Inventory, Character.Equipment,
		Character.Inventory->AddItem(Of(OneHandedBase)), First);
	UCataclysmWearing::WearFromCarried(Character.Inventory, Character.Equipment,
		Character.Inventory->AddItem(Of(OtherOneHandedBase)), Second);

	if (!TestEqual(TEXT("both weapons are worn"),
			Character.Equipment->NumEquipped(), 2))
	{
		return false;
	}
	const int32 Before = Character.Everything();

	// TWO WORN MEANS EITHER MAY COME OFF, because one is still held afterwards.
	TestTrue(TEXT("with two weapons on, one comes off"),
		UCataclysmWearing::TakeOffInto(Character.Inventory, Character.Equipment,
									   First) == ECataclysmWearResult::TakenOff);
	TestEqual(TEXT("and nothing was destroyed"),
		Character.Everything(), Before);

	// AND NOW THE SECOND IS THE LAST ONE. This is the half of the rule that a
	// test checking only the single-weapon case would miss: the count is read
	// each time rather than the first weapon being special.
	TestTrue(TEXT("but the second one will not come off"),
		UCataclysmWearing::TakeOffInto(Character.Inventory, Character.Equipment,
									   Second) == ECataclysmWearResult::TheLastWeapon);
	TestEqual(TEXT("and still nothing was destroyed"),
		Character.Everything(), Before);
	TestNotNull(TEXT("the remaining weapon is still worn"),
		Character.Equipment->EquippedAt(Second));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearingKeepsATwoHandedWeaponOn,
	"Cataclysm.Wearing.ATwoHandedWeaponCountsAsTheOnlyWeaponAndStaysOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearingKeepsATwoHandedWeaponOn::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearingTest;

	// A TWO-HANDED WEAPON IS STORED IN THE FIRST WEAPON SLOT ALONE and leaves
	// the second empty, so counting occupied weapon slots answers one. That is
	// the right answer -- it is the only weapon -- but it is worth a test of its
	// own, because a count that had been written to expect two would let the
	// character strip down to nothing while holding a greatsword.
	FCarriedAndWorn Character;

	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	UCataclysmWearing::WearFromCarried(Character.Inventory, Character.Equipment,
		Character.Inventory->AddItem(Of(TwoHandedBase)), Went);
	const int32 Before = Character.Everything();

	TestTrue(TEXT("the greatsword will not come off"),
		UCataclysmWearing::TakeOffInto(Character.Inventory, Character.Equipment,
									   Went) == ECataclysmWearResult::TheLastWeapon);
	TestEqual(TEXT("and nothing was destroyed"),
		Character.Everything(), Before);
	TestNotNull(TEXT("it is still worn"), Character.Equipment->EquippedAt(Went));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearingStillTakesOffArmour,
	"Cataclysm.Wearing.ArmourStillComesOffWhileTheOnlyWeaponIsWorn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearingStillTakesOffArmour::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearingTest;

	// THE RULE IS ABOUT WEAPON SLOTS AND NOTHING ELSE. A version that refused
	// whenever one item was worn would lock the character into its whole outfit,
	// and every other test here would still pass.
	FCarriedAndWorn Character;

	ECataclysmGearSlot WeaponWent = ECataclysmGearSlot::Count;
	ECataclysmGearSlot HelmWent = ECataclysmGearSlot::Count;
	UCataclysmWearing::WearFromCarried(Character.Inventory, Character.Equipment,
		Character.Inventory->AddItem(Of(OneHandedBase)), WeaponWent);
	UCataclysmWearing::WearFromCarried(Character.Inventory, Character.Equipment,
		Character.Inventory->AddItem(Of(HelmBase)), HelmWent);
	const int32 Before = Character.Everything();

	TestTrue(TEXT("the helm comes off even though one weapon is worn"),
		UCataclysmWearing::TakeOffInto(Character.Inventory, Character.Equipment,
									   HelmWent) == ECataclysmWearResult::TakenOff);
	TestEqual(TEXT("and nothing was destroyed"),
		Character.Everything(), Before);
	TestNotNull(TEXT("the weapon is untouched"),
		Character.Equipment->EquippedAt(WeaponWent));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearingSwapsTheOnlyWeapon,
	"Cataclysm.Wearing.TheOnlyWeaponCanStillBeSwappedForAnother",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearingSwapsTheOnlyWeapon::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearingTest;

	// THE POINT OF THE WHOLE RULE IS THAT THIS STILL WORKS. Issue #840 was
	// reported as putting a whip on making the character worse; if keeping a
	// weapon on had been written as "refuse to empty a weapon slot" rather than
	// as a check inside TakeOffInto, changing weapon would have been refused too
	// and the fix would have been worse than the fault.
	FCarriedAndWorn Character;

	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	UCataclysmWearing::WearFromCarried(Character.Inventory, Character.Equipment,
		Character.Inventory->AddItem(Of(OneHandedBase)), Went);

	const int32 Carried = Character.Inventory->AddItem(Of(TwoHandedBase, 3));
	const int32 Before = Character.Everything();

	// A TWO-HANDED WEAPON OVER A ONE-HANDED ONE TAKES THE OLD ONE OFF, so this
	// is the case that empties the weapon the character was holding.
	ECataclysmGearSlot SwappedInto = ECataclysmGearSlot::Count;
	const ECataclysmWearResult Result = UCataclysmWearing::WearFromCarried(
		Character.Inventory, Character.Equipment, Carried, SwappedInto);

	TestTrue(TEXT("the new weapon goes on and the old one comes off"),
		Result == ECataclysmWearResult::Swapped);
	TestEqual(TEXT("and nothing was destroyed"),
		Character.Everything(), Before);

	const FCataclysmItem* Worn = Character.Equipment->EquippedAt(SwappedInto);
	if (TestNotNull(TEXT("a weapon is worn afterwards"), Worn))
	{
		TestEqual(TEXT("and it is the new one"), Worn->Base,
			FName(TwoHandedBase));
		TestEqual(TEXT("upgrade level and all"), Worn->GearLevel, 3);
	}

	return true;
}


// ---------------------------------------------------------------------------
// Moving a carried slot with the cursor. Issue #853.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearingPicksUpAndPutsDown,
	"Cataclysm.Wearing.ACarriedSlotGoesOnTheCursorAndComesBackOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearingPicksUpAndPutsDown::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearingTest;

	FCarriedAndWorn Character;
	const int32 Where = Character.Inventory->AddItem(Of(HelmBase, 3));
	const int32 Before = Character.Everything();

	TestFalse(TEXT("nothing is held to begin with"),
		Character.Inventory->IsHolding());

	TestEqual(TEXT("picking it up succeeds"),
		UCataclysmWearing::PickUpCarried(Character.Inventory, Where),
		ECataclysmWearResult::PickedUp);
	TestTrue(TEXT("something is held"), Character.Inventory->IsHolding());
	TestEqual(TEXT("and it is the slot that was pressed"),
		Character.Inventory->HeldSlot(), Where);

	// THE ITEM DID NOT GO ANYWHERE, and that is the whole design. It is drawn as
	// though it were on the cursor and it is still one of the 48, so a save taken
	// now writes it out and there is no third place for it to be lost from.
	TestEqual(TEXT("the character still has everything it had"),
		Character.Everything(), Before);
	const FCataclysmItem* Still = Character.Inventory->ItemAt(Where);
	TestTrue(TEXT("the item is still in the slot it was picked up from"),
		Still != nullptr && Still->Base == FName(HelmBase));

	// PUTTING IT BACK WHERE IT CAME FROM IS HOW A PLAYER CANCELS.
	TestEqual(TEXT("putting it back down where it came from succeeds"),
		UCataclysmWearing::PutDownCarried(Character.Inventory, Where),
		ECataclysmWearResult::PutDown);
	TestFalse(TEXT("nothing is held afterwards"),
		Character.Inventory->IsHolding());
	TestEqual(TEXT("and nothing was lost"), Character.Everything(), Before);

	// LETTING GO WITHOUT PUTTING IT DOWN, which is what closing the screen does.
	UCataclysmWearing::PickUpCarried(Character.Inventory, Where);
	UCataclysmWearing::ReleaseHeld(Character.Inventory);
	TestFalse(TEXT("releasing clears the cursor"),
		Character.Inventory->IsHolding());
	TestEqual(TEXT("and it loses nothing, because nothing had moved"),
		Character.Everything(), Before);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearingExchangesTwoSlots,
	"Cataclysm.Wearing.PuttingOneDownOnAnotherExchangesThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearingExchangesTwoSlots::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearingTest;

	FCarriedAndWorn Character;
	const int32 First = Character.Inventory->AddItem(Of(HelmBase));
	const int32 Second = Character.Inventory->AddItem(Of(BootsBase));
	const int32 Before = Character.Everything();

	UCataclysmWearing::PickUpCarried(Character.Inventory, First);
	TestEqual(TEXT("putting it on an occupied cell is an exchange"),
		UCataclysmWearing::PutDownCarried(Character.Inventory, Second),
		ECataclysmWearResult::Exchanged);

	// AN EXCHANGE AND NOT A REFUSAL, which is what makes the grid rearrangeable
	// at all, and neither piece went anywhere but into the other's cell.
	const FCataclysmItem* NowFirst = Character.Inventory->ItemAt(First);
	const FCataclysmItem* NowSecond = Character.Inventory->ItemAt(Second);
	TestTrue(TEXT("the boots are where the helm was"),
		NowFirst != nullptr && NowFirst->Base == FName(BootsBase));
	TestTrue(TEXT("the helm is where the boots were"),
		NowSecond != nullptr && NowSecond->Base == FName(HelmBase));
	TestFalse(TEXT("nothing is held afterwards"),
		Character.Inventory->IsHolding());
	TestEqual(TEXT("and the character has exactly what it had"),
		Character.Everything(), Before);

	// AN EMPTY CELL IS A PLAIN PUT DOWN.
	const int32 Empty = Character.Inventory->FirstFreeSlot();
	UCataclysmWearing::PickUpCarried(Character.Inventory, First);
	TestEqual(TEXT("putting it on an empty cell is not an exchange"),
		UCataclysmWearing::PutDownCarried(Character.Inventory, Empty),
		ECataclysmWearResult::PutDown);
	TestEqual(TEXT("and still nothing was lost"), Character.Everything(), Before);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearingCursorRefusals,
	"Cataclysm.Wearing.TheCursorRefusesWhatItShould",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearingCursorRefusals::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearingTest;

	FCarriedAndWorn Character;
	const int32 Where = Character.Inventory->AddItem(Of(HelmBase));
	const int32 Empty = Character.Inventory->FirstFreeSlot();

	TestEqual(TEXT("putting down with nothing held is refused"),
		UCataclysmWearing::PutDownCarried(Character.Inventory, Where),
		ECataclysmWearResult::NothingHeld);

	TestEqual(TEXT("picking up an empty cell is refused"),
		UCataclysmWearing::PickUpCarried(Character.Inventory, Empty),
		ECataclysmWearResult::NothingToPickUp);
	TestFalse(TEXT("and nothing is held after that refusal"),
		Character.Inventory->IsHolding());

	TestEqual(TEXT("picking up a cell outside the bag is refused"),
		UCataclysmWearing::PickUpCarried(Character.Inventory, 999),
		ECataclysmWearResult::NothingToPickUp);

	// ONE THING AT A TIME. A second pick up while holding would have to decide
	// what happens to the first, and every answer is a rule the player has to
	// learn. Refusing is the one answer that needs no explaining.
	UCataclysmWearing::PickUpCarried(Character.Inventory, Where);
	const int32 Other = Character.Inventory->AddItem(Of(BootsBase));
	TestEqual(TEXT("picking up a second thing is refused"),
		UCataclysmWearing::PickUpCarried(Character.Inventory, Other),
		ECataclysmWearResult::AlreadyHolding);
	TestEqual(TEXT("and the first thing is still the one held"),
		Character.Inventory->HeldSlot(), Where);

	TestEqual(TEXT("no inventory at all is refused"),
		UCataclysmWearing::PickUpCarried(nullptr, 0),
		ECataclysmWearResult::NothingToWorkWith);
	TestEqual(TEXT("and so is putting down without one"),
		UCataclysmWearing::PutDownCarried(nullptr, 0),
		ECataclysmWearResult::NothingToWorkWith);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearingPicksUpAWornPiece,
	"Cataclysm.Wearing.AWornPieceComesOffOntoTheCursor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearingPicksUpAWornPiece::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearingTest;

	FCarriedAndWorn Character;
	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	UCataclysmWearing::WearFromCarried(Character.Inventory, Character.Equipment,
		Character.Inventory->AddItem(Of(HelmBase)), Went);
	const int32 Before = Character.Everything();

	TestEqual(TEXT("taking it off onto the cursor succeeds"),
		UCataclysmWearing::PickUpWorn(Character.Inventory, Character.Equipment,
									  Went),
		ECataclysmWearResult::PickedUp);
	TestTrue(TEXT("something is held"), Character.Inventory->IsHolding());

	// IT IS IN THE BAG, in the slot the cursor is pointing at. A worn piece is
	// not a carried slot, so it has to land in one before it can be held.
	const FCataclysmItem* Held =
		Character.Inventory->ItemAt(Character.Inventory->HeldSlot());
	TestTrue(TEXT("the held slot holds the piece that came off"),
		Held != nullptr && Held->Base == FName(HelmBase));
	TestEqual(TEXT("and the character has exactly what it had"),
		Character.Everything(), Before);

	// EVERY REFUSAL TakeOffInto MAKES COMES BACK UNCHANGED, and nothing is left
	// on the cursor after one.
	FCarriedAndWorn Full;
	ECataclysmGearSlot WentToo = ECataclysmGearSlot::Count;
	UCataclysmWearing::WearFromCarried(Full.Inventory, Full.Equipment,
		Full.Inventory->AddItem(Of(HelmBase)), WentToo);
	FillTheBag(Full);
	TestEqual(TEXT("a full bag refuses it"),
		UCataclysmWearing::PickUpWorn(Full.Inventory, Full.Equipment, WentToo),
		ECataclysmWearResult::NoRoomInTheBag);
	TestFalse(TEXT("and nothing is held after that refusal"),
		Full.Inventory->IsHolding());

	FCarriedAndWorn Armed;
	ECataclysmGearSlot WeaponSlot = ECataclysmGearSlot::Count;
	UCataclysmWearing::WearFromCarried(Armed.Inventory, Armed.Equipment,
		Armed.Inventory->AddItem(Of(OneHandedBase)), WeaponSlot);
	TestEqual(TEXT("a character's only weapon refuses it"),
		UCataclysmWearing::PickUpWorn(Armed.Inventory, Armed.Equipment,
									  WeaponSlot),
		ECataclysmWearResult::TheLastWeapon);
	TestFalse(TEXT("and nothing is held after that refusal either"),
		Armed.Inventory->IsHolding());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWearingCursorLosesNothing,
	"Cataclysm.Wearing.NoItemIsLostHoweverOftenTheCursorMovesThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWearingCursorLosesNothing::RunTest(const FString& Parameters)
{
	using namespace CataclysmWearingTest;

	// THE ONE RULE THIS MODULE STATES TWICE, applied to the cursor. Every path
	// leaves every item either worn or carried, or changes nothing at all.
	FCarriedAndWorn Character;
	const TCHAR* Bases[] = { HelmBase, BootsBase, OneHandedBase,
							 OtherOneHandedBase, TwoHandedBase };
	for (const TCHAR* Base : Bases)
	{
		Character.Inventory->AddItem(Of(Base));
	}

	/** How many of each base the character holds anywhere. */
	auto Census = [&Character]() -> TMap<FName, int32>
	{
		TMap<FName, int32> Counted;
		for (const FCataclysmCarriedSlot& Slot : Character.Inventory->GetSlots())
		{
			if (!UCataclysmInventoryComponent::SlotIsEmpty(Slot.Item))
			{
				++Counted.FindOrAdd(Slot.Item.Base);
			}
		}
		return Counted;
	};

	const TMap<FName, int32> Before = Census();
	const int32 CountBefore = Character.Everything();

	// A LONG SEQUENCE, INCLUDING THE REFUSALS. A rule that only holds for the
	// gestures that succeed is not the rule this module states.
	const int32 Slots = Character.Inventory->GetSlots().Num();
	for (int32 Step = 0; Step < 200; ++Step)
	{
		const int32 Target = (Step * 7 + 3) % Slots;
		if (Character.Inventory->IsHolding())
		{
			UCataclysmWearing::PutDownCarried(Character.Inventory, Target);
		}
		else
		{
			UCataclysmWearing::PickUpCarried(Character.Inventory, Target);
		}
	}
	UCataclysmWearing::ReleaseHeld(Character.Inventory);

	const TMap<FName, int32> After = Census();
	TestEqual(TEXT("the character has the same number of things"),
		Character.Everything(), CountBefore);
	TestEqual(TEXT("and the same kinds of them"), After.Num(), Before.Num());

	for (const TPair<FName, int32>& Each : Before)
	{
		const int32* Now = After.Find(Each.Key);
		TestTrue(FString::Printf(TEXT("%s is still here"), *Each.Key.ToString()),
			Now != nullptr && *Now == Each.Value);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
