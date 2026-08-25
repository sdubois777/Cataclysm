// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmPassiveTreeLayout.generated.h"

class UDataTable;

/**
 * The rectangle a tree's nodes occupy, in the authoring tool's own coordinates.
 *
 * NOT IN PIXELS. The four class trees are laid out in
 * `C:\Projects\PassiveTreeCreator` on a canvas of its own, and those numbers
 * come through `game/Data/PassiveNodes.csv` untouched. The Masochist tree is
 * 3,600 units across and the Berserker one 1,622, so a tree has to be scaled to
 * fit a screen rather than drawn at its authored size.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmTreeExtent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Passives")
	FVector2D Least = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Passives")
	FVector2D Most = FVector2D::ZeroVector;

	/** Whether any node was found at all. An empty extent has no centre. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Passives")
	bool bAny = false;

	/** The middle of the rectangle, which is what the view starts looking at. */
	FVector2D Centre() const { return (Least + Most) * 0.5; }

	/** How wide and tall it is. Zero on either axis for a tree in a line. */
	FVector2D Span() const { return Most - Least; }
};

/**
 * Where a passive tree's nodes and edges go on screen.
 *
 * WHY IT IS A SEPARATE CLASS FROM THE WIDGET, which is the reason
 * `UCataclysmInventoryScreen` is separate from `UCataclysmInventoryWidget`: the
 * automation test command in `tools/unreal_build.py` passes `-nullrhi`, so
 * nothing that reaches the screen can be watched by a test. Everything here is
 * arithmetic over plain values, so all of it is covered while the drawing itself
 * stays uncovered.
 *
 * THE ONE THING SLATE CANNOT WORK OUT. A canvas panel places a child where it is
 * told and nothing more. Which point of a 3,600-unit-wide authored tree a
 * 2,400-pixel-wide panel should show, and where a node at (-2000, 1200) lands
 * once it is scaled and panned, is this class.
 */
