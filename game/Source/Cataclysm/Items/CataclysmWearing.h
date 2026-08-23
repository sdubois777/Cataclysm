// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CataclysmWearing.generated.h"

class UCataclysmInventoryComponent;

/** What taking an item out of the bag and putting it on, or the reverse, did. */
UENUM(BlueprintType)
enum class ECataclysmWearResult : uint8
{
	/** It went on and nothing came off. */
	Worn				UMETA(DisplayName = "Worn"),

	/** It went on and what came off is now carried. */
	Swapped				UMETA(DisplayName = "Worn, and what came off is carried"),

	/** It came off and is now carried. */
	TakenOff			UMETA(DisplayName = "Taken off"),

	/** That carried slot is empty, or holds a crafting material. */
	NothingToWear		UMETA(DisplayName = "Nothing there to wear"),

	/** That gear slot is empty. */
	NothingWorn			UMETA(DisplayName = "Nothing worn there"),

	/** Its base names no slot this character has. */
	CannotBeWorn		UMETA(DisplayName = "It goes in no slot"),

	/**
	 * Refused, because carrying out the change would have destroyed an item.
	 *
	 * NOT A FAILURE OF THE BAG BEING FULL AS SUCH. A one-for-one swap works with
	 * a full bag, because the slot the item came out of is free by the time
	 * anything needs putting back. This is the case where more would come off
	 * than there is room for -- a two-handed weapon replacing two one-handed
	 * ones -- and the design's rule that an item which will not fit stays where
	 * it is rather than vanishing.
	 */
	NoRoomInTheBag		UMETA(DisplayName = "No room in the bag"),

	/**
	 * Refused, because it is the only weapon worn and a character must hold one.
	 *
	 * A CHOICE RATHER THAN A STOPGAP, AND THIS COMMENT SAID OTHERWISE UNTIL
	 * 2026-08-23. It claimed the refusal was waiting on four weapon types to be
	 * given Demonic skills. THAT WILL NEVER HAPPEN. The design document's table
	 * of which weapon types each damage type covers gives Demonic exactly ten,
	 * and 2H Crossbow, Crossbow, Shield and Spear are deliberately not among
	 * them; it adds that Demonic's ten "are now designed". So the stated
	 * condition for removing this described something that was never going to
	 * arrive, which is worse than giving no reason at all, because the next
	 * person reads it as a plan.
	 *
	 * WHAT IS ACTUALLY TRUE. A character holding no weapon has no weapon skills,
	 * because UCataclysmWeaponSlotsComponent grants the six ability slots from
	 * the worn weapon's type. That is equally true of a Demonic character
	 * holding a Spear, and the design accepts THAT deliberately --
	 * Cataclysm.WeaponSlots.AWeaponItsDamageTypeDoesNotCoverOffersNothing
	 * asserts it on purpose so that nobody "fixes" it later.
	 *
	 * SO THE OPEN QUESTION IS ONLY WHETHER AN UNARMED CHARACTER SHOULD HAVE NO
	 * SKILLS, and issue #841 is where that is decided. Letting the slots empty
	 * was recommended when this was built and the project owner chose this
	 * instead. It stays or goes on its own merits, not on any content arriving.
	 *
	 * SWAPPING IS NOT REFUSED, only taking the last one off. Wearing a different
	 * weapon over this one goes through WearFromCarried and never reaches here.
	 */
	TheLastWeapon		UMETA(DisplayName = "That is your only weapon"),

	/** One of the two components was missing. */
	NothingToWorkWith	UMETA(DisplayName = "Nothing to work with"),

	// ADDED AT THE END RATHER THAN BESIDE WHAT THEY RESEMBLE, so every value
	// above keeps the number it had. Issue #853.

	/** It is on the cursor now, and still in its slot. */
	PickedUp			UMETA(DisplayName = "Picked up"),

	/** It went into the cell, which was empty. */
	PutDown				UMETA(DisplayName = "Put down"),

	/** It went into the cell and what was there took its place. */
	Exchanged			UMETA(DisplayName = "Put down, and the two changed places"),

	/** That cell is empty, so there is nothing there to pick up. */
	NothingToPickUp		UMETA(DisplayName = "Nothing there to pick up"),

	/** Asked to put something down with nothing on the cursor. */
	NothingHeld			UMETA(DisplayName = "Nothing is being held"),

	/** Asked to pick something up while already holding something. */
	AlreadyHolding		UMETA(DisplayName = "Something is already held"),
};

