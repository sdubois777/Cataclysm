// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "GameplayTagContainer.h"
#include "CataclysmSkillEffects.generated.h"

class UAbilitySystemComponent;
class UDataTable;
class UGameplayEffect;
struct FGameplayEffectContextHandle;

// What a blow resolved to on its target, reported back by `ApplyDirectDamage`
// and `ApplyHit`. Issue #1156. Declared rather than included, because both take
// it by pointer and `CataclysmDamageCalculation.h` includes this header's
// neighbours -- a full include here is a cycle waiting to be written.
struct FCataclysmDamageResult;

/**
 * How a hit reached its target, which decides two steps of the mitigation order.
 *
 * NOT WHAT THE HIT IS -- its damage and its damage type are elsewhere. This is
 * how it arrived, and it is the caller's to state because only the caller knows.
 *
 * THE RULE FOR `bIsArea` IS ABOUT HOW THE TARGET WAS FOUND, not about which
 * skill shape produced it. A hit that swept a volume and caught whatever was
 * inside is area damage; a hit that made contact with one target is direct. That
 * distinction is already in the code at every damage site, so nothing has to be
 * guessed from a shape's name: a projectile hits one thing while it travels and
 * sweeps a sphere when it detonates, and those are the same projectile.
 *
 * Both default to false, so a caller that says nothing deals an ordinary direct
 * blow. That is the common case and the safe one: the wrong default here would
 * make an attack unevadable rather than evadable.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmHitDelivery
{
	GENERATED_BODY()

	/** Area damage cannot be evaded. It can still be blocked. */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Skill Effects")
	bool bIsArea = false;

	/**
	 * Bleed, poison, burn and the rest. An energy shield does not absorb it,
	 * which is what makes a shield a distinct defence rather than extra health.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Skill Effects")
	bool bIsDamageOverTime = false;

	/**
	 * The blow was struck in melee. Issue #1032.
	 *
	 * SET FROM THE SKILL'S OWN TAGS IN `ApplyHit`, the same way and in the same
	 * place `bIsArea` is, and combined with whatever the caller said rather than
	 * overriding it. Both sides already state it: `Type.Melee` is on six of the
	 * seven enemy abilities since issue #1020 and on 27 rows of
	 * `game/Data/WeaponSkills.csv`, including every Fist skill the Masochist
	 * uses.
	 *
	 * IT TRAVELS AS A TAG ON THE EFFECT because nothing else crosses to the
	 * defender. A skill's tags are used where the blow is BUILT -- to look up
	 * what its damage should be -- and are not carried to whoever it lands on;
	 * only the handful `ApplyTypedSpec` puts on the spec make that journey. This
	 * is the fifth.
	 *
	 * MUTILATION MASTERY IS WHAT NEEDED IT: "Your melee critical strikes have a
	 * 5% chance per point to apply Bleeding." The rule fires in
	 * `UCataclysmVitalAttributeSet`, on the defender, where the skill is long
	 * out of scope.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Skill Effects")
	bool bIsMelee = false;

	/**
	 * This blow may not critically strike, whatever the attacker's chance is.
	 *
	 * FOR A SUMMONED MINION. A minion's damage is dealt in its summoner's name,
	 * so the attacker the engine reads a critical strike chance off is the
	 * player. The design says "a minion takes neither the summoner's critical
	 * strike chance nor its multiplier" and set minion damage at the top of its
	 * band because a minion has no critical strike layer to compound with.
	 * See `docs/Cataclysm_GDD_v2.md` lines 1747 and 1776.
	 *
	 * A DAMAGE OVER TIME TICK DOES NOT NEED THIS. It is already excluded by
	 * `bIsDamageOverTime`, which the defender's attribute set reads directly.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Skill Effects")
	bool bCannotCriticallyStrike = false;

	/**
	 * This blow ignores none of the target's armour or resistance.
	 *
	 * FOR A SUMMONED MINION, and for the same reason the flag above exists. A
	 * minion's damage is dealt in its summoner's name, so the attacker the engine
	 * reads a penetration figure off is the player. The design blocks it: "A
	 * minion does not take the summoner's weapon damage, flat added damage,
	 * attack speed, critical strike chance or multiplier, penetration...", under
	 * the general rule that a minion reaches its summoner through exactly three
	 * channels and nothing else crosses. See `docs/Cataclysm_GDD_v2.md:1747`.
	 *
	 * IT COVERS THREE ROUTES RATHER THAN TWO. The attacker's `Penetration` and
	 * `ArmorPenetration` attributes are the obvious two. The third is the
	 * summoner's weapon: a piercing weapon makes `Resolve` ignore a further 20%
	 * of the target's armour, and the weapon is read off the effect causer, which
	 * for a minion's blow is also the summoner. Issue #659.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Skill Effects")
	bool bCannotPenetrate = false;

	/**
	 * This blow carries none of the attacker's weapon sub-type effects.
	 *
	 * FOR A SUMMONED MINION, the third exclusion built for the same reason as the
	 * two above. The weapon a hit is credited to is read off the effect causer,
	 * and a minion's damage is dealt in its summoner's name, so without this a
	 * sword in the player's hand makes its imps deal 10% more to health and a
	 * wand makes them strip 10% more energy shield.
	 *
	 * BLOCKED BY THE DESIGN'S GENERAL RULE rather than by a sentence naming
	 * sub-types: "A minion reaches its summoner through exactly three channels,
	 * and nothing else crosses", then "Everything else is blocked unless a
	 * modifier says minion". A weapon sub-type is not one of the three.
	 * `docs/Cataclysm_GDD_v2.md:1747`. Issue #676.
	 *
	 * IT OVERLAPS WITH `bCannotPenetrate` ON PIERCING, on purpose: piercing's
	 * whole effect is armour penetration, so both flags block it and a hit needs
	 * only one of them to be free of it.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Skill Effects")
	bool bCarriesNoWeaponSubType = false;

	/**
	 * This blow gives its attacker no leech.
	 *
	 * FOR A SUMMONED MINION, the fourth exclusion built for the same reason as
	 * the three above. Leech is read off the attacker when a hit lands, and a
	 * minion's damage is dealt in its summoner's name, so without this a
	 * Ravager's imps would heal the Ravager with every blow they struck.
	 *
	 * THE DESIGN NAMES LEECH OUTRIGHT among what does not cross: "A minion does
	 * not take the summoner's weapon damage, flat added damage, attack speed,
	 * critical strike chance or multiplier, penetration, armour, evasion,
	 * block, resistances, energy shield, leech, movement speed..." Issue #895.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Skill Effects")
	bool bCannotLeech = false;

	/**
	 * This blow provokes no retaliation from what it strikes.
	 *
	 * FOR A SUMMONED MINION, and the only one of its five exclusions that
	 * protects the summoner rather than the target. Retaliation is dealt back to
	 * whoever the hit was credited to, and a minion's blow is credited to its
	 * summoner, so without this a Ritualist standing at range would take damage
	 * every time one of its imps struck a retaliating enemy.
	 *
	 * BLOCKED BY THE DESIGN'S GENERAL RULE, the same one that blocks the weapon
	 * sub-type: "A minion reaches its summoner through exactly three channels,
	 * and nothing else crosses." Issue #895.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Skill Effects")
	bool bCannotBeRetaliatedAgainst = false;

	/**
	 * The base critical strike chance of the skill dealing this blow, or -1 to
	 * take whatever the attacker's own attribute holds.
	 *
	 * THE ONLY NUMBER ON THIS STRUCT, and it is here because critical strike
	 * chance belongs to the skill rather than to the character. The design says
	 * so twice: its stat source table names "the skill being used" as the source,
	 * and the sentence after it is "A character has no critical strike chance in
	 * the abstract." A character holds six skills at once and has one
	 * `CritChance` attribute, so the skill's own figure has to travel with the
	 * hit instead of being written onto the character. Issue #657.
	 *
	 * -1 IS THE ORDINARY CASE. Every one of the 398 rows of the weapon skill
	 * matrix leaves the Crit Chance column blank today, and an enemy's attack, a
	 * minion's blow and a burning patch of ground never had a skill row at all.
	 * All of them take the attacker's attribute, which is what happened before
	 * this existed.
	 *
	 * ZERO IS A REAL ANSWER, so it cannot be the sentinel. A skill designed never
	 * to critically strike states 0 and gets 0.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Skill Effects")
	float CritChancePercent = -1.0f;

	/**
	 * What the skill dealing this blow cost, as a percentage of the attacker's
	 * maximum health, or -1 for a blow with no skill behind it.
	 *
	 * THE SECOND NUMBER ON THIS STRUCT, and it is here for the same reason as
	 * the first. The Masochist's Grand Tithe node reads "a skill whose health
	 * cost is above 10% of your maximum health deals 4% increased damage per
	 * point", which asks about the SKILL rather than the character, so no state
	 * built from the character alone can answer it.
	 * `UCataclysmSkillEffects::ApplyHit` receives the skill's tags and not the
	 * skill, so the number has to travel with the blow. Issue #983.
	 *
	 * -1 IS THE ORDINARY CASE. An enemy's attack, a minion's blow and a burning
	 * patch of ground never had a skill behind them, and every one of those
	 * correctly refuses a condition about what a skill cost.
	 *
	 * ZERO IS A REAL ANSWER, so it cannot be the sentinel, exactly as for the
	 * critical strike chance above. A skill that was used and charged nothing
	 * reports zero, which is every skill in the game except Blood Pyre for a
	 * character with no point in the Deeper Cuts node.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Skill Effects")
	float SkillHealthCostPercent = -1.0f;

	/**
	 * The `Element.*` tag of the skill dealing this blow, for colour only.
	 *
	 * WHAT IT IS FOR. A player's effects were all drawn white because the only
	 * damage type anything could see was the attacker's, and a player's hits
	 * carry none. This carries the skill's own, which the skill row states and
	 * which nothing was reading. Issue #803.
	 *
	 * IT NEVER DECIDES A RESISTANCE. `UCataclysmSkillEffects::ApplyTypedSpec`
	 * puts it on the damage effect together with
	 * `Data.ElementIsForColourOnly`, and that marker is what stops the defender
	 * treating it as a damage type. See the marker's own declaration on
	 * `UCataclysmDamageCalculation` for why the two are separated at the source
	 * rather than at the far end.
	 *
	 * AN INVALID TAG IS THE ORDINARY CASE for everything that has no skill row:
	 * an enemy's attack, a minion's blow, a burning patch of ground. Those are
	 * typed by their attacker already, or are meant to be untyped.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Skill Effects")
	FGameplayTag SkillElement;

	/** A hit that covers ground rather than touching one target. */
	static FCataclysmHitDelivery Area()
	{
		FCataclysmHitDelivery Delivery;
		Delivery.bIsArea = true;
		return Delivery;
	}
};

