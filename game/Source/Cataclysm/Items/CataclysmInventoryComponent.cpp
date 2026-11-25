// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmInventoryComponent.h"
#include "Save/CataclysmSaveWriter.h"

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
	// A SLOT IS EMPTY ONLY WHEN IT HOLDS NO KIND. A stack of zero is not a
	// stack, and a material or a gem with no name is not one, so a name has to be
	// there before the quantity is worth looking at. Gems since issue #46. A
	// stash slot still holds stacks; a carried gear slot holds none since #2849.
	return SlotIsEmpty(Slot.Item)
		&& ((Slot.Material.IsNone() && Slot.Gem.IsNone()) || Slot.Quantity <= 0);
}

void UCataclysmInventoryComponent::NoteChanged()
{
	++Changes;
	UCataclysmSaveWriter::NoteTriggerIn(GetWorld(), ECataclysmSaveTrigger::InventoryChanged);
}

int32 UCataclysmInventoryComponent::NumItems() const
{
	// GEAR ONLY. Since #2849 a gear slot never holds a stack, and the gear item is
	// what decides whether a slot is taken.
	int32 Count = 0;
	for (const FCataclysmCarriedSlot& Slot : Slots)
	{
		if (!SlotIsEmpty(Slot.Item))
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
		if (SlotIsEmpty(Slots[Slot].Item))
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
	// same thing.
	Slots[Slot] = FCataclysmCarriedSlot();
	Slots[Slot].Item = Item;

	// AN ITEM ENTERING THE INVENTORY IS ONE OF THE FIVE EVENTS SECTION 6
	// WRITES ON. What a character is carrying lives in its own record rather
	// than in the run's, so this is one of the two triggers that writes it.
	NoteChanged();
	return Slot;
}

int32 UCataclysmInventoryComponent::StackOfMaterial(FName Material) const
{
	if (Material.IsNone())
	{
		return INDEX_NONE;
	}
	for (int32 Stack = 0; Stack < MaterialStacks.Num(); ++Stack)
	{
		if (MaterialStacks[Stack].Material == Material)
		{
			return Stack;
		}
	}
	return INDEX_NONE;
}

int32 UCataclysmInventoryComponent::CountOfMaterial(FName Material) const
{
	const int32 Stack = StackOfMaterial(Material);
	return Stack == INDEX_NONE ? 0 : FMath::Max(0, MaterialStacks[Stack].Quantity);
}

void UCataclysmInventoryComponent::PutMaterial(FName Material, int32 Quantity)
{
	const int32 Stack = StackOfMaterial(Material);
	if (Stack != INDEX_NONE)
	{
		MaterialStacks[Stack].Quantity += Quantity;
		return;
	}
	FCataclysmCarriedSlot Added;
	Added.Material = Material;
	Added.Quantity = Quantity;
	MaterialStacks.Add(Added);
}

void UCataclysmInventoryComponent::PutGems(FName Gem, ECataclysmRarity Rarity, int32 Quantity)
{
	const int32 Stack = StackOfGems(Gem, Rarity);
	if (Stack != INDEX_NONE)
	{
		GemStacks[Stack].Quantity += Quantity;
		return;
	}
	// A NEW STACK GOES WHERE THE ORDER BY GEM, THEN RARITY, PUTS IT, which is the order the gems tab draws (Q7 of #2849).
	FCataclysmCarriedSlot Added;
	Added.Gem = Gem;
	Added.GemRarity = Rarity;
	Added.Quantity = Quantity;
	int32 At = 0;
	while (At < GemStacks.Num()
		   && (GemStacks[At].Gem.LexicalLess(Gem)
			   || (GemStacks[At].Gem == Gem && static_cast<int32>(GemStacks[At].GemRarity) < static_cast<int32>(Rarity))))
	{
		++At;
	}
	GemStacks.Insert(Added, At);
}

bool UCataclysmInventoryComponent::AddMaterial(FName Material, int32 Quantity)
{
	// NOTHING TO ADD. A material with no name cannot be told from another, and
	// none of something is not a drop.
	if (Material.IsNone() || Quantity <= 0)
	{
		return false;
	}

	// ONE STACK PER MATERIAL, AND NO LIMIT: the owner's ruling of 2026-11-22 (#2849). A material is never refused for
	// want of room and never takes a gear slot.
	PutMaterial(Material, Quantity);
	NoteChanged();
	return true;
}

bool UCataclysmInventoryComponent::RemoveMaterial(FName Material, int32 Quantity)
{
	// NOTHING TO TAKE, OR NOT ENOUGH OF IT: NOTHING IS TAKEN. A price is paid whole or not at all.
	const int32 Stack = StackOfMaterial(Material);
	if (Quantity <= 0 || Stack == INDEX_NONE || MaterialStacks[Stack].Quantity < Quantity)
	{
		return false;
	}

	MaterialStacks[Stack].Quantity -= Quantity;
	if (MaterialStacks[Stack].Quantity <= 0)
	{
		// AN EMPTY STACK LEAVES THE STORE. The materials tab still shows the material, with nought carried.
		MaterialStacks.RemoveAt(Stack);
	}

	// A STACK GETTING SMALLER IS A CHANGE, as one getting bigger is: the tool tip states how many are carried.
	NoteChanged();
	return true;
}

int32 UCataclysmInventoryComponent::StackOfGems(FName Gem, ECataclysmRarity Rarity) const
{
	if (Gem.IsNone())
	{
		return INDEX_NONE;
	}
	for (int32 Stack = 0; Stack < GemStacks.Num(); ++Stack)
	{
		if (GemStacks[Stack].Gem == Gem && GemStacks[Stack].GemRarity == Rarity)
		{
			return Stack;
		}
	}
	return INDEX_NONE;
}

int32 UCataclysmInventoryComponent::CountOfGems(FName Gem, ECataclysmRarity Rarity) const
{
	const int32 Stack = StackOfGems(Gem, Rarity);
	return Stack == INDEX_NONE ? 0 : FMath::Max(0, GemStacks[Stack].Quantity);
}

bool UCataclysmInventoryComponent::RemoveGems(FName Gem, ECataclysmRarity Rarity, int32 Quantity)
{
	// ALL OR NOTHING, as a material is taken.
	const int32 Stack = StackOfGems(Gem, Rarity);
	if (Quantity <= 0 || Stack == INDEX_NONE || GemStacks[Stack].Quantity < Quantity)
	{
		return false;
	}
	GemStacks[Stack].Quantity -= Quantity;
	if (GemStacks[Stack].Quantity <= 0)
	{
		GemStacks.RemoveAt(Stack);
	}
	NoteChanged();
	return true;
}

bool UCataclysmInventoryComponent::AddGems(FName Gem, ECataclysmRarity Rarity, int32 Quantity)
{
	if (Gem.IsNone() || Quantity <= 0)
	{
		return false;
	}

	// A STACK OF THIS GEM AT THIS RARITY FIRST, and a new one otherwise. No limit, so this never fails for room.
	PutGems(Gem, Rarity, Quantity);
	NoteChanged();
	return true;
}

bool UCataclysmInventoryComponent::SwapSlots(int32 First, int32 Second)
{
	if (!Slots.IsValidIndex(First) || !Slots.IsValidIndex(Second))
	{
		return false;
	}
	if (First == Second)
	{
		// Putting something back where it came from. Nothing moved, so nothing
		// is recorded as having changed either.
		return true;
	}

	// GEAR SLOTS ONLY, and since #2849 a gear slot holds nothing else, so a swap can never move a stack into a gear
	// cell: the stacks are not in this array.
	Slots.Swap(First, Second);

	// COUNTED AND RECORDED, because where an item sits is part of what the
	// save holds. FCataclysmSaveGather writes the slots in order, so two
	// pieces changing places changes the file even though the bag holds the
	// same things.
	NoteChanged();
	return true;
}

bool UCataclysmInventoryComponent::HoldSlot(int32 Slot)
{
	if (Slot != INDEX_NONE && !Slots.IsValidIndex(Slot))
	{
		return false;
	}

	// NEITHER COUNTED NOR RECORDED. Nothing about the bag changed: the same
	// items are in the same slots, and only the cursor moved.
	Held = Slot;
	return true;
}

bool UCataclysmInventoryComponent::ReplaceItemAt(int32 Slot, const FCataclysmItem& Changed)
{
	const FCataclysmItem* Current = ItemAt(Slot);
	if (Current == nullptr || Changed.Base != Current->Base)
	{
		return false;
	}

	Slots[Slot].Item = Changed;
	++Changes;
	return true;
}

bool UCataclysmInventoryComponent::RemoveItemAt(int32 Slot)
{
	if (!Slots.IsValidIndex(Slot) || SlotIsEmpty(Slots[Slot].Item))
	{
		return false;
	}

	// RESET RATHER THAN REMOVE. Taking the entry out of the array would shorten
	// it, which would move every item after this one into a different slot.
	Slots[Slot] = FCataclysmCarriedSlot();

	// LEAVING COUNTS AS WELL AS ARRIVING. Section 6 says "an item entering or
	// leaving the inventory", and a record written only on arrival would hand
	// back an item the player had put down.
	NoteChanged();
	return true;
}

bool UCataclysmInventoryComponent::RestoreSlots(const TArray<FCataclysmCarriedSlot>& Saved)
{
	if (Saved.Num() != SlotCount)
	{
		return false;
	}

	// GEAR STAYS IN ITS CELL; A STACK IN A SAVED SLOT GOES TO ITS STORE, onto what the store holds, and its cell is left
	// empty (#2849). See the header.
	Slots.SetNum(SlotCount);
	for (int32 Slot = 0; Slot < SlotCount; ++Slot)
	{
		const FCataclysmCarriedSlot& From = Saved[Slot];
		Slots[Slot] = FCataclysmCarriedSlot();
		Slots[Slot].Item = From.Item;
		if (!SlotIsEmpty(From.Item) || From.Quantity <= 0)
		{
			continue;
		}
		if (!From.Material.IsNone())
		{
			PutMaterial(From.Material, From.Quantity);
		}
		else if (!From.Gem.IsNone())
		{
			PutGems(From.Gem, From.GemRarity, From.Quantity);
		}
	}
	++Changes;
	return true;
}

void UCataclysmInventoryComponent::RestoreStacks(const TArray<FCataclysmCarriedSlot>& Materials,
												 const TArray<FCataclysmCarriedSlot>& Gems)
{
	MaterialStacks.Reset();
	GemStacks.Reset();
	for (const FCataclysmCarriedSlot& Stack : Materials)
	{
		if (!Stack.Material.IsNone() && Stack.Quantity > 0)
		{
			PutMaterial(Stack.Material, Stack.Quantity);
		}
	}
	for (const FCataclysmCarriedSlot& Stack : Gems)
	{
		if (!Stack.Gem.IsNone() && Stack.Quantity > 0)
		{
			PutGems(Stack.Gem, Stack.GemRarity, Stack.Quantity);
		}
	}
	++Changes;
}

void UCataclysmInventoryComponent::RemoveEverything()
{
	for (FCataclysmCarriedSlot& Slot : Slots)
	{
		Slot = FCataclysmCarriedSlot();
	}
	MaterialStacks.Reset();
	GemStacks.Reset();

	// UNCONDITIONALLY, EVEN WHEN NOTHING WAS CARRIED. A screen reading
	// this only needs to know it cannot trust what it built last time, and
	// an extra rebuild costs one frame's work while a missed one shows the
	// player a tool tip for an item they no longer have.
	++Changes;
}

const FCataclysmItem* UCataclysmInventoryComponent::ItemAt(int32 Slot) const
{
	if (!Slots.IsValidIndex(Slot) || SlotIsEmpty(Slots[Slot].Item))
	{
		return nullptr;
	}
	return &Slots[Slot].Item;
}
