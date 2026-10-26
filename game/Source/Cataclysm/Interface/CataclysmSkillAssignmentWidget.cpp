// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmSkillAssignmentWidget.h"

#include "Character/CataclysmPlayerCharacter.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Interface/CataclysmChoiceButton.h"
#include "Interface/CataclysmSkillBar.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Player/CataclysmPlayerState.h"

const TCHAR* UCataclysmSkillAssignmentWidget::BasicAttackLine = TEXT("Basic attack: from the weapon");
const TCHAR* UCataclysmSkillAssignmentWidget::NoSlotSelectedRefusal = TEXT("Click a slot first, then a skill.");
const TCHAR* UCataclysmSkillAssignmentWidget::ClearLabelText = TEXT("Clear the slot");

namespace
{
	/** Between a weapon type and a damage type, as the plan's tool tip and kept line write it: a middle dot. */
	const TCHAR* const SkillScreenDot = TEXT(" \u00B7 ");

	/** A skill's name as the player reads it. The row name only for a row the workbook gave no name. */
	FString SkillScreenName(const FCataclysmWeaponSkill& Skill)
	{
		return Skill.Name.IsEmpty() ? Skill.RowName.ToString() : Skill.Name;
	}
}

FString FCataclysmSkillScreenRow::AsLine() const
{
	const TCHAR* Standing = HeldRow.IsNone() ? TEXT("empty") : (bChosen ? TEXT("chosen") : TEXT("default"));
	return FString::Printf(TEXT("%s: %s (%s)"), *ACataclysmPlayerState::SkillSlotName(Key), *HeldName, Standing);
}

// ---------------------------------------------------------------------------
// Opening, and redrawing
// ---------------------------------------------------------------------------

void UCataclysmSkillAssignmentWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (TitleLabel)
	{
		TitleLabel->SetText(Title);
	}

	Refresh();
}

void UCataclysmSkillAssignmentWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// THE WORN WEAPONS CAN CHANGE WHILE THE SCREEN IS OPEN, and the pool with them (ruled Q9). Asked by the count, so
	// nothing is redrawn and no tool tip is rebuilt on a frame where nothing changed: a tool tip set every frame is
	// never shown (`UCataclysmEquipmentComponent::ChangeCount`, issue #1192).
	RefreshIfGearChanged();
}

void UCataclysmSkillAssignmentWidget::Refresh()
{
	const ACataclysmPlayerCharacter* Character = ShownCharacter();
	const UCataclysmEquipmentComponent* Equipment = Character ? Character->GetEquipment() : nullptr;
	LastChangeCount = Equipment ? Equipment->ChangeCount() : 0;

	ReadModel();
	RedrawPanels();
	WriteLabels();
}

bool UCataclysmSkillAssignmentWidget::RefreshIfGearChanged()
{
	const ACataclysmPlayerCharacter* Character = ShownCharacter();
	const UCataclysmEquipmentComponent* Equipment = Character ? Character->GetEquipment() : nullptr;
	if ((Equipment ? Equipment->ChangeCount() : 0) == LastChangeCount)
	{
		return false;
	}
	Refresh();
	return true;
}

ACataclysmPlayerState* UCataclysmSkillAssignmentWidget::ShownState() const
{
	// THE GAME'S OWN PLAYER FIRST, ALWAYS; the test seam only when there is no owning player state, which is the case
	// a headless test is in and nothing in a running game ever is.
	if (ACataclysmPlayerState* Owning = GetOwningPlayerState<ACataclysmPlayerState>())
	{
		return Owning;
	}
	return PlayerStateForTests;
}

ACataclysmPlayerCharacter* UCataclysmSkillAssignmentWidget::ShownCharacter() const
{
	const ACataclysmPlayerState* State = ShownState();
	return State ? Cast<ACataclysmPlayerCharacter>(State->GetPawn()) : nullptr;
}

const UCataclysmWeaponSlotsComponent* UCataclysmSkillAssignmentWidget::ShownWeaponSlots() const
{
	const ACataclysmPlayerCharacter* Character = ShownCharacter();
	return Character ? Character->FindComponentByClass<UCataclysmWeaponSlotsComponent>() : nullptr;
}

