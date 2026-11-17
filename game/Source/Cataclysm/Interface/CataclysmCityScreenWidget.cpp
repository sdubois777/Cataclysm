// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmCityScreenWidget.h"

#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Data/CataclysmCityUpgradeMapping.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Interface/CataclysmChoiceButton.h"
#include "Interface/CataclysmWalkFromTheScreen.h"
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

	if (SlotsLabel)
	{
		SlotsLabel->SetText(FText::FromString(
			UCataclysmCityScreenLayout::SlotsTextFor(Run, CityId)));
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
		 UCataclysmCityScreenLayout::OffersFor(Run, CityId))
	{
		// ONLY WHAT CAN BE BOUGHT GETS A BUTTON. What the city already has is a
		// row under "What this city has", and what does nothing yet is a row in
		// its own section; neither is a thing to press.
		if (!Offer.bCanBuy)
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
						  /* bChosen */ false, /* bAvailable */ true);

		Button->OnChoiceClicked.AddDynamic(
			this, &UCataclysmCityScreenWidget::HandleOfferClicked);

		OfferBox->AddChild(Button);

		OfferButtons.Add(Button);
		OfferRowNames.Add(Offer.RowName);
	}
}

void UCataclysmCityScreenWidget::HandleOfferClicked(FName Value)
{
	UCataclysmEmpireRun* Run = ShownRun();
	if (Run == nullptr)
	{
		return;
	}

	const FCataclysmCityUpgrade Upgrade =
		UCataclysmCityUpgradeMapping::MakeFromTable(Value);

	const ECataclysmCityUpgradeResult Result =
		Run->BuyCityUpgrade(CityId, Upgrade);

	// A REFUSAL IS SHOWN RATHER THAN SWALLOWED. Only a buyable upgrade is given
	// a button, so a refusal here means the city changed underneath the screen
	// -- it fell, or another slot was spent -- and a player who pressed a button
	// and saw nothing happen would reasonably think the screen was broken.
	Refusal = Result == ECataclysmCityUpgradeResult::Bought
				  ? FString()
				  : UCataclysmCityUpgradeRules::ResultText(Result);

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
	// hiding them would make the game look as though it has ten city upgrades
	// rather than twenty-four with fourteen waiting on other systems.
	TArray<FCataclysmCityUpgradeOffer> NotBuilt;

	for (const FCataclysmCityUpgradeOffer& Offer :
		 UCataclysmCityScreenLayout::OffersFor(Run, CityId))
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
	Refusal = FCataclysmWalkFromTheScreen::Walk(ShownRun(), DungeonMode(), DungeonId);

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
