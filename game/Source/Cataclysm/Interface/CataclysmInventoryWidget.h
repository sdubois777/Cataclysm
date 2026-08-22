// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "CataclysmInventoryWidget.generated.h"

class UBorder;
class UCataclysmEquipmentComponent;
class UCataclysmInventoryComponent;
class UDataTable;
class USizeBox;
class UTextBlock;
class UUniformGridPanel;
class UVerticalBox;

/**
 * One cell of the carried grid: the widgets that have to be written to when
 * what is in that slot changes.
 *
 * KEPT AS A STRUCT OF POINTERS RATHER THAN LOOKED UP BY NAME EACH TIME. The
 * refresh runs every frame the screen is open and walks all 48; searching the
 * widget tree for a name 48 times a frame would be a lookup for something that
 * cannot move.
 */
USTRUCT()
struct FCataclysmInventoryCellWidgets
{
	GENERATED_BODY()

	/** Sets the cell's exact size, so a long name cannot widen the whole grid. */
	UPROPERTY()
	TObjectPtr<USizeBox> Size = nullptr;

	/** Filled with the frame's colour and padded by its thickness. */
	UPROPERTY()
	TObjectPtr<UBorder> Frame = nullptr;

	/** Filled with the cell's interior colour, on top of the frame. */
	UPROPERTY()
	TObjectPtr<UBorder> Interior = nullptr;

	/** What the slot holds, wrapped and cut to fit. */
	UPROPERTY()
	TObjectPtr<UTextBlock> Label = nullptr;

	/** How many are stacked, in the bottom right corner. Empty for gear. */
	UPROPERTY()
	TObjectPtr<UTextBlock> Quantity = nullptr;
};

/**
 * The carried inventory as a widget: 48 cells in four rows of twelve.
 *
 * IT SHIPS NO CONTENT ASSET. The widget tree is built in C++, in RebuildWidget,
 * and there is no widget Blueprint anywhere under `game/Content`. The project
 * owner chose that over a generated asset on 2026-08-19, for the reason
 * `Interface/CataclysmCombatOverlay.h` already records: a `.uasset` is binary, it
 * cannot be reviewed in a diff, and the editor rewrites it when merely opened
 * (issue #140). The cost is that nothing here can be moved around in the editor;
 * every size and colour is a named constant on UCataclysmInventoryScreen.
 *
 * IN RebuildWidget AND NOT IN NativeConstruct OR NativeOnInitialized.
 * NativeConstruct runs after the Slate root has already been taken, so a tree
 * built there never appears. NativeOnInitialized runs early enough, but
 * UUserWidget::Initialize only calls it when the widget has a valid player
 * context, which depends on how it was created. RebuildWidget always runs, and
 * UUserWidget::Initialize has created WidgetTree before it does.
 *
 * A HOLLOW FRAME OF ANY THICKNESS, WITH NO IMAGE ASSET. Each cell is a border
 * filled with the frame's colour, padded inward by the frame's thickness, holding
 * a second border filled with the cell's interior colour. FSlateColorBrush fills
 * a rectangle with a colour and no texture, so the two together draw an outline
 * whose width is the padding. That is what carries the rarity as a thickness, and
 * the Interface Colour section of `docs/Cataclysm_GDD_v2.md` requires it.
 *
 * THE WIDGET ITSELF IS NOT HIT-TESTABLE AND ITS CELLS ARE. It used to be
 * HitTestInvisible, which applies to the whole subtree, so that Slate never
 * consumed a mouse event: letting a widget eat the click would also stop the
 * character walking, and whether that happens depends on the input mode and on
 * which widgets are hit-testable, none of which can be tested here because the
 * automation command passes -nullrhi. The controller's existing guard, which the
 * project owner has played, was kept instead and asks CursorIsOverPanel.
 *
 * THAT MADE THE TOOL TIPS OF ISSUE #733 IMPOSSIBLE TO SHOW, and it was found
 * after they had merged. A Slate tool tip is only offered on a widget that takes
 * part in hit testing, so the text was being set on all 48 cells every time the
 * contents changed and Slate had no reason to ask for any of it. Nothing failed:
 * the tests cover the wording, which was correct.
 *
 * SelfHitTestInvisible EXEMPTS THIS WIDGET AND NOT ITS CHILDREN, so a cell can
 * be hovered. The click still reaches the game -- a UBorder binds no mouse
 * handler, so SBorder::OnMouseButtonDown returns unhandled and the event carries
 * on to the viewport -- and CursorIsOverPanel is untouched.
 *
 * NOTHING HERE DECIDES ANYTHING. What a cell says, what colour and how thick its
 * frame is, how big a cell is and what the header reads are all static functions
 * on UCataclysmInventoryScreen, for the reason that class's header gives.
 */
