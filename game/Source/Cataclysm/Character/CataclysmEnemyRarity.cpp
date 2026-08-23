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

float UCataclysmEnemyRarity::BodyScaleForStep(
	const UDataTable* EnemyRarityTable, int32 Step)
{
	// ONE BY DEFAULT, SO A FAILURE CHANGES NOTHING, for the same reason
	// ScalingFromCommon below defaults to one: a missing table has to leave
	// the creature as it was rather than scaling it to nothing.
	float Found = 1.0f;
	if (!EnemyRarityTable)
	{
		return Found;
	}

	EnemyRarityTable->ForeachRow<FCataclysmEnemyRarityRow>(
		TEXT("UCataclysmEnemyRarity::BodyScaleForStep"),
		[&](const FName&, const FCataclysmEnemyRarityRow& Row)
		{
			if (Row.Step == Step && Row.BodyScale > 0.0f)
			{
				Found = Row.BodyScale;
			}
		});

	return Found;
}

bool UCataclysmEnemyRarity::ScalingFromCommon(
	const UDataTable* EnemyRarityTable, int32 Step,
	float& OutHealth, float& OutDamage, float& OutArmour)
{
	// ONE BY DEFAULT, SO A FAILURE CHANGES NOTHING. A missing table has to leave
	// the creature standing at its designed Common figures rather than at zero,
	// which would be an enemy that dies to one hit and deals nothing.
	OutHealth = 1.0f;
	OutDamage = 1.0f;
	OutArmour = 1.0f;

	if (!EnemyRarityTable)
	{
		return false;
	}

	const FCataclysmEnemyRarityRow* Wanted = nullptr;
	const FCataclysmEnemyRarityRow* Common = nullptr;

	EnemyRarityTable->ForeachRow<FCataclysmEnemyRarityRow>(
		TEXT("UCataclysmEnemyRarity::ScalingFromCommon"),
		[&](const FName&, const FCataclysmEnemyRarityRow& Row)
		{
			if (Row.Step == Step)
			{
				Wanted = &Row;
			}
			// COMMON IS FOUND BY ITS STEP AND NOT BY ITS NAME. The name is a
			// display string and could be translated or renamed; step 0 is what
			// the table means by "the bottom rung".
			if (Row.Step == 0)
			{
				Common = &Row;
			}
		});

	if (!Wanted || !Common)
	{
		return false;
	}

	// A ZERO IN THE DENOMINATOR IS A BROKEN TABLE, not a rarity worth nothing.
	// Armour is the one that can legitimately be zero for a creature, but that
	// is the creature's own share on FCataclysmEnemyArchetypeRow, not this.
	const auto Ratio = [](float Wanted, float Common, float& Out)
	{
		if (Common > 0.0f && Wanted > 0.0f)
		{
			Out = Wanted / Common;
		}
	};

	Ratio(Wanted->HealthPerScore, Common->HealthPerScore, OutHealth);
	Ratio(Wanted->DamagePerScore, Common->DamagePerScore, OutDamage);
	Ratio(Wanted->ArmorPerScore, Common->ArmorPerScore, OutArmour);

	return true;
}
