// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmPowerGapRewards.h"
#include "Dungeon/CataclysmEnemyScore.h"

double FCataclysmPowerGapRule::Multiplier(double Gap, bool bLoot)
{
	// AS `Rule.multiplier` WRITES IT, line for line: the band's lower side, the floor, then above, below, inside.
	const double Below = BandBelow;
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
	Out.Experience = Multiplier(Gap, /*bLoot=*/false);
	Out.Loot = Multiplier(Gap, /*bLoot=*/true);
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
