// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmDebuffs.generated.h"

class UAbilitySystemComponent;

/**
 * How many distinct debuffs a character is carrying, and whether one of them is
 * a particular kind.
 *
 * WHAT IT IS FOR. Five Masochist nodes ask about the harmful effects the
 * character itself is under, and none of them could be built without this:
 *
 *   Thirst for Pain         "While you are Bleeding, +2% increased Attack
 *                           Speed per point"
 *   Battle-Scarred          "+2% increased Armor per point for each unique
 *                           debuff on you"
 *   Endurance in Suffering  "For each unique debuff on you, +1% increased
 *                           damage and +0.5% increased Damage Reduction per
 *                           point"
 *   Doctrine of Pain        "You deal 4% more damage for each unique debuff on
 *                           you"
 *   Flagellant              "Every debuff on you grants 5 Fervour per second
 *                           for as long as it lasts"
 *
 * Issue #962. Four of the five want a COUNT and the fifth wants a NAMED KIND,
 * and both are the same question asked of the same place, which is why they are
 * one file rather than two.
 *
 * NOTHING COUNTED THEM BEFORE. `game/Data/StatusEffects.csv` describes what 52
 * effects ARE, and the ability system holds the effects that are running, but
 * nothing asked how many distinct harmful ones a character was under.
 *
 * IT NEEDS NO STATE AND NO TIMER, WHICH IS THE WHOLE SHAPE. A lasting effect
 * already grants its target a gameplay tag for exactly as long as it runs --
 * `MakeSingleStackTagged` in `CataclysmSkillEffects.cpp` attaches one through a
 * `UTargetTagsGameplayEffectComponent` -- so the ability system is already
 * keeping this list up to date and the count is a read of it. An effect that
 * expired a moment ago has already taken its tag off, so the answer is right
 * with nothing having run in the meantime. That is the same argument
 * `UCataclysmStacks` makes for its own count.
 *
 * WHAT COUNTS AS A DEBUFF IS AN EXPLICIT LIST AND NOT A JUDGEMENT. See
 * `DebuffRootNames` below. All three games in the genre settled this the same
 * way and none of them infers it: Path of Exile marks a debuff with a red border
 * and shows debuffs in a separate row from buffs, Last Epoch declares an
 * enumerated set of ailments, and Diablo 4 names exactly eleven crowd control
 * effects. `docs/DECISIONS.md` carries the sources.
 */
