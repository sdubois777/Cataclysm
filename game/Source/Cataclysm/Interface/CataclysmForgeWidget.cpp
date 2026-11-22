// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmForgeWidget.h"

#include "Character/CataclysmPlayerCharacter.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Interface/CataclysmChoiceButton.h"
#include "Items/CataclysmEquipmentComponent.h"
// A gold amount as a player reads it, issue #2830.
#include "Items/CataclysmDroppedItem.h"
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
	ShownGemCombines = UCataclysmForgeLayout::GemCombineActionsFor(Inventory);
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
		// AND A SOCKET TO ADD, AND EACH EMPTY SOCKET'S TYPE TO CHANGE. Issue #46, slice G9.
		ShownSocketWorkActions =
			UCataclysmForgeLayout::SocketWorkActionsFor(*Item, UCataclysmForgeCraft::MaxSocketsOf(*Item));
		// AND ITS AFFIXES, with a reroll, a perfecting and a raise for each. Issue #47, slice F1.
		const FString Affixes = UCataclysmForgeLayout::AffixesDetailFor(*Item);
		if (!Affixes.IsEmpty())
		{
			Detail += TEXT("\n") + Affixes;
		}
		ShownAffixActions = UCataclysmForgeLayout::AffixActionsFor(*Item);
	}
	else
	{
		Detail = Character ? TEXT("Choose a piece to raise.") : TEXT("No character is playing.");
		UpgradeWords = TEXT("Upgrade");
		ShownSocketActions.Reset();
		ShownSocketWorkActions.Reset();
		ShownAffixActions.Reset();
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

	for (const FCataclysmForgeSocketWorkAction& Action : ShownSocketWorkActions)
	{
		if (UCataclysmChoiceButton* SocketWork = CreateWidget<UCataclysmChoiceButton>(this, ButtonClass))
		{
			SocketWork->SetChoice(Action.ButtonName, FText::FromString(Action.Label),
								  /*bChosen*/ Action.ButtonName == PendingSocketAction, /*bAvailable*/ true);
			SocketWork->OnChoiceClicked.AddDynamic(this, &UCataclysmForgeWidget::HandleActionClicked);
			ActionBox->AddChild(SocketWork);
		}
	}

	// THE AFFIX CRAFTS, two presses each as socket work is. Issue #47, slice F1.
	for (const FCataclysmForgeAffixAction& Action : ShownAffixActions)
	{
		if (UCataclysmChoiceButton* AffixWork = CreateWidget<UCataclysmChoiceButton>(this, ButtonClass))
		{
			AffixWork->SetChoice(Action.ButtonName, FText::FromString(Action.Label),
								 /*bChosen*/ Action.ButtonName == PendingSocketAction, /*bAvailable*/ true);
			AffixWork->OnChoiceClicked.AddDynamic(this, &UCataclysmForgeWidget::HandleActionClicked);
			ActionBox->AddChild(AffixWork);
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

	// A GEM COMBINE FOR EACH CARRIED GEM STACK below Cataclysmic. Issue #46, slice G6.
	for (const FCataclysmForgeGemCombineAction& Action : ShownGemCombines)
	{
		if (UCataclysmChoiceButton* Combine = CreateWidget<UCataclysmChoiceButton>(this, ButtonClass))
		{
			Combine->SetChoice(Action.ButtonName, FText::FromString(Action.Label), /*bChosen*/ false,
							   /*bAvailable*/ true);
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
			? FString::Printf(TEXT("Combined two +%d stones into one +%d for %s."), Done.FromLevel, Done.IntoLevel,
							  *ACataclysmDroppedItem::GoldName(Done.Gold))
			: Reason;
		RefreshDisplay();
		return;
	}

	// A GEM COMBINE IS ONE PRESS, as a stone combine is: it changes no item and costs no day. Issue #46, slice G6.
	FCataclysmForgeGemCombineAction GemCombine;
	if (UCataclysmForgeLayout::GemCombineActionForButton(ButtonName, ShownGemCombines, GemCombine))
	{
		CancelThePendingUpgrade();
		FCataclysmForgeGemCombine Done;
		FString Reason;
		Message = UCataclysmForgeCraft::CombineGems(Context(), GemCombine.Gem, GemCombine.Rarity, Done, Reason)
			? FString::Printf(TEXT("Combined two %s (%s) into one %s (%s) for %s."), *UCataclysmGems::NameOf(Done.Gem),
							  *UCataclysmGems::RarityName(Done.From), *UCataclysmGems::NameOf(Done.Gem),
							  *UCataclysmGems::RarityName(Done.Into), *ACataclysmDroppedItem::GoldName(Done.Gold))
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
			Message = FString::Printf(TEXT("%s %s. It cost %s and %d day%s of the run."),
									  Done.bUnsocket ? TEXT("Unsocketed") : TEXT("Socketed"),
									  *UCataclysmGems::NameOf(Done.Gem), *ACataclysmDroppedItem::GoldName(Done.Gold),
									  Done.Days, Done.Days == 1 ? TEXT("") : TEXT("s"));
			break;
		case ECataclysmForgeResult::Refused:
		default:
			Message = Said;
			break;
		}
		RefreshDisplay();
		return;
	}

	// A SOCKET ADDED OR A SOCKET'S TYPE CHANGED, ASKED ON ITS FIRST PRESS AND DONE ON ITS SECOND, as socketing is.
	// Issue #46, slice G9.
	FCataclysmForgeSocketWorkAction SocketWork;
	if (bHasChosen && UCataclysmForgeLayout::SocketWorkActionForButton(ButtonName, ShownSocketWorkActions, SocketWork))
	{
		const bool bSecondPress = PendingSocketAction == ButtonName;
		const bool bThresholdWasAsked = bSecondPress && bThresholdAsked;
		CancelThePendingUpgrade();

		FString Refusal = UCataclysmForgeCraft::RefusalNow(Context());
		const FCataclysmForgeSocketWork Plan = SocketWork.bAdd
			? UCataclysmForgeCraft::AddSocketPlanFor(Context(), ChosenTarget)
			: UCataclysmForgeCraft::ChangeSocketTypePlanFor(Context(), ChosenTarget, SocketWork.Socket,
															SocketWork.NewType);
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
			Message = UCataclysmForge::SocketWorkQuestion(Plan);
			RefreshDisplay();
			return;
		}

		FCataclysmForgeSocketWork Done;
		FString Said;
		const ECataclysmForgeResult Result = SocketWork.bAdd
			? UCataclysmForgeCraft::AddSocket(Context(), ChosenTarget, bThresholdWasAsked, Done, Said)
			: UCataclysmForgeCraft::ChangeSocketType(Context(), ChosenTarget, SocketWork.Socket, SocketWork.NewType,
													 bThresholdWasAsked, Done, Said);
		switch (Result)
		{
		case ECataclysmForgeResult::AsksAboutTheThreshold:
			PendingSocketAction = ButtonName;
			bThresholdAsked = true;
			Message = Said + TEXT(" Press it again to go on.");
			break;
		case ECataclysmForgeResult::Done:
			Message = Done.bAdd
				? FString::Printf(TEXT("Added socket %d, a %s socket. It cost %s and %d day%s of the run."),
								  Done.Socket + 1, *UCataclysmGems::SocketTypeName(Done.ToType),
								  *ACataclysmDroppedItem::GoldName(Done.Gold), Done.Days, Done.Days == 1 ? TEXT("") : TEXT("s"))
				: FString::Printf(TEXT("Socket %d is now a %s socket. It cost %s and %d day%s of the run."),
								  Done.Socket + 1, *UCataclysmGems::SocketTypeName(Done.ToType),
								  *ACataclysmDroppedItem::GoldName(Done.Gold), Done.Days, Done.Days == 1 ? TEXT("") : TEXT("s"));
			break;
		case ECataclysmForgeResult::Refused:
		default:
			Message = Said;
			break;
		}
		RefreshDisplay();
		return;
	}

	// AN AFFIX REROLLED, PERFECTED OR RAISED A TIER, ASKED ON ITS FIRST PRESS AND DONE ON ITS SECOND, as socket work is.
	// Issue #47, slice F1.
	FCataclysmForgeAffixAction AffixWork;
	if (bHasChosen && UCataclysmForgeLayout::AffixActionForButton(ButtonName, ShownAffixActions, AffixWork))
	{
		const bool bSecondPress = PendingSocketAction == ButtonName;
		const bool bThresholdWasAsked = bSecondPress && bThresholdAsked;
		CancelThePendingUpgrade();

		FString Refusal = UCataclysmForgeCraft::RefusalNow(Context());
		const FCataclysmForgeAffixWork Plan =
			UCataclysmForgeCraft::AffixWorkPlanFor(Context(), ChosenTarget, AffixWork.Craft, AffixWork.Affix);
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
			Message = UCataclysmForge::AffixWorkQuestion(Plan);
			RefreshDisplay();
			return;
		}

		FCataclysmForgeAffixWork Done;
		FString Said;
		const ECataclysmForgeResult Result = UCataclysmForgeCraft::CraftAffix(
			Context(), ChosenTarget, AffixWork.Craft, AffixWork.Affix, bThresholdWasAsked, Done, Said);
		switch (Result)
		{
		case ECataclysmForgeResult::AsksAboutTheThreshold:
			PendingSocketAction = ButtonName;
			bThresholdAsked = true;
			Message = Said + TEXT(" Press it again to go on.");
			break;
		case ECataclysmForgeResult::Done:
		{
			const FString Cost = FString::Printf(TEXT("It cost %s and %d day%s of the run."),
												 *ACataclysmDroppedItem::GoldName(Done.Gold), Done.Days,
												 Done.Days == 1 ? TEXT("") : TEXT("s"));
			Message = Done.Craft == ECataclysmForgeAffixCraft::UpgradeTier
				? FString::Printf(TEXT("Affix %d is tier %d now, its roll kept at %d percent. %s"), Done.Affix + 1,
								  Done.ToTier, UCataclysmForge::RollPercent(Done.ToRoll), *Cost)
				: FString::Printf(TEXT("Affix %d's roll is %d percent of tier %d's range now, from %d. %s"), Done.Affix + 1,
								  UCataclysmForge::RollPercent(Done.ToRoll), Done.ToTier,
								  UCataclysmForge::RollPercent(Done.FromRoll), *Cost);
			break;
		}
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
		Message = FString::Printf(TEXT("Raised to +%d. It cost %s and %d day%s of the run."), Done.ToLevel,
								  *ACataclysmDroppedItem::GoldName(Done.Gold), Done.Days,
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
