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
};
