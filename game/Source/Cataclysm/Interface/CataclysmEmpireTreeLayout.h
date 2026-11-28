// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interface/CataclysmPassiveTreeLayout.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmEmpireTreeLayout.generated.h"

struct FCataclysmEmpireTreeDecorationRow;
struct FCataclysmEmpireTreeNodeRow;
class UDataTable;

/**
 * The empire upgrade tree screen's arithmetic and words, as plain functions over the generated tables. Issue #2741,
 * slice 2.
 *
 * THE CLASS TREE SCREEN'S SPLIT, AND MOST OF ITS ARITHMETIC. `UCataclysmPassiveTreeLayout` places a class tree's nodes
 * on a canvas, and everything in it that is plain geometry -- fitting a zoom, placing a point, turning a line, the
 * zoom's range, a node's mark by its kind -- is used as it is. The things it does over a table read the class trees'
 * row, so this class does them over the empire tree's: an extent, and how large the marks may be drawn.
 *
 * THE WHOLE TREE AT ONCE, AS THE OWNER BUILT IT, since 2026-11-27. The owner, that day: "Right now it looks like this
 * weird mess, not the beautiful circle json with 4 quadrants we built with the passivetreebuilder". Until then the
 * screen drew one branch at a time, each fitted to its own extent with its own node size (slice 2's S3 of 2026-11-18),
 * because every node was a 150 by 44 box holding its name and the whole tree allowed a box only 0.263 of that size. A
 * node is now a mark sized by its kind, as on the class tree screen (2026-11-24), with its name beside it on hover, and
 * the whole tree allows the marks 0.69 of their size (`MarkScaleFor`), so it is drawn whole at the tree file's own
 * positions, with the builder's rings, lines and labels (`DT_EmpireTreeDecorations`). A branch is still a view: it
 * points the same drawing at that branch's nodes.
 *
 * EVERY WORD THE SCREEN SHOWS IS BUILT HERE, so a test reads the words a player does: the automation command passes
 * `-nullrhi`, and no headless test can see a screen.
 */
