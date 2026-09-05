// Copyright Stephen Dubois. All Rights Reserved.

#include "Data/CataclysmCityUpgradeMapping.h"

#include "Cataclysm.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"

const TCHAR* UCataclysmCityUpgradeMapping::TableAssetPath =
	TEXT("/Game/Data/DT_CityUpgrades.DT_CityUpgrades");

namespace
{
	/**
	 * Row name to effect, for all 24 rows of the City Upgrades sheet.
	 *
	 * WRITTEN OUT RATHER THAN INFERRED. Reading the effect out of the sentence
	 * would be a second prose heuristic on top of the one that reads the tier 1
	 * magnitude, and this one decides what an upgrade DOES rather than how
	 * strong it is, so a wrong answer is worse.
	 */
	const TMap<FName, ECataclysmCityUpgradeEffect>& RowEffects()
	{
		static const TMap<FName, ECataclysmCityUpgradeEffect> Map = {
			// Architect. All ten are built.
			{ TEXT("Architect_Increase_max_defense_by_20"),
			  ECataclysmCityUpgradeEffect::MaxDefence },
			{ TEXT("Architect_Increase_max_population_by_20"),
			  ECataclysmCityUpgradeEffect::MaxPopulation },
			{ TEXT("Architect_Remove_25_of_dungeons_on_this_city"),
			  ECataclysmCityUpgradeEffect::RemoveDungeons },
			{ TEXT("Architect_Restore_city_s_defenses_by_50"),
			  ECataclysmCityUpgradeEffect::RestoreDefence },
			{ TEXT("Architect_Restore_city_s_population_by_50"),
			  ECataclysmCityUpgradeEffect::RestorePopulation },
			{ TEXT("Architect_This_city_resists_25_of_damage"),
			  ECataclysmCityUpgradeEffect::ResistDefenceLoss },
			{ TEXT("Architect_This_city_resists_25_of_population_loss"),
			  ECataclysmCityUpgradeEffect::ResistPopulationLoss },
			{ TEXT("Architect_Every_20_days_this_city_s_defenses_heal"),
			  ECataclysmCityUpgradeEffect::HealDefenceEvery },
			{ TEXT("Architect_Every_20_days_this_city_s_population_rec"),
			  ECataclysmCityUpgradeEffect::RecoverPopulationEvery },
			{ TEXT("Architect_When_you_clear_a_dungeon_the_city_s_Def"),
			  ECataclysmCityUpgradeEffect::RestoreDefenceOnClear },

			// Explorer, the four that shape a dungeon. Issue #1266.
			{ TEXT("Explorer_There_can_be_no_more_than_15_dungeons_on"),
			  ECataclysmCityUpgradeEffect::DungeonCap },
			{ TEXT("Explorer_Dungeons_here_take_4_less_days_to_beat"),
			  ECataclysmCityUpgradeEffect::DungeonResolveDaysFewer },
			{ TEXT("Explorer_Dungeons_here_have_5_more_floors"),
			  ECataclysmCityUpgradeEffect::DungeonFloorsMore },
			{ TEXT("Explorer_Dungeons_here_have_5_fewer_floors_to_a"),
			  ECataclysmCityUpgradeEffect::DungeonFloorsFewer },

			// Explorer, the four sub-type rows. Issue #41. THREE OF THESE FOUR
			// NAMES END IN A DEDUPLICATION SUFFIX, because all four sentences
			// begin with the same words; the fourth is told apart by a spelling
			// mistake in the source sheet. The test checks each row's sentence
			// names the sub-type it was mapped to.
			{ TEXT("Explorer_Increases_the_chance_of_dungeons_on_this"),
			  ECataclysmCityUpgradeEffect::SubTypeChanceHorde },
			{ TEXT("Explorer_Increases_the_chance_of_dungeons_on_this_1"),
			  ECataclysmCityUpgradeEffect::SubTypeChanceElite },
			{ TEXT("Explorer_Increases_the_chance_of_dungeons_on_this_2"),
			  ECataclysmCityUpgradeEffect::SubTypeChanceSacrificial },
			{ TEXT("Explorer_Increases_the_chance_of_dungeons_on_thie"),
			  ECataclysmCityUpgradeEffect::SubTypeChanceVolatile },

			// Treasurer and Artisan. Issues #41 and #1264.
			{ TEXT("Treasurer_Dungeons_here_provide_2x_more_experience"),
			  ECataclysmCityUpgradeEffect::DungeonExperience },
			{ TEXT("Treasurer_Dungeons_here_have_10_increased_magic_f"),
			  ECataclysmCityUpgradeEffect::DungeonMagicFind },
			{ TEXT("Treasurer_Dungeons_here_drop_25_more_gold"),
			  ECataclysmCityUpgradeEffect::DungeonGold },
			{ TEXT("Treasurer_Dungeons_here_drop_25_more_loot"),
			  ECataclysmCityUpgradeEffect::DungeonLoot },
			{ TEXT("Artisan_Dungeons_here_drop_25_more_crafting_mat"),
			  ECataclysmCityUpgradeEffect::DungeonCraftingMaterials },

			// The unbranched last resort. Issue #1265.
			{ TEXT("Unbranched_Cleanse_every_player_city_of_half_of_the"),
			  ECataclysmCityUpgradeEffect::CleanseEveryCity },
		};

		return Map;
	}
}

