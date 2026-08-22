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
/**
 * One slot of the carried inventory: a gear item, a stack of one crafting
 * material, or nothing.
 *
 * ONE BAG HOLDS BOTH, because the design says so: the carried inventory is 48
 * slots and a character carries whatever it has picked up in them. A second
 * store for materials would be a second bag with a second capacity, and the
 * pressure the design wants -- a full inventory being a choice about what to
 * leave behind -- only exists if everything competes for the same 48.
 *
 * A MATERIAL IS A NAME AND A COUNT, NOT AN ITEM. It has no affixes, no upgrade
 * level, no sockets and no residue; two of the same material are the same thing
 * twice. That is why they stack and gear cannot.
 *
 * EVERY FIELD HERE IS MARKED `SaveGame` for the reason FCataclysmItem gives: a
 * carried slot is persisted inside a character record, and a field without the
 * flag is dropped from the save silently. Issue #529.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmCarriedSlot
{
	GENERATED_BODY()

	/**
	 * The gear item in this slot. Its Base is None when the slot holds no gear.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Inventory")
	FCataclysmItem Item;

	/**
	 * Which crafting material is stacked here, as a row key in
	 * `game/Data/CraftingMaterials.csv`. None when the slot holds no material.
	 *
	 * THE MATERIAL'S NAME AND NOT ITS TIER. Four materials share tier 1 and they
	 * are different things -- a Corrupted Mote is not a Whispering Ash -- so
	 * stacking by tier would merge four materials into one pile.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Inventory")
	FName Material;

	/**
	 * How many of that material are stacked here. Zero when the slot holds no
	 * material.
	 *
	 * NO CEILING. The project owner decided on 2026-08-19 that all crafting
	 * materials stack, and set no maximum. The Storage section of
	 * `docs/Cataclysm_GDD_v2.md` records that an unbounded stack means materials
	 * never contribute to a full inventory, and that a ceiling is the lever if
	 * carrying them should ever cost something.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Inventory")
	int32 Quantity = 0;
};

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
	 * Whether a loose gear item is really an item.
	 *
	 * A STATIC, because the drop spawner and the interface both need to ask the
	 * question about a loose item rather than about a slot, and neither should
	 * have to know that "no base" is how emptiness is spelled.
	 */
	static bool SlotIsEmpty(const FCataclysmItem& Item);

	/** Whether a slot of the bag holds neither gear nor a material stack. */
	static bool SlotIsEmpty(const FCataclysmCarriedSlot& Slot);

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
	 * Adds crafting materials, stacking them onto any already carried.
	 *
	 * @return the slot they went into, or **INDEX_NONE when there was no room**,
	 *         in which case nothing was stored.
	 *
	 * A STACK FIRST, A FREE SLOT SECOND. All crafting materials stack, decided
	 * by the project owner on 2026-08-19, so a second Corrupted Mote joins the
	 * first rather than taking a slot of its own. Only a material nothing is
	 * carrying yet needs a slot, which is what stops a Cataclysm Boss's
	 * twenty-four materials filling half the bag.
	 *
	 * BY NAME AND NOT BY TIER. Four materials share tier 1 and they are
	 * different things; stacking by tier would merge them.
	 *
	 * AN EXISTING STACK ALWAYS HAS ROOM, because no ceiling is set. So this can
	 * only fail for a material not already carried, and only when every slot is
	 * full.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Inventory")
	int32 AddMaterial(FName Material, int32 Quantity);

	/** How many of a crafting material are carried, across every slot. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Inventory")
	int32 CountOfMaterial(FName Material) const;

	/**
	 * The slot a crafting material is stacked in, or INDEX_NONE for none.
	 *
	 * ONE STACK PER MATERIAL. AddMaterial always joins the stack it finds, so
	 * there is never a second one to find.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Inventory")
	int32 SlotOfMaterial(FName Material) const;

	/**
	 * The item in a slot, or nullptr when the slot is empty, holds a material,
	 * or is not a slot.
	 *
	 * NOT REACHABLE FROM BLUEPRINT, because a pointer to a struct is not a thing
	 * Blueprint can hold. The screen that needs this is issue #49 and it can take
	 * a copy through GetSlots.
	 */
	const FCataclysmItem* ItemAt(int32 Slot) const;

	/** Every slot in order, the empty ones included. Always 48 long. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Inventory")
	const TArray<FCataclysmCarriedSlot>& GetSlots() const { return Slots; }

	/**
	 * How many times the contents have changed, for a screen that redraws
	 * every frame and should not redo work every frame.
	 *
	 * WHY A COUNT RATHER THAN COMPARING THE SLOTS. UCataclysmInventoryWidget
	 * refreshes from NativeTick, so anything it works out per cell it works
	 * out 48 times a frame. An item's tool tip is a dozen table lookups and a
	 * string join, and it changes when the player picks something up rather
	 * than when the frame advances. Comparing the slots to spot that would
	 * cost almost as much as rebuilding them.
	 *
	 * IT IS NOT A SAVED FIELD AND MUST NOT BECOME ONE. It says nothing about
	 * what is carried, only that it differs from a moment ago, and a loaded
	 * character starting again from zero is correct.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Inventory")
	int32 ChangeCount() const { return Changes; }

private:
	/**
	 * The 48 slots, empty at construction and never resized.
	 *
	 * Nothing here changes its length, so `Slots.Num()` is 48 for the whole life
	 * of the component and every slot number from 0 to 47 is addressable whether
	 * or not anything is in it.
	 */
	/**
	 * Raised by every function that changes what is carried. See ChangeCount.
	 *
	 * DELIBERATELY NOT SaveGame. Every other field on this component carries
	 * that flag because an inventory is persisted inside a character record;
	 * this one describes the session rather than the character.
	 */
	int32 Changes = 0;

	UPROPERTY()
	TArray<FCataclysmCarriedSlot> Slots;
};
