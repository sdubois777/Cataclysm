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