void UCataclysmSkillAssignmentWidget::ReadModel()
{
	ShownRows.Reset();
	ShownPool.Reset();

	const ACataclysmPlayerState* State = ShownState();
	const UCataclysmWeaponSlotsComponent* Slots = ShownWeaponSlots();
	const TArray<FCataclysmSkillChoice> Choices =
		State ? State->GetSkillAssignment() : TArray<FCataclysmSkillChoice>();
	if (Slots)
	{
		ShownPool = Slots->GetSkillPool();
	}

	// THE SIX KEYS IN THE SKILL BAR'S ORDER, so the screen and the bar read alike (ruled Q8). The basic attack is not
	// one of them: it comes from the weapon and cannot be chosen (layer 3's O2).
	for (const ECataclysmAbilitySlot Key : UCataclysmSkillBar::SlotsShown())
	{
		FCataclysmSkillScreenRow& Row = ShownRows.AddDefaulted_GetRef();
		Row.Key = Key;
		const FCataclysmWeaponSkill* Held = Slots ? Slots->GetAvailableSkills().FindByPredicate(
			[Key](const FCataclysmWeaponSkill& Skill) { return Skill.Slot == Key; }) : nullptr;
		Row.HeldName = Held ? SkillScreenName(*Held) : UCataclysmSkillBar::NameForEmptySlot(Key);
		Row.HeldRow = Held ? Held->RowName : NAME_None;

		// A CHOICE IS ON ITS KEY WHEN ITS ROW IS IN THE POOL, and otherwise kept while the key shows its default
		// (the owner's Q5), shown on a line of its own under the key (ruled Q4).
		const FCataclysmSkillChoice* Chosen = Choices.FindByPredicate(
			[Key](const FCataclysmSkillChoice& Choice) { return Choice.Slot == Key; });
		Row.bChosen = Chosen && Held && Held->RowName == Chosen->SkillRow;
		if (Chosen && !Row.bChosen)
		{
			Row.KeptSkillRow = Chosen->SkillRow;
			Row.KeptText = KeptTextFor(Chosen->SkillRow);
		}
	}
}

void UCataclysmSkillAssignmentWidget::RedrawPanels()
{
	// EVERY BUTTON MADE AGAIN, as the character sheet makes its rows again: six keys and a pool of a dozen or so, made
	// on a click or a change of gear and never on a frame.
	const TSubclassOf<UCataclysmChoiceButton> ButtonClass =
		(SlotBox || PoolBox) ? ChoiceButtonClass.LoadSynchronous() : nullptr;

	if (SlotBox)
	{
		SlotBox->ClearChildren();
		SlotButtons.Reset();
		AddLine(BasicAttackLine, LineColour);
		for (const FCataclysmSkillScreenRow& Row : ShownRows)
		{
			if (UCataclysmChoiceButton* Button =
					ButtonClass ? CreateWidget<UCataclysmChoiceButton>(this, ButtonClass) : nullptr)
			{
				Button->OnChoiceClicked.AddDynamic(this, &UCataclysmSkillAssignmentWidget::HandleSlotClicked);
				DescribeSlotButton(*Button, Row);
				SlotBox->AddChild(Button);
				SlotButtons.Add(Button);
			}
			if (!Row.KeptText.IsEmpty())
			{
				AddLine(Row.KeptText, KeptLineColour);
			}
		}
	}

	if (PoolBox)
	{
		PoolBox->ClearChildren();
		PoolButtons.Reset();
		for (const FCataclysmWeaponSkill& Skill : ShownPool)
		{
			if (UCataclysmChoiceButton* Button =
					ButtonClass ? CreateWidget<UCataclysmChoiceButton>(this, ButtonClass) : nullptr)
			{
				Button->OnChoiceClicked.AddDynamic(this, &UCataclysmSkillAssignmentWidget::HandleSkillClicked);
				Button->OnChoiceHovered.AddDynamic(this, &UCataclysmSkillAssignmentWidget::HandleSkillHovered);
				DescribePoolButton(*Button, Skill);
				PoolBox->AddChild(Button);
				PoolButtons.Add(Button);
			}
		}
	}
}

void UCataclysmSkillAssignmentWidget::AddLine(const FString& Text, const FSlateColor& Colour)
{
	UTextBlock* Line = SlotBox ? NewObject<UTextBlock>(this) : nullptr;
	if (!Line)
	{
		return;
	}
	Line->SetText(FText::FromString(Text));
	FSlateFontInfo Font = Line->GetFont();
	Font.Size = LineFontSize;
	Line->SetFont(Font);
	Line->SetColorAndOpacity(Colour);
	SlotBox->AddChild(Line);
}

void UCataclysmSkillAssignmentWidget::WriteLabels()
{
	LastStatus = SelectedSlot == ECataclysmAbilitySlot::None
		? FString(TEXT("Click a slot, then a skill."))
		: FString::Printf(TEXT("The %s slot is selected. Click a skill to put on it."),
						  *ACataclysmPlayerState::SkillSlotName(SelectedSlot));

	if (StatusLabel)
	{
		StatusLabel->SetText(FText::FromString(LastStatus));
	}
	if (RefusalLabel)
	{
		RefusalLabel->SetText(FText::FromString(LastRefusal));
	}
	if (DescriptionLabel)
	{
		DescriptionLabel->SetText(FText::FromString(LastDescription));
	}
	if (ClearLabel)
	{
		ClearLabel->SetText(FText::FromString(ClearLabelText));
	}
	if (ClearButton)
	{
		if (!bClearBound)
		{
			ClearButton->OnClicked.AddDynamic(this, &UCataclysmSkillAssignmentWidget::HandleClearClicked);
			bClearBound = true;
		}
		ClearButton->SetIsEnabled(CanClearSelectedSlot());
	}
}

