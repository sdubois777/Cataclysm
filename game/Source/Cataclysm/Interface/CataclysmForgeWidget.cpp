// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmForgeWidget.h"

// The damage types designed for each weapon type, issue #47 slice F4.
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Interface/CataclysmChoiceButton.h"
// The enchantment pair an imprint or a drawback change drew, read as the tool tip reads it, issue #47 slice F3.
#include "Interface/CataclysmItemTooltip.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmEquipmentComponent.h"
// A gold amount as a player reads it, issue #2830.
#include "Items/CataclysmDroppedItem.h"
// The affix table, for the affixes a piece can take, issue #47 slice F2.
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
		// AND AN IMPRINT FOR EACH AFFIX, AND A DRAWBACK TO CHANGE FOR EACH ENCHANTMENT. Issue #47, slice F3.
		ShownEnchantmentActions = UCataclysmForgeLayout::EnchantmentActionsFor(
			*Item, UCataclysmDropRoll::LoadPositiveEnchantmentTable(), UCataclysmDropRoll::LoadNegativeEnchantmentTable());
		// AND A SHIFT FOR EACH AFFIX, AND THE CHOOSER FOR ADDING ONE, the choice kept within what the piece can take.
		// Issue #47, slice F2.
		const UDataTable* AffixTable = UCataclysmDropRoll::LoadAffixTable();
		AddableAffixes = UCataclysmForge::AddableAffixesFor(*Item, AffixTable, UCataclysmItemModifiers::LoadBaseTable());
		AddAffixChosen = AddableAffixes.IsEmpty() ? 0 : FMath::Clamp(AddAffixChosen, 0, AddableAffixes.Num() - 1);
		AddAffixTier = FMath::Clamp(AddAffixTier, 1, UCataclysmItemValues::MaxAffixTier);
		ShownAffixChangeActions = UCataclysmForgeLayout::ShiftActionsFor(*Item);
		ShownAffixChangeActions.Append(
			UCataclysmForgeLayout::AddAffixActionsFor(*Item, AddableAffixes, AddAffixChosen, AddAffixTier, AffixTable));
		const FString Adding =
			UCataclysmForgeLayout::AddAffixDetailFor(*Item, AddableAffixes, AddAffixChosen, AddAffixTier, AffixTable);
		if (!Adding.IsEmpty())
		{
			Detail += TEXT("\n") + Adding;
		}
		// AND A WEAPON'S DAMAGE TYPES, with an infusion and a change for each it can take. Issue #47, slice F4.
		const UDataTable* SkillTable = UCataclysmWeaponSkills::LoadGeneratedTable();
		const UDataTable* BaseTable = UCataclysmItemModifiers::LoadBaseTable();
		const int32 MaxTypes = UCataclysmForge::MaxDamageTypesOf(*Item, SkillTable, BaseTable);
		const FString Types = UCataclysmForgeLayout::DamageTypesDetailFor(*Item, MaxTypes);
		if (!Types.IsEmpty())
		{
			Detail += TEXT("\n") + Types;
		}
		ShownDamageTypeActions = UCataclysmForgeLayout::DamageTypeActionsFor(
			*Item, UCataclysmForge::DamageTypesOpenTo(*Item, SkillTable, BaseTable), MaxTypes);
		// AND THE RESIDUE TOOLS: the two options the next craft uses, and the cleanse. Issue #47, slice F5.
		const int32 Stabilizers = Inventory ? Inventory->CountOfMaterial(FName(UCataclysmForge::ChaosStabilizerRow)) : 0;
		ShardsHeldNow = Inventory ? Inventory->CountOfMaterial(FName(UCataclysmForge::TaintedShardRow)) : 0;
		const int32 Essences = Inventory ? Inventory->CountOfMaterial(FName(UCataclysmForge::PurifiedEssenceRow)) : 0;
		// NEVER MORE THAN IS HELD: a craft that used the options' materials leaves the next one asking for none it lacks.
		Options.TaintedShards = FMath::Clamp(Options.TaintedShards, 0, ShardsHeldNow);
		Options.bChaosStabilizer = Options.bChaosStabilizer && Stabilizers > 0;
		ShownOptionActions = UCataclysmForgeLayout::OptionActionsFor(*Item, Options, Stabilizers, ShardsHeldNow, Essences);
		// AND, FOR A CARRIED PIECE, THE DISMANTLE. Issue #2848, slice S2.
		ShownDismantleActions.Reset();
		const FString Dismantle = UCataclysmForgeLayout::DismantleLabelFor(*Item, ChosenTarget.bWorn);
		if (!Dismantle.IsEmpty())
		{
			ShownDismantleActions.Add({UCataclysmForgeLayout::DismantleButtonName(), Dismantle});
		}
	}
	else
	{
		Detail = Character ? TEXT("Choose a piece to raise.") : TEXT("No character is playing.");
		UpgradeWords = TEXT("Upgrade");
		ShownSocketActions.Reset();
		ShownSocketWorkActions.Reset();
		ShownAffixActions.Reset();
		ShownEnchantmentActions.Reset();
		ShownAffixChangeActions.Reset();
		AddableAffixes.Reset();
		ShownDamageTypeActions.Reset();
		ShownOptionActions.Reset();
		ShownDismantleActions.Reset();
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

	// THE DISMANTLE, two presses. Issue #2848, slice S2.
	for (const FCataclysmForgeOptionAction& Action : ShownDismantleActions)
	{
		if (UCataclysmChoiceButton* Button = CreateWidget<UCataclysmChoiceButton>(this, ButtonClass))
		{
			Button->SetChoice(Action.ButtonName, FText::FromString(Action.Label),
							  /*bChosen*/ Action.ButtonName == PendingSocketAction, /*bAvailable*/ true);
			Button->OnChoiceClicked.AddDynamic(this, &UCataclysmForgeWidget::HandleActionClicked);
			ActionBox->AddChild(Button);
		}
	}

	// THE RESIDUE TOOLS: the options, one press each, and the cleanse, two. Issue #47, slice F5.
	for (const FCataclysmForgeOptionAction& Action : ShownOptionActions)
	{
		if (UCataclysmChoiceButton* Option = CreateWidget<UCataclysmChoiceButton>(this, ButtonClass))
		{
			const bool bOn = (Action.ButtonName == UCataclysmForgeLayout::StabilizerButtonName() && Options.bChaosStabilizer)
				|| (Action.ButtonName == UCataclysmForgeLayout::ShardsButtonName() && Options.TaintedShards > 0)
				|| Action.ButtonName == PendingSocketAction;
			Option->SetChoice(Action.ButtonName, FText::FromString(Action.Label), /*bChosen*/ bOn, /*bAvailable*/ true);
			Option->OnChoiceClicked.AddDynamic(this, &UCataclysmForgeWidget::HandleActionClicked);
			ActionBox->AddChild(Option);
		}
	}

	// THE DAMAGE-TYPE CRAFTS, two presses each. Issue #47, slice F4.
	for (const FCataclysmForgeDamageTypeAction& Action : ShownDamageTypeActions)
	{
		if (UCataclysmChoiceButton* DamageType = CreateWidget<UCataclysmChoiceButton>(this, ButtonClass))
		{
			DamageType->SetChoice(Action.ButtonName, FText::FromString(Action.Label),
								  /*bChosen*/ Action.ButtonName == PendingSocketAction, /*bAvailable*/ true);
			DamageType->OnChoiceClicked.AddDynamic(this, &UCataclysmForgeWidget::HandleActionClicked);
			ActionBox->AddChild(DamageType);
		}
	}

	// THE SHIFTS AND THE ADD-AN-AFFIX CHOOSER. Issue #47, slice F2.
	for (const FCataclysmForgeAffixChangeAction& Action : ShownAffixChangeActions)
	{
		if (UCataclysmChoiceButton* AffixChange = CreateWidget<UCataclysmChoiceButton>(this, ButtonClass))
		{
			AffixChange->SetChoice(Action.ButtonName, FText::FromString(Action.Label),
								   /*bChosen*/ Action.ButtonName == PendingSocketAction, /*bAvailable*/ true);
			AffixChange->OnChoiceClicked.AddDynamic(this, &UCataclysmForgeWidget::HandleActionClicked);
			ActionBox->AddChild(AffixChange);
		}
	}

	for (const FCataclysmForgeEnchantmentAction& Action : ShownEnchantmentActions)
	{
		if (UCataclysmChoiceButton* EnchantmentWork = CreateWidget<UCataclysmChoiceButton>(this, ButtonClass))
		{
			EnchantmentWork->SetChoice(Action.ButtonName, FText::FromString(Action.Label),
									   /*bChosen*/ Action.ButtonName == PendingSocketAction, /*bAvailable*/ true);
			EnchantmentWork->OnChoiceClicked.AddDynamic(this, &UCataclysmForgeWidget::HandleActionClicked);
			ActionBox->AddChild(EnchantmentWork);
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
		// ANOTHER PIECE STARTS THE ADD-AN-AFFIX CHOOSER AGAIN, at its first affix and tier 1. Issue #47, slice F2.
		if (!(bHasChosen && Target == ChosenTarget))
		{
			AddAffixChosen = 0;
			AddAffixTier = 1;
			// AND THE OPTIONS, which belong to the piece they were asked for (slice F5).
			Options = FCataclysmForgeCraftOptions();
		}
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
		// THE OPTIONS' EFFECT, FROM THE FUNCTION THE CRAFT WILL USE, and a refused option refuses the press (slice F5).
		const FCataclysmForgeOptionEffect Effect = UCataclysmForgeCraft::OptionsFor(
			Context(), Plan.ResidueBefore, Plan.ResidueAfter, Plan.Days, Plan.Gold, Options);
		if (Refusal.IsEmpty() && !Effect.bPossible)
		{
			Refusal = Effect.Refusal;
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
			Message = UCataclysmForge::SocketingQuestion(Plan)
				+ UCataclysmForge::OptionsQuestion(Effect, Plan.ResidueBefore, Plan.ResidueAfter, Plan.Days);
			RefreshDisplay();
			return;
		}

		FCataclysmForgeSocketing Done;
		FString Said;
		const ECataclysmForgeResult Result = Socketing.bUnsocket
			? UCataclysmForgeCraft::UnsocketGem(Context(), ChosenTarget, Socketing.Socket, bThresholdWasAsked, Done, Said, Options)
			: UCataclysmForgeCraft::SocketGem(Context(), ChosenTarget, Socketing.Socket, Socketing.Gem, Socketing.Rarity,
											  bThresholdWasAsked, Done, Said, Options);
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
		// THE OPTIONS' EFFECT, FROM THE FUNCTION THE CRAFT WILL USE, and a refused option refuses the press (slice F5).
		const FCataclysmForgeOptionEffect Effect = UCataclysmForgeCraft::OptionsFor(
			Context(), Plan.ResidueBefore, Plan.ResidueAfter, Plan.Days, Plan.Gold, Options);
		if (Refusal.IsEmpty() && !Effect.bPossible)
		{
			Refusal = Effect.Refusal;
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
			Message = UCataclysmForge::SocketWorkQuestion(Plan)
				+ UCataclysmForge::OptionsQuestion(Effect, Plan.ResidueBefore, Plan.ResidueAfter, Plan.Days);
			RefreshDisplay();
			return;
		}

		FCataclysmForgeSocketWork Done;
		FString Said;
		const ECataclysmForgeResult Result = SocketWork.bAdd
			? UCataclysmForgeCraft::AddSocket(Context(), ChosenTarget, bThresholdWasAsked, Done, Said, Options)
			: UCataclysmForgeCraft::ChangeSocketType(Context(), ChosenTarget, SocketWork.Socket, SocketWork.NewType,
													 bThresholdWasAsked, Done, Said, Options);
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
		// THE OPTIONS' EFFECT, FROM THE FUNCTION THE CRAFT WILL USE, and a refused option refuses the press (slice F5).
		const FCataclysmForgeOptionEffect Effect = UCataclysmForgeCraft::OptionsFor(
			Context(), Plan.ResidueBefore, Plan.ResidueAfter, Plan.Days, Plan.Gold, Options);
		if (Refusal.IsEmpty() && !Effect.bPossible)
		{
			Refusal = Effect.Refusal;
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
			Message = UCataclysmForge::AffixWorkQuestion(Plan)
				+ UCataclysmForge::OptionsQuestion(Effect, Plan.ResidueBefore, Plan.ResidueAfter, Plan.Days);
			RefreshDisplay();
			return;
		}

		FCataclysmForgeAffixWork Done;
		FString Said;
		const ECataclysmForgeResult Result = UCataclysmForgeCraft::CraftAffix(
			Context(), ChosenTarget, AffixWork.Craft, AffixWork.Affix, bThresholdWasAsked, Done, Said, Options);
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

	// THE ADD-AN-AFFIX CHOOSER'S STEPS ARE ONE PRESS EACH and change nothing but what the chooser shows; an affix shifted
	// or added is asked on its first press and done on its second, as the other affix crafts are. Issue #47, slice F2.
	FCataclysmForgeAffixChangeAction AffixChange;
	if (bHasChosen && UCataclysmForgeLayout::AffixChangeActionForButton(ButtonName, ShownAffixChangeActions, AffixChange))
	{
		const int32 Count = AddableAffixes.Num();
		if (ButtonName == UCataclysmForgeLayout::AddAffixPreviousButtonName()
			|| ButtonName == UCataclysmForgeLayout::AddAffixNextButtonName()
			|| ButtonName == UCataclysmForgeLayout::AddAffixTierDownButtonName()
			|| ButtonName == UCataclysmForgeLayout::AddAffixTierUpButtonName())
		{
			CancelThePendingUpgrade();
			Message.Reset();
			if (Count > 0 && ButtonName == UCataclysmForgeLayout::AddAffixPreviousButtonName())
			{
				AddAffixChosen = (AddAffixChosen + Count - 1) % Count;
			}
			else if (Count > 0 && ButtonName == UCataclysmForgeLayout::AddAffixNextButtonName())
			{
				AddAffixChosen = (AddAffixChosen + 1) % Count;
			}
			else if (ButtonName == UCataclysmForgeLayout::AddAffixTierDownButtonName())
			{
				AddAffixTier = FMath::Max(1, AddAffixTier - 1);
			}
			else if (ButtonName == UCataclysmForgeLayout::AddAffixTierUpButtonName())
			{
				AddAffixTier = FMath::Min(UCataclysmItemValues::MaxAffixTier, AddAffixTier + 1);
			}
			RefreshDisplay();
			return;
		}

		const bool bSecondPress = PendingSocketAction == ButtonName;
		const bool bThresholdWasAsked = bSecondPress && bThresholdAsked;
		CancelThePendingUpgrade();
		const FName Adding = !AffixChange.bShift && AddableAffixes.IsValidIndex(AddAffixChosen)
			? AddableAffixes[AddAffixChosen]
			: NAME_None;

		FString Refusal = UCataclysmForgeCraft::RefusalNow(Context());
		const FCataclysmForgeAffixChange Plan = AffixChange.bShift
			? UCataclysmForgeCraft::ShiftAffixPlanFor(Context(), ChosenTarget, AffixChange.Affix)
			: UCataclysmForgeCraft::AddAffixPlanFor(Context(), ChosenTarget, Adding, AddAffixTier);
		if (Refusal.IsEmpty() && !Plan.bPossible)
		{
			Refusal = Plan.Refusal;
		}
		// THE OPTIONS' EFFECT, FROM THE FUNCTION THE CRAFT WILL USE, and a refused option refuses the press (slice F5).
		const FCataclysmForgeOptionEffect Effect = UCataclysmForgeCraft::OptionsFor(
			Context(), Plan.ResidueBefore, Plan.ResidueAfter, Plan.Days, Plan.Gold, Options);
		if (Refusal.IsEmpty() && !Effect.bPossible)
		{
			Refusal = Effect.Refusal;
		}
		if (!Refusal.IsEmpty())
		{
			Message = Refusal;
			RefreshDisplay();
			return;
		}
		const UDataTable* AffixTable = UCataclysmDropRoll::LoadAffixTable();
		if (!bSecondPress)
		{
			PendingSocketAction = ButtonName;
			Message = UCataclysmForge::AffixChangeQuestion(Plan, AffixTable)
				+ UCataclysmForge::OptionsQuestion(Effect, Plan.ResidueBefore, Plan.ResidueAfter, Plan.Days);
			RefreshDisplay();
			return;
		}

		FCataclysmForgeAffixChange Done;
		FString Said;
		const ECataclysmForgeResult Result = AffixChange.bShift
			? UCataclysmForgeCraft::ShiftAffix(Context(), ChosenTarget, AffixChange.Affix, bThresholdWasAsked, Done, Said, Options)
			: UCataclysmForgeCraft::AddAffix(Context(), ChosenTarget, Adding, AddAffixTier, bThresholdWasAsked, Done, Said, Options);
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
			Message = Done.bShift
				? FString::Printf(TEXT("Affix %d is %s now, tier %d kept. %s"), Done.Affix + 1,
								  *UCataclysmForge::AffixNameOf(Done.ToRow, AffixTable), Done.Tier, *Cost)
				: FString::Printf(TEXT("Added %s at tier %d as affix %d; the piece is %s now. %s"),
								  *UCataclysmForge::AffixNameOf(Done.ToRow, AffixTable), Done.Tier, Done.Affix + 1,
								  *UCataclysmGems::RarityName(Done.ToRarity), *Cost);
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

	// THE OPTIONS ARE ONE PRESS EACH AND CHANGE NOTHING BUT WHAT THE NEXT CRAFT USES: the Stabilizer on and off, and the
	// Shards one more each press, back to none after the number held. Issue #47, slice F5.
	if (bHasChosen && (ButtonName == UCataclysmForgeLayout::StabilizerButtonName()
					   || ButtonName == UCataclysmForgeLayout::ShardsButtonName()))
	{
		CancelThePendingUpgrade();
		Message.Reset();
		if (ButtonName == UCataclysmForgeLayout::StabilizerButtonName())
		{
			Options.bChaosStabilizer = !Options.bChaosStabilizer;
		}
		else
		{
			Options.TaintedShards = (FMath::Max(0, Options.TaintedShards) + 1) % (FMath::Max(0, ShardsHeldNow) + 1);
		}
		RefreshDisplay();
		return;
	}

	// THE DISMANTLE, ASKED ON ITS FIRST PRESS AND DONE ON ITS SECOND: the piece is destroyed. Issue #2848, slice S2.
	if (bHasChosen && ButtonName == UCataclysmForgeLayout::DismantleButtonName())
	{
		const bool bSecondPress = PendingSocketAction == ButtonName;
		CancelThePendingUpgrade();
		FString Refusal = UCataclysmForgeCraft::RefusalNow(Context());
		const FCataclysmForgeDismantle Plan = UCataclysmForgeCraft::DismantlePlanFor(Context(), ChosenTarget);
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
			const ACataclysmPlayerCharacter* Character = UCataclysmForgeCraft::CharacterIn(Context());
			const FCataclysmItem* Item = Character ? UCataclysmForgeCraft::ItemAt(*Character, ChosenTarget) : nullptr;
			PendingSocketAction = ButtonName;
			Message = UCataclysmForge::DismantleQuestion(
				Plan, Item != nullptr ? UCataclysmForge::NameOf(*Item, UCataclysmItemModifiers::LoadBaseTable(),
																 UCataclysmDropRoll::LoadAffixTable())
									  : FString(TEXT("this piece")));
			RefreshDisplay();
			return;
		}
		FCataclysmForgeDismantle Done;
		FString Said;
		const bool bDone = UCataclysmForgeCraft::Dismantle(Context(), ChosenTarget, Done, Said) == ECataclysmForgeResult::Done;
		Message = bDone ? FString::Printf(TEXT("Dismantled. It gave %s."), *UCataclysmForge::DismantleGiven(Done)) : Said;
		if (bDone)
		{
			// THE PIECE IS GONE, so nothing stays chosen.
			bHasChosen = false;
		}
		RefreshDisplay();
		return;
	}

	// THE CLEANSE, ASKED ON ITS FIRST PRESS AND DONE ON ITS SECOND. No option applies to it. Issue #47, slice F5.
	if (bHasChosen && ButtonName == UCataclysmForgeLayout::CleanseButtonName())
	{
		const bool bSecondPress = PendingSocketAction == ButtonName;
		CancelThePendingUpgrade();
		FString Refusal = UCataclysmForgeCraft::RefusalNow(Context());
		const FCataclysmForgeCleanse Plan = UCataclysmForgeCraft::CleansePlanFor(Context(), ChosenTarget);
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
			Message = UCataclysmForge::CleanseQuestion(Plan);
			RefreshDisplay();
			return;
		}
		FCataclysmForgeCleanse Done;
		FString Said;
		Message = UCataclysmForgeCraft::Cleanse(Context(), ChosenTarget, Done, Said) == ECataclysmForgeResult::Done
			? FString::Printf(TEXT("The residue is %d now, from %d. It cost %s and %d day%s of the run."),
							  FMath::RoundToInt(Done.ResidueAfter), FMath::RoundToInt(Done.ResidueBefore),
							  *ACataclysmDroppedItem::GoldName(Done.Gold), Done.Days, Done.Days == 1 ? TEXT("") : TEXT("s"))
			: Said;
		RefreshDisplay();
		return;
	}

	// A DAMAGE TYPE INFUSED OR CHANGED, ASKED ON ITS FIRST PRESS AND DONE ON ITS SECOND, as the other crafts are. Issue
	// #47, slice F4.
	FCataclysmForgeDamageTypeAction DamageTypeWork;
	if (bHasChosen && UCataclysmForgeLayout::DamageTypeActionForButton(ButtonName, ShownDamageTypeActions, DamageTypeWork))
	{
		const bool bSecondPress = PendingSocketAction == ButtonName;
		const bool bThresholdWasAsked = bSecondPress && bThresholdAsked;
		CancelThePendingUpgrade();

		FString Refusal = UCataclysmForgeCraft::RefusalNow(Context());
		const FCataclysmForgeDamageTypeWork Plan =
			UCataclysmForgeCraft::DamageTypeWorkPlanFor(Context(), ChosenTarget, DamageTypeWork.Craft, DamageTypeWork.Type);
		if (Refusal.IsEmpty() && !Plan.bPossible)
		{
			Refusal = Plan.Refusal;
		}
		// THE OPTIONS' EFFECT, FROM THE FUNCTION THE CRAFT WILL USE, and a refused option refuses the press (slice F5).
		const FCataclysmForgeOptionEffect Effect = UCataclysmForgeCraft::OptionsFor(
			Context(), Plan.ResidueBefore, Plan.ResidueAfter, Plan.Days, Plan.Gold, Options);
		if (Refusal.IsEmpty() && !Effect.bPossible)
		{
			Refusal = Effect.Refusal;
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
			Message = UCataclysmForge::DamageTypeQuestion(Plan)
				+ UCataclysmForge::OptionsQuestion(Effect, Plan.ResidueBefore, Plan.ResidueAfter, Plan.Days);
			RefreshDisplay();
			return;
		}

		FCataclysmForgeDamageTypeWork Done;
		FString Said;
		const ECataclysmForgeResult Result = UCataclysmForgeCraft::CraftDamageType(
			Context(), ChosenTarget, DamageTypeWork.Craft, DamageTypeWork.Type, bThresholdWasAsked, Done, Said, Options);
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
			Message = Done.Craft == ECataclysmForgeDamageTypeCraft::Change
				? FString::Printf(TEXT("%s is %s now. %s"), *Done.FromType.ToString(), *Done.ToType.ToString(), *Cost)
				: FString::Printf(TEXT("The weapon carries %s now. %s"), *Done.ToType.ToString(), *Cost);
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

	// AN ENCHANTMENT IMPRINTED OR A DRAWBACK CHANGED, ASKED ON ITS FIRST PRESS AND DONE ON ITS SECOND, as a socket's
	// work is. Issue #47, slice F3.
	FCataclysmForgeEnchantmentAction EnchantmentWork;
	if (bHasChosen
		&& UCataclysmForgeLayout::EnchantmentActionForButton(ButtonName, ShownEnchantmentActions, EnchantmentWork))
	{
		const bool bSecondPress = PendingSocketAction == ButtonName;
		const bool bThresholdWasAsked = bSecondPress && bThresholdAsked;
		CancelThePendingUpgrade();

		FString Refusal = UCataclysmForgeCraft::RefusalNow(Context());
		const FCataclysmForgeEnchantmentWork Plan = EnchantmentWork.bImprint
			? UCataclysmForgeCraft::ImprintPlanFor(Context(), ChosenTarget, EnchantmentWork.Index)
			: UCataclysmForgeCraft::ChangeNegativePlanFor(Context(), ChosenTarget, EnchantmentWork.Index);
		if (Refusal.IsEmpty() && !Plan.bPossible)
		{
			Refusal = Plan.Refusal;
		}
		// THE OPTIONS' EFFECT, FROM THE FUNCTION THE CRAFT WILL USE, and a refused option refuses the press (slice F5).
		const FCataclysmForgeOptionEffect Effect = UCataclysmForgeCraft::OptionsFor(
			Context(), Plan.ResidueBefore, Plan.ResidueAfter, Plan.Days, Plan.Gold, Options);
		if (Refusal.IsEmpty() && !Effect.bPossible)
		{
			Refusal = Effect.Refusal;
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
			Message = UCataclysmForge::EnchantmentWorkQuestion(Plan, EnchantmentWork.Named)
				+ UCataclysmForge::OptionsQuestion(Effect, Plan.ResidueBefore, Plan.ResidueAfter, Plan.Days);
			RefreshDisplay();
			return;
		}

		FCataclysmForgeEnchantmentWork Done;
		FString Said;
		const ECataclysmForgeResult Result = EnchantmentWork.bImprint
			? UCataclysmForgeCraft::Imprint(Context(), ChosenTarget, EnchantmentWork.Index, bThresholdWasAsked, Done, Said, Options)
			: UCataclysmForgeCraft::ChangeNegative(Context(), ChosenTarget, EnchantmentWork.Index, bThresholdWasAsked,
												   Done, Said, Options);
		switch (Result)
		{
		case ECataclysmForgeResult::AsksAboutTheThreshold:
			PendingSocketAction = ButtonName;
			bThresholdAsked = true;
			Message = Said + TEXT(" Press it again to go on.");
			break;
		case ECataclysmForgeResult::Done:
		{
			// THE PAIR DRAWN, AS THE TOOL TIP READS IT.
			const TArray<FString> Lines = UCataclysmItemTooltip::EnchantmentLines(
				Done.Drawn, UCataclysmDropRoll::LoadPositiveEnchantmentTable(),
				UCataclysmDropRoll::LoadNegativeEnchantmentTable());
			const FString Positive = Lines.IsValidIndex(0) ? Lines[0] : Done.Drawn.Positive.ToString();
			const FString Drawback = Lines.IsValidIndex(1) ? Lines[1] : Done.Drawn.Negative.ToString();
			const FString Cost = FString::Printf(TEXT("It cost %s and %d day%s of the run."),
												 *ACataclysmDroppedItem::GoldName(Done.Gold), Done.Days,
												 Done.Days == 1 ? TEXT("") : TEXT("s"));
			Message = Done.bImprint
				? FString::Printf(TEXT("Imprinted %s (%s). The piece is now %s. %s"), *Positive, *Drawback,
								  *UCataclysmGems::RarityName(Done.RarityAfter), *Cost)
				: FString::Printf(TEXT("%s now carries %s. %s"), *Positive, *Drawback, *Cost);
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
	// THE OPTIONS' EFFECT, FROM THE FUNCTION THE CRAFT WILL USE, and a refused option refuses the press (slice F5).
	const FCataclysmForgeOptionEffect Effect = UCataclysmForgeCraft::OptionsFor(
		Context(), Plan.ResidueBefore, Plan.ResidueAfter, Plan.Days, Plan.Gold, Options);
	if (Refusal.IsEmpty() && !Effect.bPossible)
	{
		Refusal = Effect.Refusal;
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
		Message = UCataclysmForge::UpgradeQuestion(Plan)
				+ UCataclysmForge::OptionsQuestion(Effect, Plan.ResidueBefore, Plan.ResidueAfter, Plan.Days);
		RefreshDisplay();
		return;
	}

	FCataclysmForgeUpgrade Done;
	FString Said;
	const ECataclysmForgeResult Result =
		UCataclysmForgeCraft::Upgrade(Context(), ChosenTarget, bThresholdAsked, Done, Said, Options);
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
