// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmInventoryWidget.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Interface/CataclysmGearPanel.h"
#include "Interface/CataclysmInventoryScreen.h"
#include "Interface/CataclysmItemTooltip.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/DataTable.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

namespace
{
	using FScreen = UCataclysmInventoryScreen;

	/** The panel's colour, with the share of the world it hides. */
	FLinearColor PanelColour()
	{
		FLinearColor Colour =
			UCataclysmCombatOverlay::ColourFromHex(FScreen::PanelHex);
		Colour.A = FScreen::PanelOpacity;
		return Colour;
	}

	/**
	 * The inside of a cell, laid over the frame's own colour.
	 *
	 * NOT FULLY OPAQUE, AND THAT IS THE POINT. Twelve per cent of the frame
	 * beneath it reaches the middle of the cell, so an occupied cell is faintly
	 * tinted with its own rarity and an empty one is not. Issue #734.
	 */
	FLinearColor CellInteriorColour()
	{
		FLinearColor Colour =
			UCataclysmCombatOverlay::ColourFromHex(FScreen::CellInteriorHex);
		Colour.A = FScreen::CellInteriorOpacity;
		return Colour;
	}

	/** Every piece of text on this screen: the header, a label and a count. */
	FLinearColor InkColour()
	{
		return UCataclysmCombatOverlay::ColourFromHex(FScreen::InkHex);
	}

	/** The panel's edge and the rule under the header. */
	FLinearColor EdgeColour()
	{
		FLinearColor Colour =
			UCataclysmCombatOverlay::ColourFromHex(FScreen::PanelEdgeHex);
		Colour.A = FScreen::PanelOpacity;
		return Colour;
	}
}

TSharedRef<SWidget> UCataclysmInventoryWidget::RebuildWidget()
{
	// WidgetTree is created by UUserWidget::Initialize, which has always run by
	// the time RebuildWidget is called. The check is for the one case it has
	// not: Super::RebuildWidget calls Initialize itself when a Blueprint
	// recompile replaced the widget in memory.
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildTree();
	}

	return Super::RebuildWidget();
}

void UCataclysmInventoryWidget::NativeTick(const FGeometry& MyGeometry,
										   float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// THE PAWN IS ASKED EACH FRAME RATHER THAN REMEMBERED. A controller can
	// possess a different pawn, and only the player character has an inventory
	// at all; holding a pointer here would be a second record of what the
	// controller already knows.
	const UCataclysmInventoryComponent* Inventory = nullptr;
	const UCataclysmEquipmentComponent* Equipment = nullptr;
	if (const APawn* Pawn = GetOwningPlayerPawn())
	{
		Inventory = Pawn->FindComponentByClass<UCataclysmInventoryComponent>();
		Equipment = Pawn->FindComponentByClass<UCataclysmEquipmentComponent>();
	}

	Refresh(Inventory, Equipment);
}

