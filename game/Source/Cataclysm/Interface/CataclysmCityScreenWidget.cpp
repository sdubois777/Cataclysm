// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmCityScreenWidget.h"

#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Data/CataclysmCityUpgradeMapping.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Interface/CataclysmChoiceButton.h"
#include "Interface/CataclysmWalkFromTheScreen.h"
#include "Player/CataclysmDungeonBribe.h"
#include "Player/CataclysmEmpireTree.h"
#include "Player/CataclysmCapitalGameMode.h"
#include "Player/CataclysmCityUpgradePurchase.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerController.h"

void UCataclysmCityScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();

	Refresh();
}

UCataclysmEmpireRun* UCataclysmCityScreenWidget::ShownRun() const
{
	// THE GAME'S OWN RUN FIRST, ALWAYS. The test seam is only reached when there
	// is no game instance of this project's class at all, which is the case a
	// headless test is in and nothing in a running game ever is.
	if (UCataclysmEmpireRun* Run =
			UCataclysmGameInstance::EmpireRunFor(this, /*bStartIfNone*/ false))
	{
		return Run;
	}

	return RunForTests;
}

void UCataclysmCityScreenWidget::SetRunForTests(UCataclysmEmpireRun* Run)
{
	RunForTests = Run;
}

void UCataclysmCityScreenWidget::SetDungeonModeForTests(ACataclysmDungeonGameMode* Mode)
{
	DungeonModeForTests = Mode;
}

ACataclysmDungeonGameMode* UCataclysmCityScreenWidget::DungeonMode() const
{
	// THE LEVEL'S OWN GAME MODE FIRST, ALWAYS, for the reason `ShownRun` asks
	// the game instance first: the test seam is reached only when the level has
	// none, which a headless widget's never has and a dungeon level's always
	// has. Found by walking the level, as `InWorld` says, rather than by asking
	// for the authority game mode.
	if (ACataclysmDungeonGameMode* Mode = ACataclysmDungeonGameMode::InWorld(GetWorld()))
	{
		return Mode;
	}

	return DungeonModeForTests.Get();
}

void UCataclysmCityScreenWidget::SetCity(int32 InCityId)
{
	CityId = InCityId;

	// A NEW CITY CLEARS THE LAST REFUSAL. "Every upgrade slot on this city is
	// filled" left over from the previous city would be about a city the player
	// is no longer looking at.
	Refusal.Reset();

	Refresh();
}

void UCataclysmCityScreenWidget::Refresh()
{
	WriteStatus();
	BuildOfferButtons();
	WriteDetailRows();
}

// ---------------------------------------------------------------------------
// The three labels
// ---------------------------------------------------------------------------

void UCataclysmCityScreenWidget::WriteStatus()
{
	const UCataclysmEmpireRun* Run = ShownRun();

	if (TitleLabel)
	{
		TitleLabel->SetText(FText::FromString(
			UCataclysmCityScreenLayout::TitleTextFor(Run, CityId)));
	}

	if (StatusLabel)
	{
		StatusLabel->SetText(FText::FromString(
			UCataclysmCityScreenLayout::StatusTextFor(Run, CityId)));
	}

	// AND THE GOLD THAT PAYS FOR AN UPGRADE, after the slots, since issue
	// #2742's second slice.
	if (SlotsLabel)
	{
		SlotsLabel->SetText(FText::FromString(
			UCataclysmCityScreenLayout::SlotsAndGoldTextFor(this, Run, CityId)));
	}

	if (RefusalLabel)
	{
		RefusalLabel->SetText(FText::FromString(Refusal));
	}
}

// ---------------------------------------------------------------------------
// The upgrades this city can buy
// ---------------------------------------------------------------------------

