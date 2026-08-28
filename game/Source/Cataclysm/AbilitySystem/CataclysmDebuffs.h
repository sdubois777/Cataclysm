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
};
