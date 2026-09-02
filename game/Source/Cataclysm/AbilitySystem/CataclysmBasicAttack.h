// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmBasicAttack.generated.h"

class AActor;
class UAbilitySystemComponent;
class UCataclysmAbilitySystemComponent;

/**
 * The basic attack, which fires by itself.
 *
 * THE DESIGN IS UNUSUALLY SPECIFIC ABOUT THIS ONE, so almost nothing here is a
 * choice. docs/Cataclysm_GDD_v2.md:
 *
 *   "The basic attack is on no key. It fires automatically, as the Combat System
 *    section says. Nothing the player presses triggers it."
 *
 *   "The Basic Attack is automatic, so the weapon's attack speed sets its rate."
 *
 *   "There is no button to press and no rotation to perform. It is income for
 *    being in a fight rather than a filler action."
 *
 * SO IT SWINGS ONLY WHEN THERE IS SOMETHING TO HIT. "Income for being in a
 * fight" is the sentence that decides it: a character swinging at empty air
 * between fights is a filler action performed by the game rather than by the
 * player, which is the thing that sentence rules out. It also means the mana it
 * returns is earned by fighting, since the design pays that on hit rather than
 * on swing.
 *
 * WHY IT NEEDED ISSUE #647 FIRST. The rate is the equipped weapon's attack
 * speed, and until that issue nothing ever wrote a weapon's attack speed onto
 * the character -- the attribute sat at zero for every character in the game. A
 * basic attack built before it would have read a rate of zero and never swung.
 *
 * EVERY JUDGEMENT IS A STATIC FUNCTION HERE rather than sitting inside the
 * character, for the same reason UCataclysmImpactEffect and
 * UCataclysmCombatOverlay are built that way: a world built by
 * UWorld::CreateWorld is never ticked, so no automation test can watch a timer
 * fire. What can be tested is every decision the timer makes when it does.
 */
