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
 * a dungeon floor costs a day to begin with, so how much can be carried is part
 * of how deep it is worth going. **48 is a tuning value; that it does not change is the rule.**
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
 * SINCE ISSUE #2849 THE 48 SLOTS HOLD GEAR ONLY, and crafting materials and gems are carried in two stores of their
 * own, one stack per material and one per gem and rarity, with no limit. The project owner, 2026-11-22: "I think we need
 * to add tabs to the inventory. One for crafting materials and one for gems. Otherwise you wont be able to carry any
 * loot", and then, of the shape, "One cell per kind, no limit (Recommended)". So the pressure the paragraphs above
 * describe is now a pressure on gear: a material or a gem is never refused for want of room, and never takes a gear
 * slot. Diablo IV's Materials and Socketables tabs work this way.
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
 * One slot of the carried inventory or of the stash: a gear item, a stack of one crafting material, a stack of one gem
 * at one rarity, or nothing.
 *
 * SINCE ISSUE #2849 A CARRIED GEAR SLOT HOLDS GEAR ONLY, and a carried stack is an entry of
 * `UCataclysmInventoryComponent`'s material or gem store, of this same type so the screen, the tool tips and the save
 * draw and write it as before. Until #2849 one bag held all three, so that everything competed for the same 48; the
 * owner ruled on 2026-11-22 that materials and gems are carried apart. THE STASH STILL HOLDS ALL THREE in this type
 * (`Stash`, `PrivateStash`); issue #529.
 *
 * A MATERIAL IS A NAME AND A COUNT, NOT AN ITEM. It has no affixes, no upgrade
 * level, no sockets and no residue; two of the same material are the same thing
 * twice. That is why they stack and gear cannot.
 *
 * A GEM IS A NAME, A RARITY AND A COUNT, for the same reason. Ruled by the
 * coordinating session on 2026-11-21 (issue #46): gems stack, one stack per gem
 * and rarity, because a gem of one rarity is "a quantity of an interchangeable
 * thing", which is the design's reason for stacking materials. Two rarities of
 * one gem are different things and take two slots.
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
	 * Which gem is stacked here, as a row key in `game/Data/Gems.csv`. None when
	 * the slot holds no gem. Issue #46, slice G2.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Inventory")
	FName Gem;

	/** The rarity of the gems stacked here. Read only when `Gem` names one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Inventory")
	ECataclysmRarity GemRarity = ECataclysmRarity::Everyday;

	/**
	 * How many of that material, or of that gem, are stacked here. Zero when the
	 * slot holds neither.
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

/**
 * The three tabs of the carried inventory. Issue #2849, ruled 2026-11-22: gear in the 48 slots, crafting materials and
 * gems each in a store of their own.
 */
UENUM(BlueprintType)
enum class ECataclysmInventoryTab : uint8
{
	Gear,
	Materials,
	Gems,
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

	/** How many of the 48 gear slots hold an item. A material or gem stack is not counted: it has no slot (#2849). */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Inventory")
	int32 NumItems() const;

	/** How many gear slots are free. Never negative and never above 48. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Inventory")
	int32 NumFreeSlots() const;

	/** True when every gear slot holds an item, so the next gear drop stays on the floor. Materials and gems are still
	 *  taken (#2849). */
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

	/**
	 * Puts a changed copy of the item in a slot back in that same slot: the forge raising a carried piece a gear level
	 * (`UCataclysmForgeCraft::Upgrade`). Issue #2742's follow-up, ruled 2026-11-19.
	 *
	 * IN PLACE, so the piece keeps its cell: a remove and an add would move it to the first free cell.
	 *
	 * REFUSED, CHANGING NOTHING, for a slot holding no item -- an empty one, a material's, one outside the grid -- and
	 * for a replacement of another base, which is a different item rather than this one changed.
	 *
	 * IT RAISES NO SAVE TRIGGER, and counts a change. The forge raises `ForgeCrafted`, which writes the character record
	 * this slot is in; raising `InventoryChanged` here too would ask for the same write twice.
	 */
	bool ReplaceItemAt(int32 Slot, const FCataclysmItem& Changed);

	/** Empties every slot. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Inventory")
	void RemoveEverything();

	/**
	 * Puts the 48 saved gear slots back exactly, each in the position it was saved in. Issue #753, the character step.
	 *
	 * REFUSED, CHANGING NOTHING, when the saved list does not hold exactly `SlotCount` slots: the shape is checked and
	 * the contents are not, because the save is the authority for what was carried. IT RAISES NO SAVE TRIGGER, unlike
	 * every other change to the bag: putting back what the file holds is not a change the file needs to record.
	 *
	 * A STACK FOUND IN A SAVED SLOT GOES TO ITS STORE, added to any stack of that kind already there, and the slot is
	 * left empty (#2849). A record of version 16 holds none there, because the upgrade step to version 16 (removed 2026-11-27) moved them out; a test
	 * that builds a record by hand may, and nothing a player carried is lost either way.
	 */
	bool RestoreSlots(const TArray<FCataclysmCarriedSlot>& Saved);

	/**
	 * Puts the saved material and gem stores back, replacing what is carried there. Issue #2849.
	 *
	 * Each entry is a stack: a material name or a gem name and rarity, and a quantity. An entry with neither, or with a
	 * quantity of nought or less, is dropped; two entries of one kind are added together. IT RAISES NO SAVE TRIGGER, for
	 * the reason `RestoreSlots` gives.
	 *
	 * CALL IT BEFORE `RestoreSlots`: this empties both stores first, and `RestoreSlots` adds to them any stack it finds
	 * in a saved gear slot.
	 */
	void RestoreStacks(const TArray<FCataclysmCarriedSlot>& Materials, const TArray<FCataclysmCarriedSlot>& Gems);

	/**
	 * Adds crafting materials to the material store, onto any of that material already carried.
	 *
	 * @return whether they were added: false only for a material with no name or for none of it. **NEVER FALSE FOR WANT
	 *         OF ROOM**: the store has one stack per material and no limit, the owner's ruling of 2026-11-22 (#2849).
	 *         Until #2849 this returned the bag slot the stack took, and a new material was refused when every slot was
	 *         full.
	 *
	 * BY NAME AND NOT BY TIER. Four materials share tier 1 and they are different things; stacking by tier would merge
	 * them. All crafting materials stack, decided by the project owner on 2026-08-19.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Inventory")
	bool AddMaterial(FName Material, int32 Quantity);

	/** How many of a crafting material are carried. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Inventory")
	int32 CountOfMaterial(FName Material) const;

	/**
	 * Takes this many of a crafting material out of its stack. Issues #1820 and #41.
	 *
	 * @return whether they were taken. **ALL OR NOTHING**: fewer carried than asked takes none, so a price is paid
	 *         whole or not at all. A stack taken to zero leaves the store.
	 *
	 * Built for Forced Tithes, whose altar takes materials as one of its prices. It is the first thing in the game that
	 * spends a material; crafting will be the second.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Inventory")
	bool RemoveMaterial(FName Material, int32 Quantity);

	/**
	 * Adds gems of one kind and rarity to the gem store, onto any of that kind and rarity already carried. Issue #46,
	 * slice G2; a store of its own since #2849.
	 *
	 * @return whether they were added: false only for a gem with no name or for none of one. NEVER FALSE FOR WANT OF
	 *         ROOM, as `AddMaterial`. ONE STACK PER GEM AND RARITY, ruled 2026-11-21, so an Everyday Of The Viper does
	 *         not join a Superb one. The store is kept sorted by gem, then rarity, which is the order the gems tab shows
	 *         (Q7 of #2849).
	 */
	bool AddGems(FName Gem, ECataclysmRarity Rarity, int32 Quantity);

	/** How many gems of this kind and rarity are carried. */
	int32 CountOfGems(FName Gem, ECataclysmRarity Rarity) const;

	/**
	 * Takes this many gems of one kind and rarity out of their stack. Issue #46, slice G4: the forge's Socket Gem.
	 *
	 * ALL OR NOTHING, as `RemoveMaterial` is: fewer carried than asked takes none. A stack taken to nought leaves the
	 * store.
	 */
	bool RemoveGems(FName Gem, ECataclysmRarity Rarity, int32 Quantity);

	/** Every carried material stack, one per material, in the order first carried. Issue #2849. */
	const TArray<FCataclysmCarriedSlot>& GetMaterialStacks() const { return MaterialStacks; }

	/** Every carried gem stack, one per gem and rarity, sorted by gem then rarity. Issue #2849. */
	const TArray<FCataclysmCarriedSlot>& GetGemStacks() const { return GemStacks; }

	/**
	 * The item in a gear slot, or nullptr when the slot is empty or is not a slot.
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
	 * Exchange what two carried slots hold. Issue #853.
	 *
	 * NOTHING ENTERS OR LEAVES THE BAG, so this cannot destroy an item however
	 * it is called. Both entries are copies in a fixed-length array and the
	 * exchange is total; there is no state where one slot holds both.
	 *
	 * SWAPPING A SLOT WITH ITSELF IS ALLOWED AND CHANGES NOTHING, because it
	 * is what putting a held item back where it came from does, and refusing
	 * it would make the caller special-case the commonest gesture.
	 *
	 * @return false when either index is outside the bag, having changed
	 *         nothing
	 */
	bool SwapSlots(int32 First, int32 Second);

	/**
	 * Which carried slot is on the cursor, or INDEX_NONE for none.
	 *
	 * THE ITEM DOES NOT LEAVE ITS SLOT WHILE IT IS HELD, and that is the whole
	 * design. A held item is drawn as though it were on the cursor, but it is
	 * still one of the 48 and is still what FCataclysmSaveGather writes out, so
	 * a save taken mid-drag loses nothing and there is no third place for an
	 * item to be. UCataclysmWearing's rule that an item is never destroyed
	 * holds here for free rather than needing to be extended to cover a
	 * cursor.
	 *
	 * NOT SaveGame, and that is deliberate. What is held is where a cursor is
	 * pointing rather than anything about the character, and a save that
	 * restored a half-finished drag would be restoring the mouse.
	 */
	int32 HeldSlot() const { return Held; }

	/** Whether anything is on the cursor. */
	bool IsHolding() const { return Held != INDEX_NONE; }

	/**
	 * Put a carried slot on the cursor, or take it off the cursor with
	 * INDEX_NONE.
	 *
	 * @return false when the index is neither a slot in the bag nor
	 *         INDEX_NONE, having changed nothing
	 */
	bool HoldSlot(int32 Slot);

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

	/**
	 * The carried slot on the cursor, or INDEX_NONE. See HeldSlot above for
	 * why this is not persisted and why the item stays in its slot.
	 */
	int32 Held = INDEX_NONE;

	UPROPERTY()
	TArray<FCataclysmCarriedSlot> Slots;

	/** The material store: one entry per material carried, its `Material` and `Quantity` set and nothing else. #2849. */
	UPROPERTY()
	TArray<FCataclysmCarriedSlot> MaterialStacks;

	/** The gem store: one entry per gem and rarity carried, sorted by gem then rarity, its `Gem`, `GemRarity` and
	 *  `Quantity` set and nothing else. #2849. */
	UPROPERTY()
	TArray<FCataclysmCarriedSlot> GemStacks;

	/** Where a material's stack is in `MaterialStacks`, or INDEX_NONE. */
	int32 StackOfMaterial(FName Material) const;

	/** Where a gem's stack is in `GemStacks`, or INDEX_NONE. */
	int32 StackOfGems(FName Gem, ECataclysmRarity Rarity) const;

	/** Adds to a material's stack, or starts one. No count, no save trigger: the callers do that. */
	void PutMaterial(FName Material, int32 Quantity);

	/** Adds to a gem stack, or starts one where the order by gem then rarity puts it. No count, no save trigger. */
	void PutGems(FName Gem, ECataclysmRarity Rarity, int32 Quantity);

	/** One change to what is carried: counted, and the inventory save trigger raised. */
	void NoteChanged();
};
