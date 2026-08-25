// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmLeech.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "CataclysmAbilitySystemComponent.generated.h"

struct FGameplayTag;

/**
 * The project's ability system component.
 *
 * Exists as a subclass from the start so that behaviour common to every actor
 * with abilities has somewhere to live without a later refactor touching every
 * character class.
 *
 * It also owns the input side of the ability system. A key press does not name
 * an ability; it names a SLOT, as a Slot.* gameplay tag. Whichever granted
 * ability carries that tag is the one that runs. See CataclysmAbilitySlots in
 * CataclysmGameplayAbility.h for where the slot list and the tag list meet.
 */
UCLASS()
class CATACLYSM_API UCataclysmAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UCataclysmAbilitySystemComponent();

	/**
	 * The share of a stated knockback distance the NEXT displacement of this
	 * target actually moves it, recording that displacement as happening now.
	 *
	 * THE RULE, from "Stun and the Anti-Stun-Lock Rule" in
	 * docs/Cataclysm_GDD_v2.md and decided on issue #302: each displacement
	 * applied to a target already displaced within the last 5 seconds moves it
	 * HALF AS FAR as the one before -- the full distance, then half, then a
	 * quarter. The count resets once 5 seconds pass in which that target is not
	 * displaced at all.
	 *
	 * SO REPEATED SHOVES CANNOT HOLD A TARGET AT THE FAR END OF A ROOM. Four
	 * metres, then two, then one is seven metres in total and nothing worth
	 * measuring after that. That is the failure the rule was written for.
	 *
	 * WHY THE COUNT LIVES ON THIS COMPONENT. It belongs to the TARGET and has to
	 * outlive the skill that caused it, because the second shove is usually a
	 * different skill and often a different actor. This component is what every
	 * hittable thing has: being hit goes through
	 * UCataclysmSkillEffects::ApplyHit, which requires one. So it covers the
	 * player, every enemy and every minion in both directions, which issue #310
	 * settled the design needs. The shared character base was the first choice
	 * and was wrong, because a thing can be hit without being one.
	 *
	 * NO DAMAGE THRESHOLD, NO IMMUNITY FLAG AND NO BOSS EXEMPTION, unlike the
	 * stun rule. A boss is pushed under the same halving as anything else,
	 * because a boss pushed four metres is still fighting while a boss held still
	 * is not a fight.
	 *
	 * @return 1.0 for the first displacement in a window, then 0.5, 0.25, and so
	 *         on. Never zero, so a shove always visibly shoves. Issue #628.
	 */
	float TakeNextDisplacementShare();

	/**
	 * How many times this has been displaced inside the current window, without
	 * counting a new one or resetting anything. Read by tests.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Abilities")
	int32 DisplacementsInWindow() const;

	/** Records that the key for this slot went down. Does not activate anything yet. */
	void AbilityInputTagPressed(const FGameplayTag& InputTag);

	/** Records that the key for this slot came up. */
	void AbilityInputTagReleased(const FGameplayTag& InputTag);

	/**
	 * Activates whatever the presses recorded this frame asked for, then clears
	 * the record. Called once per frame from the player controller.
	 *
	 * WHY THIS IS DEFERRED RATHER THAN ACTIVATING ON THE KEY PRESS ITSELF.
	 * Enhanced Input fires its delegates part-way through the frame, and more
	 * than one can fire before the frame's input is complete. Doing the work at
	 * one known point means every ability activated in a frame sees the same
	 * state, rather than a result that depends on the order the engine happened
	 * to deliver two key presses in.
	 */
	void ProcessAbilityInput();

	/**
	 * Forgets every press and release not yet processed, and tells every running
	 * ability its input was released.
	 *
	 * Needed when input stops arriving for a reason the ability cannot see: the
	 * pawn is unpossessed, the player opens a menu, the game pauses. Without it
	 * an ability waiting on a key release waits for one that will never come.
	 */
	void ClearAbilityInput();

	/**
	 * Grants an ability into a slot the CALLER names, rather than the slot the
	 * ability class declares.
	 *
	 * WHY THIS EXISTS ALONGSIDE UCataclysmAbilitySet. An ability set reads each
	 * ability's own declared slot, which is right for a starting kit where every
	 * ability knows where it belongs. It cannot express what a weapon does: the
	 * equipped weapon decides which skill sits in each of the six slots, so the
	 * slot is a property of the pairing and not of the ability. It is also what
	 * lets one placeholder class stand in for six different slots while the real
	 * skills are undesigned.
	 *
	 * @return the granted spec's handle, or an invalid handle if nothing was
	 *         granted, which happens on a client, with no ability class, or with
	 *         a slot of None.
	 */
	FGameplayAbilitySpecHandle GiveAbilityInSlot(
		TSubclassOf<UGameplayAbility> AbilityClass,
		ECataclysmAbilitySlot Slot,
		int32 Level = 1,
		UObject* SourceObject = nullptr);

	// -- The three-bucket stat pipeline's modifiers -------------------------
	//
	// WHY THESE LIVE HERE AND NOT AS ATTRIBUTES. UCataclysmStatPipeline scopes
	// every modifier by the tags of the SKILL IN HAND, so a character's
	// increased fire damage has no single value: it is one number for a skill
	// tagged Element.Demonic and another for one that is not. A gameplay
	// attribute is one number per character, so it cannot express that. This
	// list is what an aggregated attribute would be if the engine could scope by
	// the ability being used.
	//
	// WHAT PUTS THINGS IN IT. Today only a skill's own self buff, which adds on
	// activation and removes when its duration expires. Gear, gems and passive
	// keystones will add to the same list; issue #166 is the first thing that
	// needed a route into it at all.

	/**
	 * Add a modifier and get a handle that takes it away again.
	 *
	 * @return a handle, always positive, or 0 if the modifier was refused.
	 *         UCataclysmStatPipeline::ValidateModifier decides; a More
	 *         multiplier from a source that may not grant one is refused here
	 *         rather than silently ignored at evaluation time.
	 */
	int32 AddStatModifier(const FCataclysmStatModifier& Modifier);

	/** Take a modifier away. False when the handle is unknown or already gone. */
	bool RemoveStatModifier(int32 Handle);

	/** Change a live modifier's magnitude. False when the handle is unknown. */
	bool SetStatModifierValue(int32 Handle, float NewValue);

	/** A live modifier's magnitude, or zero when the handle is unknown. */
	float GetStatModifierValue(int32 Handle) const;

	/**
	 * Every modifier this character currently carries, for the pipeline.
	 *
	 * Returned by reference and not by value because a hit evaluates this on
	 * every target it touches.
	 */
	const TArray<FCataclysmStatModifier>& GetStatModifiers() const
	{
		return StatModifiers;
	}

	/**
	 * Leech this character has been promised and not yet been paid. Issue #895.
	 *
	 * A LIST RATHER THAN A RUNNING TOTAL, because the design gives every hit its
	 * own 3-second payout and says they run alongside one another: "A character
	 * hitting continuously therefore reaches a steady state of roughly three
	 * hits' worth of leech in flight." One total could not express three
	 * payments with three different deadlines.
	 *
	 * IT EMPTIES ITSELF. UCataclysmLeech::PayOutStep drops a payment as soon as
	 * its balance or its time is gone, so a character that stops fighting stops
	 * carrying anything within three seconds.
	 */
	const TArray<FCataclysmLeechPayment>& GetLeechPayments() const
	{
		return LeechPayments;
	}

	/**
	 * The sum of increases that produced this character's attack damage.
	 *
	 * WHY A FINISHED ATTRIBUTE IS NOT ENOUGH. `AttackDamage` is
	 * `(base + flat) x (1 + increases)` with the increases already applied and
	 * no longer visible. A CONDITIONAL increase -- increased damage against
	 * Demonic enemies -- has to join that same bracket rather than becoming a
	 * second multiplier, and the design says so outright: "a conditional
	 * increase joins the increases bracket rather than becoming a third
	 * multiplier. That is what Diablo 4 and Last Epoch both do." Knowing the
	 * bracket is what lets a hit reopen it.
	 *
	 * THE DIFFERENCE IS LARGE. A character at +125% increased damage striking a
	 * matching enemy with a top-tier +400% affix deals 6.25 times its base if the
	 * two add and 11.25 times if they multiply.
	 *
	 * A FRACTION, NOT A PERCENTAGE. 1.25 for +125%, which is what
	 * UCataclysmStatPipeline reports.
	 */
	float GetAttackDamageIncreases() const
	{
		return AttackDamageIncreases;
	}

	/** Written by UCataclysmPlayerClassStats::ApplyTo when it writes the stat. */
	void SetAttackDamageIncreases(float Increases)
	{
		AttackDamageIncreases = Increases;
	}

	/**
	 * What one stat was worked out from, or null for a stat nothing recorded.
	 *
	 * FOR A CALLER THAT WANTS THE WHOLE BREAKDOWN. Most callers want a number
	 * and should use `StatForSkill` below instead.
	 */
	const FCataclysmStatInputs* GetStatInputs(FName Stat) const
	{
		return StatInputs.Find(Stat);
	}

	/** Written by UCataclysmPlayerClassStats::ApplyTo, once for every refresh. */
	void SetStatInputs(TMap<FName, FCataclysmStatInputs>&& Inputs)
	{
		StatInputs = MoveTemp(Inputs);
	}

	/**
	 * What one stat is worth to a skill carrying these tags. Issue #943.
	 *
	 * WHY A SKILL HAS TO ASK RATHER THAN READ THE ATTRIBUTE. The gameplay
	 * attribute holds the value with no skill in hand, so every modifier naming
	 * a required tag is missing from it. `UCataclysmStatPipeline` says why in its
	 * own header: "a character's area of effect has no single value -- it is one
	 * number for an area skill and another for a single-target one".
	 *
	 * `Fallback` IS RETURNED WHEN NOTHING WAS RECORDED FOR THE STAT, and that is
	 * the ordinary case rather than a fault: an enemy's ability system is never
	 * given a character stat line, and a player's has none until the first
	 * refresh. Pass the attribute's own value, so the answer is unchanged from
	 * what it was before this existed.
	 */
	float StatForSkill(FName Stat, const FGameplayTagContainer& SkillTags,
					   float Fallback) const;

	/** Promise this character one hit's worth of leech. */
	void AddLeechPayment(const FCataclysmLeechPayment& Payment)
	{
		LeechPayments.Add(Payment);
	}

	/** Replace the list with what is still owed after a step. */
	void SetLeechPayments(TArray<FCataclysmLeechPayment>&& Payments)
	{
		LeechPayments = MoveTemp(Payments);
	}

