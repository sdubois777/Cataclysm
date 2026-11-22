// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmForgeWidget.h"

#include "Character/CataclysmPlayerCharacter.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Interface/CataclysmChoiceButton.h"
#include "Items/CataclysmEquipmentComponent.h"
// A gem's name in the screen's message, issue #46 slice G4.
#include "Items/CataclysmGems.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Player/CataclysmForgeCraft.h"
#include "Player/CataclysmPlayerController.h"

void UCataclysmForgeWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (CloseButton)
	{
		CloseButton->OnClicked.AddUniqueDynamic(this, &UCataclysmForgeWidget::HandleCloseClicked);
	}
	if (TitleLabel)
	{
		TitleLabel->SetText(FText::FromString(UCataclysmForgeLayout::Title()));
	}
	RefreshDisplay();
}

const UObject* UCataclysmForgeWidget::Context() const
{
	return WorldContextForTests ? WorldContextForTests.Get() : static_cast<const UObject*>(this);
}

void UCataclysmForgeWidget::RefreshDisplay()
{
	const ACataclysmPlayerCharacter* Character = UCataclysmForgeCraft::CharacterIn(Context());
	const UCataclysmEquipmentComponent* Equipment = Character ? Character->GetEquipment() : nullptr;
	const UCataclysmInventoryComponent* Inventory = Character ? Character->GetInventory() : nullptr;

	ShownEntries = UCataclysmForgeLayout::EntriesFor(Equipment, Inventory);
	ShownCombineLevels = UCataclysmForgeLayout::CombineLevelsFor(Inventory);
	Stones = UCataclysmForgeLayout::StonesLine(Inventory);

	// THE PIECE CHOSEN STAYS CHOSEN while it is listed, so a raised piece shows its next step. A piece that is gone --
	// taken off, put down -- leaves nothing chosen.
	if (bHasChosen && !ShownEntries.ContainsByPredicate(
						  [this](const FCataclysmForgeEntry& Entry) { return Entry.Target == ChosenTarget; }))
	{
		bHasChosen = false;
		CancelThePendingUpgrade();
	}

	const FCataclysmItem* Item = bHasChosen && Character ? UCataclysmForgeCraft::ItemAt(*Character, ChosenTarget) : nullptr;
	if (Item != nullptr)
	{
		const FCataclysmForgeUpgrade Plan = UCataclysmForgeCraft::PlanFor(Context(), ChosenTarget);
		Detail = UCataclysmForgeLayout::DetailFor(*Item, Plan);
		UpgradeWords = UCataclysmForgeLayout::UpgradeButtonText(Plan);
		// AND ITS SOCKETS, with what can be put in or taken out. Issue #46, slice G4.
		const FString Sockets = UCataclysmForgeLayout::SocketsDetailFor(*Item);
		if (!Sockets.IsEmpty())
		{
			Detail += TEXT("\n") + Sockets;
		}
		ShownSocketActions = UCataclysmForgeLayout::SocketActionsFor(*Item, Inventory);
	}
	else
	{
		Detail = Character ? TEXT("Choose a piece to raise.") : TEXT("No character is playing.");
		UpgradeWords = TEXT("Upgrade");
		ShownSocketActions.Reset();
	}

	BuildPieceButtons();
	BuildActionButtons();
	WriteLines();
}

void UCataclysmForgeWidget::BuildPieceButtons()
{
	if (PieceBox == nullptr)
	{
		return;
	}
	PieceBox->ClearChildren();
	TSubclassOf<UCataclysmChoiceButton> ButtonClass = ChoiceButtonClass.LoadSynchronous();
	if (!ButtonClass)
	{
		return;
	}
	for (const FCataclysmForgeEntry& Entry : ShownEntries)
	{
		if (UCataclysmChoiceButton* Button = CreateWidget<UCataclysmChoiceButton>(this, ButtonClass))
		{
			Button->SetChoice(Entry.ButtonName, FText::FromString(Entry.Label),
							  /*bChosen*/ bHasChosen && Entry.Target == ChosenTarget, /*bAvailable*/ true);
			Button->OnChoiceClicked.AddDynamic(this, &UCataclysmForgeWidget::HandlePieceClicked);
			PieceBox->AddChild(Button);
		}
	}
}

