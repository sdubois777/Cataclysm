// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmAttributeAccessors.h"
#include "CataclysmClassResourceAttributeSet.generated.h"

/**
 * Fervour: the one resource every class shares.
 *
 * ONE BAR RATHER THAN ONE PER CLASS, decided by the project owner on 2026-08-25.
 * A two-handed weapon can roll 8 damage types and each unlocks 3 classes, so one
 * character can reach all 24 class trees, and 24 separately-generating bars is
 * not readable. What differs by class is how Fervour is filled and what it is
 * spent on, not which bar it goes into. A character in several trees therefore
 * has several ways to fill it and several things to spend it on, which is what
 * multiclassing buys beyond the nodes themselves. `docs/DECISIONS.md` has the
 * reasoning and the genre precedent.
 *
 * THE NAME IN THE DATA IS STILL `class_resource`, in `game/Data/ClassStats.csv`,
 * `game/Data/PassiveEffects.csv` and `UCataclysmPlayerClassStats::StatToAttribute`.
 * That name is accurate and is never shown to a player, so it was left alone
 * rather than renamed across three files for no gain.
 *
 * A SEPARATE SET FROM THE VITALS, which is what allows it to be granted to a
 * character that has a generator and withheld from one that has none.
 *
 * THE POOL AND THE RATES THAT MOVE IT. Fervour does not decay by default, and a
 * class may add a rule that changes that on its own starting node -- the
 * Berserker's generator empties it at 10 per second out of combat, the
 * Saboteur's adds no rule at all. The pool is 100 for every class and 150 for
 * the Ritualist.
 *
 * ONE CLASS'S GENERATOR IS BUILT AND THE OTHER 23 ARE NOT. Issue #954 added the
 * Masochist's, which is the three rates below: health lost to damage and health
 * spent as an ability cost both fill the bar, and healing empties it. The
 * Berserker filling on a critical strike and the Saboteur filling on a trap are
 * different rules, not different values of these, and each needs its own code.
 * `UCataclysmFervour` holds the Masochist's and is where a second one would join.
 */
