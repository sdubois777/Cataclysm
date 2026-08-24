// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmEnemyScore.h"

namespace
{
	/**
	 * `PLAYER_MAX_SCORES` from `sim/cataclysm_sim/scoring.py`, indexed by tier.
	 *
	 * INDEX 0 IS THE FLOOR OF TIER 1 AND IS ZERO, so `Anchors[Tier - 1]` is a
	 * tier's minimum and `Anchors[Tier]` its maximum, with no special case at
	 * either end. The Python model does the same thing with a dictionary.
	 *
	 * TIERS 2 TO 7 WERE RESET UPSTREAM ON 2026-08-05 because tier 6 was narrower
	 * than tier 5. Tiers 1 and 8 are unchanged and are what
	 * `UCataclysmPowerScore` pins its curve through.
	 */
	const TArray<double> GTierAnchors = {
		0.0, 385.0, 883.0, 1508.0, 2225.0, 3078.0, 4057.0, 5120.0, 6327.0
	};

	/** `TYPE_WEIGHTS`, as fractions of the difficulty tier's width. */
	double TypeWeightFor(ECataclysmDungeonType Type)
	{
		switch (Type)
		{
		case ECataclysmDungeonType::Basic:      return 0.0;
		case ECataclysmDungeonType::Quest:      return 0.05;
		case ECataclysmDungeonType::FallenCity: return 0.1;
		case ECataclysmDungeonType::Cataclysm:  return 0.2;
		}
		return 0.0;
	}

	/** `FLOOR_SCALING_BASES`. A flat number, not a fraction. */
	double FloorScalingBaseFor(ECataclysmDungeonType Type)
	{
		switch (Type)
		{
		case ECataclysmDungeonType::Basic:      return 100.0;
		case ECataclysmDungeonType::Quest:      return 200.0;
		case ECataclysmDungeonType::FallenCity: return 300.0;
		case ECataclysmDungeonType::Cataclysm:  return 400.0;
		}
		return 100.0;
	}

	/** `SUBTYPE_WEIGHTS`. Timed is zero on purpose; see the enum's comment. */
	double SubTypeWeightFor(ECataclysmDungeonSubType SubType)
	{
		switch (SubType)
		{
		case ECataclysmDungeonSubType::None:        return 0.0;
		case ECataclysmDungeonSubType::Timed:       return 0.0;
		case ECataclysmDungeonSubType::Horde:       return 0.05;
		case ECataclysmDungeonSubType::Siege:       return 0.05;
		case ECataclysmDungeonSubType::CowLevel:    return 0.1;
		case ECataclysmDungeonSubType::Elite:       return 0.15;
		case ECataclysmDungeonSubType::Volatile:    return 0.17;
		case ECataclysmDungeonSubType::Sacrificial: return 0.2;
		}
		return 0.0;
	}

	/**
	 * `RARITY_WEIGHTS`, indexed by the Step column of
	 * `game/Data/EnemyRarities.csv`: Common 0 through Cataclysm Boss 5.
	 *
	 * AN ARRAY AND NOT A SWITCH, unlike the three above, because a rarity is
	 * already a number everywhere else in this project and turning it into an
	 * enum here would mean converting it back at every call site.
	 */
	const TArray<double> GRarityWeights = { 0.0, 0.05, 0.1, 0.15, 0.3, 0.5 };

	/**
	 * `_js_round`: floor(x + 0.5), which is what JavaScript's Math.round does.
	 *
	 * NOT `FMath::RoundToInt`, which does not promise this on every platform,
	 * and not C's `round`, which sends a half away from zero -- the same thing
	 * for a positive number, but it says something different and these scores
	 * are not always positive. `depthTension` is large and negative near the
	 * entrance of a deep dungeon, so a floor 1 Common enemy can score below
	 * zero, and the two rules disagree there.
	 */
	int32 JsRound(double Value)
	{
		return static_cast<int32>(FMath::FloorToDouble(Value + 0.5));
	}
}

const TArray<double>& UCataclysmEnemyScore::TierAnchors()
{
	return GTierAnchors;
}

