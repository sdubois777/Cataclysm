// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmPassivePoints.h"
#include "Character/CataclysmExperience.h"
// For FCataclysmStatModifier, which GrantedByWornRows reads field by field.
#include "AbilitySystem/CataclysmStatPipeline.h"

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

const TCHAR* UCataclysmPassivePoints::GrantedByGearStat = TEXT("class_points_granted");

int32 UCataclysmPassivePoints::GrantedByWornRows(
	const TMap<FName, TArray<FCataclysmStatModifier>>& WornModifiers)
{
	const TArray<FCataclysmStatModifier>* Rows =
		WornModifiers.Find(FName(GrantedByGearStat));
	if (!Rows)
	{
		return 0;
	}

	int32 Points = 0;
	for (const FCataclysmStatModifier& Row : *Rows)
	{
		// ONLY A ROW THAT GRANTS A FIXED NUMBER OF POINTS TO THE WHOLE CHARACTER
		// IS COUNTED. This function reads the row's value directly and runs no
		// pipeline, so it cannot judge a condition, count a scale's steps or
		// match a skill's tags. A row carrying any of those grants no point, and
		// the generator refuses to write one
		// (`refuse_a_class_point_row_the_game_cannot_count`).
		if (Row.Bucket != ECataclysmStatBucket::Flat
			|| Row.Condition != ECataclysmStatCondition::Always
			|| Row.Condition2 != ECataclysmStatCondition::Always
			|| Row.Scale != ECataclysmStatScale::Fixed
			|| !Row.RequiredTags.IsEmpty())
		{
			continue;
		}

		// ROUNDED DOWN, EACH ROW BY ITSELF, BEFORE THE ROWS ARE ADDED. Ruled
		// 2026-10-09 (P2). Rows of 7.9 and 3.9 grant 7 and 3, which is 10; their
		// sum of 11.8 rounded down would be 11. A value below 1 grants nothing,
		// and so does a value below nought: no ruled row takes points away.
		if (Row.Value >= 1.0f)
		{
			Points += FMath::FloorToInt32(Row.Value);
		}
	}
	return Points;
}