protected:
	/** Slots pressed since the last ProcessAbilityInput. Not replicated; local input only. */
	TArray<FGameplayAbilitySpecHandle> InputPressedSpecHandles;

	/** Slots released since the last ProcessAbilityInput. */
	TArray<FGameplayAbilitySpecHandle> InputReleasedSpecHandles;

	/**
	 * The modifiers themselves, and the handle of each, at the same index.
	 *
	 * TWO ARRAYS RATHER THAN ONE ARRAY OF PAIRS so that GetStatModifiers can
	 * hand the pipeline exactly what it takes without building a copy on every
	 * hit. The two are always the same length; every function that changes one
	 * changes the other in the same statement.
	 */
	TArray<FCataclysmStatModifier> StatModifiers;

	/**
	 * What each character-sheet stat was worked out from. Issue #943.
	 *
	 * SEPARATE FROM `StatModifiers` ABOVE, WHICH IS A DIFFERENT LIST FOR A
	 * DIFFERENT PURPOSE. That one holds modifiers a live skill buff granted and
	 * will take away again, each with a handle so it can be revoked. This one is
	 * the character's standing stat line -- what its class, its gear and its
	 * spent passive points come to -- rewritten wholesale by every refresh and
	 * never revoked piecemeal.
	 *
	 * NOT REPLICATED, for the reason the leech list is not: what a remote client
	 * needs is the resulting numbers, and every one of them is already a
	 * replicated attribute. A hit is resolved on the authority.
	 */
	TMap<FName, FCataclysmStatInputs> StatInputs;

	/**
	 * Leech promised and not yet paid, one entry per hit. Issue #895.
	 *
	 * NOT REPLICATED. What a remote client needs is the health, mana and
	 * energy shield the payout produces, and all three of those are
	 * replicated attributes already. The schedule behind them is server
	 * bookkeeping.
	 */
	TArray<FCataclysmLeechPayment> LeechPayments;

	/**
	 * The sum of increases behind the current attack damage. Issue #895.
	 *
	 * NOT REPLICATED, for the reason the leech list is not: what a remote
	 * client needs is the damage numbers the arithmetic produces, and a hit
	 * is resolved on the authority.
	 */
	float AttackDamageIncreases = 0.0f;
	TArray<int32> StatModifierHandles;

	/** Never reused, so a stale handle cannot remove somebody else's modifier. */
	int32 NextStatModifierHandle = 1;

	/**
	 * How many times this has been displaced since the window last reset, and
	 * when the last one happened in world seconds.
	 *
	 * A NEGATIVE TIME MEANS NEVER DISPLACED, told apart from "displaced at world
	 * time zero" deliberately: without it, the first shove in a fresh world would
	 * be counted as a second one and moved half as far as it should.
	 */
	int32 DisplacementCount = 0;
	float LastDisplacedAtSeconds = -1.0f;
};
