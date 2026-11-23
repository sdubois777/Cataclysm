// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmLootFilterWidget.h"

#include "Components/EditableTextBox.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Interface/CataclysmChoiceButton.h"
#include "Interface/CataclysmLootFilterLayout.h"
#include "Items/CataclysmDropRoll.h"
#include "Player/CataclysmGameInstance.h"

const TCHAR* UCataclysmLootFilterWidget::TitleText = TEXT("Loot filter");

namespace
{
	using FPrefs = UCataclysmLootFilterStore;

	/** The number after a command's colon, or INDEX_NONE when there is none. */
	int32 IndexInCommand(const FString& Value)
	{
		return Value.IsNumeric() ? FCString::Atoi(*Value) : INDEX_NONE;
	}
}

void UCataclysmLootFilterWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (TitleLabel)
	{
		TitleLabel->SetText(FText::FromString(TitleText));
	}
	Refresh();
}

void UCataclysmLootFilterWidget::SetPreferencesForTests(const FCataclysmLootFilterPreferences& InPreferences)
{
	TestPreferences = InPreferences;
	bUseTestPreferences = true;
	Refresh();
}

FCataclysmLootFilterPreferences* UCataclysmLootFilterWidget::Editable()
{
	if (bUseTestPreferences)
	{
		return &TestPreferences;
	}
	UCataclysmGameInstance* Instance = UCataclysmGameInstance::In(this);
	return Instance != nullptr ? &Instance->LootFilterPreferences() : nullptr;
}

const FCataclysmLootFilterPreferences& UCataclysmLootFilterWidget::Preferences() const
{
	static const FCataclysmLootFilterPreferences None = FPrefs::NewPlayer();
	const FCataclysmLootFilterPreferences* Found = const_cast<UCataclysmLootFilterWidget*>(this)->Editable();
	return Found != nullptr ? *Found : None;
}

void UCataclysmLootFilterWidget::SaveIfReal()
{
	if (bUseTestPreferences)
	{
		return;
	}
	if (UCataclysmGameInstance* Instance = UCataclysmGameInstance::In(this))
	{
		Instance->SaveLootFilterPreferences();
	}
}

void UCataclysmLootFilterWidget::KeepPicksValid()
{
	const FCataclysmLootFilterPreferences& Prefs = Preferences();
	FCataclysmLootFilter Filter;
	if (!FPrefs::Find(Prefs, Picked, Filter))
	{
		// NOTHING PICKED, OR A FILTER JUST DELETED: the one in use is picked.
		Filter = FPrefs::ActiveFilter(Prefs);
		Picked = Filter.Name;
		PickedRule = INDEX_NONE;
	}
	else
	{
		Picked = Filter.Name;
	}
	if (!Filter.Rules.IsValidIndex(PickedRule))
	{
		PickedRule = Filter.Rules.Num() > 0 ? FMath::Clamp(PickedRule, 0, Filter.Rules.Num() - 1) : INDEX_NONE;
	}
}

