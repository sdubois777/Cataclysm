// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmStacks.generated.h"

class AActor;
class UCataclysmAbilitySystemComponent;

/**
 * A count of something that builds on an event and stops counting when the
 * character goes long enough without that event happening again.
 *
 * WHAT IT IS FOR. Three Masochist nodes are written this way and none of them
 * could be built without it:
 *
 *   Sanguine Momentum  a health cost paid soon after the last     3 s, up to 5
 *   Blood Offering     taking damage                              5 s, up to 5
 *   Carnage            killing an enemy while holding Fervour     8 s, up to 10
 *
 * They differ in what grants a stack, how long it lasts and how many there may
 * be, and in nothing else. Issues #1002, #1003 and #1004.
 *
 * IT NEEDS NO TIMER, WHICH IS THE WHOLE SHAPE.
 * `UCataclysmAbilitySystemComponent::DisplacementsInWindow` already does this
 * for a different count and says why: the window is applied when the count is
 * ASKED FOR rather than when it would expire, so "asking does not reset
 * anything". Nothing has to be cancelled when a character dies, and nothing runs
 * on a frame where no stack is granted and none is read.
 *
 * ONE EXPIRY FOR ALL STACKS OF A KIND, NOT ONE PER STACK. Gaining a stack
 * refreshes the whole lot. That is read off the genre rather than off the
 * design, which states no rule: Path of Exile's charges work exactly this way --
 * "gaining a charge resets the duration of all accumulated charges of the same
 * type" -- and it is much easier for a player to reason about than a queue of
 * individually expiring stacks. `docs/DECISIONS.md` carries the source.
 *
 * A SEPARATE CLASS OF STATIC FUNCTIONS, like `UCataclysmHealthDebt`,
 * `UCataclysmFervour` and `UCataclysmLeech`, and for the reason the first of
 * those gives: the rules are arithmetic on a few numbers, so they can be checked
 * by passing numbers in rather than by building a character, a world and an
 * effect spec for every case.
 *
 * THREE PLACES GRANT AND NO MORE, one per kind:
 *
 *   Sanguine Momentum  `UCataclysmSkillTemplate::PayHealthCost`, the one place
 *                      a health cost is worked out
 *   Blood Offering     `UCataclysmVitalAttributeSet::PostGameplayEffectExecute`,
 *                      the one place a resolved hit is known about
 *   Carnage            `ACataclysmEnemyCharacter::HandleDeath`, which already
 *                      reaches the player to grant experience for the kill
 */
UENUM(BlueprintType)
enum class ECataclysmStackKind : uint8
{
	/** Granted by paying a health cost soon after the last one. */
	SanguineMomentum	UMETA(DisplayName = "Sanguine Momentum"),

	/** Granted by taking damage. */
	Bloodlust			UMETA(DisplayName = "Bloodlust"),

	/** Granted by killing an enemy while holding enough of the class resource. */
	Carnage				UMETA(DisplayName = "Carnage"),

	/** How many kinds there are. Not a kind. */
	Count				UMETA(Hidden)
};

