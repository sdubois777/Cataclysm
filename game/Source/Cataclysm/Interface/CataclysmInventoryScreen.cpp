// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmInventoryScreen.h"
#include "Data/CataclysmDataRows.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Engine/DataTable.h"

const TCHAR* UCataclysmInventoryScreen::Ellipsis = TEXT("...");
const TCHAR* UCataclysmInventoryScreen::PanelHex = TEXT("0A0F12");
const TCHAR* UCataclysmInventoryScreen::EmptyCellHex = TEXT("3A4149");
const TCHAR* UCataclysmInventoryScreen::HeaderTextHex = TEXT("F5F0EA");

namespace
{
	/** Twelve. Read from the store rather than copied, so there is one answer. */
	constexpr int32 Columns = UCataclysmInventoryComponent::Columns;

	/** Four. */
	constexpr int32 Rows = UCataclysmInventoryComponent::Rows;

	/**
	 * The rarity of a carried item, defaulting to the bottom rung.
	 *
	 * THE SAME FALLBACK UCataclysmDropSpawner::SpawnDropsFor USES. Not every
	 * combination of affixes and enchantments is a rarity -- below Legendary a
	 * piece fills only as many slots as it has affixes -- so an item assembled
	 * by hand can fail to be one of the eight. Everyday is then what it is
	 * drawn as, which is a frame the player can see rather than none.
	 */
	ECataclysmRarity RarityOfCarried(const FCataclysmItem& Item)
	{
		ECataclysmRarity Rarity = ECataclysmRarity::Everyday;
		UCataclysmItemValues::RarityOf(Item.EnchantmentCount,
									   Item.Affixes.Num(), Rarity);
		return Rarity;
	}

	/** Whether this slot's gear is what the cell is about. */
	bool HoldsGear(const FCataclysmCarriedSlot& Slot)
	{
		// GEAR FIRST WHEN A SLOT SOMEHOW HOLDS BOTH. Nothing produces such a
		// slot -- AddItem writes only the item and AddMaterial only the stack --
		// but the struct allows it, and a cell has to draw one thing.
		return !UCataclysmInventoryComponent::SlotIsEmpty(Slot.Item);
	}

	/** Text cut to fit, with three stops saying that it was cut. */
	FString Shortened(const FString& Text, int32 MaxCharacters)
	{
		if (Text.Len() <= MaxCharacters)
		{
			return Text;
		}

		const int32 Room =
			MaxCharacters - FCString::Strlen(UCataclysmInventoryScreen::Ellipsis);
		if (Room <= 0)
		{
			// NO ROOM FOR THE STOPS AND A LETTER BOTH. The letters are worth
			// more than the mark saying there were more of them.
			return Text.Left(FMath::Max(0, MaxCharacters));
		}

		return Text.Left(Room) + UCataclysmInventoryScreen::Ellipsis;
	}
}

float UCataclysmInventoryScreen::CellSizeFor(float ViewportWidth,
											 float ViewportHeight)
{
	// WHAT IS LEFT FOR THE CELLS THEMSELVES once the panel's share of the
	// viewport has given up its padding, its header and the gaps between cells.
	const float AcrossWidth = ViewportWidth * PanelWidthShare
		- PanelPaddingPx * 2.0f - CellGapPx * (Columns - 1);
	const float DownHeight = ViewportHeight * PanelHeightShare
		- PanelPaddingPx * 2.0f - HeaderHeightPx - CellGapPx * (Rows - 1);

	const float Fits = FMath::Min(AcrossWidth / Columns, DownHeight / Rows);

	// THE FLOOR IS ONE PIXEL AND IS NOT A READABLE SIZE. It exists so that a
	// viewport too small to hold the padding cannot produce a negative cell,
	// which would give every rectangle a Max below its Min.
	return FMath::Clamp(Fits, 1.0f, MaxCellPx);
}

FVector2D UCataclysmInventoryScreen::PanelSizeFor(float CellPx)
{
	const float Width = CellPx * Columns + CellGapPx * (Columns - 1)
		+ PanelPaddingPx * 2.0f;
	const float Height = HeaderHeightPx + CellPx * Rows
		+ CellGapPx * (Rows - 1) + PanelPaddingPx * 2.0f;

	return FVector2D(Width, Height);
}

FBox2D UCataclysmInventoryScreen::PanelRectFor(float ViewportWidth,
											   float ViewportHeight)
{
	const FVector2D Size =
		PanelSizeFor(CellSizeFor(ViewportWidth, ViewportHeight));

	const FVector2D Min((ViewportWidth - Size.X) * 0.5,
						(ViewportHeight - Size.Y) * 0.5);

	return FBox2D(Min, Min + Size);
}

FBox2D UCataclysmInventoryScreen::CellRectFor(const FBox2D& Panel, float CellPx,
											  int32 Slot)
{
	if (!Panel.bIsValid || Slot < 0
		|| Slot >= UCataclysmInventoryComponent::SlotCount)
	{
		return FBox2D(ForceInit);
	}

	const int32 Column = Slot % Columns;
	const int32 Row = Slot / Columns;

	const FVector2D Min(
		Panel.Min.X + PanelPaddingPx + Column * (CellPx + CellGapPx),
		Panel.Min.Y + PanelPaddingPx + HeaderHeightPx
			+ Row * (CellPx + CellGapPx));

	return FBox2D(Min, Min + FVector2D(CellPx, CellPx));
}

