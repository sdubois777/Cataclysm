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
	ECataclysmGearSlot Slot)
{
	if (!Inventory || !Equipment)
	{
		return ECataclysmWearResult::NothingToWorkWith;
	}

	if (Equipment->SlotIsEmpty(Slot))
	{
		return ECataclysmWearResult::NothingWorn;
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

	Inventory->AddItem(TakenOff);
	return ECataclysmWearResult::TakenOff;
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
	case ECataclysmWearResult::NothingToWorkWith:
		return TEXT("This character has no inventory or no equipment.");
	}
	return FString();
}
