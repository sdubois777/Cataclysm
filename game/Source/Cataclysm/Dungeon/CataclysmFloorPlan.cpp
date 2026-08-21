// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmFloorPlan.h"

namespace
{
	/**
	 * The four orthogonal steps.
	 *
	 * NAMED FOR THIS FILE AND NOT SHARED. Unreal merges a module's `.cpp` files
	 * into one translation unit, so two files defining the same helper in an
	 * anonymous namespace collide -- and only once both are committed, because
	 * UnrealBuildTool keeps modified files out of the merged unit. That is what
	 * `tools/tests/test_no_two_files_share_an_anonymous_helper.py` guards.
	 */
	const FIntPoint CataclysmFloorPlanSteps[4] = {
		FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1)
	};
}

void FCataclysmFloorPlan::Reset(int32 InWidth, int32 InHeight)
{
	Width = FMath::Max(0, InWidth);
	Height = FMath::Max(0, InHeight);
	Cells.Reset();
	Cells.Init(ECataclysmFloorCell::Solid, Width * Height);
	Entrance = FIntPoint(-1, -1);
	Exit = FIntPoint(-1, -1);
}

int32 FCataclysmFloorPlan::FloorCount() const
{
	int32 Count = 0;
	for (const ECataclysmFloorCell Cell : Cells)
	{
		Count += (Cell == ECataclysmFloorCell::Floor) ? 1 : 0;
	}
	return Count;
}

int32 FCataclysmFloorPlan::OrthogonalNeighbours(FIntPoint Cell) const
{
	int32 Count = 0;
	for (const FIntPoint& Step : CataclysmFloorPlanSteps)
	{
		Count += IsFloor(Cell + Step) ? 1 : 0;
	}
	return Count;
}

TArray<int32> CataclysmFloorDistancesFrom(const FCataclysmFloorPlan& Plan, FIntPoint Start)
{
	TArray<int32> Distance;
	Distance.Init(INDEX_NONE, Plan.Cells.Num());

	const int32 StartIndex = Plan.IndexOf(Start);
	if (StartIndex == INDEX_NONE || !Plan.IsFloor(Start))
	{
		return Distance;
	}

	// A plain breadth-first search. Every step costs one, so the first time a
	// cell is reached is the shortest way to it and it never needs revisiting.
	Distance[StartIndex] = 0;

	TArray<int32> Frontier;
	Frontier.Reserve(Plan.Cells.Num());
	Frontier.Add(StartIndex);

	for (int32 Read = 0; Read < Frontier.Num(); ++Read)
	{
		const int32 Index = Frontier[Read];
		const FIntPoint Here = Plan.CellAt(Index);
		const int32 Next = Distance[Index] + 1;

		for (const FIntPoint& Step : CataclysmFloorPlanSteps)
		{
			const FIntPoint There = Here + Step;
			const int32 ThereIndex = Plan.IndexOf(There);
			if (ThereIndex == INDEX_NONE || Distance[ThereIndex] != INDEX_NONE)
			{
				continue;
			}
			if (Plan.Cells[ThereIndex] != ECataclysmFloorCell::Floor)
			{
				continue;
			}
			Distance[ThereIndex] = Next;
			Frontier.Add(ThereIndex);
		}
	}

	return Distance;
}

FCataclysmFloorQuality CataclysmMeasureFloor(const FCataclysmFloorPlan& Plan)
{
	FCataclysmFloorQuality Quality;

	const int32 Total = Plan.Cells.Num();
	if (Total <= 0)
	{
		return Quality;
	}

	TArray<bool> Narrow;
	Narrow.Init(false, Total);

	for (int32 Index = 0; Index < Total; ++Index)
	{
		if (Plan.Cells[Index] != ECataclysmFloorCell::Floor)
		{
			continue;
		}
		++Quality.FloorCells;

		const int32 Neighbours = Plan.OrthogonalNeighbours(Plan.CellAt(Index));
		if (Neighbours <= 1)
		{
			++Quality.DeadEnds;
		}
		if (Neighbours <= 2)
		{
			++Quality.NarrowCells;
			Narrow[Index] = true;
		}
	}

	// The longest unbroken stretch of single-file floor: the largest group of
	// narrow cells that touch each other. See the field's comment for why this
	// is asked separately from the fraction above.
	{
		TArray<bool> Counted;
		Counted.Init(false, Total);
		TArray<int32> Frontier;

		for (int32 Start = 0; Start < Total; ++Start)
		{
			if (!Narrow[Start] || Counted[Start])
			{
				continue;
			}

			int32 Size = 0;
			Frontier.Reset();
			Frontier.Add(Start);
			Counted[Start] = true;

			for (int32 Read = 0; Read < Frontier.Num(); ++Read)
			{
				++Size;
				const FIntPoint Here = Plan.CellAt(Frontier[Read]);
				for (const FIntPoint& Step : CataclysmFloorPlanSteps)
				{
					const int32 ThereIndex = Plan.IndexOf(Here + Step);
					if (ThereIndex == INDEX_NONE || Counted[ThereIndex]
						|| !Narrow[ThereIndex])
					{
						continue;
					}
					Counted[ThereIndex] = true;
					Frontier.Add(ThereIndex);
				}
			}

			Quality.LongestNarrowRun = FMath::Max(Quality.LongestNarrowRun, Size);
		}
	}

	Quality.OpenFraction = static_cast<float>(Quality.FloorCells) / static_cast<float>(Total);
	Quality.NarrowFraction = (Quality.FloorCells > 0)
		? static_cast<float>(Quality.NarrowCells) / static_cast<float>(Quality.FloorCells)
		: 0.0f;

	// Reachability is measured from the entrance, because that is the only place
	// the player can start. A floor that is fully connected to itself but not to
	// the entrance is exactly as broken as one that is in pieces.
	if (!Plan.IsFloor(Plan.Entrance))
	{
		Quality.UnreachableCells = Quality.FloorCells;
		return Quality;
	}

	const TArray<int32> Distance = CataclysmFloorDistancesFrom(Plan, Plan.Entrance);
	for (int32 Index = 0; Index < Total; ++Index)
	{
		if (Plan.Cells[Index] == ECataclysmFloorCell::Floor && Distance[Index] == INDEX_NONE)
		{
			++Quality.UnreachableCells;
		}
	}

	const int32 ExitIndex = Plan.IndexOf(Plan.Exit);
	if (ExitIndex != INDEX_NONE && Distance[ExitIndex] != INDEX_NONE)
	{
		Quality.bExitReachable = true;
		Quality.PathLength = Distance[ExitIndex];
	}

	return Quality;
}

const TCHAR* CataclysmFloorLayoutName(ECataclysmFloorLayout Layout)
{
	switch (Layout)
	{
	case ECataclysmFloorLayout::Halls:   return TEXT("Halls");
	case ECataclysmFloorLayout::Caverns: return TEXT("Caverns");
	case ECataclysmFloorLayout::Arena:   return TEXT("Arena");
	default:                             return TEXT("Unknown");
	}
}
