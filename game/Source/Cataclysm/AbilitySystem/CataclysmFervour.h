// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
// For FGameplayAttribute, which the private rate lookup is keyed to.
#include "AttributeSet.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmFervour.generated.h"

class AActor;
class UAbilitySystemComponent;
class UCataclysmAbilitySystemComponent;
class ACataclysmCharacterBase;
struct FCataclysmDeathNotice;

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
 * TWO OF THE SIX GENERATORS ARE HERE AND FOUR ARE NOT. The design gives six
 * classes six different generators:
 *
 *   Bulwark     taking hits, blocking, killing
 *   Berserker   1 per critical strike, emptying on a timer out of combat
 *   Saboteur    placing a trap or gadget, and one of them dealing damage
 *   Ravager     1 an enemy an attack hits, 1 a second an enemy within 4 metres,
 *               and decaying at 5 a second once nothing is in reach
 *   Masochist   health and energy shield lost to damage, and health spent as
 *               an ability cost
 *   Ritualist   1 a second for each minion held, and 5 when one of them dies
 *
 * The Masochist's is issue #954 and the Ritualist's is #1518. THE THREE WAR
 * GENERATORS ARE NOT IN THIS FILE: they are `FCataclysmWarFervour`
 * (`CataclysmWarFervour.h`), slice C of `plan_death_and_war.md` (2026-11-24),
 * kept apart so the Death trees' per-kill Fervour, written here at the same
 * time, did not collide with them. The Bulwark's kill gain is not built there.
 *
 * THE RAVAGER'S IS BUILT IN CODE, ONE OF ITS ROWS STILL WAITING ON THE DESIGN
 * WORKBOOK, AND THIS PARAGRAPH SAID THE OPPOSITE UNTIL ISSUE #1515. It read "the largest of the four left" and "nothing in the game
 * empties Fervour on a timer"; the first stopped being true and the second was
 * made false by the very change that added `DecayStep` below. A comment that
 * describes the present tense is wrong the moment the present moves, and this
 * one described a gap its own file then filled.
 *
 * WHAT IS BUILT: the rate from the enemies standing near, the decay once
 * nothing is in reach, and "1 for each enemy your attacks hit", which
 * `GainForEnemiesHit` pays for each enemy an attack lands on. The rate and the
 * decay shipped together, because the decay is the rule that makes the rate
 * worth holding. The per-hit gain came after the count of enemies one attack
 * strikes, and it does not use that count: see `GainForEnemiesHit`. Its row
 * on the starting node is the one still waiting.
 *
 * AND THE DECAY NOW EXISTS, so "nothing empties Fervour on a timer" is no
 * longer a reason for anything. The Berserker's generator was refused partly on
 * that ground and that ground is gone; whatever else it needs should be
 * re-derived rather than inherited from this sentence.
 *
 * The Ritualist's needed no decay: its row keeps the default of not decaying.
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
 *             amounts that reached health and the energy shield are known
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

	/**
	 * The Fervour healing can never take the pool below. Zero for no floor.
	 *
	 * THE MASOCHIST'S SANGUINE LEDGER, AS REWORKED BY THE PROJECT OWNER'S
	 * APPROVAL OF 2026-11-24: "Healing never takes your Fervour below 50." Its
	 * row is a flat 50. It replaced the keystone's old trade -- regeneration
	 * removing no Fervour at the price of half the regeneration -- with an
	 * upgrade: once a Masochist has banked the Ultimate's cost, healing cannot
	 * take it back, and above the floor the class rule holds.
	 *
	 * A STAT WITH NO GAMEPLAY ATTRIBUTE, read only by `FloorAgainstHealing`
	 * below, the way `LossSuppressedStat` above is read only by
	 * `LossIsSuppressed`.
	 */
	static const TCHAR* FloorAgainstHealingStat;

	/** How much Fervour arrives every second, from nothing happening. #1008. */
	static const TCHAR* PerSecondStat;

	/** How much Fervour each skill cast grants. Issue #1051. */
	static const TCHAR* PerCastStat;

	/** How much Fervour dropping to low health grants. Issue #1069. */
	static const TCHAR* OnDroppingLowStat;

	/**
	 * How much Fervour arrives every second FOR EACH MINION held. Issue #1518.
	 *
	 * SEPARATE FROM `PerSecondStat` ABOVE ON PURPOSE. The Ritualist's
	 * `Binding Sigils` node increases "Fervour gained from your minions", and
	 * one shared stat would carry that increase onto the Masochist's Low Life
	 * keystone as well, which grants Fervour for being hurt.
	 */
	static const TCHAR* FromMinionsStat;

	/** How much Fervour one of the character's minions dying grants. #1518. */
	static const TCHAR* OnMinionDeathStat;

	/**
	 * Fervour a second for each enemy standing near this character. The
	 * Ravager's starting node is its only source. Issue #1515.
	 *
	 * THE ROW COUNTS, NOT THIS CODE. Its row carries `Scale=enemies_in_reach`
	 * with `ReachMetres=4`, so the value `StatForSkill` answers already has
	 * the bodies multiplied in, exactly as `FromMinionsStat` above has the
	 * minions multiplied in.
	 */
	static const TCHAR* PerEnemyInReachStat;

	/**
	 * Fervour a second this character LOSES once it has been out of contact
	 * for `DecayGraceSeconds`. The Ravager's starting node is its only source.
	 * Issue #1515.
	 */
	static const TCHAR* DecayPerSecondStat;

	/**
	 * How near an enemy must stand to stop that decay. Two Ravager nodes grant
	 * it and their flat values SUM: the starting node's 4 and No Ground Given's
	 * 4 make the 8 that keystone's sentence names. Issue #1515.
	 */
	static const TCHAR* DecayGraceMetresStat;

	/**
	 * How long this character may be out of contact before Fervour decays,
	 * before any row lengthens it. Issue #1515.
	 *
	 * THE BASE, AND `DecayGraceSecondsStat` BELOW IS ADDED TO IT. It was a
	 * constant with nothing added because no node changed the delay; the Ravager
	 * tree rework of 2026-11-24 gave Stoked that job. `Ravager_basic_spine_000`
	 * states the base: "after 3 seconds with no enemy within 4 metres".
	 */
	static constexpr float DecayGraceSeconds = 3.0f;

	/**
	 * `fervour_decay_grace_seconds`: seconds ADDED to `DecayGraceSeconds` before
	 * an out-of-contact character's Fervour starts to decay. The Ravager tree
	 * rework of 2026-11-24, Stoked: "Your Fervour starts to decay 1 second later
	 * per point." Flat, a point a second; read in `DecayStep` through the
	 * pipeline, with nought as the fallback. No gameplay attribute, so
	 * `UCataclysmPlayerClassStats::StatsWithNoAttribute` lists it. Path of
	 * Exile's "Inherent Rage Loss starts 2 seconds later" is the shipped shape.
	 */
	static const TCHAR* DecayGraceSecondsStat;

	/**
	 * `health_restored_on_fervour_full`: the share of maximum health, in
	 * percent, restored each time this character's Fervour BECOMES full. The
	 * Ravager tree rework of 2026-11-24, Second Wind: "Each time your Fervour
	 * becomes full, restore 2% of your maximum health per point." See
	 * `RestoreHealthOnFervourFull`. No gameplay attribute, so
	 * `UCataclysmPlayerClassStats::StatsWithNoAttribute` lists it.
	 */
	static const TCHAR* HealthRestoredOnFullStat;

	/**
	 * Restore the health `HealthRestoredOnFullStat` grants, answering how much
	 * really arrived. The Ravager tree rework of 2026-11-24, Second Wind.
	 *
	 * IT DOES NOT DECIDE WHETHER THE POOL BECAME FULL. The crossing is
	 * `ACataclysmPlayerCharacter::OnClassResourceChanged`'s, which stamps the
	 * moment for `seconds_after_resource_full` and calls this beside the stamp,
	 * so the heal and the window open on the same event and never on a pool
	 * that merely sits at its maximum.
	 *
	 * THROUGH `UCataclysmRegeneration::TopUp`, as every restoration in this file
	 * is, so it obeys the same ceiling and the same received-healing rows.
	 *
	 * ASKED THROUGH THE PIPELINE WITH A FALLBACK OF NOUGHT, the rule every rate
	 * here keeps: a row that later gains a condition is never folded into an
	 * attribute, and this stat has none anyway.
	 *
	 * @return the health really restored, which is zero for a character without
	 *         the node, for one already at full health, and for one with no
	 *         vital attribute set
	 */
	static float RestoreHealthOnFervourFull(UAbilitySystemComponent* AbilitySystem);

	/**
	 * How much of a character's maximum health a kill restores, in percent.
	 * `Ravager_basic_d_c1` Wrung Out is its only source. Issue #1515.
	 */
	static const TCHAR* HealthRestoredOnKillStat;

	/**
	 * What one kill's restoration costs in Fervour. Issue #1515.
	 *
	 * A CONSTANT RATHER THAN A STAT, for the reason `DecayGraceSeconds` above
	 * gives: no node in any tree changes it. `Ravager_basic_d_c1` states it:
	 * "Killing an enemy spends 5 Fervour to restore 1% of your maximum health
	 * per point."
	 */
	static constexpr float KillRestoreCost = 5.0f;

	/**
	 * Spend the Fervour one kill's restoration costs and restore the health it
	 * buys, answering how much health really arrived. Issue #1515.
	 *
	 * THE SPENDING SIBLING OF `GainOnMinionDeath`: a death moves the pool, and
	 * this time it moves it DOWN and buys something with it.
	 *
	 * ALL OR NOTHING, AND THAT IS A READING OF THE SENTENCE RATHER THAN ONE OF
	 * ITS WORDS. "If you have no Fervour it restores nothing" is loose about a
	 * character holding 3: spend 3 and restore it all, spend 3 and restore a
	 * share, or spend nothing and restore nothing. The same tree's `Bought With
	 * Ruin` states the case exactly for the same situation -- "If you cannot
	 * pay, the attack still hits but gains nothing" -- so an unpayable cost buys
	 * nothing, and "no Fervour" is the most obvious instance of not being able
	 * to pay rather than the only one. Spending less than the cost for the full
	 * effect would make the node free at one point of Fervour; a proportional
	 * share would invent arithmetic no sentence states.
	 *
	 * ASKED FOR THROUGH THE PIPELINE, fallback zero, for the reason every rate in
	 * this file gives: a row that later gains a condition is never folded into
	 * an attribute, and a plain read would answer zero for ever.
	 *
	 * @return the health really restored, which is zero for a character without
	 *         the node, for one that cannot pay, and for one already at full
	 *         health -- in which last case NOTHING IS SPENT either, because
	 *         paying for a restoration that restores nothing is a cost with no
	 *         effect the sentence never describes
	 */
	static float RestoreHealthOnKill(UAbilitySystemComponent* AbilitySystem);

	/**
	 * How much of a character's maximum health a kill restores AT NO COST, in
	 * percent. `Ravager_capstone_50`'s third option, Long Hold, is its only
	 * source. Issue #1515.
	 *
	 * NOT `HealthRestoredOnKillStat` ABOVE, which Wrung Out's kill buys with
	 * Fervour and which restores nothing when the Fervour is not there. One
	 * stat for both would make Long Hold cost Fervour or Wrung Out free.
	 */
	static const TCHAR* HealthRestoredOnKillAtNoCostStat;

	/**
	 * Restore the health a kill restores at no cost, answering how much really
	 * arrived. Issue #1515.
	 *
	 * "Killing an enemy restores 5% of your maximum health." NOTHING IS READ
	 * FROM THE POOL OR SPENT: this is `RestoreHealthOnKill` without the price,
	 * and it heals through the same `UCataclysmRegeneration::TopUp`, so it
	 * obeys the same ceiling and the same received-healing reductions.
	 *
	 * IT DOES NOT DECIDE WHETHER THIS CHARACTER KILLED ANYTHING. The death
	 * handler on `ACataclysmPlayerCharacter` owns that, and calls this BEFORE
	 * `RestoreHealthOnKill`: ruled 2026-09-17 under the project owner's
	 * delegation, so a kill this heals to full leaves Wrung Out nothing to buy,
	 * and Wrung Out spends nothing at full health.
	 *
	 * ASKED FOR THROUGH THE PIPELINE WITH THE ATTRIBUTE AS THE FALLBACK, as
	 * `GainOnMinionDeath` asks and for its reason: the row carries no
	 * condition, so it is folded into the attribute, and a zero fallback would
	 * throw it away for an ability system with no stat line recorded.
	 *
	 * @return the health really restored, which is zero for a character
	 *         without the option, for one already at full health, and for one
	 *         with no class resource attribute set
	 */
	static float RestoreHealthOnKillAtNoCost(UAbilitySystemComponent* AbilitySystem);

	/**
	 * The stat holding the share of maximum health, in per cent, a kill by a critical strike restores: the Shadow's Clean
	 * Kill, "Killing an enemy with a critical strike restores 1% of your Maximum Health for every 2 points". The Death
	 * trees' slice C4a (2026-11-25). A STAT AND NOT AN ACTION ROW, because "for every 2 points" is a row at each second
	 * point and the generator keeps one action row per node and event. No attribute and no base.
	 */
	static const TCHAR* HealthRestoredOnCriticalKillStat;

	/**
	 * Restore `HealthRestoredOnCriticalKillStat`'s share of maximum health, through `UCataclysmRegeneration::TopUp` as
	 * `RestoreHealthOnKillAtNoCost` does. The player's kill handler calls it for a kill whose killing blow was a critical
	 * strike (`FCataclysmDeathNotice::bByACriticalStrike`). Returns the health really restored.
	 */
	static float RestoreHealthOnCriticalKill(UAbilitySystemComponent* AbilitySystem);

	/**
	 * How much increased damage, in percent, a melee attack buys for each enemy
	 * it strikes beyond the first. `Ravager_basic_b_b2` Bought With Ruin is its
	 * only source. Issue #1515.
	 */
	static const TCHAR* IncreasedDamageBoughtPerExtraEnemyHitStat;

	/**
	 * What each enemy an attack strikes beyond the first costs in Fervour.
	 * Issue #1515.
	 *
	 * A CONSTANT RATHER THAN A STAT, for the reason `KillRestoreCost` above
	 * gives: no node changes it, and the node's points change only the damage.
	 * `Ravager_basic_b_b2` states it: "Each enemy your melee attack hits beyond
	 * the first costs 2 Fervour".
	 */
	static constexpr float ExtraEnemyHitCost = 2.0f;

	/**
	 * Pay for the enemies an attack struck beyond the first, and answer the
	 * increased damage, in percent, that bought. Issue #1515.
	 *
	 * `Ravager_basic_b_b2` Bought With Ruin: "Each enemy your melee attack hits
	 * beyond the first costs 2 Fervour and deals +3% increased damage per
	 * point. If you cannot pay, the attack still hits but gains nothing."
	 *
	 * ONCE FOR THE WHOLE ATTACK, BEFORE ITS FIRST BLOW. `UCataclysmSkillTemplate`
	 * calls this where the attack's targets are gathered, and every blow of that
	 * attack carries the answer on `FCataclysmHitDelivery`.
	 *
	 * ALL OR NOTHING, ruled on 2026-09-16 as Wrung Out's cost was: holding less
	 * than the whole cost spends nothing and buys nothing. A judgement, recorded
	 * in `docs/DECISIONS.md`.
	 *
	 * ASKED WITH THE SKILL'S TAGS, so the row's `RequiredTags=Type.Melee` keeps
	 * every attack that is not melee from buying anything or paying anything.
	 *
	 * @param EnemiesStruckTogether  how many enemies the attack struck, counted
	 *        before any of its blows resolved
	 * @return the increased damage bought, in percent: zero for a character
	 *         without the node, for an attack that struck one enemy or none,
	 *         and for one the character could not pay for
	 */
	static float BuyDamageForEnemiesStruckTogether(
		UAbilitySystemComponent* AbilitySystem,
		const FGameplayTagContainer& SkillTags, int32 EnemiesStruckTogether);

	/**
	 * Fervour gained for each enemy one attack lands on. `Ravager_basic_spine_000`,
	 * the Ravager's starting node, is its only source. Issue #1515.
	 */
	static const TCHAR* PerEnemyHitStat;

	/**
	 * Grant the Fervour one attack earned by landing on this many enemies, and
	 * answer how much was really added. Issue #1515.
	 *
	 * `Ravager_basic_spine_000`: "Enemies in reach generate Fervour: 1 for each
	 * enemy your attacks hit, and 1 per second for every enemy within 4 metres of
	 * you."
	 *
	 * WHAT COUNTS, ruled on 2026-09-16 as judgements under the owner's delegation
	 * and recorded in `docs/DECISIONS.md`:
	 * - A BLOW THAT LANDED. An evaded blow gives nothing; one armour stopped
	 *   completely still counts. That is the mana-on-hit test, and the owner's
	 *   rule of 2026-09-05 that an evaded attack applies nothing it carries.
	 * - ONCE PER ENEMY PER ATTACK, where one attack is what the count of enemies
	 *   struck together calls one: a skill that lands twice per use gives two.
	 * - EVERY LANDED, DAMAGING BLOW THE PLAYER'S OWN ATTACK DEALS, WHEREVER IT
	 *   RESOLVES. Four places call this: `UCataclysmSkillTemplate::HitTargets`,
	 *   which deals most skill blows, aura pulses included;
	 *   `ACataclysmProjectile::HitOne`, once per contact, so a piercing shot
	 *   pays per enemy passed and a return pass pays again; the rack throw's
	 *   spawn-less fallback in `UCataclysmProjectileSkill`; and
	 *   `UCataclysmBuriedWeapon`'s hit on the next enemy. A use sending no damage
	 *   gives nothing, and minions' and enemies' blows earn nothing: a firer
	 *   without the node reads zero.
	 *
	 * NOT THE COUNT OF ENEMIES STRUCK TOGETHER, AND DELIBERATELY. That count is
	 * taken before the blows resolve because it prices them, so an enemy that
	 * evades is in it. This gain prices nothing and is paid after the blows
	 * resolve, when whether each one landed is known.
	 *
	 * NO CAP AND NO COOLDOWN, because the sentence states neither.
	 *
	 * ASKED WITH THE SKILL'S TAGS, fallback zero, so a later row scoped to a kind
	 * of attack counts only for that kind.
	 *
	 * @param EnemiesHit  how many enemies the attack's blows landed on
	 * @return the Fervour really added: zero for a character without the node,
	 *         for an attack that landed on nothing, and for a full pool
	 */
	static float GainForEnemiesHit(UAbilitySystemComponent* AbilitySystem,
								   const FGameplayTagContainer& SkillTags,
								   int32 EnemiesHit);

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
	 * The Fervour this character's healing may not take the pool below, or
	 * zero for none. Sanguine Ledger, 2026-11-24. See `FloorAgainstHealingStat`.
	 *
	 * ASKED WITH THE HEALING'S OWN TAGS, as `LossIsSuppressed` is, so a later
	 * row may scope a floor to one kind of healing. Sanguine Ledger's row names
	 * no tag and covers every kind.
	 *
	 * Zero for an ability system with no class resource attribute set, which is
	 * every enemy, and floored at zero, so no row can make healing ADD Fervour.
	 */
	static float FloorAgainstHealing(const UAbilitySystemComponent* AbilitySystem,
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
	 * Add the Fervour one cast is worth, and answer what arrived.
	 * Issue #1051.
	 *
	 * THE MASOCHIST'S The Last Drop IS ITS ONLY SOURCE, the first option of
	 * The Final Vow: "While below 20% health your skills cost no health, and
	 * every skill you cast grants 10 Fervour."
	 *
	 * A SEPARATE FUNCTION FROM `GainPerSecondStep` ABOVE, though both add a
	 * plain count of Fervour, because they are counted by different things.
	 * That one is a RATE and is multiplied by the length of the step it is
	 * on; this is granted once per cast and a cast has no duration.
	 *
	 * ASKED FOR RATHER THAN READ OFF THE ATTRIBUTE, for the reason that one
	 * gives: the node's row carries a health condition, so the attribute is
	 * zero even for a character holding it.
	 *
	 * EVERY SKILL, INCLUDING ONE THAT COST NOTHING. The caller in
	 * `UCataclysmSkillTemplate::PayHealthCost` therefore sits outside the
	 * branch guarded on the cost being above zero, and it has to: the
	 * option's other clause makes the cost zero, so a grant inside that
	 * branch would never fire at all.
	 *
	 * @return how much Fervour was really added, which is zero for a
	 *         character with no such node, for a full bar, and for no class
	 *         resource set
	 */
	static float GainForCast(UAbilitySystemComponent* AbilitySystem);

	/**
	 * Take away the Fervour a step this long costs a character that has been
	 * out of contact, and answer what left. Issue #1515.
	 *
	 * THE FIRST THING IN THE GAME THAT EMPTIES THE POOL ON A TIMER. Everything
	 * else that removes Fervour removes it because something happened -- today
	 * only healing does, which is the Masochist's rule.
	 * `Ravager_basic_spine_000` reads "Fervour decays at 5 per second after 3
	 * seconds with no enemy within 4 metres, so losing contact is what empties
	 * it rather than a timer."
	 *
	 * IT TAKES THE CHARACTER RATHER THAN THE ABILITY SYSTEM, unlike every other
	 * function here, because it has to ask the world where the bodies are and a
	 * component does not know its own position.
	 *
	 * IT DRAINS WHATEVER FILLED THE POOL. A character holding both this node
	 * and the Masochist's Low Life loses Fervour out of contact that Low Life
	 * put there, and that is the sentence rather than an oversight: "While you
	 * have this" scopes the rule to holding the node, not to the Fervour's
	 * source.
	 *
	 * THREE REFUSALS BEFORE IT LOOKS AT THE WORLD, and the order is what keeps
	 * this cheap: no decay rate, which is every character without the node; an
	 * empty pool, which is a Ravager between fights; and no radius. Only a
	 * Ravager with Fervour actually in hand pays for a search.
	 *
	 * @return how much Fervour really left, which is zero for a character with
	 *         no such node, an empty pool, contact held, or the grace unspent
	 */
	static float DecayStep(ACataclysmCharacterBase* Character,
						   float SecondsInStep);

	/**
	 * Add the Fervour this character gains from its health dropping low.
	 * Issue #1069.
	 *
	 * THE MASOCHIST'S Rock Bottom, the first option of The Second Vow:
	 * "Dropping below 20% health clears all outstanding debt and grants 50
	 * Fervour, no more than once every 30 seconds."
	 *
	 * A THIRD FUNCTION THAT ADDS A PLAIN COUNT, beside `GainPerSecondStep` and
	 * `GainForCast`, and the three differ in what counts them. The first is a
	 * RATE multiplied by the length of a step. The second is granted once per
	 * cast. This is granted once per crossing, and a crossing is neither.
	 *
	 * IT DOES NOT DECIDE WHETHER THE CROSSING HAPPENED.
	 * `UCataclysmLowHealthRelief` owns the threshold and the cooldown and is
	 * the only caller. This is the write to the pool, which happens in this
	 * file for every route, so there is one place that clamps it.
	 *
	 * @return how much Fervour was really added, which is zero for a character
	 *         with no such option, for a full bar, and for no class resource set
	 */
	static float GainOnDroppingLow(UAbilitySystemComponent* AbilitySystem);

	/**
	 * Add the Fervour this character gains from one of its minions dying, and
	 * answer what arrived. Issue #1518.
	 *
	 * THE RITUALIST'S GENERATOR, SECOND HALF: "and 5 when one of them dies".
	 * Its starting node is the only source, and `Binding Sigils` increases it.
	 *
	 * A FOURTH FUNCTION THAT ADDS A PLAIN COUNT, beside `GainPerSecondStep`,
	 * `GainForCast` and `GainOnDroppingLow`, and the four differ in what counts
	 * them. The first is a rate multiplied by the length of a step; the second
	 * is granted once per cast; the third once per crossing; this once per
	 * death.
	 *
	 * IT DOES NOT DECIDE WHOSE MINION DIED, OR WHETHER IT WAS A MINION.
	 * `UCataclysmVitalAttributeSet` owns that: it has the dying character in
	 * hand, asks `UCataclysmCommand::CommanderOf` who was commanding it, and
	 * calls this with the COMMANDER'S ability system. This is the write to the
	 * pool, which happens in this file for every route so there is one place
	 * that clamps it.
	 *
	 * @param AbilitySystem  the COMMANDER'S, not the dying minion's. A minion
	 *                       has no class resource attribute set, so passing its
	 *                       own would answer zero and nothing would report why
	 * @return how much Fervour was really added, which is zero for a character
	 *         with no such node, for a full bar, and for no class resource set
	 */
	static float GainOnMinionDeath(UAbilitySystemComponent* AbilitySystem);

	/**
	 * Add Fervour a skill's row states it pays, and answer what arrived. Issue #2885, the Death draft's N6, slice DS3:
	 * `KillFervour`, "every enemy it kills gives you 5 Fervour". A FIFTH FUNCTION THAT ADDS A PLAIN COUNT, beside the
	 * four above, and the one whose count is the row's own rather than a stat's: so nothing is asked of the stat lines.
	 *
	 * IT DOES NOT DECIDE WHETHER THE KILL HAPPENED OR WHOSE IT WAS. `UCataclysmKillFervour`, the curse spread, the buried
	 * weapon and the flicker do; this is the write to the pool, which happens in this file for every route so there is
	 * one place that clamps it.
	 *
	 * SCALED BY HOW FAST THE CLASS RESOURCE GENERATES, then handed whole to `RaisePool`, which clamps it at the spendable
	 * maximum and gives Overflow what does not fit, as every gain here is.
	 *
	 * A ROW'S `KillFervour` IS A KILL'S PAYMENT: `bForAKill` opens the kill-payment marker for the whole gain, as
	 * `GainOnOwnKill` does, so Any Fuel refuses it and Kindred Fires and Borrowed Time leave the hold alone. Every
	 * `KillFervour` route passes true; a row's `HitFervour` does not. The Death skills merge, obligation 4.
	 *
	 * @param AbilitySystem  whoever's skill it was: the caster's, never the dying creature's
	 * @param Stated         the row's figure; nought or less adds nothing
	 * @param bForAKill      whether this pays for a death (`KillFervour`), not a hit
	 * @return how much Fervour was really added, which is zero for a full bar and for no class resource set
	 */
	static float GainStated(UAbilitySystemComponent* AbilitySystem, float Stated, bool bForAKill = false);

	/**
	 * Empty the pool, and answer what left it: every point held. Issue #2885, the Death draft's N13, slice DS4: the
	 * Wand's Requiem, "spend every Fervour you hold, the Ultimate's 50 among it". THE WRITE TO THE POOL, here for the
	 * reason every other is: one place clamps it. It decides nothing about whether a use spends all; the strike asks
	 * its row's `SpendsAllFervour` and pays its own cost first, through `ApplyCost`.
	 *
	 * NO `resource_consumed` EVENT, a judgement of slice DS4: the use's cost already raised one ("once per payment
	 * whatever its size", issue #1833), and this is the rest of the same payment rather than a second one.
	 *
	 * @param AbilitySystem  the spender's; nothing for one with no class resource set
	 * @return how much Fervour left the pool, nought or more
	 */
	static float SpendAll(UAbilitySystemComponent* AbilitySystem);

	/**
	 * Fervour gained when an enemy dies near this character.
	 * `Ritualist_capstone_50`'s second option, Fed by the Fallen, is its only
	 * source. Issue #1515.
	 */
	static const TCHAR* OnEnemyDeathNearbyStat;

	/**
	 * How near an enemy's death must be to grant that Fervour, in metres.
	 * Issue #1515.
	 *
	 * A CONSTANT RATHER THAN A STAT, for the reason `KillRestoreCost` gives: no
	 * node changes it. The option states it: "You gain 10 Fervour whenever an
	 * enemy dies within 10 metres of you."
	 *
	 * NOT `ACataclysmPlayerCharacter::NearbyDeathRadiusCm`, the worn rows'
	 * three metres for "when an enemy dies near you", a sentence that names no
	 * distance.
	 */
	static constexpr float EnemyDeathNearbyRadiusMetres = 10.0f;

	/**
	 * Gain the Fervour an enemy's death this far away grants, answering what
	 * really arrived. Issue #1515.
	 *
	 * IT DOES NOT DECIDE WHETHER THE DEATH WAS AN ENEMY'S. The death handler on
	 * `ACataclysmPlayerCharacter` owns that, with the one test the worn rows'
	 * nearby death uses: any enemy, whoever killed it, this character's own
	 * kills included, and never its own minion. Ruled 2026-09-17 under the
	 * project owner's delegation. THIS decides only whether the death was near
	 * enough, so the radius has one home.
	 *
	 * WITHIN MEANS AT OR INSIDE, the way the worn rows' radius is compared.
	 *
	 * CLAMPED AT THE MAXIMUM, the rule every write to the pool in this file
	 * follows. No reservation is read or changed.
	 *
	 * @param AbilitySystem  this character's
	 * @param MetresAway     how far from this character the death happened, or
	 *                      negative when that is not known, which grants nothing
	 * @return how much Fervour was really added, which is zero for a character
	 *         without the option, for a death too far away, for a full bar,
	 *         and for no class resource attribute set
	 */
	static float GainOnEnemyDeathNearby(UAbilitySystemComponent* AbilitySystem,
										float MetresAway);

	//~ The Death classes' kill generator, 2026-11-24: Fervour for each enemy this character kills, by the slain
	//~ creature's rarity rung, and the decay that starts a grace after the last kill.

	/**
	 * Fervour one kill pays: `fervour_per_kill`. The Soul Collector's starting node, "1 for each enemy you kill, 3 for
	 * an Elite, 5 for a Legendary, 8 for a Herald and 15 for a Boss", and the Shadow's, "10 for each enemy you kill, 25
	 * for each Elite or stronger enemy you kill", are flat rows tiered by `target_rarity_is` and
	 * `target_rarity_at_least`, ASKED WITH THE SLAIN CREATURE AS THE TARGET, so the rung is read where it is needed.
	 */
	static const TCHAR* PerKillStat;

	/**
	 * The stat holding the chance, in per cent, that a kill gives 1 Fervour more than `PerKillStat` pays: the Soul
	 * Collector's Gleaning, "Each enemy you kill has a 5% chance per point to give 1 extra Fervour". The Death trees'
	 * slice C4a (2026-11-25). ROLLED ON EACH KILL, as ruled, in `GainOnOwnKill`, and asked against the slain creature as
	 * the payment is; `Cataclysm.KillFervourExtraRoll` pins the roll for tests. No attribute and no base.
	 */
	static const TCHAR* PerKillExtraChanceStat;

	/**
	 * Fervour a second the Death classes' decay takes once the grace after the last kill has run out:
	 * `fervour_kill_decay_per_second`. The Soul Collector's 5 and the Shadow's 10.
	 *
	 * NOT THE RAVAGER'S `DecayPerSecondStat`. That one decays once nothing is in reach, and a Soul Collector holding it
	 * would be drained by `DecayStep` whenever it stood alone for three seconds as well as by this. Two rules, two
	 * stats, and a multiclassed character holding both loses Fervour to each when each says so.
	 */
	static const TCHAR* KillDecayPerSecondStat;

	/**
	 * Seconds after a kill before that decay starts: `fervour_kill_decay_grace_seconds`, ASKED AT THE KILL WITH THE
	 * SLAIN CREATURE AS THE TARGET, so "killing an Elite or stronger creature holds off the decay for 8 seconds rather
	 * than 4" is a row under `target_rarity_at_least`, and the Shadow's Momentum ("2 seconds later") a flat row beside.
	 */
	static const TCHAR* KillDecayGraceSecondsStat;

	/**
	 * Stamp a kill this character made: the clock `seconds_after_kill` reads, and the time the decay after a kill is
	 * held off until (`UCataclysmAbilitySystemComponent::NoteOwnKill`), with the grace asked against `Slain`.
	 *
	 * FOR EVERY KILL OF EVERY CHARACTER, whatever its tree: a row under `seconds_after_kill` needs the clock whether or
	 * not the character has the generator, and a character with no grace row is held until now, which holds nothing.
	 *
	 * CALLED FROM `ACataclysmPlayerCharacter::OnSomethingDied` FOR A DEATH CREDITED TO THIS CHARACTER, after
	 * `GainOnOwnKill` (ruled 2026-11-25). The grace is asked before the clock moves, so a grace row under
	 * `seconds_after_kill` reads the time since the previous kill, as the payment does. Nothing for an ability system
	 * that is not this game's.
	 */
	static void NoteOwnKill(UAbilitySystemComponent* AbilitySystem, const AActor* Slain);

	/**
	 * Pay the Fervour one kill of `Slain` is worth, and answer what arrived.
	 *
	 * THE CLASS GENERATOR, AND NOT A SKILL'S `KillFervour`. It pays on every kill credited to this character, its damage
	 * over time's included (the ruling of 2026-09-14); a skill whose row states `KillFervour` pays its own figure on top
	 * when that skill's blow made the kill (the Death skills branch, #2885). One death, one payment from each.
	 *
	 * SCALED BY THE GENERATION RATE AND CLAMPED AT THE MAXIMUM, the rule every gain in this file follows. No reservation
	 * is read or changed.
	 *
	 * ASKED BEFORE `NoteOwnKill` STAMPS THIS KILL (ruled 2026-11-25), so a row under `seconds_after_kill` reads the
	 * time since the previous kill: "kills within 2 seconds of each other" is that row with the value 2, and the first
	 * kill of all, with no previous one, matches no such row.
	 *
	 * @return the Fervour that arrived: nought for a character without the rows, for a creature no row's rung matches,
	 *         for a full bar and for an ability system with no class resource attribute set
	 */
	static float GainOnOwnKill(UAbilitySystemComponent* AbilitySystem, const AActor* Slain);

	/**
	 * Spirit Volley, option 2 of the Soul Collector's vow at 50 points (`SoulCollector_capstone_50`), issue #2908: "Each
	 * Fervour you gain from a kill flies to you as a spirit that strikes the nearest enemy on its way for 30% of weapon
	 * damage. Kills make spirits, spirits make kills." The percent of weapon damage each spirit deals; nought without the
	 * option. Read by `LooseSpiritVolley` with no tags. No attribute.
	 */
	static const TCHAR* SpiritVolleyWeaponPercentStat;

	/**
	 * The most spirits one kill looses, a judgement of the Death finish slice A (2026-11-26): ten, so a Boss's 15 and the
	 * doubled payments of the strong cannot loose a storm of blows from one death. Path of Exile's Summon Phantasm caps
	 * its kill-made phantasms at 5 to 10 and Herald of Purity its sentinels at 4. A spirit lives only for its one blow, so
	 * a cap on one kill's spirits is the cap on how many exist at once.
	 */
	static constexpr int32 SpiritVolleyMostAKill = 10;

	/**
	 * How far from its line a spirit's flight reaches for an enemy "on its way": the half-width, in centimetres, of the
	 * corridor from the slain body to the character. Three metres, a judgement: the width of the Soul Collector's own
	 * nearby reach is five, and a corridor that wide either side would be a ten-metre swath.
	 */
	static constexpr float SpiritPathHalfWidthCm = 300.0f;

	/**
	 * Loose one spirit for each whole Fervour `Paid` (the Fervour a kill paid, before the full bar clamps it, a judgement:
	 * a full bar is where a Soul Collector stands in a fight, and "kills make spirits" would stop there), at most
	 * `SpiritVolleyMostAKill`, from where `Slain` lies toward the character. HANDED TO THE ANNOUNCER
	 * (`UCataclysmCombatEvents::AfterThisDeathIsHeard`), as the overkill explosion is, so a creature a spirit kills is a
	 * kill of the character's of its own, heard after this one, which pays and looses spirits in turn: "kills make
	 * spirits, spirits make kills". The Fervour itself arrives with the kill, as before; the spirits are its blows.
	 *
	 * @return how many spirits were loosed; nought without the option
	 */
	static int32 LooseSpiritVolley(UAbilitySystemComponent* AbilitySystem, const AActor* Slain, float Paid);

	/**
	 * Fly `Spirits` spirits from `From` toward `Character`: each strikes, as the character's own hit for `Percent` of
	 * weapon damage, the living enemy nearest `From` within `SpiritPathHalfWidthCm` of the line from `From` to the
	 * character (`Slain` left out), chosen again for each spirit so one that a spirit killed is passed over. A spirit with
	 * no enemy on its way reaches the character and strikes nothing, and so does every one after it. Public so a test can
	 * drive it.
	 *
	 * @return how many spirits struck an enemy
	 */
	static int32 FlySpirits(AActor* Character, const AActor* Slain, const FVector& From, int32 Spirits, float Percent);

	/** What every line this writes begins with, at Log: "SPIRIT VOLLEY: ...". */
	static const TCHAR* const SpiritVolleyLogPrefix;

	/**
	 * Add `Change` to the class resource pool and answer how much it rose. THE ONE WAY A GAIN REACHES THE POOL: every
	 * function here that fills it, Cinders Kept's return (`UCataclysmAbilitySystemComponent::ReturnFervourDue`), the
	 * Siphon, and a class resource top-up through `UCataclysmRegeneration::TopUp` call this rather than
	 * `ApplyModToAttribute`, and `tools/tests/test_every_gain_to_the_pool_goes_through_raise_pool.py` fails on any
	 * other positive write. The Death trees' slice C4b1 (2026-11-25), as ruled.
	 *
	 * WHY A HELPER AND NOT A HOOK ON THE ATTRIBUTE. `UAbilitySystemComponent::ApplyModToAttribute` reaches the pool
	 * through `FActiveGameplayEffectsContainer::ApplyModToAttribute`, which only calls `SetAttributeBaseValue`: the
	 * attribute set's `PostGameplayEffectExecute` never runs, and the hooks that do run see `SetNumericAttributeBase`
	 * too, which a loaded save and a creature raised holding Fervour use and which the coordinating session ruled is no
	 * gain (read in the UE 5.8 source on 2026-11-25).
	 *
	 * CLAMPED AT THE MAXIMUM, here as well as by every caller, as every gain was before this: what does not fit is lost.
	 * A rise is then handed to Kindred Fires and Borrowed Time. Nought, and nothing written, for a change of nought or
	 * less, a full bar, or an ability system with no class resource set.
	 */
	static float RaisePool(UAbilitySystemComponent* AbilitySystem, float Change);

	/**
	 * The stat that makes every gain hold off decay as a kill does: the Soul Collector's Kindred Fires, "Fervour you gain
	 * from any source, not only a kill, holds off decay as a kill does: the time before your Fervour begins to decay
	 * starts again". Above nought, a rise of the pool that is not a kill's payment restarts the hold at its full length
	 * (`KillDecayHoldFullSeconds`); a kill sets the hold only through its own stamp, as ruled. The Death trees' slice
	 * C4b1 (2026-11-25). No attribute and no base.
	 */
	static const TCHAR* HoldRestartedByAnyGainStat;

	/**
	 * The seconds a gain that is not a kill's payment lengthens a running hold by: the Soul Collector's Borrowed Time,
	 * "extended by 1 second for every 3 points, never past its full length", a row at 3 points and one at 6. The Death
	 * trees' slice C4b1 (2026-11-25). No attribute and no base.
	 */
	static const TCHAR* HoldExtendedOnGainSecondsStat;

	/**
	 * The share of the full bar after reserves, in percent, below which the kill decay does not take Fervour: the Soul
	 * Collector's Miser's Grip, "Your Fervour never decays below 3% of your maximum Fervour per point". The Death trees'
	 * slice C6c (2026-11-25), as ruled. Only the decay stops at it; a spend, a loss to healing and Soulbound's loss go
	 * below it. No attribute and no base.
	 */
	static const TCHAR* DecayFloorPercentStat;

	/**
	 * Every how many percent of maximum health lost to damage costs 1 Fervour: the Soul Collector's Soulbound, "you lose 1
	 * Fervour for every 2% of your maximum health that you lose to damage". The Death trees' slice C6c (2026-11-25), as
	 * ruled. Health lost only: an energy shield or mana that absorbs the blow costs nothing, and health paid as a skill's
	 * cost is not lost to damage; a tick of damage over time is. No attribute and no base.
	 */
	static const TCHAR* HealthPercentLostPerFervourLostStat;

	/**
	 * The seconds of the shared window Overflow's stacks last, and whether a character has Overflow at all: the Soul
	 * Collector's Overflow, "Fervour gained above the maximum becomes 5 seconds of attack speed". The Death trees' slice
	 * C6c (2026-11-25), as ruled. No attribute and no base.
	 */
	static const TCHAR* OverflowAttackSpeedSecondsStat;

	/**
	 * The seconds Unbroken Chain adds to a chain of kills: "A chain of kills allows 1 second more between kills for every
	 * 4 points. The same applies to Soul Tide." The Death trees' slice C6c (2026-11-25), as ruled. No attribute and no
	 * base. Read by `UCataclysmAbilitySystemComponent::KillChainSecondsHeld`.
	 */
	static const TCHAR* KillChainSecondsStat;

	/**
	 * The full length of the hold before decay, in seconds, as ruled: the grace the last kill set, and before any kill
	 * a common kill's grace (`KillDecayGraceSecondsStat` asked as if against rung 0). The Death trees' slice C4b1
	 * (2026-11-25). Nought for an ability system that is not this game's.
	 */
	static float KillDecayHoldFullSeconds(const UAbilitySystemComponent* AbilitySystem);

	/**
	 * The seconds before a kill whose decay a kill gives back: the Soul Collector's Reclaimed, "A kill made after your
	 * Fervour has begun to decay gives back the Fervour that decay took from you in the 3 seconds before it". The Death
	 * trees' slice C4b2 (2026-11-25). No attribute and no base.
	 */
	static const TCHAR* DecayGivenBackOnKillSecondsStat;

	/**
	 * The seconds after your hit within which an enemy's death pays you its Fervour, whatever kills it: the Soul
	 * Collector's Claimed, "within 1 second for every 2 points of that hit", a row at each second point. The Death trees'
	 * slice C4b2 (2026-11-25). No attribute and no base.
	 */
	static const TCHAR* ClaimedWithinSecondsStat;

	/**
	 * The switch that makes a follower's kill this character's for the Soul Collector tree: Death's Toll, "Enemies your
	 * minions, gadgets and traps kill give you their Fervour, and count as your kills for everything this tree asks".
	 * The Death trees' slice C4b2 (2026-11-25). No attribute and no base.
	 */
	static const TCHAR* FollowerKillsCountAsYoursStat;

	/**
	 * The switch that takes the Fervour held out of the character's damage: the Soul Collector's Death Made Flesh, "The
	 * Fervour you hold no longer adds to your damage" (issue #2951, the keystone rework of 2026-11-27). Read once per
	 * conditions fill into `FCataclysmStatConditions::bFervourHeldAddsDamage`, which the starting node's two damage rows
	 * ask through the condition `fervour_held_adds_damage`. No attribute and no base.
	 */
	static const TCHAR* FervourHeldAddsNoDamageStat;

	/**
	 * Reclaimed: give back, as a kill's payment and unscaled, what the decays took in the stat's seconds before now.
	 * Called by the player's kill handler for a kill credited to the character, and for a follower's kill Death's Toll
	 * makes its own, before the kill is paid. Answers what arrived. Nothing when nothing decayed in that time, which is
	 * every kill made before decay began. The Death trees' slice C4b2 (2026-11-25), as ruled: both decays count.
	 */
	static float GiveBackDecayOnKill(UAbilitySystemComponent* AbilitySystem);

	/**
	 * Claimed: for a death not credited to this character, pay what this character's kill of `Slain` would pay
	 * (`GainOnOwnKill`, Gleaning's roll included) when its last hit on `Slain` landed within the stat's seconds. As a
	 * kill's payment, with no kill clock and no hold, as ruled. The player's kill handler does not call it for a kill
	 * Death's Toll has made the character's. Answers what arrived. The Death trees' slice C4b2 (2026-11-25).
	 */
	static float ClaimOnDeath(UAbilitySystemComponent* AbilitySystem, const AActor* Slain);

	/**
	 * Death's Toll: when `Notice`'s killer is a follower of `Commander` (`UCataclysmCommand::CommanderOf`: a minion, a
	 * gadget, a trap or a thrall) and the stat is held, treat the kill as `Commander`'s for everything the Soul Collector
	 * tree asks: Reclaimed's give-back, the payment and every row inside it, the kill clock and the hold
	 * (`NoteOwnKill`), Last Breath (`RestoreHealthOnKillAtNoCost`), and `kill` raised to the Soul Collector's own rows
	 * only. Not the run's kill count, not the weapon's, and no other tree's rows, as ruled. Answers whether it did. The
	 * Death trees' slice C4b2 (2026-11-25).
	 */
	static bool CreditAFollowersKill(UAbilitySystemComponent* AbilitySystem, AActor* Commander,
									 const FCataclysmDeathNotice& Notice);

	/**
	 * The Ferryman's burst: `SharePercent` of `SlainMaximum` dealt to every enemy of `Killer` within
	 * `UCataclysmSkillEffects::SlainBurstRadiusCm` of `At`, through `UCataclysmSkillEffects::ExplodeForOverkill`, handed
	 * to the announcer (`UCataclysmCombatEvents::AfterThisDeathIsHeard`) so a creature it kills is `Killer`'s kill,
	 * raises `kill`, and bursts in turn. The one place both the character's own kill and a follower's kill Death's Toll
	 * makes its own make the burst. The Death trees' slice C4c1 (2026-11-25). Nothing for a share or a maximum of nought.
	 */
	static void BurstTheSlain(AActor* Killer, const AActor* Slain, const FVector& At, float SlainMaximum,
							  float SharePercent);

	/**
	 * Grim Wake: mark every enemy of the killer's within the widest `ReachMetres` of its rows under
	 * `target_near_your_kill_within_seconds`, around `At`, `Slain` left out, as having stood near its kill now
	 * (`UCataclysmAbilitySystemComponent::NoteNearKillBy`). Nothing for a character with no such row. Called by the
	 * player's kill handler for its own kill and by `CreditAFollowersKill` for a follower's kill Death's Toll makes its
	 * own. The Death trees' slice C4c2 (2026-11-25).
	 */
	static void MarkNearAKill(UAbilitySystemComponent* Killer, const AActor* Slain, const FVector& At);

	/**
	 * Fervour gained for each critical strike the character lands, asked against the character struck: the Shadow's
	 * starting node, "2 for each critical strike you land", and Telling Blows, "+1 Fervour per point for each critical
	 * strike you land on an Elite or stronger enemy". The Death trees' slice C4d1 (2026-11-25), as ruled.
	 */
	static const TCHAR* PerCriticalStrikeStat;

	/**
	 * Pay the Fervour one landed critical strike on `Struck` earns, scaled by how fast the class resource generates and
	 * held to the maximum, through `RaisePool`; answer what was added. Nought for a character without the stat. Called
	 * by the player's hit handler for its own critical strikes, a minion's left out. Not a kill's payment.
	 *
	 * ASKED WITH THE BLOW'S SKILL TAGS TOO, since the Death trees' bundle 3 merge (2026-11-25, obligation 5): the War
	 * line's Berserker reads the same stat, and Battle Lust's "on every critical melee strike" is a row scoped to melee.
	 * THE ONE PAYMENT OF THE STAT: the War line paid it a second time from its own reader, and that call is gone.
	 * `FCataclysmWarFervour::GainOnCriticalStrike` hands on to this one.
	 */
	static float GainOnCriticalStrike(UAbilitySystemComponent* AbilitySystem, const AActor* Struck,
									  const FGameplayTagContainer& SkillTags = FGameplayTagContainer());

	/**
	 * The seconds between two payments of an own kill's Fervour for a tick of the character's damage over time on a
	 * Boss: the Shadow's Patient Blade, "Against a Boss, your damage over time ticks also count as kills for filling
	 * Fervour, once every 2 seconds". Its row is under `target_is_boss`, asked against the character ticked. The Death
	 * trees' slice C4d2 (2026-11-25), as ruled: Fervour only, one clock per character.
	 */
	static const TCHAR* BossTicksPayAKillEverySecondsStat;

	/**
	 * Every how many landed hits on one Boss count as a kill of it: the Shadow's Death by Inches, "Against a Boss, every
	 * 10th hit you land counts as killing an Elite or stronger enemy, for Fervour and for everything else that happens
	 * when you kill". Its row is under `target_is_boss`, asked against the character struck. The Death trees' slice C4d2
	 * (2026-11-25), as ruled: the count is kept per Boss and never reset.
	 */
	static const TCHAR* BossHitsCountedAsAKillEveryStat;

	/**
	 * Patient Blade: for a tick of the character's damage over time on `Ticked`, pay one own kill's Fervour of it
	 * (`GainOnOwnKill`), unless the character's last such payment was less than the stat's seconds ago. Nothing for a
	 * character without the stat or a creature that is not a Boss (the row's condition). Answers what was added. Fervour
	 * only: no kill is raised and nothing else a kill does happens.
	 */
	static float PayForABossTick(UAbilitySystemComponent* AbilitySystem, const AActor* Ticked);

	/**
	 * Death by Inches: count a landed hit of the character's on `Struck`, and at every Nth, N the stat, credit a kill of
	 * it without its death (`CreditAKillWithoutADeath`). Nothing for a character without the stat, a creature that is not
	 * a Boss, or one already dead. Answers whether a kill was credited.
	 */
	static bool CountAHitOnABoss(UAbilitySystemComponent* AbilitySystem, AActor* Struck);

	/**
	 * Credit the character with a kill of `Slain` that has not died, as ruled for Death by Inches (Q7): decay given back
	 * (Reclaimed), the kill's Fervour, the stamp (the hold before decay and `seconds_after_kill`), Grim Wake's marks, and
	 * `kill` raised to every row with `Slain` as its target and its maximum health as its amount. LEFT OUT, because each
	 * needs a body: the overkill explosion, The Ferryman's burst and Passed On (the shares the rows record are taken and
	 * dropped), `afflicted_death`, risen imps, the corpse; and the player state's and weapon's kill tallies, because
	 * nothing died. The Death trees' slice C4d2 (2026-11-25).
	 */
	static void CreditAKillWithoutADeath(UAbilitySystemComponent* AbilitySystem, AActor* Slain);

	/**
	 * The enemy modifiers Wearer of the Dead can lend, in their fixed places: place N of `wearing_enemy_modifier` is the
	 * Nth of these, counted from 1. `tools/generate_datatables.py` holds the same list as `WORN_ENEMY_MODIFIERS`, and a
	 * Python test holds the two equal. The Death trees' slice C4d3 (2026-11-25), as ruled (W1): the five built
	 * modifiers that are a plain number on a player stat. TO ADD ONE: append its row name here and to
	 * `WORN_ENEMY_MODIFIERS`, and give the vow its row under `wearing_enemy_modifier` at the new place; the condition's
	 * bounds follow the list's length. Never reorder: a row's place is written in the data. Issue #2899 builds every
	 * enemy modifier but the one button ability to work on any wearer and adds each here; the first expected source is
	 * the branch `feat/death-war-enemy-modifiers`.
	 */
	static const TArray<FName>& WornEnemyModifierRows();

	/** The place of `Row` in `WornEnemyModifierRows`, counted from 1, or nought when it is not there. */
	static int32 WornEnemyModifierPlace(FName Row);

	/**
	 * The enemy modifiers Wearer of the Dead lends AS THEMSELVES rather than as a figure on a stat: each is no number on
	 * a player stat, so the vow holds no row for it, and a character holding the vow at all (`UCataclysmAbilitySystem
	 * Component::HoldsAnyRowWearing`) is lent it, recorded by its row (`NoteLentModifier`) and read wherever its effect
	 * asks `UCataclysmEnemyModifiers::CarriedBy`. Issue #2899 (2026-11-26): the Death modifiers, converted to work on any
	 * wearer. NINE OF DEATH'S ELEVEN: the owner ruled on 2026-11-26 that of the three that act at their wearer's death
	 * only Undying Will is lent to the player ("you come back once with less health and more damage"), and Haunting and
	 * Final Curse never are. `tools/tests/test_charge_and_placed_action_names_match_the_engine.py` holds this list to
	 * the Death rows of `game/Data/EnemyModifiers.csv` less those two.
	 */
	static const TArray<FName>& LentEnemyModifierRows();

	/** How long a lent modifier is worn: "for 20 seconds". */
	static constexpr float WornEnemyModifierSeconds = 20.0f;

	/**
	 * Wearer of the Dead: at a kill of `Slain`, Elite or stronger (rarity rung 1 and up, bosses included, as ruled),
	 * record on the character each of its modifiers that has a place and that the character holds a row for, worn for
	 * `WornEnemyModifierSeconds`, and refresh the character's live maximum health so a worn bonus to it arrives at once.
	 * Called by the kill handler, by `CreditAFollowersKill` and by `CreditAKillWithoutADeath`. Answers how many were
	 * recorded.
	 */
	static int32 WearTheModifiersOf(UAbilitySystemComponent* AbilitySystem, const AActor* Slain);

	/**
	 * The line the screen shows above the skill bar while modifiers are worn: each by name with its whole seconds left.
	 * Empty when none is. Every system has a basic interface; this is Wearer of the Dead's.
	 */
	static FString WornModifiersLine(const UCataclysmAbilitySystemComponent* Holder);

	/** The start of every Soul Collector row's trigger key: `Passive_SoulCollector_`. See `CreditAFollowersKill`. */
	static FString SoulCollectorRowKeyPrefix();

	/**
	 * Take away the Fervour a step this long costs once the grace after the last kill has run out, and answer what left.
	 * The Soul Collector's "Fervour decays at 5 per second once 4 seconds have passed since your last kill, until you
	 * kill again" and the Shadow's "drains at 10 per second".
	 *
	 * ITS OWN FUNCTION, BESIDE `DecayStep` AND NOT INSIDE IT, for the reason `KillDecayPerSecondStat` gives.
	 *
	 * A CHARACTER THAT HAS NEVER KILLED IS NOT HELD, so Fervour it holds from another source decays at once. "Once 4
	 * seconds have passed since your last kill" has no last kill to count from, and holding for ever would make a
	 * multiclassed Soul Collector's other generator a bar that never empties.
	 *
	 * NOR WHILE A RUNNING SELF BUFF HOLDS FERVOUR (`UCataclysmSkillTemplate::ARunningBuffHoldsFervour`, the Sword's Cold
	 * Blood), asked as `DecayStep` asks it. Added by the Death skills merge, obligation 3, as the coordinating session
	 * ruled on 2026-11-24.
	 *
	 * @return how much Fervour really left, which is zero for a character with no rate, an empty pool, a hold not yet
	 *         run out, and a creature holding a player build
	 */
	static float KillDecayStep(UAbilitySystemComponent* AbilitySystem, float SecondsInStep);

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
	 * Fill the bar from health and energy shield this character lost to damage.
	 *
	 * @param HealthLost  what reached HEALTH plus what the ENERGY SHIELD lost to
	 *                    the same blow, not what the hit was worth. The shield's
	 *                    share counts exactly as health lost, at the one rate per
	 *                    1% of maximum HEALTH: the project owner, 2026-11-24, "the
	 *                    massochist doesn't gain fervour with damage to energy
	 *                    shield, and it should", and the coordinating session's
	 *                    ruling of the same day on the rate. Issue #2873. Until
	 *                    then a blow the shield absorbed generated nothing. The
	 *                    one caller, `UCataclysmVitalAttributeSet::
	 *                    PostGameplayEffectExecute`, adds the two; a shield
	 *                    recharging removes nothing, because `RemoveForHealing`
	 *                    is asked for health alone
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
	 * Take Fervour for health this character lost to damage, under Soulbound (`HealthPercentLostPerFervourLostStat`): 1
	 * for each whole step of that share of maximum health, the remainder kept. The Death trees' slice C6c (2026-11-25).
	 * Not stopped by Miser's Grip's floor, which is about decay.
	 *
	 * @param HealthLost  what reached HEALTH, as `GainFromDamage` takes it
	 * @return how much Fervour was really taken, as a positive number
	 */
	static float LoseFromDamage(UAbilitySystemComponent* AbilitySystem, float HealthLost);

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
	 * TRUE WHEN ANY RATE ATTRIBUTE IS ABOVE ZERO, OR ANY ROW ON A STAT IN `GainStats` IS. Read by the heads-up display,
	 * which draws the bar for a character that can move it and leaves it out for one that cannot -- the same rule the
	 * energy shield bar follows, and for the same reason: a bar that can only ever read zero says the opposite of what
	 * is true.
	 *
	 * THE STAT LINES AS WELL AS THE ATTRIBUTES since 2026-11-26: the owner's play-test found no bar for a Soul Collector
	 * holding its starting node. `fervour_per_kill` has no attribute, and the attributes the Necromancer's and the
	 * Ravager's starting nodes fill were never among the rate attributes, so only the Masochist's, the Ritualist's and
	 * (through `FCataclysmWarFervour::HasARate`) the War classes' and the Shadow's starting nodes drew one.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Fervour")
	static bool HasAGenerator(const UAbilitySystemComponent* AbilitySystem);

	/**
	 * Every stat a row adds Fervour through, other than the War classes' rates (`FCataclysmWarFervour::GainStats`),
	 * which `HasAGenerator` asks of the stat lines. `fervour_lost_to_healing`, a loss, is not one.
	 */
	static const TArray<const TCHAR*>& GainStats();

private:
	/**
	 * What a rise of the pool does to the hold before decay. A kill's payment does nothing here: the kill's own stamp
	 * sets its hold. Any other rise: Kindred Fires restarts the hold; otherwise Borrowed Time lengthens a running one.
	 * Called by `RaisePool` only.
	 */
	static void AfterPoolRose(UAbilitySystemComponent* AbilitySystem, float Rise);

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
