// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CataclysmGearPanel.generated.h"

struct FCataclysmItem;
class UDataTable;

/**
 * Every judgement the panel of worn gear makes, outside the widget.
 *
 * WHY THIS EXISTS. Issue #831. Equipment landed in issue #828 and the only way
 * to use it was three console commands: the inventory screen drew the 48 carried
 * cells and had nowhere to show what a character was wearing.
 *
 * WHY IT IS A SEPARATE CLASS FROM THE WIDGET, which is the same reason
 * `UCataclysmInventoryScreen` and `UCataclysmItemTooltip` are. The automation
 * test command in `tools/unreal_build.py` passes `-nullrhi`, so nothing that
 * reaches the screen can be watched by a test. Everything here is a static
 * function over plain values.
 *
 * WHY IT IS SEPARATE FROM `UCataclysmInventoryScreen` AS WELL, which is less
 * obvious. That class is about a grid of 48 interchangeable cells whose contents
 * are decided by what the player put where. This one is about 19 named places,
 * each of which accepts one kind of thing and says what it is when empty. The
 * two share a cell's colours and sizes and share nothing else.
 */
UCLASS()
class CATACLYSM_API UCataclysmGearPanel : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** How many columns the panel is laid out in. */
	static constexpr int32 Columns = 3;

	/**
	 * Where a slot sits in the panel.
	 *
	 * THREE COLUMNS BY WHAT A THING IS, not nineteen cells in reading order.
	 * The armour goes down the first column head to foot, the eight rings fill
	 * the second, and the weapons take the third. A single run of nineteen would
	 * put a ring between a belt and a necklace, and the eight rings are half the
	 * panel: broken across a wrap they stop reading as a set.
	 *
	 * THE COLUMNS ARE RAGGED AND THAT IS FINE. A uniform grid simply has no
	 * widget at a position nothing claims.
	 */
	static void PlacementFor(ECataclysmGearSlot Slot, int32& OutRow,
							 int32& OutColumn);

	/**
	 * What a slot's cell says.
	 *
	 * The worn item's base name when something is worn, and the slot's own name
	 * when nothing is. **An empty slot says what it is for rather than being
	 * blank**, because nineteen blank squares tell a player nothing about where
	 * a ring goes; the carried grid can be blank because every one of its cells
	 * accepts anything.
	 */
	static FString LabelFor(ECataclysmGearSlot Slot, const FCataclysmItem* Worn,
							const UDataTable* BaseTable);

	/** What the panel's own heading reads, given how many slots are filled. */
	static FString HeaderTextFor(int32 Worn, int32 Slots);

	/**
	 * Whether a slot's cell should be drawn as filled.
	 *
	 * SEPARATE FROM "SOMETHING IS WORN THERE" BY ONE CASE. The second weapon
	 * slot holds nothing while a two-handed weapon is held, and it is not free:
	 * the weapon occupies both hands and is stored once so its affixes are not
	 * counted twice. A player looking at an empty second hand would reasonably
	 * try to put something in it.
	 */
	static bool SlotIsBlocked(ECataclysmGearSlot Slot,
							  const UCataclysmEquipmentComponent* Equipment);
};