/** How long an effect lasts and what it is worth. Read from the DoTs sheet. */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmStatusEffectNumbers
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Status Effect")
	float DurationSeconds = 0.0f;

	/** What one tick deals as a plain amount. Five of the six effects use this. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Status Effect")
	float FlatDamagePerTick = 0.0f;

	/**
	 * What one tick deals as a percent of the hit that applied it.
	 *
	 * Nothing states one as of 2026-08-24; it was Burn's base until the owner
	 * moved the ailments to a flat amount. See FCataclysmStatusEffectRow.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Status Effect")
	float PercentOfHit = 0.0f;

	/**
	 * What one tick deals as a percent of the target's current health.
	 *
	 * CARRIED BUT NOT USABLE THROUGH THIS PATH, which computes one fixed amount
	 * per tick up front. A share of current health is a different amount every
	 * tick. Only Void Splinter states one, and nothing implements it; issue #915.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Status Effect")
	float PercentOfCurrentHealth = 0.0f;

	/**
	 * How large the effect is, in whatever unit its own description states.
	 *
	 * Shred's 10 is resistance, Cripple's 30 is a percentage slow, Weaken's 20 is
	 * a percentage damage reduction. **What the number is OF is not in the data**
	 * -- there is no column saying so -- which is issue #1144.
	 *
	 * ZERO IS THE ORDINARY CASE. Twenty-three of the twenty-seven debuff rows
	 * state none, because they are a tag and a duration and nothing more.
	 *
	 * IT IS NOT COVERED BY `bUsable` BELOW, deliberately. That flag asks whether
	 * this row can be applied as damage over time, and Shred is not damage: it
	 * has a strength and no per-tick amount, so it is usable through
	 * `ApplyNamedEffect` and not through `ApplyDamageOverTime`.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Status Effect")
	float Strength = 0.0f;

	/**
	 * False when no row was found, or when the row states nothing this path can
	 * apply -- no duration, or no flat amount and no percent of the hit.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Status Effect")
	bool bUsable = false;

	/**
	 * What one tick deals against a hit of this size, before the attacker's
	 * three damage over time stats.
	 *
	 * THE TWO BASES ARE SUMMED RATHER THAN ONE BEING PICKED. Exactly one is ever
	 * stated, which a test checks, so the sum is that one. Summing means a row
	 * that somehow stated both would deal both rather than silently losing one,
	 * which is the failure that is easier to notice.
	 */
	float DamagePerTickAgainst(float HitDamage) const
	{
		return FlatDamagePerTick + HitDamage * PercentOfHit / 100.0f;
	}
};

/**
 * What one damage over time effect will actually do, after the attacker's
 * three stats.
 *
 * SEPARATE FROM APPLYING IT so the arithmetic can be checked with plain numbers.
 * The alternative is watching a gameplay effect tick in a test world, which
 * measures the engine's timer as much as it measures this, and which cannot
 * show that all three stats multiply without running for several seconds.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmDamageOverTimeNumbers
{
	GENERATED_BODY()

	/** What one tick deals, after the attacker's damage over time stat. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Effects")
	float DamagePerTick = 0.0f;

	/** The gap between ticks, after the attacker's frequency stat. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Effects")
	float SecondsPerTick = 0.0f;

	/** How long it runs, after the attacker's duration stat. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Effects")
	float DurationSeconds = 0.0f;

	/** How many ticks that comes to. Not a whole number; see TotalDamage. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Effects")
	float Ticks = 0.0f;

	/**
	 * What it deals altogether.
	 *
	 * COMPUTED FROM THE OTHER THREE RATHER THAN STATED, which is the point of
	 * the design's rule: the total is what the three stats produce between them
	 * and is not itself a number anything states.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Effects")
	float TotalDamage = 0.0f;

	/** False when nothing would be applied at all. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill Effects")
	bool bUsable = false;
};

/**
 * Dealing damage and applying effects, shared by every skill template.
 *
 * NOTHING COULD DO THIS BEFORE EITHER. UCataclysmDamageCalculation::Resolve was
 * called from exactly one place -- the defender's own attribute set, when the
 * Damage meta attribute changed -- and nothing in the project ever changed it.
 * Every number in the design that reads "250% weapon damage" was a percentage
 * of nothing, in the same way every cooldown reduction divided nothing before
 * issue #155.
 *
 * WHY EFFECTS ARE BUILT HERE RATHER THAN AUTHORED AS ASSETS. The magnitudes come
 * from generated data tables and from the caster's own attributes at the moment
 * of the hit, so there is no fixed number an authored asset could carry.
 * UCataclysmGameplayAbility::ApplyCooldown already builds its effect the same
 * way and for the same reason.
 */
UCLASS()
class CATACLYSM_API UCataclysmSkillEffects : public UObject
{
	GENERATED_BODY()

public:
	/** The name of the burn effect's row in the generated status effect table. */
	static const TCHAR* BurnRowName;

	/**
	 * The name of the bleed effect's row in that same table. Issue #1032.
	 *
	 * THE DESIGNED NUMBERS RATHER THAN INVENTED ONES. `game/Data/StatusEffects.csv`
	 * gives `DoT_Bleed` 20 damage a second for 5 seconds. The Masochist's
	 * Mutilation Mastery says only "apply Bleeding" and states neither a
	 * magnitude nor a duration, so the sheet is where both have to come from.
	 */
	static const TCHAR* BleedRowName;