void UCataclysmInventoryWidget::BuildTree()
{
	// THE WIDGET ITSELF IS NOT HIT-TESTABLE AND ITS CELLS ARE, which is the
	// difference between SelfHitTestInvisible and HitTestInvisible and it is
	// not a detail.
	//
	// HitTestInvisible APPLIES TO THE WHOLE SUBTREE. It was right while the
	// screen only had to be looked at. A Slate tool tip is only shown on a
	// widget that takes part in hit testing, so under HitTestInvisible the
	// tool tip text issue #733 sets on every cell could never appear -- it was
	// set correctly, and Slate had no reason to ask for it.
	//
	// THE CLICK STILL REACHES THE GAME. A UBorder does not bind a mouse
	// handler, so SBorder::OnMouseButtonDown returns unhandled and the event
	// carries on to the viewport exactly as before. What stops the character
	// walking when the player clicks the open screen is the controller asking
	// CursorIsOverPanel, which the header describes and which is untouched.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(
		UOverlay::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	// THE PANEL IS TWO FILLED RECTANGLES, the same way a cell's frame is: an
	// outer one in the edge colour, padded inward by the edge's thickness, and
	// the panel itself laid on top of it. Without an edge the panel stops where
	// the game stops being visible, which is not a boundary a player can see
	// against a dark dungeon floor. Issue #734.
	Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
												 TEXT("Panel"));
	Panel->SetBrush(FSlateColorBrush(EdgeColour()));
	Panel->SetPadding(FMargin(FScreen::PanelEdgePx));

	// CENTRED BY THE OVERLAY AND SIZED BY ITS CONTENTS. This is what the canvas
	// version had to work out for itself, and it is the whole reason the port
	// deleted that arithmetic.
	UOverlaySlot* PanelSlot = Root->AddChildToOverlay(Panel);
	PanelSlot->SetHorizontalAlignment(HAlign_Center);
	PanelSlot->SetVerticalAlignment(VAlign_Center);

	UBorder* Inside = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("PanelInside"));
	Inside->SetBrush(FSlateColorBrush(PanelColour()));
	Inside->SetPadding(FMargin(FScreen::PanelPaddingPx));
	Panel->SetContent(Inside);

	// THE WORN GEAR ON THE LEFT AND WHAT IS CARRIED ON THE RIGHT. Issue #831.
	// Both are columns of their own so each keeps its own heading, and the
	// width they need together is what UCataclysmInventoryScreen::CellSizeFor
	// now works the cell size out from.
	UHorizontalBox* Across = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("Across"));
	Inside->SetContent(Across);

	UVerticalBox* GearColumn = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("GearColumn"));
	UHorizontalBoxSlot* GearColumnSlot =
		Across->AddChildToHorizontalBox(GearColumn);
	GearColumnSlot->SetPadding(FMargin(0.0f, 0.0f, FScreen::CellGapPx, 0.0f));
	GearColumnSlot->SetVerticalAlignment(VAlign_Top);

	BuildGearPanel(GearColumn);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("Column"));
	UHorizontalBoxSlot* ColumnSlot = Across->AddChildToHorizontalBox(Column);
	ColumnSlot->SetVerticalAlignment(VAlign_Top);

	Header = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
													 TEXT("Header"));
	Header->SetColorAndOpacity(FSlateColor(InkColour()));
	Header->SetFontSize(static_cast<float>(FScreen::HeaderFontPx));
	Header->SetJustification(ETextJustify::Center);

	// THE HEADER, ITS RULE AND THE GAP UNDER THEM COME TO EXACTLY WHAT
	// CellSizeFor RESERVED. Left to size itself the line would be as tall as its
	// font happens to be, and the figure the cell size was worked out against
	// would be a guess.
	USizeBox* HeaderBand = WidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass(), TEXT("HeaderBand"));
	HeaderBand->SetHeightOverride(FScreen::HeaderHeightPx
								  - FScreen::HeaderRulePx - FScreen::CellGapPx);
	HeaderBand->SetContent(Header);

	UVerticalBoxSlot* HeaderSlot = Column->AddChildToVerticalBox(HeaderBand);
	HeaderSlot->SetHorizontalAlignment(HAlign_Fill);

	// A LINE UNDER THE COUNT, so the header and the grid read as two things
	// rather than as text floating above a rectangle.
	UBorder* Rule = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
														 TEXT("HeaderRule"));
	Rule->SetBrush(FSlateColorBrush(EdgeColour()));

	USizeBox* RuleBand = WidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass(), TEXT("HeaderRuleBand"));
	RuleBand->SetHeightOverride(FScreen::HeaderRulePx);
	RuleBand->SetContent(Rule);

	UVerticalBoxSlot* RuleSlot = Column->AddChildToVerticalBox(RuleBand);
	RuleSlot->SetHorizontalAlignment(HAlign_Fill);
	RuleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, FScreen::CellGapPx));

	UUniformGridPanel* Grid = WidgetTree->ConstructWidget<UUniformGridPanel>(
		UUniformGridPanel::StaticClass(), TEXT("Grid"));

	// HALF THE GAP ON EVERY SIDE OF EVERY CELL, which puts a whole gap between
	// two of them. It also puts half a gap around the outside of the grid, on
	// top of the panel's own padding; that is six pixels the panel is wider than
	// CellSizeFor assumed, out of a share of the viewport that leaves 14% spare.
	Grid->SetSlotPadding(FMargin(FScreen::CellGapPx * 0.5f));

	Column->AddChildToVerticalBox(Grid);

	Cells.Reset();
	Cells.Reserve(UCataclysmInventoryComponent::SlotCount);
	for (int32 SlotIndex = 0;
		 SlotIndex < UCataclysmInventoryComponent::SlotCount; ++SlotIndex)
	{
		BuildCell(Grid,
				  /*Row=*/SlotIndex / UCataclysmInventoryComponent::Columns,
				  /*Column=*/SlotIndex % UCataclysmInventoryComponent::Columns,
				  Cells);
	}
}

