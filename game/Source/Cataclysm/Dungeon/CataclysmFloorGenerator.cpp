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
	void GenCarveConnection(FCataclysmFloorPlan& Plan, FIntPoint A, FIntPoint B, int32 Wide)
	{
		const int32 FirstX = FMath::Min(A.X, B.X);
		const int32 LastX = FMath::Max(A.X, B.X);
		for (int32 X = FirstX; X <= LastX; ++X)
		{
			for (int32 Offset = 0; Offset < Wide; ++Offset)
			{
				Plan.Carve(FIntPoint(X, A.Y + Offset));
			}
		}

		const int32 FirstY = FMath::Min(A.Y, B.Y);
		const int32 LastY = FMath::Max(A.Y, B.Y);
		for (int32 Y = FirstY; Y <= LastY; ++Y)
		{
			for (int32 Offset = 0; Offset < Wide; ++Offset)
			{
				Plan.Carve(FIntPoint(B.X + Offset, Y));
			}
		}
	}

	/** Large rectangular rooms joined by connections two cells across. */
	void GenCarveHalls(FCataclysmFloorPlan& Plan, FRandomStream& Stream)
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
				const bool bCanSplitX = LeafWidth >= 2 * FGen::MinLeafSide;
				const bool bCanSplitY = LeafHeight >= 2 * FGen::MinLeafSide;

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
					const int32 At = Leaf.Min.X + FGen::MinLeafSide
						+ Stream.RandRange(0, LeafWidth - 2 * FGen::MinLeafSide);
					Next.Add(FIntRect(Leaf.Min, FIntPoint(At, Leaf.Max.Y)));
					Next.Add(FIntRect(FIntPoint(At, Leaf.Min.Y), Leaf.Max));
				}
				else
				{
					const int32 At = Leaf.Min.Y + FGen::MinLeafSide
						+ Stream.RandRange(0, LeafHeight - 2 * FGen::MinLeafSide);
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
				FMath::Min(2, RoomWidth - FGen::MinRoomSide));
			const int32 ShrinkY = Stream.RandRange(0,
				FMath::Min(2, RoomHeight - FGen::MinRoomSide));
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
		for (int32 Extra = 0; Extra < FGen::ExtraConnections; ++Extra)
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
							   FGen::ConnectionWidth);
		}
	}

	/** Rounded chambers with no straight walls. */
	void GenCarveCaverns(FCataclysmFloorPlan& Plan, FRandomStream& Stream)
	{
		for (int32 Y = 1; Y < Plan.Height - 1; ++Y)
		{
			for (int32 X = 1; X < Plan.Width - 1; ++X)
			{
				if (Stream.FRand() < FGen::CavernInitialFloorChance)
				{
					Plan.Carve(FIntPoint(X, Y));
				}
			}
		}

		for (int32 Pass = 0; Pass < FGen::CavernSmoothingPasses; ++Pass)
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
					Next[Y * Plan.Width + X] = (Solid >= FGen::CavernSolidNeighboursToFill)
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
	void GenCarveArena(FCataclysmFloorPlan& Plan, FRandomStream& Stream)
	{
		const float CentreX = (Plan.Width - 1) * 0.5f;
		const float CentreY = (Plan.Height - 1) * 0.5f;
		const float RadiusX = FMath::Max(1.0f, CentreX - 1.0f);
		const float RadiusY = FMath::Max(1.0f, CentreY - 1.0f);

		const float Phase = Stream.FRand() * 2.0f * PI;
		const int32 Lobes = Stream.RandRange(3, 6);
		const float Wobble = 0.06f + Stream.FRand() * 0.06f;
		const float ScaleX = 0.88f + Stream.FRand() * 0.12f;
		const float ScaleY = 0.88f + Stream.FRand() * 0.12f;

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

FCataclysmFloorPlan FCataclysmFloorGenerator::Generate(const FCataclysmFloorRequest& Request)
{
	const int32 Width = (Request.Width > 0) ? Request.Width : DefaultWidth;
	const int32 Height = (Request.Height > 0) ? Request.Height : DefaultHeight;
	const int32 Seed = SeedForFloor(Request.DungeonSeed, Request.FloorNumber);
	const int32 EnoughFloor = FMath::CeilToInt(MinOpenFraction * Width * Height);

	FCataclysmFloorPlan Plan;

	for (int32 Attempt = 1; Attempt <= MaxAttempts; ++Attempt)
	{
		Plan.Reset(Width, Height);
		Plan.Layout = Request.Layout;
		Plan.Seed = Seed;
		Plan.Attempts = Attempt;

		// A re-roll is as deterministic as the first try: its stream is derived
		// from the floor's own seed and the attempt number, so the whole sequence
		// is fixed by the seed.
		FRandomStream Stream(SeedForFloor(Seed, Attempt));

		switch (Request.Layout)
		{
		case ECataclysmFloorLayout::Caverns:
			GenCarveCaverns(Plan, Stream);
			break;
		case ECataclysmFloorLayout::Arena:
			GenCarveArena(Plan, Stream);
			break;
		case ECataclysmFloorLayout::Halls:
		default:
			GenCarveHalls(Plan, Stream);
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