	/**
	 * What one basic attack from this character deals.
	 *
	 * The design's anchor: the Skill Slots sheet gives the basic attack 100%
	 * because it IS weapon damage, and every other slot is a percentage of it.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill Effects")
	static float WeaponDamageOf(const UAbilitySystemComponent* AbilitySystem);

	/**
	 * Deal one hit worth DamagePercent of the caster's weapon damage.
	 *
	 * THE CASTER'S OWN MODIFIERS APPLY HERE. Weapon damage times the skill's
	 * percentage is the BASE; the caster's stat modifiers then run over it
	 * through UCataclysmStatPipeline, which is where a self buff's magnitude
	 * becomes a number. Which modifiers reach it is decided by SkillTags: an
	 * increase scoped to Element.Demonic applies to a Demonic skill and to no
	 * other. Issue #166.
	 *
	 * @param SkillTags  the tags of the skill dealing the hit. An empty
	 *                   container means only unscoped and Scope.Global
	 *                   modifiers apply, which is the right answer for a hit
	 *                   that belongs to no skill.
	 * @param OutResolved  filled with what became of the blow: whether it was
	 *                     evaded or blocked, and what reached armour, resistance,
	 *                     the energy shield and health. Optional, and most
	 *                     callers pass nothing. Issue #1156: the return value
	 *                     below cannot answer any of that, because it is decided
	 *                     before the defender is consulted.
	 *
	 * @return the damage SENT, before the defender's mitigation. Zero when
	 *         either side is missing or the caster has no weapon damage.
	 *
	 *         **THIS IS NOT WHAT LANDED AND NEVER WAS.** Evasion, block, armour,
	 *         resistance and flat reduction are applied afterwards, inside the
	 *         defender's own `UCataclysmVitalAttributeSet::
	 *         PostGameplayEffectExecute`. A caller asking whether the blow landed
	 *         must read `OutResolved`, not this. The figure is still the right
	 *         one to scale a rider by, which is what every caller uses it for.
	 */
	static float ApplyHit(AActor* Instigator, AActor* Target, float DamagePercent,
						  const FGameplayTagContainer& SkillTags = FGameplayTagContainer(),
						  const FCataclysmHitDelivery& Delivery = FCataclysmHitDelivery(),
						  FCataclysmDamageResult* OutResolved = nullptr);

	/**
	 * What one hit of this size, from this caster, is worth after the caster's
	 * own stat modifiers.
	 *
	 * Separated from ApplyHit so a test can read the number without a defender,
	 * and so the burning ground can price a tick the same way a hit is priced.
	 */
	static float ModifiedDamage(const UAbilitySystemComponent* Source,
								float BaseDamage,
								const FGameplayTagContainer& SkillTags);

	/**
	 * Deal a hit of an amount already worked out.
	 *
	 * For the callers that hold an absolute number rather than a percentage:
	 * a burning patch of ground knows what one of its ticks is worth, because
	 * the skill that left it worked that out once when it was created.
	 *
	 * @return whether damage was sent
	 */
	/**
	 * Take health off an actor without any of it being a hit.
	 *
	 * NOT A HIT, AND THAT IS THE WHOLE POINT. It is written straight to the
	 * Health attribute rather than to the Damage meta attribute, so
	 * UCataclysmDamageCalculation::Resolve never sees it: no evasion roll, no
	 * block, no armour, no resistance, no critical strike, no ailment, and no
	 * retaliation of its own. What it does still do is clamp at zero and report
	 * a death, because the vital attribute set handles the Health attribute
	 * changing as well as the Damage one.
	 *
	 * RETALIATION IS THE ONLY CALLER AND THE SHAPE COMES FROM THE GENRE. Last
	 * Epoch's reflected damage "does not Hit and instead directly reduces Health
	 * and Ward without going through damage calculations", and Path of Exile's
	 * reflected damage cannot critically strike, cannot cause ailments and does
	 * not trigger on-hit effects. Without this, two characters who both retaliate
	 * would reflect at one another without end.
	 *
	 * @return whether any health was taken
	 */
	static bool ReduceHealthDirectly(AActor* Instigator, AActor* Target,
									 float Amount);

	/**
	 * Send a blow at a target and, if asked, report what became of it.
	 *
	 * @param OutResolved  filled with what the defender's own attribute set made
	 *                     of the blow -- evaded, blocked, what armour and
	 *                     resistance took, what reached health. Left at its
	 *                     defaults when the blow never reached the attribute set
	 *                     at all, which a caller cannot tell apart from an
	 *                     untouched hit and does not need to: both mean nothing
	 *                     landed. Optional; every caller but `ApplyHit` ignores
	 *                     it. Issue #1156.
	 *
	 * @return whether damage was SENT, which is not whether any of it landed.
	 */
	static bool ApplyDirectDamage(AActor* Instigator, AActor* Target, float Damage,
								  const FCataclysmHitDelivery& Delivery =
									  FCataclysmHitDelivery(),
								  FCataclysmDamageResult* OutResolved = nullptr);

	/**
	 * Set a target alight, for the duration and share the DoTs sheet states.
	 *
	 * ONE STACK ONLY, which the design states for every effect a player can
	 * apply. A second application refreshes the duration rather than adding a
	 * second burn, which matters here more than anywhere else: fifteen of the
	 * sixteen designed Demonic skills apply burn, and several apply it many
	 * times a second.
	 *
	 * A DESIGNED BURN LANDS WHATEVER THE HIT DID, AND AN INCIDENTAL ONE NEEDS
	 * THE HIT TO HAVE HURT. Issue #917, settled on 2026-09-02. Until then this
	 * refused any hit that dealt nothing, which was arithmetic while burn was a
	 * percent of the hit and became a decision when burn went flat on
	 * 2026-08-24 -- a fully mitigated hit would otherwise have set a target
	 * alight at full strength.
	 *
	 * WHY THE THRESHOLD RATHER THAN ALLOWING IT OUTRIGHT. The design already
	 * says damage over time bypasses an energy shield, so ailments are the
	 * existing route past one defensive layer. Letting an incidental one ignore
	 * every layer is a larger change than it looks and nobody asked for it. A
	 * designed one is different: it is part of what the skill IS, so a Support
	 * skill dealing no damage by design still sets alight what its sentence
	 * says it does.
	 *
	 * THE THRESHOLD IS ON MAXIMUM HEALTH AND NOT ON THE HIT, so a scratch
	 * against a Boss cannot ignite it while the same scratch against a Common
	 * enemy can. That is the shape stunning already has and the reason is the
	 * same: what matters is whether the blow was a real blow for that target.
	 *
	 * IT IS THE SAME NUMBER AS THE STUN'S AND READS `StunDamageThresholdPercent`
	 * RATHER THAN DECLARING A SECOND ONE. The design states one figure and one
	 * figure is what the code holds, so moving it moves both.
	 *
	 * WHAT IT DOES NOT TAKE FROM THE STUN: the five second immunity window and
	 * boss immunity. Those belong to hard stops -- effects that completely stop
	 * a target operating -- and an ailment is damage over time. A boss burns.
	 *
	 * **THE THRESHOLD CANNOT FIRE TODAY AND THAT IS WORTH KNOWING.**
	 * `HitDamage` is what the attacker SENT, not what got through: `ApplyHit`
	 * returns the figure it handed to `ApplyDirectDamage`, and evasion, block,
	 * armour and resistance are all applied afterwards, inside the defender's
	 * own attribute set. So an incidental burn always arrives here with a
	 * positive figure, and a hit that was evaded outright sets a target alight
	 * exactly as it did before this change. Issue #1156 carries that, because it
	 * is a fault in what the number means rather than in this rule.
	 *
	 * WHAT THIS RULE ACTUALLY CHANGES TODAY, THEREFORE, is one thing: a skill
	 * whose own damage is zero can now set alight what its row says it does.
	 * That is the three Support-slot rows and nothing else. Every other caller
	 * behaves exactly as it did.
	 *
	 * @param HitDamage  the damage of the hit that caused it, BEFORE the
	 *                   defender's mitigation. Ignored when the burn is designed
	 * @param bBurnIsDesigned  true when the skill's own row states it burns.
	 *                   `Params.bBurns` is exactly that, so every caller that
	 *                   reads a skill row passes it. A gem, an affix or an
	 *                   enemy modifier passes false
	 * @return whether a burn was applied
	 */
	static bool ApplyBurn(AActor* Instigator, AActor* Target, float HitDamage,
						  bool bScalesWithInstigator = true,
						  bool bBurnIsDesigned = false);

	/**
	 * A percentage-of-normal stat read as a plain multiplier.
	 *
	 * ONE HUNDRED MEANS UNCHANGED for area of effect and all three damage
	 * over time stats, which is what the design gives them: "They are
	 * percentages of whatever the skill or the effect itself does, so their
	 * baseline is 100% rather than zero." So 148 reads as 1.48, and a
	 * character with no such attribute gets 1.0 rather than 0.
	 *
	 * A NULL SOURCE ANSWERS 1.0, which is how a blow dealt in someone
	 * else's name declines the scaling without every caller writing a
	 * branch of its own.
	 */
	static float AsMultiplier(const UAbilitySystemComponent* Source,
							  const FGameplayAttribute& Stat);

