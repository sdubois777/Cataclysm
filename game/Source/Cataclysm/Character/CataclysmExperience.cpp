// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmExperience.h"

namespace
{
	/**
	 * The cost of every level, worked out once and kept.
	 *
	 * A TABLE RATHER THAN A `pow` PER CALL, for two reasons that are not speed.
	 * `Grant` walks levels in a loop and a save load walks the whole curve, so
	 * the same 99 values are asked for over and over. And a table computed once
	 * cannot disagree with itself: two calls for level 50 return the identical
	 * integer whatever the compiler does with floating point between them.
	 *
	 * INDEXED BY LEVEL, so index 0 and 1 are unused zeroes and `Costs[L]` is the
	 * cost of reaching level L. Off-by-one errors in a curve that decides a save
	 * record's contents are worth spending two int64s to avoid.
	 */
	const TArray<int64>& LevelCosts()
	{
		static const TArray<int64> Costs = []()
		{
			TArray<int64> Built;
			Built.SetNumZeroed(UCataclysmExperience::MaxLevel + 1);
			for (int32 Level = 2; Level <= UCataclysmExperience::MaxLevel; ++Level)
			{
				// FLOOR OF x + 0.5, WHICH IS NOT FMath::RoundToInt64's RULE ON
				// EVERY PLATFORM and is not C's `round` either. It is what
				// `_js_round` in sim/cataclysm_sim/scoring.py does, so a half
				// goes up on both sides and the two curves produce the same
				// integers. tools/tests/test_experience_curve_port.py compares
				// them level by level.
				const double Raw = static_cast<double>(
					UCataclysmExperience::SecondLevelCost)
					* FMath::Pow(1.0 + UCataclysmExperience::GrowthPerLevel,
								 static_cast<double>(Level - 2));
				Built[Level] = static_cast<int64>(FMath::FloorToDouble(Raw + 0.5));
			}
			return Built;
		}();
		return Costs;
	}
}

int64 UCataclysmExperience::CostOfLevel(int32 Level)
{
	if (Level < 2 || Level > MaxLevel)
	{
		return 0;
	}
	return LevelCosts()[Level];
}

int64 UCataclysmExperience::TotalToReach(int32 Level)
{
	// SUMMED FROM THE ROUNDED COSTS, not computed from a closed form. A player
	// pays each level's whole-number cost, so the climb is the sum of 99
	// roundings; the closed-form geometric sum rounded once gives a number 5
	// lower over the whole climb. The Python model has the same two functions
	// and a test asserting they differ, for the same reason.
	const int32 Top = FMath::Clamp(Level, FirstLevel, MaxLevel);
	int64 Total = 0;
	for (int32 Step = 2; Step <= Top; ++Step)
	{
		Total += LevelCosts()[Step];
	}
	return Total;
}

int32 UCataclysmExperience::Grant(int64 Amount, int32& InOutLevel,
								  int64& InOutExperience)
{
	// CLAMPED ON THE WAY IN BECAUSE THIS IS REACHED FROM A SAVE RECORD, and a
	// save record holds whatever was last written to it, including whatever a
	// future migration leaves behind. A level of 0 would make the loop below
	// charge the cost of level 1, which is zero, and gain a level per iteration
	// for ever.
	InOutLevel = FMath::Clamp(InOutLevel, FirstLevel, MaxLevel);
	InOutExperience = FMath::Max<int64>(0, InOutExperience);

	if (Amount <= 0)
	{
		return 0;
	}

	if (IsMaxLevel(InOutLevel))
	{
		// NOTHING TO BANK IT AGAINST, so it is discarded rather than stored.
		InOutExperience = 0;
		return 0;
	}

	InOutExperience += Amount;

	int32 Gained = 0;
	while (!IsMaxLevel(InOutLevel))
	{
		const int64 Next = CostOfLevel(InOutLevel + 1);
		if (InOutExperience < Next)
		{
			break;
		}
		InOutExperience -= Next;
		++InOutLevel;
		++Gained;
	}

	if (IsMaxLevel(InOutLevel))
	{
		// THE OVERSHOOT IS DROPPED AT THE TOP, the same as an award arriving
		// when already at the maximum. Keeping it would leave a number that
		// only ever grows and that nothing can ever spend.
		InOutExperience = 0;
	}

	return Gained;
}

int32 UCataclysmExperience::LevelForTotal(int64 TotalExperience)
{
	if (TotalExperience <= 0)
	{
		return FirstLevel;
	}

	int32 Level = FirstLevel;
	int64 Remaining = TotalExperience;
	while (Level < MaxLevel)
	{
		const int64 Next = CostOfLevel(Level + 1);
		if (Remaining < Next)
		{
			break;
		}
		Remaining -= Next;
		++Level;
	}
	return Level;
}
