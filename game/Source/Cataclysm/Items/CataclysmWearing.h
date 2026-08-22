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

	/** One of the two components was missing. */
	NothingToWorkWith	UMETA(DisplayName = "Nothing to work with"),
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
	 */
	static ECataclysmWearResult TakeOffInto(
		UCataclysmInventoryComponent* Inventory,
		UCataclysmEquipmentComponent* Equipment,
		ECataclysmGearSlot Slot);

	/** What a result should say to somebody, in one line. */
	static FString Explain(ECataclysmWearResult Result);
};
