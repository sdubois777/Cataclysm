// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmItemTooltipPanel.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Interface/CataclysmInventoryScreen.h"

UWidget* FCataclysmItemTooltipPanel::Build(UWidgetTree* Tree, const FString& Left, const FString& Right)
{
	using FScreen = UCataclysmInventoryScreen;
	if (!Tree)
	{
		return nullptr;
	}

	UBorder* Back = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Back->SetBrush(FSlateColorBrush(FScreen::PanelColour()));
	Back->SetPadding(FMargin(FScreen::PanelPaddingPx * 0.5f));

	UHorizontalBox* Across = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Back->SetContent(Across);

	UTextBlock* Hovered = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Hovered->SetText(FText::FromString(Left));
	Hovered->SetColorAndOpacity(FSlateColor(FScreen::InkColour()));
	Hovered->SetFontSize(static_cast<float>(FScreen::ComparisonFontPx));
	Across->AddChildToHorizontalBox(Hovered)->SetVerticalAlignment(VAlign_Top);

	if (!Right.IsEmpty())
	{
		UTextBlock* Worn = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Worn->SetText(FText::FromString(Right));
		Worn->SetColorAndOpacity(FSlateColor(FScreen::InkColour()));
		Worn->SetFontSize(static_cast<float>(FScreen::ComparisonFontPx));
		UHorizontalBoxSlot* WornSlot = Across->AddChildToHorizontalBox(Worn);
		WornSlot->SetVerticalAlignment(VAlign_Top);
		WornSlot->SetPadding(FMargin(FScreen::PanelPaddingPx, 0.0f, 0.0f, 0.0f));
	}
	return Back;
}