void UCataclysmCityScreenWidget::BuildOfferButtons()
{
	if (OfferBox == nullptr)
	{
		return;
	}

	// EVERY BUTTON IS REMADE RATHER THAN UPDATED. Buying one upgrade changes
	// which of the others are still available -- a slot is gone, and a one-time
	// upgrade may have changed the city -- so the list is shorter or differently
	// worded every time. Keeping buttons and hiding some would leave the panel
	// holding widgets nothing reads.
	OfferBox->ClearChildren();
	OfferButtons.Reset();
	OfferRowNames.Reset();
	RushButtons.Reset();
	RaiseButtons.Reset();

	const UCataclysmEmpireRun* Run = ShownRun();
	if (Run == nullptr)
	{
		return;
	}

	TSubclassOf<UCataclysmChoiceButton> ButtonClass =
		ChoiceButtonClass.LoadSynchronous();
	if (!ButtonClass)
	{
		return;
	}

	for (const FCataclysmCityUpgradeOffer& Offer :
		 UCataclysmCityScreenLayout::OffersFor(
			 this, Run, CityId, UCataclysmCityUpgradePurchase::GoldHeldIn(this)))
	{
		// A HELD UPGRADE GETS A RAISE BUTTON, since issue #1265: pressable, or greyed saying why -- the gold, a Siege,
		// another build, or "Already at tier 3." -- and, with Rapid Funding held, a rush of the raise under it.
		if (Offer.bRaiseOffered)
		{
			AddRaiseButtons(Offer, ButtonClass);
			continue;
		}

		// ONLY WHAT CAN BE BOUGHT GETS A BUTTON. What the city already has is a
		// row under "What this city has", and what does nothing yet is a row in
		// its own section; neither is a thing to press.
		//
		// AND WHAT ONLY GOLD OR A SIEGE STOPS, GREYED, since issue #2742's second
		// slice: its button says the price and how much more is needed, or that
		// a Siege dungeon stands on the city, and cannot be pressed. Left off, a
		// player short of gold or under a Siege would see the offers vanish with
		// nothing to say why. AND WHAT ANOTHER BUILD ON THE CITY STOPS, since issue
		// #2825: a city builds one upgrade at a time, and its button says so.
		if (!Offer.bCanBuy && !Offer.bShortOfGold && !Offer.bBesieged && !Offer.bWaitsOnABuild)
		{
			continue;
		}

		UCataclysmChoiceButton* Button =
			CreateWidget<UCataclysmChoiceButton>(this, ButtonClass);
		if (!Button)
		{
			continue;
		}

		Button->SetChoice(Offer.RowName,
						  FText::FromString(
							  UCataclysmCityScreenLayout::ButtonTextFor(Offer)),
						  /* bChosen */ false, /* bAvailable */ Offer.bCanBuy);

		Button->OnChoiceClicked.AddDynamic(
			this, &UCataclysmCityScreenWidget::HandleOfferClicked);

		OfferBox->AddChild(Button);

		OfferButtons.Add(Button);
		OfferRowNames.Add(Offer.RowName);

		// AND ITS RUSH, WITH RAPID FUNDING HELD (issue #2832), under it: greyed, saying how much more is needed, when
		// gold is all that stops it; left off when anything else stops the offer, which its own button says.
		if (Offer.bRushOffered && (Offer.bCanBuy || Offer.bShortOfGold))
		{
			if (UCataclysmChoiceButton* Rush = CreateWidget<UCataclysmChoiceButton>(this, ButtonClass))
			{
				Rush->SetChoice(Offer.RowName,
								FText::FromString(UCataclysmCityScreenLayout::RushButtonTextFor(Offer)),
								/* bChosen */ false, /* bAvailable */ Offer.bCanRush);
				Rush->OnChoiceClicked.AddDynamic(this, &UCataclysmCityScreenWidget::HandleRushClicked);
				OfferBox->AddChild(Rush);
				RushButtons.Add(Rush);
			}
		}
	}
}

