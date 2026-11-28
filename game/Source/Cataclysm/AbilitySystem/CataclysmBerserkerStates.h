// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class ACataclysmCharacterBase;
class UAbilitySystemComponent;
class UCataclysmAbilitySystemComponent;
struct FGameplayTagContainer;

/**
 * THE BERSERKER'S STATES: Wrath, Frenzy and Berserking. `plan_death_and_war.md` slice B29, 2026-11-25. The three nodes:
 *
 *   - Wrath (`Berserker_twoh_001`): "Two-handed weapons only. When you reach maximum Fervour, enter a state of Wrath.
 *     While in Wrath your melee attacks deal +15% damage. Fervour drains at 5 per second. Wrath ends when Fervour
 *     reaches 0."
 *   - Frenzy (`Berserker_dw_001`): "Dual wield only. When you reach maximum Fervour, enter a state of Frenzy. While in
 *     Frenzy your attack speed is increased by 20% and all melee hits generate 2 additional Fervour. Fervour drains at
 *     8 per second. Frenzy ends when Fervour reaches 0."
 *   - Berserking (`Berserker_trunk_009`): "When you reach maximum Fervour your next melee attack consumes all Fervour.
 *     For each point of Fervour consumed that attack deals +10% bonus damage per point invested." It fires only outside
 *     Wrath and Frenzy: the owner's answer to Q21, 2026-11-24.
 *
 * WHAT IS DATA AND WHAT IS HERE. Every bonus a state gives is a row stated under `in_wrath` or `in_frenzy`, so this
 * file holds no figure of the design's. What it holds is the three things no row can say: when a state begins (the pool
 * reaching its maximum, with the node's flag row holding), when it ends (the pool reaching nought, or the flag row no
 * longer holding, which is a weapon swap), and Berserking's spend of the whole pool on the next melee attack.
 *
 * A DRAIN IS A ROW TOO. `fervour_drain_per_second` takes that much Fervour a second whenever a row grants it, so "Fervour
 * drains at 5 per second" is the Wrath node's row under `in_wrath`, and a later node draining more while a bonus is held
 * (Titan's Grip, Warmonger, Steeled Resolve) is another row under its own condition. Unlike the out-of-combat decay it
 * waits for nothing.
 *
 * GENRE PRECEDENT for a state that drains its resource and ends when it runs out: Path of Exile's Berserk, which spends
 * Rage each second while its buff holds (poedb.tw/us/Berserk), and Rage Vortex of Berserking, which "ends if you run out"
 * of Rage (poedb.tw/us/Rage_Vortex_of_Berserking). The numbers are the design's own sentences.
 *
 * A PLAIN CLASS OF STATIC FUNCTIONS, not a UCLASS, as `FCataclysmWarFervour` is: nothing in Blueprint calls these.
 */
class CATACLYSM_API FCataclysmBerserkerStates
{
public:
	/** A flag: 1 or more and the character enters Wrath when its pool reaches the maximum. The Wrath node's row, stated
	 *  under `wielding_two_handed_weapon`, so "Two-handed weapons only" is the row's own condition. */
	static const TCHAR* WrathOnFullFervourStat;

	/** The same for Frenzy, stated under `dual_wielding`. */
	static const TCHAR* FrenzyOnFullFervourStat;

	/** Increased damage, in percent, for each point of Fervour Berserking consumes. Scoped to `Type.Melee`, so only a
	 *  melee attack asks it with an answer above nought. */
	static const TCHAR* BerserkingDamagePerFervourStat;

	/** Berserking spends half the pool rather than all of it: Berserker's Rage, the project owner's ruling of 2026-11-27.
	 *  A flag. The War trees' engine slice E3. */
	static const TCHAR* BerserkingSpendsHalfStat;

	/** Berserking fires on every melee attack outside Wrath and Frenzy, armed or not: Bloodied's "Berserking triggers on
	 *  every melee hit rather than only when Fervour is full", under its condition. A flag. The War trees' engine slice E3. */
	static const TCHAR* BerserkingOnEveryMeleeHitStat;

	/** Fervour taken a second, by any row granting it, under any condition. */
	static const TCHAR* DrainPerSecondStat;

	/** The words the list of what affects the player shows: "Wrath", "Frenzy", and "Berserking ready" while Berserking
	 *  waits for the next melee attack. */
	static const TCHAR* WrathText;
	static const TCHAR* FrenzyText;
	static const TCHAR* BerserkingReadyText;

	/**
	 * Begin or end a state, or arm Berserking, from what the character holds now.
	 *
	 *   - A STATE ENDS when the pool is at or below nought, or when the flag row that let it begin no longer holds.
	 *   - AT A FULL POOL (at or above `MaximumClassResourceAsked`, above nought) with no state held, the character enters
	 *     Wrath if its flag holds, else Frenzy if its flag holds, else arms Berserking if it holds the Berserking row.
	 *     Wrath is asked first only so the order is fixed: a character meets both weapon rules only under Both Hands
	 *     Full, a Ravager option.
	 *   - BERSERKING IS DISARMED by any state, and by the character no longer holding its row.
	 *
	 * CALLED WHEN THE POOL MOVES (`ACataclysmPlayerCharacter::OnClassResourceChanged`) and every regeneration step
	 * (`DrainStep`), so a weapon swap, which moves no pool, is seen within a quarter of a second.
	 *
	 * @return whether the state or Berserking changed
	 */
	static bool Judge(UCataclysmAbilitySystemComponent* Cataclysm);

	/**
	 * One regeneration step: take `DrainPerSecondStat` times the step from the pool, clamped at nought, and judge the
	 * state again. Not scaled by `class_resource_generation`: a drain is not a gain. Refuses a creature holding a player
	 * build (the Corrupted Stalker), as both decays do.
	 *
	 * @return the change written to the pool: nought or negative
	 */
	static float DrainStep(ACataclysmCharacterBase* Character, float SecondsInStep);

	/**
	 * Berserking's spend, at the start of an attack: when armed, outside both states, on an attack that struck at least
	 * one enemy and whose skill tags reach the Berserking row, consume the whole pool and disarm.
	 *
	 * A NON-MELEE ATTACK LEAVES IT ARMED: the row is scoped to `Type.Melee`, so it reads nought there and nothing is
	 * spent. AN ATTACK THAT STRUCK NOBODY SPENDS NOTHING (a judgement: a swing at the air is not "that attack deals").
	 *
	 * @return the increased damage, in percent, the attack gains: the row's figure times the Fervour consumed
	 */
	static float SpendBerserking(UAbilitySystemComponent* AbilitySystem, const FGameplayTagContainer& SkillTags,
								 int32 TargetsStruck);

	/** The words for the state `Actor` is in, or Berserking ready, or empty. For `UCataclysmPlayerEffects`. */
	static FString StateTextFor(const AActor* Actor);
};
