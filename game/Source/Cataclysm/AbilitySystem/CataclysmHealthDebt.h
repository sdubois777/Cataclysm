// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmHealthDebt.generated.h"

class AActor;
class UAbilitySystemComponent;

/**
 * Health a character owes and has not yet paid.
 *
 * WHAT IT IS FOR. The Blood Tithe branch of the Masochist tree is built on the
 * idea that a health cost can be taken later instead of now, and four of its
 * nodes say so:
 *
 *   Deferred Payment   a share of a cost is not taken until 3 seconds later
 *   Compound Interest  more damage for every 5% of maximum health owed
 *   Rolling Debt       paying again while something is owed extends the delay
 *   The Reckoning      costs are never taken, and a debt past your health kills
 *
 * ALL FOUR NOW WORK. Deferred Payment came first, on its own, because what the
 * other three needed before anything else was for a character to be able to owe
 * anything at all. Issue #991 built that; issues #994, #995 and #997 built the
 * scale, the delay extension and the kill hook the other three need.
 *
 * COMPOUND INTEREST IS THE ONE THAT NEEDED NO CODE HERE. A bonus whose size
 * grows with what is owed is a `Scale` on the stat pipeline, and this class is
 * only where the amount comes from.
 *
 * A SEPARATE CLASS OF STATIC FUNCTIONS, like `UCataclysmFervour`,
 * `UCataclysmLeech` and `UCataclysmRegeneration`, and for the reason the first
 * of those gives: the rules are arithmetic on a few numbers, so they can be
 * checked by passing numbers in rather than by building a character, a world and
 * an effect spec for every case.
 *
 * THREE PLACES TOUCH IT AND NO MORE.
 *
 *   deferring  `UCataclysmSkillTemplate::PayHealthCost`, which is the one place
 *              a health cost is worked out. It also extends an outstanding debt
 *              there, because paying is what extends it.
 *   settling   `ACataclysmCharacterBase::RegenerationStep`, the per-character
 *              timer that already runs regeneration and leech. The lethal check
 *              The Reckoning needs runs on the same step.
 *   clearing   `ACataclysmEnemyCharacter::HandleDeath`, which already reaches
 *              the player to grant experience for the kill
 *
 * IT NEEDS NO TIMER OF ITS OWN, which is why settling is a third job on that
 * step rather than a fourth timer. A debt falling due a fraction of a second
 * late is not something a player can perceive, and a timer per debt would be one
 * more thing to cancel when a character dies.
 */
