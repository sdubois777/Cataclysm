// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dungeon/CataclysmFloorPlan.h"

/** What to build. Width and height of zero mean the generator's own defaults. */
struct CATACLYSM_API FCataclysmFloorRequest
{
	/** The dungeon's seed. Every floor of one dungeon shares it. */
	int32 DungeonSeed = 0;

	/** Which floor, counted from 1, the way `FCataclysmSavedFloor` counts. */
	int32 FloorNumber = 1;

	/** Which family carves it. */
	ECataclysmFloorLayout Layout = ECataclysmFloorLayout::Halls;

	/** Cells across, or 0 to let the floor's seed decide. */
	int32 Width = 0;

	/** Cells down, or 0 to let the floor's seed decide. */
	int32 Height = 0;
};

/**
 * Builds one floor from a seed.
 *
 * DETERMINISTIC, AND THAT IS THE POINT. Issue #40 requires it twice over: a
 * dungeon must look the same if the player leaves and returns, and a bug in a
 * generated floor must be reproducible from its seed alone. Everything random
 * here comes from one `FRandomStream` seeded by `SeedForFloor`, and nothing
 * reads the clock, the frame number or any global.
 *
 * ONE FLOOR AT A TIME, WITH NO STATE BETWEEN THEM. `SeedForFloor` mixes the
 * dungeon's seed with the floor number, so floor 7 can be built without floors 1
 * to 6 ever existing. That is what makes issue #40's third acceptance criterion
 * -- that a 150-floor dungeon is affordable -- a matter of never building a floor
 * the player is not standing on, rather than of making generation faster.
 *
 * WHAT IT DOES NOT DO. It produces no geometry, no actors and no map. It decides
 * which cells can be walked on, and where the player arrives and leaves. Placing
 * room art onto the result is the next piece of work and is not started.
 */
/**
 * A corridor the generator can carve between two walkable cells that are far apart to walk and close in space, and the
 * gate that closes it. Issues #1820 and #41.
 *
 * WHY IT EXISTS. Three dungeon modifier rows open a path during play: Warzone Control Points' "opening shortcuts", Soul
 * Chains' "open up new paths" and The Labrynth's paths "blocked or redirected". Measured 2026-10-02 over sixty plans
 * (docs/DECISIONS.md), the floors this generator makes are mostly tree-like: a gate on an EXISTING detour is possible
 * on 9 of 20 Halls plans, 2 of 20 Caverns and no Arena. So a path that can be opened is carved for the row and closed
 * at once, on the floors that carry the row, and opening its gate is the row's effect.
 */
struct CATACLYSM_API FCataclysmFloorShortcut
{
	/** The two walkable cells the corridor joins. */
	FIntPoint A = FIntPoint(-1, -1);
	FIntPoint B = FIntPoint(-1, -1);

	/** The cells the corridor turns from rock to floor. */
	TArray<FIntPoint> NewCells;

	/** Two of `NewCells`, side by side across the corridor. Closing both closes it. */
	TArray<FIntPoint> Gate;

	/** How many cells shorter the walk it was found for is when it is open. */
	int32 Saving = 0;

	bool IsValid() const { return Gate.Num() == 2 && NewCells.Num() >= 2; }
};

/**
 * A Halls floor divided into two or three sections, and the cells whose closing divides it. Issues #1820 and #41.
 *
 * WHAT IT IS FOR. Two dungeon modifier rows need a floor in parts: Lightforged Walls' "barriers seal sections until all
 * enemies in the area are slain" and Fragmented Reality's "certain sections of the dungeon". This is only the answer
 * to "where could the floor be divided"; nothing here closes a cell or builds a barrier.
 *
 * NO SECTIONS IS AN ANSWER. `Boundaries` and `Section` are then both empty and `SectionCount` is 0. See
 * `FCataclysmFloorGenerator::FindSections` for when that happens.
 */
struct CATACLYSM_API FCataclysmFloorSections
{
	/**
	 * The cells to close, one list for each boundary. Boundary 0 lies between sections 0 and 1, and boundary 1, when
	 * there is one, between sections 1 and 2.
	 */
	TArray<TArray<FIntPoint>> Boundaries;

	/**
	 * The same cells grouped by the line they came from: `BoundaryLines[B][L]` is line `L` of boundary `B`. Kept so
	 * a caller, and a test, can see which lines a boundary is made of without working it out again from the cells,
	 * which cannot always be done: two lines may share a cell, and two lines side by side read as lines the other way.
	 */
	TArray<TArray<TArray<FIntPoint>>> BoundaryLines;

	/**
	 * The section of every cell of the plan, indexed the way `FCataclysmFloorPlan::Cells` is. Section 0 holds the
	 * entrance and the last section holds the exit. `INDEX_NONE` for a cell that is not walkable and for a boundary
	 * cell. Empty when there are no sections.
	 */
	TArray<int32> Section;

