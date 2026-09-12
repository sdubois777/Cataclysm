// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmDungeonModifierEffects.generated.h"

class UCataclysmAbilitySystemComponent;
class UCataclysmEquipmentComponent;

/**
 * How much of what a dungeon modifier's row says has been built.
 *
 * SHOWN TO THE PLAYER. The project owner's answer on 2026-09-11, relayed by the
 * coordinating session, was that every row stays in the draw while most of them
 * do nothing, and that the floor panel marks the ones that do nothing. This is
 * what the panel reads to mark them. Issue #41.
 */
UENUM(BlueprintType)
enum class ECataclysmModifierBuilt : uint8
{
	/** Nothing the row describes happens. Its danger score still counts. */
	NotBuilt	UMETA(DisplayName = "Not built yet"),

	/** Some of what the row describes happens and some does not. */
	Partly		UMETA(DisplayName = "Partly built"),

	/** What the row describes happens. */
	Built		UMETA(DisplayName = "Built"),
};

/**
 * What the modifiers in force on one floor do to the player's own numbers.
 *
 * PLAIN NUMBERS AND NOT STAT MODIFIERS, so a rule can be read and tested without
 * knowing how the stat pipeline stores anything. `StatModifiersFor` is the one
 * place they become modifiers.
 *
 * EVERY FIELD IS A PERCENTAGE OF A FINISHED NUMBER, and zero is exactly
 * "nothing". Four are taken off -- three maximums and every resistance --
 * and one is added to every resistance. A default-constructed one is a floor
 * whose modifiers do nothing to the player, which is what every floor was
 * before issue #41.
 *
 * THE LAST TWO ARE NOT PER-FLOOR SHARES, unlike the three above them. They
 * change while the player plays, so the dungeon game mode works them out on
 * its quarter-second beat and sets them here before applying. Issue #41,
 * slice 2.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmPlayerFloorEffects
{
	GENERATED_BODY()

	/** How much less maximum health, in percent. Starvation. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	float MaxHealthLessPercent = 0.0f;

	/** How much less maximum energy shield, in percent. Starvation. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	float MaxEnergyShieldLessPercent = 0.0f;

	/** How much less maximum mana, in percent. Dehydration. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	float MaxManaLessPercent = 0.0f;

	/**
	 * How much less of every resistance, in percent. The Nihil's Embrace.
	 * Issue #41, slice 2.
	 *
	 * IT GROWS AS THE CHARACTER WALKS and is given back by a cleanse, so it is
	 * worked out on the beat rather than once a floor.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	float ResistanceLessPercent = 0.0f;

	/**
	 * How much more of every resistance, in percent: The Nihil's Embrace's reward
	 * for defeating a high tier enemy, while it lasts. Issue #41, slice 2.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	float ResistanceMorePercent = 0.0f;

	/** Whether this takes nothing from anything and adds nothing either. */
	bool IsEmpty() const
	{
		return MaxHealthLessPercent <= 0.0f && MaxEnergyShieldLessPercent <= 0.0f
			&& MaxManaLessPercent <= 0.0f && ResistanceLessPercent <= 0.0f
			&& ResistanceMorePercent <= 0.0f;
	}
};

