// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Math/Box2D.h"
#include "CataclysmInventoryScreen.generated.h"

struct FCataclysmCarriedSlot;
class UDataTable;

/**
 * Every judgement the carried inventory screen makes, outside the draw.
 *
 * WHY IT IS A SEPARATE CLASS FROM ACataclysmHUD, which is the same reason
 * UCataclysmCombatOverlay and UCataclysmDropPickup are separate from it.
 * AHUD::PostRender checks FApp::CanEverRender() before calling DrawHUD, and the
 * automation test command in tools/unreal_build.py passes -nullrhi, so DrawHUD
 * never runs under test at all. Anything decided inside it is untestable.
 * Everything here -- how big a cell is, where it sits, what its label says, what
 * colour and how thick its frame is, whether a click landed on the panel -- is a
 * static function over plain values, so all of it is covered while the drawing
 * itself stays uncovered.
 *
 * WHAT THIS SCREEN IS AND IS NOT. It is issue #731: the four rows of twelve, the
 * contents of each slot, and a key that opens and closes it. It shows and does
 * nothing else. Moving an item between slots, dropping one, equipping one (issue
 * #46), a tooltip carrying the item's whole name, sorting and the stash (issue
 * #529) are all elsewhere. The designed interface as a whole is issue #49, and
 * the port of all of this from the canvas to UMG is issue #650.
 *
 * IT DRAWS ON THE CANVAS, so it ships no asset of any kind: no widget Blueprint,
 * no font, no icon. That is what makes a cell carry a word rather than a picture.
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

	/** Room above the grid for the line saying how full the bag is. */
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
	 * THE SMALLER OF WHAT THE WIDTH AND THE HEIGHT ALLOW, capped at MaxCellPx,
	 * and never below one pixel. The floor is not a readable size and is not
	 * meant to be: it only stops a viewport too small to hold the padding from
	 * producing a negative cell, which would put every rectangle inside out.
	 */
	static float CellSizeFor(float ViewportWidth, float ViewportHeight);

	/** How wide and tall the whole panel is, for a cell of this size. */
	static FVector2D PanelSizeFor(float CellPx);

	/**
	 * Where the panel sits, centred in the viewport.
	 *
	 * CENTRED RATHER THAN AGAINST AN EDGE. The player's own health and mana bars
	 * are in the bottom left corner and the drop names are wherever the drops
	 * are, so the middle is the one place nothing else uses. It is also where
	 * this genre puts a screen that stops play rather than sitting beside it.
	 */
	static FBox2D PanelRectFor(float ViewportWidth, float ViewportHeight);

	/**
	 * Where one slot's cell sits inside a panel.
	 *
	 * SLOT NUMBERS RUN ACROSS EACH ROW AND THEN DOWN, so slot 0 is the top left
	 * and slot 47 the bottom right. That is the order
	 * UCataclysmInventoryComponent::AddItem fills them in, so an item picked up
	 * appears in the first gap a player can see rather than somewhere the
	 * numbering has hidden.
	 *
	 * @return an invalid box for a slot number outside the grid, so a caller
	 *         cannot draw a cell that does not exist
	 */
	static FBox2D CellRectFor(const FBox2D& Panel, float CellPx, int32 Slot);

	/**
	 * Whether a point on screen is over the panel.
	 *
	 * WHY THE SCREEN HAS TO ANSWER THIS. The left mouse button orders a move,
	 * and the cursor ray passes straight through anything drawn on the canvas to
	 * the floor behind it. Without this, clicking a cell would send the
	 * character walking to whatever is under the panel. Issue #731.
	 *
	 * THE WHOLE PANEL AND NOT ONLY THE CELLS, because the padding and the header
	 * are part of the screen the player is looking at, and a click that lands
	 * between two cells is still a click on the screen rather than on the world.
	 */
	static bool PanelCoversPoint(const FBox2D& Panel, const FVector2D& Point);

	//~ What a cell says.

	/**
	 * How much of ACataclysmHUD::TextScale a cell's label is drawn at, when the
	 * cell is at its largest.
	 *
	 * A RELATIVE SCALE, the same convention UCataclysmCombatOverlay::ScaleFor
	 * uses: this class decides what is bigger than what and the heads-up display
	 * decides how big everything is.
	 */
	static constexpr float LabelScaleAtMaxCell = 0.42f;

	/**
	 * The label's scale for a cell of this size.
	 *
	 * IN PROPORTION TO THE CELL, so the same number of characters fits whatever
	 * the viewport is and the grid reads identically at every resolution. A
	 * fixed scale would fit twelve characters on a wide monitor and four on a
	 * small one, and the wrapping below would then have to be told the viewport.
	 */
	static float LabelScaleFor(float CellPx);

	/**
	 * How many characters of a label fit across one cell.
	 *
	 * MEASURED BY EYE AND EXPECTED TO MOVE, like every other pixel figure in
	 * this interface. It is a character count rather than a width because the
	 * wrapping below has to work without a font, and no font exists under
	 * -nullrhi. ACataclysmHUD measures the line it is about to draw and shrinks
	 * it if this figure turns out to be generous, so the cost of it being wrong
	 * is a label drawn smaller than its neighbours rather than one that spills
	 * over the frame.
	 */
	static constexpr int32 LabelCharactersPerLine = 12;

	/** How many lines of label a cell holds before the rest is cut. */
	static constexpr int32 MaxLabelLines = 2;

	/**
	 * What is written at the end of a label too long to fit.
	 *
	 * THREE FULL STOPS RATHER THAN THE ELLIPSIS CHARACTER. This draws with the
	 * engine's own font and nothing here has checked which glyphs it carries; a
	 * missing glyph is drawn as a blank, which would read as a label that simply
	 * stops. Three stops cost two more characters and cannot go wrong.
	 */
	static const TCHAR* Ellipsis;

	/**
	 * Breaks a label into the lines a cell shows.
	 *
	 * ON SPACES, GREEDILY, which is what every text layout does: a word goes on
	 * the current line if it fits and starts a new one if it does not. A single
	 * word longer than a line is not broken in the middle -- "Greatsword" split
	 * as "Greatswo" and "rd" is worse than a shortened "Greatsw..." -- so it
	 * takes a line of its own and is cut.
	 *
	 * ANYTHING PAST MaxLines IS DROPPED AND THE LAST LINE KEPT SAYS SO, so a
	 * player can tell a name that was shortened from one that is short.
	 *
	 * @return no lines at all for empty text, which is what an empty slot gets
	 */
	static TArray<FString> LabelLinesFor(const FString& Text,
										 int32 MaxCharacters, int32 MaxLines);

	/**
	 * What a slot's cell is labelled with.
	 *
	 * FOR GEAR, THE ITEM BASE'S OWN NAME and not the whole item name. A cell is
	 * about a hundred pixels across and `Cataclysmic Greatsword of Malice` is
	 * not going into it, while `Greatsword` is exactly what an icon would say in
	 * a game that had one. The rarity that the full name opens with is already
	 * on the cell twice over, as the frame's colour and its thickness.
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

	/**
	 * How much of ACataclysmHUD::TextScale the header line is drawn at.
	 *
	 * FIXED, WHERE A CELL'S LABEL SCALES WITH ITS CELL. The header is one line
	 * of text and its band is a fixed 32 pixels, so shrinking it with the grid
	 * would make it unreadable on a small viewport for no gain in room.
	 */
	static constexpr float HeaderScale = 0.6f;

	//~ Colours. Six-digit hex, the same form UCataclysmCombatOverlay uses.

	/**
	 * The panel behind the grid.
	 *
	 * NEARLY BLACK AND NEARLY OPAQUE. This screen stops the player looking at
	 * the dungeon rather than sitting beside it, and a panel the floor shows
	 * through would put a lava texture behind an Everyday frame, which is the
	 * contrast problem DrawOutlinedText and DrawBorder both already work around.
	 */
	static const TCHAR* PanelHex;

	/** How much of the world the panel hides. */
	static constexpr float PanelOpacity = 0.94f;

	/** The frame around a slot holding nothing. Dim, and plainly not a rarity. */
	static const TCHAR* EmptyCellHex;

	/** The frame thickness of a slot holding nothing. The thinnest there is. */
	static constexpr int32 EmptyCellBorderPx = 1;

	/** The header line and a stack's count. */
	static const TCHAR* HeaderTextHex;

	/** Clear space between a cell's frame and the label inside it. */
	static constexpr float LabelInsetPx = 3.0f;
};
