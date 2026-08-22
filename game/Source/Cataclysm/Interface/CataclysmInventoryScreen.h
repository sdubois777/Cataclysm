// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmInventoryScreen.generated.h"

struct FCataclysmCarriedSlot;
class UDataTable;

/**
 * Every judgement the carried inventory screen makes, outside the widget.
 *
 * WHY IT IS A SEPARATE CLASS FROM UCataclysmInventoryWidget, which is the same
 * reason UCataclysmCombatOverlay is separate from ACataclysmHUD. The automation
 * test command in tools/unreal_build.py passes -nullrhi, so nothing that reaches
 * the screen can be watched by a test. A widget does not move that wall; issue
 * #650 says so outright. Everything here is a static function over plain values,
 * so all of it is covered while the drawing itself stays uncovered.
 *
 * WHAT MOVED OUT OF HERE WHEN THE SCREEN BECAME A WIDGET, issue #735. Where the
 * panel sits, where each of the 48 cells sits, how a long label breaks into
 * lines, and whether a point is over the panel were all arithmetic this class
 * did because a canvas draw has no layout. Slate does all four properly:
 * UUniformGridPanel places the cells, UTextBlock wraps by measured width rather
 * than by counting characters, and a widget knows its own geometry. Deleting
 * them was the point of the port rather than a loss of cover.
 *
 * WHAT DID NOT MOVE is everything that is a judgement about the game rather than
 * about layout: what a slot's label says, what colour and how thick its frame is,
 * what the header line reads, and the one layout rule Slate cannot know -- that a
 * cell is square and that twelve of them have to fit the viewport.
 *
 * WHAT THIS SCREEN IS AND IS NOT. It is issue #731 as ported by issue #735: the
 * four rows of twelve, the contents of each slot, and a key that opens and closes
 * it. It shows and does nothing else. Moving an item between slots, dropping one,
 * equipping one (issue #46), a tooltip carrying the item's whole name (issue
 * #733), sorting and the stash (issue #529) are all elsewhere. How it looks is
 * issue #734.
 */