/**
 * Moving an item between the bag and what is worn, without ever losing one.
 *
 * WHY THIS IS ITS OWN CLASS. Two things ask for it and they must not disagree:
 * the `Cataclysm.Equip` console command added with the equipment slots in issue
 * #828, and a click on the inventory screen's gear panel, issue #831. The first
 * version had the whole rule written inside the console command, so the screen
 * would have had a second copy of a rule whose failure mode is a destroyed item.
 *
 * NOTHING HERE TOUCHES A WIDGET, so all of it is covered by ordinary automation
 * tests. That matters more than usual: the automation command in
 * `tools/unreal_build.py` passes `-nullrhi` and no test in this project can watch
 * a widget draw (issue #559), so a rule left inside the screen is a rule nothing
 * checks.
 *
 * THE ONE RULE WORTH STATING TWICE. **An item is never destroyed.** Every path
 * below either completes, leaving every item either worn or carried, or changes
 * nothing at all. `UCataclysmInventoryComponent::AddItem` answers where an item
 * went rather than assuming it went anywhere, and every use of it here reads
 * that answer.
 */
UCLASS()
class CATACLYSM_API UCataclysmWearing : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Take the item out of a carried slot and put it on.
	 *
	 * Whatever comes off goes into the bag. The slot the item came out of is
	 * emptied first, so a one-for-one swap always has somewhere to put the old
	 * piece even when the bag was full.
	 *
	 * A TWO-HANDED WEAPON CAN TAKE TWO WEAPONS OFF FOR ONE GOING ON, which needs
	 * a second free slot and is the only case that can be refused for room. It
	 * is refused before anything moves rather than half-done and rolled back:
	 * a rollback has its own failure modes and this has one honest answer.
	 *
	 * @param OutSlot  where the item ended up. Untouched when nothing was worn.
	 */
	static ECataclysmWearResult WearFromCarried(
		UCataclysmInventoryComponent* Inventory,
		UCataclysmEquipmentComponent* Equipment,
		int32 CarriedSlot,
		ECataclysmGearSlot& OutSlot);

	/**
	 * Take off what is worn in a gear slot and put it in the bag.
	 *
	 * REFUSED WHEN THE BAG IS FULL, rather than taking the item off and leaving
	 * it nowhere. There is no floor to drop it on from a screen.
	 *
	 * @param OutCarriedSlot  which carried slot the piece landed in.
	 *                        Untouched unless the result is TakenOff.
	 */
	static ECataclysmWearResult TakeOffInto(
		UCataclysmInventoryComponent* Inventory,
		UCataclysmEquipmentComponent* Equipment,
		ECataclysmGearSlot Slot,
		int32* OutCarriedSlot = nullptr);

	/**
	 * Put a carried slot on the cursor. Issue #853.
	 *
	 * NOTHING MOVES. The item stays in its slot and only the cursor changes,
	 * so this cannot fail part way and cannot lose anything. See
	 * UCataclysmInventoryComponent::HeldSlot for why that is the design.
	 *
	 * A CRAFTING MATERIAL CAN BE PICKED UP TOO, unlike wearing, because moving
	 * a stack to a tidier cell is the same gesture and refusing it would be a
	 * rule the player has to learn for no reason.
	 */
	static ECataclysmWearResult PickUpCarried(
		UCataclysmInventoryComponent* Inventory, int32 CarriedSlot);

	/**
	 * Put what is on the cursor into a carried slot, exchanging with whatever
	 * is there. Issue #853.
	 *
	 * AN OCCUPIED CELL IS AN EXCHANGE AND NOT A REFUSAL, which is what every
	 * game in the genre does and what makes the grid rearrangeable at all.
	 * Nothing enters or leaves the bag, so nothing can be destroyed.
	 *
	 * PUTTING IT BACK WHERE IT CAME FROM IS ALLOWED and is how a player
	 * cancels. It reports PutDown, because from the player's side that is what
	 * happened.
	 */
	static ECataclysmWearResult PutDownCarried(
		UCataclysmInventoryComponent* Inventory, int32 CarriedSlot);

	/**
	 * Take a worn piece off into the bag and put it on the cursor. Issue #853.
	 *
	 * THE SAME REFUSALS AS TakeOffInto, because it is TakeOffInto: a full bag
	 * and a character's last weapon both stop it, and neither leaves the piece
	 * anywhere but where it started.
	 */
	static ECataclysmWearResult PickUpWorn(
		UCataclysmInventoryComponent* Inventory,
		UCataclysmEquipmentComponent* Equipment,
		ECataclysmGearSlot Slot);

	/**
	 * Take whatever is on the cursor off it, leaving it in its slot.
	 *
	 * WHAT THE INVENTORY SCREEN CLOSING DOES. Nothing moves and nothing can be
	 * lost, because the item never left the bag to begin with.
	 */
	static void ReleaseHeld(UCataclysmInventoryComponent* Inventory);

	/** What a result should say to somebody, in one line. */
	static FString Explain(ECataclysmWearResult Result);
};
