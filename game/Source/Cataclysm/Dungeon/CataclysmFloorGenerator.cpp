// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmFloorGenerator.h"

#include "Math/RandomStream.h"

namespace
{
	using FGen = FCataclysmFloorGenerator;

	/**
	 * The four orthogonal steps.
	 *
	 * SEPARATELY NAMED FROM THE ONE IN `CataclysmFloorPlan.cpp` on purpose.
	 * Unreal merges a module's `.cpp` files into one translation unit, so two
	 * files defining the same name in an anonymous namespace collide -- and only
	 * once both are committed, because UnrealBuildTool keeps modified files out
	 * of the merged unit. `tools/tests/test_no_two_files_share_an_anonymous_helper.py`
	 * guards it, and this is what complying with it looks like.
	 */
	const FIntPoint GenSteps[4] = {
		FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1)
	};

	/** Whether a cell is inside the grid's solid outer wall. */
	bool GenIsInsideBorder(const FCataclysmFloorPlan& Plan, FIntPoint Cell)
	{
		return Cell.X > 0 && Cell.Y > 0
			&& Cell.X < Plan.Width - 1 && Cell.Y < Plan.Height - 1;
	}

	/**
	 * Fills every walkable cell that is not part of the largest connected group.
	 *
	 * WHAT IT IS FOR. A cavern falls out of a random fill in several pieces, and
	 * a piece the player can never reach is floor they can see across a wall and
	 * never stand on. Discarding all but the biggest is what makes
	 * `FCataclysmFloorQuality::UnreachableCells` zero.
	 */
	void GenKeepLargestRegion(FCataclysmFloorPlan& Plan)
	{
		const int32 Total = Plan.Cells.Num();
		TArray<int32> Region;
		Region.Init(INDEX_NONE, Total);

		int32 BestRegion = INDEX_NONE;
		int32 BestSize = 0;
		int32 NextRegion = 0;

		TArray<int32> Frontier;
		Frontier.Reserve(Total);

		for (int32 Start = 0; Start < Total; ++Start)
		{
			if (Plan.Cells[Start] != ECataclysmFloorCell::Floor
				|| Region[Start] != INDEX_NONE)
			{
				continue;
			}

			const int32 Id = NextRegion++;
			int32 Size = 0;
			Frontier.Reset();
			Frontier.Add(Start);
			Region[Start] = Id;

			for (int32 Read = 0; Read < Frontier.Num(); ++Read)
			{
				++Size;
				const FIntPoint Here = Plan.CellAt(Frontier[Read]);
				for (const FIntPoint& Step : GenSteps)
				{
					const int32 ThereIndex = Plan.IndexOf(Here + Step);
					if (ThereIndex == INDEX_NONE
						|| Region[ThereIndex] != INDEX_NONE
						|| Plan.Cells[ThereIndex] != ECataclysmFloorCell::Floor)
					{
						continue;
					}
					Region[ThereIndex] = Id;
					Frontier.Add(ThereIndex);
				}
			}

			if (Size > BestSize)
			{
				BestSize = Size;
				BestRegion = Id;
			}
		}

		if (BestRegion == INDEX_NONE)
		{
			return;
		}

		for (int32 Index = 0; Index < Total; ++Index)
		{
			if (Plan.Cells[Index] == ECataclysmFloorCell::Floor
				&& Region[Index] != BestRegion)
			{
				Plan.Cells[Index] = ECataclysmFloorCell::Solid;
			}
		}
	}

	/**
	 * Fills walkable cells that have only one walkable neighbour, repeatedly.
	 *
	 * A cell with one neighbour is the tip of a tendril: somewhere the player can
	 * walk to and can only walk back out of. Removing one cannot break the floor
	 * apart, because nothing reaches the rest of the floor through it.
	 */
	void GenPruneDeadEnds(FCataclysmFloorPlan& Plan)
	{
		TArray<FIntPoint> Doomed;
		for (int32 Pass = 0; Pass < Plan.Width + Plan.Height; ++Pass)
		{
			Doomed.Reset();
			for (int32 Index = 0; Index < Plan.Cells.Num(); ++Index)
			{
				if (Plan.Cells[Index] != ECataclysmFloorCell::Floor)
				{
					continue;
				}
				const FIntPoint Cell = Plan.CellAt(Index);
				if (Plan.OrthogonalNeighbours(Cell) <= 1)
				{
					Doomed.Add(Cell);
				}
			}

			if (Doomed.Num() == 0)
			{
				return;
			}
			for (const FIntPoint& Cell : Doomed)
			{
				Plan.Fill(Cell);
			}
		}
	}

	/**
	 * Widens every passage that is one cell across.
	 *
	 * HOW IT AVOIDS MAKING THINGS WORSE. Widening a single cell would leave the
	 * cell it carved as a new dead end. So the whole run is decided first and
	 * carved together: a passage running east to west has the cell below each of
	 * its cells carved, which produces a second run alongside the first rather
	 * than a row of stubs. A corner has the cell diagonally outside it carved,
	 * which squares the corner off.
	 */
	void GenWidenNarrowPassages(FCataclysmFloorPlan& Plan)
	{
		TArray<FIntPoint> ToCarve;

		for (int32 Index = 0; Index < Plan.Cells.Num(); ++Index)
		{
			if (Plan.Cells[Index] != ECataclysmFloorCell::Floor)
			{
				continue;
			}

			const FIntPoint Cell = Plan.CellAt(Index);
			if (Plan.OrthogonalNeighbours(Cell) != 2)
			{
				continue;
			}

			const bool bEast = Plan.IsFloor(Cell + FIntPoint(1, 0));
			const bool bWest = Plan.IsFloor(Cell + FIntPoint(-1, 0));
			const bool bSouth = Plan.IsFloor(Cell + FIntPoint(0, 1));
			const bool bNorth = Plan.IsFloor(Cell + FIntPoint(0, -1));

			FIntPoint Widen;
			if (bEast && bWest)
			{
				Widen = Cell + FIntPoint(0, 1);
			}
			else if (bNorth && bSouth)
			{
				Widen = Cell + FIntPoint(1, 0);
			}
			else
			{
				// A corner. Carve the cell diagonally away from both openings.
				Widen = Cell + FIntPoint(bEast ? 1 : -1, bSouth ? 1 : -1);
			}

			if (GenIsInsideBorder(Plan, Widen) && !Plan.IsFloor(Widen))
			{
				ToCarve.Add(Widen);
			}
		}

		for (const FIntPoint& Cell : ToCarve)
		{
			Plan.Carve(Cell);
		}
	}

	/**
	 * Puts the entrance and the exit as far apart as the floor allows.
	 *
	 * THE TWO-SWEEP TRICK. Walk from any cell to the one furthest from it, then
	 * walk from there to the one furthest from THAT. On a floor that is one
	 * connected piece the second pair is the longest walk on the floor, or close
	 * to it. Two things fall out of it for free: the exit is reachable from the
	 * entrance by construction, and the walk between them is as long as the floor
	 * has to offer, which is what makes searching for the stairs take time.
	 *
	 * IT ASSUMES THE FLOOR IS ONE PIECE, so `GenKeepLargestRegion` must have run.
	 *
	 * @return false when there is no floor to stand on at all
	 */
	bool GenPlaceEndsFarApart(FCataclysmFloorPlan& Plan)
	{
		int32 AnyFloor = INDEX_NONE;
		for (int32 Index = 0; Index < Plan.Cells.Num(); ++Index)
		{
			if (Plan.Cells[Index] == ECataclysmFloorCell::Floor)
			{
				AnyFloor = Index;
				break;
			}
		}
		if (AnyFloor == INDEX_NONE)
		{
			return false;
		}

		auto FurthestFrom = [&Plan](FIntPoint From) -> int32
		{
			const TArray<int32> Distance = CataclysmFloorDistancesFrom(Plan, From);
			int32 Best = INDEX_NONE;
			int32 BestDistance = INDEX_NONE;
			for (int32 Index = 0; Index < Distance.Num(); ++Index)
			{
				if (Distance[Index] > BestDistance)
				{
					BestDistance = Distance[Index];
					Best = Index;
				}
			}
			return Best;
		};

		const int32 First = FurthestFrom(Plan.CellAt(AnyFloor));
		if (First == INDEX_NONE)
		{
			return false;
		}
		const int32 Second = FurthestFrom(Plan.CellAt(First));
		if (Second == INDEX_NONE)
		{
			return false;
		}

		Plan.Entrance = Plan.CellAt(First);
		Plan.Exit = Plan.CellAt(Second);
		return true;
	}

	/** Carves a rectangle. `Max` is one past the last cell, as `FIntRect` reads. */
	void GenCarveRect(FCataclysmFloorPlan& Plan, const FIntRect& Rect)
	{
		for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
		{
			for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
			{
				Plan.Carve(FIntPoint(X, Y));
			}
		}
	}

	/** The middle cell of a rectangle whose `Max` is one past the last cell. */
	FIntPoint GenCentreOf(const FIntRect& Rect)
	{
		return FIntPoint((Rect.Min.X + Rect.Max.X) / 2, (Rect.Min.Y + Rect.Max.Y) / 2);
	}

	/**
	 * Carves an L-shaped connection `Wide` cells across, from A to B.
	 *
	 * The horizontal leg runs at A's row and the vertical leg at B's column, so
	 * the two always cross and the connection is never in two pieces.
	 */
	TArray<FIntPoint> GenConnectionCells(FIntPoint A, FIntPoint B, int32 Wide)
	{
		// THE ONE LIST OF A CONNECTION'S CELLS. `GenCarveConnection` carves it and the gated shortcuts ask it which
		// cells a corridor would open, so the two cannot disagree about a connection's shape.
		TArray<FIntPoint> Cells;
		const int32 FirstX = FMath::Min(A.X, B.X);
		const int32 LastX = FMath::Max(A.X, B.X);
		for (int32 X = FirstX; X <= LastX; ++X)
		{
			for (int32 Offset = 0; Offset < Wide; ++Offset)
			{
				Cells.AddUnique(FIntPoint(X, A.Y + Offset));
			}
		}

		const int32 FirstY = FMath::Min(A.Y, B.Y);
		const int32 LastY = FMath::Max(A.Y, B.Y);
		for (int32 Y = FirstY; Y <= LastY; ++Y)
		{
			for (int32 Offset = 0; Offset < Wide; ++Offset)
			{
				Cells.AddUnique(FIntPoint(B.X + Offset, Y));
			}
		}
		return Cells;
	}

	void GenCarveConnection(FCataclysmFloorPlan& Plan, FIntPoint A, FIntPoint B, int32 Wide)
	{
		for (const FIntPoint& Cell : GenConnectionCells(A, B, Wide))
		{
			Plan.Carve(Cell);
		}
	}

	/** Whether every walkable cell can be walked to from the entrance. */
	bool GenEveryCellIsReachable(const FCataclysmFloorPlan& Plan)
	{
		const TArray<int32> Distance = CataclysmFloorDistancesFrom(Plan, Plan.Entrance);
		for (int32 Index = 0; Index < Plan.Cells.Num(); ++Index)
		{
			if (Plan.Cells[Index] == ECataclysmFloorCell::Floor && Distance[Index] == INDEX_NONE)
			{
				return false;
			}
		}
		return true;
	}

	/**
	 * Whether a corridor from A to B is a gated shortcut for the walk From -> To, which is `Base` cells today. Fills
	 * `Out` when it is. See `FCataclysmFloorGenerator::FindShortcutBetween`.
	 */
	bool GenCheckShortcut(const FCataclysmFloorPlan& Plan, FIntPoint A, FIntPoint B, FIntPoint From, FIntPoint To,
						  int32 Base, const TSet<FIntPoint>& Avoid, FCataclysmFloorShortcut& Out)
	{
		TArray<FIntPoint> NewCells;
		for (const FIntPoint& Cell : GenConnectionCells(A, B, FCataclysmFloorGenerator::ShortcutWidth))
		{
			if (Plan.IndexOf(Cell) == INDEX_NONE)
			{
				// OFF THE GRID: a corridor that would run off the plan is not carved at all.
				return false;
			}
			if (!Plan.IsFloor(Cell))
			{
				if (Avoid.Contains(Cell))
				{
					return false;
				}
				NewCells.Add(Cell);
			}
		}
		if (NewCells.Num() < 2)
		{
			return false;
		}

		FCataclysmFloorPlan Carved = Plan;
		for (const FIntPoint& Cell : NewCells)
		{
			Carved.Carve(Cell);
		}
		const int32 Saving = Base - CataclysmFloorDistancesFrom(Carved, From)[Carved.IndexOf(To)];
		if (Saving < FCataclysmFloorGenerator::ShortcutLeastSaving)
		{
			return false;
		}

		// A GATE OF TWO NEW CELLS, SIDE BY SIDE, whose closing gives the first walk back and strands nothing.
		for (const FIntPoint& Cell : NewCells)
		{
			for (const FIntPoint& Across : {FIntPoint(1, 0), FIntPoint(0, 1)})
			{
				const FIntPoint Other = Cell + Across;
				if (!NewCells.Contains(Other))
				{
					continue;
				}
				FCataclysmFloorPlan Closed = Carved;
				Closed.Cells[Closed.IndexOf(Cell)] = ECataclysmFloorCell::Solid;
				Closed.Cells[Closed.IndexOf(Other)] = ECataclysmFloorCell::Solid;
				if (CataclysmFloorDistancesFrom(Closed, From)[Closed.IndexOf(To)] == Base
					&& GenEveryCellIsReachable(Closed))
				{
					Out.A = A;
					Out.B = B;
					Out.NewCells = NewCells;
					Out.Gate = {Cell, Other};
					Out.Saving = Saving;
					return true;
				}
			}
		}
		return false;
	}

	/** One end, the other, and what the two searches say the corridor would save. */
	struct FGenShortcutCandidate
	{
		FIntPoint A;
		FIntPoint B;
		int32 Walk = 0;
		int32 Estimate = 0;
	};

	/** Best estimate first; ties by the cells themselves, so the same plan always gives the same order. */
	void GenSortShortcutCandidates(TArray<FGenShortcutCandidate>& Candidates)
	{
		Candidates.Sort([](const FGenShortcutCandidate& One, const FGenShortcutCandidate& Two)
		{
			if (One.Estimate != Two.Estimate)
			{
				return One.Estimate > Two.Estimate;
			}
			if (One.A != Two.A)
			{
				return One.A.Y != Two.A.Y ? One.A.Y < Two.A.Y : One.A.X < Two.A.X;
			}
			return One.B.Y != Two.B.Y ? One.B.Y < Two.B.Y : One.B.X < Two.B.X;
		});
	}

	/** Large rectangular rooms joined by connections two cells across. */
	void GenCarveHalls(FCataclysmFloorPlan& Plan, FRandomStream& Stream,
					   const FCataclysmFloorShape& Shape)
	{
		// Split the grid in half repeatedly, stopping when neither half would
		// still be MinLeafSide across. The split point is anywhere that leaves
		// both halves big enough, so the rooms are not all the same size.
		TArray<FIntRect> Leaves;
		Leaves.Add(FIntRect(FIntPoint(1, 1),
							FIntPoint(Plan.Width - 1, Plan.Height - 1)));

		bool bSplitSomething = true;
		while (bSplitSomething)
		{
			bSplitSomething = false;
			TArray<FIntRect> Next;
			Next.Reserve(Leaves.Num() * 2);

			for (const FIntRect& Leaf : Leaves)
			{
				const int32 LeafWidth = Leaf.Max.X - Leaf.Min.X;
				const int32 LeafHeight = Leaf.Max.Y - Leaf.Min.Y;
				const bool bCanSplitX = LeafWidth >= 2 * Shape.MinLeafSide;
				const bool bCanSplitY = LeafHeight >= 2 * Shape.MinLeafSide;

				if (!bCanSplitX && !bCanSplitY)
				{
					Next.Add(Leaf);
					continue;
				}

				bool bVertical;
				if (bCanSplitX && bCanSplitY)
				{
					bVertical = (LeafWidth != LeafHeight)
						? (LeafWidth > LeafHeight)
						: (Stream.RandRange(0, 1) == 0);
				}
				else
				{
					bVertical = bCanSplitX;
				}

				if (bVertical)
				{
					const int32 At = Leaf.Min.X + Shape.MinLeafSide
						+ Stream.RandRange(0, LeafWidth - 2 * Shape.MinLeafSide);
					Next.Add(FIntRect(Leaf.Min, FIntPoint(At, Leaf.Max.Y)));
					Next.Add(FIntRect(FIntPoint(At, Leaf.Min.Y), Leaf.Max));
				}
				else
				{
					const int32 At = Leaf.Min.Y + Shape.MinLeafSide
						+ Stream.RandRange(0, LeafHeight - 2 * Shape.MinLeafSide);
					Next.Add(FIntRect(Leaf.Min, FIntPoint(Leaf.Max.X, At)));
					Next.Add(FIntRect(FIntPoint(Leaf.Min.X, At), Leaf.Max));
				}
				bSplitSomething = true;
			}

			Leaves = MoveTemp(Next);
		}

		// One room per leaf, inset so that two rooms never share a wall, then
		// shrunk a little at random and never below MinRoomSide.
		TArray<FIntRect> Rooms;
		Rooms.Reserve(Leaves.Num());

		for (const FIntRect& Leaf : Leaves)
		{
			FIntRect Room(Leaf.Min + FIntPoint(1, 1), Leaf.Max - FIntPoint(1, 1));

			const int32 RoomWidth = Room.Max.X - Room.Min.X;
			const int32 RoomHeight = Room.Max.Y - Room.Min.Y;
			if (RoomWidth < FGen::MinRoomSide || RoomHeight < FGen::MinRoomSide)
			{
				continue;
			}

			const int32 ShrinkX = Stream.RandRange(0,
				FMath::Min(Shape.MostRoomShrink, RoomWidth - FGen::MinRoomSide));
			const int32 ShrinkY = Stream.RandRange(0,
				FMath::Min(Shape.MostRoomShrink, RoomHeight - FGen::MinRoomSide));
			const int32 LeftOf = Stream.RandRange(0, ShrinkX);
			const int32 TopOf = Stream.RandRange(0, ShrinkY);

			Room.Min.X += LeftOf;
			Room.Max.X -= (ShrinkX - LeftOf);
			Room.Min.Y += TopOf;
			Room.Max.Y -= (ShrinkY - TopOf);

			Rooms.Add(Room);
			GenCarveRect(Plan, Room);
		}

		if (Rooms.Num() <= 1)
		{
			return;
		}

		TArray<FIntPoint> Centres;
		Centres.Reserve(Rooms.Num());
		for (const FIntRect& Room : Rooms)
		{
			Centres.Add(GenCentreOf(Room));
		}

		auto Apart = [&Centres](int32 A, int32 B) -> int32
		{
			return FMath::Abs(Centres[A].X - Centres[B].X)
				+ FMath::Abs(Centres[A].Y - Centres[B].Y);
		};

		// The connections that make the floor one piece: grow a tree outwards,
		// always joining the nearest room that is not in it yet.
		const int32 RoomCount = Rooms.Num();
		TArray<bool> Joined;
		Joined.Init(false, RoomCount);
		Joined[0] = true;

		TArray<TPair<int32, int32>> Connections;
		for (int32 Added = 1; Added < RoomCount; ++Added)
		{
			int32 BestFrom = INDEX_NONE;
			int32 BestTo = INDEX_NONE;
			int32 BestApart = MAX_int32;

			for (int32 From = 0; From < RoomCount; ++From)
			{
				if (!Joined[From])
				{
					continue;
				}
				for (int32 To = 0; To < RoomCount; ++To)
				{
					if (Joined[To])
					{
						continue;
					}
					const int32 Distance = Apart(From, To);
					if (Distance < BestApart)
					{
						BestApart = Distance;
						BestFrom = From;
						BestTo = To;
					}
				}
			}

			if (BestTo == INDEX_NONE)
			{
				break;
			}
			Joined[BestTo] = true;
			Connections.Add(TPair<int32, int32>(BestFrom, BestTo));
		}

		// The connections that close loops, so a side room is not a trip out and
		// back. Each joins a random room to its nearest room it is not already
		// joined to.
		for (int32 Extra = 0; Extra < Shape.ExtraConnections; ++Extra)
		{
			const int32 From = Stream.RandRange(0, RoomCount - 1);
			int32 BestTo = INDEX_NONE;
			int32 BestApart = MAX_int32;

			for (int32 To = 0; To < RoomCount; ++To)
			{
				if (To == From)
				{
					continue;
				}
				const bool bAlready = Connections.ContainsByPredicate(
					[From, To](const TPair<int32, int32>& Pair)
					{
						return (Pair.Key == From && Pair.Value == To)
							|| (Pair.Key == To && Pair.Value == From);
					});
				if (bAlready)
				{
					continue;
				}
				const int32 Distance = Apart(From, To);
				if (Distance < BestApart)
				{
					BestApart = Distance;
					BestTo = To;
				}
			}

			if (BestTo != INDEX_NONE)
			{
				Connections.Add(TPair<int32, int32>(From, BestTo));
			}
		}

		for (const TPair<int32, int32>& Pair : Connections)
		{
			GenCarveConnection(Plan, Centres[Pair.Key], Centres[Pair.Value],
							   Shape.ConnectionWidth);
		}
	}

	/** Rounded chambers with no straight walls. */
	void GenCarveCaverns(FCataclysmFloorPlan& Plan, FRandomStream& Stream,
						 const FCataclysmFloorShape& Shape)
	{
		for (int32 Y = 1; Y < Plan.Height - 1; ++Y)
		{
			for (int32 X = 1; X < Plan.Width - 1; ++X)
			{
				if (Stream.FRand() < Shape.CavernInitialFloorChance)
				{
					Plan.Carve(FIntPoint(X, Y));
				}
			}
		}

		for (int32 Pass = 0; Pass < Shape.CavernSmoothingPasses; ++Pass)
		{
			TArray<ECataclysmFloorCell> Next = Plan.Cells;
			for (int32 Y = 1; Y < Plan.Height - 1; ++Y)
			{
				for (int32 X = 1; X < Plan.Width - 1; ++X)
				{
					int32 Solid = 0;
					for (int32 DY = -1; DY <= 1; ++DY)
					{
						for (int32 DX = -1; DX <= 1; ++DX)
						{
							if (DX == 0 && DY == 0)
							{
								continue;
							}
							Solid += Plan.IsFloor(FIntPoint(X + DX, Y + DY)) ? 0 : 1;
						}
					}
					Next[Y * Plan.Width + X] = (Solid >= Shape.CavernSolidNeighboursToFill)
						? ECataclysmFloorCell::Solid
						: ECataclysmFloorCell::Floor;
				}
			}
			Plan.Cells = MoveTemp(Next);
		}

		// Tendrils first, then widen what is left. Order matters: widening a
		// tendril would lengthen it rather than remove it. `Generate` prunes
		// again afterwards, for every layout, so anything the widening left
		// behind goes with it.
		GenPruneDeadEnds(Plan);
		GenWidenNarrowPassages(Plan);
	}

	/** One open space, wobbled so that two seeds are not the same arena. */
	void GenCarveArena(FCataclysmFloorPlan& Plan, FRandomStream& Stream,
					   const FCataclysmFloorShape& Shape)
	{
		const float CentreX = (Plan.Width - 1) * 0.5f;
		const float CentreY = (Plan.Height - 1) * 0.5f;
		const float RadiusX = FMath::Max(1.0f, CentreX - 1.0f);
		const float RadiusY = FMath::Max(1.0f, CentreY - 1.0f);

		const float Phase = Stream.FRand() * 2.0f * PI;
		const int32 Lobes = Stream.RandRange(3, 6);
		const float Wobble = 0.06f + Stream.FRand() * 0.06f;
		const float ScaleX = Shape.ArenaScaleX;
		const float ScaleY = Shape.ArenaScaleY;

		for (int32 Y = 1; Y < Plan.Height - 1; ++Y)
		{
			for (int32 X = 1; X < Plan.Width - 1; ++X)
			{
				const float OffsetX = (X - CentreX) / (RadiusX * ScaleX);
				const float OffsetY = (Y - CentreY) / (RadiusY * ScaleY);
				const float Radius = FMath::Sqrt(OffsetX * OffsetX + OffsetY * OffsetY);
				const float Angle = FMath::Atan2(OffsetY, OffsetX);
				const float Edge = 1.0f + Wobble * FMath::Sin(Lobes * Angle + Phase);
				if (Radius <= Edge)
				{
					Plan.Carve(FIntPoint(X, Y));
				}
			}
		}
	}

	// ------------------------------------------------------------------------------------------------------------
	// Sections. See `FCataclysmFloorGenerator::FindSections`, whose comment states the rules these follow.
	// ------------------------------------------------------------------------------------------------------------

	/** Lines that, closed together, leave exactly two areas with the entrance in one and the exit in the other. */
	struct FGenSectionBoundary
	{
		TArray<FIntPoint> Cells;

		/** The same cells, line by line, the lower-numbered line first. */
		TArray<TArray<FIntPoint>> Lines;

		/** The lowest of `Cells`, comparing Y and then X. */
		FIntPoint First = FIntPoint(-1, -1);

		/** How many cells are in the entrance's area and in the exit's, with this boundary alone closed. */
		int32 EntranceSide = 0;
		int32 ExitSide = 0;

		/** The area of every cell with this boundary alone closed, and which of the two holds the entrance. */
		TArray<int32> AreaOf;
		int32 EntranceArea = INDEX_NONE;
	};

	/** Whether `One` is the lower cell: the lower Y, and on the same row the lower X. */
	bool GenSectionCellIsLower(FIntPoint One, FIntPoint Two)
	{
		return (One.Y != Two.Y) ? (One.Y < Two.Y) : (One.X < Two.X);
	}

	/** The lowest of some cells. The list must not be empty. */
	FIntPoint GenSectionLowestCell(const TArray<FIntPoint>& Cells)
	{
		FIntPoint Lowest = Cells[0];
		for (const FIntPoint& Cell : Cells)
		{
			if (GenSectionCellIsLower(Cell, Lowest))
			{
				Lowest = Cell;
			}
		}
		return Lowest;
	}

	/**
	 * The connected areas of a plan's walkable cells: which area every cell is in, `INDEX_NONE` for rock, and how many
	 * cells each area holds. Areas are numbered in the order their first cell is met, row by row.
	 */
	void GenSectionAreas(const FCataclysmFloorPlan& Plan, TArray<int32>& AreaOf, TArray<int32>& Sizes)
	{
		const int32 Total = Plan.Cells.Num();
		AreaOf.Init(INDEX_NONE, Total);
		Sizes.Reset();

		TArray<int32> Frontier;
		Frontier.Reserve(Total);

		for (int32 Start = 0; Start < Total; ++Start)
		{
			if (Plan.Cells[Start] != ECataclysmFloorCell::Floor || AreaOf[Start] != INDEX_NONE)
			{
				continue;
			}

			const int32 Id = Sizes.Num();
			int32 Size = 0;
			Frontier.Reset();
			Frontier.Add(Start);
			AreaOf[Start] = Id;

			for (int32 Read = 0; Read < Frontier.Num(); ++Read)
			{
				++Size;
				const FIntPoint Here = Plan.CellAt(Frontier[Read]);
				for (const FIntPoint& Step : GenSteps)
				{
					const int32 ThereIndex = Plan.IndexOf(Here + Step);
					if (ThereIndex == INDEX_NONE
						|| AreaOf[ThereIndex] != INDEX_NONE
						|| Plan.Cells[ThereIndex] != ECataclysmFloorCell::Floor)
					{
						continue;
					}
					AreaOf[ThereIndex] = Id;
					Frontier.Add(ThereIndex);
				}
			}
			Sizes.Add(Size);
		}
	}

	/**
	 * Whether closing `One.Cells` is a boundary, filling the rest of `One` when it is. `Work` is the plan; its cells
	 * are closed, asked about and opened again, so it leaves as it came. `Sizes` is scratch space.
	 */
	bool GenSectionIsBoundary(FCataclysmFloorPlan& Work, int32 LeastArea, FGenSectionBoundary& One, TArray<int32>& Sizes)
	{
		for (const FIntPoint& Cell : One.Cells)
		{
			Work.Fill(Cell);
		}
		GenSectionAreas(Work, One.AreaOf, Sizes);
		for (const FIntPoint& Cell : One.Cells)
		{
			Work.Carve(Cell);
		}

		// EXACTLY TWO AREAS, EACH A TENTH OR MORE, THE ENTRANCE IN ONE AND THE EXIT IN THE OTHER.
		if (Sizes.Num() != 2)
		{
			return false;
		}
		if (Sizes[0] < LeastArea || Sizes[1] < LeastArea)
		{
			return false;
		}
		One.EntranceArea = One.AreaOf[Work.IndexOf(Work.Entrance)];
		if (One.EntranceArea == One.AreaOf[Work.IndexOf(Work.Exit)])
		{
			return false;
		}
		One.EntranceSide = Sizes[One.EntranceArea];
		One.ExitSide = Sizes[1 - One.EntranceArea];
		One.First = GenSectionLowestCell(One.Cells);
		return true;
	}

	/**
	 * The section number of every cell from its area: 0 for the entrance's area, the last for the exit's, 1 for a
	 * third area, and `INDEX_NONE` where the cell is in no area.
	 */
	TArray<int32> GenSectionNumbers(const TArray<int32>& AreaOf, int32 EntranceArea, int32 ExitArea, int32 Sections)
	{
		TArray<int32> Numbers;
		Numbers.Init(INDEX_NONE, AreaOf.Num());
		for (int32 Index = 0; Index < AreaOf.Num(); ++Index)
		{
			if (AreaOf[Index] != INDEX_NONE)
			{
				Numbers[Index] = (AreaOf[Index] == EntranceArea) ? 0 : (AreaOf[Index] == ExitArea) ? Sections - 1 : 1;
			}
		}
		return Numbers;
	}

	/** The areas of `Work` with these cells closed. Every one of them must be walkable; `Work` leaves as it came. */
	void GenSectionAreasWithout(FCataclysmFloorPlan& Work, const TArray<FIntPoint>& Shut, TArray<int32>& AreaOf,
								TArray<int32>& Sizes)
	{
		for (const FIntPoint& Cell : Shut)
		{
			Work.Fill(Cell);
		}
		GenSectionAreas(Work, AreaOf, Sizes);
		for (const FIntPoint& Cell : Shut)
		{
			Work.Carve(Cell);
		}
	}

	/**
	 * Whether an answer that holds with the may-close cells open also holds with them closed. Ruled 2026-10-08.
	 *
	 * `InOrder` is the answer's boundaries, boundary 0 first, and `OpenNumbers` every cell's section number with the
	 * cells open. `Closable` is the may-close cells that are walkable in the plan. With them and every boundary
	 * closed there must be as many areas as sections, each `LeastArea` cells or more, the entrance and the exit in
	 * different ones, and every cell still walkable must be in the section it was in. Then, for three sections, each
	 * boundary alone is closed with them: the entrance must reach exactly the cells of the sections before that
	 * boundary. That is asked for as well as the numbers because the numbers alone do not show it: a middle section
	 * could keep its number and still be reached from the entrance's section only through a gate.
	 */
	bool GenSectionHoldsWithCellsClosed(FCataclysmFloorPlan& Work, const TArray<TArray<FIntPoint>>& InOrder,
										const TArray<FIntPoint>& Closable, const TArray<int32>& OpenNumbers,
										int32 LeastArea)
	{
		const int32 Sections = InOrder.Num() + 1;
		const int32 EntranceIndex = Work.IndexOf(Work.Entrance);
		const int32 ExitIndex = Work.IndexOf(Work.Exit);

		TArray<FIntPoint> Shut = Closable;
		for (const TArray<FIntPoint>& Boundary : InOrder)
		{
			Shut.Append(Boundary);
		}
		TArray<int32> AreaOf;
		TArray<int32> Sizes;
		GenSectionAreasWithout(Work, Shut, AreaOf, Sizes);
		if (Sizes.Num() != Sections)
		{
			return false;
		}
		for (const int32 Size : Sizes)
		{
			if (Size < LeastArea)
			{
				return false;
			}
		}
		const int32 EntranceArea = AreaOf[EntranceIndex];
		const int32 ExitArea = AreaOf[ExitIndex];
		if (EntranceArea == INDEX_NONE || ExitArea == INDEX_NONE || EntranceArea == ExitArea)
		{
			return false;
		}
		const TArray<int32> ClosedNumbers = GenSectionNumbers(AreaOf, EntranceArea, ExitArea, Sections);
		for (int32 Index = 0; Index < ClosedNumbers.Num(); ++Index)
		{
			if (ClosedNumbers[Index] != INDEX_NONE && ClosedNumbers[Index] != OpenNumbers[Index])
			{
				return false;
			}
		}

		// EACH BOUNDARY ALONE, for three sections. With two there is one boundary and the question above was this one.
		for (int32 Which = 0; Which < InOrder.Num() && Sections == 3; ++Which)
		{
			Shut = Closable;
			Shut.Append(InOrder[Which]);
			GenSectionAreasWithout(Work, Shut, AreaOf, Sizes);
			const int32 ReachedArea = AreaOf[EntranceIndex];
			for (int32 Index = 0; Index < AreaOf.Num(); ++Index)
			{
				if (AreaOf[Index] == INDEX_NONE || OpenNumbers[Index] == INDEX_NONE)
				{
					continue;
				}
				if ((AreaOf[Index] == ReachedArea) != (OpenNumbers[Index] <= Which))
				{
					return false;
				}
			}
		}
		return true;
	}

	/**
	 * What the one search for the fewest lines between the near part of a floor and the far part works with.
	 *
	 * PORTED FROM THE MEASUREMENT OF 2026-10-08, AND THE ORDER IN IT IS PART OF THE ANSWER. The search stops at a
	 * limit, so which separations it has found by then depends on the order cells are walked and lines are tried.
	 * The figures recorded for 20 Halls floors are the figures of this order: lines numbered as they are met, cell by
	 * cell in index order and along X before along Y; the walk started from every near cell in index order and
	 * stepping in `GenSteps`' order; a cell's first line tried before its second.
	 */
	struct FGenSectionSearch
	{
		const FCataclysmFloorPlan* Plan = nullptr;

		/** Every line of the plan, numbered in the order it was met. */
		TArray<TArray<FIntPoint>> Lines;

		/** For every cell, the first line met that holds it and the second, or `INDEX_NONE`. A cell is in two at most. */
		TArray<int32> FirstLineOfCell;
		TArray<int32> SecondLineOfCell;

		/** For every cell, whether it is in the near part and whether it is in the far part. */
		TArray<bool> bInNear;
		TArray<bool> bInFar;

		/** For every line, whether it may be closed: none of its cells is in the near part or the far part. */
		TArray<bool> bLineMayClose;

		/** Every set of lines found with no walk from the near part to the far part, each sorted by line number. */
		TArray<TArray<int32>> Separations;

		/** Every set of lines already walked with, so that none is walked with twice. See `GenSectionKeyOfLines`. */
		TSet<int64> Tried;

		int32 Walks = 0;
		bool bReachedTheLimit = false;
	};

	/**
	 * Finds every line of the plan and which lines each cell is in.
	 *
	 * CELL BY CELL IN INDEX ORDER, AND ALONG X BEFORE ALONG Y FROM ONE CELL. A line starts at a walkable cell whose
	 * neighbour before it is not walkable. It is a line when it is `SectionLineLeastCells` to `SectionLineMostCells`
	 * cells, none of them the entrance or the exit, and it is not left out.
	 */
	void GenSectionFindLines(const FCataclysmFloorPlan& Plan, FGenSectionSearch& Search)
	{
		const int32 Total = Plan.Cells.Num();
		Search.FirstLineOfCell.Init(INDEX_NONE, Total);
		Search.SecondLineOfCell.Init(INDEX_NONE, Total);
		for (int32 Index = 0; Index < Total; ++Index)
		{
			const FIntPoint Cell = Plan.CellAt(Index);
			if (!Plan.IsFloor(Cell))
			{
				continue;
			}
			for (const FIntPoint& Along : {FIntPoint(1, 0), FIntPoint(0, 1)})
			{
				if (Plan.IsFloor(Cell - Along))
				{
					continue;
				}
				int32 Length = 0;
				while (Length <= FGen::SectionLineMostCells && Plan.IsFloor(Cell + Along * Length))
				{
					++Length;
				}
				if (Length < FGen::SectionLineLeastCells || Length > FGen::SectionLineMostCells)
				{
					continue;
				}
				TArray<FIntPoint> Line;
				bool bUsable = true;
				for (int32 Offset = 0; Offset < Length; ++Offset)
				{
					const FIntPoint OnTheLine = Cell + Along * Offset;
					bUsable = bUsable && OnTheLine != Plan.Entrance && OnTheLine != Plan.Exit;
					Line.Add(OnTheLine);
				}

				// LEFT OUT WHEN THE SAME LINE LIES ON BOTH ITS SIDES: shifted one cell sideways each way it is a row of
				// the same length, all walkable, with a cell that is not walkable at each end. The inside of a corridor.
				const FIntPoint Across(Along.Y, Along.X);
				bool bSameOnBothSides = true;
				for (const int32 Side : {-1, 1})
				{
					bool bSame = !Plan.IsFloor(Cell - Along + Across * Side)
						&& !Plan.IsFloor(Cell + Along * Length + Across * Side);
					for (const FIntPoint& OnTheLine : Line)
					{
						bSame = bSame && Plan.IsFloor(OnTheLine + Across * Side);
					}
					bSameOnBothSides = bSameOnBothSides && bSame;
				}
				if (!bUsable || bSameOnBothSides)
				{
					continue;
				}
				const int32 LineId = Search.Lines.Add(Line);
				for (const FIntPoint& OnTheLine : Line)
				{
					const int32 CellIndex = Plan.IndexOf(OnTheLine);
					if (Search.FirstLineOfCell[CellIndex] == INDEX_NONE)
					{
						Search.FirstLineOfCell[CellIndex] = LineId;
					}
					else
					{
						Search.SecondLineOfCell[CellIndex] = LineId;
					}
				}
			}
		}
	}

	/**
	 * Marks the near part and the far part of the plan, and which lines may be closed.
	 *
	 * NEAR is every walkable cell with fewer than one in `SectionLeastAreaOneIn` of the walkable cells nearer the
	 * entrance, walked, than it. FAR is every cell with all but one in that many nearer, and the exit whatever its
	 * distance. A cell that cannot be walked to is in neither.
	 */
	void GenSectionMarkNearAndFar(const FCataclysmFloorPlan& Plan, FGenSectionSearch& Search)
	{
		const int32 Total = Plan.Cells.Num();
		const int32 Walkable = Plan.FloorCount();
		const int32 ExitIndex = Plan.IndexOf(Plan.Exit);
		const TArray<int32> FromEntrance = CataclysmFloorDistancesFrom(Plan, Plan.Entrance);

		// `Nearer[D]` IS HOW MANY WALKABLE CELLS ARE LESS THAN `D` FROM THE ENTRANCE.
		int32 Furthest = 0;
		for (const int32 Away : FromEntrance)
		{
			Furthest = FMath::Max(Furthest, Away);
		}
		TArray<int32> Nearer;
		Nearer.Init(0, Furthest + 2);
		for (int32 Index = 0; Index < Total; ++Index)
		{
			if (Plan.Cells[Index] == ECataclysmFloorCell::Floor && FromEntrance[Index] != INDEX_NONE)
			{
				++Nearer[FromEntrance[Index] + 1];
			}
		}
		for (int32 Index = 1; Index < Nearer.Num(); ++Index)
		{
			Nearer[Index] += Nearer[Index - 1];
		}

		Search.bInNear.Init(false, Total);
		Search.bInFar.Init(false, Total);
		for (int32 Index = 0; Index < Total; ++Index)
		{
			if (Plan.Cells[Index] != ECataclysmFloorCell::Floor || FromEntrance[Index] == INDEX_NONE)
			{
				continue;
			}
			const int32 Before = Nearer[FromEntrance[Index]];
			Search.bInNear[Index] = Before * FGen::SectionLeastAreaOneIn < Walkable;
			Search.bInFar[Index] = Before * FGen::SectionLeastAreaOneIn >= (FGen::SectionLeastAreaOneIn - 1) * Walkable
				|| Index == ExitIndex;
		}

		Search.bLineMayClose.Init(true, Search.Lines.Num());
		for (int32 LineId = 0; LineId < Search.Lines.Num(); ++LineId)
		{
			for (const FIntPoint& OnTheLine : Search.Lines[LineId])
			{
				const int32 CellIndex = Plan.IndexOf(OnTheLine);
				if (Search.bInNear[CellIndex] || Search.bInFar[CellIndex])
				{
					Search.bLineMayClose[LineId] = false;
				}
			}
		}
	}

	/**
	 * One number for a set of lines sorted by line number, the same for the same set. Each line takes eleven bits, so
	 * it tells sets apart on any plan of fewer than 2,047 lines; three lines fit an `int64` many times over.
	 */
	int64 GenSectionKeyOfLines(const TArray<int32>& SortedIds)
	{
		int64 Key = 0;
		for (const int32 Id : SortedIds)
		{
			Key = Key * 2048 + (Id + 1);
		}
		return Key;
	}

	/** How many distinct cells these lines hold between them. */
	int32 GenSectionCellsOfLines(const FGenSectionSearch& Search, const TArray<int32>& Ids)
	{
		TArray<FIntPoint> Cells;
		for (const int32 Id : Ids)
		{
			for (const FIntPoint& Cell : Search.Lines[Id])
			{
				Cells.AddUnique(Cell);
			}
		}
		return Cells.Num();
	}

	/**
	 * With these lines closed, walks from the near part toward the far part. No walk: the lines are a separation. A
	 * walk: every separation must close one of the lines that walk crosses, so each is tried in turn, to
	 * `SectionBoundaryMostLines` lines. Calls itself, three deep at most.
	 */
	void GenSectionWalk(FGenSectionSearch& Search, const TArray<int32>& Closed)
	{
		if (Search.Walks >= FGen::SectionMostWalks)
		{
			Search.bReachedTheLimit = true;
			return;
		}
		++Search.Walks;

		const FCataclysmFloorPlan& Plan = *Search.Plan;
		const int32 Total = Plan.Cells.Num();
		TArray<bool> bBlocked;
		bBlocked.Init(false, Total);
		for (const int32 Id : Closed)
		{
			for (const FIntPoint& Cell : Search.Lines[Id])
			{
				bBlocked[Plan.IndexOf(Cell)] = true;
			}
		}

		// A BREADTH-FIRST WALK FROM EVERY NEAR CELL AT ONCE, in index order. `Parent` is -1 for a cell not reached, -2
		// for a cell the walk started on, and otherwise the cell it was reached from.
		TArray<int32> Parent;
		Parent.Init(-1, Total);
		TArray<int32> Queue;
		Queue.Reserve(Total);
		for (int32 Index = 0; Index < Total; ++Index)
		{
			if (Search.bInNear[Index] && !bBlocked[Index])
			{
				Parent[Index] = -2;
				Queue.Add(Index);
			}
		}
		int32 Reached = -1;
		for (int32 Read = 0; Read < Queue.Num() && Reached == -1; ++Read)
		{
			const int32 Here = Queue[Read];
			if (Search.bInFar[Here])
			{
				Reached = Here;
				break;
			}
			const FIntPoint HereCell = Plan.CellAt(Here);
			for (const FIntPoint& Step : GenSteps)
			{
				const int32 There = Plan.IndexOf(HereCell + Step);
				if (There == INDEX_NONE || Plan.Cells[There] != ECataclysmFloorCell::Floor || bBlocked[There]
					|| Parent[There] != -1)
				{
					continue;
				}
				Parent[There] = Here;
				Queue.Add(There);
			}
		}
		if (Reached == -1)
		{
			if (!Closed.IsEmpty())
			{
				Search.Separations.Add(Closed);
			}
			return;
		}
		if (Closed.Num() >= FGen::SectionBoundaryMostLines)
		{
			return;
		}

		// THE LINES THE WALK CROSSES THAT MAY BE CLOSED, from the far cell back to where the walk started, a cell's
		// first line before its second.
		TArray<int32> Crossed;
		for (int32 At = Reached; At >= 0; At = Parent[At])
		{
			for (const int32 Id : {Search.FirstLineOfCell[At], Search.SecondLineOfCell[At]})
			{
				if (Id != INDEX_NONE && Search.bLineMayClose[Id])
				{
					Crossed.AddUnique(Id);
				}
			}
		}
		for (const int32 Id : Crossed)
		{
			TArray<int32> Next = Closed;
			Next.Add(Id);
			Next.Sort();
			if (GenSectionCellsOfLines(Search, Next) > FGen::SectionBoundaryMostCells)
			{
				continue;
			}
			const int64 Key = GenSectionKeyOfLines(Next);
			if (Search.Tried.Contains(Key))
			{
				continue;
			}
			Search.Tried.Add(Key);
			GenSectionWalk(Search, Next);
		}
	}

	/**
	 * The boundaries among the separations the search found, in the order they are met.
	 *
	 * THE SMALLEST SEPARATIONS FIRST, AND NONE THAT HOLDS A SMALLER ONE, so no line is closed that parts nothing. Each
	 * is then a boundary or it is not, by `GenSectionIsBoundary`. A boundary is left out when one already taken has
	 * the same entrance side, the same lowest cell and as many cells: it parts the plan in the same place.
	 *
	 * THE SORT IS BY THE NUMBER OF LINES ALONE AND `TArray::Sort` IS NOT STABLE. It is the same for the same input,
	 * so the same plan still gives the same answer. The order it leaves separations of one size in is the order the
	 * measured figures came from, and a tie-break added here would change which of two like boundaries is kept.
	 */
	TArray<FGenSectionBoundary> GenSectionBoundariesFound(FCataclysmFloorPlan& Work, FGenSectionSearch& Search,
														  int32 LeastArea)
	{
		Search.Separations.Sort([](const TArray<int32>& One, const TArray<int32>& Two) { return One.Num() < Two.Num(); });
		TArray<TArray<int32>> Smallest;
		for (const TArray<int32>& Separation : Search.Separations)
		{
			bool bHoldsASmaller = false;
			for (const TArray<int32>& Had : Smallest)
			{
				bool bAllIn = true;
				for (const int32 Id : Had)
				{
					bAllIn = bAllIn && Separation.Contains(Id);
				}
				bHoldsASmaller = bHoldsASmaller || bAllIn;
			}
			if (!bHoldsASmaller)
			{
				Smallest.Add(Separation);
			}
		}

		TArray<FGenSectionBoundary> Found;
		TArray<int32> Sizes;
		for (const TArray<int32>& Separation : Smallest)
		{
			FGenSectionBoundary One;
			for (const int32 Id : Separation)
			{
				One.Lines.Add(Search.Lines[Id]);
				for (const FIntPoint& OnTheLine : Search.Lines[Id])
				{
					One.Cells.AddUnique(OnTheLine);
				}
			}
			if (!GenSectionIsBoundary(Work, LeastArea, One, Sizes))
			{
				continue;
			}
			bool bHadIt = false;
			for (const FGenSectionBoundary& Had : Found)
			{
				bHadIt = bHadIt || (Had.EntranceSide == One.EntranceSide && Had.First == One.First
					&& Had.Cells.Num() == One.Cells.Num());
			}
			if (!bHadIt)
			{
				Found.Add(MoveTemp(One));
			}
		}
		return Found;
	}

	/**
	 * Sorts boundaries by how near the entrance's side is to `Numerator` over `Denominator` of the walkable cells,
	 * then by the fewer cells closed, then by the lower lowest cell, and keeps the first `SectionBoundariesKept`.
	 * Two boundaries cannot tie on all three and be different in size, lowest cell and count, because such a pair
	 * was not both taken; two that tie with entrance sides equally far either side of the share are left in the
	 * order the sort gives, which is the same for the same input.
	 */
	void GenSectionKeepNearest(TArray<FGenSectionBoundary>& Boundaries, int32 Walkable, int32 Numerator,
							   int32 Denominator)
	{
		Boundaries.Sort([Walkable, Numerator, Denominator](const FGenSectionBoundary& One, const FGenSectionBoundary& Two)
		{
			const int32 OffOne = FMath::Abs(One.EntranceSide * Denominator - Numerator * Walkable);
			const int32 OffTwo = FMath::Abs(Two.EntranceSide * Denominator - Numerator * Walkable);
			if (OffOne != OffTwo)
			{
				return OffOne < OffTwo;
			}
			if (One.Cells.Num() != Two.Cells.Num())
			{
				return One.Cells.Num() < Two.Cells.Num();
			}
			return GenSectionCellIsLower(One.First, Two.First);
		});
		if (Boundaries.Num() > FGen::SectionBoundariesKept)
		{
			Boundaries.SetNum(FGen::SectionBoundariesKept);
		}
	}
}