// ---------------------------------------------------------------------------
// What a click does
// ---------------------------------------------------------------------------

bool UCataclysmSkillAssignmentWidget::SelectSlot(ECataclysmAbilitySlot Key)
{
	if (!UCataclysmWeaponSkills::IsChoosableSlot(Key))
	{
		return false;
	}
	SelectedSlot = Key;
	LastRefusal.Reset();
	RedrawPanels();
	WriteLabels();
	return true;
}

bool UCataclysmSkillAssignmentWidget::PickSkill(FName SkillRow)
{
	// CLICK A SLOT FIRST (ruled Q5): the screen's one sentence of its own.
	if (SelectedSlot == ECataclysmAbilitySlot::None)
	{
		LastRefusal = NoSlotSelectedRefusal;
		WriteLabels();
		return false;
	}
	ACataclysmPlayerState* State = ShownState();
	if (!State)
	{
		LastRefusal = TEXT("There is no character to choose a skill for.");
		WriteLabels();
		return false;
	}

	// THE PLAYER STATE DECIDES AND SAYS WHY, and the screen shows its sentence as it is (ruled Q3 and Q5).
	FString Reason;
	if (!State->AssignSkill(SelectedSlot, SkillRow, Reason))
	{
		LastRefusal = Reason;
		WriteLabels();
		return false;
	}
	LastRefusal.Reset();
	Refresh();
	return true;
}

bool UCataclysmSkillAssignmentWidget::ClearSelectedSlot()
{
	ACataclysmPlayerState* State = ShownState();
	if (!State || !CanClearSelectedSlot())
	{
		return false;
	}
	const bool bForgot = State->ClearSkillChoice(SelectedSlot);
	LastRefusal.Reset();
	Refresh();
	return bForgot;
}

bool UCataclysmSkillAssignmentWidget::CanClearSelectedSlot() const
{
	const ACataclysmPlayerState* State = ShownState();
	const ECataclysmAbilitySlot Key = SelectedSlot;
	return State && UCataclysmWeaponSkills::IsChoosableSlot(Key)
		&& State->GetSkillAssignment().ContainsByPredicate(
			[Key](const FCataclysmSkillChoice& Choice) { return Choice.Slot == Key; });
}

void UCataclysmSkillAssignmentWidget::HandleSlotClicked(FName Value)
{
	SelectSlot(KeyNamed(Value));
}

void UCataclysmSkillAssignmentWidget::HandleSkillClicked(FName Value)
{
	PickSkill(Value);
}

void UCataclysmSkillAssignmentWidget::HandleSkillHovered(FName Value)
{
	// ONLY THE DESCRIPTION MOVES, as on the passive tree: hovering is not an act, and it clears no refusal.
	const FCataclysmWeaponSkill* Skill =
		ShownPool.FindByPredicate([Value](const FCataclysmWeaponSkill& Each) { return Each.RowName == Value; });
	LastDescription = Skill ? ToolTipFor(*Skill, KeyHolding(Value)) : FString();
	if (DescriptionLabel)
	{
		DescriptionLabel->SetText(FText::FromString(LastDescription));
	}
}

void UCataclysmSkillAssignmentWidget::HandleClearClicked()
{
	ClearSelectedSlot();
}

// ---------------------------------------------------------------------------
// What a button says
// ---------------------------------------------------------------------------

void UCataclysmSkillAssignmentWidget::DescribeSlotButton(UCataclysmChoiceButton& Button,
														 const FCataclysmSkillScreenRow& Row) const
{
	Button.SetChoice(FName(*ACataclysmPlayerState::SkillSlotName(Row.Key)), FText::FromString(Row.AsLine()),
					 /*bInChosen=*/Row.Key == SelectedSlot, /*bInAvailable=*/true);
	const FCataclysmWeaponSkill* Held =
		ShownPool.FindByPredicate([&Row](const FCataclysmWeaponSkill& Skill) { return Skill.RowName == Row.HeldRow; });
	Button.SetToolTipText(FText::FromString(Held && !Row.HeldRow.IsNone() ? ToolTipFor(*Held, Row.Key) : FString()));
}

