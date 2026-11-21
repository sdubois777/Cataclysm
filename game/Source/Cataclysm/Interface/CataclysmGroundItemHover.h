// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CataclysmGroundItemHover.generated.h"

class ACataclysmComparisonStandIn;
class ACataclysmDroppedItem;
class UAbilitySystemComponent;
class UBorder;
class UCataclysmEquipmentComponent;

/** What a drop under the cursor gets when hovered. Issue #2807. */
UENUM()
enum class ECataclysmGroundHover : uint8
{
	/** No drop, a gold pile, or a drop holding nothing: no tooltip. */
	Nothing,
	/** A piece of gear: its lines, compared with what is worn. */
	Gear,
	/** A crafting material: its own lines, as in the bag. */
	Material,
};

/**
 * Hovering an item on the ground shows the bag's tooltip and its comparison with what is worn. Issue #2807, from the
 * owner's playtest of 2026-11-21: "I would like that same gear comparison thing we have in the inventory to pop up when
 * you hover over an item on the ground. That way you can tell if it's worth picking up before clogging your inventory".
 *
 * THE SAME TEXT AND THE SAME PANEL AS THE BAG: the text from `UCataclysmItemComparison::TooltipColumnsFor` and the panel
 * from `FCataclysmItemTooltipPanel::Build`, which the bag calls too. SHOWN WITH NO KEY HELD, as the bag shows it (the
 * coordinating session's ruling Q1 of 2026-11-21, after issue #832's ruling (d)).
 *
 * WHAT IS HOVERED IS WHAT A CLICK WOULD PICK UP: the drop whose name tag is under the cursor, by
 * `ACataclysmPlayerController::DropUnderCursor`, the click's own test. Tags close together are already pushed apart, and
 * the topmost of two overlapping tags is the one a click takes, so one tooltip shows, for that drop.
 *
 * EVERY JUDGEMENT IS A STATIC OVER PLAIN VALUES, for the reason `UCataclysmCreaturePanel` gives: no test here can see a
 * widget reach the screen (-nullrhi). `ACataclysmPlayerController::RefreshGroundItemHover` asks these each frame and
 * draws `UCataclysmGroundItemTooltip`.
 */
UCLASS()
class CATACLYSM_API UCataclysmGroundItemHover : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** What `Drop` gets when hovered: nothing for none, for a gold pile, or for a drop holding nothing. */
	static ECataclysmGroundHover KindFor(const ACataclysmDroppedItem* Drop);

	/** The slot the bag would hold `Drop` in, as picking it up fills one: the item, or the material and its count. */
	static FCataclysmCarriedSlot SlotFor(const ACataclysmDroppedItem& Drop);

	/**
	 * The tooltip's two columns for `Drop`, through `UCataclysmItemComparison::TooltipColumnsFor`: the bag's text for
	 * the same item and the same worn gear. `OutRight` is empty when nothing is compared.
	 *
	 * @return whether there is anything to show; false, with both columns emptied, when `KindFor` says nothing
	 */
	static bool ColumnsFor(const ACataclysmDroppedItem* Drop, UCataclysmEquipmentComponent* Equipment,
						   const UAbilitySystemComponent* Abilities, ACataclysmComparisonStandIn* StandIn,
						   FString& OutLeft, FString& OutRight);

	/**
	 * Where the panel's top left goes, in viewport pixels: below and to the right of the cursor by `CursorOffsetPx`;
	 * to its left when that would cross the right edge, above it when it would cross the bottom; and held inside the
	 * viewport, at its top left when the panel is larger than the viewport.
	 */
	static FVector2D PanelPositionFor(const FVector2D& Cursor, const FVector2D& Panel, const FVector2D& Viewport);

	/** The gap between the cursor and the panel, in viewport pixels, so the panel never sits under the cursor. */
	static constexpr float CursorOffsetPx = 16.0f;

	/**
	 * Whether the text must be worked out again: a different drop is hovered, or what is worn has changed since it was
	 * worked out (the equipment's change count). THE HOVER MUST NOT COST THE FRAME, the coordinating session's
	 * condition of 2026-11-21: a comparison reads the character's totals twice, so it runs on a change, not every frame.
	 */
	static bool NeedsRebuild(const ACataclysmDroppedItem* Hovered, const ACataclysmDroppedItem* Shown,
							 int32 WornChanges, int32 ShownAtWornChanges);
};

/**
 * The panel the ground hover shows beside the cursor: `FCataclysmItemTooltipPanel`'s panel inside a holder with no
 * look of its own, so it looks as the bag's tooltip does. Issue #2807.
 *
 * NEVER HIT-TESTED, so it cannot take a click or count as the cursor being over the interface.
 */
UCLASS()
class CATACLYSM_API UCataclysmGroundItemTooltip : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Replaces what the panel shows with these two columns. */
	void Show(const FString& Left, const FString& Right);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UPROPERTY()
	TObjectPtr<UBorder> Holder;
};