FCataclysmFloorShape FCataclysmFloorGenerator::RollShape(
	FRandomStream& Stream, const FCataclysmFloorRequest& Request)
{
	FCataclysmFloorShape Shape;

	// SIZE FIRST, AND THE TWO AXES SEPARATELY, so a floor can be long and
	// narrow rather than always square. A square floor of a fixed size is the
	// single biggest reason two floors read the same before anything inside
	// them is looked at.
	Shape.Width = (Request.Width > 0) ? Request.Width
		: Stream.RandRange(LeastFloorSide, MostFloorSide);
	Shape.Height = (Request.Height > 0) ? Request.Height
		: Stream.RandRange(LeastFloorSide, MostFloorSide);

	// HOW BIG A ROOM IS, WHICH DECIDES HOW MANY THERE ARE. The splitter halves
	// an area while both halves would still be this wide, so a large value
	// gives a handful of great halls and a small one gives a warren.
	//
	// CAPPED SO AT LEAST ONE SPLIT HAPPENS ON THE SHORTER AXIS. Without the cap
	// a large value on a small floor produces one room, which is an arena that
	// says it is a hall.
	const int32 Shorter = FMath::Min(Shape.Width, Shape.Height) - 2;
	Shape.MinLeafSide = Stream.RandRange(
		LeastLeafSide, FMath::Max(LeastLeafSide, FMath::Min(MostLeafSide, Shorter / 2)));

	// HOW WIDE A CORRIDOR IS. Two is the narrowest that is not single file;
	// four reads as a hall rather than a passage. Tied to the room size, because
	// a four-wide corridor into a five-wide room is not a corridor.
	Shape.ConnectionWidth = Stream.RandRange(
		LeastConnectionWidth,
		FMath::Max(LeastConnectionWidth,
				   FMath::Min(MostConnectionWidth, Shape.MinLeafSide / 3)));

	// HOW MANY WAYS ROUND. Connections beyond the ones that make the floor one
	// piece are what close loops, and a floor with none is a tree where every
	// side room is a trip out and back -- the fault Diablo 4 patched out of its
	// dungeons. So the least is one, never zero.
	Shape.ExtraConnections = Stream.RandRange(LeastExtraConnections,
											  MostExtraConnections);

	// HOW MUCH A ROOM MAY BE SMALLER THAN THE SPACE IT WAS GIVEN. At zero every
	// room fills its share exactly and the floor looks like a grid.
	Shape.MostRoomShrink = Stream.RandRange(0, MostRoomShrink);

	// HOW OPEN A CAVERN IS, and how smooth its walls. More floor to begin with
	// settles into wide caves; less settles into chambers joined by necks.
	Shape.CavernInitialFloorChance = Stream.FRandRange(LeastCavernFloorChance,
													   MostCavernFloorChance);
	Shape.CavernSmoothingPasses = Stream.RandRange(LeastCavernPasses,
												   MostCavernPasses);
	Shape.CavernSolidNeighboursToFill = Stream.RandRange(
		LeastCavernFillThreshold, MostCavernFillThreshold);

	// HOW LONG AN ARENA IS. Separately per axis, so an arena can be a corridor
	// of a room rather than always a circle.
	Shape.ArenaScaleX = Stream.FRandRange(LeastArenaScale, MostArenaScale);
	Shape.ArenaScaleY = Stream.FRandRange(LeastArenaScale, MostArenaScale);

	return Shape;
}

