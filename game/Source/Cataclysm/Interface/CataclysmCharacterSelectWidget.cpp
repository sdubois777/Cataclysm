// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmCharacterSelectWidget.h"

#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Interface/CataclysmChoiceButton.h"
#include "Kismet/GameplayStatics.h"
#include "Player/CataclysmFrontEndGameMode.h"
#include "Player/CataclysmGameInstance.h"
#include "Save/CataclysmSaveWriter.h"

FString UCataclysmCharacterSelectWidget::RowText(const FCataclysmCharacterSummary& Row)
{
	if (!Row.bReadable)
	{
		return FString::Printf(TEXT("Unreadable save, %s: %s"), *Row.SlotName, *Row.Why);
	}

	const UEnum* Lethality = StaticEnum<ECataclysmLethality>();
	const UEnum* Population = StaticEnum<ECataclysmPopulation>();
	FString Run = TEXT("no run");
	if (Row.bInARun && !Row.bRunReadable)
	{
		Run = TEXT("a run that cannot be read");
	}
	else if (Row.bInARun)
	{
		Run = FString::Printf(TEXT("in a run: day %d"), Row.Day);
		if (Row.Floor > 0)
		{
			Run += FString::Printf(TEXT(", floor %d of %s"), Row.Floor, *Row.Dungeon.ToString());
		}
	}
	return FString::Printf(TEXT("%s, level %d, %s, tier %d%s, %s, %s, %s"),
						   Row.Name.IsEmpty() ? TEXT("(unnamed)") : *Row.Name, Row.Level,
						   *Lethality->GetDisplayNameTextByValue(static_cast<int64>(Row.Partition.Lethality)).ToString(),
						   Row.DifficultyTier,
						   Row.Partition.bSoloSelfFound ? TEXT(", Solo Self-Found") : TEXT(""),
						   *Population->GetDisplayNameTextByValue(static_cast<int64>(Row.Partition.Population)).ToString(),
						   Row.WeaponType.IsEmpty() ? TEXT("no weapon type") : *Row.WeaponType, *Run);
}

bool UCataclysmCharacterSelectWidget::MapToContinue(const FCataclysmCharacterSummary& Row, FString& OutMap,
													 FString& OutWhy)
{
	if (!Row.bReadable)
	{
		OutWhy = FString::Printf(TEXT("this save cannot be read: %s"), *Row.Why);
		return false;
	}
	// A RUN SAVED INSIDE A DUNGEON LOADS ONLY IN A DUNGEON LEVEL (`FCataclysmSaveLoad::CharacterAndRun` refuses it
	// anywhere else); every other run, and a character in none, loads in the sandbox.
	const bool bInADungeon = Row.bInARun && Row.bRunReadable && Row.Floor > 0 && Row.Dungeon != FName(TEXT("Sandbox"));
	OutMap = bInADungeon ? DungeonMap : SandboxMap; // A Continue's: the level its run needs.
	return true;
}

void UCataclysmCharacterSelectWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (NewButton)
	{
		NewButton->OnClicked.AddDynamic(this, &UCataclysmCharacterSelectWidget::HandleNewClicked);
	}
	if (DeleteButton)
	{
		DeleteButton->OnClicked.AddDynamic(this, &UCataclysmCharacterSelectWidget::HandleDeleteClicked);
	}
	if (ConfirmDeleteButton)
	{
		ConfirmDeleteButton->OnClicked.AddDynamic(this, &UCataclysmCharacterSelectWidget::HandleConfirmDeleteClicked);
	}
	if (CancelButton)
	{
		CancelButton->OnClicked.AddDynamic(this, &UCataclysmCharacterSelectWidget::HandleCancelClicked);
	}
	if (ConfirmNameBox)
	{
		ConfirmNameBox->OnTextChanged.AddDynamic(this, &UCataclysmCharacterSelectWidget::HandleConfirmNameChanged);
	}
	ShowConfirmation();
	if (TitleLabel)
	{
		TitleLabel->SetText(FText::FromString(TEXT("Choose a character")));
	}
	Refresh();
}

void UCataclysmCharacterSelectWidget::Refresh()
{
	Shown = FCataclysmSaveLoad::SavedCharacters(UCataclysmSaveWriter::UserIndex);

	// THE REASON THE LAST LOAD WAS REFUSED, shown once.
	if (UCataclysmGameInstance* Instance = OwnInstance(); Instance && !Instance->LastLoadRefusal.IsEmpty())
	{
		ShowRefusal(Instance->LastLoadRefusal);
		Instance->LastLoadRefusal.Empty();
	}

	ShowRows();
}

