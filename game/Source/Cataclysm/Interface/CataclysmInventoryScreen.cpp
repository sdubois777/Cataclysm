// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmInventoryScreen.h"
#include "Data/CataclysmDataRows.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Engine/DataTable.h"

const TCHAR* UCataclysmInventoryScreen::PanelHex = TEXT("0A0F12");
const TCHAR* UCataclysmInventoryScreen::PanelEdgeHex = TEXT("4A525C");
const TCHAR* UCataclysmInventoryScreen::CellInteriorHex = TEXT("0A0F12");
const TCHAR* UCataclysmInventoryScreen::EmptyCellHex = TEXT("5A6470");
const TCHAR* UCataclysmInventoryScreen::InkHex = TEXT("F5F0EA");

namespace
{
	// NAMED `Grid...` AND NOT `Rows` AND `Columns`, WHICH IS NOT A STYLE
	// PREFERENCE. An anonymous namespace is file scope to the compiler and
	// MODULE scope to a unity build, which concatenates several .cpp files into
	// one translation unit. `Rows` is an ordinary parameter name in this module
	// -- `UCataclysmEnemyModifiers` alone takes `const TArray<FName>& Rows` nine
	// times, and `UCataclysmDropRoll::RollEnchantments` has a lambda that does
	// -- and any of those landing in the same blob as this file hides this
	// constant. Unreal builds with warnings as errors, so the module then stops
	// compiling, in a file nobody edited.
	//
	// IT HAD ALREADY STOPPED. `development` did not compile at 82f9276 for
	// exactly this collision, between this file and
	// `UCataclysmDropRoll::RollEnchantments`, which is issue #1462.
	//
	// IT IS FIXED FROM BOTH ENDS, ON PURPOSE. Pull request #1473 renamed that
	// lambda's parameter to `CandidateRows`, which removes one of the thirteen
	// declarations named `Rows` in this module; this renames what all thirteen
	// would shadow, so none of them can collide with it. Either alone unblocks
	// the build. Both together are what stops the next one.
	//
	// WHY NOBODY SAW IT SOONER, AND IT IS THE PART WORTH REMEMBERING. The build
	// log says `Using 'git status' to determine working set for adaptive
	// non-unity build`. **A file you have modified is compiled on its own**,
	// where nothing else declares the same names, so a session editing either
	// file builds green every time. The collision appears only once the tree is
	// clean and the files are merged into one translation unit. Continuous
	// integration does not compile C++ at all -- that is issue #20 -- so nothing
	// else catches it. Issue #1469 carries the guard that would.

	/** Twelve. Read from the store rather than copied, so there is one answer. */
	constexpr int32 GridColumns = UCataclysmInventoryComponent::Columns;

	/** Four. */
	constexpr int32 GridRows = UCataclysmInventoryComponent::Rows;

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
}

float UCataclysmInventoryScreen::CellSizeFor(float ViewportWidth,
											 float ViewportHeight)
{
	// WHAT IS LEFT FOR THE CELLS THEMSELVES once the panel's share of the
	// viewport has given up its padding, its header and the gaps between cells.
	// EVERY COLUMN THE PANEL HOLDS, not only the carried grid's twelve.
	// The panel of worn gear sits beside it and its columns come out of the
	// same width. Issue #831.
	constexpr int32 AllColumns = GridColumns + ColumnsBeside;
	const float AcrossWidth = ViewportWidth * PanelWidthShare
		- PanelPaddingPx * 2.0f - CellGapPx * (AllColumns - 1);
	const float DownHeight = ViewportHeight * PanelHeightShare
		- PanelPaddingPx * 2.0f - HeaderHeightPx - CellGapPx * (GridRows - 1);

	const float Fits = FMath::Min(AcrossWidth / AllColumns,
								  DownHeight / GridRows);

	// THE FLOOR IS A READABLE SIZE RATHER THAN A GUARD AGAINST A NEGATIVE
	// ONE. See SmallestCellPx: a frame is padding inside its cell, so a cell
	// under twice the thickest frame has no interior left for its label.
	return FMath::Clamp(Fits, SmallestCellPx, MaxCellPx);
}

int32 UCataclysmInventoryScreen::LabelFontSizeFor(float CellPx)
{
	// ROUNDED TO A WHOLE POINT, because a font size is a point size and Slate
	// caches a face per size. A size that changed by a fraction as the window
	// resized would rebuild the atlas for no visible gain.
	const int32 Sized = FMath::RoundToInt(FMath::Max(0.0f, CellPx)
										  * LabelFontShareOfCell);
	return FMath::Max(SmallestLabelFontPx, Sized);
}

int32 UCataclysmInventoryScreen::QuantityFontSizeFor(float CellPx)
{
	const int32 Sized = FMath::RoundToInt(FMath::Max(0.0f, CellPx)
										  * QuantityFontShareOfCell);
	return FMath::Max(SmallestLabelFontPx, Sized);
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

FString UCataclysmInventoryScreen::HeaderTextFor(int32 Used, int32 Capacity,
												 const FString& HeldName)
{
	// BOTH FIGURES, NOT A FRACTION OR A BAR. The number that matters when the
	// bag fills is how many slots are left, and the design makes a full bag a
	// choice about what to leave on the floor rather than a state to avoid.
	const FString Counted =
		FString::Printf(TEXT("Carried    %d / %d"), Used, Capacity);

	// NOTHING SAID WHEN NOTHING IS HELD. A line reading "holding nothing" is
	// there every time the screen is open and tells the player something they
	// already know.
	if (HeldName.IsEmpty())
	{
		return Counted;
	}

	return FString::Printf(TEXT("%s        holding %s"), *Counted, *HeldName);
}