void UCataclysmCityScreenWidget::AddRaiseButtons(const FCataclysmCityUpgradeOffer& Offer,
												 TSubclassOf<UCataclysmChoiceButton> ButtonClass)
{
	if (UCataclysmChoiceButton* Raise = CreateWidget<UCataclysmChoiceButton>(this, ButtonClass))
	{
		Raise->SetChoice(Offer.RowName, FText::FromString(UCataclysmCityScreenLayout::RaiseButtonTextFor(Offer)),
						 /* bChosen */ false, /* bAvailable */ Offer.bCanRaise);
		Raise->OnChoiceClicked.AddDynamic(this, &UCataclysmCityScreenWidget::HandleRaiseClicked);
		OfferBox->AddChild(Raise);
		RaiseButtons.Add(Raise);
	}
	if (Offer.bRushOffered && (Offer.bCanRaise || Offer.bRaiseShortOfGold))
	{
		if (UCataclysmChoiceButton* Rush = CreateWidget<UCataclysmChoiceButton>(this, ButtonClass))
		{
			Rush->SetChoice(Offer.RowName, FText::FromString(UCataclysmCityScreenLayout::RaiseRushButtonTextFor(Offer)),
							/* bChosen */ false, /* bAvailable */ Offer.bCanRaiseRush);
			Rush->OnChoiceClicked.AddDynamic(this, &UCataclysmCityScreenWidget::HandleRaiseRushClicked);
			OfferBox->AddChild(Rush);
			RaiseButtons.Add(Rush);
		}
	}
}

void UCataclysmCityScreenWidget::HandleRaiseClicked(FName Value)
{
	RaiseFromTheScreen(Value, /*bRush=*/false);
}

void UCataclysmCityScreenWidget::HandleRaiseRushClicked(FName Value)
{
	RaiseFromTheScreen(Value, /*bRush=*/true);
}

void UCataclysmCityScreenWidget::RaiseFromTheScreen(FName Value, bool bRush)
{
	UCataclysmEmpireRun* Run = ShownRun();
	if (Run == nullptr)
	{
		return;
	}
	// RAISED THROUGH THE ONE PLACE THAT CHARGES, and a refusal shown as a purchase's is (issue #1265).
	const int32 Tier = UCataclysmCityUpgradePurchase::RaiseTierIn(Run, CityId, Value);
	const int64 Price = bRush ? UCataclysmCityUpgradePurchase::RushPriceIn(this, Run, Value, CityId, Tier)
							  : UCataclysmCityUpgradePurchase::PriceIn(this, Run, Value, CityId, Tier);
	const int64 Held = UCataclysmCityUpgradePurchase::GoldHeldIn(this);
	const ECataclysmCityUpgradeResult Result = UCataclysmCityUpgradePurchase::Raise(this, Run, CityId, Value, bRush);
	Refusal = UCataclysmCityUpgradePurchase::RefusalTextIn(this, Result, Price, Held);
	Refresh();
}

void UCataclysmCityScreenWidget::ClickRaiseForTests(FName RowName)
{
	HandleRaiseClicked(RowName);
}

void UCataclysmCityScreenWidget::HandleRushClicked(FName Value)
{
	UCataclysmEmpireRun* Run = ShownRun();
	if (Run == nullptr)
	{
		return;
	}
	// RUSHED THROUGH THE ONE PLACE THAT CHARGES, as `HandleOfferClicked` buys, and a refusal shown the same way.
	const int64 Price = UCataclysmCityUpgradePurchase::RushPriceIn(this, Run, Value, CityId);
	const int64 Held = UCataclysmCityUpgradePurchase::GoldHeldIn(this);
	const ECataclysmCityUpgradeResult Result =
		UCataclysmCityUpgradePurchase::Buy(this, Run, CityId, Value, /*bRush=*/true);
	Refusal = UCataclysmCityUpgradePurchase::RefusalTextIn(this, Result, Price, Held);
	Refresh();
}

