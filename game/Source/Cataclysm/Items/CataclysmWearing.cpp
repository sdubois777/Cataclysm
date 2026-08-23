// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmWearing.h"

#include "Cataclysm.h"

#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"

ECataclysmWearResult UCataclysmWearing::WearFromCarried(
	UCataclysmInventoryComponent* Inventory,
	UCataclysmEquipmentComponent* Equipment,
	int32 CarriedSlot,
	ECataclysmGearSlot& OutSlot)
{
	if (!Inventory || !Equipment)
	{
		return ECataclysmWearResult::NothingToWorkWith;
	}

	const FCataclysmItem* Carried = Inventory->ItemAt(CarriedSlot);
	if (!Carried)
	{
		// EMPTY, OR HOLDING A CRAFTING MATERIAL. ItemAt answers nothing for a
		// material rather than answering an item with no base, so both cases
		// arrive here and neither is something to wear.
		return ECataclysmWearResult::NothingToWear;
	}

	// COPIED BEFORE THE SLOT IS EMPTIED. The pointer is into the inventory's own
	// array, so removing the item would leave it pointing at a cleared slot.
	const FCataclysmItem Item = *Carried;

	// -- refuse before anything moves --------------------------------------
	// A TWO-HANDED WEAPON IS THE ONLY THING THAT CAN TAKE TWO PIECES OFF for one
	// going on, and the slot this item is vacating only makes room for one. So
	// it needs a second free slot, and the check happens here rather than after
	// the fact: a half-completed change would have to be rolled back, and a
	// rollback has failure modes of its own.
	//
	// EVERYTHING ELSE IS A ONE-FOR-ONE SWAP AND ALWAYS FITS, including on a full
	// bag, which is why this is not simply "refuse when the bag is full".
	const bool bTwoHanded = UCataclysmItemModifiers::IsTwoHanded(
		Item, UCataclysmItemModifiers::LoadBaseTable());
	if (bTwoHanded && Inventory->NumFreeSlots() < 1)
	{
		int32 WeaponsWorn = 0;
		for (const ECataclysmGearSlot Weapon : UCataclysmGearSlots::WeaponSlots())
		{
			if (!Equipment->SlotIsEmpty(Weapon))
			{
				++WeaponsWorn;
			}
		}
		if (WeaponsWorn > 1)
		{
			return ECataclysmWearResult::NoRoomInTheBag;
		}
	}

	Inventory->RemoveItemAt(CarriedSlot);

	FCataclysmItem First;
	FCataclysmItem Second;
	const ECataclysmEquipResult Result =
		Equipment->Equip(Item, First, Second, OutSlot);

	if (Result != ECataclysmEquipResult::Equipped
		&& Result != ECataclysmEquipResult::Swapped)
	{
		// IT GOES STRAIGHT BACK, and it always fits: the slot it came out of is
		// still free because nothing has been put there.
		Inventory->AddItem(Item);
		return ECataclysmWearResult::CannotBeWorn;
	}

	bool bAnythingCameOff = false;
	for (const FCataclysmItem& CameOff : {First, Second})
	{
		if (CameOff.Base.IsNone())
		{
			continue;
		}
		bAnythingCameOff = true;

		// THE ANSWER IS READ. A refusal here would mean an item had been lost,
		// and the check at the top of this function is what makes it impossible;
		// if it ever happens, that check is wrong rather than this being a case
		// to handle quietly.
		if (Inventory->AddItem(CameOff) == INDEX_NONE)
		{
			UE_LOG(LogCataclysm, Error,
				   TEXT("%s came off and the bag would not take it. An item has "
						"been lost. UCataclysmWearing::WearFromCarried's room "
						"check is wrong."),
				   *CameOff.Base.ToString());
		}
	}

	return bAnythingCameOff ? ECataclysmWearResult::Swapped
							: ECataclysmWearResult::Worn;
}

