// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Math/Box2D.h"

#include "CataclysmFloorMap.generated.h"

struct FCataclysmFloorPlan;

/**
 * One run of seen walkable cells: cells `(X, FirstY)` to `(X, LastY)` inclusive.
 *
 * ALONG CELL Y AT ONE CELL X, BECAUSE THAT IS A ROW ON THE SCREEN. The camera looks along world +X, so +X is up on the
 * map and +Y is right (`UCataclysmFloorMap::ScreenOf`), and one cell X is one horizontal band of the map. A run is
 * drawn as one rectangle, which is what keeps the map's cost per frame at the number of runs rather than the number
 * of cells.
 */
struct CATACLYSM_API FCataclysmFloorMapRun
{
	int32 X = 0;
	int32 FirstY = 0;
	int32 LastY = 0;

	bool operator==(const FCataclysmFloorMapRun& Other) const
	{
		return X == Other.X && FirstY == Other.FirstY && LastY == Other.LastY;
	}
};

/**
 * Where the map is on the screen, and how large a cell is on it, in pixels.
 *
 * A SQUARE CENTRED ON THE SCREEN, WITH THE PLAYER AT ITS CENTRE. `Centre` is both: the map is drawn at a fixed scale
 * that moves with the player, the owner's choice of 2026-11-21 ("Centred on you").
 */
struct CATACLYSM_API FCataclysmFloorMapLayout
{
	/** The middle of the screen, which is where the player's own position is drawn. */
	FVector2D Centre = FVector2D::ZeroVector;

	/** Half the square's side. Nothing is drawn beyond it. */
	float HalfSide = 0.0f;

	/** How many pixels one 4 metre cell is across. */
	float PxPerCell = 0.0f;

	/** The square itself. */
	FBox2D Area() const
	{
		return FBox2D(Centre - FVector2D(HalfSide, HalfSide), Centre + FVector2D(HalfSide, HalfSide));
	}
};

/**
 * Tab shows a map of the current floor over the game, so the player can see where they have been. Issue #2805, from
 * the owner's playtest of 2026-11-21: "Tab brings up a map overlay so you can make sure you aren't running in circles".
 *
 * WHAT IS DRAWN. The walkable cells the player has seen, at 40% opacity; the player, at the centre, pointing the way
 * they face; and the stairs down once their cell has been seen -- at the map's edge, in their direction, when they lie
 * beyond it. No creatures and no items: the owner chose "No enemies", 2026-11-21.
 *
 * WHAT COUNTS AS SEEN. Every cell within `RevealRadiusCells` of the player's cell that a straight line on the grid
 * reaches without crossing a Solid cell. Recorded on `ACataclysmDungeonFloor` and gone with the floor; not saved.
 *
 * THE GAME KEEPS RUNNING UNDERNEATH, as Diablo II's Automap does: "Note that gameplay doesn't stop when you bring up
 * the Automap" (classic.battle.net, Diablo II controls). `docs/DECISIONS.md` records the sources.
 *
 * EVERY JUDGEMENT IS A STATIC OVER PLAIN VALUES, for the reason `UCataclysmCombatOverlay` gives: `DrawHUD` never runs
 * under -nullrhi, so no test can see the map drawn. `ACataclysmHUD::DrawFloorMap` asks these and only draws.
 */
