// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmEnemyRarity.h"
#include "Cataclysm.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"

const UDataTable* UCataclysmEnemyRarity::LoadEnemyRarityTable()
{
	const UDataTable* Table = LoadObject<UDataTable>(
		nullptr, TEXT("/Game/Data/DT_EnemyRarities.DT_EnemyRarities"));
	if (!Table)
	{
		// NAMES BOTH SCRIPTS, because the two failures look the same from here:
		// the model never produced the CSV, or the CSV was never imported as an
		// asset. Every other loader in this project says the same thing.
		UE_LOG(LogCataclysm, Error,
			TEXT("Could not load DT_EnemyRarities. It is produced by "
				 "tools/generate_datatable_assets.py from "
				 "game/Data/EnemyRarities.csv, which "
				 "tools/generate_datatables.py produces from "
				 "sim/cataclysm_sim/enemy_stats.py."));
	}
	return Table;
}

void UCataclysmEnemyRarity::SpawnableSteps(const UDataTable* EnemyRarityTable,
										   TArray<int32>& OutSteps)
{
	OutSteps.Reset();
	if (!EnemyRarityTable)
	{
		return;
	}

	EnemyRarityTable->ForeachRow<FCataclysmEnemyRarityRow>(
		TEXT("UCataclysmEnemyRarity::SpawnableSteps"),
		[&OutSteps](const FName& Key, const FCataclysmEnemyRarityRow& Row)
		{
			if (Row.SpawnWeight > 0.0f)
			{
				OutSteps.Add(Row.Step);
			}
		});

	// SORTED, BECAUSE A DataTable IS A MAP. See the header: an unsorted walk
	// would give a different answer from the same stream on a different run.
	OutSteps.Sort();
}

float UCataclysmEnemyRarity::SpawnWeightForStep(
	const UDataTable* EnemyRarityTable, int32 Step)
{
	if (!EnemyRarityTable)
	{
		return 0.0f;
	}

	float Found = 0.0f;
	EnemyRarityTable->ForeachRow<FCataclysmEnemyRarityRow>(
		TEXT("UCataclysmEnemyRarity::SpawnWeightForStep"),
		[Step, &Found](const FName& Key, const FCataclysmEnemyRarityRow& Row)
		{
			if (Row.Step == Step)
			{
				Found = Row.SpawnWeight;
			}
		});
	return Found;
}

FString UCataclysmEnemyRarity::RarityNameForStep(
	const UDataTable* EnemyRarityTable, int32 Step)
{
	if (!EnemyRarityTable)
	{
		return FString();
	}

	FString Found;
	EnemyRarityTable->ForeachRow<FCataclysmEnemyRarityRow>(
		TEXT("UCataclysmEnemyRarity::RarityNameForStep"),
		[Step, &Found](const FName& Key, const FCataclysmEnemyRarityRow& Row)
		{
			if (Row.Step == Step)
			{
				Found = Row.RarityName;
			}
		});
	return Found;
}

int32 UCataclysmEnemyRarity::RollRarityStep(const UDataTable* EnemyRarityTable,
											FRandomStream& Stream)
{
	TArray<int32> Steps;
	SpawnableSteps(EnemyRarityTable, Steps);
	if (Steps.IsEmpty())
	{
		// COMMON IS THE RIGHT ANSWER TO "SOMETHING WENT WRONG". See the header.
		return 0;
	}

	float Total = 0.0f;
	for (const int32 Step : Steps)
	{
		Total += SpawnWeightForStep(EnemyRarityTable, Step);
	}
	if (Total <= 0.0f)
	{
		return 0;
	}

	// THE TOTAL IS MEASURED RATHER THAN ASSUMED TO BE 1. The generator refuses
	// to write a table whose weights do not sum to one, so it is one here; but
	// this reads an asset that could have been rebuilt from an older CSV, and a
	// draw against an assumed total would silently favour the last rung.
	const float Drawn = Stream.FRandRange(0.0f, Total);

	float Running = 0.0f;
	for (const int32 Step : Steps)
	{
		Running += SpawnWeightForStep(EnemyRarityTable, Step);
		if (Drawn <= Running)
		{
			return Step;
		}
	}

	// ONLY REACHABLE ON A ROUNDING EDGE, when the draw lands a hair above the
	// running total. The last spawnable rung is what that means.
	return Steps.Last();
}
