// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmDamageConversion.generated.h"

class AActor;
class UAbilitySystemComponent;

/**
 * Damage a character takes arriving as Bleeding instead of all at once.
 *
 * WHAT IT IS FOR. The Masochist's `basic_ll_b1` The Breaking Point:
 *
 *   Dropping below 50% health converts all damage you take into Bleeding over
 *   5 seconds. The conversion lasts 3 seconds, increased by 5% per point, and
 *   cannot happen more than once every 10 seconds.
 *
 * AND IT IS WORTH FAR MORE THAN ONE NODE. Issue #985. Eleven further nodes in
 * that tree ask about status effects the character is CARRYING -- "While you are
 * Bleeding", "for each unique debuff on you", "Every debuff on you grants
 * Fervour" -- and nothing in this game put a status effect on the player at all.
 * The only damage over time applied at run time is Burn, and all four callers
 * apply it to a target. The design's other answer was enemy modifiers, and no
 * enemy modifier does anything (issues #742 and #674). This node is the tree's
 * own answer: a Masochist that bleeds ITSELF needs no enemy system, and it is
 * the only route that also unblocks Thirst for Pain, which names Bleeding.
 *
 * THREE THINGS IT IS NOT, and each was considered and rejected.
 *
 * NOT A REDUCTION. The damage is not lessened. The same total arrives, spread
 * over five seconds, which is worth something only because a character can heal
 * or leech during those five seconds. That is deliberately the same shape Path
 * of Exile's Petrified Blood has, and it is a real trade rather than a discount.
 *
 * NOT A TIMER. The window is read when it is asked about, the way the stack
 * count in `UCataclysmAbilitySystemComponent` is. A timer per character is one
 * more thing to cancel when one dies, and a window that has quietly expired
 * answers correctly with no bookkeeping at all.
 *
 * NOT APPLIED TO DAMAGE THAT IS ALREADY DAMAGE OVER TIME, and that is not
 * tidiness. The Bleeding this creates arrives as damage like anything else, so
 * converting it again would convert it for ever and no damage would ever reach
 * health. `FCataclysmHit::bIsDamageOverTime` is what tells them apart.
 */
UCLASS()
class CATACLYSM_API UCataclysmDamageConversion : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The stat saying the rule applies at all. Zero for no. */
	static const TCHAR* ActiveStat;

	/** The stat saying how many seconds one turn of it lasts. */
	static const TCHAR* WindowStat;

	/**
	 * The share of maximum health a character has to drop below.
	 *
	 * A CONSTANT AND NOT A CONDITION ON THE ROW, and a check is what settled
	 * that. The node says "DROPPING below 50% health", which is an EVENT;
	 * `health_at_or_below` means "at or below", which is a STATE.
	 * `test_a_condition_matches_the_words_of_the_node_it_is_on` refuses the
	 * second wording on this node's sentence, and it is right to: a state
	 * condition would end the conversion early if the character healed back
	 * above half, which the node does not say.
	 */
	static constexpr float HealthShare = 0.5f;

	/**
	 * How long one turn lasts before any points are spent, in seconds.
	 *
	 * THE BASE THE NODE'S `increased` ROW MULTIPLIES. It is here rather than in
	 * any table because the passive effects sheet carries values PER POINT and a
	 * base is not one, and because the class stat sheet mirrors
	 * `sim/cataclysm_sim/classes.py`, which is a statement about what makes each
	 * class feel different rather than a place for one node's timing.
	 *
	 * `ENGINE_SUPPLIED_BASES` in `tools/generate_datatables.py` names this
	 * constant as where the base comes from, so the check that refuses an
	 * increase with no base under it can see it.
	 *
	 * `UCataclysmPlayerClassStats::EngineSuppliedBases` IS WHAT PUTS IT ON A
	 * CHARACTER, and until issue #1025 nothing did. Two comments claimed a base
	 * existed, they claimed different things, and neither was true: the stat
	 * resolved to zero, and a window of zero converts nothing.
	 */
	static constexpr float BaseWindowSeconds = 3.0f;

	/** How long the Bleeding lasts, in seconds. */
	static constexpr float BleedingSeconds = 5.0f;

	/**
	 * The least time between one turn starting and the next, in seconds.
	 *
	 * MEASURED FROM THE START AND NOT FROM THE END. "Cannot happen more than
	 * once every 10 seconds" is one occurrence per ten second period, which is
	 * what that phrase says in ordinary English. At eight points the window is
	 * 4.2 seconds, so it leaves a gap of 5.8.
	 */
	static constexpr float CooldownSeconds = 10.0f;

	/**
	 * Notice that a character's health has moved, and open a window if it has
	 * just crossed below half.
	 *
	 * CALLED FROM `UCataclysmVitalAttributeSet::NotifyHealthChanged`, which
	 * already fires on EVERY write to health and whose own comment says why it
	 * exists: "A health-triggered phase begins part way down rather than at the
	 * end." Every route that lowers health goes through it -- a hit, a health
	 * cost, a debt falling due -- so the node's trigger does not have to know
	 * which one did it.
	 *
	 * THE CROSSING AND NOT THE STATE. A character sitting at 40% health does not
	 * open a new window every time it is scratched; it opens one when it goes
	 * from above half to at or below. That is what
	 * `UCataclysmAbilitySystemComponent::bWasAboveHalfHealth` remembers.
	 */
	static void NoteHealthChanged(AActor* Character);

	/**
	 * Turn damage that is about to reach health into Bleeding, if a window is
	 * open.
	 *
	 * @param Character  who is being hit
	 * @param ToHealth   what is about to be taken off health
	 * @param bIsAlreadyDamageOverTime  true for a tick of something that is
	 *                   already spread over time, which is never converted
	 * @return how much was converted, which the caller must NOT also take off
	 *         health. Zero when no window is open.
	 *
	 * THE CALLER SUBTRACTS WHAT IS LEFT, rather than this function writing
	 * health itself. One place writes health and it stays that way; a second
	 * writer would have to repeat the clamp, the death notice and the Fervour
	 * that health lost to damage generates.
	 */
	static float ConvertIfActive(AActor* Character, float ToHealth,
								 bool bIsAlreadyDamageOverTime);
};
