// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "CataclysmSkillTemplate.generated.h"

class ACataclysmGroundZone;

/**
 * What every shape template shares: its designed identity and its numbers.
 *
 * ONE CLASS PER SHAPE, NOT ONE PER SKILL. Issue #37 asks for this directly:
 * "Build these 18 abilities from shared templates, not as one-off
 * implementations. The full matrix is 558 rows. If the first 18 each need
 * bespoke work, the remaining 540 are unaffordable." The matrix is 398 rows
 * after issue #23 cut it, and the point stands.
 *
 * A SKILL IS A ROW, NOT A CLASS. The name, the description, the shape and the
 * shape's numbers all arrive from game/Data/WeaponSkills.csv when the weapon is
 * equipped, stamped onto the granted instance by
 * UCataclysmWeaponSlotsComponent. So adding a skill of an existing shape is a
 * workbook edit and needs no C++ at all, which is the acceptance criterion the
 * issue names second.
 */
UCLASS(Abstract)
class CATACLYSM_API UCataclysmSkillTemplate : public UCataclysmGameplayAbility
{
	GENERATED_BODY()

public:
	UCataclysmSkillTemplate();

	/** The designed skill's name, from the weapon skill matrix. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	FString SkillName;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	FString SkillDescription;

	/** The designed name, for anything showing the player their own abilities. */
	virtual FString DisplayedName() const override { return SkillName; }

	/** This skill's numbers, parsed from its Shape Params cell. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	FCataclysmSkillShapeParams Params;

	/**
	 * This skill's own tags, from its Tags cell.
	 *
	 * WHICH OF THE CHARACTER'S MODIFIERS REACH THIS SKILL. Every hit is
	 * evaluated against them: a stat modifier requiring Element.Demonic applies
	 * to a skill carrying that tag and to no other. Empty means only modifiers
	 * that require nothing, and those tagged Scope.Global, apply.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	FGameplayTagContainer SkillTags;

	/**
	 * This skill's radius after the caster's area of effect. Issue #895.
	 *
	 * NOTHING READ THE AreaOfEffect ATTRIBUTE AT ALL until that issue. It
	 * existed, was clamped, was replicated, was given 100 by the shared Default
	 * class line, and every skill in the game used the radius its data stated.
	 *
	 * ANYTHING CARRYING AN AREA TAG, which is the project owner's rule of
	 * 2026-08-24. `Type.AOE` is the parent of `Type.AOE.PointBlank`,
	 * `Type.AOE.Aura` and `Type.AOE.Persistent`, and a tag query against a
	 * parent matches every child, so one check covers all three: 63 of the skill
	 * rows carry one.
	 *
	 * A RADIUS ON A SKILL WITH NO AREA TAG IS LEFT ALONE. It is a reach or a
	 * projectile's body, and enlarging those would make a sword swing longer and
	 * a bolt fatter, which is not what "increased area of effect" means to a
	 * player.
	 *
	 * ADDITIVE RATHER THAN MULTIPLICATIVE, and the design says why: "Area of
	 * effect at +2% per point stays additive, because a larger radius has no
	 * runaway." Its baseline is 100, meaning unchanged.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill")
	float ScaledRadiusCm() const;

	/**
	 * The radius of the ground this skill leaves, after area of effect.
	 *
	 * ALWAYS SCALED, unlike the radius above, because a zone's damage IS area
	 * damage whatever the skill that left it was. The design: "A zone's own
	 * damage is area damage, decided where the zone deals it."
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill")
	float ScaledGroundRadiusCm() const;

	/**
	 * The caster's area of effect as a plain multiplier, or 1.0 for a caster
	 * with none.
	 */
	float AreaOfEffectMultiplier() const;

