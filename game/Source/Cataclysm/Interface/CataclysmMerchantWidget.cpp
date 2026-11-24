// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmMerchantWidget.h"

#include "Character/CataclysmPlayerCharacter.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Interface/CataclysmChoiceButton.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmForge.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmMerchant.h"
#include "Player/CataclysmForgeCraft.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerController.h"

const TCHAR* UCataclysmMerchantWidget::TitleText = TEXT("Merchant");
const TCHAR* UCataclysmMerchantWidget::SellText = TEXT("Sell");
const TCHAR* UCataclysmMerchantWidget::ChooseFirstText = TEXT("Choose a carried piece to sell first.");

namespace
{
	FString MerchantScreenPieceName(const FCataclysmItem& Item)
	{
		return UCataclysmForge::NameOf(Item, UCataclysmItemModifiers::LoadBaseTable(), UCataclysmDropRoll::LoadAffixTable());
	}
}

void UCataclysmMerchantWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (TitleLabel)
	{
		TitleLabel->SetText(FText::FromString(TitleText));
	}
	if (CloseButton && !bCloseBound)
	{
		CloseButton->OnClicked.AddDynamic(this, &UCataclysmMerchantWidget::HandleCloseClicked);
		bCloseBound = true;
	}
	Refresh();
}

const UObject* UCataclysmMerchantWidget::Context() const
{
	if (WorldContextForTests)
	{
		return WorldContextForTests;
	}
	return GetOwningPlayer() != nullptr ? static_cast<const UObject*>(GetOwningPlayer()) : this;
}

bool UCataclysmMerchantWidget::Press(const FString& Command)
{
	const UObject* World = Context();
	const ACataclysmPlayerCharacter* Character = UCataclysmForgeCraft::CharacterIn(World);
	const UCataclysmInventoryComponent* Bag = Character != nullptr ? Character->GetInventory() : nullptr;
	bool bDone = false;

	FString Kind;
	FString Value;
	if (Command.Split(TEXT(":"), &Kind, &Value) && Kind == TEXT("Cell"))
	{
		// CHOOSING A PIECE CANCELS A QUESTION ASKED ABOUT ANOTHER.
		const int32 Cell = Value.IsNumeric() ? FCString::Atoi(*Value) : INDEX_NONE;
		bDone = UCataclysmMerchant::CarriedPiecesOf(Character).Contains(Cell);
		if (bDone)
		{
			Chosen = Cell;
			bAsked = false;
			Message.Reset();
		}
	}
	else if (Command == TEXT("Sell"))
	{
		const FCataclysmItem* Item = Bag != nullptr ? Bag->ItemAt(Chosen) : nullptr;
		const FString Refusal = UCataclysmMerchant::RefusalNow(World);
		if (!Refusal.IsEmpty())
		{
			Message = Refusal;
			bAsked = false;
		}
		else if (Item == nullptr)
		{
			Message = ChooseFirstText;
			bAsked = false;
		}
		else if (!bAsked)
		{
			// THE FIRST PRESS ASKS.
			bAsked = true;
			bDone = true;
			Message = FString::Printf(TEXT("Sell %s for %s? Press Sell again to sell it."), *MerchantScreenPieceName(*Item),
									  *ACataclysmDroppedItem::GoldName(
										  UCataclysmMerchant::SellValueAt(*Item, UCataclysmMerchant::TierIn(World))));
		}
		else
		{
			// THE SECOND SELLS.
			int64 Gold = 0;
			bDone = UCataclysmMerchant::Sell(World, Chosen, Gold, Message);
			bAsked = false;
			if (bDone)
			{
				Chosen = INDEX_NONE;
			}
		}
	}
	else if (Command == TEXT("Close"))
	{
		bAsked = false;
		if (ACataclysmPlayerController* Controller = Cast<ACataclysmPlayerController>(GetOwningPlayer()))
		{
			Controller->ToggleMerchant();
		}
		bDone = true;
	}
	Refresh();
	return bDone;
}

