// Copyright Stephen Dubois. All Rights Reserved.

#include "Empire/CataclysmCityUpgrade.h"

int32 UCataclysmCityUpgradeRules::SlotsFor(int32 LethalityRung)
{
	// RUNG 2 IS HERETIC, the same numbering `UCataclysmDayClock::DeathDayCostFor`
	// uses. Anything else gets the ordinary three, so an unrecognised rung is
	// the forgiving answer rather than zero slots.
	return LethalityRung == 2 ? SlotsOnHeretic : SlotsNormally;
}

int32 UCataclysmCityUpgradeRules::BuildDaysFor(int32 UpgradeTier)
{
	// THE ENDS OF THE LADDER, so a tier nothing can reach still reads a day count rather than none.
	if (UpgradeTier >= 3)
	{
		return BuildDaysTier3;
	}
	return UpgradeTier == 2 ? BuildDaysTier2 : BuildDaysTier1;
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

	// AND THE FOUR EXPLORER UPGRADES THAT SHAPE THE DUNGEONS A CITY RECEIVES.
	// The surge scheduler applies all four as a dungeon is rolled: two move the
	// floor count, one lowers the days walking it costs WITHOUT moving the floor
	// count, and the cap takes a full city out of the target list so the wave
	// lands elsewhere.
	case ECataclysmCityUpgradeEffect::DungeonCap:
	case ECataclysmCityUpgradeEffect::DungeonWalkDaysFewer:
	case ECataclysmCityUpgradeEffect::DungeonFloorsMore:
	case ECataclysmCityUpgradeEffect::DungeonFloorsFewer:

	// AND THE FIVE THAT CHANGE WHAT A KILL THERE PAYS, issue #2781: the four
	// Treasurer upgrades and the Artisan one. A kill in a dungeon standing on the
	// city reads them through `DungeonRewardsOf`.
	case ECataclysmCityUpgradeEffect::DungeonExperience:
	case ECataclysmCityUpgradeEffect::DungeonMagicFind:
	case ECataclysmCityUpgradeEffect::DungeonGold:
	case ECataclysmCityUpgradeEffect::DungeonLoot:
	case ECataclysmCityUpgradeEffect::DungeonCraftingMaterials:
		return true;

	// EVERYTHING ELSE IS WAITING ON A SYSTEM THAT DOES NOT EXIST. Listed rather
	// than defaulted, so adding a value to the enum without deciding this is a
	// compiler warning rather than a silent "not built".

	case ECataclysmCityUpgradeEffect::SubTypeChanceHorde:
	case ECataclysmCityUpgradeEffect::SubTypeChanceElite:
	case ECataclysmCityUpgradeEffect::SubTypeChanceSacrificial:
	case ECataclysmCityUpgradeEffect::SubTypeChanceVolatile:
		// Issue #41. Nothing rolls a dungeon sub-type.
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
	// `Cataclysm.CityUpgrade.OnlyTheEffectsThatAreBuiltReportAsBuilt` checks
	// this list has 24 entries, and
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

FCataclysmCityDungeonRewards UCataclysmCityUpgradeRules::DungeonRewardsOf(const TArray<FCataclysmCityUpgrade>& Upgrades)
{
	// EACH UPGRADE AT ITS OWN FIGURE, the tier 1 one (ruling 3). EVERY "MORE" MULTIPLIES AND THE MAGIC FIND ADDS (ruling
	// 2): a percentage of 0.25 is 25% more, a multiplier of 2 is twice, and 0.10 of magic find is ten points.
	FCataclysmCityDungeonRewards Rewards;
	for (const FCataclysmCityUpgrade& Upgrade : Upgrades)
	{
		switch (Upgrade.Effect)
		{
		case ECataclysmCityUpgradeEffect::DungeonExperience:
			Rewards.ExperienceMore *= FMath::Max(0.0f, Upgrade.Value);
			break;
		case ECataclysmCityUpgradeEffect::DungeonMagicFind:
			Rewards.MagicFindIncreased += 100.0f * Upgrade.Value;
			break;
		case ECataclysmCityUpgradeEffect::DungeonGold:
			Rewards.GoldMore *= 1.0f + FMath::Max(0.0f, Upgrade.Value);
			break;
		case ECataclysmCityUpgradeEffect::DungeonLoot:
			Rewards.LootMore *= 1.0f + FMath::Max(0.0f, Upgrade.Value);
			break;
		case ECataclysmCityUpgradeEffect::DungeonCraftingMaterials:
			Rewards.MaterialsMore *= 1.0f + FMath::Max(0.0f, Upgrade.Value);
			break;
		default:
			break;
		}
	}
	return Rewards;
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
	case ECataclysmCityUpgradeEffect::DungeonWalkDaysFewer:	return TEXT("DungeonWalkDaysFewer");
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
	case ECataclysmCityUpgradeResult::CityIsBesieged:
		// WORDS OF ITS OWN SINCE ISSUE #2742's SECOND SLICE: until then it fell
		// to "Refused." below. Besieged is a Siege dungeon standing on the city
		// (`UCataclysmEmpireRun::IsBesieged`), and the owner's ruling of
		// 2026-09-05 keeps the effects of the upgrades it holds working.
		return TEXT("A Siege dungeon stands on this city, so it cannot be "
					"upgraded until that dungeon is gone. The upgrades it "
					"holds still work.");
	case ECataclysmCityUpgradeResult::BuildUnderWay:
		// ISSUE #2825: a city builds one upgrade at a time. The days left are on
		// the city screen's line for the build (`UCataclysmCityScreenLayout`).
		return TEXT("An upgrade is already being built on this city, and a "
					"city builds one at a time.");
	case ECataclysmCityUpgradeResult::NotEnoughGold:
		// THE AMOUNTS ARE NOT HERE: this module is not told them again. The city
		// screen says the price and how much more is needed
		// (`UCataclysmCityUpgradePurchase::RefusalText`).
		return TEXT("Not enough gold.");
	case ECataclysmCityUpgradeResult::RushNotHeld:
		// ISSUE #2832: said by `UCataclysmCityUpgradePurchase::Buy`, which reads the tree; the run never answers it.
		return TEXT("Rapid Funding, in the empire upgrade tree, is what lets an "
					"upgrade be rushed.");
	default:
		return TEXT("Refused.");
	}
}
