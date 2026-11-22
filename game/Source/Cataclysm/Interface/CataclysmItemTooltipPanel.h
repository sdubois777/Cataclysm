// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UWidget;
class UWidgetTree;

/**
 * The panel an item's tooltip is drawn in: the bag's, and since issue #2807 the ground hover's, so the two look the
 * same. Its text comes from `UCataclysmItemComparison::TooltipColumnsFor`, which both call too.
 *
 * WHAT IT DRAWS, unchanged from `UCataclysmInventoryWidget::MakeComparisonToolTip`, where it was built until this: the
 * screen's panel colour behind, the hovered item's lines on the left, and, when there is a right column, what is worn
 * beside them, both in the screen's ink at its comparison size.
 *
 * NO WIDGET TEST CAN SEE IT. The automation command passes -nullrhi, and issue #650 records that a widget does not move
 * that wall; the text it is given is what the tests check.
 */
struct CATACLYSM_API FCataclysmItemTooltipPanel
{
	/**
	 * Builds the panel in `Tree`, the widget tree of the widget that will hold it.
	 *
	 * @param Left   the hovered item's column
	 * @param Right  what is worn, or empty for no second column
	 */
	static UWidget* Build(UWidgetTree* Tree, const FString& Left, const FString& Right);
};