ECataclysmCityUpgradeEffect UCataclysmCityUpgradeMapping::EffectFor(
	FName RowName)
{
	const ECataclysmCityUpgradeEffect* Found = RowEffects().Find(RowName);

	return Found ? *Found : ECataclysmCityUpgradeEffect::None;
}

FCataclysmCityUpgrade UCataclysmCityUpgradeMapping::Make(
	FName RowName, const FCataclysmCityUpgradeRow& Row)
{
	FCataclysmCityUpgrade Upgrade;

	Upgrade.RowName = RowName;
	Upgrade.Effect = EffectFor(RowName);
	Upgrade.bOneTimeUse = Row.IsOneTimeUse;
	Upgrade.Tier = 1;

	// TIER 1 IS WHAT A NEWLY BOUGHT UPGRADE IS WORTH, and nothing raises it.
	// Issue #1265.
	Upgrade.Value = Row.Tier1Value;
	Upgrade.IntervalDays = Row.Tier1IntervalDays;

	return Upgrade;
}

const UDataTable* UCataclysmCityUpgradeMapping::LoadGeneratedTable()
{
	const UDataTable* Table = LoadObject<UDataTable>(nullptr, TableAssetPath);
	if (!Table)
	{
		// Naming both scripts, because the two failures look the same from
		// here: the workbook never produced the CSV, or the CSV was never
		// imported as an asset.
		UE_LOG(LogCataclysm, Error,
			TEXT("Could not load %s. It is produced by "
				 "tools/generate_datatable_assets.py from game/Data/"
				 "CityUpgrades.csv, which tools/generate_datatables.py produces "
				 "from the City Upgrades sheet of "
				 "docs/All_Things_Cataclysm.xlsx."), TableAssetPath);
		return nullptr;
	}

	return Table;
}

FCataclysmCityUpgrade UCataclysmCityUpgradeMapping::MakeFromTable(FName RowName)
{
	const UDataTable* Table = LoadGeneratedTable();
	if (Table == nullptr)
	{
		return FCataclysmCityUpgrade();
	}

	const FCataclysmCityUpgradeRow* Row =
		Table->FindRow<FCataclysmCityUpgradeRow>(RowName, TEXT("MakeFromTable"),
												 /* bWarnIfMissing */ false);

	if (Row == nullptr)
	{
		return FCataclysmCityUpgrade();
	}

	return Make(RowName, *Row);
}

TArray<FName> UCataclysmCityUpgradeMapping::AllRowNames()
{
	const UDataTable* Table = LoadGeneratedTable();

	return Table ? Table->GetRowNames() : TArray<FName>();
}