int32 FCataclysmFloorGenerator::SeedForFloor(int32 DungeonSeed, int32 FloorNumber)
{
	uint32 Mixed = static_cast<uint32>(DungeonSeed) * 0x9E3779B1u
		+ static_cast<uint32>(FloorNumber) * 0x85EBCA6Bu;
	Mixed ^= Mixed >> 16;
	Mixed *= 0x7FEB352Du;
	Mixed ^= Mixed >> 15;
	Mixed *= 0x846CA68Bu;
	Mixed ^= Mixed >> 16;
	return static_cast<int32>(Mixed & 0x7FFFFFFFu);
}

bool FCataclysmFloorGenerator::FindShortcutBetween(const FCataclysmFloorPlan& Plan, FIntPoint From, FIntPoint To,
												   const TSet<FIntPoint>& Avoid, FCataclysmFloorShortcut& Out)
{
	if (!Plan.IsFloor(From) || !Plan.IsFloor(To))
	{
		return false;
	}
	const TArray<int32> FromStart = CataclysmFloorDistancesFrom(Plan, From);
	const TArray<int32> FromEnd = CataclysmFloorDistancesFrom(Plan, To);
	const int32 Base = FromStart[Plan.IndexOf(To)];
	if (Base == INDEX_NONE)
	{
		return false;
	}

	// EVERY PAIR OF WALKABLE CELLS CLOSE ENOUGH TO JOIN, estimated from the two searches alone.
	TArray<FGenShortcutCandidate> Candidates;
	for (int32 Index = 0; Index < Plan.Cells.Num(); ++Index)
	{
		if (Plan.Cells[Index] != ECataclysmFloorCell::Floor || FromStart[Index] == INDEX_NONE)
		{
			continue;
		}
		const FIntPoint A = Plan.CellAt(Index);
		for (int32 DY = -ShortcutMostLength; DY <= ShortcutMostLength; ++DY)
		{
			for (int32 DX = -ShortcutMostLength; DX <= ShortcutMostLength; ++DX)
			{
				const int32 Length = FMath::Abs(DX) + FMath::Abs(DY);
				const FIntPoint B = A + FIntPoint(DX, DY);
				if (Length < 3 || Length > ShortcutMostLength || !Plan.IsFloor(B))
				{
					continue;
				}
				const int32 Estimate = Base - (FromStart[Index] + Length + FromEnd[Plan.IndexOf(B)]);
				if (Estimate >= ShortcutLeastSaving)
				{
					Candidates.Add({A, B, Base, Estimate});
				}
			}
		}
	}
	GenSortShortcutCandidates(Candidates);
	for (int32 Index = 0; Index < Candidates.Num() && Index < ShortcutMostChecked; ++Index)
	{
		if (GenCheckShortcut(Plan, Candidates[Index].A, Candidates[Index].B, From, To, Base, Avoid, Out))
		{
			return true;
		}
	}
	return false;
}