	/**
	 * The same reading, but for the skill in hand rather than the character.
	 *
	 * WHY IT IS A SECOND FUNCTION AND NOT THE ONLY ONE. `AsMultiplier` above
	 * reads a finished gameplay attribute, which holds the value with no skill
	 * in hand, so every modifier naming a required tag is missing from it. That
	 * is the right answer for a character sheet and the wrong one for a skill:
	 * "+15% area of effect for traps" must widen a trap and nothing else. Issue
	 * #943.
	 *
	 * BOTH THE ATTRIBUTE AND THE NAME ARE NEEDED, and they are not the same
	 * thing. The attribute says whether this ability system has the stat at all
	 * and supplies the value to fall back on; the name is the key
	 * `UCataclysmPlayerClassStats::ApplyTo` recorded the stat's inputs under.
	 * `UCataclysmPlayerClassStats::StatToAttribute` is where the pairing is
	 * stated.
	 *
	 * IT FALLS BACK TO EXACTLY WHAT `AsMultiplier` WOULD HAVE ANSWERED whenever
	 * nothing was recorded -- an enemy, or a character before its first refresh
	 * -- so no caller is worse off for using this one.
	 */
	static float AsMultiplierForSkill(const UAbilitySystemComponent* Source,
									  const FGameplayAttribute& Stat,
									  FName StatName,
									  const FGameplayTagContainer& SkillTags);

	/** One tick a second before the attacker's frequency stat is applied. */
	static constexpr float BaseSecondsPerTick = 1.0f;

	/**
	 * What a damage over time effect will do, given who is applying it.
	 *
	 * ALL THREE STATS MULTIPLY, which the design states and which is the whole
	 * reason there are three of them rather than one: "A character with 48% more
	 * of each does not deal 148% of the base total; it deals 1.48 x 1.48 x 1.48,
	 * which is 324%."
	 *
	 * FREQUENCY DIVIDES THE INTERVAL rather than multiplying it, because it is a
	 * rate. The design gives cooldown reduction that form and says damage over
	 * time frequency shares it.
	 *
	 * @param Source  the attacker, or null for a blow dealt in someone else's
	 *                name, which takes none of these three
	 */
	static FCataclysmDamageOverTimeNumbers DamageOverTimeNumbers(
		const UAbilitySystemComponent* Source, float DamagePerTick,
		float DurationSeconds);

	/** Burn's duration and damage share, or bUsable false if it has none. */
	static FCataclysmStatusEffectNumbers BurnNumbers();

	/** Bleed's duration and damage, or bUsable false if it has none. #1032. */
	static FCataclysmStatusEffectNumbers BleedNumbers();

	/**
	 * One status effect's designed numbers, read from the generated table.
	 *
	 * SHARED BY THE TWO ABOVE RATHER THAN COPIED, since issue #1032 needed a
	 * second one. The rule it enforces is the same for both and is worth stating
	 * once: an effect with no duration, or with no amount, is indistinguishable
	 * from an effect nobody wrote, which is exactly how the missing cooldown in
	 * issue #155 stayed hidden.
	 *
	 * @param RowName    the row in `game/Data/StatusEffects.csv`
	 * @param HumanName  what to call it in the warning, so a reader knows which
	 *                   effect is unusable without looking the row up
	 */
	static FCataclysmStatusEffectNumbers StatusEffectNumbers(
		const TCHAR* RowName, const TCHAR* HumanName);

	/**
	 * The same numbers, found from a status tag rather than a row name.
	 *
	 * WHY BOTH EXIST. Burn and Bleed are asked for by name, because the code
	 * asking is written for those two specifically. Everything that reads a
	 * skill's `Effect` cell has a tag instead, and turning one into the other at
	 * every call site is three lines of `FName` and `FString` juggling that
	 * would be copied rather than shared.
	 *
	 * Answers a struct with `bUsable` false when the tag names no row.
	 */
	static FCataclysmStatusEffectNumbers NumbersForEffectTag(
		const FGameplayTag& EffectTag);

	/**
	 * Which row of `game/Data/StatusEffects.csv` a lasting effect's tag names.
	 *
	 * THE OTHER DIRECTION FROM EVERYTHING ELSE HERE, and issues #1057 and #1058
	 * are why it is needed. Every existing caller knows which effect it wants and
	 * asks for its numbers by row name. Those two nodes start from a tag the
	 * character is already carrying -- "a random debuff you carry" -- and have to
	 * find out what it is before they can put it on anybody else.
	 *
	 * THE RULE IS THE ONE `tools/generate_gameplay_tags.py` ALREADY USES, read
	 * backwards: an effect's tag is its `EffectName` with every non-alphanumeric
	 * character removed, so "Void Splinter" becomes `VoidSplinter`. This compares
	 * the tag's LAST SEGMENT against that, which makes it right for both branches
	 * without knowing there are two: `Status.DoT.VoidSplinter` and
	 * `Keyword.DoT.VoidSplinter` are the same effect and both end in the same
	 * word.
	 *
	 * `State.Stunned` DELIBERATELY MATCHES NOTHING, and that is a fact about the
	 * data rather than a special case here. The stun row is `Debuff_Stun`, whose
	 * name reduces to `Stun`, and a stunned character carries `State.Stunned`.
	 * The consequence is that a character cannot pass its own stun on, which is
	 * the right outcome -- a stun applied with no hit behind it would go round
	 * the design's two anti-stun-lock rules -- but it is not the reason the rule
	 * is written this way.
	 *
	 * @return `NAME_None` when the tag is invalid, the table is missing, or no
	 *         row's name reduces to the tag's last segment
	 */
	static FName StatusEffectRowForTag(const FGameplayTag& EffectTag);

	/**
	 * Whether a skill carrying these tags deals AREA damage, which cannot be
	 * evaded.
	 *
	 * READ OFF THE SKILL'S OWN TAGS, which is where the answer already lived.
	 * `game/Data/WeaponSkills.csv` gives every designed skill a tag list and 37
	 * of them already say this: 33 carry `Type.AOE.PointBlank` and 4 carry
	 * `Type.AOE.Aura`. Nothing had to be invented and no call site has to decide.
	 *
	 * `Type.AOE.Persistent` DOES NOT COUNT, and that is the whole subtlety. The
	 * vocabulary defines it as "Ground effects, clouds, zones", so it describes
	 * the patch of burning ground a skill LEAVES rather than the blow it lands.
	 * Flamedart carries it and is a charge: the charge makes contact and is
	 * evadable, and the fire trail it leaves is a separate thing that damages
	 * whatever stands in it. The zone marks its own ticks as area damage where it
	 * deals them, in ACataclysmGroundZone.
	 *
	 * A SKILL WITH NO AREA TAG DEALS A DIRECT HIT. Cinderslash is
	 * `Type.Strike, Type.Melee` and nothing else, so it is one sword blow and can
	 * be evaded -- which is right, and is what an earlier version of this got
	 * wrong by treating every Strike as area damage.
	 *
	 * AN ENEMY'S ABILITY HAS NO TAG LIST, because enemy abilities are C++
	 * constants rather than rows in the skill matrix. Those pass
	 * `FCataclysmHitDelivery::Area()` instead, which sets the same flag. The
	 * Brute's stomp and the Abyssal Warden's ring are the two that do.
	 */
	static bool IsAreaDamage(const FGameplayTagContainer& SkillTags);

	/**
	 * Whether this skill is a spell, which decides whether spell damage applies.
	 *
	 * BY THE TAG ON THE SKILL, which is how Path of Exile scopes the same stat:
	 * a skill counts as a spell if it carries the Spell tag, and spell damage
	 * modifiers apply to those and to nothing else. In this project the tag is
	 * `Type.Spell` and it sits on the nine Wand and Staff skills -- the two
	 * Magic weapon types, which are also the two weapons whose implicits grant
	 * increased spell damage.
	 */
	static bool IsSpell(const FGameplayTagContainer& SkillTags);

	/** `Type.Spell`. See IsSpell. */
	static const TCHAR* SpellTagName;

	/**
	 * The attacker's increased damage against this target's own damage type.
	 *
	 * A FRACTION, and zero when the target has no type of its own or the
	 * attacker has nothing against it. Only an enemy carries a damage type: see
	 * DamageTypeOf.
	 */
	static float DamageAgainstTypeOf(const UAbilitySystemComponent* Source,
									 const AActor* Target);

