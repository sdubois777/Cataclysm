// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "CataclysmSkillTemplate.generated.h"

class ACataclysmGroundZone;
class ACataclysmTerrain;

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

	/**
	 * Ends the skill, and drops a blow that was waiting for the swing.
	 *
	 * PUBLIC BECAUSE THE BASE IS. `UGameplayAbility::EndAbility` is public, and
	 * an override in a protected section narrows it, which stops every existing
	 * caller compiling.
	 *
	 * IT ONLY DROPS THE BLOW WHEN THE SKILL WAS CANCELLED. Issue #1133 requires
	 * that moving damage later must not let a cancelled skill still deal it, and
	 * this is where that holds. An ordinary end happens from inside the blow
	 * itself, after it has landed, so clearing the wait then would be clearing a
	 * timer that has already fired.
	 */
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
							const FGameplayAbilityActorInfo* ActorInfo,
							const FGameplayAbilityActivationInfo ActivationInfo,
							bool bReplicateEndAbility,
							bool bWasCancelled) override;

	/**
	 * Refuses the skill when a condition its `Requires` names does not hold.
	 *
	 * HERE AND NOT IN `ActivateAbility`, so a refused skill costs nothing. The
	 * engine calls this before the cost and the cooldown are checked, and the
	 * skill bar calls it to decide whether a button is usable, so a Flashpoint
	 * with nothing alight nearby reads as unavailable rather than as spending
	 * mana on nothing.
	 *
	 * PUBLIC BECAUSE THE BASE IS, exactly as for `EndAbility` above.
	 */
	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	/**
	 * Tell this character's running skills that it killed something.
	 *
	 * ONE SKILL LISTENS TODAY: the Axe's Butcher's Heat, "every enemy you kill
	 * while it lasts grants 1% more damage and adds another second to the heat".
	 * A buff whose `ScalingSource` is not `Kill` ignores it.
	 *
	 * CALLED FROM `ACataclysmEnemyCharacter::HandleDeath`, beside the three
	 * things a kill already does there -- granting experience, clearing a health
	 * debt and building a Carnage stack. That file's comment already calls
	 * itself "the one place a kill is known about at all", and this is a fourth
	 * reader of the same event rather than a second source of it.
	 *
	 * IT CREDITS WHOEVER THAT FILE CREDITS, which is the first player
	 * controller's pawn. That is a simplification already in place for the other
	 * three and is not made worse here.
	 *
	 * @return how many running self buffs were told, whether or not each one
	 *         counts kills
	 */
	static int32 NoteKill(AActor* Killer);

	/**
	 * How much further a self buff this character is holding makes its blows
	 * reach, as a percentage.
	 *
	 * THE WHIP'S COIL OF EMBERS AND NOTHING ELSE TODAY: "wind burning coils about
	 * yourself for 10 seconds. Your attack range is increased by 30%." Its
	 * `RangeIncrease=30` was parsed and read by nothing at all until 2026-09-02.
	 *
	 * READ FROM THE RUNNING ABILITY, exactly as `HeldConsumeSpreadRadiusCm`
	 * above is, and for the same reason: a buff that lasts is an active ability
	 * while it lasts, so nothing has to be written onto the character and
	 * nothing has to be cleared when it ends.
	 *
	 * NOT AN ATTRIBUTE, WHICH IS THE OTHER PLACE IT COULD HAVE GONE. An
	 * attribute would let gear and passive nodes grant attack range too, and
	 * nothing in the game wants to today: no affix, no node and no enemy modifier
	 * mentions reach. Adding one now would be a stat with a single writer and
	 * three test files to keep in step with it.
	 *
	 * THE LARGEST RATHER THAN THE SUM, because the design gives every
	 * player-applied effect a single stack, so two copies of one buff are one
	 * buff.
	 *
	 * Zero for a character holding no such buff, which is the ordinary case.
	 */
	static float HeldRangeIncreasePercent(const AActor* Self);

	/**
	 * Tell this character's running skills that one of its blows landed, and
	 * where.
	 *
	 * TWO SKILLS LISTEN. The Warhammer's Groundbreaker, "for 10 seconds every
	 * blow you land cracks the ground beneath what it hits, leaving a fissure
	 * that knocks down the next enemy to cross it"; and the Dagger's Slipstream,
	 * "for 8 seconds every enemy you strike from behind returns your movement
	 * skill to you at once. Blows landed from the front do nothing for it." A
	 * buff that states neither `Terrain` nor `Requires=RearHit` ignores it.
	 *
	 * CALLED FROM `HitTargets`, WHICH IS WHERE EVERY BLOW IN THE GAME IS DEALT,
	 * so a fissure opens under a strike, a projectile, an aura pulse or a leap
	 * alike -- which is what "every blow you land" says. It is the same argument
	 * that put the knockback and the forced movement riders there.
	 *
	 * ONLY FOR A BLOW THAT ACTUALLY DEALT DAMAGE. "Every blow you land" is not
	 * every swing: one that was evaded, or that armour and resistance stopped
	 * completely, did not land. That is the same test the mana-on-hit rider makes
	 * and the opposite of the knockback rider, which pushes whether or not it
	 * hurt.
	 *
	 * @param Where  the position of what was hit, which is where the ground
	 *               cracks. "Beneath what it hits", not beneath the attacker
	 * @param bFromBehind  whether the attacker was inside the cone behind what
	 *               it struck. Decided by the caller rather than here, because
	 *               `RearHits=1` lets a row declare it outright and only the
	 *               skill knows whether its own row says so
	 * @return how many running self buffs were told, whether or not each one
	 *         does anything with it
	 */
	static int32 NoteBlowLanded(AActor* Attacker, const FVector& Where,
								bool bFromBehind = false);

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

	/**
	 * Run `Blow` at the moment the swing started by CommitAndBegin connects.
	 *
	 * WHY. Issue #1133. Damage, the swing arc and every other effect used to
	 * fire in the frame the ability activated, while the animation played for
	 * one to nearly two seconds beside it, so an enemy was hurt while the weapon
	 * was still going backwards.
	 *
	 * CALL IT STRAIGHT AFTER CommitAndBegin RETURNS TRUE. It reads the figure
	 * that `PlayAttackAnimation` just worked out and left on the character, and
	 * nothing else refreshes that figure.
	 *
	 * IT RUNS `Blow` IMMEDIATELY WHEN THERE IS NOTHING TO WAIT FOR, and that is
	 * the case that keeps the game working rather than an edge case. A character
	 * that played no animation reports zero. That covers every enemy, which
	 * animates from its own class; a checkout with no animation assets; and
	 * every automation test, whose world is never ticked and so could never
	 * reach a later moment at all. In all of those the behaviour is exactly what
	 * it was before issue #1133.
	 *
	 * WHATEVER ENDS THE ABILITY BELONGS INSIDE `Blow`, NOT AFTER THIS CALL.
	 * Calling EndAbility on the next line cancels the wait and the blow is never
	 * struck. `UCataclysmStrikeSkill::ActivateAbility` shows the shape.
	 *
	 * A CANCELLED SKILL STRIKES NOTHING. EndAbility with bWasCancelled clears
	 * the wait, which is what stops a skill that was interrupted from still
	 * dealing its damage a second later.
	 */
	void WhenTheSwingConnects(TFunction<void()> Blow);

	/**
	 * How long until the swing connects, from the character that is swinging.
	 *
	 * Zero when the avatar is not a Cataclysm character, or played no animation.
	 * Separated from WhenTheSwingConnects so a test can ask without waiting.
	 */
	float SecondsUntilTheSwingConnects() const;

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
	 * No designed skill states one, and what bounds it is the design's own limit
	 * on repeated displacement -- each one inside 5 seconds moves half as far as
	 * the one before, decided on issue #302 and implemented on #628 inside
	 * `UCataclysmSkillEffects::ApplyKnockback`. That rule now covers a pull, a
	 * drag and a launch as well, because all four share one displacement body.
	 */
	void ApplyKnockbackTo(AActor* Self, AActor* Target) const;

	/**
	 * Do to one target whatever this skill's `ForcedMovement` names, if it names
	 * anything. Does nothing when it does not.
	 *
	 * A RIDER BESIDE THE KNOCKBACK ABOVE AND FOR THE SAME REASON. Nine rows
	 * across three weapons state one of the five verbs, and they are spread over
	 * three different shapes: the Spear's Impale is a Strike, its Nail Down a
	 * Movement and its Skewer a Projectile. Anything written into one template
	 * would have to be written into the other two.
	 *
	 * MORE THAN ONE VERB MAY BE NAMED, comma separated, and the Whip's The
	 * Gathering is the row that needs it: `ForcedMovement=Pull, Knockdown` hauls
	 * its catch to the caster's feet and then puts it on the floor. They are
	 * applied in the order written here -- displacements first, then holds --
	 * so a target is moved before it is held rather than being pinned where it
	 * stood and then dragged out of its own pin.
	 *
	 * DRAG AND PULL ARE ONE DISPLACEMENT APPLIED AT DIFFERENT MOMENTS, which is
	 * worth stating because the sheet gives them two names. Both haul a target
	 * toward the caster; the difference is that a Movement shape has already
	 * moved the caster by the time this runs, so `Drag` on the Whip's Reel
	 * carries its catch along the whole run and dumps it at the destination,
	 * while `Pull` on The Gathering hauls to a caster that never moved.
	 *
	 * A KNOCKDOWN FROM A ROW THAT STATES ONE IS ALWAYS A DESIGNED KNOCKDOWN, so
	 * it skips the damage threshold. Every row here means to knock down; the
	 * threshold exists to stop small incidental hits interrupting, and there is
	 * nothing incidental about a parameter.
	 *
	 * @param DamageDealt  what this blow actually did, after mitigation, which
	 *                     only an undesigned knockdown would weigh
	 * @return whether the target was pinned, which is what `OnDeath=Release`
	 *         needs to know to bind a line together afterwards
	 */
	bool ApplyForcedMovementTo(AActor* Self, AActor* Target,
							   float DamageDealt) const;

	/**
	 * Whether this skill's `ForcedMovement` names one particular verb.
	 *
	 * The same shape as `RequiresCondition` above, over a different comma
	 * separated column. Kept separate rather than made generic over both,
	 * because the two are read at completely different moments -- one gates a
	 * cast and one rides a blow -- and a shared helper would be a parameter
	 * saying which column, which is longer than the four lines it saves.
	 */
	bool ForcedMovementNames(const TCHAR* Verb) const;

	/**
	 * Whether every condition this skill's `Requires` names holds right now.
	 *
	 * CHECKED BEFORE ANYTHING IS SPENT, from `CanActivateAbility`, so a skill
	 * whose condition fails costs no mana and starts no cooldown. Six Demonic
	 * skills name a condition and nothing read the column, so Flashpoint could
	 * be darted into empty air and Touch Off used with nothing alight.
	 *
	 * THE FOUR CONDITIONS, as `REQUIREMENTS` in `tools/generate_datatables.py`
	 * closes the list:
	 *
	 *   `Burning`     an enemy carrying the burn tag is within reach
	 *   `Target`      any enemy at all is within reach
	 *   `Stationary`  the caster is not moving
	 *   `RearHit`     see below -- not an activation condition
	 *
	 * `RearHit` IS NOT A GATE AND THIS ANSWERS TRUE FOR IT. The Dagger's
	 * Slipstream reads "every enemy you strike from behind returns your movement
	 * skill to you", which is a condition on what the buff reacts to while it
	 * runs, not on whether it may be cast. Treating it as a gate would make a
	 * support skill uncastable until the player was already behind something.
	 *
	 * REACH IS `RangeCm` WHEN THE SKILL STATES ONE AND `RadiusCm` OTHERWISE,
	 * because a skill that reaches out states a range and one that hits around
	 * itself states a radius. Asking about the wrong one would let Touch Off be
	 * used on an enemy fourteen metres away that its eight metre ring cannot
	 * touch.
	 *
	 * @param ActorInfo  whose conditions to judge, or null for this instance's
	 *                   own. `CanActivateAbility` is handed one and must use it:
	 *                   the engine calls that before an ability is active, when
	 *                   the instance's own cached information may not be set.
	 */
	bool RequirementsAreMet(
		const FGameplayAbilityActorInfo* ActorInfo = nullptr) const;

	/**
	 * Whether this skill's `Requires` names one particular condition.
	 *
	 * Separated so a shape can act on a condition as well as gate on it. The
	 * Movement template asks about `Burning` to decide WHERE it arrives, which
	 * is Flashpoint's "only something already alight can be reached".
	 */
	bool RequiresCondition(const TCHAR* Condition) const;

	/**
	 * The reach a `Requires` condition is judged over, in centimetres.
	 *
	 * Shared by the gate and by the Movement template, so the enemy a skill was
	 * allowed to activate for is the same one it then travels to.
	 */
	float RequirementReachCm() const;

	/**
	 * Whether this creature's health has run out.
	 *
	 * ASKED BESIDE `UCataclysmSkillEffects::IsDead` RATHER THAN INSTEAD OF
	 * IT. That one reads a tag written by a character's own death path, and
	 * whether that path has run by the time a blow returns depends on the
	 * character class. A creature at no health has been killed either way,
	 * and the Axe's Emberhaul asks whether its arrival killed.
	 *
	 * False for anything with no health attribute at all, which is every
	 * actor that is not a fighter.
	 */
	static bool AtNoHealth(const AActor* Actor);

	/**
	 * Return a cooldown, if this skill's `RefundsCooldown` names one.
	 *
	 * TWO SKILLS ASK FOR IT and they ask for different cooldowns. The Axe's
	 * Emberhaul writes `Self`: "if the arrival kills them, the axe comes back
	 * ready to throw again". The Dagger's Slipstream writes `Movement`: "every
	 * enemy you strike from behind returns your movement skill to you at once".
	 * `REFUND_TARGETS` in `tools/generate_datatables.py` closes the list at
	 * those two.
	 *
	 * A COOLDOWN IS A DURATION EFFECT GRANTING THE SLOT'S TAG, which
	 * `UCataclysmGameplayAbility::ApplyCooldown` builds, so returning one is
	 * taking that effect off. It is per slot rather than per skill, so returning
	 * the Movement cooldown returns it for whatever skill is in that slot --
	 * which is what Slipstream's sentence describes.
	 *
	 * WHEN IT HAPPENS IS THE CALLER'S BUSINESS. This says what to return, not
	 * when: Emberhaul's condition is that its own arrival killed something, and
	 * only the Movement template knows that.
	 *
	 * @return whether a cooldown was actually taken off
	 */
	bool RefundCooldown();

	/**
	 * The status effects this skill's `Effect` cell names, as tags.
	 *
	 * A LIST RATHER THAN ONE NAME, because the Wand's Anathema writes
	 * `Effect=Shred, Madness` -- "laying every curse you know on it". Until
	 * 2026-09-01 the cell was read as a single name, so that row named an effect
	 * called "Shred, Madness" which no sheet has and which granted nothing.
	 *
	 * A NAME NO SHEET CARRIES IS SKIPPED rather than failing the skill, on the
	 * same reasoning `TagsFromCell` gives: `tools/generate_datatables.py` already
	 * refuses an unknown effect, so a name arriving here means the table was
	 * edited in the editor rather than generated.
	 */
	TArray<FGameplayTag> NamedEffectTags() const;

	/**
	 * How many seconds an applied effect lasts.
	 *
	 * `EffectDuration` WHEN THE SKILL STATES ONE, AND THE EFFECT'S OWN DESIGNED
	 * DURATION OTHERWISE. Not `Duration`, which is the skill's own length: the
	 * two were one key until the sheet split them on 2026-09-01, and every
	 * Debuff-shaped row in the game now states `EffectDuration` and no
	 * `Duration`. Reading the wrong one is why all three of them applied a curse
	 * for zero seconds, which `ApplyTagForDuration` refuses outright.
	 *
	 * THE FALLBACK IS THE SHEET AND NOT A NUMBER IN C++. Foul Wake states
	 * `Effect=Shred` and no duration at all, and the Status Effects sheet gives
	 * Shred six seconds, which is exactly what that row's own sentence says its
	 * ground does.
	 */
	float AppliedEffectSeconds(const FGameplayTag& EffectTag) const;

	/**
	 * Apply every effect this skill names to one target.
	 *
	 * @param DurationScale  multiplies the duration. The Debuff template passes
	 *                       two for a target already alight, which is Whisper of
	 *                       Madness's "lasts twice as long in a mind that is
	 *                       already burning".
	 * @return how many effects were applied
	 */
	int32 ApplyNamedEffectsTo(AActor* Target, float DurationScale = 1.0f);

	/**
	 * This skill's damage type as the name the resistance slots are keyed by,
	 * or none for a skill carrying no element tag.
	 *
	 * WHY IT IS NEEDED SEPARATELY FROM `ElementTag`. A Shred cuts the resistance
	 * matching the attacker's own type, and the resistance attributes are keyed
	 * by the plain name -- `Demonic` -- while a stat modifier scopes by the tag.
	 * `UCataclysmDamageCalculation` owns both and the conversion between them.
	 */
	FName DamageTypeName() const;

	/**
	 * Put out the fire on every one of these targets that carries it.
	 *
	 * THE SWORD'S WHOLE VERB. `docs/DECISIONS.md` gives each Demonic weapon one
	 * mechanical verb no other weapon may use, and the Sword's is consuming
	 * burn. All five of its skills state `ConsumeBurn` and nothing read it, so
	 * every one of them applied burn like any other weapon and put none out.
	 *
	 * IT RUNS BEFORE THE BLOW, and the order is required rather than tidy:
	 * Quench gives an enemy whose fire was just put out 50% more damage, and
	 * Extinction's damage rises with how many fires went out at once, so the
	 * blow cannot be sized until this has run.
	 *
	 * IT DOES NOT DAMAGE ANYTHING ITSELF. What consumption is worth is decided
	 * by the skill that consumed -- as a damage figure through
	 * `ScaledDamagePercent`, and as spreading fire through
	 * `IgniteAroundConsumed`.
	 *
	 * @return the targets whose fire was put out, in the order they were given
	 */
	TArray<AActor*> ConsumeBurnFrom(const TArray<AActor*>& Targets);

	/**
	 * Set alight everything standing near an enemy whose fire was just consumed.
	 *
	 * TWO SOURCES OF THE RADIUS AND THEY ARE ADDED RATHER THAN CHOSEN BETWEEN.
	 * The consuming skill's own `ConsumeRadius` is one -- Touch Off states three
	 * metres. The other is any self buff the caster is holding that states one,
	 * which is how the Sword's Ashen Edge works: "consuming the burn from an
	 * enemy also sets alight everything within 4 meters of them, so the fire you
	 * spend is never wholly lost". A skill with neither spreads nothing.
	 *
	 * IT SETS ALIGHT AND DEALS NO DAMAGE OF ITS OWN, which is the narrower of
	 * the two readings and what both descriptions say. Touch Off's "burst of
	 * damage" is its own blow, dealt by the strike to everything in its eight
	 * metre ring; this is the fire spreading outward from each one that went out.
	 *
	 * THE CONSUMED ENEMY IS NOT SET ALIGHT AGAIN BY ITS OWN SPREAD. Its fire was
	 * just spent, and relighting it here would make consuming it free.
	 *
	 * @return how many enemies were set alight
	 */
	int32 IgniteAroundConsumed(const TArray<AActor*>& Consumed);

	/**
	 * Set alight everything standing near one target, if that target already
	 * carries what this skill's `SpreadWhen` names.
	 *
	 * ONE ROW STATES IT: the Wand's Hex of Cinders, "hexing an enemy that is
	 * already burning also sets alight everything within 4 meters of them".
	 *
	 * THE OPPOSITE OF `IgniteAroundConsumed` ABOVE IN ONE RESPECT, AND THAT IS
	 * THE WHOLE DIFFERENCE. Consuming takes the fire OUT of the target and
	 * spends it, so the target is deliberately not relit. This leaves the target
	 * burning and lights its neighbours as well, so the target is skipped only
	 * because it is already alight rather than because relighting it would be
	 * wrong. Issue #1146 records the choice between the two shapes.
	 *
	 * A TARGET THAT DOES NOT MEET THE CONDITION SPREADS NOTHING, and the skill
	 * around it still worked. A hex laid on an enemy that is not burning is a
	 * working cast; it just lights nobody.
	 *
	 * IT SETS ALIGHT AND DEALS NO DAMAGE OF ITS OWN, which is what "sets alight
	 * everything within 4 meters" says and what the consuming spread above also
	 * does.
	 *
	 * @return how many enemies were set alight
	 */
	int32 SpreadFireAround(AActor* From);

	/**
	 * How far a self buff this character is holding spreads a consumed fire.
	 *
	 * THE SWORD'S ASHEN EDGE AND NOTHING ELSE TODAY. It states a `ConsumeRadius`
	 * and consumes nothing itself, which is the only way a skill row can say
	 * "while this is up, what OTHER skills consume also spreads".
	 *
	 * READ FROM THE RUNNING ABILITY RATHER THAN FROM STATE KEPT BESIDE IT. A
	 * buff that lasts is an active ability for as long as it lasts, so its own
	 * numbers are already the answer, and there is nothing extra to clear when
	 * it ends, is cancelled, or its owner dies.
	 *
	 * Zero for a character holding no such buff, which is the ordinary case.
	 */
	static float HeldConsumeSpreadRadiusCm(const AActor* Self);

	/**
	 * How many units of this skill's `ScalingSource` apply to one blow.
	 *
	 * THREE SOURCES ARE COUNTED HERE AND ELEVEN EXIST. `SCALING_SOURCES` in
	 * `tools/generate_datatables.py` closes the list; a source this does not
	 * know answers zero, so a skill naming one scales by nothing rather than
	 * counting the wrong thing.
	 *
	 *   `HealthMissing`  percentage points of maximum health the caster lacks.
	 *                    The Fist's Searing Hook: "1% increased damage for every
	 *                    1% of your maximum health you are currently missing".
	 *   `Consumed`       how many OTHER enemies had their fire put out by the
	 *                    same use. The Sword's Extinction: "rising by 15% for
	 *                    every other enemy consumed in the same instant".
	 *   `Consume`        one when this target's own fire was put out, zero
	 *                    otherwise. The Sword's Quench: "any enemy already
	 *                    alight has their fire consumed and takes 50% more
	 *                    damage for it".
	 *
	 * `Burning` IS NOT HERE AND IS NOT MISSING. `UCataclysmSelfBuffSkill` counts
	 * it for itself, because a buff counts once when it goes up and this is
	 * asked once per blow.
	 *
	 * @param ConsumedCount        how many enemies this use put out
	 * @param bThisTargetConsumed  whether the target being priced was one of them
	 */
	float ScalingUnits(int32 ConsumedCount, bool bThisTargetConsumed) const;

	/**
	 * This skill's damage percent after its own scaling and its own ceiling.
	 *
	 * WHAT THESE PARAMETERS MOVE IS THE SKILL'S OWN PERCENT, not the character's
	 * increases sum, and that reading is taken from what the rows themselves
	 * say. Every Demonic row using them describes the result as a percentage of
	 * weapon damage -- Extinction "350% weapon damage, rising by 15% ... to a
	 * maximum of 500%", Backswing "175% weapon damage at once, rising to 350%"
	 * -- and `MaxDamagePercent` is documented in that same unit. A bonus put
	 * into the character's sum instead could not be capped in percent of weapon
	 * damage at all, so the ceiling those rows state would never bind.
	 *
	 * THE TWO BUCKETS STILL DIFFER FROM ONE ANOTHER, which is the part of the
	 * vocabulary that matters. `IncreasedDamagePer` is summed and applied once
	 * and `MoreDamagePer` multiplies separately, so a skill stating both is not
	 * the same as one stating their total. `docs/DECISIONS.md` records why the
	 * bucket is written into the parameter name.
	 *
	 * A SELF BUFF DOES NOT COME THROUGH HERE. `UCataclysmSelfBuffSkill` turns
	 * its `MoreDamagePer` into a stat modifier on the caster instead, because a
	 * buff grants something that lasts and reaches every skill it is scoped to,
	 * while this sizes one blow. Both use the same words for the same buckets.
	 *
	 * `MinDamagePercent` IS NOT READ HERE. It is the floor of a charged skill
	 * released early, and nothing in the game holds a skill: only the
	 * Greatsword's Backswing states one. Issue #1141.
	 */
	float ScaledDamagePercent(float Units) const;

	/**
	 * Deal this skill's blow, giving each target whatever its own consumption is
	 * worth.
	 *
	 * TWO GROUPS AND TWO FIGURES WHEN THE SOURCE IS PER-TARGET, one when it is
	 * not. Quench's `ScalingSource=Consume` asks about the enemy in front of it,
	 * so enemies whose fire went out are priced apart from ones that were never
	 * alight. Extinction's `Consumed` counts the whole use, so every target
	 * takes the same figure.
	 *
	 * @return how much damage was sent, summed, before mitigation
	 */
	float HitScaled(const TArray<AActor*>& Targets,
					const TArray<AActor*>& Consumed);

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
	 * Leave persistent terrain, if this skill's `Terrain` cell names a kind.
	 *
	 * A SECOND RIDER BESIDE THE BURNING GROUND, AND NOT THE SAME THING. The
	 * parameter header draws the line: burning ground is a damage patch and
	 * terrain "changes where a fight can happen -- one burns you for standing
	 * there, the other decides where 'there' is". Five rows across the Spear and
	 * the Warhammer state one, and three of those five state burning ground as
	 * well, so a skill can leave both and they do not interfere.
	 *
	 * THE HOLD IT APPLIES COMES FROM `ForcedMovementDuration`, which four of the
	 * five rows state. That is deliberate rather than a shortcut: Thicket pins
	 * for 6 seconds whether a creature was caught by the cast or walked into the
	 * spears afterwards, and reading one number for both is what keeps those two
	 * the same. Groundbreaker states none, so its fissures take the default on
	 * `ACataclysmTerrain`.
	 *
	 * @param Start  the near end. A pit, fissure or thicket puts both ends here
	 * @param End    the far end, which only a wall uses
	 */
	ACataclysmTerrain* LeaveTerrainAlong(const FVector& Start, const FVector& End);

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

	/**
	 * The wait between a swing starting and its blow landing. Issue #1133.
	 *
	 * ARMED BY WhenTheSwingConnects AND CLEARED BY A CANCELLED EndAbility.
	 * It stays unset for every skill whose character played no animation,
	 * because those strike immediately and never arm a timer at all.
	 */
	FTimerHandle SwingTimer;
};
