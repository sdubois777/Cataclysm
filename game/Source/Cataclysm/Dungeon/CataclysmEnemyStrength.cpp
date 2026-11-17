// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmEnemyStrength.h"
#include "Cataclysm.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"

const UDataTable* UCataclysmEnemyStrength::LoadEnemyArchetypeTable()
{
	const UDataTable* Table = LoadObject<UDataTable>(
		nullptr, TEXT("/Game/Data/DT_EnemyArchetypes.DT_EnemyArchetypes"));
	if (!Table)
	{
		// NAMES BOTH SCRIPTS, because the two failures look the same from here:
		// the model never produced the CSV, or the CSV was never imported as an
		// asset. Every other loader in this project says the same thing.
		UE_LOG(LogCataclysm, Error,
			TEXT("Could not load DT_EnemyArchetypes. It is produced by "
				 "tools/generate_datatable_assets.py from "
				 "game/Data/EnemyArchetypes.csv, which "
				 "tools/generate_datatables.py produces from the design "
				 "workbook."));
	}
	return Table;
}

bool UCataclysmEnemyStrength::StartingFiguresFor(const UDataTable* EnemyRarityTable,
												  const UDataTable* EnemyArchetypeTable,
												  FName ArchetypeRow,
												  const FCataclysmScoredFloor& Floor,
												  int32 RarityStep,
												  FCataclysmStartingFigures& Out)
{
	if (!EnemyRarityTable || !EnemyArchetypeTable || ArchetypeRow.IsNone())
	{
		return false;
	}

	// COMMON IS FOUND BY ITS STEP AND NOT BY ITS NAME, as `UCataclysmEnemyRarity::ScalingFromCommon` finds it: step 0
	// is what the table means by the bottom rung. The creature then multiplies these by that function's ratio.
	const FCataclysmEnemyRarityRow* Common = nullptr;
	EnemyRarityTable->ForeachRow<FCataclysmEnemyRarityRow>(
		TEXT("UCataclysmEnemyStrength::StartingFiguresFor"),
		[&Common](const FName&, const FCataclysmEnemyRarityRow& Row)
		{
			if (Row.Step == 0)
			{
				Common = &Row;
			}
		});

	const FCataclysmEnemyArchetypeRow* Kind = EnemyArchetypeTable->FindRow<FCataclysmEnemyArchetypeRow>(
		ArchetypeRow, TEXT("UCataclysmEnemyStrength::StartingFiguresFor"), false);
	if (!Common || !Kind)
	{
		return false;
	}

	const double Strength = UCataclysmEnemyScore::StrengthScoreFor(Floor, RarityStep);

	// AT LEAST ONE HEALTH, as the model's `stats_for` holds it. The minimum strength keeps every creature far above it
	// -- a Common Imp on the first floor of tier 1 has 4.2 -- so this only stops a nonsense table building a corpse.
	Out.Health = FMath::Max(1.0f, static_cast<float>(Strength * Common->HealthPerScore * Kind->HealthShare));
	Out.AttackDamage = static_cast<float>(Strength * Common->DamagePerScore * Kind->DamageShare
		* UCataclysmEnemyScore::DamageScale(Floor.DifficultyTier));
	Out.Armour = static_cast<float>(Strength * Common->ArmorPerScore * Kind->ArmorShare);
	return true;
}