	/**
	 * This character's flat spell damage, or zero when it has none.
	 *
	 * ADDED TO A SPELL'S BASE RATHER THAN REPLACING IT. The project owner chose
	 * on 2026-08-24 that a spell keeps the weapon's damage and this is added on
	 * top, rather than the Path of Exile shape where a spell ignores the weapon
	 * entirely. That keeps a Wand's own 38 flat damage worth something to the
	 * caster holding it.
	 *
	 * ASKED FOR WITH THE SKILL'S TAGS RATHER THAN READ OFF THE ATTRIBUTE, since
	 * issue #958. The attribute holds the figure worked out with no skill in
	 * hand and with nothing known about the character, so a modifier naming a
	 * required tag and a modifier that depends on the character's state are both
	 * missing from it. "While at or below 35% health, +2% increased damage per
	 * point" is one of the second kind. The attribute is still the fallback, so
	 * an ability system that recorded nothing answers exactly what it did before.
	 *
	 * AND WITH WHAT THE SKILL COST, since issue #983, for the same reason:
	 * a modifier conditioned on the skill's own health cost is another the
	 * attribute could not carry. -1 means no skill in hand and refuses it.
	 */
	static float SpellDamageOf(const UAbilitySystemComponent* Source,
							   const FGameplayTagContainer& SkillTags,
							   float SkillHealthCostPercent = -1.0f);

	/**
	 * The sum of increases already applied to this character's attack damage.
	 *
	 * A hit needs it to reopen the increases bracket, so that a conditional
	 * increase can join it rather than becoming a second multiplier. Zero for an
	 * ability system this project did not make, which leaves the arithmetic
	 * exactly as it was before that mattered.
	 *
	 * THIS IS THE BRACKET TO UNDO, NOT THE ONE TO REDO. It is what
	 * `UCataclysmPlayerClassStats::ApplyTo` folded into the gameplay attribute,
	 * and it worked that out with no skill in hand and nothing known about the
	 * character. `IncreasesForSkill` below is the same sum worked out again for
	 * the skill being used and the state the character is in, and a hit needs
	 * both: one to take the attribute apart and the other to put it together.
	 */
	static float IncreasesBehindAttackDamage(
		const UAbilitySystemComponent* Source);

	/**
	 * The sum of increases attack damage should be multiplied by right now.
	 *
	 * A FRACTION, like `IncreasesBehindAttackDamage`, and it differs from that
	 * one by exactly the modifiers which could not be judged when the attribute
	 * was written: those scoped to a skill's tags, and those that depend on the
	 * character's state. With neither present the two are equal and the
	 * arithmetic is unchanged. Issues #947 and #958.
	 *
	 * IT FALLS BACK TO `IncreasesBehindAttackDamage` when nothing was recorded
	 * for attack damage, which is the ordinary case for an enemy and for a
	 * player before its first stat refresh.
	 *
	 * AND BY THOSE CONDITIONED ON WHAT THE SKILL COST, since issue #983.
	 * -1 means no skill in hand, which refuses such a modifier.
	 */
	static float IncreasesForSkill(const UAbilitySystemComponent* Source,
								   const FGameplayTagContainer& SkillTags,
								   float SkillHealthCostPercent = -1.0f);

	/** The two tags that make a skill's hit area damage. */
	static const TCHAR* PointBlankAreaTagName;
	static const TCHAR* AuraAreaTagName;

	/**
	 * Which of the defender's eight resistances this attacker's damage is met by.
	 *
	 * An enemy's own damage type, and `NAME_None` for anything else. See the
	 * definition for why only one side of a fight is typed.
	 */
	static FName DamageTypeOf(const AActor* Attacker);

	/**
	 * Apply damage over a duration, scaled by the attacker's three stats.
	 *
	 * A FIXED AMOUNT PER TICK, NOT A TOTAL HANDED OUT IN INSTALMENTS, and until
	 * issue #895 it was the other way round. The design says so outright: "A
	 * damage over time effect deals a fixed amount per tick. It is not a total
	 * handed out in instalments. A bleed that deals 20 damage per tick, ticks
	 * once per second and lasts 5 seconds deals 100 damage in total, and every
	 * one of those three numbers can be raised on its own." It calls that a
	 * deliberate departure from Path of Exile and Last Epoch, both of which
	 * spread a total.
	 *
	 * THAT DIFFERENCE IS THE WHOLE REASON THERE ARE THREE STATS. Spreading a
	 * total means raising the tick rate delivers the same damage sooner and adds
	 * nothing, so `Stat_Increased_damage_over_time_frequency` could not have been
	 * worth anything. The project owner chose the per-tick reading on 2026-08-24.
	 *
	 * ALL THREE MULTIPLY. "A character with 48% more of each does not deal 148%
	 * of the base total; it deals 1.48 x 1.48 x 1.48, which is 324%."
	 *
	 *   Damage over Time            each tick hits harder
	 *   Damage over Time Frequency  more ticks in the same time
	 *   Damage over Time Duration   the effect runs for longer
	 *
	 * FREQUENCY DIVIDES THE INTERVAL RATHER THAN MULTIPLYING IT, because it is a
	 * rate, which is the same form the design gives cooldown reduction: "Damage
	 * over time frequency uses the same form, because it is also a rate."
	 *
	 * @param DamagePerTick  what ONE tick deals before the attacker's stats
	 * @param DurationSeconds  before the attacker's duration stat
	 * @param EffectTag    granted for the duration, and what makes it one stack
	 * @param bScalesWithInstigator  false for a blow dealt in someone else's
	 *                     name. A minion's burn is applied with its summoner as
	 *                     the instigator, and the design names damage over time
	 *                     among what a minion does not take from its summoner.
	 */
	static bool ApplyDamageOverTime(AActor* Instigator, AActor* Target,
									float DamagePerTick, float DurationSeconds,
									const FGameplayTag& EffectTag,
									bool bScalesWithInstigator = true);

	/**
	 * Grant a tag for a duration and nothing else.
	 *
	 * WHAT A BUFF OR A DEBUFF IS UNTIL ITS MAGNITUDE CAN BE APPLIED. Burning
	 * Wrath's more fire damage, Martyr's Ember's stored damage and
	 * Subjugate's Madness all name effects this project has no attribute or hook
	 * for. Granting the tag makes the duration real and makes "is it up?" a
	 * question with a true answer, which is what everything else can be built
	 * against later. Issue #166.
	 *
	 * ONE STACK ONLY, refreshed rather than added to, as the design requires of
	 * every player-applied effect.
	 */
	static bool ApplyTagForDuration(AActor* Instigator, AActor* Target,
									const FGameplayTag& EffectTag,
									float DurationSeconds);

	/**
	 * Apply a named status effect together with the stat change it carries.
	 *
	 * WHAT THIS ADDS OVER `ApplyTagForDuration` ABOVE, which grants the tag and
	 * nothing else: a debuff whose sentence names a number now applies that
	 * number. Shred is the first. Its row in `game/Data/StatusEffects.csv` reads
	 * "reduces the affected enemy's resistance by 10 for 6 seconds", and until
	 * this the tag went on and the resistance did not move.
	 *
	 * ONLY SHRED CARRIES A STAT TODAY, and the mapping from an effect to the
	 * attribute it moves is written in the .cpp rather than in the sheet.
	 * `game/Data/StatusEffects.csv` has a `Strength` column and no column saying
	 * what the strength is OF, so a second effect wanting one is the point at
	 * which that column should be added rather than a second name written into
	 * C++. Issue #1144.
	 *
	 * WHICH RESISTANCE IT REDUCES COMES FROM THE SKILL, not from the effect.
	 * Anathema reads "Demonic resistance cut by 40%" and carries
	 * `Element.Demonic`; a Shred from a Death skill should cut Death resistance.
	 * Passing no damage type reduces every resistance at once, through the
	 * generic All Resistance attribute.
	 *
	 * THE REDUCTION CANNOT TAKE A RESISTANCE PAST ZERO. `game/Data/StatusEffects.csv`
	 * says so for Shred: "magnitude raises the reduction until that resistance
	 * reaches zero, then extends the duration instead". The clamp is here; the
	 * spilling-into-duration half is not built, and #1144 carries it.
	 *
	 * @param Magnitude  the size to apply, or zero or less to take the effect's
	 *                   own designed Strength out of the status effect table
	 * @param DamageType the attacker's damage type, deciding which resistance is
	 *                   reduced. None reduces all resistance.
	 * @return whether anything was applied
	 */
	static bool ApplyNamedEffect(AActor* Instigator, AActor* Target,
								 const FGameplayTag& EffectTag,
								 float DurationSeconds,
								 float Magnitude = 0.0f,
								 FName DamageType = NAME_None);