void UCataclysmInventoryWidget::BuildGearPanel(UVerticalBox* Into)
{
	GearHeader = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("GearHeader"));
	GearHeader->SetColorAndOpacity(FSlateColor(InkColour()));
	GearHeader->SetFontSize(static_cast<float>(FScreen::HeaderFontPx));
	GearHeader->SetJustification(ETextJustify::Center);

	// THE SAME HEADER BAND AND RULE AS THE CARRIED GRID, so the two columns
	// line up. Their headings sit on one line and their first row of cells
	// starts at the same height.
	USizeBox* Band = WidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass(), TEXT("GearHeaderBand"));
	Band->SetHeightOverride(FScreen::HeaderHeightPx
							- FScreen::HeaderRulePx - FScreen::CellGapPx);
	Band->SetContent(GearHeader);
	Into->AddChildToVerticalBox(Band)->SetHorizontalAlignment(HAlign_Fill);

	UBorder* Rule = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("GearHeaderRule"));
	Rule->SetBrush(FSlateColorBrush(EdgeColour()));

	USizeBox* RuleBand = WidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass(), TEXT("GearHeaderRuleBand"));
	RuleBand->SetHeightOverride(FScreen::HeaderRulePx);
	RuleBand->SetContent(Rule);

	UVerticalBoxSlot* RuleSlot = Into->AddChildToVerticalBox(RuleBand);
	RuleSlot->SetHorizontalAlignment(HAlign_Fill);
	RuleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, FScreen::CellGapPx));

	UUniformGridPanel* GearGrid = WidgetTree->ConstructWidget<UUniformGridPanel>(
		UUniformGridPanel::StaticClass(), TEXT("GearGrid"));
	GearGrid->SetSlotPadding(FMargin(FScreen::CellGapPx * 0.5f));
	Into->AddChildToVerticalBox(GearGrid);

	GearCells.Reset();
	GearCells.Reserve(UCataclysmGearSlots::AllSlots().Num());
	for (const ECataclysmGearSlot GearSlot : UCataclysmGearSlots::AllSlots())
	{
		int32 Row = INDEX_NONE;
		int32 Column = INDEX_NONE;
		UCataclysmGearPanel::PlacementFor(GearSlot, Row, Column);
		BuildCell(GearGrid, Row, Column, GearCells);
	}
}

