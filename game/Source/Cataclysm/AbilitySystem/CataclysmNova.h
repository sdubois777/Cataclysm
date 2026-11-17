// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmNova.generated.h"

class AActor;
class UAbilitySystemComponent;

/**
 * A burst of damage a character releases around itself on a repeating interval.
 *
 * WHAT IT IS FOR. The Masochist's `basic_ll_b2` Unstable Aura:
 *
 *   While at or below 10% health, you release a nova every 5 seconds dealing
 *   damage equal to 1% of your missing health per point to enemies within 6
 *   metres.
 *
 * IT IS THE ONLY THING IN THE GAME A CHARACTER DOES WITHOUT ACTING. Every other
 * source of damage a player deals comes from a skill they used or from a blow
 * they took: a hit, a burning patch of ground the skill left, a retaliation.
 * This happens because time passed and health is low, which is why it lives on
 * the per-character step rather than on any ability.
 *
 * WHY UNSTABLE AURA WAS WORTH BUILDING. Issue #1050. Seven Masochist nodes still
 * granted nothing, and six of them are blocked or undecided: three need a way to
 * put a debuff on an ENEMY (issues #742 and #674), one has a reading question
 * open (#1033), one needs a cap on how many unique debuffs a character may carry
 * that no document decides, and one needs a comparison between the character's
 * debuffs and an enemy's. This was the last unblocked one.
 *
 * WHAT IT IS NOT.
 *
 * NOT A TIMER OF ITS OWN. `ACataclysmCharacterBase::RegenerationStep` already
 * runs several times a second and already carries five jobs for the same reason:
 * a timer per character is one more thing to cancel when one dies. The interval
 * is enforced by a timestamp on the ability system component, which is the shape
 * `UCataclysmDamageConversion` uses for its cooldown.
 *
 * NOT SCALED BY THE CHARACTER'S DAMAGE STATS. The node states the amount
 * exactly -- "damage equal to 1% of your missing health per point" -- so what it
 * deals is that and not that multiplied by the increases the character carries.
 *
 * NOT EXEMPT FROM THE TARGET'S DEFENCES. It IS a hit, unlike retaliation, which
 * the design says explicitly is not one. Armour, resistance, evasion and block
 * all apply, and it can leech and critically strike like any other hit.
 */
UCLASS()
class CATACLYSM_API UCataclysmNova : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * The stat naming what share of MISSING health one nova deals, as a
	 * percentage. As `game/Data/PassiveEffects.csv` spells it.
	 */
	static const TCHAR* DamageStat;

	/**
	 * Seconds between one nova and the next.
	 *
	 * A CONSTANT AND NOT A STAT, which is the division The Breaking Point
	 * already draws: its 5% per point is a sheet row and its 50%, its 5 seconds
	 * of Bleeding and its 10 second cooldown are all constants here. Nothing in
	 * the design changes this number, and the per-point magnitude is the one
	 * thing the sheet has to carry so that
	 * `test_every_value_appears_in_the_nodes_own_description` can tie it to the
	 * node's own words.
	 */
	static constexpr float IntervalSeconds = 5.0f;

	/** How far a nova reaches, in metres. A constant for the reason above. */
	static constexpr float RadiusMetres = 6.0f;

	/** What one metre is in the centimetres Unreal measures in. */
	static constexpr float CentimetresPerMetre = 100.0f;

	/**
	 * What one nova deals, given the character's stat and where its health is.
	 *
	 * PURE ARITHMETIC AND NO ABILITY SYSTEM, so every case can be checked by
	 * passing numbers in. That is the shape `UCataclysmLeech::AmountFrom` and
	 * `UCataclysmHealthDebt::AmountDeferred` both use and for the reason
	 * `UCataclysmFervour` gives: a rule that is arithmetic on a few numbers
	 * should not need a character, a world and an effect spec to check.
	 *
	 * @param SharePercent  what share of missing health one nova deals
	 * @param Health        what the character has now
	 * @param MaxHealth     what it could have
	 * @return zero for a character that is not hurt, has no such stat, or has no
	 *         maximum health to be measured against
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Nova")
	static float AmountFrom(float SharePercent, float Health, float MaxHealth);

	/**
	 * Release a nova if this character has one to release and it is due.
	 *
	 * CALLED EVERY REGENERATION STEP, so it is asked several times a second and
	 * answers zero nearly every time. Three things stop it in turn and all three
	 * are the ordinary case: the character has no such stat, or is not hurt
	 * enough for the stat's own health condition, or released one less than
	 * `IntervalSeconds` ago.
	 *
	 * THE STAT IS ASKED FOR RATHER THAN READ. Its one row carries a health
	 * condition, so the gameplay attribute is zero even for a character holding
	 * the node, and a plain read would release nothing for ever with nothing
	 * reporting it.
	 *
	 * A CORPSE RELEASES NOTHING, for the reason `UCataclysmLeech::PayOutStep`
	 * refuses one: a dead creature is still standing there with an ability
	 * system for a window after it dies.
	 *
	 * A NOVA IS RELEASED WHETHER OR NOT ANYTHING IS STANDING IN IT, and the
	 * interval restarts either way. "You release a nova every 5 seconds" does
	 * not make the release depend on there being a target, and the other reading
	 * would let a character save novas up while alone and fire one the instant
	 * an enemy walked into range.
	 *
	 * @return what one nova was worth, before the targets' own defences.
	 *         **Not** the damage that landed: each target's armour, resistance,
	 *         evasion and block are applied where the blow arrives, so what
	 *         reaches health is smaller and differs per target. Zero means no
	 *         nova was released at all, which is every character in the game
	 *         without the node.
	 */
	static float Step(AActor* Character);
};
