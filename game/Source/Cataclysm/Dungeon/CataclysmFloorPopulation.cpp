// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmFloorPopulation.h"

#include "Character/CataclysmSuccubusCharacter.h"
#include "Dungeon/CataclysmFloorGenerator.h"
#include "Math/RandomStream.h"

const TCHAR* CataclysmDungeonCreatureName(ECataclysmDungeonCreature Creature)
{
	switch (Creature)
	{
	case ECataclysmDungeonCreature::Imp:				return TEXT("Imp");
	case ECataclysmDungeonCreature::Hellhound:			return TEXT("Hellhound");
	case ECataclysmDungeonCreature::Brute:				return TEXT("Brute");
	case ECataclysmDungeonCreature::AbyssalWarden:		return TEXT("AbyssalWarden");
	case ECataclysmDungeonCreature::CorruptedSentinel:	return TEXT("CorruptedSentinel");
	case ECataclysmDungeonCreature::Succubus:			return TEXT("Succubus");
	case ECataclysmDungeonCreature::Gatekeeper:			return TEXT("Gatekeeper");
	default:											return TEXT("Unknown");
	}
}

int32 FCataclysmFloorPopulation::HowMany(ECataclysmDungeonCreature Creature) const
{
	int32 Count = 0;
	for (const FCataclysmEnemyPlacement& Enemy : Enemies)
	{
		if (Enemy.Creature == Creature)
		{
			++Count;
		}
	}
	return Count;
}

int32 FCataclysmFloorPopulation::KindsPresent() const
{
	int32 Kinds = 0;
	for (uint8 Which = 0; Which < static_cast<uint8>(ECataclysmDungeonCreature::Count); ++Which)
	{
		if (HowMany(static_cast<ECataclysmDungeonCreature>(Which)) > 0)
		{
			++Kinds;
		}
	}
	return Kinds;
}

const TArray<FCataclysmFloorPopulator::FPackKind>& FCataclysmFloorPopulator::PackKinds()
{
	// A FUNCTION-LOCAL STATIC RATHER THAN A FILE-SCOPE ONE. Unreal merges a
	// module's `.cpp` files into one translation unit, so a file-scope name here
	// collides with the same name in another file, and only once both are
	// committed. The dungeon's console variables next door carry the same note.
	static const TArray<FPackKind> Kinds = {
		{ ECataclysmDungeonCreature::Imp,				10, 1.0f },
		{ ECataclysmDungeonCreature::Hellhound,			 3, 1.0f },
		{ ECataclysmDungeonCreature::Brute,				 2, 1.0f },
		{ ECataclysmDungeonCreature::AbyssalWarden,		 1, 1.0f },
		{ ECataclysmDungeonCreature::CorruptedSentinel,	 1, 1.0f },
	};
	return Kinds;
}

namespace
{
	/**
	 * How far a Succubus's aura reaches, in cells.
	 *
	 * DERIVED FROM THE TWO NUMBERS IT DEPENDS ON rather than written down. The
	 * aura is 800 cm and a cell is 400 cm, so it reaches two cells; changing
	 * either constant moves this without anyone remembering to.
	 *
	 * NAMED FOR THIS FILE even though it sits in an anonymous namespace, because
	 * a unity build merges those into one namespace too and two files declaring
	 * `AuraCells` would collide.
	 */
	constexpr int32 CataclysmPopulationSuccubusAuraCells = static_cast<int32>(
		ACataclysmSuccubusCharacter::DominionRadiusCm
			/ FCataclysmFloorGenerator::CellSizeCm);

	static_assert(CataclysmPopulationSuccubusAuraCells >= 1,
		"A Succubus that cannot reach the next cell cannot buff anything, and "
		"the escort rule below would have nowhere legal to stand.");

	/** Draws one kind of group, by weight. */
	const FCataclysmFloorPopulator::FPackKind& CataclysmDrawPackKind(FRandomStream& Stream)
	{
		const TArray<FCataclysmFloorPopulator::FPackKind>& Kinds =
			FCataclysmFloorPopulator::PackKinds();

		float Total = 0.0f;
		for (const FCataclysmFloorPopulator::FPackKind& Kind : Kinds)
		{
			Total += FMath::Max(0.0f, Kind.Weight);
		}

		float Roll = Stream.FRandRange(0.0f, Total);
		for (const FCataclysmFloorPopulator::FPackKind& Kind : Kinds)
		{
			Roll -= FMath::Max(0.0f, Kind.Weight);
			if (Roll <= 0.0f)
			{
				return Kind;
			}
		}

		// Only reachable when every weight is zero, or when the roll lands
		// exactly on the end. The last entry is as good an answer as any.
		return Kinds.Last();
	}

