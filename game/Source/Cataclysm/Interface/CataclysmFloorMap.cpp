// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmFloorMap.h"

#include "Dungeon/CataclysmFloorPlan.h"

const TCHAR* UCataclysmFloorMap::FloorHex = TEXT("D8D2C4");
const TCHAR* UCataclysmFloorMap::PlayerHex = TEXT("FFFFFF");
const TCHAR* UCataclysmFloorMap::ExitHex = TEXT("F0B040");

bool UCataclysmFloorMap::LineOfSight(const FCataclysmFloorPlan& Plan, FIntPoint From, FIntPoint To)
{
	// BRESENHAM'S LINE ON THE GRID, asking every cell strictly between the two ends. The ends are not asked: the
	// player stands on one, and the other may be the wall the line ends at.
	int32 X = From.X;
	int32 Y = From.Y;
	const int32 DeltaX = FMath::Abs(To.X - From.X);
	const int32 DeltaY = -FMath::Abs(To.Y - From.Y);
	const int32 StepX = From.X < To.X ? 1 : -1;
	const int32 StepY = From.Y < To.Y ? 1 : -1;
	int32 Error = DeltaX + DeltaY;
	while (X != To.X || Y != To.Y)
	{
		const int32 Twice = 2 * Error;
		if (Twice >= DeltaY)
		{
			Error += DeltaY;
			X += StepX;
		}
		if (Twice <= DeltaX)
		{
			Error += DeltaX;
			Y += StepY;
		}
		if ((X != To.X || Y != To.Y) && !Plan.IsFloor(FIntPoint(X, Y)))
		{
			return false;
		}
	}
	return true;
}

bool UCataclysmFloorMap::IsSeen(const FCataclysmFloorPlan& Plan, const TBitArray<>& Seen, FIntPoint Cell)
{
	const int32 Index = Plan.IndexOf(Cell);
	return Index != INDEX_NONE && Seen.Num() == Plan.Width * Plan.Height && Seen[Index];
}

int32 UCataclysmFloorMap::RevealAround(const FCataclysmFloorPlan& Plan, TBitArray<>& Seen, FIntPoint From,
									   int32 RadiusCells)
{
	const int32 Cells = Plan.Width * Plan.Height;
	if (Seen.Num() != Cells)
	{
		Seen.Init(false, Cells);
	}
	// NOTHING IS SEEN FROM OFF THE FLOOR: off the grid (a hidden chamber's cells are), or a solid cell.
	if (!Plan.IsFloor(From) || RadiusCells < 0)
	{
		return 0;
	}

	int32 Newly = 0;
	const int32 RadiusSquared = RadiusCells * RadiusCells;
	for (int32 DeltaX = -RadiusCells; DeltaX <= RadiusCells; ++DeltaX)
	{
		for (int32 DeltaY = -RadiusCells; DeltaY <= RadiusCells; ++DeltaY)
		{
			if (DeltaX * DeltaX + DeltaY * DeltaY > RadiusSquared)
			{
				continue;
			}
			const FIntPoint Cell(From.X + DeltaX, From.Y + DeltaY);
			const int32 Index = Plan.IndexOf(Cell);
			if (Index == INDEX_NONE || Seen[Index] || !LineOfSight(Plan, From, Cell))
			{
				continue;
			}
			Seen[Index] = true;
			++Newly;
		}
	}
	return Newly;
}

TArray<FCataclysmFloorMapRun> UCataclysmFloorMap::RunsOf(const FCataclysmFloorPlan& Plan, const TBitArray<>& Seen)
{
	TArray<FCataclysmFloorMapRun> Runs;
	if (Seen.Num() != Plan.Width * Plan.Height)
	{
		return Runs;
	}
	for (int32 X = 0; X < Plan.Width; ++X)
	{
		int32 First = INDEX_NONE;
		for (int32 Y = 0; Y <= Plan.Height; ++Y)
		{
			const FIntPoint Cell(X, Y);
			const bool bDrawn = Y < Plan.Height && Plan.IsFloor(Cell) && Seen[Plan.IndexOf(Cell)];
			if (bDrawn && First == INDEX_NONE)
			{
				First = Y;
			}
			else if (!bDrawn && First != INDEX_NONE)
			{
				Runs.Add(FCataclysmFloorMapRun{X, First, Y - 1});
				First = INDEX_NONE;
			}
		}
	}
	return Runs;
}

