// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmInventoryComponent.h"

UCataclysmInventoryComponent::UCataclysmInventoryComponent()
{
	// NOTHING TICKS. An inventory changes when something puts an item in it or
	// takes one out, and neither of those is a frame event.
	PrimaryComponentTick.bCanEverTick = false;

	// THE FULL 48 EXIST FROM THE START, empty. See the header for why the store
	// is a fixed array of slots rather than a list that grows.
	Slots.SetNum(SlotCount);
}

bool UCataclysmInventoryComponent::SlotIsEmpty(const FCataclysmItem& Item)
{
	return Item.Base.IsNone();
}

int32 UCataclysmInventoryComponent::NumItems() const
{
	int32 Count = 0;
	for (const FCataclysmItem& Item : Slots)
	{
		if (!SlotIsEmpty(Item))
		{
			++Count;
		}
	}
	return Count;
}

int32 UCataclysmInventoryComponent::NumFreeSlots() const
{
	return SlotCount - NumItems();
}

bool UCataclysmInventoryComponent::IsFull() const
{
	return FirstFreeSlot() == INDEX_NONE;
}

int32 UCataclysmInventoryComponent::FirstFreeSlot() const
{
	for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
	{
		if (SlotIsEmpty(Slots[Slot]))
		{
			return Slot;
		}
	}
	return INDEX_NONE;
}

int32 UCataclysmInventoryComponent::AddItem(const FCataclysmItem& Item)
{
	// AN ITEM WITH NO BASE IS NOT AN ITEM. Storing one would fill a slot that
	// every count in this file would go on reading as free, so the inventory
	// would report room it does not have.
	if (SlotIsEmpty(Item))
	{
		return INDEX_NONE;
	}

	const int32 Slot = FirstFreeSlot();
	if (Slot == INDEX_NONE)
	{
		// NO ROOM, AND THE ITEM IS UNTOUCHED. The caller still has it, which is
		// what lets a drop stay on the floor rather than being destroyed.
		return INDEX_NONE;
	}

	Slots[Slot] = Item;
	return Slot;
}

bool UCataclysmInventoryComponent::RemoveItemAt(int32 Slot)
{
	if (!Slots.IsValidIndex(Slot) || SlotIsEmpty(Slots[Slot]))
	{
		return false;
	}

	// RESET RATHER THAN REMOVE. Taking the entry out of the array would shorten
	// it, which would move every item after this one into a different slot.
	Slots[Slot] = FCataclysmItem();
	return true;
}

void UCataclysmInventoryComponent::RemoveEverything()
{
	for (FCataclysmItem& Item : Slots)
	{
		Item = FCataclysmItem();
	}
}

const FCataclysmItem* UCataclysmInventoryComponent::ItemAt(int32 Slot) const
{
	if (!Slots.IsValidIndex(Slot) || SlotIsEmpty(Slots[Slot]))
	{
		return nullptr;
	}
	return &Slots[Slot];
}