void UCataclysmCityScreenWidget::HandleOfferClicked(FName Value)
{
	UCataclysmEmpireRun* Run = ShownRun();
	if (Run == nullptr)
	{
		return;
	}

	// BOUGHT WITH GOLD, since issue #2742's second slice: the price at the
	// run's tier is taken from the character's balance and the saves raised,
	// all in `UCataclysmCityUpgradePurchase::Buy`, the one place that charges.
	const int64 Price = UCataclysmCityUpgradePurchase::PriceIn(this, Run, Value, CityId);
	const int64 Held = UCataclysmCityUpgradePurchase::GoldHeldIn(this);
	const ECataclysmCityUpgradeResult Result =
		UCataclysmCityUpgradePurchase::Buy(this, Run, CityId, Value);

	// A REFUSAL IS SHOWN RATHER THAN SWALLOWED. Only a buyable upgrade is given
	// a button that can be pressed, so a refusal here means the city changed
	// underneath the screen -- it fell, or another slot was spent -- and a
	// player who pressed a button and saw nothing happen would reasonably think
	// the screen was broken.
	Refusal = UCataclysmCityUpgradePurchase::RefusalTextIn(this, Result, Price, Held);

	Refresh();
}

// ---------------------------------------------------------------------------
// The detail list
// ---------------------------------------------------------------------------

void UCataclysmCityScreenWidget::WriteDetailRows()
{
	if (DetailBox == nullptr)
	{
		return;
	}

	DetailBox->ClearChildren();
	DetailRows.Reset();
	WalkButtons.Reset();
	BribeButtons.Reset(); // Bribery's, beside them. Issue #2741 slice 9.

	const UCataclysmEmpireRun* Run = ShownRun();
	if (Run == nullptr)
	{
		AddRow(TEXT("No run has been started. Cataclysm.EmpireBegin starts one."),
			   /* bIsHeading */ false, /* bDim */ true);
		return;
	}

	// WHAT IS STANDING ON IT, FIRST. It is the thing that decides whether this
	// city needs anything at all.
	const TArray<FString> Dungeons =
		UCataclysmCityScreenLayout::DungeonLinesFor(Run, CityId);

	// AND THE DUNGEON EACH LINE DESCRIBES, in the same order, for the Walk
	// button under it. Issue #2715.
	const TArray<int32> DungeonIds =
		UCataclysmCityScreenLayout::DungeonIdsFor(Run, CityId);

	AddRow(UCataclysmCityScreenLayout::DungeonHeading(Dungeons.Num()),
		   /* bIsHeading */ true, /* bDim */ false);

	for (int32 Index = 0; Index < Dungeons.Num(); ++Index)
	{
		AddRow(Dungeons[Index], /* bIsHeading */ false, /* bDim */ false);

		if (DungeonIds.IsValidIndex(Index))
		{
			AddWalkButton(DungeonIds[Index]);
			AddBribeButton(DungeonIds[Index]); // While Bribery is held (issue #2741 slice 9, V14).
		}
	}

	// WHAT IT HAS ALREADY BOUGHT.
	const TArray<FString> Held =
		UCataclysmCityScreenLayout::HeldLinesFor(Run, CityId);

	AddRow(UCataclysmCityScreenLayout::HeldHeading(), /* bIsHeading */ true,
		   /* bDim */ false);

	if (Held.IsEmpty())
	{
		AddRow(TEXT("Nothing yet."), /* bIsHeading */ false, /* bDim */ true);
	}

	for (const FString& Line : Held)
	{
		AddRow(Line, /* bIsHeading */ false, /* bDim */ false);
	}

	// AND WHAT IS DESIGNED BUT DOES NOTHING. Shown rather than hidden, because
	// hiding them would make the game look as though it has nineteen city
	// upgrades rather than twenty-four with five waiting on other systems.
	TArray<FCataclysmCityUpgradeOffer> NotBuilt;

	for (const FCataclysmCityUpgradeOffer& Offer :
		 UCataclysmCityScreenLayout::OffersFor(
			 this, Run, CityId, UCataclysmCityUpgradePurchase::GoldHeldIn(this)))
	{
		if (!Offer.bCanBuy && !Offer.bHeld
			&& !UCataclysmCityUpgradeRules::IsBuilt(Offer.EffectKind))
		{
			NotBuilt.Add(Offer);
		}
	}

	if (!NotBuilt.IsEmpty())
	{
		AddRow(UCataclysmCityScreenLayout::NotBuiltHeading(NotBuilt.Num()),
			   /* bIsHeading */ true, /* bDim */ true);

		for (const FCataclysmCityUpgradeOffer& Offer : NotBuilt)
		{
			AddRow(UCataclysmCityScreenLayout::NotBuiltLineFor(Offer),
				   /* bIsHeading */ false, /* bDim */ true);
		}
	}
}