UCLASS()
class CATACLYSM_API UCataclysmEmpireTreeLayout : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The view of the whole tree: what the screen opens on, and the first of the views. */
	static const FString WholeTreeView;

	/** The six views the screen's list offers: the whole tree, then `UCataclysmEmpireTree::BranchNames`'s order --
	 *  four branches, then the centre's four capstones. Every view draws the whole tree; a branch's view points it at
	 *  that branch. */
	static TArray<FString> Views();

	/** What a view is called: "The whole tree", "The centre" for the centre's capstones, or the branch's name. */
	static FString ViewLabel(const FString& View);

	/** What a view's button in the list says: its name and the points spent in it, "Architect  26 points". The list
	 *  is where each branch's points are read. */
	static FString ViewButtonLabel(const FString& View, int32 SpentInView);

	/** The rectangle a view's nodes occupy: every node for the whole tree, one branch's otherwise. `bAny` is false for
	 *  a view with no nodes. */
	static FCataclysmTreeExtent ExtentOf(const UDataTable* NodeTable, const FString& View);

	/** `Extent` grown to hold every ring, line and label of the decoration table, so a fitted whole tree shows its
	 *  outermost ring. `Extent` as it is with no table. */
	static FCataclysmTreeExtent WithDecorations(const FCataclysmTreeExtent& Extent, const UDataTable* DecorationTable);

	/** The mark a node of the empire tree is drawn as, by its kind, at a zoom of one: the class tree screen's sizes
	 *  (`UCataclysmPassiveTreeLayout::MarkSizePx`), whose kinds the empire tree's file writes in the same words. */
	static FVector2D MarkSizeFor(const FCataclysmEmpireTreeNodeRow& Row);

	/** The colour a node's mark is drawn in, by its kind: an ordinary node grey, a keystone a lighter blue-grey, a
	 *  capstone gold -- the colours of the preview the owner approved on 2026-11-27, read as sRGB. */
	static FLinearColor MarkColourFor(const FCataclysmEmpireTreeNodeRow& Row);

	/** The screen's background: the preview's near-black, opaque, so the empire map does not show through. */
	static FLinearColor BackdropColour();

	/** The clear space round the whole tree's view, in panel units: less than `UCataclysmPassiveTreeLayout::FitMarginPx`,
	 *  which leaves room for a node's name beside it, because the whole tree's outermost things are its rings, lines
	 *  and labels. A branch's view keeps the class tree's margin. At 1280 by 720 the class tree's margin left the ring
	 *  about 376 pixels across (the packaged game, 2026-11-27). */
	static constexpr float WholeTreeFitMarginPx = 24.0f;

	/** The thinnest a ring or a line is drawn, in panel units. Two, not one and a half: at a 720 pixel high window
	 *  Slate draws a panel unit two thirds of a pixel, and a piece one pixel wide turned exactly upright on a whole
	 *  pixel's edge was drawn as nothing in the packaged game (the vertical quadrant line, 2026-11-27). */
	static constexpr float ThinnestDecorationPx = 2.0f;

	/**
	 * The largest the whole tree's marks may be drawn without two overlapping: `UCataclysmPassiveTreeLayout::MarkScaleFor`'s
	 * rule over the empire tree's rows -- a pair clear on either axis misses, each mark's own size counted, the smallest
	 * over every pair, capped at one with a floor of 0.05. Measured on 2026-11-27: 0.695, set by a basic node and a
	 * keystone of the Explorer branch 26 units apart across and 21 down. One scale for every view, so a mark is the same
	 * size whichever view it is seen in.
	 */
	static float MarkScaleFor(const UDataTable* NodeTable);

	/** Every edge of the tree whose two ends are both nodes of the table, as (source, target). */
	static TArray<TPair<FName, FName>> EdgesOf(const UDataTable* NodeTable, const UDataTable* EdgeTable);

	/** How many straight pieces a ring of this radius is drawn as: one for every 8 units of radius, at least 24 and at
	 *  most 96, so the largest ring's pieces are about 48 units long and the smallest's about 16. */
	static int32 RingSegments(float Radius);

	/** The points a ring is drawn through, in the tree's units, going round once and ending where it began:
	 *  `RingSegments + 1` points, each `Radius` from the centre. */
	static TArray<FVector2D> RingPoints(FVector2D Centre, float Radius);

	/** Where a line of the decoration table ends: its start, `Length` along `AngleDegrees` (clockwise from pointing
	 *  right, y growing downwards, as the builder states it). */
	static FVector2D LineEnd(const FCataclysmEmpireTreeDecorationRow& Row);

	/** A decoration's colour, from the builder's "#RRGGBB" and its opacity. A colour that does not read is white. */
	static FLinearColor ColourOf(const FString& Hex, float Opacity);

	/** The font size a label is drawn at, in Slate's points, for its size in the tree's units and the zoom: Slate's point
	 *  is 4/3 of a pixel, so three quarters of the size in pixels, and never below 1. */
	static int32 LabelFontSize(float FontSize, float Zoom);

	/** How thick a ring or a line is drawn on the panel: its thickness at the zoom, and never thinner than
	 *  `ThinnestDecorationPx`, so a ring drawn 1 unit thick at a fitted zoom of about 0.4 is still seen. */
	static float DecorationThicknessPx(float Thickness, float Zoom);

	/**
	 * The bank, as the screen's first line says it: "12.5 empire upgrade points banked: 12 to spend", the bank rounded
	 * down to a tenth as the map shows it and the whole points a node can take. A Solo Self-Found character's adds
	 * "; this character's own", since its tree is shared with no other character.
	 */
	static FString BankLine(double Banked, bool bTheCharactersOwn);

	/** The view being looked at, and the points in it and in the whole tree: "Architect: 26 points spent here, 26 in
	 *  the whole tree"; for the whole tree, "The whole tree: 26 points spent". "1 point", not "1 points". */
	static FString BranchLine(const FString& View, int32 SpentHere, int32 SpentInTree);

	/** A node's name and points, "Structural Integrity  3/10": what is drawn beside the mark of the node hovered or
	 *  selected, as a class tree's node's name is (`UCataclysmPassiveTreeLayout::NameCaption`). */
	static FString NodeLabel(const FCataclysmEmpireTreeNodeRow& Row, int32 Points);

	/**
	 * What the line under the branch says about one node: its name, branch and points, the tree file's description,
	 * a centre capstone's three options with the chosen one marked, Imperial Prowess's extra option marked "(taken with
	 * Imperial Prowess)" (issue #2741 slice 8), and -- when it is not empty -- why a point cannot go in now
	 * (`UCataclysmEmpireTree::RefusalForSpending`'s sentence).
	 *
	 * @param ExtraOption  the extra option taken on this node, 0 for none
	 * @param ExtraGranter what grants it, "Imperial Prowess"
	 */
	static FString NodeDetail(const FCataclysmEmpireTreeNodeRow& Row, int32 Points, int32 ChosenOption,
							  const FString& Refusal, int32 ExtraOption = 0, const FString& ExtraGranter = FString());

	/** What one of a centre capstone's options says in the list: "2  The Delver", "(chosen)" after the one taken,
	 *  "(taken with Imperial Prowess)" after the extra one (`TakenWith`, issue #2741 slice 8), and "waits on ..." after
	 *  one that waits on a system. */
	static FString OptionLabel(int32 Option, const FString& Name, bool bChosen, const FString& WaitsOn,
							   const FString& TakenWith = FString());

	/**
	 * What the Respec button says. "Respec: 5 days of the run" until it is pressed; "Press again to respec: 5 days of the
	 * run" while the second press is awaited. The two presses are the coordinating session's S1 of 2026-11-18: a respec
	 * costs days and cannot be undone.
	 */
	static FString RespecLabel(int32 Days, bool bAwaitingTheSecondPress);
};