/**
 * What the 117 dungeon modifiers do on a floor, for the ones that do something.
 *
 * WHY IT EXISTS. Until issue #41's first modifier slice, a dungeon modifier was a
 * name and a danger score. The score reaches the enemy score, and the enemy
 * score's only reader outside the tests is the experience a kill grants, so a
 * dungeon's modifiers made its kills worth more and changed nothing a player
 * could see. Of the 117 rows of `game/Data/DungeonModifiers.csv`, only Unstable
 * Dimensions changed a floor at all, and it did so by drawing another modifier
 * that did nothing either. Issue #1558 records the score half of that.
 *
 * WHAT IS BUILT HERE, AND IT IS FOUR. The first two change the player's own
 * maximums floor by floor, which is the cheapest shape a modifier comes in: no
 * new actor, no new creature, no art. The two that issue #41's slice 2 added
 * are the first that change while the player plays rather than once a floor,
 * and both read the movement state `UCataclysmMovement` keeps.
 *
 * | Row | Its words | What happens |
 * | :-- | :-- | :-- |
 * | `Famine_Starvation` | "Each floor the players's maximum hp and energy shield are reduced by 1%. Up to 60%." | maximum health and maximum energy shield are 1% less per floor, up to 60% |
 * | `Famine_Dehydration` | "Each floor the player's maximum resource is reduced by 1%." | maximum mana is 1% less per floor, up to 60% |
 * | `War_Forced_March` | "You take stacking damage if you stand still for >3s. It forces a ""run and gun"" playstyle." | after 3 seconds without moving, one stack a second, each costing 1% of maximum health a second, up to 5 stacks, every stack cleared by moving |
 * | `Void_The_Nihil_s_Embrace` | "As you move, your resistances are slowly and permanently reduced. To cleanse the effect, you must defeat a high tier enemy. The boss's defeat will restore all of your resistances and grant a temporary buff." | 1% off every resistance for each 10 metres walked, down to 10% off; defeating a Boss or Cataclysm Boss gives every point back and grants 10% more for 20 seconds |
 *
 * `docs/DECISIONS.md` carries the judgements these needed. FOUR ARE THE FIRST
 * TWO RULES', dated 2026-09-11: that a floor's share is taken as a Less
 * multiplier on the finished maximum rather than as a negative increase; that
 * floor 1 already counts; that Dehydration stops at 60% although its row states
 * no cap; and that "maximum resource" means maximum mana.
 *
 * EIGHT MORE ARE SLICE 2'S, dated 2026-09-12, because its two rows between them
 * state one number -- Forced March's three seconds -- and nothing else. Seven of
 * the eight are numbers: what a stack costs a second, how often one is added,
 * the most stacks Forced March reaches, the walk one point of resistance costs,
 * the most The Nihil's Embrace takes, the size of the reward a cleanse grants,
 * and how long it lasts. The eighth is which rung of the rarity ladder a cleanse
 * needs, where a Boss counts and a Herald deliberately does not.
 *
 * ONLY SIX OF THE SEVEN NUMBERS ARE CONSTANTS HERE. How often a stack is added
 * is one a second, and it lives in the shape of `ForcedMarchStacksAfter` -- one
 * stack plus one for each whole second past the threshold -- rather than in a
 * constant of its own.
 *
 * A PLAIN CLASS OF STATICS OVER PLAIN STRUCTS, the shape
 * `FCataclysmDungeonFloorRules` and `UCataclysmDungeonModifierRules` already
 * use, for the reason they give: the automation tests run with `-nullrhi`, and a
 * rule that can be called with numbers typed by hand can be checked at every
 * floor from 1 to 150 without building one.
 */