UCLASS()
class CATACLYSM_API UCataclysmFloorMap : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ------------------------------------------------------------------
	// What has been seen
	// ------------------------------------------------------------------

	/**
	 * How far around the player a cell is seen, in cells. Three cells is 12 metres, about what the camera shows
	 * around the player at its default distance. A judgement, accepted 2026-11-21.
	 */
	static constexpr int32 RevealRadiusCells = 3;

	/**
	 * Marks every cell within `RadiusCells` of `From` that `LineOfSight` reaches as seen, sizing `Seen` to the plan
	 * first if it is not. A cell is within the radius when its centre is, measured straight.
	 *
	 * @return how many cells were newly seen; nothing, from a cell off the grid or one that is not walkable
	 */
	static int32 RevealAround(const FCataclysmFloorPlan& Plan, TBitArray<>& Seen, FIntPoint From, int32 RadiusCells);

	/**
	 * Whether a straight line on the grid from `From` to `To` crosses no Solid cell. The two ends are not asked, so a
	 * wall itself can be seen; what is behind it cannot.
	 */
	static bool LineOfSight(const FCataclysmFloorPlan& Plan, FIntPoint From, FIntPoint To);

	/** Whether a cell has been seen. Off the grid, or a record of the wrong size, answers false. */
	static bool IsSeen(const FCataclysmFloorPlan& Plan, const TBitArray<>& Seen, FIntPoint Cell);

	/** The seen walkable cells as runs along cell Y, in order of X then Y. Nothing unseen and nothing Solid. */
	static TArray<FCataclysmFloorMapRun> RunsOf(const FCataclysmFloorPlan& Plan, const TBitArray<>& Seen);

	// ------------------------------------------------------------------
	// Where it is drawn
	// ------------------------------------------------------------------

	/** The map's square is this share of the screen's shorter side. A judgement, accepted 2026-11-21. */
	static constexpr float AreaShareOfShorterSide = 0.7f;

	/**
	 * How many pixels a cell is across on a screen 1080 pixels high; it scales with the screen's height.
	 *
	 * A JUDGEMENT, set 2026-11-21. The owner chose a fixed scale that moves with the player for "Larger detail", so it
	 * has to be larger than fitting the whole floor, which for a 48 cell floor in a 756 pixel square is about 16 a
	 * cell. At 24 the square shows about 31 cells across, 126 metres: most of a floor, with the far parts off its edge.
	 */
	static constexpr float PxPerCellAt1080 = 24.0f;

	/** The screen height `PxPerCellAt1080` is stated at. */
	static constexpr float ReferenceHeightPx = 1080.0f;

	/** Where the map is on a screen of this size. */
	static FCataclysmFloorMapLayout LayoutFor(const FVector2D& ScreenPx);

	/**
	 * Where a point in cell coordinates is drawn, in pixels. The player's position is at the centre; world +X is up and
	 * world +Y is right, as the camera shows them. Cell coordinates are fractional: a cell's centre is a whole number.
	 */
	static FVector2D ScreenOf(const FVector2D& CellPosition, const FVector2D& PlayerCell,
							  const FCataclysmFloorMapLayout& Layout);

	/** The rectangle a run covers on the screen, before clipping. */
	static FBox2D BoxOf(const FCataclysmFloorMapRun& Run, const FVector2D& PlayerCell,
						const FCataclysmFloorMapLayout& Layout);

	/**
	 * Clips a rectangle to the map's square.
	 *
	 * @return false, leaving nothing to draw, when the rectangle lies wholly outside it
	 */
	static bool ClipToArea(FBox2D& Box, const FCataclysmFloorMapLayout& Layout);

	/**
	 * Where a marker for a point is drawn: the point itself when it is inside the square, and otherwise the place on the
	 * square's edge in its direction from the player. A judgement, accepted 2026-11-21, so the stairs can be found when
	 * they lie off the map.
	 *
	 * @param bOutOnTheEdge set to whether it was pulled in to the edge
	 */
	static FVector2D MarkerAt(const FVector2D& CellPosition, const FVector2D& PlayerCell,
							  const FCataclysmFloorMapLayout& Layout, bool& bOutOnTheEdge);

	/**
	 * The three corners of the player's marker: a triangle pointing the way the player faces, at the centre.
	 *
	 * @param YawDegrees the player's facing in the world; 0 is world +X, which is up on the map
	 */
	static void FacingTriangle(const FVector2D& Centre, float YawDegrees, float SizePx, FVector2D OutCorners[3]);

	/** How large the player's marker is, in pixels on a screen 1080 high. */
	static constexpr float PlayerMarkerPxAt1080 = 12.0f;

	/** How large the stairs' marker is, in pixels on a screen 1080 high. */
	static constexpr float ExitMarkerPxAt1080 = 10.0f;

	// ------------------------------------------------------------------
	// When it is drawn, and in what colours
	// ------------------------------------------------------------------

	/**
	 * Whether the map is drawn this frame: the player has it shown, there is a floor, and no full screen -- the
	 * inventory, the character sheet, the passive tree, the empire's screens -- is open over the game. A judgement,
	 * accepted 2026-11-21: a map under a screen is a map nobody can read.
	 */
	static bool ShouldDraw(bool bShown, bool bHasFloor, bool bAFullScreenIsOpen);

	/** The opacity of the seen floor. 40%, inside the 30 to 50% players report setting in Path of Exile. */
	static constexpr float FloorOpacity = 0.4f;

	/** The seen floor, the player and the stairs, as hex colours. Judgements. */
	static const TCHAR* FloorHex;
	static const TCHAR* PlayerHex;
	static const TCHAR* ExitHex;
};