UCLASS()
class CATACLYSM_API UCataclysmClassResourceAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UCataclysmClassResourceAttributeSet();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

	UPROPERTY(BlueprintReadOnly, Category = "Class Resource", ReplicatedUsing = OnRep_ClassResource)
	FGameplayAttributeData ClassResource;
	ATTRIBUTE_ACCESSORS(UCataclysmClassResourceAttributeSet, ClassResource)

	UPROPERTY(BlueprintReadOnly, Category = "Class Resource", ReplicatedUsing = OnRep_MaxClassResource)
	FGameplayAttributeData MaxClassResource;
	ATTRIBUTE_ACCESSORS(UCataclysmClassResourceAttributeSet, MaxClassResource)

	//~ The three rates that move the pool. Issue #954.
	//
	// ALL THREE ARE FERVOUR PER 1% OF MAXIMUM HEALTH, and all three start at
	// zero for every class. A character that has spent no point on a generator
	// gains no Fervour and loses none, which is what makes a generator node
	// worth a point. The Masochist's starting node grants 1 to each of the
	// three; `game/Data/PassiveEffects.csv` is where that is written down.
	//
	// WHY A RATE PER 1% RATHER THAN PER POINT OF HEALTH. The design states the
	// Masochist's generator as "1 per 1% of maximum health lost to damage", so a
	// character with 500 health and one with 5000 fill the bar at the same speed
	// relative to how much of themselves they have spent. A rate per point of
	// health would make more health mean slower generation.
	//
	// THEY ARE NOT ON THE CHARACTER SHEET, for the reason the maximum critical
	// strike chance is not: no affix grants one, nothing scales one, and none
	// has a baseline of its own. `Cataclysm.Attributes.CharacterSheetIsComplete`
	// keeps that count honest and states the same three exclusions.

	/** Fervour gained per 1% of maximum health lost to damage. */
	UPROPERTY(BlueprintReadOnly, Category = "Class Resource", ReplicatedUsing = OnRep_FervourFromDamage)
	FGameplayAttributeData FervourFromDamage;
	ATTRIBUTE_ACCESSORS(UCataclysmClassResourceAttributeSet, FervourFromDamage)

	/** Fervour gained per 1% of maximum health spent as an ability cost. */
	UPROPERTY(BlueprintReadOnly, Category = "Class Resource", ReplicatedUsing = OnRep_FervourFromCost)
	FGameplayAttributeData FervourFromCost;
	ATTRIBUTE_ACCESSORS(UCataclysmClassResourceAttributeSet, FervourFromCost)

	/**
	 * Fervour REMOVED per 1% of maximum health restored.
	 *
	 * A POSITIVE NUMBER THAT TAKES SOMETHING AWAY, which is why it is named for
	 * what it does rather than for its sign. The Masochist's starting node sets
	 * it to 1, so healing empties the bar exactly as fast as damage fills it,
	 * and its Staunch node reduces it by 5% per point.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Class Resource", ReplicatedUsing = OnRep_FervourLostToHealing)
	FGameplayAttributeData FervourLostToHealing;
	ATTRIBUTE_ACCESSORS(UCataclysmClassResourceAttributeSet, FervourLostToHealing)

	/**
	 * A percentage of MAXIMUM health every skill costs, on top of its own cost.
	 *
	 * THE MASOCHIST'S DEEPER CUTS NODE IS ITS ONLY SOURCE. Issue #970. "Your
	 * skills also cost 1% of your maximum health per point, in addition to any
	 * other cost. This cost generates Fervour like any other." The node holds
	 * ten points, so ten percent of maximum health at most.
	 *
	 * IT IS HERE RATHER THAN IN THE COMBAT SET for the reason the three rates
	 * above are: it is zero for every class, no affix grants it, nothing scales
	 * it, and a passive node is the only thing that supplies it. It belongs to
	 * the same class economy -- the cost it adds is one of the two things that
	 * fill Fervour.
	 *
	 * MEASURED AGAINST MAXIMUM HEALTH, WHICH MEANS IT CAN KILL, and that is the
	 * design rather than an oversight. `docs/DECISIONS.md` draws the distinction
	 * outright and records that the project owner drew it: a cost stated as a
	 * share of CURRENT health "cannot kill on its own: 15% of current health
	 * approaches zero without reaching it... it would kill if it were a share of
	 * maximum health". A skill's own cost is a share of current health. This one
	 * is not.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Class Resource", ReplicatedUsing = OnRep_AddedHealthCost)
	FGameplayAttributeData AddedHealthCost;
	ATTRIBUTE_ACCESSORS(UCataclysmClassResourceAttributeSet, AddedHealthCost)

	/**
	 * What this character adds to every skill's health cost, as a percentage
	 * of CURRENT health. Issue #986.
	 *
	 * A SECOND STAT RATHER THAN A LARGER FIRST ONE, and the difference is the
	 * design's own. `docs/DECISIONS.md` records the project owner drawing it,
	 * quoting this very number: a cost stated as a share of CURRENT health
	 * "cannot kill on its own: 15% of current health approaches zero without
	 * reaching it... it would kill if it were a share of maximum health". The
	 * attribute above is a share of maximum health and can kill; this one
	 * cannot, and the two would be indistinguishable if they shared a stat.
	 *
	 * THE MASOCHIST'S EXSANGUINATE KEYSTONE IS ITS ONLY SOURCE: "Every skill
	 * costs an additional 15% of your current health, and every skill deals
	 * 40% more damage." A keystone holds one point, so 15% at most.
	 *
	 * THE UNSUFFIXED NAME ABOVE MEANS "OF MAXIMUM", which is not obvious and
	 * is left alone deliberately: renaming it would touch the workbook, the
	 * generated file, the class stat map and every test naming it, to say
	 * something this comment already says.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Class Resource", ReplicatedUsing = OnRep_AddedHealthCostOfCurrent)
	FGameplayAttributeData AddedHealthCostOfCurrent;
	ATTRIBUTE_ACCESSORS(UCataclysmClassResourceAttributeSet, AddedHealthCostOfCurrent)

	/**
	 * What share of a skill's health cost is not taken when the skill is used,
	 * as a percentage. Issue #991.
	 *
	 * The Masochist's Deferred Payment node is its only source: "10% per point
	 * of the health a skill costs is not taken when the skill is used. It is
	 * taken 3 seconds later." The node holds ten points, so at most the whole
	 * cost is deferred.
	 *
	 * HELD BETWEEN 0 AND 100. Below zero it would take MORE than the cost now
	 * and owe a negative amount; above a hundred it would defer more than was
	 * charged and hand the character health back.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Class Resource", ReplicatedUsing = OnRep_DeferredHealthCostShare)
	FGameplayAttributeData DeferredHealthCostShare;
	ATTRIBUTE_ACCESSORS(UCataclysmClassResourceAttributeSet, DeferredHealthCostShare)

	/**
	 * How much health this character owes and has not yet paid. Issue #991.
	 *
	 * A POOL RATHER THAN A LEVER, which is why it is an attribute at all
	 * rather than plain state on the ability system component. A player has to
	 * be able to see it: the Blood Tithe branch's keystone, The Reckoning,
	 * kills a character whose debt passes its current health, and a number that
	 * can kill you has to be on screen.
	 *
	 * IN POINTS OF HEALTH, NOT A PERCENTAGE. Compound Interest asks about it as
	 * "every 5% of your maximum health you currently owe", which is a
	 * comparison against maximum health made where it is read rather than a
	 * unit stored here. Storing points keeps it addable to what a further cast
	 * defers without a conversion each time.
	 *
	 * WHEN IT FALLS DUE IS NOT HERE. That is a timestamp, held beside the other
	 * timestamps on `UCataclysmAbilitySystemComponent`, and it is not a
	 * quantity anybody reads.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Class Resource", ReplicatedUsing = OnRep_HealthOwed)
	FGameplayAttributeData HealthOwed;
	ATTRIBUTE_ACCESSORS(UCataclysmClassResourceAttributeSet, HealthOwed)

	/**
	 * How many seconds a health cost paid while something is already owed pushes
	 * that debt further out. Issue #995.
	 *
	 * The Masochist's Rolling Debt node is its only source: "Paying a health
	 * cost while one is still owed extends the delay on what is owed by 0.5
	 * seconds per point, to a maximum of 3 seconds." The node holds six points,
	 * so 3 seconds at most from the node itself.
	 *
	 * IN SECONDS, WHICH MAKES IT THE ONE STAT ON THIS SET THAT IS NOT A SHARE OF
	 * SOMETHING. It is added to a due time rather than to a pool.
	 *
	 * THIS IS WHAT ONE PAYMENT ADDS. How far a single debt may be pushed
	 * ALTOGETHER is a separate rule, capped by
	 * `UCataclysmHealthDebt::MaxDelayExtensionSeconds`, so a character paying
	 * costs continuously cannot hold one debt off for ever. The two numbers meet
	 * at the node's full six points and are not the same rule.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Class Resource", ReplicatedUsing = OnRep_HealthDebtDelayExtension)
	FGameplayAttributeData HealthDebtDelayExtension;
	ATTRIBUTE_ACCESSORS(UCataclysmClassResourceAttributeSet, HealthDebtDelayExtension)

	/**
	 * Whether this character's debt is never taken on a timer, is cleared by
	 * killing an enemy, and kills the character if it passes its current health.
	 * Issue #997. Zero for no, above zero for yes.
	 *
	 * The Masochist's The Reckoning keystone is its only source: "Health costs
	 * are never taken. They accumulate as a debt... the debt is cleared only by
	 * killing an enemy. If your debt ever exceeds your current health, you die."
	 *
	 * ONE FLAG FOR THREE RULES, BECAUSE THE DESIGN STATES THEM AS ONE MECHANIC.
	 * A debt that never falls due has to end some other way, and the same
	 * sentence supplies both the end and the price of not reaching it. Three
	 * separate flags would allow a debt that never falls due, is never cleared
	 * and never kills, which the design describes nowhere and no node sets.
	 *
	 * NOT THE SAME THING AS DEFERRING THE WHOLE COST, which is why this is a
	 * second stat rather than a larger `DeferredHealthCostShare`. Deferred
	 * Payment at its full ten points also defers 100% of a cost, and its debt
	 * still falls due three seconds later because its own sentence says so.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Class Resource", ReplicatedUsing = OnRep_HealthDebtClearedOnlyByAKill)
	FGameplayAttributeData HealthDebtClearedOnlyByAKill;
	ATTRIBUTE_ACCESSORS(UCataclysmClassResourceAttributeSet, HealthDebtClearedOnlyByAKill)

	/**
	 * Whether healing stops removing Fervour. Issue #1006. Zero for no, above
	 * zero for yes.
	 *
	 * TWO MASOCHIST KEYSTONES SET IT AND THEY SET IT FOR DIFFERENT HEALING.
	 * Sanguine Ledger reads "Health regeneration no longer removes Fervour" and
	 * Wounds That Feed reads "Healing from Life Leech does not remove Fervour".
	 * They are one stat because they are one rule; what tells them apart is the
	 * `Required Tags` on the row, matched against the tags the healing carries.
	 *
	 * WHICH IS WHY IT IS ASKED FOR THROUGH THE STAT PIPELINE and never read off
	 * this attribute. `UCataclysmFervour::Move` asks for it with the healing's
	 * own tags in hand, exactly as it already asks for the rate beside it. The
	 * attribute holds the answer for a character with no healing in hand, which
	 * is nobody, so a plain read would be wrong in both directions.
	 *
	 * A FLAG RATHER THAN A REDUCTION OF THE RATE, and the reason is a rule the
	 * pipeline states outright. `UCataclysmStatPipeline::LessMultiplierFloor` is
	 * -99, so no modifier can take a stat to zero: "one source could otherwise
	 * zero a stat or turn it negative". That floor is right. A node that says
	 * "does not remove" is not a 99% reduction, so it needs to say so separately.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Class Resource", ReplicatedUsing = OnRep_FervourLossSuppressed)
	FGameplayAttributeData FervourLossSuppressed;
	ATTRIBUTE_ACCESSORS(UCataclysmClassResourceAttributeSet, FervourLossSuppressed)

	/**
	 * How much Fervour this character gains every second, from nothing having
	 * happened at all. Issue #1008.
	 *
	 * THE FIRST THING THAT FILLS THE POOL ON A TIMER. Everything else that moves
	 * Fervour moves it because health moved: lost to damage, spent as a cost,
	 * restored by healing. The Masochist's Low Life keystone is its only source:
	 * "While at or below 35% health you gain 10 Fervour per second."
	 *
	 * A RATE PER SECOND, NOT PER STEP. `ACataclysmCharacterBase::RegenerationStep`
	 * multiplies it by the length of the step it is on, the same way it already
	 * does for health, mana and energy shield regeneration.
	 *
	 * ASKED FOR THROUGH THE STAT PIPELINE, because its one source carries a
	 * health condition and a conditional bonus is never folded into an
	 * attribute. A plain read would answer zero for every character for ever and
	 * nothing at run time would report it.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Class Resource", ReplicatedUsing = OnRep_FervourPerSecond)
	FGameplayAttributeData FervourPerSecond;
	ATTRIBUTE_ACCESSORS(UCataclysmClassResourceAttributeSet, FervourPerSecond)

	/**
	 * Whether dropping below half health turns damage into Bleeding. Issue #985.
	 * Zero for no, above zero for yes.
	 *
	 * THE MASOCHIST'S The Breaking Point IS ITS ONLY SOURCE: "Dropping below 50%
	 * health converts all damage you take into Bleeding over 5 seconds. The
	 * conversion lasts 3 seconds, increased by 5% per point, and cannot happen
	 * more than once every 10 seconds."
	 *
	 * A FLAG AND A DURATION, WHICH ARE TWO STATS BECAUSE THEY ANSWER TWO
	 * QUESTIONS. This one says whether the rule applies at all;
	 * `DamageToBleedingWindow` beside it says how long one turn of it lasts. The
	 * duration has a base on the Masochist's class stat line, so it reads 3
	 * seconds for every Masochist whether or not they have spent a point -- and
	 * that is harmless precisely because this flag is what decides that the rule
	 * runs at all.
	 *
	 * A FLAG RATHER THAN A DURATION OF ZERO, for the reason `FervourLossSuppressed`
	 * above gives at length: `UCataclysmStatPipeline::LessMultiplierFloor` is -99,
	 * so no modifier can take a stat to zero, and a rule that is off has to say so
	 * separately rather than by being worth nothing.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Class Resource", ReplicatedUsing = OnRep_DamageToBleedingOnLowHealth)
	FGameplayAttributeData DamageToBleedingOnLowHealth;
	ATTRIBUTE_ACCESSORS(UCataclysmClassResourceAttributeSet, DamageToBleedingOnLowHealth)

	/**
	 * How many seconds one turn of that conversion lasts. Issue #985.
	 *
	 * THREE SECONDS ON THE MASOCHIST'S CLASS STAT LINE, increased by 5% per point
	 * spent in The Breaking Point, so eight points make it 4.2 seconds.
	 *
	 * THE BASE IS A CLASS STAT AND NOT A CONSTANT IN C++, and that is what makes
	 * the node's row authorable at all. The passive effects sheet carries values
	 * PER POINT; "lasts 3 seconds, increased by 5% per point" is a base plus an
	 * increase, and an `increased` row with no base under it is worth nothing.
	 * `test_every_stat_is_one_the_game_supplies` exists to catch exactly that.
	 *
	 * READ OFF THE ATTRIBUTE, unlike its neighbours, and that is safe here
	 * BECAUSE ITS ONLY ROW CARRIES NO CONDITION AND NO SCALE. The window is
	 * measured at the moment the conversion starts, when the character has just
	 * been hit and nothing is being cast, so there is no skill context to ask
	 * with. If a later node ever conditions this stat, that read has to become a
	 * `StatForSkill` call or the row will be dropped in silence.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Class Resource", ReplicatedUsing = OnRep_DamageToBleedingWindow)
	FGameplayAttributeData DamageToBleedingWindow;
	ATTRIBUTE_ACCESSORS(UCataclysmClassResourceAttributeSet, DamageToBleedingWindow)

	static TArray<FGameplayAttribute> GetAllAttributes();

	/** The three rates above, without the pool. For a caller that wants to ask
	 *  whether this character has any way of moving Fervour at all. */
	static TArray<FGameplayAttribute> GetRateAttributes();

protected:
	UFUNCTION() void OnRep_ClassResource(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_MaxClassResource(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_FervourFromDamage(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_FervourFromCost(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_FervourLostToHealing(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_AddedHealthCost(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_AddedHealthCostOfCurrent(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DeferredHealthCostShare(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_HealthOwed(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_HealthDebtDelayExtension(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_HealthDebtClearedOnlyByAKill(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_FervourLossSuppressed(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_FervourPerSecond(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DamageToBleedingOnLowHealth(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_DamageToBleedingWindow(const FGameplayAttributeData& OldValue);
};
