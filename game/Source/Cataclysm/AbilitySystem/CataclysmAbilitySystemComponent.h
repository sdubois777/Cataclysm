// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "CataclysmAbilitySystemComponent.generated.h"

struct FGameplayTag;

/**
 * The project's ability system component.
 *
 * Exists as a subclass from the start so that behaviour common to every actor
 * with abilities has somewhere to live without a later refactor touching every
 * character class.
 *
 * It also owns the input side of the ability system. A key press does not name
 * an ability; it names a SLOT, as a Slot.* gameplay tag. Whichever granted
 * ability carries that tag is the one that runs. See CataclysmAbilitySlots in
 * CataclysmGameplayAbility.h for where the slot list and the tag list meet.
 */
UCLASS()
class CATACLYSM_API UCataclysmAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UCataclysmAbilitySystemComponent();

	/** Records that the key for this slot went down. Does not activate anything yet. */
	void AbilityInputTagPressed(const FGameplayTag& InputTag);

	/** Records that the key for this slot came up. */
	void AbilityInputTagReleased(const FGameplayTag& InputTag);

	/**
	 * Activates whatever the presses recorded this frame asked for, then clears
	 * the record. Called once per frame from the player controller.
	 *
	 * WHY THIS IS DEFERRED RATHER THAN ACTIVATING ON THE KEY PRESS ITSELF.
	 * Enhanced Input fires its delegates part-way through the frame, and more
	 * than one can fire before the frame's input is complete. Doing the work at
	 * one known point means every ability activated in a frame sees the same
	 * state, rather than a result that depends on the order the engine happened
	 * to deliver two key presses in.
	 */
	void ProcessAbilityInput();

	/**
	 * Forgets every press and release not yet processed, and tells every running
	 * ability its input was released.
	 *
	 * Needed when input stops arriving for a reason the ability cannot see: the
	 * pawn is unpossessed, the player opens a menu, the game pauses. Without it
	 * an ability waiting on a key release waits for one that will never come.
	 */
	void ClearAbilityInput();

protected:
	/** Slots pressed since the last ProcessAbilityInput. Not replicated; local input only. */
	TArray<FGameplayAbilitySpecHandle> InputPressedSpecHandles;

	/** Slots released since the last ProcessAbilityInput. */
	TArray<FGameplayAbilitySpecHandle> InputReleasedSpecHandles;
};
