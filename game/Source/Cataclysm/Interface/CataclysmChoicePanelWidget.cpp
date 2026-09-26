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
	Shown = Object;
	bLastActed = false;
	Refresh();
}

void UCataclysmChoicePanelWidget::Refresh()
{
	Keys.Reset();
	const ACataclysmFloorObject* Object = Shown.Get();

	if (TitleLabel)
	{
		TitleLabel->SetText(FText::FromString(Object ? Object->DisplayName : FString()));
	}
	if (PromptLabel)
	{
		PromptLabel->SetText(FText::FromString(Object ? Object->Prompt : FString()));
	}

	// THE KEYS ARE KEPT WHETHER OR NOT THERE ARE BUTTONS, so a test with no Blueprint reads what a player would see.
	if (Object)
	{
		for (const FCataclysmFloorObjectChoice& Choice : Object->Choices)
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
	if (Object)
	{
		for (const FCataclysmFloorObjectChoice& Choice : Object->Choices)
		{
			AddButton(Choice.Key, Choice.Label, Choice.bAvailable);
		}
	}
	AddButton(LeaveKey, TEXT("Leave"), /*bAvailable=*/true);
}

void UCataclysmChoicePanelWidget::HandleChoiceClicked(FName Value)
{
	bLastActed = false;
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
	if (IsInViewport())
	{
		RemoveFromParent();
	}
}