	/**
	 * This skill's own base critical strike chance, or -1 to take the default.
	 *
	 * ON THE GRANTED INSTANCE AND NOT ON THE CLASS, for the same reason
	 * `SkillTags` and `Params` are: one class stands for every skill of that
	 * shape, so two characters holding different Projectile skills share
	 * `UCataclysmProjectileSkill` and differ only in what is stamped here.
	 *
	 * WHY IT IS NOT WRITTEN ONTO THE CHARACTER. A character holds six skills at
	 * once and the ability system has one `CritChance` attribute, so writing this
	 * onto the character would mean the last skill granted decided the chance for
	 * all six. Instead each hit carries the chance of the skill that dealt it, as
	 * a set-by-caller magnitude on the damage effect, and the character's
	 * attribute holds the default for every skill that states nothing. Issue
	 * #657.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	float CritChancePercent = -1.0f;

	/**
	 * What this skill cost the last time it was used, as a percentage of the
	 * character's maximum health. Issue #983.
	 *
	 * WRITTEN BY `PayHealthCost` AND READ WHEN A BLOW IS DEALT. The Masochist's
	 * Grand Tithe node asks about "a skill whose health cost is above 10% of
	 * your maximum health", and nothing anywhere recorded what a skill had cost.
	 *
	 * WHAT WAS REALLY CHARGED, NOT WHAT THE SKILL ROW STATES. The total is the
	 * skill's own share of CURRENT health plus the character's added share of
	 * MAXIMUM health, so it depends on where the character's health stood at the
	 * moment of the cast and on how many points are in Deeper Cuts. Neither is
	 * knowable from the skill row alone.
	 *
	 * MEASURED AGAINST MAXIMUM HEALTH, because that is what the node asks about,
	 * even though half the total was a share of current health.
	 *
	 * IT OUTLIVES THE CAST THAT WROTE IT, because an ability is instanced per
	 * actor. That is correct rather than a leak: it belongs to this skill, and
	 * every blow this skill deals should read it, including one from a
	 * projectile that lands seconds later. `PayHealthCost` writes it on every
	 * use rather than only on a use that charged something, so a skill that cost
	 * nothing records a real zero instead of keeping the last cast's figure.
	 *
	 * -1 MEANS THE SKILL HAS NOT BEEN USED YET, or that maximum health could not
	 * be read. Zero means it was used and cost nothing, which is every skill in
	 * the game except Blood Pyre for a character with no point in Deeper Cuts.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	float LastHealthCostPercentOfMaximum = -1.0f;

	/** Which shape this class implements. Every subclass answers. */
	virtual ECataclysmSkillShape Shape() const PURE_VIRTUAL(UCataclysmSkillTemplate::Shape, return ECataclysmSkillShape::None;);

	/**
	 * Percent of weapon damage one use deals.
	 *
	 * THE SKILL'S OWN FIGURE, AND ITS SLOT'S ONLY WHEN IT STATES NONE. It
	 * was called GetSlotDamagePercent until 2026-08-22 and answered the
	 * slot's alone, which was true while a skill could only sit in the slot
	 * it was designed for. A slot is now a key and any skill may go in any
	 * slot, so the name would have become a lie. Issue #836.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill")
	float GetDamagePercent() const;

	/**
	 * This skill's damage type, as its `Element.*` tag, or an invalid tag.
	 *
	 * Every row of the Weapon Skills sheet carries exactly one, because the
	 * sheet is a matrix of weapon type against damage type and the damage type
	 * is one of its two axes. It is what a self buff scopes its increase to, so
	 * that "increased fire damage" is expressed as the tag the data already
	 * carries rather than as a name written in C++.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill")
	FGameplayTag ElementTag() const;

	/**
	 * The stat saying this character's skills cost no health at all.
	 * Issue #1051. Zero for no.
	 *
	 * A FLAG AND NOT A REDUCTION, because
	 * `UCataclysmStatPipeline::LessMultiplierFloor` clamps a Less multiplier
	 * to -99 on purpose and ninety-nine per cent less is not none.
	 */
	static const TCHAR* HealthCostSuppressedStat;

	/**
	 * Whether this character's skills cost no health right now.
	 * Issue #1051.
	 *
	 * ASKED FOR RATHER THAN READ, because The Last Drop's row carries a
	 * health condition and a conditional bonus is never folded into a
	 * gameplay attribute. The answer therefore differs between two calls a
	 * moment apart, which is the whole point: the option applies while the
	 * character is below a fifth of its health and not otherwise.
	 *
	 * False for every character without that capstone option, and false for
	 * any ability system with no class resource attribute set, which is every
	 * enemy.
	 */
	static bool HealthCostIsSuppressed(
		const UAbilitySystemComponent* AbilitySystem);

	/**
	 * The stat saying this character has traded its mana pool for health.
	 * As `game/Data/PassiveEffects.csv` spells it. Issue #1067.
	 */
	static const TCHAR* ManaPoolBecomesHealthStat;