UCLASS()
class CATACLYSM_API UCataclysmInventoryScreen : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	//~ Layout, in pixels at an unscaled viewport.

	/**
	 * The largest a cell is drawn, whatever the viewport can afford.
	 *
	 * WITHOUT A CEILING THE GRID GROWS WITH THE SCREEN and on a wide monitor 48
	 * cells would each be the size of a playing card, which is not more
	 * readable, only bigger. Every game in the genre fixes its slot size and
	 * lets the surrounding space grow instead.
	 */
	static constexpr float MaxCellPx = 112.0f;

	/** Clear space between one cell and the next, in both directions. */
	static constexpr float CellGapPx = 6.0f;

	/** Clear space between the panel's edge and the grid inside it. */
	static constexpr float PanelPaddingPx = 18.0f;

	/**
	 * Room reserved above the grid for the line saying how full the bag is.
	 *
	 * A FIGURE RATHER THAN THE HEADER'S ACTUAL HEIGHT. The header is a text
	 * block in a vertical box and Slate sizes it from its font, so nothing has
	 * to be told how tall it is. CellSizeFor still has to reserve the space
	 * before any of it exists, because it answers how big a cell can be.
	 */
	static constexpr float HeaderHeightPx = 32.0f;

	/**
	 * The largest share of the viewport the panel may take, each way.
	 *
	 * THE GRID HAS TO FIT ON THE SCREEN AND IT IS TWELVE CELLS WIDE, which is
	 * three times as wide as it is tall. Width is therefore what binds on an
	 * ordinary monitor and height never is, but both are checked so that no
	 * viewport shape puts a cell off the edge.
	 */
	static constexpr float PanelWidthShare = 0.86f;
	static constexpr float PanelHeightShare = 0.72f;

	/**
	 * How big one cell is for a viewport of this size.
	 *
	 * THE ONE LAYOUT RULE SLATE CANNOT WORK OUT. A UUniformGridPanel makes every
	 * cell the same size and it sizes them from their contents, so a long item
	 * name would make all 48 wider and push the grid off the edge. Each cell is
	 * therefore given an exact size, and this is where that size comes from: a
	 * square, no bigger than MaxCellPx, and small enough that twelve of them plus
	 * the padding fit inside the panel's share of the viewport.
	 *
	 * THE SMALLER OF WHAT THE WIDTH AND THE HEIGHT ALLOW, capped at MaxCellPx,
	 * and never below one pixel. The floor is not a readable size and is not
	 * meant to be: it only stops a viewport too small to hold the padding from
	 * producing a negative cell, which Slate would treat as an error.
	 */
	static float CellSizeFor(float ViewportWidth, float ViewportHeight);

	/**
	 * How many further columns of cells sit beside the carried grid.
	 *
	 * THE PANEL OF WORN GEAR, issue #831, which is three columns: the
	 * armour head to foot, the eight rings, and the two weapon slots.
	 *
	 * IT IS STATED HERE BECAUSE CellSizeFor HAS TO FIT THEM. A cell size
	 * worked out for twelve columns and then used for fifteen makes a
	 * panel wider than the share of the viewport it was given, and on a
	 * small window that runs off the edge.
	 *
	 * Cataclysm.GearPanel.TheScreenReservesRoomForEveryGearColumn checks
	 * this against UCataclysmGearPanel::Columns, so the two cannot drift.
	 */
	static constexpr int32 ColumnsBeside = 3;

	/**
	 * The smallest a cell is allowed to be.
	 *
	 * A FRAME IS DRAWN AS PADDING INSIDE ITS CELL, so a cell smaller than
	 * twice the thickest frame has no interior left for its label. The
	 * thickest is 8 pixels, on a Cataclysmic item and on a tier 5 material,
	 * so 20 leaves four pixels of interior at the very worst.
	 *
	 * IT USED TO BE ONE PIXEL, which was a guard against a negative cell
	 * rather than a readable size, and it was harmless while the panel held
	 * twelve columns. Adding the three of the worn gear panel (issue #831)
	 * made a 400 by 300 window produce a cell of 14.9, and a Cataclysmic
	 * item in one would have been a solid square of colour with the label
	 * squeezed to nothing.
	 *
	 * A WINDOW TOO SMALL FOR THE PANEL NOW OVERFLOWS RATHER THAN SHRINKING
	 * BELOW THIS, and that is the deliberate trade. Both are wrong at a
	 * window that small; a panel running past the edge can at least be
	 * read where it is visible, and a cell with no interior cannot be read
	 * anywhere. The smallest window this actually costs anything at is
	 * below 640 wide, and at 1024 by 768 the cell is 50 pixels.
	 */
	static constexpr float SmallestCellPx = 20.0f;

	//~ Type sizes, in points.

	/**
	 * How much of a cell's height its label's font takes.
	 *
	 * IN PROPORTION TO THE CELL, so the same words fit whatever the viewport is
	 * and the grid reads the same at every resolution. A fixed size would fit a
	 * whole name on a wide monitor and two letters on a small one.
	 */
	static constexpr float LabelFontShareOfCell = 0.13f;

	/** The smallest a label is drawn, below which it is not text any more. */
	static constexpr int32 SmallestLabelFontPx = 6;

	/** The font size a cell's label is drawn at, for a cell of this size. */
	static int32 LabelFontSizeFor(float CellPx);

	/**
	 * How much of a cell's height a stack count's font takes.
	 *
	 * LARGER THAN THE LABEL'S, deliberately. How many of a material are carried
	 * is one of the two things this screen exists to show, and at the label's
	 * size the figure sat in a corner small enough to miss. It is also a figure
	 * rather than a word, so it needs less room to be read.
	 */
	static constexpr float QuantityFontShareOfCell = 0.16f;

	/** The font size a stack count is drawn at, for a cell of this size. */
	static int32 QuantityFontSizeFor(float CellPx);

	/**
	 * The font size the header line is drawn at.
	 *
	 * FIXED, WHERE A CELL'S LABEL SCALES WITH ITS CELL. The header is one line
	 * and its band is a fixed HeaderHeightPx, so shrinking it with the grid
	 * would make it unreadable on a small viewport for no gain in room.
	 */
	static constexpr int32 HeaderFontPx = 16;

	//~ What a cell says.

	/**
	 * What a slot's cell is labelled with.
	 *
	 * FOR GEAR, THE ITEM BASE'S OWN NAME and not the whole item name. A cell is
	 * about a hundred pixels across and `Cataclysmic Greatsword of Malice` is
	 * not going into it, while `Greatsword` is exactly what an icon would say in
	 * a game that had one. The rarity that the full name opens with is already
	 * on the cell twice over, as the frame's colour and its thickness. Issue
	 * #733 is the tooltip that carries the rest.
	 *
	 * FOR A MATERIAL, THE MATERIAL'S OWN NAME, which is short for the same
	 * reason: `Corrupted Mote`, not `Tier 1 material`.
	 *
	 * @return empty for an empty slot, and empty for an item or a material the
	 *         tables do not hold, which is the same answer a drop's name gives
	 */
	static FString LabelFor(const FCataclysmCarriedSlot& Slot,
							const UDataTable* BaseTable,
							const UDataTable* CraftingMaterialTable);

	/**
	 * How many are stacked in this slot, as a player reads it.
	 *
	 * ONLY FOR A MATERIAL, AND ONLY WHEN THERE IS MORE THAN ONE. Gear does not
	 * stack, so a count on it would always say 1 and mean nothing; a single
	 * material reads the same way. This is what every game in the genre does
	 * with a stack count.
	 *
	 * @return empty when there is no number worth drawing
	 */
	static FString QuantityTextFor(const FCataclysmCarriedSlot& Slot);

	/**
	 * The colour a slot's frame is drawn in.
	 *
	 * THE SAME TWO PALETTES THE DROPS ON THE FLOOR USE, because it is the same
	 * item. The Interface Colour section of `docs/Cataclysm_GDD_v2.md` names
	 * "inventory frames" as one of the three surfaces the gear rarity ramp
	 * appears on, alongside item names and the marker over a drop.
	 *
	 * @return the empty-cell colour for a slot holding nothing, so a caller can
	 *         draw every one of the 48 without asking first
	 */
	static FLinearColor ColourFor(const FCataclysmCarriedSlot& Slot,
								  const UDataTable* GearRarityTable,
								  const UDataTable* CraftingMaterialTable,
								  const UDataTable* MaterialTierTable);

	/**
	 * How thick a slot's frame is, in pixels.
	 *
	 * THE SECOND CHANNEL, AND THE SAME LADDER THE DROP NAMES USE. The Interface
	 * Colour section requires that "the frame and the drop marker must differ by
	 * shape or motion as well as by colour", because a player who cannot
	 * separate two hues still has to separate two rarities, and the ramp puts
	 * green, yellow, orange and red on four adjacent rungs. This calls
	 * UCataclysmDropPickup::NameBorderThicknessFor rather than holding a second
	 * ladder that could disagree with it.
	 *
	 * @return EmptyCellBorderPx for a slot holding nothing. An empty cell still
	 *         has a frame, because the empty slots are what tell a player how
	 *         much room is left.
	 */
	static int32 BorderThicknessFor(const FCataclysmCarriedSlot& Slot,
									const UDataTable* CraftingMaterialTable);

	/** The line above the grid: how many of the slots are used. */
	static FString HeaderTextFor(int32 Used, int32 Capacity);

	//~ Colours. Six-digit hex, the same form UCataclysmCombatOverlay uses.

	/**
	 * The panel behind the grid.
	 *
	 * NEARLY BLACK AND NEARLY OPAQUE. This screen stops the player looking at
	 * the dungeon rather than sitting beside it, and a panel the floor shows
	 * through would put a lava texture behind an Everyday frame, which is the
	 * contrast problem the drop names already work around.
	 */
	static const TCHAR* PanelHex;

	/** How much of the world the panel hides. */
	static constexpr float PanelOpacity = 0.94f;

	/**
	 * The panel's own edge, and the rule under the header line.
	 *
	 * WITHOUT AN EDGE THE PANEL STOPS WHERE THE GAME STOPS BEING VISIBLE, which
	 * is not a boundary a player can see against a dark dungeon floor. Measured
	 * at 2.43:1 against the panel: quieter than an empty cell's frame on
	 * purpose, so the edge does not compete with the grid inside it.
	 */
	static const TCHAR* PanelEdgeHex;

	/** How thick the panel's edge is. */
	static constexpr float PanelEdgePx = 2.0f;

	/** How thick the line under the header is. */
	static constexpr float HeaderRulePx = 1.0f;

	/**
	 * The inside of a cell, within its frame.
	 *
	 * A CELL IS DRAWN AS TWO FILLED RECTANGLES: one in the frame's colour, and a
	 * smaller one on top of it. That is how a hollow frame of any thickness is
	 * drawn with no image asset, and the smaller one has to be filled or the
	 * frame's colour shows through the whole cell.
	 */
	static const TCHAR* CellInteriorHex;

	/**
	 * How opaque a cell's interior is over the frame beneath it.
	 *
	 * SO AN OCCUPIED CELL IS FAINTLY TINTED WITH ITS OWN COLOUR. Twelve per cent
	 * of the frame's colour reaches the middle of the cell, which is what makes
	 * a full cell and an empty one differ by more than a thin outline. Issue
	 * #734 asked for that separation; this is where it comes from.
	 *
	 * IT WAS 6% BY ACCIDENT BEFORE THIS. The interior borrowed PanelOpacity,
	 * which is the share of the world the panel hides and has nothing to do with
	 * how much of a frame should show through. The two are separate decisions
	 * and are now separate values.
	 *
	 * THE INK STAYS READABLE OVER IT. The lightest frame is Quality white, and
	 * twelve per cent of white over the panel measures 12.4:1 against the ink,
	 * well above the 4.5:1 WCAG 2.1 asks for ordinary text.
	 */
	static constexpr float CellInteriorOpacity = 0.88f;

	/**
	 * The frame around a slot holding nothing.
	 *
	 * RAISED FROM `#3A4149`, WHICH MEASURED 1.86:1 AGAINST THE PANEL. WCAG 2.1
	 * asks 3:1 for the boundary of a user interface component, and below it the
	 * three empty rows read as one flat rectangle rather than as 36 places an
	 * item could go, which defeats the reason empty cells are drawn at all. This
	 * measures 3.20:1.
	 *
	 * STILL PLAINLY NOT A RARITY. It is a desaturated slate, no rung of either
	 * palette is near it, and an empty cell carries no label.
	 */
	static const TCHAR* EmptyCellHex;

	/** The frame thickness of a slot holding nothing. The thinnest there is. */
	static constexpr int32 EmptyCellBorderPx = 1;

	/**
	 * The colour every piece of text on this screen is drawn in.
	 *
	 * THE FRAME CARRIES THE RARITY AND THE LABEL DOES NOT, since issue #734. A
	 * cell's label used to be drawn in the item's own colour, and two things
	 * were wrong with that. Ascendant purple measured **3.95:1** against the
	 * panel, below the 4.5:1 WCAG 2.1 asks for ordinary text, so one of the
	 * thirteen labels failed outright. And the thirteen ranged from 3.95:1 to
	 * 19.27:1, so cells side by side were lit very differently for no reason a
	 * player could use.
	 *
	 * THE COLOUR IS NOT LOST, it moves. The frame carries it, its thickness
	 * carries the rung, and CellInteriorOpacity lets it tint the whole cell --
	 * which is more coloured area than the letters ever were.
	 *
	 * THE DESIGN ASKS FOR EXACTLY THIS. The Interface Colour section of
	 * `docs/Cataclysm_GDD_v2.md` says rarity colours appear on "item names,
	 * inventory frames and the marker over a drop on the ground". A cell's frame
	 * is the inventory surface it names; the label inside a cell stands in for
	 * an icon, which in this genre carries no text at all.
	 *
	 * AND IT IS WHAT THE GENRE DID AFTER THE SAME COMPLAINT. Diablo IV reduced
	 * the brightness and saturation of its item icon backgrounds and moved the
	 * rarity signal onto the border decoration, after players reported the icons
	 * unintelligible and rare and legendary indistinguishable without hovering.
	 * `docs/DECISIONS.md` carries the sources.
	 *
	 * Measured at 17.00:1 against the panel.
	 */
	static const TCHAR* InkHex;

	/** Clear space between a cell's frame and the label inside it. */
	static constexpr float LabelPaddingPx = 3.0f;
};