void UCataclysmInventoryWidget::BuildCell(
	UUniformGridPanel* Grid, int32 Row, int32 Column,
	TArray<FCataclysmInventoryCellWidgets>& Into)
{
	FCataclysmInventoryCellWidgets Widgets;

	Widgets.Size = WidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass());
	Widgets.Frame = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass());
	Widgets.Interior = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass());
	Widgets.Label = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass());
	Widgets.Quantity = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass());

	// WRAPPED BY MEASURED WIDTH AND CUT WITH A REAL ELLIPSIS. The canvas version
	// counted characters, because it had no font to measure with outside the
	// draw, and lost the last word of a three-word material name.
	Widgets.Label->SetAutoWrapText(true);
	Widgets.Label->SetJustification(ETextJustify::Center);
	Widgets.Label->SetTextOverflowPolicy(ETextOverflowPolicy::MultilineEllipsis);

	// IN THE INK AND NOT IN THE ITEM'S OWN COLOUR, since issue #734. Ascendant
	// purple measured 3.95:1 against the panel, under the 4.5:1 WCAG 2.1 asks
	// for ordinary text, and the thirteen label colours ranged over five to one.
	// The frame carries the colour, its thickness carries the rung, and the
	// cell's interior is tinted by it; the letters do not need to as well.
	Widgets.Label->SetColorAndOpacity(FSlateColor(InkColour()));

	// THE COUNT IN THE TEXT COLOUR RATHER THAN THE ITEM'S. It is how many, not
	// what: in the material's own colour it would read as another word of the
	// name. Every game in the genre puts a stack count in a corner.
	Widgets.Quantity->SetColorAndOpacity(FSlateColor(InkColour()));
	Widgets.Quantity->SetJustification(ETextJustify::Right);

	UOverlay* Inside = WidgetTree->ConstructWidget<UOverlay>(
		UOverlay::StaticClass());

	UOverlaySlot* LabelSlot = Inside->AddChildToOverlay(Widgets.Label);
	LabelSlot->SetHorizontalAlignment(HAlign_Fill);
	LabelSlot->SetVerticalAlignment(VAlign_Center);

	UOverlaySlot* QuantitySlot = Inside->AddChildToOverlay(Widgets.Quantity);
	QuantitySlot->SetHorizontalAlignment(HAlign_Right);
	QuantitySlot->SetVerticalAlignment(VAlign_Bottom);

	Widgets.Interior->SetBrush(FSlateColorBrush(CellInteriorColour()));
	Widgets.Interior->SetPadding(FMargin(FScreen::LabelPaddingPx));
	Widgets.Interior->SetContent(Inside);

	// THE FRAME IS THE OUTER FILL AND ITS THICKNESS IS THE PADDING. Filling the
	// outer border with the rarity's colour and laying the interior on top of it
	// leaves an outline exactly as wide as the padding, with no image asset
	// anywhere. Refresh sets both, because both follow what the slot holds.
	Widgets.Frame->SetContent(Widgets.Interior);

	Widgets.Size->SetContent(Widgets.Frame);

	// CLIPPED, so a label that overflows despite the wrapping cannot draw over
	// its neighbours.
	Widgets.Size->SetClipping(EWidgetClipping::ClipToBounds);

	UUniformGridSlot* GridSlot =
		Grid->AddChildToUniformGrid(Widgets.Size, Row, Column);
	GridSlot->SetHorizontalAlignment(HAlign_Fill);
	GridSlot->SetVerticalAlignment(VAlign_Fill);

	Into.Add(Widgets);
}

