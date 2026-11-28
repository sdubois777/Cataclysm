// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmPassiveTreeLayout.generated.h"

class UDataTable;

/**
 * What a class tree's node is drawn as, by its kind. Since 2026-11-24, for the class-tree sigils.
 *
 * AN ORDINARY NODE, A KEYSTONE AND A VOW (THE TREE FILES' "capstone") EACH HAVE THEIR OWN MARK, so the shape a tree is
 * laid out in reads on screen: until then every node was the same 150 by 44 labelled box, and the boxes hid the shape
 * the nodes made.
 */
enum class ECataclysmTreeMark : uint8
{
	Ordinary,
	Keystone,
	Vow,
};

/**
 * The rectangle a tree's nodes occupy, in the authoring tool's own coordinates.
 *
 * NOT IN PIXELS. The class trees are laid out in `C:\Projects\PassiveTreeCreator` on a canvas of its own, and those
 * numbers come through `game/Data/PassiveNodes.csv` untouched. The trees span from about 1,600 to 2,600 units across,
 * so a tree has to be scaled to fit a screen rather than drawn at its authored size.
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
 * told and nothing more. Which point of a 2,500-unit-wide authored tree a
 * 2,400-pixel-wide panel should show, where a node at (-1000, 800) lands once it
 * is scaled and panned, and how big its mark is, is this class.
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
	 * The height of the labelled box every tree node was drawn as, in pixels at a zoom of one: the height the choice
	 * button's designed font size was made for. A mark's spent count is scaled by the mark's height over this one, on
	 * both screens.
	 *
	 * THE BOX ITSELF IS GONE. A class tree drew every node as a 150 by 44 box until 2026-11-24 and the empire tree until
	 * 2026-11-27; both now draw marks (below), so the box's width and its 3 px edge were removed with the last screen
	 * that read them.
	 */
	static constexpr float NodeHeightPx = 44.0f;

	/**
	 * How big a class tree's mark is drawn at a zoom of one, in pixels, by kind. Since 2026-11-24.
	 *
	 * A KEYSTONE IS LARGER THAN AN ORDINARY NODE AND A VOW IS WIDER THAN IT IS TALL, so the three kinds read apart by
	 * shape as well as size: a square, a larger square, and a wide bar. A mark holds at most its spent count ("2/3");
	 * the node's name is drawn beside it on hover or selection (`NameCaption`).
	 */
	static constexpr float OrdinaryMarkPx = 28.0f;
	static constexpr float KeystoneMarkPx = 48.0f;
	static constexpr float VowMarkWidthPx = 72.0f;
	static constexpr float VowMarkHeightPx = 40.0f;

	/**
	 * How thick a class tree's edge is drawn, in pixels at a zoom of one. Since 2026-11-24.
	 *
	 * THICK ENOUGH THAT A GAP READS AS A GAP. Two marks side by side with no line between them are not joined, and a
	 * 3 px line at a fitted zoom of about 0.4 was a pixel; ten keeps a line visible at that zoom.
	 */
	static constexpr float MarkEdgeThicknessPx = 10.0f;

	/** The gap between a mark's right edge and its name, in pixels. */
	static constexpr float CaptionGapPx = 8.0f;

	/**
	 * Clear space left around a tree when the view is fitted to it.
	 *
	 * WITHOUT IT THE OUTERMOST NODES TOUCH THE EDGE, and a node is drawn
	 * centred on its position, so half of it would be off the panel.
	 */
	static constexpr float FitMarginPx = 90.0f;

	/** The mark a node of this kind is drawn as: "keystone" and "capstone" (a vow) by name, anything else ordinary. */
	static ECataclysmTreeMark MarkFor(const FString& Kind);

	/** How wide and tall a mark of this kind is at a zoom of one, in pixels. */
	static FVector2D MarkSizePx(ECataclysmTreeMark Mark);

	/** The mark size of one node of the table, by its kind. An ordinary mark's for a node the table does not have. */
	static FVector2D MarkSizeForNode(const UDataTable* NodeTable, FName Node);

	/**
	 * The largest a tree's marks may be drawn without two of them overlapping. Since 2026-11-24, in place of the box
	 * rule (`NodeScaleFor`) that shrank every node of a tree to the box its closest pair allowed.
	 *
	 * A NUMBER TO MULTIPLY EVERY MARK'S SIZE BY. Two marks miss each other when they are clear on either axis, so a pair
	 * separated by `dx` and `dy` permits any scale up to `max(dx / ((wA + wB) / 2), dy / ((hA + hB) / 2))`, each mark's
	 * own size counted, and the tree permits the smallest of those over every pair. Measured on 2026-11-24, every class
	 * tree in `PassiveNodes.csv` and all nine sigil drafts permit 1: the marks are small enough that no tree crowds them.
	 * The rule stays so a tree laid out closer is drawn smaller rather than overlapping.
	 *
	 * CAPPED AT ONE, AND A FLOOR OF 0.05 so two nodes on one point leave a mark that can still be clicked.
	 *
	 * @return 1 for a tree with fewer than two nodes, which cannot overlap
	 */
	static float MarkScaleFor(const UDataTable* NodeTable, const FString& Tree);

	/** What a mark says on itself: its spent count, "2/3", for a node holding points, and nothing for one holding none. */
	static FString MarkCaption(int32 Points, int32 MaxPoints);

	/** What is drawn beside a hovered or selected node: its name and its count, "Shield Mastery  2/3". */
	static FString NameCaption(const FString& Name, int32 Points, int32 MaxPoints);

	/**
	 * Where a node's name is drawn: beside its mark's right edge, level with its centre.
	 *
	 * @param MarkCentre  the mark's centre on the panel
	 * @param MarkSize    the mark's size on the panel, scale and zoom applied
	 * @return the point the caption's left edge sits on, halfway up it
	 */
	static FVector2D NameCaptionPosition(FVector2D MarkCentre, FVector2D MarkSize);

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

	/** The same, with a margin of the caller's instead of `FitMarginPx`: the empire tree's whole view, whose outermost
	 *  things are its rings and labels rather than nodes with a name to show beside them. */
	static float ZoomToFit(const FCataclysmTreeExtent& Extent,
						   FVector2D CanvasSize, float MarginPx);

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