UCLASS()
class CATACLYSM_API UCataclysmBasicAttack : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * The shortest gap between two swings, however much attack speed is stacked.
	 *
	 * A FLOOR RATHER THAN A CAP ON THE ATTRIBUTE, because the attribute is a
	 * rate and the thing that must not run away is the timer. Twenty swings a
	 * second is already far beyond anything the design's affixes reach; this
	 * exists so that a data error putting a huge number in the AttackSpeed
	 * column produces a fast character rather than a frozen one.
	 */
	static constexpr float FastestSwingSeconds = 0.05f;

	/**
	 * Seconds between one swing and the next, from a rate in swings per second.
	 *
	 * ZERO MEANS NEVER, AND THAT IS THE IMPORTANT CASE. A character holding
	 * nothing has an attack speed of zero, and so does one whose weapon states
	 * no rate. Reading that as an interval of zero would ask the timer to fire
	 * as fast as it can forever, so it is reported as zero and the caller is
	 * required to treat zero as "do not swing at all".
	 */
	static float SecondsBetweenSwings(float AttackSpeedPerSecond);

	/**
	 * Seconds between one swing and the next for a particular character.
	 * Issue #1002.
	 *
	 * ASKED FOR RATHER THAN READ OFF THE ATTRIBUTE, which is the whole reason it
	 * exists beside the arithmetic above. A gameplay attribute holds what a stat
	 * is worth with no state taken into account, because a bonus that depends on
	 * the character's state is never folded into one -- it would be stale the
	 * moment the state moved. So the Masochist's Sanguine Momentum node, whose
	 * attack speed grows with a stack count that expires, is invisible to a read
	 * of the attribute, and nothing at run time would report that.
	 *
	 * THE SAME CHANGE ISSUE #982 MADE FOR RETALIATION, for the same reason.
	 *
	 * A FUNCTION RATHER THAN A LINE INSIDE `ACataclysmPlayerCharacter::
	 * BasicAttackTick`, so a test can hand it a character and check what the
	 * rate comes to. That tick runs off a timer on a possessed pawn and nothing
	 * in the suite drives it.
	 *
	 * ZERO MEANS NEVER, the same contract the arithmetic above states, and it is
	 * the answer for a component with no combat attribute set.
	 */
	static float SecondsBetweenSwingsFor(
		const UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * Whether a character is in a state where it may swing at all.
	 *
	 * Not dead, and not stunned. The design defines a stun as the target being
	 * unable to act, and ACataclysmPlayerController already refuses skills and
	 * movement on the same test; an automatic attack that carried on through a
	 * stun would be the one thing a stunned character could still do.
	 */
	static bool MaySwing(const AActor* Character);

	/**
	 * How far this character's basic attack reaches, in centimetres.
	 *
	 * READ OFF THE GRANTED ABILITY rather than from a constant, because the
	 * reach belongs to the weapon: the design derives it as 0.9 metres plus the
	 * weapon's length past the fist, and it arrives here as the Radius in the
	 * Item Bases sheet's basic shape parameters. A Dagger and a Greataxe have
	 * different answers and neither is written in code.
	 *
	 * Returns 0 when the character has no basic attack granted, which is the
	 * Shield's case: it grants no attack damage and so has no hit to compose.
	 */
	static float ReachCmOf(const UAbilitySystemComponent* AbilitySystem);

	/**
	 * Whether anything this character would attack is inside that reach.
	 *
	 * ASKED BEFORE SWINGING RATHER THAN LEFT TO THE SKILL, so the character does
	 * not swing at nothing. The skill would find no targets and do no harm, but
	 * "income for being in a fight rather than a filler action" is the design's
	 * own description and a swing at empty air is the filler action.
	 */
	static bool SomethingInReach(const AActor* Character, float ReachCm);

	/**
	 * Fires the basic attack, if this character has one granted.
	 *
	 * @return whether an ability was actually activated. False when there is no
	 *         basic attack granted, or when the ability system refused it.
	 */
	static bool Swing(UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * The whole decision, in one call: whether this character should swing right
	 * now, given how far its basic attack reaches.
	 *
	 * SEPARATE FROM Swing SO A TEST CAN REACH IT. Activating an ability needs a
	 * world, a granted spec and an avatar; deciding whether to needs none of
	 * those, and it is where every rule above meets.
	 */
	static bool ShouldSwingNow(const AActor* Character, float ReachCm);

	// ----------------------------------------------------------------------
	// Swinging at one chosen target, because a player presses a button now
	// ----------------------------------------------------------------------

	/**
	 * Whether this one target is something the character would attack, and near
	 * enough to attack.
	 *
	 * THE NARROW FORM OF `SomethingInReach`, WHICH ASKS ABOUT ANYTHING. Issue
	 * #1187 moved the basic attack onto the left mouse button, so the question
	 * stopped being "is a fight happening" and became "is the thing the player
	 * pointed at close enough to hit".
	 *
	 * IT ASKS THE SAME THREE QUESTIONS THE SPHERE SEARCH DOES -- hostile, not
	 * already dead, within reach -- rather than a distance alone, so a click on
	 * a corpse or on an ally does not start a swing.
	 */
	static bool TargetIsInReach(const AActor* Character, const AActor* Target,
								float ReachCm);

	/**
	 * Whether enough time has passed since the last swing to swing again.
	 *
	 * THE RATE LIMIT, WHICH USED TO BE THE TIMER ITSELF. While the basic attack
	 * fired from a repeating timer, the weapon's attack speed WAS the timer's
	 * interval and nothing else had to enforce it. A button can be pressed
	 * faster than any weapon swings, so removing the timer removed the rate
	 * limit with it and this is what puts it back.
	 *
	 * `Swing` REFUSING A SECOND COPY IS NOT THIS. That check asks whether the
	 * previous activation is still running, which is about the animation. A
	 * weapon whose animation is shorter than its interval would pass it and
	 * still swing faster than its attack speed allows.
	 *
	 * AN INTERVAL OF ZERO OR LESS MEANS NO RATE, which is what a character
	 * holding nothing reads, and the answer is false: never swing.
	 */
	static bool IntervalHasPassed(float LastSwingSeconds, float NowSeconds,
								  float IntervalSeconds);
};
