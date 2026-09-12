// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmMovement.generated.h"

class ACataclysmCharacterBase;

/**
 * Whether a character is moving, how long it has stood still, and how far it
 * has walked since its own last attack. Issue #41, slice 2.
 *
 * WHY IT EXISTS. Rows across the data ask about a character's movement and
 * nothing could answer. Two dungeon modifiers of issue #41 need it -- Forced
 * March, "you take stacking damage if you stand still for >3s", and The
 * Nihil's Embrace, "as you move, your resistances are slowly and permanently
 * reduced" -- and so do a long list of enchantment rows and passive nodes,
 * from "while moving you deal increased damage" to "your first melee attack
 * after moving 5 metres deals 50% increased damage".
 *
 * WHAT IT DOES NOT DO. It keeps no state of its own. The readings live on the
 * character's `UCataclysmAbilitySystemComponent`, beside the other "when did
 * this last happen" clocks, and this class only samples the position and tells
 * that component what it saw.
 *
 * MOVING MEANS A CHANGE OF POSITION, whatever caused it: walking, a movement
 * skill that travels, a charge, a shove or a pull. That is the rule a Path of
 * Exile developer stated for the same question -- "if it changes, for any
 * reason, you moved" -- and it is recorded as a judgement in
 * `docs/DECISIONS.md` with its source.
 *
 * AN INSTANT RELOCATION IS NOT MOVEMENT, and it cannot be told from a very
 * fast walk by sampling position. So each place that relocates a character at
 * once calls `UCataclysmAbilitySystemComponent::NoteRelocatedInstantly` itself
 * rather than being inferred here: a blink, a recall, a position swap, a
 * Flicker, the Phasewalker enemy modifier, and the placement of the player at
 * a floor's start.
 */
UCLASS()
class CATACLYSM_API UCataclysmMovement : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * How far a character has to move in one sample to count as moving, in
	 * metres.
	 *
	 * IT EXISTS TO STOP AN ANIMATION COUNTING AS MOVEMENT. A character playing a
	 * swing or a hit reaction drifts a little without the player asking it to,
	 * and without a threshold that drift would read as walking and would cancel
	 * every bonus for standing still. Diablo IV players reported exactly that
	 * failure against its standing-still bonus, which is the reason this number is
	 * here rather than zero.
	 *
	 * FIVE CENTIMETRES IN A QUARTER OF A SECOND, which is twenty centimetres a
	 * second: far below any walking speed in this game and far above animation
	 * drift. A JUDGEMENT, recorded in `docs/DECISIONS.md`: no game in the genre
	 * publishes such a figure.
	 */
	static constexpr float MovedMetresThreshold = 0.05f;

	/** Centimetres in a metre. Unreal measures in centimetres; rows say metres. */
	static constexpr float CentimetresPerMetre = 100.0f;

	/**
	 * Look at where this character is, compare it with where the last sample saw
	 * it, and tell its ability system component what changed.
	 *
	 * A TWELFTH JOB ON THE 0.25-SECOND STEP rather than a timer of its own, for
	 * the reason the eleven before it give: that step already runs several times a
	 * second for the player, every creature and every minion, and a timer per
	 * character is one more thing to cancel when one dies.
	 *
	 * IT NEEDS NO INTERVAL. A distance is a distance; only the clocks care about
	 * time, and the component stamps those from the world.
	 *
	 * THE FIRST SAMPLE ONLY REMEMBERS WHERE THE CHARACTER IS. There is nothing to
	 * compare against yet, so it reports standing still, which also starts the
	 * clock: a character that never moves then reads a real number of seconds
	 * rather than "never".
	 */
	static void SampleStep(ACataclysmCharacterBase* Character);
};