UCLASS()
class CATACLYSM_API UCataclysmHealthDebt : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	//~ The stat name, as `game/Data/PassiveEffects.csv` spells it. It is the key
	//~ `UCataclysmPlayerClassStats::StatToAttribute` is looked up by, so it
	//~ exists once here rather than as a literal at each use.

	/** What share of a health cost is taken later instead of now, as a percent. */
	static const TCHAR* DeferredShareStat;

	/** How far one further payment pushes an outstanding debt out, in seconds. */
	static const TCHAR* DelayExtensionStat;

	/** Whether this character's debt is never taken on a timer. Zero for no. */
	static const TCHAR* ClearedOnlyByAKillStat;

	/**
	 * Whether the part of a cost this character cannot pay becomes debt instead
	 * of emptying its health. Zero for no. Issue #1069.
	 */
	static const TCHAR* UnpayableBecomesDebtStat;

	/** Whether dropping to low health clears what is owed. Zero for no. #1069. */
	static const TCHAR* ClearedOnDroppingLowStat;

	/**
	 * How long a deferred cost waits before it falls due.
	 *
	 * THREE SECONDS, WHICH THE DESIGN STATES OUTRIGHT. Deferred Payment: "It is
	 * taken 3 seconds later." A constant rather than a stat because no node
	 * changes this number: the Rolling Debt node EXTENDS it, which is a separate
	 * amount added on top and is held below.
	 */
	static constexpr float DelaySeconds = 3.0f;

	/**
	 * How far one debt may be pushed out altogether, however many payments.
	 * Issue #995.
	 *
	 * THREE SECONDS, WHICH ROLLING DEBT STATES: "extends the delay on what is
	 * owed by 0.5 seconds per point, to a maximum of 3 seconds."
	 *
	 * IT CAPS THE TOTAL RATHER THAN ONE PAYMENT, and that is a reading of the
	 * sentence rather than something it settles outright. The node holds six
	 * points and six times half a second is exactly three, so the two readings
	 * agree on a single payment and disagree only on repeated ones. Capping one
	 * payment would make the clause say nothing and would let a character that
	 * keeps paying costs push a debt out for ever, which matters because
	 * Compound Interest pays more the larger the debt is. Issue #996 carries the
	 * question, the genre precedent and the recommendation the project owner was
	 * asked to confirm.
	 */
	static constexpr float MaxDelayExtensionSeconds = 3.0f;

	/**
	 * How long a debt takes to come out of health once it is being taken.
	 * Issue #1120.
	 *
	 * FIVE SECONDS, ASKED FOR BY THE PROJECT OWNER ON 2026-08-31, AND IT
	 * REPLACES A SINGLE HIT. A debt used to be taken whole, in one write, three
	 * seconds after the cast that deferred it. That arrived detached in time
	 * from anything the player had pressed, and it is the shape behind their
	 * report of losing about 2,500 health "as soon as I hit e": nothing on
	 * screen connected the loss to the cast, because by then the cast was three
	 * seconds in the past.
	 *
	 * A DRAIN IS ALSO WHAT MAKES ROCK BOTTOM REACHABLE, which is the second
	 * reason it was asked for. That capstone option fires when health crosses
	 * below a fifth, and a debt taken in one write either does not reach the
	 * line or lands far past it. Health falling steadily crosses it.
	 *
	 * IT IS NOT A STAT AND NO NODE CHANGES IT, the same footing as
	 * `DelaySeconds` above and as `UCataclysmLeech::PayoutSeconds`, which is
	 * this same shape with the sign reversed: an amount paid out linearly across
	 * a fixed span on the regeneration step.
	 *
	 * EXPECTED TO BE TUNED BY EYE. Five seconds is the project owner's figure
	 * from play and not one taken from a shipped game.
	 */
	static constexpr float DrainSeconds = 5.0f;

	/**
	 * What share of a health cost this character takes later, as a percentage.
	 *
	 * Zero for a character with no point in Deferred Payment, and zero for any
	 * ability system without the class resource attribute set, which is every
	 * enemy. An enemy using a skill goes through the same cost function.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Health Debt")
	static float DeferredSharePercent(const UAbilitySystemComponent* AbilitySystem);

	/**
	 * How much of a cost is taken later, given the whole cost and the share.
	 *
	 * PURE ARITHMETIC AND NO ABILITY SYSTEM, so every case can be checked by
	 * passing numbers in. Answers zero for a cost of nothing and for a share of
	 * nothing, and never answers more than the cost.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Health Debt")
	static float AmountDeferred(float Cost, float SharePercent);

	/**
	 * Add this much to what the character owes, falling due after `DelaySeconds`.
	 *
	 * DOES NOTHING FOR AN AMOUNT OF NOTHING, so a character with no point in the
	 * node never acquires a due time it would then have to clear.
	 */
	static void Defer(UAbilitySystemComponent* AbilitySystem, float Amount);

	/**
	 * How much of a remaining amount comes out in a step of this length.
	 *
	 * PURE ARITHMETIC AND NO ABILITY SYSTEM, so every case can be checked by
	 * passing numbers in.
	 *
	 * LINEAR, AND THE LAST STEP TAKES WHATEVER IS LEFT. Taking a fixed fraction
	 * of the remainder each step would leave a shrinking amount that never
	 * reaches zero, so a step at least as long as the time left takes the whole
	 * balance. This is `UCataclysmLeech::PaidInStep` with the sign reversed, and
	 * it is deliberately the same shape: leech pays an amount into a pool across
	 * a fixed span on this same timer, and a debt takes one out of one.
	 *
	 * A NEGATIVE OR ZERO TIME LEFT TAKES EVERYTHING, which is what a step
	 * arriving late has to do. It is reachable rather than theoretical: the
	 * timer is a quarter second and a frame can be longer than that.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Health Debt")
	static float DrainedInStep(float Remaining, float SecondsLeft,
							   float SecondsInStep);

	/**
	 * Drain one step's worth of what is owed out of health, if it has fallen
	 * due. Called every regeneration step. Issue #1120.
	 *
	 * A DRAIN AND NOT A SINGLE HIT, ASKED FOR BY THE PROJECT OWNER ON
	 * 2026-08-31. This used to take the whole debt in one write three seconds
	 * after the cast, which arrived detached in time from anything the player
	 * had pressed. It now comes out over `DrainSeconds`. The delay before it
	 * starts is unchanged, so the Deferred Payment and Rolling Debt nodes still
	 * say what they said.
	 *
	 * THE RATE IS FIXED WHEN THE DEBT FALLS DUE AND NOT RECOMPUTED FROM WHAT IS
	 * LEFT, which is what `DrainedInStep` above is for. How far through the
	 * drain this step is comes from the due time the ability system already
	 * keeps, so nothing new has to be stored.
	 *
	 * NOTHING OWED AND NOT YET DUE BOTH DO NOTHING, and the two do not have to be
	 * told apart: `IsHealthDebtDue` answers false for both.
	 *
	 * A RECKONING DEBT IS NOT TOUCHED HERE. It never falls due at all;
	 * `DrainWhileDebtExceedsHealth` below is what happens to that one.
	 *
	 * A CORPSE PAYS NOTHING. A dead character is skipped for the reason
	 * `UCataclysmRegeneration::ApplyStep` skips one: an enemy is destroyed on the
	 * step after it dies, so there is a real window in which a dead creature is
	 * still standing there with an ability system.
	 *
	 * @return how much health was really taken this step, zero in every case above
	 */
	static float DrainIfDue(AActor* Character, float SecondsInStep);

	/**
	 * How far one further payment pushes this character's debt out, in seconds.
	 * Issue #995.
	 *
	 * Zero for a character with no point in Rolling Debt, and zero for any
	 * ability system without the class resource attribute set, which is every
	 * enemy. The same two answers `DeferredSharePercent` gives and for the same
	 * reasons.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Health Debt")
	static float DelayExtensionSeconds(const UAbilitySystemComponent* AbilitySystem);

	/**
	 * Push an outstanding debt out because a health cost was just paid.
	 * Issue #995.
	 *
	 * CALLED BEFORE THIS PAYMENT'S OWN DEFERRAL IS ADDED, which is what makes
	 * "while one is still owed" mean what it says. Called afterwards, the first
	 * debt of a fight would extend itself.
	 *
	 * NOTHING HAPPENS WITH NOTHING OWED. That is the ordinary case: a character
	 * that pays a cost outright, or the first payment after a debt settled.
	 *
	 * @return how many seconds the due time really moved
	 */
	static float ExtendForPaymentWhileOwing(UAbilitySystemComponent* AbilitySystem);

	/**
	 * Whether this character's debt is never taken on a timer, is cleared by
	 * killing an enemy, and kills it if it passes its current health. #997.
	 *
	 * False for every character without the Masochist's The Reckoning keystone,
	 * and false for any ability system without the class resource attribute set.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Health Debt")
	static bool IsClearedOnlyByAKill(const UAbilitySystemComponent* AbilitySystem);

	/**
	 * Clear what this character owes, because it just killed something.
	 * Issue #997.
	 *
	 * ONLY FOR A CHARACTER CARRYING THE RECKONING. For anybody else the design
	 * says nothing about a kill and clearing a debt would be a gift: an ordinary
	 * deferred cost is meant to be paid.
	 *
	 * @return how much was cleared, which is zero for every other character
	 */
	static float ClearOnKill(AActor* Killer);

	/**
	 * Drain health while what this character owes is past its current health.
	 * Issue #997, changed to a drain by issue #1120. Called every regeneration
	 * step, beside the drain above.
	 *
	 * IT USED TO KILL ON THE SPOT AND NOW BLEEDS THE CHARACTER OUT INSTEAD,
	 * asked for by the project owner on 2026-08-31. The death is not removed --
	 * a character in this state still dies, and faster the deeper in debt it is,
	 * because its health runs out before `DrainSeconds` do. What changes is that
	 * there is now something to see and a few seconds to act in.
	 *
	 * THE DEBT IS NOT REDUCED BY DRAINING, WHICH IS THE WHOLE DIFFERENCE FROM
	 * `DrainIfDue` ABOVE. The Reckoning says its debt "is cleared only by
	 * killing an enemy", so health comes out and the amount owed stays where it
	 * is. That also keeps the keystone's damage bonus, which is read off the
	 * amount owed, standing while the character bleeds.
	 *
	 * SO THE RATE IS CONSTANT UNLESS THE CHARACTER CASTS AGAIN, at what is owed
	 * spread across `DrainSeconds`. A further cast adds to the debt and the
	 * bleeding gets faster, which is the right way round.
	 *
	 * AND IT IS WHAT PUTS ROCK BOTTOM WITHIN REACH. That option fires when
	 * health crosses below a fifth, and health falling steadily crosses it,
	 * where a single write to zero never did. `UCataclysmLowHealthRelief` then
	 * clears the debt and the bleeding stops. Issue #1119 has the measurements
	 * of how little that option did before this.
	 *
	 * IT STILL DOES NOT HELP A CHARACTER ALREADY BELOW A FIFTH when the debt
	 * passes its health, because there is no crossing to notice. Said plainly
	 * here because it is the one case this does not reach.
	 *
	 * STRICTLY GREATER, BECAUSE THE DESIGN WRITES "EXCEEDS". A debt exactly
	 * equal to current health drains nothing.
	 *
	 * ONLY FOR A CHARACTER CARRYING THE RECKONING, and only that node's debt
	 * behaves this way. An ordinary deferred debt drains through `DrainIfDue`
	 * above and takes health to nothing if that is all there is.
	 *
	 * WHEN THE STEP WOULD TAKE THE LAST OF THE HEALTH, health is set to zero and
	 * `HandleDeath` is called, which is the pair
	 * `UCataclysmVitalAttributeSet::NotifyIfHealthReachedZero` uses, so a
	 * character dies down one code path however it got there.
	 *
	 * @return how much health was really taken this step
	 */
	static float DrainWhileDebtExceedsHealth(AActor* Character,
											 float SecondsInStep);

	/**
	 * Whether a cost this character cannot afford becomes debt rather than
	 * emptying its health. Issue #1069, the Masochist's Rock Bottom: "A health
	 * cost can never reduce you below 1 health; anything you cannot pay becomes
	 * health debt instead."
	 *
	 * False for every character without that capstone option, and false for any
	 * ability system with no class resource attribute set, which is every enemy.
	 * The same two answers `DeferredSharePercent` gives and for the same
	 * reasons.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Health Debt")
	static bool UnpayableBecomesDebt(const UAbilitySystemComponent* AbilitySystem);

	/**
	 * How much of a charge this character cannot pay without going past the
	 * floor. Issue #1069.
	 *
	 * PURE ARITHMETIC AND NO ABILITY SYSTEM, like `AmountDeferred` above, so
	 * every case can be checked by passing numbers in.
	 *
	 *     unpayable = charge - what is above the floor
	 *
	 * ANSWERS ZERO FOR A CHARGE THE CHARACTER CAN AFFORD, which is the ordinary
	 * case even for a character holding the option: most costs are a small
	 * share of health.
	 *
	 * AND ANSWERS THE WHOLE CHARGE FOR A CHARACTER ALREADY AT OR BELOW THE
	 * FLOOR. There is nothing above the floor to take, so all of it is owed.
	 * That is reachable: a character sitting on exactly 1 health has paid down
	 * to the floor already.
	 *
	 * @param Charge          what is about to be taken off health
	 * @param CurrentHealth   what the character has now
	 * @param LeastHealthLeft the floor the charge may not take health past
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Health Debt")
	static float AmountUnpayable(float Charge, float CurrentHealth,
								 float LeastHealthLeft);

	/**
	 * Clear what this character owes, because its health has just dropped low.
	 * Issue #1069.
	 *
	 * THE SECOND SENTENCE OF Rock Bottom: "Dropping below 20% health clears all
	 * outstanding debt and grants 50 Fervour, no more than once every 30
	 * seconds." `UCataclysmLowHealthRelief` owns the threshold and the
	 * cooldown and is the only caller; this is only the clearing.
	 *
	 * A THIRD WAY A DEBT ENDS, beside settling and a kill, and the three are
	 * separate on purpose. This one is not `ClearOnKill` with a different
	 * trigger: that one fires only for a character carrying The Reckoning,
	 * whose debt never falls due at all, and this one clears an ORDINARY debt
	 * that would otherwise be taken.
	 *
	 * @return how much was cleared, which is zero for a character without the
	 *         option and zero for one that owed nothing
	 */
	static float ClearOnDroppingLow(AActor* Character);
};