ECataclysmWearResult UCataclysmWearing::TakeOffInto(
	UCataclysmInventoryComponent* Inventory,
	UCataclysmEquipmentComponent* Equipment,
	ECataclysmGearSlot Slot,
	int32* OutCarriedSlot)
{
	if (!Inventory || !Equipment)
	{
		return ECataclysmWearResult::NothingToWorkWith;
	}

	if (Equipment->SlotIsEmpty(Slot))
	{
		return ECataclysmWearResult::NothingWorn;
	}

	// -- refuse before anything moves --------------------------------------
	// A CHARACTER MUST KEEP HOLD OF A WEAPON, AND ISSUE #841 IS WHERE THAT IS
	// DECIDED. UCataclysmWeaponSlotsComponent grants the six ability slots from
	// the worn weapon's type, so a character holding nothing has no skills.
	//
	// THIS COMMENT USED TO SAY THE RULE WAS WAITING ON MISSING CONTENT, naming
	// four weapon types that had no Demonic skills. That was wrong: the design
	// gives each damage type its own set of weapons and Demonic's set is those
	// ten, deliberately. The four were never going to arrive, so the reason
	// described a plan that did not exist. See the header on ECataclysmWearResult
	// for the whole of it.
	//
	// COUNTED RATHER THAN READ OFF THE SLOT. A two-handed weapon is stored in
	// the first weapon slot alone and leaves the second empty, so it counts as
	// one and is refused, which is right -- it is the only weapon. Two
	// one-handed weapons count as two and either one may come off.
	//
	// SWAPPING IS UNAFFECTED. Wearing a different weapon over this one goes
	// through WearFromCarried, which never calls this.
	if (UCataclysmGearSlots::IsWeaponSlot(Slot))
	{
		int32 WeaponsWorn = 0;
		for (const ECataclysmGearSlot HeldWeaponSlot :
			 UCataclysmGearSlots::WeaponSlots())
		{
			if (!Equipment->SlotIsEmpty(HeldWeaponSlot))
			{
				++WeaponsWorn;
			}
		}
		if (WeaponsWorn <= 1)
		{
			return ECataclysmWearResult::TheLastWeapon;
		}
	}

	// ASKED BEFORE THE PIECE COMES OFF. Taking it off first and finding nowhere
	// to put it would leave it held by a local variable about to go out of
	// scope, and there is no floor to drop it on from a screen.
	if (Inventory->IsFull())
	{
		return ECataclysmWearResult::NoRoomInTheBag;
	}

	FCataclysmItem TakenOff;
	if (!Equipment->Unequip(Slot, TakenOff))
	{
		return ECataclysmWearResult::NothingWorn;
	}

	const int32 LandedIn = Inventory->AddItem(TakenOff);
	if (OutCarriedSlot)
	{
		*OutCarriedSlot = LandedIn;
	}
	return ECataclysmWearResult::TakenOff;
}

// ---------------------------------------------------------------------------
// Moving a carried slot with the cursor. Issue #853.
// ---------------------------------------------------------------------------

ECataclysmWearResult UCataclysmWearing::PickUpCarried(
	UCataclysmInventoryComponent* Inventory, int32 CarriedSlot)
{
	if (!Inventory)
	{
		return ECataclysmWearResult::NothingToWorkWith;
	}
	if (Inventory->IsHolding())
	{
		return ECataclysmWearResult::AlreadyHolding;
	}
	if (!Inventory->GetSlots().IsValidIndex(CarriedSlot)
		|| UCataclysmInventoryComponent::SlotIsEmpty(
			   Inventory->GetSlots()[CarriedSlot]))
	{
		return ECataclysmWearResult::NothingToPickUp;
	}

	Inventory->HoldSlot(CarriedSlot);
	return ECataclysmWearResult::PickedUp;
}