UCLASS()
class CATACLYSM_API UCataclysmDebuffs : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * The tag branches whose members are debuffs on whoever carries them.
	 *
	 * A NAMED LIST RATHER THAN A RULE, because there is no rule to write. The
	 * tag vocabulary has no mark saying "this one is bad for you": `Status.` is
	 * one flat branch holding the buffs, the debuffs and the damage over times
	 * together, so `Status.DivineAegis` and `Status.Cripple` are indistinguishable
	 * from their names. Any rule inferring harm from a tag would be guessing.
	 *
	 * `State.StunImmune` IS THE CASE THAT PROVES IT MATTERS. It is granted to
	 * the target at the same instant as `State.Stunned`, by the same call in
	 * `UCataclysmSkillEffects::ApplyStun`, and it is a PROTECTION -- it is what
	 * stops the character being stunned again immediately. A rule that counted
	 * everything a hit left behind would report one stun as two debuffs and
	 * hand every one of the five nodes double what it promises.
	 *
	 * A ROOT COUNTS ITSELF AND ITS CHILDREN. `Keyword.DoT` names the whole
	 * damage over time branch, so `Keyword.DoT.Bleed` and `Keyword.DoT.Burn` are
	 * both in without being listed, and the branch cannot grow a member this
	 * list forgets.
	 *
	 * IT IS SHORT BECAUSE LITTLE REACHES A PLAYER YET, and that is the honest
	 * position rather than an oversight. Damage over time and stunning are the
	 * only lasting harmful effects anything applies to a character today. The
	 * `Status.` effects are described in the design and applied by nothing;
	 * issue #899 is the eleven affixes that would apply one. When something
	 * does, its branch belongs here.
	 */
	static const TCHAR* const DebuffRootNames[];

	/** How many entries `DebuffRootNames` has. */
	static const int32 DebuffRootCount;

	/** The debuff roots, as tags. Any the vocabulary has lost are left out. */
	static FGameplayTagContainer DebuffRoots();

	/**
	 * How many distinct debuffs this character is carrying.
	 *
	 * DISTINCT TAGS, WHICH IS WHAT "UNIQUE" MEANS IN ALL FOUR SENTENCES. A
	 * character bleeding and burning carries two; one bleeding from two sources
	 * carries one, because every lasting effect this project applies is
	 * aggregated by target and limited to a single stack, so a second
	 * application refreshes the first rather than adding to it.
	 *
	 * THE EXPLICIT TAGS AND NOT THE IMPLIED PARENTS. An ability system holding
	 * `Keyword.DoT.Bleed` answers yes to `Keyword.DoT` as well, because the
	 * engine counts a tag against its parents too. Counting what it answers yes
	 * to would report one bleed as two debuffs and then as three the moment the
	 * branch grew a level. `GetOwnedGameplayTags` returns only what was really
	 * applied, which is one entry per effect.
	 *
	 * NO ABILITY SYSTEM MEANS NO DEBUFFS, which is zero rather than unknown.
	 * There is nothing an unknown reading would let a caller do differently: a
	 * bonus counting debuffs is worth nothing both to a character carrying none
	 * and to a caller with no character in hand. `UCataclysmStacks` draws the
	 * same distinction, and for the same reason.
	 */
	static int32 CountOn(const UAbilitySystemComponent* AbilitySystem);

	/** How many distinct debuffs this actor is carrying. */
	static int32 CountOnActor(const AActor* Actor);

	/**
	 * WHICH debuffs this character is carrying, rather than how many.
	 *
	 * `CountOn` IS THIS COUNTED, and was written first because counting was all
	 * the eleven nodes that pay per debuff needed. Issues #1057 and #1058 need
	 * the list: "a random debuff you carry" cannot be chosen from a number. The
	 * two answers must never disagree, so there is one walk of the tags and
	 * `CountOn` reads its length.
	 *
	 * EVERY RULE `CountOn` DOCUMENTS APPLIES UNCHANGED, because it is the same
	 * walk: the explicit tags and not the implied parents, one entry per effect,
	 * and an empty container for no ability system.
	 *
	 * THE ORDER IS THE ABILITY SYSTEM'S AND IS NOT PROMISED. A caller wanting one
	 * at random must choose one; a caller taking the first would get whichever
	 * the engine's tag container happens to hold first, which is stable enough to
	 * look deliberate and is not.
	 */
	static FGameplayTagContainer TagsOn(
		const UAbilitySystemComponent* AbilitySystem);

	/** Which debuffs this actor is carrying. */
	static FGameplayTagContainer TagsOnActor(const AActor* Actor);

	/**
	 * Whether these two are suffering from any of the same thing.
	 *
	 * WHAT IT IS FOR. The Masochist's Wound Channeling, issue #1061: "you deal
	 * 1% increased damage per point to enemies carrying a debuff you also
	 * carry."
	 *
	 * THE EXPLICIT TAGS ON BOTH SIDES, WHICH IS THE WHOLE CARE THIS NEEDS. A
	 * Masochist that is burning and an enemy that is bleeding both answer yes to
	 * `Keyword.DoT`, because the engine counts a tag against its parents. Asking
	 * that question would pay the node for any two harmful effects at all, which
	 * is not what "a debuff you also carry" says. `TagsOn` returns what was
	 * really applied, one entry per effect, and this compares those exactly.
	 *
	 * NEITHER CARRYING ANYTHING IS FALSE rather than vacuously true. Two
	 * characters sharing nothing share nothing.
	 */
	static bool ShareADebuff(const UAbilitySystemComponent* AbilitySystem,
							 const AActor* Other);

	/**
	 * The stat naming the attacker's increased damage against a target carrying
	 * a debuff it also carries, as a percentage. As
	 * `game/Data/PassiveEffects.csv` spells it.
	 */
	static const TCHAR* SharedDebuffDamageStat;

	/**
	 * That increase as a FRACTION, or zero when the two share nothing.
	 *
	 * A FRACTION AND NOT A PERCENTAGE, because the one caller adds it into the
	 * increases bracket, which is a sum of fractions.
	 * `UCataclysmSkillEffects::DamageAgainstTypeOf` is the existing bonus of
	 * exactly this shape -- read at the moment of a hit, decided by the target --
	 * and it answers in the same units for the same reason.
	 *
	 * IT MUST JOIN THAT BRACKET RATHER THAN BECOMING A SECOND MULTIPLIER.
	 * `UCataclysmSkillEffects::ApplyHit` says why: a finished attack damage
	 * attribute has its increases already applied and no longer visible, and a
	 * hit cannot reopen a bracket it cannot see.
	 *
	 * ASKED FOR RATHER THAN READ OFF THE ATTRIBUTE where the component is one
	 * this project made, so a later row carrying a condition or a scale is not
	 * dropped in silence. Issue #1022. The attribute is the fallback, so an
	 * ability system that recorded nothing answers what it holds.
	 */
	static float DamageAgainstSharedDebuff(
		const UAbilitySystemComponent* Source, const AActor* Target);

	/**
	 * `Keyword.DoT.Bleed`, or an invalid tag if the vocabulary has lost it.
	 *
	 * Requested by name rather than declared as a native tag, for the reason
	 * `UCataclysmSkillEffects::BurnTag` gives: a native declaration would create
	 * the tag whether or not the workbook still lists it, hiding exactly the
	 * disagreement that matters.
	 */
	static FGameplayTag BleedTag();

	/**
	 * Whether this character is Bleeding, which Thirst for Pain asks.
	 *
	 * A CHILD OF THE DAMAGE OVER TIME BRANCH, so a character that is Bleeding is
	 * also carrying damage over time and answers yes to both questions. That is
	 * the engine's own parent rule and it is the right reading: Bleeding IS
	 * damage over time.
	 */
	static bool IsBleeding(const UAbilitySystemComponent* AbilitySystem);

	/**
	 * The stat for how long a lasting harmful effect on this character runs,
	 * as a percentage where 100 is normal. As
	 * `game/Data/PassiveEffects.csv` spells it. Issue #1033.
	 */
	static const TCHAR* DurationStat;

	/** What `DurationStat` holds for a character that has none of it. */
	static constexpr float NormalDuration = 100.0f;

	/**
	 * How long an effect of this length really lasts on this character.
	 *
	 * THE DEFENDER'S STAT AND NOT THE ATTACKER'S, which is the whole point of
	 * this function existing. Every other number about a lasting effect is the
	 * attacker's -- how much it deals, how often it ticks, how long the
	 * ATTACKER makes it last -- and this is the one thing the thing being hurt
	 * decides. `dot_duration` is the attacker's side of the same question and
	 * is a different stat.
	 *
	 * TWO NODES ASK FOR IT, both in the Masochist tree and both lengthening
	 * rather than shortening: Symphony of Pain by 2% a point and Vessel of
	 * Plagues by 50%. A Masochist WANTS harmful effects on itself, because
	 * eleven nodes in that branch pay it for each one it carries.
	 *
	 * ASKED FOR RATHER THAN READ, so a future row carrying a condition works.
	 * Neither row today carries one.
	 *
	 * BOTH PATHS THAT APPLY A LASTING EFFECT CALL IT, which is what issue
	 * #1033 asked for: a build honouring one and not the other would lengthen
	 * a burn and not a stun, or the reverse, and nothing would report it.
	 *
	 * @return the duration unchanged for every character in the game without
	 *         one of those two nodes, and for any ability system with no
	 *         combat attribute set
	 */
	static float DurationOn(const UAbilitySystemComponent* Defender,
							float DurationSeconds);

	/**
	 * The stat saying the debuffs on this character stop counting down at all,
	 * as `game/Data/PassiveEffects.csv` spells it. Zero for no. Issue #1070.
	 */
	static const TCHAR* DoNotExpireStat;

	/**
	 * Whether the debuffs on this character are being held right now.
	 * Issue #1070.
	 *
	 * ASKED THROUGH THE STAT PIPELINE AND NEVER READ OFF THE ATTRIBUTE, and
	 * here that is load-bearing rather than a precaution. Ceaseless Penance's
	 * row carries `health_above 50`, and a conditional bonus is never folded
	 * into a gameplay attribute, so a plain read would answer zero for ever and
	 * the option would do nothing with nothing at run time reporting it.
	 *
	 * NOTHING HERE READS HEALTH. The health threshold is the CONDITION on the
	 * row, which the pipeline judges against the character's state when it is
	 * asked. That is why "above 50%" appears in this file nowhere: it is data.
	 *
	 * False for every character without that capstone option, false for one
	 * holding it that is at or below half health, and false for any ability
	 * system this project did not make.
	 */
	static bool DoNotExpireOn(const UAbilitySystemComponent* AbilitySystem);

	/**
	 * Hold the debuffs on this character still for one step of the clock.
	 * Issue #1070, the Masochist's Ceaseless Penance: "Debuffs on you no longer
	 * expire while you are above 50% health."
	 *
	 * WHAT IT DOES TO AN EFFECT. Each debuff's start time is pushed forward by
	 * exactly the time that just passed, so the time left on it does not move. A
	 * 20 second bleed applied at 60% health still has 20 seconds left a minute
	 * later, and dropping below half resumes it from 20.
	 *
	 * PUSHED RATHER THAN RE-APPLIED, which the project owner chose on
	 * 2026-08-28 and which `docs/DECISIONS.md` records. Re-applying resets a
	 * damage over time effect's TICK clock as well as its duration, so with a
	 * quarter second step and a one second tick every burn and bleed on the
	 * character would deal no damage at all while it was being held. It would
	 * also hand the character a full fresh duration of everything it carries at
	 * the instant it fell below half, rather than what was left.
	 *
	 * THE ENGINE CALL IS `ModifyActiveEffectStartTime`, and this is the only
	 * place in the project that changes an active effect's remaining time. It
	 * moves the start, re-arms the expiry, and leaves the tick period alone,
	 * which is why a held burn keeps hurting at its normal rate.
	 *
	 * ONLY WHAT `DebuffRoots` NAMES. A buff on the character counts down as
	 * usual, because the option says debuffs. The query matches a root against
	 * its children, so `Keyword.DoT.Bleed` is held by naming `Keyword.DoT`, the
	 * same parent rule `TagsOn` deliberately avoids and this one wants.
	 *
	 * A CORPSE IS SKIPPED, the same guard every job on the per-character step
	 * opens with: a creature is destroyed on the step after it dies, so there is
	 * a real window in which a dead one still has an ability system.
	 *
	 * @param StepSeconds how long the step that just passed was
	 * @return how many effects were held, which is zero for every character in
	 *         the game without the option and for one carrying no debuffs
	 */
	static int32 HoldStep(AActor* Character, float StepSeconds);
};
