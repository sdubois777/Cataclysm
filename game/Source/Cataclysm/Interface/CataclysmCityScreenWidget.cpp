// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmCityScreenWidget.h"

#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Data/CataclysmCityUpgradeMapping.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Interface/CataclysmChoiceButton.h"
#include "Player/CataclysmGameInstance.h"

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

	AddRow(UCataclysmCityScreenLayout::DungeonHeading(Dungeons.Num()),
		   /* bIsHeading */ true, /* bDim */ false);

	for (const FString& Line : Dungeons)
	{
		AddRow(Line, /* bIsHeading */ false, /* bDim */ false);
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