TArray<FCataclysmFloorShortcut> FCataclysmFloorGenerator::FindShortcuts(const FCataclysmFloorPlan& Plan,
																		FRandomStream& Stream, int32 Most,
																		const TSet<FIntPoint>& Avoid, FIntPoint Near,
																		int32 NearCells)
{
	TArray<FCataclysmFloorShortcut> Found;
	TArray<FIntPoint> Walkable;
	for (int32 Index = 0; Index < Plan.Cells.Num(); ++Index)
	{
		if (Plan.Cells[Index] == ECataclysmFloorCell::Floor)
		{
			Walkable.Add(Plan.CellAt(Index));
		}
	}
	if (Most <= 0 || Walkable.IsEmpty())
	{
		return Found;
	}

	// WHERE TO SEARCH FROM: near a cell when one is named, else cells drawn on the floor's own stream.
	TArray<FIntPoint> Sources;
	if (Plan.IsFloor(Near))
	{
		for (const FIntPoint& Cell : Walkable)
		{
			if (FMath::Abs(Cell.X - Near.X) + FMath::Abs(Cell.Y - Near.Y) <= NearCells)
			{
				Sources.Add(Cell);
			}
		}
	}
	else
	{
		for (int32 Index = 0; Index < ShortcutSources; ++Index)
		{
			Sources.AddUnique(Walkable[Stream.RandRange(0, Walkable.Num() - 1)]);
		}
	}

	TArray<FGenShortcutCandidate> Candidates;
	for (const FIntPoint& A : Sources)
	{
		const TArray<int32> FromA = CataclysmFloorDistancesFrom(Plan, A);
		for (int32 DY = -ShortcutMostLength; DY <= ShortcutMostLength; ++DY)
		{
			for (int32 DX = -ShortcutMostLength; DX <= ShortcutMostLength; ++DX)
			{
				const int32 Length = FMath::Abs(DX) + FMath::Abs(DY);
				const FIntPoint B = A + FIntPoint(DX, DY);
				if (Length < 3 || Length > ShortcutMostLength || !Plan.IsFloor(B))
				{
					continue;
				}
				const int32 Walk = FromA[Plan.IndexOf(B)];
				if (Walk != INDEX_NONE && Walk - Length >= ShortcutLeastSaving)
				{
					Candidates.Add({A, B, Walk, Walk - Length});
				}
			}
		}
	}
	GenSortShortcutCandidates(Candidates);

	TSet<FIntPoint> Used = Avoid;
	for (int32 Index = 0; Index < Candidates.Num() && Index < ShortcutMostChecked && Found.Num() < Most; ++Index)
	{
		FCataclysmFloorShortcut One;
		if (GenCheckShortcut(Plan, Candidates[Index].A, Candidates[Index].B, Candidates[Index].A, Candidates[Index].B,
							 Candidates[Index].Walk, Used, One))
		{
			Used.Append(One.NewCells);
			Found.Add(One);
		}
	}
	return Found;
}