	/**
	 * The free walkable cells nearest a group's middle, nearest first, each with
	 * how far it is.
	 *
	 * NEAREST BY WALKING, not by straight line, because the distances handed in
	 * came from a breadth-first search across walkable cells. So a cell on the
	 * far side of a wall is far away even when it is one metre off, and a group
	 * never has a member standing in the next room.
	 *
	 * SORTED BY DISTANCE AND THEN BY CELL INDEX. `TArray::Sort` is not stable, so
	 * two cells at the same distance would otherwise come out in an order that
	 * depends on the sort rather than on the floor, and the same seed would stop
	 * giving the same floor.
	 */
	TArray<TPair<int32, int32>> CataclysmCellsNear(const TArray<int32>& FromSite,
												   const TArray<int32>& FromEntrance,
												   const TSet<int32>& Occupied,
												   int32 MostCellsAway,
												   int32 LeastCellsFromEntrance)
	{
		TArray<TPair<int32, int32>> Found;
		for (int32 Index = 0; Index < FromSite.Num(); ++Index)
		{
			const int32 Away = FromSite[Index];
			if (Away == INDEX_NONE || Away > MostCellsAway || Occupied.Contains(Index))
			{
				continue;
			}

			// THE KEEP-OUT AROUND THE ENTRANCE APPLIES TO EVERY CREATURE, not
			// only to the middle of its group. A group whose middle is eight
			// cells from the entrance could otherwise reach back to five, which
			// is inside the distance these creatures notice a target from.
			const int32 FromWayIn = FromEntrance[Index];
			if (FromWayIn == INDEX_NONE || FromWayIn < LeastCellsFromEntrance)
			{
				continue;
			}

			Found.Add(TPair<int32, int32>(Away, Index));
		}

		Found.Sort([](const TPair<int32, int32>& A, const TPair<int32, int32>& B)
		{
			return (A.Key != B.Key) ? (A.Key < B.Key) : (A.Value < B.Value);
		});

		return Found;
	}

	/**
	 * The walkable cell a wave gathers on: the one furthest from the entrance,
	 * walked.
	 *
	 * THE FURTHEST RATHER THAN A DRAWN ONE, so the wave stands at the far end of
	 * the arena and the player walks in to meet it. On an Arena floor -- which
	 * is what a Horde dungeon carves, see `FCataclysmDungeonFloorRules::LayoutFor`
	 * -- that is at or beside the way down, so clearing the wave and taking the
	 * stairs are the same walk.
	 *
	 * TIES GO TO THE LOWER CELL INDEX, which is why the comparison is strict.
	 * Several cells are usually equally far on an open floor, and taking a
	 * different one on a different run would break the promise that the same
	 * seed gives the same floor.
	 */
	int32 CataclysmPopulationWaveSite(const TArray<int32>& FromEntrance)
	{
		int32 Site = INDEX_NONE;
		int32 Furthest = -1;
		for (int32 Index = 0; Index < FromEntrance.Num(); ++Index)
		{
			if (FromEntrance[Index] != INDEX_NONE && FromEntrance[Index] > Furthest)
			{
				Furthest = FromEntrance[Index];
				Site = Index;
			}
		}
		return Site;
	}
}

