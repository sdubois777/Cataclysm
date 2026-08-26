// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
// For FGameplayAttribute, which the private rate lookup is keyed to.
#include "AttributeSet.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmFervour.generated.h"

class UAbilitySystemComponent;

/**
 * What fills Fervour and what empties it.
 *
 * FERVOUR IS THE ONE RESOURCE EVERY CLASS SHARES, decided by the project owner
 * on 2026-08-25. `docs/DECISIONS.md` has the reasoning: a two-handed weapon can
 * roll 8 damage types and each unlocks 3 classes, so one character can reach all
 * 24 class trees, and 24 separately generating bars is not readable. What differs
 * by class is how the bar is filled and what it is spent on.
 *
 * UNTIL ISSUE #954 NOTHING MOVED IT AT ALL. The pool existed --
 * `UCataclysmClassResourceAttributeSet` has always held a current value and a
 * maximum -- and the only code outside that file touching either was the line
 * that writes the maximum from the class stat line. So 23 of the Masochist's 74
 * nodes named a resource that was always zero.
 *
 * THIS BUILDS THE MASOCHIST'S GENERATOR AND NOT THE OTHER THREE. The design
 * gives four classes four different generators:
 *
 *   Bulwark     taking hits, blocking, killing
 *   Berserker   1 per critical strike
 *   Saboteur    placing a trap or gadget, and one of them dealing damage
 *   Masochist   health lost to damage, and health spent as an ability cost
 *
 * Only the fourth is here. The other three are different rules rather than
 * different numbers, and each needs its own code in its own place. Issue #950
 * covers the Ravager and the Ritualist, which have no tree at all.
 *
 * A SEPARATE CLASS OF STATIC FUNCTIONS, like `UCataclysmDamageCalculation`,
 * `UCataclysmLeech` and `UCataclysmRegeneration`. `FervourFor` below is
 * arithmetic on three floats, so the whole rule can be tested by passing numbers
 * in rather than by building a character, a world and an effect spec for every
 * case. The three functions that follow it are the thin part that touches an
 * ability system.
 *
 * THREE CALL SITES AND NO GENERIC HEALTH WATCHER, deliberately. The design
 * distinguishes health lost to damage from health spent as a cost -- the
 * Masochist tree has separate nodes increasing each, and two keystones that
 * trade one against the other -- so a single hook on "health went down" could
 * not tell them apart. The three are:
 *
 *   damage    `UCataclysmVitalAttributeSet::PostGameplayEffectExecute`, in the
 *             branch where the Damage meta attribute has been resolved and the
 *             amount that reached health is known
 *   cost      `UCataclysmSkillTemplate::PayHealthCost`
 *   healing   `UCataclysmRegeneration::TopUp`, which is the one place both
 *             health regeneration and life leech restore health through
 */