void UCataclysmCharacterSelectWidget::ShowRows()
{
	if (TitleLabel)
	{
		TitleLabel->SetText(FText::FromString(bDeleteMode ? TEXT("Choose a character to delete")
														  : TEXT("Choose a character")));
	}
	if (!CharacterList)
	{
		return;
	}
	CharacterList->ClearChildren();
	const TSubclassOf<UCataclysmChoiceButton> ButtonClass = ChoiceButtonClass.LoadSynchronous();
	if (!ButtonClass)
	{
		return;
	}
	for (int32 Index = 0; Index < Shown.Num(); ++Index)
	{
		if (UCataclysmChoiceButton* Button = CreateWidget<UCataclysmChoiceButton>(this, ButtonClass))
		{
			// IN DELETE MODE AN UNREADABLE ROW IS CLICKABLE TOO: it holds one of the 24, and deleting is how it is freed (Q2).
			Button->SetChoice(FName(*FString::FromInt(Index)), FText::FromString(RowText(Shown[Index])),
							  /*bInChosen=*/false, /*bInAvailable=*/bDeleteMode || Shown[Index].bReadable);
			Button->OnChoiceClicked.AddDynamic(this, &UCataclysmCharacterSelectWidget::HandleRowClicked);
			CharacterList->AddChild(Button);
		}
	}
}

FString UCataclysmCharacterSelectWidget::ConfirmationWordFor(const FCataclysmCharacterSummary& Row)
{
	return Row.bReadable && !Row.Name.IsEmpty() ? Row.Name : FString(DeleteWord);
}

bool UCataclysmCharacterSelectWidget::ConfirmationMatches(const FCataclysmCharacterSummary& Row, const FString& InTyped)
{
	return InTyped.Equals(ConfirmationWordFor(Row), ESearchCase::CaseSensitive); // A delete's: the name typed exactly.
}

FString UCataclysmCharacterSelectWidget::DeleteWarning(const FCataclysmCharacterSummary& Row)
{
	if (!Row.bReadable)
	{
		return FString::Printf(
			TEXT("Delete the unreadable save %s? This cannot be undone. It cannot be read, so whether it was Solo "
				 "Self-Found, with its own empire upgrade tree and private stash, cannot be told. Any run it had cannot be "
				 "found and stays, and its backups are kept, as the only way to recover it. Type %s to confirm."),
			*Row.SlotName, DeleteWord);
	}
	FString Text = FString::Printf(TEXT("Delete %s? This cannot be undone."),
								   Row.Name.IsEmpty() ? TEXT("this unnamed character") : *Row.Name);
	if (Row.bInARun && Row.bRunReadable)
	{
		Text += FString::Printf(TEXT(" Its run, on day %d, is deleted with it."), Row.Day);
	}
	else if (Row.bInARun)
	{
		Text += TEXT(" Its run is deleted with it.");
	}
	if (Row.Partition.bSoloSelfFound) // A delete's: the Solo Self-Found warning.
	{
		Text += TEXT(" It is Solo Self-Found: its own empire upgrade tree and its private stash are destroyed with it.");
	}
	Text += FString::Printf(TEXT(" Type %s to confirm."), Row.Name.IsEmpty() ? DeleteWord : TEXT("its name"));
	return Text;
}

void UCataclysmCharacterSelectWidget::SetDeleteMode(bool bOn)
{
	bDeleteMode = bOn;
	CancelDelete();
	ShowRows();
}

bool UCataclysmCharacterSelectWidget::AskToDeleteAt(int32 Index)
{
	if (!Shown.IsValidIndex(Index))
	{
		ShowRefusal(TEXT("there is no such character"));
		return false;
	}
	PendingDelete = Index;
	Typed.Empty();
	Confirmation = DeleteWarning(Shown[Index]);
	if (ConfirmNameBox)
	{
		ConfirmNameBox->SetText(FText::GetEmpty());
	}
	ShowConfirmation();
	return true;
}

void UCataclysmCharacterSelectWidget::SetConfirmationTyped(const FString& InTyped)
{
	Typed = InTyped;
	ShowConfirmation();
}

bool UCataclysmCharacterSelectWidget::CanConfirmDelete() const
{
	return Shown.IsValidIndex(PendingDelete) && ConfirmationMatches(Shown[PendingDelete], Typed);
}

bool UCataclysmCharacterSelectWidget::ConfirmDelete()
{
	if (!CanConfirmDelete()) // A delete's: refused until the name is typed.
	{
		ShowRefusal(Shown.IsValidIndex(PendingDelete)
						? FString::Printf(TEXT("type %s exactly to delete it"),
										  *ConfirmationWordFor(Shown[PendingDelete]))
						: FString(TEXT("choose a character to delete first")));
		return false;
	}
	TArray<FCataclysmCharacterSummary> Others = Shown;
	Others.RemoveAt(PendingDelete);
	FString Why;
	const bool bDeleted = FCataclysmSaveLoad::DeleteCharacter(Shown[PendingDelete], Others, UCataclysmSaveWriter::UserIndex,
															  GetWorld(), Deleted, Why);
	if (!bDeleted)
	{
		ShowRefusal(Why);
		return false;
	}
	ShowRefusal(FString());
	bDeleteMode = false;
	CancelDelete();
	Refresh();
	return true;
}

