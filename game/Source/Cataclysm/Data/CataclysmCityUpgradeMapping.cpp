// Copyright Stephen Dubois. All Rights Reserved.

#include "Data/CataclysmCityUpgradeMapping.h"

#include "Cataclysm.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Items/CataclysmDropRoll.h"

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
			  ECataclysmCityUpgradeEffect::DungeonWalkDaysFewer },
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

			// Treasurer and Artisan. Not built yet: issue #2781.
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
	FName RowName, const FCataclysmCityUpgradeRow& Row, int32 Tier)
{
	FCataclysmCityUpgrade Upgrade;
	const int32 At = FMath::Clamp(Tier, 1, UCataclysmCityUpgradeRules::HighestTier);

	// A TIER THE ROW DOES NOT HAVE IS NO UPGRADE: the cleanse above tier 1 (issue #1265).
	if (!HasTier(Row, At))
	{
		return Upgrade;
	}

	Upgrade.RowName = RowName;
	Upgrade.Effect = EffectFor(RowName);
	Upgrade.bOneTimeUse = Row.IsOneTimeUse;
	Upgrade.Tier = At;

	// TIER 1 IS WHAT A NEWLY BOUGHT UPGRADE IS WORTH; TIERS 2 AND 3 ARE WHAT A RAISE MAKES IT (issue #1265).
	Upgrade.Value = At == 3 ? Row.Tier3Value : At == 2 ? Row.Tier2Value : Row.Tier1Value;
	Upgrade.IntervalDays =
		At == 3 ? Row.Tier3IntervalDays : At == 2 ? Row.Tier2IntervalDays : Row.Tier1IntervalDays;

	return Upgrade;
}

bool UCataclysmCityUpgradeMapping::HasTier(const FCataclysmCityUpgradeRow& Row, int32 Tier)
{
	switch (Tier)
	{
	case 1:	return true;
	case 2:	return !Row.Tier2Raw.TrimStartAndEnd().IsEmpty();
	case 3:	return !Row.Tier3Raw.TrimStartAndEnd().IsEmpty();
	default: return false;
	}
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

FCataclysmCityUpgrade UCataclysmCityUpgradeMapping::MakeAtTier(FName RowName, int32 Tier)
{
	const UDataTable* Table = LoadGeneratedTable();
	const FCataclysmCityUpgradeRow* Row = Table != nullptr
		? Table->FindRow<FCataclysmCityUpgradeRow>(RowName, TEXT("MakeAtTier"), /* bWarnIfMissing */ false)
		: nullptr;
	return Row != nullptr ? Make(RowName, *Row, Tier) : FCataclysmCityUpgrade();
}

TArray<FName> UCataclysmCityUpgradeMapping::AllRowNames()
{
	const UDataTable* Table = LoadGeneratedTable();

	return Table ? Table->GetRowNames() : TArray<FName>();
}

namespace
{
	/** The table row, or null. Shared by the two readers below. */
	const FCataclysmCityUpgradeRow* FindCityUpgradeRow(FName RowName)
	{
		const UDataTable* Table =
			UCataclysmCityUpgradeMapping::LoadGeneratedTable();

		return Table ? Table->FindRow<FCataclysmCityUpgradeRow>(
						   RowName, TEXT("CityUpgradeRow"),
						   /* bWarnIfMissing */ false)
					 : nullptr;
	}
}

FString UCataclysmCityUpgradeMapping::EffectTextFor(FName RowName)
{
	const FCataclysmCityUpgradeRow* Row = FindCityUpgradeRow(RowName);

	return Row ? Row->Effect : FString();
}

FString UCataclysmCityUpgradeMapping::BranchFor(FName RowName)
{
	const FCataclysmCityUpgradeRow* Row = FindCityUpgradeRow(RowName);

	return Row ? Row->Branch : FString();
}

int64 UCataclysmCityUpgradeMapping::PriceAt(int32 TierOneGold, float GoldPerPileAtTier, float GoldPerPileAtTierOne)
{
	if (TierOneGold <= 0)
	{
		return 0;
	}
	if (GoldPerPileAtTierOne <= 0.0f || GoldPerPileAtTier <= 0.0f)
	{
		return TierOneGold;
	}
	return FMath::RoundToInt64(static_cast<double>(TierOneGold) * static_cast<double>(GoldPerPileAtTier)
							   / static_cast<double>(GoldPerPileAtTierOne));
}

int64 UCataclysmCityUpgradeMapping::PriceFor(FName RowName, int32 DifficultyTier, int32 UpgradeTier)
{
	const FCataclysmCityUpgradeRow* Row = FindCityUpgradeRow(RowName);
	// THE COLUMN OF THE TIER BOUGHT OR RAISED TO (issue #1265): tier 1 for a purchase.
	const int32 Gold = Row == nullptr ? 0
		: UpgradeTier == 3			  ? Row->Tier3Gold
		: UpgradeTier == 2			  ? Row->Tier2Gold
									  : Row->Tier1Gold;
	if (Row == nullptr || Gold <= 0)
	{
		return 0;
	}

	// THE PILE COLUMN SCALES IT, the one gold drops read (`UCataclysmDropRoll::GoldRow`, which clamps the tier).
	const UDataTable* GoldTable = UCataclysmDropRoll::LoadGoldTable();
	const FCataclysmGoldRow* AtTier = UCataclysmDropRoll::GoldRow(GoldTable, DifficultyTier);
	const FCataclysmGoldRow* AtTierOne = UCataclysmDropRoll::GoldRow(GoldTable, 1);
	if (AtTier == nullptr || AtTierOne == nullptr)
	{
		UE_LOG(LogCataclysm, Error,
			   TEXT("City upgrade %s is priced at its difficulty tier 1 figure, %d gold, unscaled: the Gold table (%s) "
					"would not load or has no row for tier %d. Run tools/generate_datatable_assets.py."),
			   *RowName.ToString(), Gold, UCataclysmDropRoll::GoldTableAssetPath, DifficultyTier);
		return Gold;
	}
	return PriceAt(Gold, AtTier->GoldPerPile, AtTierOne->GoldPerPile);
}