bool UCataclysmInventoryScreen::PanelCoversPoint(const FBox2D& Panel,
												 const FVector2D& Point)
{
	// IsInsideOrOn RATHER THAN IsInside, for the reason
	// UCataclysmDropPickup::IndexOfNameUnderPoint gives: the strict test
	// excludes the boundary, and a click on the panel's outermost row of pixels
	// is a click on the panel. Here it matters more than it does there, because
	// the alternative is not a click that misses but a move order the player
	// did not give.
	return Panel.bIsValid && Panel.IsInsideOrOn(Point);
}

float UCataclysmInventoryScreen::LabelScaleFor(float CellPx)
{
	return LabelScaleAtMaxCell * FMath::Max(0.0f, CellPx) / MaxCellPx;
}

TArray<FString> UCataclysmInventoryScreen::LabelLinesFor(const FString& Text,
														 int32 MaxCharacters,
														 int32 MaxLines)
{
	TArray<FString> Lines;
	if (Text.IsEmpty() || MaxCharacters < 1 || MaxLines < 1)
	{
		return Lines;
	}

	TArray<FString> Words;
	Text.ParseIntoArray(Words, TEXT(" "), /*InCullEmpty=*/true);

	bool bRanOutOfLines = false;
	for (const FString& Word : Words)
	{
		// THE WORD JOINS THE LINE IT IS ON IF IT FITS, counting the space that
		// would go before it.
		if (Lines.Num() > 0
			&& Lines.Last().Len() + 1 + Word.Len() <= MaxCharacters)
		{
			Lines.Last().Append(TEXT(" ")).Append(Word);
			continue;
		}

		if (Lines.Num() >= MaxLines)
		{
			bRanOutOfLines = true;
			break;
		}

		// A WORD LONGER THAN A WHOLE LINE STILL STARTS ONE and is cut below,
		// rather than being broken in the middle. "Greatswo" and "rd" is worse
		// to read than a shortened "Greatsw...".
		Lines.Add(Word);
	}

	for (FString& Line : Lines)
	{
		Line = Shortened(Line, MaxCharacters);
	}

	if (bRanOutOfLines && Lines.Num() > 0)
	{
		// THE LAST LINE SAYS THE NAME WENT ON, so a player can tell a name that
		// was shortened from one that is short.
		FString& Last = Lines.Last();
		const int32 Room = MaxCharacters - FCString::Strlen(Ellipsis);
		if (Room > 0)
		{
			Last = Last.Left(FMath::Min(Last.Len(), Room)) + Ellipsis;
		}
	}

	return Lines;
}

FString UCataclysmInventoryScreen::LabelFor(
	const FCataclysmCarriedSlot& Slot, const UDataTable* BaseTable,
	const UDataTable* CraftingMaterialTable)
{
	if (HoldsGear(Slot))
	{
		if (!BaseTable)
		{
			return FString();
		}

		const FCataclysmItemBaseRow* Row =
			BaseTable->FindRow<FCataclysmItemBaseRow>(
				Slot.Item.Base, TEXT("UCataclysmInventoryScreen::LabelFor"),
				/*bWarnIfMissing=*/false);
		return Row ? Row->BaseName : FString();
	}

	if (!UCataclysmInventoryComponent::SlotIsEmpty(Slot))
	{
		return UCataclysmDropRoll::MaterialNameOf(CraftingMaterialTable,
												  Slot.Material);
	}

	return FString();
}

FString UCataclysmInventoryScreen::QuantityTextFor(
	const FCataclysmCarriedSlot& Slot)
{
	if (HoldsGear(Slot) || Slot.Material.IsNone() || Slot.Quantity <= 1)
	{
		return FString();
	}

	return FString::FromInt(Slot.Quantity);
}

FLinearColor UCataclysmInventoryScreen::ColourFor(
	const FCataclysmCarriedSlot& Slot, const UDataTable* GearRarityTable,
	const UDataTable* CraftingMaterialTable,
	const UDataTable* MaterialTierTable)
{
	if (UCataclysmInventoryComponent::SlotIsEmpty(Slot))
	{
		return UCataclysmCombatOverlay::ColourFromHex(EmptyCellHex);
	}

	if (HoldsGear(Slot))
	{
		return UCataclysmDropSpawner::ColourFor(GearRarityTable,
												RarityOfCarried(Slot.Item));
	}

	return UCataclysmDropRoll::MaterialColourFor(
		MaterialTierTable,
		UCataclysmDropRoll::MaterialTierOf(CraftingMaterialTable,
										   Slot.Material));
}

int32 UCataclysmInventoryScreen::BorderThicknessFor(
	const FCataclysmCarriedSlot& Slot, const UDataTable* CraftingMaterialTable)
{
	if (UCataclysmInventoryComponent::SlotIsEmpty(Slot))
	{
		return EmptyCellBorderPx;
	}

	if (HoldsGear(Slot))
	{
		return UCataclysmDropPickup::NameBorderThicknessFor(
			RarityOfCarried(Slot.Item));
	}

	return UCataclysmDropPickup::NameBorderThicknessForMaterialTier(
		UCataclysmDropRoll::MaterialTierOf(CraftingMaterialTable,
										   Slot.Material));
}

FString UCataclysmInventoryScreen::HeaderTextFor(int32 Used, int32 Capacity)
{
	// BOTH FIGURES, NOT A FRACTION OR A BAR. The number that matters when the
	// bag fills is how many slots are left, and the design makes a full bag a
	// choice about what to leave on the floor rather than a state to avoid.
	return FString::Printf(TEXT("Carried    %d / %d"), Used, Capacity);
}
