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

	/** Cells across, or 0 for `FCataclysmFloorGenerator::DefaultWidth`. */
	int32 Width = 0;

	/** Cells down, or 0 for `FCataclysmFloorGenerator::DefaultHeight`. */
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
	 * `tools/tests/test_a_floor_is_crossed_in_the_designed_time.py` holds the
	 * result against the player's designed walking speed so that changing it has
	 * to confront what it does to how long a floor takes.
	 */
	static constexpr float CellSizeCm = 400.0f;

	/** Cells across, unless the request says otherwise. 40 x 4 m is 160 metres. */
	static constexpr int32 DefaultWidth = 40;

	/** Cells down, unless the request says otherwise. */
	static constexpr int32 DefaultHeight = 40;

	// ----------------------------------------------------------------------
	// Halls
	// ----------------------------------------------------------------------

	/**
	 * The smallest area the splitter will divide, in cells.
	 *
	 * THIS IS WHAT KEEPS ROOMS LARGE. The grid is split in half repeatedly and
	 * stops when neither half would be this big, so no room is ever smaller than
	 * this less its walls. Diablo 3 reached the same place from the other
	 * direction: its tiles are large and its entrances offset, so the grid
	 * underneath is not obvious. Lowering this is how a floor becomes the warren
	 * of small rooms the project owner asked to avoid.
	 */
	static constexpr int32 MinLeafSide = 8;

	/** No room is narrower than this in either direction. */
	static constexpr int32 MinRoomSide = 5;

	/**
	 * How many cells wide a connection between two rooms is.
	 *
	 * TWO, AND NOT ONE, ON PURPOSE. A passage one cell wide is a passage the
	 * player can only enter and leave the way they came, and it is what
	 * `FCataclysmFloorQuality::NarrowCells` counts. Two is the smallest width
	 * that is not that.
	 */
	static constexpr int32 ConnectionWidth = 2;

	/**
	 * Connections added beyond the ones that make the floor connected.
	 *
	 * WITHOUT THESE THE FLOOR IS A TREE, and a tree has exactly one route between
	 * any two rooms, so every side room is a trip out and back. That is the fault
	 * Diablo 4 patched out of its dungeons. Each extra connection closes a loop.
	 */
	static constexpr int32 ExtraConnections = 3;

	// ----------------------------------------------------------------------
	// Caverns
	// ----------------------------------------------------------------------

	/**
	 * How much of the grid starts as floor before smoothing.
	 *
	 * MEASURED, NOT PICKED. At 0.55 the smoothing settles into a single open
	 * mass: the sweep in `Cataclysm.Dungeon.MeasureWhatEachLayoutProduces` put
	 * every cavern above 73% walkable and every walk to the stairs between 63 and
	 * 72 cells, so one cavern looked much like the next and much like an arena.
	 * Lower leaves more rock standing between the chambers.
	 */
	static constexpr float CavernInitialFloorChance = 0.48f;

	/** How many times the smoothing rule is applied. */
	static constexpr int32 CavernSmoothingPasses = 5;

	/**
	 * How many of a cell's eight neighbours must be solid for it to become solid.
	 *
	 * The usual cellular-automaton cave rule. Five of eight is the value that
	 * settles into rounded chambers rather than into noise or into a solid block.
	 */
	static constexpr int32 CavernSolidNeighboursToFill = 5;

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

	/** Builds the floor the request asks for. */
	static FCataclysmFloorPlan Generate(const FCataclysmFloorRequest& Request);
};