void UCataclysmMerchantWidget::Refresh()
{
	const UObject* World = Context();
	const ACataclysmPlayerCharacter* Character = UCataclysmForgeCraft::CharacterIn(World);
	const UCataclysmInventoryComponent* Bag = Character != nullptr ? Character->GetInventory() : nullptr;
	const TArray<int32> Cells = UCataclysmMerchant::CarriedPiecesOf(Character);
	const int32 Tier = UCataclysmMerchant::TierIn(World);
	if (!Cells.Contains(Chosen))
	{
		Chosen = INDEX_NONE;
		bAsked = false;
	}

	int64 Held = 0;
	GoldLine = UCataclysmGameInstance::GoldHeldBy(World, Held)
		? FString::Printf(TEXT("Gold held: %s"), *ACataclysmDroppedItem::GoldName(Held))
		: FString();
	const FCataclysmItem* ChosenItem = Bag != nullptr && Chosen != INDEX_NONE ? Bag->ItemAt(Chosen) : nullptr;
	Detail = ChosenItem != nullptr
		? FString::Printf(TEXT("%s, +%d: sells for %s"), *MerchantScreenPieceName(*ChosenItem), ChosenItem->GearLevel,
						  *ACataclysmDroppedItem::GoldName(UCataclysmMerchant::SellValueAt(*ChosenItem, Tier)))
		: (Cells.Num() > 0 ? FString(ChooseFirstText) : FString(TEXT("You carry no gear to sell.")));
	if (GoldLabel)
	{
		GoldLabel->SetText(FText::FromString(GoldLine));
	}
	if (DetailLabel)
	{
		DetailLabel->SetText(FText::FromString(Detail));
	}
	if (MessageLabel)
	{
		MessageLabel->SetText(FText::FromString(Message));
	}

	// EVERY BUTTON MADE AGAIN ON A PRESS, as the forge's screen does.
	Shown.Reset();
	Buttons.Reset();
	if (PieceBox)
	{
		PieceBox->ClearChildren();
	}
	if (ActionBox)
	{
		ActionBox->ClearChildren();
	}
	const TSubclassOf<UCataclysmChoiceButton> ButtonClass =
		(PieceBox || ActionBox) ? ChoiceButtonClass.LoadSynchronous() : nullptr;
	for (const int32 Cell : Cells)
	{
		const FCataclysmItem* Item = Bag->ItemAt(Cell);
		AddButton(PieceBox, ButtonClass, FString::Printf(TEXT("Cell:%d"), Cell),
				  FString::Printf(TEXT("%s, +%d: %s"), *MerchantScreenPieceName(*Item), Item->GearLevel,
								  *ACataclysmDroppedItem::GoldName(UCataclysmMerchant::SellValueAt(*Item, Tier))),
				  Cell == Chosen);
	}
	AddButton(ActionBox, ButtonClass, TEXT("Sell"), bAsked ? FString(TEXT("Sell: press again to confirm")) : FString(SellText),
			  bAsked);
}

void UCataclysmMerchantWidget::AddButton(UPanelWidget* Panel, TSubclassOf<UCataclysmChoiceButton> ButtonClass,
										 const FString& Command, const FString& Label, bool bChosen)
{
	Shown.Add(TPair<FString, FString>(Command, Label));
	UCataclysmChoiceButton* Button =
		Panel != nullptr && ButtonClass != nullptr ? CreateWidget<UCataclysmChoiceButton>(this, ButtonClass) : nullptr;
	if (Button == nullptr)
	{
		return;
	}
	Button->OnChoiceClicked.AddDynamic(this, &UCataclysmMerchantWidget::HandleClicked);
	Button->SetChoice(FName(*Command), FText::FromString(Label), bChosen, /*bInAvailable=*/true);
	Panel->AddChild(Button);
	Buttons.Add(Button);
}

void UCataclysmMerchantWidget::HandleClicked(FName Value)
{
	Press(Value.ToString());
}

void UCataclysmMerchantWidget::HandleCloseClicked()
{
	Press(TEXT("Close"));
}