UCLASS()
class CATACLYSM_API UCataclysmFervour : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	//~ The stat names, as `game/Data/ClassStats.csv` and
	//~ `game/Data/PassiveEffects.csv` spell them. They are the keys
	//~ `UCataclysmPlayerClassStats::StatToAttribute` is looked up by, and the
	//~ keys `UCataclysmAbilitySystemComponent::StatForSkill` is asked with, so
	//~ they exist once here rather than as literals at each use.

	/** Fervour gained per 1% of maximum health lost to damage. */
	static const TCHAR* FromDamageStat;

	/** Fervour gained per 1% of maximum health spent as an ability cost. */
	static const TCHAR* FromCostStat;

	/** Fervour removed per 1% of maximum health restored. */
	static const TCHAR* LostToHealingStat;

	/** Whether healing stops removing Fervour at all. Zero for no. #1006. */
	static const TCHAR* LossSuppressedStat;

	/** How much Fervour arrives every second, from nothing happening. #1008. */
	static const TCHAR* PerSecondStat;

	/**
	 * Marks a restoration of health as coming from the character's own
	 * regeneration rate rather than from leech.
	 *
	 * WHY THE SOURCE OF HEALING HAS TO BE SAID. The Masochist's Staunch node
	 * reduces "the Fervour removed by your own health regeneration", which is
	 * narrower than healing. A modifier requiring this tag applies to
	 * regeneration and not to leech, so the scoping falls out of the existing
	 * pipeline with no new mechanism.
	 */
	static FGameplayTag RegenerationTag();

	/**
	 * Marks a restoration of health as coming from leech. Issue #1006.
	 *
	 * LEECH USED TO CARRY NO TAG AT ALL, and that was enough while the only node
	 * asking about the source of healing was Staunch, which asks about
	 * regeneration: leech carried nothing, the tag did not match, the node did
	 * not apply. Wounds That Feed asks the other way round -- "healing from Life
	 * Leech does not remove Fervour" -- and carrying nothing cannot answer that,
	 * because a future healing source would also carry nothing and would be
	 * caught by the same row.
	 */
	static FGameplayTag LeechTag();

	/**
	 * Whether this character's healing stops removing Fervour. Issue #1006.
	 *
	 * ASKED WITH THE HEALING'S OWN TAGS, because the two nodes that set the flag
	 * set it for different healing: Sanguine Ledger for regeneration and Wounds
	 * That Feed for leech. The flag is one stat and the row's required tags are
	 * what tell the two apart.
	 *
	 * False for every character without one of those keystones, and false for
	 * any ability system with no class resource attribute set.
	 */
	static bool LossIsSuppressed(const UAbilitySystemComponent* AbilitySystem,
								 const FGameplayTagContainer& Healing);

	/**
	 * Add the Fervour a step this long is worth, and answer what arrived.
	 * Issue #1008.
	 *
	 * THE FIRST THING THAT FILLS THE POOL WITHOUT HEALTH HAVING MOVED. The
	 * Masochist's Low Life keystone is its only source: "While at or below 35%
	 * health you gain 10 Fervour per second."
	 *
	 * ASKED FOR RATHER THAN READ OFF THE ATTRIBUTE, because that node carries a
	 * health condition and a conditional bonus is never folded into an
	 * attribute. A plain read would answer zero for every character for ever and
	 * nothing at run time would report it.
	 *
	 * @return how much Fervour was really added, which is zero for a character
	 *         with no such node, for a full bar, and for no class resource set
	 */
	static float GainPerSecondStep(UAbilitySystemComponent* AbilitySystem,
								   float SecondsInStep);

	/**
	 * How much Fervour a health change of this size is worth.
	 *
	 *     Fervour = (health changed / maximum health) x 100 x rate
	 *
	 * PURE ARITHMETIC AND NO ABILITY SYSTEM, so every case can be checked by
	 * passing numbers in. Answers zero for a change of nothing, a rate of
	 * nothing, or a maximum health of nothing, and never answers a negative
	 * number: the caller decides whether the result is added or taken away.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Fervour")
	static float FervourFor(float HealthChanged, float MaxHealth,
							float RatePerPercent);

	/**
	 * What one of the three rates is worth to this character right now.
	 *
	 * THROUGH `StatForSkill` RATHER THAN THE ATTRIBUTE, so a modifier scoped by
	 * a tag is not lost. The Masochist's Shared Agony node increases Fervour
	 * gained from damage over time specifically, and the gameplay attribute
	 * holds the value worked out with no tags in hand, so it cannot express
	 * that. Issue #943 records the same problem for every other stat a skill
	 * uses. The attribute is passed as the fallback, so a character whose stat
	 * line has not been worked out yet gets the same answer as before.
	 *
	 * @param Context  what the hit or the healing carries. `Keyword.DoT` for a
	 *                 damage over time tick, `Keyword.Regeneration` for health
	 *                 regeneration, empty for anything else
	 * @return zero for an ability system with no class resource attribute set,
	 *         which is every enemy in the game
	 */
	static float RateFor(const UAbilitySystemComponent* AbilitySystem,
						 FName Stat, const FGameplayTagContainer& Context);

	/**
	 * Fill the bar from health this character lost to damage.
	 *
	 * @param HealthLost  what reached HEALTH, not what the hit was worth. A blow
	 *                    an energy shield absorbed generates nothing, which the
	 *                    design states outright: a shield on a Masochist is a
	 *                    straight loss of resource generation
	 * @return how much Fervour was really added, after the clamp at the maximum
	 */
	static float GainFromDamage(UAbilitySystemComponent* AbilitySystem,
								float HealthLost,
								const FGameplayTagContainer& HitTags);

	/**
	 * Fill the bar from health this character spent to use a skill.
	 *
	 * @return how much Fervour was really added, after the clamp at the maximum
	 */
	static float GainFromHealthCost(UAbilitySystemComponent* AbilitySystem,
									float HealthSpent);

	/**
	 * Empty the bar because this character was healed.
	 *
	 * @param HealthRestored  what really went into the pool. Healing that
	 *                        overflowed a full health bar restored nothing and
	 *                        removes nothing
	 * @return how much Fervour was really removed, as a negative number, after
	 *         the clamp at zero
	 */
	static float RemoveForHealing(UAbilitySystemComponent* AbilitySystem,
								  float HealthRestored,
								  const FGameplayTagContainer& HealingTags);

	/**
	 * Whether this character has any way of moving Fervour at all.
	 *
	 * TRUE WHEN ANY OF THE THREE RATES IS ABOVE ZERO. Read by the heads-up
	 * display, which draws the bar for a character that can move it and leaves
	 * it out for one that cannot -- the same rule the energy shield bar follows,
	 * and for the same reason: a bar that can only ever read zero says the
	 * opposite of what is true.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Fervour")
	static bool HasAGenerator(const UAbilitySystemComponent* AbilitySystem);

private:
	/** The three stat names above, and the attribute each drives. Built once. */
	static const TMap<FString, FGameplayAttribute>& RateAttributes();

	/**
	 * Shared by the three above; they differ only in which rate they read and
	 * which direction they move the bar.
	 *
	 * @param Sign  +1 to add and -1 to take away
	 * @return the change the pool really underwent, which is not the amount
	 *         asked for when the clamp at zero or at the maximum bit
	 */
	static float Move(UAbilitySystemComponent* AbilitySystem, FName Stat,
					  float HealthChanged, const FGameplayTagContainer& Context,
					  float Sign);
};