void UCataclysmInventoryWidget::Refresh(
	const UCataclysmInventoryComponent* Inventory,
	const UCataclysmEquipmentComponent* Equipment)
{
	if (Cells.Num() != UCataclysmInventoryComponent::SlotCount || !Header)
	{
		// THE TREE HAS NOT BEEN BUILT. RebuildWidget builds it when the widget
		// is first taken, which is when it is added to the viewport, and a
		// refresh before that has nothing to write to.
		return;
	}

	// THE FOUR TABLES ARE LOADED ONCE FOR THE WHOLE GRID. A cell fetching its
	// own would repeat four lookups 48 times a frame for something that cannot
	// change while the screen is open.
	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	const UDataTable* Rarities = UCataclysmDropRoll::LoadGearRarityTable();
	const UDataTable* Materials = UCataclysmDropRoll::LoadCraftingMaterialTable();
	const UDataTable* Tiers = UCataclysmDropRoll::LoadMaterialTierTable();

	// THE HEADER IS SET AFTER THE TABLES RATHER THAN BEFORE, because naming
	// what is on the cursor needs them. Issue #853.
	const int32 Used = Inventory ? Inventory->NumItems() : 0;
	FString HeldName;
	if (Inventory && Inventory->IsHolding()
		&& Inventory->GetSlots().IsValidIndex(Inventory->HeldSlot()))
	{
		HeldName = FScreen::LabelFor(
			Inventory->GetSlots()[Inventory->HeldSlot()], Bases, Materials);
	}
	Header->SetText(FText::FromString(FScreen::HeaderTextFor(
		Used, UCataclysmInventoryComponent::SlotCount, HeldName)));

	// THE AFFIX TABLE IS ONLY THE TOOL TIP'S BUSINESS. A cell's label is the
	// base's own name and its colour is its rarity, neither of which needs to
	// know what the affixes are; the tool tip states every one of them.
	const UDataTable* Affixes = UCataclysmDropRoll::LoadAffixTable();

	// TOOL TIPS ARE REBUILT WHEN THE CONTENTS CHANGE AND NOT EVERY FRAME.
	// This function runs from NativeTick, so anything done per cell is done
	// 48 times a frame; a tool tip's text is a dozen table lookups and a
	// string join, and what it says changes when the player picks something
	// up. UCataclysmInventoryComponent::ChangeCount is what makes that
	// cheap to notice. Issue #733.
	const int32 Changes = Inventory ? Inventory->ChangeCount() : 0;
	const bool bContentsChanged = Changes != LastChangeCount
		|| !bToolTipsBuilt;
	LastChangeCount = Changes;
	bToolTipsBuilt = true;

	float CellPx = LastCellPx;
	if (const APlayerController* Controller = GetOwningPlayer())
	{
		int32 SizeX = 0;
		int32 SizeY = 0;
		Controller->GetViewportSize(SizeX, SizeY);
		if (SizeX > 0 && SizeY > 0)
		{
			CellPx = FScreen::CellSizeFor(static_cast<float>(SizeX),
										  static_cast<float>(SizeY));
		}
	}

	// A CELL'S SIZE AND ITS FONT ONLY MOVE WHEN THE WINDOW DOES. Writing 48 size
	// overrides and 96 font sizes every frame would rebuild Slate's font atlas
	// for a value that had not changed.
	const bool bResized = !FMath::IsNearlyEqual(CellPx, LastCellPx);
	LastCellPx = CellPx;

	const float LabelFontPx =
		static_cast<float>(FScreen::LabelFontSizeFor(CellPx));
	const float QuantityFontPx =
		static_cast<float>(FScreen::QuantityFontSizeFor(CellPx));

	const FCataclysmCarriedSlot Nothing;
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		const FCataclysmInventoryCellWidgets& Widgets = Cells[Index];

		const FCataclysmCarriedSlot& Carried =
			Inventory && Inventory->GetSlots().IsValidIndex(Index)
				? Inventory->GetSlots()[Index]
				: Nothing;

		if (bResized)
		{
			Widgets.Size->SetWidthOverride(CellPx);
			Widgets.Size->SetHeightOverride(CellPx);
			Widgets.Label->SetFontSize(LabelFontPx);
			Widgets.Quantity->SetFontSize(QuantityFontPx);
		}

		const FLinearColor Colour = FScreen::ColourFor(Carried, Rarities,
													   Materials, Tiers);

		Widgets.Frame->SetBrush(FSlateColorBrush(Colour));
		Widgets.Frame->SetPadding(FMargin(static_cast<float>(
			FScreen::BorderThicknessFor(Carried, Materials))));

		Widgets.Label->SetText(
			FText::FromString(FScreen::LabelFor(Carried, Bases, Materials)));

		Widgets.Quantity->SetText(
			FText::FromString(FScreen::QuantityTextFor(Carried)));

		if (bContentsChanged)
		{
			// ON THE FRAME RATHER THAN ON THE SIZE BOX. A USizeBox lays out and
			// does not take part in hit testing, so a tool tip on one is never
			// shown. The frame is a UBorder, which paints and is hit-testable,
			// and it is the outermost widget of the cell that is.
			//
			// AN EMPTY STRING REMOVES THE TOOL TIP RATHER THAN SHOWING A BLANK
			// BOX, which is what an empty cell should do.
			Widgets.Frame->SetToolTipText(FText::FromString(
				UCataclysmItemTooltip::TextFor(Carried, Bases, Affixes,
											   Materials)));
		}
	}

	// THE WORN GEAR IS REBUILT EVERY FRAME AND THE CARRIED CELLS ARE NOT, and
	// that used to be true of its TOOL TIP TEXT as well, which is the whole of
	// issue #1192: hovering a worn helmet showed no pop-up while hovering the
	// same helmet in the bag did. The old comment here said there was "no cheap
	// way to notice a change to what is worn". There is now --
	// UCataclysmEquipmentComponent::ChangeCount, added for this and the same
	// shape as the inventory's -- and the tool tip is the one thing that must
	// use it. Everything else in RefreshGear is cheap and stays per frame.
	const int32 GearChanges = Equipment ? Equipment->ChangeCount() : 0;
	const bool bWornChanged = GearChanges != LastGearChangeCount
		|| !bGearToolTipsBuilt;
	LastGearChangeCount = GearChanges;
	bGearToolTipsBuilt = true;

	RefreshGear(Equipment, Bases, Affixes, Rarities, Materials, Tiers,
				bResized, CellPx, LabelFontPx, bWornChanged);
}