UCLASS()
class CATACLYSM_API UCataclysmDungeonModifierEffects : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The row keys of the modifiers built here. */
	static const TCHAR* StarvationKey;
	static const TCHAR* DehydrationKey;
	static const TCHAR* ForcedMarchKey;
	static const TCHAR* NihilsEmbraceKey;

	/**
	 * What Starvation takes per floor, and the most it takes.
	 *
	 * BOTH ARE THE ROW'S OWN FIGURES: "reduced by 1%. Up to 60%."
	 * `tools/tests/test_dungeon_modifier_rules_are_the_rows.py` fails if the row
	 * stops saying either number.
	 */
	static constexpr float StarvationPercentPerFloor = 1.0f;
	static constexpr float StarvationMostPercent = 60.0f;

	/**
	 * What Dehydration takes per floor, and the most it takes.
	 *
	 * THE 1% IS THE ROW'S. **THE 60% IS NOT.** The row states no cap, and without
	 * one a 100-floor dungeon would take every point of mana a character has, so
	 * it stops where its sister row, Starvation, stops. That is a judgement and
	 * `docs/DECISIONS.md` says so. The Python test above fails if the row ever
	 * states a cap of its own, so the two cannot quietly disagree.
	 */
	static constexpr float DehydrationPercentPerFloor = 1.0f;
	static constexpr float DehydrationMostPercent = 60.0f;

	/**
	 * Forced March: how long a character may stand still before it is hurt, what
	 * each stack costs a second, and the most stacks it reaches.
	 *
	 * THE THREE SECONDS ARE THE ROW'S OWN FIGURE: "You take stacking damage if
	 * you stand still for >3s".
	 * `tools/tests/test_dungeon_modifier_rules_are_the_rows.py` fails if the row
	 * stops saying it.
	 *
	 * THE OTHER TWO ARE JUDGEMENTS AND THE ROW STATES NEITHER. One per cent of
	 * maximum health a second per stack, capped at five stacks, means a character
	 * that stops for ten seconds has lost about a third of its health and is in no
	 * danger once it moves, while one that stands still for half a minute dies.
	 * That is the "run and gun" the row asks for, at a danger score of 5 out of
	 * the table's range.
	 *
	 * NO GAME IN THE GENRE PUBLISHES A FIGURE FOR THIS, which is why both are
	 * judgements rather than borrowed. The nearest are a Path of Exile mechanic
	 * that kills in five seconds with no cap at all, and a World of Warcraft
	 * dungeon affix of a few per cent of maximum health a stack capped at four --
	 * which its own developers retuned twice in eleven days. **Expect these to
	 * need tuning against real play.** `docs/DECISIONS.md` records all of it.
	 *
	 * A SHARE OF MAXIMUM HEALTH RATHER THAN A FLAT NUMBER, which is what both of
	 * those do, so it means the same thing at every character level.
	 */
	static constexpr float ForcedMarchSecondsBeforeDamage = 3.0f;
	static constexpr float ForcedMarchPercentPerStackPerSecond = 1.0f;
	static constexpr int32 ForcedMarchMostStacks = 5;

	/**
	 * The Nihil's Embrace: how far the player walks for each point of resistance
	 * lost, the most it takes, and what defeating a high tier enemy gives back.
	 *
	 * ALL FOUR ARE JUDGEMENTS. The row states no number at all: "As you move, your
	 * resistances are slowly and permanently reduced. To cleanse the effect, you
	 * must defeat a high tier enemy. The boss's defeat will restore all of your
	 * resistances and grant a temporary buff." The Python test above fails if the
	 * row ever states one of its own, at which point the judgement stops being a
	 * judgement.
	 *
	 * TEN METRES A POINT IS WHAT "SLOWLY" WAS TAKEN TO MEAN: a floor's worth of
	 * walking costs a few points rather than a character's whole defence.
	 *
	 * TEN PER CENT IS THE FLOOR, and it is the one figure here with a precedent:
	 * a Diablo IV run modifier takes 10% of all resistances for a whole run.
	 * Without a limit a long floor would take every point a character has, which
	 * is why the coordinating session asked for one, as it did for the per-floor
	 * rule whose own row states no cap.
	 *
	 * THE REWARD MATCHES THE PENALTY IT CANCELS, for twenty seconds, which is the
	 * shorter of the two windows Path of Exile gives a reward for killing a rare
	 * monster. It reads as the curse lifted and a moment of grace rather than as a
	 * new power.
	 */
	static constexpr float NihilsEmbraceMetresPerResistancePercent = 10.0f;
	static constexpr float NihilsEmbraceMostResistancePercent = 10.0f;
	static constexpr float NihilsEmbraceRewardResistancePercent = 10.0f;
	static constexpr float NihilsEmbraceRewardSeconds = 20.0f;

	/**
	 * How much of what this row says has been built.
	 *
	 * NOT BUILT FOR EVERY KEY THIS FILE DOES NOT NAME, a key that is not a row
	 * included, because saying "built" of something that does nothing is the
	 * mistake the floor panel exists to stop a player making.
	 */
	static ECataclysmModifierBuilt BuiltStateOf(FName RowKey);

	/**
	 * Every row key that has a rule anywhere in the game, built or partly built.
	 *
	 * UNSTABLE DIMENSIONS IS HERE AND ITS RULE IS NOT IN THIS FILE. It lives in
	 * `FCataclysmDungeonFloorRules::ModifiersFor`, because it changes a floor's
	 * modifier list rather than the player, and it is only partly built: it draws
	 * another dungeon modifier where its row asks for "a new, random modifier to
	 * all enemies". Listing it here is what lets the panel say so.
	 */
	static TArray<FName> KeysWithARule();

	/**
	 * The share of a maximum a per-floor rule has taken by this floor.
	 *
	 * FLOOR 1 ALREADY COUNTS. "Each floor ... reduced by 1%" names every floor,
	 * the first included, so floor N carries N times the per-floor share. A
	 * judgement, recorded with the others.
	 *
	 * A HORDE DUNGEON TAKES IT PER WAVE, because a Horde dungeon's floors are its
	 * waves: floor 12 of one is its twelfth wave. The project owner chose that on
	 * 2026-09-11, answering question 4 of the modifier plan; the coordinating
	 * session relayed it.
	 *
	 * @param PercentPerFloor what one floor takes, in percent
	 * @param MostPercent     the most it takes, however deep
	 * @param FloorNumber     counted from 1. Zero or below takes nothing
	 */
	static float ShareTakenOnFloor(float PercentPerFloor, float MostPercent,
								   int32 FloorNumber);

	/**
	 * How many stacks of Forced March's damage a character has. Issue #41.
	 *
	 * NONE UNTIL THE ROW'S THREE SECONDS HAVE PASSED, then one a second, capped.
	 * Moving clears them, which needs no call of its own: the wait asked about is
	 * the seconds since the character last moved, and moving puts that back to
	 * nothing.
	 *
	 * @param SecondsStoodStill seconds since the character last moved. A negative
	 *        wait is "no character to read" and takes nothing, the same answer the
	 *        movement conditions give
	 */
	static int32 ForcedMarchStacksAfter(float SecondsStoodStill);

	/**
	 * What that many stacks cost a second, as a share of maximum health.
	 * Issue #41.
	 */
	static float ForcedMarchSharePerSecond(int32 Stacks);

	/**
	 * How much of every resistance The Nihil's Embrace has taken, in percent.
	 * Issue #41.
	 *
	 * WHOLE POINTS, COUNTED DOWN, because a resistance is shown to the player as a
	 * whole number and a rule that moves a number they are watching should move it
	 * in steps they can see.
	 *
	 * @param MetresWalked how far the character has walked since the last cleanse.
	 *        Nothing or less takes nothing
	 */
	static float NihilsEmbraceResistanceLost(float MetresWalked);

	/**
	 * What the modifiers in force on a floor do to the player.
	 *
	 * @param FloorModifiers the row keys in force on this floor, which is
	 *                       `FCataclysmFloorBrief::Modifiers` -- the FLOOR's list
	 *                       and not the dungeon's, so a Volatile dungeon that
	 *                       re-draws Starvation onto floor 9 starves the player
	 *                       on floor 9 and not on floor 8
	 * @param FloorNumber    counted from 1
	 */
	static FCataclysmPlayerFloorEffects PlayerEffectsFor(
		const TArray<FName>& FloorModifiers, int32 FloorNumber);

	/**
	 * The same effects as the stat pipeline's modifiers, keyed by stat name.
	 *
	 * LESS MULTIPLIERS ON THE FINISHED MAXIMUM, from `ECataclysmModifierSource::
	 * DungeonRule`. A 10% Starvation is a More of -10 on `max_health`, so it takes
	 * a tenth of whatever the character's gear and passive tree built. A negative
	 * increase would instead take ten points out of the sum of increases, and a
	 * character carrying +200% increased health would then lose about 3% rather
	 * than the 10% the row states. `docs/DECISIONS.md` has the genre precedent.
	 *
	 * NOTHING AT ALL FOR AN EMPTY EFFECT, rather than a modifier of zero, so a
	 * floor with no such modifiers leaves the stat line exactly as it was.
	 */
	static TMap<FName, TArray<FCataclysmStatModifier>> StatModifiersFor(
		const FCataclysmPlayerFloorEffects& Effects);

	/**
	 * Puts these effects on a character and works its stats out again.
	 *
	 * HELD ON THE ABILITY SYSTEM AND READ BY THE EQUIPMENT COMPONENT'S REFRESH,
	 * which is the one place every source of a character's stats is gathered.
	 * Every later refresh -- a helmet changed, a level gained -- therefore keeps
	 * them rather than dropping them, and nothing here writes an attribute
	 * directly.
	 *
	 * THE POOLS ARE LEFT WHERE THEY ARE, which is the refresh's own default. A
	 * lower maximum clamps what is above it and heals nobody; a higher one, on
	 * leaving, restores nothing that was lost.
	 *
	 * @return whether the character's stats were worked out again. False with no
	 *         ability system, and with no equipment component, in which case the
	 *         effects are still held and the next refresh applies them
	 */
	static bool ApplyToCharacter(const FCataclysmPlayerFloorEffects& Effects,
								 UCataclysmAbilitySystemComponent* AbilitySystem,
								 UCataclysmEquipmentComponent* Equipment);

	/**
	 * What these effects take, in words, for the log.
	 *
	 * EMPTY FOR AN EMPTY EFFECT, so a caller can leave the clause out.
	 */
	static FString Describe(const FCataclysmPlayerFloorEffects& Effects);
};
