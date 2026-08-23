// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Items/CataclysmItem.h"

#include "CataclysmEquipmentComponent.generated.h"

class UAbilitySystemComponent;
class UDataTable;

/**
 * Every place a character can wear one item.
 *
 * WHERE THE LIST COMES FROM. The Item Slots section of
 * `docs/Cataclysm_GDD_v2.md`: seven armour pieces, eight rings, a necklace, a
 * relic, and weapons. `game/Data/ItemBases.csv` carries a `Slot` column whose
 * values are exactly Head, Chest, Shoulders, Gloves, Pants, Boots, Belt,
 * Necklace, Relic, Ring and Weapon, so the data and the document already agree
 * and this enum is the third statement of the same list.
 *
 * EIGHT RINGS ARE WRITTEN OUT RATHER THAN COUNTED. A slot is a place a player
 * puts something, and the interface has to name each one; a ring count with an
 * index would make "which ring did I take off" a thing the caller computes.
 * `UCataclysmGearSlots::RingSlots` is the list, so nothing hard-codes eight.
 *
 * TWO WEAPON SLOTS AND NEITHER IS THE MAIN HAND. The Item Slots section states
 * it outright: this game blends two weapons into one swing, summing their base
 * damage and averaging their attack speed, so "there is no primary hand" and the
 * pair stays unordered. The two entries are interchangeable, and which one an
 * item lands in carries no meaning. A two-handed weapon occupies both -- see
 * UCataclysmEquipmentComponent::TwoHandedOccupiesBothWeaponSlots.
 *
 * THE FOUR POTION SLOTS ARE DELIBERATELY ABSENT. The design lists them under
 * Consumables and they are not gear: a potion is a thing you drink, its
 * behaviour is undesigned, and it grants no stats to accumulate. Four entries
 * nothing could ever fill would make every count and every test here report a
 * character as part-equipped forever. Issue #806 is where potions are asked for.
 */
UENUM(BlueprintType)
enum class ECataclysmGearSlot : uint8
{
	Head		UMETA(DisplayName = "Head"),
	Chest		UMETA(DisplayName = "Chest"),
	Shoulders	UMETA(DisplayName = "Shoulders"),
	Gloves		UMETA(DisplayName = "Gloves"),
	Pants		UMETA(DisplayName = "Pants"),
	Boots		UMETA(DisplayName = "Boots"),
	Belt		UMETA(DisplayName = "Belt"),

	Necklace	UMETA(DisplayName = "Necklace"),
	Relic		UMETA(DisplayName = "Relic"),

	Ring1		UMETA(DisplayName = "Ring 1"),
	Ring2		UMETA(DisplayName = "Ring 2"),
	Ring3		UMETA(DisplayName = "Ring 3"),
	Ring4		UMETA(DisplayName = "Ring 4"),
	Ring5		UMETA(DisplayName = "Ring 5"),
	Ring6		UMETA(DisplayName = "Ring 6"),
	Ring7		UMETA(DisplayName = "Ring 7"),
	Ring8		UMETA(DisplayName = "Ring 8"),

	Weapon1		UMETA(DisplayName = "Weapon 1"),
	Weapon2		UMETA(DisplayName = "Weapon 2"),

	Count		UMETA(Hidden),
};

/**
 * What a request to wear something did.
 *
 * IT ANSWERS RATHER THAN ASSUMES, for the same reason
 * `UCataclysmInventoryComponent::AddItem` does. An equip can fail, and a caller
 * that treats failure as success has taken an item out of the bag and put it
 * nowhere, which destroys it.
 */
UENUM(BlueprintType)
enum class ECataclysmEquipResult : uint8
{
	/** It went on, and nothing came off. */
	Equipped				UMETA(DisplayName = "Equipped"),

	/** It went on and what was already there came off. See the swapped-out item. */
	Swapped					UMETA(DisplayName = "Equipped, and something came off"),