void FCataclysmFloorGenerator::CarveShortcut(FCataclysmFloorPlan& Plan, const FCataclysmFloorShortcut& Shortcut)
{
	if (Shortcut.IsValid())
	{
		GenCarveConnection(Plan, Shortcut.A, Shortcut.B, ShortcutWidth);
	}
}

FCataclysmFloorSections FCataclysmFloorGenerator::FindSections(const FCataclysmFloorPlan& Plan,
	const TArray<FIntPoint>& MayClose)
{
	FCataclysmFloorSections Out;
	if (Plan.Layout != ECataclysmFloorLayout::Halls || !Plan.IsBuilt())
	{
		return Out;
	}

	const int32 Walkable = Plan.FloorCount();
	const int32 LeastArea = (Walkable + SectionLeastAreaOneIn - 1) / SectionLeastAreaOneIn;

	// THE MAY-CLOSE CELLS THAT ARE WALKABLE. One that is rock already is closed in both states and asks nothing.
	TArray<FIntPoint> Closable;
	for (const FIntPoint& Cell : MayClose)
	{
		if (Plan.IsFloor(Cell))
		{
			Closable.AddUnique(Cell);
		}
	}

	// THE ONE SEARCH: the fewest lines between the near part of the floor and the far part.
	FGenSectionSearch Search;
	Search.Plan = &Plan;
	GenSectionFindLines(Plan, Search);
	GenSectionMarkNearAndFar(Plan, Search);
	GenSectionWalk(Search, TArray<int32>());
	Out.Walks = Search.Walks;
	Out.bReachedTheWalkLimit = Search.bReachedTheLimit;

	// ONE COPY, CLOSED AND OPENED AGAIN FOR EVERY QUESTION, rather than a copy of the plan for each.
	FCataclysmFloorPlan Work = Plan;
	const int32 EntranceIndex = Plan.IndexOf(Plan.Entrance);
	const int32 ExitIndex = Plan.IndexOf(Plan.Exit);

	// ONE COLLECTION OF BOUNDARIES. THREE SECTIONS: one of the boundaries nearest one third with one of those nearest
	// two thirds.
	const TArray<FGenSectionBoundary> All = GenSectionBoundariesFound(Work, Search, LeastArea);
	TArray<FGenSectionBoundary> NearOneThird = All;
	TArray<FGenSectionBoundary> NearTwoThirds = All;
	GenSectionKeepNearest(NearOneThird, Walkable, 1, 3);
	GenSectionKeepNearest(NearTwoThirds, Walkable, 2, 3);

	const FGenSectionBoundary* BestFirst = nullptr;
	const FGenSectionBoundary* BestSecond = nullptr;
	int32 BestSmallest = 0;
	int32 BestClosed = 0;
	FIntPoint BestLowest(-1, -1);
	TArray<int32> BestNumbers;

	TArray<int32> AreaOf;
	TArray<int32> Sizes;
	for (const FGenSectionBoundary& Nearer : NearOneThird)
	{
		for (const FGenSectionBoundary& Further : NearTwoThirds)
		{
			TArray<FIntPoint> Closed = Nearer.Cells;
			for (const FIntPoint& Cell : Further.Cells)
			{
				Closed.AddUnique(Cell);
			}
			GenSectionAreasWithout(Work, Closed, AreaOf, Sizes);

			// EXACTLY THREE AREAS, EACH A TENTH OR MORE, THE ENTRANCE AND THE EXIT IN DIFFERENT ONES.
			if (Sizes.Num() != 3)
			{
				continue;
			}
			const int32 Smallest = FMath::Min3(Sizes[0], Sizes[1], Sizes[2]);
			if (Smallest < LeastArea)
			{
				continue;
			}
			const int32 EntranceArea = AreaOf[EntranceIndex];
			const int32 ExitArea = AreaOf[ExitIndex];
			if (EntranceArea == ExitArea)
			{
				continue;
			}

			// THE THREE AREAS LIE IN A ROW, ruled 2026-10-08. Each boundary alone already parts the entrance from the
			// exit, or it would not have been kept. One of them, alone, must leave the third area on the exit's side:
			// that one lies between sections 0 and 1. The other, alone, must leave it on the entrance's side. When
			// both leave it on the same side the third area is a pocket off one end, and no numbering of the two
			// boundaries could say "boundary 0 lies between sections 0 and 1": that pair is not a division.
			const int32 MiddleArea = 3 - EntranceArea - ExitArea;
			const int32 MiddleIndex = AreaOf.IndexOfByKey(MiddleArea);
			const bool bNearerHoldsItBack = Nearer.AreaOf[MiddleIndex] != Nearer.EntranceArea;
			const bool bFurtherHoldsItBack = Further.AreaOf[MiddleIndex] != Further.EntranceArea;
			if (bNearerHoldsItBack == bFurtherHoldsItBack)
			{
				continue;
			}

			// THE LARGEST SMALLEST AREA; then the fewer cells closed; then the lower lowest cell. A pair that ties on
			// all three loses to the one tried first.
			const FIntPoint Lowest = GenSectionLowestCell(Closed);
			const bool bBetter = !BestFirst
				|| Smallest > BestSmallest
				|| (Smallest == BestSmallest && Closed.Num() < BestClosed)
				|| (Smallest == BestSmallest && Closed.Num() == BestClosed && GenSectionCellIsLower(Lowest, BestLowest));
			if (!bBetter)
			{
				continue;
			}

			// AND IT HOLDS WITH THE MAY-CLOSE CELLS CLOSED, asked last because it labels the plan up to three times
			// more, and only of a pair that would otherwise be taken. With no such cells nothing is asked.
			const FGenSectionBoundary& LiesFirst = bNearerHoldsItBack ? Nearer : Further;
			const FGenSectionBoundary& LiesSecond = bNearerHoldsItBack ? Further : Nearer;
			TArray<int32> Numbers = GenSectionNumbers(AreaOf, EntranceArea, ExitArea, 3);
			if (!Closable.IsEmpty())
			{
				TArray<TArray<FIntPoint>> InOrder;
				InOrder.Add(LiesFirst.Cells);
				InOrder.Add(LiesSecond.Cells);
				if (!GenSectionHoldsWithCellsClosed(Work, InOrder, Closable, Numbers, LeastArea))
				{
					continue;
				}
			}
			BestFirst = &LiesFirst;
			BestSecond = &LiesSecond;
			BestSmallest = Smallest;
			BestClosed = Closed.Num();
			BestLowest = Lowest;
			BestNumbers = MoveTemp(Numbers);
		}
	}

	if (BestFirst && BestSecond)
	{
		Out.Boundaries.Add(BestFirst->Cells);
		Out.Boundaries.Add(BestSecond->Cells);
		Out.BoundaryLines.Add(BestFirst->Lines);
		Out.BoundaryLines.Add(BestSecond->Lines);
		Out.Section = MoveTemp(BestNumbers);
		return Out;
	}

	// TWO SECTIONS: the boundary of the same collection whose smaller side is largest; then the fewer cells closed;
	// then the lower lowest cell. A boundary that ties on all three loses to the one met first. It too must hold
	// with the may-close cells closed.
	const FGenSectionBoundary* Half = nullptr;
	for (const FGenSectionBoundary& One : All)
	{
		const int32 Smaller = FMath::Min(One.EntranceSide, One.ExitSide);
		const int32 HalfSmaller = Half ? FMath::Min(Half->EntranceSide, Half->ExitSide) : 0;
		const bool bBetter = !Half
			|| Smaller > HalfSmaller
			|| (Smaller == HalfSmaller && One.Cells.Num() < Half->Cells.Num())
			|| (Smaller == HalfSmaller && One.Cells.Num() == Half->Cells.Num()
				&& GenSectionCellIsLower(One.First, Half->First));
		if (!bBetter)
		{
			continue;
		}
		if (!Closable.IsEmpty())
		{
			TArray<TArray<FIntPoint>> InOrder;
			InOrder.Add(One.Cells);
			if (!GenSectionHoldsWithCellsClosed(
					Work, InOrder, Closable,
					GenSectionNumbers(One.AreaOf, One.EntranceArea, 1 - One.EntranceArea, 2), LeastArea))
			{
				continue;
			}
		}
		Half = &One;
	}
	if (Half)
	{
		Out.Boundaries.Add(Half->Cells);
		Out.BoundaryLines.Add(Half->Lines);
		Out.Section = GenSectionNumbers(Half->AreaOf, Half->EntranceArea, 1 - Half->EntranceArea, 2);
	}
	return Out;
}

