// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmFloorModifierPanel.h"

#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Dungeon/CataclysmDungeonModifierTable.h"

void UCataclysmFloorModifierPanel::NativeConstruct()
{
	Super::NativeConstruct();

	// NEVER IN THE WAY OF A CLICK. See the header: this is drawn over the game
	// while it is played, and a panel that caught the mouse would stop
	// click-to-move working wherever it sits.
	SetVisibility(ESlateVisibility::HitTestInvisible);

	Redraw();
}

void UCataclysmFloorModifierPanel::SetFloorModifiers(
	const TArray<FName>& RowKeys, int32 InFloorNumber,
	const TMap<FName, FString>& LiveCounts)
{
	FloorNumber = FMath::Max(1, InFloorNumber);
	Lines = UCataclysmFloorModifierPanelLayout::LinesFor(
		RowKeys, UCataclysmDungeonModifierTable::LoadDungeonModifierTable(),
		LiveCounts); // A panel's: what each row is counting. Tested, issue #1841.
	Redraw();
}

void UCataclysmFloorModifierPanel::Redraw()
{
	if (HeadingLabel != nullptr)
	{
		// THE MODIFIERS ONLY: the corrupted double's line is no modifier. Issue #2688.
		int32 Modifiers = 0;
		for (const FCataclysmFloorModifierLine& Line : Lines)
		{
			Modifiers += Line.bIsTheConsumptionDouble ? 0 : 1;
		}
		HeadingLabel->SetText(FText::FromString(
			UCataclysmFloorModifierPanelLayout::HeadingFor(FloorNumber, Modifiers)));
	}

	if (ModifierBox == nullptr)
	{
		return;
	}

	ModifierBox->ClearChildren();
	Rows.Reset();

	for (const FCataclysmFloorModifierLine& Line : Lines)
	{
		const bool bDim = !Line.bIsTheConsumptionDouble
			&& (Line.Built == ECataclysmModifierBuilt::NotBuilt || !Line.bIsARow);
		AddRow(UCataclysmFloorModifierPanelLayout::NameLineFor(Line), NameFontSize, bDim);
		if (!Line.Description.IsEmpty())
		{
			AddRow(Line.Description, DescriptionFontSize, bDim);
		}
	}
}

void UCataclysmFloorModifierPanel::AddRow(const FString& Text, int32 FontSize, bool bDim)
{
	UTextBlock* Row = NewObject<UTextBlock>(this);
	if (Row == nullptr)
	{
		return;
	}

	Row->SetText(FText::FromString(Text));

	// WRAPPED, because a description is a sentence or three and the panel is a
	// corner of the screen rather than a window.
	Row->SetAutoWrapText(true);

	FSlateFontInfo Font = Row->GetFont();
	Font.Size = FontSize;
	Row->SetFont(Font);
	Row->SetColorAndOpacity(bDim ? DimColour : RowColour);

	ModifierBox->AddChild(Row);
	Rows.Add(Row);
}
