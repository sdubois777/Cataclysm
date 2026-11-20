// Copyright Stephen Dubois. All Rights Reserved.

// THE TWO STRUCTS ARE DECLARED IN `CataclysmEmpireMap.h`, after the city tier they read and before the map that
// holds them. A header of their own would have had to include that one and be included by it.
#include "Empire/CataclysmEmpireMap.h"

float FCataclysmEmpireTreeBonuses::MaxDefenceFactorFor(ECataclysmCityTier Tier, int32 Ring,
													   int32 CitiesWithADefensiveUpgrade) const
{
	float Increase = MaxDefenceAll + MaxDefencePerDefendedCity * FMath::Max(0, CitiesWithADefensiveUpgrade);
	switch (Tier)
	{
	case ECataclysmCityTier::Outpost:
		Increase += MaxDefenceOutposts;
		break;
	case ECataclysmCityTier::Bulwark:
		Increase += MaxDefenceBulwarks;
		break;
	case ECataclysmCityTier::Sanctuary:
		Increase += MaxDefenceSanctuaries;
		break;
	default:
		break;
	}
	if (Ring <= NearPillarRings)
	{
		Increase += MaxDefenceNearPillar;
	}

	// NEVER BELOW NOTHING. No node lowers maximum defence, so this holds only against a table that one day does.
	return FMath::Max(0.0f, 1.0f + Increase);
}

float FCataclysmEmpireTreeBonuses::MaxPopulationFactorFor(ECataclysmCityTier Tier) const
{
	float Increase = BasePopulationAll;
	switch (Tier)
	{
	case ECataclysmCityTier::Outpost:
		Increase += BasePopulationOutposts;
		break;
	case ECataclysmCityTier::Bulwark:
		Increase += BasePopulationBulwarks;
		break;
	default:
		break;
	}

	// IMPERIAL DECREE TAKES 10% OFF, so a tree holding it and nothing else is below one. Never below nothing.
	return FMath::Max(0.0f, 1.0f + Increase) * FMath::Max(0.0f, TotalPopulationFactor);
}

FCataclysmTreeDamageFactors FCataclysmEmpireTreeBonuses::DamageFactorsFor(int32 Ring, int32 ActiveCataclysmTypes,
																		 float LivingShare, int32 UpgradesHeld,
																		 bool bFromTheCommonestType) const
{
	float Both = DamageTaken;
	if (Ring == AdjacentToPillarRing)
	{
		Both *= DamageTakenAdjacentToPillar;
	}
	Both *= FMath::Pow(DamageTakenPerActiveType, static_cast<float>(FMath::Max(0, ActiveCataclysmTypes)));

	// IRON WILL: "-1% damage taken by cities per point, reduced proportionally as Empire population falls below
	// maximum". Each point's reduction is scaled by the living share and then compounded, as every other point is.
	const float Share = FMath::Clamp(LivingShare, 0.0f, 1.0f);
	Both *= FMath::Pow(FMath::Max(0.0f, 1.0f - LivingShareReductionPerPoint * Share),
					   static_cast<float>(FMath::Max(0, LivingSharePoints)));

	if (UpgradesHeld >= UpgradesForImperialCommand)
	{
		Both *= DamageTakenWithUpgrades;
	}
	if (bFromTheCommonestType)
	{
		Both *= DamageTakenFromCommonestType;
	}

	FCataclysmTreeDamageFactors Out;
	Out.Defence = FMath::Max(0.0f, Both);
	float Population = Both * PopulationDamageTaken;
	if (Ring <= NearPillarRings)
	{
		Population *= PopulationDamageTakenNearPillar;
	}
	Out.Population = FMath::Max(0.0f, Population);
	return Out;
}

float FCataclysmEmpireTreeBonuses::ResolveDaysFor(ECataclysmCityTier Tier, float DefenceFraction) const
{
	float Days = ResolveDaysAll;
	if (Tier == ECataclysmCityTier::Outpost)
	{
		Days += ResolveDaysOutposts;
	}
	if (DefenceFraction < MartialLawDefenceFraction)
	{
		Days += ResolveDaysBelowTenPercent;
	}
	return Days;
}

namespace CataclysmEmpireTreeBonuses
{
	/** What a reduction of `PerStack` a point, for `Stacks` of something, leaves of a hit, held at `Cap`. */
	float KeptAfter(float PerStack, int32 Points, int32 Stacks, float Cap)
	{
		if (PerStack <= 0.0f || Points <= 0 || Stacks <= 0)
		{
			return 1.0f;
		}
		const float Kept = FMath::Pow(FMath::Max(0.0f, 1.0f - PerStack),
									  static_cast<float>(Points) * static_cast<float>(Stacks));
		return Cap > 0.0f ? FMath::Max(1.0f - Cap, Kept) : Kept;
	}
}

float FCataclysmEmpireTreeBonuses::EventDamageFactorFor(int32 ClearsSinceLastSurge, int32 CitiesReclaimed) const
{
	// EACH NODE HELD AT ITS OWN CAP, and the two then multiplied, as every other reduction multiplies (S5): Global
	// Vigilance's "15% cap" is its own text, and Reclaimer's Resolve's 15% is the coordinating session's Q2.
	return CataclysmEmpireTreeBonuses::KeptAfter(ReductionPerClear, ClearPoints, ClearsSinceLastSurge, ClearReductionCap)
		* CataclysmEmpireTreeBonuses::KeptAfter(ReductionPerReclaim, ReclaimPoints, CitiesReclaimed, ReclaimReductionCap);
}

float FCataclysmEmpireTreeBonuses::GoldDamageFactorFor(double GoldHeldAtTierOne) const
{
	// WHOLE STEPS OF 100,000 TIER 1 GOLD, compounded per point and held at the node's 25% (B7), as `KeptAfter` holds
	// Reclaimer's Resolve.
	const int32 Steps = static_cast<int32>(FMath::Min(
		FMath::Max(0.0, GoldHeldAtTierOne) / static_cast<double>(ImperialReserveGoldAStep), 1000000.0));
	return CataclysmEmpireTreeBonuses::KeptAfter(ReductionPerGoldStep, GoldStepPoints, Steps, GoldReductionCap);
}

bool FCataclysmEmpireTreeBonuses::ReachesTheWalk() const
{
	return WalkTimeFactor != 1.0f || HalveWalkAboveFloors > 0 || WalkDaysFewer > 0.0f || WalkDaysFewerWhenAlone > 0.0f
		|| WalkDaysFewerPerActiveType > 0.0f || WalkDaysFewerOnFallenCities > 0.0f || WalkDaysFewerPerFloorCleared > 0.0f
		|| OneDayWalkWithinDaysOfFalling > 0;
}

int32 FCataclysmEmpireTreeBonuses::FloorsAddedTo(bool bTheBoss, int32 ActiveCataclysmTypes) const
{
	// INFINITE DEPTHS: "to every dungeon except the Cataclysm boss dungeon" (W3).
	return FloorsAdded + (bTheBoss ? 0 : FloorsAddedPerActiveType * FMath::Max(0, ActiveCataclysmTypes));
}