void UCataclysmCityScreenWidget::AddRow(const FString& Text, bool bIsHeading,
										bool bDim)
{
	if (DetailBox == nullptr)
	{
		return;
	}

	UTextBlock* Row = NewObject<UTextBlock>(this);
	if (Row == nullptr)
	{
		return;
	}

	Row->SetText(FText::FromString(Text));

	// THE LOOK COMES FROM PROPERTIES THE BLUEPRINT SETS, for the reason the
	// character sheet's rows do: a text block made here rather than placed in
	// the designer carries the engine's own defaults, which are small and nearly
	// black, and this project's panel is nearly black too.
	FSlateFontInfo Font = Row->GetFont();
	Font.Size = bIsHeading ? HeadingFontSize : RowFontSize;
	Row->SetFont(Font);
	Row->SetColorAndOpacity(bDim ? DimColour : RowColour);

	DetailBox->AddChild(Row);
	DetailRows.Add(Row);
}

// ---------------------------------------------------------------------------
// Walking a dungeon standing on the city, issue #2715
// ---------------------------------------------------------------------------

void UCataclysmCityScreenWidget::AddWalkButton(int32 DungeonId)
{
	if (DetailBox == nullptr)
	{
		return;
	}

	TSubclassOf<UCataclysmChoiceButton> ButtonClass =
		ChoiceButtonClass.LoadSynchronous();
	if (!ButtonClass)
	{
		return;
	}

	UCataclysmChoiceButton* Button =
		CreateWidget<UCataclysmChoiceButton>(this, ButtonClass);
	if (!Button)
	{
		return;
	}

	// ALWAYS AVAILABLE, AND THE PRESS SAYS WHY WHEN IT IS REFUSED. A disabled
	// button cannot be pressed and says nothing, and the reasons a walk is
	// refused -- this is not a dungeon level, or the player is walking this one
	// now -- are too long for a button's label.
	Button->SetChoice(FCataclysmWalkFromTheScreen::ButtonNameFor(DungeonId),
					  FText::FromString(FCataclysmWalkFromTheScreen::ButtonText()),
					  /* bChosen */ false, /* bAvailable */ true);

	Button->OnChoiceClicked.AddDynamic(
		this, &UCataclysmCityScreenWidget::HandleWalkClicked);

	DetailBox->AddChild(Button);
	WalkButtons.Add(Button);
}

void UCataclysmCityScreenWidget::HandleWalkClicked(FName Value)
{
	const int32 DungeonId = FCataclysmWalkFromTheScreen::DungeonForButton(Value);

	// THE RUN THIS SCREEN SHOWS, AND THE LEVEL'S DUNGEON GAME MODE. In a running
	// game the game mode reads the same run from the game instance.
	Refusal = FCataclysmWalkFromTheScreen::Walk(ShownRun(), DungeonMode(), DungeonId,
		ACataclysmCapitalGameMode::InWorld(GetWorld())); // In the capital, the press travels: #2733 slice 3.

	// A REFUSAL IS SHOWN RATHER THAN SWALLOWED, on the line that shows a refused
	// upgrade, for the reason `HandleOfferClicked` gives. Redrawn either way: a
	// walk entered owes a day and stops that dungeon's timer.
	Refresh();

	if (!Refusal.IsEmpty())
	{
		return;
	}

	// AND THE EMPIRE SCREENS CLOSE, so the player is standing on the floor they
	// walked into. The player controller owns both screens; a headless test has
	// none, and there the screen stays.
	if (ACataclysmPlayerController* Controller =
			Cast<ACataclysmPlayerController>(GetOwningPlayer()))
	{
		Controller->CloseEmpireScreens();
	}
}