FCataclysmFloorPopulation FCataclysmFloorPopulator::Populate(
	const FCataclysmFloorPlan& Plan, float Scale,
	const FCataclysmFloorBrief& Brief)
{
	FCataclysmFloorPopulation Out;

	if (!Plan.IsBuilt())
	{
		// A floor with nothing to stand on holds nothing. Answering with an empty
		// population rather than failing is what lets a caller populate without
		// first asking whether there is a floor.
		return Out;
	}

	const float Density = EnemiesPerWalkableCell * FMath::Max(0.0f, Scale);
	Out.Wanted = FMath::RoundToInt(static_cast<float>(Plan.FloorCount()) * Density);

	// HOW FAR EVERY CELL IS FROM WHERE THE PLAYER ARRIVES, walked. It answers two
	// questions at once: which cells are far enough from the entrance to put a
	// creature on, and which cells can be reached from the entrance at all. A
	// creature on a walkable cell the player cannot reach never enters the game.
	const TArray<int32> FromEntrance =
		CataclysmFloorDistancesFrom(Plan, Plan.Entrance);

	/** Cells with a creature standing on them. No cell holds two. */
	TSet<int32> Occupied;

	// THE BOSS GOES DOWN FIRST, AND IT IS NOT PART OF THE DENSITY. "Every floor
	// ends with a boss fight" and "every dungeon has a boss on the final floor"
	// both put it at the way down, so it stands on the exit cell and that cell
	// is then taken -- no ordinary creature is placed on top of it.
	//
	// BEFORE THE EARLY RETURN BELOW, so a floor whose density asks for nothing
	// still gets its boss. `Cataclysm.DungeonEnemyScale 0` empties a floor and
	// walking a boss fight with nothing else on the floor is what that should
	// give on a boss floor.
	//
	// IT DRAWS NO NUMBERS. The stream is created after this, so an Elite
	// dungeon's ordinary creatures are drawn from the same numbers a plain
	// dungeon's are.
	if (Brief.bBossAtTheExit)
	{
		const int32 ExitIndex = Plan.IndexOf(Plan.Exit);
		if (ExitIndex != INDEX_NONE && Plan.IsFloor(Plan.Exit))
		{
			FCataclysmEnemyPlacement Boss;
			Boss.Cell = Plan.Exit;
			Boss.Creature = ECataclysmDungeonCreature::Gatekeeper;

			// `Pack` STAYS `INDEX_NONE`. A boss is not one of the groups the
			// floor is made of, so it is in no group and `PackCount` does not
			// count it.
			Out.Enemies.Add(Boss);
			Occupied.Add(ExitIndex);
		}
	}

	if (Out.Wanted <= 0)
	{
		return Out;
	}

	FRandomStream Stream(
		FCataclysmFloorGenerator::SeedForFloor(Plan.Seed, PopulationSalt));

	TArray<int32> Candidates;
	Candidates.Reserve(Plan.FloorCount());
	for (int32 Index = 0; Index < FromEntrance.Num(); ++Index)
	{
		if (FromEntrance[Index] != INDEX_NONE
			&& FromEntrance[Index] >= LeastCellsFromEntrance)
		{
			Candidates.Add(Index);
		}
	}

	// A WAVE IS ONE CROWD AND AN ORDINARY FLOOR IS SEVERAL ENCOUNTERS, and the
	// order the candidate cells are taken in is what decides which. Taken
	// nearest-the-wave-site first, the groups fill outward from one point and
	// stand together; taken shuffled, they land all over the floor.
	//
	// `LeastCellsBetweenPacks` IS THE OTHER HALF AND IT IS TURNED OFF BELOW.
	// Its own comment says it exists so "a floor reads as a series of encounters
	// rather than as one crowd that arrives together" -- a wave is that crowd,
	// so the rule that prevents one does not apply to it.
	TArray<int32> FromWave;
	if (Brief.bOneWave)
	{
		const int32 WaveIndex = CataclysmPopulationWaveSite(FromEntrance);
		if (WaveIndex != INDEX_NONE)
		{
			Out.WaveSite = Plan.CellAt(WaveIndex);
			FromWave = CataclysmFloorDistancesFrom(Plan, Out.WaveSite);

			// SORTED BY DISTANCE AND THEN BY CELL INDEX, because `TArray::Sort`
			// is not stable and a floor is full of cells that are equally far
			// from one point. Without the tiebreak the same seed would stop
			// giving the same floor.
			Candidates.Sort([&FromWave](int32 A, int32 B)
			{
				const int32 FarA = FromWave[A] == INDEX_NONE ? MAX_int32 : FromWave[A];
				const int32 FarB = FromWave[B] == INDEX_NONE ? MAX_int32 : FromWave[B];
				return (FarA != FarB) ? (FarA < FarB) : (A < B);
			});
		}
	}

	if (Out.WaveSite == FIntPoint(-1, -1))
	{
		// SHUFFLED, AND NOT AS A FLOURISH. Taken in index order the candidates run
		// left to right and top to bottom, so every group would be placed in the top
		// rows until the count was met and the bottom of every floor would be empty.
		for (int32 Last = Candidates.Num() - 1; Last > 0; --Last)
		{
			Candidates.Swap(Last, Stream.RandRange(0, Last));
		}
	}

	/** Cells too close to a group already placed to hold the middle of another. */
	TSet<int32> Claimed;

	const bool bSpreadOut = Out.WaveSite == FIntPoint(-1, -1);

	int32 Placed = 0;
	for (int32 Which = 0; Which < Candidates.Num() && Placed < Out.Wanted; ++Which)
	{
		const int32 SiteIndex = Candidates[Which];

		// ON AN ORDINARY FLOOR a group's middle may not stand within
		// `LeastCellsBetweenPacks` of one already placed, which is what spreads
		// the encounters out. IN A WAVE that rule is off and the only cell a
		// group may not start on is one that already holds a creature -- which
		// is also what keeps this loop cheap, because the crowd's own cells are
		// what it skips over as it grows outward.
		if (bSpreadOut ? Claimed.Contains(SiteIndex) : Occupied.Contains(SiteIndex))
		{
			continue;
		}

		// ONE BREADTH-FIRST SEARCH ANSWERS BOTH QUESTIONS this site raises: which
		// cells its own creatures stand on, and which cells are now too close for
		// the next group. Doing it twice would be two chances to disagree, which
		// is why `CataclysmFloorDistancesFrom` has one implementation and two
		// callers in the first place.
		const TArray<int32> FromSite =
			CataclysmFloorDistancesFrom(Plan, Plan.CellAt(SiteIndex));

		if (bSpreadOut)
		{
			for (int32 Index = 0; Index < FromSite.Num(); ++Index)
			{
				if (FromSite[Index] != INDEX_NONE
					&& FromSite[Index] < LeastCellsBetweenPacks)
				{
					Claimed.Add(Index);
				}
			}
		}

		const FPackKind& Kind = CataclysmDrawPackKind(Stream);

		const TArray<TPair<int32, int32>> Nearby = CataclysmCellsNear(
			FromSite, FromEntrance, Occupied,
			MostCellsFromPackSite, LeastCellsFromEntrance);

		// FEWER CELLS THAN THE GROUP WANTS MEANS A SMALLER GROUP. It does not
		// mean a creature standing in rock, and it does not mean the group is
		// abandoned: two Brutes in a corridor that fits one is one Brute.
		const int32 HowManyHere = FMath::Min(Kind.Count, Nearby.Num());
		if (HowManyHere <= 0)
		{
			continue;
		}

		const int32 Pack = Out.PackCount++;
		Out.PackSites.Add(Plan.CellAt(SiteIndex));

		for (int32 Member = 0; Member < HowManyHere; ++Member)
		{
			FCataclysmEnemyPlacement Placement;
			Placement.Cell = Plan.CellAt(Nearby[Member].Value);
			Placement.Creature = Kind.Creature;
			Placement.Pack = Pack;

			Out.Enemies.Add(Placement);
			Occupied.Add(Nearby[Member].Value);
			++Placed;
		}

		// AND ONE GROUP IN FOUR IS JOINED BY A SUCCUBUS. See
		// `SuccubusEscortsOnePackIn` for why one is never placed on its own.
		if (Stream.RandRange(0, SuccubusEscortsOnePackIn - 1) != 0)
		{
			continue;
		}

		// IT MUST STAND WHERE ITS AURA COVERS THE GROUP, and that is a guarantee
		// rather than a hope. `Nearby[0]` is the middle of the group itself, at a
		// walking distance of zero, and a creature is always standing on it --
		// `HowManyHere` is at least one. Straight-line distance is never longer
		// than walking distance, so any cell within two steps of that middle is
		// within the aura's 8 metres of the creature standing there.
		//
		// TAKING THE NEXT FREE CELL WITHOUT THIS CHECK WOULD USUALLY WORK AND
		// SOMETIMES NOT. A group may spread three cells from its middle, which is
		// 12 metres, and the aura reaches 8.
		//
		// ONE CELL IS LOOKED AT AND NOT A RANGE OF THEM, because `Nearby` is
		// sorted by distance: the first cell the group did not take is the
		// nearest one left, so if that is beyond the aura then every later one is
		// too, and this group goes without.
		const int32 Spare = HowManyHere;
		if (Nearby.IsValidIndex(Spare)
			&& Nearby[Spare].Key <= CataclysmPopulationSuccubusAuraCells)
		{
			FCataclysmEnemyPlacement Escort;
			Escort.Cell = Plan.CellAt(Nearby[Spare].Value);
			Escort.Creature = ECataclysmDungeonCreature::Succubus;
			Escort.Pack = Pack;

			Out.Enemies.Add(Escort);
			Occupied.Add(Nearby[Spare].Value);
			++Placed;
		}
	}

	return Out;
}