UCLASS()
class CATACLYSM_API UCataclysmPassiveTreeLayout : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * How far in and out the view may be scaled.
	 *
	 * THE FLOOR IS NOT A READABLE SIZE AND IS NOT MEANT TO BE. At a tenth, the
	 * widest tree is 360 pixels across and a node is a dot. It is there so that
	 * a player who keeps scrolling out cannot reach a zoom of zero, which would
	 * put every node on one point and divide by nothing.
	 *
	 * THE CEILING IS A READABLE SIZE. At three, one node fills a good part of
	 * the panel, which is as close as reading a single node's words needs.
	 */
	static constexpr float SmallestZoom = 0.1f;
	static constexpr float LargestZoom = 3.0f;

	/** How much one notch of the mouse wheel changes the zoom, as a factor. */
	static constexpr float ZoomPerNotch = 1.15f;

	/**
	 * How wide and tall a node is drawn at a zoom of one, in pixels.
	 *
	 * A NODE HAS TO HOLD ITS OWN NAME, which is why it is this wide rather than
	 * the 16 by 16 the authoring tool draws. `Retributive Instinct` and
	 * `Iron Will, but For the Soul` are node names, and a tree of unlabelled
	 * dots would say nothing about what any limb does.
	 */
	static constexpr float NodeWidthPx = 150.0f;
	static constexpr float NodeHeightPx = 44.0f;

	/** How thick an edge is drawn, in pixels at a zoom of one. */
	static constexpr float EdgeThicknessPx = 3.0f;

	/**
	 * Clear space left around a tree when the view is fitted to it.
	 *
	 * WITHOUT IT THE OUTERMOST NODES TOUCH THE EDGE, and a node is drawn
	 * centred on its position, so half of it would be off the panel.
	 */
	static constexpr float FitMarginPx = 90.0f;

	/**
	 * The largest a node may be drawn without two of them overlapping.
	 *
	 * A NUMBER TO MULTIPLY `NodeWidthPx` AND `NodeHeightPx` BY, so the node
	 * keeps its shape and only its size moves.
	 *
	 * WHY IT IS DERIVED RATHER THAN CHOSEN. The four trees were laid out in
	 * `C:\Projects\PassiveTreeCreator` with a spacing in mind, and it is not
	 * the same spacing in each: measured on 2026-08-25, three of them allow a
	 * node of about 160 by 47 units and the Bulwark tree allows only 74 by 22,
	 * because two of its nodes sit 74 units apart. A single chosen size would
	 * either overlap in that tree or waste most of the room in the other three.
	 *
	 * TWO BOXES MISS EACH OTHER WHEN THEY ARE CLEAR ON EITHER AXIS, so a pair
	 * separated by `dx` and `dy` permits any scale up to
	 * `max(dx / width, dy / height)`, and the tree permits the smallest of those
	 * over every pair.
	 *
	 * CAPPED AT ONE. A node bigger than its designed size gains nothing: the
	 * name inside it is already fully visible at one.
	 *
	 * @return 1 for a tree with fewer than two nodes, which cannot overlap
	 */
	static float NodeScaleFor(const UDataTable* NodeTable, const FString& Tree);

	/** The rectangle one tree's nodes occupy. `bAny` is false for a tree with
	 *  no nodes, which has no centre and cannot be fitted to. */
	static FCataclysmTreeExtent ExtentOf(const UDataTable* NodeTable,
										 const FString& Tree);

	/**
	 * The zoom at which the whole tree fits inside a panel of this size.
	 *
	 * THE SMALLER OF WHAT THE WIDTH AND THE HEIGHT ALLOW, so the tree fits both
	 * ways rather than one. A tree wider than it is tall is bound by the width
	 * on an ordinary monitor, and all four of them are.
	 *
	 * @return the clamped zoom. A panel too small for the margin, or a tree with
	 *         no extent, answers the smallest zoom rather than zero or infinity
	 */
	static float ZoomToFit(const FCataclysmTreeExtent& Extent,
						   FVector2D CanvasSize);

	/**
	 * Where a node sits on the panel, as the CENTRE of the node.
	 *
	 * THE CENTRE AND NOT THE CORNER, because a node is placed on a canvas slot
	 * whose alignment is the middle. Scaling around a corner moves a node as the
	 * view zooms even when it is the thing being zoomed towards.
	 *
	 * @param Focus  the point in the tree's own coordinates that sits in the
	 *               middle of the panel
	 */
	static FVector2D ScreenPositionFor(FVector2D TreePosition, FVector2D Focus,
									   float Zoom, FVector2D CanvasSize);

	/** The reverse: which point of the tree is under this point of the panel. */
	static FVector2D TreePositionFor(FVector2D ScreenPosition, FVector2D Focus,
									 float Zoom, FVector2D CanvasSize);

	/**
	 * Where to put a straight line joining two points on the panel.
	 *
	 * DRAWN AS A RECTANGLE TURNED TO POINT AT THE FAR END, because UMG has no
	 * line at all. An image is placed at the near end, made as long as the gap
	 * and as thick as the line wants to be, and rotated. That needs its pivot at
	 * the middle of its left edge, which is what `EdgePivot` below is.
	 *
	 * @param OutSize   length along the line, then thickness across it
	 * @param OutAngle  degrees, measured the way `SetRenderTransformAngle` wants
	 *                  them: clockwise from pointing right, because screen y
	 *                  grows downwards
	 */
	static void EdgeGeometry(FVector2D From, FVector2D To, float Thickness,
							 FVector2D& OutPosition, FVector2D& OutSize,
							 float& OutAngle);

	/** The pivot an edge image is rotated around: the middle of its left edge. */
	static FVector2D EdgePivot() { return FVector2D(0.0, 0.5); }

	/** Keep a zoom inside the range, whatever produced it. */
	static float ClampZoom(float Zoom);

	/** The zoom after this many wheel notches. Positive notches zoom in. */
	static float ZoomAfterNotches(float Zoom, float Notches);
};