	/** How many sections there are: one more than the boundaries, or 0 when there is no boundary. */
	int32 SectionCount() const { return Boundaries.IsEmpty() ? 0 : Boundaries.Num() + 1; }

	/** The section a cell of `Plan` is in, or `INDEX_NONE` for rock, a boundary cell, off the grid or no sections. */
	int32 SectionOf(const FCataclysmFloorPlan& Plan, FIntPoint Cell) const
	{
		const int32 Index = Plan.IndexOf(Cell);
		return Section.IsValidIndex(Index) ? Section[Index] : INDEX_NONE;
	}
};

class CATACLYSM_API FCataclysmFloorGenerator
{
public:
	// ----------------------------------------------------------------------
	// Size
	// ----------------------------------------------------------------------

	/**
	 * How wide one cell is in centimetres.
	 *
	 * A JUDGEMENT, NOT A MEASUREMENT, AND IT HAS NOT BEEN PLAYED. Four metres is
	 * about three player capsules wide, so a passage two cells across reads as a
	 * corridor two people can walk down rather than a gap. It is the number the
	 * floor's size in metres is derived from, and
	 * `tools/tests/test_a_dungeon_floor_is_a_walk_not_a_room.py` holds the
	 * result against the player's designed walking speed so that changing it has
	 * to confront what it does to how long a floor takes.
	 */
	static constexpr float CellSizeCm = 400.0f;

	/**
	 * The smallest and largest a floor may be on one axis, in cells.
	 *
	 * 32 x 4 m is 128 metres and 48 x 4 m is 192 metres.
	 *
	 * `MostFloorSide` IS WHAT THE DUNGEON LEVEL HAS TO BE BIG ENOUGH FOR. A
	 * navigation mesh is only built inside a bounds volume,
	 * `L_Dungeon` holds one sized once, and a floor that outgrew it would lose its
	 * outer ring with nothing reporting it.
	 * `tools/tests/test_the_dungeon_map_covers_a_whole_floor.py` holds them
	 * together.
	 *
	 * THE RANGE IS NOT WIDER THAN THIS FOR A REASON. 48 cells is 192 metres, and
	 * the walk to the stairs on the largest floors already reaches the top of what
	 * a two to five minute floor can carry.
	 */
	static constexpr int32 LeastFloorSide = 32;
	static constexpr int32 MostFloorSide = 48;

	/**
	 * The smallest and largest area the room splitter will divide.
	 *
	 * This decides whether a floor is a handful of great halls or two dozen
	 * ordinary rooms, and it is the knob that was most obviously fixed before:
	 * every floor had between ten and sixteen rooms.
	 */
	static constexpr int32 LeastLeafSide = 7;
	static constexpr int32 MostLeafSide = 18;

	/**
	 * The narrowest and widest a connection between two rooms may be, in cells.
	 *
	 * TWO IS THE FLOOR AND IT IS NOT NEGOTIABLE. A passage one cell wide can only
	 * be left the way it was entered, which is what
	 * `FCataclysmFloorQuality::LongestNarrowRun` counts and what the project owner
	 * asked to avoid.
	 */
	static constexpr int32 LeastConnectionWidth = 2;
	static constexpr int32 MostConnectionWidth = 4;

	/**
	 * The fewest and most connections beyond the ones that make a floor whole.
	 *
	 * ONE AT LEAST, NEVER ZERO. With none the floor is a tree, and a tree has
	 * exactly one route between any two rooms, so every side room is a trip out
	 * and back. That is the fault Diablo 4 patched out of its dungeons.
	 */
	static constexpr int32 LeastExtraConnections = 1;
	static constexpr int32 MostExtraConnections = 8;

	/** The most a room may be smaller than the space it was given, in cells. */
	static constexpr int32 MostRoomShrink = 4;

	/**
	 * How open a cavern may start, before smoothing.
	 *
	 * WIDER THAN IT WAS, because it is now where a cavern's tightness comes from.
	 * The smoothing threshold was going to carry some of that and cannot; see it
	 * below.
	 */
	static constexpr float LeastCavernFloorChance = 0.44f;
	static constexpr float MostCavernFloorChance = 0.58f;

	/** How many times a cavern's smoothing rule may be applied. */
	static constexpr int32 LeastCavernPasses = 3;
	static constexpr int32 MostCavernPasses = 6;

	/**
	 * How many solid neighbours may turn a cell solid when a cavern is smoothed.
	 *
	 * Five fills more in and six less, so this is part of what decides whether a
	 * cavern is chambers joined by necks or one wide cave.
	 *
	 * FOUR WAS TRIED AND IS OUT. It filled in so much that the largest surviving
	 * chamber was 8% of the grid on some seeds, and all eight attempts were used
	 * before one was shipped anyway, against a promise of at least 25%. It was
	 * found by the thousand-seed measurement and not by the assertions, which
	 * sweep 120 -- see the note on `SweepSeeds` in the test file.
	 */
	static constexpr int32 LeastCavernFillThreshold = 5;
	static constexpr int32 MostCavernFillThreshold = 6;

