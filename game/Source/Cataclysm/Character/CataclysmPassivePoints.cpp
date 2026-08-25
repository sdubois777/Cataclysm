// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmPassivePoints.h"
#include "Character/CataclysmExperience.h"

int32 UCataclysmPassivePoints::FromLevel(int32 Level)
{
	const int32 Clamped = FMath::Clamp(Level, UCataclysmExperience::FirstLevel,
									   UCataclysmExperience::MaxLevel);

	// THE BONUS COUNTS COMPLETED TENS, so level 9 has had none and level 10 has
	// had one. Integer division is that count, and it is why level 100 gives ten
	// bonuses rather than nine or eleven.
	return Clamped * PerLevel + (Clamped / BonusEvery) * PerTenLevels;
}

int32 UCataclysmPassivePoints::FromBossKills(int32 UniqueBossesDefeated)
{
	// CLAMPED AT BOTH ENDS. Below zero is nonsense; above the number of unique
	// bosses would mean a boss was counted twice, which the save record's set of
	// names is what prevents. Clamping here as well means a record edited by
	// hand cannot hand a character more than the budget.
	const int32 Clamped = FMath::Clamp(UniqueBossesDefeated, 0, UniqueBosses);
	return Clamped * PerFirstBossKill;
}

int32 UCataclysmPassivePoints::Available(int32 Level, int32 UniqueBossesDefeated)
{
	return FromLevel(Level) + FromBossKills(UniqueBossesDefeated);
}
