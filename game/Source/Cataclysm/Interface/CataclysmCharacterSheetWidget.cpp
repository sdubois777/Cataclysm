// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmCharacterSheetWidget.h"

#include "AbilitySystemComponent.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Interface/CataclysmChoiceButton.h"
#include "Player/CataclysmGameMode.h"
#include "Player/CataclysmPlayerState.h"

namespace
{
	/** The name an attribute's button carries. The attribute, written out. */
	FName AttributeButtonName(const FString& Attribute)
	{
		return FName(*Attribute);
	}
}

// ---------------------------------------------------------------------------
// Building and rewriting
// ---------------------------------------------------------------------------

void UCataclysmCharacterSheetWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (TitleLabel)
	{
		TitleLabel->SetText(Title);
	}

	Refresh();
}

void UCataclysmCharacterSheetWidget::Refresh()
{
	if (AttributeButtons.IsEmpty())
	{
		BuildAttributeButtons();
	}

	WriteStatRows();
	WriteStatus();
}

UAbilitySystemComponent* UCataclysmCharacterSheetWidget::ShownAbilitySystem() const
{
	// THE GAME'S OWN CHARACTER FIRST, ALWAYS. The test seam is only reached when
	// there is no owning player state at all, which is the case a headless test
	// is in and nothing in a running game ever is.
	if (const ACataclysmPlayerState* State = Points())
	{
		if (UAbilitySystemComponent* ASC = State->GetAbilitySystemComponent())
		{
			return ASC;
		}
	}

	return AbilitySystemForTests;
}

void UCataclysmCharacterSheetWidget::SetAbilitySystemForTests(
	UAbilitySystemComponent* ASC)
{
	AbilitySystemForTests = ASC;
}

void UCataclysmCharacterSheetWidget::SetDifficultyTierForTests(int32 Tier)
{
	DifficultyTierForTests = Tier;
}

int32 UCataclysmCharacterSheetWidget::ShownDifficultyTier() const
{
	// WHAT A TEST SAID IT IS, BEFORE ANYTHING ELSE, for the reason the empire
	// overview's panel size seam exists: a headless test has no world with a
	// game mode in it, so the tier would be the same number for ever and no test
	// could make the resistance penalty move. Zero means nothing was said.
	if (DifficultyTierForTests > 0)
	{
		return DifficultyTierForTests;
	}

	return ACataclysmGameMode::DifficultyTierIn(this);
}

ACataclysmPlayerState* UCataclysmCharacterSheetWidget::Points() const
{
	return GetOwningPlayerState<ACataclysmPlayerState>();
}

// ---------------------------------------------------------------------------
// The stat rows
// ---------------------------------------------------------------------------

void UCataclysmCharacterSheetWidget::AddRow(const FString& Text, bool bIsHeading)
{
	if (StatBox == nullptr)
	{
		return;
	}

	UTextBlock* Row = NewObject<UTextBlock>(this);
	if (Row == nullptr)
	{
		return;
	}

	Row->SetText(FText::FromString(Text));

	// THE LOOK COMES FROM PROPERTIES THE BLUEPRINT SETS, and it has to. A text
	// block made here rather than placed in the designer carries the engine's
	// own defaults, which are small and nearly black -- invisible on this
	// project's nearly black panel. Every screen that builds its children at run
	// time has this problem; the empire overview and the passive tree avoid it
	// by copying a Widget Blueprint, and a row of words is not worth a Blueprint
	// of its own.
	FSlateFontInfo Font = Row->GetFont();
	Font.Size = bIsHeading ? HeadingFontSize : RowFontSize;
	Row->SetFont(Font);
	Row->SetColorAndOpacity(bIsHeading ? HeadingColour : RowColour);

	StatBox->AddChild(Row);
	StatRows.Add(Row);
}

void UCataclysmCharacterSheetWidget::WriteStatRows()
{
	if (StatBox == nullptr)
	{
		return;
	}

	// EVERY ROW REPLACED RATHER THAN EDITED. The sheet is 51 lines and it is
	// rewritten only when the screen is opened, so there is nothing to gain from
	// keeping the old ones and a real cost to getting the pairing wrong.
	StatBox->ClearChildren();
	StatRows.Reset();

	const UAbilitySystemComponent* ASC = ShownAbilitySystem();
	const int32 Tier = ShownDifficultyTier();

	for (const ECataclysmSheetGroup Group : UCataclysmCharacterSheetLayout::Groups())
	{
		AddRow(UCataclysmCharacterSheetLayout::HeadingFor(Group), /*bIsHeading*/ true);

		for (const FCataclysmStatLine& Line :
			 UCataclysmCharacterSheetLayout::LinesIn(Group, ASC, Tier))
		{
			AddRow(Line.AsLine(), /*bIsHeading*/ false);
		}
	}
}

// ---------------------------------------------------------------------------
// The eight attributes
// ---------------------------------------------------------------------------