bool UCataclysmLootFilterWidget::Press(const FString& Command)
{
	LastRefusal.Reset();
	FCataclysmLootFilterPreferences* Prefs = Editable();
	if (Prefs == nullptr)
	{
		LastRefusal = TEXT("There is no game instance to keep loot filters in.");
		Refresh();
		return false;
	}

	FString Kind;
	FString Value;
	if (!Command.Split(TEXT(":"), &Kind, &Value))
	{
		return false;
	}

	bool bChanged = false;
	bool bDone = false;
	FString Refusal;
	if (Kind == TEXT("Filter"))
	{
		const TArray<FString> Names = FPrefs::AllNames(*Prefs);
		const int32 Index = IndexInCommand(Value);
		bDone = Names.IsValidIndex(Index);
		if (bDone)
		{
			Picked = Names[Index];
			PickedRule = 0;
		}
	}
	else if (Kind == TEXT("Rule"))
	{
		FCataclysmLootFilter Filter;
		const int32 Index = IndexInCommand(Value);
		bDone = FPrefs::Find(*Prefs, Picked, Filter) && Filter.Rules.IsValidIndex(Index);
		if (bDone)
		{
			PickedRule = Index;
		}
	}
	else if (Kind == TEXT("Do"))
	{
		FString NewName;
		if (Value == TEXT("Use"))
		{
			bDone = bChanged = FPrefs::SetActive(*Prefs, Picked, Refusal);
		}
		else if (Value == TEXT("New"))
		{
			bDone = bChanged = FPrefs::NewFilter(*Prefs, NewName, Refusal);
		}
		else if (Value == TEXT("Copy"))
		{
			bDone = bChanged = FPrefs::CopyFilter(*Prefs, Picked, NewName, Refusal);
		}
		else if (Value == TEXT("Rename"))
		{
			const FString Typed = NameBox != nullptr ? NameBox->GetText().ToString() : TypedName;
			bDone = bChanged = FPrefs::RenameFilter(*Prefs, Picked, Typed, Refusal);
			if (bDone)
			{
				NewName = Typed.TrimStartAndEnd();
			}
		}
		else if (Value == TEXT("Delete"))
		{
			bDone = bChanged = FPrefs::DeleteFilter(*Prefs, Picked, Refusal);
			if (bDone)
			{
				Picked.Reset();
			}
		}
		else if (Value == TEXT("AddRule"))
		{
			bDone = bChanged = FPrefs::AddRule(*Prefs, Picked, Refusal);
			FCataclysmLootFilter Filter;
			if (bDone && FPrefs::Find(*Prefs, Picked, Filter))
			{
				PickedRule = Filter.Rules.Num() - 1;
			}
		}
		else if (Value == TEXT("DeleteRule"))
		{
			bDone = bChanged = FPrefs::RemoveRule(*Prefs, Picked, PickedRule, Refusal);
		}
		else if (Value == TEXT("Up") || Value == TEXT("Down"))
		{
			const int32 Delta = Value == TEXT("Up") ? -1 : 1;
			bDone = bChanged = FPrefs::MoveRule(*Prefs, Picked, PickedRule, Delta, Refusal);
			if (bDone)
			{
				PickedRule += Delta;
			}
		}
		if (bDone && !NewName.IsEmpty())
		{
			Picked = NewName;
			PickedRule = INDEX_NONE;
		}
	}
	else if (Kind == TEXT("Edit"))
	{
		if (Value == TEXT("AffixPick+") || Value == TEXT("AffixPick-"))
		{
			bDone = AffixRows.Num() > 0;
			if (bDone)
			{
				AffixPick = (AffixPick + (Value.EndsWith(TEXT("+")) ? 1 : AffixRows.Num() - 1)) % AffixRows.Num();
			}
		}
		else
		{
			FCataclysmLootFilter Filter;
			if (FPrefs::IsBuiltInName(Picked))
			{
				Refusal = FPrefs::BuiltInRefusal;
			}
			else if (!FPrefs::Find(*Prefs, Picked, Filter) || !Filter.Rules.IsValidIndex(PickedRule))
			{
				Refusal = FPrefs::NoSuchRuleRefusal;
			}
			else
			{
				FCataclysmLootFilterRule Rule = Filter.Rules[PickedRule];
				bDone = UCataclysmLootFilterLayout::EditRule(Rule, Value)
					&& FPrefs::SetRule(*Prefs, Picked, PickedRule, Rule, Refusal);
				bChanged = bDone;
			}
		}
	}

	LastRefusal = bDone ? FString() : Refusal;
	if (bChanged)
	{
		SaveIfReal();
	}
	Refresh();
	return bDone;
}

void UCataclysmLootFilterWidget::Refresh()
{
	if (AffixRows.Num() == 0)
	{
		if (const UDataTable* Affixes = UCataclysmDropRoll::LoadAffixTable())
		{
			AffixRows = Affixes->GetRowNames();
		}
	}
	KeepPicksValid();

	const FCataclysmLootFilterPreferences& Prefs = Preferences();
	const FString InUse = FPrefs::ActiveFilter(Prefs).Name;
	LastStatus = FString::Printf(TEXT("In use: %s. Picked: %s%s."), *InUse, *Picked,
								 FPrefs::IsBuiltInName(Picked) ? TEXT(" (built in: copy it to change it)") : TEXT(""));
	if (StatusLabel)
	{
		StatusLabel->SetText(FText::FromString(LastStatus));
	}
	if (RefusalLabel)
	{
		RefusalLabel->SetText(FText::FromString(LastRefusal));
	}
	Redraw();
}