void UCataclysmForgeWidget::BuildActionButtons()
{
	if (ActionBox == nullptr)
	{
		return;
	}
	ActionBox->ClearChildren();
	TSubclassOf<UCataclysmChoiceButton> ButtonClass = ChoiceButtonClass.LoadSynchronous();
	if (!ButtonClass)
	{
		return;
	}

	if (UCataclysmChoiceButton* Upgrade = CreateWidget<UCataclysmChoiceButton>(this, ButtonClass))
	{
		Upgrade->SetChoice(UCataclysmForgeLayout::UpgradeButtonName(), FText::FromString(UpgradeWords),
						   /*bChosen*/ bUpgradeAsked, /*bAvailable*/ bHasChosen);
		Upgrade->OnChoiceClicked.AddDynamic(this, &UCataclysmForgeWidget::HandleActionClicked);
		ActionBox->AddChild(Upgrade);
	}

	for (const FCataclysmForgeSocketAction& Action : ShownSocketActions)
	{
		if (UCataclysmChoiceButton* Socketing = CreateWidget<UCataclysmChoiceButton>(this, ButtonClass))
		{
			Socketing->SetChoice(Action.ButtonName, FText::FromString(Action.Label),
								 /*bChosen*/ Action.ButtonName == PendingSocketAction, /*bAvailable*/ true);
			Socketing->OnChoiceClicked.AddDynamic(this, &UCataclysmForgeWidget::HandleActionClicked);
			ActionBox->AddChild(Socketing);
		}
	}

	for (const int32 Level : ShownCombineLevels)
	{
		if (UCataclysmChoiceButton* Combine = CreateWidget<UCataclysmChoiceButton>(this, ButtonClass))
		{
			Combine->SetChoice(UCataclysmForgeLayout::CombineButtonNameFor(Level),
							   FText::FromString(UCataclysmForgeLayout::CombineButtonText(Level)),
							   /*bChosen*/ false, /*bAvailable*/ true);
			Combine->OnChoiceClicked.AddDynamic(this, &UCataclysmForgeWidget::HandleActionClicked);
			ActionBox->AddChild(Combine);
		}
	}
}

void UCataclysmForgeWidget::WriteLines()
{
	if (StonesLabel)
	{
		StonesLabel->SetText(FText::FromString(Stones));
	}
	if (DetailLabel)
	{
		DetailLabel->SetText(FText::FromString(Detail));
	}
	if (MessageLabel)
	{
		MessageLabel->SetText(FText::FromString(Message));
	}
}

void UCataclysmForgeWidget::CancelThePendingUpgrade()
{
	bUpgradeAsked = false;
	bThresholdAsked = false;
	PendingSocketAction = NAME_None;
}

void UCataclysmForgeWidget::HandlePieceClicked(FName ButtonName)
{
	// ANY OTHER PRESS CANCELS THE UPGRADE ASKED FOR (ruling h).
	CancelThePendingUpgrade();
	Message.Reset();

	FCataclysmForgeTarget Target;
	if (UCataclysmForgeLayout::TargetForButton(ButtonName, Target))
	{
		ChosenTarget = Target;
		bHasChosen = true;
	}
	RefreshDisplay();
}

