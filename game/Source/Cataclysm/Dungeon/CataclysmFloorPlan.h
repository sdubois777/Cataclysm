// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CataclysmFloorPlan.generated.h"

/**
 * Which layout family carved a floor.
 *
 * THERE IS MORE THAN ONE ON PURPOSE. The project owner asked on 2026-08-21 for
 * variety -- "maybe some dungeons are rectangle rooms and such and others are
 * more cavernous, theme to each cataclysm" -- and that is what shipped games in
 * this genre do. Diablo 1 carries four separate generators, one per theme:
 * recursive room budding for the Cathedral, recursive subdivision for the
 * Catacombs, edge-based growth followed by erosion for the Caves, and a mirrored
 * variant for Hell. The sources are recorded on issue #34.
 *
 * ALL THREE CARVE THE SAME GRID. The grid is bookkeeping, not appearance: it is
 * what makes "the same seed gives the same floor" and "the exit is reachable"
 * provable in a headless test. A cavern and a hall differ in which cells are
 * carved, not in what a cell is.
 *
 * REFLECTED, WHILE THE STRUCTS BELOW ARE NOT. `FCataclysmFloorPlan`'s comment
 * says to add reflection when something actually needs it, and
 * `ACataclysmDungeonGameMode` does: a layout that cannot be chosen in the
 * editor is a layout nobody can try. Unreal's header tool refuses a
 * `UPROPERTY` of an unreflected type outright. The enum is reflected and
 * nothing else in this file is.
 */
UENUM(BlueprintType)
enum class ECataclysmFloorLayout : uint8
{
	/** Large rectangular rooms joined by connections two cells wide. */
	Halls UMETA(DisplayName = "Halls"),

	/** Organic, rounded chambers with no straight walls. */
	Caverns UMETA(DisplayName = "Caverns"),

	/** One open space. What a Horde floor is, and what a boss floor wants. */
	Arena UMETA(DisplayName = "Arena"),

	/** Not a layout. The number of them, for a test that must cover every one. */
	Count UMETA(Hidden)
};

/** What one cell of a floor is. */
enum class ECataclysmFloorCell : uint8
{
	Solid = 0,
	Floor = 1,
};

/**
 * What one floor is LIKE, as opposed to how it is arranged.
 *
 * WHY THIS EXISTS. Every number here used to be a compile-time constant, so
 * every floor of every dungeon was 40 by 40 cells, held ten to sixteen
 * rectangular rooms, and joined them with corridors exactly two cells wide.
 * The arrangement varied and nothing else did. Over a dungeon of fifty floors
 * that reads as the same floor fifty times, which is what the project owner
 * said on 2026-08-21: "Nobody wants to play a 50 floor dungeon where every
 * floor is the same layout."
 *
 * ROLLED PER FLOOR FROM ITS SEED, so it costs nothing and stays deterministic.
 * The same seed still gives the same floor, which issue #40 requires twice over
 * -- a dungeon must look the same when the player leaves and returns, and a bug
 * in a floor has to be reproducible from its seed.
 *
 * THESE ARE THE KNOBS THE GENRE SAYS MATTER. Cogmind's author, writing about
 * keeping procedurally generated maps from repeating, puts the question as
 * "Many small rooms or fewer large rooms? Lots of long corridors, or no
 * corridors at all?", and notes these change how a map PLAYS rather than only
 * how it looks. Diablo 1 reached the same place from the other direction: its
 * four dungeon themes differ in exactly these terms -- the Cathedral is large
 * rooms joined by doors, the Catacombs are cramped with tight passages, Hell is
 * very large rooms joined by short wide halls. Sources are on issue #34.
 *
 * WHAT IS NOT HERE, and is the next thing worth adding: hand-built set pieces.
 * The same writing is clear that a memorable floor comes from a deliberate room
 * somebody placed, not from a knob. That needs art direction.
 */
struct CATACLYSM_API FCataclysmFloorShape
{
	/** Cells across. */
	int32 Width = 40;