void UCataclysmEnemyScore::TierBounds(int32 Tier, double& OutMin, double& OutMax)
{
	const int32 Held = FMath::Clamp(Tier, LowestDifficultyTier, HighestDifficultyTier);
	OutMin = GTierAnchors[Held - 1];
	OutMax = GTierAnchors[Held];
}

double UCataclysmEnemyScore::TierWidth(int32 Tier)
{
	double Min = 0.0;
	double Max = 0.0;
	TierBounds(Tier, Min, Max);
	return Max - Min;
}

double UCataclysmEnemyScore::TypeWeight(ECataclysmDungeonType Type)
{
	return TypeWeightFor(Type);
}

double UCataclysmEnemyScore::SubTypeWeight(ECataclysmDungeonSubType SubType)
{
	return SubTypeWeightFor(SubType);
}

double UCataclysmEnemyScore::FloorScalingBase(ECataclysmDungeonType Type)
{
	return FloorScalingBaseFor(Type);
}

double UCataclysmEnemyScore::RarityWeight(int32 RarityStep)
{
	if (!GRarityWeights.IsValidIndex(RarityStep))
	{
		return 0.0;
	}
	return GRarityWeights[RarityStep];
}

int32 UCataclysmEnemyScore::ScoreFor(const FCataclysmScoredFloor& Floor,
									 int32 RarityStep)
{
	// CLAMPED, NOT REFUSED. This is reached from a running game where
	// `Cataclysm.DifficultyTier` can be typed at and a dungeon's length is a
	// setting. A creature worth nothing because somebody typed a nine is worse
	// than the nearest sensible answer.
	const int32 Tier = FMath::Clamp(Floor.DifficultyTier, LowestDifficultyTier,
									HighestDifficultyTier);
	const int32 TotalFloors = FMath::Max(1, Floor.TotalFloors);
	const int32 FloorNumber = FMath::Clamp(Floor.FloorNumber, 1, TotalFloors);

	double Min = 0.0;
	double Max = 0.0;
	TierBounds(Tier, Min, Max);
	const double Width = Max - Min;

	// CEIL, NOT ROUND. The Python model uses math.ceil, so a 51-floor dungeon's
	// middle is floor 26 rather than 25, and the depth tension term changes sign
	// one floor later than a reader might expect.
	const int32 MiddleFloor = FMath::CeilToInt(TotalFloors / 2.0);
	const double FloorRatio = static_cast<double>(FloorNumber)
		/ static_cast<double>(TotalFloors);

	const double Baseline = Min + Width * BaselineWeight * FloorRatio;
	const double TypeBonus = Width * TypeWeightFor(Floor.Type);
	const double SubBonus = Width * SubTypeWeightFor(Floor.SubType);

	const double ScalingFactor = FloorScalingBaseFor(Floor.Type) / ProceduralDivisor;
	const double Procedural = (ScalingFactor * FloorRatio)
		+ (static_cast<double>(FloorNumber) * ProceduralPerFloor);

	// NEGATIVE ABOVE THE MIDDLE FLOOR AND POSITIVE BELOW IT, and large: at
	// difficulty tier 8 it moves a score by 9.6 points per floor of distance
	// from the middle. This is what makes the last floor of a dungeon worth
	// meaningfully more than its entrance.
	const double DepthTension = static_cast<double>(FloorNumber - MiddleFloor)
		* (static_cast<double>(Tier) * DepthTensionPerTier);

	const double Score = Baseline + TypeBonus + SubBonus
		+ Width * RarityWeight(RarityStep)
		+ Procedural + DepthTension + static_cast<double>(Floor.ModifierScore);

	return JsRound(Score);
}

TArray<int32> UCataclysmEnemyScore::ScoresFor(const FCataclysmScoredFloor& Floor)
{
	TArray<int32> Out;
	Out.Reserve(RarityStepCount);
	for (int32 Step = 0; Step < RarityStepCount; ++Step)
	{
		Out.Add(ScoreFor(Floor, Step));
	}
	return Out;
}