void UCataclysmForgeWidget::HandleActionClicked(FName ButtonName)
{
	const int32 CombineLevel = UCataclysmForgeLayout::CombineLevelForButton(ButtonName);
	if (CombineLevel != INDEX_NONE)
	{
		CancelThePendingUpgrade();
		FCataclysmForgeCombine Done;
		FString Reason;
		Message = UCataclysmForgeCraft::Combine(Context(), CombineLevel, Done, Reason)
			? FString::Printf(TEXT("Combined two +%d stones into one +%d."), Done.FromLevel, Done.IntoLevel)
			: Reason;
		RefreshDisplay();
		return;
	}

	// A SOCKET ACTION, ASKED ON ITS FIRST PRESS AND DONE ON ITS SECOND, as an upgrade is. Issue #46, slice G4.
	FCataclysmForgeSocketAction Socketing;
	if (bHasChosen && UCataclysmForgeLayout::SocketActionForButton(ButtonName, ShownSocketActions, Socketing))
	{
		const bool bSecondPress = PendingSocketAction == ButtonName;
		const bool bThresholdWasAsked = bSecondPress && bThresholdAsked;
		CancelThePendingUpgrade();

		FString Refusal = UCataclysmForgeCraft::RefusalNow(Context());
		const FCataclysmForgeSocketing Plan = Socketing.bUnsocket
			? UCataclysmForgeCraft::UnsocketPlanFor(Context(), ChosenTarget, Socketing.Socket)
			: UCataclysmForgeCraft::SocketPlanFor(Context(), ChosenTarget, Socketing.Socket, Socketing.Gem,
												  Socketing.Rarity);
		if (Refusal.IsEmpty() && !Plan.bPossible)
		{
			Refusal = Plan.Refusal;
		}
		if (!Refusal.IsEmpty())
		{
			Message = Refusal;
			RefreshDisplay();
			return;
		}
		if (!bSecondPress)
		{
			PendingSocketAction = ButtonName;
			Message = UCataclysmForge::SocketingQuestion(Plan);
			RefreshDisplay();
			return;
		}

		FCataclysmForgeSocketing Done;
		FString Said;
		const ECataclysmForgeResult Result = Socketing.bUnsocket
			? UCataclysmForgeCraft::UnsocketGem(Context(), ChosenTarget, Socketing.Socket, bThresholdWasAsked, Done, Said)
			: UCataclysmForgeCraft::SocketGem(Context(), ChosenTarget, Socketing.Socket, Socketing.Gem, Socketing.Rarity,
											  bThresholdWasAsked, Done, Said);
		switch (Result)
		{
		case ECataclysmForgeResult::AsksAboutTheThreshold:
			PendingSocketAction = ButtonName;
			bThresholdAsked = true;
			Message = Said + TEXT(" Press it again to go on.");
			break;
		case ECataclysmForgeResult::Done:
			Message = FString::Printf(TEXT("%s %s. It cost %d day%s of the run."),
									  Done.bUnsocket ? TEXT("Unsocketed") : TEXT("Socketed"),
									  *UCataclysmGems::NameOf(Done.Gem), Done.Days, Done.Days == 1 ? TEXT("") : TEXT("s"));
			break;
		case ECataclysmForgeResult::Refused:
		default:
			Message = Said;
			break;
		}
		RefreshDisplay();
		return;
	}

	if (ButtonName != UCataclysmForgeLayout::UpgradeButtonName())
	{
		return;
	}

	if (!bHasChosen)
	{
		Message = TEXT("Choose a piece to raise first.");
		RefreshDisplay();
		return;
	}

	// A PRESS THE RULES WOULD REFUSE SAYS SO ON THE FIRST PRESS AND ASKS FOR NO SECOND, the respec's way.
	FString Refusal = UCataclysmForgeCraft::RefusalNow(Context());
	const FCataclysmForgeUpgrade Plan = UCataclysmForgeCraft::PlanFor(Context(), ChosenTarget);
	if (Refusal.IsEmpty() && !Plan.bPossible)
	{
		Refusal = Plan.Refusal;
	}
	if (!Refusal.IsEmpty())
	{
		CancelThePendingUpgrade();
		Message = Refusal;
		RefreshDisplay();
		return;
	}

	// THE FIRST PRESS SAYS WHAT IT COSTS.
	if (!bUpgradeAsked)
	{
		bUpgradeAsked = true;
		Message = UCataclysmForge::UpgradeQuestion(Plan);
		RefreshDisplay();
		return;
	}

	FCataclysmForgeUpgrade Done;
	FString Said;
	const ECataclysmForgeResult Result =
		UCataclysmForgeCraft::Upgrade(Context(), ChosenTarget, bThresholdAsked, Done, Said);
	switch (Result)
	{
	case ECataclysmForgeResult::AsksAboutTheThreshold:
		// THE THRESHOLD QUESTION, AND THE NEXT PRESS CROSSES (ruling e).
		bThresholdAsked = true;
		Message = Said + TEXT(" Press Upgrade again to go on.");
		break;
	case ECataclysmForgeResult::Done:
		CancelThePendingUpgrade();
		Message = FString::Printf(TEXT("Raised to +%d. It cost %d day%s of the run."), Done.ToLevel, Done.Days,
								  Done.Days == 1 ? TEXT("") : TEXT("s"));
		break;
	case ECataclysmForgeResult::Refused:
	default:
		CancelThePendingUpgrade();
		Message = Said;
		break;
	}
	RefreshDisplay();
}

void UCataclysmForgeWidget::HandleCloseClicked()
{
	CancelThePendingUpgrade();
	if (ACataclysmPlayerController* Controller = Cast<ACataclysmPlayerController>(GetOwningPlayer()))
	{
		Controller->ToggleForge();
	}
}