void UCataclysmCharacterSelectWidget::CancelDelete()
{
	PendingDelete = INDEX_NONE;
	Typed.Empty();
	Confirmation.Empty();
	ShowConfirmation();
}

void UCataclysmCharacterSelectWidget::ShowConfirmation()
{
	const bool bOpen = Shown.IsValidIndex(PendingDelete);
	if (ConfirmPanel)
	{
		ConfirmPanel->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (ConfirmText)
	{
		ConfirmText->SetText(FText::FromString(Confirmation));
	}
	if (ConfirmDeleteButton)
	{
		ConfirmDeleteButton->SetIsEnabled(CanConfirmDelete());
	}
}

bool UCataclysmCharacterSelectWidget::ContinueAt(int32 Index, UCataclysmGameInstance* Instance)
{
	if (!Shown.IsValidIndex(Index))
	{
		ShowRefusal(TEXT("there is no such character"));
		return false;
	}
	FString Map;
	FString Why;
	if (!MapToContinue(Shown[Index], Map, Why))
	{
		ShowRefusal(Why);
		return false;
	}
	Instance = Instance ? Instance : OwnInstance();
	if (!Instance)
	{
		ShowRefusal(TEXT("there is no game instance of this game's class to hand the character to"));
		return false;
	}
	Instance->PendingCharacterId = Shown[Index].CharacterId; // A Continue's: the character handed over.
	Instance->LastLoadRefusal.Empty();
	Open(Map);
	return true;
}

bool UCataclysmCharacterSelectWidget::StartNew(UCataclysmGameInstance* Instance)
{
	bNewCharacterRequested = false;
	if (!HasRoomForANewCharacter(Shown.Num()))
	{
		ShowRefusal(FString::Printf(TEXT("this account holds %d characters, the most it may; delete one to make another"),
									MostCharacters));
		return false;
	}
	Instance = Instance ? Instance : OwnInstance();
	if (Instance)
	{
		Instance->PendingCharacterId.Invalidate();
	}

	// THE NEW-CHARACTER SCREEN, in this screen's place. Select-screen step 4a.
	bNewCharacterRequested = true;
	if (ACataclysmFrontEndPlayerController* FrontEnd = Cast<ACataclysmFrontEndPlayerController>(GetOwningPlayer());
		FrontEnd && !FrontEnd->ShowTheNewCharacterScreen())
	{
		// THE ASSET MISSING, as between steps 4a and 4b: this screen stays, and says why.
		bNewCharacterRequested = false;
		ShowRefusal(NewCharacterScreenMissing);
		return false;
	}
	return true;
}

void UCataclysmCharacterSelectWidget::HandleRowClicked(FName Value)
{
	ClickRow(FCString::Atoi(*Value.ToString()));
}

void UCataclysmCharacterSelectWidget::ClickRow(int32 Index)
{
	if (bDeleteMode) // A delete's: in delete mode a row asks, and never continues.
	{
		AskToDeleteAt(Index);
		return;
	}
	ContinueAt(Index);
}

void UCataclysmCharacterSelectWidget::HandleDeleteClicked()
{
	SetDeleteMode(!bDeleteMode);
}

void UCataclysmCharacterSelectWidget::HandleConfirmDeleteClicked()
{
	ConfirmDelete();
}

void UCataclysmCharacterSelectWidget::HandleCancelClicked()
{
	CancelDelete();
}

void UCataclysmCharacterSelectWidget::HandleConfirmNameChanged(const FText& Text)
{
	SetConfirmationTyped(Text.ToString());
}

void UCataclysmCharacterSelectWidget::HandleNewClicked()
{
	StartNew();
}

UCataclysmGameInstance* UCataclysmCharacterSelectWidget::OwnInstance() const
{
	return Cast<UCataclysmGameInstance>(GetGameInstance());
}

void UCataclysmCharacterSelectWidget::ShowRefusal(const FString& Why)
{
	Refusal = Why;
	if (RefusalLabel)
	{
		RefusalLabel->SetText(FText::FromString(Why));
	}
}

void UCataclysmCharacterSelectWidget::Open(const FString& Map)
{
	Requested = Map;
	// NOT IN AN AUTOMATION TEST, which cannot travel; it reads `LevelRequested`.
	if (!GIsAutomationTesting)
	{
		UGameplayStatics::OpenLevel(this, FName(*Map));
	}
}
