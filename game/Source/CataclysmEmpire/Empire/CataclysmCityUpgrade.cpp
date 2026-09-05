// Copyright Stephen Dubois. All Rights Reserved.

#include "Empire/CataclysmCityUpgrade.h"

int32 UCataclysmCityUpgradeRules::SlotsFor(int32 LethalityRung)
{
	// RUNG 2 IS HERETIC, the same numbering `UCataclysmDayClock::DeathDayCostFor`
	// uses. Anything else gets the ordinary three, so an unrecognised rung is
	// the forgiving answer rather than zero slots.
	return LethalityRung == 2 ? SlotsOnHeretic : SlotsNormally;
}

bool UCataclysmCityUpgradeRules::IsFreeAndInstant()
{
	return GoldCost == 0 && BuildDays == 0;
}

bool UCataclysmCityUpgradeRules::IsBuilt(ECataclysmCityUpgradeEffect Effect)
{
	switch (Effect)
	{
	// THE TEN ARCHITECT UPGRADES, all of which act on a city's own defence,
	// population, or the dungeons standing on it. Everything they need already
	// exists: the map owns the cities and the run owns the dungeons.
	case ECataclysmCityUpgradeEffect::MaxDefence:
	case ECataclysmCityUpgradeEffect::MaxPopulation:
	case ECataclysmCityUpgradeEffect::RemoveDungeons:
	case ECataclysmCityUpgradeEffect::RestoreDefence:
	case ECataclysmCityUpgradeEffect::RestorePopulation:
	case ECataclysmCityUpgradeEffect::ResistDefenceLoss:
	case ECataclysmCityUpgradeEffect::ResistPopulationLoss:
	case ECataclysmCityUpgradeEffect::HealDefenceEvery:
	case ECataclysmCityUpgradeEffect::RecoverPopulationEvery:
	case ECataclysmCityUpgradeEffect::RestoreDefenceOnClear:

	// AND THE THREE EXPLORER UPGRADES THAT SHAPE THE DUNGEONS A CITY RECEIVES.
	// The surge scheduler applies all three: two move the floor count as a
	// dungeon is rolled, and the cap takes a full city out of the target list so
	// the wave lands elsewhere.
	case ECataclysmCityUpgradeEffect::DungeonCap:
	case ECataclysmCityUpgradeEffect::DungeonFloorsMore:
	case ECataclysmCityUpgradeEffect::DungeonFloorsFewer:
		return true;

	// EVERYTHING ELSE IS WAITING ON A SYSTEM THAT DOES NOT EXIST, or on a
	// decision. Listed rather than defaulted, so adding a value to the enum
	// without deciding this is a compiler warning rather than a silent
	// "not built".
	case ECataclysmCityUpgradeEffect::DungeonResolveDaysFewer:
		// NOT BUILT BECAUSE IT CANNOT BE TOLD APART FROM `DungeonFloorsFewer`.
		// "Dungeons here take 4 less days to beat" is time to walk the dungeon,
		// and one floor costs exactly one day -- `docs/Cataclysm_GDD_v2.md`
		// states it and `CLAUDE.md` fixes it -- so four fewer days to beat IS
		// four fewer floors. `Explorer_Dungeons_here_have_5_fewer_floors_to_a`
		// already does that, and does five. Building this would add a second
		// upgrade that does the same thing and less of it. Issue #1266.
		return false;

	case ECataclysmCityUpgradeEffect::SubTypeChanceHorde:
	case ECataclysmCityUpgradeEffect::SubTypeChanceElite:
	case ECataclysmCityUpgradeEffect::SubTypeChanceSacrificial:
	case ECataclysmCityUpgradeEffect::SubTypeChanceVolatile:
		// Issue #41. Nothing rolls a dungeon sub-type.
		return false;

	case ECataclysmCityUpgradeEffect::DungeonExperience:
	case ECataclysmCityUpgradeEffect::DungeonMagicFind:
	case ECataclysmCityUpgradeEffect::DungeonGold:
	case ECataclysmCityUpgradeEffect::DungeonLoot:
	case ECataclysmCityUpgradeEffect::DungeonCraftingMaterials:
		// Issues #41 and #1264. A dungeon in this layer is a timer and a bite;
		// it grants no reward of any kind, and there is no gold.
		return false;

	case ECataclysmCityUpgradeEffect::CleanseEveryCity:
		// Issue #1265. Gated on upgrade tier 3, which nothing reaches.
		return false;

	case ECataclysmCityUpgradeEffect::None:
	default:
		return false;
	}
}

TArray<ECataclysmCityUpgradeEffect> UCataclysmCityUpgradeRules::AllEffects()
{
	TArray<ECataclysmCityUpgradeEffect> Effects;

	// ONE PER ROW OF THE SHEET, and `None` is not one of them.
	//
	// TWO TESTS HOLD THE COUNT AT TWENTY-FOUR FROM OPPOSITE SIDES.
	// `Cataclysm.CityUpgrade.TenOfTheTwentyFourEffectsAreBuilt` checks this list
	// has 24 entries, and
	// `Cataclysm.CityUpgrade.EveryRowOfTheTableMapsToAnEffect` checks the
	// DataTable has 24 rows and that each maps to a distinct effect. So a row
	// added without a value, or a value added without a row, fails one of them.
	//
	// THE BOUND IS WRITTEN OUT RATHER THAN READ FROM THE REFLECTED ENUM. A
	// twenty-fifth value added here without a row would go unchecked by the
	// first test, which compares against this same literal -- but it cannot
	// reach a player, because an upgrade only enters the game through a table
	// row and a twenty-fifth row fails the second test loudly.
	for (int32 Value = 1; Value <= 24; ++Value)
	{
		Effects.Add(static_cast<ECataclysmCityUpgradeEffect>(Value));
	}

	return Effects;
}

