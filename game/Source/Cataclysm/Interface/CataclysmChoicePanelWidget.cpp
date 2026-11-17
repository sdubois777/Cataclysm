// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmChoicePanelWidget.h"

#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmFloorObject.h"
#include "EngineUtils.h"
#include "Interface/CataclysmChoiceButton.h"

const FName UCataclysmChoicePanelWidget::LeaveKey(TEXT("Leave"));

void UCataclysmChoicePanelWidget::SetObject(ACataclysmFloorObject* Object)
{
	EndQuestion();
	Shown = Object;
	bLastActed = false;
	Refresh();
}

void UCataclysmChoicePanelWidget::SetQuestion(const FString& Title, const FString& Prompt,
											  const TArray<FCataclysmFloorObjectChoice>& Choices,
											  const FString& LeaveLabel, TFunction<void(FName)> OnChosen)
{
	Shown = nullptr;
	bLastActed = false;
	bAsking = true;
	TitleText = Title;
	PromptText = Prompt;
	QuestionChoices = Choices;
	QuestionLeaveLabel = LeaveLabel;
	QuestionAnswered = MoveTemp(OnChosen);
	Refresh();
}

void UCataclysmChoicePanelWidget::EndQuestion()
{
	bAsking = false;
	QuestionChoices.Reset();
	QuestionLeaveLabel.Reset();
	QuestionAnswered = nullptr;
}

void UCataclysmChoicePanelWidget::Refresh()
{
	Keys.Reset();
	const ACataclysmFloorObject* Object = Shown.Get();

	// A QUESTION'S WORDS, OR THE OBJECT'S. SetQuestion wrote the question's; an object's are read here.
	if (!bAsking)
	{
		TitleText = Object ? Object->DisplayName : FString();
		PromptText = Object ? Object->Prompt : FString();
	}
	const TArray<FCataclysmFloorObjectChoice>* Choices =
		bAsking ? &QuestionChoices : (Object ? &Object->Choices : nullptr);
	const FString LeaveLabel = bAsking ? QuestionLeaveLabel : FString(TEXT("Leave"));

	if (TitleLabel)
	{
		TitleLabel->SetText(FText::FromString(TitleText));
	}
	if (PromptLabel)
	{
		PromptLabel->SetText(FText::FromString(PromptText));
	}

	// THE KEYS ARE KEPT WHETHER OR NOT THERE ARE BUTTONS, so a test with no Blueprint reads what a player would see.
	if (Choices)
	{
		for (const FCataclysmFloorObjectChoice& Choice : *Choices)
		{
			Keys.Add(Choice.Key);
		}
	}
	Keys.Add(LeaveKey);

	if (!ChoiceBox)
	{
		return;
	}
	ChoiceBox->ClearChildren();
	TSubclassOf<UCataclysmChoiceButton> ButtonClass = ChoiceButtonClass.LoadSynchronous();
	if (!ButtonClass)
	{
		return;
	}
	const auto AddButton = [this, &ButtonClass](FName Key, const FString& Label, bool bAvailable)
	{
		if (UCataclysmChoiceButton* Button = CreateWidget<UCataclysmChoiceButton>(this, ButtonClass))
		{
			Button->SetChoice(Key, FText::FromString(Label), /*bChosen=*/false, bAvailable);
			Button->OnChoiceClicked.AddDynamic(this, &UCataclysmChoicePanelWidget::HandleChoiceClicked);
			ChoiceBox->AddChild(Button);
		}
	};
	if (Choices)
	{
		for (const FCataclysmFloorObjectChoice& Choice : *Choices)
		{
			AddButton(Choice.Key, Choice.Label, Choice.bAvailable);
		}
	}
	AddButton(LeaveKey, LeaveLabel, /*bAvailable=*/true);
}

void UCataclysmChoicePanelWidget::HandleChoiceClicked(FName Value)
{
	bLastActed = false;
	if (bAsking)
	{
		// TAKEN BEFORE THE PANEL CLOSES, because closing ends the question; and called after it closes, so the answer
		// may open another question on this same panel.
		TFunction<void(FName)> Answered = MoveTemp(QuestionAnswered);
		Close();
		if (Value != LeaveKey && Answered)
		{
			bLastActed = true;
			Answered(Value);
		}
		return;
	}
	ACataclysmFloorObject* Object = Shown.Get();
	if (Value != LeaveKey && Object)
	{
		// THE OBJECT'S OWN WORLD, so the panel finds the game mode whoever created it. The world's game mode in
		// play; in an automation test the dungeon game mode is spawned as an actor and is not the world's, so the
		// first one standing in the world is asked instead.
		UWorld* World = Object->GetWorld();
		ACataclysmDungeonGameMode* Mode = World ? World->GetAuthGameMode<ACataclysmDungeonGameMode>() : nullptr;
		if (!Mode && World)
		{
			for (TActorIterator<ACataclysmDungeonGameMode> It(World); It; ++It)
			{
				Mode = *It;
				break;
			}
		}
		if (Mode)
		{
			bLastActed = Mode->ChooseAtFloorObject(Object, Value);
		}
	}
	Close();
}

void UCataclysmChoicePanelWidget::ChooseForTests(FName ChoiceKey)
{
	// THE HANDLER THE BUTTON CALLS, not a copy of it, for the reason the city screen's test hook gives.
	HandleChoiceClicked(ChoiceKey);
}

void UCataclysmChoicePanelWidget::Close()
{
	EndQuestion();
	if (IsInViewport())
	{
		RemoveFromParent();
	}
}
