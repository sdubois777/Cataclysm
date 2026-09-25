// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CataclysmSecondSelf.generated.h"

class AActor;

/**
 * A Second Self, the Ritualist's `Ritualist_capstone_200` option 1: "The minion
 * you have held longest becomes your equal: it has your Maximum Health, your
 * Spell Damage and your Area of Effect, and reserves twice the Fervour it would.
 * When it is gone, the next longest-held takes its place." Issue #1515.
 *
 * RULED 2026-09-25, UNDER THE OWNER'S DELEGATION, and resting on the ruling that
 * a passive whose own sentence names minions may pass a summoner stat to them:
 *
 *   - HELD LONGEST is the earliest `ACataclysmCharacterBase::CommandedSinceSeconds`
 *     among the commander's living minions and thralls: stamped when a minion is
 *     summoned and when an enemy is subjugated. Ties go to the nearest. A
 *     deployable -- a minion whose type row's family is Machine -- is neither a
 *     minion held nor a thrall and is never chosen. Once chosen, it stays chosen
 *     until it is gone; the next is chosen on the next step.
 *   - MAXIMUM HEALTH follows the summoner's live, as Shared Blood's shield does,
 *     and the chosen one keeps the same fraction of it, so a rise is not a heal.
 *   - SPELL DAMAGE is copied onto the chosen one's own attribute. It stays a
 *     spell stat, so it does nothing for an imp, which has no combat attribute
 *     set and no spell, and matters only to a thrall's `Type.Spell` abilities.
 *   - AREA OF EFFECT multiplies the chosen one's explosion radius and its
 *     Shared Ruin death blast, the only areas a minion has.
 *   - TWICE THE FERVOUR: a chosen thrall counts as two in the thrall limit.
 *     Nothing reads an imp's reserve yet (#1934), so for one this does nothing.
 *
 * GENRE: no shipped mechanic was found that copies the player's own maximum life
 * or spell damage onto one minion, so the shape is this design's own.
 */
UCLASS()
class CATACLYSM_API UCataclysmSecondSelf : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Above zero means the option is held. Its row will carry 1. */
	static const TCHAR* Stat;

	/**
	 * `Commander`'s Second Self: the one already chosen if it still stands and
	 * still follows, otherwise the longest-held minion or thrall. Null when it
	 * has none. Asks nothing about whether the option is held.
	 */
	static AActor* Choose(const AActor* Commander);

	/**
	 * One step of the commander's: when it holds the option, choose its Second
	 * Self, mark it, and give it the commander's maximum health and spell
	 * damage. Called from the commander's regeneration step.
	 *
	 * @return the one chosen, or null
	 */
	static AActor* Step(AActor* Commander);

	/** Whether `Follower` is its commander's Second Self right now. */
	static bool IsSecondSelf(const AActor* Follower);

	/**
	 * What `Follower`'s areas are multiplied by: its commander's area of effect
	 * multiplier when it is the Second Self, and 1 otherwise.
	 */
	static float AreaMultiplierFor(const AActor* Follower);

	/**
	 * How many thrall shares beyond one each the commander's Second Self claims:
	 * 1 when it is a thrall, or when there is none yet and the next one taken
	 * would be chosen; 0 otherwise, and 0 without the option.
	 */
	static int32 ExtraThrallShares(const AActor* Commander);
};
