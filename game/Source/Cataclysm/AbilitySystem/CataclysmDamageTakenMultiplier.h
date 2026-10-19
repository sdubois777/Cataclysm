// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmDamageTakenMultiplier.generated.h"

class AActor;
class UAbilitySystemComponent;

/**
 * A multiplier on ALL the damage one character takes, from every source, for a number of seconds: the smallest
 * enemy-side "takes more damage" the game can hold. Built 2026-10-19 for Chronomancer's Time-Lock's ten-piece bonus,
 * "All enemies that enter the rift are frozen in time for 5 seconds and take 500% more damage from all sources";
 * `docs/DECISIONS.md` has the entry. The coordinating session, K7: "x6 on all damage the enemy takes from every source
 * while this rift's freeze on it lasts; a boss the stun refused takes no extra". The one writer is
 * `UCataclysmTimeLockRift`, which holds x6 on an enemy for the seconds of the stun it laid.
 *
 * WHY A NEW THING. Before this no enemy could be made to take more damage from every source: an enemy holds no stat
 * line, and the one defender-side figure that rides on something laid on an enemy,
 * `UCataclysmAbilitySystemComponent::AilmentRiderPercentOn` with `ECataclysmAilmentRider::DamageTaken`, is an INCREASE
 * ADDED to the defender's `damage_taken`, carried by an ailment's kind. "500% more" is a multiplier of its own.
 *
 * READ IN ONE PLACE, `UCataclysmDamageCalculation::Resolve`, step 6, after `damage_taken`, the cannot-be-mitigated rule
 * and the Nth hit's share. So it reaches every blow and every tick that comes to the character through the Damage meta
 * attribute: a direct blow, an area blow, a damage over time tick, a minion's blow, another player's blow, a zone's
 * sweep, retaliation. It does not reach health taken with `UCataclysmSkillEffects::ReduceHealthDirectly`, which is
 * not a hit: an execute, a cull, the shares of the Sacrificial Bond. Nor a defender with no combat attribute set, which
 * step 6 does not reach.
 *
 * HELD HERE AND NOT ON THE ABILITY SYSTEM, keyed weakly by the character's ability system, as
 * `UCataclysmGroundPound` holds its clocks: so the shared class gains no member.
 */
UCLASS()
class CATACLYSM_API UCataclysmDamageTakenMultiplier : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Make `Target` take `Multiplier` times every blow and tick for the next `Seconds`, in its world's seconds.
	 * A MULTIPLIER OF ONE OR LESS IS REFUSED: this is "more damage taken" only. A lower figure would lower a blow the
	 * defender cannot mitigate, which the rule of 2026-10-12 forbids, so a "less" would need a rule of its own.
	 *
	 * TWO HOLDS ON ONE CHARACTER DO NOT MULTIPLY. A hold made while another runs keeps the larger multiplier and the
	 * later end. Today the one writer cannot overlap itself: its stun opens a 5 s window in which no other stun lands.
	 *
	 * @return whether the hold was made
	 */
	static bool Hold(AActor* Target, float Multiplier, float Seconds);

	/** What every blow and tick on the character whose ability system this is is multiplied by now: one for none. */
	static float On(const UAbilitySystemComponent* Defender);

	/** The seconds left on `Target`'s hold, nought for none. For tests and for nothing else. */
	static float SecondsLeftOn(const AActor* Target);
};