	/**
	 * Copy every debuff this actor carries onto each of these others.
	 *
	 * WHAT ASKS FOR IT. The Wand's Malefice, "copying every curse it already
	 * carries onto the two nearest enemies", and its Anathema, whose curse
	 * "passes to the nearest living enemy" when a damned enemy dies. Both are
	 * the Wand's designed verb, which `docs/DECISIONS.md` gives as inflicting.
	 *
	 * THE COPY LASTS THE EFFECT'S OWN DESIGNED DURATION, not whatever is left of
	 * the original. Neither row states a duration for the copy, and reading the
	 * remaining time off an active gameplay effect and handing it on would make
	 * a curse spread late worth almost nothing, which is not what either
	 * sentence describes.
	 *
	 * IT COPIES DEBUFFS AND NOT EVERY TAG. `UCataclysmDebuffs::TagsOnActor`
	 * answers only the tags under the debuff roots, so a burn, a stun or a buff
	 * the target happens to carry is not spread by this.
	 *
	 * @return how many effects were applied, summed over every recipient
	 */
	static int32 CopyDebuffsTo(AActor* Instigator, const AActor* From,
							   const TArray<AActor*>& To,
							   FName DamageType = NAME_None);

	/**
	 * Take back every effect on this actor that grants the tag.
	 *
	 * THE OTHER HALF OF ApplyTagForDuration, AND THE FIRST THING THAT NEEDED
	 * ONE. Everything granted before this was granted for a stated duration and
	 * left to expire, which is right for a curse or a burn: the moment it ends
	 * is part of what it is. An AURA is not like that. The Succubus's Dominion
	 * is held on for as long as the creature lives, and the design's whole point
	 * is that killing it ends the buff AT ONCE -- "killing it first is the
	 * correct play". Waiting out a duration would make that false, and no
	 * duration short enough to hide it is long enough to survive between
	 * refreshes.
	 *
	 * IT REMOVES BY TAG RATHER THAN BY HANDLE, which is a deliberate
	 * simplification and has a consequence worth stating: two casters granting
	 * the same tag to one target share one effect, because these are all single
	 * stack, so either caster removing it removes it for both. The survivor's
	 * next refresh puts it back. Keeping handles per grantor would make that
	 * exact and is more machinery than the gap is worth. Issue #768 is where
	 * that would be revisited.
	 *
	 * @return how many effects were removed
	 */
	static int32 RemoveEffectsGranting(AActor* Target,
									   const FGameplayTag& EffectTag);

	/** Whether this actor currently carries the tag. */
	static bool HasTag(const AActor* Actor, const FGameplayTag& Tag);

	/** The Keyword.DoT.Burn tag, or an invalid tag if the vocabulary lacks it. */
	static FGameplayTag BurnTag();

	// --- Stun -----------------------------------------------------------------
	//
	// THE FIRST THING IN THE PROJECT THAT CAN HOLD A CHARACTER STILL. Section VI
	// of docs/Cataclysm_GDD_v2.md defines a stun as "the target cannot act at
	// all", separates it from a slow such as Cripple, and gives it three rules
	// against stun-locking. sim/cataclysm_sim/damage.py is the model those rules
	// are ported from and the names below follow it deliberately.
	//
	// The stun itself is a tag held for a duration, which is how every other
	// timed effect in this project works. What the tag stops is decided by the
	// things that read it: ACataclysmEnemyController refuses to act while it is
	// up and ACataclysmPlayerController refuses to move or activate a skill.

	/**
	 * How much of a target's maximum health one hit must take to stun it.
	 *
	 * The first anti-stun-lock rule, and the reason small hits cannot interrupt
	 * constantly. Measured against damage ACTUALLY DEALT rather than damage
	 * swung, so a hit that armour reduced to a scratch is a scratch.
	 * Mirrors STUN_DAMAGE_THRESHOLD in sim/cataclysm_sim/damage.py.
	 */
	static constexpr float StunDamageThresholdPercent = 10.0f;

	/**
	 * Seconds a target that has just been stunned cannot be stunned again for.
	 *
	 * The second anti-stun-lock rule. Mirrors STUN_IMMUNITY_SECONDS in
	 * sim/cataclysm_sim/damage.py, and it is the same five seconds the design
	 * reuses for displacement diminishing returns rather than a second number.
	 *
	 * NOTHING ENFORCED THIS BEFORE. damage.py says in terms that it resolves one
	 * hit with no clock and that the game enforces the window; the game had no
	 * stun at all, so this is the first implementation of it anywhere.
	 */
	static constexpr float StunImmunityWindowSeconds = 5.0f;

	/**
	 * Hold a target still for a duration, honouring the anti-stun-lock rules.
	 *
	 * THE THIRD RULE IS NOT CHECKED HERE, because it cannot be. "A boss cannot
	 * be stunned at all" needs a boss, and no boss concept exists anywhere in
	 * game/Source -- no flag, no class, no tag. Issue #395 covers adding one.
	 * Until it exists this function would have nothing to ask.
	 *
	 * @param DamageDealt       the damage this hit actually did, after the
	 *                          defender's mitigation. Ignored when the stun is
	 *                          designed.
	 * @param bStunIsDesigned   true for a stun that is the point of the attack,
	 *                          such as the Brute's Stomp. A designed stun skips
	 *                          the damage threshold, because an attack built to
	 *                          stun should not fail to when it lands. It does
	 *                          NOT skip the immunity window.
	 * @return whether a stun was applied
	 */
	static bool ApplyStun(AActor* Instigator, AActor* Target,
						  float DurationSeconds, float DamageDealt,
						  bool bStunIsDesigned);

	/**
	 * What a crowd control effect is worth against this target, after its
	 * crowd control resistance.
	 *
	 * WHY IT EXISTS. Until 2026-09-05 the `CrowdControlResistance` attribute was
	 * read in exactly one place: it reduced the CHANCE that a blunt weapon
	 * caused an incidental stun. It did nothing about the four skills that stun
	 * by design, nothing about a slow, and nothing about a shove. So a stat on
	 * the character sheet -- granted by an affix on seven gear slots, by two
	 * class lines and by a helmet implicit -- was worth almost nothing.
	 *
	 * IT REDUCES HOW MUCH, NOT WHETHER. A stun's seconds and a shove's
	 * centimetres are both scaled by it. Diablo IV's impairment reduction stat
	 * works this way, shortening a crowd control effect rather than rolling
	 * against it, and reducing an amount is the only rule that can serve a
	 * DESIGNED stun, which has no chance to reduce.
	 *
	 * **AT 100 NOTHING LANDS, AND THIS IS THE ONE PLACE THIS GAME LETS A STAT
	 * REACH IMMUNITY.** Every other unconditional mitigation layer is capped
	 * short of it on purpose -- armour at 75%, resistance at 70%, flat damage
	 * reduction at 75% -- and `UCataclysmDamageCalculation::DamageReductionCap`
	 * states the rule outright: "No combination of these layers reaches
	 * immunity." **The project owner was shown that this contradicts it, with
	 * the figures a player can actually reach, and chose an uncapped stat anyway
	 * on 2026-09-05.** `docs/DECISIONS.md` records it, so the next person finds
	 * a decision rather than a contradiction. Diablo IV makes the other choice
	 * and keeps immunity as a separate mechanic.
	 *
	 * @param Amount  seconds, or centimetres, or whatever the effect is measured
	 *                in. Zero or less answers zero
	 * @return what is left, or zero when the target resists it entirely. A
	 *         target with no such attribute gets the amount back unchanged,
	 *         which is every creature nothing has given one to
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill Effects")
	static float AfterCrowdControlResistance(const AActor* Target, float Amount);

	/** Whether this actor is stunned right now and may not act. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill Effects")
	static bool IsStunned(const AActor* Actor);

	/**
	 * Shove a target directly away from whatever hit it.
	 *
	 * THE ONE DEFINITION OF DISPLACEMENT, and it is here rather than on the skill
	 * template because both directions need it. The template's own knockback rider
	 * calls this, and so do the enemy abilities that shove the player, which are
	 * not skill templates at all -- an enemy attack is C++ on the creature.
	 * Issue #625. Before that this body lived inside
	 * `UCataclysmSkillTemplate::ApplyKnockbackTo` and only a player skill could
	 * reach it.
	 *
	 * THE DIRECTION IS AWAY FROM THE INSTIGATOR, ALONG THE GROUND, and a target
	 * standing exactly on its instigator is not moved at all: there is no
	 * direction to push in and picking one would shove somebody somewhere they
	 * could not have predicted.
	 *
	 * HALVED FOR EACH DISPLACEMENT THE TARGET HAS ALREADY TAKEN inside the five
	 * second window, which is `TakeNextDisplacementShare` on the target's ability
	 * system component. That rule was written for "a target" rather than "an
	 * enemy" precisely so it would hold with the player as the target, and this
	 * is where that symmetry becomes real.
	 *
	 * A DISPLACEMENT RATHER THAN AN IMPULSE, because most of what this hits has no
	 * physics body and a knockback that silently did nothing would look exactly
	 * like a knockback. Swept, so a shove into a wall stops at the wall.
	 *
	 * @param DistanceCm  how far to shove before the halving rule is applied.
	 *                    Zero or less does nothing.
	 * @return whether the target was moved
	 */
	static bool ApplyKnockback(AActor* Instigator, AActor* Target,
							   float DistanceCm);