void UCataclysmCharacterSheetWidget::BuildAttributeButtons()
{
	if (AttributeBox == nullptr)
	{
		return;
	}

	TSubclassOf<UCataclysmChoiceButton> ButtonClass =
		ChoiceButtonClass.LoadSynchronous();
	if (!ButtonClass)
	{
		return;
	}

	for (const FString& Attribute : FCataclysmAttributePoints::Names())
	{
		UCataclysmChoiceButton* Button =
			CreateWidget<UCataclysmChoiceButton>(this, ButtonClass);
		if (!Button)
		{
			continue;
		}

		Button->OnChoiceClicked.AddDynamic(
			this, &UCataclysmCharacterSheetWidget::HandleAttributeClicked);

		AttributeBox->AddChild(Button);
		AttributeButtons.Add(Button);
	}
}

void UCataclysmCharacterSheetWidget::WriteStatus()
{
	const ACataclysmPlayerState* State = Points();
	const int32 Unspent = State ? State->AttributePointsUnspent() : 0;

	if (StatusLabel)
	{
		StatusLabel->SetText(FText::FromString(FString::Printf(
			TEXT("Level %d   Difficulty tier %d"),
			State ? State->GetCharacterLevel() : 0, ShownDifficultyTier())));
	}

	if (PointsLabel)
	{
		PointsLabel->SetText(FText::FromString(
			UCataclysmCharacterSheetLayout::UnspentPointsText(Unspent)));
	}

	const TArray<FString> Names = FCataclysmAttributePoints::Names();

	for (int32 Index = 0; Index < AttributeButtons.Num(); ++Index)
	{
		UCataclysmChoiceButton* Button = AttributeButtons[Index];
		if (!Button || !Names.IsValidIndex(Index))
		{
			continue;
		}

		const int32 Spent = State
			? State->GetSpentAttributePoints().PointsIn(Names[Index])
			: 0;

		// UNAVAILABLE WHEN THERE IS NOTHING TO SPEND, rather than hidden. A
		// player with no points still has to be able to read what they have put
		// into each attribute, and a list that changed length as points were
		// earned would move every button under the cursor.
		Button->SetChoice(AttributeButtonName(Names[Index]),
						  FText::FromString(FString::Printf(
							  TEXT("%s %d"), *Names[Index], Spent)),
						  /*bChosen*/ Spent > 0,
						  /*bAvailable*/ Unspent > 0);
	}
}

void UCataclysmCharacterSheetWidget::HandleAttributeClicked(FName Value)
{
	const FString Attribute = AttributeForButton(Value);
	if (Attribute.IsEmpty())
	{
		return;
	}

	ACataclysmPlayerState* State = Points();
	if (State == nullptr)
	{
		if (RefusalLabel)
		{
			RefusalLabel->SetText(NSLOCTEXT(
				"Cataclysm", "SheetNoCharacter",
				"There is no character to spend a point on."));
		}
		return;
	}

	// THE PLAYER STATE REFUSES AND SAYS WHY, and this screen shows that reason
	// rather than inventing one. `SpendAttributePoints` is refused rather than
	// clamped on purpose, so the two possible refusals -- an unknown attribute
	// and not enough points -- read differently.
	FString Reason;
	if (!State->SpendAttributePoints(Attribute, 1, Reason))
	{
		if (RefusalLabel)
		{
			RefusalLabel->SetText(FText::FromString(Reason));
		}
		return;
	}

	if (RefusalLabel)
	{
		RefusalLabel->SetText(FText::GetEmpty());
	}

	// THE WHOLE SHEET AND NOT ONLY THE BUTTON. An attribute point moves the
	// stats it scales, which is the entire reason a player spends one, and a
	// screen that showed the new point count beside the old health total would
	// be worse than one that showed neither.
	Refresh();
}

FString UCataclysmCharacterSheetWidget::AttributeForButton(FName Value) const
{
	for (const FString& Attribute : FCataclysmAttributePoints::Names())
	{
		if (AttributeButtonName(Attribute) == Value)
		{
			return Attribute;
		}
	}

	return FString();
}

// ---------------------------------------------------------------------------
// What a test can read
// ---------------------------------------------------------------------------

FString UCataclysmCharacterSheetWidget::RowText(int32 Index) const
{
	if (!StatRows.IsValidIndex(Index) || StatRows[Index] == nullptr)
	{
		return FString();
	}

	return StatRows[Index]->GetText().ToString();
}

FString UCataclysmCharacterSheetWidget::StatusText() const
{
	return StatusLabel ? StatusLabel->GetText().ToString() : FString();
}

FString UCataclysmCharacterSheetWidget::PointsText() const
{
	return PointsLabel ? PointsLabel->GetText().ToString() : FString();
}

FString UCataclysmCharacterSheetWidget::RefusalText() const
{
	return RefusalLabel ? RefusalLabel->GetText().ToString() : FString();
}