	/** Cells down. Rolled separately, so a floor can be long rather than square. */
	int32 Height = 40;

	/** The smallest area the room splitter will divide. Decides room size. */
	int32 MinLeafSide = 8;

	/** How many cells across a connection between two rooms is. */
	int32 ConnectionWidth = 2;

	/** Connections beyond the ones that make the floor one piece. Loops. */
	int32 ExtraConnections = 3;

	/** How much a room may be smaller than the space it was given. */
	int32 MostRoomShrink = 2;

	/** How much of the grid a cavern starts as floor, before smoothing. */
	float CavernInitialFloorChance = 0.48f;

	/** How many times a cavern's smoothing rule is applied. */
	int32 CavernSmoothingPasses = 5;

	/**
	 * How many of a cell's eight neighbours must be solid for it to become
	 * solid, when a cavern is smoothed.
	 *
	 * A LOWER NUMBER FILLS MORE IN, so four gives tight chambers joined by necks
	 * and six gives wide open caves. It was fixed at five, which is why every
	 * cavern came out about equally open.
	 */
	int32 CavernSolidNeighboursToFill = 5;

	/**
	 * How far an arena reaches across and along, as a fraction of the floor.
	 *
	 * ROLLED SEPARATELY SO AN ARENA CAN BE LONG. Both were between 0.88 and 1.00,
	 * so every arena was a near-circle and the walk across one was the floor's
	 * width whatever the seed. An arena is one open space by definition, so its
	 * shape is the only thing it has to vary.
	 */
	float ArenaScaleX = 1.0f;
	float ArenaScaleY = 1.0f;
};

/**
 * One floor, as a grid of cells, with an entrance and an exit.
 *
 * WHAT IT IS NOT: geometry. There is no mesh, no actor and no map here. This is
 * the walkability pass and nothing else, and it is deliberately the first thing
 * built.
 *
 * WHY WALKABILITY FIRST. Diablo 1 generates a "predungeon" -- a floor-versus-
 * solid map with no tileset in it at all -- and only afterwards converts that
 * map into tileset pieces. The reason to copy that order here is narrower than
 * the reason Blizzard had: **the automation tests run with `-nullrhi`**, which
 * is what issue #559 records about Niagara. A grid of cells can be asserted on
 * in a headless test. A room full of static meshes cannot. Every property worth
 * having -- the exit is reachable, the same seed gives the same floor, the floor
 * is not a maze of one-cell passages -- is a property of this grid, and pinning
 * them now costs a test each and retrofitting them later costs a rewrite.
 *
 * NOT A `USTRUCT`, AND NOT REFLECTED. Nothing in Blueprint reads a floor plan
 * and nothing serialises one; the plan is regenerated from its seed instead,
 * which is the whole point of generating from a seed. `CataclysmEmpire`'s build
 * file states the same preference for its own layer -- "plain arithmetic on
 * plain structs". Make it a `USTRUCT` when something actually needs reflection.
 */
struct CATACLYSM_API FCataclysmFloorPlan
{
	/** Cells across. */
	int32 Width = 0;

	/** Cells down. */
	int32 Height = 0;

	/** `Width * Height` cells, row major: index = Y * Width + X. */
	TArray<ECataclysmFloorCell> Cells;

	/** Where the player arrives. `(-1, -1)` when the floor is empty. */
	FIntPoint Entrance = FIntPoint(-1, -1);

	/** The stairs down. `(-1, -1)` when the floor is empty. */
	FIntPoint Exit = FIntPoint(-1, -1);

	/** Which family carved it. */
	ECataclysmFloorLayout Layout = ECataclysmFloorLayout::Halls;

	/** The seed this floor was carved from. Derived, not the dungeon's own. */
	int32 Seed = 0;