int32 UCataclysmCityUpgradeRules::BuiltEffectCount()
{
	int32 Built = 0;

	for (const ECataclysmCityUpgradeEffect Effect : AllEffects())
	{
		if (IsBuilt(Effect))
		{
			++Built;
		}
	}

	return Built;
}

FString UCataclysmCityUpgradeRules::EffectName(
	ECataclysmCityUpgradeEffect Effect)
{
	switch (Effect)
	{
	case ECataclysmCityUpgradeEffect::MaxDefence:				return TEXT("MaxDefence");
	case ECataclysmCityUpgradeEffect::MaxPopulation:				return TEXT("MaxPopulation");
	case ECataclysmCityUpgradeEffect::RemoveDungeons:			return TEXT("RemoveDungeons");
	case ECataclysmCityUpgradeEffect::RestoreDefence:			return TEXT("RestoreDefence");
	case ECataclysmCityUpgradeEffect::RestorePopulation:		return TEXT("RestorePopulation");
	case ECataclysmCityUpgradeEffect::ResistDefenceLoss:		return TEXT("ResistDefenceLoss");
	case ECataclysmCityUpgradeEffect::ResistPopulationLoss:		return TEXT("ResistPopulationLoss");
	case ECataclysmCityUpgradeEffect::HealDefenceEvery:			return TEXT("HealDefenceEvery");
	case ECataclysmCityUpgradeEffect::RecoverPopulationEvery:	return TEXT("RecoverPopulationEvery");
	case ECataclysmCityUpgradeEffect::RestoreDefenceOnClear:	return TEXT("RestoreDefenceOnClear");
	case ECataclysmCityUpgradeEffect::DungeonCap:				return TEXT("DungeonCap");
	case ECataclysmCityUpgradeEffect::DungeonResolveDaysFewer:	return TEXT("DungeonResolveDaysFewer");
	case ECataclysmCityUpgradeEffect::DungeonFloorsMore:		return TEXT("DungeonFloorsMore");
	case ECataclysmCityUpgradeEffect::DungeonFloorsFewer:		return TEXT("DungeonFloorsFewer");
	case ECataclysmCityUpgradeEffect::SubTypeChanceHorde:		return TEXT("SubTypeChanceHorde");
	case ECataclysmCityUpgradeEffect::SubTypeChanceElite:		return TEXT("SubTypeChanceElite");
	case ECataclysmCityUpgradeEffect::SubTypeChanceSacrificial:	return TEXT("SubTypeChanceSacrificial");
	case ECataclysmCityUpgradeEffect::SubTypeChanceVolatile:	return TEXT("SubTypeChanceVolatile");
	case ECataclysmCityUpgradeEffect::DungeonExperience:		return TEXT("DungeonExperience");
	case ECataclysmCityUpgradeEffect::DungeonMagicFind:			return TEXT("DungeonMagicFind");
	case ECataclysmCityUpgradeEffect::DungeonGold:				return TEXT("DungeonGold");
	case ECataclysmCityUpgradeEffect::DungeonLoot:				return TEXT("DungeonLoot");
	case ECataclysmCityUpgradeEffect::DungeonCraftingMaterials:	return TEXT("DungeonCraftingMaterials");
	case ECataclysmCityUpgradeEffect::CleanseEveryCity:			return TEXT("CleanseEveryCity");
	case ECataclysmCityUpgradeEffect::None:
	default:													return TEXT("None");
	}
}

FString UCataclysmCityUpgradeRules::ResultText(
	ECataclysmCityUpgradeResult Result)
{
	switch (Result)
	{
	case ECataclysmCityUpgradeResult::Bought:
		return TEXT("Bought.");
	case ECataclysmCityUpgradeResult::RunHasNotBegun:
		return TEXT("There is no run to buy anything in.");
	case ECataclysmCityUpgradeResult::NoSuchCity:
		return TEXT("There is no such city.");
	case ECataclysmCityUpgradeResult::CityHasFallen:
		return TEXT("This city has fallen. Retake it first.");
	case ECataclysmCityUpgradeResult::NoSlotsLeft:
		return TEXT("Every upgrade slot on this city is filled.");
	case ECataclysmCityUpgradeResult::AlreadyBought:
		return TEXT("This city already has that upgrade.");
	case ECataclysmCityUpgradeResult::EffectNotBuiltYet:
		return TEXT("That upgrade does nothing yet, so a slot cannot be spent "
					"on it.");
	case ECataclysmCityUpgradeResult::NotAnUpgrade:
		return TEXT("That is not an upgrade.");
	case ECataclysmCityUpgradeResult::CannotPayYet:
		return TEXT("A city upgrade now has a price or a build time, and "
					"nothing can pay either yet.");
	default:
		return TEXT("Refused.");
	}
}
