// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AttributeSet.h"
#include "CataclysmRegeneration.generated.h"

class AActor;
class UAbilitySystemComponent;

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
 * part is load-bearing: the shield does not absorb a bleed (issue #2014; until
 * then it absorbed no damage over time at all), so without it a bleeding
 * character would refill their shield freely and the shield would be strongest
 * against the one thing it ignores.
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
	 * The three rates, by the names the design sheet gives them. Issue #1038.
	 *
	 * WHY NAMES AND NOT JUST ATTRIBUTES. `ApplyStep` used to read each rate
	 * straight off its gameplay attribute, and a bonus carrying a CONDITION or a
	 * SCALE is never folded into an attribute: `UCataclysmPlayerClassStats::ApplyTo`
	 * resolves every stat with the default `FCataclysmStatConditions`, which
	 * refuses every condition and reads every count as zero, because a bonus that
	 * grows with the character's state would be stale the moment the state moved.
	 * So such a bonus was dropped in silence -- the character regenerated its base
	 * rate, the arithmetic ran, and nothing reported it. `ApplyStep` now ASKS for
	 * each rate instead, which is what `UCataclysmFervour::GainPerSecondStep`
	 * already did for its own per-second stat under issue #1008.
	 *
	 * SHARED WITH `UCataclysmPlayerClassStats::StatToAttribute`, which records
	 * each stat under exactly this key. A second spelling would fall back to the
	 * attribute silently and read as a character with no bonus rather than as a
	 * fault, which is the argument `UCataclysmItemModifiers::AttackDamageStat`
	 * makes for the same shape.
	 */
	static const TCHAR* HealthRegenStat;
	static const TCHAR* ManaRegenStat;
	static const TCHAR* EnergyShieldRegenStat;

	/**
	 * What a row does to the HEALTH every heal restores, up or down, as its own
	 * multiplier. Ruled 2026-10-06 for "Reaper's Embrace (2-Piece Bonus): You
	 * gain 10% more life from all sources", read as healing by the set's own
	 * drawback: `healing_received` more 10.
	 *
	 * APPLIED IN `TopUp`, TO THE AMOUNT OFFERED, BEFORE `healing_received_reduction`
	 * takes its share, and separate from it: that stat is a share from 0 to 100
	 * that can only lower, is clamped as one, and is untouched by this. So 10%
	 * more under a 50% reduction is 100 x 1.1 x 0.5.
	 *
	 * ASKED OF THE STAT PIPELINE WITH THE CHARACTER'S STATE, so a condition on a
	 * row is judged at each heal. It has no gameplay attribute. HEALTH ONLY, as
	 * the reduction is; and only what passes through `TopUp`.
	 */
	static const TCHAR* HealingReceivedStat;

	/**
	 * `healing_skill_health_restored`: what a row does to the health a healing
	 * skill restores, as its own multiplier. Ruled 2026-10-09 for "Healing skills
	 * restore 30%-60% more HP": a `more` row of 30 to 60.
	 *
	 * THE HEALING SKILLS ARE LIVING PYRE AND BLOOD PYRE, the owner's decision of
	 * 2026-10-09, and they are the two skills tagged under `Stat.Recovery`.
	 *
	 * READ BY `HealingSkillAmount` AND BY NOTHING ELSE, and that function is
	 * called at two places: `UCataclysmAuraSkill::NoteBlowTaken`, for the health
	 * Living Pyre returns from a blow, and `ApplyStep`, for the EXTRA health
	 * regeneration a character gets from standing in its own Blood Pyre. The
	 * character's base regeneration is not multiplied.
	 *
	 * ASKED OF THE CHARACTER WHOSE SKILL IT IS, WITH NO TAGS: the aura's holder,
	 * and the patch's owner. A row on this stat therefore carries no Required
	 * Tags; one that did would match nothing at either place. It has no gameplay
	 * attribute and no base, so it is in
	 * `UCataclysmPlayerClassStats::StatsWithNoAttribute()`.
	 */
	static const TCHAR* HealingSkillHealthRestoredStat;

	/**
	 * `healing_skill_health_as_energy_shield`: the percentage of the health a
	 * healing skill restored that the character also gains as energy shield.
	 * Ruled 2026-10-09 for "Healing skills also restore 10%-20% of the healed
	 * amount as energy shield": a `flat` row of 10 to 20.
	 *
	 * READ BY `GiveHealingSkillShield` AND BY NOTHING ELSE, called at the same
	 * two places as `HealingSkillAmount` above, with the health that ARRIVED.
	 * Asked with no tags, as that stat is. No gameplay attribute and no base.
	 */
	static const TCHAR* HealingSkillHealthAsEnergyShieldStat;

	/**
	 * An amount of health a healing skill is about to restore, after the rows
	 * on `HealingSkillHealthRestoredStat` this character carries. 100 with a
	 * `more` row of 50 is 150.
	 *
	 * THE AMOUNT IS HANDED BACK UNCHANGED for a character with no such row, and
	 * for an ability system that is not this project's own. Never below nought.
	 * An amount of nought or less is answered with nought and asks nothing.
	 *
	 * THE CALLER PAYS THE RESULT THROUGH `TopUp`, which then applies every rule
	 * a heal of health obeys. This function restores nothing itself.
	 */
	static float HealingSkillAmount(const UAbilitySystemComponent& AbilitySystem,
									float Amount);

	/**
	 * Gives this character energy shield equal to its
	 * `HealingSkillHealthAsEnergyShieldStat` percentage of `HealthArrived`, and
	 * answers how much shield was added. Nought when the character carries no
	 * such row, when `HealthArrived` is nought or less, or when the shield is
	 * already at its maximum.
	 *
	 * `HealthArrived` IS THE HEALTH THAT REALLY ROSE, read off the health
	 * attribute by the caller after `TopUp`, and not what was offered. So a heal
	 * cut by the healing ceiling, by reserved health or by a reduction of
	 * healing received gives a smaller shield, and a heal on a character at full
	 * health gives none.
	 *
	 * INTO THE ORDINARY ENERGY SHIELD, through `TopUp`, which stops at the
	 * maximum the shield's clamp and its bar use. It is not the temporary absorb
	 * `UCataclysmAbilitySystemComponent::NoteOverheal` keeps.
	 *
	 * THE SHIELD'S RECHARGE WAIT IS NEITHER READ NOR RESTARTED. That wait is the
	 * time since the character last took damage, and only taking damage writes
	 * it. This gift arrives inside the wait as well as outside it.
	 *
	 * THE RECHARGE CEILING DOES NOT BIND IT. `EnergyShieldRechargeCeilingReductionStat`
	 * is ruled to cover regeneration only, and this is not regeneration.
	 */
	static float GiveHealingSkillShield(UAbilitySystemComponent& AbilitySystem,
										float HealthArrived);

	/**
	 * The stat saying this character's energy shield recharges before the
	 * wait after being damaged has run out, at a reduced rate. Issue #1515.
	 *
	 * `Ritualist_keystone_c_kB` Ablative is the only source, and its row is
	 * a flag: above zero or not, with nothing in between meaning anything.
	 * How much the rate is reduced by is `AblativeRechargeFraction` below,
	 * not this stat.
	 */
	static const TCHAR* ShieldRechargesWhileDamagedStat;

	/**
	 * The stat saying this character's energy shield recharges at its full
	 * rate inside the wait after being damaged. Issue #1833, the small engine
	 * halves.
	 *
	 * "Energy shield regeneration begins immediately after taking damage with
	 * no delay" is the only source, and its row is a flag, like Ablative's
	 * above. Ablative gives half the rate inside the wait; this gives the whole.
	 */
	static const TCHAR* ShieldRechargeHasNoDelayStat;

	/**
	 * The stat saying this character's mana regeneration also restores its
	 * energy shield. Issue #1515.
	 *
	 * `Ritualist_keystone_d_kA` The Long Game is the only source, and its
	 * row is a flag. What share of the mana rate the shield gets is
	 * `ManaRegenToShieldFraction` below, not this stat.
	 */
	static const TCHAR* ManaRegenRestoresShieldStat;

	/**
	 * How many percentage points of its maximum the energy shield may NOT
	 * regenerate into; the shield regenerates to what is left of 100. Issue
	 * #1833: "Your energy shield cannot recharge above 50% of its maximum" is
	 * flat 50, the shape `healing_ceiling_reduction` has, so nothing reaching
	 * it means no ceiling. Read in the regeneration step alone; no attribute,
	 * so it is in `UCataclysmPlayerClassStats::StatsWithNoAttribute()`.
	 */
	static const TCHAR* EnergyShieldRechargeCeilingReductionStat;

	/**
	 * What share of its usual rate an energy shield recharges at during the
	 * wait, for a character holding Ablative.
	 *
	 * A CONSTANT AND NOT A STAT, because the design row states the number
	 * itself -- "at half its usual rate" -- and nothing else in the project
	 * grants it. A stat would be a second place to write a figure the row
	 * already fixes, and the two could then disagree.
	 */
	static constexpr float AblativeRechargeFraction = 0.5f;

	/**
	 * What share of its mana regeneration a character holding The Long Game
	 * also puts into its energy shield.
	 *
	 * A CONSTANT AND NOT A STAT, for the same reason as the fraction above:
	 * the design row says "at half its rate" and nothing else grants it.
	 */
	static constexpr float ManaRegenToShieldFraction = 0.5f;

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

	/**
	 * Shared Blood, the Ritualist's `Ritualist_capstone_50` option 1. Issue
	 * #1515: "Your minions each have 20% of your Maximum Energy Shield as their
	 * own, and it recharges when yours does." The row will carry the 20 on this
	 * stat, held by the summoner.
	 */
	static const TCHAR* SharedBloodStat;

	/**
	 * Give `Character`, if it is a minion whose summoner holds Shared Blood, its
	 * share of the summoner's maximum energy shield, and refill it. Does nothing
	 * for anything else. Ruled 2026-09-24:
	 *
	 *   THE MAXIMUM FOLLOWS THE SUMMONER LIVE, re-read every step from the
	 *   summoner's scaled maximum -- the figure its own bar shows -- and the
	 *   current shield is clamped down when it falls. The owner's wish that
	 *   "minions update live" is recorded in CataclysmMinion.h.
	 *
	 *   IT REFILLS WHEN THE SUMMONER'S DOES, at the same share of its maximum
	 *   per second: the summoner's own rate as its last step computed it, over
	 *   the summoner's maximum. So the summoner's wait, Ablative and The Long
	 *   Game all carry over, and the minion's own hits do not delay it.
	 *
	 *   MINIONS ONLY. A thrall is not one.
	 *
	 * @param bFill  start the shield full, which a summoning does
	 */
	static void SharedBloodStep(AActor* Character, float SecondsInStep, bool bFill);

	/**
	 * The most health healing may bring this character to. Issue #1607: maximum
	 * health times `CeilingShare`, less the healing ceiling's reduction (Point of
	 * No Return, and the enchantments that say "cannot be healed above"), and
	 * never above what reservation leaves (issue #1833).
	 *
	 * `TopUp` ASKS IT FOR EVERY HEAL OF HEALTH, and every heal of a player's
	 * health goes through `TopUp`; Living Pyre's was the last that did not, until
	 * issue #1608. A caller that must know whether healing could do anything
	 * before paying for it asks it too: Wrung Out, issue #1607. A full refill on
	 * a new life is not healing and does not ask it.
	 */
	static float HealthHealingCeiling(const UAbilitySystemComponent& AbilitySystem,
									  float CeilingShare = 1.0f);

	/**
	 * Adds to one pool, stopping at its maximum.
	 *
	 * ASKED AND ANSWERED IN ONE PLACE because all three pools behave the same
	 * way and writing it three times is how the third one ends up subtly
	 * different from the first two.
	 *
	 * PUBLIC SINCE ISSUE #895, so leech can pay into a pool through the same
	 * function rather than through a second copy of it. A private copy in a
	 * second file compiles under the Unreal unity build and collides the moment
	 * both files are clean, which has cost this project a broken build once.
	 *
	 * IT IS ALSO WHERE HEALING EMPTIES FERVOUR, since issue #954. Being the one
	 * place all three pools are refilled through is exactly what makes it the
	 * right place: the design says healing removes Fervour "at the same rate",
	 * without naming a source, so regeneration and leech both have to count and
	 * both arrive here.
	 *
	 * @param Gain     how much to add before the maximum is considered. A gain
	 *                 that overflows the pool restores only what fits, and only
	 *                 what fits removes Fervour
	 * @param Healing  what this restoration carries, so a passive node can be
	 *                 scoped to one source of it. `Keyword.Regeneration` for a
	 *                 regeneration step, nothing for leech. Ignored for every
	 *                 pool except health, which is the only one Fervour reads
	 * @param CeilingShare  the share of the maximum this restoration may fill to,
	 *                 1 for all of it. Issue #1833: the shield's regeneration
	 *                 step passes its recharge ceiling; everything else passes 1
	 */
	static void TopUp(UAbilitySystemComponent& AbilitySystem,
					  const FGameplayAttribute& Pool,
					  const FGameplayAttribute& Maximum, float Gain,
					  const FGameplayTagContainer& Healing =
						  FGameplayTagContainer(),
					  float CeilingShare = 1.0f);
};
