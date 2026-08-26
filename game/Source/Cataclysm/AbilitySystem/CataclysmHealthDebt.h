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
	 * Take what is owed, if it has fallen due. Called every regeneration step.
	 *
	 * NOTHING OWED AND NOT YET DUE BOTH DO NOTHING, and the two do not have to be
	 * told apart: `IsHealthDebtDue` answers false for both.
	 *
	 * A CORPSE PAYS NOTHING. A dead character is skipped for the reason
	 * `UCataclysmRegeneration::ApplyStep` skips one: an enemy is destroyed on the
	 * step after it dies, so there is a real window in which a dead creature is
	 * still standing there with an ability system.
	 *
	 * @return how much health was really taken, which is zero in every case above
	 */
	static float SettleIfDue(AActor* Character);

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
	 * Kill this character if what it owes has passed its current health.
	 * Issue #997. Called every regeneration step, beside the settle.
	 *
	 * STRICTLY GREATER, BECAUSE THE DESIGN WRITES "EXCEEDS". A debt exactly
	 * equal to current health does not kill.
	 *
	 * ONLY FOR A CHARACTER CARRYING THE RECKONING, and only that node's debt is
	 * lethal. An ordinary deferred debt larger than current health simply takes
	 * health to nothing when it settles.
	 *
	 * HEALTH IS SET TO ZERO AND `HandleDeath` IS CALLED, which is the pair
	 * `UCataclysmVitalAttributeSet::NotifyIfHealthReachedZero` uses, so a
	 * character dies down one code path however it got there.
	 *
	 * @return whether it killed the character
	 */
	static bool KillIfDebtExceedsHealth(AActor* Character);
};
