// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmEnemyModifiers.h"

#include "Cataclysm.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"

const TCHAR* UCataclysmEnemyModifiers::GenericCataclysm = TEXT("Generic");

const UDataTable* UCataclysmEnemyModifiers::LoadEnemyModifierTable()
{
	const UDataTable* Table = LoadObject<UDataTable>(
		nullptr, TEXT("/Game/Data/DT_EnemyModifiers.DT_EnemyModifiers"));
	if (!Table)
	{
		// NAMES BOTH SCRIPTS, because the two failures look the same from here:
		// the workbook never produced the CSV, or the CSV was never imported as
		// an asset. Every other loader in this project says the same thing.
		UE_LOG(LogCataclysm, Error,
			TEXT("Could not load DT_EnemyModifiers. It is produced by "
				 "tools/generate_datatable_assets.py from "
				 "game/Data/EnemyModifiers.csv, which "
				 "tools/generate_datatables.py produces from the design "
				 "workbook."));
	}
	return Table;
}

int32 UCataclysmEnemyModifiers::CountForRarityStep(int32 RarityStep)
{
	// THE STEP ITSELF. `docs/Cataclysm_GDD_v2.md` gives 0, 1, 2, 3, 4, 5 for
	// Common through Cataclysm Boss and `game/Data/EnemyRarities.csv` numbers
	// those same rungs 0 to 5, so the two ladders are one ladder.
	//
	// NOT CAPPED AT FIVE HERE. A step above the table's own is a caller error
	// rather than a rung, and `ACataclysmEnemyCharacter::SetRarityStep` is where
	// a step is bounded. Capping here as well would hide that.
	return FMath::Max(0, RarityStep);
}

bool UCataclysmEnemyModifiers::IsDrawableBy(
	const FCataclysmEnemyModifierRow& Row, FName CataclysmType)
{
	// THE WHOLE POOL RULE, IN ONE PLACE. `docs/Cataclysm_GDD_v2.md`: an enemy's
	// modifiers are "drawn from its own Cataclysm's column and the Generic one".
	return Row.CataclysmType.Equals(CataclysmType.ToString(),
									ESearchCase::IgnoreCase)
		|| Row.CataclysmType.Equals(GenericCataclysm, ESearchCase::IgnoreCase);
}

TArray<FName> UCataclysmEnemyModifiers::PoolFor(
	const UDataTable* EnemyModifierTable, FName CataclysmType)
{
	TArray<FName> Pool;
	if (!EnemyModifierTable)
	{
		return Pool;
	}

	EnemyModifierTable->ForeachRow<FCataclysmEnemyModifierRow>(
		TEXT("UCataclysmEnemyModifiers::PoolFor"),
		[&Pool, CataclysmType](const FName& Key,
							   const FCataclysmEnemyModifierRow& Row)
		{
			if (IsDrawableBy(Row, CataclysmType))
			{
				Pool.Add(Key);
			}
		});

	// SORTED, BECAUSE A DataTable IS A MAP. See the header: an unsorted walk
	// would hand the same seed a different modifier on a different run, which is
	// the fault `UCataclysmEnemyRarity::SpawnableSteps` sorts to avoid.
	Pool.Sort([](const FName& A, const FName& B)
	{
		return A.LexicalLess(B);
	});

	return Pool;
}

TArray<FName> UCataclysmEnemyModifiers::Draw(
	const UDataTable* EnemyModifierTable, FName CataclysmType, int32 Count,
	FRandomStream& Stream, const TArray<FName>& Already)
{
	TArray<FName> Drawn;
	if (Count <= 0)
	{
		return Drawn;
	}

	TArray<FName> Pool = PoolFor(EnemyModifierTable, CataclysmType);

	// WHAT THE CREATURE ALREADY HAS IS OUT OF THE POOL BEFORE ANYTHING IS
	// DRAWN, rather than being drawn and rejected. Rejecting would make the
	// number of draws depend on what was already carried, so the same seed on
	// two creatures that had been given different starting lists would produce
	// unrelated results for the rest of the draw.
	Pool.RemoveAll([&Already](const FName& Key)
	{
		return Already.Contains(Key);
	});

	while (Drawn.Num() < Count && Pool.Num() > 0)
	{
		const int32 Index = Stream.RandRange(0, Pool.Num() - 1);
		Drawn.Add(Pool[Index]);

		// REMOVED SO IT CANNOT COME UP AGAIN. `RemoveAtSwap` would be cheaper
		// and would reorder what is left, which makes the draw depend on the
		// order things were removed in as well as on the stream.
		Pool.RemoveAt(Index);
	}

	return Drawn;
}

const FCataclysmEnemyModifierRow* UCataclysmEnemyModifiers::FindRow(
	const UDataTable* EnemyModifierTable, FName Key)
{
	if (!EnemyModifierTable || Key.IsNone())
	{
		return nullptr;
	}

	return EnemyModifierTable->FindRow<FCataclysmEnemyModifierRow>(
		Key, TEXT("UCataclysmEnemyModifiers::FindRow"), /*bWarnIfMissing=*/false);
}