FCataclysmFloorMapLayout UCataclysmFloorMap::LayoutFor(const FVector2D& ScreenPx)
{
	FCataclysmFloorMapLayout Layout;
	Layout.Centre = ScreenPx * 0.5;
	Layout.HalfSide = static_cast<float>(FMath::Min(ScreenPx.X, ScreenPx.Y)) * AreaShareOfShorterSide * 0.5f;
	Layout.PxPerCell = PxPerCellAt1080 * static_cast<float>(ScreenPx.Y) / ReferenceHeightPx;
	return Layout;
}

FVector2D UCataclysmFloorMap::ScreenOf(const FVector2D& CellPosition, const FVector2D& PlayerCell,
									   const FCataclysmFloorMapLayout& Layout)
{
	// +X IS UP AND +Y IS RIGHT, as the camera shows the world: it looks along world +X.
	const FVector2D Offset = CellPosition - PlayerCell;
	return Layout.Centre + FVector2D(Offset.Y, -Offset.X) * Layout.PxPerCell;
}

FBox2D UCataclysmFloorMap::BoxOf(const FCataclysmFloorMapRun& Run, const FVector2D& PlayerCell,
								 const FCataclysmFloorMapLayout& Layout)
{
	// A CELL REACHES HALF A CELL EACH SIDE OF ITS CENTRE.
	const FVector2D Near = ScreenOf(FVector2D(Run.X - 0.5, Run.FirstY - 0.5), PlayerCell, Layout);
	const FVector2D Far = ScreenOf(FVector2D(Run.X + 0.5, Run.LastY + 0.5), PlayerCell, Layout);
	return FBox2D(FVector2D(FMath::Min(Near.X, Far.X), FMath::Min(Near.Y, Far.Y)),
				  FVector2D(FMath::Max(Near.X, Far.X), FMath::Max(Near.Y, Far.Y)));
}

bool UCataclysmFloorMap::ClipToArea(FBox2D& Box, const FCataclysmFloorMapLayout& Layout)
{
	const FBox2D Area = Layout.Area();
	const FVector2D Min(FMath::Max(Box.Min.X, Area.Min.X), FMath::Max(Box.Min.Y, Area.Min.Y));
	const FVector2D Max(FMath::Min(Box.Max.X, Area.Max.X), FMath::Min(Box.Max.Y, Area.Max.Y));
	if (Min.X >= Max.X || Min.Y >= Max.Y)
	{
		return false;
	}
	Box = FBox2D(Min, Max);
	return true;
}

FVector2D UCataclysmFloorMap::MarkerAt(const FVector2D& CellPosition, const FVector2D& PlayerCell,
									   const FCataclysmFloorMapLayout& Layout, bool& bOutOnTheEdge)
{
	const FVector2D At = ScreenOf(CellPosition, PlayerCell, Layout);
	const FVector2D Offset = At - Layout.Centre;
	const double Furthest = FMath::Max(FMath::Abs(Offset.X), FMath::Abs(Offset.Y));
	bOutOnTheEdge = Furthest > Layout.HalfSide;
	if (!bOutOnTheEdge)
	{
		return At;
	}
	// ALONG THE SAME DIRECTION, scaled until its larger component reaches the square's edge.
	return Layout.Centre + Offset * (Layout.HalfSide / Furthest);
}

void UCataclysmFloorMap::FacingTriangle(const FVector2D& Centre, float YawDegrees, float SizePx,
										FVector2D OutCorners[3])
{
	// WORLD YAW TO THE MAP: world +X (yaw 0) is up the screen, world +Y (yaw 90) is right.
	const float Radians = FMath::DegreesToRadians(YawDegrees);
	const FVector2D Forward(FMath::Sin(Radians), -FMath::Cos(Radians));
	const FVector2D Side(-Forward.Y, Forward.X);
	OutCorners[0] = Centre + Forward * SizePx;
	OutCorners[1] = Centre - Forward * (SizePx * 0.6f) + Side * (SizePx * 0.6f);
	OutCorners[2] = Centre - Forward * (SizePx * 0.6f) - Side * (SizePx * 0.6f);
}

bool UCataclysmFloorMap::ShouldDraw(bool bShown, bool bHasFloor, bool bAFullScreenIsOpen)
{
	return bShown && bHasFloor && !bAFullScreenIsOpen;
}