UCLASS()
class CATACLYSM_API UCataclysmInventoryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Writes what the inventory holds into the widgets.
	 *
	 * SAFE TO CALL WITH NO INVENTORY, which is what a pawn that is not the
	 * player character has. Every cell then reads as empty rather than keeping
	 * what the last pawn was carrying.
	 */
	void Refresh(const UCataclysmInventoryComponent* Inventory,
				 const UCataclysmEquipmentComponent* Equipment);

	/**
	 * Which carried slot the cursor is over, or INDEX_NONE.
	 *
	 * ASKED OF SLATE'S OWN GEOMETRY rather than of arithmetic this class
	 * does. Every cell knows where it was last painted, so the question is a
	 * loop and a containment test; the port in issue #735 deleted the
	 * arithmetic that used to answer it and there is no reason to bring it
	 * back.
	 *
	 * IT CANNOT BE TESTED HERE, and neither can CursorIsOverPanel beside it:
	 * a cached geometry is empty until the widget has been painted and the
	 * automation command passes -nullrhi. What happens once a slot is named
	 * is UCataclysmWearing, which is covered.
	 */
	int32 CarriedSlotUnderCursor(const FVector2D& ViewportPoint) const;

	/**
	 * Which worn gear slot the cursor is over, or ECataclysmGearSlot::Count.
	 */
	ECataclysmGearSlot GearSlotUnderCursor(const FVector2D& ViewportPoint) const;

	/**
	 * Whether the panel covers a point, given in viewport pixels.
	 *
	 * WHY THE WIDGET HAS TO ANSWER. The left mouse button orders a move and the
	 * cursor trace passes behind anything drawn over the world, so without this
	 * a click on a cell would send the character walking to whatever floor lies
	 * under the panel. Issue #731 built that guard on the canvas and issue #735
	 * moved the question here without changing the controller's side of it.
	 *
	 * FROM THE PANEL'S OWN GEOMETRY rather than from arithmetic repeated
	 * alongside the drawing, which is what a widget makes possible.
	 *
	 * @return false until the panel has been painted at least once, because a
	 *         widget has no cached geometry before then. That is the single
	 *         frame between a key press and a mouse click.
	 */
	bool CursorIsOverPanel(const FVector2D& ViewportPoint) const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	/**
	 * Refreshes the grid from whatever the possessed pawn is carrying.
	 *
	 * EVERY FRAME THE SCREEN IS OPEN, rather than when the inventory changes.
	 * UCataclysmInventoryComponent reports no change to anything, and giving it
	 * a delegate to satisfy one screen is a wider change than this issue.
	 * Nothing here allocates: 48 cells have their text and colour written, and
	 * Slate does nothing when a value has not moved.
	 *
	 * A NATIVE UUserWidget DOES TICK. UUserWidget::UpdateCanTick turns ticking on
	 * for any class that is not a widget Blueprint, which this is not.
	 */
	virtual void NativeTick(const FGeometry& MyGeometry,
							float InDeltaTime) override;

private:
	/** Builds the panel, the header and the 48 cells. Runs once. */
	void BuildTree();

	/** Builds one cell and records the widgets the refresh writes to. */
	/**
	 * One cell, built into a grid at a row and column, appended to a list.
	 *
	 * THE ROW AND COLUMN COME FROM THE CALLER because the two grids place
	 * their cells by different rules. The carried grid runs its 48 slots
	 * across twelve columns in order; the panel of worn gear puts the armour
	 * in one column, the eight rings in another and the weapons in a third,
	 * which is UCataclysmGearPanel::PlacementFor.
	 */
	void BuildCell(UUniformGridPanel* Grid, int32 Row, int32 Column,
				   TArray<FCataclysmInventoryCellWidgets>& Into);

	/** Which cell of a list the cursor is over, or INDEX_NONE. */
	int32 IndexOfCellUnderCursor(
		const TArray<FCataclysmInventoryCellWidgets>& From,
		const FVector2D& ViewportPoint) const;

	/** Builds the panel of worn gear into a column. Called from BuildTree. */
	void BuildGearPanel(UVerticalBox* Into);

	/** Fills the panel of worn gear. Called from Refresh. */
	void RefreshGear(const UCataclysmEquipmentComponent* Equipment,
					 const UDataTable* Bases, const UDataTable* Affixes,
					 const UDataTable* Rarities, const UDataTable* Materials,
					 const UDataTable* Tiers, bool bResized, float CellPx,
					 float LabelFontPx);

	/** The panel behind everything. What CursorIsOverPanel measures. */
	UPROPERTY()
	TObjectPtr<UBorder> Panel = nullptr;

	/** The line above the grid saying how many slots are used. */
	UPROPERTY()
	TObjectPtr<UTextBlock> Header = nullptr;

	/**
	 * One entry per slot, in the same order as
	 * UCataclysmInventoryComponent::GetSlots. Always 48 long once built.
	 */
	UPROPERTY()
	TArray<FCataclysmInventoryCellWidgets> Cells;

	/**
	 * The panel of worn gear, one entry per ECataclysmGearSlot in its order.
	 *
	 * INDEXED BY THE ENUM'S VALUE, which holds because the cells are built by
	 * walking UCataclysmGearSlots::AllSlots, and that is built from 0 upwards.
	 */
	UPROPERTY()
	TArray<FCataclysmInventoryCellWidgets> GearCells;

	/** The line above the panel of worn gear. */
	UPROPERTY()
	TObjectPtr<UTextBlock> GearHeader = nullptr;

	/**
	 * The cell size the widgets were last set to.
	 *
	 * KEPT SO THE REFRESH CAN SKIP THE RESIZE. A cell's size and its label's
	 * font size follow the viewport, which changes when the window is resized
	 * and at no other time. Writing 48 size overrides and 96 font sizes every
	 * frame would rebuild Slate's font atlas for a value that had not moved.
	 */
	float LastCellPx = 0.0f;

	/**
	 * What UCataclysmInventoryComponent::ChangeCount read last refresh.
	 *
	 * A tool tip's text is rebuilt only when this moves. Refresh runs from
	 * NativeTick and walks all 48 cells, and a tool tip is a dozen table
	 * lookups and a string join, so rebuilding them every frame would be a
	 * lot of work for something that changes when the player picks
	 * something up. Issue #733.
	 */
	int32 LastChangeCount = 0;

	/**
	 * Whether tool tips have been built at all since the tree was made.
	 *
	 * NEEDED SEPARATELY FROM THE COUNT, because a fresh widget and a fresh
	 * inventory both start at zero, so the first refresh would see no
	 * change and leave all 48 cells with no tool tip at all.
	 */
	bool bToolTipsBuilt = false;
};
