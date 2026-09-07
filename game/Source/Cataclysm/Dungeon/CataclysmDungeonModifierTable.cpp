// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmDungeonModifierTable.h"

#include "Cataclysm.h"
#include "Data/CataclysmDataRows.h"
#include "Empire/CataclysmRoster.h"
#include "Engine/DataTable.h"

const TCHAR* UCataclysmDungeonModifierTable::GenericCataclysm = TEXT("Generic");

const UDataTable* UCataclysmDungeonModifierTable::LoadDungeonModifierTable()
{
	const UDataTable* Table = LoadObject<UDataTable>(
		nullptr, TEXT("/Game/Data/DT_DungeonModifiers.DT_DungeonModifiers"));
	if (!Table)
	{
		// NAMES BOTH SCRIPTS, because the two failures look the same from here:
		// the workbook never produced the CSV, or the CSV was never imported as
		// an asset. Every other loader in this project says the same thing.
		UE_LOG(LogCataclysm, Error,
			TEXT("Could not load DT_DungeonModifiers. It is produced by "
				 "tools/generate_datatable_assets.py from "
				 "game/Data/DungeonModifiers.csv, which "
				 "tools/generate_datatables.py produces from the design "
				 "workbook."));
	}
	return Table;
}

ECataclysmType UCataclysmDungeonModifierTable::CataclysmFor(
	const FString& ColumnValue, bool& bOutRecognised)
{
	if (ColumnValue.Equals(GenericCataclysm, ESearchCase::IgnoreCase))
	{
		// RECOGNISED, AND STILL `None`. The Generic column is a design decision
		// and not a broken row. See the header.
		bOutRecognised = true;
		return ECataclysmType::None;
	}

	for (const ECataclysmType Cataclysm : UCataclysmRoster::All())
	{
		if (ColumnValue.Equals(UCataclysmRoster::NameFor(Cataclysm).ToString(),
							   ESearchCase::IgnoreCase))
		{
			bOutRecognised = true;
			return Cataclysm;
		}
	}

	bOutRecognised = false;
	return ECataclysmType::None;
}

TArray<FCataclysmDungeonModifier> UCataclysmDungeonModifierTable::PoolFromTable(
	const UDataTable* DungeonModifierTable)
{
	TArray<FCataclysmDungeonModifier> Pool;
	if (!DungeonModifierTable)
	{
		return Pool;
	}

	DungeonModifierTable->ForeachRow<FCataclysmDungeonModifierRow>(
		TEXT("UCataclysmDungeonModifierTable::PoolFromTable"),
		[&Pool](const FName& Key, const FCataclysmDungeonModifierRow& Row)
		{
			bool bRecognised = false;
			const ECataclysmType Cataclysm =
				CataclysmFor(Row.CataclysmType, bRecognised);

			if (!bRecognised)
			{
				// KEPT AND LOGGED. See the header: dropping it would make a
				// broken row look like a row that is not there, and the pool
				// rule already excludes it because `None` is never active.
				UE_LOG(LogCataclysm, Warning,
					TEXT("Dungeon modifier row '%s' names Cataclysm type '%s', "
						 "which is neither one of the eight nor 'Generic'. It "
						 "will never be drawn. Check the CataclysmType column "
						 "of game/Data/DungeonModifiers.csv."),
					*Key.ToString(), *Row.CataclysmType);
			}

			FCataclysmDungeonModifier Modifier;
			Modifier.RowKey = Key;
			Modifier.ModifierName = FName(*Row.ModifierName);
			Modifier.Cataclysm = Cataclysm;

			// THE `Weight` COLUMN, WHICH IS A DANGER SCORE AND NOT A FREQUENCY.
			// The project owner settled that on 2026-09-05; see
			// `FCataclysmDungeonModifierRow::Weight` and docs/DECISIONS.md.
			Modifier.Danger = Row.Weight;

			Pool.Add(Modifier);
		});

	return Pool;
}

TArray<FCataclysmDungeonModifier> UCataclysmDungeonModifierTable::LoadPool()
{
	return PoolFromTable(LoadDungeonModifierTable());
}

const FCataclysmDungeonModifierRow* UCataclysmDungeonModifierTable::FindRow(
	const UDataTable* DungeonModifierTable, FName Key)
{
	if (!DungeonModifierTable || Key.IsNone())
	{
		return nullptr;
	}

	return DungeonModifierTable->FindRow<FCataclysmDungeonModifierRow>(
		Key, TEXT("UCataclysmDungeonModifierTable::FindRow"),
		/*bWarnIfMissing=*/false);
}

FString UCataclysmDungeonModifierTable::NameOf(FName RowKey)
{
	const FCataclysmDungeonModifierRow* Row =
		FindRow(LoadDungeonModifierTable(), RowKey);

	// THE KEY ITSELF WHEN THE ROW IS GONE, rather than an empty string. A
	// player reading `Celestial_Edict_of_Silence` on a screen is told less
	// nicely than a player reading `Edict of Silence`, and is still told
	// something.
	return Row != nullptr ? Row->ModifierName : RowKey.ToString();
}
