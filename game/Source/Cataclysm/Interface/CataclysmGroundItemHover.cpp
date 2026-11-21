// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmGroundItemHover.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Interface/CataclysmItemComparison.h"
#include "Interface/CataclysmItemTooltipPanel.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmItem.h"

ECataclysmGroundHover UCataclysmGroundItemHover::KindFor(const ACataclysmDroppedItem* Drop)
{
	// A GOLD PILE IS NOT HOVERED: it has no item, and its tag already states the amount.
	if (!IsValid(Drop) || Drop->IsGold())
	{
		return ECataclysmGroundHover::Nothing;
	}
	if (Drop->IsMaterial())
	{
		return ECataclysmGroundHover::Material;
	}
	return Drop->Item.Base.IsNone() ? ECataclysmGroundHover::Nothing : ECataclysmGroundHover::Gear;
}

FCataclysmCarriedSlot UCataclysmGroundItemHover::SlotFor(const ACataclysmDroppedItem& Drop)
{
	// AS `UCataclysmDropPickup::TakeInto` FILLS ONE: `AddMaterial` with the material and its count, or `AddItem` with
	// the item alone.
	FCataclysmCarriedSlot Slot;
	if (Drop.IsMaterial())
	{
		Slot.Material = Drop.Material;
		Slot.Quantity = Drop.MaterialQuantity;
	}
	else
	{
		Slot.Item = Drop.Item;
	}
	return Slot;
}

bool UCataclysmGroundItemHover::ColumnsFor(const ACataclysmDroppedItem* Drop, UCataclysmEquipmentComponent* Equipment,
										   const UAbilitySystemComponent* Abilities,
										   ACataclysmComparisonStandIn* StandIn, FString& OutLeft, FString& OutRight)
{
	OutLeft.Reset();
	OutRight.Reset();
	if (KindFor(Drop) == ECataclysmGroundHover::Nothing)
	{
		return false;
	}
	UCataclysmItemComparison::TooltipColumnsFor(
		SlotFor(*Drop), Equipment, Abilities, StandIn, /*WornNow=*/nullptr, UCataclysmItemModifiers::LoadBaseTable(),
		UCataclysmDropRoll::LoadAffixTable(), UCataclysmDropRoll::LoadCraftingMaterialTable(),
		UCataclysmDropRoll::LoadPositiveEnchantmentTable(), UCataclysmDropRoll::LoadNegativeEnchantmentTable(), OutLeft,
		OutRight);
	return true;
}

FVector2D UCataclysmGroundItemHover::PanelPositionFor(const FVector2D& Cursor, const FVector2D& Panel,
													  const FVector2D& Viewport)
{
	FVector2D At(Cursor.X + CursorOffsetPx, Cursor.Y + CursorOffsetPx);
	if (At.X + Panel.X > Viewport.X)
	{
		At.X = Cursor.X - CursorOffsetPx - Panel.X;
	}
	if (At.Y + Panel.Y > Viewport.Y)
	{
		At.Y = Cursor.Y - CursorOffsetPx - Panel.Y;
	}
	At.X = FMath::Clamp(At.X, 0.0, FMath::Max(0.0, Viewport.X - Panel.X));
	At.Y = FMath::Clamp(At.Y, 0.0, FMath::Max(0.0, Viewport.Y - Panel.Y));
	return At;
}

bool UCataclysmGroundItemHover::NeedsRebuild(const ACataclysmDroppedItem* Hovered, const ACataclysmDroppedItem* Shown,
											 int32 WornChanges, int32 ShownAtWornChanges)
{
	return Hovered != Shown || (Hovered && WornChanges != ShownAtWornChanges);
}

TSharedRef<SWidget> UCataclysmGroundItemTooltip::RebuildWidget()
{
	// A HOLDER WITH NO LOOK OF ITS OWN, whose content `Show` replaces: the panel inside it is the bag's.
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		Holder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Holder->SetBrushColor(FLinearColor::Transparent);
		Holder->SetPadding(FMargin(0.0f));
		WidgetTree->RootWidget = Holder;
	}
	return Super::RebuildWidget();
}

void UCataclysmGroundItemTooltip::Show(const FString& Left, const FString& Right)
{
	if (Holder)
	{
		Holder->SetContent(FCataclysmItemTooltipPanel::Build(WidgetTree, Left, Right));
	}
}