	/** The item has no base, so it is not an item. */
	NotAnItem				UMETA(DisplayName = "Not an item"),

	/** Its base names no slot, or names one this character does not have. */
	NoSlotForThisItem		UMETA(DisplayName = "No slot for this item"),

	/** A slot was named explicitly and this item does not belong in it. */
	WrongSlotForThisItem	UMETA(DisplayName = "Wrong slot for this item"),

	/** The item bases table could not be read, so nothing could be judged. */
	NoBaseTable				UMETA(DisplayName = "The item bases table is missing"),
};

/**
 * What a character is wearing, and the one thing that makes gear matter.
 *
 * WHY THIS EXISTS. Issue #828. Before it, loot dropped, went into the 48-slot
 * carried bag, and could never be worn: there was no slot to put it in and
 * nothing would have happened if there were. Every character at a given level
 * was identical, whatever they found.
 *
 * **ALMOST NONE OF THE WORK IS HERE, AND THAT IS THE POINT.** The parts were all
 * built and tested already and simply had nothing joining them:
 *
 *   `UCataclysmItemModifiers::ModifiersFor` turns one item into stat modifiers.
 *   It existed with **no caller anywhere outside the automation tests**.
 *
 *   `UCataclysmItemModifiers::AccumulateInto` sums those across several items.
 *   Same.
 *
 *   `UCataclysmStatPipeline::Evaluate` combines a base value with modifiers
 *   through the three-bucket rule. At runtime it was used only for skill damage.
 *
 *   `UCataclysmPlayerClassStats::ApplyTo` writes stats onto the ability system.
 *   It wrote the class stat line and nothing else.
 *
 * This component holds the items, hands them to the first two, and hands the
 * result to the last two. It computes nothing about what an affix is worth.
 *
 * WHAT IT DOES NOT DO. Gear levelling, sockets and gems (#46). Enchantments
 * (#45). Tooltips (#733). Potions. Moving items around the bag. It also does not
 * decide what the player sees; `UCataclysmEquipmentScreen` does that, for the
 * same reason `UCataclysmInventoryScreen` is separate from its widget -- the
 * automation test command passes `-nullrhi`, so nothing that reaches the screen
 * can be watched by a test.
 *
 * NOTHING HERE NEEDS RENDERING, so unlike the effects work every rule in this
 * file is covered by an ordinary automation test. Issue #559 does not apply.
 */