	/**
	 * What this floor was rolled to be like: its size, its room size, how wide
	 * its corridors are, how loopy it is.
	 *
	 * KEPT SO IT CAN BE ASKED ABOUT. A test that wants to show floors differ in
	 * character rather than only in arrangement has to be able to read the
	 * character.
	 */
	FCataclysmFloorShape Shape;

	/**
	 * How many times generation had to re-roll before this floor was accepted.
	 *
	 * One means the first try was good. A cavern occasionally falls out of the
	 * random fill as a handful of small chambers, and the generator re-rolls
	 * rather than shipping one. Recorded rather than hidden, so a test can show
	 * that re-rolling is rare instead of assuming it.
	 */
	int32 Attempts = 0;

	/** Empties the grid to solid rock at the given size. */
	void Reset(int32 InWidth, int32 InHeight);

	/** Whether a cell coordinate is on the grid at all. */
	bool Contains(FIntPoint Cell) const
	{
		return Cell.X >= 0 && Cell.Y >= 0 && Cell.X < Width && Cell.Y < Height;
	}

	/** The index of a cell, or `INDEX_NONE` when it is off the grid. */
	int32 IndexOf(FIntPoint Cell) const
	{
		return Contains(Cell) ? Cell.Y * Width + Cell.X : INDEX_NONE;
	}

	/** Whether a cell can be walked on. Off the grid answers false. */
	bool IsFloor(FIntPoint Cell) const
	{
		const int32 Index = IndexOf(Cell);
		return Index != INDEX_NONE && Cells[Index] == ECataclysmFloorCell::Floor;
	}

	/** Makes a cell walkable. Off the grid does nothing. */
	void Carve(FIntPoint Cell)
	{
		const int32 Index = IndexOf(Cell);
		if (Index != INDEX_NONE)
		{
			Cells[Index] = ECataclysmFloorCell::Floor;
		}
	}

	/** Makes a cell solid. Off the grid does nothing. */
	void Fill(FIntPoint Cell)
	{
		const int32 Index = IndexOf(Cell);
		if (Index != INDEX_NONE)
		{
			Cells[Index] = ECataclysmFloorCell::Solid;
		}
	}

	/** How many cells can be walked on. */
	int32 FloorCount() const;

	/**
	 * How many of a cell's four orthogonal neighbours are walkable.
	 *
	 * ORTHOGONAL AND NOT DIAGONAL, because a diagonal gap is not a doorway: a
	 * character with a capsule cannot squeeze through one corner-to-corner.
	 */
	int32 OrthogonalNeighbours(FIntPoint Cell) const;

	/** The cell at an index, for walking a distance field back to coordinates. */
	FIntPoint CellAt(int32 Index) const
	{
		return (Width > 0 && Cells.IsValidIndex(Index))
			? FIntPoint(Index % Width, Index / Width)
			: FIntPoint(-1, -1);
	}

	/** Whether the plan holds a floor at all: a grid, an entrance and an exit. */
	bool IsBuilt() const
	{
		return Width > 0 && Height > 0 && Cells.Num() == Width * Height
			&& IsFloor(Entrance) && IsFloor(Exit);
	}
};

/**
 * How far every cell is from `Start`, in cells, walking orthogonally.
 *
 * `INDEX_NONE` for a solid cell and for a walkable cell that cannot be reached.
 * The array is `Width * Height` long, indexed the same way `Cells` is.
 *
 * ONE IMPLEMENTATION WITH TWO CALLERS, deliberately. The generator uses it to
 * place the entrance and the exit as far apart as the floor allows, and the
 * measurement below uses it to decide whether the exit is reachable and how long
 * the walk is. Two breadth-first searches would be two chances to disagree.
 */
CATACLYSM_API TArray<int32> CataclysmFloorDistancesFrom(const FCataclysmFloorPlan& Plan,
														FIntPoint Start);