	/**
	 * How far an arena may reach across the floor, as a fraction of it.
	 *
	 * The two axes are rolled separately, so 0.6 on one and 1.0 on the other is a
	 * long arena rather than a small round one. Below 0.6 an arena stops filling
	 * enough of its floor to pass the open-space check and is simply re-rolled.
	 */
	static constexpr float LeastArenaScale = 0.60f;
	static constexpr float MostArenaScale = 1.00f;

	// ----------------------------------------------------------------------
	// Halls
	// ----------------------------------------------------------------------

	/** No room is narrower than this in either direction. */
	static constexpr int32 MinRoomSide = 5;

	// ----------------------------------------------------------------------
	// Caverns
	// ----------------------------------------------------------------------

	// ----------------------------------------------------------------------
	// Giving up
	// ----------------------------------------------------------------------

	/**
	 * The least of the grid a finished floor may be walkable.
	 *
	 * A cavern is random enough to occasionally collapse into a few small
	 * chambers, and the largest of those is not a floor worth playing.
	 */
	static constexpr float MinOpenFraction = 0.25f;

	/** How many times generation may re-roll before returning what it has. */
	static constexpr int32 MaxAttempts = 8;

	// ----------------------------------------------------------------------

	/**
	 * The seed for one floor of one dungeon.
	 *
	 * A 32-BIT INTEGER HASH RATHER THAN ADDITION. `DungeonSeed + FloorNumber`
	 * would make floor 2 of dungeon 100 the same floor as floor 1 of dungeon 101,
	 * so consecutive dungeons would share most of their floors. The mixing
	 * function is the widely used `lowbias32` finaliser; the only property needed
	 * is that neighbouring inputs give unrelated outputs.
	 *
	 * The result is always positive, so it can be handed to `FRandomStream`
	 * without a sign to reason about.
	 */
	static int32 SeedForFloor(int32 DungeonSeed, int32 FloorNumber);

	/**
	 * Rolls what one floor is like, from a stream already seeded for that floor.
	 *
	 * PUBLIC SO A TEST CAN SWEEP IT without building a floor for every roll, and
	 * so a test can show the knobs really do vary rather than that one floor
	 * happened to differ from another.
	 *
	 * The request's width and height win when set, because a test that asks for a
	 * particular size is asking about something else.
	 */
	static FCataclysmFloorShape RollShape(FRandomStream& Stream,
										 const FCataclysmFloorRequest& Request);

	/** Builds the floor the request asks for. */
	static FCataclysmFloorPlan Generate(const FCataclysmFloorRequest& Request);

	// ----------------------------------------------------------------------
	// Gated shortcuts. Issues #1820 and #41. Figures ruled 2026-10-04, each a judgement.
	// ----------------------------------------------------------------------

	/** The least a shortcut must shorten a walk, in cells, to count. Below it a gate is hard to notice. */
	static constexpr int32 ShortcutLeastSaving = 10;

	/** The most cells a shortcut's two ends may be apart, counted along rows and columns. */
	static constexpr int32 ShortcutMostLength = 12;

	/** How wide a shortcut's corridor is: the generator's least connection width. */
	static constexpr int32 ShortcutWidth = LeastConnectionWidth;

	/** How many cells `FindShortcuts` searches from, and how many of the best candidates it checks for real. */
	static constexpr int32 ShortcutSources = 40;
	static constexpr int32 ShortcutMostChecked = 100;

	/**
	 * The gated shortcut that most shortens the walk from `From` to `To`, or false when none saves
	 * `ShortcutLeastSaving`. Does not carve. `Avoid` is cells no new corridor may use.
	 *
	 * Estimated from two breadth-first searches, then the best checked by carving on a copy: the saving must be real,
	 * and a gate of two NEW cells must exist whose closing gives the first walk back with every walkable cell still
	 * reachable from the entrance.
	 */
	static bool FindShortcutBetween(const FCataclysmFloorPlan& Plan, FIntPoint From, FIntPoint To,
									const TSet<FIntPoint>& Avoid, FCataclysmFloorShortcut& Out);

	/**
	 * Up to `Most` gated shortcuts between ANY two places, sharing no cell with each other or with `Avoid`. Each saves
	 * at least `ShortcutLeastSaving` between its own two ends. Searched from `ShortcutSources` cells drawn on `Stream`,
	 * or, when `Near` is a cell, from every walkable cell within `NearCells` of it, so one end is near that cell.
	 */
	static TArray<FCataclysmFloorShortcut> FindShortcuts(const FCataclysmFloorPlan& Plan, FRandomStream& Stream,
														 int32 Most, const TSet<FIntPoint>& Avoid,
														 FIntPoint Near = FIntPoint(-1, -1), int32 NearCells = 0);

