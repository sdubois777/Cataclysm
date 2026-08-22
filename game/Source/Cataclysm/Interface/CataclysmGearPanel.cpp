// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmGearPanel.h"

#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Items/CataclysmItem.h"

void UCataclysmGearPanel::PlacementFor(ECataclysmGearSlot Slot, int32& OutRow,
									   int32& OutColumn)
{
	// THE ARMOUR COLUMN, HEAD TO FOOT. Written out rather than taken from the
	// enum's order, because the enum lists the belt after the boots and a
	// player reading a column expects it above them.
	static const TArray<ECataclysmGearSlot> Armour = {
		ECataclysmGearSlot::Head,
		ECataclysmGearSlot::Shoulders,
		ECataclysmGearSlot::Chest,
		ECataclysmGearSlot::Gloves,
		ECataclysmGearSlot::Belt,
		ECataclysmGearSlot::Pants,
		ECataclysmGearSlot::Boots,
		ECataclysmGearSlot::Necklace,
		ECataclysmGearSlot::Relic,
	};

	if (const int32 Index = Armour.IndexOfByKey(Slot); Index != INDEX_NONE)
	{
		OutRow = Index;
		OutColumn = 0;
		return;
	}

	if (const int32 Index = UCataclysmGearSlots::RingSlots().IndexOfByKey(Slot);
		Index != INDEX_NONE)
	{
		OutRow = Index;
		OutColumn = 1;
		return;
	}

	if (const int32 Index = UCataclysmGearSlots::WeaponSlots().IndexOfByKey(Slot);
		Index != INDEX_NONE)
	{
		OutRow = Index;
		OutColumn = 2;
		return;
	}

	// A SLOT ADDED TO THE ENUM AND NOT TO A COLUMN LANDS OFF THE PANEL RATHER
	// THAN ON TOP OF SOMETHING. Cataclysm.GearPanel.EverySlotHasAPlaceOfItsOwn
	// is what stops that reaching a build.
	OutRow = INDEX_NONE;
	OutColumn = INDEX_NONE;
}

FString UCataclysmGearPanel::LabelFor(ECataclysmGearSlot Slot,
									  const FCataclysmItem* Worn,
									  const UDataTable* BaseTable)
{
	if (Worn && BaseTable)
	{
		if (const FCataclysmItemBaseRow* Base =
				BaseTable->FindRow<FCataclysmItemBaseRow>(
					Worn->Base, TEXT("UCataclysmGearPanel::LabelFor"),
					/*bWarnIfMissing=*/false))
		{
			// THE BASE'S NAME, THE SAME AS THE CARRIED GRID SHOWS. The whole
			// name is in the tool tip; a cell has room for one word.
			return Base->BaseName;
		}
	}

	// THE SLOT'S OWN NAME WHEN NOTHING IS WORN. The carried grid leaves an empty
	// cell blank because every one of its cells accepts anything; here a blank
	// square would not say where a ring goes.
	return UCataclysmGearSlots::DisplayName(Slot);
}

FString UCataclysmGearPanel::HeaderTextFor(int32 Worn, int32 Slots)
{
	return FString::Printf(TEXT("Worn  %d / %d"), Worn, Slots);
}

bool UCataclysmGearPanel::SlotIsBlocked(
	ECataclysmGearSlot Slot, const UCataclysmEquipmentComponent* Equipment)
{
	if (!Equipment)
	{
		return false;
	}

	// THE ONE CASE. A two-handed weapon is stored in the first weapon slot
	// alone, so the second reads as empty while both hands are full. Saying so
	// is the difference between a player seeing a free hand and seeing why they
	// have not got one.
	return Slot == ECataclysmGearSlot::Weapon2
		&& Equipment->TwoHandedOccupiesBothWeaponSlots();
}
