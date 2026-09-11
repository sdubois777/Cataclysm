// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmFloorModifierPanelLayout.h"

#include "Data/CataclysmDataRows.h"
#include "Dungeon/CataclysmDungeonModifierTable.h"

TArray<FCataclysmFloorModifierLine> UCataclysmFloorModifierPanelLayout::LinesFor(
	const TArray<FName>& RowKeys, const UDataTable* DungeonModifierTable)
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
	if (!Line.bIsARow)
	{
		return FString::Printf(TEXT("%s (not a row of the dungeon modifier table)"),
							   *Line.Name);
	}

	switch (Line.Built)
	{
	case ECataclysmModifierBuilt::Built:
		return Line.Name;

	case ECataclysmModifierBuilt::Partly:
		return FString::Printf(TEXT("%s (partly built)"), *Line.Name);

	case ECataclysmModifierBuilt::NotBuilt:
	default:
		return FString::Printf(TEXT("%s (not built yet: it does nothing)"), *Line.Name);
	}
}