void UCataclysmCityScreenWidget::AddBribeButton(int32 DungeonId)
{
	// ONLY WHILE BRIBERY IS HELD, so a player without the node sees the screen as it was.
	if (DetailBox == nullptr || UCataclysmGameInstance::EmpireTreePlayerBonusesFor(this).DungeonBribePoints <= 0)
	{
		return;
	}
	TSubclassOf<UCataclysmChoiceButton> ButtonClass = ChoiceButtonClass.LoadSynchronous();
	UCataclysmChoiceButton* Button = ButtonClass ? CreateWidget<UCataclysmChoiceButton>(this, ButtonClass) : nullptr;
	if (Button == nullptr)
	{
		return;
	}

	// ALWAYS PRESSABLE, SHOWING THE FEE, AND THE PRESS SAYS WHY WHEN IT IS REFUSED, as the Walk button does (V13).
	Button->SetChoice(FCataclysmDungeonBribe::ButtonNameFor(DungeonId),
					  FText::FromString(FCataclysmDungeonBribe::ButtonText(ShownRun())),
					  /* bChosen */ false, /* bAvailable */ true);
	Button->OnChoiceClicked.AddDynamic(this, &UCataclysmCityScreenWidget::HandleBribeClicked);
	DetailBox->AddChild(Button);
	BribeButtons.Add(Button);
}

void UCataclysmCityScreenWidget::HandleBribeClicked(FName Value)
{
	// A REFUSAL IS SHOWN ON THE LINE A REFUSED UPGRADE OR WALK USES, and the screen redrawn either way.
	Refusal = FCataclysmDungeonBribe::Bribe(this, ShownRun(), FCataclysmDungeonBribe::DungeonForButton(Value));
	Refresh();
}

void UCataclysmCityScreenWidget::ClickBribeForTests(int32 DungeonId)
{
	HandleBribeClicked(FCataclysmDungeonBribe::ButtonNameFor(DungeonId));
}

void UCataclysmCityScreenWidget::ClickWalkForTests(int32 DungeonId)
{
	// THE NAME THE BUTTON WOULD CARRY, so this goes through the same lookup a
	// real press does, as `UCataclysmEmpireMapWidget::ClickCityForTests` does.
	HandleWalkClicked(FCataclysmWalkFromTheScreen::ButtonNameFor(DungeonId));
}

// ---------------------------------------------------------------------------
// What a test can read
// ---------------------------------------------------------------------------

FString UCataclysmCityScreenWidget::TitleText() const
{
	return TitleLabel ? TitleLabel->GetText().ToString() : FString();
}

FString UCataclysmCityScreenWidget::StatusText() const
{
	return StatusLabel ? StatusLabel->GetText().ToString() : FString();
}

FString UCataclysmCityScreenWidget::SlotsText() const
{
	return SlotsLabel ? SlotsLabel->GetText().ToString() : FString();
}

FString UCataclysmCityScreenWidget::RefusalText() const
{
	return RefusalLabel ? RefusalLabel->GetText().ToString() : FString();
}

FName UCataclysmCityScreenWidget::OfferRowName(int32 Index) const
{
	return OfferRowNames.IsValidIndex(Index) ? OfferRowNames[Index] : NAME_None;
}

FString UCataclysmCityScreenWidget::DetailRowText(int32 Index) const
{
	if (!DetailRows.IsValidIndex(Index) || DetailRows[Index] == nullptr)
	{
		return FString();
	}

	return DetailRows[Index]->GetText().ToString();
}

void UCataclysmCityScreenWidget::ClickOfferForTests(FName RowName)
{
	// THE HANDLER THE BUTTON CALLS, not a copy of it. A test that reimplemented
	// buying would pass while the button did something else.
	HandleOfferClicked(RowName);
}
