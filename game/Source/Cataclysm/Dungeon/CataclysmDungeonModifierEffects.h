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
	 * the beat rather than once a floor, and it is the only field here that can
	 * go back to nothing without the floor changing.
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
	 * THE ONLY RULE HERE PLACED BY AN EVENT RATHER THAN BY A CLOCK. Infernal
	 * Rain and Singularity Wells both drop a hazard on a cadence and cap how
	 * many may exist. This one places a patch every time a creature dies, and
	 * caps nothing, because the row states the trigger and states no limit:
	 * a cap would make "enemies leave patches on death" stop being true at
	 * whichever enemy hit it.
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
	 * is 10 where the heaviest in the set are 15. NOT SIXTY: this is not the
	 * harshest recovery row in the set, and the two that are -- Necrotic Ground's
	 * 50% and Withered Ground's 80% -- state their own figures in the data.
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
