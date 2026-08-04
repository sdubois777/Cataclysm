// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
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

	/**
	 * Grants an ability into a slot the CALLER names, rather than the slot the
	 * ability class declares.
	 *
	 * WHY THIS EXISTS ALONGSIDE UCataclysmAbilitySet. An ability set reads each
	 * ability's own declared slot, which is right for a starting kit where every
	 * ability knows where it belongs. It cannot express what a weapon does: the
	 * equipped weapon decides which skill sits in each of the six slots, so the
	 * slot is a property of the pairing and not of the ability. It is also what
	 * lets one placeholder class stand in for six different slots while the real
	 * skills are undesigned.
	 *
	 * @return the granted spec's handle, or an invalid handle if nothing was
	 *         granted, which happens on a client, with no ability class, or with
	 *         a slot of None.
	 */
	FGameplayAbilitySpecHandle GiveAbilityInSlot(
		TSubclassOf<UGameplayAbility> AbilityClass,
		ECataclysmAbilitySlot Slot,
		int32 Level = 1,
		UObject* SourceObject = nullptr);

	// -- The three-bucket stat pipeline's modifiers -------------------------
	//
	// WHY THESE LIVE HERE AND NOT AS ATTRIBUTES. UCataclysmStatPipeline scopes
	// every modifier by the tags of the SKILL IN HAND, so a character's
	// increased fire damage has no single value: it is one number for a skill
	// tagged Element.Demonic and another for one that is not. A gameplay
	// attribute is one number per character, so it cannot express that. This
	// list is what an aggregated attribute would be if the engine could scope by
	// the ability being used.
	//
	// WHAT PUTS THINGS IN IT. Today only a skill's own self buff, which adds on
	// activation and removes when its duration expires. Gear, gems and passive
	// keystones will add to the same list; issue #166 is the first thing that
	// needed a route into it at all.

	/**
	 * Add a modifier and get a handle that takes it away again.
	 *
	 * @return a handle, always positive, or 0 if the modifier was refused.
	 *         UCataclysmStatPipeline::ValidateModifier decides; a More
	 *         multiplier from a source that may not grant one is refused here
	 *         rather than silently ignored at evaluation time.
	 */
	int32 AddStatModifier(const FCataclysmStatModifier& Modifier);

	/** Take a modifier away. False when the handle is unknown or already gone. */
	bool RemoveStatModifier(int32 Handle);

	/** Change a live modifier's magnitude. False when the handle is unknown. */
	bool SetStatModifierValue(int32 Handle, float NewValue);

	/** A live modifier's magnitude, or zero when the handle is unknown. */
	float GetStatModifierValue(int32 Handle) const;

	/**
	 * Every modifier this character currently carries, for the pipeline.
	 *
	 * Returned by reference and not by value because a hit evaluates this on
	 * every target it touches.
	 */
	const TArray<FCataclysmStatModifier>& GetStatModifiers() const
	{
		return StatModifiers;
	}

protected:
	/** Slots pressed since the last ProcessAbilityInput. Not replicated; local input only. */
	TArray<FGameplayAbilitySpecHandle> InputPressedSpecHandles;

	/** Slots released since the last ProcessAbilityInput. */
	TArray<FGameplayAbilitySpecHandle> InputReleasedSpecHandles;

	/**
	 * The modifiers themselves, and the handle of each, at the same index.
	 *
	 * TWO ARRAYS RATHER THAN ONE ARRAY OF PAIRS so that GetStatModifiers can
	 * hand the pipeline exactly what it takes without building a copy on every
	 * hit. The two are always the same length; every function that changes one
	 * changes the other in the same statement.
	 */
	TArray<FCataclysmStatModifier> StatModifiers;
	TArray<int32> StatModifierHandles;

	/** Never reused, so a stale handle cannot remove somebody else's modifier. */
	int32 NextStatModifierHandle = 1;
};