void UCataclysmLootFilterWidget::AddButton(UPanelWidget* Panel, TSubclassOf<UCataclysmChoiceButton> ButtonClass,
										   const FString& Command, const FString& Label, bool bChosen)
{
	Shown.Add(TPair<FString, FString>(Command, Label));
	UCataclysmChoiceButton* Button =
		Panel != nullptr && ButtonClass != nullptr ? CreateWidget<UCataclysmChoiceButton>(this, ButtonClass) : nullptr;
	if (Button == nullptr)
	{
		return;
	}
	Button->OnChoiceClicked.AddDynamic(this, &UCataclysmLootFilterWidget::HandleClicked);
	Button->SetChoice(FName(*Command), FText::FromString(Label), bChosen, /*bInAvailable=*/true);
	Panel->AddChild(Button);
	Buttons.Add(Button);
}

void UCataclysmLootFilterWidget::Redraw()
{
	// EVERY BUTTON MADE AGAIN on a press, as the skill-choice screen does: a press is a click, never a frame.
	Shown.Reset();
	Buttons.Reset();
	for (UPanelWidget* Panel : {FilterBox.Get(), RuleBox.Get(), EditorBox.Get()})
	{
		if (Panel)
		{
			Panel->ClearChildren();
		}
	}
	const TSubclassOf<UCataclysmChoiceButton> ButtonClass =
		(FilterBox || RuleBox || EditorBox) ? ChoiceButtonClass.LoadSynchronous() : nullptr;

	const FCataclysmLootFilterPreferences& Prefs = Preferences();
	const FString InUse = FPrefs::ActiveFilter(Prefs).Name;
	const TArray<FString> Names = FPrefs::AllNames(Prefs);
	for (int32 Index = 0; Index < Names.Num(); ++Index)
	{
		FCataclysmLootFilter Filter;
		FPrefs::Find(Prefs, Names[Index], Filter);
		AddButton(FilterBox, ButtonClass, FString::Printf(TEXT("Filter:%d"), Index),
				  UCataclysmLootFilterLayout::FilterLine(Names[Index], FPrefs::IsBuiltInName(Names[Index]),
														  Names[Index] == InUse, Filter.Rules.Num()),
				  Names[Index] == Picked);
	}
	AddButton(FilterBox, ButtonClass, TEXT("Do:Use"), TEXT("Use this filter"), false);
	AddButton(FilterBox, ButtonClass, TEXT("Do:New"), TEXT("New filter"), false);
	AddButton(FilterBox, ButtonClass, TEXT("Do:Copy"), TEXT("Copy this filter"), false);
	AddButton(FilterBox, ButtonClass, TEXT("Do:Rename"), TEXT("Rename to the name typed"), false);
	AddButton(FilterBox, ButtonClass, TEXT("Do:Delete"), TEXT("Delete this filter"), false);

	FCataclysmLootFilter Filter;
	FPrefs::Find(Prefs, Picked, Filter);
	const UDataTable* AffixTable = UCataclysmDropRoll::LoadAffixTable();
	for (int32 Index = 0; Index < Filter.Rules.Num(); ++Index)
	{
		AddButton(RuleBox, ButtonClass, FString::Printf(TEXT("Rule:%d"), Index),
				  UCataclysmLootFilterLayout::RuleLine(Filter.Rules[Index], Index, AffixTable), Index == PickedRule);
	}
	AddButton(RuleBox, ButtonClass, TEXT("Do:AddRule"), TEXT("Add a rule"), false);
	AddButton(RuleBox, ButtonClass, TEXT("Do:DeleteRule"), TEXT("Delete this rule"), false);
	AddButton(RuleBox, ButtonClass, TEXT("Do:Up"), TEXT("Move this rule up"), false);
	AddButton(RuleBox, ButtonClass, TEXT("Do:Down"), TEXT("Move this rule down"), false);

	if (Filter.Rules.IsValidIndex(PickedRule))
	{
		for (const FCataclysmLootEditorButton& Each :
			 UCataclysmLootFilterLayout::EditorButtonsFor(Filter.Rules[PickedRule], AffixRows, AffixPick, AffixTable))
		{
			AddButton(EditorBox, ButtonClass, TEXT("Edit:") + Each.Command, Each.Label, Each.bChosen);
		}
	}
}

void UCataclysmLootFilterWidget::HandleClicked(FName Value)
{
	Press(Value.ToString());
}