	/**
	 * Whether this character pays with health where others pay with mana.
	 *
	 * THE MASOCHIST'S Water to Blood, the first option of its first capstone:
	 * "You no longer have a mana pool. All maximum mana is converted into added
	 * maximum health, and every ability costs health instead of mana."
	 *
	 * IT LIVES BESIDE `HealthCostIsSuppressed` BECAUSE BOTH ANSWER "WHAT DOES A
	 * SKILL COST THIS CHARACTER", and a reader looking for one will look here
	 * for the other. They are not opposites: that one says a health cost is not
	 * taken, and this one says a MANA cost is taken out of health instead.
	 *
	 * READ IN TWO PLACES AND FOR TWO DIFFERENT REASONS.
	 * `UCataclysmPlayerClassStats::ApplyTo` reads it once, when the stat line is
	 * written, to move the resolved maximum mana onto maximum health and write
	 * the mana maximum down to zero. `UCataclysmGameplayAbility::CheckCost` and
	 * `ApplyCost` read it on every activation, to take the cost out of the right
	 * pool.
	 *
	 * False for every character without that option, and false for any ability
	 * system with no class resource attribute set, which is every enemy.
	 */
	static bool ManaPoolBecomesHealth(
		const UAbilitySystemComponent* AbilitySystem);

protected:
	/**
	 * Spend the cost, start the cooldown, and say whether the skill may run.
	 *
	 * EVERY TEMPLATE CALLS THIS FIRST AND NOTHING CALLED IT BEFORE. Issue #155
	 * built CheckCost, ApplyCost, CheckCooldown and ApplyCooldown on
	 * UCataclysmGameplayAbility, and the engine only invokes the two Apply
	 * halves from CommitAbility -- which no ability in the project called. So a
	 * skill's mana was checked and never spent and its cooldown was checked and
	 * never started, and every ability could be used continuously. The only
	 * ability that existed was the placeholder that does nothing, so nothing
	 * showed it. Cataclysm.Skills.UsingASkillSpendsManaAndStartsItsCooldown now
	 * fails if this is removed.
	 */
	bool CommitAndBegin(const FGameplayAbilitySpecHandle Handle,
						const FGameplayAbilityActorInfo* ActorInfo,
						const FGameplayAbilityActivationInfo ActivationInfo);

	/** The actor doing the hitting: the avatar, not the player state. */
	AActor* Avatar() const;

	/** Where the player is pointing, or the avatar's location if unknown. */
	FVector AimPoint() const;

	/**
	 * The direction the skill is aimed in, on the ground plane.
	 *
	 * FALLS BACK TO THE CASTER'S FACING RATHER THAN TO NOTHING, and that matters
	 * for more than tidiness. Anything with no cursor -- an enemy, a summoned
	 * minion, an automation test -- aims at its own feet through AimPoint, and a
	 * projectile aimed at its own feet has a flight path of zero length and hits
	 * nobody. It fires forward instead.
	 */
	FVector AimDirection() const;

	/**
	 * A point RangeCm away in the aimed direction, or nearer if the aim is nearer.
	 *
	 * Where a projectile lands, where a blink arrives, where a rift is torn.
	 */
	FVector AimedPointWithin(float RangeCm) const;

	/**
	 * Deal one hit to each target and set them alight if the skill says so.
	 *
	 * @param DamagePercent  negative takes the slot's figure
	 * @return how much damage was sent, summed, before mitigation
	 *
	 * WHETHER THIS IS AREA DAMAGE IS NOT AN ARGUMENT, and it was briefly. The
	 * skill's own tags already say -- `Type.AOE.PointBlank` and `Type.AOE.Aura`
	 * are on 37 designed skills -- and `SkillTags` is passed through to
	 * `UCataclysmSkillEffects::ApplyHit`, which reads them. Deciding it here from
	 * the shape instead treated every Strike as area damage, which would have
	 * made Cinderslash, one sword blow tagged `Type.Strike, Type.Melee`,
	 * impossible to evade. Issue #513.
	 */
	float HitTargets(const TArray<AActor*>& Targets, float DamagePercent = -1.0f);

	/**
	 * Returns this slot's mana on hit to the caster.
	 *
	 * CALLED BY HitTargets ONLY WHEN THE SKILL DEALT SOMETHING, which is what
	 * the design means by "each time it lands". Inert for every slot but the
	 * basic attack, because that is the only row in SkillSlots.csv with a
	 * mana-on-hit figure.
	 */
	void ApplyManaOnHit() const;

	/**
	 * Push one target away from the caster by this skill's `Knockback`, if it
	 * states one. Does nothing when it does not.
	 *
	 * CALLED FROM HitTargets, WHICH IS WHAT MAKES KNOCKBACK A RIDER. It used to
	 * be written inline in UCataclysmStrikeSkill::SwingOnce, so only a Strike
	 * could shove. Issue #626 moved it here, because displacement is not
	 * specific to one kind of skill: a strike, a leap, a charge and an enemy
	 * slam can all do it, and Shockwave Leap knocked back in its prose while its
	 * Movement shape had no way to say so.
	 *
	 * A REPEATING SHAPE SHOVES ON EVERY TICK. An Aura pulses through HitTargets
	 * once per Interval, so an Aura stating a Knockback would push on each pulse.
	 * No designed skill states one, and what would bound it is the design's own
	 * limit on repeated displacement -- each one inside 5 seconds moves half as
	 * far as the one before, decided on issue #302. That rule is stated in
	 * docs/Cataclysm_GDD_v2.md and implemented nowhere, for either direction.
	 * Issue #628 carries it.
	 */
	void ApplyKnockbackTo(AActor* Self, AActor* Target) const;