/**
 * What is measurably true about a floor, so "annoying to navigate" can be a test
 * rather than an opinion.
 *
 * WHY THIS EXISTS AT ALL. The project owner set the constraint on 2026-08-21: "I
 * mostly just want to avoid tiny tedious rooms and hallways that make it annoying
 * for the player to navigate." That is the best-documented layout lesson in this
 * genre and it is not a matter of taste:
 *
 *   - Diablo 4 shipped dungeons that sent the player down a hallway to a side
 *     room and back again, was told they were tedious, and patched them to
 *     "minimize the need for backtracking", moving objectives onto the main path.
 *   - Path of Exile's players sort map layouts into linear, open and maze, and
 *     avoid the maze ones. Years of it, in the most-played game in the genre.
 *
 * The sources are on issue #34. The point of this struct is that the complaint
 * has a shape a program can check, so a new layout family cannot quietly be a
 * maze.
 */
struct CATACLYSM_API FCataclysmFloorQuality
{
	/** How many cells can be walked on. */
	int32 FloorCells = 0;

	/** Walkable cells as a fraction of the whole grid. */
	float OpenFraction = 0.0f;

	/**
	 * Walkable cells with exactly one walkable neighbour: the tip of a dead end.
	 *
	 * A dead end is a trip the player takes and has to take back. That is the
	 * fault Diablo 4 patched out.
	 */
	int32 DeadEnds = 0;

	/**
	 * Walkable cells with two or fewer walkable neighbours.
	 *
	 * THIS IS THE "TINY TEDIOUS HALLWAY" NUMBER, and it is exact rather than a
	 * proxy. A cell in a passage one cell wide can only be left the way it was
	 * entered, so it has at most two walkable neighbours. A cell in a passage two
	 * cells wide has three: forward, back, and its partner alongside. So a
	 * one-wide corridor counts every one of its cells here and a two-wide
	 * corridor counts only its two end caps.
	 *
	 * IT IS NEVER ZERO AND IS NOT MEANT TO BE. The corners of a rectangular room
	 * have two neighbours each, so a floor of sixteen rooms carries at least
	 * sixty-four. It is the FRACTION that says whether a floor is a maze.
	 */
	int32 NarrowCells = 0;

	/** `NarrowCells` over `FloorCells`. Zero when there is no floor. */
	float NarrowFraction = 0.0f;

	/**
	 * The longest unbroken stretch of single-file cells, counted as one group of
	 * narrow cells touching each other.
	 *
	 * THE SHARP VERSION OF THE SAME QUESTION, AND THE ONE WORTH ASSERTING ON.
	 * `NarrowFraction` turned out to be too blunt to catch the fault it exists
	 * for: setting `ConnectionWidth` to 1, which makes every corridor in a Halls
	 * floor single file, moved the fraction from 0.0836 to only 0.1549, because
	 * rooms are most of a floor's cells either way. It was measured on
	 * 2026-08-21 and the guard did not fire.
	 *
	 * This does fire, because it asks a different question. The four corners of a
	 * rectangular room are narrow cells but they do not touch each other, so each
	 * is a group of one. A corridor one cell wide and eight cells long is a group
	 * of eight. Legitimate floors sit at a handful; a floor of single-file
	 * corridors cannot.
	 */
	int32 LongestNarrowRun = 0;

	/**
	 * Walkable cells that cannot be reached from the entrance.
	 *
	 * Any number above zero is a bug in the carve, not a style choice: it is
	 * floor the player can see across a wall and never stand on.
	 */
	int32 UnreachableCells = 0;

	/** Whether the stairs down can be walked to from where the player arrives. */
	bool bExitReachable = false;

	/** The shortest walk from entrance to exit in cells, or -1 if there is none. */
	int32 PathLength = -1;
};

/** Measures a floor. See `FCataclysmFloorQuality` for what each figure means. */
CATACLYSM_API FCataclysmFloorQuality CataclysmMeasureFloor(const FCataclysmFloorPlan& Plan);

/** The layout's name, for a test failure message that says which one broke. */
CATACLYSM_API const TCHAR* CataclysmFloorLayoutName(ECataclysmFloorLayout Layout);