void UCataclysmSkillAssignmentWidget::DescribePoolButton(UCataclysmChoiceButton& Button,
														 const FCataclysmWeaponSkill& Skill) const
{
	// CHOSEN-LOOKING WHEN IT SITS ON A KEY, so the pool says which of its skills are in use.
	const ECataclysmAbilitySlot OnKey = KeyHolding(Skill.RowName);
	Button.SetChoice(Skill.RowName, FText::FromString(SkillScreenName(Skill)),
					 /*bInChosen=*/OnKey != ECataclysmAbilitySlot::None, /*bInAvailable=*/true);
	Button.SetToolTipText(FText::FromString(ToolTipFor(Skill, OnKey)));
}

void UCataclysmSkillAssignmentWidget::DescribeButtonForTests(UCataclysmChoiceButton& Button, FName Value,
															 bool bIsSlot)
{
	if (bIsSlot)
	{
		const ECataclysmAbilitySlot Key = KeyNamed(Value);
		if (const FCataclysmSkillScreenRow* Row = ShownRows.FindByPredicate(
				[Key](const FCataclysmSkillScreenRow& Each) { return Each.Key == Key; }))
		{
			DescribeSlotButton(Button, *Row);
		}
		return;
	}
	if (const FCataclysmWeaponSkill* Skill =
			ShownPool.FindByPredicate([Value](const FCataclysmWeaponSkill& Each) { return Each.RowName == Value; }))
	{
		DescribePoolButton(Button, *Skill);
	}
}

FString UCataclysmSkillAssignmentWidget::ToolTipFor(const FCataclysmWeaponSkill& Skill, ECataclysmAbilitySlot OnKey)
{
	// FOUR LINES AND NO NUMBERS (ruled Q7): showing a skill's numbers would put the designed slot's fallback in a
	// second place, which `CataclysmWeaponSkills.cpp` warns against.
	TArray<FString> Lines;
	Lines.Add(SkillScreenName(Skill));
	const ECataclysmAbilitySlot Designed = UCataclysmWeaponSkills::DesignedSlotOf(Skill);
	FString MadeFor = FString::Printf(TEXT("Made for the %s slot"), *ACataclysmPlayerState::SkillSlotName(Designed));
	if (OnKey != ECataclysmAbilitySlot::None && OnKey != Designed)
	{
		MadeFor += FString::Printf(TEXT(", on the %s key"), *ACataclysmPlayerState::SkillSlotName(OnKey));
	}
	Lines.Add(MadeFor);
	Lines.Add(FString::Printf(TEXT("%s%s%s"), *Skill.WeaponType, SkillScreenDot, *Skill.DamageType));
	Lines.Add(Skill.Description);
	return FString::Join(Lines, TEXT("\n"));
}

FString UCataclysmSkillAssignmentWidget::KeptTextFor(FName SkillRow)
{
	// FROM THE TABLE, because a kept choice's weapon is off and so it is not in the pool. Its row's own name, weapon
	// type and damage type: the row reader that makes a pool entry is private to `CataclysmWeaponSkills.cpp`.
	const UDataTable* Table = UCataclysmWeaponSkills::LoadGeneratedTable();
	const FCataclysmWeaponSkillRow* Found =
		Table ? Table->FindRow<FCataclysmWeaponSkillRow>(SkillRow, TEXT(""), /*bWarnIfRowMissing=*/false) : nullptr;
	if (!Found || Found->SkillName.IsEmpty())
	{
		return FString::Printf(TEXT("Kept: %s. It returns when its weapon is worn."), *SkillRow.ToString());
	}
	return FString::Printf(TEXT("Kept: %s (%s%s%s). It returns when that weapon is worn."), *Found->SkillName,
						   *Found->WeaponType, SkillScreenDot, *Found->DamageType);
}

// ---------------------------------------------------------------------------
// Looking things up in the model, never in the buttons
// ---------------------------------------------------------------------------

ECataclysmAbilitySlot UCataclysmSkillAssignmentWidget::KeyHolding(FName SkillRow) const
{
	const FCataclysmSkillScreenRow* Row = SkillRow.IsNone() ? nullptr : ShownRows.FindByPredicate(
		[SkillRow](const FCataclysmSkillScreenRow& Each) { return Each.HeldRow == SkillRow; });
	return Row ? Row->Key : ECataclysmAbilitySlot::None;
}

ECataclysmAbilitySlot UCataclysmSkillAssignmentWidget::KeyNamed(FName Value)
{
	for (const ECataclysmAbilitySlot Key : UCataclysmSkillBar::SlotsShown())
	{
		if (FName(*ACataclysmPlayerState::SkillSlotName(Key)) == Value)
		{
			return Key;
		}
	}
	return ECataclysmAbilitySlot::None;
}

void UCataclysmSkillAssignmentWidget::SetPlayerStateForTests(ACataclysmPlayerState* State)
{
	PlayerStateForTests = State;
}
