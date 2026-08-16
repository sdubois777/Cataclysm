// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmRegeneration.generated.h"

class AActor;

/**
 * Health, mana and energy shield coming back over time.
 *
 * THE THREE ATTRIBUTES ALREADY EXISTED AND NOTHING READ THEM. HealthRegen,
 * ManaRegen and EnergyShieldRegen are declared on UCataclysmVitalAttributeSet,
 * initialised, clamped and replicated, and before this class no code anywhere
 * added anything to a pool. Mana therefore only ever went down: every ability
 * subtracts its cost and refuses to activate without it, so a play session
 * ended with every ability permanently refused. The only thing that restored
 * mana was dying, because ACataclysmPlayerCharacter::Revive fills all three
 * pools. Issue #653, reported from play.
 *
 * A FLAT AMOUNT PER SECOND, WHICH IS THE DESIGN'S OWN SHAPE rather than a
 * choice made here. docs/Cataclysm_GDD_v2.md, under Stat Calculation: "The base
 * regeneration rate is a small flat value per second, supplied the same way
 * base health is. This applies to health, mana and energy shield regeneration
 * alike." The percentages players collect are increases to that base --
 * `Final = Base x (1 + increases)` -- and not percentages of the maximum, which
 * the same passage spells out because reading them the other way would have 50
 * points of Vitality returning half a character's health every second.
 *
 * The design's own check on the figures: a Heavy attack used the moment it
 * returns costs 10 mana per second against the 10.9 per second a character
 * regenerates at level 100, so the primary damage button is affordable from
 * regeneration alone. That still holds at the level the sandbox runs at.
 *
 * THE ENERGY SHIELD IS THE ONE WITH A DELAY, and it is the design's number
 * rather than a guess. Its section says the shield "refills 3 seconds after the
 * character last took damage", that taking damage again inside that window
 * restarts the wait, and that damage over time restarts it as well. That last
 * part is load-bearing: the shield does not absorb damage over time at all, so
 * without it a bleeding character would refill their shield freely and the
 * shield would be strongest against the one thing it ignores.
 *
 * Health and mana have no such delay. Nothing in the design gives them one, and
 * the enchantment that proves the shield's delay exists -- "regeneration begins
 * immediately after taking damage with no delay" -- names only the shield.
 */
UCLASS()
class CATACLYSM_API UCataclysmRegeneration : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Seconds a character must go without taking damage before its energy
	 * shield begins to refill. docs/Cataclysm_GDD_v2.md, Energy Shield.
	 */
	static constexpr float ShieldRefillDelaySeconds = 3.0f;

	/**
	 * Seconds between one application and the next.
	 *
	 * FINER THAN THE RATE IT APPLIES, deliberately. The rate is stated per
	 * second and this is a quarter of a second, so each application adds a
	 * quarter of the rate. A whole second per step is what the damage over time
	 * effect uses, and it is right there because a burn tick is an event a
	 * player should see land; a pool coming back is not an event, and a bar
	 * that jumps once a second reads as broken rather than as recovering.
	 */
	static constexpr float StepSeconds = 0.25f;

	/**
	 * How much a pool gains in one step, given its per-second rate.
	 *
	 * Zero for a rate of zero or below. A negative regeneration rate is not a
	 * drain in this design -- nothing states one -- so it is treated as none.
	 */
	static float GainPerStep(float RatePerSecond, float SecondsInStep);

	/**
	 * Whether an energy shield may refill yet, given how long since its owner
	 * last took damage.
	 *
	 * A CHARACTER THAT HAS NEVER BEEN HURT MAY REFILL. Callers that have no
	 * record of any damage should pass a large number rather than zero, which
	 * is what ACataclysmCharacterBase::SecondsSinceLastDamage does.
	 */
	static bool ShieldMayRefill(float SecondsSinceLastDamage);

	/**
	 * Adds one step of health, mana and energy shield to a character.
	 *
	 * NOTHING FOR THE DEAD. A corpse healing back up is the obvious failure,
	 * and an enemy is destroyed on the tick after it dies, so without this a
	 * creature could be pulled off zero health in the frame between the two.
	 *
	 * NOTHING WHEN A POOL IS ALREADY FULL either, which matters because
	 * PreAttributeChange clamps rather than refuses: without the check every
	 * step would write the maximum back over itself and fire an attribute
	 * change for a value that did not change.
	 *
	 * Does nothing at all when the actor has no ability system or no vital
	 * attribute set, which includes every actor before its ability system has
	 * been initialised.
	 */
	static void ApplyStep(AActor* Character, float SecondsInStep,
						  float SecondsSinceLastDamage);
};
