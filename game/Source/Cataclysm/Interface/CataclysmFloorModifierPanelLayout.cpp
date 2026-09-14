// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmFloorModifierPanelLayout.h"

#include "Data/CataclysmDataRows.h"
#include "Dungeon/CataclysmDungeonModifierTable.h"

TArray<FCataclysmFloorModifierLine> UCataclysmFloorModifierPanelLayout::LinesFor(
	const TArray<FName>& RowKeys, const UDataTable* DungeonModifierTable,
	const TMap<FName, FString>& LiveCounts)
{
	TArray<FCataclysmFloorModifierLine> Lines;
	Lines.Reserve(RowKeys.Num());

	for (const FName Key : RowKeys)
	{
		FCataclysmFloorModifierLine Line;
		Line.RowKey = Key;
		Line.Built = UCataclysmDungeonModifierEffects::BuiltStateOf(Key);

		const FCataclysmDungeonModifierRow* Row =
			UCataclysmDungeonModifierTable::FindRow(DungeonModifierTable, Key);
		Line.bIsARow = Row != nullptr;
		Line.Name = Row != nullptr ? Row->ModifierName : Key.ToString();
		Line.Description = Row != nullptr ? Row->Description : FString();

		// WHAT THIS ROW IS COUNTING, IF ANYTHING IS COUNTING FOR IT. Looked up by
		// key rather than asked of the row, because the count belongs to the rule
		// that keeps it and not to the table.
		if (const FString* Counting = LiveCounts.Find(Key))
		{
			Line.LiveCount = *Counting;
		}

		Lines.Add(MoveTemp(Line));
	}

	return Lines;
}

FString UCataclysmFloorModifierPanelLayout::HeadingFor(int32 FloorNumber,
													   int32 ModifierCount)
{
	return FString::Printf(TEXT("Floor %d: %d dungeon modifier%s"),
						   FMath::Max(1, FloorNumber), ModifierCount,
						   ModifierCount == 1 ? TEXT("") : TEXT("s"));
}

FString UCataclysmFloorModifierPanelLayout::NameLineFor(const FCataclysmFloorModifierLine& Line)
{
	FString Named;

	if (!Line.bIsARow)
	{
		Named = FString::Printf(TEXT("%s (not a row of the dungeon modifier table)"),
								*Line.Name);
	}
	else
	{
		switch (Line.Built)
		{
		case ECataclysmModifierBuilt::Built:
			Named = Line.Name;
			break;

		case ECataclysmModifierBuilt::Partly:
			Named = FString::Printf(TEXT("%s (partly built)"), *Line.Name);
			break;

		case ECataclysmModifierBuilt::NotBuilt:
		default:
			Named = FString::Printf(TEXT("%s (not built yet: it does nothing)"),
									*Line.Name);
			break;
		}
	}

	// AND WHAT IT IS COUNTING, WHEN IT COUNTS ANYTHING. Appended rather than
	// woven into the branches above so that a row counting nothing comes out of
	// this function byte for byte as it did before the count existed -- which is
	// what keeps the assertions in `CataclysmDungeonModifierEffectsTests.cpp`
	// true without touching them.
	if (!Line.LiveCount.IsEmpty())
	{
		Named = FString::Printf(TEXT("%s (%s)"), *Named, *Line.LiveCount);
	}

	return Named;
}