	// --- Forced movement --------------------------------------------------
	//
	// THE FIVE VERBS `ForcedMovement` MAY NAME: Knockdown, Launch, Pull, Drag
	// and Pin. `Knockback` is deliberately not one of them and is the function
	// above; the header on `FCataclysmSkillShapeParams::ForcedMovement` says so.
	// Nine skill rows across the Spear, the Warhammer and the Whip state one or
	// more of these and until now nothing read the key at all.
	//
	// TWO OF THE FIVE ARE HOLDS AND THREE ARE DISPLACEMENTS, and section VI of
	// `docs/Cataclysm_GDD_v2.md` treats those two groups completely differently.
	// A hold is checked against the anti-stun-lock rules; a displacement is not,
	// and is limited instead by halving its distance on repeat.
	//
	//   Knockdown  a hard stop. Covered by all three anti-stun-lock rules
	//   Pin        movement only. Covered by none of them -- see `ApplyPin`
	//   Pull       displacement toward whatever applied it
	//   Drag       the same displacement, applied after the caster has moved
	//   Launch     displacement straight up

	/**
	 * Put a target on the floor for a duration, honouring the anti-stun-lock
	 * rules in full.
	 *
	 * A HARD STOP, AND THE DESIGN SAYS SO IN TERMS. Section VI of
	 * `docs/Cataclysm_GDD_v2.md` lists Knockdown beside Stun in its table of
	 * what the rule covers -- "the target cannot act at all; it is simply on the
	 * floor while it happens" -- and then spends a paragraph on why: a knockdown
	 * runs 2 to 3 seconds where every stun runs 0.75 to 1.5, so leaving it
	 * outside the rule would make the longest hold in the game the one nothing
	 * limits.
	 *
	 * IT SHARES ONE IMMUNITY WINDOW WITH THE STUN RATHER THAN HAVING ITS OWN,
	 * which is the design's own wording: "The two share one window rather than
	 * one each, because two 3-second holds taken in turn is exactly the failure
	 * the window exists to stop." So this reads and writes `State.StunImmune`,
	 * and a target just stunned cannot be knocked down either.
	 *
	 * THE SAME ONE EXEMPTION AS A STUN. A skill whose stated effect is to knock
	 * down skips the damage threshold and skips neither the window nor boss
	 * immunity. Every row that states `ForcedMovement=Knockdown` means to, so
	 * `bKnockdownIsDesigned` is true for all three of them.
	 *
	 * @param DamageDealt  what this hit actually did, after mitigation. Ignored
	 *                     when the knockdown is designed.
	 * @return whether the target was knocked down
	 */
	static bool ApplyKnockdown(AActor* Instigator, AActor* Target,
							   float DurationSeconds, float DamageDealt,
							   bool bKnockdownIsDesigned);

	/**
	 * Hold a target where it stands for a duration, and optionally make it take
	 * more damage while held.
	 *
	 * NOT A HARD STOP, AND THEREFORE NOT COVERED BY THE ANTI-STUN-LOCK RULES.
	 * This is the one judgement in forced movement that the design document does
	 * not settle outright, so the reasoning is written here rather than assumed.
	 * Section VI states the test -- an effect is covered "when it completely
	 * stops the target operating any part of its character" -- and then applies
	 * it to Disarm, which is not covered because "movement and any skill that
	 * does not need the weapon still work". A pin is that sentence with the two
	 * halves swapped: attacks, turning and any skill that does not need movement
	 * still work. So it takes no damage threshold, no immunity window and no
	 * boss exemption, exactly as a slow does not.
	 *
	 * IT IS RECORDED AS A DECISION RATHER THAN A DERIVATION, because the covered
	 * table in section VI lists seven effects and pinning is not one of them:
	 * the Spear kit that introduced `ForcedMovement=Pin` was added on 2026-09-01
	 * and no entry was written for it. Issue #1149 puts the question to the
	 * project owner. The consequence if it is wrong is that Thicket holds a boss
	 * still for 6 seconds; the boss can still fight back throughout.
	 *
	 * WHAT THE TAG STOPS IS DECIDED BY WHAT READS IT, the same way a stun works.
	 * `ACataclysmEnemyController` stops ordering a pinned creature to walk and
	 * lets it keep attacking, and `ACataclysmPlayerController` refuses movement
	 * input and allows skills.
	 *
	 * @param DamageTakenIncrease  percentage points added to the target's Damage
	 *                             Taken stat for as long as the pin lasts, or
	 *                             zero for none. The Spear's Impale states 30:
	 *                             "while a target is pinned it takes 30% more
	 *                             damage from every source". It rides on the
	 *                             same gameplay effect as the tag, so it is
	 *                             taken back when the pin ends and cannot be
	 *                             left behind by any path.
	 * @return whether the target was pinned
	 */
	static bool ApplyPin(AActor* Instigator, AActor* Target,
						 float DurationSeconds,
						 float DamageTakenIncrease = 0.0f);

	/**
	 * Take a pin off a target early.
	 *
	 * WHAT ASKS FOR IT. The Spear's Skewer: "the whole line is held together for
	 * 4 seconds. Killing any one of them frees the rest." `UCataclysmPinnedLine`
	 * is what remembers which creatures are in one line and calls this on the
	 * survivors.
	 *
	 * THE DAMAGE TAKEN INCREASE GOES WITH IT, because the two ride on one
	 * gameplay effect and this removes the effect rather than the tag.
	 *
	 * @return whether a pin was actually removed
	 */
	static bool ReleasePin(AActor* Target);

	/**
	 * Haul a target toward whatever applied it.
	 *
	 * THE SAME DISPLACEMENT AS A KNOCKBACK, POINTED THE OTHER WAY, and it goes
	 * through the same halving rule for the reason the design gives: a pull is
	 * a displacement, section VI does not cover displacement with the
	 * anti-stun-lock rules, and what limits it instead is that each one inside
	 * the five second window moves the target half as far as the one before.
	 *
	 * ZERO DISTANCE MEANS ALL THE WAY, WHICH IS WHAT THE TWO ROWS ASK FOR AND
	 * WHAT NEITHER OF THEM STATES. The Whip's The Gathering hauls enemies "into
	 * a burning heap at your feet" and its Reel dumps them "at your feet", and
	 * neither writes `ForcedMovementDistance`. A stated distance is honoured and
	 * moves the target that far toward the instigator instead. The halving still
	 * applies to both, so a second haul inside the window brings a target half
	 * the remaining way rather than all of it.
	 *
	 * A TARGET STANDING ON ITS INSTIGATOR IS NOT MOVED, for the reason a
	 * knockback does not move one: there is no direction, and it is already
	 * where a pull would put it.
	 *
	 * @param DistanceCm  how far to haul before the halving rule, or zero or
	 *                    less for the whole distance to the instigator
	 * @return whether the target was moved
	 */
	static bool ApplyPull(AActor* Instigator, AActor* Target,
						  float DistanceCm);

	/**
	 * Throw a target straight up.
	 *
	 * THE WARHAMMER'S UPTHRUST IS THE ONLY ROW THAT STATES IT: "anything
	 * standing where it rises is thrown into the air and set alight."
	 *
	 * A DISPLACEMENT AND NOT A HOLD. The row states no
	 * `ForcedMovementDuration`, and nothing about being in the air stops a
	 * character acting, so this takes no anti-stun-lock rule. It is halved on
	 * repeat like every other displacement.
	 *
	 * SWEPT, so a launch under a low ceiling stops at the ceiling rather than
	 * putting the target inside it. That is the same reason `ApplyKnockback`
	 * sweeps.
	 *
	 * WHAT BRINGS IT BACK DOWN IS GRAVITY AND NOTHING HERE. A character with a
	 * movement component falls; a bare actor with none stays where it was put,
	 * which is what the test harness sees and is why a test measures the rise
	 * rather than the fall.
	 *
	 * @param DistanceCm  how far up, before the halving rule. Zero or less does
	 *                    nothing, so a row that states no distance launches
	 *                    nobody rather than guessing a height.
	 * @return whether the target was moved
	 */
	static bool ApplyLaunch(AActor* Instigator, AActor* Target,
							float DistanceCm);