	/**
	 * Leave a burning patch of ground, if this skill's numbers say to.
	 *
	 * Eight of the sixteen designed Demonic skills do, on top of whatever else
	 * they are, which is why this is here and not a shape of its own.
	 */
	ACataclysmGroundZone* LeaveGroundAt(const FVector& Location);

	/**
	 * Leave burning ground along a path rather than at a point.
	 *
	 * For the skills whose text says the path itself burns. Emberhurl leaves
	 * "its flight path burning for 4 seconds"; Cinder Rush "leaves a trail of
	 * fire behind you". Before this both left one patch at the far end, so an
	 * enemy standing halfway along stood on ground that was not burning.
	 * Issue #167.
	 *
	 * The skill's GroundRadius becomes the half-width of the path.
	 */
	ACataclysmGroundZone* LeaveGroundAlong(const FVector& Start, const FVector& End);

	/**
	 * Take the health this cast costs, and generate Fervour from it.
	 *
	 * TWO COSTS, MEASURED AGAINST DIFFERENT THINGS, AND THAT IS DELIBERATE.
	 * Issue #970.
	 *
	 *   the SKILL's own `HealthCostPercent` is a share of CURRENT health, which
	 *   is what makes it self-limiting: each cast costs less than the last, so
	 *   it cannot kill. Only Blood Pyre states one.
	 *
	 *   the CHARACTER's `added_health_cost` is a share of MAXIMUM health, which
	 *   means it CAN kill. The Masochist's Deeper Cuts node is its only source,
	 *   and `docs/DECISIONS.md` records the project owner drawing exactly that
	 *   distinction between the two shapes.
	 *
	 * IT RUNS FOR EVERY SKILL, not only for one that states a cost of its own,
	 * because the character's added cost applies to all of them.
	 *
	 * AND IT MAY CHARGE NOTHING AT ALL. Issue #1051. The Masochist's The Last
	 * Drop reads "While below 20% health your skills cost no health", and
	 * `HealthCostSuppressedStat` below is the flag that says so. It suppresses
	 * the WHOLE cost, both halves above, so nothing is taken, nothing is
	 * deferred, and no Fervour is generated from a cost that was not paid.
	 *
	 * IT ALSO GRANTS FERVOUR FOR THE CAST ITSELF, which is that option's other
	 * clause and is why this function does something for a skill that costs
	 * nothing.
	 */
	void PayHealthCost();

	/**
	 * What this character adds to every skill's health cost, as a percentage of
	 * maximum health.
	 *
	 * Zero for a character with no point in Deeper Cuts, and zero for any
	 * ability system without the class resource attribute set -- which is every
	 * enemy, and an enemy using a skill goes through the same function.
	 * Issue #970.
	 */
	static float AddedHealthCostPercent(
		const UAbilitySystemComponent* AbilitySystem);

	/**
	 * What this character adds to every skill's health cost, as a percentage of
	 * CURRENT health. Issue #986.
	 *
	 * A SEPARATE FIGURE FROM THE ONE ABOVE BECAUSE IT IS MEASURED AGAINST A
	 * DIFFERENT THING, and the design draws that line itself: a share of current
	 * health cannot kill, because each cast costs less than the last, while a
	 * share of maximum health can. The Masochist's Exsanguinate keystone is this
	 * one's only source, at 15%.
	 *
	 * Zero for a character without that keystone, and zero for any ability
	 * system with no class resource attribute set, which is every enemy.
	 */
	static float AddedHealthCostOfCurrentPercent(
		const UAbilitySystemComponent* AbilitySystem);

	/**
	 * The least health a cost taken from CURRENT health may leave behind.
	 *
	 * THE DESIGN STATES IT OUTRIGHT. Exsanguinate: "A cost taken from current
	 * health cannot reduce it below 1." Issue #986.
	 *
	 * IT IS NEARLY TRUE OF THE ARITHMETIC ALREADY, and the floor is here anyway.
	 * A share of current health approaches zero without reaching it, so no
	 * number of casts empties the bar in exact arithmetic. A float does reach
	 * zero, and a rule that holds in algebra and fails in single precision is
	 * not a rule.
	 *
	 * IT DOES NOT APPLY TO THE SHARE TAKEN FROM MAXIMUM HEALTH, which the design
	 * allows to kill. See `AddedHealthCostPercent` above.
	 */
	static constexpr float LeastHealthAfterCurrentHealthCost = 1.0f;
};