	/** Carve a shortcut's corridor, through the same carving every connection uses. Its gate's cells are then floor. */
	static void CarveShortcut(FCataclysmFloorPlan& Plan, const FCataclysmFloorShortcut& Shortcut);

	// ----------------------------------------------------------------------
	// Sections. Issues #1820 and #41. Every figure below is a judged number of 2026-10-08, by the coordinating
	// session under the owner's delegation. None has been played.
	// ----------------------------------------------------------------------

	/** The fewest and the most walkable cells in a line that may be closed. A judged number of 2026-10-08. */
	static constexpr int32 SectionLineLeastCells = 2;
	static constexpr int32 SectionLineMostCells = 6;

	/** The most lines closed together as one boundary. A judged number of 2026-10-08. */
	static constexpr int32 SectionBoundaryMostLines = 3;

	/** The most distinct cells one boundary may close. A judged number of 2026-10-08. */
	static constexpr int32 SectionBoundaryMostCells = 12;

	/** How many lines, nearest the wanted share, are offered alone and together. A judged number of 2026-10-08. */
	static constexpr int32 SectionLinesOffered = 10;

	/** How many boundaries are kept for each of the two wanted shares. A judged number of 2026-10-08. */
	static constexpr int32 SectionBoundariesKept = 8;

	/**
	 * Every area holds at least one in this many of the plan's walkable cells, rounded up: a tenth. A judged number
	 * of 2026-10-08. A whole number and not 0.1, so the rounding is exact.
	 */
	static constexpr int32 SectionLeastAreaOneIn = 10;

	/**
	 * Where a Halls floor can be divided into three sections, or failing that two, by closing short lines of cells.
	 * Does not close anything. Draws nothing at random: the same plan always gives the same answer.
	 *
	 * IT READS THE PLAN AS IT IS GIVEN. A caller with gated shortcuts passes the plan after they are carved, with
	 * their gate cells walkable. A plan whose layout is not Halls, or that is not built, has no sections.
	 *
	 * A LINE is `SectionLineLeastCells` to `SectionLineMostCells` walkable cells in a straight row, along X or along Y,
	 * with a cell that is not walkable at each end, none of them the entrance or the exit. A line is left out when the
	 * same line lies on both its sides, which is the inside of a corridor; the line at the corridor's end stands for it.
	 * A line's SHARE is the fraction of the walkable cells nearer the entrance, walked, than the nearest of its own.
	 *
	 * A BOUNDARY is one to `SectionBoundaryMostLines` lines closed together, of at most `SectionBoundaryMostCells`
	 * cells, that leaves exactly two areas, each a tenth of the walkable cells or more, the entrance in one and the
	 * exit in the other. NO LINE IS CLOSED THAT PARTS NOTHING: two or three lines are not offered together when one
	 * of them, or two of them, was already found to be a boundary among the same lines.
	 *
	 * THE ORDER. The `SectionLinesOffered` lines whose share is nearest one third are offered alone, then in twos,
	 * then in threes, and the `SectionBoundariesKept` boundaries whose entrance side is nearest a third of the cells
	 * are kept. The same again for two thirds. Each kept one-third boundary is tried with each kept two-thirds one:
	 * the pair is a division when closing both leaves exactly three areas, each a tenth or more, the entrance and
	 * the exit in different ones, and the three areas lie in a row: the third area between the two boundaries, and
	 * not a pocket to one side that both of them seal. The division whose smallest area is largest is taken; then
	 * the one closing fewer cells; then the one whose lowest cell is lower, comparing Y and then X. With no division
	 * the same offers are made at one half, and the boundary whose smaller side is largest is the answer, in two
	 * sections. With none of those there are no sections.
	 *
	 * `MayClose` IS CELLS THAT MAY BE CLOSED DURING PLAY, a shortcut gate's. Lines, shares, offers and boundaries are
	 * still found on the plan as given, with those cells open. A division, and a two-section answer, is then taken
	 * only if every rule also holds with those cells closed: the same number of areas, each a tenth or more, the
	 * entrance and the exit apart, every cell in the section it had, and each boundary alone leaving the same
	 * sections on the entrance's side. The two states are enough for every mix of open and shut gates: sections
	 * stay whole because they are whole with every gate shut, and stay apart because they are apart with every gate
	 * open. An empty list, which is the default, changes nothing and costs nothing.
	 */
	static FCataclysmFloorSections FindSections(const FCataclysmFloorPlan& Plan,
		const TArray<FIntPoint>& MayClose = TArray<FIntPoint>());
};