void UCataclysmInventoryWidget::RefreshGear(
	const UCataclysmEquipmentComponent* Equipment, const UDataTable* Bases,
	const UDataTable* Affixes, const UDataTable* Rarities,
	const UDataTable* Materials, const UDataTable* Tiers, bool bResized,
	float CellPx, float LabelFontPx, bool bWornChanged)
{
	if (GearCells.Num() != UCataclysmGearSlots::AllSlots().Num() || !GearHeader)
	{
		return;
	}

	GearHeader->SetText(FText::FromString(UCataclysmGearPanel::HeaderTextFor(
		Equipment ? Equipment->NumEquipped() : 0, GearCells.Num())));

	const FCataclysmCarriedSlot Nothing;
	for (int32 Index = 0; Index < GearCells.Num(); ++Index)
	{
		const FCataclysmInventoryCellWidgets& Widgets = GearCells[Index];
		const ECataclysmGearSlot GearSlot =
			static_cast<ECataclysmGearSlot>(Index);

		const FCataclysmItem* Worn =
			Equipment ? Equipment->EquippedAt(GearSlot) : nullptr;

		// A CARRIED SLOT HOLDING THE WORN ITEM, so the colour, the frame's
		// thickness and the tool tip all come from the same functions the
		// carried grid uses. A worn Cataclysmic helm should not be a different
		// colour from the same helm sitting in the bag.
		FCataclysmCarriedSlot AsCarried = Nothing;
		if (Worn)
		{
			AsCarried.Item = *Worn;
		}

		if (bResized)
		{
			Widgets.Size->SetWidthOverride(CellPx);
			Widgets.Size->SetHeightOverride(CellPx);
			Widgets.Label->SetFontSize(LabelFontPx);
		}

		Widgets.Frame->SetBrush(FSlateColorBrush(
			FScreen::ColourFor(AsCarried, Rarities, Materials, Tiers)));
		Widgets.Frame->SetPadding(FMargin(static_cast<float>(
			FScreen::BorderThicknessFor(AsCarried, Materials))));

		Widgets.Label->SetText(FText::FromString(
			UCataclysmGearPanel::LabelFor(GearSlot, Worn, Bases)));

		// ONLY WHEN WHAT IS WORN HAS CHANGED, AND THAT IS THE FIX FOR #1192.
		//
		// Setting a widget's tool tip text builds a NEW tool tip object every
		// call: UWidget::SetToolTipText hands it to SWidget::SetToolTipText,
		// which calls FSlateApplicationBase::MakeToolTip. FSlateUser::UpdateTooltip
		// then decides whether the tool tip changed by comparing that object
		// against the active one, so a fresh object every frame reads as a
		// change every frame. It closes and re-opens the tool tip window each
		// time, and FSlateUser::ShowTooltip sets the window's opacity to zero
		// and restarts its fade-in clock, so the opacity it computes is
		// negative every frame and clamps to zero. The pop-up is summoned
		// constantly and never allowed to become visible.
		//
		// The carried grid above was already guarded this way and its pop-ups
		// worked, which is exactly the difference the play test reported.
		if (!bWornChanged)
		{
			continue;
		}

		// THE SECOND HAND SAYS WHY IT IS EMPTY when a two-handed weapon fills
		// both. Otherwise a player sees a free hand and tries to use it.
		if (UCataclysmGearPanel::SlotIsBlocked(GearSlot, Equipment))
		{
			Widgets.Frame->SetToolTipText(FText::FromString(
				TEXT("Both hands are holding the two-handed weapon.")));
			continue;
		}

		Widgets.Frame->SetToolTipText(FText::FromString(
			Worn ? UCataclysmItemTooltip::TextFor(AsCarried, Bases, Affixes,
												  Materials)
				 : FString()));
	}
}

