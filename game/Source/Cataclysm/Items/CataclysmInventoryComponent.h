// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Items/CataclysmItem.h"
#include "CataclysmInventoryComponent.generated.h"

/**
 * What a character carries into a dungeon: 48 slots, and nothing makes it 49.
 *
 * THE SIZE IS FIXED BY DESIGN AND NOT BY CONVENIENCE. The Storage section of
 * `docs/Cataclysm_GDD_v2.md` says 48 slots, four rows of twelve, one item per
 * slot, and that no empire node, no affix and no city upgrade grants another.
 * The decision of 2026-08-05 in `docs/DECISIONS.md` gives the reasoning: Diablo
 * IV fixes its inventory at 33 and states why, Path of Exile's grid never grows,
 * and a scaling source would weaken a pressure this design created on purpose --
 * a dungeon floor costs a day, so how much can be carried is part of how deep it
 * is worth going. **48 is a tuning value; that it does not change is the rule.**
 *
 * SO ADDING AN ITEM CAN FAIL, AND THAT IS THE INTERESTING BRANCH. The decision
 * of 2026-08-14 settled what a full inventory means: there is no way out of a
 * dungeon partway through, so an item that will not fit **stays on the floor**
 * and the player chooses what is worth a slot. AddItem therefore answers where
 * the item went rather than assuming it went anywhere, and a caller that ignores
 * the answer has silently destroyed an item.
 *
 * A FIXED ARRAY OF SLOTS RATHER THAN A LIST THAT GROWS TO 48. A slot is a place,
 * not just a unit of capacity: the design draws four rows of twelve, the save
 * design reserves "48 inventory slots" in an ordinary character record, and every
 * game in the genre lets a player leave a gap where they like. Storing 48 entries
 * from the start means a gap is representable now, so the interface that lets a
 * player arrange items does not need the store to change shape underneath it.
 *
 * AN EMPTY SLOT IS ONE WITH NO BASE. `FCataclysmItem::Base` names a row in the
 * ItemBases table and every rolled item has one; an item without a base cannot be
 * named, valued or drawn, so it is not an item. See SlotIsEmpty.
 *
 * WHAT IS DELIBERATELY NOT HERE.
 *
 * - **The stash.** A different container with different rules -- 600 slots, six
 *   tabs, shared per lethality mode, and it never enters a dungeon. Issue #529
 *   has to exist first for it to persist.
 * - **The inventory screen.** Drawing the grid is interface work, issue #49.
 * - **Equipping out of it**, issue #46, and **saving it**, issue #529.
 * - **Replication.** Co-op is issue #56 and nothing in this project replicates
 *   yet; adding it here alone would be one replicated system in a game with none.
 */
UCLASS(ClassGroup = (Cataclysm), meta = (BlueprintSpawnableComponent))
class CATACLYSM_API UCataclysmInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** Rows in the carried grid. Four, from the Storage section. */
	static constexpr int32 Rows = 4;

	/** Slots across one row. Twelve, which is Path of Exile's grid width. */
	static constexpr int32 Columns = 12;

	/**
	 * How many items a character can carry. 48.
	 *
	 * WRITTEN AS A NUMBER RATHER THAN AS `Rows * Columns` so that
	 * `tools/tests/test_carried_inventory_is_forty_eight_slots.py` can read it
	 * out of this header as text. Continuous integration compiles no C++ at all,
	 * so a `static_assert` here would never run on a pull request and a Python
	 * test that reads the source does. The assert below still holds the three
	 * constants together for anyone who does compile it.
	 */
	static constexpr int32 SlotCount = 48;

	static_assert(Rows * Columns == SlotCount,
				  "The carried grid is four rows of twelve and holds 48 items. "
				  "Changing one of these three without the others makes the "
				  "grid and the capacity disagree.");

	UCataclysmInventoryComponent();

	/**
	 * Whether a slot holds nothing.
	 *
	 * A STATIC, because the drop spawner and the interface both need to ask the
	 * question about a loose item rather than about a slot, and neither should
	 * have to know that "no base" is how emptiness is spelled.
	 */
	static bool SlotIsEmpty(const FCataclysmItem& Item);

	/** How many of the 48 slots hold an item. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Inventory")
	int32 NumItems() const;

	/** How many slots are free. Never negative and never above 48. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Inventory")
	int32 NumFreeSlots() const;

	/** True when every slot holds an item, so the next drop stays on the floor. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Inventory")
	bool IsFull() const;

	/** The lowest-numbered free slot, or INDEX_NONE when there is none. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Inventory")
	int32 FirstFreeSlot() const;

	/**
	 * Puts an item in the lowest-numbered free slot.
	 *
	 * @return the slot it went into, or **INDEX_NONE when there was no room**,
	 *         in which case nothing was stored and the caller still owns the
	 *         item. Leaving it on the floor is what the design asks for.
	 *
	 * THE LOWEST FREE SLOT, so a gap left by a removal is refilled before the
	 * end of the grid is used. That is what the genre does and it is what stops
	 * a player's inventory turning into 48 sparse entries after a few removals.
	 *
	 * AN ITEM WITH NO BASE IS REFUSED rather than stored, because storing one
	 * would occupy a slot that every count here would then read as free.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Inventory")
	int32 AddItem(const FCataclysmItem& Item);

	/**
	 * Empties a slot.
	 *
	 * @return whether there was anything in it. False for an empty slot and for
	 *         a slot number outside the grid, so a caller cannot mistake either
	 *         for a successful removal.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Inventory")
	bool RemoveItemAt(int32 Slot);

	/** Empties every slot. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Inventory")
	void RemoveEverything();

	/**
	 * The item in a slot, or nullptr when the slot is empty or is not a slot.
	 *
	 * NOT REACHABLE FROM BLUEPRINT, because a pointer to a struct is not a thing
	 * Blueprint can hold. The screen that needs this is issue #49 and it can take
	 * a copy through GetSlots.
	 */
	const FCataclysmItem* ItemAt(int32 Slot) const;

	/** Every slot in order, the empty ones included. Always 48 long. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Inventory")
	const TArray<FCataclysmItem>& GetSlots() const { return Slots; }

private:
	/**
	 * The 48 slots, filled with empty items at construction and never resized.
	 *
	 * Nothing here changes its length, so `Slots.Num()` is 48 for the whole life
	 * of the component and every slot number from 0 to 47 is addressable whether
	 * or not anything is in it.
	 */
	UPROPERTY()
	TArray<FCataclysmItem> Slots;
};
