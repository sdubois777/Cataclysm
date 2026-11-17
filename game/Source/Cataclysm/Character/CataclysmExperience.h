// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CataclysmExperience.generated.h"

/**
 * What a character level costs, ported from `sim/analyse_experience_curve.py`.
 *
 * THE CURVE. A level costs 8.2% more than the level below it, and level 2 costs
 * 230,000:
 *
 *     cost of level L = 230,000 x 1.082 ^ (L - 2),   for L from 2 to 100
 *
 * Level 100 costs 519,995,268 and the whole climb 6,858,596,102.
 *
 * WHERE 8.2% COMES FROM, because it is not a number anybody picked. Path of
 * Exile publishes its whole experience table, and 45.50% of the climb to level
 * 100 is spent between levels 90 and 100. Fitting that one figure fixes the
 * rate, and the other two checkpoints then agree without being fitted: 1.90% of
 * the climb is spent by level 50 against their 1.28%, and the last level alone
 * is 7.58% against their 7.47%. `docs/DECISIONS.md`, 2026-08-24.
 *
 * WHERE 230,000 COMES FROM. The size of the climb, not a feel. A run is played
 * at a fixed difficulty tier, so passing through all eight tiers is eight
 * campaigns; a campaign is about 26 dungeons; so the climb is 208 dungeons, and
 * 230,000 is what makes those 208 dungeons pay for exactly 99 levels. Level 100
 * arrives at the end of difficulty tier 8.
 *
 * WHY THE ROUNDING IS SPELLED OUT AND TESTED. A player pays each level's cost as
 * a WHOLE number, so the climb is the sum of 99 roundings and not the rounding
 * of a geometric sum. Those two differ by 5 over the whole climb -- nothing as a
 * quantity, and everything to a save record that stores how far into a level a
 * character is. `floor(x + 0.5)` is the rounding, matching `_js_round` in
 * `sim/cataclysm_sim/scoring.py`, so a half goes up on both sides.
 *
 * WHAT THIS DOES NOT DO. It does not say what a kill is worth. The design says
 * an enemy's Enemy Score IS the experience it grants, and Enemy Score has no
 * port in this project at all -- it lives only in
 * `sim/cataclysm_sim/scoring.py`. Porting it needs three inputs the game does
 * not have yet: a dungeon's total floor count, its type and its sub-type.
 * `ACataclysmDungeonGameMode` says so itself -- "There is no bottom to the
 * dungeon, so the stairs go down for ever". Issue #926 is the port and the
 * stand-in it needs; issue #41 is where a real dungeon object lands.
 *
 * `tools/tests/test_experience_curve_port.py` compares every constant and a
 * spread of levels against the Python, because two copies of a number are two
 * numbers and this repository has watched that go wrong twice already.
 */
UCLASS()
class CATACLYSM_API UCataclysmExperience : public UObject
{
	GENERATED_BODY()

public:
	/** `docs/Cataclysm_GDD_v2.md` section XII: "The max level is 100." */
	static constexpr int32 MaxLevel = 100;

	/** A character is created at level 1 and has paid nothing. */
	static constexpr int32 FirstLevel = 1;

	/** How much more each level costs than the one below it. */
	static constexpr double GrowthPerLevel = 0.082;

	/** What the step from level 1 to level 2 costs. Every other level is this
	 *  multiplied by the growth, so it sets the size of the whole climb. */
	static constexpr int64 SecondLevelCost = 230000;

	/**
	 * What the step from `Level - 1` to `Level` costs.
	 *
	 * ZERO OUTSIDE 2 TO MaxLevel, rather than an error. Level 1 costs nothing to
	 * reach because a character starts there, and there is nothing above the
	 * maximum. A caller asking for either is asking a reasonable question with a
	 * zero answer, and returning zero is what lets `TotalToReach` and
	 * `Grant` below stay free of boundary special cases.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Experience")
	static int64 CostOfLevel(int32 Level);

	/** Every level cost from 2 to `Level`, summed. What a character of that
	 *  level has earned in total. Zero at level 1 and below. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Experience")
	static int64 TotalToReach(int32 Level);

	/**
	 * Add experience to a character, raising its level as far as the amount pays
	 * for and leaving the remainder as progress into the level it lands on.
	 *
	 * WHAT IS STORED IS PROGRESS INTO THE CURRENT LEVEL, NOT A RUNNING TOTAL,
	 * which is the shape `FCataclysmCharacterRecord` already declared: an int32
	 * `Level` beside an int64 `Experience` described as "Experience toward the
	 * next level". A running total would make the level derivable and therefore
	 * a second copy of the same fact, and worse, retuning the curve would
	 * silently move every existing character's level. Progress into the current
	 * level survives a retune: the character keeps the level it earned.
	 *
	 * AT THE MAXIMUM LEVEL THE REMAINDER IS DISCARDED and `OutExperience` is set
	 * to zero, rather than accumulating toward a level that does not exist. A
	 * stored number that can only grow and can never be spent is a number that
	 * eventually overflows and never does anything useful first.
	 *
	 * @param Amount        experience to add. Zero and negative amounts do
	 *                      nothing at all, so a caller need not check.
	 * @param InOutLevel    the character's level, raised in place. Clamped into
	 *                      1 to MaxLevel on the way in, because this is reached
	 *                      from a save record and a save record can hold
	 *                      anything.
	 * @param InOutExperience progress into the current level, updated in place.
	 * @return how many levels were gained, which is what a caller wanting to
	 *         award a point per level needs and cannot recover afterwards.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Experience")
	static int32 Grant(int64 Amount, UPARAM(ref) int32& InOutLevel,
					   UPARAM(ref) int64& InOutExperience);

	/**
	 * The level a total amount of experience reaches, counting from level 1.
	 *
	 * FOR REPORTING AND FOR TESTS, not for play. Play adds experience through
	 * `Grant` above, which keeps the level a character earned even if the curve
	 * is later retuned. This walks the curve from the bottom instead, so it says
	 * what a character WOULD be on today's curve.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Experience")
	static int32 LevelForTotal(int64 TotalExperience);

	/** True when the character can gain no more levels. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Experience")
	static bool IsMaxLevel(int32 Level) { return Level >= MaxLevel; }
};
