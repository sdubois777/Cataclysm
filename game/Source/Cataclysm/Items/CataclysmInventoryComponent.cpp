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

bool UCataclysmInventoryComponent::SlotIsEmpty(const FCataclysmCarriedSlot& Slot)
{
	// A SLOT IS EMPTY ONLY WHEN IT HOLDS NEITHER KIND. A material stack of zero
	// is not a stack, and a material with no name is not a material, so both
	// have to be false before the quantity is worth looking at.
	return SlotIsEmpty(Slot.Item)
		&& (Slot.Material.IsNone() || Slot.Quantity <= 0);
}

int32 UCataclysmInventoryComponent::NumItems() const
{
	int32 Count = 0;
	for (const FCataclysmCarriedSlot& Slot : Slots)
	{
		if (!SlotIsEmpty(Slot))
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

	// GEAR NEVER STACKS. Two items of one base carry different affixes, upgrade
	// levels, sockets and residue, so there is no sense in which they are the
	// same thing. The design says so and the header on AddMaterial says why the
	// other kind is different.
	Slots[Slot].Item = Item;
	return Slot;
}

int32 UCataclysmInventoryComponent::SlotOfMaterial(FName Material) const
{
	if (Material.IsNone())
	{
		return INDEX_NONE;
	}

	for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
	{
		if (Slots[Slot].Material == Material && Slots[Slot].Quantity > 0)
		{
			return Slot;
		}
	}
	return INDEX_NONE;
}

int32 UCataclysmInventoryComponent::CountOfMaterial(FName Material) const
{
	if (Material.IsNone())
	{
		return 0;
	}

	// SUMMED ACROSS EVERY SLOT rather than read off the one SlotOfMaterial
	// finds. AddMaterial keeps a material to a single stack, and this is what
	// would notice if that ever stopped being true.
	int32 Total = 0;
	for (const FCataclysmCarriedSlot& Slot : Slots)
	{
		if (Slot.Material == Material && Slot.Quantity > 0)
		{
			Total += Slot.Quantity;
		}
	}
	return Total;
}

int32 UCataclysmInventoryComponent::AddMaterial(FName Material, int32 Quantity)
{
	// NOTHING TO ADD. A material with no name cannot be told from another, and
	// none of something is not a drop.
	if (Material.IsNone() || Quantity <= 0)
	{
		return INDEX_NONE;
	}

	// AN EXISTING STACK FIRST. All crafting materials stack, so a second
	// Corrupted Mote joins the first rather than taking a slot of its own. No
	// ceiling is set, so a stack always has room and this never fails.
	const int32 Stacked = SlotOfMaterial(Material);
	if (Stacked != INDEX_NONE)
	{
		Slots[Stacked].Quantity += Quantity;
		return Stacked;
	}

	// ONLY A MATERIAL NOTHING IS CARRYING YET NEEDS A SLOT.
	const int32 Slot = FirstFreeSlot();
	if (Slot == INDEX_NONE)
	{
		// NO ROOM, AND NOTHING WAS STORED. The caller still has the materials,
		// so they can stay on the floor like a gear drop that will not fit.
		return INDEX_NONE;
	}

	Slots[Slot].Material = Material;
	Slots[Slot].Quantity = Quantity;
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
	//
	// A WHOLE STACK GOES AT ONCE, because a slot is what is being emptied. There
	// is no way to put down some of a stack and keep the rest; splitting one is
	// interface work and belongs with the inventory screen, issue #49.
	Slots[Slot] = FCataclysmCarriedSlot();
	return true;
}

void UCataclysmInventoryComponent::RemoveEverything()
{
	for (FCataclysmCarriedSlot& Slot : Slots)
	{
		Slot = FCataclysmCarriedSlot();
	}
}

const FCataclysmItem* UCataclysmInventoryComponent::ItemAt(int32 Slot) const
{
	// A SLOT HOLDING A MATERIAL HOLDS NO ITEM, so this answers nothing for one
	// rather than answering an item with no base.
	if (!Slots.IsValidIndex(Slot) || SlotIsEmpty(Slots[Slot].Item))
	{
		return nullptr;
	}
	return &Slots[Slot].Item;
}
