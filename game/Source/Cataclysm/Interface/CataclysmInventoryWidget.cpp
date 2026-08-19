// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmInventoryWidget.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Interface/CataclysmInventoryScreen.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
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

	/** The inside of a cell, matching the panel so the two read as one surface. */
	FLinearColor CellInteriorColour()
	{
		FLinearColor Colour =
			UCataclysmCombatOverlay::ColourFromHex(FScreen::CellInteriorHex);
		Colour.A = FScreen::PanelOpacity;
		return Colour;
	}

	/** The header line and a stack's count. */
	FLinearColor InkColour()
	{
		return UCataclysmCombatOverlay::ColourFromHex(FScreen::HeaderTextHex);
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
	if (const APawn* Pawn = GetOwningPlayerPawn())
	{
		Inventory = Pawn->FindComponentByClass<UCataclysmInventoryComponent>();
	}

	Refresh(Inventory);
}

void UCataclysmInventoryWidget::BuildTree()
{
	// NOTHING IN THIS TREE IS HIT-TESTABLE. See the header for why the click is
	// stopped by the controller rather than eaten by Slate.
	SetVisibility(ESlateVisibility::HitTestInvisible);

	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(
		UOverlay::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
												 TEXT("Panel"));
	Panel->SetBrush(FSlateColorBrush(PanelColour()));
	Panel->SetPadding(FMargin(FScreen::PanelPaddingPx));

	// CENTRED BY THE OVERLAY AND SIZED BY ITS CONTENTS. This is what the canvas
	// version had to work out for itself, and it is the whole reason the port
	// deleted that arithmetic.
	UOverlaySlot* PanelSlot = Root->AddChildToOverlay(Panel);
	PanelSlot->SetHorizontalAlignment(HAlign_Center);
	PanelSlot->SetVerticalAlignment(VAlign_Center);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("Column"));
	Panel->SetContent(Column);

	Header = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
													 TEXT("Header"));
	Header->SetColorAndOpacity(FSlateColor(InkColour()));
	Header->SetFontSize(static_cast<float>(FScreen::HeaderFontPx));
	Header->SetJustification(ETextJustify::Center);

	// THE HEADER'S BAND IS EXACTLY WHAT CellSizeFor RESERVED FOR IT. Left to
	// size itself the line would be as tall as its font happens to be, and the
	// figure the cell size was worked out against would be a guess.
	USizeBox* HeaderBand = WidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass(), TEXT("HeaderBand"));
	HeaderBand->SetHeightOverride(FScreen::HeaderHeightPx);
	HeaderBand->SetContent(Header);

	UVerticalBoxSlot* HeaderSlot = Column->AddChildToVerticalBox(HeaderBand);
	HeaderSlot->SetHorizontalAlignment(HAlign_Fill);

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
		BuildCell(Grid, SlotIndex);
	}
}

void UCataclysmInventoryWidget::BuildCell(UUniformGridPanel* Grid,
										  int32 SlotIndex)
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

	UUniformGridSlot* GridSlot = Grid->AddChildToUniformGrid(
		Widgets.Size,
		/*InRow=*/SlotIndex / UCataclysmInventoryComponent::Columns,
		/*InColumn=*/SlotIndex % UCataclysmInventoryComponent::Columns);
	GridSlot->SetHorizontalAlignment(HAlign_Fill);
	GridSlot->SetVerticalAlignment(VAlign_Fill);

	Cells.Add(Widgets);
}

void UCataclysmInventoryWidget::Refresh(
	const UCataclysmInventoryComponent* Inventory)
{
	if (Cells.Num() != UCataclysmInventoryComponent::SlotCount || !Header)
	{
		// THE TREE HAS NOT BEEN BUILT. RebuildWidget builds it when the widget
		// is first taken, which is when it is added to the viewport, and a
		// refresh before that has nothing to write to.
		return;
	}

	const int32 Used = Inventory ? Inventory->NumItems() : 0;
	Header->SetText(FText::FromString(FScreen::HeaderTextFor(
		Used, UCataclysmInventoryComponent::SlotCount)));

	// THE FOUR TABLES ARE LOADED ONCE FOR THE WHOLE GRID. A cell fetching its
	// own would repeat four lookups 48 times a frame for something that cannot
	// change while the screen is open.
	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	const UDataTable* Rarities = UCataclysmDropRoll::LoadGearRarityTable();
	const UDataTable* Materials = UCataclysmDropRoll::LoadCraftingMaterialTable();
	const UDataTable* Tiers = UCataclysmDropRoll::LoadMaterialTierTable();

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
			Widgets.Quantity->SetFontSize(LabelFontPx);
		}

		const FLinearColor Colour = FScreen::ColourFor(Carried, Rarities,
													   Materials, Tiers);

		Widgets.Frame->SetBrush(FSlateColorBrush(Colour));
		Widgets.Frame->SetPadding(FMargin(static_cast<float>(
			FScreen::BorderThicknessFor(Carried, Materials))));

		Widgets.Label->SetText(
			FText::FromString(FScreen::LabelFor(Carried, Bases, Materials)));
		Widgets.Label->SetColorAndOpacity(FSlateColor(Colour));

		Widgets.Quantity->SetText(
			FText::FromString(FScreen::QuantityTextFor(Carried)));
	}
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