UCLASS(ClassGroup = (Cataclysm), meta = (BlueprintSpawnableComponent))
class CATACLYSM_API UCataclysmEquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCataclysmEquipmentComponent();

	/** How many places there are to wear something. */
	static constexpr int32 SlotCount = static_cast<int32>(ECataclysmGearSlot::Count);

	// -- what is worn ------------------------------------------------------

	/** The item in a slot, or null when it is empty or the slot is out of range. */
	const FCataclysmItem* EquippedAt(ECataclysmGearSlot Slot) const;

	/** Whether a slot holds an item. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Equipment")
	bool SlotIsEmpty(ECataclysmGearSlot Slot) const;

	/** How many slots hold an item. A two-handed weapon counts once. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Equipment")
	int32 NumEquipped() const;

	/** Every slot, in the enum's order. Always SlotCount long. */
	const TArray<FCataclysmItem>& GetSlots() const { return Slots; }

	/**
	 * Whether the weapon slots hold one two-handed weapon.
	 *
	 * THE ITEM IS STORED ONCE, IN Weapon1, AND Weapon2 IS LEFT EMPTY. Storing it
	 * in both would be the obvious way to say "it fills both hands" and it would
	 * be wrong: every accumulation over the slots would count the weapon's
	 * affixes twice, and a two-handed weapon already gets its own doubling from
	 * `UCataclysmItemValues::TwoHandedMultiplier`. So the second slot is empty
	 * and blocked rather than occupied, and this is how anything asks.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Equipment")
	bool TwoHandedOccupiesBothWeaponSlots() const;

	// -- changing what is worn ---------------------------------------------

	/**
	 * Wear an item, choosing the slot from its base.
	 *
	 * A ring goes in the first free ring slot, and in Ring1 when all eight are
	 * full. A one-handed weapon goes in the first free weapon slot. Anything
	 * else has exactly one slot to go in.
	 *
	 * TWO ITEMS CAN COME OFF FOR ONE GOING ON, and forgetting the second one
	 * destroys it. A two-handed weapon put on while two one-handed weapons are
	 * held takes both. The first version of this function called EquipInto
	 * with a local variable for the second and threw the local away, so one
	 * weapon silently vanished; nothing else in the project would have
	 * noticed, which is why
	 * Cataclysm.Equipment.ATwoHandedWeaponHandsBackBothWeaponsItReplaced
	 * exists.
	 *
	 * @param OutRemoved      what came off, if anything. Untouched otherwise.
	 * @param OutAlsoRemoved  the second thing that came off, for the two-handed
	 *                        case. Untouched otherwise.
	 * @param OutSlot         where it went.
	 */
	ECataclysmEquipResult Equip(const FCataclysmItem& Item,
								FCataclysmItem& OutRemoved,
								FCataclysmItem& OutAlsoRemoved,
								ECataclysmGearSlot& OutSlot);

	/**
	 * Wear an item in a named slot, refusing if it does not belong there.
	 *
	 * NEEDED SEPARATELY FROM Equip BECAUSE A PLAYER CHOOSES. With eight ring
	 * slots and two weapon slots, "put this one here" is a thing the interface
	 * has to be able to say, and picking the slot for them would move a ring
	 * they did not ask to move.
	 *
	 * A TWO-HANDED WEAPON PUT IN EITHER WEAPON SLOT LANDS IN Weapon1, because
	 * there is no primary hand and both slots mean the same thing. Whatever was
	 * in either hand comes off; only the first of the two is reported in
	 * OutRemoved, and OutAlsoRemoved carries the second.
	 */
	ECataclysmEquipResult EquipInto(const FCataclysmItem& Item,
									ECataclysmGearSlot Slot,
									FCataclysmItem& OutRemoved,
									FCataclysmItem& OutAlsoRemoved);

	/**
	 * Take the item out of a slot.
	 *
	 * @return whether there was one to take.
	 */
	bool Unequip(ECataclysmGearSlot Slot, FCataclysmItem& OutRemoved);

	/** Take everything off. Used by tests and by a character being rebuilt. */
	void UnequipEverything();

	// -- what it is all for ------------------------------------------------

	/**
	 * Every stat modifier the worn items grant, summed.
	 *
	 * Keyed by stat name, which is what `UCataclysmPlayerClassStats::
	 * StatToAttribute` turns into a gameplay attribute. An empty map is a
	 * legitimate answer and means the character is wearing nothing that grants
	 * anything, which is different from an error.
	 */
	TMap<FName, TArray<FCataclysmStatModifier>> GatherModifiers() const;

	/**
	 * Which weapon type the character is holding, for the ability slots.
	 *
	 * WHY IT LIVES HERE. `UCataclysmWeaponSlotsComponent` fills the seven
	 * ability slots from a weapon TYPE, and until issue #828 that type was a
	 * hard-coded string on the component with no item behind it. It is now read
	 * off the item actually worn, which is what makes a dropped weapon change
	 * what the player can do.
	 *
	 * WITH TWO ONE-HANDED WEAPONS IT ANSWERS THE FIRST OCCUPIED SLOT'S, AND
	 * THAT IS THE WRONG SHAPE RATHER THAN THE WRONG CHOICE. The design was
	 * settled on 2026-08-22 and it is not "pick a weapon": both weapons
	 * contribute their skills to one pool and the player assigns any of them
	 * to any slot. The Skill Slots section of docs/Cataclysm_GDD_v2.md said
	 * so before this function was written; issue #829 was raised calling it
	 * undefined and was wrong.
	 *
	 * SO THIS FUNCTION SHOULD NOT EXIST IN THE END. It is what lets
	 * UCataclysmWeaponSlotsComponent keep working while it still takes one
	 * weapon type, and it goes when issue #837 gives the player the choice.
	 * A matched pair -- same weapon type, same damage types -- is unaffected
	 * either way, because both weapons contribute the same skills.
	 *
	 * @return the weapon type, or an empty string when no weapon is worn.
	 */
	FString EquippedWeaponType() const;

	/**
	 * Recompute every stat from the class line plus what is worn, and write it.
	 *
	 * THE POOLS ARE NOT REFILLED, and that is the difference between this and a
	 * character arriving in the world. `UCataclysmPlayerClassStats::ApplyTo`
	 * fills health, mana and shield to their maximums, which is right at
	 * possession and wrong here: a player who swapped a helmet mid-fight would
	 * be healed to full by doing it.
	 *
	 * @return how many attributes were written.
	 */
	int32 RefreshAttributes(UAbilitySystemComponent* AbilitySystem) const;

	/** Fires after anything changes what is worn, before attributes are written. */
	DECLARE_MULTICAST_DELEGATE(FOnEquipmentChanged);
	FOnEquipmentChanged EquipmentChanged;