ECataclysmWearResult UCataclysmWearing::PutDownCarried(
	UCataclysmInventoryComponent* Inventory, int32 CarriedSlot)
{
	if (!Inventory)
	{
		return ECataclysmWearResult::NothingToWorkWith;
	}
	if (!Inventory->IsHolding())
	{
		return ECataclysmWearResult::NothingHeld;
	}
	if (!Inventory->GetSlots().IsValidIndex(CarriedSlot))
	{
		return ECataclysmWearResult::NothingToPickUp;
	}

	// READ BEFORE THE SWAP, because afterwards the target holds what was on
	// the cursor and the two cases cannot be told apart.
	const int32 From = Inventory->HeldSlot();
	const bool bWasOccupied =
		!UCataclysmInventoryComponent::SlotIsEmpty(
			Inventory->GetSlots()[CarriedSlot])
		&& CarriedSlot != From;

	if (!Inventory->SwapSlots(From, CarriedSlot))
	{
		return ECataclysmWearResult::NothingToWorkWith;
	}
	Inventory->HoldSlot(INDEX_NONE);

	return bWasOccupied ? ECataclysmWearResult::Exchanged
						: ECataclysmWearResult::PutDown;
}

ECataclysmWearResult UCataclysmWearing::PickUpWorn(
	UCataclysmInventoryComponent* Inventory,
	UCataclysmEquipmentComponent* Equipment,
	ECataclysmGearSlot Slot)
{
	if (!Inventory || !Equipment)
	{
		return ECataclysmWearResult::NothingToWorkWith;
	}
	if (Inventory->IsHolding())
	{
		return ECataclysmWearResult::AlreadyHolding;
	}

	// TAKEN OFF FIRST AND HELD SECOND, because a worn piece is not in the bag
	// and the cursor holds a carried slot. Every refusal TakeOffInto makes --
	// a full bag, a character's last weapon -- comes back unchanged and
	// nothing is held.
	int32 LandedIn = INDEX_NONE;
	const ECataclysmWearResult Result =
		TakeOffInto(Inventory, Equipment, Slot, &LandedIn);
	if (Result != ECataclysmWearResult::TakenOff)
	{
		return Result;
	}

	if (LandedIn == INDEX_NONE)
	{
		// IT CAME OFF AND IS IN THE BAG, so nothing is lost; only the cursor
		// missed it. Reported as taken off rather than picked up, because that
		// is what happened.
		return ECataclysmWearResult::TakenOff;
	}

	Inventory->HoldSlot(LandedIn);
	return ECataclysmWearResult::PickedUp;
}

void UCataclysmWearing::ReleaseHeld(UCataclysmInventoryComponent* Inventory)
{
	if (Inventory)
	{
		Inventory->HoldSlot(INDEX_NONE);
	}
}

FString UCataclysmWearing::Explain(ECataclysmWearResult Result)
{
	switch (Result)
	{
	case ECataclysmWearResult::Worn:
		return TEXT("Worn.");
	case ECataclysmWearResult::Swapped:
		return TEXT("Worn, and what came off is in the bag.");
	case ECataclysmWearResult::TakenOff:
		return TEXT("Taken off and put in the bag.");
	case ECataclysmWearResult::NothingToWear:
		return TEXT("There is nothing there to wear.");
	case ECataclysmWearResult::NothingWorn:
		return TEXT("Nothing is worn there.");
	case ECataclysmWearResult::CannotBeWorn:
		return TEXT("That goes in no slot this character has.");
	case ECataclysmWearResult::NoRoomInTheBag:
		return TEXT("There is no room in the bag, so nothing was moved.");
	case ECataclysmWearResult::TheLastWeapon:
		return TEXT("That is your only weapon, and you have to hold one. "
					"Wear a different weapon over it instead.");
	case ECataclysmWearResult::NothingToWorkWith:
		return TEXT("This character has no inventory or no equipment.");
	case ECataclysmWearResult::PickedUp:
		return TEXT("Picked up.");
	case ECataclysmWearResult::PutDown:
		return TEXT("Put down.");
	case ECataclysmWearResult::Exchanged:
		return TEXT("Put down, and the two changed places.");
	case ECataclysmWearResult::NothingToPickUp:
		return TEXT("There is nothing there to pick up.");
	case ECataclysmWearResult::NothingHeld:
		return TEXT("Nothing is being held.");
	case ECataclysmWearResult::AlreadyHolding:
		return TEXT("Something is already being held. Put it down first.");
	}
	return FString();
}
