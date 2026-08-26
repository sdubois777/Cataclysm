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
 * ONLY THE FIRST IS BUILT HERE. Issue #991. The other three need a scale, a
 * delay extension and a kill hook respectively, and each is its own piece of
 * work; what they all needed first was for a character to be able to owe
 * anything at all.
 *
 * A SEPARATE CLASS OF STATIC FUNCTIONS, like `UCataclysmFervour`,
 * `UCataclysmLeech` and `UCataclysmRegeneration`, and for the reason the first
 * of those gives: the rules are arithmetic on a few numbers, so they can be
 * checked by passing numbers in rather than by building a character, a world and
 * an effect spec for every case.
 *
 * TWO PLACES TOUCH IT AND NO MORE.
 *
 *   deferring  `UCataclysmSkillTemplate::PayHealthCost`, which is the one place
 *              a health cost is worked out
 *   settling   `ACataclysmCharacterBase::RegenerationStep`, the per-character
 *              timer that already runs regeneration and leech
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

	/**
	 * How long a deferred cost waits before it falls due.
	 *
	 * THREE SECONDS, WHICH THE DESIGN STATES OUTRIGHT. Deferred Payment: "It is
	 * taken 3 seconds later." A constant rather than a stat because no node
	 * changes this number: the Rolling Debt node EXTENDS it, which is a separate
	 * amount added on top and is not built yet.
	 */
	static constexpr float DelaySeconds = 3.0f;

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
};