UCLASS()
class CATACLYSM_API UCataclysmStacks : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** How many kinds of stack there are, for the arrays that hold them. */
	static constexpr int32 KindCount =
		static_cast<int32>(ECataclysmStackKind::Count);

	/**
	 * How long a kind of stack lasts after the last one was granted, in seconds.
	 *
	 * EACH NUMBER IS ITS NODE'S OWN WORDS. Sanguine Momentum says "within 3
	 * seconds of the last", Blood Offering says "for 5 seconds", Carnage says
	 * "for 8 seconds". None of them is a constant this project chose.
	 */
	static float WindowSecondsFor(ECataclysmStackKind Kind);

	/**
	 * The most stacks of a kind a character may hold.
	 *
	 * ALSO ITS NODE'S OWN WORDS: "up to 5 stacks" twice and "up to 10 stacks"
	 * once. A cap of nothing would mean the count runs away, so an unknown kind
	 * answers zero and grants nothing rather than growing without bound.
	 */
	static int32 CapFor(ECataclysmStackKind Kind);

	//~ The stat names Carnivore grants, as `game/Data/PassiveEffects.csv`
	//~ spells them. They are the keys
	//~ `UCataclysmPlayerClassStats::StatToAttribute` is looked up by and the
	//~ keys `StatForSkill` is asked with, so they exist once here rather than as
	//~ literals at each use.

	/** Whether taking a hit grants a stack of Carnage. Zero for no. #1071. */
	static const TCHAR* CarnageFromDamageTakenStat;

	/** Whether Carnage has no upper limit on this character. Zero for no. */
	static const TCHAR* CarnageHasNoMaximumStat;

	/**
	 * The cap used for a character whose stacks have no maximum. Issue #1071.
	 *
	 * THE LARGEST INT32 RATHER THAN A SPECIAL CASE INSIDE `GrantStack`, and it
	 * is worth saying why calling that "no maximum" is not a lie. A stack of
	 * Carnage lasts 8 seconds and the whole count lapses when no new one arrives
	 * inside that window, so reaching this number would need two billion hits to
	 * land on one character in eight seconds. It is not a large limit; it is a
	 * limit nothing in this game can approach.
	 *
	 * IT ALSO CANNOT OVERFLOW. `GrantStack` writes `Min(Standing + 1, Cap)` and
	 * `Standing` can only arrive at this value by counting to it one at a time,
	 * so the addition is never performed on the maximum.
	 */
	static constexpr int32 NoMaximum = MAX_int32;

	/**
	 * Whether taking a hit grants this character a stack of Carnage.
	 * Issue #1071, the Masochist's Carnivore.
	 *
	 * False for every character without that capstone option, and false for any
	 * ability system with no class resource attribute set, which is every enemy.
	 */
	static bool CarnageFromDamageTaken(
		const UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * The most stacks of a kind THIS character may hold. Issue #1071.
	 *
	 * `CapFor` ABOVE IS THE DESIGN'S NUMBER AND THIS IS THIS CHARACTER'S. The
	 * two agree for everybody in the game except a Masochist holding Carnivore,
	 * whose Carnage has no maximum.
	 *
	 * ASKED AT THE MOMENT A STACK IS GRANTED AND NOWHERE ELSE, because that is
	 * where a cap is applied:
	 * `UCataclysmAbilitySystemComponent::GrantStack` stores the capped count and
	 * `Held` only reports what was stored. A character that loses the option
	 * keeps whatever it was holding until the window lapses, which takes at most
	 * the 8 seconds Carnage runs for.
	 */
	static int32 CapForOn(const UCataclysmAbilitySystemComponent* AbilitySystem,
						  ECataclysmStackKind Kind);

	/**
	 * How much of the class resource Carnage needs the killer to be holding.
	 *
	 * STRICTLY ABOVE, BECAUSE THE DESIGN WRITES "above 75 Fervour". The same
	 * boundary Grand Tithe drew in issue #983, and the same reason: the words
	 * decide it. A character sitting exactly on 75 gains nothing.
	 */
	static constexpr float CarnageClassResourceAbove = 75.0f;

	/**
	 * A health cost was paid. Grants a Sanguine Momentum stack if the previous
	 * one was recent enough. Issue #1002.
	 *
	 * CALLED BEFORE THE COMPONENT'S OWN TIMESTAMP IS UPDATED, which is what
	 * makes "within 3 seconds of THE LAST" mean what it says. Called afterwards,
	 * every payment would be nought seconds after itself and the first health
	 * cost of a fight would grant a stack.
	 *
	 * @return whether a stack was granted
	 */
	static bool NoteHealthCostPaid(UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * Damage reached this character. Grants a Bloodlust stack. Issue #1003.
	 *
	 * DAMAGE OF ANY TYPE, which is a reading of the node rather than its words.
	 * Blood Offering says "physical damage" and this game has eight damage types
	 * and no physical one; issue #1001 carries the question and the
	 * recommendation this follows.
	 *
	 * AND A STACK OF CARNAGE AS WELL, FOR A CHARACTER HOLDING CARNIVORE. Issue
	 * #1071: "Every hit you take grants a stack of Carnage." Two kinds from one
	 * event, which is the first time that has happened, and they stay two kinds:
	 * they last different lengths of time and different nodes read each.
	 *
	 * @return whether a Bloodlust stack was granted, which is always. The
	 *         Carnage stack is reported by `Held` rather than here, because a
	 *         single yes or no could not say which of the two was meant.
	 */
	static bool NoteDamageTaken(UCataclysmAbilitySystemComponent* AbilitySystem);

	/**
	 * This character killed an enemy. Grants a Carnage stack if it is holding
	 * more than `CarnageClassResourceAbove` of the class resource. Issue #1004.
	 *
	 * TAKES AN ACTOR RATHER THAN A COMPONENT, because the one caller has a pawn
	 * in hand and not an ability system. `UCataclysmHealthDebt::ClearOnKill`
	 * sits beside it in the same function and takes the same argument.
	 *
	 * @return whether a stack was granted
	 */
	static bool NoteEnemyKilled(AActor* Killer);

	/**
	 * How many stacks of a kind this character is holding right now.
	 *
	 * ZERO FOR EVERY CHARACTER THAT HAS NOT EARNED ONE, and zero once the window
	 * has passed. The two do not have to be told apart, unlike the readings a
	 * condition depends on: a bonus counting stacks is worth nothing for either,
	 * and no caller can act differently on them.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Stacks")
	static int32 Held(const UCataclysmAbilitySystemComponent* AbilitySystem,
					  ECataclysmStackKind Kind);

	/** The name a console command and a log line print for a kind. */
	static const TCHAR* NameOf(ECataclysmStackKind Kind);
};
