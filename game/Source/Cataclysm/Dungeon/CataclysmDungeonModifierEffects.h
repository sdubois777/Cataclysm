// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmContagion.h"
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
	 * How much more armour the player carries for the commanders they have killed, in
	 * percent. March of Progress. Issues #1820 and #41.
	 *
	 * ITS OWN FIELD, NOT A SHARE OF ONE ALREADY HERE. No other rule writes armour, so
	 * nothing forced this; the reason is that a shared field is what makes two rules
	 * silently overwrite each other, which is the hazard `SetPlacedDamageMultiplier`
	 * carries the same warning about on the creature side. Adding a field costs nothing.
	 *
	 * IT SURVIVES A FLOOR CHANGE, ALONE AMONG THE FIELDS HERE. Every other value in this
	 * struct is worked out from the floor being stood on or from where the player is
	 * standing, so a new floor starts it again. This one is the run's count of commanders
	 * killed, and `ACataclysmDungeonGameMode::LeaveEmpireDungeon` is the only thing that
	 * clears it.
	 *
	 * A More MULTIPLIER ON `armor`, WHICH IS NOT THE SAME AS 10% LESS DAMAGE TAKEN.
	 * `UCataclysmDamageCalculation::ArmorReduction` is 100 x Armor / (Armor + K) capped
	 * at 75, so armour has diminishing returns of its own and 10% more armour is always
	 * worth less than 10% of a hit. The row says "increase the player's armor by 10%",
	 * which is what this does.
	 *
	 * IT IS WORTH NOTHING TO A CHARACTER WITH NO ARMOUR, and that is a real difference
	 * between classes rather than a fault. `game/Data/ClassStats.csv` gives an armour
	 * line to the Ravager and the Masochist and to no other class, so every other class
	 * carries armour only from gear, and a More multiplying a base of nothing is nothing.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	float ArmourMorePercent = 0.0f;

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
	 * How much less of ONE resistance the player has, in percent: the Judgment
	 * stacks Holy Repercussions leaves on them. Issues #1820 and #41.
	 *
	 * THE FIRST FIELD HERE THAT MOVES ONE RESISTANCE RATHER THAN ALL EIGHT, and
	 * that is the whole reason it is a new field. `ResistanceLessPercent` and
	 * `ResistanceMorePercent` above are each applied inside a loop over every
	 * damage type and each is documented as "of every resistance". Carrying a
	 * type on one of them would mean a branch inside that loop and a change to
	 * how The Nihil's Embrace's own two values are applied -- a shipped rule
	 * altered for a new one's convenience. This is one stat, written once,
	 * outside the loop.
	 *
	 * WHICH RESISTANCE IS NOT A FIELD, because the row names it and only this
	 * rule writes this. `HolyRepercussionsResistance` is the damage type, and
	 * `UCataclysmItemModifiers::ResistanceStatFor` turns it into the stat name
	 * -- the same call the loop above makes, so a renamed damage type moves both
	 * together.
	 *
	 * NAMED FOR ITS SOURCE, like `SicknessMaxHealthLessPercent` and
	 * `GraspMovementLessPercent` and for the same reason: a second field that
	 * moves a stat another field already moves cannot be named after the stat.
	 *
	 * IT GROWS ON A BLOW AND IS GONE AT THE STAIRS, so it is worked out on the
	 * beat rather than once a floor.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	float JudgmentResistanceLessPercent = 0.0f;

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

	/**
	 * Whether the player's SPELLS are locked, and by how much. Anti-Magic Zones. Issues
	 * #1820 and #41.
	 *
	 * THE SAME STAT AS `SkillsLockedValue` AND A FIELD OF ITS OWN, because the two differ
	 * in what they reach. That one is written UNSCOPED and reaches every skill; this one
	 * is written with `RequiredTags` set to `Type.Spell` and reaches only spells. Sharing
	 * a field would mean whichever rule wrote second decided the scope for both, which is
	 * issue #1765's fault in another shape. A floor carrying both rows gives
	 * `skill_locked` two entries, and a skill asks whether their sum is above zero.
	 *
	 * NOT A PERCENTAGE, for the reason `SkillsLockedValue` gives: it is the VALUE of
	 * `skill_locked`. ON AND OFF AS THE PLAYER WALKS IN AND OUT OF A ZONE.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	float SpellsLockedValue = 0.0f;

	/**
	 * The share of current health a cast pays instead of its mana while mana is
	 * low. Desperate Measures. Issues #1820 and #41.
	 *
	 * ONCE A FLOOR, NOT ON THE BEAT: the floor carries the row or it does not,
	 * and whether mana is low right now is judged at each cast by the condition
	 * the modifier carries. `DesperateMeasuresManaBelowPercent` is that
	 * condition's threshold and is not a field, because only this rule writes it.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	float ManaCostAsCurrentHealthPercent = 0.0f;

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
			&& JudgmentResistanceLessPercent <= 0.0f
			&& SkillsLockedValue <= 0.0f
			&& SpellsLockedValue <= 0.0f
			&& ManaCostAsCurrentHealthPercent <= 0.0f
			// AND THE ONE FIELD HERE THAT IS A REWARD RATHER THAN A LOSS. Issues #1820
			// and #41. March of Progress' armour is still something the floor is doing
			// to the player, so a floor carrying it is not empty.
			&& ArmourMorePercent <= 0.0f;
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
	 * Holy Repercussions: "Enemies have a chance to retaliate with radiant
	 * bursts upon being hit, dealing damage in an area and inflicting 'Judgment'
	 * (a stacking debuff that increases holy damage taken)." Issues #1820 and
	 * #41.
	 *
	 * THE ROW STATES NO FIGURE AT ALL -- not the chance, the damage, the reach,
	 * the cap, or what a stack is worth. All five are judgements and each is
	 * named beside its constant, with the precedent it follows or the absence of
	 * one. `docs/DECISIONS.md` carries the same five.
	 *
	 * IT IS THE PLAYER'S BLOW THAT PROVOKES IT, which is Brand of the
	 * Aggressor's direction and the opposite of Wasting Sickness's. "upon being
	 * hit" is the creature being hit, so the listener tests that the player is
	 * the attacker and a creature is the target.
	 *
	 * THE BURST IS THE CREATURE'S OWN AND REACHES THE PLAYER, which is why it
	 * asks `FindEnemiesInSphere` with the creature as instigator: a creature's
	 * enemies are the player's side. `Demonic_Brand_of_the_Aggressor` asks
	 * `FindAlliesInSphere` because its row names the player's own side, and
	 * `Demonic_Hellfire` asks `FindEveryoneInLine` because its row names nobody.
	 * Three rows, three searches, each read off its own sentence.
	 *
	 * AN ILLUSION RETALIATES FOR NOTHING, and no code here knows that. The burst
	 * is the creature's own attack damage, `Chaos_Illusory_Enemies` makes that
	 * zero, and `ApplyDirectDamage` of nothing is nothing. A floor carrying both
	 * rows needs nothing written for the pair.
	 */
	static const TCHAR* HolyRepercussionsKey;

	/**
	 * Which resistance Judgment lowers.
	 *
	 * THE ROW'S OWN WORD. "increases holy damage taken" -- holy is Celestial in
	 * this game's vocabulary, which is the Cataclysm type this row belongs to,
	 * so the row and its own column agree.
	 *
	 * A DAMAGE TYPE AND NOT A STAT NAME. `UCataclysmItemModifiers::
	 * ResistanceStatFor` builds `resistance_celestial` from it, which is the
	 * same call `StatModifiersFor`'s all-resistance loop makes, so a damage type
	 * renamed in the design workbook moves this with it rather than leaving a
	 * stat name nothing writes.
	 */
	static const TCHAR* HolyRepercussionsResistance;

	/**
	 * Leech Spores: "When you kill an enemy, a cloud of Leech Spores explodes
	 * from their corpse. If you come into contact with the spores, a small
	 * portion of your health is drained and used to heal any enemies within a
	 * 10-meter radius." Issues #1820 and #41.
	 *
	 * THREE THINGS ABOUT IT WERE DECIDED BEFORE THIS RULE EXISTED, and none is
	 * a judgement made here. `docs/DECISIONS.md` records, for the change that
	 * built `ACataclysmGroundZone::SpawnForTheFloor`, that this row is one of the
	 * hazards that "state no duration at all" and so lasts the floor; that the
	 * floor hazard source owns its cloud, because a hazard placed "from their
	 * corpse" cannot be owned by a creature that is already dead; and that it
	 * acts differently on the player and on creatures.
	 *
	 * ONE READING IN THAT RECORD IS SUPERSEDED FOR THIS ROW. It grouped Leech
	 * Spores with hazards acting on "creatures standing in the same patch". The
	 * row's own words are "any enemies within a 10-meter radius", and a cloud is
	 * 300 cm wide, so the two name different creatures. The row's words win;
	 * the grouping was a summary of a family of rows, not a reading of this one.
	 *
	 * A DRAIN AND NOT A BLOW. The player's health is taken through
	 * `UCataclysmSkillEffects::ReduceHealthDirectly`, which no armour, resistance
	 * or evasion reduces and which is NOT announced as a hit -- so a floor
	 * carrying a rule that listens for blows does not count the drain as one.
	 * `War_Forced_March` and `Death_Mortal_Decay` take health the same way.
	 *
	 * HEALTH MOVES AND NONE IS CREATED. What heals the creatures is what
	 * actually left the player, measured before and after, which is less than
	 * the share asked for when the player has less health than that share. The
	 * creatures divide it equally; a creature already at its maximum takes its
	 * share and wastes it, because `UCataclysmRegeneration::TopUp` caps there.
	 */
	static const TCHAR* LeechSporesKey;

	/**
	 * Blood Altar: "Slaying enemies contributes to the blood altar. The altar sends
	 * out damaging pulses that grow stronger with the number of enemies slain."
	 * Issues #1820 and #41.
	 *
	 * EVERY CREATURE DEATH ON THE FLOOR FEEDS IT, including deaths the player did
	 * not cause. "Slaying enemies" names no killer, which is the rule
	 * `docs/DECISIONS.md` records for Fungal Overgrowth's "Killing enemies" and
	 * Withered Ground's "on death". That is the OPPOSITE of Leech Spores above,
	 * whose row says "When you kill an enemy" and so counts only the player's kills.
	 *
	 * ONE ALTAR A FLOOR, ON THE EXIT CELL, which is where the stairs are placed.
	 * Nothing is drawn at the altar's centre, so "at" and "beside" the stairs are
	 * the same place. Its reach is drawn as a ring lasting the floor, and the ring is
	 * the only warning a pulse gets.
	 *
	 * A PULSE HITS THE PLAYER'S PAWN AND NOTHING ELSE. A pulse that struck creatures
	 * could kill them, and every death it caused would make the next pulse stronger.
	 *
	 * DEMONIC DAMAGE DEALT FROM THE FLOOR, the way Artillery Strike's shell is:
	 * `ApplyDirectDamage` from the floor hazard source as an area hit. So the
	 * player's Demonic resistance applies, and a rule listening for hits sees a blow
	 * the floor dealt rather than a creature's. The pulse carries this row's type
	 * on its own delivery, as every floor rule's damage does since issue #1924.
	 */
	static const TCHAR* BloodAltarKey;

	/**
	 * Necrotic Ground: "The dungeon floor is covered in a spreading necrotic fog. The
	 * fog deals damage over time and reduces your healing effectiveness by 50% while
	 * standing in it. Enemies standing in the fog regen their health at 10%/s."
	 * Issues #1820 and #41.
	 *
	 * THE FOG IS PATCHES THAT SPREAD. The first appears one cadence into the floor,
	 * near the player but not on them; each later one touches a random patch already
	 * there; the floor holds at most `NecroticGroundMostPatches`, and they last the
	 * floor.
	 *
	 * STANDING IN ANY PATCH DOES TWO THINGS TO THE PLAYER. Health restored is cut by
	 * the row's 50 points, which add to Death's Embrace's in the same stat, and once a
	 * second the fog burns for a share of maximum health as Death damage. THE RULE
	 * DEALS THE BURN, NOT EACH PATCH'S SWEEP, so a player where patches overlap loses
	 * the one figure rather than one for each patch.
	 *
	 * A CREATURE STANDING IN ANY PATCH, bosses included, regains the row's 10% of its
	 * own maximum health a second, counted once however many patches cover it.
	 */
	static const TCHAR* NecroticGroundKey;

	/**
	 * The row whose creatures grow stronger the longer they stay alive. Issues #1820
	 * and #41.
	 *
	 * EACH CREATURE COUNTS ITS OWN TIME ALIVE, from the first beat that finds it, and
	 * holds a stack for every `RavenousHoardSecondsPerStack` of it, up to
	 * `RavenousHoardMostStacks`. Each stack adds `RavenousHoardDamagePercentPerStack`
	 * of its own base to its attack damage. Nothing else about the creature changes,
	 * its health least of all: see
	 * `ACataclysmEnemyCharacter::SetTimeAliveDamageMultiplier`, which was called
	 * `SetFloorRuleDamageMultiplier` until Grave Tide added a second source.
	 */
	static const TCHAR* RavenousHoardKey;

	/**
	 * The row whose waves of creatures rise through the floor. Issues #1820 and #41.
	 *
	 * A WAVE EVERY `GraveTideSecondsBetweenWaves`, each one creature larger than the
	 * last and each creature placed with a damage multiplier a step above the last
	 * wave's, up to `GraveTideMostWaves` waves. The creatures are the ones the floor's
	 * own populator picks; the row's "undead" names no creature this game has.
	 */
	static const TCHAR* GraveTideKey;

	/**
	 * The row whose wounded creatures turn into a rarer kind. Issues #1820 and #41.
	 *
	 * A CREATURE BELOW `VolatileEvolutionHealthPercentToMutate` OF ITS MAXIMUM HEALTH
	 * HAS `VolatileEvolutionChancePercent` ON EACH BEAT of rising
	 * `VolatileEvolutionRungsGained` rung of the rarity ladder. It rises once and never
	 * past `VolatileEvolutionHighestRung`, and rising re-reads its whole stat block and
	 * draws the modifiers its new rung carries.
	 *
	 * SO IT IS WORTH MORE WHEN IT DIES, WITH NOTHING WRITTEN TO MAKE THAT SO.
	 * `UCataclysmDropSpawner::SpawnDropsFor` and the experience grant beside it in
	 * `ACataclysmEnemyCharacter`'s death handler both read the rarity step the creature
	 * holds at the moment it dies.
	 *
	 * THE HEALTH AND ENERGY SHIELD IT HAD ARE PUT BACK AFTERWARDS.
	 * `ACataclysmEnemyCharacter::SetRarityStep` and `DrawModifiersForRarity` both end by
	 * calling `ApplyStartingAttributes`, which refills both pools, and a mutation that
	 * healed the creature would undo the work that wounded it.
	 */
	static const TCHAR* VolatileEvolutionKey;

	/**
	 * The row whose wounded creatures call two guards of their own kind. Issues #1820
	 * and #41.
	 *
	 * A CREATURE OF `RoyalGuardLowestRungThatSummons` OR ABOVE, BELOW
	 * `RoyalGuardHealthPercentToSummon` OF ITS MAXIMUM HEALTH, HAS
	 * `RoyalGuardChancePercent` ONCE of calling `RoyalGuardGuardsSummoned` creatures of
	 * its own kind, a rung above itself and never past `RoyalGuardHighestRung`.
	 *
	 * "ABOVE UNCOMMON RANKED" NAMES NO RUNG THIS GAME HAS. `game/Data/EnemyRarities.csv`
	 * is Common, Elite, Legendary, Herald, Boss and Cataclysm Boss; the only Uncommon in
	 * the game's data is the second crafting material tier in
	 * `game/Data/MaterialTiers.csv`, and gear uses a third vocabulary again. Ruled to
	 * mean every rung above the bottom one, so Elite and above. THE PROJECT OWNER HAS
	 * SINCE CHOSEN TO REWORD THE ROW to "above Common ranked", which means the same
	 * rung and leaves `RoyalGuardLowestRungThatSummons` where it is; the row's text
	 * lives in the design workbook and the reword lands in a later change.
	 *
	 * THE GUARDS ARE ORDINARY CREATURES. Each drops loot and pays experience by its own
	 * rung when it dies, from its own death handler, so a floor carrying this row is
	 * worth more than one without it. That follows from the row rather than from a
	 * choice, and the entry says so.
	 */
	static const TCHAR* RoyalGuardKey;

	/**
	 * The row where a creature the PLAYER kills can bring a greater one out of its corpse.
	 * Issues #1820 and #41.
	 *
	 * A KILL THE PLAYER MADE HAS `DemonPrinceChancePercent` OF BRINGING ONE, at most
	 * `DemonPrincesPerFloor` a floor. What rises is a creature of the slain one's own kind
	 * at `DemonPrinceRung`, standing where it died.
	 *
	 * "A DEMONIC PRINCE" NAMES NO CREATURE THIS GAME HAS. The seven kinds a floor places
	 * are the Imp, the Hellhound, the Brute, the Abyssal Warden, the Corrupted Sentinel,
	 * the Succubus and the Gatekeeper. Ruled: the slain creature's own kind, raised to the
	 * rung below the first boss rung, until the project owner names a creature for it.
	 *
	 * THE KILL MUST BE THE PLAYER'S OWN, WHICH IS TWO TESTS AND NOT ONE. The row says
	 * "when you slay an enemy". A minion's blow is credited to its summoner --
	 * `FCataclysmHitNotice::Attacker` says so in `CataclysmCombatEvents.h` -- so the
	 * killer being the player is true of a minion's kill as well. The rule also requires
	 * the blow's dealer not to be a minion, which is the field that tells them apart.
	 * Ruled under the project owner's decision that a minion's hits are the minion's own;
	 * the Conduit keystone may change that, and this rule reads the dealer, so it would
	 * follow whatever that change decides.
	 */
	static const TCHAR* DemonPrinceKey;

	/**
	 * The row where a disease passes from a corpse to the creature beside it, and a chain
	 * of them kills a whole group. Issues #1820 and #41.
	 *
	 * A KILL THE PLAYER MADE, ON A CREATURE CARRYING A DEBUFF THAT CAN SPREAD, HAS
	 * `EpidemicSpreadChancePercent` of putting ALL of that creature's spreadable debuffs
	 * on the nearest creature within `EpidemicRadiusMetres`. Each one that lands adds to
	 * the floor's chain; a roll that misses sets the chain back to nothing.
	 *
	 * A CREATURE CARRYING NOTHING IS NOT DISEASED AND NOTHING IS ROLLED, so the chain is
	 * left where it is rather than broken. That is a different case from a roll that
	 * misses, and the two are ruled differently on purpose.
	 *
	 * AT `EpidemicSpreadsToKill` THE CHAIN ENDS ITSELF: every creature within the same
	 * reach dies, one Plague Lord rises, and the chain goes back to nothing.
	 *
	 * THOSE ARE REAL DEATHS AND THEY PAY. The rule writes each creature's health to zero
	 * and calls `HandleDeath`, which is the pair `UCataclysmHealthDebt` uses, so the loot
	 * roll and the experience grant in the creature's own handler both run. Marking a
	 * creature dead instead would announce the death and pay nothing.
	 *
	 * "PLAGUE LORD" NAMES NO CREATURE THIS GAME HAS, so it is a creature of the last
	 * victim's kind at the same ceiling the other rules use, `EpidemicPlagueLordsPerFloor`
	 * a floor, until the project owner names one. The reading Grave Tide's "undead",
	 * Royal Guard's "Uncommon" and Demon Prince's "demonic prince" all needed.
	 */
	static const TCHAR* EpidemicKey;

	/**
	 * The row where an Elite grows on the deaths of the creatures around it. Issues
	 * #1820 and #41.
	 *
	 * A CREATURE THAT DIES WITHIN `BloodForgedChampionsRadiusMetres` OF AN ELITE FEEDS
	 * THE NEAREST ONE. At `BloodForgedChampionsDeathsPerRung` fed, that creature rises
	 * one rung of the rarity ladder and its tally starts again. It rises from
	 * `BloodForgedChampionsLowestRung` and never past `BloodForgedChampionsHighestRung`.
	 *
	 * "TRANSFORMING INTO A MINI-BOSS" IS NOT A JUDGEMENT HERE, WHICH IS WHY THIS ROW WAS
	 * CHOSEN. `ACataclysmEnemyCharacter.h` says of the rarity ladder that "Herald, at 3,
	 * is deliberately below the line -- the Abyssal Warden's reference rarity is Herald
	 * and it is a mini-boss", and elsewhere that a floor rule may reach "Herald, so a
	 * floor rule cannot make a boss out of an ordinary creature". So the row's own last
	 * words land on the ceiling the other three rules already share, and this rule needs
	 * no fifth meaning of any word.
	 *
	 * NO KILLER IS ASKED FOR, AND THAT IS DELIBERATE. Volatile Evolution, Royal Guard,
	 * Demon Prince and Epidemic all ask "did the player do this". This row does not: it
	 * says "nearby dying allies" and names no killer, so a creature killed by another
	 * creature, by burning ground, or by another floor rule feeds a champion exactly as
	 * the player's own kill does. Ruled under the project owner's delegation. A check on
	 * `Notice.Killer` here would be a fifth rule's habit carried into a row that does not
	 * have it.
	 *
	 * THE HEALTH AND ENERGY SHIELD IT HAD ARE PUT BACK AFTERWARDS, for the reason
	 * `VolatileEvolutionKey` above gives at length: `SetRarityStep` and
	 * `DrawModifiersForRarity` both end in `ApplyStartingAttributes`, which refills both
	 * pools, and a champion that healed itself every third death would undo the work the
	 * player had done on it.
	 */
	static const TCHAR* BloodForgedChampionsKey;

	/**
	 * The row where a kill of the player's may stand back up as something that hunts it.
	 * Issues #1820 and #41.
	 *
	 * A CREATURE THE PLAYER KILLS HAS `VengefulWraithsChancePercent` OF LEAVING A WRAITH
	 * of its own kind where it fell. A wraith takes
	 * `VengefulWraithsDamageReductionMore` off every hit as ONE multiplicative source,
	 * carries `VengefulWraithsIncreasePercent` more attack damage, attack speed and
	 * movement speed than its kind would, and notices a target from
	 * `VengefulWraithsSightMultiplier` times its own sight.
	 *
	 * THE ROW'S KEY IS SPELT `Death_Vengful_Wraiths`, WITHOUT THE SECOND `e`. That is the
	 * design data's spelling and the key has to match it exactly; the constants here are
	 * spelt properly. `test_every_row_key_the_rules_name_is_a_row_of_the_table` is what
	 * catches the two drifting apart.
	 *
	 * "THE ONE WHO KILLED THEM" DECIDES WHETHER A WRAITH RISES, NOT WHOM IT CHASES. The
	 * creature AI picks targets by sight and nothing in the module takes a named quarry,
	 * so a wraith does not hold a grudge against a particular actor. What the sight
	 * multiplier buys is that it notices a target from anywhere on the floor, which is
	 * what the row describes in play. So the rule asks who killed the creature, and a
	 * kill that was not the player's raises nothing at all.
	 *
	 * A MINION'S KILL WITHOUT THE CONDUIT KEYSTONE RAISES NOTHING, and that is deliberate
	 * rather than an oversight. `UCataclysmCombatEvents::NoteBlow` credits a minion's
	 * blow to the minion unless its summoner holds Conduit, issue #1515, so asking
	 * `Notice.Killer` is already the whole question. Reading a minion's kill as the
	 * summoner's here would put back by hand the credit that change took away and would
	 * leave the keystone meaning nothing in this rule.
	 */
	static const TCHAR* VengefulWraithsKey;

	/**
	 * The row where radiant ground punishes standing still and pays for it. Issues #1820
	 * and #41.
	 *
	 * A ZONE APPEARS EVERY `JudgmentZonesSecondsBetweenZones`, up to
	 * `JudgmentZonesMostZones` at once, each lasting `JudgmentZonesSeconds` and reaching
	 * `JudgmentZonesRadiusCm`. A player standing in one takes
	 * `JudgmentZonesPercentPerSecond` of their maximum health for the first second and one
	 * step more for each further second, never past `JudgmentZonesMostPercentPerSecond`.
	 * Stepping out sets that back to nothing. At `JudgmentZonesTriggersForTheBonus` the
	 * floor grants `JudgmentZonesMagicFind` on a boss kill for the rest of the floor.
	 *
	 * "HOLY DAMAGE" NAMES NO DAMAGE TYPE THIS GAME HAS -- the eight are the eight
	 * Cataclysms -- AND NOTHING HAD TO RULE ON IT. The four built zone rules read the type
	 * off the row rather than writing one, and `StepInfernalRain` says why: "a row retyped
	 * in the workbook retypes its hazard with no code change. A constant here would be this
	 * file's opinion of the data." This row's own `CataclysmType` is Celestial, so the
	 * mechanism answers the question.
	 *
	 * THE RULE DEALS THE DAMAGE AND COUNTS THE TRIGGERS, AND THE ZONE CARRIES NEITHER.
	 * A zone's damage is fixed when it is spawned and it ticks on its own second, so a rule
	 * that owned the count while the zone owned the damage would have two clocks for one
	 * figure and they could disagree by up to a second. The zone is spawned with no damage
	 * -- which issue #1701 made possible so Singularity Wells could have a well that slows
	 * without damaging -- and is the ground the player sees and stands in. So a trigger IS
	 * a tick of this rule's damage, and "five triggers" and "five seconds of damage" are
	 * the same five by construction. Ruled under the project owner's delegation.
	 *
	 * THE FIVE NEED NOT BE CONSECUTIVE. The row says "5+ times" and not "in a row", so
	 * ticks anywhere on the floor count towards the same total.
	 */
	static const TCHAR* JudgmentZonesKey;

	/**
	 * March of Progress: "Each floor, enemies damage increases by 10%. Killing the
	 * Commander in each level will increase the player's armor by 10%. If the player
	 * skips killing too many commanders, they may find they can't withstand the
	 * increasing damage." Issues #1820 and #41.
	 *
	 * THE ROW DESCRIBES A TRADE AND THE TRADE IS THE WHOLE RULE. The damage rises with
	 * the floor whatever the player does; the armour rises only for the player who goes
	 * and kills the floor's Commander. Its last sentence is the consequence of the two
	 * and not a third mechanism: a player who skips commanders keeps the rising damage
	 * and none of the armour.
	 *
	 * THE COMMANDER IS A CREATURE THIS RULE CHOOSES AND NOT A CREATURE THAT CARRIES THE
	 * COMMANDER TAG. `ACataclysmEnemyCharacter::CommanderMultiplier` makes whoever
	 * carries that tag 20% faster, and both granters give it to OTHERS, so the tag means
	 * "buffed by a commander" rather than "is a commander". One tag with two meanings is
	 * what that class's own comment forbids. The game mode holds this rule's Commander as
	 * a weak pointer instead, chosen once when the floor is populated.
	 *
	 * NOTHING ON THE CHOSEN CREATURE CHANGES. It is an ordinary creature of its kind and
	 * rung, and the floor panel is the only thing that says a Commander exists. What a
	 * player would need in order to pick it out of a crowd is issue #1997, deliberately
	 * not folded in here.
	 */
	static const TCHAR* MarchOfProgressKey;

	/**
	 * Commander's Aura: "Certain elite enemies act as commanders, providing buffs (e.g.,
	 * increased health, damage, or resistance) to nearby allies." Issues #1820 and #41.
	 *
	 * THE WORD "COMMANDER" NOW MEANS THREE DIFFERENT THINGS IN THIS GAME, AND A READER
	 * MUST GO BY THE RULE KEY RATHER THAN BY THE WORD. They are:
	 *
	 *   the Commander gameplay tag        "buffed by a commander", never "is a
	 *                                     commander". `CommanderMultiplier` makes
	 *                                     whoever holds it 20% faster, and all four
	 *                                     things that grant it give it to OTHERS.
	 *   `War_March_of_Progress`           ONE creature a floor, chosen and hunted. It
	 *                                     carries no tag and nothing on it changes.
	 *   this row                          EVERY creature at Elite or above, each
	 *                                     granting the tag to its neighbours.
	 *
	 * So a floor carrying both War rows has one creature the player must hunt and
	 * several creatures buffing their neighbours, and neither is the other. Every
	 * identifier here carries its rule's key for that reason, and the floor panel names
	 * the rule beside its count. `docs/DECISIONS.md` records the three meanings.
	 *
	 * "CERTAIN ELITE ENEMIES" IS READ AS EVERY CREATURE AT ELITE OR ABOVE. The row names
	 * no number, so "certain" is read as the rung and not as a count; ruled under the
	 * project owner's delegation. `War_Royal_Guard`'s row was reworded by the owner to
	 * "above Uncommon ranked" and this library reads that as Elite, the rung above
	 * Common, so the same reading is used here rather than a second one.
	 *
	 * THE ROW'S OWN EXAMPLES ARE NOT FOLLOWED, AND THAT IS A JUDGEMENT WITH A MEASURED
	 * REASON. It suggests "increased health, damage, or resistance"; the Commander tag
	 * raises movement speed and attack speed. The project owner decided on 2026-08-20
	 * which stats that tag covers, and excluded maximum health for a reason this row
	 * would walk straight into: an enemy's attributes are BASE values and current health
	 * does not rise with the maximum, so an ally walking in and out of an aura would lose
	 * health permanently from an effect meant to help it. "e.g." states examples rather
	 * than a requirement, and `ACataclysmEnemyCharacter` records that this tag "remains
	 * the only thing in the game that makes a creature better", so using it is the one
	 * reading that needs no new buff.
	 */
	static const TCHAR* CommandersAuraKey;

	/**
	 * The row where patches of ground refuse the player's spells. Issues #1820 and #41.
	 *
	 * A ZONE APPEARS EVERY `AntiMagicZonesSecondsBetweenZones`, up to
	 * `AntiMagicZonesMostZones` at once, each lasting `AntiMagicZonesSeconds` and reaching
	 * `AntiMagicZonesRadiusCm`. While the player stands in one, every skill they carry
	 * that is tagged `Type.Spell` is refused when pressed; stepping out gives them back.
	 *
	 * "MAGICAL ABILITIES" IS READ AS THE SKILLS TAGGED `Type.Spell`, AND NOTHING ELSE IS
	 * LOCKED. Ruled under the project owner's delegation. The row sets "magical" against
	 * "physical", and `Type.Spell` is the one tag this game's skill data uses for that
	 * split: `UCataclysmSkillEffects::IsSpell` reads it to decide whether spell damage
	 * reaches a blow. So the lock is a `skill_locked` modifier SCOPED BY `RequiredTags`
	 * to that tag, and `UCataclysmStatPipeline` judges a modifier's required tags against
	 * the skill being pressed -- the same scoping the enchantment row "your own ultimate
	 * ability is disabled" uses for `Slot.Ultimate`.
	 *
	 * WHAT THAT REACHES TODAY, MEASURED ON `game/Data/WeaponSkills.csv`. Nine skills
	 * carry `Type.Spell`, all of them Demonic wand and staff skills. The tenth designed
	 * wand or staff skill, the Demonic Staff's "Summon Imp", is untagged and stays usable
	 * in a zone: a summon is not a spell, which is the project owner's decision of
	 * 2026-09-23. The wand and staff slots of the other six Cataclysms are undesigned
	 * placeholders today, with no skill name, so no player casts them (issue #2012).
	 *
	 * A BASIC ATTACK IS NEVER LOCKED, AND NOTHING HERE HAS TO SAY SO.
	 * `UCataclysmSkillTemplate::CanActivateAbility` skips the lock check for the basic
	 * attack's slot unconditionally, for the Edict of Silence's "Only basic attacks
	 * function during this period".
	 *
	 * THE LOCK IS SHOWN SLOT BY SLOT WITH NOTHING ADDED HERE. The skill bar marks a slot
	 * locked by asking `skill_locked` with that slot's own skill tags (issue #1810, built
	 * in #1819), so inside a zone each spell's slot is marked and every other slot is not.
	 * The heads-up display's "skills locked" line needs EVERY filled slot locked, and a
	 * Demonic caster's Aura is not a spell, so it does not appear -- which is accurate.
	 * The floor panel says how many zones are standing.
	 */
	static const TCHAR* AntiMagicZonesKey;

	/**
	 * The row where a player low on mana pays for skills in health. Issues #1820
	 * and #41.
	 *
	 * "When your Mana falls below 10%, your skills cost 5% of your current Health
	 * to cast instead of Mana." Both figures are the row's own:
	 * `DesperateMeasuresManaBelowPercent` and `DesperateMeasuresHealthPercent`.
	 *
	 * A ROW ON THE CHARACTER'S STAT LINE AND NOTHING ON THE BEAT. The floor writes
	 * `mana_cost_as_current_health_percent` under the condition `mana_below`, and
	 * the condition is judged each time a cast asks what it costs -- so the rule
	 * turns on and off with the mana in hand, with no clock of its own.
	 *
	 * ONLY A CAST THAT WOULD HAVE TAKEN MANA PAYS, ruled under the project owner's
	 * delegation on 2026-09-23: "instead of Mana" replaces a cost and does not
	 * add one. `UCataclysmGameplayAbility::ManaCostPaidAsHealthPercent` holds
	 * that rule, and its comment says why the cost after the character's own
	 * reductions decides it rather than the slot's figure.
	 *
	 * AN AURA'S UPKEEP IS NOT A CAST AND STILL DRAINS MANA, a judgement under the
	 * same delegation. The row says "to cast", and an upkeep paid in health
	 * instead would let a toggled aura run for ever at low mana.
	 */
	static const TCHAR* DesperateMeasuresKey;

	/**
	 * The row where the floor's dead rise again, once. Issues #1820 and #41.
	 *
	 * "Once per floor, all defeated enemies on that floor resurrect at half health in a
	 * sudden holy revival." Both figures are the row's own: once a floor, and
	 * `DivineResurgenceHealthPercent`.
	 *
	 * WHEN IT FIRES IS A RULING, UNDER THE PROJECT OWNER'S DELEGATION ON 2026-09-23:
	 * the moment at least `DivineResurgenceFallenPercent` of the creatures the floor
	 * placed have died, counted at each death and rounded up. A state the player
	 * creates, which cannot fire on an empty floor. `DivineResurgenceIsDue` holds the
	 * arithmetic.
	 *
	 * EVERY CREATURE RECORDED AS DYING BEFORE THAT MOMENT RISES, AT THE KIND AND RUNG
	 * IT DIED AT, and none that dies afterwards: once means once. No cap -- the row says
	 * "all", and the trigger holds the number at about half the floor.
	 *
	 * EACH ONE RISES MARKED, so its second death pays no loot and no experience (the
	 * owner's decision of 2026-09-17), is not counted among the creatures the floor
	 * placed, and is never recorded as a death that could rise.
	 */
	static const TCHAR* DivineResurgenceKey;

	/**
	 * The row where a killed creature may get up again. Issues #1820 and #41.
	 *
	 * "Enemies have a chance to revive after being killed." The row gives no figure, so
	 * the chance is `DeadRisingChancePercent`, the ten this table already uses for a
	 * chance fired by a death.
	 *
	 * RULED UNDER THE PROJECT OWNER'S DELEGATION ON 2026-09-23:
	 * - EVERY DEATH ROLLS, WHOEVER DEALT IT. The row says "after being killed" and names
	 *   no killer, where Vengeful Wraiths says "the one who killed them".
	 * - AT ONCE, in the same death notice, where it fell.
	 * - AT THE KIND AND RUNG IT DIED AT, AND AT FULL HEALTH. "Revive" with no reduction
	 *   stated is the creature as it was placed; Divine Resurgence's half is that row's
	 *   own figure.
	 * - MARKED, so its second death pays no loot and no experience (the owner's decision
	 *   of 2026-09-17), and A MARKED CREATURE NEVER ROLLS: one extra life at most.
	 */
	static const TCHAR* DeadRisingKey;

	/**
	 * The row where the dungeon drains the player's health and mana. Issues #1820 and
	 * #41.
	 *
	 * "The dungeon passively saps the player's resources (e.g., health, mana, stamina)
	 * at a slow but constant rate." Each quarter-second beat takes
	 * `SufferingAuraHealthPercentPerSecond` of the player's maximum health and
	 * `SufferingAuraManaPercentPerSecond` of their maximum mana, for the beat's length.
	 *
	 * RULED UNDER THE PROJECT OWNER'S DELEGATION ON 2026-09-23:
	 * - HEALTH AND MANA ONLY. No stamina exists in this game, and the row names neither
	 *   the energy shield nor a class resource.
	 * - THE SAME RATE ON EVERY FLOOR: "constant". Mortal Decay's grows with depth.
	 * - IT CAN KILL, as Mortal Decay can. The health goes through
	 *   `UCataclysmSkillEffects::ReduceHealthDirectly`, which is not a hit.
	 *
	 * THE RATES ARE THE PROJECT OWNER'S DECISION OF 2026-09-23: a drain the player
	 * notices, enough to beat every class's BASE regeneration. See the two constants.
	 */
	static const TCHAR* SufferingAuraKey;

	/**
	 * The row where the stairs stay sealed until the player has killed enough. Issues
	 * #1820 and #41.
	 *
	 * "Doors leading to the next level in a dungeon are sealed shut until the player has
	 * slain enough enemies to open them." Taking the stairs does nothing until the player
	 * has slain `BloodGatesSlainPercent` of the creatures the floor placed, rounded up.
	 *
	 * RULED UNDER THE PROJECT OWNER'S DELEGATION ON 2026-09-23:
	 * - "PLACED" IS THE PLAYER'S KILLS PLUS THE UNMARKED CREATURES STILL STANDING, counted
	 *   at each arrival. A creature that dies to anything but the player leaves both the
	 *   count and the target. SO ONCE NO UNMARKED CREATURE STANDS, THE GATE IS OPEN: a
	 *   count that kept the other deaths could leave a player on a floor they could never
	 *   leave, which is why this reading replaced it.
	 * - "THE PLAYER HAS SLAIN" is the killer on the death notice, and a minion's kill
	 *   counts only when its summoner holds the Conduit keystone, as Vengeful Wraiths
	 *   reads it.
	 * - A MARKED CREATURE (one brought back from the dead) is neither placed nor slain.
	 * - THE LAST FLOOR IS NOT SEALED: its stairs lead out, not to a next level.
	 *
	 * A HORDE DUNGEON HAS NO STAIRS, so on one this row does nothing.
	 */
	static const TCHAR* BloodGatesKey;

	/**
	 * The row where a crescendo hastes every creature on the floor for ten seconds.
	 * Issues #1820 and #41.
	 *
	 * "Distant funeral music plays; when it crescendos, all enemies gain haste and fear
	 * immunity for 10 seconds." Every `DirgeResonanceEverySeconds` of the floor's beat,
	 * every living creature on the floor gains `Status.Buff.Commander` for
	 * `DirgeResonanceHasteSeconds`.
	 *
	 * RULED UNDER THE PROJECT OWNER'S DELEGATION ON 2026-09-23:
	 * - THE PERIOD IS THE EDICT OF SILENCE'S NINETY SECONDS, the only period the table
	 *   states for a repeating event across a whole floor; the first crescendo comes
	 *   ninety seconds into the floor.
	 * - "HASTE" IS `Status.Buff.Commander`, 20% more movement and attack speed, the one
	 *   haste-like status a creature can hold. It reaches both speeds through
	 *   `ACataclysmEnemyCharacter::SpeedMultiplier`, and refreshes rather than stacks.
	 * - "ALL ENEMIES" is every living creature on the floor at the crescendo, marked ones
	 *   included. One that arrives during the ten seconds waits for the next.
	 *
	 * TWO HALVES OF THE ROW DO NOTHING TODAY. There is no fear in this game, so "fear
	 * immunity" grants nothing; and there is no audio for the "distant funeral music".
	 */
	static const TCHAR* DirgeResonanceKey;

	/**
	 * The row where one worn piece of gear gives nothing for a floor. Issues #1820 and #41.
	 *
	 * "At the start of each floor, a random equipment slot (excluding weapons) has its
	 * stats and enchantments disabled for that floor." As each floor begins, one of the
	 * non-weapon slots that hold an item is drawn, and
	 * `UCataclysmEquipmentComponent::SetDisabledSlot` switches it off until the next.
	 *
	 * RULED UNDER THE PROJECT OWNER'S DELEGATION ON 2026-09-23:
	 * - DRAWN EVENLY FROM THE NON-WEAPON SLOTS THAT HOLD AN ITEM, so the rule always costs
	 *   something; with nothing but weapons worn, nothing is switched off. Rings are drawn
	 *   more often because there are more of them: a play-test point.
	 * - THE ITEM COUNTS AS NOT WORN FOR STATS: no implicit, affix or enchantment, and no
	 *   piece of a set. It stays worn and visible.
	 * - THE SLOT, NOT THE ITEM, for the whole floor, so swapping gear is the answer.
	 *
	 * THE DRAW IS SEEDED BY THE DUNGEON AND THE FLOOR, the way a floor's contents are, so
	 * the same floor of the same dungeon draws from the same stream; `ScarcityPick` holds it.
	 */
	static const TCHAR* ScarcityKey;

	/**
	 * The row where an enemy drop's affix tiers are drawn evenly. Issues #1820 and #41.
	 *
	 * "Items dropped by enemies have randomized stats within a wide range, making each
	 * piece potentially extremely valuable or useless." On a floor carrying it, each
	 * affix of an enemy drop draws its tier evenly from T1 to the difficulty's cap
	 * (`UCataclysmDropRoll::RollChaoticAffixTier`), and its value evenly within it.
	 *
	 * RULED BY THE COORDINATING SESSION UNDER THE OWNER'S DELEGATION, 2026-09-23:
	 * - THE CAP STAYS: docs/Cataclysm_GDD_v2.md section VII, "The affix tier column IS
	 *   still a hard cap." From difficulty 6 up every tier is 1 in 7; at difficulty 1,
	 *   T1 and T2 are 1 in 2 each, where they are 2 in 3 and 1 in 3 elsewhere.
	 * - RARITY, THE NUMBER OF AFFIXES AND MAGIC FIND ARE UNCHANGED: the row says stats.
	 * - EVERY ENEMY DROP ON THE FLOOR, whoever made the kill. Nothing crafted or already
	 *   owned, and no material.
	 *
	 * THE DUNGEON GAME MODE ANSWERS `ACataclysmGameMode::DropsAreChaotic` from its floor,
	 * and the drop spawner asks it as it asks the difficulty tier.
	 */
	static const TCHAR* ChaoticLootKey;

	/**
	 * The row where the stairs are a portal that may not take the player down. Issues
	 * #1820 and #41.
	 *
	 * "Stepping through a portal has a 50% chance of taking you to the next floor, a 25%
	 * chance of returning you to the beginning of the current floor, and a 25% chance of
	 * spawning a powerful, unpredictable mini-boss." All three odds are the row's own;
	 * `UnstablePortalOutcomeFor` holds them.
	 *
	 * RULED BY THE COORDINATING SESSION UNDER THE OWNER'S DELEGATION, 2026-09-23:
	 * - THE MINI-BOSS IS AN ABYSSAL WARDEN at `UnstablePortalMiniBossRung`, the Herald rung
	 *   the built rules already call the mini-boss rung, with modifiers drawn afresh:
	 *   docs/Cataclysm_GDD_v2.md names the Warden as the mini-boss, and its random
	 *   modifiers are the "unpredictable". It drops loot as any creature of its rung.
	 * - ONE ROLL PER STEP: after a roll that did not take the player down, the stairs
	 *   ignore the player until a look finds them out of reach.
	 * - BLOOD GATES FIRST: sealed stairs roll nothing. THE LAST FLOOR'S STAIRS ARE NOT A
	 *   PORTAL: they lead out, not to a next floor.
	 * - "THE BEGINNING OF THE CURRENT FLOOR" moves the player only, to the entrance; the
	 *   floor keeps all its state.
	 * - A WARDEN THE PORTAL RAISED IS LEFT OUT OF BLOOD GATES' COUNT, placed and slain, so it
	 *   cannot seal again stairs the player had opened.
	 *
	 * A HORDE DUNGEON HAS NO STAIRS, so on one this row does nothing.
	 */
	static const TCHAR* UnstablePortalKey;

	/**
	 * The row where the player's kills fuel the dungeon's final boss. Issues #1820 and #41.
	 *
	 * "Enemies that the player kills aren't forgotten, instead a portion of their stats are
	 * fed back into the void to fuel the final boss of the dungeon." Each creature the
	 * player kills on a floor carrying the row adds `NothingIsForgottenPortionPercent` of
	 * its maximum health and of its attack damage to a total the dungeon keeps; when the
	 * final floor's boss is placed, the total is added to it.
	 *
	 * RULED BY THE COORDINATING SESSION UNDER THE OWNER'S DELEGATION, 2026-09-23:
	 * - FIVE PERCENT OF EACH, a judgement: no figure in the design settles it.
	 * - THE HEALTH IS UNCAPPED; THE DAMAGE IS CAPPED at `NothingIsForgottenMostDamagePercent`
	 *   of the boss's own, so it hits at most twice as hard. Uncapped damage over a long
	 *   dungeon would make a fight no play could survive. Both are play-test points.
	 * - THE FINAL BOSS ONLY: the Gatekeeper the last floor places at its exit, even in an
	 *   Elite dungeon where every floor ends with one. A dungeon of one floor has no final
	 *   boss at its exit, so there the row feeds nothing.
	 * - THE PLAYER'S KILLS OF UNMARKED CREATURES, only on floors carrying the row. A risen
	 *   creature's second death feeds nothing. The total empties when the player leaves.
	 */
	static const TCHAR* NothingIsForgottenKey;

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
	 *
	 * THE ROW IT WAS DERIVED FROM NOW HAS A RULE OF ITS OWN, and that rule's
	 * `VengefulWraithsChancePercent` is declared as THIS constant rather than
	 * writing ten a second time. Two rules holding one design figure must not be
	 * able to drift apart. The tie is written that way round only because a
	 * static member can name only one declared before it, and this one is first;
	 * the borrowing here still runs the other way, from Spore Clouds to the row.
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

	/**
	 * How often a blow the player lands provokes a retaliation, in percent.
	 *
	 * A JUDGEMENT, AND THE TABLE'S OWN FIGURE FOR THIS SHAPE. The row says
	 * "a chance" and states none. Ten is what `game/Data/DungeonModifiers.csv`
	 * uses for a chance fired by an event, and it is what
	 * `SporeCloudsChancePercentOnDeath` and `HellfireChancePercentOnDeath` both
	 * took for the same reason.
	 *
	 * THAT PRECEDENT APPLIES HERE AND DID NOT APPLY TO THE ROW BEFORE THIS ONE.
	 * `IllusoryEnemiesSharePercent` is deliberately 25 rather than 10, because a
	 * share of a floor's population is not a chance fired by an event. This one
	 * is fired by an event, so the ten is the right precedent rather than a
	 * habit.
	 */
	static constexpr float HolyRepercussionsChancePercentOnHit = 10.0f;

	/**
	 * How far a radiant burst reaches.
	 *
	 * A JUDGEMENT, declared as another rule's constant rather than as a number
	 * for the reason `BrandNovaRadiusCm` gives: 300 is this project's settled
	 * answer for a thing at a point on the floor. The row says "in an area" and
	 * nothing about size.
	 */
	static constexpr float HolyRepercussionsBurstRadiusCm = InfernalRainRadiusCm;

	/**
	 * The most Judgment stacks the player carries, and what one is worth.
	 *
	 * BOTH ARE JUDGEMENTS AND THE ROW STATES NEITHER. It says "a stacking debuff
	 * that increases holy damage taken" and stops.
	 *
	 * THE CAP IS `WastingSicknessMostStacks`'S FIGURE AND NOT BRAND'S. Five,
	 * because Judgment makes the player take MORE damage and an uncapped count
	 * would make a floor lethal in a way the row does not ask for.
	 * `Famine_Wasting_Sickness` caps at five for the same shape of reason;
	 * `Demonic_Brand_of_the_Aggressor`'s twenty is a count that ERUPTS and
	 * clears rather than a debuff that stays.
	 *
	 * WHAT A STACK IS WORTH HAS NO PRECEDENT IN THIS FILE AT ALL, and that is
	 * said plainly rather than dressed up. No other rule lowers one resistance,
	 * so there is no figure to follow. Five percent per stack is twenty-five at
	 * the cap, which is a quarter of one resistance -- enough for a player to
	 * feel on a Celestial floor and far from the pipeline's clamp.
	 */
	static constexpr int32 HolyRepercussionsJudgmentMostStacks = 5;
	static constexpr float HolyRepercussionsJudgmentLessPerStackPercent = 5.0f;

	/**
	 * What one cloud drains from the player, as a share of MAXIMUM health.
	 *
	 * A JUDGEMENT. The row says "a small portion of your health" and states no
	 * figure. A share of the maximum rather than of what the player has now,
	 * which is `Demonic_Brand_of_the_Aggressor`'s shape, so a cloud costs a
	 * wounded player the same as a healthy one rather than less.
	 */
	static constexpr float LeechSporesDrainPercentOfMaximumHealth = 10.0f;

	/**
	 * How wide a cloud is.
	 *
	 * A JUDGEMENT, declared as another rule's constant rather than as a number
	 * for the reason `BrandNovaRadiusCm` gives: 300 cm is this project's settled
	 * answer for a thing at a point on the floor. The row says "a cloud".
	 */
	static constexpr float LeechSporesCloudRadiusCm = WitheredGroundPatchRadiusCm;

	/**
	 * How far from the player a creature can be and still be healed.
	 *
	 * THE ROW'S OWN FIGURE: "any enemies within a 10-meter radius". Measured
	 * from the PLAYER at the moment of the drain rather than from the cloud,
	 * because the drain is what feeds the heal and it is the player who is
	 * drained.
	 */
	static constexpr float LeechSporesHealRadiusCm = 1000.0f;

	static_assert(
		LeechSporesDrainPercentOfMaximumHealth > 0.0f
			&& LeechSporesDrainPercentOfMaximumHealth < 100.0f,
		"A cloud that drains nothing heals nothing and the row is unbuilt; one "
		"that drains all of a player's maximum kills them on contact, which 'a "
		"small portion' does not describe.");

	static_assert(
		LeechSporesCloudRadiusCm > 0.0f,
		"A cloud that covers nowhere can be touched by nobody.");

	static_assert(
		LeechSporesHealRadiusCm > LeechSporesCloudRadiusCm,
		"The heal now reaches no further than the cloud is wide. The row says "
		"'within a 10-meter radius', which is wider than a cloud, and this rule "
		"deliberately read that over a record that grouped it with rules acting "
		"only inside their own patch.");

	/**
	 * What each death counted adds to one pulse, as a share of the player's MAXIMUM
	 * health.
	 *
	 * A JUDGEMENT, ruled under the owner's delegation of unstated figures. The row
	 * says the pulses "grow stronger with the number of enemies slain" and gives no
	 * figure. A share of the maximum follows `ArtilleryStrikeMaxHealthPercent` and
	 * `BrandNovaMaxHealthPercent`, so a pulse costs a wounded player what it costs a
	 * healthy one. An altar nobody has fed deals nothing.
	 */
	static constexpr float BloodAltarMaxHealthPercentPerDeath = 0.5f;

	/**
	 * The most one pulse can take, as a share of the player's maximum health.
	 *
	 * A JUDGEMENT. The row states no ceiling; without one, enough deaths make every
	 * pulse a certain kill, and the row does not say the altar kills outright. For
	 * comparison, `ArtilleryStrikeMaxHealthPercent` is 25.
	 */
	static constexpr float BloodAltarMostMaxHealthPercent = 30.0f;

	/**
	 * The death at which a pulse stops growing.
	 *
	 * DERIVED FROM THE TWO FIGURES ABOVE rather than written down, so the count the
	 * floor panel shows and the damage stop at the same death.
	 */
	static constexpr int32 BloodAltarDeathsToCeiling = static_cast<int32>(
		BloodAltarMostMaxHealthPercent / BloodAltarMaxHealthPercentPerDeath + 0.5f);

	/**
	 * How far a pulse reaches from the altar.
	 *
	 * A JUDGEMENT. The row gives no figure. The stairs stand at the altar's centre,
	 * so no player leaves the floor without entering its reach; this figure decides
	 * how far around the stairs a pulse can find them.
	 */
	static constexpr float BloodAltarReachCm = 1000.0f;

	/**
	 * How long after the floor's start, and after each pulse, the next pulse comes.
	 *
	 * THE PROJECT OWNER'S FIGURE, not a judgement. Nothing derives it, and nothing
	 * else here is derived from it.
	 */
	static constexpr float BloodAltarSecondsBetweenPulses = 30.0f;

	static_assert(
		BloodAltarMaxHealthPercentPerDeath > 0.0f
			&& BloodAltarMaxHealthPercentPerDeath <= BloodAltarMostMaxHealthPercent,
		"A death that adds nothing leaves the altar harmless however much it is "
		"fed, and one that adds more than the ceiling reaches the ceiling at the "
		"first death, which is not 'grow stronger'.");

	static_assert(
		BloodAltarMostMaxHealthPercent < 100.0f,
		"A pulse allowed to take all of a player's maximum health is a certain kill "
		"once the altar is fed, which the row does not describe.");

	static_assert(
		static_cast<float>(BloodAltarDeathsToCeiling) * BloodAltarMaxHealthPercentPerDeath
			== BloodAltarMostMaxHealthPercent,
		"The share per death no longer divides the ceiling evenly, so a pulse would "
		"reach its ceiling part way through a death and the floor panel's count "
		"would stop at a different death from the damage.");

	static_assert(
		BloodAltarReachCm > 0.0f && BloodAltarSecondsBetweenPulses > 0.0f,
		"An altar that reaches nowhere, or pulses on every beat, is not the row.");

	/**
	 * How much Necrotic Ground cuts the health a player standing in its fog restores,
	 * in points of `HealingReceivedReduction`.
	 *
	 * THE ROW'S OWN FIGURE: "reduces your healing effectiveness by 50%". Death's
	 * Embrace writes the same stat, and the two add. The stat is held between 0 and
	 * 100, so a player at five Embrace stacks (50) standing in the fog (50 more)
	 * restores no health at all.
	 */
	static constexpr float NecroticGroundHealingLessPercent = 50.0f;

	/**
	 * What a creature standing in the fog regains a second, as a share of its own
	 * maximum health.
	 *
	 * THE ROW'S OWN FIGURE: "Enemies standing in the fog regen their health at 10%/s".
	 */
	static constexpr float NecroticGroundCreatureRegenPercentPerSecond = 10.0f;

	/**
	 * How long after the floor's start, and after each patch, the next patch comes.
	 *
	 * A JUDGEMENT, ruled under the owner's delegation of unstated figures. The row says
	 * the fog is "spreading" and gives no rate.
	 */
	static constexpr float NecroticGroundSecondsBetweenPatches = 5.0f;

	/**
	 * The most patches the fog spreads to on one floor.
	 *
	 * A JUDGEMENT. "The dungeon floor is covered" states no extent; twelve is four times
	 * `InfernalRainMostPatches`.
	 */
	static constexpr int32 NecroticGroundMostPatches = 12;

	/**
	 * How wide one patch is.
	 *
	 * DERIVED, not chosen: this project's settled figure for a patch on the floor.
	 */
	static constexpr float NecroticGroundPatchRadiusCm = InfernalRainRadiusCm;

	/**
	 * How far a new patch's centre is from the patch it spreads from: one patch-width,
	 * so the two touch.
	 */
	static constexpr float NecroticGroundSpreadCm = NecroticGroundPatchRadiusCm * 2.0f;

	/**
	 * How far from the player the first patch may appear.
	 *
	 * DERIVED, Infernal Rain's placement: near the player, never on them.
	 */
	static constexpr float NecroticGroundFirstPatchWithinCm = InfernalRainFallsWithinCm;

	/**
	 * What the fog burns a player standing in it for, a second, as a share of maximum
	 * health.
	 *
	 * A JUDGEMENT. The ground zones other floor rules place that burn by the second
	 * take one of two shares: 2%, Infernal Rain's and Hallowed Groundfall's, or 1%,
	 * `SingularityWellsPercentPerSecond`. The fog takes the lower because it also
	 * halves healing. The row says "deals damage over time" and gives no figure.
	 */
	static constexpr float NecroticGroundPercentPerSecond = SingularityWellsPercentPerSecond;

	/** How often the burn lands, which is what "a second" in the figure above means. */
	static constexpr float NecroticGroundSecondsBetweenBurns = 1.0f;

	static_assert(
		NecroticGroundHealingLessPercent > 0.0f && NecroticGroundHealingLessPercent <= 100.0f,
		"The healing cut is a share of a hundred, and at zero the fog cuts nothing.");

	static_assert(
		NecroticGroundMostPatches > 1 && NecroticGroundSecondsBetweenPatches > 0.0f,
		"A fog that cannot reach a second patch does not spread, and one placed on every "
		"beat covers the floor at once.");

	static_assert(
		NecroticGroundFirstPatchWithinCm > NecroticGroundPatchRadiusCm + 1.0f,
		"The first patch is placed past its own radius from the player, so the range it "
		"is drawn from has to reach further than that.");

	/**
	 * How long a creature must stay alive for each stack of Ravenous Hoard.
	 *
	 * A JUDGEMENT, ruled under the owner's delegation of unstated figures, and Death's
	 * Embrace's figure: that row is this project's answer to "worse the longer you
	 * stay", on the player's side. The row states no rate.
	 */
	static constexpr float RavenousHoardSecondsPerStack = DeathsEmbraceSecondsPerStack;

	/** The most stacks a creature holds. A JUDGEMENT, and Death's Embrace's cap. */
	static constexpr int32 RavenousHoardMostStacks = DeathsEmbraceMostStacks;

	/**
	 * What each stack adds to a creature's attack damage, as a share of its own base.
	 *
	 * A JUDGEMENT, and Death's Embrace's figure for a stack, so at the cap a creature
	 * hits half as hard again. The row says only "grow stronger".
	 */
	static constexpr float RavenousHoardDamagePercentPerStack =
		DeathsEmbracePercentPerStack;

	static_assert(
		RavenousHoardSecondsPerStack > 0.0f && RavenousHoardMostStacks > 0
			&& RavenousHoardDamagePercentPerStack > 0.0f,
		"A creature that never gains a stack, or gains one worth nothing, does not grow "
		"stronger the longer it lives.");

	/**
	 * How long between one wave of Grave Tide and the next, counted from the floor's
	 * start.
	 *
	 * A JUDGEMENT, ruled under the owner's delegation, and the figure this project uses
	 * for a floor event on a clock: `ArtilleryStrikeSecondsBetween`. The row says
	 * "periodically" and states no figure.
	 */
	static constexpr float GraveTideSecondsBetweenWaves = ArtilleryStrikeSecondsBetween;

	/** How many creatures rise in the first wave. A JUDGEMENT. */
	static constexpr int32 GraveTideFirstWaveCreatures = 3;

	/** How many more rise in each wave after the first. A JUDGEMENT: "more numerous". */
	static constexpr int32 GraveTideMoreCreaturesPerWave = 1;

	/** How many waves one floor holds. A JUDGEMENT: without a ceiling a floor fills. */
	static constexpr int32 GraveTideMostWaves = 6;

	/**
	 * What each wave after the first adds to its creatures' attack damage, as a share of
	 * their own.
	 *
	 * A JUDGEMENT, and Death's Embrace's figure for a step, the same share Ravenous
	 * Hoard gives for time alive. "Grow stronger" states no figure.
	 */
	static constexpr float GraveTideDamagePercentPerWave = DeathsEmbracePercentPerStack;

	/**
	 * The most a wave's creatures are placed with, as a share of their own damage.
	 *
	 * A JUDGEMENT, and Ravenous Hoard's ceiling: that rule's cap of stacks at that share
	 * each, so a wave cannot place a creature stronger than one that lived to its cap.
	 */
	static constexpr float GraveTideMostDamagePercent =
		RavenousHoardMostStacks * GraveTideDamagePercentPerWave;

	static_assert(
		GraveTideSecondsBetweenWaves > 0.0f && GraveTideMostWaves > 0
			&& GraveTideFirstWaveCreatures > 0,
		"A tide with no wave, no creature in it or no time between waves is not the row.");

	/**
	 * How far a creature's health must fall before it can mutate, as a share of its
	 * maximum.
	 *
	 * STATED BY THE ROW, WHICH IS UNUSUAL AMONG THESE FIGURES: "once they drop below 75%
	 * hp". It is not a judgement, and
	 * `tools/tests/test_dungeon_modifier_rules_are_the_rows.py` reads the row's own
	 * sentence for the number rather than trusting this line.
	 */
	static constexpr float VolatileEvolutionHealthPercentToMutate = 75.0f;

	/**
	 * The chance a creature under that share mutates, on each beat of the floor.
	 *
	 * A JUDGEMENT, ruled under the project owner's delegation. The row says only "a
	 * chance" and states no figure.
	 *
	 * ITS OWN NUMBER RATHER THAN ANOTHER ROW'S, DELIBERATELY. Ten is what this library
	 * already uses wherever a row says a chance -- `SporeCloudsChancePercentOnDeath`,
	 * `HellfireChancePercentOnDeath` and `HolyRepercussionsChancePercentOnHit` are all
	 * ten -- so this starts from that figure. Writing it as `= SporeCloudsChance...`
	 * would say the two must move together, and nothing in the design says that.
	 */
	static constexpr float VolatileEvolutionChancePercent = 10.0f;

	/** How many rungs of the rarity ladder one mutation climbs. A JUDGEMENT: one. */
	static constexpr int32 VolatileEvolutionRungsGained = 1;

	/**
	 * The highest rung a mutation reaches: Herald, the rung under the first boss.
	 *
	 * A JUDGEMENT, ruled under the project owner's delegation.
	 * `ACataclysmEnemyCharacter::FirstBossRarityStep` is 4 and `IsBoss()` is that
	 * comparison, so a rule allowed one rung higher could make a boss out of an
	 * ordinary creature in the middle of a fight, carrying the boss stun rule and the
	 * boss row of `game/Data/EnemyDrops.csv` with it.
	 *
	 * TIED TO THAT CONSTANT BY A `static_assert` IN `CataclysmDungeonGameMode.cpp`,
	 * where the cap is applied, because this header does not include the creature class
	 * and should not: it is a table of figures that the tests and the Python checks
	 * read.
	 */
	static constexpr int32 VolatileEvolutionHighestRung = 3;

	static_assert(
		VolatileEvolutionHealthPercentToMutate > 0.0f
			&& VolatileEvolutionHealthPercentToMutate < 100.0f
			&& VolatileEvolutionChancePercent > 0.0f
			&& VolatileEvolutionChancePercent <= 100.0f
			&& VolatileEvolutionRungsGained > 0
			&& VolatileEvolutionHighestRung >= VolatileEvolutionRungsGained,
		"A threshold at full health or at none, a chance of nothing, a climb of no rungs "
		"or a ceiling below one climb is not a creature that mutates when it is wounded.");

	/**
	 * How far a creature's health must fall before it calls its guards, as a share of its
	 * maximum. STATED BY THE ROW: "drops below 30% health".
	 */
	static constexpr float RoyalGuardHealthPercentToSummon = 30.0f;

	/** The chance it calls them when it falls that far. STATED BY THE ROW: "50% chance". */
	static constexpr float RoyalGuardChancePercent = 50.0f;

	/** How many arrive. STATED BY THE ROW: "two guards". */
	static constexpr int32 RoyalGuardGuardsSummoned = 2;

	/**
	 * The lowest rung that calls guards at all: Elite, the rung above Common.
	 *
	 * A JUDGEMENT, ruled under the project owner's delegation, because the row's own
	 * words name no rung this game has. See `RoyalGuardKey` above for what it says and
	 * what the three ladders in the data actually are.
	 */
	static constexpr int32 RoyalGuardLowestRungThatSummons = 1;

	/**
	 * The highest rung a guard can arrive at: the same ceiling Volatile Evolution has.
	 *
	 * NOT A SECOND NUMBER, DELIBERATELY. Both rules are held below the first boss rung
	 * for one reason -- a floor rule must not make a boss out of an ordinary creature in
	 * the middle of a fight -- and that reason is tied to
	 * `ACataclysmEnemyCharacter::FirstBossRarityStep` by the `static_assert` beside the
	 * other rule's ceiling in `CataclysmDungeonGameMode.cpp`. Writing a second 3 here
	 * would be the same fact in two places with nothing holding them together.
	 */
	static constexpr int32 RoyalGuardHighestRung = VolatileEvolutionHighestRung;

	static_assert(
		RoyalGuardHealthPercentToSummon > 0.0f
			&& RoyalGuardHealthPercentToSummon < 100.0f
			&& RoyalGuardChancePercent > 0.0f
			&& RoyalGuardChancePercent <= 100.0f
			&& RoyalGuardGuardsSummoned > 0
			&& RoyalGuardLowestRungThatSummons > 0,
		"A threshold at full health or at none, a chance of nothing, no guards at all, or "
		"a rule that every creature answers is not the row.");

	/**
	 * The chance a kill the player made brings a greater creature out of the corpse.
	 *
	 * A JUDGEMENT, ruled under the project owner's delegation. The row says
	 * "Occassionally" -- its own spelling -- and states no figure.
	 *
	 * ITS OWN NUMBER, STARTING FROM THE HOUSE FIGURE. Ten is what this library uses
	 * wherever a row says a chance on a death or a hit: `SporeCloudsChancePercentOnDeath`,
	 * `HellfireChancePercentOnDeath` and `HolyRepercussionsChancePercentOnHit`. Royal
	 * Guard's fifty is not a precedent for it: that figure is stated by that row.
	 */
	static constexpr float DemonPrinceChancePercent = 10.0f;

	/** How many may rise on one floor. A JUDGEMENT: one, so a floor cannot fill. */
	static constexpr int32 DemonPrincesPerFloor = 1;

	/**
	 * The rung the risen creature stands at: the same ceiling the other two rules use.
	 *
	 * NOT A THIRD NUMBER, for the reason `RoyalGuardHighestRung` gives: one fact, one
	 * place, and the tie to `ACataclysmEnemyCharacter::FirstBossRarityStep` lives beside
	 * Volatile Evolution's ceiling in `CataclysmDungeonGameMode.cpp`.
	 */
	static constexpr int32 DemonPrinceRung = VolatileEvolutionHighestRung;

	static_assert(
		DemonPrinceChancePercent > 0.0f && DemonPrinceChancePercent <= 100.0f
			&& DemonPrincesPerFloor > 0,
		"A chance of nothing, or no creature allowed to rise at all, is not the row.");

	/**
	 * The chance a kill the player made passes the corpse's debuffs on.
	 *
	 * STATED BY THE ROW: "there is a 25% chance for the disease to spread". Not a
	 * judgement, and `tools/tests/test_dungeon_modifier_rules_are_the_rows.py` reads the
	 * row's own sentence for it.
	 */
	static constexpr float EpidemicSpreadChancePercent = 25.0f;

	/**
	 * How many spreads in a row end the chain by killing everything nearby.
	 *
	 * STATED BY THE ROW: "If the disease spreads 5 times in a single chain".
	 */
	static constexpr int32 EpidemicSpreadsToKill = 5;

	/**
	 * How far the disease reaches, and how far the killing at the end of a chain reaches.
	 *
	 * A JUDGEMENT, ruled under the project owner's delegation, and NOT A NUMBER OF ITS
	 * OWN: it is the reach the two existing spreading nodes already have,
	 * `UCataclysmContagion::RadiusMetres`. The row says "a nearby enemy" and "all nearby
	 * enemies" and states no distance, and a second six in this file would be the same
	 * design figure written twice.
	 */
	static constexpr float EpidemicRadiusMetres = UCataclysmContagion::RadiusMetres;

	/** How many Plague Lords one floor may have. A JUDGEMENT: one, so a floor cannot fill. */
	static constexpr int32 EpidemicPlagueLordsPerFloor = 1;

	/** The rung a Plague Lord stands at: the ceiling the other rules share. */
	static constexpr int32 EpidemicPlagueLordRung = VolatileEvolutionHighestRung;

	static_assert(
		EpidemicSpreadChancePercent > 0.0f && EpidemicSpreadChancePercent <= 100.0f
			&& EpidemicSpreadsToKill > 0 && EpidemicRadiusMetres > 0.0f
			&& EpidemicPlagueLordsPerFloor > 0,
		"A chance of nothing, a chain that is over before it starts, no reach at all, or "
		"no Plague Lord allowed is not the row.");

	/**
	 * How far a death has to be from an Elite to feed it.
	 *
	 * A JUDGEMENT, ruled under the project owner's delegation, and NOT A NUMBER OF ITS
	 * OWN. The row says "nearby" and states no distance. This is the reach Epidemic
	 * already uses for the same word, `UCataclysmContagion::RadiusMetres`, so "nearby" in
	 * a floor rule means one distance rather than two.
	 */
	static constexpr float BloodForgedChampionsRadiusMetres =
		UCataclysmContagion::RadiusMetres;

	/**
	 * How many deaths nearby raise a champion one rung.
	 *
	 * A JUDGEMENT, ruled under the project owner's delegation. The row says only that
	 * Elites "absorb the strength of nearby dying allies, growing stronger". Three, so an
	 * Elite needs six deaths to reach Herald and the change is something a player watches
	 * happen rather than meets already finished.
	 */
	static constexpr int32 BloodForgedChampionsDeathsPerRung = 3;

	/**
	 * The lowest rung that absorbs anything: Elite, the rung above Common.
	 *
	 * DECLARED AS `RoyalGuardLowestRungThatSummons` AND NOT AS 1. That constant already
	 * carries this project's answer to "which creatures count as Elite", with the reading
	 * of `game/Data/EnemyRarities.csv` that produced it. Rules meaning the same rung
	 * should not be able to drift apart.
	 *
	 * THIS SENTENCE COUNTED THE RULES UNTIL 2026-09-18 AND SAID "TWO". `Commander's Aura`
	 * declares its own lowest rung the same way and made it three. Count the declarations
	 * rather than reading a number here: a comment that counts what sits under it goes
	 * wrong without being touched, and nothing reports it. Issue #1760 records the habit.
	 */
	static constexpr int32 BloodForgedChampionsLowestRung =
		RoyalGuardLowestRungThatSummons;

	/**
	 * The rung a champion stops at, which is the mini-boss the row names.
	 *
	 * DECLARED AS `VolatileEvolutionHighestRung` AND NOT AS 3, for the reason Royal
	 * Guard, Demon Prince and Epidemic all declare theirs that way: that constant is tied
	 * to `ACataclysmEnemyCharacter::FirstBossRarityStep` by the `static_assert` beside it,
	 * so no floor rule can make a boss out of an ordinary creature.
	 */
	static constexpr int32 BloodForgedChampionsHighestRung = VolatileEvolutionHighestRung;

	static_assert(
		BloodForgedChampionsRadiusMetres > 0.0f
			&& BloodForgedChampionsDeathsPerRung > 0
			&& BloodForgedChampionsLowestRung < BloodForgedChampionsHighestRung,
		"No reach at all, a rung earned by no deaths, or a floor already at the ceiling "
		"is not the row: an Elite has to be able to rise.");

	/**
	 * The chance a creature the player kills leaves a wraith.
	 *
	 * STATED BY THE ROW: "Enemies have a 10% chance of turning into wraiths when killed".
	 *
	 * DECLARED AS `SporeCloudsChancePercentOnDeath` AND NOT AS TEN, AND THAT READS
	 * BACKWARDS UNTIL YOU KNOW WHY. That constant was DERIVED FROM THIS ROW on 2026-09-14,
	 * as the comment above it says at length: this is the only row in the table stating a
	 * figure for a chance fired by ANY enemy's death. So the two are one design figure,
	 * and the rule this project follows everywhere else -- the reach, the Elite floor and
	 * the Herald ceiling all name an existing constant rather than repeat its number --
	 * says they must not be able to drift apart.
	 *
	 * THE DIRECTION IS FORCED BY DECLARATION ORDER, not chosen. A static member can only
	 * name one declared before it, and Spore Clouds' sits 765 lines above this. The
	 * comment on that one points back here so a reader coming from either end finds the
	 * other. Ruled under the project owner's delegation.
	 */
	static constexpr float VengefulWraithsChancePercent = SporeCloudsChancePercentOnDeath;

	/**
	 * What a wraith takes off every hit, as ONE multiplicative source.
	 *
	 * STATED BY THE ROW: "Wraiths have 90% damage reduction". **WHICH LAYER IT IS WRITTEN
	 * INTO IS THE PROJECT OWNER'S DECISION OF 2026-09-17**, and it had to be made because
	 * 90 IS ABOVE THE CAP ON THE ADDITIVE LAYER. `UCataclysmDamageCalculation::
	 * DamageReductionCap` is 75 and its comment records that Path of Exile's 90% "was
	 * deliberately not copied", because that 90% covers physical damage alone where this
	 * covers all eight types. Issue #644.
	 *
	 * SO IT IS WRITTEN INTO `DamageReductionMore`, the multiplicative bucket, whose bound
	 * is `MoreDamageReductionCap` at 99. A wraith therefore takes a tenth of whatever the
	 * other layers leave, which is what the row's number says, and the 75 cap on the
	 * additive pool is untouched.
	 *
	 * THIS IS THE LARGEST MULTIPLICATIVE REDUCTION IN THE GAME, and the entry says so.
	 * That bound's own comment notes "the largest multiplicative node in any class tree
	 * is 3% per point over 8 points, which is 24%". The owner's reason for allowing it
	 * here: a wraith is one temporary creature the player is meant to hunt down, which is
	 * not the same kind of number as a permanent node on a passive tree.
	 */
	static constexpr float VengefulWraithsDamageReductionMore = 90.0f;

	/**
	 * How much more attack damage, attack speed and movement speed a wraith has.
	 *
	 * STATED BY THE ROW: "20% increased damage, movespeed, and attack speed". ONE FIGURE
	 * FOR ALL THREE, because the row states one.
	 */
	static constexpr float VengefulWraithsIncreasePercent = 20.0f;

	/**
	 * What a wraith multiplies its own sight by, so it hunts across the whole floor.
	 *
	 * A JUDGEMENT, ruled under the project owner's delegation, and SIZED BY MEASUREMENT
	 * rather than picked. The row says "across the entire dungeon" and states no
	 * distance.
	 *
	 * THE MULTIPLIER SCALES EACH CREATURE'S OWN RADIUS, not a shared base --
	 * `ACataclysmCharacterBase::NoticesFromCm` is `SightRadiusCm() * multiplier` -- and
	 * the smallest of the seven designed creatures' own radii is 1000 cm. The largest
	 * floor is `FCataclysmFloorGenerator::MostFloorSide` of
	 * `FCataclysmFloorGenerator::CellSizeCm`, so 48 x 400 = 19,200 cm a side and about
	 * 27,153 cm corner to corner. Thirty times 1000 is 30,000, which covers it.
	 *
	 * `CataclysmDungeonGameMode.cpp` carries the `static_assert` that holds this figure to
	 * that span; it is there rather than here because that file already includes the floor
	 * generator and the Imp, and this one includes neither.
	 */
	static constexpr float VengefulWraithsSightMultiplier = 30.0f;

	static_assert(
		VengefulWraithsChancePercent > 0.0f && VengefulWraithsChancePercent <= 100.0f
			&& VengefulWraithsDamageReductionMore > 0.0f
			&& VengefulWraithsIncreasePercent > 0.0f
			&& VengefulWraithsSightMultiplier > 1.0f,
		"A chance of nothing, a wraith that takes no less damage, a wraith no stronger "
		"than what it rose from, or one that sees no further than its kind is not the "
		"row.");

	/**
	 * How long a judgment zone stands.
	 *
	 * STATED BY THE ROW: "Radiant zones spawn for 20 seconds".
	 */
	static constexpr float JudgmentZonesSeconds = 20.0f;

	/**
	 * How many ticks of this rule's damage earn the floor's loot bonus.
	 *
	 * STATED BY THE ROW: "if triggered 5+ times". THE PLUS IS WHY THIS IS A FLOOR AND NOT
	 * AN EXACT COUNT, and why the ticks need not be consecutive.
	 */
	static constexpr int32 JudgmentZonesTriggersForTheBonus = 5;

	/**
	 * How far a judgment zone reaches.
	 *
	 * DECLARED AS `WitheredGroundPatchRadiusCm` AND NOT AS 300. That is this project's
	 * settled answer for a patch of ground, shared by Infernal Rain, Singularity Wells and
	 * Withered Ground, and Spore Clouds already declares itself as it. The row states no
	 * distance.
	 */
	static constexpr float JudgmentZonesRadiusCm = WitheredGroundPatchRadiusCm;

	/**
	 * How far from the player a zone may appear.
	 *
	 * DECLARED AS `InfernalRainFallsWithinCm` AND NOT AS 1200, for the same reason: it is
	 * the settled answer for where a hazard laid near the player falls, shared with
	 * Singularity Wells.
	 */
	static constexpr float JudgmentZonesFallsWithinCm = InfernalRainFallsWithinCm;

	/**
	 * How many judgment zones may stand at once.
	 *
	 * A JUDGEMENT, ruled under the project owner's delegation, WRITTEN AS A FIGURE AND NOT
	 * TIED. Infernal Rain's `InfernalRainMostPatches` and Singularity Wells'
	 * `SingularityWellsMostWells` are both 3, written independently. That makes three a
	 * VOCABULARY rather than one design figure, so this follows it without naming either --
	 * the same treatment `HolyRepercussionsChancePercentOnHit` gets for the table's ten.
	 */
	static constexpr int32 JudgmentZonesMostZones = 3;

	/**
	 * How long between one judgment zone appearing and the next.
	 *
	 * A JUDGEMENT, ruled under the project owner's delegation, and again a vocabulary
	 * rather than a tie: Infernal Rain lays one every 5 seconds and Singularity Wells one
	 * every 8.
	 *
	 * EIGHT, WITH THE ARITHMETIC: a zone lasts 20 seconds and three may stand at once, so
	 * one every 8 seconds reaches three at 16 seconds, just as the first is due to go. At
	 * five the ceiling would be reached at 10 seconds and the floor would sit at three for
	 * the rest of its life, which is a different rule from the one the row describes.
	 */
	static constexpr float JudgmentZonesSecondsBetweenZones = 8.0f;

	/**
	 * What a first second inside a zone costs, and what each further second adds.
	 *
	 * A JUDGEMENT ON A SHAPE THE ROW ONLY HINTS AT, ruled under the project owner's
	 * delegation. The row says standing inside "RAMPS" the damage, so it grows; the two
	 * zone rules that damage are both flat, at `SingularityWellsPercentPerSecond` 1.0 and
	 * `InfernalRainPercentPerSecond` 2.0. This starts at the lower of those and adds it
	 * again for each further whole second in the same zone.
	 */
	static constexpr float JudgmentZonesPercentPerSecond = 1.0f;

	/**
	 * Where the ramp stops.
	 *
	 * A JUDGEMENT, ruled under the project owner's delegation. Five steps of one, so the
	 * ceiling is reached at the fifth second.
	 *
	 * THAT IT LANDS ON THE SAME NUMBER AS `JudgmentZonesTriggersForTheBonus` IS A
	 * CONSEQUENCE OF TWO JUDGEMENTS AND NOT A DESIGN FACT. The row states the five; the
	 * ramp's step and ceiling were chosen separately. A reader should not infer that one
	 * was derived from the other, and moving either does not move the other.
	 */
	static constexpr float JudgmentZonesMostPercentPerSecond = 5.0f;

	/**
	 * What the floor adds to the player's magic find on a boss kill, once earned.
	 *
	 * A JUDGEMENT, ruled under the project owner's delegation, AND THE LEAST EVIDENCED
	 * FIGURE IN THIS RULE. No built floor rule writes magic find at all; the six player
	 * stats a floor rule writes are max health, maximum energy shield, max mana, healing
	 * received, movement speed and health regeneration.
	 *
	 * THE UNIT WAS MEASURED RATHER THAN ASSUMED, because the data files disagree.
	 * `UCataclysmDropRoll` multiplies by `(1 + MagicFind / 100)`, so the stat is a
	 * PERCENTAGE where 100 means +100%, and `MagicFindCeiling` stops the effective figure
	 * at 400.
	 *
	 * TWENTY, WITH THE ARITHMETIC: one gear affix, `Stat_Flat_magic_find` in
	 * `game/Data/Affixes.csv`, is worth flat 10, so this is two affixes' worth. It is
	 * bought with five ticks of the ramp above, which is 1+2+3+4+5 = 15% of the player's
	 * maximum health, and it is 5% of the 400 ceiling, so it cannot crowd out gear.
	 */
	static constexpr float JudgmentZonesMagicFind = 20.0f;

	static_assert(
		JudgmentZonesSeconds > 0.0f && JudgmentZonesTriggersForTheBonus > 0
			&& JudgmentZonesMostZones > 0 && JudgmentZonesSecondsBetweenZones > 0.0f
			&& JudgmentZonesPercentPerSecond > 0.0f
			&& JudgmentZonesMostPercentPerSecond >= JudgmentZonesPercentPerSecond
			&& JudgmentZonesMagicFind > 0.0f,
		"A zone that does not stand, a bonus earned by nothing, no zones at all, a ramp "
		"that does not climb or whose ceiling is below its first step, or a reward of "
		"nothing is not the row.");

	/**
	 * What each floor adds to every creature's damage, and what killing the floor's
	 * Commander adds to the player's armour. Issues #1820 and #41.
	 *
	 * BOTH ARE THE ROW'S OWN FIGURES, and it states the same 10% for each:
	 * "enemies damage increases by 10%" and "increase the player's armor by 10%".
	 * `tools/tests/test_dungeon_modifier_rules_are_the_rows.py` fails if the row stops
	 * saying either.
	 *
	 * TWO CONSTANTS AND NOT ONE, THOUGH THEY HOLD THE SAME NUMBER. They are two figures
	 * in the row that happen to agree, on opposite sides of the trade, and tuning one
	 * must not move the other. `JudgmentZonesMostPercentPerSecond` carries the same
	 * warning about the same coincidence.
	 */
	static constexpr float MarchOfProgressEnemyDamagePercentPerFloor = 10.0f;
	static constexpr float MarchOfProgressArmourPercentPerCommander = 10.0f;

	static_assert(
		MarchOfProgressEnemyDamagePercentPerFloor > 0.0f
			&& MarchOfProgressArmourPercentPerCommander > 0.0f,
		"A floor that makes creatures no stronger, or a Commander worth no armour, is "
		"not the row.");

	static_assert(
		HolyRepercussionsChancePercentOnHit > 0.0f
			&& HolyRepercussionsChancePercentOnHit < 100.0f,
		"Holy Repercussions is a CHANCE on a blow. At zero nothing ever "
		"retaliates and the row is unbuilt; at a hundred every blow the player "
		"lands is answered and the row's own word 'chance' describes nothing.");

	static_assert(
		HolyRepercussionsBurstRadiusCm > 0.0f,
		"A burst that reaches nowhere reaches nobody, the player included.");

	static_assert(
		HolyRepercussionsJudgmentMostStacks > 1,
		"A cap of one is not a stacking debuff, and the row's word 'stacking' "
		"describes nothing.");

	static_assert(
		HolyRepercussionsJudgmentLessPerStackPercent > 0.0f
			&& HolyRepercussionsJudgmentLessPerStackPercent
				* HolyRepercussionsJudgmentMostStacks < 100.0f,
		"Judgment at the cap now takes all of a resistance or more. The row asks "
		"for holy damage TAKEN to increase, not for the resistance to stop "
		"existing, and the pipeline clamps a single Less at -99 so the figure "
		"would stop meaning what it says.");

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

	/**
	 * How far a commander's aura reaches, for Commander's Aura. Issues #1820 and #41.
	 *
	 * THE SUCCUBUS'S OWN FIGURE, 800 cm, AND NOT A SECOND ONE. `Dominion` grants this
	 * same tag to every ally within 8 metres, so a second distance for the same buff
	 * would mean the game empowered creatures at two different ranges with no row asking
	 * it to. Ruled under the project owner's delegation.
	 *
	 * WRITTEN HERE AND CHECKED AGAINST THE SUCCUBUS BY A PYTHON TEST rather than
	 * included from that creature's header. This library holds figures for the tests and
	 * the checks to read and does not include the creature classes; the check is what
	 * stops the two drifting apart.
	 */
	static constexpr float CommandersAuraRadiusCm = 800.0f;

	/**
	 * How long the buff a commander grants lasts, for Commander's Aura.
	 *
	 * RE-APPLIED EVERY BEAT RATHER THAN TRACKED, WHICH IS WHY THIS IS SHORT.
	 * `ApplyTagForDuration` keeps one effect per tag, so a second application refreshes
	 * the first rather than stacking. A creature that stays beside its commander keeps
	 * the buff; one that walks away loses it when this runs out, which is at most this
	 * long and in practice within a beat.
	 *
	 * ONE SECOND, WHICH IS `HallowedGroundfallEmpowerSeconds`. That rule grants this same
	 * tag from this same beat in exactly this shape, so its figure is used rather than a
	 * new one. The beat is a quarter second, so the buff is refreshed four times over
	 * before it could lapse.
	 */
	static constexpr float CommandersAuraGrantSeconds = HallowedGroundfallEmpowerSeconds;

	/**
	 * The lowest rung that commands: Elite, the rung above Common.
	 *
	 * DECLARED AS `RoyalGuardLowestRungThatSummons` AND NOT AS 1, for the reason
	 * `BloodForgedChampionsLowestRung` is: that constant already carries this project's
	 * answer to "which creatures count as Elite", with the reading of
	 * `game/Data/EnemyRarities.csv` that produced it. Rules meaning the same rung should
	 * not be able to drift apart.
	 */
	static constexpr int32 CommandersAuraLowestRung = RoyalGuardLowestRungThatSummons;

	static_assert(
		CommandersAuraRadiusCm > 0.0f && CommandersAuraGrantSeconds > 0.0f
			&& CommandersAuraLowestRung > 0,
		"An aura that reaches nowhere, a buff that lasts no time, or a rung that lets "
		"every Common command is not the row.");

	/**
	 * How many anti-magic zones may stand at once.
	 *
	 * THE ROW STATES NO FIGURE, SO NONE IS INVENTED. Every figure of this rule is the
	 * shared constant it copies, ruled under the project owner's delegation: Judgment
	 * Zones is the one built rule that lays timed ground near the player for a rule of
	 * its own to act on, and this rule has that shape exactly. So its three, its eight
	 * seconds and its twenty are used rather than a second set of the same kind.
	 */
	static constexpr int32 AntiMagicZonesMostZones = JudgmentZonesMostZones;

	/** How long between one anti-magic zone appearing and the next. Judgment Zones' own. */
	static constexpr float AntiMagicZonesSecondsBetweenZones =
		JudgmentZonesSecondsBetweenZones;

	/** How long an anti-magic zone stands. Judgment Zones' own. */
	static constexpr float AntiMagicZonesSeconds = JudgmentZonesSeconds;

	/**
	 * How far an anti-magic zone reaches.
	 *
	 * DECLARED AS `WitheredGroundPatchRadiusCm`, this project's settled answer for a
	 * patch of ground, which Judgment Zones and Spore Clouds declare themselves as too.
	 */
	static constexpr float AntiMagicZonesRadiusCm = WitheredGroundPatchRadiusCm;

	/**
	 * How far from the player a zone may appear.
	 *
	 * DECLARED AS `InfernalRainFallsWithinCm`, the settled answer for where ground laid
	 * near the player falls, shared with Singularity Wells and Judgment Zones.
	 */
	static constexpr float AntiMagicZonesFallsWithinCm = InfernalRainFallsWithinCm;

	/**
	 * What `skill_locked` is set to while the player stands in a zone.
	 *
	 * DECLARED AS `EdictOfSilenceLockValue`, because every reader of that stat asks only
	 * whether it is above zero, and one value meaning "locked" is enough for the game.
	 */
	static constexpr float AntiMagicZonesLockValue = EdictOfSilenceLockValue;

	// NO static_assert COMPARES THESE WITH WHAT THEY COPY, because each is declared AS
	// the other, and `X = Y` beside `X == Y` could never fail. The Python check
	// `test_anti_magic_zones_figures_are_the_shared_constants` reads the declarations
	// instead. This one asks something the declarations do not say.
	static_assert(
		AntiMagicZonesMostZones > 0 && AntiMagicZonesSecondsBetweenZones > 0.0f
			&& AntiMagicZonesSeconds > 0.0f && AntiMagicZonesRadiusCm > 0.0f
			&& AntiMagicZonesFallsWithinCm > AntiMagicZonesRadiusCm
			&& AntiMagicZonesLockValue > 0.0f,
		"No zones, zones that never come or never stand, zones of no size, zones that "
		"cannot be laid clear of the player, or a lock of nothing is not the row.");

	/**
	 * The share of maximum mana below which Desperate Measures turns a cast's
	 * cost into health. STATED BY THE ROW: "When your Mana falls below 10%".
	 * Strictly below, which is the reading `mana_below` takes.
	 */
	static constexpr float DesperateMeasuresManaBelowPercent = 10.0f;

	/**
	 * The share of CURRENT health a cast pays instead of its mana, once below
	 * that. STATED BY THE ROW: "your skills cost 5% of your current Health".
	 */
	static constexpr float DesperateMeasuresHealthPercent = 5.0f;

	static_assert(
		DesperateMeasuresManaBelowPercent > 0.0f
			&& DesperateMeasuresManaBelowPercent < 100.0f
			&& DesperateMeasuresHealthPercent > 0.0f
			&& DesperateMeasuresHealthPercent < 100.0f,
		"A threshold no mana is below or all mana is below, or a health cost of "
		"nothing or of everything, is not the row.");

	/**
	 * The share of its maximum health a creature rises with. STATED BY THE ROW:
	 * "resurrect at half health".
	 */
	static constexpr float DivineResurgenceHealthPercent = 50.0f;

	/**
	 * The share of the creatures the floor placed that must have died for the
	 * revival to come. A RULING UNDER THE OWNER'S DELEGATION, 2026-09-23: half, so it
	 * comes part-way through a clear rather than on a timer that runs regardless of
	 * play. Rounded up, so of five placed, three must fall.
	 */
	static constexpr int32 DivineResurgenceFallenPercent = 50;

	static_assert(
		DivineResurgenceHealthPercent > 0.0f && DivineResurgenceHealthPercent <= 100.0f
			&& DivineResurgenceFallenPercent > 0 && DivineResurgenceFallenPercent <= 100,
		"A creature rising with no health, or a revival that no number of deaths or "
		"none at all brings on, is not the row.");

	/**
	 * The chance a killed creature gets up again. THE ROW SAYS "a chance" AND GIVES NO
	 * FIGURE, so it is declared as `SporeCloudsChancePercentOnDeath`, the ten that
	 * constant's comment derives from the one row stating a chance on a death, rather
	 * than writing ten a third time. Ruled under the owner's delegation, 2026-09-23.
	 */
	static constexpr float DeadRisingChancePercent = SporeCloudsChancePercentOnDeath;

	static_assert(
		DeadRisingChancePercent > 0.0f && DeadRisingChancePercent < 100.0f,
		"A chance of nothing is not the row, and a certainty is not a chance.");

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
	 * The shares of the player's maximum health and maximum mana that Suffering Aura
	 * takes each second. THE ROW SAYS "slow but constant" AND GIVES NO FIGURE.
	 *
	 * THE PROJECT OWNER DECIDED THE INTENT ON 2026-09-23: a drain the player notices,
	 * enough to beat every class's base regeneration. These two figures are the
	 * coordinating session's under that intent. Base regeneration as a share of the
	 * maximum, over every class of game/Data/ClassStats.csv and every level from 1 to
	 * 100, is at most 2.00% a second for health (the Masochist at level 1) and 2.50% for
	 * mana (the Ravager, at every level). So a pool with nothing else refilling it
	 * falls. REGENERATION FROM ATTRIBUTES AND GEAR CAN STILL OUTPACE THEM.
	 */
	static constexpr float SufferingAuraHealthPercentPerSecond = 2.5f;
	static constexpr float SufferingAuraManaPercentPerSecond = 3.0f;

	static_assert(
		SufferingAuraHealthPercentPerSecond > 2.0f && SufferingAuraManaPercentPerSecond > 2.5f,
		"Suffering Aura no longer beats every class's base regeneration, which is what the "
		"project owner asked of it on 2026-09-23. If the class table changed, re-measure "
		"the highest base regeneration share and move these figures, not this check.");

	static_assert(
		SufferingAuraHealthPercentPerSecond < 100.0f && SufferingAuraManaPercentPerSecond < 100.0f,
		"A drain of a whole pool each second is not slow.");

	/**
	 * The share of the creatures the floor placed that the player must slay before the
	 * stairs open. THE ROW SAYS "enough" AND GIVES NO FIGURE. Ruled under the owner's
	 * delegation on 2026-09-23 as half, rounded up -- Divine Resurgence's share, so the
	 * two rules that count a floor's dead count it the same way. Less than all, because
	 * `Celestial_Lightforged_Walls` is the row that forces a full clear.
	 */
	static constexpr int32 BloodGatesSlainPercent = 50;

	static_assert(
		BloodGatesSlainPercent > 0 && BloodGatesSlainPercent < 100,
		"Stairs that open with nothing slain are not sealed, and stairs that need every "
		"creature slain are Lightforged Walls rather than this row.");

	/**
	 * How often the dirge crescendos. THE ROW GIVES NO FIGURE. Ruled under the owner's
	 * delegation on 2026-09-23 as the Edict of Silence's period, and declared as that
	 * constant rather than written a second time.
	 */
	static constexpr float DirgeResonanceEverySeconds = EdictOfSilenceEverySeconds;

	/** How long a crescendo's haste lasts. STATED BY THE ROW: "for 10 seconds". */
	static constexpr float DirgeResonanceHasteSeconds = 10.0f;

	/**
	 * What Scarcity's draw adds to the floor's seed, so it is not the stream that lays
	 * the floor out or the one that populates it. The same pattern as
	 * `FCataclysmDungeonFloorRules::ModifierSalt` and `FCataclysmFloorPopulator::
	 * PopulationSalt`; "sca" in ASCII.
	 */
	static constexpr int32 ScarcitySalt = 0x736361;

	/** A roll below this, of 0 to 100, takes the player down. STATED BY THE ROW: 50%. */
	static constexpr float UnstablePortalDescendPercent = 50.0f;

	/**
	 * A roll below this and at or above the last returns the player to the entrance; the
	 * rest raise a mini-boss. STATED BY THE ROW: 25% each.
	 */
	static constexpr float UnstablePortalReturnBelow = 75.0f;

	/**
	 * The mini-boss's rung. DECLARED AS `VolatileEvolutionHighestRung`, the Herald rung that
	 * Epidemic's Plague Lord and Blood-Forged Champions' ceiling already share as the
	 * mini-boss rung. Ruled under the owner's delegation, 2026-09-23.
	 */
	static constexpr int32 UnstablePortalMiniBossRung = VolatileEvolutionHighestRung;

	static_assert(
		UnstablePortalDescendPercent > 0.0f && UnstablePortalDescendPercent < UnstablePortalReturnBelow
			&& UnstablePortalReturnBelow < 100.0f,
		"Each of the row's three outcomes must keep some share of the roll.");

	/**
	 * The share of a killed creature's maximum health, and of its attack damage, that the
	 * void keeps for the final boss. THE ROW SAYS "a portion" AND GIVES NO FIGURE: five is
	 * a judgement, ruled under the owner's delegation on 2026-09-23.
	 */
	static constexpr float NothingIsForgottenPortionPercent = 5.0f;

	/**
	 * The most attack damage the void may add to the final boss, as a share of the boss's
	 * own: it hits at most twice as hard. Ruled under the owner's delegation, 2026-09-23.
	 * Its health has no cap.
	 */
	static constexpr float NothingIsForgottenMostDamagePercent = 100.0f;

	static_assert(
		NothingIsForgottenPortionPercent > 0.0f && NothingIsForgottenPortionPercent < 100.0f
			&& NothingIsForgottenMostDamagePercent > 0.0f,
		"A portion of nothing or of everything is not the row, and a cap of nothing feeds "
		"the boss no damage at all.");

	static_assert(
		DirgeResonanceHasteSeconds > 0.0f
			&& DirgeResonanceHasteSeconds < DirgeResonanceEverySeconds,
		"A haste of no length is not the row, and one as long as the gap between "
		"crescendos never ends.");

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

	/** Whether a blow the player just landed is answered with a burst. */
	static bool HolyRepercussionsRetaliates(float Roll);

	/**
	 * The Judgment count after a burst, which stops at the cap.
	 *
	 * IT SATURATES RATHER THAN WRAPPING, which is the difference from
	 * `BrandStacksAfterHit`. That count clears itself at its threshold because
	 * its row erupts; this one is a debuff that stays, so the cap holds it.
	 */
	static int32 HolyRepercussionsStacksAfterBurst(int32 Stacks);

	/** What a Judgment count takes off the player's Celestial resistance. */
	static float HolyRepercussionsJudgmentLessPercent(int32 Stacks);

	/** What one cloud asks to drain from a player with this maximum health. */
	static float LeechSporesDrain(float MaximumHealth);

	/**
	 * What each creature is healed by when a drain is shared among them.
	 *
	 * EQUAL SHARES OF WHAT WAS ACTUALLY DRAINED, so the total healed never
	 * exceeds the total taken. Nothing for no creatures, and nothing for a drain
	 * that took nothing -- both are ordinary, not faults.
	 */
	static float LeechSporesHealEach(float Drained, int32 Creatures);

	/**
	 * What one pulse takes from a player with this maximum health, once the altar has
	 * counted this many deaths.
	 *
	 * NOTHING FOR NO DEATHS, and the share stops growing at
	 * `BloodAltarDeathsToCeiling`. Nothing for a maximum health that is not positive.
	 */
	static float BloodAltarPulseDamage(float MaximumHealth, int32 Deaths);

	/**
	 * The altar's count after one more death.
	 *
	 * IT SATURATES AT `BloodAltarDeathsToCeiling`, the shape
	 * `HolyRepercussionsStacksAfterBurst` has: a death past the ceiling changes
	 * nothing a pulse does, so the count stops where the damage stops.
	 */
	static int32 BloodAltarDeathsAfterOne(int32 Deaths);

	/** Whether the altar pulses this long after the floor's start or its last pulse. */
	static bool BloodAltarPulseIsDue(float SecondsSinceLastPulse);

	/**
	 * Whether the fog spreads to another patch, this long after the floor's start or its
	 * last patch, with this many already on the floor.
	 *
	 * THE CAP FIRST, so a floor at its limit does no arithmetic.
	 */
	static bool NecroticGroundPatchIsDue(float SecondsSinceLastPatch, int32 PatchesAlive);

	/** What one burn takes from a player with this maximum health; nothing for none. */
	static float NecroticGroundBurn(float MaximumHealth);

	/**
	 * What a creature standing in the fog regains in one beat of this many seconds, for
	 * a creature with this maximum health. Nothing for either not positive.
	 */
	static float NecroticGroundRegenPerBeat(float MaximumHealth, float BeatSeconds);

	/**
	 * How many Ravenous Hoard stacks a creature alive this many seconds holds: one for
	 * each whole `RavenousHoardSecondsPerStack`, at most `RavenousHoardMostStacks`.
	 *
	 * THE CAP IS HERE AND NOWHERE ELSE, so there is one place it can be wrong.
	 */
	static int32 RavenousHoardStacksAfter(float SecondsAlive);

	/** What a creature holding this many stacks multiplies its attack damage by. */
	static float RavenousHoardDamageMultiplier(int32 Stacks);

	/**
	 * Whether Grave Tide's next wave is due: this long since the last one, and fewer
	 * than `GraveTideMostWaves` waves so far.
	 *
	 * THE CEILING FIRST, so a floor at its limit does no arithmetic.
	 */
	static bool GraveTideWaveIsDue(float SecondsSinceLastWave, int32 WavesSoFar);

	/** How many creatures rise in the wave after this many waves. */
	static int32 GraveTideCreaturesInWave(int32 WavesSoFar);

	/**
	 * What the creatures of the wave after this many waves are placed with, as a
	 * multiplier on their own attack damage, held to `GraveTideMostDamagePercent`.
	 */
	static float GraveTideDamageMultiplier(int32 WavesSoFar);

	/**
	 * Whether a creature at this health is wounded enough to mutate: below
	 * `VolatileEvolutionHealthPercentToMutate` of the maximum given.
	 *
	 * FALSE FOR A MAXIMUM OF ZERO OR LESS, which is a creature whose attributes have not
	 * been written yet rather than one at death's door.
	 */
	static bool VolatileEvolutionIsWounded(float Health, float MaxHealth);

	/**
	 * The rung a creature at this rung reaches when it mutates, held to
	 * `VolatileEvolutionHighestRung`.
	 *
	 * THE CEILING IS HERE AND NOWHERE ELSE, so there is one place it can be wrong, which
	 * is what `RavenousHoardStacksAfter` says about its own cap.
	 */
	static int32 VolatileEvolutionRungAfter(int32 RarityStep);

	/**
	 * Whether a creature at this health is hurt enough to call its guards: below
	 * `RoyalGuardHealthPercentToSummon` of the maximum given.
	 *
	 * FALSE FOR A MAXIMUM OF ZERO OR LESS, for the reason `VolatileEvolutionIsWounded`
	 * gives: that is a creature whose attributes have not been written yet.
	 */
	static bool RoyalGuardIsWounded(float Health, float MaxHealth);

	/** Whether a creature of this rung calls guards at all. */
	static bool RoyalGuardMaySummon(int32 RarityStep);

	/**
	 * The rung the guards of a creature at this rung arrive at: one higher, held to
	 * `RoyalGuardHighestRung`.
	 *
	 * THE CEILING IS HERE AND NOWHERE ELSE, so there is one place it can be wrong.
	 */
	static int32 RoyalGuardRungForGuards(int32 RarityStep);

	/** Whether a roll of 0 to 100 brings a greater creature out of a corpse. */
	static bool DemonPrinceRises(float Roll);

	/**
	 * Whether another may rise on a floor that has already had this many.
	 *
	 * THE CEILING IS HERE AND NOWHERE ELSE, so there is one place it can be wrong.
	 */
	static bool DemonPrinceMayRise(int32 RisenSoFar);

	/** Whether a roll of 0 to 100 passes the corpse's debuffs on. */
	static bool EpidemicSpreads(float Roll);

	/**
	 * Whether a chain of this many spreads is the one that kills everything nearby.
	 *
	 * THE CEILING IS HERE AND NOWHERE ELSE, so there is one place it can be wrong.
	 */
	static bool EpidemicChainIsComplete(int32 Spreads);

	/** The reach, in the centimetres Unreal works in. */
	static float EpidemicRadiusCm();

	/** The reach a death has to be inside to feed a champion, in centimetres. */
	static float BloodForgedChampionsRadiusCm();

	/**
	 * Whether a creature at this rung absorbs a death at all.
	 *
	 * BOTH ENDS ARE HERE AND NOWHERE ELSE, so there is one place either can be wrong. A
	 * Common is below the floor and absorbs nothing; a creature already at the ceiling is
	 * refused here rather than being fed and then found to have nowhere to rise.
	 */
	static bool BloodForgedChampionsAbsorbs(int32 RarityStep);

	/** Whether the deaths fed since a champion's last rung have earned another. */
	static bool BloodForgedChampionsRungIsEarned(int32 DeathsSinceItsLastRung);

	/** The rung after this one, never past the ceiling. */
	static int32 BloodForgedChampionsRungAfter(int32 RarityStep);

	/** Whether a roll of 0 to 100 leaves a wraith where a creature fell. */
	static bool VengefulWraithRises(float Roll);

	/**
	 * A figure raised by the row's one increase.
	 *
	 * ONE FUNCTION FOR ALL THREE STATS, because the row states one figure for all three
	 * and three call sites reading the same constant would be the same arithmetic written
	 * three times.
	 */
	static float VengefulWraithsIncreased(float Base);

	/** Whether another judgment zone is due, given the clock and how many stand now. */
	static bool JudgmentZoneIsDue(float SecondsSinceLastZone, int32 StandingNow);

	/**
	 * What a share of maximum health this second in a zone costs, as a percentage.
	 *
	 * THE RAMP IS HERE AND NOWHERE ELSE, so there is one place its shape can be wrong.
	 * `SecondsStoodIn` is how many ticks this rule has already dealt to the player in the
	 * zone they are standing in, so the first tick asks with nothing stood.
	 */
	static float JudgmentZonesPercentAfter(int32 SecondsStoodIn);

	/** What that share is of a real maximum health, or nothing for a character with none. */
	static float JudgmentZonesDamageFor(float MaxHealth, int32 SecondsStoodIn);

	/** Whether this many ticks have earned the floor's loot bonus. */
	static bool JudgmentZonesBonusIsEarned(int32 Triggers);

	/**
	 * What magic find a drop roll should use, given who died and what the floor grants.
	 *
	 * **SPLIT OUT SO THE READING CAN BE TESTED AT ALL**, which is the same split
	 * `UCataclysmEnemyScore::FloorFor` is written for and for the same reason. The caller
	 * is `ACataclysmEnemyCharacter::HandleDeath`, which finds the floor's bonus through
	 * `UWorld::GetAuthGameMode` -- and an automation world has no authority game mode, so
	 * a test can reach this arithmetic and cannot reach that lookup.
	 *
	 * THE BONUS IS FOR A BOSS AND NOTHING ELSE, which is what the row says: "increases
	 * BOSS loot quality".
	 */
	static float JudgmentZonesMagicFindFor(float PlayersMagicFind, bool bVictimIsBoss,
										   float FloorBonus);

	/**
	 * What every creature on this floor multiplies its designed attack damage by.
	 *
	 * ADDITIVE AND NOT COMPOUNDING, WHICH IS A JUDGEMENT ON A SENTENCE THAT COULD BE
	 * READ EITHER WAY. "Each floor, enemies damage increases by 10%" gives floor N a
	 * multiplier of (1 + 0.10 x N), so the tenth floor is twice. Read as compounding it
	 * would be 1.10^N, which is 2.59 times at ten floors and 117 times at fifty, and no
	 * amount of armour answers that. `docs/DECISIONS.md` records the rejected reading.
	 *
	 * A MULTIPLIER RATHER THAN A SHARE, because that is what the creature takes.
	 * `ACataclysmEnemyCharacter::SetFloorDepthDamageMultiplier` multiplies the creature's
	 * DESIGNED damage, so 1.0 means a creature dealing exactly what its kind deals.
	 *
	 * FLOOR 1 IS ALREADY 10% AND THAT IS THE ROW AS WRITTEN. "Each floor" includes the
	 * floor the rule is drawn on; a player who takes the row and sees nothing happen has
	 * been told the rule is running and shown that it is not.
	 *
	 * @param FloorNumber which floor, counted from 1. Below 1 is read as 1
	 */
	static float MarchOfProgressDamageMultiplierOnFloor(int32 FloorNumber);

	/**
	 * How much more armour the player carries for the commanders they have killed, as a
	 * percentage, or nothing for a player who has killed none.
	 *
	 * ONE FIGURE FOR THE WHOLE RUN AND NOT ONE PER FLOOR. The row pays for killing "the
	 * Commander in each level" and says nothing about the payment ending, so the count is
	 * cleared when the run ends and not when the floor changes.
	 *
	 * IT BECOMES ONE MODIFIER AND NOT ONE PER COMMANDER, which is what makes it additive.
	 * `StatModifiersFor` writes a single More multiplier of this value, so three
	 * commanders are x1.3; three separate 10% modifiers would have compounded to x1.331,
	 * because `UCataclysmStatPipeline` multiplies each source on its own.
	 *
	 * @param CommandersKilled how many of this run's commanders the player has killed.
	 *                         Below zero is read as none
	 */
	static float MarchOfProgressArmourMorePercentFor(int32 CommandersKilled);

	/**
	 * Whether a creature at this rung of the rarity ladder commands, for Commander's
	 * Aura.
	 *
	 * A FUNCTION OF THE RUNG AND NOTHING ELSE, so the reading of "certain elite enemies"
	 * can be checked by passing a number rather than by building a floor. Every creature
	 * at Elite or above commands; the row states no count, so there is none.
	 */
	static bool CommandersAuraCommandsAtRung(int32 RarityStep);

	/** Whether another anti-magic zone is due, given the clock and how many stand now. */
	static bool AntiMagicZoneIsDue(float SecondsSinceLastZone, int32 StandingNow);

	/**
	 * Whether Divine Resurgence is due: at least `DivineResurgenceFallenPercent` of
	 * `Placed` have fallen, rounded UP, and at least one was placed. Whole numbers
	 * throughout, so "half of five" is three and never a float that rounds the wrong way.
	 */
	static bool DivineResurgenceIsDue(int32 Fallen, int32 Placed);

	/** The health a creature rises with: `DivineResurgenceHealthPercent` of its maximum. */
	static float DivineResurgenceHealthFor(float MaxHealth);

	/** Whether a roll of 0 to 100 gets a killed creature up again under Dead Rising. */
	static bool DeadRisingRevives(float Roll);

	/**
	 * What Suffering Aura takes from a pool with this maximum over this many seconds, at
	 * this share of the maximum each second. Nothing from an empty maximum or a time of
	 * none or less.
	 */
	static float SufferingAuraLossFor(float Maximum, float PercentPerSecond, float Seconds);

	/**
	 * How many of `Placed` the player must have slain for the stairs to open:
	 * `BloodGatesSlainPercent` of them, rounded UP, in whole numbers. Nothing when nothing
	 * was placed.
	 */
	static int32 BloodGatesOpenAt(int32 Placed);

	/** Whether the stairs are open: `Slain` has reached `BloodGatesOpenAt(Placed)`. */
	static bool BloodGatesAreOpen(int32 Slain, int32 Placed);

	/**
	 * Whether a crescendo is due: `DirgeResonanceEverySeconds` of the floor's beat have
	 * passed since the last one, or since the floor began. A negative wait brings none.
	 */
	static bool DirgeResonanceIsDue(float SecondsSinceLast);

	/**
	 * Which of `Candidates` worn slots Scarcity switches off on this floor of this
	 * dungeon, or `INDEX_NONE` when there are none. From a stream seeded by the dungeon's
	 * seed, the floor number and `ScarcitySalt`, so it is the same every time it is asked.
	 */
	static int32 ScarcityPick(int32 Candidates, int32 DungeonSeed, int32 FloorNumber);

	/** What one step through an unstable portal does: these three values. */
	static constexpr int32 UnstablePortalDescends = 0;
	static constexpr int32 UnstablePortalReturns = 1;
	static constexpr int32 UnstablePortalRaisesAMiniBoss = 2;

	/**
	 * What a step does, from a roll of 0 to 100: below 50 down, below 75 back to the
	 * entrance, otherwise a mini-boss. One of the three values above.
	 */
	static int32 UnstablePortalOutcomeFor(float Roll);

	/** What the void keeps of one killed creature's figure: the portion, never below none. */
	static float NothingIsForgottenPortionOf(float Figure);

	/**
	 * The attack damage the void adds to a boss whose own is `BossOwnDamage`, holding
	 * `Held`: all of it, up to `NothingIsForgottenMostDamagePercent` of the boss's own.
	 */
	static float NothingIsForgottenDamageAdded(float Held, float BossOwnDamage);

	/**
	 * What `skill_locked` on the player's spells should be, given whether they stand in
	 * an anti-magic zone: `AntiMagicZonesLockValue` inside, nothing outside.
	 */
	static float SpellsLockedWhile(bool bInsideAZone);

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