int32 UCataclysmInventoryWidget::CarriedSlotUnderCursor(
	const FVector2D& ViewportPoint) const
{
	return IndexOfCellUnderCursor(Cells, ViewportPoint);
}

ECataclysmGearSlot UCataclysmInventoryWidget::GearSlotUnderCursor(
	const FVector2D& ViewportPoint) const
{
	const int32 Index = IndexOfCellUnderCursor(GearCells, ViewportPoint);
	return Index == INDEX_NONE ? ECataclysmGearSlot::Count
							   : static_cast<ECataclysmGearSlot>(Index);
}

int32 UCataclysmInventoryWidget::IndexOfCellUnderCursor(
	const TArray<FCataclysmInventoryCellWidgets>& From,
	const FVector2D& ViewportPoint) const
{
	// THE MOUSE POSITION IS IN VIEWPORT PIXELS AND A GEOMETRY IS IN SLATE'S
	// ABSOLUTE COORDINATES, and the two differ by the interface's scale. This
	// is the same conversion CursorIsOverPanel does.
	FVector2D Absolute = FVector2D::ZeroVector;
	USlateBlueprintLibrary::ScreenToWidgetAbsolute(
		const_cast<UCataclysmInventoryWidget*>(this), ViewportPoint, Absolute,
		/*bIncludeWindowPosition=*/false);

	for (int32 Index = 0; Index < From.Num(); ++Index)
	{
		const UBorder* Frame = From[Index].Frame;
		if (!Frame)
		{
			continue;
		}

		const FGeometry& Geometry = Frame->GetCachedGeometry();
		if (FVector2D(Geometry.GetLocalSize()).IsNearlyZero())
		{
			// NEVER PAINTED, so it is nowhere. The one frame between the key
			// press that opens the screen and the first time it is drawn.
			continue;
		}

		if (Geometry.IsUnderLocation(Absolute))
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

bool UCataclysmInventoryWidget::CursorIsOverPanel(
	const FVector2D& ViewportPoint) const
{
	if (!Panel)
	{
		return false;
	}

	const FGeometry& Geometry = Panel->GetCachedGeometry();
	const FVector2D Size = FVector2D(Geometry.GetLocalSize());
	if (Size.X <= 0.0 || Size.Y <= 0.0)
	{
		// NEVER PAINTED, so there is nothing to be over. See the header: this is
		// the one frame between the key press that opens the screen and the
		// first time it is drawn.
		return false;
	}

	// THE MOUSE POSITION IS IN VIEWPORT PIXELS AND THE GEOMETRY IS IN SLATE'S
	// ABSOLUTE COORDINATES, and the two differ by the interface's scale. This is
	// the conversion between them.
	FVector2D Absolute = FVector2D::ZeroVector;
	USlateBlueprintLibrary::ScreenToWidgetAbsolute(
		this, ViewportPoint, Absolute, /*bIncludeWindowPosition=*/false);

	return USlateBlueprintLibrary::IsUnderLocation(Geometry, Absolute);
}