private:
	/**
	 * One entry per slot, always SlotCount long, an empty item meaning nothing
	 * is worn there.
	 *
	 * A FIXED ARRAY RATHER THAN A MAP, matching UCataclysmInventoryComponent: a
	 * slot is a place rather than a unit of capacity, an empty one has to be
	 * representable, and the interface draws all of them whether or not
	 * anything is in them.
	 */
	UPROPERTY(SaveGame)
	TArray<FCataclysmItem> Slots;

	/** Puts the item in, reports what came out, and raises the change. */
	void PlaceInto(const FCataclysmItem& Item, ECataclysmGearSlot Slot,
				   FCataclysmItem& OutRemoved);
};

/**
 * Which slots an item may go in, and what each is called.
 *
 * SEPARATE FROM THE COMPONENT because all of it is a judgement over plain values
 * with no actor involved, which is the same split
 * `UCataclysmInventoryScreen` and `UCataclysmItemValues` already use. Every rule
 * below is testable without building a world.
 */
UCLASS()
class CATACLYSM_API UCataclysmGearSlots : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Every slot, in the enum's order. */
	static const TArray<ECataclysmGearSlot>& AllSlots();

	/** The eight ring slots, in order. */
	static const TArray<ECataclysmGearSlot>& RingSlots();

	/** The two weapon slots, in order. */
	static const TArray<ECataclysmGearSlot>& WeaponSlots();

	/** Whether a slot is one of the eight ring slots. */
	static bool IsRingSlot(ECataclysmGearSlot Slot);

	/** Whether a slot is one of the two weapon slots. */
	static bool IsWeaponSlot(ECataclysmGearSlot Slot);

	/** What to call a slot on screen. */
	static FString DisplayName(ECataclysmGearSlot Slot);

	/**
	 * The `Slot` value an item base must carry to go in this slot.
	 *
	 * All eight ring slots answer "Ring" and both weapon slots answer "Weapon",
	 * which is what makes one column in `game/Data/ItemBases.csv` serve eighteen
	 * places.
	 */
	static FString BaseSlotFor(ECataclysmGearSlot Slot);

	/**
	 * Every slot an item with this base `Slot` value could go in, in order.
	 *
	 * Empty when the value names nothing this character has, which is not an
	 * error worth logging: a consumable's base names a slot this enum does not
	 * carry, on purpose.
	 */
	static TArray<ECataclysmGearSlot> CandidateSlotsFor(const FString& BaseSlot);
};
