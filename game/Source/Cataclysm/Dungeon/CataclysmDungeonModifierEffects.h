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
 * THE FIELDS BELOW THE FIRST THREE ARE NOT PER-FLOOR SHARES. The first three
 * are worked out once when a floor is entered; the rest change while the
 * player plays, so the dungeon game mode works them out on its
 * quarter-second beat and sets them here before applying. Issue #41,
 * slice 2.
 *
 * SAID THAT WAY RATHER THAN AS A COUNT, because it used to read "THE LAST
 * TWO" and there are now more than two. Issue #1760. A comment that counts
 * the members beneath it becomes wrong the next time one is added, and
 * nothing reports it.
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

	/**
	 * How many percentage points less of each healing amount arrives. Death's
	 * Embrace. Issue #41, slice 5.
	 *
	 * THE AMOUNT AND NOT THE RATE, which is what makes it reach everything. It
	 * writes `UCataclysmVitalAttributeSet::HealingReceivedReduction`, whose own
	 * comment carries the scope: health regeneration, life leech and direct
	 * healing alike, health only. A rule wanting the RATES alone -- Withered
	 * Ground says "recovery (regen/leech)" -- wants a Less multiplier on
	 * `health_regen` and `mana_regen` instead, and needs nothing here.
	 *
	 * IT GROWS WHILE THE PLAYER STAYS ON THE FLOOR and is gone when they take
	 * the stairs, so it is worked out on the beat rather than once a floor.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	float HealingReceivedLessPercent = 0.0f;

	/**
	 * How much slower the character moves, in percent. Singularity Wells.
	 * Issues #1605 and #41.
	 *
	 * ON AND OFF AS THE PLAYER WALKS IN AND OUT OF A WELL, so it is worked out on
	 * the beat rather than once a floor.
	 *
	 * IT SAID "AND IT IS THE ONLY FIELD HERE THAT CAN GO BACK TO NOTHING WITHOUT
	 * THE FLOOR CHANGING", AND THAT WAS ALREADY FALSE BEFORE THE CHANGE THAT
	 * CORRECTED IT. Three other fields say so in their own comments:
	 * `RecoveryLessPercent` is "ON AND OFF AS THE PLAYER WALKS IN AND OUT OF A
	 * PATCH", `GraspMovementLessPercent` is "ON AND OFF AS A GRAB TAKES HOLD AND
	 * RELEASES", and `SkillsLockedValue` is "ON AND OFF ON A CLOCK". The two
	 * mushroom fields make five. Corrected while building
	 * `Pestilence_Fungal_Overgrowth`, which is why it is in that change rather
	 * than one of its own. Issues #1820 and #41.
	 *
	 * A MULTIPLIER ON `movement_speed` AND DELIBERATELY NOT A STATUS EFFECT ROW.
	 * `UCataclysmSkillEffects::ApplyNamedEffect` resolves what to move from the
	 * `MovesStat` column of game/Data/StatusEffects.csv and then SUBTRACTS a flat
	 * value clamped against the attribute's own current value. A player's
	 * `movement_speed` is 4.0 metres a second, so a magnitude of 40 would clamp
	 * to 4.0 and leave them standing still. That path is built for resistances,
	 * where subtracting points is the right shape, and a speed is not that.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	float MovementSpeedLessPercent = 0.0f;

	/**
	 * How much less health and mana the character gets back, in percent.
	 * Withered Ground. Issue #41.
	 *
	 * ON AND OFF AS THE PLAYER WALKS IN AND OUT OF A PATCH, like the field
	 * above it and unlike the three per-floor shares. The two are the only
	 * fields here that report where the player is STANDING rather than what
	 * the floor is.
	 *
	 * ONE FIELD FOR FOUR STATS, because the row states one figure for all of
	 * them -- "your Health and Mana recovery (regen/leech) is reduced by
	 * 80%". `StatModifiersFor` turns it into four Less multipliers, on
	 * `health_regen`, `mana_regen`, `life_leech` and `mana_leech`.
	 *
	 * TWO OF THOSE FOUR ARE WORTH NOTHING TO ALMOST EVERY CHARACTER TODAY,
	 * which is worth knowing before somebody reports it as a bug. A Less
	 * multiplies, and a multiplier on a base of zero is zero.
	 * `game/Data/ClassStats.csv` gives `health_regen` and `mana_regen` a
	 * Default row, so every class has them; it gives `life_leech` to the
	 * Ravager alone and gives NO class any `mana_leech`. So the regeneration
	 * half of this reaches everyone and the leech half reaches a Ravager and
	 * anyone whose gear grants leech.
	 *
	 * ALL FOUR ARE WRITTEN ANYWAY, AND THAT IS THE DECISION RATHER THAN AN
	 * OVERSIGHT. The row names leech, the reduction is correct for the
	 * characters that carry it, and it becomes correct for the rest the day a
	 * class line or an affix grants leech -- with no change here.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	float RecoveryLessPercent = 0.0f;

	/**
	 * How much less maximum health and maximum mana the stacks of Wasting
	 * Sickness take. Issues #1786 and #41.
	 *
	 * TWO FIELDS RATHER THAN THE PER-FLOOR PAIR ABOVE, AND THAT IS THE WHOLE
	 * POINT OF THEM. Starvation writes `MaxHealthLessPercent` and Dehydration
	 * writes `MaxManaLessPercent`, once per floor, inside `PlayerEffectsFor`.
	 * This rule's reduction is driven by stacks and is written on the beat with
	 * plain assignment ON TOP of what that function produced, so sharing either
	 * field would silently erase the other rule's share on a floor carrying
	 * both, and only on such a floor. Issue #1765 describes that fault and
	 * Withered Ground's `RecoveryLessPercent` took a new field for the same
	 * reason.
	 *
	 * SEPARATE FIELDS ALSO COMPOSE THE WAY THIS GAME COMPOSES. `StatModifiersFor`
	 * turns each into its own Less multiplier, and
	 * `UCataclysmStatPipeline` multiplies each source on its own rather than
	 * summing them first. So Starvation at 10% and Wasting Sickness at 15% leave
	 * a character with 0.9 x 0.85 of their maximum, not 0.75 of it.
	 *
	 * NAMED FOR THEIR SOURCE WHERE EVERY OTHER FIELD HERE IS NAMED FOR ITS
	 * EFFECT, because two fields that take a share of the same stat cannot both
	 * be called after the stat.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	float SicknessMaxHealthLessPercent = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	float SicknessMaxManaLessPercent = 0.0f;

	/**
	 * How much slower a grabbed character moves, in percent. Grasping Tentacles.
	 * Issues #1786 and #41.
	 *
	 * NOT `MovementSpeedLessPercent`, THOUGH IT MOVES THE SAME STAT, and the
	 * reason is the one `SicknessMaxHealthLessPercent` above gives. Singularity
	 * Wells writes that field, both rows are Void, and a floor can carry both --
	 * at which point a shared field means whichever rule wrote second erases the
	 * first. Issue #1765 names that fault; two fields become two Less
	 * multipliers, which is how every other pair in this game composes.
	 *
	 * ON AND OFF AS A GRAB TAKES HOLD AND RELEASES, so it is worked out on the
	 * beat rather than once a floor.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	float GraspMovementLessPercent = 0.0f;

	/**
	 * How much faster the player moves while standing on a mushroom that helps.
	 * Fungal Overgrowth. Issues #1820 and #41.
	 *
	 * THE ONLY FIELD HERE THAT RAISES A STAT BESIDES `ResistanceMorePercent`,
	 * and that one is the precedent this copies rather than a thing this beats.
	 * The Nihil's Embrace has given back more resistance than a character
	 * started with since issue #41's second slice, through the same helper with
	 * a positive value, and `Describe` has printed it beside the "less" version
	 * for as long.
	 *
	 * ITS OWN FIELD AND NOT A NEGATIVE `MovementSpeedLessPercent`, which is the
	 * rule `SicknessMaxHealthLessPercent` and `GraspMovementLessPercent` above
	 * both follow. A floor can carry a Singularity Well, a tentacle and a
	 * mushroom at once; a shared field means whichever rule wrote second erased
	 * the first, which is issue #1765. As separate entries the pipeline
	 * multiplies each on its own.
	 *
	 * ON AND OFF AS THE PLAYER WALKS ON AND OFF A MUSHROOM, like the three
	 * fields around it that report where the player is STANDING rather than what
	 * the floor is.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	float MushroomSpeedMorePercent = 0.0f;

	/**
	 * How much slower the player moves while standing on a mushroom that hurts.
	 * Fungal Overgrowth. Issues #1820 and #41.
	 *
	 * ITS OWN FIELD FOR THE REASON THE FIELD ABOVE GIVES, and separate from it
	 * for one more: a player standing where a helping and a hurting mushroom
	 * overlap is under both, and one field could hold only the sum of two
	 * figures neither of which the row states.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	float MushroomSpeedLessPercent = 0.0f;

	/**
	 * Whether the player's skills are locked, and by how much. Edict of Silence.
	 * Issues #1786 and #41.
	 *
	 * NOT A PERCENTAGE, WHICH MAKES IT THE ONLY FIELD HERE THAT IS NOT. The
	 * struct's own comment says every field is a percentage of a finished number;
	 * this one is the VALUE of `skill_locked`, and everything that reads that
	 * stat asks only whether it is above zero. One is what above zero is written
	 * as. Said here rather than left to be discovered, because a reader who
	 * assumed the pattern would look for a share of something.
	 *
	 * ON AND OFF ON A CLOCK, so it is worked out on the beat rather than once a
	 * floor -- and unlike every other beat-driven field here, its clock is not
	 * reset by taking the stairs. See `ACataclysmDungeonGameMode`.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	float SkillsLockedValue = 0.0f;

	/** Whether this takes nothing from anything and adds nothing either. */
	bool IsEmpty() const
	{
		return MaxHealthLessPercent <= 0.0f && MaxEnergyShieldLessPercent <= 0.0f
			&& MaxManaLessPercent <= 0.0f && ResistanceLessPercent <= 0.0f
			&& ResistanceMorePercent <= 0.0f
			&& HealingReceivedLessPercent <= 0.0f
			&& MovementSpeedLessPercent <= 0.0f
			&& RecoveryLessPercent <= 0.0f
			&& SicknessMaxHealthLessPercent <= 0.0f
			&& SicknessMaxManaLessPercent <= 0.0f
			&& GraspMovementLessPercent <= 0.0f
			&& MushroomSpeedMorePercent <= 0.0f
			&& MushroomSpeedLessPercent <= 0.0f
			&& SkillsLockedValue <= 0.0f;
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
 * WHAT IS BUILT HERE. The two per-floor rules change the player's own maximums
 * floor by floor, which is the cheapest shape a modifier comes in: no new
 * actor, no new creature, no art. The two that issue #41's slice 2 added are
 * the first that change while the player plays rather than once a floor, and
 * both read the movement state `UCataclysmMovement` keeps. Slice 5's Death's
 * Embrace is the first to change what a player's healing is worth rather than
 * what their bars hold.
 *
 * NAMED RATHER THAN COUNTED, AND THE COUNT IS EXACTLY WHAT WENT WRONG. This
 * heading read "AND IT IS FIVE" from the commit that made it five until the one
 * that added Mortal Decay, and three rules landed in between without moving it.
 * Read the table's rows; do not write their number here again. Issue #1786.
 *
 * DEATH'S EMBRACE IS THE ONE THAT USES THE FLAT BUCKET. A rule taking a share
 * of a stat with a real base can say what it means with a Less multiplier;
 * `healing_received_reduction` is zero for every class, and a multiplier on zero
 * is zero however large it is.
 *
 * | Row | Its words | What happens |
 * | :-- | :-- | :-- |
 * | `Famine_Starvation` | "Each floor the players's maximum hp and energy shield are reduced by 1%. Up to 60%." | maximum health and maximum energy shield are 1% less per floor, up to 60% |
 * | `Famine_Dehydration` | "Each floor the player's maximum resource is reduced by 1%." | maximum mana is 1% less per floor, up to 60% |
 * | `War_Forced_March` | "You take stacking damage if you stand still for >3s. It forces a ""run and gun"" playstyle." | after 3 seconds without moving, one stack a second, each costing 1% of maximum health a second, up to 5 stacks, every stack cleared by moving |
 * | `Void_The_Nihil_s_Embrace` | "As you move, your resistances are slowly and permanently reduced. To cleanse the effect, you must defeat a high tier enemy. The boss's defeat will restore all of your resistances and grant a temporary buff." | 1% off every resistance for each 10 metres walked, down to 10% off; defeating a Boss or Cataclysm Boss gives every point back and grants 10% more for 20 seconds |
 * | `Death_Death_s_Embrace` | "Players periodically gain stacks of a debuff called ""Embrace of Death,"" which reduces healing received. Stacks reset when entering a new floor." | one stack every 10 seconds spent on the floor, each taking 10 percentage points off every amount of health restored, up to 5 stacks; the stairs clear them |
 * | `Celestial_Edict_of_Silence` | "Every 90 seconds, a divine silence sweeps the dungeon for 15 seconds, preventing all skill usage. Only basic attacks function during this period." | every 90 seconds of play on a floor carrying the row, every skill but the basic attack is refused for 15 seconds; the clock carries across the stairs |
 * | `Void_Grasping_Tentacles` | "Void tentacles appear all over the dungeon. The player will have to be careful of getting too close or they might be grabbed, restricting their movement." | a tentacle appears near the player every 8 seconds, up to 5 on the floor; each beat spent within 3 metres of one has a 5% chance to be grabbed, and a grab takes 99% of the character's speed for 1.5 seconds before releasing that tentacle for 5 |
 * | `Famine_Wasting_Sickness` | "Enemies have a chance to inflict a stacking debuff that reduces your max HP and max mana. This debuff is permanent for the duration of the dungeon and can only be removed by defeating a floor boss." | each landed enemy blow has a 10% chance to add a stack, up to 5, and each stack takes 3% off maximum health and maximum mana; a boss's death on the floor clears them, and so does the player's own death |
 * | `Death_Mortal_Decay` | "The Death cataclysm introduces an affliction of mortal decay, gradually sapping the player's life force as they progress through the dungeon. To counter this, the player must give death his due souls by reaping enemies to temporarily slow the effect of the affliction." | health drains by 0.1% of the maximum a second for each floor of depth, up to 1% a second; a creature the player kills halves that for 5 seconds |
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
 * THE EDICT OF SILENCE ADDED NONE, dated 2026-09-14, AND THAT IS THE POINT OF
 * ITS ENTRY. It is the only row of the four built in this pass to state its own
 * numbers, so its entry records a reading and two rulings instead: what "sweeps
 * the dungeon" was taken to mean and why that reading has an expiry, and why its
 * clock alone survives the stairs. Issue #1786.
 *
 * FIVE MORE ARE GRASPING TENTACLES', dated 2026-09-14, and its row states no
 * number either: how many a floor carries, the chance a beat inside a reach is
 * grabbed, how long a grab holds, how long before the same tentacle may grab
 * again, and how much speed a grab takes. Three further figures it uses are
 * BORROWED rather than chosen -- the reach, the appearance distance and the
 * cadence -- and that entry says which is which. It also records the two
 * readings of "grabbed" and why a third shape was built instead. Issue #1786.
 *
 * THREE MORE ARE WASTING SICKNESS'S, dated 2026-09-13, and its row states no
 * number either: the chance a landed blow inflicts a stack, what one stack takes
 * off both maximums, and the most stacks. That entry also records why stacks are
 * allowed at all for a debuff an enemy puts on the player, and which of the two
 * readings of "a floor boss" was taken. Issue #1786.
 *
 * FOUR MORE ARE MORTAL DECAY'S, dated 2026-09-13, and its row states no number
 * either: how much faster the decay runs per floor of depth, the fastest it
 * runs, how much a kill takes off it, and how long that lasts. Issue #1786.
 *
 * THREE MORE ARE SLICE 5'S, dated 2026-09-12, and Death's Embrace's row states
 * no number either: what one stack takes off healing, the most stacks it
 * reaches, and how long a stack takes to arrive. All three are constants here.
 * The first two have a published range to sit inside -- Path of Exile's map
 * modifiers run 10% to 60% less recovery -- and the third has no precedent in
 * any game found.
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

	/**
	 * The row that rains fireballs and leaves burning ground. Issues #1605, #41.
	 *
	 * `Partly`, AND THE MISSING HALF IS THE FIREBALL. The burning ground is
	 * built: a patch falls near the player on a cadence, carries the row's own
	 * Cataclysm type so one of the player's eight resistances meets it, and burns
	 * for the ten seconds the row states. Nothing draws a fireball falling into
	 * it, and the row says fireballs rain, so `BuiltStateOf` answers `Partly`
	 * and the floor panel tells the player so. Issue #1699.
	 *
	 * "IN COMBAT ZONES" IS READ AS "NEAR THE PLAYER", because the game has no
	 * combat-zone concept to bind to. `ECataclysmFloorLayout::Arena` is a floor's
	 * whole shape rather than a region inside one, and nothing tracks where
	 * fighting is happening. `docs/DECISIONS.md` records that as a judgement.
	 *
	 * AN EARLIER VERSION OF THIS COMMENT SAID THE KEY WAS DELIBERATELY ABSENT
	 * FROM `KeysWithARule`, and that was true while the rule was unwritten. It is
	 * in that list now, and the two move together: `EveryRuleNamesARowOfTheTable`
	 * walks the list and asks the state, `EveryRowWithSomethingBuiltIsInTheRuleList`
	 * walks the table and asks the list, so a half-done edit fails.
	 */
	static const TCHAR* InfernalRainKey;
	static const TCHAR* NihilsEmbraceKey;
	static const TCHAR* DeathsEmbraceKey;
	static const TCHAR* FieldMedicKey;

	/**
	 * Mortal Decay: "The Death cataclysm introduces an affliction of mortal
	 * decay, gradually sapping the player's life force as they progress through
	 * the dungeon. To counter this, the player must give death his due souls by
	 * reaping enemies to temporarily slow the effect of the affliction."
	 * Issues #1786 and #41.
	 *
	 * "AS THEY PROGRESS THROUGH THE DUNGEON" IS THE FLOOR NUMBER AND NOT THE
	 * WALK, AND THAT IS A READING OF A STANDING RULE RATHER THAN A NEW
	 * JUDGEMENT. `CLAUDE.md`: "Depth and reward are the same axis. Depth and
	 * time are not, once a player has invested in separating them ... Resolve
	 * timers scale with depth and never with the walk time, for that reason."
	 * `FCataclysmDungeon::Floors` is the depth and `FCataclysmDungeon::WalkDays`
	 * is the walk cost. A decay keyed to the distance walked would let a player
	 * who had bought faster walking take less of it at the same depth, which
	 * inverts what the row is for.
	 *
	 * THE SECOND RULE HERE THAT TAKES HEALTH RATHER THAN MOVING A STAT, after
	 * Forced March. So it needs no field on `FCataclysmPlayerFloorEffects` and
	 * appears in neither `StatModifiersFor` nor `Describe`.
	 */
	static const TCHAR* MortalDecayKey;

	/**
	 * Wasting Sickness: "Enemies have a chance to inflict a stacking debuff that
	 * reduces your max HP and max mana. This debuff is permanent for the
	 * duration of the dungeon and can only be removed by defeating a floor
	 * boss." Issues #1786 and #41.
	 *
	 * THE FIRST RULE IN THIS FILE THAT READS A BLOW LANDING ON THE PLAYER.
	 * `UCataclysmCombatEvents::OnHit` has announced every blow since issue
	 * #41's slice 4 and nothing outside the tests listened to it; this row is
	 * its first production listener. The announcement was not built here.
	 *
	 * IT HAS TWO REMOVAL ROUTES AND THE ROW STATES ONE. The row names the floor
	 * boss. `docs/DECISIONS.md` of 2026-09-10 adds the other: the project owner
	 * ruled that anything lasting only for a dungeon ends at the player's death,
	 * "since in the real game that dungeon would resolve on death and you
	 * wouldn't respawn in it", and named this row as one of the five it covers --
	 * under its old name, `Famine_Withering_Touch`, which the dungeon side
	 * stopped using on 2026-09-13.
	 *
	 * "A FLOOR BOSS" IS ANY BOSS ON THE FLOOR AND NOT THE ONE AT THE EXIT, which
	 * is a judgement. The row's article is indefinite, the same line The Nihil's
	 * Embrace's cleanse already draws, and nothing in the game marks the creature
	 * placed at a floor's exit as that floor's boss: `FCataclysmFloorBrief::
	 * bBossAtTheExit` is a fact about the FLOOR, and the Gatekeeper placed there
	 * by `FCataclysmFloorPopulation` carries no mark saying so.
	 */
	static const TCHAR* WastingSicknessKey;

	/**
	 * Grasping Tentacles: "Void tentacles appear all over the dungeon. The player
	 * will have to be careful of getting too close or they might be grabbed,
	 * restricting their movement." Issues #1786 and #41.
	 *
	 * A GRAB IS AN EVENT AND NOT A PLACE THE PLAYER IS STANDING, which is the
	 * ruling this rule was built to. "Grabbed" has two plain readings and neither
	 * is clean: rooted in place fits the word and nothing in the game implements
	 * a root, while slowed-while-inside-a-reach is entirely buildable and would
	 * make this row a near-duplicate of `Void_Singularity_Wells`, whose own row
	 * already says "slowing movement by 40%". So a grab takes hold on a chance,
	 * lasts a few seconds, RELEASES, and that tentacle cannot grab again for a
	 * while. The coordinating session ruled on 2026-09-14; `docs/DECISIONS.md`
	 * carries both readings.
	 *
	 * NINETY-NINE PER CENT AND NOT A NEW STATE. `UCataclysmStatPipeline::
	 * LessMultiplierFloor` is -99, so the strongest single reduction the pipeline
	 * allows takes a 4.0 metre-a-second character to 0.04 -- held in place, while
	 * still able to attack. That difference is exactly what separates "restricting
	 * their movement" from `Debuff_Stun`, which stops the target acting at all.
	 *
	 * AND DELIBERATELY NOT THE STATUS EFFECT PATH. `UCataclysmSkillEffects::
	 * ApplyNamedEffect` SUBTRACTS a flat value clamped against the attribute's own
	 * value, so a magnitude against a speed of 4.0 leaves a character standing
	 * still with nothing to lift it. `FCataclysmPlayerFloorEffects::
	 * MovementSpeedLessPercent` records that for Singularity Wells, and it is why
	 * `Debuff_Cripple` is the wrong instrument here as well.
	 *
	 * THE SPAWN NEEDED NO NEW CODE, AND ISSUE #1786 SAYS OTHERWISE. That issue
	 * reports this row as needing one new function because `ACataclysmTerrain`
	 * has no floor-lasting spawn. True of that class -- its kinds are None, Pit
	 * and Wall -- and beside the point: `ACataclysmGroundZone::SpawnForTheFloor`
	 * does exactly this and already has two production users, Singularity Wells
	 * and Withered Ground.
	 */
	static const TCHAR* GraspingTentaclesKey;

	/**
	 * Edict of Silence: "Every 90 seconds, a divine silence sweeps the dungeon
	 * for 15 seconds, preventing all skill usage. Only basic attacks function
	 * during this period." Issues #1786 and #41.
	 *
	 * THE ONLY ROW OF THE FOUR BUILT IN THIS PASS THAT STATES ITS OWN NUMBERS.
	 * Ninety and fifteen are the row's, so neither is a judgement, and
	 * `tools/tests/test_dungeon_modifier_rules_are_the_rows.py` fails if the row
	 * stops saying either.
	 *
	 * THE MECHANISM IT NEEDS WAS ALREADY BUILT, AND ISSUE #1786 SAYS OTHERWISE.
	 * That issue flagged this row rather than scheduling it, for needing "a
	 * floor-wide timed on and off cycle that no existing rule has". Both halves
	 * of such a cycle exist and are each used three times in this file: a cadence
	 * counted on the beat, as Infernal Rain, Singularity Wells and Grasping
	 * Tentacles do, and a world-time stamp that expires, as The Nihil's Embrace's
	 * reward, Mortal Decay's slow and a tentacle's grab do. This row is the two
	 * together.
	 *
	 * AND THE LOCK ITSELF NEEDED NOTHING. `UCataclysmSkillTemplate::
	 * CanActivateAbility` already refuses every skill outside the Basic Attack
	 * slot while `UCataclysmSkillSlots::LockedStat` is above zero, and it skips
	 * the check for that slot UNCONDITIONALLY. The row's second sentence -- "Only
	 * basic attacks function during this period" -- is that exemption, already
	 * written.
	 *
	 * UNSCOPED, WHICH IS WHAT "ALL SKILL USAGE" MEANS AND WHAT SEPARATES THIS
	 * FROM THE TWO ENCHANTMENTS THAT USE THE SAME STAT. The lock is read through
	 * `StatForSkill` with the skill's own tags, so a value carrying
	 * `RequiredTags` reaches only skills that match. The two enchantment rows in
	 * `game/Data/EnchantmentEffects.csv` are scoped to one slot each, under a
	 * condition the player controls; this one carries no tags and no condition.
	 *
	 * "SWEEPS THE DUNGEON" IS BUILT AS THE FLOOR THE PLAYER STANDS ON, AND THAT
	 * READING HAS A STATED EXPIRY RATHER THAN BEING A JUDGEMENT THAT STANDS FOR
	 * EVER. Only one floor exists at a time -- a floor is built on arrival and
	 * `UCataclysmFloorContents::ClearTheFloor` empties it on leaving -- so "every
	 * floor at once" and "this floor" are indistinguishable today, and the second
	 * costs no state nothing can observe. `docs/DECISIONS.md` records that a
	 * change which keeps floors alive has to revisit it.
	 *
	 * THE CLOCK IS THE ONE PLACE THE TWO READINGS DIFFER IN PLAY, AND IT CARRIES
	 * ACROSS THE STAIRS FOR THAT REASON. Every other beat-driven rule here
	 * forgets its clock when the floor changes. This one does not: a player who
	 * descended every eighty seconds would otherwise never be silenced at all,
	 * and the row says the silence sweeps the dungeon rather than the floor.
	 */
	static const TCHAR* EdictOfSilenceKey;

	/**
	 * Artillery Strike: "Every 30 seconds, a massive red circle appears on the
	 * ground. A powerful artillery strike will land in that circle, dealing
	 * massive damage to everything inside. Enemies and players can be hit,
	 * creating a strategic element of using the enemy's own weapons against
	 * them." Issues #1820 and #41.
	 *
	 * THE ROW STATES ONE NUMBER AND THIS RULE CHOOSES THREE. Thirty seconds is
	 * the row's. The warning, the radius and the damage are not in it; each is
	 * recorded as a judgement in `docs/DECISIONS.md` with what it was derived
	 * from.
	 *
	 * "ENEMIES AND PLAYERS CAN BE HIT" IS THE ONLY CLAUSE THAT NEEDED ANYTHING
	 * UNUSUAL, and what it needed already existed:
	 * `UCataclysmTargeting::FindEveryoneInLine` finds whatever is standing in a
	 * place rather than only the other side. `ACataclysmGroundZone` chooses
	 * between that and `FindEnemiesInLine` on its own `bBurnsEveryone`, and this
	 * rule asks the same question directly, because the circle it places deals
	 * no damage of its own and a flag on a harmless zone would decide nothing.
	 */
	static const TCHAR* ArtilleryStrikeKey;

	/**
	 * Hallowed Groundfall: "Angelic artillery bombards random areas every 30
	 * seconds, leaving consecrated craters that burn players and empower
	 * enemies." Issues #1820 and #41.
	 *
	 * TWO THINGS IN ONE ROW AND BOTH ARE BUILT. The craters are burning ground,
	 * which `Demonic_Infernal_Rain` already places; the empowerment is
	 * `Status.Buff.Commander`, which `ACataclysmEnemyCharacter` already reads as
	 * twenty percent more movement and attack speed. NOTHING NEW WAS INVENTED
	 * FOR EITHER HALF.
	 *
	 * "EMPOWER" IS READ AS THE BUFF THE GAME ALREADY HAS, which is the same
	 * argument `UCataclysmEnemyModifiers` makes where it rallies allies: "THE
	 * COMMANDER BUFF, WHICH ALREADY EXISTS AND ALREADY SAYS THIS ... A second
	 * buff meaning the same thing would be two names for one effect." The row
	 * names no stat, so nothing here chooses one.
	 *
	 * THE CRATER BURNS THE PLAYER AND THE BEAT EMPOWERS THE CREATURES, which is
	 * two different questions asked of two different sides. The zone's own sweep
	 * finds the hazard source's enemies, which is the player; the beat asks for
	 * the PLAYER's enemies, which is everything else. That is why neither half
	 * needs a flag and why a creature is never burned by a crater it is standing
	 * in.
	 */
	static const TCHAR* HallowedGroundfallKey;

	/**
	 * Spore Clouds: "Enemies have a chance to release spores on death that
	 * poison the player." Issues #1820 and #41.
	 *
	 * THIS RULE STATES NO FIGURE FOR THE POISON AND THAT IS THE POINT. It applies
	 * `DoT_Poison` from `game/Data/StatusEffects.csv`, whose own row says 20
	 * damage a second for 8 seconds, one stack -- read through
	 * `UCataclysmSkillEffects::NumbersForEffectTag` rather than copied here, so
	 * the rule and the data cannot drift apart.
	 *
	 * THIS IS THE FIRST THING IN THE GAME THAT APPLIES POISON. Measured
	 * 2026-09-14: outside `game/Source/Cataclysm/Tests/`, the only mention of
	 * `DoT_Poison` in the module is the line in `CataclysmAilments.cpp` that
	 * names it and its keyword tag. So this rule has no applier to copy, and
	 * `NumbersForEffectTag` is how it reads the row instead.
	 *
	 * THE POISON ROW CALLS ITSELF "A player-applied effect" AND THAT DESCRIBES
	 * WHO USUALLY APPLIES IT RATHER THAN WHO MAY. `DoT_Burn`'s row opens with the
	 * same sentence and then names two enemy modifiers that apply it to the
	 * player -- Infernal Brand and Hellfire Aura -- and both of those are real
	 * code in `CataclysmEnemyModifiers.cpp`, where line 492 calls `ApplyBurn`
	 * with an enemy as the instigator. A sibling row already reads that way, so
	 * this one may.
	 *
	 * THE POISON ROW ALSO NAMES "the Toxic Trail enemy modifier" AND NOTHING IN
	 * `game/Source` IMPLEMENTS IT, so that sentence is not evidence of anything
	 * and is not cited as such here. That gap is issue #1832's subject, not this
	 * rule's.
	 *
	 * A SECOND, PESTILENCE-FLAVOURED POISON WOULD BE TWO NAMES FOR ONE EFFECT,
	 * which is the argument `UCataclysmEnemyModifiers` already makes about the
	 * Commander buff.
	 */
	static const TCHAR* SporeCloudsKey;

	/**
	 * Hellfire: "Enemies have a chance to explode in hellfire when killed."
	 * Issues #1820 and #41.
	 *
	 * THIS RULE STATES NO DAMAGE FIGURE, AND THAT IS COPIED FROM AN EXPLOSION THE
	 * GAME ALREADY HAS. `UCataclysmEnemyModifiers::InfernalBrand` explodes a
	 * creature and works out the damage as that creature's own `AttackDamage`
	 * attribute times a count of hits, under the comment "Read off the creature
	 * rather than written here, so a Herald's brand is a Herald's brand". The same
	 * reading here means a deeper floor's creatures explode harder with no scaling
	 * code, because the game mode already gives each creature kind its damage when
	 * it spawns them.
	 *
	 * A CREATURE WITH NO ATTACK DAMAGE EXPLODES FOR NOTHING, and that is correct
	 * rather than a fault. `ACataclysmEnemyCharacter::StartingAttackDamage` is 0
	 * by default -- its own comment says "Zero means it deals nothing" -- and
	 * Infernal Brand guards the same case with an early return.
	 *
	 * IT IS A BLOW AND NOT AN AILMENT, WHICH IS WHAT SEPARATES IT FROM
	 * `Pestilence_Spore_Clouds`. That row names an ailment -- "spores ... that
	 * poison the player" -- and applies `DoT_Poison`; this row names an explosion
	 * and says nothing about a lasting effect. Reading this one as an ailment too
	 * would make the two rows the same rule under two names.
	 */
	static const TCHAR* HellfireKey;

	/**
	 * Brand of the Aggressor: "Hitting an enemy applies a stack of 'Brand' to
	 * you. At 20 stacks, you erupt in a fire nova that deals 20% of your max HP
	 * to you and nearby allies." Issues #1820 and #41.
	 *
	 * THE ROW STATES BOTH OF ITS NUMBERS, so this rule judges neither. Twenty
	 * stacks and twenty percent of maximum health are read off the sentence.
	 *
	 * THAT IS NOT UNIQUE AND THE FIRST DRAFT OF THIS COMMENT SAID IT WAS.
	 * Measured across the seventeen rows built before this one:
	 * `Famine_Starvation` states two ("reduced by 1%. Up to 60%") and
	 * `Celestial_Edict_of_Silence` states two ("Every 90 seconds ... for 15
	 * seconds"). What IS true is narrower and worth the line: the only figure
	 * this rule decides is the nova's radius, which is one fewer than
	 * `Demonic_Hellfire` below it and one fewer than `Pestilence_Spore_Clouds`,
	 * each of which had to answer a "chance" the table left open.
	 *
	 * IT IS THE PLAYER'S OWN BLOW THAT BRANDS, WHICH IS THE OPPOSITE WAY ROUND
	 * FROM `Famine_Wasting_Sickness`. That row counts stacks when a blow lands ON
	 * the player and its listener tests `Notice.Target`; this row counts them
	 * when the player's blow lands on a creature, so its listener tests
	 * `Notice.Attacker`. Everything else about the two counts is the same shape.
	 *
	 * THE NOVA REACHES THE PLAYER AND THEIR ALLIES AND NOT THE CREATURES, which
	 * is the row's own wording -- "to you and nearby allies" -- and the opposite
	 * of `Demonic_Hellfire` above, whose row names nobody and therefore catches
	 * everyone.
	 */
	static const TCHAR* BrandOfTheAggressorKey;

	/**
	 * Fungal Overgrowth: "Killing enemies creates mushrooms. Stepping on them
	 * grants either a 50% speed boost or a 50% slow." Issues #1820 and #41.
	 *
	 * BOTH OF ITS FIGURES ARE THE ROW'S, so this rule judges neither share. What
	 * it does judge is named beside each constant: how wide a mushroom is, how
	 * often each kind comes up, how long one lasts, and what colour each is
	 * drawn in.
	 *
	 * PLACED BY A DEATH AND READ BY THE BEAT, WHICH IS WITHERED GROUND'S SHAPE
	 * EXACTLY. That rule leaves a patch where a creature dies and asks on each
	 * beat whether the player is standing on one. This one does the same and
	 * differs in two ways: it places one of TWO kinds, and one of the two helps
	 * the player.
	 *
	 * THE FIRST RULE HERE THAT CAN HELP, WHICH IS A STATEMENT ABOUT DUNGEON
	 * RULES AND NOT ABOUT THE STRUCT. `FCataclysmPlayerFloorEffects::
	 * ResistanceMorePercent` already raises a player stat for The Nihil's
	 * Embrace, so the machinery is not new; what is new is a modifier whose own
	 * row offers the player something. Every other built row costs them
	 * something or costs them nothing.
	 *
	 * THE TWO KINDS LOOK DIFFERENT AND THE ROW DOES NOT SAY WHICH IS WHICH. A
	 * player meeting their first mushroom on a floor is guessing, and after they
	 * step on one they know that colour for the rest of the dungeon. That is the
	 * reading of "either ... or" this rule takes: a gamble that turns into
	 * knowledge, rather than a coin flip repeated for ever.
	 */
	static const TCHAR* FungalOvergrowthKey;

	/**
	 * Which element's colours each kind of mushroom is drawn in.
	 *
	 * A JUDGEMENT. The row says the two kinds exist and says nothing about how
	 * either looks.
	 *
	 * THE PAIR WAS MEASURED RATHER THAN PICKED BY EYE. Of the 28 pairs of rows
	 * in `game/Data/ElementVisuals.csv`, Celestial and Void are the second
	 * furthest apart by plain distance between their primary colours (0.96) and
	 * the second furthest by a luminance-weighted one (0.58), and they are the
	 * furthest-apart pair that does not use Demonic. Measured 2026-09-14 by
	 * comparing every pair in that file.
	 *
	 * DEMONIC IS EXCLUDED DELIBERATELY, which is why the furthest pair overall
	 * -- Death and Demonic -- is not this one. Demonic red is the colour every
	 * `Demonic_Infernal_Rain` patch is drawn in, and that is burning ground a
	 * player must get off. A mushroom in that colour would tell them the
	 * opposite of what it does, whichever kind it was.
	 *
	 * NEITHER NAME MEANS ANYTHING HERE BEYOND THE COLOUR, and that is worth
	 * stating because both are damage types. A mushroom deals no damage at all,
	 * so nothing ever asks what element it is: `ACataclysmGroundZone::Sweep`
	 * returns early on a zone with no damage and no curse, and the damage type a
	 * blow is met by comes from the zone's OWNER in any case. See
	 * `ACataclysmGroundZone::DrawnAsType`.
	 *
	 * NEITHER COLOUR PROMISES ANYTHING EITHER, and that was checked rather than
	 * assumed. `Celestial_Hallowed_Groundfall` leaves craters that burn the
	 * player, so gold is not the game's colour for safety; both Void rows that
	 * place a zone slow the player, which happens to agree with the slow
	 * mushroom but is not a convention anything else relies on. The two colours
	 * are here to differ from each other.
	 */
	static const TCHAR* FungalOvergrowthBoostDrawnAs;
	static const TCHAR* FungalOvergrowthSlowDrawnAs;

	/**
	 * Illusory Enemies: "Some enemies are illusions. They look and act like real
	 * enemies but do no damage." Issues #1820 and #41.
	 *
	 * THE SMALLEST RULE IN THIS FILE, AND THAT IS A PROPERTY OF THE GAME RATHER
	 * THAN OF THE ROW. Every route by which a creature damages the player takes
	 * its figure from one attribute, `attack_damage`, on that creature -- the
	 * basic attack, every creature ability, every creature projectile, the
	 * Hellhound's burning lane, the Gatekeeper's burning ground, and the
	 * exploding brand a creature modifier grants. Measured 2026-09-14 by reading
	 * all 30 damage-apply call sites in the 133 non-test source files of
	 * `game/Source/Cataclysm`. So "do no damage" is one number.
	 *
	 * THE ROW STATES NO FIGURE AT ALL. "Some" is the only quantity in it, and
	 * `IllusoryEnemiesSharePercent` below is this rule's own judgement.
	 *
	 * IT CHANGES NOTHING ELSE ABOUT A CREATURE, which is the row's own sentence
	 * -- "they look and act like real enemies". An illusion has its kind's
	 * health and armour, is drawn and animated as its kind, chases and attacks as
	 * its kind, takes blows, dies, announces its death, and is worth what its
	 * kind is worth. Only the damage its attacks carry is zero.
	 *
	 * SO `Demonic_Hellfire` ON AN ILLUSION EXPLODES FOR NOTHING, and that is the
	 * zero working rather than a case anybody wrote. That rule's explosion is the
	 * dying creature's own attack damage multiplied by a count, and it refuses at
	 * zero. A floor carrying both rules needs no code that knows about both.
	 *
	 * TWO THINGS STILL REACH THE PLAYER FROM AN ILLUSION AND THE ROW PERMITS
	 * BOTH. The Abyssal Warden's aura strips two resistances through a status
	 * effect that deals no damage, and a status effect carrying a
	 * `FlatDamagePerTick` would not scale with attack damage at all. No creature
	 * applies one of the second kind today -- the resistance strip is the only
	 * status effect any creature applies -- so the row holds as written. Said
	 * here because it is a fact about today and not a law.
	 */
	static const TCHAR* IllusoryEnemiesKey;

	/**
	 * The row whose void orbs pull, damage and slow. Issues #1605, #41.
	 *
	 * `Partly` BUILT, AND THE MISSING HALF IS THE PULL. The orbs are placed, they
	 * deal void damage read off the row's own type, and standing in one slows the
	 * player by the 40% the row states. **Nothing pulls.** The row names the pull
	 * first, so `BuiltStateOf` answers `Partly` and the floor panel says so.
	 *
	 * THE PULL IS TWO SEPARATE PIECES OF WORK AND NEITHER IS HERE. Pulling the
	 * player cannot go through `UCataclysmSkillEffects::ApplyPull` on a repeating
	 * beat: the diminishing-returns rule halves every displacement inside a five
	 * second window, so a pulsing pull would fade to nothing within about a
	 * second. `ACataclysmTether::Check` documents the way round it and writes a
	 * swept `SetActorLocation` four times a second instead. Pulling a projectile
	 * needs a new mid-flight re-aim; `ACataclysmProjectile::GlanceOnwardFrom` is
	 * the pattern and `Direction` is private today.
	 */
	static const TCHAR* SingularityWellsKey;

	/**
	 * Withered Ground: "Enemies leave patches of Barren Earth on death. While
	 * standing on it, your Health and Mana recovery (regen/leech) is reduced
	 * by 80%." Issue #41.
	 *
	 * THE FIRST RULE HERE PLACED BY AN EVENT RATHER THAN BY A CLOCK, AND NO
	 * LONGER THE ONLY ONE. Infernal Rain and Singularity Wells both drop a
	 * hazard on a cadence and cap how many may exist. This one places a patch
	 * every time a creature dies, and caps nothing, because the row states the
	 * trigger and states no limit: a cap would make "enemies leave patches on
	 * death" stop being true at whichever enemy hit it.
	 *
	 * THIS SAID "THE ONLY RULE" UNTIL `Pestilence_Fungal_Overgrowth` WAS BUILT,
	 * which places a mushroom on every creature's death and caps nothing either,
	 * for the reason given here. Issues #1820 and #41.
	 */
	static const TCHAR* WitheredGroundKey;

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
	 * Infernal Rain's six figures, and which of them are judgements.
	 *
	 * THE ROW STATES ONE OF THEM. "patches of burning ground that deal fire
	 * damage over time for 10 seconds" gives the life outright, so
	 * `InfernalRainPatchSeconds` is not a choice.
	 *
	 * THE RADIUS IS BORROWED FROM THIS PROJECT RATHER THAN CHOSEN. The
	 * Gatekeeper's Soulfall burning ground is `SoulfallGroundRadiusCm = 300`, so
	 * a burning patch in this game is three metres across the radius and this one
	 * is the same size as the one a player has already learned to step out of.
	 *
	 * THE DAMAGE IS A SHARE OF MAXIMUM HEALTH, WHICH IS WHAT A MODIFIER USES.
	 * `ForcedMarchPercentPerStackPerSecond` above states the reason in its own
	 * comment: a share "means the same thing at every character level".
	 *
	 * AND THE OTHER PROJECT RULE IS DELIBERATELY NOT USED. A creature's burning
	 * ground deals `100 / duration` percent OF AN ORDINARY HIT, so a full stay
	 * costs exactly one hit -- 10 over 10 seconds for the Gatekeeper, 25 over 4
	 * for the Hellhound. **A floor hazard has no ordinary hit**, so there is
	 * nothing for that percentage to be a percentage of.
	 *
	 * TWO PER CENT A SECOND IS A JUDGEMENT, and a full ten-second stay therefore
	 * costs a fifth of maximum health. Forced March reaches five per cent a
	 * second and its comment says a character standing still for half a minute
	 * dies; a patch can be walked out of, so it should cost less per second and
	 * still be worth moving for. Expect tuning against real play, which Forced
	 * March says of its own figures too.
	 *
	 * FIVE SECONDS BETWEEN PATCHES IS A JUDGEMENT TAKEN FROM A SHAPE RATHER THAN
	 * A FIGURE, AND NOTHING SHIPPED PUBLISHES THE FIGURE. Diablo IV's Volcanic
	 * dungeon affix is almost this row -- gouts of flame periodically erupt near
	 * players while in combat -- and it states no number at all: not a cadence,
	 * not a radius, not a duration, not a rate. Nor do Path of Exile 2's own
	 * threads about its burning-ground map modifier. So the five is mine.
	 * `docs/DECISIONS.md` names the sources.
	 *
	 * AND WHERE A SHIPPED VERSION WENT WRONG IS WHY THIS IS THE LOW SIDE. Path of
	 * Exile 2's Early Access feedback on the same modifier is that it hurts badly
	 * even at high fire resistance, and that a patch cannot be told apart from
	 * the player's own effects. The first is an argument for a modest share; the
	 * second is issue #1699 and why this row is `Partly` built.
	 *
	 * THREE AT ONCE IS WHAT MAKES THE CADENCE SAFE. Five seconds against a
	 * ten-second life accumulates on purpose -- that is what rain means -- where
	 * the Gatekeeper's ground asserts that lasting exactly its cooldown is what
	 * stops patches accumulating faster than they expire. Two is the steady
	 * state, three allows the transient, and a long fight cannot fill the floor.
	 *
	 * TWELVE METRES IS "IN COMBAT ZONES" MADE CONCRETE, AND THE READING IS NOT
	 * ONLY MINE. The game has no combat-zone concept to bind to --
	 * `ECataclysmFloorLayout::Arena` is a whole floor's shape rather than a
	 * region inside one, and nothing tracks where fighting is happening -- so the
	 * player's position is the only thing this rule can locate on every beat.
	 * Diablo IV's Volcanic affix settles that this is what a shipped game means
	 * by the sentence: its flames erupt near players, while in combat.
	 *
	 * THE DISTANCE ITSELF IS THE JUDGEMENT. Far enough that a patch is not
	 * dropped on a standing player's feet with no warning -- Path of Exile 2's
	 * players complain about exactly that, a patch that damages instantly on
	 * appearing -- and near enough that moving a few steps is not an escape from
	 * the modifier itself.
	 *
	 * THIS IS THE FURTHEST AND THE NEAREST IS NOT A CONSTANT, which is worth
	 * knowing before reading the placer. The nearest is one centimetre PAST
	 * `InfernalRainRadiusCm` rather than at it, because
	 * `UCataclysmTargeting::IsInLine` decides who is inside with `<=`: a patch
	 * centred at exactly the radius covers a standing player, and the point of the
	 * nearest distance is that it does not.
	 */
	static constexpr float InfernalRainPatchSeconds = 10.0f;
	static constexpr float InfernalRainRadiusCm = 300.0f;
	static constexpr float InfernalRainPercentPerSecond = 2.0f;
	static constexpr float InfernalRainSecondsBetweenPatches = 5.0f;
	static constexpr int32 InfernalRainMostPatches = 3;
	// NO DIGIT SEPARATOR IN THIS NUMBER, AND THAT IS NOT A STYLE CHOICE. With an
	// apostrophe between the thousands and the hundreds, this file does not build:
	// Unreal Header Tool reads the apostrophe as the start of a character literal
	// and fails with "Unterminated character constant" before the compiler ever
	// sees the line. Measured on this line, which was written that way first.
	//
	// EVERY DIGIT SEPARATOR IN game/Source IS IN A .cpp FILE, which Unreal Header
	// Tool does not parse, and this was the only one in a header. So the project's
	// practice was already right and nothing stated it.
	//
	// AN APOSTROPHE IN A COMMENT IS FINE, which is worth saying because the
	// failure was a tokeniser error and the cautious reading would be that no
	// apostrophe may appear anywhere. Counted: this header on development carries
	// 55 of them in prose and builds. It is the one in the number that breaks it.
	// Issue #1703.
	static constexpr float InfernalRainFallsWithinCm = 1200.0f;

	static_assert(
		InfernalRainSecondsBetweenPatches < InfernalRainPatchSeconds,
		"Infernal Rain's patches no longer overlap. A cadence at or beyond the "
		"patch life means one patch expires before the next falls, which is not "
		"rain -- it is one hazard at a time. The cap is what bounds the "
		"accumulation, not the cadence.");

	static_assert(
		InfernalRainPercentPerSecond * InfernalRainPatchSeconds < 100.0f,
		"Standing in one Infernal Rain patch for its whole life now costs a "
		"character its entire maximum health. The row describes a hazard to "
		"walk out of, not a death sentence for being caught once.");

	/**
	 * Singularity Wells' six figures, and which of them are judgements.
	 *
	 * THE ROW STATES ONE: "slowing movement by 40%". That is
	 * `SingularityWellsSlowPercent` and it is not a choice.
	 *
	 * TWO ARE BORROWED FROM THIS PROJECT RATHER THAN CHOSEN. The radius is the
	 * burning ground's `SoulfallGroundRadiusCm`, so a well is the size of a patch
	 * a player has already learned to step out of; the distance is Infernal
	 * Rain's, for the reason that comment gives.
	 *
	 * THE OTHER THREE ARE JUDGEMENTS, and `docs/DECISIONS.md` carries the
	 * research behind each. What follows is the short form.
	 *
	 * WHAT THE GENRE SAYS TO WORRY ABOUT, AND WHY IT WAS THE WRONG WORRY. Path of
	 * Exile 2 ships a movement-slowing map modifier that its players object to in
	 * at least eight feedback threads, at a published 30% -- less than this row's
	 * 40% -- and the complaint is that it is "impossible to actually interact with
	 * and avoid". So this was designed for avoidability first.
	 *
	 * THEN THE ARITHMETIC REVERSED IT. Three wells of this radius cover 0.33% of
	 * a 160 by 160 metre floor. The risk was never that the slow is everywhere;
	 * it is that a player never meets the modifier at all, which is how a row
	 * ends up doing nothing. So the wells are placed near the player, the way
	 * Infernal Rain's patches are, and then they cover 18.8% of the twelve metre
	 * circle around them and leave 81% of it clear.
	 *
	 * WALKING OUT OF ONE TAKES 1.2 SECONDS FROM ITS CENTRE, at the slowed speed
	 * of 2.4 metres a second. That is the figure that makes this avoidable rather
	 * than the 40%, and it is why three is the cap.
	 *
	 * ONE PER CENT A SECOND IS THE SMALLEST DAMAGE OF THE THREE HAZARDS HERE, at
	 * half Infernal Rain's. Diablo IV's pulling affix Tempest "deals light damage
	 * and pulls in players", and where a shipped affix both moves a player and
	 * hurts them the damage is the lesser half. This row slows AND pulls AND
	 * damages, so its damage is the least of what it does.
	 *
	 * EIGHT SECONDS BETWEEN WELLS IS THE ONLY FIGURE THE RESEARCH OFFERED. Diablo
	 * IV publishes cadences for the affixes that act rather than persist --
	 * Teleporter every 8 seconds, Hellbound every 15 -- and publishes no radius,
	 * pull distance or slow percentage for any of them. So eight is the bottom of
	 * a shipped range rather than a number of mine.
	 *
	 * **Expect these to need tuning against real play**, which Forced March and
	 * Infernal Rain both say of their own figures.
	 */
	static constexpr float SingularityWellsSlowPercent = 40.0f;
	static constexpr float SingularityWellsRadiusCm = 300.0f;
	static constexpr float SingularityWellsFallsWithinCm = 1200.0f;
	static constexpr int32 SingularityWellsMostWells = 3;
	static constexpr float SingularityWellsPercentPerSecond = 1.0f;
	static constexpr float SingularityWellsSecondsBetweenWells = 8.0f;

	static_assert(
		SingularityWellsSlowPercent > 0.0f && SingularityWellsSlowPercent < 100.0f,
		"Singularity Wells' slow is no longer a fraction of a character's speed. "
		"At 100 it stops the player dead, which the row does not ask for, and the "
		"pipeline floors a Less at -99 so the figure would stop meaning what it "
		"says.");

	static_assert(
		SingularityWellsFallsWithinCm > SingularityWellsRadiusCm,
		"A Singularity Well can no longer land clear of the player. The nearest "
		"distance is past the radius so a well does not cover someone standing "
		"still, and the furthest has to be beyond that for there to be anywhere "
		"to put one.");

	static_assert(
		SingularityWellsPercentPerSecond < InfernalRainPercentPerSecond,
		"A Singularity Well now costs at least as much a second as a patch of "
		"Infernal Rain. A well slows and pulls as well as damaging, so its damage "
		"is meant to be the least of the three things it does; the burning ground "
		"only damages.");

	/**
	 * Withered Ground: how much recovery a patch takes, and how wide a patch is.
	 *
	 * THE FIRST IS THE ROW'S OWN NUMBER AND NOT A JUDGEMENT. "reduced by 80%"
	 * is stated, which makes this the only hazard rule here whose main figure
	 * came with the row. `tools/tests/test_dungeon_modifier_rules_are_the_rows.py`
	 * holds the two together, so the row and this constant cannot drift.
	 *
	 * 80% IS INSIDE THE PIPELINE'S FLOOR, which matters because the pipeline
	 * clamps a single Less at -99 and counts the clamp.
	 * `UCataclysmStatPipeline::LessMultiplierFloor` is -99, so an 80 applies as
	 * written and the figure keeps meaning what the row says.
	 *
	 * THE RADIUS IS A JUDGEMENT AND IT IS THE HOUSE FIGURE. Three separate
	 * things already use 300 cm for a patch of ground a player stands in: the
	 * Gatekeeper's Soulfall burning ground, Infernal Rain's patches, and a
	 * Singularity Well. Choosing a fourth number would make this row's patch
	 * differently sized for no reason the row gives.
	 *
	 * NO CAP AND NO CADENCE CONSTANT, WHICH IS WHY THERE ARE ONLY TWO FIGURES
	 * HERE. The two hazard rules above need `Most...` and `SecondsBetween...`
	 * because a clock places them; this one is placed by a death, and the row
	 * gives neither a limit nor a chance.
	 *
	 * IF A LIMIT EVER BECOMES NECESSARY it is a performance concern -- actor
	 * count on a floor with many kills -- and the project owner deprioritised
	 * performance work on 2026-09-10 with "do not make feature work wait on a
	 * performance capture". The version to build then is replacing the oldest
	 * patch rather than refusing a new one, because that is the only form that
	 * keeps the row's sentence true.
	 */
	static constexpr float WitheredGroundRecoveryLessPercent = 80.0f;
	static constexpr float WitheredGroundPatchRadiusCm = 300.0f;

	static_assert(
		WitheredGroundRecoveryLessPercent > 0.0f
			&& WitheredGroundRecoveryLessPercent < 100.0f,
		"Withered Ground's reduction is no longer a fraction of what a character "
		"recovers. At 100 it stops recovery dead, which the row does not ask for, "
		"and the pipeline floors a Less at -99 so the figure would stop meaning "
		"what it says.");

	static_assert(
		WitheredGroundPatchRadiusCm == SingularityWellsRadiusCm
			&& WitheredGroundPatchRadiusCm == InfernalRainRadiusCm,
		"A patch of Barren Earth is no longer the size of the other two patches a "
		"player can stand in. That was the whole argument for the figure -- no row "
		"states a radius, so the three agree rather than each inventing one. If "
		"this row's patch should differ, say why beside the constant and delete "
		"this assertion rather than loosening it.");

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
	 * Death's Embrace: what one stack of Embrace of Death takes off the healing
	 * a player receives, the most stacks it reaches, and how long one takes to
	 * arrive.
	 *
	 * ALL THREE ARE JUDGEMENTS. The row states no number: "Players periodically
	 * gain stacks of a debuff called ""Embrace of Death,"" which reduces healing
	 * received. Stacks reset when entering a new floor." The Python test fails if
	 * it ever states one, at which point these stop being judgements.
	 *
	 * FIFTY PER CENT AT WORST SITS INSIDE WHAT THE GENRE PUBLISHES. Path of Exile
	 * runs reduced recovery on MAP modifiers, the closest thing in the genre to a
	 * dungeon modifier, from "10% less Recovery Rate" to "60% less Recovery Rate
	 * of Life and Energy Shield"; items sit at "(20-30)% reduced Recovery rate"
	 * and keystones at "50% less Life Regeneration Rate". Five stacks of ten is
	 * inside that range and below its top, which suits a row whose danger weight
	 * is 10 on a ladder that runs 5, 10, 15, 20 (issue #1790: this said the
	 * heaviest was 15, and it was 20 when that was written). NOT SIXTY: this is
	 * not the harshest recovery row in the set, and the two that are -- Necrotic
	 * Ground's 50% and Withered Ground's 80% -- state their own figures in the
	 * data.
	 *
	 * FIVE STACKS IS THIS PROJECT'S OWN CAP, matching Forced March, so a player
	 * reads the two alike.
	 *
	 * TEN SECONDS A STACK IS THE ONE FIGURE WITH NO PRECEDENT ANYWHERE. No game
	 * found publishes an interval at which a healing-reduction stack arrives, so
	 * it rests on the row's word "periodically" and on what a floor lasts: fifty
	 * seconds to the cap is about one floor's fighting, which fits a row whose
	 * stacks "reset when entering a new floor". A player who clears briskly never
	 * sees the cap and one who lingers does.
	 *
	 * EXPECT ALL THREE TO NEED TUNING. The one comparable lever with published
	 * figures -- a World of Warcraft dungeon affix adding a share of maximum
	 * health per stack -- was retuned twice in eleven days by its own developers,
	 * which `docs/DECISIONS.md` already records for Forced March.
	 */
	static constexpr float DeathsEmbracePercentPerStack = 10.0f;
	static constexpr int32 DeathsEmbraceMostStacks = 5;
	static constexpr float DeathsEmbraceSecondsPerStack = 10.0f;

	/**
	 * Mortal Decay: how much faster the decay runs for each floor of depth, the
	 * fastest it ever runs, how much a kill takes off it, and for how long.
	 *
	 * ALL FOUR ARE JUDGEMENTS. The row states no number and no percentage at
	 * all. `tools/tests/test_dungeon_modifier_rules_are_the_rows.py` fails if it
	 * ever states one, at which point whichever of these it names stops being a
	 * judgement.
	 *
	 * A SHARE OF MAXIMUM HEALTH PER SECOND, WHICH IS BOTH THE HOUSE UNIT AND THE
	 * GENRE'S. Forced March's comment above gives the house reason: a share
	 * "means the same thing at every character level". Path of Exile's Delve
	 * prices its own depth-scaled drain the same way -- standing in the Darkness
	 * costs 2% of Life and Energy Shield per second per stack, and a player's
	 * Darkness Resistance falls automatically the deeper they delve. So a life
	 * drain that worsens with depth is a shape a shipped game in this genre
	 * already runs, and it is priced in exactly these units.
	 *
	 * ONE PER CENT A SECOND AT WORST, WHICH IS THE BOTTOM OF THIS GAME'S OWN
	 * BAND RATHER THAN THE MIDDLE OF IT. Forced March reaches 5% a second at
	 * five stacks, a patch of Infernal Rain costs 2% a second, and a Singularity
	 * Well costs 1%. **Every one of those three stops when the player moves** --
	 * out of the patch, out of the well, or off the spot Forced March punishes.
	 * This one has nowhere to stand: no position on the floor is free of it, and
	 * the only lever the row gives the player is killing. So its ceiling sits at
	 * the bottom of the band, and Delve's 2% is NOT evidence for a larger
	 * figure, because Delve's darkness is cleared by one step back into light.
	 *
	 * A TENTH OF A PER CENT A FLOOR, SO THE CEILING ARRIVES AT FLOOR 10. Against
	 * the fifty seconds Death's Embrace's comment calls "about one floor's
	 * fighting", floor 1 costs about 5% of a health bar and floor 10 and deeper
	 * costs about half of one, for a player who never kills anything. Five per
	 * cent is the "gradually" the row asks for; half a bar is the reason to
	 * reap.
	 *
	 * HALF, BECAUSE "SLOW" IS NOT "STOP". The row says "temporarily slow the
	 * effect of the affliction", so a kill must not buy immunity. Half is the
	 * plain reading of the word and is large enough that reaping is clearly
	 * worth doing.
	 *
	 * FIVE SECONDS A KILL, SO FIGHTING HOLDS IT AND WALKING DOES NOT. A player
	 * working through a pack kills every few seconds and keeps the slow up; one
	 * crossing an empty floor loses it. That is what makes "give death his due
	 * souls" something the player keeps doing rather than did once.
	 *
	 * THE GENRE SETTLES THE SHAPE OF THAT WINDOW AND NOT ITS LENGTH. Path of
	 * Exile's Breach is the published case of killing buying time: a Breach
	 * stays open a minimum of 30 seconds and a maximum of 60, and how fast its
	 * monsters die is what moves it between the two. So a BOUNDED window is what
	 * a shipped game grants for killing, and this one is bounded by expiring
	 * rather than by a ceiling. The five seconds themselves are mine.
	 *
	 * EXPECT ALL FOUR TO NEED TUNING AGAINST REAL PLAY, which Forced March,
	 * Infernal Rain, Singularity Wells and Death's Embrace each say of their own
	 * figures. `docs/DECISIONS.md` carries the sources.
	 */
	/**
	 * Wasting Sickness: the chance one landed blow inflicts a stack, what a stack
	 * takes off both maximums, and the most stacks it reaches.
	 *
	 * ALL THREE ARE JUDGEMENTS. The row states no number and no percentage.
	 * `tools/tests/test_dungeon_modifier_rules_are_the_rows.py` fails if it ever
	 * states one.
	 *
	 * THREE PER CENT A STACK COMES FROM THE FAMILY THIS ROW IS ACTUALLY IN.
	 * World of Warcraft's Necrotic affix has enemy melee attacks apply a stacking
	 * debuff at 3% a stack, and a sibling affix of the same shape uses 2%. That
	 * is the shipped figure for "enemy hits stack a debuff on you", and it is
	 * where this comes from.
	 *
	 * AND NOT FROM THE GAMES THAT TAKE A QUARTER OF A HEALTH BAR, which are the
	 * closer match for the WORDING and the wrong match for the SIZE. Vermintide
	 * 2's Grimoire curse takes 30% of every party member's maximum health for the
	 * mission, and Darktide's corruption about 25% for one grimoire and 50% for
	 * two -- mission-scoped maximum-health reductions, exactly this row's "for
	 * the duration of the dungeon". **In both, the player CHOOSES to take it in
	 * exchange for reward.** This one is inflicted by being hit, so those settle
	 * that the shape is shipped and settle nothing about the magnitude.
	 *
	 * FIVE STACKS IS THE HOUSE CAP, matching Forced March and Death's Embrace, so
	 * a player reads the three alike. Fifteen per cent at worst against
	 * Starvation's 60% suits a row of weight 15 against that row's 20.
	 *
	 * THE CAP IS LOAD-BEARING HERE AND DECORATIVE IN THE OTHER TWO, because of
	 * how rarely the row's own cure appears. Only the Elite dungeon sub-type ends
	 * every floor with a boss; on other sub-types a player can meet no boss for
	 * many floors, and the debuff really does last the dungeon. Forced March's
	 * stacks clear by moving and Death's Embrace's by the stairs.
	 *
	 * TEN PER CENT A BLOW SITS BELOW THIS PROJECT'S OWN RANGE FOR AN ON-HIT
	 * DEBUFF CHANCE. `game/Data/EnchantmentsPositive.csv` carries "Strike skills
	 * have a 15%-30% chance to apply a random debuff on hit". Below it because
	 * that one is a reward the player built for and this is a penalty they did
	 * not choose, and because this one does not expire on its own.
	 *
	 * A LANDED BLOW AND NOT AN ATTEMPT, which is what makes the chance mean what
	 * it says: `FCataclysmHitNotice::Landed` is zero for a blow that was evaded
	 * or wholly mitigated, and such a blow inflicts nothing.
	 *
	 * EXPECT ALL THREE TO NEED TUNING AGAINST REAL PLAY, which every other rule
	 * in this file says of its own figures.
	 */
	/**
	 * Grasping Tentacles: how wide a tentacle's reach is, how far from the player
	 * one appears, how many a floor carries, how often another appears, the
	 * chance a beat inside a reach is grabbed, how long a grab holds, how long
	 * before that tentacle may grab again, and how much speed a grab takes.
	 *
	 * THE ROW STATES NO NUMBER AT ALL, so none of these is read off it.
	 * `tools/tests/test_dungeon_modifier_rules_are_the_rows.py` fails if it ever
	 * states one.
	 *
	 * THREE ARE BORROWED FROM THIS PROJECT RATHER THAN CHOSEN. The reach is the
	 * house figure for a patch of ground a character stands in -- the Gatekeeper's
	 * Soulfall ground, Infernal Rain, a Singularity Well and Withered Ground all
	 * use 300, and a fifth number would make this one differently sized for no
	 * reason the row gives. The appearance distance is Infernal Rain's twelve
	 * metres, for the reason that comment states. The cadence is Singularity
	 * Wells' eight seconds, which is itself the bottom of the only published
	 * range found: Diablo IV states cadences for the affixes that act rather than
	 * persist, and publishes no radius, reach or duration for any of them.
	 *
	 * "ALL OVER THE DUNGEON" IS READ AS "NEAR THE PLAYER, REPEATEDLY", WHICH IS A
	 * JUDGEMENT AND NOT A NEW ONE. Nothing the beat can reach knows the shape of
	 * the floor: the game mode does not keep the floor plan after building, and
	 * the player's position is the only thing it can locate four times a second.
	 * Infernal Rain reads "in combat zones" the same way and `docs/DECISIONS.md`
	 * records that as a judgement. So a tentacle appears near the player on a
	 * cadence, and a player who walks the floor meets them all over it.
	 *
	 * FIVE AT ONCE, WHICH IS MORE THAN THE THREE SINGULARITY WELLS ALLOWS AND
	 * DELIBERATELY SO. That row is weight 15 and each of its wells damages,
	 * slows and is meant to pull; this row is weight 5 and a tentacle does
	 * nothing at all until it grabs. More of a milder thing is what "all over"
	 * asks for.
	 *
	 * A TWENTIETH OF A BEAT IS THE CHANCE, AND WHAT SIZES IT IS CROSSING RATHER
	 * THAN STANDING. The beat is a quarter second, so walking through a three
	 * metre reach at 4 metres a second is about six beats and a one-in-four
	 * chance of being caught, while lingering four seconds is about even. That is
	 * "have to be careful of getting too close" -- a risk when you stay, rarely a
	 * toll when you pass.
	 *
	 * A SECOND AND A HALF IS THIS PROJECT'S OWN FIGURE AND NOT AN INVENTED ONE.
	 * `game/Data/StatusEffects.csv` says of a stun that "designed skills run 0.75
	 * to 1.5 seconds". A grab is gentler than a stun, because the character can
	 * still act, so it sits at the top of that band rather than beyond it.
	 *
	 * FIVE SECONDS BEFORE THE SAME TENTACLE MAY GRAB AGAIN, AND THE SHAPE IS THE
	 * GENRE'S EVEN THOUGH THE FIGURE IS MINE. Crowd control in this genre is
	 * governed by diminishing returns precisely so that repeated application
	 * cannot hold a player indefinitely -- Diablo III makes a target progressively
	 * resistant to repeated control, and one published system halves a root's
	 * duration on a second application within fifteen seconds, quarters it on the
	 * third, then grants immunity. **No published root DURATION was found for
	 * Path of Exile or Diablo IV**; the searches returned design commentary. So
	 * the shape is borrowed and the number is not.
	 *
	 * WHAT THE COOLDOWN BUYS, IN ARITHMETIC: a character standing on one tentacle
	 * is held 1.5 seconds in every 6.5, about 23 per cent of the time, and is free
	 * the rest. At weight 5 -- the lightest band in the table, shared with Forced
	 * March and Withered Ground -- that is the intended weight of it.
	 *
	 * EXPECT THESE TO NEED TUNING AGAINST REAL PLAY, which every other rule in
	 * this file says of its own figures.
	 */
	/**
	 * Edict of Silence: how often the silence comes, how long it lasts, and what
	 * the lock is written as.
	 *
	 * THE FIRST TWO ARE THE ROW'S OWN AND NEITHER IS A JUDGEMENT: "Every 90
	 * seconds, a divine silence sweeps the dungeon for 15 seconds".
	 * `tools/tests/test_dungeon_modifier_rules_are_the_rows.py` fails if the row
	 * stops saying either, which is what keeps them from quietly drifting apart
	 * from what a player is shown.
	 *
	 * THE THIRD IS NOT A FIGURE ANYBODY CHOSE. Everything that reads
	 * `UCataclysmSkillSlots::LockedStat` asks whether it is above zero, so one is
	 * simply how "above zero" is written. It is a constant rather than a literal
	 * so that the rule and its tests cannot disagree about it.
	 *
	 * NINETY IS THE GAP BETWEEN SILENCES AND NOT THE GAP BETWEEN THEM ENDING AND
	 * BEGINNING, which is worth stating because the row can be read either way.
	 * "Every 90 seconds ... for 15 seconds" reads as a ninety-second cycle with
	 * fifteen of it silent, not as fifteen seconds of silence separated by ninety
	 * of quiet, which would be a hundred-and-five-second cycle. The first is the
	 * ordinary reading of "every N seconds" and is what the beat implements.
	 */
	static constexpr float EdictOfSilenceEverySeconds = 90.0f;
	static constexpr float EdictOfSilenceLastsSeconds = 15.0f;
	static constexpr float EdictOfSilenceLockValue = 1.0f;

	/**
	 * How often a strike is called in. THE ROW'S OWN NUMBER: "Every 30 seconds".
	 */
	static constexpr float ArtilleryStrikeSecondsBetween = 30.0f;

	/**
	 * How long the circle sits on the ground before the shell lands.
	 *
	 * A JUDGEMENT, AND DERIVED RATHER THAN PICKED. The row says a circle appears
	 * and a strike "will land", which is a delay, and gives no length for it.
	 * `ACataclysmPlayerCharacter::DefaultWalkSpeedCmPerSecond` is 400, so walking
	 * out of a 600 cm circle from its centre takes 1.5 seconds at base speed.
	 * Three seconds is twice that, and still clears the 40% slow
	 * `Void_Singularity_Wells` can be applying on the same floor, where the walk
	 * out takes 2.5 seconds.
	 *
	 * IF EITHER FIGURE MOVES, THIS ONE HAS TO BE RE-DERIVED. A larger radius or a
	 * slower player makes three seconds too short, which is why the static
	 * assertion below ties it to the radius and the walk speed rather than
	 * leaving it as a number somebody once liked.
	 */
	static constexpr float ArtilleryStrikeWarningSeconds = 3.0f;

	/**
	 * How wide the circle is.
	 *
	 * A JUDGEMENT. "Massive" is not a number. `InfernalRainRadiusCm` is 300 and
	 * that row says only "patches", so this being twice it is what "massive"
	 * buys.
	 */
	static constexpr float ArtilleryStrikeRadiusCm = 600.0f;

	/**
	 * What the shell takes off whatever it lands on, once, as a share of
	 * maximum health.
	 *
	 * A JUDGEMENT, AND THE ONE WITH THE LEAST BEHIND IT. No source read for this
	 * change gives a telegraphed ground attack's damage as a share of maximum
	 * health, so this is not derived from anything. What it is measured against:
	 * `InfernalRainPercentPerSecond` is 2 for `InfernalRainPatchSeconds` of 10,
	 * so standing in fire for its whole life costs 20% -- and a player can step
	 * out at any moment. This lands once and cannot be partly taken, so being
	 * slightly worse than the whole of the other is the intent.
	 *
	 * AND THIS ROW IS WEIGHT 5.0, THE LIGHTEST BAND IN THE TABLE, shared with 24
	 * other rows. A lightest-band row that hurt more than the heavy ones would be
	 * wrong however good the number looked on its own.
	 */
	static constexpr float ArtilleryStrikeMaxHealthPercent = 25.0f;

	/**
	 * How far from the player a strike can be called in.
	 *
	 * THE SAME FIGURE INFERNAL RAIN USES, and for its reason: a hazard that only
	 * ever appears on top of the player is not a thing to walk out of. Copied as
	 * a conclusion rather than as a number -- see `InfernalRainFallsWithinCm`.
	 */
	static constexpr float ArtilleryStrikeLandsWithinCm = 1200.0f;

	/**
	 * How often the artillery comes. THE ROW'S OWN NUMBER: "every 30 seconds".
	 */
	static constexpr float HallowedGroundfallSecondsBetween = 30.0f;

	/**
	 * How often a death releases spores, as a percentage.
	 *
	 * A JUDGEMENT, DERIVED FROM THE TABLE ITSELF. The row says "a chance" and
	 * gives no figure, and it is not alone: measured 2026-09-14, EIGHT of the 117
	 * rows in `game/Data/DungeonModifiers.csv` say "a chance" with no percentage
	 * beside it. One of those eight is `Famine_Wasting_Sickness`, which is built,
	 * and the project answered that same gap with ten -- see
	 * `WastingSicknessChancePercentPerHit` below.
	 *
	 * THIS PARAGRAPH SAID `WastingSicknessChancePercentPerHit` WAS "the only other
	 * chance comparison in the built rules" AND `Demonic_Hellfire` MADE THAT
	 * FALSE. There are three now: Wasting Sickness on a blow, this row on a death,
	 * and Hellfire on a death. The sentence was true when written and was
	 * corrected by the change that stopped it being true.
	 *
	 * TEN IS ALSO WHAT THE TABLE USES FOR THIS EXACT TRIGGER.
	 * `Death_Vengful_Wraiths` -- "Enemies have a 10% chance of turning into
	 * wraiths when killed" -- is the only row that states a figure for a chance
	 * fired by ANY enemy's death, which is the shape this row has.
	 * `Pestilence_Epidemic` states 25% but conditions it on the dead enemy being
	 * diseased, so it answers a narrower question.
	 *
	 * THE STATED CHANCES ACROSS THE WHOLE TABLE ARE 5, 10, 25, 30 AND 50, in
	 * `Chaos_Wild_Magic`, `Death_Vengful_Wraiths`, `Chaos_Unstable_Portal`,
	 * `Pestilence_Epidemic` and `War_Royal_Guard`. Ten is inside that vocabulary
	 * rather than a new number.
	 */
	static constexpr float SporeCloudsChancePercentOnDeath = 10.0f;

	/**
	 * How far from the corpse the spores reach.
	 *
	 * THE SAME FIGURE `WitheredGroundPatchRadiusCm` USES, and copied as a
	 * conclusion rather than derived again: spores released at a corpse and a
	 * patch left at a corpse are one question -- how far does a thing left by a
	 * death reach -- and the project has already answered it once. Two answers to
	 * one question is what this avoids.
	 */
	static constexpr float SporeCloudsReachCm = WitheredGroundPatchRadiusCm;

	/**
	 * How often a death explodes, as a percentage.
	 *
	 * THE SAME GAP AND THE SAME ANCHOR AS `SporeCloudsChancePercentOnDeath` above,
	 * and the anchor is about the data rather than about this file, so building a
	 * second rule on it does not weaken it: `Death_Vengful_Wraiths` -- "Enemies
	 * have a 10% chance of turning into wraiths when killed" -- is the only row of
	 * `game/Data/DungeonModifiers.csv` that states a figure for a chance fired by
	 * ANY enemy's death, which is the shape this row has too.
	 *
	 * BOTH ROWS SAY "a chance" AND NEITHER GIVES A FIGURE. Eight of the 117 rows
	 * are written that way, measured 2026-09-14.
	 */
	static constexpr float HellfireChancePercentOnDeath = 10.0f;

	/**
	 * How far the explosion reaches.
	 *
	 * DECLARED AS `InfernalRainRadiusCm` AND NOT AS A FIGURE. 300 is this
	 * project's settled answer for a thing at a point on the floor: Infernal
	 * Rain's patch, Singularity Wells, Withered Ground's patch, Grasping
	 * Tentacles' reach, the Gatekeeper's Soulfall ground and Spore Clouds' reach
	 * are all that number.
	 *
	 * NOT `ArtilleryStrikeRadiusCm`, WHICH IS 600. That figure belongs to a circle
	 * its own row calls "a massive red circle"; this row says nothing about size,
	 * so taking the larger number would be inventing one.
	 */
	static constexpr float HellfireRadiusCm = InfernalRainRadiusCm;

	/**
	 * How many of the dying creature's own blows the explosion is worth.
	 *
	 * A JUDGEMENT, AND THE ONLY ONE IN THIS RULE. The row states no magnitude.
	 *
	 * ONE, NOT `UCataclysmEnemyModifiers::InfernalBrandExplosionHits`, WHICH IS 5.
	 * That five has a reason -- the brand banks one hit's worth per stack and
	 * explodes when five have built up -- and the reason does not transfer to a
	 * death that banks nothing. What is copied from Infernal Brand is the shape,
	 * reading the damage off the creature; the count is derived again here.
	 *
	 * WHY ONE. This fires at the moment the player kills something, when they are
	 * usually standing next to it, and there is no warning to react to.
	 * `War_Artillery_Strike` takes a quarter of maximum health but announces
	 * itself three seconds early and can be walked out of; an unavoidable blow
	 * cannot be priced like an avoidable one. The smallest honest reading of a
	 * creature exploding is that it lands one more of its own blows as it dies.
	 */
	static constexpr float HellfireExplosionHits = 1.0f;

	/**
	 * How many of the player's landed blows brand before the nova.
	 *
	 * THE ROW'S OWN FIGURE: "At 20 stacks, you erupt". Nothing is derived here
	 * and nothing is judged.
	 */
	static constexpr int32 BrandStacksToErupt = 20;

	/**
	 * What share of the player's maximum health the nova deals.
	 *
	 * THE ROW'S OWN FIGURE: "deals 20% of your max HP".
	 *
	 * DEALT AND NOT TAKEN, AND THE DIFFERENCE IS NOT PEDANTRY. The nova is
	 * delivered as an ordinary area blow, the same way `War_Artillery_Strike`
	 * delivers its own, so armour and resistances take their cut on the way in
	 * and what reaches health is less than the figure stated. The row says what
	 * is dealt. `ACataclysmDungeonGameMode` cannot promise what arrives without
	 * bypassing every defence the player has earned, which no other rule does.
	 */
	static constexpr float BrandNovaMaxHealthPercent = 20.0f;

	/**
	 * How far the nova reaches.
	 *
	 * THE ONE FIGURE THE ROW DOES NOT STATE, and it is declared as
	 * `InfernalRainRadiusCm` rather than as a number for the reason
	 * `HellfireRadiusCm` above gives: 300 is this project's settled answer for a
	 * thing at a point on the floor.
	 *
	 * NOT `ArtilleryStrikeRadiusCm`, WHICH IS 600, because that figure belongs to
	 * a circle its own row calls "a massive red circle" and this row says nothing
	 * about size.
	 */
	static constexpr float BrandNovaRadiusCm = InfernalRainRadiusCm;

	static_assert(
		BrandStacksToErupt > 1,
		"A nova at one stack is a nova on every blow, and the row's word 'stacks' "
		"describes nothing.");

	static_assert(
		BrandNovaMaxHealthPercent > 0.0f && BrandNovaMaxHealthPercent < 100.0f,
		"At zero the nova deals nothing and the row is unbuilt; at a hundred it "
		"kills a player at full health outright, which the row does not say.");

	static_assert(
		BrandNovaRadiusCm > 0.0f,
		"A nova that reaches nowhere reaches nobody, the player included.");

	/**
	 * What a mushroom does to the player standing on it, in percent, each way.
	 *
	 * BOTH ARE THE ROW'S OWN FIGURES: "either a 50% speed boost or a 50% slow".
	 * `tools/tests/test_dungeon_modifier_rules_are_the_rows.py` holds each
	 * constant against the sentence it came from, so the row and these cannot
	 * drift apart.
	 *
	 * THEY ARE NOT THE SAME NUMBER TWICE, WHICH IS WORTH SAYING BECAUSE THEY
	 * READ AS ONE. A 50% boost multiplies speed by 1.5 and a 50% slow multiplies
	 * it by 0.5, so the pair is not symmetrical and a player who steps on one of
	 * each is left at 0.75 of their speed rather than back where they started.
	 * The row asks for two shares of the same stat and says nothing about
	 * cancelling, so they compose the way every other pair in this game
	 * composes.
	 */
	static constexpr float FungalOvergrowthSpeedMorePercent = 50.0f;
	static constexpr float FungalOvergrowthSpeedLessPercent = 50.0f;

	/**
	 * How often a mushroom is the kind that helps, in percent.
	 *
	 * A JUDGEMENT, AND AN EVEN SPLIT IS THE ONLY ONE THE ROW SUPPORTS. "either a
	 * 50% speed boost or a 50% slow" names two outcomes and no odds between
	 * them. Any other figure would be this file deciding a thing the design left
	 * open in a direction nobody asked for.
	 *
	 * NOT A CHANCE THAT A MUSHROOM APPEARS AT ALL, which is the figure
	 * `SporeCloudsChancePercentOnDeath` and `HellfireChancePercentOnDeath` hold
	 * for their rows. This row states no such chance -- "Killing enemies creates
	 * mushrooms" is unconditional, the way Withered Ground's "Enemies leave
	 * patches of Barren Earth on death" is -- so every creature death leaves one
	 * and this figure only decides which kind.
	 */
	static constexpr float FungalOvergrowthBoostChancePercent = 50.0f;

	/**
	 * How wide a mushroom is.
	 *
	 * A JUDGEMENT AND IT IS THE HOUSE FIGURE, declared as another rule's
	 * constant rather than as a number for the reason `BrandNovaRadiusCm` above
	 * gives. Withered Ground's patch, a Singularity Well, Infernal Rain's patch
	 * and the Gatekeeper's Soulfall are all 300 cm, and a mushroom is the same
	 * thing: a piece of ground a player stands on.
	 */
	static constexpr float FungalOvergrowthMushroomRadiusCm =
		WitheredGroundPatchRadiusCm;

	static_assert(
		FungalOvergrowthSpeedLessPercent > 0.0f
			&& FungalOvergrowthSpeedLessPercent < 100.0f,
		"A mushroom's slow is no longer a fraction of the speed a character "
		"walks at. At 100 it stops them dead, which the row does not ask for, "
		"and the pipeline floors a Less at -99 so the figure would stop meaning "
		"what it says.");

	static_assert(
		FungalOvergrowthSpeedMorePercent > 0.0f,
		"A boost of nothing is not a boost, and the row's promise of one is "
		"then unbuilt.");

	static_assert(
		FungalOvergrowthBoostChancePercent > 0.0f
			&& FungalOvergrowthBoostChancePercent < 100.0f,
		"Every mushroom is now the same kind, and the row's word 'either' "
		"describes nothing.");

	// A MUSHROOM THAT REACHES NOWHERE CAN BE STOOD ON BY NOBODY, so this row is
	// unbuilt at zero however right everything else is.
	//
	// AND NOT `== WitheredGroundPatchRadiusCm`, WHICH IS WHAT THIS ASSERTION
	// SAID FIRST. The constant is DECLARED as that one three lines above, so
	// comparing the two is A == A: an assertion no edit to this file could ever
	// make fail, which is worse than none because it reads like a guard. The
	// derivation is held instead by
	// `test_fungal_overgrowth_mushroom_radius_is_still_a_derivation` in
	// `tools/tests/test_dungeon_modifier_rules_are_the_rows.py`, which reads the
	// declaration as text and fails when it becomes a number -- the same way
	// Hellfire's and Brand of the Aggressor's derived radii are held, and each
	// of those asserts only that it is positive.
	static_assert(
		FungalOvergrowthMushroomRadiusCm > 0.0f,
		"A mushroom that reaches nowhere is a mushroom nobody can stand on.");

	/**
	 * What share of a floor's creatures are illusions, in percent.
	 *
	 * A JUDGEMENT, AND THE ROW STATES NOTHING. "Some enemies are illusions" is
	 * the whole of it. `tools/tests/test_dungeon_modifier_rules_are_the_rows.py`
	 * fails if the row ever states a figure of its own, at which point this
	 * stops being a judgement and has to be read off the row instead.
	 *
	 * ONE IN FOUR, AND THE REASONING IS THE ROW'S OWN SUBJECT. This rule works by
	 * uncertainty: it is worth something only while the player cannot tell which
	 * creature in front of them is real. That fails in both directions. At a
	 * large share most of what a player meets is harmless and the floor stops
	 * being dangerous; at a small one a player would finish a dungeon without
	 * meeting an illusion and the row would change nothing. A quarter keeps most
	 * enemies real and still puts illusions in front of a player on every floor.
	 *
	 * NOT THE TABLE'S 10, WHICH IS THE FIGURE THE TWO ROWS BEFORE THIS ONE TOOK.
	 * `SporeCloudsChancePercentOnDeath` and `HellfireChancePercentOnDeath` are
	 * both 10 because that is what `game/Data/DungeonModifiers.csv` uses for a
	 * CHANCE FIRED BY AN EVENT. This is not that: it is a share of a floor's
	 * population, decided once per creature as it is placed, and nothing in the
	 * table sets a precedent for one.
	 *
	 * DECIDED PER CREATURE AND NOT AS A COUNT PER FLOOR. A count would need to
	 * know how many creatures a floor holds before placing any of them, and it
	 * would make the number of illusions predictable to a player who counted.
	 */
	static constexpr float IllusoryEnemiesSharePercent = 25.0f;

	static_assert(
		IllusoryEnemiesSharePercent > 0.0f
			&& IllusoryEnemiesSharePercent < 100.0f,
		"At zero no creature is ever an illusion and the row is unbuilt; at a "
		"hundred every creature is one and a floor carrying this row cannot hurt "
		"the player at all, which 'Some enemies are illusions' does not ask "
		"for.");

	static_assert(
		HellfireChancePercentOnDeath > 0.0f
			&& HellfireChancePercentOnDeath < 100.0f,
		"Hellfire is a CHANCE on death. At zero it never happens and the row is "
		"unbuilt; at a hundred every kill explodes and the row's own word "
		"'chance' describes nothing.");

	static_assert(
		HellfireRadiusCm > 0.0f,
		"An explosion that reaches nowhere hits nobody.");

	static_assert(
		HellfireExplosionHits > 0.0f,
		"An explosion worth no blows deals nothing, which is the row unbuilt "
		"rather than the row tuned low.");

	static_assert(
		SporeCloudsChancePercentOnDeath > 0.0f
			&& SporeCloudsChancePercentOnDeath < 100.0f,
		"Spore Clouds is a CHANCE on death. At zero it never happens and the row "
		"is unbuilt; at a hundred it happens every time and the row's own word "
		"'chance' describes nothing.");

	static_assert(
		SporeCloudsReachCm > 0.0f,
		"Spores that reach nowhere poison nobody.");

	/**
	 * How many craters one bombardment leaves.
	 *
	 * A JUDGEMENT, DERIVED FROM AN EXISTING BOUND RATHER THAN CHOSEN. The row
	 * says "areas", plural, and gives no count. `InfernalRainMostPatches` is the
	 * most burning ground this game already allows on a floor at once, so a
	 * bombardment that leaves that many never puts more on the floor than a rule
	 * already shipped permits.
	 */
	static constexpr int32 HallowedGroundfallCraters = InfernalRainMostPatches;

	/**
	 * How long a crater burns.
	 *
	 * A JUDGEMENT, DERIVED FROM THE ROW'S OWN CADENCE: half of it. The row says
	 * the artillery comes every thirty seconds, and a crater lasting half that
	 * makes the floor alternate between dangerous and clear, which is what a
	 * bombardment describes.
	 *
	 * NEAREST EXISTING FIGURE, AND WHY IT IS NOT COPIED:
	 * `InfernalRainPatchSeconds` is 10, but that rule drops a patch every five
	 * seconds, so ITS floor is never clear. This one's is, for half of every
	 * cycle. The difference is the point of the row rather than an accident.
	 */
	static constexpr float HallowedGroundfallCraterSeconds =
		HallowedGroundfallSecondsBetween / 2.0f;

	/**
	 * How wide a crater is.
	 *
	 * A JUDGEMENT. The row says "craters" with no size word, where
	 * `War_Artillery_Strike` says "massive" and takes 600. This is
	 * `InfernalRainRadiusCm`, so three craters cover the same ground as the three
	 * patches the game already permits.
	 */
	static constexpr float HallowedGroundfallCraterRadiusCm = InfernalRainRadiusCm;

	/**
	 * What standing in a crater costs per second, as a share of maximum health.
	 *
	 * COPIED AS A CONCLUSION RATHER THAN DERIVED, and that is deliberate. These
	 * ARE burning ground, and `InfernalRainPercentPerSecond` is the intensity
	 * this project has already settled for it. What differs between the two rules
	 * is WHEN the ground burns, not how hot it is.
	 *
	 * THE CONSEQUENCE, SAID RATHER THAN HIDDEN: fifteen seconds at two percent is
	 * thirty percent of maximum health for standing in one crater for its whole
	 * life, against Infernal Rain's twenty. That is a judgement. It is defensible
	 * because this hazard is rare and avoidable where Infernal Rain's is
	 * constant, and `docs/DECISIONS.md` records it beside both figures.
	 */
	static constexpr float HallowedGroundfallPercentPerSecond =
		InfernalRainPercentPerSecond;

	/**
	 * How long the empowerment lasts once it is granted.
	 *
	 * A JUDGEMENT, DERIVED FROM THE BEAT AND NOT FROM TASTE. The rule steps four
	 * times a second, and this is re-applied on every beat a creature is standing
	 * in a crater, so one second is four beats of margin: a creature inside never
	 * flickers out, and one that walks out loses it promptly.
	 *
	 * THE STATUS ROW STATES NO DURATION. `Buff_Commander` in
	 * `game/Data/StatusEffects.csv` carries `DurationSeconds` of zero, so every
	 * caller supplies its own -- `UCataclysmEnemyModifiers` passes eight seconds
	 * for the buff a sacrifice grants. Re-applying on a clock is the Abyssal
	 * Aura's shape, which says why in terms: a second application refreshes the
	 * one already there rather than adding another.
	 */
	static constexpr float HallowedGroundfallEmpowerSeconds = 1.0f;

	/**
	 * How far from the player a crater can fall. The same figure Infernal Rain
	 * uses, and for its reason: ground that only ever appears underfoot is not
	 * ground to walk out of.
	 */
	static constexpr float HallowedGroundfallFallsWithinCm = InfernalRainFallsWithinCm;

	static_assert(
		HallowedGroundfallCraterSeconds < HallowedGroundfallSecondsBetween,
		"A crater now outlasts the gap between bombardments, so the floor would "
		"never be clear and the row's 'every 30 seconds' would describe nothing "
		"the player could feel.");

	static_assert(
		HallowedGroundfallEmpowerSeconds < HallowedGroundfallCraterSeconds,
		"The empowerment now outlasts the crater that grants it, so a creature "
		"would keep it after the ground it was standing on had gone.");

	static_assert(
		HallowedGroundfallCraters > 1,
		"The row says the artillery bombards AREAS, plural. One crater is not a "
		"bombardment.");

	static_assert(
		HallowedGroundfallFallsWithinCm > HallowedGroundfallCraterRadiusCm,
		"A crater can no longer fall anywhere the player is not already standing, "
		"so there would be nothing to walk out of.");

	static_assert(
		ArtilleryStrikeWarningSeconds < ArtilleryStrikeSecondsBetween,
		"A strike now takes longer to land than the gap between strikes, so the "
		"next would be called in before the last had landed. The rule keeps one "
		"circle at a time and would stall.");

	static_assert(
		ArtilleryStrikeWarningSeconds * 400.0f
			>= ArtilleryStrikeRadiusCm * 2.0f,
		"The warning is no longer twice the time it takes to walk out of the "
		"circle at the player's base speed of 400 cm per second. Either the "
		"radius grew or the warning shrank; re-derive the warning, and check it "
		"against the 40% slow Singularity Wells can be applying at the same "
		"time. docs/DECISIONS.md carries the derivation.");

	static_assert(
		ArtilleryStrikeMaxHealthPercent < 100.0f,
		"A strike now takes a player from full health to nothing in one hit, "
		"which is not what a lightest-weight-band row should do.");

	static_assert(
		ArtilleryStrikeLandsWithinCm > ArtilleryStrikeRadiusCm,
		"A strike can no longer be called in anywhere the player is not already "
		"standing, so the circle would always cover them and there would be "
		"nothing to walk out of.");

	static_assert(
		EdictOfSilenceLastsSeconds < EdictOfSilenceEverySeconds,
		"The Edict of Silence now lasts at least as long as the gap between "
		"silences, so the player is never able to use a skill. The row describes "
		"a silence that sweeps and passes, and 'every 90 seconds ... for 15' is "
		"a cycle with a quiet part.");

	static_assert(
		EdictOfSilenceLockValue > 0.0f,
		"The Edict of Silence's lock value is no longer above zero, so nothing "
		"is locked. Everything that reads the skill-lock stat asks only whether "
		"it is above zero.");

	static constexpr float GraspingTentaclesReachCm = 300.0f;
	static constexpr float GraspingTentaclesAppearWithinCm = 1200.0f;
	static constexpr int32 GraspingTentaclesMostOnAFloor = 5;
	static constexpr float GraspingTentaclesSecondsBetween = 8.0f;
	static constexpr float GraspingTentaclesGrabChancePercentPerBeat = 5.0f;
	static constexpr float GraspingTentaclesGrabSeconds = 1.5f;
	static constexpr float GraspingTentaclesGrabCooldownSeconds = 5.0f;
	static constexpr float GraspingTentaclesGrabMovementLessPercent = 99.0f;

	static_assert(
		GraspingTentaclesGrabMovementLessPercent
			<= -UCataclysmStatPipeline::LessMultiplierFloor,
		"A grab now asks for more speed than the stat pipeline will take. It "
		"floors a single Less at -99 and counts the clamp, so a larger figure "
		"would be silently reduced to 99 and this constant would stop saying "
		"what happens.");

	static_assert(
		GraspingTentaclesGrabMovementLessPercent < 100.0f,
		"A grab now stops the character dead rather than restricting their "
		"movement. The row says 'restricting their movement', and a character "
		"that cannot move at all is a stun -- which this rule deliberately is "
		"not, because a grabbed character can still act.");

	static_assert(
		GraspingTentaclesGrabCooldownSeconds > GraspingTentaclesGrabSeconds,
		"One tentacle can now grab again before its last grab has ended, so a "
		"player standing in a reach is held without a break. The cooldown being "
		"longer than the grab is what makes this repeated grabs rather than a "
		"permanent hold, which is the whole reading this rule was built to.");

	static_assert(
		GraspingTentaclesAppearWithinCm > GraspingTentaclesReachCm,
		"A tentacle can no longer appear clear of the player. It must be able "
		"to land outside its own reach, or it grabs the moment it appears and "
		"'careful of getting too close' describes nothing the player chose.");

	static constexpr float WastingSicknessChancePercentPerHit = 10.0f;
	static constexpr float WastingSicknessPercentPerStack = 3.0f;
	static constexpr int32 WastingSicknessMostStacks = 5;

	static_assert(
		WastingSicknessChancePercentPerHit > 0.0f
			&& WastingSicknessChancePercentPerHit < 100.0f,
		"Wasting Sickness no longer inflicts its debuff on a CHANCE. At 100 every "
		"landed blow inflicts a stack, which the row does not ask for -- it says "
		"'Enemies have a chance to inflict' -- and at 0 the row does nothing.");

	static_assert(
		WastingSicknessPercentPerStack
				* static_cast<float>(WastingSicknessMostStacks) < 100.0f,
		"Wasting Sickness at its cap now takes a character's entire maximum "
		"health and mana. The row describes a debuff to fight through and cure "
		"at a boss, not one that removes the character.");

	static constexpr float MortalDecayPercentPerSecondPerFloor = 0.1f;
	static constexpr float MortalDecayMostPercentPerSecond = 1.0f;
	static constexpr float MortalDecaySlowPercent = 50.0f;
	static constexpr float MortalDecaySlowSeconds = 5.0f;

	static_assert(
		MortalDecaySlowPercent > 0.0f && MortalDecaySlowPercent < 100.0f,
		"Mortal Decay's reward for a kill no longer slows the affliction. At 100 "
		"a kill stops it outright, which the row does not ask for -- it says "
		"'temporarily slow the effect' -- and at 0 the row's second sentence "
		"does nothing at all.");

	static_assert(
		MortalDecayMostPercentPerSecond <= SingularityWellsPercentPerSecond,
		"Mortal Decay now costs more per second than standing inside a "
		"Singularity Well. That was the whole argument for its ceiling: every "
		"other per-second cost in this file can be stopped by moving and this "
		"one cannot, so it sits at the bottom of the band. If it should cost "
		"more, say why beside the constant and delete this assertion rather "
		"than loosening it.");

	static_assert(
		MortalDecayPercentPerSecondPerFloor < MortalDecayMostPercentPerSecond,
		"Mortal Decay now reaches its ceiling on the first floor, so the row's "
		"'gradually ... as they progress through the dungeon' describes nothing "
		"a player could observe: every floor would sap at the same rate.");

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
	 * Whether Infernal Rain should drop a patch now, and what it should deal.
	 *
	 * TWO PLAIN FUNCTIONS RATHER THAN ARITHMETIC INSIDE THE BEAT, which is the
	 * shape every rule in this file already takes: the decision can then be
	 * tested by passing numbers in, with no world, no floor and no player.
	 *
	 * @param SecondsSinceLastPatch  how long since one last fell
	 * @param PatchesAlive           how many of this modifier's patches are still
	 *                               burning
	 * @return whether one falls on this beat
	 */
	static bool InfernalRainPatchIsDue(float SecondsSinceLastPatch,
									   int32 PatchesAlive);

	/**
	 * What one second in an Infernal Rain patch costs a character.
	 *
	 * A SHARE OF THE CHARACTER'S OWN MAXIMUM HEALTH, so the figure means the same
	 * thing at every level. The patch actor sweeps once a second -- its own
	 * `TickSeconds` is 1 -- so this is both the per-second share and the
	 * per-sweep damage, with no conversion to get wrong.
	 *
	 * @param MaximumHealth  the character's maximum health
	 * @return the damage one sweep deals, or zero for a maximum of nothing
	 */
	static float InfernalRainDamagePerSecond(float MaximumHealth);

	/**
	 * Whether a floor carrying Singularity Wells should place another one now.
	 *
	 * THE CAP IS ASKED BEFORE THE CLOCK, so a floor already carrying its limit
	 * does no arithmetic and, more to the point, does not swallow the clock: the
	 * caller keeps counting, so the beat a well is destroyed on places the next
	 * one at once rather than waiting a further eight seconds.
	 *
	 * AT OR PAST THE CADENCE, NOT PAST IT. The beat is a quarter of a second, so
	 * insisting on strictly past would put every well one beat later than the
	 * figure says for no reason anybody could observe.
	 *
	 * @param SecondsSinceLastWell  how long since one was last placed
	 * @param WellsAlive            how many are on the floor now
	 */
	static bool SingularityWellIsDue(float SecondsSinceLastWell, int32 WellsAlive);

	/**
	 * What one second inside a Singularity Well costs a character.
	 *
	 * A SHARE OF THE CHARACTER'S OWN MAXIMUM HEALTH, so the figure means the same
	 * thing at every level, which is what every dungeon modifier here does. The
	 * well sweeps once a second -- `ACataclysmGroundZone::TickSeconds` is 1 -- so
	 * this is both the per-second share and the per-sweep damage.
	 *
	 * NOT A SHARE OF AN ORDINARY HIT, which is the other burning-ground rule in
	 * this project. A creature prices its patch from `WeaponDamageOf` its own
	 * ability system; a floor hazard carries no attribute sets at all, because
	 * the damage calculation reads the defender's attributes and not the
	 * source's, so there is no hit to take a share of.
	 *
	 * @param MaximumHealth  the character's maximum health
	 * @return the damage one sweep deals, or zero for a maximum of nothing
	 */
	static float SingularityWellDamagePerSecond(float MaximumHealth);

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
	 * How many stacks of Embrace of Death a player has. Issue #41, slice 5.
	 *
	 * NONE FOR THE FIRST TEN SECONDS OF A FLOOR, then one every ten, capped. A
	 * player who takes the stairs promptly never carries one, which is the row's
	 * "stacks reset when entering a new floor" read as a reward for moving on
	 * rather than only as a mercy.
	 *
	 * THE STAIRS CLEAR THEM WITHOUT A CALL OF THEIR OWN, the way moving clears
	 * Forced March: the figure asked about is the time spent on THIS floor, and a
	 * new floor puts that back to nothing.
	 *
	 * @param SecondsOnFloor seconds the player has spent on this floor. Nothing
	 *        or less carries no stacks
	 */
	static int32 DeathsEmbraceStacksAfter(float SecondsOnFloor);

	/**
	 * What that many stacks take off each amount of healing, in percentage
	 * points. Issue #41, slice 5.
	 */
	static float DeathsEmbraceHealingLessPercent(int32 Stacks);

	/**
	 * How fast Mortal Decay saps a player on this floor, as a share of their
	 * maximum health per second. Issues #1786 and #41.
	 *
	 * THE FLOOR NUMBER AND NOT THE WALK. See `MortalDecayKey` for the standing
	 * rule that settles which of the two "as they progress through the dungeon"
	 * means.
	 *
	 * FLOOR 1 ALREADY COUNTS, because this shares `ShareTakenOnFloor` with the
	 * two per-floor rules rather than repeating their arithmetic, and that
	 * function is where the judgement lives.
	 *
	 * THE CAP IS TAKEN FIRST AND THE KILL SLOW SECOND. The other order would let
	 * the cap swallow the slow on any floor deep enough for the uncapped rate to
	 * exceed twice the ceiling, so from floor 20 down reaping would change
	 * nothing -- which is where the row most needs it to.
	 *
	 * @param FloorNumber     counted from 1. Zero or below saps nothing
	 * @param bSlowedByAKill  whether a kill's window is still running
	 * @return the share of maximum health lost each second, in percent
	 */
	static float MortalDecayPercentPerSecond(int32 FloorNumber,
											 bool bSlowedByAKill);

	/**
	 * What that many stacks of Wasting Sickness take off each of the player's two
	 * maximums, in percent. Issues #1786 and #41.
	 *
	 * ONE FIGURE FOR BOTH MAXIMUMS, because the row states one: "reduces your max
	 * HP and max mana". `StatModifiersFor` turns it into two Less multipliers.
	 *
	 * CLAMPED HERE AS WELL AS WHERE STACKS ARE COUNTED, because this is public
	 * and a caller holding a count from somewhere else must not be able to ask
	 * for more than the cap. Death's Embrace's pair makes the same argument.
	 */
	static float WastingSicknessMaximumsLessPercent(int32 Stacks);

	/**
	 * How many stacks of Wasting Sickness a player carries after a blow lands,
	 * given what they carried and whether the chance came up.
	 *
	 * A PURE FUNCTION OF THE TWO, so the rule can be checked by passing numbers
	 * in and the caller keeps the random draw. That is what lets a test assert
	 * the cap and the refusal without making a roll come up.
	 *
	 * @param Stacks    what the player carried before the blow. Below nothing
	 *                  counts as nothing
	 * @param bInflicts whether this blow's chance came up
	 */
	static int32 WastingSicknessStacksAfterHit(int32 Stacks, bool bInflicts);

	/**
	 * Whether a floor carrying Grasping Tentacles should put another one down.
	 * Issues #1786 and #41.
	 *
	 * THE CAP IS ASKED BEFORE THE CLOCK, the shape `SingularityWellIsDue` uses
	 * and for its reason: a floor already carrying its limit does no arithmetic
	 * and does not swallow the clock, so the beat one is destroyed on places the
	 * next at once.
	 *
	 * @param SecondsSinceLast  how long since one was last placed
	 * @param Alive             how many are on the floor now
	 */
	static bool GraspingTentacleIsDue(float SecondsSinceLast, int32 Alive);

	/**
	 * Whether the Edict of Silence should begin now. Issues #1786 and #41.
	 *
	 * NO CAP TO ASK ABOUT, unlike the two hazard rules whose "is due" functions
	 * check one first: a silence is a state rather than an actor, so there is
	 * nothing to count.
	 *
	 * AT OR PAST THE CADENCE, NOT PAST IT, for the reason every other cadence
	 * here gives: the beat is a quarter of a second, so insisting on strictly
	 * past would put every silence one beat later than the row says for no reason
	 * anybody could observe.
	 *
	 * @param SecondsSinceLast  how long since the last silence BEGAN. A negative
	 *        wait is "no clock to read" and brings nothing
	 */
	static bool EdictOfSilenceIsDue(float SecondsSinceLast);

	/**
	 * What the skill-lock stat is worth while a silence is running, or nothing
	 * while it is not. Issues #1786 and #41.
	 *
	 * A FUNCTION OF WHETHER IT IS RUNNING AND NOTHING ELSE. Every silence is the
	 * same: the row describes one that prevents all skill usage, not one that
	 * prevents more of it at depth or after a while.
	 */
	static float SkillsLockedWhile(bool bSilenced);

	/**
	 * Whether to call in a strike this beat.
	 *
	 * ONE CIRCLE AT A TIME, ASKED BEFORE THE CLOCK, which is the shape
	 * `GraspingTentacleIsDue` uses and for the same reason: while one is in the
	 * air no arithmetic is done and the caller keeps counting, so the beat it
	 * lands on can place the next without waiting a further cadence.
	 *
	 * @param SecondsSinceLast  how long since the last circle was placed
	 * @param bOneInTheAir      whether a circle is on the ground now
	 */
	static bool ArtilleryStrikeIsDue(float SecondsSinceLast, bool bOneInTheAir);

	/** Whether the circle placed this long ago has been landed on yet. */
	static bool ArtilleryStrikeHasLanded(float SecondsSinceItAppeared);

	/**
	 * What one strike takes off something with this much maximum health.
	 *
	 * A SHARE AND NOT A FIGURE, for the reason `InfernalRainDamagePerSecond`
	 * gives where it does the same: a hazard the floor applies has no weapon and
	 * no level, so a flat number would be trivial at one depth and lethal at
	 * another.
	 *
	 * ANSWERS ZERO FOR A TARGET WHOSE MAXIMUM HEALTH IS UNKNOWN, so a caller
	 * that cannot read one does nothing rather than applying a damage of zero
	 * that still counts as a hit.
	 */
	static float ArtilleryStrikeDamage(float MaximumHealth);

	/** Whether a bombardment is due this beat. */
	static bool HallowedGroundfallIsDue(float SecondsSinceLast);

	/**
	 * What a crater takes per second from something with this much maximum
	 * health.
	 *
	 * A SHARE AND NOT A FIGURE, for the reason `InfernalRainDamagePerSecond`
	 * gives where it does the same: ground the floor lays has no weapon and no
	 * level, so a flat number would be trivial at one depth and lethal at
	 * another. ANSWERS ZERO FOR AN UNKNOWN MAXIMUM, so a caller that cannot read
	 * one lays no crater rather than one that burns for nothing.
	 */
	static float HallowedGroundfallBurnPerSecond(float MaximumHealth);

	/**
	 * Whether a death releases spores, given the roll it drew.
	 *
	 * BELOW AND NOT AT OR BELOW: a roll is drawn from 0 up to 100, so "below 10"
	 * is one chance in ten and "at or below" would be a hair more. Every other
	 * chance comparison in the built rules makes the same one --
	 * `ACataclysmDungeonGameMode::NoteHitForWastingSickness` writes
	 * `DungeonGameModeWastingSicknessRoll() < WastingSicknessChancePercentPerHit`,
	 * and `HellfireExplodes` below compares the same way.
	 *
	 * THIS SAID "The one other chance comparison" UNTIL `Demonic_Hellfire` MADE IT
	 * THREE. Corrected by the change that stopped it being true rather than left
	 * to be found.
	 */
	static bool SporeCloudsRelease(float Roll);

	/** Whether spores released this far from a target reach it. */
	static bool SporeCloudsReach(float DistanceCm);

	/**
	 * Whether a death explodes, given the roll it drew.
	 *
	 * BELOW AND NOT AT OR BELOW, for the reason `SporeCloudsRelease` above gives.
	 */
	static bool HellfireExplodes(float Roll);

	/**
	 * What the explosion deals, given the dying creature's own attack damage.
	 *
	 * ANSWERS ZERO FOR A CREATURE THAT DEALS NOTHING, rather than a negative or a
	 * tiny positive. `UCataclysmSkillEffects::ApplyDirectDamage` refuses an amount
	 * at or below zero anyway; answering zero here says so where it can be read.
	 */
	static float HellfireDamage(float CreatureAttackDamage);

	/**
	 * The brand count after a blow, given what it was and whether the blow
	 * branded.
	 *
	 * IT RETURNS TO ZERO ON THE STACK THAT ERUPTS rather than sitting at the
	 * threshold. "At 20 stacks, you erupt" describes something that happens once
	 * per twenty; left at twenty, every later blow would erupt again.
	 *
	 * THE SHAPE `WastingSicknessStacksAfterHit` ALREADY HAS -- a pure function of
	 * the old count and one boolean -- so the arithmetic can be tested without a
	 * world, which is what makes the boundary at twenty checkable at all.
	 */
	static int32 BrandStacksAfterHit(int32 Stacks, bool bBrands);

	/** Whether a count that has just been raised is the one that erupts. */
	static bool BrandErupts(int32 StacksBeforeThisBlow);

	/**
	 * What the nova deals, given the player's maximum health.
	 *
	 * ANSWERS ZERO FOR A PLAYER WHOSE MAXIMUM HEALTH IS UNKNOWN, which is the
	 * guard `ArtilleryStrikeDamage` above makes and for the same reason: a blow
	 * that deals nothing still announces itself and would read in a log as a nova
	 * that landed for nothing.
	 */
	static float BrandNovaDamage(float MaximumHealth);

	/**
	 * Whether a mushroom just created is the kind that helps.
	 *
	 * BELOW AND NOT AT OR BELOW, the same comparison `SporeCloudsRelease` and
	 * `HellfireExplodes` make, so a roll of exactly the chance falls on the
	 * other side and a pinned 0 always helps.
	 *
	 * IT DECIDES THE KIND AND NEVER WHETHER THERE IS A MUSHROOM. Every creature
	 * death leaves one; see `FungalOvergrowthBoostChancePercent`.
	 */
	static bool FungalOvergrowthBoosts(float Roll);

	/** Whether a creature just placed on the floor is an illusion. */
	static bool IllusoryEnemiesIsAnIllusion(float Roll);

	/**
	 * What a grab takes off the character's speed, in percent, or nothing when
	 * no grab is holding. Issues #1786 and #41.
	 *
	 * A FUNCTION OF WHETHER A GRAB IS RUNNING AND NOTHING ELSE, so the rule can
	 * be checked by passing a bool rather than by building a world. Every grab is
	 * worth the same: the row describes being grabbed, not being grabbed harder.
	 */
	static float GraspMovementLessPercentWhile(bool bGrabbed);

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
