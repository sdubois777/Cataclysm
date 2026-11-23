// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * What a kill's rewards are multiplied by, for the gap between the dungeon's score and the player's. Issue #2824,
 * slice c. THE DEFAULT CHANGES NOTHING: both factors exactly 1 and `bScored` false, which is what a kill gets outside a
 * dungeon of the empire, or with no player to score.
 */
struct CATACLYSM_API FCataclysmPowerGapRewards
{
	/** The creature's experience figure, and the skills' share that follows it, are multiplied by this. */
	double Experience = 1.0;

	/** The kill's loot quantity (its gear, its materials and its gems) and its gold multiplier are multiplied by this. */
	double Loot = 1.0;

	/** Whether both scores were known, so the factors were worked out and the screens have a line to show. */
	bool bScored = false;

	/** The experience figure after this factor, rounded to the nearest whole point: the figure itself at exactly 1, so a
	 *  kill outside an empire dungeon grants exactly what it did. */
	int32 ExperienceFrom(int32 Figure) const
	{
		return Experience == 1.0 ? Figure : FMath::RoundToInt32(static_cast<double>(Figure) * Experience);
	}
};

/** What a kill hands the drop spawner after the loot factor: its loot quantity and its gold multiplier. */
struct CATACLYSM_API FCataclysmPowerGapKillLoot
{
	/** The loot quantity every roll of the kill uses: gear, materials, the tree's extra drops and gems. */
	float LootQuantity = 0.0f;

	/** The multiplier on each gold pile. */
	float GoldMultiplier = 0.0f;
};

/**
 * THE RULE FOR A KILL'S EXPERIENCE AND LOOT BY THE POWER SCORE GAP. Issue #2824, slice c: the coordinating session's
 * rule D and the owner's loot floor, both of 2026-11-22, recorded in `docs/DECISIONS.md`.
 *
 * `docs/Cataclysm_GDD_v2.md`, section IV, "Power Score": "Dungeons within a close power range provide average exp and
 * loot. Dungeons below the threshold provide less. Dungeons above provide more." The design states the direction; the
 * numbers below are the rulings.
 *
 * A PORT OF `sim/analyse_power_gap_rewards.py`'s `Rule.multiplier` for `RULED`, the same comparisons in the same order.
 * `tools/tests/test_power_gap_rewards_port.py` compares each constant here with that rule's field and every row of the
 * automation test's table with the simulation's figure, so a change on either side fails there.
 *
 *     gap = (Dungeon Score - player's Power Score) / the dungeon's tier width, positive when the dungeon is above
 *     gap >  BandAbove:   1 + BonusCap x min(1, (gap - BandAbove) / BonusRamp)
 *     gap < -BandBelow:   1 - (1 - floor) x min(1, (-gap - BandBelow) / CutRamp), the floor ExperienceFloor or LootFloor
 *     otherwise:          1
 */
struct CATACLYSM_API FCataclysmPowerGapRule
{
	/** The bonus begins this far (in tier widths) above the player: the simulation's `band`. */
	static constexpr double BandAbove = 0.25;

	/** The cut begins this far below the player: `band_down`. It holds the reference character, which sits 0.48 to 0.50
	 *  of a width above its own tier's Dungeon Score, with about 0.11 to spare. */
	static constexpr double BandBelow = 0.6;

	/** The most the bonus adds: `cap_up`, Diablo IV's 25% cap. */
	static constexpr double BonusCap = 0.25;

	/** How far beyond the band the bonus reaches its cap: `ramp_up`. */
	static constexpr double BonusRamp = 0.5;

	/** How far beyond the band the cut reaches its floor: `ramp_down`. */
	static constexpr double CutRamp = 1.0;

	/** What experience falls to: `floor`, Diablo II's 5%. */
	static constexpr double ExperienceFloor = 0.05;

	/** What loot quantity and the gold multiplier fall to: `loot_floor`, the owner's 25%. */
	static constexpr double LootFloor = 0.25;

	/** The factor at `Gap`, for experience or (with `bLoot`) for loot. The bonus above the band is the same for both. */
	static double Multiplier(double Gap, bool bLoot);

	/** `(DungeonScore - PlayerScore) / UCataclysmEnemyScore::TierWidth(DifficultyTier)`. */
	static double GapOf(int32 DungeonScore, int32 PlayerScore, int32 DifficultyTier);

	/**
	 * What a kill hands the drop spawner: `LootQuantity` (the player's, after the city's "more loot") and
	 * `GoldMultiplier` (the empire tree's times the city's), each times `Rewards.Loot`. What
	 * `ACataclysmEnemyCharacter::HandleDeath` calls, so a test reads the factor's effect without counting random drops.
	 * Both unchanged at a factor of exactly 1. Issue #2824, slice c.
	 */
	static FCataclysmPowerGapKillLoot KillLootFor(float LootQuantity, float GoldMultiplier,
												  const FCataclysmPowerGapRewards& Rewards);

	/** Both factors for these scores, or the default (1, 1, not scored) when either score is `INDEX_NONE`. */
	static FCataclysmPowerGapRewards RewardsFor(int32 DungeonScore, int32 PlayerScore, int32 DifficultyTier);

	/**
	 * The factors as the screens show them, beside the Dungeon Score: "Rewards 112%." when experience and loot round to
	 * the same whole percentage, "Rewards: experience 5%, loot 25%." when they do not, and empty when not scored.
	 */
	static FString RewardsText(const FCataclysmPowerGapRewards& Rewards);
};