FCataclysmFloorPlan FCataclysmFloorGenerator::Generate(const FCataclysmFloorRequest& Request)
{
	const int32 Seed = SeedForFloor(Request.DungeonSeed, Request.FloorNumber);

	FCataclysmFloorPlan Plan;

	for (int32 Attempt = 1; Attempt <= MaxAttempts; ++Attempt)
	{
		// A re-roll is as deterministic as the first try: its stream is derived
		// from the floor's own seed and the attempt number, so the whole sequence
		// is fixed by the seed.
		FRandomStream Stream(SeedForFloor(Seed, Attempt));

		// THE SHAPE IS ROLLED BEFORE ANYTHING IS CARVED, and a re-roll rolls a
		// new one. That matters: an attempt fails when the floor came out too
		// small, and trying the same shape again with a different arrangement is
		// a worse bet than trying a different shape.
		const FCataclysmFloorShape Shape = RollShape(Stream, Request);
		const int32 EnoughFloor =
			FMath::CeilToInt(MinOpenFraction * Shape.Width * Shape.Height);

		Plan.Reset(Shape.Width, Shape.Height);
		Plan.Layout = Request.Layout;
		Plan.Seed = Seed;
		Plan.Attempts = Attempt;
		Plan.Shape = Shape;

		switch (Request.Layout)
		{
		case ECataclysmFloorLayout::Caverns:
			GenCarveCaverns(Plan, Stream, Shape);
			break;
		case ECataclysmFloorLayout::Arena:
			GenCarveArena(Plan, Stream, Shape);
			break;
		case ECataclysmFloorLayout::Halls:
		default:
			GenCarveHalls(Plan, Stream, Shape);
			break;
		}

		// Every layout gets the same two guarantees, rather than each family
		// remembering to ask for them: the floor is one connected piece, and
		// nowhere on it is a cell the player can walk to and only walk back out
		// of. The arena's wobbled edge produced a couple of one-cell tips before
		// the pruning was moved here.
		GenKeepLargestRegion(Plan);
		GenPruneDeadEnds(Plan);
		GenPlaceEndsFarApart(Plan);

		if (Plan.IsBuilt() && Plan.FloorCount() >= EnoughFloor)
		{
			return Plan;
		}
	}

	// Every attempt was too small or produced nothing to stand on. The plan is
	// returned as it stands rather than faked, so the caller and the tests see
	// the truth: `IsBuilt` is false or `OpenFraction` is below MinOpenFraction,
	// and `Attempts` says the re-rolls were used up.
	return Plan;
}
