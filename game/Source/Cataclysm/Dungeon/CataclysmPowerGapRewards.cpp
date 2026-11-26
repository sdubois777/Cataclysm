// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmPowerGapRewards.h"
#include "Dungeon/CataclysmEnemyScore.h"

double FCataclysmPowerGapRule::BandBelow(int32 DifficultyTier)
{
	// TIERS 1 TO 8, the simulation's `RULED.band_down_by_tier`; `tools/tests/test_power_gap_rewards_port.py` reads this
	// line back.
	static constexpr double ByTier[] = { 1.4, 1.3, 1.1, 1.0, 0.9, 0.9, 0.8, 0.8 };
	// HELD INSIDE 1 TO 8, where the simulation refuses a tier outside them: a console variable can set any tier, and a
	// kill must still be paid something.
	const int32 Tier = FMath::Clamp(DifficultyTier, 1, static_cast<int32>(UE_ARRAY_COUNT(ByTier)));
	return ByTier[Tier - 1];
}

double FCataclysmPowerGapRule::Multiplier(double Gap, bool bLoot, int32 DifficultyTier)
{
	// AS `Rule.multiplier` WRITES IT, line for line: the band's lower side at the tier, the floor, then above, below,
	// inside.
	const double Below = BandBelow(DifficultyTier);
	const double Floor = bLoot ? LootFloor : ExperienceFloor;
	if (Gap > BandAbove)
	{
		return 1.0 + BonusCap * FMath::Min(1.0, (Gap - BandAbove) / BonusRamp);
	}
	if (Gap < -Below)
	{
		return 1.0 - (1.0 - Floor) * FMath::Min(1.0, (-Gap - Below) / CutRamp);
	}
	return 1.0;
}

double FCataclysmPowerGapRule::GapOf(int32 DungeonScore, int32 PlayerScore, int32 DifficultyTier)
{
	return (static_cast<double>(DungeonScore) - static_cast<double>(PlayerScore))
		/ UCataclysmEnemyScore::TierWidth(DifficultyTier);
}

FCataclysmPowerGapKillLoot FCataclysmPowerGapRule::KillLootFor(float LootQuantity, float GoldMultiplier,
															   const FCataclysmPowerGapRewards& Rewards)
{
	FCataclysmPowerGapKillLoot Out;
	Out.LootQuantity = LootQuantity * static_cast<float>(Rewards.Loot);
	Out.GoldMultiplier = GoldMultiplier * static_cast<float>(Rewards.Loot);
	return Out;
}

FCataclysmPowerGapRewards FCataclysmPowerGapRule::RewardsFor(int32 DungeonScore, int32 PlayerScore,
															 int32 DifficultyTier)
{
	FCataclysmPowerGapRewards Out;
	// NO SCORE, NO CHANGE: a dungeon not on the map, or nobody playing, pays what it did before this rule.
	if (DungeonScore == INDEX_NONE || PlayerScore == INDEX_NONE || UCataclysmEnemyScore::TierWidth(DifficultyTier) <= 0.0)
	{
		return Out;
	}
	const double Gap = GapOf(DungeonScore, PlayerScore, DifficultyTier);
	Out.Experience = Multiplier(Gap, /*bLoot=*/false, DifficultyTier);
	Out.Loot = Multiplier(Gap, /*bLoot=*/true, DifficultyTier);
	Out.bScored = true;
	return Out;
}

FString FCataclysmPowerGapRule::RewardsText(const FCataclysmPowerGapRewards& Rewards)
{
	if (!Rewards.bScored)
	{
		return FString();
	}
	const int32 Experience = FMath::RoundToInt32(Rewards.Experience * 100.0);
	const int32 Loot = FMath::RoundToInt32(Rewards.Loot * 100.0);
	return Experience == Loot
		? FString::Printf(TEXT("Rewards %d%%."), Experience)
		: FString::Printf(TEXT("Rewards: experience %d%%, loot %d%%."), Experience, Loot);
}
