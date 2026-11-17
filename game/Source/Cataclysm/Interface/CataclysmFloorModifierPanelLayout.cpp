// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmFloorModifierPanelLayout.h"

#include "Data/CataclysmDataRows.h"
#include "Dungeon/CataclysmDungeonModifierEffects.h"
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

		// THE CORRUPTED DOUBLE'S LINE, WHICH IS NO ROW: named for what it is, with its count. Issue #2688.
		if (Key == FName(ConsumptionDoubleKey))
		{
			Line.bIsTheConsumptionDouble = true;
			Line.Built = ECataclysmModifierBuilt::Built;
			Line.Name = ConsumptionDoubleName;
			if (const FString* Counting = LiveCounts.Find(Key))
			{
				Line.LiveCount = *Counting;
			}
			Lines.Add(MoveTemp(Line));
			continue;
		}

		// THE CATACLYSM BOSS'S LINE, WHICH IS NO ROW EITHER: named for what it is, with whether it seals the stairs.
		// Issue #1324 slice 6d.
		if (Key == FName(CataclysmBossKey))
		{
			Line.bIsTheCataclysmBoss = true;
			Line.Built = ECataclysmModifierBuilt::Built;
			Line.Name = CataclysmBossName;
			if (const FString* Counting = LiveCounts.Find(Key))
			{
				Line.LiveCount = *Counting;
			}
			Lines.Add(MoveTemp(Line));
			continue;
		}

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

	if (Line.bIsTheConsumptionDouble || Line.bIsTheCataclysmBoss)
	{
		// The double's or the Cataclysm Boss's: no row, and not marked as a missing one. Issues #2688 and #1324.
		Named = Line.Name;
	}
	else if (!Line.bIsARow)
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