	/** Whether this actor is pinned right now and may not move. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill Effects")
	static bool IsPinned(const AActor* Actor);

	/** The tag a character carries while nothing can hit it. */
	static FGameplayTag UntargetableTag();

	/**
	 * Make this actor impossible to hit for a while.
	 *
	 * ONE ROW STATES IT: the Dagger's Everywhere at Once, "for 4 seconds you are
	 * nowhere long enough to be hit ... nothing can touch you between arrivals".
	 * Its `Untargetable=1` is what asks.
	 *
	 * IT REFUSES THE HIT RATHER THAN HIDING THE CHARACTER, and that is the
	 * narrower of the two readings on purpose. "Nothing can touch you" is a
	 * statement about what lands, so `ApplyHit` refuses a blow against a carrier
	 * and every source of damage in the game goes through that one function.
	 * Making enemies fail to FIND the character instead would mean changing what
	 * the enemy brain searches, and a creature that forgot its target for four
	 * seconds would still be wandering off after the skill ended.
	 *
	 * IT IS NOT A HARD STOP AND DOES NOT JOIN `CannotAct`. The character keeps
	 * acting throughout -- the whole skill is a string of blows -- so this is the
	 * opposite of a stun rather than a relative of it.
	 *
	 * DAMAGE OVER TIME ALREADY ON THE CHARACTER KEEPS TICKING, because a burn is
	 * applied once and then deals its damage through the attribute set rather
	 * than through `ApplyHit`. "Nothing can touch you" is about being reached,
	 * and a fire already burning has reached you.
	 *
	 * @return whether the tag was applied
	 */
	static bool ApplyUntargetable(AActor* Instigator, AActor* Target,
								  float DurationSeconds);

	/** Whether this actor cannot be hit right now. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill Effects")
	static bool IsUntargetable(const AActor* Actor);

	/** Whether this actor is on the floor right now and may not act. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill Effects")
	static bool IsKnockedDown(const AActor* Actor);

	/**
	 * Whether this actor is under a hard stop and may not act at all.
	 *
	 * ONE QUESTION RATHER THAN TWO, AND THAT IS THE POINT. Section VI of the
	 * design document puts Stun and Knockdown in one row of its table -- both
	 * "completely stop the target operating any part of its character" -- and
	 * everything that has to refuse to drive a character wants that row and not
	 * either half of it. Three places ask: the enemy brain, the player
	 * controller and the basic attack. Asking `IsStunned` alone at any of them
	 * would let a knocked-down creature keep fighting from the floor.
	 *
	 * A PIN IS DELIBERATELY NOT INCLUDED. It stops movement and nothing else;
	 * see `ApplyPin`.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill Effects")
	static bool CannotAct(const AActor* Actor);

	/**
	 * How wide the cone behind a creature is, in degrees, for a blow to count
	 * as landed from behind.
	 *
	 * NINETY, MEASURED SYMMETRICALLY ABOUT THE CREATURE'S BACK, so an attacker
	 * must be within 45 degrees of directly behind it. That is the rear quadrant
	 * Final Fantasy XIV uses, which divides a creature's hitbox into a front, two
	 * flanks and a rear off its facing.
	 *
	 * THE ALTERNATIVE THE GENRE ALSO OFFERS IS TWICE AS WIDE. Guild Wars 2
	 * defines "behind" as the 180 degree cone directly behind a target, and
	 * "flanking" as everything outside the front 90. Divinity: Original Sin 2
	 * draws a rear arc segment on the targeting circle and allows a backstab only
	 * from inside it, and only with a dagger.
	 *
	 * NINETY WAS CHOSEN BECAUSE IT IS THE STRICTER OF THE TWO and this is the
	 * Dagger's identity rather than a bonus every weapon gets. It is also the
	 * vocabulary the Weapon Skills sheet already uses: `Angle` is a cone in
	 * degrees, and Emberpierce's own is 60.
	 *
	 * IT IS A TUNING NUMBER AND SHOULD MOVE AGAINST REAL PLAY. Enemies turn to
	 * face whoever is hitting them, so how often a player can be inside 45
	 * degrees of a creature's back is a question about the enemy brain's turning
	 * speed and cannot be settled by reading. One constant, so moving it moves
	 * every skill that asks.
	 */
	static constexpr float RearArcDegrees = 90.0f;

	/**
	 * Whether the attacker is standing behind the target.
	 *
	 * THREE DAGGER ROWS ASK. Slipstream returns the movement cooldown for "every
	 * enemy you strike from behind"; Emberpierce "deals 40% more damage from
	 * behind"; and Everywhere at Once strikes "each from behind as you arrive",
	 * which its `RearHits=1` states outright rather than leaving to geometry.
	 *
	 * MEASURED FROM THE TARGET'S FACING AND NOT FROM THE ATTACKER'S. What makes
	 * a blow a blow in the back is that the creature taking it was looking the
	 * other way. Where the attacker happens to be facing does not enter into it,
	 * and asking about it would mean a character walking backwards out of a fight
	 * could not be struck from behind at all.
	 *
	 * FLATTENED TO THE GROUND PLANE. A creature standing on a ledge above another
	 * is behind it or not for the same reason one standing level with it is, and
	 * leaving the height in would make the same ground position answer
	 * differently for a tall creature and a short one.
	 *
	 * FALSE WHEN THE TWO ARE IN THE SAME PLACE, because there is no direction
	 * between them to judge, and false for anything with no facing at all.
	 *
	 * @return whether the attacker is inside `RearArcDegrees` about the target's
	 *         back
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill Effects")
	static bool IsBehind(const AActor* Attacker, const AActor* Target);

	/**
	 * Whether this actor's health has reached zero.
	 *
	 * A TAG RATHER THAN A FLAG, for the reason a stun is one: the two controllers
	 * that have to refuse to drive a dead character already ask about state this
	 * way, and anything else that needs to know can ask without holding a pointer
	 * to the character's own class.
	 *
	 * IT IS NOT TIMED, unlike every other state tag here. `ApplyTagForDuration`
	 * grants a tag that expires; this one is added loosely and comes off only
	 * when something takes it off deliberately. Issue #517.
	 *
	 * NOTHING TAKES IT OFF AN ENEMY, which is what it was written for: an enemy
	 * is destroyed on the next tick, so there is nobody left to clear it from.
	 * A PLAYER IS DIFFERENT AND THAT IS WHY `ClearDead` EXISTS. A player is not
	 * destroyed -- the design says ordinary death continues the run and never
	 * costs the character -- so a player comes back, and coming back means this
	 * tag comes off. Issue #570.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill Effects")
	static bool IsDead(const AActor* Actor);

	/** Mark this actor dead. Does nothing to one already marked. */
	static bool MarkDead(AActor* Actor);

	/**
	 * Take the dead mark off again. Does nothing to one that is not marked.
	 *
	 * THE RETURN VALUE IS THE POINT, the same way it is on `MarkDead`. Both
	 * answer "did this call change anything", which is what lets a caller run
	 * once rather than every time it is asked. Reviving something that was not
	 * dead is a caller getting its own state wrong, not a thing to do quietly.
	 */
	static bool ClearDead(AActor* Actor);

	/** The State.Dead tag, or an invalid tag if the vocabulary lacks it. */
	static FGameplayTag DeadTag();

	/** The State.Stunned tag, or an invalid tag if the vocabulary lacks it. */
	static FGameplayTag StunnedTag();

	/** The State.StunImmune tag, or an invalid tag if the vocabulary lacks it. */
	static FGameplayTag StunImmuneTag();

	/** The State.KnockedDown tag, or an invalid tag if the vocabulary lacks it. */
	static FGameplayTag KnockedDownTag();

	/** The State.Pinned tag, or an invalid tag if the vocabulary lacks it. */
	static FGameplayTag PinnedTag();

private:
	/** Where the imported status effect table lives. */
	static const TCHAR* StatusEffectTableAssetPath;

	static const UDataTable* LoadStatusEffectTable();

	/**
	 * Apply a damage-carrying effect with the attacker's damage type on it.
	 *
	 * Every path that damages anything goes through here, so there is one place
	 * a hit's properties are attached and one place they can be forgotten.
	 */
	static void ApplyTypedSpec(UGameplayEffect* Effect,
							   const FGameplayEffectContextHandle& Context,
							   UAbilitySystemComponent* Defender,
							   const AActor* Attacker,
							   const FCataclysmHitDelivery& Delivery);
};
